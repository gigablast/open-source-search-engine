#include "gb-include.h"

#include "Msg36.h"
#include "RdbCache.h"
#include "RequestTable.h"
#include "Posdb.h"

// TODO: if host goes dead then we should not let multicast re-route to its
//       twin in the case of exact counts. because when the dead host comes
//       back up his quota.cache may have obsolete data!!!

//RdbCache g_qtable;

//static bool     s_init = false;

static RequestTable s_requestTableServer36;

static void gotReplyWrapper36 ( void *state , void *state2 ) ;
static void handleRequest36 ( UdpSlot *slot , long netnice ) ;
static void gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) ;
static void gotReplyRequestTableServerEnd ( char *reply  , long replySize , 
					    void *state1 , void *state2   ) ;


bool Msg36::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x36
        if ( ! g_udpServer.registerHandler ( 0x36, handleRequest36 )) 
                return false;
        //if ( ! g_udpServer2.registerHandler ( 0x36, handleRequest36 )) 
        //        return false;
        return true;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "termIds/termFreqs" should NOT be on the stack in case we block
// . i based this on ../titledb/Msg23.cpp 
bool Msg36::getTermFreq ( char      *coll       ,
			  long       maxAge     ,
			  long long  termId     ,
			  void      *state      ,
			  void (* callback)(void *state ) ,
			  long       niceness   ,
			  bool       exactCount ,
			  bool       incCount   ,
			  bool       decCount   ,
			  bool       isSplit) {
	// sanity check
	if ( termId == 0LL ) { 
		g_errno = EBADENGINEER;
		log("quota: msg36: termid is 0.");
		return true;
	}
	// warning
	if ( ! coll ) log(LOG_LOGIC,"quota: msg36: NULL collection.");
	// no more quotas here!
	if ( incCount || decCount ) { char *xx = NULL; *xx = 0; }
	// sanity check
	//if ( incCount && ! exactCount ) { char *xx = NULL; *xx = 0; }
	//if ( decCount && ! exactCount ) { char *xx = NULL; *xx = 0; }
	// sanity check
	//if ( incCount && isSplit ) { char *xx = NULL; *xx = 0; }
	//if ( decCount && isSplit ) { char *xx = NULL; *xx = 0; }
	// cannot call handler asynchronously when doing exact counts...
	//if ( exactCount ) niceness = MAX_NICENESS;
	// keep a pointer for the caller
	m_state    = state;
	m_callback = callback;
	m_termFreq = 0LL;
	m_niceness = niceness;

	m_errno    = 0LL;
	m_isSplit = isSplit;
	// TODO: have a local by-pass for speed
	// if we have this termlist local then we can skip the network stuff
	//if ( g_indexdb.isLocal ( termId ) ) { return getTermFreqLocally(); }

	// make a key from our termId, and if docId is provided, that too.
	key144_t key ;
	g_posdb.makeStartKey ( &key, termId , 0LL );
	// . now what group do we belong to?
	// . groupMask has hi bits set before it sets low bits
	//unsigned long groupId = key.n1 & g_hostdb.m_groupMask;
	//unsigned long groupId;
	/*
	if ( g_hostdb.m_indexSplits > 1 ) 
		groupId = g_indexdb.getBaseGroupId(&key);
	else
		groupId = g_indexdb.getGroupIdFromKey(&key);
	*/
	//groupId = g_indexdb.getNoSplitGroupId(&key);
	uint32_t groupId = getGroupId ( RDB_POSDB , &key , false );
	
	log(LOG_DEBUG,"quota: msg36 termid=%lli inc=%li dec=%li "
	    "sending to group=0x%lx\n",termId,(long)incCount,(long)decCount,
	    (long)groupId);

		//unsigned long groupId = g_indexdb.getBaseGroupId(&key);
	                                    //getGroupIdFromKey ( &key );
	// . what is the ideal hostId based on this key?
	// . this is what multicast does to determine the 1st host to send to
	//if ( groupId == g_hostdb.m_groupId &&
	bool local = true;
	if ( g_hostdb.m_indexSplits != 1   ) local = false;
	if ( groupId != g_hostdb.m_groupId ) local = false;
	//if ( g_conf.m_fullSplit            ) local = true;
	local = true;
	if ( exactCount                    ) local = false;
	//if ( g_hostdb.m_indexSplits == 1 &&
	//     groupId == g_hostdb.m_groupId &&
	//     //!g_conf.m_interfaceMachine &&
	//    !exactCount ) {
	if ( local ) {
		//long numHosts;
		//Host *hosts = g_hostdb.getGroup(g_hostdb.m_groupId,&numHosts);
		//unsigned long i = ((unsigned long)groupId/*key*/) % numHosts;
		// if it's us then no need to multicast to ourselves
		//if(hosts[i].m_hostId==g_hostdb.m_hostId||g_conf.m_fullSplit) {
		m_termFreq = g_posdb.getTermFreq ( coll , termId );
		// clear g_errno
		g_errno = 0;
		return true;
	}

	// . make a request
	// . just send the termId and collection name
	char *p = m_request;
	*p = 0;
	// exact flag
	if ( exactCount ) *p |= 0x01;
	//if ( incCount   ) *p |= 0x02;
	//if ( decCount   ) *p |= 0x04;
	if ( m_niceness ) *p |= 0x08;
	p++;
	*(long long *)p = termId ; p += sizeof(long long);
	strcpy ( p , coll ); p += gbstrlen(coll) + 1; // copy includes \0

	long timeout = 5;
	//if ( incCount || decCount ) timeout = 9999999;
	if ( exactCount           ) timeout = 9999999;

	// . need to send out to all the indexdb split hosts
	m_numRequests = 0;
	m_numReplies  = 0;
	bool blocked  = false;
	// just do one host and multiply his count by the split
	// for now to increase performance
	bool semiExact = true;
	if(!m_isSplit) semiExact = false;
	// send a request for every split
	for ( long i = 0; i < g_hostdb.m_indexSplits; i++ ) {
		long  gr;
		char *buf;
		// semiExact overrides all
		if ( semiExact && g_hostdb.m_indexSplits > 1 ) {
			long nn = (unsigned long)termId % 
				g_hostdb.m_indexSplits;
			// sanity check
			if ( nn < 0 || nn >= g_hostdb.m_indexSplits ) {
				char *xx = NULL; *xx = 0; }
			//gr = g_indexdb.getSplitGroupId ( groupId , nn);
			// need to select the first buffer
			buf = &m_reply[i*8];
			// do not use this!
			char *xx=NULL;*xx=0;
		}			
		else if ( g_hostdb.m_indexSplits > 1 && m_isSplit) {
			//gr  = g_indexdb.getSplitGroupId ( groupId, i );
			buf = &m_reply[i*8];
			// do not use this!
			char *xx=NULL;*xx=0;
		}
		else {
			gr  = groupId;  //this is just the baseGroupId
			buf = m_reply;
		}
		// in case it fails somehow
		*(long long *)buf = 0LL;

		// .  multicast to a host in group
		// . returns false and sets g_errno on error
		if ( ! m_mcast[i].
		     send ( m_request    , 
			    p - m_request, // request size
			    0x36         , // msgType 0x36
			    false        , // multicast owns msg?
			    gr           ,
			    false        , // send to whole group?
			    termId       , // key is termId
			    this         , // state data
			    NULL         , // state data
			    gotReplyWrapper36 ,
			    timeout,
			    //5               , // 5 second timeout
			    m_niceness      ,
			    false           , // realtime?
			    -1              , // first hostid
			    buf             ,
			    8               ,
			    false           ) ) { // free reply buf?
			log("quota: msg36: sending mcast had error: %s",
			    mstrerror(g_errno));
			//return true;
		}
		else {
			m_numRequests++;
			blocked = true;
		}
		// only launch (attempt to launch) one request if semiExact
		if ( semiExact ) break;
		// is we are not split only one host has the termlist
		if ( ! m_isSplit ) break;
		// no inefficient looping! let's nuke this mcast array
		char *xx = NULL; *xx = 0;
	}
	// we blocked on the multicast
	if ( blocked ) return false;
	return true;
}

void gotReplyWrapper36 ( void *state , void *state2 ) {
	Msg36 *THIS = (Msg36 *)state;
	THIS->m_numReplies++;
	if ( g_errno ) THIS->m_errno = g_errno;
	if ( THIS->m_numReplies < THIS->m_numRequests )
		return;
	// gotReply() does not block, and does NOT call our callback
	if ( ! THIS->m_errno ) THIS->gotReply( ) ;
	// call callback since we blocked, since we're here
	THIS->m_callback ( THIS->m_state );
}

void Msg36::gotReply ( ) {
	// . get best reply for multicast
	// . we are responsible for freeing it
	long  replySize;
	long  replyMaxSize;
	bool  freeit;
	// force it to save disk seeks for now
	bool  semiExact = true;
	if(!m_isSplit) semiExact = false;
	// sanity check
	if ( m_termFreq ) { char *xx = NULL; *xx = 0; }
	// add up termfreqs from all replies
	if ( m_isSplit && g_hostdb.m_indexSplits > 1 ) {
		//for ( long i = 0; i < g_hostdb.m_indexSplits; i++ ) {
		for ( long i = 0; i < m_numReplies; i++ ) {
			char *reply = m_mcast[i].getBestReply ( &replySize,
							&replyMaxSize,
							&freeit );
			// sanity check, make sure reply does not breach buf
			if ( replySize > 8 ) { char *xx = NULL; *xx = 0; }
			// if no reply freak out!
			if ( reply != &m_reply[i*8] ) {
				log(LOG_LOGIC,"query: Got bad reply for term "
				      "frequency. Bad.");
				char *xx = NULL; *xx = 0;
			}
			//	long  bufSize = slot->m_readBufSize;
			// buf should have the # of records for m_termId
			else 
				m_termFreq += *(long long *)reply ;
			// the LinkInfo now owns this slot's read buffer,
			// so don't free it
			//mfree ( reply , replySize , "Msg36" );
		}
	}
	else {
		// . get best reply for multicast
		// . we are responsible for freeing it
		long  replySize;
		long  replyMaxSize;
		bool  freeit;
		char *reply = m_mcast[0].getBestReply(&replySize,
				&replyMaxSize,&freeit);
		// if no reply freak out!
		if ( reply != m_reply ) 
			log(LOG_LOGIC,"query: Got bad reply for term "
				      "frequency. Bad.");
		//	long  bufSize = slot->m_readBufSize;
		// buf should have the # of records for m_termId
		m_termFreq = *(long long *)m_reply ;
		// the LinkInfo now owns this slot's read buffer, 
		// so don't free it
		//mfree ( reply , replySize , "Msg36" );
	}
	// since we are now forcing, multiply
	if ( semiExact && g_hostdb.m_indexSplits > 1 )
		m_termFreq *= g_hostdb.m_indexSplits;
	//log(LOG_WARN,"msg36: term freq is %li",m_termFreq);
}

class State36 {
public:
	long long  m_termId   ;
	collnum_t  m_collnum  ;
	//bool       m_incCount ;
	//bool       m_decCount ;
	Msg5       m_msg5;
	RdbList    m_list;
	long long  m_oldListSize;
	long long  m_requestHash;
	char      *m_recPtr;
	long       m_niceness;
};

static void callMsg5 ( State36 *st , key144_t startKey , key144_t endKey  );

//we don't need MRS to be 200 megs, 5 megs should be enuf for most sites
//#define MRS (200*1024*1024)
#define MRS (5 * 1024 * 1024)

#define MAX_AGE (7*24*3600)

// . handle a request to get a linkInfo for a given docId/url/collection
// . returns false if slot should be nuked and no reply sent
// . sometimes sets g_errno on error
void handleRequest36 ( UdpSlot *slot , long netnice ) {
	// get the request
        char *request     = slot->m_readBuf;
        long  requestSize = slot->m_readBufSize;

        // ensure it's size
        if ( requestSize <= 9 ) {
		log("query: Got bad request size of %li for term frequency.",
		    requestSize);
                g_udpServer.sendErrorReply ( slot , EBADREQUESTSIZE ); 
		return;
	}

	// get the termId we need the termFreq for
	char exactCount = false;
	//char incCount   = false;
	//char decCount   = false;
	long niceness   = 0;
	if ( *request & 0x01 ) exactCount = true;
	//if ( *request & 0x02 ) incCount   = true;
	//if ( *request & 0x04 ) decCount   = true;
	if ( *request & 0x08 ) niceness   = MAX_NICENESS;
	long long  termId = *(long long *) (request+1) ; 
	char      *coll   = request + 8 + 1;

	// if there is no way this termlist size exceeds exactMax, then just
	// return the approximation we got, saves on disk seeks
	if ( ! exactCount ) {//&& ! incCount && ! decCount ) { //max<exactMax){
		long long termFreq = g_posdb.getTermFreq(coll,termId);
		// no need to malloc since we have the tmp buf
		char *reply = slot->m_tmpBuf;
		*(long long *)reply = termFreq ;
		// . send back the buffer, it now belongs to the slot
		// . this list and all our local vars should be freed on return
		g_udpServer.sendReply_ass ( reply , 8 , reply , 8 , slot );
		return;
	}

	// check our cache for this termid and collection, 
	collnum_t collnum = g_collectiondb.getCollnum(coll);
	if ( collnum < 0 ) {
		g_errno = ENOCOLLREC;
		log("quota: msg36: collection does not exist.");
                g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	/*
	// init now if we need to
	if ( ! s_init ) {
		// keep trying this each time until it succeeds
		long maxCacheMem = g_conf.m_quotaTableMaxMem; // 256*1024;
		// key + collnum + 8byteCount + timestamp
		long nodeSize    = 25;//sizeof(key_t) + sizeof(collnum_t) + 8 + 4;
		if ( ! g_qtable.init ( maxCacheMem  ,
				       8            , // long fixedDataSize , 
				       false        , // bool supportLists  ,
				       maxCacheMem/nodeSize, // maxCacheNodes ,
				       false        , // bool useHalfKeys   ,
				       "quota"      , // char *dbname       ,
				       true         ))// bool  loadFromDisk );
			log("quota: msg36: could not init g_qtable.");
		// no need to call again on success
		else s_init = true;
	}

	// see if we had this cached to save
	// disk seeks. But don't check the table if we are incrementing or 
	// decrementing. The reason is that we don't know what split to inc/
	// dec, so just get the right count from disk. 
	//
	// NO LONGER! we use zak's "no split" thing so that one host and only 
	// one host (and twin) is responsible for storing this termlist. 
	// certain termids are "no split" and all qhost: and qdom: are no 
	// split, as well as the gbtagvec and gbgigabitvec termids i think.
	//if ( !incCount && !decCount ) {
	char *rec;
	long  recSize;
	key_t k;
	k.n0 = 0;
	k.n1 = (unsigned long long)termId;
	// . return false if not found
	// . we can't promote it because we re-set the count below by
	//   doing a *(long long *)rec=count, if we promoted the slot then that
	//   "rec" would point to an invalid slot's data in the cache.
	bool inCache = g_qtable.getRecord ( collnum    ,
					    (char *)&k ,
					    &rec       ,
					    &recSize   ,
					    false      ,  // do copy?
					    MAX_AGE    ,  // maxCacheAge=7days
					    true       ,  // incCounts? stats.
					    NULL       ,  // cacheTime ptr
					    false      ); // promoteRecord?
	// get the cached count
	long long count = 0;
	if ( inCache ) count = *(long long *)rec;
	// set to -1 if not in cache at all
	else           count = -1;

	log(LOG_DEBUG,"quota: msg36: got cached quota for termid=%llu "
	    "count=%lli collnum=%li inc=%li dec=%li in g_qtable.",
	    (long long)termId,count,(long)collnum,(long)incCount,
	    (long)decCount);

	// -1 means not in the cache, otherwise it is there
	if ( count >= 0 ) {
		// sanity check
		if ( recSize != 8 ) { char *xx = NULL; *xx = 0; }
		// inc it? this means the doc was successfully added
		// and we're basically updating its quota count
		if ( incCount ) count++;
		if ( decCount ) count--;
		if ( incCount || decCount ) {
			//log(LOG_DEBUG,"build: adding quota to table for "
			//    "termId %llu. newcount=%lli.",termId, count);
			//g_qtable.addLongLong(collnum,termId,count);
			// to prevent cache churn, just set it directly now.
			// because of a ton of "backoffs" from Msg13, we often
			// check the quota for a host/domain but do not proceed
			// with spidering it until later.
			*(long long *)rec = count;
		}
		char *reply = slot->m_tmpBuf;
		*(long long *)reply = count;
		g_udpServer.sendReply_ass ( reply , 8 , reply , 8 , slot );
		return;
	}
	*/

	// make a hash of just the termid and collnum
	long long requestHash = hash64 ( termId , (long long)collnum);
	// . add the request hash to the table
	// . returns the number of requests in the table with that hash
	//   AFTER this add was completed
	long nr = s_requestTableServer36.addRequest ( requestHash , slot );
	// returns -1 if failed to add it and sets g_errno
	if ( nr == -1 ) return g_udpServer.sendErrorReply ( slot, g_errno );
	// . are we currently servicing this request already?
	// . if so, wait in line for the reply to be generated
	//   and it will call s_requestTable50.gotReply() below and that
	//   will call gotReplyToSendFromRequestTable() for each person
	//   waiting in line
	if ( nr >= 2 ) {
		log(LOG_DEBUG,"quota: Waiting in line for termid=%llu",termId);
		return;
	}

	// make a new state so we can read the termlist from disk and tree
	State36 *st ;
	try { st = new (State36); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("quota: msg36: could not allocate %li bytes for state. "
		    ,(long)sizeof(State36));
		// at this point we should not have anyone waiting in line
		// because we are the first, so just send an error reply back
		// sanity check. BUT, we have to remove from request table...
		s_requestTableServer36.m_htable.removeKey(requestHash);
                g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}
	mnew (st,sizeof(State36),"Msg36");
	st->m_termId      = termId;
	st->m_collnum     = collnum;
	//st->m_incCount    = incCount;
	//st->m_decCount    = decCount;
	st->m_oldListSize = 0;
	st->m_requestHash = requestHash;
	st->m_recPtr      = NULL;
	st->m_niceness    = niceness;

	log(LOG_DEBUG,"quota: msg36: getting list for termid=%llu "//cnt=%li "
	    "collnum=%li in g_qtable.",(long long)termId,//(long)count,
	    (long)collnum);

	// establish the list boundary keys
	key144_t startKey;
	key144_t endKey;
	g_posdb.makeStartKey ( &startKey,termId);
	g_posdb.makeEndKey   ( &endKey,termId);

	//now call msg5
	callMsg5 ( st , startKey , endKey );
}

void callMsg5 ( State36 *st , key144_t startKey , key144_t endKey  ) {
	
	// . if we need an *exact* count we must get the list itself!
	// . TODO: if quota is over about 30 million docs for a particular site
	//   then we will need to fix this code, cuz it only reads up to 
	//   200MB (MRS) if the site: termlist
	char *coll = g_collectiondb.getCollName ( st->m_collnum );
	//log (LOG_WARN,"build: getting frequency from disk");
	if ( ! st->m_msg5.getList ( RDB_POSDB    ,
				    coll           ,
				    &st->m_list    ,
				    &startKey       ,
				    &endKey         ,
				    MRS            , // minRecSizes, *large*
				    true           , // include tree?
				    false          , // add to cache?
				    0              , // max cache age
				    0              , // start file num
				    -1             , // numFiles
				    (void *) st    ,
				    gotListWrapper ,
				    // try this again with better caching
				    st->m_niceness , // MAX_NICENESS
				    // spiders all block up on this little
				    // msg36 request if cache not big enough
				    // and it really slows the pipeline down
				    //0              , 
				    false          ))// do error correction?
		return;
	// we got the list without blocking...
	gotListWrapper ( st , NULL , NULL );
}

void gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	State36 *st = (State36 *)state;

	long long  count;

	// if we store in cache successfully this will be non-NULL, otherwise
	// it will be NULL. *retRecPtr will point to the data of the record 
	// we stored.
	//char *retRecPtr = NULL;

	// bail on error, a Msg36 error, spider should give up and retry
	// forever later
	if ( g_errno ) goto sendReplies;

	//add the count
	st->m_oldListSize += st->m_list.m_listSize - 6;
	if ( st->m_oldListSize < 0 ) st->m_oldListSize = 0;

	//fixing the problem of the list being more than the MRS
	if ( st->m_list.m_listSize >= MRS ) {
		/*log(LOG_LOGIC,
		    "build: Term List is greater than %li, getting more from "
		    "disk.", MRS);*/
		//no need to check for special case of list=0
		char *lastKeyPtr = st->m_list.m_listEnd - 6;
		//we make a new start key
		key144_t startKey;
		st->m_list.getKey ( lastKeyPtr , (char *)&startKey );
		//increment the startkey.n0 by 1
		startKey.n0++;
		//end key is the last key
		key144_t endKey ;
		g_posdb.makeEndKey ( &endKey,st->m_termId );
		//free the list so we don't waste MRS bytes
		st->m_list.freeList();
		return callMsg5 ( st , startKey , endKey );
	}

	// each docid is 6 bytes (first docid is 12, but we removed that above)
	count = st->m_oldListSize / 6;

	// . store it in the cache
	// . this should set g_errno on error
	//g_qtable.addLongLong(st->m_collnum,st->m_termId,count,&retRecPtr);
	// this is NULL if we were unable to add to cache
	//if (retRecPtr == NULL ) { g_errno = EBADENGINEER; goto sendReplies; }

	// keep the ptr so all can mod it if they need to
	//st->m_recPtr = retRecPtr;
	// sanity check
	//if( *(long long *)(st->m_recPtr) != count ) { char *xx=NULL; *xx=0; }

 sendReplies:
	// . send the reply tp everyone waiting in line
	// . when s_requestTableServer36 calls gotReplyRequestTableServerEnd()
	//   it will set state2 from its hash table
	// . that will send back an error reply if g_errno is set (see below)
	s_requestTableServer36.gotReply ( st->m_requestHash  ,
					  NULL               , // reply
					  0                  , // replySize
					  st                 , // state1
					  gotReplyRequestTableServerEnd );

	mdelete ( st,sizeof(State36),"Msg36");
	delete  ( st);
}	

// called by s_requestTableServer36.gotReply() for each person waiting in line
void gotReplyRequestTableServerEnd ( char *reply  , long replySize , 
				     void *state1 , void *state2   ) {
	UdpSlot *slot = (UdpSlot *)state2;

	// point to the count in the g_qtable cache
	State36   *st       = (State36 *)state1;
	long long *countPtr = (long long *)st->m_recPtr;

	// retrun on any error
	if ( g_errno ) {
		log(LOG_DEBUG,"quota: msg36: sending error reply for "
		    "termid=%llu err=%s",st->m_termId,mstrerror(g_errno));
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	/*
	// get the request parms
        char *request  = slot->m_readBuf;
	char  incCount = false;
	char  decCount = false;
	if ( *request & 0x02 ) incCount   = true;
	if ( *request & 0x04 ) decCount   = true;

	// inc or dec if we should
	if ( incCount ) *countPtr = *countPtr + 1;
	if ( decCount ) *countPtr = *countPtr - 1;
	*/

	// use the slot's tmp buf to hold the reply
	reply = slot->m_tmpBuf;

	// set the reply to this new value
	*(long long *)reply = *countPtr;

	log(LOG_DEBUG,"quota: msg36: sending reply for termid=%llu count=%lli",
	    st->m_termId,(long long)*countPtr);

	// send back the reply
	g_udpServer.sendReply_ass ( reply, 8, reply, 8, slot );
}
