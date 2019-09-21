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
#include "Stats.h"

// from XmlDoc.cpp
bool isRobotsTxtFile ( char *url , int32_t urlLen ) ;

static bool sendHttpReply        ( void *state );

// scan each parm for OBJ_IR (injection request)
// and set it from the hr class then.
// use ptr_string/size_string stuff to point into the hr buf.
// but if we call serialize() then it makes news ones into its own blob.
// so we gotta know our first and last ptr_* pointers for serialize/deseria().
// kinda like how search input works
void setInjectionRequestFromParms ( TcpSocket *sock , 
				    HttpRequest *hr ,
				    CollectionRec *cr ,
				    InjectionRequest *ir ) {

	// just in case set all to zero
	memset ( ir , 0 , sizeof(InjectionRequest ));

	if ( ! cr ) {
		log("inject: no coll rec");
		return;
	}

	// use this, is more reliable, "coll" can disappear from under us
	ir->m_collnum = cr->m_collnum;

	// scan the parms
	for ( int i = 0 ; i < g_parms.m_numParms ; i++ ) {
		Parm *m = &g_parms.m_parms[i];
		if ( m->m_obj != OBJ_IR ) continue;
		// get it
		if ( m->m_type == TYPE_CHARPTR ||
		     m->m_type == TYPE_FILEUPLOADBUTTON ) {
			int32_t stringLen;
			char *str =hr->getString(m->m_cgi,&stringLen,m->m_def);
			// avoid overwriting the "url" parm with the "u" parm
			// since it is just an alias
			if ( ! str ) continue;
			// serialize it as a string
			char *foo = (char *)ir + m->m_off;
			char **ptrPtr = (char **)foo;
			// store the ptr pointing into hr buf for now
			*ptrPtr = str;
			// how many strings are we past ptr_url?
			int32_t count = ptrPtr - &ir->ptr_url;
			// and length. include \0
			int32_t *sizePtr = &ir->size_url + count;
			if ( str ) *sizePtr = stringLen + 1;
			else *sizePtr = 0;
			continue;
		}
		// numbers are easy
		else if ( m->m_type == TYPE_LONG ) {
			int32_t *ii = (int32_t *)((char *)ir + m->m_off);
			int32_t def = atoll(m->m_def);
			*ii = hr->getLong(m->m_cgi,def);
		}
		else if ( m->m_type == TYPE_CHECKBOX || 
			  m->m_type == TYPE_BOOL ) {
			char *ii = (char *)((char *)ir + m->m_off);
			int32_t def = atoll(m->m_def);
			*ii = (char)hr->getLong(m->m_cgi,def);
		}
		else if ( m->m_type == TYPE_IP ) {
			char *ii = (char *)((char *)ir + m->m_off);
			char *is = hr->getString(m->m_cgi,NULL);
			*(int32_t *)ii = 0; // default ip to 0
			// otherwise, set the ip
			if ( is ) *(int32_t *)ii = atoip(is);
		}
		// if unsupported let developer know
		else { char *xx=NULL;*xx=0; }
	}


	// if content is "" make it NULL so XmlDoc will download it
	// if user really wants empty content they can put a space in there
	// TODO: update help then...
	if ( ir->ptr_content && ! ir->ptr_content[0]  )
		ir->ptr_content = NULL;

	if ( ir->ptr_contentFile && ! ir->ptr_contentFile[0]  )
		ir->ptr_contentFile = NULL;

	if ( ir->ptr_contentDelim && ! ir->ptr_contentDelim[0] )
		ir->ptr_contentDelim = NULL;

	if ( ir->ptr_queryToScrape && ! ir->ptr_queryToScrape[0] ) 
		ir->ptr_queryToScrape = NULL;

	if ( ir->ptr_url && ! ir->ptr_url[0] ) 
		ir->ptr_url = NULL;

	// if we had a delimeter but not content, zero it out...
	if ( ! ir->ptr_content && ! ir->ptr_contentFile ) 
		ir->ptr_contentDelim = NULL;
}

// void doneLocalInjectWrapper ( void *state ) {
// 	XmlDoc *xd = (XmlDoc *)state;
// 	Msg7 *msg7 = (Msg7 *)THIS->m_injectionState;
// 	void (* callback ) (void *) = xd->m_injectionCallback;
// 	void *state = xd->m_injectionState;
// 	mdelete ( xd, sizeof(XmlDoc) , "PageInject" );
// 	delete (xd);
// 	// now call the callback since the local inject is done
// 	callback ( state );
// }

Host *getHostToHandleInjection ( char *url ) {
	Url norm;
	norm.set ( url );
	int64_t docId = g_titledb.getProbableDocId ( &norm );
	// get iroupId from docId
	uint32_t shardNum = getShardNumFromDocId ( docId );
	// from Msg22.cpp
	Host *group = g_hostdb.getShard ( shardNum );
	int32_t hostNum = docId % g_hostdb.m_numHostsPerShard;
	Host *host = &group[hostNum];

	bool isWarcInjection = false;
	int32_t ulen = gbstrlen(url);
	if ( ulen > 10 && strcmp(url+ulen-8,".warc.gz") == 0 )
		isWarcInjection = true;
	if ( ulen > 10 && strcmp(url+ulen-5,".warc") == 0 )
		isWarcInjection = true;

	if ( ! isWarcInjection ) return host;

	// warc files end up calling XmlDoc::indexWarcOrArc() which spawns
	// a msg7 injection request for each doc in the warc/arc file
	// so let's do load balancing differently for them so one host
	// doesn't end up doing a bunch of wget/gunzips on warc files 
	// thereby bottlenecking the cluster. get the first hostid that
	// we have not sent a msg7 injection request to that is still out
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		h->m_tmpCount = 0;
	}
	for ( UdpSlot *slot = g_udpServer.m_head2 ; 
	      slot ; 
	      slot = slot->m_next2 ) {
		// skip if not injection request
		if ( slot->m_msgType != 0x07 ) continue;
		//if ( ! slot->m_weInitiated ) continue;
		// if we did not initiate the injection request, i.e. if
		// it is to us, skip it
		if ( ! slot->m_callback ) continue;
		// who is it from?
		int32_t hostId = slot->m_hostId;
		if ( hostId < 0 ) continue;
		Host *h = g_hostdb.getHost ( hostId );
		if ( ! h ) continue;
		h->m_tmpCount++;
	}
	int32_t min = 999999;
	Host *minh = NULL;
	for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		if ( h->m_tmpCount == 0 ) return h;
		if ( h->m_tmpCount >= min ) continue;
		min  = h->m_tmpCount;
		minh = h;
	}
	if ( minh ) return minh;
	// how can this happen?
	return host;
}

// void gotForwardedReplyWrapper ( void *state ) {
// 	Msg7 *THIS = (Msg7 *)state;
// 	if ( g_errno ) 
// 		log("inject: error from remote host: %s",mstrerror(g_errno));
// 	THIS->m_callback ( THIS->m_state );
// }

void gotUdpReplyWrapper ( void *state , UdpSlot *slot ) {
	Msg7 *THIS = (Msg7 *)state;
	THIS->gotUdpReply(slot);
}

void Msg7::gotUdpReply ( UdpSlot *slot ) {

	// dont' free the sendbuf that is Msg7::m_sir/m_sirSize. msg7 will
	// free it. 
	slot->m_sendBufAlloc = NULL;

	m_replyIndexCode = EBADENGINEER;
	if ( slot && slot->m_readBuf && slot->m_readBufSize >= 12 ) {
		m_replyIndexCode = *(int32_t *)(slot->m_readBuf);
		m_replyDocId     = *(int64_t *)(slot->m_readBuf+4);
	}

	m_callback ( m_state );
}

// . "sir" is the serialized injectionrequest
// . this is called from the http interface, as well as from
//   XmlDoc::indexWarcOrArc() to inject individual recs/docs from the warc/arc
// . returns false and sets g_errno on error, true on success
bool Msg7::sendInjectionRequestToHost ( InjectionRequest *ir , 
					void *state ,
					void (* callback)(void *) ) {

	// ensure it is our own
	if ( &m_injectionRequest != ir ) { char *xx=NULL;*xx=0; }

	//if ( strcmp ( ir->ptr_url , "http://www.indyweek.com/durham/current/news.html" )  == 0 )
	//	fprintf(stderr,"ey\n");

	// ensure url not beyond limit
	if ( ir->ptr_url &&
	     gbstrlen(ir->ptr_url) > MAX_URL_LEN ) {
		g_errno = EURLTOOBIG;
		return log("inject: url too big.");
	}

	// hack fix core
	if ( ir->size_metadata == 0 ) ir->ptr_metadata = NULL;

	int32_t sirSize = 0;
	char *sir = serializeMsg2 ( ir ,
				    sizeof(InjectionRequest),
				    &ir->ptr_url,
				    &ir->size_url ,
				    &sirSize );
	// oom?
	if ( ! sir ) 
		return log("inject: failed to serialize request");

	// free any old one if we are being reused
	if ( m_sir ) {
		mfree ( m_sir , m_sirSize , "m7ir" );
		m_sir = NULL;
	}

	m_state = state;
	m_callback = callback;

	// save it for freeing later
	m_sir = sir;
	m_sirSize = sirSize;

	// forward it to another shard?
	Host *host = getHostToHandleInjection ( ir->ptr_url );

	log("inject: sending injection request of url %s reqsize=%i "
	    "to host #%"INT32"",
	    ir->ptr_url,(int)sirSize,host->m_hostId);

	// . ok, forward it to another host now
	// . and call got gotForwardedReplyWrapper when reply comes in
	// . returns false and sets g_errno on error
	// . returns true on success
	if ( g_udpServer.sendRequest ( sir , // req ,
					 sirSize,
					 0x07 , // msgtype
					 host->m_ip , // ip
					 host->m_port , // port
					 host->m_hostId,
					 NULL, // retslot
					 this,//state,
					 gotUdpReplyWrapper,//acallback,
					 99999999 , // timeout
					 -1 , // backoff
					 -1 , // maxwait
					 NULL, // replybuf
					 0, // replybufmaxsize
					 MAX_NICENESS // niceness
				       ) )
		// we also return true on success, false on error
		return true;

	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// there was an error, g_errno should be set
	return false;
}

void sendHttpReplyWrapper ( void *state ) {
	sendHttpReply ( state );
}

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

	char format = hr->getReplyFormat();
	char *coll  = hr->getString("c",NULL);

	// no url parm?
	if ( format != FORMAT_HTML && ! coll ) {//hr->getString("c",NULL) ) {
		g_errno = ENOCOLLREC;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	if ( g_repairMode ) { 
		g_errno = EREPAIRING;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( sock , hr );
	bool isCollAdmin = g_conf.isCollAdmin ( sock , hr );
	if ( ! isMasterAdmin && ! isCollAdmin ) {
		g_errno = ENOPERM;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	}

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

	// save some state info into msg7 directly
	msg7->m_socket = sock;
	msg7->m_format = format;
	msg7->m_replyIndexCode = 0;
	msg7->m_replyDocId = 0;
	
	msg7->m_hr.copy ( hr );

	// use Parms.cpp like how we set GigablastRequest to initialize parms
	// from the http request. i.e. setGigablastRequest(). 
	// the InjectionRequest::ptr_*  members will reference into
	// msg7->m_hr buffers so they should be ok.
	InjectionRequest *ir = &msg7->m_injectionRequest;
	setInjectionRequestFromParms (sock, &msg7->m_hr, cr, ir );

	// a scrape request?
	if ( ir->ptr_queryToScrape ) {
		//char *uf="http://www.google.com/search?num=50&"
		//	"q=%s&scoring=d&filter=0";
		msg7->m_linkDedupTable.set(4,0,512,NULL,0,false,0,"ldtab");
		if ( ! msg7->scrapeQuery ( ) ) return false;
		return sendHttpReply ( msg7 );
	}

	// if no url do not inject
	if ( ! ir->ptr_url )
		return sendHttpReply ( msg7 );

	// this will be NULL if the "content" was empty or not given
	//char *content = ir->ptr_content;

	// . try the uploaded file if nothing in the text area
	// . this will be NULL if the "content" was empty or not given
	//if ( ! content ) content = ir->ptr_contentFile;

	// forward it to another shard?
	//Host *host = getHostToHandleInjection ( ir->ptr_url );
	// if we are the responsible host, continue onwards with the injection
	// if ( host == g_hostdb.m_myHost ) {
	// 	// just do it now
	// 	if ( ! injectForReals ( ir , this , doneLocalInjectWrapper ) ) 
	// 		// if it would block, return false
	// 		return false;
	// 	// all done already...
	// 	log("inject: did not block");
	// 	mdelete ( msg7, sizeof(Msg7) , "PageInject" );
	// 	delete (msg7);
	// 	g_errno = EBADENGINEER;
	// 	char *msg = mstrerror(g_errno);
	// 	return g_httpServer.sendErrorReply(sock,g_errno,msg,NULL);
	// }

	// when we receive the udp reply then send back the http reply
	// we return true on success, which means it blocked... so return false
	if ( msg7->sendInjectionRequestToHost(ir,msg7,sendHttpReplyWrapper)) 
		return false;

	if ( ! g_errno ) {
		log("inject: blocked with no error!");
		char *xx=NULL;*xx=0; 
	}
		
	// error?
	log("inject: error forwarding reply: %s (%i)",  mstrerror(g_errno),
	    (int)g_errno);
	// it did not block, i gues we are done
	return sendHttpReply ( msg7 );
}

bool sendHttpReply ( void *state ) {
	// get the state properly
	Msg7 *msg7= (Msg7 *) state;

	InjectionRequest *ir = &msg7->m_injectionRequest;

	// extract info from state
	TcpSocket *sock = msg7->m_socket;

	//XmlDoc *xd = msg7->m_xd;

	int64_t docId  = msg7->m_replyDocId; // xd->m_docId;

	// might already be EURLTOOBIG set from above
	if ( ! g_errno ) g_errno = msg7->m_replyIndexCode;

	int32_t      hostId = 0;//msg7->m_msg7.m_hostId;

	// set g_errno to index code
	//if ( xd->m_indexCodeValid && xd->m_indexCode && ! g_errno )
	//	g_errno = xd->m_indexCode;


	char format = msg7->m_format;

	// no url parm?
	if ( ! g_errno && ! ir->ptr_url && format != FORMAT_HTML )
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
		// if xmldoc was a container of subdocs that XmlDoc::indexDoc()
		// call indexWarcOrArc() on then docid is not valid since
		// we do not index container docs.
		//int64_t docId = xd->m_docId;
		//if ( ! xd->m_docIdValid ) docId = 0;
		am.safePrintf("\t<docId>%"INT64"</docId>\n",docId);
		// this will have to be re-tooled if we deem necessary.
		// was being use to do section voting for diffbot
		// upon a url being injected.
		/*
		if ( ir->m_getSections ) {
			SafeBuf *secBuf = xd->getInlineSectionVotingBuf();
			am.safePrintf("\t<htmlSrc><![CDATA[");
			if ( secBuf->length() ) 
				am.cdataEncode(secBuf->getBufStart());
			am.safePrintf("]]></htmlSrc>\n");
		}
		*/
		am.safePrintf("</response>\n");
		ct = "text/xml";
	}

	if ( format == FORMAT_JSON ) {
		am.safePrintf("{\"response\":{\n");
		am.safePrintf("\t\"statusCode\":%"INT32",\n",(int32_t)g_errno);
		am.safePrintf("\t\"statusMsg\":\"");
		am.jsonEncode(mstrerror(g_errno));
		am.safePrintf("\",\n");
		am.safePrintf("\t\"docId\":%"INT64",\n",docId);//xd->m_docId);
		// this will have to be re-tooled if we deem necessary.
		// was being use to do section voting for diffbot
		// upon a url being injected.
		/*

		if ( ir->m_getSections ) {
			SafeBuf *secBuf = xd->getInlineSectionVotingBuf();
			am.safePrintf("\t\"htmlSrc\":\"");
			if ( secBuf->length() ) 
				am.jsonEncode(secBuf->getBufStart());
			am.safePrintf("\",\n");
		}
		*/
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

	char *url = ir->ptr_url;
	
	// . if we're talking w/ a robot he doesn't care about this crap
	// . send him back the error code (0 means success)
	if ( url && ir->m_shortReply ) {
		char buf[1024*32];
		char *p = buf;
		// return docid and hostid
		if ( ! g_errno ) p += sprintf ( p , 
						"0,docId=%"INT64","
						"hostId=%"INT32"," , 
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
	g_pages.printAdminTop ( &sb, sock , &msg7->m_hr );

	// print a response msg if rendering the page after a submission
	if ( g_errno )
		sb.safePrintf ( "<center>Error injecting url: <b>%s[%i]</b>"
				"</center>", 
				mstrerror(g_errno) , g_errno);
	else if ( (ir->ptr_url && ir->ptr_url[0]) ||
		  (ir->ptr_queryToScrape&&ir->ptr_queryToScrape[0]) )
		sb.safePrintf ( "<center><b>Sucessfully injected %s"
				"</center><br>"
				, ir->ptr_url
				//, xd->m_firstUrl.m_url
				);


	// print the table of injection parms
	g_parms.printParmTable ( &sb , sock , &msg7->m_hr );


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

/////////////
//
// HANDLE INCOMING UDP INJECTION REQUEST
//
////////////

XmlDoc *s_injectHead = NULL;
XmlDoc *s_injectTail = NULL;

XmlDoc *getInjectHead ( ) { return s_injectHead; }

// send back a reply to the originator of the msg7 injection request
void sendUdpReply7 ( void *state ) {

	XmlDoc *xd = (XmlDoc *)state;

	// remove from linked list
	if ( xd->m_nextInject ) 
		xd->m_nextInject->m_prevInject = xd->m_prevInject;
	if ( xd->m_prevInject )
		xd->m_prevInject->m_nextInject = xd->m_nextInject;
	if ( s_injectHead == xd )
		s_injectHead = xd->m_nextInject;
	if ( s_injectTail == xd )
		s_injectTail = xd->m_prevInject;
	xd->m_nextInject = NULL;
	xd->m_prevInject = NULL;


	UdpSlot *slot = xd->m_injectionSlot;

    uint32_t statColor = 0xccffcc;
    if(xd->m_indexCode) {
        statColor = 0xaaddaa;//0x4e99e9;
    }
	g_stats.addStat_r ( xd->m_rawUtf8ContentSize,
						xd->m_injectStartTime, 
						gettimeofdayInMilliseconds(),
						statColor );


	// injecting a warc seems to not set m_indexCodeValid to true
	// for the container doc... hmmm...
	int32_t indexCode = -1;
	int64_t docId = 0;
	if ( xd && xd->m_indexCodeValid ) indexCode = xd->m_indexCode;
	if ( xd && xd->m_docIdValid     ) docId = xd->m_docId;
	mdelete ( xd, sizeof(XmlDoc) , "PageInject" );
	delete (xd);


	if ( g_errno ) {
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}
	// just send back the 4 byte indexcode, which is 0 on success,
	// otherwise it is the errno
	char *tmp = slot->m_tmpBuf;
	char *p = tmp;
	memcpy ( p , (char *)&indexCode , 4 );
	p += 4;
	memcpy ( p , (char *)&docId , 8 );
	p += 8;

	g_udpServer.sendReply_ass(tmp,(p-tmp),NULL,0,slot);
}
	

void handleRequest7 ( UdpSlot *slot , int32_t netnice ) {

	InjectionRequest *ir = (InjectionRequest *)slot->m_readBuf;

	// now just supply the first guy's char ** and size ptr
	if ( ! deserializeMsg2 ( &ir->ptr_url, &ir->size_url ) ) {
		log("inject: error deserializing inject request from "
		    "host ip %s port %i",iptoa(slot->m_ip),(int)slot->m_port);
		g_errno = EBADREQUEST;
		g_udpServer.sendErrorReply(slot,g_errno);
		//g_corruptCount++;
		return;
	}
		

	// the url can be like xyz.com. so need to do another corruption
	// test for ia
	if ( ! ir->ptr_url ) { // || strncmp(ir->ptr_url,"http",4) != 0 ) {
		//log("inject: trying to inject NULL or non http url.");
		log("inject: trying to inject NULL url.");
		g_errno = EBADURL;
		//g_corruptCount++;
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}

	CollectionRec *cr = g_collectiondb.getRec ( ir->m_collnum );
	if ( ! cr ) {
		log("inject: cr rec is null %i", ir->m_collnum);
		g_errno = ENOCOLLREC;
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}

	XmlDoc *xd;
	try { xd = new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: import failed: new(%i): %s", 
		    (int)sizeof(XmlDoc),mstrerror(g_errno));
		g_udpServer.sendErrorReply(slot,g_errno);
		return;
	}
	mnew ( xd, sizeof(XmlDoc) , "PageInject" );

	xd->m_injectionSlot = slot;
	xd->m_injectStartTime = gettimeofdayInMilliseconds();

	// add to linked list
	xd->m_nextInject = NULL;
	xd->m_prevInject = NULL;
	if ( s_injectTail ) {
		s_injectTail->m_nextInject = xd;
		xd->m_prevInject = s_injectTail;
		s_injectTail = xd;
	}
	else {
		s_injectHead = xd;
		s_injectTail = xd;
	}
	if(ir->ptr_content && ir->ptr_content[ir->size_content - 1]) {
		// XmlDoc expects this buffer to be null terminated.
		char *xx=NULL;*xx=0;
	}

	if ( ! xd->injectDoc ( ir->ptr_url , // m_injectUrlBuf.getBufStart() ,
			       cr ,
			       ir->ptr_content , // start , // content ,
			       ir->ptr_diffbotReply,
			       // if this doc is a 'container doc' then
			       // hasMime applies to the SUBDOCS only!!
			       ir->m_hasMime, // content starts with http mime?
			       ir->m_hopCount,
			       ir->m_charset,

			       ir->m_deleteUrl,
			       // warcs/arcs include the mime so we don't
			       // look at this in that case in 
			       // XmlDoc::injectDoc() when it calls set4()
			       ir->ptr_contentTypeStr, // text/html text/xml
			       ir->m_spiderLinks ,
			       ir->m_newOnly, // index iff new

			       xd, // state ,
			       sendUdpReply7 ,

			       // extra shit
			       ir->m_firstIndexed,
			       ir->m_lastSpidered ,
			       // the ip of the url being injected.
			       // use 0 if unknown and it won't be valid.
			       ir->m_injectDocIp ,
				   ir->ptr_contentDelim,
				   ir->ptr_metadata,
				   ir->size_metadata,
				   ir->size_content - 1 // there should be a null in that last byte
			       ) )
		// we blocked...
		return;

	// if injected without blocking, send back reply
	sendUdpReply7 ( xd );
}



//////////////////
//
// TITLEREC INJECT IMPORT CODE
//
//////////////////

Msg7::Msg7 () {
	m_xd = NULL;
	m_sir = NULL;
	m_inUse = false;
	reset();
}

Msg7::~Msg7 () {
	reset();
}

//void Msg7::constructor () {
//	reset();
//}

void Msg7::reset() { 
	m_round = 0;
	//if ( m_inUse ) { char *xx=NULL;*xx=0; }
	//m_firstTime = true;
	//m_fixMe = false;
	//m_injectCount = 0;
	//m_start = NULL;
	m_sbuf.reset();
	//m_isDoneInjecting = false;
	if ( m_xd ) {
		mdelete ( m_xd, sizeof(XmlDoc) , "PageInject" );
		delete (m_xd);
		m_xd = NULL;
	}
	if ( m_sir ) {
		mfree ( m_sir , m_sirSize , "m7ir" );
		m_sir = NULL;
	}
}

// when XmlDoc::inject() complets it calls this
//void doneInjectingWrapper9 ( void *state ) {
/*
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

	if ( loopIt ) { // && msg7->m_start ) {
		// do another injection. returns false if it blocks
		if ( ! msg7->inject ( msg7->m_state , msg7->m_callback ) )
			return;
	}

	// if we don't check 'loopIt' here single url injects will loop
	if ( loopIt && ! msg7->m_isDoneInjecting )
		goto loop;

	// and we call the original caller
	msg7->m_callback ( msg7->m_state );
}
*/

/*
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

	return inject2 ( state , callback );
}
*/

// returns false if would block
// bool Msg7::injectTitleRec ( void *state ,
// 			    void (*callback)(void *state) ,
// 			    CollectionRec *cr ) {


// when XmlDoc::inject() complets it calls this
void doneInjectingWrapper10 ( void *state ) {
	XmlDoc *xd = (XmlDoc *)state;
	UdpSlot *slot = (UdpSlot *)xd->m_slot;
	int32_t err = g_errno;
	mdelete ( xd, sizeof(XmlDoc) , "PageInject" );
	delete (xd);
	g_errno = err;
	if ( g_errno ) g_udpServer.sendErrorReply(slot,g_errno);
	else           g_udpServer.sendReply_ass(NULL,0,NULL,0,slot);
}

void handleRequest7Import ( UdpSlot *slot , int32_t netnice ) {

	//m_state = state;
	//m_callback = callback;


	XmlDoc *xd;
	try { xd = new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: import failed: new(%i): %s", 
		    (int)sizeof(XmlDoc),mstrerror(g_errno));
		g_udpServer.sendErrorReply(slot,g_errno);
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
		g_udpServer.sendErrorReply(slot,g_errno);
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
	if ( g_errno ) g_udpServer.sendErrorReply(slot,g_errno);
	else           g_udpServer.sendReply_ass(NULL,0,NULL,0,slot);
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
		sb->safeMemcpy(&msg7->m_xd->m_serpBuf);
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

	InjectionRequest *ir = &m_injectionRequest;

	// error?
	char *qts = ir->ptr_queryToScrape;
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

	//char *coll2 = ir->m_coll;
	CollectionRec *cr = g_collectiondb.getRec ( ir->m_collnum );//coll2 );

	// need to make a new one now
	XmlDoc *xd;
	try { xd = new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageInject: scrape failed: new(%i): %s", 
		    (int)sizeof(XmlDoc),mstrerror(g_errno));
		return true;
	}
	mnew ( xd, sizeof(XmlDoc) , "PageInject" );

	// save it
	m_xd = xd;

	// forceDEl = false, niceness = 0
	m_xd->set4 ( &sreq , NULL , cr->m_coll , NULL , 0 ); 

	//m_xd.m_isScraping = true;

	// download without throttling
	//m_xd.m_throttleDownload = false;

	// disregard this
	m_xd->m_useRobotsTxt = false;

	// this will tell it to index ahrefs first before indexing
	// the doc. but do NOT do this if we are from ahrefs.com
	// ourselves to avoid recursive explosion!!
	if ( m_useAhrefs )
		m_xd->m_useAhrefs = true;

	m_xd->m_reallyInjectLinks = true;//ir->m_injectLinks;

	//
	// rather than just add the links of the page to spiderdb,
	// let's inject them!
	//
	m_xd->setCallback ( this , doneInjectingLinksWrapper );

	// niceness is 0
	m_linkDedupTable.set(4,0,512,NULL,0,false,0,"ldtab2");

	// do we actually inject the links, or just scrape?
	if ( ! m_xd->injectLinks ( &m_linkDedupTable ,
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
