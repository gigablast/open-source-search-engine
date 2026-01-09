#include "gb-include.h"

#include "Pages.h"
#include "TcpSocket.h"
#include "HttpRequest.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Users.h"
#include "Parms.h"

bool sendPageAddDelColl ( TcpSocket *s , HttpRequest *r , bool add ) ;

bool sendPageAddColl ( TcpSocket *s , HttpRequest *r ) {
	return sendPageAddDelColl ( s , r , true ); 
}

bool sendPageDelColl ( TcpSocket *s , HttpRequest *r ) {
	return sendPageAddDelColl ( s , r , false ); 
}

bool sendPageAddDelColl ( TcpSocket *s , HttpRequest *r , bool add ) {
	// get collection name
	//int32_t  nclen;
	//char *nc   = r->getString ( "nc" , &nclen );
	//int32_t  cpclen;
	//char *cpc  = r->getString ( "cpc" , &cpclen );

	g_errno = 0;

	//bool cast = r->getLong("cast",0);

	char *msg = NULL;

	// if any host in network is dead, do not do this
	//if ( g_hostdb.hasDeadHost() ) msg = "A host in the network is dead.";

	// . are we adding a collection?
	// . return if error adding, might already exist!
	// . g_errno should be set
	// . WE DO NOT NEED THIS ANYMORE. Pages.cpp now broadcasts
	//   addcoll as CommandAddColl() parm.
	/*
	if ( nclen > 0 && add && ! cast ) {
		// do not allow "main" that is used for the "" collection
		// for backwards compatibility
		//if ( strcmp ( nc , "main" ) != 0 ) 
		g_collectiondb.addRec (nc,cpc,cpclen,true,(collnum_t)-1,
				       false , // isdump?
				       true  ) ;// save it?
		//else 
		//	log("admin: \"main\" collection is forbidden.");
	}

	if ( ! add && ! cast ) g_collectiondb.deleteRecs ( r )   ;
	*/

	char format = r->getReplyFormat();


	if ( format == FORMAT_XML || format == FORMAT_JSON ) {
		// no addcoll given?
		int32_t  page = g_pages.getDynamicPageNumber ( r );
		char *addcoll = r->getString("addcoll",NULL);
		char *delcoll = r->getString("delcoll",NULL);
		if ( ! addcoll ) addcoll = r->getString("addColl",NULL);
		if ( ! delcoll ) delcoll = r->getString("delColl",NULL);
		if ( page == PAGE_ADDCOLL && ! addcoll ) {
			g_errno = EBADENGINEER;
			char *msg = "no addcoll parm provided";
			return g_httpServer.sendErrorReply(s,g_errno,msg,NULL);
		}
		if ( page == PAGE_DELCOLL && ! delcoll ) {
			g_errno = EBADENGINEER;
			char *msg = "no delcoll parm provided";
			return g_httpServer.sendErrorReply(s,g_errno,msg,NULL);
		}
		return g_httpServer.sendSuccessReply(s,format);
	}

	// error?
	char *action = r->getString("action",NULL);
	char *addColl = r->getString("addcoll",NULL);

	// add our ip to the list
	//char *ips = r->getString("collips",NULL);
	//char *pwds = r->getString("collpwd",NULL);


	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);


	//
	// CLOUD SEARCH ENGINE SUPPORT - GIGABOT ERRORS
	//

	SafeBuf gtmp;
	char *gmsg = NULL;
	// is it too big?
	if ( action && addColl && gbstrlen(addColl) > MAX_COLL_LEN ) {
		gtmp.safePrintf("search engine name is too long");
		gmsg = gtmp.getBufStart();
	}
	// from Collectiondb.cpp::addNewColl() ensure coll name is legit
	char *x = addColl;
	for ( ; x && *x ; x++ ) {
		if ( is_alnum_a(*x) ) continue;
		if ( *x == '-' ) continue;
		if ( *x == '_' ) continue; // underscore now allowed
		break;
	}
	if ( x && *x ) {
		g_errno = EBADENGINEER;
		gtmp.safePrintf("<font color=red>Error. \"%s\" is a "
				"malformed name because it "
				"contains the '%c' character.</font><br><br>",
				addColl,*x);
		gmsg = gtmp.getBufStart();
	}

	//
	// END GIGABOT ERRORS
	//



	//
	// CLOUD SEARCH ENGINE SUPPORT
	//
	// if added the coll successfully, do not print same page, jump to
	// printing the basic settings page so they can add sites to it.
	// crap, this GET request, "r", is missing the "c" parm sometimes.
	// we need to use the "addcoll" parm anyway. maybe print a meta
	// redirect then?
	char guide = r->getLong("guide",0);
	// do not redirect if gmsg is set, there was a problem with the name
	if ( action && ! msg && format == FORMAT_HTML && guide && ! gmsg ) {
		//return g_parms.sendPageGeneric ( s, r, PAGE_BASIC_SETTINGS );
		// just redirect to it
		if ( addColl )
			p.safePrintf("<meta http-equiv=Refresh "
				      "content=\"0; URL=/admin/settings"
				      "?guide=1&c=%s\">",
				      addColl);
		return g_httpServer.sendDynamicPage (s,
						     p.getBufStart(),
						     p.length());
	}


	// print standard header
	g_pages.printAdminTop ( &p , s , r , NULL, 
				"onload=document."
				"getElementById('acbox').focus();");


	// gigabot error?
	//if ( gmsg ) 
	//	p.safePrintf("Gigabot says: %s<br><br>",gmsg);



	//int32_t  page     = g_pages.getDynamicPageNumber ( r );
	//char *coll     = r->getString    ( "c"    );
	//char *pwd      = r->getString    ( "pwd" );
	//char *username = g_users.getUsername( r );
	//int32_t  user = g_pages.getUserType ( s , r );
	//if ( ! coll )  coll = "";

	//if ( ! nc   )    nc = "";
	//if ( ! pwd  )   pwd = "";

	if ( g_errno ) msg = mstrerror(g_errno);





	if ( msg && ! guide ) {
		char *cc = "deleting";
		if ( add ) cc = "adding";
		p.safePrintf (
			  "<center>\n"
			  "<font color=red>"
			  "<b>Error %s collection: %s. "
			  "See log file for details.</b>"
			  "</font>"
			  "</center><br>\n",cc,msg);
	}

	//
	// CLOUD SEARCH ENGINE SUPPORT
	//
	if ( add && guide )
		printGigabotAdvice ( &p , PAGE_ADDCOLL , r , gmsg );



	// print the add collection box
	if ( add /*&& (! nc[0] || g_errno ) */ ) {

		char *t1 = "Add Collection";
		if ( guide ) t1 = "Add Search Engine";

		p.safePrintf (
			  "<center>\n<table %s>\n"
			   "<tr class=hdrow><td colspan=2>"
			  "<center><b>%s</b></center>"
			  "</td></tr>\n"
			  ,TABLE_STYLE
			  ,t1
			      );
		char *t2 = "collection";
		if ( guide ) t2 = "search engine";
		char *str = addColl;
		if ( ! addColl ) str = "";
		p.safePrintf (
			      "<tr bgcolor=#%s>"
			      "<td><b>name of new %s to add</td>\n"
			      "<td><input type=text name=addcoll size=30 "
			      "id=acbox "
			      "value=\"%s\">"
			      "</td></tr>\n"
			      , LIGHT_BLUE
			      , t2 
			      , str
			      );

		// don't show the clone box if we are under gigabot the guide
		if ( ! guide )
			p.safePrintf(
				     "<tr bgcolor=#%s>"
				     "<td><b>clone settings from this "
				     "collection</b>"
				     "<br><font size=1>Copy settings from "
				     "this pre-existing collection. Leave "
				     "blank to "
				     "accept default values.</font></td>\n"
				     "<td><input type=text name=clonecoll "
				     "size=30>"
				     "</td>"
				     "</tr>"
				     , LIGHT_BLUE
				     );

		// collection pwds
		p.safePrintf(
			     "<tr bgcolor=#%s>"
			     "<td><b>collection passwords"
			     "</b>"
			     "<br><font size=1>List of white space separated "
			     "passwords allowed to adminster collection."
			     "</font>"
			     "</td>\n"
			     "<td><input type=text name=collpwd "
			     "size=60>"
			     "</td>"
			     "</tr>"
			     , LIGHT_BLUE
			     );

		// ips box for security
		p.safePrintf(
			     "<tr bgcolor=#%s>"
			     "<td><b>collection ips"
			     "</b>"

			     "<br><font size=1>List of white space separated "
			     "IPs allowed to adminster collection."
			     "</font>"

			     "</td>\n"
			     "<td><input type=text name=collips "
			     "size=60>"
			     "</td>"
			     "</tr>"
			     , LIGHT_BLUE
			     );

		// now list collections from which to copy the config
		//p.safePrintf (
		//	  "<tr><td><b>copy configuration from this "
		//	  "collection</b><br><font size=1>Leave blank to "
		//	  "accept default values.</font></td>\n"
		//	  "<td><input type=text name=cpc value=\"%s\" size=30>"
		//	  "</td></tr>\n",coll);
		p.safePrintf ( "</table></center><br>\n");

		// wrap up the form started by printAdminTop
		g_pages.printAdminBottom ( &p );
		int32_t bufLen = p.length();
		return g_httpServer.sendDynamicPage (s,p.getBufStart(),bufLen);
	}

	// if we added a collection, print its page
	//if ( add && nc[0] && ! g_errno ) 
	//	return g_parms.sendPageGeneric2 ( s , r , PAGE_SEARCH ,
	//					  nc , pwd );

	if ( g_collectiondb.m_numRecsUsed <= 0 ) goto skip;

	// print all collections out in a checklist so you can check the
	// ones you want to delete, the values will be the id of that collectn
	p.safePrintf (
		  "<center>\n<table %s>\n"
		  "<tr class=hdrow><td><center><b>Delete Collections"
		  "</b></center></td></tr>\n"
		  "<tr bgcolor=#%s><td>"
		  "<center><b>Select the collections you wish to delete. "
		  //"<font color=red>This feature is currently under "
		  //"development.</font>"
		  "</b></center></td></tr>\n"
		  "<tr bgcolor=#%s><td>"
		  // table within a table
		  "<center><table width=20%%>\n",
		  TABLE_STYLE,
		  LIGHT_BLUE,
		  DARK_BLUE
		      );

	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		p.safePrintf (
			  "<tr bgcolor=#%s><td>"
			  "<input type=checkbox name=delcoll value=\"%s\"> "
			  "%s</td></tr>\n",
			  DARK_BLUE,
			  cr->m_coll,cr->m_coll);
	}
	p.safePrintf( "</table></center></td></tr></table><br>\n" );
skip:
	// wrap up the form started by printAdminTop
	g_pages.printAdminBottom ( &p );
	int32_t bufLen = p.length();
	return g_httpServer.sendDynamicPage (s,p.getBufStart(),bufLen);
}

bool sendPageCloneColl ( TcpSocket *s , HttpRequest *r ) {

	char format = r->getReplyFormat();

	char *coll = r->getString("c");

	if ( format == FORMAT_XML || format == FORMAT_JSON ) {
		if ( ! coll ) {
			g_errno = EBADENGINEER;
			char *msg = "no c parm provided";
			return g_httpServer.sendErrorReply(s,g_errno,msg,NULL);
		}
		return g_httpServer.sendSuccessReply(s,format);
	}

	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);

	// print standard header
	g_pages.printAdminTop ( &p , s , r );

	char *msg = NULL;
	if ( g_errno ) msg = mstrerror(g_errno);

	if ( msg ) {
		p.safePrintf (
			  "<center>\n"
			  "<font color=red>"
			  "<b>Error cloning collection: %s. "
			  "See log file for details.</b>"
			  "</font>"
			  "</center><br>\n",msg);
	}

	// print the clone box

	p.safePrintf (
		      "<center>\n<table %s>\n"
		      "<tr class=hdrow><td colspan=2>"
		      "<center><b>Clone Collection</b></center>"
		      "</td></tr>\n",
		      TABLE_STYLE);

	p.safePrintf (
		      "<tr bgcolor=#%s>"
		      "<td><b>clone settings from this collection</b>"
		      "<br><font size=1>Copy settings FROM this "
		      "pre-existing collection into the currently "
		      "selected collection."
		      "</font></td>\n"
		      "<td><input type=text name=clonecoll size=30>"
		      "</td>"
		      "</tr>"

		      , LIGHT_BLUE
		      );

	p.safePrintf ( "</table></center><br>\n");
	// wrap up the form started by printAdminTop
	g_pages.printAdminBottom ( &p );
	int32_t bufLen = p.length();
	return g_httpServer.sendDynamicPage (s,p.getBufStart(),bufLen);

}


