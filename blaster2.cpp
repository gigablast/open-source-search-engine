// Matt Wells, copyright Sep 2001

// the main program that brings it all together

#include "gb-include.h"

#include "Mem.h"
#include "Conf.h"
#include "Dns.h"
#include "HttpServer.h"
#include "Loop.h"
#include <sys/resource.h>  // setrlimit
#include "SafeBuf.h"

static void startSpidering ( ) ;
static void gotDocWrapper ( void *state , TcpSocket *s ) ;
static void sleepWrapper ( int fd , void *state ) ;

bool sendPageSEO(TcpSocket *s, HttpRequest *hr) {return true;}
bool g_recoveryMode;
int g_inMemcpy;
int32_t g_recoveryLevel;

static int32_t  s_maxNumThreads = 1 ;
static int32_t  s_launched   = 0;
static int32_t  s_total      = 0;
static char *s_p          = NULL;
static char *s_pend       = NULL;
static bool  s_portSwitch = 0;
static int32_t  s_wait;
static int32_t  s_lastTime   = 0;
static int32_t  s_printIt    = true;
static char  s_append[512];
static SafeBuf s_words;
static SafeBuf s_windices;
static char *s_server = NULL;
static int32_t  s_numRandWords = 0;
int32_t getRandomWords(char *buf, char *bufend, int32_t numWords);
bool getWords();


bool mainShutdown ( bool urgent ) { return true; }
bool closeAll ( void *state , void (* callback)(void *state) ) {return true;}
bool allExit ( ) {return true;}

int main ( int argc , char *argv[] ) {
	// let's ensure our core file can dump
	struct rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE,&lim) )
		log("blaster::setrlimit: %s", mstrerror(errno) );

	//g_conf.m_maxMem = 500000000;

	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("blaster::hashinit failed" ); return 1; }

	// init the memory class after conf since it gets maxMem from Conf
	//if ( ! g_mem.init ( 20000000 ) ) {
	//	log("blaster::Mem init failed" ); return 1; }
	//g_mem.m_maxMem = 200000000;
	// start up log file
	if ( ! g_log.init( "/tmp/blasterLog" )        ) {
		log("blaster::Log open /tmp/blasterLog failed" ); return 1; }

	// get dns ip from /etc/resolv.conf
	g_conf.m_dnsIps[0] = 0;
	FILE *fd = fopen ( "/etc/resolv.conf" , "r" );
	if ( ! fd ) {
		log("blaster::fopen: /etc/resolve.conf %s",
		    mstrerror(errno)); return 1; }

	char tmp[1024];
	while ( fgets ( tmp , 1024 , fd ) ) {
		// tmp buf ptr
		char *p = tmp;
		// skip comments
		if ( *p == '#' ) continue;
		// skip nameserver name
		if ( ! isdigit(*p) ) while ( ! isspace ( *p ) ) p++ ;
		// skip spaces
		while ( isspace ( *p ) ) p++;
		// if this is not a digit, continue
		if ( ! isdigit(*p) ) continue;
		// get ip
		g_conf.m_dnsIps[0] = atoip ( p , gbstrlen(p) );
		// done
		break;
	}
	fclose ( fd );


	// if no dns server found, bail
	if ( g_conf.m_dnsIps[0] == 0 ) {
		log("blaster:: no dns ip found in /etc/resolv.conf");return 1;}

	// hack # of dns servers
	g_conf.m_numDns         = 1;
	g_conf.m_dnsPorts[0]    = 53;
	//g_conf.m_dnsIps  [0]    = atoip ( "192.168.0.1", 11 );
	//g_conf.m_dnsClientPort  = 9909;
	g_conf.m_dnsMaxCacheMem = 1024*10;
	// hack http server port to -1 (none)
	//g_conf.m_httpPort           = 0;
	g_conf.m_httpMaxSockets     = 200;
	//g_conf.m_httpMaxReadBufSize = 102*1024*1024;
	g_conf.m_httpMaxSendBufSize = 16*1024;
	//g_conf.m_httpMaxDownloadSockets = 200;

	if ( argc != 4 && argc != 5 && argc !=6 ) {
	printUsage:
		fprintf(stderr,"USAGE: blaster [fileOfUrls | -r<num random words><server>] [maxNumThreads] [wait in ms] " 
		    "<lines to skip> <string to append>\n");
		fprintf(stderr,"USAGE: examples:\n");
		fprintf(stderr,"USAGE:  ./blaster queries.fromlog 10 1\n");
		fprintf(stderr,"USAGE:  ./blaster -r3http://www.gigablast.com/index.php?q= 1 100\n");
		return 1; 
	}

	fprintf(stderr,"Logging to /tmp/blasterLog\n");

	// init the loop
	if ( ! g_loop.init() ) {
		log("blaster::Loop init failed" ); return 1; }
	// . then dns client
	// . server should listen to a socket and register with g_loop
	if ( ! g_dns.init(6000)        ) {
		log("blaster::Dns client init failed" ); return 1; }
	// . then webserver
	// . server should listen to a socket and register with g_loop
	for(int32_t i = 0; i < 50; i++) {
		if ( ! g_httpServer.init( 8333 + i, 9334+i ) ) {
			log("blaster::HttpServer init failed" ); 
			//return 1; 
		}
		else break;
	}
	// set File class
	char *fname = argv[1];
	int32_t fnameLen = gbstrlen(fname);
	int32_t fileSize = 0;
	int32_t bufSize = 0;
	char *buf = NULL;
	int32_t  n = 0;

	//should we generate random queries?
	if(fnameLen > 2 && fname[0] == '-' && fname[1] == 'r') {
		char *p = fname + 2;
		s_numRandWords = atoi( p );
		while(is_digit(*p)) p++;
		getWords();
		
		if(*p == '\0') goto printUsage;
		s_server = p;
		log("blaster server is %s", s_server);
		//		char x[1024];
		// 		while(1) {
		// 			int32_t l = getRandomWords(x, x + 1024, s_numRandWords);
		// 			*(x + l) = '\0';
		// 			log("blaster: %s", x);
		// 		}
		//		exit(1);
	}
	else { //it is a real file
		File f;
		f.set ( fname );

		// open file
		if ( ! f.open ( O_RDONLY ) ) {
			log("blaster::open: %s %s",fname,mstrerror(g_errno)); 
			return 1; 
		}

		// get file size
		fileSize = f.getFileSize() ;

		// store a \0 at the end
		bufSize = fileSize + 1;

		// make buffer to hold all
		buf = (char *) mmalloc ( bufSize , "blaster" );
		if ( ! buf) {log("blaster::mmalloc: %s",mstrerror(errno));return 1;}

		//char *bufEnd = buf + bufSize;

		// set s_p
		s_p    = buf;
		s_pend = buf + bufSize - 1;

		// read em all in
		if ( ! f.read ( buf , fileSize , 0 ) ) {
			log("blaster::read: %s %s",fname,mstrerror(g_errno));return 1;}

		// change \n to \0
		//char *p = buf;
		for ( int32_t i = 0 ; i < bufSize ; i++ ) {
			if ( buf[i] != '\n' ) continue;
			buf[i] = '\0';
			n++;
		}

		f.close();
	}
	// log a msg
	log(LOG_INIT,"blaster: read %"INT32" urls into memory", n );

	int32_t linesToSkip = 0;
	if ( argc >=  5 ) {
		linesToSkip = atoi ( argv[4] );
		log (LOG_INIT,"blaster: skipping %"INT32" urls",linesToSkip);
	}
	for ( int32_t i = 0; i < linesToSkip && s_p < s_pend; i++ )
		s_p += gbstrlen(s_p) + 1;
	
	if ( argc == 6 ) {
		int32_t len  = gbstrlen ( argv[5] );
		if ( len > 512 )
			len = 512;
		strncpy ( s_append , argv[5] , gbstrlen (argv[5]) );
	}
	else
		s_append[0] = '\0';

	// get min time bewteen each spider in milliseconds
	s_wait = atoi( argv[3] );

	// # of threads
	s_maxNumThreads = 1;
	s_maxNumThreads = atoi ( argv[2] );

	s_portSwitch = 0;
	//if ( argc == 4 ) s_portSwitch = 1;
	//else             s_portSwitch = 0;

	// start our spider loop
	//startSpidering( );

	// wakeup wrapper every X ms
	g_loop.registerSleepCallback ( s_wait , NULL , sleepWrapper );

	//msg10.addUrls ( uu , gbstrlen(uu)+1, NULL,0,time(0),4,true,NULL,NULL);
	// . now start g_loops main interrupt handling loop
	// . it should block forever
	// . when it gets a signal it dispatches to a server or db to handle it
	if ( ! g_loop.runLoop()    ) {
		log("blaster::runLoop failed" ); return 1; }
	// dummy return (0-->normal exit status for the shell)
	return 0;
}

void sleepWrapper ( int fd , void *state ) {
	startSpidering();
}


void startSpidering ( ) {
	// url class for parsing/normalizing url
	Url u;
	// count total urls done
	static int64_t s_startTime = 0;
	// set startTime
	if ( s_startTime == 0 ) s_startTime = gettimeofdayInMilliseconds();
	// get time now
	int64_t now = gettimeofdayInMilliseconds();
	// elapsed time to do all urls
	double took = (double)(now - s_startTime) / 1000.0 ;
	// log this every 20 urls
	if ( s_printIt && s_total > 0 && ( s_total % 20 ) == 0 ) {
		logf(LOG_INFO,"did %"INT32" urls in %f seconds. %f urls per second."
		    " threads now = %"INT32".",
		    s_total ,  took , ((double)s_total) / took, s_launched);
		s_printIt = false;
	}
	// did we wait int32_t enough?
	if ( now - s_lastTime < s_wait ) return;
	s_lastTime = now;
	// . use HttpServer.getDoc() to fetch it
	// . fetch X at a time
	while ( (s_server || s_p < s_pend) && s_launched < s_maxNumThreads ) {
		// clear any error
		g_errno = 0;
		//append s_append to the url
		char url[MAX_URL_LEN];
		char *p = url;
		char *pend = url + MAX_URL_LEN;
		char *t = NULL;

		if(s_server) {
			int32_t len = gbstrlen(s_server);
			gbmemcpy ( p, s_server, len);
			p += len;
			p += getRandomWords(p, pend, s_numRandWords);
			int32_t appendLen = gbstrlen(s_append);
			if ( p + appendLen < pend ) {
				gbmemcpy ( p, s_append, gbstrlen(s_append) );
				p += gbstrlen(s_append);
			}
			*p++ = '\0';
			u.set ( url , p - url);
			t = g_mem.strdup(url, "saved url");
		}
		else {
			gbmemcpy ( p, s_p, gbstrlen(s_p));
			p += gbstrlen ( s_p );
			if ( gbstrlen(s_p) + gbstrlen(s_append) < MAX_URL_LEN )
				gbmemcpy ( p, s_append, gbstrlen(s_append) );
			p += gbstrlen(s_append);
			//null end
			*p ='\0';

			// make into a url class
			u.set ( url , gbstrlen(url) );
			// set port if port switch is true
			//if ( s_portSwitch ) {
			//	int32_t r = rand() % 32;
			//	u.setPort ( 8000 + r );
			//}
			// save s_p
			t = s_p;
			// skip to next url
			s_p += gbstrlen ( s_p ) + 1;
		}
		// count it
		s_launched++;
		// get it
		bool status = g_httpServer.getDoc ( u.getUrl() , // url
						    0, // ip
						    0 ,  // offset
						    -1 ,  // size
						    0 , // ifModifiedSince
						    (void *)t ,  // state
						    gotDocWrapper, // callback
						    20*1000, // timeout
						    0, // proxy ip
						    0, // proxy port
						    30*1024*1024, //maxLen
						    30*1024*1024);//maxOtherLen
		// continue if it blocked
		if ( ! status ) continue;
		// otherwise, got it right away
		s_launched--;
		// log msg
		log("got doc1 %s: %s", u.getUrl() , mstrerror(g_errno) );
		// we gotta wait
		break;
	}
	// bail if not done yet
	//if ( s_launched > 0 ) return;
	if ( s_server || s_p < s_pend ) return;
	// otherwise, we're all done
	logf(LOG_INFO,"blaster: did %"INT32" urls in %f seconds. %f urls per "
	     "second.",
	    s_total ,  took , ((double)s_total) / took );
	// exit now
	exit ( 0 );
}

void gotDocWrapper ( void *state , TcpSocket *s ) {
	// no longer launched
	s_launched--;
	char* url = (char*)state;
	// bail if got cut off
	if ( s->m_readOffset == 0 ) {
		log("lost %s",(char *) state);
		if(s_server) mfree(url, gbstrlen(url)+1, "saved url");
		return;
	}
	// got one more result page
	s_total++;
	// allow printing
	s_printIt = true;
	// get time now
	int64_t now = gettimeofdayInMilliseconds();
	// get hash
	char *reply = s->m_readBuf ;
	int32_t  size  = s->m_readOffset;
	HttpMime mime;
	mime.set ( reply , size , NULL );
	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = size  - mime.getMimeLen();
	int32_t status      = mime.getHttpStatus();
	uint32_t h = hash32 ( content , contentLen );
	char *p = mime.getMime();
	char *pend = p + mime.getMimeLen();
	char message[256];
	int32_t mlen = 0;

	// parse status message out of response

	// HTTP/1.0
	while ( p < pend && !isspace(*p) ) p++;
	// skip space
	while ( p < pend &&  isspace(*p) ) p++;
	// copy to end of line
	while (p < pend && mlen < 255 && *p != '\r' && *p != '\n'){
		message[mlen++] = *p;
	}
	message[mlen] = '\0';

	// log msg
	if ( g_errno ) 
		logf(LOG_INFO,"blaster: got doc (status=%"INT32") (%"INT32") (%"INT32"ms) %s : "
		     "%s", status,
		      s->m_readOffset      , 
		      (int32_t)(now - s->m_startTime) , 
		      (char *)state        , 
		      mstrerror(g_errno)   );
	else
		logf(LOG_INFO,"blaster: got doc (status=%"INT32") (%"INT32") (%"INT32"ms) "
		     "(hash=%"XINT32") %s", status,
		      s->m_readOffset      , 
		      (int32_t)(now - s->m_startTime) , 
		      h ,
		      (char *)state        );

	if(s_server) mfree(url, gbstrlen(url)+1, "saved url");
	// try to launch another
	startSpidering();
}

int32_t getRandomWords(char *buf, char *bufend, int32_t numWords) {
	int32_t totalWords = s_windices.length() / sizeof(int32_t);
	char *p = buf;
	while(1) {
		int32_t wordNum = rand() % totalWords;
		int32_t windex = *(int32_t*)(&s_windices[wordNum*sizeof(int32_t)]);
		int32_t wlen = gbstrlen(&s_words[windex]);
		if(wlen + 1 + p >= bufend) return p - buf;
		gbmemcpy(p, &s_words[windex], wlen);
		p += wlen;
		if(--numWords <= 0) return p - buf;
		*p++ = '+';
	}
	return p - buf;
}

bool getWords() {
	FILE *fd = fopen ( "/usr/share/dict/words" , "r" );
	if ( ! fd ) {
		log("blaster:: failed to open /usr/share/dict/words %s",
		    mstrerror(errno)); 
		return 1; 
	}
	char tmp[1024];
	while ( fgets ( tmp , 1024 , fd ) ) {
		int32_t len = gbstrlen(tmp);
		if(len > 2 && tmp[len-2] == 's' && tmp[len-3] == '\'') continue;
		s_windices += s_words.length();
		s_words.safeMemcpy(tmp, len-1); //copy in data minus the newline
		s_words += '\0';
	}
	fclose ( fd );
	log("blaster: read %"INT32" words, "
	    "%"INT32" bytes in from dictionary.", 
	    (int32_t)(s_windices.length() / sizeof(int32_t)), 
	    (int32_t)s_words.length());
	return true;
}
