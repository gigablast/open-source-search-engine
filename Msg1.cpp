#include "gb-include.h"

#include "Msg1.h"
#include "Tfndb.h"
#include "Clusterdb.h"
#include "Spider.h"
//#include "Checksumdb.h"
#include "Datedb.h"
#include "Rdb.h"
//#include "Indexdb.h"
#include "Profiler.h"
#include "Repair.h"

static void gotReplyWrapper1 ( void    *state , void *state2 ) ;
static void handleRequest1   ( UdpSlot *slot  , int32_t niceness ) ;

// . all these parameters should be preset
bool Msg1::registerHandler ( ) {
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( 0x01, handleRequest1 ) )
		return false;
	return true;
}

// . have an array of Msg1s we use for doing no-wait sending
// . this is like the stacks in Threads.cpp
// . BUG: when set to 800 and adding a file of 1 million urls on newspaper
//        archive machines, Multicast bitches about ENOSLOTS, and sometimes
//        udp bitches about no slots available and we don't get all the urls
//        added. so let's decrease from 800 to 20.
#define MAX_MSG1S 100
static Msg1  s_msg1 [ MAX_MSG1S ];
static int32_t  s_next [ MAX_MSG1S ];
static int32_t  s_head = 0 ;
static bool  s_init = false;
static Msg1 *getMsg1    ( ) ;
static void  returnMsg1 ( void *state );
static void  init       ( );
// returns NULL if none left
Msg1 *getMsg1 ( ) {
	if ( ! s_init ) { init(); s_init = true; }
	if ( s_head == -1 ) return NULL;
	int32_t i = s_head;
	s_head = s_next [ s_head ];
	// debug msg
	//log("got mcast=%"INT32"",(int32_t)(&s_msg1[i].m_mcast));
	return &s_msg1[i];
}
void returnMsg1 ( void *state ) {
	Msg1 *msg1 = (Msg1 *)state;
	// free this if we have to
	msg1->m_ourList.freeList();
	// debug msg
	//log("return mcast=%"INT32"",(int32_t)(&msg1->m_mcast));
	int32_t i = msg1 - s_msg1;
	if ( i < 0 || i > MAX_MSG1S ) {
		log(LOG_LOGIC,"net: msg1: Major problem adding data."); 
		char *xx = NULL; *xx = 0; }
	if ( s_head == -1 ) { s_head    = i      ; s_next[i] = -1; }
	else                { s_next[i] = s_head ; s_head    =  i; }
}
void init ( ) {
	// init the linked list
	for ( int32_t i = 0 ; i < MAX_MSG1S ; i++ ) {
		if ( i == MAX_MSG1S - 1 ) s_next[i] = -1;
		else                      s_next[i] = i + 1;
		// these guys' constructor is not called, so do it?
		//s_msg1[i].m_ourList.m_alloc = NULL;
	}
	s_head = 0;
}

bool Msg1::addRecord ( char *rec , 
		       int32_t recSize , 
		       char          rdbId             ,
		       collnum_t collnum ,
		       void         *state             ,
		       void (* callback)(void *state)  ,
		       int32_t          niceness          ) {

	key_t sk;
	key_t ek;
	sk.setMin();
	ek.setMax();
	//RdbList list;
	m_tmpList.set ( rec , 
		   recSize ,
		   rec ,
		   recSize ,
		   (char *)&sk,
		   (char *)&ek,
		   -1 , // fixed data size
		   false , // owndata?
		   false , // use half keys?
		   sizeof(key_t));
	return addList ( &m_tmpList ,
			 rdbId ,
			 collnum,//g_collectiondb.m_recs[collnum]->m_coll ,
			 state ,
			 callback ,
			 false , // force local?
			 niceness );
}

// . send an add command to all machines in the appropriate group
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . groupId is -1 if we choose it automatically
// . if waitForReply is false we return true right away, but we can only
//   launch MAX_MSG1S requests without waiting for replies, and
//   when the reply does come back we do NOT call the callback
bool Msg1::addList ( RdbList      *list              ,
		     char          rdbId             ,
		     collnum_t collnum, // char         *coll              ,
		     void         *state             ,
		     void (* callback)(void *state)  ,
		     bool          forceLocal        ,
		     int32_t          niceness          ,
		     bool          injecting         ,
		     bool          waitForReply      ,
		     bool         *inTransit         ) {
	// warning
	if ( collnum<0 ) log(LOG_LOGIC,"net: bad collection. msg1.cpp.");
	// if list has no records in it return true
	if ( ! list || list->isEmpty() ) return true;
	// sanity check
	if ( list->m_ks !=  8 &&
	     list->m_ks != 12 &&
	     list->m_ks != 16 &&
	     list->m_ks != 24 ) { 
		char *xx=NULL;*xx=0; }
	// start at the beginning
	list->resetListPtr();
	// if caller does not want reply try to accomodate him
	if ( ! waitForReply && list != &m_ourList ) {
		Msg1 *Y = getMsg1();
		if ( ! Y ) { 
			waitForReply = true; 
			log(LOG_DEBUG,"net: msg1: "
			    "No floating request slots "
			    "available for adding data. "
			    "Blocking on reply."); 
			goto skip; 
		}
		// steal the list, we don't want caller to free it
		gbmemcpy ( &Y->m_ourList , list , sizeof(RdbList) );
		
 		QUICKPOLL(niceness);
		
		// if list is small enough use our buf
		if ( ! list->m_ownData && list->m_listSize <= MSG1_BUF_SIZE ) {
			gbmemcpy ( Y->m_buf , list->m_list , list->m_listSize );
			Y->m_ourList.m_list    = Y->m_buf;
			Y->m_ourList.m_listEnd = Y->m_buf + list->m_listSize;
			Y->m_ourList.m_alloc   = NULL;
			Y->m_ourList.m_ownData = false;
		}
		// otherwise, we cannot copy it and i don't want to mdup it...
		else if ( ! list->m_ownData ) {
			log(LOG_LOGIC,"net: msg1: List must own data. Bad "
			    "engineer.");
			char *xx = NULL; *xx = 0; 
		}
		// lastly, if it was a clean steal, don't let list free it
		else list->m_ownData = false;
		// reset m_listPtr and m_listPtrHi so we pass the isExhausted()
		// check in sendSomeOfList() below
		Y->m_ourList.resetListPtr();
		// sanity test
		if ( Y->m_ourList.isExhausted() ) {
			log(LOG_LOGIC,"net: msg1: List is exhausted. "
			    "Bad engineer."); 
			char *xx = NULL; *xx = 0; }
		// now re-call
		bool inTransit;
		bool status = Y->addList ( &Y->m_ourList ,
					   rdbId         ,
					   collnum       ,
					   Y             , // state
					   returnMsg1    , // callback
					   forceLocal    ,
					   niceness      ,
					   injecting     ,
					   waitForReply  ,
					   &inTransit    ) ;
		// if we really blocked return false
		if ( ! status ) return false;
		// otherwise, it may have returned true because waitForReply
		// is false, but the request may still be in transit
		if ( inTransit ) return true;
		// debug msg
		//log("did not block, listSize=%"INT32"",m->m_ourList.m_listSize);
		// we did it without blocking, but it is still in transit
		// unless there was an error
		if ( g_errno ) log("net: Adding data to %s had error: %s.",
				   getDbnameFromId(rdbId),
				   mstrerror(g_errno));
		// otherwise, if not in transit and no g_errno then it must
		// have really completed without blocking. in which case
		// we are done with "Y"
		returnMsg1 ( (void *)Y );
		return true;
	}
 skip:
	// remember these vars
	m_list          = list;
	m_rdbId         = rdbId;
	m_collnum       = collnum;
	m_state         = state;
	m_callback      = callback;
	m_forceLocal    = forceLocal;
	m_niceness      = niceness;
	m_injecting     = injecting;
	m_waitForReply  = waitForReply;

	QUICKPOLL(niceness);
	// reset m_listPtr to point to first record again
	list->resetListPtr();
	// is the request in transit? assume not (assume did not block)
	if ( inTransit ) *inTransit = false;
	// . not all records in the list may belong to the same group
	// . records should be sorted by key so we don't need to sort them
	// . if this did not block, return true
	if ( sendSomeOfList ( ) ) return true;
	// it is in transit
	if ( inTransit ) *inTransit = true;
	// if we should waitForReply return false
	if ( m_waitForReply ) return false;
	// tell caller we did not block on the reply, even though we did
	return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . if the list is sorted by keys this will be the most efficient
bool Msg1::sendSomeOfList ( ) {
	// sanity check
	if ( m_list->m_ks !=  8 &&
	     m_list->m_ks != 12 &&
	     m_list->m_ks != 16 &&
	     m_list->m_ks != 24 ) { 
		char *xx=NULL;*xx=0; }
	// debug msg
	//log("sendSomeOfList: mcast=%"UINT32" exhausted=%"INT32"",
	//    (int32_t)&m_mcast,(int32_t)m_list->isExhausted());
 loop:
	// return true if list exhausted and nothing left to add
	if ( m_list->isExhausted() ) return true;
	// get key of the first record in the list
	//key_t firstKey = m_list->getCurrentKey();
	char firstKey[MAX_KEY_BYTES];
	m_list->getCurrentKey(firstKey);
 	QUICKPOLL(m_niceness);
	// get groupId from this key
	//uint32_t groupId ; 
	// . use the new Hostdb.h inlined function
	uint32_t shardNum = getShardNum ( m_rdbId , firstKey );
	// . default is to use top bits of the key
	// . but if we're adding to titledb use last bits in the top of key
	// . but if we're adding to spiderdb we use the last int32_t in the key
	// . tfndb urlRec key same as titleRec key
	/*
	if      ( m_rdbId == RDB_INDEXDB )
		groupId = g_indexdb.getGroupIdFromKey((key_t *)firstKey);
	else if ( m_rdbId == RDB_DATEDB )
		groupId = g_datedb.getGroupIdFromKey((key128_t *)firstKey);
	else if ( m_rdbId == RDB_TITLEDB)
		groupId = g_titledb.getGroupIdFromKey((key_t *)firstKey);
	else if ( m_rdbId == RDB_CHECKSUMDB)
		groupId = g_checksumdb.getGroupId ( firstKey );
	else if ( m_rdbId == RDB_SPIDERDB )
		groupId = g_spiderdb.getGroupId ( (key_t *)firstKey );
	else if ( m_rdbId == RDB_TFNDB )
		groupId = g_tfndb.getGroupId    ( (key_t *)firstKey );
	else if ( m_rdbId == RDB_CLUSTERDB )
		groupId = g_clusterdb.getGroupIdFromKey((key_t *)firstKey);

	else if ( m_rdbId == RDB2_INDEXDB2 )
		groupId = g_indexdb.getGroupIdFromKey((key_t *)firstKey);
	else if ( m_rdbId == RDB2_DATEDB2 )
		groupId = g_datedb.getGroupIdFromKey((key128_t *)firstKey);
	else if ( m_rdbId == RDB2_TITLEDB2)
		groupId = g_titledb.getGroupIdFromKey((key_t *)firstKey);
	else if ( m_rdbId == RDB2_CHECKSUMDB2)
		groupId = g_checksumdb.getGroupId ( firstKey );
	else if ( m_rdbId == RDB2_SPIDERDB2 )
		groupId = g_spiderdb.getGroupId ( (key_t *)firstKey );
	else if ( m_rdbId == RDB2_TFNDB2 )
		groupId = g_tfndb.getGroupId    ( (key_t *)firstKey );
	else if ( m_rdbId == RDB2_CLUSTERDB2 )
		groupId = g_clusterdb.getGroupIdFromKey((key_t *)firstKey);
	//else    groupId=firstKey.n1 & g_hostdb.m_groupMask;
	else    groupId = (((key_t *)firstKey)->n1) & g_hostdb.m_groupMask;
	*/
	// point to start of data we're going to send
	char *dataStart = m_list->getListPtr();
	// how many records belong to the same group as "firstKey"
	//key_t key;
	char key[MAX_KEY_BYTES];
	while ( ! m_list->isExhausted() ) {
		//key = m_list->getCurrentKey();
		m_list->getCurrentKey(key);
#ifdef GBSANITYCHECK
		// no half bits in here!
		// debug point
		if ( m_list->useHalfKeys() && 
		     m_list->isHalfBitOn ( m_list->getCurrentRec() ) )
			log(LOG_LOGIC,"net: msg1: Got half bit. Bad "
			    "engineer.");
#endif
		// . if key belongs to same group as firstKey then continue
		// . titledb now uses last bits of docId to determine groupId
		// . but uses the top 32 bits of key still
		// . spiderdb uses last 64 bits to determine groupId
		// . tfndb now is like titledb(top 32 bits are top 32 of docId)
		//if ( getGroupId(m_rdbId,key) != groupId ) goto done;
		if ( getShardNum(m_rdbId,key) != shardNum ) goto done;
		/*
		switch ( m_rdbId ) {
		case RDB_TITLEDB: 
			if(g_titledb.getGroupIdFromKey((key_t *)key)!=groupId) 
			goto done;
			break;
		case RDB_CHECKSUMDB:
			if(g_checksumdb.getGroupId (         key)!=groupId)
			goto done;
			break;
		case RDB_SPIDERDB: 
			if ( g_spiderdb.getGroupId ((key_t *)key) != groupId) 
			goto done;
			break;
		case RDB_TFNDB:	
			if ( g_tfndb.getGroupId    ((key_t *)key) != groupId) 
			goto done;
			break;
		case RDB_CLUSTERDB:
		      if(g_clusterdb.getGroupIdFromKey((key_t *)key)!=groupId) 
			goto done;
			break;
		case RDB_DATEDB:
		       if(g_datedb.getGroupIdFromKey((key128_t *)key)!=groupId)
			goto done;
			break;
		case RDB_INDEXDB:
		       if(g_indexdb.getGroupIdFromKey((key_t *)key)!=groupId)
			goto done;
			break;
		//default:if ((key.n1&g_hostdb.m_groupMask)  != groupId) 
		default:  if ( ((((key_t *)key)->n1) & g_hostdb.m_groupMask) !=
			       groupId) 
			goto done;
		}
		*/
		// . break so we don't send more than MAX_DGRAMS defined in 
		//   UdpServer.cpp.
		// . let's boost it from 16k to 64k for speed
		if ( m_list->getListPtr() - dataStart > 64*1024 ) goto done;
		// . point to next record
		// . will point passed records if no more left!
 		QUICKPOLL(m_niceness);
		//int32_t crec = m_list->getCurrentRecSize();
		m_list->skipCurrentRecord();
		// sanity check
		if ( m_list->m_listPtr > m_list->m_listEnd ) {
			char *xx=NULL;*xx=0; }
	}
 done:
	// now point to the end of the data
	char *dataEnd = m_list->getListPtr();
	// . if force local is true we force the data to be added locally
	// . this fixes the bug we had from spiderdb since a key got corrupted
	//   just enough to put it into a different groupId (but not out
	//   of order) so we couldn't delete it cuz our delete keys would go
	//   elsewhere
	if ( m_forceLocal && shardNum != getMyShardNum() &&
	     ! g_conf.m_interfaceMachine ) {
		// make the groupId local, our group
		//groupId = g_hostdb.m_groupId;
		// bitch about this to log it
		log("net: Data does not belong in shard %"UINT32", but adding "
		    "to %s anyway. Probable data corruption.",
		    (uint32_t)shardNum,getDbnameFromId(m_rdbId));
	}
	
 	QUICKPOLL(m_niceness);

	// sanity test for new rdbs
	if ( m_list->m_fixedDataSize != getDataSizeFromRdbId(m_rdbId) ) {
		char *xx=NULL;*xx=0; }

	// little debug thing for genCatdb from msg9b's huge list add
	//if ( m_list->m_listSize > 10000000 )
	//	log("msg1: adding chunk @ %"INT32" of %"INT32" bytes",
	//	    (int32_t)(dataStart - m_list->m_list) ,
	//	    (int32_t)m_list->m_listSize );

	// . now send this list to the host
	// . this returns false if blocked, true otherwise
	// . it also sets g_errno on error
	// . if it blocked return false
	if ( ! sendData ( shardNum , dataStart , dataEnd - dataStart ) )
		return false;
	// if there was an error return true
	if ( g_errno ) return true;
	// otherwise, keep adding
	goto loop;
}

// . return false if blocked, true otherwise
// . sets g_errno on error
bool Msg1::sendData ( uint32_t shardNum, char *listData , int32_t listSize) {
	// debug msg
	//log("sendData: mcast=%"UINT32" listSize=%"INT32"",
	//    (int32_t)&m_mcast,(int32_t)listSize);

	// bail if this is an interface machine, don't write to the main
	if ( g_conf.m_interfaceMachine ) return true;
	// return true if no data
	if ( listSize == 0 ) return true;
	// how many hosts in this group
	//int32_t numHosts = g_hostdb.getNumHostsPerShard();
	// . NOTE: for now i'm removing this until I handle ETRYAGAIN errors
	//         properly... by waiting and retrying...
	// . if this is local data just for us just do an addList to OUR rdb
	/*
	if ( groupId == g_hostdb.m_groupId  && numHosts == 1 ) {
		// this sets g_errno on error
		Msg0 msg0;
		Rdb *rdb = msg0.getRdb ( (char) m_rdbId );
		if ( ! rdb ) return true;
		// make a list from this data
		RdbList list;
		list.set (listData,listSize,listSize,rdb->getFixedDataSize(),
			  false) ; // ownData?
		// this returns false and sets g_errno on error
		rdb->addList ( &list );
		// . if we got a ETRYAGAIN cuz the buffer we add to was full
		//   then we should sleep and try again!
		// . return false cuz this blocks for a period of time
		//   before trying again
		if ( g_errno == ETRYAGAIN ) {
			// try adding again in 1 second
			registerSleepCallback ( 1000, slot, tryAgainWrapper1 );
			// return now
			return false;
		}
		// . always return true cuz we did not block
		// . g_errno may be set
		return true;
	}
	*/
	// if the data is being added to our group, don't send ourselves
	// a msg1, if we can add it right now
	// MDW: crap this is getting ETRYAGAIN and it isn't being tried again
	// i guess and Spider.cpp fails to add to doledb but the doleiptable
	// maintains a positive count, thereby hanging the spiders. let's
	// just always go through multicast so it will auto-retry ETRYAGAIN
	/*
	bool sendToSelf = true;
	if ( shardNum == getMyShardNum() &&
	     ! g_conf.m_interfaceMachine ) {
		// get the rdb to which it belongs, use Msg0::getRdb()
		Rdb *rdb = getRdbFromId ( (char) m_rdbId );
		if ( ! rdb ) goto skip;
		// key size
		int32_t ks = getKeySizeFromRdbId ( m_rdbId );
		// reset g_errno
		g_errno = 0;
		// . make a list from this data
		// . skip over the first 4 bytes which is the rdbId
		// . TODO: embed the rdbId in the msgtype or something...
		RdbList list;
		// set the list
		list.set ( listData ,
			   listSize ,
			   listData ,
			   listSize ,
			   rdb->getFixedDataSize() ,
			   false                   ,  // ownData?
			   rdb->useHalfKeys()      ,
			   ks                      ); 
		// note that
		//log("msg1: local addlist niceness=%"INT32"",m_niceness);
		// this returns false and sets g_errno on error
		rdb->addList ( m_coll , &list , m_niceness );
		// if titledb, add tfndb recs to map the title recs
		//if ( ! g_errno && rdb == g_titledb.getRdb() && m_injecting ) 
		//	// this returns false and sets g_errno on error
		//	updateTfndb ( m_coll , &list , true , m_niceness);
		// if no error, no need to use a Msg1 UdpSlot for ourselves
		if ( ! g_errno ) sendToSelf = false;
		else {
			log("rdb: msg1 coll=%s rdb=%s had error: %s",
			    m_coll,rdb->m_dbname,mstrerror(g_errno));
			// this is messing up generate catdb's huge rdblist add
			// why did we put it in there??? from msg9b.cpp
			//return true;
		}
		
 		QUICKPOLL(m_niceness);
		// if we're the only one in the group, bail, we're done
		if ( ! sendToSelf &&
		     g_hostdb.getNumHostsPerShard() == 1 ) return true;
	}
skip:
	*/
	// . make an add record request to multicast to a bunch of machines
	// . this will alloc new space, returns NULL on failure
	//char *request = makeRequest ( listData, listSize, groupId , 
	//m_rdbId , &requestLen );
	//int32_t collLen = gbstrlen ( m_coll );
	// . returns NULL and sets g_errno on error
	// . calculate total size of the record
	// . 1 byte for rdbId, 1 byte for flags,
	//   then collection NULL terminated, then list
	int32_t requestLen = 1 + 1 + sizeof(collnum_t) + listSize ;
	// make the request
	char *request = (char *) mmalloc ( requestLen ,"Msg1" );
	if ( ! request ) return true;
	char *p = request;
	// store the rdbId at top of request
	*p++ = m_rdbId;
	// then the flags
	*p = 0;
	if ( m_injecting ) *p |= 0x80;
	p++;
	// then collection name
	//gbmemcpy ( p , m_coll , collLen );
	//p += collLen;
	//*p++ = '\0';
	*(collnum_t *)p = m_collnum;
	p += sizeof(collnum_t);
	// sanity check
	//if ( collLen <= 0 ) {
	//	log(LOG_LOGIC,"net: No collection specified for list add.");
	//	//char *xx = NULL; *xx = 0;
	//	g_errno = ENOCOLLREC;
	//	return true;
	//}
	//if ( m_deleteRecs    ) request[1] |= 0x80;
	//if ( m_overwriteRecs ) request[1] |= 0x40;
	// store the list after coll
	gbmemcpy ( p , listData , listSize );
 	QUICKPOLL(m_niceness);
	// debug msg
	//if ( ! m_waitForReply ) // (m_rdbId == RDB_SPIDERDB || 
	//m_rdbId == RDB_TFNDB)  )
	//	// if we don't get here we lose it!!!!!!!!!!!!!!!!!!!!!
	//	log("using mcast=%"UINT32" rdbId=%"INT32" listData=%"UINT32" listSize=%"UINT32" "
	//	    "gid=%"UINT32"",
	//	   (int32_t)&m_mcast,(int32_t)m_rdbId,(int32_t)listData,(int32_t)listSize,
	//	    groupId);
	// for small packets
	//int32_t niceness = 2;
	//if ( requestLen < TMPBUFSIZE - 32 ) niceness = 0;
	//log("msg1: sending mcast niceness=%"INT32"",m_niceness);
	// . multicast to all hosts in group "groupId"
	// . multicast::send() returns false and sets g_errno on error
	// . we return false if we block, true otherwise
	// . will loop indefinitely if a host in this group is down
	key_t k; k.setMin();
	if ( m_mcast.send ( request    , // sets mcast->m_msg    to this
			    requestLen , // sets mcast->m_msgLen to this
			    0x01       , // msgType for add rdb record
			    true       , // does multicast own msg?
			    shardNum   , // group to send to (groupKey)
			    true       , // send to whole group?
			    0          , // key is useless for us
			    this       , // state data
			    NULL       , // state data
			    gotReplyWrapper1 ,
			    60         , // timeout in secs
			    m_niceness , // niceness 
			    false    , // realtime
			    -1    , // first host to try
			    NULL  , // replyBuf        = NULL ,
			    0     , // replyBufMaxSize = 0 ,
			    true  , // freeReplyBuf    = true ,
			    false , // doDiskLoadBalancing = false ,
			    -1    , // no max cache age limit
			    //(key_t)0 , // cache key
			    k    , // cache key
			    RDB_NONE , // bogus rdbId
			    -1    , // unknown minRecSizes read size
			    true )) // sendToSelf ))
		return false;

 	QUICKPOLL(m_niceness);
	// g_errno should be set
	log("net: Had error when sending request to add data to %s in shard "
	    "#%"UINT32": %s.", getDbnameFromId(m_rdbId),shardNum,mstrerror(g_errno));
	return true;	
}

// . this should only be called by m_mcast when it has successfully sent to
//   ALL hosts in group "groupId"
void gotReplyWrapper1 ( void *state , void *state2 ) {
	Msg1 *THIS = (Msg1 *)state;
	// print the error if any
	if ( g_errno && g_errno != ETRYAGAIN ) 
		log("net: Got bad reply when attempting to add data "
		    "to %s: %s",getDbnameFromId(THIS->m_rdbId),
		    mstrerror(g_errno));

	//int32_t address = (int32_t)THIS->m_callback;

	// if our list to send is exhausted then we're done!
	if ( THIS->m_list->isExhausted() ) {

		//if(g_conf.m_profilingEnabled){
		//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
		//}
		if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state ); 
		//if(g_conf.m_profilingEnabled){
		//	if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
		//		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
		//		    (int32_t)address);
		//}

		return; 
	}
	// otherwise we got more to send to groups
	if ( THIS->sendSomeOfList() ) {
		//if(g_conf.m_profilingEnabled){
		//	g_profiler.startTimer(address, __PRETTY_FUNCTION__);
		//}
		if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state ); 
		//if(g_conf.m_profilingEnabled){
		//	if(!g_profiler.endTimer(address, __PRETTY_FUNCTION__))
		//		log(LOG_WARN,"admin: Couldn't add the fn %"INT32"",
		//		    (int32_t)address);
		//}
		return; 
	}
}

class State1 {
public:
	UdpSlot     *m_slot;
	Rdb         *m_rdb;
	RdbList      m_list;
};

static void addedList   ( UdpSlot *slot , Rdb *rdb );
//static bool updateTfndb ( RdbList *list , bool isTitledb ) ;

// . destroys the slot if false is returned
// . this is registered in Msg1::set() to handle add rdb record msgs
// . seems like we should always send back a reply so we don't leave the
//   requester's slot hanging, unless he can kill it after transmit success???
// . TODO: need we send a reply back on success????
// . NOTE: Must always call g_udpServer::sendReply or sendErrorReply() so
//   read/send bufs can be freed
void handleRequest1 ( UdpSlot *slot , int32_t netnice ) {


	// extract what we read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;
	int32_t niceness = slot->m_niceness;

	// select udp server based on niceness
	UdpServer *us = &g_udpServer;
	// must at least have an rdbId
	if ( readBufSize <= 4 ) {
		g_errno = EREQUESTTOOSHORT;
		us->sendErrorReply ( slot , g_errno );
		return;
	}
	char *p    = readBuf;
	char *pend = readBuf + readBufSize;
	// extract rdbId
	char rdbId = *p++;
	// get the rdb to which it belongs, use Msg0::getRdb()
	Rdb *rdb = getRdbFromId ( (char) rdbId );
	if ( ! rdb ) { us->sendErrorReply ( slot, EBADRDBID ); return;}
	// keep track of stats
	rdb->readRequestAdd ( readBufSize );
	// reset g_errno
	g_errno = 0;
	// are we injecting some title recs?
	bool injecting;
	if ( *p & 0x80 ) injecting = true;
	else             injecting = false;
	p++;
	// then collection
	//char *coll = p;
	//p += gbstrlen (p) + 1;
	collnum_t collnum = *(collnum_t *)p;
	p += sizeof(collnum_t);
	// . make a list from this data
	// . skip over the first 4 bytes which is the rdbId
	// . TODO: embed the rdbId in the msgtype or something...
	RdbList list;
	// set the list
	list.set ( p        , // readBuf     + 4         ,
		   pend - p , // readBufSize - 4         ,
		   p        , // readBuf     + 4         ,
		   pend - p , // readBufSize - 4         ,
		   rdb->getFixedDataSize() ,
		   false                   ,  // ownData?
		   rdb->useHalfKeys()      ,
		   rdb->getKeySize ()      ); 
	// note it
	//log("msg1: handlerequest1 calling addlist niceness=%"INT32"",niceness);
	//log("msg1: handleRequest1 niceness=%"INT32"",niceness);
	// this returns false and sets g_errno on error
	rdb->addList ( collnum , &list , niceness);
	// if titledb, add tfndb recs to map the title recs
	//if ( ! g_errno && rdb == g_titledb.getRdb() && injecting ) 
	//	updateTfndb ( coll , &list , true, 0);
	// but if deleting a "new" and unforced record from spiderdb
	// then only delete tfndb record if it was tfn=255
	//if ( ! g_errno && rdb == g_spiderdb.getRdb() )
	//	updateTfndb2 ( coll , &list , false );
	// retry on some errors
	addedList ( slot , rdb );
}

// . this code is taken from RdbDump::updateTfndb() so when injecting a url
//   we have an entry for it in tfndb
// . returns false and sets g_errno on error
// . Sync.cpp may now call this as well
// . OBSOLETE: now handled in Rdb.cpp::addRecord()
/*
bool updateTfndb ( char *coll , RdbList *list , bool isTitledb, 
		   int32_t niceness ) {
	// if this is titledb then add to tfndb first so it doesn't
	// get dumped after we add it
	list->resetListPtr();
	Rdb *udb = g_tfndb.getRdb();
 loop:
	// get next
	if ( list->isExhausted() ) return true;
	// get the TitleRec key
	//key_t k = list->getCurrentKey();
	char k[MAX_KEY_BYTES];
	list->getCurrentKey(k);
	// skip if a delete
	//if ( (k.n0 & 0x01) == 0x00 ) {
	if ( KEYNEG(k) ) {
		// advance for next call
		list->skipCurrentRecord();
		goto loop;
	}
	// make the tfndb record
	key_t ukey;
	if ( isTitledb ) {
		int64_t d = g_titledb.getDocIdFromKey ( (key_t *)k );
		int32_t e = g_titledb.getHostHash ( (key_t *)k );
		ukey = g_tfndb.makeKey ( d, e, 255, false, false );//255=tfn
		//g_tfndb.makeKey ( d, e, 255, false, false , ukey );
	}
	// otherwise spiderdb
	else {
		key_t k        = list->getCurrentKey() ;
		char *data     = list->getCurrentData();
		int32_t  dataSize = list->getCurrentDataSize();
		// is it a delete?
		if ( dataSize == 0 ) {
			// if not a delete, that's weird...
			if ( (k.n0 & 0x01) == 0x01 )
			       log("net: Got mysterious corrupted spiderdb "
				   "record. Ignoring.");
			// don't update tfndb on deletes either
			return true;
		}
		// otherwise we should have a good data size
		SpiderRec sr;
		sr.set ( k , data , dataSize );
		int64_t d = sr.getDocId();
		int32_t e = g_tfndb.makeExt ( sr.getUrl() );
		ukey = g_tfndb.makeKey ( d, e, 255, false, false );//255=tfn
		//g_tfndb.makeKey ( d, e, 255, false, false , ukey );
	}
	// advance for next call
	list->skipCurrentRecord();

	QUICKPOLL(niceness);

	// add it, returns false and sets g_errno on error
	if ( udb->addRecord ( coll , ukey , NULL , 0, niceness) ) goto loop;
	// . at this point, g_errno should be set, since addRecord() failed
	// . return true with g_errno set for most errors, that's bad
	// . why return true on error? always should be false
	if ( g_errno != ETRYAGAIN && g_errno != ENOMEM ) return false;
	// save it
	int32_t saved = g_errno;
	// try starting a dump, Rdb::addRecord() does not do this like it
	// should, only Rdb::addList() does
	if ( udb->needsDump() ) {
		log(LOG_INFO,"net: Dumping tfndb tree to disk.");
		// . CAUTION! must use niceness one because if we go into
		//   urgent mode all niceness 2 stuff will freeze up until
		//   we exit urgent mode! so when tfndb dumps out too much
		//   stuff he'll go into urgent mode and freeze himself
		if ( ! udb->dumpTree ( 1 ) ) // niceness
			log("net: Got error while dumping tfndb tree to disk: "
			    "%s", mstrerror(g_errno));
	}
	// resetore it
	g_errno = saved;
	// return false on error
	return false;
}
*/

static void tryAgainWrapper ( int fd , void *state ) ;

// g_errno may be set when this is called
void addedList ( UdpSlot *slot , Rdb *rdb ) {
	// no memory means to try again
	if ( g_errno == ENOMEM ) g_errno = ETRYAGAIN;
	// doing a full rebuid will add collections
	if ( g_errno == ENOCOLLREC &&
	     g_repairMode > 0 )
	     //g_repair.m_fullRebuild )
		g_errno = ETRYAGAIN;

	// it seems like someone can delete a collection and there can
	// be adds in transit to doledb and it logs
	// "doledb bad collnum of 30110"
	// so just absorb those
	if ( g_errno == ENOCOLLREC ) {
		log("msg1: missing collrec to add to to %s. just dropping.",
		    rdb->m_dbname);
		g_errno = 0;
	}

	// . if we got a ETRYAGAIN cuz the buffer we add to was full
	//   then we should sleep and try again!
	// . return false cuz this blocks for a period of time
	//   before trying again
	// . but now to free the udp slot when we are doing an urgent merge
	//   let's send an error back!
	//if ( g_errno == ETRYAGAIN ) {
		// debug msg
		//log("REGISTERING SLEEP CALLBACK");
		// try adding again in 1 second
	//	g_loop.registerSleepCallback ( 1000, slot, tryAgainWrapper );
		// return now
	//	return;
	//}
	// random test
	//if ( (rand() % 10) == 1 ) g_errno = ETRYAGAIN;
	//int32_t niceness = slot->getNiceness() ;
	// select udp server based on niceness
	UdpServer *us = &g_udpServer ;
	//if ( niceness == 0 ) us = &g_udpServer2;
	//else                 us = &g_udpServer ;
	// chalk it up
	rdb->sentReplyAdd ( 0 );
	// are we done
	if ( ! g_errno ) {
		// . send an empty (non-error) reply as verification
		// . slot should be auto-nuked on transmission/timeout of reply
		// . udpServer should free the readBuf
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot ) ;
		return;
	}
	// on other errors just send the err code back
	us->sendErrorReply ( slot , g_errno );
}

void tryAgainWrapper ( int fd , void *state ) {
	// stop waiting
	g_loop.unregisterSleepCallback ( state , tryAgainWrapper );
	// clear g_errno
	g_errno = 0;
	// get slot
	UdpSlot *slot = (UdpSlot *)state;
	// try adding again
	handleRequest1 ( slot , -2 ); // slot->getNiceness() );
	return;
}

//if split is true, it can still be overrriden by the parm in g_conf
//if false, we don't split index and date lists, other dbs are unaffected
/*
uint32_t getGroupId ( char rdbId , char *key, bool split ) {

	if ( split ) {
	// try to put those most popular ones first for speed
	if      ( rdbId == RDB_INDEXDB )
		return g_indexdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB_DATEDB )
		return g_datedb.getGroupIdFromKey((key128_t *)key);
	else if ( rdbId == RDB_LINKDB )
		return g_linkdb.getGroupId((key128_t *)key);
	else if ( rdbId == RDB_TFNDB )
		return g_tfndb.getGroupId    ( (key_t *)key );
	else if ( rdbId == RDB_TITLEDB)
		return g_titledb.getGroupIdFromKey((key_t *)key);
	//else if ( rdbId == RDB_CHECKSUMDB)
	//	return g_checksumdb.getGroupId ( key );
	else if ( rdbId == RDB_SPIDERDB )
		return g_spiderdb.getGroupId ( (key_t *)key );
	else if ( rdbId == RDB_CLUSTERDB )
		return g_clusterdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB_TAGDB )
		return g_tagdb.getGroupId((key_t *)key);
	else if ( rdbId == RDB_CATDB )
		return g_tagdb.getGroupId((key_t *)key);

	else if ( rdbId == RDB2_INDEXDB2 )
		return g_indexdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB2_DATEDB2 )
		return g_datedb.getGroupIdFromKey((key128_t *)key);
	else if ( rdbId == RDB2_TITLEDB2)
		return g_titledb.getGroupIdFromKey((key_t *)key);
	//else if ( rdbId == RDB2_CHECKSUMDB2)
	//	return g_checksumdb.getGroupId ( key );
	else if ( rdbId == RDB2_SPIDERDB2 )
		return g_spiderdb.getGroupId ( (key_t *)key );
	else if ( rdbId == RDB2_TFNDB2 )
		return g_tfndb.getGroupId    ( (key_t *)key );
	else if ( rdbId == RDB2_CLUSTERDB2 )
		return g_clusterdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB2_LINKDB2 )
		return g_linkdb.getGroupId((key128_t *)key);
	else if ( rdbId == RDB2_SITEDB2 )
		return g_tagdb.getGroupId((key_t *)key);
	else if ( rdbId == RDB2_CATDB2 )
		return g_tagdb.getGroupId((key_t *)key);
	// core -- must be provided
	char *xx = NULL; *xx = 0;
	//groupId=key.n1 & g_hostdb.m_groupMask;
	return (((key_t *)key)->n1) & g_hostdb.m_groupMask;
	}

	if ( rdbId == RDB_INDEXDB )
		return g_indexdb.getNoSplitGroupId((key_t *)key);
	if ( rdbId == RDB_DATEDB )
		return g_datedb.getNoSplitGroupId((key128_t *)key);
	if ( rdbId == RDB2_INDEXDB2 )
		return g_indexdb.getNoSplitGroupId((key_t *)key);
	if ( rdbId == RDB2_DATEDB2 )
		return g_datedb.getNoSplitGroupId((key128_t *)key);
	// nobody else can be "no split" i guess
	char *xx = NULL; *xx = 0;
	return 0;
}
*/
