#include "gb-include.h"

#include "SafeBuf.h"
#include "Collectiondb.h"
#include "CollectionRec.h"
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

static bool sendErrorReply ( void *state , long err ) ;
static void processLoopWrapper ( void *state ) ;
static bool processLoop ( void *state ) ;

class State2 {
public:
	Msg22      m_msg22;
	//TitleRec   m_tr;
	long       m_niceness;
	XmlDoc     m_xd;
	char      *m_tr;
	long       m_trSize;
	uint8_t    m_langId;
	//Msg8a      m_msg8a;
	//SiteRec    m_sr;
	//TagRec     m_tagRec;
	TcpSocket *m_socket;
	HttpRequest m_r;
	char m_coll[50];
	//CollectionRec *m_cr;
	bool       m_isAdmin;
	bool       m_isLocal;
	//bool       m_seq;
	bool       m_rtq;
	char       m_q[MAX_QUERY_LEN+1];
	long       m_qlen;
	char       m_boolFlag;
	long long  m_docId;
	bool       m_includeHeader;
	bool       m_includeBaseHref;
	bool       m_queryHighlighting;
	long       m_strip;
	bool       m_clickAndScroll;
	bool	   m_clickNScroll; // new click 'n' scroll
	bool	   m_cnsPage;      // Are we in the click 'n' scroll page?
	bool	   m_printDisclaimer;
	bool       m_netTestResults;
	bool       m_isBanned;
	bool       m_noArchive;
};
	

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageGet ( TcpSocket *s , HttpRequest *r ) {
	// get the collection
	long  collLen = 0;
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
		g_errno = ENOTFOUND;
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
	long  qlen = 0;
	char *q = r->getString ( "q" , &qlen , NULL /*default*/);
	// ensure query not too big
	if ( qlen >= MAX_QUERY_LEN-1 ) { 
		g_errno=EQUERYTOOBIG; 
		return g_httpServer.sendErrorReply (s,500 ,mstrerror(g_errno));
	}
	// the docId
	long  long docId = r->getLongLong ( "d" , 0LL /*default*/ );
	// get url
	char *url = r->getString ( "u",NULL);
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
		log("PageGet: new(%i): %s", sizeof(State2),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State2) , "PageGet1" );
	// save the socket and if Host: is local in the Http request Mime
	st->m_socket   = s;
	st->m_isAdmin  = g_collectiondb.isAdmin ( r , s );
	st->m_isLocal  = r->isLocal();
	st->m_docId    = docId;
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
	strncpy ( st->m_coll , coll , 40 );
	// store query for query highlighting
	st->m_netTestResults    = r->getLong ("rnettest", false );
	if( st->m_netTestResults ) {
		mdelete ( st , sizeof(State2) , "PageGet1" );
		delete ( st );
		return sendPageNetResult( s );
	}
	if ( q && qlen > 0 ) strcpy ( st->m_q , q );
	else                 st->m_q[0] = '\0';
	st->m_qlen = qlen;
	//st->m_seq      = seq;
	st->m_rtq      = rtq;
	st->m_boolFlag = r->getLong ("bq", 2 /*default is 2*/ );
	st->m_isBanned = false;
	st->m_noArchive = false;
	st->m_socket = s;
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
	long cacheAge = r->getLong ( "cacheAge" , 60*60 ); // default one hour
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
		xd->set4 ( &sreq , NULL , coll , NULL , st->m_niceness ); 
	}
	// . when getTitleRec() is called it will load the old one
	//   since XmlDoc::m_setFromTitleRec will be true
	// . niceness is 0
	else {
		xd->set3 ( docId , coll , 0 );
	}
	// if it blocks while it loads title rec, it will re-call this routine
	xd->setCallback ( st , processLoopWrapper );
	// good to go!
	return processLoop ( st );
}

// returns true
bool sendErrorReply ( void *state , long err ) {
	// ensure this is set
	if ( ! err ) { char *xx=NULL;*xx=0; }
	// get it
	State2 *st = (State2 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_socket;

	char tmp [ 1024*32 ] ;
	sprintf ( tmp , "<b>had server-side error: %s</b><br>",
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
		// and tell it to load from the old title rec
		if ( ! xd->loadFromOldTitleRec ( ) ) return false;
	}

	if ( g_errno ) return sendErrorReply ( st , g_errno );
	// now force it to load old title rec
	char **tr = xd->getTitleRec();
	// blocked? return false if so. it will call processLoop() when it rets
	if ( tr == (void *)-1 ) return false;
	// we did not block. check for error? this will free "st" too.
	if ( ! tr ) return sendErrorReply ( st , g_errno );
	// if title rec was empty, that is a problem
	if ( xd->m_titleRecSize == 0 ) return sendErrorReply ( st , ENOTFOUND);

	// set callback
	char *na = xd->getIsNoArchive();
	// wait if blocked
	if ( na == (void *)-1 ) return false;
	// error?
	if ( ! na ) return sendErrorReply ( st , g_errno );
	// forbidden? allow turkeys through though...
	if ( ! st->m_isAdmin && *na )
		return sendErrorReply ( st , ENOCACHE );

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
	if ( ! st->m_isAdmin && ! *vi ) return sendErrorReply (st,EDOCBANNED);
	*/

	// get the utf8 content
	char **utf8 = xd->getUtf8Content();
	//long   len  = xd->size_utf8Content - 1;
	// wait if blocked???
	if ( utf8 == (void *)-1 ) return false;
	// strange
	if ( xd->size_utf8Content<=0) return sendErrorReply(st,EBADENGINEER );
	// alloc error?
	if ( ! utf8 ) return sendErrorReply ( st , g_errno );

	// get this host
	Host *h = g_hostdb.getHost ( g_hostdb.m_hostId );
	if ( ! h ) return sendErrorReply ( st , EBADENGINEER );


	char *content    = xd->ptr_utf8Content;
	long  contentLen = xd->size_utf8Content - 1;

	// shortcut
	char strip = st->m_strip;

	SafeBuf sb;

	// alloc buffer now
	char *buf = NULL;
	long  bufMaxSize = 0;
	//bufMaxSize = len + ( 32 * 1024 ) ;
	bufMaxSize = contentLen + ( 32 * 1024 ) ;
	buf        = (char *)mmalloc ( bufMaxSize , "PageGet2" );
	char *p          = buf;
	char *bufEnd     = buf + bufMaxSize;
	if ( ! buf ) {
		return sendErrorReply ( st , g_errno );
	}

	// for undoing the header
	char *start1 = p;

	// we are always utfu
	if ( strip != 2 )
		p += sprintf(p, "<meta http-equiv=\"Content-Type\" "
			     "content=\"text/html;charset=utf8\">\n");

	// base href
	//Url *base = &xd->m_firstUrl;
	//if ( xd->ptr_redirUrl.m_url[0] )
	//	base = &xd->m_redirUrl;
	char *base = xd->ptr_firstUrl;
	if ( xd->ptr_redirUrl ) base = xd->ptr_redirUrl;
	//Url *redir = *xd->getRedirUrl();
	if ( strip != 2 ) {
		sprintf ( p , "<BASE HREF=\"%s\">" , base );
		p += gbstrlen ( p );
	}

	// default colors in case css files missing
	if ( strip != 2 ) {
		sprintf ( p , "\n<style type=\"text/css\">\n"
			  "body{background-color:white;color:black;}\n"
			  "</style>\n");
		p += gbstrlen ( p );
	}


	char *start2 = p;

	// query should be NULL terminated
	char *q    = st->m_q;
	long  qlen = st->m_qlen;

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


	// We should always be displaying this disclaimer.
	// - May eventually want to display this at a different location
	//   on the page, or on the click 'n' scroll browser page itself
	//   when this page is not being viewed solo.
	// CNS: if ( ! st->m_clickNScroll ) {
	if ( st->m_printDisclaimer ) {

		sprintf ( p , 
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
			  "This is Gigablast's cached page of </span>"
			  "<a href=\"%s\" style=\"%s\">%s</a>"
			  "" , styleTitle, f->getUrl(), styleLink,
			  f->getUrl() );
		p += gbstrlen ( p );
		// then the rest
		sprintf(p , 
			"<span style=\"%s\">. "
			"Gigablast is not responsible for the content of "
			"this page.</span>", styleTitle );
		p += gbstrlen ( p );

		sprintf ( p , "<br/><span style=\"%s\">"
			  "Cached: </span>"
			  "<span style=\"%s\">",
			  styleTitle, styleText );
		p += gbstrlen ( p );

		// then the spider date in GMT
		time_t lastSpiderDate = xd->m_spideredTime;
		struct tm *timeStruct = gmtime ( &lastSpiderDate );
		strftime ( p, 100,"%b %d, %Y UTC", timeStruct);
		p += gbstrlen ( p );

		// Moved over from PageResults.cpp
		p += sprintf (p, "</span> - <a href=\""
			      "/get?"
			      "q=%s&amp;c=%s&amp;rtq=%li&amp;"
			      "d=%lli&amp;strip=1\""
			      " style=\"%s\">"
			      "[stripped]</a>", 
			      q , st->m_coll , 
			      (long)st->m_rtq,
			      st->m_docId, styleLink ); 

		// a link to alexa
		if ( f->getUrlLen() > 5 ) {
			p += sprintf (p, " - <a href=\"http:"
					 "//web.archive.org/web/*/%s\""
					 " style=\"%s\">"
					 "[older copies]</a>" ,
					 f->getUrl(), styleLink );
		}

		if (st->m_noArchive){
			p += sprintf(p, " - <span style=\"%s\"><b>"
				     "[NOARCHIVE]</b></span>",
				     styleTell );
		}
		if (st->m_isBanned){
			p += sprintf(p, " - <span style=\"%s\"><b>"
				     "[BANNED]</b></span>",
				     styleTell );
		}

		// only print this if we got a query
		if ( qlen > 0 ) {
			sprintf (p,"<br/><br/><span style=\"%s\"> "
				   "These search terms have been "
				   "highlighted:  ",
				   styleText );
			p += gbstrlen ( p );
		}
		
	}

	// how much space left in p?
	long avail = bufEnd - p;
	// . make the url that we're outputting for (like in PageResults.cpp)
	// . "thisUrl" is the baseUrl for click & scroll
	char thisUrl[MAX_URL_LEN];
	char *thisUrlEnd = thisUrl + MAX_URL_LEN;
	char *x = thisUrl;
	// . use the external ip of our gateway
	// . construct the NAT mapped port
	// . you should have used iptables to map port to the correct
	//   internal ip:port
	//unsigned long  ip   =g_conf.m_mainExternalIp  ; // h->m_externalIp;
	//unsigned short port=g_conf.m_mainExternalPort;//h->m_externalHttpPort
	// local check
	//if ( st->m_isLocal ) {
	unsigned long  ip   = h->m_ip;
	unsigned short port = h->m_httpPort;
	//}
	//sprintf ( x , "http://%s:%li/get?q=" , iptoa ( ip ) , port );
	// . we no longer put the port in here
	// . but still need http:// since we use <base href=>
	if (port == 80) sprintf(x,"http://%s/get?q=",iptoa(ip));
	else            sprintf(x,"http://%s:%hu/get?q=",iptoa(ip),port);
	x += gbstrlen ( x );
	// the query url encoded
	long elen = urlEncode ( x , thisUrlEnd - x , q , qlen );
	x += elen;
	// separate cgi vars with a &
	//sprintf ( x, "&seq=%li&rtq=%lid=%lli",
	//	  (long)st->m_seq,(long)st->m_rtq,st->m_msg22.getDocId());
	sprintf ( x, "&d=%lli",st->m_docId );
	x += gbstrlen(p);		
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
	//for ( long i = 0 ; i < qq.m_numWords ; i++ )
	//	if ( ! m.matchWord ( &qq.m_qwords[i],i ) ) ss.m_scores[i] = 0;
	// now set m.m_matches[] to those words in qw that match a query word
	// or phrase in qq.
	m.setQuery ( &qq );
	//m.addMatches ( &qw , &ss , true );
	m.addMatches ( &qw );
	long hilen = 0;
	// CNS: if ( ! st->m_clickNScroll ) {
	// and highlight the matches
	if ( st->m_printDisclaimer ) {
		hilen = hi.set ( p       ,
				 avail   ,
				 &qw     , // words to highlight
				 &m      , // matches relative to qw
				 false   , // doSteming
				 false   , // st->m_clickAndScroll , 
				 (char *)thisUrl );// base url for ClcknScrll
		p += hilen;
		// now an hr
		memcpy ( p , "</span></table></table>\n" , 24 );   p += 24;
	}

	//mfree(uq, uqCapacity, "PageGet");
	// undo the header writes if we should
	if ( ! st->m_includeHeader ) {
		// including base href is off by default when not including
		// the header, so the caller must explicitly turn it back on
		if ( st->m_includeBaseHref ) p = start2;
		else                         p = start1;
	}

	// identify start of <title> tag we wrote out
	char *sbstart = sb.getBufStart();
	char *sbend   = sb.getBufEnd();
	char *titleStart = NULL;
	char *titleEnd   = NULL;
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

		p += sprintf ( p , 
			       "<table border=1 "
			       "cellpadding=10 "
			       "cellspacing=0 "
			       "width=100%% "
			       "color=#ffffff>" );

		long printLinks = st->m_r.getLong("links",0);

		if ( ! st->m_printDisclaimer && printLinks )
			p += sprintf ( p , 
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
				       "c=%s&d=%lli&qh=0&cnsp=1&eb=%s\">"
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
			p += sprintf ( p ,
				       "<tr><td bgcolor=pink>"
				       "<span style=\"font-size:18px;"
				       "font-weight:600;"
				       "color:#000000;\">"
				       "&nbsp; "
				       "<b>PAGE TITLE:</b> "
				       );
			long tlen = titleEnd - titleStart;
			memcpy ( p , titleStart , tlen ); p += tlen;
			p += sprintf ( p , "</span></td></tr>" );
		}

		p += sprintf ( p , "</table><br>\n" );

	}

	// is the content preformatted?
	bool pre = false;
	char ctype = (char)xd->m_contentType;
	if ( ctype == CT_TEXT ) pre = true ; // text/plain
	if ( ctype == CT_DOC  ) pre = true ; // filtered msword
	if ( ctype == CT_PS   ) pre = true ; // filtered postscript
	// if it is content-type text, add a <pre>
	if ( p + 5 < bufEnd && pre ) {
		memcpy ( p , "<pre>" , 5 );
		p += 5;
	}

	if ( st->m_strip == 1 )
		contentLen = stripHtml( content, contentLen, 
					(long)xd->m_version, st->m_strip );
	// it returns -1 and sets g_errno on error, line OOM
	if ( contentLen == -1 ) {
		if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );	
		return sendErrorReply ( st , g_errno );
	}

	Xml xml;
	Words ww;

	// if no highlighting, skip it
	bool queryHighlighting = st->m_queryHighlighting;
	if ( st->m_strip == 2 ) queryHighlighting = false;
	if ( ! queryHighlighting ) {
		memcpy ( p , content , contentLen );
		p += contentLen ;
	}
	else {
		// get the content as xhtml (should be NULL terminated)
		//Words *ww = xd->getWords();
		if ( ! xml.set ( content , contentLen , false ,
				 0 , false , TITLEREC_CURRENT_VERSION ,
				 false , 0 ) ) { // niceness is 0
			if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );
			return sendErrorReply ( st , g_errno );
		}			
		if ( ! ww.set ( &xml , true , 0 ) ) { // niceness is 0
			if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );
			return sendErrorReply ( st , g_errno );
		}
		// sanity check
		//if ( ! xd->m_wordsValid ) { char *xx=NULL;*xx=0; }
		// how much space left in p?
		avail = bufEnd - p;

		Matches m;
		m.setQuery ( &qq );
		m.addMatches ( &ww );
		hilen = hi.set ( p , avail , &ww , &m ,
				 false /*doStemming?*/ ,  
				 st->m_clickAndScroll , 
				 thisUrl /*base url for click & scroll*/);
		p += hilen;
		log(LOG_DEBUG, "query: Done highlighting cached page content");
	}

	// if it is content-type text, add a </pre>
	if ( p + 6 < bufEnd && pre ) {
		memcpy ( p , "</pre>" , 6 );
		p += 6;
	}

	// calculate bufLen
	long bufLen = p - buf;

	long ct = xd->m_contentType;

	// now filter the entire buffer to escape out the xml tags
	// so it is displayed nice
	SafeBuf newbuf;

	if ( ct == CT_XML ) {
		// encode the xml tags into &lt;tagname&gt; sequences
		if ( !newbuf.htmlEncodeXmlTags ( buf , p-buf,0)){// niceness=0
			if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );
			return sendErrorReply ( st , g_errno );
		}
		// free out buffer that we alloc'd before returning since this
		// should have copied it into another buffer
		if ( buf ) mfree ( buf , bufMaxSize , "PageGet2" );	
		// reassign
		buf    = newbuf.getBufStart();
		bufLen = newbuf.length();
	}

	// now encapsulate it in html head/tail and send it off
	// sendErr:
	char *contentType = "text/html";
	if ( strip == 2 ) contentType = "text/xml";
	// xml is usually buggy and this throws browser off
	//if ( ctype == CT_XML ) contentType = "text/xml";

	// nuke state2
	mdelete ( st , sizeof(State2) , "PageGet1" );
	delete (st);

	bool status = g_httpServer.sendDynamicPage (s,buf,bufLen,-1,false,
						    contentType,
						     -1, NULL, "utf8" );
	// free out buffer that we alloc'd before returning since this
	// should have copied it into another buffer

	if      ( ct == CT_XML ) newbuf.purge();
	else if ( buf          ) mfree ( buf , bufMaxSize , "PageGet2" );
	
	// and convey the status
	return status;
}
