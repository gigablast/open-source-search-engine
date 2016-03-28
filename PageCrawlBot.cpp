// diffbot api implementaion

//
// WHAT APIs are here?
//
// . 1. the CrawlBot API to start a crawl 
// . 2. To directly process a provided URL (injection)
// . 3. the Cache API so phantomjs can quickly check the cache for files
//      and quickly add files to the cache.
//

// Related pages:
//
// * http://diffbot.com/dev/docs/  (Crawlbot API tab, and others)
// * http://diffbot.com/dev/crawl/

#include "Errno.h"
#include "PageCrawlBot.h"
#include "TcpServer.h"
#include "HttpRequest.h"
#include "HttpServer.h"
#include "Pages.h" // g_msg
#include "XmlDoc.h" // for checkRegex()
#include "PageInject.h" // Msg7
#include "Repair.h"
#include "Parms.h"

// so user can specify the format of the reply/output
//#define FMT_HTML 1
//#define FMT_XML  2
//#define FMT_JSON 3
//#define FMT_CSV  4
//#define FMT_TXT  5

void doneSendingWrapper ( void *state , TcpSocket *sock ) ;
bool sendBackDump ( TcpSocket *s,HttpRequest *hr );
CollectionRec *addNewDiffbotColl ( char *addColl , char *token,char *name ,
				   class HttpRequest *hr ) ;
bool resetUrlFilters ( CollectionRec *cr ) ;

bool setSpiderParmsFromHtmlRequest ( TcpSocket *socket ,
				     HttpRequest *hr , 
				     CollectionRec *cr ) ;


////////////////
//
// SUPPORT FOR DOWNLOADING an RDB DUMP
//
// We ask each shard for 10MB of Spiderdb records. If 10MB was returned
// then we repeat. Everytime we get 10MB from each shard we print the
// Spiderdb records out into "safebuf" and transmit it to the user. once
// the buffer has been transmitted then we ask the shards for another 10MB
// worth of spider records.
//
////////////////


// use this as a state while dumping out spiderdb for a collection
class StateCD {
public:
	StateCD () { m_needsMime = true; };
	void sendBackDump2 ( ) ;
	bool readDataFromRdb ( ) ;
	bool sendList ( ) ;
	void printSpiderdbList ( RdbList *list , SafeBuf *sb ,
				 char **lastKeyPtr ) ;
	void printTitledbList ( RdbList *list , SafeBuf *sb ,
				char **lastKeyPtr );
	bool printJsonItemInCsv ( char *json , SafeBuf *sb ) ;

	int64_t m_lastUh48;
	int32_t m_lastFirstIp;
	int64_t m_prevReplyUh48;
	int32_t m_prevReplyFirstIp;
	int32_t m_prevReplyError;
	time_t m_prevReplyDownloadTime;

	char m_fmt;
	Msg4 m_msg4;
	HttpRequest m_hr;
	Msg7 m_msg7;
	int32_t m_dumpRound;
	int64_t m_accumulated;

	WaitEntry m_waitEntry;

	bool m_isFirstTime;
	bool m_printedFirstBracket;
	bool m_printedEndingBracket;
	bool m_printedItem;

	bool m_needHeaderRow;

	SafeBuf m_seedBank;
	SafeBuf m_listBuf;

	bool m_needsMime;
	char m_rdbId;
	bool m_downloadJSON;
	collnum_t m_collnum;
	int32_t m_numRequests;
	int32_t m_numReplies;
	int32_t m_minRecSizes;
	bool m_someoneNeedsMore;
	TcpSocket *m_socket;
	Msg0 m_msg0s[MAX_HOSTS];
	key128_t m_spiderdbStartKeys[MAX_HOSTS];
	key_t m_titledbStartKeys[MAX_HOSTS];
	RdbList m_lists[MAX_HOSTS];
	bool m_needMore[MAX_HOSTS];

};

// . basically dump out spiderdb
// . returns urls in csv format in reply to a 
//   "GET /api/download/%s_data.json"
//   "GET /api/download/%s_data.xml"
//   "GET /api/download/%s_urls.csv"
//   "GET /api/download/%s_pages.txt"
//   where %s is the collection name
// . the ordering of the urls is not specified so whatever order they are
//   in spiderdb will do
// . the gui that lists the urls as they are spidered in real time when you
//   do a test crawl will just have to call this repeatedly. it shouldn't
//   be too slow because of disk caching, and, most likely, the spider requests
//   will all be in spiderdb's rdbtree any how
// . because we are distributed we have to send a msg0 request to each 
//   shard/group asking for all the spider urls. dan says 30MB is typical
//   for a csv file, so for now we will just try to do a single spiderdb
//   request.
bool sendBackDump ( TcpSocket *sock, HttpRequest *hr ) {

	char *path = hr->getPath();
	int32_t pathLen = hr->getPathLen();
	char *pathEnd = path + pathLen;

	char *str = strstr ( path , "/download/" );
	if ( ! str ) {
		char *msg = "bad download request";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}

	// when downloading csv socket closes because we can take minutes
	// before we send over the first byte, so try to keep open
	//int parm = 1;
	//if(setsockopt(sock->m_sd,SOL_TCP,SO_KEEPALIVE,&parm,sizeof(int))<0){
	//	log("crawlbot: setsockopt: %s",mstrerror(errno));
	//	errno = 0;
	//}

	//int32_t pathLen = hr->getPathLen();
	char rdbId = RDB_NONE;
	bool downloadJSON = false;
	int32_t fmt;
	char *xx;
	int32_t dt = CT_JSON;

	if ( ( xx = strstr ( path , "_data.json" ) ) ) {
		rdbId = RDB_TITLEDB;
		fmt = FORMAT_JSON;
		downloadJSON = true;
		dt = CT_JSON;
	}
	else if ( ( xx = strstr ( path , "_html.json" ) ) ) {
		rdbId = RDB_TITLEDB;
		fmt = FORMAT_JSON;
		downloadJSON = true;
		dt = CT_HTML;
	}
	else if ( ( xx = strstr ( path , "_data.csv" ) ) ) {
		rdbId = RDB_TITLEDB;
		downloadJSON = true;
		fmt = FORMAT_CSV;
	}
	else if ( ( xx = strstr ( path , "_urls.csv" ) ) ) {
		rdbId = RDB_SPIDERDB;
		fmt = FORMAT_CSV;
	}
	else if ( ( xx = strstr ( path , "_urls.txt" ) ) ) {
		rdbId = RDB_SPIDERDB;
		fmt = FORMAT_TXT;
	}
	else if ( ( xx = strstr ( path , "_pages.txt" ) ) ) {
		rdbId = RDB_TITLEDB;
		fmt = FORMAT_TXT;
	}

	// sanity, must be one of 3 download calls
	if ( rdbId == RDB_NONE ) {
		char *msg ;
		msg = "usage: downloadurls, downloadpages, downloaddata";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}


	char *coll = str + 10;
	if ( coll >= pathEnd ) {
		char *msg = "bad download request2";
		log("crawlbot: %s",msg);
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}

	// get coll
	char *collEnd = xx;

	//CollectionRec *cr = getCollRecFromHttpRequest ( hr );
	CollectionRec *cr = g_collectiondb.getRec ( coll , collEnd - coll );
	if ( ! cr ) {
		char *msg = "token or id (crawlid) invalid";
		log("crawlbot: invalid token or crawlid to dump");
		g_httpServer.sendErrorReply(sock,500,msg);
		return true;
	}



	// . if doing download of csv, make it search results now!
	// . make an httprequest on stack and call it
	if ( fmt == FORMAT_CSV && rdbId == RDB_TITLEDB ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		int32_t dr = 1;
		// do not dedup bulk jobs
		if ( cr->m_isCustomCrawl == 2 ) dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search.csv?icc=1&format=csv&sc=0&"
			       // dedup. since stream=1 and pss=0 below
			       // this will dedup on page content hash only
			       // which is super fast.
			       "dr=%"INT32"&"
			       "c=%s&n=1000000&"
			       // stream it now
			       "stream=1&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // no gigabits
			       "dsrt=0&"
			       // do not compute summary. 0 lines.
			       "ns=0&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      "\r\n\r\n"
			       , dr
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	// . if doing download of json, make it search results now!
	// . make an httprequest on stack and call it
	if ( fmt == FORMAT_JSON && rdbId == RDB_TITLEDB && dt == CT_HTML ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		int32_t dr = 1;
		// do not dedup bulk jobs
		if ( cr->m_isCustomCrawl == 2 ) dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search.csv?icc=1&format=json&sc=0&"
			       // dedup. since stream=1 and pss=0 below
			       // this will dedup on page content hash only
			       // which is super fast.
			       "dr=%"INT32"&"
			       "c=%s&n=1000000&"
			       // we can stream this because unlink csv it
			       // has no header row that needs to be 
			       // computed from all results.
			       "stream=1&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // no gigabits
			       "dsrt=0&"
			       // do not compute summary. 0 lines.
			       "ns=0&"
			       //"q=gbsortby%%3Agbspiderdate&"
			       //"prepend=type%%3A%s"
			       "q=type%%3Ahtml"
			      "\r\n\r\n"
			       , dr 
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	if ( fmt == FORMAT_JSON && rdbId == RDB_TITLEDB ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		int32_t dr = 1;
		// do not dedup bulk jobs
		if ( cr->m_isCustomCrawl == 2 ) dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search.csv?icc=1&format=json&sc=0&"
			       // dedup. since stream=1 and pss=0 below
			       // this will dedup on page content hash only
			       // which is super fast.
			       "dr=%"INT32"&"
			       "c=%s&n=1000000&"
			       // we can stream this because unlink csv it
			       // has no header row that needs to be 
			       // computed from all results.
			       "stream=1&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // no gigabits
			       "dsrt=0&"
			       // do not compute summary. 0 lines.
			       "ns=0&"
			       "q=gbsortby%%3Agbspiderdate&"
			       "prepend=type%%3Ajson"
			       "\r\n\r\n"
			       , dr 
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}

	// . now the urls.csv is also a query on gbss files
	// . make an httprequest on stack and call it
	// . only do this for version 3 
	//   i.e. GET /v3/crawl/download/token-collectionname_urls.csv
	if ( fmt == FORMAT_CSV && 
	     rdbId == RDB_SPIDERDB &&
	     path[0] == '/' &&
	     path[1] == 'v' &&
	     path[2] == '3' ) {
		char tmp2[5000];
		SafeBuf sb2(tmp2,5000);
		// never dedup
		int32_t dr = 0;
		// do not dedup for crawls either it is too confusing!!!!
		// ppl wonder where the results are!
		dr = 0;
		sb2.safePrintf("GET /search?"
			       // this is not necessary
			       //"icc=1&"
			       "format=csv&"
			       // no site clustering
			       "sc=0&"
			       // never dedup.
			       "dr=0&"
			       "c=%s&"
			       "n=10000000&"
			       // stream it now
			       // can't stream until we fix headers be printed
			       // in Msg40.cpp. so gbssUrl->Url etc.
			       // mdw: ok should work now
			       "stream=1&"
			       //"stream=0&"
			       // no summary similarity dedup, only exact
			       // doc content hash. otherwise too slow!!
			       "pss=0&"
			       // no gigabits
			       "dsrt=0&"
			       // do not compute summary. 0 lines.
			       //"ns=0&"
			       "q=gbrevsortbyint%%3AgbssSpiderTime+"
			       "gbssIsDiffbotObject%%3A0"
			       "&"
			       //"prepend=type%%3Ajson"
			       "\r\n\r\n"
			       , cr->m_coll
			       );
		log("crawlbot: %s",sb2.getBufStart());
		HttpRequest hr2;
		hr2.set ( sb2.getBufStart() , sb2.length() , sock );
		return sendPageResults ( sock , &hr2 );
	}



	//if ( strncmp ( path ,"/crawlbot/downloadurls",22  ) == 0 )
	//	rdbId = RDB_SPIDERDB;
	//if ( strncmp ( path ,"/crawlbot/downloadpages",23  ) == 0 )
	//	rdbId = RDB_TITLEDB;
	//if ( strncmp ( path ,"/crawlbot/downloaddata",22  ) == 0 ) {
	//	downloadJSON = true;
	//	rdbId = RDB_TITLEDB;
	//}


	StateCD *st;
	try { st = new (StateCD); }
	catch ( ... ) {
	       return g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
	}
	mnew ( st , sizeof(StateCD), "statecd");

	// initialize the new state
	st->m_rdbId = rdbId;
	st->m_downloadJSON = downloadJSON;
	st->m_socket = sock;
	// the name of the collections whose spiderdb we read from
	st->m_collnum = cr->m_collnum;

	st->m_fmt = fmt;
	st->m_isFirstTime = true;

	st->m_printedFirstBracket = false;
	st->m_printedItem = false;
	st->m_printedEndingBracket = false;

	// for csv...
	st->m_needHeaderRow = true;

	st->m_lastUh48 = 0LL;
	st->m_lastFirstIp = 0;
	st->m_prevReplyUh48 = 0LL;
	st->m_prevReplyFirstIp = 0;
	st->m_prevReplyError = 0;
	st->m_prevReplyDownloadTime = 0LL;
	st->m_dumpRound = 0;
	st->m_accumulated = 0LL;

	// debug
	//log("mnew1: st=%"XINT32"",(int32_t)st);

	// begin the possible segmented process of sending back spiderdb
	// to the user's browser
	st->sendBackDump2();
	// i dont think this return values matters at all since httpserver.cpp
	// does not look at it when it calls sendReply()
	return true;
}


// . all wrappers call this
// . returns false if would block, true otherwise
bool readAndSendLoop ( StateCD *st , bool readFirst ) {

 subloop:

	// if we had a broken pipe on the sendChunk() call then hopefully
	// this will kick in...
	if ( g_errno ) {
		log("crawlbot: readAndSendLoop: %s",mstrerror(g_errno));
		readFirst = true;
		st->m_someoneNeedsMore = false;
	}

	// wait if some are outstanding. how can this happen?
	if ( st->m_numRequests > st->m_numReplies ) {
		log("crawlbot: only got %"INT32" of %"INT32" replies. waiting for "
		    "all to come back in.",
		    st->m_numReplies,st->m_numRequests);
		return false;
	}

	// are we all done? we still have to call sendList() to 
	// set socket's streamingMode to false to close things up
	if ( readFirst && ! st->m_someoneNeedsMore ) {
		log("crawlbot: done sending for download request");
		mdelete ( st , sizeof(StateCD) , "stcd" );
		delete st;
		return true;
	}

	// begin reading from each shard and sending the spiderdb records
	// over the network. return if that blocked
	if ( readFirst && ! st->readDataFromRdb ( ) ) return false;

	// did user delete their collection midstream on us?
	if ( g_errno ) {
		log("crawlbot: read shard data had error: %s",
		    mstrerror(g_errno));
		goto subloop;
	}

	// send it to the browser socket. returns false if blocks.
	if ( ! st->sendList() ) return false;

	// read again i guess
	readFirst = true;

	// hey, it did not block... tcpserver caches writes...
	goto subloop;
}

void StateCD::sendBackDump2 ( ) {

	m_numRequests = 0;
	m_numReplies  = 0;

	// read 10MB from each shard's spiderdb at a time
	//m_minRecSizes = 9999999;
	// 1ook to be more fluid
	m_minRecSizes = 99999;

	// we stop reading from all shards when this becomes false
	m_someoneNeedsMore = true;

	// initialize the spiderdb startkey "cursor" for each shard's spiderdb
	for ( int32_t i = 0 ; i < g_hostdb.m_numShards ; i++ ) {
		m_needMore[i] = true;
		KEYMIN((char *)&m_spiderdbStartKeys[i],sizeof(key128_t));
		KEYMIN((char *)&m_titledbStartKeys[i],sizeof(key_t));
	}

	// begin reading from the shards and trasmitting back on m_socket
	readAndSendLoop ( this , true );
}


static void gotListWrapper7 ( void *state ) {
	// get the Crawler dump State
	StateCD *st = (StateCD *)state;
	// inc it up here
	st->m_numReplies++;
	// wait for all
	if ( st->m_numReplies < st->m_numRequests ) return;
	// read and send loop
	readAndSendLoop( st , false );
}
	

bool StateCD::readDataFromRdb ( ) {

	// set end key to max key. we are limiting using m_minRecSizes for this
	key128_t ek; KEYMAX((char *)&ek,sizeof(key128_t));

	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	// collection got nuked?
	if ( ! cr ) {
		log("crawlbot: readdatafromrdb: coll %"INT32" got nuked",
		    (int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return true;
	}

	// top:
	// launch one request to each shard
	for ( int32_t i = 0 ; i < g_hostdb.m_numShards ; i++ ) {
		// reset each one
		m_lists[i].freeList();
		// if last list was exhausted don't bother
		if ( ! m_needMore[i] ) continue;
		// count it
		m_numRequests++;
		// this is the least nice. crawls will yield to it mostly.
		int32_t niceness = 0;
		// point to right startkey
		char *sk ;
		if ( m_rdbId == RDB_SPIDERDB )
			sk = (char *)&m_spiderdbStartKeys[i];
		else
			sk = (char *)&m_titledbStartKeys[i];
		// get host
		Host *h = g_hostdb.getLiveHostInShard(i);
		// show it
		int32_t ks = getKeySizeFromRdbId(m_rdbId);
		log("dump: asking host #%"INT32" for list sk=%s",
		    h->m_hostId,KEYSTR(sk,ks));
		// msg0 uses multicast in case one of the hosts in a shard is
		// dead or dies during this call.
		if ( ! m_msg0s[i].getList ( h->m_hostId , // use multicast
					    h->m_ip,
					    h->m_port,
					    0, // maxcacheage
					    false, // addtocache?
					    m_rdbId,
					   cr->m_collnum,
					   &m_lists[i],
					   sk,
					   (char *)&ek,
					   // get at most about
					   // "minRecSizes" worth of spiderdb
					   // records
					   m_minRecSizes,
					   this,
					    gotListWrapper7 ,
					    niceness ) ) {
			log("crawlbot: blocked getting list from shard");
			// continue if it blocked
			continue;
		}
		log("crawlbot: did not block getting list from shard err=%s",
		    mstrerror(g_errno));
		// we got a reply back right away...
		m_numReplies++;
	}
	// all done? return if still waiting on more msg0s to get their data
	if ( m_numReplies < m_numRequests ) return false;
	// i guess did not block, empty single shard? no, must have been
	// error becaues sendList() would have sent back on the tcp
	// socket and blocked and returned false if not error sending
	return true;
}

bool StateCD::sendList ( ) {
	// get the Crawler dump State
	// inc it
	//m_numReplies++;
	// sohw it
	log("crawlbot: got list from shard. req=%"INT32" rep=%"INT32"",
	    m_numRequests,m_numReplies);
	// return if still awaiting more replies
	if ( m_numReplies < m_numRequests ) return false;

	SafeBuf sb;
	//sb.setLabel("dbotdmp");

	char *ct = "text/csv";
	if ( m_fmt == FORMAT_JSON )
		ct = "application/json";
	if ( m_fmt == FORMAT_XML )
		ct = "text/xml";
	if ( m_fmt == FORMAT_TXT )
		ct = "text/plain";
	if ( m_fmt == FORMAT_CSV )
		ct = "text/csv";

	// . if we haven't yet sent an http mime back to the user
	//   then do so here, the content-length will not be in there
	//   because we might have to call for more spiderdb data
	if ( m_needsMime ) {
		m_needsMime = false;
		HttpMime mime;
		mime.makeMime ( -1, // totel content-lenght is unknown!
				0 , // do not cache (cacheTime)
				0 , // lastModified
				0 , // offset
				-1 , // bytesToSend
				NULL , // ext
				false, // POSTReply
				ct, // "text/csv", // contenttype
				"utf-8" , // charset
				-1 , // httpstatus
				NULL ); //cookie
		sb.safeMemcpy(mime.getMime(),mime.getMimeLen() );
	}

	//CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	if ( ! m_printedFirstBracket && m_fmt == FORMAT_JSON ) {
		sb.safePrintf("[\n");
		m_printedFirstBracket = true;
	}

	// these are csv files not xls
	//if ( ! m_printedFirstBracket && m_fmt == FORMAT_CSV ) {
	//	sb.safePrintf("sep=,\n");
	//	m_printedFirstBracket = true;
	//}


	// we set this to true below if any one shard has more spiderdb
	// records left to read
	m_someoneNeedsMore = false;

	//
	// got all replies... create the HTTP reply and send it back
	//
	for ( int32_t i = 0 ; i < g_hostdb.m_numShards ; i++ ) {
		if ( ! m_needMore[i] ) continue;
		// get the list from that group
		RdbList *list = &m_lists[i];

		// should we try to read more?
		m_needMore[i] = false;

		// report it
		log("dump: got list of %"INT32" bytes from host #%"INT32" round #%"INT32"",
		    list->getListSize(),i,m_dumpRound);


		if ( list->isEmpty() ) {
			list->freeList();
			continue;
		}

		// get the format
		//char *format = cr->m_diffbotFormat.getBufStart();
		//if ( cr->m_diffbotFormat.length() <= 0 ) format = NULL;
		//char *format = NULL;

		// this cores because msg0 does not transmit lastkey
		//char *ek = list->getLastKey();

		char *lastKeyPtr = NULL;

		// now print the spiderdb list out into "sb"
		if ( m_rdbId == RDB_SPIDERDB ) {
			// print SPIDERDB list into "sb"
			printSpiderdbList ( list , &sb , &lastKeyPtr );
			//  update spiderdb startkey for this shard
			KEYSET((char *)&m_spiderdbStartKeys[i],lastKeyPtr,
			       sizeof(key128_t));
			// advance by 1
			m_spiderdbStartKeys[i] += 1;
		}

		else if ( m_rdbId == RDB_TITLEDB ) {
			// print TITLEDB list into "sb"
			printTitledbList ( list , &sb , &lastKeyPtr );
			//  update titledb startkey for this shard
			KEYSET((char *)&m_titledbStartKeys[i],lastKeyPtr,
			       sizeof(key_t));
			// advance by 1
			m_titledbStartKeys[i] += 1;
		}

		else { char *xx=NULL;*xx=0; }

		// figure out why we do not get the full list????
		//if ( list->m_listSize >= 0 ) { // m_minRecSizes ) {
		m_needMore[i] = true;
		m_someoneNeedsMore = true;
		//}

		// save mem
		list->freeList();
	}

	m_dumpRound++;

	//log("rdbid=%"INT32" fmt=%"INT32" some=%"INT32" printed=%"INT32"",
	//    (int32_t)m_rdbId,(int32_t)m_fmt,(int32_t)m_someoneNeedsMore,
	//    (int32_t)m_printedEndingBracket);

	m_socket->m_streamingMode = true;

	// if nobody needs to read more...
	if ( ! m_someoneNeedsMore && ! m_printedEndingBracket ) {
		// use this for printing out urls.csv as well...
		m_printedEndingBracket = true;
		// end array of json objects. might be empty!
		if ( m_rdbId == RDB_TITLEDB && m_fmt == FORMAT_JSON )
			sb.safePrintf("\n]\n");
		//log("adding ]. len=%"INT32"",sb.length());
		// i'd like to exit streaming mode here. i fixed tcpserver.cpp
		// so if we are called from makecallback() there it won't
		// call destroysocket if we WERE in streamingMode just yet
		m_socket->m_streamingMode = false;		
	}

	TcpServer *tcp = &g_httpServer.m_tcp;

	// . transmit the chunk in sb
	// . steals the allocated buffer from sb and stores in the 
	//   TcpSocket::m_sendBuf, which it frees when socket is
	//   ultimately destroyed or we call sendChunk() again.
	// . when TcpServer is done transmitting, it does not close the
	//   socket but rather calls doneSendingWrapper() which can call
	//   this function again to send another chunk
	if ( ! tcp->sendChunk ( m_socket , 
				&sb  ,
				this ,
				doneSendingWrapper ) )
		return false;

	// we are done sending this chunk, i guess tcp write was cached
	// in the network card buffer or something
	return true;
}

// TcpServer.cpp calls this when done sending TcpSocket's m_sendBuf
void doneSendingWrapper ( void *state , TcpSocket *sock ) {
	StateCD *st = (StateCD *)state;
	// error on socket?
	//if ( g_errno ) st->m_socketError = g_errno;
	//TcpSocket *socket = st->m_socket;
	st->m_accumulated += sock->m_totalSent;

	log("crawlbot: done sending on socket %"INT32"/%"INT32" [%"INT64"] bytes",
	    sock->m_totalSent,
	    sock->m_sendBufUsed,
	    st->m_accumulated);


	readAndSendLoop ( st , true );

	return;
}

void StateCD::printSpiderdbList ( RdbList *list,SafeBuf *sb,char **lastKeyPtr){
	// declare these up here
	SpiderRequest *sreq = NULL;
	SpiderReply   *srep = NULL;
	int32_t badCount = 0;

	int32_t nowGlobalMS = gettimeofdayInMillisecondsGlobal();
	CollectionRec *cr = g_collectiondb.getRec(m_collnum);
	uint32_t lastSpidered = 0;

	// parse through it
	for ( ; ! list->isExhausted() ; list->skipCurrentRec() ) {
		// this record is either a SpiderRequest or SpiderReply
		char *rec = list->getCurrentRec();
		// save it
		*lastKeyPtr = rec;
		// we encounter the spiderreplies first then the
		// spiderrequests for the same url
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			srep = (SpiderReply *)rec;
			if ( sreq ) lastSpidered = 0;
			sreq = NULL;
			if ( lastSpidered == 0 )
				lastSpidered = srep->m_spideredTime;
			else if ( srep->m_spideredTime > lastSpidered )
				lastSpidered = srep->m_spideredTime;
			m_prevReplyUh48 = srep->getUrlHash48();
			m_prevReplyFirstIp = srep->m_firstIp;
			// 0 means indexed successfully. not sure if
			// this includes http status codes like 404 etc.
			// i don't think it includes those types of errors!
			m_prevReplyError = srep->m_errCode;
			m_prevReplyDownloadTime = srep->m_spideredTime;
			continue;
		}
		// ok, we got a spider request
		sreq = (SpiderRequest *)rec;
		
		if ( sreq->isCorrupt() ) {
			log("spider: encountered a corrupt spider req "
			    "when dumping cn=%"INT32". skipping.",
			    (int32_t)cr->m_collnum);
			continue;
		}

		// sanity check
		if ( srep && srep->getUrlHash48() != sreq->getUrlHash48()){
			badCount++;
			//log("diffbot: had a spider reply with no "
			//    "corresponding spider request for uh48=%"INT64""
			//    , srep->getUrlHash48());
			//char *xx=NULL;*xx=0;
		}

		// print the url if not yet printed
		int64_t uh48 = sreq->getUrlHash48  ();
		int32_t firstIp = sreq->m_firstIp;
		bool printIt = false;
		// there can be multiple spiderrequests for the same url!
		if ( m_lastUh48 != uh48 ) printIt = true;
		// sometimes the same url has different firstips now that
		// we have the EFAKEFIRSTIP spider error to avoid spidering
		// seeds twice...
		if ( m_lastFirstIp != firstIp ) printIt = true;
		if ( ! printIt ) continue;
		m_lastUh48 = uh48;
		m_lastFirstIp = firstIp;

		// make sure spiderreply is for the same url!
		if ( srep && srep->getUrlHash48() != sreq->getUrlHash48() )
			srep = NULL;
		if ( ! srep )
			lastSpidered = 0;

		bool isProcessed = false;
		if ( srep ) isProcessed = srep->m_sentToDiffbotThisTime;

		if ( srep && srep->m_hadDiffbotError )
			isProcessed = false;

		// debug point
		//if ( strstr(sreq->m_url,"chief") )
		//	log("hey");

		// 1 means spidered, 0 means not spidered, -1 means error
		int32_t status = 1;
		// if unspidered, then we don't match the prev reply
		// so set "status" to 0 to indicate hasn't been 
		// downloaded yet.
		if ( m_lastUh48 != m_prevReplyUh48 ) status = 0;
		if ( m_lastFirstIp != m_prevReplyFirstIp ) status = 0;
		// if it matches, perhaps an error spidering it?
		if ( status && m_prevReplyError ) status = -1;

		// use the time it was added to spiderdb if the url
		// was not spidered
		time_t time = sreq->m_addedTime;
		// if it was spidered, successfully or got an error,
		// then use the time it was spidered
		if ( status ) time = m_prevReplyDownloadTime;

		char *msg = "Successfully Downloaded";//Crawled";
		if ( status == 0 ) msg = "Not downloaded";//Unexamined";
		if ( status == -1 ) {
			msg = mstrerror(m_prevReplyError);
			// do not print "Fake First Ip"...
			if ( m_prevReplyError == EFAKEFIRSTIP )
				msg = "Initial crawl request";
			// if the initial crawl request got a reply then that
			// means the spiderrequest was added under the correct
			// firstip... so skip it. i am assuming that the
			// correct spidrerequest got added ok here...
			if ( m_prevReplyError == EFAKEFIRSTIP )
				continue;
		}

		if ( srep && srep->m_hadDiffbotError )
			msg = "Diffbot processing error";

		// indicate specific diffbot error if we have it
		if ( srep && 
		     srep->m_hadDiffbotError && 
		     srep->m_errCode &&
		     // stick with "diffbot processing error" for these...
		     srep->m_errCode != EDIFFBOTINTERNALERROR )
			msg = mstrerror(srep->m_errCode);

		// matching url filter, print out the expression
		int32_t ufn ;
		ufn = ::getUrlFilterNum(sreq,
					srep,
					nowGlobalMS,
					false,
					MAX_NICENESS,
					cr,
					false, // isoutlink?
					NULL,
					-1); // langIdArg
		char *expression = NULL;
		int32_t  priority = -4;
		// sanity check
		if ( ufn >= 0 ) { 
			expression = cr->m_regExs[ufn].getBufStart();
			priority   = cr->m_spiderPriorities[ufn];
		}

		if ( ! expression ) {
			expression = "error. matches no expression!";
			priority = -4;
		}

		// when spidering rounds we use the 
		// lastspidertime>={roundstart} --> spiders disabled rule
		// so that we do not spider a url twice in the same round
		if ( ufn >= 0 && //! cr->m_spidersEnabled[ufn] ) {
		     cr->m_regExs[ufn].length() &&
		     // we set this to 0 instead of using the checkbox
		     strstr(cr->m_regExs[ufn].getBufStart(),"round") ) {
			//cr->m_maxSpidersPerRule[ufn] <= 0 ) {
			priority = -5;
		}

		char *as = "discovered";
		if ( sreq && 
		     ( sreq->m_isInjecting ||
		       sreq->m_isAddUrl ) ) {
			as = "manually added";
		}

		// print column headers?
		if ( m_isFirstTime ) {
			m_isFirstTime = false;
			sb->safePrintf("\"Url\","
				       "\"Entry Method\","
				       );
			if ( cr->m_isCustomCrawl )
				sb->safePrintf("\"Processed?\",");
			sb->safePrintf(
				       "\"Add Time\","
				       "\"Last Crawled\","
				       "\"Last Status\","
				       "\"Matching Expression\","
				       "\"Matching Action\"\n");
		}

		// "csv" is default if json not specified
		if ( m_fmt == FORMAT_JSON ) 
			sb->safePrintf("[{"
				       "{\"url\":"
				       "\"%s\"},"
				       "{\"time\":"
				       "\"%"UINT32"\"},"

				       "{\"status\":"
				       "\"%"INT32"\"},"

				       "{\"statusMsg\":"
				       "\"%s\"}"
				       
				       "}]\n"
				       , sreq->m_url
				       // when was it first added to spiderdb?
				       , sreq->m_addedTime
				       , status
				       , msg
				       );
		// but default to csv
		else {
		    if (cr && cr->m_isCustomCrawl == 1 && sreq && !sreq->m_isAddUrl && !sreq->m_isInjecting) {
		        if (cr->m_diffbotUrlCrawlPattern.m_length == 0
                    && cr->m_diffbotUrlProcessPattern.m_length == 0) {
		            // If a crawl and there are no urlCrawlPattern or urlCrawlRegEx values, only return URLs from seed domain
		            if (sreq && !sreq->m_sameDom)
		                continue;
		        } else {
		            // TODO: if we get here, we have a crawl with a custom urlCrawlPattern and/or custom
		            //       urlProcessPattern. We have to check if the current url matches the pattern

		        }
		    }

			sb->safePrintf("\"%s\",\"%s\","
				       , sreq->m_url
				       , as
				       );
			if ( cr->m_isCustomCrawl )
				sb->safePrintf("%"INT32",",(int32_t)isProcessed);
			sb->safePrintf(
				       "%"UINT32",%"UINT32",\"%s\",\"%s\",\""
				       //",%s"
				       //"\n"
				       // when was it first added to spiderdb?
				       , sreq->m_addedTime
				       // last time spidered, 0 if none
				       , lastSpidered
				       //, status
				       , msg
				       // the url filter expression it matches
				       , expression
				       // the priority
				       //, priorityMsg
				       //, iptoa(sreq->m_firstIp)
				       );
			// print priority
			//if ( priority == SPIDER_PRIORITY_FILTERED )
			// we just turn off the spiders now
			if ( ufn >= 0 && cr->m_maxSpidersPerRule[ufn] <= 0 )
				sb->safePrintf("url ignored");
			//else if ( priority == SPIDER_PRIORITY_BANNED )
			//	sb->safePrintf("url banned");
			else if ( priority == -4 )
				sb->safePrintf("error");
			else if ( priority == -5 )
				sb->safePrintf("will spider next round");
			else 
				sb->safePrintf("%"INT32"",priority);
			sb->safePrintf("\""
				       "\n");
		}
	}

	if ( ! badCount ) return;

	log("diffbot: had a spider reply with no "
	    "corresponding spider request %"INT32" times", badCount);
}



void StateCD::printTitledbList ( RdbList *list,SafeBuf *sb,char **lastKeyPtr){

	XmlDoc xd;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );

	// save it
	*lastKeyPtr = NULL;

	// parse through it
	for ( ; ! list->isExhausted() ; list->skipCurrentRec() ) {
		// this record is either a SpiderRequest or SpiderReply
		char *rec = list->getCurrentRec();
		// skip ifnegative
		if ( (rec[0] & 0x01) == 0x00 ) continue;
		// set it
		*lastKeyPtr = rec;
		// reset first since set2() can't call reset()
		xd.reset();
		// uncompress it
		if ( ! xd.set2 ( rec ,
				 0, // maxSize unused
				 cr->m_coll ,
				 NULL , // ppbuf
				 0 , // niceness
				 NULL ) ) { // spiderRequest
			log("diffbot: error setting titlerec in dump");
			continue;
		}
		// must be of type json to be a diffbot json object
		if ( m_downloadJSON && xd.m_contentType != CT_JSON ) continue;
		// or if downloading web pages...
		if ( ! m_downloadJSON ) {
			// skip if json object content type
			if ( xd.m_contentType == CT_JSON ) continue;
			// . just print the cached page
			// . size should include the \0
			sb->safeStrcpy ( xd.m_firstUrl.m_url);
			// then \n
			sb->pushChar('\n');
			// then page content
			sb->safeStrcpy ( xd.ptr_utf8Content );
			// null term just in case
			//sb->nullTerm();
			// separate pages with \0 i guess
			sb->pushChar('\0');
			// \n
			sb->pushChar('\n');
			continue;
		}

		// skip if not a diffbot json url
		if ( ! xd.m_isDiffbotJSONObject ) continue;

		// get the json content
		char *json = xd.ptr_utf8Content;
		
		// empty?
		if ( xd.size_utf8Content <= 1 )
			continue;

		// if not json, just print the json item out in csv
		// moved into PageResults.cpp...
		//if ( m_fmt == FORMAT_CSV ) {
		//	printJsonItemInCsv ( json , sb );
		//	continue;
		//}

		// just print that out. encode \n's and \r's back to \\n \\r
		// and backslash to a \\ ...
		// but if they originally had a \u<backslash> encoding and
		// we made into utf8, do not put that back into the \u
		// encoding because it is not necessary.

		// print in json
		if ( m_printedItem )
			sb->safePrintf("\n,\n");

		m_printedItem = true;

		//if ( ! sb->safeStrcpyPrettyJSON ( json ) ) 
		//	log("diffbot: error printing json in dump");
		sb->safeStrcpy ( json );

		sb->nullTerm();

		// separate each JSON object with \n i guess
		//sb->pushChar('\n');
	}
}

/*
////////////////
//
// SUPPORT FOR GET /api/crawls and /api/activecrawls
//
// Just scan each collection record whose collection name includes the
// provided "token" of the user. then print out the stats of just
//
////////////////

// example output for http://live.diffbot.com/api/crawls?token=matt
// [{"id":"c421f09d-7c31-4131-9da2-21e35d8130a9","finish":1378233585887,"matched":274,"status":"Stopped","start":1378233159848,"token":"matt","parameterMap":{"token":"matt","seed":"www.techcrunch.com","api":"article"},"crawled":274}]

// example output from activecrawls?id=....
// {"id":"b7df5d33-3fe5-4a6c-8ad4-dad495b586cd","finish":null,"matched":27,"status":"Crawling","start":1378322184332,"token":"matt","parameterMap":{"token":"matt","seed":"www.alleyinsider.com","api":"article"},"crawled":34}

// NOTE: it does not seem to include active crawls! bad!! like if you lost
// the crawlid...

// "cr" is NULL if showing all crawls!
bool showAllCrawls ( TcpSocket *s , HttpRequest *hr ) {

	int32_t tokenLen = 0;
	char *token = hr->getString("token",&tokenLen);

	// token MUST be there because this function's caller checked for it
	if ( ! token ) { char *xx=NULL;*xx=0; }

	// store the crawl stats as html into "sb"
	SafeBuf sb;

	// scan the collection recs
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if empty
		if ( ! cr ) continue;
		// get name
		char *coll = cr->m_coll;
		//int32_t collLen = cr->m_collLen;
		// skip if first 16 or whatever characters does not match
		// the user token because the name of a collection is
		// <TOKEN>-<CRAWLID>
		if ( coll[0] != token[0] ) continue;
		if ( coll[1] != token[1] ) continue;
		if ( coll[2] != token[2] ) continue;
		// scan the rest
		bool match = true;
		for ( int32_t i = 3 ; coll[i] && token[i] ; i++ ) {
			// the name of a collection is <TOKEN>-<CRAWLID>
			// so if we hit the hyphen we are done
			if ( coll[i] == '-' ) break;
			if ( coll[i] != token[i] ) { match = false; break; }
		}
		if ( ! match ) continue;
		// we got a match, print them out
		printCrawlStats ( &sb , cr );
	}

	// and send back now
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), 
					     sb.length(),
					     -1);// cachetime

}
*/

/*
char *getTokenFromHttpRequest ( HttpRequest *hr ) {
	// provided directly?
	char *token = hr->getString("token",NULL,NULL);
	if ( token ) return token;
	// extract token from coll?
	char *c = hr->getString("c",NULL,NULL);
	// try new "id" approach
	if ( ! c ) c = hr->getString("id",NULL,NULL);
	if ( ! c ) return NULL;
	CollectionRec *cr = g_collectiondb.getRec(c);
	if ( ! cr ) return NULL;
	if ( cr->m_diffbotToken.length() <= 0 ) return NULL;
	token = cr->m_diffbotToken.getBufStart();
	return token;
}

CollectionRec *getCollRecFromHttpRequest ( HttpRequest *hr ) {
	// if we have the collection name explicitly, get the coll rec then
	char *c = hr->getString("c",NULL,NULL);
	// try new "id" approach
	if ( ! c ) c = hr->getString("id",NULL,NULL);
	if ( c ) return g_collectiondb.getRec ( c );
	// no matches
	return NULL;
}
*/

/*
// doesn't have to be fast, so  just do a scan
CollectionRec *getCollRecFromCrawlId ( char *crawlId ) {

	int32_t idLen = gbstrlen(crawlId);

	// scan collection names
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it
		CollectionRec *cr = g_collectiondb.m_recs[i];
		// skip if empty
		if ( ! cr ) continue;
		// get name
		char *coll = cr->m_coll;
		int32_t collLen = cr->m_collLen;
		if ( collLen < 16 ) continue;
		// skip if first 16 or whatever characters does not match
		// the user token because the name of a collection is
		// <TOKEN>-<CRAWLID>
		if ( coll[collLen-1] != crawlId[idLen-1] ) continue;
		if ( coll[collLen-2] != crawlId[idLen-2] ) continue;
		if ( coll[collLen-3] != crawlId[idLen-3] ) continue;
		if ( ! strstr ( coll , crawlId ) ) continue;
		return cr;
	}
	return NULL;
}

void printCrawlStatsWrapper ( void *state ) {
	StateXX *sxx = (StateXX *)state;
	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec(sxx->m_collnum);
	// print out the crawl
	SafeBuf sb;
	printCrawlStats ( &sb , cr );
	// save before nuking state
	TcpSocket *sock = sxx->m_socket;
	// nuke the state
	mdelete ( sxx , sizeof(StateXX) , "stxx" );
	delete sxx;
	// and send back now
	g_httpServer.sendDynamicPage ( sock ,
				       sb.getBufStart(), 
				       sb.length(),
				       -1 ); // cachetime
}


void printCrawlStats ( SafeBuf *sb , CollectionRec *cr ) {

	// if we are the first, print a '[' to start a json thingy
	if ( sb->length() == 0 )
		sb->pushChar('[');
	// otherwise, remove the previous ']' since we are not the last
	else {
		char *p = sb->getBufStart();
		int32_t plen = sb->length();
		if ( p[plen-1]=='[' ) 
			sb->incrementLength(-1);
	}
	
	sb->safePrintf( "{"
			"\"id\":\""
			);
	// get the token from coll name
	char *token = cr->m_coll;
	// and the length, up to the hyphen that separates it from crawl id
	int32_t tokenLen = 0;
	for ( ; token[tokenLen] && token[tokenLen] != '-' ; tokenLen++ );
	// now crawl id
	char *crawlId = token + tokenLen;
	// skip hyphen
	if ( crawlId[0] == '-' ) crawlId++;
	// print crawl id out
	sb->safeStrcpy ( crawlId );
	// end its quote
	sb->safeStrcpy ( "\",");
	// now the time the crawl finished. 
	if ( cr->m_spideringEnabled )
		sb->safePrintf("\"finish\":null,");
	else
		sb->safePrintf("\"finish\":%"INT64",",cr->m_diffbotCrawlEndTime);
	// how many urls we handoff to diffbot api. that implies successful
	// download and that it matches the url crawl pattern and 
	// url process pattern and content regular expression pattern.
	//
	// NOTE: pageProcessAttempts can be higher than m_pageDownloadAttempts
	// when we call getMetaList() on an *old* (in titledb) xmldoc,
	// where we just get the cached content from titledb to avoid a
	// download, but we still call getDiffbotReply(). perhaps reconstruct
	// the diffbot reply from XmlDoc::m_diffbotJSONCount
	//
	// "processed" here corresponds to the "maxProcessed" cgi parm 
	// specified when instantiating the crawl parms for the first time.
	//
	// likewise "crawled" corresponds to "maxCrawled"
	//
	sb->safePrintf("\"processedAttempts\":%"INT64",",
		       cr->m_globalCrawlInfo.m_pageProcessAttempts);
	sb->safePrintf("\"processed\":%"INT64",",
		       cr->m_globalCrawlInfo.m_pageProcessSuccesses);

	sb->safePrintf("\"crawlAttempts\":%"INT64",",
		       cr->m_globalCrawlInfo.m_pageDownloadAttempts);
	sb->safePrintf("\"crawled\":%"INT64",",
		       cr->m_globalCrawlInfo.m_pageDownloadSuccesses);

	sb->safePrintf("\"urlsConsidered\":%"INT64",",
		       cr->m_globalCrawlInfo.m_urlsConsidered);

	// how many spiders outstanding for this coll right now?
	SpiderColl *sc = g_spiderCache.getSpiderColl(cr->m_collnum);
	int32_t spidersOut = sc->getTotalOutstandingSpiders();

	// . status of the crawl: "Stopped" or "Active"?
	// . TODO: check with dan to see if Active is correct and
	//   ShuttingDown is allowable
	if ( cr->m_spideringEnabled )
		sb->safePrintf("\"status\":\"Active\",");
	else if ( spidersOut )
		sb->safePrintf("\"status\":\"ShuttingDown\",");
	else
		sb->safePrintf("\"status\":\"Stopped\",");

	// spider crawl start time
	sb->safePrintf("\"start\":%"INT64",",cr->m_diffbotCrawlStartTime);

	// the token
	sb->safePrintf("\"token\":\"");
	sb->safeMemcpy(token,tokenLen);
	sb->safePrintf("\",");

	//
	// BEGIN parameter map
	//
	// the token again
	sb->safePrintf("{");
	sb->safePrintf("\"token\":\"");
	sb->safeMemcpy(token,tokenLen);
	sb->safePrintf("\",");
	// the seed url
	sb->safePrintf("\"seed\":\"%s\",",cr->m_diffbotSeed.getBufStart());
	// the api
	sb->safePrintf("\"api\":\"%s\",",cr->m_diffbotApi.getBufStart());
	sb->safePrintf("},");
	//
	// END parameter map
	//

	// crawl count. counts non-errors. successful downloads.
	//sb->safePrintf("\"crawled\":%"INT64"",
	//	       cr->m_globalCrawlInfo.m_pageCrawlAttempts);
	
	sb->safePrintf("}");

	// assume we are the last json object in the array
	sb->pushChar(']');

}
*/

////////////////
//
//  **** THE CRAWLBOT CONTROL PANEL *****
//
// . Based on  http://diffbot.com/dev/crawl/ page. 
// . got to /dev/crawl to see this!
//
////////////////

/*
// generate a random collection name
char *getNewCollName ( ) { // char *token , int32_t tokenLen ) {
	// let's create a new crawl id. dan was making it 32 characters
	// with 4 hyphens in it for a total of 36 bytes, but since
	// MAX_COLL_LEN, the maximum length of a collection name, is just
	// 64 bytes, and the token is already 32, let's limit to 16 bytes
	// for the crawlerid. so if we print that out in hex, 16 hex chars
	// 0xffffffff 0xffffffff is 64 bits. so let's make a random 64-bit
	// value here.
	uint32_t r1 = rand();
	uint32_t r2 = rand();
	uint64_t crawlId64 = (uint64_t) r1;
	crawlId64 <<= 32;
	crawlId64 |= r2;

	static char s_collBuf[MAX_COLL_LEN+1];

	//int32_t tokenLen = gbstrlen(token);

	// include a +5 for "-test"
	// include 16 for crawlid (16 char hex #)
	//if ( tokenLen + 16 + 5>= MAX_COLL_LEN ) { char *xx=NULL;*xx=0;}
	// ensure the crawlid is the full 16 characters long so we
	// can quickly extricate the crawlid from the collection name
	//gbmemcpy ( s_collBuf, token, tokenLen );
	//sprintf(s_collBuf + tokenLen ,"-%016"XINT64"",crawlId64);
	sprintf(s_collBuf ,"%016"XINT64"",crawlId64);
	return s_collBuf;
}
*/

//////////////////////////////////////////
//
// MAIN API STUFF I GUESS
//
//////////////////////////////////////////


bool sendReply2 (TcpSocket *socket , int32_t fmt , char *msg ) {
	// log it
	log("crawlbot: %s",msg);

	char *ct = "text/html";

	// send this back to browser
	SafeBuf sb;
	if ( fmt == FORMAT_JSON ) {
		sb.safePrintf("{\n\"response\":\"success\",\n"
			      "\"message\":\"%s\"\n}\n"
			      , msg );
		ct = "application/json";
	}
	else
		sb.safePrintf("<html><body>"
			      "success: %s"
			      "</body></html>"
			      , msg );

	//return g_httpServer.sendErrorReply(socket,500,sb.getBufStart());
	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0, // cachetime
					     false, // POST reply?
					     ct);
}


bool sendErrorReply2 ( TcpSocket *socket , int32_t fmt , char *msg ) {

	// log it
	log("crawlbot: sending back 500 http status '%s'",msg);

	char *ct = "text/html";

	// send this back to browser
	SafeBuf sb;
	if ( fmt == FORMAT_JSON ) {
		sb.safePrintf("{\"error\":\"%s\"}\n"
			      , msg );
		ct = "application/json";
	}
	else
		sb.safePrintf("<html><body>"
			      "failed: %s"
			      "</body></html>"
			      , msg );

	// log it
	//log("crawlbot: %s",msg );

	//return g_httpServer.sendErrorReply(socket,500,sb.getBufStart());
	return g_httpServer.sendDynamicPage (socket, 
					     sb.getBufStart(), 
					     sb.length(),
					     0, // cachetime
					     false, // POST reply?
					     ct ,
					     500 ); // error! not 200...
}

bool printCrawlBotPage2 ( class TcpSocket *s , 
			  class HttpRequest *hr ,
			  char fmt,
			  class SafeBuf *injectionResponse ,
			  class SafeBuf *urlUploadResponse ,
			  collnum_t collnum ) ;

void addedUrlsToSpiderdbWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;
	SafeBuf rr;
	rr.safePrintf("Successfully added urls for spidering.");
	printCrawlBotPage2 ( st->m_socket,
			     &st->m_hr ,
			     st->m_fmt,
			     NULL ,
			     &rr ,
			     st->m_collnum );
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
	//log("mdel2: st=%"XINT32"",(int32_t)st);
}
/*
void injectedUrlWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;

	Msg7 *msg7 = &st->m_msg7;
	// the doc we injected...
	XmlDoc *xd = &msg7->m_xd;

	// make a status msg for the url
	SafeBuf sb;
	SafeBuf js; // for json reply
	if ( xd->m_indexCode == 0 ) {
		sb.safePrintf("<b><font color=black>"
			      "Successfully added ");
		js.safePrintf("Seed Successful. ");
	}
	else if ( xd->m_indexCode == EDOCFILTERED ) {
		sb.safePrintf("<b><font color=red>"
			      "Error: <i>%s</i> by matching "
			      "url filter #%"INT32" "
			      "when adding "
			      , mstrerror(xd->m_indexCode) 
			      // divide by 2 because we add a 
			      // "manualadd &&" rule with every url filter
			      // that the client adds
			      , (xd->m_urlFilterNum - 2) / 2
			      );
		js.safePrintf("Seed URL filtered by URL filter #%"INT32""
			      , (xd->m_urlFilterNum - 2) / 2 );
	}
	else {
		sb.safePrintf("<b><font color=red>"
			      "Error: <i>%s</i> when adding "
			      , mstrerror(xd->m_indexCode) );
		js.safePrintf("Error adding seed url: %s"
			      , mstrerror(xd->m_indexCode) );
	}
	sb.safeTruncateEllipsis(xd->m_firstUrl.getUrl(),60);
	
	if ( xd->m_indexCode == 0 ) {
		if ( xd->m_numOutlinksAddedValid ) {
			sb.safePrintf(" &nbsp; (added %"INT32" outlinks)"
				      ,(int32_t)xd->m_numOutlinksAdded);
			js.safePrintf("Added %"INT32" outlinks from same domain. "
				      "%"INT32" outlinks were filtered."
			       ,(int32_t)xd->m_numOutlinksAddedFromSameDomain
				      ,(int32_t)xd->m_numOutlinksFiltered
				      );
		}
		else {
			sb.safePrintf(" &nbsp; (added 0 outlinks)");
			js.safePrintf("Added 0 outlinks from same domain. "
				      "0 links were filtered." );
		}
	}
	
	sb.safePrintf("</font></b>");
	sb.nullTerm();

	js.nullTerm();

	// send back the html or json response?
	SafeBuf *response = &sb;
	if ( st->m_fmt == FORMAT_JSON ) response = &js;

	// . this will call g_httpServer.sendReply()
	// . pass it in the injection response, "sb"
	printCrawlBotPage2 ( st->m_socket,
			     &st->m_hr ,
			     st->m_fmt,
			     response,
			     NULL ,
			     st->m_collnum );
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
}
*/	

class HelpItem {
public:
	char *m_parm;
	char *m_desc;
};

static class HelpItem s_his[] = {
	{"format","Use &format=html to show HTML output. Default is JSON."},
	{"token","Required for all operations below."},

	{"name","Name of the crawl. If missing will just show "
	 "all crawls owned by the given token."},

	{"delete=1","Deletes the crawl."},
	{"reset=1","Resets the crawl. Removes all seeds."},
	{"restart=1","Restarts the crawl. Keeps the seeds."},

	{"pause",
	 "Specify 1 or 0 to pause or resume the crawl respectively."},

	{"repeat","Specify number of days as floating point to "
	 "recrawl the pages. Set to 0.0 to NOT repeat the crawl."},

	{"crawlDelay","Wait this many seconds between crawling urls from the "
	 "same IP address. Can be a floating point number."},

	//{"deleteCrawl","Same as delete."},
	//{"resetCrawl","Same as delete."},
	//{"pauseCrawl","Same as pause."},
	//{"repeatCrawl","Same as repeat."},

	{"seeds","Whitespace separated list of URLs used to seed the crawl. "
	 "Will only follow outlinks on the same domain of seed URLs."
	},
	{"spots",
	 "Whitespace separated list of URLs to add to the crawl. "
	 "Outlinks will not be followed." },
	{"urls",
	 "Same as spots."},
	//{"spiderLinks","Use 1 or 0 to spider the links or NOT spider "
	// "the links, respectively, from "
	// "the provided seed or addUrls parameters. "
	// "The default is 1."},


	{"maxToCrawl", "Specify max pages to successfully download."},
	//{"maxToDownload", "Specify max pages to successfully download."},

	{"maxToProcess", "Specify max pages to successfully process through "
	 "diffbot."},
	{"maxRounds", "Specify maximum number of crawl rounds. Use "
	 "-1 to indicate no max."},

	{"onlyProcessIfNew", "Specify 1 to avoid re-processing pages "
	 "that have already been processed once before."},

	{"notifyEmail","Send email alert to this email when crawl hits "
	 "the maxtocrawl or maxtoprocess limit, or when the crawl "
	 "completes."},
	{"notifyWebhook","Fetch this URL when crawl hits "
	 "the maxtocrawl or maxtoprocess limit, or when the crawl "
	 "completes."},
	{"obeyRobots","Obey robots.txt files?"},
	//{"restrictDomain","Restrict downloaded urls to domains of seeds?"},

	{"urlCrawlPattern","List of || separated strings. If the url "
	 "contains any of these then we crawl the url, otherwise, we do not. "
	 "An empty pattern matches all urls."},

	{"urlProcessPattern","List of || separated strings. If the url "
	 "contains any of these then we send url to diffbot for processing. "
	 "An empty pattern matches all urls."},

	{"pageProcessPattern","List of || separated strings. If the page "
	 "contains any of these then we send it to diffbot for processing. "
	 "An empty pattern matches all pages."},

	{"urlCrawlRegEx","Regular expression that the url must match "
	 "in order to be crawled. If present then the urlCrawlPattern will "
	 "be ignored. "
	 "An empty regular expression matches all urls."},

	{"urlProcessRegEx","Regular expression that the url must match "
	 "in order to be processed. "
	 "If present then the urlProcessPattern will "
	 "be ignored. "
	 "An empty regular expression matches all urls."},

	{"apiUrl","Diffbot api url to use. We automatically append "
	 "token and url to it."},


	//{"expression","A pattern to match in a URL. List up to 100 "
	// "expression/action pairs in the HTTP request. "
	// "Example expressions:"},
	//{"action","Take the appropriate action when preceeding pattern is "
	// "matched. Specify multiple expression/action pairs to build a "
	// "table of filters. Each URL being spidered will take the given "
	// "action of the first expression it matches. Example actions:"},


	{NULL,NULL}
};

/*
// get the input string from the httprequest or the json post
char *getInputString ( char *string , HttpRequest *hr , Json *JS ) {
	// try to get it from http request
	char *val = hr->getString(string);
	// if token in json post, use that
	if ( ! val ) {
		JsonItem *ji = JS.getItem(string);
		if ( ji ) val = ji->getValue();
	}
	return val;
}
*/

void collOpDoneWrapper ( void *state ) {
	StateCD *st = (StateCD *)state;
	TcpSocket *socket = st->m_socket;
	log("crawlbot: done with blocked op.");
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
	//log("mdel3: st=%"XINT32"",(int32_t)st);
	g_httpServer.sendDynamicPage (socket,"OK",2);
}

// . when we receive the request from john we call broadcastRequest() from
//   Pages.cpp. then msg28 sends this replay with a &cast=0 appended to it
//   to every host in the network. then when msg28 gets back replies from all 
//   those hosts it calls sendPageCrawlbot() here but without a &cast=0
// . so if no &cast is present we are the original!!!
bool sendPageCrawlbot ( TcpSocket *socket , HttpRequest *hr ) {

	// print help
	int32_t help = hr->getLong("help",0);
	if ( help ) {
		SafeBuf sb;
		sb.safePrintf("<html>"
			      "<title>Crawlbot API</title>"
			      "<h1>Crawlbot API</h1>"
			      "<b>Use the parameters below on the "
			      "<a href=\"/crawlbot\">/crawlbot</a> page."
			      "</b><br><br>"
			      "<table>"
			      );
		for ( int32_t i = 0 ; i < 1000 ; i++ ) {
			HelpItem *h = &s_his[i];
			if ( ! h->m_parm ) break;
			sb.safePrintf( "<tr>"
				       "<td>%s</td>"
				       "<td>%s</td>"
				       "</tr>"
				       , h->m_parm
				       , h->m_desc
				       );
		}
		sb.safePrintf("</table>"
			      "</html>");
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}

	// . Pages.cpp by default broadcasts all PageCrawlbot /crawlbot 
	//   requests to every host in the network unless a cast=0 is 
	//   explicitly given
	// . Msg28::massConfig() puts a &cast=0 on the secondary requests 
	//   sent to each host in the network
	//int32_t cast = hr->getLong("cast",1);

	// httpserver/httprequest should not try to decode post if
	// it's application/json.
	//char *json = hr->getPOST();
	//Json JS; 
	//if ( json ) JS.parseJsonStringIntoJsonItems ( json );

	// . now show stats for the current crawl
	// . put in xml or json if format=xml or format=json or
	//   xml=1 or json=1 ...
	char fmt = FORMAT_JSON;

	// token is always required. get from json or html form input
	//char *token = getInputString ( "token" );
	char *token = hr->getString("token");
	char *name = hr->getString("name");

	// . try getting token-name from ?c= 
	// . the name of the collection is encoded as <token>-<crawlname>
	char *c = hr->getString("c");
	char tmp[MAX_COLL_LEN+100];
	if ( ! token && c ) {
		strncpy ( tmp , c , MAX_COLL_LEN );
		token = tmp;
		name = strstr(tmp,"-");
		if ( name ) {
			*name = '\0';
			name++;
		}
		// change default formatting to html
		fmt = FORMAT_HTML;
	}

	if (token){
			for ( int32_t i = 0 ; i < gbstrlen(token) ; i++ ){
				token[i]=tolower(token[i]);
			}
		}


	char *fs = hr->getString("format",NULL,NULL);
	// give john a json api
	if ( fs && strcmp(fs,"html") == 0 ) fmt = FORMAT_HTML;
	if ( fs && strcmp(fs,"json") == 0 ) fmt = FORMAT_JSON;
	if ( fs && strcmp(fs,"xml") == 0 ) fmt = FORMAT_XML;
	// if we got json as input, give it as output
	//if ( JS.getFirstItem() ) fmt = FORMAT_JSON;



	if ( ! token && fmt == FORMAT_JSON ) { // (cast==0|| fmt == FORMAT_JSON ) ) {
		char *msg = "invalid token";
		return sendErrorReply2 (socket,fmt,msg);
	}

	if ( ! token ) {
		// print token form if html
		SafeBuf sb;
		sb.safePrintf("In order to use crawlbot you must "
			      "first LOGIN:"
			      "<form action=/crawlbot method=get>"
			      "<br>"
			      "<input type=text name=token size=50>"
			      "<input type=submit name=submit value=OK>"
			      "</form>"
			      "<br>"
			      "<b>- OR -</b>"
			      "<br> SIGN UP"
			      "<form action=/crawlbot method=get>"
			      "Name: <input type=text name=name size=50>"
			      "<br>"
			      "Email: <input type=text name=email size=50>"
			      "<br>"
			      "<input type=submit name=submit value=OK>"
			      "</form>"
			      "</body>"
			      "</html>");
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}

	if ( gbstrlen(token) > 32 ) { 
		//log("crawlbot: token is over 32 chars");
		char *msg = "crawlbot: token is over 32 chars";
		return sendErrorReply2 (socket,fmt,msg);
	}

	char *seeds = hr->getString("seeds");
	char *spots = hr->getString("spots");

	// just existence is the operation
	//bool delColl   = hr->hasField("deleteCrawl");
	//bool resetColl = hr->hasField("resetCrawl");

	// /v2/bulk api support:
	if ( ! spots ) spots = hr->getString("urls");

	if ( spots && ! spots[0] ) spots = NULL;
	if ( seeds && ! seeds[0] ) seeds = NULL;

	//if ( ! delColl   ) delColl   = hr->hasField("delete");
	//if ( ! resetColl ) resetColl = hr->hasField("reset");

	bool restartColl = hr->hasField("restart");


	//if ( delColl && !  && cast == 0 ) {
	//	log("crawlbot: no collection found to delete.");
	//	char *msg = "Could not find crawl to delete.";
	//	return sendErrorReply2 (socket,fmt,msg);
	//}

	// just send back a list of all the collections after the delete
	//if ( delColl && cast && fmt == FORMAT_JSON ) {
	//	char *msg = "Collection deleted.";
	//	return sendReply2 (socket,fmt,msg);
	//}

	// default name to next available collection crawl name in the
	// case of a delete operation...
	char *msg = NULL;
	if ( hr->hasField("delete") ) msg = "deleted";
	// need to re-add urls for a restart
	//if ( hr->hasField("restart") ) msg = "restarted";
	if ( hr->hasField("reset") ) msg = "reset";
	if ( msg ) { // delColl && cast ) {
		// this was deleted... so is invalid now
		name = NULL;
		// no longer a delete function, we need to set "name" below
		//delColl = false;//NULL;
		// john wants just a brief success reply
		SafeBuf tmp;
		tmp.safePrintf("{\"response\":\"Successfully %s job.\"}",
			       msg);
		char *reply = tmp.getBufStart();
		if ( ! reply ) {
			if ( ! g_errno ) g_errno = ENOMEM;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
		return g_httpServer.sendDynamicPage( socket,
						     reply,
						     gbstrlen(reply),
						     0, // cacheTime
						     false, // POSTReply?
						     "application/json"
						     );
	}

	// if name is missing default to name of first existing
	// collection for this token. 
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) { // cast
		if (  name ) break;
		// do not do this if doing an
		// injection (seed) or add url or del coll or reset coll !!
		if ( seeds ) break;
		if ( spots ) break;
		//if ( delColl ) break;
		//if ( resetColl ) break;
		if ( restartColl ) break;
		CollectionRec *cx = g_collectiondb.m_recs[i];
		// deleted collections leave a NULL slot
		if ( ! cx ) continue;
		// skip if token does not match
		if ( strcmp ( cx->m_diffbotToken.getBufStart(),token) )
			continue;
		// got it
		name = cx->m_diffbotCrawlName.getBufStart();
		break;
	}

	if ( ! name ) {
		// if the token is valid
		char *ct = "application/json";
		char *msg = "{}\n";
		return g_httpServer.sendDynamicPage ( socket, 
						      msg,
						      gbstrlen(msg) ,
						      -1 , // cachetime
						      false ,
						      ct ,
						      200 ); // http status
		//log("crawlbot: no crawl name given");
		//char *msg = "invalid or missing name";
		//return sendErrorReply2 (socket,fmt,msg);
	}


	if ( gbstrlen(name) > 30 ) { 
		//log("crawlbot: name is over 30 chars");
		char *msg = "crawlbot: name is over 30 chars";
		return sendErrorReply2 (socket,fmt,msg);
	}

	// make the collection name so it includes the token and crawl name
	char collName[MAX_COLL_LEN+1];
	// sanity
	if ( MAX_COLL_LEN < 64 ) { char *xx=NULL;*xx=0; }
	// make a compound name for collection of token and name
	sprintf(collName,"%s-%s",token,name);

	// if they did not specify the token/name of an existing collection
	// then cr will be NULL and we'll add it below
	CollectionRec *cr = g_collectiondb.getRec(collName);

	// i guess bail if not there?
	if ( ! cr ) {
		log("crawlbot: missing coll rec for coll %s",collName);
		//char *msg = "invalid or missing collection rec";
		char *msg = "Could not create job because missing seeds or "
			"urls.";
		return sendErrorReply2 (socket,fmt,msg);
	}


	// if no token... they need to login or signup
	//char *token = getTokenFromHttpRequest ( hr );

	// get coll name if any
	//char *c = hr->getString("c");
	//if ( ! c ) c = hr->getString("id");

	// get some other parms provided optionally
	//char *addColl   = hr->getString("addcoll");

	// try json
	//if ( JS.getInputString("addNewCrawl") ) addColl = collName;
	//if ( JS.getInputString("deleteCrawl") ) delColl = true;
	//if ( JS.getInputString("resetCrawl") ) resetColl = true;

	//if ( resetColl && ! cr ) {
	//	//log("crawlbot: no collection found to reset.");
	//	char *msg = "Could not find crawl to reset.";
	//	return sendErrorReply2 (socket,fmt,msg);
	//}

	//if ( restartColl && ! cr ) {
	//	char *msg = "Could not find crawl to restart.";
	//	return sendErrorReply2 (socket,fmt,msg);
	//}

	// make a new state
	StateCD *st;
	try { st = new (StateCD); }
	catch ( ... ) {
		return sendErrorReply2 ( socket , fmt , mstrerror(g_errno));
	}
	mnew ( st , sizeof(StateCD), "statecd");

	// debug
	//log("mnew2: st=%"XINT32"",(int32_t)st);

	// copy crap
	st->m_hr.copy ( hr );
	st->m_socket = socket;
	st->m_fmt = fmt;
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;

	// save seeds
	if ( cr && restartColl ) { // && cast ) {
		// bail on OOM saving seeds
		if ( ! st->m_seedBank.safeMemcpy ( &cr->m_diffbotSeeds ) ||
		     ! st->m_seedBank.pushChar('\0') ) {
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
	}

	//
	// if we can't compile the provided regexes, return error
	//
	if ( cr ) {
		char *rx1 = hr->getString("urlCrawlRegEx",NULL);
		if ( rx1 && ! rx1[0] ) rx1 = NULL;
		char *rx2 = hr->getString("urlProcessRegEx",NULL);
		if ( rx2 && ! rx2[0] ) rx2 = NULL;
		// this will store the compiled regular expression into ucr
		regex_t re1;
		regex_t re2;
		int32_t status1 = 0;
		int32_t status2 = 0;
		if ( rx1 )
			status1 = regcomp ( &re1 , rx1 ,
					    REG_EXTENDED|REG_ICASE|
					    REG_NEWLINE|REG_NOSUB);
		if ( rx2 )
			status2 = regcomp ( &re2 , rx2 ,
					    REG_EXTENDED|REG_ICASE|
					    REG_NEWLINE|REG_NOSUB);
		if ( rx1 ) regfree ( &re1 );
		if ( rx2 ) regfree ( &re2 );
		SafeBuf em;
		if ( status1 ) {
			log("xmldoc: regcomp %s failed.",rx1);
			em.safePrintf("Invalid regular expression: %s",rx1);
		}
		else if ( status2 ) {
			log("xmldoc: regcomp %s failed.",rx2);
			em.safePrintf("Invalid regular expression: %s",rx2);
		}
		if ( status1 || status2 ) {
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			char *msg = em.getBufStart();
			return sendErrorReply2(socket,fmt,msg);
		}
	}
		

			

	// . if this is a cast=0 request it is received by all hosts in the 
	//   network
	// . this code is the only code run by EVERY host in the network
	// . the other code is just run once by the receiving host
	// . so we gotta create a coll rec on each host etc.
	// . no need to update collectionrec parms here since Pages.cpp calls
	//   g_parms.setFromRequest() for us before calling this function,
	//   pg->m_function(). even though maxtocrawl is on "PAGE_NONE" 
	//   hopefully it will still be set
	// . but we should take care of add/del/reset coll here.
	// . i guess this will be handled by the new parm syncing logic
	//   which deals with add/del coll requests

	/*
	if ( cast == 0 ) {
		// add a new collection by default
		if ( ! cr && name && name[0] ) 
			cr = addNewDiffbotColl ( collName , token , name, hr );
		// also support the good 'ole html form interface
		if ( cr ) setSpiderParmsFromHtmlRequest ( socket , hr , cr );
		// . we can't sync these operations on a dead host when it
		//   comes back up yet. we can only sync parms, not collection
		//   adds/deletes/resets
		// . TODO: make new collections just a list of rdb records, 
		//   then they can leverage the msg4 and addsinprogress.dat 
		//   functionality we have for getting dead hosts back up to 
		//   sync. Call it Colldb.
		// . PROBLEM: when just starting up seems like hasDeadHost()
		//   is returning true because it has not yet received its
		//   first ping reply
		//if ( addColl || delColl || resetColl ) {
		//	// if any host in network is dead, do not do this
		//	if ( g_hostdb.hasDeadHost() )  {
		//		char *msg = "A host in the network is dead.";
		//		// log it
		//		log("crawlbot: %s",msg);
		//		// make sure this returns in json if required
		//		return sendErrorReply2(socket,fmt,msg);
		//	}
		//}

		// problem?
		if ( ! cr ) {
			// send back error
			char *msg = "Collection add failed";
			if ( delColl ) msg = "No such collection";
			if ( resetColl ) msg = "No such collection";
			if ( restartColl ) msg = "No such collection";
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			// log it
			log("crawlbot: cr is null. %s",msg);
			// make sure this returns in json if required
			return sendErrorReply2(socket,fmt,msg);
		}


		// set this up
		WaitEntry *we = &st->m_waitEntry;
		we->m_state = st;
		we->m_callback = collOpDoneWrapper;
		// this won't work, collname is on the stack!
		//we->m_coll = collName;
		we->m_coll = cr->m_coll;

		if ( delColl ) {
			// note it
			log("crawlbot: deleting coll");
			// delete collection name
			// this can block if tree is saving, it has to wait
			// for tree save to complete before removing old
			// collnum recs from tree
			if ( ! g_collectiondb.deleteRec ( collName , we ) )
				return false;
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			// all done
			return g_httpServer.sendDynamicPage (socket,"OK",2);
		}

		if ( resetColl || restartColl ) {
			// note it
			log("crawlbot: resetting/restarting coll");
			//cr = g_collectiondb.getRec ( resetColl );
			// this can block if tree is saving, it has to wait
			// for tree save to complete before removing old
			// collnum recs from tree
			bool purgeSeeds = true;
			if ( restartColl ) purgeSeeds = false;
			if ( ! g_collectiondb.resetColl ( collName , 
							  we ,
							  purgeSeeds ) )
				return false;
			// it is a NEW ptr now!
			cr = g_collectiondb.getRec( collName );
			// if reset from crawlbot api page then enable spiders
			// to avoid user confusion
			if ( cr ) cr->m_spideringEnabled = 1;
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			// all done
			return g_httpServer.sendDynamicPage (socket,"OK",2);
		}
		// nuke it
		mdelete ( st , sizeof(StateCD) , "stcd" );
		delete st;
		// this will set the the collection parms from json
		//setSpiderParmsFromJSONPost ( socket , hr , cr , &JS );
		// this is a cast, so just return simple response
		return g_httpServer.sendDynamicPage (socket,"OK",2);
	}
	*/

	/////////
	//
	// after all hosts have replied to the request, we finally send the
	// request here, with no &cast=0 appended to it. so there is where we
	// send the final reply back to the browser
	//
	/////////

	/*
	// in case collection was just added above... try this!!
	cr = g_collectiondb.getRec(collName);

	// collectionrec must be non-null at this point. i.e. we added it
	if ( ! cr ) {
		char *msg = "Crawl name was not found.";
		if ( name && name[0] )
			msg = "Failed to add crawl. Crawl name is illegal.";
		// nuke it
		mdelete ( st , sizeof(StateCD) , "stcd" );
		delete st;
		//log("crawlbot: no collection found. need to add a crawl");
		return sendErrorReply2(socket,fmt, msg);
	}

	//char *spots = hr->getString("spots",NULL,NULL);
	//char *seeds = hr->getString("seeds",NULL,NULL);
	*/

	// check seed bank now too for restarting a crawl
	if ( st->m_seedBank.length() && ! seeds )
		seeds = st->m_seedBank.getBufStart();

	char *coll = "NONE";
	if ( cr ) coll = cr->m_coll;

	if ( seeds )
		log("crawlbot: adding seeds=\"%s\" coll=%s (%"INT32")",
		    seeds,coll,(int32_t)st->m_collnum);

	char bulkurlsfile[1024];
	// when a collection is restarted the collnum changes to avoid
	// adding any records destined for that collnum that might be on 
	// the wire. so just put these in the root dir
	snprintf(bulkurlsfile, 1024, "%sbulkurls-%s.txt", 
		 g_hostdb.m_dir , coll );//, (int32_t)st->m_collnum );
	if ( spots && cr && cr->m_isCustomCrawl == 2 ) {
	    int32_t spotsLen = (int32_t)gbstrlen(spots);
		log("crawlbot: got spots (len=%"INT32") to add coll=%s (%"INT32")",
		    spotsLen,coll,(int32_t)st->m_collnum);
		FILE *f = fopen(bulkurlsfile, "w");
		if (f != NULL) {
		    // urls are space separated.
		    // as of 5/14/2014, it appears that spots is space-separated for some URLs (the first two)
		    // and newline-separated for the remainder. Make a copy that's space separated so that restarting bulk jobs works.
		    // Alternatives:
		    //  1) just write one character to disk at a time, replacing newlines with spaces
		    //  2) just output what you have, and then when you read in, replace newlines with spaces
		    //  3) probably the best option: change newlines to spaces earlier in the pipeline
		    char *spotsCopy = (char*) mmalloc(spotsLen+1, "create a temporary copy of spots that we're about to delete");
		    for (int i = 0; i < spotsLen; i++) {
		        char c = spots[i];
		        if (c == '\n')
		            c = ' ';
		        spotsCopy[i] = c;
		    }
		    spotsCopy[spotsLen] = '\0';
		    fprintf(f, "%s", spotsCopy);
		    fclose(f);
		    mfree(spotsCopy, spotsLen+1, "no longer need copy");
		}
	}

	// if restart flag is on and the file with bulk urls exists, 
	// get spots from there
	SafeBuf bb;
	if ( !spots && restartColl && cr && cr->m_isCustomCrawl == 2 ) {
		bb.load(bulkurlsfile);
		bb.nullTerm();
		spots = bb.getBufStart();
		log("crawlbot: restarting bulk job file=%s bufsize=%"INT32" for %s",
		    bulkurlsfile,bb.length(), cr->m_coll);
	}
	/*
	    FILE *f = fopen(bulkurlsfile, "r");
	    if (f != NULL) {
	        fseek(f, 0, SEEK_END);
	        int32_t size = ftell(f);
	        fseek(f, 0, SEEK_SET);
	        char *bulkurls = (char*) mmalloc(size, "reading in bulk urls");
		if ( ! bulkurls ) {
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
	        fgets(bulkurls, size, f);
	        spots = bulkurls;
		fclose(f);
	    }
	}
	*/

	///////
	// 
	// handle file of urls upload. can be HUGE!
	//
	///////
	if ( spots || seeds ) {
		// error
		if ( g_repair.isRepairActive() &&
		     g_repair.m_collnum == st->m_collnum ) {
			log("crawlbot: repair active. can't add seeds "
			    "or spots while repairing collection.");
			g_errno = EREPAIRING;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
		// . avoid spidering links for these urls? i would say
		// . default is to NOT spider the links...
		// . support camel case and all lower case
		//int32_t spiderLinks = hr->getLong("spiderLinks",1);
		//spiderLinks      = hr->getLong("spiderlinks",spiderLinks);
		//bool spiderLinks = false;
		// make a list of spider requests from these urls
		//SafeBuf listBuf;
		// this returns NULL with g_errno set
		bool status = true;
		if ( ! getSpiderRequestMetaList ( seeds,
						  &st->m_listBuf ,
						  true , // spiderLinks?
						  cr ) )
			status = false;
		// do not spider links for spots
		if ( ! getSpiderRequestMetaList ( spots,
						  &st->m_listBuf ,
						  false , // spiderLinks?
						  NULL ) )
			status = false;
		// empty?
		int32_t size = st->m_listBuf.length();
		// error?
		if ( ! status ) {
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,mstrerror(g_errno));
		}
		// if not list
		if ( ! size ) {
			// nuke it
			mdelete ( st , sizeof(StateCD) , "stcd" );
			delete st;
			return sendErrorReply2(socket,fmt,"no urls found");
		}
		// add to spiderdb
		if ( ! st->m_msg4.addMetaList( st->m_listBuf.getBufStart() ,
					       st->m_listBuf.length(),
					       cr->m_coll,
					       st ,
					       addedUrlsToSpiderdbWrapper,
					       0 // niceness
					       ) )
			// blocked!
			return false;
		// did not block, print page!
		addedUrlsToSpiderdbWrapper(st);
		return true;
	}

	/////////
	//
	// handle direct injection of a url. looks at "spiderlinks=1" parm
	// and all the other parms in Msg7::inject() in PageInject.cpp.
	//
	//////////
	/*
	if ( injectUrl ) {
		// a valid collection is required
		if ( ! cr ) 
			return sendErrorReply2(socket,fmt,
					       "invalid collection");
		// begin the injection
		if ( ! st->m_msg7.inject ( st->m_socket,
					   &st->m_hr,
					   st ,
					   injectedUrlWrapper ,
					   1 , // spiderLinks default is on
					   collName ) ) // coll override
			// if blocked, return now
			return false;
		// otherwise send back reply
		injectedUrlWrapper ( st );
		return true;
	}
	*/

	// we do not need the state i guess

	////////////
	//
	// print the html or json page of all the data
	//
	printCrawlBotPage2 ( socket,hr,fmt,NULL,NULL,cr->m_collnum);

	// get rid of that state
	mdelete ( st , sizeof(StateCD) , "stcd" );
	delete st;
	//log("mdel4: st=%"XINT32"",(int32_t)st);
	return true;
}


/*
bool printUrlFilters ( SafeBuf &sb , CollectionRec *cr , int32_t fmt ) {

	if ( fmt == FORMAT_JSON )
		sb.safePrintf("\"urlFilters\":[");

	// skip first filters that are:
	// 0. ismedia->ignore and
	// 1. !isonsamedomain->ignore
	// 2. lastspidertime or !isindexed
	// 3. errorcount rule
	// 4. errorcount rule

	int32_t istart = 5;
	// if respidering then we added an extra filter 
	// lastspidertime>={roundstart} --> FILTERED
	//if ( cr->m_collectiveRespiderFrequency > 0.0 )
	//	istart++;

	for ( int32_t i = istart ; i < cr->m_numRegExs ; i++ ) {
		//sb.safePrintf
		char *expression = cr->m_regExs[i].getBufStart();
		// do not allow nulls
		if ( ! expression ) expression = "";
		// skip spaces
		if ( *expression && is_wspace_a(*expression) ) expression++;
		if ( strcmp(expression,"default") == 0 ) expression = "*";
		char *action = cr->m_spiderDiffbotApiUrl[i].getBufStart();
		// do not all nulls
		if ( ! action ) action = "";
		// skip spaces
		if ( *action && is_wspace_a(*action) ) action++;
		// if no diffbot api url specified, do not process
		if ( ! *action ) action = "doNotProcess";
		// if filtered from crawling, do not even spider
		int32_t priority = cr->m_spiderPriorities[i];
		if ( priority == SPIDER_PRIORITY_FILTERED ) // -3
			action = "doNotCrawl";
		// we add this supplemental expressin/action for every
		// one the user adds in order to give manually added
		// urls higher spider priority, so skip it
		if ( strncmp(expression,"ismanualadd && ",15) == 0 )
			continue;
		if ( fmt == FORMAT_HTML ) {
			sb.safePrintf("<tr>"
				      "<td>Expression "
				      "<input type=text "
				      "name=expression size=30 "
				      "value=\"%s\"> "
				      "</td><td>"
				      "Action "
				      "<input type=text name=action size=50 "
				      "value=\"%s\">"
				      "</td>"
				      "</tr>\n"
				      , expression
				      , action
				      );
			continue;
		}
		// show it
		sb.safePrintf("{\"expression\":\"%s\",",expression);
		sb.safePrintf("\"action\":\"%s\"}",action);
		// more follow?
		sb.pushChar(',');
		sb.pushChar('\n');
	}

	if ( fmt == FORMAT_JSON ) {
		// remove trailing comma
		sb.removeLastChar('\n');
		sb.removeLastChar(',');
		sb.safePrintf("]\n");
	}

	return true;
}
*/

bool printCrawlDetailsInJson ( SafeBuf *sb , CollectionRec *cx ) {
    return printCrawlDetailsInJson( sb , cx , HTTP_REQUEST_DEFAULT_REQUEST_VERSION);
}

bool printCrawlDetailsInJson ( SafeBuf *sb , CollectionRec *cx, int version ) {

	SafeBuf tmp;
	int32_t crawlStatus = -1;
	getSpiderStatusMsg ( cx , &tmp , &crawlStatus );
	CrawlInfo *ci = &cx->m_localCrawlInfo;
	int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
	if ( sentAlert ) sentAlert = 1;

	char *crawlTypeStr = "crawl";
	//char *nomen = "crawl";
	if ( cx->m_isCustomCrawl == 2 ) {
		crawlTypeStr = "bulk";
		//nomen = "job";
	}

	// don't print completed time if spidering is going on
	uint32_t completed = cx->m_diffbotCrawlEndTime;
	// if not yet done, make this zero
	if ( crawlStatus == SP_INITIALIZING ) completed = 0;
	if ( crawlStatus == SP_NOURLS ) completed = 0;
	//if ( crawlStatus == SP_PAUSED ) completed = 0;
	//if ( crawlStatus == SP_ADMIN_PAUSED ) completed = 0;
	if ( crawlStatus == SP_INPROGRESS ) completed = 0;

	sb->safePrintf("\n\n{"
		      "\"name\":\"%s\",\n"
		      "\"type\":\"%s\",\n"

		       "\"jobCreationTimeUTC\":%"INT32",\n"
		       "\"jobCompletionTimeUTC\":%"INT32",\n"

		      //"\"alias\":\"%s\",\n"
		      //"\"crawlingEnabled\":%"INT32",\n"
		      "\"jobStatus\":{" // nomen = jobStatus / crawlStatus
		      "\"status\":%"INT32","
		      "\"message\":\"%s\"},\n"
		      "\"sentJobDoneNotification\":%"INT32",\n"
		      //"\"crawlingPaused\":%"INT32",\n"
		      "\"objectsFound\":%"INT64",\n"
		      "\"urlsHarvested\":%"INT64",\n"
		      //"\"urlsExamined\":%"INT64",\n"
		      "\"pageCrawlAttempts\":%"INT64",\n"
		      "\"pageCrawlSuccesses\":%"INT64",\n"
		      "\"pageCrawlSuccessesThisRound\":%"INT64",\n"

		      "\"pageProcessAttempts\":%"INT64",\n"
		      "\"pageProcessSuccesses\":%"INT64",\n"
		      "\"pageProcessSuccessesThisRound\":%"INT64",\n"

		      "\"maxRounds\":%"INT32",\n"
		      "\"repeat\":%f,\n"
		      "\"crawlDelay\":%f,\n"

		      //,cx->m_coll
		      , cx->m_diffbotCrawlName.getBufStart()
		      , crawlTypeStr

		       , cx->m_diffbotCrawlStartTime
		       // this is 0 if not over yet
		       , completed

		      //, alias
		      //, (int32_t)cx->m_spideringEnabled
		      , crawlStatus
		      , tmp.getBufStart()
		      , sentAlert
		      //, (int32_t)paused
		      , cx->m_globalCrawlInfo.m_objectsAdded -
		      cx->m_globalCrawlInfo.m_objectsDeleted
		      , cx->m_globalCrawlInfo.m_urlsHarvested
		      //,cx->m_globalCrawlInfo.m_urlsConsidered
		      , cx->m_globalCrawlInfo.m_pageDownloadAttempts
		      , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
		      , cx->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound

		      , cx->m_globalCrawlInfo.m_pageProcessAttempts
		      , cx->m_globalCrawlInfo.m_pageProcessSuccesses
		      , cx->m_globalCrawlInfo.m_pageProcessSuccessesThisRound

		      , (int32_t)cx->m_maxCrawlRounds
		      , cx->m_collectiveRespiderFrequency
		      , cx->m_collectiveCrawlDelay
		      );

	sb->safePrintf("\"obeyRobots\":%"INT32",\n"
		      , (int32_t)cx->m_useRobotsTxt );

	// if not a "bulk" injection, show crawl stats
	if ( cx->m_isCustomCrawl != 2 ) {

		sb->safePrintf(
			      // settable parms
			      "\"maxToCrawl\":%"INT64",\n"
			      "\"maxToProcess\":%"INT64",\n"
			      //"\"restrictDomain\":%"INT32",\n"
			      "\"onlyProcessIfNew\":%"INT32",\n"
			      , cx->m_maxToCrawl
			      , cx->m_maxToProcess
			      //, (int32_t)cx->m_restrictDomain
			      , (int32_t)cx->m_diffbotOnlyProcessIfNewUrl
			      );
		sb->safePrintf("\"seeds\":\"");
		sb->safeUtf8ToJSON ( cx->m_diffbotSeeds.getBufStart());
		sb->safePrintf("\",\n");
	}

	sb->safePrintf("\"roundsCompleted\":%"INT32",\n",
		      cx->m_spiderRoundNum);

	sb->safePrintf("\"roundStartTime\":%"UINT32",\n",
		      cx->m_spiderRoundStartTime);

	sb->safePrintf("\"currentTime\":%"UINT32",\n",
		       (uint32_t)getTimeGlobal() );
	sb->safePrintf("\"currentTimeUTC\":%"UINT32",\n",
		       (uint32_t)getTimeGlobal() );


	sb->safePrintf("\"apiUrl\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotApiUrl.getBufStart() );
	sb->safePrintf("\",\n");


	sb->safePrintf("\"urlCrawlPattern\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlCrawlPattern.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"urlProcessPattern\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlProcessPattern.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"pageProcessPattern\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotPageProcessPattern.getBufStart() );
	sb->safePrintf("\",\n");


	sb->safePrintf("\"urlCrawlRegEx\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlCrawlRegEx.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"urlProcessRegEx\":\"");
	sb->safeUtf8ToJSON ( cx->m_diffbotUrlProcessRegEx.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"maxHops\":%"INT32",\n",
		       (int32_t)cx->m_diffbotMaxHops);

	char *token = cx->m_diffbotToken.getBufStart();
	char *name = cx->m_diffbotCrawlName.getBufStart();

	char *mt = "crawl";
	if ( cx->m_isCustomCrawl == 2 ) mt = "bulk";

	sb->safePrintf("\"downloadJson\":"
		      "\"http://api.diffbot.com/v%d/%s/download/"
		      "%s-%s_data.json\",\n"
	          , version
		      , mt
		      , token
		      , name
		      );

	sb->safePrintf("\"downloadUrls\":"
		      "\"http://api.diffbot.com/v%d/%s/download/"
		      "%s-%s_urls.csv\",\n"
	          , version
		      , mt
		      , token
		      , name
		      );

	sb->safePrintf("\"notifyEmail\":\"");
	sb->safeUtf8ToJSON ( cx->m_notifyEmail.getBufStart() );
	sb->safePrintf("\",\n");

	sb->safePrintf("\"notifyWebhook\":\"");
	sb->safeUtf8ToJSON ( cx->m_notifyUrl.getBufStart() );
	sb->safePrintf("\"\n");
	//sb->safePrintf("\",\n");

	/////
	//
	// show url filters table. kinda hacky!!
	//
	/////
	/*
	  g_parms.sendPageGeneric ( socket ,
	  hr ,
	  PAGE_FILTERS ,
	  NULL ,
	  &sb ,
	  cr->m_coll,  // coll override
	  true // isJSON?
	  );
	*/
	//printUrlFilters ( sb , cx , FORMAT_JSON );
	// end that collection rec
	sb->safePrintf("}\n");

	return true;
}

bool printCrawlDetails2 (SafeBuf *sb , CollectionRec *cx , char format ) {

	SafeBuf tmp;
	int32_t crawlStatus = -1;
	getSpiderStatusMsg ( cx , &tmp , &crawlStatus );
	CrawlInfo *ci = &cx->m_localCrawlInfo;
	int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
	if ( sentAlert ) sentAlert = 1;

	// don't print completed time if spidering is going on
	uint32_t completed = cx->m_diffbotCrawlEndTime; // time_t
	// if not yet done, make this zero
	if ( crawlStatus == SP_INITIALIZING ) completed = 0;
	if ( crawlStatus == SP_NOURLS ) completed = 0;
	//if ( crawlStatus == SP_PAUSED ) completed = 0;
	//if ( crawlStatus == SP_ADMIN_PAUSED ) completed = 0;
	if ( crawlStatus == SP_INPROGRESS ) completed = 0;

	if ( format == FORMAT_JSON ) {
		sb->safePrintf("{"
			       "\"response:{\n"
			       "\t\"statusCode\":%"INT32",\n"
			       "\t\"statusMsg\":\"%s\",\n"
			       "\t\"jobCreationTimeUTC\":%"INT32",\n"
			       "\t\"jobCompletionTimeUTC\":%"INT32",\n"
			       "\t\"sentJobDoneNotification\":%"INT32",\n"
			       "\t\"urlsHarvested\":%"INT64",\n"
			       "\t\"pageCrawlAttempts\":%"INT64",\n"
			       "\t\"pageCrawlSuccesses\":%"INT64",\n"
			       , crawlStatus
			       , tmp.getBufStart()
			       , cx->m_diffbotCrawlStartTime
			       , completed
			       , sentAlert
			       , cx->m_globalCrawlInfo.m_urlsHarvested
			       , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			       , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			       );
		sb->safePrintf("\t\"currentTime\":%"UINT32",\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t\"currentTimeUTC\":%"UINT32",\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t}\n");
		sb->safePrintf("}\n");
	}

	if ( format == FORMAT_XML ) {
		sb->safePrintf("<response>\n"
			       "\t<statusCode>%"INT32"</statusCode>\n"
			       , crawlStatus
			       );
		sb->safePrintf(
			       "\t<statusMsg><![CDATA[%s]]></statusMsg>\n"
			       "\t<jobCreationTimeUTC>%"INT32""
			       "</jobCreationTimeUTC>\n"
			       , (char *)tmp.getBufStart()
			       , (int32_t)cx->m_diffbotCrawlStartTime
			       );
		sb->safePrintf(
			       "\t<jobCompletionTimeUTC>%"INT32""
			       "</jobCompletionTimeUTC>\n"

			       "\t<sentJobDoneNotification>%"INT32""
			       "</sentJobDoneNotification>\n"

			       "\t<urlsHarvested>%"INT64"</urlsHarvested>\n"

			       "\t<pageCrawlAttempts>%"INT64""
			       "</pageCrawlAttempts>\n"

			       "\t<pageCrawlSuccesses>%"INT64""
			       "</pageCrawlSuccesses>\n"

			       , completed
			       , sentAlert
			       , cx->m_globalCrawlInfo.m_urlsHarvested
			       , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			       , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			       );
		sb->safePrintf("\t<currentTime>%"UINT32"</currentTime>\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("\t<currentTimeUTC>%"UINT32"</currentTimeUTC>\n",
			       (uint32_t)getTimeGlobal() );
		sb->safePrintf("</response>\n");
	}

	return true;
}

bool printCrawlBotPage2 ( TcpSocket *socket , 
			  HttpRequest *hr ,
			  char fmt, // format
			  SafeBuf *injectionResponse ,
			  SafeBuf *urlUploadResponse ,
			  collnum_t collnum ) {
	

	// store output into here
	SafeBuf sb;

	if ( fmt == FORMAT_HTML )
		sb.safePrintf(
			      "<html>"
			      "<title>Crawlbot - "
			      "Web Data Extraction and Search Made "
			      "Easy</title>"
			      "<body>"
			      );

	CollectionRec *cr = g_collectiondb.m_recs[collnum];

	// was coll deleted while adding urls to spiderdb?
	if ( ! cr ) {
		g_errno = EBADREQUEST;
		char *msg = "invalid crawl. crawl was deleted.";
		return sendErrorReply2(socket,fmt,msg);
	}

	char *token = cr->m_diffbotToken.getBufStart();
	char *name = cr->m_diffbotCrawlName.getBufStart();

	// this is usefful
	SafeBuf hb;
	hb.safePrintf("<input type=hidden name=name value=\"%s\">"
		      "<input type=hidden name=token value=\"%s\">"
		      "<input type=hidden name=format value=\"html\">"
		      , name
		      , token );
	hb.nullTerm();

	// and this
	SafeBuf lb;
	lb.safePrintf("name=");
	lb.urlEncode(name);
	lb.safePrintf ("&token=");
	lb.urlEncode(token);
	if ( fmt == FORMAT_HTML ) lb.safePrintf("&format=html");
	lb.nullTerm();
	

	// set this to current collection. if only token was provided
	// then it will return the first collection owned by token.
	// if token has no collections it will be NULL.
	//if ( ! cr ) 
	//	cr = getCollRecFromHttpRequest ( hr );

	//if ( ! cr ) {
	//	char *msg = "failed to add new collection";
	//	g_msg = " (error: crawlbot failed to allocate crawl)";
	//	return sendErrorReply2 ( socket , fmt , msg );
	//}
			

	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf("<table border=0>"
			      "<tr><td>"
			      "<b><font size=+2>"
			      "<a href=/crawlbot?token=%s>"
			      "Crawlbot</a></font></b>"
			      "<br>"
			      "<font size=-1>"
			      "Crawl, Datamine and Index the Web"
			      "</font>"
			      "</td></tr>"
			      "</table>"
			      , token
			      );
		sb.safePrintf("<center><br>");
		// first print help
		sb.safePrintf("[ <a href=/crawlbot?help=1>"
			      "api help</a> ] &nbsp; "
			      // json output
			      "[ <a href=\"/crawlbot?token=%s&format=json&"
			      "name=%s\">"
			      "json output"
			      "</a> ] &nbsp; "
			      , token 
			      , name );
		// random coll name to add
		uint32_t r1 = rand();
		uint32_t r2 = rand();
		uint64_t rand64 = (uint64_t) r1;
		rand64 <<= 32;
		rand64 |=  r2;
		char newCollName[MAX_COLL_LEN+1];
		snprintf(newCollName,MAX_COLL_LEN,"%s-%016"XINT64"",
			 token , rand64 );
		// first print "add new collection"
		sb.safePrintf("[ <a href=/crawlbot?name=%016"XINT64"&token=%s&"
			      "format=html&addCrawl=%s>"
			      "add new crawl"
			      "</a> ] &nbsp; "
			      "[ <a href=/crawlbot?token=%s>"
			      "show all crawls"
			      "</a> ] &nbsp; "
			      , rand64
			      , token
			      , newCollName
			      , token
			      );
	}
	

	bool firstOne = true;

	//
	// print list of collections controlled by this token
	//
	for ( int32_t i = 0 ; fmt == FORMAT_HTML && i<g_collectiondb.m_numRecs;i++ ){
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// get its token if any
		char *ct = cx->m_diffbotToken.getBufStart();
		if ( ! ct ) continue;
		// skip if token does not match
		if ( strcmp(ct,token) )
			continue;
		// highlight the tab if it is what we selected
		bool highlight = false;
		if ( cx == cr ) highlight = true;
		char *style = "";
		if  ( highlight ) {
			style = "style=text-decoration:none; ";
			sb.safePrintf ( "<b><font color=red>");
		}
		// print the crawl id. collection name minus <TOKEN>-
		sb.safePrintf("<a %shref=/crawlbot?token=", style);
		sb.urlEncode(token);
		sb.safePrintf("&name=");
		sb.urlEncode(cx->m_diffbotCrawlName.getBufStart());
		sb.safePrintf("&format=html>"
			      "%s (%"INT32")"
			      "</a> &nbsp; "
			      , cx->m_diffbotCrawlName.getBufStart()
			      , (int32_t)cx->m_collnum
			      );
		if ( highlight )
			sb.safePrintf("</font></b>");
	}

	if ( fmt == FORMAT_HTML )
		sb.safePrintf ( "</center><br/>" );

	// the ROOT JSON [
	if ( fmt == FORMAT_JSON )
		sb.safePrintf("{\n");

	// injection is currently not in use, so this is an artifact:
	if ( fmt == FORMAT_JSON && injectionResponse )
		sb.safePrintf("\"response\":\"%s\",\n\n"
			      , injectionResponse->getBufStart() );

	if ( fmt == FORMAT_JSON && urlUploadResponse )
		sb.safePrintf("\"response\":\"%s\",\n\n"
			      , urlUploadResponse->getBufStart() );


	//////
	//
	// print collection summary page
	//
	//////

	// the items in the array now have type:bulk or type:crawl
	// so call them 'jobs'
	if ( fmt == FORMAT_JSON )
		sb.safePrintf("\"jobs\":[");//\"collections\":");

	int32_t summary = hr->getLong("summary",0);
	// enter summary mode for json
	if ( fmt != FORMAT_HTML ) summary = 1;
	// start the table
	if ( summary && fmt == FORMAT_HTML ) {
		sb.safePrintf("<table border=1 cellpadding=5>"
			      "<tr>"
			      "<td><b>Collection</b></td>"
			      "<td><b>Objects Found</b></td>"
			      "<td><b>URLs Harvested</b></td>"
			      "<td><b>URLs Examined</b></td>"
			      "<td><b>Page Download Attempts</b></td>"
			      "<td><b>Page Download Successes</b></td>"
			      "<td><b>Page Download Successes This Round"
			      "</b></td>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td><b>Page Process Successes</b></td>"
			      "<td><b>Page Process Successes This Round"
			      "</b></td>"
			      "</tr>"
			      );
	}

	char *name3 = hr->getString("name");

	// scan each coll and get its stats
	for ( int32_t i = 0 ; summary && i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// must belong to us
		if ( strcmp(cx->m_diffbotToken.getBufStart(),token) )
			continue;


		// just print out single crawl info for json
		if ( fmt != FORMAT_HTML && cx != cr && name3 ) 
			continue;

		// if json, print each collectionrec
		if ( fmt == FORMAT_JSON ) {
			if ( ! firstOne ) 
				sb.safePrintf(",\n\t");
			firstOne = false;
			//char *alias = "";
			//if ( cx->m_collectionNameAlias.length() > 0 )
			//	alias=cx->m_collectionNameAlias.getBufStart();
			//int32_t paused = 1;

			//if ( cx->m_spideringEnabled ) paused = 0;
			if ( cx->m_isCustomCrawl )
				printCrawlDetailsInJson ( &sb , cx , 
						  getVersionFromRequest(hr) );
			else
				printCrawlDetails2 ( &sb,cx,FORMAT_JSON );

			// print the next one out
			continue;
		}


		// print in table
		sb.safePrintf("<tr>"
			      "<td>%s</td>"
			      "<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      //"<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      "<td>%"INT64"</td>"
			      "</tr>"
			      , cx->m_coll
			      , cx->m_globalCrawlInfo.m_objectsAdded -
			        cx->m_globalCrawlInfo.m_objectsDeleted
			      , cx->m_globalCrawlInfo.m_urlsHarvested
			      //, cx->m_globalCrawlInfo.m_urlsConsidered
			      , cx->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cx->m_globalCrawlInfo.m_pageDownloadSuccesses
			      , cx->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound
			      , cx->m_globalCrawlInfo.m_pageProcessAttempts
			      , cx->m_globalCrawlInfo.m_pageProcessSuccesses
			      , cx->m_globalCrawlInfo.m_pageProcessSuccessesThisRound
			      );
	}
	if ( summary && fmt == FORMAT_HTML ) {
		sb.safePrintf("</table></html>" );
		return g_httpServer.sendDynamicPage (socket, 
						     sb.getBufStart(), 
						     sb.length(),
						     0); // cachetime
	}

	if ( fmt == FORMAT_JSON ) 
		// end the array of collection objects
		sb.safePrintf("\n]\n");

	///////
	//
	// end print collection summary page
	//
	///////


	//
	// show urls being crawled (ajax) (from Spider.cpp)
	//
	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf ( "<table width=100%% cellpadding=5 "
				"style=border-width:1px;border-style:solid;"
				"border-color:black;>"
				//"bgcolor=#%s>\n" 
				"<tr><td colspan=50>"// bgcolor=#%s>"
				"<b>Last 10 URLs</b> (%"INT32" spiders active)"
				//,LIGHT_BLUE
				//,DARK_BLUE
				,(int32_t)g_spiderLoop.m_numSpidersOut);
		char *str = "<font color=green>Resume Crawl</font>";
		int32_t pval = 0;
		if ( cr->m_spideringEnabled )  {
			str = "<font color=red>Pause Crawl</font>";
			pval = 1;
		}
		sb.safePrintf(" "
			      "<a href=/crawlbot?%s"
			      "&pauseCrawl=%"INT32"><b>%s</b></a>"
			      , lb.getBufStart() // has &name=&token= encoded
			      , pval
			      , str
			      );

		sb.safePrintf("</td></tr>\n" );

		// the table headers so SpiderRequest::printToTable() works
		if ( ! SpiderRequest::printTableHeaderSimple(&sb,true) ) 
			return false;
		// int16_tcut
		XmlDoc **docs = g_spiderLoop.m_docs;
		// row count
		int32_t j = 0;
		// first print the spider recs we are spidering
		for ( int32_t i = 0 ; i < (int32_t)MAX_SPIDERS ; i++ ) {
			// get it
			XmlDoc *xd = docs[i];
			// skip if empty
			if ( ! xd ) continue;
			// sanity check
			if ( ! xd->m_sreqValid ) { char *xx=NULL;*xx=0; }
			// skip if not our coll rec!
			//if ( xd->m_cr != cr ) continue;
			if ( xd->m_collnum != cr->m_collnum ) continue;
			// grab it
			SpiderRequest *oldsr = &xd->m_sreq;
			// get status
			char *status = xd->m_statusMsg;
			// show that
			if ( ! oldsr->printToTableSimple ( &sb , status,xd,j)) 
				return false;
			j++;
		}

		// end the table
		sb.safePrintf ( "</table>\n" );
		sb.safePrintf ( "<br>\n" );

	} // end html format




	// this is for making sure the search results are not cached
	uint32_t r1 = rand();
	uint32_t r2 = rand();
	uint64_t rand64 = (uint64_t) r1;
	rand64 <<= 32;
	rand64 |=  r2;


	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf("<br>"
			      "<table border=0 cellpadding=5>"
			      
			      // OBJECT search input box
			      "<form method=get action=/search>"
			      "<tr>"
			      "<td>"
			      "<b>Search Objects:</b>"
			      "</td><td>"
			      "<input type=text name=q size=50>"
			      // site clustering off
			      "<input type=hidden name=sc value=0>"
			      // dup removal off
			      "<input type=hidden name=dr value=0>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=rand value=%"INT64">"
			      // bypass ajax, searchbox, logo, etc.
			      "<input type=hidden name=id value=12345>"
			      // restrict search to json objects
			      "<input type=hidden name=prepend "
			      "value=\"type:json |\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</tr>"
			      "</form>"
			      
			      
			      // PAGE search input box
			      "<form method=get action=/search>"
			      "<tr>"
			      "<td>"
			      "<b>Search Pages:</b>"
			      "</td><td>"
			      "<input type=text name=q size=50>"
			      // site clustering off
			      "<input type=hidden name=sc value=0>"
			      // dup removal off
			      "<input type=hidden name=dr value=0>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=rand value=%"INT64">"
			      // bypass ajax, searchbox, logo, etc.
			      "<input type=hidden name=id value=12345>"
			      // restrict search to NON json objects
			      "<input type=hidden "
			      "name=prepend value=\"-type:json |\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</tr>"
			      "</form>"
			      
			      // add url input box
			      "<form method=get action=/crawlbot>"
			      "<tr>"
			      "<td>"
			      "<b>Add Seed Urls: </b>"
			      "</td><td>"
			      "<input type=text name=seeds size=50>"
			      "%s" // hidden tags
			      " "
			      "<input type=submit name=submit value=OK>"
			      //" &nbsp; &nbsp; <input type=checkbox "
			      //"name=spiderLinks value=1 "
			      //"checked>"
			      //" <i>crawl links on this page?</i>"
			      , cr->m_coll
			      , rand64
			      , cr->m_coll
			      , rand64
			      , hb.getBufStart() // hidden tags
			      );
	}

	if ( injectionResponse && fmt == FORMAT_HTML )
		sb.safePrintf("<br><font size=-1>%s</font>\n"
			      ,injectionResponse->getBufStart() 
			      );

	if ( fmt == FORMAT_HTML )
		sb.safePrintf(//"<input type=hidden name=c value=\"%s\">"
			      //"<input type=hidden name=crawlbotapi value=1>"
			      "</td>"
			      "</tr>"
			      //"</form>"
			      
			      
			      "<tr>"
			      "<td><b>Add Spot URLs:</b></td>"
			      
			      "<td>"
			      // this page will call 
			      // printCrawlbotPage2(uploadResponse) 2display it
			      //"<form method=post action=/crawlbot>"
			      //"<input type=file name=spots size=40>"
			      "<input type=text name=spots size=50> "
			      "<input type=submit name=submit value=OK>"
			      "%s" // hidden tags
			      //" &nbsp; &nbsp; <input type=checkbox "
			      //"name=spiderLinks value=1 "
			      //"checked>"
			      //" <i>crawl links on those pages?</i>"
			      
			      "</form>"

			      "</td>"
			      "</tr>"
			      
			      "</table>"
			      "<br>"
			      //, cr->m_coll
			      , hb.getBufStart()
			      );


	//
	// show stats
	//
	if ( fmt == FORMAT_HTML ) {

		char *seedStr = cr->m_diffbotSeeds.getBufStart();
		if ( ! seedStr ) seedStr = "";

		SafeBuf tmp;
		int32_t crawlStatus = -1;
		getSpiderStatusMsg ( cr , &tmp , &crawlStatus );
		CrawlInfo *ci = &cr->m_localCrawlInfo;
		int32_t sentAlert = (int32_t)ci->m_sentCrawlDoneAlert;
		if ( sentAlert ) sentAlert = 1;

		sb.safePrintf(

			      "<form method=get action=/crawlbot>"
			      "%s"
			      , hb.getBufStart() // hidden input token/name/..
			      );
		sb.safePrintf("<TABLE border=0>"
			      "<TR><TD valign=top>"

			      "<table border=0 cellpadding=5>"

			      //
			      "<tr>"
			      "<td><b>Crawl Name:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Type:</td>"
			      "<td>%"INT32"</td>"
			      "</tr>"

			      //"<tr>"
			      //"<td><b>Collection Alias:</td>"
			      //"<td>%s%s</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Token:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Seeds:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status:</td>"
			      "<td>%"INT32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Status Msg:</td>"
			      "<td>%s</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Start Time:</td>"
			      "<td>%"UINT32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Last Crawl Completion Time:</td>"
			      "<td>%"UINT32"</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Rounds Completed:</td>"
			      "<td>%"INT32"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Has Urls Ready to Spider:</td>"
			      "<td>%"INT32"</td>"
			      "</tr>"

			      , cr->m_diffbotCrawlName.getBufStart()
			      
			      , (int32_t)cr->m_isCustomCrawl

			      , cr->m_diffbotToken.getBufStart()

			      , seedStr

			      , crawlStatus
			      , tmp.getBufStart()

			      , cr->m_diffbotCrawlStartTime
			      // this is 0 if not over yet
			      , cr->m_diffbotCrawlEndTime

			      , cr->m_spiderRoundNum
			      , cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider

			      );

		// show crawlinfo crap
		CrawlInfo *cis = (CrawlInfo *)cr->m_crawlInfoBuf.getBufStart();
		sb.safePrintf("<tr><td><b>Ready Hosts</b></td><td>");
		for ( int32_t i = 0 ; i < g_hostdb.getNumHosts() ; i++ ) {
			CrawlInfo *ci = &cis[i];
			if ( ! ci ) continue;
			if ( ! ci->m_hasUrlsReadyToSpider ) continue;
			Host *h = g_hostdb.getHost ( i );
			if ( ! h ) continue;
			sb.safePrintf("<a href=http://%s:%i/crawlbot?c=%s>"
				      "%i</a> "
				      , iptoa(h->m_ip)
				      , (int)h->m_httpPort
				      , cr->m_coll
				      , (int)i
				      );
		}
		sb.safePrintf("</tr>\n");


		sb.safePrintf(

			      // this will  have to be in crawlinfo too!
			      //"<tr>"
			      //"<td><b>pages indexed</b>"
			      //"<td>%"INT64"</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Objects Found</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>URLs Harvested</b> (inc. dups)</td>"
			      "<td>%"INT64"</td>"
     
			      "</tr>"

			      //"<tr>"
			      //"<td><b>URLs Examined</b></td>"
			      //"<td>%"INT64"</td>"
			      //"</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Attempts</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Crawl Successes This Round</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Attempts</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Successes This Round</b></td>"
			      "<td>%"INT64"</td>"
			      "</tr>"

			      
			      , cr->m_globalCrawlInfo.m_objectsAdded -
			        cr->m_globalCrawlInfo.m_objectsDeleted
			      , cr->m_globalCrawlInfo.m_urlsHarvested
			      //, cr->m_globalCrawlInfo.m_urlsConsidered

			      , cr->m_globalCrawlInfo.m_pageDownloadAttempts
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccesses
			      , cr->m_globalCrawlInfo.m_pageDownloadSuccessesThisRound

			      , cr->m_globalCrawlInfo.m_pageProcessAttempts
			      , cr->m_globalCrawlInfo.m_pageProcessSuccesses
			      , cr->m_globalCrawlInfo.m_pageProcessSuccessesThisRound
			      );


		uint32_t now = (uint32_t)getTimeGlobalNoCore();

		sb.safePrintf("<tr>"
			      "<td><b>Download Objects:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/download/%s_data.csv>"
			      "csv</a>"

			      " &nbsp; "

			      "<a href=/crawlbot/download/%s_data.json>"
			      "json full dump</a>"

			      " &nbsp; "

			      , cr->m_coll
			      , cr->m_coll

			      );

		sb.safePrintf(
			      // newest json on top of results
			      "<a href=/search?icc=1&format=json&sc=0&dr=0&"
			      "c=%s&n=10000000&rand=%"UINT64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      ">"
			      "json full search (newest on top)</a>"


			      " &nbsp; "

			      // newest json on top of results, last 10 mins
			      "<a href=/search?icc=1&format=json&"
			      // disable site clustering
			      "sc=0&"
			      // doNOTdupcontentremoval:
			      "dr=0&"
			      "c=%s&n=10000000&rand=%"UINT64"&scores=0&id=1&"
			      "stream=1&" // stream results back as we get them
			      "q="
			      // put NEWEST on top
			      "gbsortbyint%%3Agbspiderdate+"
			      // min spider date = now - 10 mins
			      "gbminint%%3Agbspiderdate%%3A%"INT32"&"
			      //"debug=1"
			      "prepend=type%%3Ajson"
			      ">"
			      "json search (last 30 seconds)</a>"



			      "</td>"
			      "</tr>"
			      
			      // json search with gbsortby:gbspiderdate
			      , cr->m_coll
			      , rand64


			      // json search with gbmin:gbspiderdate
			      , cr->m_coll
			      , rand64
			      , now - 30 // 60 // last 1 minute

			      );


		sb.safePrintf (
			      "<tr>"
			      "<td><b>Download Products:</b> "
			      "</td><td>"
			      // make it search.csv so excel opens it
			      "<a href=/search.csv?icc=1&format=csv&sc=0&dr=0&"
			      "c=%s&n=10000000&rand=%"UINT64"&scores=0&id=1&"
			      "q=gbrevsortby%%3Aproduct.offerPrice&"
			      "prepend=type%%3Ajson"
			      //"+type%%3Aproduct%%7C"
			      ">"
			      "csv</a>"
			      " &nbsp; "
			      "<a href=/search?icc=1&format=html&sc=0&dr=0&"
			      "c=%s&n=10000000&rand=%"UINT64"&scores=0&id=1&"
			      "q=gbrevsortby%%3Aproduct.offerPrice&"
			      "prepend=type%%3Ajson"
			      ">"
			      "html</a>"

			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Download Urls:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/download/%s_urls.csv>"
			      "csv</a>"

			      " <a href=/v3/crawl/download/%s_urls.csv>"
			      "new csv format</a>"

			      " <a href=/search?q=gbsortby"
			      "int%%3AgbssSpiderTime&n=50&c=%s>"
			      "last 50 download attempts</a>"
			      
			      "</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Latest Objects:</b> "
			      "</td><td>"
			      "<a href=/search.csv?icc=1&format=csv&sc=0&dr=0&"
			      "c=%s&n=10&rand=%"UINT64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      ">"
			      "csv</a>"
			      " &nbsp; "
			      "<a href=/search?icc=1&format=html&sc=0&dr=0&"
			      "c=%s&n=10rand=%"UINT64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson"
			      ">"
			      "html</a>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Latest Products:</b> "
			      "</td><td>"
			      "<a href=/search.csv?icc=1&format=csv&sc=0&dr=0&"
			      "c=%s&n=10&rand=%"UINT64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson+type%%3Aproduct"
			      ">"
			      "csv</a>"
			      " &nbsp; "
			      "<a href=/search?icc=1&format=html&sc=0&dr=0&"
			      "c=%s&n=10&rand=%"UINT64"&scores=0&id=1&"
			      "q=gbsortby%%3Agbspiderdate&"
			      "prepend=type%%3Ajson+type%%3Aproduct"
			      ">"
			      "html</a>"

			      "</td>"
			      "</tr>"


			      "<tr>"
			      "<td><b>Download Pages:</b> "
			      "</td><td>"
			      "<a href=/crawlbot/download/%s_pages.txt>"
			      "txt</a>"
			      //
			      "</td>"
			      "</tr>"

			      "</table>"

			      "</TD>"
			      
			      // download products html
			      , cr->m_coll
			      , rand64

			      , cr->m_coll
			      , rand64

			      //, cr->m_coll
			      //, cr->m_coll
			      //, cr->m_coll

			      // urls.csv old
			      , cr->m_coll

			      // urls.csv new format v3
			      , cr->m_coll


			      // last 50 downloaded urls
			      , cr->m_coll

			      // latest objects in html
			      , cr->m_coll
			      , rand64

			      // latest objects in csv
			      , cr->m_coll
			      , rand64

			      // latest products in html
			      , cr->m_coll
			      , rand64

			      // latest products in csv
			      , cr->m_coll
			      , rand64

			      // download pages
			      , cr->m_coll
			      );


		// spacer column
		sb.safePrintf("<TD>"
			      "&nbsp;&nbsp;&nbsp;&nbsp;"
			      "&nbsp;&nbsp;&nbsp;&nbsp;"
			      "</TD>"
			      );

		// what diffbot api to use?
		/*
		char *api = cr->m_diffbotApi.getBufStart();
		char *s[10];
		for ( int32_t i = 0 ; i < 10 ; i++ ) s[i] = "";
		if ( api && strcmp(api,"all") == 0 ) s[0] = " selected";
		if ( api && strcmp(api,"article") == 0 ) s[1] = " selected";
		if ( api && strcmp(api,"product") == 0 ) s[2] = " selected";
		if ( api && strcmp(api,"image") == 0 ) s[3] = " selected";
		if ( api && strcmp(api,"frontpage") == 0 ) s[4] = " selected";
		if ( api && strcmp(api,"none") == 0 ) s[5] = " selected";
		if ( ! api || ! api[0] ) s[5] = " selected";
		*/
		sb.safePrintf( "<TD valign=top>"

			      "<table cellpadding=5 border=0>"
			       /*
			      "<tr>"
			      "<td>"
			      "Diffbot API"
			      "</td><td>"
			      "<select name=diffbotapi>"
			      "<option value=all%s>All</option>"
			      "<option value=article%s>Article</option>"
			      "<option value=product%s>Product</option>"
			      "<option value=image%s>Image</option>"
			      "<option value=frontpage%s>FrontPage</option>"
			      "<option value=none%s>None</option>"
			      "</select>"
			      "</td>"
			       "</tr>"
			      , s[0]
			      , s[1]
			      , s[2]
			      , s[3]
			      , s[4]
			      , s[5]
			       */
			      );

		//char *alias = "";
		//if ( cr->m_collectionNameAlias.length() > 0 )
		//	alias = cr->m_collectionNameAlias.getBufStart();
		//char *aliasResponse = "";
		//if ( alias && ! isAliasUnique(cr,token,alias) )
		//	aliasResponse = "<br><font size=1 color=red>"
		//		"Alias not unique</font>";

		char *urtYes = " checked";
		char *urtNo  = "";
		if ( ! cr->m_useRobotsTxt ) {
			urtYes = "";
			urtNo  = " checked";
		}

		/*
		char *rdomYes = " checked";
		char *rdomNo  = "";
		if ( ! cr->m_restrictDomain ) {
			rdomYes = "";
			rdomNo  = " checked";
		}
		*/

		char *isNewYes = "";
		char *isNewNo  = " checked";
		if ( cr->m_diffbotOnlyProcessIfNewUrl ) {
			isNewYes = " checked";
			isNewNo  = "";
		}

		char *api = cr->m_diffbotApiUrl.getBufStart();
		if ( ! api ) api = "";
		SafeBuf apiUrl;
		apiUrl.htmlEncode ( api , gbstrlen(api), true , 0 );
		apiUrl.nullTerm();

		char *px1 = cr->m_diffbotUrlCrawlPattern.getBufStart();
		if ( ! px1 ) px1 = "";
		SafeBuf ppp1;
		ppp1.htmlEncode ( px1 , gbstrlen(px1) , true , 0 );
		ppp1.nullTerm();

		char *px2 = cr->m_diffbotUrlProcessPattern.getBufStart();
		if ( ! px2 ) px2 = "";
		SafeBuf ppp2;
		ppp2.htmlEncode ( px2 , gbstrlen(px2) , true , 0 );
		ppp2.nullTerm();

		char *px3 = cr->m_diffbotPageProcessPattern.getBufStart();
		if ( ! px3 ) px3 = "";
		SafeBuf ppp3;
		ppp3.htmlEncode ( px3 , gbstrlen(px3) , true , 0 );
		ppp3.nullTerm();

		char *rx1 = cr->m_diffbotUrlCrawlRegEx.getBufStart();
		if ( ! rx1 ) rx1 = "";
		SafeBuf rrr1;
		rrr1.htmlEncode ( rx1 , gbstrlen(rx1), true , 0 );

		char *rx2 = cr->m_diffbotUrlProcessRegEx.getBufStart();
		if ( ! rx2 ) rx2 = "";
		SafeBuf rrr2;
		rrr2.htmlEncode ( rx2 , gbstrlen(rx2), true , 0 );

		char *notifEmail = cr->m_notifyEmail.getBufStart();
		char *notifUrl   = cr->m_notifyUrl.getBufStart();
		if ( ! notifEmail ) notifEmail = "";
		if ( ! notifUrl   ) notifUrl = "";

		sb.safePrintf(
			      
			      //
			      //
			      "<tr>"
			      "<td><b>Repeat Crawl:</b> "
			      "</td><td>"
			      "<input type=text name=repeat "
			      "size=10 value=\"%f\"> "
			      "<input type=submit name=submit value=OK>"
			      " days"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Diffbot API Url:</b> "
			      "</td><td>"
			      "<input type=text name=apiUrl "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Crawl Pattern:</b> "
			      "</td><td>"
			      "<input type=text name=urlCrawlPattern "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Process Pattern:</b> "
			      "</td><td>"
			      "<input type=text name=urlProcessPattern "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Page Process Pattern:</b> "
			      "</td><td>"
			      "<input type=text name=pageProcessPattern "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Crawl RegEx:</b> "
			      "</td><td>"
			      "<input type=text name=urlCrawlRegEx "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Url Process RegEx:</b> "
			      "</td><td>"
			      "<input type=text name=urlProcessRegEx "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max hopcount to seeds:</b> "
			      "</td><td>"
			      "<input type=text name=maxHops "
			      "size=9 value=%"INT32"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"
			      "<tr>"

            "<td><b>Only Process If New:</b> "
			      "</td><td>"
			      "<input type=radio name=onlyProcessIfNew "
			      "value=1%s> yes &nbsp; "
			      "<input type=radio name=onlyProcessIfNew "
			      "value=0%s> no &nbsp; "
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Crawl Delay (seconds):</b> "
			      "</td><td>"
			      "<input type=text name=crawlDelay "
			      "size=9 value=%f> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max Page Crawl Successes:</b> "
			      "</td><td>"
			      "<input type=text name=maxToCrawl "
			      "size=9 value=%"INT64"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max Page Process Successes:</b>"
			      "</td><td>"
			      "<input type=text name=maxToProcess "
			      "size=9 value=%"INT64"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Max Rounds:</b>"
			      "</td><td>"
			      "<input type=text name=maxRounds "
			      "size=9 value=%"INT32"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Notification Email:</b>"
			      "</td><td>"
			      "<input type=text name=notifyEmail "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr>"
			      "<td><b>Notification URL:</b>"
			      "</td><td>"
			      "<input type=text name=notifyWebhook "
			      "size=20 value=\"%s\"> "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>"

			      "<tr><td>"
			      "<b>Use Robots.txt when crawling?</b> "
			      "</td><td>"
			      "<input type=radio name=obeyRobots "
			      "value=1%s> yes &nbsp; "
			      "<input type=radio name=obeyRobots "
			      "value=0%s> no &nbsp; "
			      "</td>"
			      "</tr>"

			      //"<tr><td>"
			      //"<b>Restrict domain to seeds?</b> "
			      //"</td><td>"
			      //"<input type=radio name=restrictDomain "
			      //"value=1%s> yes &nbsp; "
			      //"<input type=radio name=restrictDomain "
			      //"value=0%s> no &nbsp; "
			      //"</td>"
			      //"</tr>"

			      //"<tr><td>"
			      //"Use spider proxies on AWS? "
			      //"</td><td>"
			      //"<input type=checkbox name=usefloaters checked>
			      //"</td>"
			      //"</tr>"


			      "</table>"

			      "</TD>"
			      "</TR>"
			      "</TABLE>"


			      , cr->m_collectiveRespiderFrequency

			      , apiUrl.getBufStart()
			      , ppp1.getBufStart()
			      , ppp2.getBufStart()
			      , ppp3.getBufStart()

			      , rrr1.getBufStart()
			      , rrr2.getBufStart()
            
            , cr->m_diffbotMaxHops

			      , isNewYes
			      , isNewNo
			      
			      , cr->m_collectiveCrawlDelay


			      , cr->m_maxToCrawl 
			      , cr->m_maxToProcess
			      , (int32_t)cr->m_maxCrawlRounds

			      , notifEmail
			      , notifUrl

			      , urtYes
			      , urtNo

			      //, rdomYes
			      //, rdomNo

			      );
	}


	// xml or json does not show the input boxes
	//if ( format != FORMAT_HTML ) 
	//	return g_httpServer.sendDynamicPage ( s, 
	//					      sb.getBufStart(), 
	//					      sb.length(),
	//					      -1 ); // cachetime


	//
	// print url filters. use "multimedia" to handle jpg etc.
	//
	// use "notindexable" for images/movies/css etc.
	// add a "process" column to send to diffbot...
	//
	//

	/*
	char *s1 = "Show";
	char *s2 = "none";
	if ( hr->getLongFromCookie("showtable",0) ) {
		s1 = "Hide";
		s2 = "";
	}

	if ( fmt == FORMAT_HTML )
		sb.safePrintf(
			      
			      "<a onclick="
			      "\""
			      "var e = document.getElementById('filters');"
			      "var m = document.getElementById('msg');"
			      "if ( e.style.display == 'none' ){"
			      "e.style.display = '';"
			      "m.innerHTML='Hide URL Filters Table';"
			      "document.cookie = 'showtable=1;';"
			      "}"
			      "else {"
			      "e.style.display = 'none';"
			      "m.innerHTML='Show URL Filters Table';"
			      "document.cookie = 'showtable=0;';"
			      "}"
			      "\""
			      " "
			      "style="
			      "cursor:hand;"
			      "cursor:pointer;"
			      "color:blue;>"
			      
			      "<u><b>"
			      "<div id=msg>"
			      "%s URL Filters Table"
			      "</div>"
			      "</b></u>"
			      "</a>"
			      
			      "<div id=filters style=display:%s;>"
			      "<form method=get action=/crawlbot>"
			      "<input type=hidden name=c value=\"%s\">"
			      "<input type=hidden name=showtable value=1>"
			      , s1
			      , s2
			      , cr->m_coll
			      );
	

	//
	// print url filters. HACKy...
	//
	if ( fmt == FORMAT_HTML )
		g_parms.sendPageGeneric ( socket ,
					  hr ,
					  PAGE_FILTERS ,
					  NULL ,
					  &sb ,
					  cr->m_coll,  // coll override
					  false ); // isJSON?
	//
	// end HACKy hack
	//
	if ( fmt == FORMAT_HTML )
		sb.safePrintf(
			      "</form>"
			      "</div>"
			      "<br>"
			      "<br>"
			      );
	*/


	//
	// add search box to your site
	//
	/*
	sb.safePrintf("<br>"
		      "<table>"
		      "<tr>"
		      "<td><a onclick=unhide();>"
		      "Add this search box to your site"
		      "</a>"
		      "</td>"
		      "</tr>"
		      "</table>");
	*/

	//
	// show simpler url filters table
	//
	if ( fmt == FORMAT_HTML ) {
		/*
		sb.safePrintf ( "<table>"
				"<tr><td colspan=2>"
				"<b>URL Filters</b>"
				"</td></tr>\n"
				);
		// true means its html input
		printUrlFilters ( sb , cr , fmt );
		// for adding new rule
		sb.safePrintf("<tr>"
			      "<td>Expression "
			      "<input type=text name=expression size=30 "
			      "value=\"\"> "
			      "</td><td>"
			      "Action <input type=text name=action size=50 "
			      "value=\"\">"
			      " "
			      "<input type=submit name=submit value=OK>"
			      "</td>"
			      "</tr>\n"
			      );
		
		
		//sb.safePrintf("<tr><td colspan=2><font size=-1><i>U
		sb.safePrintf("</table>\n");
		*/
		// 
		// END THE BIG FORM
		//
		sb.safePrintf("</form>");
	}

	//
	// show reset and delete crawl buttons
	//
	if ( fmt == FORMAT_HTML ) {
		sb.safePrintf(
			      "<table cellpadding=5>"
			      "<tr>"

			      "<td>"


			      // reset collection form
			      "<form method=get action=/crawlbot>"
			      "%s" // hidden tags
			      , hb.getBufStart()
			      );
		sb.safePrintf(

			      "<input type=hidden name=reset value=1>"
			      // also show it in the display, so set "c"
			      "<input type=submit name=button value=\""
			      "Reset this collection\">"
			      "</form>"
			      // end reset collection form
			      "</td>"

			      "<td>"

			      // delete collection form
			      "<form method=get action=/crawlbot>"
			      "%s"
			      //, (int32_t)cr->m_collnum
			      , hb.getBufStart()
			      );

		sb.safePrintf(

			      "<input type=hidden name=delete value=1>"
			      "<input type=submit name=button value=\""
			      "Delete this collection\">"
			      "</form>"
			      // end delete collection form
			      "</td>"


			      // restart collection form
			      "<td>"
			      "<form method=get action=/crawlbot>"
			      "%s"
			      "<input type=hidden name=restart value=1>"
			      "<input type=submit name=button value=\""
			      "Restart this collection\">"
			      "</form>"
			      "</td>"

			      // restart collection form
			      "<td>"
			      "<form method=get action=/crawlbot>"
			      "%s"
			      "<input type=hidden name=roundStart value=1>"
			      "<input type=submit name=button value=\""
			      "Restart spider round\">"
			      "</form>"
			      "</td>"


			      "</tr>"
			      "</table>"

			      //, (int32_t)cr->m_collnum
			      , hb.getBufStart()
			      , hb.getBufStart()
			      //, (int32_t)cr->m_collnum
			      );
	}


	// the ROOT JSON }
	if ( fmt == FORMAT_JSON )
		sb.safePrintf("}\n");

	char *ct = "text/html";
	if ( fmt == FORMAT_JSON ) ct = "application/json";
	if ( fmt == FORMAT_XML ) ct = "text/xml";
	if ( fmt == FORMAT_CSV ) ct = "text/csv";

	// this could be in html json or xml
	return g_httpServer.sendDynamicPage ( socket, 
					      sb.getBufStart(), 
					      sb.length(),
					      -1 , // cachetime
					      false ,
					      ct );

	/*		      
		      "<h1>API for Diffbot</h1>"
		      "<form action=/api/diffbot>"
		      "<input type=text name=url size=100>"
		      "<input type=submit name=inject value=\"Inject\">"
		      "</form>"
		      "<br>"

		      "<h1>API for Crawlbot</h1>"

      //        "<form id=\"addCrawl\" onSubmit=\"addCrawlFromForm(); return false;\">"
		      "<form action=/api/startcrawl method=get>"


	"<div class=\"control-group well\">"
        "<div id=\"apiSelection\" class=\"titleColumn\">"
	"<div class=\"row \">"

		      "Token: <input type=text name=token><br><br>"
		      "API: <input type=text name=api> <i>(article, product)</i><br><br>"

	"<div class=\"span2\"><label class=\"on-default-hide\">Page-type</label></div>"
	"<div class=\"input-append span7\">"
	"<select id=\"apiSelect\" name=\"api\" class=\"span2\" value=\"sds\">"
	"<option value=\"\" disabled=\"disabled\" selected=\"selected\">Select pages to process and extract</option>"
	"<option class=\"automatic\" value=\"article\">Article</option>"
	"<option class=\"automatic\" value=\"frontpage\">Frontpage</option>"
	"<option class=\"automatic\" value=\"image\">Image</option>"
	"<option class=\"automatic\" value=\"product\">Product</option>"
	"</select>"
	"<span id=\"formError-apiSelect\" class=\"formError\">Page-type is required</span>"
	"<span class=\"inputNote\">API calls will be made using your current token.</span>"
	"</div>"
	"</div>"
        "</div>"
        "<div id=\"apiQueryString\" class=\"titleColumn\">"
	"<div class=\"row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\">API Querystring</label></div>"
	"<div class=\"input-prepend span7\">"
	"<span class=\"add-on\">?</span><input class=\"span6 search-input\" name=\"apiQueryString\" size=\"16\" type=\"text\" placeholder=\"Enter a querystring to specify Diffbot API parameters\">"
	"</div>"
	"</div>"
        "</div>"
        "<hr>"
        "<div id=\"seedUrl\" class=\"titleColumn\">"
	"<div class=\"row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\">Seed URL</label></div>"
	"<div class=\"input-append span7\">"
	"<input class=\"span6 search-input\" name=\"seed\" size=\"16\" type=\"text\" placeholder=\"Enter a seed URL\">"
	"<span id=\"formError-seedUrl\" class=\"formError\"><br>Seed URL is required</span>"
	"</div>"
	"</div>"
        "</div>"
        "<hr>"
        "<div id=\"headerRow\" class=\"titleColumn\">"
	"<div class=\"row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\"><strong>Crawl Filters</strong></label></div>"
	"</div>"
        "</div>"
        "<div id=\"urlCrawlPattern\" class=\"titleColumn\">"
	"<div class=\"regex-edit row \">"
	"<div class=\"span2\"><label class=\"on-default-hide\">URL Regex</label></div>"
	"<div class=\"input-append span7\">"
	"<input class=\"span6\" name=\"urlCrawlPattern\" size=\"16\" type=\"text\" placeholder=\"Only crawl pages whose URLs match this regex\" value=\"\">"
	"<span class=\"inputNote\">Diffbot uses <a href=\"http://www.regular-expressions.info/refflavors.html\" target=\"_blank\">Java regex syntax</a>. Be sure to escape your characters.</span>"
	"</div>"
	"</div>"
        "</div>"
        "<div id=\"maxCrawled\" class=\"titleColumn\">"
	"<div class=\"regex-edit row \"><div class=\"span2\"><label class=\"on-default-hide\">Max Pages Crawled</label></div>         <div class=\"input-append span7\">               <input class=\"span1\" name=\"maxCrawled\" size=\"\" type=\"text\" value=\"\">            </div>          </div>        </div>        <div id=\"headerRow\" class=\"titleColumn\">          <div class=\"row \">		<div class=\"span2\"><label class=\"on-default-hide\"><strong>Processing Filters</strong></label></div>          </div>        </div>        <div id=\"classify\" class=\"titleColumn\">          <div class=\"row\">		<div class=\"span2\" id=\"smartProcessLabel\"><label class=\"on-default-hide\">Smart Processing</label></div>		<div class=\"span7\"><label class=\"checkbox\"><input id=\"smartProcessing\" type=\"checkbox\" name=\"classify\"><span id=\"smartProcessAutomatic\">Only process pages that match the selected page-type. Uses <a href=\"/our-apis/classifier\">Page Classifier API</a>.</span><span id=\"smartProcessCustom\">Smart Processing only operates with Diffbot <a href=\"/products/automatic\">Automatic APIs.</a></span></label></div>          </div>        </div>        <div id=\"urlProcessPattern\" class=\"titleColumn\">          <div class=\"regex-edit row \">		<div class=\"span2\"><label class=\"on-default-hide\">URL Regex</label></div>            <div class=\"input-append span7\">                <input class=\"span6\" name=\"urlProcessPattern\" size=\"16\" type=\"text\" placeholder=\"Only process pages whose URLs match this regex\" value=\"\">            </div>          </div>        </div>        <div id=\"pageProcessPattern\" class=\"titleColumn\">          <div class=\"regex-edit row \">		<div class=\"span2\"><label class=\"on-default-hide\">Page-Content Regex</label></div>            <div class=\"input-append span7\">                <input class=\"span6\" name=\"pageProcessPattern\" size=\"16\" type=\"text\" placeholder=\"Only process pages whose content contains a match to this regex\" value=\"\">            </div>          </div>        </div>        <div id=\"maxMatches\" class=\"titleColumn\">          <div class=\"regex-edit row \">		<div class=\"span2\"><label class=\"on-default-hide\">Max Pages Processed</label></div>            <div class=\"input-append span7\">                <input class=\"span1\" name=\"maxProcessed\" size=\"16\" type=\"text\" value=\"\">            </div>          </div>        </div>        <hr>        <div class=\"controls row\">		<div class=\"span2\">&nbsp;</div>            <div class=\"span7\" id=\"startCrawlButtons\">					   <button id=\"testButton\" class=\"btn\" type=\"button\" onclick=\"testcrawl(formToData());clicky.log('/dev/crawl#testCrawl','Test Crawl');\">Test</button>						   "

"<!--<button id=\"submitButton\" class=\"btn btn-info\" type=\"button\" onclick=\"addCrawlFromForm()\" >Start Crawl</button>-->"

"<input type=submit name=start value=\"Start Crawl\">"


"          </div>        </div>    </div>        <div id=\"hiddenTestDiv\"  style=\"display: none;\"></div>    </form>    </div><!-- end Crawler tab -->" );


*/
}

// . do not add dups into m_diffbotSeeds safebuf
// . return 0 if not in table, 1 if in table. -1 on error adding to table.
int32_t isInSeedBuf ( CollectionRec *cr , char *url, int len ) {

	HashTableX *ht = &cr->m_seedHashTable;

	// if table is empty, populate it
	if ( ht->m_numSlotsUsed <= 0 ) {
		// initialize the hash table
		if ( ! ht->set(8,0,1024,NULL,0,false,1,"seedtbl") ) 
			return -1;
		// populate it from list of seed urls
		char *p = cr->m_diffbotSeeds.getBufStart();
		for ( ; p && *p ; ) {
			// get url
			char *purl = p;
			// advance to next
			for ( ; *p && !is_wspace_a(*p) ; p++ );
			// make end then
			char *end = p;
			// skip possible white space. might be \0.
			if ( *p ) p++;
			// hash it
			int64_t h64 = hash64 ( purl , end-purl );
			if ( ! ht->addKey ( &h64 ) ) return -1;
		}
	}

	// is this url in the hash table?
	int64_t u64 = hash64 ( url, len );
	
	if ( ht->isInTable ( &u64 ) ) return 1;

	// add it to hashtable
	if ( ! ht->addKey ( &u64 ) ) return -1;

	// WAS not in table
	return 0;
}

// just use "fakeips" based on the hash of each url hostname/subdomain
// so we don't waste time doing ip lookups.
bool getSpiderRequestMetaList ( char *doc , 
				SafeBuf *listBuf ,
				bool spiderLinks ,
				CollectionRec *cr ) {

	if ( ! doc ) return true;

	// . scan the list of urls
	// . assume separated by white space \n \t or space
	char *p = doc;

	uint32_t now = (uint32_t)getTimeGlobal();

	// a big loop
	while ( true ) {
		// skip white space (\0 is not a whitespace)
		for ( ; is_wspace_a(*p) ; p++ );
		// all done?
		if ( ! *p ) break;
		// save it
		char *saved = p;
		// advance to next white space
		for ( ; ! is_wspace_a(*p) && *p ; p++ );
		// set end
		char *end = p;
		// get that url
		Url url;
		url.set ( saved , end - saved );
		// if not legit skip
		if ( url.getUrlLen() <= 0 ) continue;
		// need this
		int64_t probDocId = g_titledb.getProbableDocId(&url);
		// make it
		SpiderRequest sreq;
		sreq.reset();
		sreq.m_firstIp = url.getHostHash32(); // fakeip!
		// avoid ips of 0 or -1
		if ( sreq.m_firstIp == 0 || sreq.m_firstIp == -1 )
			sreq.m_firstIp = 1;
		sreq.m_hostHash32 = url.getHostHash32();
		sreq.m_domHash32  = url.getDomainHash32();
		sreq.m_siteHash32 = url.getHostHash32();
		//sreq.m_probDocId  = probDocId;
		sreq.m_hopCount   = 0; // we're a seed
		sreq.m_hopCountValid = true;
		sreq.m_addedTime = now;
		sreq.m_isNewOutlink = 1;
		sreq.m_isWWWSubdomain = url.isSimpleSubdomain();
		
		// treat seed urls as being on same domain and hostname
		sreq.m_sameDom = 1;
		sreq.m_sameHost = 1;
		sreq.m_sameSite = 1;

		sreq.m_fakeFirstIp = 1;
		sreq.m_isAddUrl = 1;

		// spider links?
		if ( ! spiderLinks )
			sreq.m_avoidSpiderLinks = 1;

		// save the url!
		strcpy ( sreq.m_url , url.getUrl() );
		// finally, we can set the key. isDel = false
		sreq.setKey ( sreq.m_firstIp , probDocId , false );

		int32_t oldBufSize = listBuf->getCapacity();
		int32_t need = listBuf->getLength() + 100 + sreq.getRecSize();
		int32_t newBufSize = 0;
		if ( need > oldBufSize ) newBufSize = oldBufSize + 100000;
		if ( newBufSize && ! listBuf->reserve ( newBufSize ) )
			// return false with g_errno set
			return false;

		// store rdbid first
		if ( ! listBuf->pushChar(RDB_SPIDERDB) )
			// return false with g_errno set
			return false;
		// store it
		if ( ! listBuf->safeMemcpy ( &sreq , sreq.getRecSize() ) )
			// return false with g_errno set
			return false;

		if ( ! cr ) continue;

		// do not add dups into m_diffbotSeeds safebuf
		int32_t status = isInSeedBuf ( cr , saved , end - saved );

		// error?
		if ( status == -1 ) {
			log ( "crawlbot: error adding seed to table: %s",
			      mstrerror(g_errno) );
			return true;
		}

		// already in buf
		if ( status == 1 ) continue;

		// add url into m_diffbotSeeds, \n separated list
		if ( cr->m_diffbotSeeds.length() )
			// make it space not \n so it looks better in the
			// json output i guess
			cr->m_diffbotSeeds.pushChar(' '); // \n
		cr->m_diffbotSeeds.safeMemcpy (url.getUrl(), url.getUrlLen());
		cr->m_diffbotSeeds.nullTerm();
	}
	// all done
	return true;
}

/*
bool isAliasUnique ( CollectionRec *cr , char *token , char *alias ) {
	// scan all collections
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		CollectionRec *cx = g_collectiondb.m_recs[i];
		if ( ! cx ) continue;
		// must belong to us
		if ( strcmp(cx->m_diffbotToken.getBufStart(),token) )
			continue;
		// skip if collection we are putting alias on
		if ( cx == cr ) continue;
		// does it match?
		if ( cx->m_collectionNameAlias.length() <= 0 ) continue;
		// return false if it matches! not unique
		if ( strcmp ( cx->m_collectionNameAlias.getBufStart() ,
			      alias ) == 0 )
			return false;
	}
	return true;
}
*/

// json can be provided via get or post but content type must be
// url-encoded so we can test with a simple html form page.
/*
bool setSpiderParmsFromJSONPost ( TcpSocket *socket , 
				  HttpRequest *hr ,
				  CollectionRec *cr ) {

	// get the json
	char *json = hr->getString("json");
	if ( ! json ) 
		return sendReply2 ( socket, 
				    FORMAT_JSON,
				    "No &json= provided in request.");


	Json JP;
	bool status = JP.parseJsonStringIntoJsonItems ( json );

	// wtf?
	if ( ! status ) 
		return sendReply2 ( socket, FORMAT_JSON,
				    "Error with JSON parser.");

	// error adding it?
	if ( ! cr )
		return sendReply2 ( socket,FORMAT_JSON,
				    "Failed to create new collection.");

	ji = JP.getFirstItem();

	char *seed = NULL;

	// traverse the json
	for ( ; ji ; ji = ji->m_next ) {
		// just get STRINGS or NUMS
		if ( ji->m_type != JT_STRING && ji->m_type != JT_NUMBER ) 
			continue;
		// check name
		char *name   = ji->m_name;
		char *val = ji->getValue();

		if ( strcmp(name,"seed") == 0 )
			seed = val;
		if ( strcmp(name,"email") == 0 )
			cr->m_notifyEmail.set(val);
		if ( strcmp(name,"webhook") == 0 )
			cr->m_notifyUrl.set(val);
		if ( strcmp(name,"frequency") == 0 )
			cr->m_collectiveRespiderFrequency = atof(val);
		if ( strcmp(name,"maxToCrawl") == 0 )
			cr->m_maxToCrawl = atoll(val);
		if ( strcmp(name,"maxToProcess") == 0 )
			cr->m_maxToProcess = atoll(val);
		if ( strcmp(name,"pageProcessPattern") == 0 )
			cr->m_diffbotPageProcessPattern.set(val);
		if ( strcmp(name,"obeyRobots") == 0 ) {
			if ( val[0]=='t' || val[0]=='T' || val[0]==1 )
				cr->m_useRobotsTxt = true;
			else
				cr->m_useRobotsTxt = false;
		}
		if ( strcmp(name,"onlyProcessNew") == 0 ) {
			if ( val[0]=='t' || val[0]=='T' || val[0]==1 )
				cr->m_diffbotOnlyProcessIfNew = true;
			else
				cr->m_diffbotOnlyProcessIfNew = false;
		}
		if ( strcmp(name,"pauseCrawl") == 0 ) {
			if ( val[0]=='t' || val[0]=='T' || val[0]==1 )
				cr->m_spideringEnabled = 0;
			else
				cr->m_spideringEnabled = 1;
		}
	}

	// set collective respider in case just that was passed
	for ( int32_t i =0 ; i < MAX_FILTERS ; i++ ) 
		cr->m_spiderFreqs[i] = cr->m_collectiveRespiderFrequency;

	// if url filters not specified, we are done
	if ( ! JP.getItem("urlFilters") )
		return true;

	// reset the url filters here to the default set.
	// we will append the client's filters below them below.
	resetUrlFilters ( cr );


	char *expression = NULL;
	char *action = NULL;

	// start over at top
	ji = JP.getFirstItem();

	// "urlFilters": [
	// {
	//   "value": "*",   // MDW - this matches all urls! ("default")
	//   "action": "http://www.diffbot.com/api/analyze?mode=auto"
	// }
	// {
	//   "value": "company",
	//   "action" : "http://www.diffbot.com/api/article?tags&meta"
	// }
	// {
	//   "value": "^http://www",
	//   "action": "doNotProcess"
	// }
	// { 
	//   "value": "$.html && category",
	//   "action": "doNotCrawl"
	// }
	// {
	//   "value": "!$.html && $.php",
	//   "action": "doNotCrawl"
	// }
	// ]

	// how many filters do we have so far?
	int32_t nf = cr->m_numRegExs;

	for ( ; ji ; ji = ji->m_next ) {
		// just get STRINGS only
		if ( ji->m_type != JT_STRING ) continue;
		// must be right now
		char *name = ji->m_name;
		char *value = ji->getValue();
		if ( strcmp(name,"value")==0 )
			expression = value;
		if ( strcmp(name,"action")==0 )
			action = ji->getValue();
		// need both
		if ( ! action ) continue;
		if ( ! expression ) continue;
		// they use "*" instead of "default" so put that back
		if ( expression[0] == '*' )
			expression = "default";
		// deal with it
		cr->m_regExs[1].set(expression);
		cr->m_numRegExs++;
		int32_t priority = 50;
		// default diffbot api call:
		char *api = NULL;
		if ( strcasecmp(action,"donotcrawl") == 0 )
			priority = SPIDER_PRIORITY_FILTERED;
		//if ( strcasecmp(action,"donotprocess") == 0 )
		//	api = NULL;
		// a new diffbot url?
		if ( strcasecmp(action,"http") == 0 )
			api = action;
		// add the new filter
		cr->m_regExs             [nf].set(expression);
		cr->m_spiderPriorities   [nf] = priority;
		cr->m_spiderDiffbotApiUrl[nf].set(api);
		nf++;

		// add a mirror of that filter but for manually added,
		// i.e. injected or via add url, 
		if ( priority < 0 ) continue;

		// make the priority higher!
		cr->m_regExs[nf].safePrintf("ismanualadd && %s",expression);
		cr->m_spiderPriorities   [nf] = 70; 
		cr->m_spiderDiffbotApiUrl[nf].set(api); // appends \0
		nf++;

		// NULL out again
		action = NULL;
		expression = NULL;

		if ( nf < MAX_FILTERS ) continue;
		log("crawlbot: too many url filters!");
		break;
	}

	// update the counts
	cr->m_numRegExs   = nf;
	cr->m_numRegExs2  = nf;
	cr->m_numRegExs3  = nf;
	cr->m_numRegExs10 = nf;
	cr->m_numRegExs5  = nf;
	cr->m_numRegExs6  = nf;
	cr->m_numRegExs7  = nf;
	cr->m_numRegExs11 = nf;

	// set collective respider
	for ( int32_t i =0 ; i < nf ; i++ ) 
		cr->m_spiderFreqs[i] = cr->m_collectiveRespiderFrequency;

	return true;
}
*/


/*
  THIS IS NOW AUTOMATIC from new Parms.cpp broadcast logic

bool setSpiderParmsFromHtmlRequest ( TcpSocket *socket ,
				     HttpRequest *hr , 
				     CollectionRec *cr ) {
	//  update the url filters for now since that is complicated
	//  supply "cr" directly since "c" may not be in the http
	//  request if addcoll=xxxxxx (just created a new rec)
	//int32_t page = PAGE_FILTERS;
	//WebPage *pg = g_pages.getPage ( page ) ;
	//g_parms.setFromRequest ( hr , socket , pg->m_function, cr );

	bool rebuild = false;

	//
	// set other diffbot parms for this collection
	//
	int32_t maxToCrawl = hr->getLongLong("maxToCrawl",-1LL);
	if ( maxToCrawl == -1 ) 
		maxToCrawl = hr->getLongLong("maxToDownload",-1LL);
	if ( maxToCrawl != -1 ) {
		cr->m_maxToCrawl = maxToCrawl;
		cr->m_needsSave = 1;
	}
	int32_t maxToProcess = hr->getLongLong("maxToProcess",-1LL);
	if ( maxToProcess != -1 ) {
		cr->m_maxToProcess = maxToProcess;
		cr->m_needsSave = 1;
	}
	// -1 means no max, so use -2 as default here
	int32_t maxCrawlRounds = hr->getLongLong("maxCrawlRounds",-2LL);
	if ( maxCrawlRounds == -2 )
		maxCrawlRounds = hr->getLongLong("maxRounds",-2LL);
	if ( maxCrawlRounds != -2 ) {
		cr->m_maxCrawlRounds = maxCrawlRounds;
		cr->m_needsSave = 1;
	}
	char *email = hr->getString("notifyEmail",NULL,NULL);
	if ( email ) {
		cr->m_notifyEmail.set(email);
		cr->m_needsSave = 1;
	}
	char *url = hr->getString("notifyWebHook",NULL,NULL);
	if ( ! url ) url = hr->getString("notifyWebhook",NULL,NULL);
	if ( url ) {
		// assume url is invalid, purge it
		cr->m_notifyUrl.purge();
		// normalize
		Url norm;
		norm.set ( url );
		if ( norm.getDomainLen() > 0 &&
		     norm.getHostLen() > 0 ) 
			// set the ssafebuf to it. will \0 terminate it.
			cr->m_notifyUrl.set(norm.getUrl());
		// save the collection rec
		cr->m_needsSave = 1;
	}
	int32_t pause = hr->getLong("pauseCrawl",-1);

	// /v2/bulk api support
	if ( pause == -1 ) pause = hr->getLong("pause",-1);

	if ( pause == 0 ) { cr->m_needsSave = 1; cr->m_spideringEnabled = 1; }
	if ( pause == 1 ) { cr->m_needsSave = 1; cr->m_spideringEnabled = 0; }
	int32_t obeyRobots = hr->getLong("obeyRobots",-1);
	if ( obeyRobots == -1 ) obeyRobots = hr->getLong("robots",-1);
	if ( obeyRobots != -1 ) {
		cr->m_useRobotsTxt = obeyRobots;
		cr->m_needsSave = 1;
	}
	int32_t restrictDomain = hr->getLong("restrictDomain",-1);
	if ( restrictDomain != -1 ) {
		cr->m_restrictDomain = restrictDomain;
		cr->m_needsSave = 1;
		rebuild = true;
	}

	char *api = hr->getString("apiUrl",NULL);
	if ( api ) {
		cr->m_diffbotApiUrl.set(api);
		cr->m_needsSave = 1;
	}
	char *ppp1 = hr->getString("urlCrawlPattern",NULL);
	if ( ppp1 ) {
		cr->m_diffbotUrlCrawlPattern.set(ppp1);
		cr->m_needsSave = 1;
		rebuild = true;
	}
	char *ppp2 = hr->getString("urlProcessPattern",NULL);
	if ( ppp2 ) {
		cr->m_diffbotUrlProcessPattern.set(ppp2);
		cr->m_needsSave = 1;
	}
	char *ppp3 = hr->getString("pageProcessPattern",NULL);
	if ( ppp3 ) {
		cr->m_diffbotPageProcessPattern.set(ppp3);
		cr->m_needsSave = 1;
	}
	// reg ex support
	char *rx1 = hr->getString("urlCrawlRegEx",NULL);
	// clear what we had
	if ( rx1 && cr->m_hasucr ) {
		regfree ( &cr->m_ucr );
		cr->m_hasucr = false;
		cr->m_diffbotUrlCrawlRegEx.purge();
		cr->m_needsSave = 1;
		rebuild = true;
	}
	// add a new one if not blank
	if ( rx1 && rx1[0] ) {
		cr->m_diffbotUrlCrawlRegEx.set(rx1);
		cr->m_needsSave = 1;
		// this will store the compiled regular expression into ucr
		if ( regcomp ( &cr->m_ucr ,
			       // the regular expression to compile
			       rx1 ,
			       // some flags
			       REG_EXTENDED|REG_ICASE|
			       REG_NEWLINE|REG_NOSUB) ) {
			regfree ( &cr->m_ucr);
			// should never fail!
			return log("xmldoc: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx1,mstrerror(errno));
		}
		cr->m_hasucr = true;
	}


	char *rx2 = hr->getString("urlProcessRegEx",NULL);
	// clear what we had
	if ( rx2 && cr->m_hasupr ) {
		regfree ( &cr->m_upr );
		cr->m_hasupr = false;
		cr->m_diffbotUrlProcessRegEx.purge();
		cr->m_needsSave = 1;
	}
	// add a new one if not blank
	if ( rx2 && rx2[0] ) {
		cr->m_diffbotUrlProcessRegEx.set(rx2);
		cr->m_needsSave = 1;
		// this will store the compiled regular expression into upr
		if ( regcomp ( &cr->m_upr ,
			       // the regular expression to compile
			       rx2 ,
			       // some flags
			       REG_EXTENDED|REG_ICASE|
			       REG_NEWLINE|REG_NOSUB) ) {
			regfree ( &cr->m_upr);
			// error!
			return log("xmldoc: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx2,mstrerror(errno));
		}
		cr->m_hasupr = true;
	}


	float respider = hr->getFloat("repeatJob",-1.0);
	if ( respider == -1.0 ) respider = hr->getFloat("repeat",-1.0);
	if ( respider == -1.0 ) respider = hr->getFloat("repeatCrawl",-1.0);
	if ( respider >= 0.0 ) {
		// if not 0, then change this by the delta
		if ( cr->m_spiderRoundStartTime ) {
			// convert from days into seconds
			float rfOld = cr->m_collectiveRespiderFrequency;
			float rfNew = respider;
			// 86400 seconds in a day
			int32_t secondsOld = (int32_t)(rfOld * 86400);
			int32_t secondsNew = (int32_t)(rfNew * 86400);
			// remove old one.
			cr->m_spiderRoundStartTime -= secondsOld;
			// add in new one
			cr->m_spiderRoundStartTime += secondsNew;
		}
		// if 0 that means NO recrawling
		if ( respider == 0.0 ) {
			cr->m_spiderRoundStartTime = 0;//getTimeGlobal();
		}
		cr->m_collectiveRespiderFrequency = respider;
		cr->m_needsSave = 1;
	}

	float delay = hr->getFloat("crawlDelay",-1.0);
	//int32_t crawlWait = hr->getLong("wait",-1);
	if ( delay >= 0.0 ) {
		rebuild = true;
		cr->m_collectiveCrawlDelay = delay;
	}
	
	int32_t onlyProcessNew = hr->getLong("onlyProcessIfNew",-1);
	if ( onlyProcessNew != -1 ) {
		cr->m_diffbotOnlyProcessIfNew = onlyProcessNew;
		cr->m_needsSave = 1;
	}

	// set collective respider
	//for ( int32_t i =0 ; i < cr->m_numRegExs ; i++ ) {
	//	if ( cr->m_collectiveRespiderFrequency == 0.0 )
	//		cr->m_spiderFreqs[i] = 0.000;
	//	else
	//		cr->m_spiderFreqs[i] = 0.001;
	//	//cr->m_collectiveRespiderFrequency;
	//}


	char *path = hr->getPath();
	bool isBulkApi = false;
	if ( path && strncmp(path,"/v2/bulk",8)==0 ) isBulkApi = true;


	// were any url filteres specified? if not, don't reset them
	//if ( ! hr->hasField("action") )
	//	return true;

	// reset the url filters here to the default set.
	// we will append the client's filters below them below.
	resetUrlFilters ( cr );

	// if it was not recrawling and we made it start we have
	// to repopulate waiting tree because most entries will
	// need to be re-added!
	// really, anytime we change url filters we have to repopulate
	// the waiting tree
	SpiderColl *sc = cr->m_spiderColl;
	if ( sc && rebuild ) {
		// this is causing a bulk job not to complete because
		// jenkins keeps checking it every 10 seconds
		sc->m_waitingTreeNeedsRebuild = true;
	}

	return true;

	// "urlFilters": [
	// {
	//   "value": "*",   // MDW - this matches all urls! ("default")
	//   "action": "http://www.diffbot.com/api/analyze?mode=auto"
	// }
	// {
	//   "value": "company",
	//   "action" : "http://www.diffbot.com/api/article?tags&meta"
	// }
	// {
	//   "value": "^http://www",
	//   "action": "doNotProcess"
	// }
	// { 
	//   "value": "$.html && category",
	//   "action": "doNotCrawl"
	// }
	// {
	//   "value": "!$.html && $.php",
	//   "action": "doNotCrawl"
	// }
	// ]

	char *expression = NULL;
	char *action = NULL;

	// how many filters do we have so far?
	int32_t nf = cr->m_numRegExs;

	// delete the 3rd default filter cuz we should re-add it below
	// to the bottom of the list.
	if ( nf >= 3 ) nf--;

	bool addedDefault = false;

	// loop over the cgi parms
	for ( int32_t i = 0 ; i < hr->getNumFields() ; i++ ) {
		// get cgi parm name
		char *field = hr->getField    ( i );
		//int32_t  flen  = hr->getFieldLen ( i );
		if ( strcmp(field,"expression") == 0 )
			expression = hr->getValue(i);
		if ( strcmp(field,"action") == 0 ) 
			action = hr->getValue(i);
		// need both
		if ( ! action ) continue;
		// no! the /v2/bulk api just has a single action
		if ( isBulkApi ) expression = "*";
		// action before expresion???? set action to NULL then?
		if ( ! expression ) continue;
		//else continue;// { action = NULL; continue; }
		// skip whitespace
		while ( is_wspace_a(*expression) ) expression++;
		while ( is_wspace_a(*action) ) action++;
		// skip if expression is empty
		if ( ! expression[0] ) { 
			action = NULL; expression = NULL; continue; }
		// they use "*" instead of "default" so put that back
		if ( expression[0] == '*' ) {
			expression = "default";
			addedDefault = true;
		}
		// deal with it
		int32_t priority = 50;
		// default diffbot api call:
		//char *api = NULL;
		if ( strcasecmp(action,"donotcrawl") == 0 ) 
			priority = SPIDER_PRIORITY_FILTERED;
		//if ( strcasecmp(action,"donotprocess") == 0 )
		//	api = NULL;
		// a new diffbot url?
		//if ( strncasecmp(action,"http",4) == 0 )
		//api = action;

		// add a mirror of that filter but for manually added,
		// i.e. injected or via add url, 
		if ( priority >= 0 ) {
			// purge because might have been the last "default"
			// filter that we did nf-- above on.
			cr->m_regExs [nf].purge();
			// make the priority higher!
			cr->m_regExs [nf].safePrintf("ismanualadd && %s",
						     expression);
			cr->m_spiderPriorities   [nf] = 70; 
			cr->m_spiderDiffbotApiUrl[nf].set(action); // appends\0
			cr->m_spiderFreqs[nf]=
				cr->m_collectiveRespiderFrequency;
			nf++;
		}

		// add the new filter
		cr->m_regExs             [nf].set(expression);
		cr->m_spiderPriorities   [nf] = priority;
		cr->m_spiderDiffbotApiUrl[nf].set(action);
		cr->m_spiderFreqs [nf] = cr->m_collectiveRespiderFrequency;
		nf++;

		// NULL out again
		action = NULL;
		expression = NULL;

		if ( nf < MAX_FILTERS ) continue;
		log("crawlbot: too many url filters!");
		break;
	}

	// if no '*' line was provided, add it here
	if ( ! addedDefault ) {
		cr->m_regExs [nf].set("default");
		cr->m_spiderPriorities   [nf] = 50; 
		cr->m_spiderDiffbotApiUrl[nf].set(NULL);
		cr->m_spiderFreqs[nf] = cr->m_collectiveRespiderFrequency;
		nf++;
	}

	// update the counts
	cr->m_numRegExs   = nf;
	cr->m_numRegExs2  = nf;
	cr->m_numRegExs3  = nf;
	cr->m_numRegExs10 = nf;
	cr->m_numRegExs5  = nf;
	cr->m_numRegExs6  = nf;
	cr->m_numRegExs7  = nf;
	cr->m_numRegExs11 = nf;

	// set collective respider
	//for ( int32_t i =0 ; i < nf ; i++ ) 
	//	cr->m_spiderFreqs[i] = cr->m_collectiveRespiderFrequency;

	return true;
}
*/


///////////
//
// SUPPORT for getting the last 100 spidered urls
//
// . sends request to each node
// . each node returns top 100 after scanning spiderdb (cache for speed)
// . master node gets top 100 of the top 100s
// . sends pretty html or json back to socket
// . then user can see why their crawl isn't working
// . also since we are scanning spiderdb indicate how many urls are
//   ignored because they match "ismedia" or "!isonsamedomain" etc. so
//   show each url filter expression then show how many urls matched that.
//   when doing this make the spiderReply null, b/c the purpose is to see
//   what urls 
// . BUT url may never be attempted because it matches "ismedia" so that kind
//   of thing might have to be indicated on the spiderdb dump above, not here.
//
//////////

//bool sendPageLast100Urls ( TcpSocket *socket , HttpRequest *hr ) {
