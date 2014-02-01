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

//static void gotSpellingWrapper ( void *state ) ;
static void gotResultsWrapper  ( void *state ) ;
//static void gotAdsWrapper      ( void *state ) ;
static void gotState           ( void *state ) ;
static bool gotResults         ( void *state ) ;

class State0 {
public:
        Query        m_q;
	SearchInput  m_si;
	Msg40        m_msg40;
	TcpSocket   *m_socket;
	Msg0         m_msg0;
	long long    m_startTime;
	//Ads          m_ads;
	bool         m_gotAds;
	bool         m_gotResults;
	char         m_spell  [MAX_FRAG_SIZE]; // spelling recommendation
	bool         m_gotSpell;
	long         m_errno;
	Query        m_qq3;
        long         m_numDocIds;
	long long    m_took; // how long it took to get the results
	HttpRequest  m_hr;
	bool         m_printedHeaderRow;

	// for printing our search result json items in csv:
	HashTableX   m_columnTable;
	long         m_numCSVColumns;

	// stuff for doing redownloads
	bool    m_didRedownload;
	XmlDoc *m_xd;
	long    m_oldContentHash32;
};

static bool printResult ( SafeBuf &sb,
			 State0 *st,
			 long ix , 
			 CollectionRec *cr ,
			 char *qe ) ;

bool printCSVHeaderRow ( SafeBuf *sb , State0 *st ) ;

bool printJsonItemInCSV ( char *json , SafeBuf *sb , class State0 *st ) ;

bool printPairScore ( SafeBuf &sb , SearchInput *si , PairScore *ps ,
		      Msg20Reply *mr , Msg40 *msg40 , bool first ) ;

bool printScoresHeader ( SafeBuf &sb ) ;

bool printSingleScore ( SafeBuf &sb , SearchInput *si , SingleScore *ss ,
			Msg20Reply *mr , Msg40 *msg40 ) ;

bool printLogoAndSearchBox ( SafeBuf &sb , HttpRequest *hr , long catId );

bool sendReply ( State0 *st , char *reply ) {

	long savedErr = g_errno;

	TcpSocket *s = st->m_socket;
	if ( ! s ) { char *xx=NULL;*xx=0; }
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


	long rlen = 0;
	if ( reply ) rlen = gbstrlen(reply);
	logf(LOG_DEBUG,"gb: sending back %li bytes",rlen);

	// . use light brown if coming directly from an end user
	// . use darker brown if xml feed
	long color = 0x00b58869;
	if ( si->m_format != FORMAT_HTML )color = 0x00753d30 ;
	long long nowms = gettimeofdayInMilliseconds();
	long long took  = nowms - st->m_startTime ;
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
			    st->m_q.m_numTerms);

	// . log the time
	// . do not do this if g_errno is set lest m_sbuf1 be bogus b/c
	//   it failed to allocate its buf to hold terminating \0 in
	//   SearchInput::setQueryBuffers()
	if ( ! g_errno && st->m_took >= g_conf.m_logQueryTimeThreshold ) {
		logf(LOG_TIMING,"query: Took %lli ms for %s. results=%li",
		     st->m_took,
		     si->m_sbuf1.getBufStart(),
		     st->m_msg40.getNumResults());
	}

	//bool xml = si->m_xml;

	g_stats.logAvgQueryTime(st->m_startTime);

	mdelete(st, sizeof(State0), "PageResults2");
	delete st;

	if ( ! savedErr ) { // g_errno ) {
		g_stats.m_numSuccess++;
		// . one hour cache time... no 1000 hours, basically infinite
		// . no because if we redo the query the results are cached
		long cacheTime = 3600;//*1000;
		// the "Check it" link on add url uses &usecache=0 to tell
		// the browser not to use its cache...
		//if ( hr->getLong("usecache",-1) == 0 ) cacheTime = 0;
		//
		// send back the actual search results
		//
		g_httpServer.sendDynamicPage(s,
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
		return true;
	}
	// error otherwise
	if ( savedErr != ENOPERM ) 
		g_stats.m_numFails++;

	if ( format == FORMAT_XML ) {
		SafeBuf sb;
		sb.safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"UTF-8\" ?>\n"
			      "<response>\n"
			      "\t<errno>%li</errno>\n"
			      "\t<errmsg>%s</errmsg>\n"
			      "</response>\n"
			      ,(long)savedErr
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

	long status = 500;
	if (savedErr == ETOOMANYOPERANDS ||
	    savedErr == EBADREQUEST ||
	    savedErr == ENOPERM ||
	    savedErr == ENOCOLLREC) 
		status = 400;

	g_httpServer.sendQueryErrorReply(s,
					 status,
					 mstrerror(savedErr),
					 format,//xml,
					 savedErr, 
					 "There was an error!");
	return true;
}

bool printCSSHead ( SafeBuf &sb ) {
	return sb.safePrintf(
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
			      "color: #000000;"
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
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "msg" will be inserted into the access log for this request
bool sendPageResults ( TcpSocket *s , HttpRequest *hr ) {
	// . check for sdirt=4, this a site search on the given directory id
	// . need to pre-query the directory first to get the sites to search
	//   this will likely have just been cached so it should be quick
	// . then need to construct a site search query
	//long rawFormat = hr->getLong("xml", 0); // was "raw"
	//long xml = hr->getLong("xml",0);

	// what format should search results be in? default is html
	char format = getFormatFromRequest ( hr );

	// get the dmoz catid if given
	//long searchingDmoz = hr->getLong("dmoz",0);

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
		printCSSHead ( sb );
		sb.safePrintf(
			      "<body "
			      "onLoad=\""
			      "var client = new XMLHttpRequest();\n"
			      "client.onreadystatechange = handler;\n"
			      //"var url='http://10.5.1.203:8000/search?q="
			      "var url='/search?q="
			      );
		long  qlen;
		char *qstr = hr->getString("q",&qlen,"",NULL);
		// . crap! also gotta encode apostrophe since "var url='..."
		// . true = encodeApostrophes?
		sb.urlEncode2 ( qstr , true );
		// progate query language
		char *qlang = hr->getString("qlang",NULL,NULL);
		if ( qlang ) sb.safePrintf("&qlang=%s",qlang);
		// propagate "admin" if set
		long admin = hr->getLong("admin",-1);
		if ( admin != -1 ) sb.safePrintf("&admin=%li",admin);
		// propagate showing of banned results
		if ( hr->getLong("sb",0) ) sb.safePrintf("&sb=1");
		// propagate list of sites to restrict query to
		long sitesLen;
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
		long debug = hr->getLong("debug",0);
		if ( debug ) sb.safePrintf("&debug=%li",debug);
		// propagate "s"
		long ss = hr->getLong("s",-1);
		if ( ss > 0 ) sb.safePrintf("&s=%li",ss);
		// propagate "n"
		long n = hr->getLong("n",-1);
		if ( n >= 0 ) sb.safePrintf("&n=%li",n);
		// Docs to Scan for Related Topics
		long dsrt = hr->getLong("dsrt",-1);
		if ( dsrt >= 0 ) sb.safePrintf("&dsrt=%li",dsrt);
		// debug gigabits?
		long dg = hr->getLong("dg",-1);
		if ( dg >= 0 ) sb.safePrintf("&dg=%li",dg);
		// show gigabits?
		long gb = hr->getLong("gigabits",1);
		if ( gb >= 1 ) sb.safePrintf("&gigabits=%li",gb);
		// show banned results?
		long showBanned = hr->getLong("sb",0);
		if ( showBanned ) sb.safePrintf("&sb=1");
		// propagate collection
		long clen;
		char *coll = hr->getString("c",&clen,"",NULL);
		if ( coll ) sb.safePrintf("&c=%s",coll);
		// forward the "ff" family filter as well
		long ff = hr->getLong("ff",0);
		if ( ff ) sb.safePrintf("&ff=%li",ff);
		// provide hash of the query so clients can't just pass in
		// a bogus id to get search results from us
		unsigned long h32 = hash32n(qstr);
		if ( h32 == 0 ) h32 = 1;
		// add this timestamp so when we hit back button this
		// parent page will be cached and so will this ajax url.
		// but if they hit reload the parent page reloads with a
		// different ajax url because "rand" is different
		unsigned long long rand64 = gettimeofdayInMillisecondsLocal();
		sb.safePrintf("&id=%lu&rand=%llu';\n"
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
		printLogoAndSearchBox ( sb , hr , -1 ); // catId = -1
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
			      "Copyright &copy; 2013. All Rights Reserved.<br/>"
				  "Powered by the <a href='https://www.gigablast.com/'>GigaBlast</a> open source search engine."
			      "</font>"
			      "</center>\n"

			      "</body>\n"
			      "</html>\n"
			      );
		// one hour cache time... no 1000 hours, basically infinite
		long cacheTime = 3600; // *1000;
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
		    "Could not allocate %li bytes for query. "
		    "Returning HTTP status of 500.",(long)sizeof(State0));
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

	// . parse it up
	// . this returns false and sets g_errno and, maybe, g_msg on error
	SearchInput *si = &st->m_si;
	if ( ! si->set ( s ,
			 // si just copies the ptr into the httprequest
			 // into stuff like SearchInput::m_defaultSortLanguage
			 // so do not use the "hr" on the stack. SearchInput::
			 // m_hr points to the hr we pass into
			 // SearchInput::set
			 &st->m_hr, 
			 &st->m_q ) ) {
		log("query: set search input: %s",mstrerror(g_errno));
		g_errno = EBADENGINEER;
		return sendReply ( st, NULL );
	}

	long  codeLen = 0;
	char *code = hr->getString("code", &codeLen, NULL);
	// allow up to 1000 results per query for paying clients
	CollectionRec *cr = si->m_cr;


	// limit here
	long maxpp = cr->m_maxSearchResultsPerQuery ;
	if ( si->m_docsWanted > maxpp &&
	     // disable serp max per page for custom crawls
	     ! cr->m_isCustomCrawl )
		si->m_docsWanted = maxpp;

        st->m_numDocIds = si->m_docsWanted;

	// watch out for cowboys
	if ( si->m_firstResultNum>=si->m_maxResults) return sendReply(st,NULL);

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

	st->m_socket  = s;
	// assume we'll block
	st->m_gotResults = false;
	st->m_gotAds     = false;
	st->m_gotSpell   = false;

	// reset
	st->m_printedHeaderRow = false;

	long ip = s->m_ip;
	long uipLen;
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

	// LAUNCH ADS
	// . now get the ad space for this query
	// . don't get ads if we're not on the first page of results
	// . query must be NULL terminated
	st->m_gotAds = true;
	/*
	if (si->m_adFeedEnabled && ! si->m_xml && si->m_docsWanted > 0) {
                long pageNum = (si->m_firstResultNum/si->m_docsWanted) + 1;
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
	// . we print out matching docIds to long if m_isDebug is true
	// . no longer forward this, since proxy will take care of evenly
	//   distributing its msg 0xfd "forward" requests now
	st->m_gotResults=st->m_msg40.getResults(si,false,st,gotResultsWrapper);
	// save error
	st->m_errno = g_errno;

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


// print all sentences containing this gigabit
static bool printGigabit ( State0 *st,
			   SafeBuf &sb , 
			   Msg40 *msg40 , 
			   Gigabit *gi , 
			   SearchInput *si ) {

	//static long s_gigabitCount = 0;

	sb.safePrintf("<nobr><b>");
	//"<img src=http://search.yippy.com/"
	//"images/new/button-closed.gif><b>");

	HttpRequest *hr = &st->m_hr;

	// make a new query
	sb.safePrintf("<a href=\"/search?gigabits=1&q=");
	sb.urlEncode(gi->m_term,gi->m_termLen);
	sb.safeMemcpy("+|+",3);
	char *q = hr->getString("q",NULL,"");
	sb.urlEncode(q);
	sb.safePrintf("\">");
	sb.safeMemcpy(gi->m_term,gi->m_termLen);
	sb.safePrintf("</a></b>");
	sb.safePrintf(" <font color=gray size=-1>");
	//long numOff = sb.m_length;
	// now the # of pages not nuggets
	sb.safePrintf("(%li)",gi->m_numPages);
	sb.safePrintf("</font>");
	sb.safePrintf("</b>");
	if ( si->m_isAdmin ) 
		sb.safePrintf("[%.0f]{%li}",
			      gi->m_gbscore,
			      gi->m_minPop);
	// that's it for the gigabit
	sb.safePrintf("<br>");

	return true;
}

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

	long long nowMS = gettimeofdayInMilliseconds();
	// log the time
	long long took = nowMS - st->m_startTime;
	// record that
	st->m_took = took;


	// grab the query
	Msg40 *msg40 = &(st->m_msg40);
	char  *q    = msg40->getQuery();
	long   qlen = msg40->getQueryLen();

	SearchInput *si = &st->m_si;

	// shortcuts
	char        *coll    = si->m_coll2;
	long         collLen = si->m_collLen2;
	
	// collection rec must still be there since SearchInput references 
	// into it, and it must be the SAME ptr too!
	CollectionRec *cr = g_collectiondb.getRec ( coll , collLen );
	if ( ! cr || cr != si->m_cr ) {
	       g_errno = ENOCOLLREC;
	       return sendReply(st,NULL);
	}

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
	long mabr = st->m_hr.getLong("maxagebeforeredownload",-1);
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
		long newHash32 = xd->getContentHash32();
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


  	//char  local[ 128000 ];
	//SafeBuf sb(local, 128000);
	SafeBuf sb;
	// reserve 1.5MB now!
	if ( ! sb.reserve(1500000 ,"pgresbuf" ) ) // 128000) )
		return true;
	// just in case it is empty, make it null terminated
	sb.nullTerm();

	// . if not matt wells we do not do ajax
	// . the ajax is just there to prevent bots from slamming me 
	//   with queries.
	if ( ! g_conf.m_isMattWells && si->m_format == FORMAT_HTML ) {
		printCSSHead ( sb );
		sb.safePrintf("<body>");
		printLogoAndSearchBox ( sb , &st->m_hr , -1 ); // catId = -1
	}



	// xml
	if ( si->m_format == FORMAT_XML )
		sb.safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"UTF-8\" ?>\n"
			      "<response>\n" );

	// show current time
	if ( si->m_format == FORMAT_XML ) {
		long long globalNowMS = localToGlobalTimeMilliseconds(nowMS);
		sb.safePrintf("\t<currentTimeUTC>%lu</currentTimeUTC>\n",
			      (long)(globalNowMS/1000));
	}

	// show response time
	if ( si->m_format == FORMAT_XML )
		sb.safePrintf("\t<responseTimeMS>%lli</responseTimeMS>\n",
			      st->m_took);

	// out of memory allocating msg20s?
	if ( st->m_errno ) {
		log("query: Query failed. Had error processing query: %s",
		    mstrerror(st->m_errno));
		g_errno = st->m_errno;
		return sendReply(st,sb.getBufStart());
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


	// save how many docs are in it
	long long docsInColl = -1;
	//RdbBase *base = getRdbBase ( RDB_CHECKSUMDB , si->m_coll );
	//RdbBase *base = getRdbBase ( (uint8_t)RDB_CLUSTERDB , si->m_coll2 );
	//if ( base ) docsInColl = base->getNumGlobalRecs();
	docsInColl = g_hostdb.getNumGlobalRecs ( );
	// include number of docs in the collection corpus
	if ( si->m_format == FORMAT_XML && docsInColl >= 0LL )
		sb.safePrintf ( "\t<docsInCollection>%lli"
				"</docsInCollection>\n", docsInColl );

 	long numResults = msg40->getNumResults();
	bool moreFollow = msg40->moreResultsFollow();
	// an estimate of the # of total hits
	long long totalHits = msg40->getNumTotalHits();
	// only adjust upwards for first page now so it doesn't keep chaning
	if ( totalHits < numResults ) totalHits = numResults;

	if ( si->m_format == FORMAT_XML )
		sb.safePrintf("\t<hits>%lli</hits>\n"
			      "\t<moreResultsFollow>%li</moreResultsFollow>\n"
			      ,(long long)totalHits
			      ,(long)moreFollow
			      );

	// . did he get a spelling recommendation?
	// . do not use htmlEncode() on this anymore since receiver
	//   of the XML feed usually does not want that.
	if ( si->m_format == FORMAT_XML && st->m_spell[0] ) {
		sb.safePrintf ("\t<spell><![CDATA[");
		sb.safeStrcpy(st->m_spell);
		sb.safePrintf ("]]></spell>\n");
	}

	// debug
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: Displaying up to %li results.",
		     numResults);

	// tell browser again
	//if ( si->m_format == FORMAT_HTML )
	//	sb.safePrintf("<meta http-equiv=\"Content-Type\" "
	//		      "content=\"text/html; charset=utf-8\">\n");

	// get some result info from msg40
	long firstNum   = msg40->getFirstResultNum() ;
	// numResults may be more than we requested now!
	long n = msg40->getDocsWanted();
	if ( n > numResults )  n = numResults;

	// . make the query class here for highlighting
	// . keepAllSingles means to convert all individual words into
	//   QueryTerms even if they're in quotes or in a connection (cd-rom).
	//   we use this for highlighting purposes
	Query qq;
	qq.set2 ( si->m_displayQuery, langUnknown , si->m_queryExpansion );
	//	 si->m_boolFlag,
	//         true ); // keepAllSingles?

	if ( g_errno ) return sendReply (st,NULL);

	DocIdScore *dpx = NULL;
	if ( numResults > 0 ) dpx = msg40->getScoreInfo(0);

	if ( si->m_format == FORMAT_XML && dpx ) {
		// # query terms used!
		//long nr = dpx->m_numRequiredTerms;
		float max = 0.0;
		// max pairwise
		float lw = getHashGroupWeight(HASHGROUP_INLINKTEXT);
		// square that location weight
		lw *= lw;
		// assume its an inlinker's text, who has rank 15!!!
		lw *= getLinkerWeight(MAXSITERANK);
		// double loops
		/*
		for ( long i = 0 ; i< nr ; i++ ) {
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = getTermFreqWeight(ssi->m_listSize);
			for ( long j = i+1; j< nr ; j++ ) {
				SingleScore *ssj = &dpx->m_singleScores[j];
				float tfwj = getTermFreqWeight(ssj->m_listSize);
				max += (lw * tfwi * tfwj)/3.0;
			}
		}
		*/
		// single weights
		float maxtfw1 = 0.0;
		long maxi1;
		// now we can have multiple SingleScores for the same term!
		// because we take the top MAX_TOP now and add them to
		// get the term's final score.
		for ( long i = 0 ; i< dpx->m_numSingles ; i++ ) {
			SingleScore *ssi = &dpx->m_singleScores[i];
			float tfwi = ssi->m_tfWeight;
			if ( tfwi <= maxtfw1 ) continue;
			maxtfw1 = tfwi;
			maxi1 = i;
		}
		float maxtfw2 = 0.0;
		long maxi2;
		for ( long i = 0 ; i< dpx->m_numSingles ; i++ ) {
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
		sb.safePrintf ("\t\t<theoreticalMaxFinalScore>%f"
			       "</theoreticalMaxFinalScore>\n",
			       max );
	}
	


	// debug msg
	log ( LOG_TIMING ,
	     "query: Got %li search results in %lli ms for q=%s",
	      numResults,gettimeofdayInMilliseconds()-st->m_startTime,
	      qq.getQuery());

	//Highlight h;

	// encode query buf
	char qe[MAX_QUERY_LEN+1];
	char *dq    = si->m_displayQuery;
	long  dqlen = si->m_displayQueryLen;
	urlEncode(qe,MAX_QUERY_LEN*2,dq,dqlen);

	// how many results were requested?
	long docsWanted = msg40->getDocsWanted();

	// store html head into p, but stop at %q
	//char *head = cr->m_htmlHead;
	//long  hlen = cr->m_htmlHeadLen;
	//if ( ! si->m_xml ) sb.safeMemcpy ( head , hlen );


	// ignore imcomplete or invalid multibyte or wide characters errors
	//if ( g_errno == EILSEQ ) {
	//	log("query: Query error: %s. Ignoring.", mstrerror(g_errno));
	//	g_errno = 0;
	//}

	// secret search backdoor
	if ( qlen == 7 && q[0]=='3' && q[1]=='b' && q[2]=='Y' &&
	     q[3]=='6' && q[4]=='u' && q[5]=='2' && q[6]=='Z' ) {
		sb.safePrintf ( "<br><b>You owe me!</b><br><br>" );
	}

	// print it with commas into "thbuf" and null terminate it
	char thbuf[64];
	ulltoa ( thbuf , totalHits );

	char inbuf[128];
	ulltoa ( inbuf , docsInColl );

        Query qq3;
	Query *qq2;
	bool firstIgnored;
	bool isAdmin = si->m_isAdmin;
	if ( si->m_format != FORMAT_HTML ) isAdmin = false;

	// otherwise, we had no error
	if ( numResults == 0 && si->m_format == FORMAT_HTML ) {
		sb.safePrintf ( "No results found in <b>%s</b> collection.",
				cr->m_coll);
	}
	else if ( moreFollow && si->m_format == FORMAT_HTML ) {
		if ( isAdmin && si->m_docsToScanForReranking > 1 )
			sb.safePrintf ( "PQR'd " );
		sb.safePrintf ("Results <b>%li</b> to <b>%li</b> of "
			       "exactly <b>%s</b> from an index "
			       "of %s pages" , 
			       firstNum + 1          ,
			       firstNum + n          ,
			       thbuf                 ,
			       inbuf
			       );
	}
	// otherwise, we didn't get enough results to show this page
	else if ( si->m_format == FORMAT_HTML ) {
		if ( isAdmin && si->m_docsToScanForReranking > 1 )
			sb.safePrintf ( "PQR'd " );
		sb.safePrintf ("Results <b>%li</b> to <b>%li</b> of "
			       "exactly <b>%s</b> from an index "
			       "of %s pages" , 
			       firstNum + 1          ,
			       firstNum + n          ,
			       thbuf                 ,
			       inbuf
			       );
	}

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
		sb.safePrintf("<br><br>"
			      "Could not find that url in the "
			      "index. Try <a href=/addurl?u=");
		sb.urlEncode(url,ue-url,false,false);
		sb.safePrintf(">Adding it.</a>");
	}

	// sometimes ppl search for "www.whatever.com" so ask them if they
	// want to search for url:www.whatever.com
	if ( numResults > 0  && si->m_format == FORMAT_HTML && url && url ==q){
		sb.safePrintf("<br><br>"
			      "Did you mean to "
			      "search for the url "
			      "<a href=/search?q=url%%3A");
		sb.urlEncode(url,ue-url,false,false);
		sb.safePrintf(">");
		sb.safeMemcpy(url,ue-url);
		sb.safePrintf("</a> itself?");
	}



	// is it the main collection?
	bool isMain = false;
	if ( collLen == 4 && strncmp ( coll, "main", 4) == 0 ) isMain = true;

	// print "in collection ***" if we had a collection
	if ( collLen > 0 && ! isMain && si->m_format == FORMAT_HTML )
		sb.safePrintf (" in collection <b>%s</b>",coll);


	char *pwd = si->m_pwd;
	if ( ! pwd ) pwd = "";

	/*
	if ( si->m_format == FORMAT_HTML )
		sb.safePrintf(" &nbsp; <u><b><font color=blue><a onclick=\""
			      "for (var i = 0; i < %li; i++) {"
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

	// convenient admin link
	if ( isAdmin ) {
		sb.safePrintf(" &nbsp; "
			      "<font color=red><b>"
			      "<a href=\"/master?c=%s\">"
			      "[admin]"
			      "</a></b></font>",coll);
		// print reindex link
		// get the filename directly
		char *langStr = getLangAbbr ( si->m_queryLang );
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       "<a href=\"/admin/reindex?c=%s&"
			       "qlang=%s&q=%s\">"
			       "[reindex or delete these results]</a></b>"
			       "</font> ", coll , langStr , qe );
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       "<a href=\"/inject?c=%s&qts=%s\">"
			       "[scrape]</a></b>"
			       "</font> ", coll , qe );
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       "<a href=\"/search?sb=1&c=%s&"
			       "qlang=%s&q=%s\">"
			       "[show banned results]</a></b>"
			       "</font> ", coll , langStr , qe );
	}

	// if its an ip: or site: query, print ban link
	if ( isAdmin && strncmp(si->m_displayQuery,"ip:",3)==0) {
		// get the ip
		char *ips = si->m_displayQuery + 3;
		// copy to buf, append a ".0" if we need to
		char buf [ 32 ];
		long i ;
		long np = 0;
		for ( i = 0 ; i<29 && (is_digit(ips[i])||ips[i]=='.'); i++ ){
			if ( ips[i] == '.' ) np++;
			buf[i]=ips[i];
		}
		// if not enough periods bail
		if ( np <= 1 ) goto skip2;
		if ( np == 2 ) { buf[i++]='.'; buf[i++]='0'; }
		buf[i] = '\0';
		// search ip back or forward
		long ip = atoip(buf,i);
		sb.safePrintf ("&nbsp <b>"
			       "<a href=\"/search?q=ip%%3A%s&c=%s&n=%li\">"
			       "[prev %s]</a></b>" , 
			       iptoa(ip-0x01000000),coll,docsWanted,
			       iptoa(ip-0x01000000));
		sb.safePrintf ("&nbsp <b>"
			       "<a href=\"/search?q=ip%%3A%s&c=%s&n=%li\">"
			       "[next %s]</a></b>" , 
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
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       "<a href=\"/master/tagdb?"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s\">"
			       "[ban %s]</a></b>"
			       "</font> ",coll , start );
		*sp = c;
	}
	if ( isAdmin && strncmp(si->m_displayQuery,"gbad:",5)==0) {
		// get the ip
		char *start = si->m_displayQuery + 5;
		char *sp = start;
		while ( *sp && ! is_wspace_a(*sp) ) sp++;
		char c = *sp;
		*sp = '\0';
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       "<a href=\"/master/tagdb?"
			       //"tagid0=%li&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s"
			       "&u=%s-gbadid.com\">"
			       "[ban %s]</a></b>"
			       "</font> ", coll , start , start );
		*sp = c;
	}
 skip2:

	// cache switch for admin
	if ( isAdmin && msg40->getCachedTime() > 0 ) {
		// get the filename directly
		sb.safePrintf(" &nbsp; "
			      "<font color=red><b>"
			      "<a href=\"/search?c=%s",
			      coll );
		// finish it
		sb.safePrintf("&q=%s&rcache=0&seq=0&rtq=0\">"
			      "[cache off]</a></b>"
			      "</font> ", qe );
	}

	// mention ignored query terms
	// we need to set another Query with "keepAllSingles" set to false
	qq2 = si->m_q;
	//qq2.set ( q , qlen , NULL , 0 , si->m_boolFlag , false );
	firstIgnored = true;
	for ( long i = 0 ; i < qq2->m_numWords ; i++ ) {
		//if ( si->m_xml ) break;
		QueryWord *qw = &qq2->m_qwords[i];
		// only print out words ignored cuz they were stop words
		if ( qw->m_ignoreWord != IGNORE_QSTOP ) continue;
		// print header -- we got one
		if ( firstIgnored ) {
			if ( si->m_format == FORMAT_XML )
				sb.safePrintf ("\t<ignoredWords><![CDATA[");
			else
				sb.safePrintf (" &nbsp; <font "
					       "color=\"#707070\">The "
					       "following query words "
					       "were ignored: "
					       "<b>");
			firstIgnored = false;
		}
		// print the word
		char *t    = qw->m_word; 
		long  tlen = qw->m_wordLen;
		sb.utf8Encode2 ( t , tlen );
		sb.safePrintf (" ");
	}
	// print tail if we had ignored terms
	if ( ! firstIgnored ) {
		sb.incrementLength(-1);
		if ( si->m_format == FORMAT_XML )
			sb.safePrintf("]]></ignoredWords>\n");
		else
			sb.safePrintf ("</b>. Preceed each with a '+' or "
				       "wrap in "
				       "quotes to not ignore.</font>");
	}

	if ( si->m_format == FORMAT_HTML ) sb.safePrintf("<br><br>");

	if ( si->m_format == FORMAT_HTML )
		sb.safePrintf("<table cellpadding=0 cellspacing=0>"
			      "<tr><td valign=top>");

	SafeBuf *gbuf = &msg40->m_gigabitBuf;
	long numGigabits = gbuf->length()/sizeof(Gigabit);

	if ( si->m_format == FORMAT_XML ) numGigabits = 0;

	// print gigabits
	Gigabit *gigabits = (Gigabit *)gbuf->getBufStart();
	//long numCols = 5;
	//long perRow = numGigabits / numCols;
	if ( numGigabits && si->m_format == FORMAT_HTML )
		sb.safePrintf("<table cellspacing=7 bgcolor=lightgray>"
			      "<tr><td width=200px; valign=top>");
	for ( long i = 0 ; i < numGigabits ; i++ ) {
		if ( i > 0 && si->m_format == FORMAT_HTML ) 
			sb.safePrintf("<hr>");
		//if ( perRow && (i % perRow == 0) )
		//	sb.safePrintf("</td><td valign=top>");
		// print all sentences containing this gigabit
		Gigabit *gi = &gigabits[i];
		printGigabit ( st,sb , msg40 , gi , si );
		sb.safePrintf("<br>");
	}
	if ( numGigabits && si->m_format == FORMAT_HTML )
		sb.safePrintf("</td></tr></table>");

	// two pane table
	if ( si->m_format == FORMAT_HTML ) 
		sb.safePrintf("</td><td valign=top>");

	// did we get a spelling recommendation?
	if ( si->m_format == FORMAT_HTML && st->m_spell[0] ) {
		// encode the spelling recommendation
		long len = gbstrlen ( st->m_spell );
		char qe2[MAX_FRAG_SIZE];
		urlEncode(qe2, MAX_FRAG_SIZE, st->m_spell, len);
		sb.safePrintf ("<font size=+0 color=\"#c62939\">Did you mean:"
			       "</font> <font size=+0>"
			       "<a href=\"/search?q=%s",
			       qe2 );
		// close it up
		sb.safePrintf ("\"><i><b>");
		sb.utf8Encode2(st->m_spell, len);
		// then finish it off
		sb.safePrintf ("</b></i></a></font>\n<br><br>\n");
	}

	// . Wrap results in a table if we are using ads. Easier to display.
	//Ads *ads = &st->m_ads;
	//if ( ads->hasAds() )
        //        sb.safePrintf("<table width=\"100%%\">\n"
        //                    "<tr><td style=\"vertical-align:top;\">\n");

	// debug
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: Printing up to %li results. "
		     "bufStart=0x%lx", 
		     numResults,(long)sb.getBuf());


	//
	// BEGIN PRINT THE RESULTS
	//

	//sb.safePrintf("<iframe src=\"http://10.5.54.154:8000/\"></iframe>");
	//sb.safePrintf("<iframe src=\"http://www.google.com/cse?cx=013269018370076798483%3A8eec3papwpi&ie=UTF-8&q=poop\"></iframe>");

	/*
	sb.safePrintf(

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
		sb.safePrintf("<a onclick=\""
			      "var e = "
			      "document.getElementsByTagName('html')[0];\n"
			      "alert ('i='+e.innerHTML);"
			      "\">"
			      "CLICK ME"
			      "</a>");
	*/


	// don't display more than docsWanted results
	long count = msg40->getDocsWanted();
	bool hadPrintError = false;

	for ( long i = 0 ; count > 0 && i < numResults ; i++ ) {
		// prints in xml or html
		if ( ! printResult ( sb , st , i , cr , qe ) ) {
			hadPrintError = true;
			break;
		}
		// limit it
		count--;
	}


	if ( hadPrintError ) {
		if ( ! g_errno ) g_errno = EBADENGINEER;
		log("query: had error: %s",mstrerror(g_errno));
		return sendReply ( st , sb.getBufStart() );
	}


	//
	// END PRINT THE RESULTS
	//

	// end the two-pane table
	if ( si->m_format == FORMAT_HTML ) sb.safePrintf("</td></tr></table>");

	// for storing a list of all of the sites we displayed, now we print a 
	// link at the bottom of the page to ban all of the sites displayed 
	// with one click
	SafeBuf banSites;

	//long tailLen = 0;
	//char *tail = NULL;


	//
	// PRINT PREV 10 NEXT 10 links!
	// 

	// center everything below here
	if ( si->m_format == FORMAT_HTML ) sb.safePrintf ( "<br><center>" );

	long remember = sb.length();

	// now print "Prev X Results" if we need to
	if ( firstNum < 0 ) firstNum = 0;

	char abuf[300];
	SafeBuf args(abuf,300);
	// show banned?
	if ( si->m_showBanned && ! si->m_isAdmin )
		args.safePrintf("&sb=1");
	if ( ! si->m_showBanned && si->m_isAdmin )
		args.safePrintf("&sb=0");
	// collection
	args.safePrintf("&c=%s",coll);
	// carry over the sites we are restricting the search results to
	if ( si->m_whiteListBuf.length() )
		args.safePrintf("&sites=%s",si->m_whiteListBuf.getBufStart());
	

	if ( firstNum > 0 && si->m_format == FORMAT_HTML ) {
		long ss = firstNum - msg40->getDocsWanted();
		sb.safePrintf("<a href=\"/search?s=%li&q=",ss);
		// our current query parameters
		sb.safeStrcpy ( qe );
		// print other args if not zero
		sb.safeMemcpy ( &args );
		// close it up
		sb.safePrintf ("\"><b>"
			       "<font size=+0>Prev %li Results</font>"
			       "</b></a>", 
			       docsWanted );
	}

	// now print "Next X Results"
	if ( moreFollow && si->m_format == FORMAT_HTML ) {
		long ss = firstNum + msg40->getDocsWanted();
		// print a separator first if we had a prev results before us
		if ( sb.length() > remember ) sb.safePrintf ( " &nbsp; " );
		// add the query
		sb.safePrintf ("<a href=\"/search?s=%li&q=",ss);
		// our current query parameters
		sb.safeStrcpy ( qe );
		// print other args if not zero
		sb.safeMemcpy ( &args );
		// close it up
		sb.safePrintf("\"><b>"
			      "<font size=+0>Next %li Results</font>"
			      "</b></a>", 
			      docsWanted );
	}


	// print try this search on...
	// an additional <br> if we had a Next or Prev results link
	if ( sb.length() > remember ) sb.safeMemcpy ("<br>" , 4 ); 

	//
	// END PRINT PREV 10 NEXT 10 links!
	// 

	// end results table cell... and print calendar at top
	//tail = cr->m_htmlTail;
	//tailLen = gbstrlen (tail );
	//if ( si->m_format == FORMAT_HTML ) sb.safeMemcpy ( tail , tailLen );

	if ( si->m_format == FORMAT_HTML ) {
		/*
		sb.safePrintf("<table cellpadding=2 cellspacing=0 border=0>"
			      "<tr><td></td>"
			      "<td valign=top align=center>"
			      "<nobr>"
			      "<input type=text name=q2 size=60 value=\"" );
		sb.htmlEncode ( si->m_sbuf1.getBufStart() , 
				si->m_sbuf1.length() ,
				false );
		sb.safePrintf("\">"
			      "<input type=submit value=\"Search\" border=0>"
			      "</nobr><br>"
			      "</td><td></td>"
			      "</tr>"
			      );
		sb.safePrintf("</table>");
		*/
		sb.safePrintf("<input name=c type=hidden value=\"%s\">",coll);
	}

	if ( isAdmin && banSites.length() > 0 )
		sb.safePrintf ("<br><br><div align=right><b>"
			       "<a href=\"/master/tagdb?"
			       //"tagid0=%li&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s&uenc=1&u=%s\">"
			       "[ban all of these domains]</a></b></div>"
			       "<br>\n ", 
			       coll, banSites.getBufStart());


	// TODO: print cache line in light gray here
	// TODO: "these results were cached X minutes ago"
	if ( msg40->getCachedTime() > 0 && si->m_format == FORMAT_HTML ) {
		sb.safePrintf("<br><br><font size=1 color=707070><b><center>");
		sb.safePrintf ( " These results were cached " );
		// this cached time is this local cpu's time
		long diff = getTime() - msg40->getCachedTime();
		if      ( diff < 60   ) sb.safePrintf ( "%li seconds" , diff );
		else if ( diff < 2*60 ) sb.safePrintf ( "1 minute");
		else                    sb.safePrintf ( "%li minutes",diff/60);
		sb.safePrintf ( " ago. [<a href=\"/pageCache.html\">"
				"<font color=707070>Info</font></a>]");
		sb.safePrintf ( "</center></font>");
	}

	
	if ( si->m_format == FORMAT_XML )
		sb.safePrintf("</response>\n");


	// if we did not use ajax, print this tail here now
	if ( si->m_format == FORMAT_HTML && ! g_conf.m_isMattWells ) {
		sb.safePrintf ( "<br>"
				"<center>"
				"<font color=gray>"
				"Copyright &copy; 2013. All Rights "
				"Reserved.<br/>"
				"Powered by the <a href='https://www."
				"gigablast.com/'>GigaBlast</a> open source "
				"search engine."
				"</font>"
				"</center>\n"
				
				"</body>\n"
				"</html>\n"
			      );
	}


	if ( sb.length() == 0 )
		sb.pushChar('\n');

	return sendReply ( st , sb.getBufStart() );
}

bool printTimeAgo ( SafeBuf &sb , long ts , char *prefix , SearchInput *si ) {
	// Jul 23, 1971
	sb.reserve2x(200);
	long now = getTimeGlobal();
	// for printing
	long mins = 1000;
	long hrs  = 1000;
	long days ;
	if ( ts > 0 ) {
		mins = (long)((now - ts)/60);
		hrs  = (long)((now - ts)/3600);
		days = (long)((now - ts)/(3600*24));
		if ( mins < 0 ) mins = 0;
		if ( hrs  < 0 ) hrs  = 0;
		if ( days < 0 ) days = 0;
	}
	// print the time ago
	if      ( mins ==1)sb.safePrintf(" - %s: %li minute ago",prefix,mins);
	else if (mins<60)sb.safePrintf ( " - %s: %li minutes ago",prefix,mins);
	else if ( hrs == 1 )sb.safePrintf ( " - %s: %li hour ago",prefix,hrs);
	else if ( hrs < 24 )sb.safePrintf ( " - %s: %li hours ago",prefix,hrs);
	else if ( days == 1 )sb.safePrintf ( " - %s: %li day ago",prefix,days);
	else if (days< 7 )sb.safePrintf ( " - %s: %li days ago",prefix,days);
	// do not show if more than 1 wk old! we want to seem as
	// fresh as possible
	else if ( ts > 0 ) { // && si->m_isAdmin ) {
		struct tm *timeStruct = localtime ( &ts );
		sb.safePrintf(" - %s: ",prefix);
		char tmp[100];
		strftime(tmp,100,"%b %d %Y",timeStruct);
		sb.safeStrcpy(tmp);
	}
	return true;
}

int linkSiteRankCmp (const void *v1, const void *v2) {
	Inlink *i1 = *(Inlink **)v1;
	Inlink *i2 = *(Inlink **)v2;
	return i2->m_siteRank - i1->m_siteRank;
}

bool printInlinkText ( SafeBuf &sb , Msg20Reply *mr , SearchInput *si ,
		       long *numPrinted ) {
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
	if ( info && mr->size_linkInfo != info->m_size ){char *xx=NULL;*xx=0; }
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
	long numLinks = 0;
	for ( ; k ; k = info->getNextInlink(k) ) {
		ptrs[numLinks++] = k;
		if ( numLinks >= MAX_LINKERS ) break;
	}
	// sort them
	gbsort ( ptrs , numLinks , 4 , linkSiteRankCmp );
	// print xml starter
	if ( si->m_format == FORMAT_XML ) sb.safePrintf("\t\t<inlinks>\n");
	// loop through the inlinks
	bool printedInlinkText = false;
	bool firstTime = true;
	long inlinkId = 0;
	long long  starttime = gettimeofdayInMillisecondsLocal();

	//long icount = 0;
	//long ecount = 0;
	//long absSum = 0;
	for ( long i = 0 ; i < numLinks ; i++ ) {
		k = ptrs[i];
		if ( ! k->ptr_linkText ) continue;
		if ( ! si->m_doQueryHighlighting && 
		     si->m_format == FORMAT_HTML ) 
			continue;
		char *str   = k-> ptr_linkText;
		long strLen = k->size_linkText;
		//char tt[1024*3];
		//char *ttend = tt + 1024*3;
		char *frontTag = 
		     "<font style=\"color:black;background-color:yellow\">" ;
		char *backTag = "</font>";
		if ( si->m_format == FORMAT_XML ) {
			frontTag = "<b>";
			backTag  = "</b>";
		}
		Highlight hi;
		SafeBuf hb;
		long hlen = hi.set ( &hb,//tt , 
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
			sb.safePrintf("\t\t\t<inlink "
				      "docId=\"%lli\" "
				      "url=\"",
				      k->m_docId );
			// encode it for xml
			sb.htmlEncode ( k->ptr_urlBuf,
					k->size_urlBuf - 1 , false );
			sb.safePrintf("\" "
				      //"hostId=\"%lu\" "
				      "firstindexed=\"%lu\" "
				      // not accurate!
				      //"lastspidered=\"%lu\" "
				      "wordposstart=\"%li\" "
				      "id=\"%li\" "
				      "siterank=\"%li\" "
				      "text=\"",
				      //hh ,
				      //(long)k->m_datedbDate,
				      (unsigned long)k->m_firstIndexedDate,
				      //(unsigned long)k->m_lastSpidered,
				      (long)k->m_wordPosStart,
				      inlinkId,
				      //linkScore);
				      (long)k->m_siteRank
				      );
			// HACK!!!
			k->m_siteHash = inlinkId;
			// inc it
			inlinkId++;
			// encode it for xml
			if ( !sb.htmlEncode ( hb.getBufStart(),
					      hb.length(),
					      false)) 
				return false;
			sb.safePrintf("\"/>\n");
			continue;
		}


		if ( firstTime ) {
			sb.safePrintf("<font size=-1>");
			sb.safePrintf("<table border=1>"
				      "<tr><td colspan=3>"
				      "<center>"
				      "<b>Inlinks with Query Terms</b>"
				      "</center>"
				      "</td></tr>"
				      "<tr>"
				      "<td>Inlink Text</td>"
				      "<td>From</td>"
				      "<td>Site Rank</td>"
				      "</tr>"
				      );
		}
		firstTime = false;
		sb.safePrintf("<tr><td>"
			      "<a href=/get?c=%s&d=%lli&cnsp=0>"
			      //"<a href=\"/print?"
			      //"page=7&"
			      //"c=%s&"
			      //"d=%lli\">"
			      //k->ptr_urlBuf);
			      ,si->m_cr->m_coll
			      ,k->m_docId);
		if ( ! sb.safeMemcpy(&hb) ) return false;
		long hostLen = 0;
		char *host = getHostFast(k->ptr_urlBuf,&hostLen,NULL);
		sb.safePrintf("</td><td>");
		if ( host ) sb.safeMemcpy(host,hostLen);
		sb.safePrintf("</td><td>%li</td></tr>",(long)k->m_siteRank);
		//sb.safePrintf("<br>");
		printedInlinkText = true;
		*numPrinted = *numPrinted + 1;
	}

	long long took = gettimeofdayInMillisecondsLocal() - starttime;
        if ( took > 2 )
                log("timing: took %lli ms to highlight %li links."
                    ,took,numLinks);


	// closer for xml
	if ( si->m_format == FORMAT_XML ) sb.safePrintf("\t\t</inlinks>\n");
	//if ( printedInlinkText ) sb.safePrintf("<br>\n");
	if ( printedInlinkText )
		sb.safePrintf("</font>"
			      "</table>"
			      "<br>");
	return true;
}

//
// . print a dmoz topic for the given numeric catid UNDER search result
// . print "Search in Category" link as well
//
static bool printDMOZCategoryUnderResult ( SafeBuf &sb , 
					   SearchInput *si, 
					   long catid ,
					   State0 *st ) {

	uint8_t queryLanguage = langUnknown;
	// Don't print category if not in native language category
	// Note that this only trims out "World" cats, not all
	// of them. Some of them may still sneak in.
	if(si->m_langHint)
		queryLanguage = si->m_langHint;
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
	sb.safePrintf("<a href=\"/search?s=0&q=gbipcatid%%3A%li",catid);
	sb.urlEncode("|",1);
	sb.urlEncode(si->m_sbuf1.getBufStart(),si->m_sbuf1.length());
	sb.safePrintf("\">Search in Category</a>: ");
	
	// setup the host of the url
	//if ( dmozHost )
	//	sb.safePrintf("<a href=\"http://%s/", dmozHost );
	//else
	sb.safePrintf("<a href=\"/");
	// print link
	g_categories->printPathFromId(&sb, catid, true,si->m_isRTL);
	sb.safePrintf("/\">");
	// print the name of the dmoz category
	sb.safePrintf("<font color=#c62939>");
	g_categories->printPathFromId(&sb, catid, false,si->m_isRTL);
	sb.safePrintf("</font></a><br>");
	//++tr.brCount;
	return true;
}


// use this for xml as well as html
static bool printResult ( SafeBuf &sb,
			 State0 *st,
			 long ix , 
			 CollectionRec *cr ,
			 char *qe ) {

	// shortcuts
	SearchInput *si    = &st->m_si;
	Msg40       *msg40 = &st->m_msg40;

	// ensure not all cluster levels are invisible
	if ( si->m_debug )
		logf(LOG_DEBUG,"query: result #%li clusterlevel=%li",
		     ix, (long)msg40->getClusterLevel(ix));

	long long d = msg40->getDocId(ix);

	if ( si->m_docIdsOnly ) {
		if ( si->m_format == FORMAT_XML )
			sb.safePrintf("\t\t<docId>%lli</docId>\n"
				      "\t</result>\n", 
				      d );
		else
			sb.safePrintf("%lli<br/>\n", 
				      d );
		return true;
	}


	Msg20      *m20 = msg40->m_msg20[ix];
	Msg20Reply *mr  = m20->m_r;

	// . sometimes the msg20reply is NULL so prevent it coring
	// . i think this happens if all hosts in a shard are down or timeout
	//   or something
	if ( ! mr ) return false;

	// . if section voting info was request, display now, it's in json
	// . so if in csv it will mess things up!!!
	if ( mr->ptr_sectionVotingInfo )
		// it is possible this is just "\0"
		sb.safeStrcpy ( mr->ptr_sectionVotingInfo );

	// each "result" is the actual cached page, in this case, a json
	// object, because we were called with &icc=1. in that situation
	// ptr_content is set in the msg20reply.
	if ( si->m_format == FORMAT_CSV &&
	     mr->ptr_content &&
	     mr->m_contentType == CT_JSON ) {
		// parse it up
		char *json = mr->ptr_content;
		// only print header row once, so pass in that flag
		if ( ! st->m_printedHeaderRow ) {
			sb.reset();
			printCSVHeaderRow ( &sb , st );
			st->m_printedHeaderRow = true;
		}
		printJsonItemInCSV ( json , &sb , st );
		return true;
	}



	// just print cached web page?
	if ( mr->ptr_content ) {
		sb.safeStrcpy ( mr->ptr_content );
		//mr->size_content );
		if ( si->m_format == FORMAT_HTML )
			sb.safePrintf("\n\n<br><br>\n\n");
		if ( si->m_format != FORMAT_HTML )
			sb.safePrintf("\n\n");
		// just in case
		sb.nullTerm();
		return true;
	}


	if ( si->m_format == FORMAT_XML ) sb.safePrintf("\t<result>\n" );

	Highlight hi;

	// get the url
	char *url    = mr->ptr_ubuf      ;
	long  urlLen = mr->size_ubuf - 1 ;
	long  err    = mr->m_errno       ;

	// . remove any session ids from the url
	// . for speed reasons, only check if its a cgi url
	Url uu;
	uu.set ( url , urlLen, false, true );
	url    = uu.getUrl();
	urlLen = uu.getUrlLen();

	// get my site hash
	unsigned long long siteHash = 0;
	if ( uu.getHostLen() > 0 ) 
		siteHash = hash64(uu.getHost(),uu.getHostLen());
	// indent it if level is 2
	bool indent = false;

	bool isAdmin = si->m_isAdmin;
	if ( si->m_format == FORMAT_XML ) isAdmin = false;

	//unsigned long long lastSiteHash = siteHash;
	if ( indent && si->m_format == FORMAT_HTML ) 
		sb.safePrintf("<blockquote>"); 

	// print the rank. it starts at 0 so add 1
	if ( si->m_format == FORMAT_HTML )
		sb.safePrintf("<table><tr><td valign=top>%li.</td><td>",
			      ix+1 + si->m_firstResultNum );

	if ( si->m_showBanned ) {
		if ( err == EDOCBANNED   ) err = 0;
		if ( err == EDOCFILTERED ) err = 0;
	}

	// if this msg20 had an error print "had error"
	if ( err || urlLen <= 0 || ! url ) {
		// it's unprofessional to display this in browser
		// so just let admin see it
		if ( isAdmin ) {
			sb.safePrintf("<i>docId %lli had error: "
				      "%s</i><br><br>",
				      mr->m_docId,//msg40->getDocId(i),
				      mstrerror(err));
		}
		// log it too!
		log("query: docId %lli had error: %s.",
		    mr->m_docId,mstrerror(err));
		// wrap it up if clustered
		if ( indent ) sb.safeMemcpy("</blockquote>",13);
		return true;
	}
	

	// the score if admin
	/*
	if ( isAdmin ) {
		long level = (long)msg40->getClusterLevel(ix);
		// print out score
		sb.safePrintf ( "s=%.03f "
				"docid=%llu "
				"sitenuminlinks=%li%% "
				"hop=%li "
				"cluster=%li "
				"summaryLang=%s "
				"(%s)<br>",
				(float)msg40->getScore(ix) ,
				mr->m_docId,
				(long )mr->m_siteNumInlinks,
				(long)mr->m_hopcount,
				level ,
				getLanguageString(mr->m_summaryLanguage),
				g_crStrings[level]);
	}
	*/

	// print youtube and metacafe thumbnails here
	// http://www.youtube.com/watch?v=auQbi_fkdGE
	// http://img.youtube.com/vi/auQbi_fkdGE/2.jpg
	// get the thumbnail url
	if ( mr->ptr_imgUrl && si->m_format == FORMAT_HTML )
		sb.safePrintf ("<a href=%s><image src=%s></a>",
				   url,mr->ptr_imgUrl);
	// the a href tag
	if ( si->m_format == FORMAT_HTML ) sb.safePrintf ( "\n\n" );

	// then if it is banned 
	if ( mr->m_isBanned && si->m_format == FORMAT_HTML )
		sb.safePrintf("<font color=red><b>BANNED</b></font> ");

	///////
	//
	// PRINT THE TITLE
	//
	///////


	// the a href tag
	if ( si->m_format == FORMAT_HTML ) {
		sb.safePrintf ( "<a href=" );
		// print the url in the href tag
		sb.safeMemcpy ( url , urlLen ); 
		// then finish the a href tag and start a bold for title
		sb.safePrintf ( ">");//<font size=+0>" );
	}


	// . then the title  (should be NULL terminated)
	// . the title can be NULL
	// . highlight it first
	// . the title itself should not have any tags in it!
	char  *str  = mr->ptr_tbuf;//msg40->getTitle(i);
	long strLen = mr->size_tbuf - 1;// msg40->getTitleLen(i);
	if ( ! str || strLen < 0 ) strLen = 0;

	/////
	//
	// are we printing a dmoz category page?
	// get the appropriate dmoz title/summary to use since the same
	// url can exist in multiple topics (catIds) with different
	// titles summaries.
	//
	/////

	char *dmozSummary = NULL;
	// TODO: just get the catid from httprequest directly?
	if ( si->m_catId > 0 ) { // si->m_cat_dirId > 0) {
		// . get the dmoz title and summary
		// . if empty then just a bunch of \0s, except for catIds
	        Msg20Reply *mr = m20->getReply();
		char *dmozTitle  = mr->ptr_dmozTitles;
		dmozSummary = mr->ptr_dmozSumms;
		char *dmozAnchor = mr->ptr_dmozAnchors;
		long *catIds     = mr->ptr_catIds;
		long numCats = mr->size_catIds / 4;
		// loop through looking for the right ID
		for (long i = 0; i < numCats ; i++ ) {
			// assign shit if we match the dmoz cat we are showing
			if ( catIds[i] ==  si->m_catId) break;
			dmozTitle +=gbstrlen(dmozTitle)+1;
			dmozSummary +=gbstrlen(dmozSummary)+1;
			dmozAnchor += gbstrlen(dmozAnchor)+1;
		}
		// now make the title the dmoz title
		str = dmozTitle;
		strLen = gbstrlen(str);
	}
	


	long hlen;
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
	long cols = 80;
	if ( si->m_format == FORMAT_XML ) 
		sb.safePrintf("\t\t<title><![CDATA[");
	SafeBuf hb;
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
		//if (!sb.utf8Encode2(tt, hlen)) return false;
		if ( ! sb.brify ( hb.getBufStart(),
				  hb.getLength(),
				  0,
				  cols) ) return false;
	}
	else if ( str && strLen ) {
		// determine if TiTle wraps, if it does add a <br> count for
		// each wrap
		//if (!sb.utf8Encode2(str , strLen )) return false;
		if ( ! sb.brify ( str,strLen,0,cols) ) return false;
	}
	// . use "UNTITLED" if no title
	// . msg20 should supply the dmoz title if it can
	if ( strLen == 0 ) {
		if(!sb.safePrintf("<i>UNTITLED</i>"))
			return false;
	}
	// close up the title tag
	if ( si->m_format == FORMAT_XML ) sb.safePrintf("]]></title>\n");


	if ( si->m_format == FORMAT_HTML ) sb.safePrintf ("</a><br>\n" ) ;

	/////
	//
	// print content type after title
	//
	/////
	unsigned char ctype = mr->m_contentType;
	if ( ctype > 2 && ctype <= 13 ) {
		char *cs = g_contentTypeStrings[ctype];
		if ( si->m_format == FORMAT_XML )
			sb.safePrintf("\t\t<contentType>"
				      "<![CDATA["
				      "%s"
				      "]]>"
				      "</contentType>\n",
				      cs);
		else
			sb.safePrintf(" (%s) &nbsp;" ,cs);
	}

	////////////
	//
	// print the summary
	//
	////////////

	// . then the summary
	// . "s" is a string of null terminated strings
	char *send;
	// do the normal summary
	str    = mr->ptr_sum;
	strLen = mr->size_sum-1;
	// this includes the terminating \0 or \0\0 so back up
	if ( strLen < 0 ) strLen  = 0;
	send = str + strLen;

	// dmoz summary might override if we are showing a dmoz topic page
	if ( dmozSummary ) {
		str = dmozSummary;
		strLen = gbstrlen(dmozSummary);
	}

	if ( si->m_format == FORMAT_XML ) sb.safePrintf("\t\t<sum><![CDATA[");

	sb.brify ( str , strLen, 0 , cols ); // niceness = 0
	// close xml tag
	if ( si->m_format == FORMAT_XML ) sb.safePrintf("]]></sum>\n");
	// new line if not xml
	else if ( strLen ) sb.safePrintf("<br>\n");

	////////////
	//
	// . print DMOZ topics under the summary
	// . will print the "Search in Category" link too
	//
	////////////
	//Msg20Reply *mr = m20->getMsg20Reply();
	long nCatIds = mr->getNumCatIds();
	for (long i = 0; i < nCatIds; i++) {
		long catid = ((long *)(mr->ptr_catIds))[i];
		printDMOZCategoryUnderResult(sb,si,catid,st);
	}
	// skipCatsPrint:
	// print the indirect category Ids
	long nIndCatids = mr->size_indCatIds / 4;
	//if ( !cr->m_displayIndirectDmozCategories )
	//	goto skipCatsPrint2;
	for ( long i = 0; i < nIndCatids; i++ ) {
		long catid = ((long *)(mr->ptr_indCatIds))[i];
		// skip it if it's a regular category
		//bool skip = false;
		long d; for ( d = 0; d < nCatIds; d++) {
			if (  catid == mr->ptr_catIds[i] ) break;
		}
		// skip if the indirect catid matched a directed catid
		if ( d < nCatIds ) continue;
		// otherwise print it
		printDMOZCategoryUnderResult(sb,si,catid,st);
	}


	////////////
	//
	// print the URL
	//
	////////////
	// hack off the http:// if any for displaying it on screen
	if ( urlLen > 8 && strncmp ( url , "http://" , 7 )==0 ) {
		url += 7; urlLen -= 7; }
	// . remove trailing /
	// . only remove from root urls in case user cuts and 
	//   pastes it for link: search
	if ( url [ urlLen - 1 ] == '/' ) {
		// see if any other slash before us
		long j;
		for ( j = urlLen - 2 ; j >= 0 ; j-- )
			if ( url[j] == '/' ) break;
		// if there wasn't, we must have been a root url
		// so hack off the last slash
		if ( j < 0 ) urlLen--;
	}
	if ( si->m_format == FORMAT_HTML ) {
		sb.safePrintf ("<font color=gray>" );
		//sb.htmlEncode ( url , gbstrlen(url) , false );
		// 20 for the date after it
		sb.safeTruncateEllipsis ( url , cols - 30 );
		// turn off the color
		sb.safePrintf ( "</font>\n" );
	}
	if ( si->m_format == FORMAT_XML ) {
		sb.safePrintf("\t\t<url><![CDATA[");
		sb.safeMemcpy ( url , urlLen );
		sb.safePrintf("]]></url>\n");
	}


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
		sb.safePrintf ( "\t\t<size><![CDATA[%4.0fk]]></size>\n",
				(float)mr->m_contentLen/1024.0);
		// . docId for possible cached link
		// . might have merged a bunch together
		sb.safePrintf("\t\t<docId>%lli</docId>\n",mr->m_docId );
		// . show the site root
		// . for hompages.com/users/fred/mypage.html this will be
		//   homepages.com/users/fred/
		// . for www.xyz.edu/~foo/burp/ this will be
		//   www.xyz.edu/~foo/ etc.
		long  siteLen = 0;
		char *site = NULL;
		// seems like this isn't the way to do it, cuz Tagdb.cpp
		// adds the "site" tag itself and we do not always have it
		// in the XmlDoc::ptr_tagRec... so do it this way:
		site    = mr->ptr_site;
		siteLen = mr->size_site-1;
		//char *site=uu.getSite( &siteLen , si->m_coll, false, tagRec);
		sb.safePrintf("\t\t<site><![CDATA[");
		if ( site && siteLen > 0 ) sb.safeMemcpy ( site , siteLen );
		sb.safePrintf("]]></site>\n");
		//long sh = hash32 ( site , siteLen );
		//sb.safePrintf ("\t\t<siteHash32>%lu</siteHash32>\n",sh);
		//long dh = uu.getDomainHash32 ();
		//sb.safePrintf ("\t\t<domainHash32>%lu</domainHash32>\n",dh);
		// spider date
		sb.safePrintf ( "\t\t<spidered>%lu</spidered>\n",
				mr->m_lastSpidered);
		// backwards compatibility for buzz
		sb.safePrintf ( "\t\t<firstIndexedDateUTC>%lu"
				"</firstIndexedDateUTC>\n",
				mr->m_firstIndexedDate);
		// pub date
		long datedbDate = mr->m_datedbDate;
		// show the datedb date as "<pubDate>" for now
		if ( datedbDate != -1 )
			sb.safePrintf ( "\t\t<pubdate>%lu</pubdate>\n",
					datedbDate);
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
		sb.safePrintf("\t\t<outlink "
			      "docId=\"%lli\" "
			      "hostId=\"%lu\" "
			      "indexed=\"%li\" "
			      "pubdate=\"%li\" ",
			      k->m_docId ,
			      k->m_ip, // hostHash, but use ip for now
			      (long)k->m_firstIndexedDate ,
			      (long)k->m_datedbDate );
	if ( si->m_format == FORMAT_XML ) {
		// result
		sb.safePrintf("\t\t<language><![CDATA[%s]]>"
			      "</language>\n", 
			      getLanguageString(mr->m_language));
		
		char *charset = get_charset_str(mr->m_charset);
		if(charset)
			sb.safePrintf("\t\t<charset><![CDATA[%s]]>"
				      "</charset>\n", charset);
	}

	//
	// end more xml stuff
	//


	
	if ( isAdmin ) {
		long lang = mr->m_language;
		if ( lang ) sb.safePrintf(" - %s",getLanguageString(lang));
		uint16_t cc = mr->m_computedCountry;
		if( cc ) sb.safePrintf(" - %s", g_countryCode.getName(cc));
		char *charset = get_charset_str(mr->m_charset);
		if ( charset ) sb.safePrintf(" - %s ", charset);
	}

	if ( si->m_format == FORMAT_HTML ) sb.safePrintf("<br>\n");

	char *coll = si->m_cr->m_coll;

	// print the [cached] link?
	bool printCached = true;
	if ( mr->m_noArchive       ) printCached = false;
	if ( isAdmin               ) printCached = true;
	if ( mr->m_contentLen <= 0 ) printCached = false;
	if ( si->m_format == FORMAT_XML ) printCached = false;
	if ( printCached && cr->m_clickNScrollEnabled ) 
		sb.safePrintf ( " - <a href=/scroll.html?page="
				"get?"
				"q=%s&c=%s&d=%lli>"
				"cached</a>",
				qe , coll ,
				mr->m_docId );
	else if ( printCached )
		sb.safePrintf ( "<a href=\""
				"/get?"
				"q=%s&"
				"qlang=%s&"
				"c=%s&d=%lli&cnsp=0\">"
				"cached</a>", 
				qe , 
				// "qlang" parm
				si->m_defaultSortLanguage,
				coll , 
				mr->m_docId ); 

	// the new links
	if ( si->m_format == FORMAT_HTML && g_conf.m_isMattWells && 1 == 0 ) {
		//sb.safePrintf(" - <a href=\"/scoring?"
		//	      "c=%s&\">scoring</a>",
		//	      coll );
		//sb.safePrintf(" - <a href=\"/print?c=%s&",coll);
		if ( g_conf.m_isMattWells )
			sb.safePrintf(" - <a href=\"/seo?");//c=%s&",coll);
		else
			sb.safePrintf(" - <a href=\"https://www.gigablast."
				      "com/seo?");//c=%s&",coll);
		//sb.safePrintf("d=%lli",mr->m_docId);
		sb.safePrintf("u=");
		sb.urlEncode ( url , gbstrlen(url) , false );
		//sb.safePrintf("&page=1\">seo</a>" );
		sb.safePrintf("\"><font color=red>seo</font></a>" );
	}

	// only display re-spider link if addurl is enabled
	//if ( isAdmin &&
	//     g_conf.m_addUrlEnabled &&
	//     cr->m_addUrlEnabled      ) {
	/*
	if ( si->m_format == FORMAT_HTML ) {
		// the [respider] link
		// save this for seo iframe!
		sb.safePrintf (" - <a href=\"/admin/inject?u=" );
		// encode the url now
		sb.urlEncode ( url , urlLen );
		// then collection
		if ( coll ) {
			sb.safeMemcpy ( "&c=" , 3 );
			sb.safeMemcpy ( coll , gbstrlen(coll) );
		}
		//sb.safePrintf ( "&force=1\">reindex</a>" );
		sb.safePrintf ( "\">reindex</a>" );
	}
	*/

	// unhide the divs on click
	long placeHolder = -1;
	long placeHolderLen;
	if ( si->m_format == FORMAT_HTML ) {
		// place holder for backlink table link
		placeHolder = sb.length();
		sb.safePrintf (" - <a onclick="

			       "\""
			       "var e = document.getElementById('bl%li');"
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
			       "color:red;>"
			       "<u>00000 backlinks</u>"
			       "</a>"
			       , ix 
			       );
		placeHolderLen = sb.length() - placeHolder;
		// unhide the scoring table on click
		sb.safePrintf (" - <a onclick="

			       "\""
			       "var e = document.getElementById('sc%li');"
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
			       "color:red;>"
			       "<u>scoring</u>"
			       "</a>"
			       ,ix
			       );
		// reindex
		sb.safePrintf(" - <a style=color:red; href=\"/addurl?u=");
		sb.urlEncode ( url , gbstrlen(url) , false );
		unsigned long long rand64 = gettimeofdayInMillisecondsLocal();
		sb.safePrintf("&rand64=%llu\">respider</a>",rand64);
	}


	// this stuff is secret just for local guys!
	if ( si->m_format == FORMAT_HTML && ( isAdmin || cr->m_isCustomCrawl)){
		// now the ip of url
		//long urlip = msg40->getIp(i);
		// don't combine this with the sprintf above cuz
		// iptoa uses a static local buffer like ctime()
		sb.safePrintf(//"<br>"
			      " &nbsp; - &nbsp; <a href=\"/search?"
			      "c=%s&sc=1&dr=0&q=ip:%s&"
			      "n=100&usecache=0\">%s</a>",
			      coll,iptoa(mr->m_ip), iptoa(mr->m_ip) );
		// ip domain link
		unsigned char *us = (unsigned char *)&mr->m_ip;//urlip;
		sb.safePrintf (" - <a href=\"/search?c=%s&sc=1&dr=0&n=100&"
					       "q=ip:%li.%li.%li&"
			       "usecache=0\">%li.%li.%li</a>",
			       coll,
			       (long)us[0],(long)us[1],(long)us[2],
			       (long)us[0],(long)us[1],(long)us[2]);

		/*
		// . now the info link
		// . if it's local, don't put the hostname/port in
		//   there cuz it will mess up Global Spec's machine
		//if ( h->m_groupId == g_hostdb.m_groupId ) 
		sb.safePrintf(" - <a href=\"/master/titledb?c=%s&"
			      "d=%lli",coll,mr->m_docId);
		// then the [info] link to show the TitleRec
		sb.safePrintf ( "\">[info]</a>" );
		
		// now the analyze link
		sb.safePrintf (" - <a href=\"/master/parser?c=%s&"
			       "old=1&hc=%li&u=", 
			       coll,
			       (long)mr->m_hopcount);
		// encode the url now
		sb.urlEncode ( url , urlLen );
		// then the [analyze] link
		sb.safePrintf ("\">[analyze]</a>" );
		
		// and links: query link
		sb.safePrintf( " - <a href=\"/search?c=%s&dr=0&"
			       "n=100&q=links:",coll);
		// encode the url now
		sb.urlEncode ( url , urlLen );
		sb.safeMemcpy ("\">linkers</a>" , 14 ); 
		*/
	}

	// admin always gets the site: option so he can ban
	if ( si->m_format == FORMAT_HTML && ( isAdmin || cr->m_isCustomCrawl)){
		char dbuf [ MAX_URL_LEN ];
		long dlen = uu.getDomainLen();
		memcpy ( dbuf , uu.getDomain() , dlen );
		dbuf [ dlen ] = '\0';
		// newspaperarchive urls have no domain
		if ( dlen == 0 ) {
			dlen = uu.getHostLen();
			memcpy ( dbuf , uu.getHost() , dlen );
			dbuf [ dlen ] = '\0';
		}
		sb.safePrintf (" - "
			       " <a href=\"/search?"
			       "q=site%%3A%s&sc=0&c=%s\">"
			       "%s</a> " ,
			       dbuf ,
			       coll , dbuf );
		char *un = "";
		long  banVal = 1;
		if ( mr->m_isBanned ) {
			un = "UN";
			banVal = 0;
		}
		sb.safePrintf(" - "
			      " <a href=\"/master/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=%li&"
			      "u=%s&c=%s\">"
			      "<nobr><b>%sBAN %s</b>"
			      "</nobr></a> "
			      , banVal
			      , dbuf
			      , coll
			      , un
			      , dbuf );
		//banSites->safePrintf("%s+", dbuf);
		dlen = uu.getHostLen();
		memcpy ( dbuf , uu.getHost() , dlen );
		dbuf [ dlen ] = '\0';
		sb.safePrintf(" - "
			      " <a href=\"/master/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=%li&"
			      "u=%s&c=%s\">"
			      "<nobr>%sBAN %s</nobr></a> "
			      , banVal
			      , dbuf
			      , coll
			      , un
			      , dbuf
			      );
		// take similarity out until working again
		/*
		sb.safePrintf (" - [similar -"
			       " <a href=\"/search?"
			       "q="
			       "gbtagvector%%3A%lu"
			       "&sc=1&dr=0&c=%s&n=100"
			       "&rcache=0\">"
			       "tag</a> " ,
			       (long)mr->m_tagVectorHash,  coll);
		sb.safePrintf ("<a href=\"/search?"
			       "q="
			       "gbgigabitvector%%3A%lu"
			       "&sc=1&dr=0&c=%s&n=100"
			       "&rcache=0\">"
			       "topic</a> " ,
			       (long)mr->m_gigabitVectorHash, coll);
		*/
		if ( mr->size_gbAdIds > 0 ) 
			sb.safePrintf ("<a href=\"/search?"
				       "q=%s"
				       "&sc=1&dr=0&c=%s&n=200&rat=0\">"
				       "Ad Id</a> " ,
				       mr->ptr_gbAdIds,  coll);
		
		//sb.safePrintf ("] ");
		
		long urlFilterNum = (long)mr->m_urlFilterNum;
		if(urlFilterNum != -1) {
			sb.safePrintf (" - <a href=/admin/filters?c=%s>"
				       "UrlFilter</a>:%li", 
				       coll ,
				       urlFilterNum);
		}					

		
	}

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
			"SCORE = (%li - |pos1-pos2|) * "
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
			, (long)MAXWORDPOS+1
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
	for ( long i = 0 ; i < HASHGROUP_END ; i++ ) {
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
	for ( long i = 0 ; i < MAXDENSITYRANK ; i++ ) {
		help.safePrintf("<tr>"
				"<td>%li</td>"
				"<td>%li</td>"
				"<td>%.0f</td>"
				"</tr>"
				,maxw,i,dweight );
	}
	help.safePrintf("</table>");
	help.safePrintf("<br><br>"
	*/
		

	if ( si->m_format == FORMAT_HTML )
		sb.safePrintf ( "<br><br>\n");


	// done?
	DocIdScore *dp = msg40->getScoreInfo(ix);
	if ( ! dp ) {
		if ( si->m_format == FORMAT_XML ) 
			sb.safePrintf ("\t</result>\n\n");
		// wtf?
		//char *xx=NULL;*xx=0;
		// at least close up the table
		if ( si->m_format == FORMAT_HTML ) 
			sb.safePrintf("</table>\n");
		return true;
	}


	//
	// scoring info tables
	//

	long nr = dp->m_numRequiredTerms;
	if ( nr == 1 ) nr = 0;
	// print breakout tables here for distance matrix
	//SafeBuf bt;
	// final score calc
	SafeBuf ft;
	// shortcut
	//Query *q = si->m_q;

	// put in a hidden div so you can unhide it
	if ( si->m_format == FORMAT_HTML )
		sb.safePrintf("<div id=bl%li style=display:none;>\n", ix );

	// print xml and html inlinks
	long numInlinks;
	printInlinkText ( sb , mr , si , &numInlinks );


	if ( si->m_format == FORMAT_HTML ) {
		sb.safePrintf("</div>");
		sb.safePrintf("<div id=sc%li style=display:none;>\n", ix );
	}


	// if pair changes then display the sum
	long lastTermNum1 = -1;
	long lastTermNum2 = -1;

	float minScore = -1;

	// display all the PairScores
	for ( long i = 0 ; i < dp->m_numPairs ; i++ ) {
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
		for ( long j = i ; j < dp->m_numPairs ; j++ ) {
			// get it
			PairScore *ps = &dp->m_pairScores[j];
			// stop if different pair now
			if ( ps->m_qtermNum1 != fps->m_qtermNum1 ) break;
			if ( ps->m_qtermNum2 != fps->m_qtermNum2 ) break;
			// skip if 0. neighborhood terms have weight of 0 now
			if ( ps->m_finalScore == 0.0 ) continue;
			// first time?
			if ( firstTime && si->m_format == FORMAT_HTML ) {
				Query *q = si->m_q;
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
		if ( si->m_format == FORMAT_XML ) continue;
		//sb.safePrintf("<table border=1><tr><td><center><b>");
		// print pair text
		//long qtn1 = fps->m_qtermNum1;
		//long qtn2 = fps->m_qtermNum2;
		//if ( q->m_qterms[qtn1].m_isPhrase )
		//	sb.pushChar('\"');
		//sb.safeMemcpy ( q->m_qterms[qtn1].m_term ,
		//		q->m_qterms[qtn1].m_termLen );
		//if ( q->m_qterms[qtn1].m_isPhrase )
		//	sb.pushChar('\"');
		//sb.safePrintf("</b> vs <b>");
		//if ( q->m_qterms[qtn2].m_isPhrase )
		//	sb.pushChar('\"');
		//sb.safeMemcpy ( q->m_qterms[qtn2].m_term ,
		//		q->m_qterms[qtn2].m_termLen );
		//if ( q->m_qterms[qtn2].m_isPhrase )
		//	sb.pushChar('\"');

		sb.safePrintf("<tr><td><b>%.04f</b></td>"
			      "<td colspan=20>total of above scores</td>"
			      "</tr>",
			      totalPairScore);
		// close table from printScoresHeader
		if ( ! firstTime ) sb.safePrintf("</table><br>");
	}


	

	// close the distance table
	//if ( nr ) sb.safePrintf("</table>");
	// print the breakout tables
	//if ( nr ) {
	//	//sb.safePrintf("<br>");
	//	sb.safeMemcpy ( &bt );
	//}

	// the singles --- TODO: make it ALL query terms
	//nr = dp->m_numRequiredTerms;
	//for ( long i = 0 ; i < nr && nr == 1 ; i++ ) {

	long lastTermNum = -1;

	long numSingles = dp->m_numSingles;
	// do not print this if we got pairs
	if ( dp->m_numPairs ) numSingles = 0;

	for ( long i = 0 ; i < numSingles ; i++ ) {
		float totalSingleScore = 0.0;
		// print all the top winners for this single
		SingleScore *fss = &dp->m_singleScores[i];
		// if same combo as last time skip
		if ( fss->m_qtermNum == lastTermNum ) continue;
		// do not reprint for this query term num
		lastTermNum = fss->m_qtermNum;
		bool firstTime = true;
		// print all singles for this combo
		for ( long j = i ; j < dp->m_numSingles ; j++ ) {
			// get it
			SingleScore *ss = &dp->m_singleScores[j];
			// stop if different single now
			if ( ss->m_qtermNum != fss->m_qtermNum ) break;
			// skip if 0. skip neighborhoods i guess
			if ( ss->m_finalScore == 0.0 ) continue;
			// first time?
			if ( firstTime && si->m_format == FORMAT_HTML ) {
				Query *q = si->m_q;
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
		if ( si->m_format == FORMAT_XML ) continue;
		//sb.safePrintf("<table border=1><tr><td><center><b>");
		// print pair text
		//long qtn = fss->m_qtermNum;
		//sb.safeMemcpy(q->m_qterms[qtn].m_term ,
		//	      q->m_qterms[qtn].m_termLen );
		//sb.safePrintf("</b></center></td></tr>");
		sb.safePrintf("<tr><td><b>%.04f</b></td>"
			      "<td colspan=20>total of above scores</td>"
			      "</tr>",
			      totalSingleScore);
		// close table from printScoresHeader
		if ( ! firstTime ) sb.safePrintf("</table><br>");
	}


	char *ff = "";
	if ( si->m_useMinAlgo ) ff = "MIN ";
	char *ff2 = "sum";
	if ( si->m_useMinAlgo ) ff2 = "min";
	//if ( nr ) sb.safePrintf("</table>");
	//sb.safePrintf("<br>");
	// final score!!!
	if ( si->m_format == FORMAT_XML ) {
		sb.safePrintf ("\t\t<siteRank>%li</siteRank>\n",
			       (long)dp->m_siteRank );

		sb.safePrintf ("\t\t<numGoodSiteInlinks>%li"
			       "</numGoodSiteInlinks>\n",
			       (long)mr->m_siteNumInlinks );

		sb.safePrintf ("\t\t<numTotalSiteInlinks>%li"
			       "</numTotalSiteInlinks>\n",
			       (long)mr->m_siteNumInlinksTotal );
		sb.safePrintf ("\t\t<numUniqueIpsLinkingToSite>%li"
			       "</numUniqueIpsLinkingToSite>\n",
			       (long)mr->m_siteNumUniqueIps );
		sb.safePrintf ("\t\t<numUniqueCBlocksLinkingToSite>%li"
			       "</numUniqueCBlocksLinkingToSite>\n",
			       (long)mr->m_siteNumUniqueCBlocks );


		struct tm *timeStruct3 = gmtime(&mr->m_pageInlinksLastUpdated);
		char tmp3[64];
		strftime ( tmp3 , 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct3 );
		// -1 means unknown
		if ( mr->m_pageNumInlinks >= 0 )
			// how many inlinks, external and internal, we have
			// to this page not filtered in any way!!!
			sb.safePrintf("\t\t<numTotalPageInlinks>%li"
				      "</numTotalPageInlinks>\n"
				      ,mr->m_pageNumInlinks
				      );
		// how many inlinking ips we got, including our own if
		// we link to ourself
		sb.safePrintf("\t\t<numUniqueIpsLinkingToPage>%li"
			      "</numUniqueIpsLinkingToPage>\n"
			      ,mr->m_pageNumUniqueIps
			      );
		// how many inlinking cblocks we got, including our own if
		// we link to ourself
		sb.safePrintf("\t\t<numUniqueCBlocksLinkingToPage>%li"
			      "</numUniqueCBlocksLinkingToPage>\n"
			      ,mr->m_pageNumUniqueCBlocks
			      );
		
		// how many "good" inlinks. i.e. inlinks whose linktext we
		// count and index.
		sb.safePrintf("\t\t<numGoodPageInlinks>%li"
			      "</numGoodPageInlinks>\n"
			      "\t\t<pageInlinksLastComputedUTC>%lu"
			      "</pageInlinksLastComputedUTC>\n"
			      ,mr->m_pageNumGoodInlinks
			      ,mr->m_pageInlinksLastUpdated
			      );


		float score    = msg40->getScore   (ix);
		sb.safePrintf("\t\t<finalScore>%f</finalScore>\n", score );
		sb.safePrintf ("\t\t<finalScoreEquationCanonical>"
			       "<![CDATA["
			       "Final Score = (siteRank/%.01f+1) * "
			       "(%.01f [if not foreign language]) * "
			       "(%s of above matrix scores)"
			       "]]>"
			       "</finalScoreEquationCanonical>\n"
			       , SITERANKDIVISOR
			       , SAMELANGMULT
			       , ff2 
			       );
		sb.safePrintf ("\t\t<finalScoreEquation>"
			       "<![CDATA["
			       "<b>%.03f</b> = (%li/%.01f+1) " // * %s("
			       , dp->m_finalScore
			       , (long)dp->m_siteRank
			       , SITERANKDIVISOR
			       //, ff
			       );
		// then language weight
		if ( si->m_queryLang == 0 || 
		     mr->m_language == 0 ||
		     si->m_queryLang == mr->m_language )
			sb.safePrintf(" * %.01f",
				      SAMELANGMULT);//FOREIGNLANGDIVISOR);
		// the actual min then
		sb.safePrintf(" * %.03f",minScore);
		// no longer list all the scores
		//sb.safeMemcpy ( &ft );
		sb.safePrintf(//")"
			      "]]>"
			      "</finalScoreEquation>\n");
		sb.safePrintf ("\t</result>\n\n");
		return true;
	}

	char *cc = getCountryCode ( mr->m_country );
	if ( mr->m_country == 0 ) cc = "Unknown";

	sb.safePrintf("<table border=1>"

		      "<tr><td colspan=10><b><center>"
		      "final score</center></b>"
		      "</td></tr>"

		      "<tr>"
		      "<td>docId</td>"
		      "<td>%lli</td>"
		      "</tr>"

		      "<tr>"
		      "<td>site</td>"
		      "<td>%s</td>"
		      "</tr>"

		      "<tr>"
		      "<td>hopcount</td>"
		      "<td>%li</td>"
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
		      "<td><font color=blue>%li</font></td>"
		      "</tr>"

		      "<tr><td colspan=100>"
		      , dp->m_docId
		      , mr->ptr_site
		      , (long)mr->m_hopcount
		      //, getLanguageString(mr->m_summaryLanguage)
		      , getLanguageString(mr->m_language) // use page language
		      , cc
		      , (long)dp->m_siteRank
		      );

	// list all final scores starting with pairs
	sb.safePrintf("<b>%f</b> = "
		      "(<font color=blue>%li</font>/%.01f+1)"
		      , dp->m_finalScore
		      , (long)dp->m_siteRank
		      , SITERANKDIVISOR
		      );

	// if lang is different
	if ( si->m_queryLang == 0 || 
	     mr->m_language == 0 ||
	     si->m_queryLang == mr->m_language )
		sb.safePrintf(" * <font color=green><b>%.01f</b></font>",
			      SAMELANGMULT);//FOREIGNLANGDIVISOR);

	// list all final scores starting with pairs
	sb.safePrintf(" * %s("
		      , ff
		      );
	sb.safeMemcpy ( &ft );
	sb.safePrintf(")</td></tr></table><br>");

	// put in a hidden div so you can unhide it
	sb.safePrintf("</div>\n");

	// result is in a table so we can put the result # in its own column
	sb.safePrintf("</td></tr></table>");


	/*
	// UN-indent it if level is 1
	if ( si->m_format == FORMAT_HTML && si->m_doIpClustering ) {
		sb.safePrintf (" - [ <a href=\"/search?"
			       "q=%%2Bip%%3A%s+%s&sc=0&c=%s\">"
			       "More from this ip</a> ]",
			       iptoa ( mr->m_ip ) ,
			       qe , coll );
		if ( indent ) sb.safePrintf ( "</blockquote><br>\n");
		else sb.safePrintf ( "<br><br>\n");
	}
	else if ( si->m_format == FORMAT_HTML && si->m_doSiteClustering ) {
		char hbuf [ MAX_URL_LEN ];
		long hlen = uu.getHostLen();
		memcpy ( hbuf , uu.getHost() , hlen );
		hbuf [ hlen ] = '\0';
		sb.safePrintf (" - <nobr><a href=\"/search?"
			       "q=%%2Bsite%%3A%s+%s&sc=0&c=%s\">"
			       "More from this site</a></nobr>",
			       hbuf ,
			       qe , coll );
		if ( indent ) sb.safePrintf ( "</blockquote><br>\n");
		else sb.safePrintf ( "<br><br>\n");
	}
	*/

	// space out 0000 backlinks
	char *p = sb.getBufStart() + placeHolder;
	long plen = placeHolderLen;
	if ( numInlinks == 0 ) 
		memset ( p , ' ' , plen );
	if ( numInlinks > 0 && numInlinks < 99999 ) {
		char *ss = strstr ( p, "00000" );
		char c = ss[5];
		sprintf(ss,"%5li",numInlinks);
		ss[5] = c;
	}
	// print "1 backlink" not "1 backlinks"
	if ( numInlinks == 1 ) {
		char *xx = strstr(p,"backlinks");
		xx[8] = ' ';
	}

	return true;
}




bool printPairScore ( SafeBuf &sb , SearchInput *si , PairScore *ps ,
		      Msg20Reply *mr , Msg40 *msg40 , bool first ) {

	// shortcut
	Query *q = si->m_q;

	//SafeBuf ft;

	// store in final score calc
	//if ( ft.length() ) ft.safePrintf(" + ");
	//ft.safePrintf("%f",ps->m_finalScore);
	
	long qtn1 = ps->m_qtermNum1;
	long qtn2 = ps->m_qtermNum2;
	
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
	
	long hg1 = ps->m_hashGroup1;
	long hg2 = ps->m_hashGroup2;
	
	
	float hgw1 = getHashGroupWeight(hg1);
	float hgw2 = getHashGroupWeight(hg2);
	
	long wp1 = ps->m_wordPos1;
	long wp2 = ps->m_wordPos2;
	
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
	
	//long long sz1 = ps->m_listSize1;
	//long long sz2 = ps->m_listSize2;
	//long long tf1 = ps->m_termFreq1;//sz1 / 10;
	//long long tf2 = ps->m_termFreq2;//sz2 / 10;
	long long tf1 = msg40->m_msg3a.m_termFreqs[qtn1];
	long long tf2 = msg40->m_msg3a.m_termFreqs[qtn2];
	float tfw1 = ps->m_tfWeight1;
	float tfw2 = ps->m_tfWeight2;
	
	char *wp = "no";
	float wiw = 1.0;
	if ( ps->m_inSameWikiPhrase ) {
		wp = "yes";
		wiw = WIKI_WEIGHT; // 0.50;
	}
	long a = ps->m_wordPos2;
	long b = ps->m_wordPos1;
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
		sb.safePrintf("\t\t<pairInfo>\n");
		
		
		/*
		  sb.safePrintf("\t\t\t<diversityRankLeft1>%li"
		  "</diversityRankLeft1>\n",
		  (long)drl1);
		  sb.safePrintf("\t\t\t<diversityRankRight1>%li"
		  "</diversityRankRight1>\n",
		  (long)drr1);
		  sb.safePrintf("\t\t\t<diversityWeightLeft1>%f"
		  "</diversityWeightLeft1>\n",
		  dvwl1);
		  sb.safePrintf("\t\t\t<diversityWeightRight1>%f"
		  "</diversityWeightRight1>\n",
		  dvwr1);
		  
		  
		  sb.safePrintf("\t\t\t<diversityRankLeft2>%li"
		  "</diversityRankLeft2>\n",
		  (long)drl2);
		  sb.safePrintf("\t\t\t<diversityRankRight2>%li"
		  "</diversityRankRight2>\n",
		  (long)drr2);
		  sb.safePrintf("\t\t\t<diversityWeightLeft2>%f"
		  "</diversityWeightLeft2>\n",
		  dvwl2);
		  sb.safePrintf("\t\t\t<diversityWeightRight2>%f"
		  "</diversityWeightRight2>\n",
		  dvwr2);
		*/
		
		sb.safePrintf("\t\t\t<densityRank1>%li"
			      "</densityRank1>\n",
			      (long)de1);
		sb.safePrintf("\t\t\t<densityRank2>%li"
			      "</densityRank2>\n",
			      (long)de2);
		sb.safePrintf("\t\t\t<densityWeight1>%f"
			      "</densityWeight1>\n",
			      dnw1);
		sb.safePrintf("\t\t\t<densityWeight2>%f"
			      "</densityWeight2>\n",
			      dnw2);
		
		sb.safePrintf("\t\t\t<term1><![CDATA[");
		sb.safeMemcpy ( q->m_qterms[qtn1].m_term ,
				q->m_qterms[qtn1].m_termLen );
		sb.safePrintf("]]></term1>\n");
		sb.safePrintf("\t\t\t<term2><![CDATA[");
		sb.safeMemcpy ( q->m_qterms[qtn2].m_term ,
				q->m_qterms[qtn2].m_termLen );
		sb.safePrintf("]]></term2>\n");
		
		sb.safePrintf("\t\t\t<location1><![CDATA[%s]]>"
			      "</location1>\n",
			      getHashGroupString(hg1));
		sb.safePrintf("\t\t\t<location2><![CDATA[%s]]>"
			      "</location2>\n",
			      getHashGroupString(hg2));
		sb.safePrintf("\t\t\t<locationWeight1>%.01f"
			      "</locationWeight1>\n",
			      hgw1 );
		sb.safePrintf("\t\t\t<locationWeight2>%.01f"
			      "</locationWeight2>\n",
			      hgw2 );
		
		sb.safePrintf("\t\t\t<wordPos1>%li"
			      "</wordPos1>\n", wp1 );
		sb.safePrintf("\t\t\t<wordPos2>%li"
			      "</wordPos2>\n", wp2 );
		//long wordDist = wp2 - wp1;
		//if ( wordDist < 0 ) wordDist *= -1;
		//sb.safePrintf("\t\t\t<wordDist>%li"
		//	      "</wordDist>\n",wdist);
		
		sb.safePrintf("\t\t\t<isSynonym1>"
			      "<![CDATA[%s]]>"
			      "</isSynonym1>\n",
			      syn1);
		sb.safePrintf("\t\t\t<isSynonym2>"
			      "<![CDATA[%s]]>"
			      "</isSynonym2>\n",
			      syn2);
		sb.safePrintf("\t\t\t<synonymWeight1>%.01f"
			      "</synonymWeight1>\n",
			      sw1);
		sb.safePrintf("\t\t\t<synonymWeight2>%.01f"
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
		sb.safePrintf("\t\t\t<%s>%li</%s>\n",
			      r1,(long)wr1,r1);
		sb.safePrintf("\t\t\t<%s>%li</%s>\n",
			      r2,(long)wr2,r2);
		sb.safePrintf("\t\t\t<%s>%.02f</%s>\n",
			      t1,wsw1,t1);
		sb.safePrintf("\t\t\t<%s>%.02f</%s>\n",
			      t2,wsw2,t2);
		

		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		for (;k&&hg1==HASHGROUP_INLINKTEXT ; k=info->getNextInlink(k)){
			if ( ! k->ptr_linkText ) continue;
			if ( k->m_wordPosStart > wp1 ) continue;
			if ( k->m_wordPosStart + 50 < wp1 ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb.safePrintf("\t\t\t<inlinkId1>%li"
				      "</inlinkId1>\n",
				      k->m_siteHash);
		}

		k = info->getNextInlink(NULL);
		for (;k&&hg2==HASHGROUP_INLINKTEXT ; k=info->getNextInlink(k)){
			if ( ! k->ptr_linkText ) continue;
			if ( k->m_wordPosStart > wp2 ) continue;
			if ( k->m_wordPosStart + 50 < wp2 ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb.safePrintf("\t\t\t<inlinkId2>%li"
				      "</inlinkId2>\n",
				      k->m_siteHash);
		}




		// term freq
		sb.safePrintf("\t\t\t<termFreq1>%lli"
			      "</termFreq1>\n",tf1);
		sb.safePrintf("\t\t\t<termFreq2>%lli"
			      "</termFreq2>\n",tf2);
		sb.safePrintf("\t\t\t<termFreqWeight1>%f"
			      "</termFreqWeight1>\n",tfw1);
		sb.safePrintf("\t\t\t<termFreqWeight2>%f"
			      "</termFreqWeight2>\n",tfw2);
		
		sb.safePrintf("\t\t\t<isWikiBigram1>"
			      "%li</isWikiBigram1>\n",
			      (long)(ps->m_isHalfStopWikiBigram1));
		sb.safePrintf("\t\t\t<isWikiBigram2>"
			      "%li</isWikiBigram2>\n",
			      (long)(ps->m_isHalfStopWikiBigram2));
		
		sb.safePrintf("\t\t\t<wikiBigramWeight1>%.01f"
			      "</wikiBigramWeight1>\n",
			      wbw1);
		sb.safePrintf("\t\t\t<wikiBigramWeight2>%.01f"
			      "</wikiBigramWeight2>\n",
			      wbw2);
		
		sb.safePrintf("\t\t\t<inSameWikiPhrase>"
			      "<![CDATA[%s]]>"
			      "</inSameWikiPhrase>\n",
			      wp);
		
		sb.safePrintf("\t\t\t<queryDist>"
			      "%li"
			      "</queryDist>\n",
			      ps->m_qdist );
		
		sb.safePrintf("\t\t\t<wikiWeight>"
			      "%.01f"
			      "</wikiWeight>\n",
			      wiw );
		
		sb.safePrintf("\t\t\t<score>%f</score>\n",
			      ps->m_finalScore);
		
		sb.safePrintf("\t\t\t<equationCanonical>"
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
		
		sb.safePrintf("\t\t\t<equation>"
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
			sb.safePrintf(
				      "/<b>%li</b> "
				      , (long)FIXED_DISTANCE );
		else
			sb.safePrintf(
				      "/"
				      "(((<font color=darkgreen>%li</font>"
				      "-<font color=darkgreen>%li</font>"
				      ")-<font color=lime>%li</font>)+1.0%s)"
				      ,
				      a,b,ps->m_qdist,bes);
		// wikipedia weight
		if ( wiw != 1.0 )
			sb.safePrintf("*%.01f", wiw );
		sb.safePrintf("]]>"
			      "</equation>\n" );
		sb.safePrintf("\t\t</pairInfo>\n");
		return true; // continue;
	}
	
	
	// print out the entire details i guess
	//sb.safePrintf("<td>%.02f</td>"
	//	      ,ps->m_finalScore
	//	      );
	// . print out the breakout tables then
	// . they should pop-up when the user 
	//   mouses over a cell in the distance matrix
	//sb.safePrintf("<table border=1>"
	//	      "<tr><td colspan=100>"
	//	      "<center><b>");
	//if ( q->m_qterms[qtn1].m_isPhrase )
	//	sb.pushChar('\"');
	//sb.safeMemcpy ( q->m_qterms[qtn1].m_term ,
	//		q->m_qterms[qtn1].m_termLen );
	//if ( q->m_qterms[qtn1].m_isPhrase )
	//	sb.pushChar('\"');
	//sb.safePrintf("</b> vs <b>");
	//if ( q->m_qterms[qtn2].m_isPhrase )
	//	sb.pushChar('\"');
	//sb.safeMemcpy ( q->m_qterms[qtn2].m_term ,
	//		q->m_qterms[qtn2].m_termLen );
	//if ( q->m_qterms[qtn2].m_isPhrase )
	//	sb.pushChar('\"');
	//sb.safePrintf("</b></center></td></tr>");
	// then print the details just like the
	// single term table below
	//sb.safePrintf("<tr>"
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
	sb.safePrintf("<tr><td rowspan=3>");

	sb.safePrintf("<a onclick=\""
		      "var e = document.getElementById('poo');"
		      "if ( e.style.display == 'none' ){"
		      "e.style.display = '';"
		      "}"
		      "else {"
		      "e.style.display = 'none';"
		      "}"
		      "\">"
		      );
	sb.safePrintf("%.04f</a></td>",ps->m_finalScore);
	//sb.safeMemcpy ( q->m_qterms[qtn1].m_term ,
	//		q->m_qterms[qtn1].m_termLen );
	//sb.safePrintf("</td>");
	sb.safePrintf("<td>"
		      "%s <font color=orange>"
		      "%.01f</font></td>"
		      , getHashGroupString(hg1)
		      , hgw1 );
	// the word position
	sb.safePrintf("<td>");
		      //"<a href=\"/print?d="
		      //"&page=4&recycle=1&"

	if ( g_conf.m_isMattWells )
		sb.safePrintf("<a href=\"/seo?d=");
	else
		sb.safePrintf("<a href=\"https://www.gigablast.com/seo?d=");

	sb.safePrintf("%lli"
		      "&page=sections&"
		      "hipos=%li&c=%s\">"
		      "%li</a></td>"
		      "</a></td>"
		      ,mr->m_docId
		      ,(long)ps->m_wordPos1
		      ,si->m_cr->m_coll
		      ,(long)ps->m_wordPos1);
	// is synonym?
	//if ( sw1 != 1.00 )
		sb.safePrintf("<td>%s <font color=blue>%.02f"
			      "</font></td>",syn1,sw1);
	//else
	//	sb.safePrintf("<td>&nbsp;</td>");

	
	// wikibigram?/weight
	//if ( wbw1 != 1.0 )
		sb.safePrintf("<td>%s <font color=green>%.02f"
			      "</font></td>",bs1,wbw1);
	//else
	//	sb.safePrintf("<td>&nbsp;</td>");

	
	// diversity -
	// not needed for term pair algo
	//sb.safePrintf("<td>%li/<font color=green>"
	//	      "%f</font></td>",
	//	      (long)dr1,dvw1);
	
	// density
	sb.safePrintf("<td>%li <font color=purple>"
		      "%.02f</font></td>",
		      (long)de1,dnw1);
	// word spam
	if ( hg1 == HASHGROUP_INLINKTEXT ) {
		sb.safePrintf("<td>&nbsp;</td>");
		sb.safePrintf("<td>%li <font color=red>"
			      "%.02f</font></td>",
			      (long)wr1,wsw1);
	}
	else {
		sb.safePrintf("<td>%li", (long)wr1);
		//if ( wsw1 != 1.0 )
			sb.safePrintf( " <font color=red>"
				       "%.02f</font>",  wsw1);
		sb.safePrintf("</td>");
		sb.safePrintf("<td>&nbsp;</td>");
	}
	
	// term freq
	sb.safePrintf("<td>%lli <font color=magenta>"
		      "%.02f</font></td>",
		      tf1,tfw1);
	// insamewikiphrase?
	sb.safePrintf("<td>%s %li/%.01f</td>",
		      wp,ps->m_qdist,wiw);
	// end the row
	sb.safePrintf("</tr>");
	//
	// print 2nd term in 2nd row
	//
	sb.safePrintf("<tr><td>");
	//sb.safeMemcpy ( q->m_qterms[qtn2].m_term ,
	//		q->m_qterms[qtn2].m_termLen );
	//sb.safePrintf("</td>");
	sb.safePrintf(//"<td>"
		      "%s <font color=orange>"
		      "%.01f</font></td>"
		      , getHashGroupString(hg2)
		      , hgw2 );
	// the word position
	sb.safePrintf("<td>");
		      //"<a href=\"/print?d="
		      //"%lli"
		      //"&page=4&recycle=1&"

	if ( g_conf.m_isMattWells )
		sb.safePrintf("<a href=\"/seo?d=");
	else
		sb.safePrintf("<a href=\"https://www.gigablast.com/seo?d=");

	sb.safePrintf("%lli"
		      "&page=sections&"
		      "hipos=%li&c=%s\">"
		      "%li</a></td>"
		      "</a></td>"
		      ,mr->m_docId
		      ,(long)ps->m_wordPos2
		      ,si->m_cr->m_coll
		      ,(long)ps->m_wordPos2);
	
	// is synonym?
	//if ( sw2 != 1.00 )
		sb.safePrintf("<td>%s <font color=blue>%.02f"
			      "</font></td>",syn2,sw2);
	//else
	//	sb.safePrintf("<td>&nbsp;</td>");

	// wikibigram?/weight
	//if ( wbw2 != 1.0 )
		sb.safePrintf("<td>%s <font color=green>%.02f"
			      "</font></td>",bs2,wbw2);
	//else
	//	sb.safePrintf("<td>&nbsp;</td>");

	
	// diversity
	//sb.safePrintf("<td>%li/<font color=green>"
	//	      "%f</font></td>",
	//	      (long)dr2,dvw2);
	
	// density
	sb.safePrintf("<td>%li <font color=purple>"
		      "%.02f</font></td>",
		      (long)de2,dnw2);
	// word spam
	if ( hg2 == HASHGROUP_INLINKTEXT ) {
		sb.safePrintf("<td>&nbsp;</td>");
		sb.safePrintf("<td>%li <font color=red>"
			      "%.02f</font></td>",
			      (long)wr2,wsw2);
	}
	else {
		sb.safePrintf("<td>%li", (long)wr2);
		//if ( wsw2 != 1.0 )
			sb.safePrintf( " <font color=red>"
				       "%.02f</font>",  wsw2);
		sb.safePrintf("</td>");
		sb.safePrintf("<td>&nbsp;</td>");
	}
	// term freq
	sb.safePrintf("<td>%lli <font color=magenta>"
		      "%.02f</font></td>",
		      tf2,tfw2);
	// insamewikiphrase?
	sb.safePrintf("<td>%s/%li %.01f</td>",
		      wp,ps->m_qdist,wiw);
	// end the row
	sb.safePrintf("</tr>");
	sb.safePrintf("<tr><td ");
	// last row is the computation of score
	//static bool s_first = true;
	if ( first ) {
		//static long s_count = 0;
		//s_first = false;
		//sb.safePrintf("id=poo%li ",s_count);
	}
	sb.safePrintf("colspan=50>" //  style=\"display:none\">"
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
		      //"(%li - "
		      , ps->m_finalScore
		      //, idstr
		      , hgw1
		      , hgw2
		      //, (long)MAXWORDPOS+1
		      );
	sb.safePrintf("<font color=blue>%.1f</font>"
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
		sb.safePrintf(
			      "/<b>%li</b> "
			      , (long)FIXED_DISTANCE );
	else
		sb.safePrintf(
			      "/"
			      "(((<font color=darkgreen>%li</font>"
			      "-<font color=darkgreen>%li</font>)-"
			      "<font color=lime>%li</font>) + 1.0%s)"
			      ,
			      a,b,ps->m_qdist,bes);
	// wikipedia weight
	if ( wiw != 1.0 )
		sb.safePrintf("*%.01f", wiw );
	sb.safePrintf( // end formula
		      "</td></tr>"
		      //"</table>"
		      //"<br>");
		      );
	return true;
}

bool printSingleTerm ( SafeBuf &sb , Query *q , SingleScore *ss ) {

	long qtn = ss->m_qtermNum;

	sb.safePrintf("<table border=1 cellpadding=3>");
	sb.safePrintf("<tr><td colspan=50><center><b>");
	// link to rainbow page
	//sb.safePrintf("<a href=\"/print?u=");
	//sb.urlEncode( mr->ptr_ubuf );
	//sb.safePrintf("&page=4&recycle=1&c=%s\">",coll);
	if ( q->m_qterms[qtn].m_isPhrase )
		sb.pushChar('\"');
	sb.safeMemcpy ( q->m_qterms[qtn].m_term ,
			q->m_qterms[qtn].m_termLen );
	if ( q->m_qterms[qtn].m_isPhrase )
		sb.pushChar('\"');
	//sb.safePrintf("</a>");
	sb.safePrintf("</b></center></td></tr>");
	return true;
}

bool printTermPairs ( SafeBuf &sb , Query *q , PairScore *ps ) {
	// print pair text
	long qtn1 = ps->m_qtermNum1;
	long qtn2 = ps->m_qtermNum2;
	sb.safePrintf("<table cellpadding=3 border=1>"
		      "<tr><td colspan=20><center><b>");
	if ( q->m_qterms[qtn1].m_isPhrase )
		sb.pushChar('\"');
	sb.safeMemcpy ( q->m_qterms[qtn1].m_term ,
			q->m_qterms[qtn1].m_termLen );
	if ( q->m_qterms[qtn1].m_isPhrase )
		sb.pushChar('\"');
	sb.safePrintf("</b> vs <b>");
	if ( q->m_qterms[qtn2].m_isPhrase )
		sb.pushChar('\"');
	sb.safeMemcpy ( q->m_qterms[qtn2].m_term ,
			q->m_qterms[qtn2].m_termLen );
	if ( q->m_qterms[qtn2].m_isPhrase )
		sb.pushChar('\"');
	return true;
}

bool printScoresHeader ( SafeBuf &sb ) {

	sb.safePrintf("<tr>"
		      "<td>score</td>"
		      "<td>location</td>"
		      "<td>wordPos</td>"
		      "<td>synonym</td>"
		      "<td>wikibigram</td>"
		      //"<td>diversityRank</td>"
		      "<td>density</td>"
		      "<td>spam</td>"
		      "<td>inlnkPR</td>" // nlinkSiteRank</td>"
		      "<td>termFreq</td>"
		      "</tr>" 
		      );
	return true;
}

bool printSingleScore ( SafeBuf &sb , 
			SearchInput *si , 
			SingleScore *ss ,
			Msg20Reply *mr , Msg40 *msg40 ) {

	// shortcut
	Query *q = si->m_q;

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
	
	//long long tf = ss->m_termFreq;//ss->m_listSize;
	long qtn = ss->m_qtermNum;
	long long tf = msg40->m_msg3a.m_termFreqs[qtn];
	float tfw = ss->m_tfWeight;
	
	if ( si->m_format == FORMAT_XML ) {
		sb.safePrintf("\t\t<termInfo>\n");
		
		/*
		  sb.safePrintf("\t\t\t<diversityRank>%li"
		  "</diversityRank>\n",
		  (long)ss->m_diversityRank);
		  sb.safePrintf("\t\t\t<diversityWeight>%f"
		  "</diversityWeight>\n",
		  dvw);
		*/
		
		sb.safePrintf("\t\t\t<densityRank>%li"
			      "</densityRank>\n",
			      (long)ss->m_densityRank);
		sb.safePrintf("\t\t\t<densityWeight>%f"
			      "</densityWeight>\n",
			      dnw);
		sb.safePrintf("\t\t\t<term><![CDATA[");
		sb.safeMemcpy ( q->m_qterms[qtn].m_term ,
				q->m_qterms[qtn].m_termLen );
		sb.safePrintf("]]></term>\n");
		
		sb.safePrintf("\t\t\t<location><![CDATA[%s]]>"
			      "</location>\n",
			      getHashGroupString(ss->m_hashGroup));
		sb.safePrintf("\t\t\t<locationWeight>%.01f"
			      "</locationWeight>\n",
			      hgw );
		sb.safePrintf("\t\t\t<wordPos>%li"
			      "</wordPos>\n", (long)ss->m_wordPos );
		sb.safePrintf("\t\t\t<isSynonym>"
			      "<![CDATA[%s]]>"
			      "</isSynonym>\n",
			      syn);
		sb.safePrintf("\t\t\t<synonymWeight>%.01f"
			      "</synonymWeight>\n",
			      sw);
		sb.safePrintf("\t\t\t<isWikiBigram>%li"
			      "</isWikiBigram>\n",
			      (long)(ss->m_isHalfStopWikiBigram) );
		sb.safePrintf("\t\t\t<wikiBigramWeight>%.01f"
			      "</wikiBigramWeight>\n",
			      (float)WIKI_BIGRAM_WEIGHT);
		// word spam
		if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
			sb.safePrintf("\t\t\t<inlinkSiteRank>%li"
				      "</inlinkSiteRank>\n",
				      (long)ss->m_wordSpamRank);
			sb.safePrintf("\t\t\t<inlinkTextWeight>%.02f"
				      "</inlinkTextWeight>\n",
				      wsw);
		}
		else {
			sb.safePrintf("\t\t\t<wordSpamRank>%li"
				      "</wordSpamRank>\n",
				      (long)ss->m_wordSpamRank);
			sb.safePrintf("\t\t\t<wordSpamWeight>%.02f"
				      "</wordSpamWeight>\n",
				      wsw);
		}


		// if offsite inlink text show the inlinkid for matching
		// to an <inlink>
		LinkInfo *info = (LinkInfo *)mr->ptr_linkInfo;//inlinks;
		Inlink *k = info->getNextInlink(NULL);
		for ( ; k && ss->m_hashGroup==HASHGROUP_INLINKTEXT ; 
		      k=info->getNextInlink(k)){
			if ( ! k->ptr_linkText ) continue;
			if ( k->m_wordPosStart > ss->m_wordPos ) continue;
			if ( k->m_wordPosStart + 50 < ss->m_wordPos ) continue;
			// got it. we HACKED this to put the id
			// in k->m_siteHash
			sb.safePrintf("\t\t\t<inlinkId>%li"
				      "</inlinkId>\n",
				      k->m_siteHash);
		}

		// term freq
		sb.safePrintf("\t\t\t<termFreq>%lli"
			      "</termFreq>\n",tf);
		sb.safePrintf("\t\t\t<termFreqWeight>%f"
			      "</termFreqWeight>\n",tfw);
		
		sb.safePrintf("\t\t\t<score>%f</score>\n",
			      ss->m_finalScore);
		
		sb.safePrintf("\t\t\t<equationCanonical>"
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
		
		sb.safePrintf("\t\t\t<equation>"
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
		sb.safePrintf("\t\t</termInfo>\n");
		return true;
	}



	sb.safePrintf("<tr>"
		      "<td rowspan=2>%.03f</td>"
		      "<td>%s <font color=orange>%.1f"
		      "</font></td>"
		      // wordpos
		      "<td>"
		      "<a href=\"https://www.gigablast.com/seo?d=" 
		      , ss->m_finalScore
		      , getHashGroupString(ss->m_hashGroup)
		      , hgw
		      );
	//sb.urlEncode( mr->ptr_ubuf );
	sb.safePrintf("%lli",mr->m_docId );
	sb.safePrintf("&page=sections&"
		      "hipos=%li&c=%s\">"
		      ,(long)ss->m_wordPos
		      ,si->m_cr->m_coll);
	sb.safePrintf("%li</a></td>"
		      "<td>%s <font color=blue>%.1f"
		      "</font></td>" // syn
		      
		      // wikibigram?/weight
		      "<td>%s <font color=green>%.02f</font></td>"
		      
		      //"<td>%li/<font color=green>%f"
		      //"</font></td>" // diversity
		      "<td>%li <font color=purple>"
		      "%.02f</font></td>" // density
		      , (long)ss->m_wordPos
		      , syn
		      , sw // synonym weight
		      , bs
		      , wbw
		      //, (long)ss->m_diversityRank
		      //, dvw
		      , (long)ss->m_densityRank
		      , dnw
		      );
	if ( ss->m_hashGroup == HASHGROUP_INLINKTEXT ) {
		sb.safePrintf("<td>&nbsp;</td>"
			      "<td>%li <font color=red>%.02f"
			      "</font></td>" // wordspam
			      , (long)ss->m_wordSpamRank
			      , wsw
			      );
	}
	else {
		sb.safePrintf("<td>%li <font color=red>%.02f"
			      "</font></td>" // wordspam
			      "<td>&nbsp;</td>"
			      , (long)ss->m_wordSpamRank
			      , wsw
			      );
		
	}
	
	sb.safePrintf("<td>%lli <font color=magenta>"
		      "%.02f</font></td>" // termfreq
		      "</tr>"
		      , tf
		      , tfw
		      );
	// last row is the computation of score
	sb.safePrintf("<tr><td colspan=50>"
		      "%.03f "
		      " = "
		      //" %li * "
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
		      "</td></tr>"
		      , ss->m_finalScore
		      //, (long)MAXWORDPOS+1
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
	//sb.safePrintf("</table>"
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
bool printDMOZSubTopics ( SafeBuf&  sb, long catId, bool inXml ) {

	if ( catId <= 0 ) return true;

	long currType;
	bool first;
	bool nextColumn;
	long maxPerColumn;
	long currInColumn;
	long currIndex;
	char *prefixp;
	long prefixLen;
	char *catName;
	long catNameLen;
	char encodedName[2048];

	//SearchInput *si = &st->m_si;

	bool isRTL = g_categories->isIdRTL ( catId );

	SafeBuf subCatBuf;
	// stores a list of SubCategories into "subCatBuf"
	long numSubCats = g_categories->generateSubCats ( catId , &subCatBuf );

	// . get the subcategories for a given categoriy
	// . msg2b::gernerateDirectory() was launched in Msg40.cpp
	//long numSubCats      = st->m_msg40.m_msg2b.m_numSubCats;
	//SubCategory *subCats = st->m_msg40.m_msg2b.m_subCats;
	//char *catBuffer      = st->m_msg40.m_msg2b.m_catBuffer;
	//bool showAdultOnTop  = st->m_si.m_cr->m_showAdultCategoryOnTop;


	// just print <hr> if no sub categories
	if (inXml) {
		sb.safePrintf ( "\t<directory>\n"
				"\t\t<dirId>%li</dirId>\n"
				"\t\t<dirName><![CDATA[",
				catId);//si.m_cat_dirId );
		g_categories->printPathFromId ( &sb, 
						catId, // st->m_si.m_cat_dirId,
						true );
		sb.safePrintf ( "]]></dirName>\n");
		sb.safePrintf ( "\t\t<dirIsRTL>%li</dirIsRTL>\n",
				(long)isRTL);
	}

	char *p    = subCatBuf.getBufStart();
	char *pend = subCatBuf.getBuf();
	SubCategory *ptrs[MAX_SUB_CATS];
	long count = 0;

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


	for (long i = 0; i < count ; i++ ) {
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
				sb.safePrintf ( "\t\t<letterbar><![CDATA[" );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</letterbar>\n" );
				break;
			case SUBCAT_NARROW2:
				sb.safePrintf ( "\t\t<narrow2><![CDATA[" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>");
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</narrow2>\n" );
				break;
			case SUBCAT_NARROW1:
				sb.safePrintf ( "\t\t<narrow1><![CDATA[" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</narrow1>\n" );
				break;
			case SUBCAT_NARROW:
				sb.safePrintf ( "\t\t<narrow><![CDATA[" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</narrow>\n" );
				break;
			case SUBCAT_SYMBOLIC2:
				sb.safePrintf ( "\t\t<symbolic2><![CDATA[" );
				sb.utf8Encode2 ( prefixp, prefixLen  );
				sb.safePrintf ( ":" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</symbolic2>\n" );
				break;
			case SUBCAT_SYMBOLIC1:
				sb.safePrintf ( "\t\t<symbolic1><![CDATA[" );
				sb.utf8Encode2 ( prefixp, prefixLen  );
				sb.safePrintf ( ":" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</symbolic1>\n" );
				break;
			case SUBCAT_SYMBOLIC:
				sb.safePrintf ( "\t\t<symbolic><![CDATA[" );
				sb.utf8Encode2 ( prefixp, prefixLen  );
				sb.safePrintf ( ":" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</symbolic>\n" );
				break;
			case SUBCAT_RELATED:
				sb.safePrintf ( "\t\t<related><![CDATA[" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</related>\n" );
				break;
			case SUBCAT_ALTLANG:
				sb.safePrintf ( "\t\t<altlang><![CDATA[" );
				sb.utf8Encode2 ( prefixp, prefixLen  );
				sb.safePrintf ( ":" );
				sb.utf8Encode2 ( catName, catNameLen );
				sb.safePrintf ( "]]>" );
				sb.safePrintf ( "<urlcount>%li</urlcount>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
				sb.safePrintf ( "</altlang>\n");
				break;
			}
			continue;
		}
		// print type header
		if ( cat->m_type - currType >= 10) {
			// end the last type
			if (currType == SUBCAT_LETTERBAR)
				sb.safePrintf(" ]</center>\n");
			else if (currType != 0)
				sb.safePrintf ( "\n</span></ul></td></tr>"
						"</table>\n" );
			// start the new type
			switch (cat->m_type) {
			case SUBCAT_LETTERBAR:
				sb.safePrintf ( "<span class=\"directory\">"
						"<center>[ " );
				break;
			case SUBCAT_NARROW2:
			case SUBCAT_SYMBOLIC2:
			case SUBCAT_NARROW1:
			case SUBCAT_SYMBOLIC1:
			case SUBCAT_NARROW:
			case SUBCAT_SYMBOLIC:
				sb.safePrintf("<hr>\n");
				break;
			case SUBCAT_RELATED:
				if (currType == 0 ||
				    currType == SUBCAT_LETTERBAR)
					sb.safePrintf("<hr>");
				else
					sb.safePrintf("<br>");
				if (isRTL)
					sb.safePrintf("<span dir=ltr>");
				sb.safePrintf ( "<b>Related Categories:"
						"</b>" );
				if (isRTL)
					sb.safePrintf("</span>");
				break;
			case SUBCAT_ALTLANG:
				if (currType == 0 ||
				    currType == SUBCAT_LETTERBAR)
					sb.safePrintf("<hr>");
				else
					sb.safePrintf("<br>");
				if (isRTL)
					sb.safePrintf("<span dir=ltr>");
				sb.safePrintf ( "<b>This category in other"
						" languages:</b>");
				if (isRTL)
					sb.safePrintf("</span>");
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
				long numInType = 1;
				for (long j = i+1; j < numSubCats; j++) {
					if ( ptrs[j]->m_type - currType >= 10)
						break;
					numInType++;
				}
				// column for every 5, up to 3 columns
				long numColumns = numInType/5;
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
				sb.safePrintf ( "<table border=0>"
						"<tr><td valign=top>"
						"<ul><span class=\"directory\">"
						"\n<li>");
		}
		// check for the next column
		else if (nextColumn) {
			sb.safePrintf ( "\n</span></ul></td><td valign=top>"
					"<ul><span class=\"directory\">"
					"\n<li>");
			nextColumn = false;
		}
		// or just next link
		else {
			if (currType == SUBCAT_LETTERBAR)
				sb.safePrintf("| ");
			else
				sb.safePrintf("<li>");
		}
		// print out the prefix as a link
		//if ( p + catNameLen + 16 > pend ) {
		//	goto diroverflow;
		//}
		sb.safePrintf("<a href=\"/");
		sb.utf8Encode2(catName, catNameLen);
		sb.safePrintf("/\">");
		// prefix...
		//if ( p + prefixLen + 512 > pend ) {
		//	goto diroverflow;
		//}
		if (currType != SUBCAT_ALTLANG)
			sb.safePrintf("<b>");
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
				sb.safePrintf("<span dir=ltr>");
			g_categories->printPathFromIndex (
							&sb,
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
			for (long c = 0; c < prefixLen; c++) {
				if (*prefixp == '_')
					//*p = ' ';
					sb.safePrintf(" ");
				else
					//*p = *prefixp;
					sb.utf8Encode2(prefixp, 1);
				//p++;
				prefixp++;
			}
		}
		//if ( p + 512 > pend ) {
		//	goto diroverflow;
		//}
		// end the link
		if (currType != SUBCAT_ALTLANG)
			sb.safePrintf("</b>");
		sb.safePrintf("</a>");
		// print an @ for symbolic links
		if ( (cat->m_type % 10) == 1)
			sb.safePrintf("@");
		// print number of urls under here
		if ( cat->m_type != SUBCAT_LETTERBAR) { 
			sb.safePrintf("&nbsp&nbsp<i>");
			if (isRTL)
				sb.safePrintf ( "<span dir=ltr>(%li)"
						"</span></i>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
			else
				sb.safePrintf ( "(%li)</i>",
					g_categories->getNumUrlsFromIndex(
						currIndex) );
		}
		// next line/letter
		if ( cat->m_type == SUBCAT_LETTERBAR) {
			sb.safePrintf(" ");
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
			sb.safePrintf(" ]</center>\n");
		else
			sb.safePrintf("</ul></td></tr></table>\n");
	}
dirEnd:
	if (inXml)
		sb.safePrintf("\t</directory>\n");
	else {
		sb.safePrintf("</span>");
		sb.safePrintf("<hr>\n");//<br>\n");
	}

	return true;
}

bool printDMOZCrumb ( SafeBuf &sb , long catId , bool xml ) {

	// catid -1 means error
	if ( catId <= 0 ) return true;

	long dirIndex = g_categories->getIndexFromId(catId);
	//  dirIndex = g_categories->getIndexFromId(si->m_cat_sdir);
	if (dirIndex < 0) dirIndex = 0;
	//   display the directory bread crumb
	//if( (si->m_cat_dirId > 0 && si->m_isAdmin && !si->m_isFriend)
	//     || (si->m_cat_sdir > 0 && si->m_cat_sdirt != 0) )
	//	sb.safePrintf("<br><br>");
	// shortcut. rtl=Right To Left language format.
	bool rtl = g_categories->isIdRTL ( catId ) ;
	//st->m_isRTL = rtl;
	if ( ! xml ) {
		sb.safePrintf("\n<font size=4><b>");
		if ( rtl ) sb.safePrintf("<span dir=ltr>");
		//sb.safePrintf("<a href=\"/Top\">Top</a>: ");
	}
	// put crumbin xml?
	if ( xml ) 
		sb.safePrintf("<breacdcrumb><![CDATA[");
	// display the breadcrumb in xml or html?
	g_categories->printPathCrumbFromIndex(&sb,dirIndex,rtl);
	
	if ( xml )
		sb.safePrintf("]]></breadcrumb>\n" );
	
	// how many urls/entries in this topic?
	long nu =g_categories->getNumUrlsFromIndex(dirIndex);

	// print the num
	if ( ! xml ) {
		sb.safePrintf("</b>&nbsp&nbsp<i>");
		if ( rtl )
			sb.safePrintf("<span dir=ltr>(%li)</span>",nu);
		else
			sb.safePrintf("(%li)", nu);
		sb.safePrintf("</i></font><br><br>\n");
	}
	return true;
}

bool printDmozRadioButtons ( SafeBuf &sb , long catId ) ;

// if catId >= 1 then print the dmoz radio button
bool printLogoAndSearchBox ( SafeBuf &sb , HttpRequest *hr , long catId ) {

	char *root = "";
	if ( g_conf.m_isMattWells )
		root = "http://www.gigablast.com";

	sb.safePrintf(
		      // logo and menu table
		      "<table border=0 cellspacing=5>"
		      //"style=color:blue;>"
		      "<tr>"
		      "<td rowspan=2 valign=top>"
		      "<a href=/>"
		      "<img "
		      "border=0 "
		      "src=%s/logo-small.png "
		      "height=64 width=295>"
		      "</a>"
		      "</td>"
		      
		      "<td>"
		      , root
		      );
	// menu above search box
	sb.safePrintf(
		      "<br>"
		      
		      " &nbsp; "
		      );

	if ( catId <= 0 )
		sb.safePrintf("<b title=\"Search the web\">web</b>");
	else
		sb.safePrintf("<a title=\"Search the web\" href=/>web</a>");


	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; "  );


	if ( g_conf.m_isMattWells ) {
		//  SEO functionality not included yet - so redir to gigablast.
		if ( g_conf.m_isMattWells )
			sb.safePrintf("<a title=\"Rank higher in "
				      "Google\" href='/seo'>");
		else
			sb.safePrintf("<a title=\"Rank higher in "
				      "Google\" href='https://www.gigablast."
				      "com/seo'>");
	
		sb.safePrintf(
			      "seo</a>"
			      " &nbsp;&nbsp;&nbsp;&nbsp; "
			      );
	}


	if (catId <= 0 )
		sb.safePrintf("<a title=\"Browse the DMOZ directory\" "
			      "href=/Top>"
			      "directory"
			      "</a>" );
	else
		sb.safePrintf("<b title=\"Browse the DMOZ directory\">"
			      "directory</b>");

	char *coll = hr->getString("c");
	if ( ! coll ) coll = "";

	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; "
		      
		      // i'm not sure why this was removed. perhaps
		      // because it is not working yet because of
		      // some bugs...
		      "<a title=\"Advanced web search\" "
		      "href=/adv.html>"
		      "advanced"
		      "</a>"
		      
		      " &nbsp;&nbsp;&nbsp;&nbsp;"
		      
		      "<a title=\"Add your url to the index\" "
		      "href=/addurl>"
		      "add url"
		      "</a>"
		      
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
		      
		      "<br><br>"
		      //
		      // search box
		      //
		      "<form name=f method=GET action=/search>\n\n" 

		      // propagate the collection if they re-search
		      "<input name=c type=hidden value=\"%s\">"
		      , coll
		      );

	// propagate prepend
	char *prepend = hr->getString("prepend");
	if ( prepend ) {
		sb.safePrintf("<input name=prepend type=hidden value=\"");
		sb.htmlEncode ( prepend, gbstrlen(prepend), false);
		sb.safePrintf("\">");
	}
	

	sb.safePrintf (
		      // input table
		      //"<div style=margin-left:5px;margin-right:5px;>
		      "<input size=40 type=text name=q value=\""
		      );
	// contents of search box
	long  qlen;
	char *qstr = hr->getString("q",&qlen,"",NULL);
	sb.htmlEncode ( qstr , qlen , false );
	sb.safePrintf ("\">"
		       "<input type=submit value=\"Search\" border=0>"
		       "<br>"
		       "<br>"
		       );
	if ( catId >= 0 ) {
		printDmozRadioButtons(sb,catId);
	}
	else {
		sb.safePrintf("Try your search (not secure) on: "
			      "&nbsp;&nbsp; "
			      "<a href=https://www.google"
			      ".com/search?q="
			      );
		sb.urlEncode ( qstr );
		sb.safePrintf (">google</a> &nbsp;&nbsp;&nbsp;&nbsp; "
			       "<a href=http://www.bing.com/sea"
			       "rch?q=");
		sb.urlEncode ( qstr );		
		sb.safePrintf (">bing</a>");
	}
	sb.safePrintf( "</form>\n"
		       "</td>"
		       "</tr>"
		       "</table>\n"
		       );
	return true;
}

bool printDmozRadioButtons ( SafeBuf &sb , long catId ) {
	sb.safePrintf("Search "
		      "<input type=radio name=prepend "
		      "value=gbipcatid:%li checked> sites "
		      "<input type=radio name=prepend "
		      "value=gbpcatid:%li> pages "
		      "in this topic or below"
		      , catId
		      , catId
		      );
	return true;
}

/*
// print the search options under a dmoz search box
bool printDirectorySearchType ( SafeBuf& sb, long sdirt ) {
	// default to entire directory
	if (sdirt < 1 || sdirt > 4)
		sdirt = 3;

	// by default search the whole thing
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"3\"");
	if (sdirt == 3) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Entire Directory<br>\n");
	// entire category
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"1\"");
	if (sdirt == 1) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Entire Category<br>\n");
	// base category only
	sb.safePrintf("<nobr><input type=\"radio\" name=\"sdirt\" value=\"2\"");
	if (sdirt == 2) sb.safePrintf(" checked>");
	else            sb.safePrintf(">"); 
	sb.safePrintf("Pages in Base Category</nobr><br>\n");
	// sites in base category
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"7\"");
	if (sdirt == 7) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Sites in Base Category<br>\n");
	// sites in entire category
	sb.safePrintf("<input type=\"radio\" name=\"sdirt\" value=\"6\"");
	if (sdirt == 6) sb.safePrintf(" checked>");
	else            sb.safePrintf(">");
	sb.safePrintf("Sites in Entire Category<br>\n");
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
	// otherwise string compare
	int val = strcmp(pa,pb);
	return val;
}
	

#include "Json.h"

// 
// print header row in csv
//
bool printCSVHeaderRow ( SafeBuf *sb , State0 *st ) {

	Msg40 *msg40 = &st->m_msg40;
 	long numResults = msg40->getNumResults();

	char tmp1[1024];
	SafeBuf tmpBuf (tmp1 , 1024);

	char tmp2[1024];
	SafeBuf nameBuf (tmp2, 1024);

	char nbuf[27000];
	HashTableX nameTable;
	if ( ! nameTable.set ( 8,4,2048,nbuf,27000,false,0,"ntbuf") )
		return false;

	long niceness = 0;

	// . scan every fucking json item in the search results.
	// . we still need to deal with the case when there are so many
	//   search results we have to dump each msg20 reply to disk in
	//   order. then we'll have to update this code to scan that file.

	for ( long i = 0 ; i < numResults ; i++ ) {

		// get the msg20 reply for search result #i
		Msg20      *m20 = msg40->m_msg20[i];
		Msg20Reply *mr  = m20->m_r;

		if ( ! mr ) {
			log("results: missing msg20 reply for result #%li",i);
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

			// reset length of buf to 0
			tmpBuf.reset();

			// . get the name of the item into "nameBuf"
			// . returns false with g_errno set on error
			if ( ! ji->getCompoundName ( tmpBuf ) )
				return false;

			// is it new?
			long long h64 = hash64n ( tmpBuf.getBufStart() );
			if ( nameTable.isInTable ( &h64 ) ) continue;

			// record offset of the name for our hash table
			long nameBufOffset = nameBuf.length();
			
			// store the name in our name buffer
			if ( ! nameBuf.safeStrcpy ( tmpBuf.getBufStart() ) )
				return false;
			if ( ! nameBuf.pushChar ( '\0' ) )
				return false;

			// it's new. add it
			if ( ! nameTable.addKey ( &h64 , &nameBufOffset ) )
				return false;
		}
	}

	// . make array of ptrs to the names so we can sort them
	// . try to always put title first regardless
	char *ptrs [ 1024 ];
	long numPtrs = 0;
	for ( long i = 0 ; i < nameTable.m_numSlots ; i++ ) {
		if ( ! nameTable.m_flags[i] ) continue;
		long off = *(long *)nameTable.getValueFromSlot(i);
		char *p = nameBuf.getBufStart() + off;
		ptrs[numPtrs++] = p;
		if ( numPtrs >= 1024 ) break;
	}

	// sort them
	qsort ( ptrs , numPtrs , 4 , csvPtrCmp );

	// set up table to map field name to column for printing the json items
	HashTableX *columnTable = &st->m_columnTable;
	if ( ! columnTable->set ( 8,4, numPtrs * 4,NULL,0,false,0,"coltbl" ) )
		return false;

	// now print them out as the header row
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		if ( i > 0 && ! sb->pushChar(',') ) return false;
		if ( ! sb->safeStrcpy ( ptrs[i] ) ) return false;
		// record the hash of each one for printing out further json
		// objects in the same order so columns are aligned!
		long long h64 = hash64n ( ptrs[i] );
		if ( ! columnTable->addKey ( &h64 , &i ) ) 
			return false;
	}

	st->m_numCSVColumns = numPtrs;

	if ( ! sb->pushChar('\n') )
		return false;
	if ( ! sb->nullTerm() )
		return false;

	return true;
}

// returns false and sets g_errno on error
bool printJsonItemInCSV ( char *json , SafeBuf *sb , State0 *st ) {

	long niceness = 0;

	// parse the json
	Json jp;
	jp.parseJsonStringIntoJsonItems ( json , niceness );

	HashTableX *columnTable = &st->m_columnTable;
	long numCSVColumns = st->m_numCSVColumns;

	
	// make buffer space that we need
	char ttt[1024];
	SafeBuf ptrBuf(ttt,1024);
	long need = numCSVColumns * sizeof(JsonItem *);
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

		// is it new?
		long long h64 = hash64n ( tmpBuf.getBufStart() );

		long slot = columnTable->getSlot ( &h64 ) ;
		// MUST be in there
		if ( slot < 0 ) { char *xx=NULL;*xx=0;}

		// get col #
		long column = *(long *)columnTable->getValueFromSlot ( slot );

		// sanity
		if ( column >= numCSVColumns ) { char *xx=NULL;*xx=0; }

		// set ptr to it for printing when done parsing every field
		// for this json item
		ptrs[column] = ji;
	}

	// now print out what we got
	for ( long i = 0 ; i < numCSVColumns ; i++ ) {
		// , delimeted
		if ( i > 0 ) sb->pushChar(',');
		// get it
		ji = ptrs[i];
		// skip if none
		if ( ! ji ) continue;
		//
		// get value and print otherwise
		//
		if ( ji->m_type == JT_NUMBER ) {
			// print numbers without double quotes
			if ( ji->m_valueDouble *10000000.0 == 
			     (double)ji->m_valueLong * 10000000.0 )
				sb->safePrintf("%li",ji->m_valueLong);
			else
				sb->safePrintf("%f",ji->m_valueDouble);
			continue;
		}

		// print the value
		sb->pushChar('\"');
		sb->csvEncode ( ji->getValue() , ji->getValueLen() );
		sb->pushChar('\"');
	}

	sb->pushChar('\n');
	sb->nullTerm();

	return true;
}

