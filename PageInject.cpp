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

// from XmlDoc.cpp
bool isRobotsTxtFile ( char *url , int32_t urlLen ) ;

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

	if ( ! g_conf.m_injectionsEnabled ) {
		g_errno = EINJECTIONSDISABLED;//BADENGINEER;
		log("inject: injection disabled");
		return g_httpServer.sendErrorReply(sock,500,"injection is "
						   "disabled by "
						   "the administrator in "
						   "the master "
						   "controls");
	}



	// get the collection
	// make a new state
	Msg7 *msg7;
	try { msg7= new (Msg7); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: new(%i): %s", 
		    (int)sizeof(Msg7),mstrerror(g_errno));
	       return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
	}
	mnew ( msg7, sizeof(Msg7) , "PageInject" );

	msg7->m_socket = sock;

	char format = hr->getReplyFormat();

	// no url parm?
	if ( format != FORMAT_HTML && ! hr->getString("c",NULL) ) {
		g_errno = ENOCOLLREC;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

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

	// set this to  false
	gr->m_gotSections = false;

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

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( sock , hr );
	bool isCollAdmin = g_conf.isCollAdmin ( sock , hr );
	if ( ! isMasterAdmin &&
	     ! isCollAdmin ) {
		g_errno = ENOPERM;
		return sendReply ( msg7 );
	}

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
	//int64_t docId  = msg7->m_msg7.m_docId;
	//int32_t      hostId = msg7->m_msg7.m_hostId;
	int64_t docId  = xd->m_docId;
	int32_t      hostId = 0;//msg7->m_msg7.m_hostId;

	// set g_errno to index code
	if ( xd->m_indexCodeValid && xd->m_indexCode && ! g_errno )
		g_errno = xd->m_indexCode;

	char format = gr->m_hr.getReplyFormat();

	// no url parm?
	if ( ! g_errno && ! gr->m_url && format != FORMAT_HTML )
		g_errno = EMISSINGINPUT;

	if ( g_errno && g_errno != EDOCUNCHANGED ) {
		int32_t save = g_errno;
		mdelete ( msg7, sizeof(Msg7) , "PageInject" );
		delete (msg7);
		g_errno = save;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,save,msg,NULL);
	}

	char abuf[320];
	SafeBuf am(abuf,320,0,false);
	am.setLabel("injbuf");
	char *ct = NULL;

	// a success reply, include docid and url i guess
	if ( format == FORMAT_XML ) {
		am.safePrintf("<response>\n");
		am.safePrintf("\t<statusCode>%"INT32"</statusCode>\n",
			      (int32_t)g_errno);
		am.safePrintf("\t<statusMsg><![CDATA[");
		am.cdataEncode(mstrerror(g_errno));
		am.safePrintf("]]></statusMsg>\n");
		am.safePrintf("\t<docId>%"INT64"</docId>\n",xd->m_docId);
		if ( gr->m_getSections ) {
			SafeBuf *secBuf = xd->getInlineSectionVotingBuf();
			am.safePrintf("\t<htmlSrc><![CDATA[");
			if ( secBuf->length() ) 
				am.cdataEncode(secBuf->getBufStart());
			am.safePrintf("]]></htmlSrc>\n");
		}
		am.safePrintf("</response>\n");
		ct = "text/xml";
	}

	if ( format == FORMAT_JSON ) {
		am.safePrintf("{\"response\":{\n");
		am.safePrintf("\t\"statusCode\":%"INT32",\n",(int32_t)g_errno);
		am.safePrintf("\t\"statusMsg\":\"");
		am.jsonEncode(mstrerror(g_errno));
		am.safePrintf("\",\n");
		am.safePrintf("\t\"docId\":%"INT64",\n",xd->m_docId);
		if ( gr->m_getSections ) {
			SafeBuf *secBuf = xd->getInlineSectionVotingBuf();
			am.safePrintf("\t\"htmlSrc\":\"");
			if ( secBuf->length() ) 
				am.jsonEncode(secBuf->getBufStart());
			am.safePrintf("\",\n");
		}
		// subtract ",\n"
		am.m_length -= 2;
		am.safePrintf("\n}\n}\n");
		ct = "application/json";
	}

	if ( format == FORMAT_XML || format == FORMAT_JSON ) {
		mdelete ( msg7, sizeof(Msg7) , "PageInject" );
		delete (msg7);
		return g_httpServer.sendDynamicPage(sock,
						    am.getBufStart(),
						    am.length(),
						    0,
						    false,
						    ct );
	}

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
		// return docid and hostid
		if ( ! g_errno ) p += sprintf ( p , 
					   "0,docId=%"INT64",hostId=%"INT32"," , 
					   docId , hostId );
		// print error number here
		else  p += sprintf ( p , "%"INT32",0,0,", (int32_t)g_errno );
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
				, gr->m_url
				//, xd->m_firstUrl.m_url
				);


	// print the table of injection parms
	g_parms.printParmTable ( &sb , sock , &gr->m_hr );


	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	// calculate buffer length
	//int32_t bufLen = p - buf;
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
	reset();
}

Msg7::~Msg7 () {
}

//void Msg7::constructor () {
//	reset();
//}

void Msg7::reset() { 
	m_round = 0;
	m_firstTime = true;
	m_fixMe = false;
	m_injectCount = 0;
	m_start = NULL;
	m_sbuf.reset();
	m_isWarc = false;
	m_isArc  = false;
	m_isDoneInjecting = false;
}

// when XmlDoc::inject() complets it calls this
//void doneInjectingWrapper9 ( void *state ) {
void injectLoopWrapper9 ( void *state ) {

	Msg7 *msg7 = (Msg7 *)state;

	msg7->m_inUse = false;
	
	XmlDoc *xd = &msg7->m_xd;

	GigablastRequest *gr = &msg7->m_gr;

	if ( gr->m_getSections && ! gr->m_gotSections ) {
		// do not re-call
		gr->m_gotSections = true;
		// new callback now, same state
		xd->m_callback1 = injectLoopWrapper9;
		// and if it blocks internally, it will call 
		// getInlineSectionVotingBuf until it completes then it will 
		// call xd->m_callback
		xd->m_masterLoop = NULL;
		// get sections
		SafeBuf *buf = xd->getInlineSectionVotingBuf();
		// if it returns -1 wait for it to call wrapper10 when done
		if ( buf == (void *)-1 ) return;
		// error?
		if ( ! buf ) log("inject: error getting sections: %s",
				 mstrerror(g_errno));
	}

 loop:

	// if we were injecting delimterized documents...
	char *delim = gr->m_contentDelim;
	if ( delim && ! delim[0] ) delim = NULL;
	bool loopIt = false;
	if ( delim ) loopIt = true;
	// by default warc and arc files consist of many subdocuments
	// that have to be indexed individually as well
	if ( msg7->m_isWarc ) loopIt = true;
	if ( msg7->m_isArc  ) loopIt = true;

	if ( loopIt ) { // && msg7->m_start ) {
		// do another injection. returns false if it blocks
		if ( ! msg7->inject ( msg7->m_state , msg7->m_callback ) )
			return;
	}

	//if ( msg7->m_start && delim ) 
	if ( ! msg7->m_isDoneInjecting )
		goto loop;

	// and we call the original caller
	msg7->m_callback ( msg7->m_state );
}

bool Msg7::inject ( char *coll ,
		    char *proxiedUrl ,
		    int32_t  proxiedUrlLen ,
		    char *content ,
		    void *state ,
		    void (*callback)(void *state) ) {

	GigablastRequest *gr = &m_gr;
	// reset THIS to defaults. use NULL for cr since mostly for SearchInput
	g_parms.setToDefault ( (char *)gr , OBJ_GBREQUEST , NULL);

	// copy into safebufs in case the underlying data gets deleted.
	gr->m_tmpBuf1.safeStrcpy ( coll );
	gr->m_coll = gr->m_tmpBuf1.getBufStart();
	
	// copy into safebufs in case the underlying data gets deleted.
	gr->m_tmpBuf2.safeMemcpy ( proxiedUrl , proxiedUrlLen );
	gr->m_tmpBuf2.nullTerm();

	gr->m_url = gr->m_tmpBuf2.getBufStart();

	// copy into safebufs in case the underlying data gets deleted.
	gr->m_tmpBuf3.safeStrcpy ( content );
	gr->m_content = gr->m_tmpBuf3.getBufStart();
	
	gr->m_hasMime = true;

	return inject ( state , callback );
}

// returns false if would block
// bool Msg7::injectTitleRec ( void *state ,
// 			    void (*callback)(void *state) ,
// 			    CollectionRec *cr ) {


static void sendReply ( UdpSlot *slot ) {

	if ( g_errno )
		g_udpServer.sendErrorReply(slot,g_errno);
	else
		g_udpServer.sendReply_ass(NULL,0,NULL,0,slot);

}

// when XmlDoc::inject() complets it calls this
void doneInjectingWrapper10 ( void *state ) {
	XmlDoc *xd = (XmlDoc *)state;
	UdpSlot *slot = (UdpSlot *)xd->m_slot;
	int32_t err = g_errno;
	mdelete ( xd, sizeof(XmlDoc) , "PageInject" );
	delete (xd);
	g_errno = err;
	sendReply ( slot );
}

void handleRequest7 ( UdpSlot *slot , int32_t netnice ) {

	//m_state = state;
	//m_callback = callback;

	XmlDoc *xd;
	try { xd = new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: import failed: new(%i): %s", 
		    (int)sizeof(XmlDoc),mstrerror(g_errno));
		sendReply(slot);
		return;
	}
	mnew ( xd, sizeof(XmlDoc) , "PageInject" );

	//xd->reset();
	char *titleRec = slot->m_readBuf;
	int32_t titleRecSize = slot->m_readBufSize;

	int32_t collnum = *(int32_t *)titleRec;

	titleRec += 4;
	titleRecSize -= 4;

	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	if ( ! cr ) {
		sendReply(slot);
		return;
	}

	// if injecting a titlerec from an import operation use set2()
	//if ( m_sbuf.length() > 0 ) {
	xd->set2 ( titleRec,//m_sbuf.getBufStart() ,
		   titleRecSize,//m_sbuf.length() ,
		   cr->m_coll ,
		   NULL, // pbuf
		   MAX_NICENESS ,
		   NULL ); // sreq
	// log it i guess
	log("inject: importing %s",xd->m_firstUrl.getUrl());
	// call this when done indexing
	//xd->m_masterState = this;
	//xd->m_masterLoop  = injectLoopWrapper9;
	xd->m_state = xd;//this;
	xd->m_callback1  = doneInjectingWrapper10;
	xd->m_isImporting = true;
	xd->m_isImportingValid = true;
	// hack this
	xd->m_slot = slot;
	// then index it
	if ( ! xd->indexDoc() )
		// return if would block
		return;

	// all done?
	//return true;
	sendReply ( slot );
}

void gotWarcContentWrapper ( void *state , TcpSocket *ts ) {
	Msg7 *THIS = (Msg7 *)state;
	// set content to that
	GigablastRequest *gr = &THIS->m_gr;
	gr->m_contentBuf.setBuf (ts->m_readBuf, 
				 ts->m_readBufSize ,
				 ts->m_readOffset ,
				 true , // ownBuf?
				 0 ); // encoding
	// just ref it
	gr->m_content = ts->m_readBuf;
	// so tcpserver.cpp doesn't free the ward/arc file
	ts->m_readBuf = NULL;
	// continue with injection loop
	injectLoopWrapper9 ( THIS );
}

// . returns false if blocked and callback will be called, true otherwise
// . sets g_errno on error
bool Msg7::inject ( void *state ,
		    void (*callback)(void *state) 
		    //int32_t spiderLinksDefault ,
		    //char *collOveride ) {
		    ) {

	GigablastRequest *gr = &m_gr;

	char *coll2 = gr->m_coll;
	CollectionRec *cr = g_collectiondb.getRec ( coll2 );

	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		return true;
	}

	m_state = state;
	m_callback = callback;

	// shortcut
	XmlDoc *xd = &m_xd;

	if ( ! gr->m_url &&
	     // if there is a record delimeter, we form a new fake url
	     // for each record based on content hash
	     ! gr->m_contentDelim ) {
		log("inject: no url provied to inject");
		g_errno = EBADURL;
		return true;
	}

	//char *coll = cr->m_coll;

	if ( g_repairMode ) { g_errno = EREPAIRING; return true; }

	// this will be NULL if the "content" was empty or not given
	char *content = gr->m_content;

	// . try the uploaded file if nothing in the text area
	// . this will be NULL if the "content" was empty or not given
	if ( ! content ) content = gr->m_contentFile;

	// if it is a warc or arc url then download it to fill in the content
	Url u;
	if ( gr->m_url )
		// get the normalized url
		u.set ( gr->m_url );

	char    *ustr = u.getUrl();
	int32_t  ulen = u.getUrlLen();
	char    *uend = ustr + ulen;

	m_isWarc = false;
	m_isArc  = false;

	if ( ulen>8 && strncmp(uend-8,".warc.gz",8)==0 )
		m_isWarc = true;
	if ( ulen>8 && strncmp(uend-5,".warc"   ,5)==0 )
		m_isWarc = true;

	if ( ulen>8 && strncmp(uend-7,".arc.gz",7)==0 )
		m_isArc = true;
	if ( ulen>8 && strncmp(uend-4,".arc"   ,4)==0 )
		m_isArc = true;

	// if warc/arc download it and make gr->m_content reference it...
	// we won't handle redirects though.
	if ( ! content && ( m_isWarc || m_isArc) ) {
		// download the warc/arc url
		if ( ! g_httpServer.getDoc ( ustr ,
					     0 , // urlip
					     0                    , // offset
					     -1                   ,
					     0,//r->m_ifModifiedSince ,
					     this                 , // state
					     gotWarcContentWrapper ,// callback
					     30*1000   , // 30 sec timeout
					     0 , // r->m_proxyIp     ,
					     0 , // r->m_proxyPort   ,
					     -1,//r->m_maxTextDocLen   ,
					     -1,//r->m_maxOtherDocLen  ,
					     NULL,//agent                ,
					     DEFAULT_HTTP_PROTO , // "HTTP/1.0"
					     false , // doPost?
					     NULL , // cookie
					     NULL , // additionalHeader
					     NULL , // our own mime!
					     NULL , // postContent
					     NULL))//proxyUsernamePwdAuth ) )
			// return false if blocked
			return false;
		// error?
		log("inject: %s",mstrerror(g_errno));
	}

	if ( m_firstTime && m_isWarc ) {
		// skip over the first http mime header, it is not
		// part of the warc file per se.
		content = strstr(content,"\r\n\r\n");
		if ( ! content ) {
			log("inject: no mime received from webserver");
			return true;
		}
		// skip over that to point to start of actual warc
		// file content
		content += 4;
	}
		
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
	// delim is sill for warc/arcs so ignore it
	if ( m_isWarc || m_isArc ) delim = NULL;


	// if doing delimeterized injects, hitting a \0 is the end of the road
	if ( delim && m_fixMe && ! m_saved ) {
		m_isDoneInjecting = true;
		return true;
	}


	if ( m_fixMe ) {
		// we had made the first delim char a \0 to index the
		// previous document, now put it back to what it was
		*m_start = m_saved;
		// i guess unset this
		m_fixMe = false;

	}

	bool advanced = false;

	// we've saved m_start as "start" above, 
	// so find the next delimeter after it and set that to m_start
	// add +1 to avoid infinite loop
	if ( delim ) { // gr->m_containerContentType == CT_UNKNOWN )
		m_start = strstr(start+1,delim);
		advanced = true;
		// if m_start is NULL, it couldn't be found so advance
		// to end of file which should be a \0 already
		if ( ! m_start ) 
			m_start = start + gbstrlen(start);
	}

	// WARC files are mime delimeted. the http reply, which 
	// contains a mime, as a mime a level above that whose 
	// content-length: field includes the original http reply mime
	// as part of its content.
	if ( m_isWarc ) { // gr->m_containerContentType == CT_WARC ) {
		// no setting delim for this!
		if ( delim ) { char *xx=NULL;*xx=0; }
		// should have the url as well
		char *mm = strstr(start,"Content-Length:");
		char *mmend = NULL;
		if ( mm ) mmend = strstr (mm,"\n");
		if ( ! mm || ! mmend ) {
			log("inject: warc: all done");
			// XmlDoc.cpp checks for this to stop calling us
			m_isDoneInjecting = true;
			return true;
		}
		char c = *mmend;
		*mmend = '\0';
		int64_t recordSize = atoll ( mm + 15 );
		*mmend = c;

		// end of mime header
		char *hend = strstr ( mm, "\r\n\r\n");
		if ( ! hend ) {
			log("inject: warc: could no mime header end.");
			return true;
		}

		// tmp \0 that for these strstr() calls
		c = *hend;
		*hend = '\0';

		char *warcUrl  = strstr(start,"WARC-Target-URI:");
		char *warcType = strstr(start,"WARC-Type:");
		char *warcDate = strstr(start,"WARC-Date:");
		char *warcIp   = strstr(start,"WARC-IP-Address:");

		// advance
		if ( warcUrl  ) warcUrl  += 16;
		if ( warcType ) warcType += 10;
		if ( warcDate ) warcDate += 10;
		if ( warcIp   ) warcIp   += 17;

		// restore
		*hend = c;

		// skip the \r\n\r\n
		hend += 4;

		// adjust start to point to start of the content really
		start = hend;

		// and over record 
		m_start = hend + recordSize;
		advanced = true;

		if ( ! warcType ) {
			log("inject: warc: could not find rec type");
			return true;
		}

		if ( is_wspace_a(*warcType) ) warcType++;
		if ( is_wspace_a(*warcType) ) warcType++;

		// WARC-Type:
		// do not index this record as a doc if it is not a
		// "WARC-Type: response" record.
		if ( strncmp(warcType,"response",8) != 0 ) 
			return true;

		// skip this rec if url-less
		if ( ! warcUrl ) {
			log("inject: warc: could not find rec url");
			return true;
		}
		if ( ! warcDate ) {
			log("inject: warc: could not find rec date");
			return true;
		}

		// skip spaces on all
		if ( warcUrl  && is_wspace_a(*warcUrl ) ) warcUrl++;
		if ( warcUrl  && is_wspace_a(*warcUrl ) ) warcUrl++;
		if ( warcDate && is_wspace_a(*warcDate) ) warcDate++;
		if ( warcDate && is_wspace_a(*warcDate) ) warcDate++;
		if ( warcIp   && is_wspace_a(*warcIp  ) ) warcIp++;
		if ( warcIp   && is_wspace_a(*warcIp  ) ) warcIp++;

		// url must start with http:// or https://
		// it's probably like WARC-Target-URI: dns:www.xyz.com
		// so it is a dns response
		if ( strncmp(warcUrl,"http://" ,7) != 0 &&
		     strncmp(warcUrl,"https://",8) != 0 ) 
			return true;

		gr->m_injectDocIp = 0;

		// get the record IP address from the warc header if there
		if ( warcIp ) {
			// get end of ip
			char *warcIpEnd = warcIp;
			// skip digits and periods
			while ( ! is_wspace_a(*warcIpEnd) ) warcIpEnd++;
			// we now have the ip address for doing ip: searches
			// this func is in ip.h
			gr->m_injectDocIp = atoip ( warcIp, warcIpEnd-warcIp );
		}
		
		// convert date to timestamp
		int64_t warcTime = 0;
		if ( warcDate ) warcTime = atotime ( warcDate );
		gr->m_firstIndexed = warcTime;
		gr->m_lastSpidered = warcTime;
		// does this work?
		gr->m_hopCount     = -1;
		gr->m_diffbotReply = 0;
		gr->m_newOnly      = 0;
		// end of the url
		char *warcUrlEnd = warcUrl;
		for ( ; *warcUrlEnd && ! is_wspace_a(*warcUrlEnd) ;
		      warcUrlEnd++ );
		// set it to that
		m_injectUrlBuf.reset();
		// by default append a -<ch64> to the provided url
		int32_t warcUrlLen = warcUrlEnd - warcUrl;
		m_injectUrlBuf.safeMemcpy(warcUrl,warcUrlLen);
		m_injectUrlBuf.nullTerm();
		// skip if robots.txt
		if ( isRobotsTxtFile(m_injectUrlBuf.getBufStart(),
				     m_injectUrlBuf.getLength() ) )
			return true;
		// all warc records have the http mime
		gr->m_hasMime = true;
		char *recMime = hend;
		// and find the next \r\n\r\n
		char *recMimeEnd = strstr ( recMime , "\r\n\r\n" );
		if ( ! recMimeEnd ) {
			log("inject: warc: no http mime.");
			return true;
		}
		// gotta include the \r\n\r\n in the mime length here
		recMimeEnd += 4;
		// should be a mime that starts with GET or POST
		HttpMime mime;
		if ( ! mime.set ( recMime, recMimeEnd - recMime , NULL ) ) {
			log("inject: warc: mime set failed ");
			return true;
		}
		// check content type. if bad advance to next rec.
		int ct = mime.getContentType();
		if ( ct != CT_HTML &&
		     ct != CT_TEXT &&
		     ct != CT_XML &&
		     ct != CT_JSON )
			return true;
	}


	// for injecting "start" set this to \0
	if ( advanced ) { // m_start ) {
		// save it
		m_saved = *m_start;
		// null term it
		*m_start = '\0';
		// put back the original char on next round...?
		m_fixMe = true;
	}

	if ( ! delim && ! m_isWarc && ! m_isArc ) 
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
		int64_t ch64 = hash64n ( start , 0LL );
		// normalize it
		//Url u; u.set ( gr->m_url );
		// reset it
		m_injectUrlBuf.reset();
		// by default append a -<ch64> to the provided url
		m_injectUrlBuf.safePrintf("%s-%"UINT64"",u.getUrl(),ch64);

		// HOWEVER, if an hasmime is true and an http:// follows
		// the delimeter then use that as the url...
		// this way we can specify our own urls.
		char *du = start;
		du += gbstrlen(delim);
		if ( du && is_wspace_a ( *du ) ) du++;
		if ( du && is_wspace_a ( *du ) ) du++;
		if ( du && is_wspace_a ( *du ) ) du++;
		if ( gr->m_hasMime && 
		     (strncasecmp( du,"http://",7) == 0 ||
		      strncasecmp( du,"https://",8) == 0 ) ) {
			// find end of it
			char *uend = du + 7;
			for ( ; *uend && ! is_wspace_a(*uend) ; uend++ );
			// inject that then
			m_injectUrlBuf.reset();
			m_injectUrlBuf.safeMemcpy ( du , uend - du );
			m_injectUrlBuf.nullTerm();
			// and point to the actual http mime then
			start = uend;
		}

	}

	// count them
	m_injectCount++;

	m_inUse = true;

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
			       injectLoopWrapper9 ,

			       // extra shit
			       gr->m_firstIndexed,
			       gr->m_lastSpidered ,
			       // the ip of the url being injected.
			       // use 0 if unknown and it won't be valid.
			       gr->m_injectDocIp
			       ) )
		// we blocked...
		return false;


	m_inUse = false;

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
	TcpSocket *sock = msg7->m_socket;
	g_httpServer.sendDynamicPage ( sock, 
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
	ebuf.nullTerm();

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
	int32_t firstIp = hash32n(ubuf);
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

	m_xd.m_reallyInjectLinks = true;//gr->m_injectLinks;

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

///////////////////////////////////////
///////////////////////////////////////

// IMPORT CODE

///////////////////////////////////////
///////////////////////////////////////

//////
//
// BEGIN IMPORT TITLEDB FUNCTIONS
//
//////

// . injecting titledb files from other gb clusters into your collection
// . select the 'import' tab in the admin gui and enter the directory of
//   the titledb files you want to import/inject.
// . it will scan that directory for all titledb files.
// . you can also set max simultaneous injections. set to auto so it
//   will do 10 per host, up to like 100 max.

#define MAXINJECTSOUT 100

class ImportState {

public:

	// available msg7s to use
	class Multicast *m_ptrs;
	int32_t   m_numPtrs;

	// collection we are importing INTO
	collnum_t m_collnum;

	int64_t m_numIn;
	int64_t m_numOut;

	// bookmarking helpers
	int64_t m_fileOffset;
	int32_t m_bfFileId;
	BigFile m_bf;
	bool m_loadedPlaceHolder;
	int64_t m_bfFileSize;

	class Multicast *getAvailMulticast();// Msg7();

	void saveFileBookMark ( );//class Msg7 *msg7 );

	bool setCurrentTitleFileAndOffset ( );

	ImportState() ;
	~ImportState() { reset(); }

	bool importLoop();

	void reset();
};

ImportState::ImportState () {
	m_numIn = 0 ; 
	m_numOut = 0; 
	m_ptrs = NULL; 
	m_numPtrs=0;
	m_bfFileId = -1;
	m_bfFileSize = -1;
	m_fileOffset = 0;
}

void ImportState::reset() {
	for ( int32_t i = 0 ; i < m_numPtrs ; i++ ) {
		Multicast *mcast = &m_ptrs[i];
		mcast->destructor();
		//m_ptrs[i] = NULL;
	}
	mfree ( m_ptrs , MAXINJECTSOUT * sizeof(Multicast) , "ism7f" );
	m_ptrs = NULL;
	m_numPtrs = 0;
	m_fileOffset = 0LL;
	m_bfFileId = -2;
	m_loadedPlaceHolder = false;
}

static bool s_tried = false;

// if user clicks on "enable import loop" for a collection we call this
// from Parms.cpp
void resetImportLoopFlag () {
	s_tried = false;
}

// . call this when gb startsup
// . scan collections to see if any imports were active
// . returns false and sets g_errno on failure
bool resumeImports ( ) {

	if ( s_tried ) return true;
	s_tried = true;

	if ( g_hostdb.m_hostId != 0 ) return true;

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		if ( ! cr->m_importEnabled ) continue;
		// each import has its own state
		// it contains a sequence of msg7s to do simulataneous
		// injections
		ImportState *is;
		try { is = new (ImportState); }
		catch ( ... ) { 
			g_errno = ENOMEM;
			log("PageInject: new(%"INT32"): %s", 
			    (int32_t)sizeof(ImportState),mstrerror(g_errno));
			return false;
		}
		mnew ( is, sizeof(ImportState) , "isstate");
		// assign to cr as well
		cr->m_importState = is;
		// and collnum
		is->m_collnum = cr->m_collnum;
		// resume the import
		is->importLoop ( );
	}

	return true;
}



// . sets m_fileOffset and m_bf
// . returns false and sets g_errno on error
// . returns false if nothing to read too... but does not set g_errno
bool ImportState::setCurrentTitleFileAndOffset ( ) {

	// leave m_bf and m_fileOffset alone if there is more to read
	if ( m_fileOffset < m_bfFileSize )
		return true;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) return false;

	log("import: import finding next file");
	
	// if ( m_offIsValid ) {
	// 	//*off = m_fileOffset;
	// 	return &m_bf; 
	// }
	//m_offIsValid = true;

	// look for titledb0001.dat etc. files in the 
	// workingDir/inject/ subdir
	SafeBuf ddd;
	ddd.safePrintf("%sinject",cr->m_importDir.getBufStart());
	// now use the one provided. we should also provide the # of threads
	if ( cr->m_importDir.getBufStart() && 
	     cr->m_importDir.getBufStart()[0] ) {
		ddd.reset();
		ddd.safeStrcpy ( cr->m_importDir.getBufStart() );
	}

	//
	// assume we are the first filename
	// set s_fileId to the minimum
	//
	Dir dir;
	dir.set(ddd.getBufStart());

	if ( ! dir.open() ) return false;

	// assume none
	int32_t minFileId = -1;

	// getNextFilename() writes into this
	char pattern[64]; strcpy ( pattern , "titledb*" );
	char *filename;
	while ( ( filename = dir.getNextFilename ( pattern ) ) ) {
		// filename must be a certain length
		int32_t filenameLen = gbstrlen(filename);
		// we need at least "titledb0001.dat"
		if ( filenameLen < 15 ) continue;
		// ensure filename starts w/ our m_dbname
		if ( strncmp ( filename , "titledb", 7 ) != 0 )
			continue;
		// skip if not .dat file
		if ( ! strstr ( filename , ".dat" ) )
			continue;
		// then a 4 digit number should follow
		char *s = filename + 7;
		if ( ! isdigit(*(s+0)) ) continue;
		if ( ! isdigit(*(s+1)) ) continue;
		if ( ! isdigit(*(s+2)) ) continue;
		if ( ! isdigit(*(s+3)) ) continue;
		// convert digit to id
		int32_t id = atol(s);
		// . do not accept files we've already processed
		// . -1 means we haven't processed any yet
		if ( m_bfFileId >= 0 && id <= m_bfFileId ) continue;
		// the min of those we haven't yet processed/injected
		if ( id < minFileId || minFileId < 0 ) minFileId = id;
	}

	// get where we left off
	if ( ! m_loadedPlaceHolder ) {
		// read where we left off from file if possible
		char fname[256];
		sprintf(fname,"%slasttitledbinjectinfo.dat",g_hostdb.m_dir);
		SafeBuf ff;
		ff.fillFromFile(fname);
		if ( ff.length() > 1 ) {
			m_loadedPlaceHolder = true;
			// get the placeholder
			sscanf ( ff.getBufStart() 
				 , "%"UINT64",%"INT32""
				 , &m_fileOffset
				 , &minFileId
				 );
		}
	}

	// if no files! return false to indicate we are done
	if ( minFileId == -1 ) return false;

	// set up s_bf then
	//if ( m_bfFileId != minFileId ) {
	SafeBuf tmp;
	tmp.safePrintf("titledb%04"INT32"-000.dat"
		       //,dir.getDirname()
		       ,minFileId);
	m_bf.set ( dir.getDirname() ,tmp.getBufStart() );
	if ( ! m_bf.open( O_RDONLY ) ) {
		log("inject: import: could not open %s%s for reading",
		    dir.getDirname(),tmp.getBufStart());
		return false;
	}
	m_bfFileId = minFileId;
	// reset ptr into file
	//*off = 0;
	// and set this
	m_bfFileSize = m_bf.getFileSize();

	m_fileOffset = 0;
	//}

	log("import: importing from file %s",m_bf.getFilename());

	return true;//&m_bf;
}

void gotMulticastReplyWrapper ( void *state , void *state2 ) ;


//
// . ENTRY POINT FOR IMPORTING TITLEDB RECS FROM ANOTHER CLUSTER
// . when user clicks 'begin' in import page we come here..
// . so when that parm changes in Parms.cpp we sense that and call
//   beginImport(CollectionRec *cr)
// . or on startup we call resumeImports to check each coll for 
//   an import in progress.
// . search for files named titledb*.dat
// . if none found just return
// . when msg7 inject competes it calls this
// . call this from sleep wrapper in Process.cpp
// . returns false if would block (outstanding injects), true otherwise
// . sets g_errno on error
bool ImportState::importLoop ( ) {

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	if ( ! cr || g_hostdb.m_hostId != 0 ) { 
		// if coll was deleted!
		log("import: collnum %"INT32" deleted while importing into",
		    (int32_t)m_collnum);
		//if ( m_numOut > m_numIn ) return true;
		// delete the entire import state i guess
		// what happens if we have a msg7 reply come back in?
		// it should see the collrec is NULL and just fail.
		mdelete ( this, sizeof(ImportState) , "impstate");
		delete (this);
		return true;
	}

 INJECTLOOP:

	// stop if waiting on outstanding injects
	int64_t out = m_numOut - m_numIn;
	if ( out >= cr->m_numImportInjects ) {
		g_errno = 0;
		return false;
	}
	

	if ( ! cr->m_importEnabled ) {
		// wait for all to return
		if ( out > 0 ) return false;
		// then delete it
		log("import: collnum %"INT32" import loop disabled",
		    (int32_t)m_collnum);
		mdelete ( this, sizeof(ImportState) , "impstate");
		delete (this);
		return true;
	}




	// scan each titledb file scanning titledb0001.dat first,
	// titledb0003.dat second etc.

	//int64_t offset = -1;
	// . when offset is too big for current m_bigFile file then
	//   we go to the next and set offset to 0.
	// . sets m_bf and m_fileOffset
	if ( ! setCurrentTitleFileAndOffset ( ) ) {//cr  , -1 );
		log("import: import: no files to read");
		//goto INJECTLOOP;
		return true;
	}



	// this is -1 if none remain!
	if ( m_fileOffset == -1 ) {
		log("import: import fileoffset is -1. done.");
		return true;
	}

	int64_t saved = m_fileOffset;

	//Msg7 *msg7;
	//GigablastRequest *gr;
	//SafeBuf *sbuf = NULL;

	int32_t need = 12;
	int32_t dataSize = -1;
	//XmlDoc xd;
	key_t tkey;
	bool status;
	SafeBuf tmp;
	SafeBuf *sbuf = &tmp;
	int64_t docId;
	int32_t shardNum;
	int32_t key;
	Multicast *mcast;
	char *req;
	int32_t reqSize;

	if ( m_fileOffset >= m_bfFileSize ) {
		log("inject: import: done processing file %"INT32" %s",
		    m_bfFileId,m_bf.getFilename());
		goto nextFile;
	}
	
	// read in title rec key and data size
	status = m_bf.read ( &tkey, sizeof(key_t) , m_fileOffset );
	
	//if ( n != 12 ) goto nextFile;
	if ( g_errno ) {
		log("inject: import: reading file error: %s. advancing "
		    "to next file",mstrerror(g_errno));
		goto nextFile;
	}

	m_fileOffset += 12;

	// if negative key, skip
	if ( (tkey.n0 & 0x01) == 0 ) {
		goto INJECTLOOP;
	}

	// if non-negative then read in size
	status = m_bf.read ( &dataSize , 4 , m_fileOffset );
	if ( g_errno ) {
		log("main: failed to read in title rec "
		    "file. %s. Skipping file %s",
		    mstrerror(g_errno),m_bf.getFilename());
		goto nextFile;
	}
	m_fileOffset += 4;
	need += 4;
	need += dataSize;
	need += 4; // collnum, first 4 bytes
	if ( dataSize < 0 || dataSize > 500000000 ) {
		log("main: could not scan in titledb rec of "
		    "corrupt dataSize of %"INT32". BAILING ENTIRE "
		    "SCAN of file %s",dataSize,m_bf.getFilename());
		goto nextFile;
	}

	//gr = &msg7->m_gr;

	//XmlDoc *xd = getAvailXmlDoc();
	//msg7 = getAvailMsg7();
	mcast = getAvailMulticast();

	// if none, must have to wait for some to come back to us
	if ( ! mcast ) {
		// restore file offset
		//m_fileOffset = saved;
		// no, must have been a oom or something
		log("import: import no mcast available");
		return true;//false;
	}
	
	// this is for holding a compressed titlerec
	//sbuf = &mcast->m_sbuf;//&gr->m_sbuf;

	// point to start of buf
	sbuf->reset();

	// ensure we have enough room
	sbuf->reserve ( need );

	// collnum first 4 bytes
	sbuf->pushLong( (int32_t)m_collnum );

	// store title key
	sbuf->safeMemcpy ( &tkey , sizeof(key_t) );

	// then datasize if any. neg rec will have -1 datasize
	if ( dataSize >= 0 ) 
		sbuf->pushLong ( dataSize );

	// then read data rec itself into it, compressed titlerec part
	if ( dataSize > 0 ) {
		// read in the titlerec after the key/datasize
		status = m_bf.read ( sbuf->getBuf() ,
				     dataSize ,
				     m_fileOffset );
		if ( g_errno ) { // n != dataSize ) {
			log("main: failed to read in title rec "
			    "file. %s. Skipping file %s",
			    mstrerror(g_errno),m_bf.getFilename());
			// essentially free up this msg7 now
			//msg7->m_inUse = false;
			//msg7->reset();
			goto nextFile;
		}
		// advance
		m_fileOffset += dataSize;
		// it's good, count it
		sbuf->m_length += dataSize;
	}

	// set xmldoc from the title rec
	//xd->set ( sbuf.getBufStart() );
	//xd->m_masterState = NULL;
	//xd->m_masterCallback ( titledbInjectLoop );

	// we use this so we know where the doc we are injecting
	// was in the foregien titledb file. so we can update our bookmark
	// code.
	mcast->m_hackFileOff = saved;//m_fileOffset;
	mcast->m_hackFileId  = m_bfFileId;

	//
	// inject a title rec buf this time, we are doing an import
	// FROM A TITLEDB FILE!!!
	//
	//gr->m_titleRecBuf = &sbuf;

	// break it down into gw
	// xd.set2 ( sbuf.getBufStart() ,
	// 	  sbuf.length() , // max size
	// 	  cr->m_coll, // use our coll
	// 	  NULL , // pbuf for page parser
	// 	  1 , // niceness
	// 	  NULL ); //sreq );

	// // note it
	// log("import: importing %s",xd.m_firstUrl.getUrl());

	// now we can set gr for the injection
	// TODO: inject the whole "sbuf" so we get sitenuminlinks etc
	// all exactly the same...
	// gr->m_url = xd.getFirstUrl()->getUrl();
	// gr->m_queryToScrape = NULL;
	// gr->m_contentDelim = 0;
	// gr->m_contentTypeStr = g_contentTypeStrings [xd.m_contentType];
	// gr->m_contentFile = NULL;
	// gr->m_content = xd.ptr_utf8Content;
	// gr->m_diffbotReply = NULL;
	// gr->m_injectLinks = false;
	// gr->m_spiderLinks = true;
	// gr->m_shortReply = false;
	// gr->m_newOnly = false;
	// gr->m_deleteUrl = false;
	// gr->m_recycle = true; // recycle content? or sitelinks?
	// gr->m_dedup = false;
	// gr->m_hasMime = false;
	// gr->m_doConsistencyTesting = false;
	// gr->m_getSections = false;
	// gr->m_gotSections = false;
	// gr->m_charset = xd.m_charset;
	// gr->m_hopCount = xd.m_hopCount;


	//
	// point to next doc in the titledb file
	//
	//m_fileOffset += need;

	// get docid from key
	docId = g_titledb.getDocIdFromKey ( &tkey );

	// get shard that holds the titlerec for it
	shardNum = g_hostdb.getShardNumFromDocId ( docId );

	// for selecting which host in the shard receives it
	key = (int32_t)docId;


	m_numOut++;

	// then index it. master callback will be called
	//if ( ! xd->index() ) return false;

	// TODO: make this forward the request to an appropriate host!!
	// . gr->m_sbuf is set to the titlerec so this should handle that
	//   and use XmlDoc::set4() or whatever
	// if ( msg7->injectTitleRec ( msg7 , // state
	// 			    gotMsg7ReplyWrapper , // callback
	// 			    cr )) {
	// 	// it didn't block somehow...
	// 	msg7->m_inUse = false;
	// 	msg7->gotMsg7Reply();
	// }


	req = sbuf->getBufStart();
	reqSize = sbuf->length();

	if ( reqSize != need ) { char *xx=NULL;*xx=0 ; }

	// do not free it, let multicast free it after sending it
	sbuf->detachBuf();


	if ( ! mcast->send ( req ,
			     reqSize ,
			     0x07 ,
			     true , // ownmsg?
			     shardNum,
			     false, // send to whole shard?
			     key , // for selecting host in shard
			     mcast , // state
			     NULL , // state2
			     gotMulticastReplyWrapper ,
			     999999 ) ) { // total timeout in seconds
		log("import: import mcast had error: %s",mstrerror(g_errno));
		m_numIn++;
	}

	goto INJECTLOOP;

 nextFile:
	// invalidate this flag
	//m_offIsValid = false;
	// . and call this function. we add one to m_bfFileId so we
	//   do not re-get the file we just injected.
	// . sets m_bf and m_fileOffset
	// . returns false if nothing to read
	if ( ! setCurrentTitleFileAndOffset ( ) ) { //cr , m_bfFileId+1 );
		log("import: import: no files left to read");
		//goto INJECTLOOP;
		return true;
	}

	// if it returns NULL we are done!
	log("main: titledb injection loop completed. waiting for "
	    "outstanding injects to return.");
		
	if ( m_numOut > m_numIn )
		return false;

	log("main: all injects have returned. DONE.");

	// dummy return
	return true;
}

void gotMulticastReplyWrapper ( void *state , void *state2 ) {

	Multicast *mcast = (Multicast *)state;
	//msg7->gotMsg7Reply();

	ImportState *is = mcast->m_importState;

	is->m_numIn++;

	log("import: imported %"INT64" docs (off=%"INT64")",
	    is->m_numIn,is->m_fileOffset);

	if ( ! is->importLoop() ) return;

	// we will be called again when this multicast reply comes in...
	if ( is->m_numIn < is->m_numOut ) return;

	log("inject: import is done");

	CollectionRec *cr = g_collectiondb.getRec ( is->m_collnum );
	// signify to qa.cpp that we are done
	if ( cr ) cr->m_importState = NULL;

	mdelete ( is, sizeof(ImportState) , "impstate");
	delete (is);
}

// . return NULL with g_errno set on error
// . importLoop() calls this to get a msg7 to inject a doc from the foreign
//   titledb file into our local collection
Multicast *ImportState::getAvailMulticast() { // Msg7 ( ) {

	//static XmlDoc **s_ptrs = NULL;

	// this is legit because parent checks for it
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// each msg7 has an xmldoc doc in it
	if ( ! m_ptrs ) {
		int32_t max = (int32_t)MAXINJECTSOUT;
		m_ptrs=(Multicast *)mcalloc(sizeof(Multicast)* max,"sxdp");
		if ( ! m_ptrs ) return NULL;
		m_numPtrs = max;//(int32_t)MAXINJECTSOUT;
		for ( int32_t i = 0 ; i < m_numPtrs ;i++ ) 
			m_ptrs[i].constructor();
	}

	// respect the user limit for this coll
	int64_t out = m_numOut - m_numIn;
	if ( out >= cr->m_numImportInjects ) {
		g_errno = 0;
		return NULL;
	}

	// find one not in use and return it
	for ( int32_t i = 0 ; i < m_numPtrs ; i++ ) {
		// point to it
		Multicast *mcast = &m_ptrs[i];
		if ( mcast->m_inUse ) continue;
		//m7->m_inUse = true;
		mcast->m_importState = this;
		return mcast;
	}
	// none avail
	g_errno = 0;
	return NULL;
}

void saveImportStates ( ) {
	if ( g_hostdb.m_myHost->m_hostId != 0 ) return;
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		if ( ! cr->m_importEnabled ) continue;
		cr->m_importState->saveFileBookMark ();
	}
}

// "xd" is the XmlDoc that just completed injecting
void ImportState::saveFileBookMark ( ) { //Msg7 *msg7 ) {

	int64_t minOff = -1LL;
	int32_t minFileId = -1;

	//int32_t fileId  = msg7->m_hackFileId;
	//int64_t fileOff = msg7->m_hackFileOff;

	// if there is one outstanding the preceeded us, we can't update
	// the bookmark just yet.
	for ( int32_t i = 0 ; i < m_numPtrs ; i++ ) {
		Multicast *mcast = &m_ptrs[i];
		if ( ! mcast->m_inUse ) continue;
		if ( minOff == -1 ) {
			minOff = mcast->m_hackFileOff;
			minFileId = mcast->m_hackFileId;
			continue;
		}
		if ( mcast->m_hackFileId > minFileId ) 
			continue;
		if ( mcast->m_hackFileId == minFileId &&
		     mcast->m_hackFileOff > minOff ) 
			continue;
		minOff = mcast->m_hackFileOff;
		minFileId = mcast->m_hackFileId;
	}

	char fname[256];
	sprintf(fname,"%slasttitledbinjectinfo.dat",g_hostdb.m_dir);
	SafeBuf ff;
	ff.safePrintf("%"INT64",%"INT32"",minOff,minFileId);//_fileOffset,m_bfFileId);
	ff.save ( fname );
}
