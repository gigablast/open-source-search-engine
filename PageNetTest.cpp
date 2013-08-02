#include "gb-include.h"

#include "PageNetTest.h"
#include "CollectionRec.h"
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
	long numHst = g_hostdb.getNumHosts();
	for( long i = 0; i < MAX_TEST_THREADS; i++ ) {
		if( m_hostRates[i] ) mfree( m_hostRates[i],
					    sizeof(unsigned long *)*numHst,
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

	long numHst = g_hostdb.getNumHosts();

	for( long i = 0; i < 4; i++ ) {
		m_hostRates[i]=(unsigned long *)mmalloc(sizeof(unsigned long *)*
							numHst,
							"NT-HostRate" );
		if( !m_hostRates[i] ) {
			g_errno = ENOMEM;
			log( "net: nettest: could not allocate %li bytes.",
			     sizeof(unsigned long *) * numHst);
			return false;
		}
		memset( m_hostRates[i], 0, numHst * sizeof(unsigned long *) );
	}

	/*
	long switchId = -1;
	for( long i = 0; i < g_hostdb.getNumHosts(); i++ ) {
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

	for( long i = 0; i < MAX_TEST_THREADS; i++ ) {
		if( m_testHostId[i] < 0 ) 
			m_testHostId[i] = g_hostdb.getNumHosts()-1;
		if( m_testHostId[i] >= g_hostdb.getNumHosts() ) 
			m_testHostId[i] = 0;	
		m_testIp[i] = g_hostdb.getHost( m_testHostId[i] )->m_ip;
		if( (unsigned long)m_testIp[i] == g_hostdb.getMyIp() ) 
			log( "net: nettest: cannot test two processes on the "
			     "same IP." );

	}

	char buf [16]; 
	strcpy( buf, iptoa(g_hostdb.getMyIp()) );
	char buf2[16];
	strcpy( buf2, iptoa(m_testIp[0]) );

	log( LOG_DEBUG, "net: nettest: hostId %ld (%s) is on switch %ld and "
	     "will test hostId %ld (%s) as a %s on port %ld first", m_hostId, 
	     buf, m_switchId, (long)m_testHostId, buf2, 
	     (m_type[0] == TEST_SEND)?"sender":"receiver", m_port[0] );

	memset ( m_sdgram , 'X' , NTDGRAM_SIZE );
	memset ( m_calcTable, 0, MAX_TEST_THREADS*AVG_TABLE_SIZE );

	return true;
}

void PageNetTest::reset() {
	m_threadNum = 0;
	m_threadReturned = 0;
	memset ( m_calcTable, 0, MAX_TEST_THREADS*AVG_TABLE_SIZE );
	for( long i = 0; i < 4; i++ )
		memset( m_hostRates[i], 0, 
			g_hostdb.getNumHosts() * sizeof(unsigned long *) );
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
	long ip = g_hostdb.getHost( m_numResultsSent )->m_ip;
	long port = g_hostdb.getHost( m_numResultsSent )->m_httpPort;
	//long len = 0;
	sprintf(temp, "http://%s:%li/get?rnettest=1", iptoa(ip), port);
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
	long  bufLen, bufMaxLen;
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
	long len = 0;
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

	long state = 0;
	long hostId = 0;
	long testId = 0;

	if( !bufLen ) log( LOG_INFO, "net: nettest: we got an empty doc." );

	buf += mime.getMimeLen();
	bufLen -= mime.getMimeLen();

	for( long i = 0; i < bufLen; i++ ){		
		if( buf[i] == ' '  ) continue;
		if( buf[i] == '\r' ) continue;
		if( buf[i] == '\n' ) continue;
		if( buf[i] <  '0'  ) continue;

		if( state == 0 ) {
			hostId = atoi(&buf[i]);
			log( LOG_DEBUG, "net: nettest: host id is %ld",
			     hostId);
			state = 1;
		}
		else if( state == 1 ) {
			testId = atoi(&buf[i]);
			log( LOG_DEBUG, "net: nettest: test id is %ld",
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


bool PageNetTest::netTestStart_r( bool amThread, long num ) {
	long long endTime   = 0;
	long long calcTime  = 0;
	long      count;
	long      index     = 0;

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
		log( LOG_INFO, "net: nettest: took %lli ms to %s %li bytes at "
		     "%.2f Mbps", endTime - calcTime, 
		     (m_type[num] == TEST_READ)?"receive":"send", count, mbps );
		log( LOG_INFO, "net: nettest: run time %lli s", 
		     (endTime-m_startTime)/1000 );

		m_calcTable[num][index] = (unsigned long)mbps;
		if( ++index >= AVG_TABLE_SIZE ) index = 0;

		if( !m_runNetTest ) break;
	}

	m_sock[num] = closeSock( m_sock[num] );	
	return true;
}


int PageNetTest::openSock( long num, long type, struct sockaddr_in *name, 
			   long port ) {
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
                log( "net: nettest: bind on port %lu: %s", port, 
		     strerror(errno) );
		return -1;
        }

	if( type == TEST_SEND ) {
		m_to.sin_family      = AF_INET;
		m_to.sin_addr.s_addr = m_testIp[num];
		m_to.sin_port        = htons ( port );//2000 ) ; // m_port );
		bzero ( &(m_to.sin_zero) , 8 );
	}

	log( LOG_DEBUG, "net: nettest: open socket for %s on port %ld to %s", 
	     (type == TEST_SEND)?"sending":"receiving", port, 
	     iptoa(m_testIp[num]) );
	return sock;
}


int PageNetTest::closeSock( int sock ) {
	close( sock );
	log( LOG_DEBUG, "net: nettest: socket closed" );
	return -1;
}


long PageNetTest::readSock( int sock ) {
	int n;
	unsigned int fromLen;
	long count = 0;
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


long PageNetTest::sendSock( int sock ) {
	int n;
	unsigned int toLen;
	long count = 0;	
	// send more than expected to make up for losses
	long nn = m_testBytes * 10;

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
	long pwdLen = 0;
	char *pwd = r->getString ( "pwd" , &pwdLen );
	if ( pwdLen > 31 ) pwdLen = 31;
	char pbuf [32];
	if ( pwdLen > 0 ) strncpy ( pbuf , pwd , pwdLen );
	pbuf[pwdLen]='\0';

	long hids[MAX_HOSTS];
	long numHosts = g_hostdb.getNumHosts();

	long len = 0;
	char *coll = r->getString( "c", &len );
	memcpy( m_coll, coll, len );

	//long ntnd      = r->getLong( "ntnd", 0              );
	//long rcv       = r->getLong( "ntrs", 0              );
	m_testBytes    = r->getLong( "ntb" , m_testBytes    );
	m_port[0]      = r->getLong( "ntp1" , m_port[0]     );
	m_port[1]      = r->getLong( "ntp2" , m_port[1]     );
	m_port[2]      = m_port[0];
	m_port[3]      = m_port[1];
	m_testDuration = r->getLong( "ntd" , m_testDuration );
	m_fullDuplex   = r->getLong( "ntfd" , m_fullDuplex  );
	long sort      = r->getLong( "sort", 7 );
	
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
		 "<td><input name=ntd value=%li type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Port 1"
		 "</span><br><font size=-1>The port number to use for testing."
		 " Default:5066</font></td>"
		 "<td><input name=ntp1 value=%li type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Port 2"
		 "</span><br><font size=-1>The port number to use for testing."
		 " Default:5067</font></td>"
		 "<td><input name=ntp2 value=%li type=text></td></tr>"
		 "<tr><td><span style=\"font-weight:bold;\">Test Bytes"
		 "</span><br><font size=-1>The number of bytes to test the "
		 "transfer rate. Default:10000000</font></td>"
		 "<td><input name=ntb value=%li type=text></td></tr>"
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

	for( long i = 0; i < numHosts; i++ ) hids[i] = i;

	switch( sort ) {
	case 1: gbsort( hids, numHosts, sizeof(long), switchSort   ); break;
	case 2: gbsort( hids, numHosts, sizeof(long), networkSort  ); break;
	case 3: gbsort( hids, numHosts, sizeof(long), send1Sort    ); break;
	case 4: gbsort( hids, numHosts, sizeof(long), receive1Sort ); break;
	case 5: gbsort( hids, numHosts, sizeof(long), send2Sort    ); break;
	case 6: gbsort( hids, numHosts, sizeof(long), receive2Sort ); break;
	case 7: gbsort( hids, numHosts, sizeof(long), hostSort     ); break;
	case 8: gbsort( hids, numHosts, sizeof(long), testId1Sort  ); break;
	case 9: gbsort( hids, numHosts, sizeof(long), testId2Sort  ); break;
	}

	// width=100%% ??
	sprintf( p, "<br><br>"
		 "<div style=\"font-weight:bold;\">"
		 "Results (in Mbps)</div>"
		 "<table border=1 cellpadding=4 "
		 "style=\"empty-cells: hide;\" bgcolor=#%s>", LIGHT_BLUE );
	p += gbstrlen( p );

	for( long i = 0; i < MAX_TEST_THREADS + 5; i++ ) {
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
		
		for( long j = 0; j < numHosts; j++ ) {
			if     ( i == 1 ) {
				Host *h = g_hostdb.getHost(hids[j]);
				long switchGroup = 0;
				if ( g_hostdb.m_indexSplits > 1 )
					switchGroup = h->m_group %
						g_hostdb.m_indexSplits;
				sprintf( p, "<td>%li</td>", switchGroup );
			}
			else if( i == 2 ) {
				Host *h = g_hostdb.getHost(hids[j]);
				sprintf( p, "<td>%d</td>", h->m_switchId );
			}
			else if( i == 0 ) {
				sprintf( p, "<th bgcolor=#%s>%lu</th>",
					 DARK_BLUE, hids[j] );
			}
			else if( i == 3 ) {
				long tid = (hids[j]%2)?(hids[j]+1):(hids[j]-1);
				if( tid < 0         ) tid = numHosts-1;
				if( tid >= numHosts ) tid = 0;
				sprintf( p, "<td>%lu</td>", tid );
			}
			else if( i > 3 && i < 6 ) {
				sprintf( p, "<td>%lu</td>",
					 m_hostRates[i-4][hids[j]] );
			}
			else if( i == 6 ) {
				long tid = (hids[j]%2)?(hids[j]-1):(hids[j]+1);
				if( tid < 0         ) tid = numHosts-1;
				if( tid >= numHosts ) tid = 0;
				sprintf( p, "<td>%lu</td>", tid );
			}
			else if( i > 6 ) {
				sprintf( p, "<td>%lu</td>",
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

	long bufLen = p - buf;

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


	long long avg[4];
	for( long j = 0; j < 4 ; j++ ) {
		avg[j] = 0;
		long i;
		for( i = 0; i < 50 && m_calcTable[j][i]; i++ )
			avg[j] += m_calcTable[j][i];

		if( i ) avg[j] /= i;
	}

	if( m_type[0] == TEST_SEND )
		sprintf( p, "%ld %ld %llu %llu\r\n", m_hostId, m_testHostId[0],
			 avg[0], avg[1] );
	else
		sprintf( p, "%ld %ld %llu %llu\r\n", m_hostId, m_testHostId[0],
			 avg[1], avg[0] );
	p += gbstrlen( p );

	if( m_type[2] == TEST_SEND )
		sprintf( p, "%ld %ld %llu %llu\r\n", m_hostId, m_testHostId[1],
			 avg[2], avg[3] );
	else
		sprintf( p, "%ld %ld %llu %llu\r\n", m_hostId, m_testHostId[1],
			 avg[3], avg[2] );
	p += gbstrlen( p );

	// PageNetTest results page ends here.
	//------------------------------------------

	long bufLen = p - buf;

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
	Host *h1 = g_hostdb.getHost(*(long *)p1);
	Host *h2 = g_hostdb.getHost(*(long *)p2);
	long sg1 = 0;
	long sg2 = 0;
	if ( g_hostdb.m_indexSplits > 1 ) {
		sg1 = h1->m_group % g_hostdb.m_indexSplits;
		sg2 = h2->m_group % g_hostdb.m_indexSplits;
	}
	return (sg1-sg2);
}


//Sort left neighbor send descending.
int send1Sort( const void *p1, const void *p2 ) {
	long s1 = g_pageNetTest.getSend1(*(long *)p1);
	long s2 = g_pageNetTest.getSend1(*(long *)p2);

	return (s2-s1);
}


//Sort left neighbor receive descending.
int receive1Sort( const void *p1, const void *p2 ) {
	long r1 = g_pageNetTest.getReceive1(*(long *)p1);
	long r2 = g_pageNetTest.getReceive1(*(long *)p2);

	return (r2-r1);
}


//Sort right neighbor send descending.
int send2Sort( const void *p1, const void *p2 ) {
	long s1 = g_pageNetTest.getSend2(*(long *)p1);
	long s2 = g_pageNetTest.getSend2(*(long *)p2);

	return (s2-s1);
}


//Sort right neighbor receive descending.
int receive2Sort( const void *p1, const void *p2 ) {
	long r1 = g_pageNetTest.getReceive2(*(long *)p1);
	long r2 = g_pageNetTest.getReceive2(*(long *)p2);

	return (r2-r1);
}


//Sort host ids ascending.
int hostSort( const void *p1, const void *p2 ) {
	return ((*(long *)p1) - (*(long *)p2));
}


//Sort switch ids ascending.
int networkSort( const void *p1, const void *p2 ) {
	Host *h1 = g_hostdb.getHost(*(long *)p1);
	Host *h2 = g_hostdb.getHost(*(long *)p2);

	return (h1->m_switchId-h2->m_switchId);
}


//Sort test #1 host ids ascending.
int testId1Sort( const void *p1, const void *p2 ) {
	long id1 = ((*(long *)p1)%2)?((*(long *)p1)+1):((*(long *)p1)-1);
	long id2 = ((*(long *)p2)%2)?((*(long *)p2)+1):((*(long *)p2)-1);

	long numHosts = g_hostdb.getNumHosts();
	
	if( id1 < 0         ) id1 = numHosts-1;
	if( id1 >= numHosts ) id1 = 0;
	if( id2 < 0         ) id2 = numHosts-1;
	if( id2 >= numHosts ) id2 = 0;
       		
	return (id1 - id2);
}


//Sort test #2 host ids ascending.
int testId2Sort( const void *p1, const void *p2 ) {
	long id1 = ((*(long *)p1)%2)?((*(long *)p1)-1):((*(long *)p1)+1);
	long id2 = ((*(long *)p2)%2)?((*(long *)p2)-1):((*(long *)p2)+1);

	long numHosts = g_hostdb.getNumHosts();

	if( id1 < 0         ) id1 = numHosts-1;
	if( id1 >= numHosts ) id1 = 0;
	if( id2 < 0         ) id2 = numHosts-1;
	if( id2 >= numHosts ) id2 = 0;

	return (id1 - id2);
}
