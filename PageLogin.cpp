#include "gb-include.h"

#include "Pages.h"
#include "Parms.h"
#include "Users.h"

bool sendPageLogin ( TcpSocket *s , HttpRequest *r ) {
	return sendPageLogin ( s , r, NULL);
}

bool sendPageLogin ( TcpSocket *s , HttpRequest *r , char *emsg ) {

	// get the collection
	long  collLen = 0;
	char *coll    = r->getString("c",&collLen);
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
		collLen = gbstrlen(coll);
	}

	// does collection exist? ...who cares, proxy doesn't have coll data.
	//CollectionRec *cr = g_collectiondb.getRec ( coll );
	//if ( ! cr ) emsg = "Collection does not exist.";


	// log off user whose username is in the cookie
	char *username = r->getStringFromCookie("username",NULL);
	char *password = r->getString("pwd",NULL);
	if ( username && !password ) g_users.logoffUser( username, s->m_ip );
	
	//  get username from the request
	username = NULL;
	username = r->getString("username",NULL);
	
	// reset emsg if user is coming for the first time
	long page = g_pages.getDynamicPageNumber(r);
	if ( !username && !password && 
		(page == PAGE_LOGIN || page == PAGE_LOGIN2) && emsg)
		emsg ="";

	// just make cookie same format as an http request for ez parsing
	char cookieData[2024];
	char host[1024]="";
	/*if ( cr && userType == USER_MASTER && username ) 
		return g_parms.sendPageGeneric ( s , r , PAGE_MASTER , cookie);
	if ( userType == USER_ADMIN && username )
		return g_parms.sendPageGeneric ( s , r , PAGE_SEARCH , cookie);
	*/


	// print it
	char  buf [ 2*1024 ];
	char *p    = buf;
	char *pend = buf + 2*1024;

	// print colors
	p = g_pages.printColors ( p , pend );
	// start table
	sprintf ( p , "<table><tr><td>");
	p += gbstrlen ( p );
	// print logo
	p = g_pages.printLogo   ( p , pend , coll );

	// make it printable
	char *pu = g_users.getUsername(r);
	if ( ! pu ) pu = "";

	// then Login
	if ( r->getHostLen() < 1024 )
		strncpy ( host, r->getHost(), r->getHostLen() );

	char *cookie = NULL;
	User *user = NULL;
	if ( username && host[0] ) user = g_users.getUser(username);
	if ( user && !emsg ){	
		sprintf ( cookieData , "username=%s;expires=0;"
					,username);
		
		// try to the get reference Page
		long refPage = r->getLong("ref",-1);
		if ( refPage >= 0 && refPage != PAGE_LOGIN && refPage != PAGE_LOGIN2
			&& g_users.hasPermission(username,refPage)){
			WebPage *page = g_pages.getPage(refPage);
			sprintf ( p, "<meta http-equiv=\"refresh\" content=\"0;"
		              	"http://%s/%s?c=%s\">",
				host,page->m_filename,coll);
		}
		else{	
			long pageNum = user->firstPage();
			char *path = g_pages.getPath(pageNum); 
			sprintf ( p, "<meta http-equiv=\"refresh\" content=\"0;"
			              "http://%s/%s?c=%s\">",
					host,path,coll);
		}
		p += gbstrlen ( p );
		cookie = cookieData;
	}
	
	if ( !emsg ) emsg = "";
	sprintf ( p ,
		  "&nbsp; &nbsp; "
		  "</td><td><font size=+1><b>Login</b></font></td></tr>"
		  "</table>" 
		  "<form method=post action=\"/login\" name=f>"
		  "<input type=hidden name=ref value=\"%li\">"
		  "<center>"
		  "<br><br>"
		  "<font color=ff0000><b>%s</b></font>"
		  "<br><br>"

		  "<table cellpadding=2><tr><td>"
		  "<b>Username</td><td>"
		  "<input type=text name=username size=30 value=\"%s\">"
		  "</td><td></td></tr>"
		  "<tr><td>"

		  "<b>Collection</td><td>"
		  "<input type=text name=c size=30 value=\"%s\">"
		  "</td><td></td></tr>"
		  "<tr><td>"
		  "<b>Password</td><td><input type=password name=pwd size=30>"
		  "</td><td>"
		  "<input type=submit value=ok border=0></td>"
		  "</tr></table>"
		  "</center>"
		  "<br><br>",
		  page, emsg , pu , coll );
	p += gbstrlen ( p );
	// master test
	/*
	long user = g_pages.getUserType ( s , r );
	if ( user != USER_MASTER ) {
		sprintf ( p , "\n<input type=hidden name=master value=0>\n"
			  "</form>" );
		p += gbstrlen ( p );
	}
	*/
	// print the tail
	p = g_pages.printTail ( p , pend , r->isLocal() ); // pwd
	// send the page
	return g_httpServer.sendDynamicPage ( s , buf , p - buf ,
					      -1    , // cacheTime
					      false , // POSTReply?
					      NULL  , // contentType
					      -1   ,
					      cookie);// Forbidden http status
}
