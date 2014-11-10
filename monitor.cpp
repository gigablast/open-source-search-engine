// Matt Wells, copyright Feb 2003

#include "gb-include.h"

#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "PingServer.h"
#include "HttpServer.h"
#include "Dns.h"
#include <sys/time.h>
#include <sys/resource.h>

// Loop.h defines these to core, so fix that!
#undef  sleep(a)
#undef usleep(a)

extern int h_errno;

bool mainShutdown ( bool urgent ) { return true; }
bool closeAll ( void *state , void (* callback)(void *state) ) { return true; }
bool allExit ( ) { return true; }

void checkPage ( char *host , uint16_t port , char *path ) ;
bool getPage ( char *host , uint16_t port , char *path ) ;
int connectSock ( char *host , uint16_t port ) ;
bool sendEmail ( char *errmsg ) ;
//bool sendEmailSSL ( char *errmsg ) ;
void sleepWrapper ( int fd , void *state ) ;



// a static buf
static char s_errbuf [ 50024 ];

// sample every X seconds (was 120)
#define WAIT 30
// for debugging
//#define WAIT 2

static int32_t s_wait = WAIT;

// count # of consecutive errors
static int32_t s_count = 0;

// last time we sent an email
static time_t s_lastTime = 0;

//static bool s_buzz = false;

char *g_host = NULL;
int32_t  g_port = 80;
bool g_montest = false;
bool g_isFlurbit = false;
bool g_isProCog = false;
char *g_fqhn = "gigablast.com";

int main ( int argc , char *argv[] ) {
	// debug test
	//sendEmail("hey you!");
	//return 0;
	//if(argc > 1 && strcmp(argv[1], "buzz") == 0) {
	//	s_buzz = true;
	//}
	bool badArgs = false;

	for ( int32_t i = 2 ; i < argc ; i++ ) {

		if ( argv[i][0]=='-' &&
		     argv[i][1]=='t' ) {
			//argc--;
			g_montest = true;
			s_wait = 1;
			continue;
		}

		if ( i+1 < argc &&
		     argv[i][0]=='-' &&
		     argv[i][1]=='h' ) {
			g_fqhn = argv[i+1];
			i++;
			continue;
		}

		badArgs = true;
	}


	if ( argc < 2 || badArgs ) {
		fprintf(stderr,"Usage: monitor www.xyz.com:80 [-h FQHN] [-t]\n");
		fprintf(stderr,"FQHN defaults to gigablast.com, but if you are"
			" not monitoring on gigablast's network then you "
			"need to set this to like monitor2.gigablast.com "
			"or whatever your hostname is so verizon accepts our "
			"email.\n");
		exit(-1);
	}

	g_host = argv[1];
	// scan for port
	char *portStr = strstr(g_host,":");
	g_port = 80;
	if ( portStr ) {
		g_port = atoi(portStr+1);
		*portStr = 0;
	}

	g_isFlurbit = (bool)strstr(g_host,"flurbit.com");
	if ( ! g_isFlurbit ) 
		g_isFlurbit = (bool)strstr(g_host,"69.64.70.68");
	if ( ! g_isFlurbit ) 
		g_isFlurbit = (bool)strstr(g_host,"eventguru.com");
	if ( ! g_isFlurbit ) 
		g_isFlurbit = (bool)strstr(g_host,"eventwidget.com");


	// let's ensure our core file can dump
	struct rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE,&lim) )
		log("monitor: setrlimit: %s.", mstrerror(errno) );

	g_isProCog = (bool)strstr(g_host,"procog.com");


	g_conf.m_sendEmailAlerts              = true;
	g_conf.m_sendEmailAlertsToSysadmin    = true;
	//g_conf.m_sendEmailAlertsToMattAlltell = false;
	//g_conf.m_sendEmailAlertsToJavier      = false;
	//g_conf.m_sendEmailAlertsToPartap      = false;
	//g_conf.m_sendEmailAlertsToZak         = false;
	g_conf.m_delayNonCriticalEmailAlerts  = false;

	// avoid any local dns, just go directly to the roots
	//g_conf.setRootIps();
	// no, try asking local bind9 !! seems safer... the root dns
	// seem to go down forever after a while...
	g_conf.m_askRootNameservers           = false;//true;

	// hack # of dns servers
	g_conf.m_numDns         = 2;//3;
	g_conf.m_dnsPorts[0]    = 53;
	g_conf.m_dnsPorts[1]    = 53;
	g_conf.m_dnsPorts[2]    = 53;
	// local bind9 server
	//g_conf.m_dnsIps  [2]    = atoip ( "127.0.0.1",9);
	// google open dns server
	g_conf.m_dnsIps  [1]    = atoip ( "8.8.8.8",7);
	//g_conf.m_dnsIps  [0]    = atoip ( "8.8.4.4",7);
	g_conf.m_dnsIps  [0]    = atoip ( "127.0.0.1",9);
	g_conf.m_dnsMaxCacheMem = 1024*10;

	g_conf.m_logDebugDns = false;//true;

	// matt wells
	// call alltel mail server directly to send to matt in case
	// mail.gigablast.com is down
	// hey, it already goes directly in m_sendEmailAlertsToMattAlltell
	// so set this to false.
	g_conf.m_sendEmailAlertsToEmail1 = true;
	// verizon bought us out... smtp-sl.vtext.com
	// was messages.alltel.com
	strcpy ( g_conf.m_email1Addr , "5054503518@vtext.com");
	strcpy ( g_conf.m_email1From , "monitor@gigablast.com");
	// use our email server
	// nah, our mailserver is probably down...
	// result of "dig MX message.alltel.com", make this canonical
	// in case they change their IP one of these days!
	//strcpy ( g_conf.m_email1MX   , "sms-mta.alltel.net" );
	//strcpy ( g_conf.m_email1MX   , "smtp-sl.vtext.com");
	// . result of "dig MX vtext.com" = smtp-bb.vtext.com
	// . they changed this on 4/19/2013!!
	// . prepend gbmxrec- to hostname to do an mxrec lookup
	// . fix it :
	//   1366500952283 000 DEBUG dns     Got CNAME alias smtp-bb.vtext.com for gbmxrec-vtext.com
	//   1366500952283 000 DEBUG dns     Got CNAME alias smtp-sl.vtext.com for smtp-bb.vtext.com

	// got ip 69.78.67.53 for 'smtp-sl.vtext.com'
	//strcpy ( g_conf.m_email1MX   , "gbmxrec-vtext.com");
	strcpy ( g_conf.m_email1MX   , "smtp-bb.vtext.com");

	// send to zak directly
	g_conf.m_sendEmailAlertsToEmail2 = false;//true;
	//strcpy ( g_conf.m_email2Addr , "5052204556@message.alltel.com" );
	//strcpy ( g_conf.m_email2Addr , "5052204556@vtext.com" );
	//strcpy ( g_conf.m_email2From , "monitor@gigablast.com");
	// got this ip from the cmd:
	// dig mx message.alltel.com 
	//strcpy ( g_conf.m_email2MX   , "166.102.165.62" );
	//strcpy ( g_conf.m_email2MX   , "sms-mta.alltel.net" );
	//strcpy ( g_conf.m_email2MX   , "smtp-sl.vtext.com");

	// to sabino right to his email server
	//g_conf.m_sendEmailAlertsToEmail3 = true;
	//strcpy ( g_conf.m_email3Addr , "7082076550@mobile.mycingular.com");
	//strcpy ( g_conf.m_email3From , "sysadmin@comcastbusiness.net" );
	// "mx.mycingular.net"
	//strcpy ( g_conf.m_email3MX   , "66.102.164.222");

	//partap
	g_conf.m_sendEmailAlertsToEmail3 = false;//true;
	//strcpy ( g_conf.m_email3Addr , "5056889554@txt.att.net");
	strcpy ( g_conf.m_email3Addr , "sysadmin@gigablast.com" );
	strcpy ( g_conf.m_email3From , "sysadmin@gigablast.com" );
	//strcpy ( g_conf.m_email3MX   , "66.231.188.229");
	strcpy ( g_conf.m_email3MX   , "10.6.54.47");

	
	g_conf.m_httpMaxSockets               = 100;

	g_loop.init();

	hashinit();

	// use -1 or 0 for port to avoid setting up the server, just client
	g_httpServer.init ( -1 , -1 );

	g_loop.registerSleepCallback ( 10 , // WAIT * 1000 ,
				       NULL ,
				       sleepWrapper );

	if ( ! g_dns.init( 9532 ) ) return 1;

	if ( ! g_loop.runLoop() ) return 1;
}

// have a list of queries we cycle through
int32_t g_qn = 0;

char *g_queries[] = {
	//"buzzlogic",
	"broncos",
	"ibm",
	"yahoo",
	//"buzzlogic",
	"google",
	"amazon",
	//"ebay",
	NULL
};

#define TYPE_GB   0
#define TYPE_BUZZ 1

char g_type[] = {
	//1, // buzzlogic
	//0, // test
	0 , // broncos
	0, // ibm
	0, // yahoo
	//1, // buzzlogic
	0,
	0,
	//0,
	0
};

// . need to contain these substrings in the result page
// . added quote because they need to be in an href="xxx" tag
#define MAX_SUBSTRINGS 7
char *g_substrings[][MAX_SUBSTRINGS] = {
	// just for buzz
	//{"buzzlogic.com/",NULL},

	// broncos
	{"denverpost.com/broncos",
	 "denverbroncos.com"},

	// test
	//{"testpreview.com","test.com",
	// "toefl.org","tmworld.com","collegeboard.com",
	// "en.wikipedia.org" , NULL } ,

	// ibm
	{"http://www.ibm.com/",
	 "http://en.wikipedia.org/",
	 "http://research.ibm.com/", NULL},
	// yahoo
	{"http://www.yahoo.com/",NULL},
	// just for buzz
	//{"buzzlogic.com/",NULL},
	// google
	{"http://www.google.com/",NULL},
	// amazon
	{"http://www.amazon.com/",NULL},
	// ebay
	//{"href=http://www.ebay.com/>",NULL},
	// ... add more here...
};

static int32_t s_goodMXIp = 0;


void gotMXIpWrapper ( void *state , int32_t ip ) {
	g_conf.m_logDebugDns = false;
	log("monitor: gotmxipwrapper ip of %s for %s",
		iptoa(ip),g_conf.m_email1MX);
	if ( ip != 0 && ip != -1 ) {
		// make sure ping server just uses the last legit ip
		// if it's lookup fails
		//g_conf.m_email1MXIPBackup = ip;
		s_goodMXIp = ip;
		log("monitor: saving good mxip %s",iptoa(ip));
	}
}


// try to load the page every tick
void sleepWrapper ( int fd , void *state ) {

	// register ourselves
	g_loop.unregisterSleepCallback ( state , sleepWrapper );
	// re-register
	g_loop.registerSleepCallback ( s_wait * 1000 ,
				       NULL ,
				       sleepWrapper );

	//fprintf(stderr,"ok\n");
	// the main monitoring loop
	// loop:
	// check port 80
	// make the request
	char query[1024];
	//sprintf(query,"/index.php?q=%s&usecache=0&"
	//	"code=gbmonitor",g_queries[g_qn]);
	sprintf(query,"/search?q=%s&usecache=0&"
		"code=gbmonitor",g_queries[g_qn]);

	//char *host = "www1.gigablast.com";
	// we need to use port 8002 if running from titan, but on voyager2
	// we can still use port 80
	int16_t port = 80;

	// hack for testing on titan
	//port = 8002;

	// for debugging
	//port = 5699;
	
	if( g_type[g_qn]== TYPE_BUZZ) {
		//host = "66.231.188.239";
		port = 8000;
		sprintf(query,"/search?q=%s&usecache=0"
			"&pwd=natoma",g_queries[g_qn]);
	}

	if ( g_isFlurbit )
		sprintf(query,"/NM/Albuquerque" );

	if ( g_isProCog )
		sprintf(query,"/?q=%s&nocache=1&"
			"code=gbmonitor",g_queries[g_qn]);


	// launch dns lookup every 30 minutes
	int32_t now = getTimeLocal();
	static int32_t s_lastDnsTime = 0;
	static int32_t s_mxip1;
	//static DnsState s_ds;
	if ( now - s_lastDnsTime > 30*60 ) {
		s_lastDnsTime = now;
		// note it
		log("dns: calling g_dns.getIp()");
		g_conf.m_logDebugDns = true;
		// . smtp-sl.vtext.com
		// . this will store it in the cache to keep it on hand
		if ( ! g_dns.getIp ( g_conf.m_email1MX  ,
				     strlen(g_conf.m_email1MX),
				     &s_mxip1 ,
				     NULL,
				     gotMXIpWrapper ,
				     NULL,//&s_ds,
				     80, // 30 second timeout
				     false, // dnslookup?
				     // if we lose our internet connection 
				     // temporarily we do not want to cache a 
				     // bad ip address for the cellphone's 
				     // mx server!!!
				     false // CACHE NOT FOUNDS? NO!!!!!!!!
				     ) )
			// return if it blocked
			return;
		// we got it without blocking
		log("dns: got ip without blocking");
		gotMXIpWrapper(NULL,s_mxip1);
	}

	// save this
	int32_t old = g_qn;

	int64_t startTime = gettimeofdayInMilliseconds();

	bool status;

	if ( g_montest ) {
		status = false;
		strcpy(s_errbuf,"monitor: test error");
	}
	// need raw=9 so robot checking doesn't cut us off
	else 
		status = getPage( g_host,
			       //"www.gidfsgablast.com",
			       g_port,
			       //"/search?q=test&usecache=0&raw=9&"
			       // php front-end now redirects raw=x
			       // to feeds.gigablast.com
			       //"/index.php?q=test&usecache=0&"
			       //"code=gbmonitor");
			       query );

	// check all hosts
	//for ( uint16_t port = 8000 ; port < 8032 ; port++ ) 
	//isUp("www.gigablast.com",port,"/cgi/0.cgi?q=test&usecache=0");

	time_t t = time(NULL);
	char *s = ctime(&t);
	s[strlen(s)-1] = '\0';
	char buf [ 30024 ];

	int64_t took = gettimeofdayInMilliseconds() - startTime;

	// if ok, loop back
	if ( status ) { 
		if ( ! g_isFlurbit )
			fprintf(stderr,"monitor: %s got page ok in %"INT64" ms "
				"(%s)\n",
				s,took,g_queries[old]);
		else
			fprintf(stderr,"monitor: %s got page ok in %"INT64" ms "
				"(flurbit.com/Albuquerque/NM)\n",
				s,took);
		s_count = 0; 
		//sleep ( WAIT ); 
		//goto loop; 
		return;
	}

	if ( strlen(s_errbuf) > 20000 ) s_errbuf[20000] = '\0';
	// make a pretty error msg
	sprintf ( buf , "monitor %s:%"INT32": %s %s\n" , g_host,g_port,s,s_errbuf );
	// log to console
	//fprintf ( stderr , buf );
	// there might %'s in the s_errbuf so do this!!
	fprintf ( stderr , "monitor %s:%"INT32": %s %s\n" , g_host,g_port,s,s_errbuf );

	// count the error
	s_count++;

	// have we already sent an email in the last 10 mins?
	if ( t - s_lastTime < 10*60 ) { s_count = 0; return; }
	//sleep ( WAIT ); goto loop; }

	// we need 3 errors in a row to send an email
	if ( s_count < 3 ) return; // { sleep ( WAIT ); goto loop; }

	// log to cell phone
	//Host h;
	//h.m_emailCode = 0;
	//h.m_hostId = 1000;
	log("sendEmail: sending email alert. mxip=%s",iptoa(s_goodMXIp));
	//if ( ! g_pingServer.sendEmail ( NULL , buf ) ) // &h , buf ) )
	g_pingServer.sendEmail ( NULL , 
				 buf , 
				 true ,  // sendtoAdmin? default
				 false,  // oom? default
				 false,  // kernelerrors? default
				 false,  // parmchanged? default
				 false,  // forceit? default
				 s_goodMXIp); // mx ip address
	//g_fqhn ) ;

	//fprintf ( stderr , "sendEmail: %s\n" , s_errbuf);

	// so we don't send more than 1 email every 10 minutes
	s_lastTime = t;

	// loop back
	s_count = 0;
	//sleep ( WAIT );
	//goto loop;
	if ( g_montest ) {
		g_montest = false;
		s_wait = WAIT;
	}
}

// . returns false and fills in s_errbuf on error
// . returns true if no error
bool getPage ( char *host , uint16_t port , char *path ) {
	// get the socket fd
	int sd = connectSock ( host , port );
	if ( sd < 0 ) return false;
	SafeBuf sbuf;
	// a tmp buf
	char tbuf [ 1024*100+10 ];
	tbuf[0] = '\0';
	// send the request
	sprintf ( tbuf , 
		  "GET %s HTTP/1.0\r\n"
		  "Connection: close\r\n"
		  "Host: www.gigablast.com\r\n\r\n" , path );
	if ( send ( sd , tbuf , strlen(tbuf) , 0 ) == -1 ) {
		sprintf ( s_errbuf ,"send: %s" , strerror(errno) );
		close ( sd );
		return false;
	}
	// read the reply
	int32_t nb;
	//int32_t sum = 0;

	// make it non blocking in case we don't get reply
	int flags = fcntl ( sd , F_GETFL ) ;
        if ( flags < 0 ) {
		sprintf ( s_errbuf,"fcntl: (F_GETFL): %s.",strerror(errno));
		close ( sd );
		return false;
        }
	if ( fcntl ( sd, F_SETFL, flags|O_NONBLOCK|O_ASYNC ) < 0 ) {
		sprintf ( s_errbuf ,"fcntl: %s" , strerror(errno) );
		close ( sd );
		return false;
	}

	// try to fix core in Mem.cpp from trying to freee in thread
	g_mem.setPid(); // s_pid = getpid();
	// crazy?
	if ( g_mem.getPid() == -1 ) {
		log("monitor: bad s_pid"); char *xx=NULL;*xx=0; }

	// start time
	int32_t now = time(NULL);
	int32_t end = now + 25; // 25 seconds to read
 loop:

	if ( (nb = read ( sd , tbuf , 1024*100 ) ) == -1 &&
	     errno != EAGAIN ) {
		sprintf ( s_errbuf ,"read: %s" , strerror(errno) );
		close ( sd );
		return false;
	}
	// 0 means blocked
	if ( nb == -1 ) { 
		now = time(NULL);
		if ( now >= end ) {
			errno = ETIMEDOUT;
			sprintf ( s_errbuf ,"read: timedout after 25 seconds "
				  "of trying to read reply"  );
			close ( sd );
			return false;
		}
		// wait for data to be there! wait 5 ms
		usleep(5);
		//sleep(1);
		goto loop;
	}
	// copy into safebuf
	sbuf.safeMemcpy ( tbuf , nb );
	// keep going if we read something
	if ( nb > 0 ) goto loop; // { sum += nb; goto loop; }
	// add this just in case
	sbuf.pushChar('\0');
	//if ( sum > 1024*100 ) sum = 1024*100;
	//if ( sum >= 0 ) buf[sum] = '\0';
	// . must have read something, at least this for the 'test' query!!!
	// . no results page is only 
	if ( sbuf.length() < 3*1024 ) {
		sprintf ( s_errbuf ,"read: only read %"INT32" bytes for %s. "
			  "readbuf=%s" , sbuf.length()-1, 
			  g_queries[g_qn],
			  sbuf.getBufStart());
		close ( sd );
		return false;
	}	

	// if flurbit. look for "Next " link
	if ( g_isFlurbit && ! strstr(sbuf.getBufStart(),"Next 25") ) {
		sprintf ( s_errbuf ,"monitor: did not read Next 25 link: "
			  "readbuf=%s", sbuf.getBufStart());
		close ( sd );
		return false;
		
	}

	// search for testpreview.com, test.com, toefl.org tmworld.com,...
	// in the search results, at least one should be there!
	char *p = NULL;
	// it must contain at least one substring
	for ( int32_t i = 0 ; i < MAX_SUBSTRINGS && ! g_isFlurbit ; i++ ) {
		// end of list...
		if ( g_substrings[g_qn][i] == NULL ) break;
		p = strstr(sbuf.getBufStart(), g_substrings[g_qn][i]);
		if ( p ) break;
	}
	//if ( ! p ) p = strstr(buf,"testpreview.com");
	//if ( ! p ) p = strstr(buf,"test.com");
	//if ( ! p ) p = strstr(buf,"toefl.org");
	//if ( ! p ) p = strstr(buf,"tmworld.com");
	//if ( ! p ) p = strstr(buf,"collegeboard.com");
	//if ( ! p ) p = strstr(buf,"en.wikipedia.org");

	// debug it!
	//p = NULL;

	if ( ! p && ! g_isFlurbit ) { // && !s_buzz) {
		int32_t slen = sbuf.length();
		char *pbuf = sbuf.getBufStart();
		//if ( slen > 30000 ) pbuf[30000] = '\0';
		if ( slen > 1000 ) pbuf[1000] = '\0';
		snprintf(s_errbuf,45000,
			 "read: bad search results (len=%"INT32") for %s "
			"readbuf="
			 //"????"
			 "%s"
			 ,sbuf.length()-1
			 ,g_queries[g_qn]
			 ,pbuf 
			 );
		close ( sd );
		// do NOT inc g_qn cuz this query needs to work! the
		// problem might be with just it and we need to
		// strike out 3 times to send an email alert
		return false;
	}
	
	// try next
	g_qn++;
	// wrap around
	if ( g_queries[g_qn] == NULL ) g_qn = 0;
	
	close ( sd );
	// success
	return true;
}

// . returns -1 and fill s_errbuf on error
// . returns fd on success
int connectSock ( char *host , uint16_t port ) {

	/*
	// use the same socket connection
	sethostent( 0 ) ; // stayopen? 0--> use udp
	// say what we're monitoring
	hostent *e = gethostbyname( host );
	// close the connection, but not for buzz because they go by ip
	//if(!s_buzz) endhostent ();
	endhostent ();
	// return false on error
	if ( ! e ) {
		sprintf ( s_errbuf ,"connectSock: gethostbyname (%s): %s", 
			  host, hstrerror(h_errno) );
		return -1;
	}
	// get first ip address
	//int32_t n = e->h_length;
	uint32_t ip = *(int32_t *)(e->h_addr_list[0]);
	*/
	pid_t pid = getpid();
	char cmd[256];
	sprintf(cmd,"/usr/bin/dig +int16_t  %s | tail -1 > /tmp/ip.%"UINT32"",
		host,(int32_t)pid);
	system ( cmd );
	char filename[256];
	sprintf(filename,"/tmp/ip.%"UINT32"",(int32_t)pid);
	FILE *fd = fopen( filename,"r");
	char ipstring[256];
	fscanf(fd,"%s",ipstring);
	int32_t ip = atoip(ipstring,strlen(ipstring));
	fclose(fd);

	// print that
	//fprintf(stderr,"ip=%s",iptoa(ip));

	// now make a new socket descriptor
	int sd = socket ( AF_INET , SOCK_STREAM , 0 ) ;
	if ( sd < 0 ) {
		sprintf ( s_errbuf ,"connectSock: socket: %s" , 
			  strerror(errno) );
		return -1;
	}

	//fd_set set;
	//FD_ZERO(&set);
	//FD_SET(sd,&set);
	fcntl ( sd, F_SETFL, O_NONBLOCK );

	// set up for connection
	struct sockaddr_in to;
	to.sin_family = AF_INET;
	// our ip's are always in network order, but ports are in host order
	to.sin_addr.s_addr =  ip;
	to.sin_port        = htons (port);
	bzero ( &(to.sin_zero) , 8 );

	int64_t start = gettimeofdayInMillisecondsLocal();

 connectLoop:
	// NON-BLOCKING!
	// connect to the socket, it returns 0 on success
	int ret = connect ( sd, (sockaddr *)&to, sizeof(to) ) ;

	if ( ret != 0 && errno != EINPROGRESS ) {
		sprintf ( s_errbuf ,"connectSock: connect: %s" , 
			  strerror(errno) );
		close ( sd );
		return -1;
	}

	// if it has been 10 seconds, forget it!
	int64_t now = gettimeofdayInMillisecondsLocal();
	int64_t elapsed = now - start;
	if ( elapsed >= 10000 ) {
		sprintf ( s_errbuf ,"connectSock: connect: TIMEDOUT!", 
			  strerror(errno) );
		close ( sd );
		return -1;
	}

	// sleep for 10ms and try again
	usleep(10);
	goto connectLoop;

	// fake return for compiler
	return 1;
}


