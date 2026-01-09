#include "Linkdb.h"
#include "Threads.h"
#include "Titledb.h"
#include "linkspam.h"
#include "sort.h"
#include "XmlDoc.h" // score32to8()
#include "Rebalance.h"
#include "Process.h"

Linkdb g_linkdb;
Linkdb g_linkdb2;

void Linkdb::reset() {
	m_rdb.reset();
}

bool Linkdb::init ( ) {

	key224_t  k;
	// sanity tests
	uint32_t    linkeeSiteHash32 = (uint32_t)rand();
	uint32_t    linkerSiteHash32 = (uint32_t)rand();
	uint64_t linkeeUrlHash64  = (uint64_t)rand() << 32LL | rand();
	// mask it to 32+15 bits
	linkeeUrlHash64 &= 0x00007fffffffffffLL;
	unsigned char linkerSiteRank = 13;
	unsigned char      hopCount         = 7;
	int32_t      ip               = rand();
	int32_t      ipdom3 = ipdom(ip);
	int64_t docId = ((uint64_t)rand() << 32 | rand()) & DOCID_MASK;
	int32_t discoveryDate = 1339784732;
	int32_t lostDate      = discoveryDate + 86400*23;
	char linkSpam = 1;
	k = makeKey_uk ( linkeeSiteHash32 ,
			 linkeeUrlHash64  ,
			 linkSpam         , // islinkspam?
			 linkerSiteRank   ,
			 hopCount         ,
			 ip               ,
			 docId            ,
			 discoveryDate    ,
			 lostDate         ,
			 false            , // newaddtooldpage?
			 linkerSiteHash32 ,
			 false            ); // is del?

	// jan 1 2008
	uint32_t epoch = LINKDBEPOCH;
	int32_t dd2 = (discoveryDate - epoch) / 86400;
	if ( discoveryDate == 0 ) dd2 = 0;
	dd2 = dd2 * 86400  + epoch;
	int32_t ld2 = (lostDate - epoch) / 86400;
	if ( lostDate == 0 ) ld2 = 0;
	ld2 = ld2 * 86400  + epoch;

	// try this
	setLostDate_uk(&k,ld2 );

	// now test it
	gbassert_false(getLinkeeSiteHash32_uk(&k)!=linkeeSiteHash32);
	gbassert_false(getLinkeeUrlHash64_uk(&k)!=linkeeUrlHash64);
	gbassert_false( isLinkSpam_uk    ( &k ) != linkSpam       );
	gbassert_false(getLinkerSiteHash32_uk(&k)!=linkerSiteHash32);
	gbassert_false( getLinkerSiteRank_uk(&k) != linkerSiteRank);
	gbassert_false( getLinkerIp24_uk ( &k ) != ipdom3         );
	gbassert_false( getLinkerIp_uk ( &k ) != ip         );
	gbassert_false( getLinkerDocId_uk( &k ) != docId          );
	gbassert_false( getDiscoveryDate_uk(&k) != dd2  );
	gbassert_false( getLostDate_uk(&k) != ld2  );

	// more tests
	setDiscoveryDate_uk (&k,discoveryDate);
	setLostDate_uk (&k,lostDate);
	gbassert_false( getDiscoveryDate_uk(&k) != dd2  );
	gbassert_false( getLostDate_uk(&k) != ld2  );

	int32_t ip3 = 0xabcdef12;
	setIp32_uk ( &k , ip3 );
	int32_t ip4 = getLinkerIp_uk ( &k );
	gbassert_false( ip3 != ip4 );

	int64_t maxTreeMem = 40000000; // 40MB
	int32_t maxTreeNodes = maxTreeMem /(sizeof(key224_t)+16);
	// TODO: would be nice to just do page caching on the satellite files;
	//       look into "minimizeDiskSeeks" at some point...
	// init the rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "linkdb" ,
			    true     , // dedup
			    0        , // fixeddatasize is 0 since no data
			    // keep it high since we are mostly ssds now and
			    // the reads are small...
			    -1,//g_conf.m_linkdbMinFilesToMerge ,
			    // fix this to 15 and rely on the page cache of
			    // just the satellite files and the daily merge to
			    // keep things fast.
			    //15       , 
			    maxTreeMem ,
			    maxTreeNodes ,
			    true     , //isTreeBalanced
			    0        , // cache mem 
			    0        , // cache nodes
			    false, // true     , // use half keys
			    false    , // load cache from disk
			    NULL,
			    false    , // false
			    false    , // preload page cache
			    sizeof(key224_t) ,
			    true             ); // bias page cache? (true!)
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Linkdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	int32_t nodeSize = ( sizeof(key224_t) + 12 + 4 ) + sizeof(collnum_t);
	int32_t maxTreeNodes  = treeMem / nodeSize;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir     ,
			    "linkdbRebuild" ,
			    true          , // dedup
			    0             , // no data now! just docid/s/c
			    50            , // m_clusterdbMinFilesToMerge,
			    treeMem       ,
			    maxTreeNodes  , 
			    true          , // balance tree?
			    0             , // maxCacheMem   , 
			    0             , // maxCacheNodes ,
			    false, // true          , // half keys?
			    false         ,
			    NULL          ,
			    false         , // is titledb
			    false         , // preload disk page cache
			    sizeof(key224_t), // key size
			    true          );// bias disk page cache
}

bool Linkdb::verify ( char *coll ) {
	log ( LOG_DEBUG, "db: Verifying Linkdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key224_t startKey;
	key224_t endKey;
	startKey.setMin();
	endKey.setMax();
	int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_LINKDB   ,
			      cr->m_collnum      ,
			      &list         ,
			      (char*)&startKey      ,
			      (char*)&endKey        ,
			      minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key224_t k;
		list.getCurrentKey((char*)&k);
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;
		uint32_t shardNum = getShardNum( RDB_LINKDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %" INT32 " records in Linkdb , "
		     "only %" INT32 " belong to our group.",count,got);

		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
				    "right "
				    "data in the right directory? "
				    "Exiting.");
		log ( "db: Exiting due to inconsistency.");
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_DEBUG, "db: Linkdb passed verification successfully for "
	      "%" INT32 " recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// make a "url" key
key224_t Linkdb::makeKey_uk ( uint32_t  linkeeSiteHash32       ,
			      uint64_t  linkeeUrlHash64        ,
			      bool      isLinkSpam       ,
			      unsigned char      linkerSiteRank   ,
			      unsigned char      linkerHopCount         ,
			      uint32_t  linkerIp               ,
			      int64_t linkerDocId ,
			      uint32_t      discoveryDate ,
			      uint32_t      lostDate ,
			      bool      newAddToOldPage ,
			      uint32_t linkerSiteHash32 ,
			      bool      isDelete         ) {

	// mask it
	linkeeUrlHash64 &= LDB_MAXURLHASH;

	key224_t k;

	k.n3 = linkeeSiteHash32;
	k.n3 <<= 32;
	k.n3 |= (linkeeUrlHash64>>15) & 0xffffffff;

	// finish the url hash
	k.n2 = linkeeUrlHash64 & 0x7fff;

	k.n2 <<= 1;
	if ( isLinkSpam ) k.n2 |= 0x01;

	// make it 8-bites for now even though only needs 4
	k.n2 <<= 8;
	k.n2 |= (unsigned char)~linkerSiteRank;

	k.n2 <<= 8;
	// this is now part of the linkerip, steve wants the full ip
	k.n2 |= (linkerIp >> 24);

	k.n2 <<= 24;
	k.n2 |= (linkerIp & 0x00ffffff);

	k.n2 <<= 8;
	k.n2 |= (((uint64_t)linkerDocId) >> 30);

	k.n1 = (((uint64_t)linkerDocId) & 0x3fffffffLL);

	// two reserved bits
	k.n1 <<= 2;

	// sanity checks
	if ( lostDate && lostDate < LINKDBEPOCH){
        lostDate = LINKDBEPOCH;
    }

	// . convert discovery date from utc into days since jan 2008 epoch
	// . the number is for jan 2012, so subtract 4 years to do 2008
	uint32_t epoch = LINKDBEPOCH;
	if ( discoveryDate && discoveryDate < epoch ) {
        discoveryDate = epoch;
    }
	uint32_t nd = (discoveryDate - epoch) / 86400;
	if ( discoveryDate == 0 ) nd = 0;
	// makeEndKey_uk() maxes this out!
	if ( nd > 0x3fff ) nd = 0x3fff;

	k.n1 <<= 14;
	k.n1 |= nd;

	// one reservied bit
	k.n1 <<= 1;

	k.n1 <<= 1;
	if ( newAddToOldPage ) k.n1 |= 0x01;

	// the "lost" date. 0 if not yet lost.
	uint32_t od = (lostDate - LINKDBEPOCH) / 86400;
	if ( lostDate == 0 ) od = 0;
	// makeEndKey_uk() maxes this out!
	if ( od > 0x3fff ) od = 0x3fff;
	k.n1 <<= 14;
	k.n1 |= od;

	// 2 bits of linker site hash
	k.n1 <<= 2;
	k.n1 |= linkerSiteHash32 >> 30;

	// rest of linker site hash
	k.n0 = linkerSiteHash32;
	// halfbit - unused now!
	k.n0 <<= 1;
	// delbit
	k.n0 <<= 1;
	if ( ! isDelete ) k.n0 |= 0x01;
 
	return k;
}

/////////
//
// MSG25 :: getLinkInfo()
//
/////////

#include "Collectiondb.h"

// 1MB read size for now
#define READSIZE 1000000

#define MAX_INTERNAL_INLINKS 10 

static void gotListWrapper           ( void *state ,RdbList *list,Msg5 *msg5);
static bool gotLinkTextWrapper       ( void *state );

Msg25::Msg25() {
	m_numRequests = 0;
	m_linkSpamOut = 0;
	m_numReplyPtrs = 0;
	m_ownReplies = true;
}

Msg25::~Msg25 ( ) {
	reset();
}

void Msg25::reset() {
	if ( ! m_ownReplies ) m_numReplyPtrs = 0;
	for ( int32_t i = 0 ; i < m_numReplyPtrs ; i++ )
		mfree ( m_replyPtrs[i], m_replySizes[i], "msg25r");
	// reset array count to 0
	m_numReplyPtrs = 0;
	m_table.reset();
	m_ipTable.reset();
	m_fullIpTable.reset();
	m_firstIpTable.reset();
	m_docIdTable.reset();
}

#define MODE_PAGELINKINFO 1
#define MODE_SITELINKINFO 2

// . we got a reply back from the msg25 request
// . reply should just be a LinkInfo class
// . set XmlDoc::m_linkInfoBuf safebuf to that reply
// . we store tr to that safebuf in Msg25Request::m_linkInfoBuf
void gotMulticastReplyWrapper25 ( void *state , void *state2 ) {

	Msg25Request *req = (Msg25Request *)state;

	// call callback now if error is set
	if ( g_errno ) {
		req->m_callback ( req->m_state );
		return;
	}

	Multicast *mcast = req->m_mcast;

	int32_t  replySize;
	int32_t  replyMaxSize;
	bool  freeit;
	char *reply = mcast->getBestReply (&replySize,&replyMaxSize,&freeit);

	// . store reply in caller's linkInfoBuf i guess
	// . mcast should free the reply
	req->m_linkInfoBuf->safeMemcpy ( reply , replySize );

	// i guess we gotta free this
	mfree ( reply , replySize , "rep25" );

	req->m_callback ( req->m_state );
}


// . returns false if would block, true otherwise
// . sets g_errno and returns true on launch error
// . calls req->m_callback when ready if it would block
bool getLinkInfo ( SafeBuf   *reqBuf              ,
		   Multicast *mcast               ,
		   char      *site                ,
		   char      *url                 ,
		   bool       isSiteLinkInfo      ,
		   int32_t       ip                  ,
		   int64_t  docId               ,
		   collnum_t  collnum             ,
		   char      *qbuf,
		   int32_t       qbufSize,
		   void      *state               ,
		   void (* callback)(void *state) ,
		   bool       isInjecting         ,
		   SafeBuf   *pbuf                ,
		   bool       printInXml          ,
		   int32_t       siteNumInlinks      ,
		   LinkInfo  *oldLinkInfo         ,
		   int32_t       niceness            ,
		   bool       doLinkSpamCheck     ,
		   bool       oneVotePerIpDom     ,
		   bool       canBeCancelled      ,
		   int32_t       lastUpdateTime      ,
		   bool       onlyNeedGoodInlinks ,
		   bool       getLinkerTitles     ,
		   int32_t       ourHostHash32       ,
		   int32_t       ourDomHash32        ,
		   SafeBuf   *linkInfoBuf         ) {

	int32_t siteLen = gbstrlen(site);
	int32_t urlLen  = gbstrlen(url);

	int32_t oldLinkSize = 0;
	if ( oldLinkInfo )
		oldLinkSize = oldLinkInfo->getSize();

	int32_t need = sizeof(Msg25Request) + siteLen+1 + urlLen+1 + oldLinkSize;

	// keep it in a safebuf so caller can just add "SafeBuf m_msg25Req;"
	// to his .h file and not have to worry about freeing it.
	reqBuf->purge();

	// clear = true. put 0 bytes in there
	if ( ! reqBuf->reserve ( need ,"m25req", true ) ) return true;

	Msg25Request *req = (Msg25Request *)reqBuf->getBufStart();

	req->m_linkInfoBuf = linkInfoBuf;

	req->m_mcast = mcast;

	req->ptr_site = site;
	req->size_site = siteLen + 1;

	req->ptr_url  = url;
	req->size_url  = urlLen  + 1;

	req->ptr_oldLinkInfo = (char *)oldLinkInfo;
	if ( oldLinkInfo ) req->size_oldLinkInfo = oldLinkInfo->getSize();
	else               req->size_oldLinkInfo = 0;

	if ( isSiteLinkInfo ) req->m_mode = MODE_SITELINKINFO;
	else                  req->m_mode = MODE_PAGELINKINFO;
	
	req->m_ip = ip;
	req->m_docId = docId;
	req->m_collnum = collnum;
	req->m_state = state;
	req->m_callback = callback;
	req->m_isInjecting = isInjecting;
	req->m_printInXml = printInXml;
	req->m_siteNumInlinks = siteNumInlinks;
	req->m_niceness = niceness;
	req->m_doLinkSpamCheck = doLinkSpamCheck;
	req->m_oneVotePerIpDom = oneVotePerIpDom;
	req->m_canBeCancelled = canBeCancelled;
	req->m_lastUpdateTime = lastUpdateTime;
	req->m_onlyNeedGoodInlinks = onlyNeedGoodInlinks;
	req->m_getLinkerTitles = getLinkerTitles;
	req->m_ourHostHash32 = ourHostHash32;
	req->m_ourDomHash32 = ourDomHash32;

	Url u;
	u.set ( req->ptr_url );

	req->m_linkHash64 = (uint64_t)u.getUrlHash64();


	req->m_siteHash32 = 0LL;
	req->m_siteHash64 = 0LL;
	if ( req->ptr_site ) {
		// hash collection # in with it
		int64_t h64 = hash64n ( req->ptr_site );
		h64 = hash64 ((char *)&req->m_collnum,sizeof(collnum_t),h64);
		req->m_siteHash64 = h64;
		req->m_siteHash32 = hash32n ( req->ptr_site );
	}

	// send to host for local linkdb lookup
	key224_t startKey ;
	// access different parts of linkdb depending on the "mode"
	if ( req->m_mode == MODE_SITELINKINFO )
		startKey = g_linkdb.makeStartKey_uk ( req->m_siteHash32 );
	else
		startKey = g_linkdb.makeStartKey_uk (req->m_siteHash32,
						     req->m_linkHash64 );
	// what group has this linkdb list?
	uint32_t shardNum = getShardNum ( RDB_LINKDB, &startKey );
	// use a biased lookup
	int32_t numTwins = g_hostdb.getNumHostsPerShard();
	int64_t sectionWidth = (0xffffffff/(int64_t)numTwins) + 1;
	// these are 192 bit keys, top 32 bits are a hash of the url
	uint32_t x = req->m_siteHash32;//(startKey.n1 >> 32);
	int32_t hostNum = x / sectionWidth;
	int32_t numHosts = g_hostdb.getNumHostsPerShard();
	Host *hosts = g_hostdb.getShard ( shardNum); // Group ( groupId );
	gbassert_false( hostNum >= numHosts );
	int32_t hostId = hosts [ hostNum ].m_hostId ;
	if( !hosts [ hostNum ].m_spiderEnabled) {
		hostId = g_hostdb.getHostIdWithSpideringEnabled ( shardNum );
	}


	// . serialize the string buffers
	// . use Msg25Request::m_buf[MAX_NEEDED]
	// . turns the ptr_* members into offsets into req->m_buf[]
	req->serialize();

	// this should always block
	if ( ! mcast->send ( 
			    (char *)req ,
			    req->getStoredSize() ,
			    0x25 ,
			    false        , // does multicast own request?
			    shardNum ,
			    false        , // send to whole group?
			    0            , // key is passed on startKey
			    req          , // state data
			    NULL         , // state data
			    gotMulticastReplyWrapper25 ,
			    // if this is too low we core in XmlDoc.cpp
			    // after getNewSpiderReply() returns a -1 because
			    // it blocks for some reason.
			    9999998     , // timeout in seconds (was 30)
			    req->m_niceness ,
			    false, // realtime     ,
			    hostId )) {// firstHostId  ,
		log("linkdb: Failed to send multicast for %s err=%s",
		    u.getUrl(),mstrerror(g_errno));
		return true;
	}

	// wait for req->m_callback(req->m_state) to be called
	return false;
}

HashTableX g_lineTable;

static void sendReplyWrapper ( void *state ) {

	int32_t saved = g_errno;

	Msg25 *m25 = (Msg25 *)state;
	// the original request
	Msg25Request *mr = m25->m_req25;
	// get udp slot for sending back reply
	UdpSlot *slot2 = mr->m_udpSlot;
	// int16_tcut
	SafeBuf *info = m25->m_linkInfoBuf;
	// steal this buffer
	char *reply1 = info->getBufStart();
	int32_t  replySize = info->length();
	// sanity. no if collrec not found its 0!
	if ( ! saved && replySize <= 0 ) { 
		saved = g_errno = EBADENGINEER;
		log("linkdb: sending back empty link text reply. did "
		    "coll get deleted?");
	}
	// get original request
	Msg25Request *req = (Msg25Request *)slot2->m_readBuf;
	// sanity
	gbassert_false( req->m_udpSlot != slot2 );
	// if in table, nuke it
	// but only if it was in SITE mode, not PAGE. we've lost our
	// table entry like this before.
	// TODO: if this still doesn't work then ensure the stored 'req'
	// is the same!
	if ( req->m_mode == MODE_SITELINKINFO ) {
		g_lineTable.removeKey ( &req->m_siteHash64 );
		if ( g_conf.m_logDebugLinkInfo )
			log("linkdb: removing sitehash64=%" INT64 "",
			    req->m_siteHash64);
	}

 nextLink:

	UdpSlot *udpSlot = req->m_udpSlot;

	// update for next udpSlot
	req = req->m_next;

	// just dup the reply for each one
	char *reply2 = (char *)mdup(reply1,replySize,"m25repd");

	// error?
	if ( saved || ! reply2 ) {
		int32_t err = saved;
		if ( ! err ) err = g_errno;
		gbassert(err);
		g_udpServer.sendErrorReply(udpSlot,err);
	}
	else {
		// send it back to requester
		g_udpServer.sendReply_ass ( reply2 ,
					    replySize ,
					    reply2 ,
					    replySize,
					    udpSlot );
	}

	// if we had a link
	if ( req ) goto nextLink;

	// the destructor
	mdelete ( m25 ,sizeof(Msg25),"msg25");
	delete ( m25 );
}


void  handleRequest25 ( UdpSlot *slot , int32_t netnice ) {

	Msg25Request *req = (Msg25Request *)slot->m_readBuf;

	req->deserialize();

	// make sure this always NULL for our linked list logic
	req->m_next = NULL;

	// udp socket for sending back the final linkInfo in m_linkInfoBuf
	// used by sendReply()
	req->m_udpSlot = slot;

	if ( g_conf.m_logDebugLinkInfo && req->m_mode == MODE_SITELINKINFO ) {
		log("linkdb: got msg25 request sitehash64=%" INT64 " "
		    "site=%s "
		    ,req->m_siteHash64
		    ,req->ptr_site
		    );
	}

	// set up the hashtable if our first time
	if ( ! g_lineTable.isInitialized() )
		g_lineTable.set ( 8,sizeof(Msg25Request *),256,
				  NULL,0,false,MAX_NICENESS,"lht25");

	// . if already working on this same request, wait for it, don't
	//   overload server with duplicate requests
	// . hashkey is combo of collection, url, and m_mode
	// . TODO: ensure does not send duplicate "page" link info requests
	//   just "site" link info requests
	int32_t slotNum = -1;
	bool isSiteLinkInfo = false;
	if ( req->m_mode == MODE_SITELINKINFO ) {
		slotNum = g_lineTable.getSlot ( &req->m_siteHash64 );
		isSiteLinkInfo = true;
	}

	if ( slotNum >= 0 ) {
		Msg25Request *head ;
		head = *(Msg25Request **)g_lineTable.getValueFromSlot(slotNum);
		if ( head->m_next ) 
			req->m_next = head->m_next;
		head->m_next = req;
		req->m_waitingInLine = 1;
		// note it for debugging
		log("build: msg25 request waiting in line for %s "
		    "udpslot=0x%" PTRFMT "",
		    req->ptr_url,(PTRTYPE)slot);
		// we will send a reply back for this guy when done
		// getting the reply for the head msg25request
		return;
	}

	req->m_waitingInLine = 0;

	// make a new Msg25
	Msg25 *m25;
	try { m25 = new ( Msg25 ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("build: msg25: new(%" INT32 "): %s", 
		    (int32_t)sizeof(Msg25),mstrerror(g_errno));
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	mnew ( m25 , sizeof(Msg25) , "Msg25" );

	if ( isSiteLinkInfo ) {
		// add the initial entry
		g_lineTable.addKey ( &req->m_siteHash64 , &req );
	}

	// point to a real safebuf here for populating with data
	m25->m_linkInfoBuf = &m25->m_realBuf;

	// set some new stuff. should probably be set in getLinkInfo2()
	// but we are trying to leave that as unaltered as possible to
	// try to reduce debugging.
	m25->m_req25 = req;

	// this should call our callback when done
	if ( ! m25->getLinkInfo2 ( req->ptr_site ,
				   req->ptr_url ,
				   isSiteLinkInfo      ,
				   req->m_ip ,
				   req->m_docId ,
				   req->m_collnum , // coll
				   NULL, // qbuf
				   0 , // qbufSize
				   m25 , // state
				   sendReplyWrapper , // CALLBACK!
				   req->m_isInjecting         ,
				   req->m_printDebugMsgs ,
				   req->m_printInXml ,
				   req->m_siteNumInlinks      ,
				   (LinkInfo *)req->ptr_oldLinkInfo ,
				   req->m_niceness            ,
				   req->m_doLinkSpamCheck     ,
				   req->m_oneVotePerIpDom     ,
				   req->m_canBeCancelled      ,
				   req->m_lastUpdateTime      ,
				   req->m_onlyNeedGoodInlinks  ,
				   req->m_getLinkerTitles ,
				   req->m_ourHostHash32 ,
				   req->m_ourDomHash32 ,
				   m25->m_linkInfoBuf ) ) // SafeBuf 4 output
		return;

	gbassert_false(m25->m_linkInfoBuf->getLength()<=0&&!g_errno);

	gbassert_false( g_errno == ETRYAGAIN );

	// wait for msg5 to be done reading list. this happens somehow,
	// i'm not 100% sure how. code has too many indirections.
	if ( m25->m_gettingList ) {
		log("linkdb: avoiding core");
		return;
	}

	// sanity
	gbassert_false( m25->m_msg5.m_msg3.m_numScansCompleted < 
	     m25->m_msg5.m_msg3.m_numScansStarted );

	if ( g_errno )
		log("linkdb: error getting linkinfo: %s",mstrerror(g_errno));

	// it did not block... g_errno will be set on error so sendReply()
	// should in that case send an error reply.
	sendReplyWrapper ( m25 );
}

int32_t Msg25Request::getStoredSize() {
	return getMsgStoredSize(sizeof(*this), &size_site, &size_oldLinkInfo);
}

// . fix the char ptrs for sending over the network
// . use a for loop like we do in Msg20.cpp if we get too many strings
void Msg25Request::serialize ( ) {
	char *p = ((char*)this) + sizeof(*this);

	gbmemcpy ( p , ptr_url , size_url );
	p += size_url;

	gbmemcpy ( p , ptr_site , size_site );
	p += size_site;

	gbmemcpy ( p , ptr_oldLinkInfo , size_oldLinkInfo );
	p += size_oldLinkInfo;
}

void Msg25Request::deserialize ( ) {

	char *p = ((char*)this) + sizeof(*this);

	ptr_url = p;
	p += size_url;

	if ( size_url == 0 ) ptr_url = NULL;

	ptr_site = p;
	p += size_site;

	if ( size_site == 0 ) ptr_site = NULL;

	ptr_oldLinkInfo = p;
	p += size_oldLinkInfo;

	if ( size_oldLinkInfo == 0 ) ptr_oldLinkInfo = NULL;
}

//////
//
// OLD interface below here. use the stuff above now so we can send
// the request to a single host and multiple incoming requests can
// wait in line, and we can set network bandwidth too.
//
/////

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we need the siteRec of the url for merging the linkInfo's
// . NOTE: make sure no input vars are on the stack in case we block
// . reallyGetLinkInfo is set to false if caller does not want it but calls
//   us anyway for some reason forgotten...
bool Msg25::getLinkInfo2( char      *site                ,
			  char      *url                 ,
			  // either MODE_PAGELINKINFO or MODE_SITELINKINFO
			  bool       isSiteLinkInfo      ,
			  int32_t       ip                  ,
			  int64_t  docId               ,
			  collnum_t collnum,
			  char      *qbuf                ,
			  int32_t       qbufSize            ,
			  void      *state               ,
			  void (* callback)(void *state) ,
			  bool       isInjecting         ,
			  bool     printDebugMsgs ,
			  bool     printInXml ,
			  int32_t       siteNumInlinks      ,
			  LinkInfo  *oldLinkInfo         ,
			  int32_t       niceness            ,
			  bool       doLinkSpamCheck     ,
			  bool       oneVotePerIpDom     ,
			  bool       canBeCancelled      ,
			  int32_t       lastUpdateTime      ,
			  bool       onlyNeedGoodInlinks  ,
			  bool       getLinkerTitles ,
			  int32_t       ourHostHash32 ,
			  int32_t       ourDomHash32 ,
			  // put LinkInfo output class in here
			  SafeBuf   *linkInfoBuf ) {

	// reset the ip table
	reset();

	if ( isSiteLinkInfo ) m_mode = MODE_SITELINKINFO;
	else                  m_mode = MODE_PAGELINKINFO;
	m_printInXml = printInXml;

	if ( printDebugMsgs ) m_pbuf = &m_tmp;
	else                  m_pbuf = NULL;

	m_onlyNeedGoodInlinks = onlyNeedGoodInlinks;
	m_getLinkerTitles     = getLinkerTitles;
	// save safebuf ptr, where we store the link info
	m_linkInfoBuf = linkInfoBuf;
	gbassert(linkInfoBuf);
	// sanity check
	gbassert_false( m_mode == MODE_PAGELINKINFO && ! docId );
	// get collection rec for our collection
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	// bail if NULL
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("build: No collection record found when getting "
		    "link info.");
		return true;
	}

	m_gettingList = false;
	// record this in case we were called by Msg3b with the spiders off
	m_spideringEnabled    = g_conf.m_spideringEnabled;
	m_ourHostHash32 = ourHostHash32;
	m_ourDomHash32 = ourDomHash32;
	m_niceness            = niceness;
	m_maxNumLinkers       = MAX_LINKERS;
	m_errno               = 0;
	m_numReplyPtrs        = 0;
	m_bufPtr              = m_buf;
	m_bufEnd              = m_buf + MAX_NOTE_BUF_LEN;
	m_dupCount            = 0;
	m_vectorDups          = 0;
	m_spamLinks           = 0;
	m_errors              = 0;
	m_noText              = 0;
	m_reciprocal          = 0;
	m_ipDupsLinkdb        = 0;
	m_docIdDupsLinkdb     = 0;
	m_lostLinks           = 0;
	m_ipDups              = 0;
	m_linkSpamLinkdb      = 0;
	m_docId               = docId;
	m_collnum = collnum;
	m_callback            = callback;
	m_state               = state;
	m_oneVotePerIpDom     = oneVotePerIpDom;
	m_doLinkSpamCheck     = doLinkSpamCheck;
	m_canBeCancelled      = canBeCancelled;
	m_siteNumInlinks      = siteNumInlinks; // -1 --> unknown
	m_qbuf                = qbuf;
	m_qbufSize            = qbufSize;
	m_isInjecting         = isInjecting;
	m_oldLinkInfo         = oldLinkInfo;
	m_ip                  = ip;
	m_top                 = iptop(m_ip);
	m_lastUpdateTime      = lastUpdateTime;

	m_nextKey.setMin();

	m_adBanTable.reset();
	m_adBanTable.set(4,0,0,NULL,0,false,m_niceness,"adbans");

	m_table.set (4,sizeof(NoteEntry *),0,
		     NULL,0,false,m_niceness,"msg25tab");

	QUICKPOLL(m_niceness);

	m_url = url;
	m_site = site;

	// and the "mid domain hash" so that ibm.com and ibm.ru cannot both
	// vote even if from totally different ips
	Url u; u.set(url);
	char *m      = u.getMidDomain();
	int32_t  mlen   = u.getMidDomainLen();
	m_midDomHash = hash32 ( m , mlen );

	// do not prepend "www." to the root url
	m_prependWWW = false;
	// we have not done a retry yet
	m_retried = false;

	// then the url/site hash
	m_linkHash64 = (uint64_t) u.getUrlHash64();

	m_round = 0;

	// must have a valid ip
	if ( ! ip || ip == -1 ) {
		log("linkdb: no inlinks because ip is invalid");
		g_errno = EBADENGINEER;
		return true;
	}


	return doReadLoop();
}

// . returns false if blocked, returns true otherwise
// . returns true and sets g_errno on error
bool Msg25::doReadLoop ( ) {

	// sanity. no double entry.
	gbassert_false( m_gettingList );

	// . get the top X results from this termlist
	// . but skip link: terms with a 1 (no link text) for a score
	// . these keys are ordered from lowest to highest
	key224_t startKey ;
	key224_t endKey   ;

	int32_t siteHash32 = hash32n ( m_site );

	// access different parts of linkdb depending on the "mode"
	if ( m_mode == MODE_SITELINKINFO ) {
		startKey = g_linkdb.makeStartKey_uk ( siteHash32 );
		endKey   = g_linkdb.makeEndKey_uk   ( siteHash32 );
	}
	else {
		startKey = g_linkdb.makeStartKey_uk (siteHash32,m_linkHash64 );
		endKey   = g_linkdb.makeEndKey_uk   (siteHash32,m_linkHash64 );
	}

	// resume from where we left off?
	if ( m_round > 0 ) 
		gbmemcpy ( &startKey , &m_nextKey , LDBKS );

	// but new links: algo does not need internal links with no link test
	// see Links.cpp::hash() for score table

	QUICKPOLL(m_niceness);

	m_minRecSizes = READSIZE;

	int32_t numFiles = -1;
	// NO, DON't restrict because it will mess up the hopcount.
	bool includeTree = true;
	// debug log
	if ( g_conf.m_logDebugLinkInfo ) {
		char *ms = "page";
		if ( m_mode == MODE_SITELINKINFO ) ms = "site";
		log("msg25: reading linkdb list mode=%s site=%s url=%s "
		    "docid=%" INT64 " linkdbstartkey=%s",
		    ms,m_site,m_url,m_docId,KEYSTR(&startKey,LDBKS));
	}

        if ( g_process.m_mode == EXIT_MODE ) {
		log("linkdb: shutting down. exiting link text loop.");
		g_errno = ESHUTTINGDOWN;
		return false;
	}

	m_gettingList = true;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) {
		log("linkdb: no coll for collnum %" INT32 "",(int32_t)m_collnum);
		g_errno = ENOCOLLREC;
		return true;
	}

	// . get the linkdb list
	// . we now get the WHOLE list so we can see how many linkers there are
	// . we need a high timeout because udp server was getting suspended
	//   before for 30 seconds and this was timing out and yahoo.com
	//   was getting spidered w/o any link text -- that's bad.
	//   Now we hang indefinitely. We also fixed UdpServer to resend
	//   requests after 30 seconds even though it was fully acked in case
	//   the receiving host went down and is now back up.
	if ( ! m_msg5.getList ( 
				RDB_LINKDB      ,
				cr->m_collnum          ,
				&m_list         ,
				(char*)&startKey,
				(char*)&endKey  ,
				m_minRecSizes   ,
				includeTree     ,
				false , // add to cache?
				0 ,
				0               ,
				numFiles        ,
				this            ,
				gotListWrapper  ,
				m_niceness      ,
				true            )){ // error correct?
		return false;
	}
	// all done
	m_gettingList = false;
	// debug log
	if ( g_conf.m_logDebugBuild )
		log("build: msg25 call to msg5 did not block");

	// sanity
	gbassert_false( m_msg5.m_msg3.m_numScansCompleted < 
	     m_msg5.m_msg3.m_numScansStarted );

	// return true on error
	if ( g_errno ) {
		log("build: Had error getting linkers to url %s : %s.",
		    m_url,mstrerror(g_errno));
		return true;
	}
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	return gotList();
}

void gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	Msg25 *THIS = (Msg25 *) state;

	// return if it blocked
	// . this calls sendRequests()
	// . which can call gotLinkText(NULL) if none sent
	// . which can call doReadLoop() if list was not empty (lost linker)
	// . which can block on msg0
	if ( ! THIS->gotList() ) return;

	// error? wait for all replies to come in...
	if ( THIS->m_numRequests > THIS->m_numReplies ) {
		log("msg25: had error %s numreplies=%" INT32 " numrequests=%" INT32 " "
		    "round=%" INT32 "",
		    mstrerror(g_errno),THIS->m_numReplies,THIS->m_numRequests,
		    THIS->m_round);
		return;
	}

	// the call to gotList() may have launched another msg0 even
	// though it did not return false???
	// THIS FIXED a double entry core!!!! msg0 would return after
	// we had destroyed the xmldoc class behind this!!
	if ( THIS->m_gettingList ) {
		return;
	}

	// otherwise call callback, g_errno probably is set
	THIS->m_callback ( THIS->m_state );//, THIS->m_linkInfo );
}

// . this returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg25::gotList() {
	// all done
	m_gettingList = false;

	// sanity
	gbassert_false( m_msg5.m_msg3.m_numScansCompleted < 
	     m_msg5.m_msg3.m_numScansStarted );

	// return true on error
	if ( g_errno ) {
		log("build: Had error getting linkers to url %s : %s.",
		    m_url,mstrerror(g_errno));
		return true;
	}

	// . record the # of hits we got for weighting the score of the
	//   link text iff it's truncated by MAX_LINKERS
	// . TODO: if url is really popular, like yahoo, we should use the
	//   termFreq of the link: term!
	// . TODO: we now only read in first 50k linkers so Msg0::getList()
	//   doesn't waste space through its stupid buffer pre-allocation.
	//   it should not preallocate for us since our niceness is over 1
	//   cuz we don't require a real-time signal handler to read our reply.
	m_list.resetListPtr();
	// clear this too
	m_k = (Inlink *)-1;

	// we haven't got any responses as of yet or sent any requests
	m_numReplies    = 0;
	m_numRequests   = 0;

	if ( m_round == 0 ) {
		m_linkSpamOut   = 0;
		m_numFromSameIp = 0;
		memset ( m_inUse  , 0 , MAX_MSG20_OUTSTANDING );
		// use this to dedup ips in linkdb to avoid looking up their
		// title recs... saves a lot of lookups
		if (!m_ipTable.set(4,0,256,NULL,0,false,m_niceness,"msg25ips"))
			return true;
		int64_t needSlots = m_list.getListSize() / LDBKS;
		// wtf?
		if ( m_list.getListSize() > READSIZE + 10000 ) {
			//gbassert(false); }
			log("linkdb: read very big linkdb list %" INT32 " bytes "
			    "bigger than needed",
			    m_list.getListSize() - READSIZE );
		}
		// triple for hash table speed
		needSlots *= 3;
		// ensure 256 min
		if ( needSlots < 256 ) needSlots = 256;
		
		if ( ! m_fullIpTable.set(4,0,needSlots,NULL,0,false,
					 m_niceness,"msg25ip32") )
			return true;

		if ( ! m_firstIpTable.set(4,0,needSlots,NULL,0,false,
					  m_niceness,"msg25fip32") )
			return true;
		// this too
		if ( ! m_docIdTable.set(8,0,needSlots,
					NULL,0,false,m_niceness,"msg25docid") )
			return true;
		// . how many link spam inlinks can we accept?
		// . they do not contribute to quality
		// . they only contribute to link text
		// . their number and weights depend on our ROOT QUALITY!
		// . we need this because our filters are too stringent!
		m_spamCount  = 0;
		m_spamWeight = 0;
		m_maxSpam    = 0;
		m_numDocIds  = 0;
		m_cblocks    = 0;
		m_uniqueIps  = 0;
	}

	// if we are doing site linkinfo, bail now
	if ( m_mode == MODE_SITELINKINFO ) return sendRequests();

	// when MODE_PAGELINKINFO we must have a site quality for that site
	gbassert_false( m_siteNumInlinks < 0 );

	// int16_tcut
	int32_t n = m_siteNumInlinks;
	if      ( n >= 1000 ) {m_spamWeight = 90; m_maxSpam = 4000;}
	else if ( n >=  900 ) {m_spamWeight = 80; m_maxSpam = 3000;}
	else if ( n >=  800 ) {m_spamWeight = 70; m_maxSpam = 2000;}
	else if ( n >=  700 ) {m_spamWeight = 55; m_maxSpam = 1000;}
	else if ( n >=  600 ) {m_spamWeight = 50; m_maxSpam =  100;}
	else if ( n >=  500 ) {m_spamWeight = 15; m_maxSpam =   20;}
	else if ( n >=  200 ) {m_spamWeight = 10; m_maxSpam =   15;}
	else if ( n >=   70 ) {m_spamWeight = 07; m_maxSpam =   10;}
	else if ( n >=   20 ) {m_spamWeight = 05; m_maxSpam =    7;}

	// now send the requests
	m_list.resetListPtr();
	return sendRequests();
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg25::sendRequests ( ) {
	uint64_t lastDocId = 0LL;

	// smaller clusters cannot afford to launch the full 300 msg20s
	// because it can clog up one host!
	float ratio = (float)g_hostdb.getNumHosts() / 128.0;
	int32_t ourMax = (int32_t)(ratio * (float)MAX_MSG20_OUTSTANDING);
	if ( ourMax > MAX_MSG20_OUTSTANDING )
		ourMax = MAX_MSG20_OUTSTANDING;

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) {
		log("linkdb: collnum %" INT32 " is gone 1",(int32_t)m_collnum);
		// that func doesn't set g_errno so we must
		g_errno = ENOCOLLREC;
		return true;
	}

	// if more than 300 sockets in use max this 1. prevent udp socket clog.
	if ( g_udpServer.m_numUsedSlots >= 300 ) ourMax = 1;

	// keep sending requests
	while ( 1 == 1 ) {

		// breathe
		QUICKPOLL ( m_niceness );

		// if we still haven't gotten enough good inlinks, quit after
		// looking up this many titlerecs
		if ( m_numRequests >= MAX_DOCIDS_TO_SAMPLE ) break;
		// . we only need at most MAX_LINKERS in our sample
		// . but we do keep "losers" until the very end so we can
		//   remove them in an order-independent fashion to guarantee
		//   consistency. otherwise, "losers" will depend on the order
		//   in which the Msg20Replies are received to some degree
		if ( m_numReplyPtrs >= MAX_LINKERS ) break;
		// do not have more than this many outstanding Msg23s
		if ( m_numRequests-m_numReplies >= ourMax ) break;
		// we may have pre-allocated the LinkText classes for
		// use be currently outstanding Msg20 requests, therefore
		// they are not available at this time... m_numReplyPtrs is
		// how many replies we have kept.
		if ( m_numReplyPtrs+m_numRequests-m_numReplies>=MAX_LINKERS) 
			break;



		// reset g_errno just in case
		g_errno = 0;

		char     isLinkSpam =  0;
		int32_t     itop  ;
		uint32_t     ip32;
		uint64_t docId ;
		int32_t     discovered = 0;
		// was the link lost?
		int32_t     lostDate = 0; 

		// . recycle inlinks from the old link info guy
		// . this keeps our inlinks persistent!!! very nice...
		// . useful for when they disappear on an aggregator site
		if ( m_list.isExhausted() && m_round == 0 ) {
			// recycle old inlinks at this point
			if ( m_k == (Inlink *)-1 ) m_k = NULL;
			// get it
            if ( ! m_oldLinkInfo ) break;
			m_k = m_oldLinkInfo->getNextInlink ( m_k );
			// if none left, we really are done
			if ( ! m_k ) break;
			// set these
			itop       = m_k->m_ip & 0x0000ffff;
			ip32       = m_k->m_ip;
			isLinkSpam = m_k->m_isLinkSpam;
			docId      = m_k->m_docId;
			discovered = m_k->m_firstIndexedDate;
		}
		else if ( m_list.isExhausted() && m_round != 0 ) 
			break;
		// is this a "url" key?
		else if ( m_mode == MODE_PAGELINKINFO ) {
			// get the current key if list has more left
			key224_t key; m_list.getCurrentKey( &key );
			itop       = g_linkdb.getLinkerIp24_uk     ( &key );
			ip32       = g_linkdb.getLinkerIp_uk     ( &key );
			isLinkSpam = g_linkdb.isLinkSpam_uk  ( &key );
			docId      = g_linkdb.getLinkerDocId_uk    ( &key );
			discovered = g_linkdb.getDiscoveryDate_uk(&key);
			// is it expired?
			lostDate = g_linkdb.getLostDate_uk(&key);
			// update this
			gbmemcpy ( &m_nextKey  , &key , LDBKS );
			m_nextKey += 1;
		}
		// otherwise this is a "site" key. we are getting all the
		// inlinks to any page on the site...
		else {
			// get the current key if list has more left
			key224_t key; m_list.getCurrentKey( &key );
			itop       = g_linkdb.getLinkerIp24_uk     ( &key );
			ip32       = g_linkdb.getLinkerIp_uk     ( &key );
			isLinkSpam = false;
			docId      = g_linkdb.getLinkerDocId_uk    ( &key );
			discovered = g_linkdb.getDiscoveryDate_uk(&key);
			// is it expired?
			lostDate = g_linkdb.getLostDate_uk(&key);
			// update this
			gbmemcpy ( &m_nextKey  , &key , LDBKS );
			m_nextKey += 1;
		}

		// advance to next rec if the list has more to go
		if ( ! m_list.isExhausted() ) m_list.skipCurrentRecord();

		// clear this if we should
		if ( ! m_doLinkSpamCheck ) isLinkSpam = false;

		// if it is no longer there, just ignore
		if ( lostDate ) {
			m_lostLinks++;
			continue;
		}

		// try using this to save mem then
		if ( docId == lastDocId ) {
			m_docIdDupsLinkdb++;
			continue;
		}
		// update this then
		lastDocId = docId;

		// count unique docids
		m_numDocIds++;

		// count unique ips for steve's stats
		if ( ! m_fullIpTable.isInTable(&ip32) ) {
			// return true on error
			if ( ! m_fullIpTable.addKey(&ip32) )
				return true;
			// count it
			m_uniqueIps++;
		}

		// TODO: if inlinker is internal by having the same DOMAIN
		// even though a different ip, we should adjust this logic!!
		if ( itop != m_top ) {
			int32_t slot = m_ipTable.getSlot ( &itop );
			if ( slot != -1 ) {m_ipDupsLinkdb++;continue;}
			// store it
			if ( ! m_ipTable.addKey ( &itop ) )
				return true;
			// count unique cblock inlinks
			m_cblocks++;
		}
		// if we are local... allow up to 5 votes, weight is diminished
		else {
			// count your own, only once!
			if ( m_numFromSameIp == 0 ) m_cblocks++;
			// count it as internal
			m_numFromSameIp++;
			// only get link text from first 5 internal linkers,
			// they will all count as one external linker
			if ( m_numFromSameIp > MAX_INTERNAL_INLINKS ) {
				m_ipDupsLinkdb++; continue; }
		}

			
		// count this request as launched
		m_numRequests++;
		// if linkspam, count this
		if ( isLinkSpam ) m_linkSpamOut++;

		// find a msg20 we can use
		int32_t j ;
		for (j=0 ;j<MAX_MSG20_OUTSTANDING;j++) if (!m_inUse[j]) break;
		// sanity check
		gbassert_false( j >= MAX_MSG20_OUTSTANDING );
		// "claim" it
		m_inUse [j] = 1;

		// . this will return false if blocks
		// . we EXPECT these recs to be there...
		// . now pass in the score for the newAlgo
		Msg20Request *r = &m_msg20Requests[j];
		// clear it. reset to defaults.
		r->reset();
		// set the request
		r->m_getLinkText     = true;
		r->m_onlyNeedGoodInlinks = m_onlyNeedGoodInlinks;
		// is linkee a site? then we will try to find link text
		// to any page on that site...
		// if we are in site mode, then m_url should be m_site!
		if ( m_mode == MODE_PAGELINKINFO ) {
			r->m_isSiteLinkInfo = false;
			r-> ptr_linkee = m_url;
			r->size_linkee = gbstrlen(m_url)+1; // include \0
		}
		else {
			r->m_isSiteLinkInfo = true;
			r-> ptr_linkee = m_site;
			r->size_linkee = gbstrlen(m_site)+1; // include \0
		}
		r->m_collnum = cr->m_collnum;
		r->m_docId           = docId;
		r->m_expected        = true; // false;
		r->m_niceness        = m_niceness;
		r->m_state           = r;
		r->m_state2          = this;
		r->m_j               = j;
		r->m_callback        = gotLinkTextWrapper;
		// do NOT get summary stuff!! slows us down...
		r->m_maxNumCharsPerLine = 0;
		r->m_numSummaryLines    = 0;
		// get title now for steve
		r->m_titleMaxLen        = 300;
		r->m_summaryMaxLen      = 0;
		r->m_discoveryDate      = discovered;
		// buzz sets the query to see if inlinker has the query terms
		// so we can set <absScore2>
		r->m_langId             = langUnknown; // no synonyms i guess
		r->ptr_qbuf             = m_qbuf;
		r->size_qbuf            = m_qbufSize;
		// place holder used below
		r->m_isLinkSpam         = isLinkSpam;
		// buzz may not want link spam checks! they are pretty clean.
		r->m_doLinkSpamCheck    = m_doLinkSpamCheck;
		// this just gets the LinkInfo class from the TITLEREC
		// so that Msg25 should not be called. thus avoiding an
		// infinite loop!
		// we need the LinkInfo of each inlinker to get the inlinker's
		// sitePop and numInlinksToSite, which is needed to call
		// makeLinkInfo() below.
		r->m_getLinkInfo        = true;


		r->m_ourHostHash32 = m_ourHostHash32;
		r->m_ourDomHash32  = m_ourDomHash32;

		// . MAKE A FAKE MSG20REPLY for pre-existing Inlinks
		// . the opposite of Inlink::set(Msg20Reply *)
		// . we used that as a reference
		// . ISSUES:
		// . 1. if inlinker gets banned, we still recycle it
		// . 2. if ad id gets banned, we still recycle it
		// . 3. we cannot dedup by the vectors, because we do not
		//      store those in the Inlink class (Msg25::isDup())
		if ( m_k && m_k != (Inlink *)-1 ) {
			Msg20Reply *rep = &m_msg20Replies[j];
			rep->reset();
			m_k->setMsg20Reply ( rep );
			// let receiver know we are a recycle
			rep->m_recycled = 1;
			// . this returns true if we are done
			// . g_errno is set on error, and true is returned
			if ( gotLinkText ( r ) ) return true;
			// keep going
			continue;
		}

		// debug log
		if ( g_conf.m_logDebugLinkInfo ) {
			char *ms = "page";
			if ( m_mode == MODE_SITELINKINFO ) ms = "site";
			log("msg25: getting single link mode=%s site=%s "
			    "url=%s docid=%" INT64 " request=%" INT32 "",
			    ms,m_site,m_url,docId,m_numRequests-1);
		}

		// returns false if blocks, true otherwise
		bool status = m_msg20s[j].getSummary ( r ) ;
		// breathe
		QUICKPOLL(m_niceness);
		// if blocked launch another
		if ( ! status ) continue;
		// . this returns true if we are done
		// . g_errno is set on error, and true is returned
		if ( gotLinkText ( r ) ) return true;
		
	}

	// we may still be waiting on some replies to come in
	if ( m_numRequests > m_numReplies ) return false;

	// . returns false if not all replies have been received
	// . returns true if done
	// . sets g_errno on error
	// . if all replies are in then this can call doReadLoop() and
	//   return false!
	return gotLinkText ( NULL );
}

bool gotLinkTextWrapper ( void *state ) {
	Msg20Request *req = (Msg20Request *)state;
	// get our Msg25
	Msg25 *THIS = (Msg25 *)req->m_state2;

	// . this returns false if we're still awaiting replies
	// . returns true if all replies have been received and processed
	if ( THIS->gotLinkText ( req ) ) {

		if ( THIS->m_gettingList ) return false;

		// . now call callback, we're done
		// . g_errno will be set on critical error
		THIS->m_callback ( THIS->m_state );
		return true;
	}
	// if gotLinkText() called doReadLoop() it blocked calling msg0
	if ( THIS->m_gettingList ) return false;
	// . try to send more requests
	// . return if it blocked
	// . shit, this could call doReadLoop() now that we have added
	//   the lostdate filter, because there will end up being no requests
	//   sent out even though the list was of positive size, so it
	//   will try to read the next round of msg0.
	if ( ! THIS->sendRequests ( ) ) return false;
	// . shit, therefore, return false if we did launch a msg0 after this
	if ( THIS->m_gettingList ) return false;

	// otherwise we're done
	THIS->m_callback ( THIS->m_state );
	return true;
}

char *getExplanation ( char *note ) {

	if ( ! note ) return NULL;
	if ( strcmp(note,"good")==0) return NULL;

	static char *s_notes[] = {

		"same mid domain",
		"inlinker's domain, excluding TLD, is same as page it "
		"links to",

		"linker banned or filtered", 
		"inlinker's domain has been manually banned",

		"no link text",
		"inlink contains no text, probably an image",

		"banned by ad id",
		"inlinking page contains a google ad id that has been "
		"manually banned",

		"ip dup",
		"inlinker is from the same C Block as another inlinker",

		"first ip dup",
		"first recorded C Block of inlinker matches another inlinker",

		"post page",
		"inlink is from a page that contains a form tag whose "
		"submit url contains character sequence that are indicative "
		"of posting a comment, thereby indicating that the inlink "
		"could be in a comment section",

		"path is cgi",
		"inlinker url contains a question mark",

		"similar link desc",
		"the text surrounding the anchor text of this inlink is "
		"too similar to that of another processed inlink",

		"similar content",
		"the inlinker's page content is "
		"too similar to that of another processed inlink",

		"doc too big",
		"inlinker's page is too large, and was truncated, and "
		"might have lost some indicators",

		"link chain middle",
		"inlink is in the middle of a list of inlinks, without "
		"any non-link text separating it, inidicative of text ads",

		"link chain right",
		"inlink is at the end of a list of inlinks, without "
		"any non-link text separating it, inidicative of text ads",

		"link chain left",
		"inlink is at the beginning of a list of inlinks, without "
		"any non-link text separating it, inidicative of text ads",

		"near sporny outlink",
		"inlink is near another outlink on that page which contains "
		"porn words in its domain or url",

		"70.8*.",
		"inlinker is from an IP address that is of the form "
		"70.8*.*.* which is a notorious block of spam",

		".info tld",
		"inlinker's tld is .info, indicative of spam",

		".biz tld",
		"inlinker's tld is .biz, indicative of spam",

		"textarea tag",
		"inlinker page contains a textarea html tag, indicative "
		"of being in a comment section",

		"stats page",
		"inlinker is from a web access stats page",
		
		"has dmoz path",
		"inlinker url looks like a dmoz mirror url",
		
		"guestbook in hostname",
		"inlinker is from a guestbook site",

		"ad table",
		"inlink appears to be in a table of ad links",

		"duplicateIPCClass",
		"duplicate ip c block"

	};

	int32_t n = sizeof(s_notes)/ sizeof(char *);
	for ( int32_t i = 0 ; i < n ; i += 2 ) {
		if ( strcmp(note,s_notes[i]) ) continue;
		return s_notes[i+1];
	}

	if ( strncmp(note,"path has",8) == 0 )
		return "inlinker's url contains keywords indicative "
			"of a comment page, guestbook page or "
			"link exchange";

	return 
		"inlinker's page contains the described text, indicative of "
		"being a link exchange or being in a comment section or "
		"being an otherwise spammy page";
}

// . returns false if not all replies have been received (or timed/erroredout)
// . returns true if done
// . sets g_errno on error
bool Msg25::gotLinkText ( Msg20Request *req ) {

	int32_t j = -1;
	if ( req ) j = req->m_j;
	// get it
	Msg20 *m = NULL;
	// the reply
	Msg20Reply *r = NULL;
	// the alloc size of the reply
	int32_t rsize = 0;
	// the original request

	// set the reply
	if ( j >= 0 ) {
		// get the msg20
		m = &m_msg20s[j];
		// set the reply
		r = m->m_r;
		// the reply size
		rsize = m->m_replyMaxSize;
		// inc # of replies
		m_numReplies++;
		// get the request
		Msg20Request *req = &m_msg20Requests[j];
		// discount this if was linkspam
		if ( req->m_isLinkSpam ) m_linkSpamOut--;
		// "make available" msg20 and msg20Request #j for re-use
		m_inUse [ j ] = 0;
		// . propagate internal error to g_errno
		// . if g_errno was set then the reply will be empty
		if ( r && r->m_errno && ! g_errno ) g_errno = r->m_errno;
		// if it had an error print it for now
		if ( r && r->m_errno )
			log("query: msg25: msg20 had error for docid %" INT64 " : "
			    "%s",r->m_docId, mstrerror(r->m_errno));
	}
	
	// what is the reason it cannot vote...?
	char *note    = NULL;
	int32_t  noteLen = 0;
	
	// assume it CAN VOTE for now
	bool good = true;

	// just log then reset g_errno if it's set
	if ( g_errno ) {
		// a dummy docid
		int64_t docId = -1LL;
		// set it right
		if ( r ) docId = r->m_docId;
		// we often restrict link: termlist lookup to indexdb root
		// file, so we end up including terms from deleted docs...
		// this we get a lot of ENOTFOUND errors. 
		// MDW: we no longer do this restriction...
		log(LOG_DEBUG,
		    "build: Got error getting link text from one document: "
		    "%s. Will have to restart later. docid=%" INT64 ".",
		    mstrerror(g_errno),docId);
		// this is a special case
		if ( g_errno == ECANCELLED || 
		     g_errno == ENOCOLLREC ||
		     g_errno == ENOMEM     ||
		     g_errno == ENOSLOTS    ) {
			m_errors++;
			if ( m_numReplies < m_numRequests ) return false;
			if ( m_gettingList ) {
				log("linkdb: gotLinkText: gettinglist1");
				return false;
			}
			return true;
		}
		// otherwise, keep going, but this reply can not vote
		good = false;
		m_errors++;
		note = mstrerror ( g_errno );
		// reset g_errno
		g_errno = 0;		
	}

	// are we an "internal" inlink?
	bool internal = false;
	if ( r && iptop(r->m_firstIp) == m_top )
		internal = true;
	if ( r && iptop(r->m_ip) == m_top )
		internal = true;

	// . if the mid domain hash of the inlinker matches ours, no voting
	// . this is set to 0 for recycles
	if ( r && good && r->m_midDomHash == m_midDomHash && ! internal ) {
		good = false;
		m_sameMidDomain++;
		note = "same mid domain";
	}

	// is the inlinker banned?
	if ( r && good && r->m_isBanned ) {
		// it is no longer good
		good = false;
		// inc the general count, too
		m_spamLinks++;
		// add his ad id hash to the table, so if any
		// other linker has it, it will be banned by ad id!
		if ( r->m_adIdHash ) m_adBanTable.addKey ( &r->m_adIdHash );
		// count each *type* of "link spam". the type is given
		// by linkText->m_note and is a string...
		note = "linker banned or filtered";
	}

	// breathe
	QUICKPOLL(m_niceness);

	// get the linker url
	Url linker; 
	if ( r ) linker.set ( r->ptr_ubuf , r->size_ubuf );

	// sanity check, Xml::set() requires this...
	if ( r&&r->size_rssItem > 0 && r->ptr_rssItem[r->size_rssItem-1]!=0 ) {
		log("admin: received corrupt rss item of size "
		    "%" INT32 " not null terminated  from linker %s",
		    r->size_rssItem,r->ptr_ubuf);
		// ignore it for now
		r->size_rssItem = 0;
		r->ptr_rssItem  = NULL;
	}

	// . if no link text, count as error
	if ( r && good &&
	     r->size_linkText <= 0 && 
	     r->size_rssItem  <= 0 && 
	     // allow if from a ping server because like 
	     // rpc.weblogs.com/int16_tChanges.xml so we can use
	     // "inlink==xxx" in the url filters to assign any page linked
	     // to by a pingserver into a special spider queue. then we can
	     // spider that page quickly and get its xml feed url, and then
	     // spider that to get new outlinks of permalinks.
	     // Well now we use "inpingserver" instead of having to specify
	     // the "inlink==xxx" expression for every ping server we know.
	     ! linker.isPingServer() ) {
		good = false;
		m_noText++;
		note = "no link text";
	}

	// banned by way of ad id?
	if (r && good&& r->m_adIdHash&&m_adBanTable.getSlot(&r->m_adIdHash)>0){
		// it is no longer good
		good = false;
		// inc the general count, too
		m_spamLinks++;
		// count each *type* of "link spam". the type is given
		// by linkText->m_note and is a string...
		note = "banned by ad id";
	}

	QUICKPOLL(m_niceness);
	// discount if LinkText::isLinkSpam() or isLinkSpam2() said it
	// should not vote
	if ( r && good && ! internal && r->m_isLinkSpam &&
	     // we can no allow link spam iff it is below the max!
	     ++m_spamCount >= m_maxSpam ) {
		// it is no longer good
		good = false;
		// inc the general count, too
		m_spamLinks++;
		// count each *type* of "link spam". the type is given
		// by linkText->m_note and is a string...
		note    = r-> ptr_note;
		noteLen = r->size_note - 1; // includes \0
	}

	// loop over all the replies we got so far to see if "r" is a dup
	// or if another reply is a dup of "r"
	int32_t n = m_numReplyPtrs;
	// do not do the deduping if no reply given
	if ( ! r ) n = 0; 
	// do not do this if "r" already considered bad
	if ( ! good ) n = 0;
	// this is the "dup"
	Msg20Reply *dup  = NULL;
	int32_t        dupi = -1;
	// . we do not actually remove the Msg20Replies at this point because 
	//   this filter is dependent on the order in which we receive the 
	//   Msg20Replies. we do the removal below after all replies are in.
	// . NO! not anymore, i don't want to hit MAX_LINKERS and end up
	//   removing all the dups below and end up with hardly any inlinkers
	for ( int32_t i = 0 ; ! internal && i < n ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get the reply in a ptr
		Msg20Reply *p = m_replyPtrs[i];
		// is it internal
		bool pinternal = false;
		if ( iptop(p->m_ip) == m_top ) pinternal = true;
		if ( iptop(p->m_firstIp) == m_top ) pinternal = true;
		// allow internal inlinks to match
		if ( pinternal ) continue;
		// is "p" a dup of us? (or we of it?)
		char *dupNote = isDup ( r , p ) ;
		// if it is not a dup, keep going
		if ( ! dupNote ) continue;
		// getLoser() returns the lowest-scoring reply of "r" and "p"
		Msg20Reply *tmp = getLoser ( r , p );
		// is it worse than the current "dup"? if so, update "dup"
		dup = getLoser ( tmp , dup );
		// get the "i" value
		if ( dup == r ) dupi = j;
		if ( dup == p ) dupi = i;
		// we got a dup
		good = false;
		note = dupNote;
	}

	// inc this count
	if ( dup ) m_dupCount++;

	// if "p" is the lower-scoring dup, put "r" in its place, and then
	// set "r" to "p" doing a swap operation
	if ( dup && dup != r ) {
		// sanity check
		gbassert_false( dupi < 0 );
		// HACK: swap them
		Msg20Reply *tmp      = m_replyPtrs [dupi];
		int32_t        tmpSize  = m_replySizes[dupi];
		m_replyPtrs [dupi] = r;
		m_replySizes[dupi] = rsize;
		r                    = tmp;
		rsize                = tmpSize;
		// make Msg20 point to that old "dup" reply
		m->m_r            = r;
		m->m_replyMaxSize = rsize;
	}

	// breathe
	QUICKPOLL(m_niceness);

	if ( r && good ) {
		int32_t iptop1 = iptop(r->m_ip);
		int32_t iptop2 = iptop(r->m_firstIp);
		if ( m_firstIpTable.isInTable ( &iptop1 ) ||
		     m_firstIpTable.isInTable ( &iptop2 ) ) {
			good = false;
			m_ipDups++;
			note = "first ip dup";
		}
		// add to table. return true with g_errno set on error
		if ( ! m_firstIpTable.addKey(&iptop1) ) return true;
		if ( ! m_firstIpTable.addKey(&iptop2) ) return true;
	}


	// BUT do not set good to false so it is stored so we can look
	// at the linktext for indexing purposes anyway, but we do not
	// want to count it towards the # of good siteinlinks because
	// its internal
	if ( internal && ! note ) {
		m_ipDups++;
		note = "ip dup";
	}

	if ( r && ! good && ! note )
		note = "unknown reason";

	// compile the reason it could not vote
	if ( r && ! good ) {
		// set "noteLen" if not yet set
		if ( note && noteLen == 0 ) noteLen = gbstrlen ( note );
		// add it to our table
		addNote ( note , noteLen , r->m_docId );
		// . free the reply since it cannot vote
		// . no, it should be auto-freed when the msg20 is re-used
	}

	bool store = true;
	if ( ! good ) store = false;
	// . if doing for display, show good
	// . steve needs to see the bad guys as well ppl can learn
	if ( m_pbuf ) store = true;

	// now for showing recommended link sources let's include the
	// bad boys because we might have mislabelled them as bad, b/c maybe
	// google thinks they are good! fix for 
	if ( ! m_onlyNeedGoodInlinks ) store = true;

	// how is this NULL?
	if ( ! r ) store = false;

	if ( store ) {
		// save the reply
		m_replyPtrs [m_numReplyPtrs] = r;
		m_replySizes[m_numReplyPtrs] = rsize;
		// why we do this?
		if ( note && ! r->ptr_note ) {
			r->ptr_note = note;
			r->size_note = noteLen+1;
		}
		// store this in the reply for convenience
		r->m_discoveryDate = req->m_discoveryDate;
		m_numReplyPtrs++;
		// do not allow Msg20 to free it
		m->m_r = NULL;
	}

	// free the reply buf of this msg20 now to save mem because
	// we can't send out like 100,000 of these for yahoo.com to find
	// less than 1000 good ones!
	// tell msg20 to free the reply if not null
	if ( m ) m->reset();

	// wait for all replies to come in
	if ( m_numReplies < m_numRequests ) return false;

	if ( m_gettingList ) {
		log("linkdb: gotLinkText: gettinglist2");
		return false;
	}

	//
	//
	// READ MORE FROM LINKDB to avoid truncation
	//
	//
	// youtube is doing like 180,000 rounds! wtf! limit to 10000
	if ( m_list.m_listSize > 0 &&
	     // no! now we shrink a list by removing dup docids from it
	     // in Msg0.cpp before sending back to save memory and cpu and
	     // network. so it can be well below m_minRecSizes and still need
	     // to go on to the next round
	     m_numReplyPtrs < MAX_LINKERS ) {
		// count it
		m_round++;
		// note it
		char *ms = "page";
		char *id = m_url;
		if ( m_mode == MODE_SITELINKINFO ) {
			ms = "site";
			id = m_site;
		}
		// debug
		if ( g_conf.m_logDebugLinkInfo ) {
			log("linkdb: recalling round=%" INT32 " for %s=%s "
			    "req=0x%" PTRFMT " numlinkerreplies=%" INT32 ,
			    m_round,ms,m_site,(PTRTYPE)m_req25,m_numReplyPtrs);
		}
		// and re-call. returns true if did not block.
		// returns true with g_errno set on error.
		if ( ! doReadLoop() ) return false;
	}
		

	//
	//
	// process all "good" Msg20Replies
	//
	//

	// breathe
	QUICKPOLL(m_niceness);

	// debug log
	if ( g_conf.m_logDebugLinkInfo ) {
		char *ms = "page";
		if ( m_mode == MODE_SITELINKINFO ) ms = "site";
		log("msg25: making final linkinfo mode=%s site=%s url=%s "
		    "docid=%" INT64 "",
		    ms,m_site,m_url,m_docId);
	}

	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) {
		log("linkdb: collnum %" INT32 " is gone 2",(int32_t)m_collnum);
		// that func doesn't set g_errno so we must
		g_errno = ENOCOLLREC;
		return true;
	}
	char *coll = cr->m_coll;

	// . this returns NULL and sets g_errno on error
	// . returns an allocated ptr to a LinkInfo class
	// . we are responsible for freeing
	// . LinkInfo::getSize() returns the allocated size
	makeLinkInfo ( coll            ,
				    m_ip              ,
				    m_siteNumInlinks  ,
				    m_replyPtrs       ,
				    m_numReplyPtrs    ,
				    m_spamWeight      ,
				    m_oneVotePerIpDom ,
				    m_docId           ,
				    m_lastUpdateTime  ,
				    m_onlyNeedGoodInlinks ,
				    m_niceness        ,
				    this ,
				    m_linkInfoBuf );
	// return true with g_errno set on error
	if ( ! m_linkInfoBuf->length() ) {
		log("build: msg25 linkinfo set: %s",mstrerror(g_errno));
		return true;
	}

	// if nothing to print out, be on our way
	if ( ! m_pbuf ) return true;

	/////////////////////////////////////////
	//
	// print out for PageParser.cpp
	//
	/////////////////////////////////////////

	// sort by docid so we get consistent output on PageParser.cpp
	char sflag = 1;
	while ( sflag ) {
		sflag = 0;
		for ( int32_t i = 1 ; i < m_numReplyPtrs ; i++ ) {
			// sort by quality first
			char q1 = m_replyPtrs[i-1]->m_siteRank;//docQuality;
			char q2 = m_replyPtrs[i  ]->m_siteRank;//docQuality;
			if ( q1 > q2 ) continue;
			// if tied, check docids
			int64_t d1 = m_replyPtrs[i-1]->m_docId;
			int64_t d2 = m_replyPtrs[i  ]->m_docId;
			if ( d1 == d2 )
				log("build: got same docid in msg25 "
				    "d=%" INT64 " url=%s",d1,
				    m_replyPtrs[i]->ptr_ubuf);
			if ( q1 == q2 && d1 <= d2 ) continue;
			// swap them
			Msg20Reply *tmp   = m_replyPtrs  [i-1];
			int32_t        size  = m_replySizes [i-1];
			m_replyPtrs [i-1] = m_replyPtrs  [i  ];
			m_replySizes[i-1] = m_replySizes [i  ];
			m_replyPtrs [i  ] = tmp;
			m_replySizes[i  ] = size;
			sflag = 1;
		}
	}

	time_t ttt;
	struct tm *timeStruct = localtime ( &ttt );
	m_lastUpdateTime = ttt;
	char buf[64];
	if ( timeStruct )
		strftime ( buf, 64 , "%b-%d-%Y(%H:%M:%S)" , timeStruct );
	else
		sprintf(buf,"UNKNOWN time");

	char *ss = "site";
	if ( m_mode == MODE_PAGELINKINFO ) ss = "page";

	LinkInfo *info = (LinkInfo *)m_linkInfoBuf->getBufStart();

	int32_t siteRank = ::getSiteRank ( info->m_numGoodInlinks );

	if ( m_printInXml ) { // && m_xd ) {

		m_pbuf->safePrintf("\t<desc>inlinks to %s</desc>\n",ss);

		m_pbuf->safePrintf("\t<sampleCreatedUTC>%" UINT32 ""
				   "</sampleCreatedUTC>\n"
				   , m_lastUpdateTime
				   );
		// m_url should point into the Msg25Request buffer
		char *u = m_url;
		if ( u )
			m_pbuf->safePrintf("\t<url><![CDATA[%s]]></url>\n",u);

		// m_site should point into the Msg25Request buffer
		char *site = m_site;
		if ( site )
			m_pbuf->safePrintf("\t<site><![CDATA[%s]]></site>\n",
					   site);

		int64_t d = m_docId;
		if ( d && d != -1LL )
			m_pbuf->safePrintf("\t<docId>%" INT64 "</docId>\n",d);
			
		m_pbuf->safePrintf(
				   "\t<ipAddress><![CDATA[%s]]></ipAddress>\n"

				   "\t<totalSiteInlinksProcessed>%" INT32 ""
				   "</totalSiteInlinksProcessed>\n"

				   "\t<totalGoodSiteInlinksProcessed>%" INT32 ""
				   "</totalGoodSiteInlinksProcessed>\n"

				   "\t<numUniqueCBlocksLinkingToPage>%" INT32 ""
				   "</numUniqueCBlocksLinkingToPage>\n"

				   "\t<numUniqueIpsLinkingToPage>%" INT32 ""
				   "</numUniqueIpsLinkingToPage>\n"

				   , iptoa(m_ip)
				   // the total # of inlinkers. we may not have
				   // read all of them from disk though.
				   , m_numDocIds
				   , info->m_numGoodInlinks
				   , m_cblocks
				   , m_uniqueIps
				   );
	}
	else
		m_pbuf->safePrintf(   "<table width=100%%>"
				      "<td bgcolor=lightyellow>\n"
				      "<b>Summary of inlinks to %s "
				      "%s</b>\n"
				      "<br><br>"
				      
				      "<table cellpadding=3 "
				      "border=1 width=100%%>\n"
				      
				      "<tr>"
				      "<td>sample created</td>"
				      "<td>%s</td>"
				      "<td>when this info was last "
				      "computed</td>"
				      "</tr>\n"
				      
				      "<tr>"
				      "<td>IP address</td>"
				      "<td>%s</td>"
				      "<td>&nbsp;"
				      "</td>"
				      "<td> &nbsp; </td>"
				      "</tr>\n"
				      
				      "<tr>"
				      "<td>total inlinkers</td>"
				      "<td>%" INT32 "</td>"
				      "<td>how many inlinks we have total. "
				      "Max: %" INT32 "."
				      //" Bad docids are removed so may be "
				      //"less than that max."
				      "</td>"
				      "<td> &nbsp; </td>"
				      "</tr>\n"

				      "<tr>"
				      "<td>unique cblocks</td>"
				      "<td>%" INT32 "</td>"
				      "<td>unique EXTERNAL cblock inlinks</td>"
				      "<td> &nbsp; </td>"
				      "</tr>\n"

				      "<tr>"
				      "<td>unique ips</td>"
				      "<td>%" INT32 "</td>"
				      "<td>unique EXTERNAL IP inlinks</td>"
				      "<td> &nbsp; </td>"
				      "</tr>\n"

				      ,
				      ss,
				      m_url,
				      buf,
				      iptoa(m_ip),
				      // the total # of inlinkers. we may not
				      // have read all of them from disk though
				      m_numDocIds ,
				      (int32_t)READSIZE/(int32_t)LDBKS,
				      //(int32_t)MAX_LINKERS_IN_TERMLIST,
				      // how many docids we read from disk
				      //(m_list.getListSize()-6)/12 ,
				      //(int32_t)MAX_DOCIDS_TO_SAMPLE);
				      m_cblocks,
				      m_uniqueIps
				      );

	if ( m_mode == MODE_SITELINKINFO && m_printInXml )
		m_pbuf->safePrintf("\t<siteRank>%" INT32 "</siteRank>\n" , siteRank );

	// print link spam types
	int32_t ns = m_table.getNumSlots();
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// skip empty slots
		if ( m_table.isEmpty(i) ) continue;
		// who is in this slot
		NoteEntry *e = *(NoteEntry **)m_table.getValueFromSlot(i);
		char *exp = getExplanation ( e->m_note );
		// show it
		if ( m_printInXml ) {
			m_pbuf->safePrintf ( "\t<inlinkStat>\n");
			m_pbuf->safePrintf ( "\t\t<name><![CDATA[" );
			m_pbuf->safeStrcpy ( e->m_note );
			m_pbuf->safePrintf ( "]]></name>\n");
			if ( exp )
				m_pbuf->safePrintf ( "\t\t<explanation>"
						     "<![CDATA[%s]]>"
						     "</explanation>\n",
						     exp);
			m_pbuf->safePrintf ( "\t\t<count>%" INT32 "</count>\n",
					     e->m_count );
			m_pbuf->safePrintf ( "\t</inlinkStat>\n");
		}
		else {
			m_pbuf->safePrintf ( "<tr><td>%s", e->m_note );
			m_pbuf->safePrintf("</td>");
			m_pbuf->safePrintf (
					    "<td><font color=red>%" INT32 "</font>"
					    "</td>"
					    "<td>reason could not vote</td>"
					    "<td>"
					    , e->m_count );
		}
		// print some docids that had this problem
		for ( int32_t j = 0 ; j < MAX_ENTRY_DOCIDS ; j++ ) {
			if ( e->m_docIds[j] == -1LL ) break;
			if ( ! m_printInXml )
				m_pbuf->safePrintf ("<a href=\"/admin/titledb"
						    "?c=%s&d=%" INT64 "\">"
						    "%" INT32 "</a> ",
						    coll,e->m_docIds[j],j);
		}
		if ( ! m_printInXml )
			m_pbuf->safePrintf ( "&nbsp; </td></tr>\n" );
	}
		
	if ( ! m_printInXml ) 
		m_pbuf->safePrintf(  

				   "<tr>"
				   "<td>ip dup</td>"
				   "<td><font color=red>%" INT32 "</font></td>"
				   "<td>"
				   "basically the same ip, but we looked "
				   "it up anyway."
				   "</td>"
				   "</tr>\n"

				   "<tr>"
				   "<td>ip dup linkdb</td>"
				   "<td>%" INT32 "</td>"
				   "<td>"
				   "linkdb saved us from having to "
				   "look up this many title recs from the "
				   "same ip C block.</td>"
				   "</tr>\n"

				   "<tr>"
				   "<td>docid dup linkdb</td>"
				   "<td>%" INT32 "</td>"
				   "<td>"
				   "linkdb saved us from having to "
				   "look up this many title recs from the "
				   "same docid.</td>"
				   "</tr>\n"
				   
				   "<tr>"
				   "<td>link spam linkdb</td>"
				   "<td>%" INT32 "</td>"
				   "<td>"
				   "linkdb saved us from having to "
				   "look up this many title recs because "
				   "they were pre-identified as link "
				   "spam.</td>"
				   "</tr>\n"
				   
				   "<tr>"
				   "<td><b>good</b></td>"
				   "<td><b>%" INT32 "</b></td>"
				   "<td>"
				   //"may include anomalies and some "
				   //"link farms discovered later. "
				   "# inlinkers with positive weight"
				   //"limited to MAX_LINKERS = %" INT32 ""
				   "</td>"
				   "<td> &nbsp; </td>"
				   "</tr>\n"
				   
				   ,
				   (int32_t)m_ipDups   ,
				   (int32_t)m_ipDupsLinkdb   ,
				   (int32_t)m_docIdDupsLinkdb   ,
				   (int32_t)m_linkSpamLinkdb ,
				   info->m_numGoodInlinks
				   );

	if ( m_mode == MODE_SITELINKINFO && ! m_printInXml )
		m_pbuf->safePrintf(
				   "<tr><td><b>siterank</b></td>"
				   "<td><b>%" INT32 "</b></td>"
				   "<td>based on # of good inlinks</td>"
				   "</tr>",
				   siteRank
				   );

	if ( ! m_printInXml )
		m_pbuf->safePrintf("</table>"
				   "<br>"
				   "<br>" );

	// xml?
	if ( m_printInXml && m_ipDups ) {
		// ip dups
		m_pbuf->safePrintf ( "\t<inlinkStat>\n"
				     "\t\t<name><![CDATA[");
		m_pbuf->safePrintf ( "duplicateIPCClass" );
		m_pbuf->safePrintf ( "]]></name>\n");

		m_pbuf->safePrintf ( "\t\t<explanation><![CDATA[");
		m_pbuf->safePrintf ( "inlinker is form the same C Block "
				     "as another inlinker we processed");
		m_pbuf->safePrintf ( "]]></explanation>\n");

		m_pbuf->safePrintf ( "\t\t<count>%" INT32 "</count>\n",
				     m_ipDups );
		m_pbuf->safePrintf ( "\t</inlinkStat>\n");
	}
	if ( m_printInXml && m_ipDupsLinkdb ) {
		// ip dups
		m_pbuf->safePrintf ( "\t<inlinkStat>\n"
				     "\t\t<name><![CDATA[");
		m_pbuf->safePrintf ( "duplicateIPCClass" );
		m_pbuf->safePrintf ( "]]></name>\n");
		m_pbuf->safePrintf ( "\t\t<explanation><![CDATA[");
		m_pbuf->safePrintf ( "inlinker is form the same C Block "
				     "as another inlinker we processed");
		m_pbuf->safePrintf ( "]]></explanation>\n");

		m_pbuf->safePrintf ( "\t\t<count>%" INT32 "</count>\n",
				     m_ipDupsLinkdb );
		m_pbuf->safePrintf ( "\t</inlinkStat>\n");
	}
	if ( m_printInXml && m_docIdDupsLinkdb ) {
		// ip dups
		m_pbuf->safePrintf ( "\t<inlinkStat>\n"
				     "\t\t<name><![CDATA[");
		m_pbuf->safePrintf ( "duplicateDocId" );
		m_pbuf->safePrintf ( "]]></name>\n");
		m_pbuf->safePrintf ( "\t\t<explanation><![CDATA[");
		m_pbuf->safePrintf ( "inlinker is on the same page "
				     "as another inlinker we processed");
		m_pbuf->safePrintf ( "]]></explanation>\n");
		m_pbuf->safePrintf ( "\t\t<count>%" INT32 "</count>\n",
				     m_ipDupsLinkdb );
		m_pbuf->safePrintf ( "\t</inlinkStat>\n");
	}
	if ( m_printInXml && m_linkSpamLinkdb ) {
		// link spam
		m_pbuf->safePrintf ( "\t<inlinkStat>\n"
				     "\t\t<name><![CDATA[");
		m_pbuf->safePrintf ( "generalLinkSpam" );
		m_pbuf->safePrintf ( "]]></name>\n");
		m_pbuf->safePrintf ( "\t\t<count>%" INT32 "</count>\n",
				     m_linkSpamLinkdb );
		m_pbuf->safePrintf ( "\t</inlinkStat>\n");
	}


	char *tt = "";
	if ( m_mode == MODE_SITELINKINFO ) tt = "site ";

	if ( ! m_printInXml ) {
		m_pbuf->safePrintf(  "<table cellpadding=3 border=1>"
				     "<tr>"
				     "<td colspan=20>Inlinks "
				     "to %s%s &nbsp; (IP=%s) "
				     "</td>"
				     "</tr>\n"
				     "<tr>"
				     "<td>#</td>"
				     "<td>docId</td>"
				     "<td>note</td>"
				     "<td>url</td>"
				     "<td>site</td>"
				     "<td>title</td>"
				     "<td>IP</td>"
				     "<td>firstIP</td>"
				     "<td>lang</td>"
				     "<td>discovered</td>"
				     "<td>pubdate</td>"
				     "<td>hop count</td>"
				     "<td>site rank</td>"
				     "<td># words in link text</td>"
				     "<td>link text bytes</td>"
				     "<td>link text</td>",
				     tt,m_url,iptoa(m_ip));
		if ( m_mode == MODE_SITELINKINFO )
			m_pbuf->safePrintf("<td>link url</td>");
		m_pbuf->safePrintf("<td>neighborhood</td>"
				   "</tr>\n" );
	}

	for ( int32_t i = 0 ; i < m_numReplyPtrs ; i++ ) {
		// point to a reply
		Msg20Reply *r = m_replyPtrs[i];
		// are we internal
		bool internal = false;
		if ( (r->m_ip&0x0000ffff) == (m_ip&0x0000ffff) ) 
			internal = true;
		if ( (r->m_firstIp&0x0000ffff) == (m_ip&0x0000ffff))
			internal = true;
		// are we an "anomaly"?
		char *note = r->ptr_note;
		if ( r->m_isLinkSpam && !note )
			note = "unknown";
		// print the link text itself
		char *txt  = r->ptr_linkText;
		// get length of link text
		int32_t tlen = r->size_linkText;
		if ( tlen > 0 ) tlen--;
		// datedb date
		char dbuf[128];
		if ( r->m_datedbDate > 1 ) {
			time_t ttt = (time_t)r->m_datedbDate;
			sprintf(dbuf,"%s UTC",
				asctime (gmtime( &ttt ))  );
		}
		else 
			sprintf(dbuf,"---");

		char discBuf[128];
		time_t dd = (time_t)r->m_discoveryDate;
		if ( dd ) {
			struct tm *timeStruct = gmtime ( &dd );
			if ( timeStruct )
				strftime ( discBuf, 128 , 
					   "<nobr>%b %d %Y</nobr>" , 
					   timeStruct);
			else
				sprintf(discBuf,"UNKNOWN DATE");
		}
		else 
			sprintf(discBuf,"---");

		char *title = r->ptr_tbuf;
		if ( ! title ) title = "";
		
		// show the linking docid, the its weight
		if ( m_printInXml ) {
			char *ns = note;
			if ( ! note ) ns = "good";
			m_pbuf->safePrintf("\t<inlink>\n"

					   "\t\t<docId>%" INT64 "</docId>\n"

					   "\t\t<url><![CDATA[%s]]></url>\n"

					   "\t\t<site><![CDATA[%s]]></site>\n"

					  "\t\t<title><![CDATA[%s]]></title>\n"

					   //"\t\t<weight>%.01f</weight>\n"

					   "\t\t<note><![CDATA[%s]]>"
					   "</note>\n"

					   , r->m_docId
					   , r->ptr_ubuf
					   , r->ptr_site
					   , title
					   //, weight
					   , ns
					   );

			// get explanation of note
			char *exp = getExplanation ( ns );
			if ( exp )
				m_pbuf->safePrintf("\t\t<explanation>"
						   "<![CDATA[%s]]>"
						   "</explanation>\n"
						   , exp );

			m_pbuf->safePrintf("\t\t<ipAddress>"
					   "<![CDATA[%s]]>"
					   "</ipAddress>\n"
					   ,iptoa(r->m_ip) );

			m_pbuf->safePrintf("\t\t<firstIpAddress>"
					   "<![CDATA[%s]]>"
					   "</firstIpAddress>\n"
					   ,iptoa(r->m_firstIp) );

			m_pbuf->safePrintf(

					   "\t\t<onSite>%" INT32 "</onSite>\n"

					   "\t\t<discoveryDateUTC>%" UINT32 ""
					   "</discoveryDateUTC>\n"

					   "\t\t<language><![CDATA[%s]]>"
					   "</language>\n"

					   "\t\t<siteRank>%" INT32 "</siteRank>\n"
					   , (int32_t)internal
					   , (uint32_t)dd
					   , getLanguageString(r->m_language)
					   , (int32_t)r->m_siteRank
					   );
			m_pbuf->safePrintf("\t\t<linkText><![CDATA[");
			m_pbuf->htmlEncode ( txt,tlen,0);
			m_pbuf->safePrintf("]]>"
					   "</linkText>\n"
					   );
			if ( m_mode == MODE_SITELINKINFO )
				m_pbuf->safePrintf("\t<linkUrl><![CDATA[%s]]>"
						   "</linkUrl>\n",
						   r->ptr_linkUrl);
			m_pbuf->safePrintf(
					   "\t</inlink>\n"
					   );
			continue;
		}

		m_pbuf->safePrintf(  "<tr>"
				     "<td>%" INT32 "</td>" // #i
				     "<td>"
				     "<a href=\"/print?page=1&d=%" INT64 "\">"
				     "%" INT64 "</a></td>" // docid
				     "<td><nobr>"//%.1f"
				     ,i+1
				     ,r->m_docId
				     ,r->m_docId
				     //,weight
				     );
		if ( note )
			m_pbuf->safePrintf("%s", note );
		else
			m_pbuf->safePrintf("<b>good</b>");

		m_pbuf->safePrintf(  "</nobr></td>"//wghtnte
				     "<td>%s</td>" // url
				     "<td>%s</td>" // site
				     "<td>%s</td>", // title
				     r->ptr_ubuf,
				     r->ptr_site,
				     title);
		m_pbuf->safePrintf("<td><a href=\"/search?q=ip%%3A"
				   "%s&c=%s&n=200\">%s</a></td>"  // ip
				   , iptoa(r->m_ip)
				   , coll
				   , iptoa(r->m_ip)
				   );
		m_pbuf->safePrintf("<td>%s</td>"
				   , iptoa(r->m_firstIp)
				   );
		m_pbuf->safePrintf(  //"<td>%s</td>"   // external
				     "<td>%s</td>"   // language
				     "<td>%s</td>"   // discoverydate
				     "<td>%s</td>"   // datedbdate
				     "<td>%" INT32 "</td>" // hopcount
				     "<td><font color=red><b>%" INT32 ""
				     "</b></font></td>"  // site rank
				     "<td>%" INT32 "</td>"  // nw 
				     "<td>%" INT32 "</td>" // textLen
				     "<td><nobr>", // text
				     //ext,
				     getLanguageString(r->m_language),
				     discBuf,
				     dbuf,
				     (int32_t)r->m_hopcount,
				     (int32_t)r->m_siteRank, // docQuality,
				     (int32_t)r->m_linkTextNumWords ,
				     tlen );
		// only bold if good
		if ( ! note )
			m_pbuf->safePrintf("<b>");
		// this is in utf8 already
		m_pbuf->safeMemcpy ( txt , tlen );
		// only bold if good
		if ( ! note )
			m_pbuf->safePrintf("</b>");
		// wrap it up
		m_pbuf->safePrintf("</nobr></td>");
		// print url that is linked to in the case of site inlinks
		if ( m_mode == MODE_SITELINKINFO )
			m_pbuf->safePrintf("<td>%s</td>",r->ptr_linkUrl);
		// print the neighborhood
		m_pbuf->safePrintf("<td>");
		txt  = r->ptr_surroundingText;
		tlen = r->size_surroundingText - 1;
		if(!txt) {
			m_pbuf->safePrintf("--\n");
			m_pbuf->safePrintf("</td></tr>\n");
			continue;
		}
		// this is utf8 already
		m_pbuf->safeMemcpy ( txt , tlen );
		m_pbuf->safePrintf("</td></tr>\n");
	}

	if ( ! m_printInXml ) {
		m_pbuf->safePrintf(  "</table>\n<br>\n" );
		m_pbuf->safePrintf(  "</td></table>\n<br><br>\n" );
	}

	// print site rank
	if ( m_printInXml && m_mode == MODE_SITELINKINFO )
		m_pbuf->safePrintf("\t<siteRankTable>\n");

	if ( ! m_printInXml && m_mode == MODE_SITELINKINFO )
		m_pbuf->safePrintf("<table border=1>"
				   "<tr><td colspan=2>"
				   "<center>siteRankTable</center>"
				   "</td></tr>"
				   "<tr><td># good inlinks</td>"
				   "<td>siteRank</td></tr>"
				   );

	// print site rank table
	int32_t lastsr = -1;
	for ( int32_t i = 0 ; i < 11000 && m_mode == MODE_SITELINKINFO ; i++ ) {
		int32_t sr = ::getSiteRank ( i );
		if ( sr == lastsr ) continue;
		lastsr = sr;
		if ( m_printInXml )
			m_pbuf->safePrintf("\t\t<row>"
					   "\t\t\t<numInlinks>%" INT32 ""
					   "</numInlinks>\n"
					   "\t\t\t<siteRank>%" INT32 ""
					   "</siteRank>\n"
					   "\t\t</row>\n"
					   ,i,sr);
		else
			m_pbuf->safePrintf("<tr><td>%" INT32 "</td><td>%" INT32 "</td></tr>"
					   ,i,sr);
	}
	if ( m_printInXml && m_mode == MODE_SITELINKINFO )
		m_pbuf->safePrintf("\t</siteRankTable>\n");
	else if ( m_mode == MODE_SITELINKINFO )
		m_pbuf->safePrintf("</table>"
				   "<br>" );

	return true;
}

// . return the "worst" of the two Msg20Replies
// . in the case of a dup, we use this to determine which one is kicked out
// . if one is NULL, return the other
Msg20Reply *Msg25::getLoser ( Msg20Reply *r , Msg20Reply *p ) {
	if ( ! p ) return r;
	if ( ! r ) return p;
	// if "r" is internal, but p is not, r is the loser
	bool rinternal = false;
	bool pinternal = false;
	if ( iptop(r->m_ip     ) == m_top ) rinternal = true;
	if ( iptop(r->m_firstIp) == m_top ) rinternal = true;
	if ( iptop(p->m_ip     ) == m_top ) pinternal = true;
	if ( iptop(p->m_firstIp) == m_top ) pinternal = true;
	if ( rinternal && ! pinternal )
		return r;
	// and vice versa
	if ( pinternal && ! rinternal )
		return p;
	if ( p->m_siteRank < r->m_siteRank ) return p;
	if ( r->m_siteRank < p->m_siteRank ) return r;
	// . if they had the same quality... check docid
	// . the lower the docid, the "better" it is. this behavior
	//   is opposite of the quality behavior.
	if ( p->m_docId > r->m_docId ) return p;
	// fall back to r then
	return r;
}


// . is "p" a dup of "r"?
// . we will kick out the worst one so it cannot vote
// . returns NULL if not a dup
// . returns NULL with g_errno set on error
char *Msg25::isDup ( Msg20Reply *r , Msg20Reply *p ) {

	// reset this
	g_errno = 0;

	// do ip tops match? (top 2 bytes of ip addresses)
	bool internal = false;
	if ( iptop(p->m_ip) == m_top ) internal = true;
	if ( iptop(p->m_firstIp) == m_top ) internal = true;
	if ( internal && m_oneVotePerIpDom )
		return "ip dup";

	// only one ad id
	if ( r->m_adIdHash && r->m_adIdHash == p->m_adIdHash )
		return "same ad id";

	// see if he is too similar to another, if so he is not a good voter
	int32_t *v1 = (int32_t *)r->ptr_vector1;
	int32_t *v2 = (int32_t *)r->ptr_vector2;
	int32_t *v3 = (int32_t *)r->ptr_vector3;

	int32_t nv1 = r->size_vector1 / 4;
	int32_t nv2 = r->size_vector2 / 4;
	int32_t nv3 = r->size_vector3 / 4;

	// get vectors for Msg20Reply "p"
	int32_t *x1 = (int32_t *)p->ptr_vector1;
	int32_t *x2 = (int32_t *)p->ptr_vector2;
	int32_t *x3 = (int32_t *)p->ptr_vector3;

	int32_t nx1 = p->size_vector1 / 4;
	int32_t nx2 = p->size_vector2 / 4;
	int32_t nx3 = p->size_vector3 / 4;

	// these count the terminating 0 int32_t as a component
	if ( v1 && x1 && nv1 >= 2 && nx1 >= 2 ) {
		//p1 = (int32_t)computeSimilarity (v1,x1,NULL,NULL,NULL,ni);
		// are these two vecs 80% or more similar?
		if ( isSimilar_sorted (v1,x1,nv1,nx1,80,m_niceness) ) {
			return "similar content";
		}
	}

	// only consider p2 if each vector is beefy. these
	// can be small because these vectors represent the
	// word pairs just to the right of the link in the
	// content, and before any "breaking" tag thereafter.
	// no, there are too many little ads, so disregard the beeft
	// requirement.
	if ( v2 && x2 && nv2 >= 2 && nx2 >= 2 ) {
		//p2 = (int32_t)computeSimilarity (v2,x2,NULL,NULL,NULL,ni);
		// are these two vecs 80% or more similar?
		if ( isSimilar_sorted (v2,x2,nv2,nx2,80,m_niceness) ) {
			return "similar link desc";
		}
	}

	// compare tag id pair vectors
	if ( v3 && x3 && nv3 >= 2 && nx3 >= 2 ) {
		//p3 = (int32_t)computeSimilarity (v3,x3,NULL,NULL,NULL,ni);
		// are these two vecs 80% or more similar?
		if ( isSimilar_sorted (v3,x3,nv3,nx3,100,m_niceness) ) {
			return "similar tag template";
		}
	}

	return NULL;
}

bool Msg25::addNote ( char *note , int32_t noteLen , int64_t docId ) {
	// return right away if no note
	if ( ! note || noteLen <= 0 ) return true;
	// get hash
	int32_t  h          = hash32 ( note , noteLen );
	NoteEntry **pentry = (NoteEntry **)m_table.getValue ( &h );
	// if this "type" has not been recorded, add it
	if ( ! pentry && h ) {
		char *p = m_bufPtr;
		if ( p + sizeof(NoteEntry) + noteLen + 1 >= m_bufEnd ) {
			log("build: increase buf size in Msg25.");
			gbassert(false);
		}
		// store the entry
		NoteEntry *e = (NoteEntry *)p;
		p += sizeof(NoteEntry);
		// init that entry
		e->m_count     =  1;
		e->m_note      =  p;
		e->m_docIds[0] =  docId;
		e->m_docIds[1] = -1LL;
		// store note into the buffer, NULL terminated
		gbmemcpy ( p , note , noteLen ); p += noteLen;
		*p++ = '\0';
		// add to the table
		int32_t slot = -1;
		if ( ! m_table.addKey(&h,&e,&slot))
			log("build: Msg25 could not add note.");
		// did we add successfully?
		if ( slot < 0 ) return false;
		// advance to next spot
		m_bufPtr = p;
		return true;
	}
	// cast it
	NoteEntry *entry = *pentry;
	// get the count
	entry->m_count++;
	// add docids to the list
	for ( int32_t i = 0 ; i < MAX_ENTRY_DOCIDS ; i++ ) {
		// skip if not empty
		if ( entry->m_docIds[i] != -1LL ) continue;
		// take it over, its empty if it is -1LL
		entry->m_docIds[i] = docId;
		// next one should be -1 now
		if ( i + 1 < MAX_ENTRY_DOCIDS ) entry->m_docIds[i+1] = -1LL;
		// all done
		break;
	}
	return true;
}

//////////
//
// LINKINFO
//
//////////

#include "HashTableX.h"
#include "Words.h"
#include "Msge0.h"

#define MAX_LINKERS          3000
#define MAX_INTERNAL_INLINKS 10

// . after Msg25 calls LinkInfo::set() (was merge()) make it call
//   Msg25::print(SafeBuf *pbuf) to print the stats. it can access
//   LinkInfo's Inlinks to get their weights, etc.
// . returns the LinkInfo on success
// . returns NULL and sets g_errno on error
LinkInfo *makeLinkInfo ( char        *coll                    ,
			 int32_t         ip                      ,
			 int32_t         siteNumInlinks          ,
			 Msg20Reply **replies                 ,
			 int32_t         numReplies              ,
			 // if link spam give this weight
			 int32_t         spamWeight              ,
			 bool         oneVotePerIpTop         ,
			 int64_t    linkeeDocId             ,
			 int32_t         lastUpdateTime          ,
			 bool         onlyNeedGoodInlinks      ,
			 int32_t         niceness                ,
			 Msg25 *msg25 ,
			 SafeBuf *linkInfoBuf ) {

	// for parsing the link text
	Words words;
	// a table for counting words per link text
	HashTableX tt;
	// buf for tt
	char ttbuf[2048];
	// must init it!
	tt.set ( 8 ,4,128,ttbuf,2048,false,niceness,"linknfo");
	// how many internal linkers do we have?
	int32_t icount = 0;
	// we are currently only sampling from 10
	for ( int32_t i = 0 ; i < numReplies ; i++ ) {
		// replies are NULL if MsgE had an error, like ENOTFOUND
		if ( ! replies[i] ) continue;
		bool internal = false;
		if ( (replies[i]->m_ip&0x0000ffff) == (ip&0x0000ffff) )
			internal = true;
		if ( (replies[i]->m_firstIp&0x0000ffff) == (ip&0x0000ffff) )
			internal = true;
		if ( internal )
			icount++;
	}

	// we can estimate our quality here
	int32_t numGoodInlinks = 0;

	// set m_linkTextNumWords
	for ( int32_t i = 0 ; i < numReplies ; i++ ) {
		// get the reply
		Msg20Reply *r = replies[i];
		// replies are NULL if MsgE had an error, like ENOTFOUND
		if ( ! r ) continue;
		// get the link text itself
		char *txt    = r->ptr_linkText;
		int32_t  txtLen = r->size_linkText;
		// discount terminating \0
		if ( txtLen > 0 ) txtLen--; 
		// get approx # of words in link text
		int32_t nw = 0;
		if ( txtLen > 0 )
			nw = getNumWords(txt,txtLen,TITLEREC_CURRENT_VERSION);
		// store it
		r->m_linkTextNumWords = nw;
		
		// linkspam?
		if ( r->ptr_note ) {
			r->m_isLinkSpam = true;
			continue;
		}

		bool internal = false;
		if ((r->m_ip&0x0000ffff) == (ip & 0x0000ffff)) 
			internal = true;
		if ((r->m_firstIp&0x0000ffff) == (ip & 0x0000ffff)) 
			internal = true;

		// if its internal do not count towards good, but do
		// indeed store it!
		if ( internal ) continue;

		// otherwise count as good
		numGoodInlinks++;
	}

	int32_t count = 0;
	// . now just store the Inlinks whose weights are > 0
	// . how much space do we need?
	int32_t need = 0;
	// how much space do we need?
	for ( int32_t i = 0 ; i < numReplies ; i++ ) {
		// get the reply
		Msg20Reply *r = replies[i];
		// replies are NULL if MsgE had an error, like ENOTFOUND
		if ( ! r ) continue;
		// ignore if spam
		if ( r->m_isLinkSpam ) {
			// linkdb debug
			if ( g_conf.m_logDebugLinkInfo ) 
				log("linkdb: inlink #%" INT32 " is link spam: %s",
				    i,r->ptr_note);
			if ( onlyNeedGoodInlinks )
				continue;
		}
		// do a quick set
		Inlink k; k.set ( r );
		// get space
		need += k.getStoredSize ( );
		// count it
		count++;
	}

	// we need space for our header
	need += sizeof(LinkInfo);
	if ( ! linkInfoBuf->reserve ( need , "LinkInfo" ) ) return NULL;
	// set ourselves to this new buffer
	LinkInfo *info = (LinkInfo *)(linkInfoBuf->getBufStart());
	
	// set our header
	info->m_version                = 0;
	info->m_lisize                 = need;
	info->m_lastUpdated            = lastUpdateTime;//getTimeGlobal();
	// how many Inlinks we stored in info->m_buf[]
	info->m_numStoredInlinks       = count;
	// the gross total of inlinks we got, both internal and external
	info->m_totalInlinkingDocIds   = msg25->m_numDocIds;
	// . only valid if titlerec version >= 119
	// . how many unique c blocks link to us?
	// . includes your own internal c block
	info->m_numUniqueCBlocks       = msg25->m_cblocks;
	// . only valid if titlerec version >= 119
	// . how many unique ips link to us?
	// . this count includes internal IPs as well
	info->m_numUniqueIps           = msg25->m_uniqueIps;
	// keep things consistent for the "qatest123" coll
	info->m_reserved1              = 0;
	info->m_reserved2              = 0;
	// how many total GOOD inlinks we got. does not include internal cblock
	info->m_numGoodInlinks  = numGoodInlinks;

	// point to our buf
	char *p    = info->m_buf;
	char *pend = linkInfoBuf->getBufStart() + need;
	// count the ones we store that are internal
	int32_t  icount3 = 0;
	// now set each inlink
	for ( int32_t i = 0 ; i < numReplies ; i++ ) {
		// get the reply
		Msg20Reply *r = replies[i];
		// replies are NULL if MsgE had an error, like ENOTFOUND
		if ( ! r ) continue;
		// skip weights 0 or less
		//if ( r->m_linkTextScoreWeight <= 0 ) continue;
		// ignore if spam
		if ( r->m_isLinkSpam && onlyNeedGoodInlinks ) continue;
		// are we internal?
		bool internal = false;
		if ( (r->m_ip&0x0000ffff) == (ip & 0x0000ffff) ) 
			internal = true;
		if ( (r->m_firstIp&0x0000ffff) == (ip & 0x0000ffff) )
			internal = true;
		if ( internal ) icount3++;
		// set the Inlink
		Inlink k;
		// store it. our ptrs will reference into the Msg20Reply buf
		k.set ( r );
		// . this will copy itself into "p"
		// . "true" --> makePtrsRefNewBuf
		int32_t wrote;
		char *s = k.serialize ( &wrote , p , pend - p , true );
		// sanity check
		gbassert_false( s != p );
		// sanity check
		gbassert_false( k.getStoredSize() != wrote );
		// note it if recycled
		if ( k.m_recycled )
			logf(LOG_DEBUG,"build: recycling Inlink %s for linkee "
			     "%" INT64 "", k.getUrl(),linkeeDocId);
		// advance
		p += wrote;
	}
	// . sanity check, should have used up all the buf exactly
	// . so we can free the buf with k->getStoredSize() being the allocSize
	gbassert_false( p != pend );

	// how many guys that we stored were internal?
	info->m_numInlinksInternal = (char)icount3;

	linkInfoBuf->setLength ( need );

	// success
	return info;
}

Inlink *LinkInfo::getNextInlink ( Inlink *k ) {
	// if none, return NULL
	if ( m_numStoredInlinks == 0 ) return NULL;
	// if k is NULL, return the first
	if ( ! k ) {
		// set it to the first one
		k = (Inlink *)m_buf;
		// done
		return k;
	}
	// point to next
	int32_t size = k->getStoredSize();
	// get the inlink to return
	Inlink *next = (Inlink *)((char *)k + size);
	// return NULL if breached
	int64_t x = (char *)next - (char *)this;
	// was that the end of them?
	if ( x >= m_lisize ) return NULL;
	// otherwise, we are still good
	return next;
}

// . returns false and sets g_errno on error
// . returns true if no error was encountered
// . call xml->isEmpty() to see if you got anything
bool LinkInfo::getItemXml ( Xml *xml , int32_t niceness ) {
	// reset it
	xml->reset();
	// loop through the Inlinks
	Inlink *k = NULL;
	for ( ; (k = getNextInlink(k)) ; ) {
		// does it have an xml item? skip if not.
		if ( k->size_rssItem <= 1 ) continue;
		// got it
		break;
	}
	// return if nada
	if ( ! k ) return true;
	// set the xml
	return k->setXmlFromRSS ( xml , niceness );
}

bool Inlink::setXmlFromRSS ( Xml *xml , int32_t niceness ) {
	// compute the length (excludes the \0's)
	int32_t len = size_rssItem - 1;
	// return false and set g_errno if this fails
	return xml->set ( getRSSItem(),//ptr_rssItem              ,
			  len                      ,
			  false                    , // own data?
			  0                        , // allocSize
			  true                     , // pure xml?
			  TITLEREC_CURRENT_VERSION ,
			  false                    , // no need to now
			  niceness                 ,
			  CT_XML );
}

bool LinkInfo::hasLinkText ( ) {
	// loop through the Inlinks
	for ( Inlink *k = NULL; (k = getNextInlink(k)) ; ) 
		if ( k->size_linkText > 1 ) return true;
	return false;
}

void Inlink::set ( Msg20Reply *r ) {

	// set ourselves now
	m_ip                 = r->m_ip;
	m_firstIp            = r->m_firstIp;
	m_wordPosStart       = r->m_wordPosStart;
	m_docId              = r->m_docId;
	m_firstSpidered      = r->m_firstSpidered;
	m_lastSpidered       = r->m_lastSpidered;
	m_datedbDate         = r->m_datedbDate;
	m_firstIndexedDate   = r->m_firstIndexedDate;
	m_numOutlinks        = r->m_numOutlinks;

	m_isPermalink        = r->m_isPermalink;
	m_outlinkInContent   = r->m_outlinkInContent;
	m_outlinkInComment   = r->m_outlinkInComment;
	m_isLinkSpam         = r->m_isLinkSpam;
	m_hasAllQueryTerms   = r->m_hasAllQueryTerms;
	m_recycled           = r->m_recycled;

	m_country             = r->m_country;
	m_language            = r->m_language;
	m_siteRank            = r->m_siteRank;
	m_hopcount            = r->m_hopcount;

	// MDW: use a new way. construct m_buf. 64-bit stuff.
	int32_t poff = 0;
	char *p = m_buf;

	int32_t need = 
		r->size_ubuf + 
		r->size_linkText +
		r->size_surroundingText +
		r->size_rssItem +
		r->size_categories +
		r->size_gigabitQuery +
		r->size_templateVector;

	char *pend = p + need;
	// -10 to add \0's for remaining guys in case of breach
	pend -= 10;


	size_urlBuf           = r->size_ubuf;
	size_linkText         = r->size_linkText;
	size_surroundingText  = r->size_surroundingText;
	size_rssItem          = r->size_rssItem;
	size_categories       = r->size_categories;
	size_gigabitQuery     = r->size_gigabitQuery;
	size_templateVector   = r->size_templateVector;


	/////////////

	off_urlBuf = poff;
	gbmemcpy ( p , r->ptr_ubuf , size_urlBuf );
	poff += size_urlBuf;
	p    += size_urlBuf;

	/////////////

	off_linkText = poff;
	gbmemcpy ( p , r->ptr_linkText , size_linkText );
	poff += size_linkText;
	p    += size_linkText;

	/////////////

	off_surroundingText = poff;
	if ( p + r->size_surroundingText < pend ) {
		gbmemcpy (p,r->ptr_surroundingText , size_surroundingText );
	}
	else {
		size_surroundingText = 1;
		*p = '\0';
	}
	poff += size_surroundingText;
	p    += size_surroundingText;

	/////////////

	off_rssItem = poff;
	if ( p + r->size_rssItem < pend ) {
		gbmemcpy ( p , r->ptr_rssItem , size_rssItem );
	}
	else {
		size_rssItem = 1;
		*p = '\0';
	}
	poff += size_rssItem;
	p    += size_rssItem;

	/////////////

	off_categories = poff;
	if ( p + r->size_categories < pend ) {
		gbmemcpy ( p , r->ptr_categories , size_categories );
	}
	else {
		size_categories = 1;
		*p = '\0';
	}
	poff += size_categories;
	p    += size_categories;

	/////////////
	
	off_gigabitQuery = poff;
	if ( p + r->size_gigabitQuery < pend ) {
		gbmemcpy ( p , r->ptr_gigabitQuery , size_gigabitQuery );
	}
	else {
		size_gigabitQuery = 1;
		*p = '\0';
	}
	poff += size_gigabitQuery;
	p    += size_gigabitQuery;

	/////////////

	off_templateVector = poff;
	if ( p + r->size_templateVector < pend ) {
		gbmemcpy (p , r->ptr_templateVector , size_templateVector );
	}
	else {
		size_templateVector = 1;
		*p = '\0';
	}
	poff += size_templateVector;
	p    += size_templateVector;
}

// Msg25 calls this to make a "fake" msg20 reply for recycling Inlinks
// that are no longer there... preserves rssInfo, etc.
void Inlink::setMsg20Reply ( Msg20Reply *r ) {

	r->m_ip                  = m_ip;
	r->m_firstIp             = m_firstIp;
	r->m_wordPosStart        = m_wordPosStart;
	r->m_docId               = m_docId;
	r->m_firstSpidered       = m_firstSpidered;
	
	r->m_lastSpidered        = m_lastSpidered;
	r->m_datedbDate          = m_datedbDate;
	r->m_firstIndexedDate    = m_firstIndexedDate;
	r->m_numOutlinks         = m_numOutlinks;

	r->m_isPermalink         = m_isPermalink;
	r->m_outlinkInContent    = m_outlinkInContent;
	r->m_outlinkInComment    = m_outlinkInComment;
	
	r->m_isLinkSpam          = m_isLinkSpam;
	r->m_hasAllQueryTerms    = m_hasAllQueryTerms;
	
	r->m_country             = m_country;
	r->m_language            = m_language;
	r->m_siteRank            = m_siteRank;
	r->m_hopcount            = m_hopcount;

	r->ptr_ubuf              = getUrl();//ptr_urlBuf;
	r->ptr_linkText          = getLinkText();//ptr_linkText;
	r->ptr_surroundingText   = getSurroundingText();//ptr_surroundingText;
	r->ptr_rssItem           = getRSSItem();//ptr_rssItem;
	r->ptr_categories        = getCategories();//ptr_categories;
	r->ptr_gigabitQuery      = getGigabitQuery();//ptr_gigabitQuery;
	r->ptr_templateVector    = getTemplateVector();//ptr_templateVector;
	
	r->size_ubuf             = size_urlBuf;
	r->size_linkText         = size_linkText;
	r->size_surroundingText  = size_surroundingText;
	r->size_rssItem          = size_rssItem;
	r->size_categories       = size_categories;
	r->size_gigabitQuery     = size_gigabitQuery;
	r->size_templateVector   = size_templateVector;
}

void Inlink::reset ( ) {
	// clear ourselves out
	memset ( (char *)this,0,sizeof(Inlink) - MAXINLINKSTRINGBUFSIZE);
}

// . set a new Inlink from an older versioned Inlink
// . this is how we handle versioning
void Inlink::set2 ( Inlink *old ) {
	// clear ouselves
	reset();

	int fullSize = old->getStoredSize();
	// return how many bytes we processed
	gbmemcpy ( (char *)this , (char *)old , fullSize );
}

int32_t Inlink::getStoredSize ( ) {
	int32_t size = sizeof(Inlink) -	MAXINLINKSTRINGBUFSIZE;

	size += size_urlBuf;
	size += size_linkText;
	size += size_surroundingText;
	size += size_rssItem;
	size += size_categories;
	size += size_gigabitQuery;
	size += size_templateVector;

	return size;
}

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *Inlink::serialize ( int32_t *retSize     ,
			  char *userBuf     ,
			  int32_t  userBufSize ,
			  bool  makePtrsRefNewBuf ) {
	// make a buffer to serialize into
	char *buf  = NULL;
	int32_t  need = getStoredSize();
	// big enough?
	if ( need <= userBufSize ) buf = userBuf;
	// alloc if we should
	if ( ! buf ) buf = (char *)mmalloc ( need , "Ra" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy the easy stuff
	char *p = buf;
	char *pend = buf + need;
	gbmemcpy ( p , (char *)this , need );
	p += need;

	gbassert_false( p != pend );

	return buf;
}

// used by PageTitledb.cpp
bool LinkInfo::print ( SafeBuf *sb , char *coll ) {
	int32_t count = 1;
	// loop through the link texts
	for ( Inlink *k = NULL; (k = getNextInlink(k)) ; count++ ) {
		char *s    = k->getLinkText();//ptr_linkText;
		int32_t  slen = k->size_linkText - 1;
		char *d    = k->getSurroundingText();//ptr_surroundingText;
		int32_t  dlen = k->size_surroundingText - 1;
		char *r    = k->getRSSItem();//ptr_rssItem;
		int32_t  rlen = k->size_rssItem - 1;
		char *g    = k->getGigabitQuery();//ptr_gigabitQuery;
		int32_t  glen = k->size_gigabitQuery - 1;
		char *c    = k->getCategories();//ptr_categories;
		int32_t  clen = k->size_categories - 1;
		if ( slen < 0 ) slen = 0;
		if ( dlen < 0 ) dlen = 0;
		if ( rlen < 0 ) rlen = 0;
		if ( glen < 0 ) glen = 0;
		if ( clen < 0 ) clen = 0;
		if ( ! c || clen <= 0 ) c = "";

		// encode the rss item further
		char buf3b[MAX_RSSITEM_SIZE*2];
		buf3b[0] = 0;
		if ( rlen > 0 ) 
			htmlEncode ( buf3b , 
				     buf3b + MAX_RSSITEM_SIZE*2 ,
				     r , // buf3 ,
				     r + rlen ,
				     true         );

		// . print link text and score into table
		// . there is a ton more info in Inlink class to print if
		//   you want to throw it in here...
		sb->safePrintf(
			       "<tr><td colspan=2>link #%04" INT32 " "
			       "("
			       "d=<a href=\"/admin/titledb?c=%s&"
			       "d=%" INT64 "\">%016" INT64 "</a>, "
			       "siterank=%" INT32 ", "
			       "hopcount=%03" INT32 " "
			       "outlinks=%05" INT32 ", "
			       "ip=%s "
			       "numLinksToSite=%" INT32 " "
			       "<b>url</b>=\"%s\" "
			       "<b>txt=</b>\""
			       "%s\" "
			       "<b>neigh=</b>\"%s\" "
			       "<b>rssItem</b>=\"%s\" "
			       "<b>gigabits</b>=\"%s\" "
			       "<b>categories</b>=\"%s\" "
			       "</td></tr>\n",
			       count , 
			       coll ,
			       k->m_docId,
			       k->m_docId,
			       (int32_t)k->m_siteRank,
			       (int32_t)k->m_hopcount,
			       (int32_t)k->m_numOutlinks ,
			       iptoa(k->m_ip),
			       (int32_t)k->m_siteNumInlinks,
			       k->getUrl(),//ptr_urlBuf, // the linker url
			       s, // buf,
			       d, // buf2,
			       buf3b,
			       g, // buf4,
			       c
			       );
	}
	return true;
}

bool LinkInfo::hasRSSItem() {
	for ( Inlink *k=NULL;(k=getNextInlink(k));) 
		// rss item?
		if ( k->size_rssItem > 10 ) return true;
	return false;
}

///////////////
//
// LINKS CLASS
//
///////////////

// . use siteRec to set m_extractRedirects from <extractRedirectsFromLinks>
// . this is used because yahoo has links like:
//   yahoo.com/drst/pop/2/10/576995/37640326/*http://www.m.com/
// . we need yahoo's link: terms to be more precise for our ban/unban algorithm

static int32_t getLinkBufferSize(int32_t numLinks);
Links::Links(){
	m_allocBuf = NULL;
	m_allocSize = 0;
	m_linkBuf  = NULL;
	m_allocLinks = 0;
	m_spamNote = NULL;
	m_numLinks = 0;
	m_baseUrl = NULL;
	m_numOutlinksAdded = 0;
}

Links::~Links() {
	reset();
}

void Links::reset() {
	if (m_allocBuf) mfree(m_allocBuf, m_allocSize, "Links");
	m_allocBuf = NULL;
	m_allocSize = 0;
	if (m_linkBuf) mfree(m_linkBuf, getLinkBufferSize(m_allocLinks), 
			     "Links");
	m_linkBuf = NULL;
	m_allocLinks = 0;
	m_spamNote = NULL;
	m_numLinks = 0;
	m_flagged = false;
	m_hasRSS  = false;
	m_isFeedBurner = false;
	m_hasSelfPermalink = false;
	m_hasRSSOutlink = false;
	m_hasSubdirOutlink = false;
	m_rssOutlinkPtr = NULL;
}

bool Links::set ( bool useRelNoFollow ,
		  Xml *xml , Url *parentUrl , bool setLinkHash ,
		  // use null for this if you do not want to use it
		  Url *baseUrl , 
		  int32_t version , 
		  int32_t niceness ,
		  bool parentIsPermalink ,
		  Links *oldLinks ,
		  bool doQuickSet ,
		  // some json from diffbot:
		  SafeBuf *diffbotReply ) {

	reset();

	// always for this to true now since we need them for linkdb
	setLinkHash = true;
	if ( doQuickSet ) setLinkHash = false;

	m_xml       = xml;
	m_baseUrl   = parentUrl;
	m_parentUrl = parentUrl;
	m_doQuickSet = doQuickSet;
	m_parentIsPermalink = parentIsPermalink;

	m_baseSite    = NULL;
	m_baseSiteLen = 0;

	m_numLinks = 0;
	m_numNodes = xml->getNumNodes();
	m_bufPtr   = NULL;

	m_linksToGigablast = false;
	m_hasRelNoFollow   = false;

	// this is not a good thing, don't strip here, but we did it for
	// version 51 so we have to keep doing it for version 51. we strip
	// the session id in Msg10.cpp in the addUrlLoop().
	m_stripIds = false;
	// ok, let's remove it for the links: hashing, it just makes more
	// sense this way i think. we can normalize the links: terms in the
	// query if you are worried about it.
	m_stripIds = true;

	// get the <base href=> tag if any (12)
	if ( baseUrl ) m_baseUrl = baseUrl;

	// get list of links from diffbot json reply
	char *p = NULL;
	if ( diffbotReply && diffbotReply->length() > 10 ) 
		p = strstr ( diffbotReply->getBufStart() , "\"links\":[\"" );
	// skip over the heading stuff
	if ( p ) p += 10;
	// parse out the links from diffbot reply
	for ( ; p ; ) {
		// must not be json mark up
		if ( ! *p || *p == ']' || *p == '\"' ) break;
		// save p
		char *start = p;
		// get length of the link
		for ( ; *p && *p != '\"' ; p++ );
		// set end of link
		char *end = p;
		// add the link
		if ( ! addLink ( start ,  // linkStr
				 end - start ,  // linkStrLen
				 -1, // i 
				 setLinkHash , 
				 TITLEREC_CURRENT_VERSION ,
				 niceness , 
				 false , // isRSS?
				 TAG_LINK , // node id -> LF_LINKTAG flag
				 0 )) // flags
			return false;
		// now advance to next link if any.
		for ( ; *p == '\"' || *p == ',' || is_wspace_a(*p) ; p++ );
	}
	

	// visit each node in the xml tree. a node can be a tag or a non-tag.
	char *urlattr = NULL;
	for ( int32_t i=0; i < m_numNodes ; i++ ) {
		QUICKPOLL(niceness);
		// . continue if this tag ain't an <a href> tag
		// . atom feeds have a <link href=""> field in them
		int32_t id = xml->getNodeId ( i );

		int32_t  slen;
		char *s ;
		// reset
		linkflags_t flags = 0;

		if ( id != TAG_A         &&
		     id != TAG_LINK      && // rss feed url
		     id != TAG_LOC       && // sitemap.xml url
		     id != TAG_AREA      &&
		     id != TAG_ENCLOSURE &&
		     id != TAG_WEBLOG    &&
		     id != TAG_URLFROM   && //  <UrlFrom> for ahrefs.com
		     id != TAG_FBORIGLINK )
			continue;

		//gotOne:

		urlattr = "href";
		if ( id == TAG_WEBLOG     ) urlattr ="url";
		if ( id == TAG_FBORIGLINK ) m_isFeedBurner = true;

		// if it's a back tag continue
		if ( xml->isBackTag ( i ) ) continue;
		// . if it has rel=nofollow then ignore it
		// . for old titleRecs we should skip this part so that the
		//   link: terms are indexed/hashed the same way in XmlDoc.cpp
		if ( useRelNoFollow ) s = xml->getString ( i , "rel", &slen ) ;
		if ( useRelNoFollow &&
		     slen==8 &&   // ASCII
		     strncasecmp ( s,"nofollow", 8 ) == 0) {
			// if this flag is set then::hasSpamLinks() will always
			// return false. the site owner is taking the necessary
			// precautions to prevent log spam.
			m_hasRelNoFollow = true;
			// . do not ignore it now, just flag it
			// . fandango has its ContactUs with a nofollow!
			flags |= LF_NOFOLLOW;
		}
		// get the href field of this anchor tag
		int32_t linkLen;
		char *link = (char *) xml->getString ( i, urlattr, &linkLen );
		// if no href, but we are a <link> tag then the url may
		// follow, like in an rss feed.
		if ( linkLen==0 && 
		     (id == TAG_LINK || 
		      id == TAG_LOC || // sitemap.xml urls
		      id == TAG_URLFROM ||
		      id == TAG_FBORIGLINK) ) {
			// the the <link> node
			char *node    = xml->getNode(i);
			int32_t  nodeLen = xml->getNodeLen(i);
			// but must NOT end in "/>" then
			if ( node[nodeLen-2] == '/' )  continue;
			// expect the url like <link> url </link> then
			if ( i+2 >= m_numNodes         ) continue;
			if ( xml->getNodeId(i+2) != id ) continue;
			if ( ! xml->isBackTag(i+2)     ) continue;
			// ok assume url is next node
			link    = xml->getNode(i+1);
			linkLen = xml->getNodeLen(i+1);
			// watch out for CDATA
			if ( linkLen > 12 &&
			     strncasecmp(link, "<![CDATA[", 9) == 0 ) {
				link += 9;
				linkLen -= 12;
			}
		}

		// . it doesn't have an "href" field (could be "name" field)
		// . "link" may not be NULL if empty, so use linkLen
		if ( linkLen == 0 ) 
			continue;
		// skip spaces in the front (should be utf8 compatible)
		while ( linkLen > 0 && is_wspace_a(*link) ) {
			link++; 
			linkLen--;
		}
		// don't add this link if it begins with javascript:
		if ( linkLen >= 11 && strncasecmp (link,"javascript:",11) ==0){
			// well... a lot of times the provided function has
			// the url as an arg to a popup window
			int32_t oclen = 0;
			char *oc = xml->getString(i,"onclick",&oclen);
			// if none, bail
			if ( ! oc ) continue;
			// set end
			char *ocend = oc + oclen - 2;
			char *ocurl = NULL;
			// scan for "'/" which should indicate the url
			for ( ; oc < ocend ; oc++ ) {
				if ( *oc   !='\'' ) continue;
				if (  oc[1]!='/'  ) continue;
				// set the start
				ocurl = oc + 1;
				// and stop the scan
				break;
			}
			// if none, bail
			if ( ! ocurl ) continue;
			// now find the end of the url
			char *ocurlend = ocurl + 1;
			for ( ; ocurlend < ocend ; ocurlend++ ) 
				if ( *ocurlend == '\'' ) break;
			// assign it now
			link    = ocurl;
			linkLen = ocurlend - ocurl;
			// and continue
		}
		if ( linkLen == 0 ) 
			continue;
		// it's a page-relative link
		if ( link[0]=='#' )  continue;
		// ignore mailto: links
		if ( linkLen >= 7 && strncasecmp( link , "mailto:" , 7 ) == 0 )
			continue;
		// make sure not too many links already
		//if ( version < 72 && m_numLinks >= MAX_LINKS ) break;

		QUICKPOLL(niceness);

		// if we have a sequence of alnum chars (or hpyhens) followed 
		// by a ':' then that is a protocol. we only support http and 
		// https protocols right now. let "p" point to the ':'.
		char *p = link;
		int32_t  pmaxLen = linkLen;
		if ( pmaxLen > 20 ) pmaxLen = 20;
		char *pend = link + pmaxLen;
		while ( p < pend && (is_alnum_a(*p) || *p=='-') ) p++;
		// is the protocol, if it exists, a valid one like http or 
		// https?  if not, ignore it. we only support FQDNs 
		// (fully qualified domain names) here really. so if you
		// have something like mylocalhostname:8000/ it is not going
		// to work anymore. you would need "use /etc/hosts" enabled
		// for that to work, too.
		bool proto = true;
		if ( p < pend && *p == ':' ) {
			proto = false;
			int32_t plen = p - link;
			if ( plen == 4 && strncasecmp(link,"http" ,plen) == 0 )
				proto = true;
			if ( plen == 5 && strncasecmp(link,"https",plen) == 0 )
				proto = true;
		}
		// skip if proto invalid like callto:+4355645998 or
		// mailto:jimbob@hoho.com
		if ( ! proto ) continue;
		// add it
		char  ptmp [ MAX_URL_LEN + 1 + 1 ];
		// keep an underpad of 1 byte in case we need to prepend a /
		char *tmp = ptmp + 1;
		if ( linkLen > MAX_URL_LEN ) {
			// only log this once just so people know, don't spam
			// the log with it.
			static bool s_flag = 1;
			if ( s_flag ) {
				s_flag = 0;
				log(LOG_INFO, "build: Link len %" INT32 " is longer "
					      "than max of %" INT32 ". Link will not "
					      "be added to spider queue or "
					      "indexed for link: search.",
					      linkLen,(int32_t)MAX_URL_LEN);
			}
			continue;
		}
		// see if the <link> tag has a "type" file
		bool isRSS = false;
		int32_t typeLen;
		char *type =(char *)xml->getString(i, "type", &typeLen );
		// compare
		if ( type ) {
			if (strncasecmp(type,"application/atom+xml",20)==0)
				isRSS=true;
			if (strncasecmp(type,"application/rss+xml" ,19)==0)
				isRSS=true;
			if (strncasecmp(type,"text/xml",8)==0)
				isRSS=true;
		}
		int32_t relLen = 0;
		char *rel = NULL;
		// make sure we got rel='alternate' or rel="alternate", etc.
		if ( isRSS ) rel = xml->getString(i,"rel",&relLen);
		// compare
		if ( rel && strncasecmp(rel,"alternate",9) != 0 ) 
			isRSS = false;
		// skip if a reply! rss feeds have these links to comments
		// and just ignore them for now
		if ( rel && strncasecmp(rel,"replies",7)==0 )
			continue;
		// http://dancleary.blogspot.com/feeds/posts/default uses edit:
		if ( rel && strncasecmp(rel,"edit",4)==0 )
			continue;
		// store it
		if ( isRSS ) m_hasRSS = true;
		// JAB: warning abatement
		//TODO: should we urlEncode here?
		// i didn't know this, but links can have encoded html entities
		// like &amp; and &gt; etc. in them and we have to decode
		// assign the new decoded length. 
		// this is not compatible with m_doQuickSet because we store
		// the "link" ptr into the array of link ptrs, and this uses
		// the "tmp" buf.
		// nono, need this now otherwise it hits that linkNode<0
		// error msg in XmlDoc.cpp. but for Msg13 spider compression
		// you might want to do something else then i guess...
		linkLen = htmlDecode ( tmp ,
				       link , 
				       linkLen,
				       false,
				       niceness );
		// use tmp buf
		link = tmp;

		if (!addLink ( link , linkLen , i , setLinkHash , 
			       version , niceness , isRSS , id , flags ))
			return false;
		// get the xml node
		//XmlNode *node = m_xml->getNodePtr(i);
		// set this special member
		//node->m_linkNum = m_numLinks - 1;
		// set the flag if it is an RSS link
	}
	// . flag the links we have that are old (spidered last time)
	// . set LF_OLDLINK flag
	if ( ! flagOldLinks ( oldLinks ) ) return false;
	return true;
}

// just a NULL-terminated text buffer/file of links to add
bool Links::set ( char *buf ,  int32_t niceness ) {
	reset();
	// need "coll" for Url::isSiteRoot(), etc.
	//m_coll = coll;
	m_parentUrl = NULL;
	m_baseUrl = NULL;
	m_addSiteRootFlags = false;
	m_xml = NULL;
	char *p = buf;
	while ( *p ) {
		// skip spaces
		while ( *p && is_wspace_a(*p) ) p++;
		// get the length of the link
		char *q = p;
		while ( *q && ! is_wspace_a(*q) ) q++;
		int32_t len = q - p;
		// add the link
		if ( ! addLink ( p , len , -1 , false , 
				 TITLEREC_CURRENT_VERSION , niceness, false,
				 TAG_A , 0 ) ) 
			return false;
		// advance
		p = q;
	}
	// assume none are flagged as old, LF_OLDLINK
	m_flagged = true;
	return true;
}

bool Links::print ( SafeBuf *sb ) {
	sb->safePrintf(
		       "<table cellpadding=3 border=1>\n"
		       "<tr>"
		       "<td>#</td>"
		       "<td colspan=40>"
		       // table header row
		       "Outlink"
		       "</td>"
		       "</tr>"
		       );
	// find the link point to our url
	int32_t i;
	for ( i = 0 ; i < m_numLinks ; i++ ) {
		char *link    = getLinkPtr(i);
		int32_t  linkLen = getLinkLen(i);
		sb->safePrintf("<tr><td>%" INT32 "</td><td>",i);
		sb->safeMemcpy(link,linkLen);
		sb->safePrintf("</td></tr>\n");
	}
	sb->safePrintf("</table>\n<br>\n");
	return true;
}

bool Links::addLink ( char *link , int32_t linkLen , int32_t nodeNum ,
		      bool setLinkHash , int32_t titleRecVersion ,
		      int32_t niceness , bool isRSS , int32_t tagId ,
		      int32_t flagsArg ){

	// don't add 0 length links
	if ( linkLen <= 0 ) return true;

	// do we need to alloc more link space?
	if (m_numLinks >= m_allocLinks) {
		int32_t newAllocLinks;

		if (!m_allocLinks)            newAllocLinks =10000;
		else if (m_allocLinks<100000) newAllocLinks =m_allocLinks*2;
		else                          newAllocLinks =m_allocLinks+100000;
		
		// how much mem do we need for newAllocLinks links?
		int32_t newAllocSize = getLinkBufferSize(newAllocLinks); 

		QUICKPOLL(niceness);

		char *newBuf = (char*)mmalloc(newAllocSize, "Links");
		if (!newBuf) return false; 

		QUICKPOLL(niceness);

		// a ptr to it
		char *p = newBuf;
		// debug msg
		log(LOG_DEBUG, "build: resizing Links ptr buffer to %" INT32 "",
		    newAllocSize);

		char **newLinkPtrs = (char**)p;
		p += newAllocLinks * sizeof(char *) ;

		int32_t *newLinkLens = (int32_t*)p;
		p += newAllocLinks * sizeof(int32_t) ;

		int32_t *newLinkNodes = (int32_t*)p; 
		p += newAllocLinks * sizeof(int32_t) ;

		uint64_t *newLinkHashes = (uint64_t *)p;
		p += newAllocLinks * sizeof(uint64_t) ;

		uint64_t *newHostHashes = (uint64_t *)p;
		p += newAllocLinks * sizeof(uint64_t) ;

		int32_t *newDomHashes = (int32_t *)p;
		p += newAllocLinks * sizeof(int32_t);

		linkflags_t *newLinkFlags = (linkflags_t *)p;
		p += newAllocLinks * sizeof(linkflags_t) ;

		char **newSpamNotes = (char **)p;
		p += newAllocLinks * sizeof(char **);

		// sanity check -- check for breach
		gbassert_false( p > newBuf + newAllocSize );

		if (m_linkBuf){
			gbmemcpy(newLinkPtrs, m_linkPtrs, 
			       m_numLinks * sizeof(char*));
			QUICKPOLL(niceness);
			gbmemcpy(newLinkLens, m_linkLens, 
			       m_numLinks * sizeof(int32_t));
			QUICKPOLL(niceness);
			gbmemcpy(newLinkNodes, m_linkNodes, 
			       m_numLinks * sizeof(int32_t));
			QUICKPOLL(niceness);
			gbmemcpy(newLinkHashes, m_linkHashes,
			       m_numLinks * sizeof(uint64_t));
			QUICKPOLL(niceness);
			gbmemcpy(newHostHashes, m_hostHashes,
			       m_numLinks * sizeof(uint64_t));
			QUICKPOLL(niceness);
			gbmemcpy(newDomHashes, m_domHashes,
			       m_numLinks * sizeof(int32_t));
			QUICKPOLL(niceness);
			gbmemcpy(newLinkFlags, m_linkFlags,
			       m_numLinks * sizeof(linkflags_t));
			QUICKPOLL(niceness);
			gbmemcpy(newSpamNotes,m_spamNotes,
			       m_numLinks * sizeof(char *));
			int32_t oldSize = getLinkBufferSize(m_allocLinks);
			mfree(m_linkBuf, oldSize, "Links");
			QUICKPOLL(niceness);
		}
		m_allocLinks = newAllocLinks;
		m_linkBuf    = newBuf;
		m_linkPtrs   = newLinkPtrs;
		m_linkLens   = newLinkLens;
		m_linkNodes  = newLinkNodes;
		m_linkHashes = newLinkHashes;
		m_hostHashes = newHostHashes;
		m_domHashes  = newDomHashes;
		m_linkFlags  = newLinkFlags;
		m_spamNotes  = newSpamNotes;
	}

	// normalize the link and prepend base url if needed
	Url url;

	// we now use everything has is for sites like file.org
	bool addWWW = false;

	/////
	//
	// hack fix. if link has spaces in it convert to +'s
	// will fix urls like those in anchor tags on
	// http://www.birmingham-boxes.co.uk/catagory.asp
	//
	/////
	bool hasSpaces = false;
	char tmp[MAX_URL_LEN+2];
	for ( int32_t k = 0 ; k < linkLen ; k++ ) {
		if ( link[k] == ' ' ) hasSpaces = true;
		// watch out for unterminated quotes
		if ( link[k] == '>' ) { hasSpaces = false; break; }
	}
	bool hitQuestionMark = false;
	int32_t src = 0;
	int32_t dst = 0;
	for ( ;hasSpaces && linkLen<MAX_URL_LEN && src<linkLen ; src++ ){
		// if not enough buffer then we couldn't do the conversion.
		if ( dst+3 >= MAX_URL_LEN ) { hasSpaces = false; break; }
		if ( link[src] == '?' ) 
			hitQuestionMark = true;
		if ( link[src] != ' ' ) {
			tmp[dst++] = link[src];
			continue;
		}
		// if we are part of the cgi stuff, use +
		if ( hitQuestionMark ) { 
			tmp[dst++] = '+'; 
			continue;
		}
		// if before the '?' then use %20
		tmp[dst++] = '%';
		tmp[dst++] = '2';
		tmp[dst++] = '0';
	}
	if ( hasSpaces ) {
		link = tmp;
		linkLen = dst;
		tmp[dst] = '\0';
	}
		


	url.set ( m_baseUrl       ,
		  link            ,
		  linkLen         ,
		  addWWW          , // addWWW?
		  m_stripIds      ,
		  // now i strip this thang because the rss
		  // feeds have a link to every comment but it is
		  // really the same url...
		  true            , // stripPound? 
		  // convert /index.html to /
		  // turned this back on per john's request
		  // will cause undeletable data in existing indexes.
		  true            , // stripCommonFile?
		  titleRecVersion );// used for removing session ids


	// sometimes there's links like:
	// http://'+ycso[8]+ \n'commentsn?blog_id=... which is within
	// <script></script> tags
	if ( url.getDomainLen() <= 0 || url.getHostLen() <= 0 ) return true;

	// stop http://0x0017.0000000000000000000000000000000000000024521276/
	// which somehow make it through without this!!
	if ( ! url.isIp() && url.getTLDLen() <= 0 ) return true;

	// debug TODO: fix a href=\"http://www.thecounter.com"\ thangs

	int32_t bufSpace ;
	if ( m_allocBuf ) bufSpace = m_allocSize - (m_bufPtr-m_allocBuf);
	else              bufSpace = 0;
	// allocate dynamic buffer for lotsa links
	if ( url.getUrlLen() + 1 > bufSpace ) {
		// grow by 100K
		int32_t newAllocSize;
		if ( ! m_allocSize ) newAllocSize = LINK_BUF_SIZE;
		else if (m_allocSize < 1024*1024) newAllocSize = m_allocSize*2;
		else  newAllocSize = m_allocSize + 1024*1024;
		// MDW: a realloc would be more efficient here.
		QUICKPOLL(niceness);
		char *newBuf = (char*)mmalloc(newAllocSize, "Links");
		if ( ! newBuf ) return log("build: Links failed to realloc.");
		log(LOG_DEBUG, "build: resizing Links text buffer to %" INT32 "",
		    newAllocSize);
		QUICKPOLL(niceness);
		if ( m_allocBuf ) {
			QUICKPOLL(niceness);
			gbmemcpy ( newBuf , m_allocBuf , m_allocSize );
			QUICKPOLL(niceness);
			// update pointers to previous buffer
			int64_t offset = newBuf - m_allocBuf;
			char *allocEnd = m_allocBuf + m_allocSize;
			for (int32_t i = 0 ; i < m_numLinks ; i++ ) {
				QUICKPOLL(niceness);
				if ( m_linkPtrs[i] <  m_allocBuf ) continue;
				if ( m_linkPtrs[i] >= allocEnd   ) continue;
				m_linkPtrs[i] += offset;
			}
			m_bufPtr += offset;
			QUICKPOLL(niceness);
			mfree ( m_allocBuf , m_allocSize , "Links");
			QUICKPOLL(niceness);
		}
		else m_bufPtr = newBuf;

		m_allocBuf  = newBuf;
		m_allocSize = newAllocSize;
	}
	
	// . is hostname gigablast.com or www.gigablast.com?
	// . must be in the top 100k of link text, too!
	int32_t  hlen = url.getHostLen();
	char *h    = url.getHost   ();
	if ( hlen == 13 && strncmp ( h , "gigablast.com"     , 13 ) == 0 )
		m_linksToGigablast = true;
	if ( hlen == 17 && strncmp ( h , "www.gigablast.com" , 17 ) == 0 )
		m_linksToGigablast = true;
	// add some info
	m_linkPtrs    [ m_numLinks ] = m_bufPtr;
	m_linkLens    [ m_numLinks ] = url.getUrlLen();
	m_linkNodes   [ m_numLinks ] = nodeNum;
	// serialize the normalized link into the buffer 
	gbmemcpy ( m_bufPtr , url.getUrl(), url.getUrlLen() );
	m_bufPtr += url.getUrlLen();
	QUICKPOLL(niceness);

	// and NULL terminate it
	*m_bufPtr++  = '\0';

	// . set link hash if we need to
	// . the Vector class uses these link hashes for determining similarity
	//   of this document to another for purposes of fightling link spam
	// . we essentially compare the linking web pages against one another
	//   and if we find one that is similar to another we weight it's
	//   link text down. The more similar the more the penalty. We just
	//   see what links it has in common with the others for now...
	if ( setLinkHash ) {
		// sanity
		gbassert_false( m_doQuickSet );
		// get url length
		int32_t ulen = url.getUrlLen();
		// subtract the cgi length
		if ( url.isCgi() ) ulen -= 1 + url.getQueryLen();
		// store it's hash
		m_linkHashes [ m_numLinks ] = url.getUrlHash64();
		m_hostHashes [ m_numLinks ] = url.getHostHash64();
		m_domHashes  [ m_numLinks ] = url.getDomainHash32();
	}

	// set the bits in the flags byte
	linkflags_t flags = flagsArg; // 0;
	// set flag bit #0 if it is an "internal" link -- from same hostname
	if ( m_baseUrl && url.getHostLen() == m_baseUrl->getHostLen() &&
	     strncmp(url.getHost(),m_baseUrl->getHost(),url.getHostLen())==0){
		flags |= LF_SAMEHOST;
		flags |= LF_SAMEDOM;
	}
	else if (m_baseUrl &&url.getDomainLen() == m_baseUrl->getDomainLen() &&
	     strncmp(url.getDomain(),m_baseUrl->getDomain(),
		     url.getDomainLen())==0) {
		flags |= LF_SAMEDOM;
		// . memoori was adding www.construction.com which redirected
		//   to construction.com/index.asp and it did not add the
		//   outlinks because "spider internal links only" was true.
		//   i.e. Msg16::m_sameHostLinks was true
		// . if not same host but domains match, consider it internal
		//   if hosts only differ by a www. this should fix that.
		if ( m_baseUrl->isHostWWW() && !url       .hasSubdomain() )
			flags |= LF_SAMEHOST;
		if ( url.       isHostWWW() && !m_baseUrl->hasSubdomain() )
			flags |= LF_SAMEHOST;
	}		

	char *tld  = url.getTLD();
	int32_t  tlen = url.getTLDLen();
	if ( tlen == 3 && ! strncmp(tld,"edu",3) ) flags |= LF_EDUTLD;
	if ( tlen == 3 && ! strncmp(tld,"gov",3) ) flags |= LF_GOVTLD;

	// rss?
	if ( isRSS ) {
		// flag it
		flags |= LF_RSS;
		// we had one
		m_hasRSSOutlink = true;
		// store the first one
		if ( ! m_rssOutlinkPtr ) {
			m_rssOutlinkPtr = m_linkPtrs[m_numLinks];
			m_rssOutlinkLen = m_linkLens[m_numLinks];
		}
	}


	if      ( tagId == TAG_A          ) flags |= LF_AHREFTAG;
	else if ( tagId == TAG_LINK       ) flags |= LF_LINKTAG;
	else if ( tagId == TAG_FBORIGLINK ) flags |= LF_FBTAG;

	// a self link?
	if ( m_parentUrl &&
	     // MUST be a PROPER subset, links to itself do not count!
	     url.getUrlLen() == m_parentUrl->getUrlLen() &&
	     strncmp(url.getUrl(), m_parentUrl->getUrl(), 
		     m_parentUrl->getUrlLen())==0) {
		flags |= LF_SELFLINK;
		// turn this flag on
		if ( nodeNum >= 0 ) m_xml->m_nodes[nodeNum].m_isSelfLink = 1;
	}

	// now check for the "permalink" key word or "permanent link" keyphrase
	// TEST CASES:
	//http://www.celebritybabyscoop.com/2008/12/28/jennifer-garner-is-still-pregnant/  +  fp_1765421_garner_jennifer_znk_122808jpg/
	//http://www.celebritybabyscoop.com/2008/12/27/gwen-stefani-family-spread-holiday-cheer/
	// http://www.thetrendwatch.com/2008/12/22/how-big-shows-are-becoming-utterly-blaze/   + events-are-boring/
	// http://thinkprogress.org/2008/12/26/bush-pardon-campaign/
	if ( ( flags & LF_SELFLINK ) && ( flags & LF_AHREFTAG ) &&
	     // must be valid
	     nodeNum >= 0 ) {
		XmlNode *nodes = m_xml->getNodes();
		// get back tag
		int32_t max = nodeNum + 20;
		if ( max > m_xml->getNumNodes() ) max = m_xml->getNumNodes();
		int32_t nn = nodeNum + 1;
		while ( nn < max && nodes[nn].m_nodeId != TAG_A ) nn++;
		if ( nn < max ) {
			char *s       = nodes[nodeNum].m_node;
			char *send    = nodes[nn].m_node;
			for ( ; s < send ; s++ ) {
				if ( *s != 'p' && *s != 'P' ) continue;
				if ( ! strncasecmp(s,"permalink",9) ) 
					break;
				if ( ! strncasecmp(s,"permanent link",14) ) 
					break;
			}
			if ( s < send ) {
				flags |= LF_SELFPERMALINK;
				m_hasSelfPermalink = true;
			}
		}
	}

	// get each url length without the cgi
	int32_t len1 = url.getUrlLen() - url.getQueryLen();
	int32_t len2 = 0;
	if ( m_parentUrl ) 
		len2 = m_parentUrl->getUrlLen() - m_parentUrl->getQueryLen();
	// discount the '?' cuz it is not included in the queryLen right now
	if ( url.getQueryLen() ) len1--;
	if ( m_parentUrl && m_parentUrl->getQueryLen() ) len2--;

	// . is it in a subdir of us?
	// TEST CASES:
	// http://joedecie.livejournal.com/28834.html?thread=167074#t167074
	if ( m_parentUrl &&
	     // MUST be a PROPER subset, links to itself do not count!
	     len1 > len2 &&
	     strncmp(url.getUrl(), m_parentUrl->getUrl(),len2)==0) {
		flags |= LF_SUBDIR;
		m_hasSubdirOutlink = true;
	}


	// FIXME:
	// http://www.packers.com/news/releases/2008/12/24/1/email_to_a_friend/

	// FIXME:
	// only has one hyphen but is indeed a permalink!
	// http://marccooper.com/xmas-vacation/

	// TEST CASES:
	// href="...ami.php?url=http://lewebpedagogique.com/blog/2008/11/16/
	// la-seconde-guerre-mondiale-cours ... will have its cgi ignored so
	// such links as this one will not be considered permalinks
	char *pathOverride = NULL;
	bool  ignoreCgi    = false;
	if ( (flags & LF_SUBDIR) && m_parentIsPermalink ) {
		pathOverride = url.getUrl() + m_parentUrl->getUrlLen();
		// must be same host
		if ( m_parentUrl->getHostLen() != url.getHostLen() )
			pathOverride = NULL;
		// same host str check
		else if ( strncmp( m_parentUrl->getHost() ,
				   url.getHost() ,
				   url.getHostLen()) )
			pathOverride = NULL;
		// must be in bounds		
		else if ( url.getUrlLen() <= m_parentUrl->getUrlLen() )
			pathOverride = NULL;
		// if we are a permalink, ignore cgi for seeing if they are
		if ( pathOverride ) ignoreCgi = true;
	}

	// if it is a subset of a permalink parent, it is not a "true" 
	// permalink if it concatenates the word "comment" onto the parent url,
	// it is likely a permalink for a comment, which does not really count
	// TEST CASES:
	// www.flickr.com/photos/korayem/2947977582/comment72157608088269210/
	// robertsquier.blogspot.com/2008/10/drawing-tv.html?showComment=.2..
	// profootballtalk.com/2008/12/28/vikings-win-nfc-north/comment-page-1/
	bool permCheck = true;
	if ( m_doQuickSet ) permCheck = false;
	if ( permCheck && ignoreCgi && strstr(pathOverride,"comment") ) 
		permCheck = false;
	if ( permCheck && ignoreCgi && strstr(pathOverride,"Comment") ) 
		permCheck = false;
	if ( permCheck && ignoreCgi && strstr(pathOverride,"COMMENT") ) 
		permCheck = false;

	// . are we probably a permalink?
	// . we do not have a TagRec at this point so we just can not 
	//   tell whether we are siteRoot, and therefore NOT a permalink
	linkflags_t extraFlags = 0;
	if ( permCheck &&
	     ::isPermalink ( //m_coll   ,
			     NULL     , // Links ptr
			     &url     , // the url
			     CT_HTML  , // contentType
			     NULL     , // LinkInfo ptr
			     isRSS    ,
			     NULL     , // note ptr
			     pathOverride ,
			     ignoreCgi    ,
			     // might include LF_STRONGPERM
			     &extraFlags  ) ) {
		flags |= LF_PERMALINK;
		flags |= extraFlags;
	}

	// set in flag array
	m_linkFlags [ m_numLinks ] = flags;

	// set to NULL for now -- call setLinkSpam() later...
	m_spamNotes [ m_numLinks ] = NULL;

	QUICKPOLL(niceness);

	// inc the count
	m_numLinks++;
	return true;
}

// . does link #i have link text?
// . link text must have at least one alnum in it
bool Links::hasLinkText ( int32_t n, int32_t version ) {
	// return 0 if no link to our "url"
	if ( n >= m_numLinks ) return false;
	// get the node range so we can call Xml::getText()
	int32_t node1 = m_linkNodes [ n ];

	// post-dating this change back to version 75, since it happened
	// sometime right before this version bump, it allows for 
	// the least amount of docs to be indexed wrong
	// only for <a> tags
	if (node1 >= m_xml->getNumNodes()) return false;
	if (m_xml->getNodeId(node1) != TAG_A) return false;
	
	// find the </a> to this <a href> tag, or next <a href> tag
	int32_t node2 = m_xml->getNodeNum ( node1+1,9999999,"a",1);
	// if not found use the last node in the document
	if ( node2 < 0 ) node2 = m_xml->getNumNodes();
	// check for text node in (node1,node2) range
	for ( int32_t i = node1+1 ; i < node2 ; i++ ) {
		// continue if a tag
		if ( m_xml->isTag(i) ) continue;
		// otherwise, it's text
		char *s    = m_xml->getNode   (i);
		char *send = s + m_xml->getNodeLen(i);
		// . does it have any alnums in it?
		// . may be tricked by html entities like #187; or something
		for ( ; s < send ; s += getUtf8CharSize(s) ) 
			if ( is_alnum_utf8 ( s ) ) return true;
	}
	// otherwise, we found no text node with an alnum
	return false;
}

// . stores link text into "buf" and returns the length
// . TODO: speed up so we don't have to set Url for every link in doc
int32_t Links::getLinkText ( char  *linkee ,
			  bool   getSiteLinkInfo ,
			  char  *buf       , 
			  int32_t   bufMaxLen , 
			  //bool   filter    ,
			  char **itemPtr   ,
			  int32_t  *itemLen   ,
			  int32_t  *retNode1  ,
			  int32_t  *retLinkNum ,
			  int32_t   niceness  ) {
	// assume none
	if ( retNode1   ) *retNode1 = -1;
	// assume no link text
	buf[0] = '\0';
	// assume no item
	if ( itemPtr ) *itemPtr = NULL;
	if ( itemLen ) *itemLen = 0;

	// if it is site based, skip the protocol because the site might
	// be just a domain and not a subdomain
	if ( getSiteLinkInfo ) {
		char *pp = strstr ( linkee, "://");
		if ( pp ) linkee = pp + 3;
	}

	int32_t linkeeLen = gbstrlen(linkee);

	// find the link point to our url
	int32_t i;
	for ( i = 0 ; i < m_numLinks ; i++ ) {
		QUICKPOLL(niceness);
		char *link    = getLinkPtr(i);
		int32_t  linkLen = getLinkLen(i);
		// now see if its a full match
		// special case if site
		if ( getSiteLinkInfo ) {
			if ( strstr ( link, linkee ) ) break;
			continue;
		}
		// continue if don't match
		if ( linkLen != linkeeLen ) continue;
		// continue if don't match
		if ( strcmp ( link , linkee ) != 0 ) continue;
		// otherwise it's a hit
		break;
	}
	// return 0 if no link to our "url"
	if ( i >= m_numLinks ) return 0;

	*retLinkNum = i;

	return getLinkText2(i,buf,bufMaxLen,itemPtr,itemLen,retNode1,niceness);
}


int32_t Links::getLinkText2 ( int32_t i ,
			   char  *buf       , 
			   int32_t   bufMaxLen , 
			   //bool   filter    ,
			   char **itemPtr   ,
			   int32_t  *itemLen   ,
			   int32_t  *retNode1  ,
			   int32_t   niceness  ) {
	// get the node range so we can call Xml::getText()
	int32_t node1 = m_linkNodes [ i ];

	// . <area href=> tags have no link text
	// . fix for http://www.cs.umass.edu/%7Everts/index.html 's
	//   link to phdcomics.com . it was picking up bogus link text
	//   from page tail.
	XmlNode *xmlNodes = m_xml->getNodes();
	if ( xmlNodes[node1].m_nodeId == TAG_AREA ) return 0;

	// what delimiter are we using? this only applies to rss/atom feeds.
	//char *del = NULL;
	char del[16];
	int32_t dlen = 0;
	int32_t rss = m_xml->isRSSFeed();
	if ( rss == 1 ) {
		gbmemcpy(del, "item\0", 5);
		dlen = 4;
	}
	else if ( rss == 2 ) {
		gbmemcpy(del, "entry\0", 6);
		dlen = 5;
	}
	// if rss or atom page, return the whole xml <item> or <entry>
	if ( dlen > 0 ) {
		// bail if not wanted
		if ( ! itemPtr ) return 0;
		int32_t     xmlNumNodes = m_xml->getNumNodes();
		// . must come from a <link> node, not a <a>
		// . can also be an <enclosure> tag now too
		if ( xmlNodes[node1].m_nodeId == 2 ) goto skipItem;
		// back pedal node until we hit <item> or <entry> tag
		int32_t j ;
		for ( j = node1 ; j > 0 ; j-- ) {
			QUICKPOLL(niceness);
			// skip text nodes
			if ( xmlNodes[j].m_nodeId == 0 ) continue;
			// check the tag
			if(xmlNodes[j].m_tagNameLen != dlen) continue;
			if(strncasecmp(xmlNodes[j].m_tagName,del,dlen))
				continue;
			break;
		}
		// . if j is 0 we never found the <item> or <entry> tag
		//   because rss and atom feeds never start with such a tag
		// . but we could be in the <channel> section, which is ok
		//   so i commented this out
		//if ( j == 0 ) return 0;
		// ptr to the start of it
		char *s = xmlNodes[j].m_node;
		// save this
		if ( retNode1 ) *retNode1 = j;
		// the end ptr
		//char *send = s + xmlNodes[j].m_nodeLen;
		char *send = m_xml->getContent() + m_xml->getContentLen();
		// . start at the first tag in this element/item
		// . we will copy the blurb on the interval [j,k)
		for ( int32_t k = j+1 ; k < xmlNumNodes ; k++ ) {
			QUICKPOLL(niceness);
			// get the next node in line
			XmlNode *nn = &xmlNodes[k];
			// break out if done
			if ( k >= xmlNumNodes ) break;
			// skip text nodes
			if ( nn->m_nodeId == 0 ) continue;
			// skip script sections, inside script tags
			if ( nn->m_nodeId == TAG_SCRIPTTEXT ) continue;
			if(nn->m_tagNameLen != dlen) continue;
			if(strncasecmp(nn->m_tagName,del,dlen))	continue;
			// we got the end of the item, set "send"
			send = nn->m_node + nn->m_nodeLen;
			// and we're done, break out
			break; 
		}
		// this is a blurb, send it back as such
		*itemPtr = s;
		*itemLen = send - s;
		// rss feeds do not have conventional link text
		return 0;
	}
skipItem:
	// find the </a> to this <a href> tag, or next <a href> tag
	int32_t node2 = m_xml->getNodeNum ( node1+1,9999999,"a",1);
	// if not found use the last node in the document
	if ( node2 < 0 ) node2 = 99999999;
	// now we can call Xml::getText()
	int32_t bufLen =  m_xml->getText ( buf   , 
					bufMaxLen ,
					node1 ,  // get kid text of this node
					node2 ,
					false ,  // include tags?
					true  ,  // visible text only?
					//true  ,  // filter? (entities to iso)
					false ,  // filter? (entities to iso)
					false ,  // filter spaces?
					false ); // exclude if in <stop index>?
	// set it
	if ( retNode1 ) *retNode1 = node1;
	// hunt for an alnum in the link text
	char *p    = buf;
	char *pend = buf + bufLen;
	for ( ; p < pend ; p += getUtf8CharSize(p) ) {
		QUICKPOLL ( niceness );
		if ( is_alnum_utf8(p) ) break;
	}
	// if no alnum then return 0 as the link text len
	if ( p >= pend ) return 0;
	// find last non-space char
	char *q = p;
	char *last = NULL;
	for ( ; q < pend ; q += getUtf8CharSize(p) ) {
		QUICKPOLL ( niceness );
		if ( ! is_wspace_utf8(q) ) last = q;
	}
	// hack off trailing spaces
	if ( last ) pend = last + getUtf8CharSize(last); // +1;
	// shift left if we expunged some leading non-alnums
	memmove ( buf , p , pend - p );
	// reset buflen
	bufLen = pend - p;
	// null terminate
	buf [ bufLen ] = '\0';
	// return length
	return bufLen;
}

// find an ascii substring in linktext for this link and return a pointer 
// to it, or NULL if not present
char *Links::linkTextSubstr(int32_t linkNum, char *string, int32_t niceness) {
	if (linkNum >= m_numLinks) return NULL;
	int32_t nodeNum = getNodeNum(linkNum);
	if (nodeNum >= m_xml->getNumNodes()-1) return NULL;
	
	for (int32_t i=nodeNum+1 ; i < m_xml->getNumNodes() ; i++ ) {
		XmlNode *node = m_xml->getNodePtr(i);
		if (node->getNodeId() == TAG_A) return NULL;
		if (node->getNodeId() != TAG_TEXTNODE) continue;
		// quickpoll, this is prone to blocking
		QUICKPOLL(niceness);
		// maybe handle img alt text here someday, too
		char *ptr;
		if ((ptr = strncasestr(node->getNode(), 
				       node->getNodeLen(), string)))
			return ptr;
	}
	return NULL;
}

int32_t Links::findLinkNum(char* url, int32_t urlLen) {
	for(int32_t i = 0;i< m_numLinks; i++) {
		if(m_linkLens[i] == urlLen &&
		   strncmp(url, m_linkPtrs[i], urlLen) == 0)
			return i;
	}
	return -1;
}

// helper function for shared link ptr buffer
static int32_t getLinkBufferSize(int32_t numLinks){
	return numLinks * 
		(sizeof(char*        ) + // linkPtrs
		 sizeof(int32_t         ) + // linkLens
		 sizeof(int32_t         ) + // linkNodes
		 sizeof(uint64_t     ) + // linkHashes
		 sizeof(uint64_t     ) + // hostHashes
		 sizeof(int32_t         ) + // domHashes
		 sizeof(linkflags_t  ) + // linkFlags
		 sizeof(char*        )   // spamNotes
		 );
}

// returns false and sets g_errno on error
bool Links::flagOldLinks ( Links *old ) {
	// do not double call
	if ( m_flagged ) return true;
	// only call once
	m_flagged = true;
	// skip if null
	if ( ! old ) return true;
	// hash the old links into a table
	HashTable ht;
	for ( int32_t i = 0 ; i < old->m_numLinks ; i++ ) {
		// get the url
		char *u    = old->m_linkPtrs[i];
		int32_t  ulen = old->m_linkLens[i];
		// hash it
		int64_t uh = hash32 ( u , ulen );
		// it does not like keys of 0, that means empty slot
		if ( uh == 0 ) uh = 1;
		// add to hash table
		if ( ! ht.addKey ( uh , 1 ) ) return false;
	}
	// set the flags
	for ( int32_t i = 0 ; i < m_numLinks ; i++ ) {
		// get the url
		char *u    = m_linkPtrs[i];
		int32_t  ulen = m_linkLens[i];
		// get our hash
		int64_t uh = hash32 ( u , ulen );
		// it does not like keys of 0, that means empty slot
		if ( uh == 0 ) uh = 1;
		// check if our hash is in this hash table, if not, then
		// it is a new link, skip this
		if ( ht.getSlot ( uh ) < 0 ) continue;
		// assume new
		m_linkFlags[i] |= LF_OLDLINK;
	}
	return true;
}

// . are we a permalink?
// . this registers as a permalink which it is not:
//   http://www.dawn.com/2009/01/04/rss.htm
//   http://www.msnbc.msn.com/id/3032072
bool isPermalink (
		   Links       *links        ,
		   Url         *u            ,
		   char         contentType  ,
		   LinkInfo    *linkInfo     ,
		   bool         isRSS        ,
		   char       **note         ,
		   char        *pathOverride ,
		   bool         ignoreCgi    ,
		   linkflags_t *retFlags     ) {

	// reset. caller will OR these into its flags
	if ( retFlags ) *retFlags = 0;

	// how can this happen?
	if ( ! u ) return false;

	// rss feeds cannot be permalinks
	if ( isRSS ) { if ( note ) *note = "url is rss feed."; return false; }

	// root pages don't get to be permalinks
	if ( u->isRoot() ) {
		if ( note ) *note = "url is a site root";	return false; }
	
	// only html (atom feeds link to themselves)
	if ( contentType != CT_HTML) {
		if ( note ) *note = "content is not html"; return false;}

	// techcrunch has links like this in the rss:
	// http://feedproxy.google.com/~r/Techcrunch/~3/pMaRh78u1W8/
	if ( strncmp(u->getHost(),"feedproxy.",10)==0 ) {
		if ( note ) *note = "from feedproxy host"; return true; }
	// might want to get <feedburner:origLink> instead of <link> if
	// we can. that would save a redirect through evil g
	if ( strncmp(u->getHost(),"feeds.feedburner.com/~",22)==0 ) {
		if ( note ) *note = "feedburner tilde url"; return true; }
	


	// . BUT if it has a link to itself on digg, reddit, etc. then it
	//   doesn't need to have the digits or the underscores...
	// . this helps us disnguish between
	//   science.howstuffworks.com/fantasy-football.html (permalink) and
	//   science.howstuffworks.com/space-channel.htm (NOT a permalink)
	// . i guess this includes "post a comment" links that are just 
	//   anchor links to the textarea at the page bottom so that will fix:
	//   http://workandplay.vox.com/library/post/running-again.html
	// . includes trackbacks, comments feed, etc.
	// . returns -1 if unknown whether it is a permalink or not
	char status = -1;
	if ( links ) status = links->isPermalink ( note );
	if ( status == 1 ) return true;
	if ( status == 0 ) return false;

	char *pathStart = u->getPath();
	// a hack by Links.cpp after setting LF_SUBDIR
	if ( pathOverride ) pathStart = pathOverride;

	// compute these
	linkflags_t extraFlags  = 0;

	// we must have a sequence of 3 or more digits in the path
	char *p      = pathStart;
	int32_t  plen   = u->getPathLen();
	char *pend   = u->getPath() + plen;
	int32_t  dcount = 0;
	// now we scan the cgi stuff too!!
	// http://www.rocklintoday.com/news/templates/sierra_college.asp?articleid=6848&zoneid=51
	// http://www.freemarketnews.com/WorldNews.asp?nid=57373
	char *uend   = u->getUrl() + u->getUrlLen();
	// halt at path if we should
	if ( ignoreCgi ) uend -= u->getQueryLen(); // CgiLen();
	// see if we find the digits in the cgi part
	bool digitsInCgi = false;
	// start scanning at the path
	for ( ; p < uend ; p++ ) {
		if ( *p == '?' ) digitsInCgi = true;
		// if not a digit, reset count
		if ( ! is_digit(*p) ) { dcount = 0; continue; }
		// . check if it is a "strong permalink"
		// . i.e. contains /yyyy/mm/?? in PATH (not cgi)
		if ( p + 9 < pend  &&
		     *(p-1)=='/'    && 
		     is_digit(p[0]) &&
		     is_digit(p[1]) &&
		     is_digit(p[2]) &&
		     is_digit(p[3]) &&
		     p[4] == '/'    &&
		     is_digit(p[5]) &&
		     is_digit(p[6]) &&
		     p[7] == '/'    ) {
			// http://www.it.com.cn/f/office/091/4/722111.htm
			// was thought to have strong outlinks, but they were
			// not! this should fix it...
			int32_t y = atoi(p+0);
			int32_t m = atoi(p+5);
			// make sure the year and month are in range
			if ( y >= 1990 && y <= 2050 && m >= 1 && m <= 31 )
				extraFlags |= LF_STRONGPERM;
		}
		// count it if a digit
		if ( ++dcount == 3 ) break;
	}
	// it can also have 2+ hyphens or 2+ underscores in a single
	// path component to be a permalink
	int32_t hcount = 0;
	p = pathStart;
	for ( ; p < pend ; p++ ) {
		// if not a digit, reset count
		if ( *p == '/' ) { hcount = 0; continue; }
		// is it a thing?
		if ( *p != '_' && *p != '-' ) continue;
		// count it
		if ( ++hcount == 2 ) break;
	}

	// we can have a cgi of "?p=<digit>" and be a permalink
	p = u->m_query;
	bool hasp = ( p && p[0]=='p' && p[1]=='=' && is_digit(p[2]) ) ;
	// fix for http://proglobalbusiness.org/?m=200806 being detected as
	// a permalink... it has ?p=xxx outlinks.
	if ( hasp ) extraFlags |= LF_STRONGPERM;

	// return these if the caller wants them
	if ( retFlags ) *retFlags = extraFlags;

	// . if we don't then not a permalink
	// . THIS STILL FAILS on stuff like:
	//   BUT we can fix that by doing url pattern analysis? yeah, 
	//   each domain can have a tag that is the permalink subdir, so
	//   that any url in that subdir is a permalink.
	if ( ! hasp && dcount < 3 && hcount < 2 ) {
		if ( note ) 
			*note = "path has no digits, underscores or hyphens";
		return false;
	}


	// if self link check for link text "permalink" then we are
	// probably very strongly a permalink
	// http://www.5minutesformom.com/5225/wordless-wednesday-angel/
	// has a /promote-your-site tack-on which causes the LF_SUBDIR
	// algo to call the parent a NON-permalink.this should fix that
	// because it has a link to itself with the word "permalink"
	if ( links && links->hasSelfPermalink() ) {
		if ( note ) *note = "has permalink text to itself";
		return true;
	}

	// http://proglobalbusiness.org/?m=200806 is never a permalink
	p = u->m_query;
	if ( p && p[0]=='m' && p[1]=='=' && is_digit(p[2]) ) {
		int32_t n = atoi(p+2);
		if ( n > 199000 && n < 205000 ) {
			if ( note ) *note = "has ?m=<year><month> cgi";
			return false;
		}
	}

	// . if we have an internal outlink that is a permalink and is
	//   in a subdirectory of us, THEN we are not a permalink
	// . fixes andrewsullivan.atlanticmonthly.com/the_daily_dish/
	linkflags_t mf  = (LF_PERMALINK | LF_SAMEHOST | LF_SUBDIR );
	// loop over all outlinks
	int32_t no = 0;
	// make sure we got them
	if ( links ) no = links->m_numLinks;
	// practically all internal outlinks have LF_SUBDIR set for permalinks
	// for the url http://www.breitbart.tv/?p=249453 so do not do this 
	// outlink algo on it on such urls! basically anytime we got our
	// permalink indicator in the cgi portion of the url, do not do this
	// subdir algorithm.
	if ( hcount < 2 && dcount < 3 && hasp ) no = 0;
	// or if we only got digits and they were in the cgi
	if ( hcount < 2 && ! hasp && digitsInCgi ) no = 0;
	// do the outlink loop
	for ( int32_t i = 0 ; i < no ; i++ ) {
		// get the flags
		linkflags_t flags = links->m_linkFlags[i];
		// skip if not a match. match the match flags = "mf"
		if ( (flags & mf) != mf ) continue;
		// allow /print/ "printer view" pages
		if ( note ) *note = "has subdir permalink outlink";
		// ok, we are not a permalink now
		return false;
	}


	// now check for strong outlinks on same host when we are not strong
	if ( links ) no = links->m_numLinks;
	// if we are strong, forget it
	if ( extraFlags & LF_STRONGPERM ) no = 0;
	// look for strong permalink outlinks
	mf = (LF_STRONGPERM| LF_SAMEHOST );
	// loop over all outlinks we have
	for ( int32_t i = 0 ; i < no ; i++ ) {
		// get the flags
		linkflags_t flags = links->m_linkFlags[i];
		// . if we are NOT a "strong permalink" but we have a same host
		//   outlink that is, then we are not a permalink
		// . fixes: http://blog.makezine.com/archive/kids/
		//   ?CMP=OTC-0D6B48984890
		if ( (flags & mf) != mf ) continue;
		// allow /print/ "printer view" pages
		if ( note ) *note = "has strong permalink outlink";
		// ok, we are not a permalink now
		return false;
	}


	// no permalinks for archive directories
	if ( (gb_strcasestr(u->getPath(),"/archive")||
	      u->getPathDepth(false)==0) &&
	     gb_strcasestr(u->getPath(), "/index.") && 
	     !u->isCgi()){ 
		if ( note ) *note = "has /archive and /index. and not cgi";
		return false;}
	// no, /tag/ is ok -->  http://www.makeuseof.com/
	// BUT technorati.com/tag/search-engine-optimization is not a 
	// permalink!!! i took technorati.com|jp out of ruleset 36 for now
	// ah, but a ton of the urls have /tags/ and are NOT permalinks!!!
	if (gb_strcasestr(u->getPath(), "/tag/")){
		if ( note ) *note = "has /tag/"; return false;}
	// no forums or category indexes
	if (gb_strcasestr(u->getPath(), "/category")){
		if ( note ) *note = "has /category"; return false;}
	if (gb_strcasestr(u->getPath(), "/cat_")){
		if ( note ) *note = "has /cat_"; return false;}
	// http://www.retailerdaily.com/cat/search-engine-marketing/
	if (gb_strcasestr(u->getPath(), "/cat/")){
		if ( note ) *note = "has /cat/"; return false;}
	if (gb_strcasestr(u->getPath(), "/comment.html")){
		if ( note ) *note = "has /comment.html"; return false;}
	if (gb_strcasestr(u->getPath(), "/comments/")){
		if ( note ) *note = "has /comments/"; return false;}
	

	char *pos;
	// category or tag page detection
	pos = gb_strcasestr(u->getUrl(), "cat=");
	if ( pos && pos > u->getUrl() && !is_alpha_a(*(pos-1))){
		if ( note ) *note = "has [A-z]cat="; return false;}
	pos = gb_strcasestr(u->getUrl(), "tag=");
	if ( pos && pos > u->getUrl() && !is_alpha_a(*(pos-1))){
		if ( note ) *note = "has [A-z]tag="; return false;}
	pos = gb_strcasestr(u->getUrl(), "tags=");
	if ( pos && pos > u->getUrl() && !is_alpha_a(*(pos-1))){
		if ( note ) *note = "has [A-z]tags="; return false;}

	// more forum detection
	if (gb_strcasestr(u->getUrl(), "forum")){
		if ( note ) *note = "has forum"; return false;}
	if (gb_strcasestr(u->getPath(), "thread")){
		if ( note ) *note = "has thread";	return false;}
	if (gb_strcasestr(u->getPath(), "topic") &&
	    !gb_strcasestr(u->getPath(), "/topics/")){
		if ( note ) *note = "has /topics/"; return false;}

	// more index page detection
	if (gb_strcasestr(u->getPath(), "/default.")){
		if ( note ) *note = "has /default."; return false;}
	if (gb_strcasestr(u->getPath(), "/profile.")){
		if ( note ) *note = "has /profile."; return false;}
	if (gb_strcasestr(u->getPath(), "/archives.")){
		if ( note ) *note = "has /archives."; return false;}
	if (gb_strcasestr(u->getPath(), "_archive.")){
		if ( note ) *note = "has _archive."; return false;}
	if (gb_strcasestr(u->getPath(), "/search.")){
		if ( note ) *note = "has /search."; return false; }
	if (gb_strcasestr(u->getPath(), "/search/")){
		if ( note ) *note = "has /search/"; return false; }

	// get path end
	p    = u->getPath() + u->getPathLen();
	plen = u->getPathLen();
	// back up over index.html
	if ( plen > 10 && strncmp(p-10,"index.html",10)==0 ) {
		plen -= 10; p -= 10; }
	// hack off the /
	if ( p[-1]=='/' ) { plen--; p--; }

	// ends in /trackback means not a permalink
	if ( plen >= 10 && strncasecmp(p-10,"/trackback",10)==0) {
		if ( note ) *note = "ends in /trackback";
		return false;
	}

	// ends in /dddd/dd means usually an archive date
	if ( plen >= 8 && 
	     is_digit(p[-1]) &&
	     is_digit(p[-2]) &&
	     p[-3] == '/'    &&
	     is_digit(p[-4]) &&
	     is_digit(p[-5]) &&
	     is_digit(p[-6]) &&
	     is_digit(p[-7]) &&
	     p[-8] == '/'      ) {
		     // ensure the numbers are in range for a date
		     int32_t year  = atoi(p-7);
		     int32_t month = atoi(p-2);
		     if ( year  > 1990 && year  <= 2015 &&
			  month > 0    && month <= 12    ) {
			     if ( note ) *note = "ends in /dddd/dd/"; 
			     return false; 
		     }
	}

	// /2008 is usually not permalink
	if ( plen >= 5 &&
	     p[-5] == '/' &&
	     p[-4] == '2' &&
	     p[-3] == '0' &&
	     atoi(p-2) < 50 ) {
		if ( note ) *note = "ends in year /20xx";
		return false;
	}
	// /199? too
	if ( plen >= 5 &&
	     p[-5] == '/' &&
	     p[-4] == '1' &&
	     p[-3] == '9' &&
	     atoi(p-2) > 90 ) {
		if ( note ) *note = "ends in year /19xx";
		return false;
	}


	// . look for a repetitive sequence of html tags
	// . each must contain an outlink! there are some blog entries that
	//   have excerpts of an email chain. 
	// . if the repetition intersects the main content section, 
	//   then it is an index page

	// . make sure that SCORES can detect comment sections. very often
	//   the comment is bigger than the main section!!!!

	// . or we can subtract the repetitive sections, and see if we have
	//   any beefy content left over... then we don't have to worry
	//   about comment identification

	// . index tag pairs
	// . look at header tags, div, p, index level # before the pair

	// . find the delimiter between the blurbs
	// . delimiter must touch the beefy content section
	// . go by "strings" of tagids, a tagid of 0 means text i think
	//   but eliminate it if pure punctuation
	// . and have a subtagid field, which is the hash of a tag's attributes
	//   BUT in the case of a text tag, a hash of the alpha chars

	// . how many delimiters can we find that start at level X.

	// . now we are determining 
	if ( note ) *note = "is permalink";

	return true;
}



int32_t getSiteRank ( int32_t sni ) {
	if ( sni <= 0 ) return 0;
	if ( sni <= 1 ) return 1;
	if ( sni <= 2 ) return 2;
	if ( sni <= 3 ) return 3;
	if ( sni <= 4 ) return 4;
	if ( sni <= 5 ) return 5;
	if ( sni <= 9 ) return 6;
	if ( sni <= 19 ) return 7;
	if ( sni <= 39 ) return 8;
	if ( sni <= 79 ) return 9;
	if ( sni <= 200-1 ) return 10;
	if ( sni <= 500-1 ) return 11;
	if ( sni <= 2000-1 ) return 12;
	if ( sni <= 5000-1 ) return 13;
	if ( sni <= 10000-1 ) return 14;
	return 15;
}

