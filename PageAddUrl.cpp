#include "gb-include.h"

#include "Pages.h"
#include "Collectiondb.h"
#include "HashTable.h"
#include "Msg4.h"
#include "TuringTest.h"
#include "AutoBan.h"
#include "CollectionRec.h"
//#include "Links.h"
#include "Users.h"
#include "HashTableT.h"
#include "Spider.h"

static bool sendReply        ( void *state  , bool addUrlEnabled );
static bool canSubmit        (unsigned long h, long now, long maxUrlsPerIpDom);

static void addedStuff ( void *state );

void resetPageAddUrl ( ) ;

class State2 {
public:
	Url        m_url;
	char      *m_buf;
	long       m_bufLen;
	long       m_bufMaxLen;
};

class State1 {
public:
	Msg4       m_msg4;
	TcpSocket *m_socket;
        bool       m_isAdmin;
	char       m_coll[MAX_COLL_LEN+1];
	bool       m_goodAnswer;
	bool       m_doTuringTest;
	long       m_ufuLen;
	char       m_ufu[MAX_URL_LEN];

	long       m_urlLen;
	char       m_url[MAX_URL_LEN];

	char       m_username[MAX_USER_SIZE];
	bool       m_strip;
	bool       m_spiderLinks;
	bool       m_forceRespider;
 	// buf filled by the links coming from google, msn, yahoo, etc
	State2     m_state2[5]; // gb, goog, yahoo, msn, ask
	long       m_numSent;
	long       m_numReceived;
	//long       m_raw;
	SpiderRequest m_sreq;
};

// only allow up to 1 Msg10's to be in progress at a time
static bool s_inprogress = false;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageAddUrl ( TcpSocket *s , HttpRequest *r ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	long  urlLen = 0;
	char *url = r->getString ( "u" , &urlLen , NULL /*default*/);

	// see if they provided a url of a file of urls if they did not
	// provide a url to add directly
	bool isAdmin = g_collectiondb.isAdmin ( r , s );
	long  ufuLen = 0;
	char *ufu = NULL;
	if ( isAdmin )
		// get the url of a file of urls (ufu)
		ufu = r->getString ( "ufu" , &ufuLen , NULL );

	// can't be too long, that's obnoxious
	if ( urlLen > MAX_URL_LEN || ufuLen > MAX_URL_LEN ) {
		g_errno = EBUFTOOSMALL;
		g_msg = " (error: url too long)";
		return g_httpServer.sendErrorReply(s,500,"url too long");
	}
	// get the collection
	long  collLen = 0;
	char *coll    = r->getString("c",&collLen);
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
		collLen = gbstrlen(coll);
	}
	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		g_msg = " (error: no collection)";
		return g_httpServer.sendErrorReply(s,500,"no coll rec");
	}
	// . make sure the ip is not banned
	// . we may also have an exclusive list of IPs for private collections
	if ( ! cr->hasSearchPermission ( s ) ) {
		g_errno = ENOPERM;
		g_msg = " (error: permission denied)";
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
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
	// save socket and isAdmin
	st1->m_socket  = s;
	st1->m_isAdmin = isAdmin;

	// assume no url buf yet, set below
	//st1->m_ubuf      = NULL;
	//st1->m_ubufAlloc = NULL;
	//st1->m_metaList  = NULL;

	// save the url
	st1->m_url[0] = '\0';
	if ( url ) {
		// normalize and add www. if it needs it
		Url uu;
		uu.set ( url , gbstrlen(url) , true );
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

	// save the "ufu" (url of file of urls)
	st1->m_ufu[0] = '\0';
	st1->m_ufuLen  = ufuLen;
	memcpy ( st1->m_ufu , ufu , ufuLen );
	st1->m_ufu[ufuLen] = '\0';

	st1->m_doTuringTest = cr->m_doTuringTest;
	char *username     = g_users.getUsername(r);
	if(username) strcpy(st1->m_username,username);
	//st1->m_user    = g_pages.getUserType ( s , r );
	st1->m_spiderLinks = true;
	st1->m_strip   = true;
	//st1->m_raw = r->getLong("raw",0);

	// init state2
	for ( long i = 0; i < 5; i++ ){
		st1->m_state2[i].m_buf = NULL;
		st1->m_state2[i].m_bufLen = 0;
		st1->m_state2[i].m_bufMaxLen = 0;
	}

	// save the collection name in the State1 class
	if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	strncpy ( st1->m_coll , coll , collLen );
	st1->m_coll [ collLen ] = '\0';

	// assume they answered turing test correctly
	st1->m_goodAnswer = true;
	// if addurl is turned off, just print "disabled" msg
	if ( ! g_conf.m_addUrlEnabled ) return sendReply ( st1 , false );
	// can also be turned off in the collection rec
	if ( ! cr->m_addUrlEnabled    ) return sendReply ( st1 , false );
	// or if in read-only mode
	if (   g_conf.m_readOnlyMode  ) return sendReply ( st1 , false );
	// cannot add if another Msg10 from here is still in progress
	if ( s_inprogress ) return sendReply ( st1 , true );
	// use now as the spiderTime

	// get ip of submitter
	//unsigned long h = ipdom ( s->m_ip );
	// . use top 2 bytes now, some isps have large blocks
	// . if this causes problems, then they can do pay for inclusion
	unsigned long h = iptop ( s->m_ip );
	long codeLen;
	char* code = r->getString("code", &codeLen);
	if(g_autoBan.hasCode(code, codeLen, s->m_ip)) {
		long uipLen = 0;
		char* uip = r->getString("uip",&uipLen);
		long hip = 0;
		//use the uip when we have a raw query to test if 
		//we can submit
		if(uip) {
			hip = atoip(uip, uipLen);
			h = iptop( hip );
		}
	}


	st1->m_strip = r->getLong("strip",0);
	// Remember, for cgi, if the box is not checked, then it is not 
	// reported in the request, so set default return value to 0
	st1->m_spiderLinks = r->getLong("spiderLinks",0);

	// . should we force it into spiderdb even if already in there
	// . use to manually update spider times for a url
	// . however, will not remove old scheduled spider times
	// . mdw: made force on the default
	st1->m_forceRespider = r->getLong("force",1); // 0);

	long now = getTimeGlobal();

	// . allow 1 submit every 1 hour
	// . restrict by submitter domain ip
	if ( ! st1->m_isAdmin &&
	     ! canSubmit ( h , now , cr->m_maxAddUrlsPerIpDomPerDay ) ) {
		// return error page
		g_errno = ETOOEARLY;
		return sendReply ( st1 , true );
	}


	//st1->m_query = r->getString( "qts", &st1->m_queryLen );


	// check it, if turing test is enabled for this collection
	if ( ! st1->m_isAdmin && cr->m_doTuringTest && 
	     ! g_turingTest.isHuman(r) )  {
		// log note so we know it didn't make it
		g_msg = " (error: bad answer)";
		//log("PageAddUrl:: addurl failed for %s : bad answer",
		//    iptoa(s->m_ip));
		st1->m_goodAnswer = false;
		return sendReply ( st1 , true /*addUrl enabled?*/ );
	}

	//if ( st1->m_queryLen > 0 )
	//	return getPages( st1 );

	// if no url given, just print a blank page
	if ( ! url ) return sendReply (  st1 , true );


	//
	// make a SpiderRequest
	//

	SpiderRequest *sreq = &st1->m_sreq;
	// reset it
	sreq->reset();
	// make the probable docid
	long long probDocId = g_titledb.getProbableDocId ( st1->m_url );
	// make one up, like we do in PageReindex.cpp
	long firstIp = (probDocId & 0xffffffff);
	// . now fill it up
	// . TODO: calculate the other values... lazy!!! (m_isRSSExt, 
	//         m_siteNumInlinks,...)
	sreq->m_isNewOutlink = 1;
	sreq->m_isAddUrl     = 1;
	sreq->m_addedTime    = now;
	sreq->m_fakeFirstIp   = 1;
	sreq->m_probDocId     = probDocId;
	sreq->m_firstIp       = firstIp;
	sreq->m_hopCount      = 0;
	// its valid if root
	Url uu; uu.set ( st1->m_url );
	if ( uu.isRoot() ) sreq->m_hopCountValid = true;
	// too big?
	//long len = st1->m_urlLen;
	// the url! includes \0
	strcpy ( sreq->m_url , st1->m_url );
	// call this to set sreq->m_dataSize now
	sreq->setDataSize();
	// make the key dude -- after setting url
	sreq->setKey ( firstIp , 0LL, false );
	// need a fake first ip lest we core!
	//sreq->m_firstIp = (pdocId & 0xffffffff);
	// how to set m_firstIp? i guess addurl can be throttled independently
	// of the other urls???  use the hash of the domain for it!
	long  dlen;
	char *dom = getDomFast ( st1->m_url , &dlen );
	// fake it for this...
	//sreq->m_firstIp = hash32 ( dom , dlen );
	// sanity
	if ( ! dom ) {
		g_errno = EBADURL;
		return sendReply ( st1 , true );
	}
	// shortcut
	Msg4 *m = &st1->m_msg4;
	// now add that to spiderdb using msg4
	if ( ! m->addMetaList ( (char *)sreq    ,
				sreq->getRecSize() ,
				coll            ,
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
	s_inprogress = false;
	// get the state properly
	State1 *st1 = (State1 *) state;
	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	log(LOG_INFO,"http: add url %s (%s)",st1->m_url ,mstrerror(g_errno));
	// extract info from state
	TcpSocket *s       = st1->m_socket;
	bool       isAdmin = st1->m_isAdmin;
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
	if ( st1->m_coll[0] != '\0' && ! isAdmin ) 
		sprintf ( tt , " for %s", st1->m_coll );
	// the bg colors and style
	g_pages.printColors (&sb);
	sb.safePrintf ( "<title>Gigablast Add a Url</title>"
			"<table><tr><td valign=bottom><a href=/>"
		      //"<img width=200 length=25 border=0 src=/logo2.gif></a>"
			"<img width=210 height=25 border=0 src=/logo2.gif></a>"
			"&nbsp;&nbsp;</font></td><td><font size=+1>"
			"<b>Add Url%s</td></tr></table>" , tt );
	// watch out for NULLs
	if ( ! url ) url = "http://";
	// blank out url if adding a url of a file of urls
	//	if ( st1->m_ufu ) url = "http://";
	// if there was an error let them know
	char msg[MAX_URL_LEN + 1024];
	char *pm = "";
	if ( g_errno ) {
		if ( g_errno == ETOOEARLY ) {
			pm = "Error. 100 urls have "
			"already been submitted by "
			"this IP address for the last 24 hours. "
			"<a href=/addurlerror.html>Explanation</a>.";
			log("addurls: Failed for user at %s: "
			    "quota breeched.", iptoa(s->m_ip));

			//rb.safePrintf("Error. %li urls have "
			//	      "already been submitted by "
			//	      "this IP address for the "
			//	      "last 24 hours. ",
			//	      cr->m_maxAddUrlsPerIpDomPerDay);
		}
		else {
			sprintf ( msg ,"Error adding url(s): <b>%s[%i]</b>", 
				  mstrerror(g_errno) , g_errno);
			pm = msg;
			//rb.safePrintf("Error adding url(s): %s[%i]", 
			//	      mstrerror(g_errno) , g_errno);
		}
	}
	else {
		if      ( ! addUrlEnabled ) {//g_conf.m_addUrlEnabled ) 
			pm = "<font color=#ff0000>"
				"Sorry, this feature is temporarily disabled. "
				"Please try again later.</font>";
			if ( st1->m_urlLen ) 
				log("addurls: failed for user at %s: "
				    "add url is disabled. "
				    "Enable add url on the "
				    "Master Controls page and "
				    "on the Spider Controls page for "
				    "this collection.", 
				    iptoa(s->m_ip));

			//rb.safePrintf("Sorry, this feature is temporarily "
			//	      "disabled. Please try again later.");
		}
		else if ( s_inprogress ) {
			pm = "Add url busy. Try again later.";
			log("addurls: Failed for user at %s: "
			    "busy adding another.", iptoa(s->m_ip));
			//rb.safePrintf("Add url busy. Try again later.");

		}
		// did they fail the turing test?
		else if ( ! st1->m_goodAnswer ) {
			pm = "<font color=#ff0000>"
				"Oops, you did not enter the 4 large letters "
				"you see below. Please try again.</font>";
			//rb.safePrintf("could not add the url"
			//	      " because the turing test"
			//	      " is enabled.");

		}
		if ( url && ! st1->m_ufu[0] && url[0] && printUrl ) {
				sprintf ( msg ,"<u>%s</u> added to spider "
					  "queue "
					  "successfully", url );
				//rb.safePrintf("%s added to spider "
				//	      "queue successfully", url );
		}
		else if ( st1->m_ufu[0] ) {
			sprintf ( msg ,"urls in <u>%s</u> "
				  "added to spider queue "
				  "successfully", st1->m_ufu );

			//rb.safePrintf("urls in %s added to spider "
			//	      "queue successfully", url );

		}
		else {
			sprintf(msg,"Add the url you want:");
			//rb.safePrintf("Add the url you want:");
		}
		
		pm = msg;
		url = "http://";
		//else
		//	pm = "Don't forget to <a href=/gigaboost.html>"
		//		"Gigaboost</a> your URL.";
	}

	// TODO: show them a list of the urls they added
	// print the addUrl page in here with a status msg
	sb.safePrintf (
		  "<br><br><br><center>"
		  "<b>%s</b>" // the url msg
		  "<br><br>"
		  "<FORM method=get action=/addurl>" 
		  "<input type=text name=u value=\"%s\" size=50> "
		  "<input type=submit value=\"add url\" border=0><br>",pm,url);
	// if we're coming from local ip print the collection box
	if ( isAdmin ) 
		sb.safePrintf (
			  "\n"

			  "<br><b>or specify the url of a "
			  "file of urls to add:</b>"
			  "<br>\n"
			  "<input type=text name=ufu size=50> "
			  "<input type=submit value=\"add file\" border=0><br>"
			  "<br>"

			  //"<br><b>or a query to scrape from major engines:</b>"
			  //"<br>\n"
			  // qts = query to scrape
			  //"<input type=text name=qts size=49> "
			  //"<input type=submit value=\"add query\" border=0><br>"
			  //"<br>"

			  "<br><b>collection to add to:</b> "
			  "<input type=text name=c size=20 value=\"%s\">"
			  "<br><br>\n",
			  st1->m_coll );
	// otherwise hide it
	else 
		sb.safePrintf ( "<input type=hidden name=c value=\"%s\">" ,
			  st1->m_coll );

	
	char *ss = "";
	if ( st1->m_strip ) ss =" checked";
	sb.safePrintf ("<br>"
		       "<input type=checkbox name=strip value=1%s> "
		       "strip sessionids<br>", ss );
	
	sb.safePrintf("<br>\n");

 	//Adding spider links box
 	char *sl = "";
 	if ( st1->m_spiderLinks ) sl =" checked";
 	sb.safePrintf ("<input type=checkbox name=spiderLinks value=1%s> "
 		       "spider (harvest) links from page<br><br>\n", sl );

	if ( ! s_inprogress && addUrlEnabled && st1->m_doTuringTest ) {
		g_turingTest.printTest(&sb);
	}

	// . print the url box, etc...
	// . assume user is always forcing their url
	// sprintf ( p ,
	//	  "<br><br>"
	//	  "<input type=checkbox name=force value=1 checked> "
	//        "force respider<br>" );
	//p += gbstrlen ( p );
	/*
	sprintf ( p , 
		  "<br>"
		  "<a href=/?redir="
		  "http://www.searchengineguide.com/submit/gigablast.html>"
		  "<b>Search Engine Marketing News</b></a><br>"
		  "If you would like to stay up to date with the "
		  "latest articles on using search engines to market "
		  "your web site, we recommend subscribing to the "
		  "Search Engine Marketing weekly newsletter. Once a "
		  "week, a digest of articles from the top search "
		  "engine marketing experts is delivered straight to "
		  "your inbox for free.<br><br>");
	p += gbstrlen(p);
	*/
	// print the final tail
	g_pages.printTail ( &sb, st1->m_isAdmin ); // local?
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	//bool raw = st1->m_raw;
	// free the buffer
	//if ( st1->m_ubufAlloc )
	//	mfree ( st1->m_ubufAlloc , st1->m_ubufAllocSize,"pau");
	//if ( st1->m_metaList )
	//	mfree ( st1->m_metaList , st1->m_metaListAllocSize,"pau");
	// nuke state
	mdelete ( st1 , sizeof(State1) , "PageAddUrl" );
	delete (st1);
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	// . i thought we need -2 for cacheTime, but i guess not
	//rb.safePrintf("</status>\n");
	//if(raw)	return g_httpServer.sendDynamicPage (s, 
	//					     rb.getBufStart(), 
	//					     rb.length(),
	//					     -1/*cachetime*/,
	//					     false, // POSTREply? 
	//					     "text/xml"// content type
	//					     );

	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), 
					     sb.length(),
					     -1/*cachetime*/);
}


// we get like 100k submissions a day!!!
static HashTable s_htable;
static bool      s_init = false;
static long      s_lastTime = 0;
bool canSubmit ( unsigned long h , long now , long maxAddUrlsPerIpDomPerDay ) {
	// . sometimes no limit
	// . 0 means no limit because if they don't want any submission they
	//   can just turn off add url and we want to avoid excess 
	//   troubleshooting for why a url can't be added
	if ( maxAddUrlsPerIpDomPerDay <= 0 ) return true;
	// init the table
	if ( ! s_init ) {
		s_htable.set ( 50000 );
		s_init = true;
	}
	// clean out table every 24 hours
	if ( now - s_lastTime > 24*60*60 ) {
		s_lastTime = now;
		s_htable.clear();
	}
	// . if table almost full clean out ALL slots
	// . TODO: just clean out oldest slots
	if ( s_htable.getNumSlotsUsed() > 47000 ) s_htable.clear ();
	// . how many times has this IP domain submitted?
	// . allow 10 times per day
	long n = s_htable.getValue ( h );
	// if over 24hr limit then bail
	if ( n >= maxAddUrlsPerIpDomPerDay ) return false;
	// otherwise, inc it
	n++;
	// add to table, will replace old values
	s_htable.addKey ( h , n );
	return true;
}


void resetPageAddUrl ( ) {
	s_htable.reset();
}

