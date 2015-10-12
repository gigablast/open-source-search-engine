#include "gb-include.h"

#include "Msg39.h"
#include "Stats.h"
#include "Threads.h"
#include "TopTree.h"
#include "UdpServer.h"
//#include "CollectionRec.h"
#include "SearchInput.h"

// called to send back the reply
static void  sendReply         ( UdpSlot *slot         ,
				 Msg39   *msg39        ,
				 char    *reply        ,
				 int32_t     replySize    ,
				 int32_t     replyMaxSize ,
				 bool     hadError     );
// called when Msg2 has got all the termlists
//static void  gotListsWrapper   ( void *state ) ;
// thread wrappers
static void *addListsWrapper   ( void *state , ThreadEntry *t ) ;
//static void  threadDoneWrapper ( void *state , ThreadEntry *t ) ;

bool Msg39::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x39
	if ( ! g_udpServer.registerHandler ( 0x39, handleRequest39 )) 
		return false;
	return true;
}

Msg39::Msg39 () {
	m_inUse = false;
	reset();
}

Msg39::~Msg39 () {
	reset();
}

void Msg39::reset() {
	if ( m_inUse ) { char *xx=NULL;*xx=0; }
	m_allocedTree = false;
	//m_numDocIdSplits = 1;
	m_tmpq.reset();
	m_numTotalHits = 0;
	m_gotClusterRecs = 0;
	reset2();
}

void Msg39::reset2() {
	// reset lists
	int32_t nqt = m_stackBuf.getLength() / sizeof(RdbList);
	//for ( int32_t j = 0 ; j < m_msg2.m_numLists && m_lists ; j++ ) {
	for ( int32_t j = 0 ; j < nqt && m_lists ; j++ ) {
		//m_lists[j].freeList();
		//log("msg39: destroy list @ 0x%"PTRFMT,(PTRTYPE)&m_lists[j]);
		// same thing but more generic
		m_lists[j].destructor();
	}
	m_stackBuf.purge();
	m_lists = NULL;
	m_msg2.reset();
	m_posdbTable.reset();
	m_callback = NULL;
	m_state = NULL;
	m_blocked = false;
	m_tmp = NULL;
}

// . handle a request to get a the search results, list of docids only
// . returns false if slot should be nuked and no reply sent
// . sometimes sets g_errno on error
void handleRequest39 ( UdpSlot *slot , int32_t netnice ) {
	// use Msg39 to get the lists and intersect them
	Msg39 *THIS ;
	try { THIS = new ( Msg39 ); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("msg39: new(%"INT32"): %s", 
		    (int32_t)sizeof(Msg39),mstrerror(g_errno));
		sendReply ( slot , NULL , NULL , 0 , 0 ,true);
		return;
	}
	mnew ( THIS , sizeof(Msg39) , "Msg39" );
	// clear it
	g_errno = 0;
	// . get the resulting docIds, usually blocks
	// . sets g_errno on error
	THIS->getDocIds ( slot ) ;
}

// this must always be called sometime AFTER handleRequest() is called
void sendReply ( UdpSlot *slot , Msg39 *msg39 , char *reply , int32_t replyLen ,
		 int32_t replyMaxSize , bool hadError ) {
	// debug msg
	if ( g_conf.m_logDebugQuery || (msg39&&msg39->m_debug) ) 
		logf(LOG_DEBUG,"query: msg39: [%"PTRFMT"] "
		     "Sending reply len=%"INT32".",
		     (PTRTYPE)msg39,replyLen);

	// sanity
	if ( hadError && ! g_errno ) { char *xx=NULL;*xx=0; }

	// no longer in use. msg39 will be NULL if ENOMEM or something
	if ( msg39 ) msg39->m_inUse = false;

	// . if we enter from a local call and not from handling a udp slot
	//   then execute this logic here to return control to caller.
	// . do not delete ourselves because we will be re-used probably and
	//   caller handles that now.
	if ( msg39 && msg39->m_callback ) {
		// if we blocked call user callback
		if ( msg39->m_blocked ) msg39->m_callback ( msg39->m_state );
		// if not sending back a udp reply, return now
		return;
	}

	// . now we can free the lists before sending
	// . may help a little bit...
	//if ( msg39 ) {
	//	for ( int32_t j = 0 ; j < msg39->m_msg2.m_numLists ; j++ ) 
	//		msg39->m_lists[j].freeList();
	//}
	// get the appropriate UdpServer for this niceness level
	UdpServer *us = &g_udpServer;
	// i guess clear this
	int32_t err = g_errno;
	g_errno = 0;
	// send an error reply if g_errno is set
	if ( err ) us->sendErrorReply ( slot , err ) ; 
	else       us->sendReply_ass ( reply    , 
				       replyLen , 
				       reply    , 
				       replyMaxSize , 
				       slot     );
	// always delete ourselves when done handling the request
	if ( msg39 ) {
		mdelete ( msg39 , sizeof(Msg39) , "Msg39" );
		delete (msg39);
	}
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . calls gotDocIds to send a reply
void Msg39::getDocIds ( UdpSlot *slot ) {
	// remember the slot
	m_slot = slot;
	// reset this
	m_errno = 0;
	// get the request
        m_r  = (Msg39Request *) m_slot->m_readBuf;
        int32_t requestSize = m_slot->m_readBufSize;
        // ensure it's size is ok
        if ( requestSize < 8 ) { 
	BadReq:
		g_errno = EBADREQUESTSIZE; 
		log(LOG_LOGIC,"query: msg39: getDocIds: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}

	// deserialize it before we do anything else
	int32_t finalSize = deserializeMsg ( sizeof(Msg39Request) ,
					  &m_r->size_readSizes ,
					  &m_r->size_whiteList,//coll ,
					  &m_r->ptr_readSizes,
					  m_r->m_buf );

	// sanity check
	if ( finalSize != requestSize ) {
		log("msg39: sending bad request.");
		goto BadReq;
		//char *xx=NULL;*xx=0; }
	}

	getDocIds2 ( m_r );
}

// . the main function to get the docids for the provided query in "req"
// . it always blocks i guess
void Msg39::getDocIds2 ( Msg39Request *req ) {

	// flag it as in use
	m_inUse = true;

	// store it, might be redundant if called from getDocIds() above
	m_r = req;

	// a handy thing
	m_debug = false;
	if ( m_r->m_debug          ) m_debug = true;
	if ( g_conf.m_logDebugQuery  ) m_debug = true;
	if ( g_conf.m_logTimingQuery ) m_debug = true;

        // ensure it's size is ok
	/*
        if ( m_r->size_whiteList <= 0 ) {
		g_errno = ENOCOLLREC;
		log(LOG_LOGIC,"query: msg39: getDocIds: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}
	*/

        CollectionRec *cr = g_collectiondb.getRec ( m_r->m_collnum );
        if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log(LOG_LOGIC,"query: msg39: getDocIds: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}

	// . set our m_q class
	// . m_boolFlag is either 1 or 0 in this case, the caller did the
	//   auto-detect (boolFlag of 2) before calling us
	// . this now calls Query::addCompoundTerms() for us
	if ( ! m_tmpq.set2 ( m_r->ptr_query  , 
			     m_r->m_language ,
			     m_r->m_queryExpansion ,
			     m_r->m_useQueryStopWords ,
			     m_r->m_maxQueryTerms ) ) {
		log("query: msg39: setQuery: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}

	// wtf?
	if ( g_errno ) { char *xx=NULL;*xx=0; }

	QUICKPOLL ( m_r->m_niceness );

	// set m_errno
	if ( m_tmpq.m_truncated ) m_errno = EQUERYTRUNCATED;
	// ensure matches with the msg3a sending us this request
	if ( m_tmpq.getNumTerms() != m_r->m_nqt ) {
		g_errno = EBADENGINEER;
		log("query: Query parsing inconsistency for q=%s. "
		    "%i != %i. "
		    "langid=%"INT32". Check langids and m_queryExpansion parms "
		    "which are the only parms that could be different in "
		    "Query::set2(). You probably have different mysynoyms.txt "
		    "files on two different hosts! check that!!"
		    ,m_tmpq.m_orig
		    ,(int)m_tmpq.getNumTerms()
		    ,(int)m_r->m_nqt
		    ,(int32_t)m_r->m_language
		    );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return ; 
	}
	// debug
	if ( m_debug )
		logf(LOG_DEBUG,"query: msg39: [%"PTRFMT"] Got request "
		     "for q=%s", (PTRTYPE) this,m_tmpq.m_orig);

	// reset this
	m_tt.reset();

	QUICKPOLL ( m_r->m_niceness );

	// . if caller already specified a docid range, then be loyal to that!
	// . or if we do not have enough query terms to warrant splitting
	//if ( m_numDocIdSplits == 1 ) {
	//	getLists();
	//	return;
	//}

	// . set up docid range cursor
	// . do twin splitting
	// . we do no do it this way any more... we subsplit each split
	//   into two halves...!!! see logic in getLists() below!!!
	//if ( m_r->m_stripe == 1 ) {
	//	m_ddd = MAX_DOCID / 2LL;
	//	m_dddEnd = MAX_DOCID + 1LL;
	//}
	//else if ( m_r->m_stripe == 0 ) {
	//	m_ddd = 0;
	//	m_dddEnd = MAX_DOCID / 2LL;
	//}
	// support triplets, etc. later
	//else {
	//	char *xx=NULL;*xx=0; 
	//}

	// do not do twin splitting if only one host per group
	//if ( g_hostdb.getNumStripes() == 1 ) {
	m_ddd    = 0;
	m_dddEnd = MAX_DOCID;
	//}

	m_phase = 0;

	// if ( m_r->m_docsToGet <= 0 ) {
	// 	estimateHitsAndSendReply ( );
	// 	return;
	// }

	// if ( m_tmpq.m_numTerms <= 0 ) {
	// 	estimateHitsAndSendReply ( );
	// 	return;
	// }

	// . otherwise, to prevent oom, split up docids into ranges
	//   and get winners of each range.
	//if ( ! doDocIdSplitLoop() ) return;

	// . return false if it blocks true otherwise
	// . it will send a reply when done
	if ( ! controlLoop() ) return;

	// error?
	// if ( g_errno ) {
	// 	log(LOG_LOGIC,"query: msg39: doDocIdSplitLoop: %s." , 
	// 	    mstrerror(g_errno) );
	// 	sendReply ( m_slot , this , NULL , 0 , 0 , true );
	// 	return ; 
	// }
	// it might not have blocked! if all lists in tree and used no thread
	// it will come here after sending the reply and destroying "this"
	return;
}

void controlLoopWrapper2 ( void *state , ThreadEntry *t ) {
	Msg39 *THIS = (Msg39 *)state;
	THIS->controlLoop();
}


void controlLoopWrapper ( void *state ) {
	Msg39 *THIS = (Msg39 *)state;
	THIS->controlLoop();
}

// . returns false if blocks true otherwise
// 1. read all termlists for docid range
// 2. intersect termlists to get the intersecting docids
// 3. increment docid ranges and keep going
// 4. when done return the top docids
bool Msg39::controlLoop ( ) {

 loop:
	
	// error?
	if ( g_errno ) {
	hadError:
		log(LOG_LOGIC,"query: msg39: controlLoop: %s." , 
		    mstrerror(g_errno) );
		sendReply ( m_slot , this , NULL , 0 , 0 , true );
		return true; 
	}

	if ( m_phase == 0 ) {
		// next phase
		m_phase++;
		// the starting docid...
		int64_t d0 = m_ddd;
		// int16_tcut
		int64_t delta = MAX_DOCID / (int64_t)m_r->m_numDocIdSplits;
		// advance to point to the exclusive endpoint
		m_ddd += delta;
		// ensure this is exclusive of ddd since it will be
		// inclusive in the following iteration.
		int64_t d1 = m_ddd;
		// fix rounding errors
		if ( d1 + 20LL > MAX_DOCID ) {
			d1    = MAX_DOCID;
			m_ddd = MAX_DOCID;
		}
		// fix it
		m_r->m_minDocId = d0;
		m_r->m_maxDocId = d1; // -1; // exclude d1
		// allow posdbtable re-initialization each time to set
		// the msg2 termlist ptrs anew, otherwise we core in
		// call to PosdbTable::init() below
		//m_posdbTable.m_initialized = false;
		// reset ourselves, partially, anyway, not tmpq etc.
		reset2();
		// debug log
		if ( ! m_r->m_forSectionStats && m_debug )
			log("msg39: docid split phase %"INT64"-%"INT64"",d0,d1);
		// wtf?
		//if ( d0 >= d1 ) break;
		// load termlists for these docid ranges using msg2 from posdb
		if ( ! getLists() ) return false;
	}

	if ( m_phase == 1 ) {
		m_phase++;
		// intersect the lists we loaded using a thread
		if ( ! intersectLists() ) return false;
		// error?
		if ( g_errno ) goto hadError;
	}

	// sum up some stats
	if ( m_phase == 2 ) {
		m_phase++;
		if ( m_posdbTable.m_t1 ) {
			// . measure time to add the lists in bright green
			// . use darker green if rat is false (default OR)
			int32_t color;
			//char *label;
			color = 0x0000ff00 ;
			//label = "termlist_intersect";
			g_stats.addStat_r ( 0 , 
					    m_posdbTable.m_t1 , 
					    m_posdbTable.m_t2 , color );
		}
		// accumulate total hits count over each docid split
		m_numTotalHits += m_posdbTable.m_docIdVoteBuf.length() / 6;
		// minus the shit we filtered out because of gbminint/gbmaxint/
		// gbmin/gbmax/gbsortby/gbrevsortby/gbsortbyint/gbrevsortbyint
		m_numTotalHits -= m_posdbTable.m_filtered;
		// error?
		if ( m_posdbTable.m_errno ) {
			// we do not need to store the intersection i guess..??
			m_posdbTable.freeMem();
			g_errno = m_posdbTable.m_errno;
			log("query: posdbtable had error = %s",
			    mstrerror(g_errno));
			sendReply ( m_slot , this , NULL , 0 , 0 ,true);
			return true;
		}
		// if we have more docid ranges remaining do more
		if ( m_ddd < m_dddEnd ) {
			m_phase = 0;
			goto loop;
		}
	}

	// ok, we are done, get cluster recs of the winning docids
	if ( m_phase == 3 ) {
		m_phase++;
		// . this loads them using msg51 from clusterdb
		// . if m_r->m_doSiteClustering is false it just returns true
		// . this sets m_gotClusterRecs to true if we get them
		if ( ! setClusterRecs ( ) ) return false;
		// error setting clusterrecs?
		if ( g_errno ) goto hadError;
	}

	// process the cluster recs if we got them
	if ( m_gotClusterRecs && ! gotClusterRecs() )
		goto hadError;

	// . all done! set stats and send back reply
	// . only sends back the cluster recs if m_gotClusterRecs is true
	estimateHitsAndSendReply();

	return true;
}

/*
// . returns false if blocked, true if done
// . only come here if m_numDocIdSplits > 1
// . to avoid running out of memory, generate the search results for
//   multiple smaller docid-ranges, one range at a time.
bool Msg39::doDocIdSplitLoop ( ) {
	int64_t delta = MAX_DOCID / (int64_t)m_numDocIdSplits;
	for ( ; m_ddd < m_dddEnd ; ) {
		// the starting docid...
		int64_t d0 = m_ddd;
		// advance to point to the exclusive endpoint
		m_ddd += delta;
		// ensure this is exclusive of ddd since it will be
		// inclusive in the following iteration.
		int64_t d1 = m_ddd;
		// fix rounding errors
		if ( d1 + 20LL > MAX_DOCID ) {
			d1    = MAX_DOCID;
			m_ddd = MAX_DOCID;
		}
		// fix it
		m_r->m_minDocId = d0;
		m_r->m_maxDocId = d1; // -1; // exclude d1
		// allow posdbtable re-initialization each time to set
		// the msg2 termlist ptrs anew, otherwise we core in
		// call to PosdbTable::init() below
		//m_posdbTable.m_initialized = false;
		// reset ourselves, partially, anyway, not tmpq etc.
		reset2();
		// debug log
		log("msg39: docid split phase %"INT64"-%"INT64"",d0,d1);
		// wtf?
		if ( d0 >= d1 ) break;
		// use this
		//m_debug = true;
		//log("call1");
		// . get the lists
		// . i think this always should block!
		// . it will also intersect the termlists to get the search
		//   results and accumulate the winners into the "tree"
		if ( ! getLists() ) return false;
		//log("call2 g_errno=%"INT32"",(int32_t)g_errno);
		// if there was an error, stop!
		if ( g_errno ) break;
	}

	// return error reply if we had an error
	if ( g_errno ) {
		log("msg39: Had error3: %s.", mstrerror(g_errno));
		sendReply (m_slot,this,NULL,0,0 , true);
		return true; 
	}

	if ( m_debug ) 
		log("msg39: done with all docid range splits");

	// all done. this will send reply back
	//estimateHitsAndSendReply();
	//addedLists();

	// should we put cluster recs in the tree?
	//m_gotClusterRecs = ( g_conf.m_fullSplit && m_r->m_doSiteClustering );
	m_gotClusterRecs = ( m_r->m_doSiteClustering );
	
	// . before we send the top docids back, lookup their site hashes
	//   in clusterdb so we can do filtering at this point.
	//   BUT only do this if we are in a "full split" config, because that
	//   way we can guarantee all clusterdb recs are local (on this host)
	//   and should be in the page cache. the page cache should do ultra
	//   quick lookups and no gbmemcpy()'s for this operation. it should
	//   be <<1ms to lookup thousands of docids.
	// . when doing innerLoopSiteClustering we always use top tree now
	//   because our number of "top docids" can be somewhat unpredictably 
	//   large due to having a ton of results with the same "domain hash" 
	//   (see the "vcount" in IndexTable2.cpp)
	// . do NOT do if we are just "getting weights", phr and aff weights
	if ( m_gotClusterRecs ) {
		// . set the clusterdb recs in the top tree
		// . this calls estimateHits() in its reply wrapper when done
		return setClusterRecs ( ) ;
	}

	// if we did not call setClusterRecs, go on to estimate the hits
	estimateHitsAndSendReply();

	// no block, we are done
	return true;
}
*/

// void tryAgainWrapper ( int fd , void *state ) {
// 	Msg39 *THIS = (Msg39 *)state;
// 	g_loop.unregisterSleepCallback ( state , tryAgainWrapper );
// 	THIS->getLists();
// }


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . called either from 
//   1) doDocIdSplitLoop
//   2) or getDocIds2() if only 1 docidsplit
bool Msg39::getLists () {

	if ( m_debug ) m_startTime = gettimeofdayInMilliseconds();
	// . ask Indexdb for the IndexLists we need for these termIds
	// . each rec in an IndexList is a termId/score/docId tuple

	//
	// restrict to docid range?
	//
	// . get the docid start and end
	// . do docid paritioning so we can send to all hosts
	//   in the network, not just one stripe
	int64_t docIdStart = 0;
	int64_t docIdEnd = MAX_DOCID;
	// . restrict to this docid?
	// . will really make gbdocid:| searches much faster!
	int64_t dr = m_tmpq.m_docIdRestriction;
	if ( dr ) {
		docIdStart = dr;
		docIdEnd   = dr + 1;
	}
	// . override
	// . this is set from Msg39::doDocIdSplitLoop() to compute 
	//   search results in stages, so that we do not load massive
	//   termlists into memory and got OOM (out of memory)
	if ( m_r->m_minDocId != -1 ) docIdStart = m_r->m_minDocId;
	if ( m_r->m_maxDocId != -1 ) docIdEnd   = m_r->m_maxDocId+1;
	
	// if we have twins, then make sure the twins read different
	// pieces of the same docid range to make things 2x faster
	//bool useTwins = false;
	//if ( g_hostdb.getNumStripes() == 2 ) useTwins = true;
	//if ( useTwins ) {
	//	int64_t delta2 = ( docIdEnd - docIdStart ) / 2;
	//	if ( m_r->m_stripe == 0 ) docIdEnd = docIdStart + delta2;
	//	else                      docIdStart = docIdStart + delta2;
	//}
	// new striping logic:
	int32_t numStripes = g_hostdb.getNumStripes();
	int64_t delta2 = ( docIdEnd - docIdStart ) / numStripes;
	int32_t stripe = g_hostdb.getMyHost()->m_stripe;
	docIdStart += delta2 * stripe; // is this right?
	docIdEnd = docIdStart + delta2;
	// add 1 to be safe so we don't lose a docid
	docIdEnd++;
	// TODO: add triplet support later for this to split the
	// read 3 ways. 4 ways for quads, etc.
	//if ( g_hostdb.getNumStripes() >= 3 ) { char *xx=NULL;*xx=0;}
	// do not go over MAX_DOCID  because it gets masked and
	// ends up being 0!!! and we get empty lists
	if ( docIdEnd > MAX_DOCID ) docIdEnd = MAX_DOCID;
	// remember so Msg2.cpp can use them to restrict the termlists 
	// from "whiteList" as well
	m_docIdStart = docIdStart;
	m_docIdEnd   = docIdEnd;
	

	//
	// set startkey/endkey for each term/termlist
	//
	for ( int32_t i = 0 ; i < m_tmpq.getNumTerms() ; i++ ) {
		// breathe
		QUICKPOLL ( m_r->m_niceness );
		// int16_tcuts
		QueryTerm *qterm = &m_tmpq.m_qterms[i];
		char *sk = qterm->m_startKey;
		char *ek = qterm->m_endKey;
		// get the term id
		int64_t tid = m_tmpq.getTermId(i);
		// if only 1 stripe
		//if ( g_hostdb.getNumStripes() == 1 ) {
		//	docIdStart = 0;
		//	docIdEnd   = MAX_DOCID;
		//}
		// debug
		if ( m_debug )
			log("query: setting sk/ek for docids %"INT64""
			    " to %"INT64" for termid=%"INT64""
			    , docIdStart
			    , docIdEnd
			    , tid
			    );
		// store now in qterm
		g_posdb.makeStartKey ( sk , tid , docIdStart );
		g_posdb.makeEndKey   ( ek , tid , docIdEnd   );
		qterm->m_ks = sizeof(POSDBKEY);//key144_t);
	}

	// debug msg
	if ( m_debug || g_conf.m_logDebugQuery ) {
		for ( int32_t i = 0 ; i < m_tmpq.getNumTerms() ; i++ ) {
			// get the term in utf8
			//char bb[256];
			QueryTerm *qt = &m_tmpq.m_qterms[i];
			//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
			char *tpc = qt->m_term + qt->m_termLen;
			char  tmp = *tpc;
			*tpc = '\0';
			char sign = qt->m_termSign;
			if ( sign == 0 ) sign = '0';
			QueryWord *qw = qt->m_qword;
			int32_t wikiPhrId = qw->m_wikiPhraseId;
			if ( m_tmpq.isPhrase(i) ) wikiPhrId = 0;
			char leftwikibigram = 0;
			char rightwikibigram = 0;
			if ( qt->m_leftPhraseTerm &&
			     qt->m_leftPhraseTerm->m_isWikiHalfStopBigram )
				leftwikibigram = 1;
			if ( qt->m_rightPhraseTerm &&
			     qt->m_rightPhraseTerm->m_isWikiHalfStopBigram )
				rightwikibigram = 1;
			/*
			char c = m_tmpq.getTermSign(i);
			char tt[512];
			int32_t ttlen = m_tmpq.getTermLen(i);
			if ( ttlen > 254 ) ttlen = 254;
			if ( ttlen < 0   ) ttlen = 0;
			// old:painful: convert each term from unicode to ascii
			gbmemcpy ( tt , m_tmpq.getTerm(i) , ttlen );
			*/
			int32_t isSynonym = 0;
			QueryTerm *st = qt->m_synonymOf;
			if ( st ) isSynonym = true;
			SafeBuf sb;
			// now we can display it
			//tt[ttlen]='\0';
			//if ( c == '\0' ) c = ' ';
			sb.safePrintf(
			     "query: msg39: [%"PTRFMT"] "
			     "query term #%"INT32" \"%s\" "
			     "phr=%"INT32" termId=%"UINT64" rawTermId=%"UINT64" "
			     //"estimatedTermFreq=%"INT64" (+/- ~16000) "
			     "tfweight=%.02f "
			     "sign=%c "
			     "numPlusses=%hhu "
			     "required=%"INT32" "
			     "fielcode=%"INT32" "

			     "ebit=0x%0"XINT64" "
			     "impBits=0x%0"XINT64" "

			     "wikiphrid=%"INT32" "
			     "leftwikibigram=%"INT32" "
			     "rightwikibigram=%"INT32" "
			     //"range.startTermNum=%hhi range.endTermNum=%hhi "
			     //"minRecSizes=%"INT32" "
			     "readSizeInBytes=%"INT32" "
			     //"ebit=0x%"XINT64" "
			     //"impBits=0x%"XINT64" "
			     "hc=%"INT32" "
			     "component=%"INT32" "
			     "otermLen=%"INT32" "
			     "isSynonym=%"INT32" "
			     "querylangid=%"INT32" " ,
			     (PTRTYPE)this ,
			     i          ,
			     qt->m_term,//bb ,
			     (int32_t)m_tmpq.isPhrase (i) ,
			     m_tmpq.getTermId      (i) ,
			     m_tmpq.getRawTermId   (i) ,
			     ((float *)m_r->ptr_termFreqWeights)[i] ,
			     sign , //c ,
			     0 , 
			     (int32_t)qt->m_isRequired,
			     (int32_t)qt->m_fieldCode,

			     (int64_t)qt->m_explicitBit  ,
			     (int64_t)qt->m_implicitBits ,

			     wikiPhrId,
			     (int32_t)leftwikibigram,
			     (int32_t)rightwikibigram,
			     ((int32_t *)m_r->ptr_readSizes)[i]         ,
			     //(int64_t)m_tmpq.m_qterms[i].m_explicitBit  ,
			     //(int64_t)m_tmpq.m_qterms[i].m_implicitBits ,
			     (int32_t)m_tmpq.m_qterms[i].m_hardCount ,
			     (int32_t)m_tmpq.m_qterms[i].m_componentCode,
			     (int32_t)m_tmpq.getTermLen(i) ,
			     isSynonym,
			     (int32_t)m_tmpq.m_langId ); // ,tt
			// put it back
			*tpc = tmp;
			if ( st ) {
				int32_t stnum = st - m_tmpq.m_qterms;
				sb.safePrintf("synofterm#=%"INT32"",stnum);
				//sb.safeMemcpy(st->m_term,st->m_termLen);
				sb.pushChar(' ');
				sb.safePrintf("synwid0=%"INT64" ",qt->m_synWids0);
				sb.safePrintf("synwid1=%"INT64" ",qt->m_synWids1);
				sb.safePrintf("synalnumwords=%"INT32" ",
					      qt->m_numAlnumWordsInSynonym);
				// like for synonym "nj" it's base,
				// "new jersey" has 2 alnum words!
				sb.safePrintf("synbasealnumwords=%"INT32" ",
					      qt->m_numAlnumWordsInBase);
			}
			logf(LOG_DEBUG,"%s",sb.getBufStart());

		}
		m_tmpq.printBooleanTree();
	}
	// timestamp log
	if ( m_debug ) 
		log(LOG_DEBUG,"query: msg39: [%"PTRFMT"] "
		    "Getting %"INT32" index lists ",
		     (PTRTYPE)this,m_tmpq.getNumTerms());
	// . now get the index lists themselves
	// . return if it blocked
	// . not doing a merge (last parm) means that the lists we receive
	//   will be an appending of a bunch of lists so keys won't be in order
	// . merging is uneccessary for us here because we hash the keys anyway
	// . and merging takes up valuable cpu time
	// . caution: the index lists returned from Msg2 are now compressed
	// . now i'm merging because it's 10 times faster than hashing anyway
	//   and the reply buf should now always be <= minRecSizes so we can
	//   pre-allocate one better, and, 3) this should fix the yahoo.com 
	//   reindex bug
	char rdbId = RDB_POSDB;

	// . TODO: MDW: fix
	// . partap says there is a bug in this??? we can't cache UOR'ed lists?
	bool checkCache = false;
	// split is us????
	//int32_t split = g_hostdb.m_myHost->m_group;
	int32_t split = g_hostdb.m_myHost->m_shardNum;


	int32_t nqt = m_tmpq.getNumTerms();
	int32_t need = sizeof(RdbList) * nqt ;
	m_stackBuf.setLabel("stkbuf2");
	if ( ! m_stackBuf.reserve ( need ) ) return true;
	m_lists = (IndexList *)m_stackBuf.getBufStart();
	m_stackBuf.setLength ( need );
	for ( int32_t i = 0 ; i < nqt ; i++ ) {
		m_lists[i].constructor();
		//log("msg39: constructlist @ 0x%"PTRFMT,(PTRTYPE)&m_lists[i]);
	}

	// call msg2
	if ( ! m_msg2.getLists ( rdbId                      ,
				 m_r->m_collnum,//m_r->ptr_coll              ,
				 m_r->m_maxAge              ,
				 m_r->m_addToCache          ,
				 //m_tmpq.m_qterms ,
				 &m_tmpq,
				 m_r->ptr_whiteList,
				 // we need to restrict docid range for
				 // whitelist as well! this is from
				 // doDocIdSplitLoop()
				 m_docIdStart,
				 m_docIdEnd,
				 // how much of each termlist to read in bytes
				 (int32_t *)m_r->ptr_readSizes ,
				 //m_tmpq.getNumTerms()       , // numLists
				 // 1-1 with query terms
				 m_lists                    ,
				 this                       ,
				 controlLoopWrapper,//gotListsWrapper      ,
				 m_r                        ,
				 m_r->m_niceness            ,
				 true                       , // do merge?
				 m_debug                  ,
				 NULL                       ,  // best hostids
				 m_r->m_restrictPosdbForQuery  ,
				 split                      ,
				 checkCache                 )) {
		m_blocked = true;
		return false;
	}

	// error?
	//if ( g_errno ) { 
	//	log("msg39: Had error getting termlists2: %s.",
	//	    mstrerror(g_errno));
	//	// don't bail out here because we are in docIdSplitLoop()
	//	//sendReply (m_slot,this,NULL,0,0,true);
	//	return true; 
	//}
	
	//return gotLists ( true );
	return true;
}

/*
void gotListsWrapper ( void *state ) {
	Msg39 *THIS = (Msg39 *) state;

	// save this
	int32_t numDocIdSplits = THIS->m_numDocIdSplits;

	// . hash the lists into our index table
	// . this will send back a reply or recycle and read more list data
	// . this may call addedLists() which may call 
	//   estimateHitsAndSendReply() which nukes "THIS" msg39 but
	//   it only does that if  m_numDocIdSplits is 1
	// . this make nuke msg39
	if ( ! THIS->gotLists ( true ) ) return;


	// . if he did not block and there was an errno we send reply
	//   otherwise if there was NO error he will have sent the reply
	// . if gotLists() was called in the ABOVE function and it returns
	//   true then the docIdLoop() function will send back the reply.
	if ( g_errno ) {
		log("msg39: sending back error reply = %s",mstrerror(g_errno));
		sendReply ( THIS->m_slot , THIS , NULL , 0 , 0 ,true);
	}

	// no, block? call the docid split loop
	// . but if we only had one split msg39 will have been nuked
	//if ( numDocIdSplits <= 1 ) return;

	// if we get the lists and processed them without blocking, repeat!
	if ( ! THIS->doDocIdSplitLoop() ) return;

	// send back reply
	estimateHitsAndSendReply();
}
*/

// . now come here when we got the necessary index lists
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Msg39::intersectLists ( ) { // bool updateReadInfo ) {
	// bail on error
	if ( g_errno ) { 
	hadError:
		log("msg39: Had error getting termlists: %s.",
		    mstrerror(g_errno));
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		//sendReply (m_slot,this,NULL,0,0,true);
		return true; 
	}
	// timestamp log
	if ( m_debug ) {
		log(LOG_DEBUG,"query: msg39: [%"PTRFMT"] "
		    "Got %"INT32" lists in %"INT64" ms"
		    , (PTRTYPE)this,m_tmpq.getNumTerms(),
		     gettimeofdayInMilliseconds() - m_startTime);
		m_startTime = gettimeofdayInMilliseconds();
	}

	// breathe
	QUICKPOLL ( m_r->m_niceness );

	// ensure collection not deleted from under us
	CollectionRec *cr = g_collectiondb.getRec ( m_r->m_collnum );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		goto hadError;
	}

	// . set the IndexTable so it can set it's score weights from the
	//   termFreqs of each termId in the query
	// . this now takes into account the special termIds used for sorting
	//   by date (0xdadadada and 0xdadadad2 & TERMID_MASK)
	// . it should weight them so much so that the summation of scores
	//   from other query terms cannot make up for a lower date score
	// . this will actually calculate the top
	// . this might also change m_tmpq.m_termSigns 
	// . this won't do anything if it was already called
	m_posdbTable.init ( &m_tmpq                ,
			    m_debug              ,
			    this                   ,
			    &m_tt                  ,
			    m_r->m_collnum,//ptr_coll          , 
			    &m_msg2 , // m_lists                ,
			    //m_tmpq.m_numTerms      , // m_numLists
			    m_r                              );

	// breathe
	QUICKPOLL ( m_r->m_niceness );

	// . we have to do this here now too
	// . but if we are getting weights, we don't need m_tt!
	// . actually we were using it before for rat=0/bool queries but
	//   i got rid of NO_RAT_SLOTS
	if ( ! m_allocedTree && ! m_posdbTable.allocTopTree() ) {
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		//sendReply ( m_slot , this , NULL , 0 , 0 , true);
		return true;
	}

	// if msg2 had ALL empty lists we can cut it int16_t
	if ( m_posdbTable.m_topTree->m_numNodes == 0 ) {
		//estimateHitsAndSendReply ( );
		return true;
	}
		

	// we have to allocate this with each call because each call can
	// be a different docid range from doDocIdSplitLoop.
	if ( ! m_posdbTable.allocWhiteListTable() ) {
		log("msg39: Had error allocating white list table: %s.",
		    mstrerror(g_errno));
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		//sendReply (m_slot,this,NULL,0,0,true);
		return true; 
	}


	// do not re do it if doing docid range splitting
	m_allocedTree = true;


	// . now we must call this separately here, not in allocTopTree()
	// . we have to re-set the QueryTermInfos with each docid range split
	//   since it will set the list ptrs from the msg2 lists
	if ( ! m_posdbTable.setQueryTermInfo () ) return true;

	// print query term bit numbers here
	for ( int32_t i = 0 ; m_debug && i < m_tmpq.getNumTerms() ; i++ ) {
		QueryTerm *qt = &m_tmpq.m_qterms[i];
		//utf16ToUtf8(bb, 256, qt->m_term, qt->m_termLen);
		char *tpc = qt->m_term + qt->m_termLen;
		char  tmp = *tpc;
		*tpc = '\0';
		SafeBuf sb;
		sb.safePrintf("query: msg39: BITNUM query term #%"INT32" \"%s\" "
			      "bitnum=%"INT32" ", i , qt->m_term, qt->m_bitNum );
		// put it back
		*tpc = tmp;
		logf(LOG_DEBUG,"%s",sb.getBufStart());
	}


	// timestamp log
	if ( m_debug ) {
		log(LOG_DEBUG,"query: msg39: [%"PTRFMT"] "
		    "Preparing to intersect "
		     "took %"INT64" ms",
		     (PTRTYPE)this, 
		    gettimeofdayInMilliseconds() - m_startTime );
		m_startTime = gettimeofdayInMilliseconds();
	}

	// time it
	int64_t start = gettimeofdayInMilliseconds();
	int64_t diff;

	// . don't bother making a thread if lists are small
	// . look at STAGE? in IndexReadInfo.cpp to see how we read in stages
	// . it's always saying msg39 handler is hogging cpu...could this be it
	//if ( m_msg2.getTotalRead() < 2000*8 ) goto skipThread;

	// debug
	//goto skipThread;

	// . NOW! let's do this in a thread so we can continue to service
	//   incoming requests
	// . don't launch more than 1 thread at a time for this
	// . set callback when thread done

	// breathe
	QUICKPOLL ( m_r->m_niceness );

	// . create the thread
	// . only one of these type of threads should be launched at a time
	if ( ! m_debug &&
	     g_threads.call ( INTERSECT_THREAD  , // threadType
			      m_r->m_niceness   ,
			      this              , // top 4 bytes must be cback
			      controlLoopWrapper2,//threadDoneWrapper ,
			      addListsWrapper   ) ) {
		m_blocked = true;
		return false;
	}
	// if it failed
	//log(LOG_INFO,"query: Intersect thread creation failed. Doing "
	//    "blocking. Hurts performance.");
	// check tree
	if ( m_tt.m_nodes == NULL ) {
		log(LOG_LOGIC,"query: msg39: Badness."); 
		char *xx = NULL; *xx = 0; }

	// sometimes we skip the thread
	//skipThread:
	// . addLists() should never have a problem
	// . g_errno should be set by prepareToAddLists() above if there is
	//   going to be a problem
	//if ( m_r->m_useNewAlgo )
	m_posdbTable.intersectLists10_r ( );
	//else
	//	m_posdbTable.intersectLists9_r ( );

	// time it
	diff = gettimeofdayInMilliseconds() - start;
	if ( diff > 10 ) log("query: Took %"INT64" ms for intersection",diff);

	// returns false if blocked, true otherwise
	//return addedLists ();
	return true;
}

void *addListsWrapper ( void *state , ThreadEntry *t ) {
	// we're in a thread now!
	Msg39 *THIS = (Msg39 *)state;
	// . do the add
	// . addLists() returns false and sets errno on error
	// . hash the lists into our table
	// . this returns false and sets g_errno on error
	// . Msg2 always compresses the lists so be aware that the termId
	//   has been discarded
	//THIS->m_posdbTable.intersectLists9_r ();
	//if ( THIS->m_r->m_useNewAlgo )
	THIS->m_posdbTable.intersectLists10_r ( );
	//else
	//	THIS->m_posdbTable.intersectLists9_r ( );
	// . exit the thread
	// . top 4 bytes of "state" ptr should be our done callback
	// . threadDoneWrapper will be called by g_loop when he gets the 
	//   thread's termination signal, sig niceness is m_niceness
	// . bogus return
	return NULL;
}

/*
// we come here after thread exits
void threadDoneWrapper ( void *state , ThreadEntry *t ) {
	// get this class
	Msg39 *THIS = (Msg39 *)state;
	// sanity check
	if ( ! THIS->m_blocked ) { char *xx=NULL;*xx=0; }

	// addedLists() could send reply and destroy "THIS" so save this.
	// it will only sendReply back if it calls estimateHits() which
	// is only called if numDocIdSplits <= 1...
	int32_t numDocIdSplits = THIS->m_numDocIdSplits;
	char debug = THIS->m_debug;

	// just return if it blocked
	if ( ! THIS->addedLists () ) {
		// this can't block
		if ( numDocIdSplits >= 2 ) { char *xx=NULL;*xx=0; }
		if ( debug ) log("msg39: addedLists blocked");
		return;
	}
	if ( debug ) log("msg39: addedLists no block. i guess reply sent");


	// . if he did not block and there was an errno we send reply
	//   otherwise if there was NO error he will have sent the reply
	// . if gotLists() was called in the ABOVE function and it returns
	//   true then the docIdLoop() function will send back the reply.
	if ( g_errno ) {
		log("msg39: sending back error reply = %s",mstrerror(g_errno));
		sendReply ( THIS->m_slot , THIS , NULL , 0 , 0 ,true);
	}

	// no, block? call the docid split loop
	// . but if we only had one split msg39 will have been nuked
	//if ( numDocIdSplits <= 1 ) return;

	// if we get the lists and processed them without blocking, repeat!
	if ( ! THIS->doDocIdSplitLoop() ) return;

	// send back reply
	estimateHitsAndSendReply();

	// no, block? call the docid split loop
	//if ( numDocIdSplits <= 1 ) return;
	// . just re-do the whole she-bang but do not reset m_tt top tree!!!
	// . it returns false if it blocks
	//THIS->doDocIdSplitLoop();
}
*/
/*
// return false if blocked, true otherwise
bool Msg39::addedLists ( ) {

	if ( m_posdbTable.m_t1 ) {
		// . measure time to add the lists in bright green
		// . use darker green if rat is false (default OR)
		int32_t color;
		//char *label;
		color = 0x0000ff00 ;
		//label = "termlist_intersect";
		g_stats.addStat_r ( 0 , 
				    m_posdbTable.m_t1 , 
				    m_posdbTable.m_t2 , color );
	}


	// accumulate total hits count over each docid split
	m_numTotalHits += m_posdbTable.m_docIdVoteBuf.length() / 6;

	// before wrapping up, complete our docid split loops!
	// so do not send the reply back yet... send reply back from
	// the docid loop function... doDocIdSplitLoop()
	//if ( m_numDocIdSplits >= 2 ) return true;


	// . save some memory,free m_topDocIdPtrs2,m_topScores2,m_topExplicits2
	// . the m_topTree should have been filled from the call to
	//   IndexTable2::fillTopDocIds() and it no longer has ptrs to the
	//   docIds, but has the docIds themselves
	//m_posdbTable.freeMem();

	// error?
	if ( m_posdbTable.m_errno ) {
		// we do not need to store the intersection i guess...??
		m_posdbTable.freeMem();
		g_errno = m_posdbTable.m_errno;
		log("query: posdbtable had error = %s",mstrerror(g_errno));
		sendReply ( m_slot , this , NULL , 0 , 0 ,true);
		return true;
	}


	// should we put cluster recs in the tree?
	//m_gotClusterRecs = ( g_conf.m_fullSplit && m_r->m_doSiteClustering );
	//m_gotClusterRecs = ( m_r->m_doSiteClustering );
	
	// . before we send the top docids back, lookup their site hashes
	//   in clusterdb so we can do filtering at this point.
	//   BUT only do this if we are in a "full split" config, because that
	//   way we can guarantee all clusterdb recs are local (on this host)
	//   and should be in the page cache. the page cache should do ultra
	//   quick lookups and no gbmemcpy()'s for this operation. it should
	//   be <<1ms to lookup thousands of docids.
	// . when doing innerLoopSiteClustering we always use top tree now
	//   because our number of "top docids" can be somewhat unpredictably 
	//   large due to having a ton of results with the same "domain hash" 
	//   (see the "vcount" in IndexTable2.cpp)
	// . do NOT do if we are just "getting weights", phr and aff weights
	// if ( m_gotClusterRecs ) {
	// 	// . set the clusterdb recs in the top tree
	// 	return setClusterRecs ( ) ;
	// }
	// if we did not call setClusterRecs, go on to estimate the hits
	// estimateHitsAndSendReply();
	// return true;

	return true;
}
*/

// . set the clusterdb recs in the top tree
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Msg39::setClusterRecs ( ) {

	if ( ! m_r->m_doSiteClustering ) return true;

	// make buf for arrays of the docids, cluster levels and cluster recs
	int32_t nodeSize  = 8 + 1 + 12;
	int32_t numDocIds = m_tt.m_numUsedNodes;
	m_bufSize = numDocIds * nodeSize;
	m_buf = (char *)mmalloc ( m_bufSize , "Msg39docids" );
	// on error, return true, g_errno should be set
	if ( ! m_buf ) { 
		log("query: msg39: Failed to alloc buf for clustering.");
		sendReply(m_slot,this,NULL,0,0,true);
		return true; 
	}

	// assume we got them
	m_gotClusterRecs = true;

	// parse out the buf
	char *p = m_buf;
	// docIds
	m_clusterDocIds = (int64_t *)p; p += numDocIds * 8;
	m_clusterLevels = (char      *)p; p += numDocIds * 1;
	m_clusterRecs   = (key_t     *)p; p += numDocIds * 12;
	// sanity check
	if ( p > m_buf + m_bufSize ) { char *xx=NULL; *xx=0; }
	
	// loop over all results
	int32_t nd = 0;
	for ( int32_t ti = m_tt.getHighNode() ; ti >= 0 ; 
	      ti = m_tt.getPrev(ti) , nd++ ) {
		// get the guy
		TopNode *t = &m_tt.m_nodes[ti];
		// get the docid
		//int64_t  docId = getDocIdFromPtr(t->m_docIdPtr);
		// store in array
		m_clusterDocIds[nd] = t->m_docId;
		// assume not gotten
		m_clusterLevels[nd] = CR_UNINIT;
		// assume not found, make the whole thing is 0
		m_clusterRecs[nd].n1 = 0;
		m_clusterRecs[nd].n0 = 0LL;
	}

	// store number
	m_numClusterDocIds = nd;

	// sanity check
	if ( nd != m_tt.m_numUsedNodes ) { char *xx=NULL;*xx=0; }

	// . ask msg51 to get us the cluster recs
	// . it should read it all from the local drives
	// . "maxAge" of 0 means to not get from cache (does not include disk)
	if ( ! m_msg51.getClusterRecs ( m_clusterDocIds       ,
					m_clusterLevels       ,
					m_clusterRecs         ,
					m_numClusterDocIds    ,
					m_r->m_collnum ,
					0                     , // maxAge
					false                 , // addToCache
					this                  ,
					//gotClusterRecsWrapper ,
					controlLoopWrapper,
					m_r->m_niceness       ,
					m_debug             ) )
		// did we block? if so, return
		return false;

	// ok, process the replies
	//gotClusterRecs();
	// the above never blocks
	return true;
}

// void gotClusterRecsWrapper ( void *state ) {
// 	// get this class
// 	Msg39 *THIS = (Msg39 *)state;
// 	// be on our way
// 	THIS->gotClusterRecs ();
// }

// return false and set g_errno on error
bool Msg39::gotClusterRecs ( ) {

	if ( ! m_gotClusterRecs ) return true;

	// now tell msg5 to set the cluster levels
	if ( ! setClusterLevels ( m_clusterRecs      ,
				  m_clusterDocIds    ,
				  m_numClusterDocIds ,
				  2                  , // maxdocidsperhostname
				  m_r->m_doSiteClustering ,
				  m_r->m_familyFilter     ,
				  // turn this off, not needed now that
				  // we have the langid in every posdb key
				  0,//m_r->m_language         ,
				  m_debug          ,
				  m_clusterLevels    )) {
		m_errno = g_errno;
		// send back an error reply
		//sendReply ( m_slot , this , NULL , 0 , 0 ,true);
		return false;
	}

	// count this
	m_numVisible = 0;

	// now put the info back into the top tree
	int32_t nd = 0;
	for ( int32_t ti = m_tt.getHighNode() ; ti >= 0 ; 
	      ti = m_tt.getPrev(ti) , nd++ ) {
		// get the guy
		TopNode *t = &m_tt.m_nodes[ti];
		// get the docid
		//int64_t  docId = getDocIdFromPtr(t->m_docIdPtr);
		// sanity check
		if ( t->m_docId != m_clusterDocIds[nd] ) {char *xx=NULL;*xx=0;}
		// set it
		t->m_clusterLevel = m_clusterLevels[nd];
		t->m_clusterRec   = m_clusterRecs  [nd];
		// visible?
		if ( t->m_clusterLevel == CR_OK ) m_numVisible++;
	}

	log(LOG_DEBUG,"query: msg39: %"INT32" docids out of %"INT32" are visible",
	    m_numVisible,nd);

	// free this junk now
	mfree ( m_buf , m_bufSize , "Msg39cluster");
	m_buf = NULL;

	// accumulate total hit count over each docid split!
	//m_numTotalHits += m_posdbTable.m_docIdVoteBuf.length() / 6;

	// before wrapping up, complete our docid split loops!
	// so do not send the reply back yet... send reply back from
	// the docid loop function... doDocIdSplitLoop()
	//if ( m_numDocIdSplits >= 2 ) return;

	// finish up and send back the reply
	//estimateHitsAndSendReply ();
	return true;
}	

void Msg39::estimateHitsAndSendReply ( ) {

	// no longer in use
	m_inUse = false;

	// now this for the query loop on the QueryLogEntries.
	m_topDocId50 = 0LL;
	m_topScore50 = 0.0;

	// a little hack for the seo pipeline in xmldoc.cpp
	m_topDocId  = 0LL;
	m_topScore  = 0.0;
	m_topDocId2 = 0LL;
	m_topScore2 = 0.0;
	int32_t ti = m_tt.getHighNode();
	if ( ti >= 0 ) {
		TopNode *t = &m_tt.m_nodes[ti];
		m_topDocId = t->m_docId;
		m_topScore = t->m_score;
	}
	// try the 2nd one too
	int32_t ti2 = -1;
	if ( ti >= 0 ) ti2 = m_tt.getNext ( ti );
	if ( ti2 >= 0 ) {
		TopNode *t2 = &m_tt.m_nodes[ti2];
		m_topDocId2 = t2->m_docId;
		m_topScore2 = t2->m_score;
	}

	// convenience ptrs. we will store the docids/scores into these arrays
	int64_t *topDocIds;
	double    *topScores;
	key_t     *topRecs;

	// numDocIds counts docs in all tiers when using toptree.
	int32_t numDocIds = m_tt.m_numUsedNodes;

	// the msg39 reply we send back
	int32_t  replySize;
	char *reply;

	//m_numTotalHits = m_posdbTable.m_docIdVoteBuf.length() / 6;

	// make the reply?
	Msg39Reply mr;

	// this is what you want to look at if there is no seo.cpp module...
	if ( ! m_callback ) {
		// if we got clusterdb recs in here, use 'em
		if ( m_gotClusterRecs ) numDocIds = m_numVisible;
		
		// don't send more than the docs that are asked for
		if ( numDocIds > m_r->m_docsToGet) numDocIds =m_r->m_docsToGet;

		// # of QueryTerms in query
		int32_t nqt = m_tmpq.m_numTerms;
		// start setting the stuff
		mr.m_numDocIds = numDocIds;
		// copy # estiamted hits into 8 bytes of reply
		//int64_t est = m_posdbTable.m_estimatedTotalHits;
		// ensure it has at least as many results as we got
		//if ( est < numDocIds ) est = numDocIds;
		// or if too big...
		//if ( numDocIds < m_r->m_docsToGet ) est = numDocIds;
		// . total estimated hits
		// . this is now an EXACT count!
		mr.m_estimatedHits = m_numTotalHits;
		// sanity check
		mr.m_nqt = nqt;
		// the m_errno if any
		mr.m_errno = m_errno;
		// int16_tcut
		PosdbTable *pt = &m_posdbTable;
		// the score info, in no particular order right now
		mr.ptr_scoreInfo  = pt->m_scoreInfoBuf.getBufStart();
		mr.size_scoreInfo = pt->m_scoreInfoBuf.length();
		// that has offset references into posdbtable::m_pairScoreBuf 
		// and m_singleScoreBuf, so we need those too now
		mr.ptr_pairScoreBuf    = pt->m_pairScoreBuf.getBufStart();
		mr.size_pairScoreBuf   = pt->m_pairScoreBuf.length();
		mr.ptr_singleScoreBuf  = pt->m_singleScoreBuf.getBufStart();
		mr.size_singleScoreBuf = pt->m_singleScoreBuf.length();
		// save some time since seo.cpp gets from posdbtable directly,
		// so we can avoid serializing/copying this stuff at least
		if ( ! m_r->m_makeReply ) {
			mr.size_scoreInfo      = 0;
			mr.size_pairScoreBuf   = 0;
			mr.size_singleScoreBuf = 0;
		}
		//mr.m_sectionStats    = pt->m_sectionStats;
		// reserve space for these guys, we fill them in below
		mr.ptr_docIds       = NULL;
		mr.ptr_scores       = NULL;
		mr.ptr_clusterRecs  = NULL;
		// this is how much space to reserve
		mr.size_docIds      = 8 * numDocIds; // int64_t
		mr.size_scores      = sizeof(double) * numDocIds; // float
		// if not doing site clustering, we won't have these perhaps...
		if ( m_gotClusterRecs ) 
			mr.size_clusterRecs = sizeof(key_t) *numDocIds;
		else    
			mr.size_clusterRecs = 0;

		#define MAX_FACETS 20000

		/////////////////
		//
		// FACETS
		//
		/////////////////

		// We can have multiple gbfacet: terms in a query so
		// serialize all the QueryTerm::m_facetHashTables into
		// Msg39Reply::ptr_facetHashList.
		//
		// combine the facet hash lists of each query term into
		// a list of lists. each lsit is preceeded by the query term
		// id of the query term (like gbfacet:xpathsitehash12345)
		// followed by a 4 byte length of the following 32-bit
		// facet values
		int32_t need = 0;
		for ( int32_t i = 0 ; i < m_tmpq.m_numTerms; i++ ) {
			QueryTerm *qt = &m_tmpq.m_qterms[i];
			// skip if not facet
			if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
			     qt->m_fieldCode != FIELD_GBFACETINT &&
			     qt->m_fieldCode != FIELD_GBFACETFLOAT )
				continue;
			HashTableX *ft = &qt->m_facetHashTable;
			if ( ft->m_numSlotsUsed == 0 ) continue;
			int32_t used = ft->m_numSlotsUsed;
			// limit for memory
			if ( used > (int32_t)MAX_FACETS ) {
				log("msg39: truncating facet list to 20000 "
				    "from %"INT32" for %s",used,qt->m_term);
				used = (int32_t)MAX_FACETS;
			}
			// store query term id 64 bit
			need += 8;
			// then size
			need += 4;
			// then buckets. keys and counts
			need += (4+sizeof(FacetEntry)) * used;
			// for # of ALL docs that have this facet, even if
			// not in search results
			need += sizeof(int64_t);
		}
		// allocate
		SafeBuf tmp;
		if ( ! tmp.reserve ( need ) ) {
			log("query: Could not allocate memory "
			    "to hold reply facets");
			sendReply(m_slot,this,NULL,0,0,true);
			return;
		}
		// point to there
		char *p = tmp.getBufStart();
		for ( int32_t i = 0 ; i < m_tmpq.m_numTerms ; i++ ) {
			QueryTerm *qt = &m_tmpq.m_qterms[i];
			// skip if not facet
			if ( qt->m_fieldCode != FIELD_GBFACETSTR &&
			     qt->m_fieldCode != FIELD_GBFACETINT &&
			     qt->m_fieldCode != FIELD_GBFACETFLOAT )
				continue;
			// get all the facet hashes and their counts
			HashTableX *ft = &qt->m_facetHashTable;
			// skip if none
			if ( ft->m_numSlotsUsed == 0 ) continue;
			// store query term id 64 bit
			*(int64_t *)p = qt->m_termId;
			p += 8;
			int32_t used = ft->getNumSlotsUsed();
			if ( used > (int32_t)MAX_FACETS ) 
				used = (int32_t)MAX_FACETS;
			// store count
			*(int32_t *)p = used;
			p += 4;
			int32_t count = 0;
			// for sanity check
			char *pend = p + (used * (4+sizeof(FacetEntry)));
			// serialize the key/val pairs
			for ( int32_t k = 0 ; k < ft->m_numSlots ; k++ ) {
				// skip empty buckets
				if ( ! ft->m_flags[k] ) continue;
				// store key. the hash of the facet value.
				*(int32_t *)p = ft->getKey32FromSlot(k); p += 4;
				// then store count
				//*(int32_t *)p = ft->getVal32FromSlot(k); p += 4;
				// now this has a docid on it so we can
				// lookup the text of the facet in Msg40.cpp
				FacetEntry *fe;
				fe = (FacetEntry *)ft->getValFromSlot(k);
				// sanity
				// no, count can be zero if its a range facet
				// that was never added to. we add those
				// empty FaceEntries only for range facets
				// in Posdb.cpp
				//if(fe->m_count == 0 ) { char *xx=NULL;*xx=0;}
				gbmemcpy ( p , fe , sizeof(FacetEntry) );
				p += sizeof(FacetEntry);
				// do not breach
				if ( ++count >= (int32_t)MAX_FACETS ) break;
			}
			// sanity check
			if ( p != pend ) { char *xx=NULL;*xx=0; }
			// do the next query term
		}
		// now point to that so it can be serialized below
		mr.ptr_facetHashList  = tmp.getBufStart();
		mr.size_facetHashList = p - tmp.getBufStart();//tmp.length();

		/////////////
		//
		// END FACETS
		//
		/////////////

		// how many docs IN TOTAL had the facet, including all docs
		// that did not match the query.
		// it's 1-1 with the query terms.
		mr.ptr_numDocsThatHaveFacetList  = NULL;
		mr.size_numDocsThatHaveFacetList = nqt * sizeof(int64_t);

		// . that is pretty much it,so serialize it into buffer,"reply"
		// . mr.ptr_docIds, etc., will point into the buffer so we can
		//   re-serialize into it below from the tree
		// . returns NULL and sets g_errno on error
		// . "true" means we should make mr.ptr_* reference into the 
		//   newly  serialized buffer.
		reply = serializeMsg ( sizeof(Msg39Reply), // baseSize
				       &mr.size_docIds, // firstSizeParm
				       &mr.size_clusterRecs,//lastSizePrm
				       &mr.ptr_docIds , // firstStrPtr
				       &mr , // thisPtr
				       &replySize , 
				       NULL , 
				       0 , 
				       true ) ;
		if ( ! reply ) {
			log("query: Could not allocated memory "
			    "to hold reply of docids to send back.");
			sendReply(m_slot,this,NULL,0,0,true);
			return;
		}
		topDocIds    = (int64_t *) mr.ptr_docIds;
		topScores    = (double    *) mr.ptr_scores;
		topRecs      = (key_t     *) mr.ptr_clusterRecs;

		// sanity
		if ( nqt != m_msg2.m_numLists )
			log("query: nqt mismatch for q=%s",m_tmpq.m_orig);
		int64_t *facetCounts=(int64_t*)mr.ptr_numDocsThatHaveFacetList;
		for ( int32_t i = 0 ; i < nqt ; i++ ) {
			QueryTerm *qt = &m_tmpq.m_qterms[i];
			// default is 0 for non-facet termlists
			facetCounts[i] = qt->m_numDocsThatHaveFacet;
		}
		/*
		  MDW - no, because some docs have the same facet field
		  multiple times and we want a doc count. so do it in Posdb.cpp
		// fill these in now too
		int64_t *facetCounts=(int64_t*)mr.ptr_numDocsThatHaveFacetList;
		for ( int32_t i = 0 ; i < nqt ; i++ ) {
			// default is 0 for non-facet termlists
			facetCounts[i] = 0;
			QueryTerm *qt = &m_tmpq.m_qterms[i];
			// skip if not facet term
			bool isFacetTerm = false;
			if ( qt->m_fieldCode == FIELD_GBFACETSTR )
				isFacetTerm = true;
			if ( qt->m_fieldCode == FIELD_GBFACETINT )
				isFacetTerm = true;
			if ( qt->m_fieldCode == FIELD_GBFACETFLOAT )
				isFacetTerm = true;
			if ( ! isFacetTerm )
				continue;
			RdbList *list = &m_lists[i];
			// they should be all 12 bytes except first rec which
			// is 18 bytes.
			int64_t count = list->m_listSize;
			count -= 6;
			count /= 12;
			facetCounts[i] = count;
		}
		*/	
	}

	int32_t docCount = 0;
	// loop over all results in the TopTree
	for ( int32_t ti = m_tt.getHighNode() ; ti >= 0 ; 
	      ti = m_tt.getPrev(ti) ) {
		// get the guy
		TopNode *t = &m_tt.m_nodes[ti];
		// skip if clusterLevel is bad!
		if ( m_gotClusterRecs && t->m_clusterLevel != CR_OK ) 
			continue;

		// if not sending back a reply... we were called from seo.cpp
		// State3f logic to evaluate a QueryLogEntry, etc.
		if ( m_callback ) {
			// skip results past #50
			if ( docCount > 50 ) continue;
			// set this
			m_topScore50 = t->m_score;
			m_topDocId50 = t->m_docId;
			// that's it
			continue;
		}

		// get the docid ptr
		//char      *diptr = t->m_docIdPtr;
		//int64_t  docId = getDocIdFromPtr(diptr);
		// sanity check
		if ( t->m_docId < 0 ) { char *xx=NULL; *xx=0; }
		//add it to the reply
		topDocIds         [docCount] = t->m_docId;
		topScores         [docCount] = t->m_score;
		if ( m_tt.m_useIntScores ) 
			topScores[docCount] = (double)t->m_intScore;
		// supply clusterdb rec? only for full splits
		if ( m_gotClusterRecs ) 
			topRecs [docCount] = t->m_clusterRec;
		//topExplicits      [docCount] = 
		//	getNumBitsOn(t->m_explicits)
		docCount++;

		// 50th score? set this for seo.cpp. if less than 50 results
		// we want the score of the last doc then.
		if ( docCount <= 50 ) m_topScore50 = t->m_score;
		
		if ( m_debug ) {
			logf(LOG_DEBUG,"query: msg39: [%"PTRFMT"] "
			    "%03"INT32") docId=%012"UINT64" sum=%.02f",
			    (PTRTYPE)this, docCount,
			    t->m_docId,t->m_score);
		}
		//don't send more than the docs that are wanted
		if ( docCount >= numDocIds ) break;
	}
 	if ( docCount > 300 && m_debug )
		log("query: Had %"INT32" nodes in top tree",docCount);

	// this is sensitive info
	if ( m_debug ) {
		log(LOG_DEBUG,
		    "query: msg39: [%"PTRFMT"] "
		    "Intersected lists took %"INT64" (%"INT64") "
		    "ms "
		    "docIdsToGet=%"INT32" docIdsGot=%"INT32" "
		    "q=%s",
		    (PTRTYPE)this                        ,
		    m_posdbTable.m_addListsTime       ,
		    gettimeofdayInMilliseconds() - m_startTime ,
		    m_r->m_docsToGet                       ,
		    numDocIds                         ,
		    m_tmpq.getQuery()                 );
	}


	// if we blocked because we used a thread then call callback if
	// summoned from a msg3f handler and not a msg39 handler
	if ( m_callback ) {
		// if we blocked call user callback
		if ( m_blocked ) m_callback ( m_state );
		// if not sending back a udp reply, return now
		return;
	}

	// now send back the reply
	sendReply(m_slot,this,reply,replySize,replySize,false);
	return;
}
