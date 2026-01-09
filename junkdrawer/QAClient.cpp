#include "gb-include.h"

#include "QAClient.h"
#include "Log.h"
#include "HttpServer.h"
#include "Process.h"
#include "Diff.h"

#define MAX_QA_SOCKETS 300
#define MAX_TEST_URLS 2000

static void doQA ( int fd , void *state ) ;
static void nextTestWrapper(void *state);

static char *s_parserUrls[] = 
{"http://reddit.com/",
 "http://www.searchengineshowdown.com/cse/search-state-libraries/"
};

static char *s_queries[] = 
{"q=the&n=10",
 "q=search&n=100"
};

QAClient::QAClient(){
	m_numTests = 0;
	m_curTest  = NULL;
	m_pbuf     = NULL;
	m_pbufPtr  = NULL;
	m_qbuf     = NULL;
	m_qbufPtr  = NULL;
}
QAClient::~QAClient(){
	if (m_pbuf){
		mfree(m_pbuf, m_pbufSize, "qaparse");
		m_pbuf = NULL;
	}
	if (m_pbufPtr){
		mfree(m_pbufPtr, m_pbufPtrSize, "qaparse");
		m_pbufPtr = NULL;
	}
	if (m_qbuf){
		mfree(m_qbuf, m_qbufSize, "qaquery");
		m_qbuf = NULL;
	}
	if (m_qbufPtr){
		mfree(m_qbufPtr, m_qbufPtrSize, "qaquery");
		m_qbufPtr = NULL;
	}
}
bool QAClient::init(char *s1, char * s2, char *pfile, char *qfile){

	// get host
	Host *h = g_hostdb.getHost ( 0 );
	if ( ! h ) return log("build: inject: Hostid %i is invalid.",
			      0)-1;

	g_conf.m_save = false;
	g_conf.m_readOnlyMode = true;
	g_conf.m_useThreads = false;
	g_conf.m_useQuickpoll = false;
	// download 1 page from each server simultaneously
	//g_conf.m_httpMaxDownloadSockets = 2; 
	g_conf.m_httpMaxSockets = 2;
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." ); return 1; }
	// . hashinit() calls srand() w/ a fixed number
	// . let's mix it up again
	srand ( time(NULL) );
	// set up the loop
	if ( ! g_loop.init() ) return log("build: inject: Loop init "
					  "failed.")-1;
	//g_process.init();

	//if ( ! g_dns.init( h->m_dnsClientPort ) ) {
	//	log("db: Dns distributed client init failed." ); return 1; }
	// set up http server for client only
	if ( ! g_httpServer.init( 0, 0, NULL))
		return log("db: HttpServer init failed.");
	

	// Set up server urls
	if (s1) m_server1.set(s1, gbstrlen(s1), false);
	if (s2) m_server2.set(s2, gbstrlen(s2), false);
	if (!s1){
		// load servers from hosts file
		char buf[MAX_URL_LEN];
		snprintf(buf, MAX_URL_LEN, "http://%s:%hd/", 
			 iptoa(h->m_ip), h->m_httpPort);
		m_server1.set(buf, gbstrlen(buf), false);
		// tmp server: httpPort + 1
		snprintf(buf, MAX_URL_LEN, "http://%s:%hd/", 
			 iptoa(h->m_ip), h->m_httpPort+1);
		m_server2.set(buf, gbstrlen(buf), false);
	}


	// Initialize tests
	m_curTest = NULL;
	m_nextTest = 0;

	if (!pfile){
		m_parserUrls = s_parserUrls;
		m_numParserUrls = sizeof(s_parserUrls)/sizeof(char*);
	}
	else {
		// load urls from file, and put them in an allocated buffer
		int fd = open(pfile, O_RDONLY);
		if (fd < 0 ) {
			fprintf(stderr, "error opening %s\n", pfile);
			return false;
		}
		int32_t len =  lseek(fd, 0, SEEK_END);
		if (len < 0){
			fprintf(stderr, "error seeking %s\n", pfile);
			close(fd);
			return false;
		}
		char *buf = (char*)mmalloc(len+1, "qaparse");
		if (!buf){
			fprintf(stderr, "can't alloc %"INT32" bytes for %s\n",
				len+1, pfile);
			close(fd);
			return false;
		}
		buf[len] = '\0';
		lseek(fd, 0, SEEK_SET);
		int32_t n = read(fd, buf, len);
		if (n && n != len){
			fprintf(stderr, "error reading %s: "
				"expected %"INT32" bytes got %"INT32".\n"
				"(errno=%s)\n",
				pfile, len, n, strerror(errno));
			return false;
		}

		// allocate a list of pointers to each url in the file.
		int32_t size = MAX_TEST_URLS*sizeof(char**);
		char **ptrs = (char**) mmalloc(size, "qaparse");
		if (!buf){
			fprintf(stderr, "can't alloc %"INT32" bytes for %s\n",
				size, pfile);
			close(fd);
			return false;
		}

		// fill the pointer list and null-terminate each url
		int32_t urls = 0;
		for (char *p=buf, *q=buf,*pend=buf+len ; 
		     p<pend && urls < MAX_TEST_URLS; p++){
			if (*p && *p!='\n' && *p!='\r' && *p!='\t' && *p!=' ') 
				continue;
			*p = '\0';
			// skip whitespace
			if (p-q <= 0) { q=p+1; continue; }
			ptrs[urls++] = q;
			q = p+1;
		}
		m_pbuf          = buf;
		m_pbufSize      = len+1;
		m_pbufPtr       = ptrs;
		m_pbufPtrSize   = size;

		m_parserUrls    = ptrs;
		m_numParserUrls = urls;

		close(fd);
	}
	if (!qfile){
		m_queries = s_queries;
		m_numQueries = sizeof(s_queries)/sizeof(char*);
	}
	else{
		// load urls from file, and put them in an allocated buffer
		int fd = open(qfile, O_RDONLY);
		if (fd < 0 ) {
			fprintf(stderr, "error opening %s\n", qfile);
			return false;
		}
		int32_t len =  lseek(fd, 0, SEEK_END);
		if (len < 0){
			fprintf(stderr, "error seeking %s\n", qfile);
			close(fd);
			return false;
		}
		char *buf = (char*)mmalloc(len+1, "qaquery");
		if (!buf){
			fprintf(stderr, "can't alloc %"INT32" bytes for %s\n",
				len+1, qfile);
			close(fd);
			return false;
		}
		buf[len] = '\0';
		lseek(fd, 0, SEEK_SET);
		int32_t n = read(fd, buf, len);
		if (n && n != len){
			fprintf(stderr, "error reading %s: "
				"expected %"INT32" bytes got %"INT32".\n"
				"(errno=%s)\n",
				pfile, len, n, strerror(errno));
			return false;
		}

		// allocate a list of pointers to each url in the file.
		int32_t size = MAX_TEST_URLS*sizeof(char**);
		char **ptrs = (char**) mmalloc(size, "qaquery");
		if (!buf){
			fprintf(stderr, "can't alloc %"INT32" bytes for %s\n",
				size, qfile);
			close(fd);
			return false;
		}

		// fill the pointer list and null-terminate each url
		int32_t urls = 0;
		for (char *p=buf, *q=buf,*pend=buf+len ; 
		     p<pend && urls < MAX_TEST_URLS; p++){
			if (*p && *p!='\n' && *p!='\r' && *p!='\t' && *p!=' ') 
				continue;
			*p = '\0';
			// skip whitespace
			if (p-q <= 0) { q=p+1; continue; }
			ptrs[urls++] = q;
			q = p+1;
		}
		m_qbuf         = buf;
		m_qbufSize     = len+1;
		m_qbufPtr      = ptrs;
		m_qbufPtrSize  = size;

		m_queries      = ptrs;
		m_numQueries   = urls;
		
		close(fd);

	}
	m_numTests = m_numParserUrls + m_numQueries;

// 	int32_t numTests = 2;
// 	for (int32_t i=0;i<numTests && i < MAX_QA_TESTS ; i++){
// 		char u1[MAX_URL_LEN];
// 		snprintf(u1,MAX_URL_LEN, 
// 			 "%smaster/parser?username=qa&pwd=&c=main&cast=0&u=%s",
// 			 m_server1.getUrl(), s_testUrls[i]);
// 		u1[MAX_URL_LEN-1] = '\0';
// 		char u2[MAX_URL_LEN];
// 		snprintf(u2, MAX_URL_LEN,
// 			 "%smaster/parser?username=qa&pwd=&c=main&cast=0&u=%s",
// 			 m_server2.getUrl(), s_testUrls[i]);
// 		u2[MAX_URL_LEN-1] = '\0';
// 		m_tests[i].set(u1,u2, this, nextTestWrapper);
// 		m_numTests++;
// 	}
	return true;
}

bool QAClient::runTests(){
	// register sleep callback to get started
	if ( ! g_loop.registerSleepCallback(1, this, doQA) )
		return log("build: inject: Loop init failed.")-1;
	// run the loop
	if ( ! g_loop.runLoop() ) return log("build: inject: Loop "
					     "run failed.")-1;
	return true;
}

static void doQA( int fd , void *state){
	g_loop.unregisterSleepCallback( state, doQA);
	log(LOG_INIT, "qa: Starting tests");
	QAClient *qa = (QAClient*)state;
	qa->runNextTest();
}
void nextTestWrapper(void *state){
	QAClient *qa = (QAClient*)state;
	qa->runNextTest();
}



void QAClient::runNextTest(){
	// Free last test
	if (m_curTest){
		delete m_curTest;
		mdelete(m_curTest, m_curTestSize, "qaclient");
		m_curTest = NULL;
		m_curTestSize = 0;
	}

	if (m_nextTest >= m_numTests) {
		log(LOG_INIT,"qa: Done with tests");
		exit(0);
	}

	// Moved test initialization here since we don't need them all
	// in memory at once
	int32_t i = m_nextTest;

	// PageParser tests
	if (i < m_numParserUrls){
		// encode the url for including as a parm
 		char testUrl[MAX_URL_LEN];
		urlEncode(testUrl,MAX_URL_LEN,
			  m_parserUrls[i],gbstrlen(m_parserUrls[i]));
 		testUrl[MAX_URL_LEN-1] = '\0';

		// construct the request for server 1
 		char u1[MAX_URL_LEN];
 		snprintf(u1,MAX_URL_LEN, 
 			 "%smaster/parser?username=qa&old=1&c=main&cast=0&u=%s",
 			 m_server1.getUrl(), testUrl);
 		u1[MAX_URL_LEN-1] = '\0';

		// construct the request for server 2
 		char u2[MAX_URL_LEN];
 		snprintf(u2, MAX_URL_LEN,
 			 "%smaster/parser?username=qa&old=1&c=main&cast=0&u=%s",
 			 m_server2.getUrl(), testUrl);
 		u2[MAX_URL_LEN-1] = '\0';
		
		QADiffTest *t;
		try {
			t = new QADiffTest();
		}
		catch(...) {
			log("qa: Unable to allocate qa test!"); 
			exit(0);
		}
		m_curTestSize = sizeof(*t);
		mnew(t, m_curTestSize, "qaclient");

		char desc[256];
		sprintf(desc, "parser_%"INT32"", i);
		t->set(desc,u1,u2, this, nextTestWrapper);
		m_curTest = t;
	}
	i -= m_numParserUrls;
	// query diff tests
	if (i>=0 && i < m_numQueries){
		
 		char u1[MAX_URL_LEN];
 		snprintf(u1,MAX_URL_LEN, 
 			 "%ssearch?username=qa&pwd=&c=main&raw=9&%s",
 			 m_server1.getUrl(), m_queries[i]);
 		u1[MAX_URL_LEN-1] = '\0';
 		char u2[MAX_URL_LEN];
 		snprintf(u2, MAX_URL_LEN,
 			 "%ssearch?username=qa&pwd=&c=main&raw=9&%s",
 			 m_server2.getUrl(), m_queries[i]);
 		u2[MAX_URL_LEN-1] = '\0';
		
		QADiffTest *t;
		try {
			t = new QADiffTest();
		}
		catch(...) {
			log("qa: Unable to allocate qa test!"); 
			exit(0);
		}
		m_curTestSize = sizeof(*t);
		mnew(t, m_curTestSize, "qaclient");
		char desc[256];
		sprintf(desc, "query_%"INT32"", i);
		t->set(desc,u1,u2, this, nextTestWrapper);
		m_curTest = t;		
	}
	m_nextTest++;

	m_curTest->startTest();
}

void QATest::reset(){

}
QATest::~QATest(){
	reset();
}
//QADiffTest::~QADiffTest(){
//	reset();
//}

bool QADiffTest::set(char *desc,
		     char *u1, 
		     char *u2, 
		     void *state, 
		     qatest_callback_t callback){
	reset();

	m_doc1.m_url.set(u1, gbstrlen(u1), false);
	m_doc2.m_url.set(u2, gbstrlen(u2), false);
	m_state = state;
	m_callback = callback;
	if (desc) strncpy(m_desc, desc, 255);
	m_desc[255] = '\0';
	return true;
}
static void gotDiffTestDocWrapper ( void *state , HttpDoc *doc) ;
void QADiffTest::startTest(){
	log(LOG_INIT, "qa: starting test: %s", m_desc);
	m_doc1.get(this, gotDiffTestDocWrapper);
	m_doc2.get(this, gotDiffTestDocWrapper);
}

void gotDiffTestDocWrapper ( void *state , HttpDoc *doc) {
	QADiffTest *t = (QADiffTest*)state;
	if (t->m_doc1.m_done && t->m_doc2.m_done){
		t->processResults();
	}
}


// encapsulate doc-fetching
HttpDoc::HttpDoc(){
	m_ownBuf = false;
	m_buf = NULL;
}
HttpDoc::~HttpDoc(){
	reset();
}
void HttpDoc::reset() {
	m_url.reset();
	m_done       = false;
	m_errno      = 0;
	m_startTime  = 0;
	m_elapsed    = 0;
	m_content    = NULL;
	m_contentLen = 0;
	m_httpStatus = 0;

	if (m_buf && m_ownBuf){
		mfree(m_buf, m_bufSize, "httpdoc");
	}
	m_buf = NULL;
	m_bufSize = 0;
	m_ownBuf = false;
	m_callback = NULL;
	m_state = NULL;
};

static void gotHttpDoc ( void *state , TcpSocket *ts ) ;

void HttpDoc::get(void *state, httpdoc_callback_t callback){
	m_callback = callback;
	m_state    = state;
	//log(LOG_INIT, "qa: HttpDoc: fetching %s", m_url.getUrl());
	m_startTime = gettimeofdayInMilliseconds();
	if ( g_httpServer.getDoc ( &m_url,
				   0,
				   -1,
				   0,
				   this,
				   gotHttpDoc,
				   30*1000,
				   0, // proxyip 
				   0, // proxyport
				   10000000, // r->m_maxTextDocLen ,
				   10000000, // r->m_maxOtherDocLen ,
				   NULL ,
				   true ) ) {
		// on error g_errno should be set, call this now
		//if (callback) callback ( this , NULL );
		gotHttpDoc(this, NULL);
	}
}


// Fill in HttpDoc data structure with contents from server
void gotHttpDoc ( void *state , TcpSocket *ts ) {
	HttpDoc *doc = (HttpDoc*) state;
	doc->m_done = true;
	int64_t now = gettimeofdayInMilliseconds();
	doc->m_elapsed = now - doc->m_startTime;

	if (g_errno){
		doc->m_errno = g_errno;
		g_errno = 0;
		if (doc->m_callback) doc->m_callback(doc->m_state, doc);
		return;
	}
	char *reply     = ts->m_readBuf;
	int32_t  replyLen  = ts->m_readOffset;
	//int32_t  replySize = ts->m_readBufSize;

	HttpMime mime;
	Url url;
	mime.set ( reply , replyLen , &url ) ;

	int32_t  mimeLen    = mime.getMimeLen();
	doc->m_content       = reply    + mimeLen;
	doc->m_contentLen    = replyLen - mimeLen;
	doc->m_httpStatus = mime.getHttpStatus();
	
	doc->m_buf = ts->m_readBuf;
	doc->m_bufSize = ts->m_readBufSize;
	doc->m_ownBuf = true;

	char *http_cs = mime.getCharset();
	int32_t cslen = mime.getCharsetLen();
	//logf(LOG_INFO, "qa: http: %s",http_cs);
	doc->m_charset = get_iana_charset(http_cs, cslen);


	// . nullify the buf in the socket so it doesn't get freed,
	ts->m_readBuf = NULL;
	if (doc->m_callback) doc->m_callback(doc->m_state, doc);
}

void QADiffTest::processResults(){
	//log(LOG_INIT,"qa: QADiffTest: Processing");
	printf("vvv BEGIN TEST %s vvv\n", m_desc);
	printf("doc1=%s\ndoc2=%s\n",
	       m_doc1.m_url.getUrl(), 
	       m_doc2.m_url.getUrl());
	printf("status=(%d,%d) elapsed=(%lldms,%lldms) size=(%"INT32",%"INT32")\n",
	       m_doc1.m_httpStatus, m_doc2.m_httpStatus,
	       m_doc1.m_elapsed, m_doc2.m_elapsed, 
	       m_doc1.m_contentLen, m_doc2.m_contentLen);

	if (m_doc1.m_httpStatus == m_doc2.m_httpStatus){
		printf("\nvvv BEGIN DIFF %s vvv\n", m_desc);
		fflush(stdout);
		//fileDiff();
		xmlDiff();
		printf("^^^ END DIFF ^^^\n\n");
	}
	else{
		printf("***ERROR: http status does not match!\n");
	}
	printf("^^^ END TEST %s ^^^\n\n", m_desc);
	fflush(stdout);	
	if (m_callback) m_callback(m_state);
}

// diff the contents of m_doc1 and m_doc2 with unix diff
void QADiffTest::fileDiff() {
	// write page contents to files for diff
	ssize_t nw;
	int fd;
	char filename1[256];
	char filename2[256];
	int pid = getpid();
	char cmd[550];
	int status = 0;
	// Only diff if we actually got content from both servers
	if ((m_doc1.m_httpStatus != 200) ||
	    (m_doc2.m_httpStatus != 200)) {
		goto abort;
	}
	
	//sprintf(filename1, "tmp/qadiff.%d.1", pid);
	//sprintf(filename2, "tmp/qadiff.%d.2", pid);
	sprintf(filename1, "/tmp/qa.%s.%d.1", m_desc,pid);
	sprintf(filename2, "/tmp/qa.%s.%d.2", m_desc,pid);
	fd = open(filename1, O_RDWR|O_CREAT|O_TRUNC, 00666);
	if (fd < 0) {
		log(LOG_WARN, 
		    "qa: QADiffTest: unable to open \"%s\" for writing: %s",
		    filename1, strerror(errno));
		goto abort;
	}
	nw = write(fd, m_doc1.m_content, m_doc1.m_contentLen);
	if (nw < 0) {
		log(LOG_WARN, 
		    "qa: QADiffTest: unable write to \"%s\": %s",
		    filename1, strerror(errno));
		close(fd);
		goto abort;
	}
	write(fd, "\n", 1);
	close(fd);

	fd = open(filename2, O_RDWR|O_CREAT|O_TRUNC, 00666);
	if (fd < 0) {
		log(LOG_WARN, 
		    "qa: QADiffTest: unable to open \"%s\" for writing: %s",
		    filename2, strerror(errno));
		goto abort;
	}
	nw = write(fd, m_doc2.m_content, m_doc2.m_contentLen);
	if (nw < 0) {
		log(LOG_WARN, 
		    "qa: QADiffTest: unable write to \"%s\": %s",
		    filename2, strerror(errno));
		close(fd);
		goto abort;
	}
	write(fd, "\n", 1);
	close(fd);

	sprintf(cmd, "diff '%s' '%s' 2>&1", filename1, filename2);
	status = system(cmd);

	if (status == 0) {
		unlink(filename1);
		unlink(filename2);
	}
	return;
 abort:
	unlink(filename1);
	unlink(filename2);
	return;
}



// diff the contents of m_doc1 and m_doc2 
void QADiffTest::xmlDiff() {
	Xml xml1;
	Xml xml2;
	
	xml1.set(m_doc1.m_charset,
		 m_doc1.m_content,
		 m_doc1.m_contentLen,
		 false, 0, false,
		 TITLEREC_CURRENT_VERSION);
	xml2.set(m_doc2.m_charset,
		 m_doc2.m_content,
		 m_doc2.m_contentLen,
		 false, 0, false,
		 TITLEREC_CURRENT_VERSION);

	log(LOG_INIT, "qa: doc1: charset=%d len=%"INT32" "
	    "xml1: charset=%d len=%"INT32" nodes=%"INT32"\n",
	       m_doc1.m_charset, m_doc1.m_contentLen,
	       xml1.getCharset(),xml1.getContentLen() , xml1.getNumNodes() );
	log(LOG_INIT, "qa: doc2: charset=%d len=%"INT32" "
	    "xml2: charset=%d len=%"INT32" nodes=%"INT32"\n",
	       m_doc2.m_charset, m_doc2.m_contentLen,
	       xml2.getCharset(),xml2.getContentLen(), xml2.getNumNodes() );
	DiffOpt opt;
	opt.m_debug = 0;
	opt.m_context = 3;
	printXmlDiff(&xml1,&xml2, &opt);

#if 0
	
	int32_t seq1  [4096];
	int32_t seq2  [4096];
	int32_t seqLen[4096];

	
	int32_t lcsLen = longestCommonSubsequence(seq1, seq2, seqLen,4096, 
					       &xml1, &xml2);
	printf("lcs length: %"INT32"\n", lcsLen);
	lcsLen = lcsXml(seq1, seq2, seqLen,4096, 
			&xml1, &xml2, opt);
	printf("lcs length: %"INT32"\n", lcsLen);
	
	int32_t start = -1;
	printf("[ ");
	for (int32_t i=0;i<lcsLen;i++){
		int32_t n1 = lcs1[i];
		int32_t n2 = lcs2[i];
		printf("(%"INT32", %"INT32")", n1, n2);
		if (i < lcsLen-1) printf(", ");
		continue;
		if (start < 0) {
			printf("%"INT32"", n);
			start = n;
			continue;
		}
		// only print start and end of ranges
		if (start == n-1){
			start = n;
			// more in the sequence
			if (i < lcsLen-1 && lcs1[i+1] == n+1 ) continue;
			// end of sequence
			printf("-%"INT32"", lcs1[i]);
			continue;
		}

		// disconnected node
		printf(", %"INT32"", n);
		start = n;
	}
	printf(" ]\n"); 
	
	// Print Diff
	int32_t n1 = xml1.getNumNodes();
	int32_t n2 = xml2.getNumNodes();
	int32_t iLcs = -1;
	int32_t i = 0;
	int32_t j = 0;
	
	for (iLcs=0;i<lcsLen;iLcs++){
		int32_t a = seq1[iLcs];
		int32_t b = seq2[iLcs];
		int32_t len = seqLen[iLcs];
		// Deletes
		while (i < a){
			printf("- %"INT32"\n", i++);
		}
		// Adds
		while (j < b){
			printf("+ %"INT32"\n", j++);
		}
		// Same
		if (i == a) i+=len;
		if (j == b) j+=len;
	}

	// Deletes
	while (i < n1){
		printf("- %"INT32"\n", i++);
	}
	// Adds
	while (j < n2){
		printf("+ %"INT32"\n", j++);
	}
#endif
}

