#include "gb-include.h"

#include "HttpServer.h"
#include "Msg0.h"
#include "Msg1.h"
#include "IndexList.h"
#include "Msg20.h"
#include "Collectiondb.h"
#include "Hostdb.h"
#include "Conf.h"
#include "Pages.h"
#include "Msg40.h"
#include "SearchInput.h"
#include "PageReindex.h"
#include "PageInject.h"
#include "Spider.h"
#include "sort.h"
#include "PageTurk.h"
#include "fctypes.h"
#include "Repair.h"

// BASE PRICE BASE PAY
#define ERRORBASEPAY (.02*2.0*1.0)
#define TITLEBASEPAY (.01*2.0*1.0)
#define VENUEBASEPAY (.01*2.0*1.0)
#define DESCRBASEPAY (.01*2.0*1.0)

#define TURKNICE 0

///////////////////////////////////
//
// TURK USER VOTING STATS PAGE
//
///////////////////////////////////

class State60 {
public:
	SafeBuf    m_sb;
	SafeBuf    m_tagBuf;
	RdbList    m_tagdbList;
	Msg0       m_msg0;
	RdbList    m_banTagList;
	int32_t       m_ban;
	bool       m_isSuperTurk;
	char       m_turkUser[256];
	// hash64n() of m_turkUser:
	int64_t  m_callertuid64;
	//char       m_turkUserEncoded[512];
	char       m_showTurkUser[256];
	int64_t  m_tuid64;
	int32_t       m_turkIp;
	int32_t       m_date1;
	int32_t       m_date2;
	//char       m_isMasterAdmin;
	char       m_coll [ MAX_COLL_LEN + 1];
	int32_t       m_collLen;
	TcpSocket *m_socket;
	Url        m_url;
	Msg4       m_msg4;
	HttpRequest  m_r;
	//Msg8a      m_msg8a;
	//TagRec     m_tagRec;
	time_t     m_t1;
	time_t     m_t2;
	int32_t       m_useXml;
	char       m_mdy1[16];
	char       m_mdy2[16];
};

// should be in format like "12/31/11" 
time_t getTimet ( char *ds ) {

	if ( ! ds ) return 0;

	char *p = ds;

	int32_t month = atol(p);

	// skip to next /
	for ( ; *p && *p != '/' ; p++ );
	if ( *p != '/' ) return 0;
	p++;

	int32_t day = atol(p);

	// skip to next /
	for ( ; *p && *p != '/' ; p++ );
	if ( *p != '/' ) return 0;
	p++;

	int32_t year = atol(p);
	if ( year < 1900 ) year += 2000;

	// make the time
	tm ts1;
	memset(&ts1, 0, sizeof(tm));
	ts1.tm_mon  = month - 1;
	ts1.tm_mday = day;
	ts1.tm_year = year - 1900;
	time_t timestamp = mktime_utc(&ts1);
	// it is localtime, make utc
	//timestamp += timezone * 3600;
	return timestamp;
}

static bool sendPageTurkStats2 ( State60 *st ) ;
static void gotTagList ( void *state ) ;
static void addedMetaListWrapper ( void *state ) ;
static bool addedMetaList ( State60 *st ) ;
static bool initBannedTable ( State60 *st ) ;
static bool banTableReady ( State60 *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageTurkStats ( TcpSocket *s , HttpRequest *r ) {

	if ( g_hostdb.m_myHost->m_hostId != 0 ) {
		log("turk: must be called on host #0 only!");
		g_errno = EBADENGINEER;
		return g_httpServer.sendErrorReply(s,500,"not host #0!");
	}

	char *turkUser = r->getString("evaluser",NULL);
	if ( ! turkUser ) {
		log("turk: must be called with turkuser=...");
		g_errno = EBADENGINEER;
		return g_httpServer.sendErrorReply(s,500,"turkuser required");
	}


	// make a state
	State60 *st ;
	try { st = new (State60); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTagdb: new(%i): %s", 
		    sizeof(State60),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State60) , "st60" );

	// assume no error
	g_errno = 0;

	st->m_r.copy ( r );
	// do not let original free the junk, m_r will do that
	st->m_r.m_cgiBuf2 = NULL;

	// get collection name and its length
	CollectionRec *cr = g_collectiondb.getRec ( r );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("turk: bad coll");
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}

	char *coll    = cr->m_coll;
	int32_t  collLen = gbstrlen ( coll );
	gbmemcpy ( st->m_coll , coll , collLen );
	st->m_coll [ collLen ] = '\0';
	st->m_collLen=collLen;

	// save socket for retuning a page when we're done
	st->m_socket = s;

	// for partap so we can see how much we owe the turk
	st->m_useXml = r->getLong("xml",0);

	// did we get a turk user? should be there!
	strncpy ( st->m_turkUser , turkUser , 250 );

	// and store turk ip as well!
	char *tuip = r->getString("evalip",NULL);
	if ( tuip ) st->m_turkIp = atoip(tuip);
	else        st->m_turkIp = 0;

	// what turkuser do we want to show or display?
	char *showTurkUser = r->getString("showevaluser",NULL);
	st->m_showTurkUser[0] = '\0';
	if ( showTurkUser ) strncpy ( st->m_showTurkUser,showTurkUser,250);


	// get date stuff
	char *ds1 = r->getString("mdy1",NULL);
	char *ds2 = r->getString("mdy2",NULL);

	// save
	st->m_mdy1[0] = '\0';
	st->m_mdy2[0] = '\0';
	if ( ds1 ) strncpy(st->m_mdy1,ds1,15);
	if ( ds2 ) strncpy(st->m_mdy2,ds2,15);

	// should be in format like "12/31/11" 
	st->m_t1 = getTimet ( ds1 );
	st->m_t2 = getTimet ( ds2 );
	// make it right up until the next day!
	st->m_t2 += 24*3600 - 1;

	if ( ! ds1 || ds1[0]=='\0' ) st->m_t1 = 0;
	if ( ! ds2 || ds2[0]=='\0' ) st->m_t2 = 0x7fffffff;

	st->m_isSuperTurk = isSuperTurk ( turkUser );

	st->m_callertuid64 = hash64n( turkUser );

	st->m_ban = r->getLong("ban"  ,-1);
	// must be super turk
	if ( ! st->m_isSuperTurk ) st->m_ban = -1;

	// . before doing anything, initialze the table of banned turk
	//   usernames or turkips
	// . returns false if it blocks, true otherwise
	return initBannedTable ( st );
}

// maps the 64 bit hash of a turkusername or turkip (string) to yes|no
static HashTableX s_banTable;
// store all the tags banning someone in here
//static SafeBuf    s_banBuf;

static void gotBanTagListWrapper ( void *state ) ;
static bool gotBanTagList ( State60 *st );

// . return true and set g_errno on error
// . return false if blocked
bool initBannedTable ( State60 *st ) {

	// only init once!
	static bool s_init = false;
	// continue processing if table is already set to go
	if ( s_init ) return banTableReady ( st );
	// only do this once
	s_init = true;

	if ( ! s_banTable.set ( 8,0,64,NULL,0,false,0,"tbantbl") )
		return true;

	// read the entire list of turkuser tags from tagdb
	// to see what turkusernames and turkips are banned/unbanned

	char fakeUrl[1024];
	sprintf(fakeUrl,"gbturkuser.com");
	Url u;
	u.set ( fakeUrl );
	key128_t sk = g_tagdb.makeStartKey ( &u );
	key128_t ek = g_tagdb.makeEndKey ( &u );

	// int16_tcut
	Msg0 *m = &st->m_msg0;
	// get the list
	if ( ! m->getList ( -1               , // hostId
			    0                , // ip
			    0                , // port
			    0                , // maxCacheAge
			    false            , // addToCache
			    RDB_TURKDB       ,
			    st->m_coll       ,
			    &st->m_banTagList,
			    (char *)&sk      ,
			    (char *)&ek      ,
			    5000000          , // minRecSizes
			    st               ,
			    gotBanTagListWrapper ,
			    TURKNICE             )) // niceness
		// return false if blocks
		return false;
	// no waiting
	return gotBanTagList ( st );
}

void gotBanTagListWrapper ( void *state ) {
	 State60 *st = (State60 *)state;
	 gotBanTagList( st );
}

bool gotBanTagList ( State60 *st ) {
	// note it
	TcpSocket *sock = st->m_socket;
	// error?
	if ( g_errno ) {
	haderror:
		g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
	}		

	// int16_tcut
	RdbList *list = &st->m_banTagList;

	//int32_t turkuser = getTagTypeFromStr("turkuser");

	// scan the list
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// assume no tag, assume end of list
		Tag *tag = (Tag *)list->getCurrentRec();
		// get tag user
		char *data  = tag->getTagData();
		int32_t  dsize = tag->getTagDataSize();
		// sanity -- we got type 123456 sometimes!!! wtf?
		//if ( tag->m_type != turkuser ) {char*xx=NULL;*xx=0;}
		// skip if unbanned
		if ( data[dsize-2] =='0' ) continue;
		// must be banned!
		if ( data[dsize-2] !='1' ) continue;//{char*xx=NULL;*xx=0;}
		// must be comma
		if ( data[dsize-3] !=',' ) continue;//{char*xx=NULL;*xx=0;}
		// null term it
		data[dsize-3] = '\0';
		// hash the turkip or turkusername that is banned
		int64_t key64 = hash64n(data);
		// add to table now. return true w/ g_errno set on error
		if ( ! s_banTable.addKey ( &key64 ) ) goto haderror;
	}

	return banTableReady ( st );
}

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool banTableReady ( State60 *st ) {

	// error?
	if ( g_errno ) {
	hadError:
		log("turk: stats page: %s",mstrerror(g_errno));
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
		return g_httpServer.sendErrorReply(st->m_socket,500,
						   mstrerror(g_errno));
	}

	////////
	//
	// if not banning/unbanning, just print page
	//
	////////
	if ( st->m_ban == -1 )
		return sendPageTurkStats2 ( st );


	// id -- might need to write this... if it is basically -1 unsigned
	// it would mess up getLongLong()...
	// getLongLong() can't handle unsignededness...
	// TODO: use strtoull to implement this... or make atoull() for us
	// wrapping that...
	char *banTurkUser = st->m_r.getString("banevaluser",NULL);
	// maybe the ip was passed in
	char *banTurkIp = st->m_r.getString("banevalip",NULL);
	// make a 64 bit key of what we want to ban/unban
	int64_t  key64 = 0LL;
	char      *data = NULL;
	// use ip if that not there
	if ( banTurkUser ) {
		key64 = hash64n(banTurkUser);
		data  = banTurkUser;
	}
	else if ( banTurkIp ) {
		key64 = (int64_t)atoip(banTurkIp);
		data  = banTurkIp;
	}
	else {
		log("turk: not provided with something to ban");
		g_errno = EBADENGINEER;
		goto hadError;
	}
	// add the tag then (overwrite?)
	SafeBuf *tbuf = &st->m_tagBuf;
	// . make the fake url
	// . %"UINT64".gbturkuser.com
	// . %"UINT64" could be the ip as a 32 bit int32_t or it could be
	//   a 64 bit hash of the turk user name
	char fakebanurl[256];
	sprintf(fakebanurl,"gbturkuser.com/%"UINT64"",key64);
	// use this timesampe
	int32_t now = getTimeGlobal();
	// must be 0 or 1
	if ( st->m_ban != 0 && st->m_ban != 1 ) { char *xx=NULL;*xx=0; }

	// add it to the ban table
	if ( st->m_ban == 0 ) s_banTable.removeKey ( &key64 );
	// otherwise, add it
	if ( st->m_ban == 1 ) s_banTable.addKey ( &key64 );

	// make the data (e.g. "mwells,1"  or "1.2.3.4,1") means that
	// mwells and 1.2.3.4 are banned turkusernames/turkips
	char dbuf[1024];
	sprintf(dbuf,"%s,%"INT32"",data,st->m_ban);
	// first rdbid
	//if ( ! tbuf->pushChar ( RDB_TURKDB ) ) goto hadError;
	// add that tag to our buffer
	if ( ! tbuf->addTag ( fakebanurl ,
			      "turkuser",
			      now        ,
			      st->m_turkUser   ,
			      st->m_turkIp ,
			      dbuf,
			      gbstrlen(dbuf) ,
			      RDB_TURKDB ,
			      true ) )
		goto hadError;
	// add it to tagdb, no need to wait
	if ( ! st->m_msg4.addMetaList ( tbuf->getBufStart() ,
					tbuf->length(),
					st->m_coll ,
					st ,
					addedMetaListWrapper ,
					TURKNICE )) // niceness
		return false;
	// we added it
	return addedMetaList ( st );
}

void addedMetaListWrapper ( void *state ) {
	State60 *st = (State60 *)state;
	addedMetaList ( st );
}

bool addedMetaList ( State60 *st ) {
	// flush it and wait for that... although not implemented yet?
	//st->m_msg4.flushAll();
	// if msg4 did not block
	return sendPageTurkStats2 ( st );
}


bool printHeader ( char *turkUser , char *turkIp, char *coll , SafeBuf *sb ) {

	char turkUserEncoded[512];
	// encode
	turkUserEncoded[0] = '\0';
	// encode if we got it
	urlEncode ( turkUserEncoded , 500, turkUser , strlen(turkUser) );

	char *sup = "";
	if ( isSuperTurk(turkUser) ) sup = " <b>(superuser)</b>";

	return sb->safePrintf("<meta "
			      "http-equiv=\"Content-Type\" "
			      "content=\"text/html; charset=utf-8\">"
			      "<a name=top></a>\n"
			      "<br>"
			      "<div id=\"nav\">"
			      "<a href=/eval?c=%s&evaluser=%s&evalip=%s&>"
			      "<b>Evaluate an Event</b></a>"

			      "&nbsp; | &nbsp;"

			      "<a href=/evalstats?c=%s&evaluser=%s&evalip=%s&"
			      "showevaluser=%s>"
			      "<b>View My Stats</b></a>"

			      "&nbsp; | &nbsp;"

			      "<b><a href=/evalstats?c=%s&evaluser=%s&"
			      "evalip=%s>"
			      "View Leaderboard</a></b>"

			      "&nbsp; | &nbsp;"

			      "<b><a href=/account?c=%s&evaluser=%s&"
			      "evalip=%s>"
			      "My Account</a></b>"

			      "&nbsp; | &nbsp;"

			      "<b><a href=/evalhelp?c=%s&evaluser=%s&"
			      "evalip=%s>"
			      "Help/FAQ</a></b>"
			      "</div>\n"

			      "<br><br>"
			      "Logged in as %s.%s"
			      "<br><br>"
			      ,coll
			      ,turkUserEncoded
			      ,turkIp

			      ,coll
			      ,turkUserEncoded
			      ,turkIp
			      ,turkUserEncoded

			      ,coll
			      ,turkUserEncoded
			      ,turkIp

			      ,coll
			      ,turkUserEncoded
			      ,turkIp

			      ,coll
			      ,turkUserEncoded
			      ,turkIp

			      ,turkUserEncoded, sup );
}
/*
bool sendPageInstructions ( State60 *st ) {


	// int16_tcut
	SafeBuf *sb = &st->m_sb;

	if( ! printHeader ( st->m_turkUser,
			    iptoa(st->m_turkIp),
			    st->m_coll ,
			    sb ) ) {
	haderror:
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
		return g_httpServer.sendErrorReply(st->m_socket,500,
						   mstrerror(g_errno));
	}
	
	char *ins = 
		"<b><font size=+1>#1</font></b>. "
		"Click on the <i>Evaluate an Event</i> link to "
		"evaluate an event."
		"<br>";


	if ( ! sb->safePrintf("%s",ins) ) goto haderror;

	bool status = g_httpServer.sendDynamicPage ( st->m_socket,
						     sb->getBufStart(),
						     sb->length() + 1,
						     -1,
						     false ,
						     "text/html" );

	mdelete ( st , sizeof(State60) , "state60" );
	delete (st);
	return status;
}
*/

bool sendPageTurkStats2 ( State60 *st ) {

	char *ds1 = st->m_mdy1;
	char *ds2 = st->m_mdy2;

	if ( ! ds1[0] ) ds1 = "01/01/2000";
	if ( ! ds2[0] ) ds2 = "12/31/2029";

	// int16_tcut
	SafeBuf *sb = &st->m_sb;

	if ( ! st->m_useXml &&
	     ! printHeader ( st->m_turkUser,
			     iptoa(st->m_turkIp),
			     st->m_coll ,
			     sb ) )
		goto hadError;

	// print date range input boxes
	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<form method=GET action=/evalstats>\n"
			      "<input type=hidden name=c value=\"%s\">\n"
			      "<table>"
			      "<tr>"
			      "<td>Date range: </td>\n"
			      "<td><input type=text size=10 name=mdy1 "
			      "value=\"%s\">"
			      "<font size=-2><br><i>mm/dd/yyyy</i></font>"
			      "</td>\n"
			      "<td> to </td>\n"
			      "<td><input type=text size=10 name=mdy2 "
			      "value=\"%s\">"
			      "<font size=-2><br><i>mm/dd/yyyy</i>"
			      "</font></td>\n"
			      "<td><input type=submit name=submit value=ok>"
			      "</td>"
			      "</tr>\n"
			      "</table>\n"
			      ,st->m_coll
			      ,ds1
			      ,ds2) ) {
	hadError:
	      g_httpServer.sendErrorReply(st->m_socket,500,mstrerror(g_errno));
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
		// we did not block, return true
		return true;
	}		

	// only show hidden turkuser tag if we had one
	if ( ! st->m_useXml &&
	     st->m_turkUser[0] && 
	     ! sb->safePrintf ("<input type=hidden name=evaluser "
			       "value=\"%s\">\n", st->m_turkUser ) )
		goto hadError;

	// close up the form
	if ( ! st->m_useXml && ! sb->safePrintf("</form>\n") ) goto hadError;

	if ( st->m_useXml &&
	     ! sb->safePrintf ("<?xml version=\"1.0\" "
			       "encoding=\"UTF-8\" ?>\n" ) ) 
		goto hadError;


	if ( st->m_showTurkUser[0] ) 
		st->m_tuid64 = hash64n ( st->m_showTurkUser );
	else    
		st->m_tuid64 = 0LL;

	char fakeUrl[256];
	// . we just use domain for making startkey
	// . if searching vote stats from ALL turk users then just use
	//   the domain name without the tuid64
	if ( ! st->m_tuid64 ) sprintf(fakeUrl,"gbturkvotestat.com");
	// just scan vote stats for that user... should still be sorted
	// by Tag::m_timestamp since the tag type is "votestatbydate"
	else sprintf(fakeUrl,"%"UINT64"gbturkvotestat.com",st->m_tuid64);

	// . now read in the list of turkvotestatbydate tags
	//   and skip tags whose username does not match "turkuser"
	// . but first we must get the start and end keys
	// . this domain must match that in XmlDoc::addVoteStatTag()
	Url u;
	u.set ( fakeUrl );
	key128_t sk = g_tagdb.makeStartKey ( &u );
	key128_t ek = g_tagdb.makeEndKey ( &u );
	// getDeduphash() now complements the timestamps
	uint32_t ct1 = ~((uint32_t)st->m_t1);
	uint32_t ct2 = ~((uint32_t)st->m_t2);
	// and getDedupHash() uses tag timestamp for turkvotestatbydate tags
	// so HACK that in here
	sk.n0 = ct2;
	sk.n0 <<= 32;
	ek.n0 = ct1;
	ek.n0 <<= 32;
	ek.n0 |= 0xffffffff;
	
	// sanity
	if ( st->m_t1 > st->m_t2 ) {
		log("turk: bad time range");
		g_errno = EBADENGINEER;
		goto hadError;
	}

	// . if partap is requesting stats less than 24 hours old -- error out!
	// . get time now in UTC
	uint32_t now = getTimeGlobal();
	bool bad = true;
	if ( ! st->m_useXml ) bad = false;
	if ( (uint32_t)st->m_t2 < now - 24 *3600 ) bad = false;
	if ( bad ) {
		log("turk: requesting stats < 24 hrs old");
		// TODO: re-enable this before launch
		/*
		char *msg = "requesting stats < 24 hrs old. bad!";
		g_errno = EBADENGINEER;
		g_httpServer.sendErrorReply(st->m_socket,500,msg);
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
		return false;
		*/
	}

	// int16_tcut
	Msg0 *m = &st->m_msg0;
	// get the list
	if ( ! m->getList ( -1               , // hostId
			    0                , // ip
			    0                , // port
			    0                , // maxCacheAge
			    false            , // addToCache
			    RDB_TURKDB       ,
			    st->m_coll       ,
			    &st->m_tagdbList ,
			    (char *)&sk      ,
			    (char *)&ek      ,
			    5000000          , // minRecSizes
			    st               ,
			    gotTagList       ,
			    TURKNICE         )) // niceness
		// return false if blocks
		return false;

	// no waiting
	gotTagList ( st );
	return true;
}

static bool printAllUserStats ( SafeBuf *sb , RdbList *list , State60 *st ) ;
static bool printSingleUserStats ( SafeBuf *sb , RdbList *list,char *turkUser,
				   State60 *st);

void gotTagList ( void *state ) {
	// cast it
	State60 *st = (State60 *)state;
	// note it
	TcpSocket *sock = st->m_socket;
	// error?
	if ( g_errno ) {
	haderror:
		g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
		mdelete ( st , sizeof(State60) , "state60" );
		delete (st);
	}		

	// print the tags for this turk user
	SafeBuf *sb = &st->m_sb;

	// set this up
	char *showTurkUser = NULL;
	if ( st->m_showTurkUser[0] ) showTurkUser = st->m_showTurkUser;


	// int16_tcut
	RdbList *list = &st->m_tagdbList;


	if ( showTurkUser && 
	     ! printSingleUserStats ( sb , list , showTurkUser,st ) )
		goto haderror;
	
	if ( ! showTurkUser && ! printAllUserStats ( sb , list , st ) )
		goto haderror;

	char *ctype = "text/html";
	if ( st->m_useXml ) ctype = "text/xml";

	g_httpServer.sendDynamicPage ( st->m_socket,
				       sb->getBufStart(),
				       sb->length() ,
				       -1,
				       false ,
				       ctype );

	mdelete ( st , sizeof(State60) , "state60" );
	delete (st);
}

// key is the 64 bit hash of turk user, tuid64
class TurkUserStat {
public:

	int32_t m_errorPassed;
	int32_t m_errorFailed;
	int32_t m_errorUnconfirmed;
	int32_t m_errorNone;

	int32_t m_titlePassed;
	int32_t m_titleFailed;
	int32_t m_titleUnconfirmed;
	int32_t m_titleNone;

	int32_t m_venuePassed;
	int32_t m_venueFailed;
	int32_t m_venueUnconfirmed;
	int32_t m_venueNone;

	int32_t m_descrPassed;
	int32_t m_descrFailed;
	int32_t m_descrUnconfirmed;
	int32_t m_descrNone;

	float m_totalEarned;
	char  m_isBanned;
	char  m_isSuperTurk;
	int64_t m_tuid64;

	//int32_t m_numYoungSubmissions; // numyoungvotes
	int32_t m_turkUserOff;
	int32_t m_turkUserIps[32];
	int32_t m_numTurkUserIps;
};
	

// returns false and sets g_errno on error
bool printAllUserStats ( SafeBuf *sb , RdbList *list , State60 *st ) {
	// store turk usernames in here
	SafeBuf usb;

	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<br>"
			      "<h1>Leaderboard</h1>" ) )
		return false;

	// only count votes >= 24 hrs old for earning money
	time_t now = getTimeGlobal();

	/*
	char datebuf[128];
	// print that utc clock
	if ( ! st->m_useXml ) {
		struct tm *timeStruct = gmtime ( &now );
		strftime(datebuf,100,"%b %d %Y %H:%M:%S UTC",timeStruct);
		if ( ! sb->safePrintf("<font style=\"font-size:12px\">"
				      "Current Time: %s</font><br>\n",
				      datebuf))
			return false;
	}
	*/

	// table for turkuserstats, init to 512 slots
	HashTableX tust;
	if ( ! tust.set ( 8,sizeof(TurkUserStat),512,NULL,0,false,0,"tust") ) 
		return false;
	// set to top
	list->resetListPtr();
	// scan tags
	//int32_t turkvotestatbydate = getTagTypeFromStr("turkvotestatbydate");
	// loop over all tags in the buf, see if we got a dup
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// assume no tag, assume end of list
		Tag *tag = (Tag *)list->getCurrentRec();
		// get tag user
		char *user  = tag->getUser();
		char *data  = tag->getTagData();
		int32_t  dsize = tag->getTagDataSize();
		// was good or bad or unconfirmed? 
		//char vv = data[dsize-3]; // o->good a->bad e->unconfirmed
		// make key
		int64_t tuid64 = hash64n ( user );
		// already in table?
		TurkUserStat *tus = (TurkUserStat *)tust.getValue(&tuid64);
		// make a new one if not there
		if ( ! tus ) {
			TurkUserStat ntus;
			// reset all to 0
			memset ( &ntus , 0, sizeof(TurkUserStat) );
			// offset into usb buffer for turk username
			ntus.m_turkUserOff = usb.length();
			if ( ! tust.addKey ( &tuid64,&ntus ) ) return false;
			// point to it now
			tus = (TurkUserStat *)tust.getValue(&tuid64);
			if ( ! usb.safePrintf("%s",user) ) return false;
			if ( ! usb.pushChar('\0') ) return false;
		}
		// do not compile if < 24 hrs old
		if ( now - tag->m_timestamp < 24*3600 ) {
			//tus->m_numYoungVotes++; 
			//continue;
		}

		// add in ip
		int32_t k; for ( k = 0 ; k < tus->m_numTurkUserIps ; k++ ) {
			// breathe
			QUICKPOLL(TURKNICE);
			if ( tus->m_turkUserIps[k] == tag->m_ip ) break;
		}
		// if already have it, bail
		if ( k == tus->m_numTurkUserIps && tus->m_numTurkUserIps < 32){
			// add it otherwise
			tus->m_turkUserIps[k] = tag->m_ip;
			tus->m_numTurkUserIps++;
		}

		// add stats
		//if      ( vv=='o' ) tus->m_numGood++;
		//else if ( vv=='a' ) tus->m_numBad++;
		//else if ( vv=='e' ) tus->m_numUnconfirmed++;
		//else { char *xx=NULL;*xx=0; }

		char *vv = data+dsize-1;
		// back up to comma
		for ( ; *vv !=',' ; vv-- );
		// skip over comma
		vv++;

		// . now the data is a string like 'PPUN'
		// . Error|Title|Venue|Descr (TODO: add Image later)

		char *pv = vv;

		// "Error"
		if      ( *pv == 'F' ) tus->m_errorFailed++;
		else if ( *pv == 'P' ) tus->m_errorPassed++;
		else if ( *pv == 'U' ) tus->m_errorUnconfirmed++;
		else if ( *pv == 'N' ) tus->m_errorNone++;
		else { char *xx=NULL;*xx=0;}
		pv++;

		// "Title"
		if      ( *pv == 'F' ) tus->m_titleFailed++;
		else if ( *pv == 'P' ) tus->m_titlePassed++;
		else if ( *pv == 'U' ) tus->m_titleUnconfirmed++;
		else if ( *pv == 'N' ) tus->m_titleNone++;
		else { char *xx=NULL;*xx=0;}
		pv++;

		// "Venue"
		if      ( *pv == 'F' ) tus->m_venueFailed++;
		else if ( *pv == 'P' ) tus->m_venuePassed++;
		else if ( *pv == 'U' ) tus->m_venueUnconfirmed++;
		else if ( *pv == 'N' ) tus->m_venueNone++;
		else { char *xx=NULL;*xx=0;}
		pv++;

		// "Descr"
		if      ( *pv == 'F' ) tus->m_descrFailed++;
		else if ( *pv == 'P' ) tus->m_descrPassed++;
		else if ( *pv == 'U' ) tus->m_descrUnconfirmed++;
		else if ( *pv == 'N' ) tus->m_descrNone++;
		else { char *xx=NULL;*xx=0;}
		pv++;
		

	}

	// only show the turk ips if we are super turk for privacy reasons
	char *ipcol1 = "";
	char *ipcol2 = "";
	// do not even show ips if not super turk for privacy
	bool showIps = (bool)(st->m_isSuperTurk);
	if ( showIps ) {
		ipcol1 = "<td></td>";
		ipcol2 = "<td>IPs used</td>";
	}


	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<table border=1 cellpadding=3>\n"

			      // special top header row
			      "<tr>"
			      "<td colspan=2></td>" // turk
			      "%s"        // turkip

			      "<td colspan=4>Accept/Reject</td>"
			      "<td colspan=4>Title</td>"
			      "<td colspan=4>Venue</td>"
			      "<td colspan=4>Descriptions</td>"
			      "<td></td>"
			      "<td></td>"
			      "</tr>"



			      "<tr>"
			      "<td colspan=2>User</td>"
			      "%s"

			      "<td>Pass</td>"
			      "<td>Fail</td>"
			      "<td>TBD</td>"
			      "<td>None</td>"

			      "<td>Pass</td>"
			      "<td>Fail</td>"
			      "<td>TBD</td>"
			      "<td>None</td>"

			      "<td>Pass</td>"
			      "<td>Fail</td>"
			      "<td>TBD</td>"
			      "<td>None</td>"

			      "<td>Pass</td>"
			      "<td>Fail</td>"
			      "<td>TBD</td>"
			      "<td>None</td>"

			      //"<td>&lt;24HrsOld</td>"
			      "<td>Accuracy</td>"
			      "<td>Earned*</td>"
			      "</tr>\n"
			      ,ipcol1
			      ,ipcol2 ) ) 
		return false;

	if (   st->m_useXml &&
	       ! sb->safePrintf("<response>\n" ) )
		return false;


	// put the turk users ptrs into an array for sorting
	SafeBuf array;
	// alloc for ptrs
	if ( ! array.reserve ( 4 * tust.m_numSlotsUsed ) ) return false;
	// store ptrs
	TurkUserStat **pp = (TurkUserStat **)array.getBufStart();
	int32_t npp = 0;
	for ( int32_t i = 0 ; i < tust.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(TURKNICE);
		if ( ! tust.m_flags[i] ) continue;
		TurkUserStat *tus = (TurkUserStat *)tust.getValueFromSlot(i);
		pp[npp++] = tus;
		// calculate m_totalEarned for display
		float earned = 0.00;

		earned += ERRORBASEPAY * tus->m_errorPassed;
		earned += ERRORBASEPAY * tus->m_errorUnconfirmed;

		earned += TITLEBASEPAY * tus->m_titlePassed;
		earned += TITLEBASEPAY * tus->m_titleUnconfirmed;

		earned += VENUEBASEPAY * tus->m_venuePassed;
		earned += VENUEBASEPAY * tus->m_venueUnconfirmed;

		earned += DESCRBASEPAY * tus->m_descrPassed;
		earned += DESCRBASEPAY * tus->m_descrUnconfirmed;

		tus->m_totalEarned = earned;

		// set tuid64 of this turk
		char *tun = usb.getBufStart() + tus->m_turkUserOff;
		tus->m_tuid64 = hash64n ( tun );

		// are they superturk?
		tus->m_isSuperTurk = false;
		if ( isSuperTurk ( tun ) ) 
			tus->m_isSuperTurk = true;

		// are they banned?
		tus->m_isBanned = false;
		// see if any of their ips are banned
		for ( int32_t k = 0 ; k < tus->m_numTurkUserIps ; k++ ) {
			// breathe
			QUICKPOLL(TURKNICE);
			int32_t ip = tus->m_turkUserIps[k];
			// . if any ips are banned, user is banned!
			// . show partap 0.00 pay then
			if ( ! isTurkBanned ( NULL, ip ) ) continue;
			tus->m_isBanned = true;
			break;
		}
		// see if their username is banned
		if ( isTurkBanned ( &tus->m_tuid64, 0 ) )
			tus->m_isBanned = true;

	}


	// now bubble sort those ptrs so highest earning on top!!
	char flag = 1;
 bubbleLoop:
	flag = 0;
	for ( int32_t i = 1 ; i < npp ; i++ ) {
		if ( pp[i]->m_totalEarned <= pp[i-1]->m_totalEarned ) continue;
		// swap em
		TurkUserStat *tmp = pp[i-1];
		pp[i-1] = pp[i];
		pp[i]   = tmp;
		flag = 1;
	}
	if ( flag ) goto bubbleLoop;


	// print out the turk user stats table
	for ( int32_t i = 0 ; i < npp ; i++ ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// int16_tcut
		TurkUserStat *tus = pp[i];
		// get name
		char *tun = usb.getBufStart() + tus->m_turkUserOff;

		float earned = tus->m_totalEarned;

		// print username and earned if doing xml only
		if ( st->m_useXml ) {
			char *banStr = "";
			if ( tus->m_isBanned ) {
				banStr = "\t\t<banned>1</banned>\n";
				// . make sure partap does not pay
				// . no, he needs to know...
				//earned = 0.0;
			}
			char *supStr = "";
			if ( tus->m_isSuperTurk )
				supStr = "\t\t<superTurk>1</superTurk>\n";
			if ( ! sb->safePrintf("\t<record>\n"
					      "\t\t<turkuser>%s</turkuser>\n"
					      "\t\t<earned>$%.02f</earned>\n"
					      "%s"
					      "%s"
					      "</record>\n"
					      ,tun
					      ,earned
					      ,banStr
					      ,supStr
					      ))
				return false;
			continue;
		}

		// do not print out this user if they are banned and they
		// are not the one's requesting the leaderboard
		if ( tus->m_isBanned && 
		     st->m_callertuid64 != tus->m_tuid64 &&
		     ! st->m_isSuperTurk )
			continue;

		// nothing if accept/reject is wrong though!
		//if ( tus->m_errorFailed ) earned = 0.0;

		// print username and stat counts
		if ( ! sb->safePrintf("<tr>" ) )
			return false;

		bool targetSuper = false;
		if ( isSuperTurk ( tun ) ) targetSuper = true;

		// only print ban/unban link if they are superturk
		if ( st->m_isSuperTurk && ! targetSuper ) {
			// is the turkusername banned?
			int32_t ban = 1;
			//int64_t tuid64 = hash64n(tun);
			//Tag *btag = (Tag *)s_banTable.getValue ( &tuid64 );
			//if ( btag ) ban = 0;
			if ( tus->m_isBanned ) ban = 0;
			if ( ! sb->safePrintf("<td><a href=/evalstats?"
					      "evaluser=%s&"
					      "ban=%"INT32"&"
					      "banevaluser="
					      ,st->m_turkUser
					      ,ban))
				return false;
			// store turk username url encoded
			if ( ! sb->urlEncode ( tun ) ) return false;
			// print cmd
			char *cmd = "ban";
			if ( ! ban ) cmd = "<font color=red>unban</font>";
			// finish it up
			if ( ! sb->safePrintf(">%s</a></td><td>",cmd)) 
				return false;
		}
		else if ( st->m_isSuperTurk && targetSuper ) {
			if ( ! sb->safePrintf("<td>super</td><td>"))
				return false;
		}
		else {
			if ( ! sb->safePrintf("<td colspan=2>"))
				return false;
		}			

		if ( ! sb->safePrintf("<a href=\"/evalstats?"
				      "evaluser=%s&"
				      "showevaluser=%s&"
				      "c=%s&"
				      "mdy1=%s&"
				      "mdy2=%s\">"
				      // scroll to bottom to show most
				      // recent votes... "#bottom"
				      //"#bottom"\">"
				      "%s</a>"
				      ,st->m_turkUser
				      ,tun
				      ,st->m_coll
				      ,st->m_mdy1
				      ,st->m_mdy2
				      ,tun))
			return false;


		// end the cell and start new one for ip
		if ( ! sb->safePrintf("</td>" ) ) return false;

		bool showBanLink = true;
		// only show if superturk
		if ( ! st->m_isSuperTurk ) showBanLink = false;
		// and target is not super
		if ( targetSuper ) showBanLink = false;
		// start cell
		if ( showIps && ! sb->safePrintf("<td>" ) ) return false;
		// print all the ips they came from when voting so
		// we can ban individual ips from being turks
		for ( int32_t k = 0 ; k < tus->m_numTurkUserIps ; k++ ) {
			// breathe
			QUICKPOLL(TURKNICE);
			if ( ! showIps ) break;
			int32_t ip = tus->m_turkUserIps[k];
			int32_t ip64 = (int64_t)ip;
			int32_t ban = 1;
			char *cmd = "banip";
			Tag *btag = (Tag *)s_banTable.getValue ( &ip64 );
			if ( btag ) { 
				cmd = "<font color=red>unbanip</font>";
				ban = 0; 
			}
			char *ips = iptoa(ip);
			// skip if bad ip
			if ( ! strcmp (ips,"0.0.0.0"      ) ) continue;
			// the enginx gateway!
			if ( ! strcmp (ips,"65.111.176.19") ) continue;
			// the new neurobot.us ip
			if ( ! strcmp (ips,"65.111.171.90") ) continue;
			if ( ! strncmp(ips,"192.168.",8   ) ) continue;
			if ( ! strncmp(ips,"10.",3        ) ) continue;
			// separate, one ip per line
			if ( k > 0 &&  ! sb->safePrintf("<br>") )
				return false;
			if ( ! sb->safePrintf("<nobr>") ) return false;
			if ( showBanLink &&
			     ! sb->safePrintf("<a href=banevalip=%s&"
					      "ban=%"INT32">%s</a> &nbsp; "
					      ,ips 
					      ,ban
					      ,cmd) )
				return false;
			if ( ! sb->safePrintf("%s",ips) )
				return false;
			if ( ! sb->safePrintf("</nobr>") ) return false;
		}
		// end the IP cell if we printed it
		if ( showIps && ! sb->safePrintf("</td>" ) ) 
			return false;

		// print vote count stats
		if ( ! sb->safePrintf("<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      ,tus->m_errorPassed
				      ,tus->m_errorFailed
				      ,tus->m_errorUnconfirmed 
				      ,tus->m_errorNone ) )
			return false;
		// print vote count stats
		if ( ! sb->safePrintf("<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      ,tus->m_titlePassed
				      ,tus->m_titleFailed
				      ,tus->m_titleUnconfirmed 
				      ,tus->m_titleNone ) )
			return false;
		// print vote count stats
		if ( ! sb->safePrintf("<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      ,tus->m_venuePassed
				      ,tus->m_venueFailed
				      ,tus->m_venueUnconfirmed 
				      ,tus->m_venueNone ) )
			return false;
		// print vote count stats
		if ( ! sb->safePrintf("<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      "<td>%"INT32"</td>"
				      ,tus->m_descrPassed
				      ,tus->m_descrFailed
				      ,tus->m_descrUnconfirmed 
				      ,tus->m_descrNone ) )
			return false;

		float numPass = 0.0;
		numPass += tus->m_errorPassed;
		numPass += tus->m_titlePassed;
		numPass += tus->m_venuePassed;
		numPass += tus->m_descrPassed;
		float numFail = 0.0;
		numFail += tus->m_errorFailed;
		numFail += tus->m_titleFailed;
		numFail += tus->m_venueFailed;
		numFail += tus->m_descrFailed;

		// print voting accuracy
		float total = numPass + numFail;
		if ( total > 0 ) {
			float accuracy = numPass / total;
			if ( !sb->safePrintf("<td>%.0f%%</td>",accuracy*100.0))
				return false;
		}
		else {
			if ( ! sb->safePrintf("<td>--</td>") )
				return false;
		}

		// print money earned
		if ( ! sb->safePrintf("<td>$%.02f</td>",earned) )
			return false;


		if ( ! sb->safePrintf("</tr>\n" ) )
		     return false;
	}

	if (   st->m_useXml &&
	       ! sb->safePrintf("</response>" ) )
		return false;

	if ( st->m_useXml ) return true;

	// end table
	if ( ! sb->safePrintf("</table>\n"
			      "<br>\n"
			      "* <i>Votes less than 24 hours old are "
			      "subject to confirmation before "
			      "being paid.</i>" ) )
		return false;

	return true;
}


// return false with g_errno set on error
bool printSingleUserStats ( SafeBuf *sb , RdbList *list, char *showTurkUser ,
			    State60 *st ) {

	char *sutu = "";
	if ( isSuperTurk ( showTurkUser ) ) sutu = " (superuser)";
		
	if (   st->m_useXml &&
	       ! sb->safePrintf("<response>\n" ) )
		return false;

	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<br>"
			      "<h1>%s's Statistics%s</h1>"
			      , showTurkUser ,sutu) )
		return false;

	// current time in UTC!
	time_t now = getTimeGlobal();

	char datebuf[128];

	// print that utc clock
	if ( ! st->m_useXml ) {
		struct tm *timeStruct = gmtime ( &now );
		strftime(datebuf,100,"%b %d %Y %H:%M:%S UTC",timeStruct);
		if ( ! sb->safePrintf("<font style=\"font-size:12px\">"
				      "Current Time: %s</font><br><br>\n",
				      datebuf))
			return false;
	}


	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<br>"
			      "<table border=1 cellpadding=3>"
			      "<tr>"
			      "<td>#</td>"
			      "<td>Vote Time (UTC)</td>"
			      "<td>Voting IP</td>"
			      "<td>Vote review</td>"

			      "<td>Accept/Reject</td>"
			      "<td>Title</td>"
			      "<td>Venue</td>"
			      "<td>Description</td>"

			      //"<td>vote status</td>"
			      "<td>$ Earned</td>"
			      //"<td>vote weight</td>"
			      "</tr>\n" ) )
		return false;

	int32_t  lastDayNum = -1;
	int32_t  dayNum = -1;
	//int32_t  numGood = 0;
	//int32_t  numBad  = 0;
	//int32_t  numUnconfirmed = 0;
	float dailyEarned = 0.0;
	int32_t count = 0;
	// 2 cents per good vote
	// scan tags
	int32_t turkvotestatbydate = getTagTypeFromStr("turkvotestatbydate");
	// loop over all tags in the buf, see if we got a dup
	for ( ; ; list->skipCurrentRecord() ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// assume no tag, assume end of list
		Tag *tag = NULL;
		// only set tag if not end of list
		if ( ! list->isExhausted() ) {
			// get it
			tag = (Tag *)list->getCurrentRec();
			// must be a turk vote
			if ( tag->m_type != turkvotestatbydate ) continue;
			// get tag user
			char *user = tag->getUser();
			// if we had a turk user, must match, otherwise skip it
			if ( showTurkUser && strcmp(user,showTurkUser) ) 
				continue;
			// get day
			dayNum = tag->m_timestamp / 86400;
		}

		// print out date if different than last
		if ( (lastDayNum != -1 && lastDayNum != dayNum) || ! tag ) {
			// total votes
			//float total = (float)(numGood+numBad+numUnconfirmed);
			// the top of the fraction
			//float top = (float)(numGood+numUnconfirmed);
			// percent good
			//float accuracy = 0.0;
			// . only compute if had some data
			// . assume the unconfirmed votes were good! give
			//   them the benefit of the doubt. it is up to us
			//   to confirm them...
			//if ( total > 0 ) accuracy = top / total;

			// xml output for partap's payment process...
			if ( st->m_useXml && 
			     lastDayNum != -1 &&
			     ! sb->safePrintf ("\t<record>\n"
					       "\t\t<date>"
					       "<![CDATA[%s]]>"
					       "</date>\n"
					       "\t\t<earned>"
					       "<![CDATA[%.02f]]>"
					       "</earned>\n"
					       "\t</record>\n"
					       // date of last turk for day
					       ,datebuf
					       ,dailyEarned) )
				return false;

			// print out accuracy for last day
			else if ( ! st->m_useXml &&
				  ! sb->safePrintf ("<tr>"
						    "<td colspan=10>"
						    //"votes=%"INT32" "
						    //"passedvotes=%"INT32" "
						    //"failedvotes=%"INT32" "
						    //"unconfirmedvotes=%"INT32" "
						    //"accuracy=%.0f%% "
						    "earned=<b>$%.02f</b> "
						    "</td>"
						    "</tr>\n"
						    //,numGood+numBad
						    //,numGood
						    //,numBad
						    //,numUnconfirmed
						    //,accuracy*100
						    ,dailyEarned) )
				return false;
			// reset for next guy
			//numGood         = 0;
			//numBad          = 0;
			//numUnconfirmed  = 0;
			dailyEarned     = 0.0;
			count = 0;
		}

		// stop if no more tags remain to print
		if ( ! tag ) break;

		// hash of url event being voted on is contained in
		uint64_t tagUh48;
		// address/date content hash of event voted on
		uint32_t tagAdch;
		// good|bad|unconfirmed
		char status[16];
		// set these
		char *data  = tag->getTagData();
		int32_t  dsize = tag->getTagDataSize();
		// TODO: binary encode the tag data for speed?? it breaks
		// our rules for tagdb, but might be worth it
		sscanf ( tag->getTagData(),"%"UINT64",%"UINT32",%s",
			 &tagUh48,&tagAdch,status);

		// update this for printing out daily stats at end of day
		lastDayNum = dayNum;


		// print time it was turked in "datebuf"
		//struct tm *timeStruct = localtime ( &tag->m_timestamp );
		struct tm *timeStruct = gmtime ( &tag->m_timestamp );
		strftime(datebuf,100,"%b %d %Y %H:%M:%S UTC",timeStruct);
		if ( tag->m_timestamp == 0 ) sprintf(datebuf,"---");

		// make the link to the event
		char link[256];
		sprintf(link,
			"<a href=\"/eval?evaluser=%s&evalip=%s&c=%s&"
			"q=gbuh48:%"UINT64"+"
			// only if we directly turked it!
			// on salsapower.com there are event's with same
			// adch32 but in different places on page
			"gbturkeddirectlyby:%"UINT64"+"
			"gbadch32:%"UINT32"#top\">link</a>",
			st->m_turkUser,iptoa(st->m_turkIp),st->m_coll,
			tagUh48,
			st->m_tuid64,
			tagAdch);

		// print it out
		if ( ! st->m_useXml &&
		     ! sb->safePrintf ( "<tr>"
					"<td>%"INT32"</td>"
					"<td>%s</td>"
					"<td>%s</td>"
					"<td>%s</td>"
					,++count
					,datebuf
					// turk was voting from this ip
					,iptoa(tag->m_ip)
					,link) )
			return false;

		//float earned = 0.00;
		//if ( status[0] == 'g' ) earned = base; // 2 cents usually

		char *vv = data+dsize-1;
		// back up to comma
		for ( ; *vv !=',' ; vv-- );
		// skip over comma
		vv++;
		// point to it
		char *pv = vv;

		char *str = NULL;

		// print error vote status
		if      ( *pv == 'U' ) 
			str = "tbd";//unconfirmed";
		else if ( *pv == 'P' ) 
			str = "<b><font color=green>passed</font></b>";
		else if ( *pv == 'F' ) 
			str = "<b><font color=red>failed</font></b>";
		else if ( *pv == 'N' ) 
			str = "&nbsp;";//none";
		else { char *xx=NULL; *xx=0; }
		pv++;
		if ( ! st->m_useXml && ! sb->safePrintf("<td>%s</td>",str) ) 
			return false;


		// print title vote status
		if      ( *pv == 'U' ) 
			str = "tbd";
		else if ( *pv == 'P' ) 
			str = "<b><font color=green>passed</font></b>";
		else if ( *pv == 'F' ) 
			str = "<b><font color=red>failed</font></b>";
		else if ( *pv == 'N' ) 
			str = "&nbsp;";//none
		else { char *xx=NULL; *xx=0; }
		pv++;
		if ( ! st->m_useXml && ! sb->safePrintf("<td>%s</td>",str) ) 
			return false;


		// print venue vote status
		if      ( *pv == 'U' ) 
			str = "tbd";
		else if ( *pv == 'P' ) 
			str = "<b><font color=green>passed</font></b>";
		else if ( *pv == 'F' ) 
			str = "<b><font color=red>failed</font></b>";
		else if ( *pv == 'N' ) 
			str = "&nbsp;";//none";
		else { char *xx=NULL; *xx=0; }
		pv++;
		if ( ! st->m_useXml && ! sb->safePrintf("<td>%s</td>",str) ) 
			return false;

		// print descr vote status
		if      ( *pv == 'U' ) 
			str = "tbd"; // unconfirmed";
		else if ( *pv == 'P' ) 
			str = "<b><font color=green>passed</font></b>";
		else if ( *pv == 'F' ) 
			str = "<b><font color=red>failed</font></b>";
		else if ( *pv == 'N' ) 
			str = "&nbsp;";//none";
		else { char *xx=NULL; *xx=0; }
		pv++;
		if ( ! st->m_useXml && ! sb->safePrintf("<td>%s</td>",str) ) 
			return false;


		float earned = 0.0;
		// point to the voting record again
		pv = vv;
		if ( *pv == 'P' || *pv == 'U' )
			earned += ERRORBASEPAY;
		pv++;
		if ( *pv == 'P' || *pv == 'U' )
			earned += TITLEBASEPAY;
		pv++;
		if ( *pv == 'P' || *pv == 'U' )
			earned += VENUEBASEPAY;
		pv++;
		if ( *pv == 'P' || *pv == 'U' )
			earned += DESCRBASEPAY;
		
		// nothing if accept/reject is wrong though!
		if ( vv[0] == 'F' ) earned = 0.0;

		// but if less than 24 hours old do not pay!!!
		// put asterisk after it?
		char *ast = "*";
		if ( now - tag->m_timestamp >= 24*3600 ) {
			//earned = 0.0;
			ast = "";
			// accumulate per day -- onve over 24 hrs old!!
			//dailyEarned += earned;
		}
		// accumulate for all now
		dailyEarned += earned;

		// print it out
		if ( ! st->m_useXml &&
		     ! sb->safePrintf ( "<td>%.02f%s</td></tr>\n",earned,ast))
			return false;
		
	}

	if ( ! st->m_useXml && ! sb->safePrintf("</table>") )
		return false;

	// scroll to bottom to show latest
	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<a name=bottom></a>") )
		return false;


	// explain asterisk
	if ( ! st->m_useXml &&
	     ! sb->safePrintf("<br>* <i>Votes less than 24 hours old are "
			      "subject to confirmation before "
			      "being paid.</i>") )
		return false;

	if (   st->m_useXml &&
	       ! sb->safePrintf("</response>" ) )
		return false;

	return true;
}

///////////////////////////////////
//
// TURK MAIN PAGE
//
///////////////////////////////////

// . for receving a submission from a turk
// . adds the appropriate tags to the appropriate tagdb record
// . then reindexes the document with the event
// . may also query the eventtagformathash and add ALL the docids in that
//   set of search results so that they might benefit from the turking
class Msg1e {

public:

	bool processLoop ( ) ;

	bool addSupplementalVoteTags ( char *furl ,
				       int32_t now,
				       char *turkUser,
				       class TurkLock *tk ,
				       uint32_t mainSentch32,
				       uint32_t mainSentth32,
				       char *votePrefix ,
				       char *hiddenPrefix,
				       int32_t votePower,
				       SafeBuf *sb );


	TcpSocket *m_socket;
	HttpRequest *m_r;
	void *m_state;
	void (*m_callback)(void *state);
	int32_t m_stage;

	//Msg12 m_msg12;

	// let Msg7 supply this
	//XmlDoc m_xd;
	// for re-injecting the doc
	Msg7 m_msg7;
	Msg1c m_msg1c;
	SafeBuf m_sb;
	char m_queryBuf[256];
};


class CaptchaState {
public:
	void (*m_callback)(void *state);
	void  *m_state;
	bool   m_passed;
};

class State61 {
public:
	SafeBuf      m_sb;
	CaptchaState m_cst;
	bool         m_isSuperTurk;
	int64_t    m_tuid64;
	//char         m_isMasterAdmin;
	char         m_coll [ MAX_COLL_LEN + 1];
	int32_t         m_collLen;
	TcpSocket   *m_socket;
	Msg40        m_msg40;
	SearchInput  m_si;
	Query        m_qq;
	int32_t         m_round;
	HttpRequest  m_r;
	Msg1e        m_me;
	int32_t         m_i;
	// this means the user supplied the query, not us!
	bool         m_isTurkSpecialQuery;//m_checkTemplateTable;
	bool         m_showTurkInstructions;
	//int32_t         m_lastLaunched;
};

static bool getResults ( State61 *st ) ;
static bool gotResults ( State61 *st ) ;
static void gotResultsWrapper ( void *st ) {gotResults ( (State61 *)st); }
static void doneReindexing ( void *state ) ;
static bool presentTurkForm ( State61 *st ) ;
static bool printCaptcha ( State61 *st );

// Public Key: 6Lcbz8YSAAAAAKpdFkMMgC_hbqS7chAELn-RdEvO
// Private Key:6Lcbz8YSAAAAAPj32r2DiHFGMHzJj7Jd5siFpt6R
//char *g_pubKey  = "6LfD0sYSAAAAAPHUcvW0561zGy2tKuZNHzCBKJc_";
//char *g_privKey = "6LfD0sYSAAAAAGqm_MTJuley77B3PtauGoKNVt68";
char *g_pubKey  = "6LfG0sYSAAAAAGjlvIQm53qJ1LK41lSFM7ya9Qy7";
char *g_privKey = "6LfG0sYSAAAAAJIZqEYvnRgISrwB_u_schINl1ro";

static HashTableX s_captchaTable;
static char s_capbuf[1024];

bool isCaptchaReplyCorrect ( TcpSocket *s ) {
	// get the doc content
	char *reply = s->m_readBuf;
	// point to html
	char *content = NULL;
	if ( reply ) content = strstr ( reply , "\r\n\r\n" );
	if ( content ) content += 4;
	// check status
	if ( content && strncasecmp(content,"true",4) == 0 )
		return true;
	if ( content ) log("captcha: failed: %s",content);
	else           log("captcha: failed: no answer");
	return false;
}

// did we get a captcha reply?
void gotGoogleCaptchaReply ( void *state , TcpSocket *s ) {
	// cast it
	State61 *st = (State61 *)state;
	// is it correct?
	bool passed = isCaptchaReplyCorrect ( s );
	// if we passed reset this and present the next item to turk
	if ( passed ) {
		// store in table ...
		if ( s_captchaTable.m_numSlots > 0 ) 
			s_captchaTable.addTerm64 ( &st->m_tuid64 );
		presentTurkForm ( st );
		return;
	}
	// int16_tcut
	SafeBuf *sb = &st->m_sb;
	// print the full response
	//sb->safePrintf ( "Last Captcha Result: %s<br><br>",content);
	// otherwise, print another captcha!
	printCaptcha ( st );
	// and try again
	g_httpServer.sendDynamicPage ( st->m_socket,
				       sb->getBufStart(),
				       sb->length() ,
				       -1,
				       false);
	mdelete ( st , sizeof(State61) , "state61" );
	delete (st);
}

// . from http://code.google.com/apis/recaptcha/docs/verify.html
// . use POST to http://www.google.com/recaptcha/api/verify
// . returns false and sets g_errno on error
// . otherwise your callback will be called
bool verifyCaptchaInput ( TcpSocket *socket ,
			  HttpRequest *r ,
			  void *st ,
			  void (callback)(void *state,TcpSocket *s) ) {

	int32_t ip         = socket->m_ip;
	char *challenge = r->getString("recaptcha_challenge_field",NULL);
	char *response  = r->getString("recaptcha_response_field",NULL);

	char rubuf[1024];
	// the input. make this a POST!
	char *u = rubuf;
	u += sprintf(u,
		     "http://www.google.com/recaptcha/api/verify?"
		     "privatekey=%s&"
		     "remoteip=%s&"
		     //"recaptcha_challenge_field=%s&"
		     //"recaptcha_response_field=%s"
		     "challenge=%s&"
		     "response="
		     ,g_privKey
		     ,iptoa(ip)
		     ,challenge);
	// response has spaces in it so encode to %20 etc.
	u += urlEncode ( u , 100 , response , gbstrlen(response) );
	// null terminate
	*u = '\0';

	char *agent = "RecaptchaAgent/1.0";
	//char *agent = "Gigabot/2.0";
	//	"Mozilla/5.0 "
	//	"(X11; U; Linux i686; en-US; rv:1.9.2.7) "
	//	"Gecko/20100715 Ubuntu/10.04 (lucid) Firefox/3.6.7";

	// send it out
	if ( ! g_httpServer.getDoc ( rubuf,
				     0 , // ip of google unknown
				     0 , // offset
				     -1 , // size unknown
				     0 , // if modified since
				     st , // state
				     callback,//gotGoogleCaptchaReply ,
				     10*1000 , // 10 second timeout
				     0 , // proxy ip
				     0 , // proxy port
				     5000 , // max text doc len
				     5000 , // max other doc len
				     agent ,
				     "HTTP/1.0" , // proto
				     true )) // do POST?
		// return true if blocked
		return true;

	// error?
	return log("captcha: answer fetch error: %s",mstrerror(g_errno));
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageTurk ( TcpSocket *s , HttpRequest *r ) {

	// are we the admin?
	//bool isAdmin   = g_collectiondb.isAdmin ( r , s );

	// . must always be on host #0
	// . that way we can prevent lock leak
	// . so when a turk reloads the /eval page he gets back the same
	//   event he had locked... unless it got unlocked because it
	//   was like an hour old
	// . use g_lockerTable for this
	if ( g_hostdb.m_myHost->m_hostId != 0 ) {
		log("turk: must be called on host #0 only!");
		return g_httpServer.sendErrorReply(s,500,"not host #0!");
	}

	// did we get a turk user? should be there!
	char *turkUser = r->getString("evaluser",NULL);
	char *turkIpStr = r->getString("evalip",NULL);
	// if no user, print empty with form for entering user
	if ( ! turkUser )
		return g_httpServer.sendErrorReply(s,500,
						   "no evaluser supplied");

	if ( ! turkIpStr )
		return g_httpServer.sendErrorReply(s,500,
						   "no evalip supplied");

	// make a state
	State61 *st ;
	try { st = new (State61); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTagdb: new(%i): %s", 
		    sizeof(State61),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State61) , "st61" );


	st->m_r.copy ( r );
	// do not let original free the junk, m_r will do that
	st->m_r.m_cgiBuf2 = NULL;

	// get the collection record
	CollectionRec *cr = g_collectiondb.getRec ( r );
	// the pages.cpp should always verify this for us!
	if ( ! cr ) { char *xx=NULL;*xx=0; }
	// get collection name and its length
	char *coll    = cr->m_coll;
	int32_t  collLen = gbstrlen ( coll );
	gbmemcpy ( st->m_coll , coll , collLen );
	st->m_coll [ collLen ] = '\0';
	st->m_collLen=collLen;

	// save socket for retuning a page when we're done
	st->m_socket = s;

	//st->m_isSuperTurk = r->getLong("superturk",0);
	st->m_isSuperTurk = isSuperTurk ( turkUser );

	// did we get a turk user? should be there!
	//char *turkUser = r->getString("turkuser",NULL);
	// convert to 64-bit hash
	st->m_tuid64 = hash64n(turkUser);

	// this means the user supplied the query, not us!
	char *sq = st->m_r.getString("q",NULL);
	if ( sq ) st->m_isTurkSpecialQuery = true;
	else      st->m_isTurkSpecialQuery = false;

	// default to yes!
	st->m_showTurkInstructions = st->m_r.getLong("showins",-1);

	// print the admin top junk for page
	//g_pages.printAdminTop ( &st->m_sb , s , r );

	// . did we get a turk submission?
	// . let msg1e process it
	if ( r->getLong("eval",0) == 1 ) {
		// reset
		st->m_me.m_stage = 0;
		// reference our copied version of httpRequest
		st->m_me.m_r = &st->m_r;
		// set its callback
		st->m_me.m_callback = doneReindexing;
		st->m_me.m_state    = st;
		// handle the input
		if ( ! st->m_me.processLoop ( ) )
			return false;
	}

	// is it a captcha reply? let's verify it if so
	char *captchasubmit = st->m_r.getString("captchasubmit",NULL);

	// if they hit reload on the page that appeared right after they
	// solved the captcha, fix that!
	int32_t score3 = s_captchaTable.getScore ( &st->m_tuid64 );
	if ( score3 != 0 ) captchasubmit = NULL;

	if ( captchasubmit ) {
		// . send out request to google
		// . this returns false and sets g_errno on error
		// . returns true if blocked and will call callback
		if ( verifyCaptchaInput ( s , r , st ,gotGoogleCaptchaReply ) )
			return false;
		// we had an error
	hadError:
		char *errmsg = mstrerror(g_errno);
		bool ss = g_httpServer.sendErrorReply(st->m_socket,500,errmsg);
		mdelete ( st , sizeof(State61) , "state61" );
		delete (st);
		// we did not block
		return ss;
	}

	// trick em
	int32_t turkIp = atoip(turkIpStr);
	if ( isTurkBanned ( &st->m_tuid64, turkIp ) ) {
		g_errno = EDNSTIMEDOUT;
		goto hadError;
	}

	if ( ! g_conf.m_turkingEnabled ) {
		char *errmsg = "Evaluating is temporarily disabled while we "
			"are upgrading the system";
		bool ss = g_httpServer.sendErrorReply(st->m_socket,500,errmsg);
		mdelete ( st , sizeof(State61) , "state61" );
		delete (st);
		// we did not block
		return ss;
	}
		
	//
	// before displaying the page to be turked, see if we already
	// evaluated it. sometimes the server cores before it can fully
	// inject the evaluated page, so we think it hasn't been evaluated,
	// but we have already added the turk tag votes to turkdb...
	// thus we get locked up. we see the page with our votes in red,
	// and we have no submit button to re-inject it.
	// or, just display the page again, but with a submit button that
	// will just reinject the page... or allow them to re-vote on it
	// but only if we set cored=1 or something...



	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		s_captchaTable.set(8,4,64,s_capbuf,1024,false,0,"captctbl");
	}
	// assume do not have to do a captcha yet
	bool doCaptcha = false;
	// since we are always host #0, just keep a global table
	// that has how many times each turk user has voted today without
	// having a captcha!
	int32_t score = s_captchaTable.getScore ( &st->m_tuid64 );
	// 20 votes or more means do a captch
	if ( score == 0 ) doCaptcha = true;
	// but not super turk!
	if ( st->m_isSuperTurk ) doCaptcha = false;
	// turn off for debugging now!
	doCaptcha = false;
	// before doing a search query, see if we shold do a captcha
	if ( doCaptcha ) {
		// int16_tcut
		SafeBuf *sb = &st->m_sb;
		// sometimes provide a captcha
		if ( ! printCaptcha ( st ) ) goto hadError;
		// ok, print it out!
		g_httpServer.sendDynamicPage ( st->m_socket,
					       sb->getBufStart(),
					       sb->length() ,
					       -1,
					       false);
		mdelete ( st , sizeof(State61) , "state61" );
		delete (st);
		return true;
	}

	// how many times they've turked an event without answering a captcha
	s_captchaTable.addTerm64 ( &st->m_tuid64 );
	// if now 30, remove! next time they will see a captcha
	if ( score >= 29 ) s_captchaTable.removeKey ( &st->m_tuid64 );

	// now do the search and present the turk form
	return presentTurkForm ( st );
}


void doneReindexing ( void *state ) {
	State61 *st = (State61 *)state;
	presentTurkForm( st );
}

bool presentTurkForm ( State61 *st ) {

	// set stuff now
	//st->m_isMasterAdmin = isAdmin;

	// if g_errno was set then the last injection did not go through
	// perhaps because of ENOMEM or the geocoder was down!
	// so just note that here
	if ( g_errno ) {
		char *errmsg = mstrerror(g_errno);
		log("turk: injection had an error: %s",errmsg);
		bool x=g_httpServer.sendErrorReply(st->m_socket,500,errmsg);
		mdelete ( st , sizeof(State61) , "state61" );
		delete (st);
		return x;
	}

	// reset round
	st->m_round = 0;

	HttpRequest *r = &st->m_r;

	// did we get a turk user? should be there!
	char *turkUser = r->getString("evaluser",NULL);
	// convert to 64-bit hash
	//st->m_tuid64 = hash64n(turkUser);

	CollectionRec *cr = g_collectiondb.getRec ( &st->m_r );

	SearchInput *si = &st->m_si;
	// init it
	//si->set ( st->m_socket , r , &st->m_qq );
	// default this up. niceness = 0.
	si->setToDefaults ( cr , 0 );
	// the most important!
	si->m_q = &st->m_qq;
	// some search parms
	si->m_skipEventMerge            = 1;
	si->m_niceness                  = 0;// TURKNICE;
	si->m_doSiteClustering          = false;
	si->m_doIpClustering            = false;
	si->m_doDupContentRemoval       = false;
	si->m_docsWanted                = 300; // endNum - startNum;
	si->m_firstResultNum            = 0; // startNum;
	si->m_getTurkForm               = true;
	si->m_showTurkInstructions      = st->m_showTurkInstructions;
	// this means the user supplied the query, not us!
	si->m_isTurkSpecialQuery        = st->m_isTurkSpecialQuery;
	si->m_compoundListMaxSize       = 1000000;
	si->m_numTopicGroups            = 0;
	si->m_clockOff                  = 0;
	si->m_clockSet                  = 0;
	// it is not super critical machine clock is synced right away.
	// it could take a second or so after we come up to sync with host #0
	if ( isClockInSync() ) si->m_nowUTC = getTimeGlobal();
	else                   si->m_nowUTC = getTimeLocal ();
	// . sort by next upcoming time of event (if any)
	// . TODO: make sure does not include expired events
	si->m_sortBy                    = SORTBY_TIME;
	// coll
	//strncpy ( si->m_coll , st->m_coll , MAX_COLL_LEN-1);
	si->m_coll2    = st->m_coll;
	si->m_collLen2 = st->m_collLen;
	// store turk user in search input
	//strncpy ( si->m_turkUser , turkUser , 100 );
	si->m_turkUser = turkUser;
	// store turk ip in search input
	char *tips = r->getString("evalip",NULL);
	if ( tips ) si->m_turkIp = atoip ( tips );

	return getResults ( st );
}

// returns false if blocked, true otherwise
bool getResults ( State61 *st ) {

	// nothing to turk?
	if ( st->m_round >= 3 )
		// nothing too turk!
		return gotResults ( st );

	// prefer getting the stuff turked first so we can confirm it
	// and get people paid!!
	int32_t turked = 1;
	if ( st->m_round == 0 ) turked = 1;
	if ( st->m_round == 1 ) turked = 0;
	// . if no results from the first 2 rounds, now we try a 3rd
	//   query that allows us to directly turk pages that we have
	//   turked indirectly and are still not confirmed (i.e. no consensus)
	// . so we can end up directly turking every event if we do not
	//   have 5 total turks that whose 5 indirect votes are required
	//   to give consensus/confirmation on an event.
	int64_t tuh64 = st->m_tuid64;
	if ( st->m_round == 2 ) tuh64  = 0;

	// advance!
	st->m_round = st->m_round + 1;

	// sometimes we restrict turking to a city so this might
	// be gbcity:albuquerque for example
	CollectionRec *cr = g_collectiondb.getRec ( &st->m_r );
	char *suppq = "";
	if ( cr ) suppq = cr->m_supplementalTurkQuery;

	// construct the query
	char tq[1024];
	sprintf ( tq, 
		  // avoid events we already turked directly
		  "-gbturkeddirectlyby:%"UINT64" "
		  // if NOT all (error,title,venue) got an indirect vote
		  // from us, then we should directly turk it! its like
		  // a half ass indirect turk ...
		  "-gberrortitlevenueturkedindirectlyby:%"UINT64" "
		  // avoid events with outlinked titles ALWAYS. i only
		  // added these back in so that if the turks cause an event
		  // to get an outlinked title we do not lose all the voting
		  // info because before EV_OUTLINKED_TITLE bit was part
		  // of the EV_BAD_FLAGS bits and the event would not get
		  // indexed or stored.
		  //"-gbtitleinoutlink:1 "
		  // this now also includes EV_OLD_EVENT and EV_DEDUPED that
		  // were still indexed because they were directly turked
		  // at one time, otherwise, we'd lose that voting info and
		  // the voting turk could not look at their record. of course
		  // if the page is updated and the event is dropped we will
		  // lose the voting record that way, anyway...
		  "-gbbadevent:1 "
		  // avoid events whose title and status are confirmed already
		  "-gbturkconfirmed:1 "
		  // avoid store hours for now...
		  // . no because we now set EV_STORE_HOURS if the event
		  //   title equals the event venue name, and we might have
		  //   picked the wrong title, so we need the turks to
		  //   decide!
		  // . and besides the store hours seems to be a small 
		  //   percent
		  // . to fix this may maybe index gbstorehoursbytime:1
		  //   or something...
		  "-gbstorehours:1 "
		  // sometimes do initial eval, sometimes confirm old eval...
		  "+gbturked:%"INT32" "
		  "%s"
		  ,st->m_tuid64
		  // use a fake value of 0 for this to turn it off!
		  ,tuh64//,st->m_tuid64
		  ,turked
		  ,suppq);


	logf(LOG_DEBUG,"turk: q=%s",tq);

	// set query. bool flag = 0 --> not bool
	st->m_qq.set ( tq , 0 );
	// check template table by default
	//st->m_checkTemplateTable = true;
	//st->m_isTurkSpecialQuery = false;
	// . use provided query if superturk
	// . regular turk does a query to show past turked things!!
	//if ( st->m_isSuperTurk ) {
	char *sq = st->m_r.getString("q",NULL);
	// bool flag = 2 --> autodecide
	if ( sq ) st->m_qq.set ( sq, 2 ); 
	//}
	// get search results
	SearchInput *si = &st->m_si;
	// reset result for loop index
	st->m_i = 0;
	//st->m_lastLaunched = -1;

	// try to verify events even if they are expired
	if ( turked ) si->m_showExpiredEvents = 1;
	else          si->m_showExpiredEvents = 0;

	// get results
	if ( ! st->m_msg40.getResults ( si ,false,st,gotResultsWrapper ) )
		// return false if we blocked
		return false;

	// . this returns false if blocks, true otherwise
	// . sets g_errno on failure
	if ( ! gotResults ( st ) ) return false;
	// did not block...
	return true;
}

class TurkLock {
public:
	int64_t m_tuid64;
	int32_t      m_turkIp;
	uint32_t m_adch32;
	uint32_t m_adth32;
	bool      m_isSuperTurk;
	int64_t m_uh48;
	time_t    m_addTime;
	uint64_t m_templateHash64;
	bool m_isTurkSpecialQuery;
};

static HashTableX g_lockerTable;
static bool s_init = false;

RdbCache g_templateCache;

// returns false if blocked, true otherwise
bool gotResults ( State61 *st ) {
	// int16_tcut
	Msg40 *m40 = &st->m_msg40;

	// have we exhausted all queries?
	bool exhausted = (st->m_round >= 3);

	int32_t numResults = m40->getNumResults();

	// if no results, try another query or print empty page if can't
	if ( numResults <= 0 && ! exhausted ) return getResults (st);

	Msg20Reply *mr = NULL;

	if ( ! s_init ) {
		s_init = true;
		// . init the lock table, send back error msg
		// . now we allow dups so superturks can lock something
		//   in addition to one regular turk
		if ( ! g_lockerTable.set (8,(int32_t)sizeof(TurkLock)
					  ,64,NULL,0,true,0,"lckrtbl") ){
		hadError:
			char *errmsg = mstrerror(g_errno);
			bool x;
			x=g_httpServer.sendErrorReply(st->m_socket,500,errmsg);
			mdelete ( st , sizeof(State61) , "state61" );
			delete (st);
			return x;
		}
	}


	// init this if we need to
	static bool s_ttinit = false;
	if ( ! s_ttinit ) {
		if ( ! g_templateCache.init(300000,
					    0,
					    false,
					    300000/16, // maxCacheNodes
					    false , // usehalfkeys
					    "templche",
					    false,
					    8, // cachekeysize
					    0 ) )// datakeysize
			// return true with g_errno set on error
			return true;
		s_ttinit = true;
	}

	// int16_tcut
	//Msg12 *m12 = &st->m_me.m_msg12;

	// debug for now
	//g_conf.m_logDebugSpider = true;

	uint64_t templateHash64 ;


	// . scan lockertable and remove any old lock you may have had
	// . like if you are a super turk and decide to spot turk another
	//   url and abandon the page you are on, let's release the lock
	// . or if you are a regular turk viewing your past voting history
	//   because you clicked on the "link" in your "My Stats" page.
	// . ALSO, to fix turks from leaving the page on their browser we
	//   remove their lock so others can turk the page
	// . and if the try to submit the turk form after an hour
	//   it will probably log the fradulated comment below
	int32_t now = getTimeGlobal();
	HashTableX *ht = &g_lockerTable;
	// come up here if we did remove something
 redo:
	// timeout old locks first (cleanup)
	for ( int32_t i = 0 ; i < ht->m_numSlots ; i ++ ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// skip if empty
		if ( ! ht->m_flags[i] ) continue;
		// get slot
		TurkLock *tk = (TurkLock *)ht->getValueFromSlot(i);
		// get timestamp
		time_t addTime = tk->m_addTime;
		// skip it?
		bool nuke = false;
		// expired?
		if ( now - addTime >= 3600 ) nuke = true;
		// or if us!
		if ( tk->m_tuid64 == st->m_tuid64 ) nuke = true;
		// remove it?
		if ( ! nuke ) continue;
		// get key
		int64_t uh48 = tk->m_uh48;
		// ok, it is expired
		log("turk: force releasing turk lock on uh48=%"UINT64"",uh48);
		// do it
		ht->removeSlot ( i );
		// re do full loop
		goto redo;
	}





	// scan each result and get try to get the turk lock for it!
	for (  ; st->m_i < numResults ; st->m_i++ ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// get result #i reply
		mr = m40->m_msg20[st->m_i]->m_r;
		// no need to do lock checking if doing a direct query
		if ( st->m_isTurkSpecialQuery ) break;
		// int16_tcut
		int64_t uh48 = mr->m_urlHash48;
		// int16_tcut
		HashTableX *ht = &g_lockerTable;
		// check our lock table
		int32_t tslot = ht->getSlot(&uh48);
		// assume we own it
		bool available = true;
		// iterate over locks
		for ( ; tslot >= 0 ; tslot = ht->getNextSlot(tslot,&uh48) ) {
			// breathe
			QUICKPOLL(TURKNICE);
			// cast it
			TurkLock *tk = (TurkLock *)ht->getValueFromSlot(tslot);
			// skip if superturk
			if ( tk->m_isSuperTurk ) continue;
			// ignore also if just looking at stats
			if ( tk->m_isTurkSpecialQuery ) continue;
			// stop if us
			if ( tk->m_tuid64 == st->m_tuid64 ) break;
			// i guess another turk has locked this url
			available = false;
			break;
		}
		// skip result if not ours exclusivelt
		if ( ! available ) continue;
		// stop if no need to check
		//if ( ! st->m_checkTemplateTable ) break;
		// now we also skip it if we already turked this TEMPLATE
		// . crap what if the template has no definitive title tag
		//   hash that identifies the title, then the indirect votes
		//   aren't good enough and we have to turk each event 
		//   directly. so the turks will have to re-turk the same
		//   template... so this doesn't cut it...
		templateHash64 = hash64h ( st->m_tuid64 , mr->m_adth32 );
		// incorporate domain hash too now
		templateHash64 = hash64h ( mr->m_domHash , templateHash64 );
		// . only check if round excludes pages indirectly turked 
		//   by this turk to get around the "crap" note above
		// . round was incremented, so use <= 2
		if ( st->m_round <= 2 &&
		     // use -1 for maxAge --> no expiration date since
		     // once we've indirectly turked a format for a site then
		     // we've always indirectly turked it...
		     g_templateCache.isInCache ( 0,(char *)&templateHash64,-1))
			continue;
		// otherwise, not there or it is our lock
		break;
		/*
		// . try to get lock if we have not launched m_i request yet
		// . this returns false if blocked
		if ( st->m_lastLaunched != st->m_i ) {
			// launch lock request
			bool status = m12->getLocks ( mr->m_urlHash48 ,
						      mr->ptr_ubuf ,
						      st ,
						      gotResultsWrapper );
			// note it
			st->m_lastLaunched = st->m_i;
			// return false if blocked
			if ( ! status ) return false;
		}
		// ok, did not block... or last reply returned with lock!
		if ( m12->m_hasLock ) break;
		*/
	}


	// if all were locked try the query again but for stuff
	// that has not yet been turked! fixes an issue where
	// zak has lock on the only turked page becaues he's turking
	// a 2nd event on that same page.
	if ( st->m_i >= numResults && ! exhausted ) 
		return getResults (st);

	char *turkUser = st->m_r.getString("evaluser",NULL);
	char *turkIp   = st->m_r.getString("evalip",NULL);
	// where to store?
	SafeBuf *sb = &st->m_sb;
	if ( ! printHeader ( turkUser, turkIp, st->m_coll , sb ) )
		goto hadError;

	// if they were following a "link" on the stats page and the event
	// got dropped from the page, then it will be a not found!
	if ( st->m_i >= numResults && st->m_isTurkSpecialQuery ) {
		// tell them
		if ( ! sb->safePrintf("<b>Sorry, the event that you evaluated "
				      "no longer exists in the index. Most "
				      "likely the web page was updated and "
				      "the event was dropped by the "
				      "webmaster."
				      "</b>"))
			goto hadError;
	}
	// no lock? nothing to turk then...
	else if ( st->m_i >= numResults ) {
		// tell them
		if ( ! sb->safePrintf("<b>Sorry, nothing available to turk. "
				      "Are you sure <font color=red>\"%s\""
				      "</font> is the right "
				      "collection? "
				      "Please try again later.</b>",
				      st->m_coll) )
			goto hadError;
	}
	else {
		// make tbl so right pane is iframe of the page being turked
		//if ( ! sb->safePrintf("<TABLE border=1 width=100%%>") )
		//	goto hadError;

		//if(!sb->safePrintf("<TR><TD bgcolor=lightgreen valign=top>"))
		//	goto hadError;
		if ( ! sb->safePrintf("\n"
				      "<div id=\"eval-main\" "
				      "style=position:fixed;"
				      "top:100px;left:5;right:5;bottom:0>\n"
				      "<div id=\"eval-left\" style="
				      "overflow:auto;"
				      "background-color:lightgreen;"
				      "position:absolute;"
				      "top:0;"
				      "left:0;"
				      "bottom:0;"
				      //"width:900px;>\n"
				      "width:40%%;>\n"
				      ))
			goto hadError;

		// . make a turk lock
		// . save XmlDoc::getTurkForm() from having to relay this info
		//   back to us when we process the form inputs
		// . also makes it harder for turks to hack us
		TurkLock tk;
		tk.m_tuid64      = st->m_tuid64; // turkuser hash
		//tk.m_eventHash64 = mr->m_eventHash64;
		//tk.m_eventId     = mr->m_eventId;
		//tk.m_docId       = mr->m_docId;
		tk.m_turkIp      = st->m_si.m_turkIp;
		tk.m_adch32      = mr->m_adch32; // address date content hash
		tk.m_adth32      = mr->m_adth32; // address date tag hash
		tk.m_isSuperTurk = st->m_isSuperTurk;
		tk.m_uh48        = mr->m_urlHash48;
		tk.m_addTime     = getTimeGlobal(); // now;
		tk.m_templateHash64 = templateHash64;
		tk.m_isTurkSpecialQuery = st->m_isTurkSpecialQuery;
		// if we are doing a special query, do not bother with lock!
		// no, otherwise it say fradulated...
		if ( //! st->m_isTurkSpecialQuery &&
		     // otherwise, add a lock to it...
		     ! g_lockerTable.addKey(&mr->m_urlHash48,&tk))
			goto hadError;
		// log that too
		log("turk: adding lock uh48=%"UINT64" for %s",
		    mr->m_urlHash48,turkUser);
		// now display the first search result
		// use this for now
		if ( ! sb->safeMemcpy ( mr->ptr_turkForm ,
					mr->size_turkForm - 1 ) ) 
			goto hadError;

		// need to set this junk
		char ebuf[65];
		ebuf[0] = '\0';
		EventIdBits eib;
		eib.clear();
		eib.addEventId ( mr->m_eventId );
		binToHex((uint8_t *)eib.m_bits,32,ebuf);
		ebuf[64] = '\0';
		if ( ! sb->safePrintf(//"</TD><TD valign=top "
				      //"bgcolor=lightyellow>"

				      // end the left pane div
				      "</div>"
				      // begin the iframe div
				      "<div id=\"eval-right\" style="
				      "overflow:auto;"
				      "background-color:lightyellow;"
				      "position:absolute;"
				      "top:0;"
				      //"left:601px;"
				      "right:0;"
				      "bottom:0;"
				      //"width:900px;"
				      "width:60%%;"
				      ">\n"

				      "<iframe "
				      "style="
				      "position:relative;"
				      "border:0;"
				      "width:100%%;"
				      "height:100%%; "
				      "src=\""
				      // the cached url
				      // cnsp=1 means do not print disclaimer
				      // "This is Gigablast's cached page..."
				      "/get?"
				      "c=%s&d=%"INT64"&qh=0&links=1&cnsp=1&eb=%s"
				      "#gbscrolldown"
				      "\" "
				      "scrolling=yes "
				      "align=top "
				      //"width=1000 "
				      //"height=1000>"
				      ">\n"
				      "Your browser does not support iframes. "
				      "You may not evaluate this event "
				      "information until you upgrade to "
				      "a browser that does. "
				      "</iframe>"
				      //"</TD></TR></TABLE>\n"
				      "</div>\n"
				      "</div>\n"

				      ,st->m_coll
				      ,mr->m_docId 
				      ,ebuf 
				      ))
			goto hadError;
	}

	// now the content
	

	// ok, print it out!
	g_httpServer.sendDynamicPage ( st->m_socket,
				       sb->getBufStart(),
				       sb->length() ,
				       -1,
				       false);

	mdelete ( st , sizeof(State61) , "state61" );
	delete (st);
	return true;
}



////////////////////////
//
// Msg1e 
//
// adds turk votes to tagdb and reindexes turked urls
//
///////////////////////

static void processLoopWrapper ( void *state ) {
	Msg1e *THIS = (Msg1e *)state;
	if ( ! THIS->processLoop() ) return;
	// all done, call callback
	THIS->m_callback ( THIS->m_state );
}

// see XmlDoc::getTurkForm() to see where the form we process here is created
bool Msg1e::processLoop ( ) {

	// * the taghash is the hash of the first tag's id containing the event
	//   line combined with the hashes of the next four tag parents' ids.
	//   this allows us to apply what we learn here to other pages on the 
	//   same site.

	// * the contenthash is already given in EventDesc::m_contentHash and
	//   was taken from the content hash of the sentence section.

	// TODO: set EDF_VENUE_NAME bits in each EventDesc class

	// TODO: add the venue names as AF_TURKED_NAME to placedb so
	//       that the turk is verifying venue names for us.

	// TODO: make tagdb lookup url as a whole for getting tagdb record of 
	//       page so eventreject can be applied to all events on a 
	//       particular url.

	// Then when we calculate the event title, etc. we grab the tags 
	// mentioned above and use them to increase/decrease the title score,
	// desc score and venue name scores.

	// We still index rejected events, for debugging and re-turking 
	// purposes, but we index the gbtagreject:1 term for them. if an event
	// was accepted and not rejected, and it was turked, we index 
	// gbtagaccept:1


	// this happens if ENOME but more often if geocoder is down!!
	if ( g_errno ) {
		log("turk: got error: %s",mstrerror(g_errno));
		return true;
	}

	// int16_tcut for all stages to use
	XmlDoc *xd = &m_msg7.m_xd;

	///////////////////////////
	// 
	// STAGE 0
	//
	///////////////////////////

	if ( m_stage == 0 ) {
		int64_t docId = m_r->getLongLong("docid",0LL);
		log("turk: loading old title rec for turked docid=%"UINT64" "
		    ,docId);
		if ( docId == 0LL ) { g_errno = ENODOCID; return true; }
		CollectionRec *cr = g_collectiondb.getRec ( m_r );
		char *coll = cr->m_coll;
		// . try a niceness of 0
		// . should not block, but just returns false on error
		if ( ! xd->set3 ( docId , coll , TURKNICE ) ) return true;
		// use this msg and function as the callback function
		xd->m_state     = this;
		xd->m_callback1 = processLoopWrapper;
		// advance stage
		m_stage = 1;
		// this usually blocks
		if ( ! xd->loadFromOldTitleRec ( ) ) 
			return false;
	}


	///////////////
	//
	// INTERMEDIATE SETUP
	//
	//////////////

	// get key
	int64_t uh48 = xd->getFirstUrlHash48();
	char *url = xd->getFirstUrl()->getUrl();

	// need this now i guess
	char      *turkUser = m_r->getString("evaluser",NULL);
	int64_t  tuid64 = hash64n ( turkUser );

	// int16_tcut
	HashTableX *ht = &g_lockerTable;
	// . make sure this is the url they had locked
	// . prevents turk from turking a url they should not be
	// . try to find the lock that belongs to us
	int32_t tslot = ht->getSlot(&uh48);
	// assume no lock
	TurkLock *tk = NULL;
	// iterate over locks
	for ( ; tslot >= 0 ; tslot = ht->getNextSlot(tslot,&uh48) ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// cast it
		tk = (TurkLock *)ht->getValueFromSlot(tslot);
		// stop if us
		if ( tk->m_tuid64 == tuid64 ) break;
	}


	// needs to be there
	if ( ! tk ) {
		log("turk: turk fradulated bogus docid uh48=%"UINT64"",uh48);
		g_errno = EPLSRESUBMIT;
		return true;
	}
	// see if banned
	//int32_t turkIp = atoip(turkIpStr);
	if ( isTurkBanned ( &tk->m_tuid64, tk->m_turkIp ) ) {
		log("turk: turk is banned. wtf uh48=%"UINT64"",uh48);
		g_errno = EDNSTIMEDOUT;
		return true;
	}
	// must match us
	if ( tk->m_tuid64 != tuid64 ) {
		log("turk: wrong turkuser for lock uh48=%"UINT64"",uh48);
		g_errno = EBADENGINEER;
		return true;
	}
	if ( ! tk->m_adch32 ) {
		log("turk: missing adch32 for lock uh48=%"UINT64"",uh48);
		g_errno = EBADENGINEER;
		return true;
	}

	// not while repairing
	if ( g_repairMode ) {
		log("turk: can't turk while repairing.");
		g_errno = EREPAIRING;
		return true;
	}


	///////////////////////////
	// 
	// STAGE 1
	//
	///////////////////////////

	if ( m_stage == 1 ) {
		// log the stages
		log("turk: adding turk votes for uh48=%"UINT64" url=%s",
		    uh48,url);
		// info on turk's ip
		//char *turkUser  = m_r->getString("turkuser",NULL);
		//char *userIpStr = m_r->getString("turkip",NULL);
		//int32_t  turkIp    = 0;
		//if ( userIpStr ) turkIp = atoip(userIpStr);
		// get int64_ts since they are printed as 32-bit unsigned
		// so if they exceed 3 billion or so HttpRequest::getLong()
		// would falter!
		//int32_tint32_t adch64=m_r->getLongLong("addressdatecontenthash",0);
		//int64_t adth64=m_r->getLongLong("addressdatetaghash",0);
		// but ultimately they are 32 bit unsigned
		//uint32_t adch32 = (uint32_t)adch64;
		//uint32_t adth32 = (uint32_t)adth64;
		// int16_tcuts
		int32_t now = getTimeGlobal();
		SafeBuf *sb = &m_sb;

		int32_t votePower = 1;
		//char isSuperTurk = m_r->getLong("superturk",0);
		if ( tk->m_isSuperTurk ) votePower = 5;

		int32_t reject = m_r->getLong ( "reject" , 0 );
		// error?
		if ( reject <= -1 || reject >= 10 ) {
			log("turk: reject code error = %"INT32"",reject);
			g_errno = EBADENGINEER;
			return true;
		}

		// banned turk?
		if ( isTurkBanned ( &tuid64, tk->m_turkIp ) ) {
			log("turk: avoided adding banned turk votes");
			g_errno = EBADENGINEER;
			return true;
		}

		// make a special fake site so we just store all votes
		// for the same adth32 together
		char fakeSite[64+MAX_URL_LEN];
		char *dom  = xd->getFirstUrl()->getDomain();
		int32_t  dlen = xd->getFirstUrl()->getDomainLen();
		char  c = dom[dlen];
		dom[dlen] = '\0';
		sprintf(fakeSite,"adth32-%"UINT32"-%s",tk->m_adth32,dom );
		dom[dlen] = c;


		////////////////
		//
		// add vote for error (accept/reject)
		//
		////////////////

		// . add the turkaccepted/turkrejected tag
		// . did the turk reject the event using the place date hash?
		// add it
		char dataVal[256];
		// data=<uh48>,<adch32>,<adth32>>,<sentch32>
		// <sentth32>,<"title"|"venue"|"descr"|"error">,[0|1+] 
		//(c=contenthash,t=taghash)
		sprintf ( dataVal,"%"UINT64",%"UINT32",%"UINT32",%"UINT32",%"UINT32",%"INT32",error,%"INT32"",
			  uh48,tk->m_adch32,tk->m_adth32,
			  // since we do not have a single sentence
			  // for this vote, it is for the event
			  // as a whole, so use adch32/adth32
			  tk->m_adch32,tk->m_adth32,
			  votePower,reject);
		// put tag into this rdb
		//if ( ! sb->pushChar ( (char)RDB_TURKDB ) ) 
		//	return true;
		if ( ! sb->addTag ( fakeSite,//xd->getFirstUrl()->getUrl() ,
				    "turkvote" ,
				    now        ,
				    turkUser   ,
				    tk->m_turkIp     ,
				    dataVal    ,
				    gbstrlen(dataVal),
				    RDB_TURKDB ,
				    true ) )
			return true;



		////////////////
		//
		// add vote for title
		//
		////////////////
		if ( reject == 0 ) {
			// get the title value
			char *title = m_r->getValue("turktitle",NULL);
			// must be there if not rejected
			if ( ! title ) {
				log("turk: no title given!");
				g_errno = EBADENGINEER;
				return true;
			}
			// its format is like %"UINT32"-%"INT32" = (sentch32-sentth32)
			uint32_t titlech32 = 0;
			uint32_t titleth32 = 0;
			sscanf ( title , "%"UINT32"-%"UINT32"", &titlech32, &titleth32 );
			// add vote tag into here
			char dataVal[256];
			// sanity check
			if ( titlech32 == 0 ) { char *xx=NULL;*xx=0; }
			// data=<uh48>,<adch32>,<adth32>>,<sentch32>
			// <sentth32>,<"title"|"venue"|"descr"|"error">,[0|1+] 
			//(c=contenthash,t=taghash)
			sprintf ( dataVal,"%"UINT64",%"UINT32",%"UINT32",%"UINT32",%"UINT32",%"INT32",title,1",
				  uh48,tk->m_adch32,tk->m_adth32,
				  titlech32,titleth32,votePower);
			// put tag into this rdb
			//if ( ! sb->pushChar ( (char)RDB_TURKDB ) ) 
			//	return true;
			if ( ! sb->addTag ( fakeSite,
					    "turkvote" ,
					    now        ,
					    turkUser   ,
					    tk->m_turkIp     ,
					    dataVal    ,
					    gbstrlen(dataVal) ,
					    RDB_TURKDB ,
					    true ) )
				return true;
			// crap, now we must add all of our sentences that
			// have that content hash but a different tag hash
			// because the turk doesn't know which one is the
			// right one! and neither do we!!
			// so scan all of our cgis...
			//
			// ADD VOTES FOR TITL2
			if ( ! addSupplementalVoteTags ( fakeSite,
							 now,
							 turkUser,
							 tk,
							 titlech32,
							 titleth32,
							 // vote prefix
							 "titl",
							 "i-desc-",
							 votePower,
							 sb))
				return true;
		}


		////////////////
		//
		// add vote for venue
		//
		////////////////
		if ( reject == 0 ) {
			// get the title value
			char *venue = m_r->getValue("turkvenue",NULL);
			// must be there if not rejected
			if ( ! venue ) {
				log("turk: no venue given!");
				g_errno = EBADENGINEER;
				return true;
			}
			// if none, do not add
			if ( strcmp ( venue, "none" ) == 0 ) goto skipVenue;
			// its format is like %"UINT32"-%"INT32" = (sentch32-sentth32)
			uint32_t venuech32 = 0;
			uint32_t venueth32 = 0;
			sscanf ( venue , "%"UINT32"-%"UINT32"", &venuech32, &venueth32 );
			// add vote tag into here
			char dataVal[256];
			// data=<uh48>,<adch32>,<adth32>>,<sentch32>
			// <sentth32>,<"title"|"venue"|"descr"|"error">,[0|1+] 
			//(c=contenthash,t=taghash)
			sprintf ( dataVal,"%"UINT64",%"UINT32",%"UINT32",%"UINT32",%"UINT32",%"INT32",venue,1",
				  uh48,tk->m_adch32,tk->m_adth32,
				  venuech32,venueth32,votePower);
			// put tag into this rdb
			//if ( ! sb->pushChar ( (char)RDB_TURKDB ) ) 
			//	return true;
			if ( ! sb->addTag ( fakeSite,
					    "turkvote" ,
					    now        ,
					    turkUser   ,
					    tk->m_turkIp     ,
					    dataVal    ,
					    gbstrlen(dataVal) ,
					    RDB_TURKDB ,
					    true ) )
				return true;
			// sometimes there are multiple sections with
			// different turktaghashes but the same content hash.
			// these are dups. and we should add them all!
			if ( ! addSupplementalVoteTags ( fakeSite,
							 now,
							 turkUser,
							 tk,
							 venuech32,
							 venueth32,
							 // vote prefix
							 "venu",
							 "venue-",
							 votePower,
							 sb))
				return true;
		}
	skipVenue:

		///////////////////
		//
		// add votes for description
		//
		///////////////////

		// scan the cgi inputs for tags we need to add
		for ( int32_t i = 0 ; i < m_r->m_numFields ; i++ ) {
			// breathe
			QUICKPOLL(TURKNICE);
			// not if rejected!
			if ( reject ) break;
			// get it
			char *name = m_r->m_fields[i];
			// . skip if not an initial checkbox value
			// . "i-venue-*-*", "i-title-*-*" or "i-desc-*-*"
			if ( strncmp(name,"i-",2) ) continue;
			// get its initial value
			char *ival = m_r->m_fieldValues[i];
			// . was it checked initially then?
			// . sometimes this is 9 which really means '0'
			//   see below for explanation
			bool initiallyChecked = ( ival[0] == '1');
			// ok, now see if it changed because the turk 
			// checked/unchecked the checkbox
			char *val = m_r->getValue ( name+2 );
			// assume not checked now
			bool nowChecked = false;
			// if empty checkbox does not submit!!
			if ( val && val[0] == '1' ) nowChecked = true;
			// assume this checkbox value was unchanged by the turk
			bool changed = false;
			// if it was set to '9' that means record it
			// regardless because it might now NOT be part
			// of the default event description because it
			// is above the event title, but if the event title
			// changes, then it will become part of the event
			// title. so we need to record the turk votes on
			// these "pretty" sentences. SENT_PRETTY. those
			// are the only ones that isIndexable() will return
			// true on basically, and thus not set the
			// EventDesc::m_dscore to 0.
			// . now accomplish the same thing with just
			//   an extra hidden tag
			// . 'r' means REcord. should we record the vote?
			//if ( ival[0]=='9' ) record = true;
			name[0]='r';
			int32_t record = m_r->getLong ( name, 0 );
			name[0]='i';
			// always record if not description vote
			if ( name[2] != 'd' ) record = 1;
			// we always add the title regardless. that way any
			// events from this same site will be guaranteed to
			// get the turkeventtitletaghash tag and know that
			// their format has been turked
			if ( name[2]=='t' && nowChecked ) changed = true;
			// skip if unchanged
			if ( initiallyChecked != nowChecked ) changed = true;
			// skip if unchanged and not title
			if ( ! record && ! changed ) continue;
			// assume this
			int32_t modVotePower = votePower;
			// HACK: skip is true just use a voting power of 0 to
			// indicate we did not change the default vote. because
			// we only want to compare the changes a turk made to
			// the default event description. that is what we
			// compare to other turks to decide if they have
			// an agreement of 50% in order to "pass" vs "fail"
			if ( ! changed ) modVotePower = 0;
			
			// cgi parm name is like "desc-ch32-th32"
			char *sub1 = name + 2;
			// skip to first hyphen
			for ( ; *sub1 && *sub1 != '-' ; sub1++ );
			// advance over that
			sub1++;
			// now find the end
			char *subend1 = sub1;
			// find end
			for ( ; *subend1 && *subend1 != '-'; subend1++ );
			// error?
			if ( *subend1 != '-' ) {
				g_errno = EBADENGINEER;
				return true;
			}
			
			// now get the "content hash" from the cgi parm name
			char *sub2 = subend1 + 1;
			char *subend2 = sub2;
			// find end
			for ( ; *subend2 ; subend2++ );
			
			// make it easy
			*subend1 = '\0';
			
			char *voteField = NULL;
			int32_t  favor     = 1;

			if      ( name[2] == 'd' &&   nowChecked ) 
				voteField = "descr";
			else if ( name[2] == 'd' && ! nowChecked ) {
				voteField = "descr";
				favor     = 0; // a "not" vote
			}
			// otherwise, do not add a tag
			else continue;
			
			// put tag into this rdb
			//if ( ! sb->pushChar ( (char)RDB_TURKDB ) ) 
			//	return true;
			

			//if ( strncmp(sub1,"155591542",9)==0 )
			//	log("poo");
			// and the content hash as ascii
			char dataVal[256];
			// . content hash based tag
			// . sub1 is contenthash32
			// . sub2 is taghash32
			sprintf ( dataVal ,"%"UINT64",%"UINT32",%"UINT32",%s,%s,%"INT32",%5s,%"INT32"",
				  uh48,tk->m_adch32,tk->m_adth32,
				  sub1,sub2,modVotePower,voteField,favor);

			if ( ! sb->addTag3 ( fakeSite,
					    "turkvote"     ,
					    now            ,
					    turkUser       ,
					    tk->m_turkIp         ,
					    dataVal        ,
					    RDB_TURKDB ) )
				return true;
		}

		Msg4 *m = &xd->m_msg4;
		
		char *metaList     = sb->getBufStart();
		int32_t  metaListSize = sb->length();

		// advance stage
		m_stage = 2;
		
		if ( ! m->addMetaList ( metaList ,
					metaListSize ,
					xd->m_coll ,
					this ,
					processLoopWrapper ,
					0 ) ) // TURK_NICE // m_niceness ) )
			// return false if blocked
			return false;
	}

	///////////////////////////
	// 
	// STAGE 2
	//
	///////////////////////////

	if ( m_stage == 2 ) {
		// advance stage
		m_stage = 3;
		// log the stages
		log("turk: flushing msg4 buffers for uh48=%"UINT64" url=%s",
		    uh48,url);
		// flush it. true = wait?
		if ( ! flushMsg4Buffers ( this , processLoopWrapper ) ) 
			// return false if blocked
			return false;
	}

	// wait for flush. may be forever if host is dead.

	// . do a quick reinject of just that particular url, 
	// . mark as accepted/rejected
	// . should disappear from the search results either way if you have
	//   a gbturked:0 term in your query

	///////////////////////////
	// 
	// STAGE 3
	//
	///////////////////////////
	
	if ( m_stage == 3 ) {
		m_stage = 4;
		CollectionRec *cr = g_collectiondb.getRec ( m_r );
		char *coll = cr->m_coll;
		// log the stages
		log("turk: calling msg7 inject on turked uh48=%"UINT64" url=%s",
		    uh48,url);
		//if ( ! xd->m_contentTypeValid ) { char *xx=NULL;*xx=0; }
		// make it call set3 on the xmldoc just once
		m_msg7.m_needsSet = true;
		if ( ! m_msg7.inject ( xd->getFirstUrl()->getUrl() ,
				       xd->m_firstIp ,
				       NULL, // content
				       0 , // contentLen
				       true , // recycle?
				       CT_HTML, // doesn't matter! cont is null
				       coll ,
				       false , // quickreply?
				       NULL, // usenrmame
				       NULL, // pwd
				       // we really don't want a page that
				       // takes forever to parse to hold
				       // up the query pipeline, so make
				       // sure this is 1! do use niceness 0
				       // for certain msg requests like msg2c
				       // which i already did!
				       1,//TURKNICE,
				       this,
				       processLoopWrapper ) )
			// return false if blocked
			return false;
	}

	///////////////////////////
	// 
	// STAGE 4
	//
	///////////////////////////

	// add the remaining urls to spiderdb i guess
	if ( m_stage == 4 ) {
		// log it now
		log("turk: done indexing injected doc uh48=%"UINT64" url=%s",
		    uh48,url);
		m_stage = 5;
		char *dom  = xd->getFirstUrl()->getDomain();
		int32_t  dlen = xd->getFirstUrl()->getDomainLen();
		char  c = dom[dlen];
		dom[dlen] = '\0';
		// reindex all OTHER pages that have an event with this
		// same address/date tag hash. they are likely made from
		// the same template
		sprintf(m_queryBuf,
			"gbadth32:%"UINT32" "
			"-gbuh48:%"UINT64" "
			"site:%s"
			,tk->m_adth32
			,uh48
			,dom);
		// put it back
		dom[dlen] = c;
		// dedup docids, and reindex all but the one we injected
		if ( ! m_msg1c.reindexQuery ( m_queryBuf ,
					      xd->m_coll,
					      0, // startNum ,
					      999999, // endNum   ,
					      this ,
					      processLoopWrapper ) )
			// return false if blocked
			return false;
	}

	// release the lock!!
	if ( m_stage == 5 ) {
		m_stage = 6;
		// force this on, we are host #0 so it should auto propagate
		// to all hosts in the network 
		if ( m_msg1c.m_numDocIdsAdded ) 
			g_conf.m_spideringEnabled = true;
		//m_msg12.m_uh48 = uh48;
		//m_msg12.m_firstIp = xd->getFirstIp();
		// so it doesn't dump core, return false if blocked
		//m_msg12.m_state    = this;
		//m_msg12.m_callback = processLoopWrapper;
		// and do not make this turk re-turk this same template hash
		if ( !g_templateCache.addRecord( 0 , //collnum
						 (char *)&tk->m_templateHash64,
						 NULL,
						 0,
						 0,
						 NULL) )
			log("turk: failed to add template key");
		//if ( ! m_msg12.removeAllLocks() ) return false;
		//g_lockerTable.removeKey(&uh48);
		// int16_tcut
		HashTableX *ht = &g_lockerTable;
		// . make sure this is the url they had locked
		// . prevents turk from turking a url they should not be
		// . try to find the lock that belongs to us
		int32_t tslot = ht->getSlot(&uh48);
		// assume no lock
		TurkLock *tk = NULL;
		// iterate over locks
		for ( ; tslot >= 0 ; tslot = ht->getNextSlot(tslot,&uh48) ) {
			// breathe
			QUICKPOLL(TURKNICE);
			// cast it
			tk = (TurkLock *)ht->getValueFromSlot(tslot);
			// stop if us
			if ( tk->m_tuid64 == tuid64 ) break;
		}
		// remove it
		if ( tk ) ht->removeSlot ( tslot );
		// info on turk's ip
		//char *turkUser  = m_r->getString("turkuser",NULL);
		// log it
		log("turk: releasing lock on uh48=%"UINT64" for turk=%s url=%s",
		    uh48,turkUser,url);
	}

	// . give him back his search results page now
	// . might have to propagate his query in hidden input tags
	// . maybe change XmlDoc::getTurkForm() to do that
	// . in fact i think there is already code to do that with the
	//   search parms already for generating the next 10 links, etc.
	// . it's the PROPAGATE descriptor...
	// . or better yet maybe zak should just re-do the query on his end
	//   when we deliver a simple response...

	return true;
}


// . deal with duplicate sentences
// . when we vote on a title or venue, often, there are other sentences
//   that are exactly the same but have a different tag hash
// . so now we must add a vote tag for them as well because it isn't clear
//   if either is the true one...
bool Msg1e::addSupplementalVoteTags ( char *furl ,
				      int32_t now,
				      char *turkUser,
				      TurkLock *tk ,
				      uint32_t mainSentch32,
				      uint32_t mainSentth32,
				      // votePrefix is "titl" or "venu"
				      char *votePrefix ,
				      // "i-desc-" or "venue-"
				      char *hiddenPrefix , 
				      int32_t votePower,
				      // store tagdb recs into here
				      SafeBuf *sb ) { 
	char dbuf[1024];
	HashTableX dedup;
	dedup.set(4,0,32,dbuf,1024,false,TURKNICE,"supdbuf");
	int32_t count = 2;

	int32_t hplen = gbstrlen(hiddenPrefix);

	for ( int32_t i = 0 ; i < m_r->m_numFields ; i++ ) {
		// breathe
		QUICKPOLL(TURKNICE);
		// get it
		char *name = m_r->m_fields[i];
		// . skip if not an initial checkbox value
		// . "i-venue-*-*", "i-title-*-*" or "i-desc-* or "venue-"
		if ( strncmp(name,hiddenPrefix,hplen) ) continue;
		// decompose into ch32-th32
		uint32_t sentch32 = 0;
		uint32_t sentth32 = 0;
		sscanf(name+hplen,"%"UINT32"-%"UINT32"", &sentch32,&sentth32);
		// skip if ch32 does not match
		if ( sentch32 != mainSentch32 ) continue;
		// skip if taghash already matches
		if ( sentth32 == mainSentth32 ) continue;
		// make sure this is not a dup for us
		if ( dedup.isInTable ( &sentth32 ) ) continue;
		// add it
		if ( ! dedup.addKey ( &sentth32 ) ) return false;
		// ok, we gotta add it!
		char dataVal[256];
		// data=<uh48>,<adch32>,<adth32>>,<sentch32>
		// <sentth32>,
		// <"title"|"venue"|"descr"|"error">,
		// [0|1+] 
		//(c=contenthash,t=taghash)
		// titl2-4
		sprintf ( dataVal,"%"UINT64",%"UINT32",%"UINT32",%"UINT32",%"UINT32","
			  "%"INT32",%s%"INT32",1",
			  tk->m_uh48,
			  tk->m_adch32,
			  tk->m_adth32,
			  sentch32,
			  sentth32, // the new one
			  votePower,
			  votePrefix,
			  count);
		// put tag into this rdb
		//if ( ! sb->pushChar ( (char)RDB_TURKDB ) ) 
		//	return true;
		if ( ! sb->addTag(furl,
				  "turkvote" ,
				  now        ,
				  turkUser   ,
				  tk->m_turkIp     ,
				  dataVal    ,
				  gbstrlen(dataVal),
				  RDB_TURKDB ,
				  true ) )
			return true;
		// limit to up to 3 supplemental votes
		if ( count == 4 ) break;
		// inc for next
		count++;
		// only do one now
		//break;
	}

	// only do padding for superturk since non-superturks can't re-vote
	// on the same event! so no point in overriding old votes
	if ( ! tk->m_isSuperTurk ) return true;

	// if no count add null 2ndary title to remove any
	// old one that might be in there
	for ( ; count <= 4 ; count++ ) {
		// ok, we gotta add it!
		char dataVal[256];
		// data=<uh48>,<adch32>,<adth32>>,<sentch32>
		// <sentth32>,
		// <"title"|"venue"|"descr"|"error">,
		// [0|1+] 
		//(c=contenthash,t=taghash)
		sprintf ( dataVal,"%"UINT64",%"UINT32",%"UINT32",%"UINT32",%"UINT32","
			  "%"INT32",%s%"INT32",1",
			  tk->m_uh48,tk->m_adch32,tk->m_adth32,
			  (uint32_t)0,//titlech32,
			  (uint32_t)0,//sentth32,
			  (uint32_t)0,//votePower,
			  votePrefix,
			  (uint32_t)count);
		// put tag into this rdb
		//if ( ! sb->pushChar ( (char)RDB_TURKDB ) ) 
		//	return true;
		if ( ! sb->addTag(furl,
				  "turkvote" ,
				  now        ,
				  turkUser   ,
				  tk->m_turkIp     ,
				  dataVal    ,
				  gbstrlen(dataVal) ,
				  RDB_TURKDB ,
				  true ) )
			return true;
	}
	return true;
}


////////////////////
//
// captcha stuff
//
////////////////////

// from http://code.google.com/apis/recaptcha/docs/display.html
bool printCaptcha2 ( SafeBuf *sb ) {

	return sb->safePrintf ("<script type=\"text/javascript\""
			       "src=\"http://www.google.com/recaptcha"
			       "/api/challenge?k=%s\">\n"
			       "</script>\n"
			       "<noscript>\n"
			       "<iframe src=\"http://www.google.com/recaptcha/"
			       "api/noscript?k=%s\" "
			       "height=\"300\" width=\"500\" "
			       "frameborder=\"0\"></iframe><br>\n"
			       "<textarea name=\"recaptcha_challenge_field\" "
			       "rows=\"3\" cols=\"40\">\n"
			       "</textarea>\n"
			       "<input type=\"hidden\" name=\"recaptcha_"
			       "response_field\" value=\"manual_challenge\">\n"
			       "</noscript>\n"
			       "</form>\n"
			       "</html>\n" ,
			       g_pubKey,
			       g_pubKey) ;
}

bool printCaptcha ( State61 *st ) {

	// int16_tcuts
	SafeBuf *sb = &st->m_sb;

	// did we get a turk user? should be there!
	char *turkUser  = st->m_r.getString("evaluser",NULL);
	char *turkIpStr = st->m_r.getString("evalip",NULL);

	if ( ! sb->safePrintf( "<html>\n"
			       "Please answer the following captcha: <br>" 
			       "<br>"
			       "<form method=POST action=/eval>\n"
			       "<input type=hidden name=evaluser value=\"%s\">"
			       "<input type=hidden name=evalip value=\"%s\">"
			       "<input type=hidden name=c value=\"%s\">"
			       "<input type=hidden name=captchasubmit value=1>"
			       ,turkUser
			       ,turkIpStr
			       ,st->m_coll
			       ))
		return false;

	char *qq = st->m_r.getString("q",NULL);
	if (qq&&!sb->safePrintf("<input type=hidden name=q value=\"%s\">",qq))
		return false;

	return printCaptcha2 ( sb );
}

bool isTurkBanned ( int64_t *tuid64 , int32_t turkIp ) {
	// is the turkusername banned?
	if ( tuid64 ) {
		Tag *btag = (Tag *)s_banTable.getValue ( tuid64 );
		if ( btag ) return true;
	}
	if ( turkIp ) {
		int32_t ip64 = (int64_t)turkIp;
		Tag *btag = (Tag *)s_banTable.getValue ( &ip64 );
		if ( btag ) return true;
	}
	return false;
}

bool isSuperTurk ( char *turkUser ) {
	// get the list of superturks
	char *tt = g_conf.m_superTurks;
	// length of it
	int32_t tulen = gbstrlen(turkUser);
	// scan for turkuser
	char *p = tt;
	char lastChar = ' ';
	for ( ; *p ; p++ ) {
		// if last char was not space or comma then skip
		if ( lastChar != ' ' && lastChar != ',' ) {
			lastChar = *p;
			continue;
		}
		// skip if no match
		if ( *p != turkUser[0] ) {
			lastChar = *p;
			continue;
		}
		// check it
		if ( strncmp(p,turkUser,tulen) ) {
			lastChar = *p;
			continue;
		}
		// make sure on boundary
		if ( p[tulen] &&
		     p[tulen] != '\r' &&
		     p[tulen] != '\n' &&
		     p[tulen] != ' ' &&
		     p[tulen] != ',' ) {
			lastChar = *p;
			continue;
		}
		// got a match
		return true;
	}
	return false;
}
