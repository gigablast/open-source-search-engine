#include "gb-include.h"

//#include "Checksumdb.h"
#include "Collectiondb.h"
#include "Msg22.h"
#include "Pages.h"
//#include "Links.h"
//#include "TitleRec.h" // hasAdultWords()
//#include "CollectionRec.h"
//#include "TitleRec.h" 	// containsAdultWords()
#include "XmlDoc.h"
#include "Title.h"
#include "Pos.h"
#include "SafeBuf.h"
#include "linkspam.h"
#include "CountryCode.h"
#include "Users.h"
#include "Tagdb.h"
#include "Spider.h"
//#include "DateParse2.h"

// TODO: meta redirect tag to host if hostId not ours
static bool gotTitleRec        ( void *state );

class State4 {
public:
	TcpSocket   *m_socket;
	XmlDoc       m_xd;
	bool         m_isMasterAdmin;
	bool         m_isLocal;
	int64_t    m_docId;
	char        *m_pwd;
	char        *m_coll;
	int32_t         m_collLen;
	XmlDoc       m_doc;
	Msg20Request m_request;
	HttpRequest  m_r;
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the titleRec of "docId" given via cgi
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageTitledb ( TcpSocket *s , HttpRequest *r ) {
	// get the docId from the cgi vars
	int64_t docId = r->getLongLong ("d", 0LL );
	// set up a msg22 to get the next titleRec
	State4 *st ;
	try { st = new (State4); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTitledb: new(%i): %s", 
		    (int)sizeof(State4),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State4) , "PageTitledb");
	// save the socket
	st->m_socket = s;
	// copy it
	st->m_r.copy ( r );
	// remember if http request is internal/local or not
	st->m_isMasterAdmin = g_conf.isCollAdmin ( s , r );
	st->m_isLocal = r->isLocal();
	st->m_docId   = docId;
	// password, too
	st->m_pwd = r->getString ( "pwd" );
	// get the collection
	int32_t  collLen = 0;
	char *coll    = st->m_r.getString("c",&collLen);
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
		collLen = gbstrlen(coll);
	}
	st->m_coll    = coll;
	st->m_collLen = collLen;

	// just print page if no docid provided
	if ( ! docId ) return gotTitleRec ( st );

	// get the handy XmlDoc
	XmlDoc *xd = &st->m_xd;
	// use 0 for niceness
	xd->set3 ( docId , coll , 0 );
	// callback
	xd->setCallback ( st , gotTitleRec );
	// . and tell it to load from old title rec
	// . this sets all the member vars from it and also sets
	//   m_titleRecBuf to contain the actual compressed title rec
	if ( ! xd->loadFromOldTitleRec ( ) ) return false;
	// we got it without blocking. cached?
	return gotTitleRec ( st );
}


// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool gotTitleRec ( void *state ) {
	// cast the State4 out
	State4 *st = (State4 *) state;
	// get the socket
	TcpSocket *s = st->m_socket;

	SafeBuf sb;
	// get it's docId
	int64_t docId = st->m_docId;
	// make the query string for passing to different hosts
	char  qs[64];
	sprintf(qs,"&d=%"INT64"",docId);
	if ( docId==0LL ) qs[0] = 0;
	// print standard header
	sb.reserve2x ( 32768 );
	g_pages.printAdminTop (&sb, st->m_socket, &st->m_r );
	//PAGE_TITLEDB,
	//		       st->m_username,//NULL ,
	//		       st->m_coll , st->m_pwd , s->m_ip , qs );
	// int16_tcut
	XmlDoc *xd = &st->m_xd;

	// . deal with errors
	// . print none if non title rec at or after the provided docId
	if ( g_errno || docId == 0LL || xd->m_titleRecBuf.length() <= 0 ) {
		// print docId in box
		sb.safePrintf (  "<center>\nEnter docId: "
				 "<input type=text name=d value=%"INT64" size=15>",
				 docId);
		sb.safePrintf ( "</form><br>\n" );
		if ( docId == 0 ) 
			sb.safePrintf("<br>");
		else if ( g_errno ) 
			sb.safePrintf("<br><br>Error = %s",mstrerror(g_errno));
		else 
			sb.safePrintf("<br><br>No titleRec for that docId "
				      "or higher");
		// print where it should be
		//uint32_t gid = getGroupIdFromDocId ( docId );
		//Host *hosts = g_hostdb.getGroup(gid);
		int32_t shardNum = getShardNumFromDocId ( docId );
		Host *hosts = g_hostdb.getShard ( shardNum );
		int32_t hostId = -1;
		if ( hosts ) hostId = hosts[0].m_hostId;
		sb.safePrintf("<br><br>docId on host #%"INT32" and twins.",hostId);
		sb.safePrintf ( "\n</center>" );
		mdelete ( st , sizeof(State4) , "PageTitledb");
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage ( s , 
						      sb.getBufStart(),
						      sb.length() );
	}
	// print docId in box
	sb.safePrintf ("<br>\n"
		       "<center>Enter docId: "
		       "<input type=text name=d value=%"INT64" size=15>", docId );
	// print where it should be
	//uint32_t gid = getGroupIdFromDocId ( docId );
	//Host *hosts = g_hostdb.getGroup(gid);
	int32_t shardNum = getShardNumFromDocId ( docId );
	Host *hosts = g_hostdb.getShard ( shardNum );
	int32_t hostId = -1;
	if ( hosts ) hostId = hosts[0].m_hostId;
	sb.safePrintf("<br><br>docId on host #%"INT32" and twins.",hostId);
	sb.safePrintf ( "</form><br>\n" );

	//char *coll    = st->m_coll;

	Title *ti = xd->getTitle();
	if ( ! ti ) {
		log ( "admin: Could not set title" );
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	// sanity check. should not block
	if ( ! xd->m_titleValid ) { char *xx=NULL;*xx=0; }

	// print it out
	xd->printDoc ( &sb );

	// don't forget to cleanup
	mdelete ( st , sizeof(State4) , "PageTitledb");
	delete (st);
	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), sb.length());
}
