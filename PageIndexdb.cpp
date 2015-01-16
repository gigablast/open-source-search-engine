#include "gb-include.h"

#include "Indexdb.h"     // makeKey(int64_t docId)
#include "Msg0.h"
#include "Msg1.h"
#include "IndexList.h"
#include "Query.h"
#include "Titledb.h"
#include "Msg36.h"
#include "Collectiondb.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Datedb.h"
#include "Users.h"

// TODO: meta redirect tag to host if hostId not ours
static void gotTermFreqWrapper  ( void *state ) ;
static bool gotTermFreq         ( class State10 *st ) ;
static void gotIndexListWrapper ( void *state );//, RdbList *list );
static bool gotIndexList        ( void *state );
//static bool gotIndexList2       ( void *state , RdbList *list );
static void addedKeyWrapper     ( void *state );
static bool launchRequests      ( class State10 *st ) ;

// our state class
class State10 {
public:
	Msg0      m_msg0;
	Msg1      m_msg1;
	IndexList m_list;
	//IndexList m_list2;
	collnum_t m_collnum;
	char      m_query[MAX_QUERY_LEN+1];
	int32_t      m_queryLen;
	//char      m_coll[MAX_COLL_LEN+1];
	//int32_t      m_collLen;
	char     *m_coll;
	char      m_pwd[32];
	bool      m_useTree;
	bool      m_useDisk;
	bool      m_useCache;
	bool      m_add;
	bool      m_del;
	int64_t m_termId;
	int32_t      m_numRecs;
	TcpSocket *m_socket;
	HttpRequest m_r;
	bool      m_isMasterAdmin;
	bool      m_isLocal;
	Msg36     m_msg36;    // term freqs (term popularity)
	int64_t m_termFreq;
	int64_t m_docId;
	unsigned char m_score;
	key_t         m_key;
	RdbList       m_keyList;
	bool          m_useDatedb;
	int32_t          m_i;
	SafeBuf       m_pbuf;
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the config of this host
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageIndexdb ( TcpSocket *s , HttpRequest *r ) {
	// . get fields from cgi field of the requested url
	// . get the search query
	int32_t  queryLen = 0;
	char *query = r->getString ( "q" , &queryLen , NULL /*default*/);
	// ensure query not too big
	if ( queryLen >= MAX_QUERY_LEN ) { 
		g_errno = EQUERYTOOBIG; 
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	// get the collection
	int32_t  collLen = 0;
	char *coll    = r->getString("c",&collLen);
	if ( ! coll || ! coll[0] ) {
		//coll    = g_conf.m_defaultColl;
		coll = g_conf.getDefaultColl( r->getHost(), r->getHostLen() );
		collLen = gbstrlen(coll);
	}
	// ensure collection not too big
	if ( collLen >= MAX_COLL_LEN ) { 
		g_errno = ECOLLTOOBIG; 
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	}
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( ! cr ) {
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno)); 
	}
	// make a state
	State10 *st ;
	try { st = new (State10); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageIndexdb: new(%i): %s", 
		    (int)sizeof(State10),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State10) , "PageIndexdb" );
	// password, too
	int32_t pwdLen = 0 ;
	char *pwd = r->getString ( "pwd" , &pwdLen );
	if ( pwdLen > 31 ) pwdLen = 31;
	if ( pwdLen > 0 ) strncpy ( st->m_pwd , pwd , pwdLen );
	st->m_pwd[pwdLen]='\0';
	// get # of records to retreive from IndexList
	st->m_numRecs  = r->getLong ( "numRecs" , 100 );
	// use disk, tree, or cache?
	st->m_useDisk  = r->getLong ("ud" , 0 );
	st->m_useTree  = r->getLong ("ut" , 0 );
	st->m_useCache = r->getLong ("uc" , 0 );
	st->m_useDatedb= r->getLong ("ub" , 0 );
	st->m_add      = r->getLong ("add", 0 );
	st->m_del      = r->getLong ("del", 0 );
	// get the termId, if any, from the cgi vars
	st->m_termId = r->getLongLong ("t", 0LL ) ;
	// get docid and score
	st->m_docId  = r->getLongLong ("d", 0LL );
	st->m_score  = r->getLong ("score", 0 );
	// copy query/collection
	gbmemcpy ( st->m_query , query , queryLen );
	st->m_queryLen = queryLen;
	st->m_query [ queryLen ] ='\0';
	//gbmemcpy ( st->m_coll , coll , collLen );
	//st->m_collLen  = collLen;
	//st->m_coll [ collLen ] ='\0';
	st->m_coll = coll;
	st->m_collnum = cr->m_collnum;
	// save the TcpSocket
	st->m_socket = s;
	// and if the request is local/internal or not
	st->m_isMasterAdmin = g_conf.isCollAdmin ( s , r );
	st->m_isLocal = r->isLocal();
	st->m_r.copy ( r );
	// . check for add/delete request
	if ( st->m_add || st->m_del ) {
		key_t startKey = g_indexdb.makeStartKey ( st->m_termId );
		key_t endKey   = g_indexdb.makeEndKey   ( st->m_termId );
		// construct the key to add/delete
		st->m_key = g_indexdb.makeKey ( st->m_termId,
						st->m_score ,
						st->m_docId ,
						st->m_del   );
		// make an RdbList out of the key
		st->m_keyList.set ( (char*)&st->m_key,
				    sizeof(key_t),
				    (char*)&st->m_key,
				    sizeof(key_t),
				    startKey,
				    endKey,
				    0,
				    false,
				    true  );
		log ( LOG_INFO, "build: adding indexdb key to indexdb: "
				"%"XINT32" %"XINT64"", 
		      st->m_key.n1, st->m_key.n0 );
		// call msg1 to add/delete key
		if ( ! st->m_msg1.addList ( &st->m_keyList,
					     RDB_INDEXDB,
					     st->m_collnum,
					     st,
					     addedKeyWrapper,
					     false,
					     MAX_NICENESS ) )
			return false;
		// continue to page if no block
		return gotIndexList ( st );
	}

	if ( ! st->m_query[0] ) return gotIndexList(st);

	// . set query class
	// . a boolFlag of 0 means query is not boolean
	Query q;
	q.set2 ( query , langUnknown , true ); // 0 = boolFlag, not boolean!
	// reset 
	st->m_msg36.m_termFreq = 0LL;
	// if query was provided, use that, otherwise use termId
	if ( q.getNumTerms() > 0 ) st->m_termId = q.getTermId(0);
	// skip if nothing
	else return gotTermFreq ( st );
	// get the termfreq of this term!
	if ( ! st->m_msg36.getTermFreq ( st->m_collnum ,
					 0 , 
					 st->m_termId,
					 st ,
					 gotTermFreqWrapper ) ) return false;
	// otherwise, we didn't block
	return gotTermFreq ( st );
}

void gotTermFreqWrapper ( void *state ) {
	gotTermFreq( (State10 *) state );
}

bool gotTermFreq ( State10 *st ) {
	// set the term freq
	st->m_termFreq = st->m_msg36.getTermFreq();
	// reset
	st->m_i = 0;
	// . query each indexdb/datedb split
	// . returns false if blocked, true otherwise
	if ( ! launchRequests ( st ) ) return false;
	// if it completed, keep on chugging
	return gotIndexList ( (void *) st );
}

bool launchRequests ( State10 *st ) {
	// nothing to do if no query
	if ( ! st->m_query[0] ) return true;
	// all done if add request only
	if ( st->m_add || st->m_del ) return true;
loop:
	int32_t split = st->m_i;
	// all done?
	if ( split >= g_hostdb.getNumShards() ) return true;
	// get group id
	//uint32_t gid = g_hostdb.getGroupId ( split );
	// get group
	//Host *hosts = g_hostdb.getGroup ( gid );
	// get host from that group, just pick the first one, assume not dead!!!
	//Host *h = &hosts[0];
	//fprintf(stderr,"termId now=%"INT64"\n",st->m_termId);
	//fprintf(stderr,"should be=%"INT64"\n",(st->m_termId & TERMID_MASK));
	// now get the indexList for this termId
	char startKey[16];
	char endKey  [16];

	key_t s12    = g_indexdb.makeStartKey ( st->m_termId );
	key_t e12    = g_indexdb.makeEndKey   ( st->m_termId );

	key128_t s16 = g_datedb.makeStartKey ( st->m_termId ,0xffffffff);
	key128_t e16 = g_datedb.makeEndKey   ( st->m_termId ,0x0);

	char rdbId;
	int32_t ks;

	if ( st->m_useDatedb ) {
		gbmemcpy ( startKey , &s16 , 16 );
		gbmemcpy ( endKey   , &e16 , 16 );
		rdbId = RDB_DATEDB;
		ks = 16;
	}
	else {
		gbmemcpy ( startKey , &s12 , 12 );
		gbmemcpy ( endKey   , &e12 , 12 );
		rdbId = RDB_INDEXDB;
		ks = 12;
	}

	// get the rdb ptr to titledb's rdb
	//Rdb *rdb = g_indexdb.getRdb();
	// -1 means read from all files in Indexdb
	int32_t numFiles = -1;
	// make it zero if caller doesn't want to hit the disk
	if ( ! st->m_useDisk ) numFiles = 0;
	// inc to next
	st->m_i++;
	// get the title rec at or after this docId
	Msg0 *m = &st->m_msg0;
	if ( ! m->getList ( -1 ,    // h->m_hostId ,
			    -1 ,    // ip
			    -1 ,    // port
			    0  ,    // max cache age
			    false , // add to cache?
			    rdbId , // RDB_INDEXDB  , // rdbId of 2 = indexdb
			    st->m_collnum ,
			    &st->m_list  ,
			    startKey  ,
			    endKey    ,
			    st->m_numRecs * ks, // recSizes 
			    //st->m_useTree   , // include tree?
			    //st->m_useCache  , // include cache?
			    //false     , // add to cache?
			    //0         , // startFileNum
			    //numFiles  , // numFiles
			    st        , // state
			    gotIndexListWrapper ,
			    0                   , // niceness
			    false               , // error correction?
			    true                , // include tree?
			    true                , // do merge?
			    -1                  , // first hostid
			    0                   , // start file num
			    -1                  , // numFiles
			    99999               , // timeout
			    -1                  , // sync point
			    -1                  , // prefer local reads?
			    NULL                , // msg5
			    NULL                , // msg5b
			    false               , // is real merge?
			    true                , // allow page cache?
			    false               , // force local indexdb?
			    true                , // do split?
			    split               ))// group # to send to
		return false;
	// launch more
	goto loop;
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	//return gotIndexList ( (void *) st , NULL );
}	

void gotIndexListWrapper ( void *state ) {//, RdbList *list ) {
	gotIndexList ( state );
}

void addedKeyWrapper ( void *state ) {
	gotIndexList ( state );
}

// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool gotIndexList ( void *state ) {
	// the state
	State10 *st = (State10 *) state;
	// launch more
	if ( ! launchRequests ( st ) ) return false;
	/*
	// get the date list
	//fprintf(stderr,"termId now=%"INT64"\n",st->m_termId);
	//fprintf(stderr,"should be=%"INT64"\n",(st->m_termId & TERMID_MASK));
	// . now get the indexList for this termId
	// . date is complemented, so start with bigger one first
	key128_t startKey = g_datedb.makeStartKey ( st->m_termId ,0xffffffff);
	key128_t endKey   = g_datedb.makeEndKey   ( st->m_termId ,0x0);
	// get the rdb ptr to titledb's rdb
	//Rdb *rdb = g_indexdb.getRdb();
	// -1 means read from all files in Indexdb
	int32_t numFiles = -1;
	// make it zero if caller doesn't want to hit the disk
	if ( ! st->m_useDisk ) numFiles = 0;
	// get the title rec at or after this docId
	if ( ! st->m_msg0.getList ( -1 ,
				    0  ,
				    0  ,
				    0  ,    // max cache age
				    false , // add to cache?
				    RDB_DATEDB  , // rdbId of 2 = indexdb
				    st->m_coll ,
				    &st->m_list2  ,
				    (char *)&startKey  ,
				    (char *)&endKey    ,
				    st->m_numRecs * sizeof(key128_t),//recSizes
				    //st->m_useTree   , // include tree?
				    //st->m_useCache  , // include cache?
				    //false     , // add to cache?
				    //0         , // startFileNum
				    //numFiles  , // numFiles
				    st        , // state
				    gotIndexListWrapper2 ,
				    0  ) )  // niceness
		return false;
	// otherwise call gotResults which returns false if blocked, true else
	// and sets g_errno on error
	return gotIndexList2 ( (void *) st , NULL );
}


void gotIndexListWrapper2 ( void *state , RdbList *list ) {
	gotIndexList2 ( state , list );
}

void addedKeyWrapper ( void *state ) {
	gotIndexList2 ( state, NULL );
}

// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool gotIndexList2 ( void *state , RdbList *list ) {
	// the state
	State10 *st = (State10 *) state;
	*/
	// get the socket
	TcpSocket *s = st->m_socket;
	// don't allow pages bigger than 128k in cache
	//char  buf [ 64*1024 ];
	// a ptr into "buf"
	//char *p    = buf;
	//char *pend = buf + 64*1024;
	/*
	// get termId
	key_t k = *(key_t *)st->m_list.getStartKey();
	int64_t termId = g_indexdb.getTermId ( k );
	// get groupId from termId
	//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
	uint32_t groupId = g_indexdb.getGroupIdFromKey ( &k );
	int32_t hostnum = g_hostdb.makeHostId ( groupId );
	*/
	// check box " checked" strings
	char *ubs = "";
	char *uts = "";
	char *uds = "";
	char *ucs = "";
	char *add = "";
	char *del = "";
	if ( st->m_useDatedb) ubs = " checked";
	if ( st->m_useTree  ) uts = " checked";
	if ( st->m_useDisk  ) uds = " checked";
	if ( st->m_useCache ) ucs = " checked";
	if ( st->m_add      ) add = " checked";
	if ( st->m_del      ) del = " checked";

	SafeBuf *pbuf = &st->m_pbuf;

	g_pages.printAdminTop ( pbuf , st->m_socket , &st->m_r );

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; 
	if (!(base=getRdbBase((uint8_t)RDB_INDEXDB,st->m_collnum)))return true;

	// print the standard header for admin pages
	pbuf->safePrintf ( 
		  "<center>\n"
		  "<table cellpadding=2><tr><td colspan=4>"
		  "useDatedb:<input type=checkbox value=1 name=ub%s> "
		  "useTree:<input type=checkbox value=1 name=ut%s> "
		  "useDisk:<input type=checkbox value=1 name=ud%s> "
		  "useCache:<input type=checkbox value=1 name=uc%s> "
		  "ADD:<input type=checkbox value=1 name=add%s> "
		  "DELETE:<input type=checkbox value=1 name=del%s>"
		  "</td></tr><tr><td>"
		  "query:"
		  "</td><td>"
		  "<input type=text name=q value=\"%s\" size=20>"
		  "</td><td>"
		  "collection:"
		  "</td><td>"
		  "<input type=text name=c value=\"%s\" size=10>"
		  "</td></tr><tr><td>"
		  "termId:"
		  "</td><td>"
		  "<input type=text name=t value=%"INT64" size=20>"
		  "</td><td>"
		  "numRecs:"
		  "</td><td>"
		  "<input type=text name=numRecs value=%"INT32" size=10> "
		  "</td></tr><tr><td>"
		  "docId:"
		  "</td><td>"
		  "<input type=text name=d value=%"INT64" size=20> "
		  "</td><td>"
		  "score:"
		  "</td><td>"
		  "<input type=text name=score value=%"INT32" size=10> "
		  "</td><td>"
		  "<input type=submit value=ok border=0>"
		  "</td></tr>"
		  "<tr><td colspan=2>"
		  "term appears in about %"INT64" docs +/- %"INT32""
		  "</td></tr>"
		  //"<tr><td colspan=2>"
		  //"this indexlist held by host #%"INT32" and twins"
		  //"</td></tr>"
		  "</table>"
		  "</form><br><br>" ,
		  ubs, uts, uds, ucs, add, del,
		  st->m_query , st->m_coll , st->m_termId  , 
		  st->m_numRecs  ,
		  st->m_docId , (int32_t)st->m_score ,
		  st->m_termFreq ,
		  2 * (int32_t)GB_INDEXDB_PAGE_SIZE / 6 * 
		  base->getNumFiles() );
		  //hostnum );

	if ( g_errno || (st->m_list.isEmpty() ) ) {//&&st->m_list2.isEmpty())){
		if (g_errno)pbuf->safePrintf("Error = %s",mstrerror(g_errno));
		else        pbuf->safePrintf("List is empty");
		pbuf->safePrintf("</center>");
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		bool status = g_httpServer.sendDynamicPage(s , 
							   pbuf->getBufStart(),
							   pbuf->length() );
		// delete it
		mdelete ( st , sizeof(State10) , "PageIndexdb" );
		delete (st);
		return status;
	}

	pbuf->safePrintf ( 
		  "<table cellpadding=1 border=1>" 
		  "<tr><td>#</td><td>score</td>"
		  "<td>docId</td><td>domHash</td></tr>");

	//if ( searchingEvents

	// now print the score/docId of indexlist
	int32_t i = 0;
	for (   st->m_list.resetListPtr () ;
	      ! st->m_list.isExhausted  () ;
		st->m_list.skipCurrentRecord () ) {
		// break if buf is low
		//if ( p + 1024 >= pend ) break;
		// but set the ip/port to a host that has this titleRec
		// stored locally!
		int64_t     docId   = st->m_list.getCurrentDocId () ;
		//uint32_t groupId = getGroupIdFromDocId ( docId );
		int32_t shardNum = getShardNumFromDocId ( docId );
		// get the first host's hostId in this groupId
		//Host *h = g_hostdb.getFastestHostInGroup ( groupId );
		Host *hosts = g_hostdb.getShard ( shardNum );
		// just pick a host now...
		Host *h = &hosts[0];
		// . pick the first host to handle the cached titleRec request
		// . we assume it has the best time and is up!! TODO: fix!
		// . use local ip though if it was an internal request
		// . otherwise, use the external ip
		//uint32_t  ip   = h->m_externalIp;
		uint32_t  ip   = h->m_ip;
		// use the NAT mapped port
		uint16_t port = h->m_externalHttpPort;
		// log the first docid so we can blaster url: queries
		// to PageIndexdb and see if they are in indexdb
		if ( i == 0 ) 
			logf(LOG_INFO,"indexdb: %"UINT64" %s",docId,st->m_query);
		// adjust ip/port if local
		if ( st->m_isLocal ) {
			ip   = h->m_ip;
			port = h->m_httpPort;
		}
		uint32_t date = 0;
		if ( st->m_useDatedb )
			date = (uint32_t)st->m_list.getCurrentDate();
		uint8_t dh = g_titledb.getDomHash8FromDocId ( docId );
		char ds[32];
		ds[0]=0;
		if ( st->m_useDatedb ) sprintf (ds,"%"UINT32"/",date);
		pbuf->safePrintf ( 
			  "<tr><td>%"INT32".</td>"
			  "<td>%s%i</td>"
			  "<td>"
			  //"<a href=http://%s:%hu/admin/titledb?d=%"UINT64">"
			  "<a href=/admin/titledb?c=%s&d=%"UINT64">"
			  "%"UINT64""
			  //"<td><a href=/cgi/4.cgi?d=%"UINT64">%"UINT64""
			  "</td>"
			  "<td>"
			  "0x%02"XINT32""
			  "</td>"
			  "</tr>\n" ,
			  i++,
			  ds, (int)st->m_list.getCurrentScore() ,
			  //iptoa(ip) , port ,
			  st->m_coll,
			  docId , 
			  docId ,
			  (int32_t)dh );
	}	
	pbuf->safePrintf ( "</table>" );

	/*
	if ( ! st->m_list2.isEmpty() ) 
		p += sprintf ( p ,
			       "<br>"
			       "<br>"
			       "<table cellpadding=1 border=1>" 
			       "<tr><td>#</td><td>termId</td>"
			       "<td>date</td><td>score</td>"
			       "<td>docId</td></tr>");

	// now print the score/docId of datedb list
	i = 0;
	for (   st->m_list2.resetListPtr () ;
	      ! st->m_list2.isExhausted  () ;
		st->m_list2.skipCurrentRecord () ) {
		// break if buf is low
		if ( p + 1024 >= pend ) break;
		// but set the ip/port to a host that has this titleRec
		// stored locally!
		int64_t     docId   = st->m_list2.getCurrentDocId () ;
		uint32_t groupId = g_titledb.getGroupId ( docId );
		// get the first host's hostId in this groupId
		Host *h = g_hostdb.getFastestHostInGroup ( groupId );
		// . pick the first host to handle the cached titleRec request
		// . we assume it has the best time and is up!! TODO: fix!
		// . use local ip though if it was an internal request
		// . otherwise, use the external ip
		//uint32_t  ip   = h->m_externalIp;
		uint32_t  ip   = h->m_ip;
		// use the NAT mapped port
		uint16_t port = h->m_externalHttpPort;
		// adjust ip/port if local
		if ( st->m_isLocal ) {
			ip   = h->m_ip;
			port = h->m_httpPort;
		}
		// debug
		char kb[16];
		st->m_list2.getCurrentKey(kb);
		//log(LOG_INFO,"debug: n1=%016"XINT64" n0=%016"XINT64"",
		//    *(int64_t *)(kb+8),*(int64_t *)(kb+0));
		//if ( (uint32_t)st->m_list2.getCurrentDate() == 0 )
		//	log("STOP");
		sprintf ( p , 
			  "<tr><td>%"INT32".</td>"
			  "<td>%"UINT64"</td>"
			  "<td>%"UINT32"</td><td>%i</td>"
			  "<td>"
			  //"<a href=http://%s:%hu/admin/titledb?d=%"UINT64">"
			  "<a href=/admin/titledb?c=%s&d=%"UINT64">"
			  "%"UINT64""
			  //"<td><a href=/cgi/4.cgi?d=%"UINT64">%"UINT64""
			  "</td></tr>\n" ,
			  i++,
			  st->m_list2.getTermId16(kb) ,
			  (uint32_t)st->m_list2.getCurrentDate() ,
			  (int)st->m_list2.getCurrentScore() ,
			  //iptoa(ip) , port ,
			  st->m_coll,
			  docId , 
			  docId );
		p += gbstrlen ( p );
	}	
	*/
	if ( ! st->m_list.isEmpty() ) 
		pbuf->safePrintf ( "</table>" );


	// print msg if we could fit all into buf
	//if ( p + 1024 >= pend ) {
	//	sprintf ( p ,"... truncated ... no mem" );
	//	p += gbstrlen ( p );		
	//}
	// print the final tail
	//p += g_httpServer.printTail ( p , pend - p );
	pbuf->safePrintf ( "</center>\n");
	// now encapsulate it in html head/tail and send it off
	bool status = g_httpServer.sendDynamicPage ( s , 
						     pbuf->getBufStart() ,
						     pbuf->length() );
	// delete the state
	mdelete ( st , sizeof(State10) , "PageIndexdb" );
	delete (st) ;
	return status;
}
