#include "gb-include.h"

#include "Msg2.h"
#include "Stats.h"
#include "RdbList.h"
#include "Rdb.h"
#include "Threads.h"
#include "Posdb.h" // getTermId()

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
bool Msg2::getLists ( long     rdbId       ,
		      char    *coll        ,
		      long     maxAge      ,
		      bool     addToCache  ,
		      //QueryTerm *qterms ,
		      Query *query ,
		      long    *minRecSizes ,
		      //long     numLists    ,
		      RdbList *lists       ,
		      void    *state       ,
		      void   (* callback)(void *state ) ,
		      Msg39Request *request ,
		      long     niceness    ,
		      bool     doMerge     ,
		      bool     isDebug     ,
		      long    *bestSenderHostIds ,
		      bool     restrictPosdb   ,
		      char     forceParitySplit    ,
		      bool     checkCache          ) {
	// warning
	if ( ! coll ) log(LOG_LOGIC,"net: NULL collection. msg2.");
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
	m_req         = request;
	m_qterms              = m_query->m_qterms;
	m_minRecSizes         = minRecSizes;
	m_maxAge              = maxAge;
	m_getComponents       = false;
	m_rdbId               = rdbId;
	m_addToCache          = addToCache;
	m_coll                = coll;
	m_restrictPosdb       = restrictPosdb;
	m_forceParitySplit    = forceParitySplit;
	m_checkCache          = checkCache;
	// MDW: no more than an hr seconds, no matter what. let's keep it fresh
	if ( m_maxAge > 3600 ) m_maxAge = 3600;
	// we haven't got any responses as of yet or sent any requests
	m_numReplies  = 0;
	m_numRequests = 0;
	// save rdbid in case getDbnameFromId() is called below
	m_msg5[0].m_rdbId = rdbId;
	// start the timer
	m_startTime = gettimeofdayInMilliseconds();
	// set this
	m_numLists = m_query->m_numTerms;
	// make sure not too many lists being requested
	if ( m_numLists > MAX_NUM_LISTS ) {g_errno=ETOOMANYLISTS; return true;}
	// clear them all
	for ( long i = 0 ; i < m_numLists ; i++ ) {
		m_inProgress[i] = true;
		//m_slotNum[i] = -1;
	}
	// all msg5 available for use
	for ( long i = 0 ; i < MSG2_MAX_REQUESTS ; i++ ) m_avail[i] = true;
	if ( m_isDebug ) {
		if ( m_getComponents ) log ("query: Getting components.");
		else                   log ("query: Getting lists.");
	}
	// reset error
	m_errno = 0;
	// fetch what we need
	return getLists ( );
}

bool Msg2::getLists ( ) {
	// if we're just using the root file of indexdb to save seeks
	long numFiles;
	bool includeTree;
	if ( m_restrictPosdb ) { numFiles =  1; includeTree = false; }
	else                   { numFiles = -1; includeTree = true;  }

	//long pass = 0;
	//bool redo = false;

	// loop:
	// . send out a bunch of msg5 requests
	// . make slots for all
	for ( long i = 0 ; i < m_numLists ; i++ ) {			
		// if any had error, forget the rest. do not launch any more
		if ( m_errno ) break;
		// skip if already did it
		if ( ! m_inProgress[i] ) continue;
		// skip if currently launched
		//if ( m_slotNum[i] >= 0 ) continue;
		if ( ! m_avail[i] ) continue;
		// do not allow too many outstanding requests
		//if ( m_numRequests - m_numReplies >= MSG2_MAX_REQUESTS )
		//	return false;
		// . reset it just in case it was being recycled
		// . now we call Msg39::reset() which frees each m_list[i]
		//m_lists[i].freeList();
		// skip if no bytes requested
		if ( m_minRecSizes[i] == 0 ) continue;
		// get a free msg5
		//long j = 0;
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
			sk = (key144_t *)m_qterms[i].m_startKey;
			ek = (key144_t *)m_qterms[i].m_endKey;
			long long docId0 = g_posdb.getDocId(sk);
			long long docId1 = g_posdb.getDocId(ek);
			log("query: reading termlist #%li "//from "
			    //"distributed cache on host #%li. "
			    "termId=%lli. k=%s mr=%li (docid0=%lli -"
			    "docid1=%lli).",
			    i,
			    //hostId, 
			    g_posdb.getTermId(sk),
			    KEYSTR(sk,sizeof(POSDBKEY)),
			    //sk->n2,
			    //sk->n1,
			    //(long)sk->n0,
			    m_minRecSizes[i],
			    docId0,
			    docId1);
		}
		
		long minRecSize = m_minRecSizes[i];

		// sanity check
		if ( ( minRecSize > ( 500 * 1024 * 1024 ) || 
		       minRecSize < 0) ){
			log( "minRecSize = %li", minRecSize );
			char *xx=NULL; *xx=0;
		}

		//bool forceLocalIndexdb = true;
		// if it is a no-split term, we may gotta get it over the net
		//if ( ! m_qterms[i].isSplit() ) 
		//	forceLocalIndexdb = false;
		// stash this 
		m_msg5[i].m_parent = this;
		m_msg5[i].m_i      = i;

		QueryTerm *qt = &m_qterms[i];

		char *sk2 = NULL;
		char *ek2 = NULL;
		sk2 = qt->m_startKey;
		ek2 = qt->m_endKey;

		// if single word and not required, skip it
		if ( ! qt->m_isRequired && 
		     ! qt->m_isPhrase &&
		     ! qt->m_synonymOf )
			continue;

		/*
		// if doing a gbdocid:| restricted query then use msg0
		// because it is probably stored remotely!
		if ( m_query->m_docIdRestriction ) {
			// try to just ask one host for this termlist
			long long d = m_query->m_docIdRestriction;
			unsigned long gid = g_hostdb.getGroupIdFromDocId ( d );
			Host *group = g_hostdb.getGroupFromGroupId ( gid );
			long hoff = d % g_hostdb.getNumHostsPerGroup();
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
		if ( ! m_msg5[i].getList ( 
					   m_rdbId         , // rdbid
					   m_coll        ,
					   &m_lists[i], // listPtr
					   sk2,//&m_startKeys  [i*ks],
					   ek2,//&m_endKeys    [i*ks],
					   minRecSize  ,
					   includeTree,//true, // include tree?
					   false , // addtocache
					   0, // maxcacheage
					   0              , // start file num
					   numFiles,//-1    , // num files
					   &m_msg5[i]     , // state
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
			m_avail     [i] = false;
			continue;
		}

		//	noblock:

		// do not allow it to be re-used since now posdb
		// calls Msg2::getListGroup()
		//m_slotNum   [i] = i;
		// that is no longer the case!! we do a merge now... i
		// think we decided it was easier to deal with shit n posdb.cpp
		// but i don't know how much this matters really
		m_avail     [i] = false;

		// set our end time if we need to
		//if ( g_conf.m_logTimingNet )
		//	m_endTimes[i] = gettimeofdayInMilliseconds();
		// if the list is empty, we can get its components now
		m_inProgress[i] = false;
		// we didn't block, so do this
		m_numReplies++; 
		m_numRequests++; 
		// break out on error and wait for replies if we blocked
		if ( g_errno ) {
			m_errno = g_errno;
			log("query: Got error reading termlist: %s.",
			    mstrerror(g_errno));
			break;
		}
		// note it
		//if ( m_isDebug )
		//	logf(LOG_DEBUG,"query: got list #%li size=%li",
		//	     i,m_lists[i].getListSize() );
		// count it
		//m_totalRead += m_lists[i].getListSize();
	}
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

/*
void gotListWrapper0 ( void *state ) {
	Msg0 *msg0 = (Msg0 *)state;
	Msg2 *THIS = (Msg2 *)msg0->m_parent;
	RdbList *list = msg0->m_list;
	// get list #. TODO: make sure this works
	long i = list - &THIS->m_lists[0];
 	THIS->m_inProgress[i] = false;
	THIS->m_numReplies++;
	if ( THIS->m_isDebug ) {
		if ( ! list )
			logf(LOG_DEBUG,"query: got0 NULL list #%li",  i);
		else
			logf(LOG_DEBUG,"query: got0 list #%li size=%li",
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
	//long i  = list - THIS->m_lists;
	long i = ms->m_i;
	//if ( i < 0 || i >= MSG2_MAX_REQUESTS ) { char *xx=NULL;*xx=0; }
	//long nn = THIS->m_slotNum [ i ];
 	THIS->m_inProgress[ i] = false;
	// now we keep for because Msg2::getGroupList() needs it!!
	//THIS->m_avail     [nn] = true;
	//THIS->m_slotNum   [ i] = -1;
	// . now m_linkInfo[i] (for some i, i dunno which) is filled
	THIS->m_numReplies++;
	// note it
	if ( THIS->m_isDebug ) {
		if ( ! list )
			logf(LOG_DEBUG,"query: got NULL list #%li",  i);
		else
			logf(LOG_DEBUG,"query: got list #%li size=%li",
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
	//	long size = -1;
	//	if ( list ) size = list->getListSize();
	//	log("Msg2::got list size=%li listPtr=%li", size , (long)list );
	//}

	// . return true on error
	// . no, wait to get all the replies because we destroy ourselves
	//   by calling the callback, and another reply may come back and
	//   think we're still around. so, ideally, destroy those udp slots
	//   OR just wait for all replies to come in.
	//if ( g_errno ) return true;
	if ( m_errno )
		log("net: Had error fetching data from %s: %s.", 
		    getDbnameFromId(m_msg5[0].m_rdbId),
		    mstrerror(m_errno) );

	// note it
	if ( m_isDebug ) {
		for ( long i = 0 ; i < m_numLists ; i++ ) {
			log("msg2: read termlist #%li size=%li",
			    i,m_lists[i].m_listSize);
		}
	}

	// bitch if we hit our max read sizes limit, we are losing docids!
	for ( long i = 0 ; i < m_numLists ; i++ ) {
		if ( m_lists[i].m_listSize < m_minRecSizes[i] ) continue;
		if ( m_minRecSizes[i] == 0 ) continue;
		log("msg2: read termlist #%li size=%li maxSize=%li. losing "
		    "docIds!",
		    i,m_lists[i].m_listSize,m_minRecSizes[i]);
	}


	// debug msg
	long long now = gettimeofdayInMilliseconds();
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
	//for ( long i = 0 ; i < m_numLists ; i++ ) m_needsCaching[i] = false;
	// probably out of memory if this is set
	//if ( err ) return true;
	// all done
	return true;
}
