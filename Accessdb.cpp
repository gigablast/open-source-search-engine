#include "Accessdb.h"
#include "Hostdb.h"
#include "TcpSocket.h"
#include "Multicast.h"
#include "HttpServer.h"
#include "Facebook.h"

static bool printCalendars ( SafeBuf &sb , time_t startDate ) ;
static bool printCalendar ( SafeBuf &sb ,
			    int32_t showDay,
			    int32_t showMonth,
			    int32_t showYear ) ;



Accessdb g_accessdb;

static void handleRequestaa ( UdpSlot *slot , int32_t niceness ) ;

bool Accessdb::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x0A
	if ( ! g_udpServer.registerHandler ( 0xaa, handleRequestaa )) 
		return false;
	return true;
}


bool Accessdb::init ( ) {

	int32_t maxTreeMem = 3000000; // g_conf.m_accessdbMaxTreeMem;
	int32_t nodeSize = sizeof(AccessRec)+8+12+4 + sizeof(collnum_t);
	// . We take a snapshot of g_stats every minute.
	// . Each sample struct taken from g_stats ranges from 1k - 2k
	//   after compression depending on the state of the
	//   all errors arrays.
	uint32_t maxTreeNodes  = maxTreeMem / nodeSize;
	// assume low niceness
	m_niceness = 0;
	m_msg4InUse = false;
	// make the rec cache 0 bytes, cuz we are just using page cache now
	if ( ! m_rdb.init ( g_hostdb.m_dir		, // working directory
			    "accessdb"			, // dbname
			    true			, // dedup keys
			    4+8    , // fixed data size (ip+fbid)
			    2,//g_conf.m_accessdbMinFilesToMerge ,
			    maxTreeMem                  ,
			    maxTreeNodes		,
			    true			, // balance tree?
			    0                        , // m_accessdbMaxCchMem
			    0 ,// maxCacheNodes		,
			    false			, // use half keys?
			    false			, // cache from disk?
			    NULL			, // page cache pointer
			    false			, // is titledb?
			    false			, // preload cache?
			    sizeof(key128_t)		, // key size
			    false , // bias disk page cache?
			    true )) // iscollectionless? syncdb,facebookdb,...
		return false;
	// add the base since it is a collectionless rdb
	return m_rdb.addColl ( NULL );
}



// Make sure we need this function.
// main.cpp currently uses the addColl from m_rdb
bool Accessdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	return true;
}

//static Msg4 s_msg4;
//static s_msg4InUse = false;

void addedAccessRecWrapper ( void *state ) {
	g_accessdb.m_msg4InUse = false;
}

key128_t Accessdb::makeKey1 ( int64_t now, int64_t widgetId64 ) {
	key128_t ak;
	ak.n1 = now;
	ak.n0 = widgetId64;
	// make it a positive key
	ak.n0 <<= 1;
	ak.n0 |= 0x01;
	return ak;
}

key128_t Accessdb::makeKey2 ( int64_t now, int64_t widgetId64 ) {
	key128_t ak;
	// indicate its the 2nd type of key by setting high bit
	ak.n1 = widgetId64 | 0x8000000000000000LL;
	ak.n0 = now;
	// make it a positive key
	ak.n0 <<= 1;
	ak.n0 |= 0x01;
	return ak;
}

// . store two entries:
// . time64|widgetid64|ip|fbid
// . widgetid64|time64|ip|fbid
// . storing group is based on the lower bits of the time64
bool Accessdb::addAccess ( HttpRequest *hr , int32_t ip ) {

	int64_t now = gettimeofdayInMillisecondsGlobalNoCore(); 
	int64_t fbId = hr->getLongLongFromCookie("fbid",0LL);
	int64_t widgetId64 = hr->getLongLongFromCookie("widgetid",0LL);

	// not if widgetid is bogus
	if ( widgetId64 & 0x8000000000000000LL ) return true;
	// or now can't have this top bit set. we use that as indicator!
	if ( now & 0x8000000000000000LL ) { char *xx=NULL;*xx=0; }

	// log if this is a problem! we need to know it so we can fix
	if ( m_msg4InUse ) {
		log("access: msg4 is in use!!!!");
		return true;
	}

	// do not keep this on the stack in case the msg4 blocks!
	//AccessRec arec[2];
	m_arec[0].m_key128 = g_accessdb.makeKey1(now,widgetId64);
	m_arec[0].m_ip     = ip;
	m_arec[0].m_fbId   = fbId;
	m_arec[1].m_key128 = g_accessdb.makeKey2(now,widgetId64);
	m_arec[1].m_ip     = ip;
	m_arec[1].m_fbId   = fbId;
	//log("msg4: timestamp = %"UINT64"",now);
	// this has like a 1 second delay before it flushes so you might
	// not see you current access until that passes
	if ( ! m_msg4InUse &&
	     ! m_msg4.addMetaList ( (char *)&m_arec[0] ,
				    (int32_t)2*sizeof(AccessRec),
				    (collnum_t)0, // collnum
				    NULL, // state
				    addedAccessRecWrapper,
				    0, // niceness
				    RDB_ACCESSDB ) )
		// if this blocked, mark it as in use
		m_msg4InUse = true;
	return true;
}

class MsgaaRequest {
public:
	int32_t      m_startDate;
	int64_t m_widgetId;
	int32_t      m_minRecSizes;
};



class Stateaa {
public:
	int32_t m_replies;
	int32_t m_requests;
	int32_t m_errno;
	TcpSocket *m_socket;
	Msgfb m_msgfb;

	MsgaaRequest m_request;
	// for allocating usually like 32 multicasts
	int32_t m_numMulticasts;
	Multicast m_mcasts[MAX_HOSTS];
};

static void gotMulticastReplyWrapperaa ( void *state , void *state2 );

static void gotFBUserInfoWrapper ( void *state );
static bool gotFBUserInfo ( Stateaa *st ) ;

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageAccount ( TcpSocket *s , HttpRequest *r ) {

	Stateaa *st ;
	try { st = new (Stateaa); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("access: new(%i): %s", sizeof(Stateaa),mstrerror(g_errno));
		g_httpServer.sendErrorReply ( s, 500, mstrerror(g_errno) ); 
		return true; 
	}
	mnew ( st , sizeof(Stateaa) , "Stateaa" );
	// reset counts
	st->m_errno = 0;
	st->m_replies = 0;
	st->m_requests = 0;
	st->m_socket = s;

	// get startdate
	int32_t startDate = r->getLong("sd",0);
	if ( startDate == 0 ) startDate = getTimeGlobalNoCore() - 7*86400;

	// get widgetid
	int64_t widgetId = r->getLongLong("widgetid",0);
	// set request
	st->m_request.m_startDate = startDate;
	st->m_request.m_widgetId  = widgetId;
	st->m_request.m_minRecSizes = 10000;

	// first get facebook rec
	if ( ! st->m_msgfb.getFacebookUserInfo ( r ,
						 st->m_socket,
						 NULL,//coll,
						 st ,
						 "", // redirpath
						 gotFBUserInfoWrapper,
						 0 )) // niceness
		return false;
	// got it
	return gotFBUserInfo ( st );
}

void gotFBUserInfoWrapper ( void *state ) {
	Stateaa *st = (Stateaa *)state;
	if ( ! gotFBUserInfo ( st ) ) return;
	// there is no callback to call, it must have sent an http reply
}

// from PageEvents.cpp
bool printLoginScript ( SafeBuf &sb , char *redirUrl = NULL ) ;
bool printHtmlHeader ( SafeBuf &sb , char *title , bool printPrimaryDiv ,
		       SearchInput *si , bool staticPage );
bool printBlackBar(SafeBuf &sb,Msgfb *msgfb,char *page,int32_t ip,bool printLogo,
		   bool igoogle, class State7 *st );
bool printPageTitle ( SafeBuf &sb, char *title );
bool printHtmlTail ( SafeBuf *sb , Msgfb *msgfb , bool printUnsubscribedPopup);


bool gotFBUserInfo ( Stateaa *st ) {


	// if not logged into facebook...
	if ( ! st->m_msgfb.m_fbId ) {
		SafeBuf sb;
		printHtmlHeader ( sb , "My Account", 1 ,NULL,true);
		char *path = "/account.html";
		printBlackBar (sb,&st->m_msgfb, path , st->m_socket->m_ip,1,0,
			       NULL);
		printPageTitle (sb,"<nobr>My Account</nobr>");
		sb.safePrintf( "<tr>"
			       "<td width=196px></td>"
			       "<td width=%s>"
			       "<br>"
			       "<br>"
			       "<br>"
			       "<center>"
			       "<b>Login with Facebook to access "
			       "your account."
			       "</b>"
			       "<br>"
			       "<br>" 
			       "<a id=login2 "
			       "onclick=\""
			       , RESULTSWIDTHSTR
			       );
		printLoginScript ( sb );
		sb.safePrintf("\">"
			      "<img "
			      "align=center width=132 height=20 "
			      "src=/fblogin.png border=0></a>" 
			      "<br><br>"
			      "</center>"
			      "</td>"
			      "</tr>" 
			      "</table>"
			      );
		printHtmlTail ( &sb , &st->m_msgfb, false );
		TcpSocket *s = st->m_socket;
		// nuke the state now
		mdelete ( st , sizeof(Stateaa) , "Stateaa" );
		delete (st);
		int32_t bufLen = sb.length();
		// . send this page
		// . encapsulates in html header and tail
		// . make a Mime
		g_httpServer.sendDynamicPage ( s, sb.getBufStart(), bufLen );
		return true;
	}

	// if widgetid not explicitly given use facebook id of the user
	if ( st->m_request.m_widgetId == 0 )
		st->m_request.m_widgetId  = st->m_msgfb.m_fbId;

	char *request = (char *)&st->m_request;
	int32_t  requestSize = sizeof(MsgaaRequest);

	// send the request to every host in the network
	for ( int32_t i = 0 ; i < g_hostdb.getNumGroups() ; i++ ) {
		// count send outs
		st->m_requests++;
		// get group id
		uint32_t gid = g_hostdb.getGroupId ( i );
		// get the multicast for this group, int16_tcut
		Multicast *m = &st->m_mcasts[i];
		// send it out
		if ( ! m->send ( request    , 
				 requestSize, 
				 0xaa         , // msgType 0xaa
				 false        , // does multicast own request?
				 gid          , // group + offset
				 false        , // send to whole group?
				 0            , // key is passed on startKey
				 st           , // state data
				 NULL         , // state data
				 gotMulticastReplyWrapperaa ,
				 30      , // timeout in seconds (was 30)
				 0 , // niceness     ,
				 false, // realtimeudp?
				 -1 , // first host to try
				 NULL            , // reply buf
				 0 , // reply buf max size
				 true      , // free reply buf?
				 true            , // do disk load balancing?
				 0,//maxCacheAge     ,
				 0 , // *(key_t *)cacheKey        ,
				 RDB_ACCESSDB    ,
				 -1      ) ) { // minrecsizes
			log("accessdb: multicast send failed");
			st->m_errno = g_errno;
			m->reset();
			st->m_replies++;
		}
	}

	// return false if blocked
	if ( st->m_replies < st->m_requests ) return false;

	// we did not block
	gotMulticastReplyWrapperaa ( st , NULL );
	// i guess no blocking?
	return true;
}

void gotMulticastReplyWrapperaa ( void *state , void *state2 ) {

	Stateaa *st = (Stateaa *)state;
	// count it
	st->m_replies++;
	// return if awaiting more replies
	if ( st->m_replies < st->m_requests ) return;

	key128_t sk;
	key128_t ek;
	sk.setMin();
	ek.setMax();
	char *startKey = (char *)&sk;
	char *endKey   = (char *)&ek;
	int32_t ks = sizeof(key128_t);
	int32_t ds = sizeof(AccessRec) - ks;

	// ptrs to fbrecs that have your widgetid
	SafeBuf ptrBuf;

	// store page into here
	SafeBuf sb;

	// each mutlicast reply is a list
	RdbList lists[MAX_HOSTS];
	RdbList *plists[MAX_HOSTS];
	char    *m_freeMe    [MAX_HOSTS];
	int32_t     m_freeMeSize[MAX_HOSTS];

	int32_t numLists = g_hostdb.getNumGroups();
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		// get the multicast for this group, int16_tcut
		Multicast *m = &st->m_mcasts[i];
		// get the reply
		int32_t  replySize;
		int32_t  replyMaxSize;
		bool  freeReply;
		char *reply = m->getBestReply ( &replySize , 
						&replyMaxSize , 
						&freeReply );
		// save it for freeing
		if ( freeReply ) {
			m_freeMe     [i] = reply;
			m_freeMeSize [i] = replyMaxSize;
		}
		else {
			m_freeMe[i] = NULL;
		}

		// first is size of the first list
		char *p = reply;
		char *pend = p + replySize;
		int32_t firstListSize = *(int32_t *)p;
		p += 4;


		lists[i].set ( p,
			       firstListSize,
			       p,
			       firstListSize,
			       startKey,
			       endKey,
			       ds,
			       false,
			       false,
			       ks);
		plists[i] = &lists[i];

		// a facebook list follows that
		p += firstListSize;

		int32_t secondListSize = pend - p;


		// point to each facebook rec
		RdbList fblist;
		fblist.set ( p,
			     secondListSize,
			     p,
			     secondListSize,
			     startKey,
			     endKey,
			     -1, // fixeddatasize
			     false,// owndata?
			     false, // usehalfkeys?
			     sizeof(key96_t) ); // facebookdb keysize
		// scan list to add ptrs 
		for ( ; ! fblist.isExhausted() ; fblist.skipCurrentRec() ) {
			// point to it
			FBRec *fbrec = (FBRec *)fblist.getCurrentRec();
			ptrBuf.pushLong((int32_t)fbrec);
		}
	}

	// print the header from PageEvents.cpp
	printHtmlHeader ( sb , "My Account", 1 ,NULL,true);
	char *path = "/account.html";
	printBlackBar ( sb , &st->m_msgfb, path , st->m_socket->m_ip,1,0,NULL);

	printPageTitle (sb,"<nobr>My Account</nobr>");

	sb.safePrintf("<div style=width:780px;>");

	//int64_t widgetId = st->m_request.m_widgetId;

	/*
	sb.safePrintf("<br>"
		      "<div style=\"border:2px solid black\">"
		      "<table "
		      "width=100%% cellspacing=0 border=0>"
		      "<tr><td class=grad1 height=30px>" // <td width=10px>"
		      //"<img src=/eventguru.png width=64 height=64>"
		      //"</td>"
		      //"<td width=100%%>"
		      "<center>"
		      "<font color=black>"
		      "<b>"
		      "Displaying traffic data for the widget of id "
		      "<a href=http://www.facebook.com/%"UINT64">%"UINT64"</a>"
		      "</b>"
		      "</font>"
		      "</center>"
		      "</td></tr></table>"
		      "</div>"
		      "<br>"
		      , widgetId
		      , widgetId
		      );
	*/

	// widgetmaster table
	sb.safePrintf("<table border=1 width=780px; cellpadding=6 "
		      "cellspacing=3>"
		      "<tr><td colspan=10 class=grad4>"
		      "<center>"
		      "<font style=color:white;>"
		      "<b>Personal Info</b>"
		      "</font>"
		      "</center></td></tr>"
		      "<tr><td>Your Name</td>"
		      "<td>%s</td></tr>"
		      "<tr><td>Your Facebook ID</td>"
		      "<td>"
		      "<a href=\"http://www.facebook.com/%"INT64"\">%"INT64"</a>"
		      "</td></tr>"
		      "<tr><td>Your Widget ID</td>"
		      "<td>"
		      "<a href=\"http://www.facebook.com/%"INT64"\">%"INT64"</a>"
		      "</td></tr>"
		      "</table><br>"
		      ,st->m_msgfb.m_fbrecPtr->ptr_name
		      ,st->m_msgfb.m_fbId
		      ,st->m_msgfb.m_fbId
		      ,st->m_msgfb.m_fbId
		      ,st->m_msgfb.m_fbId
		      );

	// print out a calendar table of dates...
	// really you can just pick a start time and that is when we
	// start reading from.
	// but also be able to select a month...
	// maybe print 5 calendars, with the one in the middle
	// being the current month.
	printCalendars ( sb , st->m_request.m_startDate );

	// display table of facebook converts widgetmaster did
	sb.safePrintf("<table border=1 width=780px; cellpadding=6 "
		      "cellspacing=3>"
		      "<tr><td colspan=10 class=grad4>"
		      "<center>"
		      "<font style=color:white;>"
		      "<b>Event Guru Logins</b>"
		      "</font>"
		      "</center></td></tr>"
		      "<tr><td><nobr>Login Time (UTC)</nobr></td>"
		      "<td><nobr>Last Login IP</nobr></td>"
		      "<td>Country</td>"
		      "<td>Facebook ID</td>"
		      "<td>Widget ID</td>"
		      "<td><nobr>Payout*</nobr></td>"
		      "</tr>" );
	FBRec **ppp = (FBRec **)ptrBuf.getBufStart();
	int32_t n = ptrBuf.length() / 4;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		FBRec *fbrec = ppp[i];
		// skip if its you! you can't visit your own widget...
		if ( fbrec->m_fbId == fbrec->m_originatingWidgetId ) continue;
		struct tm *timeStruct = gmtime ( &fbrec->m_firstFacebookLogin);
		char time[256];
		strftime ( time , 256 , "%b %e %T %Y", timeStruct );
		float cash = 0.00;
		cash = 1.00;
		sb.safePrintf("<tr>"
			      "<td><nobr>%s</nobr></td>"
			      "<td>%s</td>"
			      "<td>%s</td>"
			      "<td><a href=http://www.facebook.com/%"UINT64">"
			      "%"UINT64"</a></td>"
			      "<td>%"UINT64"</td>"
			      "<td>$%.02f</td>"
			      "</tr>"
			      ,time // fbrec->m_firstFacebookLogin
			      ,iptoa(fbrec->m_lastLoginIP)
			      ,"US"
			      ,fbrec->m_fbId
			      ,fbrec->m_fbId
			      ,fbrec->m_originatingWidgetId
			      ,cash
			      );
	}
	sb.safePrintf("</table><br>");

	sb.safePrintf("</div>");

	int32_t minRecSizes = st->m_request.m_minRecSizes;

	// merge them together into a single list
	RdbList final;
	// owndata must be true for merge_r to work!
	final.set ( NULL,0,NULL,0,startKey,endKey,ds,true,false,ks);
	final.prepareForMerge( plists,numLists,minRecSizes);
	final.merge_r ( plists , 
			numLists ,
			startKey,
			endKey ,
			10000 ,
			true , // remove neg?
			RDB_ACCESSDB,
			NULL,
			NULL,
			NULL,
			false,
			0 ); // niceness
	
	//
	// print out first 100
	//

	// table headers
	sb.safePrintf ( "<table cellpadding=4 width=780px border=1>"
			"<tr><td colspan=10 class=grad4>"

			"<center>"
			"<font style=color:white;>"
			"<b>Event Guru Hits</b>"
			"</font>"
			"</center>"

			"</td></tr>"
			"<tr>"
			"<td><nobr>User Access Time (UTC)</nobr></td>"
			"<td>User IP</td>"
			"<td>User Facebook ID</td>"
			"<td>Widget ID</td>"
			"</tr>"
			);

	final.resetListPtr();
	for ( ;  ! final.isExhausted() ; final.skipCurrentRecord() ) {
		char *rec = final.getCurrentRec();
		AccessRec *ar = (AccessRec *)rec;
		uint64_t timestamp = ar->m_key128.n1;
		uint64_t widgetId = ar->m_key128.n0;
		// overrun the delbit
		widgetId >>= 1;
		// swap them? we do two different types of lookups.
		// if widgetid is 1, which means *any* then the starttime
		// leads, otherwise the widgetid leads.
		if ( timestamp & 0x8000000000000000LL ) {
			timestamp &= 0x7fffffffffffffffLL;
			int64_t tmp = widgetId;
			widgetId = timestamp;
			timestamp = tmp;
		}
		int32_t timestamp32 = timestamp / 1000;
		struct tm *timeStruct = gmtime ( &timestamp32 );
		char time[256];
		strftime ( time , 256 , "%b %e %T %Y", timeStruct );
		sb.safePrintf("<tr>"
			      //"<td>%"UINT64"</td>"
			      "<td><nobr>%s</nobr></td>"
			      "<td>%s</td>"
			      "<td>"
			      , time
			      , iptoa(ar->m_ip)
			      );
		if ( ar->m_fbId )
			sb.safePrintf("<a href=http://www.facebook."
				      "com/%"UINT64">%"UINT64""
				      "</a>"
				      , ar->m_fbId
				      , ar->m_fbId
				      );
		else
			sb.safePrintf("%"UINT64"" , ar->m_fbId );

		sb.safePrintf ( "</td>"
				"<td>%"UINT64"</td>"
				"</tr>"
				, widgetId
				);
	}

	// end table
	sb.safePrintf ( "</table><br>" );

	printHtmlTail ( &sb , &st->m_msgfb, false );
	
	// free the multicast replies here
	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		if ( ! m_freeMe[i] ) continue;
		mfree ( m_freeMe[i] ,m_freeMeSize[i],"acmcrep");
	}

	// make a cookie in case they just logged in through this page!
	// otherwise the login doesn't "stick"
	SafeBuf cb;
	if ( st->m_msgfb.m_fbId )
		cb.safePrintf("Set-Cookie: fbid=%"UINT64";\r\n",
			      st->m_msgfb.m_fbId);
	char *cookiePtr = NULL;
	if ( cb.length() ) cookiePtr = cb.getBufStart();
	

	TcpSocket *s = st->m_socket;

	// nuke the state now
	mdelete ( st , sizeof(Stateaa) , "Stateaa" );
	delete (st);

	int32_t bufLen = sb.length();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	g_httpServer.sendDynamicPage ( s, sb.getBufStart(), bufLen ,
				       0 , // cachetime in secs
				       false , // postreply?
				       "text/html", // content type
				       -1, // http status -1->200
				       cookiePtr,
				       "utf-8" );
}

/////////////////////////////////////////
//
// HANDLER FUNCTION
//
/////////////////////////////////////////

class Stateab {
public:
	int32_t m_startDate;
	int64_t m_widgetId;
	int32_t m_minRecSizes;
	UdpSlot *m_slot;
	int32_t m_niceness;

	Msg5 m_msg5;
	RdbList m_accessList;
	RdbList m_facebookList;
	key96_t m_fbstartKey;
	// store facebook recs in here that match widgetid
	SafeBuf m_retBuf;
};

static void gotAccessListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) ;


void handleRequestaa  ( UdpSlot *slot , int32_t niceness ) {

	Stateab *st ;
	try { st = new (Stateab); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("Stateab: new(%i): %s",sizeof(Stateab),mstrerror(g_errno));
		g_udpServer.sendErrorReply ( slot, g_errno ); 
		return;
	}
	mnew ( st , sizeof(Stateab) , "Stateab" );

	// . read in beginning at start key using msg5.
	// . limit to provided widget id
	MsgaaRequest *req  = (MsgaaRequest *)slot->m_readBuf;

	// save widgetid, startdate, minrecsizes, etc.
	st->m_startDate   = req->m_startDate;
	st->m_widgetId    = req->m_widgetId;
	st->m_minRecSizes = req->m_minRecSizes;
	st->m_slot        = slot;
	st->m_niceness    = niceness;

	// int16_tcut
	int64_t wgid = req->m_widgetId;
	// convert into milliseconds
	int64_t timestamp = ((int64_t)req->m_startDate) * 1000;
	// back 7 days from now if this is zero
	if ( req->m_startDate == 0 ) {
		timestamp = gettimeofdayInMillisecondsGlobalNoCore(); 
		// back 7 days from now
		timestamp -= 7*86400*1000;
	}

	int64_t longTime = 86400LL*365LL*1000LL*10; // 10 years in millisecs
	// use the 2nd type of key... those have widgetid first and then
	// the timestamp
	key128_t startKey  = g_accessdb.makeKey2 ( timestamp ,wgid );
	key128_t endKey    = g_accessdb.makeKey2 ( timestamp + longTime, wgid);
	// widget id of 0 means ANY widget
	if ( wgid == 0 ) {
		startKey = g_accessdb.makeKey1(timestamp , 0 );
		endKey   = g_accessdb.makeKey1(timestamp + longTime, 0 );
	}
	// lookup accessdb records from that time going forward
	if ( ! st->m_msg5.getList ( RDB_ACCESSDB ,
				    "", // m_coll          ,
				    &st->m_accessList ,
				    &startKey      ,
				    &endKey        ,
				    st->m_minRecSizes   ,
				    true          , // includeTree
				    false         , // add to cache?
				    0             , // max cache age
				    0             , // startFileNum  ,
				    -1            , // numFiles      ,
				    st           , // state
				    gotAccessListWrapper , // callback
				    st->m_niceness ,
				    false         )) // err correction?
		return;
	// we got it without blocking
	gotAccessListWrapper ( st , NULL, NULL );
}

static bool scanLoop ( Stateab *st ) ;

static void sendListsBack ( Stateab *state );


void gotAccessListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) {

	Stateab *st = (Stateab *)state;

	// store size of this list since we will be adding the
	// facebook into m_retBuf right after. (firstListSize)
	st->m_retBuf.pushLong ( st->m_accessList.m_listSize );
	// now store list from what we read into retBuf
	st->m_retBuf.safeMemcpy ( st->m_accessList.getList() ,
				  st->m_accessList.m_listSize );
				  
	// reset start key for scan
	st->m_fbstartKey.setMin();
	// . returns false if blocked, true otherwise
	// . scan ALL facebookdb for recs originating from this widgetid
	if ( ! scanLoop( st ) ) return;
	// it didn't block
	sendListsBack ( st );
}


static void gotScanListWrapper ( void *state, RdbList *list , Msg5 *msg5 ) ;
static void gotScanList ( Stateab *st ) ;

// . scan facebookdb and get every facebookid, and couple it with the
//   time we gotta send the email
// . sort by that in emailTree
// . re-scan facebookdb every few hours in case of new entries or if
//   someone updates their email
// . i would also call addToEmailTree if a new facebookdb rec comes in.
//   perhaps do that from Rdb.cpp?
// . returns false if blocked true otherwise
bool scanLoop ( Stateab *st ) {

	key96_t endKey   ;
	endKey.setMax();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	key96_t oldk; oldk.setMin();

 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! st->m_msg5.getList ( RDB_FACEBOOKDB    ,
				    "", // m_coll          ,
				    &st->m_facebookList ,
				    &st->m_fbstartKey      ,
				    &endKey        ,
				    minRecSizes   ,
				    true          , // includeTree
				    false         , // add to cache?
				    0             , // max cache age
				    0             , // startFileNum  ,
				    -1            , // numFiles      ,
				    st            , // state
				    gotScanListWrapper , // callback
				    st->m_niceness ,
				    false         )) // err correction?
		// return false if we blocked
		return false;
	// stuff the m_emailTree with some data based on m_list
	gotScanList ( st );
	// if something, get more
	if ( ! st->m_facebookList.isEmpty() ) goto loop;
	// i guess we did not block?
	return true;
}

void gotScanListWrapper ( void *state, RdbList *list , Msg5 *msg5 ) {
	// use this
	Stateab *st = (Stateab *)state;
	// this never blocks
	gotScanList ( st );
	// and resume the loop. return if it blocked.
	if ( ! scanLoop ( st ) ) return;
	// alldone
	sendListsBack ( st );
}


void gotScanList ( Stateab *st ) {

	//int32_t now = getTimeGlobal();
	//int32_t dayStart = now  - ( now % 86400 );

	if ( st->m_facebookList.isEmpty() ) return;

	// loop over entries in list
	for ( st->m_facebookList.resetListPtr() ; 
	      ! st->m_facebookList.isExhausted() ;
	      st->m_facebookList.skipCurrentRecord() ) {
		// get it
		char *drec = st->m_facebookList.getCurrentRec();
		int32_t drecSize = st->m_facebookList.getCurrentRecSize();
		// sanity check. delete key?
		if ( (drec[0] & 0x01) == 0x00 ) continue;
		// get widgetit
		FBRec *fr = (FBRec *)drec;
		// but allow widget id of 0 through if its set to 1
		int64_t wgid = fr->m_originatingWidgetId;
		if ( wgid == 0 ) wgid = 1;
		// a zero widgetid means any! a one means came from no widget.
		if ( st->m_widgetId && wgid != st->m_widgetId ) continue;
		// copy over to same list
		st->m_retBuf.safeMemcpy ( drec , drecSize );
	}
	st->m_fbstartKey = *(key96_t *)st->m_facebookList.getLastKey();
	st->m_fbstartKey += (uint32_t) 1;
	// watch out for wrap around
	//if ( startKey < *(key96_t *)list.getLastKey() ) return;
}



void sendListsBack ( Stateab *st ) {

	char *data = st->m_retBuf.getBufStart();
	int32_t  dataSize = st->m_retBuf.length();
	int32_t  allocSize = st->m_retBuf.getCapacity();

	// release it so udpserver can free it
	st->m_retBuf.detachBuf();

	// save slot
	UdpSlot *slot = st->m_slot;

	// nuke the state now
	mdelete ( st , sizeof(Stateab) , "stateab" );
	delete (st);
	
	g_udpServer.sendReply_ass ( data            ,
				    dataSize        ,
				    data            ,
				    allocSize       ,
				    slot            ,
				    60              ,
				    NULL            ,
				    NULL , // doneSending_ass ,
				    -1              ,
				    -1              ,
				    true            );
}

///////////
//
// the calendar for the page accessdb
//
//////////

// . print 5 calendars in a row, with current one in the middle
// . all are in UTC
bool printCalendars ( SafeBuf &sb , int32_t startDate ) {
	// parse it up
	int32_t now = startDate; // getTimeGlobalNoCore();
	struct tm *timeStruct = gmtime ( &now );

	// get month number (0 to 11)
	int32_t thisMonth = timeStruct->tm_mon; // 0-11
	int32_t thisDay   = timeStruct->tm_mday;
	int32_t thisYear  = timeStruct->tm_year + 1900;

	int32_t prevMonth1;
	int32_t prevYear1;
	int32_t postMonth1;
	int32_t postYear1;

	prevMonth1 = thisMonth - 1;
	prevYear1  = thisYear;
	if ( prevMonth1 < 0 ) {
		prevMonth1 += 12;
		prevYear1--;
	}

	postMonth1 = thisMonth + 1;
	postYear1  = thisYear;
	if ( postMonth1 >= 12 ) {
		postMonth1 -= 12;
		prevYear1++;
	}

	sb.safePrintf("<table border=0 width=780px; cellpadding=6 "
		      "cellspacing=3 class=grad1>"
		      "<tr><td colspan=10 class=grad4>"
		      "<center>"
		      "<font style=color:white;>"
		      "<b>Select a Date</b>"
		      "</font>"
		      "</center></td></tr>"
		      );

	sb.safePrintf("<tr><td>" );

	printCalendar ( sb , 0 , prevMonth1 , prevYear1 );

	sb.safePrintf("</td><td>");

	printCalendar ( sb , thisDay , thisMonth , thisYear );

	sb.safePrintf("</td><td>");

	printCalendar ( sb , 0 , postMonth1 , postYear1 );

	sb.safePrintf("</td></tr></table><br>");

	return true;
}

// "now" is from the msg40 we used
bool printCalendar ( SafeBuf &sb ,
		     int32_t showDay,
		     int32_t showMonth,
		     int32_t showYear ) {


	// compute show dow
	struct tm tss;
	tss.tm_mon  = showMonth;
	tss.tm_mday = 1;
	tss.tm_sec = 0;
	tss.tm_min = 0;
	tss.tm_hour = 0;
	tss.tm_year = showYear - 1900;
	time_t mt = mktime ( &tss );
	// now get dow of the first day of this month
	struct tm *tt = gmtime ( &mt );
	int32_t firstDayOfWeek = tt->tm_wday; // 0-6
	// this is that day
	time_t startDate = mt;


	// get today's month/day/year
	int32_t now = getTimeGlobalNoCore();
	struct tm *nt = gmtime ( &now );
	int32_t nowDay   = nt->tm_mday;
	int32_t nowMonth = nt->tm_mon;
	int32_t nowYear  = nt->tm_year + 1900;

	
	// we got days per month. leap year?
	int32_t daysInMonth = getNumDaysInMonth ( showMonth , showYear );

	// prev month abbr
	char *nextStr, *str, *prevStr;
	if ( showMonth == 0  ) { prevStr = "Dec"; str="Jan"; nextStr = "Feb"; }
	if ( showMonth == 1  ) { prevStr = "Jan"; str="Feb"; nextStr = "Mar"; }
	if ( showMonth == 2  ) { prevStr = "Feb"; str="Mar"; nextStr = "Apr"; }
	if ( showMonth == 3  ) { prevStr = "Mar"; str="Apr"; nextStr = "May"; }
	if ( showMonth == 4  ) { prevStr = "Apr"; str="May"; nextStr = "Jun"; }
	if ( showMonth == 5  ) { prevStr = "May"; str="Jun"; nextStr = "Jul"; }
	if ( showMonth == 6  ) { prevStr = "Jun"; str="Jul"; nextStr = "Aug"; }
	if ( showMonth == 7  ) { prevStr = "Jul"; str="Aug"; nextStr = "Sep"; }
	if ( showMonth == 8  ) { prevStr = "Aug"; str="Sep"; nextStr = "Oct"; }
	if ( showMonth == 9  ) { prevStr = "Sep"; str="Oct"; nextStr = "Nov"; }
	if ( showMonth == 10 ) { prevStr = "Oct"; str="Nov"; nextStr = "Dec"; }
	if ( showMonth == 11 ) { prevStr = "Nov"; str="Dec"; nextStr = "Jan"; }

	int32_t prevYear  = showYear;
	int32_t prevMonth = showMonth - 1;
	if ( prevMonth < 0 ) {
		prevMonth = 11;
		prevYear--;
	}
	int32_t nextYear = showYear;
	int32_t nextMonth = showMonth + 1;
	if ( nextMonth >= 12 ) {
		nextMonth = 0;
		nextYear++;
	}

	// print print out calendar header
	sb.safePrintf("<table cellspacing=0 cellpadding=3>"
		      "<tr>"
		      "<td>"
		      //"<font size=-2>"
		      //"<a "
		      //"style=\"color:black\" "
		      //"href=/traffic?displayyear=%"INT32"&displaymonth=%"INT32">%s</a>"
		      //"</font>"
		      "</td>"
		      "<td colspan=5><center>%s %"INT32"</center></td>"
		      "<td>"
		      //"<font size=-2>"
		      //"<a "
		      //"style=\"color:black\" "
		      //"href=/traffic?displayyear=%"INT32"&displaymonth=%"INT32">%s</a>"
		      //"</font>"
		      "</td>"
		      "</tr>\n"
		      "<tr>"
		      "<td>S</td>"
		      "<td>M</td>"
		      "<td>T</td>"
		      "<td>W</td>"
		      "<td>T</td>"
		      "<td>F</td>"
		      "<td>S</td>"
		      "</tr>\n"
		      // cal now for prev month
		      //, prevYear
		      //, prevMonth
		      //, prevStr
		      , str 
		      , showYear
		      // cal now for next month
		      //, nextYear
		      //, nextMonth
		      //, nextStr 
		      );
	bool printed = false;
	int32_t count = 1;

	bool showCursor = true;

	// print out days of the week header
	for ( int32_t i = 0 ; i < 35 ; i++ ) {
		if ( i % 7 == 0 )
			sb.safePrintf("<tr>");
		// is it today?
		if ( count == nowDay && 
		     showMonth == nowMonth &&
		     showYear == nowYear &&
		     i>= firstDayOfWeek && 
		     showCursor )
			sb.safePrintf("<td class=cal "
				      "style=background-color:yellow;"
				      "color:black");
		else if ( count == showDay && 
			  //clockMonth == showMonth && 
			  //clockYear == showYear &&
			  i>= firstDayOfWeek )
			sb.safePrintf("<td class=cal "
				      "style=background-color:red");
		else
			sb.safePrintf("<td class=cal");
		// do not start printing until first day of month
		if ( (i >= firstDayOfWeek || printed) &&
		     count <= daysInMonth ) {
			printed = true;
			//lo clockSet2=getYearMonthStart(showYear,showMonth+1);
			// add day to it
			//clockSet2 += (count-1) * 86400;
			// clockSet is in utc...
			//clockSet2 -= timeZoneOffset * 3600;
			// end the <td>
			/*
			sb.safePrintf(" onclick=\""
				      // set hidden tag clockset val
				      // set all to gray if not yellow
				      // set clicked to red if not yellow
				      "top.window.href='/traffic?sd=%"UINT32"';\">"
				      , startDate + (count-1)*86400
				      );
			*/
			sb.safePrintf("><a href=/account.html?sd=%"UINT32">%"INT32"</a>"
				      "</td>" 
				      , startDate + (count-1)*86400
				      , count );
			count++;
		}
		else 
			sb.safePrintf("></td>");
		if ( (i+1) % 7 == 0 )
			sb.safePrintf("</tr>\n");
	}
	sb.safePrintf("</table>");
	return true;
}
