#include "gb-include.h"

#include "Msg2.h"
#include "Stats.h"
#include "RdbList.h"
#include "Rdb.h"
#include "Threads.h"
#include "Posdb.h" // getTermId()
#include "Msg3a.h" // DEFAULT_POSDB_READ_SIZE

//static void gotListWrapper0 ( void *state ) ;
static void  gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) ;

Msg2::Msg2() {
	m_numLists = 0;
}

void Msg2::reset ( ) {
	m_numLists = 0;
}

Msg2 *g_msg2;
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . componentCodes are used to collapse a series of termlists into a single
//   compound termlist. component termlists have their compound termlist number
//   as their componentCode, compound termlists have a componentCode of -1,
//   other termlists have a componentCode of -2. These are typically taken
//   from the Query.cpp class.
bool Msg2::getLists ( int32_t     rdbId       ,
		      collnum_t collnum , // char    *coll        ,
		      int32_t     maxAge      ,
		      bool     addToCache  ,
		      //QueryTerm *qterms ,
		      Query *query ,
		      // put list of sites to restrict to in here
		      // or perhaps make it collections for federated search?
		      char *whiteList ,
		      int64_t docIdStart,
		      int64_t docIdEnd,
		      int32_t    *minRecSizes ,
		      //int32_t     numLists    ,
		      // make max MAX_MSG39_LISTS
		      RdbList *lists       ,
		      void    *state       ,
		      void   (* callback)(void *state ) ,
		      Msg39Request *request ,
		      int32_t     niceness    ,
		      bool     doMerge     ,
		      bool     isDebug     ,
		      int32_t    *bestSenderHostIds ,
		      bool     restrictPosdb   ,
		      char     forceParitySplit    ,
		      bool     checkCache          ) {
	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: bad collection. msg2.");
	if ( ! minRecSizes ) { 
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"net: MinRecSizes is NULL.");
		return true;
	}
	// save callback and state
	m_query       = query;
	if ( ! query ) { char *xx=NULL;*xx=0; }
	m_state       = state;
	m_callback    = callback;
	m_niceness    = niceness;
	m_doMerge     = doMerge;
	m_isDebug     = isDebug;
	m_lists = lists;
	//m_totalRead   = 0;
	m_whiteList = whiteList;
	m_w = 0;
	m_p = whiteList;

	m_docIdStart = docIdStart;
	m_docIdEnd   = docIdEnd;
	m_req         = request;
	m_qterms              = m_query->m_qterms;
	m_minRecSizes         = minRecSizes;
	m_maxAge              = maxAge;
	m_getComponents       = false;
	m_rdbId               = rdbId;
	m_addToCache          = addToCache;
	m_collnum             = collnum;
	m_restrictPosdb       = restrictPosdb;
	m_forceParitySplit    = forceParitySplit;
	m_checkCache          = checkCache;
	// MDW: no more than an hr seconds, no matter what. let's keep it fresh
	if ( m_maxAge > 3600 ) m_maxAge = 3600;
	// we haven't got any responses as of yet or sent any requests
	m_numReplies  = 0;
	m_numRequests = 0;
	// save rdbid in case getDbnameFromId() is called below
	//m_msg5[0].m_rdbId = rdbId;
	// start the timer
	m_startTime = gettimeofdayInMilliseconds();
	// set this
	m_numLists = m_query->m_numTerms;
	// make sure not too many lists being requested
	//if(m_numLists > MAX_NUM_LISTS ) {g_errno=ETOOMANYLISTS; return true;}
	// clear them all
	//for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
	//	m_inProgress[i] = true;
	//	//m_slotNum[i] = -1;
	//}
	// all msg5 available for use
	for ( int32_t i = 0 ; i < MSG2_MAX_REQUESTS ; i++ ) m_avail[i] = true;
	if ( m_isDebug ) {
		if ( m_getComponents ) log ("query: Getting components.");
		else                   log ("query: Getting lists.");
	}
	// reset error
	m_errno = 0;
	// reset list counter
	m_i = 0;
	// fetch what we need
	return getLists ( );
}

bool Msg2::getLists ( ) {
	// if we're just using the root file of indexdb to save seeks
	int32_t numFiles;
	bool includeTree;
	if ( m_restrictPosdb ) { numFiles =  1; includeTree = false; }
	else                   { numFiles = -1; includeTree = true;  }

	//int32_t pass = 0;
	//bool redo = false;

	// loop:
	// . send out a bunch of msg5 requests
	// . make slots for all
	for (  ; m_i < m_numLists ; m_i++ ) {
		// sanity for Msg39's sake. do no breach m_lists[].
		if ( m_i >= ABS_MAX_QUERY_TERMS ) { char *xx=NULL;*xx=0; }
		// if any had error, forget the rest. do not launch any more
		if ( m_errno ) break;
		// skip if already did it
		//if ( ! m_inProgress[i] ) continue;
		// skip if currently launched
		//if ( m_slotNum[i] >= 0 ) continue;
		//if ( ! m_avail[i] ) continue;
		// do not allow too many outstanding requests
		//if ( m_numRequests - m_numReplies >= MSG2_MAX_REQUESTS )
		//	return false;
		// . reset it just in case it was being recycled
		// . now we call Msg39::reset() which frees each m_list[i]
		//m_lists[i].freeList();
		// skip if no bytes requested
		if ( m_minRecSizes[m_i] == 0 ) continue;
		// get a free msg5
		//int32_t j = 0;
		//for( ; j < MSG2_MAX_REQUESTS ; j++ ) if ( m_avail[j] ) break;
		//if ( j >= MSG2_MAX_REQUESTS ) {
		//	log(LOG_LOGIC,"query: No msg5s available.");
		//	char *xx = NULL; *xx = 0;
		//}
		// endtime of 0 means to ignore
		//m_endTimes[i] = 0;
		//char *kp = (char *)&m_qterms[i].m_startKey;
		//bool isSplit = m_qterms[i].isSplit();//m_isSplit[i];
		//uint32_t gid = getGroupId ( m_rdbId , kp , isSplit );
		// . get the local lists last cuz we will block on reading them
		//   from disk if the niceness is 0
		//if ( pass == 0 && gid == g_hostdb.m_groupId ) continue;
		//if ( pass == 1 && gid != g_hostdb.m_groupId ) continue;

		if ( m_isDebug ) {
			key144_t *sk ;
			key144_t *ek ;
			sk = (key144_t *)m_qterms[m_i].m_startKey;
			ek = (key144_t *)m_qterms[m_i].m_endKey;
			int64_t docId0 = g_posdb.getDocId(sk);
			int64_t docId1 = g_posdb.getDocId(ek);
			log("query: reading termlist #%"INT32" "//from "
			    //"distributed cache on host #%"INT32". "
			    "termId=%"INT64". sk=%s ek=%s "
			    "mr=%"INT32" (docid0=%"INT64" to "
			    "docid1=%"INT64").",
			    m_i,
			    //hostId, 
			    g_posdb.getTermId(sk),
			    KEYSTR(sk,sizeof(POSDBKEY)),
			    KEYSTR(ek,sizeof(POSDBKEY)),
			    //sk->n2,
			    //sk->n1,
			    //(int32_t)sk->n0,
			    m_minRecSizes[m_i],
			    docId0,
			    docId1);
		}
		
		int32_t minRecSize = m_minRecSizes[m_i];

		// sanity check
		// if ( ( minRecSize > ( 500 * 1024 * 1024 ) || 
		//        minRecSize < 0) ){
		// 	log( "minRecSize = %"INT32"", minRecSize );
		// 	char *xx=NULL; *xx=0;
		// }

		//bool forceLocalIndexdb = true;
		// if it is a no-split term, we may gotta get it over the net
		//if ( ! m_qterms[i].isSplit() ) 
		//	forceLocalIndexdb = false;

		QueryTerm *qt = &m_qterms[m_i];

		char *sk2 = NULL;
		char *ek2 = NULL;
		sk2 = qt->m_startKey;
		ek2 = qt->m_endKey;

		// if single word and not required, skip it
		if ( ! qt->m_isRequired && 
		     ! qt->m_isPhrase &&
		     ! qt->m_synonymOf )
			continue;

		Msg5 *msg5 = getAvailMsg5();
		// return if all are in use
		if ( ! msg5 ) return false;

		// stash this 
		msg5->m_parent = this;
		msg5->m_i      = m_i;

		/*
		// if doing a gbdocid:| restricted query then use msg0
		// because it is probably stored remotely!
		if ( m_query->m_docIdRestriction ) {
			// try to just ask one host for this termlist
			int64_t d = m_query->m_docIdRestriction;
			uint32_t gid = g_hostdb.getGroupIdFromDocId ( d );
			Host *group = g_hostdb.getGroupFromGroupId ( gid );
			int32_t hoff = d % g_hostdb.getNumHostsPerShard();
			Host *h = &group[hoff];
			m_msg0[i].m_parent = this;
			// this will use a termlist cache locally and on the
			// remote end to ensure optimal performance.
			if ( ! m_msg0[i].getList ( -1, // hostid
						   0 , // ip
						   0 , // port
						   86400*7,// maxCacheAge
						   true , // addtocache
						   m_rdbId ,
						   m_coll        ,
						   &m_lists[i], // listPtr
						   sk2, // startkey
						   ek2, // endkey
						   minRecSize  ,
						   &m_msg0[i], // state
						   gotListWrapper0,//callback
						   m_niceness ,
						   true, // doerrocorrection?
						   true, // include tree?
						   true, // do merge?
						   h->m_hostId,//firsthostid
						   0,//startfilenum
						   -1,//numfiles
						   86400//timeout in secs
						   )){
				// if it blocked, occupy the slot
				m_numRequests++;
				m_avail [i] = false;
				continue;
			}
			// we got the list without blocking, must have been
			// in the local g_termListCache.
			goto noblock;
		}
		*/

		// . start up a Msg5 to get it
		// . this will return false if blocks
		// . no need to do error correction on this since only RdbMerge
		//   really needs to do it and he doesn't call Msg2
		// . this is really only used to get IndexLists
		// . we now always compress the list for 2x faster transmits
		if ( ! msg5->getList ( 
					   m_rdbId         , // rdbid
					   m_collnum      ,
					   &m_lists[m_i], // listPtr
					   sk2,//&m_startKeys  [i*ks],
					   ek2,//&m_endKeys    [i*ks],
					   minRecSize  ,
					   includeTree,//true, // include tree?
					   false , // addtocache
					   0, // maxcacheage
					   0              , // start file num
					   numFiles,//-1    , // num files
					   msg5,//&m_msg5[i]     , // state
					   gotListWrapper ,
					   m_niceness     ,
					   false          , // error correction
					   NULL , // cachekeyptr
					   0, // retrynum
					   -1, // maxretries
					   true, // compensateformerge?
					   -1, // syncpoint
					   NULL, // msg5b
					   false, // isrealmerge?
					   true,// allow disk page cache?
					   true, // hit disk?
					   //false)) {// MERGE LISTS??? NO!!!
					   true) ) { // MERGE AGAIN NOW!
			m_numRequests++;
			//m_slotNum   [i] = i;
			//m_avail     [i] = false;
			continue;
		}

		//	noblock:

		// do not allow it to be re-used since now posdb
		// calls Msg2::getListGroup()
		//m_slotNum   [i] = i;
		// that is no longer the case!! we do a merge now... i
		// think we decided it was easier to deal with shit n posdb.cpp
		// but i don't know how much this matters really
		//m_avail     [i] = false;

		// set our end time if we need to
		//if ( g_conf.m_logTimingNet )
		//	m_endTimes[i] = gettimeofdayInMilliseconds();
		// if the list is empty, we can get its components now
		//m_inProgress[i] = false;
		// we didn't block, so do this
		m_numReplies++; 
		m_numRequests++; 
		// return the msg5 now
		returnMsg5 ( msg5 );
		// note it
		//if ( m_isDebug )
		//	logf(LOG_DEBUG,"query: got list #%"INT32" size=%"INT32"",
		//	     i,m_lists[i].getListSize() );
		// count it
		//m_totalRead += m_lists[i].getListSize();
		// break out on error and wait for replies if we blocked
		if ( ! g_errno ) continue;
		// report the error and return
		m_errno = g_errno;
		log("query: Got error reading termlist: %s.",
		    mstrerror(g_errno));
		goto skip;
	}

	//
	// now read in lists from the terms in the "whiteList"
	//

	// . loop over terms in the whitelist, space separated. 
	// . m_whiteList is NULL if none provided.
	for ( char *p = m_p ; m_whiteList && *p ; m_w++ ) {
		// advance
		char *current = p;
		for ( ; *p && *p != ' ' ; p++ );
		// save end of "current"
		char *end = p;
		// advance to point to next item in whiteList
		for ( ; *p == ' ' ; p++ );
		// . convert whiteList term into key
		// . put the "site:" prefix before it first
		// . see XmlDoc::hashUrl() where prefix = "site"
		int64_t prefixHash = hash64b ( "site" );
		//int64_t termId = hash64(current,end-current);
		// crap, Query.cpp i guess turns xyz.com into http://xyz.com/
		int32_t conti = 0;
		int64_t termId = 0LL;
		termId = hash64_cont("http://",7,termId,&conti);
		termId = hash64_cont(current,end-current,termId,&conti);
		termId = hash64_cont("/",1,termId,&conti);
		//SafeBuf tt;
		//tt.safePrintf("http://");
		//tt.safeMemcpy(current,end-current);
		//tt.pushChar('/');
		//int64_t yy = hash64n(tt.getBufStart());
		//if ( yy != termId ) { char *xx=NULL;*xx=0; }
		int64_t finalTermId = hash64 ( termId , prefixHash );
		// mask to 48 bits
		finalTermId &= TERMID_MASK;
		// . make key. be sure to limit to provided docid range
		//   if we are doing docid range splits to prevent OOM
		// . these docid ranges were likely set in Msg39::
		//   doDocIdRangeSplitLoop(). it already applied them to
		//   the QueryTerm::m_startKey in Msg39.cpp so we have to
		//   apply here as well...
		char sk3[MAX_KEY_BYTES];
		char ek3[MAX_KEY_BYTES];
		g_posdb.makeStartKey ( sk3 , finalTermId , m_docIdStart );
		g_posdb.makeEndKey   ( ek3 , finalTermId , m_docIdEnd );
		// get one
		Msg5 *msg5 = getAvailMsg5();
		// return if all are in use
		if ( ! msg5 ) return false;

		// stash this 
		msg5->m_parent = this;
		msg5->m_i      = m_i + m_w;

		// advance cursor
		m_p = p;

		// sanity for Msg39's sake. do no breach m_lists[].
		if ( m_w >= MAX_WHITELISTS ) { char *xx=NULL;*xx=0; }

		// like 90MB last time i checked. so it won't read more
		// than that...
		// MDW: no, it's better to print oom then not give all the
		// results leaving users scratching their heads. besides,
		// we should do docid range splitting before we go out of
		// mem. we should also report the size of each termlist
		// in bytes in the query info header.
		//int32_t minRecSizes = DEFAULT_POSDB_READSIZE;
		// MDW TODO fix this later we go oom too easily for queries
		// like 'www.disney.nl'
		int32_t minRecSizes = -1;

		// start up the read. thread will wait in thread queue to 
		// launch if too many threads are out.
		if ( ! msg5->getList ( 	   m_rdbId         , // rdbid
					   m_collnum        ,
					   &m_whiteLists[m_w], // listPtr
					   &sk3,//&m_startKeys  [i*ks],
					   &ek3,//&m_endKeys    [i*ks],
					   minRecSizes,
					   includeTree,//true, // include tree?
					   false , // addtocache
					   0, // maxcacheage
					   0              , // start file num
					   numFiles,//-1    , // num files
					   msg5,//&m_msg5[i]     , // state
					   gotListWrapper ,
					   m_niceness     ,
					   false          , // error correction
					   NULL , // cachekeyptr
					   0, // retrynum
					   -1, // maxretries
					   true, // compensateformerge?
					   -1, // syncpoint
					   NULL, // msg5b
					   false, // isrealmerge?
					   true,// allow disk page cache?
					   true, // hit disk?
					   //false)) {// MERGE LISTS??? NO!!!
					   true) ) { // MERGE AGAIN NOW!
			m_numRequests++;
			//m_avail     [i] = false;
			continue;
		}
		// return it!
		m_numReplies++; 
		m_numRequests++; 
		// . return the msg5 now
		returnMsg5 ( msg5 );
		// break out on error and wait for replies if we blocked
		if ( ! g_errno ) continue;
		// report the error and return
		m_errno = g_errno;
		log("query: Got error reading termlist: %s.",
		    mstrerror(g_errno));
		goto skip;
	}
		
 skip:

	// do the 2nd pass if we need to (and there was no error)
	//if ( ! g_errno && pass == 0 ) { pass = 1; goto loop; }
	// if we did get a compound list reply w/o blocking, re-do the loop
	// because we may be able to launch some component list requests
	//if ( ! g_errno && redo ) { redo = false; pass = 0; goto loop; }
	// . bail if waiting for all non-component lists, still
	// . did anyone block? if so, return false for now
	if ( m_numRequests > m_numReplies ) return false;
	// . otherwise, we got everyone, so go right to the merge routine
	// . returns false if not all replies have been received 
	// . returns true if done
	// . sets g_errno on error
	return gotList ( NULL );
}

Msg5 *Msg2::getAvailMsg5 ( ) {
	for ( int32_t i = 0 ; i < MSG2_MAX_REQUESTS ; i++ ) {
		if ( ! m_avail[i] ) continue;
		m_avail[i] = false;
		return &m_msg5[i];
	}
	return NULL;
}

void Msg2::returnMsg5 ( Msg5 *msg5 ) {
	int32_t i; for ( i = 0 ; i < MSG2_MAX_REQUESTS ; i++ ) 
		if ( &m_msg5[i] == msg5 ) break;
	// wtf?
	if ( i >= MSG2_MAX_REQUESTS ) { char *xx=NULL;*xx=0; }
	// make it available
	m_avail[i] = true;
	// reset it
	msg5->reset();
}


/*
void gotListWrapper0 ( void *state ) {
	Msg0 *msg0 = (Msg0 *)state;
	Msg2 *THIS = (Msg2 *)msg0->m_parent;
	RdbList *list = msg0->m_list;
	// get list #. TODO: make sure this works
	int32_t i = list - &THIS->m_lists[0];
 	THIS->m_inProgress[i] = false;
	THIS->m_numReplies++;
	if ( THIS->m_isDebug ) {
		if ( ! list )
			logf(LOG_DEBUG,"query: got0 NULL list #%"INT32"",  i);
		else
			logf(LOG_DEBUG,"query: got0 list #%"INT32" size=%"INT32"",
			     i,list->getListSize() );
	}
	// try to launch more
	if ( ! THIS->getLists ( ) ) return;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
}
*/

void gotListWrapper ( void *state , RdbList *rdblist, Msg5 *msg5 ) {
	//Msg2 *THIS = (Msg2 *) state;
	Msg5    *ms   = (Msg5 *)state;
	Msg2    *THIS = (Msg2 *)ms->m_parent;
	RdbList *list = ms->m_list;
	// note it
	if ( g_errno ) {
		log ("msg2: error reading list: %s",mstrerror(g_errno));
		THIS->m_errno = g_errno;
		g_errno = 0;
	}
	// identify the msg0 slot we use
	int32_t i  = list - THIS->m_lists;
	//int32_t i = ms->m_i;
	//if ( i < 0 || i >= MSG2_MAX_REQUESTS ) { char *xx=NULL;*xx=0; }
	//int32_t nn = THIS->m_slotNum [ i ];
 	//THIS->m_inProgress[ i] = false;
	THIS->returnMsg5 ( ms );
	// now we keep for because Msg2::getGroupList() needs it!!
	//THIS->m_avail     [nn] = true;
	//THIS->m_slotNum   [ i] = -1;
	// . now m_linkInfo[i] (for some i, i dunno which) is filled
	THIS->m_numReplies++;
	// note it
	if ( THIS->m_isDebug ) {
		if ( ! list )
			logf(LOG_DEBUG,"query: got NULL list #%"INT32"",  i);
		else
			logf(LOG_DEBUG,"query: got list #%"INT32" size=%"INT32"",
			     i,list->getListSize() );
	}
	// keep a count of bytes read from all lists
	//if ( list ) THIS->m_totalRead += list->getListSize();
	// try to launch more
	if ( ! THIS->getLists ( ) ) return;
	// set g_errno if any one list read had error
	if ( THIS->m_errno ) g_errno = THIS->m_errno;
	// now call callback, we're done
	THIS->m_callback ( THIS->m_state );
}

// . returns false if not all replies have been received (or timed/erroredout)
// . returns true if done (or an error finished us)
// . sets g_errno on error
// . "list" is NULL if we got all lists w/o blocking and called this
bool Msg2::gotList ( RdbList *list ) {

	// wait until we got all the replies before we attempt to merge
	if ( m_numReplies < m_numRequests ) return false;

	// timestamp log
	//if(m_isDebug) {
	//	int32_t size = -1;
	//	if ( list ) size = list->getListSize();
	//	log("Msg2::got list size=%"INT32" listPtr=%"INT32"", size , (int32_t)list );
	//}

	// . return true on error
	// . no, wait to get all the replies because we destroy ourselves
	//   by calling the callback, and another reply may come back and
	//   think we're still around. so, ideally, destroy those udp slots
	//   OR just wait for all replies to come in.
	//if ( g_errno ) return true;
	if ( m_errno )
		log("net: Had error fetching data from %s: %s.", 
		    getDbnameFromId(m_rdbId),
		    mstrerror(m_errno) );

	// note it
	if ( m_isDebug ) {
		for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
			log("msg2: read termlist #%"INT32" size=%"INT32"",
			    i,m_lists[i].m_listSize);
		}
	}

	// bitch if we hit our max read sizes limit, we are losing docids!
	for ( int32_t i = 0 ; i < m_numLists ; i++ ) {
		if ( m_lists[i].m_listSize < m_minRecSizes[i] ) continue;
		if ( m_minRecSizes[i] == 0 ) continue;
		if ( m_minRecSizes[i] == -1 ) continue;
		// do not print this if compiling section xpathsitehash stats
		// because we only need like 10k of list to get a decent
		// reading
		if ( m_req->m_forSectionStats ) break;
		log("msg2: read termlist #%"INT32" size=%"INT32" "
		    "maxSize=%"INT32". losing docIds!",
		    i,m_lists[i].m_listSize,m_minRecSizes[i]);
	}

	// debug msg
	int64_t now = gettimeofdayInMilliseconds();
	// . add the stat
	// . use yellow for our color (Y= g -b
	if(m_niceness > 0) {
		g_stats.addStat_r ( 0 , 
				    m_startTime , 
				    now         ,
				    //"get_termlists_nice",
				    0x00aaaa00 );
	}
	else {
		g_stats.addStat_r ( 0 , 
				    m_startTime , 
				    now         ,
				    //"get_termlists",
				    0x00ffff00 );
	}

	m_k = -1;

	// set this i guess
	g_errno = m_errno;

	// assume no compound list needs to be added to the cache
	//for ( int32_t i = 0 ; i < m_numLists ; i++ ) m_needsCaching[i] = false;
	// probably out of memory if this is set
	//if ( err ) return true;
	// all done
	return true;
}
