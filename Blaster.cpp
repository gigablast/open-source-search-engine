// Matt Wells, copyright Sep 2001

// the main program that brings it all together

#include "gb-include.h"

#include "Blaster.h"
#include "Titledb.h" // TITLEREC_CURRENT_VERSION
#include "Linkdb.h"

Blaster g_blaster;
static void gotDocWrapper1 ( void *state , TcpSocket *s ) ;
static void gotDocWrapper2 ( void *state , TcpSocket *s ) ;
static void gotDocWrapper3 ( void *state , TcpSocket *s ) ;
static void gotDocWrapper4 ( void *state , TcpSocket *s ) ;
static void sleepWrapper ( int fd , void *state ) ;
static void sleepWrapperLog(int fd, void *state);

Blaster::Blaster() {}

Blaster::~Blaster() {
	if (m_buf1)
		mfree(m_buf1,m_bufSize1,"blaster1");
	if (m_buf2)
		mfree(m_buf2,m_bufSize2,"blaster2");
}


bool Blaster::init(){
	// let's ensure our core file can dump
	struct rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE,&lim) )
		log("blaster::setrlimit: %s", mstrerror(errno) );
	
	g_conf.m_maxMem = 500000000;
	
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("blaster::hashinit failed" ); return 0; }

	// init the memory class after conf since it gets maxMem from Conf
	if ( ! g_mem.init ( ) ) {//200000000 ) ) {
		log("blaster::Mem init failed" ); return 0; }
	// start up log file
	if ( ! g_log.init( "/tmp/blasterLog" )        ) {
		log("blaster::Log open /tmp/blasterLog failed" ); return 0; }

	/*
	// get dns ip from /etc/resolv.conf
	g_conf.m_dnsIps[0] = 0;
	FILE *fd = fopen ( "/etc/resolv.conf" , "r" );
	if ( ! fd ) {
		log("blaster::fopen: /etc/resolve.conf %s",
		    mstrerror(errno)); return 0; }

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
		log("blaster:: no dns ip found in /etc/resolv.conf");return 0;}

	// hack # of dns servers
	g_conf.m_numDns         = 1;
	g_conf.m_dnsPorts[0]    = 53;
	*/

	g_conf.m_askRootNameservers = true;

	//g_conf.m_dnsIps  [0]    = atoip ( "192.168.0.1", 11 );
	//g_conf.m_dnsClientPort  = 9909;
	g_conf.m_dnsMaxCacheMem = 1024*10;
	// hack http server port to -1 (none)
	//g_conf.m_httpPort           = 0;
	g_conf.m_httpMaxSockets     = 200;
	//g_conf.m_httpMaxReadBufSize = 102*1024*1024;
	g_conf.m_httpMaxSendBufSize = 16*1024;


	// init the loop
	if ( ! g_loop.init() ) {
		log("blaster::Loop init failed" ); return 0; }
	// . then dns client
	// . server should listen to a socket and register with g_loop
	if ( ! g_dns.init(6000)        ) {
		log("blaster::Dns client init failed" ); return 0; }
	// . then webserver
	// . server should listen to a socket and register with g_loop
	if ( ! g_httpServer.init( 8333 , 9334 ) ) {
		log("blaster::HttpServer init failed" ); return 0; }
	return 1;
}
	
void Blaster::runBlaster(char *file1,char *file2,
			 int32_t maxNumThreads, int32_t wait, bool isLogFile,
			 bool verbose,bool justDisplay,
			 bool useProxy ,
			 bool injectUrlWithLinks ,
			 bool injectUrl ) {
	if (!init())
		return;
	m_blasterDiff=true;
	if (!file2)
		m_blasterDiff=false;
	
	// set File class
	File f1;
	f1.set ( file1 );

	// open files
	if ( ! f1.open ( O_RDONLY ) ) {
		log("blaster:open: %s %s",file1,mstrerror(g_errno)); 
		return; 
	}

	// get file size
	int32_t fileSize1 = f1.getFileSize() ;
	// store a \0 at the end
	int32_t m_bufSize1 = fileSize1 + 1;

	m_doInjectionWithLinks = injectUrlWithLinks;
	m_doInjection = injectUrl;

	// make buffers to hold all
	m_buf1 = (char *) mmalloc ( m_bufSize1 , "blaster1" );
	if ( ! m_buf1) {
		log("blaster:mmalloc: %s",mstrerror(errno));
		return;
	}

	//char *bufEnd = buf + bufSize;

	// set m_p1
	m_p1    = m_buf1;
	m_p1end = m_buf1 + m_bufSize1 - 1;

	// read em all in
	if ( ! f1.read ( m_buf1 , fileSize1 , 0 ) ) {
		log("blaster:read: %s %s",file1,mstrerror(g_errno));
		return;
	}

	// change \n to \0
	//char *p = buf;
	int32_t  n = 0;
	for ( int32_t i = 0 ; i < m_bufSize1 ; i++ ) {
		if ( m_buf1[i] != '\n' ) continue;
		m_buf1[i] = '\0';
		n++;
	}


	if (m_blasterDiff){
		File f2;
		f2.set ( file2 );
		if ( ! f2.open ( O_RDONLY ) ) {
			log("blaster:open: %s %s",file2,mstrerror(g_errno)); 
			return; 
		}
		int32_t fileSize2 = f2.getFileSize() ;
		int32_t m_bufSize2 = fileSize2 + 1;
		m_buf2 = (char *) mmalloc ( m_bufSize2 , "blaster2" );
		if ( ! m_buf2) {
			log("blaster:mmalloc: %s",mstrerror(errno));
			return;
		}
		// set m_p2
		m_p2    = m_buf2;
		m_p2end = m_buf2 + m_bufSize2 - 1;
		if ( ! f2.read ( m_buf2 , fileSize2 , 0 ) ) {
			log("blaster:read: %s %s",file2,mstrerror(g_errno));
			return;
		}
		int32_t m=0;
		for ( int32_t i = 0 ; i < m_bufSize2 ; i++ ) {
			if ( m_buf2[i] != '\n' ) continue;
			m_buf2[i] = '\0';
			m++;
		}
		// Working on only the least number of urls from both files, 
		//because we need to work in pairs
		if (m<n) n=m;
		else m=n;
		m_totalUrls=n;

		// should we print out all the logs?
		m_verbose=verbose;
		// Should we use the proxy for getting the first Doc
		m_useProxy=useProxy;
		// Should we just display the not present links and not fetch
		// the page to see if they are actually present ?
		m_justDisplay=justDisplay;
	}
	else{
		m_isLogFile=isLogFile;
		
		/*if reading a gigablast log file, find the lines that have 
		  GET and POST commands for search, and register a sleep
		  callback for those lines with sleepWrapperLog*/
		if(!isLogFile)
			m_totalUrls=n;
		else {
			m_totalUrls=0;
			char *p=m_buf1;
			char *pend=p+m_bufSize1;
			
			// start is the time in milliseconds of the first log 
			// message
			int64_t start=atoll(m_buf1);
			while(p<pend) {
				char *lineStart=p;
				char *urlStart=strstr(p," GET /search");
				if (!urlStart)
					urlStart=strstr(p," POST /search");
				if(!urlStart){
					p+=gbstrlen(p)+1; //goto next line
					continue;
				}
				urlStart++;
				m_wait=atoll(lineStart)-start;
				// register it here
				g_loop.registerSleepCallback(m_wait , 
							     urlStart, 
							     sleepWrapperLog);
				m_totalUrls++;
				p+=gbstrlen(p)+1;
			}
		}
	}
	log(LOG_INIT,"blaster: read %"INT32" urls into memory", 
	    m_totalUrls );

	if(!isLogFile){
		// get min time bewteen each spider in milliseconds
		m_wait = wait;
			
		// # of threads
		m_maxNumThreads = maxNumThreads;
		
		m_launched=0;
		
		m_portSwitch = 0;
		//if ( argc == 4 ) m_portSwitch = 1;
		//else             m_portSwitch = 0;
			
		// start our spider loop
		//startSpidering( );
		
		// wakeup wrapper every X ms
		g_loop.registerSleepCallback ( m_wait , NULL , 
					       sleepWrapper );
	}
	// this print to print how many docs have been processed
	m_print=false;
	m_startTime=gettimeofdayInMilliseconds();
	m_totalDone=0;
	// . now start g_loops main interrupt handling loop
	// . it should block forever
	// . when it gets a signal it dispatches to a server or db to handle it
	if ( ! g_loop.runLoop()    ) {
		log("blaster::runLoop failed" ); return; }
	// dummy return (0-->normal exit status for the shell)
	return;
}

void sleepWrapper ( int fd , void *state ) {
	g_blaster.startBlastering();
}

void sleepWrapperLog(int fd, void *state) {
	// unregister the sleepCallback
	g_loop.unregisterSleepCallback(state,sleepWrapperLog);
	g_blaster.processLogFile(state);
}

void Blaster:: processLogFile(void *state){
	// No need to print how many docs processed in log
	// because this is called at epochs given in the log
	char *urlStart=(char*)state;
	if (!urlStart){
		log(LOG_WARN,"blaster: got NULL urlStart");
		return;
	}
	//	log(LOG_WARN,"blaster:: Line is %s",urlStart);
	char tmp[1024];
	if (urlStart[0]=='P'){ //POST
		// advance by "POST /search HTTP/1.1 " = 22 chars
		urlStart+=22;
		sprintf(tmp,"http://www.gigablast.com/search?%s",urlStart);
	}
	else if (urlStart[0]=='G'){ //GET
		// advance by "GET "= 4 chars
		urlStart+=4;
		char *end=strstr(urlStart," HTTP/1.");
		if (end)
			end[0]='\0';
		sprintf(tmp,"http://www.gigablast.com%s",urlStart);
	}
	//	log(LOG_WARN,"blaster: URL=%s",tmp);
	StateBD *st;
	try { st = new (StateBD); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("blaster: Failed. "
		    "Could not allocate %"INT32" bytes for query. "
		    "Returning HTTP status of 500.",
		    (int32_t)sizeof(StateBD));
		return;
	}
	mnew ( st , sizeof(StateBD) , "BlasterDiff3" );
	//st->m_u1.set(tmp,gbstrlen(tmp));
	st->m_buf1=NULL;
	// get it
	bool status = g_httpServer.getDoc ( tmp, // &(st->m_u1) , // url
					    0 , // ip (none)
					    0 ,  // offset
					    -1 ,  // size
					    0 , // ifModifiedSince
					    st,  // state
					    gotDocWrapper1, // callback
					    20*1000, // timeout
					    0, // proxy ip
					    0, // proxy port
					    30*1024*1024, //maxLen
					    30*1024*1024);//maxOtherLen
	// continue if it blocked
	if ( status )
		// else there was error
		log("blaster: got doc %s: %s", urlStart,mstrerror(g_errno) );
	return;
}
	

void Blaster::startBlastering(){
	int64_t now=gettimeofdayInMilliseconds();
	if(m_print && m_totalDone>0 && (m_totalDone % 20)==0){
		log("blaster: Processed %"INT32" urls in %"INT32" ms",m_totalDone,
		    (int32_t) (now-m_startTime));
		m_print=false;
	}
	//Launch the maximum number of threads that are allowed
	while ( m_p1 < m_p1end && m_launched < m_maxNumThreads && m_totalUrls){
		// clear any error
		g_errno = 0;
		// make a new state
		StateBD *st;
		try { st = new (StateBD); }
		catch ( ... ) {
			g_errno = ENOMEM;
			log("blaster: Failed. "
			    "Could not allocate %"INT32" bytes for query. "
			    "Returning HTTP status of 500.",
			    (int32_t)sizeof(StateBD));
			return;
		}
		mnew ( st , sizeof(StateBD) , "BlasterDiff3" );
		st->m_buf1=NULL;
		m_totalUrls--;
		// make into a url class. Set both u1 and u2 here.
		//st->m_u1.set ( m_p1 , gbstrlen(m_p1) );
		st->m_u1 = m_p1;
		// is it an injection url
		if ( m_doInjection || m_doInjectionWithLinks ) {
			// get host #0 i guess
			Host *h0 = g_hostdb.getHost(0);
			if ( ! h0 ) { char *xx=NULL;*xx=0; }
			static bool s_flag = true;
			if ( s_flag ) {
				s_flag = false;
				log("blaster: injecting to host #0 at %s on "
				    "http/tcp port %"INT32"",
				    iptoa(h0->m_ip),
				    (int32_t)h0->m_httpPort);
			}
			// use spiderlinks=1 so we add the outlinks to spiderdb
			// but that will slow the spider rate down since it 
			// will have to do a dns lookup on the domain of every
			// outlink.
			st->m_injectUrl.safePrintf("http://127.0.0.1:8000/"
						   "admin/inject?");
			if ( m_doInjectionWithLinks )
				st->m_injectUrl.safePrintf("spiderlinks=1&");
			else
				st->m_injectUrl.safePrintf("spiderlinks=0&");
			st->m_injectUrl.safePrintf("u=");
			st->m_injectUrl.urlEncode(m_p1);
			st->m_injectUrl.pushChar('\0');
			st->m_u1 = st->m_injectUrl.getBufStart();
		}
		// skip to next url
		m_p1 += gbstrlen ( m_p1 ) + 1;
		if (m_blasterDiff){
			//st->m_u2.set ( m_p2 , gbstrlen(m_p2) );
			st->m_u2 = m_p2;
			m_p2 += gbstrlen ( m_p2 ) + 1;
		}

		//		log(LOG_WARN,"\n");
		log(LOG_WARN,"blaster: Downloading %s",st->m_u1);
		// set port if port switch is true
		//if ( m_portSwitch ) {
		//	int32_t r = rand() % 32;
		//	u.setPort ( 8000 + r );
		//}

		// count it
		m_launched++;
		int32_t ip=0;
		int32_t port=0;
		if (m_useProxy){
			ip=atoip("66.154.102.20",13);
			port=3128;
		}
		// get it
		bool status = g_httpServer.getDoc ( st->m_u1 , // url
						    0, // ip
						    0 ,  // offset
						    -1 ,  // size
						    0 , // ifModifiedSince
						    st ,  // state
						    gotDocWrapper1, // callback
						    60*1000, // timeout
						    ip,
						    port,
						    30*1024*1024, //maxLen
						    30*1024*1024);
		// continue if it blocked
		if ( ! status ) continue;
		// If not blocked, there is an error.
		m_launched--;
		// log msg
		log("From file1, got doc1 %s: %s", st->m_u1 , 
		    mstrerror(g_errno) );
		// we gotta wait
		break;
	}
	// bail if not done yet
	//if ( m_launched > 0 ) return;
	if (m_totalUrls) return;
	//otherwise return if launched have not come back
	if (m_launched) return;
	// exit now
	//	g_conf.save();
	//	closeALL(NULL,NULL);
	exit ( 0 );
}


void gotDocWrapper1 ( void *state , TcpSocket *s ) {
	g_blaster.gotDoc1(state,s);
}

void Blaster::gotDoc1( void *state, TcpSocket *s){
	StateBD *st=(StateBD *)state;
	// Even if we loose the request, still count it as done.
	m_totalDone++;
	m_print=true;
	// bail if got cut off
	if ( s->m_readOffset == 0 ) {
		log("blaster: lost the Request in gotDoc1");
		m_launched--;
		freeStateBD(st);
		return;
	}

	//if we are not doing diff
	if (!m_blasterDiff){
		m_launched--;
	}
	int64_t now = gettimeofdayInMilliseconds();
	// get hash
	char *reply = s->m_readBuf ;
	int32_t  size  = s->m_readOffset;
	HttpMime mime;
	mime.set ( reply , size , NULL );
	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = size  - mime.getMimeLen();
	uint32_t h = hash32 ( content , contentLen );
	// log msg
	if ( g_errno ) 
		logf(LOG_INFO,"blaster: got doc (%"INT32") (%"INT32" ms) %s : %s",
		     s->m_readOffset      , 
		     (int32_t)(now - s->m_startTime) , 
		     st->m_u1   , 
		     mstrerror(g_errno)   );
	else
		logf(LOG_INFO,"blaster: got doc (%"INT32") (%"INT32" ms) "
		     "(hash=%"XINT32") %s",
		     s->m_readOffset      , 
		     (int32_t)(now - s->m_startTime) , 
		     h ,
		     st->m_u1       );
	if (!m_blasterDiff){
		// try to launch another if not using log file
		freeStateBD(st);
		if (!m_isLogFile){
			startBlastering();
		}
		if (m_isLogFile && --m_totalUrls==0) exit(0);
		return;
	}

	// Store the buffer from socket so that it does not get destroyed
	// at the end. Also, add another space because in gotDoc2 xml.set
	// demands the content to be null ended, so we need to store the
	// null character there. So as a precaution, just allocating the
	// max buf size.
	st->m_buf1=(char*) mcalloc(s->m_readBufSize,"Blaster5");
	gbmemcpy(st->m_buf1,s->m_readBuf,s->m_readOffset);
	//st->m_buf1=(char*) mdup(s->m_readBuf,s->m_readOffset,"Blaster5");
	st->m_buf1Len=s->m_readOffset;
	st->m_buf1MaxLen=s->m_readBufSize;

	// . don't let TcpServer free m_buf when socket is recycled/closed
	// . we own it now and are responsible for freeing it. DON'T do this
	// because I believe this makes malloc crash, since TcpServer says
	// that it has freed the memory so malloc tries to allocate wrong
	// memory and gives a seg fault.
	//	s->m_readBuf = NULL;
	
	log(LOG_WARN,"blaster: Downloading %s",st->m_u2);
	//char *ss="www.gigablast.com/search?q=hoopla&code=gbmonitor";
	//	st->m_u2.set(ss,gbstrlen(ss));
	// get it
	bool status = g_httpServer.getDoc ( st->m_u2 , // url
					    0,//ip
					    0 ,  // offset
					    -1 ,  // size
					    0 , // ifModifiedSince
					    st ,  // state
					    gotDocWrapper2, // callback
					    60*1000, // timeout
					    0,//atoip("66.154.102.20",13),//proxy ip
					    0,//3128,//80, // proxy port
					    30*1024*1024, //maxLen
					    30*1024*1024);//maxOtherLen
	// continue if it blocked
	if ( ! status ) return;
	// If not blocked, there is an error.
	m_launched--;
	// log msg
	log("From file2, gotdoc2 %s: %s", st->m_u2,
	    mstrerror(g_errno) );
	// No need to point p2 ahead because already been done
	// Free stateBD
	freeStateBD(st);
	return;
	
}

void gotDocWrapper2 ( void *state , TcpSocket *s ) {
	g_blaster.gotDoc2(state,s);
}

void Blaster::gotDoc2 ( void *state, TcpSocket *s){
	StateBD *st=(StateBD *)state;
	// bail if got cut off
	if ( s->m_readOffset == 0 ) {
		log("blaster: Lost the Request in gotDoc2");
		m_launched--;
		//No need to point p2
		// Free stateBD
		freeStateBD(st);
		return;
	}
	
	// . don't let TcpServer free m_buf when socket is recycled/closed
	// . we own it now and are responsible for freeing it
	//	s->m_readBuf = NULL;

	int64_t now = gettimeofdayInMilliseconds();
	// So now after getting both docIds, get their contents
	char *reply1 = st->m_buf1 ;
	int32_t  size1  = st->m_buf1Len;
	HttpMime mime1;
	mime1.set ( reply1 , size1 , NULL );
	char *content1    = reply1 + mime1.getMimeLen();
	int32_t  content1Len = size1  - mime1.getMimeLen();
	uint32_t h = hash32 ( content1 , content1Len );
	// log msg
	if ( g_errno ) 
		logf(LOG_INFO,"blaster: got doc (%"INT32") (%"INT32" ms) %s : %s",
		     s->m_readOffset      , 
		     (int32_t)(now - s->m_startTime) , 
		     st->m_u2   , 
		     mstrerror(g_errno)   );
	else
		logf(LOG_INFO,"blaster: got doc (%"INT32") (%"INT32" ms) "
		     "(hash=%"XINT32") %s",
		     s->m_readOffset      , 
		     (int32_t)(now - s->m_startTime) , 
		     h ,
		     st->m_u2       );


	if (m_verbose){
		log(LOG_WARN,"blaster: content1len=%"INT32", Content1 is =%s",
		    content1Len,content1);
		log(LOG_WARN,"\n");
	}
	char *reply2 = s->m_readBuf ;
	int32_t  size2  = s->m_readOffset;
	HttpMime mime2;
	mime2.set ( reply2 , size2 , NULL );
	char *content2    = reply2 + mime2.getMimeLen();
	int32_t  content2Len = size2  - mime2.getMimeLen();
	if (m_verbose)	
		log(LOG_WARN,"blaster: content2len=%"INT32", Content2 is =%s",
		    content2Len,content2);

	// Now that we've got the contents, lets get the url links out 
	// of these pages.Passing them to function getSearchLinks should 
	// get the first x links found out.
	/*	st->m_links1=(char *) mmalloc(200*MAX_URL_LEN,"Blaster3");
	st->m_links2=st->m_links1+100*MAX_URL_LEN;
	st->m_numLinks1=100;
	st->m_numLinks2=100;*/

	/*	int32_t numLinks1=getSearchLinks(content1,content1Len,
				      st->m_links1,st->m_numLinks1);
	int32_t numLinks2=getSearchLinks(content2,content2Len,
	st->m_links2,st->m_numLinks2);*/


	content1[content1Len]='\0';
	//int16_t csEnum1= get_iana_charset(mime1.getCharset(), 
	//				mime1.getCharsetLen());
	/*	if (csEnum1== csUnknown)
		log(LOG_DEBUG, "blaster: Unknown charset : %s", mime2.getCharset());*/
	Xml xml1;
	// assume utf8
	if (!xml1.set(content1, 
		     content1Len,
		     false,
		     0,
		     false,
		      TITLEREC_CURRENT_VERSION ,
		      true , // set parents
		      0 , // niceness 
		      CT_XML )){ // content type
		log(LOG_WARN,"blaster: Couldn't set XML1 Class in gotDoc2");
	}
	Links links1;
	Url parent; parent.set ( st->m_u1);
	if (!links1.set(false , // userellnofollow
			&xml1,
			&parent,//mime1.getLocationUrl(), parent Url
			false, // setLinkHashes
			NULL  , // baseUrl
			TITLEREC_CURRENT_VERSION, // version
			0 , // niceness
			false , // parent is permalink?
			NULL )) { // oldLinks
		log(LOG_WARN,"blaster: Couldn't set Links Class in gotDoc2");
	}

	content2[content2Len]='\0';
	//int16_t csEnum2= get_iana_charset(mime2.getCharset(), 
	//				mime2.getCharsetLen());
	/*	if (csEnum2== csUnknown)
		log(LOG_DEBUG, "blaster: Unknown charset : %s", mime2.getCharset());*/
	Xml xml2;
	if (!xml2.set(content2, 
		     content2Len,
		     false,
		     0,
		     false,
		      TITLEREC_CURRENT_VERSION,
		      true , // setparents
		      0 , // niceness
		      CT_XML )){
		log(LOG_WARN,"blaster: Couldn't set XML2 Class in gotDoc2");
	}
	Links links2;
	parent.set(st->m_u2);
	if (!links2.set(0,//siterec xml
			&xml2,
			&parent,//&st->m_u2,//mime2.getLocationUrl(),
			false,
			NULL,
			TITLEREC_CURRENT_VERSION,
			0,
			false,
			NULL)){
		log(LOG_WARN,"blaster: Couldn't set links2 Class in gotDoc2");
	}
	

	// put the hash of the sites into a hashtable, since we have
	// about a 100 or so of them
	HashTableT<uint32_t, bool> urlHash;
	// put the urls from doc2 into the hastable, but first check if
	// they are links to google or gigablast (for now). For msn and
	// yahoo we have to add other checks.
	char domain2[256];
	int32_t dlen = 0;
	char *dom = getDomFast ( st->m_u2 , &dlen );
	if ( dom ) strncpy(domain2,dom,dlen);
	domain2[dlen]='\0';
	for (int32_t i=0;i<links2.getNumLinks();i++){
		// The dots check if exactly google or gigablast are present
		// in the link
		char *ss=links2.getLink(i);
		char *p;
		p=strstr(ss,domain2);
		if(p) continue;
		p=strstr(ss,"google.");
		if(p) continue;
		p=strstr(ss,"cache:");  //googles cache page
		if(p) continue;
		p= strstr(ss,"gigablast.");
		if(p) continue;
		p= strstr(ss,"web.archive.org");//older copies on gigablast
		if(p) continue;
		p= strstr(ss,"search.yahoo.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"search.msn.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"s.teoma.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"search.dmoz.org");//from gigablast search
		if(p) continue;
		p= strstr(ss,"www.answers.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"cc.msncache.com");//msn's cache page
		if(p) continue;
		if (m_verbose)
			log(LOG_WARN,"blaster: link in Doc2=%s"
			    ,links2.getLink(i));
		uint32_t h=hash32Lower_a(links2.getLink(i),
					    links2.getLinkLen(i));
		//should i check for conflict. no, because it doesn't matter
		urlHash.addKey(h,1);
	}
	// now check if the urls from doc1 are in doc2. save the
	// ones that are not
	// in there for later.
	/*	int32_t numUrlsToCheck=links2.getNumLinks();*/
	int32_t numUrlsNotFound=0;
	/*if (numLinks1<numUrlsToCheck)
	numUrlsToCheck=numLinks1;*/
	char domain1[256];
	dlen = 0;
	dom = getDomFast ( st->m_u1 ,&dlen );
	if ( dom ) strncpy(domain1,dom,dlen);
	domain1[dlen]='\0';
	for (int32_t i=0;i<links1.getNumLinks();i++){
		char *ss=links1.getLink(i);
		char *p;
		p=strstr(ss,domain1);
		if(p) continue;
		p=strstr(ss,"google.");
		if(p) continue;
		p=strstr(ss,"cache:");  //googles cache page
		if(p) continue;
		p= strstr(ss,"gigablast.");
		if(p) continue;
		p= strstr(ss,"web.archive.org");//older copies on gigablast
		if(p) continue;
		p= strstr(ss,"search.yahoo.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"search.msn.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"s.teoma.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"search.dmoz.org");//from gigablast search
		if(p) continue;
		p= strstr(ss,"www.answers.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"cc.msncache.com");//msn's cache page
		if(p) continue;
		if (m_verbose)
			log(LOG_WARN,"blaster: link in Doc1=%s"
			    ,links1.getLink(i));
		uint32_t h=hash32Lower_a(links1.getLink(i),
					    links1.getLinkLen(i));
		int32_t slot= urlHash.getSlot(h);		
		if(slot!=-1) continue;

		// if url is not present, get its doc.
		if (m_verbose || m_justDisplay)
			log(LOG_WARN,"blaster: NOT FOUND %s in %s"
			    ,links1.getLink(i),domain2);
		numUrlsNotFound++;
		//Don't do anything else if just have to display the urls
		if (m_justDisplay) continue;
		//now get the doc of these urls
		//initialize
		st->m_numUrlDocsReceived=0;

		StateBD2 *st2;
		try { st2 = new (StateBD2); }
		catch ( ... ) {
			g_errno = ENOMEM;
			log("blaster: Failed. "
			    "Could not allocate %"INT32" bytes for query. "
			    "Returning HTTP status of 500.",
			    (int32_t)sizeof(StateBD2));
			return;
		}
		mnew ( st2 , sizeof(StateBD2) , "Blaster4" );
		//Point to the big state;
		st2->m_st=st;
		//Msg16 does 6 redirects, so I do 6 too
		st2->m_numRedirects=6;
		//st2->m_url.set(links1.getLink(i),links1.getLinkLen(i));
		st2->m_url = links1.getLink(i);
		// No need for a proxy ip here, since we are fetching
		// doc's from different IPs. Faster this way
		bool status = g_httpServer.getDoc ( st2->m_url, // url
						    0,//ip
						    0 ,  // offset
						    -1 ,  // size
						    0 , // ifModifiedSince
						    st2,  // state
						    gotDocWrapper3, // callback
						    60*1000, // timeout
						    0, // proxy ip
						    0, // proxy port
						    30*1024*1024, //maxLen
						    30*1024*1024);//maxOtherLen
		// continue if it blocked
		if ( ! status ) continue;
		// If not blocked, there is an error.
		st->m_numUrlDocsReceived++;
	}
	st->m_numUrlDocsSent=numUrlsNotFound;

	//There might have been an error while sending the docs, so if there
	//has been put a check
	if ( st->m_numUrlDocsReceived > 0 && 
	     st->m_numUrlDocsReceived <= st->m_numUrlDocsSent ){
		log(LOG_WARN,"blaster: %"INT32" docs could not be sent due to "
		    "error",st->m_numUrlDocsReceived);
		m_launched--;
		freeStateBD(st);
		return;
	}
		
	if (numUrlsNotFound==0){
		//job done for this pair
		log(LOG_WARN,"blaster: All urls from %s found in "
		    "%s",domain1,domain2);
		m_launched--;
		// Free stateBD
		freeStateBD(st);
		return;
	}
	log(LOG_WARN,"blaster: %"INT32" urls from %s Not found in %s",
	    numUrlsNotFound,domain1,domain2);
	if(m_justDisplay){
		m_launched--;
		// Free stateBD
		freeStateBD(st);
	}
	return;
}

// This is not a generic function as yet. Gigablast stores the link in tag
// <span class="url"> and google stores it in tag <font color=#008000>. Takes
// the content to search for links, the array in which to store the links and
// the length of the array as arguments.Returns number of links it found in
// the page. This function is not being used as yet as Xml and Links are used
#if 0
int32_t Blaster::getSearchLinks(char *content,
				 int32_t contentLen,
				 char *links,
				 int32_t numLinks){
	char *p=content;
	char *pend=content+contentLen;
	char *p2;
	int32_t linksFound=0;

	//considering code given is raw=1
	/*	while (p<pend){
		if (p=strstr(p,"http://"))
			p2=strstr(p,"\n");
		else break;
		int32_t length=p2-p;
		if (length>=MAX_URL_LEN) length=255;
		strncpy(links+linksFound*MAX_URL_LEN,p,length);
		links[linksFound*MAX_URL_LEN+length]='\0';
		log(LOG_WARN,"blaster: The url is=%s",
		    links+linksFound*MAX_URL_LEN);
		linksFound++;
		p+=7;
	}
	return linksFound;*/

	// Deciding if it is gigablast 1 or google 0 or else 2
	int32_t isGB;
	if (contentLen<19) {
		log(LOG_WARN,"blaster: Contentlen is less");
		return 0;
	}
	if (strstr(content,"<span class=\"url\">"))
		isGB=1;
	else isGB=0;
	p=content;
	if (isGB){
		while (p && p<pend && linksFound<numLinks){
			
			p=strstr(p,"<span class=\"url\">");
			if (!p) break;
			p2=strstr(p,"</span>");
			if (!p2) break;
			
			//point to the url
			p+=18;
			//Check if it is in bounds. Also need to put '\0' at
			// the end.
			int32_t length=p2-p;
			if (length>=MAX_URL_LEN) length=MAX_URL_LEN-1;
			//Copy into the links buffer
			strncpy(links+linksFound*MAX_URL_LEN,p,length);
			links[linksFound*MAX_URL_LEN+length]='\0';
			log(LOG_WARN,"blaster:the url is=%s",
			    links+linksFound*MAX_URL_LEN);
			//advance p2 too
			p2+=7;
			linksFound++;
		} 
	}
	else{
		while (p && p<pend && linksFound<numLinks){
			p=strstr(p,"<font color=#008000>");
			if(!p) break;
			p2=strstr(p,"</font>");
			if (!p2) break;
			
			//point to the url
			p+=20;
			//Check if it is in bounds. Also need to put '\0' at
			// the end.
			int32_t length=p2-p;
			if (length>=MAX_URL_LEN) length=255;
			//Copy into the links buffer
			strncpy(links+linksFound*MAX_URL_LEN,p,length);
			links[linksFound*MAX_URL_LEN+length]='\0';
			log(LOG_WARN,"blaster:the url is=%s",
			    links+linksFound*MAX_URL_LEN);
			//advance p2 too
			p2+=7;
			linksFound++;
		}
	}
	return linksFound;
}
#endif

void gotDocWrapper3 ( void *state , TcpSocket *s ) {
	g_blaster.gotDoc3(state,s);
}

void Blaster::gotDoc3 ( void *state, TcpSocket *s){
	StateBD2 *st2=(StateBD2 *)state;
	StateBD *st=st2->m_st;
	if (!s) {
		log (LOG_WARN,"blaster: Got a null s in gotDoc3."
		     "Happened because ip could not be found");
		st->m_numUrlDocsReceived++;
		//Free StateBD2
		mdelete(st2,sizeof(StateBD2),"Blaster4");
		if (st->m_numUrlDocsReceived==st->m_numUrlDocsSent){
			m_launched--;
			// Free stateBD
			freeStateBD(st);
		}
		return;
	}
	// bail if got cut off
	if ( s->m_readOffset == 0 ) {
		log("blasterDiff : lost the Request in gotDoc3");
		st->m_numUrlDocsReceived++;
		//Free StateBD2
		mdelete(st2,sizeof(StateBD2),"Blaster4");
		if (st->m_numUrlDocsReceived==st->m_numUrlDocsSent){
			m_launched--;
			// Free stateBD
			freeStateBD(st);
		}
		return;
	}
	char *reply = s->m_readBuf ;
	int32_t  size  = s->m_readOffset;
	HttpMime mime;
	mime.set(reply,size,NULL);

	int32_t httpStatus=mime.getHttpStatus();
	if(httpStatus==404){
		if (m_verbose)
			log(LOG_WARN,"blaster: The page was not found - 404");
		st->m_numUrlDocsReceived++;
	}
	// If the url is a redirect check if it is still http (might have
	// become https or something else, in which case we aren't going to
	// follow it
	else if (httpStatus>=300){
		Url *u=mime.getLocationUrl();

		//If max number of redirects done, bail
		if(!st2->m_numRedirects--){
			log(LOG_WARN,"blaster: Max number of redirects "
			    "reached.");
			st->m_numUrlDocsReceived++;
		}
		//check if it is still http (might have become https or
		// something else, in which case we aren't going to follow it
		else if (!u->isHttp()){
			log(LOG_WARN,"blaster: Redirection not for an http "
			    "page for url %s",u->getUrl());
			st->m_numUrlDocsReceived++;
		}
		// sometimes idiots don't supply us with a Location: mime
		else if ( u->getUrlLen() == 0 ) {
			log(LOG_WARN,"blaster: Redirect url is of 0 length");
			st->m_numUrlDocsReceived++;
		}
		else{
			// I'm not checking as yet if the redirect url is the
			// same as the earlier url, as I've set the max number
			// of redirs to 6 Now lets get the redirect url. Do not
			// increase the numDocsReceived because this wrapper
			// will be called back  for the page
			if (m_verbose)
				log(LOG_WARN,"blaster: Downloading redirect"
				    " %s",u->getUrl());
			//Changing the url to the new place
			//st2->m_url.set(u,false);
			st2->m_url = u->getUrl();
			bool status = g_httpServer.getDoc (st2->m_url, // url
							    0,//ip
							    0 ,  // offset
							    -1 ,  // size
							    0 ,
							    st2 ,  // state
							    gotDocWrapper3,
							    60*1000, // timeout
							    0, // proxy ip
							    0, // proxy port
						    30*1024*1024, //maxLen
							    30*1024*1024);
			// If not blocked, there is an error.
			if (status ) 
				st->m_numUrlDocsReceived++;
		}
	}
	else if(httpStatus<200){
		log(LOG_WARN,"blaster: Bad HTTP status %"INT32"",httpStatus);
		st->m_numUrlDocsReceived++;
	}
	else{
		// This means the page is still there, somewhere. Status must 
		// be 200 So find it on server2. This server is assumed to be
		// running an instance of gb, so it shall be given the query in
		// the format 'xxxxx.com/search?q=url%3Ayyyy&code=gbmonitor. 
		// Then check if we have the exact page in the search results 
		// that have come back. So now the problem is that we do
		// not know which url has been got. So I get the location
		// url from mime.
		// The site name is in st->m_u2.getSite()
		// But copy it because it is not nulled.
		char tmp[1024];
		//char site[1024];//how long could a site be?
		int32_t siteLen = 0;
		char *site    = getHostFast(st->m_u2,&siteLen);
		char c = site[siteLen];
		site[siteLen] = 0;
		//strncpy(site,st->m_u2.getSite(),
		//	st->m_u2.getSiteLen());
		//site[st->m_u2.getSiteLen()]='\0';
		sprintf(tmp,"%ssearch?"
			"code=gbmonitor&"
			"q=url%%3A%s",site,st2->m_url);
		site[siteLen] = c;
		if (m_verbose)
			log(LOG_WARN,"blaster: Checking %s",tmp);
		//Url u;
		//u.set(tmp,gbstrlen(tmp));
		//Now get the doc
		bool status = g_httpServer.getDoc ( tmp,//&u,
						    0,//ip
						    0,  // offset
						    -1 ,  // size
						    0 ,
						    st , // state
						    gotDocWrapper4,
						    60*1000, // timeout
						    0,//atoip("66.154.102.20",13),//proxy ip
						    0,//3128,//proxy port
						    30*1024*1024,
						    30*1024*1024);
		// continue if it blocked
		// If not blocked, there is an error. Since we are
		// getting the doc from a gigablast server, report it
		if (status ){
			st->m_numUrlDocsReceived++;
			log(LOG_WARN,"blaster: could not get back"
				    "%s from server in gotDoc3",tmp);
		}
	}
	// If we reached here, that means all the url redirects have been 
	// finished, and there is no need for st2. Free it
	mdelete(st2,sizeof(StateBD2),"Blaster4");


	if (st->m_numUrlDocsReceived==st->m_numUrlDocsSent){
		m_launched--;
		// Free stateBD
		freeStateBD(st);
	}
	return;
}

void gotDocWrapper4 ( void *state , TcpSocket *s ) {
	g_blaster.gotDoc4(state,s);
}

void Blaster::gotDoc4 ( void *state, TcpSocket *s){
	StateBD *st=(StateBD *)state;
	st->m_numUrlDocsReceived++;
	if (!s) {
		//Shouldn't happen, but still putting a checkpoint
		log (LOG_WARN,"blaster: Got a null s in gotDoc4."
		     "Happened because ip could not be found for gigablast"
		     "server");
		if (st->m_numUrlDocsReceived==st->m_numUrlDocsSent){
			m_launched--;
			// Free stateBD
			freeStateBD(st);
		}
		return;
	}
	// bail if got cut off
	if ( s->m_readOffset == 0 ) {
		log("blasterDiff : lost the Request in gotDoc4");
		if (st->m_numUrlDocsReceived==st->m_numUrlDocsSent){
			m_launched--;
			freeStateBD(st);
		}
		return;
	}
	char *reply = s->m_readBuf ;
	int32_t  size  = s->m_readOffset;
	HttpMime mime;
	mime.set ( reply , size , NULL );
	char *content    = reply + mime.getMimeLen();
	int32_t  contentLen = size  - mime.getMimeLen();

	//int16_t csEnum = get_iana_charset(mime.getCharset(), 
	//				mime.getCharsetLen());
	/*	if (csEnum == csUnknown)
		log(LOG_DEBUG, "blaster: Unknown charset : %s", mime.getCharset());*/
	
	Xml xml;
	if (!xml.set(
		     content, 
		     contentLen,
		     false,
		     0,
		     false,
		     TITLEREC_CURRENT_VERSION,
		     true, // setparents
		     0, // niceness
		     CT_XML )){
		log(LOG_WARN,"blaster: Couldn't set XML Class in gotDoc4");
	}
	Links links;
	Url *url=mime.getLocationUrl();
	if (!links.set(0,//siterec xml
		       &xml,
		       url,
		       false,
		       NULL,
		       TITLEREC_CURRENT_VERSION,
		       0,
		       false,
		       NULL)){
		log(LOG_WARN, "blaster: Coudn't set Links class in gotDoc4");
	}
	for (int32_t i=0;i<links.getNumLinks();i++){
		char *ss=links.getLink(i);
		char *p;
		// This page *should* always be a gigablast page. So not adding
		// checks for msn or yahoo or google page.
		p=strstr(ss,"google.");
		if(p) continue;
		p=strstr(ss,"cache:");  //googles cache page
		if(p) continue;
		p= strstr(ss,"gigablast.");
		if(p) continue;
		p= strstr(ss,"web.archive.org");//older copies on gigablast
		if(p) continue;
		p= strstr(ss,"search.yahoo.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"search.msn.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"s.teoma.com");//from gigablast search
		if(p) continue;
		p= strstr(ss,"search.dmoz.org");//from gigablast search
		if(p) continue;
		p= strstr(ss,"www.answers.com");//from gigablast search
		if(p) continue;
       		if (m_verbose)
			log(LOG_WARN,"blaster: Link Present on server2=%s",ss);
	}
	
	// So if one of the links that is returned is the exact url,
	// then we know that the url is present.So get the url from the
	// mime, search for it in the links that are returned.
	char tmp[1024];
	char *sendBuf=s->m_sendBuf;
	char *p1,*p2;

	// First get the Host, which is the domain. Since socket s is going to
	// be useless after this function, changing m_sendBuf instead of using 
	// more space
	p1=strstr(sendBuf,"%3A");
	if(p1){
		p1+=3;
		p2=strstr(p1," HTTP");
		if (p2){
			//Since I do not care about the sendbuf anymore
			*p2='\0';
		}
	}
	if (!p1 || !p2){
		log(LOG_WARN,"blasterdiff: Could not find search link"
		    "from m_sendBuf in gotdoc4");
	}
	else{
		sprintf(tmp,"%s",p1);
		//log(LOG_WARN,"blaster: tmp in gotDoc4 = %s",tmp);
		bool isFound=false;
		// So now we search for tmp in the links
		for (int32_t i=0;i<links.getNumLinks();i++){
			if(strstr(links.getLink(i),tmp) && 
			   links.getLinkLen(i)==(int)gbstrlen(tmp)){
				isFound=true;
				log(LOG_WARN,"blaster: %s in results1 but not"
				    " in results2 for query %s but does exist"
				    " in server2",tmp,st->m_u1);//->getQuery()
			}
		}
		if (!isFound)
			log(LOG_WARN,"blaster: %s in results1 but not"
			    " in results2 for query %s and does NOT exist"
			    " in server2",tmp,st->m_u1); // ->getQuery()
	}
	

      	if (st->m_numUrlDocsReceived==st->m_numUrlDocsSent){
		m_launched--;
		// Free stateBD
		freeStateBD(st);
	}
	return;
}



void Blaster::freeStateBD(StateBD *st){
	// Free stateBD's buf
	if (!st) return;
	if (st->m_buf1)
	        mfree(st->m_buf1,st->m_buf1MaxLen,"Blaster5");
	mdelete(st,sizeof(StateBD),"Blaster3");
}
