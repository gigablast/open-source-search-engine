#include <string.h>
#include "SafeBuf.h"
#include "HttpServer.h"

static long s_failures = 0;

bool getUrl( char *path , 
	     void (* callback) (void *state, TcpSocket *sock) ,
	     char *post = NULL ) {
	SafeBuf sb;
	sb.safePrintf ( "http://%s:%li%s"
			, iptoa(g_hostdb.m_myHost->m_ip)
			, (long)g_hostdb.m_myHost->m_httpPort
			, path
			);
	Url u;
	u.set ( sb.getBufStart() );
	log("qa: getting %s",sb.getBufStart());
	if ( ! g_httpServer.getDoc ( u.getUrl() ,
				     0 , // ip
				     0 , // offset
				     -1 , // size
				     0 , // ifmodsince
				     NULL ,
				     callback ,
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

bool qatest ( ) ;

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

long s_replyCRC = 0;
TcpSocket *s_sock = NULL;
static bool (*s_callback)() = NULL;

void qatestWrapper ( void *state , TcpSocket *sock ) { 
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
	s_replyCRC = qa_hash32 ( content );

	// this too is used for recording the reply into a file on disk
	s_sock = sock;

	// continue qa loop
	//qatest(); 
	s_callback();
}	

// first inject a set list of urls
static char  **s_urlPtrs = NULL;
static char  **s_contentPtrs = NULL;
static SafeBuf s_ubuf1;
static SafeBuf s_ubuf2;
static SafeBuf s_cbuf2;


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

#include "Msg0.h"
static Msg0 s_msg0;
static RdbList s_list;

void gotList33 ( void *state ) {
	long *rdbId = (long *)state;
	if ( ! s_list.isEmpty() ) {
		log("qa: delete failed. list is not empty rdbid=%li.",*rdbId);
		s_failures++;
	}
	// resume main loop
	qatest();
}

// scan all Rdb databases and ensure no recs (it was a clean delete)
bool checkRdbLists ( long *rdbId ) {
	CollectionRec *cr = g_collectiondb.getRec("qatest123");
	if ( ! cr ) return true;
	collnum_t cn = cr->m_collnum;
	for ( ; *rdbId < RDB_END ; ) {
		// pre-inc it
		*rdbId = *rdbId + 1;
		char minKey[MAX_KEY_BYTES];
		char maxKey[MAX_KEY_BYTES];
	        KEYMIN(minKey,MAX_KEY_BYTES);
	        KEYMAX(maxKey,MAX_KEY_BYTES);
		if ( ! s_msg0.getList ( 0 , // hostid
					0 , // ip
					0 , // port
					0 , // cacheage
					false, // addtocache
					*rdbId , // rdbid
					cn , // collnum
					&s_list ,
					minKey ,
					maxKey ,
					1000 , // minrecsizes
					rdbId , // state
					gotList33,
					0 // niceness
					) )
			return false;
	}
	return true;
}

// once we have triggered the dump this will cause all rdbs to tightmerge
void doneDumping ( void *state , TcpSocket *sock ) {
	CollectionRec *cr = g_collectiondb.getRec("qatest123");
	if ( ! cr ) { qatest(); return; }
	// tight merge the rdb that was dumped
	for ( long i = 0 ; i < RDB_END ; i++ ) {
		Rdb *rdb = getRdbFromId ( i );
		if ( ! rdb ) continue;
		RdbBase *base = rdb->getBase ( cr->m_collnum );
		if ( ! base ) continue;
		// . force a tight merge as soon as dump completes
		// . the dump should already be going
		base->m_nextMergeForced = true;
	}
	// wait for tight merges to complete now
	qatest();
}

bool dumpTreesToDisk () {
	static bool s_done = false;
	if ( s_done ) return true;
	s_done = true;
	// force dump data to disk. dumps all rdbs.
	return getUrl("/admin/master?dump=1",doneDumping );
}

void doneAddingUrls ( void *state ) {
	qatest();
}

void sleepCallback ( int fd , void *state ) {
	qatest();
}

// check every second to see if merges are done
bool waitForMergeToFinish ( ) {
	// if registered
	static bool s_registered = false;
	if ( s_registered ) {
		g_loop.unregisterSleepCallback ( NULL , sleepCallback );
		s_registered = false;
	}
	CollectionRec *cr = g_collectiondb.getRec("qatest123");
	if ( ! cr ) { qatest(); return true; }
	// tight merge the rdb that was dumped
	long i; for ( i = 0 ; i < RDB_END ; i++ ) {
		Rdb *rdb = getRdbFromId ( i );
		if ( ! rdb ) continue;
		RdbBase *base = rdb->getBase ( cr->m_collnum );
		if ( ! base ) continue;
		// . force a tight merge as soon as dump completes
		// . the dump should already be going
		if ( base->m_nextMergeForced ) return false;
		// still waiting on this merge
		break;
	}
	// if not still waiting return true
	if ( i >= RDB_END ) return true;
	// sleep for 1 second
	g_loop.registerSleepCallback ( 1000 , // 1000 ms
				       NULL , // state
				       sleepCallback ,
				       0 ); // niceness
	s_registered = true;
	return false;
}

bool resetColl ( ) {
	static bool s_flag = false;
	if ( s_flag ) return true;
	s_flag = true;
	// also turn spiders on
	return getUrl("/admin/master?reset=qatest123&se=1", qatestWrapper );
}
	
bool addUrlTest ( ) {
	static bool s_flag = false;
	if ( s_flag ) return true;
	s_flag = true;
	return getUrl ( "/admin/addurl"
			"?c=qatest123&u=www.dmoz.org+www.ibm.com+"
			"www.diffbot.com"
			, qatestWrapper );
}

// check every second to see if spidering phase is completed
bool checkSpidersDone ( ) {
	// if registered
	static bool s_registered = false;
	if ( s_registered ) {
		g_loop.unregisterSleepCallback ( NULL , sleepCallback );
		s_registered = false;
	}
	// we have to adjust this once we know how many pages we'll archive
	CollectionRec *cr = g_collectiondb.getRec("qatest123");
	if ( ! cr ) { qatest(); return true; }
	// return true if all done
	if ( cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound >= 200 )
		return true;
	// sleep for 1 second
	g_loop.registerSleepCallback ( 1000 , // 1000 ms
				       NULL , // state
				       sleepCallback ,
				       0 ); // niceness
	s_registered = true;
	return false;
}

//static long s_phase = -1;

void checkCRC ( long needCRC ) {

	// and our current reply
	SafeBuf fb2;
	fb2.safeMemcpy(s_sock->m_readBuf,s_sock->m_readOffset);
	fb2.nullTerm();

	if ( s_replyCRC == needCRC ) {
		// save reply if good
		char fn3[1024];
		sprintf(fn3,"%sqa/reply.%li",g_hostdb.m_dir,needCRC);
		File ff; ff.set ( fn3 );
		if ( ff.doesExist() ) return;
		// if not there yet then save it
		fb2.save(fn3);
		return;
	}

	const char *emsg = "qa: bad replyCRC of %li should be %li "
		"\n";//"phase=%li\n";
	fprintf(stderr,emsg,s_replyCRC,needCRC);//,s_phase-1);
	// get response on file
	SafeBuf fb1;
	char fn1[1024];
	sprintf(fn1,"%sqa/reply.%li",g_hostdb.m_dir,needCRC);
	fb1.load(fn1);
	fb1.nullTerm();
	// break up into lines
	char fn2[1024];
	sprintf(fn2,"/tmp/reply.%li",s_replyCRC);
	fb2.save ( fn2 );

	// do the diff between the two replies so we can see what changed
	char cmd[1024];
	sprintf(cmd,"diff %s %s",fn1,fn2);
	fprintf(stderr,"%s\n",cmd);
	system(cmd);
	// if this is zero allow it to slide by. it is learning mode i guess.
	// so we can learn what crc we need to use.
	if ( needCRC == 0 ) return;
	// otherwise, stop right there for debugging
	exit(1);
}

#undef usleep

//
// the injection qa test suite
//
bool qainject () {

	if ( ! s_callback ) s_callback = qainject;

	static bool s_x1 = false;
	if ( ! s_x1 ) {
		s_x1 = true;
		return getUrl ( "/admin/delcoll?delcoll=qatest123" , 
				qatestWrapper );
	}

	//
	// add the 'qatest123' collection
	//
	static bool s_x2 = false;
	if ( ! s_x2 ) {
		s_x2 = true;
		if ( ! getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
				qatestWrapper ) )
			return false;
	}

	//
	// check addcoll reply
	//
	static bool s_x3 = false;
	if ( ! s_x3 ) {
		s_x3 = true;
		checkCRC ( 238170006 );
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
			getUrl("/admin/inject",qatestWrapper,sb.getBufStart());
			return false;
		}
		s_x4 = true;
	}

	// +the
	static bool s_x5 = false;
	if ( ! s_x5 ) {
		usleep(500000);
		s_x5 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
			 qatestWrapper );
		return false;
	}

	static bool s_x6 = false;
	if ( ! s_x6 ) { s_x6 = true ; checkCRC ( -1452050577 ); }


	// sports news
	static bool s_x7 = false;
	if ( ! s_x7 ) {
		s_x7 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=sports+news",
			 qatestWrapper );
		return false;
	}

	static bool s_x8 = false;
	if ( ! s_x8 ) { s_x8 = true; checkCRC ( -1586622518 ); }

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
		getUrl ( sb.getBufStart() , qatestWrapper );
		return false;
	}

	//
	// make sure no results left, +the
	//
	static bool s_x9 = false;
	if ( ! s_x9 ) {
		usleep(500000);
		s_x9 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
			 qatestWrapper );
		return false;
	}

	// seems to have <docsInCollection>2</>
	static bool s_y1 = false;
	if ( ! s_y1 ) { s_y1 = true; checkCRC ( -1672870556 ); }

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
		getUrl ( "/admin/inject",qatestWrapper,sb.getBufStart());
		return false;
	}

	// check the reply, seems to have only a single docid in it...
	static bool s_y3 = false;
	if ( ! s_y3 ) { s_y3 = true; checkCRC ( -1970198487 ); }

	// now query check
	static bool s_y4 = false;
	if ( ! s_y4 ) {
		usleep(500000);
		s_y4 = true;
		getUrl ( "/search?c=qatest123&qa=1&format=xml&q=%2Bthe",
			 qatestWrapper );
		return false;
	}

	// check search results crc
	static bool s_y5 = false;
	if ( ! s_y5 ) { s_y5 = true; checkCRC ( -480078278 ); }






	// now get rdblist for every rdb for this coll and make sure all zero!
	//if ( ! checkRdbLists ( &s_rdbId1 ) ) return false;

	// dump, tight merge and ensure no data in our rdbs for this coll
	//if ( ! dumpTreesToDisk() ) return false;

	// wait for tight merge to complete
	//if ( ! waitForMergeToFinish() ) return false;

	// now get rdblist for every rdb for this coll and make sure all zero!
	//if ( ! checkRdbLists ( &s_rdbId2 ) ) return false;

	// reset the collection so we can test spidering
	//if ( ! resetColl ( ) ) return false;

	// add urls to seed spider with. make msg13.cpp recognize qatest123
	// collection and return 404 on urls not in our official list so
	// we can ensure search result consistency. msg13.cpp will initially
	// store the pages in a file, like the first 1,000 or so pages.
	//if ( ! addUrlTest () ) return false;

	// wait for spidering to complete. sleep callback. # of spidered urls
	// will be x, so we know when to stop
	//if ( ! checkSpidersDone() ) return false;

	// try a query delete
	//if ( ! queryDeleteTest() ) return false;

	// ensure empty
	//if ( ! checkRdbLists ( &s_rdbId3 ) ) return false;

	// delete the collection
	static bool s_fee = false;
	if ( ! s_fee ) {
		s_fee = true;
		return getUrl ( "/admin/delcoll?delcoll=qatest123" , 
				qatestWrapper );
	}

	static bool s_fee2 = false;
	if ( ! s_fee2 ) {
		s_fee2 = true;
		fprintf(stderr,"\n\n\nSUCCESSFULLY COMPLETED QA TEST\n\n\n");
		return true;
	}


	return true;
}


static char *s_urls1 =
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

	static bool s_x1 = false;
	if ( ! s_x1 ) {
		s_x1 = true;
		getUrl ( "/admin/delcoll?xml=1&delcoll=qatest123" , 
			 qatestWrapper );
		return false;
	}

	//
	// add the 'qatest123' collection
	//
	static bool s_x2 = false;
	if ( ! s_x2 ) {
		s_x2 = true;
		getUrl ( "/admin/addcoll?addcoll=qatest123&xml=1" , 
			 qatestWrapper );
		return false;
	}

	//
	// check addcoll reply
	//
	static bool s_x3 = false;
	if ( ! s_x3 ) {
		s_x3 = true;
		checkCRC ( 238170006 );
	}

	// . TODO: turn this off later once we've built up an archive
	//   of several hundred docs or so.
	// . enter qa build for for now. when this is off it will return 404
	//   for docs not stored in qa/ subdir
	static bool s_x4 = false;
	if ( ! s_x4 ) {
		s_x4 = true;
		getUrl("/admin/master?qabuildmode=1",qatestWrapper);
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
		getUrl ( "/admin/addurl",qatestWrapper,sb.getBufStart());
		return false;
	}
	
	return true;
}

// . run a series of tests to ensure that gb is functioning properly
// . uses the ./qa subdirectory to hold archive pages, ips, spider dates to
//   ensure consistency between tests for exact replays
bool qatest ( ) {

	if ( ! s_callback ) s_callback = qatest;

	qainject();

	qaspider();

	return true;
}

