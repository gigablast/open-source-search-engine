#include "gb-include.h"

#include "SafeBuf.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Msg22.h"
#include "Query.h"
#include "HttpServer.h"
#include "Highlight.h"
#include "Pages.h"
#include "PageNetTest.h"
#include "Tagdb.h"
#include "XmlDoc.h"
#include "PageResults.h" // printEventAddress()...

// TODO: redirect to host that has the titleRec locally

static bool sendErrorReply ( void *state , int32_t err ) ;
static void processLoopWrapper ( void *state ) ;
static bool processLoop ( void *state ) ;

class State2 {
public:
	Msg22      m_msg22;
	char m_format;
	//TitleRec   m_tr;
	int32_t       m_niceness;
	XmlDoc     m_xd;
	char      *m_tr;
	int32_t       m_trSize;
	uint8_t    m_langId;
	//Msg8a      m_msg8a;
	//SiteRec    m_sr;
	//TagRec     m_tagRec;
	TcpSocket *m_socket;
	HttpRequest m_r;
	char m_coll[MAX_COLL_LEN+2];
	//CollectionRec *m_cr;
	bool       m_isMasterAdmin;
	bool       m_isLocal;
	//bool       m_seq;
	bool       m_rtq;
	//char       m_q[MAX_QUERY_LEN+1];
	SafeBuf m_qsb;
	char m_qtmpBuf[128];
	int32_t       m_qlen;
	char       m_boolFlag;
	bool       m_printed;
	int64_t  m_docId;
	bool       m_includeHeader;
	bool       m_includeBaseHref;
	bool       m_queryHighlighting;
	int32_t       m_strip;
	bool       m_clickAndScroll;
	bool	   m_clickNScroll; // new click 'n' scroll
	bool	   m_cnsPage;      // Are we in the click 'n' scroll page?
	bool	   m_printDisclaimer;
	bool       m_netTestResults;
	bool       m_isBanned;
	bool       m_noArchive;
	SafeBuf    m_sb;
};
	

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageGet ( TcpSocket *s , HttpRequest *r ) {
	// get the collection
	int32_t  collLen = 0;
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
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("query: Archived copy retrieval failed. "
		    "No collection record found for "
		    "collection \"%s\".",coll);
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	// does this collection ban this IP?
	if ( ! cr->hasSearchPermission ( s ) ) {
		g_errno = ENOPERM;
		//log("PageGet::sendDynamicReply0: permission denied for %s",
		//    iptoa(s->m_ip) );
		g_msg = " (error: permission denied)";
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  qlen = 0;
	char *q = r->getString ( "q" , &qlen , NULL /*default*/);
	// ensure query not too big
	if ( qlen >= ABS_MAX_QUERY_LEN-1 ) { 
		g_errno=EQUERYTOOBIG; 
		return g_httpServer.sendErrorReply (s,500 ,mstrerror(g_errno));
	}
	// the docId
	int64_t docId = r->getLongLong ( "d" , 0LL /*default*/ );
	// get url
	char *url = r->getString ( "u",NULL);

	if ( docId == 0 && ! url ) {
		g_errno = EMISSINGINPUT;
		return g_httpServer.sendErrorReply (s,500 ,mstrerror(g_errno));
	}


	// . should we do a sequential lookup?
	// . we need to match summary here so we need to know this
	//bool seq = r->getLong ( "seq" , false );
	// restrict to root file?
	bool rtq = r->getLong ( "rtq" , false );

	// . get the titleRec
	// . TODO: redirect client to a better http server to save bandwidth
	State2 *st ;
	try { st = new (State2); }
	catch (... ) {
		g_errno = ENOMEM;
		log("PageGet: new(%i): %s", 
		    (int)sizeof(State2),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State2) , "PageGet1" );
	// save the socket and if Host: is local in the Http request Mime
	st->m_socket   = s;
	st->m_isMasterAdmin  = g_conf.isCollAdmin ( s , r );
	st->m_isLocal  = r->isLocal();
	st->m_docId    = docId;
	st->m_printed  = false;
	// include header ... "this page cached by Gigablast on..."
	st->m_includeHeader     = r->getLong ("ih"    , true  );
	st->m_includeBaseHref   = r->getLong ("ibh"   , false );
	st->m_queryHighlighting = r->getLong ("qh"    , true  );
	st->m_strip             = r->getLong ("strip" , 0     );
	st->m_clickAndScroll    = r->getLong ("cas"   , true  );
	st->m_cnsPage           = r->getLong ("cnsp"  , true );
	char *langAbbr = r->getString("qlang",NULL);
	st->m_langId = langUnknown;
	if ( langAbbr ) {
		uint8_t langId = getLangIdFromAbbr ( langAbbr );
		st->m_langId = langId;
	}
	strncpy ( st->m_coll , coll , MAX_COLL_LEN+1 );
	// store query for query highlighting
	st->m_netTestResults    = r->getLong ("rnettest", false );
	//if( st->m_netTestResults ) {
	//	mdelete ( st , sizeof(State2) , "PageGet1" );
	//	delete ( st );
	//	return sendPageNetResult( s );
	//}
	//if ( q && qlen > 0 ) strcpy ( st->m_q , q );
	//else                 st->m_q[0] = '\0';

	st->m_qsb.setBuf ( st->m_qtmpBuf,128,0,false );
	st->m_qsb.setLabel ( "qsbpg" );

	// save the query
	if ( q && qlen > 0 )
		st->m_qsb.safeStrcpy ( q );
	
	st->m_qlen = qlen;
	//st->m_seq      = seq;
	st->m_rtq      = rtq;
	st->m_boolFlag = r->getLong ("bq", 2 /*default is 2*/ );
	st->m_isBanned = false;
	st->m_noArchive = false;
	st->m_socket = s;
	st->m_format = r->getReplyFormat();
	// default to 0 niceness
	st->m_niceness = 0;
	st->m_r.copy ( r );
	//st->m_cr = cr;
	st->m_printDisclaimer = true;
	if ( st->m_cnsPage )
		st->m_printDisclaimer = false;
	if ( st->m_strip ) // ! st->m_evbits.isEmpty() ) 
		st->m_printDisclaimer = false;
	
	// should we cache it?
	char useCache = r->getLong ( "usecache" ,  1 );
	char rcache   = r->getLong ( "rcache"   ,  1 );
	char wcache   = r->getLong ( "wcache"   ,  1 );
	int32_t cacheAge = r->getLong ( "cacheAge" , 60*60 ); // default one hour
	if ( useCache == 0 ) { cacheAge = 0; wcache = 0; }
	if ( rcache   == 0 )   cacheAge = 0; 
	// . fetch the TitleRec
	// . a max cache age of 0 means not to read from the cache
	XmlDoc *xd = &st->m_xd;
	// url based?
	if ( url ) {
		SpiderRequest sreq;
		sreq.reset();
		strcpy(sreq.m_url, url );
		sreq.setDataSize();
		// this returns false if "coll" is invalid
		if ( ! xd->set4 ( &sreq , NULL , coll , NULL , st->m_niceness ) ) 
			goto hadSetError;
	}
	// . when getTitleRec() is called it will load the old one
	//   since XmlDoc::m_setFromTitleRec will be true
	// . niceness is 0
	// . use st->m_coll since XmlDoc just points to it!
	// . this returns false if "coll" is invalid
	else if ( ! xd->set3 ( docId , st->m_coll , 0 ) ) {
	hadSetError:
		mdelete ( st , sizeof(State2) , "PageGet1" );
		delete ( st );
		g_errno = ENOMEM;
		log("PageGet: set3: %s", mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	// if it blocks while it loads title rec, it will re-call this routine
	xd->setCallback ( st , processLoopWrapper );
	// good to go!
	return processLoop ( st );
}

// returns true
bool sendErrorReply ( void *state , int32_t err ) {
	// ensure this is set
	if ( ! err ) { char *xx=NULL;*xx=0; }
	// get it
	State2 *st = (State2 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_socket;

	char tmp [ 1024*32 ] ;
	sprintf ( tmp , "%s",
		  mstrerror(g_errno));
	// nuke state2
	mdelete ( st , sizeof(State2) , "PageGet1" );
	delete (st);
	// erase g_errno for sending
	//g_errno = 0;
	// . now encapsulate it in html head/tail and send it off
	//return g_httpServer.sendDynamicPage ( s , tmp , gbstrlen(tmp) );
	return g_httpServer.sendErrorReply ( s, err, mstrerror(err) );
}


void processLoopWrapper ( void *state ) {
	processLoop ( state );
}



// returns false if blocked, true otherwise
bool processLoop ( void *state ) {
	// get it
	State2 *st = (State2 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_socket;
	// get it
	XmlDoc *xd = &st->m_xd;

	if ( ! xd->m_loaded ) {
		// setting just the docid. niceness is 0.
		//xd->set3 ( st->m_docId , st->m_coll , 0 );
		// callback
		xd->setCallback ( state , processLoop );
		// . and tell it to load from the old title rec
		// . this sets xd->m_oldTitleRec/m_oldTitleRecSize
		// . this sets xd->ptr_* and all other member vars from
		//   the old title rec if found in titledb.
		if ( ! xd->loadFromOldTitleRec ( ) ) return false;
	}

	if ( g_errno ) return sendErrorReply ( st , g_errno );
	// now force it to load old title rec
	//char **tr = xd->getTitleRec();
	SafeBuf *tr = xd->getTitleRecBuf();
	// blocked? return false if so. it will call processLoop() when it rets
	if ( tr == (void *)-1 ) return false;
	// we did not block. check for error? this will free "st" too.
	if ( ! tr ) return sendErrorReply ( st , g_errno );
	// if title rec was empty, that is a problem
	if ( xd->m_titleRecBuf.length() == 0 ) 
		return sendErrorReply ( st , ENOTFOUND);

	// set callback
	char *na = xd->getIsNoArchive();
	// wait if blocked
	if ( na == (void *)-1 ) return false;
	// error?
	if ( ! na ) return sendErrorReply ( st , g_errno );
	// forbidden? allow turkeys through though...
	if ( ! st->m_isMasterAdmin && *na )
		return sendErrorReply ( st , ENOCACHE );

	SafeBuf *sb = &st->m_sb;


	// &page=4 will print rainbow sections
	if ( ! st->m_printed && st->m_r.getLong("page",0) ) {
		// do not repeat this call
		st->m_printed = true;
		// this will call us again since we called
		// xd->setCallback() above to us
		if ( ! xd->printDocForProCog ( sb , &st->m_r ) )
			return false;
	}

	char *contentType = "text/html";
	char format = st->m_format;
	if ( format == FORMAT_XML ) contentType = "text/xml";
	if ( format == FORMAT_JSON ) contentType = "application/json";

	// if we printed a special page (like rainbow sections) then return now
	if ( st->m_printed ) {
		bool status = g_httpServer.sendDynamicPage (s,
							    //buf,bufLen,
							    sb->getBufStart(),
							    sb->getLength(),
							    -1,false,
							    //"text/html",
							    contentType,
							    -1, NULL, "utf8" );
		// nuke state2
		mdelete ( st , sizeof(State2) , "PageGet1" );
		delete (st);
		return status;
	}

	/*
	  // this was calling XmlDoc and setting sections, etc. to
	  // get the SpiderReply junk... no no no
	// is it banned or filtered? this ignores the TagRec in the titleRec
	// and uses msg8a to get it fresh instead
	char *vi = xd->getIsFiltered();//Visible( );
	// wait if blocked
	if ( vi == (void *)-1 ) return false;
	// error?
	if ( ! vi ) return sendErrorReply ( st , g_errno );
	// banned?
	if ( ! st->m_isMasterAdmin && ! *vi ) return sendErrorReply (st,EDOCBANNED);
	*/

	// get the utf8 content
	char **utf8 = xd->getUtf8Content();
	//int32_t   len  = xd->size_utf8Content - 1;
	// wait if blocked???
	if ( utf8 == (void *)-1 ) return false;
	// strange
	if ( xd->size_utf8Content<=0) {
		log("pageget: utf8 content <= 0");
		return sendErrorReply(st,EBADENGINEER );
	}
	// alloc error?
	if ( ! utf8 ) return sendErrorReply ( st , g_errno );

	// get this host
	Host *h = g_hostdb.getHost ( g_hostdb.m_hostId );
	if ( ! h ) {
		log("pageget: hostid %"INT32" is bad",g_hostdb.m_hostId);
		return sendErrorReply(st,EBADENGINEER );
	}


	char *content    = xd->ptr_utf8Content;
	int32_t  contentLen = xd->size_utf8Content - 1;

	// int16_tcut
	char strip = st->m_strip;

	// alloc buffer now
	//char *buf = NULL;
	//int32_t  bufMaxSize = 0;
	//bufMaxSize = len + ( 32 * 1024 ) ;
	//bufMaxSize = contentLen + ( 32 * 1024 ) ;
	//buf        = (char *)mmalloc ( bufMaxSize , "PageGet2" );
	//char *p          = buf;
	//char *bufEnd     = buf + bufMaxSize;
	//if ( ! buf ) {
	//	return sendErrorReply ( st , g_errno );
	//}

	// for undoing the header
	//char *start1 = p;
	int32_t startLen1 = sb->length();

	// we are always utfu
	if ( strip != 2 )
		sb->safePrintf( "<meta http-equiv=\"Content-Type\" "
			     "content=\"text/html;charset=utf8\">\n");

	// base href
	//Url *base = &xd->m_firstUrl;
	//if ( xd->ptr_redirUrl.m_url[0] )
	//	base = &xd->m_redirUrl;
	char *base = xd->ptr_firstUrl;
	if ( xd->ptr_redirUrl ) base = xd->ptr_redirUrl;
	//Url *redir = *xd->getRedirUrl();
	if ( strip != 2 ) {
		sb->safePrintf ( "<BASE HREF=\"%s\">" , base );
		//p += gbstrlen ( p );
	}

	// default colors in case css files missing
	if ( strip != 2 ) {
		sb->safePrintf( "\n<style type=\"text/css\">\n"
			  "body{background-color:white;color:black;}\n"
			  "</style>\n");
		//p += gbstrlen ( p );
	}

	//char format = st->m_format;
	if ( format == FORMAT_XML ) sb->reset();
	if ( format == FORMAT_JSON ) sb->reset();

	if ( xd->m_contentType == CT_JSON ) sb->reset();
	if ( xd->m_contentType == CT_XML  ) sb->reset();
	if ( xd->m_contentType == CT_STATUS ) sb->reset();

	// for undoing the stuff below
	int32_t startLen2 = sb->length();//p;

	// query should be NULL terminated
	char *q    = st->m_qsb.getBufStart();
	int32_t  qlen = st->m_qsb.getLength(); // m_qlen;

	char styleTitle[128] =  "font-size:14px;font-weight:600;"
				"color:#000000;";
	char styleText[128]  =  "font-size:14px;font-weight:400;"
				"color:#000000;";
	char styleLink[128] =  "font-size:14px;font-weight:400;"
				"color:#0000ff;";
	char styleTell[128] =  "font-size:14px;font-weight:600;"
				"color:#cc0000;";

	// get the url of the title rec
	Url *f = xd->getFirstUrl();

	bool printDisclaimer = st->m_printDisclaimer;

	if ( xd->m_contentType == CT_JSON )
		printDisclaimer = false;

	if ( xd->m_contentType == CT_STATUS )
		printDisclaimer = false;

	if ( format == FORMAT_XML ) printDisclaimer = false;
	if ( format == FORMAT_JSON ) printDisclaimer = false;

	char tbuf[100];
	tbuf[0] = 0;
	time_t lastSpiderDate = xd->m_spideredTime;

	if ( printDisclaimer ||
	     format == FORMAT_XML ||
	     format == FORMAT_JSON ) {
		struct tm *timeStruct = gmtime ( &lastSpiderDate );
		strftime ( tbuf, 100,"%b %d, %Y UTC", timeStruct);
	}

	// We should always be displaying this disclaimer.
	// - May eventually want to display this at a different location
	//   on the page, or on the click 'n' scroll browser page itself
	//   when this page is not being viewed solo.
	// CNS: if ( ! st->m_clickNScroll ) {
	if ( printDisclaimer ) {

		sb->safePrintf(//sprintf ( p , 
			  //"<BASE HREF=\"%s\">"
			  //"<table border=1 width=100%%>"
			  //"<tr><td>"
			  "<table border=\"1\" bgcolor=\"#"
			  BGCOLOR
			  "\" cellpadding=\"10\" "
			  //"id=\"gbcnsdisctable\" class=\"gbcnsdisctable_v\""
			  "cellspacing=\"0\" width=\"100%%\" color=\"#ffffff\">"
			  "<tr"
			  //" id=\"gbcnsdisctr\" class=\"gbcnsdisctr_v\""
			  "><td>"
			  //"<font face=times,sans-serif color=black size=-1>"
			  "<span style=\"%s\">"
			  "This is <a href=/>Gigablast<a>'s cached page of </span>"
			  "<a href=\"%s\" style=\"%s\">%s</a>"
			  "" , styleTitle, f->getUrl(), styleLink,
			  f->getUrl() );
		//p += gbstrlen ( p );
		// then the rest
		//sprintf(p , 
		sb->safePrintf(
			"<span style=\"%s\">. "
			"Gigablast is not responsible for the content of "
			"this page.</span>", styleTitle );
		//p += gbstrlen ( p );

		sb->safePrintf ( "<br/><span style=\"%s\">"
			  "Cached: </span>"
			  "<span style=\"%s\">",
			  styleTitle, styleText );
		//p += gbstrlen ( p );

		// then the spider date in GMT
		// time_t lastSpiderDate = xd->m_spideredTime;
		// struct tm *timeStruct = gmtime ( &lastSpiderDate );
		// char tbuf[100];
		// strftime ( tbuf, 100,"%b %d, %Y UTC", timeStruct);
		//p += gbstrlen ( p );
		sb->safeStrcpy(tbuf);

		// Moved over from PageResults.cpp
		sb->safePrintf( "</span> - <a href=\""
			      "/get?"
			      "q=%s&amp;c=%s&amp;rtq=%"INT32"&amp;"
			      "d=%"INT64"&amp;strip=1\""
			      " style=\"%s\">"
			      "[stripped]</a>", 
			      q , st->m_coll , 
			      (int32_t)st->m_rtq,
			      st->m_docId, styleLink ); 

		// a link to alexa
		if ( f->getUrlLen() > 5 ) {
			sb->safePrintf( " - <a href=\"http:"
					 "//web.archive.org/web/*/%s\""
					 " style=\"%s\">"
					 "[older copies]</a>" ,
					 f->getUrl(), styleLink );
		}

		if (st->m_noArchive){
			sb->safePrintf( " - <span style=\"%s\"><b>"
				     "[NOARCHIVE]</b></span>",
				     styleTell );
		}
		if (st->m_isBanned){
			sb->safePrintf(" - <span style=\"%s\"><b>"
				     "[BANNED]</b></span>",
				     styleTell );
		}

		// only print this if we got a query
		if ( qlen > 0 ) {
			sb->safePrintf("<br/><br/><span style=\"%s\"> "
				   "These search terms have been "
				   "highlighted:  ",
				   styleText );
			//p += gbstrlen ( p );
		}
		
	}

	// how much space left in p?
	//int32_t avail = bufEnd - p;
	// . make the url that we're outputting for (like in PageResults.cpp)
	// . "thisUrl" is the baseUrl for click & scroll
	char thisUrl[MAX_URL_LEN];
	char *thisUrlEnd = thisUrl + MAX_URL_LEN;
	char *x = thisUrl;
	// . use the external ip of our gateway
	// . construct the NAT mapped port
	// . you should have used iptables to map port to the correct
	//   internal ip:port
	//uint32_t  ip   =g_conf.m_mainExternalIp  ; // h->m_externalIp;
	//uint16_t port=g_conf.m_mainExternalPort;//h->m_externalHttpPort
	// local check
	//if ( st->m_isLocal ) {
	uint32_t  ip   = h->m_ip;
	uint16_t port = h->m_httpPort;
	//}
	//sprintf ( x , "http://%s:%"INT32"/get?q=" , iptoa ( ip ) , port );
	// . we no longer put the port in here
	// . but still need http:// since we use <base href=>
	if (port == 80) sprintf(x,"http://%s/get?q=",iptoa(ip));
	else            sprintf(x,"http://%s:%hu/get?q=",iptoa(ip),port);
	x += gbstrlen ( x );
	// the query url encoded
	int32_t elen = urlEncode ( x , thisUrlEnd - x , q , qlen );
	x += elen;
	// separate cgi vars with a &
	//sprintf ( x, "&seq=%"INT32"&rtq=%"INT32"d=%"INT64"",
	//	  (int32_t)st->m_seq,(int32_t)st->m_rtq,st->m_msg22.getDocId());
	sprintf ( x, "&d=%"INT64"",st->m_docId );
	x += gbstrlen(x);		
	// set our query for highlighting
	Query qq;
	qq.set2 ( q, st->m_langId , true );

	// print the query terms into our highlight buffer
	Highlight hi;
	// make words so we can set the scores to ignore fielded terms
	Words qw;
	qw.set ( q            ,  // content being highlighted, utf8
		 qlen         ,  // content being highlighted, utf8
		 TITLEREC_CURRENT_VERSION,
		 true         ,  // computeIds
		 false        ); // hasHtmlEntities?
	// . assign scores of 0 to query words that should be ignored
	// . TRICKY: loop over words in qq.m_qwords, but they should be 1-1
	//   with words in qw.
	// . sanity check
	//if ( qw.getNumWords() != qq.m_numWords ) { char *xx = NULL; *xx = 0;}
	// declare up here
	Matches m;
	// do the loop
	//Scores ss;
	//ss.set ( &qw , NULL );
	//for ( int32_t i = 0 ; i < qq.m_numWords ; i++ )
	//	if ( ! m.matchWord ( &qq.m_qwords[i],i ) ) ss.m_scores[i] = 0;
	// now set m.m_matches[] to those words in qw that match a query word
	// or phrase in qq.
	m.setQuery ( &qq );
	//m.addMatches ( &qw , &ss , true );
	m.addMatches ( &qw );
	int32_t hilen = 0;

	// CNS: if ( ! st->m_clickNScroll ) {
	// and highlight the matches
	if ( printDisclaimer ) {
		hilen = hi.set ( //p       ,
				 //avail   ,
				sb ,
				 &qw     , // words to highlight
				 &m      , // matches relative to qw
				 false   , // doSteming
				 false   , // st->m_clickAndScroll , 
				 (char *)thisUrl );// base url for ClcknScrll
		//p += hilen;
		// now an hr
		//gbmemcpy ( p , "</span></table></table>\n" , 24 );   p += 24;
		sb->safeStrcpy("</span></table></table>\n");
	}


	bool includeHeader = st->m_includeHeader;

	// do not show header for json object display
	if ( xd->m_contentType == CT_JSON )
		includeHeader = false;
	if ( xd->m_contentType == CT_XML )
		includeHeader = false;
	if ( xd->m_contentType == CT_STATUS )
		includeHeader = false;

	if ( format == FORMAT_XML ) includeHeader = false;
	if ( format == FORMAT_JSON ) includeHeader = false;

	//mfree(uq, uqCapacity, "PageGet");
	// undo the header writes if we should
	if ( ! includeHeader ) {
		// including base href is off by default when not including
		// the header, so the caller must explicitly turn it back on
		if ( st->m_includeBaseHref ) sb->m_length=startLen2;//p=start2;
		else                         sb->m_length=startLen1;//p=start1;
	}

	//sb->safeStrcpy(tbuf);



	if ( format == FORMAT_XML ) {
		sb->safePrintf("<response>\n");
		sb->safePrintf("<statusCode>0</statusCode>\n");
		sb->safePrintf("<statusMsg>Success</statusMsg>\n");
		sb->safePrintf("<url><![CDATA[");
		sb->cdataEncode(xd->m_firstUrl.m_url);
		sb->safePrintf("]]></url>\n");
		sb->safePrintf("<docId>%"UINT64"</docId>\n",xd->m_docId);
		sb->safePrintf("\t<cachedTimeUTC>%"INT32"</cachedTimeUTC>\n",
			       (int32_t)lastSpiderDate);
		sb->safePrintf("\t<cachedTimeStr>%s</cachedTimeStr>\n",tbuf);
	}

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("{\"response\":{\n");
		sb->safePrintf("\t\"statusCode\":0,\n");
		sb->safePrintf("\t\"statusMsg\":\"Success\",\n");
		sb->safePrintf("\t\"url\":\"");
		sb->jsonEncode(xd->m_firstUrl.m_url);
		sb->safePrintf("\",\n");
		sb->safePrintf("\t\"docId\":%"UINT64",\n",xd->m_docId);
		sb->safePrintf("\t\"cachedTimeUTC\":%"INT32",\n",
			       (int32_t)lastSpiderDate);
		sb->safePrintf("\t\"cachedTimeStr\":\"%s\",\n",tbuf);
	}

	
	// identify start of <title> tag we wrote out
	char *sbstart = sb->getBufStart();
	char *sbend   = sb->getBufEnd();
	char *titleStart = NULL;
	char *titleEnd   = NULL;

	char ctype = (char)xd->m_contentType;

	// do not calc title or print it if doc is xml or json
	if ( ctype == CT_XML ) sbend = sbstart;
	if ( ctype == CT_JSON ) sbend = sbstart;
	if ( ctype == CT_STATUS ) sbend = sbstart;

	for ( char *t = sbstart ; t < sbend ; t++ ) {
		// title tag?
		if ( t[0]!='<' ) continue;
		if ( to_lower_a(t[1])!='t' ) continue;
		if ( to_lower_a(t[2])!='i' ) continue;
		if ( to_lower_a(t[3])!='t' ) continue;
		if ( to_lower_a(t[4])!='l' ) continue;
		if ( to_lower_a(t[5])!='e' ) continue;
		// point to it
		char *x = t + 5;
		// max - to keep things fast
		char *max = x + 500;
		for ( ; *x && *x != '>' && x < max ; x++ );
		x++;
		// find end
		char *e = x;
		for ( ; *e && e < max ; e++ ) {
			if ( e[0]=='<' &&
			     to_lower_a(e[1])=='/' &&
			     to_lower_a(e[2])=='t' &&
			     to_lower_a(e[3])=='i' &&
			     to_lower_a(e[4])=='t' &&
			     to_lower_a(e[5])=='l' &&
			     to_lower_a(e[6])=='e' )
				break;
		}
		if ( e < max ) {
			titleStart = x;
			titleEnd   = e;
		}
		break;
	}

	// . print title at top!
	// . consider moving
	if ( titleStart ) {

		char *ebuf = st->m_r.getString("eb");
		if ( ! ebuf ) ebuf = "";

		//p += sprintf ( p , 
		sb->safePrintf(
			       "<table border=1 "
			       "cellpadding=10 "
			       "cellspacing=0 "
			       "width=100%% "
			       "color=#ffffff>" );

		int32_t printLinks = st->m_r.getLong("links",0);

		if ( ! printDisclaimer && printLinks )
			sb->safePrintf(//p += sprintf ( p , 
				       // first put cached and live link
				       "<tr>"
				       "<td bgcolor=lightyellow>"
				       // print cached link
				       //"<center>"
				       "&nbsp; "
				       "<b>"
				       "<a "
				       "style=\"font-size:18px;font-weight:600;"
				       "color:#000000;\" "
				       "href=\""
				       "/get?"
				       "c=%s&d=%"INT64"&qh=0&cnsp=1&eb=%s\">"
				       "cached link</a>"
				       " &nbsp; "
				       "<a "
				       "style=\"font-size:18px;font-weight:600;"
				       "color:#000000;\" "
				       "href=%s>live link</a>"
				       "</b>"
				       //"</center>"
				       "</td>"
				       "</tr>\n"
				       ,st->m_coll
				       ,st->m_docId 
				       ,ebuf
				       ,thisUrl // st->ptr_ubuf
				       );

		if ( printLinks ) {
			sb->safePrintf(//p += sprintf ( p ,
				       "<tr><td bgcolor=pink>"
				       "<span style=\"font-size:18px;"
				       "font-weight:600;"
				       "color:#000000;\">"
				       "&nbsp; "
				       "<b>PAGE TITLE:</b> "
				       );
			int32_t tlen = titleEnd - titleStart;
			sb->safeMemcpy ( titleStart , tlen );
			sb->safePrintf ( "</span></td></tr>" );
		}

		sb->safePrintf( "</table><br>\n" );

	}

	// is the content preformatted?
	bool pre = false;
	if ( ctype == CT_TEXT ) pre = true ; // text/plain
	if ( ctype == CT_DOC  ) pre = true ; // filtered msword
	if ( ctype == CT_PS   ) pre = true ; // filtered postscript

	if ( format == FORMAT_XML ) pre = false;
	if ( format == FORMAT_JSON ) pre = false;

	// if it is content-type text, add a <pre>
	if ( pre ) {//p + 5 < bufEnd && pre ) {
		sb->safePrintf("<pre>");
		//p += 5;
	}

	if ( st->m_strip == 1 )
		contentLen = stripHtml( content, contentLen, 
					(int32_t)xd->m_version, st->m_strip );
	// it returns -1 and sets g_errno on error, line OOM
	if ( contentLen == -1 ) {
		//if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );	
		return sendErrorReply ( st , g_errno );
	}

	Xml xml;
	Words ww;

	// if no highlighting, skip it
	bool queryHighlighting = st->m_queryHighlighting;
	if ( st->m_strip == 2 ) queryHighlighting = false;

	// do not do term highlighting if json
	if ( xd->m_contentType == CT_JSON )
		queryHighlighting = false;
	if ( xd->m_contentType == CT_STATUS )
		queryHighlighting = false;

	SafeBuf tmp;
	SafeBuf *xb = sb;
	if ( format == FORMAT_XML ) xb = &tmp;
	if ( format == FORMAT_JSON ) xb = &tmp;
	

	if ( ! queryHighlighting ) {
		xb->safeMemcpy ( content , contentLen );
		xb->nullTerm();
		//p += contentLen ;
	}
	else {
		// get the content as xhtml (should be NULL terminated)
		//Words *ww = xd->getWords();
		if ( ! xml.set ( content , contentLen , false ,
				 0 , false , TITLEREC_CURRENT_VERSION ,
				 false , 0 , CT_HTML ) ) { // niceness is 0
			//if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );
			return sendErrorReply ( st , g_errno );
		}			
		if ( ! ww.set ( &xml , true , 0 ) ) { // niceness is 0
			//if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );
			return sendErrorReply ( st , g_errno );
		}
		// sanity check
		//if ( ! xd->m_wordsValid ) { char *xx=NULL;*xx=0; }
		// how much space left in p?
		//avail = bufEnd - p;

		Matches m;
		m.setQuery ( &qq );
		m.addMatches ( &ww );
		hilen = hi.set ( xb , // p , avail , 
				 &ww , &m ,
				 false /*doStemming?*/ ,  
				 st->m_clickAndScroll , 
				 thisUrl /*base url for click & scroll*/);
		//p += hilen;
		log(LOG_DEBUG, "query: Done highlighting cached page content");
	}


	if ( format == FORMAT_XML ) {
		sb->safePrintf("\t<content><![CDATA[");
		sb->cdataEncode ( xb->getBufStart() );
		sb->safePrintf("]]></content>\n");
		sb->safePrintf("</response>\n");
	}

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("\t\"content\":\"\n");
		sb->jsonEncode ( xb->getBufStart() );
		sb->safePrintf("\"\n}\n}\n");
	}


	// if it is content-type text, add a </pre>
	if ( pre ) { // p + 6 < bufEnd && pre ) {
		sb->safeMemcpy ( "</pre>" , 6 );
		//p += 6;
	}

	// calculate bufLen
	//int32_t bufLen = p - buf;

	/*

	  MDW: return the xml page as is now. 9/28/2014

	int32_t ct = xd->m_contentType;

	// now filter the entire buffer to escape out the xml tags
	// so it is displayed nice
	SafeBuf newbuf;

	if ( ct == CT_XML ) {
		// encode the xml tags into &lt;tagname&gt; sequences
		if ( !newbuf.htmlEncodeXmlTags ( sb->getBufStart() ,
						 sb->getLength(),
						 0)){// niceness=0
			//if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );
			return sendErrorReply ( st , g_errno );
		}
		// free out buffer that we alloc'd before returning since this
		// should have copied it into another buffer
		//if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );	
		// reassign
		//buf    = newbuf.getBufStart();
		//bufLen = newbuf.length();
		sb->stealBuf ( &newbuf );
	}
	*/

	// now encapsulate it in html head/tail and send it off
	// sendErr:
	contentType = "text/html";
	if ( strip == 2 ) contentType = "text/xml";
	// xml is usually buggy and this throws browser off
	//if ( ctype == CT_XML ) contentType = "text/xml";

	if ( xd->m_contentType == CT_JSON )
		contentType = "application/json";

	if ( xd->m_contentType == CT_STATUS )
		contentType = "application/json";

	if ( xd->m_contentType == CT_XML )
		contentType = "test/xml";

	if ( format == FORMAT_XML ) contentType = "text/xml";
	if ( format == FORMAT_JSON ) contentType = "application/json";

	// safebuf, sb, is a member of "st" so this should copy the buffer
	// when it constructs the http reply, and we gotta call delete(st)
	// AFTER this so sb is still valid.
	bool status = g_httpServer.sendDynamicPage (s,
						    //buf,bufLen,
						    sb->getBufStart(),
						    sb->getLength(),
						    -1,false,
						    contentType,
						     -1, NULL, "utf8" );

	// nuke state2
	mdelete ( st , sizeof(State2) , "PageGet1" );
	delete (st);


	// free out buffer that we alloc'd before returning since this
	// should have copied it into another buffer

	//if      ( ct == CT_XML ) newbuf.purge();
	//else if ( buf          ) mfree ( buf , bufMaxSize , "PageGet2" );
	
	// and convey the status
	return status;
}
