// diffbot api implementaion

//
// WHAT APIs are here?
//
// . 1. the CrawlBot API to start a crawl 
// . 2. To directly process a provided URL (injection)
// . 3. the Cache API so phantomjs can quickly check the cache for files
//      and quickly add files to the cache.
//

// Related pages:
//
// * http://diffbot.com/dev/docs/  (Crawlbot API tab, and others)
// * http://diffbot.com/dev/crawl/

#include "TcpServer.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "Pages.h" // g_msg
#include "XmlDoc.h" // for checkRegex()
#include "PageInject.h" // Msg7

//void printCrawlStats ( SafeBuf *sb , CollectionRec *cr ) ;
void doneSendingWrapper ( void *state , TcpSocket *sock ) ;
bool sendBackDump ( TcpSocket *s,HttpRequest *hr );
//void gotMsg4ReplyWrapper ( void *state ) ;
//bool showAllCrawls ( TcpSocket *s , HttpRequest *hr ) ;
char *getTokenFromHttpRequest ( HttpRequest *hr ) ;
char *getCrawlIdFromHttpRequest ( HttpRequest *hr ) ;
CollectionRec *getCollRecFromHttpRequest ( HttpRequest *hr ) ;
//CollectionRec *getCollRecFromCrawlId ( char *crawlId );
//void printCrawlStatsWrapper ( void *state ) ;
CollectionRec *addNewDiffbotColl ( HttpRequest *hr ) ;
bool isAliasUnique ( CollectionRec *cr , char *token , char *alias ) ;

char *g_diffbotFields [] = {
	"None",
	"All", // /api/analyze?mode=auto
	"Article (force)", // /api/article
	"Article (autodetect)", // /api/analyze?mode=article
	"Product (force)",
	"Product (autodetect)",
	"Image (force)",
	"Image (autodetect)",
	"FrontPage (force)",
	"FrontPage (autodetect)",

	//
	// last field must be empty. add new fields above this.
	//
	NULL
};

/*
class StateNC {
public:
	Msg4 m_msg4;
	collnum_t m_collnum;
	TcpSocket *m_socket;
};

class StateXX {
public:
	TcpSocket *m_socket;
	collnum_t m_collnum;
};

// . HttpServer.cpp calls handleDiffbotRequest() when it senses
//   a diffbot api request, like "GET /api/ *"
// . incoming request format described in diffbot.com/dev/docs/
// . use incoming request to create a new collection and set the crawl
//   parameters of the collection if it is "/api/startcrawl"
// . url format is like: live.diffbot.com/api/startcrawl
//   or /api/stopcrawl etc.
// . it does not seem to matter if the handler returns true or false!
bool handleDiffbotRequest ( TcpSocket *s , HttpRequest *hr ) {

	// . parse out stuff out of the url call
	// . these 3 are required
	long tokenLen = 0;
	char *token = hr->getString("token",&tokenLen);

	// the seed url
	char *seed  = hr->getString("seed");

	// this can be "article" "product" "frontpage" "image"
	char *api   = hr->getString("api");

	// apiQueryString holds the cgi parms to pass to the specific diffbot
	// api like /api/article?...<apiQueryString>
	char *apiQueryString = hr->getString("apiQueryString",NULL);

	// these are regular expressions
	char *urlCrawlPattern = hr->getString("urlCrawlPattern",NULL);
	char *urlProcessPattern = hr->getString("urlProcessPattern",NULL);
	char *pageProcessPattern = hr->getString("pageProcessPattern",NULL);

	// this is 1 or 0. if enabled then diffbot.com will only try to
	// extract json objects from page types that match "api" page type
	// specified above. so if "api" is "product" and a page is identified
	// as "image" then no json objects will be extracted.
	long classify = hr->getLong("classify",0);

	// default to 100,000 pages? max pages successfully downloaded,
	// so does not include tcptimeouts, dnstimeouts, but does include
	// bad http status codes, like 404.
	long long maxCrawled = hr->getLongLong("maxCrawled",100000LL);
	// default to 100,000 pages? # of pages SUCCESSFULLY got a reply
	// from diffbot for.
	long long maxToProcess = hr->getLongLong("maxProcessed",100000LL);
	char *id = hr->getString("id",NULL); // crawl id

	// start or stop a crawl or download? /api/startcrawl /api/stopcrawl or
	// /api/downloadcrawl /api/activecrawls
	char *path = hr->getPath();
	if ( ! path || strncmp(path,"/api/",5) != 0 ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: diffbot api path invalid)";
		return g_httpServer.sendErrorReply(s,500,"invalid diffbot "
						   "api path");
	}

	#define DB_STARTCRAWL    1
	#define DB_STOPCRAWL     2
	#define DB_CRAWLS        3
	#define DB_ACTIVECRAWLS  4
	#define DB_DOWNLOADURLS  5
	#define DB_DOWNLOADOBJECTS 6
	#define DB_RESUMECRAWL     7

	long func = 0;

	bool hasFormat = hr->hasField("format");

	if ( strncmp(path,"/api/startcrawl"   ,15) == 0 ) func=DB_STARTCRAWL;
	if ( strncmp(path,"/api/stopcrawl"    ,14) == 0 ) func=DB_STOPCRAWL;
	if ( strncmp(path,"/api/resumecrawl"  ,16) == 0 ) func=DB_RESUMECRAWL;
	if ( strncmp(path,"/api/crawls"       ,11) == 0 ) func=DB_CRAWLS;
	if ( strncmp(path,"/api/activecrawls" ,17) == 0 ) func=DB_ACTIVECRAWLS;
	if ( strncmp(path,"/api/downloadurls" ,17) == 0 ) func=DB_DOWNLOADURLS;
	if ( strncmp(path,"/api/downloadcrawl",18) == 0 ) {
		if ( ! hasFormat ) func = DB_DOWNLOADURLS;
		else               func = DB_DOWNLOADOBJECTS;
	}

	if ( ! func ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: diffbot api command invalid)";
		return g_httpServer.sendErrorReply(s,500,"invalid diffbot "
						   "api command");
	}

	// token is not required for /api/activecrawls or stopcrawl, only id
	if ( (func == DB_STARTCRAWL ||
	      func == DB_CRAWLS ) && 
	     ! token ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: need \"token\" parm)";
		return g_httpServer.sendErrorReply(s,500,
						   "missing \"token\" parm");
	}

	if ( (func == DB_STOPCRAWL ||
	      func == DB_RESUMECRAWL ||
	      func == DB_ACTIVECRAWLS ||
	      func == DB_DOWNLOADURLS ||
	      func == DB_DOWNLOADOBJECTS ) && 
	     ! id ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: need \"id\" parm)";
		return g_httpServer.sendErrorReply(s,500,
						   "missing \"id\" parm");
	}

	CollectionRec *cr = NULL;

	// get collrec
	if ( func == DB_STOPCRAWL    ||
	     func == DB_RESUMECRAWL  ||
	     func == DB_ACTIVECRAWLS ||
	     func == DB_DOWNLOADURLS ||
	     func == DB_DOWNLOADOBJECTS ) {
		// the crawlid needs a valid collection
		cr = getCollRecFromCrawlId ( id );
		// complain if not there
		if ( ! cr ) {
			g_errno = EBADREQUEST;
			g_msg = " (error: invalid diffbot crawl id or token)";
			return g_httpServer.sendErrorReply(s,500,"invalid "
							   "diffbot crawl id "
							   "or token");
		}
	}

	// if stopping crawl...
	if ( func == DB_STOPCRAWL ) {
		cr->m_spideringEnabled = 0;
		char *reply = "{\"reply\":\"success\"}";
		return g_httpServer.sendDynamicPage( s,
						     reply,
						     gbstrlen(reply),
						     0, // cacheTime
						     true, // POSTReply?
						     "application/json"
						     );
	}

	// resuming crawl
	if ( func == DB_RESUMECRAWL ) {
		cr->m_spideringEnabled = 1;
		char *reply = "{\"reply\":\"success\"}";
		return g_httpServer.sendDynamicPage( s,
						     reply,
						     gbstrlen(reply),
						     0, // cacheTime
						     true, // POSTReply?
						     "application/json"
						     );
	}

	// downloading the urls from spiderdb... sorted by time?
	if ( func == DB_DOWNLOADURLS )
		return sendBackDump ( s , hr , RDB_SPIDERDB );

	if ( func == DB_DOWNLOADOBJECTS )
		return sendBackDump ( s , hr , RDB_TITLEDB );

	// viewing crawl stats just for this one collection/crawl
	if ( func == DB_ACTIVECRAWLS ) {
		// state class in case update blocks
		StateXX *sxx;
		try { sxx = new (StateXX); }
		catch ( ... ) {
			g_msg = "(error: no mem for diffbot2)";
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		}
		mnew ( sxx , sizeof(StateXX), "statexx");
		// set this shit
		sxx->m_collnum = cr->m_collnum;
		sxx->m_socket = s;
		// . if blocks then return and wait for callback 2 be called
		// . set useCache to false to get semi-exact stats
		if ( ! updateCrawlInfo ( cr , 
					 sxx ,
					 printCrawlStatsWrapper , 
					 false )) 
			return false;
		// it did not block, so call wrapper directly
		printCrawlStatsWrapper ( sxx );
		// all done
		return true;
	}

	// show stats of ALL crawls done by this token
	if ( func == DB_CRAWLS )
		return showAllCrawls ( s , hr );

	// at this point they must be starting a new crawl.no other cmds remain
	if ( func != DB_STARTCRAWL ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: diffbot api command invalid)";
		return g_httpServer.sendErrorReply(s,500,"invalid diffbot "
						   "api command");
	}


	////////////////
	//
	// SUPPORT FOR GET /api/startcrawl
	//
	// Adds a new CollectionRec, injects the seed url into it, and
	// turns spidering on.
	//
	////////////////

	if ( ! seed ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: need seed parm)";
		return g_httpServer.sendErrorReply(s,500,"need seed parm");
	}

	if ( ! api ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: need seed api)";
		return g_httpServer.sendErrorReply(s,500,"need api parm");
	}

	// sanity
	if ( gbstrlen(seed)+1 >= MAX_URL_LEN ) {
		g_errno = EBADREQUEST;
		g_msg = " (error: seed url too long)";
		return g_httpServer.sendErrorReply(s,500,"seed url must "
						   "be less than 1023 "
						   "bytes");
	}


	// make sure the provided regular expressions compile ok
	SafeBuf sb;
	bool boolVal;
	bool boolValValid;
	long compileError;
	// test the url crawl pattern
	sb.set ( urlCrawlPattern );
	checkRegex (&sb,"x",&boolVal,&boolValValid,&compileError,cr);
	if ( compileError ) {
		g_errno = EBADREQUEST;
		g_msg = "(error: bad url crawl pattern)";
		return g_httpServer.sendErrorReply(s,500,
						   "bad url crawl pattern");
	}
	// test the url process pattern
	sb.set ( urlProcessPattern );
	checkRegex (&sb,"x",&boolVal,&boolValValid,&compileError,cr);
	if ( compileError ) {
		g_errno = EBADREQUEST;
		g_msg = "(error: bad url process pattern)";
		return g_httpServer.sendErrorReply(s,500,
						   "bad url process pattern");
	}
	// test the page process pattern
	sb.set ( pageProcessPattern );
	checkRegex (&sb,"x",&boolVal,&boolValValid,&compileError,cr);
	if ( compileError ) {
		g_errno = EBADREQUEST;
		g_msg = "(error: bad page process pattern)";
		return g_httpServer.sendErrorReply(s,500,
						   "bad page process pattern");
	}

	//
	// crap we need a new state: NC = New Collection state
	// because we do a msg4 below that could block...
	//
	StateNC *nc;
	try { nc = new (StateNC); }
	catch ( ... ) {
		g_msg = "(error: no mem for diffbot)";
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	mnew ( nc , sizeof(StateNC), "statenc");
	



	// let's create a new crawl id. dan was making it 32 characters
	// with 4 hyphens in it for a total of 36 bytes, but since
	// MAX_COLL_LEN, the maximum length of a collection name, is just
	// 64 bytes, and the token is already 32, let's limit to 16 bytes
	// for the crawlerid. so if we print that out in hex, 16 hex chars
	// 0xffffffff 0xffffffff is 64 bits. so let's make a random 64-bit
	// value here.
	unsigned long r1 = rand();
	unsigned long r2 = rand();
	unsigned long long crawlId64 = (unsigned long long) r1;
	crawlId64 <<= 32;
	crawlId64 |= r2;

	// the name of the new collection we are creating for this crawl
	// will be <tokenId>-<crawlId>. if it is a "test" crawl as
	// specified as an option in the diffbot crawlbot api page,
	// then make it <tokenId>-<crawlId>-test. Test crawls do not index,
	// they only crawl.
	char collBuf[MAX_COLL_LEN+1];
	// include a +5 for "-test"
	// include 16 for crawlid (16 char hex #)
	if ( tokenLen + 16 + 5>= MAX_COLL_LEN ) { char *xx=NULL;*xx=0;}
	char *testStr = "";
	//if ( cr->m_isDiffbotTestCrawl ) testStr = "-test";
	// ensure the crawlid is the full 16 characters long so we
	// can quickly extricate the crawlid from the collection name
	sprintf(collBuf,"%s-%016llx%s",token,crawlId64,testStr);

	/////////////
	//
	// . make a new collection! "cr" is the collectionRec.
	// . collection Name is the crawl id
	//
	/////////////
	if ( ! g_collectiondb.addRec ( collBuf ,
				     NULL ,  // copy from
				     0  , // copy from len
				     true , // it is a brand new one
				     -1 , // we are new, this is -1
				     false , // is NOT a dump
				     true // save it for sure!
				       ) ) {
		log("diffbot: failed to add new coll rec");
		g_msg = " (error: diffbot failed to allocate crawl)";
		return g_httpServer.sendErrorReply(s,500,"diffbot crawl "
						   "alloc failed?");
	}

	// get the collrec
	cr = g_collectiondb.getRec ( collBuf );

	// did an alloc fail?
	if ( ! cr ) { char *xx=NULL;*xx=0; }


	// noralize the seed url
	Url norm;
	norm.set ( seed );
	cr->m_diffbotSeed.set ( norm.getUrl() );
	
    
	// these must be there too
	//cr->m_diffbotToken.set ( token );
	cr->m_diffbotApi.set ( api );


	// these are optional, may be NULL
	cr->m_diffbotApiQueryString.set ( apiQueryString );
	cr->m_diffbotUrlCrawlPattern.set ( urlCrawlPattern );
	cr->m_diffbotUrlProcessPattern.set ( urlProcessPattern );
	cr->m_diffbotPageProcessPattern.set ( pageProcessPattern );
	cr->m_diffbotClassify = classify;

	// let's make these all NULL terminated strings
	cr->m_diffbotSeed.nullTerm();
	//cr->m_diffbotToken.nullTerm();
	cr->m_diffbotApi.nullTerm();
	cr->m_diffbotApiQueryString.nullTerm();
	cr->m_diffbotUrlCrawlPattern.nullTerm();
	cr->m_diffbotUrlProcessPattern.nullTerm();
	cr->m_diffbotPageProcessPattern.nullTerm();

	// do not spider more than this many urls total. -1 means no max.
	cr->m_diffbotMaxToCrawl = maxCrawled;
	// do not process more than this. -1 means no max.
	cr->m_diffbotMaxToProcess = maxToProcess;

	// reset the crawl stats
	cr->m_diffbotCrawlStartTime = gettimeofdayInMillisecondsGlobal();
	cr->m_diffbotCrawlEndTime   = 0LL;

	// reset crawler stats. they should be loaded from crawlinfo.txt
	memset ( &cr->m_localCrawlInfo , 0 , sizeof(CrawlInfo) );
	memset ( &cr->m_globalCrawlInfo , 0 , sizeof(CrawlInfo) );
	//cr->m_globalCrawlInfoUpdateTime = 0;
	cr->m_replies = 0;
	cr->m_requests = 0;

	// support current web page api i guess for test crawls
	//cr->m_isDiffbotTestCrawl = false;
	//char *strange = hr->getString("href",NULL);
	//if ( strange && strcmp ( strange,"/dev/crawl#testCrawl" ) == 0 )
	//	cr->m_isDiffbotTestCrawl = true;

	///////
	//
	// extra diffbot ARTICLE parms
	//
	///////
	// . ppl mostly use meta, html and tags.
	// . dropping support for dontStripAds. mike is ok with that.
	// . use for jsonp requests. needed for cross-domain ajax.
	//char *callback = hr->getString("callback",NULL);
	// a download timeout
	//long timeout = hr->getLong("timeout",5000);
	// "xml" or "json"
	char *format = hr->getString("format",NULL,"json");

	// save that
	cr->m_diffbotFormat.safeStrcpy(format);

	// return all content from page? for frontpage api.
	// TODO: can we put "all" into "fields="?
	//bool all = hr->hasField("all");

	
	/////////
	//
	// specify diffbot fields to return in the json output
	//
	/////////
	// point to the safebuf that holds the fields the user wants to
	// extract from each url. comma separated list of supported diffbot
	// fields like "meta","tags", ...
	SafeBuf *f = &cr->m_diffbotFields;
	// transcribe provided fields if any
	char *fields = hr->getString("fields",NULL);
	// appends those to our field buf
	if ( fields ) f->safeStrcpy(fields);
	// if something there push a comma in case we add more below
	if ( f->length() ) f->pushChar(',');
	// return contents of the page's meta tags? twitter card metadata, ..
	if ( hr->hasField("meta"    ) ) f->safeStrcpy("meta,");
	if ( hr->hasField("html"    ) ) f->safeStrcpy("html,");
	if ( hr->hasField("tags"    ) ) f->safeStrcpy("tags,");
	if ( hr->hasField("comments") ) f->safeStrcpy("comments,");
	if ( hr->hasField("summary" ) ) f->safeStrcpy("summary,");
	if ( hr->hasField("all"     ) ) f->safeStrcpy("all,");
	// if we added crap to "fields" safebuf remove trailing comma
	f->removeLastChar(',');


	// set some defaults. max spiders for all priorities in this collection
	cr->m_maxNumSpiders = 10;

	// make the gigablast regex table just "default" so it does not
	// filtering, but accepts all urls. we will add code to pass the urls
	// through m_diffbotUrlCrawlPattern alternatively. if that itself
	// is empty, we will just restrict to the seed urls subdomain.
	for ( long i = 0 ; i < MAX_FILTERS ; i++ ) {
		cr->m_regExs[i].purge();
		cr->m_spiderPriorities[i] = 0;
		cr->m_maxSpidersPerRule [i] = 10;
		cr->m_spiderIpWaits     [i] = 250; // 250 ms for now
		cr->m_spiderIpMaxSpiders[i] = 10;
		cr->m_spidersEnabled    [i] = 1;
		cr->m_spiderFreqs       [i] = 7.0;
	}


	// 
	// by default to not spider image or movie links or
	// links with /print/ in them
	//
	long i = 0;
	cr->m_regExs[i].safePrintf("$.css");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.mpeg");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.mpg");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.wmv");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf(".css?");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.jpg");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.JPG");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.gif");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.GIF");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("$.ico");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	cr->m_regExs[i].safePrintf("/print/");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	// if user did not specify a url crawl pattern then keep
	// the crawl limited to the same subdomain of the seed url
	if ( cr->m_diffbotUrlCrawlPattern.length() == 0 ) {
		// first limit to http://subdomain
		cr->m_regExs[i].safePrintf("^http://");
		cr->m_regExs[i].safeMemcpy(norm.getHost(),norm.getHostLen());
		cr->m_regExs[i].pushChar('/');
		cr->m_regExs[i].nullTerm();
		cr->m_spiderPriorities  [i] = 50;
		cr->m_maxSpidersPerRule [i] = 10;
		cr->m_spiderIpWaits     [i] = 250; // 500 ms for now
		cr->m_spiderIpMaxSpiders[i] = 10;
		cr->m_spidersEnabled    [i] = 1;
		i++;
		// then include HTTPS
		cr->m_regExs[i].safePrintf("^https://");
		cr->m_regExs[i].safeMemcpy(norm.getHost(),norm.getHostLen());
		cr->m_regExs[i].pushChar('/');
		cr->m_regExs[i].nullTerm();
		cr->m_spiderPriorities  [i] = 50;
		cr->m_maxSpidersPerRule [i] = 10;
		cr->m_spiderIpWaits     [i] = 250; // 500 ms for now
		cr->m_spiderIpMaxSpiders[i] = 10;
		cr->m_spidersEnabled    [i] = 1;
		i++;
		// and make all else filtered
		cr->m_regExs[i].safePrintf("default");
		cr->m_spiderPriorities  [i] = SPIDER_PRIORITY_FILTERED;
		cr->m_maxSpidersPerRule [i] = 10;
		cr->m_spiderIpWaits     [i] = 250; // 500 ms for now
		cr->m_spiderIpMaxSpiders[i] = 10;
		cr->m_spidersEnabled    [i] = 1;
		i++;
	}
	else {
		cr->m_regExs[i].safePrintf("default");
		cr->m_spiderPriorities  [i] = 50;
		cr->m_maxSpidersPerRule [i] = 10;
		cr->m_spiderIpWaits     [i] = 250; // 500 ms for now
		cr->m_spiderIpMaxSpiders[i] = 10;
		cr->m_spidersEnabled    [i] = 1;
		i++;
	}

	

	// just the default rule!
	cr->m_numRegExs   = i;
	cr->m_numRegExs2  = i;
	cr->m_numRegExs3  = i;
	cr->m_numRegExs10 = i;
	cr->m_numRegExs5  = i;
	cr->m_numRegExs6  = i;
	cr->m_numRegExs7  = i;

	//cr->m_spiderPriorities  [1] = -1; // filtered? or banned?
	//cr->m_maxSpidersPerRule [1] = 10;
	//cr->m_spiderIpWaits     [1] = 500; // 500 ms for now

	cr->m_needsSave = 1;


	// start the spiders!
	cr->m_spideringEnabled = true;

	// and global spider must be on...
	// do not turn it off on shutdown i guess, too
	g_conf.m_spideringEnabled = true;

	// . add the seed url to spiderdb
	// . make a "meta" list to add to spiderdb using msg4 below
	SafeBuf listBuf;
	listBuf.pushChar ( RDB_SPIDERDB );
	SpiderRequest sreq;
	// constructor does not use reset i guess so we must call it
	sreq.reset();
	// string ptr
	char *url = cr->m_diffbotSeed.getBufStart();
	// use this as the url
	strcpy( sreq.m_url, url );
	// parentdocid of 0
	long firstIp = hash32n ( url );
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	sreq.setKey( firstIp,0LL, false );
	sreq.m_isInjecting   = 0; 
	sreq.m_isPageInject  = 0;
	sreq.m_hopCount      = 0;
	sreq.m_hopCountValid = 1;
	sreq.m_fakeFirstIp   = 1;
	sreq.m_firstIp       = firstIp;

	// store it into list to add to spiderdb
	listBuf.safeMemcpy ( (char *)&sreq , sreq.getRecSize() );

	// allow search queries to take precedence over this operation.
	// otherwise, we'd make it niceness 0.
	long niceness = 1;

	Msg4 *m4 = &nc->m_msg4;
	// save this in our state
	nc->m_collnum = cr->m_collnum;
	nc->m_socket  = s;

	if ( m4->addMetaList ( listBuf.getBufStart(),
			       listBuf.length() ,
			       // add spiderrequest to our new coll name
			       collBuf,
			       nc ,
			       gotMsg4ReplyWrapper ,
			       niceness ) ) {
		// i guess it did not block
		gotMsg4ReplyWrapper ( nc );
		return true;
	}

	// it blocked
	return false;
}

// . come here after the SpiderRequest was added to Spiderdb
// . just transmit back the crawlerid, just like dan does now
void gotMsg4ReplyWrapper ( void *state ) {
	// cast it
	StateNC *nc = (StateNC *)state;
	// get the special ptr we hid in there
	CollectionRec *cr = g_collectiondb.getRec(nc->m_collnum);
	// the crawlid is the last 16 characters of the collection name
	char *crawlIdStr = cr->m_coll + cr->m_collLen - 16;
	// get it
	TcpSocket *socket = nc->m_socket;
	// nuke it
	delete nc;
	mdelete ( nc , sizeof(StateNC) , "stnc" );
	// httpserver.cpp copies the reply so don't worry that it is on
	// the stack
	//char reply[128];
	//sprintf(reply,"%016llx", crawlIdStr );
	// we successfully started the crawl...
	g_httpServer.sendDynamicPage ( socket , 
				       crawlIdStr, 
				       gbstrlen(crawlIdStr) );
}
*/

////////////////
//
// SUPPORT FOR DOWNLOADING an RDB DUMP
//
// We ask each shard for 10MB of Spiderdb records. If 10MB was returned
// then we repeat. Everytime we get 10MB from each shard we print the
// Spiderdb records out into "safebuf" and transmit it to the user. once
// the buffer has been transmitted then we ask the shards for another 10MB
// worth of spider records.
//
////////////////


// use this as a state while dumping out spiderdb for a collection
class StateCD {
public:
	StateCD () { m_needsMime = true; };
	void sendBackDump2 ( ) ;
	void readDataFromRdb ( ) ;
	void gotRdbList ( ) ;
	void printSpiderdbList ( RdbList *list , SafeBuf *sb , char *format ) ;
	void printTitledbList ( RdbList *list , SafeBuf *sb , char *format ) ;

	char m_fmt;
	Msg4 m_msg4;
	HttpRequest m_hr;
	Msg7 m_msg7;

	bool m_needsMime;
	char m_rdbId;
	bool m_downloadJSON;
	collnum_t m_collnum;
	long m_numRequests;
	long m_numReplies;
	long m_minRecSizes;
	bool m_someoneNeedsMore;
	TcpSocket *m_socket;
	Msg0 m_msg0s[MAX_HOSTS];
	key128_t m_spiderdbStartKeys[MAX_HOSTS];
	key_t m_titledbStartKeys[MAX_HOSTS];
	RdbList m_lists[MAX_HOSTS];
	bool m_needMore[MAX_HOSTS];

};

// . basically dump out spiderdb
// . returns urls in csv format in reply to a "GET /api/downloadcrawl "
// . the ordering of the urls is not specified so whatever order they are
//   in spiderdb will do
// . the gui that lists the urls as they are spidered in real time when you
//   do a test crawl will just have to call this repeatedly. it shouldn't
//   be too slow because of disk caching, and, most likely, the spider requests
//   will all be in spiderdb's rdbtree any how
// . because we are distributed we have to send a msg0 request to each 
//   shard/group asking for all the spider urls. dan says 30MB is typical
//   for a csv file, so for now we will just try to do a single spiderdb
//   request.
bool sendBackDump ( TcpSocket *s, HttpRequest *hr ) {

	CollectionRec *cr = getCollRecFromHttpRequest ( hr );
	if ( ! cr ) {
		char *msg = "token or id (crawlid) invalid";
		log("crawlbot: invalid token or crawlid to dump");
		g_httpServer.sendErrorReply(s,500,msg);
		return true;
	}

	char *path = hr->getPath();
	char rdbId = RDB_NONE;
	bool downloadJSON = false;
	if ( strncmp ( path ,"/crawlbot/downloadurls",22  ) == 0 )
		rdbId = RDB_SPIDERDB;
	if ( strncmp ( path ,"/crawlbot/downloadpages",23  ) == 0 )
		rdbId = RDB_TITLEDB;
	if ( strncmp ( path ,"/crawlbot/downloadobjects",25  ) == 0 ) {
		downloadJSON = true;
		rdbId = RDB_TITLEDB;
	}

	// sanity, must be one of 3 download calls
	if ( rdbId == RDB_NONE ) {
		char *msg ;
		msg = "usage: downloadurls, downloadpages, downloadobjects";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(s,500,msg);
		return true;
	}

	StateCD *st;
	try { st = new (StateCD); }
	catch ( ... ) {
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	mnew ( st , sizeof(StateCD), "statecd");
	// initialize the new state
	st->m_rdbId = rdbId;
	st->m_downloadJSON = downloadJSON;
	st->m_socket = s;
	// the name of the collections whose spiderdb we read from
	st->m_collnum = cr->m_collnum;
	// begin the possible segmented process of sending back spiderdb
	// to the user's browser
	st->sendBackDump2();
	// i dont think this return values matters at all since httpserver.cpp
	// does not look at it when it calls sendReply()
	return true;
}

void StateCD::sendBackDump2 ( ) {

	m_numRequests = 0;
	m_numReplies  = 0;

	// read 10MB from each shard's spiderdb at a time
	m_minRecSizes = 9999999;

	// we stop reading from all shards when this becomes false
	m_someoneNeedsMore = true;

	// initialize the spiderdb startkey "cursor" for each shard's spiderdb
	for ( long i = 0 ; i < g_hostdb.m_numGroups ; i++ ) {
		m_needMore[i] = true;
		KEYMIN((char *)&m_spiderdbStartKeys[i],sizeof(key128_t));
		KEYMIN((char *)&m_titledbStartKeys[i],sizeof(key_t));
	}

	// begin reading from each shard and sending the spiderdb records
	// over the network
	readDataFromRdb ( );

}

void gotRdbListWrapper ( void *state ) ;

void StateCD::readDataFromRdb ( ) {

	// set end key to max key. we are limiting using m_minRecSizes for this
	key128_t ek; KEYMAX((char *)&ek,sizeof(key128_t));

	CollectionRec *cr = g_collectiondb.getRec(m_collnum);

	// launch one request to each shard
	for ( long i = 0 ; i < g_hostdb.m_numGroups ; i++ ) {
		// count it
		m_numRequests++;
		// this is the least nice. crawls will yield to it mostly.
		long niceness = 0;
		// point to right startkey
		char *sk ;
		if ( m_rdbId == RDB_SPIDERDB )
			sk = (char *)&m_spiderdbStartKeys[i];
		else
			sk = (char *)&m_titledbStartKeys[i];
		// get host
		Host *h = g_hostdb.getLiveHostInGroup(i);
		// msg0 uses multicast in case one of the hosts in a shard is
		// dead or dies during this call.
		if ( ! m_msg0s[i].getList ( h->m_hostId , // use multicast
					    h->m_ip,
					    h->m_port,
					    0, // maxcacheage
					    false, // addtocache?
					    m_rdbId,
					   cr->m_coll,
					   &m_lists[i],
					   sk,
					   (char *)&ek,
					   // get at most about
					   // "minRecSizes" worth of spiderdb
					   // records
					   m_minRecSizes,
					   this,
					   gotRdbListWrapper ,
					   niceness ) )
			// continue if it blocked
			continue;
		// we got a reply back right away...
		m_numReplies++;
	}
	// all done? return if still waiting on more msg0s to get their data
	if ( m_numReplies < m_numRequests ) return;
	// done i guess, print and return
	gotRdbList();
}

void gotRdbListWrapper ( void *state ) {
	// get the Crawler dump State
	StateCD *st = (StateCD *)state;
	st->gotRdbList();
}
	
void StateCD::gotRdbList ( ) {
	// get the Crawler dump State
	// inc it
	m_numReplies++;
	// return if still awaiting more replies
	if ( m_numReplies < m_numRequests ) return;

	SafeBuf sb;
	//sb.setLabel("dbotdmp");

	// . if we haven't yet sent an http mime back to the user
	//   then do so here, the content-length will not be in there
	//   because we might have to call for more spiderdb data
	if ( m_needsMime ) {
		HttpMime mime;
		mime.makeMime ( -1, // totel content-lenght is unknown!
				0 , // do not cache (cacheTime)
				0 , // lastModified
				0 , // offset
				-1 , // bytesToSend
				NULL , // ext
				false, // POSTReply
				"text/csv", // contenttype
				"utf-8" , // charset
				-1 , // httpstatus
				NULL ); //cookie
		sb.safeMemcpy(mime.getMime(),mime.getMimeLen() );
	}

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// we set this to true below if any one shard has more spiderdb
	// records left to read
	m_someoneNeedsMore = false;

	//
	// got all replies... create the HTTP reply and send it back
	//
	for ( long i = 0 ; i < g_hostdb.m_numGroups ; i++ ) {
		if ( ! m_needMore[i] ) continue;
		// get the list from that group
		RdbList *list = &m_lists[i];

		// get the format
		char *format = cr->m_diffbotFormat.getBufStart();
		if ( cr->m_diffbotFormat.length() <= 0 ) format = NULL;

		char *ek = list->getEndKey();

		// now print the spiderdb list out into "sb"
		if ( m_rdbId == RDB_SPIDERDB ) {
			// print SPIDERDB list into "sb"
			printSpiderdbList ( list , &sb , format );
			//  update spiderdb startkey for this shard
			KEYSET((char *)&m_spiderdbStartKeys[i],ek,
			       sizeof(key128_t));
			// advance by 1
			m_spiderdbStartKeys[i] += 1;
		}

		if ( m_rdbId == RDB_TITLEDB ) {
			// print TITLEDB list into "sb"
			printTitledbList ( list , &sb , format );
			//  update titledb startkey for this shard
			KEYSET((char *)&m_titledbStartKeys[i],ek,
			       sizeof(key_t));
			// advance by 1
			m_titledbStartKeys[i] += 1;
		}

		// should we try to read more?
		m_needMore[i] = false;
		if ( list->m_listSize >= m_minRecSizes ) {
			m_needMore[i] = true;
			m_someoneNeedsMore = true;
		}
	}


	// if first time, send it back
	if ( m_needsMime ) {
		// only do once
		m_needsMime = false;
		// start the send process
		TcpServer *tcp = &g_httpServer.m_tcp;
		if (  ! tcp->sendMsg ( m_socket ,
				       sb.getBufStart(), // sendBuf     ,
				       sb.getCapacity(),//sendBufSize ,
				       sb.length(),//sendBufSize ,
				       sb.length(), // msgtotalsize
				       this       ,   // data for callback
				       doneSendingWrapper  ) ) { // callback
			// do not free sendbuf we are transmitting it
			sb.detachBuf();
			return;
		}
		// error?
		//TcpSocket *s = m_socket;
		// sometimes it does not block and is successful
		if ( ! g_errno ) sb.detachBuf();
		// nuke state
		delete this;
		mdelete ( this , sizeof(StateCD) , "stcd" );
		if ( g_errno )
			log("diffbot: tcp sendmsg did not block. error: %s",
			    mstrerror(g_errno));
		//g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
		return ;
	}


	// if nothing to send back we are done. return true since we
	// did not block sending back.
	if ( sb.length() == 0 ) return;

	// put socket in sending-again mode
	m_socket->m_sendBuf     = sb.getBufStart();
	m_socket->m_sendBufSize = sb.getCapacity();
	m_socket->m_sendBufUsed = sb.length();
	m_socket->m_sendOffset  = 0;
	m_socket->m_totalSent   = 0;
	m_socket->m_totalToSend = sb.length();

	// tell TcpServer.cpp to send this latest buffer! HACK!
	m_socket->m_sockState = ST_SEND_AGAIN;

	// do not let safebuf free this, we will take care of it
	sb.detachBuf();

	// . when it is done sending call this callback, don't hang up!
	// . if m_someoneNeedsMore is false then this callback should just
	//   destroy the socket and delete "this"
	m_socket->m_callback = doneSendingWrapper;
	m_socket->m_state    = this;
	// we blocked sending back
	return;
}

// TcpServer.cpp calls this when done sending TcpSocket's m_sendBuf
void doneSendingWrapper ( void *state , TcpSocket *sock ) {

	StateCD *st = (StateCD *)state;

	TcpSocket *socket = st->m_socket;

	// free the old sendbuf then i guess since we might replace it
	// in the above function.
	mfree ( socket->m_sendBuf ,
		socket->m_sendBufSize ,
		"dbsbuf");

	// in case we have nothing to send back do not let socket free
	// what we just freed above. it'll core.
	socket->m_sendBuf = NULL;

	// all done?
	if ( st->m_someoneNeedsMore ) {
		// . read more from spiderdb from one or more shards
		// . will also put into socket's write buf and set
		//   TcpSocket::m_sockState to ST_SEND_AGAIN so that
		//   TcpServer.cpp resumes the sending process and does not
		//   destroy the socket
		st->readDataFromRdb();
		return;
	}
	// delete that state
	delete st;
	mdelete ( st , sizeof(StateCD) , "stcd" );
}

void StateCD::printSpiderdbList ( RdbList *list , SafeBuf *sb , char *format) {
	// declare these up here
	SpiderRequest *sreq = NULL;
	SpiderReply   *srep = NULL;
	long long lastUh48 = 0LL;
	long long prevReplyUh48 = 0LL;
	long prevReplyError = 0;
	time_t prevReplyDownloadTime = 0LL;
	long badCount = 0;
	// parse through it
	for ( ; ! list->isExhausted() ; list->skipCurrentRec() ) {
		// this record is either a SpiderRequest or SpiderReply
		char *rec = list->getCurrentRec();
		// we encounter the spiderreplies first then the
		// spiderrequests for the same url
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			srep = (SpiderReply *)rec;
			sreq = NULL;
			prevReplyUh48 = srep->getUrlHash48();
			// 0 means indexed successfully. not sure if
			// this includes http status codes like 404 etc.
			// i don't think it includes those types of errors!
			prevReplyError = srep->m_errCode;
			prevReplyDownloadTime = srep->m_spideredTime;
			continue;
		}
		// ok, we got a spider request
		sreq = (SpiderRequest *)rec;
		// sanity check
		if ( srep && srep->getUrlHash48() != sreq->getUrlHash48()){
			badCount++;
			//log("diffbot: had a spider reply with no "
			//    "corresponding spider request for uh48=%lli"
			//    , srep->getUrlHash48());
			//char *xx=NULL;*xx=0;
		}

		// print the url if not yet printed
		long long uh48 = sreq->getUrlHash48  ();
		bool printIt = false;
		// there can be multiple spiderrequests for the same url!
		if ( lastUh48 != uh48 ) printIt = true;
		if ( ! printIt ) continue;
		lastUh48 = uh48;

		// debug point
		//if ( strstr(sreq->m_url,"chief") )
		//	log("hey");

		// 1 means spidered, 0 means not spidered, -1 means error
		long status = 1;
		// if unspidered, then we don't match the prev reply
		// so set "status" to 0 to indicate hasn't been 
		// downloaded yet.
		if ( lastUh48 != prevReplyUh48 ) status = 0;
		// if it matches, perhaps an error spidering it?
		if ( status && prevReplyError ) status = -1;

		// use the time it was added to spiderdb if the url
		// was not spidered
		time_t time = sreq->m_addedTime;
		// if it was spidered, successfully or got an error,
		// then use the time it was spidered
		if ( status ) time = prevReplyDownloadTime;

		char *msg = "Successfully Crawled";
		if ( status == 0 ) msg = "Unexamined";
		if ( status == -1 ) msg = mstrerror(prevReplyError);

		// "csv" is default if json not specified
		if ( format && strcmp(format,"json")==0 )
			sb->safePrintf("[{"
				       "{\"url\":"
				       "\"%s\"},"
				       "{\"time\":"
				       "\"%lu\"},"

				       "{\"status\":"
				       "\"%li\"},"

				       "{\"statusMsg\":"
				       "\"%s\"}"
				       
				       "}]\n"
				       , sreq->m_url
				       // when was it first added to spiderdb?
				       , sreq->m_addedTime
				       , status
				       , msg
				       );
		// but default to csv
		else
			sb->safePrintf("%s,%lu,%li,\"%s\""
				       //",%s"
				       "\n"
				       , sreq->m_url
				       // when was it first added to spiderdb?
				       , sreq->m_addedTime
				       , status
				       , msg
				       //, iptoa(sreq->m_firstIp)
				       );

	}

	if ( ! badCount ) return;

	log("diffbot: had a spider reply with no "
	    "corresponding spider request %li times", badCount);
}



void StateCD::printTitledbList ( RdbList *list , SafeBuf *sb , char *format ) {

	XmlDoc xd;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// parse through it
	for ( ; ! list->isExhausted() ; list->skipCurrentRec() ) {
		// this record is either a SpiderRequest or SpiderReply
		char *rec = list->getCurrentRec();
		// skip ifnegative
		if ( (rec[0] & 0x01) == 0x00 ) continue;
		// uncompress it
		if ( ! xd.set2 ( rec ,
				 0, // maxSize unused
				 cr->m_coll ,
				 NULL , // ppbuf
				 0 , // niceness
				 NULL ) ) { // spiderRequest
			log("diffbot: error setting titlerec in dump");
			continue;
		}
		// must be of type json to be a diffbot json object
		if ( m_downloadJSON && xd.m_contentType != CT_JSON ) continue;
		// or if downloading web pages...
		if ( ! m_downloadJSON ) {
			// skip if json object content type
			if ( xd.m_contentType == CT_JSON ) continue;
			// . just print the cached page
			// . size should include the \0
			sb->safeStrcpy ( xd.m_firstUrl.m_url);
			// then \n
			sb->pushChar('\n');
			// then page content
			sb->safeStrcpy ( xd.ptr_utf8Content );
			// null term just in case
			//sb->nullTerm();
			// separate pages with \0 i guess
			sb->pushChar('\0');
			// \n
			sb->pushChar('\n');
			continue;
		}

		// skip if not a diffbot json url
		if ( ! xd.m_isDiffbotJSONObject ) continue;

		// get the json content
		char *json = xd.ptr_utf8Content;
		
		// just print that out. encode \n's and \r's back to \\n \\r
		// and backslash to a \\ ...
		// but if they originally had a \u<backslash> encoding and
		// we made into utf8, do not put that back into the \u
		// encoding because it is not necessary.
		if ( ! sb->safeStrcpyPrettyJSON ( json ) ) 
			log("diffbot: error printing json in dump");

		// separate each JSON object with \n i guess
		sb->pushChar('\n');

	}
}

/*
////////////////
//
// SUPPORT FOR GET /api/crawls and /api/activecrawls
//
// Just scan each collection record whose collection name includes the
// provided "token" of the user. then print out the stats of just
//
////////////////

// example output for http://live.diffbot.com/api/crawls?token=matt
// [{"id":"c421f09d-7c31-4131-9da2-21e35d8130a9","finish":1378233585887,"matched":274,"status":"Stopped","start":1378233159848,"token":"matt","parameterMap":{"token":"matt","seed":"www.techcrunch.com","api":"article"},"crawled":274}]

// example output from activecrawls?id=....
// {"id":"b7df5d33-3fe5-4a6c-8ad4-dad495b586cd","finish":null,"matched":27,"status":"Crawling","start":1378322184332,"token":"matt","parameterMap":{"token":"matt","seed":"www.alleyinsider.com","api":"article"},"crawled":34}

// NOTE: it does not seem to include active crawls! bad!! like if you lost
// the crawlid...

// "cr" is NULL if showing all crawls!
bool showAllCrawls ( TcpSocket *s , HttpRequest *hr ) {

	long tokenLen = 0;
	char *token = hr->getString("token",&tokenLen);

	// token MUST be there because this function's caller checked for it
	if ( ! token ) { char *xx=NULL;*xx=0; }

	// store the crawl stats as html into "sb"
	SafeBuf sb;

	// scan the collection recs
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if empty
		if ( ! cr ) continue;
		// get name
		char *coll = cr->m_coll;
		//long collLen = cr->m_collLen;
		// skip if first 16 or whatever characters does not match
		// the user token because the name of a collection is
		// <TOKEN>-<CRAWLID>
		if ( coll[0] != token[0] ) continue;
		if ( coll[1] != token[1] ) continue;
		if ( coll[2] != token[2] ) continue;
		// scan the rest
		bool match = true;
		for ( long i = 3 ; coll[i] && token[i] ; i++ ) {
			// the name of a collection is <TOKEN>-<CRAWLID>
			// so if we hit the hyphen we are done
			if ( coll[i] == '-' ) break;
			if ( coll[i] != token[i] ) { match = false; break; }
		}
		if ( ! match ) continue;
		// we got a match, print them out
		printCrawlStats ( &sb , cr );
	}

	// and send back now
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), 
					     sb.length(),
					     -1);// cachetime

}
*/

char *getTokenFromHttpRequest ( HttpRequest *hr ) {
	// provided directly?
	char *token = hr->getString("token",NULL,NULL);
	if ( token ) return token;
	// extract token from coll?
	char *c = hr->getString("c",NULL,NULL);
	if ( ! c ) return NULL;
	CollectionRec *cr = g_collectiondb.getRec(c);
	if ( ! cr ) return NULL;
	if ( cr->m_diffbotToken.length() <= 0 ) return NULL;
	token = cr->m_diffbotToken.getBufStart();
	return token;
}

CollectionRec *getCollRecFromHttpRequest ( HttpRequest *hr ) {
	// if we have the collection name explicitly, get the coll rec then
	char *c = hr->getString("c",NULL,NULL);
	if ( c ) return g_collectiondb.getRec ( c );
	// otherwise, get it from token/crawlid
	char *token = getTokenFromHttpRequest( hr );
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		if ( strcmp ( cr->m_diffbotToken.getBufStart(),token)==0 )
			return cr;
	}
	// no matches
	return NULL;
}

/*
// doesn't have to be fast, so  just do a scan
CollectionRec *getCollRecFromCrawlId ( char *crawlId ) {

	long idLen = gbstrlen(crawlId);

	// scan collection names
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if empty
		if ( ! cr ) continue;
		// get name
		char *coll = cr->m_coll;
		long collLen = cr->m_collLen;
		if ( collLen < 16 ) continue;
		// skip if first 16 or whatever characters does not match
		// the user token because the name of a collection is
		// <TOKEN>-<CRAWLID>
		if ( coll[collLen-1] != crawlId[idLen-1] ) continue;
		if ( coll[collLen-2] != crawlId[idLen-2] ) continue;
		if ( coll[collLen-3] != crawlId[idLen-3] ) continue;
		if ( ! strstr ( coll , crawlId ) ) continue;
		return cr;
	}
	return NULL;
}

void printCrawlStatsWrapper ( void *state ) {
	StateXX *sxx = (StateXX *)state;
	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec(sxx->m_collnum);
	// print out the crawl
	SafeBuf sb;
	printCrawlStats ( &sb , cr );
	// save before nuking state
	TcpSocket *sock = sxx->m_socket;
	// nuke the state
	delete sxx;
	mdelete ( sxx , sizeof(StateXX) , "stxx" );
	// and send back now
	g_httpServer.sendDynamicPage ( sock ,
				       sb.getBufStart(), 
				       sb.length(),
				       -1 ); // cachetime
}


void printCrawlStats ( SafeBuf *sb , CollectionRec *cr ) {

	// if we are the first, print a '[' to start a json thingy
	if ( sb->length() == 0 )
		sb->pushChar('[');
	// otherwise, remove the previous ']' since we are not the last
	else {
		char *p = sb->getBufStart();
		long plen = sb->length();
		if ( p[plen-1]=='[' ) 
			sb->incrementLength(-1);
	}
	
	sb->safePrintf( "{"
			"\"id\":\""
			);
	// get the token from coll name
	char *token = cr->m_coll;
	// and the length, up to the hyphen that separates it from crawl id
	long tokenLen = 0;
	for ( ; token[tokenLen] && token[tokenLen] != '-' ; tokenLen++ );
	// now crawl id
	char *crawlId = token + tokenLen;
	// skip hyphen
	if ( crawlId[0] == '-' ) crawlId++;
	// print crawl id out
	sb->safeStrcpy ( crawlId );
	// end its quote
	sb->safeStrcpy ( "\",");
	// now the time the crawl finished. 
	if ( cr->m_spideringEnabled )
		sb->safePrintf("\"finish\":null,");
	else
		sb->safePrintf("\"finish\":%lli,",cr->m_diffbotCrawlEndTime);
	// how many urls we handoff to diffbot api. that implies successful
	// download and that it matches the url crawl pattern and 
	// url process pattern and content regular expression pattern.
	//
	// NOTE: pageProcessAttempts can be higher than m_pageDownloadAttempts
	// when we call getMetaList() on an *old* (in titledb) xmldoc,
	// where we just get the cached content from titledb to avoid a
	// download, but we still call getDiffbotReply(). perhaps reconstruct
	// the diffbot reply from XmlDoc::m_diffbotJSONCount
	//
	// "processed" here corresponds to the "maxProcessed" cgi parm 
	// specified when instantiating the crawl parms for the first time.
	//
	// likewise "crawled" corresponds to "maxCrawled"
	//
	sb->safePrintf("\"processedAttempts\":%lli,",
		       cr->m_globalCrawlInfo.m_pageProcessAttempts);
	sb->safePrintf("\"processed\":%lli,",
		       cr->m_globalCrawlInfo.m_pageProcessSuccesses);

	sb->safePrintf("\"crawlAttempts\":%lli,",
		       cr->m_globalCrawlInfo.m_pageDownloadAttempts);
	sb->safePrintf("\"crawled\":%lli,",
		       cr->m_globalCrawlInfo.m_pageDownloadSuccesses);

	sb->safePrintf("\"urlsConsidered\":%lli,",
		       cr->m_globalCrawlInfo.m_urlsConsidered);

	// how many spiders outstanding for this coll right now?
	SpiderColl *sc = g_spiderCache.getSpiderColl(cr->m_collnum);
	long spidersOut = sc->getTotalOutstandingSpiders();

	// . status of the crawl: "Stopped" or "Active"?
	// . TODO: check with dan to see if Active is correct and
	//   ShuttingDown is allowable
	if ( cr->m_spideringEnabled )
		sb->safePrintf("\"status\":\"Active\",");
	else if ( spidersOut )
		sb->safePrintf("\"status\":\"ShuttingDown\",");
	else
		sb->safePrintf("\"status\":\"Stopped\",");

	// spider crawl start time
	sb->safePrintf("\"start\":%lli,",cr->m_diffbotCrawlStartTime);

	// the token
	sb->safePrintf("\"token\":\"");
	sb->safeMemcpy(token,tokenLen);
	sb->safePrintf("\",");

	//
	// BEGIN parameter map
	//
	// the token again
	sb->safePrintf("{");
	sb->safePrintf("\"token\":\"");
	sb->safeMemcpy(token,tokenLen);
	sb->safePrintf("\",");
	// the seed url
	sb->safePrintf("\"seed\":\"%s\",",cr->m_diffbotSeed.getBufStart());
	// the api
	sb->safePrintf("\"api\":\"%s\",",cr->m_diffbotApi.getBufStart());
	sb->safePrintf("},");
	//
	// END parameter map
	//

	// crawl count. counts non-errors. successful downloads.
	//sb->safePrintf("\"crawled\":%lli",
	//	       cr->m_globalCrawlInfo.m_pageCrawlAttempts);
	
	sb->safePrintf("}");

	// assume we are the last json object in the array
	sb->pushChar(']');

}
*/

////////////////
//
//  **** THE CRAWLBOT CONTROL PANEL *****
//
// . Based on  http://diffbot.com/dev/crawl/ page. 
// . got to /dev/crawl to see this!
//
////////////////


// generate a random collection name
char *getNewCollName ( ) { // char *token , long tokenLen ) {
	// let's create a new crawl id. dan was making it 32 characters
	// with 4 hyphens in it for a total of 36 bytes, but since
	// MAX_COLL_LEN, the maximum length of a collection name, is just
	// 64 bytes, and the token is already 32, let's limit to 16 bytes
	// for the crawlerid. so if we print that out in hex, 16 hex chars
	// 0xffffffff 0xffffffff is 64 bits. so let's make a random 64-bit
	// value here.
	unsigned long r1 = rand();
	unsigned long r2 = rand();
	unsigned long long crawlId64 = (unsigned long long) r1;
	crawlId64 <<= 32;
	crawlId64 |= r2;

	static char s_collBuf[MAX_COLL_LEN+1];

	//long tokenLen = gbstrlen(token);

	// include a +5 for "-test"
	// include 16 for crawlid (16 char hex #)
	//if ( tokenLen + 16 + 5>= MAX_COLL_LEN ) { char *xx=NULL;*xx=0;}
	// ensure the crawlid is the full 16 characters long so we
	// can quickly extricate the crawlid from the collection name
	//memcpy ( s_collBuf, token, tokenLen );
	//sprintf(s_collBuf + tokenLen ,"-%016llx",crawlId64);
	sprintf(s_collBuf ,"%016llx",crawlId64);
	return s_collBuf;
}

//////////////////////////////////////////
//
// MAIN API STUFF I GUESS
//
//////////////////////////////////////////

// so user can specify the format of the reply/output
#define FMT_HTML 1
#define FMT_XML  2
#define FMT_JSON 3

bool sendErrorReply2 ( TcpSocket *socket , long fmt , char *msg ) {

	// log it
	log("crawlbot: %s",msg);

	// send this back to browser
	SafeBuf sb;
	if ( fmt == FMT_JSON ) 
		sb.safePrintf("{\"response\":fail},"
			      "{\"reason\":\"%s\"}\n"
			      , msg );
	else
		sb.safePrintf("<html><body>"
			      "failed: %s"
			      "</body></html>"
			      , msg );

	//return g_httpServer.sendErrorReply(socket,500,sb.getBufStart());
	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     -1); // cachetime
}

void addedUrlsToSpiderdbWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;
	SafeBuf rr;
	rr.safePrintf("Successfully scheduled urls for spidering");
	printCrawlBotPage2 ( st->m_socket,
			     &st->m_hr ,
			     st->m_fmt,
			     NULL ,
			     &rr );
	delete st;
	mdelete ( st , sizeof(StateCD) , "stcd" );
}

void injectedUrlWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;

	Msg7 *msg7 = &st->m_msg7;
	// the doc we injected...
	XmlDoc *xd = &msg7->m_xd;

	// make a status msg for the url
	SafeBuf sb;
	if ( xd->m_indexCode == 0 ) {
		sb.safePrintf("<b><font color=black>"
			      "Successfully added ");
	}
	else if ( xd->m_indexCode == EDOCFILTERED ) {
		sb.safePrintf("<b><font color=red>"
			      "Error: <i>%s</i> by matching "
			      "url filter #%li "
			      "when adding "
			      , mstrerror(xd->m_indexCode) 
			      , xd->m_urlFilterNum
			      );
		
	}
	else {
		sb.safePrintf("<b><font color=red>"
			      "Error: <i>%s</i> when adding "
			      , mstrerror(xd->m_indexCode) );
	}
	sb.safeTruncateEllipsis(xd->m_firstUrl.getUrl(),60);
	
	if ( xd->m_indexCode == 0 ) {
		if ( xd->m_numOutlinksAddedValid ) 
			sb.safePrintf(" &nbsp; (added %li outlinks)",
				      (long)xd->m_numOutlinksAdded);
		else
			sb.safePrintf(" &nbsp; (added 0 outlinks)");
	}
	
	sb.safePrintf("</font></b>");
	sb.nullTerm();

	// . this will call g_httpServer.sendReply()
	// . pass it in the injection response, "sb"
	printCrawlBotPage2 ( st->m_socket,
			     &st->m_hr ,
			     st->m_fmt,
			     &sb ,
			     NULL );
	delete st;
	mdelete ( st , sizeof(StateCD) , "stcd" );
}
	

class HelpItem {
public:
	char *m_parm;
	char *m_desc;
};
static class HelpItem s_his[] = {
	{"format","Use &format=json to show JSON output."},
	{"token","Required for all operations below."},
	{"delcoll","Specify collection name to delete."},
	{"resetcoll","Specify collection name to reset."},
	{"addcoll","Say addcoll=1 to add a new collection."},
	{"c","Specify the collection name. "
	 "Required for all operations below. Just pass the token to "
	 "the /crawlbot page to see a list of all collections that the "
	 "token controls."},
	{"pause","Use pause=0 or pause=1 to activate or pause spidering "
	 "respectively."},
	{"alias", "Set the collection name alias to this string. Must be "
	 "unqiue over all collections under the same token."},
	{"maxtocrawl", "Specify max pages to successfully download"},
	{"maxtoprocess", "Specify max pages to successfully process through "
	 "diffbot"},
	{"urt","Use robots.txt?"},
	{"fe[N]","Filter expression #N. The first expression in the url "
	 "filters table is 0. But if N is 0, leave N out, only specify it "
	 "if N is > 0. Example &fe=onsamedomain to change the expression in "
	 "row #0 to onsamedomain. Or &fe1=foobar to change the expression "
	 "in the second row to foobar."},
	{"cspe[N]","spidering enabled for row #N in url filters table."},
	{"fsf[N]","Respider frequency in days for row #N in url filters table."},
	{"mspr[N]","Max outstanding spiders for this spider priority."},
	{"mspi[N]","Max outstanding spiders for this IP."},
	{"xg[N]","Wait this many milliseconds between spiders of same IP."},
	{"fsp[N]","Spider priority. Higher priorities spidered first. Can be from 0 to 127. But -3 means to ignore the URL. -2 means the URL is banned because it comes from an evil site."},
	{"dapi[N]","Diffbot api number. Process through this diffbot api."},
	{"injecturl","Specify a seed url to inject."},
	{"urldata","A huge string of whitespace separated URLs to add to "
	 "spiderdb for crawling."},
	{"spiderlinks","Use 0 or 1 to not spider or spider links from "
	 "the injected url respectively. Pass this along with the injecturl "
	 "parameter. Any injected url will be treated as a seed url. Use "
	 "this parameter in conjunction with the urldata parameter as well."},
	{"ins_dapi[N]","Insert a row above row #N. Do not include [N] if it "
	 "is row 0."},
	{"rm_dapi[N]","Delete row #N. Do not include [N] if it "
	 "is row 0."},

	{NULL,NULL}
};

bool printCrawlBotPage ( TcpSocket *socket , HttpRequest *hr ) {

	// print help
	long help = hr->getLong("help",0);
	if ( help ) {
		SafeBuf sb;
		sb.safePrintf("<html>"
			      "<title>Crawlbot API</title>"
			      "<h1>Crawlbot API</h1>"
			      "<b>Use the parameters below on the "
			      "<a href=\"/crawlbot\">/crawlbot</a> page."
			      "</b><br><br>"
			      "<table>"
			      );
		for ( long i = 0 ; i < 1000 ; i++ ) {
			HelpItem *h = &s_his[i];
			if ( ! h->m_parm ) break;
			sb.safePrintf( "<tr>"
				       "<td>%s</td>"
				       "<td>%s</td>"
				       "</tr>"
				       , h->m_parm
				       , h->m_desc
				       );
		}
		sb.safePrintf("</table>"
			      "</html>");
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     -1); // cachetime
	}


	// . now show stats for the current crawl
	// . put in xml or json if format=xml or format=json or
	//   xml=1 or json=1 ...
	char fmt = FMT_HTML;
	char *fs = hr->getString("format",NULL,NULL);
	// give john a json api
	if ( fs && strcmp(fs,"json") == 0 ) fmt = FMT_JSON;
	if ( fs && strcmp(fs,"xml") == 0 ) fmt = FMT_XML;


	// if no token... they need to login or signup
	char *token = getTokenFromHttpRequest ( hr );
	if ( ! token ) {
		// send back json error msg
		if ( fmt == FMT_JSON ) {
			char *msg = "invalid token";
			return sendErrorReply2 (socket,fmt,msg);
		}
		// print token form if html
		SafeBuf sb;
		sb.safePrintf("In order to use crawlbot you must "
			      "first LOGIN:"
			      "<form action=/crawlbot method=get>"
			      "<br>"
			      "<input type=text name=token size=50>"
			      "<input type=submit name=submit value=OK>"
			      "</form>"
			      "<br>"
			      "<b>- OR -</b>"
			      "<br> SIGN UP"
			      "<form action=/crawlbot method=get>"
			      "Name: <input type=text name=name size=50>"
			      "<br>"
			      "Email: <input type=text name=email size=50>"
			      "<br>"
			      "<input type=submit name=submit value=OK>"
			      "</form>"
			      "</body>"
			      "</html>");
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     -1); // cachetime
	}


	// get collection name if any was specified
	char *coll = hr->getString("c",NULL,NULL);
	// and rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );


	// make a new state
	StateCD *st;
	try { st = new (StateCD); }
	catch ( ... ) {
		return sendErrorReply2 ( socket , fmt , mstrerror(g_errno));
	}
	mnew ( st , sizeof(StateCD), "statecd");

	// copy crap
	st->m_hr.copy ( hr );
	st->m_socket = socket;
	st->m_fmt = fmt;


	///////
	// 
	// handle file of urls upload. can be HUGE!
	//
	///////
	char *urlData = hr->getString("urldata",NULL,NULL);
	if ( urlData ) {
		// a valid collection is required
		if ( ! cr ) 
			return sendErrorReply2(socket,fmt,
					       "invalid collection");
		// avoid spidering links for these urls? i would say
		// default is to NOT spider the links...
		long spiderLinks = hr->getLong("spiderlinks",0);
		// make a list of spider requests from these urls
		SafeBuf listBuf;
		// this returns NULL with g_errno set
		bool status = getSpiderRequestMetaList ( urlData , 
							 &listBuf ,
							 spiderLinks );
		// empty?
		long size = listBuf.length();
		// error?
		if ( ! status )
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		// if not list
		if ( ! size )
			return sendErrorReply2(socket,fmt,"no urls found");
		// add to spiderdb
		if ( ! st->m_msg4.addMetaList( listBuf.getBufStart() ,
					       listBuf.length(),
					       cr->m_coll,
					       st ,
					       addedUrlsToSpiderdbWrapper,
					       0 // niceness
					       ) )
			// blocked!
			return false;
		// did not block, print page!
		addedUrlsToSpiderdbWrapper(st);
		return true;
	}

	/////////
	//
	// handle direct injection of a url. looks at "spiderlinks=1" parm
	// and all the other parms in Msg7::inject() in PageInject.cpp.
	//
	//////////
	char *injectUrl = hr->getString("injecturl",NULL,NULL);
	if ( injectUrl ) {
		// a valid collection is required
		if ( ! cr ) 
			return sendErrorReply2(socket,fmt,
					       "invalid collection");
		// begin the injection
		if ( ! st->m_msg7.inject ( st->m_socket,
					   &st->m_hr,
					   st ,
					   injectedUrlWrapper ) )
			// if blocked, return now
			return false;
		// otherwise send back reply
		injectedUrlWrapper ( st );
		return true;
	}

	//
	// print the html or json page of all the data
	//
	return printCrawlBotPage2 ( socket , hr , fmt , NULL , NULL );
}



bool printCrawlBotPage2 ( TcpSocket *socket , 
			  HttpRequest *hr ,
			  char fmt, // format
			  SafeBuf *injectionResponse ,
			  SafeBuf *urlUploadResponse ) {
	
	// store output into here
	SafeBuf sb;

	if ( fmt == FMT_HTML )
		sb.safePrintf(
			      "<html>"
			      "<title>Crawlbot - "
			      "Web Data Extraction and Search Made "
			      "Easy</title>"
			      "<body>"
			      );


	CollectionRec *cr = NULL;

	// . add new collection?
	// . make a new collection and it also becomes the cursor
	long addColl = hr->getLong("addcoll",0);
	if ( addColl )
		cr = addNewDiffbotColl ( hr );

	char *delColl = hr->getString("delcoll",NULL,NULL);
	if ( delColl )
		g_collectiondb.deleteRec ( delColl , true );

	char *resetColl = hr->getString("resetcoll",NULL,NULL);
	if ( resetColl )
		g_collectiondb.resetColl ( resetColl );

	// set this to current collection. if only token was provided
	// then it will return the first collection owned by token.
	// if token has no collections it will be NULL.
	if ( ! cr ) 
		cr = getCollRecFromHttpRequest ( hr );

	// if you reset from crawlbot api page then enable spiders
	if ( resetColl && cr )
		cr->m_spideringEnabled = 1;

	// if no collection, then it is the first time or this token
	// so automatically add one for them
	if ( ! cr ) 
		cr = addNewDiffbotColl ( hr );

	if ( ! cr ) {
		char *msg = "failed to add new collection";
		g_msg = " (error: crawlbot failed to allocate crawl)";
		return sendErrorReply2 ( socket , fmt , msg );
	}
			
	char *token = getTokenFromHttpRequest ( hr );

	if ( fmt == FMT_HTML ) {
		sb.safePrintf("<table border=0>"
			      "<tr><td>"
			      "<b><font size=+2>"
			      "<a href=/crawlbot?token=%s>"
			      "Crawlbot</a></font></b>"
			      "<br>"
			      "<font size=-1>"
			      "Crawl, Datamine and Index the Web"
			      "</font>"
			      "</td></tr>"
			      "</table>"
			      , token
			      );
		sb.safePrintf("<center><br>");
		// first print help
		sb.safePrintf("[ <a href=/crawlbot?help=1>"
			      "api help</a> ] &nbsp; "
			      // json output
			      "[ <a href=/crawlbot?token=%s&format=json>"
			      "json output"
			      "</a> ] &nbsp; "
			      , token );
		// first print "add new collection"
		sb.safePrintf("[ <a href=/crawlbot?addcoll=1&token=%s>"
			      "add new collection"
			      "</a> ] &nbsp; "
			      "[ <a href=/crawlbot?summary=1&token=%s>"
			      "show all collections"
			      "</a> ] &nbsp; "

			      , token
			      , token
			      );
	}
	

	long tokenLen = gbstrlen(token);
	bool firstOne = true;

	//
	// print list of collections controlled by this token
	//
	for ( long i = 0 ; fmt == FMT_HTML && i<g_collectiondb.m_numRecs;i++ ){
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// get its token if any
		char *ct = cx->m_diffbotToken.getBufStart();
		if ( ! ct ) continue;
		// skip if token does not match
		if ( strcmp(ct,token) )
			continue;
		// highlight the tab if it is what we selected
		bool highlight = false;
		if ( cx == cr ) highlight = true;
		char *style = "";
		if  ( highlight ) {
			style = "style=text-decoration:none; ";
			sb.safePrintf ( "<b><font color=red>");
		}
		// print the crawl id. collection name minus <TOKEN>-
		sb.safePrintf("<a %shref=/crawlbot?c=%s>"
			      "%s"
			      "</a> &nbsp; "
			      , style
			      , cx->m_coll
			      , cx->m_coll
			      );
		if ( highlight )
			sb.safePrintf("</font></b>");
	}

	if ( fmt == FMT_HTML )
		sb.safePrintf ( "</center><br/>" );

	//////
	//
	// print collection summary page
	//
	//////

	if ( fmt == FMT_JSON )
		sb.safePrintf("[");//\"collections\":");

	long summary = hr->getLong("summary",0);
	// enter summary mode for json
	if ( fmt != FMT_HTML ) summary = 1;
	// start the table
	if ( summary && fmt == FMT_HTML ) {
		sb.safePrintf("<table border=1 cellpadding=5>"
			      "<tr>"
			      "<td><b>Collection</b></td>"
			      "<td><b>Objects Found</b></td>"
			      "<td><b>URLs Harvested</b></td>"
			      "<td><b>URLs Examined</b></td>"
			      "<td><b>Page Download Attempts</b></td>"
			      "<td><b>Page Download Successes</b></td>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td><b>Page Process Successes</b></td>"
			      "</tr>"
			      );
	}
	// scan each coll and get its stats
	for ( long i = 0 ; summary && i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// must belong to us
		if ( strcmp(cx->m_diffbotToken.getBufStart(),token) )
			continue;

		// if json, print each collectionrec
		if ( fmt == FMT_JSON ) {
			if ( ! firstOne ) 
				sb.safePrintf(",\n\t");
			firstOne = false;
			char *alias = "";
			if ( cx->m_collectionNameAlias.length() > 0 )
				alias=cx->m_collectionNameAlias.getBufStart();
			sb.safePrintf("\n\n{"
				      "\"name\":\"%s\",\n"
				      "\"alias\":\"%s\",\n"
				      "\"crawlingEnabled\":%li,\n"
				      "\"objectsFound\":%lli,\n"
				      "\"urlsHarvested\":%lli,\n"
				      "\"urlsExamined\":%lli,\n"
				      "\"pageDownloadAttempts\":%lli,\n"
				      "\"pageDownloadSuccesses\":%lli,\n"
				      "\"pageProcessAttempts\":%lli,\n"
				      "\"pageProcessSuccesses\":%lli,\n"
				      // settable parms
				      "\"maxtocrawl\":%lli,\n"
				      "\"maxtoprocess\":%lli,\n"
				      "\"urt\":%li,\n"
				      ,cx->m_coll
				      , alias
				      , (long)cx->m_spideringEnabled 
				      , cx->m_globalCrawlInfo.m_objectsAdded -
				      cx->m_globalCrawlInfo.m_objectsDeleted
				      , cx->m_globalCrawlInfo.m_urlsHarvested
				      , cx->m_globalCrawlInfo.m_urlsConsidered
				, cx->m_globalCrawlInfo.m_pageDownloadAttempts
				, cx->m_globalCrawlInfo.m_pageDownloadSuccesses
				, cx->m_globalCrawlInfo.m_pageProcessAttempts
				, cx->m_globalCrawlInfo.m_pageProcessSuccesses
				      , cx->m_diffbotMaxToCrawl
				      , cx->m_diffbotMaxToProcess
				      , (long)cx->m_useRobotsTxt
				      );
			/////
			//
			// show url filters table. kinda hacky!!
			//
			/////
			g_parms.sendPageGeneric ( socket ,
						  hr ,
						  PAGE_FILTERS ,
						  NULL ,
						  &sb ,
						  cr->m_coll,  // coll override
						  true // isJSON?
						  );
			// remove trailing comma
			sb.removeLastChar('\n');
			sb.removeLastChar(',');
			// end that collection rec
			sb.safePrintf("\n}\n");
			// print the next one out
			continue;
		}


		// print in table
		sb.safePrintf("<tr>"
			      "<td>%s</td>"
			      "<td>%lli</td>"
			      "<td>%lli</td>"
			      "<td>%lli</td>"
			      "<td>%lli</td>"
			      "<td>%lli</td>"
			      "<td>%lli</td>"
			      "<td>%lli</td>"
			      "</tr>"
			      , cx->m_coll
			      , cx->m_globalCrawlInfo.m_objectsAdded -
			        cx->m_globalCrawlInfo.m_objectsDeleted
			      , cx->m_globalCrawlInfo.m_urlsHarvested
			      , cx->m_globalCrawlInfo.m_urlsConsidered
			      , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			      , cx->m_globalCrawlInfo.m_pageProcessAttempts
			      , cx->m_globalCrawlInfo.m_pageProcessSuccesses
			      );
	}
	if ( summary && fmt == FMT_HTML ) {
		sb.safePrintf("</table></html>" );
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     -1); // cachetime
	}

	if ( fmt == FMT_JSON ) 
		// end the array of collection objects
		sb.safePrintf("\n]\n");

	///////
	//
	// end print collection summary page
	//
	///////


	long maxToCrawl = hr->getLongLong("maxtocrawl",-1LL);
	long maxToProcess = hr->getLongLong("maxtoprocess",-1LL);
	if ( maxToCrawl != -1 ) {
		cr->m_diffbotMaxToCrawl = maxToCrawl;
		cr->m_needsSave = 1;
	}
	if ( maxToProcess != -1 ) {
		cr->m_diffbotMaxToProcess = maxToProcess;
		cr->m_needsSave = 1;
	}

	///////
	//
	// a new alias?
	//
	///////
	char *aliasResponse = "";
	char *alias = hr->getString("alias",NULL,NULL );
	SafeBuf atr;
	if ( alias && alias[0] ) {
		// assume ok
		aliasResponse = "<font size=-2><br>"
			"Alias updated successfully.</font>";
		// scan to make sure unique for OUR token
		if ( ! isAliasUnique ( cr , token , alias ) ) {
			atr.safePrintf("<font color=red>%s"
				       "<font size=-2><br>"
				       "Alias is not unqiue. Please choose "
				       "another one.</font></font>", alias);
			aliasResponse = atr.getBufStart();
			// if json bail out i guess
			if ( fmt == FMT_JSON ) {
				char *msg = "alias is not unique for token";
				return sendErrorReply2(socket,fmt,msg);
			}
		}
		else {
			cr->m_collectionNameAlias.set(alias);
			cr->m_collectionNameAlias.nullTerm();
			cr->m_needsSave = 1;
		}
	}

		

	//char *api = hr->getString("diffbotapi",NULL,NULL);
	//if ( api ) {
	//	cr->m_diffbotApi.set(api);
	//	cr->m_diffbotApi.nullTerm();
	//}


	long pause = hr->getLong("pause",-1);
	if ( pause == 0 ) cr->m_spideringEnabled = 1;
	if ( pause == 1 ) cr->m_spideringEnabled = 0;

	//
	// show urls being crawled (ajax) (from Spider.cpp)
	//
	if ( fmt == FMT_HTML ) {
		sb.safePrintf ( "<table width=100%% cellpadding=5 "
				"style=border-width:1px;border-style:solid;"
				"border-color:black;>"
				//"bgcolor=#%s>\n" 
				"<tr><td colspan=50>"// bgcolor=#%s>"
				"<b>Last 10 URLs</b> (%li spiders active)"
				//,LIGHT_BLUE
				//,DARK_BLUE
				,(long)g_spiderLoop.m_numSpidersOut);
		if ( cr->m_spideringEnabled )
			sb.safePrintf(" "
				      "<a href=/crawlbot?c=%s&pause=1>"
				      "<font color=red><b>Pause spiders</b>"
				      "</font></a>"
				      , cr->m_coll
				      );
		else
			sb.safePrintf(" "
				      "<a href=/crawlbot?c=%s&pause=0>"
				      "<font color=green><b>Resume "
				      "spidering</b>"
				      "</font></a>"
				      , cr->m_coll
				      );

		sb.safePrintf("</td></tr>\n" );

		// the table headers so SpiderRequest::printToTable() works
		if ( ! SpiderRequest::printTableHeaderSimple(&sb,true) ) 
			return false;
		// shortcut
		XmlDoc **docs = g_spiderLoop.m_docs;
		// first print the spider recs we are spidering
		for ( long i = 0 ; i < (long)MAX_SPIDERS ; i++ ) {
			// get it
			XmlDoc *xd = docs[i];
			// skip if empty
			if ( ! xd ) continue;
			// sanity check
			if ( ! xd->m_oldsrValid ) { char *xx=NULL;*xx=0; }
			// skip if not our coll rec!
			if ( xd->m_cr != cr ) continue;
			// grab it
			SpiderRequest *oldsr = &xd->m_oldsr;
			// get status
			char *status = xd->m_statusMsg;
			// show that
			if ( ! oldsr->printToTableSimple ( &sb , status,xd) ) 
				return false;
		}

		// end the table
		sb.safePrintf ( "</table>\n" );
		sb.safePrintf ( "<br>\n" );

	} // end html format



	//
	// show stats
	//
	if ( fmt == FMT_HTML ) {
		sb.safePrintf("<br>"

			      "<form method=get action=/crawlbot>"
			      "<input type=hidden name=c value=\"%s\">"
			      , cr->m_coll
			      );

		sb.safePrintf("<TABLE border=0>"
			      "<TR><TD valign=top>"

			      "<table border=0 cellpadding=5>"

			      // this will  have to be in crawlinfo too!
			      //"<tr>"
			      //"<td><b>pages indexed</b>"
			      //"<td>%lli</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Objects Found</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>URLs Harvested</b></td>"
			      "<td>%lli</td>"
     
			      "</tr>"

			      "<tr>"
			      "<td><b>URLs Examined</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Download Attempts</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Download Successes</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes</b></td>"
			      "<td>%lli</td>"
			      "</tr>"

			      
			      "</table>"

			      "</TD>"
			      
			      , cr->m_globalCrawlInfo.m_objectsAdded -
			        cr->m_globalCrawlInfo.m_objectsDeleted
			      , cr->m_globalCrawlInfo.m_urlsHarvested
			      , cr->m_globalCrawlInfo.m_urlsConsidered

			      , cr->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccesses

			      , cr->m_globalCrawlInfo.m_pageProcessAttempts
			      , cr->m_globalCrawlInfo.m_pageProcessSuccesses
			      );

		// spacer column
		sb.safePrintf("<TD>"
			      "&nbsp;&nbsp;&nbsp;&nbsp;"
			      "&nbsp;&nbsp;&nbsp;&nbsp;"
			      "</TD>"
			      );

		// what diffbot api to use?
		/*
		char *api = cr->m_diffbotApi.getBufStart();
		char *s[10];
		for ( long i = 0 ; i < 10 ; i++ ) s[i] = "";
		if ( api && strcmp(api,"all") == 0 ) s[0] = " selected";
		if ( api && strcmp(api,"article") == 0 ) s[1] = " selected";
		if ( api && strcmp(api,"product") == 0 ) s[2] = " selected";
		if ( api && strcmp(api,"image") == 0 ) s[3] = " selected";
		if ( api && strcmp(api,"frontpage") == 0 ) s[4] = " selected";
		if ( api && strcmp(api,"none") == 0 ) s[5] = " selected";
		if ( ! api || ! api[0] ) s[5] = " selected";
		*/
		sb.safePrintf( "<TD valign=top>"

			      "<table cellpadding=5 border=0>"
			       /*
			      "<tr>"
			      "<td>"
			      "Diffbot API"
			      "</td><td>"
			      "<select name=diffbotapi>"
			      "<option value=all%s>All</option>"
			      "<option value=article%s>Article</option>"
			      "<option value=product%s>Product</option>"
			      "<option value=image%s>Image</option>"
			      "<option value=frontpage%s>FrontPage</option>"
			      "<option value=none%s>None</option>"
			      "</select>"
			      "</td>"
			       "</tr>"
			      , s[0]
			      , s[1]
			      , s[2]
			      , s[3]
			      , s[4]
			      , s[5]
			       */
			      );

		char *alias = "";
		if ( cr->m_collectionNameAlias.length() > 0 )
			alias = cr->m_collectionNameAlias.getBufStart();

		sb.safePrintf(
			      //
			      "<tr>"
			      "<td><b>Collection Name:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Collection Alias:</td>"
			      "<td>%s%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Token:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Download Objects:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/downloadobjects?"
			      "c=%s&"
			      "format=json>"
			      "json</a>"
			      "&nbsp; "
			      "<a href=/crawlbot/downloadobjects?"
			      "c=%s&"
			      "format=xml>"
			      "xml</a>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Download Urls:</b> "
			      "</td><td>"
			      /*
			      "<a href=/api/downloadcrawl?"
			      "c=%s"
			      "format=json>"
			      "json</a>"
			      " &nbsp; "
			      "<a href=/api/downloadcrawl?"
			      "c=%s"
			      "format=xml>"
			      "xml</a>"
			      "&nbsp; "
			      */
			      "<a href=/crawlbot/downloadurls?c=%s"
			      //"&format=csv"
			      ">"
			      "csv</a>"
			      //
			      "</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Download Pages:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/downloadpages?"
			      "c=%s"
			      //"&format=csv"
			      ">"
			      "raw</a>"
			      //
			      "</td>"
			      "</tr>"


			      
			      //
			      //
			      "<tr>"
			      "<td><b>Max Page Download Successes:</b> "
			      "</td><td>"
			      "<input type=text name=maxtocrawl "
			      "size=9 value=%lli> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"
			      "<tr>"
			      "<td><b>Max Page Process Successes:</b>"
			      "</td><td>"
			      "<input type=text name=maxtoprocess "
			      "size=9 value=%lli> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr><td>"
			      "Use Robots.txt when crawling? "
			      "</td><td>"
			      "<input type=checkbox name=userobotstxt checked>"
			      "</td>"
			      "</tr>"

			      "<tr><td>"
			      "Use spider proxies on AWS? "
			      "</td><td>"
			      "<input type=checkbox name=usefloaters checked>"
			      "</td>"
			      "</tr>"


			      "</table>"

			      "</TD>"
			      "</TR>"
			      "</TABLE>"

			      "</form>"


			      , cr->m_coll
			      , alias
			      , aliasResponse

			      , cr->m_diffbotToken.getBufStart()

			      , cr->m_coll
			      , cr->m_coll
			      //, cr->m_coll
			      //, cr->m_coll

			      , cr->m_coll
			      , cr->m_coll

			      , cr->m_diffbotMaxToCrawl 
			      , cr->m_diffbotMaxToProcess

			      );
	}

	unsigned long r1 = rand();
	unsigned long r2 = rand();
	unsigned long long rand64 = (unsigned long long) r1;
	rand64 <<= 32;
	rand64 |=  r2;


	if ( fmt == FMT_HTML )
		sb.safePrintf(
			      "<table border=0 cellpadding=5>"
			      
			      // OBJECT search input box
			      "<form method=get action=/search>"
			      "<tr>"
			      "<td>"
			      "<b>Search Objects:</b>"
			      "</td><td>"
			      "<input type=text name=q size=50>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=rand value=%lli>"
			      // restrict search to json objects
			      "<input type=hidden name=prepend "
			      "value=\"type:json |\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</tr>"
			      "</form>"
			      
			      
			      // PAGE search input box
			      "<form method=get action=/search>"
			      "<tr>"
			      "<td>"
			      "<b>Search Pages:</b>"
			      "</td><td>"
			      "<input type=text name=q size=50>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=rand value=%lli>"
			      // restrict search to NON json objects
			      "<input type=hidden "
			      "name=prepend value=\"-type:json |\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</tr>"
			      "</form>"
			      
			      // add url input box
			      "<form method=get action=/crawlbot>"
			      "<tr>"
			      "<td>"
			      "<b>Inject Url: </b>"
			      "</td><td>"
			      "<input type=text name=injecturl size=50>"
			      " "
			      "<input type=submit name=submit value=OK>"
			      " &nbsp; &nbsp; <input type=checkbox "
			      "name=spiderlinks value=1 "
			      "checked>"
			      " <i>crawl links on this page?</i>"
			      , cr->m_coll
			      , rand64
			      , cr->m_coll
			      , rand64
			      );

	if ( injectionResponse && fmt == FMT_HTML )
		sb.safePrintf("<br><font size=-1>%s</font>\n"
			      ,injectionResponse->getBufStart() 
			      );

	if ( fmt == FMT_HTML )
		sb.safePrintf("<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=crawlbotapi value=1>"
			      "</td>"
			      "</tr>"
			      "</form>"
			      
			      
			      "<tr>"
			      "<td><b>Upload URLs</b></td>"
			      
			      "<td>"
			      // this page will call 
			      // printCrawlbotPage2(uploadResponse) 2display it
			      "<form method=get action=/crawlbot>"
			      "<input type=file name=urldata size=40>"
			      "</form>"
			      
			      " &nbsp; &nbsp; <input type=checkbox "
			      "name=spiderlinks value=1 "
			      "checked>"
			      " <i>crawl links on those pages?</i>"
			      
			      "</td>"
			      "</tr>"
			      
			      "</table>"
			      "<br>"
			      , cr->m_coll
			      );


	// xml or json does not show the input boxes
	//if ( format != FMT_HTML ) 
	//	return g_httpServer.sendDynamicPage ( s, 
	//					      sb.getBufStart(), 
	//					      sb.length(),
	//					      -1 ); // cachetime


	//
	// print url filters. use "multimedia" to handle jpg etc.
	//
	// use "notindexable" for images/movies/css etc.
	// add a "process" column to send to diffbot...
	//
	//

	char *s1 = "Show";
	char *s2 = "none";
	if ( hr->getLongFromCookie("showtable",0) ) {
		s1 = "Hide";
		s2 = "";
	}

	if ( fmt == FMT_HTML )
		sb.safePrintf(
			      
			      "<a onclick="
			      "\""
			      "var e = document.getElementById('filters');"
			      "var m = document.getElementById('msg');"
			      "if ( e.style.display == 'none' ){"
			      "e.style.display = '';"
			      "m.innerHTML='Hide URL Filters Table';"
			      "document.cookie = 'showtable=1;';"
			      "}"
			      "else {"
			      "e.style.display = 'none';"
			      "m.innerHTML='Show URL Filters Table';"
			      "document.cookie = 'showtable=0;';"
			      "}"
			      "\""
			      " "
			      "style="
			      "cursor:hand;"
			      "cursor:pointer;"
			      "color:blue;>"
			      
			      "<u><b>"
			      "<div id=msg>"
			      "%s URL Filters Table"
			      "</div>"
			      "</b></u>"
			      "</a>"
			      
			      "<div id=filters style=display:%s;>"
			      "<form method=get action=/crawlbot>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=showtable value=1>"
			      , s1
			      , s2
			      , cr->m_coll
			      );
	
	//////////
	//
	// . update the parms for this collection
	// . just update the url filters for now since that is complicated
	//
	//////////
	long page = PAGE_FILTERS;
	WebPage *pg = g_pages.getPage ( page ) ;
	g_parms.setFromRequest ( hr , socket , pg->m_function);


	//
	// print url filters. HACKy...
	//
	if ( fmt == FMT_HTML )
		g_parms.sendPageGeneric ( socket ,
					  hr ,
					  PAGE_FILTERS ,
					  NULL ,
					  &sb ,
					  cr->m_coll,  // coll override
					  false ); // isJSON?
	//
	// end HACKy hack
	//
	if ( fmt == FMT_HTML )
		sb.safePrintf(
			      "</form>"
			      "</div>"
			      "<br>"
			      "<br>"
			      );



	//
	// add search box to your site
	//
	/*
	sb.safePrintf("<br>"
		      "<table>"
		      "<tr>"
		      "<td><a onclick=unhide();>"
		      "Add this search box to your site"
		      "</a>"
		      "</td>"
		      "</tr>"
		      "</table>");
	*/


	//
	// show input boxes
	//


	if ( fmt == FMT_HTML ) {
		sb.safePrintf(
			      "<table cellpadding=5>"
			      "<tr>"

			      "<td>"


			      // reset collection form
			      "<form method=get action=/crawlbot>"
			      "<input type=hidden name=token value=\""
			      );
		sb.safeMemcpy ( token , tokenLen );
		sb.safePrintf("\">"

			      "<input type=hidden name=resetcoll value=%s>"
			      // also show it in the display, so set "c"
			      "<input type=hidden name=c value=%s>"
			      "<input type=submit name=button value=\""
			      "Reset this collection\">"
			      "</form>"
			      // end reset collection form


			      "</td>"

			      "<td>"

			      // delete collection form
			      "<form method=get action=/crawlbot>"
			      "<input type=hidden name=token value=\""
			      , cr->m_coll
			      , cr->m_coll
			      );
		sb.safeMemcpy ( token , tokenLen );
		sb.safePrintf("\">"

			      "<input type=hidden name=delcoll value=%s>"
			      "<input type=submit name=button value=\""
			      "Delete this collection\">"
			      "</form>"
			      // end delete collection form




			      "</td>"

			      "</tr>"
			      "</table>"
			      "</form>"
			      , cr->m_coll
			      );
	}

	// this could be in html json or xml
	return g_httpServer.sendDynamicPage ( socket, 
					      sb.getBufStart(), 
					      sb.length(),
					      -1 ); // cachetime

	/*		      
		      "<h1>API for Diffbot</h1>"
		      "<form action=/api/diffbot>"
		      "<input type=text name=url size=100>"
		      "<input type=submit name=inject value=\"Inject\">"
		      "</form>"
		      "<br>"

		      "<h1>API for Crawlbot</h1>"

      //        "<form id=\"addCrawl\" onSubmit=\"addCrawlFromForm(); return false;\">"
		      "<form action=/api/startcrawl method=get>"


	"<div class=\"control-group well\">"
        "<div id=\"apiSelection\" class=\"titleColumn\">"
	"<div class=\"row \">"

		      "Token: <input type=text name=token><br><br>"
		      "API: <input type=text name=api> <i>(article, product)</i><br><br>"

	"<div class=\"span2\"><label class=\"on-default-hide\">Page-type</label></div>"
	"<div class=\"input-append span7\">"
	"<select id=\"apiSelect\" name=\"api\" class=\"span2\" value=\"sds\">"
	"<option value=\"\" disabled=\"disabled\" selected=\"selected\">Select pages to process and extract</option>"
	"<option class=\"automatic\" value=\"article\">Article</option>"
	"<option class=\"automatic\" value=\"frontpage\">Frontpage</option>"
	"<option class=\"automatic\" value=\"image\">Image</option>"
	"<option class=\"automatic\" value=\"product\">Product</option>"
	"</select>"
	"<span id=\"formError-apiSelect\" class=\"formError\">Page-type is required</span>"
	"<span class=\"inputNote\">API calls will be made using your current token.</span>"
	"</div>"
	"</div>"
        "</div>"
        "<div id=\"apiQueryString\" class=\"titleColumn\">"
	"<div class=\"row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\">API Querystring</label></div>"
	"<div class=\"input-prepend span7\">"
	"<span class=\"add-on\">?</span><input class=\"span6 search-input\" name=\"apiQueryString\" size=\"16\" type=\"text\" placeholder=\"Enter a querystring to specify Diffbot API parameters\">"
	"</div>"
	"</div>"
        "</div>"
        "<hr>"
        "<div id=\"seedUrl\" class=\"titleColumn\">"
	"<div class=\"row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\">Seed URL</label></div>"
	"<div class=\"input-append span7\">"
	"<input class=\"span6 search-input\" name=\"seed\" size=\"16\" type=\"text\" placeholder=\"Enter a seed URL\">"
	"<span id=\"formError-seedUrl\" class=\"formError\"><br>Seed URL is required</span>"
	"</div>"
	"</div>"
        "</div>"
        "<hr>"
        "<div id=\"headerRow\" class=\"titleColumn\">"
	"<div class=\"row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\"><strong>Crawl Filters</strong></label></div>"
	"</div>"
        "</div>"
        "<div id=\"urlCrawlPattern\" class=\"titleColumn\">"
	"<div class=\"regex-edit row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\">URL Regex</label></div>"
	"<div class=\"input-append span7\">"
	"<input class=\"span6\" name=\"urlCrawlPattern\" size=\"16\" type=\"text\" placeholder=\"Only crawl pages whose URLs match this regex\" value=\"\">"
	"<span class=\"inputNote\">Diffbot uses <a href=\"http://www.regular-expressions.info/refflavors.html\" target=\"_blank\">Java regex syntax</a>. Be sure to escape your characters.</span>"
	"</div>"
	"</div>"
        "</div>"
        "<div id=\"maxCrawled\" class=\"titleColumn\">"
	"<div class=\"regex-edit row \"><div class=\"span2\"><label class=\"on-default-hide\">Max Pages Crawled</label></div>         <div class=\"input-append span7\">               <input class=\"span1\" name=\"maxCrawled\" size=\"\" type=\"text\" value=\"\">            </div>          </div>        </div>        <div id=\"headerRow\" class=\"titleColumn\">          <div class=\"row \">		<div class=\"span2\"><label class=\"on-default-hide\"><strong>Processing Filters</strong></label></div>          </div>        </div>        <div id=\"classify\" class=\"titleColumn\">          <div class=\"row\">		<div class=\"span2\" id=\"smartProcessLabel\"><label class=\"on-default-hide\">Smart Processing</label></div>		<div class=\"span7\"><label class=\"checkbox\"><input id=\"smartProcessing\" type=\"checkbox\" name=\"classify\"><span id=\"smartProcessAutomatic\">Only process pages that match the selected page-type. Uses <a href=\"/our-apis/classifier\">Page Classifier API</a>.</span><span id=\"smartProcessCustom\">Smart Processing only operates with Diffbot <a href=\"/products/automatic\">Automatic APIs.</a></span></label></div>          </div>        </div>        <div id=\"urlProcessPattern\" class=\"titleColumn\">          <div class=\"regex-edit row \">		<div class=\"span2\"><label class=\"on-default-hide\">URL Regex</label></div>            <div class=\"input-append span7\">                <input class=\"span6\" name=\"urlProcessPattern\" size=\"16\" type=\"text\" placeholder=\"Only process pages whose URLs match this regex\" value=\"\">            </div>          </div>        </div>        <div id=\"pageProcessPattern\" class=\"titleColumn\">          <div class=\"regex-edit row \">		<div class=\"span2\"><label class=\"on-default-hide\">Page-Content Regex</label></div>            <div class=\"input-append span7\">                <input class=\"span6\" name=\"pageProcessPattern\" size=\"16\" type=\"text\" placeholder=\"Only process pages whose content contains a match to this regex\" value=\"\">            </div>          </div>        </div>        <div id=\"maxMatches\" class=\"titleColumn\">          <div class=\"regex-edit row \">		<div class=\"span2\"><label class=\"on-default-hide\">Max Pages Processed</label></div>            <div class=\"input-append span7\">                <input class=\"span1\" name=\"maxProcessed\" size=\"16\" type=\"text\" value=\"\">            </div>          </div>        </div>        <hr>        <div class=\"controls row\">		<div class=\"span2\">&nbsp;</div>            <div class=\"span7\" id=\"startCrawlButtons\">					   <button id=\"testButton\" class=\"btn\" type=\"button\" onclick=\"testcrawl(formToData());clicky.log('/dev/crawl#testCrawl','Test Crawl');\">Test</button>						   "

"<!--<button id=\"submitButton\" class=\"btn btn-info\" type=\"button\" onclick=\"addCrawlFromForm()\" >Start Crawl</button>-->"

"<input type=submit name=start value=\"Start Crawl\">"


"          </div>        </div>    </div>        <div id=\"hiddenTestDiv\"  style=\"display: none;\"></div>    </form>    </div><!-- end Crawler tab -->" );


*/
}

CollectionRec *addNewDiffbotColl ( HttpRequest *hr ) {

	char *token = getTokenFromHttpRequest ( hr );

	if ( ! token ) {
		log("crawlbot: need token to add new coll");
		return NULL;
	}

	char *collBuf = getNewCollName ( );//token , tokenLen );

	if ( ! g_collectiondb.addRec ( collBuf ,
				       NULL ,  // copy from
				       0  , // copy from len
				       true , // it is a brand new one
				       -1 , // we are new, this is -1
				       false , // is NOT a dump
				       true // save it for sure!
				       ) )
		return NULL;

	// get the collrec
	CollectionRec *cr = g_collectiondb.getRec ( collBuf );

	// did an alloc fail?
	if ( ! cr ) { char *xx=NULL;*xx=0; }

	// normalize the seed url
	//Url norm;
	//norm.set ( seed );
	//cr->m_diffbotSeed.set ( norm.getUrl() );

	// remember the token
	cr->m_diffbotToken.set ( token );
	cr->m_diffbotToken.nullTerm();


	/* this stuff can be set later.

	cr->m_diffbotApi.set ( api );

	// these are optional, may be NULL
	cr->m_diffbotApiQueryString.set ( apiQueryString );
	cr->m_diffbotUrlCrawlPattern.set ( urlCrawlPattern );
	cr->m_diffbotUrlProcessPattern.set ( urlProcessPattern );
	cr->m_diffbotPageProcessPattern.set ( pageProcessPattern );
	cr->m_diffbotClassify = classify;

	// let's make these all NULL terminated strings
	cr->m_diffbotSeed.nullTerm();
	cr->m_diffbotApi.nullTerm();
	cr->m_diffbotApiQueryString.nullTerm();
	cr->m_diffbotUrlCrawlPattern.nullTerm();
	cr->m_diffbotUrlProcessPattern.nullTerm();
	cr->m_diffbotPageProcessPattern.nullTerm();
	*/



	// do not spider more than this many urls total. -1 means no max.
	cr->m_diffbotMaxToCrawl = 100000;
	// do not process more than this. -1 means no max.
	cr->m_diffbotMaxToProcess = 100000;

	// this collection should always hit diffbot
	//cr->m_useDiffbot = true;

	// show the ban links in the search results. the collection name
	// is cryptographic enough to show that
	cr->m_isCustomCrawl = true;

	// reset the crawl stats
	cr->m_diffbotCrawlStartTime = gettimeofdayInMillisecondsGlobal();
	cr->m_diffbotCrawlEndTime   = 0LL;

	// reset crawler stats. they should be loaded from crawlinfo.txt
	memset ( &cr->m_localCrawlInfo , 0 , sizeof(CrawlInfo) );
	memset ( &cr->m_globalCrawlInfo , 0 , sizeof(CrawlInfo) );
	//cr->m_globalCrawlInfoUpdateTime = 0;
	cr->m_replies = 0;
	cr->m_requests = 0;

	// support current web page api i guess for test crawls
	//cr->m_isDiffbotTestCrawl = false;
	//char *strange = hr->getString("href",NULL);
	//if ( strange && strcmp ( strange,"/dev/crawl#testCrawl" ) == 0 )
	//	cr->m_isDiffbotTestCrawl = true;

	///////
	//
	// extra diffbot ARTICLE parms
	//
	///////
	// . ppl mostly use meta, html and tags.
	// . dropping support for dontStripAds. mike is ok with that.
	// . use for jsonp requests. needed for cross-domain ajax.
	//char *callback = hr->getString("callback",NULL);
	// a download timeout
	//long timeout = hr->getLong("timeout",5000);
	// "xml" or "json"
	char *format = hr->getString("format",NULL,"json");

	// save that
	cr->m_diffbotFormat.safeStrcpy(format);

	// return all content from page? for frontpage api.
	// TODO: can we put "all" into "fields="?
	//bool all = hr->hasField("all");

	
	/////////
	//
	// specify diffbot fields to return in the json output
	//
	/////////
	// point to the safebuf that holds the fields the user wants to
	// extract from each url. comma separated list of supported diffbot
	// fields like "meta","tags", ...
	SafeBuf *f = &cr->m_diffbotFields;
	// transcribe provided fields if any
	char *fields = hr->getString("fields",NULL);
	// appends those to our field buf
	if ( fields ) f->safeStrcpy(fields);
	// if something there push a comma in case we add more below
	if ( f->length() ) f->pushChar(',');
	// return contents of the page's meta tags? twitter card metadata, ..
	if ( hr->hasField("meta"    ) ) f->safeStrcpy("meta,");
	if ( hr->hasField("html"    ) ) f->safeStrcpy("html,");
	if ( hr->hasField("tags"    ) ) f->safeStrcpy("tags,");
	if ( hr->hasField("comments") ) f->safeStrcpy("comments,");
	if ( hr->hasField("summary" ) ) f->safeStrcpy("summary,");
	if ( hr->hasField("all"     ) ) f->safeStrcpy("all,");
	// if we added crap to "fields" safebuf remove trailing comma
	f->removeLastChar(',');


	// set some defaults. max spiders for all priorities in this collection
	cr->m_maxNumSpiders = 10;

	// make the gigablast regex table just "default" so it does not
	// filtering, but accepts all urls. we will add code to pass the urls
	// through m_diffbotUrlCrawlPattern alternatively. if that itself
	// is empty, we will just restrict to the seed urls subdomain.
	for ( long i = 0 ; i < MAX_FILTERS ; i++ ) {
		cr->m_regExs[i].purge();
		cr->m_spiderPriorities[i] = 0;
		cr->m_maxSpidersPerRule [i] = 10;
		cr->m_spiderIpWaits     [i] = 250; // 250 ms for now
		cr->m_spiderIpMaxSpiders[i] = 10;
		cr->m_spidersEnabled    [i] = 1;
		cr->m_spiderFreqs       [i] = 7.0;
		cr->m_spiderDiffbotApiNum[i] = DBA_NONE;
	}


	// 
	// by default to not spider image or movie links or
	// links with /print/ in them
	//
	long i = 0;
	cr->m_regExs[i].safePrintf("isinjected");
	cr->m_spiderPriorities[i] = 49;
	i++;
	cr->m_regExs[i].safePrintf("ismedia");
	cr->m_spiderPriorities[i] = SPIDER_PRIORITY_FILTERED;
	i++;
	// if user did not specify a url crawl pattern then keep
	// the crawl limited to the same subdomain of the seed url
	//if ( cr->m_diffbotUrlCrawlPattern.length() == 0 ) {
	// first limit to http://subdomain
	cr->m_regExs[i].safePrintf("isonsamedomain");//^http://");
	//cr->m_regExs[i].safeMemcpy(norm.getHost(),norm.getHostLen());
	//cr->m_regExs[i].pushChar('/');
	cr->m_regExs[i].nullTerm();
	cr->m_spiderPriorities  [i] = 50;
	cr->m_maxSpidersPerRule [i] = 10;
	cr->m_spiderIpWaits     [i] = 250; // 500 ms for now
	cr->m_spiderIpMaxSpiders[i] = 10;
	cr->m_spidersEnabled    [i] = 1;
	i++;
	// and make all else filtered
	cr->m_regExs[i].safePrintf("default");
	cr->m_spiderPriorities  [i] = SPIDER_PRIORITY_FILTERED;
	cr->m_maxSpidersPerRule [i] = 10;
	cr->m_spiderIpWaits     [i] = 250; // 500 ms for now
	cr->m_spiderIpMaxSpiders[i] = 10;
	cr->m_spidersEnabled    [i] = 1;
	i++;

	// just the default rule!
	cr->m_numRegExs   = i;
	cr->m_numRegExs2  = i;
	cr->m_numRegExs3  = i;
	cr->m_numRegExs10 = i;
	cr->m_numRegExs11 = i;
	cr->m_numRegExs5  = i;
	cr->m_numRegExs6  = i;
	cr->m_numRegExs7  = i;

	//cr->m_spiderPriorities  [1] = -1; // filtered? or banned?
	//cr->m_maxSpidersPerRule [1] = 10;
	//cr->m_spiderIpWaits     [1] = 500; // 500 ms for now

	cr->m_needsSave = 1;


	// start the spiders!
	cr->m_spideringEnabled = true;

	return cr;
}

// just use "fakeips" based on the hash of each url hostname/subdomain
// so we don't waste time doing ip lookups.
bool getSpiderRequestMetaList ( char *doc , 
				SafeBuf *listBuf ,
				bool spiderLinks ) {
	// . scan the list of urls
	// . assume separated by white space \n \t or space
	char *p = doc;

	long now = getTimeGlobal();

	// a big loop
	while ( true ) {
		// skip white space (\0 is not a whitespace)
		for ( ; is_wspace_a(*p) ; p++ );
		// all done?
		if ( ! *p ) break;
		// save it
		char *saved = p;
		// advance to next white space
		for ( ; ! is_wspace_a(*p) && *p ; p++ );
		// set end
		char *end = p;
		// get that url
		Url url;
		url.set ( saved , end - saved );
		// if not legit skip
		if ( url.getUrlLen() <= 0 ) continue;
		// need this
		long long probDocId = g_titledb.getProbableDocId(&url);
		// make it
		SpiderRequest sreq;
		sreq.reset();
		sreq.m_firstIp = url.getHostHash32(); // fakeip!
		sreq.m_hostHash32 = url.getHostHash32();
		sreq.m_domHash32  = url.getDomainHash32();
		sreq.m_siteHash32 = url.getHostHash32();
		sreq.m_probDocId  = probDocId;
		sreq.m_hopCount   = 0; // we're a seed
		sreq.m_hopCountValid = true;
		sreq.m_addedTime = now;
		sreq.m_isNewOutlink = 1;
		sreq.m_isWWWSubdomain = url.isSimpleSubdomain();
		
		// treat seed urls as being on same domain and hostname
		sreq.m_sameDom = 1;
		sreq.m_sameHost = 1;
		sreq.m_sameSite = 1;

		sreq.m_fakeFirstIp = 1;
		sreq.m_isAddUrl = 1;

		// spider links?
		if ( ! spiderLinks )
			sreq.m_avoidSpiderLinks = 1;

		// save the url!
		strcpy ( sreq.m_url , url.getUrl() );
		// finally, we can set the key. isDel = false
		sreq.setKey ( sreq.m_firstIp , probDocId , false );
		// store rdbid first
		if ( ! listBuf->pushChar(RDB_SPIDERDB) )
			// return false with g_errno set
			return false;
		// store it
		if ( ! listBuf->safeMemcpy ( &sreq , sreq.getRecSize() ) )
			// return false with g_errno set
			return false;
	}
	// all done
	return true;
}


bool isAliasUnique ( CollectionRec *cr , char *token , char *alias ) {
	// scan all collections
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// must belong to us
		if ( strcmp(cx->m_diffbotToken.getBufStart(),token) )
			continue;
		// skip if collection we are putting alias on
		if ( cx == cr ) continue;
		// does it match?
		if ( cx->m_collectionNameAlias.length() <= 0 ) continue;
		// return false if it matches! not unique
		if ( strcmp ( cx->m_collectionNameAlias.getBufStart() ,
			      alias ) == 0 )
			return false;
	}
	return true;
}
