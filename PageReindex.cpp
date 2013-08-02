#include "gb-include.h"

#include "HttpServer.h"
#include "Msg0.h"
#include "Msg1.h"
#include "IndexList.h"
#include "Msg20.h"
#include "Collectiondb.h"
#include "Hostdb.h"
#include "Conf.h"
#include "Query.h"
#include "RdbList.h"
#include "Pages.h"
#include "Msg3a.h"
#include "Msg40.h"
#include "sort.h"
#include "Users.h"
#include "Spider.h"
#include "Revdb.h"
#include "XmlDoc.h"
#include "PageInject.h" // Msg7
#include "PageReindex.h"

static char *printInterface ( char *p , char *pend , char *q ,//long user ,
                              char *username, char *c , char *errmsg = NULL ) ;


class State13 {
public:
	char       m_query [ MAX_QUERY_LEN + 1];
	char       m_isAdmin;
	Msg1c      m_msg1c;
	//Msg1d      m_msg1d;
	char       m_coll [ MAX_COLL_LEN + 1];
	long       m_collLen;
	TcpSocket *m_socket;
	char       m_replyBuf[64*1024];
	long       m_replyBufSize;
	char      *m_place;
	char       m_updateTags;
	//Query      m_qq;
};

static void doneReindexing ( void *state ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . query re-index interface
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageReindex ( TcpSocket *s , HttpRequest *r ) {

	// store password into "pwd" 
	char pwd[32];
	long len ;
	char *t = r->getString ( "pwd" , &len );
	if ( len > 31 ) len = 31;
	memcpy ( pwd , t , len );
	pwd [ len ] = '\0';

	// are we the admin?
	bool isAdmin = g_collectiondb.isAdmin ( r , s );
	//long user    = g_pages.getUserType ( s , r );
	char *username = g_users.getUsername ( r );
	char *errmsg = NULL;

	// get the collection record
	CollectionRec *cr = g_collectiondb.getRec ( r );
	if ( ! cr ) {
		log("admin: no collection record found "
		    "for supplied collection name. Query reindex failed. "
		    "Returning HTTP status of 500.");
		return g_httpServer.sendErrorReply ( s , 500 ,
						"Collection does not exist.");
	}
	bool isAssassin = cr->isAssassin ( s->m_ip );
	if ( isAdmin ) isAssassin = true;

	// bail if permission denied
	if ( ! isAssassin && ! cr->hasPermission ( r , s ) ) {
		log("admin: Bad collection name "
		    "or password. Query reindex failed. Permission denied.");
		return sendPageLogin ( s , r , 
				       "Collection name or "
				       "password is incorrect.");
	}

	// get collection name and its length
	char *coll    = cr->m_coll;
	long  collLen = gbstrlen ( coll );


	char buf[64*1024];
	char *p    = buf;
	char *pend = buf + 64*1024;

	long  qlen;
	char *q = r->getString ("q",&qlen);

	// if they are NOT submitting a request print the interface
	// and we're not running, just print the interface
	t = r->getString ("action" , &len );
	if ( len != 2 ) { // && ! s_isRunning ) {
		p = g_pages.printAdminTop ( p , pend , s , r );
		p = printInterface ( p , pend , q , username , coll );
		return g_httpServer.sendDynamicPage (s,buf,p-buf,-1,false);
	}		

	// make a state
	State13 *st ;
	try { st = new (State13); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTagdb: new(%i): %s", 
		    sizeof(State13),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State13) , "PageReindex" );

	// set stuff now
	st->m_isAdmin    = isAdmin;
	

	// save the query to static buffer
	t  = r->getString ( "q"  , &len );
	if ( len > MAX_QUERY_LEN ) len = MAX_QUERY_LEN;
	memcpy ( st->m_query , t , len );
	st->m_query[len] = '\0';

	// save start and end numbers
	long startNum = r->getLong   ( "srn" , 0 );
	long endNum   = r->getLong   ( "ern" , 0 );
	//st->m_spiderPriority  = r->getLong   ( "sp" , 7 );
	// get time offset to add to spider time for urls to be reindexed
	//float days  = r->getFloat  ( "sto" , 0.0 );
	//st->m_spiderTimeOffset  = (long)(days * 24.0*60.0*60.0);
	bool updateTags = r->getLong ( "updatetags", 0 );

	// copy collection
	memcpy ( st->m_coll , coll , collLen );
	st->m_coll [ collLen ] = '\0';
	st->m_collLen=collLen;

	// fix parms
	if ( startNum <  0 ) startNum = 0 ;
	if ( endNum   <  0 ) endNum   = 0 ;
	//if ( st->m_spiderPriority  < 0 ) st->m_spiderPriority  = 0 ;
	//if ( st->m_spiderPriority  > MAX_SPIDER_PRIORITIES ) 
	//	st->m_spiderPriority  = MAX_SPIDER_PRIORITIES-1 ;


	// . get the query terms
	// . a boolFlag of 0 means query is not boolean
	//	Query qq;
	//st->m_qq.set ( st->m_query , 0 /*boolFlag*/ );

	// . get first query term 
	// . will be the first phrase segment if 2 or more words in a phrase
	//long n = st->m_qq.getNumTerms();
	// if no query send back a bitch
	if ( ! st->m_query[0] ) {
		log("admin: Query reindex was given no query terms.");
		errmsg = "Empty Query. You must supply a query.";
 		p = g_pages.printAdminTop ( p , pend , s , r );
 		p = printInterface ( p , pend , q , username , coll , errmsg );
 		return g_httpServer.sendDynamicPage (s,buf,p-buf,-1,false);
	}

	// now lets get the index list, loop through each docId, getting the
	// titleRec to get the URL and add the URL to the spider queue

	// save socket for retuning a page when we're done
	st->m_socket = s;
	// save the reply page, for when add is done
	char *rp    = st->m_replyBuf;
	char *rpend = rp + 64*1024;
	rp = g_pages.printAdminTop ( rp , rpend , s , r );

	// place holder, for holding response when we're done adding
	// all these docids to the spider queue
	st->m_place = rp ;
	memset ( rp , ' ' , 100 );
	rp += 100;

	rp = printInterface ( rp , rpend , q , username , coll , errmsg );

	// save length
	st->m_replyBufSize = rp - st->m_replyBuf;

	// log it
	log(LOG_INFO,"admin: Performing query reindex for query: "
	    "%s",st->m_query);


	st->m_updateTags = updateTags;

	/*

	  take this our for now. we are using likedb...

	if ( updateTags ) {
		// let msg1d do all the work now
		if ( ! st->m_msg1d.updateQuery  ( st->m_query ,
						  r,
						  s,
						  st->m_coll,
						  startNum ,
						  endNum   ,
						  st ,
						  doneReindexing ) )
			return false;
	}
	else {
	*/
		// let msg1d do all the work now
		if ( ! st->m_msg1c.reindexQuery ( st->m_query ,
						  st->m_coll,
						  startNum ,
						  endNum   ,
						  st ,
						  doneReindexing ) )
			return false;
	//}

	// no waiting
	doneReindexing ( st );
	return true;
}

void doneReindexing ( void *state ) {
	// cast it
	State13 *st = (State13 *)state;
	// note it
	log(LOG_INFO,"admin: Done with query reindex. %s",mstrerror(g_errno));
	// note it
	TcpSocket *sock = st->m_socket;
	// error?
	if ( g_errno ) {
		g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
		mdelete ( st , sizeof(State13) , "PageTagdb" );
		delete (st);
	}	
	// if no error, send the pre-generated page
	// this must be under 100 chars or it messes our reply buf up
	char mesg[200];
	//
	// if we used msg1d, then WHY ARE WE USING m_msg1c.m_numDocIdsAdded 
	// here?
	//
	/*
	if ( st->m_updateTags )
		sprintf ( mesg , "<center><font color=red><b>Success. "
			  "Updated tagrecs and index for %li docid(s)"
			  "</b></font></center><br>" , 
			  st->m_msg1d.m_numDocIds );
	else
	*/
		sprintf ( mesg , "<center><font color=red><b>Success. "
			  "Added %li docid(s) to "
			  "spider queue.</b></font></center><br>" , 
			  st->m_msg1c.m_numDocIdsAdded );

	memcpy ( st->m_place, mesg , gbstrlen(mesg) );

	g_httpServer.sendDynamicPage ( sock,
				       st->m_replyBuf,
				       st->m_replyBufSize,
				       -1,
				       false);

	mdelete ( st , sizeof(State13) , "PageTagdb" );
	delete (st);
}

char *printInterface (char *p , char *pend , char *q , //long user , 
                      char *username, char *c , char *errmsg ) {
	if ( ! q ) q = "";

	// print error msg if any
	if ( errmsg ) {
		sprintf(p,"<br><center><b><font color=red>%s"
			"</font></b></center><br>",
			errmsg );
		p += gbstrlen ( p );
	}

	char bb [ MAX_COLL_LEN + 60 ];
	bb[0]='\0';
	//if ( user == USER_MASTER && c && c[0] ) sprintf ( bb , " (%s)", c);

	// print the reindex interface
	sprintf ( p , 
		  "<table width=100%% bgcolor=#%s cellpadding=4 border=1>"
		  "<tr><td colspan=3 bgcolor=#%s><center>"
		  //"<font size=+1>"
		  "<b>"
		  "Reindex Urls"
		  "</b>%s</td></tr>"
		  "<tr><td colspan=3>"
		  "<font size=1>"
		  "Reindex the URLs that match this query. If URLs are "
		  "banned they will be removed from the index. "
		  "</td></tr>"

		  "<tr><td><b>query</b>"
		  "<br><font size=1>"
		  "URLs matching this query will be added to the spider "
		  "queue for re-spidering. Spider priority will be assigned "
		  "based on what rule they match in the url filters table."
		  "</td>"
		  "<td><input type=text value=\"%s\" "
		  "name=q size=30></td></tr>"

		  "<tr><td><b>update event tags</b>"
		  "<br><font size=1>Just update the tags for each event "
		  "in the search results. For each docid in the search "
		  "results we look it up in tagdb, get its tags, and "
		  "add the terms directly into datedb/indexdb. We also have "
		  "to update the reverse index record so they will be "
		  "deleted if the doc gets deleted.</td>"
		  "<td><input type=checkbox value=1 "
		  "name=updatetags>"
		  "</td></tr>"
		  , LIGHT_BLUE , DARK_BLUE , bb , q );

	p += gbstrlen ( p );

	sprintf ( p , 

		  "<tr><td><b>start result number</b>"
		  "<font size=1>"
		  "<br>Start at this search result number. Default 0.</td>"
		  "<td><input type=text name=srn value=0 size=10>"
		  "</td></tr>"

		  "<tr><td><b>end result number</b>"
		  "<font size=1>"
		  "<br>Stop at this search result number. "
		  "Default 2000000.</td>"
		  "<td><input type=text name=ern size=10 value=2000000>"
		  "</td></tr>" );

	p += gbstrlen ( p );

	/*
		  //"<tr><td><b>just list results</b>"
		  //"<br>will list results so you can hand pick which ones "
		  //"get reindexed by selecting the checkbox.</td>"
		  //"<td><input type=checkbox name=jlr value=1 checked>"
		  //"</td></tr>"

		  "<tr><td><b>spider time offset</b>"
		  "<br><font size=1>Specify number of days relative to "
		  "today. URLs will be assigned this spider date.</td>"
		  "<td><input type=text name=sto size=10 value=0> days"
		  "</td></tr>"

		  "<tr><td><b>spider priority</b>"
		  "<br><font size=1>URLs will be added with this spider "
		  "priority.</td>"
		  "<td>" );

	// spider priority
	sprintf ( p , "<select name=sp>");
	p += gbstrlen ( p );
	for (long i=0;i<MAX_SPIDER_PRIORITIES;i++){
		if ( i == MAX_SPIDER_PRIORITIES-1 )
			sprintf(p,"<option value=%li selected>%li",i,i);
		else
			sprintf(p,"<option value=%li>%li",i,i);
		p+=gbstrlen(p);
	}
	sprintf(p,"</select>"
		  "</td></tr>");
	p += gbstrlen ( p );
	  */

	sprintf(p,"</table><br>" );

	// submit button
	sprintf ( p , 
		  "<center>"
		  "<input type=submit name=action value=OK>" 
		  "</center>"
		  "</form></html>");
	p += gbstrlen ( p );
	
	return p;
}


////////////////////////////////////////////////////////
//
//
// Msg1c if for reindexing docids
//
//
////////////////////////////////////////////////////////

static void gotDocIdListWrapper ( void *state );
static void addedListWrapper ( void *state ) ;

Msg1c::Msg1c() {
	m_numDocIds = 0;
	m_numDocIdsAdded = 0;
	m_coll = NULL;
	m_callback = NULL;
}

bool Msg1c::reindexQuery ( char *query ,
			   char *coll  ,
			   long startNum ,
			   long endNum ,
			   void *state ,
			   void (* callback) (void *state ) ) {

	m_coll           = coll;
	m_startNum       = startNum;
	m_endNum         = endNum;
	m_state          = state;
	m_callback       = callback;
	m_numDocIds      = 0;
	m_numDocIdsAdded = 0;

	m_niceness = MAX_NICENESS;

	m_qq.set2 ( query , langUnknown , true ); // /*bool flag*/ );

	//CollectionRec *cr = g_collectiondb.getRec ( coll );
	// reset again just in case
	m_req.reset();
	// set our Msg39Request
	m_req.m_docsToGet                 = endNum;
	m_req.m_niceness                  = 0,
	m_req.m_doSiteClustering          = false;
	m_req.m_doIpClustering            = false;
	m_req.m_doDupContentRemoval       = false;
	m_req.ptr_coll                    = coll;
	m_req.size_coll                   = gbstrlen(coll)+1;
	m_req.ptr_query                   = m_qq.m_orig;
	m_req.size_query                  = m_qq.m_origLen+1;
	m_req.m_timeout                   = 100000; // very high, 100k seconds

	// log for now
	logf(LOG_DEBUG,"reindex: q=%s",query);

	g_errno = 0;
	// . get the docIds
	// . this sets m_msg3a.m_clusterLevels[] for us
	if ( ! m_msg3a.getDocIds ( &m_req ,
				   &m_qq   ,
				   this   , 
				   gotDocIdListWrapper ))
		return false;
	// . this returns false if blocks, true otherwise
	// . sets g_errno on failure
	return gotList ( );
}

void gotDocIdListWrapper ( void *state ) {
	// cast
	Msg1c *m = (Msg1c *)state;
	// return if this blocked
	if ( ! m->gotList ( ) ) return;
	// call callback otherwise
	m->m_callback ( m->m_state );
}

// . this returns false if blocks, true otherwise
// . sets g_errno on failure
bool Msg1c::gotList ( ) {

	if ( g_errno ) return true;

	long long *tmpDocIds = m_msg3a.getDocIds();
	long       numDocIds = m_msg3a.getNumDocIds();

	if ( m_startNum > 0) {
		numDocIds -= m_startNum;
		tmpDocIds = &tmpDocIds[m_startNum];
	}

	m_numDocIds = numDocIds; // save for reporting
	// log it
	log(LOG_INFO,"admin: Got %li docIds for query reindex.", numDocIds);
	// bail if no need
	if ( numDocIds <= 0 ) return true;

	// force spiders on on entire network. they will progagate from 
	// host #0... 
	g_conf.m_spideringEnabled = true;

	// make a list big enough to hold all the spider recs that we make
	// from these docIds
	SafeBuf sb;

	long nowGlobal = getTimeGlobal();

	HashTableX dt;
	char dbuf[1024];
	dt.set(8,0,64,dbuf,1024,false,0,"ddocids");

	m_numDocIdsAdded = 0;
	long count = 0;
	// list consists of docIds, loop through each one
 	for(long i = 0; i < numDocIds; i++) {
		long long docId = tmpDocIds[i];
		// when searching events we get multiple docids that are same
		if ( dt.isInTable ( &docId ) ) continue;
		// add it
		if ( ! dt.addKey ( &docId ) ) return true;
		// log it if we have 1000 or less of them for now
		//if ( i <= 100 ) 
		logf(LOG_INFO,"build: Adding docid #%li/%li) %lli",
		     i,count++,docId);

		SpiderRequest sr;
		sr.reset();

		// url is a docid!
		sprintf ( sr.m_url , "%llu" , docId );
		// make a fake first ip
		long firstIp = (docId & 0xffffffff);
		// use a fake ip
		sr.m_firstIp        =  firstIp;//nowGlobal;
		sr.m_isInjecting    =  true;
		sr.m_hopCount       = -1;
		sr.m_isPageReindex  =  1;
		sr.m_urlIsDocId     =  1;
		sr.m_fakeFirstIp    =  1;
		// for msg12 locking
		sr.m_probDocId      = docId;
		// use test-parser not test-spider
		sr.m_useTestSpiderDir = 0;
		// if this is zero we end up getting deduped in
		// dedupSpiderList() if there was a SpiderReply whose
		// spider time was > 0
		sr.m_addedTime = nowGlobal;
		//sr.setDataSize();
		// . complete its m_key member
		// . parentDocId is used to make the key, but only allow one
		//   page reindex spider request per url... so use "0"
		sr.setKey( firstIp, 0LL , false );
		// how big to serialize
		long recSize = sr.getRecSize();

		m_numDocIdsAdded++;
	
		// store it
		if ( ! sb.safeMemcpy ( (char *)&sr , recSize ) ) {
			// g_errno must be set
			if ( ! g_errno ) { char *xx=NULL;*xx=0; }
			//s_isRunning = false;
			log(LOG_LOGIC,
			    "admin: Query reindex size of %li "
			    "too big. Aborting. Bad engineer." , 
			    (long)0);//m_list.getListSize() );
			return true;
		}
	}
	// make it into a list for adding with Msg1
	key128_t startKey; startKey.setMin();
	key128_t endKey  ; endKey.setMax();
	m_list2.set ( sb.getBufStart() ,
		      sb.length     () ,
		      sb.getBufStart() ,
		      sb.getCapacity() ,
		      (char *)&startKey , 
		      (char *)&endKey   ,
		      -1       ,  // fixedDatSize
		      true     ,  // ownData?
		      false    , // use half keys?
		      16 ); // 16 byte keys now
	// release from sb so it doesn't free it
	sb.detachBuf();

	//g_conf.m_logDebugSpider = 1;

	if ( ! m_msg1.addList ( &m_list2  ,
				RDB_SPIDERDB , // spiderdb
				m_coll       ,
				this ,
				addedListWrapper ,
				false     , // force local
				0         ))// niceness
		return false;
	// if we did not block, go here
	return true;
}

void addedListWrapper ( void *state ) {
	// cast
	Msg1c *m = (Msg1c *)state;
	// call callback, all done
	m->m_callback ( m->m_state );
}



////////////////////////////////////////////////////////
//
//
// Msg1d is for adding new tags for events
//
//
////////////////////////////////////////////////////////

/*

static void updateTagTermsWrapper ( void *state ) {
	Msg1d *THIS = (Msg1d *)state;
	if ( ! THIS->updateTagTerms ( ) ) return;
	// . finally done with the query reindex for tags
	// . on error g_errno is set here...
	THIS->m_callback ( THIS->m_state );
}

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Msg1d::updateQuery ( char *query ,
			   HttpRequest *r,
			   TcpSocket *sock,
			   char *coll  ,
			   long startNum ,
			   long endNum ,
			   void *state ,
			   void (* callback) (void *state ) ) {

	m_coll           = coll;
	m_startNum       = startNum;
	m_endNum         = endNum;
	m_state          = state;
	m_callback       = callback;

	m_i = 0;
	m_flushedList = 0;

	m_qq.set ( query , 0 ); // flag

	m_niceness = MAX_NICENESS;

	//CollectionRec *cr = g_collectiondb.getRec ( coll );
	
	// make a search input
	m_si.set ( sock , r , &m_qq );

	m_si.m_skipEventMerge            = 1;
	m_si.m_niceness                  = 0;
	m_si.m_doSiteClustering          = false;
	m_si.m_doIpClustering            = false;
	m_si.m_doDupContentRemoval       = false;
	m_si.m_docsWanted                = endNum - startNum;
	m_si.m_firstResultNum            = startNum;
	m_si.m_userLat                   = 999.0;
	m_si.m_userLon                   = 999.0;
	m_si.m_zipLat                    = 999.0;
	m_si.m_zipLon                    = 999.0;
	m_si.m_clockOff                  = 0;
	m_si.m_clockSet                  = 0;
	// it is not super critical machine clock is synced right away.
	// it could take a second or so after we come up to sync with host #0
	if ( isClockInSync() ) m_si.m_nowUTC = getTimeGlobal();
	else                   m_si.m_nowUTC = getTimeLocal ();
	// . sort by next upcoming time of event (if any)
	// . TODO: make sure does not include expired events
	m_si.m_sortBy = SORTBY_TIME;

	m_si.m_coll    = m_coll;
	m_si.m_collLen = gbstrlen(m_coll);


	if ( ! m_msg40.getResults ( &m_si ,
				    false ,
				    this ,
				    updateTagTermsWrapper ) )
		// return false if we blocked
		return false;

	// . this returns false if blocks, true otherwise
	// . sets g_errno on failure
	return updateTagTerms ( );
}

/////////////////////////////////////////////////
//
//
// the alternate reindex path, just update tags
//
//
/////////////////////////////////////////////////

void sleepBack ( int fd , void *state ) {
	Msg1d *THIS = (Msg1d *)state;
	// unregister
	g_loop.unregisterSleepCallback ( THIS , sleepBack );
	// note it
	log("reindex: back from sleep");
	// try to get lock again
	if ( ! THIS->updateTagTerms ( ) ) return;
	// . finally done with the query reindex for tags
	// . on error g_errno is set here...
	THIS->m_callback ( THIS->m_state );
}

bool Msg1d::updateTagTerms ( ) {
	// get docids
	m_numDocIds = m_msg40.getNumDocIds();
	// loop over each docid/eventid/eventhash
	for ( ; m_i < m_numDocIds ; ) {
		// . retry if we did not get the lock
		// . TODO: make sure this doesn't hog the cpu looping!!
		if ( ! m_msg12.m_hasLock ) m_gotLock = 0;

		// shortcut
		Msg20Reply *mr = m_msg40.m_msg20[m_i]->m_r;

		// lock it
		if ( ! m_gotLock++ ) {
			// note it
			//log("reindex: getting lock for %s",mr->ptr_ubuf);
			log("reindex: getting lock for %llu",mr->m_urlHash48);
			// try to get the lock
			if ( ! m_msg12.getLocks ( mr->m_docId,//urlHash48 ,
						  mr->ptr_ubuf , // url
						  this , 
						  updateTagTermsWrapper ) ) {
				//log("reindex: blocked");
				// return false if blocked
				return false;
			}
			// note it
			//log("reindex: did not block");
			// wait for lock?
			if ( ! m_msg12.m_hasLock ) {
				log("reindex: waiting for lock for uh=%llu",
				    mr->m_urlHash48);
				g_loop.registerSleepCallback(100,this,
							     sleepBack,0);
				return false;
			}
		}
		// sanity
		if ( ! m_msg12.m_hasLock ) { char *xx=NULL;*xx=0; }
		// get tag rec
		if ( ! m_gotTagRec++ ) {
			// make the fake url
			char fbuf[1024];
			sprintf(fbuf,"gbeventhash%llu.com",mr->m_eventHash64 );
			m_fakeUrl.set ( fbuf );
			// note it
			//log("reindex: getting tag rec for %s",mr->ptr_ubuf);
			// now look that up
			if ( ! m_msg8a.getTagRec ( &m_fakeUrl ,
						   m_coll ,
						   true , // canonical lookup?
						   m_niceness ,
						   this ,
						   updateTagTermsWrapper ,
						   &m_tagRec ) )
				return false;
		}
		// get revdb rec
		if ( ! m_gotRevdbRec++ ) {
			// note it
			//log("reindex: getting revdbrec for %s",mr->ptr_ubuf);
			// make the key range
			key_t sk = g_revdb.makeKey ( mr->m_docId , true  );
			key_t ek = g_revdb.makeKey ( mr->m_docId , false );
			// shortcut
			Msg0 *m = &m_msg0;
			// this is a no-split lookup by default now
			if ( ! m->getList ( -1    , // hostId
					    0     , // ip
					    0     , // port
					    0     , // maxCacheAge
					    false , // add to cache?
					    RDB_REVDB ,
					    m_coll      ,
					    &m_revdbList ,
					    sk          ,
					    ek          ,
					    1    , // minRecSizes in bytes
					    this ,
					    updateTagTermsWrapper ,
					    m_niceness    ))
				return false;
		}
		// process it
		if ( ! m_madeList++ ) {
			// note it
			//log("reindex: making meta list for %s",mr->ptr_ubuf);
			// returns false and sets g_errno on error
			// . makes a metalist for us to add to datedb
			// . adds in the new tag terms
			// . includes a new revdb record that is basically
			//   the old revdb record plus the new tag terms
			if ( ! getMetaList ( mr->m_docId , 
					     mr->m_eventId , 
					     &m_tagRec,
					     &m_revdbList ,
					     m_niceness ,
					     &m_addBuf ) )
				return true;
			// shortcut
			m_metaList     = m_addBuf.getBufStart();
			m_metaListSize = m_addBuf.getBufUsed();
			// debug log
			log("reindex: event reindex d=%llu eid=%lu "
			    "eventhash=%llu",
			    mr->m_docId,mr->m_eventId,mr->m_eventHash64);
		}
		// add using msg4
		if ( ! m_addedList++ ) {
			// note it
			//log("reindex: adding meta list for %s",mr->ptr_ubuf);
			if ( ! m_msg4.addMetaList ( m_metaList ,
						    m_metaListSize ,
						    m_coll ,
						    this ,
						    updateTagTermsWrapper ,
						    m_niceness ) )
				return false;
		}
		// return lock just for our uh48
		if ( ! m_removeLock++ ) {
			// note it
			log("reindex: removing lock for %llu",mr->m_urlHash48);
			if ( ! m_msg12.removeAllLocks ( ) )
				return false;
		}
		// update
		m_i++;
		// reset for next guy
		m_gotLock     = 0;
		m_gotTagRec   = 0;
		m_gotRevdbRec = 0;
		m_madeList    = 0;
		m_addedList   = 0;
		m_removeLock  = 0;
	}
	// flush and wait
	// TODO: add this back one we code it up
	flushMsg4Buffers ( NULL , NULL );
	//if ( ! m_flushedList++ && 
	//     ! m_msg4.flushMsg4Buffers ( this , 
	//				 updateTagTermsWrapper ) )
	//	return false;
	// all done
	return true;
}

// . put the meta list into "addBuf"
// . returns false and sets g_errno on error
bool Msg1d::getMetaList ( long long docId , 
			  long eventId , 
			  TagRec *egr ,
			  RdbList *oldList ,
			  long niceness ,
			  SafeBuf *addBuf ) {

	// . now make the positive tag terms
	// . put our new tag hashes in here
	HashTableX dt;
	char dtbuf[1524];
	// these keys are 12 bytes here
	dt.set ( 12,4,64,dtbuf,1524,false,niceness,"msg1dbuf");
	// hash without prefix (no gbtag: junk)
	if ( ! hashEventTagRec ( egr ,
				 eventId ,
				 &dt ,
				 NULL , // pbuf
				 NULL , // wts
				 NULL , // wbuf
				 niceness ) )
		return false;

	// point to the OLD meta list (inside the revdb record)
	char *om    = NULL;
	long  osize = 0;
	char *omend = NULL;
	// . only point to records in list record if there
	// . taken from XmlDoc.cpp:15228
	if ( oldList->m_listSize > 16 ) {
		om    = oldList->m_list + 12 + 4;
		osize = *(long *)(oldList->m_list + 12);
		omend = om + osize;
	}

	// how much space in new revdb rec that will replace "oldList"?
	long need = osize + dt.m_numSlotsUsed * (1+16);
	// make new revdb rec from that
	if ( ! m_rr.reserve ( need ) ) return false;

	// scan the meta list
	for ( char *p = om ; p < omend ; ) {
		// breathe
		QUICKPOLL(niceness);
		// save this
		char byte = *p;
		// get the rdbid for this rec
		char rdbId = byte & 0x7f;
		// skip that
		p++;
		// get the key size
		long ks = getKeySizeFromRdbId ( rdbId );
		// get that
		char *k = p;
		// store it in new revdb rec
		if ( ! m_rr.pushChar ( rdbId ) ) return false;
		// and key
		if ( ! m_rr.safeMemcpy ( k , ks ) ) return false;
		// unlike a real meta list, this meta list has
		// no data field, just rdbIds and keys only! because
		// we only use it for deleting, which only requires
		// a key and not the data
		p += ks;
		// skip now
		if ( rdbId != RDB_DATEDB ) continue;
		// . date must be 0x7fff**** in order to be a tag term
		// . this is because we hacked the high bit on in 
		//   hashEventTagRec in XmlDoc.cpp, then the date is 
		//   complemented by g_datedb.makeKey()
		// . so skip this datedb key if not a tag term
		if ( k[9] != (char)0x7f || k[8] != (char)0xff ) continue;
		// remove tag term from new list, we'll add it back later below
		m_rr.incrementLength ( -ks -1 );
		// add it as negative key, first the rdbId
		if ( ! addBuf->pushChar ( rdbId ) ) return false;
		// make key negative by clearing LSB
		k[0] &= 0xfe;
		// add this negative key to the msg4 addlist buffer
		if ( ! addBuf->safeMemcpy ( k , ks ) ) return false;
	}

	// . scan each term in with prefix
	// . the key formation code taken from XmlDoc::addTableDate()
	for ( long i = 0 ; i < dt.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if empty
		if ( ! dt.m_flags[i] ) continue;
		// get its key
		key96_t *k = (key96_t *)dt.getKey ( i );
		// get its value
		uint32_t v = *(uint32_t *)dt.getValueFromSlot ( i );
		// convert to 8 bits
		v = score32to8 ( v );
		// . make the meta list key for datedb
		// . a datedb key (see Datedb.h)
		// . date is fake in that it is like the dates in
		//   XmlDoc::hashEventTagRec(), it is an eventid range
		//   with the tagterm bit (0x80000000) set
		key128_t mk = g_datedb.makeKey ( k->n0  , // termId
						 k->n1  , // date
						 v      , // score (8 bits)
						 docId  ,
						 false  );// del key?
		// add that to list, first the rdbid
		if ( ! addBuf->pushChar ( (char)RDB_DATEDB ) ) return false;
		// then the key
		if ( ! addBuf->safeMemcpy ( (char *)&mk , 16 ) ) return false;
		// also add to the new revdb rec
		if ( ! m_rr.pushChar ( (char)RDB_DATEDB ) ) return false;
		// and key to that
		if ( ! m_rr.safeMemcpy ( (char *)&mk , 16 ) ) return false;
	}

	// now for the final metalist to add, we will be adding the new
	// revdb record to RDB_REVDB and we will be adding a bunch of
	// RDB_DATEDB records to datedb.

	// partap got a revdb record of zero size... strange
	if ( oldList->m_listSize > 0 ) {
		// revdb rec
		if ( ! addBuf->pushChar ( (char)RDB_REVDB ) ) return false;
		// revdb key
		if ( ! addBuf->safeMemcpy ( oldList->m_list , 12 ) ) 
			return false;
		// and datasize
		long dataSize = m_rr.getBufUsed();
		// store that after key
		if ( ! addBuf->safeMemcpy ( (char *)&dataSize , 4 ) ) 
			return false;
		// append the data of the revdb record then
		if ( ! addBuf->cat ( m_rr ) ) return false;
	}
	else {
		log("reindex: strange. revdb rec is empty.");
	}

	// free it to save mem
	m_rr.purge();

	return true;
}		
*/
