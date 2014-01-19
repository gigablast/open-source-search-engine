#include "gb-include.h"

#include "PageInject.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Users.h"
#include "XmlDoc.h"
#include "PageParser.h"
#include "Repair.h"
#include "PageCrawlBot.h"

static bool sendReply        ( void *state );

static void sendReplyWrapper ( void *state ) {
	sendReply ( state );
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageInject ( TcpSocket *s , HttpRequest *r ) {
	// get the collection
	long  collLen = 0;
	char *coll  = r->getString ( "c" , &collLen  , NULL /*default*/);

	long crawlbotAPI = r->getLong("crawlbotapi",0);

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("build: Injection from %s failed. "
		    "Collection \"%s\" does not exist.",
		    iptoa(s->m_ip),coll);
		return g_httpServer.sendErrorReply(s,500,
					      "collection does not exist");
	}

	// make a new state
	Msg7 *msg7;
	try { msg7= new (Msg7); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: new(%i): %s", 
		    sizeof(Msg7),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( msg7, sizeof(Msg7) , "PageInject" );

	msg7->m_socket = s;

	msg7->m_isScrape = false;

	msg7->m_crawlbotAPI = crawlbotAPI;

	strncpy(msg7->m_coll,coll,MAX_COLL_LEN);

	// for diffbot
	if ( crawlbotAPI ) 
		msg7->m_hr.copy ( r );

	// a scrape request?
	char *qts = r->getString("qts",NULL);
	if ( qts && ! qts[0] ) qts = NULL;
	if ( qts ) {
		// qts is html encoded? NO! fix that below then...
		//char *uf="http://www.google.com/search?num=50&"
		//	"q=%s&scoring=d&filter=0";
		msg7->m_isScrape = true;
		msg7->m_qbuf.safeStrcpy(qts);
		msg7->m_linkDedupTable.set(4,0,512,NULL,0,false,0,"ldtab");
		msg7->m_useAhrefs = r->getLong("useahrefs",0);
		// default to yes, injectlinks.. no default to no
		msg7->m_injectLinks = r->getLong("injectlinks",0);
		if ( ! msg7->scrapeQuery ( ) ) return false;
		return sendReply ( msg7 );
	}

	if ( ! msg7->inject ( s , r , msg7 , sendReplyWrapper ) )
		return false;

	// it did not block, i gues we are done
	return sendReply ( msg7 );
}

bool sendReply ( void *state ) {
	// get the state properly
	Msg7 *msg7= (Msg7 *) state;
	// extract info from state
	TcpSocket *s = msg7->m_socket;

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
	


	// page is not more than 32k
	//char buf[1024*32];


	// . if we're talking w/ a robot he doesn't care about this crap
	// . send him back the error code (0 means success)
	if ( msg7->m_quickReply ) {
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
		return g_httpServer.sendDynamicPage ( s, buf , gbstrlen(buf) ,
						      -1/*cachetime*/);
	}

	// get an active ptr into buf
	//char *p    = buf;
	//char *pend = buf + 1024*32;

	SafeBuf sb;

	// print admin bar
	g_pages.printAdminTop ( &sb, // p , pend , 
				PAGE_INJECT, 
				NULL, // msg7->m_username ,
				msg7->m_coll , 
				NULL ,  // pwd
				s->m_ip );

	// if there was an error let them know
	char msg[1024];
	char *pm = "";
	if ( g_errno ) {
		sprintf ( msg ,"Error injecting url: <b>%s[%i]</b>", 
			  mstrerror(g_errno) , g_errno);
		pm = msg;
	}
	//else if ( msg7->m_injected )
	//	pm = "url successfully injected";

	// bail if not enabled
	//if ( ! g_conf.m_injectionEnabled ) {
	//	sprintf ( msg ,"<font color=red>URL injection is disabled "
	//		  "in the Master Controls</font>");
	//	pm = msg;
	//}

	//char *c = msg7->m_coll;
	char bb [ MAX_COLL_LEN + 60 ];
	bb[0]='\0';
	//if ( c && c[0] ) sprintf ( bb , " (%s)", c);

	// make a table, each row will be an injectable parameter
	sb.safePrintf (
		  "<center>"
		  "<b>%s</b>\n\n" // the url msg
		  //"<FORM method=POST action=/inject>\n\n" 

		  "<FORM method=GET action=/inject>\n\n" 

		  //"<input type=hidden name=pwd value=\"%s\">\n"
		  //"<input type=hidden name=username value=\"%s\">\n"
		  "<table width=100%% bgcolor=#%s cellpadding=4 border=1>"
		  "<tr><td  bgcolor=#%s colspan=2>"
		  "<center>"
		  //"<font size=+1>"
		  "<b>"
		  "Inject URL</b>%s"
		  //"</font>"
		  "<br>"
		  //"Enter the information below to inject "
		  //"a URL. This allows you to specify the URL as well as the "
		  //"content for the URL."
		  "</td></tr>\n\n"

		  "<tr><td><b>url</b></td>"
		  "<td>\n"
		  "<input type=text name=u value=\"\" size=50>"
		  "</td></tr>\n\n"

		  "<tr><td><b>query to scrape</b></td>"
		  "<td>\n"
		  "<input type=text name=qts value=\"\" size=50>"
		  "</td></tr>\n\n"

		  //"<tr><td><b>use ahrefs.com</b></td>"
		  //"<td>\n"
		  //"<input type=radio name=useahrefs value=0 checked>no &nbsp; "
		  //"<input type=radio name=useahrefs value=1>yes "
		  //"</td></tr>\n\n"

		  
		  "<tr><td><b>spider links</b></td>"
		  "<td>\n"
		  "<input type=radio name=spiderlinks value=0>no &nbsp; "
		  "<input type=radio name=spiderlinks value=1 checked>yes "
		  "<br>"
		  "<font size=1>Should we add the page's outlinks to "
		  "spiderdb for spidering? "
		  "Default: yes"
		  "</font>"
		  "</td></tr>\n\n"



		  "<tr><td><b>inject scraped links</b></td>"
		  "<td>\n"
		  "<input type=radio name=injectlinks value=0 checked>no &nbsp; "
		  "<input type=radio name=injectlinks value=1>yes "
		  "</td></tr>\n\n"

		  "<tr><td><b>collection</b></td>"
		  "<td>\n"
		  "<input type=text name=c value=\"%s\" size=15>"
		  "</td></tr>\n\n"

		  "<tr><td><b>quick reply?</b><br>"
		  "<font size=1>Should reply be short? "
		  "Default: no"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=quick value=0 checked>no &nbsp; "
		  "<input type=radio name=quick value=1>yes "
		  "</td></tr>\n\n"

		  "<tr><td><b>only inject new docs?</b><br>"
		  "<font size=1>Skips injection if docs already indexed. "
		  "Default: no"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=newonly value=0 checked>no &nbsp; "
		  "<input type=radio name=newonly value=1>yes "
		  "</td></tr>\n\n"


		  "<tr><td><b>delete url?</b><br>"
		  "<font size=1>Should this url be deleted from the index? "
		  "Default: no"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=deleteurl value=0 checked>no &nbsp; "
		  "<input type=radio name=deleteurl value=1>yes "
		  "</td></tr>\n\n"


		  "<tr><td><b>recycle content?</b><br>"
		  "<font size=1>Should page content be recycled if "
		  "reindexing? "
		  "Default: no"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=recycle value=0 checked>no &nbsp; "
		  "<input type=radio name=recycle value=1>yes "
		  "</td></tr>\n\n"

		  "<tr><td><b>ip</b><br>"
		  "<font size=1>IP address of the url. If blank then "
		  "Gigablast will look up. "
		  "Default: blank"
		  "</td>"
		  "<td>\n<input type=text name=ip value=\"\" size=15>"
		  "</td></tr>\n\n"

		  /*
		  "<tr><td><b>do ip lookups?</b><br>"
		  "<font size=1>Should Gigablast look up the IP address "
		  "of the url, if it is not provided. "
		  "Default: yes"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=iplookups value=0>no &nbsp; "
		  "<input type=radio name=iplookups value=1 checked>yes "
		  "</td></tr>\n\n"
		  */

		  //"<tr><td><b>is url new?</b><br>"
		  //"<font size=1>Is this url new to the index? If unsure "
		  //"then you should say no here. "
		  //"Default: yes"
		  //"</td>"
		  //"<td>\n"
		  //"<input type=radio name=isnew value=0>no &nbsp; "
		  //"<input type=radio name=isnew value=1 checked>yes "
		  //"</td></tr>\n\n"

		  "<tr><td><b>dedup?</b><br>"
		  "<font size=1>Should this url be skipped if there is "
		  "already  a url in the index from this same domain with "
		  "this same content? "
		  "Default: yes"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=dedup value=0>no &nbsp; "
		  "<input type=radio name=dedup value=1 checked>yes "
		  "</td></tr>\n\n" ,
		  //"<tr><td><b>ruleset</b><br>"
		  //"<font size=1>Use this ruleset to index the URL. "
		  //"Default: auto"
		  //"</td>"
		  //"<td>\n<select name=rs>" ,
		  pm , // msg7->m_pwd , 
		  //msg7->m_username,
		  LIGHT_BLUE , DARK_BLUE , bb , msg7->m_coll );


	//p += gbstrlen(p);

	// . print pulldown menu of different site filenums
	// . 0 - default site
	// . 1 - banned  site
	// . 2 - bad     site
	// . 3 - decent  site
	// . 4 - good    site
	// . 5 - super   site
	/*
	for ( long i = 0 ; i < 10000 ; i++ ) {
		Xml *xml = g_tagdb.getSiteXml(i, msg7->m_coll, 
					       gbstrlen(msg7->m_coll));
		if ( ! xml ) break;
		long  slen;
		char *s = xml->getString ( "name" , &slen );
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			sprintf ( p , "<option value=%li>%s", i , s );
			s[slen] = c;
		}
		else  
			sprintf ( p , "<option value=%li>#%li", i , i );
		p += gbstrlen ( p );
	}
	// end the pull-down menu
	sprintf ( p , "</select></td></tr>\n\n" );
	p += gbstrlen ( p );	
	*/

	// make a table, each row will be an injectable parameter
	sb.safePrintf (
		  "<tr><td><b>content has mime</b><br>"
		  "<font size=1>IP address of the url. If blank then "
		  "Gigablast will look up. "
		  "Default: blank"
		  "</td>"
		  "<td>\n"
		  "<input type=radio name=hasmime value=0 checked>no &nbsp; "
		  "<input type=radio name=hasmime value=1>yes "
		  "</td></tr>\n\n" 

		  "<tr><td colspan=2>"
		  "<center>"
		  "<b>content</b><br>"
		  "<font size=1>Enter the content here. Enter MIME header "
		  "first if \"content has mime\" is set to true above. "
		  "Separate MIME from actual content with two returns."
		  "<br>"
		  "<input type=submit value=Submit>"
		  "<br>"
		  "\n"
		  "<textarea rows=32 cols=80 name=content>"
		  "</textarea>"
		  "<br>"
		  "<br>\n\n"
		  "<input type=submit value=Submit>"
		  "</center>"
		  "</td></tr></table>\n"
		  "</form>\n"
		  );


	sb.safePrintf( "\n</body>\n</html>\n");
	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p , true /*adminLink?*/);
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
	return g_httpServer.sendDynamicPage (s, 
					     sb.getBufStart(),
					     sb.length(), 
					     -1/*cachetime*/);
}


Msg7::Msg7 () {
	//m_needsSet = true;
	m_contentAllocSize = 0;
	m_content = NULL;
	m_round = 0;
}

Msg7::~Msg7 () {
	if ( m_content ) 
		mfree ( m_content , m_contentAllocSize,"injcont");
	m_content = NULL;
}

bool Msg7::inject ( TcpSocket *s , 
		    HttpRequest *r ,
		    void *state ,
		    void (*callback)(void *state) ,
		    long spiderLinksDefault ,
		    char *collOveride ) {

	// save socket
	// socket is responsible for free the HTTP request, which contains
	// the POSTed content, so if he gets destroyed we have to make sure
	// we no longer reference that content.
	m_socket  = s;

	long  contentLen;

	// get the junk
	//char *coll        = r->getString ( "c" , NULL  , NULL /*default*/);
	//if ( ! coll ) coll = "main";
	// sometimes crawlbot will add or reset a coll and do an inject
	// in PageCrawlBot.cpp
	//if ( ! coll ) coll = r->getString("addcoll");
	//if ( ! coll ) coll = r->getString("resetcoll");
	//if ( ! coll ) coll = collOveride;

	// default to main
	//if ( ! coll || ! coll[0] ) coll = "main";

	if ( collOveride && ! collOveride[0] ) collOveride = NULL;

	CollectionRec *cr = NULL;
	if ( collOveride ) cr = g_collectiondb.getRec ( collOveride );
	else cr = g_collectiondb.getRec ( r );

	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		return true;
	}

	char *coll = cr->m_coll;

	bool  quickReply     = r->getLong   ( "quick" , 0 );	
	//char *pwd            = r->getString ( "pwd" , NULL );
	char *url            = r->getString ( "u" , NULL , NULL /*default*/);
	// for diffbot.cpp api
	if ( ! url ) url = r->getString("injecturl",NULL,NULL);
	if ( ! url ) url = r->getString("url",NULL,NULL);
	// PageCrawlBot.cpp uses "seed"
	if ( ! url ) url = r->getString("seed",NULL,NULL);

	bool  recycleContent = r->getLong   ( "recycle",0);
	//char *ips            = r->getString ( "ip" , NULL , NULL );
	//char *username       = g_users.getUsername(r);
	//long firstIndexed = r->getLongLong("firstindexed",0LL);
	//long lastSpidered = r->getLongLong("lastspidered",0LL);
	long hopCount     = r->getLong("hopcount",-1);
	long newOnly      = r->getLong("newonly",0);
	long charset      = r->getLong("charset",-1);
	long deleteUrl    = r->getLong("deleteurl",0);
	char hasMime      = r->getLong("hasmime",0);
	// do consistency testing?
	bool doConsistencyTesting = r->getLong("dct",0);
	// . default spiderlinks to no for injects. no, not for
	//   seed urls from PageCrawlbot.cpp, ppl expect links to be spidered.
	// . support both camel and all-lower cases
	long spiderLinks = r->getLong("spiderLinks",spiderLinksDefault);
	spiderLinks = r->getLong("spiderlinks",spiderLinks);

	long  forcedIp  = 0;
	
	//if ( ips ) forcedIp = atoip ( ips , gbstrlen(ips) );

	char *content        = r->getString ( "content" , &contentLen , NULL );
	// mark doesn't like to url-encode his content
	if ( ! content ) { 
		content    = r->getUnencodedContent    ();
		contentLen = r->getUnencodedContentLen ();
		//contentIsEncoded = false;
	}


	// we do not want the parser every holding up a query really
	long niceness = 1;

	// tell xmldoc to download the doc
	if ( contentLen == 0 ) content = NULL;

	// the http request gets freed if this blocks, so we have to
	// copy the content!!!
	if ( content ) {
		m_contentAllocSize = contentLen + 1;
		m_content = mdup ( content , contentLen + 1 , "injcont" );
	}
	else {
		m_content = NULL;
		m_contentAllocSize = 0;
	}

	return inject ( url,
			forcedIp,
			m_content,
			contentLen,
			recycleContent,
			CT_HTML, // contentType,
			coll,
			quickReply ,
			NULL,//username ,
			NULL,//pwd,
			niceness,
			state,
			callback,
			//firstIndexed,
			//lastSpidered,
			hopCount,
			newOnly,
			charset,
			spiderLinks,
			deleteUrl,
			hasMime,
			doConsistencyTesting);
}

// . returns false if blocked, true otherwise
// . if returns false will call your callback(state) when is done
// . returns true and sets g_errno on error
bool Msg7::inject ( char *url ,
		    long  forcedIp ,
		    char *content ,
		    long  contentLen ,
		    bool  recycleContent,
		    uint8_t contentType,
		    char *coll ,
		    bool  quickReply ,
		    char *username ,
		    char *pwd ,
		    long  niceness,
		    void *state ,
		    void (*callback)(void *state),
		    //long firstIndexed,
		    //long lastSpidered,
		    long hopCount,
		    char newOnly,
		    short charset,
		    char spiderLinks,
		    char deleteUrl,
		    char hasMime,
		    bool doConsistencyTesting
		    ) {

	m_quickReply = quickReply;

	// store coll
	//if ( ! coll ) { g_errno = ENOCOLLREC; return true; }
	//      long collLen = gbstrlen ( coll );
	//if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	//strncpy ( m_coll , coll , collLen );
	//m_coll [ collLen ] = '\0';

	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) { g_errno = ENOCOLLREC; return true; }

	// store user
	//long ulen = 0;
	//if ( username ) ulen = gbstrlen(username);
	//if ( ulen >= MAX_USER_SIZE-1 ) {g_errno = EBUFOVERFLOW; return true;}
	//if ( username ) strcpy( m_username, username );

	// store password
	//long pwdLen = 0;
	//if ( pwd ) pwdLen = gbstrlen(pwd);
	//m_pwd [ 0 ] ='\0';
	//if ( pwdLen > 31 ) pwdLen = 31;
	//if ( pwdLen > 0 ) strncpy ( m_pwd , pwd , pwdLen );
	//m_pwd [ pwdLen ] = '\0';

	// store url
	if ( ! url ) { g_errno = 0; return true; }
	long urlLen = gbstrlen(url);
	if ( urlLen > MAX_URL_LEN ) {g_errno = EBADENGINEER; return true; }
	// skip injecting if no url given! just print the admin page.
	if ( urlLen <= 0 ) return true;
	//strcpy ( m_url , url );

	if ( g_repairMode ) { g_errno = EREPAIRING; return true; }

	// send template reply if no content supplied
	//if ( ! content && ! recycleContent ) {
	//	log("inject: no content supplied to inject command and "
	//	    "recycleContent is false.");
	//	//return true;
	//}

	// shortcut
	XmlDoc *xd = &m_xd;

	if ( ! xd->injectDoc ( url ,
			       cr ,
			       content ,
			       hasMime , // content starts with http mime?
			       hopCount,
			       charset,

			       deleteUrl,
			       contentType, // CT_HTML, CT_XML
			       spiderLinks ,
			       newOnly, // index iff new

			       state,
			       callback ) )
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

	// error?
	if ( m_qbuf.length() > 500 ) {
		g_errno = EQUERYTOOBIG;
		return true;
	}

	// first encode the query
	SafeBuf ebuf;
	ebuf.urlEncode ( m_qbuf.getBufStart() ); // queryUNEncoded );

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

	// forceDEl = false, niceness = 0
	m_xd.set4 ( &sreq , NULL , m_coll , NULL , 0 ); 

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

	m_xd.m_reallyInjectLinks = m_injectLinks;

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
