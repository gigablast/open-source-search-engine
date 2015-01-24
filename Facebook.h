
#ifndef _FACEBOOK_H_
#define _FACEBOOK_H_

#include "Conf.h"       // for setting rdb from Conf file
#include "Rdb.h"
#include "Msg4.h"
#include "Tagdb.h"
#include "Msg0.h"
#include "Msg1.h"
#include "PageInject.h" // msg7

// likedb flags
#define LF_DELBIT        0x0001 // reserved internal use
#define LF_TYPEBIT       0x0002 // reserved internal use (true if 2nd type)
#define LF_ISEVENTGURUID 0x0004 // reserved internal use
#define LF_FROMFACEBOOK  0x0008
#define LF_PRIVATE       0x0010 // used for LF_SECRET OR LF_CLOSED

#define LF_DECLINED      0x0020
#define LF_LIKE_EG       0x0040
#define LF_HIDE          0x0080
#define LF_REJECT        0x0100 // turk vote
#define LF_ACCEPT        0x0200 // turk vote
#define LF_INVITED       0x0400
#define LF_GOING         0x0800
#define LF_MAYBE_FB      0x1000
#define LF_EMAILED_EG    0x2000 // have we emailed this to the user? dedup!

bool saveQueryLoopState ( ) ;
bool loadQueryLoopState ( ) ;
void facebookSpiderSleepWrapper ( int fd , void *state ) ;

class Facebookdb {
 public:
	void reset();
	bool init  ( );
	bool addColl ( char *coll, bool doVerify = true );
	Rdb *getRdb ( ) { return &m_rdb; };
	Rdb   m_rdb;
	// key.n0 is the user id
	int64_t getUserId ( void *fbrec ) {
		// the delbit is the last bit, so shift over that
		return (((key96_t *)fbrec)->n0) >> 1LL; }
		
	//DiskPageCache m_pc;	
};

/////////////////
//
// Facebook accessor msg class
//
/////////////////

// values for FBRec::m_flags
//#define FB_LOGGEDIN          0x01 // aka success
//#define FB_LOGINERROR        0x02
//#define FB_DOWNLOADEDFRIENDS 0x04 // have we done pipeline #2 on them yet?

// values for FBRec::m_flags
// is it currently in the queue? if it is we display that we are waiting
// on facebook download to complete if the user clicks "show friends events..."
#define FB_INQUEUE             0x01

// we store this in Facebookdb
class FBRec {
 public:

	FBRec() { reset(); }
	void reset() { memset ( (char *)this,0,sizeof(FBRec) ); };

	// start of an rdb record
	key96_t m_key;
	int32_t    m_dataSize;

	// i've seen these up to 999 trillion
	int64_t m_fbId;
	// used for fetching
	int32_t   m_flags;
	time_t m_accessTokenCreated;
	time_t m_eventsDownloaded;
	//int32_t   m_reserved1;
	char   m_emailFrequency; // for just for you
	char   m_reserved1b;
	char   m_reserved1c;
	char   m_reserved1d;

	int32_t   m_lastEmailAttempt;//reserved3; (for Recommendations)
	int32_t   m_nextRetry; // when to retry email if it failed (Recommenda...)
	int32_t   m_timeToEmail; // in minutes of the day (Recommendations)
	int32_t   m_myRadius; // for "Recommendations/JustForYou"

	// . from what user was this user referred? 
	// . this is how we pay the widgetmasters.
	// . 0 means not referred via a widgetmaster's widget
	int64_t m_originatingWidgetId;
	// . the date they first logged into facebook (UTC)
	// . also used to determine payment
	time_t    m_firstFacebookLogin;
	// to help 'autolocate'
	int32_t   m_lastLoginIP;

	float  m_gpsLat;//int32_t   m_reserved7;
	float  m_gpsLon;//int32_t   m_reserved8;
	int32_t   m_reserved9;
	int32_t   m_reserved10;
	int32_t   m_reserved11;
	int32_t   m_reserved12;
	int32_t   m_reserved13;
	int32_t   m_reserved14;
	int32_t   m_reserved15;
	int32_t   m_reserved16;
	int32_t   m_reserved17;
	int32_t   m_reserved18;
	int32_t   m_reserved19;
	int32_t   m_reserved20;

	char m_verified;
	char m_is_blocked;
	char m_is_minor;
	char m_is_app_user;
	char m_timezone;
	int32_t m_likes_count;
	int32_t m_friend_count;

	char *ptr_accessToken;
	char *ptr_firstName;
	char *ptr_lastName;
	char *ptr_name;
	char *ptr_pic_square;
	char *ptr_religion;
	char *ptr_birthday;
	char *ptr_birthday_date;
	char *ptr_sex;
	char *ptr_hometown_location;
	char *ptr_current_location;
	char *ptr_activities;
	char *ptr_tv;
	char *ptr_email;
	char *ptr_interests;
	char *ptr_music;
	char *ptr_movies;
	char *ptr_books;
	char *ptr_about_me;
	char *ptr_status;
	char *ptr_online_presence;
	char *ptr_proxied_email;
	char *ptr_website;
	char *ptr_contact_email;
	char *ptr_work;
	char *ptr_education;
	char *ptr_sports;
	char *ptr_languages;
	// . facebook downloaded interests + cookies on eventguru
	// . 0 means unchecked
	// . 1 means checked
	// . 2 means nuked
	// . 3 means unchecked and from facebook
	// . 4 means checked and from facebook
	// . 5 means nuked and from facebook
	char *ptr_mergedInterests;//reserved1;
	char *ptr_myLocation;//reserved2;
	char *ptr_reserved3;
	char *ptr_reserved4;
	char *ptr_reserved5;
	char *ptr_reserved6;
	char *ptr_reserved7;
	char *ptr_reserved8;
	char *ptr_friendIds;

	int32_t size_accessToken;
	int32_t size_firstName;
	int32_t size_lastName;
	int32_t size_name;
	int32_t size_pic_square;
	int32_t size_religion;
	int32_t size_birthday;
	int32_t size_birthday_date;
	int32_t size_sex;
	int32_t size_hometown_location;
	int32_t size_current_location;
	int32_t size_activities;
	int32_t size_tv;
	int32_t size_email;
	int32_t size_interests;
	int32_t size_music;
	int32_t size_movies;
	int32_t size_books;
	int32_t size_about_me;
	int32_t size_status;
	int32_t size_online_presence;
	int32_t size_proxied_email;
	int32_t size_website;
	int32_t size_contact_email;
	int32_t size_work;
	int32_t size_education;
	int32_t size_sports;
	int32_t size_languages;
	int32_t size_mergedInterests;//reserved1;
	int32_t size_myLocation;//reserved2;
	int32_t size_reserved3;
	int32_t size_reserved4;
	int32_t size_reserved5;
	int32_t size_reserved6;
	int32_t size_reserved7;
	int32_t size_reserved8;
	int32_t size_friendIds;

	char  m_buf[0];
};

// here's the event guru app control panel:
// https://developers.facebook.com/apps/356806354331432/summary
// https://developers.facebook.com/apps

// facebook app info
#define APPID "356806354331432"
#define APPSECRET "ba5b51a5175951748cb43a5cea9b352e"
#define APPNAMESPACE "eventguru"

#define APPNAME "Event Guru"
#define APPDOMAIN "eventguru.com"
#define APPSUBDOMAIN "www.eventguru.com"

//#define TITAN

// for debugging on titan
#ifdef TITAN
#define APPHOSTUNENCODED "http://www2.eventguru.com:8000/"
#define APPHOSTUNENCODEDNOSLASH "http://www2.eventguru.com:8000"
#define APPHOSTENCODED "http%3A%2F%2Fwww2.eventguru.com%3A8000%2F"
#define APPHOSTENCODEDNOSLASH "http%3A%2F%2Fwww2.eventguru.com%3A8000"
#else
#define APPHOSTUNENCODED "http://www.eventguru.com/"
#define APPHOSTUNENCODEDNOSLASH "http://www.eventguru.com"
#define APPHOSTENCODED "http%3A%2F%2Fwww.eventguru.com%2F"
#define APPHOSTENCODEDNOSLASH "http%3A%2F%2Fwww.eventguru.com"
#endif

// facebook id for matt wells
#define FB_MATTWELLS 100003532411011LL


//#define APPNAME "Event Widget"
//#define APPDOMAIN "eventwidget.com"
//#define APPHOST "http://www2.eventwidget.com:8000/"

#define APPFBPAGE "http://www.facebook.com/pages/Event-Guru/385608851465019"

// . ask for interests now so we can email them something
// . might as well get offline access since we are paying for this stuff now
//   so we can mine their events... and need it for emailing...
// . need user_birthday so we don't send them kids events?
#define APPSCOPE1 "user_events,friends_events,email,user_interests,user_activities,user_location,offline_access,user_birthday"

// fix this so it is not hogging mem!
//#define MAXEVENTPTRS 1000

#define MAX_TOKEN_LEN 128

// are we downloading or waiting to download events from facebook for
// this person, fbId? used by PageEvents to display a warning msg to
// let the user know more events are pending so the search results might
// be incomplete.
bool isStillDownloading ( int64_t fbId , collnum_t collnum ) ;

class Msgfb {
public:

	Msgfb();
	~Msgfb();
	void reset();

	//
	// login pipeline (pipeline #1)
	//
	bool getFacebookUserInfo ( HttpRequest *hr ,
				   TcpSocket *s,
				   char *coll ,
				   void *state ,
				   char *redirPath ,
				   void (* callback) ( void *) ,
				   int32_t niceness );
	bool downloadAccessToken ( );
	bool gotFBAccessToken ( class TcpSocket *s );
	bool doneRechecking ( );
	bool downloadUserToUserRequestInfo();
	bool gotFBUserToUserRequest( class TcpSocket *s );
	bool downloadFBUserInfo();
	bool gotFBUserRec ( ) ;
	bool gotFQLUserInfo ( class TcpSocket *s );
	bool saveFBRec ( class FBRec * );


	//
	// event download & injection pipeline (pipeline #2)
	//
	bool processFBId ( int64_t fbId , 
			   collnum_t collnum ,
			   void *state, 
			   void (* callback)(void *));
	bool hitFacebook ( );
	bool gotFQLReply ( class TcpSocket *s );
	bool downloadEvents ( );
	bool injectFBEvents ( class TcpSocket *s );
	bool doInjectionLoop ( );
	bool addLikes ( );
	bool doFinalFBRecSave ( ) ;

	bool makeLikedbKeyList ( class Msg7 *msg7 , class RdbList *list );
	bool setFBRecFromFQLReply ( char *reply, 
				    int32_t replySize ,
				    class FBRec *fbrec );
	//bool overwriteFriendsFromFQLReply ( char *reply, int32_t replySize );
	//bool overwriteEventIdsFromFQLReply ( char *reply, int32_t replySize );
	bool setNewEventIdsFromFQLReply ( char *reply, int32_t replySize );
	bool parseFacebookEventReply ( char *reply, int32_t replySize );
	// the http reply from facebook that contains event titles/descrs/etc.
	bool  m_inProgress;
	int32_t  m_i;
	char *m_facebookReply;
	int32_t  m_facebookReplySize;
	int32_t  m_facebookAllocSize;
	// another one
	//char *m_facebookReply2;
	//int32_t  m_facebookReplySize2;
	//int32_t  m_facebookAllocSize2;
	// facebook reply buf is now rbuf
	SafeBuf m_rbuf;
	//char *m_facebookContent;
	//char  m_c;
	int32_t m_eventStartNum;
	int32_t m_eventStep;
	int32_t m_errorCount;
	int32_t m_retryCount;

	int32_t m_phase;
	SafeBuf m_fullReply;
	int32_t m_niceness;
	int32_t m_chunkStart;
	int32_t m_chunkSize;
	int32_t m_myChunkStart;
	int32_t m_myChunkSize;
	void queueLoop ( );

	char *m_redirPath;

	int64_t m_userToUserWidgetId;

	int32_t m_privacy;
	// fixed length. include +1 for \0
	char m_accessToken[MAX_TOKEN_LEN+1];

	// parse output. set eventPtrs to NULL if not new!
	//char     *m_eventPtrs[MAXEVENTPTRS];
	//int32_t      m_eventLens[MAXEVENTPTRS];
	//int64_t m_eventIds [MAXEVENTPTRS];
	SafeBuf   m_evPtrBuf;
	SafeBuf   m_evIdsBuf;
	int32_t      m_numEvents;

	// parse fql reply and store into here and reference from
	// m_fbrec::ptr_* etc.
	//SafeBuf m_sbuf;

	int64_t m_fbId;
	void (*m_afterSaveCallback) ( void * );
	HttpRequest m_hr;
	TcpSocket *m_socket;
	int32_t m_requests;
	int32_t m_replies;
	int32_t m_errno;
	RdbList m_list1;
	RdbList m_list2;
	RdbList m_list3;
	RdbList m_list4;
	RdbList m_list33;
	Msg5 m_msg5;
	Msg4 m_msg4;
	Msg0 m_msg0;
	Msg1 m_msg1;
	class Msg7 *m_msg7;
	void *m_state;
	void (* m_callback)(void *state);
	//char *m_coll;
	collnum_t m_collnum;

	FBRec  m_fbrecGen;
	FBRec *m_fbrecPtr;
	FBRec *m_oldFbrec;

	void *m_tmp;

	// store friend and event ids in here, reference via 
	// FBRec::ptr_friendIds and ptr_oldEventIds and ptr_newEventIds
	SafeBuf m_fidBuf;
	SafeBuf m_eidBuf; // fb event ids
	HashTableX m_dedupEidBuf;

	int64_t m_widgetId;

	HashTableX m_likedbTable;
};

//////////////////
//
// LIKEDB
//
//////////////////

#define LIKEDB_KEYSIZE sizeof(key192_t)
#define LIKEDB_DATASIZE 12
#define LIKEDB_RECSIZE  (LIKEDB_KEYSIZE+LIKEDB_DATASIZE)

class Likedb {
 public:
	void reset();
	bool init  ( );
	bool addColl ( char *coll, bool doVerify = true );
	Rdb *getRdb ( ) { return &m_rdb; };
	Rdb   m_rdb;

	char *makeRecs ( int64_t  uid         ,
			 int64_t  docId       ,
			 int32_t       eventId     ,
			 int32_t       rsvp_status ,
			 int32_t       start_time  ,
			 uint64_t eventHash64 ,
			 int64_t  value       );

	bool makeFriendTable ( class Msg39Request *req ,
			       int32_t likedbFlags , 
			       class HashTableX *ht ) ;

	int64_t getDocId ( key192_t *k ) { //return (k->n0)>>24; };
		// this is 1 if docid leads
		if ( k->n0 & LF_TYPEBIT ) return k->n2 >> 26;
		// otherwise it is in 2nd int64_t
		return k->n1 >> 26;
	};
	int64_t getDocIdFromRec ( char *rec );

	key192_t makeKey      ( int64_t docId, int32_t eventId );
	key192_t makeStartKey ( int64_t docId, int32_t eventId );
	key192_t makeEndKey   ( int64_t docId, int32_t eventId );

	key192_t makeStartKey2 ( int64_t uid ) ;

	int64_t getUserIdFromRec ( void *rec );
	int32_t getEventIdFromRec ( void *rec );
	void setEventId ( char *rec , int32_t eventId ) ;

	int32_t getRawFlagsFromRec  ( char *rec ) {
		return  *(int32_t *)rec ;};
	int32_t getFlagsFromRec  ( char *rec ) {
		return (*(int32_t *)rec) & ~(LF_DELBIT|LF_TYPEBIT);};
	int32_t getStartTimeFromRec ( char *rec ) {
		return (*(int32_t *)(rec+4)); };
	uint32_t getEventHash32FromRec(char *rec){
		return *(uint32_t *)(rec+sizeof(key192_t));};
	int64_t getValueFromRec ( char *rec ) {
		return *(int64_t *)(rec+sizeof(key192_t)+4);};

	// for the LF_ADDEDTOFACEBOOK flag
	int64_t getFacebookEventId ( char *rec ) {
		return *(int64_t *)(rec+sizeof(key192_t)+4);};

	// . OR all the flags this user has set in the likedb list
	// . used in PageEvents.cpp
	int32_t getUserFlags ( int64_t userId , int32_t start_time ,
			    char *list, int32_t listSize ) ;

	int32_t getPositiveFlagsFromRec  ( char *rec ) ;

	char *getRecFromLikedbList ( int64_t userId ,
				     int32_t start_time ,
				     int32_t flags , 
				     char *list ,
				     int32_t  listSize ) ;
};

/////////
//
// LIKEDB ACCESSOR class
//
/////////

class Msgfc {
 public:

	char m_recs [ LIKEDB_RECSIZE * 4 ];
	RdbList m_list6;
	Msg1 m_msg1;

	// add this to likedb (or remove if "neg" is true.
	bool addLikedbTag ( int64_t userId,
			    int64_t docId,
			    int32_t      eventId,
			    uint64_t eventHash64 ,
			    int32_t start_time,
			    int32_t likedbTag , // LB_* #define's above
			    bool negative , // turn off that flag?
			    char *coll,
			    void *state ,
			    void (* callback)(void *) );

};

///////////////////
//
// EMAILER class
//
///////////////////

class EmailState {
 public:

	// for use by PageEvents.cpp "sendemailfromfile" algo
	bool m_sendSingleEmail;
	void (* m_singleCallback) ( void *);
	void *m_singleState;
	TcpSocket *m_socket;

	int64_t m_fbId;
	SafeBuf m_emailResultsBuf;
	SafeBuf m_emailLikedbListBuf;
	char m_inUse;
	RdbList m_list9;
	RdbList m_list5;
	char m_emailSubdomain[100];
	Msg0 m_msg0;
	Msg1 m_msg1;
	MsgC m_msgc;
	class Emailer *m_emailer;
	int32_t m_ip;
	int32_t m_errno;
	SafeBuf m_hrsb;
	char *m_coll;
};

#define MAX_OUTSTANDING_EMAILS 20

class Emailer {
 public:

	bool emailEntryLoop ( ) ;
	bool emailScan ( class EmailState *es );
	bool launchEmail ( int64_t fbId );
	bool getMailServerIP ( EmailState *es );
	bool gotMXIp ( EmailState *es );
	bool gotEmailReply ( EmailState *es , TcpSocket *s );
	bool gotRec3 ( EmailState *es );
	bool savedUpdatedRec ( EmailState *es );
	bool doneAddingEmailedLikes ( EmailState *es ) ;
	bool populateEmailTree ( );
	bool scanLoop ( ) ;
	void gotScanList ( );

	bool sendSingleEmail ( class EmailState *es , int64_t fbId );

	bool m_populateInProgress;
	time_t m_lastScan;
	collnum_t m_collnum;
	char      *m_coll;
	bool       m_init;

	key96_t m_startKey;
	RdbTree m_emailTree;
	RdbList m_list7;
	Msg5 m_msg5;

	time_t m_lastEmailLoop;
	bool   m_emailInProgress;

	int32_t   m_emailRequests;
	int32_t   m_emailReplies;

	Msg0    m_msg0;
	RdbList m_list2;

	EmailState m_emailStates[MAX_OUTSTANDING_EMAILS];
	
	Emailer() { 
		m_lastScan = 0;
		m_populateInProgress = false;
		m_emailInProgress = false;
		m_emailRequests = 0;
		m_emailReplies  = 0;
		m_init = false;
		m_lastEmailLoop = 0;
		for ( int32_t i = 0 ; i < MAX_OUTSTANDING_EMAILS;i++ )
			m_emailStates[i].m_inUse = 0;
	};


};

extern class Facebookdb g_facebookdb;
extern class Likedb     g_likedb;
extern class Emailer    g_emailer;

extern HashTableX g_nameTable;
extern char *g_pbuf;

#endif

