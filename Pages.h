// Copyright Matt Wells Sep 2004

// used for sending all dynamic html pages

#ifndef _PAGES_H_
#define _PAGES_H_

bool printRedBox2 ( SafeBuf *sb , bool isRootWebPage = false ) ;
bool printRedBox ( SafeBuf *mb , bool isRootWebPage = false ) ;

// for PageEvents.cpp and Accessdb.cpp
//#define RESULTSWIDTHSTR "550px"

#include "TcpSocket.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "SafeBuf.h"
#include "PageCrawlBot.h" // sendPageCrawlBot()

#define LIGHTER_BLUE "e8e8ff"
#define LIGHT_BLUE "d0d0e0"
#define DARK_BLUE  "c0c0f0"
#define DARKER_BLUE  "a0a0f0"
#define DARKEST_BLUE  "8080f0"
#define TABLE_STYLE " style=\"border-radius:10px;border:#6060f0 2px solid;\" width=100% bgcolor=#a0a0f0 cellpadding=4 border=0 "

extern char *g_msg;

// . declare all dynamic functions here
// . these are all defined in Page*.cpp files
// . these are called to send a dynamic page
bool sendPageBasicSettings   ( TcpSocket *s , HttpRequest *r );
bool sendPageBasicStatus     ( TcpSocket *s , HttpRequest *r );
//bool sendPageBasicDiffbot    ( TcpSocket *s , HttpRequest *r );



bool sendPageRoot     ( TcpSocket *s , HttpRequest *r );
bool sendPageRoot     ( TcpSocket *s , HttpRequest *r, char *cookie );
bool sendPageResults  ( TcpSocket *s , HttpRequest *r );
//bool sendPageWidget   ( TcpSocket *s , HttpRequest *r );
//bool sendPageEvents   ( TcpSocket *s , HttpRequest *r );
bool sendPageAddUrl   ( TcpSocket *s , HttpRequest *r );
bool sendPageGet      ( TcpSocket *s , HttpRequest *r );
bool sendPageLogin    ( TcpSocket *s , HttpRequest *r );
bool sendPageStats    ( TcpSocket *s , HttpRequest *r );
bool sendPageHosts    ( TcpSocket *s , HttpRequest *r );
bool sendPageSockets  ( TcpSocket *s , HttpRequest *r );
bool sendPageLog      ( TcpSocket *s , HttpRequest *r );
bool sendPageMaster   ( TcpSocket *s , HttpRequest *r );
bool sendPageSitedb   ( TcpSocket *s , HttpRequest *r );
//bool sendPageSync     ( TcpSocket *s , HttpRequest *r );
bool sendPagePerf     ( TcpSocket *s , HttpRequest *r );
bool sendPageIndexdb  ( TcpSocket *s , HttpRequest *r );
bool sendPageTitledb  ( TcpSocket *s , HttpRequest *r );
bool sendPageParser   ( TcpSocket *s , HttpRequest *r );
bool sendPageSecurity ( TcpSocket *s , HttpRequest *r );
bool sendPageAddColl  ( TcpSocket *s , HttpRequest *r );
bool sendPageDelColl  ( TcpSocket *s , HttpRequest *r );
//bool sendPageOverview ( TcpSocket *s , HttpRequest *r );
bool sendPageSpiderdb ( TcpSocket *s , HttpRequest *r );
bool sendPageFilters  ( TcpSocket *s , HttpRequest *r );
bool sendPageReindex  ( TcpSocket *s , HttpRequest *r );
bool sendPageInject   ( TcpSocket *s , HttpRequest *r );
//bool sendPageMatchingQueries ( TcpSocket *s , HttpRequest *r );
bool sendPageSEO      ( TcpSocket *s , HttpRequest *r );
bool sendPageAccess   ( TcpSocket *s , HttpRequest *r );
bool sendPageSearch2  ( TcpSocket *s , HttpRequest *r );
bool sendPageAddUrl2  ( TcpSocket *s , HttpRequest *r );
bool sendPageGeneric  ( TcpSocket *s , HttpRequest *r ); // in Parms.cpp
bool sendPageCatdb    ( TcpSocket *s , HttpRequest *r );
bool sendPageDirectory ( TcpSocket *s , HttpRequest *r );
bool sendPageSpamr     ( TcpSocket *s , HttpRequest *r );
bool sendPageAutoban   ( TcpSocket *s , HttpRequest *r );
//bool sendPageTopDocs   ( TcpSocket *s , HttpRequest *r );
bool sendPageTopics    ( TcpSocket *s , HttpRequest *r );
//bool sendPageSpiderLocks ( TcpSocket *s , HttpRequest *r );
bool sendPageLogView    ( TcpSocket *s , HttpRequest *r );
bool sendPageProfiler   ( TcpSocket *s , HttpRequest *r );
bool sendPageReportSpam ( TcpSocket *s , HttpRequest *r );
bool sendPageSpam       ( TcpSocket *s , HttpRequest *r );
bool sendPageThreads    ( TcpSocket *s , HttpRequest *r );
bool sendPageNetTest    ( TcpSocket *s , HttpRequest *r );
bool sendPageAPI        ( TcpSocket *s , HttpRequest *r );
bool sendPageWordVec   ( TcpSocket *s , HttpRequest *r );
bool sendPageQualityAgent   ( TcpSocket *s , HttpRequest *r );
bool sendPageThesaurus  ( TcpSocket *s , HttpRequest *r );
bool sendPageStatsdb   ( TcpSocket *s , HttpRequest *r );

// . description of a dynamic page
// . we have a static array of these in Pages.cpp
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



class Pages {

 public:

	WebPage *getPage ( long page ) ;

	// . associate each page number (like PAGE_SEARCH) with a callback
	//   to which we pass the HttpRequest and TcpSocket
	// . associate each page with a security level
	void init ( );

	// return page number for a filename
	// returns -1 if page not found
	long getPageNumber ( char *filename );

	// a request like "GET /sockets" should return PAGE_SOCKETS
	long getDynamicPageNumber ( HttpRequest *r ) ;

	char *getPath ( long page ) ;

	long getNumPages ( );
	// this passes control to the dynamic page generation routine based
	// on the path of the GET request
	bool sendDynamicReply ( TcpSocket *s , HttpRequest *r , long page );

	// . each dynamic page generation routine MUST call this to send
	//   its reply to the requesting client's browser
	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool sendPage ( TcpSocket *s           ,
			char      *page        ,
			long       pageLen     ,
			long       cacheTime   ,
			bool       POSTReply   ,
			char      *contentType ) ;


	bool broadcastRequest ( TcpSocket *s , HttpRequest *r , long page ) ;

	// . returns USER_PUBLIC, USER_MASTER, USER_ADMIN or USER_SPAM
	// . used to determine if the client browser has the permission
	//long getUserType ( TcpSocket *s , HttpRequest *r ) ;



	bool getNiceness ( long page );

	//
	// HTML generation utility routines
	// . (always use the safebuf versions when possible)
	// . also, modify both if you modify either!

	bool printAdminTop 	       ( SafeBuf     *sb   ,
				  TcpSocket   *s    ,
				  HttpRequest *r    ,
				  char        *qs = NULL,
				  char* bodyJavascript = "" );


	bool printAdminTop2 	       ( SafeBuf     *sb   ,
					 TcpSocket   *s    ,
					 HttpRequest *r    ,
					 char        *qs      = NULL,
					 char	     *scripts = NULL,
					 long	      scriptsLen = 0);

	bool printAdminTop2            ( SafeBuf *sb    ,
					 long    page   ,
					 //long    user   ,
					 char   *username,
					 char   *coll   ,
					 char   *pwd    ,
					 long    fromIp ,
					 char        *qs      = NULL,
					 char	     *scripts = NULL,
					 long	      scriptsLen = 0);

	void printFormTop(  SafeBuf *sb, HttpRequest *r );
	void printFormData( SafeBuf *sb, TcpSocket *s, HttpRequest *r );

	//char *printAdminBottom       ( char *p, char *pend, HttpRequest *r );
	//char *printAdminBottom       ( char *p, char *pend);
	bool  printAdminBottom         ( SafeBuf *sb, HttpRequest *r );
	bool  printAdminBottom         ( SafeBuf *sb);
	bool  printAdminBottom2        ( SafeBuf *sb, HttpRequest *r );
	bool  printAdminBottom2        ( SafeBuf *sb);
	bool  printTail                ( SafeBuf* sb, 
					 bool isLocal );
	bool printSubmit ( SafeBuf *sb ) ;
					 //long user , 
					 //char *username,
					 //char *pwd );
	//char *printTail                ( char *p    ,
	//				 char *pend ,
	//				 bool isLocal );
	//long  user ,
	//char *username,
	//char *pwd  ) ;
	bool  printColors              ( SafeBuf *sb , char* bodyJavascript = "" ) ;
	//char *printColors              ( char *p , char *pend , 
	//				 char* bodyJavascript = "");

	//char *printColors2           ( char *p , char *pend ) ;
	bool  printColors3	       ( SafeBuf *sb ) ;
	//char *printFocus             ( char *p , char *pend ) ;
	bool  printLogo                ( SafeBuf *sb, char *coll ) ;
	//char *printLogo              ( char *p , char *pend , char *coll ) ;
	bool  printHostLinks           ( SafeBuf *sb  ,
					 long  page   ,
					 char *username ,
					 char *password ,
					 char *coll   ,
					 char *pwd    ,
					 long  fromIp ,
					 char *qs = NULL ) ;
	/*
	char *printHostLinks           ( char *p      ,
					 char *pend   ,
					 long  page   ,
					 char *coll   ,
					 char *pwd    ,
					 long  fromIp ,
					 char *qs = NULL ) ;
	*/
	bool  printAdminLinks          ( SafeBuf *sb, 
					 long  page ,
					 char *coll ,
					 bool isBasic );
	/*
	char *printAdminLinks          ( char *p    , 
					 char *pend , 
					 long  page ,
					 //long  user ,
					 char *username,
					 char *coll ,
					 char *pwd  ,
					 bool  top  ) ;
	*/
	bool  printCollectionNavBar ( SafeBuf *sb     ,
				      long  page     ,
				      //long  user     ,
				      char *username,
				      char *coll     ,
				      char *pwd      ,
				      char *qs       );
	/*
	char *printCollectionNavBar    ( char *p    ,
					 char *pend , 
					 long  page ,
					 //long  user ,
					 char *username,
					 char *coll ,
					 char *pwd  ,
					 char *qs = NULL );
	*/
	/*
	bool printRulesetDropDown ( SafeBuf *sb        ,
				    long  user         ,
				    char *cgi          ,
				    long  selectedNum  ,
				    long  subscript    );

	char *printRulesetDropDown     ( char *p           , 
					 char *pend        ,
					 long  user        ,
					 char *cgi         ,
					 long  selectedNum ,
					 long  subscript   ) ;

	char *printRulesetDescriptions ( char *p , char *pend , long user ) ;
	bool  printRulesetDescriptions ( SafeBuf *sb , long user ) ;
	*/
};

extern class Pages g_pages;


// . each dynamic page has a number
// . some pages also have urls like /search to mean page=0
enum {
	// dummy pages
	PAGE_NOHOSTLINKS = 0,
	PAGE_ADMIN     ,
	//PAGE_QUALITY   ,
	PAGE_PUBLIC    ,

	// public pages
	PAGE_ROOT        ,
	PAGE_RESULTS     ,
	//PAGE_WIDGET,
	PAGE_ADDURL      , // 5
	PAGE_GET         ,
	PAGE_LOGIN       ,
	PAGE_DIRECTORY   ,
	PAGE_REPORTSPAM  ,
	//PAGE_WORDVECTOR  ,

	// basic controls page /admin/basic
	PAGE_BASIC_SETTINGS ,
	PAGE_BASIC_STATUS ,
	//PAGE_BASIC_SEARCH , // TODO
	//PAGE_BASIC_DIFFBOT , // TODO
	PAGE_BASIC_SECURITY ,
	PAGE_BASIC_SEARCH ,

	// master admin pages
	PAGE_MASTER      , 
	PAGE_SEARCH      ,  
	PAGE_SPIDER      ,
	PAGE_SPIDERPROXIES ,
	PAGE_LOG         ,
	PAGE_SECURITY    ,
	PAGE_ADDCOLL     ,	
	PAGE_DELCOLL     , 
	PAGE_REPAIR      ,
	//PAGE_SITES , // site filters
	PAGE_FILTERS     ,
	PAGE_INJECT      , 
	PAGE_ADDURL2     ,
	PAGE_REINDEX     ,	

	PAGE_HOSTS       ,
	PAGE_STATS       , // 10
	PAGE_STATSDB	 ,
	PAGE_PERF        ,
	PAGE_SOCKETS     ,

	PAGE_LOGVIEW     ,
//	PAGE_SYNC        , 
	PAGE_AUTOBAN     , // 20
	//PAGE_SPIDERLOCKS ,
	PAGE_PROFILER    ,
	PAGE_THREADS     ,

//	PAGE_THESAURUS   , 

	// . non master-admin pages (collection controls)
	// . PAGE_OVERVIEW acts as a cutoff point (search Parms.cpp for it)
	//PAGE_OVERVIEW    ,  //25
	PAGE_API ,

	PAGE_RULES       ,
	PAGE_INDEXDB     ,  //30
	PAGE_TITLEDB     ,  
	//PAGE_STATSDB	 ,

	PAGE_CRAWLBOT    , // 35
	PAGE_SPIDERDB    , 
	//PAGE_PRIORITIES  ,  // priority queue controls
	//PAGE_KEYWORDS    ,
	PAGE_SEO         ,
	PAGE_ACCESS      ,  //40	
	PAGE_SEARCHBOX   ,
	PAGE_PARSER      ,
	PAGE_SITEDB      ,  
	PAGE_CATDB       ,
	PAGE_LOGIN2      ,
//	PAGE_TOPDOCS     ,
// 	PAGE_TOPICS      ,
// 	PAGE_SPAM        ,
//	PAGE_QAGENT      ,
//	PAGE_NETTEST     ,
	//PAGE_ADFEED      ,
//	PAGE_TURK2       ,
	PAGE_INFO        , 
	PAGE_NONE     	};
	

#endif
