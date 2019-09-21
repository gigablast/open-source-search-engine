#include "gb-include.h"

#include "Msg22.h"
#include "Tfndb.h"     // g_tfndb.makeKey()
#include "UdpServer.h"

static void handleRequest22 ( UdpSlot *slot , int32_t netnice ) ;

bool Msg22::registerHandler ( ) {
        // . register ourselves with the udp server
        // . it calls our callback when it receives a msg of type 0x23
        if ( ! g_udpServer.registerHandler ( 0x22, handleRequest22 )) 
                return false;
        return true;
}

Msg22::Msg22() {
	m_outstanding = false;
}

Msg22::~Msg22(){
}

static void gotReplyWrapper22     ( void *state1 , void *state2 ) ;


// . sets m_availDocId or sets g_errno to ENOTFOUND on error
// . calls callback(state) when done
// . returns false if blocked true otherwise
bool Msg22::getAvailDocIdOnly ( Msg22Request  *r              ,
				int64_t preferredDocId ,
				char *coll ,
				void *state ,
				void (* callback)(void *state) ,
				int32_t niceness ) {
	return getTitleRec ( r ,
			     NULL     , //   url
			     preferredDocId    ,
			     coll     ,
			     NULL     , // **titleRecPtrPtr
			     NULL     , //  *titleRecSizePtr
			     false    , //   justCheckTfndb
			     true     , //   getAvailDocIdOnly
			     state    ,
			     callback ,
			     niceness ,
			     false    , // addToCache
			     0        , // maxCacheAge
			     9999999  , // timeout
			     false   ); // doLoadBalancing 
}


// . if url is NULL use the docId to get the titleRec
// . if titleRec is NULL use our own internal m_myTitleRec
// . sets g_errno to ENOTFOUND if TitleRec does not exist for this url/docId
// . if g_errno is ENOTFOUND m_docId will be set to the best available docId
//   for this url to use if we're adding it to Titledb
// . if g_errno is ENOTFOUND and m_docId is 0 then no docIds were available
// . "url" must be NULL terminated
bool Msg22::getTitleRec ( Msg22Request  *r              ,
			  char          *url            ,
			  int64_t      docId          ,
			  char          *coll           ,
			  char         **titleRecPtrPtr ,
			  int32_t          *titleRecSizePtr,
			  bool           justCheckTfndb ,
			  // when indexing spider replies we just want
			  // a unique docid... "docId" should be the desired
			  // one, but we might have to change it.
			  bool           getAvailDocIdOnly  ,
			  void          *state          ,
			  void         (* callback) (void *state) ,
			  int32_t           niceness       ,
			  bool           addToCache     ,
			  int32_t           maxCacheAge    ,
			  int32_t           timeout        ,
			  bool           doLoadBalancing ) {

	m_availDocId = 0;
	// sanity
	if ( getAvailDocIdOnly && justCheckTfndb ) { char *xx=NULL;*xx=0; }
	if ( getAvailDocIdOnly && url            ) { char *xx=NULL;*xx=0; }

	//if ( url ) log(LOG_DEBUG,"build: getting TitleRec for %s",url);
	// sanity checks
	if ( url    && docId!=0LL ) { char *xx=NULL;*xx=0; }
	if ( url    && !url[0]    ) { char *xx=NULL;*xx=0; }
	if ( docId!=0LL && url    ) { char *xx=NULL;*xx=0; }
	if ( ! coll               ) { char *xx=NULL;*xx=0; }
	if ( ! callback           ) { char *xx=NULL;*xx=0; }
	if ( r->m_inUse           ) { char *xx=NULL;*xx=0; }
	if ( m_outstanding        ) { char *xx = NULL;*xx=0; }
	// sanity check
	if ( ! justCheckTfndb && ! getAvailDocIdOnly ) {
		if ( ! titleRecPtrPtr  ) { char *xx=NULL;*xx=0; }
		if ( ! titleRecSizePtr ) { char *xx=NULL;*xx=0; }
	}

	// remember, caller want us to set this
	m_titleRecPtrPtr  = titleRecPtrPtr;
	m_titleRecSizePtr = titleRecSizePtr;
	// assume not found. this can be NULL if justCheckTfndb is true,
	// like when it is called from XmlDoc::getIsNew()
	if ( titleRecPtrPtr  ) *titleRecPtrPtr  = NULL;
	if ( titleRecSizePtr ) *titleRecSizePtr = 0;

	// save callback
	m_state           = state;
	m_callback        = callback;

	// save it
	m_r = r;
	// set request
	r->m_docId           = docId;
	r->m_niceness        = niceness;
	r->m_justCheckTfndb  = (bool)justCheckTfndb;
	r->m_getAvailDocIdOnly   = (bool)getAvailDocIdOnly;
	r->m_doLoadBalancing = (bool)doLoadBalancing;
	r->m_collnum         = g_collectiondb.getCollnum ( coll );
	r->m_addToCache      = false;
	r->m_maxCacheAge     = 0;
	// url must start with http(s)://. must be normalized.
	if ( url && url[0] != 'h' ) {
		log("msg22: BAD URL! does not start with 'h'");
		m_errno = g_errno = EBADENGINEER;
		return true;
	}
	// store url
	if ( url ) strcpy(r->m_url,url);
	else r->m_url[0] = '\0';

	// if no docid provided, use probable docid
	if ( ! docId ) 
		docId = g_titledb.getProbableDocId ( url );

	// get groupId from docId
	uint32_t shardNum = getShardNumFromDocId ( docId );
	// generate cacheKey, just use docid now
	key_t cacheKey ; cacheKey.n1 = 0; cacheKey.n0 = docId;
	// do load balancing iff we're the spider because if we send this
	// request to a merging host, and prefer local reads is true, the
	// resulting disk read will be starved somewhat. otherwise, we save
	// time by not having to cast a Msg36
	bool balance = false;

	/*
	// if clusterdb, do bias
	int32_t firstHostId = -1;
	// i don't see why not to always bias it, this makes tfndb page cache
	// twice as effective for all lookups
	int32_t numTwins = g_hostdb.getNumHostsPerShard();
	//int64_t bias=((0x0000003fffffffffLL)/(int64_t)numTwins);
	int64_t sectionWidth = (DOCID_MASK/(int64_t)numTwins) + 1;
	int32_t hostNum = (docId & DOCID_MASK) / sectionWidth;
	int32_t numHosts = g_hostdb.getNumHostsPerShard();
	Host *hosts = g_hostdb.getGroup ( groupId );
	if ( hostNum >= numHosts ) { char *xx = NULL; *xx = 0; }
	firstHostId = hosts [ hostNum ].m_hostId ;
	*/
	
	Host *firstHost ;
	// if niceness 0 can't pick noquery host.
	// if niceness 1 can't pick nospider host.
	firstHost = g_hostdb.getLeastLoadedInShard ( shardNum, r->m_niceness );
	int32_t firstHostId = firstHost->m_hostId;

	m_outstanding = true;
	r->m_inUse    = 1;

	// . send this request to the least-loaded host that can handle it
	// . returns false and sets g_errno on error
	// . use a pre-allocated buffer to hold the reply
	// . TMPBUFSIZE is how much a UdpSlot can hold w/o allocating
        if ( ! m_mcast.send ( (char *)r       , 
			      r->getSize()    ,
			      0x22            , // msgType 0x22
			      false           , // m_mcast own m_request?
			      shardNum        , // send to group (groupKey)
			      false           , // send to whole group?
			      //hostKey         , // key is lower bits of docId
			      0               , // key is lower bits of docId
			      this            , // state data
			      NULL            , // state data
			      gotReplyWrapper22 ,
			      timeout         , // 60 second time out
			      r->m_niceness   , // nice, reply size can be huge
			      false           , // realtime?
			      firstHostId     , // first hostid
			      NULL            , // replyBuf
			      0               , // replyBufMaxSize
			      false           , // free reply buf?
			      balance         , // do disk load balancing?
			      maxCacheAge     , // maxCacheAge
			      cacheKey        , // cacheKey
			      RDB_TITLEDB     , // rdbId of titledb
			      32*1024       ) ){// minRecSizes avg
		log("db: Requesting title record had error: %s.",
		    mstrerror(g_errno) );
		// set m_errno
		m_errno = g_errno;
		// no, multicast will free since he owns it!
		//if (replyBuf) mfree ( replyBuf , replyBufMaxSize , "Msg22" );
		return true;	
	}
	// otherwise, we blocked and gotReplyWrapper will be called
	return false;
}

void gotReplyWrapper22 ( void *state1 , void *state2 ) {
	Msg22 *THIS = (Msg22 *)state1;
	THIS->gotReply();
}

void Msg22::gotReply ( ) {
	// save g_errno
	m_errno = g_errno;
	// int16_tcut
	Msg22Request *r = m_r;
	// back
	m_outstanding = false;
	r->m_inUse    = 0;

	// bail on error, multicast will free the reply buffer if it should
	if ( g_errno ) {
		if ( r->m_url[0] )
			log("db: Had error getting title record for %s : %s.",
			    r->m_url,mstrerror(g_errno));
		else
			log("db: Had error getting title record for docId of "
			    "%"INT64": %s.",r->m_docId,mstrerror(g_errno));
		// free reply buf right away
		m_mcast.reset();
		m_callback ( m_state );
		return;
	}

	// breathe
	QUICKPOLL ( r->m_niceness );

	// get the reply
	int32_t  replySize = -1 ;
	int32_t  maxSize   ;
	bool  freeIt    ;
	char *reply     = m_mcast.getBestReply (&replySize, &maxSize, &freeIt);
	relabel( reply, maxSize, "Msg22-mcastGBR" );

	// breathe
	QUICKPOLL ( r->m_niceness );

	// a NULL reply happens when not found at one host and the other host
	// is dead... we need to fix Multicast to return a g_errno for this
	if ( ! reply ) {
		// set g_errno for callback
		m_errno = g_errno = EBADENGINEER;
		log("db: Had problem getting title record. Reply is empty.");
		m_callback ( m_state );
		return;
	}		

	// if replySize is only 8 bytes that means a not found
	if ( replySize == 8 ) {
		// we did not find it
		m_found = false;
		// get docid provided
		int64_t d = *(int64_t *)reply;
		// this is -1 or 0 if none available
		m_availDocId = d;
		// nuke the reply
		mfree ( reply , maxSize , "Msg22");
		// store error code
		m_errno = ENOTFOUND;
		// debug msg
		//if ( m_availDocId != m_probableDocId && m_url )
		//	log(LOG_DEBUG,"build: Avail docid %"INT64" != probable "
		//	     "of %"INT64" for %s.", 
		//	     m_availDocId, m_probableDocId , m_urlPtr );
		// this is having problems in Msg23::gotTitleRec()
		m_callback ( m_state );
		return;
	}

	// sanity check. must either be an empty reply indicating nothing
	// available or an 8 byte reply above!
	if ( m_r->m_getAvailDocIdOnly ) { char *xx=NULL;*xx=0; }

	// otherwise, it was found
	m_found = true;

	// if just checking tfndb, do not set this, reply will be empty!
	if ( ! r->m_justCheckTfndb ) { // && ! r->m_getAvailDocIdOnly ) {
		*m_titleRecPtrPtr  = reply;
		*m_titleRecSizePtr = replySize;
	}
	// if they don't want the title rec, nuke it!
	else {
		// nuke the reply
		mfree ( reply , maxSize , "Msg22");
	}

	// all done
	m_callback ( m_state );
}


class State22 {
public:
	UdpSlot   *m_slot;
	//int32_t       m_tfn;
	//int32_t       m_tfn2;
	int64_t  m_pd;
	int64_t  m_docId1;
	int64_t  m_docId2;
	//RdbList    m_ulist;
	RdbList    m_tlist;
	Msg5       m_msg5;
	Msg5       m_msg5b;
	int64_t  m_availDocId;
	int64_t  m_uh48;
	class Msg22Request *m_r;
	// free slot request here too
	char *m_slotReadBuf;
	int32_t  m_slotAllocSize;
	State22() {m_slotReadBuf = NULL;};
	~State22() {
		if ( m_slotReadBuf )
			mfree(m_slotReadBuf,m_slotAllocSize,"st22");
		m_slotReadBuf = NULL;
	};
		
};

static void gotTitleList ( void *state , RdbList *list , Msg5 *msg5 ) ;
//void gotUrlListWrapper     ( void *state , RdbList *list , Msg5 *msg5 ) ;

void handleRequest22 ( UdpSlot *slot , int32_t netnice ) {
	// int16_tcut
	UdpServer *us = &g_udpServer;
	// get the request
	Msg22Request *r = (Msg22Request *)slot->m_readBuf;
       // get this
	//char *coll = g_collectiondb.getCollName ( r->m_collnum );

	// sanity check
	int32_t  requestSize = slot->m_readBufSize;
	if ( requestSize < r->getMinSize() ) {
		log("db: Got bad request size of %"INT32" bytes for title record. "
		    "Need at least 28.",  requestSize );
		us->sendErrorReply ( slot , EBADREQUESTSIZE );
		return;
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *tbase; 
	if ( ! (tbase=getRdbBase(RDB_TITLEDB,r->m_collnum) ) ) {
		log("db: Could not get title rec in collection # %"INT32" "
		    "because rdbbase is null.",
		    (int32_t)r->m_collnum);
		g_errno = EBADENGINEER;
		us->sendErrorReply ( slot , g_errno ); 
		return; 
	}

	// overwrite what is in there so niceness conversion algo works
	r->m_niceness = netnice;

	// if just checking tfndb, do not do the cache lookup in clusterdb
	if ( r->m_justCheckTfndb ) r->m_maxCacheAge = 0;

	// keep track of stats
	//if    (r->m_justCheckTfndb)
	//       g_tfndb.getRdb()->readRequestGet(requestSize);
	//      else    
       g_titledb.getRdb()->readRequestGet  (requestSize);

       // breathe
       QUICKPOLL ( r->m_niceness);

       // sanity check
       if ( r->m_collnum < 0 ) { char *xx=NULL;*xx=0; }


       // make the state now
       State22 *st ;
       try { st = new (State22); }
       catch ( ... ) {
	       g_errno = ENOMEM;
	       log("query: Msg22: new(%"INT32"): %s", (int32_t)sizeof(State22),
		   mstrerror(g_errno));
	       us->sendErrorReply ( slot , g_errno );
	       return;
       }
       mnew ( st , sizeof(State22) , "Msg22" );
       
       // store ptr to the msg22request
       st->m_r = r;
       // save for sending back reply
       st->m_slot = slot;

       // then tell slot not to free it since m_r references it!
       // so we'll have to free it when we destroy State22
       st->m_slotAllocSize = slot->m_readBufMaxSize;
       st->m_slotReadBuf   = slot->m_readBuf;
       slot->m_readBuf = NULL;

       // . make the keys for getting recs from tfndb
       // . url recs map docid to the title file # that contains the titleRec
       //key_t uk1 ;
       //key_t uk2 ;
       // . if docId was explicitly specified...
       // . we may get multiple tfndb recs
       if ( ! r->m_url[0] ) {
	       // there are no del bits in tfndb
	       //uk1 = g_tfndb.makeMinKey ( r->m_docId );
	       //uk2 = g_tfndb.makeMaxKey ( r->m_docId );
	       st->m_docId1 = r->m_docId;
	       st->m_docId2 = r->m_docId;
       }

       // but if we are requesting an available docid, it might be taken
       // so try the range
       if ( r->m_getAvailDocIdOnly ) {
	       int64_t pd = r->m_docId;
	       int64_t d1 = g_titledb.getFirstProbableDocId ( pd );
	       int64_t d2 = g_titledb.getLastProbableDocId  ( pd );
	       // sanity - bad url with bad subdomain?
	       if ( pd < d1 || pd > d2 ) { char *xx=NULL;*xx=0; }
	       // make sure we get a decent sample in titledb then in 
	       // case the docid we wanted is not available
	       st->m_docId1 = d1;
	       st->m_docId2 = d2;
       }

       // . otherwise, url was given, like from Msg15
       // . we may get multiple tfndb recs
       if ( r->m_url[0] ) {
	       int32_t  dlen = 0;
	       // this causes ip based urls to be inconsistent with the call
	       // to getProbableDocId(url) below
	       char *dom  = getDomFast ( r->m_url , &dlen );
	       // bogus url?
	       if ( ! dom ) {
		       log("msg22: got bad url in request: %s from "
			   "hostid %"INT32" for msg22 call ",
			   r->m_url,slot->m_host->m_hostId);
		       g_errno = EBADURL;
		       us->sendErrorReply ( slot , g_errno ); 
		       mdelete ( st , sizeof(State22) , "Msg22" );
		       delete ( st );
		       return; 
	       }
	       int64_t pd = g_titledb.getProbableDocId (r->m_url,dom,dlen);
	       int64_t d1 = g_titledb.getFirstProbableDocId ( pd );
	       int64_t d2 = g_titledb.getLastProbableDocId  ( pd );
	       // sanity - bad url with bad subdomain?
	       if ( pd < d1 || pd > d2 ) { char *xx=NULL;*xx=0; }
	       // there are no del bits in tfndb
	       //uk1 = g_tfndb.makeMinKey ( d1 );
	       //uk2 = g_tfndb.makeMaxKey ( d2 );
	       // store these
	       st->m_pd     = pd;
	       st->m_docId1 = d1;
	       st->m_docId2 = d2;
	       st->m_uh48   = hash64b ( r->m_url ) & 0x0000ffffffffffffLL;
       }
       
       QUICKPOLL ( r->m_niceness );
       
       /*
       // int16_tcut
       Rdb *tdb = g_titledb.getRdb();

       // init this
       st->m_tfn2 = -1;
       // skip tfndb lookup if we can. saves some time.
       if ( g_conf.m_readOnlyMode && 
	    // must not be a *url* lookup, it must be a docid lookup
	    ! r->m_url[0] &&
	    // tree must be empty too i guess
	    tdb->getTree()->getNumUsedNodes() ==0 ) {
	       // the RdbBase contains the BigFiles for tfndb
	       RdbBase *base = tdb->m_bases[r->m_collnum];
	       // can only have one titledb file
	       if ( base->getNumFiles() == 1 ) {
		       // now we can get RdbBase
		       st->m_tfn2 = base->m_fileIds2[0];
		       // sanity check
		       if ( st->m_tfn2 < 0 ) { char *xx = NULL; *xx = 0; }
	       }
       }

       // check the tree for this docid
       RdbTree *tt = tdb->getTree();
       // make titledb keys
       key_t startKey = g_titledb.makeFirstKey ( st->m_docId1 );
       key_t endKey   = g_titledb.makeLastKey  ( st->m_docId2 );
       int32_t  n        = tt->getNextNode ( r->m_collnum , startKey );
       
       // there should only be one match, one titlerec per docid!
       for ( ; n >= 0 ; n = tt->getNextNode ( n ) ) {
	       // break if collnum does not match. we exceeded our tree range.
	       if ( tt->getCollnum ( n ) != r->m_collnum ) break;
	       // get the key of this node
	       key_t k = *(key_t *)tt->getKey(n);
	       // if passed limit, break out, no match
	       if ( k > endKey ) break;
	       // if we had a url make sure uh48 matches
	       if ( r->m_url[0] ) {
		       // get it
		       int64_t uh48 = g_titledb.getUrlHash48(&k);
		       // sanity check
		       if ( st->m_uh48 == 0 ) { char *xx=NULL;*xx=0; }
		       // we must match this exactly
		       if ( uh48 != st->m_uh48 ) continue;
	       }
	       // . if we matched a negative key, then skip
	       // . just break out here and enter the normal logic
	       // . it should load tfndb and find that it is not in tfndb
	       //   because when you add a negative key to titledb in
	       //   Rdb::addList, it adds a negative rec to tfndb immediately
	       // . NO! because we add the negative key to the tree when we
	       //   delete the old titledb rec, then we add the new one!
	       //   when a negative key is added Rdb::addRecord() removes
	       //   the positive key (and vice versa) from the tree.
	       if ( KEYNEG((char *)&k) ) continue;
	       // if just checking for its existence, we are done
	       if ( r->m_justCheckTfndb ) {
		       us->sendReply_ass ( NULL,0,NULL,0,slot);
		       // don't forget to free the state
		       mdelete ( st , sizeof(State22) , "Msg22" );
		       delete ( st );
		       return;
	       }
	       // ok, we got a match, return it
	       char *data     = tt->getData     ( n );
	       int32_t  dataSize = tt->getDataSize ( n );
	       // wierd!
	       if ( dataSize == 0 ) { char *xx=NULL;*xx=0; }
	       // send the whole rec back
	       int32_t need = 12 + 4 + dataSize;
	       // will this copy it? not!
	       char *buf = (char *)mmalloc ( need , "msg22t" );
	       if ( ! buf ) {
		       us->sendErrorReply ( slot , g_errno ); 
		       mdelete ( st , sizeof(State22) , "Msg22" );
		       delete ( st );
		       return; 
	       }
	       // log it
	       if ( g_conf.m_logDebugSpider )
		       logf(LOG_DEBUG,"spider: found %s in titledb tree",
			    r->m_url);
	       // store in the buf for sending
	       char *p = buf;
	       // store key
	       *(key_t *)p = k; p += sizeof(key_t);
	       // then dataSize
	       *(int32_t *)p = dataSize; p += 4;
	       // then the data
	       gbmemcpy ( p , data , dataSize ); p += dataSize;
	       // send off the record
	       us->sendReply_ass (buf, need,buf, need,slot);
	       // don't forget to free the state
	       mdelete ( st , sizeof(State22) , "Msg22" );
	       delete ( st );
	       return;
       }

       // if we did not need to consult tfndb cuz we only have one file
       if ( st->m_tfn2 >= 0 ) {
	       gotUrlListWrapper ( st , NULL , NULL );
	       return;
       }

       // . get the list of url recs for this docid range
       // . this should not block, tfndb SHOULD all be in memory all the time
       // . use 500 million for min recsizes to get all in range
       // . no, using 500MB causes problems for RdbTree::getList, so use
       //   100k. how many recs can there be?
       if ( ! st->m_msg5.getList ( RDB_TFNDB         ,
				   coll              ,
				   &st->m_ulist      ,
				   uk1               , // startKey
				   uk2               , // endKey
				   // use 0x7fffffff preceisely because it
				   // will determine eactly how long the
				   // tree list needs to allocate in Msg5.cpp
				   0x7fffffff        , // minRecSizes
				   true              , // includeTree?
				   false             , // addToCache?
				   0                 , // max cache age
				   0                 , // startFileNum
				   -1                , // numFiles (-1 =all)
				   st                ,
				   gotUrlListWrapper ,
				   r->m_niceness     ,
				   true              ))// error correction?
	       return ;
       // we did not block
       gotUrlListWrapper ( st , NULL , NULL );
}

static void gotTitleList ( void *state , RdbList *list , Msg5 *msg5 ) ;

void gotUrlListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	// int16_tcuts
	State22   *st = (State22 *)state;
	UdpServer *us = &g_udpServer;

	// bail on error
	if ( g_errno ) { 
		log("db: Had error getting info from tfndb: %s.",
		    mstrerror(g_errno));
		log("db: uk1.n1=%"INT32" n0=%"INT64" uk2.n1=%"INT32" n0=%"INT64" "
		    "d1=%"INT64" d2=%"INT64".",
		    ((key_t *)st->m_msg5.m_startKey)->n1 ,
		    ((key_t *)st->m_msg5.m_startKey)->n0 ,
		    ((key_t *)st->m_msg5.m_endKey)->n1   ,
		    ((key_t *)st->m_msg5.m_endKey)->n0   ,
		    st->m_docId1 ,
		    st->m_docId2 );
		us->sendErrorReply ( st->m_slot , g_errno ); 
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		return;
	}

	// int16_tcuts
	RdbList      *ulist = &st->m_ulist;
	Msg22Request *r     = st->m_r;
	char         *coll  = g_collectiondb.getCollName ( r->m_collnum );

	// point to top just in case
	ulist->resetListPtr();

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *tbase = getRdbBase(RDB_TITLEDB,coll);

	// set probable docid
	int64_t pd = 0LL;
	if ( r->m_url[0] ) {
		pd = g_titledb.getProbableDocId(r->m_url);
		// sanity
		if ( pd != st->m_pd ) { char *xx=NULL;*xx=0; }
	}

	// . these are both meant to be available docids
	// . if ad2 gets exhausted we use ad1
	int64_t ad1 = st->m_docId1;
	int64_t ad2 = pd;


	int32_t tfn = -1;
	// sanity check. make sure did not load from tfndb if did not need to
	if ( ! ulist->isExhausted() && st->m_tfn2 >= 0 ) {char *xx=NULL;*xx=0;}
	// if only one titledb file and none in memory use it
	if ( st->m_tfn2 >= 0 ) tfn = st->m_tfn2;

	// we may have multiple tfndb recs but we should NEVER have to read
	// multiple titledb files...
	for ( ; ! ulist->isExhausted() ; ulist->skipCurrentRecord() ) {
		// breathe
		QUICKPOLL ( r->m_niceness );
		// get first rec
		key_t k = ulist->getCurrentKey();
		// . skip negative keys
		// . seems to happen when we have tfndb in the tree...
		if ( KEYNEG((char *)&k) ) continue;

		// if we have a url and no docid, we gotta check uh48!
		if ( r->m_url[0] && g_tfndb.getUrlHash48(&k)!=st->m_uh48){
			// get docid of that guy
			int64_t dd = g_tfndb.getDocId(&k);
			// if matches avail docid, inc it
			if ( dd == ad1 ) ad1++;
			if ( dd == ad2 ) ad2++;
			// try next tfndb key
			continue;
		}
		// . get file num this rec is stored in
		// . this is updated right after the file num is merged by
		//   scanning all records in tfndb. this is very quick if all
		//   of tfndb is in memory, otherwise, it might take a few
		//   seconds. update call done in RdbMerge::incorporateMerge().
		tfn = g_tfndb.getTfn ( &k );
		// i guess we got a good match!
		break;
	}

	// sanity check. 255 used to mean in spiderdb or in tree
	if ( tfn >= 255 ) { char *xx=NULL;*xx=0; }


	// maybe no available docid if we breached our range
	if ( ad1 >= pd           ) ad1 = 0LL;
	if ( ad2 >  st->m_docId2 ) ad2 = 0LL;
	// get best
	int64_t ad = ad2;
	// but wrap around if we need to
	if ( ad == 0LL ) ad = ad1;

	// breathe
	QUICKPOLL ( r->m_niceness);

	// . log if different
	// . if our url rec was in there, this could still be different
	//   if there was another url rec in there with the same docid and
	//   a diferent extension, but with a tfn of 255, meaning that it
	//   is just in spiderdb and not in titledb yet. so it hasn't been
	//   assigned a permanent docid...
	// . another way "ad" may be different now is from the old bug which
	//   did not chain the docid properly because it limited the docid
	//   chaining to one titleRec file. so conceivably we can have 
	//   different docs sharing the same docids, but with different 
	//   url hash extensions. for instance, on host #9 we have:
	//   00f3b2ff63aec3a9 docId=261670033643 e=0x58 tfn=117 clean=0 half=0 
	//   00f3b2ff63af66c9 docId=261670033643 e=0x6c tfn=217 clean=0 half=0 
	// . Msg16 will only use the avail docid if the titleRec is not found
	if ( r->m_url[0] && pd != ad ) {
		//log(LOG_INFO,"build: Docid %"INT64" collided. %s Changing "
		//
		// http://www.airliegardens.org/events.asp?dt=2&date=8/5/2011
		//
		// COLLIDES WITH
		//
		// http://www.bbonline.com/i/chicago.html
		//
		// collision alert!
		log("spider: Docid %"INT64" collided. %s Changing "
		    "to %"INT64".", r->m_docId , r->m_url , ad );
		// debug this for now
		//char *xx=NULL;*xx=0; 
	}

	// remember it
	st->m_availDocId = ad;

	// if tfn is -1 then it was not in titledb
	if ( tfn == -1 ) { 
		// store docid in reply
		char *p = st->m_slot->m_tmpBuf;
		// send back the available docid
		*(int64_t *)p = ad;
		// send it
		us->sendReply_ass ( p , 8 , p , 8 , st->m_slot );
		// don't forget to free state
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		return;
	}

	// sanity
	if ( tfn < 0 ) { char *xx=NULL;*xx=0; }

	// breathe
	QUICKPOLL ( r->m_niceness );

	// ok, if just "checking tfndb" no need to go further
	if ( r->m_justCheckTfndb ) {
		// send back a good reply (empty means found!)
		us->sendReply_ass ( NULL,0,NULL,0,st->m_slot);
		// don't forget to free the state
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		return;
	}

	// . compute the file scan range
	// . tfn is now equivalent to Rdb's id2, a secondary file id, it
	//   follows the hyphen in "titledb0001-023.dat"
	// . default to just scan the root file AND the tree, cuz we're
	//   assuming restrictToRoot was set to true so we did not get a tfndb
	//   list
	// . even if a file number is given, always check the tree in case
	//   it got re-spidered
	// . shit, but we can still miss it if it gets dumped right after
	//   our thread is spawned, in which case we'd fall back to the old
	//   version. no. because if its in the tree now we get it before
	//   spawning a thread. there is no blocking. TRICKY. so if it is in 
	//   the tree at this point we'll get it, but may end up scanning the
	//   file with the older version of the doc... not too bad.
	int32_t startFileNum = tbase->getFileNumFromId2 ( tfn );

	// if tfn refers to a missing titledb file...
	if ( startFileNum < 0 ) {
		if ( r->m_url[0] ) log("db: titledb missing url %s",r->m_url);
		else        log("db: titledb missing docid %"INT64"", r->m_docId);
		us->sendErrorReply ( st->m_slot,ENOTFOUND );
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st ); 
		return ;
	}

	// save this
	st->m_tfn = tfn;
	*/
	// make the cacheKey ourself, since Msg5 would make the key wrong
	// since it would base it on startFileNum and numFiles
	key_t cacheKey ; cacheKey.n1 = 0; cacheKey.n0 = r->m_docId;
	// make titledb keys
	key_t startKey = g_titledb.makeFirstKey ( st->m_docId1 );
	key_t endKey   = g_titledb.makeLastKey  ( st->m_docId2 );

	// . load the list of title recs from disk now
	// . our file range should be solid
	// . use 500 million for min recsizes to get all in range
	if ( ! st->m_msg5.getList ( RDB_TITLEDB       ,
				    r->m_collnum ,
				    &st->m_tlist      ,
				    startKey          , // startKey
				    endKey            , // endKey
				    500000000         , // minRecSizes
				    true              , // includeTree
				    false,//r->m_addToCache   , // addToCache?
				    0,//r->m_maxCacheAge  , // max cache age
				    0,//startFileNum      ,
				    -1                 , // numFiles
				    st , // state             ,
				    gotTitleList      ,
				    r->m_niceness     ,
				    true              , // do error correct?
				    &cacheKey         ,
				    0                 , // retry num
				    -1                , // maxRetries
				    true              , // compensate for merge
				    -1LL              , // sync point
				    &st->m_msg5b      ) ) return ;

	// we did not block, nice... in cache?
	gotTitleList ( st , NULL , NULL );
}

void gotTitleList ( void *state , RdbList *list , Msg5 *msg5 ) {

	State22 *st = (State22 *)state;
	// if niceness is 0, use the higher priority udpServer
	UdpServer *us = &g_udpServer;
	// int16_tcut
	Msg22Request *r = st->m_r;
	// breathe
	QUICKPOLL(r->m_niceness);

	// send error reply on error
	if ( g_errno ) { 
	hadError:
		log("db: Had error getting title record from titledb: %s.",
		    mstrerror(g_errno));
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		us->sendErrorReply ( st->m_slot , g_errno ); 
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st ); 
		return ;
	}

	// convenience var
	RdbList *tlist = &st->m_tlist;

	// set probable docid
	int64_t pd = 0LL;
	if ( r->m_url[0] ) {
		//log("msg22: url= %s",r->m_url);
		pd = g_titledb.getProbableDocId(r->m_url);
		if ( pd != st->m_pd ) { 
			log("db: crap probable docids do not match! u=%s",
			    r->m_url);
			g_errno = EBADENGINEER;
			goto hadError;
		}
		// sanity
		//if ( pd != st->m_pd ) { char *xx=NULL;*xx=0; }
	}

	// the probable docid is the PREFERRED docid in this case
	if ( r->m_getAvailDocIdOnly ) pd = st->m_r->m_docId;

	// . these are both meant to be available docids
	// . if ad2 gets exhausted we use ad1
	int64_t ad1 = st->m_docId1;
	int64_t ad2 = pd;


	bool docIdWasFound = false;

	// scan the titleRecs in the list
	for ( ; ! tlist->isExhausted() ; tlist->skipCurrentRecord ( ) ) {
		// breathe
		QUICKPOLL ( r->m_niceness );
		// get the rec
		char *rec     = tlist->getCurrentRec();
		int32_t  recSize = tlist->getCurrentRecSize();
		// get that key
		key_t *k = (key_t *)rec;
		// skip negative recs, first one should not be negative however
		if ( ( k->n0 & 0x01 ) == 0x00 ) continue;

		// get docid of that titlerec
		int64_t dd = g_titledb.getDocId(k);

		if ( r->m_getAvailDocIdOnly ) {
			// make sure our available docids are availble!
			if ( dd == ad1 ) ad1++;
			if ( dd == ad2 ) ad2++;
			continue;
		}
		// if we had a url make sure uh48 matches
		else if ( r->m_url[0] ) {
			// get it
			int64_t uh48 = g_titledb.getUrlHash48(k);
			// sanity check. MDW: looks like we allow 0 to
			// be a valid hash. so let this through. i've seen
			// it core here before.
			//if ( st->m_uh48 == 0 ) { char *xx=NULL;*xx=0; }
			// make sure our available docids are availble!
			if ( dd == ad1 ) ad1++;
			if ( dd == ad2 ) ad2++;
			// we must match this exactly
			if ( uh48 != st->m_uh48 ) continue;
		}
		// otherwise, check docid
		else {
			// compare that
			if ( r->m_docId != dd ) continue;
		}

		// flag that we matched m_docId
		docIdWasFound = true;

		// do not set back titlerec if just want avail docid
		//if ( r->m_getAvailDocIdOnly ) continue;

		// ok, if just "checking tfndb" no need to go further
		if ( r->m_justCheckTfndb ) {
			// send back a good reply (empty means found!)
			us->sendReply_ass ( NULL,0,NULL,0,st->m_slot);
			// don't forget to free the state
			mdelete ( st , sizeof(State22) , "Msg22" );
			delete ( st );
			return;
		}

		// use rec as reply
		char *reply = rec;

		// . send this rec back, it's a match
		// . if only one rec in list, steal the list's memory
		if ( recSize != tlist->getAllocSize() ) {
			// otherwise, alloc space for the reply
			reply = (char *)mmalloc (recSize, "Msg22");
			if ( ! reply ) goto hadError;
			gbmemcpy ( reply , rec , recSize );
		}
		// otherwise we send back the whole list!
		else {
			// we stole this from list
			tlist->m_ownData = false;
		}
		// off ya go
		us->sendReply_ass(reply,recSize,reply,recSize,st->m_slot);
		// don't forget to free the state
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		// all done
		return;
	}

	// maybe no available docid if we breached our range
	if ( ad1 >= pd           ) ad1 = 0LL;
	if ( ad2 >  st->m_docId2 ) ad2 = 0LL;
	// get best
	int64_t ad = ad2;
	// but wrap around if we need to
	if ( ad == 0LL ) ad = ad1;
	// if "docId" was unmatched that should be the preferred available
	// docid then...
	//if(! docIdWasFound && r->m_getAvailDocIdOnly && ad != r->m_docId ) { 
	//	char *xx=NULL;*xx=0; }
	// remember it. this might be zero if none exist!
	st->m_availDocId = ad;
	// note it
	if ( ad == 0LL && (r->m_getAvailDocIdOnly || r->m_url[0]) ) 
		log("msg22: avail docid is 0 for pd=%"INT64"!",pd);

	// . ok, return an available docid
	if ( r->m_url[0] || r->m_justCheckTfndb || r->m_getAvailDocIdOnly ) {
		// store docid in reply
		char *p = st->m_slot->m_tmpBuf;
		// send back the available docid
		*(int64_t *)p = st->m_availDocId;
		// send it
		us->sendReply_ass ( p , 8 , p , 8 , st->m_slot );
		// don't forget to free state
		mdelete ( st , sizeof(State22) , "Msg22" );
		delete ( st );
		return;
	}

	// not found! and it was a docid based request...
	log("msg22: could not find title rec for docid %"UINT64" collnum=%"INT32"",
	    r->m_docId,(int32_t)r->m_collnum);
	g_errno = ENOTFOUND;
	goto hadError;
}
