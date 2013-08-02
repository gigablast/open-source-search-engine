#include "gb-include.h"

#include "TcpSocket.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Indexdb.h"
#include "sort.h"
#include "Users.h"

static int pingSort1      ( const void *i1, const void *i2 );
static int pingSort2      ( const void *i1, const void *i2 );
static int pingAgeSort    ( const void *i1, const void *i2 );
static int pingMaxSort    ( const void *i1, const void *i2 );
static int slowDiskSort   ( const void *i1, const void *i2 );
static int splitTimeSort  ( const void *i1, const void *i2 );
static int flagSort       ( const void *i1, const void *i2 );
static int resendsSort    ( const void *i1, const void *i2 );
static int errorsSort     ( const void *i1, const void *i2 );
static int tryagainSort   ( const void *i1, const void *i2 );
static int dgramsToSort   ( const void *i1, const void *i2 );
static int dgramsFromSort ( const void *i1, const void *i2 );
//static int loadAvgSort    ( const void *i1, const void *i2 );
static int memUsedSort    ( const void *i1, const void *i2 );
static int cpuUsageSort   ( const void *i1, const void *i2 );

long generatePingMsg( Host *h, long long nowms, char *buffer );

// . returns false if blocked, true otherwise
// . sets errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageHosts ( TcpSocket *s , HttpRequest *r ) {
	// don't allow pages bigger than 128k in cache
	char  buf [ 64*1024 ];
	//char *p    = buf;
	//char *pend = buf + 64*1024;
	SafeBuf sb(buf, 64*1024);
	// check for a sort request
	long sort  = r->getLong ( "sort", -1 );
	// sort by ping times by default now, we are usually always doing that
	if ( sort == -1 ) sort = 1;
	char *coll = r->getString ( "c" );
	//char *pwd  = r->getString ( "pwd" );
	// check for setnote command
	long setnote = r->getLong("setnote", 0);
	long setsparenote = r->getLong("setsparenote", 0);
	// check for replace host command
	long replaceHost = r->getLong("replacehost", 0);
	// check for sync host command
	long syncHost = r->getLong("synchost", 0);
	// set note...
	if ( setnote == 1 ) {
		// get the host id to change
		long host = r->getLong("host", -1);
		if ( host == -1 ) goto skipReplaceHost;
		// get the note to set
		long  noteLen;
		char *note = r->getString("note", &noteLen, "", 0);
		// set the note
		g_hostdb.setNote(host, note, noteLen);
	}
	// set spare note...
	if ( setsparenote == 1 ) {
		// get the host id to change
		long spare = r->getLong("spare", -1);
		if ( spare == -1 ) goto skipReplaceHost;
		// get the note to set
		long  noteLen;
		char *note = r->getString("note", &noteLen, "", 0);
		// set the note
		g_hostdb.setSpareNote(spare, note, noteLen);
	}
	// replace host...
	if ( replaceHost == 1 ) {
		// get the host ids to swap
		long rhost = r->getLong("rhost", -1);
		long rspare = r->getLong("rspare", -1);
		if ( rhost == -1 || rspare == -1 )
			goto skipReplaceHost;
		// replace
		g_hostdb.replaceHost(rhost, rspare);
	}
	// sync host...
	if ( syncHost == 1 ) {
		// get the host id to sync
		long syncHost = r->getLong("shost", -1);
		if ( syncHost == -1 ) goto skipReplaceHost;
		// call sync
		g_hostdb.syncHost(syncHost, false);
		//g_syncdb.syncHost ( syncHost );
	}
	if ( syncHost == 2 ) {
		// get the host id to sync
		long syncHost = r->getLong("shost", -1);
		if ( syncHost == -1 ) goto skipReplaceHost;
		// call sync
		g_hostdb.syncHost(syncHost, true);
		//g_syncdb.syncHost ( syncHost );
	}

skipReplaceHost:

	long refreshRate = r->getLong("rr", 0);
	if(refreshRate > 0) 
		sb.safePrintf("<META HTTP-EQUIV=\"refresh\" "
			      "content=\"%li\"\\>", 
			      refreshRate);

	// ignore
	char *username = g_users.getUsername ( r );
	char *password = NULL;
	User *user = NULL;
	if ( username ) user = g_users.getUser (username );
	if ( user     ) password = user->m_password;
	if ( ! password ) password = "";
	if ( ! username ) username = "";

	// print standard header
	// 	char *pp    = sb.getBuf();
	// 	char *ppend = sb.getBufEnd();
	// 	if ( pp ) {
	g_pages.printAdminTop ( &sb , s , r );
	//	sb.incrementLength ( pp - sb.getBuf() );
	//	}
	char *colspan = "30";
	//char *shotcol = "";
	char shotcol[1024];
	shotcol[0] = '\0';
	if ( g_conf.m_useShotgun ) {
		colspan = "31";
		//shotcol = "<td><b>ip2</b></td>";
		sprintf ( shotcol, "<td><a href=\"/master/hosts?c=%s"
			 	   "&sort=2&username=%s&pwd=%s\">"
			  "<b>ping2</b></td></a>",
			  coll,username,password);
	}

	// print host table
	sb.safePrintf ( 
		  "<table cellpadding=4 border=1 width=100%% bgcolor=#%s>" 
		  "<tr><td colspan=%s bgcolor=#%s><center>"
		  //"<font size=+1>"
		  "<b>Hosts "
		  "(<a href=\"/master/hosts?c=%s&sort=%li&reset=1\">"
		  "reset)</b>"
		  //"</font>"
		  "</td></tr>" 
		  "<tr>"
		  "<td><a href=\"/master/hosts?c=%s&sort=0&username=%s&"
		  "password=%s\">"
		  "<b>hostId</b></td>"
		  "<td><b>host name</b></td>"
		  "<td><b>mirror group</b></td>"
		  "<td><b>stripe</b></td>"

		  // i don't remember the last time i used this, so let's
		  // just comment it out to save space
		  //"<td><b>group mask</td>"

		  //"<td><b>ip1</td>"
		  //"<td><b>ip2</td>"
		  //"<td><b>udp port</td>"

		  // this is now more or less obsolete
		  //"<td><b>priority udp port</td>"

		  //"<td><b>dns client port</td>"
		  "<td><b>http port</td>"

		  // this is now obsolete since ide channel is. it was used
		  // so that only the guy with the token could merge,
		  // and it made sure that only one merge per ide channel
		  // and per group was going on at any one time for performance
		  // reasons.
		  //"<td><b>token group</td>"

		  //"<td><b>best switch id</td>"
		  //"<td><b>actual switch id</td>"
		  //"<td><b>switch id</td>"

		  // this is now fairly obsolete
		  //"<td><b>ide channel</td>"

		  "<td><b>HD temps (C)</b></td>"

		  //"<td><b>resends sent</td>"
		  //"<td><b>errors recvd</td>"
		  //"<td><b>ETRYAGAINS recvd</td>"
		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=3\">"
		  "<b>dgrams resent</a></td>"
		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=4\">"
		  "<b>errors recvd</a></td>"
		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=5\">"
		  "<b>ETRY AGAINS recvd</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=6\">"
		  "<b>dgrams to</a></td>"
		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=7\">"
		  "<b>dgrams from</a></td>"

		  //"<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=8\">"
		  //"<b>loadavg</a></td>"


		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=13\">"
		  "<b>avg split time</a></td>"

		  "<td><b>splits done</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=12\">"
		  "<b>status</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=15\">"
		  "<b>slow reads</a></td>"

		  "<td><b>docs indexed</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=9\">"
		  "<b>mem used</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=10\">"
		  "<b>cpu</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=14\">"
		  "<b>max ping1</a></td>"

		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=11\">"
		  "<b>ping1 age</a></td>"

		  //"<td><b>ip1</td>"
		  "<td><a href=\"/master/hosts?c=%s&username=%s&pwd=%s&sort=1\">"
		  "<b>ping1</a></td>"

		  "%s"// "<td><b>ip2</td>"
		  //"<td><b>inSync</td>",
		  //"<td>avg roundtrip</td>"
		  //"<td>std. dev.</td></tr>"
		  "<td><b>note</td>",
		  LIGHT_BLUE ,
		  colspan    ,
		  DARK_BLUE  ,
		  coll, sort,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  //coll,username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  coll, username, password,
		  shotcol    );

	// loop through each host we know and print it's stats
	long nh = g_hostdb.getNumHosts();
	// should we reset resends, errorsRecvd and ETRYAGAINS recvd?
	if ( r->getLong("reset",0) ) {
		for ( long i = 0 ; i < nh ; i++ ) {
			// get the ith host (hostId)
			Host *h = g_hostdb.getHost ( i );
			h->m_totalResends   = 0;
			h->m_errorReplies = 0;
			h->m_etryagains   = 0;
			h->m_dgramsTo     = 0;
			h->m_dgramsFrom   = 0;
		}
	}

	// sort hosts if needed
	long hostSort [ MAX_HOSTS ];
	for ( long i = 0 ; i < nh ; i++ )
		hostSort [ i ] = i;
	switch ( sort ) {
	case 1: gbsort ( hostSort, nh, sizeof(long), pingSort1      ); break;
	case 2: gbsort ( hostSort, nh, sizeof(long), pingSort2      ); break;
	case 3: gbsort ( hostSort, nh, sizeof(long), resendsSort    ); break;
	case 4: gbsort ( hostSort, nh, sizeof(long), errorsSort     ); break;
	case 5: gbsort ( hostSort, nh, sizeof(long), tryagainSort   ); break;
	case 6: gbsort ( hostSort, nh, sizeof(long), dgramsToSort   ); break;
	case 7: gbsort ( hostSort, nh, sizeof(long), dgramsFromSort ); break;
	//case 8: gbsort ( hostSort, nh, sizeof(long), loadAvgSort    ); break;
	case 9: gbsort ( hostSort, nh, sizeof(long), memUsedSort    ); break;
	case 10:gbsort ( hostSort, nh, sizeof(long), cpuUsageSort   ); break;
	case 11:gbsort ( hostSort, nh, sizeof(long), pingAgeSort    ); break;
	case 12:gbsort ( hostSort, nh, sizeof(long), flagSort       ); break;
	case 13:gbsort ( hostSort, nh, sizeof(long), splitTimeSort  ); break;
	case 14:gbsort ( hostSort, nh, sizeof(long), pingMaxSort    ); break;
	case 15:gbsort ( hostSort, nh, sizeof(long), slowDiskSort    ); break;
	}

	// we are the only one that uses these flags, so set them now
	/*
	static char s_properSet = 0;
	if ( ! s_properSet ) {
		s_properSet = 1;
		g_hostdb.setOnProperSwitchFlags();
	}
	*/

	long long nowmsLocal = gettimeofdayInMillisecondsLocal();

	// print it
	//long ng = g_hostdb.getNumGroups();
	for ( long si = 0 ; si < nh ; si++ ) {
		long i = hostSort[si];
		// get the ith host (hostId)
		Host *h = g_hostdb.getHost ( i );
		// get avg/stdDev msg roundtrip times in ms for ith host
		//long avg , stdDev;
		//g_hostdb.getTimes ( i , &avg , &stdDev );
                char ptr[256];
                long pingAge = generatePingMsg(h, nowmsLocal, ptr);
		char pms[64];
		if ( h->m_pingMax < 0 ) sprintf(pms,"???");
		else                    sprintf(pms,"%lims",h->m_pingMax);
		// the sync status ascii-ized
		char syncStatus = h->m_syncStatus;
		char *ptr2;
		if      (syncStatus==0) 
			ptr2 ="<b>N</b>";
		else if (syncStatus==1) 
			ptr2 ="Y";
		else 
			ptr2 ="?";
		char ipbuf1[64];
		char ipbuf2[64];
		strcpy(ipbuf1,iptoa(h->m_ip));
		strcpy(ipbuf2,iptoa(h->m_ipShotgun));

		char  hdbuf[128];
		char *hp = hdbuf;
		for ( long k = 0 ; k < 4 ; k++ ) {
			long temp = h->m_hdtemps[k];
			if ( temp > 50 )
				hp += sprintf(hp,"<font color=red><b>%li"
					      "</b></font>",
					      temp);
			else
				hp += sprintf(hp,"%li",temp);
			if ( k < 3 ) *hp++ = '/';
			*hp = '\0';
		}

		//long switchGroup = 0;
		//if ( g_hostdb.m_indexSplits > 1 )
		//	switchGroup = h->m_group%g_hostdb.m_indexSplits;

		// the switch id match
		//char tmpN[256];
		//if ( ! h->m_onProperSwitch )
		//	sprintf(tmpN, "<font color=#ff0000><b>%li</b></font>",
		//		(long)h->m_switchId);
		//else
		//	sprintf(tmpN, "%li", (long)h->m_switchId);

		// host can have 2 ip addresses, get the one most
		// similar to that of the requester
		long eip = g_hostdb.getBestIp ( h , s->m_ip );
		char ipbuf3[64];
		strcpy(ipbuf3,iptoa(eip));

		char *fontTagFront = "";
		char *fontTagBack  = "";
		if ( h->m_percentMemUsed >= 98.0 ) {
			fontTagFront = "<font color=red>";
			fontTagBack  = "</font>";
		}

		float cpu = h->m_cpuUsage;
		if ( cpu > 100.0 ) cpu = 100.0;
		if ( cpu < 0.0   ) cpu = -1.0;

		// split time, don't divide by zero!
		long splitTime = 0;
		if ( h->m_splitsDone ) 
			splitTime = h->m_splitTimes / h->m_splitsDone;

		char flagString[32];
		char *fs = flagString;
		*fs = '\0';
		// if it has spiders going on say "S"
		if ( h->m_flags & PFLAG_HASSPIDERS )
			strcat ( fs , "S");
		// say "M" if merging
		if (   h->m_flags & PFLAG_MERGING )
			strcat ( fs , "M");
		// say "D" if dumping
		if (   h->m_flags & PFLAG_DUMPING )
			strcat ( fs , "D");
		// say "y" if doing the daily merge
		if (  !(h->m_flags & PFLAG_MERGEMODE0) )
			strcat ( fs , "y");
		// clear it if it is us, this is invalid
		if ( ! h->m_gotPingReply )
			strcpy(flagString,"??");
		if ( fs[0] == '\0' )
			strcpy(flagString,"&nbsp;");

		// print it
		sb.safePrintf (
			  "<tr>"
			  "<td><a href=\"http://%s:%hi/master/hosts?"
			  "username=%s&pwd=%s&"
			  "c=%s"
			  "&sort=%li\">%li</a></td>"

			  "<td>%s</td>" // hostname

			  "<td>%li</td>" // group
			  "<td>%li</td>" // stripe
			  //"<td>0x%08lx</td>" // group mask

			  //"<td>%s</td>" // ip1
			  //"<td>%s</td>" // ip2
			  //"<td>%hi</td>" // port
			  //"<td>%hi</td>" // client port
			  "<td>%hi</td>" // http port
			  //"<td>%li</td>" // token group num
			  //"<td>%li</td>" // switch group
			  //"<td>%s</td>" // tmpN
			  //"<td>%li</td>" // ide channel

			  // hd temps
			  "<td>%s</td>"

			  // resends
			  "<td>%li</td>"
			  // error replies
			  "<td>%li</td>"
			  // etryagains
			  "<td>%li</td>"

			  // # dgrams sent to
			  "<td>%lli</td>"
			  // # dgrams recvd from
			  "<td>%lli</td>"

			  // loadavg
			  //"<td>%.2f</td>"

			  // split time
			  "<td>%li</td>"
			  // splits done
			  "<td>%li</td>"

			  // flags
			  "<td>%s</td>"

			  // slow disk reads
			  "<td>%li</td>"

			  // docs indexed
			  "<td>%li</td>"

			  // percent mem used
			  "<td>%s%.1f%%%s</td>"
			  // cpu usage
			  "<td>%.1f%%</td>"

			  // ping max
			  "<td>%s</td>"

			  // ping age
			  "<td>%lims</td>"

			  // ping
			  "<td>%s</td>"
			  //"<td>%s</td>"
			  //"<td>%lims</td>"
			  "<td nowrap=1>%s</td>"
			  "</tr>" , 
			  ipbuf3, h->m_httpPort, 
			  username, password,
			  coll, sort,
			  i , 
			  h->m_hostname,
			  h->m_group,
			  h->m_stripe,
			  // group mask is not looked at a lot and is
			  // really only for indexdb and a few other rdbs
			  //g_hostdb.makeGroupId(i,ng) ,
			  //ipbuf1,
			  //ipbuf2,
			  //h->m_port , 
			  //h->m_dnsClientPort ,
			  h->m_httpPort ,
			  //h->m_tokenGroupNum,
			  //switchGroup ,
			  //tmpN,
			  //h->m_ideChannel,
			  hdbuf,
			  h->m_totalResends,
			  h->m_errorReplies,
			  h->m_etryagains,

			  h->m_dgramsTo,
			  h->m_dgramsFrom,

			  //h->m_loadAvg, // double
			  splitTime,
			  h->m_splitsDone,

			  flagString,

			  h->m_slowDiskReads,
			  h->m_docsIndexed,

			  fontTagFront,
			  h->m_percentMemUsed, // float
			  fontTagBack,
			  cpu, // float

			  // ping max
			  pms,
			  // ping age
			  pingAge,

			  //avg , 
			  //stdDev,
			  //ping,
			  ptr ,
			  //ptr2 ,
			  h->m_note );
	}
	// end the table now
	sb.safePrintf ( "</table><br>\n" );

	// print spare hosts table
	sb.safePrintf ( 
		  "<table cellpadding=4 border=1 width=100%% bgcolor=#%s>" 
		  "<tr><td colspan=10 bgcolor=#%s><center>"
		  //"<font size=+1>"
		  "<b>Spares</b>"
		  //"</font>"
		  "</td></tr>" 
		  "<tr>"
		  "<td><b>spareId</td>"
		  "<td><b>host name</td>"
		  "<td><b>ip1</td>"
		  "<td><b>ip2</td>"
		  //"<td><b>udp port</td>"
		  //"<td><b>priority udp port</td>"
		  //"<td><b>dns client port</td>"
		  "<td><b>http port</td>"
		  //"<td><b>switch id</td>"

		  // this is now fairly obsolete
		  //"<td><b>ide channel</td>"

		  "<td><b>note</td>",
		  LIGHT_BLUE ,
		  DARK_BLUE  );

	for ( long i = 0; i < g_hostdb.m_numSpareHosts; i++ ) {
		// get the ith host (hostId)
		Host *h = g_hostdb.getSpare ( i );

		char ipbuf1[64];
		char ipbuf2[64];
		strcpy(ipbuf1,iptoa(h->m_ip));
		strcpy(ipbuf2,iptoa(h->m_ipShotgun));

		// print it
		sb.safePrintf (
			  "<tr>"
			  "<td>%li</td>"
			  "<td>%s</td>"
			  "<td>%s</td>"
			  "<td>%s</td>"
			  //"<td>%hi</td>"
			  //"<td>%hi</td>" // priority udp port
			  //"<td>%hi</td>"
			  "<td>%hi</td>"
			  //"<td>%i</td>" // switch id
			  //"<td>%li</td>" // ide channel
			  "<td>%s</td>"
			  "</tr>" , 
			  i , 
			  h->m_hostname,
			  ipbuf1,
			  ipbuf2,
			  //h->m_port , 
			  //h->m_port2 , 
			  //h->m_dnsClientPort ,
			  h->m_httpPort ,
			  //h->m_switchId,
			  //h->m_ideChannel ,
			  h->m_note );
	}
	sb.safePrintf ( "</table><br>" );

	// print proxy hosts table
	sb.safePrintf ( 
		  "<table cellpadding=4 border=1 width=100%% bgcolor=#%s>" 
		  "<tr><td colspan=12 bgcolor=#%s><center>"
		  //"<font size=+1>"
		  "<b>Proxies</b>"
		  //"</font>"
		  "</td></tr>" 
		  "<tr>"
		  "<td><b>proxyId</b></td>"
		  "<td><b>type</b></td>"
		  "<td><b>host name</b></td>"
		  "<td><b>ip1</b></td>"
		  "<td><b>ip2</b></td>"
		  //"<td><b>udp port</td>"

		  //"<td><b>priority udp port</td>"

		  //"<td><b>dns client port</td>"
		  "<td><b>http port</b></td>"
		  //"<td><b>switch id</td>"
                  "<td><b>max ping1</b></td>"
                  "<td><b>ping1 age</b></td>"
                  "<td><b>ping1</b></td>"
		  //"<td><b>ping2</b></td>"
		  // this is now fairly obsolete
		  //"<td><b>ide channel</td>"

		  "<td><b>note</td>",
		  LIGHT_BLUE ,
		  DARK_BLUE  );
	for ( long i = 0; i < g_hostdb.m_numProxyHosts; i++ ) {
		// get the ith host (hostId)
		Host *h = g_hostdb.getProxy ( i );

                char ptr[256];
                long pingAge = generatePingMsg(h, nowmsLocal, ptr);

		char ipbuf1[64];
		char ipbuf2[64];
		strcpy(ipbuf1,iptoa(h->m_ip));
		strcpy(ipbuf2,iptoa(h->m_ipShotgun));

		// host can have 2 ip addresses, get the one most
		// similar to that of the requester
		long eip = g_hostdb.getBestIp ( h , s->m_ip );
		char ipbuf3[64];
		strcpy(ipbuf3,iptoa(eip));


		char pms[64];
		if ( h->m_pingMax < 0 ) sprintf(pms,"???");
		else                    sprintf(pms,"%lims",h->m_pingMax);
		// the sync status ascii-ized

		char *type = "proxy";
		if ( h->m_type == HT_QCPROXY ) type = "qcproxy";
		if ( h->m_type == HT_SCPROXY ) type = "scproxy";

		// print it
		sb.safePrintf (
			  "<tr>"

			  "<td><a href=\"http://%s:%hi/master/hosts?"
			  "username=%s&pwd=%s&"
			  "c=%s\">"
			  "%li</a></td>"

			  "<td>%s</td>"
			  "<td>%s</td>"
			  "<td>%s</td>"
			  "<td>%s</td>"
			  //"<td>%hi</td>"
			  //"<td>%hi</td>" // priority udp port
			  //"<td>%hi</td>"
			  "<td>%hi</td>"
			  //"<td>%i</td>" // switch id
			  "<td>%s</td>" // ping max
			  "<td>%ldms</td>" // ping age
			  "<td>%s</td>" // ping
			  //"<td>%li</td>" // ide channel
			  "<td>%s </td>"
			  "</tr>" , 

			  ipbuf3,
			  h->m_httpPort,
			  username,
			  password,
			  coll,
			  i , 

			  type,
			  h->m_hostname,
			  ipbuf1,
			  ipbuf2,
			  //h->m_port , 
			  //h->m_port2 , 
			  //h->m_dnsClientPort ,
			  h->m_httpPort ,
			  //h->m_switchId,
			  pms,
                          pingAge,
                          ptr,
			  //h->m_ideChannel ,
			  h->m_note );
	}
	sb.safePrintf ( "</table><br><br>" );

	// print help table
	sb.safePrintf ( 
		  "<table cellpadding=4 border=1 width=100%% bgcolor=#%s>" 
		  "<tr><td colspan=10 bgcolor=#%s><center>"
		  //"<font size=+1>"
		  "<b>Key</b>"
		  //"</font>"
		  "</td></tr>" 

		  "<tr>"
		  "<td>mirror group</td>"
		  "<td>"
		  "The active hosts are divded into groups. These are "
		  "mirror groups. A host in group X ideally has exactly "
		  "the same data as any other host in group X."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>stripe</td>"
		  "<td>"
		  "Each stripe is a set of hosts, one from each mirror "
		  "group. Each strip is basically a complete and independent "
		  "search engine index. Although some functionality, like "
		  "summary generation, is generally distributed across "
		  "stripe boundaries."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>ip1</td>"
		  "<td>The primary IP address of the host."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>ip2</td>"
		  "<td>The secondary IP address of the host."
		  "</td>"
		  "</tr>\n"

		  /*
		  "<tr>"
		  "<td>udp port</td>"
		  "<td>The UDP port the host uses to send and recieve "
		  "datagrams."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>dns client port</td>"
		  "<td>The UDP port used to send and receive dns traffic with."
		  "</td>"
		  "</tr>\n"
		  */

		  "<tr>"
		  "<td>http port</td>"
		  "<td>The port you can connect a browser to."
		  "</td>"
		  "</tr>\n"

		  /*
		  "<tr>"
		  "<td>best switch id</td>"
		  "<td>The host prefers to be on this switch because it "
		  "needs to send a lot of data to other hosts on this swtich. "
		  "Therefore, ideally, the best switch id should match the "
		  "actual switch id for optimal performance."
		  "</td>"
		  "</tr>\n"
		  */

		  /*
		  "<tr>"
		  "<td>switch id</td>"
		  "<td>Hosts that share the same switch id are "
		  "physically on the same switch."
		  "</td>"
		  "</tr>\n"
		  */

		  "<tr>"
		  "<td>dgrams resent</td>"
		  "<td>How many datagrams have had to be resent to a host "
		  "because it was not ACKed quick enough or because it was "
		  "fully ACKed but the entire request was resent in case "
		  "the host was reset."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>errors recvd</td>"
		  "<td>How many errors were received from a host in response "
		  "to a request to retrieve or insert data."
		  "</td>"
		  "</tr>\n"


		  "<tr>"
		  "<td>ETRYAGAINS recvd</td>"
		  "<td>How many ETRYAGAIN were received in response to a "
		  "request to add data. Usually because the host's memory "
		  "is full and it is dumping its data to disk. This number "
		  "can be high if the host if failing to dump the data "
		  "to disk because of some malfunction, and it can therefore "
		  "bottleneck the entire cluster."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>dgrams to</td>"
		  "<td>How many datagrams were sent to the host from the "
		  "selected host since startup. Includes ACK datagrams. This "
		  "can actually be higher than the number of dgrams read "
		  "when the selected host is the same as the host in the "
		  "table because of resends. Gigablast will resend datagrams "
		  "that are not promptly ACKknowledged."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>dgrams from</td>"
		  "<td>How many datagrams were received from the host by the "
		  "selected host since startup. Includes ACK datagrams."
		  "</td>"
		  "</tr>\n"

		  //"<tr>"
		  //"<td>loadavg</td>"
		  //"<td>1-minute sliding-window load average from "
		  //"/proc/loadavg."
		  //"</td>"
		  //"</tr>\n"

		  "<tr>"
		  "<td>mem used</td>"
		  "<td>percentage of memory currently used."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>cpu usage</td>"
		  "<td>percentage of cpu resources in use by the gb process."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>ping1 age</td>"
		  "<td>How long ago the last ping request was sent to "
		  "this host. Let's us know how fresh the ping time is."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>ping1</td>"
		  "<td>Ping time to this host on the primary network."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>ping2</td>"
		  "<td>Ping time to this host on the seconday/shotgun "
		  "network. This column is not visible if the shotgun "
		  "network is not enabled in the master controls."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>M (status flag)</td>"
		  "<td>Indicates host is merging files on disk."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>D (status flag)</td>"
		  "<td>Indicates host is dumping data to disk."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>S (status flag)</td>"
		  "<td>Indicates host has outstanding spiders."
		  "</td>"
		  "</tr>\n"

		  "<tr>"
		  "<td>y (status flag)</td>"
		  "<td>Indicates host is performing the daily merge."
		  "</td>"
		  "</tr>\n"


		  ,
		  LIGHT_BLUE ,
		  DARK_BLUE  );

	sb.safePrintf ( "</table><br></form><br>" );

	//p = g_pages.printAdminBottom ( p , pend );

	// calculate buffer length
	//long bufLen = p - buf;
	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage ( s , (char*) sb.getBufStart() ,
						  sb.length() );
}

long generatePingMsg( Host *h, long long nowms, char *buf ) {
        long ping = h->m_ping;
        // show ping age first
        long pingAge = nowms- h->m_lastPing;
        // if host is us, we don't ping ourselves
        if ( h->m_hostId == g_hostdb.m_hostId && h == g_hostdb.m_myHost) 
                pingAge = 0; 
        // if last ping is still 0, we haven't pinged it yet
        if ( h->m_lastPing == 0 ) pingAge = 0;
        // ping to string
        sprintf ( buf , "%lims", ping );
        // ping time ptr
        // make it "DEAD" if > 6000
        if ( ping >= g_conf.m_deadHostTimeout ) {
                // mark SYNC if doing a sync
                if ( h->m_doingSync )
                        sprintf(buf, "<font color=#ff8800><b>SYNC</b></font>");
                else
                        sprintf(buf, "<font color=#ff0000><b>DEAD</b></font>");
        }
        // for kernel errors
        else if ( h->m_kernelErrors > 0 ){
                if ( h->m_kernelErrors == ME_IOERR )
                        sprintf(buf, "<font color=#ff0080><b>IOERR"
                                "</b></font>");
                else if ( h->m_kernelErrors == ME_100MBPS )
                        sprintf(buf, "<font color=#ff0080><b>100MBPS"
                                "</b></font>");
                else
                        sprintf(buf, "<font color=#ff0080><b>KERNELERR"
                                "</b></font>");
        }

	if ( ! g_conf.m_useShotgun ) return pingAge;

	char *p = buf + gbstrlen(buf);

	p += sprintf ( p , "</td><td>" );

        // the second eth port, ip2, the shotgun port
        long pingB = h->m_pingShotgun;
        sprintf ( p , "%lims", pingB );
        if ( pingB >= g_conf.m_deadHostTimeout ) {
                // mark SYNC if doing a sync
                if ( h->m_doingSync )
                        sprintf(p,"<font color=#ff8800><b>SYNC</b></font>");
                else
                        sprintf(p,"<font color=#ff0000><b>DEAD</b></font>");
		return pingAge;
        }

        return pingAge;
}

int pingSort1    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	// float up to the top if the host is reporting kernel errors
	// even if the ping is normal
	if ( h1->m_kernelErrors  > 0 ) return -1;
	if ( h2->m_kernelErrors  > 0 ) return  1;
	if ( h1->m_ping > h2->m_ping ) return -1;
	if ( h1->m_ping < h2->m_ping ) return  1;
	return 0;
}

int pingSort2    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	// float up to the top if the host is reporting kernel errors
	// even if the ping is normal
	if ( h1->m_kernelErrors  > 0 ) return -1;
	if ( h2->m_kernelErrors  > 0 ) return  1;
	if ( h1->m_pingShotgun > h2->m_pingShotgun ) return -1;
	if ( h1->m_pingShotgun < h2->m_pingShotgun ) return  1;
	return 0;
}

int pingMaxSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_pingMax > h2->m_pingMax ) return -1;
	if ( h1->m_pingMax < h2->m_pingMax ) return  1;
	return 0;
}

int slowDiskSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_slowDiskReads > h2->m_slowDiskReads ) return -1;
	if ( h1->m_slowDiskReads < h2->m_slowDiskReads ) return  1;
	return 0;
}

int pingAgeSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_lastPing > h2->m_lastPing ) return -1;
	if ( h1->m_lastPing < h2->m_lastPing ) return  1;
	return 0;
}

int splitTimeSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	long t1 = 0;
	long t2 = 0;
	if ( h1->m_splitsDone > 0 ) t1 = h1->m_splitTimes / h1->m_splitsDone;
	if ( h2->m_splitsDone > 0 ) t2 = h2->m_splitTimes / h2->m_splitsDone;
	if ( t1 > t2 ) return -1;
	if ( t1 < t2 ) return  1;
	return 0;
}

int flagSort    ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_flags > h2->m_flags ) return -1;
	if ( h1->m_flags < h2->m_flags ) return  1;
	return 0;
}

int resendsSort  ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_totalResends > h2->m_totalResends ) return -1;
	if ( h1->m_totalResends < h2->m_totalResends ) return  1;
	return 0;
}

int errorsSort   ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_errorReplies > h2->m_errorReplies ) return -1;
	if ( h1->m_errorReplies < h2->m_errorReplies ) return  1;
	return 0;
}

int tryagainSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_etryagains > h2->m_etryagains ) return -1;
	if ( h1->m_etryagains < h2->m_etryagains ) return  1;
	return 0;
}

int dgramsToSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_dgramsTo > h2->m_dgramsTo ) return -1;
	if ( h1->m_dgramsTo < h2->m_dgramsTo ) return  1;
	return 0;
}


int dgramsFromSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_dgramsFrom > h2->m_dgramsFrom ) return -1;
	if ( h1->m_dgramsFrom < h2->m_dgramsFrom ) return  1;
	return 0;
}

/*
int loadAvgSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_loadAvg > h2->m_loadAvg ) return -1;
	if ( h1->m_loadAvg < h2->m_loadAvg ) return  1;
	return 0;
}
*/

int memUsedSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_percentMemUsed > h2->m_percentMemUsed ) return -1;
	if ( h1->m_percentMemUsed < h2->m_percentMemUsed ) return  1;
	return 0;
}

int cpuUsageSort ( const void *i1, const void *i2 ) {
	Host *h1 = g_hostdb.getHost ( *(long*)i1 );
	Host *h2 = g_hostdb.getHost ( *(long*)i2 );
	if ( h1->m_cpuUsage > h2->m_cpuUsage ) return -1;
	if ( h1->m_cpuUsage < h2->m_cpuUsage ) return  1;
	return 0;
}
