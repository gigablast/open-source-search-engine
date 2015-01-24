#include "gb-include.h"

#include "PageParser.h"
//#include "IndexTable.h"
//#include "IndexTable2.h"
//#include "XmlDoc.h" // addCheckboxSpan()

bool g_inPageParser = false;
bool g_inPageInject = false;

// TODO: meta redirect tag to host if hostId not ours
static bool processLoop ( void *state ) ;
static bool gotXmlDoc ( void *state ) ;
static bool sendErrorReply ( void *state , int32_t err ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
// . TODO: don't close this socket until httpserver returns!!
bool sendPageParser ( TcpSocket *s , HttpRequest *r ) {
	return sendPageParser2 ( s , r , NULL , -1LL , NULL , NULL, 
				 NULL , NULL, NULL , NULL );
}

// . a new interface so Msg3b can call this with "s" set to NULL
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageParser2 ( TcpSocket   *s , 
		       HttpRequest *r ,
		       State8      *st ,
		       int64_t    docId ,
		       Query       *q ,
		       // in query term space, not imap space
		       int64_t   *termFreqs       ,
		       // in imap space
		       float       *termFreqWeights ,
		       // in imap space
		       float       *affWeights      ,
		       void        *state ,
		       void       (* callback)(void *state) ) {

	//log("parser: read sock=%"INT32"",s->m_sd);

	// might a simple request to addsomething to validated.*.txt file
	// from XmlDoc::print() or XmlDoc::validateOutput()
	char *add = r->getString("add",NULL);
	//int64_t uh64 = r->getLongLong("uh64",0LL);
	char *uh64str = r->getString("uh64",NULL);
	//char *divTag = r->getString("div",NULL);
	if ( uh64str ) {
		// convert add to number
		int32_t addNum = 0;
		if ( to_lower_a(add[0])=='t' ) // "true" or "false"?
			addNum = 1;
		// convert it. skip beginning "str" inserted to prevent
		// javascript from messing with the int64_t since it
		// was rounding it!
		//int64_t uh64 = atoll(uh64str);//+3);
		// urldecode that
		//int32_t divTagLen = gbstrlen(divTag);
		//int32_t newLen  = urlDecode ( divTag , divTag , divTagLen );
		// null term?
		//divTag[newLen] = '\0';
		// do it. this is defined in XmlDoc.cpp
		//addCheckboxSpan ( uh64 , divTag , addNum );
		// make basic reply
		char *reply;
		reply = "HTTP/1.0 200 OK\r\n"
			"Connection: Close\r\n";
		// that is it! send a basic reply ok
		bool status = g_httpServer.sendDynamicPage( s , 
							    reply,
							    gbstrlen(reply),
							    -1, //cachtime
							    false ,//postreply?
							    NULL, //ctype
							    -1 , //httpstatus
							    NULL,//cookie
							    "utf-8");
		return status;
	}

	// make a state
	if (   st ) st->m_freeIt = false;
	if ( ! st ) {
		try { st = new (State8); }
		catch ( ... ) {
			g_errno = ENOMEM;
			log("PageParser: new(%i): %s", 
			    (int)sizeof(State8),mstrerror(g_errno));
			return g_httpServer.sendErrorReply(s,500,
						       mstrerror(g_errno));}
		mnew ( st , sizeof(State8) , "PageParser" );
		st->m_freeIt = true;
	}
	// msg3b uses this to get a score from the query
	st->m_state           = state;
	st->m_callback        = callback;
	st->m_q               = q;
	st->m_termFreqs       = termFreqs;
	st->m_termFreqWeights = termFreqWeights;
	st->m_affWeights      = affWeights;
	//st->m_total           = (score_t)-1;
	st->m_indexCode       = 0;
	st->m_blocked         = false;
	st->m_didRootDom      = false;
	st->m_didRootWWW      = false;
	st->m_wasRootDom      = false;
	st->m_u               = NULL;
	st->m_recompute       = false;
	//st->m_url.reset();

	// do not allow more than one to be launched at a time if in 
	// a quickpoll. will cause quickpoll in quickpoll.
	g_inPageParser = true;

	// password, too
	int32_t pwdLen = 0;
	char *pwd = r->getString ( "pwd" , &pwdLen );
	if ( pwdLen > 31 ) pwdLen = 31;
	if ( pwdLen > 0 ) strncpy ( st->m_pwd , pwd , pwdLen );
	st->m_pwd[pwdLen]='\0';

	// save socket ptr
	st->m_s = s;
	st->m_r.copy ( r );
	// get the collection
	char *coll    = r->getString ( "c" , &st->m_collLen ,NULL /*default*/);
	if ( st->m_collLen > MAX_COLL_LEN )
		return sendErrorReply ( st , ENOBUFS );
	if ( ! coll )
		return sendErrorReply ( st , ENOCOLLREC );
	strcpy ( st->m_coll , coll );

	// version to use, if -1 use latest
	st->m_titleRecVersion = r->getLong("version",-1);
	if ( st->m_titleRecVersion == -1 ) 
		st->m_titleRecVersion = TITLEREC_CURRENT_VERSION;
	// default to 0 if not provided
	st->m_hopCount = r->getLong("hc",0);
	//int32_t  ulen    = 0;
	//char *u     = r->getString ( "u" , &ulen     , NULL /*default*/);
	int32_t  old     = r->getLong   ( "old", 0 );
	// set query
	int32_t qlen;
	char *qs = r->getString("q",&qlen,NULL);
	if ( qs ) st->m_tq.set2 ( qs , langUnknown , true );
	// url will override docid if given
	if ( ! st->m_u || ! st->m_u[0] ) 
		st->m_docId = r->getLongLong ("docid",-1);
	else    
		st->m_docId = -1;
	// set url in state class (may have length 0)
	//if ( u ) st->m_url.set ( u , ulen );
	//st->m_urlLen = ulen;
	st->m_u = st->m_r.getString("u",&st->m_ulen,NULL);
	// should we recycle link info?
	st->m_recycle  = r->getLong("recycle",0);
	st->m_recycle2 = r->getLong("recycleimp",0);
	st->m_render   = r->getLong("render" ,0);
	// for quality computation... takes way longer cuz we have to 
	// lookup the IP address of every outlink, so we can get its root
	// quality using Msg25 which needs to filter out voters from that IP
	// range.
	st->m_oips     = r->getLong("oips"    ,0);

	int32_t  linkInfoLen  = 0;
	// default is NULL
	char *linkInfoColl = r->getString ( "oli" , &linkInfoLen, NULL );
	if ( linkInfoColl ) strcpy ( st->m_linkInfoColl , linkInfoColl );
	else st->m_linkInfoColl[0] = '\0';
	
	// set the flag in our SafeBuf class so that Words.cpp knows to show 
	// html or html source depending on this value
	st->m_xbuf.m_renderHtml = st->m_render;

	// should we use the old title rec?
	st->m_old    = old;
	// are we coming from a local machine?
	st->m_isLocal = r->isLocal();
 	//no more setting the default root quality to 30, instead if we do not
 	// know it setting it to -1
 	st->m_rootQuality=-1;







	// header
	SafeBuf *xbuf = &st->m_xbuf;
	xbuf->safePrintf("<meta http-equiv=\"Content-Type\" "
			 "content=\"text/html; charset=utf-8\">\n");

	// print standard header
	g_pages.printAdminTop ( xbuf , st->m_s , &st->m_r );


	// print the standard header for admin pages
	char *dd     = "";
	char *rr     = "";
	char *rr2    = "";
	char *render = "";
	char *oips   = "";
	char *us     = "";
	if ( st->m_u && st->m_u[0] ) us = st->m_u;
	//if ( st->m_sfn != -1 ) sprintf ( rtu , "%"INT32"",st->m_sfn );
	if ( st->m_old ) dd = " checked";
	if ( st->m_recycle            ) rr     = " checked";
	if ( st->m_recycle2           ) rr2    = " checked";
	if ( st->m_render             ) render = " checked";
	if ( st->m_oips               ) oips   = " checked";

	xbuf->safePrintf(
			 "<style>"
			 ".poo { background-color:#%s;}\n"
			 "</style>\n" ,
			 LIGHT_BLUE );


	int32_t clen;
	char *contentParm = r->getString("content",&clen,"");
	
	// print the input form
	xbuf->safePrintf (
		       "<style>\n"
		       "h2{font-size: 12px; color: #666666;}\n"

		       ".gbtag { border: 1px solid gray;"
		  	"background: #ffffef;display:inline;}\n"
		  	".gbcomment { border: 1px solid gray;"
		  	"color: #888888; font-style:italic; "
		  	"background: #ffffef;display:inline;}\n"

		       ".token { border: 1px solid gray;"
		       "background: #f0ffff;display:inline;}\n"
		       ".spam { border: 1px solid gray;"
		       "background: #af0000;"
		       "color: #ffffa0;}"
		       ".hs {color: #009900;}"
		       "</style>\n"
		       "<center>"

		  "<table %s>"

			  "<tr><td colspan=5><center><b>"
			  "Parser"
			  "</b></center></td></tr>\n"

		  "<tr class=poo>"
		  "<td>"
		  "<b>url</b>"
			  "<br><font size=-2>"
			  "Type in <b>FULL</b> url to parse."
			  "</font>"
		  "</td>"

		  "</td>"
		  "<td>"
		  "<input type=text name=u value=\"%s\" size=\"40\">\n"
		  "</td>"
		  "</tr>"


			  /*
		  "<tr class=poo>"
		  "<td>"
		  "Parser version to use: "
		  "</td>"
		  "<td>"
		  "<input type=text name=\"version\" size=\"4\" value=\"-1\"> "
		  "</td>"
		  "<td>"
		  "(-1 means to use latest title rec version)<br>"
		  "</td>"
		  "</tr>"
			  */

			  /*
		  "<tr class=poo>"
		  "<td>"
		  "Hop count to use: "
		  "</td>"
		  "<td>"
		  "<input type=text name=\"hc\" size=\"4\" value=\"%"INT32"\"> "
		  "</td>"
		  "<td>"
		  "(-1 is unknown. For root urls hopcount is always 0)<br>"
		  "</td>"
		  "</tr>"
			  */

		  "<tr class=poo>"
		  "<td>"
			  "<b>use cached</b>"

			  "<br><font size=-2>"
			  "Load page from cache (titledb)?"
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=checkbox name=old value=1%s> "
		  "</td>"
		  "</tr>"

			  /*
		  "<tr class=poo>"
		  "<td>"
		  "Reparse root:"
		  "</td>"
		  "<td>"
		  "<input type=checkbox name=artr value=1%s> "
		  "</td>"
		  "<td>"
		  "Apply selected ruleset to root to update quality"
		  "</td>"
		  "</tr>"
			  */

		  "<tr class=poo>"
		  "<td>"
		  "<b>recycle link info</b>"

			  "<br><font size=-2>"
			  "Recycle the link info from the title rec"
			  "Load page from cache (titledb)?"
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=checkbox name=recycle value=1%s> "
		  "</td>"
		  "</tr>"

			  /*
		  "<tr class=poo>"
		  "<td>"
		  "Recycle Link Info Imported:"
		  "</td>"
		  "<td>"
		  "<input type=checkbox name=recycleimp value=1%s> "
		  "</td>"
		  "<td>"
		  "Recycle the link info imported from other coll"
		  "</td>"
		  "</tr>"
			  */

		  "<tr class=poo>"
		  "<td>"
		  "<b>render html</b>"

			  "<br><font size=-2>"
			  "Render document content as HTML"
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=checkbox name=render value=1%s> "
		  "</td>"
		  "</tr>"

			  /*
		  "<tr class=poo>"
		  "<td>"
		  "Lookup outlinks' ruleset, ips, quality:"
		  "</td>"
		  "<td>"
		  "<input type=checkbox name=oips value=1%s> "
		  "</td>"
		  "<td>"
		  "To compute quality lookup IP addresses of roots "
		  "of outlinks."
		  "</td>"
		  "</tr>"

		  "<tr class=poo>"
		  "<td>"
		  "LinkInfo Coll:"
		  "</td>"
		  "<td>"
		  "<input type=text name=\"oli\" size=\"10\" value=\"\"> "
		  "</td>"
		  "<td>"
		  "Leave empty usually. Uses this coll to lookup link info."
		  "</td>"
		  "</tr>"
			  */

		  "<tr class=poo>"
		  "<td>"
		  "<b>optional query</b>"

			  "<br><font size=-2>"
			  "Leave empty usually. For title generation only."
			  "</font>"

		  "</td>"
		  "<td>"
		  "<input type=text name=\"q\" size=\"20\" value=\"\"> "
		  "</td>"
		       "</tr>",

			  TABLE_STYLE,
			  us ,
			   dd,
			   rr, 
			   render
			  );

	xbuf->safePrintf(
		  "<tr class=poo>"
			  "<td>"
			  "<b>content type below is</b>"
			  "<br><font size=-2>"
			  "Is the content below HTML? XML? JSON?"
			  "</font>"
			  "</td>"

		  "<td>"
		       //"<input type=checkbox name=xml value=1> "
		       "<select name=ctype>\n"
		       "<option value=%"INT32" selected>HTML</option>\n"
		       "<option value=%"INT32">XML</option>\n"
		       "<option value=%"INT32">JSON</option>\n"
		       "</select>\n"

		  "</td>"
		       "</tr>",
		       (int32_t)CT_HTML,
		       (int32_t)CT_XML,
		       (int32_t)CT_JSON
			  );

	xbuf->safePrintf(

			  "<tr class=poo>"
			  "<td><b>content</b>"
			  "<br><font size=-2>"
			  "Use this content for the provided <i>url</i> "
			  "rather than downloading it from the web."
			  "</td>"

			  "<td>"
			  "<textarea rows=10 cols=80 name=content>"
			  "%s"
			  "</textarea>"
			  "</td>"
			  "</tr>"

		  "</table>"
		  "</center>"
		  "</form>"
		  "<br>",

			  //oips ,
			  contentParm );



	xbuf->safePrintf(
			 "<center>"
			 "<input type=submit value=Submit>"
			 "</center>"
			 );


	// just print the page if no url given
	if ( ! st->m_u || ! st->m_u[0] ) return processLoop ( st );


	XmlDoc *xd = &st->m_xd;
	// set this up
	SpiderRequest sreq;
	sreq.reset();
	strcpy(sreq.m_url,st->m_u);
	int32_t firstIp = hash32n(st->m_u);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	// parentdocid of 0
	sreq.setKey( firstIp, 0LL, false );
	sreq.m_isPageParser = 1;
	sreq.m_hopCount = st->m_hopCount;
	sreq.m_hopCountValid = 1;
	sreq.m_fakeFirstIp   = 1;
	sreq.m_firstIp = firstIp;
	Url nu;
	nu.set(sreq.m_url);
	sreq.m_domHash32 = nu.getDomainHash32();
	sreq.m_siteHash32 = nu.getHostHash32();

	// . get provided content if any
	// . will be NULL if none provided
	// . "content" may contain a MIME
	int32_t  contentLen = 0;
	char *content = r->getString ( "content" , &contentLen , NULL );
	// is the "content" url-encoded? default is true.
	bool contentIsEncoded = true;
	// mark doesn't like to url-encode his content
	if ( ! content ) {
		content    = r->getUnencodedContent    ();
		contentLen = r->getUnencodedContentLen ();
		contentIsEncoded = false;
	}
	// ensure null
	if ( contentLen == 0 ) content = NULL;

	uint8_t contentType = CT_HTML;
	if ( r->getBool("xml",0) ) contentType = CT_XML;

	contentType = r->getLong("ctype",contentType);//CT_HTML);


	// if facebook, load xml content from title rec...
	bool isFacebook = (bool)strstr(st->m_u,"http://www.facebook.com/");
	if ( isFacebook && ! content ) {
		int64_t docId = g_titledb.getProbableDocId(st->m_u);
		sprintf(sreq.m_url ,"%"UINT64"", docId );
		sreq.m_isPageReindex = true;
	}

	// hack
	if ( content ) {
		st->m_dbuf.purge();
		st->m_dbuf.safeStrcpy(content);
		//char *data = strstr(content,"\r\n\r\n");
		//int32_t dataPos = 0;
		//if ( data ) dataPos = (data + 4) - content;
		//st->m_dbuf.convertJSONtoXML(0,dataPos);
		//st->m_dbuf.decodeJSON(0);
		content = st->m_dbuf.getBufStart();
	}

	// . use the enormous power of our new XmlDoc class
	// . this returns false if blocked
	if ( ! xd->set4 ( &sreq       ,
			  NULL        ,
			  st->m_coll  ,
			  &st->m_wbuf        ,
			  0 ,//PP_NICENESS ))
			  content ,
			  false, // deletefromindex
			  0, // forced ip
			  contentType ))
		// return error reply if g_errno is set
		return sendErrorReply ( st , g_errno );
	// make this our callback in case something blocks
	xd->setCallback ( st , processLoop );
	// . set xd from the old title rec if recycle is true
	// . can also use XmlDoc::m_loadFromOldTitleRec flag
	if ( st->m_recycle ) xd->m_recycleContent = true;

	return processLoop ( st );
}

bool processLoop ( void *state ) {
	// cast it
	State8 *st = (State8 *)state;
	// get the xmldoc
	XmlDoc *xd = &st->m_xd;

	// error?
	if ( g_errno ) return sendErrorReply ( st , g_errno );

	// int16_tcut
	SafeBuf *xbuf = &st->m_xbuf;

	if ( st->m_u && st->m_u[0] ) {
		// . save the ips.txt file if we are the test coll
		// . saveTestBuf() is a function in Msge1.cpp
		CollectionRec *cr = xd->getCollRec();
		if ( xd && cr && cr->m_coll && !strcmp(cr->m_coll,"qatest123"))
			// use same dir that XmlDoc::getTestDir() would use
			//saveTestBuf ( "test-page-parser" );
			saveTestBuf("qa");
		// now get the meta list, in the process it will print out a 
		// bunch of junk into st->m_xbuf
		char *metalist = xd->getMetaList ( );
		if ( ! metalist ) return sendErrorReply ( st , g_errno );
		// return false if it blocked
		if ( metalist == (void *)-1 ) return false;
		// for debug...
		if ( ! xd->m_indexCode ) xd->doConsistencyTest ( false );
		// print it out
		xd->printDoc( xbuf );
	}

	// print reason we can't analyze it (or index it)
	//if ( st->m_indexCode != 0 ) {
	//	xbuf->safePrintf ("<br><br><b>indexCode: %s</b>\n<br>", 
	//			  mstrerror(st->m_indexCode));
	//}

	// we are done
	g_inPageParser = false;

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );

	//log("parser: send sock=%"INT32"",st->m_s->m_sd);
	
	// now encapsulate it in html head/tail and send it off
	bool status = g_httpServer.sendDynamicPage( st->m_s , 
						    xbuf->getBufStart(), 
						    xbuf->length() ,
						    -1, //cachtime
						    false ,//postreply?
						    NULL, //ctype
						    -1 , //httpstatus
						    NULL,//cookie
						    "utf-8");
	// delete the state now
	if ( st->m_freeIt ) {
		mdelete ( st , sizeof(State8) , "PageParser" );
		delete (st);
	}
	// return the status
	return status;
}




// returns true
bool sendErrorReply ( void *state , int32_t err ) {
	// ensure this is set
	if ( ! err ) { char *xx=NULL;*xx=0; }
	// get it
	State8 *st = (State8 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_s;

	char tmp [ 1024*32 ] ;
	sprintf ( tmp , "<b>had server-side error: %s</b><br>",
		  mstrerror(g_errno));
	// nuke state8
	mdelete ( st , sizeof(State8) , "PageGet1" );
	delete (st);
	// erase g_errno for sending
	//g_errno = 0;
	// . now encapsulate it in html head/tail and send it off
	//return g_httpServer.sendDynamicPage ( s , tmp , gbstrlen(tmp) );
	return g_httpServer.sendErrorReply ( s, err, mstrerror(err) );
}

// for procog
bool sendPageAnalyze ( TcpSocket *s , HttpRequest *r ) {

	// make a state
	State8 *st;
	try { st = new (State8); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageParser: new(%i): %s", 
		    (int)sizeof(State8),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,
						   mstrerror(g_errno));}
	mnew ( st , sizeof(State8) , "PageParser" );
	st->m_freeIt = true;
	st->m_state           = NULL;
	//st->m_callback        = callback;
	//st->m_q               = q;
	//st->m_termFreqs       = termFreqs;
	//st->m_termFreqWeights = termFreqWeights;
	//st->m_affWeights      = affWeights;
	//st->m_total           = (score_t)-1;
	st->m_indexCode       = 0;
	st->m_blocked         = false;
	st->m_didRootDom      = false;
	st->m_didRootWWW      = false;
	st->m_wasRootDom      = false;
	st->m_u               = NULL;

	// password, too
	int32_t pwdLen = 0;
	char *pwd = r->getString ( "pwd" , &pwdLen );
	if ( pwdLen > 31 ) pwdLen = 31;
	if ( pwdLen > 0 ) strncpy ( st->m_pwd , pwd , pwdLen );
	st->m_pwd[pwdLen]='\0';

	// save socket ptr
	st->m_s = s;
	st->m_r.copy ( r );

	// get the collection
	char *coll    = r->getString ( "c" , &st->m_collLen ,NULL /*default*/);
	if ( ! coll ) coll = g_conf.m_defaultColl;
	if ( ! coll ) coll = "main";
	int32_t collLen = gbstrlen(coll);
	if ( collLen > MAX_COLL_LEN ) return sendErrorReply ( st , ENOBUFS );
	strcpy ( st->m_coll , coll );

	// version to use, if -1 use latest
	st->m_titleRecVersion = r->getLong("version",-1);
	if ( st->m_titleRecVersion == -1 ) 
		st->m_titleRecVersion = TITLEREC_CURRENT_VERSION;
	// default to 0 if not provided
	st->m_hopCount = r->getLong("hc",0);
	int32_t  old     = r->getLong   ( "old", 0 );
	// set query
	int32_t qlen;
	char *qs = r->getString("q",&qlen,NULL);
	if ( qs ) st->m_tq.set2 ( qs , langUnknown , true );
	// url will override docid if given
	st->m_docId = r->getLongLong ("d",-1);
	st->m_docId = r->getLongLong ("docid",st->m_docId);

	int32_t ulen;
	char *u = st->m_r.getString("u",&ulen,NULL);
	if ( ! u ) u = st->m_r.getString("url",&ulen,NULL);
	if ( ! u && st->m_docId == -1LL ) 
		return sendErrorReply ( st , EBADREQUEST );

	// set url in state class (may have length 0)
	//if ( u ) st->m_url.set ( u , ulen );
	//st->m_urlLen = ulen;
	st->m_u = u;
	st->m_ulen = 0;
	if ( u ) st->m_ulen = gbstrlen(u);
	// should we recycle link info?
	st->m_recycle  = r->getLong("recycle",1);
	st->m_recycle2 = r->getLong("recycleimp",0);
	st->m_render   = r->getLong("render" ,0);
	st->m_recompute = r->getLong("recompute" ,0);
	// for quality computation... takes way longer cuz we have to 
	// lookup the IP address of every outlink, so we can get its root
	// quality using Msg25 which needs to filter out voters from that IP
	// range.
	st->m_oips     = r->getLong("oips"    ,0);
	//st->m_page = r->getLong("page",1);

	int32_t  linkInfoLen  = 0;
	// default is NULL
	char *linkInfoColl = r->getString ( "oli" , &linkInfoLen, NULL );
	if ( linkInfoColl ) strcpy ( st->m_linkInfoColl , linkInfoColl );
	else st->m_linkInfoColl[0] = '\0';
	
	// set the flag in our SafeBuf class so that Words.cpp knows to show 
	// html or html source depending on this value
	//st->m_xbuf.m_renderHtml = st->m_render;

	// should we use the old title rec?
	st->m_old    = old;
	// are we coming from a local machine?
	st->m_isLocal = r->isLocal();
 	//no more setting the default root quality to 30, instead if we do not
 	// know it setting it to -1
 	st->m_rootQuality=-1;

	// header
	//xbuf->safePrintf("<meta http-equiv=\"Content-Type\" "
	//		 "content=\"text/html; charset=utf-8\">\n");

	XmlDoc *xd = &st->m_xd;

	int32_t isXml = r->getLong("xml",0);

	// if got docid, use that
	if ( st->m_docId != -1 ) {
		if ( ! xd->set3 ( st->m_docId,
				  st->m_coll,
				  0 ) ) // niceness
			// return error reply if g_errno is set
			return sendErrorReply ( st , g_errno );
		// make this our callback in case something blocks
		xd->setCallback ( st , gotXmlDoc );
		xd->m_pbuf = &st->m_wbuf;
		// reset this flag
		st->m_donePrinting = false;
		// . set xd from the old title rec if recycle is true
		// . can also use XmlDoc::m_loadFromOldTitleRec flag
		//if ( st->m_recycle ) xd->m_recycleContent = true;
		xd->m_recycleContent = true;
		// force this on
		//xd->m_useSiteLinkBuf = true;
		//xd->m_usePageLinkBuf = true;
		if ( isXml ) xd->m_printInXml = true;
		// now tell it to fetch the old title rec
		if ( ! xd->loadFromOldTitleRec () )
			// return false if this blocks
			return false;
		return gotXmlDoc ( st );
	}			

	// set this up
	SpiderRequest sreq;
	sreq.reset();
	if ( st->m_u ) strcpy(sreq.m_url,st->m_u);
	int32_t firstIp = hash32n(st->m_u);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	// parentdocid of 0
	sreq.setKey( firstIp, 0LL, false );
	sreq.m_isPageParser = 1;
	sreq.m_hopCount = st->m_hopCount;
	sreq.m_hopCountValid = 1;
	sreq.m_fakeFirstIp   = 1;
	sreq.m_firstIp = firstIp;
	Url nu;
	nu.set(sreq.m_url);
	sreq.m_domHash32 = nu.getDomainHash32();
	sreq.m_siteHash32 = nu.getHostHash32();

	// . get provided content if any
	// . will be NULL if none provided
	// . "content" may contain a MIME
	int32_t  contentLen = 0;
	char *content = r->getString ( "content" , &contentLen , NULL );
	// is the "content" url-encoded? default is true.
	bool contentIsEncoded = true;
	// mark doesn't like to url-encode his content
	if ( ! content ) {
		content    = r->getUnencodedContent    ();
		contentLen = r->getUnencodedContentLen ();
		contentIsEncoded = false;
	}
	// ensure null
	if ( contentLen == 0 ) content = NULL;

	//uint8_t contentType = CT_HTML;
	//if ( isXml ) contentType = CT_XML;
	int32_t ctype = r->getLong("ctype",CT_HTML);

	// . use the enormous power of our new XmlDoc class
	// . this returns false if blocked
	if ( ! xd->set4 ( &sreq       ,
			  NULL        ,
			  st->m_coll  ,
			  // we need this so the term table is set!
			  &st->m_wbuf        , // XmlDoc::m_pbuf
			  0, // try 0 now! 1 ,//PP_NICENESS ))
			  content ,
			  false, // deletefromindex
			  0, // forced ip
			  ctype ))
		// return error reply if g_errno is set
		return sendErrorReply ( st , g_errno );
	// make this our callback in case something blocks
	xd->setCallback ( st , gotXmlDoc );
	// reset this flag
	st->m_donePrinting = false;
	// prevent a core here in the event we download the page content
	xd->m_crawlDelayValid = true;
	xd->m_crawlDelay = 0;
	// . set xd from the old title rec if recycle is true
	// . can also use XmlDoc::m_loadFromOldTitleRec flag
	//if ( st->m_recycle ) xd->m_recycleContent = true;
	// only recycle if docid is given!!
	if ( st->m_recycle ) xd->m_recycleContent = true;
	// force this on
	//xd->m_useSiteLinkBuf = true;
	//xd->m_usePageLinkBuf = true;
	if ( isXml ) xd->m_printInXml = true;

	return gotXmlDoc ( st );
}

bool gotXmlDoc ( void *state ) {
	// cast it
	State8 *st = (State8 *)state;
	// get the xmldoc
	XmlDoc *xd = &st->m_xd;

	// if we loaded from old title rec, it should be there!


	// . save the ips.txt file if we are the test coll
	// . saveTestBuf() is a function in Msge1.cpp
	//if ( xd && xd->m_coll && ! strcmp ( xd->m_coll , "qatest123")) 
	//	// use same dir that XmlDoc::getTestDir() would use
	//	saveTestBuf ( "test-page-parser" );

	// error?
	if ( g_errno ) return sendErrorReply ( st , g_errno );

	// int16_tcut
	SafeBuf *xbuf = &st->m_xbuf;

	bool printIt = false;
	if ( st->m_u && st->m_u[0] ) printIt = true;
	if ( st->m_docId != -1LL ) printIt = true;
	if ( st->m_donePrinting ) printIt = false;

	// do not re-call this if printDocForProCog blocked... (check length())
	if ( printIt ) {
		// mark as done
		st->m_donePrinting = true;
		// always re-compute the page inlinks dynamically, do not
		// use the ptr_linkInfo1 stored in titlerec!!
		// NO! not if set from titlerec/docid
		if ( st->m_recompute )
			xd->m_linkInfo1Valid = false;
		// try a recompute regardless, because we do not store the
		// bad inlinkers, and ppl want to see why they are bad!
		//xd->m_linkInfo1Valid = false;
		// now get the meta list, in the process it will print out a 
		// bunch of junk into st->m_xbuf
		//char *metalist = xd->getMetaList ( );
		//if ( ! metalist ) return sendErrorReply ( st , g_errno );
		// return false if it blocked
		//if ( metalist == (void *)-1 ) return false;
		// for debug...
		//if ( ! xd->m_indexCode ) xd->doConsistencyTest ( false );
		// . print it out
		// . returns false if blocks, true otherwise
		// . sets g_errno on error
		if ( ! xd->printDocForProCog ( xbuf , &st->m_r ) )
			return false;
		// error?
		if ( g_errno ) return sendErrorReply ( st , g_errno );
	}

	int32_t isXml = st->m_r.getLong("xml",0);
	char ctype2 = CT_HTML;
	if ( isXml ) ctype2 = CT_XML;
	// now encapsulate it in html head/tail and send it off
	bool status = g_httpServer.sendDynamicPage( st->m_s , 
						    xbuf->getBufStart(), 
						    xbuf->length() ,
						    -1, //cachtime
						    false ,//postreply?
						    &ctype2,
						    -1 , //httpstatus
						    NULL,//cookie
						    "utf-8");
	// delete the state now
	if ( st->m_freeIt ) {
		mdelete ( st , sizeof(State8) , "PageParser" );
		delete (st);
	}
	// return the status
	return status;
}
