#include "gb-include.h"

#include "Msg28.h"
#include "HttpServer.h"
#include "Parms.h"

Msg28::Msg28 ( ) {
	m_buf = NULL;
}

Msg28::~Msg28 ( ) {
	//if ( m_buf ) mfree ( m_buf , m_bufSize , "Msg28" );
	m_buf = NULL;
}

bool Msg28::massConfig ( char  *requestBuf              ,
			 void  *state                   ,
			 void (* callback) (void *state ) ) {
	// must be here
	if ( ! requestBuf ) { char *xx=NULL;*xx=0; }
	// must have a request buf already made then
	m_bufSize       = gbstrlen(requestBuf);
	m_bufLen        = m_bufSize;
	m_buf           = requestBuf;
	m_numRequests   = 0;
	m_numReplies    = 0;
	m_numHosts      = g_hostdb.getNumHosts();
	m_sendTotal     = m_numHosts;
	m_state         = state;
	m_callback      = callback;
	m_hostId        = -1;
	m_hostId2       = -1;
	m_ourselvesLast = false;
	m_sendToProxy   = false;
	m_freeBuf       = false;
	m_i             = 0;

	// this returns false if blocked
	if ( ! doSendLoop() ) return false;

	return true;
}

// if hostId2 >= 0 then send to the range of hostIds in the 
// interval [hostId,hostId2]
bool Msg28::massConfig ( TcpSocket   *s                       ,
			 HttpRequest *r                       ,
			 int32_t         hostId                  ,
			 void        *state                   ,
			 void     (* callback) (void *state ) ,
			 bool         ourselvesLast           ,
			 bool         sendToProxy             ,
			 int32_t         hostId2                 ) {

	m_ourselvesLast = ourselvesLast;
	m_state         = state;
	m_callback      = callback;
	m_sendToProxy   = sendToProxy;
	m_freeBuf       = true;

	// bail if we're an interface machine, don't configure the main
	//if ( g_conf.m_interfaceMachine ) return true;

	// make our own request from the cgi parms
	m_bufSize = s->m_readOffset + 250 ;
	m_buf = (char *)mmalloc (m_bufSize,"Msg28");
	if ( ! m_buf ) {
		log("admin: Could not allocate %"INT32" bytes to forward config "
		    "request.",m_bufSize);
		return true;
	}

	char *p    = m_buf;
	char *pend = m_buf + m_bufSize;
	sprintf ( p , 
		  "POST %s HTTP/1.0\r\n"
		  "Content-Length: 0000000\r\n\r\n" , 
		  r->getFilename());
	p += gbstrlen ( p );
	char *sizep = strstr ( m_buf , "Content-Length: " );
	if ( ! sizep ) { char *xx = NULL; *xx = 0; }
	sizep += 16;
	// do not cast
	char *cc = p;
	sprintf ( p , "username=msg28&cast=0&" );
	p += gbstrlen ( p );
	for ( int32_t i = 0 ; i < r->getNumFields() ; i++ ) {
		// skip cast
		if ( strcmp ( r->getField(i) , "cast" ) == 0 ) continue;
		// but keep everything else
		sprintf ( p , "%s=" , r->getField(i) );
		p += gbstrlen( p );
		if ( p + 10 >= pend ) {
			g_errno = EBADENGINEER;
			log(LOG_LOGIC,"Msg28: fix this buffer breech. "
			    "Command failed.");
			return true;
		}
		// just a field and no "=<value>" ? then just skip it
		if ( ! r->getValue(i) ) continue;
		// otherwise, propagate it into the broadcast
		p += urlEncode ( p , pend - p , 
				 r->getValue(i) , gbstrlen(r->getValue(i)) );
		if ( i + 1 < r->getNumFields() ) *p++ = '&';
	}
	*p = '\0';
	m_bufLen = p - m_buf;
	// store content-length
	sprintf ( sizep , "%07"INT32"" , (int32_t)(p - cc) );
	// recover the lost char from the sprintf's \0
	*(sizep+7) = '\r';

	m_numRequests = 0;
	m_numReplies  = 0;
	m_numHosts    = g_hostdb.getNumHosts();
	if (m_sendToProxy)
		m_numHosts = g_hostdb.m_numProxyHosts;

	m_hostId = hostId;

	// in order to specify an endpoint for the range the first point must
	// be a valid hostid, otherwise it is a mess up
	if ( hostId2 >= 0 && hostId < 0 ) {
		log("admin: Endpoint of hostid interval range is valid, but "
		    "first point is not.");
		hostId2 = -1;
	}
	m_hostId2 = hostId2;

	m_i = 0;

	if ( hostId2 >= m_numHosts ) {
		g_errno = EBADENGINEER;
		log("admin: Second hostid is %"INT32", but we only have %"INT32" hosts.",
		    hostId2,m_numHosts);
		return true;
	}
	if ( hostId >= m_numHosts ) {
		g_errno = EBADENGINEER;
		log("admin: Hostid is %"INT32", but we only have %"INT32" hosts.",
		    hostId,m_numHosts);
		return true;
	}

	// how many requests will we be sending out?
	m_sendTotal = m_numHosts;
	if ( m_hostId >= 0 && m_hostId2 >= 0 ) m_sendTotal = hostId2-hostId+1;

	// this returns false if blocked
	if ( ! doSendLoop() ) return false;

	// all done w/o blocking, free the buf here
	if ( m_buf ) mfree ( m_buf , m_bufSize , "Msg28" );
	m_buf = NULL;

	return true;
}

static void gotReply ( void *state , TcpSocket *s );

// accomodate a big cluster
#define MAX_OUT_MSG28 256

// returns false if blocked, true otherwise
bool Msg28::doSendLoop ( ) {
	// only send once if we should
	//if ( m_hostId >= 0 && m_numRequests >= MAX_OUT_MSG28 ) return true;
	if ( m_hostId>=0 && m_numRequests>=1 && m_hostId2 == -1) return true;
	// nothing to do if we sent a request to each host
	//int32_t numHosts = g_hostdb.getNumHosts();
	if ( m_numRequests >= m_sendTotal ) return true;
	// send to ourselves last iff m_ourselvesLast is true
	int32_t total = m_numHosts ;
	if ( m_ourselvesLast ) total++;
	// . now send it to all! allow up to 16 outstanding requests.
	// . only allow 1 outstanding, and do ourselves last in case of
	//   save & exit...
	for ( int32_t i = m_i ; i < total && 
		      m_numRequests - m_numReplies < MAX_OUT_MSG28 ; i++ ){
		// skip it for next call, but stick to the last host, that's us
		if ( i != m_numHosts ) m_i++;
		// if we have a range and i is not in it, skip, but watch
		// out when i==m_numHosts, because that is when we send the
		// request to ourselves, provided we are in the range
		if ( m_hostId >= 0 && m_hostId2 >= 0 && i < m_numHosts )
			if ( i < m_hostId || i > m_hostId2 ) continue;
		// do not send to ourselves until sent to others
		if ( m_ourselvesLast && i == g_hostdb.m_hostId && i<m_numHosts)
			continue;
		// if we are now sending to ourselves, check to make sure
		// all replies are back and we ourselves are in the docid range
		// if one was given
		if ( m_ourselvesLast && i == total-1 ) {
			// and we must have gotten back all the replies...
			// or at least error messages like ETIMEDOUT
			if ( m_numReplies < m_sendTotal - 1 ) continue;
			// and we must be in range, if one was given
			if ( m_hostId >= 0 && m_hostId2 >= 0 ) {
				if ( g_hostdb.m_hostId < m_hostId  ) continue;
				if ( g_hostdb.m_hostId > m_hostId2 ) continue;
			}
		}
		// count it
		m_numRequests++;
		// get the ith host
		Host *h = g_hostdb.getHost ( i );
		if ( m_sendToProxy )
			h = g_hostdb.getProxy(i);
		// . if we are the "last i", that means us
		// . we do ourselves last in case of a "save & exit" request
		if ( i == m_numHosts ) {
			h = g_hostdb.getHost(g_hostdb.m_hostId);
			if (m_sendToProxy)
				h = g_hostdb.getProxy(g_hostdb.m_hostId);
		}
		// did we have one explicitly given?
		// ... and make sure we are not a range of hostids...
		if ( m_hostId >= 0 && m_hostId2 == -1 ) {
			h = g_hostdb.getHost (m_hostId);
			if ( m_sendToProxy )
				h = g_hostdb.getProxy ( m_hostId );
		}
		// debug
		log(LOG_INIT,"admin: sending to hostid #%"INT32".",h->m_hostId);
		// timeout is int16_ter if host is dead
		int32_t timeout = 30000; // 30 seconds
		// only 7 seconds if it is dead seemingly
		if ( g_hostdb.isDead ( h ) ) timeout = 7000;
		//	continue;
		// . launch it
		// . returns false if blocked, true otherwise
		// . sets g_errno on error
		TcpServer *tcp = &g_httpServer.m_tcp;
		if ( tcp->sendMsg ( h->m_ip      ,
				    h->m_httpPort,
				    m_buf        ,
				    m_bufSize    ,
				    m_bufLen     ,
				    m_bufLen     ,
				    this         ,   // state
				    gotReply     ,
				    timeout      ,   // 5000, timeout
				    100*1024     ,   // maxTextDocLen
				    100*1024     )){ // maxOtherDocLen
			log("admin: Could not send configuration request "
			    "to hostId #%"INT32" (%s:%"INT32"): %s.",h->m_hostId,
			    iptoa(h->m_ip),(int32_t)h->m_port,mstrerror(g_errno));
			g_errno = 0;//return true;
			m_numReplies++;
		}
		// all done if only one host... and not a range
		if ( m_hostId >= 0 && m_hostId2 == -1 ) break;
		// it blocked
		//return false;
	}
	// return false if we blocked
	//if ( m_numReplies < m_numRequests ) return false;
	// do not finish until we got them all
	if ( m_numReplies < m_sendTotal ) return false;
	return true;
}

void gotReply ( void *state , TcpSocket *s ) {
	// send another
	Msg28 *THIS = (Msg28 *)state;
	// count em
	THIS->m_numReplies++;
	// do not free send buffer
	s->m_sendBuf = NULL;
	// debug
	Host *h = g_hostdb.getTcpHost ( s->m_ip , s->m_port );
	//if (THIS->m_sendToProxy)
	//	h = g_hostdb.getProxyFromTcpPort ( s->m_ip , s->m_port );
	log(LOG_INIT,"admin: got reply from hostid #%"INT32".",h->m_hostId);
	//slot->m_readBufSize,h->m_hostId);
	// log errors
	if ( g_errno ) {
		if ( h ) log("admin: Error broadcasting config request to "
			     "hostid #%"INT32" (%s:%"INT32"): %s.",
			     h->m_hostId,iptoa(h->m_ip),(int32_t)s->m_port,
			     mstrerror(g_errno));
		else     log("admin: Error broadcasting config request: "
			     "%s.",mstrerror(g_errno));
		g_errno = 0;
	}
	// try to send more
	if ( ! THIS->doSendLoop ( ) ) return;
	// do we have all the replies?
	//if ( THIS->m_numReplies < THIS->m_numRequests ) return;
	// do not finish until we got them all
	if ( THIS->m_hostId < 0 && THIS->m_numReplies < THIS->m_sendTotal ) 
		return;
	if ( THIS->m_hostId >= 0 && THIS->m_hostId2 >= 0 &&
	     THIS->m_numReplies < THIS->m_sendTotal ) 
		return;
	// all done, free the buf here
	if ( THIS->m_freeBuf ) 
		mfree ( THIS->m_buf , THIS->m_bufSize , "Msg28" );
	THIS->m_buf = NULL;
	// all done if did not block
	THIS->m_callback ( THIS->m_state );
}
