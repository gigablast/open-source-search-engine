#include "gb-include.h"

#include "PageInject.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Users.h"
#include "XmlDoc.h"
#include "PageParser.h"
#include "Repair.h"
#include "PageCrawlBot.h"
#include "HttpRequest.h"


//
// HTML INJECITON PAGE CODE
//

static bool sendReply        ( void *state );

static void sendReplyWrapper ( void *state ) {
	sendReply ( state );
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we are called by Parms::sendPageGeneric() to handle this request
//   which was called by Pages.cpp's sendDynamicReply() when it calls 
//   pg->function() which is called by HttpServer::sendReply(s,r) when it 
//   gets an http request
// . so "hr" is on the stack in HttpServer::requestHandler() which calls
//   HttpServer::sendReply() so we gotta copy it here
bool sendPageInject ( TcpSocket *sock , HttpRequest *hr ) {

	// get the collection
	// make a new state
	Msg7 *msg7;
	try { msg7= new (Msg7); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: new(%i): %s", 
		    sizeof(Msg7),mstrerror(g_errno));
	       return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
	}
	mnew ( msg7, sizeof(Msg7) , "PageInject" );


	// set this. also sets gr->m_hr
	GigablastRequest *gr = &msg7->m_gr;
	// this will fill in GigablastRequest so all the parms we need are set
	g_parms.setGigablastRequest ( sock , hr , gr );

	// if content is "" make it NULL so XmlDoc will download it
	// if user really wants empty content they can put a space in there
	// TODO: update help then...
	if ( gr->m_content && ! gr->m_content[0]  )
		gr->m_content = NULL;

	if ( gr->m_contentFile && ! gr->m_contentFile[0]  )
		gr->m_contentFile = NULL;

	if ( gr->m_contentDelim && ! gr->m_contentDelim[0] )
		gr->m_contentDelim = NULL;

	// if we had a delimeter but not content, zero it out...
	char *content = gr->m_content;
	if ( ! content ) content = gr->m_contentFile;
	if ( ! content ) gr->m_contentDelim = NULL;

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( gr->m_coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		//log("build: Injection from %s failed. "
		//    "Collection \"%s\" does not exist.",
		//    iptoa(s->m_ip),coll);
		// g_errno should be set so it will return an error response
		return sendReply ( msg7 );
	}

	// a scrape request?
	if ( gr->m_queryToScrape && gr->m_queryToScrape[0] ) {
		//char *uf="http://www.google.com/search?num=50&"
		//	"q=%s&scoring=d&filter=0";
		msg7->m_linkDedupTable.set(4,0,512,NULL,0,false,0,"ldtab");
		if ( ! msg7->scrapeQuery ( ) ) return false;
		return sendReply ( msg7 );
	}

	// if no url do not inject
	if ( ! gr->m_url || gr->m_url[0] == '\0' ) 
		return sendReply ( msg7 );

	// call sendReply() when inject completes
	if ( ! msg7->inject ( msg7 , sendReplyWrapper ) )
		return false;

	// it did not block, i gues we are done
	return sendReply ( msg7 );
}

bool sendReply ( void *state ) {
	// get the state properly
	Msg7 *msg7= (Msg7 *) state;

	GigablastRequest *gr = &msg7->m_gr;

	// extract info from state
	TcpSocket *sock = gr->m_socket;

	XmlDoc *xd = &msg7->m_xd;
	// log it
	//if ( msg7->m_url[0] ) xd->logIt();

	// msg7 has the docid for what we injected, iff g_errno is not set
	//long long docId  = msg7->m_msg7.m_docId;
	//long      hostId = msg7->m_msg7.m_hostId;
	long long docId  = xd->m_docId;
	long      hostId = 0;//msg7->m_msg7.m_hostId;
	

	//
	// debug
	//

	/*
	// now get the meta list, in the process it will print out a 
	// bunch of junk into msg7->m_pbuf
	if ( xd->m_docId ) {
		char *metalist = xd->getMetaList ( 1,1,1,1,1,1 );
		if ( ! metalist || metalist==(void *)-1){char *xx=NULL;*xx=0;}
		// print it out
		SafeBuf *pbuf = &msg7->m_sbuf;
		xd->printDoc( pbuf );
		bool status = g_httpServer.sendDynamicPage( msg7->m_socket , 
							   pbuf->getBufStart(),
							    pbuf->length() ,
							    -1, //cachtime
							    false ,//postreply?
							    NULL, //ctype
							    -1 , //httpstatus
							    NULL,//cookie
							    "utf-8");
		// delete the state now
		mdelete ( st , sizeof(Msg7) , "PageInject" );
		delete (st);
		// return the status
		return status;
	}
	*/
	//
	// end debug
	//

	char *url = gr->m_url;
	
	// . if we're talking w/ a robot he doesn't care about this crap
	// . send him back the error code (0 means success)
	if ( url && gr->m_shortReply ) {
		char buf[1024*32];
		char *p = buf;
		// set g_errno to index code
		if ( xd->m_indexCodeValid &&
		     xd->m_indexCode &&
		     ! g_errno )
			g_errno = xd->m_indexCode;
		// return docid and hostid
		if ( ! g_errno ) p += sprintf ( p , 
					   "0,docId=%lli,hostId=%li," , 
					   docId , hostId );
		// print error number here
		else  p += sprintf ( p , "%li,0,0,", (long)g_errno );
		// print error msg out, too or "Success"
		p += sprintf ( p , "%s", mstrerror(g_errno));
		mdelete ( msg7, sizeof(Msg7) , "PageInject" );
		delete (msg7);
		return g_httpServer.sendDynamicPage ( sock,buf, gbstrlen(buf) ,
						      -1/*cachetime*/);
	}

	SafeBuf sb;

	// print admin bar
	g_pages.printAdminTop ( &sb, sock , &gr->m_hr );

	// print a response msg if rendering the page after a submission
	if ( g_errno )
		sb.safePrintf ( "<center>Error injecting url: <b>%s[%i]</b>"
				"</center>", 
				mstrerror(g_errno) , g_errno);
	else if ( (gr->m_url&&gr->m_url[0]) ||
		  (gr->m_queryToScrape&&gr->m_queryToScrape[0]) )
		sb.safePrintf ( "<center><b>Sucessfully injected %s"
				"</center><br>"
				, xd->m_firstUrl.m_url
				);


	// print the table of injection parms
	g_parms.printParmTable ( &sb , sock , &gr->m_hr );


	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	// calculate buffer length
	//long bufLen = p - buf;
	// nuke state
	mdelete ( msg7, sizeof(Msg7) , "PageInject" );
	delete (msg7);
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	// . i thought we need -2 for cacheTime, but i guess not
	return g_httpServer.sendDynamicPage (sock, 
					     sb.getBufStart(),
					     sb.length(), 
					     -1/*cachetime*/);
}

//
// END HTML INJECTION PAGE CODE
//


Msg7::Msg7 () {
	m_round = 0;
	m_firstTime = true;
	m_fixMe = false;
	m_injectCount = 0;
}

Msg7::~Msg7 () {
}

// when XmlDoc::inject() complets it calls this
void doneInjectingWrapper9 ( void *state ) {

	
	Msg7 *msg7 = (Msg7 *)state;

 loop:

	// if we were injecting delimterized documents...
	GigablastRequest *gr = &msg7->m_gr;
	char *delim = gr->m_contentDelim;
	if ( delim && ! delim[0] ) delim = NULL;
	if ( delim && msg7->m_start ) {
		// do another injection. returns false if it blocks
		if ( ! msg7->inject ( msg7->m_state , msg7->m_callback ) )
			return;
	}

	if ( msg7->m_start ) 
		goto loop;

	// and we call the original caller
	msg7->m_callback ( msg7->m_state );
}


bool Msg7::inject ( void *state ,
		    void (*callback)(void *state) 
		    //long spiderLinksDefault ,
		    //char *collOveride ) {
		    ) {

	GigablastRequest *gr = &m_gr;

	char *coll2 = gr->m_coll;
	CollectionRec *cr = g_collectiondb.getRec ( coll2 );

	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		return true;
	}

	//char *coll = cr->m_coll;

	m_state = state;
	m_callback = callback;

	// test
	//diffbotReply = "{\"request\":{\"pageUrl\":\"http://www.washingtonpost.com/2011/03/10/ABe7RaQ_moreresults.html\",\"api\":\"article\",\"version\":3},\"objects\":[{\"icon\":\"http://www.washingtonpost.com/favicon.ico\",\"text\":\"In Case You Missed It\nWeb Hostess Live: The latest from the Web (vForum, May 15, 2014; 3:05 PM)\nGot Plans: Advice from the Going Out Guide (vForum, May 15, 2014; 2:05 PM)\nWhat to Watch: TV chat with Hank Stuever (vForum, May 15, 2014; 1:10 PM)\nColor of Money Live (vForum, May 15, 2014; 1:05 PM)\nWeb Hostess Live: The latest from the Web (vForum, May 15, 2014; 12:25 PM)\nMichael Devine outdoor entertaining and design | Home Front (vForum, May 15, 2014; 12:20 PM)\nThe Answer Sheet: Education chat with Valerie Strauss (vForum, May 14, 2014; 2:00 PM)\nThe Reliable Source Live (vForum, May 14, 2014; 1:05 PM)\nAsk Tom: Rants, raves and questions on the DC dining scene (vForum, May 14, 2014; 12:15 PM)\nOn Parenting with Meghan Leahy (vForum, May 14, 2014; 12:10 PM)\nAsk Aaron: The week in politics (vForum, May 13, 2014; 3:05 PM)\nEugene Robinson Live (vForum, May 13, 2014; 2:05 PM)\nTuesdays with Moron: Chatological Humor Update (vForum, May 13, 2014; 12:00 PM)\nComPost Live with Alexandra Petri (vForum, May 13, 2014; 11:05 AM)\nAsk Boswell: Redskins, Nationals and Washington sports (vForum, May 12, 2014; 1:50 PM)\nAdvice from Slate's 'Dear Prudence' (vForum, May 12, 2014; 1:40 PM)\nDr. Gridlock (vForum, May 12, 2014; 1:35 PM)\nSwitchback: Talking Tech (vForum, May 9, 2014; 12:05 PM)\nThe Fix Live (vForum, May 9, 2014; 12:00 PM)\nWhat to Watch: TV chat with Hank Stuever (vForum, May 8, 2014; 1:10 PM)\nMore News\",\"title\":\"The Washington Post\",\"diffbotUri\":\"article|3|828850106\",\"pageUrl\":\"http://www.washingtonpost.com/2011/03/10/ABe7RaQ_moreresults.html\",\"humanLanguage\":\"en\",\"html\":\"<p>In Case You Missed It<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/web-hostess-140515-new.html\\\">Web Hostess Live: The latest from the Web<\\/a>  <\\/p>\n<p>(vForum, May 15, 2014; 3:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/got-plans-05-15-2014.html\\\">Got Plans: Advice from the Going Out Guide<\\/a>  <\\/p>\n<p>(vForum, May 15, 2014; 2:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/tv-chat-140515.html\\\">What to Watch: TV chat with Hank Stuever<\\/a>  <\\/p>\n<p>(vForum, May 15, 2014; 1:10 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/color-of-money-live-20140515.html\\\">Color of Money Live<\\/a>  <\\/p>\n<p>(vForum, May 15, 2014; 1:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/web-hostess-140515-new.html\\\">Web Hostess Live: The latest from the Web<\\/a>  <\\/p>\n<p>(vForum, May 15, 2014; 12:25 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/home-front-0515.html\\\">Michael Devine outdoor entertaining and design | Home Front<\\/a>  <\\/p>\n<p>(vForum, May 15, 2014; 12:20 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/the-answer-sheet-20140514.html\\\">The Answer Sheet: Education chat with Valerie Strauss<\\/a>  <\\/p>\n<p>(vForum, May 14, 2014; 2:00 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/the-reliable-source-140514-new.html\\\">The Reliable Source Live<\\/a>  <\\/p>\n<p>(vForum, May 14, 2014; 1:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/ask-tom-5-14-14.html\\\">Ask Tom: Rants, raves and questions on the DC dining scene <\\/a>  <\\/p>\n<p>(vForum, May 14, 2014; 12:15 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/parenting-0514.html\\\">On Parenting with Meghan Leahy<\\/a>  <\\/p>\n<p>(vForum, May 14, 2014; 12:10 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/post-politics-ask-aaron-051313.html\\\">Ask Aaron: The week in politics<\\/a>  <\\/p>\n<p>(vForum, May 13, 2014; 3:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/opinion-focus-with-eugene-robinson-20140513.html\\\">Eugene Robinson Live<\\/a>  <\\/p>\n<p>(vForum, May 13, 2014; 2:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/gene-weingarten-140513.html\\\">Tuesdays with Moron: Chatological Humor Update<\\/a>  <\\/p>\n<p>(vForum, May 13, 2014; 12:00 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/compost-live-140513.html\\\">ComPost Live with Alexandra Petri<\\/a>  <\\/p>\n<p>(vForum, May 13, 2014; 11:05 AM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/ask-boswell-1400512.html\\\">Ask Boswell: Redskins, Nationals and Washington sports<\\/a>  <\\/p>\n<p>(vForum, May 12, 2014; 1:50 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/dear-prudence-140512.html\\\">Advice from Slate's 'Dear Prudence'<\\/a>  <\\/p>\n<p>(vForum, May 12, 2014; 1:40 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/gridlock-0512.html\\\">Dr. Gridlock <\\/a>  <\\/p>\n<p>(vForum, May 12, 2014; 1:35 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/switchback-20140509.html\\\">Switchback: Talking Tech<\\/a>  <\\/p>\n<p>(vForum, May 9, 2014; 12:05 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/live-fix-140509.html\\\">The Fix Live<\\/a>  <\\/p>\n<p>(vForum, May 9, 2014; 12:00 PM)<\\/p>\n<p>  <a href=\\\"http://live.washingtonpost.com/tv-chat-140508.html\\\">What to Watch: TV chat with Hank Stuever<\\/a>  <\\/p>\n<p>(vForum, May 8, 2014; 1:10 PM)<\\/p>\n<p>  <a href=\\\"http://www.washingtonpost.com/2011/03/10/ /2011/03/10/ABe7RaQ_moreresults.html ?startIndex=20&dwxLoid=\\\">More News <\\/a>  <\\/p>\",\"date\":\"Tue, 13 May 2014 00:00:00 GMT\",\"type\":\"article\"}]}";

	if ( g_repairMode ) { g_errno = EREPAIRING; return true; }

	// shortcut
	XmlDoc *xd = &m_xd;

	// this will be NULL if the "content" was empty or not given
	char *content = gr->m_content;

	// . try the uploaded file if nothing in the text area
	// . this will be NULL if the "content" was empty or not given
	if ( ! content ) content = gr->m_contentFile;

	if ( m_firstTime ) {
		m_firstTime = false;
		m_start = content;
	}

	// save current start since we update it next
	char *start = m_start;

	// if this is empty we are done
	//if ( ! start ) 
	//	return true;

	char *delim = gr->m_contentDelim;
	if ( delim && ! delim[0] ) delim = NULL;

	if ( m_fixMe ) {
		// we had made the first delim char a \0 to index the
		// previous document, now put it back to what it was
		*m_start = *delim;
		// i guess unset this
		m_fixMe = false;
	}

	// if we had a delimeter...
	if ( delim ) {
		// we've saved m_start as "start" above, 
		// so find the next delimeter after it and set that to m_start
		// add +1 to avoid infinite loop
		m_start = strstr(start+1,delim);
		// for injecting "start" set this to \0
		if ( m_start ) {
			// null term it
			*m_start = '\0';
			// put back the original char on next round...?
			m_fixMe = true;
		}
	}

	// this is the url of the injected content
	m_injectUrlBuf.safeStrcpy ( gr->m_url );

	bool modifiedUrl = false;

	// if we had a delimeter we must make a fake url
	// if ( delim ) {
	//  	// if user had a <url> or <doc> or <docid> field use that
	//  	char *hint = strcasestr ( start , "<url>" );
	//  	if ( hint ) {
	// 		modifiedUrl = true;
	// 		...
	// 	}
	// }

	// if we had a delimeter thus denoting multiple items/documents to
	// be injected, we must create unique urls for each item.
	if ( delim && ! modifiedUrl ) {
		// use hash of the content
		long long ch64 = hash64n ( start , 0LL );
		// normalize it
		Url u; u.set ( gr->m_url );
		// reset it
		m_injectUrlBuf.reset();
		// by default append a -<ch64> to the provided url
		m_injectUrlBuf.safePrintf("%s-%llu",u.getUrl(),ch64);
	}

	// count them
	m_injectCount++;


	if ( ! xd->injectDoc ( m_injectUrlBuf.getBufStart() ,
			       cr ,
			       start , // content ,
			       gr->m_diffbotReply,
			       gr->m_hasMime, // content starts with http mime?
			       gr->m_hopCount,
			       gr->m_charset,

			       gr->m_deleteUrl,
			       gr->m_contentTypeStr, // text/html text/xml
			       gr->m_spiderLinks ,
			       gr->m_newOnly, // index iff new

			       this ,
			       doneInjectingWrapper9 ) )
		// we blocked...
		return false;

	return true;
}


///////////////
//
// SCRAPE GOOGLE
//
// and inject the serps
//
///////////////


void doneInjectingLinksWrapper ( void *state ) {
	Msg7 *msg7 = (Msg7 *)state;
	SafeBuf *sb = &msg7->m_sb;
	// copy the serps into ou rbuf
	if ( ! g_errno ) {
		// print header
		if ( sb->length() == 0 ) {
			// print header of page
			sb->safePrintf("<?xml version=\"1.0\" "
				       "encoding=\"UTF-8\" ?>\n"
				       "<response>\n" );
		}
		// serp header
		if ( msg7->m_round == 1 )
			sb->safePrintf("\t<googleResults>\n");
		else
			sb->safePrintf("\t<bingResults>\n");
		// print results
		sb->safeMemcpy(&msg7->m_xd.m_serpBuf);
		// end that
		if ( msg7->m_round == 1 )
			sb->safePrintf("\t</googleResults>\n");
		else
			sb->safePrintf("\t</bingResults>\n");
	}
	// do bing now
	if ( msg7->m_round == 1 ) {
		// return if it blocks
		if ( ! msg7->scrapeQuery() ) return;
	}
	TcpSocket *s = msg7->m_socket;
	// otherwise, parse out the search results so steve can display them
	if ( g_errno )
		sb->safePrintf("<error><![CDATA[%s]]></error>\n",
			       mstrerror(g_errno));
	// print header of page
	sb->safePrintf("</response>\n");
	// page is not more than 32k
	//char buf[1024*32];
	//char *p = buf;
	// return docid and hostid
	//p += sprintf ( p , "scraping status ");
	// print error msg out, too or "Success"
	//p += sprintf ( p , "%s", mstrerror(g_errno));
	g_httpServer.sendDynamicPage ( s, 
				       sb->getBufStart(),
				       sb->length(),
				       -1/*cachetime*/);
	// hopefully sb buffer is copied becaues this will free it:
	mdelete ( msg7, sizeof(Msg7) , "PageInject" );
	delete (msg7);
}

// . "uf" is printf url format to scrape with a %s for the query
// . example: uf="http://www.google.com/search?num=50&q=%s&scoring=d&filter=0";
bool Msg7::scrapeQuery ( ) {

	// advance round now in case we return early
	m_round++;

	GigablastRequest *gr = &m_gr;

	// error?
	char *qts = gr->m_queryToScrape;
	if ( ! qts ) { char *xx=NULL;*xx=0; }

	if ( gbstrlen(qts) > 500 ) {
		g_errno = EQUERYTOOBIG;
		return true;
	}

	// first encode the query
	SafeBuf ebuf;
	ebuf.urlEncode ( qts ); // queryUNEncoded );

	char *uf;
	if ( m_round == 1 )
		// set to 1 for debugging
		uf="http://www.google.com/search?num=20&"
			"q=%s&scoring=d&filter=0";
		//uf = "https://startpage.com/do/search?q=%s";
		//uf = "http://www.google.com/"
		//	"/cse?cx=013269018370076798483%3A8eec3papwpi&"
		//	"ie=UTF-8&q=%s&"
		//	"num=20";
	else
		uf="http://www.bing.com/search?q=%s";

	// skip bing for now
	//if ( m_round == 2 )
	//	return true;
	//if ( m_round == 1 )
	//	return true;
		
	// make the url we will download
	char ubuf[2048];
	sprintf ( ubuf , uf , ebuf.getBufStart() );

	// log it
	log("inject: SCRAPING %s",ubuf);

	SpiderRequest sreq;
	sreq.reset();
	// set the SpiderRequest
	strcpy(sreq.m_url, ubuf);
	// . tell it to only add the hosts of each outlink for now!
	// . that will be passed on to when XmlDoc calls Links::set() i guess
	// . xd will not reschedule the scraped url into spiderdb either
	sreq.m_isScraping = 1;
	sreq.m_fakeFirstIp = 1;
	long firstIp = hash32n(ubuf);
	if ( firstIp == 0 || firstIp == -1 ) firstIp = 1;
	sreq.m_firstIp = firstIp;
	// parent docid is 0
	sreq.setKey(firstIp,0LL,false);

	char *coll2 = gr->m_coll;
	CollectionRec *cr = g_collectiondb.getRec ( coll2 );

	// forceDEl = false, niceness = 0
	m_xd.set4 ( &sreq , NULL , cr->m_coll , NULL , 0 ); 

	//m_xd.m_isScraping = true;

	// download without throttling
	//m_xd.m_throttleDownload = false;

	// disregard this
	m_xd.m_useRobotsTxt = false;

	// this will tell it to index ahrefs first before indexing
	// the doc. but do NOT do this if we are from ahrefs.com
	// ourselves to avoid recursive explosion!!
	if ( m_useAhrefs )
		m_xd.m_useAhrefs = true;

	m_xd.m_reallyInjectLinks = gr->m_injectLinks;

	//
	// rather than just add the links of the page to spiderdb,
	// let's inject them!
	//
	m_xd.setCallback ( this , doneInjectingLinksWrapper );

	// niceness is 0
	m_linkDedupTable.set(4,0,512,NULL,0,false,0,"ldtab2");

	// do we actually inject the links, or just scrape?
	if ( ! m_xd.injectLinks ( &m_linkDedupTable ,
				  NULL,
				  this , 
				  doneInjectingLinksWrapper ) ) 
		return false;
	// otherwise, just download the google/bing search results so we
	// can display them in xml
	//else if ( m_xd.getUtf8Content() == (char **)-1 )
	//	return false;
		
	// print reply..
	//printReply();
	return true;
}
