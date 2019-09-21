#include "gb-include.h"

#include "Pages.h"
#include "Collectiondb.h"
#include "Msg4.h"
#include "Spider.h"
#include "Parms.h"

static bool sendReply        ( void *state  , bool addUrlEnabled );

//static void addedStuff ( void *state );


// class State1 {
// public:
// 	Msg4       m_msg4;
// 	//TcpSocket *m_socket;
// 	//HttpRequest m_hr;
// 	//int32_t       m_urlLen;
// 	//char       m_url[MAX_URL_LEN];
// 	//bool       m_strip;
// 	//bool       m_spiderLinks;
// 	int32_t       m_numSent;
// 	int32_t       m_numReceived;
// 	SpiderRequest m_sreq;
// };

// from PageCrawlBot.cpp:
bool getSpiderRequestMetaList ( char *doc , 
				SafeBuf *listBuf ,
				bool spiderLinks ,
				CollectionRec *cr ) ;


static void addedUrlsToSpiderdbWrapper ( void *state ) {
	//gr *st1 = (gr *)state;
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	sendReply ( state , true );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . add url page for admin, users use sendPageAddUrl() in PageRoot.cpp
bool sendPageAddUrl2 ( TcpSocket *sock , HttpRequest *hr ) {

	// or if in read-only mode
	if ( g_conf.m_readOnlyMode ) {
		g_errno = EREADONLYMODE;
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,500,msg);
	}

	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  urlLen = 0;
	char *urls = hr->getString ( "urls" , &urlLen , NULL /*default*/);
	// also try "url" and "urls"
	//if ( ! url ) url = r->getString ( "url" , &urlLen , NULL );
	//if ( ! url ) url = r->getString ( "urls" , &urlLen , NULL );


	char format = hr->getReplyFormat();

	char *c = hr->getString("c");
	
	if ( ! c && (format == FORMAT_XML || format == FORMAT_JSON) ) {
		g_errno = EMISSINGINPUT;
		char *msg = "missing c parm. See /admin/api to see parms.";
		return g_httpServer.sendErrorReply(sock,500,msg);
	}

	if ( ! urls && (format == FORMAT_XML || format == FORMAT_JSON) ) {
		g_errno = EMISSINGINPUT;
		char *msg = "missing urls parm. See /admin/api to see parms.";
		return g_httpServer.sendErrorReply(sock,500,msg);
	}


	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( hr );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		//g_msg = " (error: no collection)";
		char *msg = mstrerror(g_errno);
		return g_httpServer.sendErrorReply(sock,500,msg);
	}


	// make a new state
	GigablastRequest *gr;
	try { gr = new (GigablastRequest); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAddUrl: new(%i): %s", 
		    (int)sizeof(GigablastRequest),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(sock,500,
						   mstrerror(g_errno)); 
	}
	mnew ( gr , sizeof(GigablastRequest) , "PageAddUrl" );


	// this will fill in GigablastRequest so all the parms we need are set
	// set this. also sets gr->m_hr
	g_parms.setGigablastRequest ( sock , hr , gr );

	// if no url given, just print a blank page
	if ( ! urls ) return sendReply (  gr , true );

		


	bool status = true;

	// do not spider links for spots
	if ( ! getSpiderRequestMetaList ( urls,
					  // a safebuf
					  &gr->m_listBuf ,
					  gr->m_harvestLinks, // spiderLinks?
					  NULL ) )
		status = false;

	// empty?
	int32_t size = gr->m_listBuf.length();
	
	// error?
	if ( ! status ) {
		// nuke it
		mdelete ( gr , sizeof(gr) , "PageAddUrl" );
		delete (gr);
		return g_httpServer.sendErrorReply(gr);
	}
	// if not list
	if ( ! size ) {
		// nuke it
		mdelete ( gr , sizeof(gr) , "PageAddUrl" );
		delete (gr);
		g_errno = EMISSINGINPUT;
		return g_httpServer.sendErrorReply(gr);
	}

	// add to spiderdb
	if ( ! gr->m_msg4.addMetaList( gr->m_listBuf.getBufStart() ,
				       gr->m_listBuf.length(),
				       cr->m_coll,
				       gr ,
				       addedUrlsToSpiderdbWrapper,
				       0 // niceness
				       ) )
		// blocked!
		return false;

	// did not block, print page!
	//addedUrlsToSpiderdbWrapper(gr);
	sendReply ( gr , true );
	return true;

	// send back the reply
	//return sendReply ( gr , true );
}

bool sendReply ( void *state , bool addUrlEnabled ) {
	// allow others to add now
	//s_inprogress = false;
	// get the state properly
	//gr *st1 = (gr *) state;
	GigablastRequest *gr = (GigablastRequest *)state;
	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	SafeBuf xb;
	if ( gr->m_urlsBuf ) {
		xb.safeTruncateEllipsis ( gr->m_urlsBuf , 200 );
		log(LOG_INFO,"http: add url %s (%s)",
		    xb.getBufStart(),mstrerror(g_errno));
	}

	char format = gr->m_hr.getReplyFormat();
	TcpSocket *sock    = gr->m_socket;

	if ( format == FORMAT_JSON || format == FORMAT_XML ) {
		bool status = g_httpServer.sendSuccessReply ( gr );
		// nuke state
		mdelete ( gr , sizeof(gr) , "PageAddUrl" );
		delete (gr);
		return status;
	}


	int32_t ulen = 0;
	char *url = gr->m_urlsBuf;
	if ( url ) ulen = gbstrlen (url);

	// re-null it out if just http://
	bool printUrl = true;
	if ( ulen == 0 ) printUrl = false;
	if ( ! gr->m_urlsBuf       ) printUrl = false;
	if ( ulen==7 && printUrl && !strncasecmp(gr->m_url,"http://",7))
		printUrl = false;
	if ( ulen==8 && printUrl && !strncasecmp(gr->m_url,"https://",8))
		printUrl = false;

	// page is not more than 32k
	char buf[1024*32+MAX_URL_LEN*2];
	SafeBuf sb(buf, 1024*32+MAX_URL_LEN*2);
	
	//char rawbuf[1024*8];
	//SafeBuf rb(rawbuf, 1024*8);	
	//rb.safePrintf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	//rb.safePrintf("<status>\n");
	//CollectionRec *cr = g_collectiondb.getRec ( gr->m_coll );
	
	// collection name

	char tt [ 128 ];
	tt[0] = '\0';

	g_pages.printAdminTop ( &sb , sock , &gr->m_hr );

	// display url
	//char *url = gr->m_urlsBuf;
	//if ( url && ! url[0] ) url = NULL;

	// watch out for NULLs
	if ( ! url ) url = "http://";

	// if there was an error let them know
	//char msg[MAX_URL_LEN + 1024];
	SafeBuf mbuf;
	//char *pm = "";
	if ( g_errno ) {
		mbuf.safePrintf("<center><font color=red>");
		mbuf.safePrintf("Error adding url(s): <b>%s[%i]</b>", 
				mstrerror(g_errno) , g_errno);
		mbuf.safePrintf("</font></center>");
		//pm = msg;
		//rb.safePrintf("Error adding url(s): %s[%i]", 
		//	      mstrerror(g_errno) , g_errno);
	}
	else if ( printUrl ) {
		mbuf.safePrintf("<center><font color=red>");
		mbuf.safePrintf("<b><u>");
		mbuf.safeTruncateEllipsis(gr->m_urlsBuf,200);
		mbuf.safePrintf("</u></b></font> added to spider "
				 "queue "
				 "successfully<br><br>");
		mbuf.safePrintf("</font></center>");
		//rb.safePrintf("%s added to spider "
		//	      "queue successfully", url );
		//pm = msg;
		//url = "http://";
		//else
		//	pm = "Don't forget to <a href=/gigaboost.html>"
		//		"Gigaboost</a> your URL.";
	}


	if ( mbuf.length() ) sb.safeStrcpy ( mbuf.getBufStart() );

	g_parms.printParmTable ( &sb , sock , &gr->m_hr );

	// print the final tail
	g_pages.printTail ( &sb, true ); // admin?
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;

	// nuke state
	mdelete ( gr , sizeof(GigablastRequest) , "PageAddUrl" );
	delete (gr);

	return g_httpServer.sendDynamicPage (sock, 
					     sb.getBufStart(), 
					     sb.length(),
					     -1 ); // cachetime
}
