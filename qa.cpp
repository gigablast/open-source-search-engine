#include <string.h>
#include "SafeBuf.h"
#include "HttpServer.h"

static long s_failures = 0;

bool getUrl( char *path , void (* callback) (void *state, TcpSocket *sock) ) {
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
				     NULL ) ) // useragent
		return false;
	// error?
	log("qa: getUrl error: %s",mstrerror(g_errno));
	return true;
}	

bool qatest ( ) ;

void qatestWrapper ( void *state , TcpSocket *sock ) { 
	log("qa: got reply(%li)=%s",sock->m_readOffset,sock->m_readBuf);
	qatest(); 

}	

// return false if blocked, true otherwise
bool addColl ( ) {
	static bool s_flag = false;
	if ( s_flag ) return true;
	s_flag = true;
	return getUrl ( "/admin/addcoll?c=qatest123&xml=1" , 
			qatestWrapper );
}


// first inject a set list of urls
static char  **s_urlPtrs = NULL;
static long    s_numUrls = 0;
static SafeBuf s_ubuf1;
static SafeBuf s_ubuf2;


bool loadUrls ( ) {
	static bool s_loaded = false;
	if ( s_loaded ) return true;
	// use injectme3 file
	s_ubuf1.load("./injectme3");
	// scan for +++URL: xxxxx
	char *s = s_ubuf1.getBufStart();
	for ( ; *s ; s++ ) {
		if ( strncmp(s,"+++URL: ",8) ) continue;
		// got one
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
	}
	// make array of url ptrs
	s_urlPtrs = (char **)s_ubuf2.getBufStart();
	return true;
}

bool injectUrls ( ) {
	loadUrls();
	static long s_ii = 0;
	for ( ; s_ii < s_numUrls ; ) {
		// pre-inc it
		s_ii++;
		// inject using html api
		SafeBuf sb;
		sb.safePrintf("/admin/inject?c=qatest123&delete=0&"
			      "format=xml&u=");
		sb.urlEncode ( s_urlPtrs[s_ii] );
		sb.nullTerm();
		return getUrl ( sb.getBufStart() , qatestWrapper );
	}
	return true;
}

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

static long s_checksums[] = {
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static long s_qi1 = 0;

void doneSearching1 ( void *state , TcpSocket *sock ) {
	//loadQueries1();
	long ii = s_qi1 - 1;
	// get checksum of it
	HttpMime hm;
	hm.set ( sock->m_readBuf , sock->m_readOffset , NULL );
	char *page = sock->m_readBuf + hm.getMimeLen() ;
	// we will need to ignore fields like the latency etc.
	// perhaps pass that in as a cgi parm. &qa=1
	long crc = hash32n ( page );
	if ( crc != s_checksums[ii] ) {
		log("qatest: query '%s' checksum %lu != %lu",
		    s_queries[ii],
		    s_checksums[ii],
		    crc);
		s_failures++;
	}
	// resume the qa loop
	qatest();
}
		

// ensure search results are consistent
bool searchTest1 () {
	long nq = sizeof(s_queries)/sizeof(char *);
	for ( ; s_qi1 < nq ; ) {
		// pre-inc it
		s_qi1++;
		// inject using html api
		SafeBuf sb;
		// qa=1 tell gb to exclude "variable" or "random" things
		// from the serps so we can checksum it consistently
		sb.safePrintf ( "/search?c=qatest123&qa=1&format=xml&q=" );
		sb.urlEncode ( s_queries[s_qi1] );
		sb.nullTerm();
		return getUrl ( sb.getBufStart() , doneSearching1 );
	}
	return true;
}	

static long s_qi2 = 0;

void doneSearching2 ( void *state , TcpSocket *sock ) {
	//loadQueries1();
	long ii = s_qi2 - 1;
	// get checksum of it
	HttpMime hm;
	hm.set ( sock->m_readBuf , sock->m_readOffset , NULL );
	char *page = sock->m_readBuf + hm.getMimeLen() ;
	// we will need to ignore fields like the latency etc.
	// perhaps pass that in as a cgi parm. &qa=1
	long crc = hash32n ( page );
	if ( crc != s_checksums[ii] ) {
		log("qatest: query '%s' checksum %lu != %lu",
		    s_queries[ii],
		    s_checksums[ii],
		    crc);
		s_failures++;
	}
	// resume the qa loop
	qatest();
}
		

// ensure search results are consistent
bool searchTest2 () {
	long nq = sizeof(s_queries)/sizeof(char *);
	for ( ; s_qi2 < nq ; ) {
		// pre-inc it
		s_qi2++;
		// inject using html api
		SafeBuf sb;
		// qa=1 tell gb to exclude "variable" or "random" things
		// from the serps so we can checksum it consistently
		sb.safePrintf ( "/search?c=qatest123&qa=1&format=xml&q=" );
		sb.urlEncode ( s_queries[s_qi2] );
		sb.nullTerm();
		return getUrl ( sb.getBufStart() , doneSearching2 );
	}
	return true;
}	

bool deleteUrls ( ) {
	static long s_ii2 = 0;
	for ( ; s_ii2 < s_numUrls ; ) {
		// pre-inc it
		s_ii2++;
		// reject using html api
		SafeBuf sb;
		sb.safePrintf( "/admin/inject?c=qatest123&delete=1&"
			       "format=xml&u=");
		sb.urlEncode ( s_urlPtrs[s_ii2] );
		sb.nullTerm();
		return getUrl ( sb.getBufStart() , qatestWrapper );
	}
	return true;
}

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

bool delColl ( ) {
	static bool s_flag = false;
	if ( s_flag ) return true;
	s_flag = true;
	return getUrl ( "/admin/delcoll?c=qatest123" , qatestWrapper );
}


static long s_rdbId1 = 0;
static long s_rdbId2 = 0;
//static long s_rdbId3 = 0;

// . run a series of tests to ensure that gb is functioning properly
// . use s_urls[] array of urls for injecting and spider seeding
// . contain an archive copy of all webpages in the injectme3 file and
//   in pagearchive1.txt file
// . while initially spidering store pages in pagearchive1.txt so we can
//   replay later. store up to 100,000 pages in there.
bool qatest ( ) {

	// add the 'qatest123' collection
	if ( ! addColl () ) return false;

	// inject urls, return false if not done yet
	if ( ! injectUrls ( ) ) return false;

	// test search results
	if ( ! searchTest1 () ) return false;

	// delete all urls cleanly now
	if ( ! deleteUrls ( ) ) return false;

	// now get rdblist for every rdb for this coll and make sure all zero!
	if ( ! checkRdbLists ( &s_rdbId1 ) ) return false;

	// dump, tight merge and ensure no data in our rdbs for this coll
	if ( ! dumpTreesToDisk() ) return false;

	// wait for tight merge to complete
	if ( ! waitForMergeToFinish() ) return false;

	// now get rdblist for every rdb for this coll and make sure all zero!
	if ( ! checkRdbLists ( &s_rdbId2 ) ) return false;

	// reset the collection so we can test spidering
	if ( ! resetColl ( ) ) return false;

	// add urls to seed spider with. make msg13.cpp recognize qatest123
	// collection and return 404 on urls not in our official list so
	// we can ensure search result consistency. msg13.cpp will initially
	// store the pages in a file, like the first 1,000 or so pages.
	if ( ! addUrlTest () ) return false;

	// wait for spidering to complete. sleep callback. # of spidered urls
	// will be x, so we know when to stop
	if ( ! checkSpidersDone() ) return false;

	// . now search again on the large collection most likely
	// . store search queries and checksum into queries2.txt
	// . a 0 (or no) checksum means we should fill it in
	if ( ! searchTest2 () ) return false;

	// try a query delete
	//if ( ! queryDeleteTest() ) return false;

	// ensure empty
	//if ( ! checkRdbLists ( &s_rdbId3 ) ) return false;

	// delete the collection
	if ( ! delColl() ) return false;

	return true;
}
