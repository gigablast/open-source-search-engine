/*

================
IGOOGLE
================

google editor of our eventguru.xml file:
http://code.google.com/apis/gadgets/docs/tools.html#GGE

the google-hosted url:
http://hosting.gmodules.com/ig/gadgets/file/108415847701685077985/eventguru.xml

the content of that url:

===============

<?xml version="1.0" encoding="UTF-8"?>
<Module>
  <ModulePrefs width="400" height="300" thumbnail="http://www.eventguru.com/eventguru120x60.png" description="Show events near you." screenshot="http://www.eventguru.com/googlescreenshot.png" title_url="http://www.eventguru.com/?ig=0" title="Events" author="Event Guru" author_email="flurbit@mail.com" category="news" category2="lifestyle"/>
  <Content view="home" type="html">
    <![CDATA[<body onLoad="window.location.href='http://www.eventguru.com/?ig=1&from='+encodeURIComponent(window.location.href);"></body>]]>
  </Content>
  <Content view="canvas" type="url" href="http://www.eventguru.com/?ig=1" />
</Module>

================

Use "nocache=1" for testing i guess.

http://www.google.com/ig?nocache=1
=================

List of gadgets I authored:

http://code.google.com/igoogle/dashboard/

==================

Google Gadget Forum:

https://groups.google.com/forum/?fromgroups#!forum/Google-Gadgets-API

==================

iGoogle Developer Blog:

http://igoogledeveloper.blogspot.com/
*/


#include "gb-include.h"

#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Stats.h"
#include "Statsdb.h"
#include "Ads.h"
#include "Query.h"
#include "Speller.h"
#include "Msg40.h"
#include "Pages.h"
#include "Highlight.h"
#include "SearchInput.h"
#include <math.h>
#include "SafeBuf.h"
#include "iana_charset.h"
#include "Pos.h"
#include "Bits.h"
#include "AutoBan.h"
#include "sort.h"
#include "LanguageIdentifier.h"
#include "LanguagePages.h"
#include "LangList.h"
#include "CountryCode.h"
#include "Unicode.h"
#include "XmlDoc.h" // GigabitInfo class
#include "PageTurk.h"
#include "Facebook.h"

#define GRAYSUBTEXT "#606060"

#define GRAD3 "#55aaee"
#define GRAD4 "#003366"
#define GRAD1 "#e0e0e0"
#define GRAD2 "#606060"
#define GRADMIDDLE "#b6b6b6"

#define GRAD5 "#3e3e3e"
#define GRAD6 "#000000"

#define GRADFONT "black"
#define GRADFONTCOMP "white"

#define SITTING "/gurusitting.png"
#define SITTINGDXORIG ((int32_t)145)
#define SITTINGDYORIG ((int32_t)114)
#define SITTINGDX128 ((int32_t)128)
#define SITTINGDY128 ((int32_t)(((128.0*(float)SITTINGDYORIG))/((float)SITTINGDXORIG)))
#define SITTINGDX64 ((int32_t)64)
#define SITTINGDY64 ((int32_t)(((64.0*(float)SITTINGDYORIG))/((float)SITTINGDXORIG)))
#define SITTINGDX96 ((int32_t)96)
#define SITTINGDY96 ((int32_t)(((96.0*(float)SITTINGDYORIG))/((float)SITTINGDXORIG)))

#define GURUPNG "/eventguru.png"
#define GURUDXORIG 204
#define GURUDYORIG 143
#define GURUDX ((int32_t)204)
#define GURUDY ((int32_t)(((204.0*(float)GURUDYORIG))/((float)GURUDXORIG)))
#define GURUDX128 ((int32_t)128)
#define GURUDY128 ((int32_t)(((128.0*(float)GURUDYORIG))/((float)GURUDXORIG)))
#define GURUDX96 ((int32_t)96)
#define GURUDY96 ((int32_t)(((96.0*(float)GURUDYORIG))/((float)GURUDXORIG)))
#define GURUDX64 ((int32_t)64)
#define GURUDY64 ((int32_t)(((64.0*(float)GURUDYORIG))/((float)GURUDXORIG)))
#define GURUDX32 ((int32_t)32)
#define GURUDY32 ((int32_t)(((32.0*(float)GURUDYORIG))/((float)GURUDXORIG)))



static void gotSpellingWrapper ( void *state ) ;
static void gotResultsWrapper  ( void *state ) ;
static void gotAdsWrapper      ( void *state ) ;
static void gotState           ( void *state ) ;
static bool gotResults         ( void *state ) ;

bool printCitiesFrame ( SafeBuf &sb ) ;
bool printPopularInterestsFrame ( SafeBuf &sb , class State7 *st ) ;
bool printWidgetFrame ( SafeBuf &sb , class State7 *st ) ;

static bool printEventAddress ( SafeBuf &sb , char *addr , SearchInput *si ,
			 double *lat , double *lon , bool xml ,
			 float zipLat ,
			 float zipLon ,
			 double eventGeocoderLat,
			 double eventGeocoderLon ,
			 char *eventBestPlaceName ,
				Msg20Reply *mr ) ;
bool printCategoryTable ( SafeBuf &sb , SearchInput *si ) ;
bool printCategoryInputs ( SafeBuf &sb , SearchInput *si ) ;
char *getEventImage    ( class Msg20Reply *mr ) ;
bool printEventTitle   ( SafeBuf &sb , class Msg20Reply *mr ,
			 class State7 *st ) ;
bool printEventSummary ( SafeBuf &sb , class Msg20Reply *mr , int32_t width ,
			 int32_t minusFlags , int32_t requiredFlags ,
			 class State7 *st , ExpandedResult *er ,
			 int32_t maxChars );
bool printEventCountdown2 ( SafeBuf &sb ,
			    SearchInput *si,
			    int32_t nowUTC ,
			    int32_t timeZoneOffset ,
			    char useDST,
			    int32_t nextStart ,
			    int32_t nextEnd ,
			    int32_t prevStart ,
			    int32_t prevEnd ,
			    bool storeHours ,
			    bool onlyPrintIfSoon ) ;
bool printEventCountdown ( SafeBuf &sb , Msg20Reply *mr , Msg40 *msg40 ,
			   ExpandedResult *er , bool onlyPrintIfSoon ,
			   bool isXml , class State7 *st ) ;
//bool printEventCachedUrl ( SafeBuf &sb , Msg20Reply *mr , Msg20 *m ,
//			   char *qe , char *coll ) ;
bool printEventTags ( Msg20Reply *mr , SafeBuf& sb ) ;
void printCountdown3 ( int32_t togo , SafeBuf& sb , bool onlyPrintIfSoon ) ;
bool printLocalTime ( SafeBuf &sb , class State7 *st ) ;
bool printCalendar ( SafeBuf &sb , class State7 *st );
bool printBalloon ( SafeBuf &sb,SearchInput *si, char letter,int32_t balloonId ) ;
bool printPageContent ( SafeBuf &sb , class State7 *st ) ;
bool printTodRange ( SafeBuf &sb , class State7 *st , ExpandedResult *er ) ;

static bool sendErrorReply7 ( class State7 *st ) ;

//static char *printPost ( char *p , char *pend, class State7 *st , char *name , 
//			 int32_t s , int32_t n , char *qe , char *button ) ;

class State7 {
public:
	bool         m_printedBox;
	char        *m_coll;
	void        *m_emailState;
	void      (* m_emailCallback)(void *emailState);
	SafeBuf     *m_emailResultsBuf;
	SafeBuf     *m_emailLikedbListBuf;
	SafeBuf      m_likedbListBuf;
	// used by PageSubmit.cpp:
	SafeBuf     *m_providedBuf;
	void        *m_providedState;
	void      (* m_providedCallback)(void *providedState);
        Query        m_q;
	SearchInput  m_si;
	Msg40        m_msg40;
	Msgfb        m_msgfb;
	Msgfc        m_msgfc;
	TcpSocket   *m_socket;
	Msg0         m_msg0;
	int64_t    m_startTime;
	Ads          m_ads;
	int32_t         m_numResultsPrinted;
	bool         m_gotAds;
	bool         m_gotResults;
	char         m_request[256];
	char         m_spell  [MAX_FRAG_SIZE]; // spelling recommendation
	int32_t         m_numNarrows;
	char         m_narrow [MAX_FRAG_SIZE]; // narrow recommendation
	bool         m_gotSpell;
	int32_t         m_errno;
	bool         m_isLocal;
	bool         m_isRTL;
	char         m_doRobotChecking;
	Query        m_qq3;
        int32_t         m_numDocIds;
	int64_t    m_took; // how long it took to get the results
	//SafeBuf      m_interestCookies;
	char         m_needSave;
	bool         m_didSave;
	HttpRequest  m_hr;
	//char *m_myLocation;
	//class Msg1d *m_msg1d;
};

static bool printMetaContent ( Msg40 *msg40 , int32_t i , State7 *st , 
			       SafeBuf& sb , bool inXml ) ;

static bool gotFriends ( void *state );

static void gotFriendsWrapper ( void *state ) {
	gotFriends ( state );
}

bool sendPageEvents2 ( TcpSocket *s , 
		       HttpRequest *hr ,
		       SafeBuf *emailResultsBuf,
		       SafeBuf *emailLikedbListBuf,
		       void *state  ,
		       void (* emailCallback)(void *state) ,
		       SafeBuf *providedBuf ,
		       void *providedState ,
		       void (* providedCallback)(void *state) ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "msg" will be inserted into the access log for this request
bool sendPageEvents ( TcpSocket *s , HttpRequest *hr ) {
	return sendPageEvents2 ( s , hr , 
				 NULL , NULL , NULL , NULL , 
				 NULL , NULL , NULL );
}

static void doneSendingEmailWrapper ( void *state ) {
	EmailState *es = (EmailState *)state;
	// note it
	log("emailer: done sending email. deleting state");
	// save this
	TcpSocket *socket = es->m_socket;
	// must have been an error i guess!
	mdelete ( es , sizeof(EmailState) , "es2" );
	delete  ( es );
	// return a reply then!
	char *reply = "Email has been sent!";
	g_httpServer.sendDynamicPage ( socket ,
				       reply,
				       gbstrlen(reply),
				       -1); // cachetime
}

// from PageSubmit.cpp
bool sendPageSubmit ( TcpSocket *s , HttpRequest *hr ) ;
bool sendPageFormProxy ( TcpSocket *s , HttpRequest *hr ) ;

// for Facebook.cpp's Emailer class...
bool sendPageEvents2 ( TcpSocket *s , 
		       HttpRequest *hr ,
		       // parms for Emailer class in Facebook.cpp:
		       SafeBuf *emailResultsBuf,
		       SafeBuf *emailLikedbListBuf,
		       void *emailState  ,
		       void (* emailCallback)(void *state) ,
		       SafeBuf *providedBuf ,
		       void *providedState ,
		       void (* providedCallback)(void *state) ) {

	// ?formurl=... means to send the event form submission tool page
	if ( hr->getLong("form",0) ) 
		return sendPageSubmit ( s , hr );

	// . re-submits this HTTP REQUEST to the url specified by "formproxyto"
	// . being a form submission proxy for event forms allows us to
	//   insert the event image into the HTTP REQUEST before delivering
	//   it back to the external event form server. currently, this is
	//   really the sole purpose of "formproxyto".
	// . the proper insertion point is marked in the POST data of the
	//   http request typically with a "filename=\"" indicator. it
	//   represents an <input type=file> tag submission.
	if ( hr->getString("formproxyto",NULL) ) 
		return sendPageFormProxy ( s , hr );


	char *coll = hr->getString("c");
	if ( ! coll ) coll = g_conf.m_defaultColl;
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	if ( ! cr ) log("shit shit shit");
	else coll = cr->m_coll;


	//
	// are we being requested to email a file????
	//
	if ( hr->getString("sendemailfromfile",0) ) {
		// get associated fbid so we can get the file to email
		int64_t fbId = hr->getLongLong("usefbid",0LL);
		// int16_tcut
		char *dir = g_hostdb.m_dir;
		// make the filename
		char filename[512];
		sprintf(filename,"%s/html/email/email.%"UINT64".html", dir, fbId );
		// log it
		log("email: emailing file %s", filename);
		// alloc the state
		EmailState *es;
		try { es = new (EmailState); }
		catch ( ... ) {
			g_errno = ENOMEM;
			char *msg = "could not alloc mem to email";
			return g_httpServer.sendErrorReply(s,500,msg);
		}
		mnew ( es , sizeof(EmailState) , "es2" );

		//
		// read the buf in from disk
		//
		if(!es->m_emailResultsBuf.fillFromFile(filename))goto error;

		//
		// and we need the likedb list too!
		// sometimes this does not exist
		//
		sprintf(filename,"%s/html/email/likedblist.%"UINT64"", dir,fbId);
		es->m_emailLikedbListBuf.fillFromFile(filename);
		// clear error if it did not exist
		g_errno = 0;

		// prepare it
		es->m_sendSingleEmail = true;
		es->m_singleCallback = doneSendingEmailWrapper;
		es->m_singleState    = es;
		es->m_socket         = s;
		// for adding likes to likedb using msg0 in Facebook.cpp
		es->m_coll = coll;
		// . send it. it will call our callback in emailScan() when
		//   it's done
		// . returns false if blocked, true otherwise
		if ( ! g_emailer.sendSingleEmail ( es ,fbId ) ) return false;
	error:
		// log it
		log("emailer: had error sending email from file: %s",
		    mstrerror(g_errno));
		// must have been an error i guess!
		mdelete ( es , sizeof(EmailState) , "es2" );
		delete  ( es );
		return g_httpServer.sendErrorReply(s,500,"emailer had error");
	}


	// . need to pre-query the directory first to get the sites to search
	//   this will likely have just been cached so it should be quick
	// . then need to construct a site search query
	int32_t rawFormat = hr->getLong("xml", 0); // was "raw"
	// make a new state
	State7 *st;
	try { st = new (State7); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("query: Query failed. "
		    "Could not allocate %"INT32" bytes for query. "
		    "Returning HTTP status of 500.",(int32_t)sizeof(State7));
		g_stats.m_numFails++;
		if ( emailCallback ) return true;
		return g_httpServer.sendQueryErrorReply
			(s,500,mstrerror(g_errno),
			 rawFormat, g_errno, "Query failed.  "
			 "Could not allocate memory to execute a search.  "
			 "Please try later." );
	}
	mnew ( st , sizeof(State7) , "PageEvents2" );
	// copy yhits
	st->m_hr.copy ( hr );
	// save this
	st->m_needSave = false;
	st->m_didSave = false;
	st->m_socket = s;
	st->m_coll = coll;//st->m_hr.getString("c");
	//if ( ! st->m_coll ) st->m_coll = g_conf.m_defaultColl;
	st->m_emailResultsBuf    = emailResultsBuf;
	st->m_emailLikedbListBuf = emailLikedbListBuf;
	st->m_emailState         = emailState;
	st->m_emailCallback      = emailCallback;

	st->m_providedBuf        = providedBuf;
	st->m_providedState      = providedState;
	st->m_providedCallback   = providedCallback;

	// before we set searchinput, set Msgfb so it can construct the
	// query from the list of friends if the user is trying to see
	// what events his friends have liked or planned on going to.
	// we now needs this all the time so we can render the user's fb pic
	// and print the "log out" link. if the cookie does not provide
	// an "fbid" and there is no "&code=" it should not block or take
	// any time. so it should not affect speed of non-facebook users
	/*
	if ( hr->getBool("friendslike"   ,false) ||
	     hr->getBool("friendsgoingto",false) ||
	     hr->getBool("hiddenbyme"    ,false) ||
	     hr->getBool("addedbyme"     ,false) ||
	     // or if tagging an event, try to get facebook info
	     hr->getString("tag",NULL) ||
	     // if they push the login button, facebook gives us a "&code="
	     hr->getString("code",NULL) ) {
	*/
	//char *coll = hr->getString("c",NULL);
	//if ( ! coll ) coll = g_conf.m_defaultColl;
	// . what friend ids have liked/attended events
	// . their facebook id should be in the cookie once they login
	if ( ! st->m_msgfb.getFacebookUserInfo ( &st->m_hr ,
						 st->m_socket,
						 st->m_coll,
						 st , 
						 "", // redirPath
						 gotFriendsWrapper ,
						 0 )) // niceness
		// return false if we blocked
		return false;
	//}

	return gotFriends ( st );
}

bool sendErrorReply2 ( State7 *st ) { // , SafeBuf &sb ) {
	// sanity check
	if ( ! g_errno ) { char *xx=NULL;*xx=0; } 
	log("query: sending back results error = %s",mstrerror(g_errno) );
	// extract the TcpSocket from the "state" data
	TcpSocket *s = st->m_socket;
	// nuke State7 class
	delete (st);
	// the 0 means browser caches the page for however int32_t its set
	//g_stats.m_numFails++;
	return g_httpServer.sendErrorReply ( s , 500,mstrerror(g_errno) );
}

static bool addedTags ( void *state ) ;

static void addedTagsWrapper ( void *state ) {
	addedTags ( state );
}

static bool getResults ( void *state );

bool gotFriends ( void *state ) {

	State7 *st = (State7 *)state;
	HttpRequest *hr = &st->m_hr;

	//
	// BEGIN SPECIAL TAGGING CODE
	//
	char *iconIdStr = hr->getString("iconid",NULL);
	char *iconSrc   = hr->getString("iconsrc",NULL);
	uint64_t eventHash64 = hr->getLongLong("evh64",0LL);

	// skip the tagging if we can
	if ( ! eventHash64 || ! iconSrc || ! iconIdStr  )
		return getResults ( st );

	// try to get facebook user id. return browser id if no
	// facebook id available. returns 0 if neither available.
	int64_t facebookId = st->m_msgfb.m_fbId;
	// we assign a random userid if none in cookie. we call
	// set-cookie: userid=xxxxx;expires=1000000000; on it so it
	// should NEVER expire. 32 years...
	int64_t eventGuruId = hr->getLongLongFromCookie("userid",0LL);
	// sanity constraint
	if ( eventGuruId < 0 ) eventGuruId = 0;
	if ( facebookId  < 0 ) facebookId  = 0;
	// if facebook is there do not double add eventguruid
	int64_t userId = 0;
	if ( eventGuruId ) userId = eventGuruId;
	if ( facebookId  ) userId = facebookId;

	if ( ! userId ) {
		log("events: can not tag without being logged in. id "
		    "required.");
		g_errno = EBADENGINEER;
		return sendErrorReply2 ( st ); 
	}
	// ensure tag legit
	int32_t rsvp = 0;//LB_NONE;
	bool neg = false;

	if      ( strstr(iconSrc,"/thumbdown") )  rsvp = LF_HIDE;
	else if ( strstr(iconSrc,"/redthumbd") ) {rsvp = LF_HIDE; neg=true;}
	else if ( strstr(iconSrc,"/thumb"    ) )  rsvp = LF_LIKE_EG;
	else if ( strstr(iconSrc,"/redthumb" ) ) {rsvp = LF_LIKE_EG; neg=true;}
	else if ( strstr(iconSrc,"/hide"     ) )  rsvp = LF_HIDE;
	else if ( strstr(iconSrc,"/redhide"  ) ) {rsvp = LF_HIDE; neg=true;}
	else if ( strstr(iconSrc,"/going"    ) )  rsvp = LF_GOING;
	else if ( strstr(iconSrc,"/redgoing" ) ) {rsvp = LF_GOING; neg=true;}
	else if ( strstr(iconSrc,"/accept"   ) ) {rsvp = LF_ACCEPT; }
	else if ( strstr(iconSrc,"/redaccept") ) {rsvp = LF_ACCEPT; neg=true;}
	else if ( strstr(iconSrc,"/reject"   ) ) {rsvp = LF_REJECT; }
	else if ( strstr(iconSrc,"/redreject") ) {rsvp = LF_REJECT; neg=true;}

	if ( ! rsvp ) {
		log("events: bad iconSrc=%s",iconSrc);
		g_errno = EBADENGINEER;
		return sendErrorReply2 ( st ); 
	}

	int64_t docId = hr->getLongLong("d",0LL);
	int32_t      gbeventId = hr->getLong("evid",0);
	int32_t      start_time = hr->getLong("starttime",0);

	if ( userId == eventGuruId )
		rsvp |= LF_ISEVENTGURUID;

	// use new likedb
	if ( ! st->m_msgfc.addLikedbTag ( userId,
					  docId, 
					  gbeventId, 
					  eventHash64, 
					  start_time,
					  rsvp ,
					  neg ,
					  st->m_coll,
					  st ,
					  addedTagsWrapper))
		return false;
	
	return addedTags ( st );
}

// send back javascript for browser to run after tagging this event
bool addedTags ( void *state ) {
	State7 *st = (State7 *)state;
	// get the tag
	//HttpRequest *hr = &st->m_hr;

	//int64_t facebookId = st->m_msgfb.m_fbId;

	//char *iconIdStr = hr->getString("iconid",NULL);
	//char *iconSrc   = hr->getString("iconsrc",NULL);

	//int64_t eventHash64 = hr->getLongLong("ev",0LL);

	// get the search result id from the iconIdStr
	//char *p = iconIdStr;
	//for ( ; *p && ! is_digit(*p) ; p++);
	//int32_t iconId = -1;
	//if ( *p ) iconId = atol(p);

	//SafeBuf sb;
	//sb.safePrintf("reloadFrame(false);\n");

	// now resume with getting the events
	return getResults ( st );

	/*
	// extract the TcpSocket from the "state" data
	TcpSocket *sock = st->m_socket;
	// send that back so browser runs it now!
	mdelete ( st , sizeof(State7) , "PageEvents2" );
	delete ( st );
	return g_httpServer.sendDynamicPage ( sock, 
					      "",//sb.getBufStart() ,
					      0,//sb.length(), 
					      0 );
	*/
}

void didSaveWrapper ( void *state ) {
	Msgfb *msgfb = (Msgfb *)state;
	// get our state7 from that?
	State7 *st = (State7 *)msgfb->m_tmp;
	// mark as gotten
	st->m_didSave = true;
	// log the error first
	if ( g_errno ) log("query: save fbrec: %s.",mstrerror(g_errno));
	// clear any error cuz ads aren't needed
	else log("query: fbrec save completed after blocking");
	g_errno = 0;
	gotState (st);
}

bool getResults ( void *state ) {

	State7 *st = (State7 *)state;
	HttpRequest *hr = &st->m_hr;

	int32_t rawFormat = hr->getLong("xml", 0); // was "raw"
	// extract the TcpSocket from the "state" data
	TcpSocket *s = st->m_socket;
	// . parse it up
	// . this returns false and sets g_errno and, maybe, g_msg on error
	SearchInput *si = &st->m_si;
	if ( ! si->set ( s , hr , &st->m_q , &st->m_msgfb ) ) {
		return sendErrorReply7 ( st );
		/*
		if ( g_errno && g_errno != ENOPERM ) 
			log("query: Query failed 5: %s.",mstrerror(g_errno));
		mdelete ( st , sizeof(State0) , "PageResults2" );
		delete ( st );
		if ( g_errno != ENOPERM && g_errno != ENOCOLLREC ) 
			g_stats.m_numFails++;
		int32_t status = 500;
		if (g_errno == ETOOMANYOPERANDS ||
		    g_errno == EBADREQUEST ||
		    g_errno == ENOPERM ||
		    g_errno == ENOCOLLREC) status = 400;
		return g_httpServer.sendQueryErrorReply
			(s,status,mstrerror(g_errno), rawFormat,g_errno, 
			 "Query failed.");
		*/
	}


	// crap, facebook uses code!! so use "accesscode" now...
	int32_t  codeLen = 0;
	char *code = hr->getString("accesscode", &codeLen, NULL);
	// allow up to 1000 results per query for paying clients
	CollectionRec *cr = si->m_cr;
	int32_t maxpp = cr->m_maxSearchResultsPerQuery ;
	if ( codeLen > 0 && strcmp(code,"gbfront") != 0) {
		int32_t tmax = cr->m_maxSearchResultsPerQueryForClients;
		//if ( tmax < maxpp ) tmax = maxpp;
		if ( si->m_docsWanted > tmax ) si->m_docsWanted = tmax;
	}
	// limit here for non-paying clients
	else if ( si->m_docsWanted > maxpp )
		si->m_docsWanted = maxpp;

        st->m_numDocIds = si->m_docsWanted;

	// if they don't have the ever changing key, they're probably a bot
	/*
	if ( (!si->m_isAssassin||si->m_isMasterAdmin) &&
	     si->m_raw == 0 && si->m_siteLen <= 0 &&
	     si->m_sitesLen <= 0 ) {
		// if there and robot checking on, check it
		if ( cr && cr->m_doRobotChecking && 
		     ! g_httpServer.hasPermission ( s->m_ip , 
						    hr, 
						    si->m_qbuf1 , 
						    si->m_qbufLen1 ,
						    si->m_firstResultNum,
						    si->m_docsWanted ) ) {
			// mark it in the log
			g_msg = " (error: robot denied)";
			mdelete ( st , sizeof(State7) , "PageEvents2" );
			delete ( st );
			// . if permission was denied present root page
			// . it should put the query in the query box
			g_stats.m_numSuccess++;
			return sendPageRoot ( s , hr );
		}
	}
	*/

	// watch out for cowboys
	if ( si->m_firstResultNum >= si->m_maxResults && ! si->m_emailFormat ){
		char buf[256];
		sprintf ( buf, "<html><b>Error. Only up to %"INT32" search results "
			  "permitted, cowbot.</b></html>\n", si->m_maxResults);
		mdelete ( st , sizeof(State7) , "PageEvents2" );
		delete ( st );
		g_stats.m_numSuccess++;
		return g_httpServer.sendDynamicPage ( s, buf, gbstrlen(buf),0);
	}

	// save state in TcpSocket's m_tmp ptr for debugging. in case 
	// we lose our string of control and Msg40::getResults() never 
	// comes back.
	if ( s ) s->m_tmp = (char *)st;
	// add query stat
	st->m_startTime = gettimeofdayInMilliseconds();
	// reset
	st->m_errno = 0;

	// save full request
	int32_t  rqlen = hr->getPathLen();
	if ( rqlen >= 256 ) rqlen = 255;
	char *pp1 = st->m_request;
	char *pp2 = hr->getPath();
	// . when parsing cgi parms, HttpRequest converts the &'s to \0's so
	//   it can avoid having to malloc a separate m_cgiBuf
	// . now it also converts ='s to 0's, so flip flop back and forth
	char dd = '=';
	for ( int32_t i = 0 ; i < rqlen ; i++ , pp1++, pp2++ ) {
		if ( *pp2 == '\0' ) { 
			*pp1 = dd;
			if ( dd == '=' ) dd = '&';
			else             dd = '=';
			continue;
		}
		*pp1 = *pp2;
	}
	//strncpy ( st->m_request , hr->getPath () , rqlen );
	st->m_request [ rqlen ] = '\0';

	// debug msg
	log ( LOG_DEBUG , "query: Getting search results for q=%s",
	      st->m_si.m_displayQuery);

	//st->m_socket  = s;
	st->m_isLocal = hr->isLocal();
	// assume we'll block
	st->m_numResultsPrinted = 0;
	st->m_gotResults = false;
	st->m_gotAds     = false;
	st->m_gotSpell   = false;
	st->m_isRTL      = false;
	st->m_didSave    = false;

	if ( g_conf.m_doAutoBan && ! si->m_emailFormat ) {
		int32_t ip = s->m_ip;
                int32_t uipLen;
		char *uip = hr->getString("uip", &uipLen, NULL);
		char testBufSpace[2048];
		SafeBuf testBuf(testBufSpace, 1024);
		if(!g_autoBan.hasPerm(ip, 
				      code, codeLen, 
				      uip, uipLen, 
				      s, 
				      hr,
				      &testBuf)) {
			g_msg = " (error: autoban rejected.)";
                        mdelete(st, sizeof(State7), "PageEvents2");
                        delete st;
			if( testBuf.length() > 0 ) {
				g_stats.m_numSuccess++;
				return g_httpServer.sendDynamicPage(s, 
							testBuf.getBufStart(),
						        testBuf.length(),  0);
			}
			//log(LOG_WARN,"AutoBan: Query denied. %s %s", 
			//uip,iptoa(s->m_ip));
			g_stats.m_numSuccess++;
			return g_httpServer.sendQueryErrorReply
				(s,402, mstrerror(EBUYFEED),
				 rawFormat, g_errno, 
				 "You have exceeded the allowed "
				 "amount of free searches.");
		}
	}

	// . do not get results if showing cities only in the searchresults div
	// . needsave might be true for these guys but do not save at this
	//   point. i guess it could be true because they supply their gps
	//   coordinates... but if we do then didsavewrapper double calls 
	//   gotResults()
	if ( si->m_cities ) {
		st->m_gotResults = true;
		return gotResults ( st );
	}

	if ( si->m_testerPage ) {
		st->m_gotResults = true;
		return gotResults ( st );
	}


	// LAUNCH ADS
	// . now get the ad space for this query
	// . don't get ads if we're not on the first page of results
	// . query must be NULL terminated
	if (si->m_adFeedEnabled && si->m_raw == 0 && si->m_docsWanted > 0) {
                int32_t pageNum = (si->m_firstResultNum/si->m_docsWanted) + 1;
		st->m_gotAds = st->m_ads.
			getAds(si->m_displayQuery    , //query
			       si->m_displayQueryLen , //q len
			       pageNum               , //page num
                               si->m_queryIP         ,
			       si->m_coll2           , //coll
			       st                    , //state
			       gotAdsWrapper         );//clbk
        }
	else	
		st->m_gotAds = true;

	// LAUNCH SPELLER
	// get our spelling correction if we should (spell checker)
	st->m_spell[0] = '\0';
	st->m_narrow[0] = '\0';
	st->m_numNarrows = 0;
	if ( ( si->m_spellCheck && cr->m_spellCheck && 
	       g_conf.m_doSpellChecking ) ) {
		bool narrowSearch = ( si->m_doNarrowSearch && 
				      cr->m_doNarrowSearch &&
				      g_conf.m_doNarrowSearch );
		//Query qq;
		//qq.set (si->m_qbuf2, si->m_qbufLen2, NULL, 0,si->m_boolFlag);
		st->m_gotSpell = g_speller.
			getRecommendation( &st->m_q,          // Query
					   si->m_spellCheck,  // spellcheck
					   st->m_spell,       // Spell buffer
					   MAX_FRAG_SIZE,     // spell buf size
					   narrowSearch,      // narrow search?
					   st->m_narrow,      // narrow buf
					   MAX_FRAG_SIZE,    // narrow buf size
					   &st->m_numNarrows,// num of narrows 
					   st,               // state
					   gotSpellingWrapper );// callback
	}
	else
		st->m_gotSpell = true;

	// LAUNCH fbrec save
	FBRec *fbrec = st->m_msgfb.m_fbrecPtr;

	/*
	//
	// set State7::m_myLocation
	//
	char *loc = NULL;
	if ( si->m_useCookie ) loc = hr->getStringFromCookie("myloc",NULL);
	// if still null try getting from fbrec... we need to do this
	// when m_emailFormat is true cuz we are sending an email. so we
	// do not have cookies.
	if ( ! loc ) loc = fbrec->ptr_myLocation;
	// nuke this
	if ( loc && strncasecmp("Enter city/st",loc,9) == 0 ) loc = NULL;
	// if that is empty, try importing from where box
	if ( ! loc || ! loc[0] ) loc = si->m_where;
	// check it out some more
	if ( loc && ! loc[0] ) loc = NULL;
	// set this
	st->m_myLocation = loc;
	*/


	//
	// this is for storing the rec that we just generate from a successful
	// facebook login...  however, we should move it so it can also
	// save their interests
	//
	// assign
	bool needSave = false;
	// skip save?
	bool doSave = true;
	if ( ! fbrec             ) doSave = false;
	if ( ! si->m_useCookie   ) doSave = false;
	if (   si->m_emailFormat ) doSave = false;

	// but sometimes i do want to override the fb rec to fix things
	// before emailing out. like to fix their mylocation parameter.
	if ( fbrec && hr->getLong("save",0) ) doSave = true;

	// do not do this when emailing! or when not using cookies!
	if ( doSave ) {
		// sometimes their gps coordinates change
		if ( si->m_gpsLat != 999 &&
		     si->m_gpsLon != 999 &&
		     ( si->m_gpsLat != fbrec->m_gpsLat ||
		       si->m_gpsLon != fbrec->m_gpsLon ) ) {
			fbrec->m_gpsLat = si->m_gpsLat;
			fbrec->m_gpsLon = si->m_gpsLon;
			needSave = true;
		}
		// compare interests
		char *s = fbrec->ptr_mergedInterests;
		char *t = si->m_intBuf.getBufStart();
		if ( si->m_intBuf.length() <= 0 ) t = NULL;
		if ( s && t && strcmp ( s , t ) ) needSave = true;
		if ( ! s &&   t ) needSave = true;
		if (   s && ! t ) needSave = true;
		// get mylocation
		char *loc = si->m_myLocation;
		// . compare location
		// . this should be set to lat/lon string if it was
		//   available when we first saved the record
		// . then it would fallback to facebook's current_location
		// . then it would fall back to city/state/ctry based on IP
		s = fbrec->ptr_myLocation;
		t = loc;
		if ( s && t && strcmp ( s , t ) ) needSave = true;
		if ( ! s &&   t ) needSave = true;
		if (   s && ! t ) needSave = true;
		// compare radius (0 means anywhere)
		int32_t myRadius = fbrec->m_myRadius;
		//int32_t mr2 = hr->getLongFromCookie("myradius",0);
		if ( myRadius != si->m_myRadius ) needSave = true;
		// compare email selection
		char emailFrequency = fbrec->m_emailFrequency;
		if ( emailFrequency != si->m_emailFreq ) needSave = true;
		// store interests
		char *ib     = si->m_intBuf.getBufStart();
		int32_t  ibSize = si->m_intBuf.length() + 1;
		if ( ibSize == 1 ) { ibSize = 0; ib = NULL; }
		if ( si->m_intBuf.length() <= 0 ) ib = NULL;
		fbrec->ptr_mergedInterests = ib;
		fbrec->size_mergedInterests = ibSize;
		// log it for debug
		//log("facebook: saving interests: %s",ib);
		// store location
		int32_t locSize = 0;
		if ( loc ) locSize = gbstrlen(loc)+1;
		fbrec->ptr_myLocation  = loc;
		fbrec->size_myLocation = locSize;
		// store radius
		fbrec->m_myRadius = (int32_t)si->m_myRadius;//mr2;
		// store email frequency
		fbrec->m_emailFrequency = si->m_emailFreq;
		// save it"
		st->m_needSave = needSave;
	}


	// if a host is dead, do not do this because it will block until
	// that host comes back up!
	if ( st->m_needSave && fbrec ) { // && ! g_hostdb.isHostDead() ) {
		st->m_didSave = false;
		st->m_msgfb.m_tmp = st;
		st->m_msgfb.m_afterSaveCallback = didSaveWrapper;
		// this won't block if its local
		if ( st->m_msgfb.saveFBRec ( fbrec ) ) {
			st->m_didSave = true;
			log("query: fbrec save completed");
		}
	}
	else {
		st->m_didSave = true;
		log("query: fbrec save not needed");
	}


	// default query highlighting to true if NOT doing widget. it is
	// now defaulted to off for the widget in Parms.cpp ("qh")
	/*
	if ( ! si->m_widget ) { // && ! si->m_makeWidget ) {
		si->m_showDates      = st->m_hr.getLong("showdates",1);
		si->m_showCountdowns = st->m_hr.getLong("showcountdowns",1);
		si->m_showSummaries  = st->m_hr.getLong("showsummaries",1);
		si->m_showAddresses  = st->m_hr.getLong("showaddresses",1);
		si->m_doQueryHighlighting = st->m_hr.getLong("qh",1);
	}
	else {
		// some popular options as checkboxes
		si->m_showDates      = st->m_hr.getLong("showdates",1);
		si->m_showCountdowns = st->m_hr.getLong("showcountdowns",1);
		si->m_showSummaries  = st->m_hr.getLong("showsummaries",0);
		si->m_showAddresses  = st->m_hr.getLong("showaddresses",0);
		si->m_doQueryHighlighting = st->m_hr.getLong("qh",0);
		// turn this off. it was getting set to true from cookie.
		si->m_showWidget = 0;
	}
	*/

	if ( si->m_widget && ! si->m_igoogle ) {
		si->m_showWidget = false;
		si->m_doQueryHighlighting = st->m_hr.getLong("qh",0);
	}

	if ( si->m_igoogle ) {
		si->m_doQueryHighlighting = st->m_hr.getLong("qh",1);
	}
	

	// LAUNCH RESULTS

	//log("query: got query1 %s",si->m_sbuf1.getBufStart());

	//log("msg40 call coll=%s",si->m_coll2);

	// . get some results from it
	// . this returns false if blocked, true otherwise
	// . it also sets g_errno on error
	// . use a niceness of 0 for all queries so they take precedence
	//   over the indexing process
	// . this will copy our passed "query" and "coll" to it's own buffer
	// . we print out matching docIds to int32_t if m_isDebug is true
	// . no longer forward this, since proxy will take care of evenly
	//   distributing its msg 0xfd "forward" requests now
	st->m_gotResults=st->m_msg40.getResults(si,false,st,gotResultsWrapper);
	// save error
	st->m_errno = g_errno;

	// wait for ads and spellcheck and results?
	if ( ! st->m_gotAds     || 
	     ! st->m_gotSpell   || 
	     ! st->m_gotResults || 
	     ! st->m_didSave)
		return false;

	// skipResults:

	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	bool status2 = gotResults ( st );
	// note it. this can block if it calls msg40 a second time because
	// it's getting recommendations... (m_showPersonal=1)
	if ( ! status2 ) return false;
	// log the time
	//if ( g_conf.m_logQueryTimes ) {
	int64_t took = gettimeofdayInMilliseconds()-st->m_startTime;
	if ( took >= g_conf.m_logQueryTimeThreshold ) {
		//log(LOG_INFO,"query: Took %"INT64" ms for %s",took,ttq);
		logf(LOG_TIMING,"query: Took %"INT64" ms for %s. results=%"INT32"",
		     took,si->m_sbuf1.getBufStart(),
		     st->m_msg40.getNumResults());
	}
	g_stats.logAvgQueryTime(st->m_startTime);

	// we now delete this, not gotResults
	mdelete ( st , sizeof(State7) , "PageEvents2" );
	delete ( st );
	return status2;
}

void gotSpellingWrapper( void *state ){
	// cast our State7 class from this
	State7 *st = (State7 *) state;
	// log the error first
	if ( g_errno ) log("query: speller: %s.",mstrerror(g_errno));
	// clear any error cuz spellchecks aren't needed
	g_errno = 0;
	st->m_gotSpell = true;
	gotState(st);
}

void gotResultsWrapper ( void *state ) {
	// cast our State7 class from this
	State7 *st = (State7 *) state;
	// save error
	st->m_errno = g_errno;
	// sanity
	if ( st->m_gotResults ) { char *xx=NULL;*xx=0; }
	// mark as gotten
	st->m_gotResults = true;
	gotState (st);
}

void gotResultsWrapper3 ( void *state ) {
	// cast our State7 class from this
	State7 *st = (State7 *) state;
	// save error
	st->m_errno = g_errno;
	// sanity
	if ( st->m_gotResults ) { char *xx=NULL;*xx=0; }
	// mark as gotten
	st->m_gotResults = true;
	gotState (st);
}


void gotAdsWrapper ( void *state ) {
	// cast our State7 class from this
	State7 *st = (State7 *) state;
	// mark as gotten
	st->m_gotAds = true;
	// log the error first
	if ( g_errno ) log("query: adclient: %s.",mstrerror(g_errno));
	// clear any error cuz ads aren't needed
	g_errno = 0;
	gotState (st);
}

void deleteState ( State7 *st ) {
	// log the time
	int64_t took = gettimeofdayInMilliseconds()-st->m_startTime;
	// record that
	st->m_took = took;
	//if ( g_conf.m_logQueryTimes ) {
	if ( took >= g_conf.m_logQueryTimeThreshold ) {
		//log(LOG_INFO,"query: Took %"INT64" ms for %s",took,ttq);
		logf(LOG_TIMING,"query: Took %"INT64" ms for %s. results=%"INT32"",took,
		     st->m_q.m_orig,//msg40.getCompoundQueryBuf(),
		     st->m_msg40.getNumResults());
	}
	g_stats.logAvgQueryTime(st->m_startTime);
	// free the state
	mdelete ( st , sizeof(State7) , "PageEvents2" );
	delete ( st );
}

void gotState ( void *state ){
	// cast our State7 class from this
	State7 *st = (State7 *) state;
	if ( !st->m_gotAds || 
	     !st->m_gotSpell || 
	     !st->m_gotResults ||
	     !st->m_didSave )
		return;
	// we're ready to go. this can now block because it might re-call
	// msg40->getResults() if doing the "Just for You" logic
	if ( ! gotResults ( state ) ) return;
	// close it up
	deleteState ( st );
}


class Balloon {
public:
	char m_letter;
	double m_lat;
	double m_lon;
};

// returns false if blocks, true otherwise
bool sendErrorReply7 ( State7 *st ) { // , SafeBuf &sb ) {
	// log it
	//g_errno = EBUFTOOSMALL;
	//log( "query: Query failed. Maximum page buffer size (%i bytes) "
	//     "exceeded. Sending 500 HTTP status.",
	//     sb.getBufEnd() - sb.getBuf());
	// extract the TcpSocket from the "state" data
	TcpSocket *s = st->m_socket;
	SearchInput *si      = &st->m_si;
	// save for callback potentially
	void (* emailCallback)(void *) = st->m_emailCallback;
	void *emailState = st->m_emailState;
	// nuke the email resutls if there
	if ( st->m_emailResultsBuf    ) st->m_emailResultsBuf   ->purge();
	if ( st->m_emailLikedbListBuf ) st->m_emailLikedbListBuf->purge();


	if ( g_errno && g_errno != ENOPERM ) 
		log("query: Query failed 5: %s.",mstrerror(g_errno));
	if ( g_errno != ENOPERM && g_errno != ENOCOLLREC && ! emailCallback ) 
		g_stats.m_numFails++;

	int32_t status = 500;
	if (g_errno == ETOOMANYOPERANDS ||
	    g_errno == EBADREQUEST ||
	    g_errno == ENOPERM ||
	    g_errno == ENOCOLLREC) 
		status = 400;

	if ( ! g_errno )
		log("query: g_errno not set but sendErrorReply() called");

	int32_t raw = si->m_raw;

	// nuke State7 class here, otherwise
	//mdelete ( st , sizeof(State7) , "PageEvents2" );
	//delete ( st );

	// if we are doing an email, "emailformat=1" then call the
	// email callback
	if ( emailCallback ) {
		emailCallback ( emailState );
		return true;
	}

	// . the 0 means browser caches the page for however int32_t its set
	// . returns false if blocked true otherwise
	g_httpServer.sendQueryErrorReply ( s,
					   status ,
					   mstrerror(g_errno),
					   raw,
					   g_errno, 
					   "There was an error "
					   "processing the query"
					   );
	// tell caller it did not block since we can't wait around for it
	// anyway!!! it has not done callback
	return true;
}


bool printTwitterLink ( SafeBuf &sb ) {
		sb.safePrintf("<a href=\"http://twitter.com/share?text=");
		sb.urlEncode ( APPNAME );
		sb.urlEncode(" datamines the web for events. Pretty cool.");
		sb.safePrintf("&url=");
		sb.urlEncode ("http://");
		sb.urlEncode ( APPDOMAIN );
		sb.safePrintf("/&data-hashtags=");
		sb.urlEncode ( APPNAME );
		sb.safePrintf("\" target=\"_parent\">");
		return true;
}

bool printLinks ( SafeBuf &sb , State7 *st ) {
	SearchInput *si = &st->m_si;
	Msg40 *msg40 = &(st->m_msg40);

	sb.safePrintf ( "<table width=100%%>" );

	// twitter in links submenu
	sb.safePrintf ( "<tr><td>");
	// prints an <a href=> tag
	printTwitterLink ( sb );
	sb.safePrintf ( "<img align=left height=32 width=32 "
			"border=0 src=/twitter32.png></a></td>"
			"<td>Tweet %s</td>"
			"</tr>" 
			, APPNAME
			);


	// http://hosting.gmodules.com/ig/gadgets/file/108415847701685077985/eventguru.xml?nocache=1
	sb.safePrintf ( "<tr><td><a href=\"http://fusion.google.com/"
			"add?source=atgs&moduleurl=" );
	SafeBuf ubuf;
	ubuf.safeStrcpy ( "http://hosting.gmodules.com/ig/gadgets/file/108415847701685077985/eventguru.xml?nocache=1");
	ubuf.urlEncode();
	sb.safePrintf("%s\">", ubuf.getBufStart());

	sb.safePrintf("<img align=left "
		      "height=17 width=62 "
		      "src=\"http://gmodules.com/ig/images/plus_google.gif\" "
		      "border=0 alt=\"Add to iGoogle\"></td><td>"
		      "Add to iGoogle</a>"
		      "</td></tr>");
	//"\"><img border=0 height=32 width=32 src=/igoogle.png></a></td><td> Add to iGoogle</td></tr>" );




	// onclick change window url to it
	// call getFormParms() maybe, but do not include submenus
	sb.safePrintf ( "<tr class=hand onclick=\""
			//"alert(getFormParms());"
			"window.location.href='/?xml=1&qh=0&'"
			"+getFormParms();\">"
			"<td>"
			"<img align=left "
			"height=32 width=32 border=0 src=/rss.png></td>"
			"<td> RSS Feed</td></tr>" );

	// the Developer API
	/*
	sb.safePrintf ( "<tr>"
			"<td><a href=/api.html>"
			"<img height=32 width=32 border=0 src=/rss2.gif>"
			"</a></td>"
			"<td><a href=/api.html>Developer API</a></td></tr>" );
	*/


	// hardcode clockSet here since this is a permalink and if clockset
	// was zero that means to use the current time
	int32_t clockSet = si->m_clockSet;
	if ( clockSet == 0 ) clockSet = msg40->m_nowUTC;
	int32_t timeZoneOffset = msg40->m_timeZoneOffset;
	if ( timeZoneOffset == UNKNOWN_TIMEZONE ) 
		timeZoneOffset = si->m_guessedTimeZone;
	if ( timeZoneOffset == UNKNOWN_TIMEZONE )
		timeZoneOffset = -5;
	clockSet += timeZoneOffset * 3600;
	sb.safePrintf ( "<tr class=hand onclick=\""
			"window.location.href='/?clockset=%"UINT32"&'"
			"+getFormParms()\">"
			"<td>"
			"<img align=left height=32 width=32 border=0 "
			"src=/permalink.png></a></td>"
			"<td> Permalink</td></tr>"
			, clockSet );


	//sb.safePrintf ( "<tr>"
	//		"<td><a href=\"http://www.facebook.com/share.php?u=http://eventguru.com/\" onClick=\"return fbs_click()\" target=\"_blank\"><img height=32 width=32 border=0 src=/facebook.png></a><script> //<![CDATA[ function fbs_click() {u=location.href;t=document.title;window.open('http://www.facebook.com/sharer.php?u='+encodeURIComponent(u)+'&t='+encodeURIComponent(t),'sharer','toolbar=0,status=0,width=626,height=436');return false;} //]]></script></td><td>Facebook Eventguru</td></tr>" );

	sb.safePrintf ( "</table>" );
	return true;
}

//
// BECAUSE YOU ARE SIMILAR TO table
//
bool printSimilarToTable ( SafeBuf &sb , State7 *st , Msg20Reply *mr ,
			   int32_t iconId , ExpandedResult *er ) {

	int32_t width = 20;
	int32_t height = 20;
	SearchInput *si = &st->m_si;
	// do not even bother if not doing "Just For You" search
	if ( ! si->m_showPersonal ) return true;
	HttpRequest *hr = &st->m_hr;
	char *p = mr->ptr_likedbList;
	char *pend = p + mr->size_likedbList;
	int32_t count = 0;
	bool firstOne = true;
	int32_t maxPics = 20;
	// hide them?
	char *s = "";
	char *target = "";
	if ( si->m_widget ) {
		// about 30 pixels across per 
		int32_t widgetWidth = hr->getLong("widgetwidth",200);
		maxPics = (widgetWidth - 100) / 30;
		if ( maxPics <= 0 ) maxPics = 1;
		target = " target=_parent";
	}
	//if ( si->m_invited == 0 ) s = " style=\"display:none;\"";
	// do not even print invisibly if we are a widget
	//if ( si->m_invited == 0 && si->m_widget ) p = pend;
	for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
		int32_t flags = g_likedb.getPositiveFlagsFromRec ( p );
		if ( ! ( flags &(LF_INVITED|LF_LIKE_EG|LF_GOING|LF_MAYBE_FB)))
			continue;
		// get id
		int64_t uid = g_likedb.getUserIdFromRec ( p );
		// see if they are in our Msg39Request::ptr_similarPeopleIds
		// list of ids we queried for.
		bool inList = false;
		char      *sids = si->m_similarPeopleIds.getBufStart();
		int32_t       slen = si->m_similarPeopleIds.length();
		int64_t *sim  = (int64_t *) sids;
		int64_t *send = (int64_t *)(sids+slen);
		for ( ; sim < send ; sim++ ) {
			if ( *sim != uid ) continue;
			inList = true;
			break;
		}
		if ( ! inList ) continue;
		count++;
		if ( count > maxPics ) continue;
		if ( firstOne )
			sb.safePrintf ( "<table cellpadding=0 cellspacing=0>" 
					"<tr id=similar%"INT32"%s>"
					"<td colspan=9>"
					//"<img height=16 width=16 "
					//"src=/invited16.png "
					//"title=\"Invited to this "
					//"event\">"
					"<font size=-1>"
					"Because you are similar to"
					"</font> "
					"&nbsp;"
					"</td><td>" , iconId , s );
		firstOne = false;
		// http://graph.facebook.com/502303355/picture shows pic 4 uid
		if ( flags & LF_ISEVENTGURUID )
			sb.safePrintf ( "<img width=%"INT32" height=%"INT32" "
					"src=/frog16.png title=%"UINT64">"
					"&nbsp;"
					, width
					, height
					, uid
					);
		else
			sb.safePrintf ( "<a "
					// facebook canvas puts us in an iframe
					// so bust out with target=_parent
					"target=_parent "
					"href=http://www.facebook.com/%"UINT64""
					"%s>"
					"<img width=%"INT32" height=%"INT32" "
					"src=\"http://graph.facebook.com/"
					"%"UINT64"/picture\">"
					"</a>"
					"&nbsp;"
					, uid
					, target
					, width
					, height
					, uid
					);
	}
	if ( count )
		sb.safePrintf ( "</td></tr></table>" );
	return true;
}

bool printPic ( SafeBuf &sb ,
		SearchInput *si,
		int32_t width ,
		int32_t height,
		int64_t uid,
		char *target ) {

	// ok, print it
	sb.safePrintf ( 
		       "<div style=\""
		       "position:relative;"
		       "width:%"INT32"px;"
		       "height:%"INT32"px;"
		       "padding-right:5px;"
		       "display:inline-block;"
		       "\">"
		       "<a "
		       // facebook canvas puts us in an iframe
		       // so bust out with target=_parent
		       //"target=_parent "
		       "href=http://www.facebook.com/%"UINT64""
		       "%s>"
		       "<img width=%"INT32" height=%"INT32" "
		       , width
		       , height
		       , uid
		       , target
		       , width
		       , height
		       );

	if ( ! si->m_emailFormat )
		sb.safePrintf ( 
			       "style=position:absolute; "
			       "onmouseout=\""
			       "this.style.zIndex=98;"
			       "this.width=%"INT32";"
			       "this.height=%"INT32";\" "
			       "onmouseover=\""
			       "this.style.zIndex=99;"
			       "this.width=50;"
			       "this.height=50;\" "
			       , width
			       , height
			       );
	sb.safePrintf ( "src=\"http://graph.facebook.com/"
			"%"UINT64"/picture\">"
			"</a>"
			"</div>"
			//"&nbsp;"
			, uid
			);
	
	return true;
}



bool printPics ( SafeBuf &sb ,
		 State7 *st ,
		 Msg20Reply *mr,
		 int32_t requiredFlags ,
		 int32_t  iconId ,
		 ExpandedResult *er ,
		 HashTableX *friends ,
		 bool friendsOnly ) {

	SearchInput *si = &st->m_si;
	HttpRequest *hr = &st->m_hr;

	int32_t width = 20;//32;
	int32_t height = 20;//32;
	if ( si->m_widget ) {
		width = 20;
		height = 20;
	}

	int64_t userId = si->m_eventGuruId;
	if ( si->m_facebookId ) userId = si->m_facebookId;

	char *target = "";

	int32_t maxPics = 15;

	if ( si->m_emailFormat ) {
		width = 50;
		height = 50;
		maxPics = 10;
	}

	if ( si->m_widget ) {
		// about 30 pixels across per 
		int32_t widgetWidth = hr->getLong("widgetwidth",200);
		maxPics = (widgetWidth - 100) / 30;
		if ( maxPics <= 0 ) maxPics = 1;
		// do not leave the original widget website!
		//target = " target=_parent";
		target = " target=_blank";
	}

	int32_t count = 0;

	// hide them?
	char *s = "";
	int32_t pc = 0;
	char *idPrefix = NULL;
	char *msg = NULL;
	if ( requiredFlags & LF_GOING    ) {
		pc = si->m_going; 
		idPrefix = "going"; 
		msg = "Because your friends are going:";
	}
	if ( requiredFlags & LF_INVITED  ) {
		pc = si->m_invited; 
		idPrefix = "invited"; 
		msg = "Because your friends are invited:";
	}
	if ( requiredFlags & LF_MAYBE_FB ) {
		pc = si->m_likers; 
		idPrefix = "likers"; 
		msg = "Because your friends might go:";
	}

	if ( pc == 0 ) s = " style=\"display:none;\"";

	// save in case we print nothing!
	int32_t savedPos = sb.length();

	// assume we will print something
	sb.safePrintf ( "<tr id=%s%"INT32"%s>" // "going"
			"<td>"
			"<font size=-1>"
			, idPrefix // "going" etc.
			, iconId
			, s
			);
	// print "Because your friends might go:" etc.
	if ( friendsOnly ) sb.safePrintf("%s",msg);
	// just print going: invited: or likers:
	else sb.safePrintf ( "%s" , idPrefix );
	//if ( friendsOnly ) sb.safePrintf("</i>");
	sb.safePrintf ( "</font> "
			"&nbsp;"
			"</td><td>" 
			);


	// get user's vote if there
	char *me = g_likedb.getRecFromLikedbList ( userId ,
						   0,// start_time
						   requiredFlags,//LF_GOING,
						   mr->ptr_likedbList,
						   mr->size_likedbList );

	//
	// print YOU first
	//
	if ( me && ! friendsOnly ) {
		// count it
		count++;
		// ok, print it
		printPic ( sb , si , width , height , userId , target );
	}
	

	char *p    =     mr-> ptr_likedbList;
	char *pend = p + mr->size_likedbList;

	// now print out the friends first!!!
	for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
		// stop before breach
		if ( count >= maxPics ) break;
		// see if we got a match
		int32_t flags = g_likedb.getPositiveFlagsFromRec ( p );
		if ( ! ( flags & requiredFlags ) ) continue;
		// going also requires a start_time match!
		// so check that on the expanded result
		int32_t start_time = g_likedb.getStartTimeFromRec ( p );
		if ( er && er->m_timeStart != start_time ) continue;
		// we already printed you!
		if ( p == me ) continue;
		// http://graph.facebook.com/502303355/picture shows pic 4 uid
		int64_t uid = g_likedb.getUserIdFromRec ( p );
		// need a friend for this
		if ( ! friends->isInTable ( &uid ) ) continue;
		// ok, print it
		printPic ( sb , si , width , height , uid , target );
		// stop before breach
		count++;
	}

	p    =     mr-> ptr_likedbList;
	pend = p + mr->size_likedbList;

	for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
		// do not breach
		if ( count >= maxPics ) break;
		// stop if not needed
		if ( friendsOnly ) break;
		// see if we got a match
		int32_t flags = g_likedb.getPositiveFlagsFromRec ( p );
		if ( ! ( flags & requiredFlags ) ) continue;
		// going also requires a start_time match!
		// so check that on the expanded result
		int32_t start_time = g_likedb.getStartTimeFromRec ( p );
		if ( er && er->m_timeStart != start_time ) continue;
		// we already printed you!
		if ( p == me ) continue;
		// http://graph.facebook.com/502303355/picture shows pic 4 uid
		int64_t uid = g_likedb.getUserIdFromRec ( p );
		// friends were printed above
		if ( friends->isInTable ( &uid ) ) continue;
		// ok, print it
		printPic ( sb , si , width , height , uid , target );
		// stop before breach
		count++;
	}
	if ( count > maxPics )
		// if that was 10th, print more like
		sb.safePrintf("<a "
			      "href=mor2.html><font size=-1>"
			      "%"INT32" more</font></a>", count - maxPics );
	if ( count )
		sb.safePrintf ( "</td></tr>" );

	// if we printed nothing, then rewind!
	if ( count == 0 ) sb.setLength ( savedPos );

	return true;
}


bool printAttendeesAndLikers ( SafeBuf &sb , State7 *st , Msg20Reply *mr ,
			       int32_t iconId , ExpandedResult *er ) {


	//HttpRequest *hr = &st->m_hr;

	// return if no likers/attendees/etc.
	if ( mr->size_likedbList == 0 ) return true;

	sb.safePrintf ( "<table cellpadding=0 cellspacing=0>" );

	SearchInput *si = &st->m_si;

	// hide the note? only if all are hidden
	char *s = "";
	if ( si->m_going   == 0 &&
	     si->m_invited == 0 &&
	     si->m_likers  == 0 ) 
		s = " style=\"display:none;\"";
	// reference the msg20 reply/summary
	if ( mr->m_eventFlags & EV_HIDEGUESTLIST ) {
		sb.safePrintf ( "<tr id=going%"INT32"%s>"
				"<td>"
				"<font size=-1 color=gray><i>"
				"Guest list is hidden"
				"</i></font>"
				"</td>"
				"</tr>"
				"</table>" 
				, iconId 
				, s 
				);
		// at least print this
		//printSimilarToTable (sb,st,mr,iconId,er);
		return true;
	}

	//
	// make a table of all friend ids
	//
	HashTableX friends;
	int32_t nf = 0;
	FBRec *fbrec = st->m_msgfb.m_fbrecPtr;
	if ( fbrec ) nf = fbrec->size_friendIds / 8;
	friends.set ( 8 ,0, nf,NULL,0,false,0,"frdtbl");
	int64_t *fids = NULL;
	if ( fbrec ) fids = (int64_t *)fbrec->ptr_friendIds;
	for ( int32_t i = 0 ; i < nf ; i++ ) friends.addKey(&fids[i]);

	// print friends only?
	bool fo = false;
	if ( si->m_showPersonal ) fo = true;

	// 
	// PRINT GOING
	//
	printPics ( sb,st,mr,LF_GOING , iconId,er,&friends,fo);


	//
	// PRINT LIKERS AND MAYBES
	//
	printPics(sb,st,mr,(LF_LIKE_EG|LF_MAYBE_FB), iconId,er,&friends,fo);


	//
	// PRINT INVITED
	//
	printPics ( sb,st,mr, LF_INVITED, iconId,er,&friends,fo);


	// if showing personal results print "Because your friends are invited"
	// or whatever...
	

	//
	// end the whole table
	//
	sb.safePrintf("</table>");

	// 
	// BECAUSE you are similar to: ...
	//
	//printSimilarToTable (sb,st,mr,iconId,er);

	return true;
}

// "Because you like|are going to|are invited to"...
bool printBecauseILike  ( SafeBuf &sb , State7 *st , Msg20Reply *mr ) {

	char *p    =     mr-> ptr_likedbList;
	char *pend = p + mr->size_likedbList;
	for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
		// who is liking, etc. this event?
		int64_t uid = g_likedb.getUserIdFromRec ( p );
		// skip if its not you!
		if ( uid != st->m_msgfb.m_fbId ) continue;
		// how did you flag it?
		int32_t flags = g_likedb.getPositiveFlagsFromRec ( p );
		// print out because
		char *reason = NULL;//"are associated with";
		// this means they DON'T like it!
		if ( flags & LF_LIKE_EG  ) reason = "like";
		if ( flags & LF_INVITED  ) reason = "are inivited to";
		if ( flags & LF_MAYBE_FB ) reason = "might go to";
		if ( flags & LF_GOING    ) reason = "are going to";
		// probably LF_EMAILED_EG meaning that we emailed it to
		// them already!!!
		if ( ! reason ) continue;
		sb.safePrintf("<font size=-1>"
			      "Because you %s this event."
			      "</font>"
			      "<br>"
			      , reason 
			      );
	}
	return true;
}

// "Because it has the terms <i>xxx</i> yyy and zzz.
bool printMatchingTerms ( SafeBuf &sb , State7 *st , Msg20Reply *mr ) {
	SearchInput *si = &st->m_si;
	// only for "Just For You" searches right now
	if ( ! si->m_showPersonal ) return true;
	// int16_tcut
	int64_t fbId = st->m_msgfb.m_fbId;
	// . scan the query terms
	// . stop at first pipe
	// . ignore gbsimilarinto:1 term
	// . use words/xml classes we set for term highlighting
	// . when a term is matched, record the query term #
	// . so we can say "party" even though "bash" is highlighted...
	// . Because you like: "party (aka bash), education (aka campus)"
	// . use SearchInput::m_q because that is what Msg40.cpp
	//   uses to set SearchInput::ptr_qbuf used by 
	//   XmlDoc::getEventSummary() to set ptr_matchedQueryWords
	Query *q = si->m_q;
	// get it
	int32_t *qwi = (int32_t *)mr->ptr_matchedQueryWords;
	char *mt  = (char *)mr->ptr_matchedTypes;
	//int32_t *nqw = (int32_t *)mr->ptr_numMatchedQueryWords;
	int32_t  n   = mr->size_matchedQueryWords / 4;
	bool firstOne = true;
	// scan for matched query words
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get the query word #
		int32_t qn = qwi[i];
		// skip if not in title. 0 means body.
		if ( mt[i] != 1 ) continue;
		// print that query word out!
		if ( qn < 0 || qn >= q->m_numWords ) 
			continue;
		// cast it
		QueryWord *qw = &q->m_qwords[qn];
		// get the topic in [str,slen]
		char *str  = qw->m_word;
		int32_t  slen = qw->m_wordLen;
		if ( qw->m_quoteStart >= 0 &&
		     qw->m_quoteEnd >= 0 &&
		     qw->m_quoteEnd > qw->m_quoteStart ) {
			// get the quotes as words
			QueryWord *qw1 = &q->m_qwords[qw->m_quoteStart];
			QueryWord *qw2 = &q->m_qwords[qw->m_quoteEnd];
			// the first word after the quote
			str = qw1->m_word;
			// the last word in the quote...
			char *end = qw2->m_word + qw2->m_wordLen;
			// get total in the quote
			slen = end - str;
		}
		// and print
		if ( firstOne ) {
			sb.safePrintf("<font size=-1>"
				      //"<br>"
				      "Because it matches "
				      "your interests: "
				      );
		}
		else {
			sb.safePrintf(", ");
		}
		sb.safePrintf("<a "
			      // in case we are in a widget
			      "target=_parent "
			      "href=\"%s?"
			      // show brown box
			      "showpersonal=1&"
			      // show interest submenu
			      "suggestions=1&"
			      "ei=%"UINT64"&"
			      "usefbid=%"UINT64"&"
			      "fh=%"UINT32"&"
			      "hi="
			      , APPHOSTUNENCODED
			      , fbId
			      , fbId
			      // this is like the password
			      , hash32 ( (char *)&fbId , 8, 0 )
			      );
		sb.urlEncode ( str , slen );
		sb.safePrintf("#edit\"><i>");
		sb.safeMemcpy ( str , slen );
		sb.safePrintf("</i></a>");
		firstOne = false;
	}
	// if we printed an interest, then print a <br>
	if ( ! firstOne )
		sb.safePrintf("</font><br>");
	return true;
}


bool printEventTitleLink ( SafeBuf &sb, SearchInput *si, Msg20Reply *mr ,
			   State7 *st ) {

	// . if we are facebook.
	// . it seems we can't load facebook pages in an iframe!! wtf?
	//if ( mr->m_eventFlags & EV_FACEBOOK ) {
	//	sb.safePrintf("%s", mr->ptr_ubuf );
	//	return true;
	//}

	char *base = "";
	if ( si->m_emailFormat ) base = APPHOSTUNENCODEDNOSLASH;

	sb.safePrintf ( "%s/?" , base );
	// first print query to highlight as the original query
	if ( si->m_query && si->m_query[0] ) {
		sb.safePrintf("hq=");
		sb.urlEncode ( si->m_query );
		sb.safePrintf("&");
	}

	// int16_tcut
	int64_t fbId = st->m_msgfb.m_fbId;

	// print email fbid for tracking purposes
	if ( si->m_emailFormat ) sb.safePrintf("ei=%"UINT64"&",fbId);

	//printEventCachedUrl ( sb , mr , m20 , qe, coll );
	// no, now just do a gbdocid:X query with icc=1
	// (include cached copy) so we get back the entire
	// contents of that docid. then call our new function
	// printCachedPage( sb, mr )...
	// set SummaryWidth (sw) to 100 characters.
	// use gbresultset:3 otherwise it will be forced to 1
	// and we might miss out!!
	sb.safePrintf("id=%"UINT64".%"UINT64""
		      , mr->m_docId
		      , mr->m_eventHash64 
		      );
	/*
	sb.safePrintf("q=gbdocid%%3A%"UINT64"+gbeventhash%%3A%"UINT64""
		      "&where=anywhere"
		      // leave this off so it works on gk144 now
		      // since it hasn't indexed this yet
		      //"+gbresultset%%3A3&"
		      // we also need to show expired events so the
		      // cached page isn't blank!!
		      "&sw=100&n=1&ns=10&showexpiredevents=1&"
		      "icc=1"//&%s"
		      ,mr->m_docId
		      ,mr->m_eventHash64);
		      //si->m_urlParms ); // uc);
		      */
	return true;
};

// call this now for the cached page as well! but use the 32 bit width icons
bool printIcons2 ( SafeBuf &sb , 
		   SearchInput *si , 
		   Msg20Reply *mr ,
		   int32_t iconId ,
		   int64_t eventHash64 ,
		   int64_t docId ,
		   int32_t      eventId ,
		   int32_t start_time ,
		   State7 *st ,
		   int32_t *retLikedbFlags = NULL ) {


	// print target...
	if ( si->m_forForm )
		// . just tell the parent document to reload the right
		//   frame but using this docid/eventid. 
		// . parent window needs saveTagRec() function that can
		//   be called by kids. it will get the query/location/radius
		//   from the left frame, and the mappinginfo from the right
		//   frame and save that under the formurl in tagdb.
		// . then call parent window's insertEvent() function
		//   which will reload the right frame with a specified
		//   docid/eventid. that function should
		sb.safePrintf ( "<td>"
				"<img "
				"id=bullseye%"UINT32" "
				"title=\"insert into form\" "
				"src=/bullseye.jpg "
				"height=16 width=16 "
				"style=padding-right:15px; "
				"onclick=\""
				//"document."
				//"getElementById('formdocid').value = %"INT64";"
				//"document."
				//"getElementById('formeventid').value = %"INT32";"
				"reloadPage(%"INT64",%"INT32",%"UINT32");"
				// TODO: and make this image highlighted
				"\""
				">"
				//"</a>"
				"</td>"
				, iconId
				, docId
				, eventId
				, si->m_clockSet
				);

	// must be logged in to facebook to see these icons
	int64_t facebookId = 0LL;
	if ( si->m_msgfb ) facebookId = si->m_msgfb->m_fbId;

	bool icc = si->m_includeCachedCopy;

	//int64_t userId = si->m_eventGuruId;
	//if ( si->m_facebookId ) userId = si->m_facebookId;

	// scan likedblist to see if this user likes this...
	int32_t myLikedbFlags = g_likedb.getUserFlags ( facebookId ,
						     start_time ,
						     mr->ptr_likedbList ,
						     mr->size_likedbList );

	if ( retLikedbFlags ) *retLikedbFlags = myLikedbFlags;

	char *src1 = "/thumb16.png";
	if ( icc ) src1 = "/thumb32.png";
	char *note1 = "Like this event";
	if ( myLikedbFlags & (LF_LIKE_EG|LF_MAYBE_FB)) {
		src1="/redthumb16.png";
		if ( icc ) src1 = "/redthumb32.png";
		note1 = "UNlike this event";
	}

	char *src2 = "/thumbdown16.png";
	if ( icc ) src2 = "/thumbdown32.png";
	char *note2 = "Hide this event. Click 'Display > Hidden by me' "
		"to undo.";
	if ( myLikedbFlags & LF_HIDE ) {
		src2 = "/redthumbdown16.png";
		if ( icc ) src2 = "/redthumbdown32.png";
		note2 = "UNhide this event";
	}

	char *src3 = "/going16.png";
	char *note3 = "Plan on going to this event";
	if ( icc ) src3 = "/going32.png";
	if ( myLikedbFlags & LF_GOING ) {
		src3 = "/redgoing16.png";
		if ( icc ) src3 = "/redgoing32.png";
		note3 = "Stop planning on going to this event";
	}

	char *src4 = "/accept16.png";
	if ( icc ) src4 = "/accept32.png";
	if ( myLikedbFlags & LF_ACCEPT ) {
		src4 = "/redaccept16.png";
		if ( icc ) src4 = "/redaccept32.png";
	}

	char *src5 = "/reject16.png";
	if ( icc ) src5 = "/reject32.png";
	if ( myLikedbFlags & LF_REJECT ) {
		src5 = "/redreject16.png";
		if ( icc ) src5 = "/redreject32.png";
	}

	int32_t height = 16;
	int32_t width  = 16;
	if ( icc ) {
		height = 32;
		width  = 32;
	}


	if ( si->m_emailFormat ) return true;

	char *base = "";
	if ( si->m_emailFormat ) base = APPHOSTUNENCODEDNOSLASH;

	if ( ! facebookId || si->m_emailFormat ) {

		SafeBuf ttt;
		if ( si->m_emailFormat )
			ttt.safePrintf ( "<a href=\"%s?id=%"UINT64".%"UINT64"\">"
					 , APPHOSTUNENCODED
					 , mr->m_docId
					 , mr->m_eventHash64
					 );
		else
			ttt.safePrintf("<a onclick=\"needLogin();\">");


		sb.safePrintf ( "<td>"
				"%s"
				"<img "
				"id=img%"UINT32"a "
				"title=\"%s\" "
				"src=%s%s "
				"height=%"INT32" width=%"INT32"></a>"
				"</td>"
				, ttt.getBufStart()
				, iconId
				, note1
				, base
				, src1
				, height
				, width 
				);

		if ( icc ) sb.safePrintf ( "<td width=30></td>" );
		else       sb.safePrintf ( "<td width=20></td>" );

		sb.safePrintf (	"<td>"
				"%s"
				"<img "
				"id=img%"UINT32"b "
				"title=\"%s\" "
				"src=%s%s "
				"height=%"INT32" width=%"INT32"></a>"
				"</td>"
				, ttt.getBufStart()
				, iconId
				, note2
				, base
				, src2
				, height
				, width 
				);


		if ( icc ) {
			sb.safePrintf ( "<td width=30></td>" );
			sb.safePrintf ( "<td>"
					"%s"
					"<img "
					"id=img%"UINT32"c "
					"title=\"%s\" "
					"src=%s%s "
					"height=%"INT32" width=%"INT32"></a>"
					"</td>"
					, ttt.getBufStart()
					, iconId
					, note3
					, base
					, src3
					, height
					, width 
					);
		}

		return true;
	}


	// "function tagEvent ( imgid, ev , st , d , evid ) {\n"
	sb.safePrintf ( "<td>"
			// we have to pass back the %"UINT64" guys as strings
			// because the javascript on chrome doesn't support
			// int64_t integers! it ends up zeroing out our
			// last few bits!!!!!
			"<a>"
			"<img "
			"id=img%"UINT32"a "
			"onclick=\"tagEvent('img%"UINT32"a','%"UINT64"','%"UINT32"',"
			"'%"UINT64"','%"INT32"');\" "
			"title=\"%s\" "
			"src=%s%s "
			"height=%"INT32" width=%"INT32"></a>"
			"</td>"
			, iconId
			, iconId
			, eventHash64
			, start_time
			, docId
			, eventId
			, note1
			, base
			, src1
			, height
			, width 

			);

	if ( icc ) sb.safePrintf ( "<td width=30></td>" );
	else       sb.safePrintf ( "<td width=20></td>" );

	sb.safePrintf ( "<td>"
			"<a>"
			"<img "
			"id=img%"UINT32"b "
			"onclick=\"tagEvent('img%"UINT32"b','%"UINT64"','%"UINT32"',"
			"'%"UINT64"','%"INT32"');\" "
			"title=\"%s\" "
			"src=%s%s "
			"height=%"INT32" width=%"INT32"></a>"
			"</td>"

			//"<a href=""><img src=/twitter16.png "
			//"height=16 width=16></a> &nbsp; "
			//"<a href=""><img src=/email16.png "
			//"height=16 width=16></a> &nbsp; "
			//"<a href="">more</a>"
			, iconId
			, iconId
			, eventHash64
			, start_time
			, docId
			, eventId
			, note2
			, base
			, src2
			, height
			, width 

			/*
			*/
			);


	if ( icc ) {
		sb.safePrintf ( "<td width=30></td>"
				"<td>"
				"<a>"
				"<img "
				"id=img%"UINT32"c "
				"onclick=\"tagEvent('img%"UINT32"c','%"UINT64"','%"UINT32"',"
				"'%"UINT64"','%"INT32"');\" "
				"title=\"%s\" "
				"src=%s%s "
				"height=%"INT32" width=%"INT32"></a>"
				"</td>"
				, iconId
				, iconId
				, eventHash64
				, start_time
				, docId
				, eventId
				, note3
				, base
				, src3
				, height
				, width 
				);
	}



	if ( si->m_isTurk && ! si->m_emailFormat ) {

		if ( icc ) sb.safePrintf ( "<td width=30></td>" );
		else       sb.safePrintf ( "<td width=10></td>" );

		sb.safePrintf("<td>"
			      "<a>"
			      "<img "
			      "id=img%"UINT32"d "
			      "onclick=\"tagEvent('img%"UINT32"d','%"UINT64"',"
			      "'%"UINT32"','%"UINT64"','%"INT32"');\" "
			      "title=\"Accept this event\" "
			      "src=%s%s "
			      "height=%"INT32" width=%"INT32"></a>"
			      "</td>"
			      , iconId
			      , iconId
			      , eventHash64
			      , start_time
			      , docId
			      , eventId
			      , base
			      , src4
			      , height
			      , width
			      );
			      
		if ( icc ) sb.safePrintf ( "<td width=30></td>" );
		else       sb.safePrintf ( "<td width=10></td>" );

		sb.safePrintf("<td>"
			      "<a>"
			      "<img "
			      "id=img%"UINT32"e "
			      "onclick=\"tagEvent('img%"UINT32"e','%"UINT64"',"
			      "'%"UINT32"','%"UINT64"','%"INT32"');\" "
			      "title=\"Reject this event\" "
			      "src=%s%s "
			      "height=%"INT32" width=%"INT32"></a>"
			      "</td>"
			      , iconId
			      , iconId
			      , eventHash64
			      , start_time
			      , docId
			      , eventId
			      , base
			      , src5
			      , height
			      , width
			      );
	}

	//if ( ! icc ) sb.safePrintf ( "</span>" );

	//sb.safePrintf("</nobr>");
	return true;
}

/*
bool printIcons1 ( SafeBuf &sb , 
		   Msg20 *msg20 ,
		   Msg20Reply *mr ,
		   ExpandedResult *er ,
		   SearchInput *si ,
		   int32_t iconId ) {

	return true;

	// sorting by distance does nto have expanded result (SORTBY_DIST)
	if ( ! er ) return true;

	// store title of event url encoded
	char tbuf[512];
	SafeBuf tb(tbuf,512);
	//printEventTitle ( tb , mr );
	// filter out tags from highlighted query terms in the title
	tb.filterTags();
	// this one url encodes the title
	char tebuf[512];
	SafeBuf tbenc(tebuf,512);
	tbenc.safeStrcpy ( tb.getBufStart() );
	tbenc.urlEncode();
	// get the date in the special format
	char buf1[128];
	char buf2[128];
	sprintf(buf1,"%"INT32"%02"INT32"%02"INT32"T%02"INT32"%02"INT32"00"
		, (int32_t)er->m_year
		, (int32_t)er->m_month
		, (int32_t)er->m_dayNum
		, (int32_t)er->m_hour1
		, (int32_t)er->m_min1 );
	sprintf(buf2,"%"INT32"%02"INT32"%02"INT32"T%02"INT32"%02"INT32"00"
		, (int32_t)er->m_year
		, (int32_t)er->m_month
		, (int32_t)er->m_dayNum
		, (int32_t)er->m_hour2
		, (int32_t)er->m_min2 );

	int32_t duration = (er->m_hour2 - er->m_hour1)*3600;
	duration += (er->m_min2*60);
	duration -= (er->m_min1*60);
	
	// is it visible or not?
	char *s = "";
	if ( ! si->m_icons) s = " style=\"display:none\"";

	sb.safePrintf ( "<td id=icons%"INT32"%s>" , iconId , s );

	sb.safePrintf ( "<nobr>"
			"<a onclick=\"thumbsup();\">"
			"<img height=16 width=16 border=0 "
			"title=\"Click if you like this type of event\" "
			"src=/thumb16.png></a>"
			"&nbsp;&nbsp;&nbsp;" 
			);
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<img height=16 width=16 border=0 "
			"title=\"Click if you plan on attending\" "
			"src=/going16.png></a>"
			"&nbsp;&nbsp;&nbsp;" 
			);
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<img height=16 width=16 border=0 "
			"title=\"Hide this event\" "
			"src=/hide16.png></a>"
			"</nobr>"
			//"<br>"
			"<br>" );


	sb.safePrintf ( "<nobr>"
			"<a onclick=\"thumbsup();\">"
			"<img height=11 width=16 border=0 "
			"title=\"Tweet this event\" "
			"src=/twitter16.png></a>"
			"&nbsp;&nbsp;&nbsp;" 
			);
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<img height=16 width=16 border=0 "
			"title=\"Email this event\" "
			"src=/email16.png></a>"
			"&nbsp;&nbsp;&nbsp;" 
			);
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<font size=-2>"
			"more..."
			"</font>"
			"</a>"
			"</nobr>" );

	sb.safePrintf ( "<img border=0 height=32 width=32 src=/ical.png "
			"title=\"Add to your iCalendar\">"
			"&nbsp;" );
	sb.safePrintf ( "<a href=\"http://www.google.com/calendar/render?"
			"sprop=website:www.eventguru.com&amp;action=TEMPLATE&"
			"amp;text=%s&amp;dates=%s/%s"
			"&amp;details=Details+found+here:+"
			"%s&amp;location=%s\">"
			"<img height=32 width=32 border=0 src=/google.png "
			"title=\"Add to your Google calendar\"></a>"
			"</nobr>"
			"<br>"
			,tbenc.getBufStart()
			,buf1
			,buf2
			,url
			,location
			);
	sb.safePrintf ( "<a href=\"http://plancast.com/p/9khe/"
			"startup-coffee?feed=ical\">"
			"<img height=32 width=32 border=0 "
			"title=\"Add to your Outlook calendar\" "
			"src=/outlook.png></a>"
			"&nbsp;" );
	sb.safePrintf ( "<a target=\"_blank\" "
			"href=\"http://calendar.yahoo.com/?v=60&amp;"
			"view=d&amp;type=20&amp;title=%s&"
			"amp;st=%s&amp;dur=%"INT32"&amp;"
			"desc=Details found here:+%s"
			"&amp;in_loc=%s\">"
			"<img border=0 height=32 width=32 src=/yahoo.png "
			"title=\"Add to your Yahoo calendar\"></a>"
			"&nbsp;" 
			,tbenc.getBufStart()
			,buf1
			,duration
			,url
			,location
			);
	sb.safePrintf ( "<a onclick=\"awesm_share('facebook','%s"
			"','',''); return false;\">"
			"<img border=0 height=32 width=32 "
			"src=/facebook.png title=\"Announce this event "
			"on Facebook\"></a>"
			"&nbsp;" , url );
	sb.safePrintf ( "<a onclick=\"awesm_share('twitter','','','I like "
			"%s');\">"
			"<img height=32 width=32 border=0 src=/twitter.png>"
			"</a>"
			"<br>" 
			, tb.getBufStart() );
	
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<img height=32 width=32 border=0 "
			"title=\"Announce this event on linkedin\" "
			"src=/linkedin.png></a>"
			"&nbsp;" );
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<img height=32 width=32 border=0 "
			"title=\"Print this event\" src=print.png></a>"
			"&nbsp;" );
	sb.safePrintf ( "<a onclick=\"thumbsup();\">"
			"<img height=32 width=32 border=0 "
			"title=\"Hide this event from me\" "
			"src=/spam.png></a>" );

	sb.safePrintf ( "</td>" );
	return true;
}
*/

static char *s_mapColors[] = {
	"orange",
	//"red", there is no marker_red%c.png on maps.google!!
	"purple",
	"green",
	"brown",
	"black",
	"white",
	"grey" ,
	"yellow"
};

bool printMapUrl ( SafeBuf &sb , State7 *st , int32_t width , int32_t height ) {

	// get stuff
	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;

	sb.safePrintf ( "http://maps.google.com/maps/api/staticmap?size=%"INT32"x%"INT32"&maptype=roadmap&sensor=false" ,
			width, height );

	HashTableX dups;
	char dbuf[1024];
	dups.set ( 4,0,64,dbuf,1024,false,0,"bdbuf");

	int32_t nr = msg40->getNumResults();
	// scan the non-expanded events i guess
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		// get msg20
		Msg20Reply *mr = msg40->m_msg20[i]->m_r;
		// get letter
		int32_t key = (int32_t)mr->m_balloonLetter;
		// did we already add this letter?
		if ( dups.isInTable ( &key ) ) continue;
		// do not repeat
		dups.addKey ( &key );
		// add it now
		char letter = mr->m_balloonLetter;
		// skip if bad letter
		if ( letter < 'A' || letter > 'Z' ) continue;
		// get color string
		int32_t ci = letter - 'A';
		// num we have
		int32_t numColors = sizeof(s_mapColors)/sizeof(char *);
		// mod it
		ci = ci % numColors;
		// panic?
		if ( ci < 0 || ci >= numColors ) continue;
		// concat to url
		sb.safePrintf("&markers="
			      "size:medium"
			      "%%7Ccolor:%s"
			      "%%7Clabel:%c" // letter
			      "%%7C%.07f" // lat
			      "%%2C%.07f" //lon
			      ,s_mapColors[ci]
			      ,letter
			      ,mr->m_balloonLat
			      ,mr->m_balloonLon );
	}

	// get most accurate lat/lon
	float lat = NO_LATITUDE;
	float lon = NO_LONGITUDE;
	int32_t zoom = 0; // world
	// if no results, print one balloon and the userlat/userlon so
	// they can see the location being searched!
	if ( nr == 0 ) {
		if ( si->m_countryLat != NO_LATITUDE && 
		     si->m_countryLon != NO_LONGITUDE ) {
			lat = si->m_countryLat;
			lon = si->m_countryLon;
			zoom = 3; // country?
		}
		if ( si->m_stateLat != NO_LATITUDE && 
		     si->m_stateLon != NO_LONGITUDE ) {
			lat = si->m_stateLat;
			lon = si->m_stateLon;
			zoom = 5; // state?
		}
		if ( si->m_cityLat != NO_LATITUDE && 
		     si->m_cityLon != NO_LONGITUDE ) {
			lat = si->m_cityLat;
			lon = si->m_cityLon;
			zoom = 7; // city?
		}
		if ( si->m_zipLat != NO_LATITUDE && 
		     si->m_zipLon != NO_LONGITUDE ) {
			lat = si->m_zipLat;
			lon = si->m_zipLon;
			zoom = 8; // zip?
		}
		if ( si->m_userLat != NO_LATITUDE && 
		     si->m_userLon != NO_LONGITUDE ) {
			lat = si->m_userLat;
			lon = si->m_userLon;
			zoom = 8;
		}
	}
	
	if ( nr == 0 && lat != NO_LATITUDE && lon != NO_LONGITUDE )
		sb.safePrintf("&zoom=%"INT32""
			      "&markers="
			      "size:medium"
			      "%%7Ccolor:%s"
			      "%%7Clabel:%c" // letter
			      "%%7C%.07f" // lat
			      "%%2C%.07f" //lon
			      ,zoom
			      ,s_mapColors[0]
			      ,'A'
			      ,lat 
			      ,lon );

	// close img tag
	//sb.safePrintf("\">");
	return true;
}

// . print each expanded search result displayed on the map
bool printMap ( SafeBuf &sb , State7 *st , int32_t width , int32_t height ) {
	// init the start of img tag
	sb.safePrintf ( "<img id=mapimg width=%"INT32" height=%"INT32" "
			"style=\""
			"border-top:1px solid gray;"
			"border-left:1px solid gray;"
			"border-right:1px solid black;"
			"border-bottom:1px solid black;"
			"\" "
			"src=\"",
			width, height );
	printMapUrl ( sb , st , width , height );
	sb.safePrintf("\">");
	return true;
}

static char *s_interests[] = {
	"Comedy",
	"Conferences",
	"Education",
	"Family",
	"Fitness",
	"Fine Art",
	"Food & Drink",
	"Museum",
	"Music",
	"Perf. Arts",
	"Politics",
	"Religion",
	"Shopping",
	"Social",
	"Sports"
};
	

bool printLoginScript ( SafeBuf &sb , char *redirUrl = NULL ) {


	// make sure after logging in it comes back to
	// this exact same page

	// http://www.facebook.com/dialog/oauth?client_id=356806354331432&redirect_uri=http%3A//www2.eventguru.com%3A8000/%3Fid%3D101335202664.1780844722033258271&scope=user_events,friends_events

	// this is what we send here:
	//_uri=http%3A//www2.eventguru.com%3A8000/%3Fid%3D101335202664.1780844722033258271&scope=user_events,friends_events

	// this is what Facebook.cpp::downloadAccessToken() uses:
	// _uri=http%3A%2F%2Fwww2.eventguru.com%3A8000%2F%3Fid=101335202664.1780844722033258271
	// it seems to work now!

	sb.safePrintf ( "document.getElementById('login').innerHTML="
			"'<b><font size=1 color=white>Logging in..."
			"</font></b>';"

			//"var tu = top.location.href;\n"
			//"tu.replace('?','0');\n"
			//"tu.replace('g','0');\n"
			//"alert(tu);\n"
			//"redirect_uri='+encodeURIComponent(tu)+'&"

			// facebook hacks off the cgi parms when it
			// redirects you to this url... 
			"var redir="
			);

	if ( redirUrl )
		sb.safePrintf("'%s';\n",redirUrl);
	else
		sb.safePrintf("encodeURIComponent(top.location.href);\n");

	sb.safePrintf ( 
			"var url='http://www.facebook.com/"
			"dialog/oauth?client_id=%s&"
			// keep it simple for now because this redirect
			// url really can't have cgi parms in it and
			// it has to match the one we set in
			// Facebook.cpp EXACTLY!!!!
			//
			"redirect_uri='+redir+'&"
			"scope=%s';\n"
			//"alert(url);\n"
			"top.location.href=url;\n"

			, APPID 
			//, APPHOSTENCODED
			, APPSCOPE1 
			);
	return true;
}			

bool printLogoutLink( SafeBuf &sb, Msgfb *msgfb , bool igoogle , char *page ) {

	if ( page[0] != '/' ) { char *xx=NULL;*xx=0; }
	// skip / since APPHOST has it already
	page++;

	// print logout link
	sb.safePrintf(//"<font size=-1><b>"
		      "<a "
		      // setting fbid to zero in cookie is basically
		      // what logout=1 was doing anyway!!
		      "onclick=document.cookie='fbid=0;' "
		      "href=\"https://www.facebook.com/logout.php?"
		      "next="
		      //, msgfb->m_fbId 
		      );
	SafeBuf ue;
	//ue.safePrintf("%s%s?&logout=1", APPHOSTUNENCODED, page );
	ue.safePrintf("%s%s", APPHOSTUNENCODED, page );
	if ( igoogle ) ue.safePrintf("?ig=1");
	ue.urlEncode();
	char *access_token = "";
	if ( msgfb->m_fbrecPtr &&
	     msgfb->m_fbrecPtr->ptr_accessToken )
		access_token = msgfb->m_fbrecPtr->ptr_accessToken;
	sb.safePrintf(
		      "%s&access_token=%s\">"
		      , ue.getBufStart()
		      , access_token
		      );
	return true;
}


class InterestEntry {
public:
	char *m_topic;
	int32_t  m_topicLen;
	char  m_valc;
	char  m_type; // 1 = manual, 2 = default, 3 = facebook
	int32_t  m_count;
	int32_t  m_h32;
};


bool printInterestCell ( SafeBuf &sb , 
			 InterestEntry *ie ,
			 int32_t specialHighlightHash ,
			 bool printX ) {
	// what is the new value? make sure that facebook values
	// of 3,4 and 5 remain as facebook values. non facebook values
	// are 0,1 and 2.
	char coff = '0'; if ( ie->m_valc >= '3' ) coff = '3';
	char con  = '1'; if ( ie->m_valc >= '3' ) con  = '4';
	char cdel = '2'; if ( ie->m_valc >= '3' ) cdel = '5';


	sb.safePrintf("<td width=20>");
	if ( printX ) {
		sb.safePrintf("<a "
			      "onmouseover=\"document.getElementBy"
			      "Id('idx_%"INT32"').src='/xon.png';\" "
			      "onmouseout=\"document.getElementBy"
			      "Id('idx_%"INT32"').src='/xoff.png';\" "
			      // delete it? 
			      // nuke cookie for it
			      "onclick=\""
			      // if showpop=1 make sure to transfer that
			      // otherwise we lose it because it has no
			      // hidden input form variable like most
			      // of our parms do.
			      // SearchInput.cpp will look for "topicnuke"
			      // and remove that topic from m_intBuf.
			      "reloadResults(0,'&topic=%c-"
			      ,ie->m_count
			      ,ie->m_count
			      ,cdel
			      );
		// print the topic name
		sb.urlEncode ( ie->m_topic , ie->m_topicLen );
		// end the onclick function
		sb.safePrintf("');\""
			      // crap, the interestcookie then should not
			      // override this...
			      ">"
			      );
		// print a hidden X so they can delete it
		sb.safePrintf("<img "
			      //"align=left "
			      "src=/xoff.png "
			      "width=11 "
			      "height=11 "
			      //"style=display:none "
			      "id=idx_%"INT32">"
			      ,ie->m_count);
		sb.safePrintf("</a>");
	}
	// next cell
	sb.safePrintf("</td><td width=20");
	if ( ie->m_h32 == specialHighlightHash ) {
		sb.safePrintf(" style=background-color:yellow;>");
		sb.safePrintf("<a name=edit></a>");
	}
	else
		sb.safePrintf(">");
	// use an anchor tag to indicate they can click in here
	// to set the checkbox
	// check the checkbox valud
	char *checked = "";
	// 1 is eventguru interest checked, 4 is facebook interest chkd
	if ( ie->m_valc =='1' || ie->m_valc == '4' ) checked = " checked";
	// print out the checkbox for the interest
	sb.safePrintf("<input "
		      "type=checkbox "
		      "onclick=\""
		      "if ( this.checked ) "
		      "reloadResults(false,'&topic=%c-" , con );
	sb.urlEncode ( ie->m_topic, ie->m_topicLen );
	sb.safePrintf("');"
		      "else "
		      "reloadResults(false,'&topic=%c-", coff);
	sb.urlEncode ( ie->m_topic, ie->m_topicLen );
	sb.safePrintf("');"
		      "\" "
		      "id=cb_ixt%02"INT32"%s> "
		      , ie->m_count
		      , checked );
	// set the background to yellow if its highlighted
	// from the user clicking on the interest under the
	// search result (i.e. "Because you like: [Movies]")
	sb.safePrintf("</td><td with=50%%>");
	// limit to one line
	sb.safePrintf("<div style=height:20px;overflow:hidden;>");
	//
	// the text
	//
	sb.safeMemcpy ( ie->m_topic , ie->m_topicLen );
	// end div
	sb.safePrintf("</div>");
	// end cell
	sb.safePrintf("</td>");
	return true;
}


// . only non-nuked entries should be in "ie"
bool printInterestSubTable ( SafeBuf &sb , 
			     InterestEntry *ie , 
			     int32_t nie , 
			     int32_t specialHighlightHash ,
			     bool printX ) {

	int32_t colSize0 = nie / 2;
	// add remainder to first column
	if ( colSize0 * 2 < nie ) colSize0++;

	// print the two interests per row!
	sb.safePrintf("<table width=100%%>");

	for ( int32_t i = 0 ; i < nie ; i++ ) {
		// stop when done
		if ( i >= colSize0 ) break;
		// begin new row
		sb.safePrintf("<tr>");
		// print the interest cell
		printInterestCell ( sb , &ie[i] , specialHighlightHash ,
				    printX );
		// spacer column
		sb.safePrintf("<td>&nbsp;</td>");
		// print the two interests per row!
		if ( i + colSize0 < nie ) 
			printInterestCell (sb,
					   &ie[i+colSize0],
					   specialHighlightHash ,
					   printX );
		else
			sb.safePrintf("<td></td>");
		// end the row
		sb.safePrintf("</tr>\n");
	}
	// end table
	sb.safePrintf("</table>");
	return true;
}


// . special interests for suggestions
// . use javascript client-side to set newinterest into a cookie.
// . then compile the interest cookies in SearchInput.cpp.
// . also, store the interest cookies into
bool printMyInterests ( SafeBuf &sb , State7 *st , bool justPrintHiddens ) {

	SearchInput *si = &st->m_si;
	int32_t *intOffsets = (int32_t *)si->m_intOffsets.getBufStart();

	////////
	//
	// hash the default interests so we know if an interest is default
	//
	////////
	int32_t n = sizeof(s_interests) / sizeof(char *);
	HashTableX dit;
	char ditbuf[2048];
	dit.set(4,0,128,ditbuf,2048,false,0,"dittbl");
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char *di = s_interests[i];
		int32_t h32 = hash32Lower_a ( di ,gbstrlen(di),0);
		dit.addKey(&h32);
	}

	////////
	//
	// set "chk" table which contains the CHECKED default interests
	//
	////////
	HashTableX chk;
	char cbuf[1024];
	chk.set(4,0,32,cbuf,1024,false,0,"chktab");
	for ( int32_t i = 0 ; i < si->m_numInterests ; i++ ) {
		// get it
		char *p = si->m_intBuf.getBufStart() + intOffsets[i];
		// get value
		char valc = p[0];
		// must be checked
		if ( valc != '1' && valc != '4' ) continue;
		// get end of it
		char *e = p;
		for ( ; *e && *e != ';'; e++ );
		// hash it. skip over number and hash
		int32_t h32 = hash32Lower_a(p+2,e-(p+2),0);
		// skip counting if default interest, it's included
		// in the "numGood+n" line below
		if ( ! dit.isInTable(&h32) ) continue;
		// add to the checked table so we can use that
		// when we print out the default topics to see if
		// its checked or not.
		chk.addKey(&h32);
	}

	// dedup interests table
	HashTableX dups;
	char dbuf[2048];
	dups.set(4,0,128,dbuf,2048,false,0,"dtoptbl");

	// store all interests into here
	InterestEntry ie[MAX_INTERESTS];
	int32_t nie = 0;
	int32_t count = 0;

	///////
	//
	// now store the manually entered interests
	//
	///////
	InterestEntry *manualInterests = &ie[nie];
	int32_t numManualInterests = 0;
	for ( int32_t i = 0 ; i < si->m_numInterests ; i++ ) {
		// get it
		char *p = si->m_intBuf.getBufStart() + intOffsets[i];
		// get value
		char valc = p[0];
		// skip if a nuke
		if ( valc == '2' ) continue;// a manually entered delete
		if ( valc == '5' ) continue;// a facebook interest delete
		// first store the facebook entries
		if ( valc != '0' && valc != '1' ) continue;
		// point to it
		char *topic = p + 2;
		char *e = topic; for ( ; *e && *e != ';' ; e++ );
		int32_t  topicLen = e - topic;
		int32_t  h32 = hash32Lower_a ( topic , topicLen );
		// skip if default interest
		if ( dit.isInTable ( &h32 ) ) continue;
		// breach check
		if ( nie >= (int32_t)MAX_INTERESTS ) break;
		ie[nie].m_topic = topic;
		ie[nie].m_topicLen = topicLen;
		ie[nie].m_h32 = h32;
		ie[nie].m_count = count++;
		ie[nie].m_valc   = valc;
		ie[nie].m_type = 1;
		nie++;
		numManualInterests++;
	}
	/////
	//
	// now store the default interests that are checked into "chk" table
	//
	/////
	InterestEntry *defaultInterests = &ie[nie];
	int32_t numDefaultInterests = 0;
	for ( int32_t i = 0 ; i < si->m_numInterests ; i++ ) {
		// get it
		char *p = si->m_intBuf.getBufStart() + intOffsets[i];
		// get value
		char valc = p[0];
		// skip if a nuke
		if ( valc == '2' ) continue;// a manually entered delete
		if ( valc == '5' ) continue;// a facebook interest delete
		// must be checked!
		if ( valc != '1' && valc != '4' ) continue;
		// point to it
		char *topic = p + 2;
		char *e = topic; for ( ; *e && *e != ';' ; e++ );
		int32_t  topicLen = e - topic;
		int32_t  h32 = hash32Lower_a ( topic , topicLen );
		// skip if NOT default interest
		if ( ! dit.isInTable ( &h32 ) ) continue;
		// hash in chk table
		chk.addKey ( &h32 );
	}
	// now scan default interests from s_interests
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// breach check
		if ( nie >= (int32_t)MAX_INTERESTS ) break;
		// int16_tcut
		char *di = s_interests[i];
		int32_t h32 = hash32Lower_a ( di ,gbstrlen(di),0);
		char valc = '0';
		if ( chk.isInTable ( &h32 ) ) valc = '1';
		// store it as well
		ie[nie].m_topic = di;
		ie[nie].m_topicLen = gbstrlen(di);
		ie[nie].m_h32 = h32;
		ie[nie].m_count = count++;
		ie[nie].m_valc   = valc;
		ie[nie].m_type = 2;
		nie++;
		numDefaultInterests++;
	}
	///////
	//
	// store facebook interests into "ie" array last for truncation issues
	//
	///////
	InterestEntry *facebookInterests = &ie[nie];
	int32_t numFacebookInterests = 0;
	for ( int32_t i = 0 ; i < si->m_numInterests ; i++ ) {
		// get it
		char *p = si->m_intBuf.getBufStart() + intOffsets[i];
		// get value
		char valc = p[0];
		// skip if a nuke
		if ( valc == '2' ) continue;// a manually entered delete
		if ( valc == '5' ) continue;// a facebook interest delete
		// first store the facebook entries
		if ( valc != '3' && valc != '4' ) continue;
		// point to it
		char *topic = p + 2;
		char *e = topic; for ( ; *e && *e != ';' ; e++ );
		int32_t  topicLen = e - topic;
		int32_t  h32 = hash32Lower_a ( topic , topicLen );
		// skip if default interest
		if ( dit.isInTable ( &h32 ) ) continue;
		// breach check
		if ( nie >= (int32_t)MAX_INTERESTS ) break;
		// store for printing the table
		ie[nie].m_topic = topic;
		ie[nie].m_topicLen = topicLen;
		ie[nie].m_h32 = h32;
		ie[nie].m_count = count++;
		ie[nie].m_valc   = valc;
		ie[nie].m_type = 3;
		nie++;
		numFacebookInterests++;
	}
	
	//////
	//
	// print the hidden inputs now
	//
	//////
	for ( int32_t i = 0 ; i < nie ; i++ ) {
		// skip if its a default that is not checked...
		if ( ie[i].m_type == 2 && ie[i].m_valc != '1' ) continue;
		// a hidden tag so we get these topics into the meta cookie
		sb.safePrintf("<input type=hidden name=ixt%02"INT32" id=ixt%02"INT32" "
			      "value=\"%c-"
			      , ie[i].m_count
			      , ie[i].m_count
			      , ie[i].m_valc
			      );
		sb.safeMemcpy ( ie[i].m_topic , ie[i].m_topicLen );
		sb.safePrintf("\">");
	}

	// if just printing hidden, we are done
	if ( justPrintHiddens ) return true;


	HttpRequest *hr = &st->m_hr;

	// shoule we highlight one?
	char *hi = hr->getString("hi",NULL,NULL);
	int32_t specialHighlightHash = 0;
	if ( hi ) specialHighlightHash = hash32Lower_a ( hi , gbstrlen(hi) );

	///////////
	//
	// print interest lists header
	//
	///////////
	char *s = " style=display:none;";
	if ( si->m_suggestions ) s = "";
	if ( si->m_numCheckedInterests <= 0 ) s = "";

	sb.safePrintf("<div id=intbuf%s>" , s );

	sb.safePrintf("<br>");

	//int32_t numCols = 2;//1;//3;
	//if ( si->m_igoogle ) numCols = 1;

	int32_t fs = 16;
	if ( si->m_widget ) fs = 12;
	// print the facebook table
	sb.safePrintf("<b style=font-size:%"INT32"px;>"
		      "Facebook Interests</b>"
		      , fs
		      );
	Msgfb *msgfb = &st->m_msgfb;
	// just logout to the root
	if ( si->m_widget ) {
		sb.safePrintf ( " - ");
		printLogoutLink( sb,msgfb,si->m_igoogle,"/");
		sb.safePrintf("<font style=\"color:black;font-size:10px;\">"
			      "logout"
			      "</font>" 
			      "</a>"
			      );
	}

	sb.safePrintf("<br>" );

	printInterestSubTable ( sb , 
				facebookInterests,
				numFacebookInterests,
				specialHighlightHash ,
				true);
	if ( ! msgfb->m_fbId ) {
		sb.safePrintf("<br>"
			      "If you ");
		sb.safePrintf("<a onclick=\"" );
		printLoginScript(sb,"http://www.eventguru.com/");
		sb.safePrintf ( "\" style=\"color:blue;\"><u>"
				"Login with Facebook</u></a> "
				"then Event Guru will automatically "
				"download your "
				"interests from your Facebook profile and "
				"put them here. "
				"<br>"

				);
	}



	// print manually entered interests
	sb.safePrintf("<br>"
		      "<div style="
		      //"background-color:#e5c095;"
		      "padding-top:10px;"
		      "padding-bottom:10px;>"
		      "<b style=font-size:%"INT32"px;>"
		      "Entered Interests"
		      "</b><br><br>"
		      "<input type=text name=newinterest "
		      "style=color:gray;font-size:14px;width:185px; "
		      "onclick=\"this.value='';this.style.color='black';\" "
		      // . if they push enter key submit it
		      // . for some reason chrome needs this but firefox
		      //   does not! firefox calls the <form onsubmit> 
		      //   function...
		      "onkeypress=\""
		      "if(event.keyCode==13)reloadResults();\" "
		      
		      "value=\"Enter new interest here\">"
		      "<font size=-1>"
		      "<br>"
		      "<br>"
		      "</font>"
		      ,fs
		      );
	printInterestSubTable ( sb , 
				manualInterests,
				numManualInterests,
				specialHighlightHash ,
				true);
	sb.safePrintf("</div>");

	// print default interests
	sb.safePrintf("<br>"
		      "<b style=font-size:%"INT32"px;>Common Interests"
		      "</b><br>"
		      , fs 
		      );
	printInterestSubTable ( sb , 
				defaultInterests,
				numDefaultInterests,
				specialHighlightHash ,
				false);
	
	//sb.safePrintf("</td></tr></table>");
	//sb.safePrintf("</div>\n");

	// print instructions
	sb.safePrintf("<table cellpadding=0 cellspacing=0 border=0>"
		      "<tr>"
		      "<td align=left>" );
	sb.safePrintf ( "<br>"
			"Check your interests above. "
			"The events presented below are "
			"based on the interests you check. "
			"Click the X to permanently delete an "
			"interest. "
			);
	sb.safePrintf(  "Event Guru limits you to %"INT32" "
			"interests total."
			"<br>"
			"<br>"
			"</td></tr></table>"
			, (int32_t)MAX_INTERESTS
			);

	return true;
}



#define COLOR_RED     0
#define COLOR_BLUE    1
#define COLOR_PURPLE  2
#define COLOR_GREEN   3
#define COLOR_ORANGE  4
#define COLOR_YELLOW  5
#define COLOR_FLESH   6
#define COLOR_BRIGHT_RED 7
//#define COLOR_MAGENTA 8

// loop this now
class Color {
public:
	char *m_color;
	char *m_hex1;
	char *m_hex2;
	char *m_bgcolor;
};

Color s_gradColors[] = {
	{"red","#ffc0c0","#ff7070","#ff9898"},
	{"blue","#c0f0ff","#70a0ff","#98c8ff"},
	{"purple","#d0d0ff","#a0a0ff","#b8b8ff"},
	{"green","#c0ffc0","#70ff70","#98ff98"},
	{"orange","#ffc178","#ff9b25","#ffb040"},
	{"yellow","#fff299","#ffe525","#fff060"},
	{"flesh","#e1c3a0","#b18f68","#c8b088"},
	{"brightred","#ff0000","#ff7070","#ff3838"}
	//{"magenta","#e893b5","#d42f71","#e8c3d5"}
};

#define SHADOWCOLOR "#000000"

//static bool printQueryBoxes ( SafeBuf &sb, SearchInput *si ) ;

// call this right before printRedBoxes() so we can put tabs
// on the first red box
bool printTabs ( SafeBuf &sb , State7 *st ) {

	SearchInput *si = &st->m_si;

	sb.safePrintf("<nobr>");

	//
	// RECOMMENDATIONS TAB
	//

	sb.safePrintf(
		      // div position holder
		      "<div style=display:inline-block;position:relative;>"
		      // relative div tab mask
		      "<div "
		      "class=hand "
		      "style=\""
		      "position:relative;"
		      // so the next tab can be on the right...
		      //"display:inline-block;"
		      "width:190px;"
		      "height:25px;"
		      "overflow:hidden;"
		      // old firefox needs this
		      "z-index:499;"
		      "\" "
		      "onclick=\""
		      "var e = document.getElementById"
		      "('showpersonal');"
		      "if (e.value==1) return;"
		      "e.value=1;"
		      "reloadResults();"
		      "\" "
		      ">"
		      // absolute div tab
		      "<div "
		      "style=\""
		      "position:relative;"//absolute;"
		      "border-radius:10px;"
		      // light flesh
		      //"background-color:#e1c3a0;"

		      "background-image:url('"
		      //"http://www.backgroundsy.com/file/preview/stainless-steel-texture.jpg"
		      //http://webdesignandsuch.com/posts/stainless/1.jpg"
		      "/ss.jpg"
		      "');"
		      "background-repeat:repeat;"
		      "background-attachment:fixed;"


		      "box-shadow: 6px 6px 3px %s;"
		      "left:20px;"
		      "padding-left:7px;"
		      "padding-right:3px;"
		      "padding-top:3px;"
		      // cartoon border
		      //"border:2px solid black;"
		      "border:1px solid black;"
		      "height:32px;"
		      "width:150px;"
		      "font-size:16px;"
		      "z-index:-1;"
		      "\">"
		      "<b>"
		      "Recommendations"
		      "</b>"
		      "</div>"
		      
		      // end div mask
		      "</div>"
		      , SHADOWCOLOR
		      );

	if ( si->m_showPersonal )
		sb.safePrintf ( // cover up here
			       "<div "
			       "style=\""
			       "position:absolute;"
			       //"background-color:red;"
			       //"background-color:#e1c3a0;"

			      "background-image:url('"
			       "/ss.jpg"
			      "');"
			      "background-repeat:repeat;"
			      "background-attachment:fixed;"


			       "height:4px;"
			       "width:160px;"
			       "top:25px;"
			       "left:21px;"
			       //"top:59px;"
			       //"left:252px;"
			       "z-index:699;"
			       "\">"
			       "</div>"
			       );

	// end primary div container
	sb.safePrintf("</div>");



	// 
	// SEARCH EVENTS TAB
	//
	sb.safePrintf(
		      // div position holder
		      "<div style=display:inline-block;position:relative;>"
		      // relative div tab mask
		      "<div "
		      "class=hand "
		      "style=\""
		      "position:relative;"
		      "display:inline-block;"
		      "width:190px;"
		      "height:25px;"
		      "overflow:hidden;"
		      // old firefox needs this
		      "z-index:499;"
		      "\" "
		      "onclick=\""
		      "var e = document.getElementById"
		      "('showpersonal');"
		      "if (e.value==0) return;"
		      "e.value=0;"
		      "reloadResults();"
		      "\">"
		      // absolute div tab
		      "<div "
		      "style=\""
		      "position:relative;"//absolute;"
		      "border-radius:10px;"
		      // light blue
		      //"background-color:#c0f0ff;"

		      "background-image:url('"
		      "/ss.jpg"
		      "');"
		      "background-repeat:repeat;"
		      "background-attachment:fixed;"


		      "box-shadow: 6px 6px 3px %s;"
		      "left:5px;"
		      "padding-left:7px;"
		      "padding-right:3px;"
		      "padding-top:3px;"
		      // cartoon border
		      //"border:2px solid black;"
		      "border:1px solid black;"
		      "height:32px;"
		      "width:120px;"
		      "font-size:16px;"
		      "z-index:-1;"
		      "\">"
		      "<b>"
		      "Search Events"
		      "</b>"
		      "</div>"

		      // end div mask
		      "</div>"
		      , SHADOWCOLOR
		      );

	if ( ! si->m_showPersonal )
		sb.safePrintf(
			      // cover up here
			      "<div "
			      "style=\""
			      "position:absolute;"
			      //"background-color:red;"
			      //"background-color:#c0f0ff;"

			      "background-image:url('"
			      "/ss.jpg"
			      "');"
			      "background-repeat:repeat;"
			      "background-attachment:fixed;"

			      "height:4px;"
			      "width:130px;"
			      "top:25px;"
			      "left:6px;"
			      //"top:59px;"
			      //"left:252px;"
			      "z-index:699;"
			      "\">"
			      "</div>"
			      );

			      
	// end primary div container
	sb.safePrintf("</div>");

	sb.safePrintf("</nobr>");

	return true;
}


// print a submenu for the left side nav bar. display. map. calendar. ...
bool printSubMenu ( SafeBuf &sb , State7 *st, char subMenu ,
		    bool printHorizontal ) {

	SearchInput *si = &st->m_si;
	Parm *m = NULL;
	Msgfb *msgfb = &st->m_msgfb;

	// print the checkboxes under it
	int32_t i; for ( i = 0 ; i < g_parms.m_numParms ; i++ ) {
		// int16_tcut
		m = &g_parms.m_parms[i];
		// skip until we hit the parms we want
		if ( m->m_subMenu == subMenu ) break;
	}

	// is parm 0 or 1?
	bool isParmSet = m->getValueAsBool ( si );

	// if first one that is the submenu header
	char *title = m->m_title;

	if ( printHorizontal ) {
		sb.safePrintf("<span class=hand "
			      "style=height:27px; "
			      "onclick=\"");
		//if ( subMenu != SUBMENU_SEARCH )
		//	sb.safePrintf("toggleOff2('searchmenu');");
		//if ( subMenu != SUBMENU_LOCATION )
		//	sb.safePrintf("toggleOff2('location');");
		//if ( subMenu != SUBMENU_CATEGORIES )
		//	sb.safePrintf("toggleOff2('categories');");
		if ( subMenu != SUBMENU_TIME )
			sb.safePrintf("toggleOff2('timemenu');");
		if ( subMenu != SUBMENU_CALENDAR )
			sb.safePrintf("toggleOff2('calendar');");
		sb.safePrintf("toggleHide('%s');",m->m_scgi);
	}


	char *grad       = "4";
	char *fontcolor  = "white";
	char *bgcolor    = "lightgray";
	//int32_t  colorIndex = -1;

	// more like apple
	fontcolor = "black";
	grad = "";

	// make them all black now, ppl think the colors are too much.
	// need to look more like apple
	/*
	if ( subMenu == SUBMENU_CATEGORIES ) colorIndex = COLOR_BLUE;
	if ( subMenu == SUBMENU_CALENDAR   ) colorIndex = COLOR_ORANGE;
	if ( subMenu == SUBMENU_LOCATION   ) colorIndex = COLOR_GREEN;//MAGENTA
	if ( subMenu == SUBMENU_TIME       ) colorIndex = COLOR_GREEN;//PURPLE;
	if ( subMenu == SUBMENU_SOCIAL     ) colorIndex = COLOR_YELLOW;
	if ( colorIndex >= 0 ) {
		fontcolor = "black";
		grad      = s_gradColors[colorIndex].m_color;
		bgcolor   = s_gradColors[colorIndex].m_bgcolor;
	}
	*/

	if ( printHorizontal ) fontcolor = "white";

	int32_t fs;
	if ( printHorizontal ) fs = 13;
	else                   fs = 16;

	// each button is now its own table

	// print shadow div?
	if ( ! printHorizontal ) {
		/*
		sb.safePrintf("<div style="
			      "background-color:#404040;"
			      "border-radius:10px;"
			      "margin-left:10px;"
			      ">");
		*/

		// PRINT SUBMENU rounded div
		// rounded corners div mask to fix msie
		// msie can't handle gradient AND rounded corners:
		// http://stackoverflow.com/questions/4692686/ie9-border-radius-and-background-gradient-bleeding
		sb.safePrintf ( "<div "
				"style=\""
				"position:relative;"
				"margin-left:5px;"
				"display:inline-block;"
				//"width:202px;"
				"width:192:px;"
				//"border:2px solid black;"
				"border-radius:10px;"
				"padding:0px;"
				"overflow:hidden;"
				//"bottom:6px;"
				//"right:6px;"
				"box-shadow: 6px 6px 3px %s;"
				"\""
				">"
				, SHADOWCOLOR
				);

		sb.safePrintf ( 
			       "<div "
			       //"class=grad%s "
			       "style=\""
			       "position:relative;"
			       // just white now, no grad. no make it
			       // some textured url.
			       //"background-color:white;"
			       // 

			       "background-image:url('"
			       "/ss.jpg"
			       "');"
			       "background-repeat:repeat;"
			       //"background-position: 900  200px;"
			       //"background-size:100%% 100%%;"
			       "background-attachment:fixed;"
			       

			       //"right:6px;"
			       //"bottom:6px;"
			       "color:%s;"
			       "padding:3px;"
			       "font-size:%"INT32"px;"
			       //"height:27px;"
			       "width:192px;"
			       "cursor:hand;"
			       "cursor:pointer;"
			       // cartoon border
			       //"border:2px solid black;"
			       "border:1px solid black;"
			       "border-radius:10px;"
			       "\">"
			       //, grad 
			       , fontcolor
			       , fs );
		// only the header div is clickable
		sb.safePrintf("<div "
			       "onclick=\""
			      );
	}

		
	if ( subMenu == SUBMENU_WIDGET )
		sb.safePrintf ( 
				// get the menu part of the widget 
				//"var f=document.getElementById"
				//"('ID_widgetmenu');"
				// get this input parm
				"var g=document.getElementById"
				"('widgetmenu');"
				// get hidden tag
				"var e = document."
				"getElementById('showwidget');"
				// if off, turn it on
				"if ( e.value == 0 ){"
				//"f.style.display = '';"
				"e.value=1;"
				"g.value=1;"
				"}"
				"else {"
				//"f.style.display = 'none';"
				"e.value=0;"
				"g.value=0;"
				"}"
				"reloadResults();"
				);
	else if ( ! printHorizontal )
		sb.safePrintf ( "toggleHide('%s');",m->m_scgi );

	if ( ! printHorizontal && subMenu == SUBMENU_MAP )
		sb.safePrintf("toggleMe('balloon',25);" );

	// finish the header div or span
	sb.safePrintf("\">" );


	// abbreviate more to make all on one line
	if ( printHorizontal ) {
		if ( ! strcasecmp(title,"Recommendations") )
			title = "For You";
		if ( ! strcasecmp(title,"Choose Dates") )
			title = "Cal";
		if ( ! strcasecmp(title,"Categories") )
			title = "Topic";
		if ( ! strcasecmp(title,"Distance") )
			title = "Dist";
		if ( ! strcasecmp(title,"Frequency") )
			title = "Freq";
	}		

	//char *nbsp2 = "&nbsp;";
	//if ( printHorizontal && subMenu == SUBMENU_SEARCH )
	//	nbsp2 = "";

	sb.safePrintf ( "<input type=hidden name=%s id=%s value=%"INT32">"
			, m->m_scgi
			, m->m_scgi
			, (int32_t)isParmSet
			);

	if ( ! printHorizontal )
		sb.safePrintf("<nobr>");

	//if ( ! printHorizontal || subMenu != SUBMENU_SEARCH )
	//	sb.safePrintf("&nbsp;");

	sb.safePrintf ( "<b>"
			"&nbsp;%c%s"
			"</b>"
			, to_upper_a(title[0])
			, title + 1
			);

	if ( ! printHorizontal ) {
		sb.safePrintf("</nobr>");
		// end the header div
		sb.safePrintf("</div>");
	}

	if ( printHorizontal )
		// end the header span
		sb.safePrintf("</span>");


	// end this down below so it contains the whole menu now, hidden
	//sb.safePrintf("</div>");

	// horizontal spacer
	if ( printHorizontal )
		sb.safePrintf("&nbsp; ");


	// is it up or down?
	char *hidden = "";
	// toggleHide() should set this hidden input parm 
	// appropriately!
	if ( ! isParmSet ) hidden = " display:none;";

	bool printedDiv = false;
	bool printedSpan = false;

	// formatting
	if ( printHorizontal ) {
		// raise from 130 to 150 to prevent cutoff of checkbox in 
		// the dist menu, "sort by distance"
		char *ws = "width:160px;";
		char *hs = "height:200px;";
		if ( subMenu == SUBMENU_SEARCH ) ws = "";
		if ( subMenu == SUBMENU_CALENDAR ) {
			ws = "width:160px;";
			hs = "height:230px;";
		}
		if ( subMenu == SUBMENU_TIME ) {
			ws = "width:120px;";
			hs = "height:150px;";
		}
		// a hack to push the drop down down if we got a two-row
		// black bar...
		char * top = "top:22px;";
		if ( si->m_widgetWidth <= 191 ) top = "top:42px;";
		if ( si->m_widgetWidth <= 119 ) top = "top:62px;";
		printedDiv = true;
		sb.safePrintf ( //"<div "
				//"style=\""
				//"position:relative;"
				//"display:inline-block;"
				//"\">"

				"<div id=ID_%s style=\""
				"display:none;"
				"background-color:%s;"
				"border:2px black solid;"
				"text-align:right;"
				"color:black;"
				"z-index:1900;"
				"font-size:14px;"
				"%s"
				"%s"
				"overflow-y:auto;"
				"overflow-x:hidden;"
				"position:absolute;"//relative;"
				"%s"
				"left:0px;"
				//"\">"
				, m->m_scgi
				, bgcolor
				, hs
				, ws
				, top
				);
		if ( subMenu == SUBMENU_TIME ) 
			sb.safePrintf("padding:5px;");
		// end it
		sb.safePrintf("\">");

	}
	else {
		printedSpan = true;
		sb.safePrintf ( "<div id=ID_%s "
				//"align=right "
				"style=\""
				"%s" // display:none;?
				"color:%s;"
				"text-align:right;"
				"font-size:14px;"
				"\">"
				, m->m_scgi
				, hidden 
				, fontcolor
				);
	}

	// a small spacer!
	if ( subMenu != SUBMENU_MAP &&
	     subMenu != SUBMENU_LINKS &&
	     ! printHorizontal ) 
		sb.safePrintf("<div style=line-height:10px><br></div>");


	//sb.safePrintf("<span style=font-size:14px>");

	bool print;

	/*
	// print recent locations
	if ( subMenu == SUBMENU_LOCATION ) {
		print = true;
		if ( print &&! si->m_loc1 ) 
			print = false;
		if ( print &&! si->m_loc1[0] ) 
			print = false;
		if ( print &&si->m_where&& !strcmp(si->m_loc1, si->m_where))
			print=false;
		if ( print ) 
			sb.safePrintf ( "%s<input "
					"onclick=\"reloadResults(1);\" "
					"type=checkbox>"
					"<br>" ,
					si->m_loc1 );
		print = true;
		if ( print &&! si->m_loc2 ) 
			print = false;
		if ( print &&! si->m_loc2[0] ) 
			print = false;
		if ( print &&si->m_where&& !strcmp(si->m_loc2, si->m_where))
			print=false;
		if ( print ) 
			sb.safePrintf ( "%s<input "
					"onclick=\"reloadResults(1);\" "
					"type=checkbox>"
					"<br>" ,
					si->m_loc2 );
	}
	*/

	if ( subMenu == SUBMENU_SOCIAL ) {
		// recent people you clicked on their pic for
		print = true;
		if ( print && ! si->m_pic1    ) print = false;
		if ( print && ! si->m_pic1[0] ) print = false;
		if ( print ) 
			sb.safePrintf ( "Liked by %s<input "
					"onclick=\"reloadResults(1);\" "
					"type=checkbox>"
					"<br>" ,
					si->m_pic1 );
		// another person you clicked on
		if ( print && ! si->m_pic2    ) print = false;
		if ( print && ! si->m_pic2[0] ) print = false;
		if ( print ) 
			sb.safePrintf ( "Liked by %s<input "
					"onclick=\"reloadResults(1);\" "
					"type=checkbox>"
					"<br>" ,
					si->m_pic2 );
	}

	//
	// now the rest are controls, checkboxes
	//
	for ( i++ ; i < g_parms.m_numParms ; i++ ) {
		// int16_tcut
		m = &g_parms.m_parms[i];

		// stop when done
		if ( m->m_subMenu != subMenu ) continue;//break;

		// strings = foreground,background,font
		if ( m->m_type == TYPE_STRING ) {
			sb.safePrintf ( "%c%s<input size=8 name=%s "
					"type=text value=\"%s\"><br>"
					, (char)to_upper_a(m->m_title[0])
					, m->m_title + 1
					, m->m_scgi
					, m->getValueAsString(si)
					);
			continue;
		}
		if ( m->m_type == TYPE_STRINGBOX ) {
			sb.safePrintf ( "%c%s &nbsp; "
					"<font size=-2 color=%s "
					"style=\"line-height:.6em;\">" 
					"<br>"
					"<a "
					"style=color:white; "
					"onclick=\"setVal('%s','" 
					, (char)to_upper_a(m->m_title[0])
					, m->m_title + 1
					, GRADFONT
					, m->m_scgi );
			sb.htmlEncode( m->m_def ,gbstrlen(m->m_def),false,0);
			sb.safePrintf ( "');\">"
					"Set to default</a> &nbsp; <br><br>"
					"</font>"
					"<center>"
					"<textarea "
					"rows=10 cols=20 id=%s name=%s>"
					"%s</textarea>"
					"</center>"
					, m->m_scgi
					, m->m_scgi
					, m->getValueAsString(si)
					// for testing
					//, m->m_def
					);
			continue;
		}
		// int32_ts = width,height
		if ( m->m_type == TYPE_LONG ) {
			sb.safePrintf ( "%c%s <input size=4 name=%s "
					"type=text value=%"INT32"><br>"
					, to_upper_a(m->m_title[0])
					, m->m_title + 1
					, m->m_scgi
					, m->getValueAsLong(si)
					);
			// msie hack so spacing is right
			sb.safePrintf("<div style=line-height:0.5em>"
				      "<br></div>");
			continue;
		}

		// if showing social submenu, do not print accept/reject
		// checkboxes if not a turk. those are for turks to use only.
		if ( subMenu == SUBMENU_SOCIAL &&
		     ! si->m_isTurk &&
		     m->m_qterm &&
		     ( ! strcmp (m->m_qterm,"gbrejectedbysomeone") ||
		       ! strcmp (m->m_qterm,"gbhiddenbysomeone") ||
		       ! strcmp (m->m_qterm,"gbacceptedbysomeone") ) )
			continue;

		// print icon if there
		//if ( m->m_icon )
		//	sb.safePrintf("<img height=16 width=16 src=%s> ",
		//		      m->m_icon );
		// get value
		isParmSet = m->getValueAsBool ( si );
		// otherwise its a checkbox i guess
		char *checked = ""; 
		if ( isParmSet ) checked = " checked";
		// we have to use a hidden parm since these do not 
		// post anything if the box is unchecked. the "Icons"
		// checkbox is checked by default so if its unchecked we
		// have no way of knowing that we should use the default
		// value or we should not display the icons. so we must
		// toggle this value when the checkbox is clicked!
		// BUT ONLY do this if default value is CHECKED!
		// NO, it turns out there is a race condition between the
		// submit() and setting the checkbox value otherwise, so
		// let's do this for all checkboxes now.
		// NOW we need an "id" as well so setVal() function works!
		sb.safePrintf("<input type=hidden id=%s name=%s value=%"INT32"",
			      m->m_scgi,
			      m->m_scgi,
			      (int32_t)isParmSet );
		if ( m->m_class )
			sb.safePrintf(" class=%s",m->m_class);
		sb.safePrintf(">");
		// print a nameless checkbox then
		sb.safePrintf ( "%c%s <input " // name=%s "
				, to_upper_a(m->m_title[0])
				, m->m_title + 1 );
		// radio button simulation?
		char *radio = "";
		if ( m->m_class ) {
			sb.safePrintf("class=%s ",m->m_class );
			radio = "Radio";
			//if ( ! strcmp(m->m_class,"soc" ) )
			//	radio = "Radio2";
		}


		// if not logged in, and no fbid, do not allow use of
		// friends invited/goingto/likes  or iam invited/likes...
		bool popup = false;
		if ( msgfb && 
		     msgfb->m_fbId == 0LL &&
		     m->m_subMenu == SUBMENU_SOCIAL ) {
			if (!strncasecmp(m->m_scgi,"friends",7)) popup = true;
			if (!strncasecmp(m->m_scgi,"i",1)) popup = true;
		}


		if ( popup )
			sb.safePrintf ( "onclick=\"needLogin();"
					"this.checked=0;" );
		else 
			sb.safePrintf ( "onclick=\"toggleBool%s(this,'%s');"
					, radio
					, m->m_scgi );


		// a few checkboxes just toggle the display:none of stuff
		// in the search results iframe
		bool tog = false;
		if ( m->m_subMenu == SUBMENU_DISPLAY ) {
			if ( ! strcasecmp(m->m_scgi,"icons"  ) ) tog = true;
			if ( ! strcasecmp(m->m_scgi,"images" ) ) tog = true;
			if ( ! strcasecmp(m->m_scgi,"going"  ) ) tog = true;
			if ( ! strcasecmp(m->m_scgi,"likers" ) ) tog = true;
			if ( ! strcasecmp(m->m_scgi,"invited") ) tog = true;
		}

		// i disabled for now because when displaying the widget
		// we need to RELOAD because we can't get the tags we need
		// to turn them invisible because the widget is an iframe!
		//tog = false;

		// the toggleBool() function below will save cookie
		//if ( tog ) sb.safePrintf("toggleMe('%s',25);", m->m_scgi );
		// the default function is submit to the iframe
		//else if ( ! popup ) sb.safePrintf("reloadResults(1);" );
		if ( ! popup ) sb.safePrintf("reloadResults(1);" );

		// close up the checkbox
		sb.safePrintf ( "\" "
				"id=cb_%s "
				//"align=right "
				"style=text-align:right; "
				"type=checkbox%s><br>"
				// hack for msie (chrome needs the div,
				// so you can just style the br tag)
				"<div style=line-height:0.5em><br></div>"
				, m->m_scgi
				, checked );
	}

	// print search boxes
	//if ( subMenu == SUBMENU_SEARCH ) {
	//	// prints the query box table
	//	printQueryBoxes ( sb , si );
	//}

	if ( subMenu == SUBMENU_MAP ) {
		sb.safePrintf("<center>");
		sb.safePrintf("<div style=\"padding:3px 3px 3px 3px;\">");
		// had to make this 180 down from 185 so i.e. didn't
		// inflate the sidebar width when map was displayed
		printMap ( sb , st , 180 , 180 );
		sb.safePrintf("</div>");
		sb.safePrintf("</center>");
	}

	if ( subMenu == SUBMENU_CALENDAR ) {
		sb.safePrintf("<center>");
		printCalendar ( sb , st );
		sb.safePrintf("</center>");
	}

	if ( subMenu == SUBMENU_LINKS )
		printLinks ( sb , st );

	if ( subMenu == SUBMENU_WIDGET ) {
		sb.safePrintf("<input type=submit value=Submit"
			      " onclick=\"reloadResults(1);return;\""
			      ">");
	}

	// close up font-size span tag
	//sb.safePrintf("</span>");

	// close up the controls cell
	//if ( ! printHorizontal ) sb.safePrintf ( "</td></tr>" );

	// end the invisible div
	//if ( printHorizontal ) sb.safePrintf("</div>");

	// end the menu span
	if ( printedSpan )
		sb.safePrintf("</div>");
	if ( printedDiv ) {
		// close the absolute div
		sb.safePrintf("</div>");
		// close the relative div
		//sb.safePrintf("</div>");
	}

	if ( ! printHorizontal ) {
		// end button div
		sb.safePrintf("</div>");
		// end mask div
		sb.safePrintf("</div>");
		// end shadow div
		//sb.safePrintf("</div>");
		// spacer row
		sb.safePrintf("<div style=line-height:10px><br></div>");
	}

	// shadow row. widget is last menu, don't do it for that one
	//if ( subMenu != SUBMENU_WIDGET && ! printHorizontal )
	//	sb.safePrintf("<tr cellspacing=5 height=5px>"
	//		      //"<td colspan=2 bgcolor=#d0d0d0></td></tr>" );
	//		      "<td colspan=2></td></tr>" );
	return true;
}



bool printLogoTD ( SafeBuf &sb , bool printSlogan ) {
	sb.safePrintf ( // -- logo cell
		       //"<td width=15px>&nbsp;&nbsp;&nbsp;</td>"
		       //"<br>"
		       "<center>"
		       "<a href=http://www.eventguru.com/>"
		       "<img border=0 width=%"INT32"px height=%"INT32"px "
		       "title=\"Event Guru Home\" "
		       "src=%s></a>"
		       , GURUDX
		       , GURUDY
		       , GURUPNG
		       );

	if ( printSlogan )
		sb.safePrintf (
			       "<br>"
			       "<font style=\"color:#a0cf02;font-size:18px;"
			       "text-shadow: 4px 4px 2px black;\">"
			       "<b>What's Hoppening?</b>"
			       "</font>"
			       );

	sb.safePrintf (
		       //"<nobr>"
		       //"<font size=-1 color=%s><i>"
		       //"\"The largest event "
		       //"search engine "
		       //"in the U.S.\"</i></font>"
		       //"</nobr>"
		       "</center>"
		       //"<br>"
		       //"</td>"
		       );
	return true;
}

// print the ad table
bool printAdTable ( SafeBuf &sb ) {
	sb.safePrintf("<table border=0>"
		      "<tr><td width=150px>"
		      //"<div style=background-color:#707070;>"
		      "<div "
		      "class=gradyellow "
		      "style=\"border:2px solid black;"
		      "position:relative;"
		      //"bottom:4px;"
		      //"right:4px;"
		      "box-shadow: 6px 6px 3px %s;"
		      "font-size:14px;"
		      "padding:4px;"
		      "background-color:lightyellow;\">"
		      "<center>"
		      "<b><font color=red>"
		      "<nobr>"
		      "<a href=/friends.html style=color:blue>"
		      "Make $1 per Friend</a>"
		      "</nobr>"
		      "</font></b>"
		      "<br>"
		      "<font size=-1>Promote Event Guru</font>"
		      "</center>"
		      "</div>"
		      //"</div>"
		      "</td>"
		      "</tr>"

		      // spacer row
		      "<tr><td>"
		      "<div style=line-height:8px;><br></div>"
		      "</td></tr>"

		      "<tr>"
		      "<td>"
		      //"<div style=background-color:#505050;>"
		      "<div "
		      "class=gradyellow "
		      "style=\"border:2px solid black;"
		      "position:relative;"
		      //"bottom:4px;"
		      //"right:4px;"
		      "box-shadow: 6px 6px 3px %s;"
		      "font-size:14px;"
		      "padding:4px;"
		      "background-color:lightyellow;\">"
		      "<center>"
		      "<b>"
		      "<a href=/widget.html style=color:blue;>"
		      "Make $1 per User"
		      "</a>"
		      "</b>"
		      "<br>"
		      "<font size=-1>"
		      "Install the Widget"
		      "</font>"
		      "</center>"
		      "</div>"
		      //"</div>"
		      "</td>"
		      "</tr>"

		      "</table>" 
		      , SHADOWCOLOR
		      , SHADOWCOLOR
		      );
	return true;
}

// left nav, side nav, leftnav, sidenav
bool printSideBarNav ( SafeBuf &sb , State7 *st , bool printHorizontal ) {

	if ( printHorizontal )
		sb.safePrintf ("<table width=400px height=27px "
			       "border=0 cellspacing=0>");
	else
		sb.safePrintf ("<table width=196px "
			       "border=0 cellspacing=0>");

	// print logo here now
	sb.safePrintf ( "<tr>"
			"<td colspan=2 "
			"style=padding-top:20px;padding-bottom:20px;>"
			);
	printLogoTD ( sb , true );

	///////////////
	//
	// ICONS BENEATH THE LOGO
	//
	///////////////

	sb.safePrintf(//"<br>"
		      "<center>"
		      );

	/*
	//
	// RECOMMENDED ICON
	//
	sb.safePrintf(
		      "<a "
		      "onclick=\""
		      //"document.getElementById('cb_showpersonal')"
		      //".checked=true;"
		      "var e = document.getElementById"
		      "('showpersonal');"
		      "if (e.value==1)e.value=0;"
		      "else e.value=1;"
		      // hidden input
		      //"document.forms[0].elements['suggestions'].value=1;"
		      // and cookie. this is part of meta cookie now.
		      // reloadResults() should put it into the meta cookie
		      // because we got a form tag for it.
		      //"document.cookie = 'suggestions=1';"
		      // then reload
		      "reloadResults();\">"
		      
		      "<img width=32 height=32 src=/point.png "
		      "title=\"Show events based on your interests.\">"
		      "</a>"
		      " &nbsp; "
		      );
	*/

	/*
	//
	// FRIENDS ICON
	//
	if ( ! st->m_msgfb.m_fbId )
		sb.safePrintf ( "<a onclick=\"needLogin();\">" );
	else
		// need to turn off just for you?
		sb.safePrintf ( "<a onclick=\"setVal('showfriends',1);"
				//"document.forms[0].elements['suggestions']."
				//"value=0;"
				"setVal('showpersonal',0);"
				"reloadResults();"
				"\">"
				);
	sb.safePrintf ( "<img width=32 height=32 src=/friends32.png "
			"title=\"Show events your friends are into.\">"
			"</a>"
			" &nbsp; "
			);
	//
	// MY EVENTS ICON
	//
	if ( ! st->m_msgfb.m_fbId ) {
		sb.safePrintf ( "<a onclick=\"needLogin();\">" 
				"<img border=0 "
				"title=\"Show events you like, are going to "
				"or have been invited to.\" "
				"src=/fbface32.png "
				"width=32 height=32>"
				);
	}	
	else {
		sb.safePrintf ( //"<a href=/?showmystuff=1&showpersonal=0>"
				"<a onclick=\"setVal('showmystuff',1);"
				//"document.forms[0].elements['suggestions']."
				//"value=0;"
				"setVal('showpersonal',0);"
				"reloadResults();"
				"\">"
				"<img border=0 "
				"title=\"Show events you like, are going to "
				"or have been invited to.\" "
				"src=http://graph.facebook.com/%"UINT64"/picture "
				"width=32 height=32>"
				, st->m_msgfb.m_fbId );
	}
	sb.safePrintf(" &nbsp; ");

	//
	// WIDGET ICON
	//

	sb.safePrintf ( "<a href=\"/?showwidget=1&widgetmenu=1&suggestions=0&categories=0&calendar=0&socialmenu=0&location=0&timemenu=0&linksmenu=0&display=0&map=0\">"
			"<img border=0 "
			"title=\"Install this widget on your website. "
			"Make $1 per click.\" "
			"src=/gears32.png "
			"width=32 height=32>"
			"</a>"
			"</center>"
			);
	*/	




	//
	// CLOSE UP THE LOGO TD CELL (with icons beneath it)
	//
	sb.safePrintf ( "</td></tr>" );

	// put these submenus all in one row/td now...
	sb.safePrintf("<tr><td>" );

	//printSubMenu ( sb , st , SUBMENU_SUGGESTIONS , printHorizontal );
	//printSubMenu ( sb , st , SUBMENU_CATEGORIES , printHorizontal );
	//if ( printHorizontal )
	printSubMenu ( sb , st , SUBMENU_CALENDAR , printHorizontal );
	//printSubMenu ( sb , st , SUBMENU_LOCATION , printHorizontal );
	printSubMenu ( sb , st , SUBMENU_TIME , printHorizontal );
	printSubMenu ( sb , st , SUBMENU_SOCIAL , printHorizontal );
	printSubMenu ( sb , st , SUBMENU_MAP , printHorizontal );
	printSubMenu ( sb , st , SUBMENU_DISPLAY , printHorizontal );
	printSubMenu ( sb , st , SUBMENU_LINKS , printHorizontal );
	printSubMenu ( sb , st , SUBMENU_WIDGET , printHorizontal );

	/*
	//
	// THUMBS UP WITH SHADOW and MSIE div mask
	//
	// thumbs up icon shadow
	sb.safePrintf("<div style="
		      "background-color:#404040;"
		      "display:inline-block;"
		      "border-radius:10px;"
		      "margin-left:10px;"
		      ">");
	// thumbs up div mask for msie
	sb.safePrintf ( "<div "
			"style=\""
			"position:relative;"
			"display:inline-block;"
			"border-radius:10px;"
			"padding:0px;"
			"overflow:hidden;"
			"bottom:6px;"
			"right:6px;"
			"\""
			">"
			);
	// thumbs up icon
	sb.safePrintf("<div "
		      "class=grad3 "
		      "style=\""
		      "display:inline-block;"
		      "position:relative;"
		      "border:2px solid black;"
		      "border-radius:10px;"
		      "padding:4px;"
		      "\">"

		      "<a href=/?like=1>"
		      "<img height=24px width=24px "
		      "src=/biglike.png>"
		      "</a>"
		      "</div>"
		      "</div>"
		      "</div>"
		      );


	//
	// TWITTER WITH SHADOW and MSIE div mask
	//
	// icon shadow
	sb.safePrintf("<div style="
		      "background-color:#404040;"
		      "display:inline-block;"
		      "border-radius:10px;"
		      "margin-left:10px;"
		      ">");
	// div mask for msie
	sb.safePrintf ( "<div "
			"style=\""
			"position:relative;"
			"display:inline-block;"
			"border-radius:10px;"
			"padding:0px;"
			"overflow:hidden;"
			"bottom:6px;"
			"right:6px;"
			"\""
			">"
			);
	// icon with gradient
	sb.safePrintf("<div "
		      "class=grad3 "
		      "style=\""
		      "display:inline-block;"
		      "position:relative;"
		      "border:2px solid black;"
		      "border-radius:10px;"
		      "padding:4px;"
		      "\">"

		      "<a href=/?like=1>"
		      "<img height=24px width=24px "
		      "src=/twitter32.png>"
		      "</a>"
		      "</div>"
		      "</div>"
		      "</div>"
		      );

	*/


	sb.safePrintf("</td></tr>" );

	SearchInput *si = &st->m_si;

	if ( ! si->m_forForm ) {
		sb.safePrintf("<tr><td colspan=2><br><center>");
		printAdTable ( sb );
		sb.safePrintf("</center></td></tr>");
	}

	sb.safePrintf ("</table>");
	return true;
}

// page is either / or /about.html or /addevent
bool printBlackBar ( SafeBuf &sb , Msgfb *msgfb , char *page , int32_t ip ,
		     bool printLogo , bool igoogle , State7 *st ) {

	char *grad = " class=grad4";
	if ( igoogle ) grad = "";
	SearchInput *si = &st->m_si;

	sb.safePrintf ( // BEGIN TOP TABLE
			"<table id=toptable "
			"width=100%% cellpadding=0 cellspacing=0 border=0 "
			//"class=grad1>"
			"style=background-color:transparent;>"
			// top nav bar row
			"<tr bgcolor=black height=22px>"
			// msie won't handle class=grad4 on <tr> so
			// gotta put it on <td>
			"<td colspan=9 style=font-size:16px; valign=center%s>"

			//"<span style=font-size:16px>"

			// a table just for blackbar
			"<table id=bbar cellspacing=0 cellpadding=0 "
			"border=0>"

			"<tr>"
			"<td>"
			//"<b>"
			, grad
			);

	if ( ! igoogle )
		sb.safePrintf("&nbsp;");

	if ( igoogle ) {
		sb.safePrintf("<font style=color:white;font-size:13px;>");
		// print each submenu as a relative div
		//printSubMenu ( sb,st,SUBMENU_SEARCH,true );
		//printSubMenu ( sb,st,SUBMENU_CATEGORIES,true );
		//printSubMenu ( sb,st,SUBMENU_LOCATION,true );
		int32_t hp1 = 20;
		int32_t hp2 = 22;
		char *bg1 = "gray";
		char *bg2 = "white";
		if ( si->m_showPersonal ) {
			hp1 = 22;
			hp2 = 20;
			bg1 = "white";
			bg2 = "gray";
		}
		printSubMenu ( sb,st,SUBMENU_CALENDAR ,true);
		printSubMenu ( sb,st,SUBMENU_TIME ,true);
		// do not split the widget's tabs, they need to be
		// in contact with the white background that holds
		// the red boxes and search results
		sb.safePrintf("<nobr style=display:inline;>");
		//
		// a simplified version of the tabs here
		//
		sb.safePrintf(
			      // div mask
			      "<div style=\""
			      "height:%"INT32"px;"
			      // added 5px for spacing
			      "width:63px;"
			      "vertical-align:top;"
			      "display:inline-block;"
			      "overflow:hidden;\">"
			      // div tab
			      "<div style=\""
			      "height:44px;"
			      "width:58px;"
			      "background-color:%s;"
			      "border-radius:10px;"
			      "\">"

			      "<span class=hand "
			      "style=height:27px;color:black; "
			      "onclick=\""
			      "var e = document.getElementById"
			      "('showpersonal');"
			      "e.value=1;"
			      "reloadResults();"
			      "\">" 
			      "<b style=color:black;>"
			      "&nbsp;For You" // foryou
			      "</b>"
			      "</span>"
			      "</div>"
			      "</div>"
			      //"&nbsp; "
			      , hp1
			      , bg1
			      );

		sb.safePrintf(
			      // div mask
			      "<div style=\""
			      "height:%"INT32"px;"
			      "width:51px;"
			      "vertical-align:top;"
			      "display:inline-block;"
			      "overflow:hidden;\">"
			      // div tab
			      "<div style=\""
			      "height:44px;"
			      "width:51px;"
			      "background-color:%s;"
			      "border-radius:10px;"
			      "\">"

			      "<span class=hand "
			      "style=height:27px;color:black; "
			      "onclick=\""
			      "var e = document.getElementById"
			      "('showpersonal');"
			      "e.value=0;"
			      "reloadResults();"
			      "\">" 
			      "<b style=color:black;>"
			      "&nbsp;Search"
			      "</b>"
			      "</span>"
			      "</div>"
			      "</div>"
			      //"&nbsp; "
			      , hp2
			      , bg2
			      );
		sb.safePrintf("</nobr>");
		//printSubMenu ( sb,st,SUBMENU_SUGGESTIONS ,true);
		sb.safePrintf("<span id=login>");
	}



	if ( ! igoogle )
		sb.safePrintf (
			       "<b>"
			       "<a href=/ "
			       "style=\"color:white\">"
			       "<nobr>"
			       "Event Guru"
			       "</nobr>"
			       "</a>"
			       "</b>"
			       //"&nbsp;&nbsp;&nbsp;&nbsp;"
			       "</td><td width=25px></td>"
			       "<td>"
			       "<b>"
			       //"<a onclick=\"reloadResults(1,'cities=1');\" "
			       "<a "
			       "href=\"/?cities=1\" "
			       //"onclick=\""
			       //"reloadResults(0,'&cities=1');\" "
			       "style=\"color:white;\">"
			       "Cities</a>"
			       "</b>"
			       //"&nbsp;&nbsp;&nbsp;&nbsp;"
			       "</td><td width=25px></td><td>"
			       "<b>"
			       "<a href=/addevent "
			       "style=\"color:white\">"
			       "<nobr>Add Event</nobr></a>"
			       "</b>"
			       //"&nbsp;&nbsp;&nbsp;&nbsp;"
			       "</td><td width=25px></td><td>"
			       "<b>"
			       "<a href=/about.html "
			       "style=\"color:white\">"
			       "About</a>"
			       "</b>"
			       //"&nbsp;&nbsp;&nbsp;&nbsp;"
			       "</td><td width=25px></td><td>"
			       "<b>"
			       "<a href=/blog.html "
			       "style=\"color:white\">"
			       "Blog</a>"
			       "</b>"
			       //"&nbsp;&nbsp;&nbsp;&nbsp;"
			       
			       
			       
			       "</td>"
			       "<td width=30px></td>"
			       "<td valign=center id=login>"
			       );


	// we need user_events and friends_events otherwise we can't
	// access the event_member table!!!! unfortunately we have no way
	// of knowing which events are private and which are public!!! wtf?
	// we NEED email up front so we can ensure engagement, i think its
	// worth the sacrifice.
	//char *scope = "user_events,friends_events,email";
	// only ask for these when needed!
	// rsvp_event,email,user_location,user_interests";
	// if its matt, coming from qwest at home
	//if ( ip == 137470027 ) // atoip("75.160.49.8"
	//	scope = "user_events,friends_events,"
	//		"offline_access,create_event,email";


	if ( msgfb->m_fbId && ! igoogle ) {
		char *name = "";
		if ( msgfb->m_fbrecPtr ) name = msgfb->m_fbrecPtr->ptr_name;
		// do not print pic and name for igoogle
		if ( ! igoogle )
			sb.safePrintf(
				      "<a "
				      "style=\"color:white;\" "
				      "href=/account.html>"
				      "<img align=left border=0 "
				      "src=http://graph.facebook.com/"
				      "%"UINT64"/picture "
				      "width=20 height=20>"
				      "<nobr>"
				      " &nbsp; "
				      "<font size=-1><b>"
				      "%s"
				      "</b></font>"
				      "</a> &nbsp; &nbsp; &nbsp; "
				      , msgfb->m_fbId 
				      , name 
				      );
		printLogoutLink( sb, msgfb , igoogle , page );
		sb.safePrintf("<font size=-1 style=\"color:white;\">"
			      "<b>"
			      "logout"
			      "</b></font>" 
			      "</a>"
			      "</nobr>"
			      );
	}
	else if ( ! igoogle ) {
		// . developers.facebook.com/docs/reference/api/permissions/
		// . ask for offline access perm to make access token permanent
		sb.safePrintf("<a onclick=\"" );
		printLoginScript(sb);
		sb.safePrintf("\">"
			      "<img "
			      "align=center width=132 height=20 "
			      "src=/fblogin.png border=0></a>" 
			      );
	}

	if ( igoogle ) {
		sb.safePrintf("</span>"); // login span
		//sb.safePrintf("</nobr>");
		sb.safePrintf("</font>");
		sb.safePrintf("</b>");
		// end black bar table
		sb.safePrintf("</td></tr></table>");
		// end TOP TABLE
		sb.safePrintf("</td></tr></table>");
		return true;
	}

	// spacer
	sb.safePrintf ( "</td><td width=40px></td><td>" );

	// like in blackbar
	// only show if logged in i guess then we can make it post
	// on their wall or something.
	sb.safePrintf("<a href=/?like=1>"
		      "<img valign=center align=center "
		      "src=/like.png width=20 height=20>"
		      //" <font size=-1 color=white>Like &nbsp;</font>"
		      "</a>"
		      );

	// spacer
	sb.safePrintf ( "</td><td width=10px></td><td>");

	// . twitter in blackbar
	// . prints an <a href=> tag
	printTwitterLink ( sb );
	sb.safePrintf("<img valign=center align=center "
		      "src=/twitter32.png width=20 height=20>"
		      //" <font size=-1 color=white>Tweet&nbsp;</font>"
		      "</a>"
		      );




	// facebook like box thumb
	/*
	sb.safePrintf ( "<div "
			"style=\""
			"display:inline;"
			"width:50px;"
			//"border:2px solid black;"
			//"border-bottom: 5px solid black;\" "
			"\" "
			"class=fb-like "
			"data-href=\"http://www.flurbit.com/\" "
			"data-send=false "
			"data-layout=button_count "
			//"data-width=50 "
			//"data-height=20 "
			"data-show-faces=false "
			"data-colorscheme=dark "
			"data-font=arial "
			//"data-stream=false "
			//"data-header=false "
			">"
			"</div>" );
	*/

	//char *hs = "";
	//if ( printLogo ) hs = " height=190px";

	sb.safePrintf (	//"</b>"
		        //"</nobr>"
		        "</td></tr></table>"
			// the font-size span
			"</span>"
			"</td>"
			"</tr>" 
			"</table>"
			/*
			// - logo/querybox row
			"<tr "//%s "//bgcolor=#e0e0e0 "
			//"class=grad "
			"style=\""
			"color:%s;"
			"\">"
			//, hs
			, GRADFONT
			*/
			);

	// print the horizontal menus underneath blackbar
	


	return true;
}


bool printUnsubscribedPopup2 ( SafeBuf &sb , Msgfb *msgfb ) {
	//Msgfb *msgfb = &st->m_msgfb;
	// return if already logged in. no need to print it
	//if ( msgfb && msgfb->m_fbId ) return true;
	sb.safePrintf(
		      "<div "
		      "id=unsub "
		      "class=grad3 "
		      "style=\""
		      //"display:none;"
		      "position:fixed;"
		      "background-color:black;"
		      "z-index:999;"
		      "border: 10px solid black;"
		      
		      "top:60px;"
		      "right:60px;"
		      "left:60px;"
		      "height:320px;"

		      "\""
		      ">"
		      
		      "<br>"
		      
		      "<table width=100%%>"
		      "<tr>"
		      "<td width=25%% valign=center>"
		      "<center>"
		      "<img width=%"INT32"px height=%"INT32"px "
		      "src=%s>"
		      , SITTINGDX128
		      , SITTINGDY128
		      , SITTING
		      );

	sb.safePrintf(
		      "</center>"
		      "</td>"
		      
		      "<td>"
		      
		      "<center>"
		      "<b>"
		      "<font id=wmsg "
		      "style=\"color:white;font-size:18px;"
		      "text-shadow: 2px 4px 7px black;\">"
		      "You have been unsubscribed."
		      "</font>"
		      "</b>"
		      "<br>"
		      "<br>"
		      "<br>"
		      
		      "<table width=450px><tr><td>"
		      "<font style=\"color:white;font-size:16px;"
		      "line-height:24px;"
		      "text-shadow: 2px 4px 7px black;\">"
		      "We are sad to see you go. Please let us know "
		      "how we can improve the services on "
		      "<u><a style=color:white href=\"%s\">our wall</a></u>."
		      "<br>"
		      "</font>"
		      "</td></tr></table>"
		      "<br>"
		      "<br>"
		      "<a onclick=\"document.getElementById"
		      "('unsub').style.display='none';\">"
		      "Click here to exit."
		      "</a>"
		      
		      "</td>"
		      
		      "<td width=25%%>"
		      "</td>"
		      
		      "</tr>"
		      "</table>"
		      
		      "</div>\n"
		      , APPFBPAGE
		      );
	return true;
}

bool printAddSearchProvider ( SafeBuf &sb ) {
	sb.safePrintf(
		      /*
		      "var osu='https://blekko.com/s/blekko.xml';var osu2='https://blekko.com/s/blekko_https.xml';function should_ask_to_install_osp()"

	"{var a=navigator.userAgent.toLowerCase();var m=a.match(/(firefox|msie [789])/);var b=(m&&m.length?m[0]:'');if(b!=''&&get_cookie('osp')!='no'){var e=window.external;if(e&&(\"AddSearchProvider\"in e)){if(!('IsSearchProviderInstalled'in e)||((e.IsSearchProviderInstalled(osu)==0)&&(e.IsSearchProviderInstalled(osu2)==0))){return 1;}}}"
		      "return 0;}"
		      "function osp(a)"
	"{var t=3652;$(\"#default-search\").slideUp();if(a==\"yes\"){var e=window.external;if(e&&(\"AddSearchProvider\"in e)){if(window.location.protocol=='https'){e.AddSearchProvider(osu2);}"
		      "else{e.AddSearchProvider(osu);}}}"
		      "if(a==\"later\"){t=30;}"
		      "if(a==\"clear\"){set_cookie('osp','',1);}"
		      "else{set_cookie('osp','no',t);}"
		      "return false;}"
		      function searchPlugin(a)
	{$(\"#plugin-promo\").slideUp();if(a==\"yes\"){set_cookie(\"ffplugin\",1,3652);window.open(\"https://addons.mozilla.org/en-US/firefox/addon/blekko-search-plugin/\");}else{set_cookie(\"ffplugin\",1,30);}
	return false;}
		      */


		      "<script>\n"
		      "function addEngine() {\n"
		      "if (window.external && "
		      "('AddSearchProvider' in window.external)) {\n"
			// Firefox 2 and IE 7, OpenSearch
		      "window.external.AddSearchProvider('http://"
		      "www.eventguru.com/eventguru.xml');\n"
		      "}\n"
		      "else if (window.sidebar && ('addSearchEngine' "
		      "in window.sidebar)) {\n"
			// Firefox <= 1.5, Sherlock
		      "window.sidebar.addSearchEngine('http://"
		      "www.eventguru.com/eventguru.xml',"
		      //"example.com/search-plugin.src',"
		      "'http://www.eventguru.com/eventguru.png',"
		      "'Search Plugin', '');\n"
		      "}\n"
		      "else {"
		      // No search engine support (IE 6, Opera, etc).
		      "alert('No search engine support');\n"
		      "}\n"
		      // do not ask again if they tried to add it
		      // meta cookie should store this
		      //"document.getElementById('addedse').value='1';\n"
		      // NEVER ask again! permanent cookie
		      "document.cookie = 'didse=3';"
		      // make it invisible again
		      "var e = document.getElementById('addse');\n"
		      "e.style.display = 'none';\n"
		      "}\n"

		      // . script to slide the div up
		      // . trigger on a scroll position of...
		      "function slideup () {\n"
		      // only call slideup2() once the user has scrolled
		      // down some
		      //"alert(document.body.scrollTop);\n"
		      "if ( document.body.scrollTop < 300 ) {\n"
		      // call us again if searcher has not scrolled down yet
		      "setTimeout('slideup()',300);\n"
		      "return;\n"
		      "}\n"
		      "var e = document.getElementById('addse');\n"
		      // set the height to 10
		      "e.style.height='10';\n"
		      // and bottom to 0
		      "e.style.bottom='0';\n"
		      // make it visible
		      "e.style.display='';\n"
		      // set the slideup timeout, 100ms intervals
		      "setTimeout('slideup2()',100);\n"
		      "}\n"

		      // script to slide the div up called every 100ms
		      "function slideup2 () {\n"
		      "var e = document.getElementById('addse');\n"
		      "var h = parseInt(e.style.height);\n"
		      //"alert('h='+h);\n"
		      "if ( h < 75 ) {\n"
		      "h = h + 10;\n"
		      "e.style.height = h+'px';\n"
		      // set the slideup timeout, 100ms intervals
		      "setTimeout('slideup2()',30);\n"
		      //"alert('newh='+h);\n"
		      "return;\n"
		      "}\n"
		      "var b = parseInt(e.style.bottom);\n"
		      "if ( b < 10 ) {\n"
		      "b = b + 5;\n"
		      "e.style.bottom = b+'px';\n"
		      // set the slideup timeout, 100ms intervals
		      "setTimeout('slideup2()',30);\n"
		      "}\n"
		      "}\n"


		      "</script>\n"

		      "<div "
		      "id=addse "
		      "class=grad3 "
		      "style=\""
		      "display:none;"
		      "position:fixed;"
		      "overflow-y:hidden;"
		      "z-index:911;"
		      // cartoon border
		      //"border: 2px solid black;"
		      "border:1px solid black;"
		      "box-shadow: 6px 6px 3px %s;"
		      //"border-radius:10px;"
		      
		      "bottom:0px;"//10px;"
		      "right:10px;"
		      "height:0px;"//75px;"
		      "width:300px;"

		      "\""
		      ">"
		      
		      // if they click on the X then add didse cookie
		      // but expire in one day!!!
		      "<img id=asei "
		      "src=/xoff.png border=0 style=\"position:absolute;"
		      "right:5px;top:5px;\" "

		      "onmouseover=\"document.getElementBy"
		      "Id('asei').src='/xon.png';\" "
		      "onmouseout=\"document.getElementBy"
		      "Id('asei').src='/xoff.png';\" "

		      "onclick=\""
		      "document.getElementById('addse')."
		      "style.display='none';document."
		      "cookie='didse=2;max-age=3600;';\">"

		      "<table width=100%%>"
		      "<tr>"
		      "<td width=25%% valign=top>"
		      "<center>"
		      "<img width=%"INT32"px height=%"INT32"px "
		      "src=%s>"
		      , SHADOWCOLOR
		      , SITTINGDX96
		      , SITTINGDY96
		      , SITTING
		      );
	sb.safePrintf(
		      "</center>"
		      "</td>"
		      
		      "<td valign=center>"
		      
		      "<center>"
		      "<font "
		      "style=\"color:white;font-size:14px;"
		      "text-shadow: 2px 4px 7px black;\">"
		      "<a onclick='addEngine();'>"
		      "Add Event Guru to your "
		      "<br>"
		      "Search Engine Providers"
		      "</a>"
		      "</font>"
		      "</center>"

		      "</td>"
		      //"<td width=25%%>"
		      //"</td>"
		      
		      "</tr>"
		      "</table>"
		      
		      "</div>\n"
		      );
	return true;
}




bool printWelcomePopup ( SafeBuf &sb , Msgfb *msgfb ) {
	//Msgfb *msgfb = &st->m_msgfb;
	// return if already logged in. no need to print it
	if ( msgfb && msgfb->m_fbId ) return true;
	sb.safePrintf(
		      "<div "
		      "id=welcome "
		      "class=grad3 "
		      "style=\""
		      "display:none;"
		      "position:fixed;"
		      "background-color:black;"
		      "z-index:999;"
		      "border: 10px solid black;"
		      
		      "top:60px;"
		      "right:60px;"
		      "left:60px;"
		      "height:320px;"

		      "\""
		      ">"
		      
		      "<br>"
		      
		      "<table width=100%%>"
		      "<tr>"
		      "<td width=25%% valign=center>"
		      "<center>"
		      "<img "
		      "style=padding:20px; "
		      "width=%"INT32"px height=%"INT32"px "
		      "src=%s>"
		      , SITTINGDXORIG
		      , SITTINGDYORIG
		      , SITTING
		      );
	sb.safePrintf(
		      "</center>"
		      "</td>"
		      
		      "<td>"
		      
		      "<center>"
		      "<b>"
		      "<font id=wmsg "
		      "style=\"color:white;font-size:18px;"
		      "text-shadow: 2px 4px 7px black;\">"
		      "You need to login to facebook to "
		      "use this feature."
		      "</font>"
		      "</b>"
		      "<br>"
		      "<br>"
		      "<br>"
		      
		      "<a id=login3 onclick=\""
		      );

	printLoginScript(sb);

	sb.safePrintf("\">"
		      "<img "
		      "align=center width=132 height=20 "
		      "src=/fblogin.png border=0>"
		      "</a>" 
		      
		      "<br>"
		      "<br>"
		      
		      "<table width=450px><tr><td>"
		      "<font style=\"color:white;font-size:16px;"
		      "line-height:24px;"
		      "text-shadow: 2px 4px 7px black;\">"
		      "Event Guru is the largest search engine of "
		      "events in the U.S. "
		      //"If you are looking for "
		      //"something TOO - DOO then think 
		      //"Event GOO - ROO. "
		      "Event Guru will NEVER spam you. Event Guru will "
		      "NEVER post on your wall. Our goal is to match people "
		      "with events they care about. Sharing your " // public "
		      "information on Facebook "
		      //"and areas of interests from Facebook "
		      "helps us do that."
		      "</font>"
		      "</td></tr></table>"

		      "<br>"
		      "<a onclick=\"document.getElementById"
		      "('welcome').style.display='none';\">"
		      "No thanks, just browsing."
		      "</a>"
		      
		      "</center>"
		      "</td>"
		      
		      "<td width=25%%>"
		      "</td>"
		      
		      "</tr>"
		      "</table>"
		      
		      "</div>\n"
		      );
	return true;
}

//#define RGB1 "30,87,153"
//#define RGB2 "125,185,232"
#define RGB1 "255,255,255"
#define RGB2 "100,100,100"

bool printGradHalo ( SafeBuf &sb ) {
	// . blue to transparent
	// . from http://www.colorzilla.com/gradient-editor/
	// . change "center" to "64 64"
	SafeBuf ttt;
	// i changed the ",1" to ",.3" that is the starting opacity
	// i changed the ,.5 to ,.1 , that is the stopping opacity
	ttt.safePrintf (".gradhalo{"

			// i had to change "centerxxx" to 64px 64px 45deg for
			// this to work...
			"background: -moz-radial-gradient(centerxxx 45deg, ellipse cover, rgba(%s,.5) 0%%, rgba(%s,0.1) 15%%, rgba(%s,0) 100%%);" /* FF3.6+ */
			"background: -webkit-gradient(radial, centerxxx centerxxx, 0px, centerxxx centerxxx, 100%%, color-stop(0%%,rgba(%s,.5)), color-stop(15%%,rgba(%s,0.1)), color-stop(100%%,rgba(%s,0)));" /* Chrome,Safari4+ */
			"background: -webkit-radial-gradient(centerxxx, ellipse cover, rgba(%s,.5) 0%%,rgba(%s,0.1) 15%%,rgba(%s,0) 100%%);" /* Chrome10+,Safari5.1+ */
			"background: -o-radial-gradient(centerxxx, ellipse cover, rgba(%s,.5) 0%%,rgba(%s,0.1) 15%%,rgba(%s,0) 100%%);" /* Opera 12+ */
			"background: -ms-radial-gradient(centerxxx, ellipse cover, rgba(%s,.5) 0%%,rgba(%s,0.1) 15%%,rgba(%s,0) 100%%);" /* IE10+ */
			"background: radial-gradient(centerxxx, ellipse cover, rgba(%s,.5) 0%%,rgba(%s,0.1) 15%%,rgba(%s,0) 100%%);" /* W3C */
			//filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='#1e5799', endColorstr='#007db9e8',GradientType=1 ); /* IE6-9 fallback on horizontal gradient */

			"}\n"
			, (char *)RGB1
			, (char *)RGB2
			, (char *)RGB2
			, (char *)RGB1
			, (char *)RGB2
			, (char *)RGB2

			, (char *)RGB1
			, (char *)RGB2
			, (char *)RGB2

			, (char *)RGB1
			, (char *)RGB2
			, (char *)RGB2

			, (char *)RGB1
			, (char *)RGB2
			, (char *)RGB2

			, (char *)RGB1
			, (char *)RGB2
			, (char *)RGB2
			);

	//"background: -webkit-radial-gradient(64 64, circle cover, rgba(30,87,153,0.3) 0%%,rgba(125,185,232,0) 50%%);" /* Chrome10+,Safari5.1+ */

	//"background: -webkit-radial-gradient(center, ellipse cover, rgba(125,185,232,0.3) 0%%,rgba(125,185,232,0) 100%%);" /* Chrome10+,Safari5.1+ */


	// swap out
	ttt.replace("centerxxx" ,"89px 64px" );
	ttt.replace("ellipse","circle ");
	ttt.replace("50%%"   ,"10%%" );

	// now print it to sb
	sb.safePrintf("%s",ttt.getBufStart());
	return true;
}


bool printHtmlHeader ( SafeBuf &sb , char *title , bool printPrimaryDiv ,
		       SearchInput *si , bool staticPage ) {


	bool igoogle = false;
	if ( si && si->m_igoogle ) igoogle = true;

	char *spacer = "";
	if ( title[0] ) spacer = " | ";

	sb.safePrintf ( //"<html>"
		       // without this msie goes into quirks mode and
		       // does not like the div position:fixed; so
		       // needLogin() is unable to popup the "you need to
		       // login to facebook" msg. also without this msie
		       // renders the thumbs up/down on the line below
		       // the event title.
		       // BUT with this we lose the radial halo gradient
		       // on chrome for some reason... but ff works, and
		       // msie does not work..
		       //"<!doctype html>" // i guess this is html 5.0
		       "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML "
		       "4.01 Transitional//EN\" \"http://www.w3.org/"
		       "TR/html4/loose.dtd\">"

		        //"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML "
		        //"1.0 Strict//EN\" \"http://www.w3.org/TR/"
		        //"xhtml1/DTD/xhtml1-strict.dtd\">"

		        //"<html xmlns=\"http://www.w3.org/1999/xhtml\" "
		        //"lang=\"en\" int16_ttag=\"yes\">"
		        "<html>"
		       );

	// HOTMAIL STRIPS THE HEAD
	if ( ! si || si->m_emailFormat == 0 )
		sb.safePrintf("<head>");

	sb.safePrintf (	"<meta http-equiv=\"Content-Type\" "
			"content=\"text/html; charset=utf-8\"/>\n"
			"<title>%s%s%s</title>"
		       , title
		       , spacer
		       , APPNAME
		       );

	sb.safePrintf ( 
			"<style type=\"text/css\">"
			// this gets rid of that annoying general margin
			// around the web page
		       " * { margin:0px; }\n"
			// this was disabling scroll, when combined with
			// removing the height::100%% from below for the
			// ipad chunking up problem
			//"html,body{height:100%%;overflow:hidden;font-size:18px;font-family:arial;}\n"
			"html{font-size:18px;font-family:arial;}\n"
			"a{cursor:hand;cursor:pointer;text-decoration:none;}\n"
			".hand{cursor:hand;cursor:pointer;"
			"text-decoration:none;}\n"
			"span.address {color:#404040;font-size:14px;}\n"
			"span.title {font-weight:bold;font-size:16px;}\n"
		       "span.prevnext {font-size:14px;color:black;}\n"
		       // for distance slider bar, do not select the text!
		       /*
		       ".noselect{"
		       "-webkit-user-select: none;"
		       "-khtml-user-select: none;"
		       "-moz-user-select: none;"
		       "-ms-user-select: none;"
		       "user-select: none;"
		       "}\n"
		       */
			//"hr.spacer30 {display:none;width:30px}\n"
			//"hr.spacer10 {display:none;width:10px}\n"
			// stupid ie needs this to turn off borders
		       "img{border:none}\n"
		       "span.summary {font-size:14px;}\n"
		       "span.countdown {font-size:14px;}\n"
		       "span.dates {font-size:14px;color:green;}\n"
			// it seems that the table "line-height" even though
			// implicitly "normal" does not inherit from html.
			// so use this line then:
			"table{line-height:22px;}\n"

			".grad1{"
			// msie 6-9
			"filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='%s', endColorstr='%s',GradientType=0 );"
			// gecko based browsers
			"background:-moz-linear-gradient(top, %s,%s);"
			// webkit based browsers
			"background:-webkit-gradient(linear, left top, left bottom, from(%s), to(%s));"
			// msie10+
			"background:-ms-linear-gradient(top, %s 0%%,%s 100%%);"
			"}\n"

			".grad2{"
			// msie 6-9
			"filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='%s', endColorstr='%s',GradientType=0 );"
			// gecko based browsers
			"background:-moz-linear-gradient(top, %s,%s);"
			// webkit based browsers
			"background:-webkit-gradient(linear, left top, left bottom, from(%s), to(%s));"
			// msie10+
			"background:-ms-linear-gradient(top, %s 0%%,%s 100%%);"
			"}\n"

			".grad3{"
			// msie 6-9
			"filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='%s', endColorstr='%s',GradientType=0 );"
			// gecko based browsers
			"background:-moz-linear-gradient(top, %s,%s);"
			// webkit based browsers
			"background:-webkit-gradient(linear, left top, left bottom, from(%s), to(%s));"
			// msie10+
			"background:-ms-linear-gradient(top, %s 0%%,%s 100%%);"
			"}\n"

			// black gradient for the black bars we use
			".grad4{"
			// msie 6-9
			"filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='%s', endColorstr='%s',GradientType=0 );"
			// gecko based browsers
			"background:-moz-linear-gradient(top, %s,%s);"
			// webkit based browsers
			"background:-webkit-gradient(linear, left top, left bottom, from(%s), to(%s));"
			// msie10+
			"background:-ms-linear-gradient(top, %s 0%%,%s 100%%);"
			"}\n"

			, GRAD1
			, GRAD2
			, GRAD1
			, GRAD2
			, GRAD1
			, GRAD2
			, GRAD1
			, GRAD2

			, GRAD2
			, GRAD1
			, GRAD2
			, GRAD1
			, GRAD2
			, GRAD1
			, GRAD2
			, GRAD1

			, GRAD3
			, GRAD4
			, GRAD3
			, GRAD4
			, GRAD3
			, GRAD4
			, GRAD3
			, GRAD4


			, GRAD5
			, GRAD6
			, GRAD5
			, GRAD6
			, GRAD5
			, GRAD6
			, GRAD5
			, GRAD6


			);



	//if ( ! igoogle ) printGradHalo ( sb );


	int32_t nc = sizeof(s_gradColors)/sizeof(Color);
	for ( int32_t i = 0 ; i < nc ; i++ ) {
		sb.safePrintf(
			// red gradient
			".grad%s{"
			// msie 6-9
			"filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='%s', endColorstr='%s',GradientType=0 );"
			// gecko based browsers
			"background:-moz-linear-gradient(top, %s,%s);"
			// webkit based browsers
			"background:-webkit-gradient(linear, left top, left bottom, from(%s), to(%s));"
			// msie10+
			"background:-ms-linear-gradient(top, %s 0%%,%s 100%%);"
			"}\n"
			, s_gradColors[i].m_color
			, s_gradColors[i].m_hex1
			, s_gradColors[i].m_hex2
			, s_gradColors[i].m_hex1
			, s_gradColors[i].m_hex2
			, s_gradColors[i].m_hex1
			, s_gradColors[i].m_hex2
			, s_gradColors[i].m_hex1
			, s_gradColors[i].m_hex2
			);
	}
			
	sb.safePrintf ( "</style>\n" );

	if ( ! igoogle )
		sb.safePrintf ( "</head>\n" );

	// HOTMAIL HATES BODY TAGS!!!
	if ( si && si->m_emailFormat == 0 ) {
		float radius = si->m_radius;
		if ( si->m_showPersonal ) radius = si->m_myRadius;
		sb.safePrintf (
			       //"<body onLoad='invite1();welcomeLoad();'>\n"
			       // disable inviter popup for now
			       //"<body onLoad='welcomeLoad();'>\n"
			       "<body onLoad=\"gpsCheck();"
			       // need to update the frogface in the distance 
			       // slider to make sure it is still accurate. 
			       // and if window gets too narrow this should 
			       // hide the entire slider so it doesn't cause 
			       // the x-scroller to appear.
			       //"window.onresize=function(){alert('hey');}"
			       "\" "
			       "onResize=\""
			       "var px=getArrowPos(%f);\n"
			       "setArrowPos(px);"
			       "\""
			       ">\n"
			       , radius
			       );
	}
	// HOTMAIL HATES BODY TAGS!!!
	//else if ( si && si->m_emailFormat == 0 )
	//	sb.safePrintf ( "<body>\n" );

	/*
	// the background div
	sb.safePrintf("<div "
		      "style=\""
		      "position:fixed;"
		      "top:0;"
		      "bottom:0;"
		      "right:0;"
		      "left:0;"
		      //"width:100%%;"
		      //"height:100%%;"
		      "background-image:url('/jungle.jpg');"
		      "background-repeat:no-repeat;"
		      "z-index:-2;"
		      //"background-position: 0  0px;"
		      "background-size:100%% 100%%;"
		      "\">"
		      "</div>"
		      );
	// the background div
	sb.safePrintf("<div "
		      "style=\""
		      "position:fixed;"
		      "top:0;"
		      "bottom:0;"
		      "right:0;"
		      "left:0;"
		      //"width:100%%;"
		      //"height:100%%;"
		      "background-color:black;"
		      "opacity:.50;"
		      "z-index:-1;"
		      //"background-position: 0  0px;"
		      "background-size:100%% 100%%;"
		      "\">"
		      "</div>"
		      );
	*/

	if ( ! printPrimaryDiv ) return true;

	if ( igoogle ) return true;

	// skip all for now
	//return true;

	//char *gwidth  = "width:196px;";
	//if ( staticPage ) gwidth = "width:100%%;";
	char *gwidth = "width:100%;";

	// the black and white gradient div for the query boxes at the
	// top of the screen. this shines through the transparent tables.
	/*
	sb.safePrintf ( "<div class=grad2 "
			"style=\""
			"position:fixed;"//absolute;"
			"overflow:hidden;"
			"z-index:-2;"
			// msie think 100% width is width of the
			// container, the td, but chrome thinks
			// its of the whole page!
			//"width:100%%;"
			//"width:196px;"
			"%s"
			//"width:3000px;"
			"top:0px;"
			"left:0px;"
			//"margin-left:-30px;"
			//"margin-right:200px;"
			//"height:190px;"
			//"height:160px;"
			"height:100%%;"
			"\">"
			"</div>"
			, gwidth
			);
	*/

	// the textured bg image from twitter
	sb.safePrintf ( "<div "
			"style=\""
			"position:fixed;"//absolute;"
			"overflow:hidden;"
			"z-index:-3;"
			// msie think 100% width is width of the
			// container, the td, but chrome thinks
			// its of the whole page!
			//"width:100%%;"
			//"width:196px;"
			"%s"
			//"width:3000px;"
			"top:0px;"
			"left:0px;"
			//"http://www1.sk-static.com/images/bundles/nw/furniture/body-background.3901085.png"
			"background-image:url('/bg.png');"
			"background-repeat:repeat;"
			"opacity:1.00;"
			//"background-attachment:fixed;"
			//"margin-left:-30px;"
			//"margin-right:200px;"
			//"height:190px;"
			//"height:160px;"
			"height:100%%;"
			"\">"
			"</div>"
			, gwidth
			);
	



	/*
	  horizontal racing stripe...
	sb.safePrintf ( "<div "
			"style=\""
			"position:absolute;"
			"overflow:hidden;"
			"z-index:-2;"
			// msie think 100% width is width of the
			// container, the td, but chrome thinks
			// its of the whole page!
			//"width:100%%;"
			//"width:196px;"
			"%s"
			//"width:3000px;"
			"top:270px;"
			"left:0px;"
			"background-color:black;"
			//"margin-left:-30px;"
			//"margin-right:200px;"
			//"height:190px;"
			//"height:160px;"
			"height:2px;"
			"\">"
			"</div>"
			, gwidth
			);
	*/

	/*
	// the radial gradient in the upper left corner
	if ( si && si->m_emailFormat == 0 )
		// . tania's email program sets the height:100%% to
		//   height:11190px; for some reason pushing her email down.
		//   i guess it doesn't understand position:fixed!
		//   so take this out for emailing
		// . the halo div radial gradient around the event guru logo
		sb.safePrintf ( "<div class=gradhalo "
				"style=\""
				"position:fixed;"//absolute;"
				"overflow:hidden;"
				"z-index:-1;"
				//"width:100%%;"
				//"width:196px;"
				"%s"
				"top:0px;"
				"left:0px;"
				//"margin-left:-30px;"
				//"margin-right:200px;"
				//"height:189px;"
				//"height:160px;"
				"height:100%%;"
				"\">"
				"</div>"
				, gwidth
				);
	*/

	sb.safePrintf ( "<div style=\""
			"font-size:18px;"
			"font-family:arial;"
			//"overflow:hidden;"
			//"position:absolute;"
			//"background-color:white;"//lightgray;"
			// make it transparent so the gradhalo shows through
			// because that has a z-index of -1
			"background-color:transparent;"
			"position:relative;"
			"overflow:auto;"
			//"top:0;"
			//"left:601px;"
			//"right:0;"
			//"left:0;"
			// height:100% causes the ipad to chunk-up !!!!
			// so in removing this we lose the position:fixed
			// logic for msie... :(
			//"height:100%%;"
			//"bottom:0;"
			//"width:900px;"
			//"width:60%%;"
			//"height:20%%;"
			//"height:300px;"
			"\">\n"
			);
	return true;
};



bool printHtmlTail ( SafeBuf *sb , Msgfb *msgfb ,
		     bool printUnsubscribedPopup = false ) {
	sb->safePrintf("<br>"
		       //"<br>"
		       "<center>"
		       "<font size=-1 color=gray>"

		       "<a style=\"color:gray;text-decoration:none;\" "
		       "href=/api.html>"
		       "Developer API</a> | "

		       "<a style=\"color:gray;text-decoration:none;\" "
		       "href=/privacy.html>"
		       "Privacy Policy</a> | "
		       "<a style=\"color:gray;text-decoration:none;\" "
		       "href=/terms.html>"
		       "Terms of Use</a> | "
		       "Copyright &copy; 2012. "
		       "All Rights Reserved."
		       "</font>"
		       "</center>"
		       "<br>"
		       );
	// end the primary div
	sb->safePrintf ( "</div>"
			 // part of the fix for msie position:fixed workaround
			 //"</div>" // class=wrapper>\n"
			 );

	// . we now have to put the popup here!!
	// . part of the msie position:fixed hack
	printWelcomePopup ( *sb , msgfb );

	// if cookies not enabled
	sb->safePrintf( "<div "
			"id=nocookies "
			"class=grad3 "
			"style=\""
			"display:none;"
			"position:fixed;"
			"background-color:black;"
			"z-index:999;"
			"border: 10px solid black;"
			"top:60px;"
			"right:60px;"
			"left:60px;"
			"height:220px;"
			"\""
			">"
			"<br>"
			"<table width=100%%>"
			"<tr>"
			"<td width=25%% valign=center>"
			"<center>"
			"<img width=%"INT32"px height=%"INT32"px "
			"src=%s>"
			, SITTINGDX128
			, SITTINGDY128
			, SITTING
			);
	sb->safePrintf(
			"</center>"
			"</td>"
			"<td>"
			"<center>"
			"<b>"
			"<font "
			"style=\"color:white;font-size:18px;"
			"text-shadow: 2px 4px 7px black;\">"
			"You need to enable cookies to use this site."
			"<br>"
			"<br>"

			"<a "
			"onclick=\"document.getElementById"
			"('nocookies').style.display='none';\">"
			"<u>Bummer</u>"
			"&nbsp;&nbsp;&nbsp;"
			"<u>Major Bummer</u>"
			"</a>"

			"</b>"
			"</font>"
			"<br>"
			"</td>"
			"<td width=25%%>"
			"</td>"
			"</tr>"
			"</table>"
			"</div>\n"
		      );


	if ( printUnsubscribedPopup )
		printUnsubscribedPopup2 ( *sb , msgfb );

	// finish it up
	sb->safePrintf ( "</body>"
			 "</html>"
			 );
	return true;
}

bool printScriptsCommon ( SafeBuf &sb , State7 *st ) {

	Msgfb *msgfb = &st->m_msgfb;
	SearchInput *si = &st->m_si;

	if ( si->m_forForm ) {
		// anytime the admin clicks on a bullseye for an event, then
		// after you save your hidden input values, then call this 
		// to reload the page.
		sb.safePrintf(
			      //"<script>"
			      "function reloadPage( formdocid,formeventid,"
			      "formclockset){\n"
			      
			      // get from hidden tag
			      "var formurl = "
			      "document.getElementById('formurl').value;\n"
			      
			      //"var d = document;\n"
			      
			      // and from left iframe
			      "var q = document."
			      "getElementById('q').value;\n"

			      "var where = document."
			      "getElementById('wherebox').value;\n"

			      "var radius = document."
			      "getElementById('radius').value;\n"

			      //"var docid = document."
			      //"getElementById('formdocid').value;\n"

			      //"var evid = document."
			      //"getElementById('formeventid').value;\n"
			      
			      // and mapping from right iframe
			      "var etitle = '';\n"
			      
			      // update current url
			      "var url = '/?form=1';\n"
			      "url = url + '&save=1';\n"		       
			      "url = url + '&formurl=' + formurl;\n"
			      "url = url + '&formdocid=' + formdocid;\n"
			      "url = url + '&formeventid=' + formeventid;\n"
			      "url = url + '&formclockset=' + formclockset;\n"
			      "url = url + '&q=' + encodeURIComponent(q);\n"
			      "url = url + '&where=' + "
			      "encodeURIComponent(where);\n"

			      "url = url + '&radius=' + radius;\n"
			      // . reload the page
			      // . it should save this url in tagdb for the 
			      //   form url. this url contains all the info
			      //   we should need.
			      //   it should save that onLoad().
			      "top.location.href = url;\n"
			      
			      "}\n"
			      
			      //"</script>"
			      );
	}


	/*
	sb.safePrintf ( "function welcomeLoad() {\n" );
	if ( msgfb && msgfb->m_fbId ) sb.safePrintf("return;\n");
	else sb.safePrintf (
			    // fix height of iframe div
			    //"if (navigator.appName=='Microsoft "
			    //"Internet Explorer') {\n"
			    //"var i = document.getElementById('sly');\n"
			    // if you change the middle black bar height or
			    // the height of the SUMMARY DIV then you gotta
			    // change the 300 here!
			    //"i.style.height=document.body.clientHeight-300;\n"
			    //"}\n"
			    //"var isiPad = navigator.userAgent."
			    //"match(/iPad/i) != null;\n"
			    //"if ( isiPad ) {\n"
			    //"alert('ipad '+window.innerHeight);\n"
			    //"var i = document.getElementById('sly');\n"
			    ////"i.style.height=window.innerHeight-300;\n"
			    ////"i.style.height=100;\n"
			    "}\n"

			    // if already logged in, skip this
			    //"var f = getCookie('fbid');\n"
			    //"if ( ! f || f == 0 ) return;\n"
			    //"document.cookie = 'welcomed=1; max-age=10';\n"
			    //"document.cookie = 'welcomed=0';\n"
			    "var w = getCookie('welcomed');\n"
			    // debug
			    //"document.cookie = 'welcomed=0;';\n"
			    "if ( w && w == 1 ) return\n"
			    "var e = document.getElementById('welcome');\n"
			    // set msg to this one
			    "var m = document.getElementById('wmsg');\n"
			    "m.innerHTML='Please login to Facebook for the "
			    "maximum Event Guru experience.';\n"
			    // make that div visible
			    "e.style.display = '';\n"
			    // set expiration to 5 days so they get prompted
			    // to login to facebook every 5 days
			    "document.cookie = 'welcomed=1; max-age=432000';\n"
			    );
	sb.safePrintf("}\n");
	*/

	// then the need to login script
	sb.safePrintf ( "function needLogin() {\n" );
	if ( msgfb && msgfb->m_fbId ) sb.safePrintf("return;\n");
	else sb.safePrintf (
			    "var e = document.getElementById('welcome');\n"
			    // set msg to this one
			    "var m = document.getElementById('wmsg');\n"
			    "m.innerHTML='You need to login to Facebook "
			    "to use this feature.';\n"
			    // popup must be absolute for msie otherwise
			    // it will not render.
			    /*
			    "if (navigator.appName=='Microsoft "
			    "Internet Explorer') {\n"
			    "e.style.position='absolute';\n"
			    "window.scrollTo(0,0);\n"
			    "}\n"
			    */
			    // make that div visible
			    "e.style.display = '';\n"
			    );
	sb.safePrintf("}\n");

	sb.safePrintf ( "function getCookie(name) {\n"
			//Without this, it will return the first value 
			//in document.cookie when name is the empty string.
			"if (name == '')  return ('');\n"
			"name_index = document.cookie.indexOf(name + '=');\n"
			"if(name_index == -1) {return('');}\n"
			"cookie_value =  document.cookie.substr(name_index +"
			"name.length + 1, document.cookie.length);\n"
			// All cookie name-value pairs end with a semi-colon, 
			// except the last one.
			"end_of_cookie = cookie_value.indexOf(';');\n"
			"if(end_of_cookie != -1) {"
			"cookie_value = cookie_value.substr(0, "
			"end_of_cookie);}\n"
			//Restores all the blank spaces.
			"space = cookie_value.indexOf('+');\n"
			"while(space != -1) { \n"
			"cookie_value = cookie_value.substr(0, space) + ' ' + "
			"cookie_value.substr(space + 1, cookie_value."
			"length);\n"
			"space = cookie_value.indexOf('+');\n"
			"}\n"
			"return(cookie_value);\n"
			"}\n"
			);

	return true;
}


// scripts for voting buttons on cached page
bool printScripts2 ( SafeBuf &sb , State7 *st ) {

	// for facebook inviter
	sb.safePrintf ( "<div id=\"fb-root\"></div>" );

	sb.safePrintf (
		       "<script type=\"text/javascript\">\n"
		       
		       "function tagEvent ( imgid, ev , st , d , evid ) {\n"
		       "var im = document.getElementById(imgid);\n"
		    //"alert('elmsrc='+imgid+' '+ev+' '+st+' '+d+' '+evid);\n"
		       "var url = "
		       // used to see what to change it too
		       "'/?"
		       // used to change the img src
			"&iconid='+imgid+"
		       // the docid so we know what to tag
		       "'&d='+d+"
		       // the eventid
		       "'&evid='+evid+"
		       // starttime=time_t
		       "'&starttime='+st+"			
		       // ev=eventHash64
		       "'&evh64='+ev+"
		       "'&iconsrc='+encodeURIComponent(im.src);\n"
		       "var client = new XMLHttpRequest();\n"
		       "client.onreadystatechange = handler2;\n"
		       "client.open(\"GET\", url );\n"
		       "client.send();\n"
		       "}\n"
		       );

	// a DIFFERENT handler2() function
	sb.safePrintf ( "function handler2() {\n"
			"if(this.readyState != 4 ) return;\n"
			//"alert(this.responseText);\n"
			//"eval(this.responseText);\n"
			"reloadFrame2();\n"
			"}\n"
			);

	// a DIFFERENT reloadFrame function
	sb.safePrintf ( "function reloadFrame2 () {\n"
			// in this one url generation is different
			"var url = window.location.href;\n"
			"window.location.href = url;\n"
			"}\n"
			);

	printScriptsCommon ( sb , st );

	sb.safePrintf ( "</script>\n" );

	// for facebook inviter
	sb.safePrintf ( "<script src=\"http://connect.facebook."
			"net/en_US/all.js\"></script>"
			"<script>"
			"FB.init({\n"
			"appId  : '%s',\n"
			"});\n"
			"function "
			"sendRequestViaMultiFriendSelector( msg ){\n"
			"FB.ui({method: 'apprequests',\n"
			"message: msg\n"
			"}, requestCallback);\n"
			"}\n"
			"function requestCallback(response) {\n"
			"}\n"
			"</script>\n"
			, APPID
			);

	//printWelcomePopup ( sb , &st->m_msgfb );

	return true;
}

#define INVITE_MSG "Hi my friend! You should check out Event Guru. It shows you events tailored to your interests."


// main scripts
bool printScripts1 ( SafeBuf &sb , State7 *st ) {

	//HttpRequest *hr = &st->m_hr;
	SearchInput *si = &st->m_si;
	//Msg40 *msg40 = &(st->m_msg40);
	//Msgfb *msgfb = &st->m_msgfb;

	int32_t fs = 20;
	if ( si->m_igoogle ) fs = 13;

	sb.safePrintf (
		       "<script type=\"text/javascript\">\n"

		       "function tagEvent ( imgid, ev , st , d , evid ) {\n"
		       "var im = document.getElementById(imgid);\n"
		       //"alert('elmsrc='+imgid+' '+ev+' '+st+' '+d+' '+evid);"
		       //"var url = "
		       "var appendage = "
		       // used to see what to change it too
		       //"'/?"
		       // used to change the img src
			"'iconid='+imgid+"
		       // the docid so we know what to tag
		       "'&d='+d+"
		       // the eventid
		       "'&evid='+evid+"
		       // preserve result page
		       "'&s=%"INT32""
		       // starttime=time_t
		       "&starttime='+st+"			
		       // ev=eventHash64
		       "'&evh64='+ev+"
		       "'&iconsrc='+encodeURIComponent(im.src);\n"
		       //"var client = new XMLHttpRequest();\n"
		       //"client.onreadystatechange = handler2;\n"
		       //"client.open(\"GET\", url );\n"
		       //"client.send();\n"
		       "reloadFrame(false,appendage);\n"
		       "}\n"
		       //,host
		       , si->m_firstResultNum
		       );
	
	//sb.safePrintf ( "function handler2() {\n"
	//		"if(this.readyState != 4 ) return;\n"
	//		//"alert(this.responseText);\n"
	//		//"eval(this.responseText);\n"
	//		"reloadFrame(false,'s=%"INT32"');\n"
	//		"}\n"
	//		, msg40->getFirstResultNum()
	//		);

	// popup if they have not been "welcomed" yet
	printScriptsCommon ( sb , st );

	// . popup if they have not been "welcomed" yet
	// . make this a delay so it works?
	sb.safePrintf ( "function invite1() {\n"
			//"var w = getCookie('welcomed');\n"
			// if not yet welcomed, wait for that
			//"if ( ! w || w == 0 ) return;\n"
			// need a facebook id ... i.e. be logged in
			"var f = getCookie('fbid');\n"
			"if ( ! f || f == 0 ) return;\n"
			// did we already invite before?
			"var v = getCookie('showedinvited');\n"
			"if ( v && v == 1 ) return;\n"
			// wait
			"setTimeout('invite2()',2000);\n"
			"}\n"
			);

	sb.safePrintf ( "function invite2() {\n"
			/*
			// add the script now
			"var head =document.getElementsByTagName('head')[0];\n"
			"var js = document.createElement('script');\n"
			"js.type = 'text/javascript';\n" // new one
			"js.src = \"//connect.facebook.net/"
			"en_US/all.js\";\n" // #xfbml=1&appId=%s\";\n"
			"head.appendChild(js);\n"
			*/
			
			// use the invite div
			"var e = document.getElementById('invite');\n"
			// make that visible now
			"e.style.display = '';\n"
			// . only do this once!
			// . include '2' so does not match 'iaminvited'
			// . prompt once every 30 days
			"document.cookie = 'showedinvited=1; max-age=2592000';\n"

			// open up the dialog now in this div
			"sendRequestViaMultiFriendSelector();"

			"}\n"
			//, APPID 
			);


	/*
	// ipad scrolling
	sb.safePrintf ( "setTimeout(function () {\n"
			"var startY = 0;\n"
			"var startX = 0;\n"
			"var b = document.body;\n"
			"b.addEventListener('touchstart', function (event) {\n"
			"parent.window.scrollTo(0, 1);\n"
			"startY = event.targetTouches[0].pageY;\n"
			"startX = event.targetTouches[0].pageX;\n"
			"});\n"
			"b.addEventListener('touchmove', function (event) {\n"
			"event.preventDefault();\n"
			"var posy = event.targetTouches[0].pageY;\n"
			"var h = parent.document.getElementById('scroller');\n"
			"var sty = h.scrollTop;\n"
			"var posx = event.targetTouches[0].pageX;\n"
			"var stx = h.scrollLeft;\n"
			"h.scrollTop = sty - (posy - startY);\n"
			"h.scrollLeft = stx - (posx - startX);\n"
			"startY = posy;\n"
			"startX = posx;\n"
			"});\n"
			"}, 1000);\n"
			);
	*/

	sb.safePrintf ( "function toggleHide( id ) {\n"
			//"var opt = document.all ? document.all[id] :\n"
			//"document.getElementById ?\n"
			//"document.getElementById(id) : null;\n"
			"var opt = document.getElementById('ID_'+id);\n"
			"if ( opt == null ) return;\n"
			"if(opt.style.display == 'none') {\n"
			"opt.style.display = '';\n"
			// turn hidden parm off
			"document.forms[0].elements[id].value = 1;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=1\";\n"
			//"alert(document.cookie);\n"
			"saveCookies();\n"
			"} else {\n"
			"opt.style.display = 'none';\n"
			// turn hidden parm on
			"document.forms[0].elements[id].value = 0;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=0\";\n"
			"saveCookies();\n"
			//"document.cookie = \"boo=hoo; path=/\";\n"
			//"alert(document.cookie);\n"
			"}\n"
			"}\n"

			// like togglehide, but turns off!
			"function toggleOff( id ) {\n"
			"var opt = document.getElementById('ID_'+id);\n"
			"if ( opt == null ) return;\n"
			//"opt.style.display = 'none';\n"
			"document.forms[0].elements[id].value = 0;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=0\";\n"
			"saveCookies();\n"
			"}\n"

			"function toggleOff2( id ) {\n"
			"var opt = document.getElementById('ID_'+id);\n"
			"if ( opt == null ) return;\n"
			"opt.style.display = 'none';\n"
			//"document.forms[0].elements[id].value = 0;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=0\";\n"
			//"saveCookies();\n"
			"}\n"

			/*
			"function saveHist ( ) {\n"
			"var i;\n"
			"var url = \"/?\";\n"
			"for(i=0; i<document.myform.elements.length; i++){\n"
			// skip submit button and nameless checkboxes
			"if ( document.myform.elements[i].name == '' ) {\n"
			"continue;\n"
			"}\n"
			"url = "
			"url + "
			"document.myform.elements[i].name + \"=\" + "
			"document.myform.elements[i].value + \"&\" ;\n"
			"}\n"
			//"alert(url);\n"
			"history.current = url;\n"
			"}\n"
			*/

			/*
			"function getCookie( name ) {\n"
			"var start = document.cookie.indexOf( name + '=' );\n"
			"var len = start + name.length + 1;\n"
			"if ( ( !start ) &&\n"
			"(name!=document.cookie.substring( 0,name.length))){\n"
			"return null;\n"
			"}\n"
			"if ( start == -1 ) return null;\n"
			"var end = document.cookie.indexOf( ';', len );\n"
			"if ( end == -1 ) end = document.cookie.length;\n"
			"return unescape( document.cookie.substring( len, end ) );\n"
			"}\n"
			*/

			"function saveCookies () {\n"
			"var i;\n"
			//"alert('current cookie = '+document.cookie);\n"
			"var l1 = getCookie('loc1');\n"
			"var l2 = getCookie('loc2');\n"
			"var l3 = getCookie('loc3');\n"
			"var l4 = getCookie('loc4');\n"
			"var q1 = getCookie('qry1');\n"
			"var q2 = getCookie('qry2');\n"
			"var q3 = getCookie('qry3');\n"
			"var q4 = getCookie('qry4');\n"
			// init the meta cookie, "mc"
			"var mc = '&';\n"
			"for(i=0; i<document.myform.elements.length; i++){\n"
			// form input tag "name=" is fn
			"var fn = document.myform.elements[i].name;\n"
			// skip submit button and nameless checkboxes
			"if ( ! fn ) continue;\n"
			"if ( fn == '' ) continue;\n"
			"var fv = document.myform.elements[i].value;\n"

			// facebook interests are stored in hidden form
			// elements so the checkbox thing works on them,
			// however, do not ever store them in the cookie.
			// they are private! and if a user logs out i don't
			// want them hanging around. plus they take up a lot
			// of valuable cookie space. all cookies have to
			// fit in 4KB for MSIE and you can only have up to
			// 50 of them
			"if ( "
			"fn.charAt(0)=='i' && "
			"fn.charAt(1)=='x' && "
			"fn.charAt(2)=='t' && "
			"fv.charAt(0)>='3' ) {"
			//"alert('fv='+fv);"
			"continue;}\n"

			// ok, msie cookie limit of 50 is being hit, so
			// now just put all the cookies into one cookie.
			// do NOT put the widget header in here, nor the
			// l1-4 nor q1-q4 cookies. we still are limited to
			// how big a cookie can be as well. the l1-4 and q1-4
			// have no corresponding form tags so we don't
			// have to worry about them in this loop.
			// make it > 4 to include widget's "width=400" etc.
			// doing if ( fb.length > 4 ) doesn't cut it because
			// a cookies length can change from 1 to 10 or whatever
			// and appear by itself as well as the meta cookie
			// thereby confusing the browser. so let's just
			// avoid specific elements here
			"if ( "
			"fn == 'widgetheader' || "
			"fn == 'where' || "
			//"fn == 'mylocation' || "
			"fn == 'q' || "
			"fn == 'newinterest' "
			" ) {;\n"
			// this piece of crap converts utf8 strings to iso
			// latin 1 then encodes that!!!
			//"document.cookie = fn +'=' + escape(fv);\n"
			// this keeps it as utf8!!
			"document.cookie = fn +'=' + encodeURIComponent(fv);\n"
			// note that
			//"if ( fn == 'mylocation' ) "
			//"alert(fn +'=' + encodeURIComponent(fv));\n"
			// calling escape(fv) messes up utf8 strings..
			// because we do not call decode on the cookie?
			//"document.cookie = fn +'=' + fv;\n"
			"continue;\n"
			"}\n"
			// compile into a single cookie string
			//"mc = mc+fn+'='+escape(fv)+'&';\n"
			"mc = mc+fn+'='+encodeURIComponent(fv)+'&';\n"
			"}\n"
			// now set the meta cookie
			// the meta cookie's elements are separated with &'s
			"document.cookie = 'metacookie='+mc;\n"
			// set query history
			"var cq = document.getElementById('q').value;\n"
			// encode it for comparing
			"var ecq = encodeURIComponent(cq);\n"
			"if ( ecq != q1 && ecq != q2 && ecq != q3 && "
			"ecq != q4 && "
			"cq != 'anything' && "
			// fix 'Enter search terms here'
			"cq.substring(0,9) != 'Enter sea'){\n"
			"document.cookie = 'qry4='+q3;\n"
			"document.cookie = 'qry3='+q2;\n"
			"document.cookie = 'qry2='+q1;\n"
			"document.cookie = 'qry1='+ecq;\n"
			"}\n"
			// MSIE needs to set these otherwise we don't get them
			// because MSIE unline the others does not propagate
			// old cookie values it seems
			"else {\n"
			"document.cookie = 'qry4='+q4;\n"
			"document.cookie = 'qry3='+q3;\n"
			"document.cookie = 'qry2='+q2;\n"
			"document.cookie = 'qry1='+q1;\n"
			"}\n"
			// repeat for location cookies
			"var cl = document.getElementById('wherebox').value;\n"
			// encore it for comparing
			"var ecl = encodeURIComponent(cl);\n"
			"if ( ecl != l1 && ecl != l2 && ecl != l3 && "
			"ecl != l4 &&"
			// fix 'Enter city, state....' default
			" cl != 'anywhere' && "
			" cl != 'mylocation' && "
			"cl.substring(0,9) != 'Enter cit'){\n"
			"document.cookie = 'loc4='+l3;\n"
			"document.cookie = 'loc3='+l2;\n"
			"document.cookie = 'loc2='+l1;\n"
			"document.cookie = 'loc1='+ecl;\n"
			"}\n"
			// MSIE needs to set these otherwise we don't get them
			// because MSIE unline the others does not propagate
			// old cookie values it seems
			"else {\n"
			"document.cookie = 'loc4='+l4;\n"
			"document.cookie = 'loc3='+l3;\n"
			"document.cookie = 'loc2='+l2;\n"
			"document.cookie = 'loc1='+l1;\n"
			"}\n"
			//"alert('cookie='+document.cookie);\n"
			"}\n"
			


			// reload the search results td. make the submit url.
			// this function should replace calls to submit().
			/*
			"function getUrl (resetStart,append) {\n"
			"var i;\n"
			"if ( resetStart ) {\n"
			"document.getElementById('s').value = 0;\n"
			"}\n"
			//"var url = \"/?frame=1&\";\n"
			"var url = \"/?\";\n"
			"if ( append && append != '' ) {\n"
			"url = url + append +\"&\";\n"
			"}\n"
			"for(i=0; i<document.myform.elements.length; i++){\n"
			// int16_tcut
			"var fn = document.myform.elements[i].name;\n"
			// skip submit button and nameless checkboxes
			"if ( ! fn ) continue;\n"
			"if ( fn == '' ) continue;\n"
			"var fv = document.myform.elements[i].value;\n"
			"url = url + fn + \"=\" + fv + \"&\" ;\n"
			"}\n"
			//"alert(url);\n"
			"return url;\n"
			//"window.location.href = url;\n"
			//"var client = new XMLHttpRequest();\n"
			//"client.onreadystatechange = handler;\n"
			//"client.open(\"GET\", url );\n"
			//"client.send();\n"
			//"document.getElementById('searchresults').innerHTML='<br><b>Loading...</b>';\n"
			"}\n"
			*/
			);


	char *propagate = "";
	if ( si->m_igoogle && si->m_widget ) 
		propagate = "ig=1&widget=1&";
	if ( si->m_interactive && si->m_widget ) 
		propagate = "interactive=1&widget=1&";

	SafeBuf ttt;
	if ( si->m_forForm ) {
		ttt.safePrintf("forform=1&formurl=");
		ttt.urlEncode(si->m_formUrl);
		ttt.safePrintf("&");
		propagate = ttt.getBufStart();
	}

	if ( propagate[0] )
		sb.safePrintf ( 
			       "function reloadResults (resetStart,append) {\n"
			       //"var url=getUrl(resetStart,append);\n"
			       "saveCookies();\n"
			       "if ( ! append ) append = '';\n"
			       // so the back button works throw in a count
			       // append starts with a '&'
			       "var count = new Date().getTime();\n"
			       //"var count=1;\n"
			       "var url = '/?%swidgetheight=%"INT32"&"
			       "widgetwidth=%"INT32"&page=' + count + append;\n"
			       //"alert('f1'+url);\n"
			       "window.location.href = url;\n"
			       "}\n"
			       , propagate
			       , si->m_widgetHeight
			       , si->m_widgetWidth
			       );
	else
		sb.safePrintf ( 
			       "function reloadResults (resetStart,append) {\n"
			       //"var url=getUrl(resetStart,append);\n"
			       "saveCookies();\n"
			       "if ( ! append ) append = '';\n"
			       // so the back button works throw in a count
			       // append starts with a '&'
			       "var count = new Date().getTime();\n"
			       //"var count=1;\n"
			       "var url = '/?page=' + count + append;\n"
			       //"alert('f2'+url);\n"
			       "window.location.href = url;\n"
			       "}\n"
			       );


	sb.safePrintf ( 
			"function reloadFrame ( resetStart , append ) {\n"
			//"var url=getUrl(resetStart,append);\n"
			//"url=url+'&frame=1';\n"
			"if ( ! append ) append = '';\n"
			"var url = '/?frame=1&'+append;\n"
			"saveCookies();\n"
			//"window.location.href = url;\n"
			"var client = new XMLHttpRequest();\n"
			"client.onreadystatechange = handler;\n"
			"client.open(\"GET\", url );\n"
			"client.send();\n"
			//"document.getElementById('searchresults').innerHTML='<br><b>Loading...</b>';\n"
			"}\n"

			// when reloadResults() completes it calls this
			"function handler() {\n"
			"if(this.readyState == 4 ) {\n"
			//"alert(\"handler \"+this.responseText );\n"
			//"alert(\"handler\"+this.responseXml);\n"
			//"if(this.status != 200 ) return;\n"
			//"if(this.responseXML == null ) return;\n"
			"document.getElementById('searchresults').innerHTML="
			"this.responseText;\n"
			//"alert(this.responseText);\n"
			// get new map url from search results we loaded.
			// use a hidden div tag.
			"var mapurl = document.getElementById('mapurl').getAttribute('url');\n"
			"document.getElementById('mapimg').src=mapurl;\n"
			"}\n"
			"}\n"

			
			"function getFormParms ( ) {\n"
			"var i;\n"
			"var url = '';\n"
			"for(i=0; i<document.myform.elements.length; i++){\n"
			"var elm = document.myform.elements[i];\n"
			// skip submit button and nameless checkboxes
			"if ( elm.name == '' ) {\n"
			//"alert(document.myform.elements[i].value)\n"
			"continue;\n"
			"}\n"
			// until we had def=%"INT32" to each input parm assume
			// default is 0. i guess if it has no def= attribute
			// assume default is 0
			"if ( elm.value == '0' ) {\n"
			"continue;\n"
			"}\n"
			"if ( elm.value == '' ) {\n"
			"continue;\n"
			"}\n"
			"url = "
			"url + "
			"elm.name + \"=\" + "
			"elm.value + \"&\" ;\n"
			"}\n"
			"return url;\n"
			"}\n"


			"function toggleMe( name , num ) {\n"
			"for (var i = 0; i < num; i++) {\n" 
			"var nombre;\n"
			"nombre = name + i;\n"
			"var e = document.getElementById(nombre);\n"
			"if ( e == null ) continue;\n"
			"if(e.style.display == 'none') {\n"
			"e.style.display = '';\n"
			"} else {\n"
			"e.style.display = 'none';\n"
			"}\n"
			"}\n"
			"}\n"


			"function swh ( place ) {\n"
			"window.scrollTo(0,0);\n"
			//"var e = document.getElementById('where');\n"
			//"e.value = place;\n"
			"var count = new Date().getTime();\n"
			"var url = '/?page=' + count + '&where='"
			"+encodeURIComponent(place);\n"
			//"alert(url);\n"
			"window.location.href = url;\n"
			"}\n"

			/*
			"function setBorders ( nombre, yoyo ) {\n"
			"if ( nombre == 'where' || nombre == 'q' ) {\n"
			"var e = document.getElementById(nombre);\n"
			"e.style.color='black';\n"
			"e.style.fontWeight='bold';\n"
			"e.style.fontSize='20px';\n"
			"if ( yoyo == 'off' )"
			"e.style.borderColor='#ffffff';\n"
			"if ( nombre == 'where' && yoyo == 'on' )"
			"e.style.borderColor='#50ff50';\n"
			"if ( nombre == 'q' && yoyo == 'on' )"
			"e.style.borderColor='#ff7070';\n"
			"}\n"
			"}\n"
			*/
			);

	sb.safePrintf (
			"function setVal ( nombre , yoyo ) {\n"
			"var e = document.getElementById(nombre);\n"
			//"if ( ! e ) { alert(nombre+\"is null\");return;}\n"
			"if ( ! e ) return;\n"
			"e.value = yoyo;\n"

			//"if ( yoyo != '' && yoyo !='anywhere') "
			//"setBorders ( nombre, 'on' );\n"
			//"else setBorders ( nombre, 'off' );\n"
			/*
			"if ( nombre == 'where' || nombre == 'q' ) {\n"
			"var e = document.getElementById(nombre);\n"
			"e.style.color='black';\n"
			"e.style.fontWeight='bold';\n"
			"e.style.fontSize='%"INT32"px';\n"
			"}\n"
			, fs );

	sb.safePrintf ( 
			*/

			//"alert(nombre+\"=\"+e.value);\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = nombre+\"=\"+yoyo;\n"
			"saveCookies();\n"
			"}\n"


			// simulate radio buttons with checkboxes
			"function toggleBoolRadio ( control , id ) {\n"
			// get the class
			"var cs = control.getAttribute('class');\n"
			// clear all checkboxes except control otherwise
			// as int32_t as in this class
			"var i;\n"
			"for(i=0; i<document.myform.elements.length; i++){\n"
			"var elm = document.myform.elements[i];\n"
			"if ( elm.getAttribute('class') != cs ) continue;\n"
			"elm.checked = false;\n"
			"elm.value = 0;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=0\";\n"
			"saveCookies();\n"
			"}\n"
			// return if trying to uncheck a checkbox
			"control.checked = true;\n"
			"document.forms[0].elements[id].value = 1;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=1\";\n"
			"saveCookies();\n"
			"}\n"

			// . simulate radio buttons with checkboxes
			// . !!!! these can be unchecked as well !!!!!
			/*
			"function toggleBoolRadio2 ( control , id ) {\n"
			// get the class
			"var cs = control.getAttribute('class');\n"
			"var old = control.checked;\n"
			// clear all checkboxes except control otherwise
			// as int32_t as in this class
			"var i;\n"
			"for(i=0; i<document.myform.elements.length; i++){\n"
			"var elm = document.myform.elements[i];\n"
			"if ( elm.getAttribute('class') != cs ) continue;\n"
			"elm.checked = false;\n"
			"elm.value = 0;\n"
			"document.cookie = id+\"=0\";\n"
			"}\n"
			// return if trying to uncheck a checkbox
			"if ( ! old ) return;\n"
			//"alert(old+' to '+control.checked);\n"
			"control.checked = true;\n"
			"document.forms[0].elements[id].value = 1;\n"
			"document.cookie = id+\"=1\";\n"
			//"}\n"
			//"else {\n"
			//"alert('else '+old+' to '+control.checked);\n"
			//"control.checked = false;\n"
			//"document.forms[0].elements[id].value = 0;\n"
			//"document.cookie = id+\"=0\";\n"
			//"}\n"
			"}\n"
			*/

			// set the corresponding hidden parm for checkbox
			"function toggleBool ( control , id ) {\n"
			//"var opt = document.all ? document.all[id] :\n"
			//"document.getElementById ?\n"
			//"document.getElementById(id) : null\n"
			//"saveHist();\n"
			"if(document.forms[0].elements[id].value == 1 ) {\n"
			"document.forms[0].elements[id].value = 0;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=0\";\n"
			//"saveCookies();\n"
			"} else {\n"
			"document.forms[0].elements[id].value = 1;\n"
			// reloadResults() should save this in meta cookie now
			//"document.cookie = id+\"=1\";\n"
			//"saveCookies();\n"
			"}\n"
			"}\n"


			// set all cal days to gray if not yellow and
			// set clicked one to red
			"function setClock ( control, id , val ) {\n"
			// set hidden tag to clock value
			"var e = document.getElementById(id);\n"
			"e.value = val\n"
			// clear all but the yellow block off cal
			//"resetCal();\n"
			//"if ( control.style.backgroundColor ) return;\n"
			//"control.style.backgroundColor = 'red';\n"
			"reloadResults(1);\n"
			"}\n"

			/*
			"function resetCal ( ) {\n"
			// get all table cells to loop over
			"var tags=document.getElementsByTagName('td');\n"
			"var i;\n"
			"for(i=0; i< tags.length; i++){\n"
			"var elm = tags[i];\n"
			"if ( elm.getAttribute('class') != 'cal' ) continue;\n"
			"if ( elm.style.backgroundColor == 'yellow' ) continue;\n"
			"elm.style.backgroundColor = '';\n"
			"}\n"
			"}\n"
			*/

			""
			//"</script>\n"

			// https://developers.facebook.com/apps/
			// 356806354331432/summary
			// used for facebook login described at
			// developers.facebook.com/docs/reference/plugins/like/
			/*
			"<div id=\"fb-root\"></div>"
			"<script>(function(d, s, id) {\n"
			"var js, fjs = d.getElementsByTagName(s)[0];\n"
			"if (d.getElementById(id)) return;\n"
			"js = d.createElement(s); js.id = id;\n"
			"js.src = \"//connect.facebook.net/en_US/all.js#xfbml=1&appId=%s\";\n"
			"fjs.parentNode.insertBefore(js, fjs);\n"
			"}(document, 'script', 'facebook-jssdk'));</script>\n"
			, APPID
			*/
			);

	//printWelcomePopup ( sb , &st->m_msgfb );

		//
		// the invite box. hide this as well
		//
	/*
		sb.safePrintf ( "<div "
				"id=invite"
				">"
				"<div id=\"fb-root\"></div>" 
				"</div>\n");
	*/

		/*
		sb.safePrintf(
			      "<div "
			      "id=invite "
			      "class=grad3 "
			      "style=\""
			      "display:none;"
			      "position:absolute;"
			      "background-color:black;"

			      "border: 10px solid black;"

			      "top:60;"
			      "right:60;"
			      "left:60;"
			      "height:640px;"
			      "\""
			      ">"

			      "<br>"

			      "<table width=100%%>"
			      "<tr>"
			      "<td width=25%% valign=center>"
			      "<center>"
			      "<img src=/eventguru.png>"
			      "</center>"
			      "</td>"

			      "<td>"

			      "<center>"
			      //"<b>"
			      //"<font style=\"color:white;font-size:18px;"
			      //"text-shadow: 2px 4px 7px black;\">"
			      //"Invite your friends to use Event Guru."
			      //"</font>"
			      //"</b>"

			      // we set the innerhtml of this in the script
			      // so it loads and renders the facebook 
			      // javascript crap
			      "<div id=\"fb-root\">"
			      "</div>"

			      "<br>"
			      "<br>"

			      "</td>"

			      "<td width=25%%>"
			      "</td>"

			      "</tr>"
			      "</table>"

			      "</div>"
			      //, APPID
			      );
		*/


	sb.safePrintf (
		       "function foundloc ( position ) {"
		       // Check to see if there is already a location.
		       // There is a bug in FireFox where this gets
		       // invoked more than once with a cahced result.
		       //"if (locationMarker){"
		       //"return;"
		       //"}"
		       // Log that this is the initial position.
		       //"console.log( \"Initial Position Found\" );"
		       //"alert('got it');"
		       // Add a marker to the map using the position.
		       "var lat = position.coords.latitude;"
		       "var lon = position.coords.longitude;"
		       //"console.log('lat='+lat);"
		       //"console.log('lon='+lon);"
		       //"alert('lat='+lat);"
		       // compare to what was in cookie
		       "var ulat = getCookie('gpslat');"
		       "var ulon = getCookie('gpslon');"
		       // if the same, return
		       "if ( lat == ulat && lon == ulon ) return;"
		       // otherwise set cookies
		       "document.cookie = 'gpslat='+lat;"
		       "document.cookie = 'gpslon='+lon;"
		       // if it had those cookies do not reload either!
		       // otherwise ipad is always double loading because its
		       // gps is so sensitive and changes from one room to
		       // the other. the user can always reload their page
		       // the get the updated gps coordinates in here.
		       "if ( ulat && ulat != '' && ulon && ulon != '' ) "
		       "return;"
		       // and reload results
		       "reloadResults();"
		       "};\n"
		       
		       "function noloc ( ){"
		       "alert('Could not determine your GPS location');\n"
		       "console.log( 'Something went wrong:' );\n"
		       "};\n"
		       //"{"
		       //"timeout: (5 * 1000)," // 5 seconds?
		       //"maximumAge: (1000 * 60 * 15)," // 15 minutes?
		       //"enableHighAccuracy: true"
		       //"}"
		       );


	float radius = si->m_radius;
	if ( si->m_showPersonal ) radius = si->m_myRadius;
	sb.safePrintf ("function gpsCheck() {"

		       // save newinterest=xxxx into cookie before the user
		       // clicks a regular link and we fail to save it. we
		       // can't really do this when they click on the interest
		       // because we do not know what ixt%02"INT32" cookie name
		       // it will be assigned to, if any! it might be a dup
		       // and not added. these manual topics do not really
		       // have any "onclick" code that is used to set them
		       // as cookies, so we gotta convert their hidden inputs
		       // here into cookies.
		       //"if(window.location.href.indexOf('newinterest=')>=0||"
		       //"window.location.href.indexOf('topic=')>=0)"
		       //"saveCookies();\n"

		       // crap, if they click on unsubscribe in their email
		       // the new emailfreq is never saved in the cookie
		       // so if they click the "eventguru.com" link the
		       // cookie never gets saved to the new value and the
		       // old value is back!
		       "saveCookies();\n"

		       // print no=cookie popup
		       "if ( navigator.cookieEnabled == 0 ) {"
		       "document.getElementById('nocookies')."
		       "style.display='';"
		       "return;"
		       "}\n"

		       // get arrow
		       "var a = document.getElementById('arrow');\n"
		       "if ( a ) {\n"
		       // when we load init the arrow
		       "var px=getArrowPos(%f);\n"
		       //"alert('px='+px+' radius=%f');\n"
		       // arrow icon wdith is 20 (frogface) so center it
		       // ICON_WIDTH
		       "setArrowPos(px);\n"
		       // make arrow visible now
		       "a.style.display='';\n"
		       // it doesn't quite line up exactly right so force it!
		       "document.getElementById('radiusbox').value = '%"INT32"';\n"
		       "}\n"
		       //, si->m_radius
		       , radius
		       , (int32_t)(radius+.5)
		       );

	if ( si->m_didse == 0 )
		// timer for add search engine slideup
		sb.safePrintf("setTimeout('slideup()',2000);\n");

	sb.safePrintf( // fix the double reload bug
		       "var nr = getCookie('noreload');"
		       "if ( nr == '1' ) return;"
		       // fix the double reload bug. 15 second timeout.
		       // do not reload for 15 seconds.
		       "document.cookie = 'noreload=1;max-age=86400';"
		       );
	// do not ask for gps info if it is barred
	bool doGPS = true;
	if ( si->m_widget && ! si->m_doWidgetGPS ) doGPS = false;
	// do not do for /NM/Albuquerque type urls
	if ( si->m_locationInUrl ) doGPS = false;

	if ( doGPS )
		sb.safePrintf("navigator.geolocation.getCurrentPosition("
			      "foundloc,noloc);\n"
			      );
	sb.safePrintf("};\n");


	//////////
	//
	// for the distance slider. mx is the mouse x position.
	//
	//////////

	// get mouse position
	sb.safePrintf ( "function mouseX(e) {\n"
			"if (e.pageX) return e.pageX;\n"
			"else if (e.clientX)\n"
			"return e.clientX + document.body.scrollLeft;\n"
			"else return null;\n"
			"}\n"
			);
	/*
	// get mouse position
	sb.safePrintf ( "function mouseY(e) {\n"
			"if (e.pageY) return e.pageY;\n"
			"else if (e.clientY)\n"
			"return e.clientY + document.body.scrollTop;\n"
			"else return null;\n"
			"}\n"
			);
	*/

	// make this store the x offset to avoid recomputes
	sb.safePrintf ( "function getXOffset(e){\n"
			// i give up!!!
			//"return 339;\n"
			"var off = 0;\n"
			"while ( e != null ) {\n"
			"if ( e.offsetLeft > 0 && e.offsetLeft < 10000 ) {\n"
			//"if ( e.nodeName == 'body' ) break;\n"
			// set c to the computed style if it globalstorage is
			// non-zero
			"var dx = e.offsetLeft-e.scrollLeft;\n"
			// include border width if c is non-zero
			//"var c = isNaN(window.globalStorage)?0:"
			//"window.getComputedStyle(e,null);\n"
			//"if ( c ) dx += parseInt(c."
			//"getPropertyValue('border-left-width'),10);\n"
			//"var bw = parseInt(e.style.borderLeftWidth);\n"
			"off += dx;\n"
			//"alert('node='+e.nodeName+' val='+e.nodeValue+' dx='+dx+' cum='+off);\n"
			//"alert('offset='+off);"
			"}\n"
			"e = e.offsetParent;\n"
			"}\n"
			"return off;\n"
			"}\n"
			);

	sb.safePrintf( "var gloc = null;\n");
	sb.safePrintf( "var gqry = null;\n");
	sb.safePrintf( "var gpoo = 0;\n");

	// also set the appropriate hidden box to this new radius value
	char *name = "radius";
	if ( si->m_showPersonal ) name = "myradius";
	sb.safePrintf( // global var
		       "var drag = 0;\n"
		       "function setArrowPos ( mx ) {\n"
		       "var e = document.getElementById('distbar');\n"
		       // when distbar is invisible we can't compute its
		       // width right, so get the containing td for width
		       "var td1 = document.getElementById('distbartd');\n"
		       "var td1x = getXOffset(td1);\n"
		       //"alert('ax='+ax);"
		       "var offset = mx - td1x;\n"
		       "if ( offset < 10 ) return;\n"
		       //"alert('offset='+offset);"
		       // get width of the slider bar...
		       "var td2 = document.getElementById('nexttd');\n"
		       "var td2x = getXOffset(td2);\n"
		       "var width = td2x - td1x;\n"
		       // when its hidden it's width is like unknown!
		       // turn off if too narrow!
		       "if ( width < 150 ) {\n"
		       "e.style.display = 'none';"
		       "return;\n"
		       "}\n"
		       // otherwise, make sure its on! in case we resized
		       // the window bigger again!
		       "e.style.display = '';\n"
		       //"alert('visible '+width);\n"
		       //"alert('setpos-width='+width);\n"
		       "if ( offset + 23 > width ) return;\n"
		       //"alert('w='+width);"
		       // map pixels to distance then
		       // first 20 pixels is 1-2 miles
		       // divide up bar into 4 quads:
		       //   0..10
		       //  10..100
		       // 100.. 1k
		       //  1k..10k
		       "var a = width / 4 ;\n"
		       "var b = width / 2 ;\n"
		       "var c = (3 * width) / 4 ;\n"
		       "var d = width ;\n"
		       "var dist;\n"
		       "if ( offset <= a ) dist = (10 * offset) / a;\n"
		       "else if ( offset <= b)dist=10+(90*(offset-a))/(b-a);\n"
		       "else if ( offset<=c)dist=100+(900*(offset-b))/(c-b);\n"
		       "else dist=1000+(9000*(offset-c))/(d-c);\n"
		       "if ( dist < 1.0 ) dist = 1.0;\n"
		       // update form box with that distance. they can enter 
		       // it in manually as well!
		       "var nv = parseInt(dist);\n"
		       "document.getElementById('radiusbox').value = nv;\n"
		       // update appropriate guy as well!!!
		       "document.getElementById('%s').value = nv;\n"
		       //"alert('%s.value='+nv);\n"
		       // update arrow. 
		       // the frogface icon is 18 pixels across, so subtract
		       // 9 to center it on the arrow/click position
		       "document.getElementById('arrow').style.left = "
		       // we now use a 5 pixel wide red div as the arrow
		       // so subtract 2
		       "offset-2+'px';\n"
		       "}\n"
		       , name
		       );

	// convert a radius into arrow position
	sb.safePrintf( "function getArrowPos ( radius ) {\n"
		       //"var e = document.getElementById('distbar');\n"
		       //"var ax = getXOffset(e);\n"
		       "var td1 = document.getElementById('distbartd');\n"
		       "var td1x = getXOffset(td1);\n"
		       // get width of the slider bar...
		       "var td2 = document.getElementById('nexttd');\n"
		       "var td2x = getXOffset(td2);\n"
		       "var width = td2x - td1x;\n"
		       //"alert('getpos-width='+width);\n"
		       "var quad = width / 4;\n"
		       "var px = 4*quad;\n"
		       "if(radius<=10)"
		       "px=(radius*quad) / 10.0;\n"
		       "else if(radius<=100)"
		       "px=quad+(((radius-10)*quad)/90.0);\n"
		       "else if(radius<=1000)"
		       "px=2*quad+(((radius-100)*quad)/900.0);\n"
		       "else if(radius<=10000)"
		       "px=3*quad+(((radius-1000)*quad)/9000.0);\n"
		       "px = px + td1x;\n"
		       "return px;\n"
		       "}\n"
		       );
		       

		       

	//////////
	//
	// end distance slider code
	//
	//////////


	sb.safePrintf("</script>\n");

	//
	// this did force them to invite their friends kinda...
	// but disable it now. all that facebook javascript it spawned
	// seemed to slow things down.
	// 
	/*
	if ( ! hr->getLongFromCookie("showinvited",0) &&
	     si->m_useCookie &&
	     // and they are logged into facebook
	     hr->getLongLongFromCookie("fbid",0LL) ) {
		// this script slows loading down so only do it
		// when we are going to show the inviter.
		sb.safePrintf (""
			       "<script src=\"http://connect.facebook."
			       "net/en_US/all.js\"></script>"
			       "<script>"
			       "FB.init({\n"
			       "appId  : '%s',\n"
				       "});\n"
			       "function "
			       "sendRequestViaMultiFriendSelector(){\n"
			       "FB.ui({method: 'apprequests',\n"
			       "message: '%s'\n"
			       "}, requestCallback);\n"
			       "}\n"
			       "function requestCallback(response) {\n"
			       "// Handle callback here\n"
			       "}\n"
			       "</script>\n"
			       , APPID
			       , INVITE_MSG
			       );
	}
	*/

	return true;
}

bool printLocationRow ( SafeBuf &sb , char *str1 , char *str2 ) {

	if ( ! str1 || ! str1[0] ) return true;

	sb.safePrintf ( "<tr><td "
			"class=hand "
			"onmouseover=\"this.style.backgroundColor='purple';"
			"this.style.color='white'; gloc='%s';\" "

			"onmouseout=\"this.style."
			//"backgroundColor='#eeffee';"
			"backgroundColor='white';"
			"this.style.color='black'; gloc = null;"

			// this event is not gotten because onblur is
			// called for the input text box and it hides this
			// div before it can get the click event!
			//"onclick=\""
			//"setVal('where','%s');"
			, str1
			);

	sb.safePrintf ( //"reloadResults();"
			"\""
		       ">"
		       // MSIE does not like this nobr, wtf?
		       //"<nobr>%s</nobr>"
			"%s"
			"</td></tr>" 
			, str2
			);
	return true;
}

bool printQueryRow ( SafeBuf &sb , char *str1 , char *str2 ) {

	if ( ! str1 || ! str1[0] ) return true;

	sb.safePrintf ( "<tr><td "
			"class=hand "
			"onmouseover=\"this.style.backgroundColor='purple';"
			"this.style.color='white'; gqry='"
			);

	// encode it in case it has apostrophe's!
	int32_t strlen1 = gbstrlen(str1);
	sb.escapeJS ( str1 , strlen1 );

	sb.safePrintf ( "';\" "

			"onmouseout=\"this.style."
			//"backgroundColor='#eeffee';"
			"backgroundColor='white';"
			"this.style.color='black'; gqry = null;\" "
			);

	sb.safePrintf ( ">"
			// MSIE does not like this nobr!!! wtf?
			//"<nobr>%s</nobr>"
			"%s"
			"</td></tr>" 
			, str2
			);
	return true;
}

//
// query history
//
bool printQueryHistory ( SafeBuf &sb, SearchInput *si ) {
	sb.safePrintf("<table width=100%% border=0>");
	printQueryRow ( sb, "anything", "<i>anything - any event</i>" );
	printQueryRow ( sb , si->m_qry1 , si->m_qry1 ); 
	printQueryRow ( sb , si->m_qry2 , si->m_qry2 );
	printQueryRow ( sb , si->m_qry3 , si->m_qry3 );
	printQueryRow ( sb , si->m_qry4 , si->m_qry4 );
	sb.safePrintf("</table>");
	return true;
}

//
// location history
//
bool printLocationHistory ( SafeBuf &sb, SearchInput *si ) {
	//sb.safePrintf("<font color=%s><b><nobr>" // no <br> needed now
	//	      , GRADFONT );
	//sb.safePrintf("<b>");
	sb.safePrintf("<table width=100%% border=0>");

	printLocationRow ( sb, "anywhere", "<i>anywhere</i>" );

	printLocationRow ( sb, "autolocate", "<i>automatically get "
			   "my location</i>" );

	printLocationRow ( sb , si->m_loc1 , si->m_loc1 ); 
	printLocationRow ( sb , si->m_loc2 , si->m_loc2 );
	printLocationRow ( sb , si->m_loc3 , si->m_loc3 );
	printLocationRow ( sb , si->m_loc4 , si->m_loc4 );

	sb.safePrintf("</table>");
	//sb.safePrintf("</b>");

	return true;
}

/*
bool printQueryBoxes ( SafeBuf &sb, SearchInput *si ) {
	char *qval = si->m_displayQuery;
	char *styleAddition1 = 
		"font-weight:bold;"
		"color:black;"
		"font-size:20px;";

	if ( si->m_igoogle )
		styleAddition1 = 
		"font-weight:bold;"
		"color:black;"
		"font-size:13px;";

	if ( ! qval || ! qval[0] ) {
		qval = "Enter search terms here";
		styleAddition1 = 
			"color:gray;"
			"font-size:16px;";
		if ( si->m_igoogle )
			styleAddition1 = 
			"color:gray;"
			"font-size:13px;";
	}

	if ( si->m_igoogle )
		sb.safePrintf("<div style=line-height:8px;><br></div>");

	sb.safePrintf (
			// --- BEGIN querybox table
			"<table id=qbox cellpadding=5 cellspacing=0 "
		       "border=0><tr>"
			);

	// spacer column
	if ( ! si->m_igoogle )
		sb.safePrintf("<td></td>");

	// html encode qval in case it has quotes in it! needs to be &quot;
	char hbuf[128];
	SafeBuf henc(hbuf,128);
	henc.htmlEncode ( qval , gbstrlen(qval) , 0 );

	int32_t fs = 20;
	char *ws = "250px;";
	if ( si->m_igoogle ) {
		fs = 13;
		ws = "175px;";
	}

	sb.safePrintf ( 
			"<td>"
			"<input type=text style=\"font-size:%"INT32"px;"
		       "width:%s;height:25px;"
		       "border: 3px inset #ff7070;" // red border!
		       // without this msie allow text next to the input box
		       "display:block;%s\" id=q "
		       "name=q value=\"%s\" "
		       
		       "onclick=\"if ( this.style.color != 'black' ) "
		       "this.value='';"//setBorders('q','on');\""
		       "this.style.color='black';"
		       "this.style.fontWeight='bold';"
		       "this.style.fontSize='%"INT32"px';\""

		       "/>"
		       //, si->m_displayQuery );
			, fs
			, ws
		       , styleAddition1
			, henc.getBufStart()
			, fs
		       );


	//
	// query history
	//
	sb.safePrintf("<font color=%s size=-1><nobr>" // no <br> needed now
		      , GRADFONT 
		      );
	sb.safePrintf("<a onclick=\""
		      "setVal('q','');"
		      "reloadResults();"
		      "\">"
		      "<i>anything</i>"
		      "</a>" );
	if ( si->m_qry1 && si->m_qry1[0] )
		sb.safePrintf(", <a onclick=\""
			      "setVal('q','%s');"
			      "reloadResults();"
			      "\"><u>"
			      "%s"
			      "</u></a>"
			      , si->m_qry1
			      , si->m_qry1 );
	if ( si->m_qry2 && si->m_qry2[0] )
		sb.safePrintf(", <a onclick=\""
			      "setVal('q','%s');"
			      "reloadResults();"
			      "\"><u>"
			      "%s"
			      "</u></a>"
			      , si->m_qry2
			      , si->m_qry2 );
	if ( si->m_qry3 && si->m_qry3[0] )
		sb.safePrintf(", <a onclick=\""
			      "setVal('q','%s');"
			      "reloadResults();"
			      "\"><u>"
			      "%s"
			      "</u></a>"
			      , si->m_qry3
			      , si->m_qry3 );
	if ( si->m_qry4 && si->m_qry4[0] )
		sb.safePrintf(", <a onclick=\""
			      "setVal('q','%s');"
			      "reloadResults();"
			      "\"><u>"
			      "%s"
			      "</u></a>"
			      , si->m_qry4
			      , si->m_qry4 );
	sb.safePrintf("</nobr></font></td></tr>");

	char *whereValue = si->m_origWhere;
	char *styleAddition = 
		"font-weight:bold;"
		"color:black;"
		"font-size:20px;";

	if ( si->m_igoogle )
		styleAddition = 
		"font-weight:bold;"
		"color:black;"
		"font-size:13px;";


	if ( ! si->m_origWhere || ! si->m_origWhere[0] ) {
		whereValue = "Enter city, state, country or zip here";
		styleAddition = 
			"color:gray;"
			"font-size:16px;";
		if ( si->m_igoogle )
			styleAddition = 
			"color:gray;"
			"font-size:13px;";
	}


	sb.safePrintf ( "<tr>" ); // location box

	if ( ! si->m_igoogle )
		sb.safePrintf ("<td>"
			       "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
			       "</td>");

	sb.safePrintf (
		       //"<td>"
		       //"<font style=\"color:%s;font-size:18px;"
		       //"text-shadow: 2px 4px 7px %s;"
		       //"\"> &nbsp;<b>in</b>"
		       //"&nbsp; </font></td>"
		       "<td>"
		       "<input type=text "
		       "style=\"font-size:%"INT32"px;width:%s;height:25px;"
		       "border: 3px inset #50ff50;"
		       // without this msie allow text next to the input box
		       "display:block;%s"
		       "\" id=where name=where "
		       "value=\"%s\" "
		       "onclick=\"if ( this.style.color != 'black' ) "
		       "this.value='';"//setBorders('where','on');\""
		       "this.style.color='black';"
		       "this.style.fontWeight='bold';"
		       "this.style.fontSize='%"INT32"px';\""
		       ">"
		       , fs
		       , ws
		       , styleAddition
		       , whereValue
		       , fs
		       );


	printLocationHistory ( sb , si );

	sb.safePrintf ( "</td></tr>"
			"<tr>"
			);

	// spacer column
	if ( ! si->m_igoogle )
		sb.safePrintf("<td></td>");

	sb.safePrintf (
			"<td>"
			//"<tr>"
			//"<td>"
			"<input type=submit style=\"font-size:18px;\" "
			"onclick=\"reloadResults(1);return;\" "
			"value=\"Search Events\">"
			"</td>"

			// spacer
			//"<td>&nbsp;&nbsp;&nbsp;&nbsp;</td>"

			// facebook login cell
			//"&nbsp; &nbsp; &nbsp; "
			//"&nbsp; &nbsp; &nbsp; "
			//"<div style=\"display:inline;border: 2px solid black;border-bottom: 5px solid black;\" class=\"fb-login-button\" data-show-faces=\"false\" data-width=\"200\" data-max-rows=\"1\"></div>"
			//"<td valign=center>"
			//"<div style=\"height:20px;width:80px;overflow:hidden;\">"

			//"<div class=fb-login-button>"
			//"Login with Facebook"

			//"<fb:login-button data-width=200 data-show-faces=false></fb:login-button>"
			// you can add like &scope=email after the redirect_uri
			// to get more permissions
			// http://developers.facebook.com/docs/reference/api/permissions/
			//"<a onclick=\"top.location.href='http://www.facebook.com/dialog/oauth?client_id=%s&redirect_uri=%s&scope=user_events,friends_events'\"><img width=152 height=22 src=fblogin.png></a>" 

			//"</div>"

			//, APPID 
			//, APPHOST
			);

	// END qbox table
	sb.safePrintf ( "</td></tr></table>");

	return true;
}
*/

bool printTopBarNav ( SafeBuf &sb , State7 *st ) {

	//char *host = "www2.flurbit.com:8000";

	SearchInput *si = &st->m_si;

	sb.safePrintf ( "<form name=myform "//target=searchframe>"
			"autocomplete=off "
			// i guess returning false here prevents the
			// browser from changing the window's url...
			"onSubmit=\"return false;\">"
			);

	printScripts1 ( sb , st );

	int32_t ip = 0;
	if ( st->m_socket ) ip = st->m_socket->m_ip;


	//bool printLogo = true;
	//if ( si->m_igoogle ) printLogo = false;

	// try skipping for now
	//printLogo = false;

	// . begins but does not end the toptable
	// . begins a new row and puts in logo if printLogo is true
	// . if igoogle, will end both tables...
	printBlackBar ( sb , &st->m_msgfb , "/" , ip , false, // printLogo
			si->m_igoogle , st );

	return true;

	/*
	if ( si->m_igoogle ) return true;

	// try skipping for now!
	return true;

	sb.safePrintf ( // -- querybox cell
		       // i had set this to width=400px but ie likes it
		       // better as 100%
		       "<td valign=center width=400px>"//100%%>"
		       //"&nbsp;&nbsp; "
		       );

	// prints a table
	printQueryBoxes ( sb , si );
	
	sb.safePrintf ( // spacer after query box table
			"</td><td>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</td>"

			// this cell contains the ICON FOURSQUARES TABLE
			"<td valign=center>"

			//
			// ICON FOURSQUARES TABLE
			// 

			"<table border=0><tr><td>"

			"<center>"
			"<a "
			"onclick=\"document.getElementById('cb_showpersonal')"
			".checked=true;document.getElementById"
			"('showpersonal').value=1;"
			// hidden input
			"document.forms[0].elements['suggestions'].value=1;"
			// and cookie. this is part of meta cookie now.
			// reloadResults() should put it into the meta cookie
			// because we got a form tag for it.
			//"document.cookie = 'suggestions=1';"
			// then reload
			"reloadResults();\">"
	
			"<img width=32 height=32 src=/point.png "
			"title=\"Show events tailored to your interests.\">"

			"<br>"
			//"<div style=line-height:8px;><br></div>"
			"<b><span style=\""
			"color:black;"
			"font-size:12px;"
			//"outline-color:black;"
			//"outline-width:2px;"
			//"outline-style:inherit;"
			//"text-shadow: 2px 4px 10px black;"
			"\"><nobr>For You</nobr>"
			"</span>"
			"</b>"
			"</center>"
			"</a>"
			"</td>"
			// spacer cell
			"<td width=20px></td>"
			);


	// next cell in same row in ICON FOURSQUARE (friends invited)
	sb.safePrintf("</td><td valign=bottom><center>");
	if ( ! st->m_msgfb.m_fbId )
		sb.safePrintf ( "<a onclick=\"needLogin();\">" );
	else
		// need to turn off just for you?
		sb.safePrintf ( "<a onclick=\"setVal('showfriends',1);"
				"document.forms[0].elements['suggestions']."
				"value=0;"
				"setVal('showpersonal',0);"
				"reloadResults();"
				"\">"
				);
	sb.safePrintf ( "<img width=32 height=32 src=/friends32.png "
			"title=\"Show events your friends are into.\">"

			"<br>"
			//"<div style=line-height:8px;><br></div>"
			"<b><span style=\""
			"color:black;"
			"font-size:12px;"
			"\"><nobr>Friends</nobr>"
			"</span>"
			"</b>"
			"</a>"
			"</center>"
			// NEXT ROW in ICON FOURSQUARE
			"</td>"
			"</tr>"
			// spacer row
			"<tr><td colspan=9 height=20px></td></tr>"
			"<tr>"
			"<td>"
			"<center>"
			);
	// . 2nd row in ICON FOURSQUARE
	// . show my pic for my events
	if ( ! st->m_msgfb.m_fbId ) {
		sb.safePrintf ( "<a onclick=\"needLogin();\">" 
				"<img border=0 "
				"title=\"Show events you like, are going to "
				"or have been invited to.\" "
				"src=/fbface32.png "
				"width=32 height=32>"
				);
	}	
	else {
		sb.safePrintf ( //"<a href=/?showmystuff=1&showpersonal=0>"
				"<a onclick=\"setVal('showmystuff',1);"
				"document.forms[0].elements['suggestions']."
				"value=0;"
				"setVal('showpersonal',0);"
				"reloadResults();"
				"\">"
				"<img border=0 "
				"title=\"Show events you like, are going to "
				"or have been invited to.\" "
				"src=http://graph.facebook.com/%"UINT64"/picture "
				"width=32 height=32>"
				, st->m_msgfb.m_fbId );
	}
	
	sb.safePrintf ( "<br>"
			//"<div style=line-height:8px;><br></div>"
			"<b><span style=\""
			"color:black;"
			"font-size:12px;"
			"\"><nobr>Invited</nobr>"
			"</span>"
			"</b>"
			"</a>"
			"</center>"
			"</td>"
			// spacer cell
			"<td width=20px>&nbsp;&nbsp;</td>"

			// 4th ICON FOURSQUARE is empty
			// END the ICON FOURSQUARE TABLE
			"<td>"
			"<center>"
			"<a href=\"/?showwidget=1&widgetmenu=1&suggestions=0&categories=0&calendar=0&socialmenu=0&location=0&timemenu=0&linksmenu=0&display=0&map=0\">"
			"<img border=0 "
			"title=\"Install this widget on your website. "
			"Make $1 per click.\" "
			"src=/gears32.png "
			"width=32 height=32>"
			"<br>"
			//"<div style=line-height:8px;><br></div>"
			"<b><span style=\""
			"color:black;"
			"font-size:12px;"
			"\"><nobr>Widget</nobr>"
			"</span>"
			"</b>"
			"</a>"
			"</center>"

			"</td></tr></table>"
			);
	

	// . end the cell containing the ICON FOURSQUARE TABLE
	sb.safePrintf ( "</td>" );

	// spacer
	sb.safePrintf("<td>&nbsp;&nbsp;&nbsp;&nbsp;</td>");

	// print the ad table
	sb.safePrintf("<td>");
	printAdTable ( sb );
	sb.safePrintf("</td>");


	// . force the other td cells to their specified widths:
	sb.safePrintf ( "<td width=100%%>&nbsp;</td>"

			// - END logo/query row
			"</tr>"
			// - shadow row
			//"<tr cellspacing=5 height=5px><td colspan=9 "
			//"bgcolor=#%s></td></tr>"
			// END TOP TABLE
			"</table>" 
			//, GRAD2
			);

	return true;
	*/
}

bool printAdminLinks ( SafeBuf &sb , State7 *st ) {

	SearchInput *si = &st->m_si;

	if ( ! si->m_isMasterAdmin ) return true;

	Msg40 *msg40 = &(st->m_msg40);
	// how many results were requested?
	int32_t docsWanted = msg40->getDocsWanted();

	// convenient admin link
	sb.safePrintf(" &nbsp; "
		      "<font color=red><b>"
		      "<a href=\"/master?c=%s\">"
		      "[admin]"
		      "</a></b></font>",si->m_coll2);

	// print reindex link
	// get the filename directly
	sb.safePrintf (" &nbsp; "
		       "<font color=red><b>"
		       "<a href=\"/admin/reindex?c=%s&q=%s\">"
		       "[query reindex]</a></b>"
		       "</font> ", si->m_coll2 , si->m_qe );
	
	sb.safePrintf (" &nbsp; "
		       "<font color=red><b>"
		       "<a href=\"/addurl?c=%s&qts=%s"
		       "&strip=1&spiderLinks=1\">"
		       "[scrape]</a></b>"
		       "</font> ", si->m_coll2 , si->m_qe );

	// if its an ip: or site: query, print ban link
	if ( strncmp(si->m_displayQuery,"ip:",3)==0) {
		// get the ip
		char *ips = si->m_displayQuery + 3;
		// copy to buf, append a ".0" if we need to
		char buf [ 32 ];
		int32_t i ;
		int32_t np = 0;
		for ( i = 0 ; i<29 && (is_digit(ips[i])||ips[i]=='.'); i++ ){
			if ( ips[i] == '.' ) np++;
			buf[i]=ips[i];
		}
		// if not enough periods bail
		if ( np <= 1 ) goto skip2;
		if ( np == 2 ) { buf[i++]='.'; buf[i++]='0'; }
		buf[i] = '\0';
		// search ip back or forward
		int32_t ip = atoip(buf,i);
		sb.safePrintf ("&nbsp <b>"
			       "<a href=\"/search?q=ip%%3A%s&c=%s&n=%"INT32"\">"
			       "[prev %s]</a></b>" , 
			       iptoa(ip-0x01000000),si->m_coll2,docsWanted,
			       iptoa(ip-0x01000000));
		sb.safePrintf ("&nbsp <b>"
			       "<a href=\"/search?q=ip%%3A%s&c=%s&n=%"INT32"\">"
			       "[next %s]</a></b>" , 
			       iptoa(ip+0x01000000),si->m_coll2,docsWanted,
			       iptoa(ip+0x01000000));
	}
 skip2:
	// if its an ip: or site: query, print ban link
	if ( strncmp(si->m_displayQuery,"site:",5)==0) {
		// get the ip
		char *start = si->m_displayQuery + 5;
		char *sp = start;
		while ( *sp && ! is_wspace_a(*sp) ) sp++;
		char c = *sp;
		//int32_t bannedTagId = getTagTypeFromStr("manualban",9);
		// get the filename directly
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       //"<a href=\"/admin/tagdb?f=%"INT32"&c=%s&u=%s\">"
			       "<a href=\"/admin/tagdb?"
			       //"tagid0=%"INT32"&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s\">"
			       "[ban %s]</a></b>"
			       //"</font> ", rs , coll , start , start );
			       "</font> ",si->m_coll2 , start );
		*sp = c;
	}
	if ( strncmp(si->m_displayQuery,"gbad:",5)==0) {
		// get the ip
		char *start = si->m_displayQuery + 5;
		char *sp = start;
		while ( *sp && ! is_wspace_a(*sp) ) sp++;
		char c = *sp;
		*sp = '\0';
		//int32_t bannedTagId = getTagTypeFromStr("manualban",9);
		sb.safePrintf (" &nbsp; "
			       "<font color=red><b>"
			       "<a href=\"/admin/tagdb?"
			       //"tagid0=%"INT32"&"
			       "tagtype0=manualban&"
			       "tagdata0=1&"
			       "c=%s"
			       "&u=%s-gbadid.com\">"
			       "[ban %s]</a></b>"
			       "</font> ", si->m_coll2 , start , start );
		*sp = c;
	}

	// cache switch for admin
	if ( msg40->getCachedTime() > 0 ) {
		// get the filename directly
		sb.safePrintf(" &nbsp; "
			      "<font color=red><b>"
			      "<a href=\"/search?c=%s",
			      si->m_coll2 );
		// finish it
		sb.safePrintf("&q=%s&rcache=0&seq=0&rtq=0\">"
			      "[cache off]</a></b>"
			      "</font> ", si->m_qe );
	}

	return true;
}

// first we re-write the summary with highlights into the "src" buf, then
// we call this to write it into the final display buffer, "dst" with
// the appropriate <br> tags inserted
bool brformat ( SafeBuf *src ,
		SafeBuf *dst ,
		int32_t     width ,
		// truncate if more than this many
		int32_t     maxChars = 9999999 ) {

	// leave room for adding a ...
	maxChars -= 3;

	int32_t srcLen = src->length();
	// brs needed
	int32_t brsNeeded = (srcLen / width) * 2;
	// byte sneeded to store
	int32_t need = srcLen + brsNeeded * 5;
	// ensure enough room
	if ( ! dst->reserve ( need ) ) return false;

	char *pstart = src->getBufStart();
	char *p      = src->getBufStart();
	char *pend   = p + src->length();
	char *dstart = dst->getBuf();
	char *d      = dst->getBuf();
	char *dend   = dst->getBufEnd();
	char size;
	int32_t col = 0;
	char *lastSpaceSrc = NULL;
	char *lastSpaceDst = NULL;
	char  savedChar = 0;
	char  lastChar  = 0;
	bool  lastWasBr = false;
	int32_t  charCount = 0;
	bool  inFontTag = false;
	// scan the summary
	for ( ; p < pend ; p += size ) {
		// get utf8 char size
		size = getUtf8CharSize(p);
		// skip tags
		if ( *p == '<' ) {
			bool copy = true;
			// if a br tag, do not copy over
			if ( to_lower_a(p[1])=='b' &&
			     to_lower_a(p[2])=='r' ) 
				copy = false;

			// a tag start? must be a font font tag
			// because that's all we allow in the title or
			// event description
			else if ( p[1] == 'f' && 
				  p[2] == 'o' &&
				  p[3] == 'n' )
				inFontTag = 1;
			else if ( p[1] == '/' && 
				  p[2] == 'f' &&
				  p[3] == 'o' )
				inFontTag = 0;

			for ( ; *p != '>' ; p++ ) 
				if ( copy ) *d++ = *p;
			// the final '>'
			if ( copy ) *d++ = '>';
			continue;
		}
		// save it
		savedChar = lastChar;
		// record last char
		lastChar = *p;
		// skip spaces after a space
		if ( is_wspace_a(*p) && p>pstart && is_wspace_a(savedChar) ) 
			continue;
		// or space after we inserted a br tag
		if ( lastWasBr && is_wspace_a(*p) )
			continue;
		// assume not now
		lastWasBr = false;
		// record last space, actually ptr to char after it
		if ( is_wspace_a(*p) || *p == ',' ) {
			lastSpaceSrc = p;
			lastSpaceDst = d;
		}
		// store each char
		if ( size == 1 )
			*d++ = *p;
		else {
			gbmemcpy ( d , p , size );
			d += size;
		}
		// do not exceed maxchars
		if ( ++charCount >= maxChars ) {
			// rewind back to last space
			d = lastSpaceDst;
			// if none...
			if ( ! d ) { d = dstart; *d = '\0'; }
			// update safe buf 
			dst->incrementLength ( d - dstart );
			// close any open <font> tags so we don't spill the 
			// highlighting over the whole search result!!
			if ( inFontTag ) dst->safePrintf("</font>");
			// store a ...
			if ( d != dstart ) dst->safeMemcpy("...",3);
			// all done
			return true;
		}
		// inc col count
		col++;
		// if still before width, continue
		if ( col < width ) continue;
		// reset
		col = 0;
		// if no last space, just insert br
		if ( ! lastSpaceSrc ||
		// if "word" bigger than "col" do not rewrind because
		// we get into an infinite loop
		     p - lastSpaceSrc >= width ) {
			gbmemcpy(d,"<br>\n",5);
			d += 5;
			continue;
		}
		// rewind otherwise
		p = lastSpaceSrc;
		d = lastSpaceDst;
		// size is one
		size = 1;
		// skip d after the space
		d++;
		// then insert the br
		gbmemcpy(d,"<br>\n",5);
		d += 5;
		lastWasBr = true;
	}
	// sanity
	if ( d > dend ) { char *xx=NULL;*xx=0; }
	// remove final br if any
	if ( d - 5 > dstart && memcmp(d-5,"<br>\n",5) == 0 ) d -= 5;
	// update safe buf 
	dst->incrementLength ( d - dstart );

	// close any open <font> tags so we don't spill the highlighting
	// over the whole search result!!
	if ( inFontTag ) dst->safePrintf("</font>");

	return true;
}

void printAdminEventOptions ( SafeBuf* sb, 
			      Msg40* msg40, 
			      int32_t i,
			      SearchInput* si,
			      char* coll,
			      char* pwd,
			      CollectionRec* cr,
			      char* url, int32_t urlLen,
			      SafeBuf* banSites) {
	
	if (!si->m_isAssassin || si->m_isFriend) return;
	// now the ip of url
	//int32_t urlip      = msg40->getIp(i);
	int64_t docId = msg40->getDocId(i);
	Url uu;
	uu.set ( url , urlLen );
	char dbuf [ MAX_URL_LEN ];
	int32_t dlen = uu.getDomainLen();
	gbmemcpy ( dbuf , uu.getDomain() , dlen );
	dbuf [ dlen ] = '\0';
	// newspaperarchive urls have no domain
	if ( dlen == 0 ) {
		dlen = uu.getHostLen();
		gbmemcpy ( dbuf , uu.getHost() , dlen );
		dbuf [ dlen ] = '\0';
	}

	sb->safePrintf(" - <a \"onclick=\"toggleDisplay('%"INT64"');\">"
		       "more &raquo;</a>"
		       "<div id=\"%"INT64"\" style=\"display:none;\"><br>\n",
		       docId, docId);


	sb->safePrintf( "<a href=\"/search?c=%s&dr=0&"
			   "n=100&q=sitelink:",coll);
	// encode the url now
	sb->urlEncode(dbuf , dlen);
	sb->safePrintf ("\">Linkers to this Site</a><br>\n"); 
		
			
	sb->safePrintf("SubSites: ");
	sb->safePrintf (" <a href=\"/search?"
			   "q=site%%3A%s&sc=0&c=%s\">"
			   "%s</a>" ,
			   dbuf , coll , dbuf );



	char* path   = uu.getPath();
	char* pathEnd   = path + uu.getPathLen();
	char* q = path;
	while (q < pathEnd) {
		dbuf [ dlen++ ] = *q++;
		if(*q == '/') {
			dbuf [ dlen++ ] = *q++;
			dbuf [ dlen ] = '\0';
			sb->safePrintf (" | <a href=\"/search?"
					"q=site%%3A%s&sc=0&c=%s\">"
					"%s</a>" ,
					dbuf , coll , dbuf );
		}
	}
	sb->safePrintf("<br>\n");


	dlen = uu.getDomainLen();
	gbmemcpy ( dbuf , uu.getDomain() , dlen );
	dbuf [ dlen ] = '\0';
	
	sb->safePrintf("Ban By Domain: ");
	
	//int32_t bannedTagId = getTagTypeFromStr("manualban",9);
	sb->safePrintf("<a href=\"/admin/tagdb?"
		       "tagtype0=manualban&"
		       "tagdata0=1&"
		       "u=%s&c=%s\">"
		       "<nobr><b>BAN %s</b></a>",
		       dbuf , coll , dbuf );
	banSites->safePrintf("%s+", dbuf);

	sb->safePrintf("<br>\n");

	sb->safePrintf("</div>");
}

// stores it in a vector
static bool printResult ( CollectionRec *cr, 
			  State7 *st, 
			  Query &qq, 
			  char *coll, 
			  char *pwd, 
			  Msg40 *msg40, 
			  int32_t &firstNum, 
			  int32_t &count, 
			  int32_t ix,
			  bool &printed,
			  SafeBuf &sb, 
			  SafeBuf* banSites,
			  ExpandedResult *er ,
			  int32_t resultNum ,
			  int32_t iconId ) {

	Highlight hi;
	SearchInput *si = &st->m_si;

	// ensure not all cluster levels are invisible
	if ( si->m_debug )
		logf(LOG_DEBUG,
		     "query: result #%"INT32" clusterlevel=%"INT32"",
		     ix,
		     (int32_t)msg40->getClusterLevel(ix));

	Msg20      *m20 = msg40->m_msg20[ix];
	Msg20Reply *mr  = m20->m_r;

	if(!sb.reserve2x(8048)) {
		//if (si->m_cat_dirId > 0)
		//	sb.safePrintf("</td><td>");
		log("query:Maximum page buffer size (%i bytes)"
		    " exceeded. Results may be truncated.",
		    sb.getBufEnd() - sb.getBuf());
		
		if ( !sb.reserve(1024) ) {
			sb.safePrintf ("\n<br><br><b>"
				       "...Results truncated..."
				       "</b><br><br>\n");
		}
		return false;
	}
	// . if this result is first below the bar (w/o all query 
	//   terms) then print the gray bar
	// . use blue bg w/ bold, white foreground
	//if ( ix + firstNum >= numAbove && ! printed ) {
	if ( (msg40->getBitScore(ix) & (0x40|0x20)) == 0x00 && ! printed ) {
		//if (si->m_cat_dirId > 0)
		//	sb.safePrintf("</td><td>");
		printed = true;
		sb.safePrintf("<table cellpadding=3 "
			      "width=100%%>"
			      "<tr><td bgcolor=#0079ba>"
			      "<font color=#ffffff size=-1><b>"
			      "<center>"
			      "The results below may not "
			      "have all of your"
			      " query terms. "
			      "[ <a href=\"/superRecall.html\">"
			      "<font color=#ffffff size=-1>"
			      "info"
			      "</a> ]"
			      "</center>"
			      "</b></font>"
			      "</td></tr></table>"
			      "<br>\n");
	}

	if ( si->m_docIdsOnly ) {
		sb.safePrintf("%"INT64"<br>\n", mr->m_docId );
		return true;
	}
	// skip if it is hidden due to clustering
	//if ( msg40->getClusterLevel(i) == 2 ) continue;
	// count it as displayed
	//count--;
	// light gray bg if from another cluster
	//if ( msg40->isImported(i) )
	//	p += sprintf(p,"<table width=100%%><tr>"
	//		     "<td bgcolor=#f0f0f0>\n");
	// get the url
	char *url    = mr->ptr_ubuf      ; // msg40->getUrl(i) ;
	int32_t  urlLen = mr->size_ubuf - 1 ; // msg40->getUrlLen(i) ;
	int32_t  err    = mr->m_errno       ; // msg40->getErrno(i);

	// nah, use cached url so we don't hit facebook.com !
	//SafeBuf tmp;
	//if ( strstr ( url , "http://www.facebook.com/" ) ) {
	//	tmp.safePrintf("%s", APPHOST );
	//	printEventCachedUrl ( tmp, mr , m20 , si->m_qe, si->m_coll );
	//	url = tmp.getBufStart();
	//	urlLen = gbstrlen(url);
	//}

	// . remove any session ids from the url
	// . for speed reasons, only check if its a cgi url
	Url uu;
	uu.set ( url , urlLen, false, true );
	//if ( !err && url && urlLen >= 0 && strchr (url , '?')){
	url    = uu.getUrl();
	urlLen = uu.getUrlLen();
	//}
	// get my site hash
	uint64_t siteHash = 0;
	if ( uu.getHostLen() > 0 ) 
		siteHash = hash64(uu.getHost(),uu.getHostLen());
	// if this msg20 had an error print "had error"
	if ( err || urlLen <= 0 || ! url ) {
		// it's unprofessional to display this in browser
		// so just let admin see it
		if ( si->m_isAssassin && !si->m_isFriend ) {
			//if (si->m_cat_dirId > 0)
			//	sb.safePrintf("</td><td>");
			sb.safePrintf("<i>docId %"INT64" had error: "
				      "%s</i><br><br>",
				      mr->m_docId,//msg40->getDocId(i),
				      mstrerror(err));
		}
		//else if (si->m_cat_dirId > 0)
		//	skipDirRow = true;
		// log it too!
		log("query: docId %"INT64" had error: %s.",
		    mr->m_docId,mstrerror(err));
		return true;
	}

	// embody the result. give it the id 'res%"INT32"' so clicking on the
	// trashbin will result in it being hidden?
	//sb.safePrintf("<span class=result id=res%"INT32">", iconId );


	// the score if admin
	if ( si->m_isMasterAdmin && !si->m_isFriend ) {
		int32_t level = (int32_t)msg40->getClusterLevel(ix);
		char evs[1024];
		sprintf(evs,"eventhash=%"UINT64" eventid=%"INT32" "
			"sumhash=%"UINT32" ",
			mr->m_eventHash64,
			(int32_t)msg40->getEventId(ix),
			(int32_t)mr->m_eventSummaryHash);
		// print out score_t
		sb.safePrintf ( "s=%.03f "
				"%s"
				"docid=%"UINT64" "
				    "sitenuminlinks=%"INT32"%% "
				    "tier=%"INT32" "
				    "bitscore=0x%hhx "
				    //"rs=%"INT32" "
				    "hop=%"INT32" "
				    "cluster=%"INT32" "
				    //"sumryscore=%"INT32" "
				    "prox=%.03f "
				    //"sections=%"INT32" "

				    "oldscore=%.02f "
				    "diversityfactor=%.02f "
				    "qualfactor=%.03f "
				    "inlinkfactor=%.02f "
				    "proxfactor=%.02f "
				    "ctypefactor=%.02f "
				    "langfactor=%.02f "

				    "summaryLang=%s "
				    "(%s)<br>",
				//(int32_t)ix + firstNum + 1,
				    (float)msg40->getScore(ix) ,
				evs,
				mr->m_docId,
				    (int32_t )mr->m_siteNumInlinks,
				    (int32_t)msg40->getTier(ix) ,
				    (uint8_t)msg40->getBitScore(ix) ,
				    //(int32_t)msg40->getRuleset(i),
				    (int32_t)mr->m_hopcount,
				    level ,
				    //(int32_t)msg40->getSummaryScore(i),
				    (float)mr->m_proximityScore,
				    //(int32_t)msg40->getInSectionScore(i),

				    (float)m20->m_pqr_old_score ,
				    (float)m20->m_pqr_factor_diversity ,
				    (float)m20->m_pqr_factor_quality  ,
				    (float)m20->m_pqr_factor_inlinkers,
				    (float)m20->m_pqr_factor_proximity ,
				    (float)m20->m_pqr_factor_ctype     ,
				    (float)m20->m_pqr_factor_lang      ,

				    getLanguageString(mr->m_summaryLanguage),
				    g_crStrings[level]);
	}

	// print the balloon first. let search result wrap around it.
	if ( ! si->m_widget && ! si->m_includeCachedCopy )
		printBalloon ( sb , si, mr->m_balloonLetter , resultNum );


	bool showImages = true;
	if ( si->m_widget && ! si->m_images ) showImages = false;
	if ( ! mr->ptr_imgUrl ) showImages = false;


	// print youtube and metacafe thumbnails here
	// http://www.youtube.com/watch?v=auQbi_fkdGE
	// http://img.youtube.com/vi/auQbi_fkdGE/2.jpg
	// get the thumbnail url
	if ( showImages ) {

		// is it visible or not?
		char *s = "";
		if ( ! si->m_images ) s = " style=\"display:none\"";
		sb.safePrintf ("<span id=images%"INT32"%s>" , iconId , s );

		sb.safePrintf ( "<a href=\"" );
		printEventTitleLink ( sb , si, mr , st );
		sb.safePrintf("\"");
		// don't leave the original web page, open in new tab
		if ( si->m_widget ) sb.safePrintf(" target=_blank");
		sb.safePrintf(">");
		// assume 50x50 for facebook
		sb.safePrintf ("<img width=50 height=50 "
			       "style=\"padding-right:10px;\" "
			       "align=left src=%s></a>",
			       mr->ptr_imgUrl);

		sb.safePrintf ("</span>");
	}

	// the a href tag
	sb.safePrintf ( "\n\n" );
	// then if it is banned 
	if ( mr->m_isBanned ) // msg40->isBanned(ix) ) 
		sb.safePrintf("<font color=red><b>BANNED</b></font> ");


	evflags_t evf = mr->m_eventFlags;
	// take off this bit, used only internally
	evf &= ~(evflags_t)EV_DESERIALIZED;

	// print the event flags first
	if ( evf && si->m_isMasterAdmin ) {
		// color in red
		if ( ! sb.safePrintf("<b><font color=red>[") )
			return false;
		// defined in Events.cpp i guess
		if ( ! printEventFlags ( &sb , evf ) )
			return false;
		// color in red
		if ( ! sb.safePrintf("]</font></b> ") )
			return false;
	}


	int32_t icc = si->m_includeCachedCopy;

	// keep the event title and the thumbs up/down on same line
	//sb.safePrintf("<nobr>");

	if ( ! mr->ptr_turkForm && ! icc ) {
		//if ( ! si->m_widget ) sb.safePrintf("<nobr>");
		sb.safePrintf ( "<a href=\"" );
		printEventTitleLink ( sb , si, mr , st );
		sb.safePrintf("\"");
		// do not leave the original website the widget is on
		// just open in a new tab
		if ( si->m_widget )
			sb.safePrintf(" target=_blank");
		sb.safePrintf(">");
	}

	// print a span so they can format it better
	if ( ! mr->ptr_turkForm ) sb.safePrintf("<span class=title>");

	// print link to live url
	if ( ! mr->ptr_turkForm && icc ) {
		sb.safePrintf ( "<a href=\"" );
		sb.utf8Encode ( url , urlLen ); 
		sb.safePrintf("\">");
	}


	// just show turk form and not summary?
	if ( mr->ptr_turkForm ) {
		// exclude trailing \0
		if ( ! sb.safeMemcpy ( mr->ptr_turkForm ,
				       mr->size_turkForm - 1 ) )
			return false;
	}

	//if ( imgUrl )
	//	sb.safePrintf("<table><tt><td><img align=left "
	//		      "src=\"%s\"></td><td>",
	//		      imgUrl);

	//int32_t width = si->m_summaryMaxWidth;
	// if using widget, do not do format really. just allow widget's
	// narrowness to wrap it. 9999999 is now the default in Parms.cpp!
	//if ( si->m_widget ) width = 999999;
	// do not use that parm because it default to one in coll.conf
	int32_t width = st->m_hr.getLong("sw",9999999);
	//this isn't working ok...
	//if i change sw on cached page it doesn't work...

	// mark insertion point into sb for adding a <strike> tag
	int32_t insertionPoint1 = sb.length();

	//if ( ! mr->ptr_turkForm ) {
	char tbuf[1024];
	SafeBuf ttt;
	ttt.setBuf ( tbuf , 1024 , 0 , false, csUTF8 );
	// use function now
	if ( ! printEventTitle ( ttt , mr , st ) )
		return false;
	// truncate to 80 at least!
	// make it 60 now to fit icons in
	// almost but not quite, try 57
	int32_t trunc = 50;//width;
	// widget?
	if ( si->m_widget ) trunc = 80;
	// display accept/reject icons for turks as well!!
	// so truncate more in that case so icons fit!
	if ( si->m_isTurk ) trunc -= 15;
	// no truncating for email
	if ( si->m_emailFormat ) trunc = 200;
	// ok, just make it big for all now. the thumbs will be on the
	// next line a lot, but so what.... it's better to see more title
	trunc = 200;
	// then format ttt according to width and store into sb
	brformat ( &ttt , &sb , width , trunc );//80 );
	//sb.safePrintf("<br>\n");
	//}
	
	// turk forms were not in a link
	//if ( ! mr->ptr_turkForm ) {
	// print a span so they can format it better
	sb.safePrintf("</span></a>");

	//int32_t start_time = 0;
	//if ( ! recurring ) start_time = mr->m_start_time;

	int32_t myLikedbFlags = 0;
	
	// cached copy now has its own middle black bar of icons
	if ( ! si->m_widget && ! si->m_includeCachedCopy ) {
		// is it visible or not?
		char *v = "";
		if ( ! si->m_icons ) v = " style=\"display:none\"";
		sb.safePrintf (" <span id=icons%"INT32"%s>" , iconId , v );
		sb.safePrintf("<table cellpadding=0 cellspacing=0 "
			      "style=display:inline-block border=0>"
			      "<tr>"
			      "<td width=15px></td>");
		printIcons2 ( sb , 
			      si , 
			      mr ,
			      iconId ,
			      mr->m_eventHash64 ,
			      mr->m_docId ,
			      mr->m_eventId ,
			      0 , //start_time );
			      st ,
			      &myLikedbFlags );
		if ( myLikedbFlags & LF_REJECT )
			sb.insert ( "<strike>"  , insertionPoint1 );
		sb.safePrintf("</tr></table></span>");
	}
	
	// keep the event title and the thumbs up/down on same line
	//sb.safePrintf("</nobr>");
	
	// ability to hide it
	//sb.safePrintf(" <img "
	//	      "height=16 width=16 src=/spam16.png>");
	if ( ! sb.safePrintf ("<br>\n" ) ) return false;
	//}


	// ALSO MAKE A LIST OF THE EVENTS WHEN EMAILING
	// so we can add them to likedb with the
	// LF_EMAILED_EG flag so we do not re-email them
	// again!!!!
	if ( st->m_msgfb.m_fbId && 
	     si->m_emailFormat ) {
	     //st->m_emailLikedbListBuf ) {
		// . make into a list to add to likedb if we successfully
		//   email them user
		// . this makes two recs, one sorted by docid the other
		//   by userid
		// . taken from Facebook.cpp:1965
		char *recs = g_likedb.makeRecs ( st->m_msgfb.m_fbId ,
						 mr->m_docId ,
						 mr->m_eventId,
						 // ok, now make this zero
						 // so if the event occurs
						 // every day they do not keep
						 // getting it. if they want
						 // to they can 'like' it to
						 // prevent that...
						 //er->m_timeStart,
						 0,
						 LF_EMAILED_EG,
						 mr->m_eventHash64,
						 1 );
		// add to list
		int32_t recSizes = (int32_t)LIKEDB_RECSIZE*2;
		SafeBuf *dst = NULL;
		if ( st->m_emailLikedbListBuf )	dst = st->m_emailLikedbListBuf;
		else                            dst = &st->m_likedbListBuf;
		// store it
		dst->safeMemcpy ( recs, recSizes );
	}


	/*
	if ( ! mr->ptr_turkForm &&
	     // now you must be super turk to see the edit link
	     si->m_turkUser && isSuperTurk ( si->m_turkUser) ) {
		int32_t ah32 = (int32_t)mr->m_eventAddressHash64;
		int32_t dh32 = (int32_t)mr->m_eventDateHash64;
		//int32_t th32 = (int32_t)next->m_eventTitleHash64;
		uint32_t adh32 = hash32h ( ah32 , dh32 );
		// print turk "edit" link
		sb.safePrintf ( " - <a href=\"/eval?evaluser=%s&evalip=%s&"
				"q=gbadch32%%3A%"UINT32""
				"&c=%s\">edit</a>",
				si->m_turkUser,
				iptoa(st->m_socket->m_ip),
				(uint32_t)adh32,coll);
	}
	*/
	bool showDates      = si->m_showDates;
	bool showSummaries  = si->m_showSummaries;
	bool showCountdowns = si->m_showCountdowns;
	bool showAddresses  = si->m_showAddresses;
	if ( ! si->m_widget ) {
		showDates      = true;
		showSummaries  = true;
		showCountdowns = true;
		showAddresses  = true;
	}
	// turn on summaries for igoogle
	if ( si->m_igoogle ) {
		showDates      = true;
		showSummaries  = true;
		showCountdowns = true;
		showAddresses  = true;
	}

	// resort to facebook summary in pageget
	bool isFacebook = (mr->m_eventFlags & EV_FACEBOOK) ;
	if ( si->m_includeCachedCopy && isFacebook )
		showSummaries = false;

	// print the english date
	if ( showDates &&
	     ( si->m_sortBy == SORTBY_DIST || icc ) ) {
		sb.safePrintf("<i>"); // <font color=green>");
		// print a span so they can format it better
		sb.safePrintf("<span class=dates>");
		char tbuf[1024];
		SafeBuf ttt;
		ttt.setBuf ( tbuf , 1024 , 0 , false, csUTF8 );
		if ( ! ttt.safePrintf ("%s",mr->ptr_eventEnglishTime ) )
			return false;
		// do not go crazy!
		int32_t trunc = 75;
		// . then format ttt according to width and store into sb
		// . truncate to 160 chars... do not exceed 160 chars and
		//   print "..." if it gets truncated
		brformat ( &ttt , &sb , width , trunc );// 160 );
		// print a span so they can format it better
		sb.safePrintf("</span>");
		//sb.safePrintf("</font>");
		sb.safePrintf("</i><br>\n");
	}

	if ( mr->m_eventExpired )
		sb.safePrintf("<font color=red style=font-size:14px>"
			      "<b>Event is over."
			      "</b></font><br>\n");

	if ( showCountdowns ) {
		char tbuf[1024];
		SafeBuf ttt;
		ttt.setBuf ( tbuf , 1024 , 0 , false, csUTF8 );
		printEventCountdown( ttt , mr , msg40 , er , true ,false,st );
		// then format into sb
		if ( ! brformat ( &ttt , &sb , width ) ) {
			g_stats.m_numFails++;
			return false;
		}
		if ( ttt.m_length ) 
			sb.safePrintf("<br>\n");
	}

	// . then the summary
	// . "s" is a string of null terminated strings

	if ( ! mr->ptr_turkForm && showSummaries ) {
		int32_t saved = sb.m_length;
		// print a span so they can format it better
		sb.safePrintf("<span class=summary>");
		// print all but subevent brothers
		if ( ! printEventSummary ( sb , mr , width ,
					   EDF_SUBEVENTBROTHER , 0,st,er ,
					   width*5) ) // maxchars
			return false;
		// print a span so they can format it better
		sb.safePrintf("</span>");
		if ( sb.m_length != saved ) sb.safePrintf("<br>\n");
	}

	if ( myLikedbFlags & LF_REJECT )
		sb.safePrintf ( "</strike>" );

	// don't exceed the buffer
	if ( !sb.reserve2x(urlLen + 2000) ) return false;

	if ( showAddresses ) {
		double lat;
		double lon;
		char *addr = mr->ptr_eventAddr;
		// try getting ziplat/lon as well first
		float zipLat;
		float zipLon;
		getZipLatLonFromStr ( addr,&zipLat,&zipLon);

		char tbuf[1024];
		SafeBuf ttt;
		ttt.setBuf ( tbuf , 1024 , 0 , false, csUTF8 );

		// show "6pm - 7pm" if not in summary above
		if ( ! showSummaries && showDates && ! icc ) {
			printTodRange ( ttt , st , er );
			ttt.safePrintf(" - ");
		}

		if ( ! mr->ptr_turkForm )
			sb.safePrintf("<span class=address>");
		printEventAddress ( ttt,addr,si,&lat,&lon,0,zipLat,zipLon,
				    mr->m_eventGeocoderLat,
				    mr->m_eventGeocoderLon,
				    mr->ptr_eventBestPlaceName,
				    mr);
		// then format ttt according to width and store into sb
		brformat ( &ttt , &sb , width );
		if ( ! mr->ptr_turkForm )
			sb.safePrintf("</span>");
		// do not print \0 which is included in aaSize
		//if (!sb.utf8Encode(aa,aaSize-1)) return false;
		//if ( !sb.safePrintf("</font>")) return false;
		//if ( !sb.safePrintf("</b>")) return false;
		sb.safePrintf("<br>\n");
	}

	// show "6pm - 7pm" if not in summary above or address line above
	if ( ! showSummaries && 
	     ! showAddresses &&
	     showDates ) {
		printTodRange ( sb , st , er );
		sb.safePrintf("<br>");
	}

	// . print the content of the requested meta tags
	// . their names are in si->m_displayMetas[] array,
	//   space separated
	printMetaContent(msg40,ix,st,sb,false/*xml?*/);
	// . remove trailing /
	// . only remove from root urls in case user cuts and 
	//   pastes it for link: search
	if ( url [ urlLen - 1 ] == '/' ) {
		// see if any other slash before us
		int32_t j;
		for ( j = urlLen - 2 ; j >= 0 ; j-- )
			if ( url[j] == '/' ) break;
		// if there wasn't, we must have been a root url
		// so hack off the last slash
		if ( j < 0 ) urlLen--;
	}

	// . then a k, space and [cached]
	// . but set the ip/port to a host that has this titleRec
	//   stored locally!
	uint32_t groupId = getGroupIdFromDocId ( mr->m_docId );
	// get the fastest host in group "groupId"
	Host *h1 = g_hostdb.getFastestHostInGroup ( groupId );
	// . use the external ip of our gateway
	// . construct the NAT mapped port
	// . you should have used iptables to map port to the correct
	//   internal ip:port
	uint16_t hport = 80;//h1->m_externalHttpPort;
	uint32_t  hip   = 0;//h1->m_ip;
	// if coming locally, then use local address
	if ( h1 ) {
		hport = h1->m_externalHttpPort;
		hip   = h1->m_ip;
	}
	if ( h1 && st->m_isLocal ) {
		hip   = h1->m_ip;
		hport = h1->m_httpPort;
	}
	// now the last spidered date of the document
	time_t ts = mr->m_lastSpidered;//msg40->getLastSpidered(ix);
	struct tm *timeStruct = localtime ( &ts );
	// do not core on this!!
	int32_t now ;
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();
	// for printing
	int32_t mins = 1000;
	int32_t hrs  = 1000;
	int32_t days ;
	if ( ts > 0 ) {
		mins = (int32_t)((now - ts)/60);
		hrs  = (int32_t)((now - ts)/3600);
		days = (int32_t)((now - ts)/(3600*24));
		if ( mins < 0 ) mins = 0;
		if ( hrs  < 0 ) hrs  = 0;
		if ( days < 0 ) days = 0;
	}
	if ( !cr->m_displayIndexedDate )
		goto skipDisplayIndexedDate;
	if ( cr->m_indexEventsOnly )
		goto skipDisplayIndexedDate;
	// print the time of index
	if      ( mins == 1 )
		sb.safePrintf ( " - indexed: %"INT32" minute ago",mins);
	else if ( mins < 60 ) 
		sb.safePrintf ( " - indexed: %"INT32" minutes ago",mins);
	else if ( hrs == 1 )
		sb.safePrintf ( " - indexed: %"INT32" hour ago",hrs);
	else if ( hrs < 24 )
		sb.safePrintf ( " - indexed: %"INT32" hours ago",hrs);
	else if ( days == 1 )
		sb.safePrintf ( " - indexed: %"INT32" day ago",days);
	else if ( days < 7 )
		sb.safePrintf ( " - indexed: %"INT32" days ago",days);
	// do not show if more than 1 wk old! we want to seem as
	// fresh as possible
	else if ( ts > 0 && si->m_isMasterAdmin && !si->m_isFriend ) {
		char tbuf[100];
		strftime ( tbuf , 100 , " - indexed: %b %d %Y",timeStruct);
		sb.safePrintf ( "%s", tbuf );
	}

 skipDisplayIndexedDate:
	
	sb.safePrintf("\n");

	// this stuff is secret just for local guys!
	if ( si->m_isMasterAdmin ) { // Assassin && !si->m_isFriend ) {
		// now the ip of url
		//int32_t urlip = msg40->getIp(i);
		// don't combine this with the sprintf above cuz
		// iptoa uses a static local buffer like ctime()
		sb.safePrintf(" - <a href=\"/search?"
			      "c=%s&sc=1&dr=0&q=ip:%s&n=100&usecache=0\">%s</a>",
			      coll,iptoa(mr->m_ip), iptoa(mr->m_ip) );
				// ip domain link
		unsigned char *us = (unsigned char *)&mr->m_ip;//urlip;
		sb.safePrintf (" - <a href=\"/search?c=%s&sc=1&dr=0&n=100&"
					       "q=ip:%"INT32".%"INT32".%"INT32"&"
			       "usecache=0\">%"INT32".%"INT32".%"INT32"</a>",
			       coll,
			       (int32_t)us[0],(int32_t)us[1],(int32_t)us[2],
			       (int32_t)us[0],(int32_t)us[1],(int32_t)us[2]);
		
		//if ( si->m_isMasterAdmin && !si->m_isFriend ) {
		// . now the info link
		// . if it's local, don't put the hostname/port in
		//   there cuz it will mess up Global Spec's machine
		//if ( h->m_groupId == g_hostdb.m_groupId ) 
		sb.safePrintf(" - <a href=\"/admin/titledb?c=%s&"
			      "d=%"INT64"",coll,mr->m_docId);
		// then the [info] link to show the TitleRec
		sb.safePrintf ( "\">[info]</a>" );
		
		// now the analyze link
		sb.safePrintf (" - <a href=\"/admin/parser?c=%s&"
			       "old=1&hc=%"INT32"&u=", 
			       coll,
			       (int32_t)mr->m_hopcount);
		
		// encode the url now
		int32_t uelen2 = urlEncode (sb.getBuf(), sb.getAvail(),
					url , urlLen );
		sb.incrementLength(uelen2);
		// then the [analyze] link
		sb.safePrintf ("\">[analyze]</a>" );
		
		//}
		
		// and links: query link
		sb.safePrintf( " - <a href=\"/search?c=%s&dr=0&"
			       "n=100&q=links:",coll);
		// encode the url now
		int32_t uelen = urlEncode ( sb.getBuf() , sb.getAvail(),
					 url , urlLen );
		sb.incrementLength(uelen);
		
		sb.safeMemcpy ("\">linkers</a>" , 14 ); 
	}
	// only display re-spider link if addurl is enabled
	if ( si->m_isAssassin       && 
	     !si->m_isFriend	    &&
	     g_conf.m_addUrlEnabled &&
			     cr->m_addUrlEnabled      ) {
		// the [respider] link
		sb.safePrintf (" - <a href=\"/addurl?u=" );
		// encode the url again
		int32_t uelen = urlEncode ( sb.getBuf() , sb.getAvail(),
					 url , urlLen );
		sb.incrementLength(uelen);
		// then collection
		if ( coll ) {//collLen > 0 ) {
			sb.latin1Encode ( "&c=" , 3 );
			sb.latin1Encode ( coll , gbstrlen(coll) );
		}
		sb.safePrintf ( "&force=1\">[respider]</a>" );
	}
			
	// admin always gets the site: option so he can ban
	if ( si->m_isAssassin && !si->m_isFriend ) {
		char dbuf [ MAX_URL_LEN ];
		int32_t dlen = uu.getDomainLen();
		gbmemcpy ( dbuf , uu.getDomain() , dlen );
		dbuf [ dlen ] = '\0';
		// newspaperarchive urls have no domain
		if ( dlen == 0 ) {
			dlen = uu.getHostLen();
			gbmemcpy ( dbuf , uu.getHost() , dlen );
			dbuf [ dlen ] = '\0';
		}
		sb.safePrintf (" - "
			       " <a href=\"/search?"
			       "q=site%%3A%s&sc=0&c=%s\">"
			       "%s</a> " ,
			       dbuf ,
			       coll , dbuf );
		sb.safePrintf(" - "
				  " <a href=\"/admin/tagdb?"
				  "tagtype0=manualban&"
				  "tagdata0=1&"
				  "u=%s&c=%s\">"
				  "<nobr>[<b>BAN %s</b>]"
				  "</nobr></a> " ,
				  dbuf , coll , dbuf );
		banSites->safePrintf("%s+", dbuf);
		dlen = uu.getHostLen();
		gbmemcpy ( dbuf , uu.getHost() , dlen );
		dbuf [ dlen ] = '\0';
		sb.safePrintf(" - "
				  " <a href=\"/admin/tagdb?"
				  "tagtype0=manualban&"
				  "tagdata0=1&"
				  "u=%s&c=%s\">"
				  "<nobr>[BAN %s]</nobr></a> " ,
				  dbuf , coll , dbuf );
		
		sb.safePrintf (" - [similar -"
				   " <a href=\"/search?"
				   "q="
				   "gbtagvector%%3A%"UINT32""
				   "&sc=1&dr=0&c=%s&n=100"
				   "&rcache=0\">"
				   "tag</a> " ,
				   (int32_t)mr->m_tagVectorHash,  coll);
		sb.safePrintf ("<a href=\"/search?"
				   "q="
				   "gbgigabitvector%%3A%"UINT32""
				   "&sc=1&dr=0&c=%s&n=100"
				   "&rcache=0\">"
				   "topic</a> " ,
				   (int32_t)mr->m_gigabitVectorHash, coll);
		if ( mr->size_gbAdIds > 0 ) 
			sb.safePrintf ("<a href=\"/search?"
					   "q=%s"
					   "&sc=1&dr=0&c=%s&n=200&rat=0\">"
					   "Ad Id</a> " ,
					   mr->ptr_gbAdIds,  coll);

		sb.safePrintf ("] ");
		
		printAdminEventOptions(&sb, msg40, ix, si,
				  coll,pwd, cr, url,urlLen,
				  banSites);

		int32_t urlFilterNum = (int32_t)mr->m_urlFilterNum;
		
		if(urlFilterNum != -1) {
			sb.safePrintf (" - UrlFilter:%"INT32"", 
				       urlFilterNum);
		}					

		
	}
	
	// end - embody the result
	//sb.safePrintf("</span>\n");

	//if ( imgUrl ) sb.safePrintf("</td></tr></table>");

	return true;
}

bool printXmlResult ( SafeBuf &sb , State7 *st, ExpandedResult *er ,
		      int32_t ix ) {

	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;

	char tt[1024*32];
	char *ttend = tt + 1024*32;

	// get the original search result #
	//int32_t i = er->m_mapi;
	
	// if docids only, print the docid only
	if ( si->m_docIdsOnly ) {
		sb.safePrintf ( "\t<result>\n"
				"\t\t<docId>%"INT64"</docId>\n"
				"\t</result>\n",
				msg40->getDocId(ix) );
		return true;
	}
	
	//Msg20Reply *mr = msg40->m_msg20[i]->m_r;
	Msg20      *m20 = msg40->m_msg20[ix];
	Msg20Reply *mr  = m20->m_r;

	// get the request
	//Msg20 *m20 = msg40->m_msg20[i];
	// get the url
	char *url    = mr->ptr_ubuf;
	int32_t  urlLen = mr->size_ubuf - 1;
	int32_t  err    = msg40->m_msg3a.m_errno;//getErrno(i);
	// . remove any session ids from the url
	// . for speed reasons, only check if its a cgi url
	Url uu;
	uu.set ( url , urlLen, false , true );
	// remove the session id if possible
	url    = uu.getUrl();
	urlLen = uu.getUrlLen();
	// if this msg20 had an error print "had error"
	if ( err || urlLen <= 0 || ! url ) {
		log("query: docId %"INT64" had error: %s",
		    mr->m_docId,mstrerror(err));
		return false;
	}
	
	// result
	sb.safePrintf ("\t<result>\n");
	// highlight query terms in the title and store in "p"
	char *s    = mr->ptr_tbuf;
	int32_t  slen = mr->size_tbuf - 1;
	int32_t  hlen = 0;
	// in chars
	int32_t width = si->m_summaryMaxWidth;
	// title, may be empty
	sb.safePrintf ("\t\t<eventTitle><![CDATA[");
	char tbuf[1024];
	SafeBuf ttt;
	ttt.setBuf ( tbuf , 1024 , 0 , false, csUTF8 );
	// use function now
	printEventTitle ( ttt , mr , st );
	// then format ttt according to width and store into sb
	brformat ( &ttt , &sb , width , width );//80 );
	// close title
	sb.safePrintf ( "]]></eventTitle>\n" );
	// so zak can dedup search results
	sb.safePrintf ("\t\t<eventSummaryHash32>"
		       "%"UINT32""
		       "</eventSummaryHash32>\n",
		       (int32_t)mr->m_eventSummaryHash);
	// end tag and begin the summary
	sb.safePrintf ( "\t\t<eventDesc><![CDATA[");
	// then summary excerpts combined together
	s    = mr->ptr_sum     ;
	slen = mr->size_sum - 1;
	char *send = s + slen;
	// print all but subevent brothers
	printEventSummary(sb, mr,9999999, // width
			  EDF_SUBEVENTBROTHER,0,st,er,
			  999999); // maxchars
	// skip stuff below
	s = send;
	
	while ( s && s < send ) {
		// put a ... before each summary excerpt
		// . MTS Modified 04/06/06 for Bug #36:
		//   Extra Ellipsis
		//sb.safePrintf ( "..." );
		Highlight hi;
		if ( si->m_doQueryHighlighting ) {
			hlen = hi.set ( tt , ttend - tt,
					s, slen,
					&si->m_hqq , 
					false  , // doStemming?
					false  , //click&scroll?
					NULL   , // base url
					"<b>"  , // front tag
					"</b>" ); // back tag
			//sb.incrementLength(hlen);
			//if ( !useUtf8 )
			//	sb.latin1Encode(tt, hlen);
			//else
			//	sb.safeLatin1ToUtf8(tt, hlen);
			sb.utf8CdataEncode(tt,hlen);
		}
		else {
			// print out summary excerpt w/o hlght
			//strcpy ( p , s );
			//p += gbstrlen ( p );
			sb.utf8CdataEncode(s,slen);
		}
		// put a .. after each summary excerpt
		// . MTS Modified 04/06/06 for Bug #36:
		//   Extra Ellipsis
		//sb.safePrintf ( ".." );
		// advance to next excerpt in the list, if any
		s += gbstrlen ( s ) + 1;
		// i guess now the summary has no \0's in it
		// to separate the excerpts...
		break;
	}
	// end tag and begin the summary
	sb.safePrintf ( "]]></eventDesc>\n" );
	
	// print sub-event brothers in <eventSchedule> tag if any
	// record start
	int32_t slen1 = sb.m_length;
	// print start
	sb.safePrintf("\t\t<relatedEvents><![CDATA[");
	// record start
	int32_t slen2 = sb.m_length;
	// print only subevent brothers
	printEventSummary ( sb , mr , 9999999 , // width
			    0 , EDF_SUBEVENTBROTHER,st,er,
			    999999 ); // maxChars
	// erase tag if no subevents!
	if ( sb.m_length == slen2 ) sb.m_length = slen1;
	// otherwise, close it
	else sb.safePrintf("]]></relatedEvents>\n");
	
	
	Msg20Reply *next;
	
	// ALL times for event, only need to show one because
	// if they were merged they must be happening at the same
	// times i am assuming...
	Interval *ii = (Interval *)mr->ptr_eventDateIntervals;
	int32_t ni = mr->size_eventDateIntervals/sizeof(Interval);
	// using ctime to display these should work. they
	// are in localtime.
	// XmlDoc::getMsg20Reply limits to first 100
	// intervals starting at the next occuring one,
	// or the one that is happening now i guess.
	sb.safePrintf ( "\t\t<eventDateIntervalsUTC><![CDATA[" );
	for ( int32_t k = 0 ; k < ni ; k++ ) 
		sb.safePrintf("[%"UINT32",%"UINT32"],",
			      ii[k].m_a,
			      ii[k].m_b);
	sb.safePrintf ( "]]></eventDateIntervalsUTC>\n" );
	
	for ( next = mr; next ; next = next->m_nextMerged ) {
		// this is really a countdown
		sb.safePrintf ( "\t\t<eventCountdown><![CDATA[");
		printEventCountdown ( sb,next,msg40,er, false,true,st);
		sb.safePrintf("]]></eventCountdown>\n");
	}
	for ( next = mr; next ; next = next->m_nextMerged ) {
		// cron/english format
		sb.safePrintf ( "\t\t<eventEnglishTime><![CDATA[");
		sb.safePrintf ("%s", next->ptr_eventEnglishTime );
		sb.safePrintf("]]></eventEnglishTime>\n");
	}
	for ( next = mr; next ; next = next->m_nextMerged ) {
		// cron/english format
		sb.safePrintf ( "\t\t<eventEnglishTimeTruncated>"
				"<![CDATA[");
		char tbuf[1024];
		SafeBuf ttt;
		ttt.setBuf ( tbuf , 1024 , 0 , false, csUTF8 );
		ttt.safePrintf ("%s", next->ptr_eventEnglishTime );
		// truncate down to 200 chars, no width though
		brformat ( &ttt , &sb , 9999999 , 200 );
		sb.safePrintf("]]></eventEnglishTimeTruncated>\n");
	}
	
	// tags from document body
	for ( next = mr; next ; next = next->m_nextMerged ) {
		// skip if empty
		if ( next->ptr_eventTagsFromContent ) {
			sb.safePrintf ( "\t\t<eventTagsFromContent>"
					"<![CDATA[");
			char *s = next->ptr_eventTagsFromContent;
			int32_t  n = next->size_eventTagsFromContent-1;
			sb.safeMemcpy(s,n);
			sb.safePrintf("]]></eventTagsFromContent>\n");
		}
	}
	// tags from tagdb
	for ( next = mr; next ; next = next->m_nextMerged ) {
		// skip if empty
		if ( next->ptr_eventTagsFromTagdb ) {
			// these already have the xml tags built in,
			// and even a double tab... and CDATA
			char *s = next->ptr_eventTagsFromTagdb;
			int32_t  n = next->size_eventTagsFromTagdb-1;
			sb.safeMemcpy(s,n);
		}
	}
	
	
	// . print the content of the requested meta tags
	// . their names are in st->m_displayMetas[] array,
	//   space separated
	printMetaContent(msg40, ix, st, sb, true /*xml?*/);
	
	// otherwise print all urls from all merged results
	for ( next=mr; next ; next=next->m_nextMerged)
		sb.safePrintf ("\t\t<url>"
			       "<![CDATA[%s]]></url>\n" , 
			       mr->ptr_ubuf );
	
	// cache url
	for ( next=mr; next ; next=next->m_nextMerged ) {
		sb.safePrintf("\t\t<cachedUrl><![CDATA[");
		// we got a much better format now!!!
		sb.safePrintf ( "/?id=%"UINT64".%"UINT64"\">"
				, mr->m_docId
				, mr->m_eventHash64
				);
		sb.safePrintf("]]></cachedUrl>\n");
	}
	
	// then doc size
	for ( next=mr; next ; next=next->m_nextMerged)
		sb.safePrintf ( "\t\t<size>%4.1f</size>\n",
				(float)next->m_contentLen/1024.0);
	
	// . docId for possible cached link
	// . might have merged a bunch together
	for ( next = mr; next ; next = next->m_nextMerged )
		sb.safePrintf("\t\t<docId>%"INT64"</docId>\n",
			      next->m_docId );
	
	// might have merged a bunch together
	for ( next = mr; next ; next = next->m_nextMerged ) {
		/*
		sb.safePrintf("\t\t<eventHash64>%"UINT64""
			      "</eventHash64>\n",
			      next->m_eventHash64 );
		sb.safePrintf("\t\t<eventDateHash64>%"UINT64""
			      "</eventDateHash64>\n",
			      next->m_eventDateHash64 );
		sb.safePrintf("\t\t<eventTitleHash64>%"UINT64""
			      "</eventTitleHash64>\n",
			      next->m_eventTitleHash64 );
		// gbeventaddressdatetaghash32
		// gbeventaddressdatecontenthash32
		
		// . this must match up with 
		//   getAddressDateContentHash() function in 
		//   Events.h
		// . can do gbeventaddressdatecontenthash32:xxx
		//   query
		int32_t ah32 = (int32_t)next->m_eventAddressHash64;
		//int32_t dh32 = (int32_t)next->m_eventDateHash64;
		int32_t th32 = (int32_t)next->m_eventTitleHash64;
		uint32_t adch32 = next->m_adch32;
		uint32_t adth32 = next->m_adth32;
		sb.safePrintf("\t\t"
			      "<eventAddressDateContentHash32>"
			      "%"UINT32""
			      "</eventAddressDateContentHash32>"
			      "\n",
			      adch32);
		sb.safePrintf("\t\t"
			      "<eventAddressDateTagHash32>"
			      "%"UINT32""
			      "</eventAddressDateTagHash32>"
			      "\n",
			      adth32);
		// addresstitlehash
		uint32_t ath32 = hash32h ( ah32 , th32 );
		sb.safePrintf("\t\t"
			      "<eventAddressTitleContentHash32>"
			      "%"UINT32""
			      "</eventAddressTitleContentHash32>"
			      "\n",
			      (int32_t)ath32);
		*/
	}
	// might have merged a bunch together
	/*
	for ( next = mr; next ; next = next->m_nextMerged )
		sb.safePrintf("\t\t<docIdRelativeEventId>%"INT32""
			      "</docIdRelativeEventId>\n",
			      next->m_eventId);
	*/

	// get this
	int32_t timeZoneOffset = mr->m_timeZoneOffset;
	sb.safePrintf("\t\t<eventTimeZone>%"INT32""
		      "</eventTimeZone>\n",
		      timeZoneOffset);
	sb.safePrintf("\t\t<eventCityUsesDST>%"INT32""
		      "</eventCityUsesDST>\n",
		      (int32_t)mr->m_useDST);
	
	// the simpler thing to replace
	// eventNext/PrevStart/EndUTC
	int32_t displayStart = mr->m_displayStartTime;
	int32_t displayEnd   = mr->m_displayEndTime;
	if ( er ) {
		displayStart = er->m_timeStart;
		displayEnd   = er->m_timeEnd;
	}
	if ( displayStart == -1 ) {
		sb.safePrintf("\t\t<eventStartTimeUTC>"
			      "none"
			      "</eventStartTimeUTC>");
		sb.safePrintf("\t\t<eventEndTimeUTC>"
			      "none"
			      "</eventEndTimeUTC>");
	}
	else {
		sb.safePrintf("\t\t<eventStartTimeUTC>%"UINT32""
			      "</eventStartTimeUTC>\n",
			      displayStart );
		sb.safePrintf("\t\t<eventEndTimeUTC>%"UINT32""
			      "</eventEndTimeUTC>\n",
			      displayEnd );
	}
	
	if ( er ) {
		sb.safePrintf("\t\t<eventStartMonthDay>"
			      "%"INT32"</eventStartMonthDay>\n",
			      (int32_t)er->m_dayNum1);
		sb.safePrintf("\t\t<eventStartMonth>"
			      "%"INT32"</eventStartMonth>\n",
			      // make it 1-12
			      (int32_t)er->m_month1+1);
		sb.safePrintf("\t\t<eventStartYear>"
			      "%"INT32"</eventStartYear>\n",
			      (int32_t)er->m_year1);
	}
	
	// event address
	//sb.safePrintf ( "\t\t<eventAddress><![CDATA[");
	double lat;
	double lon;
	char *addr = mr->ptr_eventAddr;
	
	// show city centroid lat/lon
	double cityLat;
	double cityLon;
	getCityLatLonFromAddrStr ( addr,//ev->m_address ,
				   &cityLat ,
				   &cityLon  );
	
	// try getting ziplat/lon as well first
	float zipLat;
	float zipLon;
	getZipLatLonFromStr ( addr,&zipLat,&zipLon);
	
	// this replaces semicolons with \0's
	printEventAddress ( sb , addr, si , &lat , &lon , 1 ,
			    zipLat, zipLon , 
			    mr->m_eventGeocoderLat,
			    mr->m_eventGeocoderLon,
			    mr->ptr_eventBestPlaceName ,
			    mr );
	
	// address hash, normalized
	//sb.safePrintf("\t\t<eventAddressHash64>%"UINT64""
	//	      "</eventAddressHash64>\n",
	//	      mr->m_eventAddressHash64);
	// show geocoder lat/lon first since it is the best
	if ( mr->m_eventGeocoderLat != NO_LATITUDE &&
	     mr->m_eventGeocoderLon != NO_LONGITUDE ) {
		sb.safePrintf("\t\t<eventGeocoderLat>%.07f"
			      "</eventGeocoderLat>\n",
			      mr->m_eventGeocoderLat);
		sb.safePrintf("\t\t<eventGeocoderLon>%.07f"
			      "</eventGeocoderLon>\n",
			      mr->m_eventGeocoderLon);
	}
	//sb.safePrintf("]]></eventAddress>\n");
	if ( lat != 999.0 && lon != 999.0 ) {
		// unnormalize
		//lat -= 180.0;
		//lon -= 180.0;
		if ( lat > 180.0 ) { char *xx=NULL;*xx=0; }
		if ( lon > 180.0 ) { char *xx=NULL;*xx=0; }
		sb.safePrintf("\t\t<eventScrapedLat>%.07f"
			      "</eventScrapedLat>\n",lat);
		sb.safePrintf("\t\t<eventScrapedLon>%.07f"
			      "</eventScrapedLon>\n",lon);
	}
	if ( zipLat != 999.0 && zipLon != 999.0 ) {
		//lat -= 180.0;
		//lon -= 180.0;
		sb.safePrintf("\t\t<eventZipLat>%.07f"
			      "</eventZipLat>\n",zipLat);
		sb.safePrintf("\t\t<eventZipLon>%.07f"
			      "</eventZipLon>\n",zipLon);
	}
	
	if ( cityLat != 999.0 && cityLon != 999.0 ) {
		sb.safePrintf("\t\t<eventCityLat>%.07f"
			      "</eventCityLat>\n",cityLat);
		sb.safePrintf("\t\t<eventCityLon>%.07f"
			      "</eventCityLon>\n",cityLon);
	}
	
	// make it easier for clients to use google maps
	if ( mr->m_balloonLetter ) {
		sb.safePrintf("\t\t<eventBalloonLetter>%c"
			      "</eventBalloonLetter>\n",
			      mr->m_balloonLetter );
		// and this
		sb.safePrintf("\t\t<eventBalloonLat>%.07f"
			      "</eventBalloonLat>\n",
			      mr->m_balloonLat );
		// and this
		sb.safePrintf("\t\t<eventBalloonLon>%.07f"
			      "</eventBalloonLon>\n",
			      mr->m_balloonLon );
	}
	
	// show the hostname hash (hostId)
	//if ( showSensitiveStuff ) {
	//	int32_t hh = uu.getHostHash32();
	//	sb.safePrintf("\t\t<hostId>%"UINT32"</hostId>\n",hh);
	//}
	
	// . show the site root
	// . for hompages.com/users/fred/mypage.html this will be
	//   homepages.com/users/fred/
	// . for www.xyz.edu/~foo/burp/ this will be
	//   www.xyz.edu/~foo/ etc.
	int32_t  siteLen = 0;
	// use the domain as the default site
	//TagRec *tagRec = (TagRec *)mr->ptr_tagRec;
	//Tag *tag = NULL;
	//if ( tagRec ) tag = tagRec->getTag("site");
	char *site = NULL;
	//if ( tag    ) {
	//  site = tag->m_data;
	//  siteLen = tag->m_dataSize - 1; // include \0
	//}
	// seems like this isn't the way to do it, cuz Tagdb.cpp
	// adds the "site" tag itself and we do not always have it
	// in the XmlDoc::ptr_tagRec... so do it this way:
	site    = mr->ptr_site;
	siteLen = mr->size_site-1;
	//char *site=uu.getSite( &siteLen , si->m_coll, false, tagRec);
	sb.safePrintf("\t\t<site>");
	if ( site && siteLen > 0 ) {
		//if ( g_conf.m_isBuzzLogic ) {
		//	char *uend = site + siteLen;
		//	char *u    = uu.getUrl();
		//	sb.safeMemcpy ( u , uend - u );
		//}
		//else
		sb.safeMemcpy ( site , siteLen );
	}
	sb.safePrintf("</site>\n");
	int32_t sh = hash32 ( site , siteLen );
	sb.safePrintf ("\t\t<siteHash32>%"UINT32"</siteHash32>\n",sh);
	int32_t dh = uu.getDomainHash32 ();
	sb.safePrintf ("\t\t<domainHash32>%"UINT32"</domainHash32>\n",dh);
	
	// spider date
	//if ( ! g_conf.m_isBuzzLogic ) 
	sb.safePrintf ( "\t\t<spiderDate>%"UINT32"</spiderDate>\n",
			mr->m_lastSpidered);
	// backwards compatibility for buzz
	sb.safePrintf ( "\t\t<indexDate>%"UINT32"</indexDate>\n",
			mr->m_firstIndexedDate);
	// . next scheduled spider date
	// . this is now a variable based on the url filters
	//if ( showSensitiveStuff ) // && ! g_conf.m_isBuzzLogic )
	//	sb.safePrintf ( "\t\t<nextSpider>%"UINT32"</nextSpider>\n",
	//			mr->m_nextSpiderTime);
	// last mod date, buzz likes this
	//if ( g_conf.m_isBuzzLogic ) 
	//	sb.safePrintf ( "\t\t<lastMod>%"UINT32"</lastMod>\n",
	//			msg40->getLastModified(i));
	
	// pub date
	int32_t datedbDate = mr->m_datedbDate;
	// Msg16 clear the low bit of the datedb date to indicate
	// it is a "modified" date as opposed to a "published" date.
	// we view pages with "published" dates as permalinks, whereas
	// as index pages are typical of "modified" pages.
	//bool isModDate =((datedbDate & 0x01) == 0x01 );
	// this also indicates a mod date
	//int32_t firstSpidered = mr->m_firstSpidered;
	//if ( datedbDate-24*3600 > firstSpidered ) isModDate = true;
	// show the datedb date as "<pubDate>" for now
	if ( datedbDate != -1 )
		sb.safePrintf ( "\t\t<pubdate>%"UINT32"</pubdate>\n",
				datedbDate);
	
	// doc type iff not html
	char ctype = mr->m_contentType;//msg40->getContentType(i);
	if ( ctype > 2 && ctype <= 13 ) { // not UNKNOWN or CT_TEXT or CT_HTML
		sb.safePrintf( 
			      "\t\t<contentType>%s</contentType>\n",
			      g_contentTypeStrings[(int)ctype]);
	}
	
	if((msg40->getBitScore(ix) & (0x40|0x20)) == 0x00)
		sb.safePrintf("\t\t<notAllQueryTerms>1"
			      "</notAllQueryTerms>\n");
	
	// result
	sb.safePrintf("\t\t<language><![CDATA[%s]]>"
		      "</language>\n", 
		      getLanguageString(mr->m_language));
	
	char *charset = get_charset_str(mr->m_charset);
	if(charset)
		sb.safePrintf("\t\t<charset><![CDATA[%s]]>"
			      "</charset>\n", charset);
	sb.safePrintf ("\t</result>\n\n");
	return true;
}

static char *s_months[] = {
	"Jan",
	"Feb",
	"Mar",
	"Apr",
	"May",
	"Jun",
	"Jul",
	"Aug",
	"Sep",
	"Oct",
	"Nov",
	"Dec"
};
static char *s_dows[] = {
	"Sunday",
	"Monday",
	"Tuesday",
	"Wednesday",
	"Thursday",
	"Friday",
	"Saturday" };

bool printBox ( SafeBuf &sb , 
		State7 *st,
		SafeBuf &msg , 
		SearchInput *si ,
		int32_t colorIndex , // char *gradClass,
		char *setVal ,
		bool forcePrint , // = false ,
		int32_t count ,
		bool printDivStart = true,
		bool printDivEnd   = true,
		bool showx = true ) {

	if ( ! forcePrint  ) {
		if ( si->m_showPersonal ) return true;
		//if ( si->m_showPop      ) return true;
		//if ( si->m_showFriends  ) return true;
	}

	//HttpRequest *hr = &st->m_hr;

	static int32_t s_rbid = 0;

	char *color   = s_gradColors[colorIndex].m_color;
	char *bgcolor = s_gradColors[colorIndex].m_bgcolor;

	// override for now...
	color   = "4"; // white";
	bgcolor = "lightgray";
	char *fontColor = "black";


	char *pl = "";
	if ( si->m_igoogle ) pl = "padding-left:4px;";

	uint32_t shadowColor = 0x505050;
	shadowColor += (uint32_t)(count/2) * 0x101010;

	int32_t padding = 4;
	if ( si->m_igoogle ) padding = 0;
	

	if ( printDivStart ) {
		sb.safePrintf("<center>");
		// . a div in a div to get the margins right
		// . this div is 100% of the parent's width
		// . then we put the margin's on this guy so that the
		//   div inside it does not need margin's and it's
		//   100% width will include margins!! because by default
		//   the margin are tacked onto the 100% width calculation.
		sb.safePrintf("<div "
			      "style=\""
			      // we can't say this!
			      //"width:100%%;"
			      );
		if ( si->m_igoogle ) {
			sb.safePrintf("margin-left:2px;");
			sb.safePrintf("margin-right:16px;");
		}
		else {
			sb.safePrintf("margin-left:0px;");
			sb.safePrintf("margin-right:15px;");
		}
		sb.safePrintf("\">"
			      /*
			      // the shadow div
			      "<div style=\""
			      // these are up top where the gradient is darker
			      // so make the shadows darker
			      "background-color:#%06"XINT32";"
			      "width:100%%;"
			      "border-radius:10px;"
			      //"border:2px solid gray;"
			      "\">"
			      */
			      
			      
			      // rounded corners div mask to fix msie
			      // msie can't handle gradient AND rounded corners
			      // http://stackoverflow.com/questions/4692686/ie9-border-radius-and-background-gradient-bleeding
			      /*
				"<div "
				"style=\""
				"position:relative;"
				"display:inline-block;"
				"width:100%%;"
				"border-radius:10px;"
				"padding:0px;"
				"overflow:hidden;"
				"bottom:6px;"
				"right:6px;"
				"\""
				">"
			      */
			      
			      
			      "<div ");
		// msie can't handle rounded corners AND gradients without
		// using a special div mask. and we lose our drop downs if
		// we use that special div mask!
		//if ( ! hr->isMSIE() )
		//	sb.safePrintf ("class=grad%s ",color);
		sb.safePrintf(
			      "style=\""
			      // cartoon border
			      //"border:2px solid black;"
			      "border:1px solid black;"
			      "box-shadow: 6px 6px 3px %s;" // #%06"XINT32";"
			      "position:relative;"

			      "background-image:url('"
			      "/ss.jpg"
			      "');"
			      "background-repeat:repeat;"
			      //"background-position: 900  200px;"
			      //"background-size:100%% 100%%;"
			      "background-attachment:fixed;"

			      "display:inline-block;"
			      "text-align:left;"
			      "border-radius:10px;"
			      "color:%s;"
			      //, shadowColor
			      , SHADOWCOLOR
			      , fontColor
			      );
		//if ( hr->isMSIE() )
		//	sb.safePrintf ("background-color:%s;", bgcolor);
		if ( si->m_igoogle ) {
			// using 100%% causes an x-scrollbar
			//sb.safePrintf("width:100%%;");
			// stretch it to the margin...
			//sb.safePrintf("left:0px;"
			//	      "right:0px;");
			//sb.safePrintf("margin-left:3px;"
			//	      "margin-right:10px;");
			// igoogle does not have widgetwidth, its dynamic
			//int32_t ww = si->m_widgetWidth - -2-2-2-2-3-10-20;
			//if ( ww < 0 ) ww = 0;
			//sb.safePrintf("width:%"INT32"px;", ww);
			sb.safePrintf("width:100%%;");
			sb.safePrintf("padding-left:2px;");
			sb.safePrintf("padding-right:2px;");
		}
		else {
			//sb.safePrintf("width:100%%;"
			//	      "margin-left:5px;"
			//	      "margin-right:5px;");
			sb.safePrintf("width:100%%;");
			sb.safePrintf("padding:6px;");
		}
		sb.safePrintf ( "\">" );
	}

	sb.safePrintf ( 
		       /*
			 "<table width=100%% "
			 "cellpadding=0 "
			 "cellspacing=0 "
			 "style="
			 "%s"//padding-left:4px;"
			 "padding-right:4px;"
			 "padding-top:6px; "
			 "border=0><tr><td>"
		       */
		       
		       "<table width=100%% cellpadding=0 "
		       "cellspacing=0 "
		       );
	if ( si->m_igoogle )
		sb.safePrintf("style=line-height:18px;");
	// end the table
	sb.safePrintf ( ">" );

	
	int32_t fs = 14;
	if ( si->m_igoogle ) fs = 11;

	sb.safePrintf ( "<tr>"
			"<td width=99%%>"
			"<span style=font-size:%"INT32"px>"
			//, color
			, fs
			);

	// insert just for you bg image
	/*
	if ( useImage ) {
		sb.safePrintf ( "<div style=\""
				"position:absolute;"
				// we can move an absolute div a little from
				// the cursor spot using "margin-top" etc.
				"margin-top:-5px;"
				"margin-left:-7px;"
				"background-color:white;"
				"width:570px;"
				"height:30px;"
				"z-index:1;"
				"background-image:url('/point.png');"
				"background-repeat:repeat;"
				"opacity:0.20;"
				//"background-attachment:fixed;"
				"background-position: 0  -58px;"
				"background-size:400px 250px;"
				"\""
				">"
				"</div>"
				);
	}
	*/

	sb.safePrintf ( "%s" 
			"</span>"
			"</td>"
			, msg.getBufStart()
			);

	if ( showx ) { // ! si->m_showPersonal || count == 0 ) {
		sb.safePrintf("<td valign=top"
			      //"bgcolor=\"%s\""
			      ">"
			      "<a "
			      "onmouseover=\"document.getElementBy"
			      "Id('id_redbox%"INT32"').src='/xon.png';\" "
			      "onmouseout=\"document.getElementBy"
			      "Id('id_redbox%"INT32"').src='/xoff.png';\" "
			      "onclick=\"" 
			      //, bgcolor
			      , s_rbid
			      , s_rbid
			      );
		// sanity check - cmd = "setVal('radius','0');"
		if ( setVal[0] && setVal[gbstrlen(setVal)-1] != ';' ) { 
			char *xx=NULL;*xx=0; }
		sb.safePrintf ( "%s"
				"reloadResults();"
				"\">"
				, setVal );
		sb.safePrintf ( "<img "
				"src=/xoff.png "
				"width=11 "
				"height=11 "
				"id=id_redbox%"INT32">"
				"</a>"
				//"&nbsp;"
				"</td>"
				, s_rbid
				);
	}

	sb.safePrintf ( "</tr></table>" );

	// the "email events to me" printout line below will set count to -1
	if ( printDivEnd ) // ! si->m_showPersonal || count == -1 )
		sb.safePrintf("</div>"
			      "</div>"
			      "</center>");


	// recycle
	if ( ++s_rbid >= 20 ) s_rbid = 0;

	// oh, this prints the user's picture i guess, so it could be empt
	if ( msg.length() <= 0 ) return true; // { char *xx=NULL;*xx=0; }

	if ( si->m_igoogle ) 
		sb.safePrintf("<span style=line-height:5px><br></span>" );
	else
		sb.safePrintf("<span style=line-height:10px><br></span>" );

	return true;
}

// *hasLocation is set to false if its 'anywhere'
bool printDisplayWhere ( SafeBuf &sb , SearchInput *si , bool *hasLocation ) {
	bool alreadyPrinted = false;

	char *where = si->m_where;
	if ( si->m_showPersonal ) where = si->m_myLocation;
	if ( where && ! where[0] ) where = NULL;

	*hasLocation = true;
	// first print location on top, unless "anywhere"
	if ( where && ! strcmp ( where , "anywhere" ) )
		*hasLocation = false;

	if ( ! where )
		*hasLocation = false;

	// try to print location bar
	if ( where &&
	     strcmp(where,"anywhere") &&
	     ( si->m_cityDesc || si->m_stateDesc || si->m_countryDesc )) {
		bool printedSomething = false;
		// print city
		PlaceDesc *pd = si->m_cityDesc;
		if ( pd ) {
			sb.safePrintf("%s" , pd->getOfficialName() );
			printedSomething = true;
		}
		// print state
		pd = si->m_stateDesc;
		if ( ! pd ) pd = si->m_cityDesc;
		char *name = NULL;
		if ( pd ) name = pd->getStateName();
		if ( name ) {
			if ( printedSomething ) sb.safePrintf(", ");
			sb.safePrintf("%s" , name );//pd->getStateName() );
			printedSomething = true;
		}
		// print country
		pd = si->m_countryDesc;
		if ( ! pd ) pd = si->m_stateDesc;
		if ( ! pd ) pd = si->m_cityDesc;
		if ( pd ) {
			if ( printedSomething ) sb.safePrintf(", ");
			sb.safePrintf("%s" , pd->getCountryName() );
			printedSomething = true;
		}
		alreadyPrinted = true;
	}
	float lat = si->m_userLat;
	float lon = si->m_userLon;
	if ( lat > 180 && si->m_zipLat <= 180.0 ) {
		lat = si->m_zipLat;
		lon = si->m_zipLon;
	}
	// first print location on top, unless "anywhere"
	if ( where && 
	     !strcmp(where,"autolocate") &&
	    *hasLocation &&
	     ! alreadyPrinted &&
	     // extra constraints
	     //si->m_origWhere && 
	     // this means "eventguru.com/NM/Albuquerque"
	     //! whereBasedOnUrl &&
	     //(!si->m_origWhere[0] || !strcmp(si->m_origWhere,"default")) ) {
	     lat <= 180.0 ) {
		// mark it 
		alreadyPrinted = true;
		// if original where box was empty that means we guessed
		// the location from user's ip address or user's provided
		// gps... so get the nearest city and print that
		// print the closest city
		bool printed = false;
		float distInMilesSquared ;
		PlaceDesc *pd;
		pd = getNearestCity_new ( lat ,lon,0, &distInMilesSquared);
		if ( distInMilesSquared < 1000 ) {
			char *cityName = pd->getOfficialName();
			char *stateName = pd->getStateName();
			char *countryName = (char *)pd->getCountryName();
			// sometims "stateName" is null, so do it this way
			if ( cityName ) {
				printed = true;
				sb.safePrintf("%s",cityName);
			}
			if ( stateName ) {
				if ( printed ) sb.safePrintf(", ");
				printed = true;
				sb.safePrintf("%s",stateName);
			}
			if ( countryName ) {
				if ( printed ) sb.safePrintf(", ");
				printed = true;
				sb.safePrintf("%s",countryName);
			}
			//sb.safePrintf("%s, %s, %s",
			//	       pd->getOfficialName(),
			//	       pd->getStateName(),
			//	       pd->getCountryName());
			//printed = true;
		}
		// print lat/lon if could not print city
		if ( ! printed )
			sb.safePrintf ( "%s", where );
	}

	if ( where && 
	     //! si->m_showPersonal &&
	     //strcmp(where,"anywhere") &&
	     ! alreadyPrinted ) {
		// extra constraints
		//si->m_origWhere &&
		//si->m_origWhere[0] ) {
		alreadyPrinted = true;
		sb.safePrintf ( "%s", where ); // si->m_origWhere );
	}

	if ( ! where ) {
		sb.safePrintf("anywhere");
	}


	return true;
}

// we now print up to 3 redboxes, the top being the location,
// the 2nd being the query, the 3rd being the normal redbox.
// we do not print anything except "Just for You" for just for you
// results...
bool printRedBoxes ( SafeBuf &sb , State7 *st ) {

	SearchInput *si = &st->m_si;
	Msg40 *msg40 = &(st->m_msg40);
	int64_t fbId = st->m_msgfb.m_fbId;
	bool firstCat;
	SafeBuf cmd;

	st->m_printedBox = false;

	// the redbox is above the search results and let's the searcher know
	// that their search is being constrained to topics, etc
	//SafeBuf redBox;

	char mbuf[512];
	SafeBuf msg(mbuf,512);


	// if selected a show friends or show my events thing and 
	// we are not yet done downloading from facebook...
	// then show this box
	bool showStillDownloadingWarning = false;

	if ( si->m_showFriends || 
	     si->m_showMyStuff ||
	     si->m_friendsGoingTo ||
	     si->m_friendsLike ||
	     si->m_friendsInvited ||
	     si->m_iAmGoingTo ||
	     si->m_iLike ||
	     si->m_iAmInvited ) 
		showStillDownloadingWarning = true;

	// check to see if it is still downloading
	//if(showStillDownloadingWarning && ! isStillDownloading ( fbId,0 ) ) 
	FBRec *fbrec = st->m_msgfb.m_fbrecPtr;
	if ( showStillDownloadingWarning && 
	     fbrec && ! (fbrec->m_flags & FB_INQUEUE) )
		showStillDownloadingWarning = false;

	// if not logged in, nothing to show!
	if ( ! fbrec ) showStillDownloadingWarning = false;

	int32_t count = 0;

	if ( showStillDownloadingWarning ) {
		msg.reset();
		msg.safePrintf("<table cellspacing=0 border=0><tr><td>"
			       "<img src=%s "
			       "width=%"INT32" height=%"INT32" "
			       "style=padding-right:10px;>"
			       , SITTING
			       , SITTINGDX64
			       , SITTINGDY64
			       );
		msg.safePrintf(
			       "</td>"
			       "<td>"
			       "<font size=-1>"
			       "The results may be missing some events "
			       "because Event Guru is still "
			       "downloading your event information from "
			       "Facebook. Try reloading in a bit." 
			       "</font>"
			       "</td></tr></table>"
			       );
		printBox(sb,st,msg,si,COLOR_BRIGHT_RED,
			 "setVal('showfriends','0');"
			 "setVal('showmystuff','0');",
			 0,count++);
	}

	// show the event guru's eyes in the bg for this box
	if ( si->m_showPersonal ) {
		msg.reset();
		msg.reserve(1);
		// in case we print nothing into there!
		msg.getBufStart()[0] = '\0';
		// print the user's picture
		if ( fbId ) {
			int32_t dim = 50;
			if ( si->m_igoogle ) dim = 20;
			msg.safePrintf("<img " );
			if ( si->m_igoogle )
				msg.safePrintf("style=padding-top:3px;"
					       "padding-left:3px; "
					       );
			msg.safePrintf("width=%"INT32" height=%"INT32" align=left "
				       "src=http://graph.facebook.com/"
				       "%"INT64"/picture>"
				       "&nbsp;"
				       , dim
				       , dim
				       , fbId );
		}
		//else
		//	msg.safePrintf("");
		/*
		msg.safePrintf(
			       //"<img width=22 height=22 align=left "
			       //"src=/point.png>"
			       "&nbsp; <b>"
			       "Recommendations</b>"
			       );
		if ( ! si->m_igoogle )
			msg.safePrintf ( 
					" - &nbsp; "
					"<font size=-1>"
					"Events based on "
					"your interests. "
					//" &nbsp; ["
					//"<a onclick="
					//"\"setVal('showpersonal','0');"
					//"reloadResults();\">"
					//"close this window</a>]"
					"</font>"
					);
		*/
		printBox(sb,st,msg,si,COLOR_FLESH,
			 "setVal('showpersonal','0');"
			 //"toggleOff('suggestions');"
			 ,1,count++,true,false,false);
	}

	int32_t iwid;
	int32_t color;
	int32_t lh = 14;
	if ( si->m_igoogle ) lh = 11;

	/////////////
	//
	// LOCATION BOX
	//
	/////////////
	char *based = NULL;
	if      ( si->m_whereBoxBasedOnIP  ) based = "from IP Address";
	else if ( si->m_whereBoxBasedOnGPS ) based = "from GPS location";
	else if ( si->m_zipLat < 180.0  ) based = "from your zipcode";
	if ( si->m_whereBoxBasedOnUrl ) based = "from the url";
	//if ( si->m_showPersonal ) based = NULL;
	// print start of redbox
	msg.reset();
	//if ( si->m_showPersonal )
	//	msg.safePrintf("<img width=22 height=22 align=left "
	//		       "src=/point.png>&nbsp;" );
	msg.safePrintf("<div style=line-height:7px;><br></div>" );
	// special widget title
	if ( si->m_widget && si->m_igoogle ) {
		char *wt = "Search Events";
		if ( si->m_showPersonal ) wt = "Recommended Events";
		msg.safePrintf("<b>%s</b>",wt);
		msg.safePrintf("<div style=line-height:7px;><br></div>" );
	}
	msg.safePrintf("<font " );
	if ( based ) msg.safePrintf("title=\"%s\" ",based);
	msg.safePrintf("color=%s>Location: </font>" , GRAYSUBTEXT );
	// keep it as a string until user clicks on it then turn it
	// into this input box...
	msg.safePrintf("<a id=tloc onclick=\""
		       "var w = getElementById('wherebox');"
		       "w.style.display='';"
		       "w.focus();"
		       "this.style.display='none';"
		       "\"><b>"
		       );
	// this is false if its 'anywhere' otherwise its true
	bool hasLocation;
	// print "(user location)" if based on user location
	char *where2 = si->m_where;
	if ( si->m_showPersonal ) where2 = si->m_myLocation;
	// it's obvious it's autolocate because we print out
	// "from gps" or "from ip"...
	//if ( where2 && where2[0] && !strcmp(where2,"autolocate") )
	//	msg.safePrintf("(auto locate) ");
	printDisplayWhere ( msg , si , &hasLocation );
	msg.safePrintf("</b>");
	msg.safePrintf("<font style=font-size:11px;>");
	if ( based ) msg.safePrintf(" - %s",based);
	msg.safePrintf(" - <u>edit</u></font>");
	msg.safePrintf("</a>" );
	// make this a drop down that appears when input box gets focus
	msg.safePrintf(
		       // gotta keep this div next to the input box
		       "<nobr>"
		       "<div "
		       "id=locdrop "
		       "style=\""
		       //"display:inline-block;"
		       "display:none;"
		       "z-index:913;"
		       "position:relative;"
		       "\""
		       ">"

		       "<div "
		       "style=\""
		       //"display:inline-block;"
		       "position:absolute;"
		       "padding:5px;"
		       "border-left:2px solid black;"
		       "border-right:2px solid gray;"
		       "border-bottom:2px solid gray;"
		       //"background-color:#eeffee;"
		       "background-color:white;"
		       "opacity:.95;"
		       "overflow-x:hidden;"
		       "top:4px;"
		       "left:7px;"
		       "z-index:915;"
		       //"width:400px;"
		       //"height:300px;"
		       "\""
		       ">"
		       );
	printLocationHistory ( msg , si );
	msg.safePrintf("</div>"
		       "</div>"
		       );
	// start input element
	iwid = 350;
	if ( si->m_igoogle ) iwid=160;
	char *lname = "where";
	if ( si->m_showPersonal ) lname = "mylocation";
	msg.safePrintf("<input "
		       "autocomplete=off "
		       "name=wherebox id=wherebox "
		       "onfocus=\"document.getElementById('locdrop').style."
		       "display='inline-block';"
		       // highlight the text in case its "anywhere"
		       "this.select();"
		       "\" "

		       // the drop down location history menu
		       // now sets the highlighted query to the global
		       // variable gloc. it is null if not set. so if it
		       // is set we have to process it here since it will
		       // not get the onmouseclick event because this
		       // onblur is processed first and hides the dropdown
		       // menu and it does not get the mouse click.
		       "onblur=\"document.getElementById('locdrop')."
		       "style.display='none'; if ( ! gloc ) return;"
		       // location is "autolocate", ask for gps again...
		       "if ( gloc == 'autolocate' ) "
		       "document.cookie = 'noreload=0;';"
		       // set the value and reload!
		       "setVal('%s',gloc);reloadResults();\" "

		       // . if they push enter key submit it
		       // . for some reason chrome needs this but firefox
		       //   does not! firefox calls the <form onsubmit> 
		       //   function...
		       "onkeypress=\"if(event.keyCode!=13) return;"
		       "setVal('%s',this.value);reloadResults();\" "


		       "style=\""
		       "vertical-align:text-bottom;"
		       "width:%"INT32"px;"
		       //"width:100%%;"
		       "height:18px;"
		       "padding:0px;"
		       "display:none;" // start off invisible
		       //"font-weight:bold;"
		       //"border-radius:10px;"
		       "margin:0px;"
		       "border:1px inset lightgray;"
		       //"background-color:#eeffee;"
		       "background-color:white;"
		       "font-size:%"INT32"px;"
		       "\" "

		       "value=\""
		       , lname
		       , lname
		       , iwid 
		       , lh 
		       );
	// print it out again
	printDisplayWhere ( msg , si , &hasLocation );
	// wrap it up, the location cell
	msg.safePrintf("\">"
		       // for keeping the dropdown div next to the input box
		       "</nobr>");
	msg.safePrintf ( //"</nobr>"
			 "</td>" );
	color = COLOR_GREEN;
	if ( si->m_showPersonal ) color = COLOR_FLESH;
	bool printDivEnd = false;//true;
	bool printDivStart = true;
	bool showx = false;//true;
	// allow the distance slider to be in the same div box with us...
	if ( hasLocation ) printDivEnd = false;
	if ( si->m_showPersonal ) {
		printDivEnd = false;
		printDivStart = false;
		showx = false;
	}
	// igoogle does not print the email options...
	//if ( si->m_showPersonal && si->m_igoogle && ! hasLocation ) 
	//	printDivEnd = true;
	cmd.safePrintf("setVal('%s','anywhere');",lname);
	printBox ( sb,st,msg,si,color,cmd.getBufStart(),1,count++,
		   printDivStart,printDivEnd,showx );




	/////////////
	//
	// DISTANCE SLIDER
	//
	/////////////
	msg.reset();
	// print distance in box
	//if ( si->m_radius != 30 && 
	//     si->m_radius > 0 ) {
	// this table is within the green box and holds the slider inputs
	msg.safePrintf("<table width=100%% cellpadding=0 cellspacing=0 "
		       "border=0><tr><td>");
	msg.safePrintf("<nobr>");
	//if ( si->m_showPersonal )
	//	msg.safePrintf("<img width=22 height=22 align=left "
	//		       "src=/point.png>&nbsp;" );
	msg.safePrintf("<font color=%s>Within: </font>",GRAYSUBTEXT);
	char *name = "radius";
	char *bgcolor = "#ffffff";
	if ( si->m_showPersonal ) {
		name = "myradius";
		//bgcolor = "#f1d5b5";
	}
	//float radius = si->m_radius;
	//if ( si->m_showPersonal ) radius = si->m_myRadius;
	msg.safePrintf("<input type=text style=\""
		       "vertical-align:top;width:35px;height:18px;"
		       "vertical-align:text-bottom;"
		       "padding:0px;"
		       "border:1px inset lightgray;"
		       "background-color:%s;"
		       "font-size:10px;"
		       "\" "
		       // . if they push enter key submit it
		       // . for some reason chrome needs this but 
		       //   firefox does not! firefox calls the 
		       // <form onsubmit> function...
		       "onkeypress=\""
		       "if(event.keyCode==13) {"
		       // update appropriate guy as well!!!
		       "document.getElementById('%s').value = this.value;"
		       "reloadResults();"
		       "}\" "
		       "id=radiusbox name=radiusbox value=\"\"> mi"
		       "</nobr></td>"
		       , bgcolor
		       , name
		       );
	// . print slider out
	// . the slider is a div within the box div
	msg.safePrintf("<td width=10px>&nbsp;&nbsp;</td>"
		       "<td width=100%% valign=center id=distbartd>\n"
		       "<div id=distbar "
		       "class=hand "
		       "style=\""
		       "border:1px inset lightgray;"
		       "border-radius:10px;"
		       "position:relative;"
		       //"position:absolute;"
		       "width:100%%;"
		       "height:18px;"
		       "font-size:10px;"
		       //"valign:center;"
		       "background-color:%s;"
		       "\" "

		       // 
		       // onmousedown pick up the ptr
		       //
		       "onmousedown=\""
		       "var mx = mouseX(event);"
		       // move arrow to that (ICON_WIDTH)
		       "setArrowPos(mx);"
		       "drag=1;"

		       // disable text selection
		       // MSIE and chrome:
		       //"if (typeof document.onselectstart!='undefined')"
		       //"document.onselectstart=function(){return false;};"
		       // firefox:
		       //"alert(document.MozUserSelect);"
		       //"if (typeof document.MozUserSelect!="
		       //"'undefined'){"
		       //"document.MozUserSelect='none';"
		       //"}"
		       "\" "

		       "onmousemove=\""
		       "if ( drag != 1 ) return;"
		       "var mx = mouseX(event);"
		       "setArrowPos(mx);" // ICON_WIDTH
		       "\" "

		       "onmouseup=\""
		       "drag=0;"
		       // save m_radius
		       //"saveCookies();"
		       "reloadResults();"
		       // re-enable text selection
		       // MSIE:
		       //"if (typeof document.onselectstart!='undefined')"
		       //"document.onselectstart=null;"
		       // firefox:
		       //"else if (typeof document.MozUserSelect!="
		       //"'undefined') "
		       //"document.MozUserSelect=null;"
		       "\" "

		       /*
		       //
		       // onclick set the x-position of the arrow div
		       //
		       "onclick=\""
		       // this was window.event for chrome
		       "var mx = mouseX(event);"
		       "setArrowPos(mx);"
		       "\""
		       //"alert('mx='+mx);"
		       //"document.getElementById('arrow').style.left = mx;"
		       // make this store the x offset to avoid recomputes
		       //
		       // end onclick
		       //
		       */

		       ">"
		       // the bar is just an hr
		       //"<hr>"

		       "<div id=arrow "
		       "style=\""
		       // we turn it on after we call setArrowPos() in our
		       // body onLoad() function! we have to make sure we
		       // set it in tune with si->m_radius
		       "display:none;"//inline-block;"
		       "position:absolute;"//relative;"
		       //"left:20px;"//%"INT32"px;"
		       "width:5px;"
		       "height:18px;"
		       "valign:center;"
		       "opacity:0.60;"
		       //"z-index:200;"
		       //"background-color:red;"
		       //"background-image:url('/favicon.ico');"
		       //"background-size:20px 20px;"
		       //"background-repeat:no-repeat;"
		       "background-color:red;"
		       "\" "
		       , bgcolor
		       );
	msg.safePrintf(// prevent dragging the image in firefox
		       "onmousedown=\"if ( event.preventDefault) "
		       "event.preventDefault();\" "
		       ">"
		       "</div>"

		       // print the ruler
		       "<nobr>"

		       "<span onmousedown=\"if ( event.preventDefault) "
		       "event.preventDefault();\" "
		       "style=width:25%%;text-align:right;"
		       "display:inline-block;>10</span>"

		       "<span onmousedown=\"if ( event.preventDefault) "
		       "event.preventDefault();\" "
		       "style=width:25%%;text-align:right;"
		       "display:inline-block;>100</span>"

		       "<span onmousedown=\"if ( event.preventDefault) "
		       "event.preventDefault();\" "
		       "style=width:25%%;text-align:right;"
		       "display:inline-block;>1k</span>"

		       "<span onmousedown=\"if ( event.preventDefault) "
		       "event.preventDefault();\" "
		       "style=width:25%%;text-align:right;"
		       "display:inline-block;>10k</span>"

		       "</nobr>"
		       //"10 &nbsp; "
		       // the arrow div is overlaid on the hr
		       "</div>"
		       "</td>"
		       "<td id=nexttd width=10px>&nbsp;&nbsp;</td>"
		       "</td></tr></table>"
		       );
	if ( hasLocation ) {
		color = COLOR_GREEN;
		if ( si->m_showPersonal ) color = COLOR_FLESH;
		printDivStart = false;
		printDivEnd = false;//true;
		// igoogle does not print the email options...
		if ( si->m_showPersonal && ! si->m_igoogle ) 
			printDivEnd = false;
		printBox ( sb,st,msg,si,color,
			   //"setVal('radius','30.0');"
			   ""
			   , 1 // force print!
			   , count++
			   , printDivStart
			   , printDivEnd
			   , false // show X?
			   );
	}
	///////////////
	//
	// END print distance slider
	//
	///////////////



	//////////
	//
	// SEARCHING FOR...
	//
	/////////
	char *dquery = NULL;
	if ( si->m_displayQuery && si->m_displayQuery[0] )
		dquery = si->m_displayQuery;
	if ( ! dquery )
		dquery = "anything";
	msg.reset();
	char *sf = "Searching for";
	iwid = 240;
	if ( si->m_igoogle ) {
		iwid=150;
		sf = "Search";
	}
	//if ( si->m_showPersonal )
	//	msg.safePrintf("<img width=22 height=22 align=left "
	//		       "src=/point.png>&nbsp;" );
	msg.safePrintf ( //"<nobr>"
			 "<font color=%s>%s: </font>"
			 , GRAYSUBTEXT
			 , sf );
	/*
	// keep it as a string until user clicks on it then turn it
	// into this input box...
	msg.safePrintf(
		       "<a id=tqry onclick=\""
		       "var q = getElementById('q');"
		       "q.style.display='';"
		       "q.focus();"
		       "this.style.display='none';"
		       "\"><b>"
		       "%s"
		       "</b>"
		       "<font style=font-size:11px;>"
		       " - <u>edit</u></font>"
		       "</a>"
		       , dquery
		       );
	*/
	//
	// begin query drop box, qdrop
	//
	msg.safePrintf(
		       // gotta keep this div next to the input box
		       "<nobr>"
		       "<div "
		       "id=qdrop "
		       "style=\""
		       "display:none;"
		       "z-index:913;"
		       "position:relative;"
		       "\""
		       ">"

		       "<div "
		       "style=\""
		       "position:absolute;"
		       "padding:5px;"
		       "border-left:2px solid black;"
		       "border-right:2px solid gray;"
		       "border-bottom:2px solid gray;"
		       //"background-color:#eeffee;"
		       "background-color:white;"
		       "opacity:.95;"
		       "overflow-x:hidden;"
		       "top:4px;"
		       "left:7px;"
		       "z-index:915;"
		       "\""
		       ">"
		       );
	printQueryHistory ( msg , si );
	msg.safePrintf("</div>"
		       "</div>"
		       );

	SafeBuf enc_dquery;
	enc_dquery.htmlEncode(dquery,gbstrlen(dquery),false);
	//
	// end query drop box
	//
	msg.safePrintf ( "<input type=text "
			 "style=\""
			 "vertical-align:text-bottom;"
			 //"width:250px;"
			 //"width:100%%;"
			 "width:%"INT32"px;"
			 "height:18px;"
			 "padding:0px;"
			 "font-weight:bold;"
			 "padding-left:5px;"
			 //"border-radius:10px;"
			 "margin:0px;"
			 // not anymore! too many ppl can't find it!!!!!!
			 //"display:none;" // start off invisible
			 "border:1px inset lightgray;"
			 "background-color:#ffffff;"
			 "font-size:%"INT32"px;"
			 "\" "


			 "autocomplete=off "

			 "onmouseup=\"if ( gpoo==1 ) {"
			 "gpoo=0;this.select();return false;}\" "

			 "onfocus=\""
			 // highlight the text in case its "anything"
			 "focus();"
			 "this.select();"
			 "document.getElementById('qdrop').style."
			 "display='inline-block';"
			 "gpoo=1;"
			 "\" "
			 
			 // the drop down query history menu
			 // now sets the highlighted query to the global
			 // variable gqry. it is null if not set. so if it
			 // is set we have to process it here since it will
			 // not get the onmouseclick event because this
			 // onblur is processed first and hides the dropdown
			 // menu and it does not get the mouse click.
			 "onblur=\"document.getElementById('qdrop')."
			 "style.display='none'; if ( ! gqry ) return;"
			 "setVal('q',gqry);reloadResults();\" "



			 // . if they push enter key submit it
			 // . for some reason chrome needs this but firefox
			 //   does not! firefox calls the <form onsubmit> 
			 //   function...
			 "onkeypress=\"if(event.keyCode!=13) return;"
			 "setVal('q',this.value);reloadResults();\" "

			 "id=q name=q value=\"%s\">"
			 //"</nobr>"
			 , iwid
			 , lh
			 , enc_dquery.getBufStart()
			 );
	if ( ! si->m_igoogle )
		msg.safePrintf(" <sup><a title=help style=font-size:10px; "
			       "href=/help.html>[?]</a></sup>");
	int32_t fs = 13;
	if ( si->m_igoogle ) fs = 11;
	msg.safePrintf("</td>");
	if ( ! si->m_igoogle ) {
		char *scolor = "red";
		if ( si->m_showPersonal ) scolor = "flesh";
		msg.safePrintf ("<td style=padding-right:8px; "
				//"class=grad%s "
				"align=right>"
				"<nobr style=font-size:%"INT32"px>"
				"<font color=%s>Sort by: </font>"
				//, scolor 
				, fs 
				, GRAYSUBTEXT
				);
		msg.safePrintf("<input type=hidden id=sortbydistance "
			       "name=sortbydistance value=%"INT32">",
			       (int32_t)si->m_sortEventsByDist);
		msg.safePrintf("<input type=hidden id=sortbydate "
			       "name=sortbydate value=%"INT32">",
			       (int32_t)si->m_sortEventsByDate);
		char *da = "Distance";
		if  ( si->m_igoogle ) da = "Dist";
		if ( si->m_sortEventsByDate )
			msg.safePrintf("<b>Time</b>"
				       " | "
				       "<a onclick=\""
				       "setVal('sortbydistance','1');"
				       "setVal('sortbydate','0');"
				       "reloadResults();"
				       "\">"
				       "%s</a>"
				       , da);
		else if ( si->m_sortEventsByDist )
			msg.safePrintf("<a onclick=\""
				       "setVal('sortbydistance','0');"
				       "setVal('sortbydate','1');"
				       "reloadResults();"
				       "\">"
				       "Time</a>"
				       " | "
				       "<b><font color=red>%s</font></b>"
				       , da );
		msg.safePrintf("</nobr>"
			       "</td>");
	}
	color = COLOR_BLUE; // RED;
	if ( si->m_showPersonal ) color = COLOR_FLESH;
	// to avoid too much confusion, take this out for recommendations now
	if ( ! si->m_showPersonal )
		printBox (sb,st,msg ,si, color , "setVal('q','');" ,
			  1,
			  count++,
			  false, // printdivstart
			  false , // printdivend
			  false ); // print X
	// things get screwy because saveCookie() expects a "q" input parm
	else sb.safePrintf("<input type=hidden name=q id=q value=\"%s\">", 
			   enc_dquery.getBufStart() );



	///////////////
	//
	// CATEGORIES
	//
	///////////////
	firstCat = true;
	msg.reset();
	cmd.reset();
	msg.safePrintf ( "<a "
			 "onclick=\""
			 "var e = document.getElementById('cattbl');"
			 "var a = document.getElementById('catarrow');"
			 "if ( e.style.display=='none' ) {"
			 "e.style.display='';"
			 "setVal('categories',1);"
			 "a.src='/down.png';"
			 "}"
			 "else {"
			 "e.style.display='none';"
			 "setVal('categories',0);"
			 "a.src='/up.png';"
			 "}"
			 "\">"
			 );
	msg.safePrintf("<font color=%s>Categories: </font><b>",GRAYSUBTEXT);
	for ( int32_t i = 0 ; i < g_parms.m_numSearchParms ; i++ ) {
		Parm *m = g_parms.m_searchParms[i];
		if ( m->m_subMenu != SUBMENU_CATEGORIES )
		     //m->m_subMenu != SUBMENU_SOCIAL ) 
			continue;
		if ( m->m_type != TYPE_BOOL ) continue;
		//if ( ! printMenuJunk ) break;
		char *x = ((char *)si) + m->m_soff;
		if ( ! *x ) continue;
		if ( ! firstCat ) msg.safePrintf(", ");
		msg.safePrintf ( "%c%s" , 
				 to_upper_a(m->m_title[0]),
				 m->m_title + 1 );
		cmd.safePrintf("setVal('%s','0');",m->m_scgi);
		firstCat = false;
	}
	msg.safePrintf("</b>");
	char *src = "/up.png";
	if ( si->m_categories ) src = "/down.png";
	if ( firstCat ) {
		// this will unhide the list of categories so you can
		// select them...
		if ( si->m_igoogle )
			msg.safePrintf("Click here");
		else
			msg.safePrintf("Click here");
	}
	msg.safePrintf(" <img id=catarrow src=%s>" ,src);
	msg.safePrintf("</a>");
	// print the category div with checkboxes.
	// make it 3 columns by N rows
	printCategoryTable ( msg , si );
	//cmd.safePrintf("toggleOff('categories');");
	cmd.safePrintf("setVal('categories',0);");
	// need to pass in the SetVal() cmds ourselves here?
	printBox (sb,st,msg,si,COLOR_BLUE,cmd.getBufStart(),0,count++,
		  false, // printdivstart?
		  true , // printdivend?
		  false  // print X?
		  );


	////////////
	//
	// EMAIL ALERTS
	//
	////////////
	if ( si->m_showPersonal && ! si->m_igoogle ) {
		msg.reset();
		// set "checked" on the appropriate radio button
		char *chk[6];
		for ( int32_t i = 0 ; i < 6 ; i++ ) {
			chk[i] = "";
			if ( i == si->m_emailFreq ) chk[i] = " checked";
		}
		if ( fbId )
			msg.safePrintf(
				       "<font color=%s>"
				       "Email events to "
				       "me:</font> &nbsp; "
				       
				       "<input type=radio class=ef "
				       "onclick=\""
				       "setVal('emailfreq',1);"
				       "reloadResults();"
				       "\"%s> "
				       "Daily &nbsp; "
				       
				       "<input type=radio class=ef "
				       "onclick=\""
				       "setVal('emailfreq',2);"
				       "reloadResults();"
				       "\"%s> "
				       "Weekly &nbsp; "

				       "<input type=radio class=ef "
				       "onclick=\""
				       "setVal('emailfreq',3);"
				       "reloadResults();"
				       "\"%s> "
				       "Monthly &nbsp; "

				       "<input type=radio class=ef "
				       "onclick=\""
				       "setVal('emailfreq',4);"
				       "reloadResults();"
				       "\"%s> "
				       "Quarterly &nbsp; "
				       
				       "<input type=radio class=ef "
				       "onclick=\""
				       "setVal('emailfreq',5);"
				       "reloadResults();"
				       "\"%s> "
				       "Never"
				       ,GRAYSUBTEXT
				       ,chk[1]
				       ,chk[2]
				       ,chk[3]
				       ,chk[4]
				       ,chk[5]
				       );
		else
			msg.safePrintf(
				       "<font color=%s>"
				       "Email events to "
				       "me:</font> &nbsp; "
				       
				       "<input type=radio class=ef "
				       "onclick=\"needLogin();return false;"
				       "\"> "
				       "Daily &nbsp; "
				       
				       "<input type=radio class=ef "
				       "onclick=\"needLogin();return false;"
				       "\"> "
				       "Weekly &nbsp; "

				       "<input type=radio class=ef "
				       "onclick=\"needLogin();return false;"
				       "\"> "
				       "Monthly &nbsp; "

				       "<input type=radio class=ef "
				       "onclick=\"needLogin();return false;"
				       "\"> "
				       "Quarterly &nbsp; "
				       
				       "<input type=radio class=ef "
				       "onclick=\"needLogin();return false;"
				       "\"> "
				       "Never"
				       ,GRAYSUBTEXT
				       );
		// use -1 to tell it to end the div!
		printBox(sb,st,msg,si,COLOR_FLESH,"",1,count++,
			 false, // printdivstart?
			 false, // printdivend?
			 false); // showx?
	}

	////////////////
	//
	// INTERESTS
	//
	////////////////
	if ( si->m_showPersonal ) {
		msg.reset();
		cmd.reset();
		//msg.safePrintf("<img width=22 height=22 align=left "
		//	       "src=/point.png>&nbsp;" );
		msg.safePrintf ( // this determines whether the interests table
				 // is hidden or not
				 "<input type=hidden name=suggestions "
				 "id=suggestions value=%"INT32">"
				 // this link displays the interests or hides them
				 "<a " // style=font-size:14px; "
				 "onclick=\""
				 "var e = document.getElementById('intbuf');"
				 "var a = document.getElementById('intarrow');"
				 "if ( e.style.display=='none' ) {"
				 "e.style.display='';"
				 "a.src='/down.png';"
				 "setVal('suggestions',1);"
				 "}"
				 "else {"
				 "e.style.display='none';"
				 "a.src='/up.png';"
				 "setVal('suggestions',0);"
				 "}"
				 "\">"
				 , si->m_suggestions
				 );
		cmd.safePrintf("setVal('suggestions',0);");
		if ( si->m_numCheckedInterests > 0 || ! si->m_widget )
			msg.safePrintf("<font color=#404040>Interests: "
				       "</font>");
		if ( si->m_numCheckedInterests <= 0 ) {
			msg.safePrintf("<font style=background-color:"
				       "yellow;>");
			// keep it int16_t for igoogle and interactive widgets
			msg.safePrintf("Select interests");
			msg.safePrintf("</font>");
		}
		else 
			msg.safePrintf("Click here ");

		//if ( si->m_igoogle )
		//	msg.safePrintf(" to show ");
		//else
		//	msg.safePrintf(" to show your interests or add"
		//		       " new ones ");
		char *src = "/up.png";
		if ( si->m_suggestions ) src = "/down.png";
		if ( si->m_numCheckedInterests <= 0 ) src = "/down.png";
		msg.safePrintf("<img id=intarrow src=%s>" ,src);
		msg.safePrintf("</a>");
		printMyInterests ( msg , st , false );
		printBox ( sb,st,msg , si , COLOR_FLESH, cmd.getBufStart(),
			   1,count++,
			   false, // printdivstart?
			   true, // printdivend?
			   false); // print X?
	}




	if ( si->m_firstResultNum ) {
		msg.reset();
		msg.safePrintf ( "<font color=%s>Showing results: </font>"
				 "<b>%"INT32" to %"INT32"</b></td>"
				 , GRAYSUBTEXT
				, si->m_firstResultNum+1
				, si->m_firstResultNum+1+25
				 );
		color = COLOR_PURPLE;
		//if ( si->m_showPersonal ) color = COLOR_FLESH;
		printBox ( sb,st,msg,si , color ,"setVal('s','');",1,count++);
	}

		
	// the calendar time if not based on current time
	if ( si->m_clockSet ) {
		int32_t clockSet = si->m_clockSet;
		// get timezone offset
		int32_t timeZoneOffset = msg40->m_timeZoneOffset;
		if ( timeZoneOffset == UNKNOWN_TIMEZONE ) 
			timeZoneOffset = si->m_guessedTimeZone;
		if ( timeZoneOffset == UNKNOWN_TIMEZONE )
			timeZoneOffset = -5;
		clockSet += timeZoneOffset * 3600;
		struct tm *timeStruct = gmtime ( &clockSet );
		int32_t clockDay   = timeStruct->tm_mday;
		int32_t clockMonth = timeStruct->tm_mon;
		msg.reset();
		msg.safePrintf("<font color=%s>Starting after: </font>"
			       "<b>%s %"INT32" 12am</b>"
			       ,GRAYSUBTEXT
			       ,s_months[clockMonth]
			       ,clockDay);
		color = COLOR_ORANGE;
		//if ( si->m_showPersonal ) color = COLOR_FLESH;
		printBox ( sb,st,msg,si,color,
			   "setVal('clockset','0');toggleOff('calendar');",1,
			   count++);
	}

	// show the event guru's eyes in the bg for this box
	if ( si->m_showFriends ) {
		msg.reset();
		msg.safePrintf(
			       "<img width=22 height=22 align=left "
			       "src=/friends32.png>&nbsp;"
			       "<b>Friends Events</b> - &nbsp; <font size=-1>"
			       "Events your friends like, are going to or are "
			       "invited</font>");
		printBox(sb,st,
			 msg,si,COLOR_YELLOW,"setVal('showfriends','0');",0,
			 count++);
	}


	// show the event guru's eyes in the bg for this box
	if ( si->m_showMyStuff ) {
		msg.reset();
		msg.safePrintf(
			       "<img width=22 height=22 align=left "
			       "src=http://graph.facebook.com/%"INT64"/picture>"
			       "&nbsp;"
			       "<b>My Events</b> - &nbsp; <font size=-1>"
			       "Events you like, are going to or are "
			       "invited</font>"
			       , fbId );
		printBox(sb,st,
			 msg,si,COLOR_YELLOW,"setVal('showmystuff','0');",0,
			 count++);
	}

	//
	// SOCIAL SUBMENU box
	//
	firstCat = true;
	msg.reset();
	cmd.reset();
	msg.safePrintf("<font color=%s>Social: </font><b>",GRAYSUBTEXT);
	for ( int32_t i = 0 ; i < g_parms.m_numSearchParms ; i++ ) {
		Parm *m = g_parms.m_searchParms[i];
		if ( m->m_subMenu != SUBMENU_SOCIAL ) continue;
		if ( m->m_type != TYPE_BOOL ) continue;
		char *x = ((char *)si) + m->m_soff;
		if ( ! *x ) continue;
		if ( ! firstCat ) msg.safePrintf(", ");
		msg.safePrintf ( "%c%s" , 
				 to_upper_a(m->m_title[0]),
				 m->m_title + 1 );
		cmd.safePrintf("setVal('%s','0');",m->m_scgi);
		firstCat = false;
	}
	if ( ! firstCat ) {
		msg.safePrintf("</b>");
		cmd.safePrintf("toggleOff('socialmenu');");
		printBox ( sb,st,msg,si,COLOR_YELLOW,cmd.getBufStart(),
			   true, // force print?
			   count++);
	}


	// experimental results?
	if ( si->m_experimentalResults ) {
		msg.safePrintf("experimental");
	}

	msg.reset();
	// time constraints
	bool allEmpty = false;
	if ( ! si->m_daily &&
	     ! si->m_weekly &&
	     ! si->m_monthly &&
	     ! si->m_infrequently ) 
		allEmpty = true;
	if ( ! allEmpty && 
	     ( ! si->m_daily ||
	       ! si->m_weekly ||
	       ! si->m_monthly ||
	       ! si->m_infrequently ) ) {
		msg.safePrintf ("<font color=%s>Frequency: </font><b>",
				GRAYSUBTEXT);
		int32_t count = 0;
		if ( si->m_daily ) count++;
		if ( si->m_weekly ) count++;
		if ( si->m_monthly ) count++;
		if ( si->m_infrequently ) count++;
		int32_t printed = 0;
		if ( si->m_daily ) {
			printed++; 
			msg.safePrintf(" Daily");
		}
		if ( si->m_weekly ) {
			if ( printed ) msg.safePrintf(" or ");
			printed++;
			msg.safePrintf(" Weekly" );
		}
		if ( si->m_monthly ) {
			if ( printed ) msg.safePrintf(" or ");
			printed++;
			msg.safePrintf(" Monthly" );
		}
		if ( si->m_infrequently ) {
			if ( printed ) msg.safePrintf(" or ");
			printed++;
			msg.safePrintf(" Infrequently");
		}
		msg.safePrintf("</b>");
	}
	if ( msg.length() ) {
		color = COLOR_GREEN; // PURPLE;
		//if ( si->m_showPersonal ) color = COLOR_FLESH;
		printBox ( sb,st,msg,si,COLOR_PURPLE,
			   "setVal('daily','1');"
			   "setVal('weekly','1');"
			   "setVal('monthly','1');"
			   "setVal('infrequently','1');"
			   "toggleOff('timemenu');"
			   , 1, count++
			   );
	}	

	// if they turned off show in progress, note that, its not default
	if ( si->m_showInProgress ) {
		msg.reset();
		msg.safePrintf("<font color=%s>Frequency: </font><b>",
			       GRAYSUBTEXT);
		msg.safePrintf("Showing events in progress</b>");
		color = COLOR_PURPLE;
		//if ( si->m_showPersonal ) color = COLOR_FLESH;
		printDivStart = true;
		printDivEnd = true;
		printBox ( sb,st,msg,si,COLOR_PURPLE,
			   "setVal('inprogress','0');"
			   "toggleOff('timemenu');"
			   , 1, count++
			   , printDivStart
			   , printDivEnd
			   );
	}

	if ( si->m_showWidget && ! si->m_widget ) {
		msg.reset();
		msg.safePrintf("<img src=/gears32.png width=22 height=22 "
			       "align=left>&nbsp;"
			       "<b>Showing Widget</b>");
		printBox (sb,st,
			  msg,si,COLOR_BLUE,"setVal('showwidget','0');"
			  "setVal('widgetmenu','0');",1,count++);
	}

	if ( si->m_showNearUser &&
	     si->m_showWidget &&
	     ! si->m_widget ) {
		msg.reset();
		msg.safePrintf("<img src=/gears32.png width=22 height=22 "
			       "align=left>&nbsp;"
			       "<b>Showing Events Near User</b>");
		printBox (sb,st,msg,si,COLOR_BLUE,
			  "setVal('shownearuser','0');",1,count++);
	}

	//if ( si->m_showPop ) {
	//	msg.reset();
	//	msg.safePrintf("<b>Showing Popular Interests</b>");
	//	printBox (sb,st,msg,si,COLOR_FLESH,"setVal('showpop','0');",1,
	//		  count++);
	//}

	/*
	if ( ! redBox.m_length ) return true;

	sb.safePrintf ( "<table width=%s cellpadding=3 "
			"bgcolor=lightblue "
			"style=\"background-color:lightblue;"
			"border:2px solid blue;\""
			">"
			"<tr>"
			"<td class=gradblue width=99%%>%s</td>"
			
			"<td>"
			"<a "

			"onmouseover=\"document.getElementBy"
			"Id('id_redbox0').src='/xon.png';\" "

			"onmouseout=\"document.getElementBy"
			"Id('id_redbox0').src='/xoff.png';\" "

			"onclick=\"" 
			, RESULTSWIDTHSTR
			, redBox.getBufStart() );

	int32_t np = g_parms.m_numSearchParms;
	if ( si->m_showPop ) {
		np = 0;
		sb.safePrintf("window.location.href = '/';\">");
	}
	
	// loop over all redbox parms and print defaults if
	// not default already
	for ( int32_t i = 0 ; i < np ; i++ ) {
		Parm *m = g_parms.m_searchParms[i];
		if ( ! ( m->m_flags & PF_REDBOX ) ) continue;
		char *x = ((char *)si) + m->m_soff;
		char *def = m->m_def;
		if ( m->m_type == TYPE_BOOL && atol(def) == *x ) 
			continue;
		if ( m->m_type == TYPE_LONG && atol(def)==*(int32_t *)x) 
			continue;
		if ( m->m_type == TYPE_STRING && !strcmp(def,x) )
			continue;
		// and corresponding checkbox
		if ( m->m_type == TYPE_BOOL && def[0]=='0' )
			sb.safePrintf("document.getElementBy"
				      "Id('cb_%s')."
				      "checked=false;",m->m_scgi);
		if ( m->m_type == TYPE_BOOL && def[0]=='1' )
			sb.safePrintf("document.getElementBy"
				      "Id('cb_%s')."
				      "checked=true;",m->m_scgi);
		
		sb.safePrintf("setVal('%s','%s');",m->m_scgi,m->m_def);
		
	}
	// finally submit the form
	//sb.safePrintf("resetCal();reloadResults();\">");
	if ( ! si->m_showPop ) sb.safePrintf("reloadResults();\">");
	
	
	
	
	// all parms go to default, except display parms like
	// icons,attendees,likers,widgetwidth,widgetheight,widgetfont,
	// display,map,calendar,social,time,... etc. PF_DISPLAY
	//sb.safePrintf( "%s", si->m_redBoxParms.getBufStart() );
	// go back to sorting by time if we were doing distance
	//if ( si->m_sortBy != 1 ) sb.safePrintf("sortby=1&");
	// go back to 0 radius, which means none basically
	//if ( si->m_radius < 100 && si->m_radius > 0 ) 
	//	sb.safePrintf("radius=0&");
	// then print the rest
	sb.safePrintf( //"%s&redbox=1>"
		      //"&#x2715;"

		      "<img "
		      "src=/xoff.png "
		      "width=11 "
		      "height=11 "
		      "id=id_redbox0>"

		      "</a>"
		      "&nbsp;"
		      "</td>"
		      "</tr></table><br>" );
*/

	return true;
}

bool printDrivingDirectionsLink(SafeBuf &sb, SearchInput *si, Msg20Reply *mr){
	// get user lat/lon
	float slat = si->m_userLat;
	float slon = si->m_userLon;
	if ( slat == NO_LATITUDE ) {
		slat = si->m_ipLat;
		slon = si->m_ipLon;
	}
	if ( slat == NO_LATITUDE ) {
		slat = si->m_cityLat;
		slon = si->m_cityLon;
	}
	// destination
	float dlat = mr->m_eventGeocoderLat;
	float dlon = mr->m_eventGeocoderLon;
	sb.safePrintf("<a href=\"http://maps.google.com/maps?saddr=%.05f,%.05f&daddr=%.05f,%.05f&hl=en\">"
		      //sll=0,0&sspn=148.13559,11.25&geocode=FcpYFwIdAKKk-Q%3BFXITIgIdzvBP-w&mra=ls&glp=1&t=m&z=3\">"
		      , slat
		      , slon 
		      , dlat
		      , dlon
		      );
	return true;
}
	
bool printAllResults ( SafeBuf &sb , State7 *st , Query &qq ) {

	SearchInput *si = &st->m_si;
	Msg40 *msg40 = &(st->m_msg40);

	// if we did an icc=1 link below, print the cached page, and that's it
	bool printMenuJunk = true;
	if ( si->m_includeCachedCopy ) printMenuJunk = false;
	if ( si->m_raw               ) printMenuJunk = false;
	if ( si->m_widget            ) printMenuJunk = false;
	if ( si->m_emailFormat       ) printMenuJunk = false;

	// grab the query
	char  *q    = msg40->getQuery();
	int32_t   qlen = msg40->getQueryLen();
	// secret search backdoor "3bY6u2Z"
	if ( qlen == 7 && q[0]=='3' && q[1]=='b' && q[2]=='Y' &&
	     q[3]=='6' && q[4]=='u' && q[5]=='2' && q[6]=='Z' ) {
		sb.safePrintf ( "<br><b>You owe me!</b><br><br>" );
	}

	// print it with commas into "thbuf" and null terminate it
	char thbuf[64];
	// numResults may be more than we requested now!
	int32_t n = msg40->getDocsWanted();
	int32_t numResults = msg40->getNumResults();
	if ( n > numResults )  n = numResults;
	// an estimate of the # of total hits
	int64_t totalHits = msg40->getNumTotalHits();
	// only adjust upwards for first page now so it doesn't keep chaning
	if ( totalHits < n ) totalHits = n;
	ulltoa ( thbuf , totalHits );

	// is it the main collection?
	bool isMain = false;
	if ( si->m_collLen2 == 4 && strncmp ( si->m_coll2, "main", 4) == 0 ) 
		isMain = true;
	// print "in collection ***" if we had a collection
	if ( si->m_collLen2 >0 && ! isMain && si->m_isMasterAdmin && printMenuJunk) {
		sb.safePrintf (" in collection '<b>");
		sb.safeMemcpy ( si->m_coll2 , si->m_collLen2 );
		sb.safeMemcpy ( "</b>'" , 5 );
	}

	char *pwd = si->m_pwd;
	if ( ! pwd ) pwd = "";

	if ( printMenuJunk )
		sb.safePrintf("<span style=line-height:12px><br></span>");
	else if ( si->m_igoogle )
		sb.safePrintf("<span style=line-height:1px><br></span>");

	//
	// print style tag for iGoogle display (&ig=1)
	//
	if ( si->m_igoogle && si->m_raw == 0 )
		sb.safePrintf(
			      "<style>"
			      "html {"
			      "font-size:12px;"
			      "font-family:helvectica,arial;"
			      "background-color:transparent;"//ightblue;"
			      "color:black;"
			      "}"
			      "span.dayheader { "
			      "font-size:14px;"
			      "font-weight:bold;"
			      "}"
			      "span.title { "
			      "font-size:13px;"
			      //"font-weight:bold;"
			      "}"
			      "a { "
			      "text-decoration:none;"
			      "}"
			      "span.countdown { "
			      "font-size:12px;"
			      "color:red;"
			      "}"
			      "span.summary { "
			      "font-size:12px;"
			      "}"
			      "span.address { "
			      "font-size:12px;"
			      "color:purple;"
			      "}"
			      "span.times { "
			      "font-size:12px;"
			      "color:green;"
			      "}"
			      "span.dates { "
			      "font-size:12px;"
			      "color:green;"
			      "}"
			      "span.prevnext { "
			      "font-size:12px;"
			      "font-weight:bold;"
			      "}"


			      ".grad1{"
			      // msie 6-9
			      "filter: progid:DXImageTransform.Microsoft.gradient( startColorstr='%s', endColorstr='%s',GradientType=0 );"
			      // gecko based browsers
			      "background:-moz-linear-gradient(top, %s,%s);"
			      // webkit based browsers
			      "background:-webkit-gradient(linear, left top, left bottom, from(%s), to(%s));"
			      // msie10+
			      "background:-ms-linear-gradient(top, %s 0%%,%s 100%%);"
			      "}\n"


			      //"a {cursor:hand;cursor:pointer;}"
			      "</style>"


			      ,"#f0f0f0"
			      ,"#d0d0d0"
			      ,"#f0f0f0"
			      ,"#d0d0d0"
			      ,"#f0f0f0"
			      ,"#d0d0d0"
			      ,"#f0f0f0"
			      ,"#d0d0d0"

			      );
	// make widget display simple
	else if ( si->m_widgetHeader && si->m_raw == 0 && si->m_widget )
		sb.safePrintf("%s", si->m_widgetHeader);

	// did we get a spelling recommendation?
	if ( st->m_spell[0] && printMenuJunk ) {
		// make the anti-bot key
		//g_httpServer.getKey (&key,kname,NULL,0,getTime(),0,10);
		// encode the spelling recommendation
		int32_t len = gbstrlen ( st->m_spell );
		char qe2[MAX_FRAG_SIZE];
		urlEncode(qe2, MAX_FRAG_SIZE, st->m_spell, len);
		// temporarily chop off the q= from "uc"
		//char c = *umid;
		//*umid = '\0';
		sb.safePrintf ("<font size=+0 color=\"#c62939\">Did you mean:"
			       "</font> <font size=+0>"
			       "<a href=\"/?%s&q=%s",
			       si->m_urlParms , qe2 );
		// close it up
		sb.safePrintf ("\"><i><b>");
		sb.utf8Encode(st->m_spell, len);
		// then finish it off
		sb.safePrintf ("</b></i></a></font>\n<br><br>\n");
	}

	// . Wrap results in a table if we are using ads. Easier to display.
	Ads *ads = &st->m_ads;
	if ( ads->hasAds() && printMenuJunk )
                sb.safePrintf("<table width=\"100%%\">\n"
                              "<tr><td style=\"vertical-align:top;\">\n");

	// that calls https://graph.facebook.com/oauth/access_token?
	// client_id=YOUR_APP_ID&redirect_uri=YOUR_URL&
	// client_secret=YOUR_APP_SECRET&code=THE_CODE_FROM_ABOVE
	// to get the access token.
	// once we got the access token we can do this:
	// https://graph.facebook.com/me?access_token=ACCESS_TOKEN 
	// seems like we are calling both these from a gk144 machine
	// so the request will go out roadrunner, which is bad cuz its
	// slow... nah just route ro router-rr if its to the scproxy2...
	// then all else route through lobonet...
	// . http://developers.facebook.com/docs/authentication/
	// . THEN store the user data in tagdb? fbuserid is the key?
	//   AND do a massive UOR query of your friend's fbids to see
	//   what your friends liked. should be massively page cached.
	//   consider caching the termlists themselves, but uncaching
	//   a particular fbids termlist if that person likes or unlikes
	//   something. OR maybe just make page cache page size lower
	//   for indexdb. like 1k for indexdb when doing events. or
	//   better yet, if we do not have a tagdb rec for your friend
	//   then do not bother even including his id in the UOR!
	//   and use not tagdb but facebookdb like we have turkdb. and each
	//   host should have a special hash table of the fbids it does
	//   contain in facebookdb. it could load that up at startup and
	//   maintain it when a new add comes in in Rdb.cpp for rdbid=FACEBOOK
	// . THEN i guess searchinput needs to lookup your friend's fbids
	//   on in facebookdb, then for each one of those it needs to
	//   do a msg request to see which are in facebookdb so it can
	//   create the UOR query of your friends facebookids


	// debug
	//if ( si->m_debug )
	//	logf(LOG_DEBUG,"query: Printing up to %"INT32" results. "
	//	     "Docs wanted=%"INT32". bufStart=0x%"XINT32"", 
	//	     numResults,count,(int32_t)sb.getBuf());


	// print Recommendations / Search Events
	if ( printMenuJunk )
		printTabs ( sb , st );

	//
	// BEGIN PRINT RED BOXES
	//
	if ( printMenuJunk || ( si->m_igoogle && ! si->m_includeCachedCopy)) {
		printRedBoxes ( sb , st );
		//sb.safePrintf("<br>");
	}
	// print LOCATION and DISTANCE!
	else if ( si->m_emailFormat ) {
		int64_t fbId = st->m_msgfb.m_fbId;
		// printbox
		sb.safePrintf("<div "
			      "style=\""
			      // cartoon border
			      //"border:2px solid black;"
			      "border:1px solid black;"
			      "box-shadow: 6px 6px 3px %s;"
			      "position:relative;"
			      "display:inline-block;"
			      "margin-left:5px;"
			      "margin-right:5px;"
			      "text-align:left;"
			      "border-radius:10px;"
			      //"background-color:white;"
			      "background-image:url('/ss.jpg');"
			      "background-repeat:repeat;"
			      "background-attachment:fixed;"
			      //"width:98%%;"
			      "padding:10px;"
			      "font-size:14px;"
			      "line-height:20px;"
			      "\">"
			      , SHADOWCOLOR
			      );
		sb.safePrintf("<table cellpadding=4 cellspacing=0 border=0 "
			      "style=font-size:14px;width:700px;>"
			      "<tr><td rowspan=9>" );
		// facebook image
		sb.safePrintf("<img " 
			       "width=50 height=50 align=left "
			       "style=\""
			      //"margin-left:5px;"
			      //"margin-top:3px;"
			      // "margin-bottom:3px;"
			      "margin-right:20px;"
			      "box-shadow: 3px 3px 2px #606060;"
			       //"border:2px solid black;"
			       "\" "
			       "src=http://graph.facebook.com/"
			       "%"INT64"/picture>"
			      "</td><td align=right>"
			       , fbId );
		sb.safePrintf("<b><nobr>Your Interests</nobr>"
			      "</b></td>"
			      "<td width=100%%>"
			      "<font color=red>");
		// list a few interests from m_intestPtrs
		int32_t printed = 0;
		int32_t *offsets = (int32_t *)si->m_intOffsets.getBufStart();
		for ( int32_t i = 0 ; i < si->m_numInterests ; i++ ) {
			char *p ;
			p = si->m_intBuf.getBufStart() + offsets[i];
			if ( p[0] != '1' && p[0] != '4' ) continue;
			char *topic = p + 2;
			char *end = topic; for ( ; *end && *end !=';'; end++);
			if ( printed++ ) sb.safePrintf(", ");
			if ( printed >= 4 ) {
				sb.safePrintf("... ");
				break;
			}
			sb.safeMemcpy ( topic , end - topic );
		}
		sb.safePrintf("</font>"
			      " - <a href=%s?showpersonal=1&suggestions=1&"
			      "ei=%"UINT64"&"
			      "usefbid=%"INT64"&fh=%"UINT32"#edit>"
			      "<font size=-1>edit</font></a>"
			      "</td></tr>"
			      , APPHOSTUNENCODED 
			      , fbId
			      , fbId
			      // this is like the password
			      , hash32 ( (char *)&fbId , 8, 0 )
			      );
		sb.safePrintf("<tr><td align=right>"
			      "<b>Location"
			      "</b></td><td>"
			      "<font color=green>");
		bool hasLocation;
		printDisplayWhere ( sb , si , &hasLocation );
		// link to the page. close interests subwindow so we can
		// see the location
		sb.safePrintf("</font> - <a href=%s?showpersonal=1&"
			      "suggestions=0&"
			      "ei=%"UINT64"&"
			      "usefbid=%"INT64"&"
			      "fh=%"UINT32"&"
			      // highlight "Location:" in YELLOW!! (&hi=)
			      "hi=location>"
			      , APPHOSTUNENCODED 
			      , fbId
			      , fbId
			      // this is like the password
			      , hash32 ( (char *)&fbId , 8, 0 )
			      );
		sb.safePrintf("<font size=-1>edit</font></a>"
			      "</tr></tr>"
			       );
		if ( hasLocation ) {
			float radius = si->m_radius;
			if ( si->m_showPersonal ) radius = si->m_myRadius;
			sb.safePrintf("<tr><td align=right>"
				      "<b>Within"
				      "</b></td><td>"
				      "<font color=green>"
				      "%.0f miles"
				      "</font>"
				      " - "
				      "<a href=%s?showpersonal=1&"
				      "ei=%"UINT64"&"
				      "suggestions=0&usefbid=%"INT64"&fh=%"UINT32">"
				      "<font size=-1>"
				      "edit"
				      "</font>"
				      "</a>"
				      "</td></tr>"
				      , radius 
				      , APPHOSTUNENCODED 
				      , fbId 
				      , fbId 
				      // this is like the password
				      , hash32 ( (char *)&fbId , 8, 0 )
				      );
		}
		//
		// email frequency in email format
		// 
		// default email frequency is weekly i think
		char *fs = "weekly";
		// unsubscribe link for emails
		if      ( si->m_emailFreq == 1 ) fs = "Daily";
		else if ( si->m_emailFreq == 2 ) fs = "Weekly";
		else if ( si->m_emailFreq == 3 ) fs = "Monthly";
		else if ( si->m_emailFreq == 4 ) fs = "Quarterly";
		else if ( si->m_emailFreq == 5 ) fs = "Never";
		sb.safePrintf("<tr><td align=right>"
			      "<b>Email"
			      "</b></td>"
			      "<td><font color=brown>%s</font> ", fs );
		sb.safePrintf(" &nbsp; <font size=-1>(change to ");
		bool printed2 = false;
		// i don't want to use the set-cookie stuff
		// really, so pass this in. it should save
		// the fbrec with these new values.
		if ( si->m_emailFreq != 1 ) {
			sb.safePrintf("<i><a "
				      "style=color:black; "
				      "href=\"%s?"
				      "emailfreq=1&"
				      "showpersonal=1&"
				      "ei=%"UINT64"&"
				      "usefbid=%"UINT64"&"
				      "fh=%"UINT32""
				      "\">"
				      "daily"
				      "</a></i>"
				      , APPHOSTUNENCODED 
				      , st->m_msgfb.m_fbId 
				      , st->m_msgfb.m_fbId 
				      // this is like the password
				      , hash32 ( (char *)&fbId , 8, 0 )
				      );
			printed2 = true;
		}
		if ( si->m_emailFreq != 2 ) {
			if ( printed2 ) sb.safePrintf(", ");
			sb.safePrintf("<i><a "
				      "style=color:black; "
				      "href=\"%s?"
				      "emailfreq=2&"
				      "showpersonal=1&"
				      "ei=%"UINT64"&"
				      "usefbid=%"UINT64"&"
				      "&fh=%"UINT32""
				      "\">"
				      "weekly"
				      "</a></i>"
				      , APPHOSTUNENCODED 
				      , st->m_msgfb.m_fbId 
				      , st->m_msgfb.m_fbId 
				      // this is like the password
				      , hash32 ( (char *)&fbId , 8, 0 )
				      );
		}
		if ( si->m_emailFreq != 3 ) {
			sb.safePrintf(", ");
			sb.safePrintf("<i><a "
				      "style=color:black; "
				      "href=\"%s?"
				      "emailfreq=3&"
				      "showpersonal=1&"
				      "ei=%"UINT64"&"
				      "usefbid=%"UINT64"&"
				      "fh=%"UINT32""
				      "\">"
				      "monthly"
				      "</a></i>"
				      , APPHOSTUNENCODED 
				      , st->m_msgfb.m_fbId 
				      , st->m_msgfb.m_fbId 
				      // this is like the password
				      , hash32 ( (char *)&fbId , 8, 0 )
				      );
		}
		if ( si->m_emailFreq != 4 ) {
			sb.safePrintf(", ");
			sb.safePrintf("<i>"
				      "<a "
				      "style=color:black; "
				      "href=\"%s?"
				      "emailfreq=4&"
				      "showpersonal=1&"
				      "ei=%"UINT64"&"
				      "usefbid=%"UINT64"&"
				      "fh=%"UINT32""
				      "\">"
				      "quarterly"
				      "</a></i>" 
				      , APPHOSTUNENCODED 
				      , st->m_msgfb.m_fbId 
				      , st->m_msgfb.m_fbId 
				      // this is like the password
				      , hash32 ( (char *)&fbId , 8, 0 )
				      );
		}
		sb.safePrintf(", ");
		sb.safePrintf("or <i><a "
			      "style=color:black; "
			      "href=\"%s?unsubscribe=1&"
			      "emailfreq=5&"
			      "showpersonal=1&"
			      "ei=%"UINT64"&"
			      "usefbid=%"UINT64"&"
			      "fh=%"UINT32""
			      "\">"
			      "unsubscribe"
			      "</a></i>" 
			      , APPHOSTUNENCODED 
			      , st->m_msgfb.m_fbId 
			      , st->m_msgfb.m_fbId 
			      // this is like the password
			      , hash32 ( (char *)&fbId , 8, 0 )
			      );
		sb.safePrintf(")</font></td></tr></table></div>");
		// spacer
		sb.safePrintf("<div style=line-height:15px><br></div>" );
	}

	// . pretty div with rounded corners to hold search results
	// . take the PRETTY DIV out for cached copies
	if ( si->m_raw == 0 && ! si->m_widget && ! si->m_includeCachedCopy ) {
		sb.safePrintf(
			      // for shadow:
			      //"<div style=background-color:#606060;"
			      //"border-radius:10px;>"

			      "<div style=\"border-radius:10px;"
			      );
		if ( si->m_emailFormat )
			sb.safePrintf("margin-left:5px;"
				      "margin-right:5px;"
				      "width:700px;");

		// PRETTY DIV
		sb.safePrintf(
			      // for shadow:
			      //"position:relative;"
			      //"bottom:6px;"
			      //"right:6px;"
			      "box-shadow: 6px 6px 3px %s;"

			      "padding:10px;"
			      //"margin-left:30px;"
			      //"margin-right:30px;"
			      // cartoon border
			      //"border:2px solid black;"
			      "border:1px solid black;"
			      //"border-right:5px solid black;"
			      //"border-bottom:5px solid black;"
			      "background-color:white;"
			      //"background-color:#f0f0f0;"

			      /*
			      "background-image:url('"
			      //"http://www.backgroundsy.com/file/preview/stainless-steel-texture.jpg"
			      "http://webdesignandsuch.com/posts/stainless/1.jpg"			      
			      "/ss.jpg"
			      "');"
			      "background-repeat:repeat;"
			      "background-attachment:fixed;"
			      */

			      "\">"
			      , SHADOWCOLOR
			      );
	}

	// make a table so results are in left column and icons in right
	if ( si->m_raw == 0 )
		sb.safePrintf("<table cellpadding=3 cellspacing=0>");

	if  ( numResults == 0 && si->m_raw == 0 )
		sb.safePrintf ( "<tr><td>No events found</td></tr>" );

	// for storing a list of all of the sites we displayed, now we print a 
	// link at the bottom of the page to ban all of the sites displayed 
	// with one click
	SafeBuf banSites;

	int32_t numExpResults = msg40->getNumExpandedResults();
	// if not doing result expansion, stick with old algo
	if ( numExpResults == -1 ) numExpResults = msg40->getNumResults();

	int32_t resultNum = -1;

	int32_t eix = 0;
	// if searching events we get back the full list so advance it here
	eix += si->m_firstResultNum;


	int32_t lastDayNum = -1;

	// assume EST for thisYear
	int32_t now = msg40->m_r.m_nowUTC;
	now += (-5) * 3600;
	struct tm *timeStruct = gmtime ( &now );
	int32_t thisYear = timeStruct->tm_year + 1900;
	bool printed = false;

	// don't display more than docsWanted results
	int32_t count = msg40->getDocsWanted();
	int32_t firstNum = msg40->getFirstResultNum();
	int32_t iconId = 0;
	bool hitTimeEnd = false;

	//log("Showing %"INT32" number of results", resultSize);
	for ( ; eix < numExpResults && count > 0; eix++ ) {
		// result #
		resultNum++;
		// get the #
		ExpandedResult *er = msg40->getExpandedResult(eix);

		// stop if we hit the timeEnd, used by EMAILER
		if ( si->m_timeEnd > 0 && er->m_timeStart >= si->m_timeEnd ) {
			hitTimeEnd = true;
			break;
		}

		// print out each one so we can see why Next 50 doesn't
		// connect with prev 50!!!
		//log("query: eix=%"INT32" origi=%"INT32" inta=%"INT32"",
		//    eix,er->m_mapi,er->m_timeStart);
		// get the original search result #
		int32_t ix = eix;
		// map it to the original summary it is an expansion of
		if ( er ) ix = er->m_mapi;

		bool newHeader = true;
		// do not print header if invalid ExpandedResult
		if ( ! er ) 
			newHeader = false;
		// or its daynum is the same as the last result
		if ( er && lastDayNum != -1 && lastDayNum == er->m_dayNum1 )
			newHeader = false;
		// or if we got a straddle condition when the event
		// times straddle the last event's day
		bool straddle = false;
		if ( er &&
		     lastDayNum >= er->m_dayNum1 &&
		     lastDayNum <= er->m_dayNum2 ) {
			straddle = true;
			newHeader = false;
		}
		// do not print headers if not menu junk or widget
		if ( ! printMenuJunk && ! si->m_widget && ! si->m_emailFormat) 
			newHeader = false;
		// not on cached page
		if ( si->m_includeCachedCopy )
			newHeader = false;
		// time for a new month/daynum header?
		if ( newHeader ) {
			// sanity
			if ( er->m_month1 < 0 || er->m_month1 >= 12 ) {
				char *xx=NULL;*xx=0; }
			if ( er->m_month2 < 0 || er->m_month2 >= 12 ) {
				char *xx=NULL;*xx=0; }
			// we are in a table. left pane is result, right pane
			// is icons
			sb.safePrintf("<tr><td colspan=2>");
			// not on first one
			if ( lastDayNum != -1 && ! si->m_igoogle )
				sb.safePrintf("<br>");
			if ( si->m_widget ) 
				sb.safePrintf("<span class=dayheader>");
			if ( ! si->m_widget ) {
				sb.safePrintf("<b>");
				sb.safePrintf("<font size=+1>");
			}
			// get day of week
			//int32_t dow = getDOW ( er->m_timeStart ) - 1;
			// print the month
			sb.safePrintf("%s, %s %"INT32"", 
				      s_dows[(unsigned char)er->m_dow1],
				      s_months[(unsigned char)er->m_month1],
				      (int32_t)er->m_dayNum1 );
			char *end = "th";
			// print st nd rd th
			if ( er->m_dayNum1 == 1 ||
			     er->m_dayNum1 == 21 ||
			     er->m_dayNum1 == 31 )
				end = "st";
			if ( er->m_dayNum1 == 2 ||  er->m_dayNum1 == 22 )
				end = "nd";
			if ( er->m_dayNum1 == 3 ||  er->m_dayNum1 == 23 )
				end = "rd";
			sb.safePrintf("%s",end);
			// print year only if different than current
			if ( er->m_year1 != thisYear )
				sb.safePrintf(" %"INT32"", er->m_year1 );
			//sb.safePrintf(" -");
			if ( si->m_widget ) sb.safePrintf("</span>");
			if ( ! si->m_widget ) {
				sb.safePrintf("</font>");
				sb.safePrintf("</b>");
			}
			char *hs = "20px";
			if ( si->m_igoogle ) hs = "10px";
			sb.safePrintf("<div style=line-"
				      "height:%s;><br></div>",
				      hs);
			// an extra space only looks good on non-widgets
			//if ( ! si->m_widget )
			//sb.safePrintf("<br>");
			sb.safePrintf("</td></tr>\n");
			lastDayNum = er->m_dayNum1;
		}
			     

		// new row
		char *widthStr = RESULTSWIDTHSTR;
		// leave 200px for map!
		if ( si->m_includeCachedCopy ) widthStr = "400";
		if ( si->m_raw == 0 ) 
			sb.safePrintf("<tr><td width=%s>",widthStr);

		// embody the result. give it the id 'res%"INT32"' so clicking on 
		// the broom/trashbin will result in it being hidden
		if ( si->m_raw == 0 ) 
			sb.safePrintf("<span class=result id=res%"INT32">", iconId);

		//if ( si->m_raw == 0 && ! si->m_widget ) 
		//	sb.safePrintf ("<br>\n" ); 

		// assume no error
		g_errno = 0;

		bool tt;

		// 
		// PRINT THE SEARCH RESULT
		//
		if ( si->m_raw == 0 )
			tt = printResult ( si->m_cr, 
					   st, 
					   qq,
					   si->m_coll2, 
					   pwd, 
					   msg40, 
					   firstNum,
					   count,
					   ix, 
					   printed, 
					   sb, 
					   &banSites,
					   er,
					   resultNum,
					   iconId);
		else
			tt = printXmlResult ( sb , st , er , ix );

		// count how many we printed
		st->m_numResultsPrinted++;

		// limit display
		count--;

		// int16_tcuts
		Msg20      *msg20 = msg40->m_msg20[ix];
		Msg20Reply *mr    = msg20->m_r;

		// print the likers and attendees under the result
		if ( ! si->m_raw ) 
			// then print out so-and-so is going to this...
			printAttendeesAndLikers ( sb , st , mr , iconId , er );

		// "Because it has the terms <i>xxx</i> yyy and zzz."
		if ( ! si->m_raw && si->m_showPersonal ) {
			printMatchingTerms ( sb , st, mr );
			printBecauseILike  ( sb , st, mr );
		}

		// wrap map on right
		if ( si->m_includeCachedCopy ) {
			sb.safePrintf ( "</td>");
			sb.safePrintf ( "<td valign=top>");
			// click this to get driving directions
			printDrivingDirectionsLink ( sb , si , mr );
			sb.safePrintf ( "<img align=right "
					"title=\"Click for directions\" "
					"width=180 "
					"height=180 src=\"");
			printMapUrl ( sb , st , 180 , 180 );
			// city wide level
			sb.safePrintf("&zoom=8");
			sb.safePrintf("\">");
		}



		// print no junk below if xml feed
		if ( si->m_raw ) continue;

		// spacer
		sb.safePrintf ("<br>\n" ); 

		// end - embody the result
		//sb.safePrintf("</span>\n");

		// then the icons
		sb.safePrintf("</td>");

		// don't print these icons for widgets yet
		//if ( ! si->m_widget ) {
		//	sb.safePrintf("<td>");
		//	printIcons1 ( sb , msg20, mr , er, si , iconId );
		//	sb.safePrintf("</td>");
		//}


		iconId++;

		sb.safePrintf("</tr>\n");

		// only print one of the expanded results
		if ( si->m_includeCachedCopy ) break;
	}

	// out of mem?
	if ( g_errno ) 
		return sendErrorReply7 ( st );

	// close the results/icons table
	if ( si->m_raw == 0 ) 
		sb.safePrintf("</table>");

	//
	// PRINT PREV 10 NEXT 10 links!
	// 

	// how many results were requested?
	int32_t docsWanted = msg40->getDocsWanted();

	// if we have a int32_t list of sites, then we'll need to use the POST
	// method since MSIE cannot deal with it
	char *method = "get";
	if ( si->m_sitesLen > 800 ) method = "post";
	else if ( si->m_q->m_origLen > 800 ) method = "post";

	// set this too
	//char *widgetHeader = st->m_hr.getString("widgetheader",NULL);
	// make widget display simple
	//if ( widgetHeader && si->m_raw == 0 && si->m_widget )
	//	sb.safePrintf("%s", widgetHeader);


	// center everything below here
	if ( printMenuJunk ) 
		sb.safePrintf ( "<br><center>" );

	bool passWidgetParms = false;
	if ( si->m_widget      ) passWidgetParms = true;
	if ( si->m_igoogle     ) passWidgetParms = false;
	//if ( si->m_interactive ) passWidgetParms = true;
	int64_t fbId = st->m_msgfb.m_fbId;

	// remember this position
	int32_t remember = sb.length();
	// now print "Prev X Results" if we need to
	if ( firstNum > 0 && si->m_raw == 0 && ! si->m_includeCachedCopy ) {
		int32_t newFirstnum = firstNum - docsWanted;
		if ( firstNum < 0 ) firstNum = 0;
		// now make a secret key for clicking next 10
		int32_t ss = newFirstnum ;
		int32_t qlen2 = qlen;
		if ( ss == 0 ) qlen2 = 0;
		// no... now we store everything in cookies
		/*
		// if we are doing a post, use a button
		if ( method[0]=='p' || method[0]=='P' ) {
			char tmp[32];
			sprintf ( tmp , "Prev %"INT32"", docsWanted );
			char *p;
			p = printPost ( sb.getBuf(),sb.getBufEnd(), st, 
					"PP", ss, docsWanted, si->m_qe, tmp);
			sb.incrementLength(p - sb.getBuf());
			goto skip11;
		}	
		*/	
		if ( si->m_raw == 0 )
			sb.safePrintf("<span class=prevnext>");
		//g_httpServer.getKey (&key,kname,q,qlen2,getTime(),ss,10);
		// add the query
	        //sb.safePrintf("<a href=\"/search?%s=%"INT32"&s=%"INT32"&",kname,key,ss)
		if ( si->m_emailFormat )
			sb.safePrintf("<a href=\"%s?"
				      "showpersonal=1&"
				      "usefbid=%"UINT64"&"
				      "fh=%"UINT32""
				      "\">"
				      , APPHOSTUNENCODED 
				      //, ss
				      , st->m_msgfb.m_fbId 
				      // this is like the password
				      , hash32((char *)&fbId,8,0)
				      );
		else if ( passWidgetParms )
			sb.safePrintf("<a href=\"/?s=%"INT32"&widget=1&%s\">"
				      , ss
				      , si->m_widgetParms.getBufStart() );
		else
			sb.safePrintf("<a onclick=\""
				      //"document.getElementById('s')."
				      //"value=%"INT32";\n"
				      //"window.scrollTo(0,0);\n"
				      "reloadResults(0,'&s=%"INT32"');\">",
				      ss);
		// our current query parameters
		//sb.utf8Encode ( si->m_urlParms , si->m_urlParmsLen );
		// propagate widget special
		//if ( si->m_widget ) sb.safePrintf("&widget=1");
		// and widget header!
		//if ( widgetHeader ) {
		//	sb.safePrintf("&widgetheader=");
		//	sb.urlEncode(widgetHeader);
		//}
		// close it up
		sb.safePrintf (""
			       "<nobr>Prev %"INT32" Results</nobr>"
			       "</a>"
			       , docsWanted );
		if ( si->m_raw == 0 )
			sb.safePrintf("</span>");
	}
	// skip11:
	// now print "Next X Results" (Next 10) (Next 25)
	if ( numExpResults>0 && 
	     ! hitTimeEnd &&
	     ! si->m_includeCachedCopy &&
	     si->m_raw == 0 &&
	     ( msg40->m_msg3a.m_numDocIds >= firstNum+docsWanted ||
	       // add -5 to fix bug of losing results
	       numExpResults >= firstNum+docsWanted ) ) {
		// now make a secret key for clicking next 10
		int32_t ss = firstNum+docsWanted;
		// if we are doing a post, use a button
		// no... now we store everything in cookies
		/*
		if ( method[0]=='p' || method[0]=='P' ) {
			char tmp[32];
			sprintf ( tmp , "Next %"INT32"", docsWanted );
			char *p = sb.getBuf();
			char *pend = sb.getBufEnd();
			char* ptmp = printPost ( p,pend,st ,"NN",ss,
						 docsWanted,si->m_qe,tmp);
			sb.incrementLength(ptmp - p);
			goto skip12;
		}
		*/
		// print a separator first if we had a prev results before us
		if ( sb.length() > remember ) sb.safePrintf ( " &nbsp; " );

		//g_httpServer.getKey (&key,kname,q,qlen,getTime(),ss,10);
		// add the query
	        //sb.safePrintf("<a href=\"/search?%s=%"INT32"&s=%"INT32"&",kname,key,ss)
		//sb.safePrintf ("<a href=\"/?s=%"INT32"&",ss);

		if ( si->m_raw == 0 )
			sb.safePrintf("<span class=prevnext>");

		if ( si->m_emailFormat )
			sb.safePrintf("<a href=\"%s?"
				      "showpersonal=1&"
				      "ei=%"UINT64"&"
				      "usefbid=%"UINT64"&"
				      "fh=%"UINT32""
				      "\">"
				      , APPHOSTUNENCODED 
				      //, ss
				      , st->m_msgfb.m_fbId 
				      , st->m_msgfb.m_fbId 
				      // this is like the password
				      , hash32 ( (char *)&fbId , 8, 0 )
				      );
		else if ( passWidgetParms )
			sb.safePrintf("<a href=\"/?s=%"INT32"&widget=1&%s\">"
				      , ss
				      , si->m_widgetParms.getBufStart() );
		else
			sb.safePrintf("<a onclick=\""
				      //"document.getElementById('s')."
				      //"value=%"INT32";\n"
				      //"window.scrollTo(0,0);\n"
				      "reloadResults(0,'&s=%"INT32"');\">",
				      ss);
		// our current query parameters
		//sb.utf8Encode ( si->m_urlParms, si->m_urlParmsLen );
		// propagate widget special
		//if ( si->m_widget ) sb.safePrintf("&widget=1");
		// and widget header!
		//if ( widgetHeader ) {
		//	sb.safePrintf("&widgetheader=");
		//	sb.urlEncode(widgetHeader);
		//}

		// hotmail somehow won't display our nobr tags, i guess
		// they sylized them invisible or something....
		char *t1 = "";
		char *t2 = "";
		if ( si->m_widget ) {
			t1 = "<nobr>";
			t2 = "</nobr>";
		}

		// close it up
		if ( si->m_emailFormat ) 
			sb.safePrintf("<center>"
				      "<b>Show all events</b>"
				      "</center>"
				      "</a>");
		else if ( docsWanted == 1 )
			sb.safePrintf(""
				      "%sNext Result%s"
				      "</a>" , t1 , t2 );
		else
			sb.safePrintf(""
				      "%sNext %"INT32" Results%s"
				      "</a>", 
				      t1, docsWanted , t2 );
		if ( si->m_raw == 0 )
			sb.safePrintf("</span>");
	}
	// skip12:
	//
	// END PRINT PREV 10 NEXT 10 links!
	// 

	if ( si->m_raw == 0 && ! si->m_widget && ! si->m_includeCachedCopy ) 
		// close the pretty div containing the search results
		sb.safePrintf("</div>"
			      // close shadow div
			      //"</div>"
			      );

	// print try this search on...
	// an additional <br> if we had a Next or Prev results link
	if ( sb.length() > remember && si->m_raw == 0 )
		sb.safePrintf ("<br>" );

	// propagate widget special
	if ( si->m_widget && numResults && si->m_raw == 0 )
		sb.safePrintf("<br><br><font size=-2>"
			      "<center>"
			      "<a href=\"http://www.%s/\" "
			      "target=\"_blank\">"
			      "Powered by EventGuru.com</a>"
			      "</center>"
			      "</font>"
			      //, APPDOMAIN
			      , APPDOMAIN
			      );

	return true;
}

// print the user lat/lon either exact or zip in here now for debugging...
bool printXmlHeader ( SafeBuf &sb , State7 *st ) {

	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;

	sb.safePrintf ( "<?xml version=\"1.0\" "
			"encoding=\"UTF-8\" ?>\n"
			"<response>\n" );

	// . include error if any
	// . just because you got results does not mean there wasn't
	//   an error. one summary might have failed to generate
	//   because the document was not found or not enough memory.
	if ( msg40->m_errno ) {
		sb.safePrintf ( "\t<errno>%"INT32"</errno>\n",
				msg40->m_errno);
	}
	if ( g_errno ) {
		sb.safePrintf ( "\t<errno>%i</errno>\n",
				g_errno);
	}


	int32_t nowUTC = getTimeGlobal();
	sb.safePrintf("<currentTimeUTC>%"UINT32"</currentTimeUTC>\n",
		      nowUTC);
	// print time nwo in utc
	if ( si->m_cityLat != 999.0 )
		sb.safePrintf("<userCityLat>%.07f</userCityLat>\n",
			      si->m_cityLat);//-180.0);
	if ( si->m_cityLon != 999.0 )
		sb.safePrintf("<userCityLon>%.07f</userCityLon>\n",
			      si->m_cityLon);//-180.0);
	if ( si->m_zipLat != 999.0 )
		sb.safePrintf("<userZipLat>%.07f</userZipLat>\n",
			      si->m_zipLat);//-180.0);
	if ( si->m_zipLon != 999.0 )
		sb.safePrintf("<userZipLon>%.07f</userZipLon>\n",
			      si->m_zipLon);//-180.0);
	if ( si->m_userLat != 999.0 )
		sb.safePrintf("<userLat>%.07f</userLat>\n",
			      si->m_userLat);//-180.0);
	if ( si->m_userLon != 999.0 )
		sb.safePrintf("<userLon>%.07f</userLon>\n",
			      si->m_userLon);//-180.0);
	// bounding box
	if ( si->m_maxLat != 360  ) {
		float delta = si->m_maxLat-si->m_minLat;
		sb.safePrintf("<boxLatCenter>%.07f</boxLatCenter>\n",
			      (si->m_minLat +delta/2));// - 180);
	}
	if ( si->m_maxLon != 360  ) {
		float delta = si->m_maxLon-si->m_minLon;
		sb.safePrintf("<boxLonCenter>%.07f</boxLonCenter>\n",
			      (si->m_minLon +delta/2));// - 180);
	}
	if ( si->m_minLat != 0 ) {
		sb.safePrintf("<boxRadius>%.02f</boxRadius>\n",
			      si->m_radius);
		sb.safePrintf("<boxMinLat>%.07f</boxMinLat>\n",
			      si->m_minLat);//-180.0);
	}
	if ( si->m_maxLat != 360.0 )
		sb.safePrintf("<boxMaxLat>%.07f</boxMaxLat>\n",
			      si->m_maxLat);//-180.0);
	if ( si->m_minLon != 0 )
		sb.safePrintf("<boxMinLon>%.07f</boxMinLon>\n",
			      si->m_minLon);//-180.0);
	if ( si->m_maxLon != 360.0 )
		sb.safePrintf("<boxMaxLon>%.07f</boxMaxLon>\n",
			      si->m_maxLon);//-180.0);
		
	TcpSocket *ts = st->m_socket;
	bool isLocal = false;
        // int16_tcut
        uint8_t *p = (uint8_t *)&ts->m_ip;
        // this is local
        if ( p[0] == 10 ) isLocal = true;
        // this is local
        if ( p[0] == 192 && p[1] == 168 ) isLocal = true;
        // if we match top two ips, then its local
        //if ( (ip&0x0000ffff) == (g_hostdb.m_myIp&0x0000ffff)) isLocal = true;

	// show response time
	sb.safePrintf("\t<responseTime>%"INT64"</responseTime>\n",st->m_took);

	// save how many docs are in it
	int64_t docsInColl = -1;
	//RdbBase *base = getRdbBase ( RDB_CHECKSUMDB , si->m_coll );
	RdbBase *base = getRdbBase ( (uint8_t)RDB_CLUSTERDB , si->m_coll2 );
	if ( base ) docsInColl = base->getNumGlobalRecs();

	int64_t totalHits  = msg40->getNumTotalHits();
	sb.safePrintf(
		      "\t<hits>%"INT64"</hits>\n"
		      "\t<moreResultsFollow>%"INT32"</moreResultsFollow>\n", 
		      (int64_t)totalHits ,
		      (int32_t)msg40->moreResultsFollow() );
	// was the query dirty?
	if ( st->m_msg40.m_queryCensored )
		sb.safePrintf("\t<queryCensored>1"
			      "</queryCensored>\n");
	// how many results were censored, if any?
	if ( st->m_msg40.getNumCensored() )
		sb.safePrintf("\t<resultsCensored>%"INT32""
			      "</resultsCensored>\n",
			      (int32_t)st->m_msg40.getNumCensored() );
	// . did he get a spelling recommendation?
	// . do not use htmlEncode() on this anymore since receiver
	//   of the XML feed usually does not want that.
	if ( st->m_spell[0] ) {
		sb.safePrintf ("\t<spell><![CDATA[");
		sb.utf8CdataEncode(st->m_spell, gbstrlen(st->m_spell));
		sb.safePrintf ("]]></spell>\n");
	}

	// get the max score
	rscore_t maxScore = 0;
 	int32_t numResults = msg40->getNumResults();
	for ( int32_t i = 0 ; i < numResults ; i++ ) {
		if ( msg40->m_msg3a.m_errno ) continue;
		rscore_t mm = msg40->m_msg3a.getScores()[0];
		if ( mm > maxScore ) maxScore = mm;
	}
		
	// print current time
	sb.safePrintf("\t<serpCurrentLocalTime><![CDATA[" );
	printLocalTime(sb,st);
	sb.safePrintf("]]></serpCurrentLocalTime>\n");
	return true;
}

// always returns true
bool sendPageBack ( TcpSocket *s , 
		    SearchInput *si , 
		    SafeBuf *sb ,
		    Msgfb *msgfb ,
		    HttpRequest *hr ) {
		    //SafeBuf *interestCookies ) {

	// debug
	if ( si && si->m_debug )
		logf(LOG_DEBUG,"query: Sending back page of %"INT32" bytes.",
		     sb->length());

	// the cookie is the where box
	//char  cookieBuf[201];
	//char *cookiePtr = NULL;
	//char *where = si->m_where;//st->m_hr.getString("where",NULL);
	// . if "cookie=0" was in cgi parms, do not do this
	// . this prevents the "get the widget" page from resetting the cookie
	//HttpRequest *hr = &st->m_hr;
	//int32_t cookie = hr->getLong("cookie",1);
	//if ( where && cookie ) {
	//	cookiePtr = cookieBuf;
	//	//"username=%s;expires=0;" (from PageLogin.cpp?)
	//	snprintf ( cookieBuf , 200, "where=%s;expires=0;", where );
	//}
	//char *cookiePtr = si->m_cookieBuf.getBufStart();
	// do not use cookies on cached pages, we end up storing the
	// gbdocid: query!
	//if ( si->m_includeCachedCopy ) cookiePtr = NULL;

	// . even if logged in with facebook,maintain our own userid via cookie
	// . we also tag with this id as well in case they use the same 
	//   computer and are not logged into facebook
	char  cookieBuf[1000];
	SafeBuf cb ( cookieBuf, 1000 );

	// include the unsubscribe based cookies from searchinput
	if ( si && si->m_cookieBuf.length() )
		cb.safePrintf("%s",si->m_cookieBuf.getBufStart());

	/*
	int64_t userId = hr->getLongLongFromCookie("userid",0LL);
	if ( ! userId ) {
		userId = rand();
		uint64_t now = getTimeLocal();
		userId = hash32h ( now , userId );
		// keep positive
		userId >>= 1;
		cb.safePrintf("Set-Cookie: userid=%"UINT64";expires=2000000000;\r\n"
			     ,userId);
	}
	*/

	//if ( hr->getLong("logout",0) ) 
	//	cb.safePrintf("Set-Cookie: fbid=%"UINT64";expires=2000000000;\r\n",
	//		      0LL);
	if ( msgfb->m_fbId ) {
		// msie doesn't like expires? or at least not that big a #
		// and it should have been max-age anyway
		cb.safePrintf("Set-Cookie: fbid=%"UINT64";\r\n",msgfb->m_fbId);
	}

	// . include widgetid now for payments.
	// . the first time they visit eventguru record the widgetid
	// . record widget id of 1 if none
	if ( si ) {
		int64_t widgetId = hr->getLongLong("widgetid",0LL);
		// if we parsed it out from a user_to_user app request
		// from facebook, use that
		if ( ! widgetId && msgfb->m_userToUserWidgetId )
			widgetId = msgfb->m_userToUserWidgetId;
		// if all else fails, assume no referral. so set it to 1
		// so it can not be overwritten
		if ( ! widgetId ) widgetId = 1;
		cb.safePrintf ( "Set-Cookie: widgetid=%"UINT64";\r\n",widgetId );
	}

	//if ( interestCookies && interestCookies->length() )
	//	cb.cat ( *interestCookies );



	char *cookiePtr = NULL;
	if ( cb.length() ) cookiePtr = cb.getBufStart();

	// log that
	//if ( cookiePtr ) log("gb: set-cookie=%s",cookiePtr );

	// no longer set cookie here, let the browser set its own cookies
	// using client-side javascript now. see the <scripts> above for that.
	// every button/control/etc should set a cookie to its value when it
	// is set. fo categories we might want to delete the cookie if it
	// is disabled to save cookie space.

	logf(LOG_DEBUG,"gb: sending back %"INT32" bytes",sb->length());

	char *charset;
	if (sb->getEncoding() == csISOLatin1) charset = "ISO-8859-1";
	else                                  charset = "utf-8";

	char *ct = "text/html";
	if ( si && si->m_raw > 0 ) ct = "text/xml";

	bool tt;
	tt = g_httpServer.sendDynamicPage ( s      , 
					    sb->getBufStart(), 
					    sb->length(), 
					    25         , // cachetime in secs
					                 // pick up key changes
					                 // this was 0 before
					    false      , // POSTREply? 
					    ct         , // content type
					    -1         , // http status -1->200
					    cookiePtr  ,
					    charset    );
			
	// nuke State7 class
	//return tt;
	return true;
}

// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool gotResults ( void *state ) {
	// cast our State7 class from this
	State7 *st = (State7 *) state;
	// int16_tcuts
	SearchInput *si      = &st->m_si;
	char        *coll    = si->m_coll2;
	int32_t         collLen = si->m_collLen2;
	// light brown stats color if not raw
	int32_t color = 0x00753d30 ;
	// use light brown if coming directly from an end user
	if ( si->m_endUser )
		color = 0x00b58869;
	// extract the TcpSocket from the "state" data
	//TcpSocket *s = st->m_socket;
	Msg40 *msg40 = &(st->m_msg40);
	//int32_t rawFormat = si->m_raw;

	// out of memory allocating msg20s?
	if ( st->m_errno ) {
		log("query: Query failed. Had error processing query: %s",
		    mstrerror(st->m_errno));
		g_errno = st->m_errno;
		// should always return true
		return sendErrorReply7 ( st );
		/*
		int32_t err = st->m_errno;
		//delete (st);
		if ( err != ENOPERM ) g_stats.m_numFails++;
		int32_t status = 400;
		if (err == ENOMEM) status = 500;
		g_httpServer.sendQueryErrorReply 
			( s, status , mstrerror(err), rawFormat,
			  g_errno,"Query failed. Had error processing query.");
	       return true;
		*/
	}

	// collection rec must still be there since SearchInput references 
	// into it, and it must be the SAME ptr too!
	CollectionRec *cr = g_collectiondb.getRec ( coll , collLen );
	if ( ! cr || cr != si->m_cr ) {
	       log("query: Query failed. "
		   "Collection no longer exists or was deleted and "
		   "recreated.");
		g_errno = ENOCOLLREC;
		// should always return true
		return sendErrorReply7 ( st );
		/*
	       //delete (st);
	       g_stats.m_numFails++;
	       g_httpServer.sendQueryErrorReply
		       (s,400,mstrerror(ENOCOLLREC),rawFormat,
			ENOCOLLREC, 
			"Query failed. Collection does not exist.");
	       return true;
		*/
	}

	bool showPersonal = si->m_showPersonal;
	// turn it off for cached page display
	if ( si->m_includeCachedCopy  ) showPersonal = false;

	// if showing personal results, the first query we do is to get
	// the associated facebook ids of people that are linked to the
	// events in the search results
	int32_t nr = msg40->getNumResults();
	if (   si->m_personalRound != 0 ) nr = 0;
	if ( ! showPersonal             ) nr = 0;
	HashTableX ppl;
	char pplbuf[1024];
	if ( nr ) ppl.set ( 8,4,64,pplbuf,1024,false,0,"ppltbl");
	for ( int32_t i = 0 ; i < nr ; i++ ) {
		// get the summary for result #i
		Msg20      *msg20 = msg40->m_msg20[i];
		Msg20Reply *mr    = msg20->m_r;
		// . get the people inivited, liking it, going to it, etc.
		// . add one point every time this person was involved
		char *p    =     mr-> ptr_likedbList;
		char *pend = p + mr->size_likedbList;
		for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
			int32_t flags = g_likedb.getPositiveFlagsFromRec ( p );
			// this means they DON'T like it!
			if ( flags & LF_HIDE     ) continue;
			if ( flags & LF_DECLINED ) continue;
			int64_t fbId = g_likedb.getUserIdFromRec ( p );
			if ( ! ppl.addTerm ( &fbId ) )
				// should always return true
				return sendErrorReply7 ( st );
		}
	}
	// switch round from 0 to 1
	if ( showPersonal && si->m_personalRound == 0 ) {
		// switch it so we do not repeat this
		si->m_personalRound = 1;
		// make the list of friends to set to searchinput
		int32_t need = ppl.getNumSlotsUsed()*8;
		if ( ! si->m_similarPeopleIds.reserve(need) ) 
			// should always return true
			return sendErrorReply7 ( st );
		// get a max
		int32_t max = 0;
		int32_t maxFbId = 0LL;
		// serialize the table now, but only people with a score of 2+
		// which means they were involved with 2 or more events that
		// match your query.
		for ( int32_t i = 0 ; i < ppl.m_numSlots ; i++ ) {
			if ( ! ppl.m_flags[i] ) continue;
			// get number of events they liked/goingto/invited
			int32_t ne = ppl.getScoreFromSlot(i);
			// skip if not with 2+ events in your interests
			if ( ne < 2 ) continue;
			// who is this?
			int64_t fbId = *(int64_t *)ppl.getKeyFromSlot(i);
			// skip if its you!
			if ( fbId == st->m_msgfb.m_fbId ) continue;
			// skip if can't beat the max
			if ( ne <= max ) continue;
			// ok, new winner
			max = ne;
			maxFbId = fbId;
		}
		// 
		// HACK: turn this off for now!! i don't think it's that great
		//
		max = 0;
		// . ok, just use the max winner now since i'm not convinced
		//   this algo is all that great...
		// . Msg40.cpp will use this SafeBuf as the friendIds for
		//   the gbsimilarinto:1 query term termlist
		if ( max ) si->m_similarPeopleIds.pushLongLong(maxFbId);
		// why do we need this?
		HttpRequest *hr = &st->m_hr;
		si->m_firstResultNum = hr->getLong("s",0);
		// then get more results without pre-pending the
		// gbsomeoneinto:1 to the query in SearchInput.cpp.
		// we will use ppl table to score all the results.
		// we will add "... OR gbfriendinto:1 and require that
		// at least two "friends" from st->m_ppl table be
		// associated with the event.
		// so UNDO the parms we tweaked in SearchInput.cpp
		// because m_showPersonal was true
		si->m_numLinesInSummary = 10;
		si->m_showExpiredEvents = false;
		// let's cut down to 25 so email is not too big
		si->m_docsWanted        = 25;//100;
		// make email smaller! otherwise facebook email proxy
		// proxied email doesn't pass it on. hmmm...
		// i've seen this work now at 25. it got delivered... 
		// i dunno wassup...
		if ( si->m_emailFormat )
			si->m_docsWanted = 25;
		// and adjust the query to take advanted of our list of
		// facebook people that have similar interests to us
		si->m_sbuf1.m_length = si->m_savedLength;
		si->m_sbuf1.m_buf[si->m_savedLength] = '\0';
		// replace ") AND gbsomeoneinto:1" with " OR gbfriendsinto:1)"
		si->m_sbuf1.insert("gbsimilarinto:1 OR ",0);
		// remove events that were successfully emailed to this user
		// in the past!!
		if ( si->m_emailFormat )
			si->m_sbuf1.safePrintf(" | -gbigotemailed:1");
		// this is the query msg40 uses
		si->m_q->set(si->m_sbuf1.getBufStart());
		// . limit search results to 14 days out...
		// . will take starting time and add 14 days to it
		// . it is all UTC
		si->m_timeEnd = getTimeGlobal() + 14 * 86400;
		// another log
		log("query: got query2 %s",si->m_sbuf1.getBufStart());
		// undo this i guess. see if this fixes it.
		st->m_gotResults = false;
		// . and refetch the results
		// . return false if that blocked, which it should really
		if ( ! st->m_msg40.getResults(si,false,st,gotResultsWrapper3) )
			return false;
		// it did not block!
		st->m_gotResults = true;
		// error?
		log("query: 2nd msg40 did not block: %s",mstrerror(g_errno));
	}


  	char  local[ 86000 ];
  	SafeBuf sb(local, 86000 );
	// . set encoding here!
	// . TODO: have an enc= cgi parm to set it!
	sb.setEncoding(csUTF8);

	// tell browser again
	//if ( ! rawFormat && ! si->m_frame )
	//	sb.safePrintf("<meta http-equiv=\"Content-Type\" "
	//		      "content=\"text/html; charset=utf-8\">\n");

	// . make the query class here for highlighting
	// . keepAllSingles means to convert all individual words into
	//   QueryTerms even if they're in quotes or in a connection (cd-rom).
	//   we use this for highlighting purposes
	Query qq;
	qq.set ( si->m_displayQuery,
		 si->m_boolFlag,
	         true ); // keepAllSingles?


	// sanity check
	if ( si->m_emailFormat && ! st->m_msgfb.m_fbId ) {
		log("query: no fbid for doing email query");
		g_errno = EBADREQUEST;
	}


	// ignore imcomplete or invalid multibyte or wide characters errors
	if ( g_errno == EILSEQ ) {
		log("query: Query error: %s. Ignoring.", mstrerror(g_errno));
		g_errno = 0;
	}

	if ( g_errno ) {
		log("query: Query failed. Had error processing query: %s",
		    mstrerror(g_errno));
		// should always return true
		return sendErrorReply7 ( st );
		/*
		int32_t err = g_errno;
		if ( err != ENOPERM ) g_stats.m_numFails++;
		int32_t status = 400;
		if (err == ENOMEM) status = 500;
		g_httpServer.sendQueryErrorReply 
			( s, status , mstrerror(err), rawFormat,
			  g_errno,"Query failed. Had error processing query.");
		return true;
		*/
	}

	// debug msg
 	int32_t numResults = msg40->getNumResults();
	log ( LOG_TIMING ,
	     "query: Got %"INT32" search results in %"INT64" ms for q=%s",
	      numResults,gettimeofdayInMilliseconds()-st->m_startTime,
	      qq.getQuery());

	int64_t nowms = gettimeofdayInMilliseconds();
	int64_t delta = nowms - st->m_startTime ;

	Highlight h;

	// this is now all raw formats
	if ( si->m_raw )
		printXmlHeader ( sb , st );

	FBRec *fbrec = st->m_msgfb.m_fbrecPtr;
	char *toEmail = NULL;
	char *toName  = NULL;
	if ( fbrec ) {
		// prefer proxied email so it will come from facebook
		// and won't be chucked into the spam folder
		toEmail = fbrec->ptr_proxied_email;
		// but if that is NULL then try the actual email address
		if ( ! toEmail ) toEmail = fbrec->ptr_email;

		// for debugging
		//toEmail = fbrec->ptr_email;

		// get name too
		toName = fbrec->ptr_name;
		// default to generic name if not there
		if ( ! toName ) toName = "Facebook User";
	}
		

	// . make an email command
	// . ESMTP servers identify themselves as such and facebook's
	//   proxies all support "PIPELINING" so we can send this all at one
	if ( si->m_emailFormat && toEmail && toName ) {
		//int32_t now = getTimeGlobal();
		sb.safePrintf(
			      //"EHLO gigablast.com\r\n"//eventguru.com\r\n"
			      "EHLO eventguru.com\r\n"
			      //"MAIL from:<eventguru@eventguru.com>\r\n"
			      "MAIL From:<guru@eventguru.com>\r\n"
			      "RCPT To:<%s>\r\n"
			      "DATA\r\n"
			      //"From: mwells <mwells2@gigablast.com>\r\n"
			      "From: mwells <guru@eventguru.com>\r\n"
			      "MIME-Version: 1.0\r\n"
			      "To: %s\r\n"
			      "Subject: Personalized "
			      "Recommendations from "
			      "EventGuru.com\r\n"
			      "Content-Type: text/html; charset=UTF-8; format=flowed\r\n"
			      "Content-Transfer-Encoding: 8bit\r\n"
			      // mime header must be separated from body by 
			      // an extra \r\n
			      "\r\n"
			      "\r\n"
			      //, ctime(&now)
			      , toEmail
			      , toEmail // Name
			      );
	}

	// if we did an icc=1 link below, print the cached page, and that's it
	bool printMenuJunk = true;
	if ( si->m_includeCachedCopy ) printMenuJunk = false;
	if ( si->m_raw               ) printMenuJunk = false;
	if ( si->m_widget            ) printMenuJunk = false;
	if ( si->m_emailFormat       ) printMenuJunk = false;
	if ( si->m_frame             ) printMenuJunk = false;

	HttpRequest *hr = &st->m_hr;

	// int16_tcut
	int64_t fbId = st->m_msgfb.m_fbId;

	if ( si->m_emailFormat ) {

		// the <html> stuff
		// maybe hotmail doesn't like this!
		printHtmlHeader ( sb , "", 1,si,false);//->m_igoogle);

		//sb.safePrintf("<style type=\"text/css\">");
		//printGradHalo ( sb );
		//sb.safePrintf("</style>\n");

		//printHtmlHeader( sb , "Just For You", 0 );
		// this doesn't work in hotmail!
		//sb.safePrintf("<base href=http://www.eventguru.com/>\n");

		// i guess hotmail don't like gradhalo
		sb.safePrintf("<div " // class=gradhalo>"
			      "style=\""
			      //"background-image:url('/bg.png');"
			      //"background-repeat:repeat;"
			      "\">"
			      "<table><tr><td>"
			      "<a href=\"%s?ei=%"UINT64"\">"
			      "<img src=\"%s/eventguru.png\" "
			      "border=0 width=%"INT32"px height=%"INT32"px>"
			      "</td><td>"
			      "<h2 style=\"line-height:30px;"
			      //"color:white;"
			      "color:black;"
			      //"font-size:45px;"
			      //"outline-color:white;"
			      //"outline-width:2px;"
			      //"outline-style:inherit;"
			      "text-shadow: 2px 4px 10px white;"
			      "\">Personalized<br>"
			      "Recommendations<br>from "
			      "<a href=\"http://www.eventguru.com/?ei=%"UINT64"\">"
			      "EventGuru.com</a></h2></td></tr></table>" 
			      , APPHOSTUNENCODED
			      , fbId
			      , APPHOSTUNENCODED
			      , GURUDX
			      , GURUDY
			      , fbId
			      );
	}

	// put widget source in a div tag to prevent iframe from getting
	// scroll bars
	if ( si->m_widget ) {
		sb.safePrintf ("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML "
			       "4.01 Transitional//EN\" \"http://www.w3.org/"
			       "TR/html4/loose.dtd\">"
			       );
		// eliminate space between vertical scrollbar and div border
		sb.safePrintf ( "<body style=margin-right:0px;"
				"margin-bottom:0px;margin-top:0px;>\n"
				"<base href=%s>"
				,APPHOSTUNENCODED);
		//int32_t widgetWidth = hr->getLong("widgetwidth",200);
		int32_t widgetHeight = hr->getLong("widgetheight",400);
		// on igoogle we always use 300px for the height
		// . but we set si->m_igoogle to true if si->m_interactive
		//   is true just so webmaster can use that widget on their
		//   websites...
		if ( si->m_igoogle && ! si->m_interactive ) widgetHeight = 300;
		// the vertical scrollbar gets cutoff a little bit so i
		// guess there is an inside border maybe? this widget
		// height is the height of the outside iframe...
		// well getting rid of the body margin above fixed this!!!
		//widgetHeight -= 8;
		// 
		// the vert scrollbar is around 20 pixels wide bit varies by
		// browser!! firefox is a little wider than chrome ??
		// had to go from 20 to 25 for stupid msie. it was putting
		// the damn horizontal scrollbar at 20.
		//widgetWidth -= 25;
		// this is the first div contained by the widget iframe
		sb.safePrintf("<div " );
		//if ( si->m_igoogle ) sb.safePrintf("class=grad1 ");
		sb.safePrintf(
			      "style=\""
			      "position:relative;"//absolute;"
			      "overflow-y:scroll;"
			      //"overflow-x:hidden;"
			      //"position:absolute;"
			      "top:0px;"
			      "right:0px;"
			      "left:0px;"
			      "height:%"INT32"px;"
			      // this bottom does not work!
			      //"bottom:0px;"
			      //"width:%"INT32"px;"
			      "\">"
			      ,widgetHeight
			      //,widgetWidth
			      );
	}

	// cached web page header?
	if ( si->m_includeCachedCopy && ! si->m_frame ) {
		Msg20Reply *mr = NULL;
		if ( msg40->m_msg3a.m_numDocIds ) mr = msg40->m_msg20[0]->m_r;
		// assume no title
		char *title = "Event Page";
		// store title of event url encoded
		char tbuf[512];
		SafeBuf tb(tbuf,512);
		// if we had a result use the event title as the page title
		if ( mr ) {
			// store the title
			printEventTitle ( tb , mr , st );
			// filter out tags from highlighted query terms
			tb.filterTags();
			// make title the event title
			title = tb.getBufStart();
		}
		// the <html> stuff
		printHtmlHeader ( sb , title,0,NULL,false);
		// need to be able to "like" a result
		printScripts2 ( sb , st );
		// another div within the primary div to hold summary
		// of the one search result
		//
		// CACHED PAGE DIV #1 (SUMMARY)
		//
		sb.safePrintf("<div "
			      "style=\""
			      "overflow-y:auto;"//scroll;"
			      "overflow-x:hidden;"
			      "position:absolute;"
			      //"color:black;"
			      "top:0px;"
			      "right:0px;"
			      "left:0px;"
			      // msie needs this for some reason.
			      // not anymore, we had to use "px" for
			      // "right:0px". very strict!
			      //"width:100%%;"
			      "height:250px;\""
			      ">"
			      //">the<br>time<br>is<br>now</div>\n"
			      );
			      
		// topmost nav bar, logo and search boxes
		printBlackBar(sb,&st->m_msgfb,"/",st->m_socket->m_ip,0,0,NULL);
		// put single result into this table
		sb.safePrintf("<table cellpadding=10><tr>"
			      "<td width=%s id=searchresults>"
			      , RESULTSWIDTHSTR );
	}

	// if igoogle print a little menu
	//if ( si->m_igoogle ) {
	//	sb.safePrintf("<b style=font-size:13px;>"
	//		      "Search Topic Dist Time</b><br>");
	//}

	// the new default look
	if ( printMenuJunk || ( si->m_igoogle && ! si->m_includeCachedCopy) ) {
		// the <html> stuff
		printHtmlHeader ( sb , "", 1,si,false);//->m_igoogle);
		// topmost nav bar, logo and search boxes
		printTopBarNav ( sb , st );
		// igoogle widget we want the gradient to shine through
		//char *bg = ""; // " bgcolor=white";
		//if ( si->m_igoogle ) bg = "";
		// BEGIN MENU/RESULTS TABLE (left and right panes)
		sb.safePrintf ( "<table cellspacing=0 cellpadding=0"
				" id=main"
				// before it was transparent and showing
				// the gradhalo, so make this white.
				// this was causing us to lose our background
				// gradient for the igoogle widget
				//"%s"
				">"
				"<tr>"
				"<td valign=top>" 
				// bgcolor=#%s
				//, GRAD1
				//, bg
				);
		// assume true now
		bool printHorizontal = false;//true;
		// side bar
		if ( ! si->m_igoogle )
			printSideBarNav ( sb , st , printHorizontal );
		// end side bar
		sb.safePrintf("</td>");
		// put results below submenus?
		if ( printHorizontal || si->m_igoogle )
			sb.safePrintf("</tr><tr>");
		else
			// print spacer so results aren't so close to sidebar
			sb.safePrintf("<td width=21px></td>");
		// begin search results cell
		sb.safePrintf("<td valign=top width=%s id=searchresults>"
			      , RESULTSWIDTHSTR );

		// only prints out some header links if we are admin
		printAdminLinks ( sb , st );
	}

	if ( si->m_frame && ! si->m_includeCachedCopy ) {
		sb.safePrintf("<div id=mapurl url=\"" );
		printMapUrl ( sb , st , 180 , 180 );
		sb.safePrintf("\">");
	}

	//int32_t needLogin = hr->getLong("needlogin",0);
	int32_t likePage = hr->getLong("like",0);

	/*
	sb.safePrintf ( "<div style=line-height:8em>"
			"this is<br>"
			"a test<br>"
			"of stupid msie<br>"
			"should <input type=checkbox><br>"
			"space <input type=checkbox><br>"
			"all lines<br>"
			"equally<br>"
			"</div>" );
	*/

	// fill in the "searchresults" div
	if      ( si->m_testerPage ) printTesterPage ( sb );
	else if ( si->m_cities )     printCitiesFrame ( sb );
	//else if ( si->m_showPop )    printPopularInterestsFrame ( sb , st );
	// fill in the "searchresults" div with widget
	else if ( si->m_showWidget && printMenuJunk ) printWidgetFrame (sb,st);
	// this also prints in xml if "&xml=1"
	else if ( ! likePage)  printAllResults ( sb , st , qq );

	if ( ! si->m_testerPage &&
	     ! si->m_cities &&
	     //! si->m_showPop &&
	     printMenuJunk &&
	     ! si->m_didse &&
	     ! si->m_forForm && // PageSubmit.cpp?
	     ! si->m_widget ) {
		// . use this form variable to store meta cookie
		// . meta cookie concatenates all the form variables
		//sb.safePrintf("<input type=hidden id=addedse "
		//	      "name=addedse value=%"INT32">",
		//	      (int32_t)si->m_addedse);
		// print the invisible div that will slide up
		printAddSearchProvider ( sb );
	}



	//if ( needLogin ) 
	//	sb.safePrintf("<br>"
	//		      "<br>");

	if ( likePage ) {
		// go down some
		sb.safePrintf("<br>"
			      "<br>"
			      "<br>"
			      "<br>"
			      );
		// https://developers.facebook.com/apps/
		// 356806354331432/summary
		// used for facebook login described at
		// developers.facebook.com/docs/reference/plugins/like/
		sb.safePrintf ( "<div id=\"fb-root\"></div>"
				"<script>(function(d, s, id) {\n"
				"var js, fjs = d.getElementsByTagName(s)[0];\n"
				"if (d.getElementById(id)) return;\n"
				"js = d.createElement(s); js.id = id;\n"
				"js.src = \"//connect.facebook.net/"
				"en_US/all.js#xfbml=1&appId=%s\";\n"
				"fjs.parentNode.insertBefore(js, fjs);\n"
				"}(document, 'script', "
				"'facebook-jssdk'));</script>\n"
				, APPID
				);
		// spacer
		sb.safePrintf ( "<div style=\"width:%s\"></div>",
				RESULTSWIDTHSTR);

		// now give them the option to add the site to their
		// facebook stuff as a tab i think
		// http://developers.facebook.com/docs/reference/dialogs/
		// add_to_page/
		// "This application does not support integration with your 
		//  profile."
		/*
		sb.safePrintf (
			       "<script "
			       "src='http://connect.facebook.net/"
			       "en_US/all.js'></script>"
			       "<br><br><br>"
			       "<p><a onclick='addToPage(); "
			       "return false;'>Add to Page</a></p>"
			       "<p id='msg'></p>"
			       "<script> "
			       "FB.init({appId: \"%s\", status: true, "
			       "cookie: true});"
			       "function addToPage() {"
			       "var obj = {"
			       "method: 'pagetab',"
			       "redirect_uri: '%s',"
			       "};"
			       "FB.ui(obj);"
			       "}"
			       "</script>"
			       , APPID
			       , APPHOST
			       );
		sb.safePrintf (
			       "<a href=\"https://www.facebook.com/dialog/"
			       "pagetab?app_id=%s&display=popup&next=%s\">"
			       "Add Page 2"
			       "</a>"
			       , APPID
			       , APPHOST
			       );
		*/

		// 
		// request your friends to join you on eventguru
		//
		sb.safePrintf (
			       "<script src=\"http://connect.facebook."
			       "net/en_US/all.js\"></script>"
			       //"<p>"
			       //"<input type=button "
			       //"onclick=\"sendRequestToRecipients();"
			       //"return false;\" "
			       //"value=\"Send Request to Users Directly\""
			       //"/>"
			       //"<input type=text value=\"User ID\" "
			       //"name=\"user_ids\" />"
			       //"</p>"
			       "<br>"
			       "<center>"
			       "<input type=button "
			       "onclick=\"sendRequestViaMultiFriendSelector();"
			       "return false;\" "
			       "value=\"Tell Your Friends about Event Guru\""
			       "/>"
			       "</center>"
			       "<script>"
			       "FB.init({\n"
			       "appId  : '%s',\n"
			       "});\n"
			       "function "
			       "sendRequestViaMultiFriendSelector(){\n"
			       "FB.ui({method: 'apprequests',\n"
			       "message: '%s'\n"
			       "}, requestCallback);\n"
			       "}\n"
			       "function requestCallback(response) {\n"
			       "// Handle callback here\n"
			       "}\n"
			       "</script>\n"
			       , APPID
			       , INVITE_MSG
			       );
		//
		// add app tab to your page
		//
		//https://developers.facebook.com/docs/appsonfacebook/pagetabs/
		sb.safePrintf (
			        "<br><br>"
				"<center>"
			        //"<a href=\"http://www.facebook.com/dialog/"
				//"pagetab?app_id=%s&next=%s\">"
				"<a href=\"http://apps.facebook.com/"
				"eventguru/\">"
				"Install this App"
				"</a>"
				"</center>"
				//, APPID
				//, APPHOSTENCODED
				);

		// THIS just posts the like on your wall with a url to
		// www.eventguru.com. it does not take you to the eventguru
		// page on facebook, APPFBPAGE, which you go to from our
		// link on the About page
		sb.safePrintf ( //"<center>"
			        "<br>"
			        "<br>"
			        "<center>"
				"<b>"
				"Please click the link below to Like us."
				"</b>"
				"<br>"
				"<br>"
				"<div "
				"style=\""
				"display:inline;"
				//"width:450px;"
				//"border:2px solid black;"
				//"border-bottom: 5px solid black;\" "
				"\" "
				"class=fb-like "
				"data-href=\"http://www.%s/\" "
				"data-send=true "
				//"data-layout=button_count "
				//"data-width=450 "
				//"data-height=20 "
				"data-show-faces=true "
				"data-colorscheme=light "
				"data-font=arial "
				//"data-stream=false "
				//"data-header=false "
				">"
				"</div>" 
				"</center>"
				, APPDOMAIN
				);
		//
		// link to event guru wall
		//
		sb.safePrintf ( "<br>"
				"<br>"
				"<br>"
				"<center>"
				"<a href=\"http://www.facebook.com/"
				"pages/Event-Guru/385608851465019\">"
				"Contact Event Guru or post on his wall"
				"</a>"
				"</center>"

				// prevent tell your friends dialog
				// from being snipped by adding brs here
				"<br>"
				"<br>"
				"<br>"
				"<br>"
				"<br>"
				"<br>"
				"<br>"
				"<br>"
				);
	}

	/*
	// print facebook msg if not logged in
	if ( si->m_facebookId == 0 &&
	     ! likePage &&
	     ( si->m_friendsLike    ||
	       si->m_friendsInvited ||
	       si->m_friendsGoingTo ||
	       si->m_iAmGoingTo ||
	       si->m_iAmInvited ||
	       si->m_iLike ||
	       needLogin ) )
		sb.safePrintf( "<br>"
			       "<br>"
			       "<center>"
			       "<div style=\"width:%s;\"></div>"
			       "<b>You need to login to Facebook to use "
			       "this feature.</b>"
			       "<br>"
			       "<br>"
			       "<a id=login2 onclick=\""
			       "document.getElementById('login2').innerHTML="
			       "'<b><font size=1 color=black>Logging in..."
			       "</font></b>';"
			       "top.location.href='http://www.facebook.com/"
			       "dialog/oauth?client_id=%s&"
			       "redirect_uri=%s&"
			       "scope=user_events,friends_events'\">"
			       
			       "<img "
			       "align=center width=132 height=20 "
			       "src=/fblogin.png border=0></a>" 
			       "</center>"
			       , RESULTSWIDTHSTR
			       , APPID 
			       , APPHOSTENCODED
			       );
	*/

	// TODO: that handles a delayed cmd2 et al, but what about a delayed
	// cmd4, where we are still downloading events from facebook?
	if ( si->m_facebookId &&
	     ! likePage &&
	     ( si->m_friendsLike    ||
	       si->m_friendsInvited ||
	       si->m_friendsGoingTo ||
	       si->m_iAmGoingTo ||
	       si->m_iAmInvited ||
	       si->m_iLike ) &&
	     st->m_msgfb.m_fbrecPtr &&
	     st->m_msgfb.m_fbrecPtr->m_friend_count > 0 &&
	     st->m_msgfb.m_fbrecPtr->size_friendIds == 0 ) {
		sb.safePrintf( "<br>"
			       "<br>"
			       "<center>"
			       "<div style=\"width:%s;\"></div>"
			       "<b>Event Guru is still trying to download "
			       "your information from Facebook, so we are "
			       "unable to present you with these particular "
			       "search results at this time. Please try "
			       "again int16_tly."
			       "</b>"
			       "<br>"
			       "<br>"
			       "</center>"
			       , RESULTSWIDTHSTR
			       );
	}


	if ( printMenuJunk ) {
		// end search results cell
		sb.safePrintf("</td></tr></table>");
		//printHiddenParms ( sb , st ); // %H
	}

	// propagate this for PageSubmit.cpp
	if ( si->m_forForm ) {
		// exlude the name attribute so it does not get added into
		// cookie... but only then picked up by reloadPage() function
		sb.safePrintf("<input type=hidden id=forform "
			      "value=1>");
		sb.safePrintf("<input type=hidden id=formdocid "
			      "value=%"INT64">",
			      si->m_formDocId);
		sb.safePrintf("<input type=hidden id=formeventid "
			      "value=%"INT32">",
			      si->m_formEventId );
		if ( si->m_formUrl )
			sb.safePrintf("<input type=hidden id=formurl "
				      "name=formurl value=%s>",
				      si->m_formUrl );
	}

	if ( printMenuJunk || si->m_widget ) {
		
		sb.safePrintf("<input type=hidden id=emailfreq "
			      "name=emailfreq value=%"INT32">",si->m_emailFreq);

		sb.safePrintf("<input type=hidden name=radius id=radius "
			      "value=\"%.0f\">", si->m_radius);
		sb.safePrintf("<input type=hidden name=myradius id=myradius "
			      "value=\"%.0f\">", si->m_myRadius);

		sb.safePrintf("<input type=hidden name=where id=where "
			      "value=\"%s\">", si->m_where );
		sb.safePrintf("<input type=hidden name=mylocation "
			      "id=mylocation value=\"%s\">", si->m_myLocation);

		sb.safePrintf("<input type=hidden name=c value=%s>",
			      si->m_coll2 );
		sb.safePrintf("<input type=hidden name=showpersonal "
			      "id=showpersonal value=%"INT32">",
			      (int32_t)si->m_showPersonal );
		sb.safePrintf("<input type=hidden name=showwidget "
			      "id=showwidget value=%"INT32">",
			      (int32_t)si->m_showWidget );
		// since we moved it to redbox only we don't have this now 
		// because we do not call pritnSubMenu() on SUBMENU_CATEGORIES
		sb.safePrintf("<input type=hidden name=categories "
			      "id=categories value=%"INT32">",
			      (int32_t)si->m_categories);
		// put all the input boxes for the recommendation interests
		// here, but only if not logged into facebook! otherwise
		// we store them in the fbrec
		if ( ! st->m_msgfb.m_fbId && ! si->m_showPersonal ) 
			// justPrintHiddens = true
			printMyInterests ( sb , st , true );
		// same for categories. always propagate these...
		printCategoryInputs ( sb , si );
		// just put this here
		//sb.safePrintf("<input type=hidden name=mylocation "
		//	      "id=mylocation value=\"%s\">",
		//	      si->m_myLocation);
		// search result # to start at
		//sb.safePrintf("<input type=hidden name=s id=s value=%"INT32">",
		//	      si->m_firstResultNum );
		// we need to remember these since they have no checkboxes
		// otherwise we lose the values moving to another page.
		// only transfer them via cookies for now. add "name=" if
		// they should be in url?
		sb.safePrintf("<input type=hidden id=showmystuff "
			      "name=showmystuff value=%"INT32">",
			      (int32_t)si->m_showMyStuff);
		sb.safePrintf("<input type=hidden id=showfriends "
			      "name=showfriends value=%"INT32">",
			      (int32_t)si->m_showFriends);
		//sb.safePrintf("<input type=hidden "
		//	      "name=ig value=%"INT32">",
		//	      (int32_t)si->m_igoogle);
	}

	bool printUnsubscribePopup = hr->getLong("unsubscribe",0);

	// AFTER the big table has ended print copyright
	if ( printMenuJunk ) {
		sb.safePrintf("\n</form>\n");
		printHtmlTail ( &sb , &st->m_msgfb , printUnsubscribePopup );
	}

	// quit
	if ( si->m_emailFormat ) {
		// close gradhalo div
		sb.safePrintf("</div>");
		// a little note
		sb.safePrintf("<br><br>Please email guru@eventguru.com "
			      "with any questions or comments.");
		// convert any periods on their own line into hyphens
		// a period space \n is bad too! the period on the line
		// by itself is a signal to the email program that
		// the data is terminating!!
		sb.fixIsolatedPeriods();
		// wrap it up
		sb.safePrintf ( "\r\n.\r\nQUIT\r\n");
		//
		// inline all the style tags since hotmail, gmail.. 
		// always strip them
		//
		sb.inlineStyleTags ( );
	}

	//int64_t fbId = st->m_msgfb.m_fbId;
	if ( si->m_emailFormat && fbId ) {
		// the working dir
		char *dir = g_hostdb.m_dir;
		// save this file
		char filename[512];
		sprintf(filename,"%s/html/email/email.%"UINT64".html", dir,fbId);
		// save the buf to email
		if ( sb.dumpToFile ( filename ) ) {
			// a link to send it. not included in the sent email
			sb.safePrintf("<div id=removeme>"
				      "<br>"
				      "<br>"
				      "<a href=\""
				      );
			hr->getCurrentUrl(sb);
			sb.safePrintf("&sendemailfromfile=1\">"
				      "Send this email"
				      "</a>"
				      "</div>"
				      );
		}
		//
		// dump the likedb list too!!!
		//
		SafeBuf *ll = &st->m_likedbListBuf;//st->m_emailLikedbListBuf;
		if ( ll ) {
			sprintf(filename,"%s/html/email/likedblist.%"UINT64"",
				dir,fbId);
			ll->dumpToFile ( filename );
		}
	}


	/*
		sb.safePrintf(
			      "<br>"
			      "<center>"
			      "<font size=-1 color=gray>"
			      "Copyright &copy; 2012. All Rights Reserved."
			      "</font>"
			      "</center>"
			      "</div>"
			      "</body>"
			      "</html>" );
	*/

	Msg20Reply *mr = NULL;
	if ( msg40->m_msg3a.m_numDocIds ) mr = msg40->m_msg20[0]->m_r;

	ExpandedResult *er = NULL;
	// if "frame=1" that means they clicked on the thumbs up
	// icon while on the cached page and we are setting the
	// innerHtml of the "searchresults" <td>
	if ( ! printMenuJunk && si->m_includeCachedCopy && ! si->m_frame ) {
		// end single result cellpadding table
		sb.safePrintf("</td></tr></table>");
		// end that table started by printBlackBar() which contained
		// the search results table we already closed above.
		sb.safePrintf("</td></tr></table>");
		// end that div printed right before blackbar
		sb.safePrintf("</div>");
		// print a separator div to separate the event guru summary
		// from the cached web page. and now we fill with useful
		// ICONS!
		//
		// CACHED PAGE DIV #2 (EVENTGURU ICON)
		//
		sb.safePrintf("<div "
			      "style=\""
			      "overflow:hidden;"
			      "position:absolute;"
			      "background-color:transparent;"
			      "top:245px;"
			      "width:%"INT32"px;"
			      "left:15px;"
			      "height:%"INT32"px;"
			      "z-index:8;"
			      "\""
			      ">"
			      "<a href=/>"
			      "<img width=%"INT32"px height=%"INT32"px "
			      "src=%s>"
			      "</a>"
			      "</div>"
			      , GURUDX96
			      , GURUDY96
			      , GURUDX96
			      , GURUDY96
			      , GURUPNG
			      );

		//
		// print the MIDDLE BLACK BAR on the cached page
		//
		// CACHED PAGE DIV #3 (BLACKBAR)
		//
		sb.safePrintf("<div "
			      "class=grad4 "
			      "style=\""
			      "overflow:hidden;"
			      "position:absolute;"
			      "background-color:black;"
			      //"border-top: 20px solid white;"
			      "top:250px;"
			      "right:0px;"
			      "left:0px;"
			      // need this redundancy for msie:
			      "width:100%%;"
			      "height:50px;\""
			      ">"
			      "<table height=100%% "
			      "cellpadding=0 cellspacing=0>"
			      "<tr>"

			      "<td width=45px></td>"			      

			      "<td valign=center width=64px>"
			      //"<img width=32 height=32 src=eventguru32.png>"
			      "</td>"			      

			      "<td width=30px></td>"

			      /*
			      // call the script
			      "<td valign=center>"
			      "<img width=32 height=32 src=thumb32.png>"
			      "</td>"

			      "<td width=30px></td>"			      

			      "<td valign=center>"
			      "<img width=32 height=32 src=thumbdown32.png>"
			      "</td>"

			      "<td width=30px></td>"			      
			      */

			      );

		// . get this for seeing if its null below
		// . it will be NULL if is expired!
		Msg40 *msg40 = &(st->m_msg40);
		er = msg40->getExpandedResult(0);

		// if you tag an event just tag that one instance of it
		// which is determined by the "start time" of that instance
		int32_t start_time = 0;
		if ( mr &&
		     mr->ptr_eventDateIntervals &&
		     mr->size_eventDateIntervals ) {
			Interval *ii = (Interval *)mr->ptr_eventDateIntervals;
			start_time = ii->m_a;
		}
		// this overrides otherwise
		if ( er ) start_time = er->m_timeStart;

		// thumbs up / thumbs down and going
		if ( mr ) 
			printIcons2 ( sb , 
				      si , 
				      mr ,
				      0 , // iconId ,
				      mr->m_eventHash64 ,
				      mr->m_docId ,
				      mr->m_eventId ,
				      start_time ,
				      NULL );
	}


	if ( ! printMenuJunk && si->m_includeCachedCopy && !si->m_frame &&
	     mr && ! mr->m_eventExpired ) {
		//
		// BEGIN print email a friend link
		//
		// get the search result
		Msg40 *msg40 = &(st->m_msg40);
		ExpandedResult *er = msg40->getExpandedResult(0);
		char mbuf[512];
		SafeBuf msg(mbuf,512);
		char *ampm = "am";
		int32_t h1 = er->m_hour1;
		if ( h1 == 12 ) ampm = "pm";
		if ( h1 > 12 ) {
			ampm = "pm";
			h1 -= 12;
		}
		// store title of event url encoded
		char tbuf[512];
		SafeBuf tb(tbuf,512);
		printEventTitle ( tb , mr , st );
		// filter quotes out so they do not terminated our <a href>
		tb.filterQuotes();
		// filter out tags from highlighted query terms in the title
		tb.filterTags();
		// encode the query
		//char qebuf[512];
		//SafeBuf qe(qebuf,512);
		//qe.urlEncode(si->m_query);
		msg.safePrintf("Just wanted to let you know I was going to "
			       "\"%s\" on %s %"INT32" at %"INT32":%02"INT32" %s."
			       "\n\nPlease join me!\n\n"
			       "More info at \n\n"
			       "%s?id=%"UINT64".%"UINT64""
			       , tb.getBufStart()
			       , getMonthName(er->m_month1)
			       //,(int32_t)er->m_month1+1
			       ,(int32_t)er->m_dayNum1
			       ,(int32_t)h1
			       ,(int32_t)er->m_min1 
			       , ampm
			       , APPHOSTUNENCODED
			       , mr->m_docId
			       , mr->m_eventHash64
			       );
		// false->make spaces into %20
		msg.urlEncode(false);
		// this one url encodes the title
		char tebuf[512];
		SafeBuf tbenc(tebuf,512);
		tbenc.safeStrcpy ( tb.getBufStart() );
		// false->make spaces into %20
		tbenc.urlEncode(false);
		sb.safePrintf ( "<td width=30px></td>" );
		sb.safePrintf ( "<td>"
				"<a href=\"mailto:?subject=Going%%20to%%20%s"
				"&amp;body=%s\">"
				"<img border=0 height=32 width=32 "
				"title=\"Email a friend\" "
				"src=/email32.png></a>"
				"&nbsp;" 
				, tbenc.getBufStart()
				, msg.getBufStart() );
		sb.safePrintf("</td>" );
		//
		// END email a friend link
		//
		

		//
		// BEGIN TWITTER LINK
		//
		msg.reset();
		msg.safePrintf("I am going to "
			       "\"%s\" on %s %"INT32" at %"INT32":%02"INT32" %s."
			       , tb.getBufStart()
			       , getMonthName(er->m_month1)
			       ,(int32_t)er->m_dayNum1
			       ,(int32_t)h1
			       ,(int32_t)er->m_min1 
			       , ampm 
			       );
		// encode this one
		msg.urlEncode();
		// a special format that we need to understand
		SafeBuf link;
		//"%s?q=%s&where=anywhere&icc=1"
		link.safePrintf ("%s?id=%"UINT64".%"UINT64""
				 , APPHOSTUNENCODED
				 , mr->m_docId
				 , mr->m_eventHash64
				 );
		link.urlEncode();

		sb.safePrintf("<td width=30px></td>"			      
			      
			      "<td valign=center>"
			      "<a href=\"https://twitter.com/intent/tweet?"
			      "text=%s&url=%s"
			      //"&via=EventGuru"
			      "\">"
			      "<img title=\"Tweet this\" border=0 "
			      "width=32 height=32 src=twitter32.png>"
			      "</a>"
			      "</td>"
			      
			      , msg.getBufStart()
			      , link.getBufStart()
			      );
		//
		// END TWITTER LINK
		//

		// 
		// BEGIN FACEBOOK INVITER LINK
		//
		msg.reset();
		msg.safePrintf("I am going to "
			       "\"%s\" on %s %"INT32" at %"INT32":%02"INT32" %s. "
			       "Join me! ... Info link: %s?id=%"UINT64".%"UINT64""
			       , tb.getBufStart()
			       , getMonthName(er->m_month1)
			       ,(int32_t)er->m_dayNum1
			       ,(int32_t)h1
			       ,(int32_t)er->m_min1 
			       , ampm 
			       , APPHOSTUNENCODED
			       , mr->m_docId
			       , mr->m_eventHash64
			       );
		// encode all quotes so we can pass as javascript
		SafeBuf msg2; 
		// encode # --> true
		msg2.htmlEncode( msg.getBufStart() , msg.length() ,true,0);
		sb.safePrintf("<td width=30px></td>");
		sb.safePrintf("<td valign=center>");
		sb.safePrintf("<a>");
		sb.safePrintf("<img border=0 "
			      "title=\"Tell friends on Facebook\" "
			      "width=32 height=32 src=talk32.png" );
		if ( st->m_msgfb.m_fbId )
			sb.safePrintf(" onclick=\"sendRequestVia"
				      "MultiFriendSelector("
				      "'%s');"
				      "return false;\""
				      , msg2.getBufStart());
		else
			sb.safePrintf (" onclick=\"needLogin();\"");
		sb.safePrintf(">");
		sb.safePrintf("</a>"
			      "</td>"
			      );
		//
		// END FACEBOOK INVITER LINK
		//
	}

	if ( ! printMenuJunk && si->m_includeCachedCopy && !si->m_frame && mr){
		// get user lat/lon
		float slat = si->m_userLat;
		float slon = si->m_userLon;
		if ( slat == NO_LATITUDE ) {
			slat = si->m_ipLat;
			slon = si->m_ipLon;
		}
		if ( slat == NO_LATITUDE ) {
			slat = si->m_cityLat;
			slon = si->m_cityLon;
		}
		// destination
		float dlat = mr->m_eventGeocoderLat;
		float dlon = mr->m_eventGeocoderLon;
		//
		// DRIVING DIRECTIONS
		//
		sb.safePrintf("<td width=30px></td>");
		sb.safePrintf("<td valign=center>");
		sb.safePrintf("<a href=\"http://maps.google.com/maps?saddr=%.05f,%.05f&daddr=%.05f,%.05f&hl=en\">"
			      //sll=0,0&sspn=148.13559,11.25&geocode=FcpYFwIdAKKk-Q%3BFXITIgIdzvBP-w&mra=ls&glp=1&t=m&z=3\">"
			      , slat
			      , slon 
			      , dlat
			      , dlon
			      );
		sb.safePrintf("<img border=0 "
			      "title=\"Driving Directions\" "
			      "width=32 height=32 src=drive32.png>" );
		sb.safePrintf("</a>"
			      "</td>"
			      );
		//
		// END DRIVING DIRECTIONS
		//


		sb.safePrintf("</tr>"
			      "</table>"
			      "</div>" 
			      );

		//
		// CACHED PAGE DIV #4 (SHADOW)
		//
		sb.safePrintf("<div "
			      "style=\""
			      "overflow:hidden;"
			      "position:absolute;"
			      "background-color:black;"
			      "opacity:0.15;"
			      "z-index:6;"
			      // for ie8 and earlier
			      //"filter:alpha(opacity=40);"
			      "top:300px;"
			      "right:0px;"
			      "left:0px;"
			      "height:5px;\""
			      "></div>");

		// then the page content div
		printPageContent ( sb , st );
	}


	// end the widgetwidth div
	if ( si->m_widget ) {
		sb.safePrintf("</div>");
	}


	if ( si->m_includeCachedCopy )
		printWelcomePopup ( sb , &st->m_msgfb );

	// end the xml response
	if ( si->m_raw )
		sb.safePrintf ( "</response>");

	// filter xml
	if ( si->m_raw > 0 ) {
		// . filter anything < 0x20 to 0x20 to keep XML legal
		// . except \t, \n and \r, they're ok
		// . gotta set "f" down here in case it realloc'd the buf
		unsigned char *f    = (unsigned char *)sb.getBufStart();
		unsigned char *fend = (unsigned char *)sb.getBuf();
		for ( ; f < fend ; f++ ) 
			if ( *f < 0x20  &&
			     *f != '\t' &&
			     *f != '\n' &&
			     *f != '\r'  ) 
				*f = 0x20;
	}

	// . add the stat
	// . use brown for the stat
	g_stats.addStat_r ( delta             ,
			    st->m_startTime , 
			    nowms,//gettimeofdayInMilliseconds(),
			    color ,
			    STAT_QUERY );
	// add to statsdb, use # of qterms as the value/qty
	g_statsdb.addStat ( 0,"query",st->m_startTime,nowms,
			    st->m_q.m_numTerms);
	//delete (st);
	// . now encapsulate it in html head/tail and send it off
	// . the 0 means browser caches the page for however int32_t its set for
	g_stats.m_numSuccess++;

	if ( si->m_emailFormat && st->m_numResultsPrinted <= 0 ) {
		// do not send them anything if no results found
		sb.purge();
	}

	// this is for sending out emails for "Just for You" algo
	if ( st->m_emailCallback ) {
		st->m_emailResultsBuf->safeMemcpy ( &sb );
		st->m_emailCallback ( st->m_emailState );
		return true;
	}

	if ( st->m_providedBuf ) {
		st->m_providedBuf->safeMemcpy ( &sb );
		st->m_providedCallback ( st->m_providedState );
		return true;
	}

	// . this inserts cookie, "fbid"
	// . always returns true
	return sendPageBack ( st->m_socket,si,&sb,&st->m_msgfb,&st->m_hr);
			      //&st->m_interestCookies);
}

bool printMetaContent ( Msg40 *msg40 , int32_t i ,
			State7 *st, SafeBuf& sb , bool inXml ) {
	// store the user-requested meta tags content
	SearchInput *si = &st->m_si;
	char *pp      =      si->m_displayMetas;
	char *ppend   = pp + si->m_displayMetasLen;
	Msg20 *m = msg40->m_msg20[i];//getMsg20(i);
	Msg20Reply *mr = m->m_r;
	char *dbuf    = mr->ptr_dbuf;//msg40->getDisplayBuf(i);
	int32_t  dbufLen = mr->size_dbuf-1;//msg40->getDisplayBufLen(i);
	char *dbufEnd = dbuf + (dbufLen-1);
	char *dptr    = dbuf;
	//bool  printedSomething = false;
	// loop over the names of the requested meta tags
	while ( pp < ppend && dptr < dbufEnd ) {
		// . assure last byte of dbuf is \0
		//   provided dbufLen > 0
		// . this insures sprintf and gbstrlen won't
		//   crash on dbuf/dptr
		if ( dbuf [ dbufLen-1 ] != '\0' ) {
			log(LOG_LOGIC,"query: Meta tag buffer has no \\0.");
			break;
		}
		// skip initial spaces
		while ( pp < ppend && is_wspace_a(*pp) ) pp++;
		// break if done
		if ( ! *pp ) break;
		// that's the start of the meta tag name
		char *ss = pp;
		// . find end of that meta tag name
		// . can end in :<integer> -- specifies max len
		while ( pp < ppend && ! is_wspace_a(*pp) && 
			*pp != ':' ) pp++;
		// save current char
		char  c  = *pp;
		char *cp = pp;
		// NULL terminate the name
		*pp++ = '\0';
		// if ':' was specified, skip the rest
		if ( c == ':' ) while ( pp < ppend && ! is_wspace_a(*pp)) pp++;
		// print the name
		//int32_t sslen = gbstrlen ( ss   );
		//int32_t ddlen = gbstrlen ( dptr );
		int32_t ddlen = dbufLen;
		//if ( p + sslen + ddlen + 100 > pend ) continue;
		// newspaperarchive wants tags printed even if no value
		// make sure the meta tag isn't fucked up
		for ( int32_t ti = 0; ti < ddlen; ti++ ) {
			if ( dptr[ti] == '"' ||
			     dptr[ti] == '>' ||
			     dptr[ti] == '<' ||
			     dptr[ti] == '\r' ||
			     dptr[ti] == '\n' ||
			     dptr[ti] == '\0' ) {
				ddlen = ti;
				break;
			}
		}
#ifndef _CLIENT_
		if ( ddlen > 0 ) {
#endif
			// ship it out
			if ( inXml ) {
				sb.safePrintf ( "\t\t<display name=\"%s\">"
					  	"<![CDATA[", ss );
				sb.utf8Encode ( dptr, ddlen );
				//sb.utf8Encode(dptr,ddlen);
				sb.safePrintf ( "]]></display>\n" );
			}
			// otherwise, print in light gray
			else {
				sb.safePrintf("<font color=#c62939>"
					      "<b>%s</b>: ", ss );
				sb.utf8Encode ( dptr, ddlen );
				//sb.utf8Encode ( dptr, ddlen );
				sb.safePrintf ( "</font><br>" );
			}
#ifndef _CLIENT_
		}
#endif
		// restore tag name buffer
		*cp = c;
		// point to next content of tag to display
		dptr += ddlen + 1;
	}
	return true;
}

/*
// POST all of the query parms
char *printPost ( char *p , char *pend, State7 *st , char *name , int32_t s , 
		  int32_t n , char *qe , char *button ) {
	// do not breech
	if ( p + gbstrlen(name) + gbstrlen(qe) + 200 >= pend ) return p;
	p += sprintf ( p , 
		       "<form method=post action=/search name=%s>\n"
		       "<input type=hidden name=s value=%"INT32">\n"
		       "<input type=hidden name=q value=\"%s\">\n"
		       ,name,s, qe);

	// then get the rest non-default parms from the SearchInput class
	SearchInput *si = &st->m_si;
	if ( p + si->m_postParmsLen < pend )
		p += sprintf ( p , "%s" , si->m_postParms );
	
	// add sites if we have them
	if ( si->m_sites ) { 
		if ( p + si->m_sitesLen + 200 >= pend ) return p;
		p += sprintf(p, "<input type=hidden name=sites "
				"value=\"%s\">\n",
				si->m_sites);
	}
	
	if ( p + gbstrlen(button) + 200 >= pend ) return p;
	p += sprintf (p,"<input type=submit value=\"%s\" border=0>\n</form>\n",
		      button);

	return p;
}
*/

bool printEventTitle ( SafeBuf &sb , Msg20Reply *mr , State7 *st ) {
	// assume none
	char *str    = "Untitled";
	int32_t sslen;
	// scan the summary lines for the title line
	char *p     = mr->ptr_eventSummaryLines;
	char *pend  = p + mr->size_eventSummaryLines;
	bool first = true;
	bool hadPunct = false;
	char *end;
	SearchInput *si = NULL;
	if ( st ) si = &st->m_si;
	int32_t minmax;

	for ( ; p < pend ; ) {
		// get it
		SummaryLine *ss = (SummaryLine *)p;
		// advance
		p += ss->m_totalSize;
		// skip if not title
		if ( ! ( ss->m_flags & EDF_TITLE ) ) continue;
		// store ...
		if ( ! first && 
		     ! hadPunct &&
		     ! sb.safeMemcpy(" | ",3) ) goto failed2;
		// store space otherwise
		if ( ! first && 
		     hadPunct &&
		     ! sb.pushChar(' ') ) goto failed2;
		// no longer first
		first = false;
		// get length
		str    = ss->m_buf;
		sslen = gbstrlen(str);
		// skip over enums like "13. South Valley Library"
		// or 1. Cherry Hills Library
		if ( is_digit(str[0]) ) {
			char *p = str;
			char *pend = str + sslen;
			for ( ; p<pend ; p++ ) {
				if ( ! is_digit(*p) ) break;
			}
			// need a dot
			if ( (*p == '.'||*p==')') && is_wspace_a(p[1]) ) {
				p += 2;
				str  = p;
				sslen = pend - p;
			}
			if ( *p == '.' || *p==')' ) {
				p += 1;
				str  = p;
				sslen = pend - p;
			}
		}


		minmax = 25;
		// narrow widget? be careful, if this makes the widget
		// wider then the whole thing gets too wide and EVERY summary
		// get truncated by the iframe's internal div tag. we can't
		// even be 100% sure we won't overflow even with this much
		// smaller minmax, but at least reasonable widget widths
		// are covered for the default font.
		// "opportunities"
		if ( si && si->m_widget ) minmax = 12;

		// copy but truncate with ... if no space for a while
		if ( ! sb.truncateLongWords ( str,sslen,minmax)) goto failed2;
	
		// did we end on some punct?
		end = str + sslen - 1;
		for ( ; end > str ; end-- ) {
			// skip white space at end
			if ( is_wspace_a(*end) ) continue;
			// is this a letter or punct?
			if ( is_punct_a(*end) ) hadPunct = true;
			break;
		}
		// get more title sentences
		continue;
	failed2:
		log("query: Query failed in title.");
		// this may fail if it fails to realloc the 
		// safe buf because out of memory
		g_stats.m_numFails++;
		return false;
	}
	return true;
}

bool printEventSummary ( SafeBuf &sb , Msg20Reply *mr , int32_t width ,
			 int32_t minusFlags , int32_t requiredFlags , 
			 State7 *st , ExpandedResult *er ,
			 int32_t maxChars ) {

	SearchInput *si = NULL;
	if ( st ) si = &st->m_si;
	bool showDates = false;
	if ( si ) showDates = si->m_showDates;
	if ( si && ! si->m_widget ) showDates = true;

	// store entire summary into here first
	//SafeBuf ttt;
	bool needHyphen = false;
	if ( showDates && 
	     ! si->m_includeCachedCopy &&
	     ! si->m_raw ) { // is xml?
		printTodRange ( sb,st,er);
		needHyphen = true;
	}
	int32_t lastWordPos = -2;
	bool lastEndedInPunct = true;
	// scan the summary lines
	char *p     = mr->ptr_eventSummaryLines;
	char *pend  = p + mr->size_eventSummaryLines;
	bool  first = true;
	char *xt;
	for ( ; p < pend ; ) {
		// get it
		SummaryLine *ss = (SummaryLine *)p;
		// advance
		p += ss->m_totalSize;
		// skip if title
		if ( ss->m_flags & EDF_TITLE ) continue;

		// skip if flags do not match provided flags
		if ( ss->m_flags & minusFlags ) continue;
		// skip if does not have required flags
		if ( requiredFlags && !(ss->m_flags&requiredFlags) ) continue;

		// show bullet. this is a 2nd merged summary from
		// Msg40.cpp's summary merge algo. so separate it out
		// with a bullet
		//if ( ss->m_flags & SL_BULLET ) 
		//	ttt.safePrintf(" &bull; ");
		//else if ( ss->m_alnumPosA == lastWordPos ) {
		//	// if previous guy had no punctuation to
		//	// end him, add a period
		//	//if ( ! lastEndedInPunct )
		//	//	ttt.pushChar('.');
		//	// and always add a space
		//	ttt.pushChar(' ');
		//}
		// store ...
		//else if ( ! first )
		//	//ttt.safeMemcpy("... ",4);
		//	ttt.safeMemcpy(". ",2);
		

		// separate sentences with space
		if ( lastWordPos >= 0 )
			sb.pushChar(' ');

		// no longer first
		first = false;
		// save this
		lastWordPos = ss->m_alnumPosB;
		// get length
		char *str    = ss->m_buf;
		int32_t  sslen = gbstrlen(str);
		bool addPeriod;
		char *sbend;
		char *sbxt;

		if ( sslen && needHyphen ) {
			needHyphen = false;
			sb.safePrintf(" &nbsp; ");
		}

		// it is already pre-highlighted in XmlDoc.cpp
		// ::getEventSummary() so just print out
		//if ( ! sb.safeMemcpy ( str , sslen ) ) goto failed;

		int32_t minmax = 25;
		// narrow widget? be careful, if this makes the widget
		// wider then the whole thing gets too wide and EVERY summary
		// get truncated by the iframe's internal div tag. we can't
		// even be 100% sure we won't overflow even with this much
		// smaller minmax, but at least reasonable widget widths
		// are covered for the default font.
		if ( si && si->m_widget ) minmax = 10;
		// copy but truncate with ... if no space for a while
		if ( ! sb.truncateLongWords ( str , sslen,minmax)) goto failed;
		
		// . did we end in punct?
		// . point to the utf8 char BEFORE "p". returns NULL
		//   if could not get one
		xt = getPrevUtf8Char ( str+sslen,str );
		if ( xt ) lastEndedInPunct = is_punct_utf8(xt);

		// stop the period after the "..."
		sbend = sb.getBuf();
		sbxt = getPrevUtf8Char ( sbend,sb.getBufStart() );
		if ( sbxt && is_punct_utf8(sbxt) ) lastEndedInPunct = true;

		addPeriod = false;
		if ( ! lastEndedInPunct ) addPeriod = true;

		// if ended on a tag from query or date highlighting, skip
		// over tag to see if ended on period
		if ( *xt == '>' ) {
			char *x = str + sslen - 1;
			for ( ; x >= str ; x-- ) if ( *x == '<' ) break;
			// now back that up
			if ( x > str && ! is_punct_a(x[-1]) ) 
				addPeriod = true;
		}

		// terminate with period if did not end on punct
		if ( addPeriod ) {
			if ( ! sb.pushChar('.') ) 
				goto failed;
		}

		// always end each sentence on a \n so zak knows the
		// different sentences so we can make "Jan\nSunday\n3\n"
		// be pronounces as "Sunday January 3rd".
		// no, now the textarea that PageSubmit.cpp inserts the
		// description into can not deal with these \n's.
		//if ( ! sb.pushChar('\n') ) 
		//	goto failed;

		continue;
	failed:
		log("query: Query failed in summary.");
		// this may fail if it fails to realloc the 
		// safe buf because out of memory
		g_stats.m_numFails++;
		return false;
	}
	// then format ttt according to width and store into sb
	//brformat ( &ttt , &sb , width , maxChars );
	return true;
}

bool printEventCountdown ( SafeBuf &sb , Msg20Reply *mr , Msg40 *msg40 ,
			   ExpandedResult *er ,
			   bool onlyPrintIfSoon ,
			   bool isXml ,
			   State7 *st ) {
	// get current time in UTC (no DST)
	//int32_t now ;
	//if ( msg40->m_clockSet ) now = msg40->m_clockSet;
	//else                     now = getTimeGlobal();
	//now += msg40->m_clockOff ;
	int32_t now = msg40->m_r.m_minTime;
	if ( now == -1 ) now = msg40->m_r.m_nowUTC;
	// get this
	//int32_t timeZoneOffset = mr->m_timeZoneOffset * 3600;
	
	// int16_tcuts
	//int32_t startFromNow = mr->m_startFromNow;
	//int32_t endFromNow   = mr->m_endFromNow;
	//int32_t nextStart = mr->m_nextStart;
	//int32_t nextEnd   = mr->m_nextEnd;
	
	//bool storeHours = mr->m_flags3 & F3_STORE_HOURS;
	//storeHours = false;
	bool storeHours = false;
	//if ( mr->m_eventFlags & EV_STORE_HOURS    ) storeHours = true;
	// for dates that telescope to the store hours and have no
	// specific daynum(s)
	//if ( mr->m_eventFlags & EV_SUBSTORE_HOURS ) storeHours = true;
	// say yes if it has an end time >= 4 hrs from start
	//if ( mr->m_nextEnd - mr->m_nextStart >= 4*3600 )
	//	storeHours = true;
	// no, no, if we have an endpoint that does not equal start point
	// then assume that is store hours... otherwise the sorting of the
	// search results appears bad since that is how timedb scores
	// them.
	storeHours = false;
	if ( mr->m_prevStart != mr->m_prevEnd ||
	     mr->m_nextStart != mr->m_nextEnd )
		storeHours = true;

	// deal with expanded results
	int32_t nextStart = mr->m_nextStart;
	int32_t nextEnd   = mr->m_nextEnd;
	int32_t prevStart = mr->m_prevStart;
	int32_t prevEnd   = mr->m_prevEnd;
	if ( er ) { // && er->m_timeStart != nextStart ) {
		nextStart = er->m_timeStart;
		nextEnd   = er->m_timeEnd;
		// what would happen if we did this?
		prevStart = nextStart;
		prevEnd   = nextEnd;
		//prevStart = -1;
		//prevEnd   = -1;
	}

	int32_t saveLen = sb.m_length;
	SearchInput *si = &st->m_si;	
	char *tag = "b";
	if ( si->m_sortBy != SORTBY_TIME &&
	     si->m_sortBy != 0 )
		tag = "font";
	// take out for now
	tag = "font";

	if ( ! isXml ) 
		sb.safePrintf("<i>" );
			      //"<%s style=\"color:black;"
			      //"background-color:"
			      //"lightyellow\">" ,
			      //tag );

	if ( ! isXml )
		sb.safePrintf("<span class=countdown>");

	int32_t startLen = sb.m_length;

	bool status;
	status=printEventCountdown2 ( sb, 
				      si,
				      now,
				      mr->m_timeZoneOffset , // * 3600,
				      mr->m_useDST,
				      nextStart,
				      nextEnd,
				      prevStart,
				      prevEnd ,
				      storeHours ,
				      onlyPrintIfSoon );

	int32_t endLen = sb.m_length;

	//if ( ! isXml ) sb.safePrintf("</%s></i>",tag);

	if ( ! isXml ) // si && si->m_widget )
		sb.safePrintf("</span>");

	if ( ! isXml ) sb.safePrintf("</i>");

	// print nothing if nothing was printed
	if ( startLen == endLen ) sb.m_length = saveLen;

	return status;
}

bool printEventCountdown2 ( SafeBuf &sb ,
			    SearchInput *si,
			    int32_t nowUTC ,
			    int32_t timeZoneOffset ,
			    char useDST,
			    int32_t nextStart ,
			    int32_t nextEnd ,
			    int32_t prevStart ,
			    int32_t prevEnd ,
			    bool storeHours ,
			    bool onlyPrintIfSoon ) {

	// this is too confusing if they set a specific date on the calendar
	if ( si->m_clockSet ) return true;

	if ( timeZoneOffset != UNKNOWN_TIMEZONE &&
	     ( timeZoneOffset < -13 ||
	       timeZoneOffset > 13 ) ) { 
		char *xx=NULL;*xx=0; }
	// 2hrs old is basically over
	//if ( nextStart < nowUTC - 2*3600 ) 
	//	sb.safePrintf("Event is over.");
	// otherwise, close, might be able to make it late
	//else if ( nextStart < nowUTC )
	//	sb.safePrintf("Event started %"INT32" minutes ago",
	//		      (nextStart-nowUTC)/60);
	if ( nextStart == -1 && prevEnd <= nowUTC )
		sb.safePrintf("Event is over.");
	else if ( prevStart != -1 &&
		  prevStart < nowUTC &&
		  prevEnd   > nowUTC ) {
		int32_t togo = (prevEnd - nowUTC);
		sb.safePrintf("Event ends in ");
		printCountdown3 ( togo , sb , false );
	}
	else if ( storeHours ) {
		// need these
		//int32_t prevStart = mr->m_prevStart;
		//int32_t prevEnd   = mr->m_prevEnd;
		//int32_t nextEnd   = mr->m_nextEnd;
		// is the store currently closed?
		bool closed = true;
		if ( prevStart < nowUTC && prevEnd > nowUTC )
			closed = false;
		char *ss; 
		int32_t  ot;
		if ( closed ) {
			ss = "Opens in "; 
			ot = nextStart;
			//end = endFromNow;
		}
		else          {
			ss = "Closes in "; 
			ot = prevEnd;
			nextEnd = -1;
		}
		// use this now, more generic!!!
		ss = "in ";

		int32_t saved = sb.m_length;
		
		// print that
		sb.safePrintf("%s",ss);
		

		int32_t start = sb.m_length;

		// now print the countdown
		int32_t togo = ot - nowUTC;
		printCountdown3 ( togo , sb , true );

		int32_t end = sb.m_length;

		// do not print out anything if >= 1 day away
		if ( start == end ) {
			sb.m_length = saved;
			return true;
		}
		
		char tbuf[64];
		// get daynum for now
		struct tm *tt = gmtime(&nowUTC);
		// get nowdaynum
		int32_t nowdaynum = tt->tm_yday;
		// make it full
		int32_t full = ot + timeZoneOffset * 3600 ;
		// add in DST
		if ( useDST && getIsDST(ot,timeZoneOffset) )
			// add in an hour if it is
			full += 3600;
		// then break it down into hours/mins/secs
		tt = gmtime(&full);
		// get that day of year
		int32_t otdaynum = tt->tm_yday;
		// print "on Nov 15" if different day than now
		if ( onlyPrintIfSoon ) return true;
		if ( nowdaynum != otdaynum ) {
			strftime(tbuf,64,"%a, %b %e",tt);
			sb.safePrintf(" on %s",tbuf);
		}
		// print @ or from
		//if ( end > start ) sb.safePrintf(" from ");
		//else               sb.safePrintf(" @ ");
		if ( ! closed ) sb.safePrintf(" @ ");
		else            sb.safePrintf(" from ");
		// print start tod
		if ( tt->tm_min == 0 ) strftime(tbuf,64,"%l%P",tt);
		else                   strftime(tbuf,64,"%l:%M%P",tt);
		sb.safePrintf("%s",tbuf);
		// print end time if valid
		if ( nextEnd >= 0 ) {
			int32_t fullEnd = nextEnd + timeZoneOffset * 3600 ;
			// add in DST
			if ( useDST && getIsDST(nextEnd,timeZoneOffset) )
				// add in an hour if it is
				fullEnd += 3600;
			// break it down
			tt = gmtime(&fullEnd);
			if ( tt->tm_min == 0 )
				strftime(tbuf,64," - %l%P",tt);
			else
				strftime(tbuf,64," - %l:%M%P",tt);
			sb.safePrintf("%s",tbuf);
		}
		
	}
	else {
		// now print the countdown
		int32_t togo = nextStart - nowUTC;
		int32_t days  = togo / 86400; togo -= days  * 86400;
		int32_t hours = togo /  3600; togo -= hours * 3600;
		int32_t mins  = togo /    60;
		// do not print out at all if over 24 hours away!
		if ( days >= 1 && onlyPrintIfSoon ) return true;
		if ( hours >= 10 && onlyPrintIfSoon ) return true;
		if ( days  < 0 ) return true;
		if ( mins  < 0 ) return true;
		if ( hours < 0 ) return true;
		// singular plural
		char *ds = "day";
		if ( days != 1 ) ds = "days";
		char *hs = "hour";
		if ( hours != 1 ) hs = "hours";
		char *ms = "minute";
		if ( mins != 1 ) ms = "minutes";
		if ( days >= 10 )
			sb.safePrintf("in %"INT32" %s",days,ds);
		else if ( days >= 1 && hours )
			sb.safePrintf("in %"INT32" %s %"INT32" "
				      "%s", days,ds,hours,hs);
		else if ( days >= 1 && hours==0 )
			sb.safePrintf("in %"INT32" %s",
				      days,ds);
		else if ( hours > 0 )
			sb.safePrintf("in %"INT32" %s %"INT32" %s",
				      hours,hs,mins,ms);
		else
			sb.safePrintf("in %"INT32" %s",
				      mins,ms);
		
		// print end tod
		char ebuf[64];
		ebuf[0] = '\0';
		int32_t fullEnd = nextEnd + timeZoneOffset * 3600 ;
		// add in DST
		if ( useDST && getIsDST(nextEnd,timeZoneOffset) )
			// add in an hour if it is
			fullEnd += 3600;
		struct tm *tt;
		// print end time if valid
		if ( nextEnd > nextStart &&
		     nextEnd - nextStart < 24*3600 &&
		     // skip if it is midnight
		     (fullEnd % (24*3600)) ) {
			tt = gmtime(&fullEnd);
			if ( tt->tm_hour == 0 && 
			     tt->tm_min  == 0 &&
			     tt->tm_sec  == 0 ) ;
			else if ( tt->tm_min == 0 ) {
				strftime(ebuf,64," - %l%P",tt);
			}
			else {
				strftime(ebuf,64," - %l:%M%P",tt);
			}
		}
		
		// make it full
		int32_t fullStart = nextStart + timeZoneOffset * 3600 ;
		// adjust for DST
		if ( useDST && getIsDST(nextStart,timeZoneOffset ) )
			fullStart += 3600;

		// print " on Fri Nov 15 @ 1:30 pm
		if ( onlyPrintIfSoon ) return true;
		char tbuf[64];
		tt = gmtime(&fullStart);
		strftime(tbuf,64,"%a, %b %e",tt);
		sb.safePrintf(" on %s",tbuf);
		
		// print @ or from
		if ( ebuf[0] ) sb.safePrintf(" from ");
		else           sb.safePrintf(" @ ");
		// print start tod
		if ( tt->tm_min == 0 ) strftime(tbuf,64,"%l%P",tt);
		else                   strftime(tbuf,64,"%l:%M%P",tt);
		sb.safePrintf("%s",tbuf);
		// end tod
		sb.safePrintf("%s",ebuf);
	}
	return true;
}

void printCountdown3 ( int32_t togo , SafeBuf& sb , bool onlyPrintIfSoon ) {

	int32_t days  = togo / 86400; togo -= days  * 86400;
	int32_t hours = togo /  3600; togo -= hours * 3600;
	int32_t mins  = togo /    60;
	// singular plural
	char *ds = "day";
	if ( days != 1 ) ds = "days";
	char *hs = "hour";
	if ( hours != 1 ) hs = "hours";
	char *ms = "minute";
	if ( mins != 1 ) ms = "minutes";

	if ( days >= 1 && onlyPrintIfSoon ) return;
	if ( hours >= 10 && onlyPrintIfSoon ) return;
	if ( days  < 0 ) return;
	if ( mins  < 0 ) return;
	if ( hours < 0 ) return;

	if ( days >= 10 )
		sb.safePrintf("%"INT32" %s",days,ds);
	else if ( days >= 1 )
		sb.safePrintf("%"INT32" %s %"INT32" "
			      "%s", days,ds,hours,hs);
	else if ( hours >= 1 )
		sb.safePrintf("%"INT32" %s %"INT32" %s",
			      hours,hs,mins,ms);
	else
		sb.safePrintf("%"INT32" %s",
			      mins,ms);
}

bool printEventAddress ( SafeBuf &sb , char *addr , SearchInput *si ,
			 double *lat , double *lon , bool xml ,
			 float zipLat ,
			 float zipLon ,
			 double eventGeocoderLat,
			 double eventGeocoderLon ,
			 char *eventBestPlaceName ,
			 Msg20Reply *mr ) {
	//char *addr = mr->ptr_eventAddr;
	//int32_t addrSize = mr->size_eventAddr;
	// parse this up
	char *name1  ;
	char *name2  ;
	char *suite  ;
	char *street ;
	char *city   ;
	char *adm1   ;
	char *zip    ;
	char *country;
	//int32_t  tzoff ;
	// this now makes "city" etc point into a static buffer, beware!
	setFromStr2 ( addr, &name1 , &name2, &suite, &street, &city, 
		      &adm1, &zip, &country,lat , lon );//, &tzoff );
	//if ( !sb.safePrintf("<font color=green>")) return false;
	//if ( ! sb.safePrintf ("<font style="
	//		      "\"color:black;"
	//		      "background-color:"
	//		      "lightgreen\">" ) )
	//	return false;
	// pick the best lat/lon point if we have a choice
	if ( si ) { // && ! xml ) {
		float ulat = 999.0;
		float ulon = 999.0;
		// small cities are like zip codes
		if ( si->m_cityLat != 999.0 ) {
			ulat = si->m_cityLat;
			ulon = si->m_cityLon;
		}
		if ( si->m_zipLat != 999.0 ) {
			ulat = si->m_zipLat;
			ulon = si->m_zipLon;
		}
		// this is more precise if available. the user's exact lat/lon
		if ( si->m_userLat != 999.0 ) {
			ulat = si->m_userLat;
			ulon = si->m_userLon;
		}
		// use the exact event lat
		double tmpLat = *lat;
		double tmpLon = *lon;
		// prefer geocoder lat though
		if ( eventGeocoderLat != NO_LATITUDE &&
		     eventGeocoderLon != NO_LONGITUDE ) {
			tmpLat = eventGeocoderLat;
			tmpLon = eventGeocoderLon;
		}
		// but if not available, try zip code lat/lon centroid
		if ( tmpLat == 999.0 && zipLat != 999.0 ) {
			tmpLat = zipLat;
			tmpLon = zipLon;
		}
		// if sorting by distance, print distance first
		if ( //msg40->m_r.m_sortByDist &&
		    ulat != 999.0 &&
		    tmpLat != 999.0 &&
		    tmpLon != 999.0 ) {
			// normalize this stuff, it is not in the
			// address placedb record
			//tmpLat += 180.0;
			//tmpLon += 180.0;
			float latDiff = ulat - tmpLat;
			float lonDiff = ulon - tmpLon;
			if ( latDiff < 0 ) latDiff *= -1;
			if ( lonDiff < 0 ) lonDiff *= -1;
			// one degree is 69.0 miles
			latDiff *= 69.0;
			lonDiff *= 69.0;
			// add them for now... more like driving distance
			float dist = latDiff + lonDiff;
			char *tag1 = "<b>";
			char *tag2 = "</b>";
			if ( si->m_sortBy != SORTBY_DIST ) {
				tag1 = "";
				tag2 = "";
			}
			if ( ! xml ) {
				//sb.safePrintf ("<font style="
				//	       "\"color:black;"
				//	       "background-color:"
				//	       "lightgreen\">" );
				sb.safePrintf("%s%.01f%s",tag1,dist,tag2);
				//sb.safePrintf("</font>");
				sb.safePrintf(" mi away at ");
			}
			else {
				// manhattan distance
				sb.safePrintf("\t\t<drivingMilesAway>%.01f"
					      "</drivingMilesAway>\n",dist);
				//sb.safePrintf("\t\t<milesAway>%.01f"
				//	      "</milesAway>\n",dist);
			}
		}
	}

	if ( ! xml ) {

		bool printedSomething = false;

		// print formatted now
		if ( eventBestPlaceName ) { // name1[0] ) 
			sb.safePrintf("%s" , eventBestPlaceName ) ; // name1 );
			printedSomething = true;
		}

		//if ( eventBestPlaceName )
		//	sb.safePrintf(" &bull; ");

		if ( suite[0] ) {
			if ( printedSomething ) sb.safePrintf(", ");
			printedSomething = true;
			sb.safePrintf("%s",suite);
			//sb.safePrintf(", ");
		}

		// is street a lat/lon?
		//bool printedStreet = false;
		if ( street[0] && ! ( mr->m_eventFlags & EV_LATLONADDRESS ) ){
			if ( printedSomething ) sb.safePrintf(", ");
			printedSomething = true;
			sb.safePrintf("%s",street);
			//printedStreet = true;
		}

		//if ( printedStreet ) sb.safePrintf(", ");

		if ( city[0] ) {
			if ( printedSomething ) sb.safePrintf(", ");
			printedSomething = true;
			sb.safePrintf("%s",city);
		}

		//if ( adm1[0] && city[0] ) sb.safePrintf(", ");

		uint8_t crid = 0;
		if ( country[0] ) crid = getCountryId ( country );

		if ( adm1[0] ) {
			if ( printedSomething ) sb.safePrintf(", ");
			printedSomething = true;
			// convert adm1 code to its official name. need
			// a country though!
			char buf[3];
			buf[0] = adm1[0];
			buf[1] = adm1[1];
			buf[2] = 0;
			int64_t ph64 = getWordXorHash(buf);
			PlaceDesc *pd=getPlaceDesc(ph64,PDF_STATE,crid,NULL,0);
			if ( pd ) {
				char *str = pd->getOfficialName();
				sb.safePrintf("%s",str);
			}
			// print the code if we couldn't get an official name
			else {
				// get state name
				char buf[10];
				to_upper3_a(adm1,2,buf);
				buf[2] = 0;
				sb.safePrintf("%s",buf);
			}
		}

		//if ( zip[0] && adm1[0] ) sb.safePrintf("  ");

		if ( zip[0] ) {
			if ( printedSomething ) sb.safePrintf(", ");
			printedSomething = true;
			sb.safePrintf("%s",zip);
		}

		// print country if not country of searcher's IP address 
		if ( crid && crid != si->m_ipCrid ) {
			if ( printedSomething ) sb.safePrintf(", ");
			printedSomething = true;
			// the official name "France" or "United States"
			const char *cstr = g_countryCode.getName(crid);
			sb.safePrintf ( " %s", cstr );
		}
		// ok, all done, the rest is for xml only!!!
		return true;
	}

	if ( ! xml ) return true;

	// print out the new tags now
	char *c1 = "<![CDATA[";
	char *c2 = "]]>";

	// do our new venue thing here
	

	// do this the new way now for easier and understandable parsing
	if ( eventBestPlaceName ) // name1[0] )
		sb.safePrintf("\t\t<eventVenue>%s%s%s</eventVenue>\n",
			      c1,eventBestPlaceName,c2);//name1,c2);
	else
		sb.safePrintf("\t\t<eventVenue></eventVenue>\n");

	if ( name2[0] )
		sb.safePrintf("\t\t<eventVenue2>%s%s%s</eventVenue2>\n",
			      c1,name2,c2);
	if ( suite[0] )
		sb.safePrintf("\t\t<eventSuite>%s%s%s</eventSuite>\n",
			      c1,suite,c2);
	if ( street[0] )
		sb.safePrintf("\t\t<eventStreet>%s%s%s</eventStreet>\n",
			      c1,street,c2);
	if ( city[0] )
		sb.safePrintf("\t\t<eventCity>%s%s%s</eventCity>\n",
			      c1,city,c2);
	if ( adm1[0] ) {
		// get state two letters capitalized
		char buf[10];
		to_upper3_a(adm1,2,buf);
		buf[2] = 0;
		sb.safePrintf("\t\t<eventState>%s%s%s</eventState>\n",
			      c1,buf,c2);
	}
	if ( zip[0] )
		sb.safePrintf("\t\t<eventZip>%s%s%s</eventZip>\n",
			      c1,zip,c2);

	// if none, assume us
	char *cs = country;
	if ( ! cs || ! cs[0] ) cs = "US";

	// get state two letters capitalized
	char buf[10];
	to_upper3_a(cs,2,buf);
	buf[2] = 0;
	sb.safePrintf("\t\t<eventCountry>%s%s%s</eventCountry>\n",
		      c1,buf,c2);

	return true;
}
		

/*
bool printEventCachedUrl ( SafeBuf &sb , Msg20Reply *mr , Msg20 *m ,
			   char *qe , char *coll ) {
	char ebuf[65];
	ebuf[0] = '\0';
	//Msg20 *m = msg40->m_msg20[ix];//getMsg20(i);
	binToHex((uint8_t *)m->m_eventIdBits.m_bits,32,ebuf);
	ebuf[64] = '\0';
	sb.safePrintf(	"/get?"
			"q=%s&c=%s&d=%"INT64"&cnsp=0&eb=%s",
			qe , coll , 
			mr->m_docId , ebuf ); 
	return true;
}
*/

/*
bool printEventTags ( Msg20Reply *mr , SafeBuf& sb ) {

	char *dbuf    = mr->ptr_dbuf;
	int32_t  dbufLen = mr->size_dbuf-1;
	char *dbufEnd = dbuf + (dbufLen-1);
	char *dptr    = dbuf;
	//bool  printedSomething = false;
	// loop over the names of the requested meta tags
	while ( dptr && dbufLen > 0 && dptr < dbufEnd ) {
		// . assure last byte of dbuf is \0
		//   provided dbufLen > 0
		// . this insures sprintf and gbstrlen won't
		//   crash on dbuf/dptr
		if ( dbuf [ dbufLen ] != '\0' ) {
			log(LOG_LOGIC,"query: Meta tag buffer has no \\0.");
			break;
		}
		// print it
		sb.safePrintf("\t\t<eventTag>%s</eventTag>\n",dptr);
		// skip it . i.e. skip "2,entertainment\0"
		dptr += gbstrlen(dptr);
	}
	//return p;
	return true;
}
*/


bool printLocalTime ( SafeBuf &sb , State7 *st ) {

	// get stuff
	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;

	int32_t now;
	int32_t clockSet = si->m_clockSet;
	if ( clockSet  ) now = clockSet;
	else             now = msg40->m_r.m_nowUTC;

	//int32_t now = m_r.m_minTime;
	//if ( now == -1 ) now = m_r.m_nowUTC;

	// get timezone offset
	int32_t timeZoneOffset = msg40->m_timeZoneOffset;
	if ( timeZoneOffset == UNKNOWN_TIMEZONE ) 
		timeZoneOffset = si->m_guessedTimeZone;
	if ( timeZoneOffset == UNKNOWN_TIMEZONE ) {
		log("query: timezone is unknown in printlocaltime. "
		    "defaulting to eastern.");
		// default to eastern
		timeZoneOffset = -5;
	}
	// we add 100 to it to indicate DST not being used
	//bool useDST = true;
	//if ( timeZoneOffset >= 50 ) { timeZoneOffset -= 100; useDST = false;}

	// see if we are in daylights savings time now
	bool isDST = getIsDST ( now , timeZoneOffset );

	// do the dominating search results use DST now?
	bool useDST = msg40->m_useDST;

	// now get name from offset
	char *name = "unk";
	if ( useDST && isDST ) {
		if ( timeZoneOffset == -4 ) name = "ADT";
		if ( timeZoneOffset == -5 ) name = "EDT";
		if ( timeZoneOffset == -6 ) name = "CDT";
		if ( timeZoneOffset == -7 ) name = "MDT";
		if ( timeZoneOffset == -8 ) name = "PDT";
		if ( timeZoneOffset == -9 ) name = "HDT";
		if ( timeZoneOffset == -10) name = "HST";
		if ( timeZoneOffset == -11) name = "SDT";// samoa
		if ( timeZoneOffset == -12) name = "YST"; // yankee tz
		// change the time too
		timeZoneOffset += 1;
	}
	else {
		if ( timeZoneOffset == -4 ) name = "AST";
		if ( timeZoneOffset == -5 ) name = "EST";
		if ( timeZoneOffset == -6 ) name = "CST";
		if ( timeZoneOffset == -7 ) name = "MST";
		if ( timeZoneOffset == -8 ) name = "PST";
		if ( timeZoneOffset == -9 ) name = "HDT";
		if ( timeZoneOffset == -10) name = "HST";
		if ( timeZoneOffset == -11) name = "SST";// samoa
		if ( timeZoneOffset == -12) name = "YDT"; // yankee tz
	}
	
	// add the timezone offset to it (3600 secs in an hour)
	time_t tt2 = now + timeZoneOffset * 3600;
	// re-get the timestruct in our local timezone
	struct tm *timeStruct = gmtime ( &tt2 );

	// reserve 100 bytes, return false with g_errno set on error
	if ( ! sb.reserve ( 100 ) ) return false;
	// fill this
	char *p    = sb.getBuf(); // m_currentTimeString;
	char *pend = p + 96;
	char *pstart = p;

	// print year if not current time
	//if ( m_r.m_minTime != -1 )
	if ( si->m_clockSet )
		// the dow, month and day
		p += strftime ( p , pend - p , "%a %b %e %Y, ",timeStruct);
	else
		// the dow, month and day
		p += strftime ( p , pend - p , "%a %b %e, ",timeStruct);


	// now we can print the time
	p += strftime ( p , pend - p ,  "%l:%M %p",timeStruct);

	// get timezone name from m_timeZoneOffset

	// the time zone
	p += sprintf ( p , " %s",name );
	// update size
	sb.incrementLength ( p - pstart );
	// success
	return true;
}

/*
bool printCalendar ( SafeBuf &sb , State7 *st ) {

	// get stuff
	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;

	int32_t now = msg40->m_r.m_nowUTC;

	// we do not call getResults() if ?cities=1 so now is "0"
	if ( ! now ) now = getTimeGlobal();

	int32_t clockSet = si->m_clockSet;

	printCalendar2 ( sb , clockSet , now );
}
*/

// "now" is from the msg40 we used
bool printCalendar ( SafeBuf &sb , State7 *st ) {

	// get stuff
	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;

	int32_t now = msg40->m_r.m_nowUTC;

	// we do not call getResults() if ?cities=1 so now is "0"
	if ( ! now ) now = getTimeGlobal();

	int32_t clockSet = si->m_clockSet;
	//if ( clockSet ) now = clockSet;
	//else            now = msg40->m_r.m_nowUTC;

	// get timezone offset
	int32_t timeZoneOffset = msg40->m_timeZoneOffset;
	if ( timeZoneOffset == UNKNOWN_TIMEZONE ) 
		timeZoneOffset = si->m_guessedTimeZone;
	if ( timeZoneOffset == UNKNOWN_TIMEZONE ) {
		log("query: timezone is unknown in printcalendar. "
		    "defaulting to eastern.");
		// default to eastern
		timeZoneOffset = -5;
	}
	// incorporate timezone
	now += timeZoneOffset * 3600;
	if ( clockSet ) clockSet += timeZoneOffset * 3600;

	// parse it up
	struct tm *timeStruct = gmtime ( &now );

	// get month number (0 to 11)
	int32_t thisMonth = timeStruct->tm_mon; // 0-11
	int32_t thisDOW   = timeStruct->tm_wday; // 0-6
	int32_t thisDay   = timeStruct->tm_mday;
	int32_t thisYear  = timeStruct->tm_year + 1900;


	// show this as the calendar
	int32_t showDOW   = thisDOW;
	int32_t showDay   = thisDay;
	int32_t showMonth = thisMonth;
	int32_t showYear  = thisYear;

	// if clock was set use that for showing if no display given
	int32_t clockDay   = 0;
	int32_t clockMonth = -1;
	int32_t clockYear = 0;
	if ( clockSet ) {
		// override current time!
		struct tm *timeStruct2 = gmtime ( &clockSet );
		clockDay   = timeStruct2->tm_mday;
		clockMonth = timeStruct2->tm_mon;
		clockYear  = timeStruct2->tm_year+1900;
	}


	int32_t displayMonth = si->m_displayMonth;
	int32_t displayYear  = si->m_displayYear;
	bool showCursor = true;
	if ( displayMonth >= 0 && 
	     displayMonth <= 11 &&
	     displayYear >= 2000 &&
	     displayYear <= 2050 ) {
		struct tm ttt;
		ttt.tm_year = displayYear - 1900;
		ttt.tm_mon  = displayMonth; // 0 to 11
		ttt.tm_mday = 1;
		ttt.tm_hour = 0;
		ttt.tm_min  = 0;
		ttt.tm_sec  = 0;
		time_t then = mktime ( &ttt );
		// now get the day of week from that (0 to 6)
		showDOW = getDOW ( then ) - 1;
		// now re-set our crap
		showMonth      = displayMonth;
		showYear       = displayYear;
		showDay        = ttt.tm_mday;
		if ( showMonth != clockMonth ||
		     showYear  != clockYear  )
			showCursor     = false;
	}

	// cycle it back
	int32_t tmp = showDOW - (showDay - 1);
	// mod 7
	int32_t firstDayOfWeek = tmp % 7;
	// make it positive
	if ( firstDayOfWeek < 0 ) firstDayOfWeek += 7;


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

	// the clockset input parm
	sb.safePrintf("<input type=hidden name=clockset "
		      "id=clockset value=%"INT32">",
		      si->m_clockSet );
	sb.safePrintf("<input type=hidden id=displayyear "
		      "name=displayyear value=%"INT32">",
		      si->m_displayYear );
	sb.safePrintf("<input type=hidden id=displaymonth "
		      "name=displaymonth value=%"INT32">",
		      si->m_displayMonth );

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

	int32_t fs = 16;
	int32_t cp = 3;
	if ( si->m_igoogle ) {
		fs = 13;
		cp = 2;
	}

	// print out calendar header
	sb.safePrintf("<table cellspacing=0 cellpadding=%"INT32" style=color:%s;"
		      "font-size:%"INT32"px;>"
		      "<tr>"
		      "<td><font size=-2>"
		      "<a "
		      "style=\"color:black\" "
		      "onclick=\"reloadResults(1,"
		      "'&displayyear=%"INT32"&displaymonth=%"INT32"');\">%s</a>"
		      "</font></td>"
		      "<td colspan=5><center>%s %"INT32"</center></td>"
		      "<td><font size=-2>"
		      "<a "
		      "style=\"color:black\" "
		      "onclick=\"reloadResults(1,"
		      "'&displayyear=%"INT32"&displaymonth=%"INT32"');\">%s</a>"
		      "</font></td>"
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
		      , cp
		      , "black" // GRADFONT
		      , fs
		      // cal now for prev month
		      , prevYear
		      , prevMonth
		      , prevStr
		      , str 
		      , showYear
		      // cal now for next month
		      , nextYear
		      , nextMonth
		      , nextStr );
	bool printed = false;
	int32_t count = 1;
	// print out days of the week header
	for ( int32_t i = 0 ; i < 35 ; i++ ) {
		if ( i % 7 == 0 )
			sb.safePrintf("<tr>");
		// is it today?
		if ( count == thisDay && i>= firstDayOfWeek && showCursor )
			sb.safePrintf("<td class=cal "
				      "style=background-color:yellow;"
				      "color:black");
		else if ( count == clockDay && 
			  clockMonth == showMonth && 
			  clockYear == showYear &&
			  i>= firstDayOfWeek )
			sb.safePrintf("<td class=cal "
				      "style=background-color:red");
		else
			sb.safePrintf("<td class=cal");
		// do not start printing until first day of month
		if ( (i >= firstDayOfWeek || printed) &&
		     count <= daysInMonth ) {
			printed = true;
			//int32_t clockSet=getYearMonthStart(thisYear,thisMonth+1)
			int32_t clockSet2=getYearMonthStart(showYear,showMonth+1);
			// add day to it
			clockSet2 += (count-1) * 86400;
			// clockSet is in utc...
			clockSet2 -= timeZoneOffset * 3600;
			// end the <td>
			sb.safePrintf(" onclick=\""
				      // set hidden tag clockset val
				      // set all to gray if not yellow
				      // set clicked to red if not yellow
				      "setClock(this,'clockset',%"UINT32");"
				      "reloadResults(1);"
				      "\">",
				      clockSet2);
			// add two hours cuz i'm not sure!
			//clockSet += 2 * 3600;
			//sb.safePrintf("<a href=/?clockset=%"UINT32">%"INT32"</a>"
			sb.safePrintf("<a>%"INT32"</a>"
				      "</td>" 
				      //, clockSet
				      , count );
			count++;
		}
		else 
			sb.safePrintf("></td>");
		if ( (i+1) % 7 == 0 )
			sb.safePrintf("</tr>\n");
	}
	sb.safePrintf("<tr>"
		      "<td "
		      "onclick=\""
		      "document.getElementById('clockset').value=0;"
		      "document.getElementById('displaymonth').value=0;"
		      "document.getElementById('displayyear').value=0;"
		      //"resetCal();"
		      "reloadResults();\" "
		      "colspan=20>"
		      //"<br>"
		      "<center>"
		      "<font size=-1>"
		      "<a>"
		      "<b>"
		      "Use current time"
		      "</b>"
		      "</a>"
		      "</font>"
		      "</center>"
		      "</td>"
		      "</tr>" );
	sb.safePrintf("</table>");
	return true;
}

// print the balloon for a search result's address
bool printBalloon ( SafeBuf &sb,SearchInput *si,char letter,int32_t balloonId ) {
	// print nothing if not legit
	if ( letter < 'A' || letter > 'Z' ) return true;
	// get color string
	int32_t ci = letter - 'A';
	// num we have
	int32_t numColors = sizeof(s_mapColors)/sizeof(char *);
	// mod it
	ci = ci % numColors;
	// panic?
	if ( ci < 0 || ci >= numColors ) return true;
	// hide it?
	char *s = "";
	if ( si->m_map == 0 ) s = "display:none;";
	// use googles individual imgs for now
	return sb.safePrintf("<img id=balloon%"INT32" "
			     "style=\"padding-right:10px;%s\" "
			     "align=left width=20 height=34 src=\"http://maps.google.com/mapfiles/marker_%s%c.png\">",
			     balloonId,s,s_mapColors[ci],letter);
	// offset into the sprite
	//return sb.safePrintf("<img width=20 height=30 src=/markers.png "
	//		     // is this sprites?
	//		     "xpos=%"INT32">",
	//		     (int32_t)(letter - 'A') * 20);
}

// example: http://maps.google.com/maps/api/staticmap?size=280x256&maptype=roadmap&sensor=false&markers=size:medium%7Ccolor:gray%7Clabel:U%7C35.0844879%2C-106.6511383&markers=size:medium%7Ccolor:orange%7Clabel:A%7C35.123534%2C-106.546135&markers=size:medium%7Ccolor:red%7Clabel:B%7C35.19677%2C-106.703969&markers=size:medium%7Ccolor:yellow%7Clabel:C%7C35.150954%2C-106.559204&markers=size:medium%7Ccolor:purple%7Clabel:D%7C35.062734%2C-106.51036&markers=size:medium%7Ccolor:green%7Clabel:E%7C35.162739%2C-106.603008&markers=size:medium%7Ccolor:brown%7Clabel:F%7C35.062548%2C-106.446297&markers=size:medium%7Ccolor:black%7Clabel:G%7C35.179765%2C-106.669257&markers=size:medium%7Ccolor:white%7Clabel:H%7C35.120244%2C-106.617234&markers=size:medium%7Ccolor:orange%7Clabel:I%7C35.127949%2C-106.494848

// . returns false and sets g_errno on error
// . replaces PageGet.cpp essentially
bool printPageContent ( SafeBuf &sb , State7 *st ) {
	// get stuff
	Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;
	// get first msg20 reply
	int32_t nr = msg40->getNumResults();
	//a ssume none
	Msg20Reply *mr = NULL;
	// scan the non-expanded events i guess
	int32_t i; for ( i = 0 ; i < nr ; i++ ) {
		// get msg20
		mr = msg40->m_msg20[i]->m_r;
		// got it
		if ( mr ) break;
	}
	// none?
	if ( ! mr ) return true;

	// need to set this junk
	char ebuf[65];
	ebuf[0] = '\0';
	Msg20 *m = msg40->m_msg20[i];
	binToHex((uint8_t *)m->m_eventIdBits.m_bits,32,ebuf);
	ebuf[64] = '\0';
	
	//
	// CACHED PAGE DIV #5 (IFRAME)
	//
	// begin the iframe div for the page content  of the cached page
	if ( ! sb.safePrintf (
			      // this empty table is the same height as
			      // the first two divs, the div containing
			      // the summary and the div that is the middle
			      // black bar. this allows us to use
			      // height:100%% for the "sly" div here below
			      // and have it match up. we have to use 
			      // height:100% because msie doesn't like
			      // "bottom:0;". i think this is because the
			      // other two divs above us are absolute so
			      // their height is not contributing to the
			      // height:100% here.
			      //"<table style=height:300;><tr><td>"
			      //"</td></tr></table>"

			      //"</TABLE></div>"
			      "<div id=sly style=\""
			       // we can't use this for ipad
			       //"overflow:auto;"
			       // i guess we need this for ipad????
			       // NO! seems like we can't have overflow:auto
			       // and we need a div around the iframe for
			       // ipad to work right because it just
			       // ignores the iframe dimensions.
			       //"overflow-scrolling:touch;"
			      //"overflow-y:scroll;"
			      //"overflow-y:hidden;"
			      //"overflow-x:auto;"
			      // when this was overflow-auto it made the
			      // ipad seem to work better!
			      //"overflow:auto;"
			      "background-color:white;"//lightyellow;"
			      "position:absolute;"
			      //"position:relative;"
			      // 250 + 50 + 5
			      "top:300px;"
			      "bottom:0px;"
			      "right:0px;"
			      // msie doesn't understand bottom:0!
			      // BUT we can't do this because it creates
			      // a scrollbar for the whole page and we
			      // only want one scrollbar per div!! so set this
			      // div's height: using javascript for msie.
			      // i think the height:100%% is 
			      //"height:100%%;"
			      "left:0px;"
			      //"width:100%%;"
			      "\""
			      ">\n"

			      // this iframe contains the cached web page
			      "<iframe "
			      // this is REQUIRED for firefox and chrome,
			      // otherwise they suffer from the int16_t iframe
			      // like msie does. but msie can only do heights
			      // in pixels!
			      //"height=100%% "
			      //"height=500 "
			      // now that div hides "y" we need the iframe
			      // to do the scrolling
			      //"scrolling=yes "
			      // only firefox seems to render a useless
			      // scrollbar in the iframe. the primary div
			      // scrollbar is the one being used.
			      //"scrolling=no "
			      "style=\""
			      // i guess we need this for ipad
			      // NO! this does nothing...
			      //"overflow-scrolling:touch;"
			      //"position:relative;"
			      "border:0;"
			      "width:100%%;"
			      //"height:500;"
			      "height:100%%;"
			      // this is REQUIRED for firefox and chrome,
			      // otherwise they suffer from the int16_t iframe
			      // like msie does
			      //"height:100%%;"
			      "\" "
			      "src=\""
			      // the cached url
			      // cnsp=1 means do not print disclaimer
			      // "This is Gigablast's cached page..."
			      ))
		return false;

	// if we are facebook.
	//if ( (mr->m_eventFlags & EV_FACEBOOK) && 1==3)
	//if ( (mr->m_eventFlags & EV_FACEBOOK) )
	//	sb.safePrintf("%s", mr->ptr_ubuf );
	//else {
	sb.safePrintf("/get?"
		      "c=%s&d=%"INT64"&qh=1&cnsp=1&eb=%s&q="
		      ,si->m_coll2
		      ,mr->m_docId 
		      ,ebuf  );
	// the original query so we can highlight that
	if ( si->m_highlightQuery && 
	     si->m_highlightQuery[0] && 
	     ! sb.urlEncode ( si->m_highlightQuery  ) )
		return false;
	if ( ! sb.safePrintf ( "#gbscrolldown" ) )
		return false;
	//}

	// then the rest of the url
	if ( ! sb.safePrintf ( "\" "
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
			       "</div>" ))
		return false;
	return true;
}



// show like "7pm", not "every wednesday at 7pm"
bool printTodRange ( SafeBuf &sb , State7 *st , ExpandedResult *er ) {
	// get stuff
	//Msg40 *msg40 = &(st->m_msg40);
	SearchInput *si = &st->m_si;
	// forget it if sorting by distance
	if ( ! er ) return true;
	sb.safePrintf("<i><font color=green>");
	// print a span so they can format it better
	if ( si->m_widget ) sb.safePrintf("<span class=times>");
	// print just the tod, like "7pm" or "7pm-9pm"
	int32_t hour1 = er->m_hour1;
	char *s1 = "am";
	if ( hour1 > 12 ) { hour1 -= 12; s1 = "pm"; }
	if ( hour1 == 0 ) { hour1 = 12; s1 = "am"; }

	bool sameDay = false;

	// if same day then print ending tod
	if ( er->m_dayNum2 == er->m_dayNum1 &&
	     er->m_month2  == er->m_month1  &&
	     er->m_year2   == er->m_year1  )
		sameDay = true;

	// or lasts until 11am or less the next day, treat as an
	// "into the night" event
	if ( er->m_dayNum2 == er->m_dayNum1 + 1 &&
	     er->m_hour2   <= 11 &&
	     er->m_month2  == er->m_month1  &&
	     er->m_year2   == er->m_year1  )
		sameDay = true;

	// march31 9pm - april 1 2am
	int32_t nd = getNumDaysInMonth(er->m_month1,er->m_year1);
	int32_t nm = er->m_month1 + 1;
	int32_t ny = er->m_year1;
	if ( nm >= 12 ) { nm = 0; ny++; }
	if ( er->m_dayNum1 == nd &&
	     er->m_dayNum2 == 1 &&
	     er->m_hour2 <= 11 &&
	     er->m_month2 == nm &&
	     er->m_year2 == ny )
		sameDay = true;


	int32_t hour2 = er->m_hour2;
	char *s2 = "am";
	if ( hour2 > 12 ) { hour2 -= 12; s2 = "pm"; }
	if ( hour2 == 0 ) { hour2 = 12; s2 = "am"; }

	// print the first tod
	if ( ! sameDay )
		sb.safePrintf ( "%s %s %"INT32", "
				, getDOWName   ( er->m_dow1 )
				, getMonthName ( er->m_month1 )
				, (int32_t)er->m_dayNum1 );

	if ( er->m_min1 == 0 ) sb.safePrintf("%"INT32"%s",(int32_t)hour1,s1);
	else sb.safePrintf("%"INT32":%02"INT32"%s",(int32_t)hour1,(int32_t)er->m_min1,s1);

	// if the 2nd date is exactly the same?
	bool hasEndPoint = false;
	if ( ! sameDay ) hasEndPoint = true;
	if ( er->m_hour1 != er->m_hour2 ) hasEndPoint = true;
	if ( er->m_min1  != er->m_min2 ) hasEndPoint = true;
	

	// if there is no ending date then the two date points will be
	// equal, otherwise there is a range and we gotta print the hyphen
	if ( hasEndPoint ) sb.safePrintf(" - ");

	// otherwise print next day time
	if ( ! sameDay ) {
		sb.safePrintf ( "%s %s %"INT32", "
				, getDOWName   ( er->m_dow2 )
				, getMonthName ( er->m_month2 )
				, (int32_t)er->m_dayNum2 );
	}

	if ( hasEndPoint ) {
		if ( er->m_min2 == 0 ) 
			sb.safePrintf("%"INT32"%s",(int32_t)hour2,s2);
		else sb.safePrintf("%"INT32":%02"INT32"%s",(int32_t)hour2,
				   (int32_t)er->m_min2,s2);
	}

	// print a span so they can format it better
	if ( si->m_widget ) sb.safePrintf("</span>");
	sb.safePrintf("</font></i>");
	//sb.safePrintf("<br>\n");
	return true;
}

//
// PAGE BROWSE.HTML
//

class CityState {
public:
	int32_t m_pop;
	char *m_city;
	char *m_state;
};

// . mwells@titan:~/gigablast$ grep PPL places.txt | grep US |  awk -F"\t" '{print $15","$2","$11}' | sort -rn | head -500 | awk -F"," '{print "\t{"$1",\""$2"\",\""$3"\"},"}' > cities

static CityState s_cs[] = {
	{8008278,"New York City","NY"},
	{3694820,"Los Angeles","CA"},
	{2841952,"Chicago","IL"},
	{2027712,"Houston","TX"},
	{1517550,"Philadelphia","PA"},
	{1321045,"Phoenix","AZ"},
	{1256810,"San Antonio","TX"},
	{1223400,"San Diego","CA"},
	{1211704,"Dallas","TX"},
	{951270,"Detroit","MI"},
	{894943,"San Jose","CA"},
	{797557,"Jacksonville","FL"},
	{773283,"Indianapolis","IN"},
	{736836,"Columbus","OH"},
	{732072,"San Francisco","CA"},
	{678368,"Austin","TX"},
	{650100,"Memphis","TN"},
	{641608,"New South Memphis","TN"},
	{618119,"Fort Worth","TX"},
	{610892,"Baltimore","MD"},
	{598351,"Charlotte","NC"},
	{589141,"Boston","MA"},
	{579180,"Milwaukee","WI"},
	{571281,"South Boston","MA"},
	{569369,"Seattle","WA"},
	{563662,"El Paso","TX"},
	{554636,"Denver","CO"},
	{552433,"Washington","DC"},
	{540513,"Portland","OR"},
	{532950,"Oklahoma City","OK"},
	{530852,"Nashville","TN"},
	{518907,"Tucson","AZ"},
	{487378,"Albuquerque","NM"},
	{484674,"New Orleans","LA"},
	{482618,"Long Beach","CA"},
	{478434,"Las Vegas","NV"},
	{467898,"Sacramento","CA"},
	{465183,"Fresno","CA"},
	{449514,"Cleveland","OH"},
	{442028,"North Kansas City","MO"},
	{441545,"Kansas City","MO"},
	{425257,"Virginia Beach","VA"},
	{422908,"Atlanta","GA"},
	{399484,"Oakland","CA"},
	{396375,"Mesa","AZ"},
	{393049,"Tulsa","OK"},
	{390007,"Omaha","NE"},
	{382894,"Miami","FL"},
	{371657,"Honolulu","HI"},
	{367773,"Minneapolis","MN"},
	{360890,"Colorado Springs","CO"},
	{355244,"Wichita","KS"},
	{343054,"Santa Ana","CA"},
	{338759,"West Raleigh","NC"},
	{332969,"Arlington","TX"},
	{328014,"Anaheim","CA"},
	{324465,"Tampa","FL"},
	{320916,"Saint Louis","MO"},
	{319494,"Pittsburgh","PA"},
	{306974,"Toledo","OH"},
	{306382,"Cincinnati","OH"},
	{297554,"Riverside","CA"},
	{291389,"Bakersfield","CA"},
	{289580,"Stockton","CA"},
	{280123,"Newark","NJ"},
	{279557,"Buffalo","NY"},
	{277454,"Corpus Christi","TX"},
	{276393,"Aurora","CO"},
	{276093,"Raleigh","NC"},
	{274792,"Saint Paul","MN"},
	{272079,"Lexington-Fayette","KY"},
	{260283,"Anchorage","AK"},
	{253372,"Plano","TX"},
	{246316,"Saint Petersburg","FL"},
	{243639,"Louisville","KY"},
	{242072,"Lincoln","NE"},
	{240112,"Glendale","AZ"},
	{239757,"Henderson","NV"},
	{237681,"Jersey City","NJ"},
	{234403,"Norfolk","VA"},
	{234297,"Chandler","AZ"},
	{231769,"Greensboro","NC"},
	{231621,"Birmingham","AL"},
	{229715,"Fort Wayne","IN"},
	{225366,"Lexington","KY"},
	{224840,"Hialeah","FL"},
	{224625,"Madison","WI"},
	{223349,"Baton Rouge","LA"},
	{217219,"Garland","TX"},
	{216008,"Modesto","CA"},
	{215120,"Paradise","NV"},
	{213032,"Chula Vista","CA"},
	{211387,"Lubbock","TX"},
	{211354,"Rochester","NY"},
	{210769,"Laredo","TX"},
	{208414,"Akron","OH"},
	{207970,"Orlando","FL"},
	{204734,"Durham","NC"},
	{203201,"North Glendale","CA"},
	{202705,"Scottsdale","AZ"},
	{202615,"Fremont","CA"},
	{202381,"San Bernardino","CA"},
	{202142,"Reno","NV"},
	{200145,"Shreveport","LA"},
	{199184,"Chesapeake","VA"},
	{198325,"Montgomery","AL"},
	{197818,"Yonkers","NY"},
	{197262,"Spokane","WA"},
	{196957,"Tacoma","WA"},
	{195968,"Huntington Beach","CA"},
	{194973,"Glendale","CA"},
	{193852,"Grand Rapids","MI"},
	{193825,"Irving","TX"},
	{193300,"Winston-Salem","NC"},
	{193180,"Des Moines","IA"},
	{190886,"Richmond","VA"},
	{190274,"Mobile","AL"},
	{189297,"Irvine","CA"},
	{188608,"Sunrise Manor","NV"},
	{187235,"Oxnard Shores","CA"},
	{186470,"Columbus","GA"},
	{185360,"Arlington","VA"},
	{184217,"Little Rock","AR"},
	{181766,"Amarillo","TX"},
	{180150,"Newport News","VA"},
	{178026,"Salt Lake City","UT"},
	{177595,"Providence","RI"},
	{177216,"Worcester","MA"},
	{176696,"Jackson","MS"},
	{174276,"Aurora","IL"},
	{173198,"East New York","NY"},
	{172808,"Ontario","CA"},
	{172474,"Knoxville","TN"},
	{170829,"Gilbert","AZ"},
	{170510,"Fort Lauderdale","FL"},
	{170358,"Oxnard","CA"},
	{169160,"Fontana","CA"},
	{168927,"Santa Clarita","CA"},
	{168629,"Moreno Valley","CA"},
	{168557,"Rancho Cucamonga","CA"},
	{168015,"Brownsville","TX"},
	{167664,"Hollywood","CA"},
	{167232,"Garden Grove","CA"},
	{165015,"Spring Valley","NV"},
	{161029,"Oceanside","CA"},
	{159134,"Dayton","OH"},
	{158625,"Tempe","AZ"},
	{158368,"Tempe Junction","AZ"},
	{158216,"Huntsville","AL"},
	{157517,"Vancouver","WA"},
	{156183,"Pomona","CA"},
	{155554,"Chattanooga","TN"},
	{155118,"Santa Rosa","CA"},
	{154024,"East Chattanooga","TN"},
	{153583,"Tallahassee","FL"},
	{152954,"Corona","CA"},
	{152765,"Rockford","IL"},
	{152227,"Springfield","MA"},
	{151205,"Paterson","NJ"},
	{150443,"Springfield","MO"},
	{149080,"Overland Park","KS"},
	{148691,"Salinas","CA"},
	{147993,"East Hampton","VA"},
	{146922,"Salem","OR"},
	{146866,"Kansas City","KS"},
	{146437,"Hampton","VA"},
	{146136,"Metairie","LA"},
	{145987,"Boise","ID"},
	{145208,"Eugene","OR"},
	{144963,"Torrance","CA"},
	{144821,"Hollywood","FL"},
	{144696,"Pasadena","TX"},
	{144618,"Pasadena","CA"},
	{143850,"Naperville","IL"},
	{142489,"Metairie Terrace","LA"},
	{141830,"Syracuse","NY"},
	{141122,"Grand Prairie","TX"},
	{140772,"Lakewood","CO"},
	{140494,"Hayward","CA"},
	{140336,"Sioux Falls","SD"},
	{139090,"Bridgeport","CT"},
	{137427,"Pembroke Pines","FL"},
	{137119,"Escondido","CA"},
	{135353,"Fullerton","CA"},
	{135294,"Palmdale","CA"},
	{134957,"Joliet","IL"},
	{133517,"Warren","MI"},
	{131674,"Coral Springs","FL"},
	{131510,"Savannah","GA"},
	{131337,"Mesquite","TX"},
	{129548,"Lancaster","CA"},
	{129252,"Fort Collins","CO"},
	{128821,"Orange","CA"},
	{127942,"Thousand Oaks","CA"},
	{127273,"Alexandria","VA"},
	{126657,"Sterling Heights","MI"},
	{126499,"Sunnyvale","CA"},
	{125684,"Gainesville","FL"},
	{125626,"Concord","CA"},
	{125372,"El Monte","CA"},
	{125157,"East Los Angeles","CA"},
	{124922,"New Haven","CT"},
	{124775,"Fayetteville","NC"},
	{124019,"Hartford","CT"},
	{123913,"Elizabeth","NJ"},
	{122578,"McAllen","TX"},
	{121570,"Topeka","KS"},
	{121230,"North Stamford","CT"},
	{120792,"Carrollton","TX"},
	{120758,"Cedar Rapids","IA"},
	{120446,"Vallejo","CA"},
	{119417,"Simi Valley","CA"},
	{119371,"Port Saint Lucie","FL"},
	{119088,"Toms River","NJ"},
	{118967,"Waco","TX"},
	{117691,"Lansing","MI"},
	{117258,"Flint","MI"},
	{117083,"Stamford","CT"},
	{116278,"Columbia","SC"},
	{116158,"Inglewood","CA"},
	{115975,"Springfield","IL"},
	{115488,"North Las Vegas","NV"},
	{115474,"Evansville","IN"},
	{114247,"Abilene","TX"},
	{113810,"Ann Arbor","MI"},
	{113647,"Olathe","KS"},
	{113288,"Independence","MO"},
	{113112,"West Valley City","UT"},
	{113004,"North Peoria","IL"},
	{112936,"Peoria","IL"},
	{112647,"Roseville","CA"},
	{112197,"Lafayette","LA"},
	{111927,"Bellevue","WA"},
	{111862,"Downey","CA"},
	{111694,"Clarksville","TN"},
	{111485,"Beaumont","TX"},
	{110675,"East Independence","MO"},
	{109877,"Manchester","NH"},
	{109756,"West Covina","CA"},
	{109250,"Costa Mesa Mobile Home Estates","CA"},
	{109111,"Norwalk","CA"},
	{108724,"Costa Mesa","CA"},
	{108507,"Waterbury","CT"},
	{108364,"Peoria","AZ"},
	{108064,"Clearwater","FL"},
	{107431,"Visalia","CA"},
	{106038,"Antioch","CA"},
	{105764,"Provo","UT"},
	{105741,"Fairfield","CA"},
	{105519,"Allentown","PA"},
	{104791,"Richardson","TX"},
	{104690,"Burbank","CA"},
	{104658,"Thornton","CO"},
	{104293,"Pueblo","CO"},
	{104037,"Westminster","CO"},
	{103945,"Cary","NC"},
	{103760,"South Bend","IN"},
	{103469,"Lowell","MA"},
	{103041,"Richmond","CA"},
	{103009,"Killeen","TX"},
	{102548,"Edison","NJ"},
	{102286,"Cape Coral","FL"},
	{102250,"Santa Clara","CA"},
	{101732,"Arvada","CO"},
	{101565,"Highlands Ranch","CO"},
	{101523,"Rialto","CA"},
	{101382,"Cambridge","MA"},
	{101212,"Wichita Falls","TX"},
	{101031,"West Jordan","UT"},
	{101012,"Green Bay","WI"},
	{100975,"Denton","TX"},
	{100565,"Portsmouth","VA"},
	{100158,"Berkeley","CA"},
	{99922,"South Gate","CA"},
	{99801,"Erie","PA"},
	{99753,"Clinton","MI"},
	{99751,"Billings","MT"},
	{99049,"Portsmouth Heights","VA"},
	{98959,"Daly City","CA"},
	{98851,"Gresham","OR"},
	{98586,"Elgin","IL"},
	{98218,"Livonia","MI"},
	{97966,"Gary","IN"},
	{97782,"Midland","TX"},
	{97651,"McKinney","TX"},
	{96993,"Everett","WA"},
	{96930,"Davenport","IA"},
	{96930,"Centennial","CO"},
	{96769,"Ventura","CA"},
	{96749,"Compton","CA"},
	{96650,"Charleston","SC"},
	{96469,"Vacaville","CA"},
	{96355,"Rochester","MN"},
	{96287,"Mission Viejo","CA"},
	{96201,"Columbia","MD"},
	{95767,"Carson","CA"},
	{95694,"Norman","OK"},
	{95658,"Albany","NY"},
	{95170,"Dearborn","MI"},
	{94881,"Brockton","MA"},
	{94807,"El Cajon","CA"},
	{94717,"Sandy Springs","GA"},
	{94497,"Elk Grove","CA"},
	{94281,"High Point","NC"},
	{94273,"Macon","GA"},
	{94083,"New Bedford","MA"},
	{93982,"Kenosha","WI"},
	{93907,"Lewisville","TX"},
	{93794,"West Albany","NY"},
	{93060,"Orem","UT"},
	{93000,"Fall River","MA"},
	{92707,"Vista","CA"},
	{92695,"Waukegan","IL"},
	{92424,"Wilmington","NC"},
	{92103,"Fargo","ND"},
	{91752,"Lawton","OK"},
	{91685,"Redding","CA"},
	{91521,"Boulder","CO"},
	{91415,"Columbia","MO"},
	{91256,"Odessa","TX"},
	{91168,"Roanoke","VA"},
	{91070,"Rancho Carlsbad Trailer Park","CA"},
	{90878,"Tyler","TX"},
	{90761,"Lakeland","FL"},
	{90615,"Plymouth","MA"},
	{90165,"Brandon","FL"},
	{89982,"San Mateo","CA"},
	{89868,"Westminster","CA"},
	{89715,"Pompano Beach","FL"},
	{89691,"Lynn","MA"},
	{89575,"Sandy Hills","UT"},
	{89430,"Citrus Heights","CA"},
	{89326,"Miami Beach","FL"},
	{89168,"Quincy","MA"},
	{88714,"Murrieta","CA"},
	{88653,"Round Rock","TX"},
	{88405,"Yakima","WA"},
	{88386,"Alhambra","CA"},
	{88338,"Santa Monica","CA"},
	{88256,"San Angelo","TX"},
	{88071,"Warwick","RI"},
	{87083,"Hawthorne","CA"},
	{87039,"Nashua","NH"},
	{86875,"Greeley","CO"},
	{86825,"Canton","MI"},
	{86681,"Santa Barbara","CA"},
	{86324,"Clovis","CA"},
	{85779,"Sunrise","FL"},
	{85778,"Whittier","CA"},
	{85739,"Plantation","FL"},
	{85535,"Broken Arrow","OK"},
	{85480,"Santa Maria","CA"},
	{85406,"Sandy City","UT"},
	{85145,"Temecula","CA"},
	{84851,"Newton","MA"},
	{84833,"Trenton","NJ"},
	{84801,"Westland","MI"},
	{84800,"Duluth","MN"},
	{84700,"Murfreesboro","TN"},
	{84662,"Sparks","NV"},
	{84530,"East Norwalk","CT"},
	{84316,"Yuma","AZ"},
	{83496,"Victorville","CA"},
	{83314,"Lawrence","KS"},
	{83291,"Longmont","CO"},
	{83248,"Sioux City","IA"},
	{83216,"Tracy","CA"},
	{83026,"Kendall","FL"},
	{82951,"Norwalk","CT"},
	{82926,"Cranston","RI"},
	{82648,"Beaverton","OR"},
	{82605,"Davie","FL"},
	{82565,"Parma","OH"},
	{82320,"Hillsboro","OR"},
	{82276,"Kent","WA"},
	{82115,"Lakewood","CA"},
	{82103,"West Palm Beach","FL"},
	{82008,"Deltona","FL"},
	{81985,"Fort Smith","AR"},
	{81383,"Bloomington","MN"},
	{81269,"Troy","MI"},
	{81184,"Cicero","IL"},
	{80815,"Federal Way","WA"},
	{80690,"South Suffolk","VA"},
	{80689,"Boca Raton","FL"},
	{80566,"Baldwin Park","CA"},
	{80412,"Camden","NJ"},
	{80347,"Livermore","CA"},
	{80245,"Clifton","NJ"},
	{80184,"Las Cruces","NM"},
	{79955,"Farmington Hills","MI"},
	{79904,"San Leandro","CA"},
	{79816,"Tuscaloosa","AL"},
	{79799,"Reading","PA"},
	{79641,"North Charleston","SC"},
	{79413,"Palm Bay","FL"},
	{79263,"Racine","WI"},
	{79157,"Buena Park","CA"},
	{78883,"Hammond","IN"},
	{78702,"Newport Beach","CA"},
	{78565,"Danbury","CT"},
	{78533,"College Station","TX"},
	{78324,"Ogden","UT"},
	{78247,"Carlsbad","CA"},
	{78160,"Canton","OH"},
	{77951,"Silver Spring","MD"},
	{77904,"Casas Adobes","AZ"},
	{77868,"Decatur","IL"},
	{77540,"Youngstown","OH"},
	{77408,"Catalina Foothills","AZ"},
	{77387,"Cheektowaga","NY"},
	{77218,"Roswell","GA"},
	{77015,"Napa","CA"},
	{76828,"Southfield","MI"},
	{76733,"Lake Forest","CA"},
	{76326,"Melbourne","FL"},
	{75920,"Longview","TX"},
	{75868,"Allen","TX"},
	{75847,"Bellflower","CA"},
	{75794,"Albany","GA"},
	{75777,"Sugar Land","TX"},
	{75737,"Waterford","MI"},
	{75670,"Champaign","IL"},
	{75527,"The Woodlands","TX"},
	{75494,"Chino","CA"},
	{75311,"Evanston","IL"},
	{75261,"Somerville","MA"},
	{74995,"Arlington Heights","IL"},
	{74965,"Pawtucket","RI"},
	{74230,"Upland","CA"},
	{74138,"Kalamazoo","MI"},
	{74099,"Shelby","MI"},
	{73915,"Edmond","OK"},
	{73710,"Bethlehem","PA"},
	{73640,"Hesperia","CA"},
	{73469,"Bellingham","WA"},
	{73451,"Schaumburg","IL"},
	{73220,"Bolingbrook","IL"},
	{73206,"Scranton","PA"},
	{73064,"Nampa","ID"},
	{72786,"New Rochelle","NY"},
	{72739,"Miramar","FL"},
	{72619,"Merced","CA"},
	{72609,"Lawrence","MA"},
	{72523,"Town 'n' Country","FL"},
	{72397,"Lynwood","CA"},
	{72239,"Plymouth","MN"},
	{71969,"Redwood City","CA"},
	{71745,"Saint Joseph","MO"},
	{71666,"Largo","FL"},
	{71455,"New Britain","CT"},
	{71356,"Wilmington","DE"},
	{71329,"West Gulfport","MS"},
	{71253,"Bloomington","IN"},
	{71127,"Gulfport","MS"},
	{71025,"Frisco","TX"},
	{70757,"Greenville","NC"},
	{70706,"Bloomington","IL"},
	{70700,"Lees Summit","MO"},
	{70565,"Chico","CA"},
	{70475,"Cherry Hill","NJ"},
	{70443,"Alameda","CA"},
	{70309,"Lake Charles","LA"},
	{70308,"Wyoming","MI"},
	{70287,"Redlands","CA"},
	{70112,"Appleton","WI"},
	{69721,"Kenner","LA"},
	{69715,"Missouri City","TX"},
	{69697,"San Marcos","CA"},
	{69679,"Avondale","AZ"},
	{69638,"Medford","OR"},
	{69621,"Bryan","TX"},
	{69604,"The Hammocks","FL"},
	{69530,"Union City","CA"},
	{69225,"Tamiami","FL"},
	{69205,"Santa Fe","NM"},
	{69078,"Spring Hill","FL"},
	{69039,"Hemet","CA"},
	{68895,"Rochester Hills","MI"},
	{68845,"Passaic","NJ"},
	{68795,"Asheville","NC"},
	{68728,"Tustin","CA"},
	{68390,"East Orange","NJ"},
	{68268,"Mountain View","CA"},
	{68254,"Weston","FL"},
	{68137,"Mount Vernon","NY"},
	{68112,"Waukesha","WI"},
	{68015,"Jacksonville","NC"},
	{67994,"Pontiac","MI"},
	{67953,"Folsom","CA"},
	{67940,"Turlock","CA"},
	{67920,"Lorain","OH"},
	{67869,"Gastonia","NC"},
	{67828,"Palatine","IL"},
	{67791,"Redondo Beach","CA"},
	{67595,"Brooklyn Park","MN"},
	{67341,"Baytown","TX"},
	{66910,"Framingham","MA"},
	{66869,"Union City","CA"},
	{66787,"Chino Hills","CA"},
	{66752,"Hoover","AL"},
	{66618,"Flower Mound","TX"},
	{66610,"West Hartford","CT"},
	{66532,"Pleasanton","CA"},
	{66442,"Boynton Beach","FL"},
	{66332,"Pico Rivera","CA"},
	{66291,"Laguna Niguel","CA"},
	{66239,"Deerfield Beach","FL"},
	{66215,"Ellicott City","MD"},
	{66121,"Union City","NJ"},
	{65955,"Indio","CA"},
	{65842,"Fountainbleau","FL"},
	{65836,"Waterloo","IA"},
	{65820,"Manteca","CA"},
	{65806,"Davis","CA"},
	{65756,"Bend","OR"},
	{65517,"West Lynchburg","VA"},
	{65511,"Delray Beach","FL"},
	{65413,"Framingham Center","MA"},
	{65327,"Muncie","IN"},
	{65269,"Lynchburg","VA"},
	{65225,"Centreville","VA"},
	{65179,"Walnut Creek","CA"},
	{65088,"Taylor","MI"},
	{64864,"Fayetteville","AR"},
	{64778,"Yorba Linda","CA"},
	{64720,"Germantown","MD"},
	{64661,"Daytona Beach","FL"},
	{64502,"Palm Harbor","FL"},
	{64327,"Montebello","CA"},
	{64325,"Springfield","OH"},
	{64271,"Apple Valley","CA"},
	{64227,"Skokie","IL"},
	{64218,"Eagan","MN"},
	{64170,"Iowa City","IA"},
	{64090,"Rio Rancho","NM"},
	{63831,"Huntington Park","CA"},
	{63685,"Kissimmee","FL"},
	{63677,"Suffolk","VA"},
	{63652,"Old Greenwich","CT"},
	{63484,"North Bergen","NJ"},
	{63319,"Eden Prairie","MN"},
	{63193,"Monterey Park","CA"},
	{63164,"Missoula","MT"},
	{63159,"Harlingen","TX"},
	{63142,"Portland","ME"},
	{63096,"Saint Clair Shores","MI"},
	{63031,"Carol City","FL"},
	{63017,"Oshkosh","WI"},
	{62852,"Eau Claire","WI"},
	{62838,"Pittsburg","CA"},
	{62813,"Castro Valley","CA"},
	{62778,"Lodi","CA"},
	{62636,"Milpitas","CA"},
	{62528,"West Coon Rapids","MN"},
	{62500,"Camarillo","CA"},
	{62490,"National City","CA"},
	{62308,"Layton","UT"},
	{62272,"San Clemente","CA"},
	{62231,"Lafayette","IN"},
	{62158,"Kennewick","WA"},
	{62080,"Jackson","TN"},
	{62044,"Victoria","TX"},
	{62011,"Saint Charles","MO"},
	{61894,"League City","TX"},
	{61888,"Rapid City","SD"},
	{61791,"Kendale Lakes","FL"},
	{61741,"Dothan","AL"},
	{61675,"Janesville","WI"},
	{61659,"Dundalk","MD"},
	{61607,"Coon Rapids","MN"},
	{61585,"Park Encinitas Trailer Park","CA"},
	{61486,"Mission","TX"},
	{61465,"Haverhill","MA"},
	{61445,"Hamilton","OH"},
	{61360,"Marietta","GA"},
	{61338,"Saint George","UT"},
	{61323,"Irvington","NJ"},
	{61211,"Dale City","VA"},
	{61101,"Greenwich","CT"},
	{61076,"Reston","VA"},
	{61033,"Bristol","CT"},
	{60915,"Gardena","CA"},
	{60687,"Pharr","TX"},
	{60646,"Rockville","MD"},
	{60592,"Concord","NC"},
	{60509,"Edinburg","TX"},
	{60280,"Schenectady","NY"},
	{60062,"Gaithersburg","MD"},
	{59902,"Maple Grove","MN"},
	{59878,"Bayonne","NJ"},
	{59861,"Lauderhill","FL"},
	{59847,"Hamden","CT"},
	{59766,"Rock Hill","SC"},
	{59654,"La Habra","CA"},
	{59618,"Loveland","CO"},
	{59580,"Bossier City","LA"},
	{59492,"Burnsville","MN"},
	{59431,"Falmouth","MA"},
	{59395,"Meriden","CT"},
	{59363,"West Allis","WI"},
	{59268,"Tamarac","FL"},
	{59185,"Rancho Cordova","CA"},
	{59154,"Saint Cloud","MN"},
	{59105,"Mount Pleasant","SC"},
	{59078,"North Little Rock","AR"},
	{59052,"Fairfield","CT"},
	{58952,"South Whittier","CA"},
	{58935,"Diamond Bar","CA"},
	{58912,"Lakewood","WA"},
	{58764,"Council Bluffs","IA"},
	{58619,"Jonesboro","AR"},
	{58603,"Johnson City","TN"},
	{58590,"Utica","NY"},
	{58570,"Waltham","MA"},
	{58555,"South San Francisco","CA"},
	{58472,"North Miami","FL"},
	{58284,"Taylorsville","UT"},
	{58265,"Burke","VA"},
	{58122,"South Vineland","NJ"},
	{58107,"Brentwood","NY"},
	{58014,"Encinitas","CA"},
	{57915,"Wayne","NJ"},
	{57764,"Warner Robins","GA"},
	{57570,"Royal Oak","MI"},
	{57566,"Flagstaff","AZ"},
	{57536,"Saginaw","MI"},
	{57512,"Annandale","VA"},
	{57496,"Anderson","IN"},
	{57341,"Carson City","NV"},
	{57297,"Frederick","MD"},
	{57275,"Paramount","CA"},
	{57260,"White Plains","NY"},
	{57244,"Tinley Park","IL"},
	{57185,"Dubuque","IA"},
	{57160,"Taunton","MA"},
	{57063,"Terre Haute","IN"},
	{56945,"Port Arthur","TX"},
	{56771,"Union","NJ"},
	{56716,"Arcadia","CA"},
	{56706,"Bismarck","ND"},
	{56606,"Ames","IA"},
	{56508,"Palo Alto","CA"},
	{56420,"Galveston","TX"},
	{56402,"Springdale","AR"},
	{56359,"Dearborn Heights","MI"},
	{56271,"Vineland","NJ"},
	{56255,"Pensacola","FL"},
	{56141,"Renton","WA"},
	{56133,"Fountain Valley","CA"},
	{56079,"Brookline","MA"},
	{56075,"Great Falls","MT"},
	{56066,"Rosemead","CA"},
	{56032,"Springfield","OR"},
	{55997,"Rocky Mount","NC"},
	{55880,"Kettering","OH"},
	{55873,"Elyria","OH"},
	{55741,"Orland Park","IL"},
	{55703,"Temple","TX"},
	{55559,"Greenville","SC"},
	{55510,"Margate","FL"},
	{55443,"Cheyenne","WY"},
	{55352,"Petaluma","CA"},
	{55303,"Mount Prospect","IL"},
	{55290,"Malden","MA"},
	{55279,"West Des Moines","IA"},
	{55270,"San Rafael","CA"},
	{55233,"Pearland","TX"},
	{55179,"North Chicopee","MA"},
	{55007,"Rocklin","CA"},
	{54991,"Murray","UT"},
	{54958,"Midwest City","OK"},
	{54829,"Fishers","IN"},
	{54785,"Rowlett","TX"},
	{54767,"Oak Lawn","IL"},
	{54752,"Lake Havasu City","AZ"},
	{54740,"Manchester","CT"},
	{54735,"Lancaster","PA"},
	{54653,"Chicopee","MA"},
	{54621,"Decatur","AL"},
	{54617,"Santa Cruz","CA"},
	{54608,"Bradenton","FL"},
	{54545,"Wheaton","IL"},
	{54478,"Owensboro","KY"},
	{54395,"Weymouth","MA"},
	{54289,"Hacienda Heights","CA"},
	{54209,"Port Orange","FL"},
	{54207,"Blaine","MN"},
	{54104,"East Pensacola Heights","FL"},
	{54088,"Battle Creek","MI"},
	{54087,"La Mesa","CA"},
	{54067,"Des Plaines","IL"},
	{53963,"Medford","MA"},
	{53779,"Saint Peters","MO"},
	{53754,"Novi","MI"},
	{53749,"Towson","MD"},
	{53746,"Country Club","FL"},
	{53430,"Aspen Hill","MD"},
	{53401,"West Haven","CT"},
	{53327,"Sarasota","FL"},
	{53319,"Sterling","VA"},
	{53305,"Cerritos","CA"},
	{53221,"Euless","TX"},
	{53217,"Pine Bluff","AR"},
	{53164,"Wellington","FL"},
	{53117,"Levittown","PA"},
	{53054,"Niagara Falls","NY"},
	{53015,"Fort Myers","FL"},
	{52991,"Cathedral City","CA"},
	{52837,"Hempstead","NY"},
	{52834,"Middletown","OH"},
	{52801,"Bethesda","MD"},
	{52756,"Lakewood","OH"},
	{52600,"Santee","CA"},
	{52548,"Levittown","NY"},
	{52432,"Yuba City","CA"},
	{52377,"Rowland Heights","CA"},
	{52335,"Colton","CA"},
	{52175,"Idaho Falls","ID"},
	{51958,"Irondequoit","NY"},
	{51824,"Cuyahoga Falls","OH"},
	{51818,"La Crosse","WI"},
	{51786,"Florissant","MO"},
	{51731,"Woodland","CA"},
	{51695,"Cedar Park","TX"},
	{51657,"Shoreline","WA"},
	{51625,"Monroe","LA"},
	{51558,"Bowling Green","KY"},
	{51541,"Glendora","CA"},
	{51524,"Conway","AR"},
	{51507,"Casper","WY"},
	{51451,"Madera","CA"},
	{51400,"Elkhart","IN"},
	{51376,"Berwyn","IL"},
	{51344,"Rancho Santa Margarita","CA"},
	{51331,"Normal","IL"},
	{51312,"Highland","CA"},
	{51301,"Minnetonka","MN"},
	{51268,"Stratford","CT"},
	{51195,"Milford","CT"},
	{50994,"Lakeville","MN"},
	{50992,"Carmichael","CA"},
	{50955,"Woodbury","MN"},
	{50934,"Cupertino","CA"},
	{50701,"Corvallis","OR"},
	{50656,"Pocatello","ID"},
	{50644,"Biloxi","MS"},
	{50633,"New Brunswick","NJ"},
	{50541,"Hoffman Estates","IL"},
	{50540,"Apple Valley","MN"},
	{50470,"North La Crosse","WI"},
	{50453,"East Providence","RI"},
	{50427,"Charleston","WV"},
	{50392,"Yucaipa","CA"},
	{50362,"Middletown","CT"},
	{50326,"Coconut Creek","FL"},
	{50310,"Mansfield","OH"},
	{50293,"South Peabody","MA"},
	{50269,"Bowie","MD"},
	{50160,"East Hartford","CT"},
	{50123,"Euclid","OH"},
	{50117,"Minnetonka Mills","MN"},
	{50103,"Tulare","CA"},
	{49963,"La Mirada","CA"},
	{49936,"Redford","MI"},
	{49833,"Auburn","AL"},
	{49767,"Blue Springs","MO"},
	{49726,"Mentor","OH"},
	{49706,"Franklin","TN"},
	{49666,"Grapevine","TX"},
	{49558,"Ocala","FL"},
	{49509,"Oak Park","IL"},
	{49504,"Downers Grove","IL"},
	{49333,"Port Charlotte","FL"},
	{49185,"Bedford","TX"},
	{49160,"Placentia","CA"},
	{49120,"Bloomfield","NJ"},
	{49107,"Perth Amboy","NJ"},
	{49055,"Mishawaka","IN"},
	{48974,"Jupiter","FL"},
	{48828,"South Bel Air","MD"},
	{48817,"Novato","CA"},
	{48688,"Covina","CA"},
	{48591,"Sheboygan","WI"},
	{48534,"Lehigh Acres","FL"},
	{48531,"Palm Desert","CA"},
	{48518,"Huntington","WV"},
	{48495,"East Brunswick","NJ"},
	{48492,"Chantilly","VA"},
	{48426,"Grand Forks","ND"},
	{48371,"East Lansing","MI"},
	{48297,"Azusa","CA"},
	{48286,"Andover","MA"},
	{48232,"Troy","NY"},
	{48140,"Palm Beach Gardens","FL"},
	{48131,"West Orange","NJ"},
	{48129,"Peabody","MA"},
	{48123,"Hattiesburg","MS"},
	{48099,"Bellevue","NE"},
	{48044,"Poway","CA"},
	{47996,"Shawnee","KS"},
	{47840,"Harrisburg","PA"},
	{47821,"Barnstable","MA"},
	{47795,"Plainfield","NJ"},
	{47726,"Cleveland Heights","OH"},
	{47648,"Hanford","CA"},
	{47529,"Chapel Hill","NC"},
	{47475,"Moore","OK"},
	{47372,"Roseville","MI"},
	{47371,"Cypress","CA"},
	{47264,"Redmond","WA"},
	{47219,"Carmel","IN"},
	{47128,"Chesterfield","MO"},
	{47068,"Tigard","OR"},
	{47059,"Kentwood","MI"},
	{46962,"Altoona","PA"},
	{46921,"Burlington","NC"},
	{46818,"Joplin","MO"},
	{46812,"Newark","OH"},
	{46748,"Wilson","NC"},
	{46736,"San Marcos","TX"},
	{46669,"Brentwood","CA"},
	{46621,"Laguna","CA"},
	{46561,"Smyrna","GA"},
	{46539,"Glenview","IL"},
	{46495,"Pinellas Park","FL"},
	{46482,"North Fort Myers","FL"},
	{46420,"Revere","MA"},
	{46403,"Portage","MI"},
	{46318,"Perris","CA"},
	{46227,"Kokomo","IN"},
	{46169,"O'Fallon","MO"},
	{46140,"Egypt Lake-Leto","FL"},
	{46138,"Enid","OK"},
	{46119,"Bell Gardens","CA"},
	{46010,"West New York","NJ"},
	{45945,"Edina","MN"},
	{45938,"Grand Junction","CO"},
	{45894,"Danville","VA"},
	{45878,"North Highlands","CA"},
	{45840,"New Braunfels","TX"},
	{45839,"Woonsocket","RI"},
	{45803,"Meridian","ID"},
	{45778,"Kirkland","WA"},
	{45734,"Athens","GA"},
	{45724,"Watsonville","CA"},
	{45723,"Salina","KS"},
	{45661,"Valdosta","GA"},
	{45650,"Auburn","WA"},
	{45629,"Manhattan","KS"},
	{45541,"DeSoto","TX"},
	{45530,"Pine Hills","FL"},
	{45524,"Rogers","AR"},
	{45480,"Binghamton","NY"},
	{45475,"Warren","OH"},
	{45453,"Wauwatosa","WI"},
	{45372,"Methuen","MA"},
	{45309,"Gilroy","CA"},
	{45289,"Sanford","FL"},
	{45187,"Alexandria","LA"},
	{45129,"Potomac","MD"},
	{45060,"Elmhurst","IL"},
	{44916,"Olympia","WA"},
	{44915,"Aloha","OR"},
	{44869,"Richland","WA"},
	{44863,"Strongsville","OH"},
	{44838,"Logan","UT"},
	{44766,"Grand Island","NE"},
	{44757,"Delano","CA"},
	{44757,"Albany","OR"},
	{44722,"San Ramon","CA"},
	{44712,"West Seneca","NY"},
	{44692,"Broomfield","CO"},
	{44674,"Porterville","CA"},
	{44610,"Penn Hills","PA"},
	{44598,"Hendersonville","TN"},
	{44493,"North Atlanta","GA"},
	{44379,"North Bethesda","MD"},
	{44271,"Attleboro","MA"},
	{44174,"Pittsfield","MA"},
	{44139,"South Hill","WA"},
	{44132,"San Luis Obispo","CA"},
	{44071,"Saint Louis Park","MN"},
	{44069,"Roswell","NM"},
	{44049,"Cedar Hill","TX"},
	{43952,"Kingsport","TN"},
	{43931,"Tuckahoe","VA"},
	{43716,"Hackensack","NJ"},
	{43606,"West Babylon","NY"},
	{43459,"Augusta","GA"},
	{43447,"Freeport","NY"},
	{43390,"Pasco","WA"},
	{43383,"DeKalb","IL"},
	{43370,"Covington","KY"},
	{43331,"Lenexa","KS"},
	{43281,"Buffalo Grove","IL"},
	{43278,"Farmington","NM"},
	{43264,"Bartlett","TN"},
	{43226,"Altadena","CA"},
	{43121,"Fairfield","OH"},
	{43050,"Hilo","HI"},
	{42922,"Newark","CA"},
	{42890,"Sayreville Junction","NJ"},
	{42852,"Danville","CA"},
	{42836,"Lombard","IL"},
	{42827,"Catonsville","MD"},
	{42807,"Palm Springs","CA"},
	{42757,"Stillwater","OK"},
	{42700,"Titusville","FL"},
	{42693,"Rancho Palos Verdes","CA"},
	{42668,"Moline","IL"},
	{42605,"La Puente","CA"},
	{42605,"East Concord","NH"},
	{42485,"Coral Gables","FL"},
	{42455,"Salem","MA"},
	{42421,"New Milford","CT"},
	{42400,"Midland","MI"},
	{42301,"Leominster","MA"},
	{42268,"Southglenn","CO"},
	{42157,"Severn","MD"},
	{42120,"Lakewood","NJ"},
	{41860,"Fond du Lac","WI"},
	{41840,"Bonita Springs","FL"},
	{41835,"State College","PA"},
	{41821,"Lawrence","IN"},
	{41769,"The Colony","TX"},
	{41725,"Harrisonburg","VA"},
	{41721,"Montgomery Village","MD"},
	{41643,"Conroe","TX"},
	{41581,"Bountiful","UT"},
	{41556,"Haltom City","TX"},
	{41535,"San Gabriel","CA"},
	{41521,"Texas City","TX"},
	{41483,"Palm Coast","FL"},
	{41461,"Arlington","MA"},
	{41453,"Hicksville","NY"},
	{41446,"West Sacramento","CA"},
	{41420,"Romeoville","IL"},
	{41378,"Sierra Vista","AZ"},
	{41314,"Rohnert Park","CA"},
	{41314,"Aliso Viejo","CA"},
	{41244,"Greenwood","IN"},
	{41188,"Crystal Lake","IL"},
	{41078,"Sun City","AZ"},
	{41059,"Parker","CO"},
	{40957,"Lompoc","CA"},
	{40949,"Carrollwood Village","FL"},
	{40947,"Littleton","CO"},
	{40932,"Westfield","MA"},
	{40919,"Belleville","IL"},
	{40849,"Marshfield","MA"},
	{40819,"Altamonte Springs","FL"},
	{40788,"Beavercreek","OH"},
	{40736,"Wilkes-Barre","PA"},
	{40687,"Concord","NH"},
	{40630,"Lake Elsinore","CA"},
	{40596,"Hutchinson","KS"},
	{40554,"Atlantic City","NJ"},
	{40377,"Sayreville","NJ"},
	{40365,"Beverly Cove","MA"},
	{40363,"Fitchburg","MA"},
	{40354,"Findlay","OH"},
	{40349,"Pahrump","NV"},
	{40342,"Huntersville","NC"},
	{40334,"Hickory","NC"},
	{40317,"Glen Burnie","MD"},
	{40213,"Franconia","VA"},
	{40183,"Blacksburg","VA"},
	{40141,"Holyoke","MA"},
	{40136,"Prescott","AZ"},
	{40129,"Culver City","CA"},
	{40112,"Muskegon","MI"},
	{40082,"Ceres","CA"},
	{40078,"Teaneck","NJ"},
	{40056,"Brookfield","WI"},
	{39917,"Edmonds","WA"},
	{39906,"Lima","OH"},
	{39904,"Billerica","MA"},
	{39878,"Linden","NJ"},
	{39862,"Beverly","MA"},
	{39839,"Carol Stream","IL"},
	{39738,"Hoboken","NJ"},
	{39735,"North Miami Beach","FL"},
	{39731,"Quincy","IL"},
	{39701,"Montclair","NJ"},
	{39695,"Shelton","CT"},
	{39662,"San Bruno","CA"},
	{39651,"McLean","VA"},
	{39587,"York","PA"},
	{39574,"Coppell","TX"},
	{39542,"Brea","CA"},
	{39511,"Urbana","IL"},
	{39508,"South Valley","NM"},
	{39437,"Meridian","MS"},
	{39395,"Columbus","IN"},
	{39234,"Kearny","NJ"},
	{39044,"Redan","GA"},
	{38895,"Georgetown","TX"},
	{38851,"Essex","MD"},
	{38814,"Castle Rock","CO"},
	{38805,"Dublin","CA"},
	{38748,"Kannapolis","NC"},
	{38735,"New Berlin","WI"},
	{38696,"Muskogee","OK"},
	{38602,"Twin Falls","ID"},
	{38601,"Burlington","VT"},
	{38578,"Southaven","MS"},
	{38455,"Ormond Beach","FL"},
	{38439,"Monrovia","CA"},
	{38391,"Huber Heights","OH"},
	{38388,"Calumet City","IL"},
	{38383,"Rock Island","IL"},
	{38366,"Eldersburg","MD"},
	{38335,"Bell","CA"},
	{38334,"Marlborough","MA"},
	{38323,"El Centro","CA"},
	{38297,"Lincoln Park","MI"},
	{38269,"Bartlett","IL"},
	{38254,"Woodlawn","MD"},
	{38243,"Goldsboro","NC"},
	{38195,"Lake Magdalene","FL"},
	{38107,"Greenville","MS"},
	{38054,"North Port","FL"},
	{37992,"Spartanburg","SC"},
	{37989,"Bullhead City","AZ"},
	{37988,"Vestavia Hills","AL"},
	{37973,"Temple City","CA"},
	{37914,"Stanton","CA"},
	{37854,"Collierville","TN"},
	{37839,"Fort Lee","NJ"},
	{37749,"Hurst","TX"},
	{37746,"Woburn","MA"},
	{37723,"Fort Pierce","FL"},
	{37699,"Manassas","VA"},
	{37690,"La Quinta","CA"},
	{37652,"Madison","AL"},
	{37634,"University City","MO"},
	{37633,"Panama City","FL"},
	{37631,"Richmond","IN"},
	{37625,"Streamwood","IL"},
	{37487,"Keller","TX"},
	{37485,"Hollister","CA"},
	{37482,"Marion","OH"},
	{37470,"Hot Springs","AR"},
	{37443,"Cleveland","TN"},
	{37427,"East Meadow","NY"},
	{37394,"Addison","IL"},
	{37361,"Germantown","TN"},
	{37313,"Hagerstown","MD"},
	{37301,"North Chicago","IL"},
	{37259,"Bremerton","WA"},
	{37221,"Oro Valley","AZ"},
	{37174,"Manhattan Beach","CA"},
	{37150,"Coram","NY"},
	{37148,"Everett","MA"},
	{37141,"South Jordan Heights","UT"},
	{37108,"Sherman","TX"},
	{37107,"Brewster","MA"},
	{37083,"Hanover Park","IL"},
	{37079,"Kailua","HI"},
	{37053,"Leesburg","VA"},
	{37034,"Park Ridge","IL"},
	{37031,"West Hollywood","CA"},
	{37020,"Dunedin","FL"},
	{37010,"Moorpark","CA"},
	{36957,"Olney","MD"},
	{36938,"Chester","PA"},
	{36924,"Golden Glades","FL"},
	{36878,"Belleville","NJ"},
	{36834,"Carpentersville","IL"},
	{36821,"Gadsden","AL"},
	{36779,"Wausau","WI"},
	{36702,"Martinez","CA"},
	{36675,"Puyallup","WA"},
	{36613,"Mansfield","TX"},
	{36597,"Lake Oswego","OR"},
	{36559,"Ewing","NJ"},
	{36529,"Campbell","CA"},
	{36525,"Bridgewater","MA"},
	{36493,"Greenfield","WI"},
	{36472,"Pacifica","CA"},
	{36441,"Norwich","CT"},
	{36422,"San Dimas","CA"},
	{36389,"Calexico","CA"},
	{36385,"Spring","TX"},
	{36376,"Saint Charles","MD"},
	{36368,"Valley Stream","NY"},
	{36349,"Longview","WA"},
	{36332,"Pennsauken","NJ"},
	{36280,"Chillum","MD"},
	{36265,"Roy","UT"},
	{36264,"Florence","AL"},
	{36258,"Cedar Falls","IA"},
	{36252,"Annapolis","MD"},
	{36216,"Boardman","OH"},
	{36195,"Commack","NY"},
	{36138,"New Albany","IN"},
	{36090,"Merritt Island","FL"},
	{36079,"Woodlawn","MD"},
	{36068,"Del Rio","TX"},
	{36065,"Trumbull","CT"},
	{36058,"Alamogordo","NM"},
	{36000,"West Torrington","CT"},
	{35997,"Dunwoody","GA"},
	{35956,"Tupelo","MS"},
	{35934,"North Valley Stream","NY"},
	{35904,"Cape Girardeau","MO"},
	{35883,"Lancaster","OH"},
	{35883,"Jefferson City","MO"},
	{35870,"Dana Point","CA"},
	{35840,"Noblesville","IN"},
	{35817,"Lake Worth","FL"},
	{35815,"Wheeling","IL"},
	{35803,"Mechanicsville","VA"},
	{35787,"Portage","IN"},
	{35786,"East Point","GA"},
	{35762,"Seaside","CA"},
	{35761,"Hallandale Beach","FL"},
	{35743,"Drexel Heights","AZ"},
	{35733,"East Florence","AL"},
	{35729,"Montclair","CA"},
	{35690,"Lewiston","ME"},
	{35590,"Huntsville","TX"},
	{35584,"Brighton","NY"},
	{35571,"South Miami Heights","FL"},
	{35537,"Keizer","OR"},
	{35525,"Coventry","RI"},
	{35452,"Rome","GA"},
	{35405,"Marrero","LA"},
	{35379,"Homestead","FL"},
	{35368,"Claremont","CA"},
	{35355,"Beverly Hills","CA"},
	{35349,"Stow","OH"},
	{35345,"Kearns","UT"},
	{35321,"Willowbrook","CA"},
	{35315,"Texarkana","TX"},
	{35309,"Oakville","MO"},
	{35301,"San Juan Capistrano","CA"},
	{35252,"Woodbridge","VA"},
	{35237,"Dublin","OH"},
	{35210,"Beloit","WI"},
	{35203,"Long Beach","NY"},
	{35202,"Torrington","CT"},
	{35186,"Draper","UT"},
	{35138,"Mankato","MN"},
	{35119,"La Presa","CA"},
	{35102,"Atascocita","TX"},
	{34991,"Duncanville","TX"},
	{34970,"Kāne‘ohe","HI"},
	{34958,"Summerville","SC"},
	{34950,"Rome","NY"},
	{34947,"Maplewood","MN"},
	{34907,"Central Islip","NY"},
	{34904,"Apache Junction","AZ"},
	{34886,"Sandwich","MA"},
	{34885,"Minot","ND"},
	{34880,"Leavenworth","KS"},
	{34869,"Alpharetta","GA"},
	{34843,"Cumberland","RI"},
	{34790,"Brunswick","OH"},
	{34755,"Oak Creek","WI"},
	{34750,"Royal Palm Beach","FL"},
	{34719,"Morgan Hill","CA"},
	{34703,"Charlottesville","VA"},
	{34622,"Friendswood","TX"},
	{34618,"Amherst Center","MA"},
	{34610,"Bartlesville","OK"},
	{34588,"Waipahu","HI"},
	{34568,"Bay City","MI"},
	{34544,"Holland","MI"},
	{34539,"Derry Village","NH"},
	{34515,"Menomonee Falls","WI"},
	{34514,"Coeur d'Alene","ID"},
	{34510,"Springfield","VA"},
	{34500,"Woodridge","IL"},
	{34485,"Saint Charles","IL"},
	{34479,"Elmont","NY"},
	{34477,"Pleasant Hill","CA"},
	{34457,"Orange","NJ"},
	{34457,"Hilton Head Island","SC"},
	{34416,"Jackson","MI"},
	{34382,"Lake Ridge","VA"},
	{34330,"Caldwell","ID"},
	{34322,"Northbrook","IL"},
	{34267,"North Lauderdale","FL"},
	{34261,"Elk Grove Village","IL"},
	{34226,"Westerville","OH"},
	{34215,"Peachtree City","GA"},
	{34190,"Butte","MT"},
	{34143,"Mission Bend","TX"},
	{34104,"Sammamish","WA"},
	{34065,"Bourne","MA"},
	{33942,"New City","NY"},
	{33939,"Manitowoc","WI"},
	{33929,"West Lake Sammamish","WA"},
	{33925,"North Bel Air","MD"},
	{33925,"Chelmsford","MA"},
	{33916,"Ankeny","IA"},
	{33893,"Shrewsbury","MA"},
	{33835,"North Providence","RI"},
	{33811,"Randallstown","MD"},
	{33755,"Lacey","WA"},
	{33740,"Petersburg","VA"},
	{33740,"La Porte","TX"},
	{33698,"La Verne","CA"},
	{33686,"Upper Arlington","OH"},
	{33678,"Riviera Beach","FL"},
	{33667,"Lufkin","TX"},
	{33641,"Columbia","TN"},
	{33617,"Fallbrook","CA"},
	{33528,"Urbandale","IA"},
	{33509,"Braintree","MA"},
	{33478,"Lynnwood","WA"},
	{33365,"Ken Caryl","CO"},
	{33349,"Richfield","MN"},
	{33317,"Los Banos","CA"},
	{33297,"Gainesville","GA"},
	{33286,"Dover","DE"},
	{33272,"Franklin","WI"},
	{33242,"Lincoln","CA"},
	{33237,"Reynoldsburg","OH"},
	{33216,"Apopka","FL"},
	{33178,"Bozeman","MT"},
	{33163,"Clovis","NM"},
	{33137,"Moorhead","MN"},
	{33127,"Gahanna","OH"},
	{33062,"Chelsea","MA"},
	{33047,"Glendale Heights","IL"},
	{33012,"Mundelein","IL"},
	{32975,"Cottage Grove","MN"},
	{32963,"Smyrna","TN"},
	{32963,"Greenacres City","FL"},
	{32958,"North Olmsted","OH"},
	{32918,"Watertown","MA"},
	{32906,"East Palo Alto","CA"},
	{32884,"Wildwood","MO"},
	{32860,"Englewood","CO"},
	{32841,"Lawndale","CA"},
	{32813,"Sun City West","AZ"},
	{32749,"Granger","IN"},
	{32743,"Pekin","IL"},
	{32732,"Weslaco","TX"},
	{32730,"Mount Lebanon","PA"},
	{32682,"Eastpointe","MI"},
	{32670,"Walpole","MA"},
	{32658,"Winter Springs","FL"},
	{32656,"Northglenn","CO"},
	{32622,"Oceanside","NY"},
	{32587,"Danville","IL"},
	{32586,"Fairborn","OH"},
	{32563,"Roseville","MN"},
	{32530,"Perry Hall","MD"},
	{32474,"Bethel Park","PA"},
	{32430,"New Iberia","LA"},
	{32415,"Prescott Valley","AZ"},
	{32377,"Westmont","CA"},
	{32363,"Inver Grove Heights","MN"},
	{32308,"El Mirage","AZ"},
	{32276,"Natick","MA"},
	{32252,"West Little River","FL"},
	{32235,"Rubidoux","CA"},
	{32171,"Oakton","VA"},
	{32161,"Naugatuck","CT"},
	{32138,"Casa Grande","AZ"},
	{32094,"Galesburg","IL"},
	{32051,"Lakeside","FL"},
	{31990,"Westlake","OH"},
	{31924,"Channelview","TX"},
	{31903,"Michigan City","IN"},
	{31882,"Pikesville","MD"},
	{31876,"Glastonbury","CT"},
	{31855,"Riverton","UT"},
	{31825,"North Tonawanda","NY"},
	{31806,"Chicago Heights","IL"},
	{31800,"Houma","LA"},
	{31791,"Douglasville","GA"},
	{31720,"Walnut","CA"},
	{31698,"Chalmette","LA"},
	{31675,"Plant City","FL"},
	{31668,"Willingboro","NJ"},
	{31605,"Oakland Park","FL"},
	{31598,"Dalton","GA"},
	{31589,"Lauderdale Lakes","FL"},
	{31587,"Burton","MI"},
	{31572,"Pearl City","HI"},
	{31564,"Parkersburg","WV"},
	{31526,"Massillon","OH"},
	{31524,"Long Branch","NJ"},
	{31503,"Fair Lawn","NJ"},
	{31476,"Mableton","GA"},
	{31473,"Bangor","ME"},
	{31470,"Bentonville","AR"},
	{31457,"Delaware","OH"},
	{31422,"Lewiston Orchards","ID"},
	{31421,"Bettendorf","IA"},
	{31403,"Highland Park","IL"},
	{31362,"Parkville","MD"},
	{31351,"San Pablo","CA"},
	{31346,"Gurnee","IL"},
	{31279,"Brentwood Estates","TN"},
	{31168,"Merrillville","IN"},
	{31159,"Ithaca","NY"},
	{30988,"Mason","OH"},
	{30978,"Wheat Ridge","CO"},
	{30978,"Port Huron","MI"},
	{30969,"Austintown","OH"},
	{30950,"West Lafayette","IN"},
	{30916,"Gloucester","MA"},
	{30904,"Lewiston","ID"},
	{30878,"Radnor","PA"},
	{30873,"University Place","WA"},
	{30873,"Apex","NC"},
	{30857,"Thomasville","NC"},
	{30848,"Surprise","AZ"},
	{30843,"Laguna Hills","CA"},
	{30785,"Marion","IA"},
	{30768,"Randolph","MA"},
	{30755,"Coachella","CA"},
	{30744,"Ballwin","MO"},
	{30743,"Winchester","NV"},
	{30735,"Norristown","PA"},
	{30732,"Granite City","IL"},
	{30711,"Juneau","AK"},
	{30706,"Williamsport","PA"},
	{30697,"Richmond","KY"},
	{30696,"Millcreek","UT"},
	{30691,"Nacogdoches","TX"},
	{30690,"Westchester","FL"},
	{30680,"Fortuna Foothills","AZ"},
	{30662,"East Chicago","IN"},
	{30661,"Lexington","MA"},
	{30647,"San Juan","TX"},
	{30636,"Franklin","MA"},
	{30615,"Bothell","WA"},
	{30611,"Burien","WA"},
	{30578,"Walla Walla","WA"},
	{30546,"Copperas Cove","TX"},
	{30520,"Jacksonville","AR"},
	{30517,"Florence","SC"},
	{30496,"Alton","IL"},
	{30484,"Huntington Station","NY"},
	{30411,"Banning","CA"},
	{30394,"Algonquin","IL"},
	{30384,"Shakopee","MN"},
	{30345,"Poughkeepsie","NY"},
	{30317,"Rochester","NH"},
	{30307,"Monterey","CA"},
	{30292,"Severna Park","MD"},
	{30238,"Newark","DE"},
	{30224,"Fairbanks","AK"},
	{30209,"Andover","MN"},
	{30201,"Marion","IN"},
	{30151,"Hingham","MA"},
	{30146,"West Warwick","RI"},
	{30078,"Carney","MD"},
	{30077,"Jamestown","NY"},
	{30045,"Goshen","IN"},
	{30035,"Grove City","OH"},
	{30026,"Elmira","NY"},
	{30004,"Madison Heights","MI"},
	{29984,"Oregon City","OR"},
	{29982,"Clinton","MD"},
	{29945,"Newington","CT"},
	{29940,"Westfield","NJ"},
	{29930,"Rockwall","TX"},
	{29924,"Texarkana","AR"},
	{29851,"Tooele","UT"},
	{29824,"Marysville","WA"},
	{29808,"Florin","CA"},
	{29773,"East Saint Louis","IL"},
	{29765,"Niles","IL"},
	{29759,"McMinnville","OR"},
	{29753,"Rosenberg","TX"},
	{29737,"Holladay","UT"},
	{29686,"Atwater","CA"},
	{29681,"Monroe","NC"},
	{29669,"Southgate","MI"},
	{29665,"Northampton","MA"},
	{29662,"Waimalu","HI"},
	{29658,"South Gate","MD"},
	{29658,"Franklin Square","NY"},
	{29614,"Garfield","NJ"},
	{29549,"Salem","NH"},
	{29541,"Pflugerville","TX"},
	{29449,"Doral","FL"},
	{29443,"Cheshire","CT"},
	{29439,"Rexburg","ID"},
	{29438,"Branford","CT"},
	{29437,"South Jordan","UT"},
	{29431,"West Bend","WI"},
	{29394,"East Lake","FL"},
	{29388,"Wheeling","WV"},
	{29376,"San Jacinto","CA"},
	{29368,"Lawrenceville","GA"},
	{29349,"Monroeville","PA"},
	{29328,"Brighton","CO"},
	{29326,"Tewksbury","MA"},
	{29313,"Westborough","MA"},
	{29312,"Mount Vernon","WA"},
	{29251,"Upper Alton","IL"},
	{29219,"Raytown","MO"},
	{29218,"Garfield Heights","OH"},
	{29215,"Bowling Green","OH"},
	{29208,"Goose Creek","SC"},
	{29201,"Lancaster","TX"},
	{29175,"Mehlville","MO"},
	{29172,"Cooper City","FL"},
	{29159,"Wenatchee","WA"},
	{29125,"Needham","MA"},
	{29081,"Lindenhurst","NY"},
	{29075,"Drexel Hill","PA"},
	{29074,"Milford Mill","MD"},
	{29071,"Menlo Park","CA"},
	{29064,"Laplace","LA"},
	{29061,"Dover","NH"},
	{29056,"Oviedo","FL"},
	{29027,"Liberty","MO"},
	{29023,"Garden City","MI"},
	{29015,"Santa Paula","CA"},
	{29006,"East Haven","CT"},
	{28989,"Fair Oaks","CA"},
	{28981,"Cleburne","TX"},
	{28965,"Maywood","CA"},
	{28962,"Oildale","CA"},
	{28957,"Morgantown","WV"},
	{28956,"Saratoga","CA"},
	{28920,"North Royalton","OH"},
	{28911,"West Islip","NY"},
	{28844,"Inkster","MI"},
	{28831,"Dracut","MA"},
	{28830,"Orcutt","CA"},
	{28826,"Dania Beach","FL"},
	{28812,"Mililani Town","HI"},
	{28812,"Harvey","IL"},
	{28787,"Valparaiso","IN"},
	{28772,"Deer Park","TX"},
	{28761,"Agawam","MA"},
	{28758,"West Springfield","VA"},
	{28758,"Kearney","NE"},
	{28734,"West Elkridge","MD"},
	{28692,"Shawnee","OK"},
	{28668,"Des Moines","WA"},
	{28668,"Cookeville","TN"},
	{28644,"Oak Park","MI"},
	{28637,"Socorro Mission Number 1 Colonia","TX"},
	{28633,"Prichard","AL"},
	{28631,"Milford","MA"},
	{28628,"Bessemer","AL"},
	{28625,"Norwood","MA"},
	{28570,"Ocoee","FL"},
	{28541,"Lake in the Hills","IL"},
	{28534,"Burleson","TX"},
	{28532,"Foster City","CA"},
	{28530,"Winter Park","FL"},
	{28487,"Jefferson","VA"},
	{28476,"Newburgh","NY"},
	{28465,"Jeffersonville","IN"},
	{28439,"Kennesaw","GA"},
	{28427,"Phenix City","AL"},
	{28424,"Clearfield","UT"},
	{28417,"Schertz","TX"},
	{28407,"Holbrook","NY"},
	{28387,"Kent","OH"},
	{28351,"Goleta","CA"},
	{28267,"Rancho San Diego","CA"},
	{28265,"Prattville","AL"},
	{28222,"North Andover","MA"},
	{28175,"Batavia","IL"},
	{28165,"Palm City","FL"},
	{28157,"Allen Park","MI"},
	{28143,"Oakdale","MN"},
	{28140,"Soledad","CA"},
	{28122,"Oak Forest","IL"},
	{28082,"Richmond West","FL"},
	{28066,"Hinesville","GA"},
	{28065,"West Memphis","AR"},
	{28055,"Watertown","NY"},
	{28042,"North Kingstown","RI"},
	{28021,"Cottonwood Heights","UT"},
	{28020,"Burbank","IL"},
	{28013,"Hobbs","NM"},
	{28008,"Hobart","IN"},
	{27990,"Port Chester","NY"},
	{27989,"Saratoga Springs","NY"},
	{27938,"Winter Haven","FL"},
	{27912,"West Springfield","MA"},
	{27892,"Aventura","FL"},
	{27873,"Auburn","NY"},
	{27853,"Livingston","NJ"},
	{27853,"Deer Park","NY"},
	{27810,"Ferry Pass","FL"},
	{27802,"Mason City","IA"},
	{27799,"Slidell","LA"},
	{27772,"Clinton","IA"},
	{27755,"Shaker Heights","OH"},
	{27748,"Olive Branch","MS"},
	{27718,"Brooklyn Center","MN"},
	{27706,"West Scarborough","ME"},
	{27695,"Duxbury","MA"},
	{27680,"Palm Valley","FL"}
};


bool printWidgetFrame ( SafeBuf &sb , State7 *st ) {

	SearchInput *si = &st->m_si;

	sb.safePrintf ( "<div style=\""
			"width:%s;"
			//"position:absolute;"
			//"top:300px;"
			//"right:0;"
			//"left:0;"
			//"bottom:0;"
			"\">"
			"<div style=line-height:13px;><br></div>"
			//"<br>"
			, RESULTSWIDTHSTR 
			);

	printTabs ( sb , st );
	printRedBoxes ( sb , st );


	//sb.safePrintf("<div style=line-height:5px;><br></div>");


	sb.safePrintf ( "<div "
			//"class=grad3 "
			"style=\""
			"border-radius:10px;"
			"box-shadow: 6px 6px 3px %s;"
			"border:2px solid black;"
			"padding:15px;"
			"background-image:url('/ss.jpg');"
			"background-repeat:repeat;"
			"background-attachment:fixed;"
			"\">"
			, SHADOWCOLOR
			//"<br>"
			);

	// space widget to the right using this table
	sb.safePrintf("<table cellpadding=0>"
		      //class=grad3 "
		      //"style=\""
		      //"border:2px solid black;"
		      //"padding-bottom:10px;"
		      //"padding-top:10px;"
		      //"padding-left:10px;"
		      //"\""
		      //">"
		      "<tr><td valign=top>"
		      "<img src=/gears32.png width=64 height=64>"
		      "<br><br>"
		      );


	int32_t start = sb.length();

	char *fb = "";
	if ( si->m_frameBorder ) fb = "border:3px solid black;";

	// this iframe contains the WIDGET
	sb.safePrintf (
		       /*
		       "<div "
		       "id=scrollerxyz "
		       "style=\""
		       //"width:%"INT32"px;" // 200;"
		       //"height:%"INT32"px;" // 400;"
		       //"overflow:hidden;"
		       "padding:0px;"
		       "margin:0px;"
		       "background-color:white;"
		       //"padding-left:7px;"
		       "%s"
		       //"background-color:%s;"//lightblue;"
		       //"foreground-color:%s;"
		       //"overflow:scroll;"
		       //"overflow-scrolling:touch;"
		       "\">"
		       */

			"<iframe width=\"%"INT32"px\" height=\"%"INT32"px\" "
			//"scrolling=yes "
			"style=\"background-color:white;"
			"padding-right:0px;"
			"%s\" "
			"scrolling=no "
			"frameborder=no "
			"src=\"%s/?widget=1&"
			"widgetwidth=%"INT32"&widgetheight=%"INT32"&"
		       //, si->m_width
		       //, si->m_height
			//, fb
			//, si->m_background
			//, si->m_foreground
			, si->m_widgetWidth
			, si->m_widgetHeight
			, fb
			//, "http://10.5.1.203:8000"
			, APPHOSTUNENCODEDNOSLASH
			, si->m_widgetWidth
			, si->m_widgetHeight
			);

	int64_t fbId = 0LL;
	if ( st->m_msgfb.m_fbrecPtr && st->m_msgfb.m_fbId )
		fbId = st->m_msgfb.m_fbId;

	// print extra parm "&usefbid=%"UINT64"" if we have showpersona;=1
	if ( si->m_showPersonal )
		sb.safePrintf("usefbid=%"UINT64"&" // fh=%"UINT32"&"
			      , fbId 
			      // this is like the password
			      //, hash32 ( (char *)&fbId , 8, 0 )
			      );


	// . if they are logged into facebook
	// . tag the widget with this so we can pay the webmasters!!!
	if ( fbId ) 
		sb.safePrintf("widgetid=%"UINT64"&" , fbId );
	// otherwise, use matt wells' id
	else
		sb.safePrintf("widgetid=%"UINT64"&" , MATTWELLS );


	char *wp = si->m_widgetParms.getBufStart();
	// sometimes they arrive here as the first page and do not have
	// ANY cookies!! so this is NULL. which means the widget will
	// end up using default parms i guess. it is dangerous for them
	// because we might change the defaults in the future.
	if ( ! wp || si->m_widgetParms.length() <= 0 ) wp = "";

	sb.safePrintf ( // do not reset the user's "where" cookie
			// to NYC from looking at this widget!
			//"cookie=0&"
			"%s"
			"\">"
			"Your browser does not support iframes"
			"</iframe>\n"
			//"</div>" 
			//, si->m_urlParms);
			, wp );

	int32_t end = sb.length();

	sb.reserve ( end - start + 1000 );

	char *wdir = "on the left";
	int32_t cols = 32;

	if ( si->m_widgetWidth <= 240 ) 
		sb.safePrintf("</td><td>&nbsp;&nbsp;</td><td valign=top>");
	else {
		sb.safePrintf("</td></tr><tr><td><br><br>");
		wdir = "above";
		cols = 60;
	}

	sb.safePrintf ( "\n\n"
			//"<br><br><br>"
			"<font style=\"font-size:16px;\">"
			"Insert the following code into your website to "
			"generate the widget %s. "
			//"<br>"
			//"<b><u>"
			//"<a style=color:white href=/widget.html>"
			//"Make $1 per click!</a></u></b>"
			//"</font>"
			"<br><br><b>" , wdir );
	
	char *p = sb.getBufStart() + start;

	sb.safePrintf("<textarea rows=30 cols=%"INT32" "
		      "style=\"border:2px solid black;\">", cols);
	sb.htmlEncode ( p           ,
			end - start ,
			false       ,  // bool encodePoundSign
			0           ); // niceness
	sb.safePrintf("</textarea>");

	sb.safePrintf("</b>");

	// space widget to the right using this table
	sb.safePrintf("</td></tr></table>");

	sb.safePrintf("</div>");
	sb.safePrintf("</div>");

	return true;
}

// . if ?cities=1 then the above code calls this function. 
// . similar to ?frame=1 logic.
bool printCitiesFrame ( SafeBuf &sb ) {


	sb.safePrintf (
		       "<br>"
		       // for shadow:
		       //"<div style=background-color:#606060;"
		       //"border-radius:10px;>"
		       "<div style=\"border-radius:10px;"
		       // for shadow:
		       //"position:relative;"
		       //"bottom:6px;"
		       //"right:6px;"
		       "box-shadow: 6px 6px 3px %s;"
		       "padding:10px;"
		       //"margin-left:30px;"
		       //"margin-right:30px;"
		       "border:2px solid black;"
		       //"border-right:5px solid black;"
		       //"border-bottom:5px solid black;"
		       "background-color:white;"
		       "font-size:14px;"
		       "\">"
		      , SHADOWCOLOR
		       );

	sb.safePrintf (
			"<table cellpadding=4 style=line-height:32px;>"
			"<tr>"
			"<td valign=top>"
			// this sets the parm and reloads the searchresults div
			"<h2><a onclick=\"swh('anywhere');\">"
			"Anywhere</a></h2><br>"
			);

	int32_t maxCities  = 20;
	int32_t maxCities2 = 5;
	int32_t popx = 80000;

	// first count total lines for break points
	int32_t lines = 0;
	for ( int32_t i = 0 ; ; i++ ) {
		StateDesc *sd = getStateDescByNum ( i );
		if ( ! sd ) break;
		int32_t count = 0;
		int32_t np = (int32_t)sizeof(s_cs)/(int32_t)sizeof(CityState);
		lines++;
		for ( int32_t j = 0 ; j < np ; j++ ) {
			CityState *cs = &s_cs[j];
			// skip if not our state
			if ( to_lower_a(cs->m_state[0]) !=
			     to_lower_a(sd->m_adm1[0]) )
				continue;
			if ( to_lower_a(cs->m_state[1]) !=
			     to_lower_a(sd->m_adm1[1]) )
				continue;
			lines++;
			count++;
			if ( count >= maxCities ) break;
			if ( cs->m_pop < popx && count >= maxCities2 ) break;
		}
	}

	bool firstBreak = false;
	bool secondBreak = false;
	int32_t cursor = 0;
	// cruise the states alphabetically
	for ( int32_t i = 0 ; ; i++ ) {
		StateDesc *sd = getStateDescByNum ( i );
		if ( ! sd ) break;
		// print state name
		sb.safePrintf("<h2>");
		// capitalize first
		char sn[64];
		sprintf(sn,"%s",sd->m_name1 );
		sn[0] = to_upper_a(sn[0]);
		for ( char *p = sn+1; *p ; p++ )
			if ( p[-1]==' ' ) *p = to_upper_a(*p);
		sb.safePrintf("%s",sn);
		sb.safePrintf("</h2>");		
		int32_t count = 0;
		// count state
		cursor++;
		// . now list the cities we got for it by pop
		// . scan the city/state pairs
		int32_t np = (int32_t)sizeof(s_cs)/(int32_t)sizeof(CityState);
		for ( int32_t j = 0 ; j < np ; j++ ) {
			CityState *cs = &s_cs[j];
			// skip if not our state
			if ( to_lower_a(cs->m_state[0]) !=
			     to_lower_a(sd->m_adm1[0]) )
				continue;
			if ( to_lower_a(cs->m_state[1]) !=
			     to_lower_a(sd->m_adm1[1]) )
				continue;

			sb.safePrintf("<a "
				      "onclick=\"swh('%s, %s');\">",
				      cs->m_city, cs->m_state );
			sb.safePrintf("%s</a><br>\n", cs->m_city );

			//sb.safePrintf("<a href=\"");
			// ok, print city link now
			//char path[256];
			//sprintf(path,"/%s/%s",cs->m_state, cs->m_city );
			// convert spaces to +'s
			//sb.urlEncode ( path );
			// close up the link then
			//sb.safePrintf("\">%s</a><br>\n", cs->m_city );
			// count these
			cursor++;
			// limit city count to 20
			count++;
			if ( count >= maxCities ) break;
			if ( cs->m_pop < popx && count >= maxCities2 ) break;
		}
		// break at column end
		if ( cursor >= lines/3 && ! firstBreak ) {
			firstBreak = true;
			sb.safePrintf("</td><td valign=top>");
		}
		else if ( cursor >= (2*lines)/3 && ! secondBreak ) {
			secondBreak = true;
			sb.safePrintf("</td><td valign=top>");
		}
		// separate states
		else 
			sb.safePrintf("<br>\n");
	}
			
	// end the table
	sb.safePrintf ( "</td></tr></table></div>" );

	return true;
}


bool sendPageSiteMap ( TcpSocket *s , HttpRequest *r ) {

	static PlaceDesc *s_top[1000];
	static int32_t s_numTop = 0;
	// pick the 100 most popular cities and make links to them
	// like http://www.eventguru.com/?where=<city>+<country>
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		// scan them all
		PlaceDesc *pd = getPlaceDescBuf();//g_pbuf;
		for ( ; ; pd++ ) {
			// stop if we enter the name buf space
			if ( ((char *)pd)[0] == 'u' &&
			     ((char *)pd)[1] == 'n' &&
			     ! strcmp((char *)pd,"unknown name" ) ) 
				break;
			// must be city
			if ( !(pd->m_flags & PDF_CITY ) ) continue;
			// pop cutoff
			if ( pd->m_population < 400000 ) continue;
			// add it
			s_top[s_numTop++] = pd;
			// do not breach
			if ( s_numTop >= 1000 ) break;
		}
	}

	SafeBuf sb;

	sb.safePrintf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	sb.safePrintf("<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n");


	for ( int32_t i = 0 ; i < s_numTop ; i++ ) {
		// add it
		sb.safePrintf("<url>"
			      "\t<loc>http://www.eventguru.com/"
			      "?where=");
		// get the top city
		PlaceDesc *pd = s_top[i];
		// url encode city name
		char *name = pd->getOfficialName();
		sb.urlEncode ( name, gbstrlen(name) );
		// then the country
		sb.pushChar('+');
		name = (char *)pd->getCountryName();
		sb.urlEncode ( name, gbstrlen(name) );
		// close up the url
		sb.safePrintf("</loc>\n");
		sb.safePrintf("\t<changefreq>daily</changefreq>\n");
		sb.safePrintf("</url>\n");
	}
	sb.safePrintf("</urlset>\n");

	logf(LOG_DEBUG,"gb: sending back sitemap %"INT32" bytes",sb.length());

	char *charset = "utf-8";
	char *ct = "text/xml";
	bool tt;
	tt = g_httpServer.sendDynamicPage ( s      , 
					    sb.getBufStart(), 
					    sb.length(), 
					    25         , // cachetime in secs
					                 // pick up key changes
					                 // this was 0 before
					    false      , // POSTREply? 
					    ct         , // content type
					    -1         , // http status -1->200
					    NULL, // cookiePtr  ,
					    charset    );
			
	return true;
}

//////////////////////
//
// ADD EVENT
//
//////////////////////

#include "gb-include.h"

#include "Pages.h"
#include "Collectiondb.h"
#include "HashTable.h"
#include "Msg4.h"
#include "AutoBan.h"
//#include "CollectionRec.h"
//#include "Links.h"
#include "Users.h"
#include "HashTableT.h"
#include "Spider.h"
#include "PageInject.h"
#include "PageTurk.h"
#include "Repair.h"


static bool canSubmit  (uint32_t h, int32_t now, int32_t maxUrlsPerIpDom);

class State9 {
public:
	TcpSocket *m_socket;
        bool       m_isMasterAdmin;
	char       m_coll[MAX_COLL_LEN+1];

	HttpRequest  m_hr;

	uint32_t m_hashedUID32;

	int32_t       m_urlLen;
	char       m_url[MAX_URL_LEN];

	char       m_fakeUrl[MAX_URL_LEN];

	char       m_username[MAX_USER_SIZE];
	int32_t       m_numSent;
	int32_t       m_numReceived;
	Msg7       m_msg7; // for injecting
	SafeBuf    m_sbuf;
	SafeBuf    m_xmlBuf;

	Msgfb  m_msgfb;

	char *m_evUrl   ;
	char *m_evTitle ; 
	char *m_evDesc  ; 
	char *m_evTags  ; 
	char *m_evSite  ;
	char *m_evVenue ; 
	char *m_evStreet; 
	char *m_evCity  ; 
	char *m_evState ; 
	char *m_uid     ;

};

static bool sendAddEventTail ( State9 *st9 ) ;
static bool sendErrorReply9 ( State9 *st9 ) ;

static bool sendReply ( void *state ) ;

/*
static void doneInjectingWrapper ( void *state ) {
	State9 *st1 = (State9 *)state;
	// call sendReply to send back the page
	sendReply ( st1 );
}
*/

bool gotCaptchaReply ( State9 *st , TcpSocket *s ) ;

void gotCaptchaReplyWrapper ( void *st , TcpSocket *s ) {
	gotCaptchaReply ( (State9 *)st , s );
}

static bool sendPageAddEvent2 ( State9 *st9 ) ;

static void gotUserInfoWrapper3 ( void *state ) {
	State9 *st9 = (State9 *)state;
	sendPageAddEvent2 ( st9 );
}

// only allow up to 1 Msg10's to be in progress at a time
static bool s_inprogress = false;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageAddEvent ( TcpSocket *s , HttpRequest *r ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  urlLen = 0;
	//char *url = r->getString ( "addeventurl", &urlLen , NULL );

	// see if they provided a url of a file of urls if they did not
	// provide a url to add directly
	bool isAdmin = g_collectiondb.isAdmin ( r , s );

	if ( g_repairMode ) {
		g_errno = EBADENGINEER;
		g_msg = " (error: temporarily disabled)";
		return g_httpServer.sendErrorReply(s,500,"add event"
						   "temporarily disabled");
	}

	// can't be too long, that's obnoxious
	if ( urlLen > MAX_URL_LEN ) {
		g_errno = EBUFTOOSMALL;
		g_msg = " (error: url too long)";
		return g_httpServer.sendErrorReply(s,500,"url too long");
	}

	// get the collection
	int32_t  collLen = 0;
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
	State9 *st9 ;
	try { st9 = new (State9); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAddEvent: new(%i): %s", 
		    sizeof(State9),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); }
	mnew ( st9 , sizeof(State9) , "PageAddEvent" );

	// save socket and isAdmin
	st9->m_socket  = s;
	st9->m_isMasterAdmin = isAdmin;

	st9->m_hr.copy ( r );

	char *username     = g_users.getUsername(r);
	if(username) strcpy(st9->m_username,username);
	//st9->m_spiderLinks = true;
	//st9->m_strip   = true;

	// save the collection name in the State9 class
	if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	strncpy ( st9->m_coll , coll , collLen );
	st9->m_coll [ collLen ] = '\0';



	// . what friend ids have liked/attended events
	// . their facebook id should be in the cookie once they login
	if ( ! st9->m_msgfb.getFacebookUserInfo ( &st9->m_hr ,
						  st9->m_socket,
						  coll,
						  st9 ,
						  "addevent", // redirPath
						  gotUserInfoWrapper3 ,
						  0 )) // niceness
		// return false if we blocked
		return false;

	return sendPageAddEvent2 ( st9 );
}

bool printPageTitle ( SafeBuf &sb , char *title ) {
	sb.safePrintf("<table cellpadding=0 cellspacing=0>"
		      "<tr>"
		      "<td " // colspan=2 "
		      "width=196px "
		      "style=padding-top:20px;padding-bottom:20px;>"
		      );
	printLogoTD ( sb , false );
	sb.safePrintf("</td>");

	//
	// print page title (i.e. "Blog")
	//
	sb.safePrintf ( // spacer
		       //"<td width=30px>"
		       //	"&nbsp;"
			//"&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
			//"</td>"
			"<td><b><span style=\""
			"color:white;"
			"font-size:45px;"
			"outline-color:black;"
			"outline-width:2px;"
			"outline-style:inherit;"
			"text-shadow: 2px 4px 10px black;"
			"\">%s</span></b></td>"
			// force the other td cells to their specified widths:
			//"<td width=100%%>&nbsp;</td>"
			// - END logo/query row
			"</tr>"
			// end table from above
			"</table>" 
			, title
			);

	return true;
}

bool sendPageAddEvent2 ( State9 *st9 ) {

	// save the url
	//st9->m_evUrl[0] = '\0';
	//if ( url ) {
	//	st9->m_urlLen=cleanInput(st9->m_url,MAX_URL_LEN, url, urlLen);
	//}

	SafeBuf &sb = st9->m_sbuf;


	// the <html> stuff
	printHtmlHeader ( sb , "Add Event" , 1,NULL,true);//false);

	printBlackBar(sb,&st9->m_msgfb,"/addevent",st9->m_socket->m_ip,
		      1,0,NULL);


	printPageTitle ( sb , "Add Event" );

	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( st9->m_coll );

	// are we temporarily disabled???
	if ( ! g_conf.m_addUrlEnabled ||
	     ! cr->m_addUrlEnabled    ||
	     g_conf.m_readOnlyMode    ||
	     g_repairMode             ) {
		sb.safePrintf("<br><b>Add Event Temporarily Disabled. Please "
			      "try again later.</b><br>");
		return sendErrorReply9 ( st9 );
	}

	// this downloads from google to see if the answer was correct
	if ( st9->m_hr.getLong("captchasubmit",0) ) {
		// . send out request to google
		// . this returns false and sets g_errno on error
		// . returns true if blocked and will call callback
		if ( verifyCaptchaInput ( st9->m_socket, 
					  &st9->m_hr, 
					  st9, 
					  gotCaptchaReplyWrapper))
			return false;
		// we had an error sending to google...
		return sendErrorReply9 ( st9 );
	}

	// be on our merry way, i guess did not have to do a captcha
	return gotCaptchaReply ( st9 , NULL );
}

void sendReplyWrapper ( void *state ) {
	sendReply ( state );
}

// come here if we got the captcha reply from google
bool gotCaptchaReply ( State9 *st9 , TcpSocket *s ) {

	SafeBuf *sb = &st9->m_sbuf;
	HttpRequest *r = &st9->m_hr;
	CollectionRec *cr = g_collectiondb.getRec ( st9->m_coll );

	// failed captcha?
	bool passed = true;
	// turn off captcha for now
	//if ( s ) passed = isCaptchaReplyCorrect ( s );
	if ( ! passed && ! st9->m_isMasterAdmin ) {
		sb->safePrintf("<br><b>Captcha had incorrect answer</b><br>");
		//return sendErrorReply9 ( st9 );
	}

	// get ip of submitter
	//uint32_t h = ipdom ( s->m_ip );
	// . use top 2 bytes now, some isps have large blocks
	// . if this causes problems, then they can do pay for inclusion
	/*
	uint32_t h = iptop ( s->m_ip );
	int32_t codeLen;
	char* code = r->getString("code", &codeLen);
	if(g_autoBan.hasCode(code, codeLen, s->m_ip)) {
		int32_t uipLen = 0;
		char* uip = r->getString("uip",&uipLen);
		int32_t hip = 0;
		//use the uip when we have a raw query to test if 
		//we can submit
		if(uip) {
			hip = atoip(uip, uipLen);
			h = iptop( hip );
		}
	}
	*/

	int32_t now = getTimeGlobal();

	TcpSocket *socket = st9->m_socket;
	int32_t sip = socket->m_ip;

	uint32_t h = iptop ( sip );
	// . allow 1 submit every 1 hour
	// . restrict by submitter domain ip
	if ( ! st9->m_isMasterAdmin &&
	     ! canSubmit ( h , now , cr->m_maxAddUrlsPerIpDomPerDay ) ) {
		// print error
		sb->safePrintf("<br><b>Exceed quota</b><br>");
		sb->safePrintf("Error. 100 events have "
			       "already been submitted by "
			       "this IP address for the last 24 hours.");
		log("addevents: Failed for user at %s: "
		    "quota breeched.", iptoa(sip));
		// return page
		return sendErrorReply9 ( st9 );
	}


	if ( s_inprogress ) {
		sb->safePrintf("<br><b>Add event is under high load. "
			       "Please try again later."
			       "</b><br>");
		log("addevent: Failed for user at %s: "
		    "busy adding another.", iptoa(sip));
		return sendErrorReply9 ( st9 );
	}

	// if any host is dead pretend disabled
	if ( g_hostdb.hasDeadHost ( ) ) {
		sb->safePrintf("<br><b>Add event is experiencing "
			       "technical difficulties. Please try again "
			       "later.</b><br>");
		if ( s ) log("addevent: Failed for user at %s: "
			     "dead host.", iptoa(sip));
		return sendErrorReply9 ( st9 );
	}

	// if they provided the content use that!!!
	int32_t  evTitleLen;
	int32_t  evDescLen;
	int32_t  evTagsLen;
	int32_t  evSiteLen;
	int32_t  evVenueLen;
	int32_t  evStreetLen;
	int32_t  evCityLen;
	int32_t  evStateLen;
	int32_t  uidLen;
	int32_t  evUrlLen;
	char *evUrl    = r->getString("addevurl"   ,&evUrlLen   ,"");
	char *evTitle  = r->getString("addevtitle" ,&evTitleLen ,"");
	char *evDesc   = r->getString("addevdesc"  ,&evDescLen  ,"");
	char *evTags   = r->getString("addevtags"  ,&evTagsLen  ,"");
	char *evSite   = r->getString("addevsite"  ,&evSiteLen  ,"");
	char *evVenue  = r->getString("addevvenue" ,&evVenueLen ,"");
	char *evStreet = r->getString("addevstreet",&evStreetLen,"");
	char *evCity   = r->getString("addevcity"  ,&evCityLen  ,"");
	char *evState  = r->getString("addevstate" ,&evStateLen ,"");
	char *uid      = r->getString("adduid"     ,&uidLen     ,"");

	// save for repopulating form in sendAddEventTail()
	st9->m_evUrl    = evUrl;
	st9->m_evTitle  = evTitle;
	st9->m_evDesc   = evDesc;
	st9->m_evTags   = evTags;
	st9->m_evSite   = evSite;
	st9->m_evVenue  = evVenue;
	st9->m_evStreet = evStreet;
	st9->m_evCity   = evCity;
	st9->m_evState  = evState;
	st9->m_uid      = uid;


	int32_t submitting = r->getLong("submitting",0);
	if ( ! submitting ) return sendAddEventTail ( st9 );

	// if no url given, just print a blank page
	if ( evUrlLen == 0 && evTitleLen == 0 ) {
		sb->safePrintf("<b><font color=red>"
			       "No url or title was supplied</font></b>");
		return sendAddEventTail (  st9 );
	}

	bool isEventGuru = false;
	if ( evUrlLen > 0 && gb_strcasestr(evUrl,"flurbit.com/") )
		isEventGuru = true;
	if ( evUrlLen > 0 && gb_strcasestr(evUrl,"eventguru.com/") )
		isEventGuru = true;
	if ( evUrlLen > 0 && gb_strcasestr(evUrl,"eventwidget.com/") )
		isEventGuru = true;
	if ( isEventGuru ) {
		sb->safePrintf("<b><font color=red>"
			       "Adding %s urls is not allowed."
			       "</font></b>"
			       , APPNAME );
		return sendAddEventTail (  st9 );
	}

	// create an xmldoc from their provided content
	SafeBuf *xb = &st9->m_xmlBuf;
	// start printing crap into that
	xb->safePrintf("<!DOCTYPE text/xml>\n");
	xb->safePrintf("<event>\n");
	xb->safePrintf("\t<eventTitle>%s</eventTitle>\n"  , evTitle  );
	xb->safePrintf("\t<eventDesc>%s</eventDesc>\n"    , evDesc   );
	xb->safePrintf("\t<eventTags>%s</eventTags>\n"    , evTags   );
	xb->safePrintf("\t<eventUrl>%s</eventUrl>\n"      , evSite   );
	xb->safePrintf("\t<eventVenue>%s</eventVenue>\n"  , evVenue  );
	xb->safePrintf("\t<eventStreet>%s</eventStreet>\n", evStreet );
	xb->safePrintf("\t<eventCity>%s</eventCity>\n"    , evCity   );
	xb->safePrintf("\t<eventState>%s</eventState>\n"  , evState  );
	// pubdate is great to have in case we do not have a year for event
	struct tm *timeStruct = gmtime ( &now );
	char tmp[64];
	strftime(tmp,64,"%b-%d-%Y %H:%M:%S", timeStruct);
	xb->safePrintf("\t<pubDate>%s</pubDate>\n",tmp);
	// close it up
	xb->safePrintf("</event>\n");

	char *content = NULL;
	int32_t  contentLen = 0;

	// hash the uid
	uint32_t hashedUID32 = 0;
	if ( uid ) hashedUID32 = hash32n ( uid );
	st9->m_hashedUID32 = hashedUID32;

	// assume none!
	st9->m_fakeUrl[0] = '\0';

	// make a fake url like
	// http://www.eventguru.com/events/y12/m1/d4/h15/m30/{UIDHASH}.xml
	char fakeUrl[256];
	if ( evUrlLen == 0 ) {
		// use this content then!
		content    = xb->getBufStart();
		contentLen = xb->length();
		// and make the fake url so its highly searchable
		sprintf(fakeUrl,
			"http://www.eventguru.com/events/"
			"y%"INT32"/m%"INT32"/d%"INT32"/h%"INT32"/i%"INT32"/s%"INT32"/%"UINT32".xml"
			,(int32_t)timeStruct->tm_year+1900-2000
			,(int32_t)timeStruct->tm_mon+1 // make it 1-12
			,(int32_t)timeStruct->tm_mday
			,(int32_t)timeStruct->tm_hour
			,(int32_t)timeStruct->tm_min
			,(int32_t)timeStruct->tm_sec
			,hashedUID32 );
		evUrl = fakeUrl;
		// store here too
		strcpy ( st9->m_fakeUrl , evUrl );
	}

	// log that xml so we can test it out on page parser
	if ( st9->m_isMasterAdmin && 1 == 2) {
		SafeBuf ttt;
		ttt.safePrintf("<br>"
			       "<a href=/admin/parser?"
			       //"user=mwells&pwd=mwell62&"
			       "c=%s&u=%s&content=",
			       st9->m_coll,
			       st9->m_fakeUrl);
		ttt.urlEncode(content,contentLen);
		ttt.safePrintf("\"><font color=lightgray>TEST</font></a><br>");
		sb->cat ( ttt );
	}



	int32_t forcedIp = atoip("192.168.1.1");

	// 
	// DIRECTLY inject... while you wait...
	//
	// return false if it blocks
	if ( ! st9->m_msg7.inject ( evUrl,
				    forcedIp,
				    content,
				    contentLen,
				    false, // recycleContent,
				    CT_HTML, // what if its xml?
				    st9->m_coll,
				    true , // quickReply,
				    NULL,//st9->m_username,
				    NULL,//st9->m_pwd,
				    MAX_NICENESS , // always when parsing!!!!
				    st9 , 
				    sendReplyWrapper ) ) {
		return false;
	}
	// error?
	return true;
}

bool sendReply ( void *state ) {
	// allow others to add now
	s_inprogress = false;
	// get the state properly
	State9 *st9 = (State9 *) state;
	//HttpRequest *r = &st9->m_hr;

	// in order to see what sites are being added log it, then we can
	// more easily remove sites from sitesearch.gigablast.com that are
	// being added but not being searched
	log(LOG_INFO,"http: add event %s (%s)",
	    st9->m_evUrl,mstrerror(g_errno));


	// extract info from state
	//TcpSocket *s       = st9->m_socket;
	//bool       isAdmin = st9->m_isMasterAdmin;
	//char      *url     = NULL;
	//if ( st9->m_urlLen ) url = st9->m_url;

	// re-null it out if just http://
	//bool printUrl = true;
	//if ( st9->m_urlLen == 0 ) printUrl = false;
	//if ( ! st9->m_url       ) printUrl = false;
	//if(st9->m_urlLen==7&&st9->m_url&&!strncasecmp(st9->m_url,"http://",7)
	//	printUrl = false;

	SafeBuf *sb = &st9->m_sbuf;
	
	// watch out for NULLs
	//if ( ! url ) url = "http://";
	// if there was an error let them know
	if ( g_errno ) {
		sb->safePrintf("<b><font color=red>"
			       "Error adding event: <b>%s[%i]</b></font></b>", 
			       mstrerror(g_errno) , g_errno);
		return sendAddEventTail ( st9 );
	}

	// is this a submission. if not, just print the form out and do not
	// bother with the stuff below here.
	//if ( ! r->getLong("submit",0) ) return sendAddEventTail ( st9 );

	// was address gotten?
	XmlDoc *xd = &st9->m_msg7.m_xd;
	int32_t na = 0;
	if ( xd->m_addressesValid ) na = xd->m_addresses.getNumAddresses();
	int32_t ne = 0;
	if ( xd->m_numHashableEventsValid ) ne = xd->m_numHashableEvents;

	if ( na <= 0 ) {
		sb->safePrintf("<b><font color=red>"
			       "Error adding event: could not find a "
			       "legitimate address.</font></b>");
		return sendAddEventTail ( st9 );
	}

	if ( ne <= 0 ) {
		sb->safePrintf("<b><font color=red>"
			       "Error adding event: found address, but could "
			       "not find an event. Did  it have a time like"
			       " 7pm? And either a recurring day of week like "
			       "<i>Every Wedensday</i> or a specific day like "
			       "<i>April 12th, 2012 at 3pm</i>?</font></b>");
		return sendAddEventTail ( st9 );
	}

	// looks like we got one!
	char *url2 = st9->m_fakeUrl;
	// try using a submitted external url if fake is bogus
	if ( url2 && url2[0] == '\0' ) url2 = st9->m_evUrl;
	sb->safePrintf("Successfully added "
		       "<a href=\"/search?"
		       "showexpiredevents=1&"
		       "restrict=0&"
		       "where=&"
		       "q=url%%3A");
	sb->urlEncode ( url2 );
	sb->safePrintf("\">%"INT32" events</a>.",ne );

	// show iframe of the search results of events we added either
	// from the xml form or the submitted url that was scraped
	/*
	sb->safePrintf("<br>"
		       "<iframe width=800 height=800 "
		       "src=\"/search?showexpiredevents=1&restrict=0&where=&"
		       "q=url%%3A%s\">"
		       "If your browser supported iframes you would be "
		       "seeing search results here! Please upgrade your "
		       "browser.</iframe>",
		       st9->m_fakeUrl);
	
	sb->safePrintf("<br>"
		       "<br>"
		       "You can also see the events you added "
		       "<a href=\"/search?showexpiredevents=1&restrict=0&"
		       "q=url%%3A%s\">"
		       "here</a>."
		       "<br>",
		       st9->m_fakeUrl );
	*/
	
	sb->safePrintf(" (all events added by you "
		       "<a href=\"/?showexpiredevents=1&restrict=0&"
		       "q=inurl%%3A%"UINT32"\">"
		       "here</a>.)"
		       "<br><br>",
		       st9->m_hashedUID32 );


	return sendAddEventTail ( st9 );
}

bool sendAddEventTail ( State9 *st9 ) {
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	SafeBuf *sb = &st9->m_sbuf;
	// print any error first
	if ( g_errno ) 
		sb->safePrintf("ERROR = %s",mstrerror(g_errno));
	// end the last row that has the status msg i fuess
	sb->safePrintf("</TD></TR><TR><TD>");

	// put it in a div so we can round the border corners and
	// make the background white
	sb->safePrintf("<div style=\"border-radius:10px;"
			"box-shadow: 6px 6px 3px %s;"
		       "width:720px;"
		      "padding:10px;"
		       "margin-left:30px;"
		       "margin-right:30px;"
		      "background-color:white;"
		      "border:2px solid black;\">"
		       //,RESULTSWIDTHSTR
		      , SHADOWCOLOR
		       );

	sb->safePrintf("<table width=100%% cellpadding=3 border=0>");


	// sometimes provide a captcha. might set g_errno!!
	bool doCaptcha = true;
	// turn it off for now!!!
	doCaptcha = false;

	if ( doCaptcha ) {
		sb->safePrintf("<tr>"
			       "<td colspan=10>"
			       "<b>Please solve the following captacha "
			       "before submitting information.</b>"
			       "</td></tr>");
		sb->safePrintf("<tr>"
			       //"<td>Captcha</td>"
			       "<td colspan=10>"
			       "<input type=hidden name=captchasubmit "
			       "value=1>\n");
		printCaptcha2 ( sb );
		sb->safePrintf("<br><br><br></td></tr>");
	}



	Msgfb *msgfb = &st9->m_msgfb;

	// if not logged into facebook, they must!
	if ( ! msgfb->m_fbId ) {
		sb->safePrintf("<tr>"
			       //"<td width=20%%></td>"
			       //"<td width=30px></td>"
			       "<td>"// width=%s>"
			       "<br>"
			       "<br>"
			       "<center>"
			       "<b>In order to add events you must "
			       "login with Facebook.</b>"
			       "<br>"
			       "<br>" 
			       "<a id=login2 "
			       "onclick=\""
			       //, RESULTSWIDTHSTR
			       );

		printLoginScript ( *sb );

		sb->safePrintf("\">"
			       "<img "
			       "align=center width=132 height=20 "
			       "src=/fblogin.png border=0></a>" 
			       "</center>"
			       "<br>"
			       "<br>"
			       "</td>"
			       "</tr>" 
			       "</table>"
			       );
	}


	if ( msgfb->m_fbId ) {
		// print the form for adding a url or event
		sb->safePrintf("<tr>"
			       "<td colspan=10>"
			       "<b>"
			       "1. Please enter the url that contains a valid "
			       "event time and address here."
			       "</b>"
			       "<br>"
			       "</td>"
			       "</tr>"
			       
			       "<tr>"
			       "<td width=20%%>Event Url<br></td>"
			       "<td>"
			       "<input type=text name=addevurl size=60 value=\"%s\">"
			       "<br>"
			       "<input type=submit value=\"Submit this Url\">"
			       
			       "</td></tr>"
			       
			       "<tr><td colspan=10>"
			       "<br>"
			       "<br>"
			       "<b><font color=red size=+1>OR</font></b>"
			       "<br>"
			       "<br>"
			       "<br>"
			       "<b> 2. If no url, enter your event information into "
			       "this form:</b>"
			       "<br>"
			       "</td></tr>"
			       
			       "<tr>"
			       "<td>Event Title</td>"
			       "<td><input type=text name=addevtitle value=\"%s\" "
			       "size=60>"
			       "</tr>"
			       
			       "<tr>"
			       "<td valign=top><nobr>Event Description</nobr>"
			       "<br><font size=-1>Include the <b>date</b> and "
			       "<b>time</b> of the "
			       "event or events you are describing, otherwise it "
			       "will not be added.</font>"
			       "</td>"
			       "<td><textarea rows=15 cols=60 name=addevdesc>"
			       "%s"
			       "</textarea></td>"
			       "</tr>"
			       
			       "<tr>"
			       "<td>Event Tags</td>"
			       "<td><input type=text name=addevtags "
			       "value=\"%s\">"
			       "</tr>"

			       "<tr>"
			       "<td>Event Website (optional)</td>"
			       "<td><input type=text name=addevsite "
			       "value=\"%s\">"
			       "</tr>"

			       
			       "<tr>"
			       "<td><nobr>Event Place Name</nobr>"
			       "<br><font size=-1>i.e. Bob's Place</font>"
			       "</td>"
			       "<td valign=top>"
			       "<input type=text name=addevvenue value=\"%s\">"
			       "</td>"
			       "</tr>"
			       
			       
			       "<tr>"
			       "<td>Event Street</td>"
			       "<td><input type=text name=addevstreet value=\"%s\">"
			       "</tr>"
			       
			       "<tr>"
			       "<td>Event City</td>"
			       "<td><input type=text name=addevcity value=\"%s\">"
			       "</tr>"
			       
			       "<tr>"
			       "<td>Event State</td>"
			       "<td><input type=text name=addevstate value=\"%s\">"
			       "</tr>"
			       
			       /* NOW USE FACEBOOK ID
				  "<tr>"
				  "<td><nobr>Your Secret Word</nobr>"
				  "<br><font size=-1>Use this to edit and search "
				  "the events you added. Do not forget it and use the "
				  "same secret word each time you add an event."
				  "</font>"
				  "</td>"
			       */
			       "<tr><td></td>"
			       
			       "<td valign=top>"
			       //"<input type=text name=adduid value=\"%s\">"
			       // attach this other crap
			       "<input type=hidden name=c value=\"%s\">\n"
			       "<input type=hidden name=submitting value=1>\n"
			       //"<br>"
			       //"<br>"
			       "<input type=submit value=\"Submit this Event\">"
			       "</td>"
			       "</tr>" 
			       "</table>"
			       
			       ,st9->m_evUrl
			       ,st9->m_evTitle
			       ,st9->m_evDesc
			       ,st9->m_evTags
			       ,st9->m_evSite
			       ,st9->m_evVenue
			       ,st9->m_evStreet
			       ,st9->m_evCity
			       ,st9->m_evState
			       //,st9->m_uid
			       ,st9->m_coll
			       );
		
	}
	
	sb->safePrintf( "</div></TD></TR></TABLE>"
			"<br><br>" );

	printHtmlTail ( sb , msgfb );
	/*
	sb->safePrintf( "<center>"
			"<font size=-1 color=gray>"
			"Copyright &copy; 2012. All Rights Reserved.</font>"
			"</center>"
			"</div>"
			"</body>"
			"</html>" );
	*/

	// always returns true
	bool status = sendPageBack ( st9->m_socket , 
				     NULL, // si , 
				     sb , 
				     msgfb ,
				     &st9->m_hr );
				     //NULL );

	//bool status = g_httpServer.sendDynamicPage ( st9->m_socket, 
	//					     sb->getBufStart(), 
	//					     sb->length(),
	//					     -1); // cachetime

	// nuke state
	mdelete ( st9 , sizeof(State9) , "PageAddEvent" );
	delete (st9);
	return status;
}

bool sendErrorReply9 ( State9 *st9 ) {
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	SafeBuf *sb = &st9->m_sbuf;
	// print any error first
	if ( g_errno ) 
		sb->safePrintf("ERROR = %s",mstrerror(g_errno));
	bool status = g_httpServer.sendDynamicPage ( st9->m_socket, 
						     sb->getBufStart(), 
						     sb->length(),
						     -1); // cachetime
	// nuke state
	mdelete ( st9 , sizeof(State9) , "PageAddEvent" );
	delete (st9);
	return status;
}
	

// we get like 100k submissions a day!!!
static HashTable s_htable;
static bool      s_init = false;
static int32_t      s_lastTime = 0;
bool canSubmit ( uint32_t h, int32_t now, int32_t maxAddEventsPerIpDomPerDay ) {
	// . sometimes no limit
	// . 0 means no limit because if they don't want any submission they
	//   can just turn off add url and we want to avoid excess 
	//   troubleshooting for why a url can't be added
	if ( maxAddEventsPerIpDomPerDay <= 0 ) return true;
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
	int32_t n = s_htable.getValue ( h );
	// if over 24hr limit then bail
	if ( n >= maxAddEventsPerIpDomPerDay ) return false;
	// otherwise, inc it
	n++;
	// add to table, will replace old values
	s_htable.addKey ( h , n );
	return true;
}


void resetPageAddEvent ( ) {
	s_htable.reset();
}

/////////////////////////
//
// ABOUT PAGE
//
/////////////////////////

class State3 {
public:
	TcpSocket *m_socket;
	Msgfb m_msgfb;
	char m_pageNum;
	HttpRequest m_hr;
};

static bool sendPageAbout2 ( State3 *st3 );

static void gotUserInfoWrapper2 ( void *state ) {
	State3 *st3 = (State3 *)state;
	sendPageAbout2 ( st3 );
	mdelete ( st3 , sizeof(State3) , "PageAbout" );
	delete (st3);
}

#define PAGE_ABOUT    1
#define PAGE_BLOG     2
#define PAGE_PRIVACY  3 
#define PAGE_TERMS    4
#define PAGE_SPIDER   5
#define PAGE_BIO      6
#define PAGE_HELP     7
#define PAGE_API      8
#define PAGE_WIDGET   9
#define PAGE_FRIENDS  10

// path is /about.html or /blog.html
bool sendPageAbout ( TcpSocket *s , HttpRequest *r , char *path ) {
	// make a new state
	State3 *st3;
	try { st3 = new (State3); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("PageAbout: new(%i): %s", 
		    sizeof(State3),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); }
	mnew ( st3 , sizeof(State3) , "PageAbout" );

	// hide it in here
	st3->m_socket = s;

	st3->m_hr.copy ( r );

	int32_t pathLen = gbstrlen(path);
	char page = 0;
	if ( ! strncmp(path,"/blog.html",pathLen   ) ) page = PAGE_BLOG;
	if ( ! strncmp(path,"/about.html",pathLen  ) ) page = PAGE_ABOUT;
	if ( ! strncmp(path,"/privacy.html",pathLen) ) page = PAGE_PRIVACY;
	if ( ! strncmp(path,"/terms.html",pathLen  ) ) page = PAGE_TERMS;
	if ( ! strncmp(path,"/spider.html",pathLen ) ) page = PAGE_SPIDER;
	if ( ! strncmp(path,"/widget.html",pathLen ) ) page = PAGE_WIDGET;
	if ( ! strncmp(path,"/friends.html",pathLen) ) page = PAGE_FRIENDS;
	if ( ! strncmp(path,"/bio.html",pathLen    ) ) page = PAGE_BIO;
	if ( ! strncmp(path,"/help.html",pathLen   ) ) page = PAGE_HELP;
	if ( ! strncmp(path,"/api.html",pathLen    ) ) page = PAGE_API;
	if ( ! page ) page = PAGE_ABOUT;
	st3->m_pageNum = page;


	char *coll = r->getString("c",NULL);
	if ( ! coll ) coll = g_conf.m_defaultColl;
	// . what friend ids have liked/attended events
	// . their facebook id should be in the cookie once they login
	if ( ! st3->m_msgfb.getFacebookUserInfo ( &st3->m_hr ,
						  st3->m_socket,
						  coll,
						  st3 ,
						  "", // redirPath
						  gotUserInfoWrapper2 ,
						  0 )) // niceness
		// return false if we blocked
		return false;

	return sendPageAbout2 ( st3 );
}


bool sendPageAbout2 ( State3 *st3 ) {
	TcpSocket *s = st3->m_socket;
	SafeBuf sb;

	// int16_tcut
	char page = st3->m_pageNum;

	// we now print two pages here
	char *path = "/about.html";
	char *title = "About";
	char *titleTag = "About";
	if ( page == PAGE_BLOG ) {
		path = "/blog.html";
		title = "Blog";
		titleTag = "Blog";
	}
	if ( page == PAGE_PRIVACY ) {
		path = "/privacy.html";
		title = "<nobr>Privacy Policy</nobr>";
		titleTag = "Privacy Policy";
	}
	if ( page == PAGE_TERMS ) {
		path = "/terms.html";
		title = "<nobr>Terms of Use</nobr>";
		titleTag = "Terms of Use";
	}
	if ( page == PAGE_BIO ) {
		path = "/bio.html";
		title = "<nobr>Matt Wells</nobr>"; // Bio";
		titleTag = "Matt Wells";
	}
	if ( page == PAGE_SPIDER ) {
		path = "/spider.html";
		title = "EventGuruBot";
		titleTag = title;
	}
	if ( page == PAGE_WIDGET ) {
		path = "/widget.html";
		title = "<nobr>The Widget</nobr>";
		titleTag = "The Widget";
	}
	if ( page == PAGE_FRIENDS ) {
		path = "/friends.html";
		title = "<nobr>Invite Friends</nobr>";
		titleTag = "Invite Friends";
	}
	if ( page == PAGE_HELP ) {
		path = "/help.html";
		title = "Help";
		titleTag = "Help";
	}
	if ( page == PAGE_API ) {
		path = "/api.html";
		title = "API";
		titleTag = title;
	}

	// the <html> stuff
	printHtmlHeader ( sb , titleTag , 1,NULL,true);


	printBlackBar ( sb , &st3->m_msgfb , path , s->m_ip,1 ,0,NULL);

	printPageTitle ( sb , title );


	sb.safePrintf ( "<table cellspacing=0 cellpadding=0>"
			"<tr>"
			);
	

	if ( page == PAGE_BLOG || page == PAGE_ABOUT )
		sb.safePrintf(
			      "<td valign=top>"
			      // put sidebar in curvy div

			      "<div "
			      //"class=grad2 "
			      "style=\"border-radius:10px;"
			      "box-shadow: 6px 6px 3px %s;"
			      "width:196px;"
			      "padding:10px;"
			      "margin-left:5px;"
			      "background-image:url('/ss.jpg');"
			      "background-repeat:repeat;"
			      "background-attachment:fixed;"
			      //"margin-right:5px;"
			      "line-height:17px;"
			      //"background-color:white;"
			      "border:2px solid black;\">"

			      // bio sidebar gradient
			      "<div style=\""
			      "color:black;"//white;"
			      "width:196px;"
			      "height:22px;"
			      "font-size:16px;"
			      "\">"
			      "<b>"
			      "Founder Bio"
			      "</b>"
			      "</div>"

			      "<div style=\""
			      "color:black;"
			      "font-size:13px;"
			      "\">"

			      "<br>"

			      "<center>"
			      "<img width=190 height=225 src=http://www.gigablast.com/images/matt_founder-lg.jpg style=\"border:2px solid black;\">"

			      "</center>"
			      // spacer
			      "<br>"

			      "%s was created by <a href=/bio.html style=color:blue;>Matt Wells</a>"
			      //" (<a href=/bio.html style=color:blue;>bio</a>)"
			      ", who also created <a href=http://www.gigablast.com/ style=\"color:blue;\">Gigablast</a>, the web's largest clean energy search engine. %s represents a paradigm shift in the event space. The %s technology was built on the same technology used by the Gigablast web search engine. In aggregate, it represents about twelve years of work, which aside from a few third party contributions, was all performed by Matt. Matt is currently seeking a way to take this project to the next level."
			      "</font>"
			      "<br>"
			      "</div>"
			      "</div>"
			      
			      , SHADOWCOLOR
			      , APPNAME
			      //, GRADFONT
			      , APPNAME
			      , APPNAME
			      );

	// spacer
	sb.safePrintf("<td width=10px></td>");

	// establish RIGHT CELL width
	sb.safePrintf ( "<td width=%s valign=top>" , RESULTSWIDTHSTR );

	// put it in a div so we can round the border corners and
	// make the background white
	if ( page != PAGE_FRIENDS && page != PAGE_WIDGET )
		sb.safePrintf("<div style=\"border-radius:10px;"
			      "box-shadow: 6px 6px 3px %s;"
			      "padding:10px;"
			      "background-color:white;"
			      "border:2px solid black;\">"
			      , SHADOWCOLOR
			      );

	if ( page == PAGE_ABOUT )
		sb.safePrintf (
			       "<br>"
			       "%s is the largest event search engine in the U.S. It is also the only search engine to spider the whole web for unstructured events. It was programmed to use the same analytical processes of the human mind in order to determine the event information from an arbitrary website. The %s technology has been in use and development for over ten years and has data-mined millions of events."
			       "</p>"
			       "<br><br>"
			       "&bull; Try our search service on the <a href=/>%s homepage</a>."
			       "<br><br>"
			       
			       "&bull; Install the "
			       "<a href=/?widgetmenu=1&calendar=0&map=0&location=0&social=0&time=0&categories=0&display=0&showwidget=1&within30=1&where=anywhere&sortbydate=1>"
			       "event widget</a> on your website to show your viewers events that you want them to see."
			       "<br><br>"
			       ""
			       ""
			       ""
			       "&bull; <a href=/addevent>Add events</a> in plain english to the database as well as direct the spiders to scrape events off of a web page you already have."
			       "<br><br>"
			       ""
			       "&bull; Incorporate our events into your technology. The <a href=/api.html>xml event stream</a> can be customized to provide your software with only the types of events you want, in a convenient xml format. "
			       "<br><br>"
			       ""
			       // facebook canvas puts us in an iframe
			       // so bust out with target=_parent
			       "&bull; Please <b>contact me</b> by clicking \"Message\" on the <a href=%s target=_parent>%s Facebook page</a>. I appreciate your feedback and need that to improve the service."
			       ""
			       , APPNAME
			       , APPNAME
			       , APPNAME
			       , APPFBPAGE
			       , APPNAME
			       );

	if ( page == PAGE_API ) {
		sb.safePrintf(
			      "<span style=\"font-size:16px;\">"
			      "<br>"
			      "For developers that require the highest level "
			      "of control, "
			      "Event Guru can return search results in XML "
			      "format using a url like: "
			      "<a href=/?xml=1&where=san+diego&"
			      "radius=30&q=party>http://www.eventguru."
			      "com/?xml=1&where=san+diego&radius=30&q=party...</a>"
			      "<br><br>"
			      "The only thing we request is that if you use "
			      "this then you please place a link like<br>"
			      "<i><u>"
			      "&lt;a href=http://www.eventguru.com/&gt;"
			      "Events&lt;/a&gt; powered by EventGuru.com"
			      "</u></i>"
			      "<br>on the page that is displaying the events."
			      "<br><br>"
			      "<a href=#output>Jump to the XML Output "
			      "Description</a>"
			      "<br><br>"
			      "<table cellpadding=10>"
			      "<tr height=22px><td colspan=10 class=gradgreen>"
			      "<center><b>Input Parameters</b></center>"
			      "</td></tr>"
			      );
		sb.safePrintf ("<tr><td><b>Name</b></td>"
			       "<td><b>Value</b></td>"
			       "<td><b>Default</b></td>"
			       //"<td><b>Note</b></td>"
			       "<td><b>Description</b></td>"
			       "</tr>\n" );
		
		int32_t count = 0;
		// from SearchInput.cpp:
		for ( int32_t i = 0 ; i < g_parms.m_numSearchParms ; i++ ) {
			Parm *parm = g_parms.m_searchParms[i];
			// check if we should print it...
			if ( ! ( parm->m_flags & PF_API ) ) continue;
			//if ( parm->m_flags & PF_SUBMENU_HEADER ) continue;
			//if ( ! parm->m_flags ) continue;
			// print it
			if ( ! parm->m_sparm ) continue;
			// use m_cgi if no m_scgi
			char *cgi = parm->m_cgi;
			if ( parm->m_scgi ) cgi = parm->m_scgi;
			// alternat bg color
			char *bgcolor = "";
			if ( ++count % 2 == 1 )
				bgcolor = " bgcolor=lightgray";
			// print the parm
			sb.safePrintf ( "<tr><td%s><b>%s</b></td>"
					"<td%s>", bgcolor,cgi,bgcolor );
			if ( parm->m_type == TYPE_BOOL2 ) 
				sb.safePrintf ( "0 or 1" );
			else if ( parm->m_type == TYPE_BOOL ) 
				sb.safePrintf ( "0 or 1" );
			else if ( parm->m_type == TYPE_CHAR ) 
				sb.safePrintf ( "CHAR" );
			else if ( parm->m_type == TYPE_CHAR2 ) 
				sb.safePrintf ( "CHAR" );
			else if ( parm->m_type == TYPE_FLOAT ) 
				sb.safePrintf ( "FLOAT" );
			else if ( parm->m_type == TYPE_IP ) 
				sb.safePrintf ( "IP" );
			else if ( parm->m_type == TYPE_LONG ) 
				sb.safePrintf ( "INT32" );
			else if ( parm->m_type == TYPE_LONG_LONG ) 
				sb.safePrintf ( "INT64" );
			else if ( parm->m_type == TYPE_STRING ) 
				sb.safePrintf ( "STRING" );
			else if ( parm->m_type == TYPE_STRINGBOX ) 
				sb.safePrintf ( "STRING" );
			else
				sb.safePrintf ( "OTHER" );

			char *def = parm->m_def;
			if ( ! def ) def = "";
			char *desc = parm->m_desc;
			if ( ! desc || ! desc[0] ) 
				desc = "Show events from this category.";
			sb.safePrintf ( "</td>"
					"<td%s>%s</td>" // default
					//"<td nowrap=1>%s</td>"
					"<td%s><font size=-1>%s</font>"
					"</td></tr>\n",
					//parm->m_title, 
					bgcolor,
					def,
					bgcolor,
					desc );
		}
		// close it up
		sb.safePrintf ( "</table><br><br>" );


		sb.safePrintf ( "<a name=output></a>");

		sb.safePrintf("</span>"
			      "<span style=\"font-size:14px;\">"
			      );


		sb.safePrintf("<table cellpadding=10>"
			      "<tr height=22px><td colspan=10 "
			      "class=gradorange>"
			      "<center><b>XML Output Description</b></center>"
			      "</td></tr>"
			      "<tr><td>"
			      );

		// . print out a sample in preformatted xml
		// . just use &xml=1&help=1 then each field will be
		//   described in line!!
		// . so we can use an iframe for this...
		// . or just do a query and paste it into here!!!
		char *xmlOutput = 

		"# The first xml tag is the response tag.\n"
		"<response>\n"
		"\n"

		"# This is the current time in UTC that was used for the "
		"query\n"
		"<currentTimeUTC>1332453466</currentTimeUTC>\n"
		"\n"

		"# This is the latitude and longitude location of the user.\n"
		"# Based on the user's IP address or her GPS coords\n"
		"# or based on the city derived from the entered location\n"
		"<userCityLat>35.0844879</userCityLat>\n"
		"<userCityLon>-106.6511383</userCityLon>\n"
		"\n"

		"# This is the lat/lon bounding box to which the search\n"
		"# was constrained.\n"
		"<boxLatCenter>35.0844955</boxLatCenter>\n"
		"<boxLonCenter>-106.6511383</boxLonCenter>\n"
		"<boxRadius>100.00</boxRadius>\n"
		"<boxMinLat>33.6352234</boxMinLat>\n"
		"<boxMaxLat>36.5337677</boxMaxLat>\n"
		"<boxMinLon>-108.1004105</boxMinLon>\n"
		"<boxMaxLon>-105.2018661</boxMaxLon>\n"
		"\n"

		"# This is how long the search took in milliseconds\n"
		"<responseTime>0</responseTime>\n"
		"\n"

		"# This is how many events are contained in this response\n"
		"<hits>20</hits>\n"
		"\n"

		"# This is '1' if more events follow or 0 if not\n"
		"<moreResultsFollow>0</moreResultsFollow>\n"
		"\n"

		"# This is the current time in the timezone of that of\n"
		"# the majority of the events in the contained herein\n"
		"<serpCurrentLocalTime>"
		"<![CDATA[ Thu Mar 22, 3:57 PM MDT ]]>"
		"</serpCurrentLocalTime>\n"
		"\n"
		
		"# This is the indicator of an event\n"
		"<result>\n"
		"\n"

		"# This is the title of the event\n"
		"<eventTitle>"
		"<![CDATA[ UNM'S Poli Sci 101: Pimping IS Easy! ]]>"
		"</eventTitle>\n"
		"\n"

		"# This is a 32-bit hash of the event's summary\n"
		"<eventSummaryHash32>328692420</eventSummaryHash32>\n"
		"\n"

		"# This is the summary of the event\n"
		"<eventDesc>\n"
		"\t<![CDATA["
		"\tNot just by cheering me on when I announce myself or the general radness I've come to expect from you, but the fact that there was a friggin' waiting list of teams to buy me drinks. All of you knew Vanilla Ice, which I suppose says something about the longevity of crap."
		"]]>\n"
		"<![CDATA["
		"Sorry Lauryn Hill, a white boy named Bob Van Winkle has stood the <font style=\"color:black;background-color:yellow\">test</font> of time better than you."
		"\t]]>\n"
		"</eventDesc>\n"
		"\n"

		"# This represents all the date intervals the event occurs\n"
		"# at over the next year or two. The two numbers in each\n"
		"# closed interval are UNIX timestamps in UTC.\n"
		"# There can be hundreds of intervals.\n"
		"<eventDateIntervalsUTC>"
		"<![CDATA["
		"[1332471600,1332471600],[1333076400,1333076400],[1333681200,1333681200],"
		"]]>"
		"</eventDateIntervalsUTC>\n"
		"\n"

		"# This is how long until the next occurence of this event\n"
		"<eventCountdown>"
		"<![CDATA[ in 5 hours 2 minutes on Thu, Mar 22 @ 9pm ]]>"
		"</eventCountdown>\n"
		"\n"

		"# This is the canonical time of the event\n"
		"<eventEnglishTime>"
		"<![CDATA[ every Thursday at 9 pm ]]>"
		"</eventEnglishTime>\n"
		"\n"

		"# This is the canonical time of the event, truncated, in\n"
		"# case it is a huge list of times.\n"
		"<eventEnglishTimeTruncated>"
		"<![CDATA[ every Thursday at 9 pm ]]>"
		"</eventEnglishTimeTruncated>\n"
		"\n"

		"# This is the url the event came from\n"
		"<url>"
		"<![CDATA["
		"http://www.geekswhodrink.com/index.cfm?event=client.page&pageid=90&contentid=2146"
		"\t]]>"
		"</url>\n"
		"\n"

		"# This is the url of the cached copy of that page\n"
		"<cachedUrl>"
		"<![CDATA["
		"/?id=229952262607.3596429002840667676"
		"]]>"
		"</cachedUrl>\n"
		"\n"

		"# This is the size of that web page in Kilobytes\n"
		"<size>63.0</size>\n"
		"\n"

		"# This is the numeric Document Identifier of that page\n"
		"<docId>229952262607</docId>\n"
		"\n"

		"# The timzone the event is in. Goes from -11 to 11.\n"
		"<eventTimeZone>-7</eventTimeZone>\n"
		"\n"

		"# Does the location the event is in use Daylight "
		"Savings Time?\n"
		"<eventCityUsesDST>1</eventCityUsesDST>\n"
		"\n"

		"# When is the next start time of the event in UTC?\n"
		"# If event is in progress, this is when it started\n"
		"<eventStartTimeUTC>1332457200</eventStartTimeUTC>\n"
		"\n"

		"# When is the next end time of ths event in UTC?\n"
		"<eventEndTimeUTC>1332464400</eventEndTimeUTC>\n"
		"\n"

		"# When is the next start monthday, month and year of the\n"
		"# event in UTC? If event is in progress, this is when it\n"
		"# started\n"
		"<eventStartMonthDay>22</eventStartMonthDay>\n"
		"<eventStartMonth>3</eventStartMonth>\n"
		"<eventStartYear>2012</eventStartYear>\n"
		"\n"

		"# How far away is the event in Manhattan distance?\n"
		"<drivingMilesAway>8.6</drivingMilesAway>\n"
		"\n"

		"# The address broken down into separate tags\n"
		"# The state here is always a two character abbreviation.\n"
		"# The country here is always a two character abbreviation.\n"
		"<eventVenue>"
		"<![CDATA[ Wendy's ]]>"
		"</eventVenue>\n"
		"<eventStreet>"
		"<![CDATA[ 410 Eubank Boulevard Northeast ]]>"
		"</eventStreet>\n"
		"<eventCity>"
		"<![CDATA[ Albuquerque ]]</b>"
		"</eventCity>\n"
		"<eventState>"
		"<![CDATA[ NM ]]>"
		"</eventState>\n"
		"<eventCountry>"
		"<![CDATA[ US ]]>"
		"</eventState>\n"
		"\n"


		"# The latitude and longitude of the event according to\n"
		"# The geocoder, if available.\n"
		"<eventGeocoderLat>35.0784380</eventGeocoderLat>\n"
		"<eventGeocoderLon>-106.5322530</eventGeocoderLon>\n"
		"\n"

		"# The latitude and longitude of the centroid of the city\n"
		"# that the event is taking place in, if available\n"
		"<eventCityLat>35.0844917</eventCityLat>\n"
		"<eventCityLon>-106.6511383</eventCityLon>\n"
		"\n"

		"# The final latitude and longitude of the event\n"
		"# You can count on this to be there.\n"
		"<eventBalloonLetter>A</eventBalloonLetter>\n"
		"<eventBalloonLat>35.0784380</eventBalloonLat>\n"
		"<eventBalloonLon>-106.5322530</eventBalloonLon>\n"
		"\n"

		"# The website the event came from and the 32-bit\n"
		"# hash of that site and its domain.\n"
		"<site>www.facebook.com</site>\n"
		"<siteHash32>1486592848</siteHash32>\n"
		"<domainHash32>2890457068</domainHash32>\n"
		"\n"

		"# The last time the event was spidered. A UNIX timestamp\n"
		"# in UTC.\n"
		"<spiderDate>1332346009</spiderDate>\n"
		"\n"

		"# The last time the event was successfully indexed\n"
		"# A UNIX timestamp in UTC.\n"
		"<indexDate>1332346009</indexDate>\n"
		"\n"

		"# The content type of the page the event was indexed from.\n"
		"# Values can be 'xml' or 'html'\n"
		"<contentType>xml</contentType>\n"
		"\n"

		"# The language of the page the event was on.\n"
		"# Values are the standard two-letter abbreviations, like\n"
		"# 'en' for english. It uses 'Unknown' if unknown.\n"
		"<language>"
		"<![CDATA[ Unknown ]]>"
		"</language>\n"
		"\n"

		"# That does it!\n"
		"</result>\n"

		"</response>\n";

		// reserve an extra 3k
		sb.reserve ( 25000 );

		// get ptr
		char *dst = sb.getBuf();
		char *src = xmlOutput;

		bool inBold = false;
		bool inFont = false;

		// copy into sb, but rewrite \n as <br> and
		// < as &lt; and > as &gt;
		for ( ; *src ; src++ ) {
			if ( *src == '#' ) {
				gbmemcpy ( dst,"<font color=gray>",17);
				dst += 17;
				inFont = true;
			}
			if ( *src == '<' ) {
				gbmemcpy ( dst , "&lt;",4);
				dst += 4;
				// boldify start tags
				//if ( src[1] != '/' && src[1] !='!' ) {
				//	gbmemcpy(dst,"<b>",3);
				//	dst += 3;
				//	inBold = true;
				//}
				continue;
			}
			else if ( *src == '>' ) {
				// end bold tags
				if ( inBold ) {
					gbmemcpy(dst,"</b>",4);
					dst += 4;
					inBold = false;
				}
				gbmemcpy ( dst , "&gt;",4);
				dst += 4;
				continue;
			}
			else if ( *src == '\n' ) {
				if ( inFont ) {
					gbmemcpy(dst,"</font>",7);
					dst += 7;
					inFont = false;
				}
				gbmemcpy ( dst , "<br>",4);
				dst += 4;
				continue;
			}
			// default
			*dst = *src;
			dst++;
		}
		// just in case
		*dst = '\0';
		// update sb length
		sb.m_length = dst - sb.getBufStart();


		sb.safePrintf("</td></tr></table>");

		/*
		sb.safePrintf ( "<table width=100%% cellpadding=2 "
				"bgcolor=#%s border=1>"
				"<tr><td colspan=2 bgcolor=#%s>"
				"<center><b>Query Operators</b></td></tr>"
				"<tr><td><b>Operator</b></td>"
				"<td><b>Description</b>"
				"</td></tr>\n",
				LIGHT_BLUE, DARK_BLUE );
		// table of the query keywords
		int32_t n = getNumFieldCodes();
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// get field #i
			QueryField *f = &g_fields[i];
			// print it out
			char *d = f->desc;
			// fix table internal cell bordering
			if ( d[0] == '\0' ) d = "&nbsp;";
			sb.safePrintf("<tr><td><b>%s</b>:</td><td>%s</td>"
				      "</tr>\n",
				      f->text,d);
		}
		*/
		sb.safePrintf ( "</span>" );
	}

	// they need to login to facebook to invite friends
	if ( page == PAGE_FRIENDS ) {
		sb.safePrintf ( "<br>"
				"<div style=background-color:#707070;"
				"margin-left:10px;>"
				"<div "
				"class=gradyellow "
				"style=\"border:2px solid black;"
				"position:relative;"
				"right:8px;"
				"bottom:8px;"
				"padding:10px;"
				"\">"
				"You make $1 for every friend you "
				"invite from Facebook, provided they login "
				"to Event Guru. Payments will be made "
				"quarterly using PayPal with the email "
				"address you have registered with Facebook. "
				"Payments of less than $5 will "
				"carryover to the "
				"next quarter. No forms to fill out. "
				"<a href=/account.html>Simple "
				"accounting.</a> "
				"It's as easy as that. "
				"What are you waiting for?"
				"<br>"
				"<br>"
				"<center>"
				);
		if ( ! st3->m_msgfb.m_fbId ) {
			sb.safePrintf ( "Login with Facebook "
					"to begin inviting your friends. "
					"<br><br>"
					"<a id=login3 onclick=\""
			      );
			printLoginScript(sb);
			sb.safePrintf("\">"
				      "<img "
				      "align=center width=132 height=20 "
				      "src=/fblogin.png border=0>"
				      "</a>" 
				      "<br>"
				      "&nbsp;"
				      "<br>"
				      );
		}
		else {
			sb.safePrintf ( 
				       "<script src=\"http://connect.facebook."
				       "net/en_US/all.js\"></script>"
				       "<a "
				       "onclick=\"sendRequestVia"
				       "MultiFriendSelector();"
				       "return false;\">"
				       "<b><u style=color:blue;>"
				       "Click here to invite your friends"
				       "</u></b>"
				       "</a>"
				       "<script>"
				       "FB.init({\n"
				       "appId  : '%s',\n"
				       "});\n"
				       "function "
				       "sendRequestViaMultiFriendSelector(){\n"
				       "FB.ui({"
				       "method: 'apprequests',\n"
				       "message: '"
				       "Hi my friend, I recommend "
				       "EventGuru.com "
				       "for discovering interesting local "
				       "events. "
				       "Plus, if you login to Event Guru "
				       "I make a buck.',\n"
				       "data: '%"UINT64"',\n"
				       "title: 'Event Guru Invitation'\n"
				       "}, requestCallback);\n"
				       "}\n"
				       "function requestCallback(response) {\n"
				       "// Handle callback here\n"
				       "}\n"
				       "</script>\n"
				       , APPID
				       , st3->m_msgfb.m_fbId
				       );
		}
		sb.safePrintf("</center>"
			      "</div>"
			      "</div>"
			      "<br><br><br>");
	}


	if ( page == PAGE_BLOG )
		// if we are the blog, read the file and spew that out
		sb.fillFromFile(g_hostdb.m_dir,"html/blog.html");

	if ( page == PAGE_PRIVACY )
		sb.fillFromFile(g_hostdb.m_dir,"html/privacy.html");

	if ( page == PAGE_TERMS )
		sb.fillFromFile(g_hostdb.m_dir,"html/terms.html");

	if ( page == PAGE_SPIDER )
		sb.fillFromFile(g_hostdb.m_dir,"html/spider.html");

	if ( page == PAGE_WIDGET )
		sb.fillFromFile(g_hostdb.m_dir,"html/widget.html");

	if ( page == PAGE_BIO )
		sb.fillFromFile(g_hostdb.m_dir,"html/bio.html");

	if ( page == PAGE_HELP )
		sb.fillFromFile(g_hostdb.m_dir,"html/help.html");


	if ( page != PAGE_FRIENDS && page != PAGE_WIDGET )
		sb.safePrintf("</div>");

	sb.safePrintf ( "</td></tr>"
			"</table>"
			);

	printHtmlTail ( &sb , &st3->m_msgfb );

	// always returns true
	sendPageBack ( s,NULL,&sb,&st3->m_msgfb, &st3->m_hr );//, NULL );

	//g_httpServer.sendDynamicPage ( s ,
	//			       sb.getBufStart(), 
	//			       sb.length(),
	//			       -1); // cachetime
	return true;
}

/*
bool printPopularInterestsFrame ( SafeBuf &sb , State7 *st ) {

	sb.safePrintf("<br>" );

	// make a note in the red box (blue bar)
	printRedBoxes ( sb , st );


	sb.safePrintf (
			"<table cellpadding=10 style=color:blue>"
			"<tr>"
			"<td colspan=10>"
			"<b><font color=black size=+1>"
			//"<center>"
			"Click on what kind of events you like:"
			//"</center>"
			"</font></b>"
			"</td>"
			"</tr>"
			"<tr>"
			"<td valign=top>"
			);

	int32_t n = sizeof(s_interests) / sizeof(char *);
	int32_t cursor = 0;
	int32_t lines = n;
	bool firstBreak = false;
	bool secondBreak = false;

	// cruise the interets
	for ( int32_t i = 0 ; i < n ; i++ ) {

		char *si = s_interests[i];

		// . now list the cities we got for it by pop
		// . scan the city/state pairs
		sb.safePrintf("<a onclick=\""
			      // gotta save as cookie here!
			      // no, what if its a dup! well then do not
			      // allow them to click on dups then!
			      "var e ='&showpop=1&newinterest="
			      "'+encodeURIComponent('%s');"
			      "reloadResults(0,e);\">"
			      //"window.location.href='/?showpop=1&"
			      //"newinterest='+"
			      //"escape('%s');\">"
			      , si
			      );

		sb.safePrintf("%s</a><br><br>\n", si );

		// count these
		cursor++;

		// break at column end
		if ( cursor >= lines/3 && ! firstBreak ) {
			firstBreak = true;
			sb.safePrintf("</td><td valign=top>");
		}
		else if ( cursor >= (2*lines)/3 && ! secondBreak ) {
			secondBreak = true;
			sb.safePrintf("</td><td valign=top>");
		}
	}
			
	// end the table
	sb.safePrintf ( "</td></tr></table>" );

	sb.safePrintf("<center><b>"
		      "<a href=/?showpop=0 style=color:blue;>"
		      "All Done"
		      "</a>"
		      "</b></center>");

	return true;
}
*/

bool printCategoryTable ( SafeBuf &sb , SearchInput *si ) {

	char *s = " style=display:none;";
	if ( si->m_categories ) s = "";

	sb.safePrintf("<div id=cattbl%s>" , s );

	sb.safePrintf (	"<table width=100%% cellpadding=2 style=color:black;"
			// make it easier for ipad users:
			"line-height:30px;>"
			/*
			"<tr>"
			"<td colspan=10>"
			"<b><font color=black>"
			"All events will be restricted to "
			"the categories that you select."
			"</font></b>"
			"</td>"
			"</tr>"
			"<tr>"
			*/
			"<td valign=top>"
			);

	int32_t n = sizeof(s_interests) / sizeof(char *);
	int32_t cursor = 0;
	int32_t lines = n;
	bool firstBreak = false;
	bool secondBreak = false;

	int32_t cols = 3;
	if ( si->m_igoogle ) cols = 1;

	///////////////////////////

	for ( int32_t i = 0 ; i < g_parms.m_numParms ; i++ ) {
		// int16_tcut
		Parm *m = &g_parms.m_parms[i];
		// stop when done
		if ( m->m_subMenu != SUBMENU_CATEGORIES ) continue;
		// skip if type int32_t. the menu up/down state.
		if ( m->m_type != TYPE_BOOL ) continue;
		// get value
		bool isParmSet = m->getValueAsBool ( si );
		// otherwise its a checkbox i guess
		char *checked = ""; 
		if ( isParmSet ) checked = " checked";
		// we have to use a hidden parm since these do not 
		// post anything if the box is unchecked. the "Icons"
		// checkbox is checked by default so if its unchecked we
		// have no way of knowing that we should use the default
		// value or we should not display the icons. so we must
		// toggle this value when the checkbox is clicked!
		// BUT ONLY do this if default value is CHECKED!
		// NO, it turns out there is a race condition between the
		// submit() and setting the checkbox value otherwise, so
		// let's do this for all checkboxes now.
		// NOW we need an "id" as well so setVal() function works!
		//sb.safePrintf("<input type=hidden id=%s name=%s value=%"INT32"",
		//	      m->m_scgi,
		//	      m->m_scgi,
		//	      (int32_t)isParmSet );
		//sb.safePrintf(">");
		// print a nameless checkbox then
		sb.safePrintf ( "<input onclick=\"toggleBool%s(this,'%s');"
				, "" // radio
				, m->m_scgi );
		// the default function is submit to the iframe
		sb.safePrintf("reloadResults(1);" );
		// close up the checkbox
		sb.safePrintf ( "\" "
				"id=cb_%s "
				"type=checkbox%s> "
				"%c%s"
				"<br>"
				// hack for msie (chrome needs the div,
				// so you can just style the br tag)
				"<div style=line-height:0.5em><br></div>"
				, m->m_scgi
				, checked
				, to_upper_a(m->m_title[0])
				, m->m_title + 1 );

		// count these
		cursor++;

		// break at column end
		if ( cursor >= lines/cols && cols>=2 && ! firstBreak ) {
			firstBreak = true;
			sb.safePrintf("</td><td valign=top>");
		}
		else if ( cursor >= (2*lines)/cols && cols>=3 &&!secondBreak){
			secondBreak = true;
			sb.safePrintf("</td><td valign=top>");
		}

	}

	//////////////////////

			
	// end the table
	sb.safePrintf ( "</td></tr></table>" );

	sb.safePrintf("</div>");

	return true;
}

bool printCategoryInputs ( SafeBuf &sb , SearchInput *si ) {
	for ( int32_t i = 0 ; i < g_parms.m_numParms ; i++ ) {
		// int16_tcut
		Parm *m = &g_parms.m_parms[i];
		// stop when done
		if ( m->m_subMenu != SUBMENU_CATEGORIES ) continue;
		// skip if type int32_t. the menu up/down state.
		if ( m->m_type != TYPE_BOOL ) continue;
		// get value
		bool isParmSet = m->getValueAsBool ( si );
		// we have to use a hidden parm since these do not 
		// post anything if the box is unchecked. the "Icons"
		// checkbox is checked by default so if its unchecked we
		// have no way of knowing that we should use the default
		// value or we should not display the icons. so we must
		// toggle this value when the checkbox is clicked!
		// BUT ONLY do this if default value is CHECKED!
		// NO, it turns out there is a race condition between the
		// submit() and setting the checkbox value otherwise, so
		// let's do this for all checkboxes now.
		// NOW we need an "id" as well so setVal() function works!
		sb.safePrintf("<input type=hidden id=%s name=%s value=%"INT32"",
			      m->m_scgi,
			      m->m_scgi,
			      (int32_t)isParmSet );
		sb.safePrintf(">");
	}
	return true;
}

////////////////////
//
// print facebook user stats!
//
////////////////////

class State66 {
public:
	TcpSocket *m_socket;
	Msg0       m_msg0;
	RdbList    m_list;
	int32_t       m_printCount;
	int32_t       m_i;
	SafeBuf    m_sbuf;
	Msgfb m_msgfb;
};

static bool doMsg0Loop           ( State66 *st ) ;
static bool printFacebookRecsOut ( State66 *st ) ;

static void gotFacebookListWrapper ( void *state ) {
	State66 *st = (State66 *)state;
	// print out the recs in this list into st->m_sbuf
	printFacebookRecsOut ( st );
	// and do the next host
	doMsg0Loop ( st );
}

// . print the facebook info of all users we got
// . print the email links
// . call Facebook.cpp's Emailer::sendSingleEmail() to send email
bool sendPageFacebookStats ( TcpSocket *s , HttpRequest *hr ) {

	// error if ip not local
	if ( ! hr->isLocal() ) {
		log("facebook: ip is not local for fb.html");
		g_errno = ENOTLOCAL;
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}

	// make a new state
	State66 *st;
	try { st = new (State66); }
	catch ( ... ) {
		g_errno = ENOMEM;
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	mnew ( st , sizeof(State66) , "PageFBStats");
	// save this
	st->m_socket = s;
	st->m_printCount = 0;
	st->m_i = 0;

	// we must be matt wells fbid
	if ( ! st->m_msgfb.getFacebookUserInfo ( hr , // it will copy this!
						 st->m_socket,
						 "",//coll,
						 st ,
						 "", // redirPath
						 gotFacebookListWrapper,
						 0 )) // niceness
		// return false if we blocked
		return false;
	
	// we got it without blocking!
	return doMsg0Loop ( st );
}

bool doMsg0Loop ( State66 *st ) {
	//
	// loop over all hosts with a msg0
	//
	for ( ; st->m_i < g_hostdb.m_numHosts ; ) {
		// get host
		Host *h = g_hostdb.getHost ( st->m_i );
		// adance incase msg0 blocks
		st->m_i++;
		// stick with stripe #0
		if ( h->m_stripe != 0 ) continue;
		key96_t startKey;
		key96_t endKey;
		startKey.setMin();
		endKey  .setMax();
		// send to it
		Msg0 *m = &st->m_msg0;
		if ( ! m->getList ( h->m_hostId             , // hostId
				    0                       , // ip
				    0                       , // port
				    0                       , // maxCacheAge
				    false                   , // addToCache
				    RDB_FACEBOOKDB          , // was RDB_DATEDB
				    "", //m_coll                  ,
				    &st->m_list           ,
				    (char *)&startKey    ,
				    (char *)&endKey         ,
				    100000000 , // minRecSizes = 100MB
				    st , // state
				    gotFacebookListWrapper ,
				    0              , // niceness
				    // default parms follow
				    true  ,  // doErrorCorrection?
				    true  ,  // includeTree?
				    true  ,  // doMerge?
				    -1    ,  // firstHostId
				    0     ,  // startFileNum
				    -1    ,  // numFiles
				    30    ,  // timeout
				    -1    ,  // syncPoint
				    -1    ,  // preferLocalReads
				    NULL  ,  // msg5
				    NULL  ,  // msg5b
				    false ,  // isrealmerge?
				    true  )) // allowpagecache?
			// return false if blocks
			return false;
		// print out the recs in this list into st->m_sbuf
		printFacebookRecsOut ( st );
	}
	// ok, return the page now!
	//return sendFacebookPageBack ( st );
	bool status = sendPageBack ( st->m_socket ,
				     NULL, // SearchInput *si , 
				     &st->m_sbuf , // SafeBuf *sb ,
				     &st->m_msgfb ,
				     NULL ) ; // HttpRequest *hr ) {
	// nuke state
	mdelete ( st , sizeof(State66) , "PageFBStats" );
	delete (st);
	return status;
}

bool printFacebookRecsOut ( State66 *st ) {
	// get list of recs
	RdbList *list = &st->m_list;
	if ( list->isEmpty() ) return true;

	SafeBuf *sb = &st->m_sbuf;

	// if first one print header crap
	if ( st->m_printCount == 0 ) {
		printHtmlHeader ( *sb , 
				  "Facebook Users" , 
				  1, // printprimarydiv
				  NULL, // searchinput *si
				  true); // static page?
		sb->safePrintf("<h1>Facebook Users</h1>");
		sb->safePrintf("<table border=1 style=font-size:12px;>"
			       "<tr>"
			       "<td>#</td>"
			       "<td><nobr>Pic</nobr></td>"
			       "<td><nobr>Name</nobr></td>"
			       "<td><nobr>FbID</nobr></td>"
			       "<td><nobr>Sex</nobr></td>"
			       "<td><nobr>Birthday</nobr></td>"
			       "<td><nobr>appuser?</nobr></td>"
			       "<td><nobr>myradius</nobr></td>"
			       "<td><nobr>mylocation</nobr></td>"
			       "<td><nobr>iplocation</nobr></td>"
			       "<td><nobr>gps</nobr></td>"
			       "<td><nobr>timezone</nobr></td>"
			       "<td><nobr>widgetid</nobr></td>"
			       "<td><nobr>firstlogin date</nobr></td>"
			       "<td><nobr>lastlogin ip</nobr></td>"
			       "<td><nobr>email freq</nobr></td>"
			       "<td><nobr>last email attempt</nobr></td>"
			       "<td><nobr>next retry</nobr></td>"
			       "<td><nobr># interests</nobr></td>"
			       "<td><nobr># friends</nobr></td>"
			       "<td></td>" // showemail
			       "<td></td>" // edit
			       "</tr>"
			       );
		// inc the count
		st->m_printCount++;
	}
			      

	// parse the recs
	for ( list->resetListPtr(); 
	      ! list->isExhausted() ; 
	      list->skipCurrentRecord () ) {
		// get it
		FBRec *rec = (FBRec *) list->getCurrentRec();
		// i guess deserialize it
		deserializeMsg ( sizeof(FBRec),
				 &rec->size_accessToken,
				 &rec->size_friendIds,
				 &rec->ptr_accessToken,
				 rec->m_buf );

		char *ef = "default";
		if ( rec->m_emailFrequency == 1 ) ef = "daily";
		if ( rec->m_emailFrequency == 2 ) ef = "weekly";
		if ( rec->m_emailFrequency == 3 ) ef = "monthly";
		if ( rec->m_emailFrequency == 4 ) ef = "quarterly";
		if ( rec->m_emailFrequency == 5 ) ef = "never";

		int32_t ip = rec->m_lastLoginIP;

		/*
		// hack for ppl we missed...
		int64_t fbId = rec->m_fbId;
		// jezebel
		if ( fbId == 100003381767946LL ) ip = atoip("75.160.49.8");
		// somesh
		if ( fbId == 100000727656299LL ) ip = atoip("117.204.21.161");
		// antonette martinez
		if ( fbId == 839955276LL ) ip = atoip("");
		// karlos barrios
		if ( fbId == 100002101230636LL ) ip = atoip("");
		// tresa a campbell
		if ( fbId == 1807666805LL ) ip = atoip("");
		// prince pascal
		if ( fbId == 100003102747159LL ) ip = atoip("");
		// besplatni
		if ( fbId == 100002916473213LL ) ip = atoip("");
		*/
		

		// . but if that is NULL or autolocate or anywhere or default
		//   or 0.000 0.000 then figure it out based on IP
		// . when we do generate the email we pass in the user's
		//   rec->m_lastLoginIP as the IP so that search input
		//   can use that to generate the user's location if their
		//   rec->m_myLocation is not descriptive enough...
		// . but i'd like to know here what location it's predicting
		//   based on that ip...
		char ipLocation[128];
		char *ipcity = NULL;
		char *ipstate = NULL;
		char *ipcountry = NULL;
		double ipLat = 0.0;
		double ipLon = 0.0;
		ipLocation[0] = '\0';
		getIPLocation ( ip ,
				&ipLat ,
				&ipLon ,
				NULL , // radius
				&ipcity ,
				&ipstate ,
				&ipcountry ,
				ipLocation ,
				128 );
		char tmpWhere[256];
		tmpWhere[0] = 0;
		if ( ipcity && ipstate && ipcountry &&
		     to_lower_a(ipcountry[0]) == 'u' &&
		     to_lower_a(ipcountry[1]) == 's' ) {
			snprintf(tmpWhere,120,"%s, %s",ipcity,ipstate);
		}
		// . if no city or no state then just use country in where box.
		// . test on /?uip=74.82.64.144 which just gives the
		//   centroid lat/lon of the US
		else if ( ipcountry && (! ipcity || ! ipstate) ) {
			snprintf(tmpWhere,120,"%s", ipcountry);
		}
		// country might be null even if lat/lon were provided!
		else { // if ( lat != 0.0 && lon != 0.0 ) {
			snprintf(tmpWhere,120,"%.06f, %.06f",
				 ipLat,ipLon);
		}

		// store
		// print out the juicy stuff
		sb->safePrintf("<tr>"
			       "<td>%"INT32"</td>"
			       "<td><a href=\"http://www.facebook.com/%"INT64"\">"
			       "<img src=\"%s\"></a></td>"
			       "<td><a href=\"http://www.facebook.com/%"INT64"\">"
			       "%s</a></td>" // name
			       "<td>%"UINT64"</td>" // fbid
			       "<td>%s</td>" // sex
			       "<td>%s</td>" // bday
			       "<td>%"INT32"</td>" // appuser?
			       "<td>%"INT32"</td>" // myradius
			       "<td>%s</td>" // mylocation
			       "<td>%s</td>" // iplocation
			       "<td>%.05f,%.05f</td>" // gps
			       "<td>%"INT32"</td>" // timezone
			       "<td>%"UINT64"</td>" // widgetid
			       , st->m_printCount++
			       , rec->m_fbId
			       , rec->ptr_pic_square
			       , rec->m_fbId
			       , rec->ptr_name
			       , rec->m_fbId
			       , rec->ptr_sex
			       , rec->ptr_birthday_date
			       , (int32_t)rec->m_is_app_user
			       , rec->m_myRadius
			       , rec->ptr_myLocation
			       , tmpWhere
			       , rec->m_gpsLat
			       , rec->m_gpsLon
			       , (int32_t)rec->m_timezone
			       , rec->m_originatingWidgetId
			       );

		char *ft = "---";
		if ( rec->m_firstFacebookLogin )
			ft = ctime(&rec->m_firstFacebookLogin);
		sb->safePrintf("<td>%s</td>" , ft );

		sb->safePrintf( "<td>%s</td>" // lastlogin ip
				"<td>%s</td>" // email freq
				, iptoa(ip)
				, ef
				);

		char *et = "---";
		if ( rec->m_lastEmailAttempt )
			et = ctime(&rec->m_lastEmailAttempt);
		sb->safePrintf("<td>%s</td>" , et ); // last email attempt

		// next retry if last was failure
		if ( rec->m_nextRetry )
			sb->safePrintf("<td>%s</td>",ctime(&rec->m_nextRetry));
		else
			sb->safePrintf("<td>----</td>"  );

		// count # interests
		char *p = rec->ptr_mergedInterests;
		int32_t count = 0;
		for ( ; p && *p ; p++ ) if ( *p == ';' ) count++;
		sb->safePrintf("<td>%"INT32"</td>", count ); // # interests
		sb->safePrintf("<td>%"INT32"</td>", rec->m_friend_count );
		// the showemail link
		sb->safePrintf("<td>"
			       "<a href=/?usefbid=%"UINT64"&fh=%"UINT32"&uip=%s&"
			       "emailformat=1&"
			       "map=0&"
			       "usecookie=0&showpersonal=1>showemail</a>"
			       "</td>"
			       , rec->m_fbId
			       , hash32 ( (char *)&rec->m_fbId , 8, 0 )
			       , iptoa(rec->m_lastLoginIP)
			       );
		// edit link
		sb->safePrintf("<td>"
			       "<a href=/?usefbid=%"UINT64"&fh=%"UINT32"&uip=%s&"
			       "emailformat=0&"
			       "map=0&"
			       "usecookie=0&showpersonal=1>edit</a>"
			       "</td>"
			       , rec->m_fbId
			       , hash32 ( (char *)&rec->m_fbId , 8, 0 )
			       , iptoa(rec->m_lastLoginIP)
			       );
		sb->safePrintf("</tr>");
	}

	return true;
}
