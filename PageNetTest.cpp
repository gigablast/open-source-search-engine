#include "gb-include.h"

#include "PageNetTest.h"
//#include "CollectionRec.h"
#include "HttpServer.h"
#include "Mime.h"
#include "sort.h"

PageNetTest g_pageNetTest;


static void netTestDoneWrapper    ( void *state , ThreadEntry *t );
static void *netTestStartWrapper_r( void *state , ThreadEntry *t );
static void gotResultsWrapper     ( void *state, TcpSocket *s );

int switchSort  ( const void *p1, const void *p2 );
int send1Sort   ( const void *p1, const void *p2 );
int receive1Sort( const void *p1, const void *p2 );
int send2Sort   ( const void *p1, const void *p2 );
int receive2Sort( const void *p1, const void *p2 );
int hostSort    ( const void *p1, const void *p2 );
int networkSort ( const void *p1, const void *p2 );
int testId1Sort ( const void *p1, const void *p2 );
int testId2Sort ( const void *p1, const void *p2 );

void controlNetTest( int fd, void *state ) {
	if( !g_pageNetTest.isRunning() && g_pageNetTest.runNetTest() ) {
		g_pageNetTest.reset();
		g_pageNetTest.start();
	}
	else if( g_pageNetTest.isRunning() && !g_pageNetTest.runNetTest() )
		g_pageNetTest.stop();
}


void PageNetTest::destructor() {
	int32_t numHst = g_hostdb.getNumHosts();
	for( int32_t i = 0; i < MAX_TEST_THREADS; i++ ) {
		if( m_hostRates[i] ) mfree( m_hostRates[i],
					    sizeof(uint32_t *)*numHst,
					    "NT-fHostRate" );
		m_hostRates[i] = NULL;
	}
}


bool PageNetTest::init() {
	// ignore for now
	return true;
	m_runNetTest       = false;
	m_running          = false;
	m_sock[0]          = -1;
	m_sock[1]          = -1;
	m_port[0]          = 5066;
	m_port[1]          = 5067;
	m_port[2]	   = 5066;
	m_port[3]	   = 5067;
	m_testDuration     = 10;
	m_testBytes        = 10000000;
	m_numSwitches      = 0;
	m_numHostsOnSwitch = 0;
	m_fullDuplex       = 0;
	m_threadNum	   = 0;

	m_hostId     = g_hostdb.getHostId( g_hostdb.getMyIp(), 
					   g_hostdb.getMyPort() );
	m_switchId   = g_hostdb.getHost( m_hostId )->m_switchId;

	int32_t numHst = g_hostdb.getNumHosts();

	for( int32_t i = 0; i < 4; i++ ) {
		m_hostRates[i]=(uint32_t *)mmalloc(sizeof(uint32_t *)*
							numHst,
							"NT-HostRate" );
		if( !m_hostRates[i] ) {
			g_errno = ENOMEM;
			log( "net: nettest: could not allocate %"INT32" bytes.",
			     sizeof(uint32_t *) * numHst);
			return false;
		}
		memset( m_hostRates[i], 0, numHst * sizeof(uint32_t *) );
	}

	/*
	int32_t switchId = -1;
	for( int32_t i = 0; i < g_hostdb.getNumHosts(); i++ ) {
		Host *h = g_hostdb.getHost( i );

		if( switchId != h.m_switchId ) {
			switchId = h.m_switchId;
			m_numSwitches++;
		}

		if( switchId != m_switchId ) continue;
		if( !m_firstHostOnSwitch ) m_firstHostOnSwitch = h.m_hostId;
		m_lastHostOnSwitch = h.hostId;
		m_numHostsOnSwitch++;
	}

	if( (m_hostId == m_lastHostOnSwitch) && () ) {
		m_testHostId = m_hostId++;
		m_type = TEST_SEND;
	}
	else if( m_hostId == m_firstHostOnSwitch) {
		m_testHostId = m_hostId--;
		m_type = TEST_READ;
	}
	else {
		if( (m_hostId - m_firstHostOnSwitch) % 2 ) {
		}
	}
	*/

	if( m_hostId % 2 ) {
		m_testHostId[0] = m_hostId - 1; m_testHostId[1] = m_hostId - 1;
		m_testHostId[2] = m_hostId + 1; m_testHostId[3] = m_hostId + 1;
		m_type[0]       = TEST_READ   ; m_type[1]       = TEST_SEND   ;
		m_type[2]       = TEST_READ   ; m_type[3]       = TEST_SEND   ;
	}
	else {
		m_testHostId[0] = m_hostId + 1; m_testHostId[1] = m_hostId + 1;
		m_testHostId[2] = m_hostId - 1; m_testHostId[3] = m_hostId - 1;
		m_type[0]       = TEST_SEND   ; m_type[1]       = TEST_READ   ;
		m_type[2]       = TEST_SEND   ; m_type[3]       = TEST_READ   ;
	}

	for( int32_t i = 0; i < MAX_TEST_THREADS; i++ ) {
		if( m_testHostId[i] < 0 ) 
			m_testHostId[i] = g_hostdb.getNumHosts()-1;
		if( m_testHostId[i] >= g_hostdb.getNumHosts() ) 
			m_testHostId[i] = 0;	
		m_testIp[i] = g_hostdb.getHost( m_testHostId[i] )->m_ip;
		if( (uint32_t)m_testIp[i] == g_hostdb.getMyIp() ) 
			log( "net: nettest: cannot test two processes on the "
			     "same IP." );

	}

	char buf [16]; 
	strcpy( buf, iptoa(g_hostdb.getMyIp()) );
	char buf2[16];
	strcpy( buf2, iptoa(m_testIp[0]) );

	log( LOG_DEBUG, "net: nettest: hostId %"INT32" (%s) is on switch %"INT32" and "
	     "will test hostId %"INT32" (%s) as a %s on port %"INT32" first", m_hostId, 
	     buf, m_switchId, (int32_t)m_testHostId, buf2, 
	     (m_type[0] == TEST_SEND)?"sender":"receiver", m_port[0] );

	memset ( m_sdgram , 'X' , NTDGRAM_SIZE );
	memset ( m_calcTable, 0, MAX_TEST_THREADS*AVG_TABLE_SIZE );

	return true;
}

void PageNetTest::reset() {
	m_threadNum = 0;
	m_threadReturned = 0;
	memset ( m_calcTable, 0, MAX_TEST_THREADS*AVG_TABLE_SIZE );
	for( int32_t i = 0; i < 4; i++ )
		memset( m_hostRates[i], 0, 
			g_hostdb.getNumHosts() * sizeof(uint32_t *) );
}


bool PageNetTest::stop() {
	m_running    = false;
	m_runNetTest = false;
	//reset();
	m_numResultsSent = 0;
	m_numResultsRecv  = 0;
	if( m_hostId == 0 ) collectResults();
	return true;
}


bool PageNetTest::start() {
        // MTS: Just in case someone finds a way to start this...
        return false;

	if( !m_runNetTest ) return false;
	
	if( (m_threadNum % 2) == 0 ){
		if ( g_threads.call ( GENERIC_THREAD       ,
				      MAX_NICENESS         ,
				      this                 ,
				      netTestDoneWrapper   ,
				      netTestStartWrapper_r ) ) goto read;
		netTestStart_r( false/*am thread?*/, m_threadNum );
		m_threadNum++;
		goto read2;
	}
	else goto read2;
	
 read:
	log( LOG_DEBUG, "net: nettest: called thread 1" );
	if( !m_fullDuplex ) return false; //Wait until the thread returns
	while((m_threadNum%2) != 1);
 read2:
	if( (m_threadNum % 2) == 1 ) {
		if ( g_threads.call ( GENERIC_THREAD       ,
				      MAX_NICENESS         ,
				      this                 ,
				      netTestDoneWrapper   ,
				      netTestStartWrapper_r ) ) {
			return false;
		}
		netTestStart_r( false/*am thread?*/, m_threadNum );
		m_threadNum++;
	}
	
	log( LOG_DEBUG, "net: nettest: called thread 2" );

	return true;
}


void netTestDoneWrapper( void *state , ThreadEntry *t ) {
	PageNetTest *THIS = (PageNetTest *)state;
	THIS->threadControl();
	return;
}


void *netTestStartWrapper_r( void *state , ThreadEntry *t ) {
	PageNetTest *THIS = (PageNetTest *)state;
	THIS->m_threadNum++;
	THIS->netTestStart_r( true /*am thread?*/, THIS->m_threadNum-1 );
	return NULL;

}


bool PageNetTest::collectResults() {

	CollectionRec *cr = g_collectiondb.getRec ( m_coll );

	if( m_numResultsSent >= g_hostdb.getNumHosts() ) return true;
		
	char temp[64];
	int32_t ip = g_hostdb.getHost( m_numResultsSent )->m_ip;
	int32_t port = g_hostdb.getHost( m_numResultsSent )->m_httpPort;
	//int32_t len = 0;
	sprintf(temp, "http://%s:%"INT32"/get?rnettest=1", iptoa(ip), port);
	log( LOG_DEBUG, "net: nettest: queried results from: %s", temp );
	
	//Url u;
	//u.set( temp, len );
	m_numResultsSent++;

	if ( ! g_httpServer.getDoc ( temp ,// &u                , 
				     0 , // ip
				     0                 , //offset
				     -1                , //size
				     0                 , //modifiedSince
				     this              , //state
				     gotResultsWrapper , //callback
				     30*1000           , //timeout
				     cr->m_proxyIp     , //proxyIp
				     cr->m_proxyPort   , //proxyPort
				     200               , //maxTextLen
				     200               ) ) return false;
	if ( g_errno ) {
		g_errno = 0;
		return gotResults ( NULL );
	}

	return true;
}


void gotResultsWrapper ( void *state, TcpSocket *s ) {
	PageNetTest *THIS = (PageNetTest *)state;
	if( !THIS->gotResults( s ) ) return;
}


bool PageNetTest::gotResults( TcpSocket *s ) {
	char *buf;
	int32_t  bufLen, bufMaxLen;
	HttpMime mime;

	if ( g_errno ) {
		log( "net: nettest: g_errno: %s", mstrerror(g_errno) );
		g_errno = 0;
		return false;
	}
	if ( !s ) return false;


	buf       = s->m_readBuf;
	bufLen    = s->m_readOffset;
	bufMaxLen = s->m_readBufSize;

	char temp[64];
	int32_t len = 0;
	len = sprintf(temp, "http://%s:%i/get?rnettest=1", 
		      iptoa(s->m_ip), s->m_port);
	Url u;
	u.set( temp, len );
	if ( !mime.set ( buf, bufLen, &u ) ) {		
		log( "net: nettest: MIME.set() failed." );
		return false;
	}

	if ( mime.getHttpStatus() != 200 ) {
		log( "net: nettest: MIME.getHttpStatus() failed." );
	        return false;
	}

	int32_t state = 0;
	int32_t hostId = 0;
	int32_t testId = 0;

	if( !bufLen ) log( LOG_INFO, "net: nettest: we got an empty doc." );

	buf += mime.getMimeLen();
	bufLen -= mime.getMimeLen();

	for( int32_t i = 0; i < bufLen; i++ ){		
		if( buf[i] == ' '  ) continue;
		if( buf[i] == '\r' ) continue;
		if( buf[i] == '\n' ) continue;
		if( buf[i] <  '0'  ) continue;

		if( state == 0 ) {
			hostId = atoi(&buf[i]);
			log( LOG_DEBUG, "net: nettest: host id is %"INT32"",
			     hostId);
			state = 1;
		}
		else if( state == 1 ) {
			testId = atoi(&buf[i]);
			log( LOG_DEBUG, "net: nettest: test id is %"INT32"",
			     testId);
			state = 2;
		}
		else if( state == 2 ){
			if( ((testId < hostId) || !hostId) && (testId) ) {
				if( !m_hostRates[0][hostId] )
					m_hostRates[0][hostId] = atoi(&buf[i]);
				else 
					m_hostRates[2][hostId] = atoi(&buf[i]);
			}
			else {
				if( !m_hostRates[2][hostId] )
					m_hostRates[2][hostId] = atoi(&buf[i]);
				else
					m_hostRates[0][hostId] = atoi(&buf[i]);
			}
			state = 3;
			log( LOG_DEBUG, "net: nettest: send rate is %d",
			     atoi(&buf[i]));
		}
		else if( state == 3 ) {
			if( ((testId < hostId) || !hostId) && (testId) ) {
				if( !m_hostRates[1][hostId] )
					m_hostRates[1][hostId] = atoi(&buf[i]);
				else
					m_hostRates[3][hostId] = atoi(&buf[i]);
			}
			else {
				if( !m_hostRates[3][hostId] )
					m_hostRates[3][hostId] = atoi(&buf[i]);
				else
					m_hostRates[1][hostId] = atoi(&buf[i]);				
			}
			state = 0;
			log( LOG_DEBUG, "net: nettest: rcv rate is %d",
			     atoi(&buf[i]));
		}

		while( buf[i+1] >= '0'  ) i++;
	}


	if( m_numResultsSent < g_hostdb.getNumHosts() )
		return collectResults();

	if( ++m_numResultsRecv < m_numResultsSent )
		return false;
	
	return true;
}


void PageNetTest::threadControl() {
	//Increment done thread count
	m_threadReturned++;

	// If num of thread calls is equal to max, then we have
	// completed our task and may exit now.
	if(    m_threadNum >= (MAX_TEST_THREADS) 
	    && m_threadReturned >= (MAX_TEST_THREADS) ) {
		stop();
		return;
	}

	// If the num of threads is greater than
	// the number of threads returned, we wait until it 
	// has returned.
	if( m_threadNum > m_threadReturned ) return;

	// If we have a full duplex test, we need 2 threads outstanding	
	if( m_fullDuplex && (m_threadNum % 2) ) {
		start();
		return;
	}

	start();
}


bool PageNetTest::netTestStart_r( bool amThread, int32_t num ) {
	int64_t endTime   = 0;
	int64_t calcTime  = 0;
	int32_t      count;
	int32_t      index     = 0;

	m_running = true;
	m_startTime = gettimeofdayInMilliseconds();
	endTime = gettimeofdayInMilliseconds();
	
	if( (m_sock[num] = openSock( num, m_type[num], &m_name[num], 
				     m_port[num])) == -1 ) 
		return false;	

	while( (endTime - m_startTime) < (m_testDuration * 1000) ) {
		count = 0;
		calcTime = gettimeofdayInMilliseconds();

		if( m_type[num] == TEST_READ ) 
			count = readSock( m_sock[num] );

		else if( m_type[num] == TEST_SEND )
			count = sendSock( m_sock[num] ); 

		endTime = gettimeofdayInMilliseconds();
		float secs = (endTime - calcTime)/1000.0;
		float mb   = (float)count * 8.0 / (1024.0 * 1024.0);
		float mbps = mb/secs;
		log( LOG_INFO, "net: nettest: took %"INT64" ms to %s %"INT32" bytes at "
		     "%.2f Mbps", endTime - calcTime, 
		     (m_type[num] == TEST_READ)?"receive":"send", count, mbps );
		log( LOG_INFO, "net: nettest: run time %"INT64" s", 
		     (endTime-m_startTime)/1000 );

		m_calcTable[num][index] = (uint32_t)mbps;
		if( ++index >= AVG_TABLE_SIZE ) index = 0;

		if( !m_runNetTest ) break;
	}

	m_sock[num] = closeSock( m_sock[num] );	
	return true;
}


int PageNetTest::openSock( int32_t num, int32_t type, struct sockaddr_in *name, 
			   int32_t port ) {
	// set up our socket
        int sock  = socket ( AF_INET, SOCK_DGRAM , 0 );
        if ( sock < 0 ) {
		log( "net: nettest: socket-%s",strerror(errno) );
		return false;
	}

        // reset it all just to be safe
        bzero((char *)name, sizeof(*name));
        name->sin_family      = AF_INET;
        name->sin_addr.s_addr = 0; /*INADDR_ANY;*/
        name->sin_port        = htons(port);
        // we want to re-use port it if we need to restart
        int options = 1;
        if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR ,
			&options,sizeof(options)) < 0 ) {
		log( "net: nettest: setsockopt-%s", strerror(errno) );
		return -1;
	}
	if( type == TEST_READ ) {
		struct timeval timeo;
		timeo.tv_sec  = 0;
		timeo.tv_usec = 500000;
		if ( setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
				&timeo,sizeof(timeo)) < 0 ) {
			log( "net: nettest: setsockopt-%s", strerror(errno) );
			return -1;
		}
	}
        // bind this name to the socket
        if ( bind ( sock, (struct sockaddr *)name, sizeof(*name)) < 0) {
                close ( sock );
                log( "net: nettest: bind on port %"UINT32": %s", port, 
		     strerror(errno) );
		return -1;
        }

	if( type == TEST_SEND ) {
		m_to.sin_family      = AF_INET;
		m_to.sin_addr.s_addr = m_testIp[num];
		m_to.sin_port        = htons ( port );//2000 ) ; // m_port );
		bzero ( &(m_to.sin_zero) , 8 );
	}

	log( LOG_DEBUG, "net: nettest: open socket for %s on port %"INT32" to %s", 
	     (type == TEST_SEND)?"sending":"receiving", port, 
	     iptoa(m_testIp[num]) );
	return sock;
}


int PageNetTest::closeSock( int sock ) {
	close( sock );
	log( LOG_DEBUG, "net: nettest: socket closed" );
	return -1;
}


int32_t PageNetTest::readSock( int sock ) {
	int n;
	unsigned int fromLen;
	int32_t count = 0;
	// send more than expected to make up for losses

	while( count < m_testBytes ) {
		if( !m_runNetTest ) return count;
		fromLen = sizeof ( struct sockaddr );
		n = recvfrom( sock, m_rdgram, NTDGRAM_SIZE, 0,
			      (sockaddr *)&m_from, &fromLen );

		if     ( n <= 0 ) {log( "net: nettest: recvfrom:%s", 
					strerror(errno) );}
		else              {count += n;}

		if( (gettimeofdayInMilliseconds() - m_startTime) >
		    (m_testDuration * 1000) ) 
			return count;
	}
	
	return count;
}


int32_t PageNetTest::sendSock( int sock ) {
	int n;
	unsigned int toLen;
	int32_t count = 0;	
	// send more than expected to make up for losses
	int32_t nn = m_testBytes * 10;

	toLen = sizeof(struct sockaddr);

	while( count < nn ) {
		if( !m_runNetTest ) return count;
		n = sendto( sock, m_sdgram, NTDGRAM_SIZE, 0, 
			    (struct sockaddr *)&m_to, toLen );
		if ( n != NTDGRAM_SIZE ) log("net: nettest: sendto:%s",
					     strerror(errno));
		else                     count += n;

		if( (gettimeofdayInMilliseconds() - m_startTime) > 
		    (m_testDuration * 1000) ) 
			return count;
	}

	return count;
}


bool PageNetTest::controls( TcpSocket *s, HttpRequest *r ) {
	char buf [ 64*1024 ];
	char *p    = buf;
	char *pend = buf + 64*1024;
	// password, too
	int32_t pwdLen = 0;
	char *pwd = r->getString ( "pwd" , &pwdLen );
	if ( pwdLen > 31 ) pwdLen = 31;
	char pbuf [32];
	if ( pwdLen > 0 ) strncpy ( pbuf , pwd , pwdLen );
	pbuf[pwdLen]='\0';

	int32_t hids[MAX_HOSTS];
	int32_t numHosts = g_hostdb.getNumHosts();

	int32_t len = 0;
	char *coll = r->getString( "c", &len );
	gbmemcpy( m_coll, coll, len );

	//int32_t ntnd      = r->getLong( "ntnd", 0              );
	//int32_t rcv       = r->getLong( "ntrs", 0              );
	m_testBytes    = r->getLong( "ntb" , m_testBytes    );
	m_port[0]      = r->getLong( "ntp1" , m_port[0]     );
	m_port[1]      = r->getLong( "ntp2" , m_port[1]     );
	m_port[2]      = m_port[0];
	m_port[3]      = m_port[1];
	m_testDuration = r->getLong( "ntd" , m_testDuration );
	m_fullDuplex   = r->getLong( "ntfd" , m_fullDuplex  );
	int32_t sort      = r->getLong( "sort", 7 );
	
	m_runNetTest   = r->getLong( "rnt", m_runNetTest );	

	//if( ntnd ) return true;

	p = g_pages.printAdminTop ( p , pend , s , r );

	//-------------------------------------------
	// PageNetTest html controls page goes below.

	//Header
	sprintf( p, "<table width=100%% bgcolor=#%s border=1 cellpadding=4>"
		 "<tr><td bgcolor=#%s colspan=2><center><font size=+1><b>"
		 "Network Test/Discovery Tool</b></font></center></td></tr>",
		 LIGHT_BLUE, DARK_BLUE );
	p += gbstrlen( p );

	//Controls
	char temp[20];
	sprintf( temp, "&ntfd=%d&cast=1", (m_fullDuplex)?0:1 );
	sprintf( p, "<tr><td><span style=\"font-weight:bold;\">Test Duration"
		 "</span><br><font size=-1>The number of seconds each test "
		 "should last. Default: 10</font></td>"
		 "<td><input name=ntd value=%"INT32" type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Port 1"
		 "</span><br><font size=-1>The port number to use for testing."
		 " Default:5066</font></td>"
		 "<td><input name=ntp1 value=%"INT32" type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Port 2"
		 "</span><br><font size=-1>The port number to use for testing."
		 " Default:5067</font></td>"
		 "<td><input name=ntp2 value=%"INT32" type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Bytes"
		 "</span><br><font size=-1>The number of bytes to test the "
		 "transfer rate. Default:10000000</font></td>"
		 "<td><input name=ntb value=%"INT32" type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Full Duplex"
		 "</span><br><font size=-1>Tests full duplex (transmit and "
		 "receive simultaneously) otherwise runs half duplex.  "
		 "Requires threads to be enabled. Default: OFF</font>"
		 "</td><td bgcolor=#%s "
		 "style=\"font-weight:bold;text-align:center;\"><a "
		 "href=\"/admin/nettest?c=%s\">%s</a>"
		 "</td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Run Test"
		 "</span><br><span style=\"float:left;font-size:smaller;\">"
                 "Starts/stops the network test.</span>"
                 "<span style=\"float:right;font-weight:bold;color:red;\">"
                 "DISABLED DUE TO NETWORK SWITCH FAILURES</b></span></td>"
		 "<td bgcolor=#%s "
		 "style=\"font-weight:bold;text-align:center;\">"
		 //"<a href=\"/admin/nettest?c=%s&pwd=&rnt=%d&cast=1\">"
                 "<span style=\"text-decoration:underline;\">%s</span>"
                 //"</a>"
		 "</td></tr></table><center>"
		 "<input name=submit value=Submit type=submit></center>",
		 m_testDuration, m_port[0], m_port[1], m_testBytes, 
		 (m_fullDuplex)?"00ff00":"ff0000", coll,
		 //(g_conf.m_useThreads)?temp:"",
		 (g_conf.m_useThreads)?((m_fullDuplex)?"ON":"OFF"):"THREADS OFF",
		 (m_runNetTest)?"00ff00":"ff0000", 
                 //coll, (m_runNetTest)?0:1, 
		 (m_runNetTest)?"ON":"OFF" );
	p += gbstrlen( p );

	//Results 
	//	only print on Host 0 page
	if( m_hostId ) goto sendPage;

	for( int32_t i = 0; i < numHosts; i++ ) hids[i] = i;

	switch( sort ) {
	case 1: gbsort( hids, numHosts, sizeof(int32_t), switchSort   ); break;
	case 2: gbsort( hids, numHosts, sizeof(int32_t), networkSort  ); break;
	case 3: gbsort( hids, numHosts, sizeof(int32_t), send1Sort    ); break;
	case 4: gbsort( hids, numHosts, sizeof(int32_t), receive1Sort ); break;
	case 5: gbsort( hids, numHosts, sizeof(int32_t), send2Sort    ); break;
	case 6: gbsort( hids, numHosts, sizeof(int32_t), receive2Sort ); break;
	case 7: gbsort( hids, numHosts, sizeof(int32_t), hostSort     ); break;
	case 8: gbsort( hids, numHosts, sizeof(int32_t), testId1Sort  ); break;
	case 9: gbsort( hids, numHosts, sizeof(int32_t), testId2Sort  ); break;
	}

	// width=100%% ??
	sprintf( p, "<br><br>"
		 "<div style=\"font-weight:bold;\">"
		 "Results (in Mbps)</div>"
		 "<table border=1 cellpadding=4 "
		 "style=\"empty-cells: hide;\" bgcolor=#%s>", LIGHT_BLUE );
	p += gbstrlen( p );

	for( int32_t i = 0; i < MAX_TEST_THREADS + 5; i++ ) {
		if     ( i == 1 ) sprintf( p, "<tr><th width=20 bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=1\">Switch Id</a></th>", 
					   DARK_BLUE, coll );
		else if( i == 2 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=2\">Switch Id</a></th>", 
					   DARK_BLUE, coll );
		else if( i == 3 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=8\">Test #1 Host Id</a></th>", 
					   DARK_BLUE, coll );
		else if( i == 4 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=3\">Test #1 Send</a></th>", 
					   DARK_BLUE, coll );
		else if( i == 5 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=4\">Test #1 Recv</a></th>",
 					   DARK_BLUE, coll );
		else if( i == 6 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=9\">Test #2 Host Id</a></th>", 
					   DARK_BLUE, coll );
		else if( i == 7 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=5\">Test #2 Send</a></th>",
 					   DARK_BLUE, coll );
		else if( i == 8 ) sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=6\">Test #2 Recv</a></th>", 
					   DARK_BLUE, coll );
		else              sprintf( p, "<tr><th bgcolor=#%s>"
					   "<a href=\"/admin/nettest?c=%s&"
					   "sort=7\">Host Id</a></th>", 
					   DARK_BLUE, coll );
		p += gbstrlen( p );
		
		for( int32_t j = 0; j < numHosts; j++ ) {
			if     ( i == 1 ) {
				Host *h = g_hostdb.getHost(hids[j]);
				int32_t switchGroup = 0;
				if ( g_hostdb.m_indexSplits > 1 )
					switchGroup = h->m_shardNum %
						g_hostdb.m_indexSplits;
				sprintf( p, "<td>%"INT32"</td>", switchGroup );
			}
			else if( i == 2 ) {
				Host *h = g_hostdb.getHost(hids[j]);
				sprintf( p, "<td>%d</td>", h->m_switchId );
			}
			else if( i == 0 ) {
				sprintf( p, "<th bgcolor=#%s>%"UINT32"</th>",
					 DARK_BLUE, hids[j] );
			}
			else if( i == 3 ) {
				int32_t tid = (hids[j]%2)?(hids[j]+1):(hids[j]-1);
				if( tid < 0         ) tid = numHosts-1;
				if( tid >= numHosts ) tid = 0;
				sprintf( p, "<td>%"UINT32"</td>", tid );
			}
			else if( i > 3 && i < 6 ) {
				sprintf( p, "<td>%"UINT32"</td>",
					 m_hostRates[i-4][hids[j]] );
			}
			else if( i == 6 ) {
				int32_t tid = (hids[j]%2)?(hids[j]-1):(hids[j]+1);
				if( tid < 0         ) tid = numHosts-1;
				if( tid >= numHosts ) tid = 0;
				sprintf( p, "<td>%"UINT32"</td>", tid );
			}
			else if( i > 6 ) {
				sprintf( p, "<td>%"UINT32"</td>",
					 m_hostRates[i-5][hids[j]] );
			}
			p += gbstrlen( p );			
		}
		sprintf( p, "</tr>" );
		p += gbstrlen( p );
	}

	sprintf( p, "</table>" );
	p += gbstrlen( p );

 sendPage:
	// PageNetTest html control page ends here.
	//------------------------------------------

	int32_t bufLen = p - buf;

	return g_httpServer.sendDynamicPage ( s , buf , bufLen );
}


bool sendPageNetTest( TcpSocket *s, HttpRequest *r ) {
	return g_pageNetTest.controls( s, r );
}


//This page is retrieved via the PageGet class.
//The url would look like http://host:port/get?rnettest=1
bool PageNetTest::resultsPage( TcpSocket *s ) {//, HttpRequest *r ) {
	char buf [ 64*1024 ];
	char *p    = buf;

	//------------------------------------------
	// PageNetTest results page starts here.
	// Information is printed out as:
	// HostId TestHostId SendRate RcvRate\r\n (TEST #1)
	// HostId TestHostId SendRate RcvRate\r\n (TEST #2)


	int64_t avg[4];
	for( int32_t j = 0; j < 4 ; j++ ) {
		avg[j] = 0;
		int32_t i;
		for( i = 0; i < 50 && m_calcTable[j][i]; i++ )
			avg[j] += m_calcTable[j][i];

		if( i ) avg[j] /= i;
	}

	if( m_type[0] == TEST_SEND )
		sprintf( p, "%"INT32" %"INT32" %"UINT64" %"UINT64"\r\n", m_hostId, m_testHostId[0],
			 avg[0], avg[1] );
	else
		sprintf( p, "%"INT32" %"INT32" %"UINT64" %"UINT64"\r\n", m_hostId, m_testHostId[0],
			 avg[1], avg[0] );
	p += gbstrlen( p );

	if( m_type[2] == TEST_SEND )
		sprintf( p, "%"INT32" %"INT32" %"UINT64" %"UINT64"\r\n", m_hostId, m_testHostId[1],
			 avg[2], avg[3] );
	else
		sprintf( p, "%"INT32" %"INT32" %"UINT64" %"UINT64"\r\n", m_hostId, m_testHostId[1],
			 avg[3], avg[2] );
	p += gbstrlen( p );

	// PageNetTest results page ends here.
	//------------------------------------------

	int32_t bufLen = p - buf;

	return g_httpServer.sendDynamicPage ( s , buf , bufLen );
}


bool sendPageNetResult( TcpSocket *s ) {//, HttpRequest *r ) {
	return g_pageNetTest.resultsPage( s );
}


//
//Results Table Sort Functions
//

//Sort switch groups ascending.
int switchSort( const void *p1, const void *p2 ) {
	Host *h1 = g_hostdb.getHost(*(int32_t *)p1);
	Host *h2 = g_hostdb.getHost(*(int32_t *)p2);
	int32_t sg1 = 0;
	int32_t sg2 = 0;
	if ( g_hostdb.m_indexSplits > 1 ) {
		sg1 = h1->m_shardNum % g_hostdb.m_indexSplits;
		sg2 = h2->m_shardNum % g_hostdb.m_indexSplits;
	}
	return (sg1-sg2);
}


//Sort left neighbor send descending.
int send1Sort( const void *p1, const void *p2 ) {
	int32_t s1 = g_pageNetTest.getSend1(*(int32_t *)p1);
	int32_t s2 = g_pageNetTest.getSend1(*(int32_t *)p2);

	return (s2-s1);
}


//Sort left neighbor receive descending.
int receive1Sort( const void *p1, const void *p2 ) {
	int32_t r1 = g_pageNetTest.getReceive1(*(int32_t *)p1);
	int32_t r2 = g_pageNetTest.getReceive1(*(int32_t *)p2);

	return (r2-r1);
}


//Sort right neighbor send descending.
int send2Sort( const void *p1, const void *p2 ) {
	int32_t s1 = g_pageNetTest.getSend2(*(int32_t *)p1);
	int32_t s2 = g_pageNetTest.getSend2(*(int32_t *)p2);

	return (s2-s1);
}


//Sort right neighbor receive descending.
int receive2Sort( const void *p1, const void *p2 ) {
	int32_t r1 = g_pageNetTest.getReceive2(*(int32_t *)p1);
	int32_t r2 = g_pageNetTest.getReceive2(*(int32_t *)p2);

	return (r2-r1);
}


//Sort host ids ascending.
int hostSort( const void *p1, const void *p2 ) {
	return ((*(int32_t *)p1) - (*(int32_t *)p2));
}


//Sort switch ids ascending.
int networkSort( const void *p1, const void *p2 ) {
	Host *h1 = g_hostdb.getHost(*(int32_t *)p1);
	Host *h2 = g_hostdb.getHost(*(int32_t *)p2);

	return (h1->m_switchId-h2->m_switchId);
}


//Sort test #1 host ids ascending.
int testId1Sort( const void *p1, const void *p2 ) {
	int32_t id1 = ((*(int32_t *)p1)%2)?((*(int32_t *)p1)+1):((*(int32_t *)p1)-1);
	int32_t id2 = ((*(int32_t *)p2)%2)?((*(int32_t *)p2)+1):((*(int32_t *)p2)-1);

	int32_t numHosts = g_hostdb.getNumHosts();
	
	if( id1 < 0         ) id1 = numHosts-1;
	if( id1 >= numHosts ) id1 = 0;
	if( id2 < 0         ) id2 = numHosts-1;
	if( id2 >= numHosts ) id2 = 0;
       		
	return (id1 - id2);
}


//Sort test #2 host ids ascending.
int testId2Sort( const void *p1, const void *p2 ) {
	int32_t id1 = ((*(int32_t *)p1)%2)?((*(int32_t *)p1)-1):((*(int32_t *)p1)+1);
	int32_t id2 = ((*(int32_t *)p2)%2)?((*(int32_t *)p2)-1):((*(int32_t *)p2)+1);

	int32_t numHosts = g_hostdb.getNumHosts();

	if( id1 < 0         ) id1 = numHosts-1;
	if( id1 >= numHosts ) id1 = 0;
	if( id2 < 0         ) id2 = numHosts-1;
	if( id2 >= numHosts ) id2 = 0;

	return (id1 - id2);
}
