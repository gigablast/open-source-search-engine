#include <string.h>
#include "SafeBuf.h"
#include "HttpServer.h"

static long s_expectedCRC = 0;

// after we got the reply and verified expected crc, call the callback
static bool (*s_callback)() = NULL;

// first inject a set list of urls
static char  **s_urlPtrs = NULL;
static char  **s_contentPtrs = NULL;
static SafeBuf s_ubuf1;
static SafeBuf s_ubuf2;
static SafeBuf s_cbuf2;

void markOut ( char *reply , char *needle ) {

	if ( ! reply ) return;

	char *s = strstr ( reply , needle );
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


// come here after receiving ANY reply from the gigablast server
static void gotReplyWrapper ( void *state , TcpSocket *sock ) {

	// store our current reply
	SafeBuf fb2;
	fb2.safeMemcpy(sock->m_readBuf,sock->m_readOffset);
	fb2.nullTerm();

	// log that we got the reply
	log("qa: got reply(%li)=%s",sock->m_readOffset,sock->m_readBuf);

	// get mime
	HttpMime mime;
	mime.set ( sock->m_readBuf , sock->m_readOffset , NULL );
	// only hash content since mime has a timestamp in it
	char *content = mime.getContent();
	long  contentLen = mime.getContentLen();
	if ( content[contentLen] ) { char *xx=NULL;*xx=0; }

	char *reply = sock->m_readBuf;

	// take out <responseTimeMS>
	markOut ( reply , "<currentTimeUTC>");

	markOut ( reply , "<responseTimeMS>");

	// until i figure this one out, take it out
	markOut ( reply , "<docsInCollection>");

	// until i figure this one out, take it out
	markOut ( reply , "<hits>");

	// make checksum. we ignore back to back spaces so this
	// hash works for <docsInCollection>10 vs <docsInCollection>9
	long replyCRC = qa_hash32 ( content );

	// if what we expected, save to disk if not there yet, then
	// call s_callback() to resume the qa pipeline
	if ( replyCRC == s_expectedCRC ) {
		// save reply if good
		char fn3[1024];
		sprintf(fn3,"%sqa/reply.%li",g_hostdb.m_dir,replyCRC);
		File ff; ff.set ( fn3 );
		if ( ! ff.doesExist() ) {
			// if not there yet then save it
			fb2.save(fn3);
		}
		// . continue on with the qa process
		// . which qa function that may be
		s_callback();
		return;
	}


	//
	// if crc of reply does not match what was expected then do a diff
	// so we can see why not
	//

	const char *emsg = "qa: bad replyCRC of %li should be %li "
		"\n";//"phase=%li\n";
	fprintf(stderr,emsg,replyCRC,s_expectedCRC);//,s_phase-1);
	// get response on file
	SafeBuf fb1;
	char fn1[1024];
	sprintf(fn1,"%sqa/reply.%li",g_hostdb.m_dir,s_expectedCRC);
	fb1.load(fn1);
	fb1.nullTerm();
	// break up into lines
	char fn2[1024];
	sprintf(fn2,"/tmp/reply.%li",replyCRC);
	fb2.save ( fn2 );

	// do the diff between the two replies so we can see what changed
	char cmd[1024];
	sprintf(cmd,"diff %s %s",fn1,fn2);
	fprintf(stderr,"%s\n",cmd);
	system(cmd);
	// if this is zero allow it to slide by. it is learning mode i guess.
	// so we can learn what crc we need to use.
	// otherwise, stop right there for debugging
	if ( s_expectedCRC != 0 ) exit(1);

	// keep on going
	s_callback();
}

bool getUrl( char *path , long expectedCRC = 0 , char *post = NULL ) {

	SafeBuf sb;
	sb.safePrintf ( "http://%s:%li%s"
			, iptoa(g_hostdb.m_myHost->m_ip)
			, (long)g_hostdb.m_myHost->m_httpPort
			, path
			);

	s_expectedCRC = expectedCRC;

	Url u;
	u.set ( sb.getBufStart() );
	log("qa: getting %s",sb.getBufStart());
	if ( ! g_httpServer.getDoc ( u.getUrl() ,
				     0 , // ip
				     0 , // offset
				     -1 , // size
				     0 , // ifmodsince
				     NULL ,
				     gotReplyWrapper,
				     60*1000, // timeout
				     0, // proxyip
				     0, // proxyport
				     -1, // maxtextdoclen
				     -1, // maxotherdoclen
				     NULL , // useragent
				     "HTTP/1.0" , // protocol
				     true , // doPost
				     NULL , // cookie
				     NULL , // additionalHeader
				     NULL , // fullRequest
				     post ) )
		return false;
	// error?
	log("qa: getUrl error: %s",mstrerror(g_errno));
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

#undef usleep

//
// the injection qa test suite
//
bool qainject ( ) {

	if ( ! s_callback ) s_callback = qainject;

	//
	// delete the 'qatest123' collection
	//
	static bool s_x1 = false;
	if ( ! s_x1 ) {
		s_x1 = true;
		getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" );
		return false;
	}

	//
	// add the 'qatest123' collection
	//
	static bool s_x2 = false;
	if ( ! s_x2 ) {
		s_x2 = true;
		getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
			 // checksum of reply expected
			 238170006 );
		return false;
	}

	//
	// inject urls, return false if not done yet
	//
	static bool s_x4 = false;
	if ( ! s_x4 ) {
		// TODO: try delimeter based injection too
		loadUrls();
		static long s_ii = 0;
		for ( ; s_ii < s_ubuf2.length()/(long)sizeof(char *) ; ) {
			// inject using html api
			SafeBuf sb;
			sb.safePrintf("&c=qatest123&deleteurl=0&"
				      "format=xml&u=");
			sb.urlEncode ( s_urlPtrs[s_ii] );
			// the content
			sb.safePrintf("&hasmime=1");
			sb.safePrintf("&content=");
			sb.urlEncode(s_contentPtrs[s_ii] );
			sb.nullTerm();
			// pre-inc it in case getUrl() blocks
			s_ii++;
			getUrl("/admin/inject",
			       0, // no idea what crc to expect
			       sb.getBufStart());
			return false;
		}
		s_x4 = true;
	}

	// +the
	static bool s_x5 = false;
	if ( ! s_x5 ) {
		usleep(1500000);
		s_x5 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
			 -1452050577 );
		return false;
	}

	// sports news
	static bool s_x7 = false;
	if ( ! s_x7 ) {
		s_x7 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=sports+news",
			 -1586622518 );
		return false;
	}

	//
	// eject/delete the urls
	//
	static long s_ii2 = 0;
	for ( ; s_ii2 < s_ubuf2.length()/(long)sizeof(char *) ; ) {
		// reject using html api
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&deleteurl=1&"
			       "format=xml&u=");
		sb.urlEncode ( s_urlPtrs[s_ii2] );
		sb.nullTerm();
		// pre-inc it in case getUrl() blocks
		s_ii2++;
		getUrl ( sb.getBufStart() , 0 );
		return false;
	}

	//
	// make sure no results left, +the
	//
	static bool s_x9 = false;
	if ( ! s_x9 ) {
		usleep(1500000);
		s_x9 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
			 -1672870556 );
		return false;
	}

	//
	// try delimeter based injecting
	//
	static bool s_y2 = false;
	if ( ! s_y2 ) {
		s_y2 = true;
		SafeBuf sb;
		// delim=+++URL:
		sb.safePrintf("&c=qatest123&deleteurl=0&"
			      "delim=%%2B%%2B%%2BURL%%3A&format=xml&u=xyz.com&"
			      "hasmime=1&content=");
		// use injectme3 file
		SafeBuf ubuf;
		ubuf.load("./injectme3");
		sb.urlEncode(ubuf.getBufStart());
		getUrl ( "/admin/inject",
			 // check reply, seems to have only a single docid init
			 -1970198487,
			 sb.getBufStart());
		return false;
	}

	// now query check
	static bool s_y4 = false;
	if ( ! s_y4 ) {
		usleep(1500000);
		s_y4 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
			 -480078278 );
		return false;
	}

	//
	// delete the 'qatest123' collection
	//
	if ( ! s_x1 ) {
		s_x1 = true;
		getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" );
		return false;
	}


	static bool s_fee2 = false;
	if ( ! s_fee2 ) {
		s_fee2 = true;
		fprintf(stderr,"\n\n\nSUCCESSFULLY COMPLETED "
			"QA INJECT TEST\n\n\n");
		return true;
	}


	return true;
}


static char *s_urls1 =
	"+walmart.com"
	"+cisco.com"
	"+t7online.com"
	"+sonyericsson.com"
	"+netsh.com"
	"+allegro.pl"
	"+hotscripts.com"
	"+sitepoint.com"
	"+so-net.net.tw"
	"+aol.co.uk"
	"+sbs.co.kr"
	"+chinaacc.com"
	"+eyou.com"
	"+spray.se"
	"+carview.co.jp"
	"+xcar.com.cn"
	"+united.com"
	"+raaga.com"
	"+primaryads.com"
	"+szonline.net"
	"+icbc.com.cn"
	"+instantbuzz.com"
	"+sz.net.cn"
	"+6to23.com"
	"+seesaa.net"
	"+tracking101.com"
	"+jubii.dk"
	"+5566.net"
	"+prikpagina.nl"
	"+7xi.net"
	"+91.com"
	"+jjwxc.com"
	"+adbrite.com"
	"+hoplay.com"
	"+questionmarket.com"
	"+telegraph.co.uk"
	"+trendmicro.com"
	"+google.fi"
	"+ebay.es"
	"+tfol.com"
	"+sleazydream.com"
	"+websearch.com"
	"+freett.com"
	"+dayoo.com"
	"+interia.pl"
	"+yymp3.com"
	"+stanford.edu"
	"+time.gr.jp"
	"+telia.com"
	"+madthumbs.com"
	"+chinamp3.com"
	"+oldgames.se"
	"+buy.com"
	"+singpao.com"
	"+cbsnews.com"
	"+corriere.it"
	"+cbs.com"
	"+flickr.com"
	"+theglobeandmail.com"
	"+incredifind.com"
	"+mit.edu"
	"+chase.com"
	"+ktv666.com"
	"+oldnavy.com"
	"+lego.com"
	"+eniro.se"
	"+bloomberg.com"
	"+ft.com"
	"+odn.ne.jp"
	"+pcpop.com"
	"+ugameasia.com"
	"+cantv.net"
	"+allinternal.com"
	"+aventertainments.com"
	"+invisionfree.com"
	"+hangzhou.com.cn"
	"+zhaopin.com"
	"+bcentral.com"
	"+lowes.com"
	"+adprofile.net"
	"+yninfo.com"
	"+jeeran.com"
	"+twbbs.net.tw"
	"+yousendit.com"
	"+aavalue.com"
	"+google.com.co"
	"+mysearch.com"
	"+worldsex.com"
	"+navisearch.net"
	"+lele.com"
	"+msn.co.in"
	"+officedepot.com"
	"+xintv.com"
	"+204.177.92.193"
	"+travelzoo.com"
	"+bol.com.br"
	"+dtiserv2.com"
	"+optonline.net"
	"+hitslink.com"
	"+freechal.com"
	"+infojobs.net"
	;

bool qaspider ( ) {

	if ( ! s_callback ) s_callback = qaspider;

	//
	// delete the 'qatest123' collection
	//
	static bool s_x1 = false;
	if ( ! s_x1 ) {
		s_x1 = true;
		getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" );
		return false;
	}

	//
	// add the 'qatest123' collection
	//
	static bool s_x2 = false;
	if ( ! s_x2 ) {
		s_x2 = true;
		getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
			 // checksum of reply expected
			 238170006 );
		return false;
	}


	//
	// use the add url interface now
	//
	static bool s_y2 = false;
	if ( ! s_y2 ) {
		s_y2 = true;
		SafeBuf sb;
		// delim=+++URL:
		sb.safePrintf("&c=qatest123"
			      "&format=json"
			      "&strip=1"
			      "&spiderlinks=1"
			      "&urls="
			      );
		// . now a list of websites we want to spider
		// . the space is already encoded as +
		sb.safeStrcpy(s_urls1);
		getUrl ( "/admin/addurl",0,sb.getBufStart());
		return false;
	}
	
	// delete the collection
	static bool s_fee = false;
	if ( ! s_fee ) {
		s_fee = true;
		return getUrl ( "/admin/delcoll?delcoll=qatest123" );
	}

	static bool s_fee2 = false;
	if ( ! s_fee2 ) {
		s_fee2 = true;
		fprintf(stderr,"\n\n\nSUCCESSFULLY COMPLETED "
			"QA SPIDER TEST\n\n\n");
		return true;
	}

	return true;
}

// test ability for gb to act as a squid proxy that returns
// html with _s= tag attributes inserted
bool qasquid ( ) {

	if ( ! s_callback ) s_callback = qaspider;

	//
	// delete the 'qatest123' collection
	//
	static bool s_x1 = false;
	if ( ! s_x1 ) {
		s_x1 = true;
		getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" );
		return false;
	}

	//
	// add the 'qatest123' collection
	//
	static bool s_x2 = false;
	if ( ! s_x2 ) {
		s_x2 = true;
		getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
			 // checksum of reply expected
			 238170006 );
		return false;
	}

	// 
	// restrict hopcount to 0 or 1 in url filters so we do not spider
	// too deep
	static bool s_z1 = false;
	if ( ! s_z1 ) {
		s_z1 = true;
		SafeBuf sb;
		sb.safePrintf("&c=qatest123&ufp=0&fe=%%21ismanualadd+%%26%%26+%%21insitelist&hspl=0&hspl=1&fsf=0.000000&mspr=99&mspi=1&xg=1000&fsp=-3&fe1=hopcount%%3C%%3D1&hspl1=0&hspl1=1&fsf1=1.000000&mspr1=5&mspi1=1&xg1=1000&fsp1=3&fe2=default&hspl2=0&hspl2=1&fsf2=1.000000&mspr2=0&mspi2=1&xg2=1000&fsp2=45&fe3=&hspl3=0&fsf3=0.000000&mspr3=0&mspi3=0&xg3=0&fsp3=0&action=submit");
		getUrl ( "/admin/filters",0,sb.getBufStart());
		return false;
	}


	//
	// spider some websites using addurl to hopcount 0 and 1.
	//
	static bool s_y2 = false;
	if ( ! s_y2 ) {
		s_y2 = true;
		SafeBuf sb;
		// delim=+++URL:
		sb.safePrintf("&c=qatest123"
			      "&format=json"
			      "&strip=1"
			      "&spiderlinks=1"
			      "&urls="
			      );
		// . now a list of websites we want to spider
		// . the space is already encoded as +
		sb.safeStrcpy(s_urls1);
		getUrl ( "/admin/addurl",0,sb.getBufStart());
		return false;
	}

	// verify no 2 hopcounts in results
	static bool s_y4 = false;
	if ( ! s_y4 ) {
		usleep(1500000);
		s_y4 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&"
			 "q=gbhopcount%%3A2",
			 123456 );
		return false;
	}
	
	// check facet sections query for walmart
	static bool s_y5 = false;
	if ( ! s_y5 ) {
		s_y5 = true;
		getUrl ( "/search?c=qatest123&format=json&"
			 "q=gbfacetstr%%3Agbxpathsitehash2492664135",
			 123456 );
		return false;
	}

	static bool s_y6 = false;
	if ( ! s_y6 ) {
		s_y6 = true;
		getUrl ( "/get?page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=main&d=61506292&cnsp=0" , 123456 );
		return false;
	}

	// in xml
	static bool s_y7 = false;
	if ( ! s_y7 ) {
		s_y7 = true;
		getUrl ( "/get?xml=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=main&d=61506292&cnsp=0" , 123456 );
		return false;
	}

	// and json
	static bool s_y8 = false;
	if ( ! s_y8 ) {
		s_y8 = true;
		getUrl ( "/get?json=1&page=4&q=gbfacetstr:gbxpathsitehash2492664135&qlang=xx&c=main&d=61506292&cnsp=0" , 123456 );
		return false;
	}


	static bool s_fee2 = false;
	if ( ! s_fee2 ) {
		s_fee2 = true;
		fprintf(stderr,"\n\n\nSUCCESSFULLY COMPLETED "
			"QA SQUID TEST\n\n\n");
		return true;
	}
		
	return true;
}

// . run a series of tests to ensure that gb is functioning properly
// . uses the ./qa subdirectory to hold archive pages, ips, spider dates to
//   ensure consistency between tests for exact replays
bool qatest ( ) {

	if ( ! s_callback ) s_callback = qatest;

	qainject ( );

	qaspider ( );

	qasquid  ( );

	return true;
}

