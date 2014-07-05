#include "gb-include.h"

//#include "GBVersion.h"
#include "Pages.h"
#include "Parms.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Tagdb.h"
#include "Categories.h"
#include "Proxy.h"
#include "PageParser.h" // g_inPageParser
#include "Users.h"
#include "Rebalance.h"

// a global class extern'd in Pages.h
Pages g_pages;
//const char *GBVersion;
// error message thingy used by HttpServer.cpp for logging purposes
char *g_msg;

/*
class WebPage {
 public:
	char  m_pageNum;  // see enum array below for this
	char *m_filename;
	long  m_flen;
	char *m_name;     // for printing the links to the pages in admin sect.
	bool  m_cast;     // broadcast input to all hosts?
	bool  m_usePost;  // use a POST request/reply instead of GET?
	                  // used because GET's input is limited to a few k.
	//char  m_perm;     // permissions, see USER_* #define's below
	char *m_desc; // page description
	bool (* m_function)(TcpSocket *s , HttpRequest *r);
	long  m_niceness;
};
*/

// . list of all dynamic pages, their path names, permissions and callback
//   functions that generate that page
// . IMPORTANT: these must be in the same order as the PAGE_* enum in Pages.h
//   otherwise you'll get a malformed error when running
static long s_numPages = 0;
static WebPage s_pages[] = {
	// dummy pages 
	{ PAGE_NOHOSTLINKS	, "nohostlinks",   0, "host links", 0, 0, 
	  "dummy page - if set in the users row then host links will not be "
	  " shown",
	  NULL, 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_ADMIN           , "colladmin",   0, "master=0", 0, 0,
	  "dummy page - if set in the users row then user will have master=0 and "
	  " collection links will be highlighted in red",
	  NULL, 0 ,NULL,NULL,PG_NOAPI},  



	//{ PAGE_QUALITY         , "quality",     0, "quality",  0, 0,
	//  "dummy page - if set in the users row then  \"Quality Control\""
	//  " will be printed besides the logo for certain pages",
	//  NULL, 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_PUBLIC   	, "public",   0, "public", 0, 0,
	  "dummy page - if set in the users row then page function is"
	  " called directly and not through g_parms.setFromRequest", 
	  NULL, 0 ,NULL,NULL,PG_NOAPI},
 	
	// publicly accessible pages
	{ PAGE_ROOT      , "index.html"    , 0 , "root" , 0 , 0 ,
	  "search page to query",
	  sendPageRoot   , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_RESULTS   , "search"        , 0 , "search" , 0 , 0 ,
	  "results page",
	  sendPageResults, 0 ,NULL,NULL,0},
	//{ PAGE_WIDGET   , "widget"        , 0 , "widget" , 0 , 0 ,
	//  "widget page",
	//  sendPageWidget, 0 ,NULL,NULL,PG_NOAPI},

	// this is the public addurl, /addurl, if you are using the 
	// api use PAGE_ADDURL2 which is /admin/addurl. so we set PG_NOAPI here
	{ PAGE_ADDURL    , "addurl"       , 0 , "add url" , 0 , 0 ,
	  "Page where you can add url for spidering",
	  sendPageAddUrl, 0 ,NULL,NULL,PG_NOAPI},

	{ PAGE_GET       , "get"           , 0 , "get" ,  0 , 0 ,
	  //USER_PUBLIC | USER_MASTER | USER_ADMIN | USER_CLIENT, 
	  "gets cached url",
	  sendPageGet  , 0 ,NULL,NULL,0},
	{ PAGE_LOGIN     , "login"         , 0 , "login" ,  0 , 0 ,
	  //USER_PUBLIC | USER_MASTER | USER_ADMIN | USER_SPAM | USER_CLIENT, 
	 "login page",
	 sendPageLogin, 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_DIRECTORY , "dir"           , 0 , "directory" , 0 , 0 ,
	  //USER_PUBLIC | USER_MASTER | USER_ADMIN | USER_CLIENT, 
	  "directory page",
	  sendPageDirectory , 0 ,NULL,NULL,0},
	{ PAGE_REPORTSPAM , "reportspam"   , 0 , "report spam" , 0 , 0 ,
	  //USER_PUBLIC | USER_MASTER | USER_ADMIN |  USER_PROXY | USER_CLIENT, 
	  "report spam page",
	  sendPageReportSpam , 0 ,NULL,NULL,PG_NOAPI},
	//{ PAGE_WORDVECTOR, "vec"           , 0 , "word vectors" , 0 , 1 ,
	//  //USER_PUBLIC | USER_MASTER | USER_ADMIN , 
	//  "word vectors page",
	//  sendPageWordVec , 0 ,NULL,NULL,PG_NOAPI},

	// use post now for the "site list" which can be big
	{ PAGE_BASIC_SETTINGS, "admin/settings", 0 , "settings",1, M_POST , 
	  "Basic settings page.", sendPageGeneric , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_BASIC_STATUS, "admin/status", 0 , "status",1, 0 , 
	  "Basic status page.", sendPageBasicStatus  , 0 ,NULL,NULL,0},
	//{ PAGE_BASIC_DIFFBOT, "admin/diffbot", 0 , "diffbot",1, 0 , 
	//  "Basic diffbot page.",  sendPageBasicDiffbot  , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_BASIC_SECURITY, "admin/security", 0 , "security",1, 0 , 
	  "Basic security page.", sendPageGeneric  , 0 ,NULL,NULL,0},
	{ PAGE_BASIC_SEARCH, "", 0 , "search",1, 0 , 
	  "Basic search page.", sendPageRoot  , 0 ,NULL,NULL,PG_NOAPI},



	{ PAGE_MASTER    , "admin/master"  , 0 , "master controls" ,  1 , 0 , 
	  //USER_MASTER | USER_PROXY ,
	  "master controls page",
	  sendPageGeneric  , 0 ,NULL,NULL,0},
	// use POST for html head/tail and page root html. might be large.
	{ PAGE_SEARCH    , "admin/search"   , 0 , "search controls" ,1,M_POST,
	  //USER_ADMIN | USER_MASTER   , 
	  "search controls page",
	  sendPageGeneric  , 0 ,NULL,NULL,0},
	// use post now for the "site list" which can be big
	{ PAGE_SPIDER    , "admin/spider"   , 0 , "spider controls" ,1,M_POST,
	  //USER_ADMIN | USER_MASTER | USER_PROXY   ,
	  "spider controls page",
	  sendPageGeneric  , 0 ,NULL,NULL,0},
	{ PAGE_LOG       , "admin/log"     , 0 , "log controls"     ,  1 , 0 ,
	  //USER_MASTER | USER_PROXY,
	  "log page",
	  sendPageGeneric  , 0 ,NULL,NULL,0},
	{ PAGE_SECURITY, "admin/security2", 0 , "security"     ,  1 , 0 ,
	  //USER_MASTER | USER_PROXY ,
	  "advanced security page",
	  sendPageGeneric , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_ADDCOLL   , "admin/addcoll" , 0 , "add collection"  ,  1 , 0 ,
	  //USER_MASTER , 
	  "add a new collection using this page",
	  sendPageAddColl  , 0 ,NULL,NULL,0},
	{ PAGE_DELCOLL   , "admin/delcoll" , 0 , "delete collections" ,  1 ,0,
	  //USER_MASTER , 
	  "delete a collection using this page",
	  sendPageDelColl  , 0 ,NULL,NULL,0},
	{ PAGE_REPAIR    , "admin/repair"   , 0 , "repair" ,  1 , 0 ,
	  //USER_MASTER ,
	  "repair page",
	  sendPageGeneric   , 0 ,NULL,NULL,0},
	// { PAGE_SITES   , "admin/sites", 0 , "site list" ,  1 , 1,
	//   "what sites can be spidered",
	//   sendPageGeneric , 0 ,NULL,NULL,PG_NOAPI}, // sendPageBasicSettings
	{ PAGE_FILTERS   , "admin/filters", 0 , "url filters" ,  1 ,M_POST,
	  //USER_ADMIN | USER_MASTER   , 
	  "prioritize urls for spidering",
	  sendPageGeneric  , 0 ,NULL,NULL,0},
	{ PAGE_INJECT    , "admin/inject"   , 0 , "inject url" , 0,M_MULTI ,
	  //USER_ADMIN | USER_MASTER   ,
	  "inject url in the index here",
	  sendPageInject   , 2 } ,
	// this is the addurl page the the admin!
	{ PAGE_ADDURL2   , "admin/addurl"   , 0 , "add urls" ,  0 , 0 ,
	  "add url page for admin",
	  sendPageAddUrl2   , 0 ,NULL,NULL,0},
	{ PAGE_REINDEX   , "admin/reindex"  , 0 , "query reindex" ,  0 , 0 ,
	  //USER_ADMIN | USER_MASTER, 
	  "reindex url page",
	  sendPageReindex  , 0 ,NULL,NULL,0},





	{ PAGE_HOSTS     , "admin/hosts"   , 0 , "hosts" ,  0 , 0 ,
	  //USER_MASTER | USER_PROXY,
	  "hosts page",
	  sendPageHosts    , 0 ,NULL,NULL,0},

	// master admin pages
	{ PAGE_STATS     , "admin/stats"   , 0 , "stats" ,  0 , 0 ,
	  //USER_MASTER | USER_PROXY , 
	  "statistics page",
	  sendPageStats    , 0 ,NULL,NULL,0},

	{ PAGE_STATSDB , "admin/statsdb"  , 0 , "graph"  ,  0 , 0 ,
	  //USER_MASTER , 
	  "statistics page",
	  sendPageStatsdb  , 2 /*niceness*/ ,NULL,NULL,0},

	{ PAGE_PERF      , "admin/perf"    , 0 , "performance"     ,  0 , 0 ,
	  //USER_MASTER | USER_PROXY ,
	  "master performance page",
	  sendPagePerf     , 0 ,NULL,NULL,0},

	{ PAGE_SOCKETS   , "admin/sockets" , 0 , "sockets" ,  0 , 0 ,
	  //USER_MASTER | USER_PROXY,
	  "sockets page",
	  sendPageSockets  , 0 ,NULL,NULL,0},

	{ PAGE_LOGVIEW    , "admin/logview"   , 0 , "log view" ,  0 , 0 ,
	  //USER_MASTER ,  
	  "logview page",
	  sendPageLogView  , 0 ,NULL,NULL,0},
//	{ PAGE_SYNC      , "master/sync"    , 0 , "sync"            ,  0 , 0 ,
//	  //USER_MASTER , 
//	  "sync page",
//	  sendPageGeneric  , 0 ,NULL,NULL,PG_NOAPI},

	{ PAGE_AUTOBAN    ,"admin/autoban" , 0 , "autoban" ,  1 , M_POST ,
	  //USER_MASTER | USER_PROXY , 
	  "autobanned ips",
	  sendPageAutoban   , 0 ,NULL,NULL,0},
	  /*
	{ PAGE_SPIDERLOCKS,"admin/spiderlocks" , 0 , "spider locks" ,  0 , 0 ,
	  USER_MASTER , sendPageSpiderLocks , 0 ,NULL,NULL,PG_NOAPI},
	  */
	{ PAGE_PROFILER    , "admin/profiler"   , 0 , "profiler" ,  0 ,M_POST,
	  //USER_MASTER , 
	  "profiler page",
	  sendPageProfiler   , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_THREADS    , "admin/threads"   , 0 , "threads" ,  0 , 0 ,
	  //USER_MASTER ,
	  "threads page",
	  sendPageThreads  , 0 ,NULL,NULL,0},
	//{ PAGE_THESAURUS, "admin/thesaurus",    0 , "thesaurus", 0 , 0 ,
        //  //USER_MASTER ,
	//  "thesaurus page",
	//  sendPageThesaurus , 0 ,NULL,NULL,PG_NOAPI},


	// collection admin pages
	//{ PAGE_OVERVIEW , "admin/overview"     , 0 , "overview" ,  0 , 0,
	//  //USER_MASTER | USER_ADMIN ,
	//  "overview page",
	//  sendPageOverview  , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_API , "admin/api"         , 0 , "api" , 0 , 0 ,
	  //USER_MASTER | USER_ADMIN , 
	  "api page",
	  sendPageAPI , 0 ,NULL,NULL,0},
	{ PAGE_RULES  , "admin/siterules", 0 , "site rules", 1, M_POST,
	  //USER_ADMIN | USER_MASTER   , 
	  "site rules page",
	  sendPageGeneric , 0,NULL,NULL,PG_NOAPI},
	{ PAGE_INDEXDB   , "admin/indexdb" , 0 , "indexdb"         ,  0 , 0,
	  //USER_MASTER ,
	  "indexdb page",
	  sendPageIndexdb  , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_TITLEDB   , "admin/titledb" , 0 , "titledb"         ,  0 , 0,
	  //USER_MASTER , 
	  "titledb page",
	  sendPageTitledb  , 2,NULL,NULL,PG_NOAPI},
	// 1 = usePost

	{ PAGE_CRAWLBOT    , "crawlbot"   , 0 , "crawlbot" ,  1 , 0,
	  "simplified spider controls page",
	  sendPageCrawlbot , 0 ,NULL,NULL,PG_NOAPI},

	{ PAGE_SPIDERDB  , "admin/spiderdb" , 0 , "spider queue" ,  0 , 0 ,
	  //USER_ADMIN | USER_MASTER   , 
	  "spiderdb page",
	  sendPageSpiderdb , 0 ,NULL,NULL,0},
	//{ PAGE_PRIORITIES, "admin/priorities"  , 0 , "priority controls",1,1,
	//  //USER_ADMIN | USER_MASTER   , 
	//  "spider priorities page",
	//  sendPageGeneric  , 0 ,NULL,NULL,PG_NOAPI},

	//{ PAGE_KEYWORDS, "admin/queries",0,"queries" ,  0 , 1 ,
	//  "get queries a url matches",
	//  sendPageMatchingQueries   , 2 } ,

#ifndef CYGWIN
	{ PAGE_SEO, "seo",0,"seo" ,  0 , 0 ,
	  "SEO info",
	  sendPageSEO   , 2 ,NULL,NULL,PG_NOAPI},
#else
	{ PAGE_SEO, "seo",0,"seo" ,  0 , 0 ,
	  "SEO info",
	  sendPageResults  , 0 ,NULL,NULL,PG_NOAPI},
#endif

	{ PAGE_ACCESS    , "admin/access" , 0 , "access" ,  1 , M_POST,
	  //USER_ADMIN | USER_MASTER   , 
	  "access password, ip, admin ips etc. all goes in here",
	  sendPageGeneric  , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_SEARCHBOX , "admin/searchbox", 0 , "search" ,  0 , 0 ,
	  //USER_ADMIN | USER_MASTER   , 
	  "search box",
	  sendPageResults  , 0 ,NULL,NULL,PG_NOAPI},
	{ PAGE_PARSER    , "admin/parser"  , 0 , "parser"          , 0,M_POST,
	  //USER_MASTER ,
	  "page parser page",
	  sendPageParser   , 2 ,NULL,NULL,PG_NOAPI},
	{ PAGE_SITEDB    , "admin/tagdb"  , 0 , "tagdb"  ,  0 , M_POST,
	  //USER_MASTER | USER_ADMIN,
	  "tagdb page to add/remove/get tags",
	  sendPageTagdb ,  0 ,NULL,NULL,0},	  
	{ PAGE_CATDB     , "admin/catdb"   , 0 , "catdb"           ,  0,M_POST,
	  //USER_MASTER | USER_ADMIN,
	  "catdb page",
	  sendPageCatdb    , 0 ,NULL,NULL,PG_NOAPI},
	//{ PAGE_LOGIN2    , "admin/login"         , 0 , "login" ,  0 , 0,
	//  //USER_PUBLIC | USER_MASTER | USER_ADMIN | USER_SPAM | USER_CLIENT, 
	//"login link - also logoffs user",
	//  sendPageLogin,0}
//	{ PAGE_TOPDOCS , "admin/topdocs"  , 0 , "top docs" ,  1 , 1 ,
//	  //USER_ADMIN | USER_MASTER, 
//	  "top documents page",
//	  sendPageTopDocs , 0 ,NULL,NULL,PG_NOAPI},
// 	{ PAGE_TOPICS    , "admin/topics"   , 0 , "topics" ,  0 , 1 ,
// 	  USER_ADMIN | USER_MASTER , sendPageTopics   , 0 ,NULL,NULL,PG_NOAPI},
// 	{ PAGE_SPAM    , "admin/spam"   , 0 , "spam weights" ,  1 , 1 ,
// 	  USER_ADMIN | USER_MASTER , sendPageSpam , 0  ,NULL,NULL,PG_NOAPI},
	//{ PAGE_QAGENT    , "admin/qagent"   , 0 , "quality agent" ,  1 , 1 ,
	//  //USER_ADMIN | USER_MASTER ,
	//  "quality agent",
	//  sendPageQualityAgent, 2 ,NULL,NULL,PG_NOAPI},
	// MDW: take out for now since we are fully split and don't need
	// network to transport termlists any more
	//{ PAGE_NETTEST , "admin/nettest"  , 0 , "net test" ,  1 , 1 ,
	//  //USER_ADMIN | USER_MASTER,
	//  "net test",
	//  sendPageNetTest , 0 ,NULL,NULL,PG_NOAPI},
	//{ PAGE_ADFEED  , "admin/adfeed"  , 0 , "ad feed" ,  1 , 1 ,
	//  //USER_ADMIN | USER_MASTER,
	//  "ad feed control page",
	//  sendPageGeneric , 0 ,NULL,NULL,PG_NOAPI},
 	//{ PAGE_TURK2    , "pageturkhome"       , 0 , "page turk" , 0 , 0 ,
	//  "page turk home page",
        //  sendPageTurkHome, 0 }
};

WebPage *Pages::getPage ( long page ) {
	return &s_pages[page];
}

char *Pages::getPath ( long page ) { 
	return s_pages[page].m_filename; 
}

long Pages::getNumPages(){
	return s_numPages;
}

void Pages::init ( ) {
	// array of dynamic page descriptions
	s_numPages = sizeof(s_pages) / sizeof(WebPage);
	// sanity check, ensure PAGE_* corresponds to position
	for ( long i = 0 ; i < s_numPages ; i++ ) 
		if ( s_pages[i].m_pageNum != i ) {
			log(LOG_LOGIC,"conf: Bad engineer. WebPage array is "
			    "malformed. It must be 1-1 with the "
			    "WebPage enum in Pages.h.");
			char *xx=NULL;*xx=0;
			//exit ( -1 );
		}
	// set the m_flen member
	for ( long i = 0 ; i < s_numPages ; i++ ) 
		s_pages[i].m_flen = gbstrlen ( s_pages[i].m_filename );
}

// Used by Users.cpp to get PAGE_* from the given filename
long Pages::getPageNumber ( char *filename ){
	//
	static bool s_init = false;
	static char s_buff[8192];
	static HashTableX s_ht;
	if ( !s_init ){
		s_ht.set(8,4,256,s_buff,8192,false,0,"pgnummap");
		for ( long i=0; i < PAGE_NONE; i++ ){
			if ( ! s_pages[i].m_filename  ) continue;
			if (   s_pages[i].m_flen <= 0 ) continue;
			long long pageHash = hash64( s_pages[i].m_filename,
			                             s_pages[i].m_flen    );
			if ( ! s_ht.addKey(&pageHash,&i) ){
				char *xx = NULL; *xx = 0;
			}
		}
		s_init = true;
		// make sure stay in s_buff
		if ( s_ht.m_buf != s_buff ) { char *xx=NULL;*xx=0; }
	}
	long long pageHash = hash64(filename,gbstrlen(filename));
	long slot = s_ht.getSlot(&pageHash);
	if ( slot== -1 )  return -1;
	long value = *(long *)s_ht.getValueFromSlot(slot);
	
	return value;
}

// return the PAGE_* number thingy
long Pages::getDynamicPageNumber ( HttpRequest *r ) {
	char *path    = r->getFilename();
	long  pathLen = r->getFilenameLen();
	if ( pathLen > 0 && path[0]=='/' ) { path++; pathLen--; }
	// historical backwards compatibility fix
	if ( pathLen == 9 && strncmp ( path , "cgi/0.cgi" , 9 ) == 0 ) {
		path = "search"; pathLen = gbstrlen(path); }
	if ( pathLen == 9 && strncmp ( path , "cgi/1.cgi" , 9 ) == 0 ) {
		path = "addurl"; pathLen = gbstrlen(path); }
	if ( pathLen == 6 && strncmp ( path , "inject" , 6 ) == 0 ) {
		path = "admin/inject"; pathLen = gbstrlen(path); }
	if ( pathLen == 9 && strncmp ( path , "index.php" , 9 ) == 0 ) {
		path = "search"; pathLen = gbstrlen(path); }
	if ( pathLen == 10 && strncmp ( path , "search.csv" , 10 ) == 0 ) {
		path = "search"; pathLen = gbstrlen(path); }

	// if it is like /GA/Atlanta then call sendPageResults
	// and that should be smart enough to set the m_where in
	// SearchInput.cpp from the path!!
	if ( path && 
	     // "filename" does not start with '/' for some reason
	     //path[0] &&
	     //path[0] == '/' &&
	     path[0] &&
	     is_alpha_a(path[0]) &&
	     is_alpha_a(path[1]) &&
	     pathLen<64 &&
	     // "GET /NM"
	     (path[2] == '/' || path[2]=='\0' || path[2]==' ') )
		return PAGE_RESULTS;

	// go down the list comparing the pathname to dynamic page names
	for ( long i = 0 ; i < s_numPages ; i++ ) {
		if ( pathLen != s_pages[i].m_flen ) continue;
		if ( strncmp ( path , s_pages[i].m_filename , pathLen ) == 0 )
			return i;
	}
	// check to see if the path is a category
	path    = r->getPath();
	pathLen = r->getPathLen();
	// truncate if we would breech
	if ( pathLen >= MAX_HTTP_FILENAME_LEN )
		pathLen = MAX_HTTP_FILENAME_LEN - 1;
	// decode the path
	char decodedPath[MAX_HTTP_FILENAME_LEN];
	long decodedPathLen = urlDecode(decodedPath, path, pathLen);
	// remove cgi
	for (long i = 0; i < decodedPathLen; i++) {
		if (decodedPath[i] == '?') {
			decodedPathLen = i;
			break;
		}
	}
	// sanity
	if ( ! g_categories ) log("process: no categories loaded");

	//
	// dmoz - look it up for a category
	//
	if ( g_categories &&
	     g_categories->getIndexFromPath(decodedPath, decodedPathLen) >= 0)
		return PAGE_DIRECTORY;
	// just go to PAGE_DIRECTORY for other request
	//return PAGE_DIRECTORY;
	// not found in our list of dynamic page filenames
	return -1;
}

// once all hosts have received the parms, or we've at least tried to send
// them to all hosts, then come here to return the page content back to
// the client browser
void doneBroadcastingParms ( void *state ) {
	TcpSocket *sock = (TcpSocket *)state;
	// free this mem
	sock->m_handyBuf.purge();
	// set another http request again
	HttpRequest r;
	//bool status = r.set ( sock->m_readBuf , sock->m_readOffset , sock ) ;
	r.set ( sock->m_readBuf , sock->m_readOffset , sock ) ;
	// we stored the page # below
	WebPage *pg = &s_pages[sock->m_pageNum];
	// call the page specifc function which will send data back on socket
	pg->m_function ( sock , &r );
}

// . returns false if blocked, true otherwise
// . send an error page on error
bool Pages::sendDynamicReply ( TcpSocket *s , HttpRequest *r , long page ) {
	// error out if page number out of range
	if ( page < PAGE_ROOT || page >= s_numPages ) 
		return g_httpServer.sendErrorReply ( s , 505 , "Bad Request");

	// map root page to results page for event searching
	//if ( page == PAGE_ROOT ) {
	//	char *coll = r->getString("c");
	//	// ensure it exists
	//	CollectionRec *cr = g_collectiondb.getRec ( coll );
	//	if ( cr && cr->m_indexEventsOnly ) page = PAGE_RESULTS;
	//}

	// did they supply correct password for given username?
	//bool userAccess = g_users.verifyUser(s,r);

	// does public have permission?
	bool publicPage = false;
	if ( page == PAGE_ROOT ) publicPage = true;
	// do not deny /NM/Albuquerque urls
	if ( page == PAGE_RESULTS ) publicPage = true;
	if ( page == PAGE_SEO ) publicPage = true;
	if ( page == PAGE_ADDURL ) publicPage = true;
	if ( page == PAGE_GET ) publicPage = true;
	if ( page == PAGE_CRAWLBOT ) publicPage = true;

	// get our host
	//Host *h = g_hostdb.m_myHost;

	// now use this...
	bool isAdmin = g_conf.isRootAdmin ( s , r );

	////////////////////
	////////////////////
	//
	// if it is an administrative page it requires permission!
	//
	////////////////////
	////////////////////
	if ( ! publicPage && ! isAdmin )
		return sendPageLogin ( s , r );

	if ( page == PAGE_CRAWLBOT && ! isAdmin )
		log("pages: accessing a crawlbot page without admin privs. "
		    "no parms can be changed.");

	/*
	// is request coming from a local ip?
	bool isLocal = false;
	bool isLoopback = false;
	if ( iptop(s->m_ip) == iptop(h->m_ip       ) ) isLocal = true;
	if ( iptop(s->m_ip) == iptop(h->m_ipShotgun) ) isLocal = true;
        // shortcut
        uint8_t *p = (uint8_t *)&s->m_ip;
	// 127.0.0.1
	if ( s->m_ip == 16777343 ) { isLocal = true; isLoopback = true; }
	// 127 is local
	if ( g_conf.isConnectIp ( s->m_ip ) ) isLocal = true;
	// try this too so steve's comcast home ip works
	if ( r->isLocal() ) isLocal = true;
	// don't treat the ones below as local any more because we might
	// be a compression proxy running on a dedicated server and we do
	// not want other customers on that network to hit us! if you want
	// to access it from your browser then stick your tunnel's IP into
	// the <connectIp> list in gb.conf.

	// crap, but for now zak and partap need to be able to hit the
	// machines, so at least allow or 10.* addresses through, usually
	// the dedicates hosts are 192.168.*.*
        // this is local
	if ( p[0] == 10 ) isLocal = true;
        // this is local
	//if ( p[0] == 192 && p[1] == 168 ) isLocal = true;

	bool forbidIp = false;
	if ( ! publicPage && ! isLocal ) forbidIp = true;
	// allow zak though so he can add tags using tagdb to docid/eventIds
	// no, zak should be hitting the spider compression proxy or whatever,
	// even so, we should add zak's ips to the security page of
	// connect ips at least... i don't want to override this check because
	// it is our biggest security point
	//if ( page == PAGE_SITEDB  ) forbidIp = false;
	//if ( page == PAGE_LOGIN   ) forbidIp = false;
	//if ( page == PAGE_INJECT  ) forbidIp = false;
	//if ( page == PAGE_REINDEX ) forbidIp = false;
	//if ( page == PAGE_ROOT    ) forbidIp = false;
	//if ( page == PAGE_RESULTS ) forbidIp = false;
	//if ( page == PAGE_GET     ) forbidIp = false;
	*/

	// if the page is restricted access then they must be coming from
	// an internal ip. our ip masked with 0xffff0000 is good. we assume
	// that all administrators tunnel in through router0 and thus get a
	// local ip.
	// PAGE_TAGDB: allow zak to access tagdb, etc. 
	/*
	if ( forbidIp ) {
		log("admin: must admin from internal ip"); 
		log("login: access denied 1 from ip=%s",iptoa(s->m_ip));
		return sendPageLogin( s, r, "Access Denied. IP not local.");
		//		      "in list of connect ips on security "
		//		      "tab.");
	}
	*/

	// . does client have permission for this page? they are coming from
	//   an internal ip and they provided the correct password for their
	//   username (or the page is publically accessible)
	// . BUT allow anyone to see it regardless if page is public! because
	//   often times my cookie says username=mwells but i am not logged
	//   in and i don't want to type my password to see the root page,
	//   or any other public page
	//if ( ! publicPage && ! g_users.hasPermission( r, page , s ) &&
	//     ! isLoopback ) {
	//	log("login: access denied 2 from ip=%s",iptoa(s->m_ip));
	//	return sendPageLogin ( s , r, "Access Denied. No permission.");
	//}
	//if ( ! publicPage && ! userAccess && ! isLoopback ) {
	//	log("login: access denied 3 from ip=%s",iptoa(s->m_ip));
	//	return sendPageLogin(s,r,"Access Denied. Bad or no password.");
	//}
	//if ( ! publicPage && ! isLocal && ! isLoopback ) {
	//	log("login: access denied 2 from ip=%s",iptoa(s->m_ip));
	//	return sendPageLogin ( s , r, "Access Denied. No permission.");
	//}

	g_errno = 0;

	WebPage *pg = &s_pages[page];

	// now we require a username for all "admin" type pages
	/*bool  pub = pg->m_perm & USER_PUBLIC;
	if ( ! pub ) {
		// just get from cookie so it is not broadcast over the web
		// via a referral url
		char *username = r->getStringFromCookie("username");
		// if it is a broadcast, get from request directly (Msg28.cpp)
		if ( ! username ) username = r->getString("username");
		if ( ! username ) {
			log("admin: Permission denied. You must supply a "
			    "username.");
			return sendPageLogin ( s , r );
		}
	}*/
	//g_errno = 0;
  
	// page parser is now niceness 2 and calls a bunch of functions
	// with niceness 2, so if we allow another to be launched we risk
	// a quick poll within a quickpoll. we assume all http request handlers
	// are niceness 0, except this one.
	// 	if ( g_loop.m_inQuickPoll && 
	// 	     // even if not in page parser, we could be in a quickpoll 
	// 	     // and that messes us up enough
	// 	     //g_inPageParser &&
	// 	     pg->m_function == sendPageParser ) {
	// 		g_errno = ETRYAGAIN;
	// 		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));
	// 	}


	// get safebuf stored in TcpSocket class
	SafeBuf *parmList = &s->m_handyBuf;

	// chuck this in there
	s->m_pageNum = page;

	////////
	//
	// the new way to set and distribute parm settings
	//
	////////
	
	// . convert http request to list of parmdb records
	// . will only add parm recs we have permission to modify
	// . if no collection supplied will just return true with no g_errno
	if ( isAdmin &&
	     ! g_parms.convertHttpRequestToParmList ( r , parmList , page ) )
		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));
		

	// . add parmList using Parms::m_msg4 to all hosts!
	// . returns true and sets g_errno on error
	// . returns false if would block
	// . just returns true if parmList is empty
	// . so then doneBroadcastingParms() is called when all hosts
	//   have received the updated parms, unless a host is dead,
	//   in which case he should sync up when he comes back up
	if ( isAdmin &&
	     ! g_parms.broadcastParmList ( parmList , 
					   s , // state is socket i guess
					   doneBroadcastingParms ) )
		// this would block, so return false
		return false;

	// free the mem if we didn't block
	s->m_handyBuf.purge();

	// on error from broadcast, bail here
	if ( g_errno )
		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));

	// if this is a save & exit request we must log it here because it
	// will never return in order to log it in HttpServer.cpp
	// TODO: make this a function we can call.
	if ( g_conf.m_logHttpRequests && page == PAGE_MASTER ) { 
		//&& pg->m_function==CommandSaveAndExit ) {
		// get time format: 7/23/1971 10:45:32
		time_t tt ;//= getTimeGlobal();
		if ( isClockInSync() ) tt = getTimeGlobal();
		else                   tt = getTimeLocal();
		struct tm *timeStruct = localtime ( &tt );
		char buf[64];
		strftime ( buf , 100 , "%b %d %T", timeStruct);
		// what url refered user to this one?
		char *ref = r->getReferer();
		// skip over http:// in the referer
		if ( strncasecmp ( ref , "http://" , 7 ) == 0 ) ref += 7;
		// save ip in case "s" gets destroyed
		long ip = s->m_ip;
		logf (LOG_INFO,"http: %s %s %s %s %s",
		      buf,iptoa(ip),r->getRequest(),ref,
		      r->getUserAgent());
	}

	// if we did not block... maybe there were no parms to broadcast
	return pg->m_function ( s , r );

	/*

	// broadcast request to ALL hosts if we should
	// should this request be broadcasted?
	long cast = r->getLong("cast",-1) ;

	// 0 is the default
	// UNLESS we are the crawlbot page, john does not send a &cast=1
	// on his requests and they LIKELY need to go to each host in the 
	// network like for adding/deleting/resetting collections and updating
	// coll parms like "alias" and "maxtocrawl" and "maxtoprocess"
	if ( cast == -1 ) {
		cast = 0;
		if ( page == PAGE_CRAWLBOT ) cast = 1;
	}
	*/
	// proxy can only handle certain pages. it has logic in Proxy.cpp
	// to use the 0xfd msg type to forward certain page requests to 
	// host #0, like 
	// PAGE_ROOT
	// PAGE_GET
	// PAGE_RESULTS
	// PAGE_INJECT
	// PAGE_REINDEX
	// PAGE_DIRECTORY
	// PAGE_ADDURL
	// so all other pages should be nixed by us here.. unless its
	// page admin or page master because we need those to adminster
	// the proxy..
	/*
	if ( page != PAGE_ROOT      &&
	     page != PAGE_GET       &&
	     page != PAGE_RESULTS   &&
	     page != PAGE_INJECT    &&
	     page != PAGE_REINDEX   &&
	     page != PAGE_DIRECTORY &&
	     page != PAGE_ADDURL    &&
	     page != PAGE_MASTER    &&
	     page != PAGE_ADMIN ) {
		log("admin: page %s not allowed through proxy",pg->m_name );
		g_errno = EBADENGINEER;
		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));
	}
	*/

	//but if we're a proxy don't broadcast
	//if ( userType == USER_PROXY )
	//if ( g_proxy.isProxyRunning() && 
	//   (g_conf.isMasterAdmin( s, r ) || g_hostdb.getProxyByIp(s->m_ip)) )
	//	cast = false;
	/*
	if ( g_proxy.isProxy () ) cast = 0;
	// this only returns true on error. uses msg28 to send the http request
	// verbatim to all hosts in network, using tcpserver. the spawned msg28
	// requests will come through this same path and be identical to this request
	// but their cast will be "0" this time to break any recursion.
	if ( cast ) if ( ! broadcastRequest ( s , r , page ) ) return false;
	// on error from broadcast, bail here, it call sendErrorReply()
	if ( g_errno )
		return g_httpServer.sendErrorReply(s,505,mstrerror(g_errno));
	// how did this happen?
	if ( cast && ! g_errno ) {
		log(LOG_LOGIC,"admin: broadcast did not block or have error.");
		return true;
	}
	// . if no collection specified, and page depends on collection, error
	// . allow some pages to use default if no collection explicitly given
	if ( page > PAGE_OVERVIEW && page != PAGE_TITLEDB &&
	     // crawlbot page might just have a token
	     page != PAGE_CRAWLBOT) {
		char *coll = r->getString("c");
		// ensure it exists
		CollectionRec *cr = g_collectiondb.getRec ( coll );
		if ( ! cr ) {
			if ( ! coll ) coll = "";
			log("admin: Invalid collection \"%s\".",coll);
			return g_httpServer.sendErrorReply(s,505,"No "
							  "collection given.");
		}
	}

	// if this is a save & exit request we must log it here because it
	// will never return in order to log it in HttpServer.cpp
	if ( g_conf.m_logHttpRequests && page == PAGE_MASTER ) { 
		//&& pg->m_function==CommandSaveAndExit ) {
		// get time format: 7/23/1971 10:45:32
		time_t tt ;//= getTimeGlobal();
		if ( isClockInSync() ) tt = getTimeGlobal();
		else                   tt = getTimeLocal();
		struct tm *timeStruct = localtime ( &tt );
		char buf[64];
		strftime ( buf , 100 , "%b %d %T", timeStruct);
		// what url refered user to this one?
		char *ref = r->getReferer();
		// skip over http:// in the referer
		if ( strncasecmp ( ref , "http://" , 7 ) == 0 ) ref += 7;
		// save ip in case "s" gets destroyed
		long ip = s->m_ip;
		logf (LOG_INFO,"http: %s %s %s %s %s",
		      buf,iptoa(ip),r->getRequest(),ref,
		      r->getUserAgent());
	}
	// . we did not have a broadcast, config this host
	// . this also calls command functions like CommandJustSave()
	// . commandJustSave and commandJustSaveAndExit has to block
	// . now, so it can be responsible for calling pg->m_function
	//if ( userType > USER_PUBLIC ) {
	// check if user has public page access 
	
	if ( isLocal ) { //g_users.hasPermission( r, page , s )){
		// . this will set various parms
		// . we know the request came from a host in the cluster
		//   because "isHost" is true.
		// . this will call CmdJustSave(), etc. too if need be
		// . this calls the callback pg->m_function() when done!
		// . if there was a &cast=1 it was have left up above so we
		//   know that this is a &cast=0 request and an endpoint host.
		if(!g_parms.setFromRequest ( r , 
					     //userType, 
					     s,
					     pg->m_function))
			return false;
	}

	// do not call sendPageEvents if not eventwidget
	//if ( page == PAGE_RESULTS &&
	//     ststr ( hostname, "eventwidget.com" ) )
	//	return sendPageEvents ( s , r );
	//if ( page == PAGE_ADDEVENT &&
	//     ststr ( hostname, "eventwidget.com" ) )
	//	return sendPageAddEvent2 ( s , r );
	
	

	// . these functions MUST always call g_httpServer.sendDynamicPage()
	//   eventually
	// . returns false if blocked, true otherwise
	// . sets g_errno on error i think
	// . false means not called from msg28
	return pg->m_function ( s , r );
	*/
}

/*
#include "Msg28.h"
static Msg28        s_msg28;
static TcpSocket   *s_s;
static HttpRequest  s_r;
static bool         s_locked = false;
static long         s_page;

static void doneWrapper ( void *state ) ;

// . all dynamic page requests should call this
// . returns false if blocked, true otherwise,
// . sets g_errno on error
bool Pages::broadcastRequest ( TcpSocket *s , HttpRequest *r , long page ) {
	// otherwise we may block
	if ( g_hostdb.m_hostId != 0 ) {
		log("admin: You can only make config changes from host #0.");
		g_errno = EBADENGINEER;
		return true;
	}
	// only broadcast one request at a time... for add/del coll really
	if ( s_locked ) {
		g_errno = EBADENGINEER;
		log("admin: Failed to broadcast config change. An "
		    "operation is already in progress.");
		return true;
	}
	// lock it now
	s_locked = true;
	// save stuff
	s_page   = page;
	s_s      = s;
	s_r.copy ( r ); // just a ptr copy really, references s->m_readBuf
	// . this returns false if blocked
	// . this removes &cast=1 and adds &cast=0 to the request before sending
	//   to each host in the network
	if ( ! s_msg28.massConfig ( s_s , &s_r , -1 , NULL , doneWrapper ) ) 
		return false;
	// did not block
	s_locked = false;
	return true;
}

void doneWrapper ( void *state ) {
	// release the lock
	s_locked = false;

	// . now we can handle the page
	// . this must call g_httpServer.sendDynamicReply() eventually
	s_pages[s_page].m_function ( s_s , &s_r );
}
*/

// certain pages are automatically generated by the g_parms class
// because they are menus of configurable parameters for either g_conf
// or for a particular CollectionRec record for a collection.
bool sendPageGeneric ( TcpSocket *s , HttpRequest *r ) {
	//long page = g_pages.getDynamicPageNumber ( r );
	return g_parms.sendPageGeneric ( s , r );//, page );
}

bool Pages::getNiceness ( long page ) {
	// error out if page number out of range
	if ( page < 0 || page >= s_numPages ) 
		return 0;
	return s_pages[page].m_niceness;
}

///////////////////////////////////////////////////////////
//
// Convenient html printing routines
//
//////////////n////////////////////////////////////////////

bool Pages::printAdminTop (SafeBuf     *sb   ,
			   TcpSocket   *s    ,
			   HttpRequest *r    ,
			   char        *qs   ,
			   char* bodyJavascript) {
	long  page   = getDynamicPageNumber ( r );
	//long  user   = getUserType          ( s , r );
	//char *username   = g_users.getUsername ( r );
	char *username = NULL;
	//char *coll   = r->getString ( "c"   );
	//if ( ! coll ) coll = "main";
	char *coll = g_collectiondb.getDefaultColl(r);

	//char *pwd    = r->getString ( "pwd" );
	// get username
	
	bool status = true;

	//User *user = g_users.getUser (username );//,false );
	//if ( user ) pwd = user->m_password;
	char *pwd = NULL;

	sb->safePrintf("<html>\n");

	sb->safePrintf(
		     "<head>\n"
		     "<title>%s | gigablast admin</title>\n"
		     "<meta http-equiv=\"Content-Type\" "
		     "content=\"text/html;charset=utf8\" />\n"
		     "</head>\n",  s_pages[page].m_name);

	// print bg colors
	status &= printColors ( sb, bodyJavascript);

	// print form to encompass table now

	////////
	//
	// . the form
	//
	////////
	// . we cannot use the GET method if there is more than a few k of
	//   parameters, like in the case of the Search Controls page. The
	//   browser simply will not send the request if it is that big.
	if ( s_pages[page].m_usePost == M_MULTI )
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"post\" "
				// we need this for <input type=file> tags
				"ENCTYPE=\"multipart/form-data\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	else if ( s_pages[page].m_usePost == M_POST )
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"post\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	else
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"get\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	// pass on this stuff
	//if ( ! pwd ) pwd = "";
	//sb->safePrintf ( "<input type=hidden name=pwd value=\"%s\">\n",pwd);
	//if ( ! coll ) coll = "";
	sb->safePrintf ( "<input type=hidden name=c value=\"%s\">\n",coll);
	// sometimes we do not want to be USER_MASTER for testing
	//if ( user == USER_ADMIN ) {
	//if ( g_users.hasPermission ( username, PAGE_ADMIN ) ){
	//	sb->safePrintf("<input type=hidden name=master value=0>\n");
	//}
	// should any changes be broadcasted to all hosts?
	//sb->safePrintf ("<input type=hidden name=cast value=\"%li\">\n",
	//		(long)s_pages[page].m_cast);


	// center all
	//sprintf ( p , "<center>\n");
	//p += gbstrlen ( p );
	// table
	sb->safePrintf( "<TABLE "
			"cellpadding=5 border=0>"
			"<tr><td valign=top>");
	// print the logo in upper left corner
	status &= printLogo ( sb , coll );

	/*
	sb->safePrintf("<br><br><br>");

	sb->safePrintf(
		       "<div "
		       "style=\""
		       "max-height:600px;"
		       "max-width:200px;"
		       "min-width:200px;"
		       "padding:4px;" // same as TABLE_STYLE
		       "background-color:#d0d0d0;"
		       "border-radius:10px;"
		       "border:2px #606060 solid;"
		       //"border-width:2px;"
		       //"border-color:#606060;"
		       "overflow-y:auto;"
		       "overflow-x:hidden;"
		       "line-height:23px;"
		       "\""
		       ">"
		       );
	// collection under that
	status &= printCollectionNavBar ( sb, page , username , coll,pwd, qs );
	*/


	//
	// begin the 2nd column of the display
	//
	sb->safePrintf("</TD>\n<TD>");


	// logout link on far right
	sb->safePrintf("<div align=right "
		       "style=\""
		       "max-width:100px;"
		       "right:20px;"
		       "position:absolute;"
		       "\">"
		       "<font color=blue>"
		       // clear the cookie
		       "<span "

		       "style=\"cursor:hand;"
		       "cursor:pointer;\" "

		       "onclick=\"document.cookie='pwd=;';"
		       "window.location.href='/';"
		       "\">"
		       "logout"
		       "</span>"
		       "</font>"
		       "</div>"
		       );

	// print the hosts navigation bar
	status &= printHostLinks ( sb, page , 
				   username , pwd ,
				   coll, NULL, s->m_ip, qs );

	//if ( g_hostdb.getNumHosts() > 1 )
	sb->safePrintf("<br><br>");

	// end table
	//sb->safePrintf ("</td></tr></table><br/>\n");//<br/>\n");

	SafeBuf mb;
	bool added = printRedBox ( &mb );

	// print emergency msg box
	if ( added )
		sb->safePrintf("%s",mb.getBufStart());

        bool isBasic = false;
	if ( page == PAGE_BASIC_SETTINGS ) isBasic = true;
	if ( page == PAGE_BASIC_STATUS ) isBasic = true;
	//if ( page == PAGE_BASIC_DIFFBOT ) isBasic = true;
	//if ( page == PAGE_BASIC_SEARCH  ) isBasic = true;
	if ( page == PAGE_BASIC_SECURITY ) isBasic = true;
	if ( page == PAGE_BASIC_SEARCH ) isBasic = true;

	//
	// print breadcrumb. main > Basic > Settings
	//
	char *menu = "advanced";
	if ( isBasic ) menu = "basic";
	sb->safePrintf("<br>");
	sb->safePrintf("<b><font color=gray size=+2>"
		       "%s &gt; %s &gt; %s</font></b>"
		       "<br><br>\n", 
		       coll, menu, s_pages[page].m_name);



	// print Basic | Advanced links
	if ( isBasic )
		sb->safePrintf ( "<b>"
				 "<a href=/admin/settings?c=%s "
				 "style=text-decoration:none;>"
				 "<font color=red>"
				 "basic"
				 "</font>"
				 "</a>"
				 "</b>"
				 " &nbsp; "
				 "<b>"
				 "<a href=/admin/master?c=%s "
				 "style=text-decoration:none;>"
				 "advanced"
				 "</a>"
				 "</b>"
				 , coll
				 , coll
				 );
	else
		sb->safePrintf ( "<b>"
				 "<a href=/admin/settings?c=%s "
				 "style=text-decoration:none;>"
				 "basic"
				 "</a>"
				 "</b>"
				 " &nbsp; "
				 "<b>"
				 "<a href=/admin/settings?c=%s "
				 "style=text-decoration:none;>"
				 "<font color=red>"
				 "advanced"
				 "</font>"
				 "</a>"
				 "</b>"
				 , coll
				 , coll
				 );

	sb->safePrintf("<br><br>");

	// print the menu links under that
	status &= printAdminLinks ( sb, page , coll , isBasic );

	// begin 2nd row in big table
	sb->safePrintf("</td></TR>");

	sb->safePrintf(
		       "<TR>"
		       "<TD valign=top>"
		       "<div "
		       "style=\""
		       "max-height:600px;"
		       "max-width:200px;"
		       "min-width:200px;"
		       "padding:4px;" // same as TABLE_STYLE
		       "background-color:#d0d0d0;"
		       "border-radius:10px;"
		       "border:2px #606060 solid;"
		       //"border-width:2px;"
		       //"border-color:#606060;"
		       "overflow-y:auto;"
		       "overflow-x:hidden;"
		       "line-height:23px;"
		       "\""
		       ">"
		       );
	// collection under that
	status &= printCollectionNavBar ( sb, page , username , coll,pwd, qs );

	sb->safePrintf("</div>");

	sb->safePrintf("<br>"
		       "<b>Key</b>"
		       "<br>"
		       "<br>"
		       );
	sb->safePrintf(
		       "<font color=black>"
		       "&#x25cf;</font> spider is done"
		       "<br>"

		       "<font color=orange>"
		       "&#x25cf;</font> spider is paused"
		       "<br>"

		       "<font color=green>"
		       "&#x25cf;</font> spider is active"
		       "<br>"

		       "<font color=gray>"
		       "&#x25cf;</font> spider queue is empty"
		       "<br>"
		       );


	sb->safePrintf("</TD>");

	// the controls will go here
	sb->safePrintf("<TD valign=top>");

	return true;
}


/*
bool Pages::printAdminTop2 (SafeBuf     *sb   ,
			   TcpSocket   *s    ,
			   HttpRequest *r    ,
			   //char        *qs   ) {
			   char        *qs   ,
			   char	       *scripts    ,
			   long		scriptsLen ) {
	long  page   = getDynamicPageNumber ( r );
	//long  user   = getUserType          ( s , r );
	char *username =g_users.getUsername(r);
	char *coll   = r->getString ( "c"   );
	//char *pwd    = r->getString ( "pwd" );
	long  fromIp = s->m_ip;
	return printAdminTop2 ( sb, page, username, coll, NULL, fromIp , qs ,
			       scripts, scriptsLen );
}

bool Pages::printAdminTop2 ( SafeBuf *sb    ,
			    long    page   ,
			    //long    user   ,
			    char   *username,
			    char   *coll   ,
			    char   *pwd    ,
			    long    fromIp ,
			    //char   *qs     ) {
			    char   *qs     ,
			    char   *scripts,
			    long    scriptsLen ) {
	bool status = true;

	sb->safePrintf(
		     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		     "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 "
		     "Transitional//EN\" \""
		     "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd"
		     "\">\n"
		     "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
		     " xml:lang=\"en\" lang=\"en\">\n"
		     "<head>\n"
		     );
	// this allows for inclusion of javascripts and css styles
	if ( scripts && scriptsLen > 0 )
		sb->safeMemcpy( scripts, scriptsLen );
	sb->safePrintf(
			"<style type=\"text/css\">\n"
			"<!--\n"
			"body,td,p,.h{font-family:arial,sans-serif; "
			"font-size: 15px}\n"
			"-->\n"
			"</style>\n"
			"<title>Gigablast Admin</title>\n"
			"<meta http-equiv=\"Content-Type\" "
			"content=\"text/html;charset=utf8\" />\n"
			"</head>\n" );
	// print bg colors
	status &= printColors3 ( sb );
	// master div to align admin-top table(s)
	sb->safePrintf( "<div class=\"main\">\n" );
	sb->safePrintf( "<div class=\"central\">\n" );

	// center all
	//sprintf ( p , "<center>\n");
	//p += gbstrlen ( p );
	// table
	sb->safePrintf( "<table border=\"0\"><tr><td>");
	// print the logo in upper left corner
	status &= printLogo ( sb , coll );
	// after logo text
	//if ( g_users.hasPermission(username,PAGE_QUALITY)  ) {
	//	sb->safePrintf( " &nbsp; <font size=\"+1\"><b>"
	//			"Quality Control</b></font>" );
	//}
//#ifdef SPLIT_INDEXDB
//	long split = INDEXDB_SPLIT;
//#else
//	long split = 1;
//#endif
	//long split = g_hostdb.m_indexSplits;
	// the version info
	//sb->safePrintf ("<br/><b>%s</b>", GBVersion );
			
	// . the the hosts
	// . don't print host buttons if only 1 host
	//if ( user == USER_MASTER && g_hostdb.m_numHosts > 1 ) {
	if ( !g_users.hasPermission(username,PAGE_NOHOSTLINKS) ) {
		// print the hosts navigation bar
		status &= printHostLinks ( sb, page , 
					   username , pwd ,
					   coll,NULL, fromIp, qs );
	}
	// end table
	sb->safePrintf ("</td></tr></table><br/><br/>\n");

	// print the links
	status &= printAdminLinks ( sb, page , username , coll , NULL, true );

	// collection under that
	status &= printCollectionNavBar ( sb, page , username , coll ,NULL,qs);

	// print the links
	status &= printAdminLinks ( sb, page , username , coll , NULL, false );

	sb->safePrintf( "</div>\n" );
	return true;
}
*/

void Pages::printFormTop( SafeBuf *sb, HttpRequest *r ) {
	long  page   = getDynamicPageNumber ( r );
	// . the form
	// . we cannot use the GET method if there is more than a few k of
	//   parameters, like in the case of the Search Controls page. The
	//   browser simply will not send the request if it is that big.
	if ( s_pages[page].m_usePost )
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"post\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
	else
		sb->safePrintf ("<form name=\"SubmitInput\" method=\"get\" "
				"action=\"/%s\">\n",
				s_pages[page].m_filename);
}

void Pages::printFormData( SafeBuf *sb, TcpSocket *s, HttpRequest *r ) {

	long  page   = getDynamicPageNumber ( r );
	//long  user   = getUserType          ( s , r );
	//char *username =g_users.getUsername(r);
	//char *pwd    = r->getString ( "pwd" );
	char *coll   = r->getString ( "c"   );
	// pass on this stuff
	//if ( ! pwd ) pwd = "";
	//sb->safePrintf ( "<input type=\"hidden\" name=\"pwd\" "
	//		 "value=\"%s\" />\n", pwd);
	if ( ! coll ) coll = "";
	sb->safePrintf ( "<input type=\"hidden\" name=\"c\" "
			 "value=\"%s\" />\n", coll);
	// sometimes we do not want to be USER_MASTER for testing
	//if ( user == USER_ADMIN ) {
	//if ( g_users.hasPermission( username, PAGE_ADMIN ) ){
	//	sb->safePrintf( "<input type=\"hidden\" name=\"master\" "
	//			"value=\"0\" />\n");
	//}

	// should any changes be broadcasted to all hosts?
	sb->safePrintf ("<input type=\"hidden\" name=\"cast\" value=\"%li\" "
			"/>\n",
			(long)s_pages[page].m_cast);

}

/*
char *Pages::printAdminBottom ( char *p , char *pend , HttpRequest *r ) {
	return printAdminBottom ( p , pend );
}

char *Pages::printAdminBottom ( char *p , char *pend ) {
	// update button
	sprintf ( p, "<center>"
		  "<input type=submit name=action value=submit /></center>"
		  "<br/>\n");
	p += gbstrlen ( p );
	// end form
	sprintf ( p, "</form>\n" );
	p += gbstrlen ( p );			  	
	return p;
}
*/

bool Pages::printAdminBottom ( SafeBuf *sb, HttpRequest *r ) {
	return printAdminBottom ( sb );
}

bool Pages::printSubmit ( SafeBuf *sb ) {
	// update button
	return sb->safePrintf ( 
			       //"<br>"
				"<center>"
				"<input type=submit name=action value=submit>"
				"</center>"
				"<br>"
				"\n" ) ;
}

bool Pages::printAdminBottom ( SafeBuf *sb ) {
	bool status = true;
	// update button
	if ( !sb->safePrintf ( "<center>"
			       "<input type=submit name=action value=submit>"
			       "</center>"
			       "<br>\n" ) )
		status = false;
	if ( ! sb->safePrintf(
			      "</TD>"
			      "</TR>"
			      "</TABLE>\n"
			      "</form>"
			      //"</DIV>\n"
			      ) )
		status = false;
	// end form
	if ( ! sb->safePrintf ( "</body>\n</html>\n" ) )
		status = false;
	return status;
}

bool Pages::printAdminBottom2 ( SafeBuf *sb, HttpRequest *r ) {
	return printAdminBottom2 ( sb );
}

bool Pages::printAdminBottom2 ( SafeBuf *sb ) {
	bool status = true;
	sb->safePrintf ( "</div>\n</body>\n</html>\n" );
	return status;
}

/*
char *Pages::printTail ( char *p , char *pend , bool isLocal ) {
	// don't breech the buffer
	if ( p + 2000 >= pend ) return p;
	// now print the tail
	sprintf ( p , 
		  //"\n<center><br><br><font color=#c62939 size=-1><b>"
		  "\n<center><b>"
		  "<p class=nav><a href=\"/about.html\">"
		  "About</a> &nbsp; &nbsp;");
	p += gbstrlen ( p );

	if ( g_conf.m_addUrlEnabled ) {
		sprintf(p,"<a href=\"/addurl\">"
			"Add a Url</a> &nbsp; &nbsp; ");
		p += gbstrlen ( p );
	}

	//sprintf ( p , 
	//	  "<a href=\"/careers.html\">"
	//	  "Careers</a> &nbsp; &nbsp;");
	//p += gbstrlen ( p );

	sprintf ( p , 
		  "<a href=\"/contact.html\">"
		  "Contact</a> &nbsp; &nbsp;");
	p += gbstrlen ( p );

	sprintf ( p , 
		  "<a href=\"/help.html\">Help</a> &nbsp; &nbsp;"
		  //"<a href=\"/press.html\">Press</a> &nbsp; &nbsp; " 
		  //"<a href=\"/clients.html\">Clients</a> &nbsp; &nbsp; "
		  "<a href=\"/products.html\">Services</a> &nbsp; &nbsp;");
	p += gbstrlen ( p );

	// print admin link only if we are the master admin by ip or password
	//if ( ! pwd ) pwd = "";
	//if ( user == USER_MASTER ) 
	//if ( g_users.hasPermission(username,PAGE_MASTER) )
	if ( isLocal )
		sprintf ( p , "[<a href=\"/master?\">Admin"
			  "</a>] &nbsp; &nbsp; " );
	//else              
	//	sprintf ( p , "<a href=\"/login\">Login"
	//		  "</a> &nbsp; &nbsp; " );
	p += gbstrlen ( p );

	sprintf ( p , "</p></b></center></body></html>" );
	p += gbstrlen ( p );
	// return length of bytes we stored
	return p ;
}
*/

bool Pages::printTail ( SafeBuf* sb, bool isLocal ) {
	// now print the tail
	sb->safePrintf (
		  //"\n<center><br><br><font color=#c62939 size=-1><b>"
		  "\n<center><b>"
		  "<p class=nav><a href=\"/about.html\">"
		  "About</a> &nbsp; &nbsp;");

	if ( g_conf.m_addUrlEnabled ) {
		sb->safePrintf("<a href=\"/addurl\">"
			"Add a Url</a> &nbsp; &nbsp; ");
	}

	//sb->safePrintf (
	//	  "<a href=\"/careers.html\">"
	//	  "Careers</a> &nbsp; &nbsp;");

	sb->safePrintf (
		  "<a href=\"/contact.html\">"
		  "Contact</a> &nbsp; &nbsp;");

	sb->safePrintf (
		  "<a href=\"/help.html\">Help</a> &nbsp; &nbsp;"
		  //"<a href=\"/press.html\">Press</a> &nbsp; &nbsp; " 
		  //"<a href=\"/clients.html\">Clients</a> &nbsp; &nbsp; "
		  "<a href=\"/products.html\">Products</a> &nbsp; &nbsp;");

	// print admin link only if we are the master admin by ip or password
	//if ( ! pwd ) pwd = "";
	//if (g_users.hasPermission(username,PAGE_MASTER) )
	if ( isLocal )
		sb->safePrintf ( "[<a href=\"/master\">Admin"
			  "</a>] &nbsp; &nbsp; " );
	//else              
	//	sprintf ( p , "<a href=\"/login\">Login"
	//		  "</a> &nbsp; &nbsp; " );

	sb->safePrintf ( "</p></b></center></body></html>" );
	// return length of bytes we stored
	return true ;
}




bool Pages::printColors ( SafeBuf *sb, char* bodyJavascript ) {
	// print font and color stuff
	sb->safePrintf (
		  "<body text=#000000 bgcolor=#"
		  BGCOLOR
		  " link=#000000 vlink=#000000 alink=#000000 %s>\n" 
		  "<style>"
		  "body,td,p,.h{font-family:"
		  //"arial,"
		  "arial,"
		  "helvetica-neue"
		  //"helvetica-neue,helvetica,"
		  //"sans-serif"
		  "; "
		  "font-size: 15px;} "
		  //".h{font-size: 20px;} .h{color:} "
		  //".q{text-decoration:none; color:#0000cc;}"
		  "</style>\n",
		  bodyJavascript);
	return true;
}
/*
char *Pages::printColors ( char *p , char *pend, char* bodyJavascript ) {
	// print font and color stuff
	sprintf ( p , 
		  "<body text=#000000 bgcolor=#"
		  BGCOLOR
		  " link=#000000 vlink=#000000 alink=#000000 %s>\n" 
		  "<style>"
		  "body,td,p,.h{font-family:arial,sans-serif; "
		  "font-size: 15px;} "
		  //".h{font-size: 20px;} .h{color:} "
		  //".q{text-decoration:none; color:#0000cc;}"
		  "</style>\n",
		  bodyJavascript );
	p += gbstrlen ( p );
	return p;
}

char *Pages::printColors2 ( char *p , char *pend ) {
	// print font and color stuff
	sprintf ( p , 
		  "<body text=#000000 bgcolor=#"
		  BGCOLOR
		  " link=#000000 vlink=#000000 alink=#000000 onLoad=sf()>"
		  "<style><!--"
		  "body,td,a,p,.h{font-family:arial,sans-serif "
		  "font-size: 15px;} "
		  //".h{font-size: 20px;} .h{color:} "
		  //".q{text-decoration:none; color:#0000cc;}"
		  "a:link,.w,a.w:link,.w a:link{color:#00c}"
		  "a:visited,.fl:visited{color:#551a8b}"
		  "a:active,.fl:active{color:#f00}"

		  "//--></style>\n"

		  "<style><!--"
		  "body,td,div,.p,a{font-family:arial,sans-serif }"
		  "div,td{color:#000}"
		  ".f,.fl:link{color:#6f6f6f}"
		  "a:link,.w,a.w:link,.w a:link{color:#00c}"
		  "a:visited,.fl:visited{color:#551a8b}"
		  "a:active,.fl:active{color:#f00}"
		  ".t a:link,.t a:active,.t a:visited,.t{color:#ffffff}"
		  ".t{background-color:#3366cc}"
		  ".h{color:#3366cc;font-size:14px}"
		  ".i,.i:link{color:#a90a08}"
		  ".a,.a:link{color:#008000}"
		  ".z{display:none}"
		  "div.n {margin-top: 1ex}"
		  ".n a{font-size:10pt; color:#000}"
		  ".n .i{font-size:10pt; font-weight:bold}"
		  ".q a:visited,.q a:link,.q a:active,.q {text-decoration: "
		  "none; color: #00c;}"
		  ".b{font-size: 12pt; color:#00c; font-weight:bold}"
		  ".ch{cursor:pointer;cursor:hand}"
		  "//-->"
		  "</style>" 

		  );
	p += gbstrlen ( p );
	return p;
}
*/

bool Pages::printColors3 ( SafeBuf *sb ) {
	// print font and color stuff
	sb->safePrintf (
		  "<body text=\"#000000\" bgcolor=\"#"
		  BGCOLOR
		  "\" link=\"#000000\" vlink=\"#000000\" "
		  "alink=\"#000000\" onload=\"javascript:st_init()\">\n"
		  // onLoad=sf()>"
		  );
	return true;
}
/*
char *Pages::printFocus ( char *p , char *pend ) {
	// print the logo in upper right corner
	sprintf ( p , 
		  "<script><!--"
		  "function sf(){document.f.q.focus();}"
		  "// --></script>\n" );
	p += gbstrlen ( p );
	return p;
}
*/

bool Pages::printLogo ( SafeBuf *sb, char *coll ) {
	// print the logo in upper right corner
	if ( ! coll ) coll = "";
	sb->safePrintf (
		  "<a href=\"/?c=%s\">"
		  "<img width=\"200\" height=\"40\" border=\"0\" "
		  "alt=\"Gigablast\" src=\"/logo-small.png\" />"
		  "</a>\n",coll);
	return true;
}

/*
char *Pages::printLogo ( char *p , char *pend , char *coll ) {
	// print the logo in upper right corner
	if ( ! coll ) coll = "";
	sprintf ( p , 
		  "<a href=\"/?c=%s\">"
		  "<img width=\"295\" height=\"64\" border=\"0\" "
		  "alt=\"Gigablast\" src=\"/logo-small.png\" />"
		  "</a>\n",coll);
	p += gbstrlen ( p );
	return p;
}
*/

bool Pages::printHostLinks ( SafeBuf* sb     ,
			     long     page   ,
			     char    *username ,
			     char    *password ,
			     char    *coll   ,
			     char    *pwd    ,
			     long     fromIp ,
			     char    *qs     ) {
	bool status = true;

	// ignore
	if ( ! username ) username = "";

	if ( ! password ) {
		User *user = g_users.getUser (username  );
		if ( user ) password = user->m_password;
	}
	if ( ! password ) password = "";

	long total = 0;
	// add in hosts
	total += g_hostdb.m_numHosts;
	// and proxies
	total += g_hostdb.m_numProxyHosts;	
	// don't print host buttons if only 1 host
	//if ( total <= 1 ) return status;

	sb->safePrintf (  //"&nbsp; &nbsp; &nbsp; "
			  "<a style=text-decoration:none; href=/admin/hosts>"
			  "<b>hosts in cluster</b></a>: ");

	if ( ! qs   ) qs   = "";
	//if ( ! pwd  ) pwd  = "";
	if ( ! coll ) coll = "";

	// print the 64 hosts before and after us
	long radius = 512;//64;
	long hid = g_hostdb.m_hostId;
	long a = hid - radius;
	long b = hid + radius;
	long diff ;
	if ( a < 0 ) { 
		diff = -1 * a; 
		a += diff; 
		b += diff; 
	}
	if ( b > g_hostdb.m_numHosts ) { 
		diff = b - g_hostdb.m_numHosts;
		a -= diff; if ( a < 0 ) a = 0;
	}
	for ( long i = a ; i < b ; i++ ) {
		// skip if negative
		if ( i < 0 ) continue;
		if ( i >= g_hostdb.m_numHosts ) continue;
		// get it
		Host *h = g_hostdb.getHost ( i );
		unsigned short port = h->m_httpPort;
		// use the ip that is not dead, prefer eth0
		unsigned long ip = g_hostdb.getBestIp ( h , fromIp );
		// convert our current page number to a path
		char *path = s_pages[page].m_filename;
		// highlight itself
		char *ft = "";
		char *bt = "";
		if ( i == hid && ! g_proxy.isProxy() ) {
			ft = "<b><font color=red>";
			bt = "</font></b>";
		}
		// print the link to it
		sb->safePrintf("%s<a href=\"http://%s:%hu/%s?"
			       //"username=%s&pwd=%s&"
			       "c=%s%s\">"
			       "%li</a>%s ",
			       ft,iptoa(ip),port,path,
			       //username,password,
			       coll,qs,i,bt);
	}		

	// print the proxies
	for ( long i = 0; i < g_hostdb.m_numProxyHosts; i++ ) {
		char *ft = "";
		char *bt = "";
		if ( i == hid && g_proxy.isProxy() ) {
			ft = "<b><font color=red>";
			bt = "</font></b>";
		}
		Host *h = g_hostdb.getProxy( i );
		unsigned short port = h->m_httpPort;
		// use the ip that is not dead, prefer eth0
		unsigned long ip = g_hostdb.getBestIp ( h , fromIp );
		char *path = s_pages[page].m_filename;
		sb->safePrintf("%s<a href=\"http://%s:%hu/%s?"
			       //"username=%s&pwd=%s&"
			       "c=%s%s\">"
			       "proxy%li</a>%s ",
			       ft,iptoa(ip),port,path,
			       //username,password,
			       coll,qs,i,bt);
	}

	return status;
}


// . print the master     admin links if "user" is USER_MASTER 
// . print the collection admin links if "user" is USER_ADMIN
bool  Pages::printAdminLinks ( SafeBuf *sb,
			       long  page ,
			       char *coll ,
			       bool  isBasic ) {

	bool status = true;
	// prepare for printing these
	//if ( ! coll ) coll = "";
	//if ( ! pwd  ) pwd  = "";

	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// sometimes there are no collections!
	//if ( ! cr ) return true;
	//char *coll = "";
	//if ( cr ) coll = cr->m_coll;
	

	//if ( ! top ) {
	//	// . if no collection do not print anything else
	//	// . no, we accept as legit (print out as "main")
	//	//if ( ! coll[0] ) return status;
	//	if ( g_collectiondb.m_numRecsUsed == 0 ) return status;
	//	//if ( ! g_collectiondb.getRec ( coll )  ) return status;
	//}

	//sprintf(p,"<font size=+1>\n" );
	//p += gbstrlen(p);
	//sb->safePrintf ("<center>\n" );

	// soemtimes we do not want to be USER_MASTER for testing
	char buf [ 64 ];
	buf[0] = '\0';
	//if ( g_users.hasPermission(username,PAGE_ADMIN ) ) 
	//	sprintf(buf,"&master=0");

	// unfortunately width:100% is percent of the virtual window, not the
	// visible window... so just try 1000px max
	sb->safePrintf("<div style=max-width:800px;>");

	//long matt1 = atoip ( MATTIP1 , gbstrlen(MATTIP1) );
	//long matt2 = atoip ( MATTIP2 , gbstrlen(MATTIP2) );
	for ( long i = PAGE_BASIC_SETTINGS ; i < s_numPages ; i++ ) {
		// do not print link if no permission for that page
		//if ( (s_pages[i].m_perm & user) == 0 ) continue;
		//if ( ! g_users.hasPermission(username,i) ) continue;
		// do not print Sync link if only one host
		//if ( i == PAGE_SYNC && g_hostdb.getNumHosts() == 1) continue;
		// top or bottom
		//if (   top && i >= PAGE_CGIPARMS ) continue;
		//if ( ! top && i  < PAGE_CGIPARMS ) continue;

		// skip seo link
		if ( ! g_conf.m_isMattWells && i == PAGE_SEO ) 
			continue;

		// skip page autoban link
		if ( ! g_conf.m_isMattWells && i == PAGE_AUTOBAN )
			continue;

		// is this page basic?
		bool pageBasic = false;
		if ( i >= PAGE_BASIC_SETTINGS &&
		     i <= PAGE_BASIC_SEARCH )
			pageBasic = true;

		// print basic pages under the basic menu, advanced pages
		// under the advanced menu...
		if ( isBasic != pageBasic ) continue;

		// ignore these for now
		//if ( i == PAGE_SECURITY ) continue;
		if ( i == PAGE_ACCESS ) continue;
		if ( i == PAGE_INDEXDB ) continue;
		if ( i == PAGE_RULES ) continue;
		if ( i == PAGE_SEARCHBOX ) continue;
		if ( i == PAGE_TITLEDB ) continue;
		// move these links to the coll nav bar on the left
		if ( i == PAGE_ADDCOLL ) continue;
		if ( i == PAGE_DELCOLL ) continue;
		if ( i == PAGE_HOSTS ) continue;

		// print "url download" before "inject url"
		// GET /mycollname_urls.csv
		/* nah, keep this in basic > status
		if ( i == PAGE_INJECT ) {
			sb->safePrintf (
					"<b>"
					"<a style=text-decoration:none; "
					"href=\"/download/%s_urls.txt\">"
					"<nobr>"
					"data downloads"
					"</nobr>"
					"</a>"
					"</b>"
					" &nbsp; \n",
					coll );
		}		
		*/

		if ( cr && ! cr->m_isCustomCrawl && i == PAGE_CRAWLBOT )
			continue;

		// print it out
		if ( i == PAGE_LOGIN || i == PAGE_LOGIN2 ) 
			sb->safePrintf(
				       //"<span style=\"white-space:nowrap\">"
				       "<a href=\"/%s?"
				       //"user=%s&pwd=%s&"
				       "c=%s%s\">%s</a>"
				       //"</span>"
				       " &nbsp; \n",s_pages[i].m_filename,
				       //username,pwd,
				       coll,
				       buf,s_pages[i].m_name);
		else if ( page == i )
			sb->safePrintf(
				       //"<span style=\"white-space:nowrap\">"
				       "<b>"
				       "<a style=text-decoration:none; "
				       "href=\"/%s?c=%s%s\">"
				       "<font color=red>"
				       "<nobr>"
				       "%s"
				       "</nobr>"
				       "</font>"
				       "</a>"
				       "</b>"
				       //"</span>"
				       " &nbsp; \n"
				       ,s_pages[i].m_filename
				       ,coll
				       ,buf
				       ,s_pages[i].m_name
				       );
		else
			sb->safePrintf(
				       //"<span style=\"white-space:nowrap\">"
				       "<b>"
				       "<a style=text-decoration:none; "
				       "href=\"/%s?c=%s%s\">"
				       "<nobr>"
				       "%s"
				       "</nobr>"
				       "</a>"
				       "</b>"
				       //"</span>"
				       " &nbsp; \n"
				       ,s_pages[i].m_filename
				       ,coll
				       ,buf
				       ,s_pages[i].m_name);
		// print <br> after the last master admin control
		/*
		if ( i == PAGE_DELCOLL && user == USER_MASTER ) {
			// . if no collection do not print anything else
			// . no, we accept as legit (print out as "main")
			//if ( ! coll[0] ) break;
			if ( g_collectiondb.m_numRecsUsed == 0 ) break;
			// or if no collection selected, same thing
			if ( ! coll[0] ) break;
			sprintf ( p , "<br><br>\n");
			p += gbstrlen(p);
		}
		*/
	}

	// print documentation links
	if ( ! isBasic )
		sb->safePrintf(" <a style=text-decoration:none "
			       "href=/admin.html>"
			       "<b>"
			       "admin guide"
			       "</b></a> "

			       "&nbsp; "

			       " <a style=text-decoration:none; "
			       "href=/developer.html>"
			       "<b>dev guide</b></a>" 

			       );
	
	sb->safePrintf("</div>");

	//sb->safePrintf("</center>" );
	//sb->safePrintf("<br/>" );
	//sb->safePrintf("<br/>" );

	return status;
}




bool Pages::printCollectionNavBar ( SafeBuf *sb     ,
				    long  page     ,
				    //long  user     ,
				    char *username,
				    char *coll     ,
				    char *pwd      ,
				    char *qs       ) {
	bool status = true;
	//if ( ! pwd ) pwd = "";
	if ( ! qs  ) qs  = "";

	// if not admin just print collection name
	if ( g_collectiondb.m_numRecsUsed == 0 ) {
		sb->safePrintf ( "<center>"
			  "<br/><b><font color=red>No collections found. "
			  "Click <i>add collection</i> to add one."
			  "</font></b><br/><br/></center>\n");
		return status;
	}
	// if not admin just print collection name
	//if ( user == USER_ADMIN ) {
	//if (g_users.hasPermission(username,PAGE_ADMIN) ){
	//sb->safePrintf ( "<center><br/>Collection <b>"
	//		 "<font color=red>%s</font></b>"
	//		 "<br/><br/></center>" , coll );
	//	return status ;
	//}
	// print up to 10 names on there
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	bool highlight = true;
	if ( collnum < (collnum_t)0) {
		highlight = false; collnum=g_collectiondb.getFirstCollnum(); }
	if ( collnum < (collnum_t)0) return status;
	
	long a = collnum;
	long counta = 1;
	while ( a > 0 && counta < 15 ) 
		if ( g_collectiondb.m_recs[--a] ) counta++;
	long b = collnum + 1;
	long countb = 0;
	while ( b < g_collectiondb.m_numRecs && countb < 16 )
		if ( g_collectiondb.m_recs[b++] ) countb++;

	char *s = "s";
	if ( g_collectiondb.m_numRecsUsed == 1 ) s = "";
	sb->safePrintf ( "<center><nobr><b>%li Collection%s</b></nobr>"
			 "</center>\n",
			 g_collectiondb.m_numRecsUsed , s );

	sb->safePrintf( "<center>"
			"<font size=-1>"
			"<a href=/admin/addcoll?c=%s>add</a> &nbsp; &nbsp; "
			"<a href=/admin/delcoll?c=%s>delete</a>"
			"</font>"
			"</center>"
			, coll
			, coll
			);

	char *color = "red";
	//if ( page >= PAGE_CGIPARMS ) color = "red";
	//else                         color = "black";

	// style for printing collection names
	sb->safePrintf("<style>.x{text-decoration:none;font-weight:bold;}"
		       ".e{background-color:#e0e0e0;}"
		       "</style>\n");

	long row = 0;

	//for ( long i = a ; i < b ; i++ ) {
	for ( long i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cc = g_collectiondb.m_recs[i];
		if ( ! cc ) continue;
		char *cname = cc->m_coll;

		row++;

		//if ( p + gbstrlen(cname) + 100 >= pend ) return p;
		// collection name HACK for backwards compatibility
		//if ( ! cname[0] ) cname = "main";

		// every other coll in a darker div
		if ( (row % 2) == 0 )
			sb->safePrintf("<div class=e>");

		sb->safePrintf("<nobr>");

		// print color bullet
		// green = active
		// yellow = paused
		// black = done
		// gray = empty
		// red = going but has > 50% errors in last 100 sample.
		//       like timeouts etc.

		CrawlInfo *ci = &cc->m_globalCrawlInfo;
		char *bcolor = "";
		if ( ! cc->m_spideringEnabled && ci->m_hasUrlsReadyToSpider )
			bcolor = "orange";// yellow is too hard to see
		if (   cc->m_spideringEnabled && ci->m_hasUrlsReadyToSpider )
			bcolor = "green";
		if ( ! ci->m_hasUrlsReadyToSpider )
			bcolor = "black";
		// when we first add a url via addurl or inject it will
		// set hasUrlsReadyToSpider on all hosts to true i think
		// and Spider.cpp increments urlsharvested.
		if (   cc->m_spideringEnabled && 
		     ! ci->m_hasUrlsReadyToSpider &&
		       ci->m_urlsHarvested )
			bcolor = "gray";

		sb->safePrintf("<font color=%s>&#x25cf;</font> ",bcolor);

		if ( i != collnum || ! highlight )// || ! coll || ! coll[0])
			sb->safePrintf ( "<a title=\"%s\" "
					 "class=x "
					 "href=\"/%s?c=%s%s\">%s"
				  "</a> &nbsp;",
					 cname,
					 s_pages[page].m_filename,
					 cname ,
					 qs, cname );
		else
			sb->safePrintf ( "<b><font title=\"%s\" "
					 "color=%s>%s</font></b> "
					 "&nbsp; ",  
					 cname, color , cname );
		sb->safePrintf("</nobr>");

		// every other coll in a darker div
		if ( (row % 2) == 0 )
			sb->safePrintf("</div>");
		else
			sb->safePrintf("<br>\n");
	}

	//sb->safePrintf ( "</center><br/>" );

	return status;
}


/*
char *Pages::printCollectionNavBar ( char *p        ,
				     char *pend     ,
				     long  page     ,
				     //long  user     ,
				     char *username ,
				     char *coll     ,
				     char *pwd      ,
				     char *qs       ) {
	//if ( ! pwd ) pwd = "";
	if ( ! qs  ) qs  = "";
	// if not admin just print collection name
	if ( g_collectiondb.m_numRecsUsed == 0 ) {
		sprintf ( p , "<center>"
			  "<br/><b><font color=red>No collections found. "
			  "Click <i>add collection</i> to add one."
			  "</font></b><br/><br/></center>\n");
		p += gbstrlen ( p );
		return p ;
	}
	// if not admin just print collection name
	//if ( user == USER_ADMIN ) {
	if (g_users.hasPermission(username,PAGE_ADMIN) ){	
		sprintf ( p , "<center><br/>Collection <b>"
			  "<font color=red>%s</font></b>"
			  "<br/><br/></center>" , coll );
		p += gbstrlen ( p );
		return p ;
	}
	// print up to 10 names on there
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	bool highlight = true;
	if ( collnum < (collnum_t)0) {
		highlight = false; collnum=g_collectiondb.getFirstCollnum(); }
	if ( collnum < (collnum_t)0) return p;
	
	long a = collnum;
	long counta = 1;
	while ( a > 0 && counta < 15 ) 
		if ( g_collectiondb.m_recs[--a] ) counta++;
	long b = collnum + 1;
	long countb = 0;
	while ( b < g_collectiondb.m_numRecs && countb < 16 )
		if ( g_collectiondb.m_recs[b++] ) countb++;

	sprintf ( p , "<center><br/>Collections: &nbsp;\n" );
	p += gbstrlen ( p );

	char *color;
	if ( page >= PAGE_OVERVIEW ) color = "red";
	else                         color = "black";

	for ( long i = a ; i < b ; i++ ) {
		CollectionRec *cc = g_collectiondb.m_recs[i];
		if ( ! cc ) continue;
		char *cname = cc->m_coll;
		if ( p + gbstrlen(cname) + 100 >= pend ) return p;
		// collection name HACK for backwards compatibility
		//if ( ! cname[0] ) cname = "main";

		if ( i != collnum || ! highlight )// || ! coll || ! coll[0])
			sprintf ( p , "<a href=\"/%s?c=%s%s\">%s"
				  "</a> &nbsp;",
				  s_pages[page].m_filename,cc->m_coll ,
				  qs, cname );
		else
			sprintf ( p , "<b><font color=%s>%s</font></b> "
				  "&nbsp; ",  color , cname );
		p += gbstrlen ( p );
	}

	sprintf ( p , "</center><br/>" );
	p += gbstrlen ( p );

	return p;
}
*/
/*
// print the drop down menu of rulesets used by Sitedb and URL Filters page
char *Pages::printRulesetDropDown ( char *p            , 
				    char *pend         ,
				    long  user         ,
				    char *cgi          ,
				    long  selectedNum  ,
				    long  subscript    ) {
	// . print pulldown menu of different site filenums
	// . 0 - default site
	// . 1 - banned  site
	// . 2 - bad     site
	// . 3 - decent  site
	// . 4 - good    site
	// . 5 - super   site
	if ( subscript <= 0 ) sprintf(p,"<select name=%s>\n"   ,cgi);
	else                  sprintf(p,"<select name=%s%li>\n",cgi,subscript);
	p += gbstrlen ( p );
	// print NONE (PageReindex.cpp uses this one)

	//	if ( selectedNum == -2 ) {
	sprintf (p,"<option value=-1>NONE");
	p += gbstrlen ( p );
	//	}

	long i = 0;
	for ( ; i < 10000 ; i++ ) {
		// . get the ruleset's xml
		// . this did accept the coll/collLen but now i think we 
		//   can use the same set of rulesets for all collections
		// z - now it is collection dependent again.
		Xml *xml = g_tagdb.getSiteXml(i,g_conf.m_defaultColl, 
					       gbstrlen(g_conf.m_defaultColl));
		// if NULL, we're finished
		if ( ! xml ) break;
		// skip if retired
		bool retired = xml->getBool ( "retired" , false ) ;
		if ( retired && user == USER_SPAM ) continue;
		// then if retired
		char *rr = "";
		if ( retired ) rr = "retired - ";
		// get the name of the record
		long  slen;
		char *s = xml->getString ( "name" , &slen );
		// set pp to "selected" if it matches "fileNum"
		char *pp = "";
		if ( i == selectedNum ) pp = " selected";
		// print name if we got it
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			sprintf ( p , "<option value=%li%s>%s%s "
				  "[tagdb%li.xml]",i,pp,rr,s,i);
			s[slen] = c;
		}
		// otherwise, print as number
		else  
			sprintf ( p , "<option value=%li%s>%stagdb%li.xml",
				  i,pp,rr,i);
		p += gbstrlen ( p );
	}
	sprintf ( p , "<option value=%li>Always Use Default", 
		  (long)USEDEFAULTSITEREC);
	p += gbstrlen ( p );

	sprintf ( p , "</select>\n" );
	p += gbstrlen ( p );
	return p;
}


bool Pages::printRulesetDropDown ( SafeBuf *sb        ,
				   long  user         ,
				   char *cgi          ,
				   long  selectedNum  ,
				   long  subscript    ) {
	// . print pulldown menu of different site filenums
	// . 0 - default site
	// . 1 - banned  site
	// . 2 - bad     site
	// . 3 - decent  site
	// . 4 - good    site
	// . 5 - super   site
	if ( subscript <= 0 ) sb->safePrintf("<select name=%s>\n"   ,cgi);
	else                  sb->safePrintf("<select name=%s%li>\n",cgi,
					     subscript);
	// print NONE (PageReindex.cpp uses this one)

	//	if ( selectedNum == -2 ) {
	sb->safePrintf ("<option value=-1>NONE");
	//	}

	long i = 0;
	for ( ; i < 10000 ; i++ ) {
		// . get the ruleset's xml
		// . this did accept the coll/collLen but now i think we 
		//   can use the same set of rulesets for all collections
		// z - now it is collection dependent again.
		Xml *xml = g_tagdb.getSiteXml(i,g_conf.m_defaultColl, 
					       gbstrlen(g_conf.m_defaultColl));
		// if NULL, we're finished
		if ( ! xml ) break;
		// skip if retired
		bool retired = xml->getBool ( "retired" , false ) ;
		if ( retired && user == USER_SPAM ) continue;
		// then if retired
		char *rr = "";
		if ( retired ) rr = "retired - ";
		// get the name of the record
		long  slen;
		char *s = xml->getString ( "name" , &slen );
		// set pp to "selected" if it matches "fileNum"
		char *pp = "";
		if ( i == selectedNum ) pp = " selected";
		// print name if we got it
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			sb->safePrintf ( "<option value=%li%s>%s%s "
					 "[tagdb%li.xml]",i,pp,rr,s,i);
			s[slen] = c;
		}
		// otherwise, print as number
		else  
			sb->safePrintf ( "<option value=%li%s>%stagdb%li.xml",
					 i,pp,rr,i);
	}
	sb->safePrintf ( "<option value=%li>Always Use Default", 
			 (long)USEDEFAULTSITEREC);

	sb->safePrintf ( "</select>\n" );
	return true;
}

char *Pages::printRulesetDescriptions ( char *p , char *pend , long user ) {
	sprintf ( p , "<table width=100%% cellpadding=2>" );
	p += gbstrlen ( p );	
	// print the descriptions of each one if we have them
	for ( long i = 0 ; i < 10000 ; i++ ) {
		Xml *xml = g_tagdb.getSiteXml(i,g_conf.m_defaultColl, 
					       gbstrlen(g_conf.m_defaultColl));
		// if NULL, we're finished
		if ( ! xml ) break;
		// skip if retired
		bool retired = xml->getBool ( "retired" , false ) ;
		if ( retired && user == USER_SPAM ) continue;
		// then if retired
		char *rr="";
		if ( retired ) rr = " <i>(retired)</i>";
		// skip if no description
		long slen;
		if ( ! xml->getString ( "description" , &slen ) ) continue;
		// print number of ruleset
		sprintf ( p , "<tr><td><b>tagdb%li.xml</b></td><td>",i );
		p += gbstrlen(p);
		// print the name of ruleset, if any
		char *s = xml->getString ( "name" , &slen );
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			sprintf ( p , "<span style=\"white-space:nowrap\">"
				  "%s%s</span>", s , rr );
			p += gbstrlen ( p );
			s[slen] = c;
		}
		sprintf ( p , "</td><td>" );
		p += gbstrlen(p);
		// then the description, if any
		s = xml->getString ( "description" , &slen );
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			sprintf ( p , "%s", s );
			p += gbstrlen ( p );
			s[slen] = c;
		}
		sprintf ( p , "</td></tr>" ); 
		p += gbstrlen(p);
	}
	sprintf ( p , "</table>" );
	p += gbstrlen ( p );	
	return p;
}

// returns false if failed to print (out of mem, probably)
bool Pages::printRulesetDescriptions ( SafeBuf *sb , long user ) {
	if ( ! sb->safePrintf (  "<table width=100%% cellpadding=2>" ) )
		return false;
	// print the descriptions of each one if we have them
	for ( long i = 0 ; i < 10000 ; i++ ) {
		Xml *xml = g_tagdb.getSiteXml(i,g_conf.m_defaultColl, 
					       gbstrlen(g_conf.m_defaultColl));
		// if NULL, we're finished
		if ( ! xml ) break;
		// skip if retired
		bool retired = xml->getBool ( "retired" , false ) ;
		if ( retired && user == USER_SPAM ) continue;
		// then if retired
		char *rr="";
		if ( retired ) rr = " <i>(retired)</i>";
		// skip if no description
		long slen;
		if ( ! xml->getString ( "description" , &slen ) ) continue;
		// print number of ruleset
		if ( ! sb->safePrintf( "<tr><td><b>tagdb%li.xml</b>"
				       "</td><td>",i ))
			return false;
		// print the name of ruleset, if any
		char *s = xml->getString ( "name" , &slen );
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			sb->safePrintf (  "<span style=\"white-space:nowrap\">"
					  "%s%s</span>", s , rr );
			s[slen] = c;
		}
		if ( ! sb->safePrintf (  "</td><td>" ) ) return false;
		// then the description, if any
		s = xml->getString ( "description" , &slen );
		if ( s && slen > 0 ) {
			char c = s[slen];
			s[slen] = '\0';
			if ( ! sb->safePrintf ( "%s", s ) ) return false;
			s[slen] = c;
		}
		if ( ! sb->safePrintf ( "</td></tr>\n" ) ) return false;
	}
	return sb->safePrintf ( "</table>" );
}
*/

bool sendPageReportSpam ( TcpSocket *s , HttpRequest *r ) {
	char pbuf[32768];
	SafeBuf p(pbuf, 32768);

	p.safePrintf("<html><head><title>Help Fight Search Engine Spam</title></head><body>");

	long clen;
	char* coll = r->getString("c", &clen, "");
	p.safePrintf ("<a href=\"/?c=%s\">"
		      "<img width=\"295\" height=\"64\" border=\"0\" "
		      "alt=\"Gigablast\" src=\"/logo-small.png\" />"
		      "</a>\n", coll);


	p.safePrintf("<br/><br/>Thank you for your submission.  "
		     "<a href=\"%s\">Back To Search Results</a><br/><br/>",
		     r->getReferer());	

	p.safePrintf("</body></html>");

	char* sbuf = p.getBufStart();
	long sbufLen = p.length();

	bool retval = g_httpServer.sendDynamicPage(s,
						   sbuf,
						   sbufLen,
						   -1/*cachetime*/);
	return 	retval;
}

// have the smallest twids on top!
int parmcmp ( const void *a, const void *b ) {
	Parm *pa = (Parm *)a;
	Parm *pb = (Parm *)b;
	return strcmp(pa->m_pstr,pb->m_pstr);
}

#define DARK_YELLOW "ffaaaa"
#define LIGHT_YELLOW "ffcccc"


bool printApiForPage ( SafeBuf *sb , long PAGENUM , CollectionRec *cr ) ;

// let's use a separate section for each "page"
// then have 3 tables, the input parms,
// the xml output table and the json output table
bool sendPageAPI ( TcpSocket *s , HttpRequest *r ) {
	char pbuf[32768];
	SafeBuf p(pbuf, 32768);


	// print standard header
	// 	char *pp    = sb->getBuf();
	// 	char *ppend = sb->getBufEnd();
	// 	if ( pp ) {
	//g_pages.printAdminTop ( &p , s , r );

	// 	sb->incrementLength ( pp - sb->getBuf() );
	// 	}


	CollectionRec *cr = g_collectiondb.getRec ( r , true );
	char *coll = "";
	if ( cr ) coll = cr->m_coll;

	p.safePrintf("<html><head><title>Gigablast API</title></head><body>");

	p.safePrintf("<style>body,td,p,.h{font-family:arial,helvetica-neue; "
		     "font-size: 15px;} </style>");


	// print colors
	g_pages.printColors ( &p );
	// start table
	p.safePrintf( "<table><tr><td>");
	// print logo
	g_pages.printLogo   ( &p , coll );
	p.safePrintf("</td></tr></table><br><br>");

	p.safePrintf("NOTE: All APIs support both GET and POST method. "
		     "If the size of your request is more than 2K you "
		     "should use POST.");
	p.safePrintf("<br><br>");


	p.safePrintf("<div style=padding-left:10%%>"
		     "<font size=+2><b>API by pages</b></font>"
		     "<ul>"
		     );

	for ( long i = 0 ; i < s_numPages ; i++ ) {
		if ( s_pages[i].m_pgflags & PG_NOAPI ) continue;
		char *pageStr = s_pages[i].m_filename;
		// unknown?
		if ( ! pageStr ) pageStr = "???";
		p.safePrintf("<li> <a href=#/%s>/%s</a>"
			     " - %s"
			     "</li>\n",
			     pageStr,
			     pageStr,
			     // description of page
			     s_pages[i].m_desc
			     );
	}

	p.safePrintf("</ul></div>\n");


	p.safePrintf("<div style=padding-left:10%%>"
		     "<font size=+2><b>Other Information</b></font>"
		     "<ul>"
		     "<li> <a href=#qops>Query Operators</a></li>\n"
		     "</ul>"
		     "</div>"
		     "<br>"
		     );



	p.safePrintf("<div style=padding-left:10%%;><hr></div>\n");


	bool printed = false;
	for ( long i = 0 ; i < s_numPages ; i++ ) {
		if ( i == PAGE_NONE ) continue;
		if ( s_pages[i].m_pgflags & PG_NOAPI ) continue;
		if ( printed )
			p.safePrintf("<div style=padding-left:10%%;>"
				     "<hr></div><br>\n");
		printApiForPage ( &p , i , cr );
		printed = true;
	}

	//
	// PRINT QUERY OPERATORS TABLE NOW
	//
	p.safePrintf ( "<center>"
		       "<br>"
		       "<a name=qops>"
		       "<div style=padding-left:10%%;>"
		       "<hr></div><br>\n"
		       "</a>"


		       "<table style=max-width:80%%; %s>"
		       "<tr class=hdrow><td colspan=2>"
		       "<center><b>Query Operators</b></td></tr>"
		       "<tr><td><b>Operator</b></td>"
		       "<td><b>Description</b>"
		       "</td></tr>\n",
		       TABLE_STYLE );
	// table of the query keywords
	long n = getNumFieldCodes();
	for ( long i = 0 ; i < n ; i++ ) {
		// get field #i
		QueryField *f = &g_fields[i];
		// print it out
		char *d = f->desc;
		// fix table internal cell bordering
		if ( d[0] == '\0' ) d = "&nbsp;";
		p.safePrintf("<tr bgcolor=#%s>"
			     "<td><b>%s</b>:</td><td>%s</td></tr>\n",
			     LIGHT_BLUE,f->text,d);
	}
	
	p.safePrintf("</table></center></body></html>");

	char* sbuf = p.getBufStart();
	long sbufLen = p.length();

	bool retval = g_httpServer.sendDynamicPage(s,
						   sbuf,
						   sbufLen,
						   -1/*cachetime*/);
	return 	retval;
}


bool printApiForPage ( SafeBuf *sb , long PAGENUM , CollectionRec *cr ) {

	if ( PAGENUM == PAGE_NONE ) return true;

	char *pageStr = s_pages[PAGENUM].m_filename;
	
	// unknown?
	if ( ! pageStr ) pageStr = "???";

	sb->safePrintf("<a name=/%s>",pageStr);//PAGENUM);


	sb->safePrintf("<div style=padding-left:10%%>"
		       "<font size=+2><b><a href=/%s>/%s</a></b></font>"
		       ,pageStr,pageStr);
	sb->safePrintf("</a>");

	// description of page
	sb->safePrintf("<font size=-0> - %s "
		       "[in <a href=/%s?showParms=1&format=xml>xml</a> "
		       "or <a href=/%s?showParms=1&format=json>json</a>] "
		       "or <a href=/%s>html</a>] "
		       "</font><br>",
		       s_pages[PAGENUM].m_desc,
		       pageStr,
		       pageStr,
		       pageStr);
	sb->safePrintf("</div><br>");
	
	// begin new list of centered tables
	sb->safePrintf("<center>");
	
	// and the start of the input parms table
	sb->safePrintf ( 
			"<table style=max-width:80%%; %s>"
			"<tr class=hdrow><td colspan=9>"
			"<center><b>Parms</b></tr></tr>"
			"<tr bgcolor=#%s>"
			"<td><b>#</b></td>"
			"<td><b>parm</b></td>"
			//"<td><b>Page</b></td>"
			"<td><b>Type</b></td>"
			"<td><b>Title</b></td>"
			"<td><b>Default Value</b></td>"
			"<td><b>Description</b></td></tr>\n"
			, TABLE_STYLE
			, DARK_BLUE );
	
	const char *blue = LIGHT_BLUE;
	long count = 1;

	//char *lastPage = NULL;
	//Parm *lastParm = NULL;

	for ( long i = 0; i < g_parms.m_numParms; i++ ) {
		Parm *parm = &g_parms.m_parms[i];
		// assume do not print
		//parm->m_pstr = NULL;
		// skip if hidden
		if ( parm->m_flags & PF_HIDDEN ) continue;
		if ( parm->m_type == TYPE_CMD ) continue;
		if ( parm->m_type == TYPE_COMMENT ) continue;

		if ( parm->m_flags & PF_DUP ) continue;
		if ( parm->m_flags & PF_NOAPI ) continue;
		if ( parm->m_flags & PF_DIFFBOT ) continue;
		//if ( ! (parm->m_flags & PF_API) ) continue;
		//if ( parm->m_page == PAGE_FILTERS ) continue;

		long pageNum = parm->m_page;

		// these have PAGE_NONE for some reason
		if ( parm->m_obj == OBJ_SI ) pageNum = PAGE_RESULTS;

		if ( pageNum != PAGENUM ) continue;

		if ( blue == (const char *)LIGHT_BLUE ) blue = DARK_BLUE;
		else if(blue==(const char *)DARK_BLUE ) blue = LIGHT_BLUE;


		SafeBuf tmp;
		char diff = 0;
		bool printVal = false;
		if ( (parm->m_obj == OBJ_COLL && cr) ||parm->m_obj==OBJ_CONF) {
			printVal = true;
			parm->printVal ( &tmp , cr->m_collnum , 0 );
			char *def = parm->m_def;
			if ( ! def && parm->m_type == TYPE_IP) 
				def = "0.0.0.0";
			if ( ! def ) def = "";
			if ( strcmp(tmp.getBufStart(),def) ) diff=1;
		}

		// print the parm
		if ( diff == 1 ) 
			sb->safePrintf ( "<tr bgcolor=orange>");
		else
			sb->safePrintf ( "<tr bgcolor=#%s>",blue);

		sb->safePrintf("<td>%li</td>",count++);

		// use m_cgi if no m_scgi
		char *cgi = parm->m_cgi;

		sb->safePrintf("<td><b>%s</b></td>", cgi);

		//sb->safePrintf("<td><nobr><a href=/%s?c=%s>/%s"
		//"</a></nobr></td>",
		//page,coll,page);

		sb->safePrintf("<td nowrap=1>");
		switch ( parm->m_type ) {
		case TYPE_BOOL: sb->safePrintf ( "BOOL (0 or 1)" ); break;
		case TYPE_BOOL2: sb->safePrintf ( "BOOL (0 or 1)" ); break;
		case TYPE_CHECKBOX: sb->safePrintf ( "BOOL (0 or 1)" ); break;
		case TYPE_CHAR: sb->safePrintf ( "CHAR" ); break;
		case TYPE_CHAR2: sb->safePrintf ( "CHAR" ); break;
		case TYPE_FLOAT: sb->safePrintf ( "FLOAT32" ); break;
		case TYPE_DOUBLE: sb->safePrintf ( "FLOAT64" ); break;
		case TYPE_IP: sb->safePrintf ( "IP" ); break;
		case TYPE_LONG: sb->safePrintf ( "INT32" ); break;
		case TYPE_LONG_LONG: sb->safePrintf ( "INT64" ); break;
		case TYPE_CHARPTR: sb->safePrintf ( "STRING" ); break;
		case TYPE_STRING: sb->safePrintf ( "STRING" ); break;
		case TYPE_STRINGBOX: sb->safePrintf ( "STRING" ); break;
		case TYPE_STRINGNONEMPTY:sb->safePrintf ( "STRING" ); break;
		case TYPE_SAFEBUF: sb->safePrintf ( "STRING" ); break;
		case TYPE_FILEUPLOADBUTTON: sb->safePrintf ( "STRING" ); break;
		default: sb->safePrintf("<b><font color=red>UNKNOWN</font></b>");
		}
		sb->safePrintf ( "</td><td>%s</td>",parm->m_title);
		char *def = parm->m_def;
		if ( ! def ) def = "";
		sb->safePrintf ( "<td>%s</td>",  def );
		sb->safePrintf ( "<td>%s",  parm->m_desc );
		if ( parm->m_flags & PF_REQUIRED )
			sb->safePrintf(" <b><font color=green>REQUIRED"
				     "</font></b>");
		if ( printVal ) {
			sb->safePrintf("<br><b><nobr>Current value:</nobr> ");
			// print in red if not default value
			if ( diff ) sb->safePrintf("<font color=red>");
			sb->safeTruncateEllipsis(tmp.getBufStart(),80);
			if ( diff ) sb->safePrintf("</font>");
			sb->safePrintf("</b>");
		}
		sb->safePrintf("</td>");
		sb->safePrintf ( "</tr>\n" );

	}
	
	// end input parm table we started below
	sb->safePrintf("</table><br>\n\n");

	//
	// done printing parm table
	//

	//
	// print output in xml
	//
	sb->safePrintf ( 
			"<table style=max-width:80%%; %s>"
			"<tr class=hdrow><td colspan=9>"
			"<center><b>XML Output</b></tr></tr>"
			"<tr><td bgcolor=%s>"
			, TABLE_STYLE
			, LIGHT_BLUE
			);


	// bool showParms = false;
	// if ( PAGENUM == PAGE_MASTER ||
	//      PAGENUM == PAGE_SPIDER ||
	//      PAGENUM == PAGE_SEARCH 
	//      ) 
	// 	showParms = true;


	sb->safePrintf("<pre style=max-width:500px;>\n");
	char *desc = NULL;
	bool printed = false;

	// for /admin/master /admin/spider /admin/search you can say
	// ?showparms=1 and get this listing
	// if ( showParms ) {
	// 	printed = true;
	// 	// print the page out but in xml
	// 	SafeBuf xb;
	// 	g_parms.printParms2 ( &xb ,
	// 			      PAGENUM ,
	// 			      cr ,
	// 			      1, // long nc , # cols?
	// 			      1, // long pd , printDesc?
	// 			      false , // isCrawlbot
	// 			      FORMAT_XML,
	// 			      NULL ); // TcpSocket *sock
	// 	// convert < to &lt; etc.
	// 	sb->htmlEncode ( xb.getBufStart() );
	// 	//SafeBuf hb;
	// 	//hb.htmlEncode ( xb.getBufStart() );
	// 	// then brify it
	// 	//sb->brify2 ( hb.getBufStart() , 80 );
	// }

	// example output in xml
	if ( ! desc )
		desc = "<response>\n"
			"\n"
			"\t# a numeric status code. 0 means success.\n"
			"\t<statusCode>0</>\n"
			"\n"
			"\t# the status as a string\n"
			"\t<statusMsg>Success</>\n"
			"\n"
			"</response>\n";

	if ( ! printed ) {
		printed = true;
		sb->htmlEncode ( desc);
	}


	sb->safePrintf("</pre>");
	sb->safePrintf ( "</td></tr></table><br>\n\n" );
	
	//
	// print output in json
	//
	sb->safePrintf ( 
			"<table style=max-width:80%%; %s>"
			"<tr class=hdrow><td colspan=9>"
			"<center><b>JSON Output</b></tr></tr>"
			"<tr><td bgcolor=%s>"
			, TABLE_STYLE
			, LIGHT_BLUE
			);
	sb->safePrintf("<pre>\n");

	desc = NULL;
	printed = false;

	// for /admin/master /admin/spider /admin/search you can say
	// ?showparms=1 and get this listing
	// if ( showParms ) {
	// 	printed = true;
	// 	// print the page out but in json
	// 	g_parms.printParms2 ( sb ,
	// 			      PAGENUM,
	// 	 		      cr ,
	// 	 		      1 , // long nc , # cols?
	// 	 		      1 , // long pd , print desc?
	// 	 		      false , // isCrawlbot
	// 	 		      FORMAT_JSON,
	// 	 		      NULL ); // TcpSocket *sock
	// }

	if ( ! desc )
		desc = "{ \"response:\"{\n"
			"\n"
			"\t# a numeric status code. "
			"0 means success.\n"
			"\t\"statusCode\":0,\n"
			"\n"
			"\t# the status as a string\n"
			"\t\"statusMsg\":\"Success\"\n"
			"\t}\n"
			"}\n";

	//if ( ! printed )
	//	sb->htmlEncode ( desc);

	if ( ! printed )
		sb->safeStrcpy ( desc);

	sb->safePrintf("</pre>");
	sb->safePrintf ( "</td></tr></table><br>\n\n" );
	
	sb->safePrintf("</center>");
	
	return true;
}



// any admin page calls cr->hasPermission ( hr ) and if that returns false
// then we call this function to give users a chance to login
bool sendPageLogin ( TcpSocket *socket , HttpRequest *hr ) {

	// get the collection
	long  collLen = 0;
	char *coll    = hr->getString("c",&collLen);

	// default to main collection. if you can login to main then you
	// are considered the root admin here...
	if ( ! coll ) coll = "main";

	SafeBuf emsg;

	// does collection exist? ...who cares, proxy doesn't have coll data.
	CollectionRec *cr = g_collectiondb.getRec ( hr );
	if ( ! cr )
		emsg.safePrintf("Collection \"%s\" does not exist.",coll);

	// just make cookie same format as an http request for ez parsing
	//char cookieData[2024];

	SafeBuf sb;

	// print colors
	g_pages.printColors ( &sb );
	// start table
	sb.safePrintf( "<table><tr><td>");
	// print logo
	g_pages.printLogo   ( &sb , coll );

	// get password from cgi parms OR cookie
	char *pwd = hr->getString("pwd");
	if ( ! pwd ) pwd = hr->getStringFromCookie("pwd");
	// fix "pwd=" cookie (from logout) issue
	if ( pwd && ! pwd[0] ) pwd = NULL;

	bool hasPermission = false;

	// this password applies to ALL collections. it's the root admin pwd
	if ( cr && pwd && g_conf.isRootAdmin ( socket , hr ) ) 
		hasPermission = true;

	if ( emsg.length() == 0 && ! hasPermission && pwd )
		emsg.safePrintf("Master password incorrect");


	// sanity
	if ( hasPermission && emsg.length() ) { char *xx=NULL;*xx=0; }

	// what page are they originally trying to get to?
	long page = g_pages.getDynamicPageNumber(hr);

	// try to the get reference Page
	long refPage = hr->getLong("ref",-1);
	// if they cam to login page directly... to to basic page then
	if ( refPage == PAGE_LOGIN ||
	     refPage == PAGE_LOGIN2 ||
	     refPage < 0 )
		refPage = PAGE_BASIC_SETTINGS;

	// if they had an original destination, redirect there NOW
	WebPage *pagePtr = g_pages.getPage(refPage);

	/*
	char *cookie = NULL;
	if ( hasPermission ) {
		// "pwd" could be NULL... like when it is not required,
		// perhaps only the right ip address is required, but if it
		// is there then store it in a cookie with no expiration
		//if ( pwd ) sprintf ( cookieData, "pwd=%s;expires=0;",pwd);
		// and redirect to it
		sb.safePrintf("<meta http-equiv=\"refresh\" content=\"0;"
			      "/%s?c=%s\">", page->m_filename,coll);
		// return cookie in server reply if pwd was non-null
		cookie = cookieData;
	}
	*/

	char *ep = emsg.getBufStart();
	if ( !ep ) ep = "";

	char *ff = "admin/settings";
	if ( pagePtr ) ff = pagePtr->m_filename;
	
	sb.safePrintf(
		      "&nbsp; &nbsp; "
		      "</td><td><font size=+1><b>Login</b></font></td></tr>"
		      "</table>" 
		      "<form method=post action=\"/%s\" name=f>"
		      , ff );

	sb.safePrintf(
		  "<input type=hidden name=ref value=\"%li\">"
		  "<center>"
		  "<br><br>"
		  "<font color=ff0000><b>%s</b></font>"
		  "<br><br>"
		  "<br>"

		  "<table cellpadding=2><tr><td>"

		  //"<b>Collection</td><td>"
		  "<input type=hidden name=c size=30 value=\"%s\">"
		  //"</td><td></td></tr>"
		  //"<tr><td>"

		  "<b>Master Password : &nbsp; </td>"
		  "<td><input id=ppp type=password name=pwd size=30>"
		  "</td><td>"
		  "<input type=submit value=ok border=0 onclick=\""
		  "document.cookie='pwd='+document.getElementById('ppp')"
		  ".value+"
		  "';expires=0';"
		  "\"></td>"
		  "</tr></table>"
		  "</center>"
		  "<br><br>"
		  , page, ep , coll );

	// print the tail
	//g_pages.printTail ( &sb , hr->isLocal() ); // pwd
	// send the page
	return g_httpServer.sendDynamicPage ( socket , 
					      sb.getBufStart(),
					      sb.length(),
					      -1    , // cacheTime
					      false , // POSTReply?
					      NULL  , // contentType
					      -1   ,
					      NULL);// cookie
}

bool printRedBox2 ( SafeBuf *sb , bool isRootWebPage ) {
	SafeBuf mb;
	// return false if no red box
	if ( ! printRedBox ( &mb , isRootWebPage ) ) return false;
	// otherwise, print it
	sb->safeStrcpy ( mb.getBufStart() );
	// return true since we printed one
	return true;
}

// emergency message box
bool printRedBox ( SafeBuf *mb , bool isRootWebPage ) {

	PingServer *ps = &g_pingServer;

	char *box = 
		"<table cellpadding=5 "
		// full width of enclosing div
		"width=100%% "
		"style=\""
		"background-color:#ff6666;"
		"border:2px #8f0000 solid;"
		"border-radius:5px;"
		//"max-width:500px;"
		"\" "
		"border=0"
		">"
		"<tr><td>";
	char *boxEnd =
		"</td></tr></table>";

	long adds = 0;


	mb->safePrintf("<div style=max-width:500px;>");

	// are we just starting off? give them a little help.
	CollectionRec *cr = g_collectiondb.getRec("main");
	if ( g_collectiondb.m_numRecs == 1 && 
	     cr &&
	     isRootWebPage &&
	     cr->m_globalCrawlInfo.m_pageDownloadAttempts == 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Welcome to Gigablast. The most powerful "
			       "search engine you can legally download. "
			       "Please add the websites you want to spider "
			       "<a href=/admin/settings?c=main>here</a>."
			       );
		mb->safePrintf("%s",boxEnd);
	}

	if ( isRootWebPage ) {
		mb->safePrintf("</div>");
		return (bool)adds;
	}

	if ( g_conf.m_numConnectIps == 0 && g_conf.m_numMasterPwds == 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("URGENT. Please specify a password "
			       "or IP address in the "
			       "<a href=/admin/security>security</a> "
			       "table. Right now anybody might be able "
			       "to access the Gigablast admin controls.");
		mb->safePrintf("%s",boxEnd);
	}

	// out of disk space?
	long out = 0;
	for ( long i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
		Host *h = &g_hostdb.m_hosts[i];
		if ( h->m_diskUsage < 98.0 ) continue;
		out++;
	}
	if ( out > 0 ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		char *s = "s are";
		if ( out == 1 ) s = " is";
		mb->safePrintf("%s",box);
		mb->safePrintf("%li host%s over 98%% disk usage. "
			       "See the <a href=/admin/hosts>"
			       "hosts</a> table.",out,s);
		mb->safePrintf("%s",boxEnd);
	}


	if ( g_pingServer.m_hostsConfInDisagreement ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("The hosts.conf or localhosts.conf file "
			      "is not the same over all hosts.");
		mb->safePrintf("%s",boxEnd);
	}

	if ( g_rebalance.m_isScanning ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Rebalancer is currently running.");
		mb->safePrintf("%s",boxEnd);
	}
	// if any host had foreign recs, not that
	char *needsRebalance = g_rebalance.getNeedsRebalance();
	if ( ! g_rebalance.m_isScanning &&
	     needsRebalance &&
	     *needsRebalance ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("A host requires a shard rebalance. "
			      "Click 'rebalance shards' in master controls "
			       "to rebalance all hosts.");
		mb->safePrintf("%s",boxEnd);
	}

	if ( ps->m_numHostsDead ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		char *s = "hosts are";
		if ( ps->m_numHostsDead == 1 ) s = "host is";
		mb->safePrintf("%s",box);
		mb->safePrintf("%li %s dead and not responding to "
			      "pings.",ps->m_numHostsDead ,s );
		mb->safePrintf("%s",boxEnd);
	}

	if ( ! g_conf.m_useThreads || g_threads.m_disabled ) {
		if ( adds ) mb->safePrintf("<br>");
		adds++;
		mb->safePrintf("%s",box);
		mb->safePrintf("Threads are disabled. Severely hurts "
			      "performance.");
		mb->safePrintf("%s",boxEnd);
	}

	mb->safePrintf("</div>");

	return (bool)adds;
}
