#include "gb-include.h"

#include "Pages.h"
#include "TcpSocket.h"
#include "HttpRequest.h"
#include "Collectiondb.h"
#include "CollectionRec.h"
#include "Users.h"

bool sendPageAddDelColl ( TcpSocket *s , HttpRequest *r , bool add ) ;

bool sendPageAddColl ( TcpSocket *s , HttpRequest *r ) {
	return sendPageAddDelColl ( s , r , true ); 
}

bool sendPageDelColl ( TcpSocket *s , HttpRequest *r ) {
	return sendPageAddDelColl ( s , r , false ); 
}

bool sendPageAddDelColl ( TcpSocket *s , HttpRequest *r , bool add ) {
	// get collection name
	long  nclen;
	char *nc   = r->getString ( "nc" , &nclen );
	long  cpclen;
	char *cpc  = r->getString ( "cpc" , &cpclen );

	g_errno = 0;

	bool cast = r->getLong("cast",0);

	char *msg = NULL;

	// if any host in network is dead, do not do this
	if ( g_hostdb.hasDeadHost() ) msg = "A host in the network is dead.";

	// . are we adding a collection?
	// . return if error adding, might already exist!
	// . g_errno should be set
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

	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);
	// print standard header
	g_pages.printAdminTop ( &p , s , r );

	//long  page     = g_pages.getDynamicPageNumber ( r );
	char *coll     = r->getString    ( "c"    );
	//char *pwd      = r->getString    ( "pwd" );
	//char *username = g_users.getUsername( r );
	//long  user = g_pages.getUserType ( s , r );
	//if ( ! coll )  coll = "";
	if ( ! nc   )    nc = "";
	//if ( ! pwd  )   pwd = "";

	if ( g_errno ) msg = mstrerror(g_errno);

	if ( msg ) {
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

	// print the add collection box
	if ( add /*&& (! nc[0] || g_errno ) */ ) {
		p.safePrintf (
			  "<center>\n<table border=1 cellpadding=4 "
			  "width=100%% bgcolor=#%s>\n"
			   "<tr><td colspan=2 bgcolor=#%s>"
			  "<center><b>Add Collection</b></center>"
			  "</td></tr>\n",LIGHT_BLUE,DARK_BLUE);
		p.safePrintf (
			  "<tr><td><b>name of new collection to add</td>\n"
			  "<td><input type=text name=nc size=30></td></tr>\n");
		// now list collections from which to copy the config
		p.safePrintf (
			  "<tr><td><b>copy configuration from this "
			  "collection</b><br><font size=1>Leave blank to "
			  "accept default values.</font></td>\n"
			  "<td><input type=text name=cpc value=\"%s\" size=30>"
			  "</td></tr>\n",coll);
		p.safePrintf ( "</table></center><br>\n");
		// wrap up the form started by printAdminTop
		g_pages.printAdminBottom ( &p );
		long bufLen = p.length();
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
		  "<center>\n<table border=1 cellpadding=4 "
		  "width=100%% bgcolor=#%s>\n"
		  "<tr><td bgcolor=#%s><center><b>Delete Collections"
		  "</b></center></td></tr>\n"
		  "<tr><td>"
		  "<center><b>Select the collections you wish to delete. "
		  //"<font color=red>This feature is currently under "
		  //"development.</font>"
		  "</b></center></td></tr>\n"
		  "<tr><td>"
		  // table within a table
		  "<center><table width=20%%>\n",
		  LIGHT_BLUE,DARK_BLUE);

	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		p.safePrintf (
			  "<tr><td>"
			  "<input type=checkbox name=del%s value=1> "
			  "%s</td></tr>\n",cr->m_coll,cr->m_coll);
	}
	p.safePrintf( "</table></center></td></tr></table><br>\n" );
skip:
	// wrap up the form started by printAdminTop
	g_pages.printAdminBottom ( &p );
	long bufLen = p.length();
	return g_httpServer.sendDynamicPage (s,p.getBufStart(),bufLen);
}
