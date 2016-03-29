#include "gb-include.h"

#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Stats.h"
#include "Statsdb.h"
#include "Ads.h"
#include "Query.h"
#include "Speller.h"
#include "Msg40.h"
#include "Pages.h"
#include "Highlight.h"
#include "SearchInput.h"
#include <math.h>
#include "SafeBuf.h"
#include "iana_charset.h"
#include "Pos.h"
#include "Bits.h"
#include "AutoBan.h"
#include "sort.h"
#include "LanguageIdentifier.h"
#include "LanguagePages.h"
#include "LangList.h"
#include "CountryCode.h"
#include "Unicode.h"
#include "XmlDoc.h" // GigabitInfo class
#include "Posdb.h" // MAX_TOP definition
#include "PageResults.h"
#include "Proxy.h"

static bool printSearchFiltersBar ( SafeBuf *sb , HttpRequest *hr ) ;
static bool printMenu ( SafeBuf *sb , int32_t menuNum , HttpRequest *hr ) ;

//static void gotSpellingWrapper ( void *state ) ;
static void gotResultsWrapper  ( void *state ) ;
//static void gotAdsWrapper      ( void *state ) ;
static void gotState           ( void *state ) ;
static bool gotResults         ( void *state ) ;

bool replaceParm ( char *cgi , SafeBuf *newUrl , HttpRequest *hr ) ;
bool replaceParm2 ( char *cgi , SafeBuf *newUrl , 
		    char *oldUrl , int32_t oldUrlLen ) ;


bool printCSVHeaderRow ( SafeBuf *sb , State0 *st , int32_t ct ) ;

bool printJsonItemInCSV ( char *json , SafeBuf *sb , class State0 *st ) ;

bool printPairScore ( SafeBuf *sb , SearchInput *si , PairScore *ps ,
		      Msg20Reply *mr , Msg40 *msg40 , bool first ) ;

bool printScoresHeader ( SafeBuf *sb ) ;

bool printMetaContent ( Msg40 *msg40 , int32_t i ,State0 *st, SafeBuf *sb );

bool printSingleScore ( SafeBuf *sb , SearchInput *si , SingleScore *ss ,
			Msg20Reply *mr , Msg40 *msg40 ) ;

bool printDmozEntry ( SafeBuf *sb ,
		      int32_t catId ,
		      bool direct ,
		      char *dmozTitle ,
		      char *dmozSummary ,
		      char *dmozAnchor ,
		      SearchInput *si );

bool sendReply ( State0 *st , char *reply ) {

	int32_t savedErr = g_errno;

	TcpSocket *sock = st->m_socket;
	if ( ! sock ) { 
		log("results: not sending back results on an empty socket."
		    "socket must have closed on us abruptly.");
		//char *xx=NULL;*xx=0; }
	}
	SearchInput *si = &st->m_si;
	char *ct = "text/html";
	if ( si && si->m_format == FORMAT_XML ) ct = "text/xml"; 
	if ( si && si->m_format == FORMAT_JSON ) ct = "application/json";
	if ( si && si->m_format == FORMAT_CSV ) ct = "text/csv";
	char *charset = "utf-8";

	char format = si->m_format;

	// . filter anything < 0x20 to 0x20 to keep XML legal
	// . except \t, \n and \r, they're ok
	// . gotta set "f" down here in case it realloc'd the buf
	if ( format == FORMAT_XML && reply ) {
		unsigned char *f = (unsigned char *)reply;
		for ( ; *f ; f++ ) 
			if ( *f < 0x20 && *f!='\t' && *f!='\n' && *f!='\r' ) 
				*f = 0x20;
	}


	int32_t rlen = 0;
	if ( reply ) rlen = gbstrlen(reply);
	logf(LOG_DEBUG,"gb: sending back %"INT32" bytes",rlen);

	// . use light brown if coming directly from an end user
	// . use darker brown if xml feed
	int32_t color = 0x00b58869;
	if ( si->m_format != FORMAT_HTML )color = 0x00753d30 ;
	int64_t nowms = gettimeofdayInMilliseconds();
	int64_t took  = nowms - st->m_startTime ;
	g_stats.addStat_r ( took            ,
			    st->m_startTime , 
			    nowms,
			    color ,
			    STAT_QUERY );

	// add to statsdb, use # of qterms as the value/qty
	g_statsdb.addStat ( 0,
			    "query",
			    st->m_startTime,
			    nowms,
			    si->m_q.m_numTerms);

	// . log the time
	// . do not do this if g_errno is set lest m_sbuf1 be bogus b/c
	//   it failed to allocate its buf to hold terminating \0 in
	//   SearchInput::setQueryBuffers()
	if ( ! g_errno && st->m_took >= g_conf.m_logQueryTimeThreshold ) {
		logf(LOG_TIMING,"query: Took %"INT64" ms for %s. results=%"INT32"",
		     st->m_took,
		     si->m_sbuf1.getBufStart(),
		     st->m_msg40.getNumResults());
	}

	//bool xml = si->m_xml;

	g_stats.logAvgQueryTime(st->m_startTime);

	//log("results: debug: in sendReply deleting st=%"PTRFMT,(PTRTYPE)st);

	if ( ! savedErr ) { // g_errno ) {
		g_stats.m_numSuccess++;
		// . one hour cache time... no 1000 hours, basically infinite
		// . no because if we redo the query the results are cached
		int32_t cacheTime = 3600;//*1000;
		// no... do not use cache
		cacheTime = -1;
		// the "Check it" link on add url uses &usecache=0 to tell
		// the browser not to use its cache...
		//if ( hr->getLong("usecache",-1) == 0 ) cacheTime = 0;
		//
		// send back the actual search results
		//
		if ( sock )
		g_httpServer.sendDynamicPage(sock,
					     reply,
					     rlen,//gbstrlen(reply),
					     // don't let the ajax re-gen
					     // if they hit the back button!
					     // so make this 1 hour, not 0
					     cacheTime, // cachetime in secs
					     false, // POSTReply?
					     ct,
					     -1, // httpstatus -1 -> 200
					     NULL, // cookieptr
					     charset );

		// free st after sending reply since "st->m_sb" = "reply"
		mdelete(st, sizeof(State0), "PageResults2");
		delete st;
		return true;
	}
	// error otherwise
	if ( savedErr != ENOPERM && savedErr != EQUERYINGDISABLED ) 
		g_stats.m_numFails++;

	mdelete(st, sizeof(State0), "PageResults2");
	delete st;

	/*
	if ( format == FORMAT_XML ) {
		SafeBuf sb;
		sb.safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"UTF-8\" ?>\n"
			      "<response>\n"
			      "\t<errno>%"INT32"</errno>\n"
			      "\t<errmsg>%s</errmsg>\n"
			      "</response>\n"
			      ,(int32_t)savedErr
			      ,mstrerror(savedErr)
			      );
		// clear it for sending back
		g_errno = 0;
		// send back as normal reply
		g_httpServer.sendDynamicPage(s,
					     sb.getBufStart(),
					     sb.length(),
					     0, // cachetime in secs
					     false, // POSTReply?
					     ct,
					     -1, // httpstatus -1 -> 200
					     NULL, // cookieptr
					     charset );
		return true;
	}
	*/

	// if we had a broken pipe from the browser while sending
	// them the search results, then we end up closing the socket fd
	// in TcpServer::sendChunk() > sendMsg() > destroySocket()
	if ( sock && sock->m_numDestroys ) {
		log("results: not sending back error on destroyed socket "
		    "sd=%"INT32"",sock->m_sd);
		return true;
	}

	int32_t status = 500;
	if (savedErr == ETOOMANYOPERANDS ||
	    savedErr == EBADREQUEST ||
	    savedErr == ENOPERM ||
	    savedErr == ENOCOLLREC) 
		status = 400;

	if ( sock )
	g_httpServer.sendQueryErrorReply(sock,
					 status,
					 mstrerror(savedErr),
					 format,//xml,
					 savedErr, 
					 "There was an error!");
	return true;
}

bool printCSSHead ( SafeBuf *sb , char format ) {
	sb->safePrintf(
			      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML "
			      "4.01 Transitional//EN\">\n"
			      //"<meta http-equiv=\"Content-Type\" "
			      //"content=\"text/html; charset=utf-8\">\n"
			      "<html>\n"
			      "<head>\n"
			      "<title>Gigablast Search Results</title>\n"
			      "<style><!--"
			      "body {"
			      "font-family:Arial, Helvetica, sans-serif;"
			      );

	sb->safePrintf(	      "color: #000000;"
			      "font-size: 12px;"
			      //"margin: 20px 5px;"
			      "}"
			      "a:link {color:#00c}"
			      "a:visited {color:#551a8b}"
			      "a:active {color:#f00}"
			      ".bold {font-weight: bold;}"
			      ".bluetable {background:#d1e1ff;"
			      "margin-bottom:15px;font-size:12px;}"
			      ".url {color:#008000;}"
			      ".cached, .cached a {font-size: 10px;"
			      "color: #666666;"
			      "}"
			      "table {"
			      "font-family:Arial, Helvetica, sans-serif;"
			      "color: #000000;"
			      "font-size: 12px;"
			      "}"
			      ".directory {font-size: 16px;}"
			      "-->\n"
			      "</style>\n"
			      "</head>\n"
			      );
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "msg" will be inserted into the access log for this request
bool sendPageResults ( TcpSocket *s , HttpRequest *hr ) {
	// . check for sdirt=4, this a site search on the given directory id
	// . need to pre-query the directory first to get the sites to search
	//   this will likely have just been cached so it should be quick
	// . then need to construct a site search query
	//int32_t rawFormat = hr->getLong("xml", 0); // was "raw"
	//int32_t xml = hr->getLong("xml",0);

	// what format should search results be in? default is html
	char format = hr->getReplyFormat();//getFormatFromRequest ( hr );

	// get the dmoz catid if given
	//int32_t searchingDmoz = hr->getLong("dmoz",0);

	//
	// DO WE NEED TO ALTER cr->m_siteListBuf for a widget?
	//
	// when a wordpress user changes the "Websites to Include" for
	// her widget, it should send a /search?sites=xyz.com&wpid=xxx
	// request here... 
	// so we need to remove her old sites and add in her new ones.
	// 
	/*
	  
	  MDW TURN BACK ON IN A DAY. do indexing or err pages first.

	// get wordpressid supplied with all widget requests
	char *wpid = hr->getString("wpid");
	// we have to add set &spidersites=1 which all widgets should do
	if ( wpid ) {
		// this returns NULL if cr->m_siteListBuf would be unchanged
		// because we already have the whiteListBuf sites in there
		// for this wordPressId (wpid)
		SafeBuf newSiteListBuf;
		makeNewSiteList( &si->m_whiteListBuf,
				 cr->m_siteListBuf ,
				 wpid ,
				 &newSiteListBuf);
		// . update the list of sites to crawl/search & show in widget
		// . if they give an empty list then allow that, stops crawling
		SafeBuf parmList;
		g_parms.addNewParmToList1 ( &parmList,
					    cr->m_collnum,
					    newSiteListBuf,
					    0,
					    "sitelist");
		// send the parms to all hosts in the network
		g_parms.broadcastParmList ( &parmList , 
					    NULL,//s,// state is socket i guess
					    NULL);//doneBroadcastingParms2 );
		// nothing left to do now
		return g_httpServer.sendDynamicPage(s,
						    "OK",//sb.getBufStart(),
						    2,//sb.length(),
						    cacheTime,//0,
						    false, // POST?
						    "text/html", 
						    200,  // httpstatus
						    NULL, // cookie
						    "UTF-8"); // charset
	}
	*/
	



	//
	// . send back page frame with the ajax call to get the real
	//   search results. do not do this if a "&dir=" (dmoz category)
	//   is given.
	// . if not matt wells we do not do ajax
	// . the ajax is just there to prevent bots from slamming me 
	//   with queries.
	//
	if ( hr->getLong("id",0) == 0 && 
	     format == FORMAT_HTML &&
	     g_conf.m_isMattWells ) {

		SafeBuf sb;
		printCSSHead ( &sb ,format );
		sb.safePrintf(
			      "<body "

			      "onLoad=\""
			      "var client = new XMLHttpRequest();\n"
			      "client.onreadystatechange = handler;\n"
			      //"var url='http://10.5.1.203:8000/search?q="
			      "var url='/search?q="
			      );
		int32_t  qlen;
		char *qstr = hr->getString("q",&qlen,"",NULL);
		// . crap! also gotta encode apostrophe since "var url='..."
		// . true = encodeApostrophes?
		sb.urlEncode2 ( qstr , true );
		// progate query language
		char *qlang = hr->getString("qlang",NULL,NULL);
		if ( qlang ) sb.safePrintf("&qlang=%s",qlang);
		// propagate "admin" if set
		int32_t admin = hr->getLong("admin",-1);
		if ( admin != -1 ) sb.safePrintf("&admin=%"INT32"",admin);
		// propagate showing of banned results
		if ( hr->getLong("sb",0) ) sb.safePrintf("&sb=1");
		// propagate list of sites to restrict query to
		int32_t sitesLen;
		char *sites = hr->getString("sites",&sitesLen,NULL);
		if ( sites ) {
			sb.safePrintf("&sites=");
			sb.urlEncode2 ( sites,true);
		}
		// propagate "prepend"
		char *prepend = hr->getString("prepend",NULL);
		if ( prepend ) {
			sb.safePrintf("&prepend=");
			sb.urlEncode(prepend);
		}
		// propagate "debug" if set
		int32_t debug = hr->getLong("debug",0);
		if ( debug ) sb.safePrintf("&debug=%"INT32"",debug);
		// propagate "s"
		int32_t ss = hr->getLong("s",-1);
		if ( ss > 0 ) sb.safePrintf("&s=%"INT32"",ss);
		// propagate "n"
		int32_t n = hr->getLong("n",-1);
		if ( n >= 0 ) sb.safePrintf("&n=%"INT32"",n);
		// Docs to Scan for Related Topics
		int32_t dsrt = hr->getLong("dsrt",-1);
		if ( dsrt >= 0 ) sb.safePrintf("&dsrt=%"INT32"",dsrt);
		// debug gigabits?
		int32_t dg = hr->getLong("dg",-1);
		if ( dg >= 0 ) sb.safePrintf("&dg=%"INT32"",dg);
		// show gigabits?
		//int32_t gb = hr->getLong("gigabits",1);
		//if ( gb >= 1 ) sb.safePrintf("&gigabits=%"INT32"",gb);
		// show banned results?
		int32_t showBanned = hr->getLong("sb",0);
		if ( showBanned ) sb.safePrintf("&sb=1");
		// propagate collection
		int32_t clen;
		char *coll = hr->getString("c",&clen,"",NULL);
		if ( coll ) sb.safePrintf("&c=%s",coll);
		// forward the "ff" family filter as well
		int32_t ff = hr->getLong("ff",0);
		if ( ff ) sb.safePrintf("&ff=%"INT32"",ff);
		// provide hash of the query so clients can't just pass in
		// a bogus id to get search results from us
		uint32_t h32 = hash32n(qstr);
		if ( h32 == 0 ) h32 = 1;
		// add this timestamp so when we hit back button this
		// parent page will be cached and so will this ajax url.
		// but if they hit reload the parent page reloads with a
		// different ajax url because "rand" is different
		uint64_t rand64 = gettimeofdayInMillisecondsLocal();
		sb.safePrintf("&id=%"UINT32"&rand=%"UINT64"';\n"
			      "client.open('GET', url );\n"
			      "client.send();\n"
			      "\">"
			      , h32
			      , rand64
			      );
		//
		// . login bar
		// . proxy will replace it byte by byte with a login/logout
		//   link etc.
		//
		//g_proxy.insertLoginBarDirective(&sb);

		// 
		// logo header
		//
		printLogoAndSearchBox ( &sb , hr , -1,NULL ); // catId = -1
		//
		// script to populate search results
		//
		sb.safePrintf("<script type=\"text/javascript\">\n"
			      "function handler() {\n" 
			      "if(this.readyState == 4 ) {\n"
			      "document.getElementById('results').innerHTML="
			      "this.responseText;\n"
			      //"alert(this.status+this.statusText+"
			      //"this.responseXML+this.responseText);\n"
			      "}}\n"


			      // gigabit unhide function
			      "function ccc ( gn ) {\n"
			      "var e = document.getElementById('fd'+gn);\n"
			      "var f = document.getElementById('sd'+gn);\n"
			      "if ( e.style.display == 'none' ){\n"
			      "e.style.display = '';\n"
			      "f.style.display = 'none';\n"
			      "}\n"
			      "else {\n"
			      "e.style.display = 'none';\n"
			      "f.style.display = '';\n"
			      "}\n"
			      "}\n"
			      "</script>\n"



			      // put search results into this div
			      "<div id=results>"
			      "<img height=50 width=50 "
			      "src=http://www.gigablast.com/gears.gif>"
			      "<br/>"
			      "<br/>"
			      "<b>"
			      "Waiting for results... "
			      "</b>"
			      "<br/>"
			      "<br/>"
			      "Please be a little "
			      "patient I am trying to get more servers."
			      "</div>\n"


			      "<br/>"
			      "<center>"
			      "<font color=gray>"
			      "Copyright &copy; 2014. "
			      "All Rights Reserved.<br/>"
			      "Powered by the "
			      "<a href=\"http://www.gigablast.com/\">"
			      "GigaBlast</a> open source search engine."
			      "</font>"
			      "</center>\n"

			      "</body>\n"
			      "</html>\n"
			      );
		// one hour cache time... no 1000 hours, basically infinite
		int32_t cacheTime = 3600; // *1000;
		//if ( hr->getLong("usecache",-1) == 0 ) cacheTime = 0;
		//
		// send back the parent stub containing the ajax
		//
		return g_httpServer.sendDynamicPage(s,
						    sb.getBufStart(),
						    sb.length(),
						    cacheTime,//0,
						    false, // POST?
						    "text/html", 
						    200,  // httpstatus
						    NULL, // cookie
						    "UTF-8"); // charset
	}


	// make a new state
	State0 *st;
	try { st = new (State0); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("query: Query failed. "
		    "Could not allocate %"INT32" bytes for query. "
		    "Returning HTTP status of 500.",(int32_t)sizeof(State0));
		g_stats.m_numFails++;
		return g_httpServer.sendQueryErrorReply
			(s,500,mstrerror(g_errno),
			 format, g_errno, "Query failed.  "
			 "Could not allocate memory to execute a search.  "
			 "Please try later." );
	}
	mnew ( st , sizeof(State0) , "PageResults2" );

	// init some stuff
	st->m_didRedownload    = false;
	st->m_xd               = NULL;
	st->m_oldContentHash32 = 0;

	// copy yhits
	if ( ! st->m_hr.copy ( hr ) )
		return sendReply ( st , NULL );

	// set this in case SearchInput::set fails!
	st->m_socket = s;

	// record timestamp so we know if we got our socket closed and swapped
	st->m_socketStartTimeHack = s->m_startTime;

	// save this count so we know if TcpServer.cpp calls destroySocket(s)
	st->m_numDestroys = s->m_numDestroys;

	// . parse it up
	// . this returns false and sets g_errno and, maybe, g_msg on error
	SearchInput *si = &st->m_si;
	if ( ! si->set ( s ,
			 // si just copies the ptr into the httprequest
			 // into stuff like SearchInput::m_defaultSortLanguage
			 // so do not use the "hr" on the stack. SearchInput::
			 // m_hr points to the hr we pass into
			 // SearchInput::set
			 &st->m_hr ) ) {
			 //&st->m_q ) ) {
		log("query: set search input: %s",mstrerror(g_errno));
		if ( ! g_errno ) g_errno = EBADENGINEER;
		return sendReply ( st, NULL );
	}

	// for debug
	si->m_q.m_st0Ptr = (char *)st;

	int32_t  codeLen = 0;
	char *code = hr->getString("code", &codeLen, NULL);
	// allow up to 1000 results per query for paying clients
	CollectionRec *cr = si->m_cr;

	// save collnum now
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;

	int32_t defHdr = 1;

	// default is no header for diffbot only
	if ( cr->m_isCustomCrawl ||  strcmp(cr->m_coll,"GLOBAL-INDEX") == 0 )
		defHdr = 0;

	// you have to say "&header=1" to get back the header for json now.
	// later on maybe it will default to on.
	st->m_header = hr->getLong("header",defHdr);

	// take this out here as well!
	// limit here
	// int32_t maxpp = cr->m_maxSearchResultsPerQuery ;
	// if ( si->m_docsWanted > maxpp &&
	//      // disable serp max per page for custom crawls
	//      ! cr->m_isCustomCrawl )
	// 	si->m_docsWanted = maxpp;

	// BUT if it is a custom diffbot crawl with no &stream=1 option,
	// then to prevent a results page of 1.6GB, limit it here
	if ( si->m_docsWanted > 1000 && ! si->m_streamResults ) {
	 	si->m_docsWanted = 1000;
		log("query: limiting query %s without &stream=1 option to "
		    "%"INT32" results.",st->m_si.m_displayQuery,1000);
	}

        st->m_numDocIds = si->m_docsWanted;

	// watch out for cowboys
	//if(si->m_firstResultNum>=si->m_maxResults) return sendReply(st,NULL);

	// save state in TcpSocket's m_tmp ptr for debugging. in case 
	// we lose our string of control and Msg40::getResults() never 
	// comes back.
	s->m_tmp = (char *)st;
	// add query stat
	st->m_startTime = gettimeofdayInMilliseconds();
	// reset
	st->m_errno = 0;

	// debug msg
	log ( LOG_DEBUG , "query: Getting search results for q=%s",
	      st->m_si.m_displayQuery);

	// assume we'll block
	st->m_gotResults = false;
	st->m_gotAds     = false;
	st->m_gotSpell   = false;

	// reset
	st->m_printedHeaderRow = false;

	int32_t ip = s->m_ip;
	int32_t uipLen;
	char *uip = hr->getString("uip", &uipLen, NULL);
	char testBufSpace[2048];
	SafeBuf testBuf(testBufSpace, 1024);
	if( g_conf.m_doAutoBan &&
	    !g_autoBan.hasPerm(ip, 
			       code, codeLen, 
			       uip, uipLen, 
			       s, 
			       hr,
			       &testBuf,
			       false)) { // just check? no incrementing counts
		if ( uip )
			log("results: returning EBUYFEED for uip=%s",uip);
		g_errno = EBUYFEED;
		return sendReply(st,NULL);
	}

	// for now disable queries
	if ( ! g_conf.m_queryingEnabled ) {
		g_errno = EQUERYINGDISABLED;
		return sendReply(st,NULL);
	}

	// filter that one query causing the memleak for now
	// if ( strstr(si->m_q.m_orig,
	// 	    "type:json AND ((((query=humanLanguage:en") ) {
	// 	g_errno = EQUERYINGDISABLED;
	// 	return sendReply(st,NULL);
	// }
		
	// LAUNCH ADS
	// . now get the ad space for this query
	// . don't get ads if we're not on the first page of results
	// . query must be NULL terminated
	st->m_gotAds = true;
	/*
	if (si->m_adFeedEnabled && ! si->m_xml && si->m_docsWanted > 0) {
                int32_t pageNum = (si->m_firstResultNum/si->m_docsWanted) + 1;
		st->m_gotAds = st->m_ads.
			getAds(si->m_displayQuery    , //query
			       si->m_displayQueryLen , //q len
			       pageNum               , //page num
                               si->m_queryIP         ,
			       si->m_coll2           , //coll
			       st                    , //state
			       gotAdsWrapper         );//clbk
        }
	*/

	// LAUNCH SPELLER
	// get our spelling correction if we should (spell checker)
	st->m_gotSpell = true;
	st->m_spell[0] = '\0';
	/*
	if ( si->m_spellCheck && 
	     cr->m_spellCheck && 
	     g_conf.m_doSpellChecking ) {
		st->m_gotSpell = g_speller.
			getRecommendation( &st->m_q,          // Query
					   si->m_spellCheck,  // spellcheck
					   st->m_spell,       // Spell buffer
					   MAX_FRAG_SIZE,     // spell buf size
					   false,      // narrow search?
					   NULL,//st->m_narrow  // narrow buf
					   MAX_FRAG_SIZE,    // narrow buf size
					   NULL,// num of narrows  ptr
					   st,               // state
					   gotSpellingWrapper );// callback
	}
	*/

	// LAUNCH RESULTS

	// . get some results from it
	// . this returns false if blocked, true otherwise
	// . it also sets g_errno on error
	// . use a niceness of 0 for all queries so they take precedence
	//   over the indexing process
	// . this will copy our passed "query" and "coll" to it's own buffer
	// . we print out matching docIds to int32_t if m_isDebug is true
	// . no longer forward this, since proxy will take care of evenly
	//   distributing its msg 0xfd "forward" requests now
	st->m_gotResults=st->m_msg40.getResults(si,false,st,gotResultsWrapper);
	// save error
	st->m_errno = g_errno;

	//log("results: debug: new state=%"PTRFMT,(PTRTYPE)st);

	// wait for ads and spellcheck and results?
	if ( !st->m_gotAds || !st->m_gotSpell || !st->m_gotResults )
		return false;
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	bool status2 = gotResults ( st );

	return status2;
}

// if returned json result is > maxagebeforedownload then we redownload the
// page and if its checksum has changed we return empty results
void doneRedownloadingWrapper ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	// resume
	gotResults ( st );
}

/*
void gotSpellingWrapper( void *state ){
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	// log the error first
	if ( g_errno ) log("query: speller: %s.",mstrerror(g_errno));
	// clear any error cuz spellchecks aren't needed
	g_errno = 0;
	st->m_gotSpell = true;
	gotState(st);
}
*/

void gotResultsWrapper ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	// save error
	st->m_errno = g_errno;
	// mark as gotten
	st->m_gotResults = true;
	gotState (st);
}

/*
void gotAdsWrapper ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	// mark as gotten
	st->m_gotAds = true;
	// log the error first
	if ( g_errno ) log("query: adclient: %s.",mstrerror(g_errno));
	// clear any error cuz ads aren't needed
	g_errno = 0;
	gotState (st);;
}
*/

void gotState ( void *state ){
	// cast our State0 class from this
	State0 *st = (State0 *) state;
	if ( !st->m_gotAds || !st->m_gotSpell || !st->m_gotResults )
		return;
	// we're ready to go
	gotResults ( state );
}


// print all sentences containing this gigabit (fast facts) (nuggabits)
static bool printGigabitContainingSentences ( State0 *st,
					      SafeBuf *sb , 
					      Msg40 *msg40 , 
					      Gigabit *gi , 
					      SearchInput *si ,
					      Query *gigabitQuery ,
					      int32_t gigabitId ) {

	//static int32_t s_gigabitCount = 0;
	

	char format = si->m_format;

	HttpRequest *hr = &st->m_hr;
	CollectionRec *cr = si->m_cr;//g_collectiondb.getRec(collnum );

	int32_t numOff;
	int32_t revert;
	int32_t spaceOutOff;

	if ( format == FORMAT_HTML ) {
		sb->safePrintf("<nobr><b>");
		//"<img src=http://search.yippy.com/"
		//"images/new/button-closed.gif><b>");

		// make a new query
		sb->safePrintf("<a href=\"/search?c=%s&q=",cr->m_coll);
		sb->urlEncode(gi->m_term,gi->m_termLen);
		sb->safeMemcpy("+|+",3);
		char *q = hr->getString("q",NULL,"");
		sb->urlEncode(q);
		sb->safePrintf("\">");
		sb->safeMemcpy(gi->m_term,gi->m_termLen);
		sb->safePrintf("</a></b>");
		sb->safePrintf(" <font color=gray size=-1>");
		numOff = sb->m_length;
		sb->safePrintf("      ");//,gi->m_numPages);
		sb->safePrintf("</font>");
		sb->safePrintf("</b>");
		if ( si->m_isMasterAdmin && 1 == 2 ) 
			sb->safePrintf("[%.0f]{%"INT32"}",
				       gi->m_gbscore,
				       gi->m_minPop);

		revert = sb->length();

		sb->safePrintf("<font color=blue style=align:right;>"
			       "<a style=cursor:hand;cursor:pointer; "
			       "onclick=ccc(%"INT32");>"
			       , gigabitId // s_gigabitCount 
			       );
		spaceOutOff = sb->length();
		sb->safePrintf( "%c%c%c",
				0xe2,
				0x87,
				0x93);
		sb->safePrintf(//"[more]"
			       "</a></font>");
	

		sb->safePrintf("</nobr>"); // <br>
	}

	if ( format == FORMAT_XML ) {
		sb->safePrintf("\t\t<gigabit>\n");
		sb->safePrintf("\t\t\t<term><![CDATA[");
		sb->cdataEncode(gi->m_term,gi->m_termLen);
		sb->safePrintf("]]></term>\n");
		sb->safePrintf("\t\t\t<score>%f</score>\n",gi->m_gbscore);
		sb->safePrintf("\t\t\t<minPop>%"INT32"</minPop>\n",gi->m_minPop);
	}

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("\t{\n");
		//sb->safePrintf("\t\"gigabit\":{\n");
		sb->safePrintf("\t\t\"term\":\"");
		sb->jsonEncode(gi->m_term,gi->m_termLen);
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\t\"score\":%f,\n",gi->m_gbscore);
		sb->safePrintf("\t\t\"minPop\":%"INT32",\n",gi->m_minPop);
	}

	// get facts
	int32_t numNuggets = 0;
	int32_t numFacts = msg40->m_factBuf.length() / sizeof(Fact);
	Fact *facts = (Fact *)msg40->m_factBuf.getBufStart();
	bool first = true;
	bool second = false;
	bool printedSecond = false;
	//int64_t lastDocId = -1LL;
	int32_t saveOffset = 0;
	for ( int32_t i = 0 ; i < numFacts ; i++ ) {
		Fact *fi = &facts[i];

		// if printed for a higher scoring gigabit, skip
		if ( fi->m_printed ) continue;

		// check gigabit match
		int32_t k; for ( k = 0 ; k < fi->m_numGigabits ; k++ ) 
			if ( fi->m_gigabitPtrs[k] == gi ) break;
		// skip this fact/sentence if does not contain gigabit
		if ( k >= fi->m_numGigabits ) continue;

		// do not print if no period at end
		char *s = fi->m_fact;
		char *e = s + fi->m_factLen;
		if ( e[-1] != '*' ) continue;
		e--;

	again:

		// first time, print in the single fact div
		if ( first && format == FORMAT_HTML ) {
			sb->safePrintf("<div "
				       //"style=\"border:1px lightgray solid;\"
				      "id=fd%"INT32">",gigabitId);//s_gigabitCount);
		}

		if ( second && format == FORMAT_HTML ) {
			sb->safePrintf("<div style=\"max-height:300px;"
				      "display:none;"
				      "overflow-x:hidden;"
				      "overflow-y:auto;"//scroll;"
				       //"border:1px lightgray solid; "
				       "\" "
				      "id=sd%"INT32">",gigabitId);//s_gigabitCount);
			printedSecond = true;
		}

		Msg20Reply *reply = fi->m_reply;

		// ok, print it out
		if ( ! first && ! second && format == FORMAT_HTML ) {
			//if ( reply->m_docId != lastDocId ) 
			sb->safePrintf("<br><br>\n");
			//else {
			//	sb->setLength ( saveOffset );
			//	sb->safePrintf("<br><br>\n");
			//}
		}
		else {
			//sb->safePrintf("");
		}


		numNuggets++;

		// print the fast fact (sentence)
		//sb->safeMemcpy ( s , e-s );

		// let's highlight with gigabits and query terms
		SafeBuf tmpBuf;
		Highlight h;
		h.set ( &tmpBuf , // print it out here
			s , // content
			e - s , // len
			si->m_queryLangId , // from m_defaultSortLang
			gigabitQuery , // the gigabit "query" in quotes
			true , // stemming? -- unused
			false , // use anchors?
			NULL , // baseurl
			"<u>", // front tag
			"</u>", // back tag
			0 , // fieldCode
			0  ); // niceness
		// now highlight the original query as well but in black bold
		SafeBuf tmpBuf2;
		h.set ( &tmpBuf2 , // print it out here
			tmpBuf.getBufStart() , // content
			tmpBuf.length() , // len
			si->m_queryLangId , // from m_defaultSortLang
			&si->m_q , // the regular query
			true , // stemming? -- unused
			false , // use anchors?
			NULL , // baseurl
			"<b>" , // front tag
			"</b>", // back tag
			0 , // fieldCode
			0  ); // niceness
		

		int32_t dlen; char *dom = getDomFast(reply->ptr_ubuf,&dlen);

		// print the sentence
		if ( format == FORMAT_HTML )
			sb->safeStrcpy(tmpBuf2.getBufStart());

		if ( format == FORMAT_XML ) {
			sb->safePrintf("\t\t\t<instance>\n"
				       "\t\t\t\t<sentence><![CDATA[");
			sb->cdataEncode(tmpBuf2.getBufStart());
			sb->safePrintf("]]></sentence>\n");
			sb->safePrintf("\t\t\t\t<url><![CDATA[");
			sb->cdataEncode(reply->ptr_ubuf);
			sb->safePrintf("]]></url>\n");
			sb->safePrintf("\t\t\t\t<domain><![CDATA[");
			sb->cdataEncode(dom,dlen);
			sb->safePrintf("]]></domain>\n");
			sb->safePrintf("\t\t\t</instance>\n");
		}

		if ( format == FORMAT_JSON ) {
			sb->safePrintf("\t\t\"instance\":{\n"
				       "\t\t\t\"sentence\":\"");
			sb->jsonEncode(tmpBuf2.getBufStart());
			sb->safePrintf("\",\n");

			sb->safePrintf("\t\t\t\"url\":\"");
			sb->jsonEncode(reply->ptr_ubuf);
			sb->safePrintf("\",\n");

			sb->safePrintf("\t\t\t\"domain\":\"");
			sb->jsonEncode(dom,dlen);
			sb->safePrintf("\"\n");
			sb->safePrintf("\t\t},\n");
		}


		fi->m_printed = 1;
		saveOffset = sb->length();
		if ( format == FORMAT_HTML )
			sb->safePrintf(" <a href=/get?c=%s&cnsp=0&"
				       "strip=0&d=%"INT64">",
				       cr->m_coll,reply->m_docId);

		if ( format == FORMAT_HTML )
			sb->safeMemcpy(dom,dlen);

		if ( format == FORMAT_HTML )
			sb->safePrintf("</a>\n");

		//lastDocId = reply->m_docId;

		if ( first && format == FORMAT_HTML ) {
			sb->safePrintf("</div>");
		}

		if ( second ) {
			second = false;
		}

		if ( first ) {
			first = false;
			second = true;
			// print first gigabit all over again but in 2nd div
			goto again;
		}
	}

	if ( format == FORMAT_XML ) 
		sb->safePrintf("\t\t</gigabit>\n");

	if ( format == FORMAT_JSON ) {
		// remove last ,\n
		sb->m_length -= 2;
		// replace with just \n
		// end the gigabit
		sb->safePrintf("\n\t},\n");
	}

	// all done if not html
	if ( format != FORMAT_HTML )
		return true;

	// we counted the first one twice since we had to throw it into
	// the hidden div too!
	if ( numNuggets > 1 ) numNuggets--;

	// do not print the double down arrow if no nuggets printed
	if ( numNuggets <= 0 ) {
		sb->m_length = revert;
		sb->safePrintf("</nobr>");
	}
	// just remove down arrow if only 1...
	else if ( numNuggets == 1 ) {
		char *dst = sb->getBufStart()+spaceOutOff;
		dst[0] = ' ';
		dst[1] = ' ';
		dst[2] = ' ';
	}
	// store the # of nuggets in ()'s like (10 )
	else {
		char tmp[10];
		sprintf(tmp,"(%"INT32")",numNuggets);
		char *src = tmp;
		// starting storing digits after "( "
		char *dst = sb->getBufStart()+numOff;
		int32_t srcLen = gbstrlen(tmp);
		if ( srcLen > 5 ) srcLen = 5;
		for ( int32_t k = 0 ; k < srcLen ; k++ ) 
			dst[k] = src[k];
	}

	//s_gigabitCount++;

	if ( printedSecond ) {
		sb->safePrintf("</div>");
	}

	return true;
}

/*
// print all sentences containing this gigabit
static bool printGigabit ( State0 *st,
			   SafeBuf *sb , 
			   Msg40 *msg40 , 
			   Gigabit *gi , 
			   SearchInput *si ) {

	//static int32_t s_gigabitCount = 0;

	sb->safePrintf("<nobr><b>");
	//"<img src=http://search.yippy.com/"
	//"images/new/button-closed.gif><b>");

	HttpRequest *hr = &st->m_hr;

	// make a new query
	sb->safePrintf("<a href=\"/search?gigabits=1&q=");
	sb->urlEncode(gi->m_term,gi->m_termLen);
	sb->safeMemcpy("+|+",3);
	char *q = hr->getString("q",NULL,"");
	sb->urlEncode(q);
	sb->safePrintf("\">");
	sb->safeMemcpy(gi->m_term,gi->m_termLen);
	sb->safePrintf("</a></b>");
	sb->safePrintf(" <font color=gray size=-1>");
	//int32_t numOff = sb->m_length;
	// now the # of pages not nuggets
	sb->safePrintf("(%"INT32")",gi->m_numPages);
	sb->safePrintf("</font>");
	sb->safePrintf("</b>");
	if ( si->m_isMasterAdmin ) 
		sb->safePrintf("[%.0f]{%"INT32"}",
			      gi->m_gbscore,
			      gi->m_minPop);
	// that's it for the gigabit
	sb->safePrintf("<br>");

	return true;
}
*/

class StateAU {
public:
	SafeBuf m_metaListBuf;
	Msg4    m_msg4;
};
	

void freeMsg4Wrapper( void *st ) {
	StateAU *stau = (StateAU *)st;
	mdelete(stau, sizeof(StateAU), "staud");
	delete stau;
}

// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool gotResults ( void *state ) {
	// cast our State0 class from this
	State0 *st = (State0 *) state;

	int64_t nowMS = gettimeofdayInMilliseconds();
	// log the time
	int64_t took = nowMS - st->m_startTime;
	// record that
	st->m_took = took;

	//log("results: debug: in gotResults state=%"PTRFMT,(PTRTYPE)st);

	// grab the query
	Msg40 *msg40 = &(st->m_msg40);
	//char  *q    = msg40->getQuery();
	//int32_t   qlen = msg40->getQueryLen();

	SearchInput *si = &st->m_si;

	// if we lost the socket because we were streaming and it
	// got closed from a broken pipe or something, then Msg40.cpp
	// will set st->m_socket to NULL if the fd ends up ending closed
	// because someone else might be using it and we do not want to
	// mess with their TcpSocket settings.
	if ( ! st->m_socket ) {
		log("results: socket is NULL. sending failed.");
		return sendReply(st,NULL);
	}

	// if in streaming mode and we never sent anything and we had
	// an error, then send that back. we never really entered streaming
	// mode in that case. this happens when someone deletes a coll
	// and queries it immediately, then each shard reports ENOCOLLREC.
	// it was causing a socket to be permanently stuck open.
	if ( g_errno &&
	     si->m_streamResults &&
	     st->m_socket->m_totalSent == 0 )
	       return sendReply(st,NULL);


	// if we skipped a shard because it was dead, usually we provide
	// the results anyway, but if this switch is true then return an
	// error code instead. this is the 'all or nothing' switch.
	if ( msg40->m_msg3a.m_skippedShards > 0 &&
	     ! g_conf.m_returnResultsAnyway ) {
	       char reply[256];
	       sprintf ( reply , 
			 "%"INT32" shard(s) out of %"INT32" did not "
			 "respond to query."
			 , msg40->m_msg3a.m_skippedShards
			 , g_hostdb.m_numShards );
	       g_errno = ESHARDDOWN;
	       return sendReply(st,reply);
	}


	// if already printed from Msg40.cpp, bail out now
	if ( si->m_streamResults ) {
		// this will be our final send
		if ( st->m_socket->m_streamingMode ) {
			log("res: socket still in streaming mode. wtf? err=%s",
			    mstrerror(g_errno));
			st->m_socket->m_streamingMode = false;
		}
		log("msg40: done streaming. nuking state=0x%"PTRFMT" "
		    "tcpsock=0x%"PTRFMT" "
		    "sd=%i "
		    "msg40=0x%"PTRFMT" q=%s. "
		    "msg20sin=%i msg20sout=%i sendsin=%i sendsout=%i "
		    "numrequests=%i numreplies=%i "
		    ,(PTRTYPE)st
		    ,(PTRTYPE)st->m_socket
		    ,(int)st->m_socket->m_sd
		    ,(PTRTYPE)msg40
		    ,si->m_q.m_orig

		    , msg40->m_numMsg20sIn
		    , msg40->m_numMsg20sOut
		    , msg40->m_sendsIn
		    , msg40->m_sendsOut
		    , msg40->m_numRequests
		    , msg40->m_numReplies

		    );

		// for some reason the socket still exists and will time out
		//g_tcpServer.destroySocket ( st->m_socket );

		// just let tcpserver nuke it, but don't double call
		// the callback, doneSendingWrapper9()... because msg40
		// will have been deleted!
		st->m_socket->m_callback = NULL;

		// fix this to try to fix double close i guess
		// if ( st->m_socket->m_sd > 0 )
		// 	st->m_socket->m_sd *= -1;

		mdelete(st, sizeof(State0), "PageResults2");
		delete st;
		return true;
	}

	// int16_tcuts
	//char        *coll    = si->m_coll2;
	//int32_t         collLen = si->m_collLen2;

	//collnum_t collnum = si->m_firstCollnum;

	// collection rec must still be there since SearchInput references 
	// into it, and it must be the SAME ptr too!
	CollectionRec *cr = si->m_cr;//g_collectiondb.getRec ( collnum );
	if ( ! cr ) { // || cr != si->m_cr ) {
		g_errno = ENOCOLLREC;
		return sendReply(st,NULL);
	}

	// this causes ooms everywhere, not a good fix
	if ( ! msg40->m_msg20 && ! si->m_docIdsOnly && msg40->m_errno ) {
	 	log("msg40: failed to get results q=%s",si->m_q.m_orig);
	 	//g_errno = ENOMEM;
		g_errno = msg40->m_errno;
	 	return sendReply(st,NULL);
	}




	//char *coll = cr->m_coll;

	/*
	//
	// BEGIN REDOWNLOAD LOGIC
	//

	////////////
	//
	// if caller wants a certain freshness we might have to redownload the
	// parent url to get the new json
	//
	////////////
	// get the first result
	Msg20 *m20first = msg40->m_msg20[0];
	int32_t mabr = st->m_hr.getLong("maxagebeforeredownload",-1);
	if ( mabr >= 0 && 
	     numResults > 0 &&
	     // only do this once
	     ! st->m_didRedownload &&
	     // need at least one result
	     m20first &&
	     // get the last spidered time from the msg20 reply of that result
	     m20first->m_r->m_lastSpidered - now > mabr ) {
		// make a new xmldoc to do the redownload
		XmlDoc *xd;
		try { xd = new (XmlDoc); }
		catch ( ... ) {
			g_errno = ENOMEM;
			log("query: Failed to alloc xmldoc.");
		}
		if ( g_errno ) return sendReply (st,NULL);
		mnew ( xd , sizeof(XmlDoc) , "mabrxd");
		// save it
		st->m_xd = xd;
		// get this
		st->m_oldContentHash32 = m20rep->m_contentHash32;
		// do not re-do redownload
		st->m_didRedownload = true;
		// set it
		xd->setUrl(parentUrl);
		xd->setCallback ( st , doneRedownloadingWrapper );
		// get the checksum
		if ( xd->getContentChecksum32Fast() == (void *)-1 )
			// return false if it blocked
			return false;
		// error?
		if ( g_errno ) return sendReply (st,NULL);
		// how did this not block
		log("page: redownload did not would block adding parent");
	}
	     
	// if we did the redownload and checksum changed, return 0 results
	if ( st->m_didRedownload ) {
		// get the doc we downloaded
		XmlDoc *xd = st->m_xd;
		// get it
		int32_t newHash32 = xd->getContentHash32();
		// log it
		if ( newHash32 != st->m_oldContentHash32 ) 
			// note it in logs for now
			log("results: content changed for %s",xd->m_firstUrl.m_url);
		// free it
		mdelete(xd, sizeof(XmlDoc), "mabrxd" );
		delete xd;
		// null it out so we don't try to re-free
		st->m_xd = NULL;
		// if content is significantly different, return 0 results
		if ( newHash32 != st->m_oldContentHash32 ) {
			SafeBuf sb;
			// empty json i guess
			sb.safePrintf("[]\n");
			return sendReply(st,sb.getBufStart());
		}
		// otherwise, print the diffbot json results, they are still valid
	}

	//
	// END REDOWNLOAD LOGIC
	//
	*/

	//
	// BEGIN ADDING URL
	//

	//////////
	//
	// if its a special request to get diffbot json objects for
	// a given parent url, it often contains the same url in "addurl"
	// to add as a spider request to spiderdb so that 
	// it gets spidered and processed through diffbot.
	//
	//////////
	char *addUrl = st->m_hr.getString("addurl",NULL);
	if ( addUrl ) { // && cr->m_isCustomCrawl ) {

		Url norm;
		norm.set ( addUrl );

		SpiderRequest sreq;
		// returns false and sets g_errno on error
		if ( ! sreq.setFromAddUrl ( norm.getUrl() ) ) { //addUrl ) ) {
			log("addurl: url had problem: %s",mstrerror(g_errno));
			return true;
		}

		// addurl state
		StateAU *stau;
		try { stau = new(StateAU); }
		catch ( ... ) {
			g_errno = ENOMEM;
			return true;
		}
		mnew ( stau , sizeof(StateAU) , "stau");
		
		// fill it up
		SafeBuf *mlist = &stau->m_metaListBuf;
		if ( ! mlist->pushChar(RDB_SPIDERDB) )
			return true;
		if ( ! mlist->safeMemcpy ( &sreq , sreq.getRecSize() ) )
			return true;

		Msg4 *msg4 = &stau->m_msg4;
		// this should copy the recs from list into the buffers
		if ( msg4->addMetaList ( mlist->getBufStart() ,
					 mlist->getLength() ,
					 cr->m_collnum,
					 stau ,
					 freeMsg4Wrapper ,
					 MAX_NICENESS ) ) {
			// if it copied everything ok, nuke our msg4
			// otherwise it will call freeMsg4Wraper when it
			// completes!
			freeMsg4Wrapper( stau );
		}
	}
	//
	// DONE ADDING URL
	//


 	int32_t numResults = msg40->getNumResults();

	// if user is doing ajax widget we need to know the current docid
	// that is listed at the top of their widget display so we can
	// hide the new docids above that and scroll them down slowly.
	/*
	//int32_t topDocIdPos = -1;
	bool hasInvisibleResults = false;
	//int32_t numInvisible = 0;
	int32_t numAbove = 0;
	HttpRequest *hr = &st->m_hr;
	int64_t oldTop = 0LL;
	int64_t lastDocId = 0LL;
	double lastSerpScore = 0.0;
	if ( si->m_format == FORMAT_WIDGET_AJAX ) {
		// sanity, no stream mode here, it won't work
		if ( si->m_streamResults )
			log("results: do not use stream=1 for widget");
		// get current top docid
		int64_t topDocId = hr->getLongLong("topdocid",0LL);

		// DEBUG: force it on for now
		//topDocId = 4961990748LL;

		// scan results. this does not support &stream=1 streaming
		// mode. it doesn't make sense that it needs to.
		for ( int32_t i = 0 ; i < numResults ; i++ ) {
			// skip if already invisible
			if ( msg40->m_msg3a.m_clusterLevels[i] != CR_OK ) 
				continue;
			// get it
			Msg20 *m20 = msg40->m_msg20[i];
			if ( ! m20 ) continue;
			// checkdocid
			Msg20Reply *mr = m20->m_r;
			if ( ! mr ) continue;
			// save this
			lastDocId = mr->m_docId;
			lastSerpScore = msg40->m_msg3a.m_scores[i];
			// set "oldTop" to first docid we encounter
			if ( ! oldTop ) oldTop = mr->m_docId;
			// stop if no topdocid otherwise. oldTop is now set
			if ( ! topDocId ) continue; // == 0 ) break;
			if ( mr->m_docId != topDocId ) {
				hasInvisibleResults = true;
				// count # of docids above top docid
				numAbove++;
				continue;
			}
			// we match it, so set this if not already set
			//if ( topDocIdPos != -1 ) topDocIdPos = i;
			//break;
		}
	}				
	*/

	SafeBuf *sb = &st->m_sb;

	// print javascript for scrolling down invisible div for
	// ajax based widgets
	// MDW: this does not execute because it is loaded via ajax...
	// so i moved logic into diffbot.php for now.
	/*
	if ( si->m_format == FORMAT_WIDGET_AJAX && numInvisible ) {
		sb->safePrintf("<script type=text/javascript>"
			       // call this function like 5 times a second
			       "function diffbot_scroll() {\n"
			       // get hidden div
			       "var hd = document.getElementById('diffbot_"
			       "invisible');\n"
			       // get current bottom
			       "var b=hd.style.height;\n"
			       // decrement by 1 pixel and reassign
			       "hd.style.height = hd +1;\n"
			       // we are done if height is equal to 
			       // X * resultdivheight which is 140px i think
			       "if ( hd >= %"INT32" ) return;\n"
			       // call us again in 300ms
			       "setTimeout('diffbot_scroll()',300);\n"
			       "}"

			       // on load start scrolling
			       "diffbot_scroll();\n"

			       "alert(\'poo\');\n"

			       "</script>"
			       , numInvisible * (int32_t)RESULT_HEIGHT

			       );
	}
	*/

	// print logo, search box, results x-y, ... into st->m_sb
	printSearchResultsHeader ( st );

	// propagate "topdocid" so when he does another query every 30 secs
	// or so we know what docid was on top for scrolling purposes
	//if ( si->m_format == FORMAT_WIDGET_AJAX )
	//	sb->safePrintf("<input type=hidden "
	//		       "id=topdocid name=topdocid value=%"INT64">\n",
	//		       oldTop);

	// report how many results we added above the topdocid provided, if any
	// so widget can scroll down automatically
	//if ( si->m_format == FORMAT_WIDGET_AJAX && numAbove )
	//	sb->safePrintf("<input type=hidden "
	//		       "id=topadd name=topadd value=%"INT32">\n",numAbove);
	

	// we often can add 100s of things to the widget's result set per 
	// second especially when sorting by last spidered time and spidering
	// a lot. setting the maxserpscore of the serp score of the last result
	// allows us to append new search results to what we have in a 
	// consistent manner.
	// if ( si->m_format == FORMAT_WIDGET_AJAX ) {
	// 	// let's make this ascii encoded crap
	// 	sb->safePrintf("<input type=hidden "
	// 		       "id=maxserpscore "
	// 		       "value=%f>\n",
	// 		       lastSerpScore);
	// 	// let's make this ascii encoded crap
	// 	sb->safePrintf("<input type=hidden "
	// 		       "id=maxserpdocid "
	// 		       "value=%"INT64">\n",
	// 		       lastDocId);
	// }


	// then print each result
	// don't display more than docsWanted results
	int32_t count = msg40->getDocsWanted();
	bool hadPrintError = false;
	int32_t numPrintedSoFar = 0;
	//int32_t widgetHeight = hr->getLong("widgetheight",400);
	//int32_t widgetwidth = hr->getLong("widgetwidth",250);

	for ( int32_t i = 0 ; count > 0 && i < numResults ; i++ ) {

		/*
		if ( hasInvisibleResults ) {
			//
			// MAKE THESE RESULTS INVISIBLE!
			//
			// if doing a widget, we initially hide the new results
			// and scroll them down in time so it looks cool.
			if ( i == 0 )
				sb->safePrintf("<div id=diffbot_invisible "
					       "style=top:%"INT32"px;"
					       // relative to containing div
					       // which is position:relative!
					       "position:absolute;"
					       "overflow-y:hidden;>"
					       ,
					       (-1*
						(RESULT_HEIGHT+
						 SERP_SPACER+
						 PADDING*2)*
						numInvisible));
			//
			// END INSIVISBILITY
			//
			// to test scrolling, hide the first result and
			// scroll it out
			if ( i == topDocIdPos )
				sb->safePrintf("</div>"
					       "<div id=diffbot_visible"
					       " style=top:0px;"
					       "position:absolute;>"
					       );
		}
		*/

		//////////
		//
		// prints in xml or html
		//
		//////////
		if ( ! printResult ( st , i , &numPrintedSoFar ) ) {
			hadPrintError = true;
			break;
		}


		// limit it
		count--;
	}


	if ( hadPrintError ) {
		if ( ! g_errno ) g_errno = EBADENGINEER;
		log("query: had error: %s",mstrerror(g_errno));
		//return sendReply ( st , sb.getBufStart() );
	}


	// wrap it up with Next 10 etc.
	printSearchResultsTail ( st );

	// if we split the serps into 2 divs for scrolling purposes
	// then close up the 2nd one
	//if ( hasInvisibleResults ) sb->safePrintf("</div>");

	// END SERP DIV
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX )
		sb->safePrintf("</div>");

	// send it off
	sendReply ( st , st->m_sb.getBufStart() );

	return true;
}

// defined in PageRoot.cpp
bool expandHtml (  SafeBuf& sb,
		   char *head , 
		   int32_t hlen ,
		   char *q    , 
		   int32_t qlen ,
		   HttpRequest *r ,
		   SearchInput *si,
		   char *method ,
		   CollectionRec *cr ) ;


bool printLeftColumnRocketAndTabs ( SafeBuf *sb, 
				    bool isSearchResultsPage ,
				    CollectionRec *cr ,
				    char *tabName );

bool printLeftNavColumn ( SafeBuf &sb, State0 *st ) {

	SearchInput *si = &st->m_si;
	Msg40 *msg40 = &st->m_msg40;
	CollectionRec *cr = si->m_cr;

	char format = si->m_format;

	if ( format == FORMAT_HTML ) {
		char *title = "Search Results";
		sb.safePrintf("<title>Gigablast - %s</title>\n",title);
		sb.safePrintf("<style><!--\n");
		sb.safePrintf("body {\n");
		sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
		sb.safePrintf("color: #000000;\n");
		sb.safePrintf("font-size: 12px;\n");
		sb.safePrintf("margin: 0px 0px;\n");
		sb.safePrintf("letter-spacing: 0.04em;\n");
		sb.safePrintf("}\n");
		sb.safePrintf("a {text-decoration:none;}\n");
		//sb.safePrintf("a:link {color:#00c}\n");
		//sb.safePrintf("a:visited {color:#551a8b}\n");
		//sb.safePrintf("a:active {color:#f00}\n");
		sb.safePrintf(".bold {font-weight: bold;}\n");
		sb.safePrintf(".bluetable {background:#d1e1ff;"
			      "margin-bottom:15px;font-size:12px;}\n");
		sb.safePrintf(".url {color:#008000;}\n");
		sb.safePrintf(".cached, .cached a {font-size: 10px;"
			      "color: #666666;\n");
		sb.safePrintf("}\n");
		sb.safePrintf("table {\n");
		sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
		sb.safePrintf("color: #000000;\n");
		sb.safePrintf("font-size: 12px;\n");
		sb.safePrintf("}\n");
		sb.safePrintf(".directory {font-size: 16px;}\n"
			      ".nav {font-size:20px;align:right;}\n"
			      );
		sb.safePrintf("-->\n");
		sb.safePrintf("</style>\n");
		sb.safePrintf("\n");
		sb.safePrintf("</head>\n");
		sb.safePrintf("<script>\n");
		sb.safePrintf("<!--\n");
		sb.safePrintf("var openmenu=''; var inmenuclick=0;");
		sb.safePrintf("function x(){document.f.q.focus();}\n");
		sb.safePrintf("// --></script>\n");
		sb.safePrintf("<body "

			      "onmousedown=\""

			      "if (openmenu != '' && inmenuclick==0) {"
			      "document.getElementById(openmenu)."
			      "style.display='none'; openmenu='';"
			      "}"

			      "inmenuclick=0;"
			      "\" "

			      "onload=\"x()\">\n");

		//
		// DIVIDE INTO TWO PANES, LEFT COLUMN and MAIN COLUMN
		//
		sb.safePrintf("<TABLE border=0 height=100%% cellpadding=0 "
			      "cellspacing=0>"
			      "\n<TR>\n");

		//
		// first the nav column
		//

		// . also prints <TD>...</TD>. true=isSearchresults
		// . tabName = "search"
		printLeftColumnRocketAndTabs ( &sb , true , cr , "search" );



// 		sb.safePrintf("<TD bgcolor=#f3c714 " // yellow/gold
// 			      "valign=top "
// 			      "style=\""
// 			      "width:210px;"
// 			      "border-right:3px solid blue;"
// 			      "\">"

// 			      "<br>"

// 			      "<center>"
// 			      "<a href=/?c=%s>"
// 			      "<div style=\""
// 			      "background-color:white;"
// 			      "padding:10px;"
// 			      "border-radius:100px;"
// 			      "border-color:blue;"
// 			      "border-width:3px;"
// 			      "border-style:solid;"
// 			      "width:100px;"
// 			      "height:100px;"
// 			      "\">"
// 			      "<br style=line-height:10px;>"
// 			      "<img width=54 height=79 "
// 			      "alt=HOME src=/rocket.jpg>"
// 			      "</div>"
// 			      "</a>"
// 			      "</center>"

// 			      "<br>"
// 			      "<br>"
// 			      ,cr->m_coll
// 			      );
	}


	/*
	// home link
	sb.safePrintf(
		      "<a href=/>"
		      "<div style=\"background-color:blue;"
		      "padding:5px;"
		      "text-align:right;"
		      "border-width:3px;"
		      "border-right-width:0px;"
		      "border-style:solid;"
		      "margin-left:10px;"
		      "border-color:white;"
		      "border-top-left-radius:10px;"
		      "border-bottom-left-radius:10px;"
		      "font-size:14px;"
		      "color:white;"
		      "cursor:hand;"
		      "cursor:pointer;\" "
		      " onmouseover=\""
		      "this.style.backgroundColor='lightblue';"
		      "this.style.color='black';\""
		      " onmouseout=\""
		      "this.style.backgroundColor='blue';"
		      "this.style.color='white';\""
		      ">"
		      "&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; "
		      "&nbsp; &nbsp; &nbsp; "
		      "<b>HOME</b> &nbsp; &nbsp;"
		      "</div>"
		      "</a>"
		      "<br>"
		      );
	*/


	//
	// BEGIN FACET PRINTING
	//
	// 
	// . print out one table for each gbfacet: term in the query
	// . LATER: show the text string corresponding to the hash
	//   by looking it up in the titleRec
	//
	if ( format == FORMAT_HTML ) msg40->printFacetTables ( &sb );
	//
	// END FACET PRINTING
	//

	//
	// BEGIN PRINT GIGABITS
	//

	SafeBuf *gbuf = &msg40->m_gigabitBuf;
	int32_t numGigabits = gbuf->length()/sizeof(Gigabit);

	// MDW: support gigabits in xml/json format again
	//if ( format != FORMAT_HTML ) numGigabits = 0;

	if ( ! st->m_header )
		numGigabits = 0;

	// print gigabits
	Gigabit *gigabits = (Gigabit *)gbuf->getBufStart();
	//int32_t numCols = 5;
	//int32_t perRow = numGigabits / numCols;

	if ( numGigabits && format == FORMAT_XML )
		sb.safePrintf("\t<gigabits>\n");

	if ( numGigabits && format == FORMAT_JSON )
		sb.safePrintf("\"gigabits\":[\n");


	if ( numGigabits && format == FORMAT_HTML )
		// gigabit unhide function
		sb.safePrintf (
				"<script>"
				"function ccc ( gn ) {\n"
				"var e = document.getElementById('fd'+gn);\n"
				"var f = document.getElementById('sd'+gn);\n"
				"if ( e.style.display == 'none' ){\n"
				"e.style.display = '';\n"
				"f.style.display = 'none';\n"
				"}\n"
				"else {\n"
				"e.style.display = 'none';\n"
				"f.style.display = '';\n"
				"}\n"
				"}\n"
				"</script>\n"
			       );
	
	if ( numGigabits && format == FORMAT_HTML )
		sb.safePrintf("<div id=gigabits "
			      "style="
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:blue;"
			      "background-color:white;"
			      "border-right-color:white;"
			      "margin-right:-3px;"
			      ">"
			      "<table cellspacing=7>"
			      "<tr><td width=200px; valign=top>"
			      "<center><img src=/gigabits40.jpg></center>"
			      "<br>"
			      "<br>"
			      );

	Query gigabitQuery;
	char tmp[1024];
	SafeBuf ttt(tmp, 1024);
	// limit it to 40 gigabits for now
	for ( int32_t i = 0 ; i < numGigabits && i < 40 ; i++ ) {
		Gigabit *gi = &gigabits[i];
		ttt.pushChar('\"');
		ttt.safeMemcpy(gi->m_term,gi->m_termLen);
		ttt.pushChar('\"');
		ttt.pushChar(' ');
	}
	// term on it
	ttt.nullTerm();

	if ( numGigabits > 0 ) 
		gigabitQuery.set2 ( ttt.getBufStart() ,
				    si->m_queryLangId ,
				    true , // queryexpansion?
				    true );  // usestopwords?

	// log("results: gigabitquery=%s landid=%"INT32""
	//     ,ttt.getBufStart()
	//     ,si->m_queryLangId);


	for ( int32_t i = 0 ; i < numGigabits ; i++ ) {
		//if ( i > 0 && format == FORMAT_HTML ) 
		//	sb.safePrintf("<hr>");
		//if ( perRow && (i % perRow == 0) )
		//	sb.safePrintf("</td><td valign=top>");
		// print all sentences containing this gigabit
		Gigabit *gi = &gigabits[i];
		// after the first 3 hide them with a more link
		if ( i == 1 && format == FORMAT_HTML )  {
			sb.safePrintf("</span><a onclick="
				      "\""
				      "var e = "
				      "document.getElementById('hidegbits');"
				      "if ( e.style.display == 'none' ){\n"
				      "e.style.display = '';\n"
				      "this.innerHtml='Show less';"
				      "}"
				      "else {\n"
				      "e.style.display = 'none';\n"
				      "this.innerHtml='Show more';\n"
				      "}\n"
				      "\" style=cursor:hand;cursor:pointer;>"
				      "Show more</a>");
			sb.safePrintf("<span id=hidegbits "
				      "style=display:none;>"
				      "<br><br>");
		}

		//printGigabit ( st,sb , msg40 , gi , si );
		//sb.safePrintf("<br>");
		printGigabitContainingSentences(st,&sb,msg40,gi,si,
						&gigabitQuery,
						i);
		if ( format == FORMAT_HTML )
			sb.safePrintf("<br><br>");
	}

	//if ( numGigabits >= 1 && format == FORMAT_HTML ) 

	if ( numGigabits && format == FORMAT_HTML )
		sb.safePrintf("</td></tr></table></div><br>");


	if ( numGigabits && format == FORMAT_XML )
		sb.safePrintf("\t</gigabits>\n");

	if ( numGigabits && format == FORMAT_JSON ) {
		// remove ,\n
		sb.m_length -=2;
		// add back just \n
		// end the gigabits array
		sb.safePrintf("\n],\n");
	}

	//
	// now print various knobs
	//

	//
	// print sort by date options
	//
	/*
	if ( format == FORMAT_HTML )
		sb.safePrintf(
			      "<div id=best "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:blue;"
			      "background-color:white;"
			      "border-right-color:white;"
			      "margin-right:-3px;"
			      "text-align:right;"
			      ">"
			      "<b>"
			      "SEARCH TOOLS &nbsp; &nbsp;"
			      "</b>"
			      "</div>"

			      "<br>"
	*/
			      /*
			      "<div id=newsest "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:white;"
			      "background-color:blue;"
			      "border-right-color:blue;"
			      "margin-right:0px;"
			      "text-align:right;"
			      "color:white;"
			      ">"
			      "<b>"
			      "NEWSET FIRST &nbsp; &nbsp;"
			      "</b>"
			      "</div>"

			      "<br>"

			      "<div id=newsest "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:white;"
			      "background-color:blue;"
			      "border-right-color:blue;"
			      "margin-right:0px;"
			      "text-align:right;"
			      "color:white;"
			      ">"
			      "<b>"
			      "OLDEST FIRST &nbsp; &nbsp;"
			      "</b>"
			      "</div>"
			      "<br>"

			      */


	//
	// print date constraint functions now
	//
	if ( format == FORMAT_HTML && 1 == 2)
		sb.safePrintf(
			      "<div id=best "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:blue;"
			      "background-color:white;"
			      "border-right-color:white;"
			      "margin-right:-3px;"
			      "text-align:right;"
			      ">"
			      "<b>"
			      "ANYTIME &nbsp; &nbsp;"
			      "</b>"
			      "</div>"

			      "<br>"

			      "<div id=newsest "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:white;"
			      "background-color:blue;"
			      "border-right-color:blue;"
			      "margin-right:0px;"
			      "text-align:right;"
			      "color:white;"
			      ">"
			      "<b>"
			      "LAST 24 HOURS &nbsp; &nbsp;"
			      "</b>"
			      "</div>"

			      "<br>"

			      "<div id=newsest "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:white;"
			      "background-color:blue;"
			      "border-right-color:blue;"
			      "margin-right:0px;"
			      "text-align:right;"
			      "color:white;"
			      ">"
			      "<b>"
			      "LAST 7 DAYS &nbsp; &nbsp;"
			      "</b>"
			      "</div>"
			      "<br>"

			      "<div id=newsest "
			      "style="
			      "font-size:14px;"
			      "padding:5px;"
			      "position:relative;"
			      "border-width:3px;"
			      "border-right-width:0px;"
			      "border-style:solid;"
			      "margin-left:10px;"
			      "border-top-left-radius:10px;"
			      "border-bottom-left-radius:10px;"
			      "border-color:white;"
			      "background-color:blue;"
			      "border-right-color:blue;"
			      "margin-right:0px;"
			      "text-align:right;"
			      "color:white;"
			      ">"
			      "<b>"
			      "LAST 30 DAYS &nbsp; &nbsp;"
			      "</b>"
			      "</div>"
			      "<br>"


			      );



	//
	// now the MAIN column
	//
	if ( format == FORMAT_HTML )
		sb.safePrintf("\n</TD>"
			      "<TD valign=top style=padding-left:30px;>\n");
	return true;
}

bool printIgnoredWords ( SafeBuf *sb , SearchInput *si ) {
	// mention ignored query terms
	// we need to set another Query with "keepAllSingles" set to false
	Query *qq2 = &si->m_q;
	//qq2.set ( q , qlen , NULL , 0 , si->m_boolFlag , false );
	bool firstIgnored = true;
	for ( int32_t i = 0 ; i < qq2->m_numWords ; i++ ) {
		//if ( si->m_xml ) break;
		QueryWord *qw = &qq2->m_qwords[i];
		// only print out words ignored cuz they were stop words
		if ( qw->m_ignoreWord != IGNORE_QSTOP ) continue;
		// print header -- we got one
		if ( firstIgnored ) {
			if ( si->m_format == FORMAT_XML )
				sb->safePrintf ("\t<ignoredWords><![CDATA[");
			else if ( si->m_format == FORMAT_JSON )
				sb->safePrintf ("\t\"ignoredWords\":\"");
			else if ( si->m_format == FORMAT_HTML )
				sb->safePrintf ("<br><font "
					       "color=\"#707070\">The "
					       "following query words "
					       "were ignored: "
					       "<b>");
			firstIgnored = false;
		}
		// print the word
		char *t    = qw->m_word; 
		int32_t  tlen = qw->m_wordLen;
		sb->utf8Encode2 ( t , tlen );
		sb->safePrintf (" ");
	}
	// print tail if we had ignored terms
	if ( ! firstIgnored ) {
		sb->incrementLength(-1);
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("]]></ignoredWords>\n");
		else if ( si->m_format == FORMAT_JSON )
			sb->safePrintf("\",\n");
		else if ( si->m_format == FORMAT_HTML )
			sb->safePrintf ("</b>. Preceed each with a '+' or "
				       "wrap in "
				       "quotes to not ignore.</font>");
	}
	return true;
}

bool printSearchResultsHeader ( State0 *st ) {

	SearchInput *si = &st->m_si;

	// grab the query
	Msg40 *msg40 = &(st->m_msg40);
	char  *q    = msg40->getQuery();
	int32_t   qlen = msg40->getQueryLen();

  	//char  local[ 128000 ];
	//SafeBuf sb(local, 128000);
	SafeBuf *sb = &st->m_sb;
	// reserve 1.5MB now!
	if ( ! sb->reserve(1500000 ,"pgresbuf" ) ) // 128000) )
		return false;
	// just in case it is empty, make it null terminated
	sb->nullTerm();

	// print first [ for json
	if ( si->m_format == FORMAT_JSON ) {
		if ( st->m_header ) sb->safePrintf("{\n");
		// this is just for diffbot really...
		else                sb->safePrintf("[\n");
	}

	CollectionRec *cr = si->m_cr;
	HttpRequest *hr = &st->m_hr;

	// if there's a ton of sites use the post method otherwise
	// they won't fit into the http request, the browser will reject
	// sending such a large request with "GET"
	char *method = "GET";
	if ( si->m_sites && gbstrlen(si->m_sites)>800 ) method = "POST";


	if ( si->m_format == FORMAT_HTML &&
	     cr->m_htmlHead.length() ) {
		return expandHtml ( *sb ,
				    cr->m_htmlHead.getBufStart(),
				    cr->m_htmlHead.length(),
				    q,
				    qlen,
				    hr,
				    si,
				    method,
				    cr);
	}
				 

	// . if not matt wells we do not do ajax
	// . the ajax is just there to prevent bots from slamming me 
	//   with queries.
	if ( ! g_conf.m_isMattWells && si->m_format == FORMAT_HTML ) {
		printCSSHead ( sb ,si->m_format );
		sb->safePrintf("<body>");
	}

	if ( ! g_conf.m_isMattWells && si->m_format==FORMAT_WIDGET_IFRAME ) {
		printCSSHead ( sb ,si->m_format );
		sb->safePrintf("<body style=padding:0px;margin:0px;>");
	}

	if ( si->m_format == FORMAT_WIDGET_IFRAME ) {
		int32_t refresh = hr->getLong("refresh",0);
		if ( refresh )
			sb->safePrintf("<meta http-equiv=\"refresh\" "
				       "content=%"INT32">",refresh);
	}

	// lead with user's widget header which usually has custom style tags
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		char *header = hr->getString("header",NULL);
		if ( header ) sb->safeStrcpy ( header );
	}

	// this also prints gigabits and nuggabits
	// if we are xml/json we call this below otherwise we lose
	// the header of <?xml...> or whatever
	if ( ! g_conf.m_isMattWells && si->m_format == FORMAT_HTML ) {
		printLeftNavColumn ( *sb,st );
	}

	if ( ! g_conf.m_isMattWells && si->m_format == FORMAT_HTML ) {
		printLogoAndSearchBox ( sb,&st->m_hr,-1,si); // catId = -1
	}

	// the calling function checked this so it should be non-null
	char *coll = cr->m_coll;
	int32_t collLen = gbstrlen(coll);

	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		char *pos = "relative";
		if ( si->m_format == FORMAT_WIDGET_IFRAME ) pos = "absolute";
		int32_t widgetwidth = hr->getLong("widgetwidth",150);
		int32_t widgetHeight = hr->getLong("widgetheight",400);
		//int32_t iconWidth = 25;

		// put image in this div which will have top:0px JUST like
		// the div holding the search results we print out below
		// so that the image does not scroll when you use the
		// scrollbar. holds the magifying glass img and searchbox.
		sb->safePrintf("<div class=magglassdiv "
			       "style=\"position:absolute;"
			       "right:15px;"
			       "z-index:10;"
			       "top:0px;\">");

		//int32_t refresh = hr->getLong("refresh",15);
		char *oq = hr->getString("q",NULL);
		if ( ! oq ) oq = "";
		char *prepend = hr->getString("prepend");
		if ( ! prepend ) prepend = "";
		char *displayStr = "none";
		if ( prepend && prepend[0] ) displayStr = "";
		// to do a search we need to re-call the ajax,
		// just call reload like the one that is called every 15s or so
		sb->safePrintf("<form "//method=get action=/search "
			       // use "1" as arg to force reload
			       "onsubmit=\"widget123_reload(1);"

			       // let user know we are loading
			       "var w=document.getElementById("
			       "'widget123_scrolldiv');"
			       // just set the widget content to the reply
			       "if (w) "
			       "w.innerHTML='<br><br><b>Loading Results..."
			       "</b>';"

			       // prevent it from actually submitting
			       "return false;\">");

		sb->safePrintf("<img "
			       "style=\""
			       //"position:absolute;" // absolute or relative?
			       // put it on TOP of the other stuff
			       "z-index:10;"
			       "margin-top:3px;"
			       //"right:10px;"
			       //"right:2px;"
			       //"width:%"INT32"px;"
			       // so we are to the right of the searchbox
			       "float:right;"
			       "\" "
			       "onclick=\""
			       "var e=document.getElementById('sbox');"
			       "if(e.style.display == 'none') {"
			       "e.style.display = '';"
			       // give it focus
			       "var qb=document.getElementById('qbox');"
			       "qb.focus();"
			       "} else {"
			       "e.style.display = 'none';"
			       "}"
			       "\" " // end function
			       " "
			       "width=35 "
			       "height=31 "
			       "src=\"/magglass.png\">"
			       );

		//char *origq = hr->getString("q");
		// we sort all results by spider date now so PREPEND
		// the actual user query 
		char *origq = hr->getString("prepend");
		if ( ! origq ) origq = "";
		sb->safePrintf("<div id=sbox style=\"float:left;"
			       "display:%s;"
			       "opacity:0.83;"
			       //"background-color:gray;"
			       //"padding:5px;"
			       "\">"
			       // the box that holds the query
			       "<input type=text id=qbox name=qbox "
			       "size=%"INT32" " //name=prepend "
			       "value=\"%s\"  "
			       "style=\"z-index:10;"
			       "font-weight:bold;"
			       "font-size:18px;"
			       "border:4px solid black;"
			       "margin:3px;"
			       "\">"
			       , displayStr
			       , widgetwidth / 23 
			       , origq
			       );
		sb->safePrintf("</div>"
			       "</form>\n"
			       );

		// . BEGIN SERP DIV
		// . div to hold the search results
		// . this will have the scrollbar to just scroll the serps
		//   and not the magnifying glass
		sb->safePrintf("</div>"
			       "<div id=widget123_scrolldiv "
			       "onscroll=widget123_append(); "
			       "style=\"position:absolute;"
			       "top:0px;"
			       "overflow-y:auto;"
			       "overflow-x:hidden;"
			       "width:%"INT32"px;"
			       "height:%"INT32"px;\">"
			       , widgetwidth
			       , widgetHeight);
	}

	// xml
	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"UTF-8\" ?>\n"
			      "<response>\n" );

	int64_t nowMS = gettimeofdayInMillisecondsLocal();

	// show current time
	if ( si->m_format == FORMAT_XML ) {
		int64_t globalNowMS = localToGlobalTimeMilliseconds(nowMS);
		sb->safePrintf("\t<currentTimeUTC>%"UINT32"</currentTimeUTC>\n",
			      (uint32_t)(globalNowMS/1000));
	} 
	else if ( st->m_header && si->m_format == FORMAT_JSON ) {
	    int64_t globalNowMS = localToGlobalTimeMilliseconds(nowMS);
	    sb->safePrintf("\"currentTimeUTC\":%"UINT32",\n", 
			   (uint32_t)(globalNowMS/1000));
	}

	// show response time if not doing Quality Assurance
	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t<responseTimeMS>%"INT64"</responseTimeMS>\n",
			      st->m_took);
	else if ( st->m_header && si->m_format == FORMAT_JSON )
	    sb->safePrintf("\"responseTimeMS\":%"INT64",\n", st->m_took);

	// out of memory allocating msg20s?
	if ( st->m_errno ) {
		log("query: Query failed. Had error processing query: %s",
		    mstrerror(st->m_errno));
		g_errno = st->m_errno;
		//return sendReply(st,sb->getBufStart());
		return false;
	}


	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t<numResultsOmitted>%"INT32""
			       "</numResultsOmitted>\n",
			       msg40->m_omitCount);
		sb->safePrintf("\t<numShardsSkipped>%"INT32"</numShardsSkipped>\n",
			       msg40->m_msg3a.m_skippedShards);
		sb->safePrintf("\t<totalShards>%"INT32"</totalShards>\n",
			       g_hostdb.m_numShards );
	}

	if ( st->m_header && si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\"numResultsOmitted\":%"INT32",\n",
			       msg40->m_omitCount);
		sb->safePrintf("\"numShardsSkipped\":%"INT32",\n",
			       msg40->m_msg3a.m_skippedShards);
		sb->safePrintf("\"totalShards\":%"INT32",\n",
			       g_hostdb.m_numShards );
	}




	//bool xml = si->m_xml;


	// if they are doing a search in dmoz, catId will be > 0.
	//if (  si->m_directCatId >= 0 ) {
	//	printDMOZCrumb ( sb , si->m_directCatId , xml );
	//}


	///////////
	//
	// show DMOZ subcategories if doing either a
	// "gbpcatid:<catid> |" (Search restricted to category)
	// "gbcatid:<catid>"    (DMOZ urls in that topic)
	//
	// The search gbcatid: results should be sorted by siterank i guess
	// since it is only search a single term: gbcatid:<catid> so we can
	// put our stars back onto that and should be sorted by them.
	//
	///////////
	/*
	if ( si->m_catId >= 0 ) {
		// print the subtopcis in this topic. show as links above
		// the search results
		printDMOZSubTopics ( sb, si->m_catId , xml );//st, xml );
		// ok, for now just print the dmoz topics since our search
		// results will be empty... until populated!
		//g_categories->printUrlsInTopic ( &sb , si->m_catId );
	}
	*/


	// save how many docs are in this collection
	int64_t docsInColl = -1;
	//RdbBase *base = getRdbBase ( RDB_CHECKSUMDB , si->m_coll );
	RdbBase *base = getRdbBase ( (uint8_t)RDB_CLUSTERDB , st->m_collnum );
	//if ( base ) docsInColl = base->getNumGlobalRecs();
	//docsInColl = g_hostdb.getNumGlobalRecs ( );
	// estimate it
	if ( base ) docsInColl = base->getNumGlobalRecs();
	// multiply by # of *unique* shards
	// no because it already does this i think
	//docsInColl *= g_hostdb.getNumShards();
	// include number of docs in the collection corpus
	if ( docsInColl >= 0LL ) {
	    if ( si->m_format == FORMAT_XML)
	        sb->safePrintf ( "\t<docsInCollection>%"INT64""
	                "</docsInCollection>\n", docsInColl );
	    else if ( st->m_header && si->m_format == FORMAT_JSON)
            sb->safePrintf("\"docsInCollection\":%"INT64",\n", docsInColl);
	}

 	int32_t numResults = msg40->getNumResults();
	bool moreFollow = msg40->moreResultsFollow();
	// an estimate of the # of total hits
	int64_t totalHits = msg40->getNumTotalHits();
	// only adjust upwards for first page now so it doesn't keep chaning
	if ( totalHits < numResults ) totalHits = numResults;

	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t<hits>%"INT64"</hits>\n",(int64_t)totalHits);
	else if ( st->m_header && si->m_format == FORMAT_JSON )
		sb->safePrintf("\"hits\":%"INT64",\n", (int64_t)totalHits);

	// if streaming results we just don't know if we will require
	// a "Next 10" link or not! we can print that after we print out
	// the results i guess...
	if ( ! si->m_streamResults ) {
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("\t<moreResultsFollow>%"INT32""
				       "</moreResultsFollow>\n"
				       ,(int32_t)moreFollow);
		else if ( st->m_header && si->m_format == FORMAT_JSON )
			sb->safePrintf("\"moreResultsFollow\":%"INT32",\n", 
				       (int32_t)moreFollow);
	}

	// . did he get a spelling recommendation?
	// . do not use htmlEncode() on this anymore since receiver
	//   of the XML feed usually does not want that.
	if ( si->m_format == FORMAT_XML && st->m_spell[0] ) {
		sb->safePrintf ("\t<spell><![CDATA[");
		sb->safeStrcpy(st->m_spell);
		sb->safePrintf ("]]></spell>\n");
	}

	if ( si->m_format == FORMAT_JSON && st->m_spell[0] ) {
		sb->safePrintf ("\t\"spell\":\"");
		sb->jsonEncode(st->m_spell);
		sb->safePrintf ("\"\n,");
	}

	// print individual query term info
	if ( si->m_format == FORMAT_XML ) {
		Query *q = &si->m_q;
		sb->safePrintf("\t<queryInfo>\n");
		sb->safePrintf("\t\t<fullQuery><![CDATA[");
		sb->cdataEncode(q->m_orig);
		sb->safePrintf("]]></fullQuery>\n");
		sb->safePrintf("\t\t<queryLanguageAbbr>"
			       "<![CDATA[%s]]>"
			       "</queryLanguageAbbr>\n"
			       , getLangAbbr(si->m_queryLangId) );
		sb->safePrintf("\t\t<queryLanguage>"
			       "<![CDATA[%s]]>"
			       "</queryLanguage>\n"
			       , getLanguageString(si->m_queryLangId) );
		// print query words we ignored, like stop words
		printIgnoredWords ( sb , si );

		sb->safePrintf("\t\t<queryNumTermsTotal>"
			       "%"INT32
			       "</queryNumTermsTotal>\n"
			       , q->m_numTermsUntruncated );
		sb->safePrintf("\t\t<queryNumTermsUsed>"
			       "%"INT32
			       "</queryNumTermsUsed>\n"
			       , q->m_numTerms );
		int32_t tval = 0;
		if ( q->m_numTerms < q->m_numTermsUntruncated ) tval = 1;
		sb->safePrintf("\t\t<queryWasTruncated>"
			       "%"INT32
			       "</queryWasTruncated>\n"
			       , tval );

		for ( int i = 0 ; i < q->m_numTerms ; i++ ) {
			sb->safePrintf("\t\t<term>\n");
			QueryTerm *qt = &q->m_qterms[i];
			sb->safePrintf("\t\t\t<termNum>%i</termNum>\n",i);
			char *term = qt->m_term;
			char c = term[qt->m_termLen];
			term[qt->m_termLen] = '\0';
			sb->safePrintf("\t\t\t<termStr><![CDATA[");
			char *printTerm = qt->m_term;
			if ( is_wspace_a(term[0])) printTerm++;
			sb->cdataEncode(printTerm);
			sb->safePrintf("]]>"
				       "</termStr>\n");
			term[qt->m_termLen] = c;
			// syn?
			QueryTerm *sq = qt->m_synonymOf;
			// what language did synonym come from?
			if ( sq ) {
				// language map from wiktionary
				sb->safePrintf("\t\t\t<termLang>"
					       "<![CDATA[");
				bool first = true;
				for ( int i = 0 ; i < langLast ; i++ ) {
					uint64_t bit = (uint64_t)1 << i;
					if ( ! (qt->m_langIdBits&bit))continue;
					char *str = getLangAbbr(i);
					if ( ! first ) sb->pushChar(',');
					first = false;
					sb->safeStrcpy ( str );
				}
				sb->safePrintf("]]></termLang>\n");
			}

			if ( sq ) {
				char *term = sq->m_term;
				char c = term[sq->m_termLen];
				term[sq->m_termLen] = '\0';
				char *printTerm = term;
				if ( is_wspace_a(term[0])) printTerm++;
				sb->safePrintf("\t\t\t<synonymOf>"
					       "<![CDATA[%s]]>"
					       "</synonymOf>\n"
					       ,printTerm);
				term[sq->m_termLen] = c;
			}				
			//int64_t tf = msg40->m_msg3a.m_termFreqs[i];
			int64_t tf = qt->m_termFreq;
			sb->safePrintf("\t\t\t<termFreq>%"INT64"</termFreq>\n"
				       ,tf);
			sb->safePrintf("\t\t\t<termHash48>%"INT64"</termHash48>\n"
				       ,qt->m_termId);
			sb->safePrintf("\t\t\t<termHash64>%"UINT64"</termHash64>\n"
				       ,qt->m_rawTermId);
			QueryWord *qw = qt->m_qword;
			sb->safePrintf("\t\t\t<prefixHash64>%"UINT64"</prefixHash64>\n"
				       ,qw->m_prefixHash);
			sb->safePrintf("\t\t</term>\n");
		}
		sb->safePrintf("\t</queryInfo>\n");
	}			

	// print individual query term info
	if ( si->m_format == FORMAT_JSON && st->m_header ) {
		Query *q = &si->m_q;
		sb->safePrintf("\"queryInfo\":{\n");
		sb->safePrintf("\t\"fullQuery\":\"");
		sb->jsonEncode(q->m_orig);
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\"queryLanguageAbbr\":\"");
		sb->jsonEncode ( getLangAbbr(si->m_queryLangId) );
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\"queryLanguage\":\"");
		sb->jsonEncode ( getLanguageString(si->m_queryLangId) );
		sb->safePrintf("\",\n");
		// print query words we ignored, like stop words
		printIgnoredWords ( sb , si );

		sb->safePrintf("\t\"queryNumTermsTotal\":"
			       "%"INT32",\n"
			       , q->m_numTermsUntruncated );
		sb->safePrintf("\t\"queryNumTermsUsed\":"
			       "%"INT32",\n"
			       , q->m_numTerms );
		int32_t tval = 0;
		if ( q->m_numTerms < q->m_numTermsUntruncated ) tval = 1;
		sb->safePrintf("\t\"queryWasTruncated\":"
			       "%"INT32",\n"
			       , tval );
			
		sb->safePrintf("\t\"terms\":[\n");
		for ( int i = 0 ; i < q->m_numTerms ; i++ ) {
			sb->safePrintf("\t\t{\n");
			QueryTerm *qt = &q->m_qterms[i];
			sb->safePrintf("\t\t\"termNum\":%i,\n",i);
			char *term = qt->m_term;
			char c = term[qt->m_termLen];
			term[qt->m_termLen] = '\0';
			sb->safePrintf("\t\t\"termStr\":\"");
			sb->jsonEncode (qt->m_term);
			sb->safePrintf("\",\n");
			term[qt->m_termLen] = c;
			// syn?
			QueryTerm *sq = qt->m_synonymOf;
			// what language did synonym come from?
			if ( sq ) {
				// language map from wiktionary
				sb->safePrintf("\t\t\"termLang\":\"");
				bool first = true;
				for ( int i = 0 ; i < langLast ; i++ ) {
					uint64_t bit = (uint64_t)1 << i;
					if ( ! (qt->m_langIdBits&bit))continue;
					char *str = getLangAbbr(i);
					if ( ! first ) sb->pushChar(',');
					first = false;
					sb->jsonEncode ( str );
				}
				sb->safePrintf("\",\n");
			}

			if ( sq ) {
				char *term = sq->m_term;
				char c = term[sq->m_termLen];
				term[sq->m_termLen] = '\0';
				sb->safePrintf("\t\t\"synonymOf\":\"");
				sb->jsonEncode(sq->m_term);
				sb->safePrintf("\",\n");
				term[sq->m_termLen] = c;
			}				
			//int64_t tf = msg40->m_msg3a.m_termFreqs[i];
			int64_t tf = qt->m_termFreq;
			sb->safePrintf("\t\t\"termFreq\":%"INT64",\n"
				       ,tf);

			sb->safePrintf("\t\t\"termHash48\":%"INT64",\n"
				       ,qt->m_termId);
			sb->safePrintf("\t\t\"termHash64\":%"UINT64",\n"
				       ,qt->m_rawTermId);

			// don't end last query term attr on a omma
			QueryWord *qw = qt->m_qword;
			sb->safePrintf("\t\t\"prefixHash64\":%"UINT64"\n"
				       ,qw->m_prefixHash);

			sb->safePrintf("\t}");
			if ( i + 1 < q->m_numTerms )
				sb->pushChar(',');
			sb->pushChar('\n');
		}
		sb->safePrintf("\t]\n"); // end "terms":[]
		sb->safePrintf("},\n");
	}			




	// when streaming results we lookup the facets last
	if ( si->m_format != FORMAT_HTML && ! si->m_streamResults &&
	     st->m_header ) 
		msg40->printFacetTables ( sb );

	// now print gigabits if we are xml/json
	if ( si->m_format != FORMAT_HTML ) {
		// this will print gigabits
		printLeftNavColumn ( *sb,st );
	}

	// global-index is not a custom crawl but we should use "objects"
	bool isDiffbot = cr->m_isCustomCrawl;
	if ( strcmp(cr->m_coll,"GLOBAL-INDEX") == 0 ) isDiffbot = true;

	// for diffbot collections only...
	if ( st->m_header &&
	     si->m_format == FORMAT_JSON &&
	     isDiffbot ) {
		sb->safePrintf("\"objects\":[\n");
		return true;
	}

	if ( si->m_format == FORMAT_JSON && st->m_header ) {
		sb->safePrintf("\"results\":[\n");
		return true;
	}

	// debug
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: Displaying up to %"INT32" results.",
		     numResults);

	// tell browser again
	//if ( si->m_format == FORMAT_HTML )
	//	sb->safePrintf("<meta http-equiv=\"Content-Type\" "
	//		      "content=\"text/html; charset=utf-8\">\n");

	// get some result info from msg40
	int32_t firstNum   = msg40->getFirstResultNum() ;
	// numResults may be more than we requested now!
	int32_t n = msg40->getDocsWanted();
	if ( n > numResults )  n = numResults;

	// . make the query class here for highlighting
	// . keepAllSingles means to convert all individual words into
	//   QueryTerms even if they're in quotes or in a connection (cd-rom).
	//   we use this for highlighting purposes
	Query qq;
	qq.set2 ( si->m_displayQuery, langUnknown , si->m_queryExpansion );
	//	 si->m_boolFlag,
	//         true ); // keepAllSingles?

	if ( g_errno ) return false;//sendReply (st,NULL);

	DocIdScore *dpx = NULL;
	if ( numResults > 0 ) dpx = msg40->getScoreInfo(0);

	if ( si->m_format == FORMAT_XML && dpx ) {
		// # query terms used!
		//int32_t nr = dpx->m_numRequiredTerms;
		float max = 0.0;
		// max pairwise
		float lw = getHashGroupWeight(HASHGROUP_INLINKTEXT);
		// square that location weight
		lw *= lw;
		// assume its an inlinker's text, who has rank 15!!!
		lw *= getLinkerWeight(MAXSITERANK);
		// double loops
		/*
		for ( int32_t i = 0 ; i< nr ; i++ ) {
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = getTermFreqWeight(ssi->m_listSize);
			for ( int32_t j = i+1; j< nr ; j++ ) {
				SingleScore *ssj = &dpx->m_singleScores[j];
				float tfwj =getTermFreqWeight(ssj->m_listSize);
				max += (lw * tfwi * tfwj)/3.0;
			}
		}
		*/
		// single weights
		float maxtfw1 = 0.0;
		int32_t maxi1;
		// now we can have multiple SingleScores for the same term!
		// because we take the top MAX_TOP now and add them to
		// get the term's final score.
		for ( int32_t i = 0 ; i< dpx->m_numSingles ; i++ ) {
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = ssi->m_tfWeight;
			if ( tfwi <= maxtfw1 ) continue;
			maxtfw1 = tfwi;
			maxi1 = i;
		}
		float maxtfw2 = 0.0;
		int32_t maxi2;
		for ( int32_t i = 0 ; i< dpx->m_numSingles ; i++ ) {
			if ( i == maxi1 ) continue;
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = ssi->m_tfWeight;
			if ( tfwi <= maxtfw2 ) continue;
			maxtfw2 = tfwi;
			maxi2 = i;
		}
		// only 1 term?
		if ( maxtfw2 == 0.0 ) maxtfw2 = maxtfw1;
		// best term freqs
		max *= maxtfw1 * maxtfw2;
		// site rank effect
		max *= MAXSITERANK/SITERANKDIVISOR + 1;
		sb->safePrintf ("\t\t<theoreticalMaxFinalScore>%f"
			       "</theoreticalMaxFinalScore>\n",
			       max );
	}
	


	// debug msg
	log ( LOG_TIMING ,
	     "query: Got %"INT32" search results in %"INT64" ms for q=%s",
	      numResults,gettimeofdayInMilliseconds()-st->m_startTime,
	      qq.getQuery());

	//Highlight h;

	//st->m_qe[0] = '\0';
	st->m_qesb.nullTerm();

	// encode query buf
	//char qe[MAX_QUERY_LEN+1];
	char *dq    = si->m_displayQuery;
	//int32_t  dqlen = si->m_displayQueryLen;
	if ( dq ) st->m_qesb.urlEncode(dq);

	// how many results were requested?
	//int32_t docsWanted = msg40->getDocsWanted();

	// store html head into p, but stop at %q
	//char *head = cr->m_htmlHead;
	//int32_t  hlen = cr->m_htmlHeadLen;
	//if ( ! si->m_xml ) sb->safeMemcpy ( head , hlen );


	// ignore imcomplete or invalid multibyte or wide characters errors
	//if ( g_errno == EILSEQ ) {
	//	log("query: Query error: %s. Ignoring.", mstrerror(g_errno));
	//	g_errno = 0;
	//}

	// secret search backdoor
	if ( qlen == 7 && q[0]=='3' && q[1]=='b' && q[2]=='Y' &&
	     q[3]=='6' && q[4]=='u' && q[5]=='2' && q[6]=='Z' ) {
		sb->safePrintf ( "<br><b>You owe me!</b><br><br>" );
	}

	// print it with commas into "thbuf" and null terminate it
	char thbuf[64];
	ulltoa ( thbuf , totalHits );

	char inbuf[128];
	ulltoa ( inbuf , docsInColl );

        Query qq3;
	//Query *qq2;
	//bool firstIgnored;
	//bool isAdmin = si->m_isMasterAdmin;
	bool isAdmin = (si->m_isMasterAdmin || si->m_isCollAdmin);
	if ( si->m_format != FORMAT_HTML ) isAdmin = false;

	// otherwise, we had no error
	if ( numResults == 0 && si->m_format == FORMAT_HTML ) {
		sb->safePrintf ( "No results found in <b>%s</b> collection.",
				cr->m_coll);
	}
	// the token is currently in the collection name so do not show that
	else if ( numResults == 0 && 
		  ( si->m_format == FORMAT_WIDGET_IFRAME ||
		    si->m_format == FORMAT_WIDGET_AJAX ) ) {
		sb->safePrintf ( "No results found. Wait for spider to "
				 "kick in.");
	}
	else if ( moreFollow && si->m_format == FORMAT_HTML ) {
		if ( isAdmin && si->m_docsToScanForReranking > 1 )
			sb->safePrintf ( "PQR'd " );
		sb->safePrintf ("Results <b>%"INT32"</b> to <b>%"INT32"</b> of "
			       "exactly <b>%s</b> from an index "
			       "of about %s pages" , 
			       firstNum + 1          ,
			       firstNum + n          ,
			       thbuf                 ,
			       inbuf
			       );
	}
	// otherwise, we didn't get enough results to show this page
	else if ( si->m_format == FORMAT_HTML ) {
		if ( isAdmin && si->m_docsToScanForReranking > 1 )
			sb->safePrintf ( "PQR'd " );
		sb->safePrintf ("Results <b>%"INT32"</b> to <b>%"INT32"</b> of "
			       "exactly <b>%s</b> from an index "
			       "of about %s pages" , 
			       firstNum + 1          ,
			       firstNum + n          ,
			       thbuf                 ,
			       inbuf
			       );
	}

	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf(" in %.02f seconds",((float)st->m_took)/1000.0);


	//
	// if query was a url print add url msg
	//
	char *url = NULL;
	if ( !strncmp(q,"url:"    ,4) && qlen > 4 ) url = q+4;
	if ( !strncmp(q,"http://" ,7) && qlen > 7 ) url = q;
	if ( !strncmp(q,"https://",8) && qlen > 8 ) url = q;
	if ( !strncmp(q,"www."    ,4) && qlen > 4 ) url = q;
	// find end of url
	char *ue = url;
	for ( ; ue && *ue && ! is_wspace_a(*ue) ; ue++ ) ;
	if ( numResults == 0 && si->m_format == FORMAT_HTML && url ) {
		sb->safePrintf("<br><br>"
			      "Could not find that url in the "
			      "index. Try <a href=/addurl?u=");
		sb->urlEncode(url,ue-url,false,false);
		sb->safePrintf(">Adding it.</a>");
	}

	// sometimes ppl search for "www.whatever.com" so ask them if they
	// want to search for url:www.whatever.com
	if ( numResults > 0  && si->m_format == FORMAT_HTML && url && url ==q){
		sb->safePrintf("<br><br>"
			      "Did you mean to "
			      "search for the url "
			      "<a href=/search?q=url%%3A");
		sb->urlEncode(url,ue-url,false,false);
		sb->safePrintf(">");
		sb->safeMemcpy(url,ue-url);
		sb->safePrintf("</a> itself?");
	}


	// is it the main collection?
	bool isMain = false;
	if ( collLen == 4 && strncmp ( coll, "main", 4) == 0 ) isMain = true;

	// print "in collection ***" if we had a collection
	if (collLen>0 && numResults>0 && !isMain && si->m_format==FORMAT_HTML )
		sb->safePrintf (" in collection <b>%s</b>",coll);


	//char *pwd = si->m_pwd;
	//if ( ! pwd ) pwd = "";

	/*
	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf(" &nbsp; <u><b><font color=blue><a onclick=\""
			      "for (var i = 0; i < %"INT32"; i++) {"
			      "var nombre;"
			      "nombre = 'r' + i;"
			      "var e = document.getElementById(nombre);"
			      "if ( e == null ) continue;"
			      "if ( e.style.display == 'none' ){"
			      "e.style.display = '';"
			      "}"
			      "else {"
			      "e.style.display = 'none';"
			      "}"
			      "}"
			      "\">"
			      "[show scores]"
			      "</a></font></b></u> ",
			      numResults );
	*/

	/*
	// convenient admin link
	if ( isAdmin ) {
		sb->safePrintf(" &nbsp; "
			      ""
			      "<a style=color:green; "
			       "href=\"/admin/settings?c=%s\">"
			      "admin"
			      "</a>",coll);
		// print reindex link
		// get the filename directly
		char *langStr = si->m_defaultSortLang;
		if ( numResults>0 )
			sb->safePrintf (" &nbsp; "
					""
					"<a style=color:green; "
					"href=\"/admin/reindex?c=%s&"
					"qlang=%s&q=%s\">"
					"respider these results"
					"</a>"
					" ",coll, langStr , st->m_qe );
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; "
				"href=\"/admin/inject?c=%s&qts=%s\">"
			       "scrape google/bing</a>"
			       " ", coll , st->m_qe );
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; "
				"href=\"/search?sb=1&c=%s&"
			       "qlang=%s&q=%s\">"
			       "show banned results</a>"
			       " ", coll , langStr , st->m_qe );

		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; "
				"href=\"/admin/api?&c=%s\">api</a>"
				, coll );
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; "
				"href=\"/search?format=xml&c=%s&"
			       "qlang=%s&q=%s\">"
			       "xml</a>"
			       " ", coll , langStr , st->m_qe );
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; "
				"href=\"/search?format=json&c=%s&"
			       "qlang=%s&q=%s\">"
			       "json</a>"
			       " ", coll , langStr , st->m_qe );
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; "
				"href=\"/search?admin=0&c=%s&"
			       "qlang=%s&q=%s\">"
			       "hide admin links</a>"
			       " ", coll , langStr , st->m_qe );
	}

	// if its an ip: or site: query, print ban link
	if ( isAdmin && strncmp(si->m_displayQuery,"ip:",3)==0) {
		// get the ip
		char *ips = si->m_displayQuery + 3;
		// copy to buf, append a ".0" if we need to
		char buf [ 32 ];
		int32_t i ;
		int32_t np = 0;
		for ( i = 0 ; i<29 && (is_digit(ips[i])||ips[i]=='.'); i++ ){
			if ( ips[i] == '.' ) np++;
			buf[i]=ips[i];
		}
		// if not enough periods bail
		if ( np <= 1 ) goto skip2;
		if ( np == 2 ) { buf[i++]='.'; buf[i++]='0'; }
		buf[i] = '\0';
		// search ip back or forward
		int32_t ip = atoip(buf,i);
		sb->safePrintf ("&nbsp "
			       "<a style=color:green; "
				"href=\"/search?q=ip%%3A%s&c=%s&n=%"INT32"\">"
			       "[prev %s]</a>" , 
			       iptoa(ip-0x01000000),coll,docsWanted,
			       iptoa(ip-0x01000000));
		sb->safePrintf ("&nbsp "
			       "<a style=color:green; "
				"href=\"/search?q=ip%%3A%s&c=%s&n=%"INT32"\">"
			       "[next %s]</a>" , 
			       iptoa(ip+0x01000000),coll,docsWanted,
			       iptoa(ip+0x01000000));
	}
	// if its an ip: or site: query, print ban link
	if ( isAdmin && strncmp(si->m_displayQuery,"site:",5)==0) {
		// get the ip
		char *start = si->m_displayQuery + 5;
		char *sp = start;
		while ( *sp && ! is_wspace_a(*sp) ) sp++;
		char c = *sp;
		// get the filename directly
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; href=\"/admin/tagdb?"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s\">"
			       "[ban %s]</a>"
				" ",coll , start );
		*sp = c;
	}
	if ( isAdmin && strncmp(si->m_displayQuery,"gbad:",5)==0) {
		// get the ip
		char *start = si->m_displayQuery + 5;
		char *sp = start;
		while ( *sp && ! is_wspace_a(*sp) ) sp++;
		char c = *sp;
		*sp = '\0';
		sb->safePrintf (" &nbsp; "
			       ""
			       "<a style=color:green; href=\"/admin/tagdb?"
			       //"tagid0=%"INT32"&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s"
			       "&u=%s-gbadid.com\">"
			       "[ban %s]</a>"
				" ", coll , start , start );
		*sp = c;
	}
 skip2:

	// cache switch for admin
	if ( isAdmin && msg40->getCachedTime() > 0 ) {
		// get the filename directly
		sb->safePrintf(" &nbsp; "
			      ""
			      "<a style=color:green; href=\"/search?c=%s",
			      coll );
		// finish it
		sb->safePrintf("&q=%s&rcache=0&seq=0&rtq=0\">"
			      "[cache off]</a>"
			      " ", st->m_qe );
	}
	*/



	printIgnoredWords ( sb , si );


	if ( si->m_format == FORMAT_HTML ) sb->safePrintf("<br><br>");

	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf("<table cellpadding=0 cellspacing=0>"
			      "<tr><td valign=top>");

	// two pane table
	//if ( si->m_format == FORMAT_HTML ) 
	//	sb->safePrintf("</td><td valign=top>");

	// did we get a spelling recommendation?
	if ( si->m_format == FORMAT_HTML && st->m_spell[0] ) {
		// encode the spelling recommendation
		int32_t len = gbstrlen ( st->m_spell );
		char qe2[MAX_FRAG_SIZE];
		urlEncode(qe2, MAX_FRAG_SIZE, st->m_spell, len);
		sb->safePrintf ("<font size=+0 color=\"#c62939\">Did you mean:"
			       "</font> <font size=+0>"
			       "<a href=\"/search?q=%s",
			       qe2 );
		// close it up
		sb->safePrintf ("\"><i><b>");
		sb->utf8Encode2(st->m_spell, len);
		// then finish it off
		sb->safePrintf ("</b></i></a></font>\n<br><br>\n");
	}

	// . Wrap results in a table if we are using ads. Easier to display.
	//Ads *ads = &st->m_ads;
	//if ( ads->hasAds() )
        //        sb->safePrintf("<table width=\"100%%\">\n"
        //                    "<tr><td style=\"vertical-align:top;\">\n");

	// debug
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: Printing up to %"INT32" results. "
		     "bufStart=0x%"PTRFMT"", 
		     numResults,
		     (PTRTYPE)sb->getBuf());


	//
	// BEGIN PRINT THE RESULTS
	//

	//sb->safePrintf("<iframe src=\"http://10.5.54.154:8000/\"></iframe>");
	//sb->safePrintf("<iframe src=\"http://www.google.com/cse?cx=013269018370076798483%3A8eec3papwpi&ie=UTF-8&q=poop\"></iframe>");

	/*
	sb->safePrintf(

		      "<script type=\"text/javascript\">\n"
		      "function handler() {\n" 
		      "if(this.readyState == 4 ) {\n"
		      "document.getElementById('foobar').innerHTML="
		      "'fuck'+this.responseText;\n"
		      "alert(this.status+this.statusText+this.responseXML+this.responseText);\n"
		      "}}\n"
		      "</script>\n"

		      "<div id=foobar onclick=\""
		      "var client = new XMLHttpRequest();\n"
		      "client.onreadystatechange = handler;\n"
		      //"var url='http://www.google.com/search?q=test';\n"
		      //"var url='http://10.5.54.154:8000/';\n"
		      "var url='http://10.5.1.203:8000/';\n"
		      "client.open('GET', url );\n"
		      "client.send();\n"
		      "\">CLICK ME</div>\n"

		      );
	*/

	/*
	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf("<a onclick=\""
			      "var e = "
			      "document.getElementsByTagName('html')[0];\n"
			      "alert ('i='+e.innerHTML);"
			      "\">"
			      "CLICK ME"
			      "</a>");
	*/

	//
	// DONE PRINTING SEARCH RESULTS HEADER
	//
	return true;
}


bool printSearchResultsTail ( State0 *st ) {


	SafeBuf *sb = &st->m_sb;

	SearchInput *si = &st->m_si;	

	Msg40 *msg40 = &(st->m_msg40);

	CollectionRec *cr = si->m_cr;
	char *coll = cr->m_coll;


	// if ended in ",\n" cuz it was json, remove that
	//if ( si->m_format == FORMAT_JSON && sb->length() >= 4 ) {
	//	char *p = sb->getBuf() - 2;
	//	if ( p[0] ==',' && p[1] == '\n' ) sb->incrementLength(-2);
	//}

	if ( si->m_format == FORMAT_JSON ) {	
		// remove last },\n if there and replace with just \n
		char *e = sb->getBuf() - 2;
		if ( sb->length()>=2 &&
		     e[0]==',' && e[1]=='\n') {
			sb->m_length -= 2;
			sb->safePrintf("\n");
		}
		// print ending ] for json search results
		sb->safePrintf("]\n");

		// when streaming results we lookup the facets last
		if ( si->m_streamResults ) 
			msg40->printFacetTables ( sb );

		if ( st->m_header ) sb->safePrintf("}\n");

		//////////////////////
		// for some reason if we take too long to write out this
		// tail we get a SIGPIPE on a firefox browser.
		//////////////////////

		// all done for json
		return true;
	}

	// grab the query
	char  *q    = msg40->getQuery();
	int32_t   qlen = msg40->getQueryLen();

	HttpRequest *hr = &st->m_hr;

	// get some result info from msg40
	int32_t firstNum   = msg40->getFirstResultNum() ;

	// end the two-pane table
	if ( si->m_format == FORMAT_HTML) sb->safePrintf("</td></tr></table>");

	// for storing a list of all of the sites we displayed, now we print a 
	// link at the bottom of the page to ban all of the sites displayed 
	// with one click
	SafeBuf banSites;

	//int32_t tailLen = 0;
	//char *tail = NULL;


	//
	// PRINT PREV 10 NEXT 10 links!
	// 

	// center everything below here
	if ( si->m_format == FORMAT_HTML ) sb->safePrintf ( "<br><center>" );

	int32_t remember = sb->length();

	// now print "Prev X Results" if we need to
	if ( firstNum < 0 ) firstNum = 0;

	char abuf[300];
	SafeBuf args(abuf,300);
	// show banned?
	if ( si->m_showBanned && ! si->m_isMasterAdmin )
		args.safePrintf("&sb=1");
	if ( ! si->m_showBanned && si->m_isMasterAdmin )
		args.safePrintf("&sb=0");

	//HttpRequest *hr = &st->m_hr;

	// collection
	args.safePrintf("&c=%s",coll);
	// formatting info
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		args.safePrintf("&format=widget");
		int32_t widgetwidth = hr->getLong("widgetwidth",250);
		args.safePrintf("&widgetwidth=%"INT32"",widgetwidth);
	}

	// carry over the sites we are restricting the search results to
	if ( si->m_sites )
		//whiteListBuf.getBufStart());
		args.safePrintf("&sites=%s",si->m_sites);


	if ( si->m_format == FORMAT_HTML &&
	     msg40->m_omitCount ) { // && firstNum == 0 ) { 
		// . add our cgi to the original url
		// . so if it has &qlang=de and they select &qlang=en
		//   we have to replace it... etc.
		StackBuf(newUrl);
		// show banned results
		replaceParm2 ("sb=1",
			      &newUrl,
			      hr->m_origUrlRequest,
			      hr->m_origUrlRequestLen );
		// no deduping by summary or content hash etc.
		StackBuf(newUrl2);
		replaceParm2("dr=0",&newUrl2,newUrl.getBufStart(),
			     newUrl.length());
		// and no site clustering
		StackBuf( newUrl3 );
		replaceParm2 ( "sc=0", &newUrl3 , newUrl2.getBufStart(),
			     newUrl2.length());
		// start at results #0 again
		StackBuf( newUrl4 );
		replaceParm2 ( "s=0", &newUrl4 , newUrl3.getBufStart(),
			     newUrl3.length());
		// show errors
		StackBuf( newUrl5 );
		replaceParm2 ( "showerrors=1", 
			       &newUrl5 , 
			       newUrl4.getBufStart(),
			       newUrl4.length());
		
		
		sb->safePrintf("<center>"
			       "<i>"
			       "%"INT32" results were omitted because they "
			       "were considered duplicates, banned, errors "
			       "<br>"
			       "or "
			       "from the same site as other results. "
			       "<a href=%s>Click here to show all results</a>."
			       "</i>"
			       "</center>"
			       "<br><br>"
			       , msg40->m_omitCount
			       , newUrl5.getBufStart() );
	}


	if ( firstNum > 0 && 
	     (si->m_format == FORMAT_HTML || 
	      si->m_format == FORMAT_WIDGET_IFRAME //||
	      //si->m_format == FORMAT_WIDGET_AJAX
	      ) ) {
		int32_t ss = firstNum - msg40->getDocsWanted();
		
		//sb->safePrintf("<a href=\"/search?s=%"INT32"&q=",ss);
		// our current query parameters
		//sb->safeStrcpy ( st->m_qe );
		// print other args if not zero
		//sb->safeMemcpy ( &args );

		// make the cgi parm to add to the original url
		char nsbuf[128];
		sprintf(nsbuf,"s=%"INT32"",ss);
		// get the original url and add/replace in &s=xxx
		StackBuf ( newUrl );
		replaceParm ( nsbuf , &newUrl , hr );


		// close it up
		sb->safePrintf ("<a href=\"%s\"><b>"
			       "<font size=+0>Prev %"INT32" Results</font>"
			       "</b></a>"
				, newUrl.getBufStart()
				, msg40->getDocsWanted() );
	}

	// now print "Next X Results"
	if ( msg40->moreResultsFollow() && 
	     (si->m_format == FORMAT_HTML || 
	      si->m_format == FORMAT_WIDGET_IFRAME 
	      //si->m_format == FORMAT_WIDGET_AJAX 
	      )) {
		int32_t ss = firstNum + msg40->getDocsWanted();
		// print a separator first if we had a prev results before us
		if ( sb->length() > remember ) sb->safePrintf ( " &nbsp; " );
		// add the query
		//sb->safePrintf ("<a href=\"/search?s=%"INT32"&q=",ss);
		// our current query parameters
		//sb->safeStrcpy ( st->m_qe );
		// print other args if not zero
		//sb->safeMemcpy ( &args );

		// make the cgi parm to add to the original url
		char nsbuf[128];
		sprintf(nsbuf,"s=%"INT32"",ss);
		// get the original url and add/replace in &s=xxx
		StackBuf(newUrl);
		replaceParm ( nsbuf , &newUrl , hr );

		// close it up
		sb->safePrintf("<a href=\"%s\"><b>"
			      "<font size=+0>Next %"INT32" Results</font>"
			      "</b></a>"
			       , newUrl.getBufStart()
			       , msg40->getDocsWanted() );
	}


	// print try this search on...
	// an additional <br> if we had a Next or Prev results link
	if ( sb->length() > remember &&
	     si->m_format == FORMAT_HTML ) 
		sb->safeMemcpy ("<br>" , 4 ); 

	//
	// END PRINT PREV 10 NEXT 10 links!
	// 

	// end results table cell... and print calendar at top
	//tail = cr->m_htmlTail;
	//tailLen = gbstrlen (tail );
	//if ( si->m_format == FORMAT_HTML ) sb->safeMemcpy ( tail , tailLen );

	if ( si->m_format == FORMAT_HTML ) {
		/*
		sb->safePrintf("<table cellpadding=2 cellspacing=0 border=0>"
			      "<tr><td></td>"
			      "<td valign=top align=center>"
			      "<nobr>"
			      "<input type=text name=q2 size=60 value=\"" );
		sb->htmlEncode ( si->m_sbuf1.getBufStart() , 
				si->m_sbuf1.length() ,
				false );
		sb->safePrintf("\">"
			      "<input type=submit value=\"Search\" border=0>"
			      "</nobr><br>"
			      "</td><td></td>"
			      "</tr>"
			      );
		sb->safePrintf("</table>");
		*/
		sb->safePrintf("<input name=c type=hidden value=\"%s\">",coll);
	}

	bool isAdmin = (si->m_isMasterAdmin || si->m_isCollAdmin);
	if ( si->m_format != FORMAT_HTML ) isAdmin = false;

	if ( isAdmin && banSites.length() > 0 )
		sb->safePrintf ("<br><br><div align=right><b>"
			       "<a style=color:green; href=\"/admin/tagdb?"
			       //"tagid0=%"INT32"&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s&uenc=1&u=%s\">"
			       "[ban all of these domains]</a></b></div>"
			       "<br>\n ", 
			       coll, banSites.getBufStart());


	// TODO: print cache line in light gray here
	// TODO: "these results were cached X minutes ago"
	if ( msg40->getCachedTime() > 0 && si->m_format == FORMAT_HTML ) {
		sb->safePrintf("<br><br><font size=1 color=707070>"
			       "<b><center>");
		sb->safePrintf ( " These results were cached " );
		// this cached time is this local cpu's time
		int32_t diff = getTime() - msg40->getCachedTime();
		if      ( diff < 60   ) sb->safePrintf ("%"INT32" seconds", diff );
		else if ( diff < 2*60 ) sb->safePrintf ("1 minute");
		else                    sb->safePrintf ("%"INT32" minutes",diff/60);
		sb->safePrintf ( " ago. [<a href=\"/pageCache.html\">"
				"<font color=707070>Info</font></a>]");
		sb->safePrintf ( "</center></font>");
	}

	

	if ( si->m_format == FORMAT_XML ) {

		// when streaming results we lookup the facets last
		if ( si->m_streamResults ) 
			msg40->printFacetTables ( sb );

		sb->safePrintf("</response>\n");
	}

	if ( si->m_format == FORMAT_HTML && 
	     ! g_conf.m_isMattWells &&
	     cr->m_htmlTail.length() == 0 ) {
		sb->safePrintf ( "<br>"
				 "<center>"
				 "<font color=gray>"
				 "Copyright &copy; 2014. All Rights "
				 "Reserved.<br/>"
				 "Powered by the <a href=\"http://www."
				 "gigablast.com/\">GigaBlast</a> open source "
				 "search engine."
				 "</font>"
				 "</center>\n"
				 "<br>\n"
				 );
	}


	// if we did not use ajax, print this tail here now
	if ( si->m_format == FORMAT_HTML && ! g_conf.m_isMattWells ) {
		sb->safePrintf( "</body>\n"
				"</html>\n"
				);
	}

	// ajax widgets will have this outside the downloaded content
	if ( si->m_format == FORMAT_WIDGET_IFRAME ) {
		sb->safePrintf ( "<br>"
				"<center>"
				"<font color=gray>"
				 // link to edit the list of widget sites
				 // or various other widget content properties
				 // because we can't edit the width/height
				 // of the widget like this.
				 "<a href=/widget?inlineedit=1>edit</a> "
				 "&bull; "
				 //"Copyright &copy; 2014. All Rights "
				 //"Reserved.<br/>"
				"Powered by <a href=http://www.diffbot.com/>"
				 "Diffbot</a>."
				"</font>"
				"</center>\n"
				
				"</body>\n"
				"</html>\n"
				 );
	}

	if ( sb->length() == 0 && si && si->m_format == FORMAT_JSON )
		sb->safePrintf("[]\n");

	if ( sb->length() == 0 ) {
		sb->pushChar('\n');
		sb->nullTerm();
	}

	if ( si->m_format == FORMAT_HTML &&
	     cr->m_htmlTail.length() &&
	     ! expandHtml ( *sb ,
			    cr->m_htmlTail.getBufStart(),
			    cr->m_htmlTail.length(),
			    q,
			    qlen,
			    hr,
			    si,
			    NULL, // method,
			    cr) )
			return false;

	return true;
}

bool printTimeAgo ( SafeBuf *sb, time_t ts , char *prefix , SearchInput *si ) {
	// Jul 23, 1971
	sb->reserve2x(200);
	int32_t now = getTimeGlobal();
	// for printing
	int32_t mins = 1000;
	int32_t hrs  = 1000;
	int32_t days ;
	if ( ts > 0 ) {
		mins = (int32_t)((now - ts)/60);
		hrs  = (int32_t)((now - ts)/3600);
		days = (int32_t)((now - ts)/(3600*24));
		if ( mins < 0 ) mins = 0;
		if ( hrs  < 0 ) hrs  = 0;
		if ( days < 0 ) days = 0;
	}
	// print the time ago
	if      ( mins ==1)
		sb->safePrintf(" - %s: %"INT32" minute ago",prefix,mins);
	else if (mins<60)
		sb->safePrintf ( " - %s: %"INT32" minutes ago",prefix,mins);
	else if ( hrs == 1 )
		sb->safePrintf ( " - %s: %"INT32" hour ago",prefix,hrs);
	else if ( hrs < 24 )
		sb->safePrintf ( " - %s: %"INT32" hours ago",prefix,hrs);
	else if ( days == 1 )
		sb->safePrintf ( " - %s: %"INT32" day ago",prefix,days);
	else if (days< 7 )
		sb->safePrintf ( " - %s: %"INT32" days ago",prefix,days);
	// do not show if more than 1 wk old! we want to seem as
	// fresh as possible
	else if ( ts > 0 ) { // && si->m_isMasterAdmin ) {
		struct tm *timeStruct = localtime ( &ts );
		sb->safePrintf(" - %s: ",prefix);
		char tmp[100];
		strftime(tmp,100,"%b %d %Y",timeStruct);
		sb->safeStrcpy(tmp);
	}
	return true;
}

int linkSiteRankCmp (const void *v1, const void *v2) {
	Inlink *i1 = *(Inlink **)v1;
	Inlink *i2 = *(Inlink **)v2;
	return i2->m_siteRank - i1->m_siteRank;
}

bool printInlinkText ( SafeBuf *sb , Msg20Reply *mr , SearchInput *si ,
		       int32_t *numPrinted ) {
	*numPrinted = 0;
	// . show the "LinkInfo"
	// . Msg20.cpp will have "computed" the LinkInfo if we set
	//   Msg20Request::m_computeLinkInfo to true, but if we set
	//   Msg20Request::m_getLinkInfo to true it will just get it
	//   from the TitleRec, which is much faster but more stale.
	// . "&inlinks=1" is slow and fresh, "&inlinks=2" is fast
	//   and stale. Both are really only for BuzzLogic.
	LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
	// sanity
	if ( info && mr->size_linkInfo!=info->m_lisize ){char *xx=NULL;*xx=0; }
	// NULLify if empty
	if ( mr->size_linkInfo <= 0 ) info = NULL;
	// do not both if none
	if ( info && ! info->m_numStoredInlinks ) info = NULL;
	// bail?
	if ( ! info ) return true;
	// now sort them up
	Inlink *k = info->getNextInlink(NULL);
	// #define from Linkdb.h
	Inlink *ptrs[MAX_LINKERS];
	int32_t numLinks = 0;
	for ( ; k ; k = info->getNextInlink(k) ) {
		ptrs[numLinks++] = k;
		if ( numLinks >= MAX_LINKERS ) break;
	}
	// sort them
	gbsort ( ptrs , numLinks , sizeof(Inlink *) , linkSiteRankCmp );
	// print xml starter
	if ( si->m_format == FORMAT_XML ) sb->safePrintf("\t\t<inlinks>\n");
	// loop through the inlinks
	bool printedInlinkText = false;
	bool firstTime = true;
	int32_t inlinkId = 0;
	int64_t  starttime = gettimeofdayInMillisecondsLocal();

	//int32_t icount = 0;
	//int32_t ecount = 0;
	//int32_t absSum = 0;
	for ( int32_t i = 0 ; i < numLinks ; i++ ) {
		k = ptrs[i];
		if ( ! k->getLinkText() ) continue;
		if ( ! si->m_doQueryHighlighting && 
		     si->m_format == FORMAT_HTML ) 
			continue;
		char *str   = k->getLinkText();//ptr_linkText;
		int32_t strLen = k->size_linkText;
		//char tt[1024*3];
		//char *ttend = tt + 1024*3;
		char *frontTag = 
		     "<font style=\"color:black;background-color:yellow\">" ;
		char *backTag = "</font>";
		if ( si->m_format == FORMAT_XML ) {
			frontTag = "<b>";
			backTag  = "</b>";
		}
		if ( si->m_format == FORMAT_WIDGET_IFRAME ||
		     si->m_format == FORMAT_WIDGET_AJAX ) {
			frontTag = "<font style=\"background-color:yellow\">" ;
		}

		Highlight hi;
		SafeBuf hb;
		int32_t hlen = hi.set ( &hb,//tt , 
				     //ttend - tt , 
				str, 
				strLen , 
				mr->m_language, // docLangId
				&si->m_hqq , // highlight query CLASS
				false  , // doStemming?
				false  , // use click&scroll?
				NULL   , // base url
				frontTag,
				backTag,
				0,
				0 ); // niceness
		if ( hlen <= 0 ) continue;
		// skip it if nothing highlighted
		if ( hi.getNumMatches() == 0 ) continue;

		if ( si->m_format == FORMAT_XML ) {
			sb->safePrintf("\t\t\t<inlink "
				      "docId=\"%"INT64"\" "
				      "url=\"",
				      k->m_docId );
			// encode it for xml
			sb->htmlEncode ( k->getUrl(),//ptr_urlBuf,
					k->size_urlBuf - 1 , false );
			sb->safePrintf("\" "
				      //"hostId=\"%"UINT32"\" "
				      "firstindexed=\"%"UINT32"\" "
				      // not accurate!
				      //"lastspidered=\"%"UINT32"\" "
				      "wordposstart=\"%"INT32"\" "
				      "id=\"%"INT32"\" "
				      "siterank=\"%"INT32"\" "
				      "text=\"",
				      //hh ,
				      //(int32_t)k->m_datedbDate,
				      (uint32_t)k->m_firstIndexedDate,
				      //(uint32_t)k->m_lastSpidered,
				      (int32_t)k->m_wordPosStart,
				      inlinkId,
				      //linkScore);
				      (int32_t)k->m_siteRank
				      );
			// HACK!!!
			k->m_siteHash = inlinkId;
			// inc it
			inlinkId++;
			// encode it for xml
			if ( !sb->htmlEncode ( hb.getBufStart(),
					      hb.length(),
					      false)) 
				return false;
			sb->safePrintf("\"/>\n");
			continue;
		}


		if ( firstTime ) {
			sb->safePrintf("<font size=-1>");
			sb->safePrintf("<table border=1>"
				      "<tr><td colspan=10>"
				      "<center>"
				      "<b>Inlinks with Query Terms</b>"
				      "</center>"
				      "</td></tr>"
				      "<tr>"
				      "<td>Inlink Text</td>"
				      "<td>From Site</td>"
				      "<td>Site IP</td>"
				      "<td>Site Rank</td>"
				      "</tr>"
				      );
		}
		firstTime = false;
		sb->safePrintf("<tr><td>"
			      "<a href=/get?c=%s&d=%"INT64"&cnsp=0>"
			      //"<a href=\"/print?"
			      //"page=7&"
			      //"c=%s&"
			      //"d=%"INT64"\">"
			      //k->getUrl());
			      ,si->m_cr->m_coll
			      ,k->m_docId);
		if ( ! sb->safeMemcpy(&hb) ) return false;
		int32_t hostLen = 0;
		char *host = getHostFast(k->getUrl(),&hostLen,NULL);
		sb->safePrintf("</td><td>");
		if ( host ) sb->safeMemcpy(host,hostLen);
		sb->safePrintf("</td><td>");
		sb->safePrintf("<a href=/search?c=%s&q=ip%%3A%s"
			       "+gbsortbyint%%3Agbsitenuminlinks&n=100>"
			       ,si->m_cr->m_coll,iptoa(k->m_ip));
		sb->safePrintf("%s</a>",iptoa(k->m_ip));
		sb->safePrintf("</td><td>%"INT32"</td></tr>"
			       ,(int32_t)k->m_siteRank);
		//sb->safePrintf("<br>");
		printedInlinkText = true;
		*numPrinted = *numPrinted + 1;
	}

	int64_t took = gettimeofdayInMillisecondsLocal() - starttime;
        if ( took > 2 )
                log("timing: took %"INT64" ms to highlight %"INT32" links."
                    ,took,numLinks);


	// closer for xml
	if ( si->m_format == FORMAT_XML ) sb->safePrintf("\t\t</inlinks>\n");
	//if ( printedInlinkText ) sb->safePrintf("<br>\n");
	if ( printedInlinkText )
		sb->safePrintf("</font>"
			      "</table>"
			      "<br>");
	return true;
}

//
// . print a dmoz topic for the given numeric catid UNDER search result
// . print "Search in Category" link as well
//
static bool printDMOZCategoryUnderResult ( SafeBuf *sb , 
					   SearchInput *si, 
					   int32_t catid ,
					   State0 *st ) {

	char format = si->m_format;
	// these are handled in the <dmozEntry> logic below now
	if ( format != FORMAT_HTML ) return true;

	// if ( format == FORMAT_XML ) {
	// 	sb->safePrintf("\t\t<dmozCat>\n"
	// 		       "\t\t\t<dmozCatId>%"INT32"</dmozCatId>\n"
	// 		       "\t\t\t<dmozCatStr><![CDATA["
	// 		       ,catid);
	// 	// print the name of the dmoz category
	// 	char xbuf[256];
	// 	SafeBuf xb(xbuf,256,0,false);
	// 	g_categories->printPathFromId(&xb, catid, false,si->m_isRTL); 
	// 	sb->cdataEncode(xb.getBufStart());
	// 	sb->safePrintf("]]></dmozCatStr>\n");
	// 	sb->safePrintf("\t\t</dmozCat>\n");
	// 	return true;
	// }

	// if ( format == FORMAT_JSON ) {
	// 	sb->safePrintf("\t\t\"dmozCat\":{\n"
	// 		       "\t\t\t\"dmozCatId\":%"INT32",\n"
	// 		       "\t\t\t\"dmozCatStr\":\""
	// 		       ,catid);
	// 	// print the name of the dmoz category
	// 	char xbuf[256];
	// 	SafeBuf xb(xbuf,256,0,false);
	// 	g_categories->printPathFromId(&xb, catid, false,si->m_isRTL);
	// 	sb->jsonEncode(xb.getBufStart());
	// 	sb->safePrintf("\"\n"
	// 		       "\t\t},\n");


	// 	return true;
	// }

	//uint8_t queryLanguage = langUnknown;
	uint8_t queryLanguage = si->m_queryLangId;
	// Don't print category if not in native language category
	// Note that this only trims out "World" cats, not all
	// of them. Some of them may still sneak in.
	//if(si->m_langHint)
	//	queryLanguage = si->m_langHint;
	if(queryLanguage != langUnknown) {
		char tmpbuf[1024];
		SafeBuf langsb(tmpbuf, 1024);
		g_categories->printPathFromId(&langsb, catid, false);
		char *ptr = langsb.getBufStart();
		uint8_t lang = g_langId.findLangFromDMOZTopic(ptr + 7);
		if(!strncmp("World: ", ptr, 6) &&
		   lang != langUnknown &&
		   lang != queryLanguage)
			// do not print it if not in our language
			return true;
	}
	//////
	//
	// print a link to apply your query to this DMOZ category
	//
	//////
	sb->safePrintf("<a href=\"/search?s=0&q=gbipcatid%%3A%"INT32"",catid);
	sb->urlEncode("|",1);
	sb->urlEncode(si->m_sbuf1.getBufStart(),si->m_sbuf1.length());
	sb->safePrintf("\">Search in Category</a>: ");
	
	// setup the host of the url
	//if ( dmozHost )
	//	sb->safePrintf("<a href=\"http://%s/", dmozHost );
	//else
	sb->safePrintf("<a href=\"/");
	// print link
	g_categories->printPathFromId(sb, catid, true,si->m_isRTL);
	sb->safePrintf("/\">");
	// print the name of the dmoz category
	sb->safePrintf("<font color=#c62939>");
	g_categories->printPathFromId(sb, catid, false,si->m_isRTL);
	sb->safePrintf("</font></a><br>");
	//++tr.brCount;
	return true;
}


// use this for xml as well as html
bool printResult ( State0 *st, int32_t ix , int32_t *numPrintedSoFar ) {

	SafeBuf *sb = &st->m_sb;

	HttpRequest *hr = &st->m_hr;

	CollectionRec *cr = NULL;
	cr = g_collectiondb.getRec ( st->m_collnum );
	if ( ! cr ) {
		log("query: printResult: collnum %"INT32" gone",
		    (int32_t)st->m_collnum);
		return true;
	}


	// int16_tcuts
	SearchInput *si    = &st->m_si;
	Msg40       *msg40 = &st->m_msg40;

	// ensure not all cluster levels are invisible
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: result #%"INT32" clusterlevel=%"INT32"",
		     ix, (int32_t)msg40->getClusterLevel(ix));

	int64_t d = msg40->getDocId(ix);
	// this is normally a double, but cast to float
	float docScore = (float)msg40->getScore(ix);

	// do not print if it is a summary dup or had some error
	// int32_t level = (int32_t)msg40->getClusterLevel(ix);
	// if ( level != CR_OK &&
	//      level != CR_INDENT )
	// 	return true;
	


	if ( si->m_docIdsOnly ) {
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("\t<result>\n"
				       "\t\t<docId>%"INT64"</docId>\n"
				       "\t</result>\n", 
				       d );
		else if ( si->m_format == FORMAT_JSON )
			sb->safePrintf("\t\{\n"
				       "\t\t\"docId\":%"INT64"\n"
				       "\t},\n",
				       d );
		else
			sb->safePrintf("%"INT64"<br/>\n", 
				      d );
		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		return true;
	}

	Msg20      *m20 ;
	if ( si->m_streamResults )
		m20 = msg40->getCompletedSummary(ix);
	else
		m20 = msg40->m_msg20[ix];

	// get the reply
	Msg20Reply *mr = m20->m_r;
		

	// . sometimes the msg20reply is NULL so prevent it coring
	// . i think this happens if all hosts in a shard are down or timeout
	//   or something
	if ( ! mr ) {
		sb->safePrintf("<i>getting summary for docid %"INT64" had "
			       "error: %s</i><br><br>"
			       ,d,mstrerror(m20->m_errno));
		return true;
	}

	// . if section voting info was request, display now, it's in json
	// . so if in csv it will mess things up!!!
	if ( mr->ptr_sectionVotingInfo )
		// it is possible this is just "\0"
		sb->safeStrcpy ( mr->ptr_sectionVotingInfo );

	// each "result" is the actual cached page, in this case, a json
	// object, because we were called with &icc=1. in that situation
	// ptr_content is set in the msg20reply.
	if ( si->m_format == FORMAT_CSV &&
	     mr->ptr_content &&
	     // spider STATUS docs are json
	     (mr->m_contentType == CT_JSON || mr->m_contentType == CT_STATUS)){
		// parse it up
		char *json = mr->ptr_content;
		// only print header row once, so pass in that flag
		if ( ! st->m_printedHeaderRow ) {
			sb->reset();
			printCSVHeaderRow ( sb , st , mr->m_contentType );
			st->m_printedHeaderRow = true;
		}
		printJsonItemInCSV ( json , sb , st );
		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		return true;
	}

	char *xp = NULL;

	// just print cached web page?
	if ( mr->ptr_content && 
	     si->m_format == FORMAT_JSON &&
	     ( xp = strstr(mr->ptr_ubuf,"-diffbotxyz") ) ) {

		// for json items separate with \n,\n
		if ( si->m_format != FORMAT_HTML && *numPrintedSoFar > 0 )
			sb->safePrintf(",\n");

		// a dud? just print empty {}'s
		if ( mr->size_content == 1 ) 
			sb->safePrintf("{}");
		// must have an ending } otherwise it was truncated json.
		// i'm seeing this happen sometimes, i do not know if diffbot
		// or gigablast is truncating the json
		else if ( ! endsInCurly ( mr->ptr_content, mr->size_content )){
			sb->safePrintf("{"
				       "\"error\":"
				       "\"Bad JSON. "
				       "Diffbot reply was missing final "
				       "curly bracket. Truncated JSON.\""
				       "}");
			// make a note of it
			log("results: omitting diffbot reply missing curly "
			    "for %s",mr->ptr_ubuf);
		}
		// if it's a diffbot object just print it out directly
		// into the json. it is already json.
		else
			sb->safeStrcpy ( mr->ptr_content );
			

		// . let's hack the spidertime onto the end
		// . so when we sort by that using gbsortby:spiderdate
		//   we can ensure it is ordered correctly
		// As of the update on 5/13/2014, the end of sb may have whitespace, so first move away from that
		int distance; // distance from end to first non-whitespace char
		char *end;
		for (distance = 1; distance < sb->getLength(); distance++) {
		    end = sb->getBuf() - distance;
		    if (!is_wspace_a(*end))
		        break;
		}
		if ( si->m_format == FORMAT_JSON &&
		     end > sb->getBufStart() &&
		     *end == '}' ) {
			// replace trailing } with spidertime}
			sb->incrementLength(-distance);
			// comma?
			if ( mr->size_content>1 ) sb->pushChar(',');
			sb->safePrintf("\"docId\":%"INT64"", mr->m_docId);

			// we don't store it explcitly, but the guess
			// here should almost always be right.
			char *parentUrl = mr->ptr_ubuf;
			int32_t ulen = xp - parentUrl;
			parentUrl[ulen] = '\0';
			int64_t pdocId = g_titledb.getProbableDocId(parentUrl);
			sb->safePrintf(",\"parentUrlDocId\":%"INT64"", pdocId);
			parentUrl[ulen] = '-';			


			sb->safePrintf(",\"gburl\":\"");
			sb->jsonEncode(mr->ptr_ubuf);
			sb->safePrintf("\"");
			// for deduping
			//sb->safePrintf(",\"crc\":%"UINT32"",mr->m_contentHash32);
			// crap, we lose resolution storing as a float
			// so fix that shit here...
			//float f = mr->m_lastSpidered;
			//sb->safePrintf(",\"lastCrawlTimeUTC\":%.0f}",f);
			// MDW: this is VERY convenient for debugging pls
			// leave in. we can easily see if a result 
			// should be there for a query like 
			// gbmin:gbspiderdate:12345678
			sb->safePrintf(",\"lastCrawlTimeUTC\":%"INT32"",
				       mr->m_lastSpidered);
			// also include a timestamp field with an RFC 1123 formatted date
			char timestamp[50];
			struct tm *ptm =gmtime((time_t *)&mr->m_lastSpidered );
			strftime(timestamp, 50, "%a, %d %b %Y %X %Z", ptm);
			sb->safePrintf(",\"timestamp\":\"%s\"}\n", timestamp);
		}

		//mr->size_content );
		if ( si->m_format == FORMAT_HTML )
			sb->safePrintf("\n\n<br><br>\n\n");
		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		// just in case
		sb->nullTerm();
		return true;
	}

	int32_t cursor = -1;
	if ( si->m_format == FORMAT_XML  ) cursor = sb->length();
	if ( si->m_format == FORMAT_JSON ) cursor = sb->length();

	if ( si->m_format == FORMAT_XML ) 
		sb->safePrintf("\t<result>\n" );

	if ( si->m_format == FORMAT_JSON ) {
		if ( *numPrintedSoFar != 0 ) sb->safePrintf(",\n");
		sb->safePrintf("\t{\n" );
	}


	if ( mr->ptr_content && si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<content><![CDATA[" );
		sb->cdataEncode ( mr->ptr_content );
		sb->safePrintf("]]></content>\n");
	}
		
	if ( mr->ptr_content && si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"content\":\"" );
		sb->jsonEncode ( mr->ptr_content );
		sb->safePrintf("\",\n");
	}

	// print spider status pages special
	if ( mr->ptr_content && 
	     si->m_format == FORMAT_HTML &&
	     mr->m_contentType == CT_STATUS ) {
		if ( *numPrintedSoFar )
			sb->safePrintf("<br><hr><br>\n");
		// skip to gbssurl
		char *s = strstr ( mr->ptr_content,"\"gbssUrl\":");
		if ( ! s ) {
			log("results: missing gbssUrl");
			goto badformat;
		}
		// then do two columns after the two urls
		char *e = strstr ( s , "\"gbssStatusCode\":" );
		if ( ! e ) {
			log("results: missing gbssStatusCode");
			goto badformat;
		}
		// start the second column at the start of this line:
		char *m = strstr ( e , "\"gbssUsedRobotsTxt\":");
		if ( ! m ) 
			m = strstr ( e , "\"gbssConsecutiveErrors\":");
		if ( ! m ) {
			log("results: missing gbssUsedRobotsTxt");
			goto badformat;
		}
		// exclude \0
		char *end = mr->ptr_content + mr->size_content - 1;
		// use a table with 2 columns
		// so we can use \n to separate lines and don't have to add brs
		// and boldify just the main url, not the redir url!
		sb->safePrintf("<pre style=display:inline;>"
			       "\"gbssUrl\":\""
			       "<b style=color:blue;><a href=/get?"
			       "c=%s&"
			       "d=%"INT64">"
			       , cr->m_coll
			       , mr->m_docId
			       );
		char *s2 = strstr ( s , "\"gbssFinalRedirectUrl\":");
		char *bend = e - 3;
		if ( s2 ) bend = s2 - 3;
		sb->safeMemcpy ( s+11 , bend - (s+11));
		sb->safePrintf("</a></b></pre>\",<br>");
		// now print redir url if there
		if ( s2 ) {
			sb->safePrintf("<pre style=display:inline;>");
			sb->safeMemcpy ( s2 , e-s2 );
			sb->removeLastChar('\n');
			sb->safePrintf("</pre>");
		}
		sb->safePrintf("<table border=0 cellpadding=0 cellspacing=0>"
			       "<tr><td>");
		int32_t startOff = sb->getLength();
		sb->safePrintf("<pre>");
		//int32_t off = sb->length();
		sb->safeMemcpy ( e , m - e );
		sb->safePrintf("</pre>");
		sb->safePrintf("</td><td>");
		sb->safePrintf("<pre>");
		sb->safeMemcpy ( m , end - m );
		// remove last \n
		sb->removeLastChar('\n');
		sb->removeLastChar('}');
		sb->removeLastChar('\n');
		sb->safePrintf("</pre>\n");
		sb->safePrintf("</td></tr></table>");
		//////
		// insert date strings so we don't have to convert
		// numeric timestamps to human time strings manually
		//////
		char *dateStr1 = strstr(mr->ptr_content,"\"gbssSpiderTime\":");
		if ( dateStr1 ) dateStr1 += 17;
		time_t dateInt1 = 0;
		if ( dateStr1 ) dateInt1 = atoll(dateStr1);
		struct tm *timeStruct = gmtime ( &dateInt1 );
		char tmp[513];
		strftime(tmp,64," <font color=gray>"
			 "[%b-%d-%Y %H:%M:%S]</font>", timeStruct );
		// insert position is before 'find'
		char *find = "\n\"gbssFirstIndexed\":";
		// sb has multiple results in it so just match on what we just
		// printed
		char *start = sb->getBufStart() + startOff;
		char *insertPtr = strstr ( start, find );
		int32_t insertPos = -1;
		if ( insertPtr ) {
			insertPos = insertPtr - sb->getBufStart();
			sb->insert ( tmp , insertPos );
		}
		// repeat for first indexed timestamp too
		dateStr1 = strstr(mr->ptr_content,"\"gbssFirstIndexed\":");
		if ( dateStr1 ) dateStr1 += 19;
		if ( dateStr1 ) dateInt1 = atoll(dateStr1);
		timeStruct = gmtime ( &dateInt1 );
		strftime(tmp,64," <font color=gray>"
			 "[%b-%d-%Y %H:%M:%S]</font>", timeStruct );
		// insert position is before 'find'
		find = "\n\"gbssContentHash32\":";
		// sb has multiple results in it so just match on what we just
		// printed
		start = sb->getBufStart() + startOff;
		insertPtr = strstr ( start, find );
		if ( insertPtr ) {
			insertPos = insertPtr - sb->getBufStart();
			sb->insert ( tmp , insertPos );
		}
		///////
		// done inserting date strings
		//////

		/////
		// insert link for gbssDocId
		/////
		//find = "gbssDocId\":";
		// support gbssParentDocId too
		find = "DocId\":";
		start = sb->getBufStart() + startOff;
		insertPtr = strstr ( start , find );
		if ( insertPtr ) {
			insertPtr += gbstrlen(find);
			insertPos = insertPtr - sb->getBufStart();
			int64_t docId = atoll(insertPtr);
			snprintf(tmp,512,"<a href=/search?q=gbdocid%%3A%"INT64
				 "&c=%s>", docId,cr->m_coll);
			sb->insert ( tmp , insertPos );
			// now insert the corresponding </a>
			// look for comma after "gbssDocId\":"
			insertPtr = strstr ( insertPtr , "," );
			if ( insertPtr ) {
				insertPos = insertPtr - sb->getBufStart();
				sb->insert ( "</a>" , insertPos );
			}
		}
		// replace \n with <br>
		// sb->safeReplace2 ( "\n" , 1 ,
		// 		   "<br>" , 4 ,
		// 		   0,//niceness ,
		// 		   off );
		// inc it
		*numPrintedSoFar = *numPrintedSoFar + 1;
		// just in case
		sb->nullTerm();
		return true;
	}

	badformat:

	Highlight hi;

	// get the url
	char *url    = mr->ptr_ubuf      ;
	int32_t  urlLen = mr->size_ubuf - 1 ;
	int32_t  err    = mr->m_errno       ;

	// . remove any session ids from the url
	// . for speed reasons, only check if its a cgi url
	Url uu;
	uu.set ( url , urlLen, false, true );
	url    = uu.getUrl();
	urlLen = uu.getUrlLen();

	// get my site hash
	uint64_t siteHash = 0;
	if ( uu.getHostLen() > 0 ) 
		siteHash = hash64(uu.getHost(),uu.getHostLen());
	// indent it if level is 2
	bool indent = false;

	bool isAdmin = (si->m_isMasterAdmin || si->m_isCollAdmin);
	if ( si->m_format == FORMAT_XML ) isAdmin = false;

	//uint64_t lastSiteHash = siteHash;
	if ( indent && si->m_format == FORMAT_HTML ) 
		sb->safePrintf("<blockquote>"); 

	// print the rank. it starts at 0 so add 1
	if ( si->m_format == FORMAT_HTML && si->m_streamResults )
		//sb->safePrintf("<table><tr><td valign=top>%"INT32".</td><td>",
		//	       ix+1 );
		sb->safePrintf("<table><tr><td>");
	else if ( si->m_format == FORMAT_HTML )
		//sb->safePrintf("<table><tr><td valign=top>%"INT32".</td><td>",
		//	      ix+1 + si->m_firstResultNum );
		sb->safePrintf("<table><tr><td>");

	if ( si->m_showBanned ) {
		if ( err == EDOCBANNED   ) err = 0;
		if ( err == EDOCFILTERED ) err = 0;
	}

	// if this msg20 had an error print "had error"
	if ( err || urlLen <= 0 || ! url ) {
		// revert back so we do not break the json/xml
		if ( cursor >= 0 ) sb->m_length = cursor;
		// it's unprofessional to display this in browser
		// so just let admin see it
		if ( isAdmin && si->m_format == FORMAT_HTML ) {
			sb->safePrintf("<i>docId %"INT64" had error: "
				      "%s</i><br><br>",
				      mr->m_docId,//msg40->getDocId(i),
				      mstrerror(err));
		}
		// log it too!
		log("query: docId %"INT64" had error: %s.",
		    mr->m_docId,mstrerror(err));
		// wrap it up if clustered
		if ( indent && si->m_format == FORMAT_HTML) 
			sb->safeMemcpy("</blockquote>",13);
		// DO NOT inc it otherwise puts a comma in there and
		// screws up the json
		//*numPrintedSoFar = *numPrintedSoFar + 1;
		return true;
	}
	
	// the score if admin
	/*
	if ( isAdmin ) {
		int32_t level = (int32_t)msg40->getClusterLevel(ix);
		// print out score
		sb->safePrintf ( "s=%.03f "
				"docid=%"UINT64" "
				"sitenuminlinks=%"INT32"%% "
				"hop=%"INT32" "
				"cluster=%"INT32" "
				"summaryLang=%s "
				"(%s)<br>",
				(float)msg40->getScore(ix) ,
				mr->m_docId,
				(int32_t )mr->m_siteNumInlinks,
				(int32_t)mr->m_hopcount,
				level ,
				getLanguageString(mr->m_summaryLanguage),
				g_crStrings[level]);
	}
	*/

	char *diffbotSuffix = strstr(url,"-diffbotxyz");

	// print youtube and metacafe thumbnails here
	// http://www.youtube.com/watch?v=auQbi_fkdGE
	// http://img.youtube.com/vi/auQbi_fkdGE/2.jpg
	// get the thumbnail url
	if ( mr->ptr_imgUrl && 
	     si->m_format == FORMAT_HTML &&
	     // if we got thumbnail use that not this
	     ! mr->ptr_imgData )
		sb->safePrintf ("<a href=%s><img src=%s></a>",
				   url,mr->ptr_imgUrl);

	// if we have a thumbnail show it next to the search result,
	// base64 encoded. do NOT do this for the WIDGET, only for search
	// results in html/xml.
	if ( (si->m_format == FORMAT_HTML || si->m_format == FORMAT_XML ) &&
	     //! mr->ptr_imgUrl &&
	     si->m_showImages && mr->ptr_imgData ) {
		ThumbnailArray *ta = (ThumbnailArray *)mr->ptr_imgData;
		ThumbnailInfo *ti = ta->getThumbnailInfo(0);
		if ( si->m_format == FORMAT_XML )
			sb->safePrintf("\t\t");
		ti->printThumbnailInHtml ( sb , 
					   100 ,  // max width
					   100 ,  // max height
					   true ,  // add <a href>
					   NULL ,
					   " style=\"margin:10px;\" ",
					   si->m_format );
		if ( si->m_format == FORMAT_XML ) {
			sb->safePrintf("\t\t<imageHeight>%"INT32"</imageHeight>\n",
				       ti->m_dy);
			sb->safePrintf("\t\t<imageWidth>%"INT32"</imageWidth>\n",
				       ti->m_dx);
			sb->safePrintf("\t\t<origImageHeight>%"INT32""
				       "</origImageHeight>\n",
				       ti->m_origDY);
			sb->safePrintf("\t\t<origImageWidth>%"INT32""
				       "</origImageWidth>\n",
				       ti->m_origDX);
			sb->safePrintf("\t\t<imageUrl><![CDATA[");
			sb->cdataEncode(ti->getUrl());
			sb->safePrintf("]]></imageUrl>\n");
		}
		if ( si->m_format == FORMAT_JSON ) {
			sb->safePrintf("\t\t\"imageHeight\":%"INT32",\n",
				       ti->m_dy);
			sb->safePrintf("\t\t\"imageWidth\":%"INT32",\n",
				       ti->m_dx);
			sb->safePrintf("\t\t\"origImageHeight\":%"INT32",\n",
				       ti->m_origDY);
			sb->safePrintf("\t\t\"origImageWidth\":%"INT32",\n",
				       ti->m_origDX);
			sb->safePrintf("\t\t\"imageUrl\":\"");
			sb->jsonEncode(ti->getUrl());
			sb->safePrintf("\",\n");
		}
	}

	bool isWide = false;
	int32_t newdx = 0;

	// print image for widget
	if ( //mr->ptr_imgUrl && 
	     ( si->m_format == FORMAT_WIDGET_IFRAME ||
	       si->m_format == FORMAT_WIDGET_AJAX ||
	       si->m_format == FORMAT_WIDGET_APPEND ) ) {

		int32_t widgetWidth = hr->getLong("widgetwidth",200);

		// prevent coring
		if ( widgetWidth < 1 ) widgetWidth = 1;

		// char *bg1 = "lightgray";
		// char *bg2 = "white";
		// char *bgcolor = bg1;
		// if ( (ix % 1) == 1 ) bgcolor = bg2;

		// each search result in widget has a div around it
		sb->safePrintf("<div "
			       "class=result "
			       // we need the docid and score of last result
			       // when we append new results to the end
			       // of the widget for infinite scrolling
			       // using the scripts in PageBasic.cpp
			       "docid=%"INT64" "
			       "score=%f " // double

			       "style=\""
			       "width:%"INT32"px;"
			       "min-height:%"INT32"px;"//140px;"
			       "height:%"INT32"px;"//140px;"
			       "padding:%"INT32"px;"
			       //"padding-right:40px;"
			       "position:relative;"
			       // summary overflows w/o this!
			       "overflow-y:hidden;"
			       "overflow-x:hidden;"
			       // alternate bg color to separate results!
			       //"background-color:%s;"
			       //"display:table-cell;"
			       //"vertical-align:bottom;"
			       "\""
			       ">"
			       , mr->m_docId
			       // this is a double now. this won't work
			       // for streaming...
			       , msg40->m_msg3a.m_scores[ix]
			       // subtract 8 for scrollbar on right
			       , widgetWidth - 2*8 - 8 // padding is 8px
			       , (int32_t)RESULT_HEIGHT
			       , (int32_t)RESULT_HEIGHT
			       , (int32_t)PADDING
			       //, bgcolor
			       );
		// if ( mr->ptr_imgUrl )
		// 	sb->safePrintf("background-repeat:no-repeat;"
		// 		       "background-size:%"INT32"px 140px;"
		// 		       "background-image:url('%s');"
		// 		       , widgetwidth - 2*8 // padding is 8px
		// 		       , mr->ptr_imgUrl);
		if ( mr->ptr_imgData ) {
			ThumbnailArray *ta = (ThumbnailArray *)mr->ptr_imgData;
			ThumbnailInfo *ti = ta->getThumbnailInfo(0);
			// account for scrollbar on the right
			int32_t maxWidth = widgetWidth - (int32_t)SCROLLBAR_WIDTH;
			int32_t maxHeight = (int32_t)RESULT_HEIGHT;
			// false = do not print <a href> link on image
			ti->printThumbnailInHtml ( sb , 
						   maxWidth ,
						   maxHeight , 
						   false , // add <a href>
						   &newdx );
		}
		// end the div style attribute and div tag
		//sb->safePrintf("\">");


		sb->safePrintf ( "<a "
				 "target=_blank "
				 "style=\"text-decoration:none;"
				 // don't let scroll bar obscure text
				 "margin-right:%"INT32"px;"
				 ,(int32_t)SCROLLBAR_WIDTH
				 );

		// if thumbnail is wide enough put text on top of it, otherwise
		// image is to the left and text is to the right of image
		if ( newdx > .5 * widgetWidth ) {
			isWide = true;
			sb->safePrintf("position:absolute;"
				       "bottom:%"INT32";"
				       "left:%"INT32";"
				       , (int32_t) PADDING 
				       , (int32_t) PADDING 
				       );
		}
		// to align the text verticall we gotta make a textbox div
		// otherwise it wraps below image! mdw
		//else
		//	sb->safePrintf("vertical-align:middle;");
		else
			sb->safePrintf("position:absolute;"
				       "bottom:%"INT32";"
				       "left:%"INT32";"
				       , (int32_t) PADDING
				       , (int32_t) PADDING + newdx + 10 );

		// close the style and begin the url
		sb->safePrintf( "\" "
				"href=\"" 
				 );

		// truncate off -diffbotxyz%"INT32"
		int32_t newLen = urlLen;
		if ( diffbotSuffix ) newLen = diffbotSuffix - url;
		// print the url in the href tag
		sb->safeMemcpy ( url , newLen ); 
		// then finish the a href tag and start a bold for title
		sb->safePrintf ( "\">");//<font size=+0>" );
		
		sb->safePrintf("<b style=\""
			       "text-decoration:none;"
			       "font-size: 15px;"
			       "font-weight:bold;"
			       // add padding so shadow does not stick out
			       //"padding-left:4px;"
			       //"padding-right:4px;"
			       "background-color:rgba(0,0,0,.5);"
			       "color:white;"
			       "font-family:arial;"
			       //"text-shadow:2px 4px 3px rgba(0,0,1,3);"
			       "text-shadow: 2px 2px 0 #000 "
			       ",-2px -2px 0 #000 "
			       ",-2px  2px 0 #000 "
			       ", 2px -2px 0 #000 "
			       ", 2px -2px 0 #000 "
			       ", 0px -2px 0 #000 "
			       ",  0px 2px 0 #000 "
			       ", -2px 0px 0 #000 "
			       ",  2px 0px 0 #000 "
			       ";"
			       //"-2px 2px 0 #000 "
			       //"2px -2px 0 #000 "
			       //"-2px -2px 0 #000;"
			       "\">");
		//sb->safePrintf ("<img width=50 height=50 src=%s></a>",
		//		   mr->ptr_imgUrl);
		// then title over image
	}

	// only do link here if we have no thumbnail so no bg image
	if ( (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX   ) &&
	     ! mr->ptr_imgData ) {
		sb->safePrintf ( "<a style=text-decoration:none;"
				 "color:white; "
				 "href=" );
		// truncate off -diffbotxyz%"INT32"
		int32_t newLen = urlLen;
		if ( diffbotSuffix ) newLen = diffbotSuffix - url;
		// print the url in the href tag
		sb->safeMemcpy ( url , newLen ); 
		// then finish the a href tag and start a bold for title
		sb->safePrintf ( ">");//<font size=+0>" );
	}


	// the a href tag
	if ( si->m_format == FORMAT_HTML ) sb->safePrintf ( "\n\n" );

	// then if it is banned 
	if ( mr->m_isBanned && si->m_format == FORMAT_HTML )
		sb->safePrintf("<font color=red><b>BANNED</b></font> ");


	///////
	//
	// PRINT THE TITLE
	//
	///////

	// the a href tag
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ( "<a href=" );
		// truncate off -diffbotxyz%"INT32"
		int32_t newLen = urlLen;
		if ( diffbotSuffix ) newLen = diffbotSuffix - url;
		// print the url in the href tag
		sb->safeMemcpy ( url , newLen ); 
		// then finish the a href tag and start a bold for title
		sb->safePrintf ( ">");//<font size=+0>" );
	}


	// . then the title  (should be NULL terminated)
	// . the title can be NULL
	// . highlight it first
	// . the title itself should not have any tags in it!
	char  *str  = mr->ptr_tbuf;//msg40->getTitle(i);
	int32_t strLen = mr->size_tbuf - 1;// msg40->getTitleLen(i);
	if ( ! str || strLen < 0 ) strLen = 0;

	/////
	//
	// are we printing a dmoz category page?
	// get the appropriate dmoz title/summary to use since the same
	// url can exist in multiple topics (catIds) with different
	// titles summaries.
	//
	/////

	char *dmozSummary2 = NULL;
	// TODO: just get the catid from httprequest directly?
	if ( si->m_catId > 0 ) { // si->m_cat_dirId > 0) {
		// . get the dmoz title and summary
		// . if empty then just a bunch of \0s, except for catIds
	        Msg20Reply *mr = m20->getReply();
		char *dmozTitle  = mr->ptr_dmozTitles;
		dmozSummary2 = mr->ptr_dmozSumms;
		char *dmozAnchor = mr->ptr_dmozAnchors;
		int32_t *catIds     = mr->ptr_catIds;
		int32_t numCats = mr->size_catIds / 4;
		// loop through looking for the right ID
		for (int32_t i = 0; i < numCats ; i++ ) {
			// assign shit if we match the dmoz cat we are showing
			if ( catIds[i] ==  si->m_catId) break;
			dmozTitle +=gbstrlen(dmozTitle)+1;
			dmozSummary2 +=gbstrlen(dmozSummary2)+1;
			dmozAnchor += gbstrlen(dmozAnchor)+1;
		}
		// now make the title the dmoz title
		str = dmozTitle;
		strLen = gbstrlen(str);
	}
	

	int32_t hlen;
	//copy all summary and title excerpts for this result into here
	//char tt[1024*32];
	//char *ttend = tt + 1024*32;
	char *frontTag = 
		"<font style=\"color:black;background-color:yellow\">" ;
	char *backTag = "</font>";
	if ( si->m_format == FORMAT_XML ) {
		frontTag = "<b>";
		backTag  = "</b>";
	}
	if ( si->m_format == FORMAT_WIDGET_IFRAME || 
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX ) {
		frontTag = "<font style=\"background-color:yellow\">" ;
	}
	int32_t cols = 80;
	cols = si->m_summaryMaxWidth;

	char tmp3[1024];
	SafeBuf hb(tmp3, 1024);
	if ( str && strLen && si->m_doQueryHighlighting ) {
		hlen = hi.set ( &hb,
				//tt , 
				//ttend - tt , 
				str, 
				strLen , 
				mr->m_language, // docLangId
				&si->m_hqq , // highlight query CLASS
				false  , // doStemming?
				false  , // use click&scroll?
				NULL   , // base url
				frontTag,
				backTag,
				0,
				0 ); // niceness
		// reassign!
		str = hb.getBufStart();
		strLen = hb.getLength();
		//if (!sb->utf8Encode2(tt, hlen)) return false;
		// if ( si->m_format != FORMAT_JSON )
		// 	if ( ! sb->brify ( hb.getBufStart(),
		// 			   hb.getLength(),
		// 			   0,
		// 			   cols) ) return false;
	}

	// . use "UNTITLED" if no title
	// . msg20 should supply the dmoz title if it can
	if ( strLen == 0 && 
	     si->m_format != FORMAT_XML && 
	     si->m_format != FORMAT_JSON ) {
		str = "<i>UNTITLED</i>";
		strLen = gbstrlen(str);
	}

	if ( str && 
	     strLen && 
	     ( si->m_format == FORMAT_HTML ||
	       si->m_format == FORMAT_WIDGET_IFRAME ||
	       si->m_format == FORMAT_WIDGET_APPEND ||
	       si->m_format == FORMAT_WIDGET_AJAX ) 
	     ) {
		// determine if TiTle wraps, if it does add a <br> count for
		// each wrap
		//if (!sb->utf8Encode2(str , strLen )) return false;
		if ( ! sb->brify ( str,strLen,0,cols) ) return false;
	}

	// close up the title tag
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<title><![CDATA[");
		if ( str ) sb->cdataEncode(str);
		sb->safePrintf("]]></title>\n");
	}

	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"title\":\"");
		if ( str ) sb->jsonEncode(str);
		sb->safePrintf("\",\n");
	}


	if ( si->m_format == FORMAT_HTML ) 
		sb->safePrintf ("</a><br>\n" ) ;


	// close the title tag stuf
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX ) 
		sb->safePrintf("</b></a>\n");

	//
	// print <h1> tag contents. hack for client.
	//
	char *hp = mr->ptr_htag;
	char *hpend = hp + mr->size_htag;
	for ( ; hp && hp < hpend ; ) {
		if ( si->m_format == FORMAT_XML ) {
			sb->safePrintf("\t\t<h1Tag><![CDATA[");
			sb->cdataEncode(hp);
			sb->safePrintf("]]></h1Tag>\n");
		}
		if ( si->m_format == FORMAT_JSON ) {
			sb->safePrintf("\t\t\"h1Tag\":\"");
			sb->jsonEncode(hp);
			sb->safePrintf("\",\n");
		}
		// it is a \0 separated list of headers generated from
		// XmlDoc::getHeaderTagBuf()
		hp += gbstrlen(hp) + 1;
	}

	// print all dmoz info for xml/json. 
	// seems like both direct and indirect dmoz entries here.
	if ( mr->size_catIds > 0 &&
	     ( si->m_format == FORMAT_JSON ||
	       si->m_format == FORMAT_XML ) ) {

		char *dmozTitle   = mr->ptr_dmozTitles;
		char *dmozSummary = mr->ptr_dmozSumms;
		char *dmozAnchor  = mr->ptr_dmozAnchors;
		int32_t *catIds      = mr->ptr_catIds;
		int32_t  numCats     = mr->size_catIds / 4;
		// loop through looking for the right ID
		for (int32_t i = 0; i < numCats ; i++ ) {
			printDmozEntry ( sb,
					 catIds[i],
					 true,
					 dmozTitle,
					 dmozSummary,
					 dmozAnchor ,
					 si );
			dmozTitle   += gbstrlen(dmozTitle  ) + 1;
			dmozSummary += gbstrlen(dmozSummary) + 1;
			dmozAnchor  += gbstrlen(dmozAnchor ) + 1;
		}
	}

	if ( mr->size_indCatIds > 0 &&
	     ( si->m_format == FORMAT_JSON ||
	       si->m_format == FORMAT_XML ) ) {
		// print INDIRECT dmoz entries as well
		int32_t nIndCatids = mr->size_indCatIds / 4;
		 for ( int32_t i = 0; i < nIndCatids; i++ ) {
		 	int32_t catId = ((int32_t *)(mr->ptr_indCatIds))[i];
			if ( si->m_format == FORMAT_XML )
				sb->safePrintf("\t\t<indirectDmozCatId>"
					       "%"INT32"</indirectDmozCatId>\n",
					       catId);
			if ( si->m_format == FORMAT_JSON )
				sb->safePrintf("\t\t\"indirectDmozCatId\":"
					       "%"INT32",\n",catId);
		 }
		// print INDIRECT dmoz entries as well
		// int32_t nIndCatids = mr->size_indCatIds / 4;
		// dmozTitle   = mr->ptr_indDmozTitles;
		// dmozSummary = mr->ptr_dmozSumms;
		// dmozAnchor  = mr->ptr_dmozAnchors;
		// for ( int32_t i = 0; i < nIndCatids; i++ ) {
		// 	int32_t catId = ((int32_t *)(mr->ptr_indCatIds))[i];
		// 	printDmozEntry ( sb ,
		// 			 catId ,
		// 			 false,
		// 			 dmozTitle,
		// 			 dmozSummary,
		// 			 dmozAnchor ,
		// 			 si );
		// 	dmozTitle   += gbstrlen(dmozTitle  ) + 1;
		// 	dmozSummary += gbstrlen(dmozSummary) + 1;
		// 	dmozAnchor  += gbstrlen(dmozAnchor ) + 1;
		// }
	}


	/////
	//
	// print content type after title
	//
	/////
	unsigned char ctype = mr->m_contentType;
	char *cs = g_contentTypeStrings[ctype];

	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t\t<contentType>"
			       "<![CDATA["
			       "%s"
			       "]]>"
			       "</contentType>\n",
			       cs);

	if ( si->m_format == FORMAT_JSON )
		sb->safePrintf("\t\t\"contentType\":\"%s\",\n",cs);

	if ( si->m_format == FORMAT_HTML &&
	     ctype != CT_HTML && 
	     ctype != CT_UNKNOWN ){
		sb->safePrintf(" <b><font style=color:white;"
			       "background-color:maroon;>");
		char *p = cs;
		for ( ; *p ; p++ ) {
			char c = to_upper_a(*p);
			sb->pushChar(c);
		}
		sb->safePrintf("</font></b> &nbsp;");
	}
	

	////////////
	//
	// print the summary
	//
	////////////

	// . then the summary
	// . "s" is a string of null terminated strings
	//char *send;
	// do the normal summary
	str    = mr->ptr_displaySum;
	// sometimes the summary is longer than requested because for
	// summary deduping purposes (see "pss" parm in Parms.cpp) we do not
	// get it as int16_t as request. so use mr->m_sumPrintSize here
	// not mr->size_sum
	strLen = mr->size_displaySum - 1;//-1;

	// this includes the terminating \0 or \0\0 so back up
	if ( strLen < 0 ) strLen  = 0;
	//send = str + strLen;

	// dmoz summary might override if we are showing a dmoz topic page
	if ( dmozSummary2 && (si->m_catId>0 || strLen<=0) ) {
		str = dmozSummary2;
		strLen = gbstrlen(dmozSummary2);
	}

	bool printSummary = true;
	// do not print summaries for widgets by default unless overridden
	// with &summary=1
	int32_t defSum = 0;
	// if no image then default the summary to on
	if ( ! mr->ptr_imgData ) defSum = 1;

	if ( (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX ) && 
	     hr->getLong("summaries",defSum) == 0 ) 
		printSummary = false;

	if ( printSummary &&
	     (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX ) ) {
		int32_t sumLen = strLen;
		if ( sumLen > 150 ) sumLen = 150;
		if ( sumLen ) {
			sb->safePrintf("<br>");
			sb->safeTruncateEllipsis ( str , sumLen );
		}
	}

	if ( printSummary && si->m_format == FORMAT_HTML )
		sb->brify ( str , strLen, 0 , cols ); // niceness = 0

	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<sum><![CDATA[");
		sb->cdataEncode(str);
		sb->safePrintf("]]></sum>\n");
	}

	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"sum\":\"");
		sb->jsonEncode(str);
		sb->safePrintf("\",\n");
	}


	// new line if not xml. even summary is empty we need it too like
	// when showing xml docs - MDW 9/28/2014
	if ( si->m_format == FORMAT_HTML ) // && strLen ) 
		sb->safePrintf("<br>\n");


	/////////
	// 
	// meta tag values for &dt=keywords ...
	//
	/////////
	if ( mr->ptr_dbuf && mr->size_dbuf>1 )
		printMetaContent ( msg40 , ix,st,sb);


	////////////
	//
	// . print DMOZ topics under the summary
	// . will print the "Search in Category" link too
	//
	////////////
	//Msg20Reply *mr = m20->getMsg20Reply();
	int32_t nCatIds = mr->getNumCatIds();
	for (int32_t i = 0; i < nCatIds; i++) {
		int32_t catid = ((int32_t *)(mr->ptr_catIds))[i];
		printDMOZCategoryUnderResult(sb,si,catid,st);
	}
	// skipCatsPrint:
	// print the indirect category Ids
	int32_t nIndCatids = mr->size_indCatIds / 4;
	//if ( !cr->m_displayIndirectDmozCategories )
	//	goto skipCatsPrint2;
	for ( int32_t i = 0; i < nIndCatids; i++ ) {
		int32_t catid = ((int32_t *)(mr->ptr_indCatIds))[i];
		// skip it if it's a regular category
		//bool skip = false;
		int32_t d; for ( d = 0; d < nCatIds; d++) {
			if (  catid == mr->ptr_catIds[i] ) break;
		}
		// skip if the indirect catid matched a directed catid
		if ( d < nCatIds ) continue;
		// otherwise print it
		printDMOZCategoryUnderResult(sb,si,catid,st);
	}


	///////////
	//
	// print facet field/values
	//
	// if there was a gbfacet*: term (gbfacetstr, gbfacetfloat, gbfacetint)
	// this should be non-NULL and have the facet field/value pairs
	// and every string ends in a \0
	//
	//////////
	char *fp    =      mr->ptr_facetBuf;
	char *fpEnd = fp + mr->size_facetBuf;
	for ( ; fp && fp < fpEnd ; ) {
		if ( si->m_format == FORMAT_HTML ) {
			// print first one
			sb->safePrintf("<i><font color=maroon>");
			sb->safeStrcpy(fp);
			sb->safePrintf("</font></i>");
			sb->safePrintf(" &nbsp; : &nbsp; ");
			sb->safePrintf("<b>");
			fp += gbstrlen(fp) + 1;
			sb->htmlEncode(fp);
			// begin a new pair
			sb->safePrintf("</b>");
			sb->safeStrcpy("<br>\n");
			fp += gbstrlen(fp) + 1;		
		}
		else if ( si->m_format == FORMAT_XML ) {
			// print first one
			sb->safePrintf("\t\t<facet>\n"
				       "\t\t\t<field><![CDATA[");
			sb->cdataEncode(fp);
			sb->safePrintf("]]></field>\n");
			fp += gbstrlen(fp) + 1;
			sb->safePrintf("\t\t\t<value><![CDATA[");
			sb->cdataEncode(fp);
			sb->safePrintf("]]></value>\n");
			sb->safePrintf("\t\t</facet>\n");
			fp += gbstrlen(fp) + 1;
		}
		else if ( si->m_format == FORMAT_JSON ) {
			// print first one
			sb->safePrintf("\t\t\"facet\":{\n");
			sb->safePrintf("\t\t\t\"field\":\"");
			sb->jsonEncode(fp);
			sb->safePrintf("\",\n");
			fp += gbstrlen(fp) + 1;
			sb->safePrintf("\t\t\t\"value\":\"");
			sb->jsonEncode(fp);
			sb->safePrintf("\"\n");
			fp += gbstrlen(fp) + 1;
			sb->safePrintf("\t\t},\n");
		}


	}
		      

	////////////
	//
	// print the URL
	//
	////////////

	StackBuf(tmpBuf);
	char* displayUrl = Url::getDisplayUrl(url, &tmpBuf);
	uint32_t displayUrlLen = tmpBuf.length();

	// hack off the http:// if any for displaying it on screen
	if ( displayUrlLen > 8 && strncmp ( displayUrl , "http://" , 7 )==0 ) {
		displayUrl += 7; displayUrlLen -= 7; }
	// . remove trailing /
	// . only remove from root urls in case user cuts and 
	//   pastes it for link: search
	if ( displayUrl [ displayUrlLen - 1 ] == '/' ) {
		// see if any other slash before us
		int32_t j;
		for ( j = displayUrlLen - 2 ; j >= 0 ; j-- )
			if ( displayUrl[j] == '/' ) break;
		// if there wasn't, we must have been a root url
		// so hack off the last slash
		if ( j < 0 ) displayUrlLen--;
	}
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ("<font color=gray>" );
		//sb->htmlEncode ( url , gbstrlen(url) , false );
		// 20 for the date after it
		sb->safeTruncateEllipsis ( displayUrl , 50 ); // cols - 30 );
		// turn off the color
		sb->safePrintf ( "</font>\n" );
	}

	// print url for widgets now
	if ( (si->m_format == FORMAT_WIDGET_IFRAME ||
	      si->m_format == FORMAT_WIDGET_APPEND ||
	      si->m_format == FORMAT_WIDGET_AJAX ) ) {
		//sb->safePrintf ("<br><font color=gray size=-1>" );
		// print url for widgets in top left if we have a wide image
		// otherwise it gets truncated below the title for some reason
		if ( isWide )
			sb->safePrintf ("<br><font color=white size=-1 "
					"style=position:absolute;left:10px;"
					"top:10px;background-color:black;>" );
		else if ( mr->ptr_imgData )
			sb->safePrintf ("<br><font color=gray size=-1 "
					"style=position:absolute;left:%"INT32"px;"
					"top:10px;>"
				       , (int32_t) PADDING + newdx + 10 );
		else
			sb->safePrintf ("<br><font color=gray size=-1>");
		// print the url now, truncated to 50 chars
		sb->safeTruncateEllipsis ( url , 50 ); // cols - 30 );
		sb->safePrintf ( "</font>\n" );
	}


	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<url><![CDATA[");
		sb->safeMemcpy ( displayUrl , displayUrlLen );
		sb->safePrintf("]]></url>\n");
	}
	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"url\":\"");
		sb->jsonEncode ( displayUrl , displayUrlLen );
		sb->safePrintf("\",\n");
	}

	if ( si->m_format == FORMAT_XML )
		sb->safePrintf("\t\t<hopCount>%"INT32"</hopCount>\n",
			       (int32_t)mr->m_hopcount);

	if ( si->m_format == FORMAT_JSON )
		sb->safePrintf("\t\t\"hopCount\":%"INT32",\n",(int32_t)mr->m_hopcount);

	// now the last spidered date of the document
	time_t ts = mr->m_lastSpidered;
	if ( si->m_format == FORMAT_HTML ) 
		printTimeAgo ( sb , ts , "indexed" , si );

	// the date it was last modified
	ts = mr->m_lastModified;
	if ( si->m_format == FORMAT_HTML ) 
		printTimeAgo ( sb , ts , "modified" , si );

	//
	// more xml stuff
	//
	if ( si->m_format == FORMAT_XML ) {
		// doc size in Kilobytes
		sb->safePrintf ( "\t\t<size><![CDATA[%4.0fk]]></size>\n",
				(float)mr->m_contentLen/1024.0);
		sb->safePrintf ( "\t\t<sizeInBytes>%"INT32"</sizeInBytes>\n",
				 mr->m_contentLen);
		// . docId for possible cached link
		// . might have merged a bunch together
		sb->safePrintf("\t\t<docId>%"INT64"</docId>\n",mr->m_docId );
		sb->safePrintf("\t\t<docScore>%f</docScore>\n",docScore);
	}

	if ( si->m_format == FORMAT_XML && mr->m_contentType != CT_STATUS ) {
		// . show the site root
		// . for hompages.com/users/fred/mypage.html this will be
		//   homepages.com/users/fred/
		// . for www.xyz.edu/~foo/burp/ this will be
		//   www.xyz.edu/~foo/ etc.
		int32_t  siteLen = 0;
		char *site = NULL;
		// seems like this isn't the way to do it, cuz Tagdb.cpp
		// adds the "site" tag itself and we do not always have it
		// in the XmlDoc::ptr_tagRec... so do it this way:
		site    = mr->ptr_site;
		siteLen = mr->size_site-1;
		//char *site=uu.getSite( &siteLen , si->m_coll, false, tagRec);
		sb->safePrintf("\t\t<site><![CDATA[");
		if ( site && siteLen > 0 ) sb->safeMemcpy ( site , siteLen );
		sb->safePrintf("]]></site>\n");
		//int32_t sh = hash32 ( site , siteLen );
		//sb->safePrintf ("\t\t<siteHash32>%"UINT32"</siteHash32>\n",sh);
		//int32_t dh = uu.getDomainHash32 ();
		//sb->safePrintf ("\t\t<domainHash32>%"UINT32"</domainHash32>\n",dh);
		// spider date
		sb->safePrintf ( "\t\t<spidered>%"UINT32"</spidered>\n",
				 (uint32_t)mr->m_lastSpidered);
		// backwards compatibility for buzz
		sb->safePrintf ( "\t\t<firstIndexedDateUTC>%"UINT32""
				"</firstIndexedDateUTC>\n",
				 (uint32_t)mr->m_firstIndexedDate);
		sb->safePrintf( "\t\t<contentHash32>%"UINT32""
				"</contentHash32>\n",
				(uint32_t)mr->m_contentHash32);
		// pub date
		int32_t datedbDate = mr->m_datedbDate;
		// show the datedb date as "<pubDate>" for now
		if ( datedbDate != -1 )
			sb->safePrintf ( "\t\t<pubdate>%"UINT32"</pubdate>\n",
					 (uint32_t)datedbDate);
	}

	if ( si->m_format == FORMAT_JSON ) {
		// doc size in Kilobytes
		sb->safePrintf ( "\t\t\"size\":\"%4.0fk\",\n",
				(float)mr->m_contentLen/1024.0);
		sb->safePrintf ( "\t\t\"sizeInBytes\":%"INT32",\n",
				 mr->m_contentLen);
		// . docId for possible cached link
		// . might have merged a bunch together
		sb->safePrintf("\t\t\"docId\":%"INT64",\n",mr->m_docId );
		sb->safePrintf("\t\t\"docScore\":%f,\n",docScore);
	}

	if ( si->m_format == FORMAT_JSON && mr->m_contentType != CT_STATUS ) {
		// . show the site root
		// . for hompages.com/users/fred/mypage.html this will be
		//   homepages.com/users/fred/
		// . for www.xyz.edu/~foo/burp/ this will be
		//   www.xyz.edu/~foo/ etc.
		int32_t  siteLen = 0;
		char *site = NULL;
		// seems like this isn't the way to do it, cuz Tagdb.cpp
		// adds the "site" tag itself and we do not always have it
		// in the XmlDoc::ptr_tagRec... so do it this way:
		site    = mr->ptr_site;
		siteLen = mr->size_site-1;
		//char *site=uu.getSite( &siteLen , si->m_coll, false, tagRec);
		sb->safePrintf("\t\t\"site\":\"");
		if ( site && siteLen > 0 ) sb->safeMemcpy ( site , siteLen );
		sb->safePrintf("\",\n");
		//int32_t sh = hash32 ( site , siteLen );
		//sb->safePrintf ("\t\t<siteHash32>%"UINT32"</siteHash32>\n",sh);
		//int32_t dh = uu.getDomainHash32 ();
		//sb->safePrintf ("\t\t<domainHash32>%"UINT32"</domainHash32>\n",dh);
		// spider date
		sb->safePrintf ( "\t\t\"spidered\":%"UINT32",\n",
				 (uint32_t)mr->m_lastSpidered);
		// backwards compatibility for buzz
		sb->safePrintf ( "\t\t\"firstIndexedDateUTC\":%"UINT32",\n"
				 , (uint32_t) mr->m_firstIndexedDate);
		sb->safePrintf( "\t\t\"contentHash32\":%"UINT32",\n"
				, (uint32_t)mr->m_contentHash32);
		// pub date
		int32_t datedbDate = mr->m_datedbDate;
		// show the datedb date as "<pubDate>" for now
		if ( datedbDate != -1 )
			sb->safePrintf ( "\t\t\"pubdate\":%"UINT32",\n",
					 (uint32_t)datedbDate);
	}



	// . we also store the outlinks in a linkInfo structure
	// . we can call LinkInfo::set ( Links *outlinks ) to set it
	//   in the msg20
	LinkInfo *outlinks = (LinkInfo *)mr->ptr_outlinks;
	// NULLify if empty
	if ( mr->size_outlinks <= 0 ) outlinks = NULL;
	// only for xml for now
	if ( si->m_format == FORMAT_HTML ) outlinks = NULL;
	Inlink *k;
	// do we need absScore2 for outlinks?
	//k = NULL;
	while ( outlinks &&
		(k =outlinks->getNextInlink(k))) 
		// print it out
		sb->safePrintf("\t\t<outlink "
			      "docId=\"%"INT64"\" "
			      "hostId=\"%"UINT32"\" "
			      "indexed=\"%"INT32"\" "
			      "pubdate=\"%"INT32"\" ",
			      k->m_docId ,
			       (uint32_t)k->m_ip,//hostHash, but use ip for now
			      (int32_t)k->m_firstIndexedDate ,
			      (int32_t)k->m_datedbDate );

	if ( si->m_format == FORMAT_XML ) {
		// result
		sb->safePrintf("\t\t<language><![CDATA[%s]]>"
			      "</language>\n", 
			      getLanguageString(mr->m_language));
		sb->safePrintf("\t\t<langAbbr>%s</langAbbr>\n", 
			      getLangAbbr(mr->m_language));
		char *charset = get_charset_str(mr->m_charset);
		if(charset)
			sb->safePrintf("\t\t<charset><![CDATA[%s]]>"
				      "</charset>\n", charset);
	}

	if ( si->m_format == FORMAT_JSON ) {
		// result
		sb->safePrintf("\t\t\"language\":\"%s\",\n",
			      getLanguageString(mr->m_language));
		sb->safePrintf("\t\t\"langAbbr\":\"%s\",\n",
			      getLangAbbr(mr->m_language));
		char *charset = get_charset_str(mr->m_charset);
		if(charset)
			sb->safePrintf("\t\t\"charset\":\"%s\",\n",charset);
	}

	//
	// end more xml stuff
	//


	
	if ( si->m_format == FORMAT_HTML ) {
		int32_t lang = mr->m_language;
		if ( lang ) sb->safePrintf(" - %s",getLanguageString(lang));
		uint16_t cc = mr->m_computedCountry;
		if( cc ) sb->safePrintf(" - %s", g_countryCode.getName(cc));
		char *charset = get_charset_str(mr->m_charset);
		if ( charset ) sb->safePrintf(" - %s ", charset);
	}

	if ( si->m_format == FORMAT_HTML ) sb->safePrintf("<br>\n");

	//char *coll = si->m_cr->m_coll;

	// print the [cached] link?
	bool printCached = true;
	if ( mr->m_noArchive       ) printCached = false;
	if ( isAdmin               ) printCached = true;
	if ( mr->m_contentLen <= 0 ) printCached = false;
	if ( si->m_format != FORMAT_HTML ) printCached = false;

	// get collnum result is from
	//collnum_t collnum = si->m_cr->m_collnum;
	// if searching multiple collections  - federated search
	CollectionRec *scr = g_collectiondb.getRec ( mr->m_collnum );
	char *coll = "UNKNOWN";
	if ( scr ) coll = scr->m_coll;

	if ( printCached && cr->m_clickNScrollEnabled ) 
		sb->safePrintf ( " - <a href=/scroll.html?page="
				"get?"
				"q=%s&c=%s&d=%"INT64">"
				"cached</a>\n",
				 st->m_qesb.getBufStart() , coll ,
				mr->m_docId );
	else if ( printCached )
		sb->safePrintf ( "<a href=\""
				"/get?"
				"q=%s&"
				"qlang=%s&"
				"c=%s&d=%"INT64"&cnsp=0\">"
				"cached</a>\n", 
				 st->m_qesb.getBufStart() , 
				// "qlang" parm
				si->m_defaultSortLang,
				coll , 
				mr->m_docId ); 

	// the new links
	if ( si->m_format == FORMAT_HTML && g_conf.m_isMattWells && 1 == 0 ) {
		//sb->safePrintf(" - <a href=\"/scoring?"
		//	      "c=%s&\">scoring</a>",
		//	      coll );
		//sb->safePrintf(" - <a href=\"/print?c=%s&",coll);
		if ( g_conf.m_isMattWells )
			sb->safePrintf(" - <a href=\"/seo?");//c=%s&",coll);
		else
			sb->safePrintf(" - <a href=\"https://www.gigablast."
				      "com/seo?");//c=%s&",coll);
		//sb->safePrintf("d=%"INT64"",mr->m_docId);
		sb->safePrintf("u=");
		sb->urlEncode ( url , gbstrlen(url) , false );
		//sb->safePrintf("&page=1\">seo</a>" );
		sb->safePrintf("\"><font color=red>seo</font></a>" );
	}

	// only display re-spider link if addurl is enabled
	//if ( isAdmin &&
	//     g_conf.m_addUrlEnabled &&
	//     cr->m_addUrlEnabled      ) {
	/*
	if ( si->m_format == FORMAT_HTML ) {
		// the [respider] link
		// save this for seo iframe!
		sb->safePrintf (" - <a href=\"/admin/inject?u=" );
		// encode the url now
		sb->urlEncode ( url , urlLen );
		// then collection
		if ( coll ) {
			sb->safeMemcpy ( "&c=" , 3 );
			sb->safeMemcpy ( coll , gbstrlen(coll) );
		}
		//sb->safePrintf ( "&force=1\">reindex</a>" );
		sb->safePrintf ( "\">reindex</a>" );
	}
	*/

	// unhide the divs on click
	int32_t placeHolder = -1;
	int32_t placeHolderLen = 0;
	if ( si->m_format == FORMAT_HTML && si->m_getDocIdScoringInfo ) {
		// place holder for backlink table link
		placeHolder = sb->length();
		sb->safePrintf (" - <a onclick="

			       "\""
			       "var e = document.getElementById('bl%"INT32"');"
			       "if ( e.style.display == 'none' ){"
			       "e.style.display = '';"
			       "}"
			       "else {"
			       "e.style.display = 'none';"
			       "}"
			       "\""
			       " "

			       "style="
			       "cursor:hand;"
			       "cursor:pointer;"
			       "color:blue;>"
			       "<u>00000 backlinks</u>"
			       "</a>\n"
			       , ix 
			       );
		placeHolderLen = sb->length() - placeHolder;
	}


	if ( si->m_format == FORMAT_HTML && si->m_getDocIdScoringInfo ) {
		// unhide the scoring table on click
		sb->safePrintf (" - <a onclick="

			       "\""
			       "var e = document.getElementById('sc%"INT32"');"
			       "if ( e.style.display == 'none' ){"
			       "e.style.display = '';"
			       "}"
			       "else {"
			       "e.style.display = 'none';"
			       "}"
			       "\""
			       " "

			       "style="
			       "cursor:hand;"
			       "cursor:pointer;"
			       "color:blue;>"
			       "scoring"
			       "</a>\n"
			       ,ix
			       );
	}

	if ( si->m_format == FORMAT_HTML ) {
		// reindex
		sb->safePrintf(" - <a style=color:blue; href=\"/addurl?"
			       "urls=");
		sb->urlEncode ( url , gbstrlen(url) , false );
		uint64_t rand64 = gettimeofdayInMillisecondsLocal();
		sb->safePrintf("&c=%s&rand64=%"UINT64"\">respider</a>\n",
			       coll,rand64);
	}

	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf (" - "
				"<a style=color:blue; "
				"href=\"/search?sb=1&c=%s&"
				//"q=url2%%3A" 
				"q=gbfieldmatch%%3AgbssUrl%%3A"
				, coll 
				);
		// do not include ending \0
		sb->urlEncode ( mr->ptr_ubuf , mr->size_ubuf-1 , false );
		sb->safePrintf ( "\">"
				 "spider info</a>\n"
			       );
	}

	//
	// show rainbow sections link
	//
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ( " - <a style=color:blue; href=\""
				 "/get?"
				 // show rainbow sections
				 "page=4&"
				 "q=%s&"
				 "qlang=%s&"
				 "c=%s&"
				 "d=%"INT64"&"
				 "cnsp=0\">"
				 "sections</a>\n", 
				 st->m_qesb.getBufStart() , 
				 // "qlang" parm
				 si->m_defaultSortLang,
				 coll , 
				 mr->m_docId ); 
	}

	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf ( " - <a style=color:blue; href=\""
				 "/get?"
				 // show rainbow sections
				 "page=1&"
				 //"q=%s&"
				 //"qlang=%s&"
				 "c=%s&"
				 "d=%"INT64"&"
				 "cnsp=0\">"
				 "page info</a>\n", 
				 //st->m_qe , 
				 // "qlang" parm
				 //si->m_defaultSortLang,
				 coll , 
				 mr->m_docId ); 
	}

	// this stuff is secret just for local guys! not any more
	if ( si->m_format == FORMAT_HTML ) {
		// now the ip of url
		//int32_t urlip = msg40->getIp(i);
		// don't combine this with the sprintf above cuz
		// iptoa uses a static local buffer like ctime()
		sb->safePrintf(//"<br>"
			      " - <a style=color:blue; href=\"/search?"
			      "c=%s&sc=1&dr=0&q=ip:%s&"
			      "n=100&usecache=0\">%s</a>\n",
			      coll,iptoa(mr->m_ip), iptoa(mr->m_ip) );
		// ip domain link
		unsigned char *us = (unsigned char *)&mr->m_ip;//urlip;
		sb->safePrintf (" - <a style=color:blue; "
				"href=\"/search?c=%s&sc=1&dr=0&n=100&"
				"q=ip:%"INT32".%"INT32".%"INT32"&"
				"usecache=0\">%"INT32".%"INT32".%"INT32"</a>\n",
				coll,
				(int32_t)us[0],(int32_t)us[1],(int32_t)us[2],
				(int32_t)us[0],(int32_t)us[1],(int32_t)us[2]);

		/*
		// . now the info link
		// . if it's local, don't put the hostname/port in
		//   there cuz it will mess up Global Spec's machine
		//if ( h->m_groupId == g_hostdb.m_groupId ) 
		sb.safePrintf(" - <a href=\"/admin/titledb?c=%s&"
			      "d=%"INT64"",coll,mr->m_docId);
		// then the [info] link to show the TitleRec
		sb->safePrintf ( "\">[info]</a>" );
		
		// now the analyze link
		sb.safePrintf (" - <a href=\"/admin/parser?c=%s&"
			       "old=1&hc=%"INT32"&u=", 
			       coll,
			       (int32_t)mr->m_hopcount);
		// encode the url now
		sb->urlEncode ( url , urlLen );
		// then the [analyze] link
		sb->safePrintf ("\">[analyze]</a>" );
		
		// and links: query link
		sb->safePrintf( " - <a href=\"/search?c=%s&dr=0&"
			       "n=100&q=links:",coll);
		// encode the url now
		sb->urlEncode ( url , urlLen );
		sb->safeMemcpy ("\">linkers</a>" , 14 ); 
		*/
	}

	char dbuf [ MAX_URL_LEN ];
	int32_t dlen = uu.getDomainLen();
	if ( si->m_format == FORMAT_HTML ) {
		gbmemcpy ( dbuf , uu.getDomain() , dlen );
		dbuf [ dlen ] = '\0';
		// newspaperarchive urls have no domain
		if ( dlen == 0 ) {
			dlen = uu.getHostLen();
			gbmemcpy ( dbuf , uu.getHost() , dlen );
			dbuf [ dlen ] = '\0';
		}
	}


	// admin always gets the site: option so he can ban
	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf (" - "
			       " <a style=color:blue; href=\"/search?"
			       "q=site%%3A%s&sc=0&c=%s\">"
			       "domain</a>\n" ,
				dbuf ,
				coll );//, dbuf );
	}


	if ( si->m_format == FORMAT_HTML && si->m_doSiteClustering ) {
		char hbuf [ MAX_URL_LEN ];
		int32_t hlen = uu.getHostLen();
		gbmemcpy ( hbuf , uu.getHost() , hlen );
		hbuf [ hlen ] = '\0';

		// make the cgi parm to add to the original url
		char tmp[512];
		SafeBuf qq (tmp,512);
		qq.safePrintf("q=");
		qq.urlEncode("site:");
		qq.urlEncode (hbuf);
		qq.urlEncode(" | ");
		qq.safeStrcpy(st->m_qesb.getBufStart());
		qq.nullTerm();
		// get the original url and add/replace in query
		char tmp2[512];
		SafeBuf newUrl(tmp2, 512);
		replaceParm ( qq.getBufStart() , &newUrl , hr );
		// put show more results from this site link
		sb->safePrintf (" - <nobr><a href=\"%s\">"
			       "more from this site</a></nobr>"
				, newUrl.getBufStart()
				);
		if ( indent ) sb->safePrintf ( "</blockquote><br>\n");
		//else sb->safePrintf ( "<br><br>\n");
	}


	if (si->m_format == FORMAT_HTML && ( isAdmin || cr->m_isCustomCrawl)){
		char *un = "";
		int32_t  banVal = 1;
		if ( mr->m_isBanned ) {
			un = "UN";
			banVal = 0;
		}
		// don't put on a separate line because then it is too
		// easy to mis-click on it
		sb->safePrintf(//"<br>"
			       " - "
			      " <a style=color:green; href=\"/admin/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=%"INT32"&"
			      "u=%s&c=%s\">"
			      "<nobr>%sBAN %s"
			      "</nobr></a>\n"
			      , banVal
			      , dbuf
			      , coll
			      , un
			      , dbuf );
		//banSites->safePrintf("%s+", dbuf);
		dlen = uu.getHostLen();
		gbmemcpy ( dbuf , uu.getHost() , dlen );
		dbuf [ dlen ] = '\0';
		sb->safePrintf(" - "
			      " <a style=color:green; href=\"/admin/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=%"INT32"&"
			      "u=%s&c=%s\">"
			      "<nobr>%sBAN %s</nobr></a>\n"
			      , banVal
			      , dbuf
			      , coll
			      , un
			      , dbuf
			      );
		// take similarity out until working again
		/*
		sb->safePrintf (" - [similar -"
			       " <a href=\"/search?"
			       "q="
			       "gbtagvector%%3A%"UINT32""
			       "&sc=1&dr=0&c=%s&n=100"
			       "&rcache=0\">"
			       "tag</a> " ,
			       (int32_t)mr->m_tagVectorHash,  coll);
		sb->safePrintf ("<a href=\"/search?"
			       "q="
			       "gbgigabitvector%%3A%"UINT32""
			       "&sc=1&dr=0&c=%s&n=100"
			       "&rcache=0\">"
			       "topic</a> " ,
			       (int32_t)mr->m_gigabitVectorHash, coll);
		*/
		if ( mr->size_gbAdIds > 0 ) 
			sb->safePrintf ("<a href=\"/search?"
				       "q=%s"
				       "&sc=1&dr=0&c=%s&n=200&rat=0\">"
				       "Ad Id</a>\n" ,
				       mr->ptr_gbAdIds,  coll);
		
		//sb->safePrintf ("] ");
		
		/*
		  put this on 'page info'
		int32_t urlFilterNum = (int32_t)mr->m_urlFilterNum;
		if(urlFilterNum != -1) {
			sb->safePrintf (" - <a style=color:green; "
					"href=/admin/filters?c=%s>"
				       "UrlFilter:%"INT32"</a>", 
				       coll ,
				       urlFilterNum);
		}					
		*/
		
	}

	// UN-indent it if level is 1
	/*
	if ( si->m_format == FORMAT_HTML && si->m_doIpClustering ) {
		sb->safePrintf (" - [ <a href=\"/search?"
			       "q=%%2Bip%%3A%s+%s&sc=0&c=%s\">"
			       "More from this ip</a> ]",
			       iptoa ( mr->m_ip ) ,
			       st->m_qe , coll );
		if ( indent ) sb->safePrintf ( "</blockquote><br>\n");
		else sb->safePrintf ( "<br><br>\n");
	}
	*/

	/*
	// print the help
	SafeBuf help;
	help.safePrintf("The distance matrix uses the "
			"following formula to calculate "
			"a score in a table cell for a pair of query terms: "
			"<br>"
			"<span style=\""
			"border:1px black solid;"
			"background-color:yellow;"
			"\">"
			"SCORE = (%"INT32" - |pos1-pos2|) * "
			"locationWeight * "
			"densityWeight * "
			"synWeight1 * "
			"synWeight2 * "
			"spamWeight1 * "
			"spamWeight2 * "
			"tfWeight1 * "
			"tfWeight2"
			"</span>"
			"<br>"
			"<br>"
			, (int32_t)MAXWORDPOS+1
			);
	help.safePrintf("<table>"
			"<tr><td>pos1</td><td>The word position of "
			"query term 1</td></tr>"
			"<tr><td>pos2</td><td>The word position of "
			"query term 2</td></tr>"
			"</table>"
			);

	help.safePrintf(
			//"where<br>"
			//"locationWeight is based on where "
			//"the two terms occur in the document "
			//"and uses the following table: <br>"
			"<table>"
			"<tr><td>term location</td>"
			"<td>locationWeight</td></tr>"
			);
	for ( int32_t i = 0 ; i < HASHGROUP_END ; i++ ) {
		char *hs = getHashGroupString(i);
		float hw = s_hashGroupWeights[i];
		help.safePrintf("<tr><td>%s</td><td>%.0f</td></tr>"
				,hs,hw );
	}
	help.safePrintf("</table>");
	help.safePrintf("<br><br>");
	help.safePrintf(
			"<table>"
			"<tr><td>max # alphanumeric words in location</td>"
			"<td>densityRank</td>"
			"<td>densityWeight</td>"
			"</tr>"
			);
	for ( int32_t i = 0 ; i < MAXDENSITYRANK ; i++ ) {
		help.safePrintf("<tr>"
				"<td>%"INT32"</td>"
				"<td>%"INT32"</td>"
				"<td>%.0f</td>"
				"</tr>"
				,maxw,i,dweight );
	}
	help.safePrintf("</table>");
	help.safePrintf("<br><br>"
	*/
		
	if ( mr->size_metadataBuf && si->m_format == FORMAT_JSON) {
		sb->safePrintf("\t\t\"metadata\":[");
		//sb->safeMemcpy(mr->ptr_metadataBuf, mr->size_metadataBuf);
		sb->safeStrcpy(mr->ptr_metadataBuf);
		// without this \n we seem to lose our ] i guess it gets
		// backed up over
		sb->safePrintf("],\n");
	}


	if ( mr->size_metadataBuf && si->m_format == FORMAT_HTML) {
		sb->safePrintf("<br>");

		Json md;
		JsonItem *ji = md.parseJsonStringIntoJsonItems(mr->ptr_metadataBuf,
													   0);
		char tmpBuf1[1024];
		char tmpBuf2[1024];
		SafeBuf nameBuf(tmpBuf1, 1024);
		for ( ; ji ; ji = ji->m_next ) {
			if(ji->isInArray()) continue;
			if(ji->m_type == JT_ARRAY) continue;
			ji->getCompoundName ( nameBuf ) ;
			if(nameBuf.length() == 0) {
				continue;
			}
			//nameBuf.replaceChar('-', '_');
			nameBuf.nullTerm();

			int32_t valLen;
			char* valBuf = ji->getValueAsString(&valLen);
			SafeBuf queryBuf(tmpBuf2, 1024);
			// log("compound name is %s %d %d",nameBuf.getBufStart(),
			// nameBuf.length(), valLen);

			queryBuf.safePrintf("/search?q=%s:%%22",nameBuf.getBufStart());
			queryBuf.urlEncode(valBuf, valLen);
			queryBuf.safePrintf("%%22&c=%s",coll);
			queryBuf.nullTerm();
			sb->safePrintf(" - <a href=\"%s\">%s:\"", queryBuf.getBufStart(),
						   nameBuf.getBufStart());
			sb->safeMemcpy(valBuf, valLen);
			sb->safeStrcpy("\"</a>");
		}
	}


	// end serp div
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX )
		sb->safePrintf("</div><hr>");
	

	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf ( "<br><br>\n");

	// search result spacer
	if ( si->m_format == FORMAT_WIDGET_IFRAME ||
	     si->m_format == FORMAT_WIDGET_APPEND ||
	     si->m_format == FORMAT_WIDGET_AJAX   )
		sb->safePrintf("<div style=line-height:%"INT32"px;><br></div>",
			       (int32_t)SERP_SPACER);


	// inc it
	*numPrintedSoFar = *numPrintedSoFar + 1;

	// done?
	DocIdScore *dp = msg40->getScoreInfo(ix);
	if ( ! dp ) {
		if ( si->m_format == FORMAT_XML ) 
			sb->safePrintf ("\t</result>\n\n");
		if ( si->m_format == FORMAT_JSON ) {
			// remove last ,\n
			sb->m_length -= 2;
			sb->safePrintf ("\n\t}\n\n");
		}
		// wtf?
		//char *xx=NULL;*xx=0;
		// at least close up the table
		if ( si->m_format != FORMAT_HTML ) return true;

		sb->safePrintf("</table>\n");

		return true;
	}


	//
	// scoring info tables
	//

	int32_t nr = dp->m_numRequiredTerms;
	if ( nr == 1 ) nr = 0;
	// print breakout tables here for distance matrix
	//SafeBuf bt;
	// final score calc
	char tmp[1024];
	SafeBuf ft(tmp, 1024);;
	// int16_tcut
	//Query *q = si->m_q;

	// put in a hidden div so you can unhide it
	if ( si->m_format == FORMAT_HTML )
		sb->safePrintf("<div id=bl%"INT32" style=display:none;>\n", ix );

	// print xml and html inlinks
	int32_t numInlinks = 0;
	printInlinkText ( sb , mr , si , &numInlinks );


	if ( si->m_format == FORMAT_HTML ) {
		sb->safePrintf("</div>");
		sb->safePrintf("<div id=sc%"INT32" style=display:none;>\n", ix );
	}


	// if pair changes then display the sum
	int32_t lastTermNum1 = -1;
	int32_t lastTermNum2 = -1;

	float minScore = -1;

	// display all the PairScores
	for ( int32_t i = 0 ; i < dp->m_numPairs ; i++ ) {
		float totalPairScore = 0.0;
		// print all the top winners for this pair
		PairScore *fps = &dp->m_pairScores[i];
		// if same combo as last time skip
		if ( fps->m_qtermNum1 == lastTermNum1 &&
		     fps->m_qtermNum2 == lastTermNum2 )
			continue;
		lastTermNum1 = fps->m_qtermNum1;
		lastTermNum2 = fps->m_qtermNum2;
		bool firstTime = true;
		bool first = true;
		// print all pairs for this combo
		for ( int32_t j = i ; j < dp->m_numPairs ; j++ ) {
			// get it
			PairScore *ps = &dp->m_pairScores[j];
			// stop if different pair now
			if ( ps->m_qtermNum1 != fps->m_qtermNum1 ) break;
			if ( ps->m_qtermNum2 != fps->m_qtermNum2 ) break;
			// skip if 0. neighborhood terms have weight of 0 now
			if ( ps->m_finalScore == 0.0 ) continue;
			// first time?
			if ( firstTime && si->m_format == FORMAT_HTML ) {
				Query *q = &si->m_q;
				printTermPairs ( sb , q , ps );
				printScoresHeader ( sb );
				firstTime = false;
			}
			// print it
			printPairScore ( sb , si , ps , mr , msg40 , first );
			// not first any more!
			first = false;
			// add it up
			totalPairScore += ps->m_finalScore;
		}
		if ( ft.length() ) ft.safePrintf(" , ");
		ft.safePrintf("%f",totalPairScore);
		// min?
		if ( minScore < 0.0 || totalPairScore < minScore )
			minScore = totalPairScore;
		// we need to set "ft" for xml stuff below
		if ( si->m_format != FORMAT_HTML ) continue;
		//sb->safePrintf("<table border=1><tr><td><center><b>");
		// print pair text
		//int32_t qtn1 = fps->m_qtermNum1;
		//int32_t qtn2 = fps->m_qtermNum2;
		//if ( q->m_qterms[qtn1].m_isPhrase )
		//	sb->pushChar('\"');
		//sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
		//		q->m_qterms[qtn1].m_termLen );
		//if ( q->m_qterms[qtn1].m_isPhrase )
		//	sb->pushChar('\"');
		//sb->safePrintf("</b> vs <b>");
		//if ( q->m_qterms[qtn2].m_isPhrase )
		//	sb->pushChar('\"');
		//sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
		//		q->m_qterms[qtn2].m_termLen );
		//if ( q->m_qterms[qtn2].m_isPhrase )
		//	sb->pushChar('\"');

		sb->safePrintf("<tr><td><b>%.04f</b></td>"
			      "<td colspan=20>total of above scores</td>"
			      "</tr>",
			      totalPairScore);
		// close table from printScoresHeader
		if ( ! firstTime ) sb->safePrintf("</table><br>");
	}



	// close the distance table
	//if ( nr ) sb->safePrintf("</table>");
	// print the breakout tables
	//if ( nr ) {
	//	//sb->safePrintf("<br>");
	//	sb->safeMemcpy ( &bt );
	//}

	// the singles --- TODO: make it ALL query terms
	//nr = dp->m_numRequiredTerms;
	//for ( int32_t i = 0 ; i < nr && nr == 1 ; i++ ) {

	int32_t lastTermNum = -1;

	int32_t numSingles = dp->m_numSingles;
	// do not print this if we got pairs
	if ( dp->m_numPairs ) numSingles = 0;

	for ( int32_t i = 0 ; i < numSingles ; i++ ) {
		float totalSingleScore = 0.0;
		// print all the top winners for this single
		SingleScore *fss = &dp->m_singleScores[i];
		// if same combo as last time skip
		if ( fss->m_qtermNum == lastTermNum ) continue;
		// do not reprint for this query term num
		lastTermNum = fss->m_qtermNum;
		bool firstTime = true;
		// print all singles for this combo
		for ( int32_t j = i ; j < dp->m_numSingles ; j++ ) {
			// get it
			SingleScore *ss = &dp->m_singleScores[j];
			// stop if different single now
			if ( ss->m_qtermNum != fss->m_qtermNum ) break;
			// skip if 0. skip neighborhoods i guess
			if ( ss->m_finalScore == 0.0 ) continue;
			// first time?
			if ( firstTime && si->m_format == FORMAT_HTML ) {
				Query *q = &si->m_q;
				printSingleTerm ( sb , q , ss );
				printScoresHeader ( sb );
				firstTime = false;
			}
			// print it
			printSingleScore ( sb , si , ss , mr , msg40 );
			// add up
			totalSingleScore += ss->m_finalScore;
		}
		if ( ft.length() ) ft.safePrintf(" , ");
		ft.safePrintf("%f",totalSingleScore);
		// min?
		if ( minScore < 0.0 || totalSingleScore < minScore )
			minScore = totalSingleScore;
		// we need to set "ft" for xml stuff below
		if ( si->m_format != FORMAT_HTML ) continue;
		//sb->safePrintf("<table border=1><tr><td><center><b>");
		// print pair text
		//int32_t qtn = fss->m_qtermNum;
		//sb->safeMemcpy(q->m_qterms[qtn].m_term ,
		//	      q->m_qterms[qtn].m_termLen );
		//sb->safePrintf("</b></center></td></tr>");
		sb->safePrintf("<tr><td><b>%.04f</b></td>"
			      "<td colspan=20>total of above scores</td>"
			      "</tr>",
			      totalSingleScore);
		// close table from printScoresHeader
		if ( ! firstTime ) sb->safePrintf("</table><br>");
	}

	

	char *ff = "";
	if ( si->m_useMinAlgo ) ff = "MIN ";
	char *ff2 = "sum";
	if ( si->m_useMinAlgo ) ff2 = "min";
	//if ( nr ) sb->safePrintf("</table>");
	//sb->safePrintf("<br>");
	// final score!!!
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf ("\t\t<siteRank>%"INT32"</siteRank>\n",
			       (int32_t)dp->m_siteRank );

		sb->safePrintf ("\t\t<numGoodSiteInlinks>%"INT32""
			       "</numGoodSiteInlinks>\n",
			       (int32_t)mr->m_siteNumInlinks );

		// sb->safePrintf ("\t\t<numTotalSiteInlinks>%"INT32""
		// 	       "</numTotalSiteInlinks>\n",
		// 	       (int32_t)mr->m_siteNumInlinksTotal );
		// sb->safePrintf ("\t\t<numUniqueIpsLinkingToSite>%"INT32""
		// 	       "</numUniqueIpsLinkingToSite>\n",
		// 	       (int32_t)mr->m_siteNumUniqueIps );
		// sb->safePrintf("\t\t<numUniqueCBlocksLinkingToSite>%"INT32""
		// 	       "</numUniqueCBlocksLinkingToSite>\n",
		// 	       (int32_t)mr->m_siteNumUniqueCBlocks );


		struct tm *timeStruct3;
		timeStruct3 = gmtime((time_t *)&mr->m_pageInlinksLastUpdated);
		char tmp3[64];
		strftime ( tmp3 , 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct3 );
		// -1 means unknown
		if ( mr->m_pageNumInlinks >= 0 )
			// how many inlinks, external and internal, we have
			// to this page not filtered in any way!!!
			sb->safePrintf("\t\t<numTotalPageInlinks>%"INT32""
				      "</numTotalPageInlinks>\n"
				      ,mr->m_pageNumInlinks
				      );
		// how many inlinking ips we got, including our own if
		// we link to ourself
		sb->safePrintf("\t\t<numUniqueIpsLinkingToPage>%"INT32""
			      "</numUniqueIpsLinkingToPage>\n"
			      ,mr->m_pageNumUniqueIps
			      );
		// how many inlinking cblocks we got, including our own if
		// we link to ourself
		sb->safePrintf("\t\t<numUniqueCBlocksLinkingToPage>%"INT32""
			      "</numUniqueCBlocksLinkingToPage>\n"
			      ,mr->m_pageNumUniqueCBlocks
			      );
		
		// how many "good" inlinks. i.e. inlinks whose linktext we
		// count and index.
		sb->safePrintf("\t\t<numGoodPageInlinks>%"INT32""
			      "</numGoodPageInlinks>\n"
			      "\t\t<pageInlinksLastComputedUTC>%"UINT32""
			      "</pageInlinksLastComputedUTC>\n"
			      ,mr->m_pageNumGoodInlinks
			       ,(uint32_t)mr->m_pageInlinksLastUpdated
			      );


		float score    = msg40->getScore   (ix);
		sb->safePrintf("\t\t<finalScore>%f</finalScore>\n", score );
		sb->safePrintf ("\t\t<finalScoreEquationCanonical>"
			       "<![CDATA["
			       "Final Score = (siteRank/%.01f+1) * "
			       "(%.01f [if not foreign language]) * "
			       "(%s of above matrix scores)"
			       "]]>"
			       "</finalScoreEquationCanonical>\n"
			       , SITERANKDIVISOR
				, si->m_sameLangWeight //SAMELANGMULT
			       , ff2 
			       );
		sb->safePrintf ("\t\t<finalScoreEquation>"
			       "<![CDATA["
			       "<b>%.03f</b> = (%"INT32"/%.01f+1) " // * %s("
			       , dp->m_finalScore
			       , (int32_t)dp->m_siteRank
			       , SITERANKDIVISOR
			       //, ff
			       );
		// then language weight
		if ( si->m_queryLangId == 0 || 
		     mr->m_language    == 0 ||
		     si->m_queryLangId == mr->m_language )
			sb->safePrintf(" * %.01f",
				       si->m_sameLangWeight);//SAMELANGMULT);
		// the actual min then
		sb->safePrintf(" * %.03f",minScore);
		// no longer list all the scores
		//sb->safeMemcpy ( &ft );
		sb->safePrintf(//")"
			      "]]>"
			      "</finalScoreEquation>\n");
		sb->safePrintf ("\t</result>\n\n");
		return true;
	}

	if ( si->m_format != FORMAT_HTML ) return true;

	char *cc = getCountryCode ( mr->m_country );
	if ( mr->m_country == 0 ) cc = "Unknown";

	sb->safePrintf("<table border=1>"

		      "<tr><td colspan=10><b><center>"
		      "final score</center></b>"
		      "</td></tr>"

		      "<tr>"
		      "<td>docId</td>"
		      "<td>%"INT64"</td>"
		      "</tr>"

		      "<tr>"
		      "<td>site</td>"
		      "<td>%s</td>"
		      "</tr>"

		      "<tr>"
		      "<td>hopcount</td>"
		      "<td>%"INT32"</td>"
		      "</tr>"

		      "<tr>"
		      "<td>language</td>"
		      "<td><font color=green><b>%s</b></font></td>"
		      "</tr>"

		      "<tr>"
		      "<td>country</td>"
		      "<td>%s</td>"
		      "</tr>"

		      "<tr>"
		      "<td>siteRank</td>"
		      "<td><font color=blue>%"INT32"</font></td>"
		      "</tr>"

		      "<tr><td colspan=100>"
		      , dp->m_docId
		      , mr->ptr_site
		      , (int32_t)mr->m_hopcount
		      //, getLanguageString(mr->m_summaryLanguage)
		      , getLanguageString(mr->m_language) // use page language
		      , cc
		      , (int32_t)dp->m_siteRank
		      );

	// list all final scores starting with pairs
	sb->safePrintf("<b>%f</b> = "
		      "(<font color=blue>%"INT32"</font>/%.01f+1)"
		      , dp->m_finalScore
		      , (int32_t)dp->m_siteRank
		      , SITERANKDIVISOR
		      );

	// if lang is different
	if ( si->m_queryLangId == 0 || 
	     mr->m_language    == 0 ||
	     si->m_queryLangId == mr->m_language )
		sb->safePrintf(" * <font color=green><b>%.01f</b></font>",
			       si->m_sameLangWeight);//SAMELANGMULT);

	// list all final scores starting with pairs
	sb->safePrintf(" * %s("
		      , ff
		      );
	sb->safeMemcpy ( &ft );
	sb->safePrintf(")</td></tr></table><br>");

	// put in a hidden div so you can unhide it
	sb->safePrintf("</div>\n");

	// result is in a table so we can put the result # in its own column
	sb->safePrintf("</td></tr></table>");



	// space out 0000 backlinks
	char *p = sb->getBufStart() + placeHolder;
	int32_t plen = placeHolderLen;
	if ( numInlinks == 0 ) 
		memset ( p , ' ' , plen );
	if ( numInlinks > 0 && numInlinks < 99999 ) {
		char *ss = strstr ( p, "00000" );
		if ( ss ) {
			char c = ss[5];
			sprintf(ss,"%5"INT32"",numInlinks);
			ss[5] = c;
		}
	}
	// print "1 backlink" not "1 backlinks"
	if ( numInlinks == 1 ) {
		char *xx = strstr(p,"backlinks");
		if ( xx ) xx[8] = ' ';
	}

	return true;
}




bool printPairScore ( SafeBuf *sb , SearchInput *si , PairScore *ps ,
		      Msg20Reply *mr , Msg40 *msg40 , bool first ) {

	// int16_tcut
	Query *q = &si->m_q;

	//SafeBuf ft;

	// store in final score calc
	//if ( ft.length() ) ft.safePrintf(" + ");
	//ft.safePrintf("%f",ps->m_finalScore);
	
	int32_t qtn1 = ps->m_qtermNum1;
	int32_t qtn2 = ps->m_qtermNum2;
	
	/*
	  unsigned char drl1 = ps->m_diversityRankLeft1;
	  unsigned char drl2 = ps->m_diversityRankLeft2;
	  float dvwl1 = getDiversityWeight(dr1);
	  float dvwl2 = getDiversityWeight(dr2);
	  
	  unsigned char drr1 = ps->m_diversityRankRight1;
	  unsigned char drr2 = ps->m_diversityRankRight2;
	  float dvwr1 = getDiversityWeight(dr1);
	  float dvwr2 = getDiversityWeight(dr2);
	*/
	
	unsigned char de1 = ps->m_densityRank1;
	unsigned char de2 = ps->m_densityRank2;
	float dnw1 = getDensityWeight(de1);
	float dnw2 = getDensityWeight(de2);
	
	int32_t hg1 = ps->m_hashGroup1;
	int32_t hg2 = ps->m_hashGroup2;
	
	
	float hgw1 = getHashGroupWeight(hg1);
	float hgw2 = getHashGroupWeight(hg2);
	
	int32_t wp1 = ps->m_wordPos1;
	int32_t wp2 = ps->m_wordPos2;
	
	unsigned char wr1 = ps->m_wordSpamRank1;
	float wsw1 = getWordSpamWeight(wr1);
	unsigned char wr2 = ps->m_wordSpamRank2;
	float wsw2 = getWordSpamWeight(wr2);
	
	// HACK for inlink text!
	if ( hg1 == HASHGROUP_INLINKTEXT )
		wsw1 = getLinkerWeight(wr1);
	if ( hg2 == HASHGROUP_INLINKTEXT )
		wsw2 = getLinkerWeight(wr2);
	
	char *syn1 = "no";
	char *syn2 = "no";
	float sw1 = 1.0;
	float sw2 = 1.0;
	if ( ps->m_isSynonym1 ) {
		syn1 = "yes";
		sw1  = SYNONYM_WEIGHT;
	}
	if ( ps->m_isSynonym2 ) {
		syn2 = "yes";
		sw2  = SYNONYM_WEIGHT;
	}
	
	
	//char  bf1  = ps->m_bflags1;
	//char  bf2  = ps->m_bflags2;
	char *bs1  = "no";
	char *bs2  = "no";
	//if ( bf1 & BF_HALFSTOPWIKIBIGRAM ) bs1 = "yes";
	//if ( bf2 & BF_HALFSTOPWIKIBIGRAM ) bs2 = "yes";
	if ( ps->m_isHalfStopWikiBigram1 ) bs1 = "yes";
	if ( ps->m_isHalfStopWikiBigram2 ) bs2 = "yes";
	float wbw1 = 1.0;
	float wbw2 = 1.0;
	if ( ps->m_isHalfStopWikiBigram1 ) wbw1 = WIKI_BIGRAM_WEIGHT;
	if ( ps->m_isHalfStopWikiBigram2 ) wbw2 = WIKI_BIGRAM_WEIGHT;
	
	//int64_t sz1 = ps->m_listSize1;
	//int64_t sz2 = ps->m_listSize2;
	//int64_t tf1 = ps->m_termFreq1;//sz1 / 10;
	//int64_t tf2 = ps->m_termFreq2;//sz2 / 10;
	
	QueryTerm *qt1 = &q->m_qterms[qtn1];
	QueryTerm *qt2 = &q->m_qterms[qtn2];

	//int64_t tf1 = msg40->m_msg3a.m_termFreqs[qtn1];
	//int64_t tf2 = msg40->m_msg3a.m_termFreqs[qtn2];
	int64_t tf1 = qt1->m_termFreq;
	int64_t tf2 = qt2->m_termFreq;
	float tfw1 = ps->m_tfWeight1;
	float tfw2 = ps->m_tfWeight2;
	
	char *wp = "no";
	float wiw = 1.0;
	if ( ps->m_inSameWikiPhrase ) {
		wp = "yes";
		wiw = WIKI_WEIGHT; // 0.50;
	}
	int32_t a = ps->m_wordPos2;
	int32_t b = ps->m_wordPos1;
	char *es = "";
	char *bes = "";
	if ( a < b ) {
		a = ps->m_wordPos1;
		b = ps->m_wordPos2;
		// out of query order penalty!
		es = "+ 1.0";
		bes = "+ <b>1.0</b>";
	}
	
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<pairInfo>\n");
		
		
		/*
		  sb->safePrintf("\t\t\t<diversityRankLeft1>%"INT32""
		  "</diversityRankLeft1>\n",
		  (int32_t)drl1);
		  sb->safePrintf("\t\t\t<diversityRankRight1>%"INT32""
		  "</diversityRankRight1>\n",
		  (int32_t)drr1);
		  sb->safePrintf("\t\t\t<diversityWeightLeft1>%f"
		  "</diversityWeightLeft1>\n",
		  dvwl1);
		  sb->safePrintf("\t\t\t<diversityWeightRight1>%f"
		  "</diversityWeightRight1>\n",
		  dvwr1);
		  
		  
		  sb->safePrintf("\t\t\t<diversityRankLeft2>%"INT32""
		  "</diversityRankLeft2>\n",
		  (int32_t)drl2);
		  sb->safePrintf("\t\t\t<diversityRankRight2>%"INT32""
		  "</diversityRankRight2>\n",
		  (int32_t)drr2);
		  sb->safePrintf("\t\t\t<diversityWeightLeft2>%f"
		  "</diversityWeightLeft2>\n",
		  dvwl2);
		  sb->safePrintf("\t\t\t<diversityWeightRight2>%f"
		  "</diversityWeightRight2>\n",
		  dvwr2);
		*/
		
		sb->safePrintf("\t\t\t<densityRank1>%"INT32""
			      "</densityRank1>\n",
			      (int32_t)de1);
		sb->safePrintf("\t\t\t<densityRank2>%"INT32""
			      "</densityRank2>\n",
			      (int32_t)de2);
		sb->safePrintf("\t\t\t<densityWeight1>%f"
			      "</densityWeight1>\n",
			      dnw1);
		sb->safePrintf("\t\t\t<densityWeight2>%f"
			      "</densityWeight2>\n",
			      dnw2);
		
		sb->safePrintf("\t\t\t<term1><![CDATA[");
		sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
				q->m_qterms[qtn1].m_termLen );
		sb->safePrintf("]]></term1>\n");
		sb->safePrintf("\t\t\t<term2><![CDATA[");
		sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
				q->m_qterms[qtn2].m_termLen );
		sb->safePrintf("]]></term2>\n");
		
		sb->safePrintf("\t\t\t<location1><![CDATA[%s]]>"
			      "</location1>\n",
			      getHashGroupString(hg1));
		sb->safePrintf("\t\t\t<location2><![CDATA[%s]]>"
			      "</location2>\n",
			      getHashGroupString(hg2));
		sb->safePrintf("\t\t\t<locationWeight1>%.01f"
			      "</locationWeight1>\n",
			      hgw1 );
		sb->safePrintf("\t\t\t<locationWeight2>%.01f"
			      "</locationWeight2>\n",
			      hgw2 );
		
		sb->safePrintf("\t\t\t<wordPos1>%"INT32""
			      "</wordPos1>\n", wp1 );
		sb->safePrintf("\t\t\t<wordPos2>%"INT32""
			      "</wordPos2>\n", wp2 );
		//int32_t wordDist = wp2 - wp1;
		//if ( wordDist < 0 ) wordDist *= -1;
		//sb->safePrintf("\t\t\t<wordDist>%"INT32""
		//	      "</wordDist>\n",wdist);
		
		sb->safePrintf("\t\t\t<isSynonym1>"
			      "<![CDATA[%s]]>"
			      "</isSynonym1>\n",
			      syn1);
		sb->safePrintf("\t\t\t<isSynonym2>"
			      "<![CDATA[%s]]>"
			      "</isSynonym2>\n",
			      syn2);
		sb->safePrintf("\t\t\t<synonymWeight1>%.01f"
			      "</synonymWeight1>\n",
			      sw1);
		sb->safePrintf("\t\t\t<synonymWeight2>%.01f"
			      "</synonymWeight2>\n",
			      sw2);
		
		// word spam / link text weight
		char *r1 = "wordSpamRank1";
		char *r2 = "wordSpamRank2";
		char *t1 = "wordSpamWeight1";
		char *t2 = "wordSpamWeight2";
		if ( hg1 == HASHGROUP_INLINKTEXT ) {
			r1 = "inlinkSiteRank1";
			t1 = "inlinkTextWeight1";
		}
		if ( hg2 == HASHGROUP_INLINKTEXT ) {
			r2 = "inlinkSiteRank2";
			t2 = "inlinkTextWeight2";
		}
		sb->safePrintf("\t\t\t<%s>%"INT32"</%s>\n",
			      r1,(int32_t)wr1,r1);
		sb->safePrintf("\t\t\t<%s>%"INT32"</%s>\n",
			      r2,(int32_t)wr2,r2);
		sb->safePrintf("\t\t\t<%s>%.02f</%s>\n",
			      t1,wsw1,t1);
		sb->safePrintf("\t\t\t<%s>%.02f</%s>\n",
			      t2,wsw2,t2);
		

		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		for (;k&&hg1==HASHGROUP_INLINKTEXT ; k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > wp1 ) continue;
			if ( k->m_wordPosStart + 50 < wp1 ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t<inlinkId1>%"INT32""
				      "</inlinkId1>\n",
				      k->m_siteHash);
		}

		k = info->getNextInlink(NULL);
		for (;k&&hg2==HASHGROUP_INLINKTEXT ; k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > wp2 ) continue;
			if ( k->m_wordPosStart + 50 < wp2 ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t<inlinkId2>%"INT32""
				      "</inlinkId2>\n",
				      k->m_siteHash);
		}




		// term freq
		sb->safePrintf("\t\t\t<termFreq1>%"INT64""
			      "</termFreq1>\n",tf1);
		sb->safePrintf("\t\t\t<termFreq2>%"INT64""
			      "</termFreq2>\n",tf2);
		sb->safePrintf("\t\t\t<termFreqWeight1>%f"
			      "</termFreqWeight1>\n",tfw1);
		sb->safePrintf("\t\t\t<termFreqWeight2>%f"
			      "</termFreqWeight2>\n",tfw2);
		
		sb->safePrintf("\t\t\t<isWikiBigram1>"
			      "%"INT32"</isWikiBigram1>\n",
			      (int32_t)(ps->m_isHalfStopWikiBigram1));
		sb->safePrintf("\t\t\t<isWikiBigram2>"
			      "%"INT32"</isWikiBigram2>\n",
			      (int32_t)(ps->m_isHalfStopWikiBigram2));
		
		sb->safePrintf("\t\t\t<wikiBigramWeight1>%.01f"
			      "</wikiBigramWeight1>\n",
			      wbw1);
		sb->safePrintf("\t\t\t<wikiBigramWeight2>%.01f"
			      "</wikiBigramWeight2>\n",
			      wbw2);
		
		sb->safePrintf("\t\t\t<inSameWikiPhrase>"
			      "<![CDATA[%s]]>"
			      "</inSameWikiPhrase>\n",
			      wp);
		
		sb->safePrintf("\t\t\t<queryDist>"
			      "%"INT32""
			      "</queryDist>\n",
			      ps->m_qdist );
		
		sb->safePrintf("\t\t\t<wikiWeight>"
			      "%.01f"
			      "</wikiWeight>\n",
			      wiw );
		
		sb->safePrintf("\t\t\t<score>%f</score>\n",
			      ps->m_finalScore);
		
		sb->safePrintf("\t\t\t<equationCanonical>"
			      "<![CDATA["
			      "score = "
			      " 100 * "
			      " locationWeight1" // hgw
			      " * "
			      " locationWeight2" // hgw
			      " * "
			      " synonymWeight1" // synweight
			      " * "
			      " synonymWeight2" // synweight
			      " * "
			      
			      " wikiBigramWeight1"
			      " * "
			      " wikiBigramWeight2"
			      " * "
			      
			      //"diversityWeight1"
			      //" * "
			      //"diversityWeight2"
			      //" * "
			      "densityWeight1" //density weight
			      " * "
			      "densityWeight2" //density weight
			      " * "
			      "%s" // wordspam weight
			      " * "
			      "%s" // wordspam weight
			      " * "
			      "termFreqWeight1" // tfw
			      " * "
			      "termFreqWeight2" // tfw
			      " / ( ||wordPos1 - wordPos2| "
			      " - queryDist| + 1.0 ) * "
			      "wikiWeight"
			      "]]>"
			      "</equationCanonical>\n"
			      , t1
			      , t2
			      );
		
		sb->safePrintf("\t\t\t<equation>"
			      "<![CDATA["
			      "%f="
			      "100*"
			      "<font color=orange>%.1f</font>"//hashgroupweight
			      "*"
			      "<font color=orange>%.1f</font>"//hashgroupweight
			      "*"
			      "<font color=blue>%.1f</font>" // syn weight
			      "*"
			      "<font color=blue>%.1f</font>" // syn weight
			      "*"
			      
			      "<font color=green>%.1f</font>"//wikibigramweight
			      "*"
			      "<font color=green>%.1f</font>"//wikibigramweight
			      "*"
			      
			      "<font color=purple>%.02f</font>"//density weight
			      "*"
			      "<font color=purple>%.02f</font>"//density weight
			      "*"
			      "<font color=red>%.02f</font>" // wordspam weight
			      "*"
			      "<font color=red>%.02f</font>" // wordspam weight
			      "*"
			      "<font color=magenta>%.02f</font>"//tf weight
			      "*"
			      "<font color=magenta>%.02f</font>"//tf weight
			      , ps->m_finalScore
			      , hgw1
			      , hgw2
			      , sw1
			      , sw2
			      , wbw1
			      , wbw2
			      , dnw1
			      , dnw2
			      , wsw1
			      , wsw2
			      , tfw1
			      , tfw2
			      );
		
		if ( ps->m_fixedDistance )
			sb->safePrintf(
				      "/<b>%"INT32"</b> "
				      , (int32_t)FIXED_DISTANCE );
		else
			sb->safePrintf(
				      "/"
				      "(((<font color=darkgreen>%"INT32"</font>"
				      "-<font color=darkgreen>%"INT32"</font>"
				      ")-<font color=lime>%"INT32"</font>)+1.0%s)"
				      ,
				      a,b,ps->m_qdist,bes);
		// wikipedia weight
		if ( wiw != 1.0 )
			sb->safePrintf("*%.01f", wiw );
		sb->safePrintf("]]>"
			      "</equation>\n" );
		sb->safePrintf("\t\t</pairInfo>\n");
		return true; // continue;
	}
	
	
	// print out the entire details i guess
	//sb->safePrintf("<td>%.02f</td>"
	//	      ,ps->m_finalScore
	//	      );
	// . print out the breakout tables then
	// . they should pop-up when the user 
	//   mouses over a cell in the distance matrix
	//sb->safePrintf("<table border=1>"
	//	      "<tr><td colspan=100>"
	//	      "<center><b>");
	//if ( q->m_qterms[qtn1].m_isPhrase )
	//	sb->pushChar('\"');
	//sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
	//		q->m_qterms[qtn1].m_termLen );
	//if ( q->m_qterms[qtn1].m_isPhrase )
	//	sb->pushChar('\"');
	//sb->safePrintf("</b> vs <b>");
	//if ( q->m_qterms[qtn2].m_isPhrase )
	//	sb->pushChar('\"');
	//sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
	//		q->m_qterms[qtn2].m_termLen );
	//if ( q->m_qterms[qtn2].m_isPhrase )
	//	sb->pushChar('\"');
	//sb->safePrintf("</b></center></td></tr>");
	// then print the details just like the
	// single term table below
	//sb->safePrintf("<tr>"
	//	      "<td>term</td>"
	//	      "<td>location</td>"
	//	      "<td>wordPos</td>"
	//	      "<td>synonym</td>"
	//	      "<td>wikibigram</td>"
	//	      //"<td>diversityRank/weight</td>"
	//	      "<td>densityRank</td>"
	//	      "<td>wordSpamRank</td>"
	//	      "<td>inlinkSiteRank</td>"
	//	      "<td>termFreq</td>"
	//	      "<td>inWikiPhrase/qdist</td>"
	//	      "</tr>" 
	//	      );

	//
	// print first term in first row
	//
	sb->safePrintf("<tr><td rowspan=3>");

	sb->safePrintf("<a onclick=\""
		      "var e = document.getElementById('poo');"
		      "if ( e.style.display == 'none' ){"
		      "e.style.display = '';"
		      "}"
		      "else {"
		      "e.style.display = 'none';"
		      "}"
		      "\">"
		      );
	sb->safePrintf("%.04f</a></td>",ps->m_finalScore);
	//sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
	//		q->m_qterms[qtn1].m_termLen );
	//sb->safePrintf("</td>");
	sb->safePrintf("<td>"
		      "%s <font color=orange>"
		      "%.01f</font></td>"
		      , getHashGroupString(hg1)
		      , hgw1 );
	// the word position
	sb->safePrintf("<td>");
		      //"<a href=\"/print?d="
		      //"&page=4&recycle=1&"

	if ( g_conf.m_isMattWells )
		sb->safePrintf("<a href=\"/seo?d=");
	else
		sb->safePrintf("<a href=\"/get?d=");

	sb->safePrintf("%"INT64""
		       "&page=4"
		       //"&page=sections&"
		       "&hipos=%"INT32""
		       "&c=%s#hipos\">"
		       "%"INT32"</a></td>"
		       "</a></td>"
		       ,mr->m_docId
		       ,(int32_t)ps->m_wordPos1
		       ,si->m_cr->m_coll
		       ,(int32_t)ps->m_wordPos1);
	// is synonym?
	//if ( sw1 != 1.00 )
		sb->safePrintf("<td>%s <font color=blue>%.02f"
			      "</font></td>",syn1,sw1);
	//else
	//	sb->safePrintf("<td>&nbsp;</td>");

	
	// wikibigram?/weight
	//if ( wbw1 != 1.0 )
		sb->safePrintf("<td>%s <font color=green>%.02f"
			      "</font></td>",bs1,wbw1);
	//else
	//	sb->safePrintf("<td>&nbsp;</td>");

	
	// diversity -
	// not needed for term pair algo
	//sb->safePrintf("<td>%"INT32"/<font color=green>"
	//	      "%f</font></td>",
	//	      (int32_t)dr1,dvw1);
	
	// density
	sb->safePrintf("<td>%"INT32" <font color=purple>"
		      "%.02f</font></td>",
		      (int32_t)de1,dnw1);
	// word spam
	if ( hg1 == HASHGROUP_INLINKTEXT ) {
		sb->safePrintf("<td>&nbsp;</td>");
		sb->safePrintf("<td>%"INT32" <font color=red>"
			      "%.02f</font></td>",
			      (int32_t)wr1,wsw1);
	}
	else {
		sb->safePrintf("<td>%"INT32"", (int32_t)wr1);
		//if ( wsw1 != 1.0 )
			sb->safePrintf( " <font color=red>"
				       "%.02f</font>",  wsw1);
		sb->safePrintf("</td>");
		sb->safePrintf("<td>&nbsp;</td>");
	}
	
	// term freq
	sb->safePrintf("<td id=tf>%"INT64" <font color=magenta>"
		      "%.02f</font></td>",
		      tf1,tfw1);
	// inSamePhraseId distInQuery phraseWeight
	sb->safePrintf("<td>%s</td><td>%"INT32"</td><td>%.01f</td>"
		       ,wp,ps->m_qdist,wiw);
	// end the row
	sb->safePrintf("</tr>");
	//
	// print 2nd term in 2nd row
	//
	sb->safePrintf("<tr><td>");
	//sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
	//		q->m_qterms[qtn2].m_termLen );
	//sb->safePrintf("</td>");
	sb->safePrintf(//"<td>"
		      "%s <font color=orange>"
		      "%.01f</font></td>"
		      , getHashGroupString(hg2)
		      , hgw2 );
	// the word position
	sb->safePrintf("<td>");
		      //"<a href=\"/print?d="
		      //"%"INT64""
		      //"&page=4&recycle=1&"

	if ( g_conf.m_isMattWells )
		sb->safePrintf("<a href=\"/seo?d=");
	else
		sb->safePrintf("<a href=\"/get?d=");

	sb->safePrintf("%"INT64""
		      "&page=4&"
		      "hipos=%"INT32"&c=%s#hipos\">"
		      "%"INT32"</a></td>"
		      "</a></td>"
		      ,mr->m_docId
		      ,(int32_t)ps->m_wordPos2
		      ,si->m_cr->m_coll
		      ,(int32_t)ps->m_wordPos2);
	
	// is synonym?
	//if ( sw2 != 1.00 )
		sb->safePrintf("<td>%s <font color=blue>%.02f"
			      "</font></td>",syn2,sw2);
	//else
	//	sb->safePrintf("<td>&nbsp;</td>");

	// wikibigram?/weight
	//if ( wbw2 != 1.0 )
		sb->safePrintf("<td>%s <font color=green>%.02f"
			      "</font></td>",bs2,wbw2);
	//else
	//	sb->safePrintf("<td>&nbsp;</td>");

	
	// diversity
	//sb->safePrintf("<td>%"INT32"/<font color=green>"
	//	      "%f</font></td>",
	//	      (int32_t)dr2,dvw2);
	
	// density
	sb->safePrintf("<td>%"INT32" <font color=purple>"
		      "%.02f</font></td>",
		      (int32_t)de2,dnw2);
	// word spam
	if ( hg2 == HASHGROUP_INLINKTEXT ) {
		sb->safePrintf("<td>&nbsp;</td>");
		sb->safePrintf("<td>%"INT32" <font color=red>"
			      "%.02f</font></td>",
			      (int32_t)wr2,wsw2);
	}
	else {
		sb->safePrintf("<td>%"INT32"", (int32_t)wr2);
		//if ( wsw2 != 1.0 )
			sb->safePrintf( " <font color=red>"
				       "%.02f</font>",  wsw2);
		sb->safePrintf("</td>");
		sb->safePrintf("<td>&nbsp;</td>");
	}
	// term freq
	sb->safePrintf("<td id=tf>%"INT64" <font color=magenta>"
		      "%.02f</font></td>",
		      tf2,tfw2);
	// inSamePhraseId distInQuery phraseWeight
	sb->safePrintf("<td>%s</td><td>%"INT32"</td><td>%.01f</td>"
		       ,wp,ps->m_qdist,wiw);
	// end the row
	sb->safePrintf("</tr>");
	sb->safePrintf("<tr><td ");
	// last row is the computation of score
	//static bool s_first = true;
	if ( first ) {
		//static int32_t s_count = 0;
		//s_first = false;
		//sb->safePrintf("id=poo%"INT32" ",s_count);
	}
	sb->safePrintf("colspan=50>" //  style=\"display:none\">"
		      "%.03f "
		      "= "
		      //" ( "
		      "100*"
		      "<font color=orange>%.1f"
		      "</font>"
		      "*"
		      "<font color=orange>%.1f"
		      "</font>"
		      "*"
		      //"(%"INT32" - "
		      , ps->m_finalScore
		      //, idstr
		      , hgw1
		      , hgw2
		      //, (int32_t)MAXWORDPOS+1
		      );
	sb->safePrintf("<font color=blue>%.1f</font>"
		      "*"
		      " <font color=blue>%.1f</font>"
		      "*"
		      
		      // wiki bigram weight
		      "<font color=green>%.02f</font>"
		      "*"
		      "<font color=green>%.02f</font>"
		      "*"
		      
		      "<font color=purple>%.02f</font>"
		      "*"
		      "<font color=purple>%.02f</font>"
		      "*"
		      "<font color=red>%.02f</font>"
		      "*"
		      " <font color=red>%.02f</font>"
		      "*"
		      "<font color=magenta>%.02f</font>"
		      "*"
		      "<font color=magenta>%.02f</font>"
		      , sw1
		      , sw2
		      , wbw1
		      , wbw2
		      , dnw1
		      , dnw2
		      , wsw1
		      , wsw2
		      , tfw1
		      , tfw2
		      );
	if ( ps->m_fixedDistance )
		sb->safePrintf(
			      "/<b>%"INT32"</b> "
			      , (int32_t)FIXED_DISTANCE );
	else
		sb->safePrintf(
			      "/"
			      "(((<font color=darkgreen>%"INT32"</font>"
			      "-<font color=darkgreen>%"INT32"</font>)-"
			      "<font color=lime>%"INT32"</font>) + 1.0%s)"
			      ,
			      a,b,ps->m_qdist,bes);
	// wikipedia weight
	if ( wiw != 1.0 )
		sb->safePrintf("*%.01f", wiw );
	sb->safePrintf( // end formula
		      "</td></tr>"
		      //"</table>"
		      //"<br>");
		      );
	return true;
}

bool printSingleTerm ( SafeBuf *sb , Query *q , SingleScore *ss ) {

	int32_t qtn = ss->m_qtermNum;

	sb->safePrintf("<table border=1 cellpadding=3>");
	sb->safePrintf("<tr><td colspan=50><center><b>");
	// link to rainbow page
	//sb->safePrintf("<a href=\"/print?u=");
	//sb->urlEncode( mr->ptr_ubuf );
	//sb->safePrintf("&page=4&recycle=1&c=%s\">",coll);
	if ( q->m_qterms[qtn].m_isPhrase )
		sb->pushChar('\"');
	sb->safeMemcpy ( q->m_qterms[qtn].m_term ,
			q->m_qterms[qtn].m_termLen );
	if ( q->m_qterms[qtn].m_isPhrase )
		sb->pushChar('\"');
	//sb->safePrintf("</a>");
	sb->safePrintf("</b></center></td></tr>");
	return true;
}

bool printTermPairs ( SafeBuf *sb , Query *q , PairScore *ps ) {
	// print pair text
	int32_t qtn1 = ps->m_qtermNum1;
	int32_t qtn2 = ps->m_qtermNum2;
	sb->safePrintf("<table cellpadding=3 border=1>"
		      "<tr><td colspan=20><center><b>");
	if ( q->m_qterms[qtn1].m_isPhrase )
		sb->pushChar('\"');
	sb->safeMemcpy ( q->m_qterms[qtn1].m_term ,
			q->m_qterms[qtn1].m_termLen );
	if ( q->m_qterms[qtn1].m_isPhrase )
		sb->pushChar('\"');
	sb->safePrintf("</b> vs <b>");
	if ( q->m_qterms[qtn2].m_isPhrase )
		sb->pushChar('\"');
	sb->safeMemcpy ( q->m_qterms[qtn2].m_term ,
			q->m_qterms[qtn2].m_termLen );
	if ( q->m_qterms[qtn2].m_isPhrase )
		sb->pushChar('\"');
	return true;
}

bool printScoresHeader ( SafeBuf *sb ) {

	sb->safePrintf("<tr>"
		      "<td>score</td>"
		      "<td>location</td>"
		      "<td>wordPos</td>"
		      "<td>synonym</td>"
		      "<td>wikibigram</td>"
		      //"<td>diversityRank</td>"
		      "<td>density</td>"
		      "<td>spam</td>"
		      "<td>inlinkPR</td>" // nlinkSiteRank</td>"
		      "<td>termFreq</td>"
		       "<td>inSamePhrase</td>"
		       "<td>distInQuery</td>"
		       "<td>phraseWeight</td>"
		      "</tr>\n" 
		      );
	return true;
}

bool printSingleScore ( SafeBuf *sb , 
			SearchInput *si , 
			SingleScore *ss ,
			Msg20Reply *mr , Msg40 *msg40 ) {

	// int16_tcut
	Query *q = &si->m_q;

	//SafeBuf ft;
	// store in final score calc
	//if ( ft.length() ) ft.safePrintf(" + ");
	//ft.safePrintf("%f",ss->m_finalScore);
	char *syn = "no";
	float sw = 1.0;
	if ( ss->m_isSynonym ) {
		syn = "yes";
		sw = SYNONYM_WEIGHT; // Posdb.h
	}
	//char bf = ss->m_bflags;
	float wbw = 1.0;
	char *bs = "no";
	if ( ss->m_isHalfStopWikiBigram ) {
		bs = "yes";
		wbw = WIKI_BIGRAM_WEIGHT;
	}
	float hgw = getHashGroupWeight(ss->m_hashGroup);
	//float dvw = getDiversityWeight(ss->m_diversityRank);
	float dnw = getDensityWeight(ss->m_densityRank);
	float wsw = getWordSpamWeight(ss->m_wordSpamRank);
	// HACK for inlink text!
	if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT )
		wsw = getLinkerWeight(ss->m_wordSpamRank);
	
	//int64_t tf = ss->m_termFreq;//ss->m_listSize;
	int32_t qtn = ss->m_qtermNum;
	//int64_t tf = msg40->m_msg3a.m_termFreqs[qtn];
	QueryTerm *qt = &q->m_qterms[qtn];
	int64_t tf = qt->m_termFreq;
	float tfw = ss->m_tfWeight;
	
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<termInfo>\n");
		
		/*
		  sb->safePrintf("\t\t\t<diversityRank>%"INT32""
		  "</diversityRank>\n",
		  (int32_t)ss->m_diversityRank);
		  sb->safePrintf("\t\t\t<diversityWeight>%f"
		  "</diversityWeight>\n",
		  dvw);
		*/
		
		sb->safePrintf("\t\t\t<densityRank>%"INT32""
			      "</densityRank>\n",
			      (int32_t)ss->m_densityRank);
		sb->safePrintf("\t\t\t<densityWeight>%f"
			      "</densityWeight>\n",
			      dnw);
		sb->safePrintf("\t\t\t<term><![CDATA[");
		sb->safeMemcpy ( q->m_qterms[qtn].m_term ,
				q->m_qterms[qtn].m_termLen );
		sb->safePrintf("]]></term>\n");
		
		sb->safePrintf("\t\t\t<location><![CDATA[%s]]>"
			      "</location>\n",
			      getHashGroupString(ss->m_hashGroup));
		sb->safePrintf("\t\t\t<locationWeight>%.01f"
			      "</locationWeight>\n",
			      hgw );
		sb->safePrintf("\t\t\t<wordPos>%"INT32""
			      "</wordPos>\n", (int32_t)ss->m_wordPos );
		sb->safePrintf("\t\t\t<isSynonym>"
			      "<![CDATA[%s]]>"
			      "</isSynonym>\n",
			      syn);
		sb->safePrintf("\t\t\t<synonymWeight>%.01f"
			      "</synonymWeight>\n",
			      sw);
		sb->safePrintf("\t\t\t<isWikiBigram>%"INT32""
			      "</isWikiBigram>\n",
			      (int32_t)(ss->m_isHalfStopWikiBigram) );
		sb->safePrintf("\t\t\t<wikiBigramWeight>%.01f"
			      "</wikiBigramWeight>\n",
			      (float)WIKI_BIGRAM_WEIGHT);
		// word spam
		if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
			sb->safePrintf("\t\t\t<inlinkSiteRank>%"INT32""
				      "</inlinkSiteRank>\n",
				      (int32_t)ss->m_wordSpamRank);
			sb->safePrintf("\t\t\t<inlinkTextWeight>%.02f"
				      "</inlinkTextWeight>\n",
				      wsw);
		}
		else {
			sb->safePrintf("\t\t\t<wordSpamRank>%"INT32""
				      "</wordSpamRank>\n",
				      (int32_t)ss->m_wordSpamRank);
			sb->safePrintf("\t\t\t<wordSpamWeight>%.02f"
				      "</wordSpamWeight>\n",
				      wsw);
		}


		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		for ( ; k && ss->m_hashGroup==HASHGROUP_INLINKTEXT ; 
		      k=info->getNextInlink(k)){
			if ( ! k->getLinkText() ) continue;
			if ( k->m_wordPosStart > ss->m_wordPos ) continue;
			if ( k->m_wordPosStart + 50 < ss->m_wordPos ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb->safePrintf("\t\t\t<inlinkId>%"INT32""
				      "</inlinkId>\n",
				      k->m_siteHash);
		}

		// term freq
		sb->safePrintf("\t\t\t<termFreq>%"INT64""
			      "</termFreq>\n",tf);
		sb->safePrintf("\t\t\t<termFreqWeight>%f"
			      "</termFreqWeight>\n",tfw);
		
		sb->safePrintf("\t\t\t<score>%f</score>\n",
			      ss->m_finalScore);
		
		sb->safePrintf("\t\t\t<equationCanonical>"
			      "<![CDATA["
			      "score = "
			      " 100 * "
			      " locationWeight" // hgw
			      " * "
			      " locationWeight" // hgw
			      " * "
			      " synonymWeight" // synweight
			      " * "
			      " synonymWeight" // synweight
			      " * "
			      
			      " wikiBigramWeight"
			      " * "
			      " wikiBigramWeight"
			      " * "
			      
			      //" diversityWeight" // divweight
			      //" * "
			      //" diversityWeight" // divweight
			      //" * "
			      "densityWeight" // density weight
			      " * "
			      "densityWeight" // density weight
			      " * "
			      "wordSpamWeight" // wordspam weight
			      " * "
			      "wordSpamWeight" // wordspam weight
			      " * "
			      "termFreqWeight" // tfw
			      " * "
			      "termFreqWeight" // tfw
			      //" / ( 3.0 )"
			      "]]>"
			      "</equationCanonical>\n"
			      );
		
		sb->safePrintf("\t\t\t<equation>"
			      "<![CDATA["
			      "%f="
			      "100*"
			      "%.1f" // hgw
			      "*"
			      "%.1f" // hgw
			      "*"
			      
			      "%.1f" // synweight
			      "*"
			      "%.1f" // synweight
			      "*"
			      
			      
			      "%.02f" // wikibigram weight
			      "*"
			      "%.02f" // wikibigram weight
			      "*"
			      
			      "%.02f" // density weight
			      "*"
			      "%.02f" // density weight
			      "*"
			      "%.02f" // wordspam weight
			      "*"
			      "%.02f" // wordspam weight
			      "*"
			      "%.02f" // tfw
			      "*"
			      "%.02f" // tfw
			      //" / ( 3.0 )"
			      "]]>"
			      "</equation>\n"
			      , ss->m_finalScore
			      , hgw
			      , hgw
			      , sw
			      , sw
			      , wbw
			      , wbw
			      , dnw
			      , dnw
			      , wsw
			      , wsw
			      , tfw
			      , tfw
			      );
		sb->safePrintf("\t\t</termInfo>\n");
		return true;
	}



	sb->safePrintf("<tr>"
		      "<td rowspan=2>%.03f</td>\n"
		      "<td>%s <font color=orange>%.1f"
		      "</font></td\n>"
		      // wordpos
		      "<td>"
		      "<a href=\"/get?d=" 
		      , ss->m_finalScore
		      , getHashGroupString(ss->m_hashGroup)
		      , hgw
		      );
	//sb->urlEncode( mr->ptr_ubuf );
	sb->safePrintf("%"INT64"",mr->m_docId );
	sb->safePrintf("&page=4&"
		      "hipos=%"INT32"&c=%s#hipos\">"
		      ,(int32_t)ss->m_wordPos
		      ,si->m_cr->m_coll);
	sb->safePrintf("%"INT32"</a></td>\n"
		      "<td>%s <font color=blue>%.1f"
		      "</font></td>\n" // syn
		      
		      // wikibigram?/weight
		      "<td>%s <font color=green>%.02f</font></td>\n"
		      
		      //"<td>%"INT32"/<font color=green>%f"
		      //"</font></td>" // diversity
		      "<td>%"INT32" <font color=purple>"
		      "%.02f</font></td>\n" // density
		      , (int32_t)ss->m_wordPos
		      , syn
		      , sw // synonym weight
		      , bs
		      , wbw
		      //, (int32_t)ss->m_diversityRank
		      //, dvw
		      , (int32_t)ss->m_densityRank
		      , dnw
		      );
	if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
		sb->safePrintf("<td>&nbsp;</td>"
			      "<td>%"INT32" <font color=red>%.02f"
			      "</font></td>\n" // wordspam
			      , (int32_t)ss->m_wordSpamRank
			      , wsw
			      );
	}
	else {
		sb->safePrintf("<td>%"INT32" <font color=red>%.02f"
			      "</font></td>" // wordspam
			      "<td>&nbsp;</td>\n"
			      , (int32_t)ss->m_wordSpamRank
			      , wsw
			      );
		
	}
	
	sb->safePrintf("<td id=tf>%"INT64" <font color=magenta>"
		      "%.02f</font></td>\n" // termfreq
		      "</tr>\n"
		      , tf
		      , tfw
		      );
	// last row is the computation of score
	sb->safePrintf("<tr><td colspan=50>"
		      "%.03f "
		      " = "
		      //" %"INT32" * "
		      "100 * "
		      " <font color=orange>%.1f</font>"
		      " * "
		      " <font color=orange>%.1f</font>"
		      " * "
		      " <font color=blue>%.1f</font>"
		      " * "
		      " <font color=blue>%.1f</font>"
		      " * "
		      " <font color=green>%.02f</font>"//wikibigramwght
		      " * "
		      " <font color=green>%.02f</font>"
		      " * "
		      "<font color=purple>%.02f</font>"
		      " * "
		      "<font color=purple>%.02f</font>"
		      " * "
		      "<font color=red>%.02f</font>"
		      " * "
		      "<font color=red>%.02f</font>"
		      " * "
		      "<font color=magenta>%.02f</font>"
		      " * "
		      "<font color=magenta>%.02f</font>"
		      //" / ( 3.0 )"
		      // end formula
		      "</td></tr>\n"
		      , ss->m_finalScore
		      //, (int32_t)MAXWORDPOS+1
		      , hgw
		      , hgw
		      , sw
		      , sw
		      , wbw
		      , wbw
		      //, dvw
		      //, dvw
		      , dnw
		      , dnw
		      , wsw
		      , wsw
		      , tfw
		      , tfw
		      );
	//sb->safePrintf("</table>"
	//	      "<br>");
	return true;
}

////////
//
// . print the directory subtopics
// . show these when we are in a directory topic browsing dmoz
// . just a list of all the topics/categories
//
////////
bool printDMOZSubTopics ( SafeBuf *sb, int32_t catId, bool inXml ) {

	if ( catId <= 0 ) return true;

	int32_t currType;
	bool first;
	bool nextColumn;
	int32_t maxPerColumn;
	int32_t currInColumn;
	int32_t currIndex;
	char *prefixp;
	int32_t prefixLen;
	char *catName;
	int32_t catNameLen;
	char encodedName[2048];

	//SearchInput *si = &st->m_si;

	bool isRTL = g_categories->isIdRTL ( catId );

	SafeBuf subCatBuf;
	// stores a list of SubCategories into "subCatBuf"
	int32_t numSubCats = g_categories->generateSubCats ( catId , &subCatBuf );

	// . get the subcategories for a given categoriy
	// . msg2b::gernerateDirectory() was launched in Msg40.cpp
	//int32_t numSubCats      = st->m_msg40.m_msg2b.m_numSubCats;
	//SubCategory *subCats = st->m_msg40.m_msg2b.m_subCats;
	//char *catBuffer      = st->m_msg40.m_msg2b.m_catBuffer;
	//bool showAdultOnTop  = st->m_si.m_cr->m_showAdultCategoryOnTop;


	// just print <hr> if no sub categories
	if (inXml) {
		sb->safePrintf ( "\t<directory>\n"
				"\t\t<dirId>%"INT32"</dirId>\n"
				"\t\t<dirName><![CDATA[",
				catId);//si.m_cat_dirId );
		g_categories->printPathFromId ( sb, 
						catId, // st->m_si.m_cat_dirId,
						true );
		sb->safePrintf ( "]]></dirName>\n");
		sb->safePrintf ( "\t\t<dirIsRTL>%"INT32"</dirIsRTL>\n",
				(int32_t)isRTL);
	}

	char *p    = subCatBuf.getBufStart();
	char *pend = subCatBuf.getBuf();
	SubCategory *ptrs[MAX_SUB_CATS];
	int32_t count = 0;

	if (numSubCats <= 0)
		goto dirEnd;
	// print out the cats
	currType = 0;

	// first make ptrs to them
	for ( ; p < pend ; ) {
		SubCategory *cat = (SubCategory *)p;
		ptrs[count++] = cat;
		p += cat->getRecSize();
		// do not breach
		if ( count >= MAX_SUB_CATS ) break;
	}


	for (int32_t i = 0; i < count ; i++ ) {
		SubCategory *cat = ptrs[i];
		first = false;
		catName = cat->getName();//&catBuffer[subCats[i].m_nameOffset];
		catNameLen = cat->m_nameLen;//subCats[i].m_nameLen;
		// this is the last topic in the dmoz dir path
		// so if the dmoz topic is Top/Arts/Directories then
		// the prefixp is "Directories"
		prefixp = cat->getPrefix();//&catBuffer[subCats[i].m_prefixOffset];
		prefixLen = cat->m_prefixLen;//subCats[i].m_prefixLen;
		// skip bad categories
		currIndex=g_categories->getIndexFromPath(catName,catNameLen);
		if (currIndex < 0)
			continue;
		// skip top adult category if we're supposed to
		/*
		if ( !inXml && 
		     st->m_si.m_catId == 1 && 
		     si->m_familyFilter &&
		     g_categories->isIndexAdultStart ( currIndex ) )
			continue;
		*/
		// check for room
		//if (p + subCats[i].m_prefixLen*2 +
		//	subCats[i].m_nameLen*2 +
		//	512 > pend){
		//	goto diroverflow;
		//}
		// print simple xml tag for inXml
		if (inXml) {
			switch ( cat->m_type ) {
			case SUBCAT_LETTERBAR:
				sb->safePrintf ( "\t\t<letterbar><![CDATA[" );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</letterbar>\n" );
				break;
			case SUBCAT_NARROW2:
				sb->safePrintf ( "\t\t<narrow2><![CDATA[" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>");
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</narrow2>\n" );
				break;
			case SUBCAT_NARROW1:
				sb->safePrintf ( "\t\t<narrow1><![CDATA[" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</narrow1>\n" );
				break;
			case SUBCAT_NARROW:
				sb->safePrintf ( "\t\t<narrow><![CDATA[" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</narrow>\n" );
				break;
			case SUBCAT_SYMBOLIC2:
				sb->safePrintf ( "\t\t<symbolic2><![CDATA[" );
				sb->utf8Encode2 ( prefixp, prefixLen  );
				sb->safePrintf ( ":" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</symbolic2>\n" );
				break;
			case SUBCAT_SYMBOLIC1:
				sb->safePrintf ( "\t\t<symbolic1><![CDATA[" );
				sb->utf8Encode2 ( prefixp, prefixLen  );
				sb->safePrintf ( ":" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</symbolic1>\n" );
				break;
			case SUBCAT_SYMBOLIC:
				sb->safePrintf ( "\t\t<symbolic><![CDATA[" );
				sb->utf8Encode2 ( prefixp, prefixLen  );
				sb->safePrintf ( ":" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</symbolic>\n" );
				break;
			case SUBCAT_RELATED:
				sb->safePrintf ( "\t\t<related><![CDATA[" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</related>\n" );
				break;
			case SUBCAT_ALTLANG:
				sb->safePrintf ( "\t\t<altlang><![CDATA[" );
				sb->utf8Encode2 ( prefixp, prefixLen  );
				sb->safePrintf ( ":" );
				sb->utf8Encode2 ( catName, catNameLen );
				sb->safePrintf ( "]]>" );
				sb->safePrintf ( "<urlcount>%"INT32"</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb->safePrintf ( "</altlang>\n");
				break;
			}
			continue;
		}
		// print type header
		if ( cat->m_type - currType >= 10) {
			// end the last type
			if (currType == SUBCAT_LETTERBAR)
				sb->safePrintf(" ]</center>\n");
			else if (currType != 0)
				sb->safePrintf ( "\n</span></ul></td></tr>"
						"</table>\n" );
			// start the new type
			switch (cat->m_type) {
			case SUBCAT_LETTERBAR:
				sb->safePrintf ( "<span class=\"directory\">"
						"<center>[ " );
				break;
			case SUBCAT_NARROW2:
			case SUBCAT_SYMBOLIC2:
			case SUBCAT_NARROW1:
			case SUBCAT_SYMBOLIC1:
			case SUBCAT_NARROW:
			case SUBCAT_SYMBOLIC:
				sb->safePrintf("<hr>\n");
				break;
			case SUBCAT_RELATED:
				if (currType == 0 ||
				    currType == SUBCAT_LETTERBAR)
					sb->safePrintf("<hr>");
				else
					sb->safePrintf("<br>");
				if (isRTL)
					sb->safePrintf("<span dir=ltr>");
				sb->safePrintf ( "<b>Related Categories:"
						"</b>" );
				if (isRTL)
					sb->safePrintf("</span>");
				break;
			case SUBCAT_ALTLANG:
				if (currType == 0 ||
				    currType == SUBCAT_LETTERBAR)
					sb->safePrintf("<hr>");
				else
					sb->safePrintf("<br>");
				if (isRTL)
					sb->safePrintf("<span dir=ltr>");
				sb->safePrintf ( "<b>This category in other"
						" languages:</b>");
				if (isRTL)
					sb->safePrintf("</span>");
				break;
			}
			currType = ( cat->m_type/10)*10;
			first = true;
			nextColumn = false;
			currInColumn = 0;
			if (currType == SUBCAT_LETTERBAR ||
			    currType == SUBCAT_RELATED)
				maxPerColumn = 999;
			else {
				// . check how many columns we'll use for this
				//   type
				int32_t numInType = 1;
				for (int32_t j = i+1; j < numSubCats; j++) {
					if ( ptrs[j]->m_type - currType >= 10)
						break;
					numInType++;
				}
				// column for every 5, up to 3 columns
				int32_t numColumns = numInType/5;
				if ( numInType%5 > 0 ) numColumns++;
				if ( currType == SUBCAT_ALTLANG &&
				     numColumns > 4)
					numColumns = 4;
				else if (numColumns > 3)
					numColumns = 3;
				// max number of links per column
				maxPerColumn = numInType/numColumns;
				if (numInType%numColumns > 0)
					maxPerColumn++;
			}
		}
		// start the sub cat
		if (first) {
			if (currType != SUBCAT_LETTERBAR)
				sb->safePrintf ( "<table border=0>"
						"<tr><td valign=top>"
						"<ul><span class=\"directory\">"
						"\n<li>");
		}
		// check for the next column
		else if (nextColumn) {
			sb->safePrintf ( "\n</span></ul></td><td valign=top>"
					"<ul><span class=\"directory\">"
					"\n<li>");
			nextColumn = false;
		}
		// or just next link
		else {
			if (currType == SUBCAT_LETTERBAR)
				sb->safePrintf("| ");
			else
				sb->safePrintf("<li>");
		}
		// print out the prefix as a link
		//if ( p + catNameLen + 16 > pend ) {
		//	goto diroverflow;
		//}
		sb->safePrintf("<a href=\"/");
		sb->utf8Encode2(catName, catNameLen);
		sb->safePrintf("/\">");
		// prefix...
		//if ( p + prefixLen + 512 > pend ) {
		//	goto diroverflow;
		//}
		if (currType != SUBCAT_ALTLANG)
			sb->safePrintf("<b>");
		else {
			// check for coded <b> or <strong> tags, remove
			if (prefixLen >= 19 &&
			    strncasecmp(prefixp, "&lt;b&gt;", 9) == 0 &&
			    strncasecmp(prefixp + (prefixLen-10), 
				    "&lt;/b&gt;", 10) == 0) {
				prefixp += 9;
				prefixLen -= 19;
			}
			else if (prefixLen >= 29 &&
			    strncasecmp(prefixp, "&lt;strong&gt;", 14) == 0 &&
			    strncasecmp(prefixp + (prefixLen-15), 
				    "&lt;/strong&gt;", 15) == 0) {
				prefixp += 14;
				prefixLen -= 29;
			}
		}
		if (currType == SUBCAT_RELATED) {
			// print the full path
			if (g_categories->isIndexRTL(currIndex))
				sb->safePrintf("<span dir=ltr>");
			g_categories->printPathFromIndex (
							sb,
							currIndex,
							false,
							isRTL);
		}
		else {
			char *encodeEnd = htmlEncode ( encodedName,
						       encodedName + 2047,
						       prefixp,
						       prefixp + prefixLen );
			prefixp = encodedName;
			prefixLen = encodeEnd - encodedName;
			//if ( p + prefixLen + 512 > pend ) {
			//	goto diroverflow;
			//}
			for (int32_t c = 0; c < prefixLen; c++) {
				if (*prefixp == '_')
					//*p = ' ';
					sb->safePrintf(" ");
				else
					//*p = *prefixp;
					sb->utf8Encode2(prefixp, 1);
				//p++;
				prefixp++;
			}
		}
		//if ( p + 512 > pend ) {
		//	goto diroverflow;
		//}
		// end the link
		if (currType != SUBCAT_ALTLANG)
			sb->safePrintf("</b>");
		sb->safePrintf("</a>");
		// print an @ for symbolic links
		if ( (cat->m_type % 10) == 1)
			sb->safePrintf("@");
		// print number of urls under here
		if ( cat->m_type != SUBCAT_LETTERBAR) { 
			sb->safePrintf("&nbsp&nbsp<i>");
			if (isRTL)
				sb->safePrintf ( "<span dir=ltr>(%"INT32")"
						"</span></i>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
			else
				sb->safePrintf ( "(%"INT32")</i>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
		}
		// next line/letter
		if ( cat->m_type == SUBCAT_LETTERBAR) {
			sb->safePrintf(" ");
			continue;
		}
		// check for next column
		currInColumn++;
		if (currInColumn >= maxPerColumn) {
			currInColumn = 0;
			nextColumn = true;
		}
	}
	//if ( p + 512 > pend ) {
	//	goto diroverflow;
	//}
	// end the last type
	if (!inXml) {
		if (currType == SUBCAT_LETTERBAR)
			sb->safePrintf(" ]</center>\n");
		else
			sb->safePrintf("</ul></td></tr></table>\n");
	}
dirEnd:
	if (inXml)
		sb->safePrintf("\t</directory>\n");
	else {
		sb->safePrintf("</span>");
		sb->safePrintf("<hr>\n");//<br>\n");
	}

	return true;
}

bool printDMOZCrumb ( SafeBuf *sb , int32_t catId , bool xml ) {

	// catid -1 means error
	if ( catId <= 0 ) return true;

	int32_t dirIndex = g_categories->getIndexFromId(catId);
	//  dirIndex = g_categories->getIndexFromId(si->m_cat_sdir);
	if (dirIndex < 0) dirIndex = 0;
	//   display the directory bread crumb
	//if( (si->m_cat_dirId > 0 && si->m_isMasterAdmin && !si->m_isFriend)
	//     || (si->m_cat_sdir > 0 && si->m_cat_sdirt != 0) )
	//	sb->safePrintf("<br><br>");
	// int16_tcut. rtl=Right To Left language format.
	bool rtl = g_categories->isIdRTL ( catId ) ;
	//st->m_isRTL = rtl;
	if ( ! xml ) {
		sb->safePrintf("\n<font size=4><b>");
		if ( rtl ) sb->safePrintf("<span dir=ltr>");
		//sb->safePrintf("<a href=\"/Top\">Top</a>: ");
	}
	// put crumbin xml?
	if ( xml ) 
		sb->safePrintf("<breacdcrumb><![CDATA[");
	// display the breadcrumb in xml or html?
	g_categories->printPathCrumbFromIndex(sb,dirIndex,rtl);
	
	if ( xml )
		sb->safePrintf("]]></breadcrumb>\n" );
	
	// how many urls/entries in this topic?
	int32_t nu =g_categories->getNumUrlsFromIndex(dirIndex);

	// print the num
	if ( ! xml ) {
		sb->safePrintf("</b>&nbsp&nbsp<i>");
		if ( rtl )
			sb->safePrintf("<span dir=ltr>(%"INT32")</span>",nu);
		else
			sb->safePrintf("(%"INT32")", nu);
		sb->safePrintf("</i></font><br><br>\n");
	}
	return true;
}

bool printDmozRadioButtons ( SafeBuf *sb , int32_t catId ) ;

bool printFrontPageShell ( SafeBuf *sb , char *tabName , CollectionRec *cr,
			   bool printGigablast ) ;

// if catId >= 1 then print the dmoz radio button
bool printLogoAndSearchBox ( SafeBuf *sb , HttpRequest *hr , int32_t catId ,
			     SearchInput *si ) {

	char *root = "";
	if ( g_conf.m_isMattWells )
		root = "http://www.gigablast.com";

	// now make a TABLE, left PANE contains gigabits and stuff


	/*
	sb->safePrintf(
		      // logo and menu table
		      "<table border=0 cellspacing=5>"
		      //"style=color:blue;>"
		      "<tr>"

		      // take out logo now that we have the circle rocket
		      // "<td rowspan=2 valign=top>"
		      // "<a href=/>"
		      // "<img "
		      // "border=0 "
		      // "src=%s/logo-small.png "
		      // "height=64 width=295>"
		      // "</a>"
		      // "</td>"
		      
		      "<td>"
		      //, root
		      );
	*/


	if ( catId >= 0 ) {
		CollectionRec *cr = g_collectiondb.getRec ( hr );
		printFrontPageShell ( sb , "directory",cr,true);//PAGE_DIRECTOR
	}

	/*
	// menu above search box
	sb->safePrintf(
		      "<br>"
		      
		      " &nbsp; "
		      );

	if ( catId <= 0 )
		sb->safePrintf("<b title=\"Search the web\">web</b>");
	else
		sb->safePrintf("<a title=\"Search the web\" href=/>web</a>");


	sb->safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; "  );


	if ( g_conf.m_isMattWells ) {
		//  SEO functionality not included yet - so redir to gigablast.
		if ( g_conf.m_isMattWells )
			sb->safePrintf("<a title=\"Rank higher in "
				      "Google\" href='/seo'>");
		else
			sb->safePrintf("<a title=\"Rank higher in "
				      "Google\" href='https://www.gigablast."
				      "com/seo'>");
	
		sb->safePrintf(
			      "seo</a>"
			      " &nbsp;&nbsp;&nbsp;&nbsp; "
			      );
	}


	if (catId <= 0 )
		sb->safePrintf("<a title=\"Browse the DMOZ directory\" "
			      "href=/Top>"
			      "directory"
			      "</a>" );
	else
		sb->safePrintf("<b title=\"Browse the DMOZ directory\">"
			      "directory</b>");
	*/

	char *coll = hr->getString("c");
	if ( ! coll ) coll = "";

	// if there's a ton of sites use the post method otherwise
	// they won't fit into the http request, the browser will reject
	// sending such a large request with "GET"
	char *method = "GET";
	if ( si && si->m_sites && gbstrlen(si->m_sites)>800 ) method = "POST";


	sb->safePrintf(

		       //" &nbsp;&nbsp;&nbsp;&nbsp; "
		      
		      // i'm not sure why this was removed. perhaps
		      // because it is not working yet because of
		      // some bugs...
		      // "<a title=\"Advanced web search\" "
		      // "href=/adv.html>"
		      // "advanced"
		      // "</a>"
		      
		      // " &nbsp;&nbsp;&nbsp;&nbsp;"
		      
		      // "<a title=\"Add your url to the index\" "
		      // "href=/addurl>"
		      // "add url"
		      // "</a>"
		      
		      /*
			" &nbsp;&nbsp;|&nbsp;&nbsp; "
			
			"<a title=\"Words from Gigablast\" "
			"href=/blog.html>"
			"blog"
			"</a>"
			
			" &nbsp;&nbsp;|&nbsp;&nbsp; "
			
			"<a title=\"About Gigablast\" href=/about.html>"
			"about"
			"</a>"
		      */
		      
		       //"<br><br>"
		      //
		      // search box
		      //
		      "<form name=f method=%s action=/search>\n\n" 

		      // propagate the collection if they re-search
		      "<input name=c type=hidden value=\"%s\">"
		       , method
		      , coll
		      );

	// propagate prepend
	char *prepend = hr->getString("prepend");
	if ( prepend ) {
		sb->safePrintf("<input name=prepend type=hidden value=\"");
		sb->htmlEncode ( prepend, gbstrlen(prepend), false);
		sb->safePrintf("\">");
	}
	

	// put search box in a box
	sb->safePrintf(
		       "<br>"
		       "<br>"
		       "<br>"
		       "<div style="
		       "background-color:#fcc714;"
		       "border-style:solid;"
		       "border-width:3px;"
		       "border-color:blue;"
		       //"background-color:blue;"
		       "padding:20px;"
		       "border-radius:20px;"
		       ">");


	sb->safePrintf (
			//"<div style=margin-left:5px;margin-right:5px;>
			"<input size=40 type=text name=q "

			"style=\""
			//"width:%"INT32"px;"
			"height:26px;"
			"padding:0px;"
			"font-weight:bold;"
			"padding-left:5px;"
			//"border-radius:10px;"
			"margin:0px;"
			"border:1px inset lightgray;"
			"background-color:#ffffff;"
			"font-size:18px;"
			"\" "


			"value=\""
			);

	// contents of search box
	int32_t  qlen;
	char *qstr = hr->getString("q",&qlen,"",NULL);
	sb->htmlEncode ( qstr , qlen , false );

	// if it was an advanced search, this can be empty
	if ( qlen == 0 && si && si->m_displayQuery )
		sb->htmlEncode ( si->m_displayQuery );

	sb->safePrintf ("\">"
			//"<input type=submit value=\"Search\" border=0>"

			"&nbsp; &nbsp;"

			"<div onclick=document.f.submit(); "

			" onmouseover=\""
			"this.style.backgroundColor='lightgreen';"
			"this.style.color='black';\""
			" onmouseout=\""
			"this.style.backgroundColor='green';"
			"this.style.color='white';\" "

			"style=border-radius:28px;"
			"cursor:pointer;"
			"cursor:hand;"
			"border-color:white;"
			"border-style:solid;"
			"border-width:3px;"
			"padding:12px;"
			"width:20px;"
			"height:20px;"
			"display:inline-block;"
			"background-color:green;color:white;>"
			"<b style=margin-left:-5px;font-size:18px;"
			">GO</b>"
			"</div>"
			);


	// print "Search [ ] sites  [ ] pages in this topic or below"
	if ( catId >= 0 ) {
		sb->safePrintf("<br>");
		printDmozRadioButtons(sb,catId);
	}

	sb->safePrintf(	"</div>"
			"<br>"
			"<br>"
		       );

	/*
	else {
		sb->safePrintf("Try your search on: "
			      "&nbsp;&nbsp; "
			      "<a href=https://www.google"
			      ".com/search?q="
			      );
		sb->urlEncode ( qstr );
		sb->safePrintf (">google</a> &nbsp;&nbsp;&nbsp;&nbsp; "
			       "<a href=http://www.bing.com/sea"
			       "rch?q=");
		sb->urlEncode ( qstr );		
		sb->safePrintf (">bing</a>");
	}
	*/

	// do not print filter bar if showing a dmoz topic
	if ( catId < 0 )
		printSearchFiltersBar ( sb , hr );
	

	sb->safePrintf( "</form>\n"
		       // 	"</td>"
		       // "</tr>"
		       // "</table>\n"
		       );
	return true;
}

bool printDmozRadioButtons ( SafeBuf *sb , int32_t catId ) {
	sb->safePrintf("Search "
		      "<input type=radio name=prepend "
		      "value=gbipcatid:%"INT32" checked> sites "
		      "<input type=radio name=prepend "
		      "value=gbpcatid:%"INT32"> pages "
		      "in this topic or below"
		      , catId
		      , catId
		      );
	return true;
}

/*
// print the search options under a dmoz search box
bool printDirectorySearchType ( SafeBuf& sb, int32_t sdirt ) {
	// default to entire directory
	if (sdirt < 1 || sdirt > 4)
		sdirt = 3;

	// by default search the whole thing
	sb->safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"3\"");
	if (sdirt == 3) sb->safePrintf(" checked>");
	else            sb->safePrintf(">");
	sb->safePrintf("Entire Directory<br>\n");
	// entire category
	sb->safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"1\"");
	if (sdirt == 1) sb->safePrintf(" checked>");
	else            sb->safePrintf(">");
	sb->safePrintf("Entire Category<br>\n");
	// base category only
	sb->safePrintf("<nobr><input type=\"radio\" name=\"sdirt\" value=\"2\"");
	if (sdirt == 2) sb->safePrintf(" checked>");
	else            sb->safePrintf(">"); 
	sb->safePrintf("Pages in Base Category</nobr><br>\n");
	// sites in base category
	sb->safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"7\"");
	if (sdirt == 7) sb->safePrintf(" checked>");
	else            sb->safePrintf(">");
	sb->safePrintf("Sites in Base Category<br>\n");
	// sites in entire category
	sb->safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"6\"");
	if (sdirt == 6) sb->safePrintf(" checked>");
	else            sb->safePrintf(">");
	sb->safePrintf("Sites in Entire Category<br>\n");
	// end it
	return true;
}
*/

// return 1 if a should be before b
int csvPtrCmp ( const void *a, const void *b ) {
	//JsonItem *ja = (JsonItem **)a;
	//JsonItem *jb = (JsonItem **)b;
	char *pa = *(char **)a;
	char *pb = *(char **)b;
	if ( strcmp(pa,"type") == 0 ) return -1;
	if ( strcmp(pb,"type") == 0 ) return  1;
	// force title on top
	if ( strcmp(pa,"product.title") == 0 ) return -1;
	if ( strcmp(pb,"product.title") == 0 ) return  1;
	if ( strcmp(pa,"title") == 0 ) return -1;
	if ( strcmp(pb,"title") == 0 ) return  1;

	// this is now taken care of from the 'supps[]' array below
	// by prepending two digits before each field name

	// put url first for spider status docs
	// if ( strcmp(pa,"gbssUrl") == 0 ) return -1;
	// if ( strcmp(pb,"gbssUrl") == 0 ) return  1;

	// if ( strcmp(pa,"gbssStatusMsg") == 0 ) return -1;
	// if ( strcmp(pb,"gbssStatusMsg") == 0 ) return  1;

	// if ( strcmp(pa,"gbssStatusCode") == 0 ) return -1;
	// if ( strcmp(pb,"gbssStatusCode") == 0 ) return  1;


	// otherwise string compare
	int val = strcmp(pa,pb);

	return val;
}
	

#include "Json.h"

bool printCSVHeaderRow2 ( SafeBuf *sb ,
			  int32_t ct ,
			  CollectionRec *cr ,
			  SafeBuf *nameBuf ,
			  HashTableX *columnTable ,
			  Msg20 **msg20s ,
			  int32_t numMsg20s ,
			  int32_t *numPtrsArg ) {

	*numPtrsArg = 0;

	char tmp1[1024];
	SafeBuf tmpBuf (tmp1 , 1024);

	char nbuf[27000];
	HashTableX nameTable;
	if ( ! nameTable.set ( 8,4,2048,nbuf,27000,false,0,"ntbuf") )
		return false;

	int32_t niceness = 0;

	// if doing spider status docs not all will have dupofdocid field
	char *supps [] = { 
		"00gbssUrl",
		"01gbssDocId",
		"02gbssDiscoveredTime",
		"03gbssSpiderTime",
		"06gbssContentLen",
		"07gbssDupOfDocId" ,
		"08gbssNumRedirects",
		"09gbssFinalRedirectUrl",
		"10gbssCrawlDelayMS",
		"11gbssCrawlRound",
		"12gbssPrevTotalNumIndexAttempts",
		"13gbssHopCount",
		"14gbssStatusMsg",
		"15gbssDiffbotUri",
		"16gbssSentToDiffbotThisTime",
		"17gbssDiffbotReplyMsg",

		"gbssIp",
		"gbssPercentContentChanged",
		"gbssDownloadStartTime",
		"gbssDownloadEndTime",
		"gbssContentType",
		"gbssHttpStatus",
		"gbssWasIndexed",
		"gbssAgeInIndex",
		"gbssPrevTotalNumIndexSuccesses",
		"gbssPrevTotalNumIndexFailures",
		"gbssDownloadStartTimeMS",
		"gbssDownloadEndTimeMS",
		"gbssDownloadDurationMS",
		"gbssIpLookupTimeMS",
		"gbssSiteNumInlinks",
		"gbssSiteRank",
		"gbssLanguage",
		"gbssDiffbotReplyCode",
		"gbssDiffbotLen",
		"gbssDiffbotReplyResponseTimeMS",
		"gbssDiffbotReplyRetries",
		NULL };

	for ( int32_t i = 0 ; supps[i] ; i++ ) {
		// don't add these column headers to non spider status docs
		if ( ct != CT_STATUS ) break;
		char *skip = supps[i];
		// if custom crawl only show fields in supps with digits
		if ( cr->m_isCustomCrawl && ! is_digit(skip[0]) ) continue;
		// skip over the two order digits
		if ( is_digit(skip[0]) ) skip += 2;
		// don't include the order digits in the hash
		int64_t h64 = hash64n ( skip );
		if ( nameTable.isInTable ( &h64 ) ) continue;
		// only show diffbot column headers for custom (diffbot) crawls
		if ( strncmp(skip,"gbssDiffbot",11) == 0 &&
		     ( ! cr || ! cr->m_isCustomCrawl ) )
			break;
		// record offset of the name for our hash table
		int32_t nameBufOffset = nameBuf->length();
		// store the name in our name buffer
		if ( ! nameBuf->safeStrcpy (supps[i])) return false;
		if ( ! nameBuf->pushChar ( '\0' ) ) return false;
		// it's new. add it
		if ( ! nameTable.addKey ( &h64 ,&nameBufOffset)) return false;
	}
	
	// . scan every fucking json item in the search results.
	// . we still need to deal with the case when there are so many
	//   search results we have to dump each msg20 reply to disk in
	//   order. then we'll have to update this code to scan that file.

	for ( int32_t i = 0 ; i < numMsg20s ; i++ ) { // numResults

		// if custom crawl urls.csv only show the supps[] from above
		if ( ct == CT_STATUS && cr->m_isCustomCrawl )
			break;

		// get the msg20 reply for search result #i
		//Msg20      *m20 = msg40->m_msg20[i];
		//Msg20Reply *mr  = m20->m_r;
		Msg20Reply *mr  = msg20s[i]->m_r;

		if ( ! mr ) {
			log("results: missing msg20 reply for result #%"INT32"",i);
			continue;
		}

		// get content
		char *json = mr->ptr_content;
		// how can it be empty?
		if ( ! json ) continue;

		// parse it up
		Json jp;
		jp.parseJsonStringIntoJsonItems ( json , niceness );

		// scan each json item
		for ( JsonItem *ji = jp.getFirstItem(); ji ; ji = ji->m_next ){

			// skip if not number or string
			if ( ji->m_type != JT_NUMBER && 
			     ji->m_type != JT_STRING )
				continue;

			// if in an array, do not print! csv is not
			// good for arrays... like "media":[....] . that
			// one might be ok, but if the elements in the
			// array are not simple types, like, if they are
			// unflat json objects then it is not well suited
			// for csv.
			if ( ji->isInArray() ) continue;


			// skip "html" field... too spammy for csv and > 32k 
			// causes libreoffice calc to truncate it and break 
			// its parsing
			if ( ji->m_name && 
			     //! ji->m_parent &&
			     strcmp(ji->m_name,"html")==0)
				continue;

			// for spider status docs skip these
			if ( ct == CT_STATUS && ji->m_name ) {
				if (!strcmp(ji->m_name,"") )
					continue;
			}


			// reset length of buf to 0
			tmpBuf.reset();

			// . get the name of the item into "nameBuf"
			// . returns false with g_errno set on error
			if ( ! ji->getCompoundName ( tmpBuf ) )
				return false;

			// is it new?
			int64_t h64 = hash64n ( tmpBuf.getBufStart() );
			if ( nameTable.isInTable ( &h64 ) ) continue;

			// record offset of the name for our hash table
			int32_t nameBufOffset = nameBuf->length();
			
			// store the name in our name buffer
			if ( ! nameBuf->safeStrcpy ( tmpBuf.getBufStart() ) )
				return false;
			if ( ! nameBuf->pushChar ( '\0' ) )
				return false;

			// it's new. add it
			if ( ! nameTable.addKey ( &h64 , &nameBufOffset ) )
				return false;
		}
	}

	// . make array of ptrs to the names so we can sort them
	// . try to always put title first regardless
	char *ptrs [ 1024 ];
	int32_t numPtrs = 0;
	for ( int32_t i = 0 ; i < nameTable.m_numSlots ; i++ ) {
		if ( ! nameTable.m_flags[i] ) continue;
		int32_t off = *(int32_t *)nameTable.getValueFromSlot(i);
		char *p = nameBuf->getBufStart() + off;
		ptrs[numPtrs++] = p;
		if ( numPtrs >= 1024 ) break;
	}

	// pass back to caller
	*numPtrsArg = numPtrs;

	// sort them
	qsort ( ptrs , numPtrs , sizeof(char *) , csvPtrCmp );

	// set up table to map field name to column for printing the json items
	//HashTableX *columnTable = &st->m_columnTable;
	if ( ! columnTable->set ( 8,4, numPtrs * 4,NULL,0,false,0,"coltbl" ) )
		return false;

	// now print them out as the header row
	for ( int32_t i = 0 ; i < numPtrs ; i++ ) {

		char *hdr = ptrs[i];

		if ( i > 0 && ! sb->pushChar(',') ) return false;

		// skip the two order digits
		if ( ct == CT_STATUS && is_digit(hdr[0]) ) hdr += 2;

		// save it
		char *skip = hdr;

		// now transform the hdr from gbss* into the old way
		if ( ! cr->m_isCustomCrawl )
			goto skipTransform;

		if ( ! strcmp(hdr,"gbssUrl") ) 
			hdr = "Url";
		if ( ! strcmp(hdr,"gbssDocId") ) 
			hdr = "Doc ID";
		// when url was first discovered
		if ( ! strcmp(hdr,"gbssDiscoveredTime") ) // need this!
			hdr = "Url Discovered Time";
		// when it was crawled this time
		if ( ! strcmp(hdr,"gbssSpiderTime" ) )
			hdr = "Crawled Time";
		if ( ! strcmp(hdr,"gbssContentLen") ) 
			hdr = "Content Length";
		if ( ! strcmp(hdr,"gbssDupOfDocId") ) 
			hdr = "Duplicate Of";
		if ( ! strcmp(hdr,"gbssNumRedirects") ) 
			hdr = "Redirects";
		if ( ! strcmp(hdr,"gbssFinalRedirectUrl") )
			hdr = "Redirected To";
		if ( ! strcmp(hdr,"gbssCrawlDelayMS") ) 
			hdr = "Robots.txt Crawl Delay (ms)";
		if ( ! strcmp(hdr,"gbssPercentContentChanged") )
			hdr = "Percent Changed";
		if ( ! strcmp(hdr,"gbssCrawlRound") ) 
			hdr = "Crawl Round";
		if ( ! strcmp(hdr,"gbssPrevTotalNumIndexAttempts") )
			hdr = "Crawl Try #";
		if ( ! strcmp(hdr,"gbssHopCount") ) 
			hdr = "Hop Count";
		if ( ! strcmp(hdr,"gbssIp") ) 
			hdr = "IP";
		// csv report is regular urls not diffbot object urls so
		// regular urls do not have a just a single diffboturi,
		// they could have 0 or multiple diffboturis
		//if ( ! strcmp(hdr,"gbssDiffbotUri" ) )
		//	hdr = "Diffbot URI";
		if ( ! strcmp(hdr,"gbssSentToDiffbotThisTime") ) 
			hdr = "Process Attempted";
		if ( ! strcmp(hdr,"gbssDiffbotReplyMsg") )
			hdr = "Process Response";
		if ( ! strcmp(hdr,"gbssStatusMsg") ) 
			hdr = "Crawl Status";

		//if ( ! strcmp(hdr,"gbssMatchingUrlFilter") ) 
		//	hdr = "Matching Expression";
		// value is 'url ignored', 'will spider next round', 'error' or 
		// a numeric priority
		// if ( ! strcmp(hdr,"gbssSpiderPriority") ) 
		// 	hdr = "Matching Action";

		// new columns
		// if ( ! strcmp(hdr,"gbssAgeInIndex") ) 
		// 	hdr = "Age in Index";

		// if not transformed, then do not print it out
		if ( ! strncmp(hdr,"gbss",4) )
			continue;

	skipTransform:
		if ( ! sb->safeStrcpy ( hdr ) ) return false;

		// record the hash of each one for printing out further json
		// objects in the same order so columns are aligned!
		int64_t h64 = hash64n ( skip ); // ptrs[i] );
		if ( ! columnTable->addKey ( &h64 , &i ) ) 
			return false;
	}

	return true;
}

// 
// print header row in csv
//
bool printCSVHeaderRow ( SafeBuf *sb , State0 *st , int32_t ct ) {

	Msg40 *msg40 = &st->m_msg40;
 	int32_t numResults = msg40->getNumResults();

	char tmp2[1024];
	SafeBuf nameBuf (tmp2, 1024);

	CollectionRec *cr = g_collectiondb.getRec ( st->m_collnum );

	int32_t numPtrs = 0;

	printCSVHeaderRow2 ( sb , 
			     ct ,
			     cr ,
			     &nameBuf ,
			     &st->m_columnTable ,
			     msg40->m_msg20 ,
			     numResults ,
			     &numPtrs 
			     );

	st->m_numCSVColumns = numPtrs;

	if ( ! sb->pushChar('\n') )
		return false;
	if ( ! sb->nullTerm() )
		return false;

	return true;
}

// returns false and sets g_errno on error
bool printJsonItemInCSV ( char *json , SafeBuf *sb , State0 *st ) {

	CollectionRec *cr = g_collectiondb.getRec ( st->m_collnum );

	int32_t niceness = 0;

	// parse the json
	Json jp;
	jp.parseJsonStringIntoJsonItems ( json , niceness );

	HashTableX *columnTable = &st->m_columnTable;
	int32_t numCSVColumns = st->m_numCSVColumns;

	
	// make buffer space that we need
	char ttt[1024];
	SafeBuf ptrBuf(ttt,1024);
	int32_t need = numCSVColumns * sizeof(JsonItem *);
	if ( ! ptrBuf.reserve ( need ) ) return false;
	JsonItem **ptrs = (JsonItem **)ptrBuf.getBufStart();

	// reset json item ptrs for csv columns. all to NULL
	memset ( ptrs , 0 , need );

	char tmp1[1024];
	SafeBuf tmpBuf (tmp1 , 1024);

	JsonItem *ji;

	///////
	//
	// print json item in csv
	//
	///////
	for ( ji = jp.getFirstItem(); ji ; ji = ji->m_next ) {

		// skip if not number or string
		if ( ji->m_type != JT_NUMBER && 
		     ji->m_type != JT_STRING )
			continue;

		// skip if not well suited for csv (see above comment)
		if ( ji->isInArray() ) continue;

		// . get the name of the item into "nameBuf"
		// . returns false with g_errno set on error
		if ( ! ji->getCompoundName ( tmpBuf ) )
			return false;

		// skip "html" field... too spammy for csv and > 32k causes
		// libreoffice calc to truncate it and break its parsing
		if ( ji->m_name && 
		     //! ji->m_parent &&
		     strcmp(ji->m_name,"html")==0)
			continue;

		// is it new?
		int64_t h64 = hash64n ( tmpBuf.getBufStart() );

		int32_t slot = columnTable->getSlot ( &h64 ) ;
		// MUST be in there
		if ( slot < 0 ) { 
			// we do not transform all gbss fields any more for
			// diffbot to avoid overpopulating the csv
			if ( cr && cr->m_isCustomCrawl ) continue;
			// do not core on this anymore...
			log("serps: json column not in table : %s",ji->m_name);
			continue;
			//char *xx=NULL;*xx=0;}
		}

		// get col #
		int32_t column = *(int32_t *)columnTable->getValueFromSlot ( slot );

		// sanity
		if ( column >= numCSVColumns ) { char *xx=NULL;*xx=0; }

		// set ptr to it for printing when done parsing every field
		// for this json item
		ptrs[column] = ji;
	}

	// now print out what we got
	for ( int32_t i = 0 ; i < numCSVColumns ; i++ ) {

		// get it
		ji = ptrs[i];

		// skip "html" field... too spammy for csv and > 32k causes
		// libreoffice calc to truncate it and break its parsing
		if ( ji &&
		     ji->m_name && 
		     //! ji->m_parent &&
		     strcmp(ji->m_name,"html")==0)
			continue;

		// , delimeted
		if ( i > 0 ) sb->pushChar(',');

		// skip if none
		if ( ! ji ) continue;


		//
		// get value and print otherwise
		//
		/*
		if ( ji->m_type == JT_NUMBER ) {
			// print numbers without double quotes
			if ( ji->m_valueDouble *10000000.0 == 
			     (double)ji->m_valueLong * 10000000.0 )
				sb->safePrintf("%"INT32"",ji->m_valueLong);
			else
				sb->safePrintf("%f",ji->m_valueDouble);
			continue;
		}
		*/

		int32_t vlen;
		char *str = ji->getValueAsString ( &vlen );

		// print the value
		sb->pushChar('\"');
		// get the json item to print out
		//int32_t  vlen = ji->getValueLen();
		// truncate
		char *truncStr = NULL;
		if ( vlen > 32000 ) {
			vlen = 32000;
			truncStr = " ... value truncated because "
				"Excel can not handle it. Download the "
				"JSON to get untruncated data.";
		}
		// print it out
		sb->csvEncode ( str , vlen ); // ji->getValue() , vlen );
		// print truncate msg?
		if ( truncStr ) sb->safeStrcpy ( truncStr );
		// end the CSV
		sb->pushChar('\"');
	}

	sb->pushChar('\n');
	sb->nullTerm();

	return true;
}

/*

  RIP: OLD IFRAME WIDGET CODE HACK

bool printWidgetPage ( SafeBuf *sb , HttpRequest *hr , char *coll ) {
	//
	// begin print controls
	//

	sb->safePrintf("<html>"
		       "<body bgcolor=#e8e8e8>"
		       "<title>Widget Creator</title>"
		      );


	//char *coll = "GLOBAL-INDEX";
	CollectionRec *cr = NULL;
	if ( coll ) cr = g_collectiondb.getRec(coll);

	// if admin clicks "edit" in the live widget itself put up
	// some simpler content editing boxes. token required!
	int32_t edit = hr->getLong("inlineedit",0);
	if ( edit ) {
		// get widget sites
		char *sites = cr->m_siteListBuf.getBufStart();
		sb->safePrintf("<textarea>"
			       "%s"
			       "</textarea>"
			       , sites);
		sb->safePrintf("<br>"
			       "<input type=text name=token>"
			       "<br>"
			       "<input type=submit name=submit value=ok>"
			       );
		return true;
	}


	sb->safePrintf("<script>\n");

	// onclick of a checkbox toggle it here since we reload after
	sb->safePrintf("function toggleBool ( control , id ) {\n"
		      "if(document.forms[0].elements[id].value == 1 ) {\n"
		      "document.forms[0].elements[id].value = 0;\n"
		      "} else {\n"
		      "document.forms[0].elements[id].value = 1;\n"
		      "}\n"
		      "}\n"
		      );

	// construct url based on input parms
	sb->safePrintf("function getFormParms ( ) {\n"
		      "var i;\n"
		      "var url = '';\n"
		      "for(i=0; i<document.myform.elements.length; i++){\n"
		      "var elm = document.myform.elements[i];\n"
		      // skip submit button and nameless checkboxes
		      "if ( elm.name == '' ) {\n"
		      //"alert(document.myform.elements[i].value)\n"
		      "continue;\n"
		      "}\n"
		      // until we had def=%"INT32" to each input parm assume
		      // default is 0. i guess if it has no def= attribute
		      // assume default is 0
		      //"if ( elm.value == '0' ) {\n"
		      //"continue;\n"
		      //"}\n"
		      "if ( elm.value == '' ) {\n"
		      "continue;\n"
		      "}\n"
		      "url = "
		      "url + "
		      "elm.name + \"=\" + "
		      "elm.value + \"&\" ;\n"
		      "}\n"
		      "return url;\n"
		      "}\n"
		      );

	sb->safePrintf("function reload() {\n"
		      "var url='/widget?' + getFormParms();\n"
		      "window.location.href=url;\n"
		      "}\n"
		      );


	sb->safePrintf("</script>\n");

	char *c1 = "";
	char *c2 = "";
	char *c3 = "";
	int32_t x1 = hr->getLong("dates"    ,0);
	int32_t x2 = hr->getLong("summaries",0);
	int32_t x3 = hr->getLong("border"   ,0);
	if ( x1 ) c1 = " checked";
	if ( x2 ) c2 = " checked";
	if ( x3 ) c3 = " checked";
	int32_t width  = hr->getLong("width",250);
	int32_t height = hr->getLong("height",400);
	int32_t refresh = hr->getLong("refresh",15);
	char *def = "<style>html {font-size:12px;font-family:arial;background-color:transparent;color:black;}span.title { font-size:16px;font-weight:bold;}span.summary { font-size:12px;} span.date { font-size:12px;}span.prevnext { font-size:12px;font-weight:bold;}</style>";//<h2>News</h2>";
	int32_t len1,len2,len3,len4;
	char *header = hr->getString("header",&len1,def);
	char *sites = hr->getString("sites",&len2,"");
	char *token = hr->getString("token",&len3,"");
	//char*query=hr->getString("query",&len4,
	//"type:article gbsortbyint:date");
	char *query =hr->getString("query",&len4,
				   "type:article gbsortbyint:gbspiderdate");

	sb->safePrintf("<form method=GET action=/widget>"
		       "<input type=hidden name=c value=\"%s\">"
		       "<input type=hidden name=format value=\"widget\">"
		       , coll
		       );


	sb->safePrintf(

		      "<div style=\""
		      "margin-left:5px;"
		      "padding:15px;"
		      "width:600px;"
		      "height:600px;"
		      "font-family:Arial;"
		      "border-radius:10px;"
		      "line-height:30px;"
		      "background-color:lightgray;"
		      "text-align:right;"
		      "\""
		      ">"
		      

		      "<table cellpadding=0>"
		      "<tr>"
		      "<td "
		      "style=padding:15px;background-color:lightblue;"
		      //"text-align:right;"
		      "bottom-margin:5px; "
		      "colspan=10>"

		      "<img align=right height=50 width=52 "
		      "src=http://www.diffbot.com/img/diffy-b.png>"

		      "<b style=font-size:22px;><font style=font-size:27px;>"
		      "W</font>"
		      "idget <font style=font-size:27px;>C</font>reator</b>"
		      "<br>"
		      "<font style=font-size:12px;>"
		      "<i>"
		      "Harness the power of Diffbot."
		      "</i>"
		      "</font>"

		      "</td>"
		      "</tr>"

		      "<tr>"
		      "<td style=text-align:right;line-height:30px;>"

		      "Websites to crawl:"
		      "<br>"
		      "<textarea rows=10 name=sites style=width:100%%;>"
		      "%s"
		      "</textarea>"
		      "<br>"

		      "Token:"
		      "<br>"
		      "<textarea name=token style=width:100%%;>"
		      "%s"
		      "</textarea>"
		      "<br>"

		      "Query:"
		      "<br>"
		      "<textarea rows=4 name=query style=width:100%%;>"
		      "%s"
		      "</textarea>"
		      "<br>"


		      "Show Dates "
		      "<input type=checkbox value=1 "
		      //"onclick=\"toggleBool(this,'dates');reload();\" "
		      "name=dates%s>"
		      "<br>"

		      "Show Summaries "
		      "<input type=checkbox value=1 "
		      //"onclick=\"toggleBool(this,'summaries');reload();\" "
		      "name=summaries%s>"
		      "<br>"

		      "Frame border "
		      "<input type=checkbox value=1 "
		      //"onclick=\"toggleBool(this,'border');reload();\" "
		      "name=border%s>"
		      "<br>"

		      "Width "
		      "<input size=4 type=text value=%"INT32" "
		      "name=width>"
		      "<br>"

		      "Height "
		      "<input size=4 type=text value=%"INT32" "
		      "name=height>"
		      "<br>"

		      "<nobr>Refresh in seconds "
		      "<input size=4 type=text value=%"INT32" "
		      "name=refresh></nobr>"
		      "<br>"

		      "<nobr>Custom widget header:</nobr>"
		      "<br>"
		      "<textarea rows=10 name=header style=width:100%%;>"
		      "%s"
		      "</textarea>"
		      "<br>"

		      "<input type=submit name=submit value=ok>"
		      
		      "</div>"

		      "</td>"

		      , sites
		      , token
		      , query
		      , c1
		      , c2
		      , c3
		      , width
		      , height
		      , refresh
		      , header
		      );


	//
	// end print controls
	//


	//
	// begin print widget
	//

	sb->safePrintf ( "<td>"
			 "<div style=\""
			 "width:30px;"//%"INT32"px;"
			 //"position:absolute;"
			 //"top:300px;"
			 //"right:0;"
			 //"left:0;"
			 //"bottom:0;"
			 "\">"
			 "<div style=line-height:13px;><br></div>"
			 //"<br>"
			 //, RESULTSWIDTHSTR 
			 //,width
			 );

	//printTabs ( sb , st );
	//printRedBoxes ( sb , st );

#define SHADOWCOLOR "#000000"

	sb->safePrintf ( 
			// end widget div
			"</div>"
			// end widget column in table
			"</td>"
			"<td>"
			// begin div with source in it
			//  "<div "
			// //"class=grad3 "
			// "style=\""
			// "border-radius:10px;"
			// "box-shadow: 6px 6px 3px %s;"
			// "border:2px solid black;"
			// "padding:15px;"
			//  "width:600px;"
			// //"background-image:url('/ss.jpg');"
			// //"background-repeat:repeat;"
			// //"background-attachment:fixed;"
			//  "background-color:lightgray;"
			// "\">"
			// , SHADOWCOLOR
			// //"<br>"
			);

	// space widget to the right using this table
	sb->safePrintf(
		      //class=grad3 "
		      //"style=\""
		      //"border:2px solid black;"
		      //"padding-bottom:10px;"
		      //"padding-top:10px;"
		      //"padding-left:10px;"
		      //"\""
		      //">"
		       "</td>"
		      "<td valign=top>"
		       "<br>"
		      "<img src=/gears32.png width=64 height=64>"
		      "<br><br>"
		      );


	int32_t start = sb->length();

	char *border = "frameborder=no ";
	if ( x3 ) border = "";

	// this iframe contains the WIDGET
	sb->safePrintf (
		       // "<div "
		       // "id=scrollerxyz "
		       // "style=\""
		       //"width:%"INT32"px;" // 200;"
		       //"height:%"INT32"px;" // 400;"
		       //"overflow:hidden;"
		       // "padding:0px;"
		       // "margin:0px;"
		       // "background-color:white;"
		       //"padding-left:7px;"
		       //"%s"
		       //"background-color:%s;"//lightblue;"
		       //"foreground-color:%s;"
		       //"overflow:scroll;"
		       //"overflow-scrolling:touch;"
		       "\">"

			"<iframe width=\"%"INT32"px\" height=\"%"INT32"px\" "
			//"scrolling=yes "

			//"style=\"background-color:white;"
			//"padding-right:0px;"
			//"%s\" "
			//"scrolling=no "
			//"frameborder=no "
			//"src=\"http://neo.diffbot.com:8000/search?"

			// frameborder=no
			"%s"

			"src=\""
			//"http://127.0.0.1:8000/search?"
			"http://%s:%"INT32"/search?"
			"format=widget&"
			"widgetwidth=%"INT32"&widgetheight=%"INT32"&"
			"c=%s&"
			"refresh=%"INT32""
			// show articles sorted by newest pubdate first

			, width
			, height
			, border

			, iptoa(g_hostdb.m_myHost->m_ip)
			, (int32_t)g_hostdb.m_myHost->m_httpPort

			, width
			, height
			, coll
			, refresh
			);

	sb->safePrintf("&dates=%"INT32"",x1);
	sb->safePrintf("&summaries=%"INT32"",x2);

	sb->safePrintf("&q=");
	sb->urlEncode ( query );

	// widget content header, usually a style tag
	sb->safePrintf("&header=");
	sb->urlEncode ( header );



	sb->safePrintf("\">");

	sb->safePrintf ( // do not reset the user's "where" cookie
			// to NYC from looking at this widget!
			//"cookie=0&"
			//"%s"
			"Your browser does not support iframes"
			"</iframe>\n"
			//"</div>" 
			//, si->m_urlParms);
			//, wp 
			);

	int32_t end = sb->length();

	sb->reserve ( end - start + 1000 );

	char *wdir = "on the left";
	int32_t cols = 32;

	//if ( width <= 240 ) 
		sb->safePrintf("</td><td>&nbsp;&nbsp;</td><td valign=top>");
		//else {
		//	sb->safePrintf("</td></tr><tr><td><br><br>");
		//	wdir = "above";
		//		cols = 60;
		//	}

	sb->safePrintf ( "\n\n"
			 "<br>"
			//"<br><br><br>"
			"<font style=\"font-size:16px;\">"
			"Insert the following code into your webpage to "
			"generate the widget %s. "
			//"<br>"
			//"<b><u>"
			//"<a style=color:white href=/widget.html>"
			//"Make $1 per click!</a></u></b>"
			//"</font>"
			"<br><br><b>" , wdir );
	
	char *p = sb->getBufStart() + start;

	sb->safePrintf("<textarea rows=30 cols=%"INT32" "
		      "style=\"border:2px solid black;\">", cols);
	sb->htmlEncode ( p           ,
			end - start ,
			false       ,  // bool encodePoundSign
			0           ); // niceness
	sb->safePrintf("</textarea>");

	sb->safePrintf("</b>");

	// space widget to the right using this table
	sb->safePrintf("</td></tr></table>");

	sb->safePrintf("</div>");

	sb->safePrintf("</form>");

	sb->safePrintf("</body>");

	sb->safePrintf("</html>");

	return true;
}

bool sendPageWidget ( TcpSocket *s , HttpRequest *hr ) {
	SafeBuf sb;

	char *token = hr->getString("token",NULL);
	if ( token && ! token[0] ) token = NULL;

	int32_t edit = hr->getLong("inlineedit",0);

	if ( ! token && ! edit ) {
		g_errno = ENOTOKEN;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(s,g_errno,msg);
	}

	int32_t tlen = 0;
	if ( token ) tlen = gbstrlen(token);
	if ( tlen > 64 ) { 
		g_errno = ENOCOLLREC;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(s,g_errno,msg);
	}

	char coll[MAX_COLL_LEN];
	CollectionRec *cr = NULL;
	if ( token ) {
		sprintf(coll,"%s-widget123",token);
		cr = g_collectiondb.getRec(coll);
	}

	SafeBuf parmList;

	collnum_t cn = -1;
	if ( cr ) cn = cr->m_collnum;

	// . first update their collection with the sites to crawl
	// . this is NOT a custom diffbot crawl, just a regular one using
	//   the new crawl filters logic, "siteList"
	char *sites = hr->getString("sites",NULL);
	// add the collection if does not exist
	if ( sites && ! cr && token ) {
		// we need to add the new collnum, so reserve it
		collnum_t newCollnum = g_collectiondb.reserveCollNum();
		// use that
		cn = newCollnum;
		// add the new colection named <token>-widget123
		g_parms.addNewParmToList1 ( &parmList,cn,coll,0,"addColl");
		// note it
		log("widget: adding new widget coll %s",coll);
	}


	if ( cn >= 0 && token ) {
		// use special url filters profile that spiders sites
		// shallowly and frequently to pick up new news stories
		// "1" = (int32_t)UFP_NEWS
		char ttt[12];
		sprintf(ttt,"%"INT32"",(int32_t)UFP_NEWS);
		// urlfiltersprofile
		g_parms.addNewParmToList1 ( &parmList,cn,ttt,0,"ufp");
		// use diffbot analyze
		char durl[1024];
		sprintf(durl,
			"http://api.diffbot.com/v2/analyze?mode=auto&token=%s",
			token);
		// TODO: ensure we call diffbot ok
		g_parms.addNewParmToList1 ( &parmList,cn,durl,0,"apiUrl");
	}

	if ( ! sites ) sites = "";

	// . update the list of sites to crawl and search and show in widget
	// . if they give an empty list then allow that, it will stop crawling
	if ( cn >= 0 && token )
		g_parms.addNewParmToList1 ( &parmList,cn,sites,0,"sitelist");


	if ( parmList.length() ) {
		// send the parms to all hosts in the network
		g_parms.broadcastParmList ( &parmList , 
					    NULL,//s,// state is socket i guess
					    NULL);//doneBroadcastingParms2 );
	}



	// now display the widget controls and the widget and the iframe code
	printWidgetPage ( &sb , hr , coll );

	return g_httpServer.sendDynamicPage(s,
					    sb.getBufStart(),
					    sb.length(),
					    -1,//cacheTime -1 means not tocache
					    false, // POST?
					    "text/html", 
					    200,  // httpstatus
					    NULL, // cookie
					    "UTF-8"); // charset
}
*/


bool printDmozEntry ( SafeBuf *sb ,
		      int32_t catId ,
		      bool direct ,
		      char *dmozTitle ,
		      char *dmozSummary ,
		      char *dmozAnchor ,
		      SearchInput *si ) {

	// assign shit if we match the dmoz cat we are showing
	//if ( catIds[i] ==  si->m_catId) break;
	if ( si->m_format == FORMAT_XML ) {
		sb->safePrintf("\t\t<dmozEntry>\n");
		sb->safePrintf("\t\t\t<dmozCatId>%"INT32""
			       "</dmozCatId>\n",catId);
		sb->safePrintf("\t\t\t<directCatId>%"INT32"</directCatId>\n",
			       (int32_t)direct);
		// print the name of the dmoz category
		sb->safePrintf("\t\t\t<dmozCatStr><![CDATA[");
		char xbuf[256];
		SafeBuf xb(xbuf,256,0,false);
		g_categories->printPathFromId(&xb, 
					      catId,
					      false,
					      si->m_isRTL); 
		sb->cdataEncode(xb.getBufStart());
		sb->safePrintf("]]></dmozCatStr>\n");
		sb->safePrintf("\t\t\t<dmozTitle><![CDATA[");
		sb->cdataEncode(dmozTitle);
		sb->safePrintf("]]></dmozTitle>\n");
		sb->safePrintf("\t\t\t<dmozSum><![CDATA[");
		sb->cdataEncode(dmozSummary);
		sb->safePrintf("]]></dmozSum>\n");
		sb->safePrintf("\t\t\t<dmozAnchor><![CDATA[");
		sb->cdataEncode(dmozAnchor);
		sb->safePrintf("]]></dmozAnchor>\n");
		sb->safePrintf("\t\t</dmozEntry>\n");
		return true;
	}
	if ( si->m_format == FORMAT_JSON ) {
		sb->safePrintf("\t\t\"dmozEntry\":{\n");
		sb->safePrintf("\t\t\t\"dmozCatId\":%"INT32",\n",
			       catId);
		sb->safePrintf("\t\t\t\"directCatId\":%"INT32",\n",(int32_t)direct);
		// print the name of the dmoz category
		sb->safePrintf("\t\t\t\"dmozCatStr\":\"");
		char xbuf[256];
		SafeBuf xb(xbuf,256,0,false);
		g_categories->printPathFromId(&xb, 
					      catId,
					      false,
					      si->m_isRTL); 
		sb->jsonEncode(xb.getBufStart());
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\t\t\"dmozTitle\":\"");
		sb->jsonEncode(dmozTitle);
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\t\t\"dmozSum\":\"");
		sb->jsonEncode(dmozSummary);
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\t\t\"dmozAnchor\":\"");
		sb->jsonEncode(dmozAnchor);
		sb->safePrintf("\"\n");
		sb->safePrintf("\t\t},\n");
		return true;
	}
	return true;
}

class MenuItem {
public:
	int32_t  m_menuNum;
	char *m_title;
	// we append this to the url
	char *m_cgi;
	char  m_tmp[25];
	char *m_icon; // for languages - the language flag
	char  m_iconWidth;
	char  m_iconHeight;
};

static MenuItem s_mi[200];
static int32_t s_num = 0;

bool printSearchFiltersBar ( SafeBuf *sb , HttpRequest *hr ) {

	// 1-1 with the langs in Lang.h
	char *g_flagBytes[] = {
		// base64 encoding
		NULL, // langunknown
		// english
		
	
	};

	/*
	langUnknown     = 0,
	langEnglish     = 1,
	langFrench      = 2,
	langSpanish     = 3,
	langRussian     = 4,
	langTurkish     = 5,
	langJapanese    = 6,
	langChineseTrad = 7, // cantonese
	langChineseSimp = 8, // mandarin
	langKorean      = 9,
	langGerman      = 10,
	langDutch       = 11,
	langItalian     = 12,
	langFinnish     = 13,
	langSwedish     = 14,
	langNorwegian   = 15,
	langPortuguese  = 16,
	langVietnamese  = 17,
	langArabic      = 18,
	langHebrew      = 19,
	langIndonesian  = 20,
	langGreek       = 21,
	langThai        = 22,
	langHindi       = 23,
	langBengala     = 24,
	langPolish      = 25,
	langTagalog     = 26,

	// added for wiktionary
	langLatin          = 27,
	langEsperanto      = 28,
	langCatalan        = 29,
	langBulgarian      = 30,
	langTranslingual   = 31, // used by multiple langs in wiktionary
	langSerboCroatian  = 32,
	langHungarian      = 33,
	langDanish         = 34,
	langLithuanian     = 35,
	langCzech          = 36,
	langGalician       = 37,
	langGeorgian       = 38,
	langScottishGaelic = 39,
	langGothic         = 40,
	langRomanian       = 41,
	langIrish          = 42,
	langLatvian        = 43,
	langArmenian       = 44,
	langIcelandic      = 45,
	langAncientGreek   = 46,
	langManx           = 47,
	langIdo            = 48,
	langPersian        = 49,
	langTelugu         = 50,
	langVenetian       = 51,
	langMalgasy        = 52,
	langKurdish        = 53,
	langLuxembourgish  = 54,
	langEstonian       = 55,
	langLast           = 56
};
	*/


	SafeBuf cu;
	hr->getCurrentUrl ( cu );


	sb->safePrintf("<script>"
		       "function show(id){"
		       "var e = document.getElementById(id);"
		       "if ( e.style.display == 'none' ){"
		       "e.style.display = '';"
		       "}"
		       "else {"
		       "e.style.display = 'none';"
		       "}"
		       "}"
		       "</script>"
		       );


	static bool s_init = false;

	if ( ! s_init ) {

		int32_t n = 0;

		s_mi[n].m_menuNum  = 0;
		s_mi[n].m_title    = "Any time";
		s_mi[n].m_cgi      = "secsback=0";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 0;
		s_mi[n].m_title    = "Past 24 hours";
		s_mi[n].m_cgi      = "secsback=86400";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 0;
		s_mi[n].m_title    = "Past week";
		s_mi[n].m_cgi      = "secsback=604800";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 0;
		s_mi[n].m_title    = "Past month";
		s_mi[n].m_cgi      = "secsback=2592000";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 0;
		s_mi[n].m_title    = "Past year";
		s_mi[n].m_cgi      = "secsback=31536000";
		n++;
		s_mi[n].m_icon     = NULL;

		// sort by

		s_mi[n].m_menuNum  = 1;
		s_mi[n].m_title    = "Sorted by relevance";
		s_mi[n].m_cgi      = "sortby=0";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 1;
		s_mi[n].m_title    = "Sorted by date";
		s_mi[n].m_cgi      = "sortby=1";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 1;
		s_mi[n].m_title    = "Reverse sorted by date";
		s_mi[n].m_cgi      = "sortby=2";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 1;
		s_mi[n].m_title    = "Sorted by site inlinks";
		s_mi[n].m_cgi      = "sortby=3";
		s_mi[n].m_icon     = NULL;
		n++;


		// languages

		s_mi[n].m_menuNum  = 2;
		s_mi[n].m_title    = "Any language";
		s_mi[n].m_cgi      = "qlang=xx";
		s_mi[n].m_icon     = NULL;
		n++;

		for ( int32_t i = 0 ; i < langLast ; i++ ) {
			s_mi[n].m_menuNum  = 2;
			s_mi[n].m_title    = getLanguageString(i);
			char *abbr = getLangAbbr(i);
			snprintf(s_mi[n].m_tmp,10,"qlang=%s",abbr);
			s_mi[n].m_cgi      = s_mi[n].m_tmp;
			s_mi[n].m_icon     = g_flagBytes[i]; //base64encoded
			n++;
		}

		// filetypes

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "Any filetype";
		s_mi[n].m_cgi      = "filetype=any";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "HTML";
		s_mi[n].m_cgi      = "filetype=html";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "TEXT";
		s_mi[n].m_cgi      = "filetype=txt";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "PDF";
		s_mi[n].m_cgi      = "filetype=pdf";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "Microsoft Word";
		s_mi[n].m_cgi      = "filetype=doc";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "XML";
		s_mi[n].m_cgi      = "filetype=xml";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "JSON";
		s_mi[n].m_cgi      = "filetype=json";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "Excel";
		s_mi[n].m_cgi      = "filetype=xls";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "PostScript";
		s_mi[n].m_cgi      = "filetype=ps";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 3;
		s_mi[n].m_title    = "Spider Status";
		s_mi[n].m_cgi      = "filetype=status";
		s_mi[n].m_icon     = NULL;
		n++;

		// facets

		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "No Facets";
		s_mi[n].m_cgi      = "facet=";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "Language facet";
		s_mi[n].m_cgi      = "facet=gbfacetint:gblang";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "Content type facet";
		s_mi[n].m_cgi      = "facet=gbfacetstr:type";
		s_mi[n].m_icon     = NULL;
		n++;

		// s_mi[n].m_menuNum  = 4;
		// s_mi[n].m_title    = "Ip address";
		// s_mi[n].m_cgi      = "facet=gbfacetstr:ip";
		// n++;

		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "Url path depth";
		s_mi[n].m_cgi      = "facet=gbfacetint:gbpathdepth";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "Spider date facet";
		s_mi[n].m_cgi      = "facet=gbfacetint:gbspiderdate";
		s_mi[n].m_icon     = NULL;
		n++;

		// everything in tagdb is hashed
		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "Site num inlinks facet";
		s_mi[n].m_cgi      = "facet=gbfacetint:gbtagsitenuminlinks";
		s_mi[n].m_icon     = NULL;
		n++;

		// s_mi[n].m_menuNum  = 4;
		// s_mi[n].m_title    = "Domains facet";
		// s_mi[n].m_cgi      = "facet=gbfacetint:gbdomhash";
		// n++;

		s_mi[n].m_menuNum  = 4;
		s_mi[n].m_title    = "Hopcount facet";
		s_mi[n].m_cgi      = "facet=gbfacetint:gbhopcount";
		s_mi[n].m_icon     = NULL;
		n++;

		// output

		s_mi[n].m_menuNum  = 5;
		s_mi[n].m_title    = "Output HTML";
		s_mi[n].m_cgi      = "format=html";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 5;
		s_mi[n].m_title    = "Output XML";
		s_mi[n].m_cgi      = "format=xml";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 5;
		s_mi[n].m_title    = "Output JSON";
		s_mi[n].m_cgi      = "format=json";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 5;
		s_mi[n].m_title    = "Output CSV";
		s_mi[n].m_cgi      = "format=csv";
		s_mi[n].m_icon     = NULL;
		n++;

		// show/hide banned
		s_mi[n].m_menuNum  = 6;
		s_mi[n].m_title    = "Hide banned results";
		s_mi[n].m_cgi      = "sb=0";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 6;
		s_mi[n].m_title    = "Show banned results";
		s_mi[n].m_cgi      = "sb=1";
		s_mi[n].m_icon     = NULL;
		n++;


		// spider status
		s_mi[n].m_menuNum  = 7;
		s_mi[n].m_title    = "Hide Spider Log";
		s_mi[n].m_cgi      = "splog=0";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 7;
		s_mi[n].m_title    = "Show Spider Log";
		s_mi[n].m_cgi      = "q=type:status";
		s_mi[n].m_icon     = NULL;
		n++;


		// family filter
		s_mi[n].m_menuNum  = 8;
		s_mi[n].m_title    = "Family Filter Off";
		s_mi[n].m_cgi      = "ff=0";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 8;
		s_mi[n].m_title    = "Family Filter On";
		s_mi[n].m_cgi      = "ff=1";
		s_mi[n].m_icon     = NULL;
		n++;

		// META TAGS
		s_mi[n].m_menuNum  = 9;
		s_mi[n].m_title    = "No Meta Tags";
		s_mi[n].m_cgi      = "dt=";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 9;
		s_mi[n].m_title    = "Show Meta Tags";
		s_mi[n].m_cgi      = "dt=keywords+description";
		s_mi[n].m_icon     = NULL;
		n++;


		// ADMIN

		s_mi[n].m_menuNum  = 10;
		s_mi[n].m_title    = "Show Admin View";
		s_mi[n].m_cgi      = "admin=1";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 10;
		s_mi[n].m_title    = "Show User View";
		s_mi[n].m_cgi      = "admin=0";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 11;
		s_mi[n].m_title    = "Action";
		s_mi[n].m_cgi      = "";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 11;
		s_mi[n].m_title    = "Respider all results";
		s_mi[n].m_cgi      = "";//"/admin/reindex";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 11;
		s_mi[n].m_title    = "Delete all results";
		s_mi[n].m_cgi      = "";//"/admin/reindex";
		s_mi[n].m_icon     = NULL;
		n++;

		s_mi[n].m_menuNum  = 11;
		s_mi[n].m_title    = "Scrape from google/bing";
		s_mi[n].m_cgi      = "";//"/admin/inject";
		s_mi[n].m_icon     = NULL;
		n++;

		s_num = n;
		if ( n > 200 ) { char *xx=NULL;*xx=0; }
	}


	// we'll print the admin menu custom since it's mostly off-page links

	// bar of drop down menus
	sb->safePrintf("<div style=color:gray;>");

	for ( int32_t i = 0 ; i <= s_mi[s_num-1].m_menuNum ; i++ ) {
		// after 4 make a new line
		if ( i == 5 ) sb->safePrintf("<br><br>");
		if ( i == 9 ) sb->safePrintf("<br><br>");
		printMenu ( sb , i , hr );
	}

	sb->safePrintf("</div>\n");
	sb->safePrintf("<br>\n");

	return true;
}

bool printMenu ( SafeBuf *sb , int32_t menuNum , HttpRequest *hr ) {

	bool firstOne = true;

	MenuItem *first = NULL;

	char *src    = hr->m_origUrlRequest;
	int32_t  srcLen = hr->m_origUrlRequestLen;

	char *frontTag = "";
	char *backTag = "";

	bool isDefaultHeader = true;

	// try to set first based on what's in the url
	for ( int32_t i = 0 ; i < s_num ; i++ ) {
		// int16_tcut
		MenuItem *mi = &s_mi[i];
		// skip if not our item
		if ( mi->m_menuNum != menuNum ) continue;

		// admin menu is special
		// if ( menuNum == s_num - 1 ) {
		// 	first = mi;
		// 	frontTag = "<font color=green>";
		// 	backTag = "</font>";
		// 	break;
		// }

		// is it in the url
		char *match = strnstr ( src , mi->m_cgi , srcLen );

		// or if empty quotes it is the true header like
		// for 'hide spider log' option
		if ( ! match ) {
			isDefaultHeader = false;
			continue;
		}
		// ensure ? or & preceeds
		if ( match > src && match[-1] != '?' && match[-1] != '&' )
			continue;
		// and \0 or & follows
		int32_t milen = gbstrlen(mi->m_cgi);
		if ( match+milen > src+srcLen ) continue;
		if ( ! is_wspace_a(match[milen]) && match[milen] != '&' ) 
			continue;
		// got it
		first = mi;
		// do not highlight the orig header
		if ( isDefaultHeader ) break;
		frontTag = "<b style=color:maroon;>";
		backTag = "</b>";
		break;
	}
	

	for ( int32_t i = 0 ; i < s_num ; i++ ) {

		// int16_tcut
		MenuItem *mi = &s_mi[i];

		// skip if not our item
		if ( mi->m_menuNum != menuNum ) continue;

		if ( ! first ) first = mi;

		if ( ! firstOne ) goto skip;

		firstOne = false;

		// for centering the dropdown
		sb->safePrintf("<span style=position:relative;></span>");

		// print hidden drop down menu
		sb->safePrintf(
			       "<span id=menu%"INT32" style=\"display:none;"
			       "position:absolute;"
			       //"margin-left:-20px;"
			       "margin-top:15px;"
			       "width:150px;"
			       "max-height:300px;"
			       "overflow-y:auto;"
			       "background-color:white;"
			       "padding:10px;"
			       "width=80px;border-width:1px;"
			       "border-color:lightgray;"
			       "box-shadow: -.5px 1px 1px gray;"
			       "border-style:solid;color:gray;\" "

			       //" onmouseout=\""
			       //"this.style.display='none';\""

			       // if clicking on scrollbar do not hide menu!
			       " onmousedown=\"inmenuclick=1;\" "

			       ">"
			       , mi->m_menuNum
			       );

	skip:

		
		// . add our cgi to the original url
		// . so if it has &qlang=de and they select &qlang=en
		//   we have to replace it... etc.
		char tmp2[512];
		SafeBuf newUrl(tmp2, 512);
		replaceParm ( mi->m_cgi , &newUrl , hr );

		// print each item in there
		sb->safePrintf("<a href=%s>"
			       "<div style=cursor:pointer;cursor:hand;"
			       "padding-top:10px;"
			       "padding-bottom:10px;"
			       "color:gray;"

			       " onmouseover=\""
			       "this.style.backgroundColor='#e0e0e0';\" "
			       " onmouseout=\""
			       "this.style.backgroundColor='white';\" "

			       // prevent the body onmousedown from 
			       // hiding the menu
			       " onmousedown=\"inmenuclick=1;\" "

			       ">"
			       "<nobr>"
			       , newUrl.getBufStart()
			       );

		// print checkmark (check mark) next to selected one
		// if not the default (trueHeader)
		if ( mi == first ) // ! isDefaultHeader && mi == first )
			sb->safePrintf("<b style=color:black;>%c%c%c</b>",
				       0xe2,0x9c,0x93);
		else 
			sb->safePrintf("&nbsp; &nbsp; ");

		sb->safePrintf(" %s</nobr>"
			       "</div>"
			       "</a>"
			       , mi->m_title );

		//sb->safePrintf("<br><br>");
	}

	// wrap up the drop down
	sb->safePrintf("</span>");

	// print heading or current selection i guess
	sb->safePrintf(
		       // separate menus with these two spaces
		       " &nbsp; &nbsp; "
		       // print the menu header that when clicked
		       // will show the drop down
		       "<span style=cursor:pointer;"
		       "cursor:hand; "

		       "onmousedown=\"this.style.color='red';"
		       "inmenuclick=1;"
		       "\" "

		       "onmouseup=\"this.style.color='gray';"

		       // close any other open menu
		       "if ( openmenu !='') {"
		       "document.getElementById(openmenu)."
		       "style.display='none'; "
		       "var saved=openmenu;"
		       "openmenu='';"
		       // don't reopen our same menu below!
		       "if ( saved=='menu%"INT32"') return;"
		       "}"

		       // show our menu
		       "show('menu%"INT32"'); "
		       // we are now open
		       "openmenu='menu%"INT32"'; "

		       "\""
		       ">"

		       "%s%s%s %c%c%c" 
		       "</span>"
		       , first->m_menuNum
		       , first->m_menuNum
		       , first->m_menuNum
		       , frontTag
		       , first->m_title
		       , backTag
		       // print triangle
		       ,0xe2
		       ,0x96
		       ,0xbc
		       );


	return true;
}

bool replaceParm ( char *cgi , SafeBuf *newUrl , HttpRequest *hr ) { 
	if ( ! cgi[0] ) return true;
	// get original request url. this is not \0 terminated
	char *src    = hr->m_origUrlRequest;
	int32_t  srcLen = hr->m_origUrlRequestLen;
	return replaceParm2 ( cgi ,newUrl, src, srcLen );
}

bool replaceParm2 ( char *cgi , SafeBuf *newUrl , 
		    char *oldUrl , int32_t oldUrlLen ) {

	char *src    = oldUrl;
	int32_t  srcLen = oldUrlLen;

	char *srcEnd = src + srcLen;

	char *equal = strstr(cgi,"=");
	if ( ! equal ) 
		return log("results: %s has no equal sign",cgi);
	int32_t cgiLen = equal - cgi;

	char *found = NULL;

	char *p = src;

 tryagain:

	found = strncasestr ( p , cgi , srcEnd - p , cgiLen );

	// if no ? or & before it it is bogus!
	if ( found && found[-1] != '&' && found[-1] != '?' ) {
		// try again
		p = found + 1;
		goto tryagain;
	}
		
	// fix &s= replaceing &sb=
	if ( found && found[cgiLen] != '=' ) {
		// try again
		p = found + 1;
		goto tryagain;
	}


	// if no collision, just append it
	if ( ! found ) {
		if ( ! newUrl->safeMemcpy ( src , srcLen ) ) return false;
		if ( ! newUrl->pushChar('&') ) return false;
		if ( ! newUrl->safeStrcpy ( cgi ) ) return false;
		if ( ! newUrl->nullTerm() ) return false;
		return true;
	}

	// . otherwise we have to replace it
	// . copy up to where it starts
	if ( ! newUrl->safeMemcpy ( src , found-src ) ) return false;
	// then insert our new cgi there
	if ( ! newUrl->safeStrcpy ( cgi ) ) return false;
	// then resume it
	char *foundEnd = strncasestr ( found , "&" , srcEnd - found );
	// if nothing came after...
	if ( ! foundEnd ) {
		if ( ! newUrl->nullTerm() ) return false;
		return true;
	}
	// copy over what came after
	if ( ! newUrl->safeMemcpy ( foundEnd, srcEnd-foundEnd ) ) return false;
	if ( ! newUrl->nullTerm() ) return false;
	return true;
}

bool printMetaContent ( Msg40 *msg40 , int32_t i , State0 *st, SafeBuf *sb ) {
	// store the user-requested meta tags content
	SearchInput *si = &st->m_si;
	char *pp      =      si->m_displayMetas;
	char *ppend   = pp + gbstrlen(si->m_displayMetas);
	Msg20 *m = msg40->m_msg20[i];//getMsg20(i);
	Msg20Reply *mr = m->m_r;
	char *dbuf    = mr->ptr_dbuf;//msg40->getDisplayBuf(i);
	int32_t  dbufLen = mr->size_dbuf-1;//msg40->getDisplayBufLen(i);
	char *dbufEnd = dbuf + (dbufLen-1);
	char *dptr    = dbuf;
	//bool  printedSomething = false;
	// loop over the names of the requested meta tags
	while ( pp < ppend && dptr < dbufEnd ) {
		// . assure last byte of dbuf is \0
		//   provided dbufLen > 0
		// . this insures sprintf and gbstrlen won't
		//   crash on dbuf/dptr
		if ( dbuf [ dbufLen ] != '\0' ) {
			log(LOG_LOGIC,"query: Meta tag buffer has no \\0.");
			break;
		}
		// skip initial spaces
		while ( pp < ppend && is_wspace_a(*pp) ) pp++;
		// break if done
		if ( ! *pp ) break;
		// that's the start of the meta tag name
		char *ss = pp;
		// . find end of that meta tag name
		// . can end in :<integer> -- specifies max len
		while ( pp < ppend && ! is_wspace_a(*pp) && 
			*pp != ':' ) pp++;
		// save current char
		char  c  = *pp;
		char *cp = pp;
		// NULL terminate the name
		*pp++ = '\0';
		// if ':' was specified, skip the rest
		if ( c == ':' ) while ( pp < ppend && ! is_wspace_a(*pp)) pp++;
		// print the name
		//int32_t sslen = gbstrlen ( ss   );
		//int32_t ddlen = gbstrlen ( dptr );
		int32_t ddlen = dbufLen;
		//if ( p + sslen + ddlen + 100 > pend ) continue;
		// newspaperarchive wants tags printed even if no value
		// make sure the meta tag isn't fucked up
		for ( int32_t ti = 0; ti < ddlen; ti++ ) {
			if ( dptr[ti] == '"' ||
			     dptr[ti] == '>' ||
			     dptr[ti] == '<' ||
			     dptr[ti] == '\r' ||
			     dptr[ti] == '\n' ||
			     dptr[ti] == '\0' ) {
				ddlen = ti;
				break;
			}
		}

		if ( ddlen > 0 ) {
			// ship it out
			if ( si->m_format == FORMAT_XML ) {
				sb->safePrintf ( "\t\t<display name=\"%s\">"
					  	"<![CDATA[", ss );
				sb->cdataEncode ( dptr, ddlen );
				sb->safePrintf ( "]]></display>\n" );
			}
			else if ( si->m_format == FORMAT_JSON ) {
				sb->safePrintf ( "\t\t\"display.%s\":\"",ss);
				sb->jsonEncode ( dptr, ddlen );
				sb->safePrintf ( "\",\n");
			}
			// otherwise, print in light gray
			else {
				sb->safePrintf("<font color=#c62939>"
					      "<b>%s</b>: ", ss );
				sb->safeMemcpy ( dptr, ddlen );
				sb->safePrintf ( "</font><br>" );
			}
		}
		// restore tag name buffer
		*cp = c;
		// point to next content of tag to display
		dptr += ddlen + 1;
	}
	return true;
}
