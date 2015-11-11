#include <string.h>
#include "SafeBuf.h"
#include "HttpServer.h"
#include "Posdb.h"

TcpSocket *g_qaSock = NULL;
SafeBuf g_qaOutput;
bool g_qaInProgress = false;
int32_t g_numErrors;

static int32_t s_checkCRC = 0;

static bool s_registered = false;

bool qatest ( ) ;

void qatestWrapper ( int fd , void *state ) {
	qatest();
}

// wait X seconds, call sleep timer... then call qatest()
void wait( float seconds ) {
	// put into milliseconds
	int32_t delay = (int32_t)(seconds * 1000.0);

	if ( g_loop.registerSleepCallback ( delay         ,
					    NULL , // state
					    qatestWrapper,//m_masterLoop
					    0    )) {// niceness
		log("qa: waiting %i milliseconds",(int)delay);
		s_registered = true;
		// wait for it, return -1 since we blocked
		return;
	}

	log("qa: could not register callback!");
	return;
}

// first inject a set list of urls
static char  **s_urlPtrs = NULL;
static char  **s_contentPtrs = NULL;
static SafeBuf s_ubuf1;
static SafeBuf s_ubuf2;
static SafeBuf s_cbuf2;

static Url s_url;
static char *s_expect = NULL;
static char **s_ignore = NULL;

void markOut ( char *content , char *needle ) {

	if ( ! content ) return;

 loop:

	char *s = strstr ( content , needle );
	if ( ! s ) return;

	// advance over name like "rand64=" to avoid hitting those digits
	s += gbstrlen(needle);

	for ( ; *s && ! is_digit(*s); s++ );

	// find end of digit stream
	//char *end = s;
	//while ( ; *end && is_digit(*s); end++ );
	// just bury the digit stream now, zeroing out was not
	// a consistent LENGTH if we had 10 hits vs 9... making the hash 
	// different

	// space out digits. including decimal point.
	for ( ; *s && (is_digit(*s)||*s=='.'); s++ ) *s = ' ';

	// loop for more for the "rand64=" thing
	content = s;
	goto loop;
}


void markOut2 ( char *content , char *needle ) {

	if ( ! content ) return;

	int32_t nlen = gbstrlen(needle);

 loop:

	char *s = strstr ( content , needle );
	if ( ! s ) return;

	// advance over name like "rand64=" to avoid hitting those digits
	//s += gbstrlen(needle);

	for (int32_t i = 0 ; i < nlen ; i++ )
		*s++ = ' ';

	//for ( ; *s && ! is_digit(*s); s++ );

	// find end of digit stream
	//char *end = s;
	//while ( ; *end && is_digit(*s); end++ );
	// just bury the digit stream now, zeroing out was not
	// a consistent LENGTH if we had 10 hits vs 9... making the hash 
	// different

	// space out digits. including decimal point.
	//for ( ; *s && (is_digit(*s)||*s=='.'); s++ ) *s = ' ';

	// loop for more for the "rand64=" thing
	content = s;
	goto loop;
}


void markOutBuf ( char *content ) {

	// take out <responseTimeMS>
	markOut ( content , "<currentTimeUTC>");
	markOut ( content , "<responseTimeMS>");

	// ...from an index of about 429 pages in 0.91 seconds in collection...
	markOut ( content , " pages in ");

	// until i figure this one out, take it out
	markOut ( content , "<docsInCollection>");

	markOut ( content , "spider is done (");
	markOut ( content , "spider is paused (");
	markOut ( content , "spider queue empty (");
	markOut ( content , "spider is active (");

	markOut ( content , "<totalShards>");

	// 3 Collections etc.
	markOut ( content , "/rocket.jpg></div></a></center><br><br><div style=\"width:190px;padding:4px;margin-left:10px;background-color:white;border-top-left-radius:10px;border-bottom-left-radius:10px;border-color:blue;border-width:3px;border-style:solid;margin-right:-3px;border-right-color:white;overflow-y:auto;overflow-x:hidden;line-height:23px;color:black;\"><center><nobr><b>" );

	// until i figure this one out, take it out
	markOut ( content , "<hits>");

	// for those links in the html pages
	markOut ( content, "rand64=");

	// for json
	markOut ( content , "\"currentTimeUTC\":" );
	markOut ( content , "\"responseTimeMS\":");
	markOut ( content , "\"docsInCollection\":");

	// if the results are in json, then status doc is encoded json
	markOut ( content , "\\\"gbssDownloadStartTime\\\":");
	markOut ( content , "\\\"gbssDownloadEndTime\\\":");
	markOut ( content , "\\\"gbssDownloadStartTimeMS\\\":");
	markOut ( content , "\\\"gbssDownloadEndTimeMS\\\":");
	markOut ( content , "\\\"gbssDownloadDurationMS\\\":");
	markOut ( content , "\\\"gbssAgeInIndex\\\":");
	markOut ( content , "\\\"gbssDiscoveredTime\\\":");


	// if the results are in xml, then the status doc is xml encoded
	markOut ( content , "\"gbssDownloadStartTime\":");
	markOut ( content , "\"gbssDownloadEndTime\":");
	markOut ( content , "\"gbssDownloadStartTimeMS\":");
	markOut ( content , "\"gbssDownloadEndTimeMS\":");
	markOut ( content , "\"gbssDownloadDurationMS\":");
	markOut ( content , "\"gbssAgeInIndex\":");


	// for xml
	markOut ( content , "<currentTimeUTC>" );
	markOut ( content , "<responseTimeMS>");
	markOut ( content , "<docsInCollection>");
	markOut ( content , "<firstIndexedDateUTC>");

	// indexed 1 day ago
	markOut ( content,"indexed:");
	// modified 1 day ago
	markOut ( content,"modified:");

	// s_gigabitCount... it is perpetually incrementing static counter
	// in PageResults.cpp
	markOut(content,"ccc(");
	markOut(content,"id=fd");
	markOut(content,"id=sd");

	// for some reason the term freq seems to change a little in
	// the scoring table
	markOut(content,"id=tf");

	// # of collections in the admin page: ..."4 Collections"
	markOut(content,"px;color:black;\"><center><nobr><b>");

	markOut(content,"spider is done (");
	markOut(content,"spider is paused (");
	markOut(content,"spider is active (");
	markOut(content,"spider queue empty (");

	markOut2(content,"bgcolor=#c0c0f0");
	markOut2(content,"bgcolor=#d0d0e0");
}

// do not hash 
int32_t qa_hash32 ( char *s ) {
	uint32_t h = 0;
	int32_t k = 0;
	for ( int32_t i = 0 ; s[i] ; i++ ) {
		// skip if not first space and back to back spaces
		if ( s[i] == ' ' &&i>0 && s[i-1]==' ') continue;
		h ^= g_hashtab [(unsigned char)k] [(unsigned char)s[i]];
		k++;
	}
	return h;
}

#define MAXFLAGS 100

class QATest {
public:
	bool (* m_func)();
	char *m_testName;
	char *m_testDesc;
	char  m_doTest;
	// we set s_flags to this
	int32_t  m_flags[MAXFLAGS];
};

static char *s_content = NULL;
static HashTableX s_ht;
static QATest *s_qt = NULL;

bool saveHashTable ( ) {
	if ( s_ht.m_numSlotsUsed <= 0 ) return true;
	SafeBuf fn;
	fn.safePrintf("%s/qa/",g_hostdb.m_dir);
	log("qa: saving crctable.dat");
	s_ht.save ( fn.getBufStart() , "crctable.dat" );
	return true;
}

void makeQADir ( ) {
	static bool s_init = false;
	if ( s_init ) return;
	s_init = true;
	s_ht.set(4,4,1024,NULL,0,false,0,"qaht");
	// make symlink
	//char cmd[512];
	//snprintf(cmd,"cd %s/html ;ln -s ../qa ./qa", g_hostdb.m_dir);
	//system(cmd);
	char dir[1024];
	snprintf(dir,1000,"%sqa",g_hostdb.m_dir);
	log("mkdir mkdir %s",dir);
	int32_t status = ::mkdir ( dir ,getDirCreationFlags() );
				// S_IRUSR | S_IWUSR | S_IXUSR | 
				// S_IRGRP | S_IWGRP | S_IXGRP | 
				// S_IROTH | S_IXOTH );
	if ( status == -1 && errno != EEXIST && errno )
		log("qa: Failed to make directory %s: %s.",
		    dir,mstrerror(errno));
	// try to load from disk
	SafeBuf fn;
	fn.safePrintf("%s/qa/",g_hostdb.m_dir);
	log("qa: loading crctable.dat");
	s_ht.load ( fn.getBufStart() , "crctable.dat" );
}

void processReply ( char *reply , int32_t replyLen ) {

	// store our current reply
	SafeBuf fb2;
	fb2.safeMemcpy(reply,replyLen );
	fb2.nullTerm();

	// log that we got the reply
	log("qa: got reply(len=%"INT32")(errno=%s)",
	    replyLen,mstrerror(g_errno));

	char *content = NULL;
	int32_t  contentLen = 0;

	// get mime
	if ( reply ) {
		HttpMime mime;
		mime.set ( reply, replyLen , NULL );
		// only hash content since mime has a timestamp in it
		content = mime.getContent();
		contentLen = mime.getContentLen();
		if ( content && contentLen>0 && content[contentLen] ) { 
			char *xx=NULL;*xx=0; }
	}

	if ( ! content ) {
		content = "";
		contentLen = 0;
	}

	s_content = content;

	markOutBuf ( content );


	// make checksum. we ignore back to back spaces so this
	// hash works for <docsInCollection>10 vs <docsInCollection>9
	int32_t contentCRC = 0; 
	if ( content ) contentCRC = qa_hash32 ( content );

	// note it
	log("qa: got contentCRC of %"UINT32"",contentCRC);

	// if what we expected, save to disk if not there yet, then
	// call s_callback() to resume the qa pipeline
	/*
	if ( contentCRC == s_expectedCRC ) {
		// save content if good
		char fn3[1024];
		sprintf(fn3,"%sqa/content.%"UINT32"",g_hostdb.m_dir,contentCRC);
		File ff; ff.set ( fn3 );
		if ( ! ff.doesExist() ) {
			// if not there yet then save it
			fb2.save(fn3);
		}
		// . continue on with the qa process
		// . which qa function that may be
		//s_callback();
		return;
	}
	*/

	if(s_ignore) {
		for(int i = 0;;i++) {
			if(!s_ignore[i]) break;
			if(gb_strcasestr(content, s_ignore[i])) return;
		}
		s_ignore = NULL;
	}

	// Just look a substring of the response so we don't have to worry about
	// miniscule changes in output formats or changing dates.
	if(s_expect) {
		log("expecting for %s", s_expect);
		if(gb_strcasestr(content, s_expect)) {
			g_qaOutput.safePrintf("<b style=color:green;>"
								  "passed test</b><br>%s : "
								  "<a href=%s>%s</a> Found %s (crc=%"UINT32")<br>"
								  "<hr>",
								  s_qt->m_testName,
								  s_url.getUrl(),
								  s_url.getUrl(),
								  s_expect,
								  contentCRC);

		} else {
			g_numErrors++;

			g_qaOutput.safePrintf("<b style=color:red;>FAILED TEST</b><br>%s : "
								  "<a href=%s>%s</a><br> Expected: %s in reply"
								  " (crc=%"UINT32")<br>"
								  "<hr>",
								  s_qt->m_testName,
								  s_url.getUrl(),
								  s_url.getUrl(),
								  s_expect,
								  contentCRC);


		}
		s_expect = NULL;
		return;

	}

	// this means caller does not care about the response
	if ( ! s_checkCRC ) {
		//s_callback();
		return;
	}

	//
	// if crc of content does not match what was expected then do a diff
	// so we can see why not
	//


	//const char *emsg = "qa: bad contentCRC of %"INT32" should be %"INT32" "
	//	"\n";//"phase=%"INT32"\n";
	//fprintf(stderr,emsg,contentCRC,s_expectedCRC);//,s_phase-1);

	// hash url
	int32_t urlHash32 = hash32n ( s_url.getUrl() );

	// combine test function too since two tests may use the same url
	int32_t nameHash = hash32n ( s_qt->m_testName );

	// combine together
	urlHash32 = hash32h ( nameHash , urlHash32 );

	makeQADir();

	// break up into lines
	char fn2[1024];
	sprintf(fn2,"%sqa/content.%"UINT32"",g_hostdb.m_dir,contentCRC);
	fb2.save ( fn2 );

	// look up in hashtable to see what reply crc should be
	int32_t *val = (int32_t *)s_ht.getValue ( &urlHash32 );

	// just return if the same
	if ( val && contentCRC == *val ) {
		g_qaOutput.safePrintf("<b style=color:green;>"
				      "passed test</b><br>%s : "
				      "<a href=%s>%s</a> (urlhash=%"UINT32" "
				      "crc=<a href=/qa/content.%"UINT32">"
				      "%"UINT32"</a>)<br>"
				      "<hr>",
				      s_qt->m_testName,
				      s_url.getUrl(),
				      s_url.getUrl(),
				      urlHash32,
				      contentCRC,
				      contentCRC);
		return;
	}



	if ( ! val ) {
		// add it so we know
		s_ht.addKey ( &urlHash32 , &contentCRC );
		g_qaOutput.safePrintf("<b style=color:blue;>"
				      "first time testing</b><br>%s : "
				      "<a href=%s>%s</a> "
				      "(urlhash=%"UINT32" "
				      "crc=<a href=/qa/content.%"UINT32">%"UINT32""
				      "</a>)<br>"
				      "<hr>",
				      s_qt->m_testName,
				      s_url.getUrl(),
				      s_url.getUrl(),
				      urlHash32,
				      contentCRC,
				      contentCRC);
		return;
	}


	log("qa: crc changed for url %s from %"INT32" to %"INT32"",
	    s_url.getUrl(),*val,contentCRC);

	// get response on file
	SafeBuf fb1;
	char fn1[1024];
	sprintf(fn1,"%sqa/content.%"UINT32"",g_hostdb.m_dir, *val);
	fb1.load(fn1);
	fb1.nullTerm();

	// markout both
	markOutBuf ( fb1.getBufStart() );
	markOutBuf ( fb2.getBufStart() );

	// save temps
	SafeBuf tmpfn1; 
	SafeBuf tmpfn2; 
	tmpfn1.safePrintf("%strash/tmpdiff1.txt",g_hostdb.m_dir); 
	tmpfn2.safePrintf("%strash/tmpdiff2.txt",g_hostdb.m_dir); 
	fb1.save(tmpfn1.getBufStart());
	fb2.save(tmpfn2.getBufStart());

	// do the diff between the two replies so we can see what changed
	// now do the diffs between the marked out versions so it is less
	// spammy
	char cmd[1024];
	sprintf(cmd,"diff %s %s > /tmp/diffout",
		tmpfn1.getBufStart(),
		tmpfn2.getBufStart());
	//fn1,fn2);
	//log("qa: %s\n",cmd);
	gbsystem(cmd);

	g_numErrors++;
	
	SafeBuf he;
	he.htmlEncode ( s_url.getUrl() );

	g_qaOutput.safePrintf("<b style=color:red;>FAILED TEST</b><br>%s : "
			      "<a href=%s>%s</a> (urlhash=%"UINT32")<br>"

			      "<input type=checkbox name=urlhash%"UINT32" value=1 "
			      // use ajax to update test crc. if you undo your
			      // check then it should put the old val back.
			      // when you first click the checkbox it should
			      // gray out the diff i guess.
			      "onclick=submitchanges(%"UINT32",%"UINT32");> "
			      "Accept changes"

			      "<br>"
			      "original on left, new on right. "
			      "oldcrc = <a href=/qa/content.%"UINT32">%"UINT32"</a>"

			      " != <a href=/qa/content.%"UINT32">%"UINT32"</a> = newcrc"
			      "<br>diff output follows:<br>"
			      "<pre id=%"UINT32" style=background-color:0xffffff;>",
			      s_qt->m_testName,
			      s_url.getUrl(),
			      he.getBufStart(),
			      urlHash32,

			      // input checkbox name field
			      urlHash32,

			      // submitchanges() parms
			      urlHash32, 
			      contentCRC,

			      // original/old content.%"UINT32"
			      *val,
			      *val,

			      // new content.%"UINT32"
			      contentCRC,
			      contentCRC,

			      // for the pre tag id:
			      urlHash32);


	// store in output
	SafeBuf sb;
	sb.load("/tmp/diffout");
	g_qaOutput.htmlEncode ( sb.getBufStart() );

	g_qaOutput.safePrintf("</pre><br><hr>");

	// if this is zero allow it to slide by. it is learning mode i guess.
	// so we can learn what crc we need to use.
	// otherwise, stop right there for debugging
	//if ( s_expectedCRC != 0 ) exit(1);

	// keep on going
	//s_callback();
}

// after we got the reply and verified expected crc, call the callback
static bool (*s_callback)() = NULL;

// come here after receiving ANY reply from the gigablast server
static void gotReplyWrapper ( void *state , TcpSocket *sock ) {

	processReply ( sock->m_readBuf , sock->m_readOffset );

    // Avoid resuming execution if someone called wait while a reply
    // was outstanding.
    if(s_registered) return;
	s_callback ();
}

// returns false if blocked, true otherwise, like on quick connect error
bool getUrl( char *path , int32_t checkCRC = 0 , char *post = NULL ,
             char* expect = NULL, char** ignore = NULL) {

	SafeBuf sb;
	sb.safePrintf ( "http://%s:%"INT32"%s"
			, iptoa(g_hostdb.m_myHost->m_ip)
			, (int32_t)g_hostdb.m_myHost->m_httpPort
			, path
			);

	s_checkCRC = checkCRC;

	bool doPost = true;
	if ( strncmp ( path , "/search" , 7 ) == 0 )
		doPost = false;

	//Url u;
	s_url.set ( sb.getBufStart() );
    s_expect = expect;
    s_ignore = ignore;

	log("qa: getting %s",sb.getBufStart());
	if ( ! g_httpServer.getDoc ( s_url.getUrl() ,
				     0 , // ip
				     0 , // offset
				     -1 , // size
				     0 , // ifmodsince
				     NULL ,
				     gotReplyWrapper,
				     999999*1000, // timeout ms
				     0, // proxyip
				     0, // proxyport
				     -1, // maxtextdoclen
				     -1, // maxotherdoclen
				     NULL , // useragent
				     "HTTP/1.0" , // protocol
				     doPost , // doPost
				     NULL , // cookie
				     NULL , // additionalHeader
				     NULL , // fullRequest
				     post ) )
		return false;
	// error?
	processReply ( NULL , 0 );
	//log("qa: getUrl error: %s",mstrerror(g_errno));
	return true;
}	

bool loadUrls () {
	static bool s_loaded = false;
	if ( s_loaded ) return true;
	s_loaded = true;
	// use injectme3 file
	s_ubuf1.load("./injectme3");
	// scan for +++URL: xxxxx
	char *s = s_ubuf1.getBufStart();
	for ( ; *s ; s++ ) {
		if ( strncmp(s,"+++URL: ",8) ) continue;
		// got one
		// \0 term it for s_contentPtrs below
		*s = '\0';
		// find end of it
		s += 8;
		char *e = s;
		for ( ; *e && ! is_wspace_a(*e); e++ );
		// null term it
		if ( *e ) *e = '\0';
		// store ptr
		s_ubuf2.pushPtr(s);
		// skip past that
		s = e;
		// point to content
		s_cbuf2.pushPtr(s+1);
	}
	// make array of url ptrs
	s_urlPtrs = (char **)s_ubuf2.getBufStart();
	s_contentPtrs= (char **)s_cbuf2.getBufStart();
	return true;
}

/*
static char *s_queries[] = {
	"the",
	"+the",
	"cats",
	"+cats dog",
	"+cats +dog",
	"cat OR dog",
	"cat AND dog",
	"cat AND NOT dog",
	"NOT cat AND NOT dog",
	"cat -dog",
	"site:wisc.edu"
};
*/

//#undef usleep

// nw use this
static int32_t *s_flags = NULL;

//
// the injection qa test suite
//
bool qainject1 ( ) {

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1&"
				"collectionips=127.0.0.1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn off images thumbnails
	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// no spider replies because it messes
				// up our last test to make sure posdb
				// is 100% empty. 
				// see "index spider replies" in Parms.cpp.
				"&isr=0"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// this only loads once
	loadUrls();
	int32_t max = s_ubuf2.length()/(int32_t)sizeof(char *);
	//max = 1;

	//
	// inject urls, return false if not done yet
	//
	//static bool s_x4 = false;
	if ( ! s_flags[2] ) {
		// TODO: try delimeter based injection too
		//static int32_t s_ii = 0;
		for ( ; s_flags[20] < max ; ) {
			// inject using html api
			SafeBuf sb;
			sb.safePrintf("&c=qatest123&deleteurl=0&"
				      "format=xml&u=");
			sb.urlEncode ( s_urlPtrs[s_flags[20]] );
			// the content
			sb.safePrintf("&hasmime=1");
			// sanity
			//if ( strstr(s_urlPtrs[s_flags[20]],"wdc.htm") )
			//	log("hey");
			sb.safePrintf("&content=");
			sb.urlEncode(s_contentPtrs[s_flags[20]] );
			sb.nullTerm();
			// pre-inc it in case getUrl() blocks
			s_flags[20]++;//ii++;
			if ( ! getUrl("/admin/inject",
				      0, // no idea what crc to expect
				      sb.getBufStart()) )
				return false;
		}
		s_flags[2] = true;
	}

	// +the
	//static bool s_x5 = false;
	if ( ! s_flags[3] ) {
		wait(1.5);
		s_flags[3] = true;
		return false;
	}

	if ( ! s_flags[16] ) {
		s_flags[16] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe"
				"&dsrt=500",
				702467314 ) )
			return false;
	}

	// sports news
	//static bool s_x7 = false;
	if ( ! s_flags[4] ) {
		s_flags[4] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=sports+news",2009472889 ) )
		     return false;
	}

	// stop for now
	//return true;


	// 'washer & dryer' does some algorithmic synonyms 'washer and dryer'
	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"debug=1&q=washer+%26+dryer",9999 ) )
		     return false;
	}


	//
	// adv.html test
	//
	// query for 'test' using adv.html advanced search interface
	if ( ! s_flags[27] ) {
		s_flags[27] = true;
		if ( ! getUrl ( 
			       "/search?c=qatest123&qa=17&format=xml&"
			       "dr=1&pss=50&sc=1&hacr=1&quotea=web+site&"
			       "gblang=1&minus=transcripts&n=150", 
			       123 ) )
			return false;
	}


	// &sites= test
	if ( ! s_flags[28] ) {
		s_flags[28] = true;
		if ( ! getUrl ( 
			       "/search?c=qatest123&qa=17&format=xml&q=web&"
			       "sortby=2&"
			       // html only:
			       "sw=20&"
			       "filetype=html&"
			       "ff=1&"
			       "facet=gbfacetint:gbhopcount&"
			       "sites=mindtools.com+www.redcross.org"
			       , 123 ) )
			return false;
	}

	// html test of summary width
	if ( ! s_flags[29] ) {
		s_flags[29] = true;
		if ( ! getUrl ( 
			       "/search?c=qatest123&qa=17&format=html&q=web&"
			       // html only:
			       "sw=20&tml=10&ns=1&smxcpl=30&qh=0&n=100&"
			       "dt=keywords+description&"
			       "facet=gbfacetint:gbspiderdate&"
			       , 123 ) )
			return false;
	}


	// stop for now so we can analyze the index
	//return true; //

	//
	// eject/delete the urls
	//
	//static int32_t s_ii2 = 0;
	for ( ; s_flags[5] < max ; ) {
		// reject using html api
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&deleteurl=1&"
			       "format=xml&u=");
		sb.urlEncode ( s_urlPtrs[s_flags[5]] );
		sb.nullTerm();
		// pre-inc it in case getUrl() blocks
		//s_ii2++;
		s_flags[5]++;
		if ( ! getUrl ( sb.getBufStart() , 0 ) )
			return false;
	}

	//
	// make sure no results left, +the
	//
	if ( ! s_flags[6] ) {
		wait(1.5);
		s_flags[6] = true;
		return false;
	}

	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=2&format=xml&q=%2Bthe",
				-1672870556 ) )
			return false;
	}


	// force a dump of posdb and other rdbs from mem to disk
	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/admin/master?c=qatest123&dump=1",
				-1672870556 ) )
			return false;
	}


	if ( ! s_flags[21] ) {
		wait(6.0);
		s_flags[21] = true;
		return false;
	}


	// ensure no posdb files on disk!
	if ( ! s_flags[19] ) {
		s_flags[19] = true;
		// just use a msg5 to ensure posdb is empty
		Msg5 msg5;
		RdbList list;
		key144_t startKey;
		key144_t endKey;
		startKey.setMin();
		endKey.setMax();
		CollectionRec *cr = g_collectiondb.getRec("qatest123");
		g_threads.disableThreads();
		if ( ! msg5.getList ( RDB_POSDB  ,
				      cr->m_collnum  ,
				      &list         ,
				      (char *)&startKey      ,
				      (char *)&endKey        ,
				      64000         , // minRecSizes   ,
				      true          , // includeTree   ,
				      false         , // add to cache?
				      0             , // max cache age
				      0             , // startFileNum  ,
				      -1            , // numFiles      ,
				      NULL          , // state
				      NULL          , // callback
				      0             , // niceness
				      false         , // err correction?
				      NULL          ,
				      0             ,
				      -1            ,
				      true          ,
				      -1LL          ,
				      NULL , // &msg5b        ,
				      true          )) {
			log("qa: HEY! it did not block");
			char *xx=NULL;*xx=0;
		}
		g_threads.enableThreads();
		if ( list.m_listSize ) {
			log("qa: failed qa test of posdb0001.dat. "
			    "has %i bytes of positive keys! coring.",
			    (int)list.m_listSize);
			char rec [ 64];
			for ( list.getCurrentKey ( rec ) ;
			      ! list.isExhausted() ;
			      list.skipCurrentRecord() ) {
				// parse it up
				int64_t tid = g_posdb.getTermId ( rec );
				int64_t d = g_posdb.getDocId ( rec ) ;
				log("qa: termid=%"INT64" docid=%"INT64,
				    tid,d);
			}
			//char *xx=NULL;*xx=0;
			exit(0);
		}


		/*
		  MDW: can't use this since we currently just dump out all
		  the negative recs to first file. i started to modify
		  RdbDump.cpp to call RdbList::removeNegRecs() when it was
		  dumping the first file for this coll/rdb but then decided
		  not to follow through with it for now.
		SafeBuf sb;
		CollectionRec *cr = g_collectiondb.getRec("qatest123");
		sb.safePrintf("%s/coll.qatest123.%i/posdb0001.dat"
			      , g_hostdb.m_dir
			      , (int)cr->m_collnum
			      );
		File ff;
		ff.set ( sb.getBufStart() );
		if ( ff.doesExist() ) {
			log("qa: failed qa test of posdb0001.dat. coring.");
			char *xx=NULL;*xx=0;
		}
		*/
	}

	

	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED "
			"QA INJECT TEST 1");
		//if ( s_callback == qainject ) exit(0);
		return true;
	}


	return true;
}

//static int32_t s_savedAutoSaveFreq = 0;

bool qainject2 ( ) {

	//if ( ! s_callback ) s_callback = qainject2;

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		//s_savedAutoSaveFreq = g_conf.m_autoSaveFrequency;
		//g_conf.m_autoSaveFrequency = 0;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// turn off images thumbnails
	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		// can't turn off spiders because we need for query reindex
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	//
	// try delimeter based injecting
	//
	//static bool s_y2 = false;
	if ( ! s_flags[7] ) {
		s_flags[7] = true;
		SafeBuf sb;
		// delim=+++URL:
		sb.safePrintf("&c=qatest123&deleteurl=0&"
			      "delim=%%2B%%2B%%2BURL%%3A&format=xml&u=xyz.com&"
			      "hasmime=1&content=");
		// use injectme3 file
		SafeBuf ubuf;
		ubuf.load("./injectme3");
		sb.urlEncode(ubuf.getBufStart());
		sb.nullTerm();
		if ( ! getUrl ( "/admin/inject",
				// check reply, seems to have only a single 
				// docid in it
				-1970198487, sb.getBufStart()) )
			return false;
	}

	// now query check
	//static bool s_y4 = false;
	if ( ! s_flags[8] ) {
		wait(1.5);
		s_flags[8] = true;
		return false;
	}

	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
				-1804253505 ) )
			return false;
	}

	//static bool s_y5 = false;
	if ( ! s_flags[9] ) {
		s_flags[9] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=sports"
				"+news&ns=1&tml=20&smxcpl=30&"
				"sw=10&showimages=1"
				,-1874756636 ) )
			return false;
	}

	//static bool s_y6 = false;
	if ( ! s_flags[10] ) {
		s_flags[10] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=sports"
				"+news&ns=1&tml=20&smxcpl=30&"
				"sw=10&showimages=0&hacr=1"
				,1651330319 ) )
			return false;
	}

	//static bool s_y7 = false;
	if ( ! s_flags[11] ) {
		s_flags[11] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=sports"
				"+news&ns=1&tml=20&smxcpl=30&"
				"sw=10&showimages=0&sc=1"
				,-1405546537 ) )
			return false;
	}


	//
	// mdw: query DELETE test
	//
	 if ( ! s_flags[30] ) {


	 	s_flags[30] = true;

		// log("qa: SUCCESSFULLY COMPLETED "
		// 	"QA INJECT TEST 2 *** FAKE");
		// //if ( s_callback == qainject ) exit(0);
		// g_conf.m_autoSaveFrequency = s_savedAutoSaveFreq;
		// return true;


	 	if ( ! getUrl ( "/admin/reindex"
				"?c=qatest123"
				"&format=xml"
	 			//"&debug=1"
				"&q=sports"
				"&forcedel=1"
				"&qa=1"
				,9999 ) )
	 		return false;
	}


	 // wait 10 seconds for reindex to finish
	if ( ! s_flags[31] ) {
		wait(10.0);
		s_flags[31] = true;
		return false;
	}

	// ensure no results for sports now
	if ( ! s_flags[32] ) {
		s_flags[32] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=sports"
				"&ns=1&tml=20&smxcpl=30&"
				"sw=10&showimages=0&sc=1"
				,-1405546537 ) )
			return false;
	}

	// and this particular url has two spider status records indexed
	if ( ! s_flags[33] ) {
		s_flags[33] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q="
				"gbssUrl%3Axyz.com%2F-13737921970569011262&"
				"xml=1"
				,-1405546537 ) )
			return false;
	}


	//
	// delete the 'qatest123' collection
	//
	// if ( ! s_flags[12] ) {
	// 	s_flags[12] = true;
	// 	if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
	// 		return false;
	// }


	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED "
			"QA INJECT TEST 2");
		//if ( s_callback == qainject ) exit(0);
		//g_conf.m_autoSaveFrequency = s_savedAutoSaveFreq;
		return true;
	}


	return true;
}


bool qaSyntax ( ) {

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		//s_savedAutoSaveFreq = g_conf.m_autoSaveFrequency;
		//g_conf.m_autoSaveFrequency = 0;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// turn off images thumbnails
	if ( ! s_flags[2] ) {
		s_flags[2] = true;
		// can't turn off spiders because we need for query reindex
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// index spider reply status docs
				"&isr=1"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	
	//
	// try delimeter based injecting
	//
	//static bool s_y2 = false;
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;
		// delim=+++URL:
		sb.safePrintf("&c=qatest123&deleteurl=0&"
			      "delim=%%2B%%2B%%2BURL%%3A&format=xml&u=xyz.com&"
			      "hasmime=1&content=");
		// use injectme3 file
		SafeBuf ubuf;
		ubuf.load("./injectmedemo");
		sb.urlEncode(ubuf.getBufStart());
		sb.nullTerm();
		if ( ! getUrl ( "/admin/inject",
				// check reply, seems to have only a single 
				// docid in it
				-1970198487, sb.getBufStart()) )
			return false;
	}

	static int s_i;

	// now query check
	//static bool s_y4 = false;
	if ( ! s_flags[4] ) {
		wait(1.5);
		s_flags[4] = true;
		s_i = 0;
		return false;
	}

	//
	// now run a bunch of queries
	//
	static char *s_q[] ={"cat dog",
			     "+cat",
			     "mp3 \"take five\"",
			     "\"john smith\" -\"bob dole\"",
			     "bmx -game",
			     "cat | dog",
			     "document.title:paper",

			     "gbfieldmatch:strings.vendor:\"My Vendor Inc.\"",
			     "url:www.abc.com/page.html",
			     "ext:doc",
			     "link:www.gigablast.com/foo.html",
			     "sitelink:abc.foobar.com",
			     "site:mysite.com",
			     "ip:1.2.3.4",
			     "ip:1.2.3",
			     "inurl:dog",
			     "suburl:dog",
			     "title:\"cat food\"",
			     "title:cat",
			     "gbinrss:1",
			     "type:json",
			     "filetype:json",
			     "gbisadult:1",
			     "gbimage:site.com/image.jpg",
			     "gbhasthumbnail:1",
			     "gbtagsitenuminlinks:0",
			     "gbzip:90210",
			     "gbcharset:windows-1252",
			     "gblang:de",
			     "gbpathdepth:3",
			     "gbhopcount:2",
			     "gbhasfilename:1",
			     "gbiscgi:1",
			     "gbhasext:1",
			     "gbsubmiturl:domain.com/process.php",
			     "gbparenturl:www.xyz.com/abc.html",

			     "cameras gbsortbyfloat:price",
			     "cameras gbsortbyfloat:product.price",
			     "cameras gbrevsortbyfloat:product.price",
			     "pilots gbsortbyint:employees",
			     "gbsortbyint:gbspiderdate",
			     "gbsortbyint:company.employees",
			     "gbsortbyint:gbsitenuminlinks",
			     "gbrevsortbyint:gbspiderdate",
			     "cameras gbminfloat:price:109.99",
			     "cameras gbminfloat:product.price:109.99",
			     "cameras gbmaxfloat:price:109.99",
			     "gbequalfloat:product.price:1.23",
			     "gbminint:gbspiderdate:1391749680",
			     "gbmaxint:company.employees:20",
			     "gbequalint:company.employees:13",

			     "gbdocspiderdate:1400081479",
			     "gbspiderdate:1400081479",
			     "gbdocindexdate:1400081479",
			     "gbindexdate:1400081479",

			     "gbfacetstr:color",
			     "gbfacetstr:product.color",
			     "gbfacetstr:gbtagsite cat",
			     "gbfacetint:product.cores",
			     "gbfacetint:gbhopcount",
			     "gbfacetint:size,0-10,10-20,30-100,100-200,200-1000,1000-10000",
			     "gbfacetint:gbsitenuminlinks",
			     "gbfacetfloat:product.weight",
			     "gbfacetfloat:product.price,0-1.5,1.5-5,5.0-20,20-100.0",
			     "gbcountry:us",
			     "gbpermalink:1",
			     "gbdocid:123456",

			     "gbssStatusCode:0",
			     "gbssStatusmsg:tcp",
			     "gbssUrl:www.abc.com/page.html",
			     "gbssDomain:mysite.com",
			     "gbssIp:1.2.3.4",
			     "gbssUrl:dog",
			     //"gbpathdepth:2",
			     "gbssHopcount:3",
			     //"gbhasfilename2:1",
			     //"gbiscgi2:1",
			     //"gbhasext2:1",

			     "cat AND dog",
			     "cat OR dog",
			     "cat dog OR pig",
			     "\"cat dog\" OR pig",
			     "title:\"cat dog\" OR pig",
			     "cat OR dog OR pig",
			     "cat OR dog AND pig",
			     "cat AND NOT dog",
			     "cat AND NOT (dog OR pig)",
			     "(cat OR dog) AND NOT (cat AND dog)",

			     NULL
	};

	if ( ! s_flags[s_i+10] && s_q[s_i] ) {
		s_flags[s_i+10] = true;
		SafeBuf tmp;
		tmp.safePrintf( "/search?c=qatest123&"
				"qa=3&"
				"qlang=en&"
				"icc=1&"
				"format=json&"
				"q=");
		tmp.urlEncode ( s_q[s_i] );
		// get back 100 for debugging better
		if ( strcmp(s_q[s_i],"gbssStatusCode:0") == 0 ) {
			tmp.safePrintf("&n=100");
		}
		tmp.nullTerm();
		// point to next query
		s_i++;
		if ( ! getUrl ( tmp.getBufStart() , -1804253505 ) )
			return false;
	}


	//static bool s_fee2 = false;
	if ( ! s_flags[5] ) {
		s_flags[5] = true;
		log("qa: SUCCESSFULLY COMPLETED "
			"QA SYNTAX TEST");
		//if ( s_callback == qainject ) exit(0);
		//g_conf.m_autoSaveFrequency = s_savedAutoSaveFreq;
		return true;
	}


	return true;
}

typedef enum {
    DELETE_COLLECTION = 0, 
    ADD_COLLECTION = 1,
    ADD_INITIAL_URLS = 2,
    URL_COUNTER = 20,
    CONTENT_COUNTER = 21,
    SET_PARAMETERS = 17,
    WAIT_A_BIT = 3,
    EXAMINE_RESULTS1 = 16,
    EXAMINE_RESULTS2 = 22,
    EXAMINE_RESULTS3 = 24
} TimeAxisFlags;
char* g_timeAxisIgnore[3] = {"Bad IP", "Doc is error page", NULL};


bool qaTimeAxis ( ) {
	if ( ! s_flags[DELETE_COLLECTION] ) {
		s_flags[DELETE_COLLECTION] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	if ( ! s_flags[ADD_COLLECTION] ) {
		s_flags[ADD_COLLECTION] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1&"
				"collectionips=127.0.0.1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	if ( ! s_flags[SET_PARAMETERS] ) {
		s_flags[SET_PARAMETERS] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// no spider replies because it messes
				// up our last test to make sure posdb
				// is 100% empty. 
				// see "index spider replies" in Parms.cpp.
				"&isr=0"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
                // This is what we are testing
				"&usetimeaxis=1"
				"&de=0"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// this only loads once
	loadUrls();
	int32_t numDocsToInject = s_ubuf2.length()/(int32_t)sizeof(char *);


	//
	// Inject urls, return false if not done yet.
	// Here we alternate sending the same url -> content pair with sending 
    // the same url with different content to simulate a site that is updated
    // at about half the rate that we spider them.
	if ( ! s_flags[ADD_INITIAL_URLS] ) {
		for ( ; s_flags[URL_COUNTER] < numDocsToInject &&
                s_flags[URL_COUNTER] + s_flags[CONTENT_COUNTER] < numDocsToInject; ) {
            // inject using html api
            SafeBuf sb;

            int32_t urlIndex = s_flags[URL_COUNTER];
            int32_t flipFlop = s_flags[CONTENT_COUNTER] % 2;
            int32_t contentIndex = s_flags[URL_COUNTER] +
                s_flags[CONTENT_COUNTER] - flipFlop ;

            char* expect = "[Success]";
            if(flipFlop && urlIndex != contentIndex) {
                expect = "[Doc unchanged]";
            }

            log("sending url num %d with content num %d, flip %d expect %s",
                urlIndex, contentIndex, flipFlop, expect);
            sb.safePrintf("&c=qatest123&deleteurl=0&"
                          "format=xml&u=");
            sb.urlEncode ( s_urlPtrs[s_flags[URL_COUNTER]]);
            sb.safePrintf("&hasmime=1");
	    // add some meta data now, the current time stamp so we can
	    // make sure the meta data is updated even if its EDOCUNCHANGED
	    sb.safePrintf("&metadata=");
	    static int32_t s_count9 = 0;
	    SafeBuf tmp;
	    tmp.safePrintf("{\"qatesttime\":%"INT32"}\n",s_count9++);
	    sb.urlEncode ( tmp.getBufStart(), tmp.getLength() );
            sb.safePrintf("&content=");
            sb.urlEncode(s_contentPtrs[contentIndex]);

            sb.nullTerm();


            if(s_flags[CONTENT_COUNTER] >= 5) {
                s_flags[URL_COUNTER] += s_flags[CONTENT_COUNTER];
                s_flags[CONTENT_COUNTER] = 0;
            }
            s_flags[CONTENT_COUNTER]++;

            // if(s_flags[URL_COUNTER] >= 12) {
            //     s_flags[ADD_INITIAL_URLS] = true;
            // }

            //wait(1.0);
            if ( ! getUrl("/admin/inject",
                          0, // no idea what crc to expect
                          sb.getBufStart(),
                          expect,
                          g_timeAxisIgnore)
                 )
                return false;
            return false;
        }
		s_flags[ADD_INITIAL_URLS] = true;
	}

	if ( ! s_flags[WAIT_A_BIT] ) {
		wait(1.5);
		s_flags[3] = true;
		return false;
	}

	// this doc should have qatesttime:197 and qatesttime:198
	// since it had a EDOCUNCHANGED error the 2nd time around but
	// different metadata.
	if ( ! s_flags[EXAMINE_RESULTS1] ) {
	 	s_flags[EXAMINE_RESULTS1] = true;
	 	if ( ! getUrl ( "/search?c=qatest123&qa=1&"
				"format=json&"
				"q=qatesttime:197",
	 			702467314 ) )
	 		return false;
	}

    return true;
}

bool qaWarcFiles ( ) {
	if ( ! s_flags[DELETE_COLLECTION] ) {
		s_flags[DELETE_COLLECTION] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	if ( ! s_flags[ADD_COLLECTION] ) {
		s_flags[ADD_COLLECTION] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1&"
				"collectionips=127.0.0.1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	if ( ! s_flags[SET_PARAMETERS] ) {
		s_flags[SET_PARAMETERS] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// no spider replies because it messes
				// up our last test to make sure posdb
				// is 100% empty. 
				// see "index spider replies" in Parms.cpp.
				"&isr=0"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
				// This is what we are testing
				"&usetimeaxis=1"
				// we are indexing warc files
				"&indexwarcs=1"
				,
				// checksum of reply expected
				0 ) )
			return false;
	}


	//
	// Inject urls, return false if not done yet.
	// Here we alternate sending the same url -> content pair with sending 
	// the same url with different content to simulate a site that is updated
	// at about half the rate that we spider them.
	if ( s_flags[ADD_INITIAL_URLS] == 0) {
		s_flags[ADD_INITIAL_URLS]++;
		SafeBuf sb;

		sb.safePrintf("&c=qatest123"
					  "&format=json"
					  "&url=http://%s:%"INT32"/test.warc.gz"
				  , iptoa(g_hostdb.m_myHost->m_ip)
				  , (int32_t)g_hostdb.m_myHost->m_httpPort

			      );
		if ( ! getUrl ( "/admin/inject",0,sb.getBufStart()) )
			return false;
	}
	if ( s_flags[EXAMINE_RESULTS1] == 0) {
		s_flags[EXAMINE_RESULTS1]++;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe"
				"&dsrt=500",
				702467314 ) )
			return false;
	}


	if ( s_flags[ADD_INITIAL_URLS] == 1) {
		s_flags[ADD_INITIAL_URLS]++;

		SafeBuf sb;
		sb.safePrintf("&c=qatest123"
				"&format=json"
				"&url=http://%s:%"INT32"/test.arc.gz"
				, iptoa(g_hostdb.m_myHost->m_ip)
				, (int32_t)g_hostdb.m_myHost->m_httpPort);

		if ( ! getUrl ( "/admin/inject",0,sb.getBufStart()) )
			return false;
	}

	if ( s_flags[EXAMINE_RESULTS2] == 0) {
		s_flags[EXAMINE_RESULTS2]++;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe"
				"&dsrt=500",
				702467314 ) )
			return false;
	}
	return true;
}

bool qaInjectMetadata ( ) {
	if ( ! s_flags[DELETE_COLLECTION] ) {
		s_flags[DELETE_COLLECTION] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	if ( ! s_flags[ADD_COLLECTION] ) {
		s_flags[ADD_COLLECTION] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1&"
				"collectionips=127.0.0.1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	if ( ! s_flags[SET_PARAMETERS] ) {
		s_flags[SET_PARAMETERS] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// no spider replies because it messes
				// up our last test to make sure posdb
				// is 100% empty. 
				// see "index spider replies" in Parms.cpp.
				"&isr=0"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
				// This is what we are testing
				"&usetimeaxis=1"
				,
				// checksum of reply expected
				0 ) )
			return false;
	}


	//
	// Inject urls, return false if not done yet.
	// Here we alternate sending the same url -> content pair with sending 
	// the same url with different content to simulate a site that is updated
	// at about half the rate that we spider them.
	if ( s_flags[ADD_INITIAL_URLS] == 0) {

		char* metadata = "{\"testtest\":42,\"a-hyphenated-name\":5, "
			"\"a-string-value\":\"can we search for this\", "
			"\"an array\":[\"a\",\"b\", \"c\", 1,2,3], "
			"\"a field with spaces\":6, \"compound\":{\"field\":7}}";
		
		s_flags[ADD_INITIAL_URLS]++;
		SafeBuf sb;

		sb.safePrintf("&c=qatest123"
					  "&format=json"
					  "&spiderlinks=1"
					  "&url=http://%s:%"INT32"/test.warc.gz"
					  "&metadata=%s"
					  , iptoa(g_hostdb.m_myHost->m_ip)
					  , (int32_t)g_hostdb.m_myHost->m_httpPort
					  , metadata
					  );
		if ( ! getUrl ( "/admin/inject",0,sb.getBufStart()) )
			return false;
	}
	if ( s_flags[EXAMINE_RESULTS1] == 0) {
		s_flags[EXAMINE_RESULTS1]++;
		log("searching for metadata");
		if ( ! getUrl ( "/search?c=qatest123&q=testtest%3A42"
                        "&n=1000&sb=1&dr=0&sc=0&s=0&showerrors=1&format=json",
                        1,// Checksum
						NULL,
                        "hits\":106"
                        ) )
		  return false;
	}

	if ( s_flags[EXAMINE_RESULTS2] == 0) {
		s_flags[EXAMINE_RESULTS2]++;
		log("searching for metadata");
		if ( ! getUrl ( "/search?c=qatest123&q=a-hyphenated-name%3A5"
                        "&n=1000&sb=1&dr=0&sc=0&s=0&showerrors=1&format=json",
                        1,// Checksum
						NULL,
                        "hits\":106"
                        ) )
		  return false;
	}

	if ( s_flags[EXAMINE_RESULTS3] == 0) {
		s_flags[EXAMINE_RESULTS3]++;
		log("searching for metadata");
		if ( ! getUrl ( "/search?c=qatest123&q=a-string-value%3A\"can+we+search+for+this\""
                        "&n=1000&sb=1&dr=0&sc=0&s=0&showerrors=1&format=json",
                        1,// Checksum
						NULL,
                        "hits\":106"
                        ) )
		  return false;
	}

	return true;
}

bool qaMetadataFacetSearch ( ) {
	if ( ! s_flags[DELETE_COLLECTION] ) {
		s_flags[DELETE_COLLECTION] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	if ( ! s_flags[ADD_COLLECTION] ) {
		s_flags[ADD_COLLECTION] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1&"
				"collectionips=127.0.0.1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	if ( ! s_flags[SET_PARAMETERS] ) {
		s_flags[SET_PARAMETERS] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// no spider replies because it messes
				// up our last test to make sure posdb
				// is 100% empty. 
				// see "index spider replies" in Parms.cpp.
				"&isr=0"
				// turn off use robots to avoid that
				// xyz.com/robots.txt redir to seekseek.com
				"&obeyRobots=0"
                // This is what we are testing
				"&usetimeaxis=1"
				"&de=0"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// this only loads once
	loadUrls();
	int32_t numDocsToInject = s_ubuf2.length()/(int32_t)sizeof(char *);


	//
	// Inject urls, return false if not done yet.
	// Here we alternate sending the same url -> content pair with sending 
    // the same url with different content to simulate a site that is updated
    // at about half the rate that we spider them.
	if ( ! s_flags[ADD_INITIAL_URLS] ) {
		for ( ; s_flags[URL_COUNTER] < numDocsToInject ; s_flags[URL_COUNTER]++) {
            // inject using html api
            SafeBuf sb;

            char* expect = "[Success]";

            sb.safePrintf("&c=qatest123&deleteurl=0&"
                          "format=xml&u=");
            sb.urlEncode ( s_urlPtrs[s_flags[URL_COUNTER]]);
            sb.safePrintf("&hasmime=1");
            sb.safePrintf("&metadata= {\"string-facets\":\"testing %d\", \"number-facets\":%d }",
						  s_flags[URL_COUNTER] % 10,s_flags[URL_COUNTER] % 10);
            sb.safePrintf("&content=");
            sb.urlEncode(s_contentPtrs[s_flags[URL_COUNTER]]);
            sb.nullTerm();


			s_flags[URL_COUNTER]++;
            if ( ! getUrl("/admin/inject",
                          0, // no idea what crc to expect
                          sb.getBufStart(),
                          expect,
                          g_timeAxisIgnore)
                 )
                return false;
            return false;
        }
		s_flags[ADD_INITIAL_URLS] = true;
	}

	if ( ! s_flags[WAIT_A_BIT] ) {
		wait(1.5);
		s_flags[3] = true;
		return false;
	}

	// if ( ! s_flags[EXAMINE_RESULTS] ) {
	// 	s_flags[16] = true;
	// 	if ( ! getUrl ( "/search?c=qatest123&qa=1&q=%2Bthe"
	// 			"&dsrt=500",
	// 			702467314 ) )
	// 		return false;
	// }

    return true;
}



bool qaimport () {

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn spiders off so it doesn't spider while we are importing
	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/admin/spider?cse=0&qa=1&c=qatest123",
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// set the import dir and # inject threads
	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		if ( ! getUrl ( "/admin/import?c=qatest123&importdir=%2Fhome%2Fmwells%2Ftesting%2Fimport%2F&numimportinjects=3&import=1&action=submit",
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// wait for importloop to "kick in" so it can set cr->m_importState
	if ( ! s_flags[3] ) {
	 	wait(1.0);
	 	s_flags[3] = true;
	 	return false;
	}

	// import must be done!
	if ( ! s_flags[19] ) {
		CollectionRec *cr = g_collectiondb.getRec("qatest123");
		// if still importing this will be non-null
		if ( cr->m_importState ) {
			wait(1.0);
			return false;
		}
		// all done then
		s_flags[19] = true;
	}

	// wait for absorption of index
	if ( ! s_flags[28] ) {
	 	wait(2.0);
	 	s_flags[28] = true;
	 	return false;
	}

	// test query
	if ( ! s_flags[16] ) {
		s_flags[16] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe"
				"&dsrt=500",
				702467314 ) )
			return false;
	}


	// test site clustering
	if ( ! s_flags[29] ) {
		s_flags[29] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=mediapost&dsrt=0&sc=1",
				702467314 ) )
			return false;
	}


	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED DATA "
		    "IMPORT TEST");
		//if ( s_callback == qainject ) exit(0);
		return true;
	}


	return true;
}




bool qainlinks() {

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// turn spiders off so it doesn't spider while we are importing
	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/admin/spider?cse=0&qa=1&c=qatest123",
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// inject youtube
	if ( ! s_flags[2] ) {
		s_flags[2] = true;
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&"
			       "format=xml&u=www.youtube.com");
		if ( ! getUrl ( sb.getBufStart() , 999 ) )
			return false;
	}

	// test query
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=youtube"
				,702467314 ) )
			return false;
	}



	// scrape inlinkers
	if ( ! s_flags[4] ) {
		s_flags[4] = true;
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&"
			       "format=xml&qts=link:www.youtube.com&n=100");
		if ( ! getUrl ( sb.getBufStart() , 999 ) )
			return false;
	}

	// inject better inlinkers
	if ( ! s_flags[20] ) {
		s_flags[20] = true;
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&"
			       "format=xml&"
			       "url=www.freebsd.org%%2Fcommunity.html");
		if ( ! getUrl ( sb.getBufStart() , 999 ) )
			return false;
	}




	// wait a second for linkdb absorption
	if ( ! s_flags[5] ) {
	 	wait(1.0);
	 	s_flags[5] = true;
	 	return false;
	}



	// RE-inject youtube
	if ( ! s_flags[6] ) {
		s_flags[6] = true;
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&"
			       "format=xml&u=www.youtube.com");
		if ( ! getUrl ( sb.getBufStart() , 999 ) )
			return false;
	}


	// wait a second term freq stabilization
	if ( ! s_flags[9] ) {
	 	wait(2.0);
	 	s_flags[9] = true;
	 	return false;
	}

	// test query
	if ( ! s_flags[7] ) {
		s_flags[7] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&"
				"format=xml&q=youtube"
				// get scoring info
				"&scores=1"
				,702467314 ) )
			return false;
	}




	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED INLINK TEST");
		//if ( s_callback == qainject ) exit(0);
		return true;
	}


	return true;
}

// query reindex test
bool qareindex() {

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn off images thumbnails
	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1",
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// this only loads once
	loadUrls();
	int32_t max = s_ubuf2.length()/(int32_t)sizeof(char *);
	//max = 1;

	//
	// inject urls, return false if not done yet
	//
	//static bool s_x4 = false;
	if ( ! s_flags[2] ) {
		// TODO: try delimeter based injection too
		//static int32_t s_ii = 0;
		for ( ; s_flags[20] < max ; ) {
			// inject using html api
			SafeBuf sb;
			sb.safePrintf("&c=qatest123&deleteurl=0&"
				      "format=xml&u=");
			sb.urlEncode ( s_urlPtrs[s_flags[20]] );
			// the content
			sb.safePrintf("&hasmime=1");
			// sanity
			//if ( strstr(s_urlPtrs[s_flags[20]],"wdc.htm") )
			//	log("hey");
			sb.safePrintf("&content=");
			sb.urlEncode(s_contentPtrs[s_flags[20]] );
			sb.nullTerm();
			// pre-inc it in case getUrl() blocks
			s_flags[20]++;//ii++;
			if ( ! getUrl("/admin/inject",
				      0, // no idea what crc to expect
				      sb.getBufStart()) )
				return false;
		}
		s_flags[2] = true;
	}

	// wait for absorption
	if ( ! s_flags[3] ) {
		wait(1.5);
		s_flags[3] = true;
		return false;
	}

	// query for 'test'
	if ( ! s_flags[27] ) {
		s_flags[27] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=17&format=xml&q=test&icc=1",
				-1672870556 ) )
			return false;
	}

	// make 2nd url filter !isreindex just have 0 spiders so we do
	// not spider the links from the REINDEXED PAGES
	if ( ! s_flags[4] ) {
		s_flags[4] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&"
			      // make it the custom filter
			      "ufp=custom&"
			      // zero spiders if not isreindex
			      "fe1=default&hspl1=0&hspl1=1&fsf1=1.000000&"
			      "fdu1=0&"
			      "mspr1=0&mspi1=0&xg1=1000&fsp1=45&"
		);
		if ( ! getUrl ( "/admin/filters",0,sb.getBufStart()) )
			return false;
	}



	// do the query reindex on 'test'
	if ( ! s_flags[16] ) {
		s_flags[16] = true;
		if ( ! getUrl ( "/admin/reindex?c=qatest123&qa=16&"
				"format=xml&q=test"
				, 702467314 ) )
			return false;
	}

 checkagain2:
	// wait until spider finishes. check the spider status page
	// in json to see when completed
	if ( ! s_flags[5] ) {
		wait(3.0);
		s_flags[5] = true;
		return false;
	}


	// wait for all spiders to stop
	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/admin/status?format=json&c=qatest123",0) )
			return false;
	}

	//static bool s_k2 = false;
	if ( ! s_flags[6] ) {
		// ensure spiders are done. 
		// "Nothing currently available to spider"
		if ( s_content&&!strstr(s_content,"Nothing currently avail")){
			s_flags[5] = false;
			s_flags[15] = false;
			goto checkagain2;
		}
		s_flags[6] = true;
	}

	//
	// query for 'test' again after the reindex
	//
	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=14&format=xml&q=test&icc=1",
				-1672870556 ) )
			return false;
	}

	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED "
		    "QUERY REINDEX");
		//if ( s_callback == qainject ) exit(0);
		return true;
	}


	return true;
}


/*
static char *s_urls1 =
	" walmart.com"
	" cisco.com"
	" t7online.com"
	" sonyericsson.com"
	" netsh.com"
	" allegro.pl"
	" hotscripts.com"
	" sitepoint.com"
	" so-net.net.tw"
	" aol.co.uk"
	" sbs.co.kr"
	" chinaacc.com"
	" eyou.com"
	" spray.se"
	" carview.co.jp"
	" xcar.com.cn"
	" united.com"
	" raaga.com"
	" primaryads.com"
	" szonline.net"
	" icbc.com.cn"
	" instantbuzz.com"
	" sz.net.cn"
	" 6to23.com"
	" seesaa.net"
	" tracking101.com"
	" jubii.dk"
	" 5566.net"
	" prikpagina.nl"
	" 7xi.net"
	" 91.com"
	" jjwxc.com"
	" adbrite.com"
	" hoplay.com"
	" questionmarket.com"
	" telegraph.co.uk"
	" trendmicro.com"
	" google.fi"
	" ebay.es"
	" tfol.com"
	" sleazydream.com"
	" websearch.com"
	" freett.com"
	" dayoo.com"
	" interia.pl"
	" yymp3.com"
	" stanford.edu"
	" time.gr.jp"
	" telia.com"
	" madthumbs.com"
	" chinamp3.com"
	" oldgames.se"
	" buy.com"
	" singpao.com"
	" cbsnews.com"
	" corriere.it"
	" cbs.com"
	" flickr.com"
	" theglobeandmail.com"
	" incredifind.com"
	" mit.edu"
	" chase.com"
	" ktv666.com"
	" oldnavy.com"
	" lego.com"
	" eniro.se"
	" bloomberg.com"
	" ft.com"
	" odn.ne.jp"
	" pcpop.com"
	" ugameasia.com"
	" cantv.net"
	" allinternal.com"
	" aventertainments.com"
	" invisionfree.com"
	" hangzhou.com.cn"
	" zhaopin.com"
	" bcentral.com"
	" lowes.com"
	" adprofile.net"
	" yninfo.com"
	" jeeran.com"
	" twbbs.net.tw"
	" yousendit.com"
	" aavalue.com"
	" google.com.co"
	" mysearch.com"
	" worldsex.com"
	" navisearch.net"
	" lele.com"
	" msn.co.in"
	" officedepot.com"
	" xintv.com"
	" 204.177.92.193"
	" travelzoo.com"
	" bol.com.br"
	" dtiserv2.com"
	" optonline.net"
	" hitslink.com"
	" freechal.com"
	" infojobs.net"
	;
*/

bool qaspider1 ( ) {
	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn off images thumbnails
	// set max spiders to 1 for consistency!
	if ( ! s_flags[24] ) {
		s_flags[24] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// so site2:www.walmart.com works
                                "&isr=1"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// restrict hopcount to 0 or 1 in url filters so we do not spider
	// too deep
	//static bool s_z1 = false;
	if ( ! s_flags[2] ) {
		s_flags[2] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&"
			      // make it the custom filter
			      "ufp=custom&"

	       "fdu=0&fe=%%21ismanualadd+%%26%%26+%%21insitelist&hspl=0&hspl=1&fsf=0.000000&mspr=0&mspi=1&xg=1000&fsp=-3&"

			      // take out hopcount for now, just test quotas
			      //	       "fe1=tag%%3Ashallow+%%26%%26+hopcount%%3C%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=3&"

			      // just one spider out allowed for consistency
	       "fdu1=0&fe1=tag%%3Ashallow+%%26%%26+sitepages%%3C%%3D20&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=45&"

	       "fdu2=0&fe2=default&hspl2=0&hspl2=1&fsf2=1.000000&mspr2=0&mspi2=1&xg2=1000&fsp2=45&"

		);
		if ( ! getUrl ( "/admin/filters",0,sb.getBufStart()) )
			return false;
	}

	// set the site list to 
	// a few sites
	//static bool s_z2 = false;
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&format=xml&sitelist=");
		sb.urlEncode("tag:shallow site:www.walmart.com\r\n"
			     "tag:shallow site:http://www.ibm.com/\r\n");
		sb.nullTerm();
		if ( ! getUrl ("/admin/settings",0,sb.getBufStart() ) )
			return false;
	}
		
	//
	// use the add url interface now
	// walmart.com above was not seeded because of the site: directive
	// so this will seed it.
	//
	//static bool s_y2 = false;
	if ( ! s_flags[4] ) {
		s_flags[4] = true;
		SafeBuf sb;
		// delim=+++URL:
		sb.safePrintf("&c=qatest123"
			      "&format=json"
			      "&strip=1"
			      "&spiderlinks=1"
			      "&urls=www.walmart.com+ibm.com"
			      );
		// . now a list of websites we want to spider
		// . the space is already encoded as +
		//sb.urlEncode(s_urls1);
		if ( ! getUrl ( "/admin/addurl",0,sb.getBufStart()) )
			return false;
	}

	//
	// wait for spidering to stop
	//
 checkagain:

	// wait until spider finishes. check the spider status page
	// in json to see when completed
	//static bool s_k1 = false;
	if ( ! s_flags[5] ) {
		// wait 5 seconds, call sleep timer... then call qatest()
		//usleep(5000000); // 5 seconds
		wait(3.0);
		s_flags[5] = true;
		return false;
	}

	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/admin/status?format=json&c=qatest123",0) )
			return false;
	}

	//static bool s_k2 = false;
	if ( ! s_flags[6] ) {
		// ensure spiders are done. 
		// "Nothing currently available to spider"
		if ( s_content&&!strstr(s_content,"Nothing currently avail")){
			s_flags[5] = false;
			s_flags[15] = false;
			goto checkagain;
		}
		s_flags[6] = true;
	}


	// wait for index msg4 to not be cached to ensure all results indexed
	if ( ! s_flags[22] ) {
		s_flags[22] = true;
		wait(1.5);
	}


	// verify no results for gbhopcount:2 query
	//static bool s_y4 = false;
	if ( ! s_flags[7] ) {
		s_flags[7] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbhopcount%3A2",
				-1672870556 ) )
			return false;
	}

	// but some for gbhopcount:0 query
	//static bool s_t0 = false;
	if ( ! s_flags[8] ) {
		s_flags[8] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbhopcount%3A0",
				908338607 ) )
			return false;
	}
	
	// check facet sections query for walmart
	//static bool s_y5 = false;
	if ( ! s_flags[9] ) {
		s_flags[9] = true;
		if ( ! getUrl ( "/search?c=qatest123&format=json&stream=1&"
				"q=gbfacetstr%3Agbxpathsitehash2492664135",
				55157060 ) )
			return false;
	}

	//static bool s_y6 = false;
	if ( ! s_flags[10] ) {
		s_flags[10] = true;
		if ( ! getUrl ( "/get?page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=9861563119&cnsp=0" , 999 ) )
			return false;
	}

	// in xml
	//static bool s_y7 = false;
	if ( ! s_flags[11] ) {
		s_flags[11] = true;
		if ( ! getUrl ( "/get?xml=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=9861563119&cnsp=0" , 999 ) )
			return false;
	}

	// and json
	//static bool s_y8 = false;
	if ( ! s_flags[12] ) {
		s_flags[12] = true;
		if ( ! getUrl ( "/get?json=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=9861563119&cnsp=0" , 999 ) )
			return false;
	}


	// delete the collection
	//static bool s_fee = false;
	// if ( ! s_flags[13] ) {
	// 	s_flags[13] = true;
	// 	if ( ! getUrl ( "/admin/delcoll?delcoll=qatest123" ) )
	// 		return false;
	// }

	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbssSubdomain%3Awww.walmart.com+"
				"gbsortbyint%3AgbssDownloadStartTime",
				999 ) )
			return false;
	}

	// xpath is like a title here i think. check the returned
	// facet table in the left column
	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=html&"
				"q=gbfacetstr%3Agbxpathsitehash3624590799"
				, 999 ) )
			return false;
	}

	if ( ! s_flags[19] ) {
		s_flags[19] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&xml=1&"
				"q=gbfacetint%3Agbhopcount"
				, 999 ) )
			return false;
	}

	if ( ! s_flags[20] ) {
		s_flags[20] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&json=1&"
				"q=gbfacetint%3Alog.score"
				, 999 ) )
			return false;
	}

	if ( ! s_flags[21] ) {
		s_flags[21] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&xml=1&"
				"q=gbfacetfloat%3Atalks.rating"
				, 999 ) )
			return false;
	}

	if ( ! s_flags[23] ) {
		s_flags[23] = true;
		// test facets mixed with gigabits in left hand column
		if ( ! getUrl ( "/search?c=qatest123&qa=1&html=1&"
				"q=gbfacetint%3Agbhopcount+walmart"
				, 999 ) )
			return false;
	}


	//static bool s_fee2 = false;
	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		log("qa: SUCCESSFULLY COMPLETED "
			"QA SPIDER1 TEST");
		return true;
	}

	return true;
}

bool qaspider2 ( ) {

	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn off images thumbnails
	if ( ! s_flags[24] ) {
		s_flags[24] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1",
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// restrict hopcount to 0 or 1 in url filters so we do not spider
	// too deep
	//static bool s_z1 = false;
	if ( ! s_flags[2] ) {
		s_flags[2] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&"
			      // make it the custom filter
			      "ufp=custom&"

	       "fdu=0&fe=%%21ismanualadd+%%26%%26+%%21insitelist&hspl=0&hspl=1&fsf=0.000000&mspr=0&mspi=1&xg=1000&fsp=-3&"

			      // take out hopcount for now, just test quotas
			      //	       "fe1=tag%%3Ashallow+%%26%%26+hopcount%%3C%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=3&"

			      // sitepages is a little fuzzy so take it
			      // out for this test and use hopcount!!!
			      //"fe1=tag%%3Ashallow+%%26%%26+sitepages%%3C%%3D20&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=45&"
			      "fdu1=0&fe1=tag%%3Ashallow+%%26%%26+hopcount<%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=45&"

	       "fdu2=0&fe2=default&hspl2=0&hspl2=1&fsf2=1.000000&mspr2=0&mspi2=1&xg2=1000&fsp2=45&"

		);
		if ( ! getUrl ( "/admin/filters",0,sb.getBufStart()) )
			return false;
	}

	// set the site list to 
	// a few sites
	// these should auto seed so no need to use addurl
	//static bool s_z2 = false;
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&format=xml&sitelist=");
		sb.urlEncode(//walmart has too many pages at depth 1, so remove it
			     //"tag:shallow www.walmart.com\r\n"
			     "tag:shallow http://www.ibm.com/\r\n");
		sb.nullTerm();
		if ( ! getUrl ("/admin/settings",0,sb.getBufStart() ) )
			return false;
	}
		

	//
	// wait for spidering to stop
	//
 checkagain:

	// wait until spider finishes. check the spider status page
	// in json to see when completed
	//static bool s_k1 = false;
	if ( ! s_flags[4] ) {
		//usleep(5000000); // 5 seconds
		s_flags[4] = true;
		wait(3.0);
		return false;
	}

	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		if ( ! getUrl ( "/admin/status?format=json&c=qatest123",0) )
			return false;
	}

	//static bool s_k2 = false;
	if ( ! s_flags[5] ) {
		// ensure spiders are done. 
		// "Nothing currently available to spider"
		if ( s_content&&!strstr(s_content,"Nothing currently avail")){
			s_flags[4] = false;
			s_flags[14] = false;
			goto checkagain;
		}
		s_flags[5] = true;
	}




	// verify no results for gbhopcount:2 query
	//static bool s_y4 = false;
	if ( ! s_flags[6] ) {
		s_flags[6] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbhopcount%3A2",
				-1310551262 ) )
			return false;
	}

	// but some for gbhopcount:0 query
	//static bool s_t0 = false;
	if ( ! s_flags[7] ) {
		s_flags[7] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&n=500&"
				"q=gbhopcount%3A0",
				999 ) )
			return false;
	}
	
	// check facet sections query for walmart
	//static bool s_y5 = false;
	if ( ! s_flags[8] ) {
		s_flags[8] = true;
		if ( ! getUrl ( "/search?c=qatest123&format=json&stream=0&"
				"q=gbfacetstr%3Agbxpathsitehash3311332088",
				999 ) )
			return false;
	}

	// wait for some reason
	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		wait(1.5);
		return false;
	}



	//static bool s_y6 = false;
	// 102573507011 docid is 
	// http://www.ibm.com/smarterplanet/us/en/overview/ideas/
	if ( ! s_flags[9] ) {
		s_flags[9] = true;
		if ( ! getUrl ( "/get?page=4&q=gbfacetstr:gbxpathsitehash3311332088&qlang=xx&c=qatest123&d=102573507011&cnsp=0" , 999 ) )
			return false;
	}

	// in xml
	//static bool s_y7 = false;
	if ( ! s_flags[10] ) {
		s_flags[10] = true;
		if ( ! getUrl ( "/get?xml=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=102573507011&cnsp=0" , 999 ) )
			return false;
	}

	// and json
	//static bool s_y8 = false;
	if ( ! s_flags[11] ) {
		s_flags[11] = true;
		if ( ! getUrl ( "/get?json=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=102573507011&cnsp=0" , 999 ) )
			return false;
	}


	if ( ! s_flags[12] ) {
		s_flags[12] = true;
		if ( ! getUrl ( "/search?json=1&q=ibm.com&qlang=xx&"
				"c=qatest123" , 999 ) )
			return false;
	}


	// delete the collection
	//static bool s_fee = false;
	// if ( ! s_flags[12] ) {
	// 	s_flags[12] = true;
	// 	if ( ! getUrl ( "/admin/delcoll?delcoll=qatest123" ) )
	// 		return false;
	// }

	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED "
		    "QA SPIDER2 TEST");
		return true;
	}

	return true;
}

bool qascrape ( ) {
	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}


	// turn off images thumbnails
	if ( ! s_flags[24] ) {
		s_flags[24] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1",
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// scrape it
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&"
			       "format=xml&qts=test");
		if ( ! getUrl ( sb.getBufStart() , 999 ) )
			return false;
	}
		


	// verify no results for gbhopcount:2 query
	//static bool s_y4 = false;
	if ( ! s_flags[6] ) {
		s_flags[6] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=test",
				-1310551262 ) )
			return false;
	}


	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED "
		    "QA SCRAPE TEST");
		return true;
	}

	return true;
}

static char *s_ubuf4 = 
	"http://www.nortel.com/multimedia/flash/mediaplayer/config/solutions_enterprisesecurity.json "
	"http://quirksmode.org/m/d/md.json "
	"http://www.chip.de/headfoot/json/8659753/tk.json?t=11-02-08-13-32 "
	"http://developer.apple.com/wwdc/data/sessions.json "
	"http://www.bbc.co.uk/radio4/programmes/schedules/fm/today.json "
	"http://www.hellonorthgeorgia.com/slideShowJSON11034.json "
	"http://www.metastatic.org/log-4.json "
	"http://www.metastatic.org/log.json "
	"http://www.textsfromlastnight.com/Vote-Down-Text-24266.json "
	"http://www.textsfromlastnight.com/Vote-Up-Text-13999.json "
	"http://shapewiki.com/shapes/4755.json "
	"http://shapewiki.com/shapes/40.json "
	"http://www.neocol.com/news/hcc-international-appoint-neocol-as-information-management-partner.json "
	"http://www.bbc.co.uk/programmes/b00vy3l1.json "
	"http://iwakura.clipp.in/feed.json "
	"http://schwarzlich.clipp.in/feed.json "
	"http://freethefoxes.googlecode.com/svn/trunk/lang/sv.json "
	"http://www.domik.net/data/vCard1.json "
	"http://www.domik.net/data/vCard14205.json "
	"http://www.chip.de/headfoot/json/8659753/handy.json?t=11-02-08-13-32 "
	"http://www.neocol.com/news/neocol-relocates-to-new-expanded-hq.json "
	"http://www.nbafinals.com/video/channels/nba_tv/2009/07/23/nba_20090723_1fab5_pistons.nba.json "
	"http://quiltid.com/feeds/me/blake.json "
	"http://parliament.southgatelabs.com/members.json "
	"http://www.funradio.fr/service/carrousel.json?home "
	"http://doyouflip.com/dcefd5cffeecebcabc049a8a1cc18fac/bundle.json "
	"http://freethefoxes.googlecode.com/svn/trunk/lang/sch.json "
	"http://delphie.clipp.in/feed.json "
	"http://gotgastro.com/notices.json "
	"http://www.paralela45bacau.ro/ajax/newsletter.json "
	"http://www.elstoleno.com/unsorted.json "
	"http://papanda.clipp.in/feed.json "
	"http://d.yimg.com/b/api/data/us/news/elections/2010/result/us_house.json "
	"http://www.nba.co.nz/video/teams/sixers/2009/07/28/090727lou.sixers.json "
	"http://n2.talis.com/svn/playground/mmmmmrob/OpenLibrary/tags/day1/data/authors.1in10.json "
	"http://asn.jesandco.org/resources/D2364040_manifest.json "
	"http://search.twitter.com/search.json?q=from%3ADrathal "
	"http://www.matthiresmusic.com/3f6524261baf47acc61d3fb22ab9b18a/bundle.json "
	"http://search.twitter.com/search.json?q= "
	"http://www.christinaperri.com/98a59708246eb4fcc4e22a09113699c6/bundle.json "
	"http://www.misterbluesky.nl/News.json "
	"http://ymorimo.clipp.in/feed.json "
	"http://wedata.net/databases.json "
	"http://cms.myspacecdn.com/cms/api/opensearch.json "
	"http://seria.clipp.in/feed.json "
	"http://www.treysongz.com/6b10fcf3a6f99b4622e4d33d1532b380/bundle.json "
	"http://psychedesire.clipp.in/feed.json "
	"http://www.sekaino.com/skedu/demodata/dev_data_ccmixter.json "
	"http://www.360wichita.com/slideShowJSON8496.json "
	"http://speakerrate.com/events/856-jquery-conference-2011-san-francisco-bay-area.json "
	;

bool qajson ( ) {
	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn off images thumbnails
	if ( ! s_flags[24] ) {
		s_flags[24] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1"
				// index spider replies status docs
				"&isr=1"
				,
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// add the 50 urls
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;

		sb.safePrintf("&c=qatest123"
			      "&format=json"
			      "&strip=1"
			      "&spiderlinks=0"
			      "&urls="//www.walmart.com+ibm.com"
			      );
		sb.urlEncode ( s_ubuf4 );
		sb.nullTerm();
		// . now a list of websites we want to spider
		// . the space is already encoded as +
		if ( ! getUrl ( "/admin/addurl",0,sb.getBufStart()) )
			return false;
	}


	//
	// wait for spidering to stop
	//
 checkagain:

	// wait until spider finishes. check the spider status page
	// in json to see when completed
	//static bool s_k1 = false;
	if ( ! s_flags[5] ) {
		// wait 5 seconds, call sleep timer... then call qatest()
		//usleep(5000000); // 5 seconds
		wait(3.0);
		s_flags[5] = true;
		return false;
	}

	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/admin/status?format=json&c=qatest123",0) )
			return false;
	}

	//static bool s_k2 = false;
	if ( ! s_flags[6] ) {
		// ensure spiders are done. 
		// "Nothing currently available to spider"
		if ( s_content&&!strstr(s_content,"Nothing currently avail")){
			s_flags[5] = false;
			s_flags[15] = false;
			goto checkagain;
		}
		s_flags[6] = true;
	}

		

	if ( ! s_flags[7] ) {
		s_flags[7] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=type%3Ajson+meta.authors%3Appk",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[8] ) {
		s_flags[8] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&n=100&"
				"q=type%3Ajson",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[9] ) {
		s_flags[9] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfacetstr%3Ameta.authors",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[10] ) {
		s_flags[10] = true;
		// this has > 50 values for the facet field hash
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfacetstr%3Astrings.key",
				-1310551262 ) )
			return false;
	}


	// other query tests...
	if ( ! s_flags[12] ) {
		s_flags[12] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbssUrl%3Aquirksmode.org%2Fm%2F",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=site%3Aquirksmode.org",
				-1310551262 ) )
			return false;
	}
	

	// test gbfieldmatch:field:"quoted value" query to ensure it converts
	// the quoted value into the right int32
	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key"
				"%3Ainvestigate-tweet",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key"
				"%3A\"Maemo+Browser\"",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[16] ) {
		s_flags[16] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key"
				"%3A\"Google+Wireless+Transcoder\"",
				-1310551262 ) )
			return false;
	}

	// this should have no results, not capitalized
	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key%3A\"samsung\"",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key%3ASamsung",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key%3A\"Samsung\"",
				-1310551262 ) )
			return false;
	}



	//static bool s_fee2 = false;
	if ( ! s_flags[20] ) {
		s_flags[20] = true;
		log("qa: SUCCESSFULLY COMPLETED "
		    "QA JSON TEST");
		return true;
	}

	return true;
}

static char *s_ubuf5 = 
	"http://www.thompsoncancer.com/News/RSSLocation2.ashx?sid=7 "
	"http://www.jdlculaval.com/xmlrpc.php?rsd "
	"http://pharmacept.com/feed/ "
	"http://www.web-erfolg.net/feed/ "
	"http://www.extremetriathlon.org/site/feed/ "
	"http://www.pilatesplusdublin.ie/wp-includes/wlwmanifest.xml "
	"http://www.youtube.com/oembed?url=http%3A//www.youtube.com/watch?v%3Dv0lZQVaXSyM&format=xml "
	"http://www.ehow.com/feed/home/garden-lawn/lawn-mowers.rss "
	"http://www.functionaltrainingpro.com/xmlrpc.php?rsd "
	"http://mississippisociety.com/index.php/feed "
	;
				      ;

bool qaxml ( ) {
	//
	// delete the 'qatest123' collection
	//
	//static bool s_x1 = false;
	if ( ! s_flags[0] ) {
		s_flags[0] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}

	//
	// add the 'qatest123' collection
	//
	//static bool s_x2 = false;
	if ( ! s_flags[1] ) {
		s_flags[1] = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// turn off images thumbnails
	if ( ! s_flags[24] ) {
		s_flags[24] = true;
		if ( ! getUrl ( "/admin/spider?c=qatest123&qa=1&mit=0&mns=1",
				// checksum of reply expected
				238170006 ) )
			return false;
	}

	// add the 50 urls
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;

		sb.safePrintf("&c=qatest123"
			      "&format=json"
			      "&strip=1"
			      "&spiderlinks=0"
			      "&urls="//www.walmart.com+ibm.com"
			      );
		sb.urlEncode ( s_ubuf5 );
		sb.nullTerm();
		// . now a list of websites we want to spider
		// . the space is already encoded as +
		if ( ! getUrl ( "/admin/addurl",0,sb.getBufStart()) )
			return false;
	}


	//
	// wait for spidering to stop
	//
 checkagain:

	// wait until spider finishes. check the spider status page
	// in json to see when completed
	//static bool s_k1 = false;
	if ( ! s_flags[5] ) {
		// wait 5 seconds, call sleep timer... then call qatest()
		//usleep(5000000); // 5 seconds
		wait(3.0);
		s_flags[5] = true;
		return false;
	}

	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/admin/status?format=json&c=qatest123",0) )
			return false;
	}

	//static bool s_k2 = false;
	if ( ! s_flags[6] ) {
		// ensure spiders are done. 
		// "Nothing currently available to spider"
		if ( s_content&&!strstr(s_content,"Nothing currently avail")){
			s_flags[5] = false;
			s_flags[15] = false;
			goto checkagain;
		}
		s_flags[6] = true;
	}

		

	if ( ! s_flags[7] ) {
		s_flags[7] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=type%3Axml+oembed.type%3Avideo",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[8] ) {
		s_flags[8] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=video",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[9] ) {
		s_flags[9] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=oembed.thumbnail_height%3A360",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[10] ) {
		s_flags[10] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbminint%3Aoembed.thumbnail_height%3A380",
				-1310551262 ) )
			return false;
	}


	// other query tests...
	if ( ! s_flags[12] ) {
		s_flags[12] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbmaxint%3Aoembed.thumbnail_height%3A380",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=rss.channel.item.title%3Abests",
				-1310551262 ) )
			return false;
	}
	

	if ( ! s_flags[14] ) {
		s_flags[14] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&"
				"q=gbfacetstr%3Arss.channel.title",
				-1310551262 ) )
			return false;
	}

	/*
	if ( ! s_flags[15] ) {
		s_flags[15] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key"
				"%3A\"Maemo+Browser\"",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[16] ) {
		s_flags[16] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key"
				"%3A\"Google+Wireless+Transcoder\"",
				-1310551262 ) )
			return false;
	}

	// this should have no results, not capitalized
	if ( ! s_flags[17] ) {
		s_flags[17] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key%3A\"samsung\"",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key%3ASamsung",
				-1310551262 ) )
			return false;
	}

	if ( ! s_flags[18] ) {
		s_flags[18] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=json&"
				"q=gbfieldmatch%3Astrings.key%3A\"Samsung\"",
				-1310551262 ) )
			return false;
	}
	*/


	//static bool s_fee2 = false;
	if ( ! s_flags[20] ) {
		s_flags[20] = true;
		log("qa: SUCCESSFULLY COMPLETED "
		    "QA XML TEST");
		return true;
	}

	return true;
}




/*
bool qaspider ( ) {

	if ( ! s_callback ) s_callback = qaspider;

	// do first qa test for spider
	// returns true when done, false when blocked
	if ( ! qaspider1() ) return false;

	// do second qa test for spider
	// returns true when done, false when blocked
	if ( ! qaspider2() ) return false;

	return true;
}
*/

static QATest s_qatests[] = {

	{qainject1,
	 "injectTest1",
	 "Test injection api. Test injection of multiple urls with content. "
	 "Test deletion of urls via inject api. Test most query api parms. "
	 "Test advanced search parms."},

	{qainject2,
	 "injectTest2",
	 "Test injection api. Test delimeter-based injection of single file. "
	 "test tml ns smxcpl sw showimages sc search parms."},

	{qaspider1,
	 "spiderSitePagesTest",
	 "Test spidering walmart.com and ibm.com using sitepages quota. "
	 "Test facets."},

	{qaspider2,
	 "spiderHopCountTest",
	 "Test spidering ibm.com using hopcount limit."},

	{qascrape,
	 "queryScrapeTest",
	 "Scrape and inject results from google and bing."},

	{qajson,
	 "jsonTest",
	 "Add Url some JSON pages and test json-ish queries. Test facets over "
	 "json docs."},

	{qaxml,
	 "xmlTest",
	 "Add Url some XML pages and test xml-ish queries. Test facets over "
	 "xml docs."},

	// {qaimport,
	//  "importDataTest",
	//  "Test data import functionality. Test site clustering."},

	{qainlinks,
	 "inlinksTest",
	 "Test youtube inlinks. Test EDOCUNCHANGED iff just inlinks change."},

	{qareindex,
	 "queryReindexTest",
	 "Test query reindex function. Ensure changed docs are updated."},

	{qaSyntax,
	 "querySyntaxTest",
	 "Test the queries in the syntax.html page and inject injectmedemo."},

	{qaTimeAxis,
	 "timeAxisTest",
	 "Use Inject api to inject the same url at different times, "
	 "sometimes changed and sometimes not.  Ensure docId is different "
	 "when content has changed, even if the url is the same. "},


	// {qaWarcFiles,
	//  "indexWarcFiles",
	//  "Ensure the spider handles arc.gz and warc.gz file formats."},

	{qaInjectMetadata,
	 "injectMetadata",
	 "When we pass json encoded metadata to an injection, make sure we can"
     "search for the fields."},

	{qaMetadataFacetSearch,
	 "metadatafacetsearch",
	 "When we pass json encoded metadata to an injection, make sure the"
     "metadata is faceted properly."}


};

void resetFlags() {
	int32_t n = sizeof(s_qatests)/sizeof(QATest);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		memset(qt->m_flags,0,4*MAXFLAGS);
	}
}

// . run a series of tests to ensure that gb is functioning properly
// . uses the ./qa subdirectory to hold archive pages, ips, spider dates to
//   ensure consistency between tests for exact replays
bool qatest ( ) {

	if ( s_registered ) {
		g_loop.unregisterSleepCallback(NULL,qatestWrapper);
		s_registered = false;
		log("qa: done waiting");
	}

	if ( ! s_callback ) s_callback = qatest;

	if ( ! g_qaSock ) return true;


	// returns true when done, false when blocked
	//if ( ! qainject ( ) ) return false;

	// returns true when done, false when blocked
	//if ( ! qaspider ( ) ) return false;

	int32_t n = sizeof(s_qatests)/sizeof(QATest);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		if ( ! qt->m_doTest ) continue;
		// store that
		s_qt = qt;
		// point to flags
		s_flags = qt->m_flags;
		// call the qatest
		if ( ! qt->m_func() ) return false;
	}

	// save this
	saveHashTable();
	// do not reset since we don't reload it above!
	//s_ht.reset();

	//if ( g_numErrors )
	//	g_qaOutput.safePrintf("<input type=submit value=submit><br>");

	g_qaOutput.safePrintf("<br>DONE RUNNING QA TESTS<br>");


	// . print the output
	// . the result of each test is stored in the g_qaOutput safebuf
	g_httpServer.sendDynamicPage(g_qaSock,
				     g_qaOutput.getBufStart(),
				     g_qaOutput.length(),
				     -1/*cachetime*/);

	g_qaOutput.purge();

	g_qaSock = NULL;

	return true;
}

#include "Parms.h"
#include "Pages.h"

bool sendPageQA ( TcpSocket *sock , HttpRequest *hr ) {
	char pbuf[32768];
	SafeBuf sb(pbuf, 32768);

	//char format = hr->getReplyFormat();

	// set this. also sets gr->m_hr
	GigablastRequest gr;
	// this will fill in GigablastRequest so all the parms we need are set
	g_parms.setGigablastRequest ( sock , hr , &gr );


	//
	// . handle a request to update the crc for this test
	// . test id identified by "ajaxUrlHash" which is the hash of the test's url
	//   and the test name, QATest::m_testName
	int32_t ajax = hr->getLong("ajax",0);
	uint32_t ajaxUrlHash ;
	ajaxUrlHash = (uint64_t)hr->getLongLong("uh",0LL);
	uint32_t ajaxCrc ;
	ajaxCrc = (uint64_t)hr->getLongLong("crc",0LL);

	if ( ajax ) {
		// make sure it is initialized
		if ( s_ht.m_ks ) {
			// overwrite current value with provided one because 
			// the user click on an override checkbox to update 
			// the crc
			s_ht.addKey ( &ajaxUrlHash , &ajaxCrc );
			saveHashTable();
		}
		// send back the urlhash so the checkbox can turn the
		// bg color of the "diff" gray
		SafeBuf sb3;
		sb3.safePrintf("%"UINT32"",ajaxUrlHash);
		g_httpServer.sendDynamicPage(sock,
					     sb3.getBufStart(),
					     sb3.length(),
					     -1/*cachetime*/);
		return true;
	}
		

	// if they hit the submit button, begin the tests
	int32_t submit = hr->hasField("action");

	int32_t n = sizeof(s_qatests)/sizeof(QATest);


	if ( submit && g_qaInProgress ) {
		g_errno = EINPROGRESS;
		g_httpServer.sendErrorReply(sock,g_errno,mstrerror(g_errno));
		return true;
	}

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( sock , hr );
	bool isCollAdmin = g_conf.isCollAdmin ( sock , hr );
	if ( ! isMasterAdmin &&
	     ! isCollAdmin ) {
		g_errno = ENOPERM;
		g_httpServer.sendErrorReply(sock,g_errno,mstrerror(g_errno));
		return true;
	}


	// set m_doTest
	for ( int32_t i = 0 ; submit && i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		char tmp[10];
		sprintf(tmp,"test%"INT32"",i);
		qt->m_doTest = hr->getLong(tmp,0);
	}

	if ( submit ) {
		// reset all the static thingies
		resetFlags();
		// save socket
		g_qaSock = sock;
		g_numErrors = 0;
		g_qaOutput.reset();
		g_qaOutput.safePrintf("<html><body>"
				      "<title>QA Test Results</title>\n");

		g_qaOutput.safePrintf("<SCRIPT LANGUAGE=\"javascript\">\n"
				      // update s_ht with the new crc for this test
				      "function submitchanges(urlhash,crc) "
				      "{\n "
				      "var client=new XMLHttpRequest();\n"
				      "client.onreadystatechange=gotsubmitreplyhandler;"
				      "var "
				      "u='/admin/qa?ajax=1&uh='+urlhash+'&crc='+crc;\n"
				      "client.open('GET',u);\n"
				      "client.send();\n"
				      
				      // use that to fix background to gray
				      "var w=document.getElementById(urlhash);\n"
				      // set background color
				      "w.style.backgroundColor = '0xe0e0e0';\n"

				      // gear spinning after checkbox
				      "}\n\n "

				      // call this when we got the reply that the 
				      // checkbox went through
				      "function gotsubmitreplyhandler() {\n"
				      // return if reply is not fully ready
				      "if(this.readyState != 4 )return;\n"
				      // if error or empty reply then do nothing
				      "if(!this.responseText)return;\n"
				      // response text is the urlhash32, uint32_t
				      "var id=this.responseText;\n"
				      // use that to fix background to gray
				      "var w=document.getElementById(id);\n"
				      // set background color
				      "w.style.backgroundColor = '0xe0e0e0';\n"
				      "}\n\n"

				      "</SCRIPT> ");
		// and run the qa test loop
		if ( ! qatest( ) ) return false;
		// what happened?
		log("qa: qatest completed without blocking");
	}

	// show tests, all checked by default, to perform

	g_pages.printAdminTop ( &sb , sock , hr );

	sb.safePrintf("<SCRIPT LANGUAGE=\"javascript\">\n"
		     "function checkAll(name, num)\n "
		      "{ "
		      "    for (var i = 0; i < num; i++) {\n"
		      "      var e = document.getElementById(name + i);\n"
		      //"alert(name+i);"
		      "      e.checked = !e.checked ;\n "
		      "  }\n"
		      "}\n\n "

		      "</SCRIPT> ");

	//sb.safePrintf("<form name=\"fo\">");

	sb.safePrintf("\n<table %s>\n",TABLE_STYLE);
	sb.safePrintf("<tr class=hdrow><td colspan=2>"
		      "<center><b>QA Tests "
		      "(ensure spidering enabled in master controls before "
		      "running these)</b></center>"
		      "</td></tr>");

	// header row
	sb.safePrintf("<tr><td><b>Do Test?</b> <a style=cursor:hand;"
		      "cursor:pointer; "
		      "onclick=\"checkAll('test', %"INT32");\">(toggle)</a>",n);
	sb.safePrintf("</td><td><b>Test Name</b></td></tr>\n");
	
	// . we keep the ptr to each test in an array
	// . print out each qa function
	for ( int32_t i = 0 ; i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		char *bg;
		if ( i % 2 == 0 ) bg = LIGHT_BLUE;
		else              bg = DARK_BLUE;
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td><input type=checkbox value=1 name=test%"INT32" "
			      "id=test%"INT32"></td>"
			      "<td>%s"
			      "<br>"
			      "<font color=gray size=-1>%s</font>"
			      "</td>"
			      "</tr>\n"
			      , bg
			      , i
			      , i
			      , qt->m_testName
			      , qt->m_testDesc
			      );
	}

	sb.safePrintf("</table>\n<br>\n");
	//	      "</form>\n");

	g_pages.printAdminBottom ( &sb , hr );


	g_httpServer.sendDynamicPage(sock,
				     sb.getBufStart(),
				     sb.length(),
				     -1/*cachetime*/);

	return true;
}
