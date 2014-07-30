#include <string.h>
#include "SafeBuf.h"
#include "HttpServer.h"

TcpSocket *g_qaSock = NULL;
SafeBuf g_qaOutput;
bool g_qaInProgress = false;
long g_numErrors;

static long s_checkCRC = 0;

static bool s_registered = false;

bool qatest ( ) ;

void qatestWrapper ( int fd , void *state ) {
	qatest();
}

// wait X seconds, call sleep timer... then call qatest()
void wait( float seconds ) {
	// put into milliseconds
	long delay = seconds * 1000;

	if ( g_loop.registerSleepCallback ( delay         ,
					    NULL , // state
					    qatestWrapper,//m_masterLoop
					    0    )) {// niceness
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

void markOut ( char *content , char *needle ) {

	if ( ! content ) return;

	char *s = strstr ( content , needle );
	if ( ! s ) return;

	for ( ; *s && ! is_digit(*s); s++ );

	// find end of digit stream
	//char *end = s;
	//while ( ; *end && is_digit(*s); end++ );
	// just bury the digit stream now, zeroing out was not
	// a consistent LENGTH if we had 10 hits vs 9... making the hash 
	// different

	// space out digits
	for ( ; *s && is_digit(*s); s++ ) *s = ' ';
}

// do not hash 
long qa_hash32 ( char *s ) {
	unsigned long h = 0;
	long k = 0;
	for ( long i = 0 ; s[i] ; i++ ) {
		// skip if not first space and back to back spaces
		if ( s[i] == ' ' &&i>0 && s[i-1]==' ') continue;
		h ^= g_hashtab [(unsigned char)k] [(unsigned char)s[i]];
		k++;
	}
	return h;
}

class QATest {
public:
	bool (* m_func)();
	char *m_testName;
	char *m_testDesc;
	char  m_doTest;
	// we set s_flags to this
	long  m_flags[30];
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

void processReply ( char *reply , long replyLen ) {

	// store our current reply
	SafeBuf fb2;
	fb2.safeMemcpy(reply,replyLen );
	fb2.nullTerm();

	// log that we got the reply
	log("qa: got reply(len=%li)(errno=%s)=%s",
	    replyLen,mstrerror(g_errno),reply);

	char *content = NULL;
	long  contentLen = 0;

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

	// take out <responseTimeMS>
	markOut ( content , "<currentTimeUTC>");
	markOut ( content , "<responseTimeMS>");

	// until i figure this one out, take it out
	markOut ( content , "<docsInCollection>");

	// until i figure this one out, take it out
	markOut ( content , "<hits>");

	// for json
	markOut ( content , "\"currentTimeUTC\":" );
	markOut ( content , "\"responseTimeMS\":");

	// make checksum. we ignore back to back spaces so this
	// hash works for <docsInCollection>10 vs <docsInCollection>9
	long contentCRC = 0; 
	if ( content ) contentCRC = qa_hash32 ( content );

	// note it
	log("qa: got contentCRC of %lu",contentCRC);


	// if what we expected, save to disk if not there yet, then
	// call s_callback() to resume the qa pipeline
	/*
	if ( contentCRC == s_expectedCRC ) {
		// save content if good
		char fn3[1024];
		sprintf(fn3,"%sqa/content.%lu",g_hostdb.m_dir,contentCRC);
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

	//
	// if crc of content does not match what was expected then do a diff
	// so we can see why not
	//

	// this means caller does not care about the response
	if ( ! s_checkCRC ) {
		//s_callback();
		return;
	}

	//const char *emsg = "qa: bad contentCRC of %li should be %li "
	//	"\n";//"phase=%li\n";
	//fprintf(stderr,emsg,contentCRC,s_expectedCRC);//,s_phase-1);

	// hash url
	long urlHash32 = hash32n ( s_url.getUrl() );

	// combine test function too since two tests may use the same url
	long nameHash = hash32n ( s_qt->m_testName );

	// combine together
	urlHash32 = hash32h ( nameHash , urlHash32 );

	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		s_ht.set(4,4,1024,NULL,0,false,0,"qaht");
		// make symlink
		//char cmd[512];
		//snprintf(cmd,"cd %s/html ;ln -s ../qa ./qa", g_hostdb.m_dir);
		//system(cmd);
		char dir[1024];
		snprintf(dir,1000,"%sqa",g_hostdb.m_dir);
		long status = ::mkdir ( dir ,
					S_IRUSR | S_IWUSR | S_IXUSR | 
					S_IRGRP | S_IWGRP | S_IXGRP | 
					S_IROTH | S_IXOTH );
	        if ( status == -1 && errno != EEXIST && errno )
			log("qa: Failed to make directory %s: %s.",
			    dir,mstrerror(errno));
		// try to load from disk
		SafeBuf fn;
		fn.safePrintf("%s/qa/",g_hostdb.m_dir);
		log("qa: loading crctable.dat");
		s_ht.load ( fn.getBufStart() , "crctable.dat" );
	}

	// break up into lines
	char fn2[1024];
	sprintf(fn2,"%sqa/content.%lu",g_hostdb.m_dir,contentCRC);
	fb2.save ( fn2 );

	// look up in hashtable to see what reply crc should be
	long *val = (long *)s_ht.getValue ( &urlHash32 );

	// just return if the same
	if ( val && contentCRC == *val ) {
		g_qaOutput.safePrintf("passed test<br>%s : "
				      "<a href=%s>%s</a> (urlhash=%lu "
				      "crc=<a href=/qa/content.%lu>"
				      "%lu</a>)<br>"
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
		g_qaOutput.safePrintf("first time testing<br>%s : "
				      "<a href=%s>%s</a> "
				      "(urlhash=%lu "
				      "crc=<a href=/qa/content.%lu>%lu"
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


	log("qa: crc changed for url %s from %li to %li",
	    s_url.getUrl(),*val,contentCRC);

	// get response on file
	SafeBuf fb1;
	char fn1[1024];
	sprintf(fn1,"%sqa/content.%lu",g_hostdb.m_dir, *val);
	fb1.load(fn1);
	fb1.nullTerm();

	// do the diff between the two replies so we can see what changed
	char cmd[1024];
	sprintf(cmd,"diff %s %s > /tmp/diffout",fn1,fn2);
	fprintf(stderr,"%s\n",cmd);
	system(cmd);

	g_numErrors++;
	
	g_qaOutput.safePrintf("FAILED TEST<br>%s : "
			      "<a href=%s>%s</a> (urlhash=%lu)<br>"

			      "<input type=checkbox name=urlhash%lu value=1 "
			      // use ajax to update test crc. if you undo your
			      // check then it should put the old val back.
			      // when you first click the checkbox it should
			      // gray out the diff i guess.
			      "onclick=submitchanges(%lu,%lu);> "
			      "Accept changes"

			      "<br>"
			      "original on left, new on right. "
			      "oldcrc = <a href=/qa/content.%lu>%lu</a>"

			      " != <a href=/qa/content.%lu>%lu</a> = newcrc"
			      "<br>diff output follows:<br>"
			      "<pre id=%lu style=background-color:0xffffff;>",
			      s_qt->m_testName,
			      s_url.getUrl(),
			      s_url.getUrl(),
			      urlHash32,

			      // input checkbox name field
			      urlHash32,

			      // submitchanges() parms
			      urlHash32, 
			      contentCRC,

			      // original/old content.%lu
			      *val,
			      *val,

			      // new content.%lu
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

	s_callback ();
}

// returns false if blocked, true otherwise, like on quick connect error
bool getUrl( char *path , long checkCRC = 0 , char *post = NULL ) {

	SafeBuf sb;
	sb.safePrintf ( "http://%s:%li%s"
			, iptoa(g_hostdb.m_myHost->m_ip)
			, (long)g_hostdb.m_myHost->m_httpPort
			, path
			);

	s_checkCRC = checkCRC;

	bool doPost = true;
	if ( strncmp ( path , "/search" , 7 ) == 0 )
		doPost = false;

	//Url u;
	s_url.set ( sb.getBufStart() );
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

bool loadUrls ( ) {
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
		s_ubuf2.pushLong((long)s);
		// skip past that
		s = e;
		// point to content
		s_cbuf2.pushLong((long)(s+1));
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
static long *s_flags = NULL;

//
// the injection qa test suite
//
bool qainject1 ( ) {

	//if ( ! s_callback ) s_callback = qainject1;

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

	// this only loads once
	loadUrls();
	long max = s_ubuf2.length()/(long)sizeof(char *);
	//max = 1;

	//
	// inject urls, return false if not done yet
	//
	//static bool s_x4 = false;
	if ( ! s_flags[2] ) {
		// TODO: try delimeter based injection too
		//static long s_ii = 0;
		for ( ; s_flags[20] < max ; ) {
			// inject using html api
			SafeBuf sb;
			sb.safePrintf("&c=qatest123&deleteurl=0&"
				      "format=xml&u=");
			sb.urlEncode ( s_urlPtrs[s_flags[20]] );
			// the content
			sb.safePrintf("&hasmime=1");
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
		//usleep(1500000);
		wait(1.5);
		s_flags[3] = true;
		return false;
	}

	if ( ! s_flags[16] ) {
		s_flags[16] = true;
		if ( ! getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
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

	//
	// eject/delete the urls
	//
	//static long s_ii2 = 0;
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
	//static bool s_x9 = false;
	if ( ! s_flags[6] ) {
		//usleep(1500000);
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

bool qainject2 ( ) {

	//if ( ! s_callback ) s_callback = qainject2;

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
		if ( ! getUrl ( "/admin/inject",
				// check reply, seems to have only a single 
				// docid in it
				-1970198487, sb.getBufStart()) )
			return false;
	}

	// now query check
	//static bool s_y4 = false;
	if ( ! s_flags[8] ) {
		//usleep(1500000);
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
	// delete the 'qatest123' collection
	//
	if ( ! s_flags[12] ) {
		s_flags[12] = true;
		if ( ! getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" ) )
			return false;
	}


	//static bool s_fee2 = false;
	if ( ! s_flags[13] ) {
		s_flags[13] = true;
		log("qa: SUCCESSFULLY COMPLETED "
			"QA INJECT TEST 2");
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

	// restrict hopcount to 0 or 1 in url filters so we do not spider
	// too deep
	//static bool s_z1 = false;
	if ( ! s_flags[2] ) {
		s_flags[2] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&"
			      // make it the custom filter
			      "ufp=0&"

	       "fe=%%21ismanualadd+%%26%%26+%%21insitelist&hspl=0&hspl=1&fsf=0.000000&mspr=0&mspi=1&xg=1000&fsp=-3&"

			      // take out hopcount for now, just test quotas
			      //	       "fe1=tag%%3Ashallow+%%26%%26+hopcount%%3C%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=3&"

	       "fe1=tag%%3Ashallow+%%26%%26+sitepages%%3C%%3D20&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=45&"

	       "fe2=default&hspl2=0&hspl2=1&fsf2=1.000000&mspr2=0&mspi2=1&xg2=1000&fsp2=45&"

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
		wait(5.0);
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
		if ( ! getUrl ( "/search?c=qatest123&format=json&"
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
				"q=site2%3Awww.walmart.com+"
				"gbsortby%3Agbspiderdate",
				999 ) )
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

	// restrict hopcount to 0 or 1 in url filters so we do not spider
	// too deep
	//static bool s_z1 = false;
	if ( ! s_flags[2] ) {
		s_flags[2] = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&"
			      // make it the custom filter
			      "ufp=0&"

	       "fe=%%21ismanualadd+%%26%%26+%%21insitelist&hspl=0&hspl=1&fsf=0.000000&mspr=0&mspi=1&xg=1000&fsp=-3&"

			      // take out hopcount for now, just test quotas
			      //	       "fe1=tag%%3Ashallow+%%26%%26+hopcount%%3C%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=3&"

			      // sitepages is a little fuzzy so take it
			      // out for this test and use hopcount!!!
			      //"fe1=tag%%3Ashallow+%%26%%26+sitepages%%3C%%3D20&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=45&"
			      "fe1=tag%%3Ashallow+%%26%%26+hopcount<%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=1&mspi1=1&xg1=1000&fsp1=45&"

	       "fe2=default&hspl2=0&hspl2=1&fsf2=1.000000&mspr2=0&mspi2=1&xg2=1000&fsp2=45&"

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
		wait(5.0);
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
		if ( ! getUrl ( "/search?c=qatest123&format=json&"
				"q=gbfacetstr%3Agbxpathsitehash3311332088",
				999 ) )
			return false;
	}

	//static bool s_y6 = false;
	if ( ! s_flags[9] ) {
		s_flags[9] = true;
		if ( ! getUrl ( "/get?page=4&q=gbfacetstr:gbxpathsitehash3311332088&qlang=xx&c=qatest123&d=9577169402&cnsp=0" , 999 ) )
			return false;
	}

	// in xml
	//static bool s_y7 = false;
	if ( ! s_flags[10] ) {
		s_flags[10] = true;
		if ( ! getUrl ( "/get?xml=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=9577169402&cnsp=0" , 999 ) )
			return false;
	}

	// and json
	//static bool s_y8 = false;
	if ( ! s_flags[11] ) {
		s_flags[11] = true;
		if ( ! getUrl ( "/get?json=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=qatest123&d=9577169402&cnsp=0" , 999 ) )
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


	// scrape it
	if ( ! s_flags[3] ) {
		s_flags[3] = true;
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&"
			       "format=xml&qts=test");
		if ( ! getUrl ( sb.getBufStart() , 0 ) )
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
	 "Test deletion of urls via inject api."},

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
	 "Scrape and inject results from google and bing."}

};

void resetFlags() {
	long n = sizeof(s_qatests)/sizeof(QATest);
	for ( long i = 0 ; i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		memset(qt->m_flags,0,4*30);
	}
}

// . run a series of tests to ensure that gb is functioning properly
// . uses the ./qa subdirectory to hold archive pages, ips, spider dates to
//   ensure consistency between tests for exact replays
bool qatest ( ) {

	if ( s_registered ) {
		g_loop.unregisterSleepCallback(NULL,qatestWrapper);
		s_registered = false;
	}

	if ( ! s_callback ) s_callback = qatest;

	if ( ! g_qaSock ) return true;


	// returns true when done, false when blocked
	//if ( ! qainject ( ) ) return false;

	// returns true when done, false when blocked
	//if ( ! qaspider ( ) ) return false;

	long n = sizeof(s_qatests)/sizeof(QATest);
	for ( long i = 0 ; i < n ; i++ ) {
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
	long ajax = hr->getLong("ajax",0);
	unsigned long ajaxUrlHash ;
	ajaxUrlHash = (unsigned long long)hr->getLongLong("uh",0LL);
	unsigned long ajaxCrc ;
	ajaxCrc = (unsigned long long)hr->getLongLong("crc",0LL);

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
		sb3.safePrintf("%lu",ajaxUrlHash);
		g_httpServer.sendDynamicPage(sock,
					     sb3.getBufStart(),
					     sb3.length(),
					     -1/*cachetime*/);
		return true;
	}
		

	// if they hit the submit button, begin the tests
	long submit = hr->hasField("action");

	long n = sizeof(s_qatests)/sizeof(QATest);


	if ( submit && g_qaInProgress ) {
		g_errno = EINPROGRESS;
		g_httpServer.sendErrorReply(sock,g_errno,mstrerror(g_errno));
		return true;
	}

	// set m_doTest
	for ( long i = 0 ; submit && i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		char tmp[10];
		sprintf(tmp,"test%li",i);
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
				      // response text is the urlhash32, unsigned long
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
		      "<center><b>QA Tests</b></center>"
		      "</td></tr>");

	// header row
	sb.safePrintf("<tr><td><b>Do Test?</b> <a style=cursor:hand;cursor:pointer; "
		      "onclick=\"checkAll('test', %li);\">(toggle)</a>",n);
	sb.safePrintf("</td><td><b>Test Name</b></td></tr>\n");
	
	// . we keep the ptr to each test in an array
	// . print out each qa function
	for ( long i = 0 ; i < n ; i++ ) {
		QATest *qt = &s_qatests[i];
		char *bg;
		if ( i % 2 == 0 ) bg = LIGHT_BLUE;
		else              bg = DARK_BLUE;
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td><input type=checkbox value=1 name=test%li "
			      "id=test%li checked></td>"
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


