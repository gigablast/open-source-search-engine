#include "gb-include.h"

//#include "CollectionRec.h"
#include "Pages.h"
#include "Msg2a.h"
#include "Msg8b.h"

static void sendReplyWrapper  ( void *state );
static bool sendReply         ( void *state );
static void gotCatInfoWrapper ( void *state );//, CatRec *rec );

class StateCatdb {
public:
	TcpSocket   *m_socket;
	HttpRequest  m_r;
	char         m_coll[MAX_COLL_LEN];
	int32_t         m_collLen;
	Msg2a        m_msg2a;
	Msg8b        m_msg8b;
	Url          m_url;
	CatRec       m_catRec;
	bool         m_catLookup;
	bool         m_genCatdb;
	int64_t    m_startTime;
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageCatdb ( TcpSocket *s , HttpRequest *r ) {
	// are we the admin?
	//bool isAdmin    = g_collectiondb.hasPermission ( r , s );
	// get the collection record
	CollectionRec *cr = g_collectiondb.getRec ( r );
	if ( ! cr ) {
		log("admin: No collection record found "
		    "for specified collection name. Could not add sites to "
		    "tagdb. Returning HTTP status of 500.");
		return g_httpServer.sendErrorReply ( s , 500 ,
						  "collection does not exist");
	}
	/*
	bool isAssassin = cr->isAssassin ( s->m_ip );
	if ( isAdmin ) isAssassin = true;
	// bail if permission denied
	if ( ! isAssassin && ! cr->hasPermission ( r , s ) ) {
		log("admin: Bad collection name or password. Could not add "
		    "sites to tagdb. Permission denied.");
		return sendPagexxxx ( s , r , 
						    "Collection name or "
						    "password is incorrect");
	}
	*/


	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );
	bool isCollAdmin = g_conf.isCollAdmin ( s , r );
	if ( ! isMasterAdmin &&
	     ! isCollAdmin ) {
		g_errno = ENOPERM;
		g_httpServer.sendErrorReply(s,g_errno,mstrerror(g_errno));
		return true;
	}


	// get the collection
	int32_t collLen = 0;
	char *coll   = r->getString("c", &collLen, NULL);
	// check for generate catdb command
	int32_t genCatdb = r->getLong("gencatdb", 0);
	// check for a lookup url
	int32_t urlLen = 0;
	char *url   = r->getString("caturl", &urlLen, NULL);
	// create the State
	StateCatdb *st;
	try { st = new (StateCatdb); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("catdb: Unable to allocate %"INT32" bytes for StateCatdb",
		    (int32_t)sizeof(StateCatdb) );
		return true;
	}
	mnew ( st, sizeof(StateCatdb), "PageCatdb" );
	// fill the state
	st->m_socket = s;
	st->m_r.copy(r);
	// copy collection
	if (collLen > MAX_COLL_LEN) collLen = MAX_COLL_LEN - 1;
	gbmemcpy(st->m_coll, coll, collLen);
	st->m_coll[collLen] = '\0';
	st->m_collLen = collLen;
	// defaults
	st->m_catLookup = false;
	st->m_genCatdb  = false;
	st->m_startTime = gettimeofdayInMilliseconds();
	// generate catdb if requested
	if (genCatdb == 1) {
		st->m_genCatdb = true;
		if (!st->m_msg2a.makeCatdb ( st->m_coll,
					     st->m_collLen,
					     false,
					     st,
					     sendReplyWrapper ) )
			return false;
	}
	// update catdb from .new files
	else if (genCatdb == 2) {
		st->m_genCatdb = true;
		if (!st->m_msg2a.makeCatdb ( st->m_coll,
					     st->m_collLen,
					     true,
					     st,
					     sendReplyWrapper ) )
			return false;
	}
	// lookup a url if requested
	else if (url && urlLen > 0) {
		st->m_catLookup = true;
		// set the url
		st->m_url.set(url, urlLen);
		// call msg8b to lookup in catdb
		if (!st->m_msg8b.getCatRec ( &st->m_url,
					     NULL,//st->m_coll,
					     0,//st->m_collLen,
					      true,
					      1,
					      &st->m_catRec,
					      st,
					      gotCatInfoWrapper))
					      //RDB_CATDB ) )
					      //RDB_TAGDB ) )
			return false;
	}
	// otherwise return the regular page
	return sendReply ( st );
}

void sendReplyWrapper ( void *state ) {
	sendReply ( state );
}

void gotCatInfoWrapper ( void *state ) { // , CatRec *rec ) {
	sendReply ( state );
}

bool sendReply ( void *state ) {
	StateCatdb *st = (StateCatdb*)state;
	// check for error
	if (g_errno) {
		if (st->m_catLookup)
			log("PageCatdb: Msg8b had error getting Site Rec: %s",
			    mstrerror(g_errno));
		else
			log("PageCatdb: Msg2a had error generating Catdb: %s",
			    mstrerror(g_errno));
		st->m_catLookup = false;
		g_errno = 0;
	}
	int64_t endTime = gettimeofdayInMilliseconds();
	// page buffer
	SafeBuf sb;
	sb.reserve(64*1024);
	// . print standard header
	// . do not print big links if only an assassin, just print host ids
	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r );

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );


	sb.safePrintf ( "<table %s>"
			"<tr><td colspan=2>"
			"<center><font size=+1><b>Catdb</b></font></center>"
			"</td></tr>", TABLE_STYLE );

	// instructions
	sb.safePrintf("<tr bgcolor=#%s>"
		      "<td colspan=3>"
		      "<font size=-2>"
		      "<center>"
		      "Don't just start using this, you need to follow the "
		      "instructions in the <i>admin guide</i> for adding "
		      "DMOZ support."
		      "</center>"
		      "</font>"
		      "</td>"
		      "</tr>"
		      ,DARK_BLUE
		      );

	// print the generate Catdb link
	sb.safePrintf ( "<tr class=poo><td>Update Catdb from DMOZ data.</td>"
			"<td><center>"
			"<a href=\"/admin/catdb?c=%s&gencatdb=2\">"
			"Update Catdb</a> "
			"</center></td></tr>",
			st->m_coll );
	sb.safePrintf ( "<tr class=poo>"
			"<td>Generate New Catdb from DMOZ data.</td>"
			"<td><center>"
			"<a href=\"/admin/catdb?c=%s&gencatdb=1\">"
			"Generate Catdb</a> "
			"</center></td></tr>",
			st->m_coll );
	if (st->m_genCatdb)
		sb.safePrintf ( "<tr class=poo>"
				"<td> Catdb Generation took %"INT64" ms."
				"</td></tr>",
				endTime - st->m_startTime );
	// print Url Catgory Lookup
	sb.safePrintf ( "<tr class=poo><td>Lookup Category of Url.</td>"
			"<td><input type=text name=caturl size=80"
			" value=\"");
	if (st->m_catLookup) {
		sb.safeMemcpy(st->m_url.getUrl(), st->m_url.getUrlLen());
	}
	sb.safePrintf("\"></center></td></tr>" );
	// print Url Info if Lookup was done
	if (st->m_catLookup) {
		sb.safePrintf("<tr><td>");
		// print the url
		sb.safeMemcpy(st->m_url.getUrl(), st->m_url.getUrlLen());
		sb.safePrintf(" (%"INT64" ms)</td><td>",
				endTime - st->m_startTime );
		// print each category id and path
		for (int32_t i = 0; i < st->m_catRec.m_numCatids; i++) {
			sb.safePrintf("<b>[%"INT32"] ",
					st->m_catRec.m_catids[i]);
			g_categories->printPathFromId(&sb,
					st->m_catRec.m_catids[i]);
			sb.safePrintf("</b><br>");
			// lookup title and summary
			char  title[1024];
			int32_t  titleLen = 0;
			char  summ[4096];
			int32_t  summLen = 0;
			char  anchor[256];
			unsigned char anchorLen = 0;
			g_categories->getTitleAndSummary(
					st->m_url.getUrl(),
					st->m_url.getUrlLen(),
					st->m_catRec.m_catids[i],
					title,
					&titleLen,
					1023,
					summ,
					&summLen,
					4098,
					anchor,
					&anchorLen,
					255 );
			title[titleLen] = '\0';
			summ[summLen] = '\0';
			anchor[anchorLen] = '\0';
			// print title and summary
			sb.safePrintf("<b>Title:</b> %s<br>"
					"<b>Summary:</b> %s<br>",
					title, summ);
			if (anchorLen > 0)
				sb.safePrintf("<b>Anchor:</b> %s<br>",
						anchor);
			sb.safePrintf("<br>");
		}
		sb.safePrintf("<b>Filenum:</b> %"INT32"<br>",
				st->m_catRec.m_filenum);
		// print indirect catids
		if (st->m_catRec.m_numIndCatids > 0) {
			sb.safePrintf("<hr><b>Indirect Catids [%"INT32"]:"
					"</b><br>\n",
					st->m_catRec.m_numIndCatids );
			for (int32_t i = 0;
				  i < st->m_catRec.m_numIndCatids; i++) {
				sb.safePrintf("%"UINT32"<br>",
					st->m_catRec.m_indCatids[i]);
			}
		}
		sb.safePrintf("</td></tr>");
	}
	// end it
	sb.safePrintf ( "</center></td></tr></table>" );
	// print submit button
	sb.safePrintf ( "<br><center>"
			"<input type=submit value=\"Submit\" border=0>"
			"</form></center>" );

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	// extract the socket
	TcpSocket *s = st->m_socket;
	// clear the state
	mdelete ( st, sizeof(StateCatdb), "PageCatdb" );
	delete st;
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage(s , sb.getBufStart(), sb.length());
}
