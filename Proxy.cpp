#include "gb-include.h"

#include "Proxy.h"
#include "Statsdb.h"
#include "Msg13.h"
#include "XmlDoc.h"
//#include "seo.h" // g_secret_tran_key and api_key


char *g_secret_tran_key = NULL;
char *g_secret_api_key  = NULL;


Proxy g_proxy;


// turn this off now
//bool g_isYippy = true;
bool g_isYippy = false;

#define MINCHARGE 5.00


static void gotTcpReplyWrapper ( void *state , TcpSocket *s ) ;

static void proxyHandlerWrapper ( TcpSocket *s );
//static void gotReplyWrapperPage( void *state, TcpSocket *s );
static void gotHttpReplyWrapper ( void *state, UdpSlot *slot ) ;
//static void gotDataFeedRequestWrapper( void *state );
static void uncountStripe ( class StateControl *stC ) ;

static bool sendPageAccount ( class TcpSocket *s , class HttpRequest *r ) ;

struct StateControl{
	int32_t m_userId32;
	float m_price;
	int32_t m_accessType;

	int32_t m_pageNum;
	bool m_isYippySearch;
	int64_t m_start;
	int32_t m_reqNum;
	SafeBuf m_sb;
	TcpSocket *m_s;
	int64_t m_startTime;
	bool m_isQuery;
	uint32_t m_hash;
	int32_t m_hostId;
	int32_t m_raw;
	int32_t m_stripe        ;
	int32_t m_numQueryTerms ;
	// hash32() of "code"
	int32_t m_ch;
	UdpSlot *m_slot;
	char    *m_slotReadBuf;
	int32_t     m_slotReadBufMaxSize;
	//int32_t     m_forward;
	int32_t     m_retries;
	int64_t     m_timeout;
	HttpRequest m_hr;
	Host *m_forwardHost;
	float m_pending;
	bool m_isEventGuru;
};

#define UIF_ADMIN   0x01
#define UIF_OLDUSER 0x02

// PageRoot.cpp's pageaddurl now uses userinfo...
class UserInfo {
public:
	// unique userid
	int32_t m_userId32; 
	// outstanding requests costs this much
	float m_pending;
	int32_t m_signUpDate;
	// strings include terminating \0
	char m_login[32];
	char m_password[32];
	char m_xmlFeedCode[16];
	char m_creditCardNum[64]; // no punct, just digits
	char m_cvv[5]; // "1234\0"
	char m_creditCardExpires[6]; // "05/13\0"
	char m_creditCardType[32]; // visa

	// new stuff
	char m_email[80];
	char m_phone[30];

	// european stuff for credit card processing
	char m_firstName[40];
	char m_lastName[40];
	char m_address[80];
	char m_city[30];
	char m_state[30];
	char m_country[30];
	char m_zip[20];

	// session info:
	int64_t m_lastSessionId64;
	int32_t m_lastActionTime;
	int32_t m_lastLoginIP;
	float m_accountBalance;

	int32_t m_flags;
};

// values for SummaryRec::m_accessType
#define AT_SEARCHFEED_OLD       1
#define AT_SEARCHFEED_NEW       2
#define AT_COMPETITOR_BACKLINKS 3
#define AT_RELATED_QUERIES      4
#define AT_MISSING_TERMS        5
#define AT_MATCHING_QUERIES     6
#define AT_COMPETITOR_PAGES     7
#define AT_ADDURL               8
#define MAX_ACCESS_TYPE         8


static void freeStateControl ( StateControl *stC );

Proxy::Proxy() {
	m_proxyId = -1;
	m_proxyRunning = false;
	for (int32_t i =0; i < MAX_HOSTS; i++)
		m_numOutstanding[i] = 0;
	for ( int32_t i = 0 ; i < MAX_STRIPES ; i++ ) {
		m_termsOutOnStripe   [i] = 0;
		m_queriesOutOnStripe [i] = 0;
		m_stripeLastHostId   [i] = -1;
	}
	m_nextStripe = 0;
	m_lastHost = 0;
	m_mainHost = 0;
        //m_msg3c    = NULL;
}

Proxy::~Proxy() {
	/*
        if(m_msg3c) {
                // free the msg3c
                mdelete(m_msg3c, sizeof(Msg3c), "Proxy-Msg3c");
                delete m_msg3c;
                m_msg3c = NULL;
        }
	*/
}

bool Proxy::initHttpServer ( uint16_t httpPort, 
			uint16_t httpsPort ) {
	if ( ! g_httpServer.init( httpPort, httpsPort, 
				  proxyHandlerWrapper ) ) {
		return false;
	}
	return true;
}

bool Proxy::initProxy ( int32_t proxyId, uint16_t udpPort,
			uint16_t udpPort2,UdpProtocol *dp ) {
	// let's ensure our core file can dump
	struct rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE,&lim) ){
		log("proxy: setrlimit: core: %s", mstrerror(errno) );
		return false;
	}
	//lim.rlim_cur = lim.rlim_max = 4000;
	//if ( setrlimit(RLIMIT_NOFILE,&lim) ){
	//	log("proxy: setrlimit: numfds: %s", mstrerror(errno) );
	//	return false;
	//}
	//log("proxy: set max fds to 4000");

	m_proxyId = proxyId;

	// set this in Hostdb too!
	g_hostdb.m_hostId = proxyId;

	m_proxyRunning = true;
	// load up hosts.conf
	/*char *hostsConf = "./hosts.conf";
	g_hostdb.reset();
	if ( ! g_hostdb.init(hostsConf, m_proxyId, NULL, 
			     true  ) ) {//isproxy
		log("db: hostdb init failed." ); return 1; }*/
	
	//gb.conf should be in the same directory as gb
 	//if ( ! g_conf.init ( "./" ) ) { // , h->m_hostId ) ) {
	//	log("db: Conf init failed." ); return 1; }

	//We log the http requests, althogh this is directly done by us
	g_conf.m_logHttpRequests = true;

	//Don't send email alerts, the machines on the cluster can do that
	//g_conf.m_sendEmailAlerts = false;

	// my new email
	
	// if proxy, always have autosave on so we can save user accouting
	// info regularly for billing feed access. save every 5 minutes.
	g_conf.m_autoSaveFrequency = 5;


	// no, now proxies do too! for out of socket conditions in tcpserver
	g_conf.m_sendEmailAlerts = true;

	g_conf.m_sendEmailAlertsToEmail1 = true;
	// verizon bought us out... smtp-sl.vtext.com
	// was messages.alltel.com
	strcpy ( g_conf.m_email1Addr , "5054503518@vtext.com");
	strcpy ( g_conf.m_email1From , "sysadmin@gigablast.com");
	// got ip 69.78.67.53 for 'smtp-sl.vtext.com'
	//strcpy ( g_conf.m_email1MX   , "gbmxrec-vtext.com");
	strcpy ( g_conf.m_email1MX   , "10.5.54.47");





	//if ( ! g_hostdb2.validateIps ( &g_conf ) ) {
	//	log("db: Failed to validate ips." ); return 1;}

	// init the loop, needs g_conf
	//if ( ! g_loop.init() ) {
	//	log("db: Loop init failed." ); return false; }
	
	// . autoban must be on
	// . MDW: why? leave it alone. not good for buzz.
	//g_conf.m_doAutoBan = true;
	if (!g_autoBan.init()){
		log("autoban: init failed.");
		return false;
	}
	
	//for pingserver
	//if ( ! g_hostdb.validateIps ( &g_conf ) ) {
	//	log("db: Failed to validate ips." ); return 1;}
	
	//if ( ! g_udpServer.init( udpPort ,dp,2/*niceness*/,
	//			  10000000 ,   // readBufSIze
	//			  10000000 ,   // writeBufSize
	//			  60       ,   // pollTime in ms
	//			  10000     )){ // max udp slots
	//	log("db: UdpServer init failed." ); return 0; 
	//}

	// start pinging right away, udpServer has already been init'ed
	if ( ! g_pingServer.init() ) {
		log("db: PingServer init failed." ); return false; 
	}

	if ( ! g_pingServer.registerHandler() ) 
		return false;

	//Also have to init pages because we need to know which requests to
	//forward. html/gif's, etc can be taken care here itself.
	g_pages.init ( );
	// load up the dmoz categories here
	char structureFile[256];
	sprintf(structureFile, "%scatdb/gbdmoz.structure.dat", g_hostdb.m_dir);
	g_categories = &g_categories1;
	if (g_categories->loadCategories(structureFile) != 0) {
		log("cat: Loading Categories From %s Failed.",
		    structureFile);
		//return 1;
	}
	log(LOG_INFO, "cat: Loaded Categories From %s.",
	structureFile);

	Msg13 msg13;	if ( ! msg13.registerHandler () ) return false;	

	// . then dns Distributed client
	// . server should listen to a socket and register with g_loop
	// . Only the distributed cache shall call the dns server.
	if ( ! g_dns.init( g_hostdb.m_myHost->m_dnsClientPort ) ) {
		log("db: Dns distributed client init failed." ); return 1; }

	MsgC msgc; if ( ! msgc.registerHandler() ) return false;

	//need to init collectiondb too because of addurl
	//set isdump to true because we aren't going to store any data in the
	//collection
	if ( !g_collectiondb.loadAllCollRecs( ) ){ //isDump
		log ("db: collectiondb init failed.");
		return false;
	}
	//init g_msg
	g_msg = "";

	// load accounting info
	if ( ! loadUserBufs ( ) )
		return log("proxy: failed to load user bufs");

	return true;
}

void proxyHandlerWrapper ( TcpSocket *s ){
	g_proxy.handleRequest (s);
}

static int32_t s_yippySearchesOut = 0;

bool Proxy::handleRequest (TcpSocket *s){

	// if we are a spider compression proxy, do not really act like
	// a proxy at all!
	if ( g_hostdb.m_myHost->m_type == HT_SCPROXY ) {
		//httprequest changes the buf
		g_httpServer.requestHandler(s);
		g_msg = "";
		return true;
	}

	/*
	char buf[MAX_REQ_LEN+10];
	int32_t bufSize = s->m_readOffset;
	if ( bufSize > MAX_REQ_LEN - 1 )
		bufSize = MAX_REQ_LEN - 1;
	gbmemcpy( buf, s->m_readBuf, bufSize );
	buf[bufSize] = '\0';
	*/

        //m_s = s;

	HttpRequest hr;

	//bool status = hr.set ( buf , bufSize , s ) ;
	bool status = hr.set ( s->m_readBuf , s->m_readOffset , s ) ;
	if ( ! status ) {
		// log a bad request
		log("http: Got bad request from %s: %s",
		    iptoa(s->m_ip),mstrerror(g_errno));
		// cancel the g_errno, we'll send a BAD REQUEST reply to them
		g_errno = 0;
		// . this returns false if blocked, true otherwise
		// . this sets g_errno on error
		// . this will destroy(s) if cannot malloc send buffer
		g_httpServer.sendErrorReply ( s , 400, "Bad Request" );
		return false;
	}

	bool isAdmin = g_conf.isMasterAdmin(s,&hr);

	int32_t redirLen = hr.getRedirLen() ;
	char *redir = NULL;
	if(redirLen > 0) redir = hr.getRedir();

	// redirect everyone away if we should
	if ( !redir &&
	     !isAdmin && 
	     // . we put this here to redirect all traffic somewhere else
	     // . you can set that url in the master controls
	     // . it only redirects there if the raw/code/site/sites is NULL
	     *g_conf.m_redirect != '\0' &&
	     hr.getLong("xml", -1) == -1 &&
	     hr.getLong("raw", -1) == -1 &&
	     hr.getString("code")  == NULL &&
	     hr.getString("site")  == NULL &&
	     hr.getString("sites") == NULL) {
		//direct all non-raw, non admin traffic away.
		redir = g_conf.m_redirect;
		redirLen = gbstrlen(g_conf.m_redirect);
	}


	// . we may be serving multiple hostnames
	// . www.gigablast.com, gigablast.com, www.inifinte.info,
	//   infinite.info, www.microdemocracy.com
	// . get the host: field from the MIME
	// . should be NULL terminated
	char *host  = hr.getHost();
	char *hdom = host;
	if ( strncasecmp(hdom,"www.",4) == 0 ) hdom += 4;
	if ( strncasecmp(hdom,"www2.",5) == 0 ) hdom += 5;
	if ( strncasecmp(hdom,"www1.",5) == 0 ) hdom += 5;
	// auto redirect eventguru.com to www.eventguru.com so cookies
	// are consistent
	if ( ! redir && 
	     ( strcasecmp ( host , "eventguru.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbit.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbits.com" ) == 0 ||
	       strcasecmp ( hdom , "flurpit.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbot.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbits.com" ) == 0 ||
	       strcasecmp ( hdom , "flurbyte.com" ) == 0 ||
	       strcasecmp ( hdom , "eventstereo.com" ) == 0 ||
	       strcasecmp ( hdom , "eventcrier.com" ) == 0 ||
	       strcasecmp ( hdom , "eventwidget.com" ) == 0 ) ) {
		redir = "http://www.eventguru.com/";
		redirLen = gbstrlen(redir);
	}

	bool isEventGuru = false;
	if ( strcasecmp(hdom,"eventguru.com") == 0 )
		isEventGuru = true;

#ifdef MATTWELLS
#define HTTPS_REDIR 1
#endif


	if ( redirLen > 0 && redir ) {
#ifdef HTTPS_REDIR
	redirect:
#endif
		HttpMime m;
		m.makeRedirMime (redir,redirLen);
		// . move the reply to a send buffer
		// . don't make sendBuf bigger than g_httpMaxSendBufSize
		int32_t sendBufSize = m.getMimeLen();
		if ( sendBufSize > g_conf.m_httpMaxSendBufSize ) 
			sendBufSize = g_conf.m_httpMaxSendBufSize;
		char *sendBuf    = (char *) mmalloc (sendBufSize,"HttpServer");
		if ( ! sendBuf ) 
			return g_httpServer.sendErrorReply(s,500,
							   mstrerror(g_errno));
		gbmemcpy ( sendBuf , m.getMime() , sendBufSize );
		// . send it away
		// . this returns false if blocked, true otherwise
		// . this sets g_errno on error
		// . this will destroy "s" on error
		// . "f" is the callback data
		// . it's passed to cleanUp() on completion/error before socket
		//   is recycled/destroyed
		// . this will call getMsgPiece() to fill up sendBuf from file
		TcpServer *tcp = s->m_this;
		if (  ! tcp->sendMsg ( s           , 
				       sendBuf     ,
				       sendBufSize ,
				       sendBufSize ,
				       sendBufSize ,
				       NULL        ,   // data for callback
				       NULL        ) ) // callback
			return false;
		// it didn't block or there was an error
		return true;
	}



	// . just requesting a static file, like rants.html or logo.gif?
	// . if so just handle that as a normal html/image file
	int32_t n = g_pages.getDynamicPageNumber ( &hr );
	char *path = hr.getPath();
	//int32_t pathLen = hr.getPathLen();

	// serve events on the gigablast.com domain:
	if ( path && strncmp(path,"/events",7) == 0 )
		isEventGuru = true;
	
	/*
	bool badPage = false;
	if ( n < 0 ) badPage = true;
	if ( hr.getRedirLen() > 0 ) badPage = true;
	if (pathLen == 11 && strncmp ( path , "/index.html" ,11 ) ==0 )
		badPage = false;
	if (pathLen >= 4 && strncmp ( path , "/seo" ,4 ) ==0 )
		badPage = false;
	if ( g_isYippy )
		badPage = false;
	if ( badPage ) {
		//httprequest changes the buf
		g_httpServer.requestHandler(s);
		g_msg = ""; 
		return true; 
	}
	*/

	// . i guess right now the proxy is handling admin pages itself
	// . i am changing this so that if &forward=<hostid> is in the url then

	//   the proxy forwards the control page request to the given hostid
	int32_t forward = hr.getLong("forward",-1);

	bool handleIt = true;
	if ( forward != -1       ) handleIt = false;
	//if ( n == PAGE_ROOT      ) handleIt = false;
	if ( n == PAGE_GET       ) handleIt = false;
	if ( n == PAGE_RESULTS   ) handleIt = false;
	if ( n == PAGE_DIRECTORY ) handleIt = false;

	// proxy now handles the shell addurl page, the actual request
	// made by the ajax in the shell to add the url has a &id= in it
	// and should go to the backend for adding the url. but we can
	// handle the shell, just the add url page html including the ajax
	// script.
	int32_t cgiId = hr.getLong("id",0);
	if ( n == PAGE_ADDURL && cgiId ) handleIt = false;

	// send this to gk144 
	if ( strncmp(path,"/seo",4) == 0 ) handleIt = false;

	if ( strncmp(path,"/seoapi",7) == 0 ) handleIt = true;

	// we're just a tcp proxy if yippy
	if ( g_isYippy ) 
		handleIt = false;

	// special pages. any html page will now need to call
	// msgfb to get the user info, like the name "Matt Wells" to
	// post in the black bar, so we can't really do this on the
	// proxy any more, we have to route all the way to the cluster.
	//if ( pathLen >= 7 && strnstr(path,".html",pathLen) ) handleIt =false;
	// same for xml... (eventguru.xml search provider list)
	//if ( pathLen >= 7 && strnstr(path,".xml",pathLen) ) handleIt = false;

	// for stats pages etc for yippy proxy
	if ( g_isYippy && ! strncmp(path,"/master",7) ) 
		handleIt = true;

	/*
	if ( ! strncmp(path,"/blog.html"   ,10) ) handleIt = false;
	if ( ! strncmp(path,"/terms.html"  ,11) ) handleIt = false;
	if ( ! strncmp(path,"/privacy.html",13) ) handleIt = false;
	if ( ! strncmp(path,"/account.html",13) ) handleIt = false;
	if ( ! strncmp(path,"/bio.html",13) ) handleIt = false;
	*/
	// our new cached page representation format
	if ( ! strncmp(path,"/?id="        ,5 ) ) handleIt = false;


	// log the request iff filename does not end in .gif .jpg .
	char *f = NULL;
	int32_t  flen = 0;
	if ( isEventGuru ) {
		f     = hr.getFilename();
		flen  = hr.getFilenameLen();
	}

	// proxy will handle eventguru images i guess
	bool  isGif = ( f && flen >= 4 && strncmp(&f[flen-4],".gif",4) == 0 );
	bool  isJpg = ( f && flen >= 4 && strncmp(&f[flen-4],".jpg",4) == 0 );
	bool  isBmp = ( f && flen >= 4 && strncmp(&f[flen-4],".bmp",4) == 0 );
	bool  isPng = ( f && flen >= 4 && strncmp(&f[flen-4],".png",4) == 0 );
	bool  isIco = ( f && flen >= 4 && strncmp(&f[flen-4],".ico",4) == 0 );
	bool  isPic = (isGif | isJpg | isBmp | isPng || isIco);

	// use event guru favicon?
	//if ( isEventGuru && isIco && strcmp(f,"favicon.ico") == 0 ) {
	//	f = "eventguru_favicon.ico";
	//	flen = gbstrlen(f);
	//}

	// eventguru.com host: in mime?
	if ( isEventGuru && ! isPic )
		handleIt = false;

	// only proxy holds the accounting info
	if ( ! strncmp ( path ,"/account", 8 ) ) {
		printRequest(s, &hr);
		return sendPageAccount ( s , &hr );
	}


	// get the server this socket uses
	TcpServer *tcp = s->m_this;
	int32_t max;
	if ( tcp == &g_httpServer.m_ssltcp ) max = g_conf.m_httpsMaxSockets;
	else                                 max = g_conf.m_httpMaxSockets;

#ifdef HTTPS_REDIR
	// if hitting root page then tell them to go to https
	// if not autobanned... but if it is an autobanned request on root
	// page it should have go the turing test above!
	if ( n == PAGE_ROOT && 
	     ! g_isYippy &&
	     // not event guru homepage
	     ! isEventGuru &&
	     // if not already on https
	     tcp != &g_httpServer.m_ssltcp &&
	     // do not redirect http://www.gigablast.com/?c=dmoz3 (directory)!
	     hr.m_cgiBufLen <=1 ) {
		// redirect to https site
		redir = "https://www.gigablast.com/";
		//
		// if we are gk267 then redirect to https://www2.gigablast.com/
		//
		static int32_t s_ip2 = 0;
		if ( ! s_ip2 ) s_ip2 = atoip("10.5.56.77");
		if ( (int32_t)g_hostdb.m_myIp == s_ip2 )
			redir = "https://www2.gigablast.com/";
		redirLen = gbstrlen(redir);
		goto redirect;
	}
#endif


	// . page addurl uses the udpserver to send the addurl stuff to one of
	//   the hosts, so we need udpserver.
	// . handle the request ourselves if it is not one of these
	//   pages and "forward" was not specified in the url cgi fields
	if ( handleIt ) {
		//httprequest changes the buf
		g_httpServer.requestHandler(s);
		g_msg = "";
		return true;
	}

	// limit yippy's "GET /search" requests to 50 out...
	int32_t ymax = 150; // 150;//50;//25; 300 for one process is good
	ymax = g_conf.m_maxYippyOut;
	bool isYippySearch = false;
	if ( g_isYippy && ! strncmp(path,"/search?",8) )
		isYippySearch = true;

	// is it a search request from a toolbar? we do not want to fuck
	// with those requests with the anti-bot code below
	bool isYippyToolBarRequest = false;
	if ( g_isYippy && isYippySearch ) {
		int32_t iflen;
		char *ifs = hr.getString("input-form",&iflen,NULL);
		if ( ! ifs ) isYippyToolBarRequest = true;
	}

	// just a safety catch
	if ( max < 20 ) max = 20;

	// enforce the open socket quota iff not admin and not from intranet
	if ( ! isAdmin && tcp->m_numIncomingUsed >= max && 
	     !tcp->closeLeastUsed()) {
		static int32_t s_last = 0;
		static int32_t s_count = 0;
		int32_t now = getTimeLocal();
		if ( now - s_last < 5 ) 
			s_count++;
		else {
			log("query: Too many sockets open. Sending 500 "
			    "http status code to %s. (msgslogged=%"INT32")[2]",
			    iptoa(s->m_ip),s_count);
			s_count = 0;
			s_last = now;
		}
		g_stats.m_closedSockets++;; 
		return g_httpServer.sendErrorReply ( s , 500 , 
						     "Too many sockets open.");
	}

	//
	// yippy traffic control
	//
	if ( isYippySearch && s_yippySearchesOut >= ymax ) {
		//
		// log a note
		//
		bool banned = false;
		int32_t yqLen;
		char *yq = hr.getString("query",&yqLen,NULL);
		if ( ! yq ) yq = "";
		if( ! isYippyToolBarRequest &&
		    !g_autoBan.hasPerm(s->m_ip, 
				      NULL,0,//code, codeLen, 
				      0,0,//uip, uipLen, 
				      s, &hr, NULL,//&testBuf,
				      true ) )  { // justCheck )) 
			banned = true;
			log("proxy: got banned search req cutoff %s (%s)",
			    iptoa(s->m_ip),yq);
		}
		else
			log("proxy: got cutoff search req %s (%s)",
			    iptoa(s->m_ip),yq);


		if ( ! banned ) {
			static int32_t s_last = 0;
			static int32_t s_count = 0;
			int32_t now = getTimeLocal();
			if ( now - s_last < 5 ) 
				s_count++;
			else {
				log("query: Too many oustanding yippy search "
				    "requests, %"INT32". closing socket on %s. "
				    "(repeats=%"INT32")",
				    ymax,
				    iptoa(s->m_ip),s_count);
				s_count = 0;
				s_last = now;
			}
		}
		g_stats.m_closedSockets++;
		return g_httpServer.sendErrorReply ( s , 500 , 
						"Too many search requests. "
						     "Please reload your "
						     "browser to try again.");
	}

	/////
	//
	// who dare accesses us?  manually or automatically...
	//
	/////
	int32_t USERID32 = 0;
	UserInfo *UI = NULL;

	//
	// the type of thing being accessed...
	//
	int32_t accessType = getAccessType ( &hr );

	//
	// the cost of the accesses in USD
	//
	float price = getPrice ( accessType );

	//


	//////////
	//
	// automated feed access verification
	//
	//////////

	// did they provide an access code?
	int32_t codeLen = 0;
	char *code = hr.getString("code", &codeLen, NULL);
	// only feed access supplies a userid in the http get request
	int32_t userId32b = hr.getLong("userid",0);
	// if there is a code but no userid, it might be an old style
	// request from client, etc... so search for those guys by
	// code...
	if ( code && userId32b == 0 ) {
		// get the userid from the provided code...
		int32_t nu = m_userInfoBuf.length()/sizeof(UserInfo);
		if ( nu > 10 ) nu = 10;
		UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
		for ( int32_t i =0 ; i < nu ; i++ ) {
			UserInfo *ui = &uis[i];
			if ( strcmp ( ui->m_xmlFeedCode,code) ) continue;
			userId32b = ui->m_userId32;
			break;
		}
		// code is invalid if is not for an old client
		if ( userId32b == 0 ) code = NULL;
	}
	// if we have both a code and userid, check to see if it is correct
	if ( code ) {
		// get it
		UserInfo *ui = getUserInfoFromId ( userId32b );
		// not found is bad! or if code does not match
		if ( price!=0.0 && (! ui || strcmp(ui->m_xmlFeedCode,code))){
			char *msg = "Permission denied. Check your "
				"<i>userid</i> and <i>code</i> cgi parms.";
			return g_httpServer.sendErrorReply(s,500,msg);
		}
		// check funds. m_pending is a positive amount, so is price.
		// accountBalance is how much money they have in their acct
		float cur = 0.0;
		if ( ui ) cur = ui->m_accountBalance - ui->m_pending - price;
		if ( ui && cur < 0.0 && 
		     // we do not use the new system to track ...
		     // we still need to bill them the old way
		     ! ( ui->m_flags & (UIF_OLDUSER | UIF_ADMIN) ) ) {
			char *msg = "Not enough funds in account to "
				"process request.";
			return g_httpServer.sendErrorReply(s,500,msg);
		}
		// set this
		USERID32 = userId32b;
		// save it
		UI = ui;
		// otherwise, it is pending!
		//stC->m_pending = price;
		//ui->m_pending += price;
	}

	////////
	//
	// paid manual access
	//
	////////
	if ( ! code ) {
		// get logged in user, if any
		UserInfo *ui = getLoggedInUserInfo2 ( &hr , s , NULL );
		// get access type
		if ( accessType == AT_SEARCHFEED_NEW ) price = 0.0;
		if ( accessType == AT_SEARCHFEED_OLD ) price = 0.0;
		// store it
		if ( ui ) USERID32 = ui->m_userId32;
		// save it
		UI = ui;
	}

        ////////
	//
	// the almighty autoban
	// 
	////////

	bool doAutoBan = true;//g_conf.m_doAutoBan;
	// if doing gigablast, only do autoban on PAGE_RESULTS
	if ( n != PAGE_RESULTS ) doAutoBan = false;
	// if they provided a valid code do not do autoban. if the code
	// was invalid we will have returned an error msg above
	if ( code ) doAutoBan = false;
	// assume not banned
	bool banned = false;
	// only check it for search results that have no valid "code"
	char testBufSpace[2048];
	SafeBuf testBuf(testBufSpace, 2048);
	if ( doAutoBan ) {
		int32_t uipLen;
		char *uip = hr.getString("uip", &uipLen, NULL);
                int32_t  ip  = s->m_ip;
		bool good = g_autoBan.hasPerm(ip, 
					      NULL,0,//code, codeLen, 
					      uip, uipLen, 
					      s, &hr, &testBuf,
					      false ); // justCheck
		if ( ! good ) banned = true;
	}
	// special yippy logging case
	if ( banned && isYippySearch ) {
		// log it
		int32_t yqLen;
		char *yq = hr.getString("query",&yqLen,NULL);
		if ( ! yq ) yq = "";
		log("proxy: got banned search req %s (%s)",
		    iptoa(s->m_ip),yq);
	}
	// print the turing test if autoban gave us one
	if ( banned ) {
		g_msg = " (error: autoban rejected.)";
		// this is the turing test i guess in testBuf
		if( testBuf.length() > 0 ) {
			printRequest(s, &hr);
			//g_stats.m_numSuccess++;
			return g_httpServer.
				sendDynamicPage(s, 
						testBuf.getBufStart(),
						testBuf.length(), 
						0);
		}
		printRequest(s, &hr);
		//g_stats.m_numSuccess++;
		int32_t rawFormat = hr.getLong("xml", 0); // was "raw"
		// support old raw=9 crap as well
		rawFormat = hr.getLong("raw",rawFormat);
		if ( rawFormat > 0 )
			return g_httpServer.sendQueryErrorReply
				(s,402, mstrerror(EBUYFEED),
				 rawFormat, g_errno, 
				 "You have exceeded the allowed "
				 "amount of free searches. You can buy "
				 "credits by creating an account at "
				 "https://www.gigablast.com/account");
		// if html format just return msg ...
		return g_httpServer.sendErrorReply(s,500,mstrerror(EBUYFEED));
	}

	// hash the code
	int32_t ch3 = 0; if ( code ) ch3 = hash32n ( code );
	
	// . increment their "outstanding requests" count
	// . customers now have a limit of outstanding requests to
	//   prevent abuse... vivisimo does not use "uip" and they 
	//   frequently get attacked by a spammer
	// . if autoban is off we still increment these counts, so if autoban
	//   gets turned on the counts will be unaffected
	/*
	int32_t bytesReceived = hr.getRequestLen();
	bool overLimit = g_autoBan.incRequestCount ( ch3 , bytesReceived );
	if ( g_conf.m_doAutoBan && overLimit ) {
		g_msg = " (error: too many outstanding requests)";
		printRequest ( s , &hr );
		// get how many bytes were sent out
		int32_t bs;
		bool st = g_httpServer.sendErrorReply(s,500,g_msg,&bs);
		g_autoBan.decRequestCount ( ch3 , bs );
		return st;
	}
	*/

	bool err2 = false;
	if ( err2 ) {
	hadError2:
		g_errno = ENOMEM;
		log("proxy: new(%i): %s",(int32_t)sizeof(StateControl),
		    mstrerror(g_errno));
		g_msg = " (error: out of memory.)";
		printRequest(s, &hr);
		int32_t bs;
		bool st;
		st=g_httpServer.sendErrorReply(s,500,mstrerror(g_errno),&bs);
		g_autoBan.decRequestCount ( ch3 , bs );
		return s;
	}

	// if we get here that means we've got something to forward.
	StateControl *stC;
	try { stC = new (StateControl) ; }
	// return true and set g_errno if couldn't make a new File class
	catch ( ... ) { 
	  goto hadError2;
	}
	mnew ( stC, sizeof(StateControl), "Proxy");

	// make a copy of this now
	if ( ! stC->m_hr.copy ( &hr ) ) {
		mdelete(stC,sizeof(StateControl),"Proxy");
		delete(stC);
		goto hadError2;
	}

	// reset to -1 in case freeStateControl is called
	stC->m_hostId = -1;
	stC->m_slot = NULL;

	// store the code for decementing the oustanding request count below
	stC->m_ch = ch3;

	// support &xml=1 or &raw=9 or &raw=8 to indicate xml output is wanted
	stC->m_raw = hr.getLong ( "xml", 0 );
	stC->m_raw = hr.getLong("raw",stC->m_raw);
	
	stC->m_s = s;

	stC->m_pageNum = n;

	stC->m_startTime = gettimeofdayInMilliseconds();

	stC->m_isQuery = false;

	//check if we've got a query
	if ( n == PAGE_RESULTS )
		stC->m_isQuery = true;

	stC->m_hash = hash32( hr.getRequest(), hr.getRequestLen() );

	// assume we are not doing a search query (stripe load balancing)
	stC->m_stripe = -1;

	stC->m_isYippySearch = isYippySearch;

	// log it
	if ( isYippySearch ) {
		int32_t yqLen;
		char *yq = hr.getString("query",&yqLen,NULL);
		if ( ! yq ) yq = "";
		log("proxy: got ok search req %s (%s)",  iptoa(s->m_ip),yq);
	}

	// yippy tcp proxy?
	int32_t x;
	char *sendBuf      ;
	int32_t  sendBufSize  ;
	int32_t  sendBufUsed  ;
	int32_t  msgTotalSize ;
	int32_t  timeout      ;
	if ( g_isYippy ) {
		// make it sticky, based on ip
		uint32_t iph = hash32((char *)&stC->m_s->m_ip,4);
		x = iph % 4;
		char *hn[] = { "10.36.14.4" , // teaski1
			       "10.36.14.5" , // teaski2
			       "10.36.14.17" , // teaski3
			       "10.36.14.44" }; // teaski4
		char *host = hn[x];
		// debug
		//host = "www2.gigablast.com";
		SafeBuf *sb = &stC->m_sb;
		sb->safeMemcpy(s->m_readBuf,s->m_readBufSize);
		sendBuf      = sb->getBufStart();
		sendBufSize  = sb->length();
		sendBufUsed  = sb->length();
		msgTotalSize = sb->length();
		// make it a int32_t time so we are less likely to overload
		// the teaski servers, wait for reply from reach one...
		timeout      = 60 * 1000; // in milliseconds
		// note it
		//log("proxy: sending request \"%s\" to %s",sendBuf,host);
		static int32_t s_reqNum = 0;
		stC->m_reqNum = s_reqNum;
		s_reqNum++;
		stC->m_start = gettimeofdayInMilliseconds();
		// debug log debug
		//log("proxy: forwarding reqNum=%"INT32" from %s to %s",
		//    stC->m_reqNum,iptoa(stC->m_s->m_ip),host);
		// only allow so many outstanding to avoid overloading
		// the teaski servers
		if ( stC->m_isYippySearch ) 
			s_yippySearchesOut++;
		// forward to a teaski
		if ( ! g_httpServer.m_tcp.sendMsg ( host, // hostname
						    gbstrlen(host),
						    80,
						    sendBuf,
						    sendBufSize,
						    sendBufUsed,
						    msgTotalSize,
						    stC,
						    gotTcpReplyWrapper,
						    timeout,
						    -1,
						    -1))
		     return false;
		// it did not block, wtf?
		return true;
	}


	// we need to know how many terms (excluding synonyms)
	// so we can do stripe load balancing by number of query terms.
	// i.e. sending 3 queries of only one term each is about the
	// same as sending one larger query to a single stripe.
	char *qs = hr.getString("q",NULL);
	Query q;
	if ( qs ) 
		q.set2 ( qs , langUnknown , false ); // 2 = autodetect bool
	// clear g_errno in case Query::set() set it
	g_errno = 0;
	// save it. might be zero!
	stC->m_numQueryTerms = q.getNumTerms();


	Host *h = NULL;
	//if ( forward >= 0 )
	//	h = g_hostdb.getHost ( forward );
	//if we want the main page, or if it is addurl, cannot send addurl to
	// another host if turing is on. Pick 1 host and keep sending to it
	if ( n == PAGE_REINDEX ||
	     n == PAGE_INJECT  ||
	     n == PAGE_ADDURL  ||
	     //n == PAGE_ROOT   || // FOR DEBUG!!
	     //n == PAGE_RESULTS || // FOR DEBUG!!
	     n == PAGE_SITEDB   )
		// get host #0
		h = g_hostdb.getHost ( 0 );
	/*
	  no longer - flurbit root page is the search page...
	else if ( n == PAGE_ADDURL || pathLen == 1 || 
	     ( pathLen == 11 && strncmp ( path , "/index.html" ,11 ) == 0 ) ){
		int32_t numTries = 0;
		while ( g_hostdb.isDead(m_mainHost) && 
			numTries++ < g_hostdb.getNumHosts() ){
			m_mainHost++;
			if ( m_mainHost >= g_hostdb.getNumHosts() )
				m_mainHost = 0;
		}
		h = g_hostdb.getHost( m_mainHost );
		m_numOutstanding[m_mainHost]++;
	}
	*/
	else 
		h = pickBestHost ( stC );

	// save in case of timeout below
	//stC->m_forward = -1;// forward;

	stC->m_retries = 0;

	stC->m_forwardHost = h;

	// . TODO: make both this and Multicast.cpp use a getTimeout() function
	// . default timeout to 8 seconds
	stC->m_timeout = 8 * 1000;
	// set the timeout
	int32_t  firstResult = hr.getLong("s", 0);
	int32_t  docsWanted  = hr.getLong("n", 10);
	// how many docsids request? first 4 bytes of request.
	int32_t  rr          = hr.getLong("rerank",-1);
	// . how many milliseconds of waiting before we re-route?
	// . 100 ms per doc wanted, but if they all end up 
	//   clustering then docsWanted is no indication of the
	//   actual number of titleRecs (or title keys) read
	// . it may take a while to do dup removal on 1 million docs
	int64_t wait = 5000 + 100  * (docsWanted+firstResult);
	// those big UOR queries should not get re-routed all the time
	wait += 1000 * stC->m_numQueryTerms;
	// a min of 8 seconds is good
	if ( wait < 8000 ) wait = 8000;
	// seems like buzz is hammering the cluster and 0x39's are 
	// timing out too much because of huge title recs taking 
	// forever with Msg20
	//if ( wait < 120000 ) wait = 120000;
	// never re-route if it has a rerank, those take forever
	if ( rr >= 0 ) wait = 3000 * 1000;
	// set it
	stC->m_timeout = wait;

	///////////////////////////////
	//
	// HACK: PRICE GATEWAY INTERCEPTION
	//
	// . page add url first loads the main page, then it has some 
	//   ajax to re-get the same url but with &id=xxx where xxx != 0
	// . so when gk144 gets a /addurl?id=123 request it will trigger
	//   the injection and send back the injection status which the ajax
	//   prints below the text box containing the url. the returned html
	//   is just a little msg which the ajax sets the content of the msgbox
	//   div to.
	// . so, we being the proxy will now pre-check any /addurl?id=123 
	//   request to make sure they are logged in. if not we will just 
	//   return a simple, you need to login, msg
	// . and if signed in, make sure they have to $10 available otherwise
	//   tell them they need to add money (check m_pending too)
	// . otherwise we have to let is pass through and look at the reply
	//   msg from gk144. if it says "Success" then we deduct from their
	//   m_accountBalance. otherwise we do not charge them...
	// . we should use this same logic for the seo html pages as well...
	//
	//

	//
	// . who is accessing what and for how much...
	// . we don't store "UI" since safebuf of UserInfos might realloc on us
	//
	stC->m_userId32   = USERID32;
	stC->m_accessType = accessType;
	stC->m_price      = price;

	// if they have "code=" in the url then that is an xml feed and
	// we forawrd the request to the back-end right away now
	if ( code ) {
		// UI Must be non-NULL since code was valid
		UI->m_pending += price;
		return forwardRequest ( stC );
	}

	// . forward any request besides addurl through
	// . this add url request is the one sent from the ajax on the add url
	//   shell page because we check way above for "id" where cgiId is.
	//if ( accessType != AT_ADDURL ) {
		// do not charge for anything else!
		stC->m_price = 0;
		// for now the tool and free and you don't have to login
		//if ( UI ) UI->m_pending += price;
		return forwardRequest ( stC );
	//}

	SafeBuf msg;
	float toolPrice = getPrice(AT_ADDURL);

	if ( ! UI )
		msg.safePrintf("<b>You need to "
			       "<a href=/account>login</a> to use "
			       "this tool. Each added url is $%.02f</b>",
			       toolPrice);

	if ( UI && UI->m_accountBalance - UI->m_pending - toolPrice<0 )
		msg.safePrintf("<b>The Add Url tool costs $%.02f and your "
			       "account balance is below this. "
			       "You need to <a href=/account>add more funds"
			       "</a> to use this "
			       "tool.</b>",
			       toolPrice);

	// . for every tool we ask "Are you sure? [yes] [no]" 1 or 0
	// . must be confirmed
	int32_t confirmed = hr.getLong("confirmed",0);
	if ( msg.length() == 0 && confirmed != 1 ) {
		// make another onclick ajax event
		msg.safePrintf("<b>Are you sure you want to add this url "
				"for $%.02f? "
			       //"<input type=submit value=Yes "
			       "<input type=button value=Yes "
				"onclick=\""
				"var client = new XMLHttpRequest();\n"
				"client.onreadystatechange = handler;\n"
				"var url='/addurl?u="
				,toolPrice);
		char *urlToAdd = hr.getString("u",NULL);
		msg.urlEncode ( urlToAdd );
		uint32_t h32 = hash32n(urlToAdd);
		if ( h32 == 0 ) h32 = 1;
		uint64_t rand64 = gettimeofdayInMillisecondsLocal();
		msg.safePrintf( "&id=%"UINT32"&rand=%"UINT64"&confirmed=1';\n"
				 "client.open('GET', url );\n"
				 "client.send();\n"
				 "\">"
				 "<input type=button value=Cancel>"
				 ,h32
				 ,rand64);
	}

	//
	// return the reason why the tool could not charge the user
	//
	if ( msg.length() ) {
		printRequest( s, &hr );
		g_httpServer.sendDynamicPage ( s      , 
					       msg.getBufStart(),
					       msg.length(),
					       // cachetime in secs
					       // make it forever
					       // to avoid hitting
					       // back button and
					       // reinjecting a url
					       86400*100
					       );
		freeStateControl ( stC );
		return true;
	}

	// assume the addurl goes through
	UI->m_pending += price;

	// send the request
	return forwardRequest ( stC );
}


bool Proxy::forwardRequest ( StateControl *stC ) {

	// this was a function arg... now it is in "stC"
	Host *h = stC->m_forwardHost;

	stC->m_hostId = h->m_hostId;

	TcpSocket *s = stC->m_s;

	//log (LOG_DEBUG,"query: proxy: (hash=%"UINT32") %s from "
	//     "hostId #%"INT32", port %i", stC->m_hash, hr.getRequest(), 
	//     h->m_hostId,h->m_httpPort);

	// if sending to the temporary network, add one to port
	int32_t port = h->m_httpPort;
	if ( g_conf.m_useTmpCluster ) port += 1;

	// put ip at end of request
	char *req     = s->m_readBuf;
	int32_t  reqSize = s->m_readOffset;
	// but then TcpServer.cpp leaves some room for a \0 and ip
	char *p = req + reqSize;
	// NULL terminate it
	*p = '\0';
	p += 1;
	// then add in ip
	*(int32_t *)p = s->m_ip;
	p += 4;

	bool isQCProxy = (g_hostdb.m_myHost->m_type & HT_QCPROXY);

	// . alter the request buffer
	// . set the please compress reply flag
	if ( isQCProxy && *req == 'G' ) *req = 'Z'; 

	// update size
	reqSize = p - req;
	// sanity check
	if ( reqSize > s->m_readBufSize ) { char *xx=NULL;*xx=0;}

	// sanity check
	if ( h->m_isProxy ) { char *xx=NULL;*xx=0; }

	// if we are a QUERY COMPRESSION proxy send to the specified address
	int32_t dstIp   = h->m_ip;
	int32_t dstPort = h->m_port;
	int32_t dstId   = h->m_hostId;
	if ( isQCProxy ) {
		dstIp   = g_hostdb.m_myHost->m_forwardIp;
		dstPort = g_hostdb.m_myHost->m_forwardPort;
		dstId   = -1;
	}

	// . default is to use the old engine. 
	// . only precise=1 uses new engine.
	HttpRequest *hr = &stC->m_hr;
	bool sendToNewEngine = false;
	int32_t precise = hr->getLong("precise",-1);
	if ( precise == 1 ) sendToNewEngine = true;
	// or if precise not given, an no code, send to new engine
	if ( precise == -1 && ! stC->m_ch ) sendToNewEngine = true;
	// if precise not given, and a code, sent to fast engine
	if ( precise == -1 &&   stC->m_ch ) sendToNewEngine = false;
	// explicit precise?
	if ( precise == 0 ) sendToNewEngine = false;
	// if no code (not a search feed) then do new engine
	//if ( ! stC->m_ch ) sendToNewEngine = true;
#ifndef _USE_GK144_
	sendToNewEngine = false;
#endif

	//
	// . HACK: always send to gk144 unless code= is specified
	// . test on www2.gigablast.com proxy first...
	//
	if ( //! stC->m_ch && 
	     sendToNewEngine &&
	     stC->m_pageNum != PAGE_DIRECTORY &&
	     ! g_isYippy ) {
		dstIp = atoip("10.5.54.154",11);
		dstPort = 9000; // udp (not dns)
		dstId = -1; // not a host in our hosts.conf
	}

	// rewrite &xml=1 as &raw=8 so old search engine sends back xml
	if ( ! sendToNewEngine && 
	     req[0]=='G' && 
	     req[1]=='E' && 
	     req[2]=='T' &&
	     req[3] == ' ' ) {
		// replace &xml=1 in request with &raw=8 to support others
		char *p = req + 4;
		char *pend = req + reqSize;
		// skip GET
		for ( ; p < pend ; p++ ) {
			// stop after url is over
			if ( *p == ' ' ) break;
			// match?
			if ( p[0] != '?' && p[0] != '&' ) continue;
			if ( p[1] != 'x' ) continue;
			if ( p[2] != 'm' ) continue;
			if ( p[3] != 'l' ) continue;
			if ( p[4] != '=' ) continue;
			if ( p[5] != '1' ) continue;
			p[1] = 'r';
			p[2] = 'a';
			p[3] = 'w';
			p[5] = '9';
			break;
		}
		// code is invalid if is not for an old client
		//if ( userId32b == 0 ) code = NULL;
	}


	// . let's use the udp server instead because it quickly switches
	//   to using eth1 if eth0 does two or more resends without an ACK,
	//   and vice versa. this ensure that if a network switch fails then
	//   we won't notice it besides a possible one-time 100ms delay.
	// . additionally, we can now accept tcp requests for admin pages
	//   even if such requests come from the proxy ip! because now they
	//   will just have to be from our ssh tunnel!!
	// . returns false and sets g_errno on error, true on success
	// . after resending the request 4 times with no ACK recv'd, call
	//   it a EUDPTIMEDOUT error and deal with that below...
	bool status;
	status = g_udpServer.sendRequest ( req         ,
					   reqSize     ,
					   0xfd        , //msgType 0xfd for fwd
					   dstIp , // h->m_ip     ,
					   dstPort , // h->m_port   ,
					   dstId , // h->m_hostId ,
					   NULL        , // the slotPtr
					   stC         , // state
					   gotHttpReplyWrapper ,
					   stC->m_timeout  ,
					   -1          , // backoff
					   -1          , // maxwait
					   NULL        , // replyBuf
					   0           , // replyBufMaxSize
					   0           , // niceness
					   4           );// maxResends

	// if no error, return false, we blocked
	if ( status ) return false;

	// wtf? if it fails unchage the pending...
	UserInfo *ui = getUserInfoFromId ( stC->m_userId32 );
	if ( ui ) ui->m_pending -= stC->m_price;

	//bool status;	
	/*
	status =  g_httpServer.getDoc ( h->m_ip,
					port , // h->m_httpPort,
					s->m_readBuf,
					s->m_readOffset,
					stC,
					gotReplyWrapperPage,
					timeout,
					10000000,
					10000000,
					false );
	// return false if it blocked
	if (!status)
		return false;
	*/
	//if not, we've got an error
	g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	freeStateControl(stC);
	// we send out what we read, s->m_readOffset bytes
	g_autoBan.decRequestCount ( stC->m_ch , s->m_readOffset );
	return true;
}
	    
void gotHttpReplyWrapper ( void *state, UdpSlot *slot ) { // TcpSocket *s ){
	g_proxy.gotReplyPage(state,slot);
}

//void Proxy::gotReplyPage ( void *state, TcpSocket *s ){
void Proxy::gotReplyPage ( void *state, UdpSlot *slot ) {

	StateControl *stC = (StateControl *) state;

	char *reply = slot->m_readBuf;
	int32_t  size  = slot->m_readBufSize;

	char *req = slot->m_sendBufAlloc;

	// . AND this is what we forwaded to a host in the flock
	// . we can free this because it reference the tcp buffer
	slot->m_sendBufAlloc = NULL;

	// decrement the oustanding request count
	g_autoBan.decRequestCount ( stC->m_ch , slot->m_readBufSize );

	// . try another host if this one times out
	// . if it is dead before we send to it then it will not ACK our
	//   requests, we will resend 10 times within about 300 ms and then
	//   we will get slot->m_errno set to EUDPTIMEDOUT
	// . it will also set the errno to EUDPTIMEDOUT if the timeout we
	//   gave sendRequest() above is reached.
	if ( slot->m_errno == EUDPTIMEDOUT && //stC->m_forward < 0 &&
	     // try this thrice i guess... hopefully we won't pick the same
	     // host we did before!
	     ++stC->m_retries <= 3 ) {
		// reduce the query load counts
		uncountStripe ( stC );
		// pick another host! should NEVER return NULL
		Host *h = pickBestHost ( stC );
		log("proxy: hostid #%"INT32" timed out. req=%s Rerouting "
		    "forward request "
		    "to hostid #%"INT32" instead.",stC->m_hostId,
		    req,//stC->m_s->m_readBuf,
		    h->m_hostId);
		// . try a resend!
		// . it will block or it will call sendErrorReply
		stC->m_forwardHost = h;
		forwardRequest ( stC );//, h );
		// all done
		return;
	}

	// save it so we can free "reply" when state is destroyed
	stC->m_slot = slot;

	// save reply so we can free it when this state is freed
	// no i am just transferring into the socket's sendbuf now
	stC->m_slotReadBuf = NULL;
	//stC->m_slotReadBuf        = slot->m_readBuf;
	//stC->m_slotReadBufMaxSize = slot->m_readBufMaxSize;

	// should we uncompress the reply?
	bool doUncompress = ( req[0] == 'Z' );
	// do not allow regular proxy to uncompress it though!
	if ( ! (g_hostdb.m_myHost->m_type & HT_QCPROXY ) ) doUncompress=false;

	// don't let udp server free the reply, we forward this to end user
	slot->m_readBuf = NULL;

	// sanity check
	//if ( s->m_readOffset < 0 ) { char *xx=NULL;*xx=0; }
	if ( slot->m_readBufSize < 0 ) { char *xx=NULL;*xx=0; }

	int64_t nowms = gettimeofdayInMilliseconds();

	//m_numOutstanding[stC->m_hostId]--;
	//if ( s->m_readOffset == 0 ){
	if ( size == 0 ){
		log (LOG_WARN,"query: Proxy: Lost the request");
		// give a 500 httpstatus to just decrement UserInfo::m_pending
		addAccessPoint ( stC , nowms , 500 );
		g_errno = EBADREQUEST;
	hadError:
		log(LOG_WARN,"proxy: error=%s req=%s",mstrerror(g_errno),req);
		g_httpServer.sendErrorReply(stC->m_s,500,mstrerror(g_errno));
		freeStateControl(stC);
		return;
	}

	uint64_t took = nowms - stC->m_startTime;

	// if reply was compressed then uncompress it
	if ( doUncompress ) {
		// sanity check
		if ( size < 12 ) { char *xx=NULL;*xx=0; }
		// parse it up
		unsigned char *p = (unsigned char *)reply;
		// get the sizes
		int32_t need  = *(int32_t *)p; p += 4; // uncompressed total size
		int32_t size1 = *(int32_t *)p; p += 4; // size of compressed mime
		int32_t size2 = *(int32_t *)p; p += 4; // size of compressed content
		// note it
		//logf(LOG_DEBUG,"proxy: uncompressing from %"INT32" to %"INT32"",
		//     size1+size2+12,need);
		// make the decompressed buf
		unsigned char *dbuf = (unsigned char *)mmalloc ( need,"pdbuf");
		unsigned char *dend = dbuf + need;
		if ( ! dbuf ) goto hadError;
		unsigned char *dptr = dbuf;

		uint32_t bytes1 = dend - dptr;
		// ucompress the http mime
		int err1 = gbuncompress (dptr , &bytes1, p , size1 );
		p += size1;
		dptr += bytes1;

		if ( size2 ) {
			uint32_t bytes2 = dend - dptr;
			// uncompress the http content
			int err2 = gbuncompress ( dptr , &bytes2, p , size2 );
			p += size2;
			dptr += bytes2;
			if ( err2 != Z_OK || err1 != Z_OK ) {
				g_errno = EUNCOMPRESSERROR;
				goto hadError;
			}
		}

		// sanity check
		if ( dptr - dbuf != need ) { char *xx=NULL;*xx=0; }

		// free original compressed reply
		mfree ( reply , size , "origreply");

		// now re-set these to the uncompressed mime/pagecontent
		reply = (char *)dbuf;
		size  = need;
		// . and this is for freeing it after it is transmitted
		// . likewise, let's directly transmit this reply and
		//   let udpserver free it when done
		//stC->m_slotReadBuf        = (char *)dbuf;
		//stC->m_slotReadBufMaxSize = need;
	}


	// if we are a regular proxy forwarding a compressed reply to a
	// query compression proxy, then the reply is compressed, just leave
	// it alone
	if ( ( req[0] == 'Z' ) && (g_hostdb.m_myHost->m_type & HT_PROXY ) ) {
		// . now record the request in our accounting system.
		// . we assume the reply is error-free at this point
		// . it might be for a GET /seo too, not just stC->m_isQuery
		addAccessPoint ( stC , nowms , 200 ); // httpStatus
		//now should be able to print
		HttpRequest r;
		r.set(stC->m_s->m_readBuf, stC->m_s->m_readOffset, stC->m_s);
		// log the request
		printRequest(stC->m_s, &r, took, NULL,0);//content,contentLen);
		// add stat for stats graph
		if ( stC->m_isQuery ) {
			g_stats.logAvgQueryTime(stC->m_startTime);
			// i dont check if query is raw or not
			int32_t color = 0x00b58869;
			if ( stC->m_raw ) color = 0x00753d30;
			int64_t nowms = gettimeofdayInMilliseconds();
			// . add the stat
			// . use brown for the stat
			g_stats.addStat_r ( 0               ,
					    stC->m_startTime , 
					    nowms ,
					    //"query",
					    color ,
					    STAT_QUERY );
			// add to statsdb as well
			g_statsdb.addStat ( 0 , // niceness
					    "query" ,
					    stC->m_startTime ,
					    nowms            ,
					    stC->m_numQueryTerms );
			g_stats.m_numSuccess++;
		}
		// forward it to the qcproxy. true -> do not re-compress!
		//g_httpServer.sendReply2(NULL,0,reply,size,stC->m_s,true);
		// let tcp server free it when done
		g_httpServer.m_tcp.sendMsg ( stC->m_s ,
					     reply ,
					     size ,
					     size ,
					     size ,
					     NULL ,
					     NULL );
		// free mem
		freeStateControl(stC);
		return;
	}
	//char *reply = s->m_readBuf;
	//int32_t size = s->m_readOffset;
	HttpMime mime;
	// re-store original mime from uncompressed mime
	mime.set ( reply, size, NULL);
	int32_t httpStatus = mime.getHttpStatus();
	if ( httpStatus != 200 )
		g_msg = " (error: unknown.)";

	// . now record the request in our accounting system.
	// . we assume the reply is error-free at this point
	// . it might be for a GET /seo too, not just stC->m_isQuery
	// . this should update the account balance
	addAccessPoint ( stC , nowms , httpStatus );


	if ( stC->m_isQuery && httpStatus == 200 ){
		g_stats.logAvgQueryTime(stC->m_startTime);
		// i dont check if query is raw or not
		int32_t color = 0x00b58869;
		if ( stC->m_raw ) color = 0x00753d30;
		int64_t nowms = gettimeofdayInMilliseconds();
		// . add the stat
		// . use brown for the stat
		g_stats.addStat_r ( 0               ,
				    stC->m_startTime , 
				    nowms ,
				    //"query",
				    color ,
				    STAT_QUERY );
		// add to statsdb as well
		g_statsdb.addStat ( 0 , // niceness
				    "query" ,
				    stC->m_startTime ,
				    nowms            ,
				    stC->m_numQueryTerms );
		g_stats.m_numSuccess++;
		/*m_numSuccess++;
		m_totalQueryTime += took;
		if ( m_numSuccess % 20 == 0 ){
			int32_t avgTime = m_totalQueryTime / m_numSuccess;
			log ( LOG_INFO,"proxy: did the last %"INT32" successful "
			      "queries in %"UINT64" ms, latency %"INT32" ms. Total "
			      "queries %"INT32"",
			      m_numSuccess,m_totalQueryTime,avgTime,
			      m_numQueries );
		}
		//we just log the last 2000 successful queries
		if ( m_numSuccess > 2000 ){
			m_numSuccess = 0;
			m_totalQueryTime = 0;
			m_numQueries = 0;
			}*/
	}
	else if ( stC->m_isQuery && httpStatus != 200 )
		g_stats.m_numFails++;
	
	//now should be able to print
	HttpRequest r;
	r.set(stC->m_s->m_readBuf, stC->m_s->m_readOffset, stC->m_s);

	/*	if ( g_conf.m_logQueryTimes )
		logf( LOG_TIMING,"query: proxy: got back %"INT32" bytes page "
		      "with status %"INT32" for request %s in %"INT32" ms",
		      size, stC->m_hash, httpStatus, r.getRequest(), took  );*/

	//char *content = s->m_readBuf + mime.getMimeLen();
	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = size  - mime.getMimeLen();

	printRequest(stC->m_s, &r, took, content,contentLen);

	/*
	char charset[1024];
	gbmemcpy ( charset, mime.getCharset(), mime.getCharsetLen() );
	charset[mime.getCharsetLen()] = '\0';

	char *contentType;
	switch ( mime.getContentType() ){
	case CT_UNKNOWN : contentType = " ";
		break;
	case CT_HTML : contentType = "text/html";
		break;
	case CT_TEXT : contentType = "text/plain";
		break;
	case CT_XML  : contentType = "text/xml";
		break;
	case CT_PDF  : contentType = "application/pdf";
		break;
	case CT_DOC  : contentType = "application/msword";
		break;
	case CT_XLS  : contentType = "application/vnd.ms-excel";
		break;
	case CT_PPT  : contentType = "application/mspowerpoint";
		break;
	case CT_PS   : contentType = "application/postscript";
		break;
	}
	*/

	/*
	g_httpServer.sendDynamicPage ( stC->m_s      , 
				       content       ,
				       contentLen    , 
				       25            , // cachetime in secs
				       // pick up key changes
				       // this was 0 before
				       false      , // POSTREply? 
				       contentType, // content type
				       -1         , // http status -1->200
				       // CRAP WHAT ABOUT COOKIE IN MIME???
				       NULL       , // cookie
				       charset    );
	*/


	/*
	g_httpServer.sendReply2 ( mime.getMime() ,
				  mime.getMimeLen() ,
				  content ,
				  contentLen ,
				  stC->m_s ,
				  false );
	*/

	// . add the login bar to all pages we send back
	// . we could also use to automatically update copyright years 
	//   and add any common elements to every page...
	// . make a new reply to send back...
	// . it may free the old "reply" or it may set newReply=reply...
	int32_t newReplySize = size;
	char *newReply = reply;

	// make sure it is HTTP/1.0 not HTTP/1.1
	if ( reply[0] == 'H' &&
	     reply[1] == 'T' &&
	     reply[2] == 'T' &&
	     reply[3] == 'P' &&
	     reply[4] == '/' &&
	     reply[5] == '1' &&
	     reply[6] == '.' &&
	     reply[7] == '1' )
		reply[7] = '0';
	     

	// do not print login bars in the xml!! do not print for ixquick
	// which gets results in html...
	if ( ! stC->m_raw && ! stC->m_ch && ! stC->m_isEventGuru )
		newReply = storeLoginBar ( reply , 
					   size ,  // transmit size
					   size , // allocsize
					   // so we can quickly jump over the
					   // mime in the reply...
					   mime.getMimeLen() ,
					   //stC->m_s->m_userId32 ,
					   //stC->m_userId32,
					   &newReplySize ,
					   &stC->m_hr );

	// . try this one instead
	// . returns false if blocked
	TcpServer *tcp = &g_httpServer.m_tcp;
	// are we using ssl?
	if ( stC->m_s->m_ssl ) tcp = &g_httpServer.m_ssltcp;
	tcp->sendMsg ( stC->m_s ,
		       newReply , 
		       newReplySize ,
		       newReplySize ,
		       newReplySize ,
		       NULL,
		       NULL);
	
	// do not let udpslot free that we are sending it off
	//slot->m_readBuf = NULL;
	
	freeStateControl(stC);
}


void freeStateControl ( StateControl *stC ){
	if ( ! stC ) return;

	if ( ! g_isYippy && stC->m_hostId >= 0 ) {
		g_proxy.m_numOutstanding[stC->m_hostId]--;
		uncountStripe ( stC );
	}

	// free the reply buffer
	if ( stC->m_slot && ! g_isYippy ) {
		// save reply so we can free it when this state is freed
		char *reply = stC->m_slotReadBuf;
		int32_t  size  = stC->m_slotReadBufMaxSize;
		if ( reply ) mfree ( reply , size , "proxy" );
		// do not double free!
		stC->m_slotReadBuf = NULL;
	}

	mdelete(stC,sizeof(StateControl),"Proxy");
	delete(stC);
}

void uncountStripe ( StateControl *stC ) {
	// if stripe is -1, it was not a search query request
	int32_t stripe = stC->m_stripe;
	if ( stripe < 0 ) return;
	// a more refined load balancing act
	g_proxy.m_termsOutOnStripe[stripe] -= stC->m_numQueryTerms;
	// dec this too
	g_proxy.m_queriesOutOnStripe[stripe]--;
}


// . now do stripe balancing
// . this prevents one machine from receving all the Msg39 requests while its
//   twin gets none
Host *Proxy::pickBestHost( StateControl *stC ) {

	// sanity check, for m_stripeLastHostId array size, which is only 8 now
	int32_t numStripes = g_hostdb.getNumStripes();
	if ( numStripes > MAX_STRIPES ) { char*xx=NULL;*xx=0; }

	// see which stripes have non-dead hosts!
	char stripeDead[MAX_STRIPES];
	bool allDead = true;
	memset ( stripeDead , 1 , MAX_STRIPES );
	int32_t nh = g_hostdb.getNumHosts();
	for ( int32_t i = 0 ; i < nh ; i++ ) {
		Host *h = g_hostdb.getHost(i);
		if ( g_hostdb.isDead ( h ) ) continue;
		// hey, we are not all dead!
		allDead = false;
		// clear it
		stripeDead [ h->m_stripe ] = 0;
	}

	// . get the stripe with the least # of outstanding query terms
	// . in the event of a tie, give each stripe an equal shot so we
	//   balance out the wear-n-tear on the drives.
	//int32_t mini = m_nextStripe;
	//int32_t min  = m_termsOutOnStripe[mini];
	//bool tied = false;

	// start at this stripe
	int32_t ns = m_nextStripe;

	int32_t min ;
	int32_t minns = -1;
	for ( int32_t i = 0 ; i < numStripes ; i++ ) {
		// get stripe number
		if ( ++ns >= numStripes ) ns = 0;
		// skip if whole stripe is dead, and other stripes are not dead
		if ( stripeDead[ns] && ! allDead ) continue;
		// how loaded is this stripe?
		int32_t termsOut = m_termsOutOnStripe[ns];
		// skip if his load is tied or higher than our current winner
		if ( minns != -1 && termsOut >= min ) continue;
		// got a new winner
		minns = ns;
		min   = termsOut;
	}

	// sanity check
	if ( minns == -1 ) { char *xx=NULL;*xx=0; }

	// rotate the prefered next stripe
	if ( ++m_nextStripe >= numStripes ) m_nextStripe = 0;

	// find the next host in line for stripe #minns
	int32_t bestHostId = m_stripeLastHostId[minns];
	// count iterations
	//int32_t count = 0;
 loop:
	// inc it, wrap it
	if ( ++bestHostId >= g_hostdb.getNumHosts() ) bestHostId = 0;
	// do not do infinite loops
	//if ( count++ >= g_hostdb.getNumHosts() ) {
	//	log ("proxy: all hosts are dead. critical error.");
	//	g_errno = EBADENGINEER;
	//	g_httpServer.sendErrorReply(stC->m_s,500,mstrerror(g_errno));
	//	freeStateControl(stC);
	//	return;
	//}
	// skip if dead
	if ( g_hostdb.isDead( bestHostId ) && ! allDead ) goto loop;
	// get the host
	Host *h = g_hostdb.getHost ( bestHostId );
	// saity check
	if ( h->m_isProxy ) { char *xx=NULL;*xx=0; }
	// advance until it is from the least-loaded stripe
	if ( h->m_stripe != minns ) goto loop;


	// add query terms to it for load balancing purposes
	m_termsOutOnStripe[minns] += stC->m_numQueryTerms;
	// inc this too
	m_queriesOutOnStripe[minns]++;
	// save it
	m_stripeLastHostId[minns] = bestHostId;

	// store ino into state so we can reduce the count when we get a reply
	stC->m_stripe        = minns;
	//stC->m_numQueryTerms = numQueryTerms;

	// return it
	return g_hostdb.getHost( bestHostId );
}

/*
Host *Proxy::pickBestHost( ) {
	int32_t  bestHost = m_lastHost;
	bestHost++;
	if ( bestHost >= g_hostdb.getNumHosts() )
		bestHost = 0;
	//check if the host is dead. if dead cycle through for a live host
	//Also check if it has got outstanding requests
	int32_t numTried = 0;
	while ((m_numOutstanding[bestHost]>0 || g_hostdb.isDead(bestHost)) &&
	       numTried < 10 ){
		bestHost++;
		if ( bestHost >= g_hostdb.getNumHosts() )
			bestHost = 0;
		numTried++;
		//This could go into an infinite loop if no hosts are on
		//so adding numTried here too. Of course if no hosts are
		//on, we're gonna lose the request
		while ( numTried < 10 && g_hostdb.isDead( bestHost ) ){
			bestHost++;
			if ( bestHost >= g_hostdb.getNumHosts() )
				bestHost = 0;
			numTried++;
		}
	}
	m_lastHost = bestHost;
	m_numOutstanding[bestHost]++;
	Host *h = g_hostdb.getHost( bestHost );
	return h;
}
*/

void Proxy::printRequest(TcpSocket *s, HttpRequest *r, 
			 uint64_t took ,
			 char *content,
			 int32_t contentLen ) {
	//LOG THE REQUEST
	/*
	// . if it is a post request, log the posted data, too
	char cgi[20058];
	cgi[0] = '\0';
	if ( r->isPOSTRequest() ) {
		int32_t  plen = r->m_cgiBufLen;
		if (  plen >= 20052 ) plen = 20052;
		char *pp1 = cgi ;
		char *pp2 = r->m_cgiBuf;
		// . when parsing cgi parms, HttpRequest converts the 
		//   &'s to \0's so it can avoid having to malloc a 
		//   separate m_cgiBuf
		// . now it also converts ='s to 0's, so flip flop back
		//   and forth
		char dd = '=';
		for ( int32_t i = 0 ; i < plen ; i++ , pp1++, pp2++ ) {
			if ( *pp2 == '\0' ) { 
				*pp1 = dd;
				if ( dd == '=' ) dd = '&';
				else             dd = '=';
				continue;
			}
			if ( *pp2 == ' ' ) *pp1 = '+';
			else               *pp1 = *pp2;
		}
		if ( r->m_cgiBufLen >= 20052 ) {
			pp1[0]='.'; pp1[1]='.'; pp1[2]='.'; pp1 += 3; }
		*pp1 = '\0';
	}
	*/

	// get time format: 7/23/1971 10:45:32
	time_t tt = getTimeLocal();
	struct tm *timeStruct = localtime ( &tt );
	char bufTime[64];
	strftime ( bufTime , 63 , "%b %d %T", timeStruct);
	//char *ref = r->getReferer ();

	// if autobanned and we should not log, return now
	if (g_msg&&!g_conf.m_logAutobannedQueries && strstr(g_msg,"autoban")){ 
		g_msg = ""; 
		return; 
	}

	/*
	// fix cookie for logging
	char cbuf[5000];
	char *pc  = r->m_cookiePtr;
	int32_t  pclen = r->m_cookieLen;
	if ( pclen >= 4998 ) pclen = 4998;
	char *pcend = r->m_cookiePtr + pclen;
	char *dst = cbuf;
	for ( ; pc < pcend ; pc++ ) {
		*dst = *pc;
		if ( ! *pc ) *dst = ';';
		dst++;
	}
	*dst = '\0';
	
	if ( ! cgi[0] ) 
		logf (LOG_INFO,"http: %s %s %s %s cookie=\"%s\" %s %s",
		      bufTime,iptoa(s->m_ip),r->getRequest(),
		      ref,cbuf,r->getUserAgent(),g_msg);
	else 
		logf (LOG_INFO,"http: %s %s %s %s %s cookie=\"%s\" %s %s",
		      bufTime,iptoa(s->m_ip),r->getRequest(),
		      cgi,ref,cbuf,r->getUserAgent(),g_msg);
	*/

	char *req = s->m_readBuf;
	//int32_t  reqLen = s->m_readOffset;

	logf (LOG_INFO,"http: %s %s %s %s",
	      bufTime,iptoa(s->m_ip),req,//r->getRequest(),
	      g_msg);


	//reset g_msg
	g_msg = "";
	if ( (int32_t)took < g_conf.m_logQueryTimeThreshold ) return;

	if ( ! g_conf.m_logQueryReply || ! content || contentLen <= 0 ) {
		logf (LOG_INFO,"http: Took %"UINT64" ms "
		      "(len=%"INT32" bytes) "
		      "for request %s",
		      took, contentLen, r->getRequest());
		return;
	}


	// copy into buf
	char *p = (char *)mmalloc ( contentLen+1,"proxycont");
	if ( ! p ) return;

	for ( int32_t i = 0 ; i < contentLen ; i++ ) {
		if ( content[i] && ! is_binary_a(content[i]) ) { 
			p[i]=content[i]; continue; }
		// fix 0's and binary stuff
		p[i]='?';
	}
	// null terminate
	p[contentLen]=0;

	logf (LOG_INFO,"http: Took %"UINT64" ms "
	      "(len=%"INT32" bytes) "
	      "for request %s reply=%s",
	      took, contentLen, r->getRequest(),content);

	mfree ( p , contentLen+1, "proxycont");
}

// for yippy only!
void gotTcpReplyWrapper ( void *state , TcpSocket *s ) {

	class StateControl *stC = (StateControl *)state;

	// i guess s->m_sendBuf is in StateControl::m_sb safebuf so 
	// avoid double free. it directly called TcpServer::sendMsg which
	// does not do a copy like httpserver
	s->m_sendBuf = NULL;

	if ( stC->m_isYippySearch ) 
		s_yippySearchesOut--;
	
	// get the reply from the teaski machine
	char *reply = s->m_readBuf;
	int32_t replySize = s->m_readOffset;
	//int64_t took = gettimeofdayInMilliseconds() - stC->m_start;

	if ( ! reply ) {
		g_errno = EBADREPLY;
		replySize = 0;
		char ipbuf[64];
		sprintf(ipbuf,"%s",iptoa(s->m_ip));
		char ipbuf2[64];
		sprintf(ipbuf2,"%s",iptoa(stC->m_s->m_ip));
		char *creq = "";
		if ( stC->m_s &&
		     stC->m_s->m_readBuf &&
		     stC->m_s->m_readOffset )
			creq = stC->m_s->m_readBuf;
		log("proxy: got a zero length reply from %s. err=%s "
		    "client=%s clientreq=%s",
		    ipbuf,
		    mstrerror(g_errno),
		    ipbuf2,
		    creq );
		// debug log debug
		//log("proxy: returning reply to %s replysize=%"INT32" "
		//    "reqnum=%"INT32" (took=%"INT64"ms)",
		//    iptoa(stC->m_s->m_ip),
		//    replySize,stC->m_reqNum,took);
		g_httpServer.sendErrorReply(stC->m_s,500,mstrerror(g_errno));
		freeStateControl(stC);
		return;
	}


	if ( g_errno ) {
		log("proxy: got error in reply from %s. err=%s",
		    iptoa(s->m_ip),mstrerror(g_errno));
		// debug log debug
		//log("proxy: returning reply to %s replysize=%"INT32" "
		//    "reqnum=%"INT32" (took=%"INT64"ms) (err=%s)",
		//    iptoa(stC->m_s->m_ip),
		//    replySize,stC->m_reqNum,took,mstrerror(g_errno));
		g_httpServer.sendErrorReply(stC->m_s,500,mstrerror(g_errno));
		freeStateControl(stC);
		return;
	}

	/*
	// debug log debug
	int32_t max;
	char c;
	if ( reply ) {
		max = 1500;
		if ( replySize < max ) max = replySize;
		max--;
		if ( max < 0 ) max = 0;
		c = reply[max];
		reply[max] = 0;
	}

	//log("proxy: returning reply back to client. size=%"INT32" reply=%s",
	//    replySize,reply);

	log("proxy: returning  reqNum=%"INT32" for  %s replysize=%"INT32" "
	    "(took=%"INT64"ms)",
	    stC->m_reqNum,
	    iptoa(stC->m_s->m_ip),
	    replySize,took);

	if ( reply )
		reply[max] = c;
	*/

	// . forward the teaski reply back to client's browser
	// . it should free the reply buf when done
	g_httpServer.sendReply2(NULL, // mime
				0, // mimelen
				reply, // content
				replySize, // contentlen
				stC->m_s,
				true); // already compressed?

	freeStateControl(stC);
}

///////////////////////////
//
// USER ACCOUNTING FUNCTIONS
//
///////////////////////////


// every day has a summary rec
class SummaryRec {
 public:
	int32_t      m_userId32;
	char      m_accessType;
	// how many times this access type was done:
	int32_t      m_numAccesses; 
	// how much user was charged for all these accesses:
	float     m_totalCost;
	// how long all replies took in milliseconds:
	int64_t m_totalProcessTime;
	char      m_month;
	char      m_day;
	int16_t     m_year;
};

#define DRF_DEPOSIT 1
#define DRF_WITHDRAW 2
#define DRF_WITHDRAW_FEE 3

// every time a deposit or withdrawal is made we have one of these
class DepositRec {
public:
	int32_t      m_userId32;
	float     m_depositAmount;
	int32_t      m_depositDate;
	// . use transactionid for doing CREDITs back to user
	// . i've seen it > 5B so use a int64_t
	int64_t m_authorizeNetTransactionId;
	int32_t      m_flags;
};


// returns NULL and sets g_errno on error
UserInfo *Proxy::getUserInfoForFeedAccess ( HttpRequest *hr ) {
	
	// assume no error
	g_errno = 0;

	//char *user = hr->getString("user",NULL);
	// we also store the username along with session id
	//if ( ! user ) user = r->getStringFromCookie("user",NULL);
	int32_t userId32 = hr->getLong("userid",0);

	char *code = hr->getString("code",NULL);

	// if no userid or code given, just rely on autoban then,
	// so do not set g_errno, just return NULL
	if ( ! userId32 && ! code ) return NULL;

	// allow others to just specify their codes to get
	// results and not the userid
	if ( ! userId32 && code ) {
		UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
		int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
		for ( int32_t i = 0 ; i < ni && i < 5 ; i++ ) {
			// int16_tcut
			UserInfo *ui = &uis[i];
			// must be an "old" user like others
			if ( ! (ui->m_flags & (UIF_OLDUSER|UIF_ADMIN))) 
				continue;
			// then check for code match
			if ( strcmp(code,ui->m_xmlFeedCode) ) continue;
			// matched!
			return ui;
		}
		// they gave a code, amd no userid, and their code was
		// unmatched... that's perm denied
		g_errno = EPERMDENIED;
		return NULL;
	}

	UserInfo *ui = getUserInfoFromId ( userId32 );
	
	// if user name has no record, that's permission denied
	if ( ! ui ) {
		log("proxy: user not found for userid=%"INT32"",userId32);
		g_errno = EPERMDENIED;
		return NULL;
	}

	// codes must match, too!
	if ( ! code || strcmp(code,ui->m_xmlFeedCode) ) {
		g_errno = EPERMDENIED;
		log("proxy: permission denied for userid=%"INT32" code=%s",
		    userId32,code);
		return NULL;
	}

	return ui;
}

int32_t Proxy::getAccessType ( HttpRequest *hr ) {

	char *path    = hr->getPath();
	int32_t  pathLen = hr->getPathLen();

	char c = path[pathLen];
	path[pathLen] = '\0';

	int32_t accessType = 0;

	if ( strncmp(path,"/addurl",7) == 0 ) {
		// assume old
		accessType = AT_ADDURL;
	}

	bool isSearch = false;

	// old access method (in Xml.cpp/Pages.cpp)
	if ( strncmp(path,"/cgi/0.cgi?",11) == 0 )
		// assume old
		isSearch = true;

	// another form in Xml.cpp/Pages.cpp
	if ( strncmp(path,"/index.php?",11) == 0 )
		// assume old
		isSearch = true;

	// searching the old index?
	if ( strncmp(path,"/search?",8) == 0 ) 
		isSearch = true;

	// default will be fast search
	if ( isSearch ) {
		// assume fast search
		accessType = AT_SEARCHFEED_OLD;
		// new index search?
		if ( hr->getLong("precise",0)==1) 
			accessType = AT_SEARCHFEED_NEW;
	}

	// seo page?
	if ( strncmp(path,"/seo?",5) == 0 ) {
		char *page = hr->getString("page",NULL);
		if ( page && strcmp(page,"matchingqueries") == 0 )
			accessType = AT_MATCHING_QUERIES;
		if ( page && strcmp(page,"competitorpages") == 0 )
			accessType = AT_COMPETITOR_PAGES;
		if ( page && strcmp(page,"competitorbacklinks") == 0 )
			accessType = AT_COMPETITOR_BACKLINKS;
		if ( page && strcmp(page,"relatedqueries") == 0 )
			accessType = AT_RELATED_QUERIES;
		if ( page && strcmp(page,"missingterms") == 0 )
			accessType = AT_MISSING_TERMS;
	}

	// revert
	path[pathLen] = c;

	return accessType;
}

// in dollars!
float Proxy::getPrice ( int32_t accessType ) {

	if ( accessType == 0 )
		return 0.0;

	if ( accessType == AT_SEARCHFEED_OLD )
		// $1 CPM
		return 1.00 / 1000.0;

	if ( accessType == AT_SEARCHFEED_NEW )
		// $2.50 CPM
		return 2.50 / 1000.0;

	if ( accessType == AT_MATCHING_QUERIES )
		return 9.99; // $10

	if ( accessType == AT_COMPETITOR_PAGES )
		return 9.99; // $10

	if ( accessType == AT_COMPETITOR_BACKLINKS )
		return 9.99; // $10

	if ( accessType == AT_RELATED_QUERIES )
		return 14.99; // $15

	if ( accessType == AT_MISSING_TERMS )
		return 19.99; // $20

	if ( accessType == AT_ADDURL )
		return 4.99; // $10

	char *xx=NULL;*xx=0;
	return 0.0;
}


// . return false with g_errno set on error, true otherwise
// . when a reply has been generated we call this to accumulate stats
// . we update the SummaryRec and UserInfo record, as well as adding a
//   record to statsdb!
// . make the proxy run on ssds for extra reliability
// . accesses are also logged by back-end machines in case we need
//   to rebuild access points, however, we'd lose CC info etc.
// . so backup userdb periodically maybe using cron to gk37 or something.
// . now they must have "&user=username&code=sdfsdfdfs"
// . processTime is in milliseconds (ms)
bool Proxy::addAccessPoint ( StateControl *stC , 
			     int64_t nowms ,
			     int32_t httpStatus ) {

	HttpRequest *hr = &stC->m_hr;

	//UserInfo *ui = getUserInfoForFeedAccess ( hr );

	// it might be an XML feed access OR an add url or seo tool access
	UserInfo *ui = getUserInfoFromId ( stC->m_userId32 );

	// ALWAYS CALL THIS!
	if ( ui ) ui->m_pending -= stC->m_price;

	// wtf? if no code or no user or incorrect code, it will return NULL
	// with g_errno set on error
	if ( ! ui && ! g_errno ) return true;
	// error? wtf...
	if ( g_errno ) return false;

	// error getting results? do not charge for it then...
	if ( httpStatus != 200 ) {
		log("proxy: got http reply error status=%"INT32"",httpStatus);
		return true;
	}

	char accessType = getAccessType ( hr );

	// no cost?
	if ( accessType == 0 ) return true;

	return addAccessPoint2 ( ui , 
				 accessType , 
				 nowms ,
				 stC->m_startTime );
}

bool Proxy::addAccessPoint2 ( UserInfo *ui , 
			      char accessType ,
			      int64_t nowms ,
			      int64_t startTime ) {


	if ( ! ui ) {
		log("proxy: addaccesspoint2 ui was NULL!" );
		return false;
	}

	float price = getPrice ( accessType );

	// get the summary rec for this day and user...
	SummaryRec *sr = getSummaryRec ( ui->m_userId32 , accessType );
	// wtf? error!
	if ( ! sr ) return false;

	// the account balance of course
	ui->m_accountBalance -= price;

	int64_t processTime = nowms - startTime; // stC->m_startTime;

	// increment counters, all else should be fixed by getSummaryRec()
	sr->m_numAccesses++;
	sr->m_totalProcessTime += processTime;
	sr->m_totalCost        += price;

	//
	// . a new statsdb key/data type i guess
	// . but now let's add 
	//
	g_statsdb.addStat ( 0 , // niceness
			    "query" ,
			    startTime ,
			    nowms            ,
			    processTime , // value
			    // set parmHash to 1 so we add oldVal/newVal
			    0 ,
			    0 , // oldval
			    0 ,// newval
			    // this is unique for each user! and must NOT
			    // be recycled in the event you delete a user...
			    ui->m_userId32 );


	return true;
}


// . use Proxy::m_sumBuf to hold the summary recs
// . one summaryrec per day/user/accesstype tuple so they can see how many 
//   queries they did of each type per day
SummaryRec *Proxy::getSummaryRec ( int32_t userId32 , char accessType ) {

	// . get epoch time utc. make sure proxy time is always in sync.
	// . add that to Process.cpp to check to make sure time sync server
	//   process is running!
	int32_t now = getTimeLocal();

	static int32_t s_nextDay   = -1;
	static int32_t s_thisDay   =  0;
	static int32_t s_thisMonth =  0;
	static int32_t s_thisYear  =  0;

	if ( s_nextDay == -1 || now > s_nextDay ) {
		struct tm *timeStruct = gmtime ( (time_t *)&now );
		s_thisDay   = timeStruct->tm_mday;
		s_thisMonth = timeStruct->tm_mon+1; // 0..11 so make it 1..12
		s_thisYear  = timeStruct->tm_year + 1900;
		int32_t elapsed = timeStruct->tm_min * 60 + timeStruct->tm_sec;
		int32_t left = 86400 - elapsed;
		s_nextDay   = now + left;
	}

	// make a unique key for this summary rec, one per day per user
	uint64_t h64 = (uint32_t)userId32;
	uint32_t t = s_thisYear;
	t <<= 16;
	t |= s_thisMonth;
	t <<= 8;
	t |= s_thisDay;
	t <<= 8;
	t |= accessType;
	h64 <<= 32;
	h64 |= t;

	// use hashtable of summary rec ptrs
	int32_t *sumOffPtr;
	sumOffPtr = (int32_t *)m_srht.getValue ( &h64 );
	//SummaryRec **srp = (SummaryRec **)m_srht.getValue ( &h64 );

	// if there, return it!
	//if ( srp  ) return *srp;
	if ( sumOffPtr ) {
		SummaryRec *sr;
		sr = (SummaryRec *)(m_sumBuf.getBufStart() + *sumOffPtr);
		return sr;
	}


	//
	// otherwise, we gotta make one!
	//

	// g_errno should be set if this fails!
	if ( ! m_sumBuf.reserve ( sizeof(SummaryRec) ) )
		return NULL;

	// ref it
	SummaryRec *sr = (SummaryRec *)m_sumBuf.getBuf();

	// init it
	memset(sr,0,sizeof(SummaryRec));
	sr->m_accessType = accessType;
	sr->m_year  = s_thisYear;
	sr->m_month = s_thisMonth;
	sr->m_day   = s_thisDay;
	sr->m_totalCost = 0;
	sr->m_totalProcessTime = 0;
	sr->m_userId32 = userId32;

	// advance it
	m_sumBuf.incrementLength ( sizeof(SummaryRec) );

	int32_t sumOff = (int32_t)(((char *)sr) - m_sumBuf.getBufStart());

	// hash it
	if ( ! m_srht.addKey ( &h64 , &sumOff ) )
		return NULL;
				
	return sr;
}


//////////////////
//
// PRINT THE USER INFO
//
//////////////////


class StateUser {
public:
	//int32_t m_errno;
	TcpSocket *m_socket;
	//Msg0 m_msg0;
	//Msg4 m_msg4;
	int64_t m_sessionId64;
	int32_t m_userId32;
	//HttpRequest m_hr;
	SafeBuf m_sb;
	SafeBuf m_sb2;
	SafeBuf m_authNetMsg;
	bool m_transactionSuccessful;
	bool m_attemptedTransaction;
	float m_deposit; // dollar amount
	float m_refund;  // dollar amount (95%)
	float m_refundFee; // 5%
	int64_t m_refundTransId;
	//bool m_doWithdraw;
	//float m_deposit;
	// authorize.net's reply
	TcpSocket *m_docSocket;
	HttpRequest m_hr;
	int32_t m_submittingNewUser;
	// is this really the admin logged in as another user?
	bool m_isMasterAdmin;
	int64_t m_adminSessId;
	int32_t m_adminId;

	// for holding error msg and pointing m_depositErr to it
	SafeBuf m_tmpBuf1;

	char m_tmpBuf2[20];

	//
	/// set these from get request
	//
	char *m_user;
	char *m_pwd;
	char *m_pwd2;
	char *m_ccNum;
	char *m_ccType;
	char *m_cvv; 
	char *m_exp;
	char *m_fc;
	//float m_deposit;
	char *m_email;
	char *m_phone;
	char  m_terms;

	// european stuff
	char *m_firstName;
	char *m_lastName;
	char *m_city;
	char *m_state;
	char *m_country;
	char *m_zip;
	char *m_address;

	char *m_userError;
	char *m_pwdError;
	char *m_pwd2Error;
	char *m_ccNumError;
	char *m_ccTypeError;
	char *m_cvvError; 
	char *m_expError;
	char *m_fcError;
	char *m_depositError;
	char *m_refundError;
	char *m_emailError;
	char *m_phoneError;
	char *m_termsError;

	//int32_t m_transactionId;

	int32_t  m_error;
};

char *getAccessTypeString ( int32_t at ) {
	if ( at == AT_SEARCHFEED_OLD ) 
		return "fast search feed";
	if ( at == AT_SEARCHFEED_NEW ) 
		return "precise search feed";
	if ( at == AT_COMPETITOR_BACKLINKS ) 
		return "competitor backlinks tool";
	if ( at == AT_RELATED_QUERIES ) 
		return "related queries tool";
	if ( at == AT_MISSING_TERMS )
		return "missing terms tool";
	if ( at == AT_ADDURL )
		return "add url tool";
	return "unknown access type";
}

void gotGifWrapper ( void *su ) {
	g_proxy.gotGif ( (StateUser *)su );
}

// userId32 is always < 0x7fffffff to avoid sign bit issues
UserInfo *Proxy::getUserInfoFromId ( int32_t userId32 ) {
	UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
	int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
	// we added 1 to the userid32 to avoid using 0
	if ( userId32-1 >= ni ) return NULL;
	if ( userId32-1 < 0 ) return NULL;
	return &uis[userId32-1];
}

UserInfo *Proxy::getLoggedInUserInfo2 ( HttpRequest *hr , 
					TcpSocket *socket,
					SafeBuf *errmsg ) {
	// are they logging in for the first time, or carrying a sessionid
	// in their cookie?
	char *login = hr->getString("login",NULL);
	char *password = hr->getString("password",NULL);

	// userid32 is used with sessionid
	int64_t sessionId64 = hr->getLongLongFromCookie("sessionid",0LL);
	int32_t userId32 = hr->getLongFromCookie("userid",0);

	// if supplying "user", then they must also supply "pwd"!
	if ( ! password ) login = NULL;

	// print login page?
	if ( ! login && ! sessionId64 ) return NULL;

	// let this override in case sessionid expires
	if ( login ) sessionId64 = 0LL;

	int32_t now = getTimeLocal();

	// try to match user password or session id
	UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
	int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
	UserInfo *ui;
	int32_t i; for ( i = 0 ; i < ni ; i++ ) {
		// int16_tcut
		ui = &uis[i];
		// login with existing session id?
		if ( sessionId64 ) {
			// check it
			if ( ui->m_lastSessionId64 != sessionId64 ) continue;
			// userid32 must match too!
			if ( ui->m_userId32 != userId32 ) continue;
			// check time though. give them 10 minutes...
			if ( now - ui->m_lastActionTime > 10*60 ) {
				if ( errmsg )
					errmsg->safePrintf("Session expired. "
							   "Please "
							   "re-login.");
				sessionId64 = 0;
				return NULL;
			}
			// update timestamp so it is 10 minutes since time
			// of LAST action...
			ui->m_lastActionTime = getTimeLocal();
			return ui;
			//break;
		}
		// . login? "user" must be non-null, and "pwd" too
		// . skip if no username match
		if ( strcmp ( ui->m_login , login ) ) continue;
		// if pwd does not match, stop! error!
		if ( strcmp ( ui->m_password, password ) ) break;
		// ok, i guess password matched, set a session id
		// assign a sessionid now. make it always positive!
		int32_t num1 = rand() % 0x7fffffff;
		int32_t num2 = rand() % 0x7fffffff;
		uint64_t newSessionId64 = num1;
		newSessionId64 <<= 32;
		newSessionId64 |= num2;
		// ensure not 0
		if ( newSessionId64 == 0 ) newSessionId64 = 1;
		// add this session if to rec and re-add
		ui->m_lastSessionId64 = newSessionId64;
		ui->m_lastActionTime = getTimeLocal();
		ui->m_lastLoginIP   = socket->m_ip;
		// save session id!
		g_proxy.saveUserBufs();
		return ui;
	}

	if ( errmsg )
		errmsg->safePrintf("Login error. Incorrect username "
				   "or password.");

	return NULL;
}

// call this once at start of functions in sendPageAccount()
UserInfo *Proxy::getLoggedInUserInfo ( StateUser *su , SafeBuf *errmsg ) {

	HttpRequest *hr = &su->m_hr;
	TcpSocket *socket = su->m_socket;

	// reset shit
	su->m_userId32 = -1;
	su->m_sessionId64 = 0;
	su->m_isMasterAdmin = false;
	su->m_adminSessId = 0LL;
	su->m_adminId = 0;

	UserInfo *ui = getLoggedInUserInfo2 ( hr , socket , errmsg );

	if ( ! ui ) return NULL;

	// ok, it's good!
	su->m_userId32 = ui->m_userId32;
	su->m_sessionId64 = ui->m_lastSessionId64;

	// are they really the admin, logged in as a user?
	int64_t asi = hr->getLongLongFromCookie("adminsessid",0LL);
	if ( ! asi ) return ui;

	// admin IP be local ip for security!
	if ( ! hr->m_isLocal ) return ui;

	// see if it matches
	UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
	int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
	int32_t i; for ( i = 0 ; i < ni ; i++ ) {
		// int16_tcut
		UserInfo *ui = &uis[i];
		// skip if not admin
		if ( ! ( ui->m_flags & UIF_ADMIN ) ) continue;
		// check it
		if ( ui->m_lastSessionId64 != asi ) continue;
		// got a match
		su->m_isMasterAdmin = true;
		// save the underlying admin user info
		su->m_adminSessId = asi;
		su->m_adminId = ui->m_userId32;
	}

	return ui;
}

void removeSpaceTrails ( char *s , int32_t maxBytes ) {
	//char *end = s + gbstrlen(s) - 1;
	//while ( *end == ' ' ) {
	//	*end = '\0';
	//	end--;
	//}
	if ( ! s ) return;
	// empty already?
	if ( ! *s ) return;
	// remove all spaces now too!
	char *src = s; 
	char *dst = s;
	char *max = s + maxBytes - 1;
	for ( ; *src ; src++ ) {
		if ( *src == ' ' ) continue;
		*dst = *src;
		dst++;
		if ( dst >= max ) break;
	}
	*dst = '\0';
	return;
}


bool Proxy::doesUsernameExist ( char *user ) {
	UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
	int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
	int32_t i; for ( i = 0 ; i < ni ; i++ ) {
		// int16_tcut
		UserInfo *ui = &uis[i];
		// skip if no match
		if ( strcmp ( ui->m_login , user ) ) continue;
		return true;
	}
	return false;
}

void setFieldErrors ( StateUser *su ) {

	// is this submitting a new user?
	HttpRequest *hr = &su->m_hr;
	int32_t new1 = hr->getLong("new",0);

	// check email address format
	bool hadAt = false;
	bool hadFirstChar = false;
	bool badChar = false;
	bool hadCharAfterAt = false;
	bool hadDot = false;
	bool hadCharAfterDot = false;
	for ( char *p = su->m_email ; *p ; p++ ) {
		if ( ! hadAt && is_alnum_a(*p) ) hadFirstChar = true;
		if ( ! is_ascii(*p) ) badChar = true;
		if ( *p == '@' ) hadAt = true;
		if ( hadAt && is_alnum_a(*p ) ) hadCharAfterAt = true;
		if ( hadCharAfterAt && *p =='.' ) hadDot = true;
		if ( hadDot && is_alnum_a(*p) ) hadCharAfterDot = true;
	}
	if ( ! hadAt || ! hadFirstChar || badChar || 
	     ! hadCharAfterAt || ! hadDot || ! hadCharAfterDot ) {
		su->m_emailError = "Bad email address";
		su->m_error = 1;
	}
	// check phone #
	int32_t numDigits = 0;
	bool hadAlpha = false;
	bool badPhoneChar = false;
	for ( char *p = su->m_phone ; *p ; p++ ) {
		if ( is_alpha_a(*p) ) hadAlpha = true;
		if ( is_digit(*p) ) { numDigits++; continue; }
		if ( *p == '(' ) continue;
		if ( *p == ')' ) continue;
		if ( *p == '-' ) continue;
		if ( *p == ' ' ) continue;
		badPhoneChar = true;
	}
	if ( badPhoneChar ) {
		su->m_phoneError = "Use only digits and ( , ) "
			"or - in the phone number";
		su->m_error = 1;
	}
	if ( hadAlpha || numDigits < 7 ) {
		su->m_phoneError = "Bad phone number";
		su->m_error = 1;
	}
	// check username
	bool badUserChar = false;
	for ( char *p = su->m_user ; *p ; p++ ) {
		if ( ! is_alnum_a(*p) ) badUserChar = true;
	}
	if ( badUserChar ) {
		su->m_userError = "Username can only contain letters "
			"and numbers in ASCII";
		su->m_error = 1;
	}
	if ( ! su->m_user[0] ) {
		su->m_userError = "Username is required";
		su->m_error = 1;
	}
	if ( ! su->m_pwd[0] ) {
		su->m_pwdError = "Password required";
		su->m_error = 1;
	}
	if ( ! su->m_pwd2[0] ) {
		su->m_pwd2Error = "Repeated password required";
		su->m_error = 1;
	}
	if ( ! su->m_email[0] ) {
		su->m_emailError = "Email address required";
		su->m_error = 1;
	}
	if ( ! su->m_phone[0] ) {
		su->m_phoneError = "Phone number required";
		su->m_error = 1;
	}
	// check cvv
	bool badCVVChar = false;
	for ( char *p = su->m_cvv ; *p ; p++ ) {
		if ( ! is_digit(*p) ) badCVVChar = true;
	}
	if ( badCVVChar ) {
		su->m_cvvError = "CVV can only be digits, no spaces";
		su->m_error = 1;
	}
	if ( ! su->m_cvv[0] ) {
		su->m_cvvError = "cvv is required";
		su->m_error = 1;
	}
	// check ccnum
	bool badCC = false;
	for ( char *p = su->m_ccNum ; *p ; p++ ) {
		if ( ! is_digit(*p) &&
		     *p != ' ' &&
		     *p != '*' &&
		     *p != '-' ) 
			badCC = true;
	}
	if ( badCC ) {
		su->m_ccNumError = "Credit Card Number can only "
			"have digits spaces and hyphens in it";
		su->m_error = 1;
	}
	if ( ! su->m_ccNum[0] ) {
		su->m_ccNumError = "credit card # is required";
		su->m_error = 1;
	}
	// MM/YY
	int32_t digitsBefore = 0;
	bool hadSlash = false;
	int32_t digitsAfter = 0;
	bool badExpChar = false;
	for ( char *p = su->m_exp ; *p ; p++ ) {
		if ( ! is_digit(*p) && *p != '/' )
			badExpChar = true;
		if ( ! hadSlash && is_digit(*p) )
			digitsBefore++;
		if ( *p == '/' ) hadSlash = true;
		if ( hadSlash && is_digit(*p) )
			digitsAfter++;
	}
	if ( digitsBefore <= 0 ||
	     digitsBefore > 2 ||
	     ! hadSlash ||
	     digitsAfter != 2 ) {
		su->m_expError = "Format must be \"MM/YY\" where "
			"MM is the month number and YY is the year "
			"number";
		su->m_error = 1;
	}
	int32_t elen = 0;
	if ( su->m_exp ) elen = gbstrlen(su->m_exp);
	if ( ! su->m_expError[0] && elen >= 4 ) {
		int32_t yy = atoi(su->m_exp + elen-2);
		if ( yy < 13 ) {
			su->m_expError = "Expiration year can not be "
				"before 2013";
			su->m_error = 1;
		}
	}
	if ( ! su->m_exp[0] ) {
		su->m_expError = "credit card expiration is required";
		su->m_error = 1;
	}
	if ( ! su->m_ccType[0] ) {
		su->m_ccTypeError = "credit card type is required";
		su->m_error = 1;
	}
	if ( ! su->m_fc[0] ) {
		su->m_fcError = "xml feed pass code is required";
		su->m_error = 1;
	}
	// TODO: check other aspects of the provided data...
	if ( su->m_pwd[0] && 
	     su->m_pwd2[0] && 
	     strcmp(su->m_pwd,su->m_pwd2) ){
		su->m_pwd2Error = "Password does not match the "
			"above password";
		su->m_error = 1;
	}
	// make sure username is new
	if ( new1 && 
	     su->m_user[0] &&
	     g_proxy.doesUsernameExist ( su->m_user ) ) {
		su->m_userError = "Username already exists. "
			"Try another.";
		su->m_error = 1;
	}
	if ( new1 && ! su->m_terms ) {
		su->m_termsError = "You must agree to the terms to "
			"proceed";
		su->m_error = 1;
	}
	if ( new1 && su->m_deposit < MINCHARGE ) {
		su->m_tmpBuf1.safePrintf("You must initially deposit "
					 "a minimum of $%.02f to set up "
					 "an account", MINCHARGE);
		su->m_depositError = su->m_tmpBuf1.getBufStart();
		su->m_error = 1;
	}
}

bool printLoginPage ( StateUser *su , SafeBuf *errmsg ) {

	HttpRequest *hr = &su->m_hr;

	// print login page?
	char *bs1 = "<br><font color=red>";
	char *bs2 = "</font>";
	char *msg = "";
	if ( errmsg ) msg = errmsg->getBufStart();
	if ( errmsg && errmsg->length() == 0 ) {
		bs1 = "";
		bs2 = "";
		msg = "";
	}
	char *u = hr->getString("login",NULL,"");
	char *pwd = hr->getString("password",NULL,"");
	SafeBuf sb;
	sb.safePrintf("<html><body>"
		      "<title>Gigablast - Login</title>"
		      
		      "<center>"
		      "<a href=/>"
		      "<img src=http://www.gigablast.com/logo-med.jpg "
		      "height=122 width=500>"
		      "</a>"
		      "</center>"
		      "<br>"
		      
		      "<form method=post action=/account>"
		      
		      "<input type=hidden name=fromloginpage value=1>"
		      
		      "<table width=100%% cellpadding=5 cellspacing=0 "
		      "border=0>"
		      
		      "<tr bgcolor=#0340fd>"
		      "<th colspan=2>"
		      "<font color=33dcff>"
		      "User Login</font>"
		      "</th>"
		      "</tr>"
		      "<tr><td><br>"
		      
		      "<table cellpadding=10>"
		      "<tr>"
		      "<td>Username:</td>"
		      "<td><input type=text name=login size=20 value="
		      "\"%s\"></td>"
		      "</tr>"
		      "<tr>"
		      "<td>"
		      "Password:</td>"
		      "<td><input type=password name=password size=20 "
		      "value=\"%s\">"
		      "%s"
		      "%s"
		      "%s"
		      "</td>"
		      "</tr>"
		      "<tr><td colspan=2>"
		      "<input type=submit name=loggingin value=OK>"
		      "</td></tr>"
		      "</table>"
		      
		      "</form>"
		      
		      "<br>"
		      "<b>OR</b>"
		      "<br>"
		      "<br>"
		      "<a href=/account?new=1>Create a new account"
		      "</a>"
		      
		      "</table>"
		      
		      "</body>"
		      "</html>"
		      , u 
		      , pwd
		      , bs1
		      , msg
		      , bs2
		      );
	g_httpServer.sendDynamicPage ( su->m_socket, 
				       sb.getBufStart(), 
				       sb.length(),
				       0 , // cachetime in secs
				       false , // postreply?
				       "text/html", // content type
				       -1, // http status -1->200
				       NULL,//cookiePtr,
				       "utf-8" );
	mdelete(su,sizeof(StateUser),"usprox");
	delete(su);
	return true;
}

bool printLogoutPage ( StateUser *su ) {

	//HttpRequest *hr = &su->m_hr;
	// print login page?
	SafeBuf sb;
	sb.safePrintf("<html><body>"
		      "<title>Gigablast - Logout</title>"
		      
		      "<center>"
		      "<a href=/>"
		      "<img src=http://www.gigablast.com/logo-med.jpg "
		      "height=122 width=500>"
		      "</a>"
		      "</center>"
		      "<br>"
		      );

	if ( su->m_userId32 <= 0 ) 
		sb.safePrintf("You were not logged in. Why are you trying "
			      "to logout again?");
	else
		sb.safePrintf("You have been successfully logged out.");

	sb.safePrintf("<br><br>"
		      "<a href=/>Return to homepage</a> or"
		      "<br><br>"
		      "<a href=/account>Return to login page</a>");

	sb.safePrintf("</table>"
		      "</body>"
		      "</html>"
		      );
	// getLoggedInUserId should have set the sessionId64
	SafeBuf cb;


	// if we are the admin logged in as someone else in 
	// disguise, then redirect back to our main page. but if we are
	// the admin and NOT logged in as someone else, then log us out
	// as normal!
	if ( su->m_isMasterAdmin && su->m_adminSessId != su->m_sessionId64 ) {
		sb.reset();
		sb.safePrintf("<META HTTP-EQUIV=refresh "
			      "content=\"0;URL=/account\">");
		cb.safePrintf("Set-Cookie: sessionid=%"INT64";\r\n"
			      "Set-Cookie: userid=%"INT32";\r\n"
			      , su->m_adminSessId
			      , su->m_adminId );
	}
	else
		cb.safePrintf("Set-Cookie: sessionid=0;\r\n"
			      "Set-Cookie: userid=0;\r\n"
			      "Set-Cookie: adminsessid=0;\r\n"
			      );

	char *cookiePtr = NULL;
	if ( cb.length() ) cookiePtr = cb.getBufStart();


	g_httpServer.sendDynamicPage ( su->m_socket, 
				       sb.getBufStart(), 
				       sb.length(),
				       0 , // cachetime in secs
				       false , // postreply?
				       "text/html", // content type
				       -1, // http status -1->200
				       cookiePtr,
				       "utf-8" );
	mdelete(su,sizeof(StateUser),"usprox");
	delete(su);
	return true;
}

bool sendRedirect ( StateUser *su ) {

	//HttpRequest *hr = &su->m_hr;

	// they logged in successfully from the post, redirect
	SafeBuf rb;
	rb.safePrintf("<META HTTP-EQUIV=refresh "
		      "content=\"0;URL=/account?rd=1\">");

	// this must be legit!
	if ( su->m_sessionId64 <= 0 ) { char *xx=NULL;*xx=0; }
	if ( su->m_userId32    <= 0 ) { char *xx=NULL;*xx=0; }

	// getLoggedInUserId should have set the sessionId64
	SafeBuf cb;
	cb.safePrintf("Set-Cookie: sessionid=%"INT64";\r\n"
		      "Set-Cookie: userid=%"INT32";\r\n"
		      ,su->m_sessionId64
		      ,su->m_userId32);
	char *cookiePtr = NULL;
	if ( cb.length() ) cookiePtr = cb.getBufStart();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	g_httpServer.sendDynamicPage ( su->m_socket, 
				       rb.getBufStart(), 
				       rb.length(),
				       0 , // cachetime in secs
				       false , // postreply?
				       "text/html", // content type
				       -1, // http status -1->200
				       cookiePtr,
				       "utf-8" );
	// i guess it copies the safebuf contents..
	mdelete(su,sizeof(StateUser),"usprox");
	delete(su);
	return true;
}




// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying user info
// . use https only!
// . password and username transmitted with post for login
// . just set cookie to the session id then
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageAccount ( TcpSocket *s , HttpRequest *hr2 ) {

	// only call this from host #0! so we can use msg5 not msg0
	// and store all of userdb on host #0 and twins.
	if ( ! g_proxy.isProxy() ) { char *xx=NULL;*xx=0; }

	// ensure only https! this is sensitive stuff
	if ( ! s->m_ssl ) {
		char *msg = "Only access account info using https!";
		g_httpServer.sendErrorReply ( s, 500, msg );
		return true; 
	}

	bool err5 = false;
	if ( err5 ) {
	hadError5:
		g_errno = ENOMEM;
		log("proxy: new(%"INT32"): %s",(int32_t)sizeof(StateUser),mstrerror(g_errno));
		g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
		return true;
	}

	//
	// shit, we gotta save state since makeGif can block
	//
	StateUser *su;
	try { su = new (StateUser) ; }
	catch ( ... ) { 
	  goto hadError5;
	}
	mnew ( su, sizeof(StateUser), "suprox" );

	// copy shit. if it fails return 500 http error.
	if ( ! su->m_hr.copy ( hr2 ) ) {
		mdelete(su,sizeof(StateUser),"suprox");
		delete(su);
		goto hadError5;
	}

	// use that now
	HttpRequest *hr = &su->m_hr;

	// copy socket and ui offset
	su->m_socket = s;

	// bogus values
	su->m_sessionId64 = 0;
	su->m_userId32 = -1;

	// reset flags
	su->m_transactionSuccessful = false;
	su->m_attemptedTransaction = false;
	su->m_deposit = hr->getFloat ("deposit",0.0);
	su->m_refund  = hr->getFloat ("refund",0.0);
	su->m_refundFee = 0.0;
	// refunds require the original transaction's id from authorize.net
	su->m_refundTransId = hr->getLongLong("transid",0LL);
	
	// set this crap from the GET request
	su->m_user    = hr->getString("user",NULL,"");
	su->m_pwd     = hr->getString("pwd",NULL,"");
	su->m_pwd2    = hr->getString("pwd2",NULL,"");
	su->m_ccNum   = hr->getString("cc",NULL,"");
	su->m_ccType  = hr->getString("cctype",NULL,"");
	su->m_cvv     = hr->getString("cvv",NULL,"");
	su->m_exp     = hr->getString("exp",NULL,"");
	su->m_fc      = hr->getString("fc",NULL,"");
	su->m_phone   = hr->getString("phone",NULL,"");
	su->m_email   = hr->getString("email",NULL,"");
	su->m_terms   = hr->getLong("terms",0);

	// clear these
	su->m_error       = 0;
	su->m_userError   = "";
	su->m_pwdError    = "";
	su->m_pwd2Error   = "";
	su->m_cvvError    = "";
	su->m_ccNumError  = "";
	su->m_expError    = "";
	su->m_ccTypeError = "";
	su->m_depositError= "";
	su->m_refundError = "";
	su->m_fcError     = "";
	su->m_phoneError  = "";
	su->m_emailError  = "";
	su->m_termsError  = "";
	// european stuff
	su->m_firstName   = "";
	su->m_lastName    = "";
	su->m_address     = "";
	su->m_city        = "";
	su->m_state       = "";
	su->m_country     = "";
	su->m_zip         = "";

	// remove trailing spaces from all. include max bytes too includng \0
	removeSpaceTrails ( su->m_user , 32 );
	removeSpaceTrails ( su->m_pwd , 32 );
	removeSpaceTrails ( su->m_pwd2 , 32 );
	removeSpaceTrails ( su->m_ccNum , 64 );
	removeSpaceTrails ( su->m_ccType , 32);
	removeSpaceTrails ( su->m_cvv , 5);
	removeSpaceTrails ( su->m_exp , 6 );
	removeSpaceTrails ( su->m_fc ,16 );
	removeSpaceTrails ( su->m_phone ,30);
	removeSpaceTrails ( su->m_email ,80);
	// european stuff for credit card processing
	removeSpaceTrails ( su->m_firstName,40);
	removeSpaceTrails ( su->m_lastName,40);
	removeSpaceTrails ( su->m_address,80);
	removeSpaceTrails ( su->m_city,30);
	removeSpaceTrails ( su->m_state,30);
	removeSpaceTrails ( su->m_country,30);
	removeSpaceTrails ( su->m_zip,30);

	// are we dealing with a form submission?
	int32_t submit = hr->getLong("submitted",0);

	int32_t new1 = hr->getLong("new",0);
	
	int32_t edit = hr->getLong("edit",0);

	int32_t logout = hr->getLong("logout",0);

	if ( new1 ) edit = 0;

	if ( new1 ) su->m_submittingNewUser = 1;
	else        su->m_submittingNewUser = 0;

	// they must be logged in
	SafeBuf errmsg;

	// this will set su->m_userId32 and su->m_sessionId64
	UserInfo *ui = g_proxy.getLoggedInUserInfo ( su , &errmsg ) ;

	if ( logout )
		return printLogoutPage ( su );

	// if not logged in, print the login page
	if ( su->m_userId32 <= 0 && ! new1 ) 
		return printLoginPage( su , &errmsg );


	// if not doing a submit of an edit or a new user action, 
	// then take these values from the UserInfo
	if ( ! submit && ! new1 ) {
		su->m_user = ui->m_login;
		su->m_pwd  = ui->m_password;
		su->m_pwd2 = ui->m_password;
		su->m_ccNum   = ui->m_creditCardNum;
		su->m_ccType  = ui->m_creditCardType;
		su->m_cvv     = ui->m_cvv;
		su->m_exp     = ui->m_creditCardExpires;
		su->m_fc      = ui->m_xmlFeedCode;
		//su->m_deposit = 0.0;
		su->m_phone   = ui->m_phone;
		su->m_email   = ui->m_email;
		su->m_firstName = ui->m_firstName;
		su->m_lastName = ui->m_lastName;
		su->m_city = ui->m_city;
		su->m_state = ui->m_state;
		su->m_country = ui->m_country;
		su->m_zip = ui->m_zip;
		su->m_address = ui->m_address;
	}



	// if they posted to us from the login page, then redirect them
	// so the client can refresh on their browser or go back to the page
	// on their browser. otherwise it asks if you want to confirm the post
	// submission.
	if ( hr->getLong("fromloginpage",0) )
		return sendRedirect ( su );

	// we are logged in at this point, so get this
	//UserInfo *ui = g_proxy.getUserInfoFromId ( su->m_userId32 );

	// they submitted a deposit/withdrawal request?
	if ( su->m_deposit != 0.0 && ui ) {

		if ( su->m_deposit < 0.0 ) {
			su->m_depositError = "Deposit amount must be positive";
			su->m_error = 1;
			return g_proxy.printAccountingInfoPage ( su );
		}
		// error?
		if ( su->m_deposit < MINCHARGE ) {
			su->m_tmpBuf1.safePrintf("Minimum deposit is $%.02f",
						 MINCHARGE);
			su->m_depositError = su->m_tmpBuf1.getBufStart();
			su->m_error = 1;
			return g_proxy.printAccountingInfoPage ( su );
		}
		// this will ultimately display the account info page
		// on success i guess!
		return g_proxy.hitCreditCard ( su );
	}

	if ( su->m_refund != 0.0 ) {
		// deal with bad refund amounts
		if ( su->m_refund < 0.0 ) {
			su->m_refundError = "Refund amount must be positive";
			su->m_error = 1;
			return g_proxy.printAccountingInfoPage ( su );
		}
		// ensure they have enough if they are withdrawing, not deposit
		if ( ui->m_accountBalance < su->m_refund ) {
			su->m_refundError = "You can not refund more than "
				"the account balance. ";
			su->m_error = 1;
			return g_proxy.printAccountingInfoPage ( su );
		}
		return g_proxy.hitCreditCard ( su );
	}



	// if adding new user with blank form generate some defaults
	if ( new1 && ! submit ) {
		su->m_deposit = MINCHARGE;
		// make a random number for the search feed code
		int32_t rc = rand() & 0x7fffffff;
		sprintf(su->m_tmpBuf2,"%"INT32"",rc);
		su->m_fc = su->m_tmpBuf2;
	}


	// . try to detect errors on submitted data
	// . sets StateUser::m_userError, m_pwdError, etc.
	// . sets StateUser::m_error on any error being set
	if ( submit ) 
		setFieldErrors ( su );

	// if the submitted data has something wrong then tell the user
	// what is wrong and allow them to fix it and resubmit
	if ( submit && su->m_error )
		return g_proxy.printEditForm( su );

	// . if we couldn't find anything wrong, try to make a deposit
	// . do not add the userinfo record itself until deposit is made ok
	if ( submit && new1 )
		return g_proxy.hitCreditCard ( su );


	if ( submit && edit ) {
		// get the userinfo class
		SafeBuf errmsg;
		// copy edited info over
		strcpy(ui->m_password,su->m_pwd);
		strcpy(ui->m_xmlFeedCode,su->m_fc);
		// . do not update this if it has ****'s in it
		// . but if they entered a whole new credit card # then
		//   update it, in which case we will have no *'s in it
		if ( su->m_ccNum[0] != '*' )
			strcpy(ui->m_creditCardNum,su->m_ccNum);
		strcpy(ui->m_cvv,su->m_cvv);
		strcpy(ui->m_creditCardExpires,su->m_exp);
		strcpy(ui->m_creditCardType,su->m_ccType);
		strcpy(ui->m_phone,su->m_phone);
		strcpy(ui->m_email,su->m_email);
		// save the new info
		g_proxy.saveUserBufs();
		// now redirect to accounting info page so they can hit the
		// back button to go back to this page without having to
		// confirm a post resubmission
		log("proxy: sending redirect 2");
		return sendRedirect ( su );		
		// now display it in the normal page display
		//return g_proxy.printAccountingInfoPage ( su );
	}

	// sanity test. submit should NOT be set here!!!
	if ( submit ) { 
		mdelete(su,sizeof(StateUser),"suprox");
		delete(su);
		log("proxy: bad submit");
		g_errno = EBADENGINEER;
		g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
		return true;
	}

	// print blank add new user form. "GET /account?new=1"
	if ( new1 ) 
		return g_proxy.printEditForm ( su );

	// edit existing user account. "GET /account?edit=1"
	if ( edit ) 
		return g_proxy.printEditForm ( su );


	return g_proxy.printAccountingInfoPage ( su );
}

void gotDepositDocWrapper ( void *st , TcpSocket *ts ) {
	StateUser *su = (StateUser *)st;
	su->m_docSocket = ts;
	g_proxy.gotDepositDoc ( su );
}

/*
int32_t Proxy::getNextTransactionId ( ) {
	// make sure s_lastTransId is set to our last transaction id PLUS one!
	static int32_t s_lastTransId = -1;

	if ( s_lastTransId == -1 ) {
		int32_t nd = m_depositBuf.length() / sizeof(DepositRec);
		DepositRec *drs = (DepositRec *)m_depositBuf.getBufStart();
		for ( int32_t i = 0 ; i < nd ; i++ ) {
			DepositRec *dr = &drs[i];
			if ( dr->m_transactionId > s_lastTransId )
				s_lastTransId = dr->m_transactionId;
		}
		// advance over that
		s_lastTransId++;
	}

	int32_t save = s_lastTransId;
	s_lastTransId++;
	return save;
}
*/

// . returns false if blocked, true otherwise
// . sets g_errno on error and returns true
// . sets su->m_authenticated to true or false
bool Proxy::hitCreditCard ( StateUser *su ) {

	// assume not authentic
	su->m_transactionSuccessful = false;
	su->m_attemptedTransaction = true;
	//HttpRequest *hr = &su->m_hr;

	// ensure plenty room
	int32_t need = sizeof(DepositRec) * 10;
	// on error, set the m_authNetMsg
	if ( ! m_depositBuf.reserve ( need ) ) 
		return gotDepositDoc(su);

	// get a unique transaction id
	//su->m_transactionId = getNextTransactionId();

	// connect to authorize.net
	SafeBuf url;
	bool testMode = false;


	if ( testMode )
		url.safePrintf("https://test.authorize.net/gateway/"
			       "transact.dll?");
	else
		url.safePrintf( "https://secure.authorize.net/gateway/"
				"transact.dll?");
	

	//UserInfo *ui = getUserInfoFromId ( su->m_userId32 );

	// make the POST
	// see  http://developer.authorize.net/guides/AIM/wwhelp/wwhimpl/js/html/wwhelp.htm 
	// see (List of API Fields at end)

	url.safePrintf("x_card_num=%s"
		       //"&x_cust_id=%"INT32""
		       "&x_customer_ip=%s"
		       "&x_delim_data=1" // must be 1 for AIM
		       "&x_description=gigablast.com+search+engine+services"
		       //"&x_invoice_num=%"INT32""
		       "&x_email_customer=1" // %"INT32""
		       "&x_version=3.0"
		       "&x_currency_code=USD"
		       "&x_recurring_billing=0"
		       "&x_relay_response=0" // we are using AIM
		       , su->m_ccNum
		       //, ui->m_userId32
		       , iptoa(su->m_socket->m_ip)
		       //, su->m_invoiceNum // also store in DepositRec
		       //, ui->m_sendEmails // send email to user
		       );

	// sanity check
	//if ( su->m_invoiceNum < 0 ) { char *xx=NULL;*xx=0; }


	// a negative amount will be a withdraw
	if ( su->m_refund != 0.0 ) {
		// we'll record this in deposit buf
		su->m_refundFee = .05 * su->m_refund;
		// this is kinda bogus...
		url.safePrintf("&x_type=CREDIT"
			       // this is kinda bogus since we do not see it
			       // has refunding original transactions but 
			       // rather getting a withdrawal
			       "&x_trans_id=%"INT64""

			       // do we need x_split_tender_id ???
			       // use that INSTEAD of x_trans_id...
			       //"&x_split_tender_id=%"INT64""

			       "&x_amount=%.02f"
			       , su->m_refundTransId
			       , su->m_refund - su->m_refundFee );
	}
	else
		url.safePrintf("&x_type=AUTH_CAPTURE"
			       "&x_amount=%.02f"
			       , su->m_deposit );


	// user's email address:
	url.safePrintf("&x_email="); 
	url.urlEncode ( su->m_email );
	url.safePrintf("&x_exp_date=");
	url.urlEncode ( su->m_exp );
	url.safePrintf("&x_phone=");
	url.urlEncode ( su->m_phone );
	//url.safePrintf("&x_card_num=");
	//url.urlEncode( su->m_ccNum );
	//
	// INSERT YOUR secret transaction/api key for authorize.net
	//

	url.safePrintf("&x_tran_key=%s",g_secret_tran_key);
	url.safePrintf("&x_login=%s",g_secret_api_key);

	//url.safePrintf("&x_tran_key=xxxxxxxxxxxxxxx");
	//url.safePrintf("&x_login=yyyyyyyyyy");

	// european requires stuff
	/*
	url.safePrintf("&x_first_name=");
	url.urlEncode ( ui->m_firstName );
	url.safePrintf("&x_last_name=");
	url.urlEncode ( ui->m_lastName );
	url.safePrintf("&x_address=");
	url.urlEncode ( ui->m_address );
	url.safePrintf("&x_city=");
	url.urlEncode ( ui->m_city );
	url.safePrintf("&x_state=");
	url.urlEncode ( ui->m_state );
	url.safePrintf("&x_zip=");
	url.urlEncode( ui->m_zip );
	*/

	log("proxy: contacting authorize.net");

	// show it
	log("proxy: authorize.netrequest=%s",url.getBufStart());

	// returns false if blocked, true otherwise
	if ( ! g_httpServer.getDoc (  url.getBufStart() ,
				      0 , // ip, 0 means unknown
				      0 , // offset
				      -1 , // size (use GET request)
				      0 , // ifmodifiedsince
				      su , // state
				      gotDepositDocWrapper ,
				      60 * 1000 , // 60 sec timeout
				      0 , // proxyip
				      0 , // proxyport
				      1000000, // 1MB maxtextdoclen
				      1000000, // 1MB maxotherdoclen
				      "Gigabot/1.0" , // useragent
				      "HTTP/1.0", // proto
				      true , // do post? YES!!!
				      NULL )) // cookie
		// return false if it blocked
		return false;

	// this never blocks?
	if ( ! gotDepositDoc ( su ) )
		log("proxy: got error in gotDepositDoc: %s",
		    mstrerror(g_errno));

	// never blocks
	return true;
}

int64_t rand63 ( ) {
	uint32_t r1 = rand();
	uint32_t r2 = rand();
	uint64_t r = r1;
	r <<= 32;
	r |= r2;
	r &= 0x7fffffffffffffffLL;
	return r;
}

// . parse a field out of an authorize.net reply	
// . first fieldNum is 0
char *getField ( char *docHTML , int32_t fieldNum , int32_t *replyMsgLen ) {

	char *p = docHTML;
	int32_t numCommas = 0;
	for ( ; *p ; p++ ) {
		if ( *p == ',' ) numCommas++;
		if ( numCommas == fieldNum ) { p++; break; }
	}
	// return NULL if not there
	if ( ! *p ) return NULL;
	// find next comma
	char *next = strchr ( p , ',' );
	// not there?
	int32_t plen;
	if ( ! next ) plen = gbstrlen(p);
	else          plen = next - p;
	// use that
	*replyMsgLen = plen;
	return p;
}


// . returns false and sets g_errno on error, true otherwise
// . does not block i guess
bool Proxy::gotDepositDoc ( StateUser *su ) {

	// another error? ETCPTIMDOUT?
	if ( g_errno ) {
		log("proxy: bad authorize.net reply");
		su->m_error = 1;
		su->m_authNetMsg.safePrintf("Error communicating with "
					    "authorize.net to charge "
					    "credit card. %s",
					    mstrerror(g_errno));
		// there was an error - redisplay the new user form
		if ( su->m_submittingNewUser ) return printEditForm ( su );
		// otherwise, we were making another deposit
		return printAccountingInfoPage ( su );
	}

	// parse reply
	TcpSocket *ts = su->m_docSocket;
	char *reply = ts->m_readBuf;
	int32_t replySize = ts->m_readOffset;
	HttpMime mime;
	Url redirUrl;
	mime.set ( reply , replySize , &redirUrl );

	// log it
	log("proxy: authorize.net reply: %s",reply);

	int32_t status = mime.getHttpStatus();

	if ( status != 200 ) {
		log("proxy: bad authorize.net mime stats = %"INT32"",status);
		su->m_error = 1;
		su->m_authNetMsg.safePrintf("Error communicating with "
					    "authorize.net to charge "
					    "credit card. HTTP status "
					    "%"INT32"",status);
		// there was an error - redisplay the new user form
		if ( su->m_submittingNewUser ) return printEditForm ( su );
		// otherwise, we were making another deposit
		return printAccountingInfoPage ( su );
	}

	// get msg
	char *docHTML = reply + mime.getMimeLen();
	//int32_t docLen = replySize - mime.getMimeLen();

	// a,b,c,ERRMSG,...
	// see http://developer.authorize.net/guides/AIM/wwhelp/wwhimpl/js/html/wwhelp.htm
	// the first number:
	// 1: approved
	// 2: declined
	// 3: error
	// 4: held for review
	int32_t returnCode = atol(docHTML);

	// show it has msg
	int32_t replyMsgLen = 0;
	char *replyMsg = getField ( docHTML , 3 , &replyMsgLen );


	if ( replyMsgLen ) 
		su->m_authNetMsg.safePrintf("<br><br>");

	// then the reply msg
	su->m_authNetMsg.safeMemcpy ( replyMsg , replyMsgLen );

	// get the reason code
	int32_t scLen; char *scPtr = getField(docHTML,2,&scLen);
	int32_t rc = 0; if ( scPtr ) rc = atol2(scPtr,scLen);
	if ( returnCode != 1 && su->m_refund != 0.0 && rc == 54 ) 
		// if this happens consider issuing a void and we can
		// save the credit card processing fees!!!
		su->m_authNetMsg.safePrintf(" You need to wait 25 hours for "
					    "the charge to settle before a "
					    "refund can be issued. ");


	su->m_authNetMsg.pushChar('\0');

	// error? this means it has NOT been approved
	if ( returnCode != 1 ) {
		su->m_error = 1;
		// there was an error - redisplay the new user form
		if ( su->m_submittingNewUser ) return printEditForm ( su );
		// otherwise, we were making another deposit
		return printAccountingInfoPage ( su );
	}

	// ensure userid32 unique!
	if ( su->m_submittingNewUser ) {
		int32_t numUsers = m_userInfoBuf.length() / sizeof(UserInfo) ;
		su->m_userId32 = numUsers + 1;
	}


	float amount;

	// make it negative for storing in deposit rec
	if ( su->m_refund != 0.0 ) amount = -1.0 * su->m_refund;
	else                       amount =        su->m_deposit;

	// log it for posterity
	log("proxy: successfully charged $%.02f to userid %"INT32""
	    ,amount
	    ,su->m_userId32
	    );


	int32_t need = sizeof(DepositRec) * 2;
	if ( ! m_depositBuf.reserve (need) ) {
		su->m_error = 1;
		su->m_authNetMsg.safePrintf("Error adding deposit to buf. "
					    "userid32=%"UINT32" amt=$%.02f"
					    ,su->m_userId32
					    ,amount);
		char *xx=NULL;*xx=0; 
	}

	// good, now add it to buffer
	DepositRec dr;
	memset ( &dr , 0 , sizeof(DepositRec) );
	int32_t now = getTimeLocal();
	if ( su->m_userId32 <= 0 ) { char *xx=NULL;*xx=0; }
	dr.m_userId32 = su->m_userId32;
	dr.m_depositDate   = now;

	//
	// . we need this for doing CREDITs/withdrawals back to the user
	// . i've seen this # > 5B so use a int64_t
	//
	int32_t tlen;
	char *tid = getField(docHTML,6,&tlen);
	if ( tid ) {
		char c = tid[tlen];
		tid[tlen] = '\0';
		int64_t transId = atoll(tid);
		tid[tlen] = c;
		// the authorize.net transaction id
		dr.m_authorizeNetTransactionId = transId;
		log("proxy: got transactionid=%"INT64"",transId);
	}
	
	// if depositing...
	if ( su->m_deposit != 0.0 ) {
		dr.m_depositAmount = su->m_deposit;
		dr.m_flags = DRF_DEPOSIT;
		m_depositBuf.safeMemcpy ( &dr , sizeof(DepositRec));
	}
	// withdraw fee? add that into this table too
	else {
		if ( su->m_refundFee <= 0.0 ) { char *xx=NULL;*xx=0; }
		dr.m_depositAmount = -1 * su->m_refundFee;
		dr.m_flags = DRF_WITHDRAW_FEE;
		m_depositBuf.safeMemcpy ( &dr , sizeof(DepositRec) );
		// amount is negative, withdraw fee is positive
		dr.m_depositAmount = -1*(su->m_deposit - su->m_refundFee);
		dr.m_flags = DRF_WITHDRAW;
		m_depositBuf.safeMemcpy ( &dr , sizeof(DepositRec) );
	}


	// success!
	su->m_transactionSuccessful = true;

	// if not submitting a new user, just add the amount to our
	// account balance and save, then print the accounting info page
	if ( ! su->m_submittingNewUser ) {
		// get user rec
		UserInfo *ui = g_proxy.getUserInfoFromId ( su->m_userId32 );
		// add it. will be negative for a refund.
		ui->m_accountBalance += amount;
		// save the deposit to disk
		saveUserBufs();
		// print the accounting info page with success msg
		su->m_authNetMsg.safePrintf("Despoit of $%.02f was "
					    "successful",amount);
		//return printAccountingInfoPage ( su );
		// shit actually, to avoid re-submits more so, make it
		// redirect i guess
		log("proxy: sending redirect 3");
		return sendRedirect ( su );		
	}



	/////////
	//
	// add the new user here!!
	//
	/////////
	UserInfo u2;
	memset ( &u2 , 0 , sizeof(UserInfo) );
	strncpy(u2.m_login, su->m_user,30);
	strncpy(u2.m_password, su->m_pwd,30);
	strncpy(u2.m_xmlFeedCode, su->m_fc,14);
	strncpy(u2.m_creditCardNum, su->m_ccNum,62);
	strncpy(u2.m_cvv, su->m_cvv,4);
	strncpy(u2.m_creditCardExpires, su->m_exp,5);
	strncpy(u2.m_creditCardType, su->m_ccType,30); // visa
	u2.m_pending = 0;
	u2.m_flags = 0;
	u2.m_accountBalance = su->m_deposit;
	//strncpy(u2.m_creditCardAddress,ccAddr,250);
	u2.m_signUpDate = getTimeLocal();
	u2.m_userId32 = su->m_userId32;
	u2.m_lastSessionId64 = rand63();
	u2.m_lastActionTime = now;
	u2.m_lastLoginIP = su->m_socket->m_ip;

	// note this as well
	log ("proxy: successfully added userid %"INT32" to userbuf",su->m_userId32);

	// store it
	if ( ! m_userInfoBuf.safeMemcpy ( &u2 , sizeof(UserInfo) ) )
		return false;

	// save it. this blocks for now...
	if ( ! saveUserBufs() ) {
		log("proxy: error saving user bufs: %s",mstrerror(g_errno));
		return false;
	}

	// we are auto-logged in
	//su->m_userId32 = userId32;
	su->m_sessionId64 = u2.m_lastSessionId64;

	// now print the accounting page. they are an official user now.
	// no! because if they try to reload that page or go back to that
	// page it asks to confirm form resubmission! then it will just
	// say that that username already exists! 
	//return printAccountingInfoPage ( su );
	// so instead, let's redirect them to /account and set the cookie!
	// make a cookie in case they just logged in through this page!
	// otherwise the login doesn't "stick"
	return sendRedirect ( su );
}

// returns false and sets g_errno on error, true otherwise. does not block
// i guess...
bool Proxy::printEditForm ( StateUser *su ) {

	HttpRequest *hr = &su->m_hr;
	TcpSocket *s = su->m_socket;

	// print form to add new user? or submitting new user info?
	int32_t new1 = hr->getLong("new",0);

	// print form to edit user? or did they submit their edits?
	int32_t edit = hr->getLong("edit",0);

	// are we dealing with a form submission?
	int32_t submit = hr->getLong("submitted",0);

	// get the user we are editing into "ui"
	UserInfo *ui = NULL;
	SafeBuf errmsg;
	if ( edit ) ui = getUserInfoFromId ( su->m_userId32 );

	char *msg = "";

	// wtf?
	if ( edit && !ui ) {
		msg = "Could not find user to edit.";
		//hadError7:
		mdelete(su,sizeof(StateUser),"usprox");
		delete(su);
		g_httpServer.sendErrorReply(s,500,msg);
		return true;
	}

	SafeBuf sb;

	// show the user info form
	sb.safePrintf( "<html>"
		       "<title>Gigablast - Account Information</title>"
		       "<body>"
		       "<center>"
		       "<a href=/>"
		       "<img src=http://www.gigablast.com/logo-med.jpg "
		       "height=122 width=500>"
		       "</a>"
		       "</center>"
		       "<br>"

		      "<form method=post name=fff action=/account>"

		       "<table width=100%% cellpadding=5 cellspacing=0 "
		       "border=0>"

		       "<tr bgcolor=#0340fd>"
		       "<th colspan=2>"
		       "<font color=33dcff>"
		       "New User Information</font>"
		       "</th>"
		       "</tr>"
		       "<tr><td><br>"
		       );


	/*
	if ( new1 && submit && ! su->m_deposit ) {
		sb.safePrintf("<br><font color=red size=-1><b>"
			      "Unable to charge "
			      "Credit Card. Please check credit card info "
			      "below and verify it is accurate before "
			      "resubmitting.");
		sb.cat ( su->m_authNetMsg );
		sb.safePrintf("</font><br>");
	}
	*/
		


	sb.safePrintf( "<center>"
		       "<table width=400px cellpadding=6>"
		       );


	if ( su->m_attemptedTransaction && ! su->m_transactionSuccessful ) {
		sb.safePrintf("<tr><td colspan=10><b><font color=red>"
			      "Failed to deposit $%.02f. %s"
			      //" Please ensure your credit card info is "
			      //"correct."
			      "</font></td></tr>"
			      , su->m_deposit
			      , su->m_authNetMsg.getBufStart()
			      );
	}


	// username
	if ( new1 ) {	
		sb.safePrintf("<tr>"
			      "<td valign=top>Username</td>"
			      "<td><input type=text name=user value=\"%s\">"
			      , su->m_user 
			      );
	}
	else {
		sb.safePrintf("<tr>"
			      "<td valign=top>Username</td>"
			      "<td>%s"
			      "<input type=hidden name=user value=\"%s\">"
			      , su->m_user 
			      , su->m_user 
			      );
	}
	if ( su->m_userError[0] )
		sb.safePrintf("<br><font size=-1 "
			      "color=red>%s</font>",su->m_userError);
	sb.safePrintf("</td></tr>");


	// password
	sb.safePrintf("<tr>"
		      "<td valign=top>Password</td>"
		      "<td><input type=password size=12 maxlength=12 "
		      "name=pwd value=\"%s\">"
		      , su->m_pwd );
	if ( su->m_pwdError[0] )
		sb.safePrintf("<br><font size=-1 "
			      "color=red>%s</font>",su->m_pwdError);
	sb.safePrintf("</td></tr>");

	// password2
	sb.safePrintf("<tr>"
		      "<td valign=top>Repeat Password</td>"
		      "<td><input type=password size=12 maxlength=12 "
		      "name=pwd2 value=\"%s\">"
		      , su->m_pwd2 );
	if ( su->m_pwd2Error[0] )
		sb.safePrintf("<br><font size=-1 "
			      "color=red>%s</font>",su->m_pwd2Error);
	sb.safePrintf("</td></tr>");


	// email
	sb.safePrintf("<tr>"
		      "<td valign=top>Email Address</td>"
		      "<td><input type=text name=email value=\"%s\">"
		      , su->m_email );
	if ( su->m_emailError[0] )
		sb.safePrintf("<br><font size=-1 "
			      "color=red>%s</font>",su->m_emailError);
	else
		sb.safePrintf("<br><font size=-1 color=gray>"
			      "Required for emailing you receipts, or a "
			      "new password should you forget it."
			      "</font>");
	sb.safePrintf("</td></tr>");

	// phone #
	sb.safePrintf("<tr>"
		      "<td valign=top>Phone Number</td>"
		      "<td><input type=text name=phone value=\"%s\">"
		      , su->m_phone );
	if ( su->m_phoneError[0] )
		sb.safePrintf("<br><font size=-1 "
			      "color=red>%s</font>",su->m_phoneError);
	sb.safePrintf("</td></tr>");



	// creidt card #
	sb.safePrintf("<tr>"
		      "<td valign=top>Credit Card Number</td>"
		      "<td><input type=text name=cc value=\"");
	// don't show the entire credit card # for security!
	if ( edit && gbstrlen(su->m_ccNum)>12  )
		sb.safePrintf("************%s\">", su->m_ccNum + 12);
	else
		sb.safePrintf("%s\">", su->m_ccNum );
	if ( su->m_ccNumError[0] )
              sb.safePrintf("<br><font size=-1 "
			    "color=red>%s</font>",su->m_ccNumError);
	sb.safePrintf("</td></tr>");


	// credit card type
	char *s1 = "";
	char *s2 = "";
	char *s3 = "";
	char *s4 = "";
	if ( !strcmp(su->m_ccType,"Visa") ) s1 = " selected";
	if ( !strcmp(su->m_ccType,"MasterCard") ) s2 = " selected";
	if ( !strcmp(su->m_ccType,"Discovery") ) s3 = " selected";
	if ( !strcmp(su->m_ccType,"AmericanExpress") ) s4 = " selected";
	sb.safePrintf("<tr>"
		      "<td valign=top>Credit Card Type</td>"
		      "<td><select name=cctype>"
		      "<option value=Visa%s>Visa</option>"
		      "<option value=MasterCard%s>MasterCard</option>"
		      "<option value=Discover%s>Discover</option>"
		      "<option value=AmericanExpress%s>American Express"
		      "</option>"
		      "</select>"
		      , s1,s2,s3,s4 );
	if ( su->m_ccTypeError[0] )
              sb.safePrintf("<br><font size=-1 "
			    "color=red>%s</font>",su->m_ccTypeError);
	sb.safePrintf("</td></tr>");

	// ccv
	sb.safePrintf("<tr>"
		      "<td valign=top>3-digit CVV</td>"
		      "<td><input type=text name=cvv size=3 "
		      "maxlength=3 value=\"%s\">&nbsp;&nbsp;"
		      "<font size=-1 color=gray>(on back "
		      "of card)</font>"
		      , su->m_cvv );
	if ( su->m_cvvError[0] )
              sb.safePrintf("<br><font size=-1 "
			    "color=red>%s</font>",su->m_cvvError);
	sb.safePrintf("</td></tr>");


	// expiration
	sb.safePrintf("<tr>"
		      "<td valign=top>"
		      "<nobr>Credit Card Expiration</nobr></td>"
		      "<td><input type=text maxlength=5 "
		      "size=5 name=exp value=\"%s\">&nbsp;&nbsp;"
		      "<font size=-1 color=gray>"
		      "(MM/YY)"
		      "</font>"
		      , su->m_exp );
	if ( su->m_expError[0] )
              sb.safePrintf("<br><font size=-1 "
			    "color=red>%s</font>",su->m_expError);
	sb.safePrintf("</td></tr>");

	// deposit amount
	// TODO: add su->m_depositError!
	if ( new1 ) {
		float deposit = su->m_deposit;
		if ( ! submit ) deposit = MINCHARGE;//100.00;
		sb.safePrintf("<tr>"
			      "<td valign=top><nobr>"
			      "Initial Deposit Amount</nobr></td>"
			      "<td>"
			      "<input type=text name=deposit value=\"%.02f\">"
			      "<br>"
			      "<font size=-1 color=gray><i>Minimum deposit "
			      "is $%.02f"
			      "</i></font>"
			      ,deposit
			      ,MINCHARGE
			      );
		if ( su->m_depositError[0] )
			sb.safePrintf("<br><font size=-1 "
				      "color=red>%s</font>",
				      su->m_depositError);
		sb.safePrintf("</td></tr>");
	}

	// search feed code
	sb.safePrintf("<tr>"
		      "<td valign=top>XML Feed Code</td>"
		      "<td><input type=text name=fc size=14 "
		      "maxlength=14 value=\"%s\">"
		      , su->m_fc );
	if ( su->m_fcError[0] )
              sb.safePrintf("<br><font size=-1 "
			    "color=red>%s</font>",su->m_fcError);
	else
		sb.safePrintf("<br><font size=-1 color=gray>"
			      "A secret code used along with your userid "
			      "to access the XML feeds."
			      "</font>");

	sb.safePrintf("</td></tr>");


	if ( new1 ) {
		char *cs = "";
		if ( su->m_terms ) cs = " checked";
		sb.safePrintf("<tr><td colspan=10>"
			      "<input type=checkbox name=terms value=1%s> "
			      "I have read and "
			      "agree to these <a href=/terms.html>terms</a>."
			      , cs);
		if ( su->m_termsError[0] )
			sb.safePrintf("<br><font size=-1 "
				      "color=red>%s</font>",su->m_termsError);
		sb.safePrintf("</td></tr>");
	}
	
	sb.safePrintf("</table>");


	// so sendPageAccount() knows when it receives a submit
	sb.safePrintf("<input type=hidden name=submitted value=1>");

	if ( new1 )
		sb.safePrintf("<input type=hidden name=new value=1>");
	if ( edit )
		sb.safePrintf("<input type=hidden name=edit value=1>");



	sb.safePrintf("<br>"
		      "<a href=/account>Cancel</a> &nbsp; "
		      "<input type=submit name=submit value=OK>");

	sb.safePrintf("</center>");

	sb.safePrintf("</form>"
		      "</body>"
		      "</html>" );

	g_httpServer.sendDynamicPage ( s ,
				       sb.getBufStart(), 
				       sb.length(),
				       0 , // cachetime in secs
				       false , // postreply?
				       "text/html", // content type
				       -1, // http status -1->200
				       NULL, // cookiePtr,
				       "utf-8" );
	return true;
}

bool Proxy::saveUserBufs() {
	log("proxy: saving user bufs");
	if ( m_sumBuf.saveToFile ( g_hostdb.m_dir , "usersum.dat" ) < 0 )
		return false;
	if ( m_userInfoBuf.saveToFile ( g_hostdb.m_dir , "userinfo.dat" ) < 0 )
		return false;
	if ( m_depositBuf.saveToFile ( g_hostdb.m_dir,"userdeposit.dat" ) < 0 )
		return false;
	return true;
}

// and make summary rec table, "m_srht"
bool Proxy::loadUserBufs ( ) {

	// these return 0 if file does not exist, which may be the case
	if ( m_sumBuf.fillFromFile ( g_hostdb.m_dir , "usersum.dat" ) < 0 )
		return false;
	if ( m_userInfoBuf.fillFromFile(g_hostdb.m_dir , "userinfo.dat")<0)
		return false;
	if ( m_depositBuf.fillFromFile(g_hostdb.m_dir,"userdeposit.dat")< 0)
		return false;
	// scan users and zero out m_pending, we might have shutdown before
	// the operation could complete so do not charge for it
	UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
	int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
	int32_t i; for ( i = 0 ; i < ni ; i++ ) {
		// int16_tcut
		UserInfo *ui = &uis[i];
		if ( ui->m_pending == 0.0 ) continue;
		log("proxy: erasing pending=%.02f for uid=%"INT32"",
		    ui->m_pending,ui->m_userId32);
		ui->m_pending = 0.0;
	}
	
	//
	// make the hashtable for summaryrecs
	//
	m_srht.reset();
	int32_t ns = m_sumBuf.length() / sizeof(SummaryRec);
	SummaryRec *ss = (SummaryRec *)m_sumBuf.getBufStart();
	int32_t needSlots = ns * 2;
	if ( ! m_srht.set(8,4,needSlots,NULL,0,false,0,"srectbl") ) {
		log("proxy: failed to alloc srht");
		return false;
	}
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// int16_tcut
		SummaryRec *sr = &ss[i];
		// get the offset
		int32_t sumOff = ((char *)sr) - m_sumBuf.getBufStart();
		// make key
		uint64_t h64 = (uint32_t)sr->m_userId32;
		uint32_t t = sr->m_year;
		t <<= 16;
		t |= sr->m_month; // 1..12
		t <<= 8;
		t |= sr->m_day; // 1..31
		t <<= 8;
		t |= sr->m_accessType;
		h64 <<= 32;
		h64 |= t;
		// add it
		if ( ! m_srht.addKey ( &h64 , &sumOff ) ) { // sr ) ) {
			log("proxy: failed to load user bufs");
			return false;
		}
	}
	// if first time, then initialize with records for our old clients
	// like existing clients
	int32_t nr = m_userInfoBuf.length() / sizeof(UserInfo);
	if ( nr >= 5 ) return true;

	// clear it all
	m_userInfoBuf.reset();

	int32_t userId = 1;

	// matt wells, admin login
	UserInfo ui;
	memset(&ui,0,sizeof(UserInfo));
	strcpy(ui.m_login,"admin");
	strcpy(ui.m_password,"admin123");
	strcpy(ui.m_xmlFeedCode,"admincode");
	strcpy(ui.m_email,"admin@admin.com");
	ui.m_flags = UIF_ADMIN;
	ui.m_accountBalance = 0.0;
	ui.m_userId32 = userId++;
	m_userInfoBuf.safeMemcpy(&ui,sizeof(UserInfo));

	// success!
	return true;
}


bool Proxy::printAccountingInfoPage ( StateUser *su , SafeBuf *errmsg ) {

	HttpRequest *hr = &su->m_hr;
	TcpSocket *socket = su->m_socket;

	// get the userinfo for logged in user
	//SafeBuf errmsg;
	// this will be NULL if userid32 is -1 (invalid, not logged in, etc.)
	UserInfo *ui = getUserInfoFromId ( su->m_userId32 );

	// print html page into this safebuf
	SafeBuf *sb = &su->m_sb;

	// ok, they are authenticated, show their account stats
	sb->safePrintf("<html>"
		       "<title>Gigablast - Account Information</title>"
		       "<body>"
		       );
	//insertLoginBarDirective ( sb );
	sb->safePrintf(

		       "<style><!--"
		       "a:link,a:visited{color:#0000ff;text-decoration:none;}"
		       "-->"
		       "</style>"
		       "<center>"
		       "<a href=/>"
		       "<img src=http://www.gigablast.com/logo-med.jpg "
		       "height=122 width=500>"
		       "</a>"
		       "</center>"
		       "<br>"
		       "<table width=100%% cellpadding=5 cellspacing=0 "
		       "border=0>"

		       "<tr bgcolor=#0340fd>"
		       "<th colspan=2>"
		       "<font color=33dcff>"
		       "Account Information</font>"
		       "</th>"
		       "</tr>"
		       "</table>"
		       "\n"
		       );


	if ( su->m_transactionSuccessful && su->m_deposit > 0.0 ) {
		sb->safePrintf("<tr><td colspan=10><b><font color=green>"
			       "%s "
			      "Successfully deposited $%.02f. "
			      "</font></b><br><br><hr><br>\n",
			       su->m_authNetMsg.getBufStart(),
			       su->m_deposit );
	}
	else if ( su->m_attemptedTransaction && su->m_refund > 0.0 ) {
		sb->safePrintf("<tr><td colspan=10><b><font color=red>"
			       "%s "
			      "Failed to refund $%.02f. "
			      "</font></b><br><br><hr><br>\n",
			       su->m_authNetMsg.getBufStart(),
			       su->m_refund );
	}
	else if ( su->m_attemptedTransaction ) {
		sb->safePrintf("<tr><td colspan=10><b><font color=red>"
			       "%s "
			       "Failed to charge $%.02f. "
			       "Please verify your credit card number, "
			       "credit card type, "
			       "3-digit CVV and expiration date."
			      "</font></b><br><br><hr><br>\n",
			       su->m_authNetMsg.getBufStart(),
			       su->m_deposit );
	}

	// likewise, put refund errors here too!
	if ( su->m_refundError[0] )
		sb->safePrintf("<tr><td colspan=10><b><font color=red>"
			       "<br><br>"
			       "%s "
			       "Failed to refund $%.02f. "
			       "</font></b><br><br><hr><br>\n",
			       su->m_refundError ,
			       su->m_refund );


	char *ccnum = ui->m_creditCardNum;
	if ( gbstrlen ( ccnum) > 12 ) ccnum = ccnum + 12;

	sb->safePrintf( 
		       "<br>"
			"<table cellpadding=6>"

			"<tr><td colspan=10>"
			//"<center>"
			"<b>User Information</b> [<a href=/account?edit=1>"
		       "edit</a>]"
			//"</center>"
			"</td></tr>"

		       "<tr>"
		       "<td>Username</td>"
		       "<td>%s</td>"
		       //"<td><a href=/account?logout=1><b>LOGOUT</b></a></td>"
		       "</tr>"

		       "<tr>"
		       "<td>Password</td>"
		       "<td>%s</td>"
		       "</tr>"

		       "<tr>"
		       "<td>Email</td>"
		       "<td>%s</td>"
		       "</tr>"

		       "<tr>"
		       "<td>Phone</td>"
		       "<td>%s</td>"
		       "</tr>"

		       "<tr>"
		       "<td>Credit Card #</td>"
		       "<td>************%s</td>"
		       "<td></td>"
		       "</tr>"


		       "<tr>"
		       "<td><nobr>User ID</nobr></td>"
		       "<td>%"INT32"</td>"
		       "<td>See the <a href=/searchfeed.html>XML Search "
		       "Feed</a> page or the <a href=/seoapi.html>SEO API</a> "
		       "page for details on using this.</td>"
		       "</tr>"

		       "<tr>"
		       "<td><nobr>XML Feed Code</nobr></td>"
		       "<td>%s</td>"
		       "<td>See the <a href=/searchfeed.html>XML Search "
		       "Feed</a> page or the <a href=/seoapi.html>SEO API</a> "
		       "page for details on using this.</td>"
		       "</tr>"

			"</td>"
		       "</tr>"

		       "</table>\n"

			, ui->m_login
			, "*******"
			, ui->m_email
			, ui->m_phone
			// only disply last 4 digits
			, ccnum
			, ui->m_userId32
			, ui->m_xmlFeedCode
		       //, ui->m_accountBalance
		       );

	if ( ui->m_userId32 == 1 && (ui->m_flags & UIF_ADMIN) ) {
		sb->safePrintf ( "<br>");
		sb->safePrintf ( "<hr>");
		sb->safePrintf ( "<br>");
		printUsers ( sb ) ;
	}


	///////////////
	//
	// DEPOSIT GUI
	//
	///////////////
	sb->safePrintf ( "<br>");
	sb->safePrintf ( "<hr>");
	sb->safePrintf ( "<br>");

	sb->safePrintf("Account Balance: &nbsp <b>$%.02f</b><br><br>"
		       , ui->m_accountBalance );

	if ( ui->m_pending != 0.0 )
		sb->safePrintf("Pending Actions: &nbsp <b>$%.02f</b><br><br>"
			       , ui->m_pending);


	sb->safePrintf(
		       "<table cellpadding=6>"
		       "<tr><td>"
		       "<form action=/account method=POST>"
		       "Make "
		       //"<select name=transtype>"
		       //"<option value=deposit selected>deposit</option>"
		       //"<option value=withdrawal>withdrawal</option>"
		       //"</select>"
		       "deposit of "
		       "<input type=text name=deposit value=\"%.02f\">"
		       ,MINCHARGE);


	// the admin can credit the account if he receives a wire or a check
	// from a user...
	/*
	if ( su->m_isMasterAdmin )
		sb->safePrintf("<br>"
			       "<font color=red>"
			       "Record Wire of "
			       "<input type=text name=wire value=\"\"> "
			       " Desc: <input type=text name=wiredesc>"
			       "<br>"
			       "Record Check of "
			       "<input type=text name=check value=\"\"> "
			       " Desc: <input type=text name=checkdesc>"
			       );
	*/


	sb->safePrintf(//"<input type=hidden name=makedeposit value=1>"
		       "<input type=submit "
		       "onclick=\""
		       "document.getElementById('gears')."
		       "style.display='';"
		       //"innerHTML="
		       //"'<img width=50 height=50 "
		       //"src=/gears.gif>';"
		       "\" "
		       "name=dotrans value=OK>"
		       );

	if ( su->m_depositError[0] )
		sb->safePrintf("<br><font size=-1 color=red>%s</font>",
			       su->m_depositError);

	sb->safePrintf("</td><td>"
		       "<div style=display:none; id=gears>"
		       "<img width=50 height=50 src=/gears.gif></div>"
		       "</form>\n"
		       "</td></tr></table>\n"
		       );
	//sb->safePrintf("<br>");


	////////////////
	//
	// DEPOSIT TABLE
	//
	// . all the deposits and withdrawls they've ever made
	//
	////////////////
	sb->safePrintf ( "<br>");
	sb->safePrintf ( "<hr>");
	sb->safePrintf ( "<br>");
	printDepositTable ( sb , ui->m_userId32 );


	int32_t cutoff;
	bool printedSomething;
	SummaryRec *srs = (SummaryRec *)m_sumBuf.getBufStart();
	int32_t nsr = m_sumBuf.length() / sizeof(SummaryRec);


	///////////////
	//
	// DAILY BREAKDOWN
	//
	// print daily query stats for selected month
	//
	//////////////
	sb->safePrintf ( "<br>");
	sb->safePrintf ( "<hr>");
	sb->safePrintf ( "<br>");
	int32_t now = getTimeLocal();
	struct tm *timeStruct = gmtime ( (time_t *)&now );
	int32_t currentMonth = timeStruct->tm_mon+1; // 1 to 12
	int32_t currentYear  = timeStruct->tm_year+1900;
	int32_t currentDay   = timeStruct->tm_mday;
	static char *s_mnames[12] = {
		"January",
		"February",
		"March",
		"April",
		"May",
		"June",
		"July",
		"August",
		"September",
		"October",
		"November",
		"December"
	};
	int32_t month = hr->getLong("month",currentMonth);
	int32_t year  = hr->getLong("year",currentYear);
	int32_t day   = hr->getLong("day",currentDay);
	if ( month < 1 ) month = 1;
	if ( month > 12 ) month = 12;
	if ( day < 1 ) day = 1;
	if ( day > 31 ) day = 31;
	char *monthName = s_mnames[month-1];
	sb->safePrintf( "<table width=600px cellpadding=6 border=0>"
			"<tr>"
		       "<td colspan=2>"
			"<b>"
			//"<center>"
		       "Daily Breakdown for %s %"INT32""
			//"</center>"
			"</b>"
		       "</td>"
		       "</tr>"
		       // the first column in this pane splitter table
		       , monthName
		       , year
		       );
	cutoff = sb->length();
	// transaction table in left pane
	sb->safePrintf ( "<tr>"
			"<td>DAY</td>"
			"<td>Total Cost</td>"
			"<td>Total Accesses</td>"
			"<td>Description</td>"
			"</tr>"
			);
	// scan daily transactions for that month and this user
	//SummaryRec *srs = (SummaryRec *)m_sumBuf.getBufStart();
	//int32_t nsr = m_sumBuf.length() / sizeof(SummaryRec);
	printedSomething = false;
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		SummaryRec *sr = &srs[i];
		if ( sr->m_userId32 != ui->m_userId32 ) continue;
		//if ( sr->m_day   != day   ) continue;
		if ( sr->m_month != month ) continue;
		if ( sr->m_year  != year  ) continue;
		char *as = getAccessTypeString(sr->m_accessType);
		// they should be in order by date...
		printedSomething = true;
		sb->safePrintf("<tr>"
			       //"<td>%02"INT32"/%02"INT32"</td>"
			       "<td>%02"INT32"</td>"
			       "<td>$%.02f</td>"
			       "<td>%"INT32"</td>"
			       "<td>%s</td>" // "new search feed", etc.
			       "</tr>\n"
			       //, (int32_t)sr->m_month
			       , (int32_t)sr->m_day
			       , sr->m_totalCost
			       , sr->m_numAccesses
			       , as
			       );
	}
	if ( ! printedSomething ) {
		sb->setLength ( cutoff );
		sb->safePrintf("<tr><td colspan=3><font size=-1>"
			       "<i>There is no data for "
			       "this because you have not accessed the feed "
			       "yet using your XML Feed Code</i>"
			       "</font></td></tr>");
	}
	sb->safePrintf("</table>\n");



	/////////
	//
	// print feed access MONTHLY SUMMARY table
	//
	////////
	sb->safePrintf ( "<br>");
	sb->safePrintf ( "<hr>");
	sb->safePrintf ( "<br>");
	sb->safePrintf ( "<table width=800px cellpadding=6>"

			 "<tr><td colspan=10>"
			 //"<center>"
			 "<b>Monthly Summary</b>"
			 //"</center>"
			 "</td></tr>"
			 );
	cutoff = sb->length();
	sb->safePrintf( "<tr>"
			"<td>Date</td>"
			"<td>Description</td>"
			"<td># Accesses</td>"
			"<td>Total Cost</td>"
			"<td>Cost Per Access</td>"
			"<td>Avg. Process Time</td>"
			"</tr>"
			);
	// . add up SummaryRec stats for each month and transaction type
	//   for this user
	// . scan daily transactions for that month and this user
	// . go out 20 years for each access type
	int32_t numCursors = (MAX_ACCESS_TYPE+1) * 20 * 12;
	int32_t need = numCursors * sizeof(SummaryRec);
	char tmp[need];
	SafeBuf ss(tmp,need);
	// reset to all zeros
	ss.zeroOut();
	// pointers
	SummaryRec *cursors = (SummaryRec *)ss.getBufStart();
	// . maybe just make a hash key of each summary rec based on
	//   it's month and year and access type
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		SummaryRec *sr = &srs[i];
		if ( sr->m_userId32 != ui->m_userId32 ) continue;
		// we go out 20 years
		int32_t index = (int32_t)sr->m_accessType * (12*20);
		index += (sr->m_year - 2013) * 12;
		// we use 1..12 for month range, so subtract 1
		index += sr->m_month - 1;
		// now get it
		SummaryRec *cursor = &cursors[index];
		// just in case it is our first time hitting "cursor"
		cursor->m_accessType   = sr->m_accessType;
		cursor->m_month        = sr->m_month;
		cursor->m_year         = sr->m_year;
		cursor->m_day          = 0; // we are accumulating all days!
		// accumulate access stats for that day
		cursor->m_numAccesses      += sr->m_numAccesses;
		cursor->m_totalCost        += sr->m_totalCost;
		cursor->m_totalProcessTime += sr->m_totalProcessTime;
	}
	printedSomething = false;
	// now print out the cursors for each month in reverse!
	for ( int32_t i = numCursors - 1 ; i >= 0 ; i-- ) {
		// int16_tcut
		SummaryRec *cursor = &cursors[i];
		// 0 is not a valid access type
		if ( cursor->m_accessType == 0 ) continue;
		// print it otherwise
		char *as = getAccessTypeString(cursor->m_accessType);
		printedSomething = true;
		sb->safePrintf("<tr>"
			       "<td>%02"INT32"/%04"INT32"</td>"
			       "<td><nobr>%s</nobr></td>"
			       "<td>%"INT32"</td>"
			       "<td>$%.02f</td>"
			       "<td>$%.05f</td>"// price per access
			       "<td>%"INT32"ms</td>"
			       "</td>"
			       , (int32_t)cursor->m_month
			       , (int32_t)cursor->m_year
			       , as
			       , cursor->m_numAccesses
			       , cursor->m_totalCost
			       , cursor->m_totalCost / 
			       (float)cursor->m_numAccesses
			       , (int32_t)(cursor->m_totalProcessTime /
					(float)cursor->m_numAccesses)
			       );
	}
	if ( ! printedSomething ) {
		sb->setLength ( cutoff );
		sb->safePrintf("<tr><td colspan=3><font size=-1>"
			       "<i>There is no data for "
			       "this because you have not accessed the feed "
			       "yet using your XML Feed Code</i>"
			       "</font></td></tr>");
	}

	// end monthly summary table
	sb->safePrintf("</table>");




	// needs this for cookie setting
	su->m_socket    = socket;


	////////////
	//
	// pretty graph in right pane of query latency and shit i guess
	//
	////////////
	//
	// convert to timestamps in seconds
	struct tm timeStruct2 ;
	timeStruct2.tm_mday = day; // 1 to 31
	timeStruct2.tm_year = year - 1900;
	timeStruct2.tm_mon  = month - 1; // 0 to 11
	timeStruct2.tm_hour = 0;
	timeStruct2.tm_sec  = 0;
	time_t t1 = mktime ( &timeStruct2 ); // utc i guess
	int32_t nextYear = year;
	int32_t nextMonth = month;
	if ( nextMonth >= 13 ) {
		nextMonth = 1;
		nextYear++;
	}
	timeStruct2.tm_mon  = nextMonth - 1; // 0 to 11
	timeStruct2.tm_year = nextYear  - 1900;
	time_t t2 = mktime ( &timeStruct2 ); // utc i guess

	if ( ! g_statsdb.makeGIF ( t2 , // end
				   t1 , // start
				   10 , // samples in moving avg...
				   &su->m_sb2,
				   su,
				   gotGifWrapper  ,
				   su->m_userId32 ) )
		return false;

	return gotGif ( su );
}



bool Proxy::gotGif ( StateUser *su ) {

	SafeBuf *sb = &su->m_sb;
	HttpRequest *hr = &su->m_hr;

	// cat the gif on
	sb->cat ( su->m_sb2 );

	sb->safePrintf("<br><br>"
		       "<center>"
		       "Copyright &copy; 2013. All rights reserved."
		       "</center>"
		       "</body>"
		       "</html>"
		       );

	// sanity!
	if ( su->m_sessionId64 < 0 ) { char *xx=NULL;*xx=0; }

	// make a cookie in case they just logged in through this page!
	// otherwise the login doesn't "stick"
	SafeBuf cb;
	cb.safePrintf("Set-Cookie: sessionid=%"INT64";\r\n"
		      "Set-Cookie: userid=%"INT32";\r\n"
		      ,su->m_sessionId64
		      ,(int32_t)su->m_userId32);

	// if admin logs in as another user by clicking on their login name
	// in the users table, we still need to know that it is the admin,
	// so use this special cookie
	UserInfo *ui = getUserInfoFromId ( su->m_userId32 );
	if ( ui && (ui->m_flags & UIF_ADMIN) )
		cb.safePrintf("Set-Cookie: adminsessid=%"INT64";\r\n"
			      ,su->m_sessionId64);

	char *cookiePtr = NULL;
	if ( cb.length() ) cookiePtr = cb.getBufStart();
	
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	g_httpServer.sendDynamicPage ( su->m_socket, 
				       sb->getBufStart(), 
				       sb->length(),
				       0 , // cachetime in secs
				       false , // postreply?
				       "text/html", // content type
				       -1, // http status -1->200
				       cookiePtr,
				       "utf-8" ,
				       hr );

	// i guess it copies the safebuf contents..
	mdelete(su,sizeof(StateUser),"usprox");
	delete(su);
	return true;
}


bool Proxy::printDepositTable ( SafeBuf *sb , int32_t userId32 ) {
	int32_t nd = m_depositBuf.length() / sizeof(DepositRec);
	DepositRec *drs = (DepositRec *)m_depositBuf.getBufStart();

	UserInfo *ui = getUserInfoFromId ( userId32 );	

	sb->safePrintf("<table cellpadding=6>"
		       "<tr><td colspan=3>"
		       "<b>Transaction Table</b>"
		       "</td></tr>"
		       
		       "<tr>"
		       "<td>Time</td>"
		       //"<td>TransId</td>"
		       "<td>Amount</td>"
		       "<td>Description</td>"
		       "<td>Transaction ID</td>"
		       
		       "</tr>"
		       );
	for ( int32_t i = nd - 1 ; i >= 0 ; i-- ) {
		DepositRec *dr = &drs[i];
		if ( dr->m_userId32 != userId32 ) continue;
		// convert timestamp to month/day/year in utc
		time_t tt = dr->m_depositDate;//getTimeLocal();
		struct tm *timeStruct = gmtime ( &tt );
		sb->safePrintf("<tr><td>"
			       "%"INT32"/%"INT32"/%"INT32" %"INT32":%02"INT32""
			       "</td>"
			       , (int32_t)(timeStruct->tm_mon + 1) // month 0..11
			       , (int32_t)timeStruct->tm_mday //day of month 1..31
			       , (int32_t)(timeStruct->tm_year+1900) // year
			       , (int32_t)(timeStruct->tm_hour)
			       , (int32_t)(timeStruct->tm_min)
			       );
		//sb->safePrintf("<td>%"INT32"</td>",dr->m_transactionId);
		char *bs1 = "<font color=green>";
		char *bs2 = "</font>";
		// is it a withdrawl?
		if ( dr->m_depositAmount < 0.0 ) {
			bs1 = "<font color=red>";
			bs2 = "</font>";
		}
		char *desc = "unknown";
		if ( dr->m_flags == DRF_DEPOSIT  ) desc = "deposit";
		if ( dr->m_flags == DRF_WITHDRAW ) desc = "withdraw";
		if ( dr->m_flags == DRF_WITHDRAW_FEE ) desc = "withdraw fee";
		sb->safePrintf("<td>%s%.02f%s</td>"
			       "<td>%s</td>"
			       "<td>%"INT64"</td>"
			       , bs1 
			       , dr->m_depositAmount 
			       , bs2 
			       , desc
			       , dr->m_authorizeNetTransactionId
			       );
		float refundAmt = dr->m_depositAmount;
		if ( refundAmt > ui->m_accountBalance )
			// subtract a penny because account balance
			// might be off by a fraction of a penny
			// and it will say that you are trying to
			// refund more than the account balance
			refundAmt = ui->m_accountBalance - .01 ;
		// there is a 5% refund charge
		//refundAmt *= 0.95;
		sb->safePrintf("<td><a href=/account?refund=%.02f&"
			       "transid=%"INT64">refund"
			       "</a></td>"
			       , refundAmt 
			       , dr->m_authorizeNetTransactionId
			       );
		sb->safePrintf("</tr>\n");
	}
	sb->safePrintf("</table>\n");
	return true;
}


#define BARSIZE 250

/*
// put a 100 byte directive into th src
bool Proxy::insertLoginBarDirective ( SafeBuf *sb ) {
	if ( ! sb->reserve(BARSIZE) ) return false;
	int32_t len = sb->length();
	//sb.safePrintf("<div style=display:none>%%login%%</div>");
	char *dir = "%%login%%";
	int32_t dlen = gbstrlen(dir);
	sb->safeMemcpy(dir,dlen);
	int32_t inserted = sb->length() - len;
	// make directive exactly 180 bytes...
	int32_t remain = BARSIZE - inserted;
	memset (sb->getBuf() , ' ' , remain );
	sb->incrementLength ( remain );
	return true;
}
*/

char *Proxy::storeLoginBar ( char *reply , 
			     int32_t replySize , 
			     int32_t replyAllocSize,
			     int32_t mimeLen,
			     //int32_t userId32 ,
			     int32_t *newReplySize ,
			     HttpRequest *hr ) {

	// assume new reply identical to old reply
	char *newReply = reply;
	*newReplySize = replySize;

	// did mime have error in reply?
	// if so, do not insert login bar
	if ( strcmp(reply,"HTTP/1.0 ") == 0 ) {
		int32_t httpStatus = atol2(reply+9,3);
		if ( httpStatus != 200 ) return reply;
	}

	// userid is in cookie
	int32_t userId32 = hr->getLongFromCookie("userid",0);
	UserInfo *ui = getUserInfoFromId ( userId32 );
	int64_t sessionId64 = hr->getLongLongFromCookie("sessionid",0);
	if ( ui && ui->m_lastSessionId64 != sessionId64 ) ui = NULL;


	SafeBuf cu;
	char *currentUrl = "";
	if ( hr ) {
		hr->getCurrentUrl ( cu );
		currentUrl = cu.getBufStart();
	}


	//return reply;

	// if too small, just return the original reply
	char *content = reply + mimeLen;
	char *contentEnd = reply + replySize;
	int32_t contentLen = contentEnd - content;
	if ( contentLen < 20 ) return newReply;

	// find <body tag in the reply
	char *p = content;
	int32_t maxLen = 3000;
	if ( contentLen < maxLen ) maxLen = contentLen;
	char *pend = content + maxLen;
	// not necessarily \0 terminated, so do not over-scan
	pend -= 10;
	for ( ; p < pend ; p++ ) {
		if ( p[0] != '<' ) continue;
		if ( p[1] != 'b' ) continue;
		if ( p[2] != 'o' ) continue;
		if ( p[3] != 'd' ) continue;
		if ( p[4] != 'y' ) continue;
		// get it!
		break;
	}
	// return if did not find directive
	if ( p >= pend ) return newReply;

	// scan to end of body tag
	for ( ; p < pend && *p != '>' ; p++ );

	// no end of body tag?
	if ( p >= pend ) return newReply;

	// skip that
	p++;

	// make the insertion
	SafeBuf ib;

	// if not logged in... print login link
	if ( ! ui ) {
		ib.safePrintf ( "<table width=100%%><tr><td align=right>"
				"[<a style=decoration:none; href=/account?"
				"follow="
				);
		ib.urlEncode ( currentUrl );
		ib.safePrintf( ">"
			       "login</a>]</td></tr></table>");
	}

	else {
		// if logged in print user name (38 bytes)
		ib.safePrintf("<table width=100%%><tr><td align=right>"
			      "logged in as <b>"
			      "<a href=/account>");
		ib.safeStrcpy ( ui->m_login);
		ib.safePrintf("</a></b> [<a style=decoration:none; "
			      "href=/account?logout=1&follow=");
		ib.urlEncode ( currentUrl );
		ib.safePrintf( ">logout</a>]</td></tr></table>");
	}

	// how much do we need?
	int32_t need = replySize + ib.length();
	// a \0 terminating i guess
	//need++;

	// make a new one
	SafeBuf sb;
	if ( ! sb.reserve ( need ) ) {
		log("proxy: failed to store login bar");
		return newReply;
	}

	// copy up to end of body tag
	sb.safeMemcpy ( reply ,  p - reply );
	// then the insertion
	sb.safeMemcpy ( &ib );
	// then the rest
	sb.safeMemcpy ( p , contentEnd - p );
	// then the \0 i guess
	//sb.addSilentNull();
	//sb.reserve ( 1 );

	// all done. +1 for the \0
	mfree ( reply , replyAllocSize , "prrepl");

	// extricate
	newReply = sb.getBufStart();
	*newReplySize = sb.length();
	sb.detachBuf();

	// fix overreading!
	// fix the fucking content-length in the mime...
	char *mp = strnstr(newReply,"Content-Length:",*newReplySize);
	if ( ! mp ) mp = strnstr(newReply,"Content-length:",*newReplySize);
	if ( ! mp ) {
		log("proxy: fuck, no content-length: in mime");
		int32_t len = *newReplySize;
		if ( len > 300 ) len = 300;
		len--;
		if ( len <= 0 ) {
			log("proxy: fuck, 0 length reply");
			return newReply;
		}
		char c = newReply[len];
		newReply[len] = '\0';
		log("proxy: fuck: reply=%s",newReply);
		newReply[len] = c;
		return newReply;
	}

	// temp fix take it out because it is not working right
	mp[0] = 'x';
	return newReply;

	// point to first digit in there
	mp += 16;
	// store our new content length as ascii into test buf
	char test[64];

	int32_t len =sprintf(test,"%"INT32"",(int32_t)(*newReplySize-mimeLen));
	// find end
	char *end = mp;
	while ( *end && is_digit(*end) ) end++;
	// copy backwards then
	char *src = test + len - 1;
	for ( ; src >= test ; src-- , end-- )
		*end = *src;
	// that should be it!!
	return newReply;
}

void Proxy::printUsers ( SafeBuf *sb ) {

	sb->safePrintf( "<table width=600px cellpadding=6 border=0>"
			"<tr>"
			"<td colspan=2>"
			"<b>"
			//"<center>"
			"User Table"
			//"</center>"
			"</b>"
			"</td>"
			"</tr>"
			);

	UserInfo *uis = (UserInfo *)m_userInfoBuf.getBufStart();
	int32_t ni = m_userInfoBuf.length() / sizeof(UserInfo) ;
	int32_t i; for ( i = 0 ; i < ni ; i++ ) {
		// int16_tcut
		UserInfo *ui = &uis[i];
		// begin new row?
		if ( i % 5 == 0 ) {
			if ( i > 0 ) sb->safePrintf("</tr>");
			sb->safePrintf("<tr>");
		}
		// if admin clicks this link then he logs in as that user,
		// but if admin we should still have set our cookie
		// adminsessid to our current session id so we know we are
		// also the admin!
		sb->safePrintf("<td><nobr>%"INT32". "
			       "<a href=/account?login=%s&password=%s>"
			       "%s</a></nobr></td>"
			       ,i
			       ,ui->m_login
			       ,ui->m_password 
			       ,ui->m_login
			       //,ui->m_userId32
			       );
	}
	sb->safePrintf("</tr>\n");
	sb->safePrintf("</table>\n");
}
