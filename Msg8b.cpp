#include "gb-include.h"

#include "Msg8b.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"


static void gotListWrapper           ( void *state );//, RdbList *list ) ;
static void handleRequest8b           ( UdpSlot *slot, int32_t niceness );
static void gotMulticastReplyWrapper8b( void *state, void *state2 );
static void gotCatRecWrapper          ( void *state );//, CatRec *catrec );
//static void doneSending_ass          ( void *state, UdpSlot *slot );
// JAB: warning abatement
//static void gotMsg8bsReplyWrapper     ( void *state, CatRec *catrec );
//static void gotMsg22sReplyWrapper    ( void *state );
//static void gotMsgcsReplyWrapper     ( void *state, int32_t ip );
static Msg8bListQueue g_msg8bQueue[MSG8BQUEUE_SIZE];
static bool          g_isMsg8bQueueInitialized = false;

bool Msg8b::registerHandler ( ) {
	// . register with udp server
	if ( ! g_udpServer.registerHandler ( 0x8b, handleRequest8b ) )
		return false;
	//if ( ! g_udpServer2.registerHandler ( 0x08, handleRequest8 ) )
	//	return false;
	return true;
}

// . get the CatRec for this url/coll
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . CatRec will be set to default site for "url" using default tagdb file
//   if no site has been defined specifically for "url"
// . updateFlag is added to check if the caller
//   is calling it for read or write(add/update/delete) operation.
//   if updateFlag is true then tagdb cache is not used
bool Msg8b::getCatRec  ( Url     *url              ,
			 char    *coll             , 
			 int32_t     collLen          ,
			 bool     useCanonicalName ,
			 int32_t       niceness         ,
			 CatRec *cr               ,
			 void    *state            ,
			 void   (* callback)(void *state ) ) {
	// clear g_errno
	g_errno = 0;
	// warning
	//if ( ! coll ) log(LOG_LOGIC,"net: NULL collection. msg8b.");
	// store the calling parameters in this class for retrieval by callback
	m_state          = state;
	m_callback       = callback;
	m_url            = url;
	//m_coll           = coll;
	//m_collLen        = collLen;
	m_cr             = cr;
	m_niceness       = niceness;

	bool isIp = m_url->isIp();
	//m_triedIp = isIp;
	// now find the min/max keys so we can call ../rdb/Msg0.h to get a list
	key_t startKey;
	key_t endKey  ;
	
	// normalize the url
	g_catdb.normalizeUrl(url, &m_normalizedUrl);
	m_url = &m_normalizedUrl;
	// make catdb only in the main collection
	//m_coll = g_conf.m_dirColl;
	//m_collLen = gbstrlen(m_coll);
	// catdb uses a dummy collection now, should not be looked at
	//m_coll = "catdb";
	//m_collLen = 5;

	//m_collnum = g_collectiondb.getCollnum ( m_coll , m_collLen );

	// . first, try it by canonical domain name
	// . if that finds no matches, then try it by ip domain
	g_catdb.getKeyRange ( isIp, m_url, &startKey, &endKey );
	
	// get the groupid
	//m_groupId = startKey.n1 & g_hostdb.m_groupMask;
	m_shardNum = getShardNum ( RDB_CATDB , &startKey );
	
	// reset the xml's in case they were already set
	m_cr->reset();


	//
	// forward
	//
	if ( getMyShardNum() != m_shardNum ) {//g_hostdb.m_groupId!=m_groupId){
		// coll, url, niceness(1), rdbid(1), useCanonicalName(1)
		int32_t requestSize = m_url->getUrlLen() + 4 + 3;
		// make the request
		char *p = m_request;
		*(int32_t *)p = m_url->getIp()     ; p+=4;
		//*p      = RDB_CATDB             ; p++;
		*p      = (char)niceness        ; p++;
		*p      = (char)useCanonicalName; p++;
		// coll
		//gbmemcpy(p, m_coll, m_collLen);
		//p      += m_collLen;
		//*p      = '\0';
		//p++;
		// url
		gbmemcpy(p, m_url->getUrl(), m_url->getUrlLen());
		 p     += m_url->getUrlLen();
		*p      = '\0';
		p++;
		// size and check
		m_requestSize = p - m_request;
		if ( m_requestSize != requestSize ) {
			log ( "Msg8b: request size %"INT32" != %"INT32", bad engineer.",
			      m_requestSize, requestSize );
			char *xx = NULL; *xx = 0;
		}
		QUICKPOLL(m_niceness);
		
		// send the group request
		if ( ! m_mcast.send ( m_request,
				      m_requestSize,
				      0x8b,
				      false,         // multicase own request?
				      m_shardNum,//m_groupId,
				      false,         // send to whole group?
				      startKey.n1,   // key
				      this,          // state data
				      NULL,          // state data
				      gotMulticastReplyWrapper8b,
				      3600*24*365,   // timeout, one year
				      m_niceness,
				      false,         // realtime
				      -1,	     // firstHostId
				      NULL,          // reply buf
				      0,             // reply size
				      true,          // free reply buf?
				      true,          // disk load balance?
				      0,             // max cache age
				      0,             // cache key
				      RDB_CATDB,
				      -1         ) ) // read size
			return true;
		return false;
	}

	//
	// local lookup
	//
	// min rec sizes
	//int32_t minRecSizes = 256*1024;
	// blogspot.com was not showing up! make this 1MB -- MDW
	
	// get min rec sizes from the original collection
	//CollectionRec *cr = g_collectiondb.getRec ( m_coll ,
	//					    m_collLen );
	int32_t minRecSizes = g_conf.m_catdbMinRecSizes;

	// reset the list completely
	//m_list.reset();
	// if url's canonical hostname is an ip try lookup by ip domain only
	// this is checked above
	//if ( m_url->isIp() ) return getCatRecByIp();
	m_localList.reset();
	m_list = &m_localList;
	// if we should NOT lookup based on cannoical name then try ip here
	if ( ! useCanonicalName ) {
		//m_localList.reset();
		//m_list        = &m_localList;
		m_queueMaster = false;
		m_queueSlave  = false;
		m_queueSlot   = -1;
		return gotList();
	}
	// if url has no ip do a warning
	//if (!url->hasIp()) log(0,"Msg8b::getCatRec: warning: url has no ip");
	QUICKPOLL(m_niceness);
	
	// check the queue for the desired list
	if ( !checkQueueForList ( startKey.n1 ) ) {
		// . summon the powerful Msg0(extracts lists from remote rdb's)
		// . store the candidate NORMAL tagdb recs in the list in
		//   rec itself so we don't have to copy from the list
		if ( ! m_msg0.getList (
				-1       , // hostId
				0        , // host ip
				0        , // host port        ,
				0        , // max cached age in seconds (60)
			        false    , // add net recv'd list to cache?
				RDB_CATDB, // specifies the rdb, 1 = tagdb
				0,//collnum"",//NULL,//m_coll   ,
				//&m_list  ,
				m_list   ,
				startKey ,
				endKey   ,
				minRecSizes, // minRecSizes(TODO: make bigger?)
				this     , // state
				gotListWrapper  , // callback
				m_niceness      , // niceness
				true            , // doErrorCorrection
				true            , // includeTree
				true            , // doMerge
				-1              , // firstHostId
				0               , // startFileNum
				-1              , // numFiles,
				3600*24*365     , // timeout, one year
				-1              , // syncPoint
				1               ) ) // prefer local reads
			return false;
				
		// first allow slaves to process with the list
		if ( m_queueMaster )
			processSlaves();

		// . this should set m_cr no matter what
		// . sets g_errno on failure
		if (!gotList())
			return false;
		// get indirect catids
		getIndirectCatids();
		
		// done, clean up master slot
		if ( m_queueMaster )
			cleanSlot();
		return true;
	}
	// attached to queue, wait for the master
	return false;
}

/*
bool Msg8b::getCatRecByIp ( ) {
	// now find the min/max keys so we can call ../rdb/Msg0.h to get a list
	key_t startKey;
	key_t endKey  ;
	// so we don't try again forever
	m_triedIp = true;
	// now try the lookup by ip domain
	g_catdb.getKeyRange (true,m_url,&startKey,&endKey);


	// check the queue for the desired list
	if ( !checkQueueForList ( startKey.n1 ) ) {
		// . summon the powerful Msg0(extracts lists from remote rdb's)
		// . store the candidate NORMAL tagdb recs in the list in
		//   rec itself so we don't have to copy from the list
		if ( ! m_msg0.getList ( -1       , // hostId
				0        , // host ip
				0        , // host port        ,
				0        , // max cached age in seconds (60)
				false    , // add net recv'd list to cache?
				RDB_CATDB  , // specifies the rdb, 1 = tagdb
				m_coll   ,
				//&m_list  ,
				m_list   ,
				startKey ,
				endKey   ,
				1024*64  , // minRecSizes (TODO: make bigger?)
				this     , // state
				gotListWrapper  , // callback
				m_niceness      ) ) // niceness
			return false;
		// first allow slaves to process with the list
		if ( m_queueMaster )
			processSlaves();
		
		// . this should set m_xml and m_xmlLen appropriately
		// . sets g_errno on failure
		//return gotList();
		if (!gotList())
			return false;
		// get indirect catids
		getIndirectCatids();
		
		// done, clean up master slot
		if ( m_queueMaster )
			cleanSlot();

		return true;
	}
	// attached to queue, wait for the master
	return false;
}
*/

void gotListWrapper ( void *state ) { //, RdbList *list ) {
	Msg8b *THIS = (Msg8b *) state;
	// first allow slaves to process with the list
	if ( THIS->m_queueMaster )
		THIS->processSlaves();
	// return if this blocks
	if ( ! THIS->gotList() ) return;
	// get indirect catids 
	THIS->getIndirectCatids();
	
	// done, clean up master slot
	if ( THIS->m_queueMaster )
		THIS->cleanSlot();
	// otherwise give control back to the caller's callback -- we're done
	THIS->m_callback ( THIS->m_state );//, THIS->m_cr );
}

void gotMulticastReplyWrapper8b ( void *state , void *state2 ) {
	Msg8b *THIS = (Msg8b*)state;
	THIS->gotReply ( );
	THIS->m_callback ( THIS->m_state );//, THIS->m_cr );
}

// . get the site rec from the reply
void Msg8b::gotReply ( ) {
	// check for error
	if ( g_errno ) {
		log ( "Msg8b: Reply had error: %s", mstrerror(g_errno));
		return;
	}
	int64_t startTime = gettimeofdayInMilliseconds();
	// get the reply
	int32_t replySize;
	int32_t replyMaxSize;
	bool freeit;
	char *reply = m_mcast.getBestReply ( &replySize,
					     &replyMaxSize,
					     &freeit );
	relabel( reply, replyMaxSize, "Msg8b-GBR" );
	//if the replysize is 0, then give an error
	//actually g_errno should already be set.
	if ( !reply || replySize <= 0 )
		g_errno = EBADREPLY;

	// set the site rec with the reply and original url
	else if (reply && replySize > 0) {
		// deserialize
		char *p = reply;
		int32_t  dataSize = *(int32_t*)p;     p += 4;
		char *data     =         p;     p += dataSize;
		bool  gotByIp  =        *p;     p++;
		bool  hadRec   =        *p;     p++;
		int32_t  numIndCatids = *(int32_t*)p; p+=4;
		int32_t *indCatids = (int32_t*)p;     p += numIndCatids*4;
		
		// sanity check
		if (p - reply != replySize) {
			log("Msg8b: Deserialized reply size %"INT32" "
			    "!= %"INT32"",
			    (int32_t)(p - reply), replySize );
			char *xx = NULL; *xx = 0;
		}
		QUICKPOLL(m_niceness);

		// get site file num and catids from reply
		m_cr->set ( m_url ,
			    data ,
			    dataSize ,
			    gotByIp ); // gotByIp?
		m_cr->m_hadRec = hadRec;
		// set the indirect catids
		m_cr->setIndirectCatids(indCatids, numIndCatids);
		
		// we have to free it
		mfree ( reply , replyMaxSize , "Msg8b" );
	}
	int64_t now = gettimeofdayInMilliseconds();
	int64_t msg8bTook = now - startTime;
	if(msg8bTook > 10)
		log(LOG_INFO, "admin: gotreply for msg8b took %"INT64"", 
		    msg8bTook);
	
}

class State08b {
public:
	Msg8b       m_msg8b;
	CatRec     m_catrec;
	UdpSlot    *m_slot;
	UdpServer  *m_us;
	int32_t        m_niceness;
	//char        m_rdbId;
	Url         m_url;
};

// . request for a CatRec
// . must call g_udpServer.senReply() or sendErrorReply()
void handleRequest8b ( UdpSlot *slot, int32_t netnice ) {
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	//if ( netnice == 0 ) us = &g_udpServer2;
	// get the request
	char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	// parse the request
	char *p       = request;
	int32_t ip       = *(int32_t *)p ; p+=4;
	//char rdbId    = *p         ; p++;
	int32_t niceness = (int32_t)*p   ; p++;
	bool useCanonicalName =  *p; p++;
	// coll
	//char *coll    = p;
	//int32_t  collLen = gbstrlen(coll);
	//p += collLen + 1;
	// url
	char *url     = p;
	int32_t  urlLen  = gbstrlen(url);
	p += urlLen + 1;
	// sanity check
	if (p - request != requestSize) {
		log("build: Msg8b: Read Request Size %"INT32" != %"INT32", "
		    "bad engineer.",
		    (int32_t)(p - request), requestSize);
		char *xx = NULL; *xx = 0;
	}
	// create the state
	State08b *st8b;
	try { st8b = new (State08b); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("Msg8b: new(%i): %s", 
		    (int)sizeof(State08b),mstrerror(g_errno));
		us->sendErrorReply ( slot, g_errno ); 
		return; 
	}
	mnew ( st8b , sizeof(State08b) , "Msg8b" );
	// fill the state
	st8b->m_slot     = slot;
	st8b->m_us       = us;
	st8b->m_niceness = niceness;
	//st8b->m_rdbId    = rdbId;
	st8b->m_url.set(url, urlLen,false);
	st8b->m_url.setIp(ip);
	// call the local msg8b to get the site rec
	if ( ! st8b->m_msg8b.getCatRec ( &st8b->m_url,
					 NULL,//coll,
					 0,//collLen,
					 useCanonicalName,
					 niceness,
					 &st8b->m_catrec,
					 (void*)st8b,
					 gotCatRecWrapper ) )
		return;
	// call wrapper
	gotCatRecWrapper ( st8b );//, &st8b->m_catrec );
}

// send the site rec back in the reply
void gotCatRecWrapper ( void *state ) { // , CatRec *catrec ) {
	char *p;
	// state
	State08b *st8b = (State08b*)state;
	// get it
	CatRec *catrec = &st8b->m_catrec;
	// get udp slot and server
	UdpSlot   *slot = st8b->m_slot;
	UdpServer *us   = st8b->m_us;
	// check for error
	if ( g_errno ) {
		mdelete ( st8b , sizeof(State08b) , "Msg8b" );
		delete (st8b);
		us->sendErrorReply(slot, g_errno);
		return;
	}
	// serialize the reply: data, dataSize(4), gotByIp(1), hadRec(1),
	int32_t  dataSize = catrec->m_dataSize + 6;
	// add indirect catids: numIndCatids(4), indCatids
	dataSize += 4 + catrec->m_numIndCatids*4;
	// check if we're bigger than the tmp buf
	char *data     = slot->m_tmpBuf;
	if (dataSize > TMPBUFSIZE) {
		data = (char*)mmalloc(dataSize, "Msg8breply"); 
		if (!data) {
			log("build: Msg8b: Can't allocate %"INT32" bytes for reply.",
			    dataSize);
			// clean up the state
			mdelete ( st8b , sizeof(State08b) , "Msg8b" );
			delete (st8b);
			g_errno = ENOMEM;
			us->sendErrorReply(slot, g_errno);
			return;
		}
	}
	p = data;
	gbmemcpy(p, &catrec->m_dataSize, 4);
	p += 4;
	gbmemcpy(p, catrec->m_data, catrec->m_dataSize);
	p += catrec->m_dataSize;
	gbmemcpy(p, &catrec->m_gotByIp, 1);
	p++;
	gbmemcpy(p, &catrec->m_hadRec, 1);
	p++;
	gbmemcpy(p, &catrec->m_numIndCatids, 4);
	p += 4;
	gbmemcpy(p, catrec->m_indCatids, catrec->m_numIndCatids*4);
	p += catrec->m_numIndCatids*4;

	// sanity check
	if (p - data != dataSize) {
		log("Msg8b: Reply Size %"INT32" != %"INT32"",
		    (int32_t)(p - data), dataSize);
		char *xx = NULL; *xx = 0;
	}
	// clean up the state
	mdelete ( st8b , sizeof(State08b) , "Msg8b" );
	delete (st8b);
	// send the reply
	us->sendReply_ass ( data,
			    dataSize,
			    data,
			    dataSize,
			    slot );
}

// . returns false if blocks, true otherwise
// . sets g_errno on error
// . each normal tagdb record has the following format:
//      templateKey (12 bytes) then non-NULL-terminated site string
bool Msg8b::gotList ( ) {
	// ignore this...
	if ( g_errno == ENOCOLLREC )
		g_errno = 0;
	// return on error
	if (g_errno){
		log("build: Had error getting ruleset record: %s.",
		    mstrerror(g_errno));
		m_list->reset();
		return true;
	}
	// . get the collection rec
	//CollectionRec *cr = g_collectiondb.getRec ( m_coll , m_collLen );
	//int32_t siteFileNum = -1;
	// watch out, if no url just default the damn thing
	if ( m_url->getUrlLen() <= 0 ) {
		// use host name as the site
		Url site;
		site.set ( m_url->getHost() , m_url->getHostLen(),false );
		// no match in tagdb or regular expressions, so use default
		//if ( cr ) siteFileNum = cr->m_defaultSiteFileNum;
		//else      siteFileNum = 0;
		// . use the default site file num as specified by the
		//   collection rec
		// . don't use the url for the site!!
		m_cr->set ( &site , //m_coll , m_collLen , //0,//siteFileNum ,
			    CATREC_CURRENT_VERSION );
		QUICKPOLL(m_niceness);
		// free the list
		m_list->reset();
		return true;
	}


	// set "gotIt" to true if we found a match in this list of tagdb recs	
	char  gotIt = false;

	// record and record size
	int32_t  recSize;
	char *rec;

	//rec = g_catdb->getRec ( &m_list , m_url , &recSize );
	rec = g_catdb.getRec(m_list,m_url,&recSize,NULL,0);//m_coll,m_collLen);

	// if record found then set it and also set gotIt to true
	if ( rec ) {
		// get site file num from "rec"
		m_cr->set ( m_url, rec , recSize ,
			    false ); //m_triedIp /*gotByIp*/);
			// got it
		gotIt = true;
	}

	QUICKPOLL(m_niceness);

	//bool defaultSet = false;

	// free the list
	// don NOT free the list yet, we have to get the INDIRECT catids!
	//m_list->reset();


	// . if we did not find a match, try looking up by ip domain name
	// . turn this off for tagdb lookups for now
	// . we might want to leave it off for performance since i don't 
	//   think it is a good idea to "ban" ips, too risky...
	//if ( ! gotIt && m_url->hasIp() && ! m_triedIp ) 
	//	return getCatRecByIp();

	// we are just catdb, so if we don't got it now, don't check url
	// filters table
	return true;
}

// get indirect catids for catdb
void Msg8b::getIndirectCatids ( ) {
	// get the indirect catids
	char *matchRecs[MAX_IND_CATIDS];
	int32_t  matchRecSizes[MAX_IND_CATIDS];
	int32_t  numMatches = g_catdb.getIndirectMatches (
					m_list,
					m_url,
					matchRecs,
					matchRecSizes,
					MAX_IND_CATIDS,
					NULL,//m_coll,
					0);//m_collLen);
	// parse out the catids from the matches
	m_cr->m_numIndCatids = 0;
	for ( int32_t i = 0; i < numMatches; i++ ) {
		char *p = matchRecs[i];
		// num catids for this rec
		char numCatids = *p;
		p++;
		// copy the catids over
		char *pend = p + numCatids*4;
		while ( m_cr->m_numIndCatids < MAX_IND_CATIDS &&
			p < pend ) {
			m_cr->m_indCatids[m_cr->m_numIndCatids] = *(int32_t*)p;
			p += 4;
			m_cr->m_numIndCatids++;
		}
	}
}

// . checks the Msg8b queue for the desired list
// . if it exists, it will attach this Msg8b to it and set m_queueSlave
// . if it doesn't, it will setup a new slot in the queue and set
//   m_queueMaster
// . if the queue is full, both master and slave will be false and the
//   local RdbList will be used
// . returns true if attached to queue, false if not and msg0 should
//   be called
bool Msg8b::checkQueueForList ( uint32_t domainHash ) {
	// make sure the queue is initialized
	if ( !g_isMsg8bQueueInitialized ) {
		for ( int32_t i = 0; i < MSG8BQUEUE_SIZE; i++ ) {
			g_msg8bQueue[i].m_list.reset();
			g_msg8bQueue[i].m_numAttached = 0;
			g_msg8bQueue[i].m_domainHash  = 0xffffffff;
			g_msg8bQueue[i].m_isOpen      = 0;
		}
		g_isMsg8bQueueInitialized = true;
	}
	// loop through the queue looking for the domainHash
	int32_t firstOpen = -1;
	Msg8bListQueue *slot;
	for (int32_t i = 0; i < MSG8BQUEUE_SIZE; i++) {
		slot = &g_msg8bQueue[i];
		// check for open slot
		if ( slot->m_domainHash == 0xffffffff ) {
			if ( firstOpen < 0 )
				firstOpen = i;
			continue;
		}
		// check the slot for existing list
		if ( slot->m_domainHash == domainHash &&
		     slot->m_numAttached < MSG8BQUEUE_MAX_ATTACHED &&
		     slot->m_isOpen == 1 ) {
			// become a slave to this slot
			m_queueMaster = false;
			m_queueSlave  = true;
			m_list        = &slot->m_list;
			m_queueSlot   = i;
			slot->m_attachedMsg8bs[slot->m_numAttached] = this;
			slot->m_numAttached++;
			return true;
		}
	}
	// do not put this here because firstOpen is set from the g_msg8bQueue
	// array above, and this may indeed call another 
	// Msg8b::checkQueueForList but instead with niceness 0 and mess up
	// the table. let's make it more atomic
	//QUICKPOLL(m_niceness);

	// . no hit found
	// . if firstOpen < 0, no slots are open, use local RdbList
	if ( firstOpen < 0 ) {
		m_localList.reset();
		m_list        = &m_localList;
		m_queueMaster = false;
		m_queueSlave  = false;
		m_queueSlot   = -1;
	}
	// . otherwise become the master of the open slot
	else {
		slot = &g_msg8bQueue[firstOpen];
		//slot->m_attachedMsg8bs[0] = this;
		//slot->m_numAttached = 1;
		slot->m_domainHash = domainHash;
		slot->m_isOpen     = 1;
		slot->m_masterMsg8b = this;
		m_queueMaster = true;
		m_queueSlave  = false;
		m_list        = &slot->m_list;
		m_queueSlot   = firstOpen;
	}
	// move down here to be safe
	QUICKPOLL(m_niceness);
	return false;
}

void Msg8b::processSlaves() {
	// if a queue master, call the slaves
	if ( !m_queueMaster ) return;
	// . could this grow durring the call? *it could reattach to itself
	//   be careful, close the slot
	Msg8bListQueue *slot = &g_msg8bQueue[m_queueSlot];
	slot->m_isOpen = 0;
	for ( int32_t i = 0; i < slot->m_numAttached; i++ ) {
		Msg8b *slave = slot->m_attachedMsg8bs[i];
		// . call the slave's gotList
		// . if it blocks, it's getting by IP and released
		//   from this queue.
		if (!slave->gotList())
			continue;
		// . otherwise call its callback to finish
		slave->m_callback ( slave->m_state );//, slave->m_cr );
	}
	// clean up this slot
	//slot->m_list.reset();
	//slot->m_numAttached = 0;
	//slot->m_domainHash  = 0xffffffff;
}

void Msg8b::cleanSlot() {
	if ( !m_queueMaster ) return;
	// clean up the master slot
	Msg8bListQueue *slot = &g_msg8bQueue[m_queueSlot];
	slot->m_list.reset();
	slot->m_numAttached = 0;
	slot->m_domainHash  = 0xffffffff;
	slot->m_masterMsg8b  = NULL;
	slot->m_isOpen      = 0;
}
