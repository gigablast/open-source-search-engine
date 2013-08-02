
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
	long long getUserId ( void *fbrec ) {
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
	long    m_dataSize;

	// i've seen these up to 999 trillion
	long long m_fbId;
	// used for fetching
	long   m_flags;
	time_t m_accessTokenCreated;
	time_t m_eventsDownloaded;
	//long   m_reserved1;
	char   m_emailFrequency; // for just for you
	char   m_reserved1b;
	char   m_reserved1c;
	char   m_reserved1d;

	long   m_lastEmailAttempt;//reserved3; (for Recommendations)
	long   m_nextRetry; // when to retry email if it failed (Recommenda...)
	long   m_timeToEmail; // in minutes of the day (Recommendations)
	long   m_myRadius; // for "Recommendations/JustForYou"

	// . from what user was this user referred? 
	// . this is how we pay the widgetmasters.
	// . 0 means not referred via a widgetmaster's widget
	long long m_originatingWidgetId;
	// . the date they first logged into facebook (UTC)
	// . also used to determine payment
	time_t    m_firstFacebookLogin;
	// to help 'autolocate'
	long   m_lastLoginIP;

	float  m_gpsLat;//long   m_reserved7;
	float  m_gpsLon;//long   m_reserved8;
	long   m_reserved9;
	long   m_reserved10;
	long   m_reserved11;
	long   m_reserved12;
	long   m_reserved13;
	long   m_reserved14;
	long   m_reserved15;
	long   m_reserved16;
	long   m_reserved17;
	long   m_reserved18;
	long   m_reserved19;
	long   m_reserved20;

	char m_verified;
	char m_is_blocked;
	char m_is_minor;
	char m_is_app_user;
	char m_timezone;
	long m_likes_count;
	long m_friend_count;

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

	long size_accessToken;
	long size_firstName;
	long size_lastName;
	long size_name;
	long size_pic_square;
	long size_religion;
	long size_birthday;
	long size_birthday_date;
	long size_sex;
	long size_hometown_location;
	long size_current_location;
	long size_activities;
	long size_tv;
	long size_email;
	long size_interests;
	long size_music;
	long size_movies;
	long size_books;
	long size_about_me;
	long size_status;
	long size_online_presence;
	long size_proxied_email;
	long size_website;
	long size_contact_email;
	long size_work;
	long size_education;
	long size_sports;
	long size_languages;
	long size_mergedInterests;//reserved1;
	long size_myLocation;//reserved2;
	long size_reserved3;
	long size_reserved4;
	long size_reserved5;
	long size_reserved6;
	long size_reserved7;
	long size_reserved8;
	long size_friendIds;

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
#define MATTWELLS 100003532411011LL


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
bool isStillDownloading ( long long fbId , collnum_t collnum ) ;

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
				   long niceness );
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
	bool processFBId ( long long fbId , 
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
				    long replySize ,
				    class FBRec *fbrec );
	//bool overwriteFriendsFromFQLReply ( char *reply, long replySize );
	//bool overwriteEventIdsFromFQLReply ( char *reply, long replySize );
	bool setNewEventIdsFromFQLReply ( char *reply, long replySize );
	bool parseFacebookEventReply ( char *reply, long replySize );
	// the http reply from facebook that contains event titles/descrs/etc.
	bool  m_inProgress;
	long  m_i;
	char *m_facebookReply;
	long  m_facebookReplySize;
	long  m_facebookAllocSize;
	// another one
	//char *m_facebookReply2;
	//long  m_facebookReplySize2;
	//long  m_facebookAllocSize2;
	// facebook reply buf is now rbuf
	SafeBuf m_rbuf;
	//char *m_facebookContent;
	//char  m_c;
	long m_eventStartNum;
	long m_eventStep;
	long m_errorCount;
	long m_retryCount;

	long m_phase;
	SafeBuf m_fullReply;
	long m_niceness;
	long m_chunkStart;
	long m_chunkSize;
	long m_myChunkStart;
	long m_myChunkSize;
	void queueLoop ( );

	char *m_redirPath;

	long long m_userToUserWidgetId;

	long m_privacy;
	// fixed length. include +1 for \0
	char m_accessToken[MAX_TOKEN_LEN+1];

	// parse output. set eventPtrs to NULL if not new!
	//char     *m_eventPtrs[MAXEVENTPTRS];
	//long      m_eventLens[MAXEVENTPTRS];
	//long long m_eventIds [MAXEVENTPTRS];
	SafeBuf   m_evPtrBuf;
	SafeBuf   m_evIdsBuf;
	long      m_numEvents;

	// parse fql reply and store into here and reference from
	// m_fbrec::ptr_* etc.
	//SafeBuf m_sbuf;

	long long m_fbId;
	void (*m_afterSaveCallback) ( void * );
	HttpRequest m_hr;
	TcpSocket *m_socket;
	long m_requests;
	long m_replies;
	long m_errno;
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

	long long m_widgetId;

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

	char *makeRecs ( long long  uid         ,
			 long long  docId       ,
			 long       eventId     ,
			 long       rsvp_status ,
			 long       start_time  ,
			 unsigned long long eventHash64 ,
			 long long  value       );

	bool makeFriendTable ( class Msg39Request *req ,
			       long likedbFlags , 
			       class HashTableX *ht ) ;

	long long getDocId ( key192_t *k ) { //return (k->n0)>>24; };
		// this is 1 if docid leads
		if ( k->n0 & LF_TYPEBIT ) return k->n2 >> 26;
		// otherwise it is in 2nd long long
		return k->n1 >> 26;
	};
	long long getDocIdFromRec ( char *rec );

	key192_t makeKey      ( long long docId, long eventId );
	key192_t makeStartKey ( long long docId, long eventId );
	key192_t makeEndKey   ( long long docId, long eventId );

	key192_t makeStartKey2 ( long long uid ) ;

	long long getUserIdFromRec ( void *rec );
	long getEventIdFromRec ( void *rec );
	void setEventId ( char *rec , long eventId ) ;

	long getRawFlagsFromRec  ( char *rec ) {
		return  *(long *)rec ;};
	long getFlagsFromRec  ( char *rec ) {
		return (*(long *)rec) & ~(LF_DELBIT|LF_TYPEBIT);};
	long getStartTimeFromRec ( char *rec ) {
		return (*(long *)(rec+4)); };
	unsigned long getEventHash32FromRec(char *rec){
		return *(unsigned long *)(rec+sizeof(key192_t));};
	long long getValueFromRec ( char *rec ) {
		return *(long long *)(rec+sizeof(key192_t)+4);};

	// for the LF_ADDEDTOFACEBOOK flag
	long long getFacebookEventId ( char *rec ) {
		return *(long long *)(rec+sizeof(key192_t)+4);};

	// . OR all the flags this user has set in the likedb list
	// . used in PageEvents.cpp
	long getUserFlags ( long long userId , long start_time ,
			    char *list, long listSize ) ;

	long getPositiveFlagsFromRec  ( char *rec ) ;

	char *getRecFromLikedbList ( long long userId ,
				     long start_time ,
				     long flags , 
				     char *list ,
				     long  listSize ) ;
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
	bool addLikedbTag ( long long userId,
			    long long docId,
			    long      eventId,
			    unsigned long long eventHash64 ,
			    long start_time,
			    long likedbTag , // LB_* #define's above
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

	long long m_fbId;
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
	long m_ip;
	long m_errno;
	SafeBuf m_hrsb;
	char *m_coll;
};

#define MAX_OUTSTANDING_EMAILS 20

class Emailer {
 public:

	bool emailEntryLoop ( ) ;
	bool emailScan ( class EmailState *es );
	bool launchEmail ( long long fbId );
	bool getMailServerIP ( EmailState *es );
	bool gotMXIp ( EmailState *es );
	bool gotEmailReply ( EmailState *es , TcpSocket *s );
	bool gotRec3 ( EmailState *es );
	bool savedUpdatedRec ( EmailState *es );
	bool doneAddingEmailedLikes ( EmailState *es ) ;
	bool populateEmailTree ( );
	bool scanLoop ( ) ;
	void gotScanList ( );

	bool sendSingleEmail ( class EmailState *es , long long fbId );

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

	long   m_emailRequests;
	long   m_emailReplies;

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
		for ( long i = 0 ; i < MAX_OUTSTANDING_EMAILS;i++ )
			m_emailStates[i].m_inUse = 0;
	};


};

extern class Facebookdb g_facebookdb;
extern class Likedb     g_likedb;
extern class Emailer    g_emailer;

extern HashTableX g_nameTable;
extern char *g_pbuf;

#endif

