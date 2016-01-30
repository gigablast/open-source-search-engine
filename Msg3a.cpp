#include "gb-include.h"

#include "Msg3a.h"
//#include "Msg3b.h"
#include "Wiki.h"
//#include "Events.h" // class EventIdBits...printEventIds()
#include "sort.h"

#include "Stats.h"

static void gotReplyWrapper3a     ( void *state , void *state2 ) ;
//static void gotRerankedDocIds     ( void *state );

int32_t *g_ggg = NULL;

Msg3a::Msg3a ( ) {
	constructor();
}

void Msg3a::constructor ( ) {
	// final buf hold the final merged docids, etc.
	m_finalBufSize = 0;
	m_finalBuf     = NULL;
	m_docsToGet    = 0;
	m_numDocIds    = 0;
	m_collnums     = NULL;
	m_inUse        = false;
	m_q            = NULL;

	m_numTotalEstimatedHits = 0LL;
	m_skippedShards = 0;

	// need to call all safebuf constructors now to set m_label
	m_rbuf2.constructor();

	// NULLify all the reply buffer ptrs
	for ( int32_t j = 0; j < MAX_SHARDS; j++ ) 
		m_reply[j] = NULL;
	m_rbufPtr = NULL;
	for ( int32_t j = 0; j < MAX_SHARDS; j++ ) 
		m_mcast[j].constructor();
	m_seoCacheList.constructor();
}

Msg3a::~Msg3a ( ) {
	reset();
	for ( int32_t j = 0; j < MAX_SHARDS; j++ ) 
		m_mcast[j].destructor();
	m_seoCacheList.freeList();
}

void Msg3a::reset ( ) {

	if ( m_inUse ) { log("msg3a: msg3a in use!"); }

	m_seoCacheList.freeList();

	m_siteHashes26 = NULL;
	// . NULLify all the reply buffer ptrs
	// . have to count DOWN with "i" because of the m_reply[i-1][j] check
	for ( int32_t j = 0; j < MAX_SHARDS; j++ ) {
		if ( ! m_reply[j] ) continue;
		mfree(m_reply[j],m_replyMaxSize[j],  "Msg3aR");
		m_reply[j] = NULL;
	}
	for ( int32_t j = 0; j < MAX_SHARDS; j++ ) 
		m_mcast[j].reset();
	// and the buffer that holds the final docids, etc.
	if ( m_finalBuf )
		mfree ( m_finalBuf, m_finalBufSize, "Msg3aF" );
	// free the request
	if ( m_rbufPtr && m_rbufPtr != m_rbuf ) {
		mfree ( m_rbufPtr , m_rbufSize, "Msg3a" );
		m_rbufPtr = NULL;
	}
	m_rbuf2.purge();
	m_finalBuf     = NULL;
	m_finalBufSize = 0;
	m_docsToGet    = 0;
	m_errno        = 0;
	m_numDocIds    = 0;
	m_collnums     = NULL;
	m_numTotalEstimatedHits = 0LL;
	m_skippedShards = 0;
}

Msg39Request *g_r = NULL;

static void gotCacheReplyWrapper ( void *state ) {
	Msg3a *THIS = (Msg3a *)state;
	// return if it blocked...
	if ( ! THIS->gotCacheReply() ) return;
	// set g_errno i guess so parent knows
	if ( THIS->m_errno ) g_errno = THIS->m_errno;
	// call callback if we did not block, since we're here. all done.
	THIS->m_callback ( THIS->m_state );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . "query/coll" should NOT be on the stack in case we block
// . uses Msg36 to retrieve term frequencies for each termId in query
// . sends Msg39 request to get docids from each indexdb shard
// . merges replies together
// . we print out debug info if debug is true
// . "maxAge"/"addToCache" is talking about the clusterdb cache as well
//   as the indexdb cache for caching termlists read from disk on the machine
//   that contains them on disk.
// . "docsToGet" is how many search results are requested
// . "useDateLists" is true if &date1=X, &date2=X or &sdate=1 was specified
// . "sortByDate" is true if we should rank the results by newest pubdate 1st
// . "soreByDateWeight" is 1.0 to do a pure sort byte date, and 0.0 to just
//   sort by the regular score. however, the termlists are still read from
//   datedb, so we tend to prefer fresher results.
// . [date1,date2] define a range of dates to restrict the pub dates of the
//   search results to. they are -1 to indicate none.
// . "restrictIndexdbForQuery" limits termlists to the first indexdb file
// . "requireAllTerms" is true if all search results MUST contain the required
//   query terms, otherwise, such results are preferred, but the result set 
//   will contain docs that do not have all required query terms.
// . "compoundListMaxSize" is the maximum size of the "compound" termlist 
//   formed in Msg2.cpp by merging together all the termlists that are UOR'ed 
//   together. this size is in bytes.
// . if "familyFilter" is true the results will not have their adult bits set
// . if language > 0, results will be from that language (language filter)
// . if rerankRuleset >= 0, we re-rank the docids by calling PageParser.cpp
//   on the first (X in &n=X) results and getting a new score for each.
// . if "artr" is true we also call PageParser.cpp on the root url of each
//   result, since the root url's quality is used to compute the quality
//   of the result in Msg16::computeQuality(). This will slow things down lots.
//   artr means "apply ruleset to roots".
// . if "recycleLinkInfo" is true then the rerank operation will not call
//   Msg25 to recompute the inlinker information used in 
//   Msg16::computeQuality(), but rather deserialize it from the TitleRec. 
//   Computing the link info takes a lot of time as well.
bool Msg3a::getDocIds ( Msg39Request *r          ,
			Query        *q          ,
			void         *state      ,
			void        (* callback) ( void *state ),
			class Host *specialHost 
			// initially this is the same as r->m_docsToGet but
			// we may up it if too many results got clustered.
			// then we re-call this function.
			//int32_t          docsToGet  ) {
			) {

	// in case re-using it
	reset();
	// remember ALL the stuff
	m_r        = r;
	// this should be &SearchInput::m_q
	m_q        = q;
	m_callback = callback;
	m_state    = state;

	// warning. coll size includes \0
	if ( ! m_r->m_collnum < 0 ) // ptr_coll || m_r->size_coll-1 <= 0 ) 
		log(LOG_LOGIC,"net: bad collection. msg3a. %"INT32"",
		    (int32_t)m_r->m_collnum);

	//m_indexdbSplit = g_hostdb.m_indexSplits;
	// certain query term, like, gbdom:xyz.com, are NOT split
	// at all in order to keep performance high because such
	// terms are looked up by the spider. if a query contains
	// multiple "no split" terms, then it becomes split unfortunately...
	//if ( ! m_q->isSplit() ) m_indexdbSplit = 1;

	// for a sanity check in Msg39.cpp
	r->m_nqt = m_q->getNumTerms();

	// we like to know if there was *any* problem even though we hide
	// title recs that are not found.
	m_errno     = 0;
	// reset this to zero in case we have error or something
	m_numDocIds = 0;
	// total # of estimated hits
	m_numTotalEstimatedHits = 0;
	// we modify this, so copy it from request
	m_docsToGet = r->m_docsToGet;

	// fix empty queries saying a shard is down
	m_skippedShards = 0;

	// . return now if query empty, no docids, or none wanted...
	// . if query terms = 0, might have been "x AND NOT x"
	if ( m_q->getNumTerms() <= 0 ) return true;
	// sometimes we want to get section stats from the hacked
	// sectionhash: posdb termlists
	//if ( m_docsToGet <= 0 && ! m_r->m_getSectionStats ) 
	//	return true;
	// . set g_errno if not found and return true
	// . coll is null terminated
	CollectionRec *cr = g_collectiondb.getRec(r->m_collnum);
	if ( ! cr ) { g_errno = ENOCOLLREC; return true; }

	// query is truncated if had too many terms in it
	if ( m_q->m_truncated ) {
		log("query: query truncated: %s",m_q->m_orig);
		m_errno = EQUERYTRUNCATED;
	}

	// a handy thing
	m_debug = 0;
	if ( m_r->m_debug          ) m_debug = 1;
	if ( g_conf.m_logDebugQuery  ) m_debug = 1;
	if ( g_conf.m_logTimingQuery ) m_debug = 1;


	// time how long it takes to get the term freqs
	if ( m_debug ) {
		// show the query terms
		printTerms ( );
		m_startTime = gettimeofdayInMilliseconds();
		logf(LOG_DEBUG,"query: msg3a: [%"PTRFMT"] getting termFreqs.", 
		     (PTRTYPE)this);
	}

	// . hit msg17 seoresults cache
	// . just stores docid/score pairs for seo.cpp
	if ( m_r->m_useSeoResultsCache ) {
		// the all important seo results cache key
		m_ckey.n0 = hash64 ( m_r->ptr_query ,m_r->size_query - 1 ,0 );
		m_ckey.n0 = hash64h ( (int64_t)m_r->m_collnum,  m_ckey.n0);
		m_ckey.n0 = hash64 ( (char *)&m_r->m_language,1 ,  m_ckey.n0 );
		m_ckey.n0 = hash64 ( (char *)&m_r->m_docsToGet,4,  m_ckey.n0 );
		// this should be non-zero so g_hostdb.getGroupId(RDB_SERPDB)
		// does not always return groupid 0!
		m_ckey.n1 = hash32 ( m_r->ptr_query ,m_r->size_query - 1 ,0 );
		// must NOT be a delete!
		m_ckey.n0 |= 0x01;
		// set hi bit to avoid collisions with keys made from
		// Cachedb::makeKey() function calls
		//m_ckey.n1 |= 0x80000000;
		key_t startKey = m_ckey;
		key_t endKey   = m_ckey;
		// clear delbit
		startKey.n0 &= 0xfffffffffffffffeLL;
		// make a proper endkey
		//endKey += 2;
		// sanity
		if ( ( m_ckey.n0 & 0x01 ) == 0x00 ) { char *xx=NULL;*xx=0; }
		// reset it
		//m_cacheRec     = NULL;
		//m_cacheRecSize = 0;
		// note it
		//log("seopipe: checking ckey=%s q=%s"
		//    ,KEYSTR(&m_ckey,12)
		//    ,m_r->ptr_query
		//    );
		//setStatus("launching msg17");
		// return FALSE if this blocks
		if ( ! m_msg0.getList ( -1, // hostid
					0 , // ip
					0 , // port
					0 , // maxcacheage
					false, // addtocache?
					RDB_SERPDB,//RDB_CACHEDB,
					m_r->m_collnum,//ptr_coll,
					&m_seoCacheList,
					(char *)&startKey ,
					(char *)&endKey,
					10, // minrecsizes 10 bytes
					this,
					gotCacheReplyWrapper,
					m_r->m_niceness ) )
			return false;
	}

	return gotCacheReply ( );
}

bool Msg3a::gotCacheReply ( ) {

	// in cache?
	if ( ! m_seoCacheList.isEmpty() ) {
		// note it
		//log("seopipe: found ckey=%s q=%s"
		//    ,KEYSTR(&m_ckey,12)
		//    ,m_r->ptr_query
		//    );
		char *p = m_seoCacheList.getList();
		// skip key
		p += sizeof(key_t);
		// datasize
		p += 4;
		// timestamp
		//int32_t cachedTime = *(int32_t *)p;
		p += 4;
		// # docids
		m_numDocIds = *(int32_t *)p;
		p += 4;
		// total # results
		m_numTotalEstimatedHits = *(int32_t *)p;
		p += 4;
		// docids
		m_docIds = (int64_t *)p;
		p += 8 * m_numDocIds;
		// scores
		m_scores = (double *)p;
		p += sizeof(double) * m_numDocIds;
		// site hashes
		m_siteHashes26 = (int32_t *)p;
		p += 4 * m_numDocIds;
		// log to log as well
		char tmp[50000];
		p = tmp;
		p += sprintf(p,
			     "seopipe: hit cache "
			     "docids=%"INT32" "
			     "query=\"%s\" ",
			     m_numDocIds,
			     m_r->ptr_query );
		// log each docid
		//for ( int32_t i = 0 ; i < m_numDocIds ; i++ ) {
		//	//float score = m_msg3a->getScores()[i];
		//	int64_t d = m_docIds[i];
		//	//int32_t sh32 = m_msg3a->getSiteHash32(i);
		//	p += sprintf(p,"d%"INT32"=%"INT64" ",i,d);
		//}
		log("%s",tmp);
		// all done!
		return true;
	}

	//CollectionRec *cr;
	//cr = g_collectiondb.getRec(m_r->ptr_coll,m_r->size_coll-1);
	//setTermFreqWeights(m_r->m_collnum,m_q,m_termFreqs,m_termFreqWeights);
	setTermFreqWeights ( m_r->m_collnum,m_q );

	if ( m_debug ) {
		//int64_t *termIds = m_q->getTermIds();
		//if ( m_numCandidates ) termIds = m_synIds;
		for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// get the term in utf8
			QueryTerm *qt = &m_q->m_qterms[i];
			//char bb[256];
			//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
			char *tpc = qt->m_term + qt->m_termLen;
			char c = *tpc;
			*tpc = 0;
			// this term freq is estimated from the rdbmap and
			// does not hit disk...
			logf(LOG_DEBUG,"query: term #%"INT32" \"%s\" "
			     "termid=%"INT64" termFreq=%"INT64" termFreqWeight=%.03f",
			     i,
			     qt->m_term, 
			     qt->m_termId,
			     qt->m_termFreq,//m_termFreqs[i],
			     qt->m_termFreqWeight);//m_termFreqWeights[i]);
			// put it back
			*tpc = c;
		}
	}

	// time how long to get each shard's docids
	if ( m_debug )
		m_startTime = gettimeofdayInMilliseconds();

	// reset replies received count
	m_numReplies  = 0;
	m_skippedShards = 0;
	// int16_tcut
	int32_t n = m_q->m_numTerms;

	/////////////////////////////
	//
	// set the Msg39 request
	//
	/////////////////////////////

	// free if we should
	if ( m_rbufPtr && m_rbufPtr != m_rbuf ) {
		mfree ( m_rbufPtr , m_rbufSize , "Msg3a");
		m_rbufPtr = NULL;
	}

	// a tmp buf
	int32_t readSizes[ABS_MAX_QUERY_TERMS];
	float   tfw      [ABS_MAX_QUERY_TERMS];
	// update our read info
	for ( int32_t j = 0; j < n ; j++ ) {
		// the read size for THIS query term
		int32_t rs = 300000000; // toRead; 300MB i guess...
		// limit to 50MB man! this was 30MB but the
		// 'time enough for love' query was hitting 30MB termlists.
		//rs = 50000000;
		rs = DEFAULT_POSDB_READSIZE;//90000000; // 90MB!
		// it is better to go oom then leave users scratching their
		// heads as to why some results are not being returned.
		// no, because we are going out of mem for queries like
		// 'www.disney.nl' etc.
		//rs = -1;
		// if section stats, limit to 1MB
		//if ( m_r->m_getSectionStats ) rs = 1000000;
		// get the jth query term
		QueryTerm *qt = &m_q->m_qterms[j];
		// if query term is ignored, skip it
		if ( qt->m_ignored ) rs = 0;
		// set it
		readSizes[j] = rs;
		// serialize these too
		tfw[j] = qt->m_termFreqWeight;
	}

	// serialize this
	m_r->ptr_readSizes  = (char *)readSizes;
	m_r->size_readSizes = 4 * n;
	m_r->ptr_termFreqWeights  = (char *)tfw;//m_termFreqWeights;
	m_r->size_termFreqWeights = 4 * n;
	// store query into request, might have changed since we called
	// Query::expandQuery() above
	m_r->ptr_query  = m_q->m_orig;
	m_r->size_query = m_q->m_origLen+1;
	// the white list now too...
	//m_r->ptr_whiteList = si->m_whiteListBuf.getBufStart();
	// free us?
	if ( m_rbufPtr && m_rbufPtr != m_rbuf ) {
		mfree ( m_rbufPtr , m_rbufSize, "Msg3a" );
		m_rbufPtr = NULL;
	}
	m_r->m_stripe = 0;
	// debug thing
	g_r        = m_r;
	// . (re)serialize the request
	// . returns NULL and sets g_errno on error
	// . "m_rbuf" is a local storage space that can save a malloc
	// . do not "makePtrsRefNewBuf" because if we do that and this gets
	//   called a 2nd time because m_getWeights got set to 0, then we
	//   end up copying over ourselves.
	m_rbufPtr = serializeMsg ( sizeof(Msg39Request),
				   &m_r->size_readSizes,
				   &m_r->size_whiteList,
				   &m_r->ptr_readSizes,
				   m_r,
				   &m_rbufSize , 
				   m_rbuf , 
				   RBUF_SIZE , 
				   false );
	
	if ( ! m_rbufPtr ) return true;

	// how many seconds since our main process was started?
	long long now = gettimeofdayInMilliseconds();
	long elapsed = (now - g_stats.m_startTime) / 1000;

	// free this one too
	m_rbuf2.purge();
	// and copy that!
	if ( ! m_rbuf2.safeMemcpy ( m_rbufPtr , m_rbufSize ) ) return true;
	// and tweak it
	((Msg39Request *)(m_rbuf2.getBufStart()))->m_stripe = 1;

	/////////////////////////////
	//
	// end formulating the Msg39 request
	//
	/////////////////////////////

	// . set timeout based on docids requested!
	// . the more docs requested the longer it will take to get
	int32_t timeout = (50 * m_docsToGet) / 1000;
	// at least 20 seconds
	if ( timeout < 20 ) timeout = 20;
	// override? this is USUALLY -1, but DupDectector.cpp needs it
	// high because it is a spider time thing.
	if ( m_r->m_timeout > 0 ) timeout = m_r->m_timeout;
	// for new posdb stuff
	if ( timeout < 60 ) timeout = 60;

	int64_t qh = 0LL; if ( m_q ) qh = m_q->getQueryHash();

	m_numHosts = g_hostdb.getNumHosts();
	// only send to one host?
	if ( ! m_q->isSplit() ) m_numHosts = 1;

	// now we run it over ALL hosts that are up!
	for ( int32_t i = 0; i < m_numHosts ; i++ ) { // m_indexdbSplit; i++ ) {
		// get that host
		Host *h = g_hostdb.getHost(i);

		if(!h->m_queryEnabled) {
			m_numReplies++;
			continue;
		}

		// if not a full split, just round robin the group, i am not
		// going to sweat over performance on non-fully split indexes
		// because they suck really bad anyway compared to full
		// split indexes. "gid" is already set if we are not split.
		int32_t shardNum = h->m_shardNum;
		int32_t firstHostId = h->m_hostId;
		// get strip num
		char *req = m_rbufPtr;
		// if sending to twin, use slightly different request
		if ( h->m_stripe == 1 ) req = m_rbuf2.getBufStart();
		// if we are a non-split query, like gbdom:xyz.com just send
		// to the host that has the first termid local. it will call
		// msg2 to download all termlists. msg2 should be smart
		// enough to download the "non split" termlists over the net.
		// TODO: fix msg2 to do that...
		if ( ! m_q->isSplit() ) {
			int64_t     tid  = m_q->getTermId(0);
			key_t         k    = g_indexdb.makeKey(tid,1,1,false );
			// split = false! do not split 
			//gid = getGroupId ( RDB_POSDB,&k,false);
			shardNum = g_hostdb.getShardNumByTermId(&k);
			firstHostId = -1;
		}
		// debug log
		if ( m_debug )
			logf(LOG_DEBUG,"query: Msg3a[%"PTRFMT"]: forwarding request "
			     "of query=%s to shard %"UINT32".", 
			     (PTRTYPE)this, m_q->getQuery(), shardNum);
		// send to this guy
		Multicast *m = &m_mcast[i];
		// clear it for transmit
		m->reset();

		// if all hosts in group dead, just skip it!
		// only do this if main process has been running more than
		// 300 seconds because our brother hosts show up as "dead"
		// until we've got a ping reply back from them.
		// use 160 seconds. seems to take 138 secs or so to
		// get pings from everyone.
		if ( g_hostdb.isShardDead ( shardNum ) ) {
			m_numReplies++;
			log("msg3a: skipping dead shard # %i "
			    "(elapsed=%li)",(int)shardNum,elapsed);
			// see if this fixes the core?
			// assume reply is empty!!
			//m_reply[t][i] = NULL;
			// nuke reply in there so getBestReply() returns NULL
			//m_mcast[i].reset();
			continue;
		}


		// . send out a msg39 request to each shard
		// . multicasts to a host in group "groupId"
		// . we always block waiting for the reply with a multicast
		// . returns false and sets g_errno on error
		// . sends the request to fastest host in group "groupId"
		// . if that host takes more than about 5 secs then sends to
		//   next host
		// . key should be largest termId in group we're sending to
		bool status;
		status = m->send ( req , // m_rbufPtr         ,
				   m_rbufSize        , // request size
				   0x39              , // msgType 0x39
				   false             , // mcast owns m_request?
				   shardNum          , // group to send to
				   false             , // send to whole group?
				   (int32_t)qh          , // 0 // startKey.n1
				   this              , // state1 data
				   m                 , // state2 data
				   gotReplyWrapper3a ,
				   timeout           , // in seconds
				   m_r->m_niceness   ,
				   false             , // realtime?
				   firstHostId, // -1// bestHandlingHostId ,
				   NULL              , // m_replyBuf   ,
				   0                 , // MSG39REPLYSIZE,
				   // this is true if multicast should free the
				   // reply, otherwise caller is responsible
				   // for freeing it after calling
				   // getBestReply().
				   // actually, this should always be false,
				   // there is a bug in Multicast.cpp.
				   // no, if we error out and never steal
				   // the buffers then they will go unfreed
				   // so they are freed by multicast by default
				   // then we steal control explicitly
				   true             );
		// if successfully launch, do the next one
		if ( status ) continue;
		// . this serious error should make the whole query fail
		// . must allow other replies to come in though, so keep going
		m_numReplies++;
		log("query: Multicast Msg3a had error: %s",mstrerror(g_errno));
		m_errno = g_errno;
		g_errno = 0;
	}
	// return false if blocked on a reply
	if ( m_numReplies < m_numHosts ) return false;//indexdbSplit )
	// . otherwise, we did not block... error?
	// . it must have been an error or just no new lists available!!
	// . if we call gotAllShardReplies() here, and we were called by 
	//   mergeLists() we end up calling mergeLists() again... bad. so
	//   just return true in that case.
	//return gotAllShardReplies();
	return true;
}


void gotReplyWrapper3a ( void *state , void *state2 ) {
	Msg3a *THIS = (Msg3a *)state;
	// timestamp log
	if ( THIS->m_debug )
		logf(LOG_DEBUG,"query: msg3a: [%"PTRFMT"] got reply #%"INT32" in %"INT64" ms."
		     " err=%s", (PTRTYPE)THIS, THIS->m_numReplies ,
		     gettimeofdayInMilliseconds() -  THIS->m_startTime ,
		     mstrerror(g_errno) );
	else if ( g_errno )
		logf(LOG_DEBUG,"msg3a: error reply. [%"PTRFMT"] got reply #%"INT32" "
		     " err=%s", (PTRTYPE)THIS, THIS->m_numReplies ,
		     mstrerror(g_errno) );

	// if one shard times out, ignore it!
	if ( g_errno == EQUERYTRUNCATED ||
	     g_errno == EUDPTIMEDOUT ) 
		g_errno = 0;

	// record it
	if ( g_errno && ! THIS->m_errno ) 
		THIS->m_errno = g_errno;

	// set it
	Multicast *m = (Multicast *)state2;
	// update time
	int64_t endTime = gettimeofdayInMilliseconds();
	// update host table
	Host *h = m->m_replyingHost;
	// i guess h is NULL on error?
	if ( h ) {
		// how long did it take from the launch of request until now
		// for host "h" to give us the docids?
		int64_t delta = (endTime - m->m_replyLaunchTime);
		// . sanity check
		// . ntpd can screw with our local time and make this negative
		if ( delta >= 0 ) {
			// count the shards
			h->m_splitsDone++;
			// accumulate the times so we can do an average display
			// in PageHosts.cpp.
			h->m_splitTimes += delta;
		}
	}
	// update count of how many replies we got
	THIS->m_numReplies++;
	// bail if still awaiting more replies
	if ( THIS->m_numReplies < THIS->m_numHosts ) return;
	// return if gotAllShardReplies() blocked
	if ( ! THIS->gotAllShardReplies( ) ) return;
	// set g_errno i guess so parent knows
	if ( THIS->m_errno ) g_errno = THIS->m_errno;
	// call callback if we did not block, since we're here. all done.
	THIS->m_callback ( THIS->m_state );
}

static void gotSerpdbReplyWrapper ( void *state ) {
	Msg3a *THIS = (Msg3a *)state;
	// remove error, like ETRYAGAIN etc.
	g_errno = 0;
	// call callback if we did not block, since we're here. all done.
	THIS->m_callback ( THIS->m_state );
}
	
bool Msg3a::gotAllShardReplies ( ) {

	// if any of the shard requests had an error, give up and set m_errno
	// but don't set if for non critical errors like query truncation
	if ( m_errno ) { 
		g_errno = m_errno; 
		return true;
	}

	// also reset the finalbuf and the oldNumTopDocIds
	if ( m_finalBuf ) {
		mfree ( m_finalBuf, m_finalBufSize, "Msg3aF" );
		m_finalBuf     = NULL;
		m_finalBufSize = 0;
	}

	// update our estimated total hits
	m_numTotalEstimatedHits = 0;

	for ( int32_t i = 0; i < m_numHosts ; i++ ) {
		// get that host that gave us the reply
		//Host *h = g_hostdb.getHost(i);
		// . get the reply from multicast
		// . multicast should have destroyed all slots, but saved reply
		// . we are responsible for freeing the reply
		// . we need to call this even if g_errno or m_errno is
		//   set so we can free the replies in Msg3a::reset()
		// . if we don't call getBestReply() on it multicast should 
		//   free it, because Multicast::m_ownReadBuf is still true
		Multicast *m = &m_mcast[i];
		bool freeit = false;
		int32_t  replySize = 0;
		int32_t  replyMaxSize;
		char *rbuf;
		Msg39Reply *mr;
		// . only get it if the reply not already full
		// . if reply already processed, skip
		// . perhaps it had no more docids to give us or all termlists
		//   were exhausted on its disk and this is a re-call
		// . we have to re-process it for count m_numTotalEstHits, etc.
		rbuf = m->getBestReply ( &replySize    ,
					 &replyMaxSize ,
					 &freeit       ,
					 true          ); //stealIt?
		// cast it
		mr = (Msg39Reply *)rbuf;
		// in case of mem leak, re-label from "mcast" to this so we
		// can determine where it came from, "Msg3a-GBR"
		relabel( rbuf, replyMaxSize , "Msg3a-GBR" );
		// . we must be able to free it... we must own it
		// . this is true if we should free it, but we should not have
		//   to free it since it is owned by the slot?
		if ( freeit ) { 
			log(LOG_LOGIC,"query: msg3a: Steal failed."); 
			char *xx = NULL; *xx=0; 
		}
		// bad reply?
		if ( ! mr || replySize < 29 ) {
			m_skippedShards++;
			if(g_hostdb.getHost(i)->m_queryEnabled) {
				log(LOG_LOGIC,"query: msg3a: Bad reply (size=%i) from "
					"host #%"INT32". Dead? Timeout? OOM?"
					,(int)replySize
					,i);
            }
			m_reply       [i] = NULL;
			m_replyMaxSize[i] = 0;
			// it might have been timd out, just ignore it!!
			continue;
			// if size is 0 it can be Msg39 giving us an error!
			g_errno = EBADREPLYSIZE;
			m_errno = EBADREPLYSIZE;
			// all reply buffers should be freed on reset()
			return true;
		}
		// how did this happen?
		// if ( replySize < 29 && ! mr->m_errno ) {
		// 	// if size is 0 it can be Msg39 giving us an error!
		// 	g_errno = EBADREPLYSIZE;
		// 	m_errno = EBADREPLYSIZE;
		// 	log(LOG_LOGIC,"query: msg3a: Bad reply size "
		// 	    "of %"INT32".",
		// 	    replySize);
		// 	// all reply buffers should be freed on reset()
		// 	return true;
		// }

		// can this be non-null? we shouldn't be overwriting one
		// without freeing it...
		if ( m_reply[i] )
			// note the mem leak now
			log("query: mem leaking a 0x39 reply");

		// cast it and set it
		m_reply       [i] = mr;
		m_replyMaxSize[i] = replyMaxSize;
		// sanity check
		if ( mr->m_nqt != m_q->getNumTerms() ) {
			g_errno = EBADREPLY;
			m_errno = EBADREPLY;
			log("query: msg3a: Shard reply qterms=%"INT32" != %"INT32".",
			    (int32_t)mr->m_nqt,(int32_t)m_q->getNumTerms() );
			return true;
		}
		// return if shard had an error, but not for a non-critical
		// error like query truncation
		if ( mr->m_errno && mr->m_errno != EQUERYTRUNCATED ) {
			g_errno = mr->m_errno;
			m_errno = mr->m_errno;
			log("query: msg3a: Shard had error: %s",
			    mstrerror(g_errno));
			return true;
		}
		// deserialize it (just sets the ptr_ and size_ member vars)
		//mr->deserialize ( );
		if ( ! deserializeMsg ( sizeof(Msg39Reply) ,
					&mr->size_docIds,
					&mr->size_clusterRecs,
					&mr->ptr_docIds,
					mr->m_buf ) ) {
			g_errno = ECORRUPTDATA;
			m_errno = ECORRUPTDATA;
			log("query: msg3a: Shard had error: %s",
			    mstrerror(g_errno));
			return true;

		}
		// skip down here if reply was already set
		//skip:
		// add of the total hits from each shard, this is how many
		// total results the lastest shard is estimated to be able to 
		// return
		// . THIS should now be exact since we read all termlists
		//   of posdb...
		m_numTotalEstimatedHits += mr->m_estimatedHits;

		// accumulate total facet count from all shards for each term
		int64_t *facetCounts;
		facetCounts = (int64_t*)mr->ptr_numDocsThatHaveFacetList;
		for ( int32_t k = 0 ; k < mr->m_nqt ;  k++ ) {
			QueryTerm *qt = &m_q->m_qterms[k];
			// sanity. this should never happen.
			if ( k >= m_q->m_numTerms ) break;
			qt->m_numDocsThatHaveFacet += facetCounts[k];
		}

		// debug log stuff
		if ( ! m_debug ) continue;
		// cast these for printing out
		int64_t *docIds    = (int64_t *)mr->ptr_docIds;
		double    *scores    = (double    *)mr->ptr_scores;
		// print out every docid in this shard reply
		for ( int32_t j = 0; j < mr->m_numDocIds ; j++ ) {
			// print out score_t
			logf( LOG_DEBUG,
			     "query: msg3a: [%"PTRFMT"] %03"INT32") "
			     "shard=%"INT32" docId=%012"UINT64" "
			      "domHash=0x%02"XINT32" "
			     "score=%f"                     ,
			     (PTRTYPE)this                      ,
			     j                                        , 
			     i                                        ,
			     docIds [j] ,
			     (int32_t)g_titledb.getDomHash8FromDocId(docIds[j]),
			      scores[j] );
		}
	}

	// this seems to always return true!
	mergeLists ( );

	if ( ! m_r->m_useSeoResultsCache ) return true;

	// now cache the reply
	SafeBuf cr;
	int32_t dataSize = 4 + 4 + 4 + m_numDocIds * (8+4+4);
	int32_t need = sizeof(key_t) + 4 + dataSize;
	bool status = cr.reserve ( need );
	// sanity
	if ( ( m_ckey.n0 & 0x01 ) == 0x00 ) { char *xx=NULL;*xx=0; }
	// ignore errors
	g_errno = 0;
	// return on error with g_errno cleared if cache add failed
	if ( ! status ) return true;
	// add to buf otherwise
	cr.safeMemcpy ( &m_ckey , sizeof(key_t) );
	cr.safeMemcpy ( &dataSize , 4 );
	int32_t now = getTimeGlobal();
	cr.pushLong ( now );
	cr.pushLong ( m_numDocIds );
	cr.pushLong ( m_numTotalEstimatedHits );//Results );
	int32_t max = m_numDocIds;
	// then the docids
	for ( int32_t i = 0 ; i < max ; i++ ) 
		cr.pushLongLong(m_docIds[i] );
	for ( int32_t i = 0 ; i < max ; i++ ) 
		cr.pushDouble(m_scores[i]);
	for ( int32_t i = 0 ; i < max ; i++ ) 
		cr.pushLong(getSiteHash26(i));
	// sanity
	if ( cr.length() != need ) { char *xx=NULL;*xx=0; }
	// make these
	key_t startKey;
	key_t endKey;
	startKey = m_ckey;
	// clear delbit
	startKey.n0 &= 0xfffffffffffffffeLL;
	// end key is us
	endKey = m_ckey;
	// that is the single record
	m_seoCacheList.set ( cr.getBufStart() ,
			     cr.length(),
			     cr.getBufStart(), // alloc
			     cr.getCapacity(), // alloc size
			     (char *)&startKey,
			     (char *)&endKey,
			     -1, // fixeddatasize
			     true, // owndata?
			     false,// use half keys?
			     sizeof(key_t) );
	// do not allow cr to free it, msg1 will
	cr.detachBuf();
	// note it
	//log("seopipe: storing ckey=%s q=%s"
	//    ,KEYSTR(&m_ckey,12)
	//    ,m_r->ptr_query
	//    );
	//log("msg1: sending niceness=%"INT32"",(int32_t)m_r->m_niceness);
	// this will often block, but who cares!? it just sends a request off
	if ( ! m_msg1.addList ( &m_seoCacheList ,
				RDB_SERPDB,//RDB_CACHEDB,
				m_r->m_collnum,//ptr_coll,
				this, // state
				gotSerpdbReplyWrapper, // callback
				false, // forcelocal?
				m_r->m_niceness ) ) {
		//log("blocked");
		return false;
	}
			 
	// we can safely delete m_msg17... just return true
	return true;
}

HashTableX *g_fht = NULL;
QueryTerm *g_qt = NULL;

// sort facets by document counts before displaying
static int feCmp ( const void *a1, const void *b1 ) {
	int32_t a = *(int32_t *)a1;
	int32_t b = *(int32_t *)b1;
	FacetEntry *fe1 = (FacetEntry *)g_fht->getValFromSlot(a);
	FacetEntry *fe2 = (FacetEntry *)g_fht->getValFromSlot(b);
	if ( fe2->m_count > fe1->m_count ) return 1;
	if ( fe2->m_count < fe1->m_count ) return -1;
	int32_t *k1 = (int32_t *)g_fht->getKeyFromSlot(a);
	int32_t *k2 = (int32_t *)g_fht->getKeyFromSlot(b);
	if ( g_qt->m_fieldCode == FIELD_GBFACETFLOAT )
		return (int)( *(float *)k2 - *(float *)k1 );
	// otherwise an int
	return ( *k2 - *k1 );
}

// each query term has a safebuf of ptrs to the facet entries in its
// m_facethashTable
bool Msg3a::sortFacetEntries ( ) {

	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		// only for html for now i guess
		//if ( m_si->m_format != FORMAT_HTML ) break;
		QueryTerm *qt = &m_q->m_qterms[i];
		// skip if not facet
		if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
		     qt->m_fieldCode != FIELD_GBFACETINT &&
		     qt->m_fieldCode != FIELD_GBFACETFLOAT )
			continue;

		HashTableX *fht = &qt->m_facetHashTable;
		// first sort facetentries in hashtable by their key before
		// we print them out
		int32_t np = fht->getNumSlotsUsed();
		SafeBuf *sb = &qt->m_facetIndexBuf;
		if ( ! sb->reserve(np*4,"sbfi") ) return false;
		int32_t *ptrs = (int32_t *)sb->getBufStart();
		int32_t numPtrs = 0;
		for ( int32_t j = 0 ; j < fht->getNumSlots() ; j++ ) {
			if ( ! fht->m_flags[j] ) continue;
			ptrs[numPtrs++] = j;
		}
		// use this as global for qsort
		g_fht = fht;
		g_qt  = qt;
		// use qsort
		gbqsort ( ptrs , numPtrs , sizeof(int32_t) , feCmp , 0 );
		// now truncate the length. really we should have a max
		// for each query term.
		// this will prevent us from looking up 70,000 facets when
		// the user specifies just &nf=50.
		sb->setLength(numPtrs * sizeof(int32_t) );
		int32_t maxSize = m_r->m_maxFacets * sizeof(int32_t);
		if ( sb->length() > maxSize )
			sb->setLength(maxSize);
	}
	return true;
}


// . merge all the replies together
// . put final merged docids into m_docIds[],m_bitScores[],m_scores[],...
// . this calls Msg51 to get cluster levels when done merging
// . Msg51 remembers clusterRecs from previous call to avoid repeating lookups
// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool Msg3a::mergeLists ( ) {

	// time how long the merge takes
	if ( m_debug ) {
		logf( LOG_DEBUG, "query: msg3a: --- Final DocIds --- " );
		m_startTime = gettimeofdayInMilliseconds();
	}

	// reset our final docids count here in case we are a re-call
	m_numDocIds = 0;
	// a secondary count, how many unique docids we scanned, and not 
	// necessarily added to the m_docIds[] array
	//m_totalDocCount = 0; // int32_t docCount = 0;
	m_moreDocIdsAvail = true;

	/*

	  this version is too simple. now each query term can be a
	  gbfacet:price or gbfacet:type term and each has a
	  list in the Msg39Reply::ptr_facetHashList for its termid

	//
	// compile facet stats
	//
	for ( int32_t j = 0; j < m_numHosts ; j++ ) {
		Msg39Reply *mr =m_reply[j];
		// one table for each query term
		char *p = mr->ptr_facetHashList;
		// loop over all query terms
		int32_t n = m_q->getNumTerms();
		// use this
		HashTableX tmp;
		// do the loop
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// size of it
			int32_t psize = *(int32_t *)p; 
			p += 4;
			tmp.deserialize ( p , psize );
			p += psize;
			// now compile the stats into a master table
			for ( int32_t k = 0 ; k < tmp.m_numSlots ; k++ ) {
				if ( ! tmp.m_flags[k] ) continue;
				// get the vlaue
				int32_t v32 = *(int32_t *)tmp.getKeyFromSlot(k);
				// and how many of them there where
				int32_t count = *(int32_t *)tmp.getValueFromSlot(k);
				// add to master
				master.addScore32 ( v32 , count );
			}
		}
	}
	////////
	//
	// now set m_facetStats
	//
	////////
	// add up all counts
	int64_t count = 0LL;
	for ( int32_t i = 0 ; i < master.getNumSlots() ; i++ ) {
		if ( ! master.m_flags[i] ) continue;
		int64_t slotCount = *(int32_t *)master.getValueFromSlot(i);
		int32_t h32 = *(int32_t *)master.getKeyFromSlot(i);
		if ( h32 == m_r->m_myFacetVal32 ) 
			m_facetStats.m_myValCount = slotCount;
		count += slotCount;
	}
	m_facetStats.m_totalUniqueValues = master.getNumUsedSlots();
	m_facetStats.m_totalValues = count;
	*/	
		

	// int16_tcut
	//int32_t numSplits = m_numHosts;//indexdbSplit;

	// . point to the various docids, etc. in each shard reply
	// . tcPtr = term count. how many required query terms does the doc 
	//   have? formerly called topExplicits in IndexTable2.cpp
	int64_t     *diPtr [MAX_SHARDS];
	double        *rsPtr [MAX_SHARDS];
	key_t         *ksPtr [MAX_SHARDS];
	int64_t     *diEnd [MAX_SHARDS];
	for ( int32_t j = 0; j < m_numHosts ; j++ ) {
		// how does this happen?
		if ( j >= MAX_SHARDS ) { char *xx=NULL;*xx=0; }
		Msg39Reply *mr =m_reply[j];
		// if we have gbdocid:| in query this could be NULL
		if ( ! mr ) {
			diPtr[j] = NULL;
			diEnd[j] = NULL;
			rsPtr[j] = NULL;
			ksPtr[j] = NULL;
			continue;
		}
		diPtr [j] = (int64_t *)mr->ptr_docIds;
		rsPtr [j] = (double    *)mr->ptr_scores;
		ksPtr [j] = (key_t     *)mr->ptr_clusterRecs;
		diEnd [j] = (int64_t *)(mr->ptr_docIds +
					  mr->m_numDocIds * 8);
	}

	// clear if we had it
	if ( m_finalBuf ) {
		mfree ( m_finalBuf, m_finalBufSize, "Msg3aF" );
		m_finalBuf     = NULL;
		m_finalBufSize = 0;
	}

	//
	// HACK: START FACET stats merge
	//
	int32_t sneed = 0;
	for ( int32_t j = 0; j < m_numHosts ; j++ ) {
		Msg39Reply *mr = m_reply[j];
		if ( ! mr ) continue;
		sneed += mr->size_facetHashList/4;
	}

	//
	// each mr->ptr_facetHashList can contain the values of
	// MULTIPLE facets, so the first is the 64-bit termid of the query
	// term, like the gbfacet:type or gbfacet:price. so
	// we want to compute the FacetStats for EACH such query term.

	// so first we scan for facet query terms and reset their
	// FacetStats arrays.
	for ( int32_t i = 0 ; i < m_q->m_numTerms ; i++ ) {
		QueryTerm *qt = &m_q->m_qterms[i];
		//qt->m_facetStats.reset();
		// now make a hashtable to compile all of the
		// facethashlists from each shard into
		//int64_t tid  = m_q->m_qterms[i].m_termId;
		// we hold all the facet values
		// m_q is a ptr to State0::m_si.m_q from PageResults.cpp
		// and Msg40.cpp ultimately.
		HashTableX *ht = &qt->m_facetHashTable;
		// we have to manually call this because Query::constructor()
		// might have been called explicitly. not now because
		// i added a call the Query::constructor() to call
		// QueryTerm::constructor() for each QueryTerm in
		// Query::m_qterms[]. this was causing a mem leak of 
		// 'fhtqt' too beacause we were re-using the query for each 
		// coll in the federated loop search.
		//ht->constructor();
		// 4 byte key, 4 byte score for counting facet values
		if ( ! ht->set(4,sizeof(FacetEntry),
			       128,NULL,0,false,
			       m_r->m_niceness,"fhtqt")) 
			return true;
		// debug note
		// log("results: alloc fhtqt of %"PTRFMT" for st0=%"PTRFMT,
		//     (PTRTYPE)ht->m_buf,(PTRTYPE)m_q->m_st0Ptr);
		// sanity
		if ( ! ht->m_isWritable ) {
			log("msg3a: queryterm::constructor not called?");
			char *xx=NULL;*xx=0;
		}
	}

	// now scan each facethashlist from each shard and compile into 
	// the appropriate query term qt->m_facetHashTable
	for ( int32_t j = 0; j < m_numHosts ; j++ ) {
		Msg39Reply *mr =m_reply[j];
		if ( ! mr ) continue;
		//SectionStats *src = &mr->m_sectionStats;
		//dst->m_onSiteDocIds      += src->m_onSiteDocIds;
		//dst->m_offSiteDocIds     += src->m_offSiteDocIds;
		//dst->m_totalMatches      += src->m_totalMatches;
		//dst->m_totalEntries      += src->m_totalEntries;
		// now the list should be the unique site hashes that
		// had the section hash. we need to uniquify them again
		// here.
		char *p = (char *)mr->ptr_facetHashList;
		char *last = p + mr->size_facetHashList;
		// skip if empty
		if ( ! p ) continue;
		// come back up here for another gbfacet:xxx term
	ploop:
		// first is the termid
		int64_t termId = *(int64_t *)p;
		// skip that
		p += 8;
		// the # of 32-bit facet hashes
		int32_t nh = *(int32_t *)p;
		p += 4;
		// get that query term
		QueryTerm *qt = m_q->getQueryTermByTermId64 ( termId );
		// sanity
		if ( ! qt ) {
			log("msg3a: query: could not find query term with "
			    "termid %"UINT64" for facet",termId);
			break;
		}

		bool isFloat  = false;
		bool isInt = false;
		if ( qt->m_fieldCode == FIELD_GBFACETFLOAT ) isFloat = true;
		if ( qt->m_fieldCode == FIELD_GBFACETINT   ) isInt = true;

		// the end point
		char *pend = p + ((4+sizeof(FacetEntry)) * nh);
		// int16_tcut
		HashTableX *ft = &qt->m_facetHashTable;
		// now compile the facet hash list into there
		for ( ; p < pend ; ) {
			int32_t facetValue = *(int32_t *)p;
			p += 4;
			// how many docids had this facetValue?
			//int32_t facetCount = *(int32_t *)p;
			//p += 4;
			FacetEntry *fe = (FacetEntry *)p;
			p += sizeof(FacetEntry);
			// debug
			//log("msg3a: got facethash %"INT32") %"UINT32"",k,p[k]);
			// accumulate scores from all shards
			//if ( ! qt->m_facetHashTable.addScore(&facetValue,
			//				     facetCount) )
			//	return true;
			FacetEntry *fe2 ;
			fe2 = (FacetEntry *)ft->getValue ( &facetValue );
			if ( ! fe2 ) {
				ft->addKey ( &facetValue,fe );
				continue;
			}



			if ( isFloat ) {
				// accumulate sum as double
				double sum1 = *((double *)&fe ->m_sum);
				double sum2 = *((double *)&fe2->m_sum);
				sum2 += sum1;
				*((double *)&fe2->m_sum) = sum2;
				// and min/max as floats

				float min1 = *((float *)&fe ->m_min);
				float min2 = *((float *)&fe2->m_min);
				if ( fe2->m_count==0 || (fe->m_count!=0 && min1 < min2 )) min2 = min1;
				*((float *)&fe2->m_min) = min2;
				float max1 = *((float *)&fe ->m_max);
				float max2 = *((float *)&fe2->m_max);
				if ( fe2->m_count==0 || (fe->m_count!=0 && max1 > max2 )) max2 = max1;
				*((float *)&fe2->m_max) = max2;
			}
			if ( isInt ) {
				fe2->m_sum += fe->m_sum;
				if ( fe2->m_count==0 || (fe->m_count!=0 && fe->m_min < fe2->m_min ))
					fe2->m_min = fe->m_min;
				if ( fe2->m_count==0 || (fe->m_count!=0 && fe->m_max > fe2->m_max ))
					fe2->m_max = fe->m_max;
			}

			fe2->m_count += fe->m_count;

			// also accumualte count of total docs, not just in
			// the search results, that have this value for this
			// facet
			fe2->m_outsideSearchResultsCount +=
				fe->m_outsideSearchResultsCount;

			// prefer docid kinda randomly to balance
			// lookupFacets() load in Msg40.cpp
			if ( rand() % 2 )
				fe2->m_docId = fe->m_docId;


		}

		// now get the next gbfacet: term if there was one
		if ( p < last ) goto ploop;
	}

	// now sort the facets and put the indexes into 
	// QueryTerm::m_facetIndexBuf. now since we sort here
	// we can limit the facets we lookup in Msg40.cpp::lookupFacets2().
	// we also limit to the SearchInput::m_maxFacets here too.
	// sets g_errno on error and returns false so we return true.
	if ( ! sortFacetEntries() )
		return true;

	//if ( m_r->m_getSectionStats ) return true;
	//
	// HACK: END section stats merge
	//


	if ( m_docsToGet <= 0 ) { char *xx=NULL;*xx=0; }

	// . how much do we need to store final merged docids, etc.?
	// . docid=8 score=4 bitScore=1 clusterRecs=key_t clusterLevls=1
	//int32_t need = m_docsToGet * (8+sizeof(double)+
	int32_t nd1 = m_docsToGet;
	int32_t nd2 = 0;
	for ( int32_t j = 0; j < m_numHosts; j++ ) {
		Msg39Reply *mr = m_reply[j];
		if ( ! mr ) continue;
		nd2 += mr->m_numDocIds;
	}
	// pick the min docid count from the above two methods
	int32_t nd = nd1;
	if ( nd2 < nd1 ) nd = nd2;

	int32_t need =  nd * (8+sizeof(double)+
			   sizeof(key_t)+sizeof(DocIdScore *)+1);
	if ( need < 0 ) {
		log("msg3a: need is %i, nd = %i is too many docids",
		    (int)need,(int)nd);
		g_errno = EBUFTOOSMALL;
		return true;
	}
		
	// allocate it
	m_finalBuf     = (char *)mmalloc ( need , "finalBuf" );
	m_finalBufSize = need;
	// g_errno should be set if this fails
	if ( ! m_finalBuf ) return true;
	// hook into it
	char *p = m_finalBuf;
	m_docIds        = (int64_t *)p; p += nd * 8;
	m_scores        = (double    *)p; p += nd * sizeof(double);
	m_clusterRecs   = (key_t     *)p; p += nd * sizeof(key_t);
	m_clusterLevels = (char      *)p; p += nd * 1;
	m_scoreInfos    = (DocIdScore **)p;p+=nd*sizeof(DocIdScore *);

	// sanity check
	char *pend = m_finalBuf + need;
	if ( p != pend ) { char *xx = NULL; *xx =0; }
	// . now allocate for hash table
	// . get at least twice as many slots as docids
	HashTableT<int64_t,char> htable;
	// returns false and sets g_errno on error
	if ( ! htable.set ( nd * 2 ) ) return true;
	// hash table for doing site clustering, provided we
	// are fully split and we got the site recs now
	HashTableT<int64_t,int32_t> htable2;
	if ( m_r->m_doSiteClustering && ! htable2.set ( nd * 2 ) ) 
		return true;

	//
	// ***MERGE ALL SHARDS INTO m_docIds[], etc.***
	//
	// . merge all lists in m_replyDocIds[splitNum]
	// . we may be re-called later after m_docsToGet is increased
	//   if too many docids were clustered/filtered out after the call
	//   to Msg51.
 mergeLoop:

	// the winning docid will be diPtr[maxj]
	int32_t maxj = -1;
	//Msg39Reply *mr;
	int32_t hslot;

	// get the next highest-scoring docids from all shard termlists
	for ( int32_t j = 0; j < m_numHosts; j++ ) {
		// . skip exhausted lists
		// . these both should be NULL if reply was skipped because
		//   we did a gbdocid:| query
		if ( diPtr[j] >= diEnd[j] ) continue;
		// compare the score
		if ( maxj == -1 ) { maxj = j; continue; }
		if ( *rsPtr[j] < *rsPtr[maxj] ) continue;
		if ( *rsPtr[j] > *rsPtr[maxj] ){ maxj = j; continue; }
		// prefer lower docids on top
		if ( *diPtr[j] < *diPtr[maxj] ) { maxj = j; continue;}
	}

	if ( maxj == -1 ) {
		m_moreDocIdsAvail = false;
		goto doneMerge;
	}

	// only do this logic if we have clusterdb recs included
	if ( m_r->m_doSiteClustering     && 
	     // if the clusterLevel was set to CR_*errorCode* then this key
	     // will be 0, so in that case, it might have been a not found
	     // or whatever, so let it through regardless
	     ksPtr[maxj]->n0 != 0LL && 
	     ksPtr[maxj]->n1 != 0   ) {
		// if family filter on and is adult...
		if ( m_r->m_familyFilter && 
		     g_clusterdb.hasAdultContent((char *)ksPtr[maxj]) )
			goto skip;
		// get the hostname hash, a int64_t
		int32_t sh = g_clusterdb.getSiteHash26 ((char *)ksPtr[maxj]);
		// do we have enough from this hostname already?
		int32_t slot = htable2.getSlot ( sh );
		// if this hostname already visible, do not over-display it...
		if ( slot >= 0 ) {
			// get the count
			int32_t val = htable2.getValueFromSlot ( slot );
			// . if already 2 or more, give up
			// . if the site hash is 0, that usually means a 
			//   "not found" in clusterdb, and the accompanying 
			//   cluster level would be set as such, but since we 
			//   did not copy the cluster levels over in the merge
			//   algo above, we don't know for sure... cluster recs
			//   are set to 0 in the Msg39.cpp clustering.
			if ( sh && val >= 2 ) goto skip;
			// if only allowing one...
			if ( sh && val >= 1 && m_r->m_hideAllClustered ) 
				goto skip;
			// inc the count
			val++;
			// store it
			htable2.setValue ( slot , val );
		}
		// . add it, this should be pre-allocated!
		// . returns false and sets g_errno on error
		else if ( ! htable2.addKey(sh,1) ) return true;
	}

	hslot = htable.getSlot ( *diPtr[maxj] );

	// . only add it to the final list if the docid is "unique"
	// . BUT since different event ids share the same docid, exception!
	if ( hslot >= 0 ) goto skip; // < 0 ) {

	// always inc this
	//m_totalDocCount++;
	// only do this if we need more
	if ( m_numDocIds < m_docsToGet ) {
		// get DocIdScore class for this docid
		Msg39Reply *mr = m_reply[maxj];
		// point to the array of DocIdScores
		DocIdScore *ds = (DocIdScore *)mr->ptr_scoreInfo;
		int32_t nds = mr->size_scoreInfo/sizeof(DocIdScore);
		DocIdScore *dp = NULL;
		for ( int32_t i = 0 ; i < nds ; i++ ) {
			if ( ds[i].m_docId != *diPtr[maxj] )  continue;
			dp = &ds[i];
			break;
		}
		// add the max to the final merged lists
		m_docIds    [m_numDocIds] = *diPtr[maxj];

		// wtf?
		if ( ! dp ) {
			// this is empty if no scoring info
			// supplied!
			if ( m_r->m_getDocIdScoringInfo )
				log("msg3a: CRAP! got empty score "
				    "info for "
				    "d=%"INT64"",
				    m_docIds[m_numDocIds]);
			//char *xx=NULL; *xx=0;  261561804684
			// qry = www.yahoo
		}
		// point to the single DocIdScore for this docid
		m_scoreInfos[m_numDocIds] = dp;

		// reset this just in case
		if ( dp ) {
			dp->m_singleScores = NULL;
			dp->m_pairScores   = NULL;
		}

		// now fix DocIdScore::m_pairScores and m_singleScores
		// ptrs so they reference into the 
		// Msg39Reply::ptr_pairScoreBuf and ptr_singleSingleBuf
		// like they should. it seems we do not free the
		// Msg39Replies so we should be ok referencing them.
		if ( dp && dp->m_singlesOffset >= 0 )
			dp->m_singleScores = 
				(SingleScore *)(mr->ptr_singleScoreBuf+
						dp->m_singlesOffset) ;
		if ( dp && dp->m_pairsOffset >= 0 )
			dp->m_pairScores = 
				(PairScore *)(mr->ptr_pairScoreBuf +
					      dp->m_pairsOffset );
					

		// turn it into a float, that is what rscore_t is.
		// we do this to make it easier for PostQueryRerank.cpp
		m_scores    [m_numDocIds]=(double)*rsPtr[maxj];
		if ( m_r->m_doSiteClustering ) 
			m_clusterRecs[m_numDocIds]= *ksPtr[maxj];
		// clear this out
		//m_eventIdBits[m_numDocIds].clear();
		// set this for use below
		hslot = m_numDocIds;
		// point to next available slot to add to
		m_numDocIds++;
	}


	// if it has ALL the required query terms, count it
	//if ( *bsPtr[maxj] & 0x60 ) m_numAbove++;
	// . add it, this should be pre-allocated!
	// . returns false and sets g_errno on error
	if ( ! htable.addKey(*diPtr[maxj],1) ) return true;

 skip:
	// increment the shard pointers from which we took the max
	rsPtr[maxj]++;
	diPtr[maxj]++;
	ksPtr[maxj]++;
	// get the next highest docid and add it in
	if ( m_numDocIds < m_docsToGet ) goto mergeLoop;

 doneMerge:

	if ( m_debug ) {
		// show how long it took
		logf( LOG_DEBUG,"query: msg3a: [%"PTRFMT"] merged %"INT32" docs from %"INT32" "
		      "shards in %"UINT64" ms. "
		      ,
		      (PTRTYPE)this, 
		       m_numDocIds, (int32_t)m_numHosts,
		       gettimeofdayInMilliseconds() - m_startTime 
		      );
		// show the final merged docids
		for ( int32_t i = 0 ; i < m_numDocIds ; i++ ) {
			int32_t sh = 0;
			if ( m_r->m_doSiteClustering )
				sh=g_clusterdb.getSiteHash26((char *)
							   &m_clusterRecs[i]);
			// print out score_t
			logf(LOG_DEBUG,"query: msg3a: [%"PTRFMT"] "
			    "%03"INT32") merged docId=%012"UINT64" "
			    "score=%f hosthash=0x%"XINT32"",
			    (PTRTYPE)this, 
			     i,
			     m_docIds    [i] ,
			     (double)m_scores    [i] ,
			     sh );
		}
	}

	// if we had a full split, we should have gotten the cluster recs
	// from each shard already
	memset ( m_clusterLevels , CR_OK , m_numDocIds );

	return true;
}

int32_t Msg3a::getStoredSize ( ) {
	// docId=8, scores=sizeof(rscore_t), clusterLevel=1 bitScores=1
	// eventIds=1
	int32_t need = m_numDocIds * ( 8 + sizeof(double) + 1 ) + 
		4 + // m_numDocIds
		8 ; // m_numTotalEstimatedHits (estimated # of results)
	return need;
}

int32_t Msg3a::serialize   ( char *buf , char *bufEnd ) {
	char *p    = buf;
	char *pend = bufEnd;
	// store # of docids we have
	*(int32_t *)p = m_numDocIds; p += 4;
	// estimated # of total hits
	*(int32_t *)p = m_numTotalEstimatedHits; p += 8;
	// store each docid, 8 bytes each
	gbmemcpy ( p , m_docIds , m_numDocIds * 8 ); p += m_numDocIds * 8;
	// store scores
	gbmemcpy ( p , m_scores , m_numDocIds * sizeof(double) );
	p +=  m_numDocIds * sizeof(double) ;
	// store cluster levels
	gbmemcpy ( p , m_clusterLevels , m_numDocIds ); p += m_numDocIds;
	// sanity check
	if ( p > pend ) { char *xx = NULL ; *xx = 0; }
	// return how much we did
	return p - buf;
}

int32_t Msg3a::deserialize ( char *buf , char *bufEnd ) {
	char *p    = buf;
	char *pend = bufEnd;
	// get # of docids we have
	m_numDocIds = *(int32_t *)p; p += 4;
	// estimated # of total hits
	m_numTotalEstimatedHits = *(int32_t *)p; p += 8;
	// get each docid, 8 bytes each
	m_docIds = (int64_t *)p; p += m_numDocIds * 8;
	// get scores
	m_scores = (double *)p; p += m_numDocIds * sizeof(double) ;
	// get cluster levels
	m_clusterLevels = (char *)p; p += m_numDocIds;
	// sanity check
	if ( p > pend ) { char *xx = NULL ; *xx = 0; }
	// return how much we did
	return p - buf;
}

void Msg3a::printTerms ( ) {
	// loop over all query terms
	int32_t n = m_q->getNumTerms();
	// do the loop
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get the term in utf8
		//char bb[256];
		// "s" points to the term, "tid" the termId
		//char      *s;
		//int32_t       slen;
		//int64_t  tid;
		//char buf[2048];
		//buf[0]='\0';
		int64_t tid  = m_q->m_qterms[i].m_termId;
		char *s    = m_q->m_qterms[i].m_term;
		if ( ! s ) {
			logf(LOG_DEBUG,"query: term #%"INT32" "
			     "\"<notstored>\" (%"UINT64")",
			     i,tid);
		}
		else {
			int32_t slen = m_q->m_qterms[i].m_termLen;
			char c = s[slen];
			s[slen] = '\0';
			//utf16ToUtf8(bb, 256, s , slen );
			//sprintf(buf," termId#%"INT32"=%"INT64"",i,tid);
			// this term freq is estimated from the rdbmap and
			// does not hit disk...
			logf(LOG_DEBUG,"query: term #%"INT32" \"%s\" (%"UINT64")",
			     i,s,tid);
			s[slen] = c;
		}
	}
}

void setTermFreqWeights ( collnum_t collnum , // char *coll,
			  Query *q ) {
			  // int64_t *termFreqs, 
			  // float *termFreqWeights ) {

	int64_t numDocsInColl = 0;
	RdbBase *base = getRdbBase ( RDB_CLUSTERDB  , collnum );	
	if ( base ) numDocsInColl = base->getNumGlobalRecs();
	// issue? set it to 1000 if so
	if ( numDocsInColl < 0 ) {
		log("query: Got num docs in coll of %"INT64" < 0",numDocsInColl);
		// avoid divide by zero below
		numDocsInColl = 1;
	}
	// now get term freqs again, like the good old days
	//int64_t *termIds = q->getTermIds();
	// just use rdbmap to estimate!
	for ( int32_t i = 0 ; i < q->getNumTerms(); i++ ) {
		QueryTerm *qt = &q->m_qterms[i];
		// GET THE TERMFREQ for setting weights
		int64_t tf = g_posdb.getTermFreq ( collnum ,qt->m_termId);
		//if ( termFreqs ) termFreqs[i] = tf;
		qt->m_termFreq = tf;
		float tfw = getTermFreqWeight(tf,numDocsInColl);
		//termFreqWeights[i] = tfw;
		qt->m_termFreqWeight = tfw;
	}
}			      
