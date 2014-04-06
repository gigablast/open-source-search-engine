#include "gb-include.h"

#include "Pages.h"
#include "Collectiondb.h"
#include "Msg4.h"
#include "Spider.h"
#include "Parms.h"

static bool sendReply        ( void *state  , bool addUrlEnabled );

static void addedStuff ( void *state );

class State1 {
public:
	Msg4       m_msg4;
	TcpSocket *m_socket;

	HttpRequest m_hr;

	long       m_urlLen;
	char       m_url[MAX_URL_LEN];

	bool       m_strip;
	bool       m_spiderLinks;

	long       m_numSent;
	long       m_numReceived;
	SpiderRequest m_sreq;
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . add url page for admin, users use sendPageAddUrl() in PageRoot.cpp
bool sendPageAddUrl2 ( TcpSocket *s , HttpRequest *r ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	long  urlLen = 0;
	char *url = r->getString ( "u" , &urlLen , NULL /*default*/);
	// also try "url" and "urls"
	if ( ! url ) url = r->getString ( "url" , &urlLen , NULL );
	if ( ! url ) url = r->getString ( "urls" , &urlLen , NULL );

	// see if they provided a url of a file of urls if they did not
	// provide a url to add directly

	// can't be too long, that's obnoxious
	if ( urlLen > MAX_URL_LEN ) {
		g_errno = EBUFTOOSMALL;
		g_msg = " (error: url too long)";
		return g_httpServer.sendErrorReply(s,500,"url too long");
	}

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( r );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		g_msg = " (error: no collection)";
		return g_httpServer.sendErrorReply(s,500,"no coll rec");
	}


	// make a new state
	State1 *st1 ;
	try { st1 = new (State1); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAddUrl: new(%i): %s", 
		    sizeof(State1),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); }
	mnew ( st1 , sizeof(State1) , "PageAddUrl" );


	st1->m_socket  = s;

	st1->m_hr.copy ( r );

	// assume no url buf yet, set below
	//st1->m_ubuf      = NULL;
	//st1->m_ubufAlloc = NULL;
	//st1->m_metaList  = NULL;

	// save the url
	st1->m_url[0] = '\0';
	if ( url ) {
		// normalize and add www. if it needs it
		Url uu;
		// do not convert xyz.com to www.xyz.com because sometimes
		// people want xyz.com exactly
		uu.set ( url , gbstrlen(url) , false ); // true );
		// remove >'s i guess and store in st1->m_url[] buffer
		st1->m_urlLen=cleanInput ( st1->m_url,
					   MAX_URL_LEN, 
					   uu.getUrl(),
					   uu.getUrlLen() );
		// point to that as the url "buf" to add
		//st1->m_ubuf      = st1->m_url;
		//st1->m_ubufSize  = urlLen;
		//st1->m_ubufAlloc = NULL; // do not free it!
	}

	st1->m_spiderLinks = true;
	st1->m_strip   = true;

	// or if in read-only mode
	if ( g_conf.m_readOnlyMode  ) return sendReply ( st1 , false );

	st1->m_strip = r->getLong("strip",0);
	// Remember, for cgi, if the box is not checked, then it is not 
	// reported in the request, so set default return value to 0
	long spiderLinks = r->getLong("spiderLinks",-1);
	// also support all lowercase like PageInject.cpp uses
	if ( spiderLinks == -1 )
		spiderLinks = r->getLong("spiderlinks",0);

	// . should we force it into spiderdb even if already in there
	// . use to manually update spider times for a url
	// . however, will not remove old scheduled spider times
	// . mdw: made force on the default
	//st1->m_forceRespider = r->getLong("force",1); // 0);

	// if no url given, just print a blank page
	if ( ! url ) return sendReply (  st1 , true );


	//
	// make a SpiderRequest
	//

	SpiderRequest *sreq = &st1->m_sreq;
	// set the SpiderRequest from this add url
	if ( ! sreq->setFromAddUrl ( st1->m_url ) ) {
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// send reply back with g_errno set if this returned false
		return sendReply ( st1 , true );
	}



	// shortcut
	Msg4 *m = &st1->m_msg4;
	// now add that to spiderdb using msg4
	if ( ! m->addMetaList ( (char *)sreq    ,
				sreq->getRecSize() ,
				cr->m_coll            ,
				st1             , // state
				addedStuff      ,
				MAX_NICENESS    ,
				RDB_SPIDERDB    ) )
		// we blocked
		return false;

	// send back the reply
	return sendReply ( st1 , true );
}

void addedStuff ( void *state ) {
	State1 *st1 = (State1 *)state;
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	sendReply ( st1 , true );
}

bool sendReply ( void *state , bool addUrlEnabled ) {
	// allow others to add now
	//s_inprogress = false;
	// get the state properly
	State1 *st1 = (State1 *) state;
	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	log(LOG_INFO,"http: add url %s (%s)",st1->m_url ,mstrerror(g_errno));
	// extract info from state
	TcpSocket *s       = st1->m_socket;
	char      *url     = NULL;
	if ( st1->m_urlLen ) url = st1->m_url;
	// re-null it out if just http://
	bool printUrl = true;
	if ( st1->m_urlLen == 0 ) printUrl = false;
	if ( ! st1->m_url       ) printUrl = false;
	if (st1->m_urlLen==7&&st1->m_url&&!strncasecmp(st1->m_url,"http://",7))
		printUrl = false;
	// page is not more than 32k
	char buf[1024*32+MAX_URL_LEN*2];
	SafeBuf sb(buf, 1024*32+MAX_URL_LEN*2);
	
	//char rawbuf[1024*8];
	//SafeBuf rb(rawbuf, 1024*8);	
	//rb.safePrintf("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n");
	//rb.safePrintf("<status>\n");
	//CollectionRec *cr = g_collectiondb.getRec ( st1->m_coll );
	
	// collection name

	char tt [ 128 ];
	tt[0] = '\0';

	g_pages.printAdminTop ( &sb , st1->m_socket , &st1->m_hr );

	// watch out for NULLs
	if ( ! url ) url = "http://";

	// if there was an error let them know
	char msg[MAX_URL_LEN + 1024];
	char *pm = "";
	if ( g_errno ) {
		sprintf ( msg ,"Error adding url(s): <b>%s[%i]</b>", 
			  mstrerror(g_errno) , g_errno);
		pm = msg;
		//rb.safePrintf("Error adding url(s): %s[%i]", 
		//	      mstrerror(g_errno) , g_errno);
	}
	else if ( url && printUrl && url[0] ) {
		sprintf ( msg ,"<b><u>%s</u></b> added to spider "
			  "queue "
			  "successfully<br><br>", url );
		//rb.safePrintf("%s added to spider "
		//	      "queue successfully", url );
		pm = msg;
		url = "http://";
		//else
		//	pm = "Don't forget to <a href=/gigaboost.html>"
		//		"Gigaboost</a> your URL.";
	}
	

	g_parms.printParmTable ( &sb , st1->m_socket , &st1->m_hr );

	// print the final tail
	g_pages.printTail ( &sb, true ); // admin?
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;

	// nuke state
	mdelete ( st1 , sizeof(State1) , "PageAddUrl" );
	delete (st1);

	return g_httpServer.sendDynamicPage (s, 
					     sb.getBufStart(), 
					     sb.length(),
					     -1 ); // cachetime
}
