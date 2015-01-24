//
// TO AUTOMATE:
//
// instead of inserting default values into input tags in processLoop24()
// generate an HTTP reply and send it back. we have to use
// <form enctype=multipart/form-data ...> to support the uploading of images
// so that each input tag will be it's own Content-Type:... along with
// a boundary delimeter. make a fake form in the <form> tag in here 
// with that enctype and you can see the multipart formatting.
//



#include "gb-include.h"

#include "XmlDoc.h"
#include "HttpServer.h"
#include "Msg1.h"
#include "Msg20.h"

void processLoop24 ( void *state );
bool printFilledOutForm ( class State24 *st , char *coll );
void gotSearchResults ( void *state ) ;
// from PageEvents.cpp:
bool printEventTitle ( SafeBuf &sb , Msg20Reply *mr , class State7 *st ) ;
bool printEventSummary ( SafeBuf &sb , class Msg20Reply *mr , int32_t width ,
			 int32_t minusFlags , int32_t requiredFlags , 
			 class State7 *st , ExpandedResult *er ,
			 int32_t maxChars );

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

enum InputType {
	IT_EVENT_TITLE = 1,
	IT_EVENT_DESC  = 2,
	IT_EVENT_URL   = 3,
	IT_EVENT_START_DATE1 = 4,
	IT_EVENT_END_DATE1   = 5
};

class State24 {
public:

	// . put this junk in top left frame
	// . this is the url of the form we have selected
	Url     m_formUrl;
	// . for querying the form urls themselves
	// . we treat them as events but index the gbeventform:1 term
	// . TODO: do this later. for now just use a simple input box
	//   to set m_formUrl directly
	//Query  m_formQuery;

	TagRec m_tagRec;
	Msg8a  m_msg8a;

	// . print the form itself in the bottom left frame from m_formUrl
	XmlDoc m_formDoc;
	// . this describes the form for m_formUrl
	// . it maps tag hashes to input types, like IT_EVENT_TITLE
	//   or IT_EVENT_DESC or IT_EVENT_URL etc.
	// . its is stored in tagdb for the form url
	// . it is a string whose format is like "1:12abef;2:34cd01;..."
	//   which is InputType:32bitValue;...
	// . we use these values to make a dropdown menu that popups up
	//   over each visible form element when you right-click on it
	//   and the default value is the one in m_inputMap.
	SafeBuf m_formInputMap;

	collnum_t m_collnum;

	// . put this junk in top right frame. the query PLUS search results.
	//   we select the event from the search results to put into the form.
	// . query #1 is for querying events to fill in the forms
	Query   m_eventQuery;
	// the location
	SafeBuf m_eventWhere;
	// radius from location in miles
	int32_t    m_eventRadius;

	// then put the event search result pageget.cpp into the
	// bottom right frame.
	int64_t m_formDocId;
	int32_t      m_formEventId;
	int32_t      m_formClockSet;

	TcpSocket   *m_socket;
	HttpRequest  m_hr;
	
	SafeBuf m_sb;

	SafeBuf m_tbuf;
	Msg1 m_msg1;
	RdbList m_tagList;

	Msg20 m_msg20;
	Msg20Request m_msg20Request;
};

static bool sendErrorReply ( void *state , int32_t err ) {
	// ensure this is set
	if ( ! err ) { char *xx=NULL;*xx=0; }
	// get it
	State24 *st = (State24 *)state;
	// get the tcp socket from the state
	TcpSocket *s = st->m_socket;
	char tmp [ 1024*32 ] ;
	sprintf ( tmp , "<b>had server-side error: %s</b><br>",
		  mstrerror(g_errno));
	// nuke state8
	mdelete ( st , sizeof(State24) , "pagesubmit" );
	delete (st);
	// erase g_errno for sending
	//g_errno = 0;
	return g_httpServer.sendErrorReply ( s, err, mstrerror(err) );
}

static bool sendReply ( void *state ) {
	// get it
	State24 *st = (State24 *)state;
	SafeBuf *sb = &st->m_sb;
	// now encapsulate it in html head/tail and send it off
	g_httpServer.sendDynamicPage( st->m_socket , 
				      sb->getBufStart(), 
				      sb->length() ,
				      -1, //cachtime
				      false ,//postreply?
				      NULL, //ctype
				      -1 , //httpstatus
				      NULL,//cookie
				      "utf-8");
	// nuke state8
	mdelete ( st , sizeof(State24) , "pagesubmit" );
	delete (st);
	return true;
}

//static void gotTagRec24 ( void *state ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . the event submission tool
bool sendPageSubmit ( TcpSocket *s , HttpRequest *hr ) {

	// make a state
	State24 *st = NULL;
	try { st = new (State24); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageParser: new(%i): %s", 
		    sizeof(State24),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,
						   mstrerror(g_errno));}
	mnew ( st , sizeof(State24) , "PageParser" );

	// save socket ptr
	st->m_socket = s;
	st->m_hr.copy ( hr );

	// must be local
	if ( ! hr->isLocal() ) return sendErrorReply(st,ENOTLOCAL);

	// get the collection
	int32_t collLen;
	char *coll = hr->getString ( "c" , &collLen ,NULL );
	if ( ! coll ) coll = g_conf.m_defaultColl;
	st->m_collnum = g_collectiondb.getCollnum ( coll );

	// set query
	int32_t eqlen;
	char *eq = hr->getString("q",&eqlen,NULL);
	if ( eq ) st->m_eventQuery.set ( eq , 2 );

	// form url
	int32_t fulen;
	char *fu = hr->getString("formurl",&fulen,NULL);
	if ( fu ) st->m_formUrl.set(fu);

	st->m_formDocId = hr->getLongLong("formdocid",0LL);
	st->m_formEventId = hr->getLong("formeventid",0);
	st->m_formClockSet = hr->getLong("formclockset",0);


	// use now if its 0
	if ( st->m_formClockSet == 0 )
		st->m_formClockSet = getTimeGlobal();

	// if we are the form iframe
	if ( hr->getLong("showform",0) ) 
		// return false if blocked
		return printFilledOutForm ( st , coll );

	// header
	SafeBuf *sb = &st->m_sb;

	sb->safePrintf("<meta http-equiv=\"Content-Type\" "
		      "content=\"text/html; charset=utf-8\">\n");


	/*
	SafeBuf cubuf;
	st->m_hr.getCurrentUrl ( cubuf );
	char *cu = cubuf.getBufStart();

	// . when this page loads, save the url to tagdb if we should
	// . i took this out since i am tring to do this without mappings
	if ( strstr (cu,"&save=1") ) {
		// make a tagrec
		SafeBuf *tbuf = &st->m_tbuf;
		tbuf->addTag3 ( fu , // mysite
				"eventformurl", // tagname
				getTimeGlobal(), // now
				"bridge",
				0, // ip
				cu ,// data is the url
				RDB_TAGDB );
		// use the list we got
		RdbList *list = &st->m_tagList;
		key128_t startKey;
		key128_t endKey;
		startKey.setMin();
		endKey.setMax();
		// set it from safe buf
		list->set ( tbuf->getBufStart() ,
			    tbuf->length() ,
			    NULL ,
			    0 ,
			    (char *)&startKey ,
			    (char *)&endKey  ,
			    -1 ,
			    false ,
			    false ,
			    sizeof(key128_t) );
		// . just use TagRec::m_msg1 now
		// . no, can't use that because tags are added using 
		//   SafeBuf::addTag() which first pushes the rdbid, so we 
		//   gotta use msg4
		if ( ! st->m_msg1.addList ( list ,
					    RDB_TAGDB ,
					    coll ,
					    st ,
					    gotTagRec24 ,
					    false ,
					    0 ) ) // st->m_niceness ) )
			return false;
		// . if addTagRecs() doesn't block then sendReply right away
		// . this returns false if blocks, true otherwise
		gotTagRec24 ( st );
		return true;
	}

	// just get the tag rec otherwise
	TagRec *gr = &st->m_tagRec;
	// lookup tag rec for form url
	if ( ! st->m_msg8a.getTagRec ( &st->m_formUrl ,
				       coll,
				       true,
				       0, // niceness
				       st ,
				       gotTagRec24 ,
				       gr ) )
		return false;

	gotTagRec24 ( st );
	return true;
}

void gotTagRec24 ( void *state ) {

	State24 *st = (State24 *)state;
	SafeBuf *sb = &st->m_sb;

	TagRec *gr = &st->m_tagRec;

	SafeBuf cubuf;
	st->m_hr.getCurrentUrl ( cubuf );
	char *cu = cubuf.getBufStart();

	// if we were fetching a tag rec for a form url, let's reload the
	// page then!!
	if ( strstr ( cu,"&reload=1") ) {
		// use the tag rec
		char *newurl = gr->getString( "eventformurl",NULL);
		if ( newurl ) {
			// send a page redirect
			sb->safePrintf("<meta name=redirect value=\"%s\">",
				       newurl);
			sendReply(st);
			return;
		}
	}
	*/


	sb->safePrintf(//"<td valign=top>"
		       "<form name=topform method=post "
		       //"enctype=\"multipart/form-data\" "
		       "action=/>"
		       "<input type=hidden name=form value=1>"
		       "<input type=text name=formurl id=formurl "
		       "size=100 "
		       "value=\"%s\">"
		       "<br>"
		       //"<input type=file name=stuff>"
		       //"<br>"
		       //"<input type=submit value=submit>"
		       "</form>"
		       //"</td>"
		      , st->m_formUrl.getUrl() );


	// parse these out of the url!
	char *query = "";
	char *location = "";
	int32_t  radius = 30;

	// put in a div that scrolls like an iframe
	sb->safePrintf("<iframe " //<div "
		       "id=iframe1 "
		       "style=\""
		       "width:800px;"
		       "height:95%%;"
		       "overflow:scroll;"
		       "display:inline-block;"
		       "\" "
		       // make this gk144 so we get the search results from
		       // there! we will need to send our msg20 that
		       // we use to generate the 2nd iframe to gk144 as well.
		       "src=\"http://10.5.54.154:8000/?"
		       "forform=1&showpersonal=0&usefbid=0"
		       );
	sb->safePrintf("&formurl=");
	sb->urlEncode(st->m_formUrl.getUrl());
	sb->safePrintf("&q=");
	sb->urlEncode ( query );
	sb->safePrintf("&wherebox=");
	sb->urlEncode ( location );
	sb->safePrintf("&radius=%"INT32"\">",radius);

	sb->safePrintf("</iframe>");

	// put in a div that scrolls like an iframe
	sb->safePrintf("<iframe " // <div "
		       "id=iframe2 "
		       "style=\""
		       "width:800px;"
		       "height:95%%;"
		       "overflow:scroll;"
		       "display:inline-block;"
		       "\" "
		       // . make this local for debugging easily!
		       // . that way we do not have to keep rolling out
		       //   changes to the gk144 cluster but we can use
		       //   its events to populate these forms. HOWEVER
		       //   it will still need to send its msg20 to gk144,
		       //   UNLESS we supplied the msg20 here, maybe
		       //   encoded... let's first try sending the msg20
		       //   request form titan to gk144 though.
		       "src=\"http://10.5.1.203:8000/?form=1&"
		       "showform=1&"
		       "formdocid=%"INT64"&"
		       "formeventid=%"INT32"&"
		       "formclockset=%"UINT32"&"
		       "formurl="
		       , st->m_formDocId
		       , st->m_formEventId
		       , st->m_formClockSet
		       );
	sb->urlEncode(st->m_formUrl.getUrl());
	sb->safePrintf("\">");



	sb->safePrintf("</iframe>");

	// now encapsulate it in html head/tail and send it off
	sendReply ( st );

	return true;
}




static bool gotMsg20Result ( void *state ) ;

// . assume we are an event submission form
// . print ourselves out into "sb"
// . detect event form inputs and put a dropdown that shows up on a right
//   click that allows the admin to change the type of input form from
//   what we've detected it to be. i.e. startdate|title|description|...
bool printFilledOutForm ( State24 *st , char *coll ) {

	char *fu = st->m_formUrl.getUrl();
	if ( ! fu || ! fu[0] ) {
		g_errno = EBADURL;
		return sendErrorReply ( st , g_errno );
	}

	//
	// get the event we are putting into the form
	//
	Msg20Request *rr = &st->m_msg20Request;
	rr->reset();
	rr->m_docId = st->m_formDocId;
	rr->m_eventId = st->m_formEventId;
	//rr->ptr_coll = coll;
	//rr->size_coll = gbstrlen(coll)+1;
	// now we are going to gk144, use "main"
	rr->ptr_coll = "main\0";
	rr->size_coll = 5;
	rr->m_callback = gotMsg20Result;
	rr->m_state = st;
	rr->m_getEventSummary = 1;

	rr->m_clockSet = st->m_formClockSet;

	// HACK: send to gk144!!! it has all the events, not titan!
	//rr->m_hostIP = atoip("10.5.54.154");
	//rr->m_hostUDPPort = 9000;

	// send to gk144 cluster
	rr->m_hostdb = &g_hostdb2;

	// also get the search result we are using to populate this form
	if ( st->m_formDocId > 0 && ! st->m_msg20.getSummary ( rr ) )
		return false;

	// we got it
	return gotMsg20Result ( st );
}

bool gotMsg20Result ( void *state ) {

	State24 *st = (State24 *)state;

	// set this up for passing to XmlDoc::set4()
	SpiderRequest sreq;
	sreq.reset();
	strcpy(sreq.m_url,st->m_formUrl.getUrl());
	int32_t firstIp = hash32n(sreq.m_url);
	sreq.setKey( firstIp, 0LL, false );
	sreq.m_isPageParser  = 0; // was 1
	sreq.m_isPageSubmit  = 1;
	sreq.m_hopCount      = 0;//st->m_hopCount;
	sreq.m_hopCountValid = 1;
	sreq.m_fakeFirstIp   = 0;//1;
	sreq.m_firstIp       = 0;//firstIp;
	sreq.m_domHash32     = st->m_formUrl.getDomainHash32();
	sreq.m_siteHash32    = st->m_formUrl.getHostHash32();

	XmlDoc *fd = &st->m_formDoc;

	//Msg20Request *rr = &st->m_msg20Request;


	char *coll = g_collectiondb.getColl(st->m_collnum);

	// . use the enormous power of our new XmlDoc class
	// . this returns false if blocked
	// . also gets XmlDoc::m_tagRec which contains the input mapping
	if ( ! fd->set4 ( &sreq      ,
			  NULL       ,
			  coll ,
			  NULL       ,  // pbuf
			  1          ,  // niceness
			  NULL       ,  // content
			  false      ,  // deletefromindex
			  0          ,  // forced ip
			  CT_HTML    )) // contentType
		// return error reply if g_errno is set
		return sendErrorReply ( st , g_errno );
	// make this our callback in case something blocks
	fd->setCallback ( st , processLoop24 );

	processLoop24 ( st );
	// this should always block
	return false;
}

#define FI_VENUE 1
#define FI_TITLE 2
#define FI_DESC  3
#define FI_DATE_START 4
#define FI_DATE_END   5
#define FI_DOW 6
#define FI_URL 7
#define FI_EMAIL 8
#define FI_PHONE 9
#define FI_STREET 10
#define FI_CITY 11
#define FI_STATE 12
#define FI_ZIP 13
#define FI_COUNTRY 14
#define FI_TIME_START 15
#define FI_TIME_END 16
#define FI_PRICE 17
#define FI_MY_NAME 18
#define FI_MY_PHONE 19
#define FI_MY_EMAIL 20
#define FI_IMAGE 21

void processLoop24 ( void *state ) {

	// cast it
	State24 *st = (State24 *)state;
	// get the xmldoc
	XmlDoc *fd = &st->m_formDoc;

	Words *w = fd->getWords();
	// blocked?
	if ( w == (void *)-1 ) return;
	// error?
	if ( ! w ) {
		sendErrorReply ( st , g_errno );
		return;
	}
	Sections *sx = fd->getSections();
	if ( sx == (void *)-1 ) return;
	if ( ! sx ) {
		sendErrorReply ( st , g_errno );
		return;
	}

	SafeBuf *sb = &st->m_sb;
	sb->safePrintf("<base href=\"%s\">", fd->getFirstUrl()->getUrl() );

	// is this basically in PageEvents.cpp?
	Msg20Reply *mr = st->m_msg20.getReply();

	if ( ! mr ) {
		sb->safeMemcpy ( fd->ptr_utf8Content,
				 fd->size_utf8Content - 1 );
		sendReply ( st );
		return;
	}

	// this logic taken from Msg40.cpp!!
	int32_t ni = mr->size_eventDateIntervals/sizeof(Interval);
	Interval *ii = (Interval *)mr->ptr_eventDateIntervals;
	int32_t timeStart = 0;
	int32_t timeEnd   = 0;
	for ( int32_t j = 0 ; j < ni ; j++ ) {
		if ( mr->m_prevStart >= 0 &
		     (uint32_t)ii[j].m_a <
		     (uint32_t)mr->m_prevStart )
			continue;
		if ( ii[j].m_a < st->m_formClockSet ) continue;
		timeStart = ii[j].m_a;
		timeEnd   = ii[j].m_b;
		break;
	}
	int32_t start1 = timeStart;
	bool isDST1 = getIsDST (start1,mr->m_timeZoneOffset);
	start1 += mr->m_timeZoneOffset * 3600;
	if ( mr->m_useDST && isDST1 ) start1 += 3600;
	struct tm *timeStruct = gmtime ( &start1 );
	ExpandedResult er;
	er.m_mapi = 0;
	er.m_timeStart = timeStart;
	er.m_timeEnd   = timeEnd;
	er.m_dayNum1 = timeStruct->tm_mday;
	er.m_month1  = timeStruct->tm_mon;
	er.m_year1   = timeStruct->tm_year+1900;
	er.m_hour1   = timeStruct->tm_hour;
	er.m_min1    = timeStruct->tm_min;
	er.m_dow1    = timeStruct->tm_wday;
	time_t start2 = timeEnd;
	bool isDST2 = getIsDST (start2,mr->m_timeZoneOffset);
	start2 += mr->m_timeZoneOffset * 3600;
	if ( mr->m_useDST && isDST2 ) start2 += 3600;
	timeStruct = gmtime ( &start2 );
	er.m_dayNum2 = timeStruct->tm_mday;
	er.m_month2  = timeStruct->tm_mon;
	er.m_year2   = timeStruct->tm_year+1900;
	er.m_hour2   = timeStruct->tm_hour;
	er.m_min2    = timeStruct->tm_min;
	er.m_dow2    = timeStruct->tm_wday;
	

	// parse out address (from PageEvents.cpp::printEventAddress)
	char *addr = mr->ptr_eventAddr;
	char *name1  ;
	char *name2  ;
	char *suite  ;
	char *street ;
	char *city   ;
	char *adm1   ;
	char *zip    ;
	char *country;
	double lat,lon;
	// this now makes "city" etc point into a static buffer, beware!
	setFromStr2 ( addr, &name1 , &name2, &suite, &street, &city, 
		      &adm1, &zip, &country,&lat , &lon );

	// override with this
	name1 = mr->ptr_eventBestPlaceName;
	

	// get the event display for this event id
	//EventDisplay *ed = fd->getEventDisplay ( st->m_eventId );

	// use the msg20reply now

	
	//TagRec *gr = &fd->m_tagRec;

	// print out each word
	int32_t nw = w->getNumWords();
	char **wptrs = w->getWords();
	int32_t  *wlens = w->getWordLens();
	int64_t *wids = w->getWordIds();
	nodeid_t *tids = w->getTagIds();

	for ( int32_t i = 0 ; i < nw ; i++ ) {

		// if no event selected, skip all this!
		//if ( ! mr ) {
		//	sb->safeMemcpy ( wptrs[i], wlens[i] );
		//	continue;
		//}

		//
		// . if its the <form> tag then make sure it goes
		//   back to the gbhom/gk144 host and does not use
		//   the <base href> tag. that way gk144 can insert the
		//   event image into the http reply from the browser before
		//   passing it on to the external event form server.
		//   we should insert a "formproxy=http://..." 
		//   input into the form so  we know to forward the form reply 
		//   to that url. and also to substitute in the picture.
		// . we detect "formproxy=..." in pageevents.cpp like we
		//   look for "form". see top of sendPageEvents2() which
		//   calls sendPageFormProxy() when it sees that.
		// . don't nuke the "url" in this <form> tag, but rather
		//   include it as a text input named "formproxy".
		// 
		// 
		if ( ! strncasecmp(wptrs[i] ,"<form ",6 ) ) {
			// scan up until we hit "action="
			char *as = strncasestr (wptrs[i],wlens[i],"action=");
			// if not there, that is strange!
			if ( ! as ) {
				sb->safeMemcpy ( wptrs[i], wlens[i] );
				continue;
			}
			// int16_tcut
			char *wend = wptrs[i] + wlens[i];
			// skip action=
			as += 7;
			// skip quote
			bool hasQuote = false;
			if ( *as == '\"' ) { hasQuote = true; as++; }
			// now point to original submission url
			char *origUrl = as;
			// find end of that
			char *end = origUrl;
			for ( ; end < wend ; end++ ) {
				if (   hasQuote && 
				     *end =='\"' ) 
					break;
				if ( ! hasQuote && 
				     *end !='>' && 
				     ! is_wspace_a(*end) )
					break;
			}
			// length of the original submission url
			int32_t origUrlLen = end - origUrl;
			// copy up until that
			int32_t leftLen = as - wptrs[i];
			sb->safeMemcpy ( wptrs[i] , leftLen );
			// insert our url so form is submitted to us
			sb->safePrintf("http://10.5.1.203:8000/");
			// end the rest of the form tag
			sb->safeMemcpy ( end , wend - end );
			// put a hidden input tag for us to use
			sb->safePrintf ("<input "
					"type=hidden "
					"name=formproxyto "
					"value=\"");
			// make it absolute if its relative
			if ( strncasecmp(origUrl,"http://",7) &&
			     strncasecmp(origUrl,"https://",8) ) {
				char *fu = st->m_hr.getString("formurl",NULL);
				Url uu;
				uu.set ( fu );
				char *us = uu.getUrl();
				int32_t  uslen = uu.getUrlLen();
				// hack off filename, if there
				uslen -= uu.getFilenameLen();
				// then prepend to form url
				sb->safeMemcpy(us,uslen);
			}
			// print original url into the submission url as
			// a cgi parm
			sb->safeMemcpy ( origUrl , origUrlLen );
			sb->safePrintf ( "\">");
			// and put the url of the image so we can download
			// it and insert it before returning the final
			// reply to the external event submission server
			sb->safePrintf("<input type=hidden "
				       "name=egimgurl value=\"%s\">" 
				       , mr->ptr_imgUrl
				       );

			// make the facebook thumb into a larger image
			if ( mr->m_eventFlags & EV_FACEBOOK ) {
				char *end = sb->getBuf() - 7;
				if ( *end == 'q' ) *end = 'n';
			}
			
			// that's it!
			continue;
		}
		
		


		bool isTextArea = false;
		if ( ! strncasecmp(wptrs[i] ,"<textarea",9)) isTextArea = true;

		// if not an input tag, just copy over and get next one
		if ( strncasecmp(wptrs[i] ,"<input ",7 ) &&
		     // textareas used for descriptions a lot
		     ! isTextArea ) {
			sb->safeMemcpy ( wptrs[i], wlens[i] );
			continue;
		}

		// if its hidden skip as well
		if ( strncasestr(wptrs[i] , wlens[i], " type=hidden" ) ) {
			sb->safeMemcpy ( wptrs[i], wlens[i] );
			continue;
		}
		if ( strncasestr(wptrs[i] , wlens[i], " type=\"hidden" ) ) {
			sb->safeMemcpy ( wptrs[i], wlens[i] );
			continue;
		}

		// now get the sentence previous to this input tag
		Section *ss = sx->m_sectionPtrs[i-1];
		// hit prev unti we got a sentence
		for ( ; ss ; ss = ss->m_prev  ) 
			if ( ss->m_flags & SEC_SENTENCE ) break;

		// if not sentence forget it!
		if ( ! ss ) {
			sb->safeMemcpy ( wptrs[i], wlens[i] );
			continue;
		}

		// ok, now scan the words sentence and normalize them
		// into a string
		SafeBuf nb;
		bool lastWasSpace = true;
		for ( int32_t j = ss->m_a ; j < ss->m_b ; j++ ) {
			// punct?
			if ( ! wids[j] ) {
				if ( lastWasSpace ) continue;
				nb.pushChar(' ');
				lastWasSpace = true;
				continue;
			}
			// skip isolated s like "Day(s) of week"
			if ( wlens[j] == 1 && to_lower_a(wptrs[j][0])=='s' )
				continue;
			// ignore this ("content photo")
			if ( !strncasecmp(wptrs[j],"content",wlens[j]) )
				continue;
			if ( !strncasecmp(wptrs[j],"event",wlens[j]) )
				continue;
			// word?
			nb.safeMemcpy ( wptrs[j], wlens[j] );
			lastWasSpace = false;
		}
			
		// convert to lower case
		//sb.toLowerUtf8();

		// now get that buf
		char *s = nb.getBufStart();
		if ( nb.length() <= 0 ) s = NULL;

		// if no words before it we can't idenfity it, so just
		// print the input tag here and continue
		if ( ! s ) {
			sb->safeMemcpy ( wptrs[i],wlens[i]);
			continue;
		}

		int32_t flags = 0;

		// look for indicative phrases
		if ( ! strcasecmp(s,"venue name") ) flags = FI_VENUE;
		if ( ! strcasecmp(s,"location") ) flags = FI_VENUE;

		if ( ! strcasecmp(s,"name") ) flags = FI_TITLE;
		if ( ! strcasecmp(s,"title") ) flags = FI_TITLE;

		if ( ! strcasecmp(s,"int16_t description") ) flags = FI_DESC;
		// mm/dd/yy
		if ( ! strcasecmp(s,"start date") ) flags = FI_DATE_START;
		// mm/dd/yy
		if ( ! strcasecmp(s,"end date") ) flags = FI_DATE_END;
		// day of week
		if ( ! strcasecmp(s,"day of week") ) flags = FI_DOW;

		if ( ! strcasecmp(s,"website") ) flags = FI_URL;
		if ( ! strcasecmp(s,"website or map") ) flags = FI_URL;
		if ( ! strcasecmp(s,"url") ) flags = FI_URL;

		if ( ! strcasecmp(s,"email") ) flags = FI_EMAIL;

		if ( ! strcasecmp(s,"main phone number") ) flags = FI_PHONE;
		if ( ! strcasecmp(s,"main phone") ) flags = FI_PHONE;
		if ( ! strcasecmp(s,"phone") ) flags = FI_PHONE;

		if ( ! strcasecmp(s,"address") ) flags = FI_STREET;

		if ( ! strcasecmp(s,"city") ) flags = FI_CITY;
		if ( ! strcasecmp(s,"town") ) flags = FI_CITY;
		if ( ! strcasecmp(s,"city town") ) flags = FI_CITY;
		if ( ! strcasecmp(s,"town city") ) flags = FI_CITY;

		if ( ! strcasecmp(s,"state") ) flags = FI_STATE;

		if ( ! strcasecmp(s,"zip") ) flags = FI_ZIP;

		if ( ! strcasecmp(s,"country") ) flags = FI_COUNTRY;

		if ( ! strcasecmp(s,"time") ) flags = FI_TIME_START;

		if ( ! strcasecmp(s,"admission") ) flags = FI_PRICE;
		if ( ! strcasecmp(s,"price") ) flags = FI_PRICE;
		if ( ! strcasecmp(s,"ticket admission") ) flags = FI_PRICE;
		if ( ! strcasecmp(s,"ticket price") ) flags = FI_PRICE;
		if ( ! strcasecmp(s,"ticket cost") ) flags = FI_PRICE;

		if ( ! strcasecmp(s,"your name") ) flags = FI_MY_NAME;
		if ( ! strcasecmp(s,"organized by") ) flags = FI_MY_NAME;
		if ( ! strcasecmp(s,"your phone number") ) flags = FI_MY_PHONE;

		if ( ! strcasecmp(s,"your email address") ) flags =FI_MY_EMAIL;
		if ( ! strcasecmp(s,"your email") ) flags = FI_MY_EMAIL;


		if ( ! strcasecmp(s,"image") ) flags = FI_IMAGE;
		if ( ! strcasecmp(s,"photo") ) flags = FI_IMAGE;
		if ( ! strcasecmp(s,"picture") ) flags = FI_IMAGE;

		SafeBuf vbuf;


		// ok, now determine value of the input box
		if ( flags == FI_TITLE ) {
			printEventTitle ( vbuf , mr , NULL );
		}

		if ( flags == FI_DESC ) {
			printEventSummary ( vbuf ,
					    mr , 
					    99999 , // width
					    EDF_SUBEVENTBROTHER , // donotprint
					    0 , 
					    NULL , // state
					    NULL , // expanded result
					    200 ); // maxchars
		}
			
		if ( flags == FI_DATE_START ) {
			vbuf.safePrintf("%s %"INT32""
					, getMonthName(er.m_month1)
					, (int32_t)er.m_dayNum1
					);
		}

		if ( flags == FI_DATE_END ) {
			vbuf.safePrintf("%s %"INT32""
					, getMonthName(er.m_month2)
					, (int32_t)er.m_dayNum2
					);
		}

		if ( flags == FI_TIME_START ) {
			char *ampm = "am";
			int32_t h = er.m_hour1;
			if ( h == 12 ) ampm = "pm";
			if ( h > 12 ) { ampm = "pm"; h -= 12; }
			vbuf.safePrintf("%"INT32":%02"INT32" %s"
					, (int32_t)h
					, (int32_t)er.m_min1
					, ampm
					);
		}

		if ( flags == FI_TIME_END ) {
			char *ampm = "am";
			int32_t h = er.m_hour2;
			if ( h == 12 ) ampm = "pm";
			if ( h > 12 ) { ampm = "pm"; h -= 12; }
			vbuf.safePrintf("%"INT32":%02"INT32" %s"
					, (int32_t)h
					, (int32_t)er.m_min2
					, ampm
					);
		}

		if ( flags == FI_URL ) {
			// make event guru url
			vbuf.safePrintf("http://www.eventguru.com/?id=%"UINT64"."
					"%"UINT64"" 
					, mr->m_docId
					, mr->m_eventHash64
					);
		}

		if ( flags == FI_EMAIL ) {
			// make event guru url
			vbuf.safePrintf("guru@eventguru.com");
		}

		if ( flags == FI_STREET && street ) {
			vbuf.safePrintf("%s",street);
		}

		if ( flags == FI_CITY && city) {
			vbuf.safePrintf("%s",city);
		}

		if ( flags == FI_STATE && adm1 ) {
			vbuf.safePrintf("%s",adm1);
		}

		if ( flags == FI_ZIP && zip ) {
			vbuf.safePrintf("%s",zip);
		}

		if ( flags == FI_COUNTRY && country ) {
			vbuf.safePrintf("%s",country);
		}

		if ( flags == FI_VENUE && name1 ) {
			vbuf.safePrintf("%s",name1);
		}

		if ( flags == FI_MY_NAME ) {
			vbuf.safePrintf("Matt Wells");
		}

		if ( flags == FI_MY_PHONE ) {
			vbuf.safePrintf("505 450 3518");
		}

		if ( flags == FI_MY_EMAIL ) {
			vbuf.safePrintf("guru@eventguru.com");
		}





		char *val = vbuf.getBufStart();
		int32_t  vlen = vbuf.length();
		if ( vlen <= 0 ) val = NULL;

		// if no value, just print the input tag with no value
		if ( ! val ) {
			sb->safeMemcpy ( wptrs[i],wlens[i]);
			continue;
		}
		
		//
		// TEXTAREA logic
		//
		if ( isTextArea ) {
			// finish text area tag
			sb->safeMemcpy ( wptrs[i],wlens[i]);
			// then insert our value
			sb->safeMemcpy ( val , vlen );
			// scan for next </textarea>
			for ( ; i+1 < nw ; i++ ) {
				// stop if it follows next
				if ( tids[i+1] == TAG_TEXTAREA|BACKBIT )
					break;
			}
			continue;
		}

		//
		// . print input box, but not "value="
		// . substitute our own value here
		//
		char *x = wptrs[i];
		char *xend = x + wlens[i];
		bool didIt = false;
		for ( ; x < xend ; x++ ) {
			if ( strncasecmp(x," value=",7) && *x != '>' ) {
				sb->pushChar(*x);
				continue;
			}
			// substitue
			sb->safePrintf(" value=\"");
			sb->safeMemcpy(val,vlen);
			sb->safePrintf("\"");
			didIt = true;
			// end?
			if ( *x == '>' ) {
				sb->pushChar('>');
				break;
			}
			// skip over original val
			x += 6;
			// quote?
			bool inQuote = false;
			if ( *x == '\"' ) { inQuote = true; x++; }
			// skip till space, or quote if in quotes
			for ( ; x < xend ; x++ ) {
				if ( is_wspace_a (*x) && ! inQuote){x--;break;}
				if ( *x == '\"' ) break;
				if ( *x == '>' && ! inQuote ) {x--;break;}
			}
			// ok, its pointing to a space or quote, do not
			// print out that space or quote
			continue;
		}

	}

	sendReply ( st );
}

static void gotImage ( void *state , TcpSocket *s ) ;

/////////////
//
// TAKE THE HTTP REQUEST from the browser meant as an event submission
// and proxy it to the external event submission server.
//
// We do this so we can insert the event image into the supplied HTTP REQUEST.
//
// The <input type=file> tag typically has a "filename=\"" string in the
// POSTed form data. so we just have to insert the image file contents 
// after that.
//
/////////////

class State27 {
public:
	TcpSocket *m_socket;
	SafeBuf m_replyBuf;
	SafeBuf m_relayBuf;
	HttpRequest m_hr;
};

static void relayReply ( void *state , TcpSocket *subsock ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool sendPageFormProxy ( TcpSocket *s , HttpRequest *hr ) {

	// must be local
	if ( ! hr->isLocal() ) return sendErrorReply(NULL,ENOTLOCAL);

	// get the image url
	char *imgUrl = hr->getString ( "egimgurl" , NULL );

	// the original form submission url, a page on the external 
	// event submission server
	char *formUrl = hr->getString ("formproxyto",NULL);

	// fake it
	//formUrl = "http://www.itsatrip.org/events/submit.aspx";

	char *req       = s->m_readBuf;
	int32_t  reqSize   = s->m_readOffset;
	int32_t  allocSize = s->m_readBufSize;

	// make a state
	State27 *st = NULL;
	try { st = new (State27); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageParser: new(%i): %s", 
		    sizeof(State27),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,
						   mstrerror(g_errno));}
	mnew ( st , sizeof(State27) , "PageParser" );

	// save socket ptr
	st->m_socket = s;
	st->m_hr.copy ( hr );

	// copy the original reply we need to forward
	st->m_replyBuf.safeMemcpy ( s->m_readBuf ,s->m_readOffset );


	// if not there, just proxy it now as it is
	if ( ! imgUrl ) {
		// assume we got one
		gotImage ( st , NULL );
		return false;
	}

	log("formprox: downloading image %s",imgUrl);

	g_errno = 0;

	// re-write it
	//if ( strncasecmp(imgUrl,"https://",8) == 0 ) imgUrl += 8;

	// otherwise, get the image, scale it and insert it into the
	// post request... look for "filename=\"" as a form element.
	if ( ! g_httpServer.getDoc ( imgUrl ,
				     0 , // ip - look it up
				     0 , // ofset
				     -1 , // size
				     0 , // if modified since
				     st ,
				     gotImage ,
				     60000 , // timeout
				     0 , // proxyip
				     0 , // proxyport
				     9999999 , // maxtextdoclen
				     9999999  // maxotherdoclen
				     ))
		return false;
	// must be error
	log("formprox: error downloading image url %s : %s",
	    imgUrl,mstrerror(g_errno));

	// delete state then
	mdelete ( st , sizeof(State27) , "pageprox" );
	delete (st);

	// prevent it from being freed when we return here
	s->m_readBuf = NULL;
	// use this directly since we are just forwarding the request
	TcpServer *tcp = &g_httpServer.m_tcp;
	// because buffer is now owned by this socket
	if ( ! tcp->sendMsg ( formUrl , // host/port
			      req     ,
			      reqSize ,
			      allocSize ,
			      reqSize ,
			      NULL, // st ,
			      NULL,//callback
			      60 , // timeout
			      999999999 ,// maxtextdoclen
			      999999999 // maxotherdoclen
			      ) )
		return false;
	// it did not block
	return true;
}

static void removeLine ( SafeBuf *rb , char *lineStart ) ;
static void removeDisposition ( SafeBuf *rb , char *name ) ;
static void adjustContentLength ( SafeBuf *rb ) ;

void gotImage ( void *state , TcpSocket *socket ) {
	// cast it
	State27 *st = (State27 *)state;
	// if image failed!
	if ( g_errno ) {
	hadError:
		log("submit: image download failed: %s",mstrerror(g_errno));
		TcpSocket *s = st->m_socket;
		mdelete ( st , sizeof(State27) , "pageprox" );
		delete (st);
		g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
		return;
	}

	// int16_tcut
	SafeBuf *rb = &st->m_replyBuf;

	HttpMime hm;
	if ( socket ) {
		hm.set ( socket->m_readBuf , socket->m_readOffset , NULL );
		if ( hm.getHttpStatus() != 200 ) { 
			g_errno = EBADREPLY; goto hadError;}
		// save it
		SafeBuf imgBuf;
		imgBuf.safeMemcpy ( hm.getContent() , hm.getContentLen() );
		char fn[128];
		sprintf(fn,"/tmp/tmpimg%"UINT32"",getTimeLocal());
		imgBuf.save ( "" , fn );
		// convert it
		char cmd[512];
		sprintf ( cmd , "anytopnm %s > %s-2", fn , fn  );
		system  ( cmd );
		// scale it
		sprintf ( cmd , "pnmscale -xysize 128 128 %s-2 > %s-3",fn,fn);
		system  ( cmd );
		// convert it to png
		sprintf ( cmd , "pnmtopng %s-3 > %s-4.png", fn , fn  );
		system  ( cmd );
		// load it (tmp/tmpimgxxxxxx4.png)
		imgBuf.purge();
		char fn2[128];
		sprintf(fn2,"/tmp/tmpimg%"UINT32"-4.png",getTimeLocal());
		imgBuf.load ( "" , fn2 );
		// now insert that into the http reply
		char *insertionPoint = strstr(rb->getBufStart(),"filename=\"");
		// skip if can't find it
		if ( ! insertionPoint ) return;
		// skip over filename=\"
		insertionPoint += 10;
		// put our junk there
		int32_t insertPos = insertionPoint - rb->getBufStart();
		rb->insert("img.png", insertPos );
		// find the \r\n\r\n after that point
		char *imgData = strstr(rb->getBufStart()+insertPos,"\r\n\r\n");
		imgData += 4;
		int32_t dataPos = imgData - rb->getBufStart();
		// note it
		log("submit: inserting image of %"INT32" bytes",imgBuf.length());
		// then the image content
		rb->insert2 ( imgBuf.getBufStart() , imgBuf.length(),dataPos );
	}

	// the original form submission url, a page on the external 
	// event submission server
	char *formUrl = st->m_hr.getString ("formproxyto",NULL);
	// now get formurl from the file
	//SafeBuf info;
	//info.loadFromFile2("/tmp/tmpfile",formUrlHash32);
	//char *formUrl = info.getBufStart();

	// fake it
	//formUrl = "http://www.itsatrip.org/events/submit.aspx";


	// now the request buf will be like "POST / HTTP/1.1\r\n...."
	// so we have to replace "/" with "formUrl" which is the original
	// url being submitted to because we had made it our own server ip
	// on the path of "/". so formUrl's path needs to go there.
	char *ourPath = strstr(rb->getBufStart()," / ");
	if ( ! ourPath ) { g_errno = EBADREPLY; goto hadError; }
	ourPath += 1;
	Url ff; ff.set(formUrl);
	char *path = ff.getPath();
	int32_t  plen = ff.getPathLen();
	rb->safeReplace ( path , plen , ourPath - rb->getBufStart() , 1 );

	// replace "Host: 10.5.1.203:8000\r\n" with the right host
	char *host = ff.getHost();
	int32_t  hlen = ff.getHostLen();
	char *hostLine = strstr(rb->getBufStart(),"Host: ");
	hostLine += 6;
	char *hostLineEnd = strstr(hostLine,"\r\n");
	if ( ! hostLine || ! hostLineEnd){g_errno = EBADREPLY; goto hadError; }
	int32_t hostLinePos = hostLine - rb->getBufStart();
	int32_t hostLineLen = hostLineEnd - hostLine;
	rb->safeReplace ( host, hlen , hostLinePos, hostLineLen );

	// remove stuff. otherwise we pass them in cookie from eventguru!
	// and referer is like 10.5.1.203/...
	removeLine ( rb , "Referer: ");
	removeLine ( rb , "Cookie: ");

	// take out the gzip encoding
	removeLine ( rb , "Accept-Encoding:");

	// we added these hidden form variables for proxying purposes
	// so we have to remove them now
	removeDisposition ( rb , "formproxyto" );
	removeDisposition ( rb , "egimgurl" );

	// . now we must adjust the content-length
	// . YEAH, because inserting the image screws this up!!!
	adjustContentLength ( rb );

	char *req = rb->getBufStart();
	int32_t  allocated = rb->getCapacity();
	int32_t  reqSize = rb->length();
	rb->detachBuf();

	// use this directly since we are just forwarding the request
	TcpServer *tcp = &g_httpServer.m_tcp;
	// . because buffer is now owned by this socket
	// . we should relay the reply from the submission server back to
	//   our st->m_socket when it comes in!!!
	// . so set the callback to do that
	if ( ! tcp->sendMsg ( formUrl , // host/port
			      req,
			      allocated,
			      reqSize,
			      reqSize,
			      st ,
			      relayReply,//callback
			      60 , // timeout
			      999999999 ,// maxtextdoclen
			      999999999 // maxotherdoclen
			      ) )
	     return;
	// did not block, must have been an error
	relayReply ( st , NULL );
}

void relayReply ( void *state , TcpSocket *subsock ) {
	// cast it
	State27 *st = (State27 *)state;
	// if failed to submit the event
	if ( g_errno || ! subsock ) {
		log("submit: failed to send to submission server: %s",
		    mstrerror(g_errno));
		TcpSocket *s = st->m_socket;
		mdelete ( st , sizeof(State27) , "pageprox" );
		delete (st);
		g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
		return;
	}
	// return reply back as it is
	char *reply     = subsock->m_readBuf;
	int32_t  replySize = subsock->m_readOffset;
	//int32_t  allocSize = subsock->m_readBufSize;

	// do not double free
	//subsock->m_readBuf = NULL;

	// copy over
	st->m_relayBuf.safeMemcpy ( reply , replySize );


	char *formUrl = st->m_hr.getString ("formproxyto",NULL);

	char ttt[128+MAX_URL_LEN];
	sprintf(ttt,"<base href=\"%s\">\n",formUrl);
	char *cstart = strstr(st->m_relayBuf.getBufStart(),"\r\n\r\n");
	int32_t cpos;
	if ( cstart ) cstart += 4;
	if ( cstart ) cpos = cstart - st->m_relayBuf.getBufStart();
	// insert the base href tag!
	st->m_relayBuf.insert2 ( ttt,gbstrlen(ttt),cpos );
	// re-set Content-Length
	adjustContentLength ( &st->m_relayBuf );

	// int16_tcuts
	char *relay = st->m_relayBuf.getBufStart();
	int32_t  relaySize = st->m_relayBuf.length();
	int32_t  relayAllocSize = st->m_relayBuf.getCapacity();

	// . let tcpserver free it when done transmitting it
	// . detach after setting "relay" because it sets m_buf to null
	st->m_relayBuf.detachBuf();

	// . relay the submission server's reply back to the admin
	// . crap this is including the http mime!
	/*
	g_httpServer.sendDynamicPage( st->m_socket , 
				      reply ,
				      replySize ,
				      -1, //cachtime
				      false ,//postreply?
				      NULL, //ctype
				      -1 , //httpstatus
				      NULL,//cookie
				      "utf-8");
	*/
	// use this directly since we are just forwarding the request
	TcpServer *tcp = &g_httpServer.m_tcp;
	// . because buffer is now owned by this socket
	// . we should relay the reply from the submission server back to
	//   our st->m_socket when it comes in!!!
	// . so set the callback to do that
	tcp->sendMsg ( st->m_socket ,
		       relay, // st->m_relayBuf.getBufStart(), // reply,
		       relayAllocSize,
		       relaySize, // replySize,
		       relaySize, // replySize,
		       NULL , // state
		       NULL//callback
		       );

	// nuke it after sendMsg() call so formUrl is valid for it
	mdelete ( st , sizeof(State27) , "pageprox" );
	delete (st);
}	

void removeLine ( SafeBuf *rb , char *lineStart ) {
	char *s = rb->getBufStart();
	char *line = strstr(s , lineStart );
	if ( ! line ) return;
	// find end of line
	char *end = line;
	for ( ; *end ; end++ ) 
		if ( *end == '\r' && end[1] == '\n' ) break;
	// no end?
	if ( ! end ) return;
	end += 2;
	rb->safeReplace ( "" , 0 , line - s , end - line );
}

// name = "formproxyto"	 etc.
// \r\n\r\n", '-' <repeats 29 times>, "148434662416964166751880432549\r\nContent-Disposition: form-data; name=\"formproxyto\"\r\n\r\nhttp://www.itsatrip.org/events/submit.aspx\r\n", '-' <repeats 29 times>, "148434662416964166751880432549
void removeDisposition ( SafeBuf *rb , char *name ) {

	char *start = rb->getBufStart();
	
	char *f = strstr(start,name);

	if ( ! f ) return;

	// back up to start of "Content-Disposition:" line
	char *b = f;
	for ( ; b > start ; b-- )
		if ( !strncasecmp(b,"Content-Disposition:",20)) break;
	if ( b == start ) return;

	// now go forward until next Content-Disposition: line
	char *next = f;
	for ( ; *next ; next++ )
		if ( !strncasecmp(next,"Content-Disposition:",20)) break;
	if ( ! *next ) return;

	// nuke that
	rb->safeReplace ( "" , 0 , b - start , next - b );
}

void adjustContentLength ( SafeBuf *rb ) {

	char *start = rb->getBufStart();
	
	char *f = strstr(start,"Content-Length:");
	if ( ! f ) return;

	// skip to number
	for ( ; *f ; f++ ) {
		if ( is_digit(*f) ) break;
	}

	// find start of content
	char *cs = strstr(start,"\r\n\r\n");
	if ( ! cs ) return;
	cs += 4;
	int32_t headerLen = cs - start;
	//int32_t clen = gbstrlen(cs);
	int32_t clen = rb->length() - headerLen;
	
	// find length of number, nd = # of digits
	int32_t nd = 0;
	char *fe = f;
	for ( ; *fe ; fe++ ) {
		if ( ! is_digit(*fe) ) break;
		nd++;
	}

	// write that out
	char format[64];
	sprintf(format,"%%0%"INT32"li",nd);
	char ttt[32];
	int32_t toPrint = sprintf(ttt,format,clen);
	// just copy it over, padded with zeroes
	gbmemcpy ( f , ttt , toPrint );
}
