#include "gb-include.h"

#include "TcpServer.h"
#include "UdpServer.h"
#include "Rdb.h"
#include "Pages.h"
#include "Dns.h"
#include "SafeBuf.h"
#include "Msg13.h"
#include "Linkdb.h" // Msg25Request

static void printTcpTable  (SafeBuf *p,char *title,TcpServer *server);
static void printUdpTable  (SafeBuf *p,char *title,UdpServer *server,
			     char *coll, char *pwd , int32_t fromIp ,
			    bool isDns = false );

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSockets ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 128*1024 ];
	SafeBuf p(buf, 128*1024);
	//char *bufEnd = buf + 256*1024;
	// a ptr into "buf"
	// password, too
	//int32_t pwdLen = 0;
	//char *pwd = r->getString ( "pwd" , &pwdLen );
	//if ( pwdLen > 31 ) pwdLen = 31;
	//if ( pwd ) pwd[pwdLen]='\0';
	int32_t collLen = 0;
	char *coll = r->getString( "c", &collLen );
	if ( collLen > MAX_COLL_LEN ) collLen = MAX_COLL_LEN;
	if ( coll ) coll[collLen] = '\0';
	//char pbuf [32];
	//if ( pwdLen > 0 ) strncpy ( pbuf , pwd , pwdLen );
	//pbuf[pwdLen]='\0';
	// print standard header


	// 	char *ss = p.getBuf();
	// 	char *ssend = p.getBufEnd();
	g_pages.printAdminTop ( &p, s , r );
	//p.incrementLength(sss - ss);

	// now print out the sockets table for each tcp server we have
	printTcpTable(&p,"HTTP Server"    ,g_httpServer.getTcp());
	printTcpTable(&p,"HTTPS Server"    ,g_httpServer.getSSLTcp());
	printUdpTable(&p,"Udp Server" , &g_udpServer,coll,NULL,s->m_ip);
	//printUdpTable(&p,"Udp Server(async)",&g_udpServer2,coll,pwd,s->m_ip);
	printUdpTable(&p,"Udp Server (dns)", &g_dns.m_udpServer,
		      coll,NULL,s->m_ip,true/*isDns?*/);

	// from msg13.cpp print the queued url download requests
	printHammerQueueTable ( &p );

	// get # of disks per machine
	int32_t count = 0;
	for ( int32_t i = 0 ; i < g_hostdb.getNumHosts(); i++ ) {
		int32_t hid = g_hostdb.m_hostPtrs[i]->m_hostId;
		int32_t m   = g_hostdb.getMachineNum ( hid );
		if ( m == 0 ) count++;
	}

	/*
	sprintf ( p , "<table width=100%% bgcolor=#d0d0f0 border=1>"
		  "<tr><td bgcolor=#c0c0f0 colspan=%"INT32">"
		  "<center><font size=+1><b>Wait Times</b></font>"
		  "</td></tr>\n" , 3 + count );
	p += gbstrlen ( p );
	// print columns
	sprintf ( p , 
		  "<tr>"
		  "<td><b>machine #</b></td>"
		  "<td><b>send wait</b></td>"
		  "<td><b>read wait</b></td>" );
	p += gbstrlen ( p );	
	// print disk columns
	for ( int32_t i = 0 ; i < count ; i++ ) {
		sprintf ( p , "<td><b>disk %"INT32" wait</b></td>",i);
		p += gbstrlen ( p );	
	}
	// end the top row
	sprintf ( p , "</tr>\n" );
	p += gbstrlen ( p );	
	// print rows
	for ( int32_t i = 0 ; i < g_hostdb.getNumMachines() ; i++ ) {
		// print machine #
		sprintf ( p , "<tr><td><b>%"INT32"</b></td>",i);
		p += gbstrlen ( p );
		// then net send
		float x = (float)g_queryRouter.m_sendWaits[i] / 1000;
		sprintf ( p , "<td>%.1fms</td>", x );
		p += gbstrlen ( p );
		// then net read
		x = (float)g_queryRouter.m_readWaits[i] / 1000;
		sprintf ( p , "<td>%.1fms</td>", x );
		p += gbstrlen ( p );
		// print disk wait in milliseconds (it's in microseconds)
		// find any host that matches this machine
		for ( int32_t j = 0 ; j < g_hostdb.getNumHosts() ; j++ ) {
			// use in order of ip
			int32_t hid = g_hostdb.m_hostPtrs[j]->m_hostId;
			// get machine #
			int32_t m = g_hostdb.getMachineNum(hid);
			// skip if no match
			if ( m != i ) continue;
			// otherwise print
			x = (float)g_queryRouter.m_diskWaits[hid] / 1000;
			sprintf ( p , "<td>%.1fms</td>", x );
			p += gbstrlen ( p );
		}
		// end row
		sprintf ( p , "</tr>\n");
		p += gbstrlen ( p );	
	}
	// end table
	sprintf ( p , "</table>");
	p += gbstrlen ( p );
	*/

	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );

	// calculate buffer length
	int32_t bufLen = p.length();
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage ( s , (char*) p.getBufStart() ,
						bufLen );
}


void printTcpTable ( SafeBuf* p, char *title, TcpServer *server ) {
	// table headers for urls current being spiderd
	p->safePrintf ( "<table %s>"
		       "<tr class=hdrow><td colspan=19>"
		       "<center>"
		       //"<font size=+1>"
		       "<b>%s</b>"
		       //"</font>"
		       "</td></tr>"
		       "<tr bgcolor=#%s>"
		       "<td><b>#</td>"
		       "<td><b>fd</td>"
		       "<td><b>age</td>"
		       "<td><b>idle</td>"
		       //"<td><b>timeout</td>"
		       "<td><b>ip</td>"
		       "<td><b>port</td>"
		       "<td><b>state</td>"
		       "<td><b>bytes read</td>"
		       "<td><b>bytes to read</td>"
		       "<td><b>bytes sent</td>"
		       "<td><b>bytes to send</td>"
		       "</tr>\n"
			, TABLE_STYLE
			, title 
			, DARK_BLUE
			);
	// current time in milliseconds
	int64_t now = gettimeofdayInMilliseconds();
	// store in buffer for sorting
	int32_t       times[MAX_TCP_SOCKS];
	TcpSocket *socks[MAX_TCP_SOCKS];
	int32_t nn = 0;
	for ( int32_t i = 0 ; i<=server->m_lastFilled && nn<MAX_TCP_SOCKS; i++ ) {
		// get the ith socket
		TcpSocket *s = server->m_tcpSockets[i];
		// continue if empty
		if ( ! s ) continue;
		// store it
		times[nn] = now - s->m_startTime;
		socks[nn] = s;
		nn++;
	}
	// bubble sort
 keepSorting:
	// assume no swap will happen
	bool didSwap = false;
	for ( int32_t i = 1 ; i < nn ; i++ ) {
		if ( times[i-1] >= times[i] ) continue;
		int32_t       tmpTime = times[i-1];
		TcpSocket *tmpSock = socks[i-1]; 
		times[i-1] = times[i];
		socks[i-1] = socks[i];
		times[i  ] = tmpTime;
		socks[i  ] = tmpSock;
		didSwap = true;
	}
	if ( didSwap ) goto keepSorting;

	// now fill in the columns
	for ( int32_t i = 0 ; i < nn ; i++ ) {
		// get the ith socket
		TcpSocket *s = socks[i];
		// set socket state
		char *st = "ERROR";
		switch ( s->m_sockState ) {
		case ST_AVAILABLE:  st="available";  break;
		//case ST_CLOSED:     st="closed";     break;
		case ST_CONNECTING: st="connecting"; break;
		case ST_READING:    st="reading";    break;
		case ST_SSL_ACCEPT:    st="ssl accept";    break;
		case ST_SSL_SHUTDOWN:    st="ssl shutdown";    break;
		case ST_WRITING:    st="sending";    break;
		case ST_NEEDS_CLOSE:    st="needs close";    break;
		case ST_CLOSE_CALLED:    st="close called";    break;
		case ST_SSL_HANDSHAKE: st = "ssl handshake"; break;
		}
		// bgcolor is lighter for incoming requests
		char *bg = "c0c0f0";
		if ( s->m_isIncoming ) bg = "e8e8ff";
		// times
		int32_t elapsed1 = now - s->m_startTime      ;
		int32_t elapsed2 = now - s->m_lastActionTime ;
		p->safePrintf ("<tr bgcolor=#%s>"
			       "<td>%"INT32"</td>" // i
			       "<td>%i</td>" // fd
			       "<td>%"INT32"ms</td>"  // elapsed seconds since start
			       "<td>%"INT32"ms</td>"  // last action
			       //"<td>%"INT32"</td>"  // timeout			  
			       "<td>%s</td>"  // ip
			       "<td>%hu</td>" // port
			       "<td>%s</td>"  // state
			       ,
			       bg ,
			       i,
			       s->m_sd ,
			       elapsed1,
			       elapsed2,
			       //s->m_timeout ,
			       iptoa(s->m_ip) ,
			       s->m_port ,
			       st );


		// tool tip to show top 500 bytes of send buf
		if ( s->m_readOffset && s->m_readBuf ) {
			p->safePrintf("<td><a title=\"");
			SafeBuf tmp;
			tmp.safeTruncateEllipsis ( s->m_readBuf , 
						   s->m_readOffset ,
						   500 );
			p->htmlEncode ( tmp.getBufStart() );
			p->safePrintf("\">");
			p->safePrintf("<u>%"INT32"</u></td>",s->m_readOffset);
		}
		else
			p->safePrintf("<td>0</td>");

		p->safePrintf( "<td>%"INT32"</td>" // bytes to read
			       "<td>%"INT32"</td>" // bytes sent
			       ,
			       s->m_totalToRead ,
			       s->m_sendOffset
			       );

		// tool tip to show top 500 bytes of send buf
		if ( s->m_totalToSend && s->m_sendBuf ) {
			p->safePrintf("<td><a title=\"");
			SafeBuf tmp;
			tmp.safeTruncateEllipsis ( s->m_sendBuf , 
						   s->m_totalToSend ,
						   500 );
			p->htmlEncode ( tmp.getBufStart() );
			p->safePrintf("\">");
			p->safePrintf("<u>%"INT32"</u></td>",s->m_totalToSend);
		}
		else
			p->safePrintf("<td>0</td>");

		p->safePrintf("</tr>\n");

	}
	// end the table
	p->safePrintf ("</table><br>\n" );
}

void printUdpTable ( SafeBuf *p, char *title, UdpServer *server ,
		     char *coll, char *pwd , int32_t fromIp ,
		     bool isDns ) {
	if ( ! coll ) coll = "main";
	//if ( ! pwd  ) pwd  = "";

	// time now
	int64_t now = gettimeofdayInMilliseconds();
	// get # of used nodes
	//int32_t n = server->getTopUsedSlot();
	// store in buffer for sorting
	int32_t     times[50000];//MAX_UDP_SLOTS];
	UdpSlot *slots[50000];//MAX_UDP_SLOTS];
	int32_t nn = 0;
	for ( UdpSlot *s = server->getActiveHead() ; s ; s = s->m_next2 ) {
		if ( nn >= 50000 ) {
			log("admin: Too many udp sockets.");
			break;
		}
		// if empty skip it
		//if ( server->isEmpty ( i ) ) continue;
		// get the UdpSlot
		//UdpSlot *s = server->getUdpSlotNum(i);
		// if data is NULL that's an error
		//if ( ! s ) continue;
		// store it
		times[nn] = now - s->m_startTime;
		slots[nn] = s;
		nn++;
	}
	// bubble sort
 keepSorting:
	// assume no swap will happen
	bool didSwap = false;
	for ( int32_t i = 1 ; i < nn ; i++ ) {
		if ( times[i-1] >= times[i] ) continue;
		int32_t     tmpTime = times[i-1];
		UdpSlot *tmpSlot = slots[i-1]; 
		times[i-1] = times[i];
		slots[i-1] = slots[i];
		times[i  ] = tmpTime;
		slots[i  ] = tmpSlot;
		didSwap = true;
	}
	if ( didSwap ) goto keepSorting;

	// count how many of each msg we have
	int32_t msgCount0[96];
	int32_t msgCount1[96];
	for ( int32_t i = 0; i < 96; i++ ) {
		msgCount0[i] = 0;
		msgCount1[i] = 0;
	}
	for ( int32_t i = 0; i < nn; i++ ) {
		UdpSlot *s = slots[i];
		if ( s->m_msgType >= 96 ) continue;
		if ( s->m_niceness == 0 )
			msgCount0[s->m_msgType]++;
		else
			msgCount1[s->m_msgType]++;
	}

	char *wr = "";
	if ( server->m_writeRegistered )
		wr = " [write registered]";

	// print the counts
	p->safePrintf ( "<table %s>"
			"<tr class=hdrow><td colspan=19>"
			"<center>"
			"<b>%s Summary</b> (%"INT32" transactions)%s"
			"</td></tr>"
			"<tr bgcolor=#%s>"
			"<td><b>niceness</td>"
			"<td><b>msg type</td>"
			"<td><b>total</td>"
			"</tr>",
			TABLE_STYLE,
			title , server->getNumUsedSlots() ,
			wr ,
			DARK_BLUE );
	for ( int32_t i = 0; i < 96; i++ ) {
		if ( msgCount0[i] <= 0 ) continue;
		p->safePrintf("<tr bgcolor=#%s>"
			      "<td>0</td><td>0x%"XINT32"</td><td>%"INT32"</td></tr>",
			      LIGHT_BLUE,i, msgCount0[i]);
	}
	for ( int32_t i = 0; i < 96; i++ ) {
		if ( msgCount1[i] <= 0 ) continue;
		p->safePrintf("<tr bgcolor=#%s>"
			      "<td>1</td><td>0x%"XINT32"</td><td>%"INT32"</td></tr>",
			      LIGHT_BLUE,i, msgCount1[i]);
	}
	p->safePrintf ( "</table><br>" );

	char *dd = "";
	if ( ! isDns ) 
		dd =    "<td><b>msgType</td>"
			"<td><b>desc</td>"
			"<td><b>hostId</td>";
	else {
		dd = //"<td><b>dns ip</b></td>"
		     "<td><b>hostname</b></td>";
	}

	//UdpSlot *slot = server->m_head3;
	//int32_t callbackReadyCount = 0;
	//for ( ; slot ; slot = slot->m_next3 , callbackReadyCount++ ); 

	p->safePrintf ( "<table %s>"
			"<tr class=hdrow><td colspan=19>"
			"<center>"
			//"<font size=+1>"
			"<b>%s</b> (%"INT32" transactions)"
			//"(%"INT32" requests waiting to processed)"
			"(%"INT32" incoming)"
			//"</font>"
			"</td></tr>"
			"<tr bgcolor=#%s>"
			"<td><b>age</td>"
			"<td><b>last read</td>"
			"<td><b>last send</td>"
			"<td><b>timeout</td>"
			"<td><b>ip</td>"
			//"<td><b>port</td>"
			//"<td><b>desc</td>"
			//"<td><b>hostId</td>"
			//"<td><b>nice</td>";
			"%s"
			"<td><b>nice</td>"
			"<td><b>transId</td>"
			"<td><b>called</td>"
			"<td><b>dgrams read</td>"
			"<td><b>dgrams to read</td>"
			"<td><b>acks sent</td>"
			"<td><b>dgrams sent</td>"
			"<td><b>dgrams to send</td>"
			"<td><b>acks read</td>"
			"<td><b>resends</td>"
			"</tr>\n" , 
			TABLE_STYLE,
			title , server->getNumUsedSlots() , 
			//callbackReadyCount ,
			server->getNumUsedSlotsIncoming() ,
			DARK_BLUE ,
			dd );


	// now fill in the columns
	for ( int32_t i = 0 ; i < nn ; i++ ) {
		// get from sorted list
		UdpSlot *s = slots[i];
		// set socket state
		//char *st = "ERROR";
		//if ( ! s->isDoneReading() ) st = "reading";
		//if ( ! s->isDoneSending() ) st = "reading";
		// times
		int64_t elapsed0 = (now - s->m_startTime    ) ;
		int64_t elapsed1 = (now - s->m_lastReadTime ) ;
		int64_t elapsed2 = (now - s->m_lastSendTime ) ;
		char e0[32],e1[32], e2[32];
		sprintf ( e0 , "%"INT64"ms" , elapsed0 );
		sprintf ( e1 , "%"INT64"ms" , elapsed1 );
		sprintf ( e2 , "%"INT64"ms" , elapsed2 );
		if ( s->m_startTime    == 0LL ) strcpy ( e0 , "--" );
		if ( s->m_lastReadTime == 0LL ) strcpy ( e1 , "--" );
		if ( s->m_lastSendTime == 0LL ) strcpy ( e2 , "--" );
		// bgcolor is lighter for incoming requests
		char *bg = LIGHT_BLUE;//"c0c0f0";
		// is it incoming
		if ( ! s->m_callback ) bg = LIGHTER_BLUE;//"e8e8ff";
		Host *h = g_hostdb.getHost ( s->m_ip , s->m_port );
		char           *eip     = "??";
		uint16_t  eport   =  0 ;
		//int32_t          ehostId = -1 ;
		char           *ehostId = "-1";
		//char tmpIp    [64];
		// print the ip

		char tmpHostId[64];
		if ( h ) {
			// host can have 2 ip addresses, get the one most
			// similar to that of the requester
			eip     = iptoa(g_hostdb.getBestIp ( h , fromIp ));
			//eip     = iptoa(h->m_externalIp) ;
			//eip     = iptoa(h->m_ip) ;
			eport   = h->m_externalHttpPort ;
			//ehostId = h->m_hostId ;
			if ( h->m_isProxy )
				sprintf(tmpHostId,"proxy%"INT32"",h->m_hostId);
			else
				sprintf(tmpHostId,"%"INT32"",h->m_hostId);
			ehostId = tmpHostId;
		}
		// if no corresponding host, it could be a request from an external
		// cluster, so just show the ip
		else {
		        sprintf ( tmpHostId , "%s" , iptoa(s->m_ip) );
			ehostId = tmpHostId;
			eip     = tmpHostId;
		}
		// set description of the msg
		int32_t msgType        = s->m_msgType;
		char *desc          = "";
		char *rbuf          = s->m_readBuf;
		char *sbuf          = s->m_sendBuf;
		int32_t  rbufSize      = s->m_readBufSize;
		int32_t  sbufSize      = s->m_sendBufSize;
		bool  weInit        = s->m_callback;
		char  calledHandler = s->m_calledHandler;
		if ( weInit ) calledHandler = s->m_calledCallback;
		char *buf     = NULL;
		int32_t  bufSize = 0;
		char tt [ 64 ];
		if ( msgType == 0x00 &&   weInit ) buf = sbuf;
		if ( msgType == 0x00 && ! weInit ) buf = rbuf;
		if ( msgType == 0x01 &&   weInit ) buf = sbuf;
		if ( msgType == 0x01 && ! weInit ) buf = rbuf;
		// . if callback was called this slot's sendbuf can be bogus
		// . i put this here to try to avoid a core dump
		if ( msgType == 0x13 &&   weInit && ! s->m_calledCallback ) {
			buf = sbuf; bufSize = sbufSize; }
		if ( msgType == 0x13 && ! weInit ) {
			buf = rbuf; bufSize = rbufSize; }
		if ( buf ) {
			int32_t rdbId = -1;
			if (msgType == 0x01) rdbId = buf[0];
			//else               rdbId = buf[8+sizeof(key_t)*2+16];
			else                 rdbId = buf[24];
			Rdb *rdb = NULL;
			if ( rdbId >= 0 && ! isDns ) 
				rdb = getRdbFromId ((uint8_t)rdbId );
			char *cmd;
			if ( msgType == 0x01 ) cmd = "add to";
			else                   cmd = "get from";
			tt[0] = ' '; tt[1]='\0';
			if ( rdb ) sprintf ( tt , "%s %s" ,
					     cmd,rdb->m_dbname );
			desc = tt;
		}
		if ( msgType == 0x10 ) desc = "add links";
		if ( msgType == 0x0c ) desc = "getting ip";
		if ( msgType == 0x0d ) desc = "get outlink ips/qualities";
		if ( msgType == 0x11 ) desc = "ping";
		if ( msgType == 0x12 ) desc = "get lock";
		if ( msgType == 0x06 ) desc = "spider lock";
		if ( msgType == 0x04 ) desc = "meta add";
		if ( msgType == 0x13 ) {
			char isRobotsTxt = 1;
			if ( buf && bufSize >= 
			     (int32_t)sizeof(Msg13Request)-(int32_t)MAX_URL_LEN ) {
				Msg13Request *r = (Msg13Request *)buf;
				isRobotsTxt = r->m_isRobotsTxt;
			}
			if ( isRobotsTxt ) desc = "get robots.txt";
			else               desc = "get web page";
		}
		if ( msgType == 0x09 ) desc = "add site";
		if ( msgType == 0x08 ) desc = "get site";
		if ( msgType == 0x8b ) desc = "get catid";
		if ( msgType == 0x34 ) desc = "get load";
		if ( msgType == 0x02 ) desc = "get lists";
		if ( msgType == 0x22 ) desc = "get titlerec";
		if ( msgType == 0x36 ) desc = "get termFreq";
		if ( msgType == 0x20 ) desc = "get summary";
		if ( msgType == 0x2c ) desc = "get address";
		if ( msgType == 0x24 ) desc = "get gigabits";
		if ( msgType == 0x39 ) desc = "get docids";
		if ( msgType == 0x17 ) desc = "cache access";
		if ( msgType == 0x23 ) desc = "get linktext";
		if ( msgType == 0x07 ) desc = "inject";
		if ( msgType == 0x35 ) desc = "merge token";
		if ( msgType == 0x3b ) desc = "get docid score";
		if ( msgType == 0x50 ) desc = "get root quality";
		if ( msgType == 0x25 ) desc = "get link info";
		if ( msgType == 0xfd ) desc = "proxy forward";

		char *req = NULL;
		int32_t reqSize = 0;
		if ( s->m_callback ) {
			req = s->m_sendBuf;
			reqSize = s->m_sendBufSize;
		}
		// are we receiving the request?
		else {
			req = s->m_readBuf;
			reqSize = s->m_readBufSize;
			// if not completely read in yet...
			if ( s->hasDgramsToRead ())
				req = NULL;
		}

		SafeBuf tmp;
		char *altText = "";

		// MSG25
		if ( req && msgType == 0x25 ) {
			Msg25Request *mr = (Msg25Request *)req;
			// it doesn't hurt if we call Msg25Request::deserialize
			// again if it has already been called
			mr->deserialize();
			if ( mr->m_mode == 2 ) { // MODE_SITELINKINFO ) {
				tmp.safePrintf(" title=\""
					       "getting site link info for "
					       "%s "
					       "in collnum %i.\n"
					       "sitehash64=%"UINT64" "
					       "waitinginline=%i"
					       "\""
					       ,mr->ptr_site
					       ,(int)mr->m_collnum
					       ,mr->m_siteHash64
					       ,(int)mr->m_waitingInLine
					       );
				desc = "getting site link info";
			}
			else {
				tmp.safePrintf(" title=\""
					       "getting page link info for "
					       "%s "
					       "in collnum %i."
					       "\""
					       ,mr->ptr_url
					       ,(int)mr->m_collnum
					       );
				desc = "getting page link info";
			}
		}

		if ( tmp.getLength() )
			altText = tmp.getBufStart();

		
		p->safePrintf ( "<tr bgcolor=#%s>"
				"<td>%s</td>"  // age
				"<td>%s</td>"  // last read
				"<td>%s</td>"  // last send
				"<td>%"INT32"</td>",  // timeout
				bg ,
				e0 ,
				e1 ,
				e2 ,
				s->m_timeout );

		// now use the ip for dns and hosts
		p->safePrintf("<td>%s:%"UINT32"</td>",
			      iptoa(s->m_ip),(uint32_t)s->m_port);

		char *cf1 = "";
		char *cf2 = "";
		if ( s->m_convertedNiceness ) {
			cf1 = "<font color=red>";
			cf2 = "</font>";
		}

		if ( isDns ) {
			//p->safePrintf("<td>%s</td>",iptoa(s->m_ip));
			char *hostname = s->m_tmpVar;
			p->safePrintf("<td><nobr>%s"
				      ,hostname);
			// get the domain from the hostname
			int32_t dlen;
			char *dbuf = ::getDomFast ( hostname,&dlen,false);
			p->safePrintf(
			      " <a href=\"/admin/tagdb?"
			      "user=admin&"
			      "tagtype0=manualban&"
			      "tagdata0=1&"
			      "u=%s&c=%s\">"
			      "[<font color=red><b>BAN %s</b></font>]"
			      "</nobr></a> " ,
			      dbuf , coll , dbuf );
			p->safePrintf("</td>"
				      "<td>%s%"INT32"%s</td>",
				      cf1,
				      (int32_t)s->m_niceness,
				      cf2);
		}

		if ( ! isDns ) {
			//"<td>%s</td>"  // ip
			//"<td>%hu</td>" // port
			// clickable hostId
			char *toFrom = "to";
			if ( ! s->m_callback ) toFrom = "from";
			//"<td><a href=http://%s:%hu/cgi/15.cgi>%"INT32"</a></td>"
			p->safePrintf (	"<td>0x%hhx</td>"  // msgtype
					"<td%s><nobr>"
					"%s</nobr></td>"  // desc
					"<td><nobr>%s <a href=http://%s:%hu/"
					"admin/sockets?"
					"c=%s>%s</a></nobr></td>"
					"<td>%s%"INT32"%s</td>" , // niceness
					s->m_msgType ,
					altText,
					desc,
					//iptoa(s->m_ip) ,
					//s->m_port ,
					// begin clickable hostId
					toFrom,
					eip     ,
					eport   ,
					coll ,
					ehostId ,
					cf1,
					(int32_t)s->m_niceness,
					cf2
					// end clickable hostId
					);
		}

		char *rf1 = "";
		char *rf2 = "";
		if ( s->m_resendCount ) {
			rf1 = "<b style=color:red;>";
			rf2 = "</b>";
		}
			
		p->safePrintf ( "<td>%"UINT32"</td>" // transId
				"<td>%i</td>" // called handler
				"<td>%"INT32"</td>" // dgrams read
				"<td>%"INT32"</td>" // dgrams to read
				"<td>%"INT32"</td>" // acks sent
				"<td>%"INT32"</td>" // dgrams sent
				"<td>%"INT32"</td>" // dgrams to send
				"<td>%"INT32"</td>" // acks read
				"<td>%s%hhu%s</td>" // resend count
				"</tr>\n" ,
				(uint32_t)s->m_transId,
				calledHandler,
				s->getNumDgramsRead() ,
				s->m_dgramsToRead ,
				s->getNumAcksSent() ,
				s->getNumDgramsSent() ,
				s->m_dgramsToSend ,
				s->getNumAcksRead() ,
				rf1 ,
				s->m_resendCount ,
				rf2
				);
	}
	// end the table
	p->safePrintf ("</table><br>\n" );
}
