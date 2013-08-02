#include "gb-include.h"

#include "Msg3a.h"
//#include "Msg3b.h"
#include "Wiki.h"
//#include "Events.h" // class EventIdBits...printEventIds()

static void gotReplyWrapper3a     ( void *state , void *state2 ) ;
//static void gotRerankedDocIds     ( void *state );

long *g_ggg = NULL;

Msg3a::Msg3a ( ) {
	constructor();
}

void Msg3a::constructor ( ) {
	// final buf hold the final merged docids, etc.
	m_finalBufSize = 0;
	m_finalBuf     = NULL;
	m_docsToGet    = 0;
	m_numDocIds    = 0;
	// NULLify all the reply buffer ptrs
	for ( long j = 0; j < MAX_INDEXDB_SPLIT; j++ ) 
		m_reply[j] = NULL;
	m_rbufPtr = NULL;
	for ( long j = 0; j < MAX_INDEXDB_SPLIT; j++ ) 
		m_mcast[j].constructor();
	m_seoCacheList.constructor();
}

Msg3a::~Msg3a ( ) {
	reset();
	for ( long j = 0; j < MAX_INDEXDB_SPLIT; j++ ) 
		m_mcast[j].destructor();
	m_seoCacheList.freeList();
}

void Msg3a::reset ( ) {

	m_seoCacheList.freeList();

	m_siteHashes26 = NULL;
	// . NULLify all the reply buffer ptrs
	// . have to count DOWN with "i" because of the m_reply[i-1][j] check
	for ( long j = 0; j < MAX_INDEXDB_SPLIT; j++ ) {
		if ( ! m_reply[j] ) continue;
		mfree(m_reply[j],m_replyMaxSize[j],  "Msg3aR");
		m_reply[j] = NULL;
	}
	for ( long j = 0; j < MAX_INDEXDB_SPLIT; j++ ) 
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
// . sends Msg39 request to get docids from each indexdb split
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
			//long          docsToGet  ) {
			) {

	// in case re-using it
	reset();
	// remember ALL the stuff
	m_r        = r;
	m_q        = q;
	m_callback = callback;
	m_state    = state;

	// warning. coll size includes \0
	if ( ! m_r->ptr_coll || m_r->size_coll-1 <= 0 ) 
		log(LOG_LOGIC,"net: NULL or bad collection. msg3a.");

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

	// . return now if query empty, no docids, or none wanted...
	// . if query terms = 0, might have been "x AND NOT x"
	if ( m_q->getNumTerms() <= 0 ) return true;
	// sometimes we want to get section stats from the hacked
	// sectionhash: posdb termlists
	if ( m_docsToGet <= 0 && ! m_r->m_getSectionStats ) 
		return true;
	// . set g_errno if not found and return true
	// . coll is null terminated
	CollectionRec *cr = g_collectiondb.getRec(r->ptr_coll, r->size_coll-1);
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
		logf(LOG_DEBUG,"query: msg3a: [%lu] getting termFreqs.", 
		     (long)this);
	}

	// . hit msg17 seoresults cache
	// . just stores docid/score pairs for seo.cpp
	if ( m_r->m_useSeoResultsCache ) {
		// the all important seo results cache key
		m_ckey.n0 = hash64 ( m_r->ptr_query ,m_r->size_query - 1 ,0 );
		m_ckey.n0 = hash64 ( m_r->ptr_coll,m_r->size_coll,  m_ckey.n0);
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
					m_r->ptr_coll,
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
		//long cachedTime = *(long *)p;
		p += 4;
		// # docids
		m_numDocIds = *(long *)p;
		p += 4;
		// total # results
		m_numTotalEstimatedHits = *(long *)p;
		p += 4;
		// docids
		m_docIds = (long long *)p;
		p += 8 * m_numDocIds;
		// scores
		m_scores = (float *)p;
		p += sizeof(float) * m_numDocIds;
		// site hashes
		m_siteHashes26 = (long *)p;
		p += 4 * m_numDocIds;
		// log to log as well
		char tmp[50000];
		p = tmp;
		p += sprintf(p,
			     "seopipe: hit cache "
			     "docids=%li "
			     "query=\"%s\" ",
			     m_numDocIds,
			     m_r->ptr_query );
		// log each docid
		//for ( long i = 0 ; i < m_numDocIds ; i++ ) {
		//	//float score = m_msg3a->getScores()[i];
		//	long long d = m_docIds[i];
		//	//long sh32 = m_msg3a->getSiteHash32(i);
		//	p += sprintf(p,"d%li=%lli ",i,d);
		//}
		log("%s",tmp);
		// all done!
		return true;
	}

	CollectionRec *cr;
	cr = g_collectiondb.getRec(m_r->ptr_coll,m_r->size_coll-1);

	setTermFreqWeights ( cr->m_coll,m_q,m_termFreqs , m_termFreqWeights );

	if ( m_debug ) {
		//long long *termIds = m_q->getTermIds();
		//if ( m_numCandidates ) termIds = m_synIds;
		for ( long i = 0 ; i < m_q->m_numTerms ; i++ ) {
			// get the term in utf8
			QueryTerm *qt = &m_q->m_qterms[i];
			//char bb[256];
			//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
			char *tpc = qt->m_term + qt->m_termLen;
			char c = *tpc;
			*tpc = 0;
			// this term freq is estimated from the rdbmap and
			// does not hit disk...
			logf(LOG_DEBUG,"query: term #%li \"%s\" "
			     "termid=%lli termFreq=%lli termFreqWeight=%.03f",
			     i,
			     qt->m_term, 
			     qt->m_termId,
			     m_termFreqs[i],
			     m_termFreqWeights[i]);
			// put it back
			*tpc = c;
		}
	}

	// time how long to get each split's docids
	if ( m_debug )
		m_startTime = gettimeofdayInMilliseconds();

	// reset replies received count
	m_numReplies  = 0;
	// shortcut
	long n = m_q->m_numTerms;

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
	long readSizes[MAX_QUERY_TERMS];
	// update our read info
	for ( long j = 0; j < n ; j++ ) {
		// the read size for THIS query term
		long rs = 300000000; // toRead; 300MB i guess...
		// limit to 50MB man! this was 30MB but the
		// 'time enough for love' query was hitting 30MB termlists.
		//rs = 50000000;
		rs = DEFAULT_POSDB_READSIZE;//90000000; // 90MB!
		// if section stats, limit to 1MB
		if ( m_r->m_getSectionStats ) rs = 1000000;
		// get the jth query term
		QueryTerm *qt = &m_q->m_qterms[j];
		// if query term is ignored, skip it
		if ( qt->m_ignored ) rs = 0;
		// set it
		readSizes[j] = rs;
	}

	// serialize this
	m_r->ptr_readSizes  = (char *)readSizes;
	m_r->size_readSizes = 4 * n;
	// and this
	m_r->ptr_termFreqWeights  = (char *)m_termFreqWeights;
	m_r->size_termFreqWeights = 4 * n;
	// store query into request, might have changed since we called
	// Query::expandQuery() above
	m_r->ptr_query  = m_q->m_orig;
	m_r->size_query = m_q->m_origLen+1;
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
				   &m_r->size_coll,
				   &m_r->ptr_readSizes,
				   m_r,
				   &m_rbufSize , 
				   m_rbuf , 
				   RBUF_SIZE , 
				   false );
	
	if ( ! m_rbufPtr ) return true;

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
	long timeout = (50 * m_docsToGet) / 1000;
	// at least 20 seconds
	if ( timeout < 20 ) timeout = 20;
	// override? this is USUALLY -1, but DupDectector.cpp needs it
	// high because it is a spider time thing.
	if ( m_r->m_timeout > 0 ) timeout = m_r->m_timeout;
	// for new posdb stuff
	if ( timeout < 60 ) timeout = 60;

	long long qh = 0LL; if ( m_q ) qh = m_q->getQueryHash();

	m_numHosts = g_hostdb.getNumHosts();
	// only send to one host?
	if ( ! m_q->isSplit() ) m_numHosts = 1;

	// now we run it over ALL hosts that are up!
	for ( long i = 0; i < m_numHosts ; i++ ) { // m_indexdbSplit; i++ ) {
		// get that host
		Host *h = g_hostdb.getHost(i);
		// if not a full split, just round robin the group, i am not
		// going to sweat over performance on non-fully split indexes
		// because they suck really bad anyway compared to full
		// split indexes. "gid" is already set if we are not split.
		unsigned long gid = h->m_groupId;//g_hostdb.getGroupId(i);
		long firstHostId = h->m_hostId;
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
			long long     tid  = m_q->getTermId(0);
			key_t         k    = g_indexdb.makeKey(tid,1,1,false );
			// split = false! do not split 
			gid = getGroupId ( RDB_POSDB,&k,false);
			firstHostId = -1;
		}
		// debug log
		if ( m_debug )
			logf(LOG_DEBUG,"query: Msg3a[%lu]: forwarding request "
			     "of query=%s to groupid 0x%lx.", 
			     (long)this, m_q->getQuery(), gid);
		// send to this guy
		Multicast *m = &m_mcast[i];
		// clear it for transmit
		m->reset();
		// . send out a msg39 request to each split
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
				   gid               , // group to send to
				   false             , // send to whole group?
				   (long)qh          , // 0 // startKey.n1
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
	// . if we call gotAllSplitReplies() here, and we were called by 
	//   mergeLists() we end up calling mergeLists() again... bad. so
	//   just return true in that case.
	//return gotAllSplitReplies();
	return true;
}


void gotReplyWrapper3a ( void *state , void *state2 ) {
	Msg3a *THIS = (Msg3a *)state;
	// timestamp log
	if ( THIS->m_debug )
		logf(LOG_DEBUG,"query: msg3a: [%lu] got reply #%li in %lli ms."
		     " err=%s", (long)THIS, THIS->m_numReplies ,
		     gettimeofdayInMilliseconds() -  THIS->m_startTime ,
		     mstrerror(g_errno) );
	else if ( g_errno )
		logf(LOG_DEBUG,"msg3a: error reply. [%lu] got reply #%li "
		     " err=%s", (long)THIS, THIS->m_numReplies ,
		     mstrerror(g_errno) );

	// if one split times out, ignore it!
	if ( g_errno == EQUERYTRUNCATED ||
	     g_errno == EUDPTIMEDOUT ) 
		g_errno = 0;

	// record it
	if ( g_errno && ! THIS->m_errno ) 
		THIS->m_errno = g_errno;

	// set it
	Multicast *m = (Multicast *)state2;
	// update time
	long long endTime = gettimeofdayInMilliseconds();
	// update host table
	Host *h = m->m_replyingHost;
	// i guess h is NULL on error?
	if ( h ) {
		// how long did it take from the launch of request until now
		// for host "h" to give us the docids?
		long long delta = (endTime - m->m_replyLaunchTime);
		// . sanity check
		// . ntpd can screw with our local time and make this negative
		if ( delta >= 0 ) {
			// count the split
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
	// return if gotAllSplitReplies() blocked
	if ( ! THIS->gotAllSplitReplies( ) ) return;
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
	
bool Msg3a::gotAllSplitReplies ( ) {

	// if any of the split requests had an error, give up and set m_errno
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

	for ( long i = 0; i < m_numHosts ; i++ ) {
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
		long  replySize = 0;
		long  replyMaxSize;
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
		if ( ! mr ) {
			log(LOG_LOGIC,"query: msg3a: Bad NULL reply.");
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
		if ( replySize < 29 && ! mr->m_errno ) {
			// if size is 0 it can be Msg39 giving us an error!
			g_errno = EBADREPLYSIZE;
			m_errno = EBADREPLYSIZE;
			log(LOG_LOGIC,"query: msg3a: Bad reply size of %li.",
			    replySize);
			// all reply buffers should be freed on reset()
			return true;
		}

		// can this be non-null? we shouldn't be overwriting one
		// without freeing it...
		if ( m_reply[i] )
			// note the mem leak now
			log("query: mem leaking a 0x39 reply");

		// cast it and set it
		m_reply       [i] = mr;
		m_replyMaxSize[i] = replyMaxSize;
		// deserialize it (just sets the ptr_ and size_ member vars)
		//mr->deserialize ( );
		deserializeMsg ( sizeof(Msg39Reply) ,
				 &mr->size_docIds,
				 &mr->size_clusterRecs,
				 &mr->ptr_docIds,
				 mr->m_buf );

		// sanity check
		if ( mr->m_nqt != m_q->getNumTerms() ) {
			g_errno = EBADREPLY;
			m_errno = EBADREPLY;
			log("query: msg3a: Split reply qterms=%li != %li.",
			    (long)mr->m_nqt,(long)m_q->getNumTerms() );
			return true;
		}
		// return if split had an error, but not for a non-critical
		// error like query truncation
		if ( mr->m_errno && mr->m_errno != EQUERYTRUNCATED ) {
			g_errno = mr->m_errno;
			m_errno = mr->m_errno;
			log("query: msg3a: Split had error: %s",
			    mstrerror(g_errno));
			return true;
		}
		// skip down here if reply was already set
		//skip:
		// add of the total hits from each split, this is how many
		// total results the lastest split is estimated to be able to 
		// return
		// . THIS should now be exact since we read all termlists
		//   of posdb...
		m_numTotalEstimatedHits += mr->m_estimatedHits;

		// debug log stuff
		if ( ! m_debug ) continue;
		// cast these for printing out
		long long *docIds    = (long long *)mr->ptr_docIds;
		score_t   *scores    = (score_t   *)mr->ptr_scores;
		// print out every docid in this split reply
		for ( long j = 0; j < mr->m_numDocIds ; j++ ) {
			// print out score_t
			logf( LOG_DEBUG,
			     "query: msg3a: [%lu] %03li) "
			     "split=%li docId=%012llu domHash=0x%02lx "
			     "score=%lu"                     ,
			     (unsigned long)this                      ,
			     j                                        , 
			     i                                        ,
			     docIds [j] ,
			     (long)g_titledb.getDomHash8FromDocId(docIds[j]),
			      (long)scores[j] );
		}
	}

	// this seems to always return true!
	mergeLists ( );

	if ( ! m_r->m_useSeoResultsCache ) return true;

	// now cache the reply
	SafeBuf cr;
	long dataSize = 4 + 4 + 4 + m_numDocIds * (8+4+4);
	long need = sizeof(key_t) + 4 + dataSize;
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
	long now = getTimeGlobal();
	cr.pushLong ( now );
	cr.pushLong ( m_numDocIds );
	cr.pushLong ( m_numTotalEstimatedHits );//Results );
	long max = m_numDocIds;
	// then the docids
	for ( long i = 0 ; i < max ; i++ ) 
		cr.pushLongLong(m_docIds[i] );
	for ( long i = 0 ; i < max ; i++ ) 
		cr.pushFloat(m_scores[i]);
	for ( long i = 0 ; i < max ; i++ ) 
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
	//log("msg1: sending niceness=%li",(long)m_r->m_niceness);
	// this will often block, but who cares!? it just sends a request off
	if ( ! m_msg1.addList ( &m_seoCacheList ,
				RDB_SERPDB,//RDB_CACHEDB,
				m_r->ptr_coll,
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
	//m_totalDocCount = 0; // long docCount = 0;
	m_moreDocIdsAvail = true;


	// shortcut
	//long numSplits = m_numHosts;//indexdbSplit;

	// . point to the various docids, etc. in each split reply
	// . tcPtr = term count. how many required query terms does the doc 
	//   have? formerly called topExplicits in IndexTable2.cpp
	long long     *diPtr [MAX_INDEXDB_SPLIT];
	float         *rsPtr [MAX_INDEXDB_SPLIT];
	key_t         *ksPtr [MAX_INDEXDB_SPLIT];
	long long     *diEnd [MAX_INDEXDB_SPLIT];
	for ( long j = 0; j < m_numHosts ; j++ ) {
		Msg39Reply *mr =m_reply[j];
		// if we have gbdocid:| in query this could be NULL
		if ( ! mr ) {
			diPtr[j] = NULL;
			diEnd[j] = NULL;
			rsPtr[j] = NULL;
			ksPtr[j] = NULL;
			continue;
		}
		diPtr [j] = (long long *)mr->ptr_docIds;
		rsPtr [j] = (float     *)mr->ptr_scores;
		ksPtr [j] = (key_t     *)mr->ptr_clusterRecs;
		diEnd [j] = (long long *)(mr->ptr_docIds +
					  mr->m_numDocIds * 8);
	}

	// clear if we had it
	if ( m_finalBuf ) {
		mfree ( m_finalBuf, m_finalBufSize, "Msg3aF" );
		m_finalBuf     = NULL;
		m_finalBufSize = 0;
	}

	//
	// HACK: START section stats merge
	//
	m_sectionStats.reset();
	long sneed = 0;
	for ( long j = 0; j < m_numHosts ; j++ ) {
		Msg39Reply *mr = m_reply[j];
		if ( ! mr ) continue;
		sneed += mr->size_siteHashList/4;
	}
	HashTableX dt;
	//char tmpBuf[5000];
	if (sneed&&!dt.set(4,0,sneed,NULL,0,false,
			   m_r->m_niceness,"uniqsit")) 
		return true;
	for ( long j = 0; sneed && j < m_numHosts ; j++ ) {
		Msg39Reply *mr =m_reply[j];
		if ( ! mr ) continue;
		SectionStats *src = &mr->m_sectionStats;
		SectionStats *dst = &m_sectionStats;
		dst->m_onSiteDocIds      += src->m_onSiteDocIds;
		dst->m_offSiteDocIds     += src->m_offSiteDocIds;
		// now the list should be the unique site hashes that
		// had the section hash. we need to uniquify them again
		// here.
		long *p = (long *)mr->ptr_siteHashList;
		long np = mr->size_siteHashList / 4;
		for ( long k = 0 ; k < np ; k++ )
			// hash it up, no dups!
			dt.addKey(&p[k]);
		// update our count based on that
		dst->m_numUniqueSites = dt.getNumSlotsUsed();
	}
	if ( m_r->m_getSectionStats ) return true;
	//
	// HACK: END section stats merge
	//


	if ( m_docsToGet <= 0 ) { char *xx=NULL;*xx=0; }

	// . how much do we need to store final merged docids, etc.?
	// . docid=8 score=4 bitScore=1 clusterRecs=key_t clusterLevls=1
	long need = m_docsToGet * (8+4+sizeof(key_t)+sizeof(DocIdScore *)+1);
	// allocate it
	m_finalBuf     = (char *)mmalloc ( need , "finalBuf" );
	m_finalBufSize = need;
	// g_errno should be set if this fails
	if ( ! m_finalBuf ) return true;
	// hook into it
	char *p = m_finalBuf;
	m_docIds        = (long long *)p; p += m_docsToGet * 8;
	m_scores        = (float     *)p; p += m_docsToGet * sizeof(float);
	m_clusterRecs   = (key_t     *)p; p += m_docsToGet * sizeof(key_t);
	m_clusterLevels = (char      *)p; p += m_docsToGet * 1;
	m_scoreInfos    = (DocIdScore **)p;p+=m_docsToGet*sizeof(DocIdScore *);

	// sanity check
	char *pend = m_finalBuf + need;
	if ( p != pend ) { char *xx = NULL; *xx =0; }
	// . now allocate for hash table
	// . get at least twice as many slots as docids
	HashTableT<long long,char> htable;
	// returns false and sets g_errno on error
	if ( ! htable.set ( m_docsToGet * 2 ) ) return true;
	// hash table for doing site clustering, provided we
	// are fully split and we got the site recs now
	HashTableT<long long,long> htable2;
	if ( m_r->m_doSiteClustering && ! htable2.set ( m_docsToGet * 2 ) ) 
		return true;

	//
	// ***MERGE ALL SPLITS INTO m_docIds[], etc.***
	//
	// . merge all lists in m_replyDocIds[splitNum]
	// . we may be re-called later after m_docsToGet is increased
	//   if too many docids were clustered/filtered out after the call
	//   to Msg51.
 mergeLoop:

	// the winning docid will be diPtr[maxj]
	long maxj = -1;
	//Msg39Reply *mr;
	long hslot;

	// get the next highest-scoring docids from all split lists
	for ( long j = 0; j < m_numHosts; j++ ) {
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
		// get the hostname hash, a long long
		long sh = g_clusterdb.getSiteHash26 ((char *)ksPtr[maxj]);
		// do we have enough from this hostname already?
		long slot = htable2.getSlot ( sh );
		// if this hostname already visible, do not over-display it...
		if ( slot >= 0 ) {
			// get the count
			long val = htable2.getValueFromSlot ( slot );
			// . if already 2 or more, give up
			// . if the site hash is 0, that usually means a 
			//   "not found" in clusterdb, and the accompanying 
			//   cluster level would be set as such, but since we 
			//   did not copy the cluster levels over in the merge
			//   algo above, we don't know for sure... cluster recs
			//   are set to 0 in the Msg39.cpp clustering.
			if ( sh && val >= 2 ) goto skip;
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
	if ( hslot < 0 ) {
		// always inc this
		//m_totalDocCount++;
		// only do this if we need more
		if ( m_numDocIds < m_docsToGet ) {
			// get DocIdScore class for this docid
			Msg39Reply *mr = m_reply[maxj];
			// point to the array of DocIdScores
			DocIdScore *ds = (DocIdScore *)mr->ptr_scoreInfo;
			long nds = mr->size_scoreInfo/sizeof(DocIdScore);
			DocIdScore *dp = NULL;
			for ( long i = 0 ; i < nds ; i++ ) {
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
					    "d=%lli",
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
			m_scores    [m_numDocIds]=(float)*rsPtr[maxj];
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
	}

 skip:
	// increment the split pointers from which we took the max
	rsPtr[maxj]++;
	diPtr[maxj]++;
	ksPtr[maxj]++;
	// get the next highest docid and add it in
	if ( m_numDocIds < m_docsToGet ) goto mergeLoop;

 doneMerge:

	if ( m_debug ) {
		// show how long it took
		logf( LOG_DEBUG,"query: msg3a: [%lu] merged %li docs from %li "
		      "splits in %llu ms. "
		      ,
		      (unsigned long)this, 
		       m_numDocIds, (long)m_numHosts,
		       gettimeofdayInMilliseconds() - m_startTime 
		      );
		// show the final merged docids
		for ( long i = 0 ; i < m_numDocIds ; i++ ) {
			long sh = 0;
			if ( m_r->m_doSiteClustering )
				sh=g_clusterdb.getSiteHash26((char *)
							   &m_clusterRecs[i]);
			// print out score_t
			logf(LOG_DEBUG,"query: msg3a: [%lu] "
			    "%03li) merged docId=%012llu "
			    "score=%.01f hosthash=0x%lx",
			    (unsigned long)this, 
			     i,
			     m_docIds    [i] ,
			     (float)m_scores    [i] ,
			     sh );
		}
	}

	// if we had a full split, we should have gotten the cluster recs
	// from each split already
	memset ( m_clusterLevels , CR_OK , m_numDocIds );

	return true;
}

long Msg3a::getStoredSize ( ) {
	// docId=8, scores=sizeof(rscore_t), clusterLevel=1 bitScores=1
	// eventIds=1
	long need = m_numDocIds * ( 8 + sizeof(rscore_t) + 1 ) + 
		4 + // m_numDocIds
		8 ; // m_numTotalEstimatedHits (estimated # of results)
	return need;
}

long Msg3a::serialize   ( char *buf , char *bufEnd ) {
	char *p    = buf;
	char *pend = bufEnd;
	// store # of docids we have
	*(long *)p = m_numDocIds; p += 4;
	// estimated # of total hits
	*(long *)p = m_numTotalEstimatedHits; p += 8;
	// store each docid, 8 bytes each
	memcpy ( p , m_docIds , m_numDocIds * 8 ); p += m_numDocIds * 8;
	// store scores
	memcpy ( p , m_scores , m_numDocIds * sizeof(rscore_t) );
	p +=  m_numDocIds * sizeof(rscore_t) ;
	// store cluster levels
	memcpy ( p , m_clusterLevels , m_numDocIds ); p += m_numDocIds;
	// sanity check
	if ( p > pend ) { char *xx = NULL ; *xx = 0; }
	// return how much we did
	return p - buf;
}

long Msg3a::deserialize ( char *buf , char *bufEnd ) {
	char *p    = buf;
	char *pend = bufEnd;
	// get # of docids we have
	m_numDocIds = *(long *)p; p += 4;
	// estimated # of total hits
	m_numTotalEstimatedHits = *(long *)p; p += 8;
	// get each docid, 8 bytes each
	m_docIds = (long long *)p; p += m_numDocIds * 8;
	// get scores
	m_scores = (rscore_t *)p; p += m_numDocIds * sizeof(rscore_t) ;
	// get cluster levels
	m_clusterLevels = (char *)p; p += m_numDocIds;
	// sanity check
	if ( p > pend ) { char *xx = NULL ; *xx = 0; }
	// return how much we did
	return p - buf;
}

void Msg3a::printTerms ( ) {
	// loop over all query terms
	long n = m_q->getNumTerms();
	// do the loop
	for ( long i = 0 ; i < n ; i++ ) {
		// get the term in utf8
		//char bb[256];
		// "s" points to the term, "tid" the termId
		//char      *s;
		//long       slen;
		//long long  tid;
		//char buf[2048];
		//buf[0]='\0';
		long long tid  = m_q->m_qterms[i].m_termId;
		char *s    = m_q->m_qterms[i].m_term;
		long slen = m_q->m_qterms[i].m_termLen;
		char c = s[slen];
		s[slen] = '\0';
		//utf16ToUtf8(bb, 256, s , slen );
		//sprintf(buf," termId#%li=%lli",i,tid);
		// this term freq is estimated from the rdbmap and
		// does not hit disk...
		logf(LOG_DEBUG,"query: term #%li \"%s\" (%llu)",i,s,tid);
		s[slen] = c;
	}
}

void setTermFreqWeights ( char *coll,
			  Query *q , 
			  long long *termFreqs, 
			  float *termFreqWeights ) {

	long long numDocsInColl = 0;
	RdbBase *base = getRdbBase ( RDB_CLUSTERDB  , coll );	
	if ( base ) numDocsInColl = base->getNumGlobalRecs();
	// issue? set it to 1000 if so
	if ( numDocsInColl < 0 ) {
		log("query: Got num docs in coll of %lli < 0",numDocsInColl);
		// avoid divide by zero below
		numDocsInColl = 1;
	}
	// now get term freqs again, like the good old days
	long long *termIds = q->getTermIds();
	// just use rdbmap to estimate!
	for ( long i = 0 ; i < q->getNumTerms(); i++ ) {
		long long tf = g_posdb.getTermFreq ( coll ,termIds[i]);
		if ( termFreqs ) termFreqs[i] = tf;
		float tfw = getTermFreqWeight(tf,numDocsInColl);
		termFreqWeights[i] = tfw;
	}
}			      
