#include "gb-include.h"

#include "Pages.h"
#include "Parms.h"


// any admin page calls cr->hasPermission ( hr ) and if that returns false
// then we call this function to give users a chance to login
bool sendPageLogin ( TcpSocket *socket , HttpRequest *hr ) {

	// get the collection
	long  collLen = 0;
	char *coll    = hr->getString("c",&collLen);

	// default to main collection. if you can login to main then you
	// are considered the root admin here...
	if ( ! coll ) coll = "main";

	SafeBuf emsg;

	// does collection exist? ...who cares, proxy doesn't have coll data.
	CollectionRec *cr = g_collectiondb.getRec ( hr );
	if ( ! cr )
		emsg.safePrintf("Collection \"%s\" does not exist.",coll);

	// just make cookie same format as an http request for ez parsing
	char cookieData[2024];

	SafeBuf sb;

	// print colors
	g_pages.printColors ( &sb );
	// start table
	sb.safePrintf( "<table><tr><td>");
	// print logo
	g_pages.printLogo   ( &sb , coll );

	// get password from cgi parms OR cookie
	char *pwd = hr->getString("pwd",NULL);

	bool hasPermission = false;

	// this password applies to ALL collections. it's the root admin pwd
	if ( cr && pwd && g_conf.isRootAdmin ( socket , hr ) ) 
		hasPermission = true;

	if ( emsg.length() == 0 && ! hasPermission )
		emsg.safePrintf("Admin password incorrect");

	// sanity
	if ( hasPermission && emsg.length() ) { char *xx=NULL;*xx=0; }

	// what page are they originally trying to get to?
	long page = g_pages.getDynamicPageNumber(hr);

	char *cookie = NULL;

	if ( hasPermission ) {
		// "pwd" could be NULL... like when it is not required,
		// perhaps only the right ip address is required, but if it
		// is there then store it in a cookie with no expiration
		if ( pwd ) sprintf ( cookieData, "pwd=%s;expires=0;",pwd);
		// try to the get reference Page
		long refPage = hr->getLong("ref",-1);
		// if they cam to login page directly... to to basic page then
		if ( refPage == PAGE_LOGIN ||
		     refPage == PAGE_LOGIN2 ||
		     refPage < 0 )
			refPage = PAGE_BASIC_SETTINGS;
		// if they had an original destination, redirect there NOW
		WebPage *page = g_pages.getPage(refPage);
		// and redirect to it
		sb.safePrintf("<meta http-equiv=\"refresh\" content=\"0;"
			      "/%s?c=%s\">", page->m_filename,coll);
		// return cookie in server reply if pwd was non-null
		cookie = cookieData;
	}

	char *ep = emsg.getBufStart();
	if ( !ep ) ep = "";
	
	sb.safePrintf(
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

		  //"<b>Collection</td><td>"
		  "<input type=hidden name=c size=30 value=\"%s\">"
		  //"</td><td></td></tr>"
		  //"<tr><td>"

		  "<b>Admin Password</td>"
		  "<td><input type=password name=pwd size=30>"
		  "</td><td>"
		  "<input type=submit value=ok border=0></td>"
		  "</tr></table>"
		  "</center>"
		  "<br><br>"
		  , page, ep , coll );

	// print the tail
	g_pages.printTail ( &sb , hr->isLocal() ); // pwd
	// send the page
	return g_httpServer.sendDynamicPage ( socket , 
					      sb.getBufStart(),
					      sb.length(),
					      -1    , // cacheTime
					      false , // POSTReply?
					      NULL  , // contentType
					      -1   ,
					      cookie);// Forbidden http status
}
