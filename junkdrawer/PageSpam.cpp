#include "gb-include.h"

#include "Pages.h"
#include "Parms.h"

#include "Collectiondb.h"


#define BABY_BLUE  "e0e0d0"
#define LIGHT_BLUE "d0d0e0"
#define DARK_BLUE  "c0c0f0"
#define GREEN      "00ff00"
#define RED        "ff0000"
#define YELLOW     "ffff00"


bool sendPageSpam ( TcpSocket *s , HttpRequest *r ) {
	int32_t  collLen = 0;
	char *coll  = r->getString ( "c" , &collLen  , NULL /*default*/);

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("build: Spam Controls Failed. "
		    "Collection \"%s\" does not exist."
		    ,coll);
		return g_httpServer.sendErrorReply(s,500,
						   "collection does not exist");
	}


// 	int32_t  user   = g_pages.getUserType ( s , r );
// 	int32_t  pwdLen = 0;
// 	char *pwd   = r->getString ( "pwd" , &pwdLen, "");

	SafeBuf sb;
	sb.reserve(32768);

	// 	char *ss = sb.getBuf();
	// 	char *ssend = sb.getBufEnd();
// 	g_pages.printAdminTop ( &sb , PAGE_SPAM, user ,
// 				coll , pwd , s->m_ip );
	//      sb.incrementLength(sss - ss);



	sb.safePrintf("\n<table width=100%% bgcolor=#%s "
		      "cellpadding=4 border=1>\n", 
		      BABY_BLUE);
	sb.safePrintf("<tr><td colspan=2 bgcolor=#%s>"
		      "<center><b>Spam Controls</b></center></td></tr>", 
		      DARK_BLUE);

	// 	ss = sb.getBuf();
	// 	ssend = sb.getBufEnd();
	g_parms.printParms (&sb, s, r);
	//	sb.incrementLength(sss - ss);



	sb.safePrintf ("<tr><td colspan=\"2\"><center>"
		       "<input type=submit value=\"Update\" border=0>"
		       "</center></td></tr>");

	sb.safePrintf ("</table><br><br>\n" );

	return g_httpServer.sendDynamicPage ( s , (char*) sb.getBufStart(), 
					      sb.length() , -1 , false);
}
