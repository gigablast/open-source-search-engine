#include "Facebook.h"
#include "HttpServer.h"
#include "sort.h"
#include "Repair.h"

// use this to get an access token for the event guru app
// https://graph.facebook.com/oauth/access_token?
// client_id=YOUR_APP_ID&client_secret=YOUR_APP_SECRET&
// grant_type=client_credentials

// use this to cache a facebook user's friends, and userid of themselves
Facebookdb g_facebookdb;
Likedb     g_likedb;

static void queueSleepWrapper ( int fd, void *state );

bool base64Decode ( char *dst, char *src, int32_t dstSize ) ;

///////////////////////////
//
// FACEBOOKDB
//
///////////////////////////

void Facebookdb::reset() {
	m_rdb.reset();
}

bool Facebookdb::init ( ) {

	if ( ! g_conf.m_indexEventsOnly )  return true;

	// load this here so calling saveQueryLoopState() does not overwrite it
	loadQueryLoopState ();
	// hit the queue
	if ( ! g_loop.registerSleepCallback(500,NULL,queueSleepWrapper))
		return false;
	/*
	char tmp[2048];
	char *sr = "eyJhbGdvcml0aG0iOiJITUFDLVNIQTI1NiIsImV4cGlyZXMiOjEzMzA0NTkyMDAsImlzc3VlZF9hdCI6MTMzMDQ1Mjk3Miwib2F1dGhfdG9rZW4iOiJBQUFGRWczUUE1eWdCQUg1b3dJTEt6WkNaQ0FDNmNTUVRaQWVaQmE2WkFiT1J2dkllSzc2MktGWFg2RmxaQ29iZWFzcENzWE5BZGh1R2VMQm1VM1hnNmMyTm1JenhxRlZkQ3d1Z0ZLRzVHWkN6WXlWaUpwZkN4UlYiLCJ1c2VyIjp7ImNvdW50cnkiOiJ1cyIsImxvY2FsZSI6ImVuX1VTIiwiYWdlIjp7Im1pbiI6MjF9fSwidXNlcl9pZCI6IjEwMDAwMzUzMjQxMTAxMSJ9";
	base64Decode ( tmp , sr , 2040 );
	log("facebook: %s",tmp);
	*/

	// . what's max # of tree nodes?
	// . assume avg facebookdb rec size of about 1000 bytes
	// . NOTE: 32 bytes of the 1000 are overhead
	int32_t maxMem = 5000000;
	int32_t maxTreeNodes = maxMem / 1000;//82;
	// each entry in the cache is usually just a single record, no lists,
	// unless a hostname has multiple sites in it. has 24 bytes more 
	// overhead in cache.
	//int32_t maxCacheNodes = g_conf.m_tagdbMaxCacheMem / 106;
	// we now use a page cache for the banned turks table which
	// gets hit all the time
	//if(! m_pc.init ("facebookdb",RDB_TAGDB,10000000,GB_TFNDB_PAGE_SIZE))
	//	return log("facebookdb: Tagdb init failed.");
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir               ,
			    "facebookdb"                     ,
			    true                       , // dedup same keys?
			    -1                         , // fixed record size
			    2,//g_conf.m_tagdbMinFilesToMerge   ,
			    maxMem, // 5MB g_conf.m_tagdbMaxTreeMem  ,
			    maxTreeNodes               ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			    NULL, // &m_pc                      ,
			    false,  // is titledb
			    false,  // preload disk page cache
			    sizeof(key96_t),     // key size
			    false , // bias disk page cache?
			    true )) // iscollectionless? syncdb,facebookdb,...
		return false;
	// add the base since it is a collectionless rdb
	return m_rdb.addColl ( NULL );
}

bool Facebookdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	return true;
}

///////////////////////
//
// MSGFB
//
///////////////////////

Msgfb::Msgfb ( ) {
	m_facebookReply = NULL;
	//m_facebookReply2 = NULL;
	m_msg7 = NULL;
	m_fbId = 0LL;
	m_inProgress = false;
	reset();
}

#include "Process.h"

void Msgfb::reset ( ) {
	// this can happen if we try to save and exit while in progress
	if ( g_process.m_mode != EXIT_MODE &&
	     m_inProgress ) { char *xx=NULL;*xx=0; }
	m_requests   = 0;
	m_replies    = 0;
	m_errno      = 0;
	m_errorCount = 0;
	m_fbId       = 0;
	m_state      = NULL;
	m_callback   = NULL;
	m_collnum    = 0;
	m_niceness   = MAX_NICENESS;
	m_socket     = NULL;
	m_retryCount = 0;
	m_widgetId = 0;
	m_userToUserWidgetId = 0LL;

	//m_fbrec.reset();
	if ( m_facebookReply ) {
		mfree ( m_facebookReply , m_facebookAllocSize ,"msgfb");
		m_facebookReply = NULL;
	}
	//if ( m_facebookReply2 ) {
	//	mfree ( m_facebookReply2 , m_facebookAllocSize2 ,"msgfb");
	//	m_facebookReply2 = NULL;
	//}
	m_rbuf.purge();

	if ( m_msg7 ) {
		mdelete ( m_msg7, sizeof(Msg7) , "FBInject" );
		delete ( m_msg7);
		m_msg7 = NULL;
	}

	m_list1.freeList();
	m_list2.freeList();
	m_list3.freeList();
	m_hr.reset();// = NULL;
	m_fbrecPtr = NULL;
	m_likedbTable.reset();
	m_evPtrBuf.purge();
	m_evIdsBuf.purge();
	m_fidBuf.purge();
	m_eidBuf.purge();
	m_dedupEidBuf.reset();
	// free mem
	m_fullReply.purge();
}

Msgfb::~Msgfb ( ) {
	reset();
}

///////////////////////
//
// MSGFB PIPELINE #1
//
///////////////////////

static void gotFBUserRecWrapper ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->gotFBUserRec ( ) ) return;
	mfb->m_callback ( mfb->m_state );
}	

static void gotFBAccessTokenWrapper ( void *state , TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->gotFBAccessToken( s ) ) return;
	mfb->m_callback ( mfb->m_state );
}

// format like strncpy()
bool base64Decode ( char *dst, char *src, int32_t dstSize ) {

	// make the map
	static unsigned char s_bmap[256];
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		memset ( s_bmap , 0 , 256 );
		unsigned char val = 0;
		for ( unsigned char c = 'A' ; c <= 'Z'; c++ ) 
			s_bmap[c] = val++;
		for ( unsigned char c = 'a' ; c <= 'z'; c++ ) 
			s_bmap[c] = val++;
		for ( unsigned char c = '0' ; c <= '9'; c++ ) 
			s_bmap[c] = val++;
		if ( val != 62 ) { char *xx=NULL;*xx=0; }
		s_bmap['+'] = 62;
		s_bmap['/'] = 63;
	}
		
	// leave room for \0
	char *dstEnd = dst + dstSize - 5;
	unsigned char *p    = (unsigned char *)src;
	unsigned char val;
	for ( ; ; ) {
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 6 bits
		*dst <<= 6;
		*dst |= val;
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 2 bits
		*dst <<= 2;
		*dst |= (val>>4);
		dst++;
		// copy 4 bits
		*dst = val & 0xf;
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 4 bits
		*dst <<= 4;
		*dst |= (val>>2);
		dst++;
		// copy 2 bits
		*dst = (val&0x3);
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 6 bits
		*dst <<= 6;
		*dst |= val;
		dst++;
		// sanity
		if ( dst >= dstEnd ) {
			log("facebook: bas64decode breach");
			//char *xx=NULL;*xx=0;
			*dst = '\0';
			return false;
		}
		if ( ! *p ) break;
	}
	// null term just in case
	dst[1] = '\0';
	return true;
}


bool Msgfb::getFacebookUserInfo ( HttpRequest *hr,
				  TcpSocket *s,
				  char *coll,
				  void *state ,
				  char *redirPath,
				  void (* callback)(void *state) ,
				  int32_t niceness ) {

	reset();
	m_state      = state;
	m_callback   = callback;
	//m_hr         = hr;
	m_oldFbrec   = NULL;
	m_redirPath  = redirPath;
	m_socket     = s;
	m_collnum = g_collectiondb.getCollnum ( coll );
	m_retryCount = 0;

	// they just logged out
	if ( hr->getLong("logout",0 ) ) return true;

	// need to make a copy of it so we can access it
	m_hr.copy ( hr );

	// save this
	m_widgetId = hr->getLongLongFromCookie("widgetid",0LL);

	// make the facebook key url for tagdb lookup
	char *fbidStr = m_hr.getStringFromCookie("fbid",NULL);
	// check for "usefbid"
	char *useFbid = m_hr.getString("usefbid",NULL);
	// that overrides
	if ( useFbid ) {
		int64_t used = strtoull(useFbid,NULL,10);
		int32_t h32a = hash32 ( (char *)&used, 8 );
		// this has to be a int64_t because fh is printed as
		// an uint32_t and atol() will croak
		int32_t h32b = m_hr.getLongLong("fh",0);
		if ( h32a != h32b ) {
			useFbid = NULL;
			log("facebook: bad usefbid=%"INT64" h32=%"UINT32" fh=%"UINT32"",
			    used,h32a,h32b);
		}
		else 
			fbidStr = useFbid;
	}

	// they've logged in before if we got this cookie, otherwise,
	// they got cookies off or they have not logged in ever yet, so
	// we gotta ask facebook for the access token.
	if ( ! fbidStr ) 
		return downloadAccessToken ( );

	// . make the facebook key url for facebook user rec
	// . we are here because we are assuming they are already logged in
	//   at some point in time and we should try to get their list
	//   of friends from the facebookdb rec so if they click 
	//   to show all the events their friends are going to we can do that
	int64_t fbId = strtoull ( fbidStr , NULL, 10 );

	// if they come into the canvas page we receive an encoded access
	// token and fbid from facebook. so get that and do the lookup.
	// if we do not have the username in the facebookdb rec or we do not
	// even have a facebook id rec, then we will have to use cmd1 fql
	// before they can be logged in.
	char *sr = m_hr.getString("signed_request",NULL);
	if ( sr ) sr = strchr(sr,'.');
	if ( sr ) sr++;
	// decode that
	char dsr[2048];
	if ( sr ) base64Decode ( dsr , sr , 2040 );
	// parse that json for fb user id
	char *jsonfbid = NULL;
	if ( sr       ) jsonfbid = strstr ( dsr , "user_id\":\"" );
	if ( jsonfbid ) jsonfbid += 10;
	if ( jsonfbid ) fbId = atoll(jsonfbid);

	// sometimes there's also an access token, if that is the case
	// we can just right to the cmd1 fql call downloadFBUserInfo().
	char *at = NULL;
	if ( dsr ) at = strstr ( dsr , "oauth_token\":\"" );
	if ( at  ) at += 14;
	if ( at && fbId ) {
		// copy into m_accessToken
		char *p = at;
		for ( ; *p && *p != '\"' && *p != ',' ; p++);
		*p = '\0';
		if ( p - at + 1 < MAX_TOKEN_LEN ) {
			strcpy ( m_accessToken , at );
			log("facebook: got access token from canvas app");
			if ( ! downloadUserToUserRequestInfo() ) return false;
			return downloadFBUserInfo();
		}
	}

	// for the facebookdb lookup to be legit this must be non-zero
	if ( fbId == 0LL )
		return downloadAccessToken ( );


	key96_t startKey;
	key96_t endKey;
	startKey.n1 = 0;
	startKey.n0 = fbId;
	endKey.n1 = 0;
	endKey.n0 = fbId;
	startKey.n0 <<= 1;
	endKey.n0 <<= 1;
	endKey.n0 |= 0x01;
	if ( ! m_msg0.getList ( -1, // hostid
				0 , // ip
				0 , // port
				0 , // maxcacheage
				false, // addtocache
				RDB_FACEBOOKDB,
				"",//coll,
				&m_list1,
				(char *)&startKey,
				(char *)&endKey,
				10, // minrecsizes
				this ,
				gotFBUserRecWrapper,
				niceness ) )
		return false;
	// i guess we got it without blocking
	return gotFBUserRec();
}

bool Msgfb::gotFBUserRec ( ) {
	// . it can be empty if never got saved!
	// . or if they logged out then logged back in, fbid will be 0
	if ( m_list1.getListSize() <= 0 ) return downloadAccessToken();
	// cast the list
	FBRec *fbrec = (FBRec *)m_list1.getList();
	// save that ptr
	m_fbrecPtr = fbrec;
	// get timestamp on that
	//int32_t now     = getTimeGlobal();
	//int32_t elapsed = now - fbrec->m_accessTokenCreated;
	// if stale, re-get it all from facebook.
	//if ( elapsed >= 60*60 ) return downloadAccessToken ( );
	// if still fresh, don't bother hitting facebook again, but we
	// need to deserialize
	deserializeMsg ( sizeof(FBRec) ,
			 &fbrec->size_accessToken ,
			 &fbrec->size_friendIds ,
			 &fbrec->ptr_accessToken ,
			 fbrec->m_buf );
	// need to set this for printBlackBar()
	m_fbId = fbrec->m_fbId;

	log("facebook: loaded fbrec for fbid=%"UINT64"",fbrec->m_fbId);
	log("facebook: loaded emailfreq=%"INT32"",(int32_t)fbrec->m_emailFrequency);
	log("facebook: loaded myradius=%"INT32"",(int32_t)fbrec->m_myRadius);


	// or if its an hour old i guess, get another token
	//int32_t expires = fbrec->m_accessTokenCreated + 3600;
	// debug 
	//expires = 0;
	//if ( getTimeGlobal() > expires ) {
	//	// use this to save again with new access token
	//	m_oldFbrec = fbrec;
	//	return downloadAccessToken();
	//}
	// use the same access token
	return true;
}

bool Msgfb::downloadAccessToken ( ) {
	//
	// ok, no cookie, so see if user just pushed the login button
	//
	int32_t  fbcodeLen = 0;
	char *fbcode = m_hr.getString("code", &fbcodeLen, NULL);
	// no they did not, no user info is available?
	if ( ! fbcode ) return true;

	// get current page url (minus the facebook code=)
	SafeBuf cup;
	m_hr.getCurrentUrl(cup);
	//m_hr->getCurrentUrlPath(cup);
	// but take out the &code=
	char *fix = strstr(cup.getBufStart(),"&code=");
	if ( fix ) *fix = 0;
	cup.urlEncode();

	// use code to get access token
	// that calls https://graph.facebook.com/oauth/access_token?
	// client_id=YOUR_APP_ID&redirect_uri=YOUR_URL&
	// client_secret=YOUR_APP_SECRET&code=THE_CODE_FROM_ABOVE
	char fbuf[1024];
	SafeBuf fburl ( fbuf,1024 );
	fburl.safePrintf ("https://graph.facebook.com/oauth/access_token?"
			  "client_id=%s&"
			  "redirect_uri=%s&"
			  "client_secret=%s&"
			  "code=%s"
			  , APPID
			  //, APPHOSTENCODEDNOSLASH
			  //, m_redirPath
			  // 
			  // KEEP It simple because this redirect_uri
			  // must EXACTLY match the one we used in
			  // PageEvents.cpp. and it can't have cgi crap in it
			  // because facebook strips them out...
			  //
			  //, "/" 
			  , cup.getBufStart()
			  , APPSECRET
			  , fbcode );

	// this fburl must match the previous url i think... i think the
	// "page" in it changes up and mess wit hit
	log("facebook: getting access token code=%s url=%s",fbcode,
	    fburl.getBufStart());

	// reset
	g_errno = 0;
	if ( ! g_httpServer.getDoc ( fburl.getBufStart() ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this                 , // state
				     gotFBAccessTokenWrapper , // callback
				     10*1000      , // 10 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     10000 , // maxTextDocLen   ,
				     10000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ) )
		// return false if blocked
		return false;
	// error?
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// let caller know we did not block
	return gotFBAccessToken ( NULL );
}

bool Msgfb::gotFBAccessToken ( TcpSocket *s ) {

	// some kind of error?
	if ( g_errno ) {
		log("facebook: error launching read of access token: %s", 
		    mstrerror(g_errno));
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// the access token should be in the reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	// mime error?
	HttpMime mime;
	// exclude the \0 i guess. use NULL for url.
	mime.set ( reply, replySize - 1, NULL );
	// not good?
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 ) {
		log("facebook: bad access request http status = %"INT32". "
		    "reply=%s",
		    httpStatus ,
		    reply );
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// point to content
	char *content = reply + mime.getMimeLen();

	// assume no accesstoken provided
	//int32_t expires = 0;
	m_accessToken[0] = '\0';

	// look for access token
	//sscanf(content,"access_token=%s&expires=%"INT32"",m_accessToken,&expires);
	char *at = strstr(content,"access_token=");
	if ( at ) {
		char *p = at + 13;
		char *start = p;
		for ( ; *p && *p != '&' ;p++ );
		int32_t len = p - start;
		if ( len > MAX_TOKEN_LEN ) { char *xx=NULL;*xx=0; }
		gbmemcpy ( m_accessToken , start , len );
		m_accessToken [ len ] = '\0';
	}

	// error?
	if ( ! m_accessToken[0] ) {
		log("facebook: could not find access token");
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// set this timestamp
	//m_accessTokenCreated = getTimeGlobal();

	// sanity
	if ( gbstrlen(m_accessToken) > MAX_TOKEN_LEN ) { char *xx=NULL;*xx=0;}

	if ( ! downloadUserToUserRequestInfo() ) return false;

	return downloadFBUserInfo();
}

static void gotFQLUserInfoWrapper ( void *state , TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	// . returns false if it blocks
	// . returns true with g_errno set on error
	if ( ! mfb->gotFQLUserInfo ( s ) ) return;
	// error?
	if ( g_errno && mfb->m_retryCount++ < 5 ) {
		// retry again. this returns false if blocks;
		if ( ! mfb->downloadFBUserInfo() ) return;
		// probably an error if it returns true!!
	}
	mfb->m_callback ( mfb->m_state );
}





////////////////
//
// BEGIN SPECIAL FACEBOOK APPREQUEST parsing for m_userToUserWidgetId
//
///////////////

static void gotFBUserToUserRequestWrapper ( void *state , TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->gotFBUserToUserRequest ( s ) ) return;
	mfb->m_callback ( mfb->m_state );
}

// before calling downloadFBUserInfo() call this to set the m_widgetId
// parameter correctly! so we can see which facebook user sent them
// this user_to_user apprequest. we need to do this before downloading
// the user info so we can store the m_originatingWidgetId into the
// FBRec before we save it!
bool Msgfb::downloadUserToUserRequestInfo ( ) {

	// hmmm. hr is invalid here.
	char *request_ids = m_hr.getString("request_ids",NULL);

	if ( ! request_ids ) return true;

	// if already got one, forget this...
	if ( m_widgetId ) return true;

	char fbuf[1024];
	SafeBuf fburl ( fbuf,1024 );
	fburl.safePrintf ("https://graph.facebook.com/me/apprequests/?"
			  "request_ids=%s&access_token=%s"
			  , request_ids
			  , m_accessToken );

	// this fburl must match the previous url i think... i think the
	// "page" in it changes up and mess wit hit
	log("facebook: getting referral fbid url=%s", fburl.getBufStart());

	// reset
	g_errno = 0;
	if ( ! g_httpServer.getDoc ( fburl.getBufStart() ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this                 , // state
				     gotFBUserToUserRequestWrapper ,
				     10*1000      , // 10 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     10000 , // maxTextDocLen   ,
				     10000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ) )
		// return false if blocked
		return false;
	// error?
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// let caller know we did not block
	return gotFBUserToUserRequest ( NULL );
}

bool Msgfb::gotFBUserToUserRequest ( TcpSocket *s ) {

	// some kind of error?
	if ( g_errno ) {
		log("facebook: error launching read of fb request: %s", 
		    mstrerror(g_errno));
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// the access token should be in the reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	// mime error?
	HttpMime mime;
	// exclude the \0 i guess. use NULL for url.
	mime.set ( reply, replySize - 1, NULL );
	// not good?
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 ) {
		log("facebook: bad fb request reply http status = %"INT32". "
		    "reply=%s",
		    httpStatus ,
		    reply );
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// point to content
	char *content = reply + mime.getMimeLen();

	// https://graph.facebook.com/me/apprequests/?request_ids=417422284951090&access_token=AAAFEg3QA5ygBALw0wqIMOZAD6zfbZBZBaH9mf7sw92kLtOqVtPjasQibZCo4P5R0HHztOnObBeoDoKbwM1ZChht04JJ7KrgkxcXwtNWcrngZDZD
	//{
	//	"data": [
	//	{
	//		"id": "417422284951090_100003381767946",
	//		"application": {
	//			"name": "Event Guru",
	//			"namespace": "eventguru",
	//			"canvas_name": "eventguru",
	//			"id": "356806354331432"
	//		},
	//		"to": {
	//			"name": "Jezebel Wells",
	//			"id": "100003381767946"
	//		},
	//		"from": {
	//			"name": "Matt Wells",
	//			"id": "100003532411011"
	//		},
	//		"data": "100003532411011",
	//		"message": "Hi my friend, I recommend EventGuru.com for discovering interesting local events. Plus, if you login to Event Guru I make a buck.",
	//		"created_time": "2012-03-29T02:41:37+0000"
	//	}
	//	],
	//	"paging": {
	//		"previous": "https://graph.facebook.com/me/apprequests?request_ids=417422284951090&access_token=AAAFEg3QA5ygBALw0wqIMOZAD6zfbZBZBaH9mf7sw92kLtOqVtPjasQibZCo4P5R0HHztOnObBeoDoKbwM1ZChht04JJ7KrgkxcXwtNWcrngZDZD&limit=50&since=1332988897&__paging_token=417422284951090_100003381767946&__previous=1",
	//		"next": "https://graph.facebook.com/me/apprequests?request_ids=417422284951090&access_token=AAAFEg3QA5ygBALw0wqIMOZAD6zfbZBZBaH9mf7sw92kLtOqVtPjasQibZCo4P5R0HHztOnObBeoDoKbwM1ZChht04JJ7KrgkxcXwtNWcrngZDZD&limit=50&until=1332988897&__paging_token=417422284951090_100003381767946"
	//	}
	//}

	// mine out who sent it
	char *from = strstr ( content , "\"from\":" );
	int64_t id1 = 0LL;
	if ( from ) {
		char *ids = strstr ( from , "\"id\":" );
		for ( ; ids && *ids && ! is_digit(*ids) ; ids++ );
		if ( ids && *ids ) id1 = atoll(ids);
	}


	// mine out who sent it again... this is only present for paid invites
	char *data = NULL;
	// start mining AFTER "from" because there is a top "data" field!!
	if ( from ) data = strstr ( from , "\"data\":" );
	int64_t id2 = 0LL;
	if ( data ) {
		char *ids = strstr ( data , "\"id\":" );
		for ( ; ids && *ids && ! is_digit(*ids) ; ids++ );
		if ( ids && *ids ) id2 = atoll(ids);
	}

	// must match to be an official paid invite. we only include
	// the "data" for the paid invites.
	if ( id1 == id2 )
		// set that to our widgetid then
		m_widgetId = id1;

	// download facebook info now
	return downloadFBUserInfo();
}

////////////////
//
// END SPECIAL FACEBOOK APPREQUEST parsing for user_to_user m_widgetId
//
///////////////



//int64_t mdw = 100003532411011LL; // my uid for Matt Wells
// 100003316058818 // uid for flurbit
// http://graph.facebook.com/502303355/picture shows pics for a uid
bool Msgfb::downloadFBUserInfo ( ) {

	log("facebook: downloading user info initial login");

	SafeBuf cmd1;
	// get your facebook id
	cmd1.safePrintf ( "SELECT uid,username,first_name,last_name,name,pic_square,profile_update_time,timezone,religion,birthday,birthday_date,sex,hometown_location,current_location,activities,interests,is_app_user,music,tv,movies,books,about_me,status,online_presence,proxied_email,verified,website,is_blocked,contact_email,email,is_minor,work,education,sports,languages,likes_count,friend_count FROM user where uid=me()");

	log("facebook: cmd1 = %s",cmd1.getBufStart());

	cmd1.urlEncode();

	// www.howtobe.pro/facebook-graph-api-graph-api-for-issuing-fql-queries
	// make a url
	SafeBuf ubuf;
	ubuf.safePrintf("https://api.facebook.com/method/"
			"fql.query?query=%s"
			"&access_token=%s&format=xml"
			, cmd1.getBufStart() 
			, m_accessToken 
			);

	log("facebook: getting url = %s",ubuf.getBufStart());

	// reset
	g_errno = 0;
	// get the results
	if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this               , // state
				     gotFQLUserInfoWrapper , // callback
				     40*1000      , // 20 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     30000000 , // maxTextDocLen   ,
				     30000000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ) )
		// return false if blocked
		return false;
	// otherwise, somehow got it without blocking... wtf?
	//return gotFQLReply();
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	log("fql: error getting doc: %s",mstrerror(g_errno));
	return true;
}

static bool queueFBId ( int64_t fbId , collnum_t collnum );

static void savedFBRecWrapper1 ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! g_errno ) queueFBId ( mfb->m_fbId , mfb->m_collnum );
	mfb->m_callback ( mfb->m_state );
}

static void doneRecheckingWrapper ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->doneRechecking ( ) ) return;
	mfb->m_callback ( mfb->m_state );
}

// returns true with g_errno set on error
bool Msgfb::gotFQLUserInfo ( TcpSocket *s ) {

	// bail on error
	if ( g_errno ) {
		log("fql: %s",mstrerror(g_errno));
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// get reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	// we reference into this, so do not free it!!
	m_facebookReply     = s->m_readBuf;
	m_facebookReplySize = s->m_readOffset;
	m_facebookAllocSize = s->m_readBufSize;
	// do not allow tcpsocket to free it. we free it in destructor.
	s->m_readBuf = NULL;


	// mime error?
	HttpMime mime;
	// exclude the \0 i guess. use NULL for url.
	mime.set ( reply, replySize - 1, NULL );
	// not good?
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 ) {
		log("facebook: bad fql request http status = %"INT32"",
		    httpStatus );
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// point to content
	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = reply + replySize - content;

	// check for error
	char *errMsg = strstr(content,"<error_msg>");
	if ( errMsg ) {
		log("facebook: error in fql reply: %s", content );
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// set it on stack i guess
	//FBRec fbrec;
	// all the FBRec::ptr_* things reference into "reply"
	// import friendids from existing rec if there, we just want to 
	// save the new access token!!!!
	if ( ! setFBRecFromFQLReply ( content,contentLen, &m_fbrecGen) ){
		log("fql: error setting fb rec from fql");
		g_errno = EBADREPLY;
		return true;
	}

	// must be there!
	if ( ! m_fbrecGen.m_fbId ) {
		log("fql: failed to get facebook id from reply");
		g_errno = EBADENGINEER;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// sanity
	log("facebook: got initial facebook reply. fbid=%"INT64"",m_fbId);

	// point to it!
	m_fbrecPtr = &m_fbrecGen;

	// now that we got the fbid see if its in facebookdb again
	int64_t fbId = m_fbrecGen.m_fbId;
	key96_t startKey;
	key96_t endKey;
	startKey.n1 = 0;
	startKey.n0 = fbId;
	endKey.n1 = 0;
	endKey.n0 = fbId;
	startKey.n0 <<= 1;
	endKey.n0 <<= 1;
	endKey.n0 |= 0x01;
	//char *coll = g_collectiondb.getColl(m_collnum);
	if ( ! m_msg0.getList ( -1, // hostid
				0 , // ip
				0 , // port
				0 , // maxcacheage
				false, // addtocache
				RDB_FACEBOOKDB,
				"",//coll,
				&m_list4,
				(char *)&startKey,
				(char *)&endKey,
				12, // minrecsizes
				this ,
				doneRecheckingWrapper,
				0 ) )//niceness ) )
		return false;
	// i guess we got it without blocking
	return doneRechecking();
}

//
// MERGE/UPDATE the old fbrec with the new fbrec
//
void mergeFBRec ( FBRec *dst , FBRec *src ) {
	// if we had a pre-existing fbrec in facebookdb, do not just 
	// save m_fbrecGen because it does not have ptr_friendIds set!
	// so inherit or import the friendIds from the pre-existing 
	// rec. otherwise we lose the friends until the cmd2 executes
	// and that could be a while or that could fail!
	if ( dst->size_friendIds <= 0 ) {
		dst->ptr_friendIds        = src-> ptr_friendIds;
		dst->size_friendIds       = src->size_friendIds;
	}
		// also this stuff too!
	dst->m_emailFrequency     = src->m_emailFrequency;
	dst->m_myRadius           = src->m_myRadius;
	dst->ptr_mergedInterests  = src->ptr_mergedInterests;
	dst->size_mergedInterests = src->size_mergedInterests;
	dst->ptr_myLocation       = src->ptr_myLocation;
	dst->size_myLocation      = src->size_myLocation;
	// save this stuff now too for Emailer
	dst->m_nextRetry        = src->m_nextRetry;
	dst->m_timeToEmail      = src->m_timeToEmail;
	dst->m_lastEmailAttempt = src->m_lastEmailAttempt;
	// other stuff
	dst->m_eventsDownloaded   = src->m_eventsDownloaded;
	dst->m_accessTokenCreated = src->m_accessTokenCreated;
	// new stuff
	dst->m_flags = src->m_flags; // FB_INQUEUE
	// for payment info:
	dst->m_originatingWidgetId = src->m_originatingWidgetId;
	// overwrite it if zero
	if ( src->m_firstFacebookLogin )
		dst->m_firstFacebookLogin = src->m_firstFacebookLogin;
	// if it is zero in the old rec, overwrite it!
	if ( src->m_lastLoginIP )
		dst->m_lastLoginIP       = src->m_lastLoginIP;
	//dst-> = src->;
}

// returns false if blocked, true otherwise
bool Msgfb::saveFBRec ( FBRec *fbrec ) {

	log("facebook: saving fbrec for fbid=%"INT64"",m_fbId);

	log("facebook: saving myradius=%"INT32"",fbrec->m_myRadius);
	log("facebook: saving mylocation=%s",fbrec->ptr_myLocation);
	log("facebook: saving emailfreq=%"INT32"",(int32_t)fbrec->m_emailFrequency);

	// . returns NULL and sets g_errno on error
	// . "true" means we should make mr.ptr_* reference into the newly
	//   serialized buffer
	int32_t replySize;
	char *reply = serializeMsg ( sizeof(FBRec) ,
				     &fbrec->size_accessToken,//1stsizeparm
				     &fbrec->size_friendIds,// lastsizeparm
				     &fbrec->ptr_accessToken , // firststrptr
				     fbrec , // thisptr
				     &replySize ,
				     NULL , 
				     0 ,
				     false ); // true );
	if ( ! reply ) {
		log("facebook: could not save fbrec: %s",mstrerror(g_errno));
		return true;
	}
		       
	
	// make the binary tag then for this facebook user
	//char *rec     = (char *)&m_key;//fbId;
	//int32_t  recSize = (char *)&m_ids[m_numIds] - rec;

	FBRec *serializedRec = (FBRec *)reply;

	// set this after we know it
	serializedRec->m_dataSize = replySize - sizeof(key96_t) - 4;


	//char *coll = g_collectiondb.getColl ( m_collnum );
	// use the list we got
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	// use m_list2 and not m_list since m_fbrecPtr is referencing
	// m_list's content from our facebookdb lookup above
	m_list2.set ( reply ,
		      replySize ,
		      NULL ,
		      0 ,
		      (char *)&startKey ,
		      (char *)&endKey  ,
		      -1 ,
		      true , // own data? yeah, free it when done
		      false ,
		      sizeof(key96_t) );
	// . just use TagRec::m_msg1 now
	// . no, can't use that because tags are added using SafeBuf::addTag()
	//   which first pushes the rdbid, so we gotta use msg4
	// . if a host is down we have to fix msg1 (and msg4) so they both
	//   just write to a file until that host comes back up.
	if ( ! m_msg1.addList ( &m_list2 ,
				RDB_FACEBOOKDB ,
				"none",//coll ,
				this ,
				m_afterSaveCallback, // savedFBRecWrapper ,
				false ,
				0 ) ) // niceness
		return false;
	// no block?
	//return downloadEvents();
	return true;
}

// returns false if blocked, true otherwise
bool Msgfb::doneRechecking ( ) {

	if ( g_errno ) {
		log("facebook: recheck lookup failed: %s",
		    mstrerror(g_errno));
		return true;
	}

	// try to set the old fbrec
	if ( m_list4.getListSize() > 0 ) {
		// cast the list
		m_oldFbrec = (FBRec *)m_list4.getList();
		deserializeMsg ( sizeof(FBRec) ,
				 &m_oldFbrec->size_accessToken ,
				 &m_oldFbrec->size_friendIds ,
				 &m_oldFbrec->ptr_accessToken ,
				 m_oldFbrec->m_buf );
		// sanity
		if ( m_fbId != m_oldFbrec->m_fbId ) 
			log("facebook: fbid mismatch 3 %"UINT64" != %"UINT64"",
			    m_fbId,m_oldFbrec->m_fbId);
		// int16_tcuts
		FBRec *src = m_oldFbrec;
		FBRec *dst = &m_fbrecGen;
		// merge src into dst. merge the old into the new.
		mergeFBRec ( dst , src );
	}
	//
	// if it did not exist, then this is the first time they logged
	// in i guess...
	//
	else {
		// . try to save this
		// . PageEvents.cpp issues a Set-Cookie: widgetid=xxxxxx;
		//   if si->m_widget is '1'. it uses si->m_widgetId which
		//   is set from the cgi parms of the iframe.
		// . use 1 if this is zero, they did not have one then...
		// . it's important we save the widgetid here correctly
		//   so we can payout the $1 reward.
		// . if it was a facebook user_to_user app request we
		//   should have called downloadUserToUserRequestInfo() to
		//   set m_widgetId if it was not already set.
		int64_t widgetId = m_widgetId;
		if ( ! widgetId ) widgetId = 1;
		m_fbrecGen.m_originatingWidgetId = widgetId;
		m_fbrecGen.m_lastLoginIP = m_socket->m_ip;
		// avoid a core
		int32_t now ;
		if ( isClockInSync() ) now = getTimeGlobal();
		else                   now = getTimeLocal();
		m_fbrecGen.m_firstFacebookLogin = now;
	}

	// saveFBRec should queue it after it saves it. we want to make sure
	// its only after a successful save because the facebookdb rec
	// needs to have been saved so we can use its access token to get
	// get event info of the user and his friends. but since that stuff
	// is more involved we have a queue that handles the requests.
	// that 2nd pipeline will set the FBRec::ptr_* things we did not set
	// here and save them back to facebookdb.
	m_afterSaveCallback = savedFBRecWrapper1;

	log("facebook: saving rec for fbid=%"INT64"",m_fbId);

	// . queue it up. it returns true if added to the queue.
	// . it won't add it if no room or its already in the queue.
	if ( ! g_errno && queueFBId ( m_fbrecGen.m_fbId , m_collnum ) )
		// . before saving it, mark it has being in the queue!! 
		// . then PageEvents.cpp can tell you that it is still 
		//   downloading you and your friends event info so its 
		//   incomplete if you try to search for that stuff
		// . once we process it we clear this flag and re-save this rec
		m_fbrecGen.m_flags |= FB_INQUEUE;

	// this calls serializeMsg() which mallocs a new reply to add
	if ( ! saveFBRec( &m_fbrecGen ) ) return false;
	// all done
	return true;
}

class LikedbTableSlot {
public:
	int64_t m_uid;
	int32_t m_start_time;
	int32_t m_rsvp;
};

bool Msgfb::setFBRecFromFQLReply ( char *content,
				   int32_t contentLen,
				   FBRec *fbrec ) {

	// int16_tcut
	char *reply = content;
	// i've seen crappy facebook return essentially an empty
	// reply here, so make sure we have all the fields!!
	if ( ! strstr(reply,"<uid>") ||
	     ! strstr(reply,"<name") ||
	     ! strstr(reply,"<first_name") ||
	     // might be <interests/>
	     ! strstr(reply,"<interests") ) {
		log("facebook: missing uid in facebook reply");
		SafeBuf eb;
		eb.safeMemcpy ( content,contentLen);
		char fname[128];
		sprintf(fname,"error.%"UINT64"",m_fbId);
		eb.save("/tmp/",fname);
		g_errno = EBADREPLY;
		return false;
	}
	
	Xml xml;
	xml.set ( content,
		  contentLen,
		  false , // owndata
		  0 , // allocsize
		  true ,  // purexml
		  TITLEREC_CURRENT_VERSION );

	// int16_tcut
	FBRec *f = fbrec;
	// clear the entire rec
	f->reset();

	f->m_fbId = xml.getLongLong("uid",0LL);

	// need this too i guess
	m_fbId = f->m_fbId;


	// scan for friend ids first since we add \0s below
	char *p = strstr ( content , "<uid2>" );
	for ( ; p ; p = strstr (p+1,"<uid2>") ) {
		int64_t uid2 = strtoull(p+6,NULL,10);
		if ( uid2 <= 0 ) continue;
		// add it
		if ( m_fidBuf.getAvail() < 8 && ! m_fidBuf.reserve(5000) ) 
			return false;
		// push it
		m_fidBuf.pushLongLong ( uid2 );
	}
	// point to those. might have to re-set if we grow m_fidBuf more!
	f->ptr_friendIds  = m_fidBuf.getBufStart();
	f->size_friendIds = m_fidBuf.length();



	f->ptr_firstName = xml.getString("first_name",&f->size_firstName);
	f->ptr_lastName = xml.getString("last_name",&f->size_lastName);
	// get the <name> tag right AFTER <last_name>, otherwise we
	// get the multi query name tag "query1"
	int32_t n0 = xml.getNodeNum ("last_name");
	if ( n0 >= 0 ) 
		f->ptr_name = xml.getString(n0,n0+5,"name",&f->size_name);
	f->ptr_pic_square = xml.getString("pic_square",&f->size_pic_square);
	f->ptr_religion = xml.getString("religion",&f->size_religion);
	f->ptr_birthday = xml.getString("birthday",&f->size_birthday);
	f->ptr_birthday_date = xml.getString("birthday_date",
					     &f->size_birthday_date);
	f->ptr_sex = xml.getString("sex",&f->size_sex);
	f->ptr_hometown_location =xml.getString("hometown_location",
						&f->size_hometown_location);
	f->ptr_current_location = xml.getString("current_location",
						&f->size_current_location);
	f->ptr_activities = xml.getString("activities",&f->size_activities);
	f->ptr_interests = xml.getString("interests",&f->size_interests);
	f->ptr_music = xml.getString("music",&f->size_music);
	f->ptr_tv = xml.getString("tv",&f->size_tv);
	f->ptr_movies = xml.getString("movies",&f->size_movies);
	f->ptr_books = xml.getString("books",&f->size_books);
	f->ptr_about_me = xml.getString("about_me",&f->size_about_me);
	f->ptr_status = xml.getString("status",&f->size_status);
	f->ptr_online_presence = xml.getString("online_presence",
					       &f->size_online_presence);
	f->ptr_proxied_email = xml.getString("proxied_email",
					     &f->size_proxied_email);
	f->ptr_website = xml.getString("website",&f->size_website);
	f->ptr_contact_email = xml.getString("contact_email",
					     &f->size_contact_email);
	f->ptr_email = xml.getString("email",&f->size_email);
	f->ptr_sports = xml.getString("sports",&f->size_sports);


	// full nodes (lists)
	f->ptr_work = xml.getNode("work",&f->size_work);
	f->ptr_education = xml.getNode("education",&f->size_education);
	f->ptr_languages = xml.getNode("languages",&f->size_languages);


	f->m_timezone = xml.getLong("timezone",99);
	f->m_is_app_user = xml.getLong("is_app_user",0);
	f->m_is_blocked = xml.getLong("is_blocked",0);
	f->m_verified = xml.getLong("verified",0);
	f->m_is_minor = xml.getLong("is_minor",0);
	f->m_likes_count = xml.getLong("likes_count",0);
	f->m_friend_count = xml.getLong("friend_count",0);

	//
	// now that we got what we want, NULL term everything
	//
	if ( f->ptr_firstName) f->ptr_firstName[f->size_firstName++] = '\0';
	if ( f->ptr_lastName) f->ptr_lastName[f->size_lastName++] = '\0';
	if ( f->ptr_name) f->ptr_name[f->size_name++] = '\0';
	if ( f->ptr_pic_square)f->ptr_pic_square[f->size_pic_square++] = '\0';
	if ( f->ptr_religion) f->ptr_religion[f->size_religion++] = '\0';
	if ( f->ptr_birthday)f->ptr_birthday[f->size_birthday++] = '\0';
	if ( f->ptr_birthday_date) 
		f->ptr_birthday_date[f->size_birthday_date++] = '\0';
	if ( f->ptr_sex) f->ptr_sex[f->size_sex++] = '\0';
	if ( f->ptr_hometown_location)
		f->ptr_hometown_location[f->size_hometown_location++] = '\0';
	if ( f->ptr_current_location)
		f->ptr_current_location[f->size_current_location++] = '\0';
	if ( f->ptr_activities) f->ptr_activities[f->size_activities++] = '\0';
	if ( f->ptr_interests)f->ptr_interests[f->size_interests++] = '\0';
	if ( f->ptr_music)f->ptr_music[f->size_music++] = '\0';
	if ( f->ptr_tv)f->ptr_tv[f->size_tv++] = '\0';
	if ( f->ptr_movies)f->ptr_movies[f->size_movies++] = '\0';
	if ( f->ptr_books)f->ptr_books[f->size_books++] = '\0';
	if ( f->ptr_about_me)f->ptr_about_me[f->size_about_me++] = '\0';
	if ( f->ptr_status)f->ptr_status[f->size_status++] = '\0';
	if ( f->ptr_online_presence)
		f->ptr_online_presence[f->size_online_presence++] = '\0';
	if ( f->ptr_proxied_email)
		f->ptr_proxied_email[f->size_proxied_email++] = '\0';
	if ( f->ptr_website)
		f->ptr_website[f->size_website++] = '\0';
	if ( f->ptr_contact_email)
		f->ptr_contact_email[f->size_contact_email++] = '\0';
	if ( f->ptr_email) f->ptr_email[f->size_email++] = '\0';
	if ( f->ptr_sports)f->ptr_sports[f->size_sports++] = '\0';
	if ( f->ptr_work) f->ptr_work[f->size_work++] = '\0';
	if ( f->ptr_education)f->ptr_education[f->size_education++] = '\0';
	if ( f->ptr_languages)f->ptr_languages[f->size_languages++] = '\0';

	// allow dups! one eid can have many likes
	m_likedbTable.set(8,sizeof(LikedbTableSlot),0,NULL,0,true,0,"lkdbtab");

	//
	// . add the eids facebook provided into m_eidBuf
	// . create a likedb record for each one to add to likedb
	//
	bool hadError = false;
	char *line = NULL;
	// scan the xmlnodes, looking for event_member
	int32_t i; for ( i = 0 ; i < xml.m_numNodes ; i++ ) {

		// get an <event_member> tag
		XmlNode *node = &xml.m_nodes[i];
		char *s = node->m_node;
		if ( strncmp(s,"<event_member",13) ) continue;

		line = s;

		hadError = true;

		// skip til end of tag
		s += 13;
		for ( ; *s && *s != '>' ; s++ ) ;
		// eof?
		if ( ! *s ) break;
		// skip >
		s++;

		// next tag
		for ( ; *s && *s != '<' ; s++ ) ;
		// must be <uid> afterwards
		if ( strncmp ( s, "<uid>" , 5 ) ) break;
		// skip that
		s += 5;
		// int64_t
		int64_t uid = strtoull ( s , NULL, 10 );
		// skip til next tag
		for ( ; *s && *s != '<' ; s++ ) ;		
		// must be /uid
		if ( strncmp ( s, "</uid>", 6 ) ) break;
		// skip that
		s += 6;

		// next tag
		for ( ; *s && *s != '<' ; s++ ) ;
		// must be <eid>
		if ( strncmp ( s, "<eid>" , 5 ) ) break;
		// skip that
		s += 5;
		// int64_t
		int64_t eid = strtoull ( s , NULL, 10 );
		// skip til next tag
		for ( ; *s && *s != '<' ; s++ ) ;		
		// must be /uid
		if ( strncmp ( s, "</eid>", 6 ) ) break;
		// skip that
		s += 6;

		// next tag
		for ( ; *s && *s != '<' ; s++ ) ;
		// must be <eid>
		if ( strncmp ( s, "<rsvp_status>" , 13 ) ) break;
		// skip that
		s += 13;
		// skip whitespace
		for ( ; *s && is_wspace_a(*s) ; s++ );
		// int64_t
		char *rsvp_status = s;
		// skip til next tag
		for ( ; *s && *s != '<' ; s++ ) ;
		// must be /uid
		if ( strncmp ( s, "</rsvp_status>", 14 ) ) break;
		// null term so "status" is null terminated
		//*s = '\0';
		// skip that
		s += 14;

		
		// next tag
		for ( ; *s && *s != '<' ; s++ ) ;
		// must be <eid>
		if ( strncmp ( s, "<start_time>" , 12 ) ) break;
		// skip that
		s += 12;
		// int64_t
		int32_t start_time = (uint32_t)(atoll ( s ));
		// skip til next tag
		for ( ; *s && *s != '<' ; s++ ) ;		
		// must be /uid
		if ( strncmp ( s, "</start_time>", 13 ) ) break;
		// skip that
		s += 13;

		// next tag
		hadError = false;

		if ( g_conf.m_logDebugFacebook ) {
			char c = rsvp_status[6];
			rsvp_status[6] = '\0';
			log("facebook: got event "
			    "eid=%"UINT64" "
			    "uid=%"UINT64" "
			    "rsvp_status=%s "
			    "start_time=%"UINT32""
			    , eid
			    , uid
			    , rsvp_status
			    , start_time 
			    );
			rsvp_status[6] = c;
		}
		
		// ensure enough room
		if ( m_eidBuf.getAvail() < 8 && ! m_eidBuf.reserve(5000) ) 
			return false;

		// dedup eidbuf
		if ( ! m_dedupEidBuf.isInTable ( &eid ) ) {
			// push it in
			m_eidBuf.pushLongLong (eid );
			// add it now
			m_dedupEidBuf.addKey ( &eid );
		}

		// TODO: what about uid/rsvp_status/start_time ???
		// we need that info for adding to likedb so make a likedb
		// key here and we'll add that if the inject goes through
		//int32_t recSize;
		//char *rec = g_likedb.makeRec ( uid         ,
		//			       docId       ,
		//			       gbeventId   ,
		//			       rsvp_status ,
		//			       start_time  ,
		//			       &recSize    );
		// a problem?
		//if ( ! rec ) 
		//	continue;

		// convert rsvpstatus to a value
		int32_t rsvp = 0;
		if      ( ! strncmp(rsvp_status,"not_replied",11) ) 
			rsvp = LF_INVITED;
		else if ( ! strncmp(rsvp_status,"attending",9  ) ) 
			rsvp = LF_GOING;
		else if ( ! strncmp(rsvp_status,"declined",8   ) ) 
			rsvp = LF_DECLINED;
		else if ( ! strncmp(rsvp_status,"unsure" ,6    ) ) 
			rsvp = LF_MAYBE_FB;
		else 
			log("facebook: unknown rsvp_status=%s",rsvp_status);

		// the data
		LikedbTableSlot lts;
		lts.m_uid         = uid;
		lts.m_start_time  = start_time;
		lts.m_rsvp        = rsvp;

		// store that in table so if we end up injecting this
		// eid successfully, we'll scan the table and add all these
		// likedb recs for it. return false with g_errno set on error.
		if ( ! m_likedbTable.addKey ( &eid , &lts ) )
			return false;
	}

	// if we broke out when we should not have...
	if ( hadError ) {
		log("facebook: had error processing event_members reply %s",
		    line);
		g_errno = EBADREPLY;
		return false;
	}


	// point to the access token from the old facebookdb rec so the
	// new one keeps it going
	fbrec->ptr_accessToken = m_accessToken;
	fbrec->size_accessToken = gbstrlen(m_accessToken)+1;
	fbrec->m_accessTokenCreated = getTimeGlobal();

	// set key, just the fbid
	fbrec->m_key.n1 = 0;
	fbrec->m_key.n0 = fbrec->m_fbId;
	fbrec->m_key.n0 <<= 1;
	fbrec->m_key.n0 |= 0x01;

	return true;
}

/////////////////////////////////////
//
// MSGFB PIPELINE #2
//
/////////////////////////////////////

// high priority queue for ppl that login
int64_t g_fbq1   [100];
collnum_t g_colls1 [100];
int32_t      g_n1 = 0;
// low priority queue for passive facebookdb scanning
//int64_t g_fbq2   [100];
//collnum_t g_colls2 [100];
//int32_t      g_n2 = 0;
// used for queue
Msgfb g_msgfb;

bool isInQueue ( int64_t fbId , collnum_t collnum ) {
	for ( int32_t i = 0 ; i < g_n1 ; i++ ) 
		if ( g_fbq1 [ i ] == fbId ) return true;
	return false;
}

bool queueFBId ( int64_t fbId , collnum_t collnum ) {
	// skip matt wells for now
	//if ( fbId == 100003532411011LL ) {
	//	log("facebook: skipping matt wells in queue");
	//	return true;
	//}
	if ( g_n1 >= 100 )
		return log("facebook: could not add fbid=%"INT64" to queue",fbId);

	// make sure not already in
	if ( isInQueue ( fbId , collnum ) ) return false;
	log("facebook: queueing fbid=%"INT64"",fbId);
	g_fbq1   [ g_n1 ] = fbId;
	g_colls1 [ g_n1 ] = collnum;
	g_n1++;
	return true;
}

/*
static void doneProcessingWrapper ( void *state ) {
	// int16_tcut
	int32_t err = g_msgfb.m_errno;
	// or inherit this. we might have forgotten to set m_errno
	if ( ! err && g_errno ) err = g_errno;
	// no longer in progress
	g_msgfb.m_inProgress = false;
	g_msgfb.reset();
	// note it
	log("facebook: done with queue for fbid=%"UINT64". error=%s",
	    g_fbq1[0],mstrerror(err));
	// save it for potential re-add
	//int64_t fsaved = g_fbq1  [0];
	//collnum_t csaved = g_colls1[0];
	// shift queue down
	for ( int32_t i = 1 ; i < g_n1 ; i++ ) {
		g_fbq1  [i-1] = g_fbq1  [i];
		g_colls1[i-1] = g_colls1[i];
	}
	// one less in queue
	g_n1--;
	// . on error, re-add to the end of the queue
	// . leave this out for now. we should have some kinda download
	//   loop perhaps to download it. at least leave this out until we
	//   have a backoff scheme in place.
	//if ( err ) queueFBId ( fsaved , csaved );
}
*/

#define NUM_MSGFBS 3
Msgfb g_msgfbs[NUM_MSGFBS];
int32_t  g_numOut = 0;

// evaluate events associated with the fbuserids in the queue
void queueSleepWrapper ( int fd, void *state ) {
	// skip for now
	//return;
	// return if empty
	if ( g_n1 == 0 ) return;
	// sanity
	if ( g_n1 >= 100 ) { char *xx=NULL;*xx=0; }
	// wait for clock to be in sync
	if ( ! isClockInSync() ) 
		return;
	// wait until done repairing... so we do not inject events!
	if ( g_repair.isRepairActive() ) 
		return;
	// get an fbid
	if ( g_numOut >= (int32_t)NUM_MSGFBS ) return;
	// return if all out and no more to put out
	if ( g_n1 <= g_numOut ) return;
	// get the next fbid
	int64_t fbId = g_fbq1[g_numOut];
	// get one not in use
	Msgfb *mfb = NULL;
	for ( int32_t i = 0 ; i < NUM_MSGFBS ; i++ ) {
		if ( g_msgfbs[i].m_inProgress ) continue;
		mfb = &g_msgfbs[i];
		break;
	}
	// return if all in progress. how can this be?
	if ( ! mfb ) return;
	// get the fbid
	//int64_t fbId    = g_fbq1[0];
	//collnum_t collnum = g_colls1[0];
	// inc this now!
	g_numOut++;
	// set it up
	mfb->m_fbId = fbId;
	mfb->m_phase = 0;
	// and launch it. will not re-launch since it sets m_inProgress = true
	mfb->queueLoop();
	// set it up
	//if( ! g_msgfb.processFBId (fbid,collnum,NULL,doneProcessingWrapper) )
	//	return;
	// error?
	//log("fbqueue: error of some sort = %s",mstrerror(g_errno));
	// wtf?
	//g_msgfb.m_inProgress = false;
}

////////////
//
// the queue loop
//
////////////

static bool s_init = false;
static int32_t s_flip = 0;
static SafeBuf s_tbuf1;
static SafeBuf s_tbuf2;
static int32_t s_ptr1 = 0; // facebook dictionary query cursor
static int32_t s_ptr2 = 0; // facebook location query cursor
static int32_t s_ptr3 = 0; // stubhub cursor
static int64_t s_ptr4 = 0; // eventbrite cursor
static int64_t s_ptr5 = 0; // local facebookdb scanner cursor
static int32_t      s_holdOffStubHubTill = 0;
static int64_t s_lastEventBriteEventId = 0;
static int32_t      s_eventBriteWaitUntil = 0;
static int32_t      s_localWaitUntil = 0;

static char *getNextQuery ();

static void queueLoopWrapper ( void *state ) {
	Msgfb *msgfb = (Msgfb *)state;
	msgfb->queueLoop();
}

static void queueLoopWrapper2 ( void *state , TcpSocket *s ) {
	Msgfb *msgfb = (Msgfb *)state;
	msgfb->m_socket = s;
	msgfb->queueLoop();
}

static void queueLoopWrapper5 ( void *state , RdbList *list, Msg5 *msg5 ) {
	Msgfb *msgfb = (Msgfb *)state;
	msgfb->queueLoop();
}

void Msgfb::queueLoop ( ) {

	// skip for now
	//return;

	// assume in use
	//m_inProgress = true;

	if ( m_phase == 0 ) {
		// do not reset this!
		int64_t saved = m_fbId;
		// make sure this is empty and ready to go
		reset();
		// re-establish this
		m_fbId = saved;
		// set this
		m_inProgress = true;
	}

	// if an fbid of -3 means to get scan facebookdb locally
	if ( m_phase == 0 && m_fbId == -3LL ) {
		// use msg5 then i guess
		key96_t startKey;
		key96_t endKey;
		startKey.n1 = 0;
		startKey.n0 = s_ptr5;
		endKey.n1 = 0;
		endKey.n0 = 0xffffffffffffffffLL;
		startKey.n0 <<= 1;
		endKey.n0 <<= 1;
		endKey.n0 |= 0x01;
		// advance phase to a special phase
		m_phase = 50;
		if ( ! m_msg5.getList ( RDB_FACEBOOKDB,
					"", // coll
					&m_list33,
					&startKey,
					&endKey,
					13, // minrecsizes
					true , // includetree?
					false, // addtocache?
					0, // maxcacheage
					0, // startfilenum
					-1, // numfiles
					this ,
					queueLoopWrapper5,
					m_niceness ,
					true // doErrorCorrection?
					) )
			return;
	}

	// handle special phase from msg5 call above
	if ( m_phase == 50 ) {
		// grab the rec
		if ( g_errno ) {
			log("fbqueue: error getting local facebookdb rec: %s", 
			    mstrerror(g_errno));
			goto error;
		}
		// empty is means done
		if ( m_list33.getListSize() <= 0 ) {
			// set this
			int32_t now = getTimeGlobal();
			// wait for a day then
			s_localWaitUntil = now + 86400;
			// reset the scan for the scan tomorrow
			s_ptr5 = 0;
			// all done
			m_phase = 16;
			// remove from queue
			goto skipdown;
		}
		// get the facebook rec... why?
		m_oldFbrec = (FBRec *)m_list33.getList();
		// get is fbid and queue that
		if ( m_oldFbrec->m_fbId < 0 ) {
			log("fbqueue: bad fbid in local scan: %"INT64"",
			    m_oldFbrec->m_fbId);
			g_errno = EBADREPLY;
			goto error;
		}
		// ok, queue that fbid now
		queueFBId ( m_oldFbrec->m_fbId , m_collnum );
		// update our scan cursor
		s_ptr5 = m_oldFbrec->m_fbId + 1;
		// and remove ourselves from the queue
		m_phase = 16;
		goto skipdown;
	}


	// if an fbid of -2 means to get some eventbrite
	if ( m_fbId == -2LL && m_phase == 0 ) {
		// . loop over all events sorted by id
		// . use &since_id=<s_ptr4> to avoid duppage
		// . we assume they only increment that id, if that is
		//   not the case we'll have to restart from 0 each
		//   time, and maybe two a roll twice per day
		// . they say they limit to 1000 requests per day!
		SafeBuf cmd10;
		cmd10.safePrintf(
				 "http://www.eventbrite.com/xml/event_search?"
				 "app_key=QSMWWLSZUJT3D5ZGPF&"
				 "page=1&"
				 // ask for 100 results
				 "max=100&"
				 "sort_by=id&"
				 "since_id=%"INT64""
				 // no need to add 1 to s_ptr4, just make
				 // sure that it exactly equals the LAST
				 // id we injected because it is EXCLUDED
				 // from these results and eventbrite starts
				 // with s_ptr4+1
				 ,s_ptr4 
				);
		log("fbspider: getting eventbrite results: cmd10 = %s",
		    cmd10.getBufStart() );
		// advance to phase to parse the events out of the reply
		m_phase = 11;
		// this will be set in queueLoopWrapper2()
		m_socket = NULL;
		// use defaults i guess
		char *ua = g_conf.m_spiderUserAgent;
		char *proto = "HTTP/1.0";
		// pillage from eventbrite
		if ( ! g_httpServer.getDoc ( cmd10.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2,
					     80*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     100000000 , // maxTextDocLen   ,
					     100000000 , // maxOtherDocLen  ,
					     ua ,
					     proto ))
			// return false if blocked
			return;
		// this is not possible, must be error!
		log("fbspider: got error downloading eventbrite results = %s",
		    mstrerror(g_errno));
		goto error;
	}

	// if an fbid of -1 means to close our eyes and suck down some stubhub
	if ( m_fbId == -1LL && m_phase == 0 ) {

		int32_t now = getTimeGlobal();
		// now subtract 12 so that gmtime() call below will
		// return current time in hawaii or right before that...
		now -= 12*3600;

		SafeBuf cmd9;
		cmd9.safePrintf("http://www.stubhub.com/"
				"listingCatalog/select/?q="
				"event_date%%3A["
				);

		if ( s_ptr3 == 0 ) s_ptr3 = now;

		// . convert into canonical time format
		// . this should be basically close to the time in hawaii
		//   which lags pretty much everyone else, that way
		//   we will get events about to happen right now in hawaii
		struct tm *tt = gmtime(&s_ptr3);

		// . now print it out for the stubhub query
		// . it uses the SOLR format that lucene uses for queries
		//   like "event_data:[X TO Y]" for doing ranges
		// http://www.stubhub.com/listingCatalog/select/?q=event_date%3A[2012-05-04T00:00:00.001Z%20TO%202012-05-04T13:59:00.001Z]
		// &start=0&rows=10
		char tbuf[512];
		strftime ( tbuf , 512, "%Y-%m-%dT%H:00:00Z" , tt );
		cmd9.urlEncode ( tbuf );
		cmd9.safePrintf("+TO+");

		// add the half day here
		int32_t delta = 12*3600;

		// 3+ months out use a full day
		if ( s_ptr3 - now > 90*86400 ) delta = 24*3600;

		// subtract 1 to avoid dups for next call in case stubhub
		// uses a closed interval, and not half open
		s_ptr3 += delta - 1;

		// get the endpoint then
		tt = gmtime(&s_ptr3);
		strftime ( tbuf , 512, "%Y-%m-%dT%H:00:00Z" , tt );
		cmd9.urlEncode ( tbuf );
		cmd9.safePrintf("]");
		cmd9.safePrintf("&fl=event_id,"
				"act_primary,event_date_time_local,"
				"keywords,lat_lon,"
				"urlpath,venueDetailUrlPath,description,"
				"ancestorGenreDescriptions"
				);


		// add the second back
		s_ptr3++;

		log("fbspider: getting stubhub results: cmd9 = %s",
		    cmd9.getBufStart() );

		// advance to phase to parse the events out of the reply
		m_phase = 11;

		// this will be set in queueLoopWrapper2()
		m_socket = NULL;

		char *ua = g_conf.m_spiderUserAgent;
		char *proto = "HTTP/1.0";
		//if ( m_fbId < 0 ) {
		//	ua = "Mozilla/5.0 (X11; U; Linux i686; "
		//		"en-US; rv:1.9.2.7) Gecko/20100715 "
		//			"Ubuntu/10.04 (lucid) Firefox/3.6.7";
		//	proto = "HTTP/1.1";
		//}

		// pillage from stubhub
		if ( ! g_httpServer.getDoc ( cmd9.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2,
					     80*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     100000000 , // maxTextDocLen   ,
					     100000000 , // maxOtherDocLen  ,
					     ua ,
					     proto ))
			// return false if blocked
			return;
		// this is not possible, must be error!
		log("fbspider: got error downloading stubhub results = %s",
		    mstrerror(g_errno));
		goto error;
	}


	// if an fbid of 0 is used that means to do a query against
	// facebook's massively lame event search engine
	if ( m_fbId == 0LL && m_phase == 0 ) {
		// . ok, pick a query to send to facebook
		// . pick the next most popular word, 
		//   or the next most popular location name
		// . interleave between words and location names
		char *fbq = getNextQuery ( );
		// don't core!
		if ( ! fbq ) {
			log("fbspider: getNextQuery() returned null. "
			    "possibly a state without an official name "
			    "and getStateName() returned NULL?");
			g_errno = EBADENGINEER;
			goto error;
		}
		// . make the query
		// . &format=xml is pointless and will not work!
		//   they made it 100% json now
		SafeBuf cmd8;
		cmd8.safePrintf("https://graph.facebook.com/"
				"search?"
				"fields=id,privacy,picture,name,location,"
				"venue,description,"
				"start_time,end_time&"
				"q="
				);
		// encode the query
		cmd8.urlEncode ( fbq );
		// and put in the rest
		cmd8.safePrintf("&"
				"type=event&"
				//"access_token=AAAAAAITEghMBADXfRdrOzCBZAV7"
				//"2K1kQurpnnyPDBIYHCSoPpRLOvdJFBZBESUZA0BgZ"
				//"AWr8dGCpNCOZBMh58opiYAnAZAavKPGaMnoKq32v9"
				//"CiYxy2WtD&"
				"limit=600&"
				"offset=0"
				);

		log("fbspider: getting fb results: cmd8 = %s",
		    cmd8.getBufStart() );

		// advance to phase to parse the events out of the reply
		m_phase = 11;

		// this will be set in queueLoopWrapper2()
		m_socket = NULL;

		// pillage from facebook!
		if ( ! g_httpServer.getDoc ( cmd8.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2,
					     40*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     100000000 , // maxTextDocLen   ,
					     100000000 , // maxOtherDocLen  ,
					     g_conf.m_spiderUserAgent    ) )
			// return false if blocked
			return;
		// this is not possible, must be error!
		log("fbspider: got error downloading fb results = %s",
		    mstrerror(g_errno));
		goto error;
	}


	// first phase is loading the facebook rec
	if ( m_phase == 0 ) {
		key96_t startKey;
		key96_t endKey;
		startKey.n1 = 0;
		startKey.n0 = m_fbId;
		endKey.n1 = 0;
		endKey.n0 = m_fbId;
		startKey.n0 <<= 1;
		endKey.n0 <<= 1;
		endKey.n0 |= 0x01;
		// now we use this
		m_dedupEidBuf.set (8,0,0,NULL,0,false,m_niceness,"deibuf");
		// advance phase
		m_phase = 1;
		if ( ! m_msg0.getList ( -1, // hostid
					0 , // ip
					0 , // port
					0 , // maxcacheage
					false, // addtocache
					RDB_FACEBOOKDB,
					"",//coll,
					&m_list1,
					(char *)&startKey,
					(char *)&endKey,
					13, // minrecsizes
					this ,
					queueLoopWrapper,
					m_niceness ) )
			return;
	}
	// check for errors
	if ( m_phase == 1 ) {
		if ( g_errno ) {
			log("fbqueue: error getting facebookdb rec: %s", 
			    mstrerror(g_errno));
			goto error;
		}
		// empty is bad
		if ( m_list1.getListSize() <= 0 ) {
			log("fbqueue: facebookdb rec is empty. wtf? fbid=%"INT64"",
			    m_fbId);
			g_errno = EBADREPLY;
			goto error;
		}
		// get the facebook rec... why?
		m_oldFbrec = (FBRec *)m_list1.getList();
		// sanity
		if ( m_oldFbrec->m_fbId != m_fbId ) {
			log("fbqueue: fbid mismatch. fbid=%"INT64"", m_fbId);
			g_errno = EBADREPLY;
			goto error;
		}
		// deserialize...
		deserializeMsg ( sizeof(FBRec) ,
				 &m_oldFbrec->size_accessToken ,
				 &m_oldFbrec->size_friendIds ,
				 &m_oldFbrec->ptr_accessToken ,
				 m_oldFbrec->m_buf );
		// copy for calling fql
		strcpy ( m_accessToken , m_oldFbrec->ptr_accessToken );
		
	retryDownload:

		// note it
		log("facebook: downloading user info again");
		// get your facebook user info again in case it changed
		SafeBuf cmd;
		cmd.safePrintf ( "SELECT uid,username,first_name,last_name,name,pic_square,profile_update_time,timezone,religion,birthday,birthday_date,sex,hometown_location,current_location,activities,interests,is_app_user,music,tv,movies,books,about_me,status,online_presence,proxied_email,verified,website,is_blocked,contact_email,email,is_minor,work,education,sports,languages,likes_count,friend_count FROM user where uid=me()");
		cmd.urlEncode();
		// www.howtobe.pro/facebook-graph-api-graph-api-for-issuing-
		// fql-queries make a url
		SafeBuf ubuf;
		ubuf.safePrintf("https://api.facebook.com/method/"
				"fql.query?query=%s"
				"&access_token=%s&format=xml"
				, cmd.getBufStart() 
				, m_accessToken 
				);
		log("facebook: cmd = %s",ubuf.getBufStart());
		// advance phase
		m_phase = 2;
		// reset
		g_errno = 0;
		// get the results
		if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2 ,
					     40*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     30000000 , // maxTextDocLen   ,
					     30000000 , // maxOtherDocLen  ,
					     g_conf.m_spiderUserAgent    ) )
			// return false if blocked
			return;
		// otherwise, somehow got it without blocking... wtf?
		//return gotFQLReply();
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		log("fql: error getting doc: %s",mstrerror(g_errno));
		goto error;
	}
	// got reply
	if ( m_phase == 2 ) {
		// bail on http reply error
		if ( g_errno ) {
			log("fbqueue: error getting fb user rec: %s", 
			    mstrerror(g_errno));
			goto error;
		}
		m_phase = 3;
		// a quick sanity check
		char *reply = m_socket->m_readBuf;
		int32_t  replyLen = m_socket->m_readOffset;
		// i've seen crappy facebook return essentially an empty
		// reply here, so make sure we have all the fields!!
		if ( ! strstr(reply,"<uid>") ||
		     ! strstr(reply,"<name") ||
		     ! strstr(reply,"<first_name") ||
		     // might be <interests/>
		     ! strstr(reply,"<interests") ) {
			log("facebook: missing uid in facebook reply #1");
			SafeBuf eb;
			eb.safeMemcpy ( reply,replyLen);
			char fname[128];
			sprintf(fname,"error1.%"UINT64"",m_fbId);
			eb.save("/tmp/",fname);
			// do a retry
			if ( m_retryCount++ < 5 ) {
				log("facebook: retrying %"INT32".",m_retryCount);
				goto retryDownload;
			}
			// otherwise abandon ship
			g_errno = EBADREPLY;
			goto error;
		}
		// copy reply into our final SafeBuf buffer
		m_fullReply.safeMemcpy ( m_socket->m_readBuf,
					 m_socket->m_readOffset );
	}
	// download friend list
	if ( m_phase == 3 ) {
		// note it
		log("facebook: downloading friend list fbid=%"INT64"",m_fbId);
		// get your facebook user info again in case it changed
		SafeBuf cmd;
		cmd.safePrintf ( "SELECT uid2 from friend WHERE uid1=me()");
		cmd.urlEncode();
		SafeBuf ubuf;
		ubuf.safePrintf("https://api.facebook.com/method/"
				"fql.query?query=%s"
				"&access_token=%s&format=xml"
				, cmd.getBufStart() 
				, m_accessToken 
				);
		log("facebook: cmd = %s",ubuf.getBufStart());
		// advance phase
		m_phase = 4;
		// reset start for phase 3
		m_chunkStart = 0;
		// reset chunksize for phase 3
		m_chunkSize = 300;
		// reset
		g_errno = 0;
		// get the results
		if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2 ,
					     40*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     30000000 , // maxTextDocLen   ,
					     30000000 , // maxOtherDocLen  ,
					     g_conf.m_spiderUserAgent    ) )
			// return false if blocked
			return;
		// otherwise, somehow got it without blocking... wtf?
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		log("fql: error getting doc: %s",mstrerror(g_errno));
		goto error;
	}
	// got reply
	if ( m_phase == 4 ) {
		// mime error?
		HttpMime mime;
		// exclude the \0 i guess. use NULL for url.
		mime.set ( m_socket->m_readBuf, m_socket->m_readOffset-1,NULL);
		// not good?
		int32_t httpStatus = mime.getHttpStatus();
		if ( httpStatus != 200 ) g_errno = EBADREPLY;
		// bail on http reply error
		if ( g_errno ) {
			log("fbqueue: error getting friend list fbid=%"INT64": "
			    "%s", m_fbId,   mstrerror(g_errno));
			goto error;
		}
		m_phase = 5;
		// copy reply into our final SafeBuf buffer
		m_fullReply.safeMemcpy ( m_socket->m_readBuf,
					 m_socket->m_readOffset );
	}


	// download some event member rsvp statuses
	if ( m_phase == 5 ) {
	recallMembers:
		// note it
		log("facebook: downloading event members #%"INT32"-#%"INT32" "
		    "fbid=%"INT64"",
		    m_chunkStart,m_chunkStart+m_chunkSize,m_fbId);
		// get status of each friend attending an event & what eventid
		SafeBuf cmd;
		// if this fails then chunk down to 50 instead of 300
		cmd.safePrintf ( "SELECT uid, eid, rsvp_status, start_time "
				 "FROM event_member where uid IN "
				 "( SELECT uid2 from friend WHERE uid1=me() "
				 "LIMIT %"INT32",%"INT32") "
				 , m_chunkStart , m_chunkSize 
				 // this start_time from the event_member table
				 // is not accurate. it is often in the past! 
				 // wtf?
				 //"AND start_time > now()"
				 );
		cmd.urlEncode();
		SafeBuf ubuf;
		ubuf.safePrintf("https://api.facebook.com/method/"
				"fql.query?query=%s"
				"&access_token=%s&format=xml"
				, cmd.getBufStart() 
				, m_accessToken 
				);
		log("facebook: cmd = %s",ubuf.getBufStart());
		// advance chunkstart
		m_chunkStart += m_chunkSize;
		// advance phase
		m_phase = 6;
		// reset
		g_errno = 0;
		// get the results
		if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2 ,
					     40*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     30000000 , // maxTextDocLen   ,
					     30000000 , // maxOtherDocLen  ,
					     g_conf.m_spiderUserAgent    ) )
			// return false if blocked
			return;
		// otherwise, somehow got it without blocking... wtf?
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		log("fql: error getting doc: %s",mstrerror(g_errno));
		goto error;
	}
	// got a reply
	if ( m_phase == 6 ) {
		// mime error?
		HttpMime mime;
		// exclude the \0 i guess. use NULL for url.
		mime.set ( m_socket->m_readBuf, m_socket->m_readOffset-1,NULL);
		// not good?
		int32_t httpStatus = mime.getHttpStatus();
		if ( httpStatus != 200 ) g_errno = EBADREPLY;
		// bail on http reply error
		if ( g_errno ) {
			log("fbqueue: error getting event members fbid=%"INT64": "
			    "%s", m_fbId,   mstrerror(g_errno));
			// count it
			m_errorCount++;
			// retry?
			if ( m_errorCount < 10 ) goto recallMembers;
			// final phase. wrap it up
			goto error;
		}
		// advance
		m_phase = 7;
		// copy reply into our final SafeBuf buffer
		m_fullReply.safeMemcpy ( m_socket->m_readBuf,
					 m_socket->m_readOffset );
		// if we had some repeat!
		if ( strstr ( m_socket->m_readBuf , "<eid>" ) ) {
			log("facebook: recalling event members");
			goto recallMembers;
		}
		// reset for my events
		m_myChunkStart = 0;
		m_myChunkSize = 300;
	}

	if ( m_phase == 7 ) {
		// get MY events
	recallMyEvents:
		// note it
		log("facebook: downloading my event ids #%"INT32"-#%"INT32" "
		    "fbid=%"INT64"",
		    m_myChunkStart,m_myChunkStart+m_myChunkSize,m_fbId);
		// get status of each friend attending an event & what eventid
		SafeBuf cmd;
		// if this fails then chunk down to 50 instead of 300
		cmd.safePrintf ( "SELECT uid, eid, rsvp_status, start_time "
				 "FROM event_member where uid=me() "
				 "LIMIT %"INT32",%"INT32""
				 , m_myChunkStart , m_myChunkSize 
				 // this start_time from the event_member table
				 // is not accurate. it is often in the past! 
				 // wtf?
				 //"AND start_time > now()"
				 );
		cmd.urlEncode();
		SafeBuf ubuf;
		ubuf.safePrintf("https://api.facebook.com/method/"
				"fql.query?query=%s"
				"&access_token=%s&format=xml"
				, cmd.getBufStart() 
				, m_accessToken 
				);
		log("facebook: cmd = %s",ubuf.getBufStart());
		// advance chunkstart
		m_myChunkStart += m_myChunkSize;
		// advance phase
		m_phase = 8;
		// reset
		g_errno = 0;
		// get the results
		if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2 ,
					     40*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     30000000 , // maxTextDocLen   ,
					     30000000 , // maxOtherDocLen  ,
					     g_conf.m_spiderUserAgent    ) )
			// return false if blocked
			return;
		// otherwise, somehow got it without blocking... wtf?
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		log("fql: error getting doc: %s",mstrerror(g_errno));
		goto error;
	}
	// got a reply
	if ( m_phase == 8 ) {
		// mime error?
		HttpMime mime;
		// exclude the \0 i guess. use NULL for url.
		mime.set ( m_socket->m_readBuf, m_socket->m_readOffset-1,NULL);
		// not good?
		int32_t httpStatus = mime.getHttpStatus();
		if ( httpStatus != 200 ) g_errno = EBADREPLY;
		// bail on http reply error
		if ( g_errno ) {
			log("fbqueue: error getting my events fbid=%"INT64": "
			    "%s", m_fbId,   mstrerror(g_errno));
			// count it
			m_errorCount++;
			// retry?
			if ( m_errorCount < 10 ) goto recallMembers;
			// final phase. wrap it up
			goto error;
		}
		// advance
		m_phase = 9;
		// copy reply into our final SafeBuf buffer
		m_fullReply.safeMemcpy ( m_socket->m_readBuf,
					 m_socket->m_readOffset );
		// if we had some repeat!
		if ( strstr ( m_socket->m_readBuf , "<eid>" ) ) {
			log("facebook: recalling my events");
			goto recallMyEvents;
		}
		// init this for downloading events
		m_eventStartNum = 0;
		// start off high, reduce to 10 on error
		m_eventStep = 100;
	}

	// save the fbrec from m_fullReply
	if ( m_phase == 9 ) {
		// save to disk for debug
		char fname[64];
		sprintf(fname,"%"INT64".txt",m_fbId);
		m_fullReply.save("/tmp/",fname);
		// compile into a new facebook rec
		char *content    = m_fullReply.getBufStart();
		int32_t  contentLen = m_fullReply.length();
		// sanity
		int64_t origFbId = m_fbId;
		// is this messing up our Msgfb::m_fbId???
		if ( ! setFBRecFromFQLReply(content,contentLen,&m_fbrecGen)) {
			log("fql: error setting fb rec from fql");
			g_errno = EBADREPLY;
			goto error;
		}
		// must be there!
		if ( ! m_fbrecGen.m_fbId ) {
			log("fql: failed to get facebook id from reply");
			g_errno = EBADENGINEER;
			goto error;
		}
		// sanity
		if ( m_fbId != origFbId ) { char *xx=NULL;*xx=0; }
		// we loaded this in phase 0
		FBRec *src = m_oldFbrec;
		// we just made this from facebook replies
		FBRec *dst = &m_fbrecGen;
		// merge src into dst. merge the old into the new.
		mergeFBRec ( dst , src );
		// advance to downloading events now
		m_phase = 10;
		// come here when done saving
		m_afterSaveCallback = queueLoopWrapper;
		// save the merged rec now
		if ( ! saveFBRec( &m_fbrecGen ) ) return;
	}

	// get events now
	if ( m_phase == 10 ) {
	recallEvents:
		// reset these so we do not re-inject the same events
		m_evPtrBuf.reset();
		m_evIdsBuf.reset();
		// skip matt wells for now
		if ( m_fbId == MATTWELLS ) { // 100003532411011LL ) {
			log("facebook: skipping matt wells event members "
			    "download");
			// save it so we can turn off the in queue bit
			//m_phase = 15;
			//goto skipdown;
		}
		// note it
		log("facebook: downloading events #%"INT32"-#%"INT32" fbid=%"INT64"",
		    m_eventStartNum,m_eventStartNum+m_eventStep,m_fbId);
		SafeBuf cmd;
		cmd.safePrintf ( "SELECT eid, "
				 "name, "
				 // deprecated as of july 5th 2012
				 //"tagline, "
				 //"nid, "
				 "pic_small, "
				 "pic_big, "
				 "pic_square, "
				 "pic, "
				 "host, "
				 "description, "
				 // deprecated as of july 5th 2012
				 //"event_type, "
				 //"event_subtype, "
				 "start_time, "
				 "end_time, "
				 "creator, "
				 "update_time, "
				 "location, "
				 "venue, "
				 "privacy, "
				 "hide_guest_list, "
				 "can_invite_friends "
				 "FROM event WHERE "
				 // this does not seem to work either!
				 //"start_time > now() AND "
				 "eid IN (" );
		// from buf
		int32_t max = m_eidBuf.length() / 8;
		// did we add any?
		bool printed = false;
		// int16_tcut
		int64_t *eids = (int64_t *)m_eidBuf.getBufStart();
		// list some eids here
		for ( int32_t i = m_eventStartNum ; 
		      i < max &&
		      i < m_eventStartNum + m_eventStep ; i++ ) {
			if ( printed ) cmd.pushChar(',');
			printed = true;
			cmd.safePrintf("%"INT64"",eids[i]);
		}
		// end it
		cmd.safePrintf(")");

		cmd.urlEncode();
		SafeBuf ubuf;
		ubuf.safePrintf("https://api.facebook.com/method/"
				"fql.query?query=%s"
				"&access_token=%s&format=xml"
				, cmd.getBufStart() 
				, m_accessToken 
				);
		log("facebook: cmd = %s",ubuf.getBufStart());
		// advance chunkstart
		m_eventStartNum += m_eventStep;
		// advance phase
		m_phase = 11;
		// reset
		g_errno = 0;
		// get the results
		if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
					     0 , // urlIp
					     0                    , // offset
					     -1                   ,
					     0 , // ifModifiedSince ,
					     this               , // state
					     queueLoopWrapper2 ,
					     40*1000      , // 20 sec timeout
					     0 , // proxyip
					     0 , // proxyport
					     30000000 , // maxTextDocLen   ,
					     30000000 , // maxOtherDocLen  ,
					     g_conf.m_spiderUserAgent    ) )
			// return false if blocked
			return;
		// otherwise, somehow got it without blocking... wtf?
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		log("fql: error getting doc: %s",mstrerror(g_errno));
		goto error;
	}
	// got events reply
	if ( m_phase == 11 ) {
		// mime error?
		HttpMime mime;
		// exclude the \0 i guess. use NULL for url.
		if ( m_socket && ! g_errno ) {
			mime.set ( m_socket->m_readBuf, 
				   m_socket->m_readOffset-1,NULL);
			// not good?
			int32_t httpStatus = mime.getHttpStatus();
			if ( httpStatus != 200 ) g_errno = EBADREPLY;
		}
		// bail on http reply error
		if ( g_errno ) {
			char *reply = "";
			if ( m_socket && m_socket->m_readBuf )
				reply = m_socket->m_readBuf;
			log("fbqueue: error getting events fbid=%"INT64": "
			    "%s : %s", m_fbId,   mstrerror(g_errno),reply);
			// bail right away if doing spider
			if ( m_fbId <= 0 ) goto error;
			// final phase. wrap it up
			if ( ++m_errorCount >= 10 ) goto error;
			// first time?
			if ( m_eventStep > 10 ) m_eventStartNum -= m_eventStep;
			// reduce step
			m_eventStep = 10;
			// retry
			goto recallEvents;
		}

		m_rbuf.purge();

		// convert json to xml
		m_rbuf.safeMemcpy ( m_socket->m_readBuf ,
				    m_socket->m_readOffset );

		// nuke the read buf then from the http server
		mfree ( m_socket->m_readBuf , m_socket->m_readBufSize,"sss" );
		// do not allow tcpsocket to free it again
		m_socket->m_readBuf = NULL;

		char *type = NULL;
		if ( m_fbId ==  0LL ) type = "facebook";
		if ( m_fbId == -1LL ) type = "stubhub";
		if ( m_fbId == -2LL ) type = "eventbrite";

		// show it
		if ( type )
			log("facebook: got %s reply (%"INT32" bytes)"
			    , type , m_rbuf.length() );

		//fprintf(stderr,"full raw reply=%s",m_rbuf.getBufStart());

		// do not do jsontoxml on the mime header!
		char *rb = m_rbuf.getBufStart();
		char *rc = strstr(rb,"\r\n\r\n");
		int32_t  pos = 0;
		if ( rc ) pos = (rc+4) - rb;

		// convert json to xml if it is in json. only for facebook
		m_rbuf.convertJSONtoXML ( m_niceness , pos );

		//fprintf(stderr,
		//"full converted reply=%s",m_rbuf.getBufStart());

		// show it
		//if ( m_fbId == 0LL )
		//	log("facebook: got fb reply xml: %s",
		//	    m_rbuf.getBufStart() );


		// did it have some events?
		//char *hadSome = strstr ( m_socket->m_readBuf , "<eid>" ) ;
		// save reply
		//m_facebookReply2     = m_socket->m_readBuf;
		//m_facebookReplySize2 = m_socket->m_readOffset;
		//m_facebookAllocSize2 = m_socket->m_readBufSize;

		// sanity. must be \0. httpserver should add this if not
		// already there!
		//if ( m_facebookReply2 [ m_facebookReplySize2 ] ) {
		//	log("fbqueue: facebookreply2 does not end in \\0");
		//	g_errno = EBADREPLY;
		//	goto error;
		//}

		// . set m_numEvents and m_eventPtrs from m_facebookReply
		// . they reference into the reply, so do not free 
		//   m_facebookReply until destructor is called
		m_numEvents = 0;

		// . get the event delimeter in the xml
		// . both facebook and eventbrite use this
		char *delim = "<event>";
		char *dend  = "</event>";
		// stubhub uses "<doc>" to delineate events
		if ( m_fbId == -1 ) {
			delim = "<doc>";
			dend  = "</doc>";
		}

		// parse the events into an array of ptrs for easier injection
		// scan for <event> tags and set m_evptrs safebuf to 
		// each ptr to those
		char *p = m_rbuf.getBufStart(); // facebookReply2;
		for ( ; *p ; ) {
			// scan to first <event> tag
			p = strstr ( p , delim );
			if ( ! p ) break;
			// store start for ptr
			char *start = p + gbstrlen(delim);
			// find end
			char *end = strstr ( p , dend );
			if ( ! end ) break;
			// null term it
			*end = '\0';
			// for next round
			p = end + gbstrlen(dend);
			// try to get event id
			char *ep = NULL;
			if ( m_fbId >= 0 ) {
				ep = strstr ( start, "<eid>");
				if ( ep ) ep += 5;
			}
			// is it a stubhub event? 
			// <str name="event_id">2659223</str>
			if ( m_fbId == -1 ) {
				ep = strstr ( start, "=\"event_id\">");
				if ( ep ) ep += 12;
			}
			// . is it an eventbrite event?
			// . the first <id> after the <event> is the event id
			if ( m_fbId == -2 ) {
				ep = strstr ( start, "<id>");
				if ( ep ) ep += 4;
			}

			if ( ! ep ) continue;
			int64_t eid = strtoull ( ep , NULL , 10 );
			if ( eid == 0 ) continue;
			if ( eid < 0 ) log("facebook: wtf? eid is 0");

			// for eventbrite record the last eventid because
			// we use that for paging through
			if ( m_fbId == -2 )
				s_lastEventBriteEventId = eid;

			// store it
			if ( ! m_evPtrBuf.pushLong ( (int32_t)start)) goto error;
			if ( ! m_evIdsBuf.pushLongLong ( eid)) goto error;
			// count them
			m_numEvents++;
		}
		log("facebook: got %"INT32" events from %s",m_numEvents,type);

		// if we got 0 events from an eventbrite request, then
		// wait for an hour before retrying
		if ( m_numEvents == 0 && m_fbId == -2 ) {
			log("facebook: pausing eventbrite feed for 1 hour");
			int32_t now = getTimeGlobal();
			s_eventBriteWaitUntil = now + 3600;
		}

		// reset loop
		m_i = 0;
		// make a new state
		if ( m_numEvents > 0 ) {
			Msg7 *msg7;
			try { msg7 = new (Msg7); }
			catch ( ... ) { 
				g_errno = ENOMEM;
				log("facebook: inject msg7 new(%i): %s", 
				    sizeof(Msg7),mstrerror(g_errno));
				goto error;
			}
			mnew ( msg7, sizeof(Msg7) , "PageInjct7" );
			// save it for freeing in destructor
			m_msg7 = msg7;
		}
		// advance
		m_phase = 12;
		// if no events, skip injection and adding likes i guess...
		if ( m_numEvents == 0 ) m_phase = 15;
	}

	// injection loop
	if ( m_phase == 12 ) {
	recallInject:
		char *coll = g_collectiondb.getColl ( m_collnum );
		char      **eventPtrs = (char **)m_evPtrBuf.getBufStart();
		int64_t  *eventIds  = (int64_t *)m_evIdsBuf.getBufStart();
		// get ptr to it
		char *content    = eventPtrs[m_i];
		int32_t  contentLen = gbstrlen(content);
		// debug thing
		//if ( eventIds[m_i] != 314901535212815LL ) {m_i++; continue;}
		// make a fake url
		char url[128];
		sprintf(url,"http://www.facebook.com/events/%"UINT64"",
			eventIds[m_i]);
		// is it a stubhub event? <str name="event_id">2659223</str>
		if ( m_fbId == -1 )
			sprintf(url,"http://www.stubhub.com/%"UINT64"",
				eventIds[m_i]);
		if ( m_fbId == -2 )
			sprintf(url,"http://www.eventbrite.com/%"UINT64"",
				eventIds[m_i]);

		// test debug (on for eventbrite now)
		if ( g_conf.m_logDebugFacebook )
			log("facebook: %s",content);
		//
		// set m_privacy for event being injected
		//
		char *s = strstr(content,"<privacy");
		// skip that
		if ( s ) s += 8;
		// skip til '>'
		for ( ; s && *s && *s !='>' ; s++ );
		// skip actual >
		if ( s && *s == '>' ) s++;
		// skip whitespace
		for ( ; s && *s && is_wspace_a(*s) ; s++ );
		// compare
		m_privacy = 0;
		if ( s && ! strncasecmp(s,"secret",6) ) 
			m_privacy = LF_PRIVATE;
		if ( s && ! strncasecmp(s,"closed",6) ) 
			m_privacy = LF_PRIVATE;

		// event brite privacy. 0 means private event.
		if ( m_fbId == -2 && s && ! strncasecmp(s,"0",1) )
			m_privacy = LF_PRIVATE;
		
		//if ( eventIds[m_i] == 273883416016761LL ) 
		//	m_privacy = LF_PRIVATE;
		// test
		//m_privacy = LF_PRIVATE;
		
		// use a forced ip for speed! otherwise it takes 
		// forever lookup up www.facebook.com for some reason!
		int32_t forcedIp = atoip("69.171.224.39");
		// advance up here
		m_i++;
		// and the phase too
		m_phase = 13;
		// inject just that
		if ( ! m_msg7->inject ( url ,
					forcedIp,
					content ,
					contentLen ,
					false, // recyclecontent
					CT_XML, // contentType,
					coll ,
					false ,
					NULL, // username
					NULL , // pwd
					m_niceness,
					this ,
					queueLoopWrapper ) )
			return;
		// bail on error
		if ( g_errno ) goto error;
		// how did this happen? it needs to block... otherwise
		// we have to add to likedb here.
		char *xx=NULL;*xx=0;
	}

	if ( m_phase == 13 ) {
		// error?
		if ( g_errno ) 
			log("facebook: injection had error: %s",
			    mstrerror(g_errno));
		// add the likes
		XmlDoc *xd = &m_msg7->m_xd;
		/*
		// if event was not found or added for some reason...
		if ( xd->m_numHashableEvents <= 0 ) {
			// try to do the next one
			if ( m_i < m_numEvents ) goto recallInject;
			// or another round?
			if ( m_numEvents > 0 ) goto recall
		}
		if ( xd->m_indexCode && xd->m_indexCodeValid ) {
			// note it
			log("facebook: could not index doc: %s",
			    mstrerror(xd->m_indexCode));
			// try to do the next one
			if ( m_i < m_numEvents ) goto recallInject;
		}
		*/
		if ( xd->m_indexCode && xd->m_indexCodeValid )
			// note it
			log("facebook: could not index doc: %s",
			    mstrerror(xd->m_indexCode));
		// advancwe
		m_phase = 14;
		//
		// ADD LIKES
		//
		// scan the event_members reply we got and cross-reference
		// those facebook eventids with our eventhash/evid/docid guys
		// we got in the injection reply to see if we added the 
		// facebook event to our db. in that case, we also want to add
		// the  maybe/goingto/invitedto/notgoing flags.
		// uses the eventhash64, eventid, docid of event added!
		// returns false with g_errno set on error.
		if ( ! makeLikedbKeyList ( m_msg7 , &m_list3 ) )
			goto error;
		// if nothing to add, we are done
		//if ( m_list3.getListSize() == 0 && m_i < m_numEvents ) 
		//	goto recallInject;
		char *coll = g_collectiondb.getColl ( m_collnum );
		// add that
		if ( ! m_msg1.addList ( &m_list3 ,
					RDB_LIKEDB ,
					coll ,
					this ,
					queueLoopWrapper,
					false ,
					m_niceness ) ) // niceness
			return;
	}

	if ( m_phase == 14 ) {
		// we saved the likes...
		// try to do the next one
		if ( m_i < m_numEvents ) goto recallInject;
		// . all done, but need to load more events?
		// . skip if fbid is zero, which means its facebook spider
		if ( m_fbId > 0 && m_numEvents > 0 ) goto recallEvents;
		// ok, i guess, we are done
		m_phase = 15;
	}

 skipdown:
	if ( m_phase == 15 ) {
		// we can nuke this now
		m_rbuf.purge();
		// advance
		m_phase = 16;
		// a 0 fbid is spidering facebook
		if ( m_fbId > 0 ) {
			// final save of rec to clear the FB_INQUEUE bit
			m_afterSaveCallback = queueLoopWrapper;
			log("facebook: saving final rec for fbid=%"INT64"",m_fbId);
			m_fbrecGen.m_flags &= ~FB_INQUEUE;
			m_fbrecGen.m_eventsDownloaded = getTimeGlobal();
			// this calls serializeMsg() which mallocs a 
			// new reply to add
			if ( ! saveFBRec( &m_fbrecGen ) ) return;
		}
	}

	// . remove from queue
	// . all done!
	if ( m_phase == 16 ) {
		// int16_tcut
		int32_t err = m_errno;
		// or inherit this. we might have forgotten to set m_errno
		if ( ! err && g_errno ) err = g_errno;
		// no longer in progress
		m_inProgress = false;
		// this will purge fullreply
		reset();
		// note it
		log("facebook: done with queue for fbid=%"INT64". error=%s",
		    g_fbq1[0],mstrerror(err));

		// if we are eventbrite, update s_ptr4 to the last eventid
		// that we had on that page
		s_ptr4 = s_lastEventBriteEventId;

		// save it for potential re-add
		//int64_t fsaved = g_fbq1  [0];
		//collnum_t csaved = g_colls1[0];
		// shift queue down
		for ( int32_t i = 1 ; i < g_n1 ; i++ ) {
			g_fbq1  [i-1] = g_fbq1  [i];
			g_colls1[i-1] = g_colls1[i];
		}
		// one less in queue
		g_n1--;
		g_numOut--;
		return;
	}

 error:
	log("facebook: queue fbid %"INT64" had error: %s",
	    m_fbId,mstrerror(g_errno));
	// no longer in progress
	m_inProgress = false;
	// this will purge fullreply
	reset();
	// save it for potential re-add
	int64_t fsaved = g_fbq1  [0];
	collnum_t csaved = g_colls1[0];
	// shift queue down
	for ( int32_t i = 1 ; i < g_n1 ; i++ ) {
		g_fbq1  [i-1] = g_fbq1  [i];
		g_colls1[i-1] = g_colls1[i];
	}
	// one less in queue
	g_n1--;
	g_numOut--;
	// re-add on certain errors
	if ( g_errno == ETIMEDOUT || g_errno == ESOCKETCLOSED ) {
		log("facebook: re-queue fbid %"INT64" from error: %s",
		    m_fbId,mstrerror(g_errno));
		queueFBId ( fsaved , csaved );
	}
}

// . these are ptrs to likedb records
// . these first int64_t is the least significant
// . the 2nd int64_t is more
int likedbCmp ( const void *a , const void *b ) {
	const key192_t *k1 = (key192_t *)a;
	const key192_t *k2 = (key192_t *)b;
	if ( k1->n2 < k2->n2 ) return -1;
	if ( k1->n2 > k2->n2 ) return  1;
	if ( k1->n1 < k2->n1 ) return -1;
	if ( k1->n1 > k2->n1 ) return  1;
	if ( k1->n0 < k2->n0 ) return -1;
	if ( k1->n0 > k2->n0 ) return  1;
	return 0;
}

// scan the event_members reply we got and cross-reference
// those facebook eventids with our eventhash/evid/docid guys
// we got in the injection reply to see if we added the facebook
// event to our db. in that case, we also want to add the 
// maybe/goingto/invitedto/notgoing flags
// returns false with g_errno set on error.
bool Msgfb::makeLikedbKeyList ( Msg7 *msg7 , RdbList *list ) {
	// reset
	list->reset();
	// sanity
	if ( m_i-1 < 0 ) { char *xx=NULL;*xx=0; }
	// int16_tcuts
	XmlDoc *xd = &msg7->m_xd;
	int64_t docId = xd->m_docId;
	// none if no events!
	if ( ! xd->size_eventData ) return true;
	if ( ! xd->m_eventDataValid ) return true;
	// . get first event and use that one
	// . really, being facebook, there should only be one eventid!
	//   but maybe later it may be different and we'll have to "like"
	//   each eventid/docid combo
	EventDisplay *ed = (EventDisplay *)xd->ptr_eventData;
	if ( ! ed ) {
		log("facebook: eventdisplay is null! wtf?");
		return true;
	}
	int32_t gbeventId = ed->m_indexedEventId;
	//int32_t gbeventHash32 = (int32_t)((uint32_t)ed->m_eventHash64);
	uint64_t evh64 = ed->m_eventHash64;
	// int16_tcuts
	//char      **eventPtrs = (char **)m_evPtrBuf.getBufStart();
	int64_t  *eventIds  = (int64_t *)m_evIdsBuf.getBufStart();
	// what facebook eventid did we just inject?
	int64_t eid = eventIds[m_i-1];
	int32_t count = 0;
	SafeBuf tmpBuf;
	if ( ! tmpBuf.reserve ( 50 ) ) return false;

	// but don't use lts->m_start_time, it is not UTC really
	// so use our own...
	Interval *ii = (Interval *)ed->m_int;
	// must be there
	if ( ed->m_intSize <= 0 ) {
		log("facebook: wtf? no intervals!");
		return true;
	}
	int32_t start_time = ii->m_a;
		
	// scan the table to see what users had an rsvp_status for this event
	for ( int32_t i = 0 ; i < m_likedbTable.m_numSlots ; i++ ) {
		// skip empties
		if ( ! m_likedbTable.m_flags[i] ) continue;
		// get the record in there
		int64_t *eid2 = (int64_t *)m_likedbTable.getKeyFromSlot(i);
		// skip if not a match
		if ( *eid2 != eid ) continue;
		// the data is a key that we made above
		LikedbTableSlot *lts;
		lts = (LikedbTableSlot *)m_likedbTable.getDataFromSlot(i);
		// debug note
		if ( g_conf.m_logDebugFacebook )
			log("facebook: makerec uid=%"INT64"",lts->m_uid);
		// assume they "like" it
		int64_t value = 1LL;
		// unless it is "negative"
		//if ( negative ) value = 0LL;
		// make the flags
		int32_t flag = lts->m_rsvp;
		// indicate from facebook so we do not upload it
		flag |= LF_FROMFACEBOOK;
		// . this makes two recs to add to likedb
		// . the first one starts with uid in the key so it will be 
		//   used to find events that your friends are 
		// . the second one starts with docid/evid so it can be used
		//   to lookup who likes an event when we generate the summary
		//   for the event because its in the search results
		char *recs = g_likedb.makeRecs ( lts->m_uid        ,
						 docId             ,
						 gbeventId         ,
						 start_time        ,
						 //lts->m_start_time ,
						 //lts->m_rsvp       ,
						 flag              ,
						 evh64 ,
						 value             );
		//continue;
		// add to list otherwise
		tmpBuf.safeMemcpy ( recs , (int32_t)LIKEDB_RECSIZE*2 );
		count++;
	}
	// add a separate rec for LF_PRIVATE if we need to because we can't
	// set more than one bit in a likedb key otherwise you can't UNDO
	// or delete key bits easily... i guess the event could change 
	// privacy status the next time we index it, so this needs to
	// override... and since the uid is 0 (the system uid) let's ignore
	// the first record and only add the second.
	// and because i only want to add these keys when the event is
	// private to avoid bloat, let's just add a negative key when its
	// public in order to nuke any possible private key.
	char *recs = g_likedb.makeRecs ( 0LL               , // uid
					 docId             ,
					 gbeventId         ,
					 start_time        ,
					 LF_FROMFACEBOOK|LF_PRIVATE,
					 evh64             ,
					 1LL               );
	// point to the 2nd key
	char *rec2 = recs + LIKEDB_RECSIZE;
	int32_t recSize = (int32_t)LIKEDB_RECSIZE;
	// make it a del/negative key by clearing the lowest bit
	if ( ! m_privacy ) { 
		rec2[0] &= 0xfe;
		// negative keys are keys only - no data!!!
		recSize = (int32_t)LIKEDB_KEYSIZE;
	}
	// add to list otherwise
	tmpBuf.safeMemcpy ( rec2 , recSize );
	count++;

	// debug
	//key192_t *k2 = (key192_t *)rec2;
	//log("key: 0x%"XINT64" 0x%"XINT64" 0x%"XINT64"",k2->n2,k2->n1,k2->n0);
	// all done if nothing
	if ( count == 0 ) return true;
	// sort the records in tmp now
	char *buf = tmpBuf.getBufStart();
	// sort for rdblist
	gbqsort ( buf , count , (int32_t)LIKEDB_RECSIZE, likedbCmp );
	// use the list we got
	key192_t startKey;
	key192_t endKey;
	startKey.setMin();
	endKey.setMax();
	// that is our list
	list->set ( buf ,
		    tmpBuf.length() ,
		    buf, // alloc
		    tmpBuf.getCapacity() , // allocSize
		    (char *)&startKey ,
		    (char *)&endKey  ,
		    LIKEDB_DATASIZE , // fixed datasize
		    true , // own data? yeah, free it when done
		    false , // use half keys? no.
		    sizeof(key192_t) );
	// steal it from safebuf so it doesn't free it
	tmpBuf.detachBuf();
	return true;
}
		

/*

YE OLD PIPELINE


static void gotRecWrapper ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->hitFacebook ( ) ) return;
	mfb->m_callback ( mfb->m_state );
}

// get it from facebookdb
bool Msgfb::processFBId ( int64_t fbId , 
			  collnum_t collnum,
			  void *state , 
			  void (* callback) (void *) ) {

	reset();

	m_inProgress = true;
	m_fbId = fbId;
	m_collnum = collnum;
	m_state = state;
	m_callback = callback;

	// sanity
	if  ( ! m_fbId ) { char *xx=NULL;*xx=0; }

	int32_t niceness = 0;
	key96_t startKey;
	key96_t endKey;
	startKey.n1 = 0;
	startKey.n0 = m_fbId;
	endKey.n1 = 0;
	endKey.n0 = m_fbId;
	startKey.n0 <<= 1;
	endKey.n0 <<= 1;
	endKey.n0 |= 0x01;
	//char *coll = g_collectiondb.getColl ( m_collnum );
	if ( ! m_msg0.getList ( -1, // hostid
				0 , // ip
				0 , // port
				0 , // maxcacheage
				false, // addtocache
				RDB_FACEBOOKDB,
				"none",//coll,
				&m_list1,
				(char *)&startKey,
				(char *)&endKey,
				10, // minrecsizes
				this ,
				gotRecWrapper,
				niceness ) )
		return false;
	// i guess we got it without blocking
	return hitFacebook();
}

static void gotFQLReplyWrapper ( void *state , TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->gotFQLReply( s ) ) return;
	mfb->m_callback ( mfb->m_state );
}

// once we get the facebook access token, we can get the user info
// https://graph.facebook.com/me?access_token=ACCESS_TOKEN 
// NO... i would use one fql call at this point...
// if it says "Requires valid signature" you need the access token
// fql console: http://developers.facebook.com/docs/reference/rest/fql.query/
bool Msgfb::hitFacebook ( ) {

	log("facebook: downloading event_members for %"UINT64"",m_fbId);

	if ( g_errno ) {
		log("fbqueue: error getting facebookdb rec: %s", 
		    mstrerror(g_errno));
		return true;
	}

	// empty is bad
	if ( m_list1.getListSize() <= 0 ) {
		log("fbqueue: facebookdb rec is empty. wtf? fbid=%"INT64"",
		    m_fbId);
		g_errno = EBADREPLY;
		return true;
	}

	// get the facebook rec... why?
	m_fbrecPtr = (FBRec *)m_list1.getList();

	// sanity
	if ( m_fbrecPtr->m_fbId != m_fbId ) {
		log("fbqueue: fbid mismatch. fbid=%"INT64"", m_fbId);
		g_errno = EBADREPLY;
		return true;
	}

	// deserialize...
	deserializeMsg ( sizeof(FBRec) ,
			 &m_fbrecPtr->size_accessToken ,
			 &m_fbrecPtr->size_friendIds ,
			 &m_fbrecPtr->ptr_accessToken ,
			 m_fbrecPtr->m_buf );

	// copy for calling fql
	strcpy ( m_accessToken , m_fbrecPtr->ptr_accessToken );

	// get your facebook user info again in case it changed
	SafeBuf cmd1;
	cmd1.safePrintf ( "SELECT uid,username,first_name,last_name,name,pic_square,profile_update_time,timezone,religion,birthday,birthday_date,sex,hometown_location,current_location,activities,interests,is_app_user,music,tv,movies,books,about_me,status,online_presence,proxied_email,verified,website,is_blocked,contact_email,email,is_minor,work,education,sports,languages,likes_count,friend_count FROM user where uid=me()");


	// get all friends for saving into likedb
	SafeBuf cmd2;
	cmd2.safePrintf ( "SELECT uid2 from friend WHERE uid1=me()");

	// get status of each friend attending an event and what eventid
	SafeBuf cmd3;
	cmd3.safePrintf ( "SELECT uid, eid, rsvp_status, start_time "
			  "FROM event_member where uid IN "
			  "( SELECT uid2 from friend WHERE uid1=me()) "
			  // this start_time from the event_member table
			  // is not accurate. it is often in the past! wtf?
			  //"AND start_time > now()"
			  );

	// composite
	SafeBuf json;
	json.safePrintf("{"
			"\"query1\":\"%s\""
			","
			"\"query2\":\"%s\""
			","
			"\"query3\":\"%s\""
			, cmd1.getBufStart()
			, cmd2.getBufStart()
			, cmd3.getBufStart()
			);
	json.safePrintf("}");
	json.urlEncode();
			
	// www.howtobe.pro/facebook-graph-api-graph-api-for-issuing-fql-queries
	// make a url
	SafeBuf ubuf;
	ubuf.safePrintf("https://api.facebook.com/method/"
			//"fql.query?query="
			"fql.multiquery?queries=%s"
			"&access_token=%s&format=xml"
			, json.getBufStart() 
			, m_accessToken 
			);

	log("facebook: queryurl = %s", ubuf.getBufStart() );

	// reset
	g_errno = 0;
	// get the results
	if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this               , // state
				     gotFQLReplyWrapper , // callback
				     40*1000      , // 20 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     30000000 , // maxTextDocLen   ,
				     30000000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ) )
		// return false if blocked
		return false;
	// otherwise, somehow got it without blocking... wtf?
	//return gotFQLReply();
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	log("fql: error getting doc: %s",mstrerror(g_errno));
	return true;
}

static void savedFBRecWrapper2 ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->downloadEvents ( ) ) return;
	// final save of rec to clear the FB_INQUEUE bit
	if ( ! mfb->doFinalFBRecSave ( ) ) return;
	mfb->m_callback ( mfb->m_state );
}

bool Msgfb::gotFQLReply ( TcpSocket *s ) {

	// bail on error
	if ( g_errno ) {
		log("fql: %s",mstrerror(g_errno));
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// get reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	// we reference into this, so do not free it!!
	m_facebookReply     = s->m_readBuf;
	m_facebookReplySize = s->m_readOffset;
	m_facebookAllocSize = s->m_readBufSize;
	// do not allow tcpsocket to free it. we free it in destructor.
	s->m_readBuf = NULL;


	// mime error?
	HttpMime mime;
	// exclude the \0 i guess. use NULL for url.
	mime.set ( reply, replySize - 1, NULL );
	// not good?
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 ) {
		log("facebook: bad fql request 2 http status = %"INT32". reply=%s"
		    , httpStatus 
		    , reply
		    );
		log("facebook: resuming despite error to download friends "
		    "for fbid=%"INT64"",m_fbId);
		//g_errno = EBADREPLY;
		m_errno = EBADREPLY;
		//m_errorCount++;
		//return true;
	}

	// point to content
	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = reply + replySize - content;

	// check for error
	char *errMsg = strstr(content,"<error_msg>");
	if ( errMsg ) {
		log("facebook: error in fql reply: %s", content );
		log("facebook: resuming despite error to download friends "
		    "for fbid=%"INT64"",m_fbId);
		//g_errno = EBADREPLY;
		m_errno = EBADREPLY;
		//m_errorCount++;
		//return true;
	}

	m_fbrecGen.reset();

	//
	// . set m_fbrecGen now !!
	// . compare to m_fbrecPtr to see what eventids are new
	//
	if ( ! m_errno &&
	     ! setFBRecFromFQLReply ( content     ,
				      contentLen  ,
				      &m_fbrecGen ) ) {
		log("fql: error setting fb rec from fql. pipeline 2.");
		g_errno = EBADREPLY;
		return true;
	}

	// merge fbrecPtr into m_fbrecGen
	if ( ! m_errno ) mergeFBRec ( &m_fbrecGen , m_fbrecPtr );

	// must match
	if ( ! m_errno && m_fbId != m_fbrecGen.m_fbId ) {
		log("fql: fbid mismatch in fql reply.");
		g_errno = EBADENGINEER;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// just make sure
	m_afterSaveCallback = savedFBRecWrapper2;

	// start at event #0
	m_eventStartNum = 0;
	// 100 events at a time
	m_eventStep     = 100;

	// save that before we start downloading the events
	if ( ! m_errno && ! saveFBRec ( &m_fbrecGen ) ) return false;
	// try to download new events
	return downloadEvents ( );
}


static void injectFBEventsWrapper ( void *state, TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->injectFBEvents ( s ) ) return;
	// error? try advancing!
	if ( mfb->m_errno && mfb->m_eventStartNum < 500 ) {
		if ( mfb->m_eventStep == 100 ) mfb->m_eventStep = 10;
		else mfb->m_eventStartNum += mfb->m_eventStep;
		if ( ! mfb->downloadEvents() ) return;
	}
	// final save of rec to clear the FB_INQUEUE bit
	if ( ! mfb->doFinalFBRecSave ( ) ) return;
	if ( mfb->m_callback ) mfb->m_callback ( mfb->m_state );
}
	
// download the new events and then inject them (ptr_newEvents)
bool Msgfb::downloadEvents ( ) {

	// reset this since we check for it in injectFBEventsWrapper
	m_errno = 0;

	log("facebook: downloading events for %"UINT64" (#%"INT32"-#%"INT32")",
	    m_fbId,m_eventStartNum,m_eventStartNum+m_eventStep);

	// skip matt wells for now
	if ( m_fbId == MATTWELLS ) { // 100003532411011LL ) {
		log("facebook: skipping matt wells event download");
		// final save of rec to clear the FB_INQUEUE bit
		return doFinalFBRecSave ( );
	}

	// save some mem
	//m_list1.freeList();

	//int32_t now = getTimeGlobal();

	// sanity checks
	if ( m_eventStartNum <  0 ) { char *xx=NULL; *xx=0; }
	if ( m_eventStep     <= 0 ) { char *xx=NULL; *xx=0; }


	if ( m_eventStartNum >= 100 && 
	     m_eventStartNum < 130 &&
	     m_errorCount >= 10 ) {
		log("facebook: too many errors in event downloads. "
		    "fbid=%"INT64" . giving up. start=%"INT32" errcount=%"INT32"",
		    m_fbId,m_eventStartNum,m_errorCount);
		return doFinalFBRecSave();
	}

	// get the events your friends are related to
	SafeBuf cmd4;
	cmd4.safePrintf ( "SELECT eid, "
			  "name, "
			  "tagline, "
			  "nid, "
			  "pic_small, "
			  "pic_big, "
			  "pic_square, "
			  "pic, "
			  "host, "
			  "description, "
			  "event_type, "
			  "event_subtype, "
			  "start_time, "
			  "end_time, "
			  "creator, "
			  "update_time, "
			  "location, "
			  "venue, "
			  "privacy, "
			  "hide_guest_list, "
			  "can_invite_friends "
			  "FROM event WHERE "
			  "start_time > now() AND "
			  "eid IN (" 
			  // how do i include events i am assoc. with too?
			  " SELECT eid FROM event_member WHERE "
			  "uid IN "
			  "( SELECT uid2 from friend WHERE uid1=me()) ) "
			  "LIMIT %"INT32",%"INT32""
			  , m_eventStartNum
			  , m_eventStep
			  );

	// list all the new event ids here
	//int64_t *newIds = (int64_t *)m_eidBuf.getBufStart();
	//int32_t       n      = m_eidBuf.length() / 8;
	//bool firstOne = true;
	//for ( int32_t i = 0 ; i < n ; i++ ) {
	//	if ( ! firstOne ) cmd4.pushChar(',');
	//	firstOne = false;
	//	cmd4.safePrintf("%"UINT64"",newIds[i]);
	//}
	//cmd4.safePrintf ( ")" // ORDER by start_time ASC "
	//		  // LIMIT 1001,100 etc. 
	//		  // LIMIT x,y (x is offset, y is # results)
	//		  //"LIMIT 0,100"
	//		  //, fbId 
	//		  );

	// composite
	cmd4.urlEncode();
			
	// www.howtobe.pro/facebook-graph-api-graph-api-for-issuing-fql-queries
	// make a url
	SafeBuf ubuf;
	ubuf.safePrintf("https://api.facebook.com/method/"
			"fql.query?query=%s"
			"&access_token=%s&format=xml"
			, cmd4.getBufStart() 
			, m_accessToken 
			);

	log("facebook: cmd4url = %s", ubuf.getBufStart() );

	// reset
	g_errno = 0;
	// get the results
	if ( ! g_httpServer.getDoc ( ubuf.getBufStart() ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this               , // state
				     injectFBEventsWrapper , // callback
				     40*1000      , // 20 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     30000000 , // maxTextDocLen   ,
				     30000000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ) )
		// return false if blocked
		return false;
	// otherwise, somehow got it without blocking... wtf?
	//return gotFQLReply();
	log("fql: http get did not block!");
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	log("fql: error getting doc: %s",mstrerror(g_errno));
	// final save of rec to clear the FB_INQUEUE bit
	return doFinalFBRecSave ( );
}

static void addLikesWrapper ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->addLikes ( ) ) return;
	if ( ! mfb->doInjectionLoop ( ) ) return;
	// try to read more!!
	if ( mfb->m_numEvents > 0 ) {
		mfb->m_eventStartNum += mfb->m_eventStep;
		if ( ! mfb->downloadEvents() ) return;
	}
	// final save of rec to clear the FB_INQUEUE bit
	if ( ! mfb->doFinalFBRecSave ( ) ) return;
	if ( mfb->m_callback ) mfb->m_callback ( mfb->m_state );
}

#define MAXEVENTPTRS 1000

bool Msgfb::injectFBEvents ( TcpSocket *s ) {

	// bail on error
	if ( g_errno ) {
		log("fql: %s",mstrerror(g_errno));
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// get reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	// mime error?
	HttpMime mime;
	// exclude the \0 i guess. use NULL for url.
	mime.set ( reply, replySize - 1, NULL );
	// not good?
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 ) {
		log("facebook: bad fql request 3 http status = %"INT32". reply=%s",
		    httpStatus ,reply );
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// point to content
	char *content = reply + mime.getMimeLen();

	// check for error
	char *errMsg = strstr(content,"<error_msg>");
	if ( errMsg ) {
		log("facebook: error in fql reply2: %s", content );
		g_errno = EBADREPLY;
		m_errno = g_errno;
		m_errorCount++;
		return true;
	}

	// if we are re-using this class. need to reset some things.
	m_evPtrBuf.reset();
	m_evIdsBuf.reset();

	// save reply
	m_facebookReply2     = s->m_readBuf;
	m_facebookReplySize2 = s->m_readOffset;
	m_facebookAllocSize2 = s->m_readBufSize;
	// do not allow tcpsocket to free it. we free it in destructor.
	s->m_readBuf = NULL;

	// . set m_numEvents and m_eventPtrs from m_facebookReply
	// . they reference into the reply, so do not free m_facebookReply
	//   until destructor is called

	m_numEvents = 0;

	// scan for <event> tags and set m_evptrs safebuf to each ptr to those
	char *p = content;
	for ( ; *p ; ) {
		// scan to first <event> tag
		p = strstr ( p , "<event>" );
		if ( ! p ) break;
		// store start for ptr
		char *start = p + 7;
		// find end
		char *end = strstr ( p , "</event>" );
		if ( ! end ) break;
		// null term it
		*end = '\0';
		// for next round
		p = end + 8;
		// try to get event id
		char *ep = strstr ( start, "<eid>");
		if ( ! ep ) continue;
		int64_t eid = strtoull ( ep + 5 , NULL , 10 );
		if ( eid == 0 ) continue;
		if ( eid < 0 ) log("facebook: wtf? eid is 0");
		// store it
		if ( ! m_evPtrBuf.pushLong     ( (int32_t)start ) ) return false;
		if ( ! m_evIdsBuf.pushLongLong ( eid         ) ) return false;
		// count them
		m_numEvents++;
	}

	int32_t askedFor = m_eidBuf.length() / 8;
	if ( askedFor != m_numEvents )
		log("facebook: asked for %"INT32" events but got %"INT32"",
		    askedFor,m_numEvents );

	// bail if none!
	if ( m_numEvents <= 0 ) return true;

	// make a new state
	Msg7 *msg7;
	try { msg7 = new (Msg7); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("facebook: inject msg7 new(%i): %s", 
		    sizeof(Msg7),mstrerror(g_errno));
		return true;
	}
	mnew ( msg7, sizeof(Msg7) , "PageInject" );
	// save it for freeing in destructor
	m_msg7 = msg7;

	m_i = 0;

	return doInjectionLoop ( );
}

bool Msgfb::doInjectionLoop ( ) {

	char *coll = g_collectiondb.getColl ( m_collnum );

	char      **eventPtrs = (char **)m_evPtrBuf.getBufStart();
	int64_t  *eventIds  = (int64_t *)m_evIdsBuf.getBufStart();

	for ( ; m_i < m_numEvents ;) {
		// get ptr to it
		char *content    = eventPtrs[m_i];
		int32_t  contentLen = gbstrlen(content);
		// debug thing
		//if ( eventIds[m_i] != 314901535212815LL ) {m_i++; continue; }
		// yoyo tuesdays:
		//if ( eventIds[m_i] != 371365776212884LL ) {m_i++; continue; }
		// latin dance night
		//if ( eventIds[m_i] != 111680095620647LL ) {m_i++; continue; }
		//if (eventIds[m_i] != 273883416016761LL )  {m_i++; continue; }
		// temp thing
		//m_c = content[contentLen];
		//content[contentLen] = '\0';
		// make a fake url
		char url[128];
		sprintf(url,"http://www.facebook.com/events/%"UINT64"",
			eventIds[m_i]);
		// test debug
		if ( g_conf.m_logDebugFacebook )
			log("facebook: %s",content);

		//
		// set m_privacy for event being injected
		//
		char *s = strstr(content,"<privacy");
		// skip that
		if ( s ) s += 8;
		// skip til '>'
		for ( ; s && *s && *s !='>' ; s++ );
		// skip actual >
		if ( s && *s == '>' ) s++;
		// skip whitespace
		for ( ; s && *s && is_wspace_a(*s) ; s++ );
		// compare
		m_privacy = 0;
		if ( ! strncasecmp(s,"secret",6) ) 
			m_privacy = LF_PRIVATE;
		if ( ! strncasecmp(s,"closed",6) ) 
			m_privacy = LF_PRIVATE;

		//if ( eventIds[m_i] == 273883416016761LL ) 
		//	m_privacy = LF_PRIVATE;
		// test
		//m_privacy = LF_PRIVATE;

		// use a forced ip for speed! otherwise it takes forever
		// lookup up www.facebook.com for some reason!!!
		int32_t forcedIp = atoip("69.171.224.39");

		// advance to next event to inject
		m_i++;
		// inject just that
		if ( ! m_msg7->inject ( url ,
					forcedIp,
					content ,
					contentLen ,
					false, // recyclecontent
					CT_XML, // contentType,
					coll ,
					false ,
					NULL, // username
					NULL , // pwd
					MAX_NICENESS, 
					this ,
					addLikesWrapper ) )
			return false;
		// bail on error
		if ( g_errno ) return true;
		// how did this happen? it needs to block... otherwise
		// we have to add to likedb here.
		char *xx=NULL;*xx=0;
	}
	// it did not block, i gues we are done
	//if ( ! doneInjecting ( ) ) return false;
	return true;
}

void doneAddingLikesWrapper ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->doInjectionLoop() ) return;
	// try to read more!!
	if ( mfb->m_numEvents > 0 ) {
		mfb->m_eventStartNum += mfb->m_eventStep;
		if ( ! mfb->downloadEvents() ) return;
	}
	// final save of rec to clear the FB_INQUEUE bit
	if ( ! mfb->doFinalFBRecSave ( ) ) return;
	mfb->m_callback ( mfb->m_state );
}


bool Msgfb::addLikes ( ) { // doneInjecting ( ) {

	// get ptr to it so we can revert the character
	//char *content    = m_eventPtrs[m_i-1];
	//int32_t  contentLen = m_eventLens[m_i-1];
	//content[contentLen] = m_c;

	XmlDoc *xd = &m_msg7->m_xd;

	// if event was not found or added for some reason...
	if ( xd->m_numHashableEvents <= 0 ) {
		// try to do the next one
		if ( m_i < m_numEvents && ! doInjectionLoop ( ) ) return false;
		// all done! no need to loop back up for more, they're done
		return true;
	}

	if ( xd->m_indexCode && xd->m_indexCodeValid ) {
		// note it
		log("facebook: could not index doc: %s",
		    mstrerror(xd->m_indexCode));
		// try to do the next one
		if ( m_i < m_numEvents && ! doInjectionLoop ( ) ) return false;
		// all done! no need to loop back up for more, they're done
		return true;
	}

	// get msg7 reply. it needs to have 

	//
	// ADD LIKES
	//
	// scan the event_members reply we got and cross-reference
	// those facebook eventids with our eventhash/evid/docid guys
	// we got in the injection reply to see if we added the 
	// facebook event to our db. in that case, we also want to add
	// the  maybe/goingto/invitedto/notgoing flags.
	// uses the eventhash64, eventid, docid of event added!
	// returns false with g_errno set on error.
	if ( ! makeLikedbKeyList ( m_msg7 , &m_list3 ) )
		return true;

	// if nothing to add, we are done
	if ( m_list3.getListSize() == 0 ) {
		// try to do the next one
		if ( m_i < m_numEvents && ! doInjectionLoop ( ) ) return false;
		// all done! no need to loop back up for more, they're done
		return true;
	}


	// extract info from state
	//TcpSocket *s = m_msg7->m_socket;
	//int64_t docId  = xd->m_docId;
	//int32_t      hostId = 0;//msg7->m_msg7.m_hostId;

	char *coll = g_collectiondb.getColl ( m_collnum );

	// add that
	if ( ! m_msg1.addList ( &m_list3 ,
				RDB_LIKEDB ,
				coll ,
				this ,
				doneAddingLikesWrapper ,
				false ,
				0 ) ) // niceness
		return false;

	// this might just add to tree in a single server setup so it
	// will not block...
	//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// error must be set!
	return true;
	//return doInjectionLoop ( );
}

// all done with everything!
static void savedFinalFBRecWrapper3 ( void *state ) {
	Msgfb *mfb = (Msgfb *)state;
	mfb->m_callback ( mfb->m_state );
}


// final save of rec to clear the FB_INQUEUE bit
bool Msgfb::doFinalFBRecSave ( ) {
	m_afterSaveCallback = savedFinalFBRecWrapper3;
	log("facebook: saving final rec for fbid=%"INT64"",m_fbId);
	m_fbrecGen.m_flags &= ~FB_INQUEUE;
	m_fbrecGen.m_eventsDownloaded = getTimeGlobal();
	// this calls serializeMsg() which mallocs a new reply to add
	return saveFBRec( &m_fbrecGen );
}

*/

/////////////////////
//
// LIKEDB
//
//////////////////////


void Likedb::reset() {
	m_rdb.reset();
}

bool Likedb::init ( ) {

	int64_t uid = 123456789123LL;
	int64_t docId = 999888777666LL;
	int32_t eventId = 12345;
	int32_t rsvp_status = LF_GOING;//|LF_HIDE;
	int32_t start_time = 6543210;
	uint64_t eventHash64 = 9999997398453LL;
	uint32_t eventHash32 = (uint32_t)eventHash64;
	int32_t value = 999888;
	char *recs = g_likedb.makeRecs ( uid         ,
					 docId       ,
					 eventId   ,
					 start_time  ,
					 rsvp_status ,
					 eventHash64 ,
					 value );
	char *p = recs;
	int64_t uid2 = g_likedb.getUserIdFromRec ( p );
	if ( uid2 != uid ) { char *xx=NULL;*xx=0; }
	int32_t flags = g_likedb.getFlagsFromRec ( p );
	if ( flags != rsvp_status ) { char *xx=NULL;*xx=0; }
	uint32_t eh = g_likedb.getEventHash32FromRec ( p );
	if ( eh != eventHash32 ) { char *xx=NULL;*xx=0; }
	if ( g_likedb.getValueFromRec ( p ) != value ) { char *xx=NULL;*xx=0;}

	p += LIKEDB_RECSIZE;
	uid2 = g_likedb.getUserIdFromRec ( p );
	if ( uid2 != uid ) { char *xx=NULL;*xx=0; }
	flags = g_likedb.getFlagsFromRec ( p );
	eh = g_likedb.getEventHash32FromRec ( p );
	if ( eh != eventHash32 ) { char *xx=NULL;*xx=0; }
	if ( flags != rsvp_status ) { char *xx=NULL;*xx=0; }


	// . what's max # of tree nodes?
	// . NOTE: 32 bytes of the 82 are overhead
	int32_t maxMem = 50000000;
	int32_t maxTreeNodes = maxMem / 48;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir               ,
			    "likedb"                     ,
			    true                       , // dedup same keys?
			    LIKEDB_DATASIZE , // fixed record size
			    2,//g_conf.m_tagdbMinFilesToMerge   ,
			    maxMem, // 5MB g_conf.m_tagdbMaxTreeMem  ,
			    maxTreeNodes               ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			    NULL , // pagecache - tree only!
			    false,  // is titledb
			    false,  // preload disk page cache
			    sizeof(key192_t),     // key size
			    false ); // bias disk page cache?
}

bool Likedb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	return true;
}

// FIRST REC KEY:
//
// key192_t:
// uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu 
// uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu u=uid (fb userid)
// dddddddd dddddddd dddddddd dddddddd d=docid
// dddddd00 00000000 eeeeeeee eeeeeeee e=gbeventid D=delbit
// ssssssss ssssssss ssssssss ssssssss start_time
// ffffffff ffffffff ffffffff ffffff0D flags (LF_HIDE,etc.)

// SECOND REC KEY:
//
// key192_t:
// dddddddd dddddddd dddddddd dddddddd d=docid
// dddddd00 00000000 eeeeeeee eeeeeeee e=gbeventid
// uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu u=uid (fb userid)
// uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu
// ssssssss ssssssss ssssssss ssssssss start_time
// ffffffff ffffffff ffffffff ffffff1D flags (LF_HIDE,etc.)


// data:
// hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh eventhash32 
// vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv v=value of flag or facebook event id
// vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv
//
// NOTE: we set "v" to facebook event id for the LF_ADDEDTOFACEBOOK flag
//


int32_t Likedb::getEventIdFromRec ( void *rec ) {
	key192_t *k = (key192_t *)rec;
	if ( k->n0 & LF_TYPEBIT )
		return k->n2 & 0xffff;
	return k->n1 & 0xffff;
}

void Likedb::setEventId ( char *rec , int32_t eventId ) {
	key192_t *k = (key192_t *)rec;
	// sanbity
	if ( eventId & 0xffff0000 ) { char *xx=NULL;*xx=0;}
	// is it a SECOND REC?
	if ( k->n0 & LF_TYPEBIT ) {
		// clear out old event id bits
		k->n2 &= 0xffffffffffff0000LL;
		// or in new, shifted up 1 for delbit
		k->n2 |= eventId;
	}
	else {
		// clear out old event id bits
		k->n1 &= 0xffffffffffff0000LL;
		// or in new, shifted up 1 for delbit
		k->n1 |= eventId;
	}
}

// use our docid/eventid because that is what we use in datedb when
// doing a search. the docid and eventid should be returned by the msg7
// inject reply.
char *Likedb::makeRecs ( int64_t  uid         ,
			 int64_t  docId       ,
			 int32_t       eventId     ,
			 int32_t       start_time  ,
			 int32_t       rsvp_status ,
			 uint64_t eventHash64 ,
			 int64_t  value       ) {
	// sanity
	if ( rsvp_status & LF_TYPEBIT ) { char *xx=NULL;*xx=0; }
	if ( rsvp_status & LF_DELBIT  ) { char *xx=NULL;*xx=0; }
	// only one flag can be set!!!
	int32_t ignore = 0;
	ignore |= LF_DELBIT;
	ignore |= LF_TYPEBIT;
	ignore |= LF_ISEVENTGURUID;
	ignore |= LF_FROMFACEBOOK;
	if ( getNumBitsOn32(rsvp_status & ~ignore)!=1) { char *xx=NULL;*xx=0;}

	if ( docId   < 0      ) { char *xx=NULL;*xx=0; }
	if ( eventId > 0xffff ) { char *xx=NULL;*xx=0; }
	if ( eventId < 0      ) { char *xx=NULL;*xx=0; }
	// the record
	static char s_buf[2*LIKEDB_RECSIZE];
	// store a 16 byte key first
	key192_t k;
	// the destination ptr
	char *p = s_buf;

	uint32_t eventHash32 = (uint32_t)eventHash64;

	//log("facebook: adding eventhash64 = %"UINT64"",eventHash64 );
	//log("facebook: adding eventhash32 = %"UINT32"",eventHash32 );

	//
	// make the first type of rec
	//

	// . this is the facebook userid OR the eventGuruId
	// . can also be the userid we assign someone i guess on our end
	k.n2 = uid;
	// reset
	k.n1 = 0;
	// then docid
	k.n1 |= docId;
	// then 10 zero bits
	k.n1 <<= 10;
	// make room for event id
	k.n1 <<= 16;
	k.n1 |= eventId;
	// starttime
	k.n0 = start_time;
	k.n0 <<= 32;
	k.n0 |= rsvp_status;
	// we are a positive key!
	k.n0 |= LF_DELBIT;
	// store that
	*(key192_t *)p = k;
	// skip key
	p += sizeof(key192_t);
	// then 4 byte data
	*(int32_t *)p = eventHash32;
	p += 4;
	// then the value
	*(int64_t *)p = value;
	p += 8;


	//
	// now make the 2nd rec
	//
	k.n2 = k.n1;
	k.n1 = uid;
	// this is the 2nd type of rec, so set this bit
	k.n0 |= LF_TYPEBIT;
	// store second key
	*(key192_t *)p = k;
	// skip second key
	p += sizeof(key192_t);
	// now this
	*(int32_t *)p = eventHash32;
	p += 4;
	// then the value
	*(int64_t *)p = value;
	p += 8;
	
	return s_buf;
}

// make a "type 2" key (docid leads)
key192_t Likedb::makeStartKey ( int64_t docId, int32_t eventId ) {
	key192_t k;
	// reset
	k.n2 = docId;
	// then 10 zero bits
	k.n2 <<= 10;
	// make room for event id
	k.n2 <<= 16;
	k.n2 |= eventId;
	// any user id
	k.n1 = 0LL;
	// any starttime or flags
	k.n0 = 0LL;
	return k;
}

// make a "type 2" key (docid leads)
key192_t Likedb::makeEndKey ( int64_t docId, int32_t eventId ) {
	key192_t k;
	// reset
	k.n2 = docId;
	// then 10 zero bits
	k.n2 <<= 10;
	// make room for event id
	k.n2 <<= 16;
	k.n2 |= eventId;
	// max user id
	k.n1 = 0xffffffffffffffffLL;
	// max starttime and flags
	k.n0 = 0xffffffffffffffffLL;
	return k;
}

int64_t Likedb::getUserIdFromRec ( void *rec ) {
	key192_t *k = (key192_t *)rec;
	if ( k->n0 & LF_TYPEBIT ) return k->n1;
	return k->n2;
}

int64_t Likedb::getDocIdFromRec ( char *rec ) {
	key192_t *k = (key192_t *)rec;
	if ( k->n0 & LF_TYPEBIT ) return k->n2 >> 26;
	return k->n1 >> 26;
}

key192_t Likedb::makeStartKey2 ( int64_t uid ) {
	key192_t k;
	k.n2 = uid;
	k.n1 = 0;
	k.n0 = 0;
	return k;
}

// . a facebook user uses likedb to tag an event rsvp_status
// . similar to facebook's event_members table, but uses our technology
// . use Rdb, but prohibit from dumping to disk! must always be in tree.
// . add this to likedb (or remove if "neg" is true.
bool Msgfc::addLikedbTag ( int64_t userId ,
			   int64_t docId,
			   int32_t      gbeventId,
			   uint64_t eventHash64 ,
			   int32_t start_time,
			   int32_t rsvp , // LF_* #define's above
			   bool negative , // turn off that flag?
			   char *coll,
			   void *state ,
			   void (* callback)(void *) ) {

	if ( userId < 0 ) { char *xx=NULL;*xx=0; }

	

	char *p = m_recs;
	int32_t size = (int32_t)LIKEDB_RECSIZE*2;
	int32_t count = 0;
	//int32_t eventHash32 = (int32_t)((uint64_t)eventHash64);
	int64_t value = 1LL;
	if ( negative ) value = 0LL;

	//if ( eventHash == 0 || eventHash32 == -1 ) { char *xx=NULL;*xx=0;}

	// sanity
	int32_t ignore;
	ignore |= LF_DELBIT;
	ignore |= LF_TYPEBIT;
	ignore |= LF_ISEVENTGURUID;
	ignore |= LF_FROMFACEBOOK;
	if ( getNumBitsOn32 ( rsvp & ~ignore ) != 1 ) { char *xx=NULL;*xx=0; }

	// only use start_time for "going". everything else tags all
	// occurences of the event: like,hide,accept,reject,invited.
	// otherwise if you like an event you have to unlike the exact
	// same occurence to turn the flags off!
	if ( ! ( rsvp & LF_GOING ) ) start_time = 0;

	// . this makes two recs to add to likedb
	// . the first one starts with uid in the key so it will be 
	//   used to find events that your friends are 
	// . the second one starts with docid/evid so it can be used
	//   to lookup who likes an event when we generate the summary
	//   for the event because its in the search results
	char *recs = g_likedb.makeRecs ( userId ,
					 docId             ,
					 gbeventId         ,
					 start_time ,
					 rsvp       ,
					 eventHash64     ,
					 value );
	// add to list otherwise
	gbmemcpy ( p , recs , size );
	p += size;
	count += 2;
	
	// sort for rdblist
	gbqsort ( m_recs , count , (int32_t)LIKEDB_RECSIZE, likedbCmp );

	// use the list we got
	key192_t startKey;
	key192_t endKey;
	startKey.setMin();
	endKey.setMax();
	// that is our list
	m_list6.set ( m_recs ,
		      p - m_recs , // listsize
		      NULL, // alloc
		      0 , // allocSize
		      (char *)&startKey ,
		      (char *)&endKey  ,
		      LIKEDB_DATASIZE , // fixed datasize
		      true , // own data? yes, free it when done
		      false , // use half keys? no.
		      LIKEDB_KEYSIZE );
	// add that
	if ( ! m_msg1.addList ( &m_list6 ,
				RDB_LIKEDB ,
				coll ,
				state, // this ,
				callback, //doneAddingLikedbTagWrapper ,
				false ,
				0 ) ) // niceness
		return false;

	return true;
}

int32_t Likedb::getUserFlags ( int64_t userId , 
			    int32_t start_time ,
			    char *list, 
			    int32_t listSize ) {
	// bail if not valid user id
	if ( userId == 0LL ) return 0;
	// scan
	char *p     = list;
	char *pend  = list + listSize;
	int32_t  flags = 0;
	for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
		// check for matching userid
		int64_t uid = g_likedb.getUserIdFromRec ( p );
		if ( uid != userId ) continue;
		// got it
		int32_t ff = g_likedb.getFlagsFromRec ( p );
		// get value
		int64_t val = g_likedb.getValueFromRec ( p );
		// skip if 0, that means unset!
		if ( ! val ) continue;
		// restrict to just this instance of its a "GOING" flag
		if ( ff & LF_GOING ) {
			int32_t start = g_likedb.getStartTimeFromRec ( p );
			if ( start&&start_time && start!=start_time) continue;
		}
		// keep tabs
		flags |= ff;
	}
	return flags;
}

int32_t Likedb::getPositiveFlagsFromRec  ( char *rec ) {
	if ( ! g_likedb.getValueFromRec ( rec ) ) return 0;
	int32_t flags = (*(int32_t *)rec) & ~(LF_DELBIT|LF_TYPEBIT);
	return flags;
}

char *Likedb::getRecFromLikedbList ( int64_t userId ,
				     int32_t start_time ,
				     int32_t flags , 
				     char *list ,
				     int32_t  listSize ) {
	// bail if not valid user id
	if ( userId == 0LL ) return NULL;
	// scan
	char *p     = list;
	char *pend  = list + listSize;
	for ( ; p < pend ; p += LIKEDB_RECSIZE ) {
		// check for matching userid
		int64_t uid = g_likedb.getUserIdFromRec ( p );
		if ( uid != userId ) continue;
		// got it
		int32_t ff = g_likedb.getFlagsFromRec ( p );
		// must match
		if ( ! ( ff & flags) ) continue;
		// get value
		int64_t val = g_likedb.getValueFromRec ( p );
		// skip if 0, that means unset!
		if ( ! val ) continue;
		// restrict to just this instance of its a "GOING" flag
		if ( flags & LF_GOING ) {
			int32_t start = g_likedb.getStartTimeFromRec ( p );
			if ( start&&start_time && start!=start_time) continue;
		}
		return p;
	}
	return NULL;
}


// http://developers.facebook.com/docs/reference/rest/events.create/
// http://developers.facebook.com/docs/reference/api/event/

// scan over all events that have someone going to them or maybe going
// and make sure facebook has that status. 
// therefore maybe add LF_EGMAYBE and LF_FBMAYBE as separate flags
// so we know if the bit can from facebook or not. once we update facebook
// with the event then we can set LF_FBMAYBE or LF_FBGOING for that user
// assuming they have a facebook id. also, if we initially use their 
// eventguruid and they tag an event then later they log in and we get
// their facebookid, we have to make sure to update the event on facebook
// to reflect they are unsure/goingtoit.

// also, if an event has LF_ACCEPTED we should upload it to facebook under
// our appid.

// so, let's be perpetually scanning likedb to do this...
// maybe just host #0 should do it to avoid slamming facebook?

/*
//////////////////////////
//
//
// ADD EVENT TO FACEBOOK
//
//
//////////////////////////

bool Msgfb::addEventToFacebook ( char *title ,
				 char *desc  ,
				 int32_t  start_time ,
				 int32_t  end_time ,
				 void *state ,
				 void (* callback)(void *state) ,
				 int32_t niceness ) {

	// how do we get the accesstoken for the app? must have to pass
	// in our secret somehow.
	if ( ! getAppAccessToken ( ) ) return false;

	return gotAppAccessToken();
}

static void addedFBEventWrapper ( void *state , TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->addedFBEvent ( s ) ) return;
	mfb->m_callback ( mfb->m_state );
}

bool Msgfb::gotAppAccessToken ( ) {

	SafeBuf args;
	args.safePrintf("name=");
	args.urlEncode( title );
	args.safePrintf("&description=");
	args.urlEncode( description );
	args.safePrintf("&start_time=%"UINT32""
			"&end_time=%"UINT32""
			"&latitude=%.07f"
			"&longitude=%.07f"
			, start_time
			, end_time 
			, latitude
			, longitude 
			);

	SafeBuf purl;
	purl.safePrintf("https://graph.facebook.com/%s/events?"
			"access_token=%s&"
			"%s"
			, APPID
			, m_appAccessToken
			, args.getBufStart() );

	// reset
	g_errno = 0;
	// . get the results
	// . TODO: make sure post puts the args in the post section
	if ( ! g_httpServer.getDoc ( purl.getBufStart() ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this               , // state
				     addedFBEventWrapper , // callback
				     40*1000      , // 40 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     30000000 , // maxTextDocLen   ,
				     30000000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ,
				     "HTTP/1.0",
				     true ) ) // doPost?
		// return false if blocked
		return false;
	
	// otherwise, somehow got it without blocking... wtf?
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	log("facebook: error adding event: %s",mstrerror(g_errno));
	return true;
}

bool Msgfb::addedFBEvent ( TcpSocket *s ) {

	// get the event id from reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	char *s = strstr ( reply , "id" );
	if ( ! s ) {
		log("facebook: add event reply had no eid");
		g_errno = EBADREPLY;
		return true;
	}

	// get it otherwise
	char *p = s;
	for ( ; *p && !is_digit(*p) ; p++ );
	if ( ! is_digit ( *p ) ) {
		log("facebook: add event reply had no eid 2");
		g_errno = EBADREPLY;
		return true;
	}

	int64_t eid = strtoull(p,NULL,10);

	// add it to likedb as being in facebook now!
	char flags = LF_ADDEDTOFACEBOOK;

	if ( ! m_msgfc.addLikedbTag ( 0 , // eventGuruId,
				      APPID , // facebookId,
				      m_docIdToAdd,
				      m_eventIdToAdd,
				      m_eventHash64ToAdd,
				      m_start_timeToAdd
				      flags , // LF_* #define's above
				      false, // negative - turn off that flag?
				      m_coll ,
				      this ,
				      addedLikedbTag2 ) )
		return false;

	
	




}

///////////////////////////
//
//
// GET APP ACCESS TOKEN
//
//
///////////////////////////

static char s_appAccessToken[256];
static bool s_appAccessTokenValid = false;
static bool s_inProgress = false;

static void gotAppAccessTokenWrapper ( void *state , TcpSocket *s ) {
	Msgfb *mfb = (Msgfb *)state;
	if ( ! mfb->gotAppAccessToken ( s ) ) return;
	mfb->m_callback ( mfb->m_state );
}

bool Msgfb::getAppAccessToken ( ) {

	if ( s_appAccessTokenValid ) {
		strcpy ( m_appAccessToken , s_appAccessToken );
		return true;
	}

	// must not be in progress
	if ( s_inProgress ) { char *xx=NULL;*xx=0; }

	s_inProgress = true;

	// use code to get access token
	// that calls https://graph.facebook.com/oauth/access_token?
	// client_id=YOUR_APP_ID&redirect_uri=YOUR_URL&
	// client_secret=YOUR_APP_SECRET
	char fburl[1024];
	sprintf(fburl,
		"https://graph.facebook.com/oauth/access_token?"
		"client_id=%s&"
		"client_secret=%s&"
		"grant_type=client_credentials"
		, APPID
		, APPSECRET
		);
	// reset
	g_errno = 0;
	if ( ! g_httpServer.getDoc ( fburl ,
				     0 , // urlIp
				     0                    , // offset
				     -1                   ,
				     0 , // ifModifiedSince ,
				     this                 , // state
				     gotAppAccessTokenWrapper , // callback
				     10*1000      , // 10 sec timeout
				     0 , // proxyip
				     0 , // proxyport
				     10000 , // maxTextDocLen   ,
				     10000 , // maxOtherDocLen  ,
				     g_conf.m_spiderUserAgent    ) )
		// return false if blocked
		return false;
	// error?
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	// all done
	s_inProgress = false;
	// let caller know we did not block
	return gotAppAccessToken ( NULL );
}

bool Msgfb::gotAppAccessToken ( TcpSocket *s ) {

	// all done
	s_inProgress = false;

	// some kind of error?
	if ( g_errno ) {
		log("facebook: error launching read of app access token: %s", 
		    mstrerror(g_errno));
		return;
	}

	// the access token should be in the reply
	char *reply     = s->m_readBuf;
	int32_t  replySize = s->m_readOffset;

	// mime error?
	HttpMime mime;
	// exclude the \0 i guess. use NULL for url.
	mime.set ( reply, replySize - 1, NULL );
	// not good?
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 ) {
		log("facebook: bad app access request http status = %"INT32"",
		    httpStatus );
		g_errno = EBADREPLY;
		return;
	}

	// point to content
	char *content = reply + mime.getMimeLen();

	// assume no accesstoken provided
	s_appAccessToken[0] = '\0';

	// look for access token
	//sscanf(content,"access_token=%s&expires=%"INT32"",m_accessToken,&expires);
	char *at = strstr(content,"access_token=");
	if ( at ) {
		char *p = at + 13;
		char *start = p;
		for ( ; *p && *p != '&' ;p++ );
		int32_t len = p - start;
		if ( len > MAX_TOKEN_LEN ) { char *xx=NULL;*xx=0; }
		gbmemcpy ( s_appAccessToken , start , len );
		s_appAccessToken [ len ] = '\0';
	}

	// error?
	if ( ! s_appAccessToken[0] ) {
		log("facebook: could not find app access token");
		g_errno = EBADREPLY;
		return;
	}

	// sanity
	if ( gbstrlen(m_accessToken) > MAX_TOKEN_LEN ) { char *xx=NULL;*xx=0;}

	// set this timestamp
	//m_accessTokenCreated = getTimeGlobal();

	s_appAccessTokenValid = true;

	strcpy ( m_appAccessToken , s_appAccessToken );

	return true;
}





// we should also have a spiderloop to continually search for and find
// events on facebook and add them to our db
*/



///////////////////////
//
//
// THE EMAILER
//
// . g_emailer is in Process.cpp and so is its 60 second sleep callback
//
///////////////////////

// . some sleepwrapper should call this once every 10 seconds or so
bool Emailer::emailEntryLoop ( ) {

	// temporarily disable
	return true;

	// skip if in progress already
	if ( m_emailInProgress ) return true;

	// wait for clock to be in sync
	if ( ! isClockInSync() ) return true;

	int32_t now = getTimeGlobal();
	if ( m_lastEmailLoop && now - m_lastEmailLoop < 60 ) return true;

	// just use first event collection
	CollectionRec *cr = NULL;
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		if ( ! cr->m_indexEventsOnly ) { cr = NULL; continue; }
		break;
	}
	if ( ! cr ) return true;

	// lock it out
	m_emailInProgress = true;

	// we are doing a scan loop, not sending a single email
	//m_sendSingleEmail = false;

	// save this
	m_coll = cr->m_coll;
	m_collnum = g_collectiondb.getCollnum ( m_coll );

	// . make sure m_emailTree is full
	// . this returns false if blocked, true otherwise
	// . it should call emailScan() when done if it blocks
	if ( ! populateEmailTree ( ) ) return false;

	// do the scan loop
	return emailScan( NULL );
}

// . ok, now m_emailTree should be fully populated
// . returns false if blocked, true otherwise
bool Emailer::emailScan ( EmailState *es ) {

	if ( es && es->m_sendSingleEmail ) {
		es->m_singleCallback ( es->m_singleState );
		return true;
	}

	log("emailer: scanning fbids");

 loop:
	int32_t now = getTimeGlobal();
	// scan for the fbids in the email tree
	int32_t n = m_emailTree.getFirstNode();
	// get the key if n is good
	key96_t *kp = NULL;
	if ( n >= 0 ) kp = (key96_t *)m_emailTree.getKey(n);
	// check time. stop scanning if in the future!
	if ( kp && kp->n1 > (uint32_t)now ) n = -1;
	// none remain?
	if ( n < 0 ) {
		// return false if waiting for email replies
		if ( m_emailRequests > m_emailReplies ) return false;
		// clear this so we can run again later
		m_emailInProgress = false;
		// i guess update this to the completion time
		int32_t now = getTimeGlobal();
		m_lastEmailLoop = now;
		return true;
	}
	// save id
	int64_t fbId = kp->n0;
	// nuke that node
	m_emailTree.deleteNode ( n , true );
	// . ok launch an email. pass in the facebook id
	// . returns false if blocked, true otherwise
	launchEmail ( fbId );
	// . if we hit the outstanding limit, "block"
	// . when an email reply comes it it should re-call
	//   emailScan()... and it should remove itself from
	//   m_emailTree so we do not re-do it!
	if ( m_emailRequests - m_emailReplies >= MAX_OUTSTANDING_EMAILS )
		return false;
	// do another one!
	goto loop;
}

//static void generateEventsEmailWrapper ( void *state ) {
//	Emailer *em = (Emailer *)state;
//	if ( ! em->generateEventsEmail( ) ) return;
//}

// from PageEvents.cpp:
extern bool sendPageEvents2 ( TcpSocket *s ,
			      HttpRequest *hr ,
			      SafeBuf *resultsBuf,
			      SafeBuf *emailLikedbListBuf,
			      void *state  ,
			      void (* emailCallback)(void *state) ,
			      SafeBuf *providedBuf ,
			      void *providedState  ,
			      void (* providedCallback)(void *state) );

static void gotPageToEmailWrapper ( void *state ) {
	EmailState *es = (EmailState *)state;
	Emailer *em = es->m_emailer;
	if ( ! em->getMailServerIP( es ) ) return;
	// scan next
	em->emailScan( es );
}

bool Emailer::launchEmail ( int64_t fbId ) {

	// we need an email state now!
	EmailState *es = NULL;
	for ( int32_t i = 0 ; i < MAX_OUTSTANDING_EMAILS ; i++ ) {
		if ( m_emailStates[i].m_inUse ) continue;
		es = &m_emailStates[i];
		break;
	}
	// how can this happen?
	if ( ! es ) { char *xx=NULL;*xx=0; }

	// make a fake http request
	//SafeBuf hrsb;

	//return true;
	
	// must remain on stack since the copied HttpRequest will point into
	// this or the SearchInput will point into this
	es->m_hrsb.safePrintf("GET /?"
			      "c=%s&"
			      "showpersonal=1&"
			      //"where=anywhere&"
			      // this should override the fbid in the cookie
			      "usefbid=%"INT64"&"
			      "fh=%"UINT32"&"
			      "usecookie=0&"
			      "map=0&"
			      "n=25&"
			      "emailformat=1"
			      " HTTP/1.0\r\n\r\n"
			      , m_coll
			      , fbId
			      , hash32((char *)&fbId,8)
			      );

	HttpRequest hr;
	hr.set (es->m_hrsb.getBufStart(), 
		es->m_hrsb.length() , 
		(TcpSocket *)NULL );

	TcpSocket *s = NULL;

	// two counts
	m_emailRequests++;

	// claim it
	es->m_inUse = true;
	// point to emailer
	es->m_emailer = this;
	// who are we sending to?
	es->m_fbId = fbId;
	// container class
	es->m_emailer = this;
	// reset this
	es->m_errno = 0;
	// our collection
	es->m_coll = m_coll;
	// we are doing a loop! so return to emailScan() function
	es->m_sendSingleEmail = false;
	// clear these
	es->m_emailResultsBuf   .purge();
	es->m_emailLikedbListBuf.purge();

	// . use that to generate the search results
	// . returns false if blocked, true otherwise
	if ( ! sendPageEvents2 ( s , 
				 &hr ,  // this is copied right away!
				 // our special parms:
				 &es->m_emailResultsBuf,
				 &es->m_emailLikedbListBuf,
				 es ,
				 gotPageToEmailWrapper ,
				 NULL ,
				 NULL ,
				 NULL ) ) 
		return false;
	// we got it
	return getMailServerIP ( es );
}

static void gotMXIpWrapper ( void *state , int32_t ip ) {
	EmailState *es = (EmailState *)state;
	Emailer *em = es->m_emailer;
	if ( ! em->gotMXIp ( es ) ) return;
	// scan next
	em->emailScan( es );
}


// . PageEvents.cpp should have stored the html content into m_emailResultsBuf
// . so email that to the recipient
bool Emailer::getMailServerIP ( EmailState *es ) {

	//
	// if no results, skip it the next couple functions
	//
	if ( es->m_emailResultsBuf.length() == 0 ) {
		log("emailer: no results for user %"UINT64"",es->m_fbId);
		return gotEmailReply ( es , NULL );
	}

	// get email address from the msg
	char *rb = es->m_emailResultsBuf.getBufStart();

	char *emailTo = NULL;
	if ( rb ) emailTo = strstr(rb,"RCPT To:<");
	// bail on error
	if ( ! emailTo ) {
		log("emailer: no email address for %"UINT64"",es->m_fbId);
		es->m_emailResultsBuf.purge();
		es->m_emailLikedbListBuf.purge();
		es->m_inUse = false;
		m_emailReplies++;
		return true;
	}

	// get domain from that
	char *p = emailTo;
	char *pend = p + 256;
	for ( ; *p && *p != '@' && p < pend ; p++ ) ;
	if ( p >= pend || ! *p ) {
		log("emailer: no at sign in email address "
		    "for %"UINT64"",es->m_fbId);
		es->m_emailResultsBuf.purge();
		es->m_emailLikedbListBuf.purge();
		es->m_inUse = false;
		m_emailReplies++;
		return true;
	}

	// skip over '@' sign
	p++;
	// set domain
	char *dom = p;
	// scan domain length
	for ( ; *p && *p != '>' && p < pend ; p++ ) ;
	int32_t domLen = p - dom;
	if ( p >= pend || ! *p || domLen > 80 ) {
		log("emailer: no valid subdomain in email address "
		    "for %"UINT64"",es->m_fbId);
		es->m_emailResultsBuf.purge();
		es->m_emailLikedbListBuf.purge();
		es->m_inUse = false;
		m_emailReplies++;
		return true;
	}
	
	
	// get the ip. use kinda a fake hostname to pass into MsgC
	// so that it understands its a special MX record lookup
	char *dst = es->m_emailSubdomain;
	gbmemcpy ( dst , "gbmxrec-" , 8 );
	dst += 8;
	gbmemcpy ( dst , dom , domLen );
	dst += domLen;
	*dst = '\0';

	// . now get the ip for that. get the MX record IP!!!
	// . it will recognize the gbmxrec- prepension and ask for the
	//   MX record
	if ( ! es->m_msgc.getIp ( es->m_emailSubdomain ,
				  dst - es->m_emailSubdomain ,
				  &es->m_ip ,
				  es ,
				  gotMXIpWrapper ) )
		return false;

	return gotMXIp ( es );
}

static void gotEmailReplyWrapper ( void *state , TcpSocket *s ) {
	EmailState *es = (EmailState *)state;
	Emailer *em = es->m_emailer;
	if ( ! em->gotEmailReply ( es , s ) ) return;
	// scan next
	em->emailScan( es );
}

bool Emailer::gotMXIp ( EmailState *es ) {

	log("facebook: got mx ip of %s for %s",
	    iptoa(es->m_ip), es->m_emailSubdomain );

	// our problem? like ENOME?
	if ( g_errno ) {
		log("emailer: had server side error getting ip: %s",
		    mstrerror(g_errno));
		es->m_errno = g_errno;
		return gotEmailReply ( es , NULL );
	}

	// int16_tcut
	int32_t ip = es->m_ip;//msgc.getIp();
	// problem?
	if ( ip == 0 || ip == -1 ) {
		log("emailer: bad ip of %"INT32" for %s for %"UINT64"",
		    ip,
		    es->m_emailSubdomain,
		    es->m_fbId);
		es->m_errno = EBADIP;
		g_errno     = EBADIP;
		return gotEmailReply ( es , NULL );
	}

	// send the message
	TcpServer *ts = g_httpServer.getTcp();
	// log it
	log ( LOG_WARN, "emailer: Sending email to %"UINT64" size=%"INT32"", 
	      es->m_fbId , es->m_emailResultsBuf.length());

	/*
	//
	// THIS ONE WORKS so work backwards from here if you have issues
	//
	SafeBuf *eb = &es->m_emailResultsBuf;
	eb->reset();
	eb->safePrintf(
		       "EHLO gigablast.com\r\n"
		       "Mail From:<mwells2@gigablast.com>\r\n"
		       "RCPT To:<app+a4gdq01pp8.2qufhgd443.c6277eee23cba81f44f0decbcb1a4d03@proxymail.facebook.com>\r\n"
		       "DATA\r\n"
		       "From: mwells <mwells2@gigablast.com>\r\n"
		       "MIME-Version: 1.0\r\n"
		       "To: app+a4gdq01pp8.2qufhgd443.c6277eee23cba81f44f0decbcb1a4d03@proxymail.facebook.com\r\n"
		       "Subject: testing\r\n"
		       "Content-Type: text/html; charset=UTF-8; format=flowed\r\n"
		       "Content-Transfer-Encoding: 8bit\r\n"
		       "\r\n"
		       "\r\n"
		       "<table cellpadding=3 cellspacing=0></table>\r\n"
		       ".\r\n"
		       "QUIT\r\n"
		       );
	*/

	//
	// debug by dumping to file!!!
	//
	char filename[512];
	int32_t now = getTimeLocal();
	sprintf ( filename,"html/email/email.%"UINT64".%"UINT32""
		  , es->m_fbId
		  , now
		  );
	es->m_emailResultsBuf.save(g_hostdb.m_dir,filename);
	log("facebook: saving email %s", filename);
	SafeBuf embuf;
	embuf.load(g_hostdb.m_dir,"html/email/email.html");
	embuf.safePrintf("<a href=/email/email.%"UINT64".%"UINT32">email.%"UINT64".%"UINT32"</a><br>"
			 , es->m_fbId
			 , now
			 , es->m_fbId
			 , now
			 );
	embuf.save(g_hostdb.m_dir,"html/email/email.html");

	log("facebook: emailing %"INT32" bytes",
	    es->m_emailResultsBuf.length() );


	//
	// skip actual email for now!
	//
	//gotEmailReply( es , NULL );
	//if ( ! es->m_sendSingleEmail ) return true;

	if ( ! ts->sendMsg ( ip,
			     25, // smtp (send mail transfer protocol) port
			     es->m_emailResultsBuf.getBufStart(),
			     es->m_emailResultsBuf.length(),
			     es->m_emailResultsBuf.length(),
			     es->m_emailResultsBuf.length(),
			     es,
			     gotEmailReplyWrapper,
			     60*1000,
			     1000*1024,
			     1000*1024 ) )
		return false;
	// we did not block, so update facebook rec with timestamps
	gotEmailReply( es , NULL );
	// we did not block
	return true;
}

static void gotRecWrapper3 ( void *state ) {
	EmailState *es = (EmailState *)state;
	Emailer *em = es->m_emailer;
	if ( ! em->gotRec3 ( es ) ) return;
	// scan next
	em->emailScan( es );
}
	

bool Emailer::gotEmailReply ( EmailState *es , TcpSocket *s ) {

	// don't free it that's our job!
	if ( s ) s->m_sendBuf = NULL;

	// free allocated memory
	es->m_emailResultsBuf.purge();

	if ( g_errno ) { 
		log("emailer: got error sending to fbid=%"INT64": %s",es->m_fbId,
		    mstrerror(g_errno));
		es->m_errno = g_errno;
		// reset these errors just in case
		g_errno = 0;
	}
	// . show the reply
	// . seems to crash if we log the read buffer... no \0?
	if ( s && s->m_readBuf )
		log("emailer: got email server reply: %s", s->m_readBuf );
	else
		log("emailer: missing email server reply!");


	log("emailer: getting fbrec for fbid=%"INT64"",es->m_fbId);

	// load the facebookdb rec so we can update it and save it then
	key96_t startKey;
	key96_t endKey;
	startKey.n1 = 0;
	startKey.n0 = es->m_fbId;
	endKey.n1 = 0;
	endKey.n0 = es->m_fbId;
	startKey.n0 <<= 1;
	endKey.n0 <<= 1;
	endKey.n0 |= 0x01;
	if ( ! m_msg0.getList ( -1, // hostid
				0 , // ip
				0 , // port
				0 , // maxcacheage
				false, // addtocache
				RDB_FACEBOOKDB,
				"",//m_coll,
				&es->m_list9,
				(char *)&startKey,
				(char *)&endKey,
				11, // minrecsizes
				es, // this ,
				gotRecWrapper3,
				MAX_NICENESS ) )
		return false;
	// i guess we got it without blocking
	return gotRec3 ( es );
}

static void savedUpdatedRecWrapper ( void *state ) {
	EmailState *es = (EmailState *)state;
	Emailer *em = es->m_emailer;
	if ( ! em->savedUpdatedRec ( es ) ) return;
	// scan next
	em->emailScan( es );
}


bool Emailer::gotRec3 ( EmailState *es ) {

	// error loading?
	if ( g_errno ) {
		log("emailer: error loading facebookdb rec for %"UINT64"",
		    es->m_fbId );
		es->m_errno = g_errno;
	}


	// empty is bad
	if ( es->m_list9.getListSize() <= 0 ) {
		log("emailer: facebookdb rec is empty. wtf? fbid=%"INT64"",
		    es->m_fbId);
		es->m_errno = EBADREPLY;
	}

	// get the facebook rec... why?
	FBRec *rec = (FBRec *)es->m_list9.getList();

	// assume no error
	if ( ! es->m_errno ) rec->m_nextRetry = 0;

	int32_t now = getTimeGlobal();

	// on error...
	if ( es->m_errno ) {
		// did we have a previous attempt?
		int32_t elapsed = now - rec->m_lastEmailAttempt ;
		// our first time? set to 6 hours retry then.
		if ( rec->m_nextRetry == 0 ) elapsed = 0;
		// ok, add 3 hours and double that
		int32_t wait = 2 * (elapsed + 3*3600);
		// store that then
		rec->m_nextRetry = now + wait;
	}

	// update the last send time
	rec->m_lastEmailAttempt = now;

	// . add the facebookdb rec back now with updated times
	// . just use TagRec::m_msg1 now
	// . no, can't use that because tags are added using SafeBuf::addTag()
	//   which first pushes the rdbid, so we gotta use msg4
	// . if a host is down we have to fix msg1 (and msg4) so they both
	//   just write to a file until that host comes back up.
	if ( ! es->m_msg1.addList ( &es->m_list9 ,
				    RDB_FACEBOOKDB ,
				    "none",//m_coll ,
				    es ,
				    savedUpdatedRecWrapper,
				    false ,
				    0 ) ) // niceness
		return false;
	// this does not block if only one host and in memory
	return savedUpdatedRec ( es );
}

static void doneAddingEmailedLikesWrapper ( void *state ) {
	EmailState *es = (EmailState *)state;
	Emailer *em = es->m_emailer;
	if ( ! em->doneAddingEmailedLikes ( es ) ) return;
	// scan next
	em->emailScan(es);
}

bool Emailer::savedUpdatedRec ( EmailState *es ) {
	// now add to likedb if no error so we do not re-email these
	// same events. start_time is non-zero so it is just the single
	// instances of each event in the case of recurring events.
	SafeBuf *eb = &es->m_emailLikedbListBuf;
	// sort the records in tmp now
	char *buf     = eb->getBufStart();
	int32_t  bufSize = eb->length();
	// how many?
	int32_t count = bufSize / (int32_t)LIKEDB_RECSIZE;
	// sort for rdblist
	gbqsort ( buf , count , (int32_t)LIKEDB_RECSIZE, likedbCmp );
	// use the list we got
	key192_t startKey;
	key192_t endKey;
	startKey.setMin();
	endKey.setMax();
	// that is our list
	es->m_list5.set ( buf ,
			  bufSize ,
			  buf, // alloc
			  eb->getCapacity() , // allocSize
			  (char *)&startKey ,
			  (char *)&endKey  ,
			  LIKEDB_DATASIZE , // fixed datasize
			  true , // own data? yeah, free it when done
			  false , // use half keys? no.
			  sizeof(key192_t) );
	// steal it from safebuf so it doesn't free it
	eb->detachBuf();
	// note it
	log("facebook: adding events to likedb to prevent re-emailing. "
	    "listsize=%"INT32"",es->m_list5.getListSize());
	// add that
	if ( ! es->m_msg1.addList ( &es->m_list5 ,
				    RDB_LIKEDB ,
				    es->m_coll ,
				    es , // this ,
				    doneAddingEmailedLikesWrapper,
				    false ,
				    0 ) ) // niceness
		return false;
	// it did not block
	return doneAddingEmailedLikes ( es );
}


bool Emailer::doneAddingEmailedLikes ( EmailState *es ) {
	m_emailReplies++;
	es->m_emailLikedbListBuf.purge();
	es->m_inUse = false;
	return true;
}

/////////////
//
// code to send an individual email
//
////////////

// need to set EmailState::m_fbId, m_emailResultsBuf
bool Emailer::sendSingleEmail (  EmailState *es , int64_t fbId ) {

	es->m_sendSingleEmail = true;
	// claim it
	es->m_inUse = true;
	// point to emailer
	es->m_emailer = this;
	// who are we sending to?
	es->m_fbId = fbId;
	// container class
	es->m_emailer = this;
	// reset this
	es->m_errno = 0;
	// send it off
	if ( ! getMailServerIP( es ) ) return false;
	return true;
}




/////////////
//
// code to populate the m_emailTree
//
/////////////


// returns false if blocked, true otherwise
bool Emailer::populateEmailTree ( ) {

	// not if already in progress
	if ( m_populateInProgress ) return true;
	// stop if emailing now, it needs the tree
	//if ( m_emailInProgress ) return true;
	// re-scan only once per hour
	int32_t now = getTimeGlobal();
	if ( m_lastScan && now - m_lastScan < 3600 ) return true;
	// update that
	m_lastScan = now;
	// lock it up just in case...
	m_populateInProgress = true;

	// init the tree the first tim eonly
	if ( ! m_init ) {
		// . what's max # of tree nodes?
		// . assume avg facebookdb rec size of about 1000 bytes
		// . NOTE: 32 bytes overhead?
		int32_t maxMem = 10000000;
		int32_t maxTreeNodes = maxMem / 32;
		if ( ! m_emailTree.set ( 0 ,
					 maxTreeNodes ,
					 true , // balance?
					 maxMem,
					 true , // owndata?
					 "emailtree", // allocname
					 false , // datainptrs
					 NULL , // dbname
					 sizeof(key96_t) )) { // keysize
			log("email: failed to init email tree");
			return true;
		}
		// only do once
		m_init = true;
	}

	// clear out all nodes
	m_emailTree.clear();
	// reset start key for scan
	m_startKey.setMin();

	// returns false if blocked, true otherwise
	return scanLoop();
}


static void gotScanListWrapper ( void *state, RdbList *list , Msg5 *msg5 ) {
	// use this
	Emailer *em = (Emailer *)state;
	// this never blocks
	em->gotScanList ();
	// and resume the loop. return if it blocked.
	if ( ! em->scanLoop () ) return;
	// it did not block, it must be done...
	// we were spawned from emailEntryLoop(), so go back there
	em->emailScan( NULL );
}

// . scan facebookdb and get every facebookid, and couple it with the
//   time we gotta send the email
// . sort by that in emailTree
// . re-scan facebookdb every few hours in case of new entries or if
//   someone updates their email
// . i would also call addToEmailTree if a new facebookdb rec comes in.
//   perhaps do that from Rdb.cpp?
// . returns false if blocked true otherwise
bool Emailer::scanLoop ( ) {

	key96_t endKey   ;
	endKey.setMax();
	// get a meg at a time
	int32_t minRecSizes = 1024*1024;
	key96_t oldk; oldk.setMin();

 loop:
	// use msg5 to get the list, should ALWAYS block since no threads
	if ( ! m_msg5.getList ( RDB_FACEBOOKDB    ,
				m_coll          ,
				&m_list7         ,
				&m_startKey      ,
				&endKey        ,
				minRecSizes   ,
				true          , // includeTree
				false         , // add to cache?
				0             , // max cache age
				0             , // startFileNum  ,
				-1            , // numFiles      ,
				this          , // state
				gotScanListWrapper , // callback
				MAX_NICENESS  , // niceness
				false         )) // err correction?
		// return false if we blocked
		return false;
	// stuff the m_emailTree with some data based on m_list
	gotScanList( );
	// if something, get more
	if ( ! m_list7.isEmpty() ) goto loop;
	// stop
	m_populateInProgress = false;
	// i guess we did not block?
	return true;
}

void Emailer::gotScanList ( ) {

	int32_t now = getTimeGlobal();
	int32_t dayStart = now  - ( now % 86400 );

	if ( m_list7.isEmpty() ) return;

	// loop over entries in list
	for ( m_list7.resetListPtr() ; ! m_list7.isExhausted() ;
	      m_list7.skipCurrentRecord() ) {
		// get it
		char *drec = m_list7.getCurrentRec();
		// sanity check. delete key?
		if ( (drec[0] & 0x01) == 0x00 ) continue;

		FBRec *fr = (FBRec *)drec;

		char ef = fr->m_emailFrequency;
		// 0 means none provided, so let's default it to weekly
		//if ( ef == 0 ) continue;
		if ( ef == 0 ) ef = 2;
		// 3 means never
		// 1 is daily, 2 is weekly
		if ( ef == 3 ) continue;
		// strange?
		if ( ef != 1 && ef != 2 ) {
			log("email: strange freq = %"INT32"",(int32_t)ef);
			continue;
		}
		// int16_tcut
		uint64_t fbId = fr->m_fbId;

		// is assigned to us for emailing?
		Host *group = g_hostdb.getMyGroup();
		int32_t hpg = g_hostdb.getNumHostsPerShard();
		int32_t i = fbId % hpg;
		Host *h = &group[i];
		// skip if not assigned to us
		if ( h->m_hostId != g_hostdb.m_hostId ) continue;

		// add him to our list. sorted by next email time and
		// fbid. so its a key96_t
		key96_t k;
		k.n0 = fr->m_fbId;
		k.n1 = fr->m_nextRetry;
		// at what time of day to email ( in minutes)? UTC
		int32_t tte = dayStart + fr->m_timeToEmail * 60;
		// when was the last attempt to email?
		int32_t success = fr->m_lastEmailAttempt;

		// reset this for debug
		//success = 0;

		// . "success" is non-zero if we had at least one successful
		//   emailing to this person
		// . for daily frequency we must wait at least a day after
		//   the last successful email
		// . we have minus 4 hours in case the email got off to
		//   a late start
		if ( ef == 1 && success && now - success < 20*3600 )
			tte += 24*3600;
		// same goes for weekly emails
		if ( ef == 2 && success && now - success < 7*24*3600-4*3600 )
			tte += 7*24*3600;
		// assume that's the unix timestamp then (UTC)
		k.n1 = tte;
		// if non-zero, this overrides. this is non-zero if we
		// had our last email fail
		if ( fr->m_nextRetry ) k.n1 = fr->m_nextRetry;
		// HACK TIME!
		//k.n1 = 0;
		// add to the tree now
		if ( m_emailTree.addNode(m_collnum,(char *)&k,NULL,0) >= 0 ) 
			continue;
		// error!
		log("email: email tree add error: %s",mstrerror(g_errno));
	}
	m_startKey = *(key96_t *)m_list7.getLastKey();
	m_startKey += (uint32_t) 1;
	// watch out for wrap around
	//if ( startKey < *(key96_t *)list.getLastKey() ) return;
}



///////////////////////
//
// FACEBOOK SPIDER
//
///////////////////////

// https://graph.facebook.com/search?fields=id,privacy,picture,name,location,venue,description,"start_time,end_time&type=event&q=china&limit=10&offset=0

// . call this every second
// . https://developers.facebook.com/docs/reference/api/#searching
void facebookSpiderSleepWrapper ( int fd , void *state ) {
	// only for host #0
	//if ( g_hostdb.m_hostId != 0 ) return;

	// all spiders off?
	if ( ! g_conf.m_spideringEnabled ) return;

	// if nothing on queue, push a 0 fbid on there to initiate
	// the query spider algo on facebook
	//if ( g_n1 >= 2 ) return;
	// for now
	//if ( g_n1 >= 1 ) return;
	// flag
	bool gotIt = false;
	collnum_t collnum;
	// get event collection
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if empty
		if ( ! cr ) continue;
		// or not events
		if ( ! cr->m_indexEventsOnly ) continue;
		// ok, use that
		collnum = cr->m_collnum;
		// flag it
		gotIt = true;
	}
	// return if no such collection
	if ( ! gotIt ) return;

	// do we have an stubhubs already queued?
	bool hasLocalFBId  = false;
	bool hasEventBrite = false;
	bool hasStubHub    = false;
	bool hasFacebook   = false;
	for ( int32_t i = 0 ; i < g_n1 ; i++ ) {
		if ( g_fbq1[i] == -3 ) hasLocalFBId  = true;
		if ( g_fbq1[i] == -2 ) hasEventBrite = true;
		if ( g_fbq1[i] == -1 ) hasStubHub    = true;
		if ( g_fbq1[i] >=  0 ) hasFacebook   = true;
	}

	// need this
	int32_t now = getTimeGlobal();

	// ok, it's empty! 0 fbid has special meaning. it means to
	// do a spider round on facebook
	if ( ! hasFacebook && g_conf.m_facebookSpideringEnabled &&
	     // only for host #0
	     g_hostdb.m_hostId == 0 )
		queueFBId ( 0 , collnum );

	// . s_ptr3 is now used for stub hub and is a time_t!
	// . reset it if it's over a year out
	if ( ! hasStubHub && g_conf.m_stubHubSpideringEnabled &&
	     // only for host #0
	     g_hostdb.m_hostId == 0 ) {
		// if ptr is over a year into the future then reset to
		// 0 and wait for 12 hours!!! stubhub.com spiders through
		// in like an hour!
		if ( s_ptr3 - now > 365*86400 )	{
			// give it a delay too!
			s_holdOffStubHubTill = now + 12*3600;
			// log it
			log("stubhub: stubhub spider completed. "
			    "waiting for 12 hours before "
			    "hitting stubhub again." );
			// and reset our timer thing
			s_ptr3 = 0;
		}
		// are we done waiting?
		if ( now > s_holdOffStubHubTill )
			// queue the stubhub
			queueFBId ( -1 , collnum );
	}

	// . s_ptr3 is on the day we need to download events from eventbrite
	//   that were create on that day
	// . if its 0 then play catch up until we hit today, then just
	//   hit it like once per hour for events created today
	if ( ! hasEventBrite && 
	     g_conf.m_eventBriteSpideringEnabled &&
	     // only for host #0
	     g_hostdb.m_hostId == 0 &&
	     // if we got no results we delay like an hour until we
	     // try again, in hopes someone added some new events to
	     // eventbrite's index
	     now > s_eventBriteWaitUntil )
		// queue the eventbrite
		queueFBId ( -2 , collnum );

	// this is for all hosts in stripe #0, not just host #0
	if ( ! hasLocalFBId &&
	     // skip if not in bottom part
	     g_hostdb.m_myHost->m_stripe == 0 &&
	     // we do this once per day
	     now > s_localWaitUntil ) 
		// queue the local facebookdb scan
		queueFBId ( -3 , collnum );

}

#include "Speller.h"

// use unified dictionary
char *getNextQuery ( ) {

	if ( g_hostdb.m_hostId != 0 ) { 
		log("qloop: wtf! not host #0");
		return NULL;
	}

	if ( ! s_init ) {
		// load query loop state
		//loadQueryLoopState();
		// try to load from disk
		//if ( loadSortByPopTable() ) s_init = true;
		bool s1 = false;
		bool s2 = false;
		s1 = s_tbuf1.load ( g_hostdb.m_dir,"/popsortwords.dat" );
		s2 = s_tbuf2.load ( g_hostdb.m_dir,"/popsortplaces.dat" );
		// if both loaded we are done
		if ( s1 && s2 ) s_init = true;
		// clear if one loaded but the other did not
		else { s_tbuf1.reset(); s_tbuf2.reset(); }
	}

	// if load was unsuccessful, then create
	if ( ! s_init ) {
		// ok, create it and save it
		HashTableX *ud = &g_speller.m_unifiedDict;
		// init trees
		RdbTree tree1;
		RdbTree tree2;
		tree1.set ( 4,  // fixeddatasize
			    3000000, // maxnodes
			    true, // do balancing
			    -1 , // maxmem
			    false, // owndata?
			    "tree1", // allocname
			    false, // datainptrs?
			    NULL, // dbname
			    12 ); // keysize
		tree2.set ( 4,  // fixeddatasize
			    5000000, // maxnodes
			    true, // do balancing
			    -1 , // maxmem
			    false, // owndata?
			    "tree2", // allocname
			    false , // datainptrs?
			    NULL, // dbname
			    12 ); // keysize
		// for keeping keys unique
		int32_t count = 0;
		// scan the unified dictionary 
		int32_t n1 = ud->m_numSlots;
		for ( int32_t i = 0 ; i < n1 ; i++ ) {
			// skip if empty
			if ( ! ud->m_flags[i] ) continue;
			// . get the ptr into m_unifiedBuf
			// . word/phrase\tlangid\tpop\tlangid\tpop....
			char *p = *(char **)ud->getValueFromSlot(i);
			// point to \0 ending the word or the phonetic
			char *w = p - 1;
			// back up again until we are at the beginning of
			// that word or phonetic
			for ( ; w[-1] ; w-- );
			// is it a phonetic?
			if ( is_upper_a(w[0]) || w[0]=='*' ) {
				// point to the \0 before it
				w--;
				// and back up to the start of the word/phrase
				// that the phonetic represents
				for ( ;w > g_speller.m_unifiedBuf &&w[-1];w--);
			}
			// scan word or phrase, we only want words not
			// phrases for this... phrases are too spammy!
			bool hadSpace = false;
			for ( char *x = w; *x ; x++ ) {
				if ( ! is_wspace_a ( *x ) ) continue;
				hadSpace = true;
				break;
			}
			// skip if its a phrase
			if ( hadSpace ) continue;
			// get the max pop from all the language/pop tuples
			int32_t maxPop = -2;
			int32_t pop;
		subloop:
			// skip over langid
			for ( ; *p && *p !='\t';p++ );
			// crazy? a pop should follow it!
			if ( ! *p ) goto done;
			// skip that
			p++;
			// get the pop
			pop = atol(p);
			// if negative make 0
			if ( pop < 0 ) pop = 0;
			// get max pop
			if ( pop > maxPop ) maxPop = pop;
			// skip over next tab, should be langid or \0
			for ( ; *p && *p !='\t';p++ );
			// if no more, get next word line
			if ( ! *p ) goto done;
			// skip that
			if ( *p ) p++;
			// get more if they are there
			goto subloop;
			// store it in tree
		done:
			// how is this possible?
			if ( maxPop < 0 ) continue;
			// make the key
			key_t k;
			k.n1 = ~((uint32_t)maxPop);
			k.n0 = count++;
			// store offset
			int32_t woff = w - g_speller.m_unifiedBuf;
			// . add to b-tree to sort by pop
			// . data is the word/phrase ptr
			tree1.addNode(0,k,(char *)woff,4);
		}

		// . now add the cities in there too!
		// . scan the cities
		// . g_nameTable is from Address.cpp
		int32_t n2 = g_nameTable.m_numSlots;
		for ( int32_t i = 0 ; i < n2 ; i++ ) {
			// skip if empty
			if ( ! g_nameTable.m_flags[i] ) continue;
			// get it
			int32_t offset = *(int32_t *)g_nameTable.getValueFromSlot(i);
			// get the ptr into g_pbuf for it
			PlaceDesc *pd = (PlaceDesc *)(g_pbuf+offset);
			// get the pop
			uint32_t pop = pd->m_population;
			// make the key
			key_t k;
			k.n1 = ~pop;
			// "China" has many spellings and each one has an
			// entry in the g_nameTable BUT they hash to the
			// same PlaceDesc ptr, so use that as part of they
			// key for making sure we have no dups in tree2.
			k.n0 = (uint64_t)pd;
			//note it
			//log("adding pop=%"UINT32"",pop);
			// add to b-tree to sort by pop
			tree2.addNode(0,k,(char *)offset,4);
		}
		// serialize tree1
		for (int32_t n=tree1.getLowestNode();n>=0;n=tree1.getNextNode(n)){
			// get data. this one is a slot # in m_unifiedDict
			int32_t woff = (int32_t)tree1.getData(n);
			// store it
			s_tbuf1.pushLong(woff);
		}
		int32_t reps = 0;
		// serialize tree2
		for (int32_t n=tree2.getLowestNode();n>=0;n=tree2.getNextNode(n)){
			// get data. this one is an offset into g_pbuf
			int32_t i = (int32_t)tree2.getData(n);
			// sample
			PlaceDesc *pd = (PlaceDesc *)(g_pbuf+i);
			// print it
			if ( reps++ == 0 && pd->m_population < 1000000 ) {
				char *xx=NULL;*xx=0; }
			// store it
			s_tbuf2.pushLong(i);
		}
		// save both
		s_tbuf1.save ( g_hostdb.m_dir,"/popsortwords.dat" );
		s_tbuf2.save ( g_hostdb.m_dir,"/popsortplaces.dat" );
		// do not re-do
		s_init = true;
		// init the ptrs then
		//s_ptr1 = 0;
		//s_ptr2 = 0;
		// save state
		saveQueryLoopState();
	}

	if ( s_flip == 0 ) s_flip = 1;
	else               s_flip = 0;

	if ( s_flip == 0 && ! g_speller.m_unifiedBuf ) {
		log("facebook: unifiedDict not loaded! skipping pop words "
		    "facebook spidering.");
		s_flip = 1;
	}

	// get the next word or location
	if ( s_flip == 0 ) {
		int32_t woff = ((int32_t *)(s_tbuf1.getBufStart())) [s_ptr1];
		s_ptr1++;
		if ( s_ptr1 * 4 > s_tbuf1.length() ) s_ptr1 = 0;

		// just to keep things somewhat fresh, let's cycle
		// once we hit 15,000 words which is about 5 days
		// at the current rate. i am planning on increasing
		// the fb spider rate though a little if i can since it
		// seems to not be rate-limited so far
		if ( s_ptr1 > 15000 ) s_ptr1 = 0;
		
		char *v = g_speller.m_unifiedBuf + woff;
		return v;
	}

	if ( s_flip == 1 ) {
		int32_t poff = s_ptr2 * 4;
		s_ptr2++;
		if ( s_ptr2 * 4 > s_tbuf2.length() ) s_ptr2 = 0;

		// just to keep things somewhat fresh, let's cycle
		// once we hit 15,000 words which is about 5 days
		// at the current rate. i am planning on increasing
		// the fb spider rate though a little if i can since it
		// seems to not be rate-limited so far
		if ( s_ptr2 > 15000 ) s_ptr2 = 0;

		int32_t offset = *(int32_t *)(s_tbuf2.getBufStart() + poff);
		// get the ptr into g_pbuf for it
		PlaceDesc *pd = (PlaceDesc *)(g_pbuf+offset);

		if ( pd->m_flags & PDF_STATE )
			return (char *)pd->getStateName();
		if ( pd->m_flags & PDF_COUNTRY )
			return (char *)pd->getCountryName();
		// crap, official names has many dups because we have one
		// place desc for every slang name of a place.
		return pd->getOfficialName();
	}

	// shouldn't be here!
	char *xx=NULL;*xx=0;
	return NULL;
}



// . save the state of getNextQuery() 
// . save the queue g_fbq1[100],g_colls1[100],g_n1
// . called from Process.cpp
bool saveQueryLoopState ( ) {
	SafeBuf ss;
	ss.pushLong(s_flip);
	ss.pushLong(s_ptr1);
	ss.pushLong(s_ptr2);
	// the queue of fbids
	ss.pushLong(g_n1);
	ss.safeMemcpy((char *)g_fbq1,g_n1*4);
	ss.safeMemcpy((char *)g_colls1,g_n1*4);
	ss.pushLong(s_ptr3);
	ss.pushLong(s_holdOffStubHubTill);
	ss.pushLongLong(s_ptr4);
	ss.pushLong(s_eventBriteWaitUntil);
	// local scan
	ss.pushLongLong(s_ptr5);
	ss.pushLong    (s_localWaitUntil);
	log("facebook: saving fbloop.dat. "
	    "s_ptr1=%"INT32" "
	    "s_ptr2=%"INT32" "
	    "s_ptr3=%"INT32" "
	    "s_ptr4=%"INT64" "
	    "s_ptr5=%"INT64" "
	    "s_holdOffStubHubTill=%"UINT32" "
	    "s_eventBriteWaitUntil=%"UINT32" "
	    "s_localWaitUntil=%"UINT32" "
	    "g_n1=%"INT32"",
	    s_ptr1,
	    s_ptr2,
	    s_ptr3,
	    s_ptr4,
	    s_ptr5,
	    s_holdOffStubHubTill,
	    s_eventBriteWaitUntil,
	    s_localWaitUntil,
	    g_n1);
	return ss.save(g_hostdb.m_dir,"fbloop.dat");
}

bool loadQueryLoopState ( ) {
	SafeBuf ss;
	if ( ! ss.load(g_hostdb.m_dir,"fbloop.dat") ) return false;
	// assign
	char *p = ss.getBufStart();
	char *pend = p + ss.length();
	s_flip = *(int32_t *)p; p += 4;
	s_ptr1 = *(int32_t *)p; p += 4;
	s_ptr2 = *(int32_t *)p; p += 4;
	g_n1   = *(int32_t *)p; p += 4;
	gbmemcpy ( g_fbq1   , p , g_n1 * 4 ); p += g_n1 * 4;
	gbmemcpy ( g_colls1 , p , g_n1 * 4 ); p += g_n1 * 4;
	if ( p >= pend ) goto done;
	s_ptr3 = *(int32_t *)p; p += 4;
	if ( p >= pend ) goto done;
	s_holdOffStubHubTill = *(int32_t *)p; p += 4;
	if ( p >= pend ) goto done;
	s_ptr4 = *(int64_t *)p; p += 8;
	s_eventBriteWaitUntil = *(int32_t *)p; p += 4;
	// local scan
	if ( p >= pend ) goto done;
	s_ptr5           = *(int64_t *)p; p += 8;
	s_localWaitUntil = *(int32_t      *)p; p += 4;
 done:
	log("facebook: loaded fbloop.dat. "
	    "s_ptr1=%"INT32" "
	    "s_ptr2=%"INT32" "
	    "s_ptr3=%"INT32" "
	    "s_ptr4=%"INT64" "
	    "s_ptr5=%"INT64" "
	    "s_holdOffStubHubTill=%"UINT32" "
	    "s_eventBriteWaitUntil=%"UINT32" "
	    "s_localWaitUntil=%"UINT32" "
	    "g_n1=%"INT32"",
	    s_ptr1,
	    s_ptr2,
	    s_ptr3,
	    s_ptr4,
	    s_ptr5,
	    s_holdOffStubHubTill,
	    s_eventBriteWaitUntil,
	    s_localWaitUntil,
	    g_n1);
	return true;
}
