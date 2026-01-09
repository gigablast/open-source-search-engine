//#include "PageTurk.h"
#include "XmlDoc.h"
#include "SafeBuf.h"

static bool sendReply ( SafeBuf *sb ) ;

class State60 {
public:
	Msg0    m_msg0;
	RdbList m_list;
	char    m_coll[MAX_COLL_LEN+1];
	char    m_user[MAX_USER_SIZE];
	//char    m_url[MAX_URL_LEN+1];
	// the docid to edit
	int64_t m_docId;
};

bool sendPageTurk ( TcpSocket *s , HttpRequest *r ) {
	// get the current timestamp
	int32_t now = getTimeGlobal ();
	


	char *coll =  r->getString("c");
	if ( ! coll )
		return g_httpServer.sendErrorReply( s, 500, "No collection");
		

	// make a state for callback
	State60 *st ;
	try {  st = new ( State60 ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log( "pgrank: new(%i): %s", sizeof(State60),
		     mstrerror(g_errno) );
		return g_httpServer.sendErrorReply(s,500,mstrerrno(g_errno));
	}
	mnew ( st , sizeof(State60) , "PageTurk" );

	// get username
	char *username = r->getStringFromCookie("username", NULL);
	if ( !username ) username = r->getString("username",NULL);
	if ( !username ) username = r->getString("user",NULL);
	if ( !username ) username = r->getString("code",NULL);

	if ( ! username )
		return g_httpServer.sendErrorReply(s,500,"No username");
	int32_t ulen = gbsrlen(username);
	if ( ulen >= MAX_USER_SIZE )
		return g_httpServer.sendErrorReply(s,500,"Bad username");

			
	// save crap. don't we need to copy "r" into our own? yeah...
	st->m_s = s;
	// save username
	strcpy(st->m_username,username,ulen+1);
	// assume no url
	//st->m_url[0] = 0;
	// copy coll
	strcpy(st->m_coll,coll);
	// this is 1 to imply to edit a page
	st->m_editMode = r->getString("edit",0);
	st->m_docId    = r->getLongLong("docid",0LL);

	// get url
	//char *url = r->getString ("url", NULL);

	// if no url is given then present their stats page
	if ( ! edit ) return sendPageTurkStats (st);

	// copy url
	//strcpy ( st->m_url , url );

	// otherwise, send them the eval page
	return sendPageTurkEval (st);
}



bool sendPageTurkStats ( State60 *st ) {

	// . for this username, scan transdb (transactiondb) to get all their
	//   info, and then turn that into stats to display on this page.
	// . use the hash of the username for making the key. 32 bit hash
	//   is probably good enough!
	// . username hash will be in the hi bits, timestamp below that, that
	//   way we store by username first
	// . use a rolling incremental count to break up the time_t into up
	//   to 65535 intervals and keep the keys unique.
	//key128_t startKey=g_transdb.makeKey ( 0 , 0 , username );
	//key128_t endKey  =g_transdb.makeKey ( 0x7fffffff, 0xffff, username );
	key128_t startKey = g_transdb.makeStartKey (username);
	key128_t endKey   = g_transdb.makeEndKey   (username);
	// get all the transactions associated with this username
	if ( ! st->m_msg0.getList ( -1          , // hostId
				    -1          , // ip
				    -1          , // port
				    0           , // maxAge        ,
				    false       , // addToCache?
				    RDB_TRANSDB , // rdbId
				    st->m_coll  , // coll
				    &st->m_list ,
				    &startKey   ,
				    &endKey     ,
				    200000      , // minRecSizes (get all!)
				    st          , // state
				    gotTransdbListWrapper ,
				    0           )) // niceness
		// return false if that blocked on us
		return false;

	// error?
	if ( g_errno ) {
		log("turk: error reading transdb for stats: %s",
		    mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerrno(g_errno));
	}
		
	// got the list. this should send a page back to httpserver
	gotTransdbList ( st );
}

// later on we can add ticket buying selling, commissions, etc. to this
// to replace the mysqldb on the front end possibly.
#define AT_RECEIVE_DOC   1 // recv a docid to edit or verify
#define AT_SUBMIT_DOC    2 // submit all your edits for the doc
#define AT_PASS_DOC      3 // when someone verifies your doc
#define AT_FAIL_DOC      4 // when someone verifies your doc
#define AT_ACCURACY_EVAL 5 // for last 100 or so docs
#define AT_CHARGE        6 // you charge flurbit. once per day.
#define AT_PAYMENT       7 // we pay you using paypal, every friday.
#define AT_LOGIN         8  
#define AT_LOGOUT        9  
#define AT_AUTO_LOGOUT   10  
#define AT_SKIP_DOC      11


class Trans {
public:
	char      m_actionType;
	int32_t      m_ip;
	float     m_number; // amount of money involved, or # of action pts.
	int64_t m_docId;
	char      m_desc[]; // username who verfied you, etc.
};

void gotListWrapper ( void *st ) { 
	gotTransdbList ( (State60 *) st );
}

// . displays the stats for a username
// . show stats for every day we have them for
// . in a big list
// . if they click the day display all docids evaluated for that day
// . show the accuracy for that day too
// . how many docs they edited
// . how many of those docs were verified by another
// . and if there was consensus
void gotTransdbList ( State60 *st ) {

	// get today's time range
	time_t now = getTimeGlobal();
	// get start of today
	time_t dayStart = now / (24*3600);

	SafeBuf sb;

	// int16_tcut
	TcpSocket *s = st->m_s;

	// make about 200k of mem to write into
	if ( ! sb.reserve ( 200000 ) ) 
		return g_httpServer.sendErrorReply(s,500,mstrerrno(g_errno));

	// print description so they can clikc a button to start the turk
	sb.safePrintf("<html>\n"
		      "<title>Event Editor</title>\n"
		      "<body>\n"
		      "<table width=\"100%%\" border=\"0\">\n"
		      "<tr><td style=\"background-color:#0079ba;\">\n"
		      "<center><font color=#00000>"
		      "<h2>Event Editor</h2>\n"
		      "</font></center></td>"
		      "</tr></table>");
	// print the content
	sb.safePrintf("<center><font size=4><blink>"
		      "<b><a href=\"/pageturk?c=%s&edit=1\">"
		      "Click here to start editing.</a></b></blink>"
		      "</font><br><i>Please take your "
		      "time to read the information below before you begin"
		      "</i><br><font color=\"red\" size=2> Warning: Adult "
		      "content might be presented to you."
		      " You should be above 18 years of age to continue."
		      "</center></font>",st->m_coll);

	sb.safePrintf("<font face=arial,sans-serif color=black size=3>"
		      "<p>By clicking <i>Start Voting</i>, you will be "
		       "presented with an interface for editing events. "
		      "The editor will display a modified web page that "
		      "contains one or more events. Each event's description "
		      "will be highlight with a blue background. You can "
		      "toggle whether a particular event is displayed by "
		      "clicking on that event's ID. You can highlight one or "
		      "multiple event descriptions at the same time. "
		      "</p><p>"
		      "By clicking on the section icons in the web page you "
		      "can tell the editor that a virtual fence should be "
		      "erected around that section. The fence will make sure "
		      "that event descriptions can not span across it. Each "
		      "event description must be fully contained either "
		      "inside or outside the fence. However, you can also "
		      "declare a section as a title section, which means that "
		      "the text that the title section contains is free to be "
		      "used by any event description."
		      "</p>\n"
		      "<p>When you are done erecting section fences, you "
		      "submit your changes. The more changes you make the "
		      "more points you earn. Other users may evaluate " 
		      "your edits for accuracy. You will be paid based on the "
		      "points you earn as well as your accuracy. All "
		      "transactions are listed in the table below.</p>"
		      "<p>You may not change your username or password "
		      "but you can change your email address. Your email "
		      "address will be used to pay you with PayPal every "
		      "Friday. Paypal fees will be deducted on your end. By "
		      "using this service you agree to all stated Terms & "
		      "Conditions.</p>"
		      "</font>\n");

	// get the user record
	User *uu = g_users.getUser ( username );
	// print out their info, like paypal email
	sb.safePrintf("<table>\n"
		      "<tr><td colspan=10><center>Your Info</center>"
		      "</td></tr>\n"
		      "<tr>"
		      "<td>Email</td>"
		      "<td><input type=text value=%s></td>"
		      "<td>email address used to pay with paypal</td>"
		      "</tr>\n"
		      "<tr><td colspan=10><input type=submit value=update>"
		      "</td></tr>\n"
		      "</table>\n" ,
		      uu->m_payPalEmail );

	// print your stats here now
	sb.safePrintf("<table>\n"
		      "<tr><td colspan=10><center>Your Stats</center>"
		      "</td></tr>\n"
		      "<tr>"
		      "<td>date</td>"
		      "<td>action</td>"
		      "<td>amount</td>"
		      "<td>desc</td>"
		      "</tr>\n");

	// int16_tcut
	RdbList *list = &st->m_list;

	int32_t lastDay        = -1;
	int32_t totalReceives  = 0;
	int32_t totalSubmits   = 0;
	int32_t totalPasses    = 0;
	int32_t totalFails     = 0;

	// scan the list
	for ( ; ! list->isExhausted() ; ) {
		// get rec
		char *rec      = list->getCurrentRecord();
		char *data     = list->getCurrentData();
		int32_t  dataSize = list->getCurrentDataSize();
		// skip that
		list->skipCurrentRecord();
		// skip if negative
		if ( (rec[0] & 0x01) == 0x00 ) continue;
		// get the time (global time - sync'd with host #0)
		time_t tt = g_transdb.getTimeStamp ( rec );
		// get day #
		int32_t daynum = tt / (24*3600);
		// is it today?
		bool isToday = ( daynum >= dayStart );
		// point to the Transaction
		Trans *trans = (Trans *)data;
		// if is today, print it out verbatim
		if ( isToday ) {
			// print it in html row format to match table above
			//printTrans ( &sb , rec );
			sb.safePrintf("<tr>");
			// make it into a nice date
			time_t dd = lastDay * 86400;
			struct tm *timeStruct = localtime ( &dd );
			char ppp[100];
			strftime(ppp,100,"%H:%M:%S",timeStruct);
			// print last days stats first
			sb.safePrintf("<td>%s</td>",ppp);
			// then stats
			if ( trans->m_actionType == AT_RECEIVE_DOC )
				sb.safePrintf("<td>receive</td>"
					      "<td>%"INT32" pts</td>"
					      "<td>docid=%"UINT64"</td>",
					      (int32_t)trans->m_number,
					      trans->m_docId);
			else if ( trans->m_actionType == AT_SUBMIT_DOC )
				sb.safePrintf("<td>submit</td>"
					      "<td>%"INT32" pts</td>"
					      "<td>docid=%"UINT64"</td>",
					      (int32_t)trans->m_number,
					      trans->m_docId);
			else if ( trans->m_actionType == AT_PASS_DOC )
				sb.safePrintf("<td>verify</td>"
					      "<td>%"INT32" pts</td>"
					      "<td>docid=%"UINT64" was verified "
					      "by user=\"%s\"</td>",
					      (int32_t)trans->m_number,
					      trans->m_docId,
					      trans->m_desc);
			else if ( trans->m_actionType == AT_FAIL_DOC )
				sb.safePrintf("<td>verify</td>"
					      "<td>%"INT32" pts</td>"
					      "<td>docid=%"UINT64" was deemed to "
					      "be incorrect "
					      "by user=\"%s\"</td>",
					      (int32_t)trans->m_number,
					      trans->m_docId,
					      trans->m_desc);
			else if ( trans->m_actionType == AT_ACCURACY_EVAL)
				sb.safePrintf("<td>accuracy eval</td>"
					      "<td>%.02f</td>"
					      "<td>docid=%"UINT64"</td>",
					      trans->m_number,
					      trans->m_docId);
			else if ( trans->m_actionType == AT_CHARGE)
				sb.safePrintf("<td>credit</td>"
					      "<td>%.02f</td>"
					      "<td>You made money.</td>",
					      trans->m_number);
			else if ( trans->m_actionType == AT_PAYMENT)
				sb.safePrintf("<td>payment</td>"
					      "<td>%.02f</td>"
					      "<td>We paid you.</td>",
					      trans->m_number);
			else if ( trans->m_actionType == AT_LOGIN)
				sb.safePrintf("<td>login</td>"
					      "<td>-</td>"
					      "<td>You logged in.</td>");
			else if ( trans->m_actionType == AT_LOGOUT)
				sb.safePrintf("<td>logout</td>"
					      "<td>-</td>"
					      "<td>You logged out.</td>");
			else if ( trans->m_actionType == AT_AUTO_LOGOUT)
				sb.safePrintf("<td>logout</td>"
					      "<td>-</td>"
					      "<td>You were auto "
					      "logged out.</td>");
			else {
				char *xx=NULL;*xx=0; }
			sb.safePrintf("</tr>\n");
			continue;
		}
		// if does not match last day, print out that last day's stats
		// and reset for next guy
		if ( daynum != lastDay && lastDay != -1 ) {
			// make it into a nice date
			time_t dd = lastDay * 86400;
			struct tm *timeStruct = localtime ( &dd );
			char ppp[100];
			strftime(ppp,100,"%b-%d-%Y",timeStruct);
			// print last days stats first
			sb.safePrintf("<td>%s</td>",ppp);
			// then stats
			sb.safePrintf("<tr>"
				      "<td>receive</td>"
				      "<td>%"INT32"</td>"
				      "<td>Total received</td>"
				      "</tr>\n",
				      totalReceives);
			sb.safePrintf("<tr>"
				      "<td>submit</td>"
				      "<td>%"INT32"</td>"
				      "<td>Total submitted</td>"
				      "</tr>\n",
				      totalSubmits);
			sb.safePrintf("<tr>"
				      "<td>pass</td>"
				      "<td>%"INT32"</td>"
				      "<td>Total accuracy tests passed</td>"
				      "</tr>\n",
				      totalPasses);
			sb.safePrintf("<tr>"
				      "<td>fail</td>"
				      "<td>%"INT32"</td>"
				      "<td>Total accuracy tests failed</td>"
				      "</tr>\n",
				      totalFails);
			// reset as well
			totalReceived = 0;
			totalSubmits  = 0;
			totalPasses   = 0;
			totalFails    = 0;
		}
		// remember last day # we processed for accumulating stats
		lastDay = daynum;
		// accum stats
		if ( trans->m_actionType == AT_RECEIVE_DOC )
			totalReceives++;
		if ( trans->m_actionType == AT_SUBMIT_DOC )
			totalSubmits++;
		if ( trans->m_actionType == AT_PASS_DOC )
			totalPasses++;
		if ( trans->m_actionType == AT_FAIL_DOC )
			totalFails++;
	}

	sb.safePrintf("</body></html>\n");

	sendReply ( &sb );
}


// . just keep it simple and store gbunturked for each page that has
//   unturked events.
// . have it store gbsemiturked if similar pages from its site have been turked
// . have it store gbturked if it has been turked itself
// . if page events change then we must re-turk it!
// . just grabbing the pages based on docid should make us turk the
//   most popular sites first on avg...
bool sendPageTurkEval ( State60 *st ) {

	// if we already have a docid, the display the editor page
	if ( st->m_docId ) return displayEditorPage ( st );

	// . otherwise, do a search to get the best page
	int64_t termId = hash64n("gbunturked");
	// event id range is forced to 1 to 1 for this one since special
	
	key128_t startKey = g_datedb.makeStartKey (termId);
	key128_t endKey   = g_datedb.makeEndKey   (termId);
	// get all the transactions associated with this username
	if ( ! st->m_msg0.getList ( -1          , // hostId
				    -1          , // ip
				    -1          , // port
				    0           , // maxAge        ,
				    false       , // addToCache?
				    RDB_DATEDB  , // rdbId
				    st->m_coll  , // coll
				    &st->m_list ,
				    &startKey   ,
				    &endKey     ,
				    200000      , // minRecSizes (get all!)
				    st          , // state
				    gotDatedbListWrapper ,
				    0           )) // niceness
		// return false if that blocked on us
		return false;

	// error?
	if ( g_errno ) {
		log("turk: error reading datedb for docs to turk: %s",
		    mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerrno(g_errno));
	}
		
	// got the list. this should send a page back to httpserver
	gotDatedbList ( st );
}

void gotListWrapper ( void *st ) { 
	gotDatedbList ( (State60 *) st );
}

// TODO: make Process.cpp save this one!
HashTableX g_turkLocks;

// key is the docid
class TurkLock {
public:
	char m_user[MAX_USER_SIZE];
	time_t m_lockTime;
};

void gotDatedbList ( State60 *st ) {

	// must only be run on host #0 since we need just one lock table
	if ( g_hostdb.m_myHost->m_hostId != 0 ) { char *xx=NULL;*xx=0; }

	// load turk lock table if we need to
	bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		if ( ! g_turkLocks.set(8,sizeof(TurkLock),256) )
			log("turk: failed to init turk lock table");
		if ( ! g_turkLocks.load(g_conf.m_dir,"turkdir/docidlocks.dat"))
			log("turk: failed to load turk lock table");
	}

	time_t now = getTimeGlobal();
	// int16_tcut
	RdbList *list = &st->m_list;
	// the best docid
	int64_t best = 0LL;
	// scan the list to get urls/docids to turk out
	for ( ; ! list->isExhausted() ; ) {
		// get rec
		char *k = list->getCurrentKey();
		// skip that
		list->skipCurrentRecord();
		// skip if negative
		if ( (k[0] & 0x01) == 0x00 ) continue;
		// get the docid
		int64_t docid = g_datedb.getDocId ( k );
		// skip if locked
		TurkLock *tt = (TurkLock *)g_turkLock.getValue(&docid);
		// if there check time
		if ( tt && now - tt->m_lockTime > 3600 ) {
			// remove it
			g_turkLock.removeKey(&docId);
			// nuke tt
			tt = NULL;
		}
		// if still there, skip it and try next one
		if ( tt ) continue;
		// ok, we got a good docid to dish out
		best = docId;
		break;
	}

	SafeBuf sb;

	// print description so they can clikc a button to start the turk
	sb.safePrintf("<html>\n"
		      "<title>Event Editor</title>\n"
		      "<body>\n"
		      "<table width=\"100%%\" border=\"0\">\n"
		      "<tr><td style=\"background-color:#0079ba;\">\n"
		      "<center><font color=#00000>"
		      "<h2>Event Editor</h2>\n"
		      "</font></center></td>"
		      "</tr></table>");

	// if we had no docid, give user an empty msg
	if ( ! best ) {
		sb.safePrintf("<center>Nothing currently available to edit. "
			      "Please try again later.</center>"
			      "</body></html>\n");
		sendReply ( &sb );
		return;
	}

	// lock it!
	TurkLock tt;
	strcpy ( tt.m_user , st->m_user );
	tt.m_lockTime = now;
	if ( ! g_lockTable.addLock ( &tt ) ) {
		sendErrorReply ( st , g_errno );
		return;
	}

	// . fetch the TitleRec
	// . a max cache age of 0 means not to read from the cache
	XmlDoc *xd = &st->m_xd;
	// . when getTitleRec() is called it will load the old one
	//   since XmlDoc::m_setFromTitleRec will be true
	// . niceness is 0
	xd->set3 ( best , st->m_coll , 0 );
	// if it blocks while it loads title rec, it will re-call this routine
	xd->setCallback ( st , processLoopWrapper );
	// good to go!
	return processLoop ( st );
}

bool sendReply ( SafeBuf *sb ) {
	// save this
	TcpSocket *s = st->m_s;
	// nuke state60
	mdelete ( st , sizeof(State60) , "turk1" );
	delete (st);
	// get page to send back
	char *buf = sb->getBufStart();
	// does this include the \0???
	int32_t  bufLen = sb->length();
	// remove \0 i guess if we had one
	if ( bufLen > 0 && buf[bufLen-1] == '\0' ) bufLen--;
	// and send that back
	bool  status = g_httpServer.sendDynamicPage (s,
						     buf,
						     bufLen,
						     -1, // cachetime
						     false, // POSTReply?
						     "text/html",
						     -1, // httpstatus
						     NULL,  // cookie
						     "utf8" ); // charset
	// and convey the status
	return status;
}

// returns true
bool sendErrorReply ( void *state , int32_t err ) {
	// ensure this is set
	if ( ! err ) { char *xx=NULL;*xx=0; }
	// get it
	State60 *st = (State60 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_s;

	char tmp [ 1024*32 ] ;
	sprintf ( tmp , "<b>had server-side error: %s</b><br>",
		  mstrerror(g_errno));
	// nuke state60
	mdelete ( st , sizeof(State60) , "turk1" );
	delete (st);
	// erase g_errno for sending
	//g_errno = 0;
	// . now encapsulate it in html head/tail and send it off
	//return g_httpServer.sendDynamicPage ( s , tmp , gbstrlen(tmp) );
	return g_httpServer.sendErrorReply ( s, err, mstrerror(err) );
}


void processLoopWrapper ( void *state ) {
	processLoop ( state );
}

// returns false if blocked, true otherwise
bool processLoop ( void *state ) {
	// get it
	State60 *st = (State60 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_socket;
	// get it
	XmlDoc *xd = &st->m_xd;

	if ( ! xd->m_loaded ) {
		// setting just the docid. niceness is 0.
		xd->set3 ( st->m_docId , st->m_coll , 0 );
		// callback
		xd->setCallback ( state , processLoop );
		// . and tell it to load from the old title rec
		// . if it returns false it blocked and will call our callback
		//   processLoop() when it completes
		if ( ! xd->loadFromOldTitleRec ( ) ) return false;
	}

	if ( g_errno ) return sendErrorReply ( st , g_errno );


	// get the utf8 content
	char **utf8 = xd->getUtf8Content();
	//int32_t   len  = xd->size_utf8Content - 1;
	// wait if blocked???
	if ( utf8 == (void *)-1 ) return false;
	// strange
	if ( xd->size_utf8Content<=0) return sendErrorReply(st,EBADENGINEER );
	// alloc error?
	if ( ! utf8 ) return sendErrorReply ( st , g_errno );

	// get this host
	Host *h = g_hostdb.getHost ( g_hostdb.m_hostId );
	if ( ! h ) return sendErrorReply ( st , EBADENGINEER );
	
	// make it into an editable page now for the turk guy
	sendTurkPageReply ( st );
}

void xdcallback ( void *state ) {
	State60 *st = (State60 *)state;
	sendTurkPageReply ( st );
}

bool sendTurkPageReply ( State60 *st ) {

	XmlDoc *xd = &st->m_xd;
	//char *content    = xd->ptr_utf8Content;
	//int32_t  contentLen = xd->size_utf8Content - 1;

	// count the total number of EventDesc classes for all evids
	//char *evd = xd->ptr_eventData;
	//EventDisplay *ed = (EventDisplay *)evd;
	//char *addr = evd + (int32_t)ed->m_addr;
	//char timeZoneOffset = getTimeZoneFromAddr ( addr );

	// in case getSections() block come right back in
	xd->setCallback ( st , xdcallback );

	// . set niceness to 1 so all this processing doesn't slow queries down
	// . however, g_niceness should still be zero... hmmm...
	xd->m_niceness = 1;

	// default to 1 niceness
	st->m_niceness = 1;

	// now set the sections class
	Sections *ss = xd->getSections();

	// now for each section with alnum text, telescope up as far as 
	// possible without containing anymore alnum text than what it 
	// contained. set SEC_CONTROL bit. such sections will have the
	// 2 green/blue dots, that are used for turning on/off title/desc.
	// but really the indians will only turn off sections that should
	// not have a title/desc.
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(st->m_niceness);
		// skip if does not have text
		if ( si->m_firstWordPos < 0 ) continue;
		// otherwise, find biggest parent that contains just that text
		Section *p    = si->m_parent;
		Section *last = si;
		for ( ; p ; p = p->m_parent ) {
			if ( p->m_firstWordPos != si->m_firstWordPos ) break;
			if ( p->m_lastWordPos  != si->m_lastWordPos  ) break;
			last = p;
		}
		// set that bit then
		last->m_flags |= SEC_CONTROL;
		// and speed up the loop
		si = last;
	}

	// * now each SEC_CONTROL sections have a fence activated by a turker

	// * an event title or description can not span a fence. it must be
	//   confined within a fence. however, it is allowed to include
	//   title or description from a "title section".

	// * hold shift down to designate as title section when clicking it

	// * show the raw text of each event changing as you fence
	//   sections in or out.  show in a right frame.

	// * show list of events on page in the top frame. can toggle them
	//   all individually.

	// * and remove no-display from all tags so we can see everything.

	// * highlight addresses, not just dates.

	// * each section hash has its own unique bg color when activated

	// * with a single click, completely reject an event because:
	//   contains bad time, address, title or desc. specify which so
	//   we can improve our algo.

	// * when selecting an individual event, scroll to its tod...

	// * remove all color from webpage that we can so our colors show up

	// * remove all imgs. just src them to dev null.

	// * allow for entering a custom title for an event or all events
	//   that are or will ever appear on the page. 

	// * when displaying the text of the events, use hyphens to
	//   delineate the section topology. strike out text as a section
	//   fence is activated.

	// * when a section is activated is it easier to just redownload
	//   the whole text of the page? maybe just the text frame?

	// * clicking on an individual sentence section should just remove
	//   that sentence. that is kinda a special content hash removal
	//   tag. like "Click here for video."

	// * when an event id is selected i guess activate its bgcolor to
	//   be light blue for all sentences currently in the event that
	//   are not in activated sections. (make exception for designated 
	//   title sections). so we need multiple tags for each events
	//   sentence div section. if sentence is split use multiple div tags
	//   then to keep the order. so each event sentence would have 
	//   <div ev1=1 ev2=1 ev10=1>...</div> if it is in event ids 1,2 and
	//   10. that way we can activate it when one of those event ids is
	//   activated.


	SafeBuf sb;

	// int16_tcuts
	if ( ! xd->m_wordsValid ) { char *xx=NULL;*xx=0; }
	Words     *words = &xd->m_words;
	int32_t       nw    = words->getNumWords();
	char     **wptrs = words->getWords();
	int32_t      *wlens = words->getWordLens();
	nodeid_t  *tids  = words->getTagIds();

	// a special array for printing </div> tags
	char *endCounts = (char *)mcalloc ( nw ,"endcounts");
	if ( ! endCounts ) return sendErrorReply ( st , g_errno );


	// 
	// now loop over all the words. if word starts a section that has
	// SEC_CONTROL bit set, and print out the section hash and a color
	// tag to be activated if the turkey activates us.
	// CAUTION: word may start multiple sections.
	//
	for ( int32_t i = 0 ; i < nw ; i++ ) { 
		// get section ptr
		Section *sj = ss->m_sectionPtrs[i];
		// sanity check. sj must be first section ptr that starts @ a
		if ( sj && sj->m_a==i && sj->m_prev && sj->m_prev->m_a==i ) {
			char *xx=NULL;*xx=0; }
		// . does word #i start a section?
		// . if section is control, print out the control
		while ( sj && sj->m_a == i ) {
			// print this section's hash
			if ( sj->m_flags & SEC_CONTROL) {
				// after the turkeys have made all the edits
				// they need to submit the changes they made.
				// how can we get that data sent back to the
				// back end? we need to send back the colors
				// of the sections that have been activated
				// i guess. just do a loop over them.
				sb.safePrintf("<div nobreak gbsecid=%"UINT32" "
					      "bgcolor=#%"XINT32" "
					      "onclick=gbtogglecolor()>",
					      (uint32_t)sj->m_tagHash,
					      (uint32_t)sj->m_tagHash);
				// sanity check
				if ( sj->m_b < 0  ) { char *xx=NULL;*xx=0; }
				if ( sj->m_b > nw ) { char *xx=NULL;*xx=0; }
				// and inc the /div count for that word
				endCounts[sj->m_b-1]++;
			}
			// try next section too
			sj = sj->m_next;
		}
		// if this is a tag, remove any coloring
		if ( tids[i] ) {
		}
		// print the word, be it a tag, alnum, punct
		sb.safeMemcpy ( wptrs[i] , wlens[i] );
		// end a div tag?
		if ( ! endCounts[i] ) continue;
		// might be many so loop it
		for ( int32_t j = 0 ; j < endCounts[i] ; j++ )
			sb.safePrintf("</div>");
	}			







	return false;
}
