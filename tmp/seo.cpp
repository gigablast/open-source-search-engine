
#include "seo.h"
// this should be obsoleted with the new QueryTerm::m_ptrPosdbList logic
//#include "Msg0.h" // for g_termListCache
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Speller.h"
#include "Synonyms.h"
#include "Process.h"
#include "sort.h"

#define TMPLISTDATASIZE 50000

// rank your changes up to the top 50 results per query
#define TOP_RESULTS_TO_SCAN 50

char *g_secret_tran_key = "5hGhr2gW2665knNg";
char *g_secret_api_key  = "89C84Yv3ftr";

//HashTableX g_qt;
SafeBuf    g_qbuf;
long       g_qbufNeedSave = false;

//long long getSynBaseHash64 ( char *qstr , uint8_t langId ) ;

/////////////////
//
// STATE3f / MSG3F
//
//
// State3f is now being used for setting QueryLogEntry::m_minTop50Score as
// well as getting the term pairs for the queries that match a given docid.
//
//
/////////////////

#define MAX_MSG39S_OUT 10

#include "Msg39.h"

// for the query loG loop
// we scan the queries in g_qbuf that have
// their m_minTop50Score set to -1 (which means invalid) and do
// the query on that to try to set it to an estimate, for which
// we then set QueryLogEntry::m_flag to QEF_ESTIMATE_SCORE. so this
// is used by runSEOQueryLoop2(). m_replyBuf should not be used.
class State3g {
public:
	char *m_coll;
	long m_errno;
	uint8_t m_langId3g;
	char *m_p;
	char *m_pend;
	char *m_nextList;
	long  m_qpcount;
	long m_numProcessed;
	long m_numOutMsg39s;
	long m_niceness;
	bool m_initialized;
	long m_interval;

	// call this when done if not null with THIS as "state" assuming
	// calling processPartialQueries() blocked!
	void (* m_doneCallback) (void *state);
	
	Msg39Request m_requests[MAX_MSG39S_OUT];
	Msg39        m_msg39s  [MAX_MSG39S_OUT];
	char         m_tmpBufs [MAX_MSG39S_OUT][MAX_QUERY_TERMS*8];

	// returns false if blocked true otherwise
	bool processPartialQueries();

	void processMsg39Reply ( class Msg39 *m39 ) ;

	State3g () {
		m_numProcessed       = 0;
		m_numOutMsg39s       = 0;
		m_errno              = 0;
		m_niceness           = MAX_NICENESS;
		m_doneCallback       = NULL;
		m_initialized        = false;
	};

};


class PosdbSlotData {
public:
	char *m_termList;
	long  m_termListSize;
};

// table maps a termid to a posdb termlist
bool setPosdbListTable ( HashTableX *plistTable ,
			 SafeBuf *listBuf,
			 char *posdbTermList,
			 long  posdbTermListSize,
			 long long docId ,
			 long niceness ) {

	long size = sizeof(PosdbSlotData);
	if ( ! plistTable->set ( 8,size,16384,NULL,0,false,niceness,"pltbl") )
		return false;

	// from handleRequest99()
	register char *p    = posdbTermList;
	register char *pend = posdbTermListSize + p;
	// get docid from first posdb key. they should all be the same docid.
	long long docId2 = 0LL;
	if ( p+4+18 <= pend ) {
		docId2 = g_posdb.getDocId(p+4);
		// sanity check
		if ( docId2 != docId ) { char *xx=NULL;*xx=0; }
	}
	// rewrite termlists into here with smaller keys (no termids)
	if ( ! listBuf->reserve ( posdbTermListSize ) )
		return false;
	// store 6-byte keys into here
	char *dst = listBuf->getBufStart();
	// scan posdb lists in the termlistbuf
	for ( ; p < pend ; ) {
		// breathe
		QUICKPOLL(niceness);
		// get next list's size
		long size = *(long *)p;
		// skip that
		p += 4;
		// grab that termlist
		char *tlist = p;
		// skip it
		p += size;
		// but cache it
		long long termId = g_posdb.getTermId(tlist);
		// show for debug
		//log("seo: tid48=%llu",termId);
		//if ( termId == 82741098886566LL )
		//	log("seo: got view");
		// each key in the ptr_posdbTermList is a full 18 bytes
		// so reduce to 6 bytes
		char *saved = dst;
		// point to the full keyed list
		char *x = tlist;
		bool firstKey = true;
		// should be 18 bytes per key
		for ( ; x < p ; x += sizeof(POSDBKEY) ) {
			// store it as 6 bytes, the lower 6 bytes
			if ( firstKey ) {
				// do not repeat
				firstKey = false;
				memcpy ( dst , x , 12 );
				// indicate a 12-byte key
				dst[0] |= 0x02;
				dst += 12;
			}
			else {
				memcpy ( dst , x , 6 );
				// top compression bit to indicate a 6 byte key
				dst[0] |= 0x04;
				// advance
				dst += 6;
			}
		}
		// make the data key
		PosdbSlotData psd;
		// . we moved the first key's top 6 bytes out of the picture
		// . it should have just been the termid really and we do not
		//   use that because we know the termid!
		psd.m_termList     = saved;
		psd.m_termListSize = dst - saved;
		// add to table
		if ( ! plistTable->addKey ( &termId , &psd ) )
			return false;
	}

	return true;
}

// . set QueryTerm::m_posdbListPtr
// . used for restricting query to a single docid
// . used by scoring matching queries and for scoring insertable terms
// . by handleRequest99() 
void setQueryTermTermList( Query *q , 
			   HashTableX *plistTable ,
			   RdbList *rdbLists ) {
	// loop over every query term
	for ( long i = 0 ; i < q->m_numTerms ; i++ ) {
		// shortcut
		QueryTerm *qt = &q->m_qterms[i];
		// assume none
		qt->m_posdbListPtr = NULL;
		// we should have already made the individual posdb lists
		PosdbSlotData *psd;
		psd =(PosdbSlotData *)plistTable->getValue ( &qt->m_termId );
		// skip if none
		if ( ! psd ) continue;
		// sanity!
		if ( g_posdb.getKeySize(psd->m_termList) != 12 ) {
			char *xx=NULL;*xx=0; }
		// make an RdbList for Posdb.cpp to use
		rdbLists[i].m_list     = psd->m_termList;
		rdbLists[i].m_listSize = psd->m_termListSize;
		rdbLists[i].m_listEnd  = psd->m_termList + psd->m_termListSize;
		// this might be NULL if does not exist
		qt->m_posdbListPtr = &rdbLists[i];
	}
}


class State95 {
public:

	void handleRequest ( ) ;
	bool setTermFreqTable ( ) ;
	bool setQueryMatchBuf ( );
	bool setWordInfoBuf ();
	void sendErrorReply95 ( ) ;
	bool addQueryChangesForQuery ( class QueryMatch *qm );
	bool setPosdbLists ( class WordPosInfo *);//long insertPosArg );
	void printTermLists ( bool orig ) ;
	//void printTermList ( long i,char *list, long listSize ) ;
	//void logScoreInfo ( SafeBuf *scoreInfoBuf , 
	//		    Msg39Request *mr ,
	//		    float newScore ) ;

	// this is deserialized
	Msg95Request *m_req95;

	// array of QueryMatch instances
	SafeBuf m_queryMatchBuf;

	// maps a termid to its posdblist for this docid
	HashTableX m_plistTable;

	// array of QueryChange instances
	SafeBuf m_queryChangeBuf;

	// for the msg95reply we need the buffer of queries that are
	// referenced by QueryChange::m_queryOffset. we now store
	// QueryLogEntries instead of just the plain strings.
	SafeBuf m_queryLogBuf;

	// re-write Msg95Request::ptr_termListBuf termlists into here
	// without the termid part of the posdbkey. 
	SafeBuf m_listBuf;

	// holds the re-written posdb lists with the term inserted
	//SafeBuf m_tmpBuf;

	// maps a 32-bit wordid to a 64-bit termfreq from group #0
	HashTableX m_termFreqTable;

	// map each insertable "term" to a list of wordInfos, which
	// dictate how many words are in the "term" and the # of words
	// in the term.
	SafeBuf m_wordInfoBuf;

	// we need this for doing a full query on the queries that we are
	// potentially in the top 50 results for to get our exact rank
	// change
	Msg3a m_msg3a;
	bool performFullQueries ( ) ;
	bool gotDocIdsForFullQuery ( ) ;
	char *m_p;
	char *m_pend;
	char *m_coll;
	QueryLogEntry *m_qe;
	long m_errno;
	Msg39Request m_r;
	Query m_query;

	// hold the DocIdScore for the unaltered doc here
	SafeBuf m_origScoreInfoBuf;

	// and the DocIdScore instances, 1-1 with QueryChanges in
	// m_queryChangeBuf. describes how we arrived at the score,
	// QueryChange::m_newScore
	SafeBuf m_debugScoreInfoBuf;

	UdpSlot *m_udpSlot;
	long m_niceness;

	// for performance reasons
	long long *m_wordIds        ;
	long       m_numWordsInTerm ;
	Query     *m_q;

	//uint8_t m_docLang95;

	long long m_numDocsInColl;

	char m_siteRank;

	long m_origOffset;

	// the original source lists
	RdbList *m_rdbLists;
	// the new lists set by setPosdbLists
	RdbList m_newRdbLists[MAX_QUERY_TERMS];

	char m_tmpListData[TMPLISTDATASIZE];
};


void logScoreInfo ( SafeBuf *scoreInfoBuf , 
		    Msg39Request *mr ,
		    float newScore ,
		    Query *q ) {

	if ( scoreInfoBuf->length() <= 0 ) {
		log("seo: scoreInfoBuf is %li. not logging.",
		    scoreInfoBuf->length() );
		return;
	}

	DocIdScore *dp = (DocIdScore *)scoreInfoBuf->getBufStart();

	// if pair changes then display the sum
        long lastTermNum1 = -1;
        long lastTermNum2 = -1;

        float minScore = -1;

	// from PageResults.cpp
	// display all the PairScores
	for ( long i = 0 ; i < dp->m_numPairs ; i++ ) {
		float totalPairScore = 0.0;
		// print all the top winners for this pair
		PairScore *fps = &dp->m_pairScores[i];
		// if same combo as last time skip
		if ( fps->m_qtermNum1 == lastTermNum1 &&
		     fps->m_qtermNum2 == lastTermNum2 )
			continue;
		lastTermNum1 = fps->m_qtermNum1;
		lastTermNum2 = fps->m_qtermNum2;
		bool firstTime = true;
		bool first = true;
		// print all pairs for this combo
		for ( long j = i ; j < dp->m_numPairs ; j++ ) {
			// get it
			PairScore *ps = &dp->m_pairScores[j];
			// stop if different pair now
			if ( ps->m_qtermNum1 != fps->m_qtermNum1 ) break;
			if ( ps->m_qtermNum2 != fps->m_qtermNum2 ) break;
			// skip if 0. neighborhood terms have weight of 0 now
			if ( ps->m_finalScore == 0.0 ) continue;
			// first time?
			if ( firstTime ) {
				//printTermPairs ( sb , si , ps );
				long qtn1 = ps->m_qtermNum1;
				long qtn2 = ps->m_qtermNum2;
				SafeBuf sb;
				if ( q->m_qterms[qtn1].m_isPhrase )
					sb.pushChar('\"');
				sb.safeMemcpy(q->m_qterms[qtn1].m_term,
					       q->m_qterms[qtn1].m_termLen);
				if ( q->m_qterms[qtn1].m_isPhrase )
					sb.pushChar('\"');
				sb.safePrintf(" vs ");
				if ( q->m_qterms[qtn2].m_isPhrase )
					sb.pushChar('\"');
				sb.safeMemcpy(q->m_qterms[qtn2].m_term,
					       q->m_qterms[qtn2].m_termLen);
				if ( q->m_qterms[qtn2].m_isPhrase )
					sb.pushChar('\"');
				sb.pushChar('\0');
				log ( "seo: %s",sb.getBufStart());
				//printScoresHeader ( sb );
				firstTime = false;
			}
			// print it
			//printPairScore ( sb , si , ps , mr , msg40 , first );
			log ("seo: qtn1 "
			     "pos=%li "
			     "dr=%li "
			     "hg=%s "
			     "spm=%li "
			     "tf=%lli "
			     "tfw=%f "
			     , ps->m_wordPos1
			     , (long)ps->m_densityRank1
			     , getHashGroupString(ps->m_hashGroup1)
			     , (long)ps->m_wordSpamRank1
			     , ps->m_termFreq1
			     , ps->m_tfWeight1
			     );
			log ("seo: qtn2 "
			     "pos=%li "
			     "dr=%li "
			     "hg=%s "
			     "spm=%li "
			     "tf=%lli "
			     "tfw=%f "
			     , ps->m_wordPos2
			     , (long)ps->m_densityRank2
			     , getHashGroupString(ps->m_hashGroup2)
			     , (long)ps->m_wordSpamRank2
			     , ps->m_termFreq2
			     , ps->m_tfWeight2
			     );
			log("seo: FINALPAIRSCORE=%.010f",ps->m_finalScore);
			// not first any more!
			first = false;
			// add it up
			totalPairScore += ps->m_finalScore;
		}
		//if ( ft.length() ) ft.safePrintf(" , ");
		//ft.safePrintf("%f",totalPairScore);
		// min?
		if ( minScore < 0.0 || totalPairScore < minScore )
			minScore = totalPairScore;
		// log sum of above pair scores
		log("seo: TOTALPAIRSCORESSUM=%.010f",totalPairScore);
		// do the siterank crap
		float srw = (((float)dp->m_siteRank)*
			     SITERANKMULTIPLIER+1.0);
		float s2 = totalPairScore * 
			(((float)dp->m_siteRank)*SITERANKMULTIPLIER+1.0);
		// site rank mod
		log("seo: %f * %f (siterank=%li) = %f "
		    , totalPairScore
		    , srw
		    , (long)dp->m_siteRank
		    , s2 );
		// lang mod
		float s3 = s2;
		if ( mr->m_language == 0 ||
		     dp->m_docLang == 0 ||
		     mr->m_language == dp->m_docLang ) {
			s3 = s2 * SAMELANGMULT;
			log("seo: %f * %f (samelang) = %f "
			    , s2
			    , (float)SAMELANGMULT
			    , s3 );
		}
		// formulate must match what we got!!
		// . not quite, we sum up the pair scores in Posdb.cpp::
		//   getTermPairScoreForAny() and then multiple by the 
		//   "wts" and m_termFreqWeights, but here we store the
		//   PairScore::m_finalScore with those weights inserted
		//   and THEN we do the sum... so it creates a little error.
		if ( s3 != newScore ) { 
			log("seo: discrepancy=%f",
			    s3-newScore);
			//char *xx=NULL;*xx=0; }
		}
	}
}


static void processMsg39ReplyWrapper ( void *state ) {
	Msg39 *m39 = (Msg39 *)state;
	State3g *st = (State3g *)m39->m_tmp;
	st->processMsg39Reply( m39 );
	// . try start more msg39s or cleanup if all done and send back reply
	// . returns false if blocked
	if ( ! st->processPartialQueries() ) return;
	// all done, call the done callback then
	st->m_doneCallback ( st );
}


class State4f {
public:
	UdpSlot *m_udpSlot;
	long m_niceness;
	XmlDoc m_xd;
	SafeBuf m_replyBuf;
	char *m_coll;
	long m_queryBufSize;
	char *m_queryBuf;
	char *m_docIdPtr;
	char *m_docIdEnd;
	char  m_setFlag;
	char  m_loadedFlag;

};

static void process4fRequest ( void *state ) ;

// . similar to handleRequest3f but we have to look up the docid locally
//   and get the termlistbuf ourselves 
// . xmldoc launches one msg4f for each host in our network. it contains
//   a list of local docids (related docid candidates) and a list of queries
//   to score each local docid for.
void handleRequest4f ( UdpSlot *udpSlot , long netnice ) {

	// make state
	State4f *st = NULL;
	try { st = new (State4f); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("seo: new(%i): %s", 
		    sizeof(State4f),mstrerror(g_errno));
		g_udpServer.sendErrorReply(udpSlot,500,g_errno);
	}
	mnew ( st, sizeof(State4f) , "st4f");


	st->m_udpSlot = udpSlot;
	st->m_niceness = netnice;

	// first is coll
	char *p = st->m_udpSlot->m_readBuf;
	// set end
	char *pend = p + udpSlot->m_readBufSize;
	// first is coll
	st->m_coll = p; 
	p += gbstrlen(p)+1;
	// then list of queries size
	st->m_queryBufSize = *(long *)p; 
	p += 4;
	// query buf
	st->m_queryBuf = p;
	p += st->m_queryBufSize;
	// docid buf/list
	st->m_docIdPtr = p;
	st->m_docIdEnd = pend;
	st->m_setFlag = 0;
	st->m_loadedFlag = 0;

	// sanity
	char *coll = st->m_coll;
	if ( ! coll ) {
		g_udpServer.sendErrorReply(st->m_udpSlot,ENOCOLLREC);
		mdelete ( st , sizeof(State4f) , "st4f" );
		delete (st);
	}

	// this returns the reply, error or otherwise
	process4fRequest ( st );
}


void process4fRequest ( void *state ) {

	State4f *st = (State4f *)state;
	XmlDoc *xd = &st->m_xd;
	SafeBuf *replyBuf = &st->m_replyBuf;
	long niceness = st->m_niceness;
	char *coll = st->m_coll;

	// the list of docids
	for ( ; st->m_docIdPtr < st->m_docIdEnd ; ) {
		// get current docid
		long long docId = *(long long *)st->m_docIdPtr;
		// something somehow gets corrupted?
		if ( (docId & DOCID_MASK) != docId ) {
			log("seo: 4f handler got docid corruption!");
			g_errno = ECORRUPTDATA;
			break;
		}
		// set it?
		if ( ! st->m_setFlag ) {
			xd->set3 ( docId,coll,niceness );
			// ensure content is recycled from title rec
			xd->m_recycleContent = true;
			//xd->m_recycleLinkInfo = true;
			// only get posdb keys really for this stuff
			xd->m_useTitledb   = false;
			xd->m_useTagdb     = false;
			xd->m_useClusterdb = false;
			xd->m_useSpiderdb  = false;
			xd->m_useLinkdb    = false;
			// xd needs to use our master functions. so anytime 
			// one of its internal functions blocks, then our 
			// m_masterLoop will be called and we'll end up right 
			// here again!
			xd->m_masterLoop  = process4fRequest;//m_masterLoop;
			xd->m_masterState = st;//m_masterState;
			// do not re-enter yet
			st->m_setFlag = 1;
		}
		// try to set from title rec first
		if ( ! st->m_loadedFlag ) {
			if ( ! xd->loadFromOldTitleRec()) return;
		}
		// did that fail? i.e. docid not found!?!?!
		if ( xd->m_oldTitleRecValid && ! xd->m_oldTitleRec ) {
			// just skip this asshole then
			log("seo: 4f docid %lli load5 failed",docId);
			// clear that
			g_errno = 0;
			// skip it
			st->m_docIdPtr += 8;
			// allow another set
			st->m_setFlag = 0;
			// try the next one
			continue;
		}
		// do not re-load if getTermListBuf() blocks
		st->m_loadedFlag = 1;
		// . this blocks sometimes too i guess
		// . if it blocks it will call xd->m_masterLoop (us!)
		SafeBuf *tbuf = xd->getTermListBuf();
		// error?
		if ( ! tbuf ) {
			// just skip this asshole then
			log("seo: 4f docid %lli termlistbuf failed",docId);
			// must be set
			if ( ! g_errno ) { char *xx=NULL;*xx=0; }
			// stop and return error reply
			break;
		}
		// return if it blocked and wait
		if ( tbuf == (void *)-1 ) return;
		// advance over docid now
		st->m_docIdPtr += 8;
		// allow setting and loading of new docid
		st->m_setFlag    = 0;
		st->m_loadedFlag = 0;
		// sanity
		if ( ! xd->m_langIdValid ) { char *xx=NULL;*xx=0; }

		HashTableX plistTable;
		SafeBuf listBuf;
		// set m_plistTable to map a termId to our docids posdblist
		if ( ! setPosdbListTable ( &plistTable ,
					   &listBuf,
					   tbuf->getBufStart(),
					   tbuf->length(),
					   docId ,
					   niceness ) ) 
			// g_errno should be set
			break;
		// this ain't quite right!
		uint8_t queryLangId = xd->m_langId;
		// fix gigablast.com not scoring for 'web search engines' 
		// because it was not getting the synonym 'engine' and we do 
		// not have 'engines' on our page, only in our neighborhood 
		// text, which hashgroup has a weight of 0.
		if ( queryLangId == langUnknown ) queryLangId = langEnglish;
		// make a msg39 request for each one
		Msg39Request mr;
		mr.reset();
		mr.ptr_coll   = coll;
		mr.size_coll  = gbstrlen(coll)+1;
		mr.m_queryExpansion = 1;
		// this is the langid the query is in
		mr.m_language = queryLangId;
		mr.m_niceness = niceness;
		// turn these off for speed
		mr.m_getDocIdScoringInfo = false;
		mr.m_doSiteClustering    = false;
		mr.m_doDupContentRemoval = false;
		mr.m_fastIntersection  = 1;
		// just one result, our url!
		mr.m_docsToGet = 1;
		mr.m_getSectionStats = false;
		// we provide our own posdb termlists...
		mr.ptr_readSizes  = NULL;
		mr.size_readSizes = 0;
		//SafeBuf replyBuf;
		//long myErrno = 0;
		// turn debugging off for now
		bool mydebug = false;
		// debug
		if ( mydebug ) {
			mr.m_debug = 1;
			//mr.m_seoDebug = 1;//true;
			mr.m_getDocIdScoringInfo = true;
		}
		// we'll set this below
		Query q;
		// get score as it is now
		PosdbTable posdbTable;
		// initialize it
		posdbTable.init ( &q ,
				  false, // debug // isDebug?
				  NULL, // logState
				  NULL, // top tree
				  coll, // collection
				  NULL, // msg2
				  &mr ); // msg39Request (search parms)
		// also need this from PosdbTable::allocTopTree()
		if ( mydebug ) {
			PosdbTable *pt = &posdbTable;
			// reset buf ptr, but do not free mem
			// alloc mem if not already there
			pt->m_scoreInfoBuf.reserve ( 5*sizeof(DocIdScore) );
			// # of possible score pairs
			pt->m_pairScoreBuf.reserve ( 50*sizeof(PairScore) );
			// # of possible singles
			pt->m_singleScoreBuf.reserve ( 50*sizeof(SingleScore));
		}
		// scan querybuf
		char *p = st->m_queryBuf;
		char *pend = st->m_queryBufSize + p;

		
		for ( ; p < pend ; ) {
		// breathe
		QUICKPOLL(niceness);
		// first is the query #, XmlDoc::m_qptrs[i]
		long qcursor = *(long *)p;
		// skip over that query #
		p += 4;
		// then the query
		char *qstr = p;
		long qlen = gbstrlen(qstr);
		// advance our cursor
		p += qlen + 1;
		mr.ptr_query  = qstr;
		mr.size_query = qlen+1;
		// TODO: should we also store the query language id???
		mr.m_language = langEnglish;
		// . stuff below here taken from Msg3a.cpp
		// . we need to set termfreq weights. true = synonymExpansion?
		// . TODO: what are these termfreqs???
		// . TODO: need to send over termfreq map as well in 3f request
		q.set2 ( qstr , queryLangId , true );
		long numTerms = q.getNumTerms();
		RdbList rdbLists[MAX_QUERY_TERMS];
		// point to the termlists relevant to the query that were 
		// provided in the msg3f request as m_termListBuf from 
		// XmlDoc::getTermListBuf().
		// set QueryTerm::m_posdbListPtr i guess and use that in
		// intersectLists10_r()
		setQueryTermTermList ( &q , &plistTable , rdbLists );
		mr.m_nqt = numTerms;
		// need termFreqWeights
		float termFreqWeights [MAX_QUERY_TERMS*8];
		setTermFreqWeights ( coll,&q,NULL,termFreqWeights);
		mr.ptr_termFreqWeights = (char *)termFreqWeights;
		mr.size_termFreqWeights = sizeof(float) * numTerms;
		// set posdb termlist for each query term
		for ( long i = 0; i < numTerms ; i++ ) {
			// shortcut
			QueryTerm *qt = &q.m_qterms[i];
			// if we are scoring queries for a single docid,
			// then assign the list ptrs here
			qt->m_posdbListPtr = &rdbLists[i];
		}
		QUICKPOLL(niceness);
		// . needs this before calling intersect
		// . was called in PosdbTable::allocTopTree but we do not 
		// need that
		if ( ! posdbTable.setQueryTermInfo() ) 
			break;
		QUICKPOLL(niceness);
		if ( mydebug ) {
			posdbTable.m_scoreInfoBuf.reset();
			posdbTable.m_pairScoreBuf.reset();
			posdbTable.m_singleScoreBuf.reset();
		}
		// hack set
		posdbTable.m_docIdHack = docId;
		// get the score for our url on this query
		posdbTable.intersectLists10_r();
		// get the final score
		float finalScore = posdbTable.m_finalScore;
		// reserve reply space
		if ( replyBuf->length() == 0 ) {
			// . over alloc but try to avoid a realloc
			// . is this enough?
			if ( ! replyBuf->reserve ( 1000 *(4+8+4) ) )
				break;
			// ignore error if that failed
			g_errno = 0;
		}
		// store query #
		if ( ! replyBuf->pushLong  ( qcursor ) ) break;
		if ( ! replyBuf->pushLongLong(docId) ) break;
		// and the score our docid had for that query
		if ( ! replyBuf->pushFloat ( finalScore ) ) break;
		if ( ! mydebug ) continue;
		log("seo: returning qstr=%s docid=%lli score=%f",
		    qstr,docId,finalScore);
		// try to print winning pair
		logScoreInfo ( &posdbTable.m_scoreInfoBuf,
			       &mr ,
			       finalScore,
			       &q);

		} // query loop

	} // docid loop

	// if g_errno is set this will send back an error reply
	// save it
	long err = g_errno;
	// if not handling a network request do not build a reply
	char *reply     = replyBuf->getBufStart();
	long  replySize = replyBuf->length();
	long  allocSize = replyBuf->getCapacity();
	char *alloc     = reply;
	// save from state before deleting it
	//long     err  = myErrno;
	UdpServer *us = &g_udpServer;
	// now send back the reply
	if ( err )
		us->sendErrorReply(st->m_udpSlot,err);
	else {
		// unhitch it so we do not free it when done handling
		replyBuf->detachBuf();
		// the udpserver will free it when done transmitting
		us->sendReply_ass( reply ,
				   replySize ,
				   alloc,
				   allocSize ,
				   st->m_udpSlot );
	}

	mdelete ( st , sizeof(State4f) , "st4f" );
	delete (st);
}

					
#include "Msg3a.h" // for DEFAULT_POSDB_READSIZE #define


// . parse request at m_p which is just a sequence of queries in utf8
// . used to set QueryLogEntry::m_topSERPScore and m_minTop50Score for
//   the query loop process that runs in the background from runSEOQueryLoop().
//   in this case we restrict the search results to just our local split and
//   often limit the split even further by making the maxdocid in the 
//   msg39Request 10% of the normal max so our loop completes in about a day
bool State3g::processPartialQueries ( ) {

	//log("qloop: in processPartialQueries");

	// scanning queries in g_qbuf to set 
	// QueryLogEntry::m_minTop50Score?
	if ( ! m_initialized ) {
		m_initialized = true;
		m_p    = (char *)g_qbuf.getBufStart();
		m_pend = (char *)g_qbuf.getBuf();
		m_nextList = m_p;
		m_qpcount = 0;
		// limit to 3 msg39s outstanding since we will be
		// hitting disk!
		m_interval = 10000;
		if ( g_titledb.getGlobalNumDocs() > 10000000 ) 
			m_interval = 100;
	}


msg39Loop:

	QUICKPOLL(m_niceness);

	// if no more to send, done
	if ( m_p >= m_pend && m_numOutMsg39s == 0 ) 
		// all done!
		return true;

	// if no more to send, wait
	if ( m_p >= m_pend ) return false;
	// wait for more msg39s to get back
	if ( m_numOutMsg39s >= MAX_MSG39S_OUT ) return false;


	long qcursor;
	char *qstr;
	long qlen;

	// default to provided langid, if any
	uint8_t thisLangId = m_langId3g;

	//long mcount = 0;

	// . if setting QueryLogEntry::m_minTop50Score
	// . limits termlist sizes to like 10% of maxdocid and only performs
	//   query on our split of the index. so it's a split of a split for
	//   speed

	// get the size
	if ( m_p == m_nextList ) {
		// how many query log entries are in this list
		long listSize = *(long *)m_p;
		// note it
		//log("qloop: hit listsize=%li",listSize);
		// skip that to point to the first QueryLogEntry in lst
		m_p += 4;
		// point to next list
		m_nextList = m_p + listSize;
	}
	// get it
	QueryLogEntry *qe = (QueryLogEntry *)m_p;
	
	// sanity
	if ( qe->getSize() <= 0 ) { char *xx=NULL;*xx=0; }
	//log("qloop: processed #%li '%s' size=%li "
	//    ,mcount++,qe->getQueryStr(),qe->getSize());
	
	
	// advance it
	m_p += qe->getSize();
	
	// debug point
	// set m_p = m_pend
	// here to stop the queryloop
	//if ( m_pend - m_p >= 127083800 ) {
	//	log("qloop: STOPPING EARLY...");
	//	m_p = m_pend;
	//}
	
	// skip if top50score not -1. that means we already
	// evaluated it.
	if ( qe->m_minTop50Score != -1.0 ) goto msg39Loop;
	// set query string
	qstr = qe->getQueryStr();
	qlen = gbstrlen(qstr);
	// the language for synonyms
	thisLangId = qe->getQueryLangId();
	// debug hack
	//qstr = "gigablast";
	//qlen = gbstrlen(qstr);
	// dummy?
	qcursor = (long)qe;

	// find next abailable msg39
	long ii = 0;
	for ( ; ii < MAX_MSG39S_OUT ; ii++ ) 
		if ( ! m_msg39s[ii].m_inUse ) break;
	// none avail? wtf?
	if ( ii >= MAX_MSG39S_OUT ) {char *xx=NULL;*xx=0; }

	// make a msg39 request for each one
	Msg39Request *mr = &m_requests[ii];
	mr->reset();
	mr->ptr_query  = qstr;
	mr->size_query = qlen+1;
	mr->ptr_coll   = m_coll;
	mr->size_coll  = gbstrlen(m_coll)+1;
	mr->m_queryExpansion = 1;
	// this is the langid the query is in
	mr->m_language = thisLangId;
	mr->m_niceness = m_niceness;
	// turn these off for speed
	mr->m_getDocIdScoringInfo = false;
	mr->m_doSiteClustering    = false;
	mr->m_doDupContentRemoval = false;
	mr->m_fastIntersection  = 1;
	// just one result, our url!
	mr->m_docsToGet = 1;
	// get more docs if getting min top 50 score
	mr->m_docsToGet = TOP_RESULTS_TO_SCAN; // 50;
	mr->m_minDocId  =  0;
	// hmmm on our tiny index this excludes all our docids!
	// try no limitation for now
	//mr->m_maxDocId  = (long long)(.1 * MAX_DOCID);
	//mr->m_maxDocId = MAX_DOCID;
	// debug hack
	//mr->m_debug = 1;
	// only do these restrictions on large indexes
	if ( g_titledb.getGlobalNumDocs() > 10000000 ) {
		// only read from the base posdb file, posdb0001.dat*
		mr->m_restrictPosdbForQuery = true;
		// limit to like 10% of our possible docids to
		// make everything fast!
		mr->m_maxDocId  = (long long)(.1 * MAX_DOCID);
	}

	log("qloop: processing query %s",qstr);
	// turn it on for debug
	//mr->m_debug             = 1;

	//mr->m_getDocIdScoringInfo = true;
	// . stuff below here taken from Msg3a.cpp
	// . we need to set termfreq weights. true = synonymExpansion?
	char *tmpBuf = m_tmpBufs[ii];
	char *op = tmpBuf;
	float *termFreqWeights = (float *)op;
	op += sizeof(float) * MAX_QUERY_TERMS;
	Query q; 
	q.set2 ( qstr , thisLangId , true );
	long numTerms = q.getNumTerms();

	//if (numTerms != nterms ) { char *xx=NULL;*xx=0; }
	mr->m_nqt = numTerms;
	setTermFreqWeights ( m_coll,&q,NULL,termFreqWeights);
	// pass them in as input
	mr->ptr_termFreqWeights = (char *)termFreqWeights;
	mr->size_termFreqWeights = sizeof(float) * numTerms;
	// and set list read sizes (taken from Msg3a.cpp)
	long *readSizes = (long *)op;
	op += sizeof(long) * MAX_QUERY_TERMS;
	for ( long j = 0; j < numTerms ; j++ ) {
		// this is now 90MB as of 3/16/2013
		long rs = DEFAULT_POSDB_READSIZE;//1000000;
		// the read size for THIS query term. 1MB if we are
		// doing a gbdocid:xxxx| query
		//if ( strncmp(qstr,"gbdocid:",8) == 0 ) rs = 1000000;
		if ( q.m_docIdRestriction ) rs = 1000000;
		// get the jth query term
		QueryTerm *qt = &q.m_qterms[j];
		// if query term is ignored, skip it
		if ( qt->m_ignored ) rs = 0;
		// set it
		readSizes[j] = rs;
	}
	mr->ptr_readSizes  = (char *)readSizes;
	mr->size_readSizes = 4 * numTerms;

	QUICKPOLL(m_niceness);

	// get the ith msg39 available
	Msg39 *m39 = &m_msg39s[ii];
	// reset all. have to code this anew since we re-use the msg39,
	// a new concept.
	//m39->reset();
	// hack
	m39->m_tmp      = this; // State3g
	m39->m_tmp2     = qcursor;
	m39->m_blocked  = false;
	m39->m_callback = processMsg39ReplyWrapper;
	m39->m_state    = m39;
	// count as outstanding
	m_numOutMsg39s++;
	// count this
	m_numProcessed++;
	// get results
	m39->getDocIds2 ( mr );

	QUICKPOLL(m_niceness);

	// if did not block, call reply handler ourselves
	if ( ! m39->m_blocked ) processMsg39Reply ( m39 );
	// it must be in use then and blocking
	else if ( ! m39->m_inUse ) { char *xx=NULL;*xx=0; }

	// do more
	goto msg39Loop;

	// fake to make compiler happy
	return false;
}

static int floatcmp ( const void *a, const void *b ) {
	float *fa = (float *)a;
	float *fb = (float *)b;
	//  high score first!
	if ( *fa < *fb ) return 1;
	if ( *fa > *fb ) return -1;
	return 0;
}


void State3g::processMsg39Reply ( Msg39 *m39 ) {
	// discount how many are outstanding
	m_numOutMsg39s--;
	// get count
	long qcursor = m39->m_tmp2;

	// if executing each query in the query log...
	// get the query log entry we processed
	QueryLogEntry *qe = (QueryLogEntry *)qcursor;
	// log it
	//char *qstr = qe->getQueryStr();
	//log("qloop: done processing '%s'", qstr );
	// how many hits did the query have total?
	qe->m_numTotalResultsInSlice = 
		m39->m_posdbTable.m_docIdVoteBuf.length() / 6;
	// . get the 50th score
	// . if less than 50 results use the score of the last result
	// . 0 means no results!
	// . this top score is really the best score in a slice JUST on
	//   this host! so it's like the score of the 128 * 10 = 1280th
	//   result on average on this 128-node cluster, actually it was
	//   640th cuz we generated these on the 64-node cluster using gk208
	//   as host #0 and copied to gk144's 64 nodes...!!! HACK HACK.
	//   to do this right we'd just do the full query and record the
	//   top 300 docids/scores/sitehash26's
	qe->m_minTop50Score = m39->m_topScore50;
	qe->m_topSERPScore = m39->m_topScore;
	//if ( m39->m_topScore50 != 0.0 )
	//	log("hey");
	//else
	//	log("foo");
	// debug hack
	//log("qloop: %s -> %f %lli",
	//    qe->getQueryStr(),
	//    qe->m_minTop50Score,
	//    m39->m_topDocId50);
	long now = getTimeGlobal();
	qe->m_minTop50ScoreDate = now;
	qe->m_flags = QEF_ESTIMATE_SCORE;
	
	// log it
	if ( (m_qpcount % m_interval) == 0 ) {
		float percent = (float)(m_p - g_qbuf.getBufStart());
		percent /= g_qbuf.length();
		percent *= 100.0;
		log("qloop: processing #%li %.01f%% done '%s' "
		    "gbtraffic=%li totalresults=%li "
		    "topScore=%f "
		    "top50Score=%f "
		    "lang=%s", 
		    m_qpcount,
		    percent,
		    qe->getQueryStr(),
		    qe->m_gigablastTraffic,
		    qe->m_numTotalResultsInSlice,
		    qe->m_topSERPScore,
		    qe->m_minTop50Score,
		    getLangAbbr(qe->m_langId));
	}
	m_qpcount++;
	
	// mark g_qbuf as needing save to disk. Process.cpp should
	// save it i guess if it needs it.
	g_qbufNeedSave = true;
	// save mem
	m39->reset();
	// all done
	return;
}

// values for QueryLogEntry::m_flags
#define QEF_ESTIMATE_SCORE 0x01 // indicates m_minTop50Score is an estimate



static bool loadQueryLogPartFile ( long part , HashTableX *dedup ) ;

int qlogcmp ( const void *a, const void *b ) {
	// put min qids in front
	unsigned long qa = *(unsigned long *)a;
	unsigned long qb = *(unsigned long *)b;
	// but stopwords always at back since handleRequest99() skips over
	// stop words in the twid vector
	bool sa = isStopWord32(qa);
	bool sb = isStopWord32(qb);
	if ( sa && ! sb ) return  1; // swap b into front of a
	if ( sb && ! sa ) return -1;
	if ( qa > qb ) return  1;
	if ( qa < qb ) return -1;
	return 0;
}

// ptrs to query entries
int buildcmp ( const void *a, const void *b ) {
	// point to first qid. format = qn+pop+qids+qstr
	//unsigned long *qa = (unsigned long *)( (*(char **)a) +5);
	//unsigned long *qb = (unsigned long *)( (*(char **)b) +5);
	QueryLogEntry *ea = *(QueryLogEntry **)a;
	QueryLogEntry *eb = *(QueryLogEntry **)b;
	unsigned long *qa = ea->m_queryTermIds32;
	unsigned long *qb = eb->m_queryTermIds32;
	// lower first
	if ( *qa > *qb ) return  1;
	if ( *qa < *qb ) return -1;
	return 0;
}

static long s_queryCount = 0;

// . to be performed upon initialization
// . just loads a split of the query log
// . for 128 hosts, we are responsible for every 128th word, starting
//   at word #<hostId>
// . afterprocessing querylog.txt we store querylog.dat for quicker init
// . we hash each word in each query and map that word hash to a ptr to
//   that query. the entry should have the following format:
//
//   1 byte num words | 4 bytes query pop | list of wordIds | queryString
//
bool loadQueryLog ( ) {


	// use the base hostid as group
	Host *myGroup = g_hostdb.getMyGroup ();
	Host *h0 = &myGroup[0];
	char myFile[1024];
	sprintf(myFile,"querylog.host%li.dat",h0->m_hostId);

	// just load the precompiled hashtable, along with buffer, from disk
	File f;
	f.set ( g_hostdb.m_dir , myFile );
	if ( f.doesExist() ) {
		//g_qt.m_allocName = "qlogtbl";
		//if ( ! g_qt.load(g_hostdb.m_dir,myFile,&g_qbuf) )
		//	// return false on error
		//	return log("xmldoc: %s load failed.",myFile);
		if ( g_qbuf.fillFromFile(g_hostdb.m_dir,myFile)<0 )
			return log("qlog: %s load failed.",myFile);
		// now fix 'xxi 21' query to have topSERPScore of 4.8M
		// because it is coming up as a matching query for a ton
		// of urls.
		char *p = g_qbuf.getBufStart();
		char *pend = g_qbuf.getBuf();
		long long start = gettimeofdayInMillisecondsLocal();
		// list size of query records that begin with the same minimum
		// wordid
		long listSize = *(long *)p;
		p += 4;
		char *nextList = p + listSize;
		for ( ; p < pend ; ) {
			QueryLogEntry *qe = (QueryLogEntry *)p;
			// skip the query log entry now
			p += qe->getSize();
			// done?
			if ( p >= pend ) break;
			// end of list?
			if ( p == nextList ) {
				listSize = *(long *)p;
				p += 4;
				nextList = p + listSize;
			}
			// is this the guy we need to fix?
			char *qstr = qe->getQueryString();
			if ( qstr[0] =='x' &&
			     qstr[1] =='x' &&
			     qstr[2] =='i' &&
			     qstr[3] ==' ' &&
			     ! strcmp(qstr,"xxi 21") ) {
				// so it won't come up for all pages that 
				// have "21"!
				log("qlog: fixing xxi 21");
				qe->m_topSERPScore = 48000000.0;
			}
			if ( (qstr[0] =='u' || qstr[1]=='a') &&
			     qstr[1] ==' ' &&
			     qstr[2] =='x' &&
			     qstr[3] =='x' &&
			     qstr[4] =='i' &&
			     qstr[5] == '\0' )  {
				log("qlog: fixing query %s",qstr);
				qe->m_topSERPScore = 48000000.0;
			}
			if ( qstr[0] =='x' &&
			     qstr[1] =='x' &&
			     qstr[2] =='i' &&
			     qstr[3] =='\0' ) {
				log("qlog: fixing xxi");
				qe->m_topSERPScore = 48000000.0;
			}
		}
		long long end = gettimeofdayInMillisecondsLocal();
		long long took = end - start;
		if ( took > 5 )
			log("qlog: took %lli ms to scan for bad queries",took);
		// all done!
		return true;
	}
	// note it
	log("xmlddoc: generating %s from querylog.txt",myFile);

	// initialize hashtable to 30M slots. key/data = 4 bytes each.
	// and allow dups = true!
	//if ( ! g_qt.set(4,4,30000000,NULL,0,true,0,"qtht") )
	//	return false;

	HashTableX dedup;
	if ( ! dedup.set ( 8 , 4 , 2000000 , NULL , 0 ,false,0,"qdedup") )
		return false;

	s_queryCount = 0;

	// . load querylog.txt.part1
	// . max is 50 million lines
	// . last i checked this was a full 50M lines
	// . we have to split up the file because >2GB is too big for the
	//   libc i use
	if ( ! loadQueryLogPartFile ( 1 , &dedup ) ) return false;
	// . load querylog.txt.part2
	// . max is 50 million lines
	// . last i checked it was about 32M lines
	if ( ! loadQueryLogPartFile ( 2 , &dedup ) ) return false;
	// sanity check!!! must be something there!
	if ( g_qbuf.length() <= 0 ) { char *xx=NULL;*xx=0; }
	// TODO: now re-make g_qt so it has just one slot per wordId, i.e.
	// allowDups is false. then that slot points to a list of offsets
	// into g_qbuf.  might need to do this to speed things up.


	// now point to each query entry so we can sort them
	SafeBuf tmpBuf;
	long need = s_queryCount * 4;
	if ( ! tmpBuf.reserve ( need ) ) 
		return false;
	char *p = g_qbuf.getBufStart();
	char *pend = p + g_qbuf.length();
	long mcount = 0;
	// first is list size
	//long listSize = *(long *)p;
	//p += 4;
	// record that
	//char *nextList = p + listSize;
	// loop over the recs in this list
	for ( ; p < pend ; ) {
		// point to that
		if ( ! tmpBuf.pushLong((long)p) ) 
			return false;
		// cast it
		QueryLogEntry *qe = (QueryLogEntry *)p;
		// qn
		//char qn = *p++;
		// then pop
		//long qpop = *(long *)p; 
		//p += 4;
		// then qids
		//unsigned long *qids = p;
		//p += 4 * qn;
		// then qstr
		//char *qstr = p;
		//p += gbstrlen(p)+1;
		p += qe->getSize();
		// count them
		mcount++;
		// end of list?
		//if ( p != nextList ) continue;
		// get new list size
		//listSize = *(long *)p;
		//p += 4;
		//nextList = p + listSize;
	}
	// sanity
	if ( mcount != s_queryCount ) { char *xx=NULL;*xx=0; }
	// sort them now
	char *qq = tmpBuf.getBufStart();
	qsort ( qq,mcount,sizeof(char *),buildcmp);
	
	// now sort into a new safebuf and also add "listsize" into there
	SafeBuf newBuf;
	need = g_qbuf.length() + tmpBuf.length();
	if ( ! newBuf.reserve ( need ) ) return false;
	char **src = (char **)tmpBuf.getBufStart();
	unsigned long lastMinQid = 0;
	long lastListSizeOffset = -1;
	long cc =0;
	for ( long i = 0 ; ; i++ ) {
		// over?
		if ( i == mcount ) {
			// store the list size into space we reserved below
			char *buf = newBuf.getBufStart();
			buf += lastListSizeOffset;
			long sz = newBuf.length()-lastListSizeOffset-4;
			*(long *)buf = sz;
			break;
		}
		// get the ptr to the query entry
		//char *qeptr = src[i];
		// cast it
		QueryLogEntry *qe = (QueryLogEntry *)src[i];
		// point to its qids
		unsigned long *qids = qe->m_queryTermIds32;//(unsigned long *)(qeptr+5);
		// sanity!
		if ( qids[0] == 0 ) { char *xx=NULL;*xx=0; }
		// reserve spot for list size for all query entries that
		// have this same minimum qid
		if ( qids[0] != lastMinQid ) {
			// store in it
			if ( lastListSizeOffset != -1 ) {
				char *buf = newBuf.getBufStart();
				buf += lastListSizeOffset;
				long sz = newBuf.length()-lastListSizeOffset-4;
				*(long *)buf = sz;
			}
			// update to new one
			lastListSizeOffset = newBuf.length();
			lastMinQid = qids[0];
			// temporarily store a zero for following list's size
			newBuf.pushLong(0);
		}
		// save ptr start
		//char *recStart = qeptr;
		// skip it
		//qeptr += qe->getSize();
		// now copy over the rest
		//char qn = *qeptr++;
		// then pop
		//qeptr += 4;
		// then qids
		//qeptr += 4 * qn;
		// then qstr
		//qeptr += gbstrlen(qeptr) + 1;
		// debug it
		if ( cc++ < 100 )
			log("qlog: pushing size=%li qstr=%s off=%li",
			    qe->getSize(),qe->getQueryStr() ,newBuf.length());
		// copy that
		if ( ! newBuf.safeMemcpy ( qe,qe->getSize() ) )
			return false;
	}

	// print out the first 100 entries
	p = newBuf.getBufStart();
	long ecount = 0;
	long listSize = *(long *)p;
	p += 4;
	char *nextList = p + listSize;
	for ( ; ecount < 100 ;  ecount++ ) {
		// cast it
		QueryLogEntry *qe = (QueryLogEntry *)p;
		// qn
		char qn = qe->m_qn;//*p++;
		// then pop
		//long pop = *(long *)p; 
		//p += 4;
		// skip it 
		p += qe->getSize();
		// then qids
		unsigned long *qids =qe->m_queryTermIds32;//(unsigned long *)p;
		//p += 4 * qn;
		// then qstr
		char *qstr = qe->getQueryStr();//p;
		//p += gbstrlen(p) + 1;
		// print that
		//char tmp[5000];
		SafeBuf sb;
		//char *x = tmp;
		sb.safePrintf("qlog: qn=%li "   , (long)qn );
		for ( long k = 0 ; k < qn ; k++ )
			sb.safePrintf("qid%li=%lu ",k,qids[k]);
		sb.safePrintf("qstr=");
		// encode it so % doesn't hurt the log statement!
		char *x = qstr;
		for ( ; *x ; x++ ) {
			if ( *x == '%' ) sb.pushChar(' ');
			else sb.pushChar(*x);
		}
		sb.pushChar('\0');
		log(sb.getBufStart());
		if ( p == nextList ) {
			listSize = *(long *)p;
			log("qlog: nextlist follows of %li bytes",listSize);
			p += 4;
			nextList = p + listSize;
		}
	}

	// now swap out!
	g_qbuf.purge();
	// copy over
	if ( ! g_qbuf.safeMemcpy ( &newBuf ) )
		return false;
	// purge the rest now
	newBuf.purge();
	tmpBuf.purge();


	/*
	// make a tmp buf to re-write g_qbuf into
	SafeBuf tmpBuf;
	long need = g_qbuf.length() + 5000000 
	if ( ! tmpBuf.reserve ( need ) ) return false;
	char *dst = tmpBuf.getBufStart();
	char *src = g_qbuf.getBufStart();
	// add list info
	long listSize = 0;
	// point to it
	long *lastListSize = dst;
	// skip for now
	dst += 4;
	// must not have reallocated
	if ( tmpBuf.length() != need ) { char *xx=NULL;*xx=0; }
	*/		       


	// now tie the buffer to the hashtable
	//g_qt.setBuffer ( &g_qbuf );
	// note it
	log("xmldoc: done generating %s. saving.",myFile);
	// and save it
	//g_qt.save ( g_hostdb.m_dir , myFile , &g_qbuf );
	g_qbuf.saveToFile ( g_hostdb.m_dir , myFile );
	return true;
}

// i prepared the querylog.txt.part1 etc. files on gk268 from query logs
bool loadQueryLogPartFile ( long part , HashTableX *dedup ) {
	// generate it otherwise
	char filename[2000];
	snprintf(filename,2000,"%squerylog.txt.part%li",g_hostdb.m_dir,part);
	if ( gbstrlen(filename) > 1990 )
		// return false on error
		return log("xmldoc: %s path is too long",filename);
	FILE *fd = fopen( filename , "r");
	if ( ! fd ) 
		return log("xmldoc: could not open %s: %s",
			   filename,mstrerror(errno));
	long line = 0;
	// how many hosts in network?
	//long numHosts = g_hostdb.m_numHosts;

	// does this belong to us or not?
	// let's use group redundancy
	long numGroups = g_hostdb.getNumGroups();
	// get our group #... starts at 0
	long me = g_hostdb.m_myHost->m_group;

	// make this a min of 16 otherwise we run outta mem!! dammit!!
	if ( numGroups < 32 ) {
		log("xmldoc: removing significant pieces of query log "
		    "because we can't fit into memory");
		numGroups = 32;
	}
	// how many total lines?
	long totalLines = 0;
	if ( part == 1 ) totalLines = 50000000;
	if ( part == 2 ) totalLines = 32000000;

	// scan each line
	char lineBuf[2100];
	for  ( ; ; line++ ) {
		// returns false if all done
		bool status = fgets ( lineBuf, 2000, fd );
		// record last list size if all done
		if ( ! status ) break;
		// count every 100000 we do
		if ( ! (line % 100000) ) 
			log("querylog: line #%li of %li",line,totalLines);
		// skip if line not for us
		//if ( line % numHosts != g_hostdb.m_hostId ) continue;
		// ok, we are responsible otherwise
		long pop = atol(lineBuf);
		// make "qs" point to the query
		char *qs = lineBuf;
		// skip initial spaces before pop
		while ( *qs == ' ' ) qs++;
		// skip over pop to query
		for ( ; *qs && *qs != ' ' && *qs !='\n' ; qs++ );
		// skip spaces
		while ( *qs == ' ' ) qs++;
		// empty?
		if ( *qs == '\n' ) continue;
		// eof?
		if ( ! *qs ) break;
		// remove \n
		long qslen = gbstrlen(qs);
		if ( qs[qslen-1] == '\n' ) qs[qslen-1] = '\0';
		// skip if has colon
		long k; 
		for ( k = 0 ; k < qslen ; k++ ) 
			if ( qs[k] == ':' ) break;
		// if it had a colon, go to next query. skip inurl: etc.
		if ( k < qslen ) continue;
		// panic?
		if ( qs[qslen-1] ) 
			return log("xmldoc: %s malformed line",filename);

		//
		// rewrite the query to clean it up before storing
		//
		char *src = qs;
		char dstBuf[3000];
		char *dst = dstBuf;
		bool hadAlnum = false;
		bool lastWasSpace = true;
		char *afterLastAlnum = NULL;
		char len;
		for ( ; *src ; src += len ) {
			// get size
			len = getUtf8CharSize(src);
			// alnum?
			if ( is_alnum_utf8(src) ) {
				// to lower
				to_lower_utf8(dst,src);
				// skip it
				dst += len;
				// set some flags
				lastWasSpace = false;
				hadAlnum     = true;
				// remember last one
				afterLastAlnum = dst;
				continue;
			}
			// . otherwise, it's punct
			// . skip if have not had alnum yet
			if ( ! hadAlnum && *src != '\"' ) continue;
			// . ignore "+AND+"
			// . ixquick was putting those in there for boolean
			if ( src[0] == '+' &&
			     src[1] == 'A' &&
			     src[2] == 'N' &&
			     src[3] == 'D' &&
			     src[4] == '+' ) 
				// point to last space then
				src += 4;
			// convert stuff to space. fix 'energy;wind' query
			if ( *src == ';' ) *src = ' ';
			// no back to back spaces
			if ( *src == ' ' && lastWasSpace ) continue;
			// set this
			if ( *src == ' ' ) lastWasSpace = true;
			else               lastWasSpace = false;
			// copy it
			if ( len == 1 ) *dst = *src;
			else memcpy ( dst , src , len );
			// skip it
			dst += len;
		}
		// bail if no alnum
		if ( ! hadAlnum ) continue;
		// include ending double quote, but that is the only punct
		// we allow to end the query
		if ( *afterLastAlnum == '\"' ) afterLastAlnum++;
		// null term it
		*afterLastAlnum = '\0';

		// if query is badly formatted, just skip it then
		if ( ! verifyUtf8(dstBuf) ) 
			continue;

		bool doubleDecoded = false;

	doubleDecode:
		// url decode it
		long oldLen = dst - dstBuf;
		long newLen = urlDecode ( dstBuf , dstBuf , oldLen );
		dstBuf[newLen] = '\0';
		
		//if ( doubleDecoded )
		//	log("qlog: fixed '%s'",dstBuf);

		// skip if it has <alnum>+<alnum>
		char last = '0';
		char *x; for ( x = dstBuf ; *x ; x++ ) {
			if ( *x != '+' ) {
				last = *x;
				continue;
			}
			if ( is_alnum_a(last) &&
			     x[1] && 
			     is_alnum_a(x[1]) ) 
				break;
			// otherwise, forget it
			last = *x;
		}
		if ( *x ) {
			if ( doubleDecoded ) {
				log("qlog: skipping '%s'",dstBuf);
				continue;
			}
			doubleDecoded = true;
			// try double decode
			goto doubleDecode;
		}

		//
		// set the query to the words class for hashing etc.
		//
		Words ww;
		// niceness = 0
		ww.set9 ( dstBuf , 0 ); // qs , 0 ); 
		// store words into buffer
		long na = ww.getNumAlnumWords();
		// sanity. not too big.
		// . can't be bigger than 63 anyway because we do "qn << 2" up
		//   above to multiply this unsigned char by 4!
		if ( na >= 64 ) continue;

		// hash it into "ah64"
		unsigned long long *wids ;
		wids = (unsigned long long *)ww.getWordIds();
		unsigned long long ah64 = 0;
		long i; for ( i = 0 ; i < ww.getNumWords() ; i++ ) {
			// skip non-alnum words
			if ( ! wids[i] ) continue;
			// gotta shift first so not always divisible by 2!
			ah64 <<= 1LL;
			// hash it up
			ah64 ^= (unsigned long long)wids[i];
			// if next 3 wids are the same bail!
			// fix "search+search+search" 
			// although might screw up tora tora tora
			if ( i+4< ww.getNumWords() &&
			     wids[i  ] == wids[i+2] &&
			     wids[i+2] == wids[i+4] ) 
				break;
			// fix for repeated words
			//ah64 ^= g_hashtab[(unsigned char)i][0];
		}

		// bail if we cut out early
		if ( i < ww.getNumWords() ) continue;

		// does it belong to our group?
		if ( (ah64 % numGroups) != (unsigned long)me )
			continue;

		// see if we already added this hash
		long dslot = dedup->getSlot(&ah64);
		if ( dslot >= 0 ) {
			// get the g_qbuf ptr so we can just increment the pop
			char *qbptr = g_qbuf.getBufStart();
			// get the offset
			long offset = *(long *)dedup->getValueFromSlot(dslot);
			// add offset
			qbptr += offset;
			// get it
			QueryLogEntry *qle = (QueryLogEntry *)qbptr;
			// point to pop, just skip the one byte "na"
			//long *popPtr = (long *)(qbptr + 1);
			// increment it
			//*popPtr = *popPtr + pop;
			qle->m_gigablastTraffic += pop;
			// all done
			continue;
		}

		// get offset length
		long startOff = g_qbuf.length();

		// entry for deduping. data val is the 4 byte offset, key
		// is the 8 byte hash
		if ( ! dedup->addKey ( &ah64 , &startOff ) ) 
			return false;

		/*
		long savedLastMinQid = s_lastMinQid;
		unsigned long minQid = 0xffffffff ;
		for ( long i = 0 ; i < ww.getNumWords() ; i++ ) {
			// skip non-alnum words
			if ( ! wids[i] ) continue;
			// otherwise convert to 32 bit
			unsigned long wid32 = (unsigned long)wids[i];
			// skip if stop word
			if ( isStopWord32(wid32) ) continue;
			// and store
			if ( wid32 < minQid ) minQid = wid32;
		}
		// all stop words?
		if ( minQid == 0xffffffff ) continue;

		// reserve spot for list size for all query entries that
		// have this same minimum qid
		if ( s_lastMinQid != minQid || s_lastListSizeOffset == -1 ) {
			s_lastListSizeOffset = g_qbuf.length();
			s_lastMinQid = minQid;
			g_qbuf.pushLong(0);
		}
		*/

		// sanity
		if ( g_qbuf.m_length > 100 &&
		     ((QueryLogEntry *)g_qbuf.m_buf)->m_flags ) {
			char *xx=NULL;*xx=0; }

		// set this
		QueryLogEntry qe;
		// the # of query terms, 32-bits each
		qe.m_qn = na;
		// store it first
		//g_qbuf.pushChar(na);
		// the traffic
		qe.m_gigablastTraffic = pop;
		// google traffic # is unknown until we scrape adwords page
		qe.m_googleTraffic    = -1;
		qe.m_googleTrafficDate = 0;
		// flags. will set QEF_ESTIMATE_SCORE if mintop50score is an 
		// estimate because we did a partial query.
		qe.m_flags = 0;
		// date the top50score was last computed. 0 means none.
		qe.m_minTop50ScoreDate = 0;
		// min top 50 score. set to unknown for now
		qe.m_minTop50Score = -1.0;
		qe.m_topSERPScore  = -1.0;
		// how many search results total in slice for query...
		qe.m_numTotalResultsInSlice = -1;
		// compute the language i guess
		qe.m_langId = ww.getLanguage();
		// store that
		g_qbuf.safeMemcpy ( &qe, sizeof(QueryLogEntry) );
		// sanity
		if ( ((QueryLogEntry *)g_qbuf.m_buf)->m_flags ) {
			char *xx=NULL;*xx=0; }
		// ptr for sorting
		long soff = g_qbuf.length();
		// then store the wordids (32 bits each!)
		long count = 0;
		for ( long i = 0 ; i < ww.getNumWords() ; i++ ) {
			// skip non-alnum words
			if ( ! wids[i] ) continue;
			// otherwise convert to 32 bit
			unsigned long wid32 = (unsigned long)wids[i];
			// and store
			g_qbuf.pushLong(wid32);
			count++;
		}
		// sanity check
		if ( count != na ) {
			g_qbuf.m_length = startOff;
			//s_lastMinQid = savedLastMinQid;
			//return log("xmldoc: %s bad alnum word count",
			//filename);
			continue;
		}

		// sort them now. put stop words at the back since we ignore
		// then in the twid vector in handleRequest99()
		unsigned long *qidBuf ;
		qidBuf = (unsigned long *)(g_qbuf.getBufStart()+soff);
		qsort(qidBuf,count,4,qlogcmp);

		// then the query string itself
		g_qbuf.safeStrcpy ( dstBuf ); // qs );
		// then the \0
		g_qbuf.pushChar('\0');
		// count it
		s_queryCount++;
		/*
		// dedup words in query
		HashTableX wdup;
		char wbuf[1000];
		wdup.set(4,0,32,wbuf,1000,false,0,"wduptb");
		// now hash each wid32 and make it point into "startOff"
		for ( long i = 0 ; i < ww.getNumWords() ; i++ ) {
			// skip non-alnum words
			if ( ! wids[i] ) continue;
			// otherwise convert to 32 bit
			unsigned long wid32 = (unsigned long)wids[i];
			// if query has a word that repeats, only index it
			// once, ok?
			if ( wdup.isInTable(&wid32) ) 
				continue;
			// add it to table
			wdup.addKey(&wid32);
			// store it. 4 byte key, 4 byte data
			g_qt.addKey ( &wid32 , &startOff );
		}
		*/
	}
	fclose(fd);
	return true;
}

// . call this when done transmitting so we can nuke the xmldoc class
// . it might be called if an error occured as well
//void doneTransmittingSEOKeywords ( void *state ) {
//	XmlDoc *xd = (XmlDoc *)state;
//	// nuke it
//	mdelete ( xd , sizeof(XmlDoc) , "XmlDoc" );
//	delete (xd);
//	// that's it!
//}

// we come here after calling xd->indexDoc()
void sendPageBackWrapper ( void *state ) {

	XmlDoc *xd = (XmlDoc *)state;

	// get socket before deleting it
	//TcpSocket *sock = xd->m_seoSocket;

	// do not allow xmldoc to destroy it!
	//xd->m_seoSocket = NULL;

	// save it
	long err = g_errno;

	// nuke it
	mdelete ( xd , sizeof(XmlDoc) , "XmlDoc" );
	delete (xd);

	if ( ! err ) return;

	// error?
	//g_httpServer.sendErrorReply(sock,500,"error initializing xmldoc");
	log("seo query info error: %s",mstrerror(err));
}

/*
// . show queries that match a url
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . alias: sendPageQueries()
bool sendPageMatchingQueries ( TcpSocket *s , HttpRequest *r ) {
	// get the collection
	long  collLen = 0;
	char *coll  = r->getString ( "c" , &collLen  , NULL );
	if ( ! coll ) coll = "main";
	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("keywords: request from %s failed. "
		    "Collection \"%s\" does not exist.",
		    iptoa(s->m_ip),coll);
		return g_httpServer.sendErrorReply(s,500,
						"collection does not exist");
	}

	// if no url was given just print the page
	long urlLen;
	char *url = r->getString("u",&urlLen);
	SafeBuf sb;

	g_pages.printAdminTop ( &sb ,
				s ,
				r ,
				NULL ,
				NULL );

	sb->safePrintf(
		      "<center>"
		      //"<FORM method=POST action=/inject>\n\n" 
		      //"<input type=hidden name=pwd value=\"%s\">\n"
		      //"<input type=hidden name=username value=\"%s\">\n"
		      "<table width=100%% bgcolor=#%s cellpadding=4 border=1>"
		      "<tr><td  bgcolor=#%s colspan=2>"
		      "<center>"
		      "<b>"
		      "Matching Queries Tool</b>"
		      "<br>"
		      "</td></tr>\n\n"
		      
		      "<tr><td><b>url</b></td>"
		      "<td>\n"
		      "<input type=text name=u value=\"\" size=50>"
		      "</td></tr>\n\n"

		      "<tr><td><b>max queries to compute</b></td>"
		      "<td>\n"
		      "<input type=text name=maxqueries value=\"30\" "
		      "size=5>"
		      "<br>"
		      "<font size=-1>"
		      "<i>"
		      "Perform up to this many full queries. Each query "
		      "takes about 11 seconds to execute on average. "
		      "The more you do the more related urls you will get and "
		      "the more related queries you will get."
		      "</i>"
		      "</font>"

		      "</td></tr>\n\n"

		      "<tr><td><b>extra queries</b></td>"
		      "<td>\n"
		      "<textarea name=extraqueries cols=80 rows=10>"
		      "</textarea>"
		      "<br>"
		      "<font size=-1>"
		      "<i>"
		      "Include ONE QUERY PER LINE."
		      "<br>"
		      "PUT the average "
		      "number of MONTHLY searches as an integer in "
		      "parentheses "
		      "preceeding the query on that line."
		      "<br>"
		      "Example: (13) this is my query"
		      "<br>"
		      "Evaluate the URL's score for each query provided here."
		      "</i>"
		      "</font>"
		      "</td></tr>\n\n"



		      "</table>\n"

		      "</center>\n"
		      "</form>\n"
		      "</body>\n"
		      "</html>\n"
		      , LIGHT_BLUE 
		      , DARK_BLUE 
		      );
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	// point to it as string
	char *buf    = sb.getBufStart();
	long  bufLen = sb.length();
	// . send this page if we do not have a url to process
	// . encapsulates in html header and tail
	// . make a Mime
	// . i thought we need -2 for cacheTime, but i guess not
	if ( ! url )
		return g_httpServer.sendDynamicPage ( s, 
						      buf, 
						      bufLen, 
						      -1 ); // cachetime


	if ( g_hostdb.m_myHost->m_hostId != 0 ) {
		g_errno = EBADENGINEER;
		return g_httpServer.sendErrorReply(s,g_errno,
						   "can only do this on "
						   "host #0");
	}

	// make a new state
	XmlDoc *xd;
	try { xd= new (XmlDoc); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("keywords: new(%i): %s", 
		    sizeof(XmlDoc),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( xd, sizeof(XmlDoc) , "XmlDockt");

	// . store the provided socket
	// . we will transmit the buffer in english to the socket as we
	//   get each result
	// . then steve can sort the results by m_seoDifficultyScore on the
	//   user-end, because this process can take a long time to run
	//   through and evaluate all the queries
	xd->m_seoSocket = s;

	// set the url
	xd->setFirstUrl ( url , true , NULL );

	// "main" collection?
	strncpy(xd->m_collBuf,coll,MAX_COLL_LEN);
	xd->m_coll = xd->m_collBuf;
	xd->m_cr   = cr;

	// make sure we flush the msg4 buffers since we query for the url
	// right away almost!
	xd->m_contentInjected = true;

	// -1 means no max
	xd->m_maxQueries = r->getLong("maxqueries",30);
	// limit to 50 no matter what lest our precious resources vanish
	if ( xd->m_maxQueries > 50 ) {
		log("seopipe: limiting maxqueries of %li to %li",
		    xd->m_maxQueries,(long)50);
		xd->m_maxQueries = 50;
	}
	//xd->m_maxRelatedQueries = r->getLong("maxrelatedqueries",-1);
	//xd->m_maxRelatedUrls = r->getLong("maxrelatedurls",-1);

	// . list of \n (url encoded) separated queries to add to our list
	// . we gotta copy since HttpRequest, "r", will go bye-bye on return
	//   of this function.
	// . copy these queries supplied by the user into m_extraQueryBuf
	//   in the exact same format that we get Msg99 replies. that way
	//   we can use our m_queryPtrsBuf safebuf to reference them the same
	//   way those reference the queries in the msg99 replies.
	char *eq = r->getString("extraqueries",NULL);
	if ( eq ) {
		// scan the queries in there
		char *p = eq;
		// loop here for each line
	doNextLine:
		// set nextLine for quick advancing
		char *nextLine = p;
		// skip to next \n or \0
		for ( ; *nextLine && *nextLine != '\n' ; nextLine++ );
		// save that
		char *queryEnd = nextLine;
		// if \n, skip that
		if ( *nextLine == '\n' ) nextLine++;
		// skip white space before traffic number
		for ( ; *p && is_wspace_a(*p) ; p++ );
		// all done?
		if ( ! *p ) goto doneScanningQueries;
		// must be '('
		if ( *p != '(' ) { p = nextLine; goto doNextLine; }
		// skip '('
		p++;
		// then a digit
		long traffic = atoi(p);
		// skip till ')'
		for ( ; *p && *p !=')' ; p++ );
		// must be ')'
		if ( *p != ')' ) { p = nextLine; goto doNextLine; }
		// skip ')'
		p++;
		// skip spaces after the (X) traffic number
		for ( ; *p && is_wspace_a(*p) ; p++ );
		// now that's the query
		char *qstr = p;
		// find end of it
		*queryEnd = '\0';
		// store traffic
		xd->m_extraQueryBuf.pushLong(traffic);
		// fake query score of -1, means a manual query
		xd->m_extraQueryBuf.pushLong(-1);
		// then the query
		xd->m_extraQueryBuf.safeStrcpy(qstr);
		// then the \0
		xd->m_extraQueryBuf.pushChar('\0');
		// do next query
		p = nextLine; goto doNextLine;
	}
	// all done?
 doneScanningQueries:

	//
	// index the doc first (iff not in index)
	//
	// only index it if not indexed yet
	xd->m_newOnly          = true;
	// do not lookup ips of each outlink
	xd->m_spiderLinks      = false;
	xd->m_spiderLinksValid = true;
	// and set the SpiderRequest record which details how to
	// spider it kinda
	long firstIp = hash32n(url);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	strcpy (xd->m_oldsr.m_url,url);
	xd->m_oldsr.m_firstIp       = firstIp;
	xd->m_oldsr.m_isInjecting   = 1;
	xd->m_oldsr.m_hopCount      = 0;
	xd->m_oldsr.m_hopCountValid = 1;
	xd->m_oldsr.m_fakeFirstIp   = 1;
	xd->m_oldsrValid            = true;
	// other crap from set3() function
	xd->m_version      = TITLEREC_CURRENT_VERSION;
	xd->m_versionValid = true;

	// unless i set the niceness to 1 i cannot connect to the http server
	// when it's sending out the msg9s in parallel in the getQueryRels() 
	// function. seems to really jam the cpu.
	xd->m_niceness     = 1;

	// when done indexing call this
	xd->m_callback1 = sendPageBackWrapper;
	// and use this state
	xd->m_state = xd;

	// fix core when calling getUrlFilterNum()
	xd->m_priority = 50;
	xd->m_priorityValid = true;

	xd->m_recycleContent = true;
	//xd->m_recycleLinkInfo = true;

	//xd->m_seoDebug = r->getLong("debug",0);

	// try to index it
	//if ( ! xd->indexDoc() ) return false;
	if ( xd->getSEOQueryInfo() == (void *)-1 ) return false;

	// all done no callback
	sendPageBackWrapper ( xd );

	return true;
}
*/

/*
static void cacheTermListsWrapper ( void *state ) {
	XmlDoc *THIS = (XmlDoc *)state;
	if ( ! THIS->cacheTermLists() ) return;
	THIS->m_callback1 ( THIS->m_state );
}
*/


//
//
// KEYWORD INSERTION TOOL (KIT) logic
//
//

//class TermPair {
//public:
//	long m_wordPos1;
//	long m_wordPos2;
//	char m_densityRank1;
//	char m_densityRank2;
//	char m_hashGroup1; // title? body? metakeywords? ...
//	char m_hashGroup2;
//	float m_score;
//};

class QueryMatch {
public:
	// point to this in our local g_qbuf
	QueryLogEntry *m_queryLogEntry;
	// . and a list of PairScores for this query executed on this docid
	// . this offset is into State3g::m_pairScoreBuf
	//long m_pairScoreBufOffset;
	//long m_numPairScores;
};


//
//
// THIS is the HEART of the keyword insertion tool
//
//
// . record the scores from inserting each provided term into every
//   possible word position in the document
// . the entire termlist of the document should also be in the request.
// . compute an InsertedTerm class for each term provided and transmit
//   that back.
void handleRequest95 ( UdpSlot *slot , long netnice ) {

	State95 *st95 = NULL;
	try { st95 = new (State95); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("seo: new(%i): %s", 
		    sizeof(State95),mstrerror(g_errno));
		g_udpServer.sendErrorReply(slot,500,g_errno);
	}
	mnew ( st95, sizeof(State95) , "st95");


	st95->m_udpSlot  = slot;
	st95->m_niceness = netnice;

	st95->handleRequest ( );
}

static void sendMsg95Reply ( void *starg ) ;

State95 *g_st95;

void State95::handleRequest ( ) {

	m_req95 = (Msg95Request *)m_udpSlot->m_readBuf;

	deserializeMsg ( sizeof(Msg95Request) ,
			 &m_req95->size_posdbTermList,
			 &m_req95->size_insertableTerms,
			 &m_req95->ptr_posdbTermList,
			 m_req95->m_buf );



	// list of QueryChanges that we will send back in the reply
	if ( ! m_queryChangeBuf.reserve ( 1000000 ) ) {
		sendErrorReply95 ( );
		return;
	}

	// the # of QueryChanges in the buffer
	//long nqc = 0;
	// save as a place holder until we are done
	//m_queryChangeBuf.pushLong ( nqc );


	// fill our m_queryMatchBuf with queries that match our doc
	if ( ! setQueryMatchBuf() ) {
		sendErrorReply95();
		return;
	}

	// set m_plistTable to map a termId to our docids posdblist for it
	if ( ! setPosdbListTable ( &m_plistTable ,
				   &m_listBuf,
				   m_req95->ptr_posdbTermList ,
				   m_req95->size_posdbTermList ,
				   m_req95->m_docId ,
				   m_niceness ) ) {
		sendErrorReply95();
		return;
	}

	// set the term freq table too! should map any word in any
	// "insertable term" (might be > 1 word) to a term freq from host
	// #0 or its twin. we need to only get termfreqs from them for
	// consistency!
	if ( ! setTermFreqTable () ) {
		sendErrorReply95();
		return;
	}

	// set m_wordInfoBuf so addQueryChangesForQuery() which scans
	// the list of insertable terms doesn't have to hash the insertable
	// terms for every query.
	if ( ! setWordInfoBuf ()) {
		sendErrorReply95();
		return;
	}

	/*
	// print the word pos info map!
	if ( g_conf.m_logDebugSEOInserts ) {
		long numWordPosInfos = m_req95->size_wordPosInfoBuf;
		numWordPosInfos /= sizeof(WordPosInfo);
		WordPosInfo *wpis = (WordPosInfo *)m_req95->ptr_wordPosInfoBuf;
		SafeBuf sb;
		for ( long wi = 0 ; wi < numWordPosInfos ; wi++ ) {
			// get it
			WordPosInfo *wpi = &wpis[wi];
			// print it
			sb->safePrintf("(%li,hg=%s,dr=%li)",
				      wpi->m_wordPos,
				      getHashGroupString(wpi->m_hashGroup),
				      (long)wpi->m_densityRank
				      );
			sb->safeMemcpy(wpi->m_wordPtr,wpi->m_wordLen);
			sb.pushChar(' ');
		}
		log("seo: wordmap=%s",sb.getBufStart());
	}
	*/

	char *px = m_req95->ptr_posdbTermList;
	if ( m_req95->size_posdbTermList <= 0 ) { char *xx=NULL;*xx=0; }
	m_siteRank = g_posdb.getSiteRank ( px );
	if ( m_siteRank < 0 || m_siteRank > MAXSITERANK) {char *xx=NULL;*xx=0;}

	m_numDocsInColl = 0;
	RdbBase *base = getRdbBase ( RDB_CLUSTERDB  , m_req95->ptr_coll );
	if ( base ) m_numDocsInColl = base->getNumGlobalRecs();
	if ( m_numDocsInColl <= 0 ) m_numDocsInColl = 1;

	QueryMatch *qm =(QueryMatch *)m_queryMatchBuf.getBufStart();
	// stop here
	QueryMatch *qmend =(QueryMatch *)m_queryMatchBuf.getBuf();
	// count it
	long numTotal = qmend - qm;
	long count = 0;
	// scan those matching queries
	for ( ; qm < qmend ; qm++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// log it
		log("seo: adding query changes for '%s' (%liof%li)"
		    ,qm->m_queryLogEntry->getQueryStr()
		    ,count
		    ,numTotal
		    );
		// evaluate that query for all terms and add QueryChange
		// instances to m_queryChangesBuf when necessary
		if ( addQueryChangesForQuery ( qm ) ) continue;
		// log it
		log("seo: error adding query changes: %s",mstrerror(g_errno));
		// error!
		sendErrorReply95();
		return;
	}

	// overwrite placeholder with actual # of QueryChanges stored so
	// the reply is correct
	long nqc = m_queryChangeBuf.length() / sizeof(QueryChange);
	//char *qcStart = m_queryChangeBuf.getBufStart();
	//*(long *)qcStart = nqc;

	//
	// now to transmit back the array of QueryChange instances we
	// will want to include the query string itself for convenience.
	// so scan the QueryChange::m_queryOffset into our g_qbuf and
	// make it relative to st->m_queryLogBuf
	//
	HashTableX dups;
	if ( ! dups.set(4,4,1024,NULL,0,false,m_niceness,"offdup") ) {
		sendErrorReply95 ( );
		return;
	}
	QueryChange *qc = (QueryChange *)m_queryChangeBuf.getBufStart();
	long lastKey = -1;
	long lastVal = -1;
	for ( long i = 0 ; i < nqc ; i++ ) {
		// get query offset
		long qoff = qc[i].m_queryOffset3;
		// a performance shortcut
		if ( qoff == lastKey ) {
			qc[i].m_replyQueryOffset = lastVal;
			continue;
		}
		// skip if already done!
		long *val = (long *)dups.getValue ( &qoff ) ;
		// if we already stored query, reference it
		if ( val ) {
			qc[i].m_replyQueryOffset = *val;
			continue;
		}
		// get cursor
		long cursor = m_queryLogBuf.length();
		// get query entry
		QueryLogEntry *qe ;
		qe = (QueryLogEntry *)(g_qbuf.getBufStart()+qoff);
		// get string
		//char *qstr = qe->getQueryStr();
		// store it
		if ( ! m_queryLogBuf.safeMemcpy(qe,qe->getSize()) ) {
			sendErrorReply95 ( );
			return;
		}
		// overwrite offset to be relative to m_queryLogBuf now
		qc[i].m_replyQueryOffset = cursor;
		// add it for others
		if ( ! dups.addKey ( &qoff , &cursor ) ) {
			sendErrorReply95 ( );
			return;
		}
		// a performance shortcut
		lastKey = qoff;
		lastVal = cursor;
	}		


	// BUT NOW we must get the scores of the top 50 results for
	// each query we reference in the QueryChange buf, so we can
	// set the QueryChange::m_newRank and m_oldRank members.
	// then we use that ultimately to set the traffic changes.

	// we ONLY need to get search results for queries for which our
	// old or new score is estimated to be in the top 50 results
	// for that query. otherwise this will take FOREVER! so that is
	// why we only add a QueryChange if it meets that requirement.

	// and we need to cache these top 50 docids/scores somewhere too!
	// maybe in the seocache?

	m_p = m_queryLogBuf.getBufStart();
	m_pend = m_queryLogBuf.getBuf();
	m_coll = m_req95->ptr_coll;
	m_errno = 0;

	// scan our queries
	if ( ! performFullQueries () ) return;

	// it did not block
	sendMsg95Reply(this);
}

static void gotDocIdsForFullQueryWrapper ( void *state ) {
	State95 *st = (State95 *)state;
	st->gotDocIdsForFullQuery();
	if ( ! st->performFullQueries() ) return;
	sendMsg95Reply(st);
}

// . when we get the results of a query update the QueryChanges for that query
// . return false and set g_errno on error, true otherwise
bool State95::gotDocIdsForFullQuery ( ) {
	// . is it getting rank for a querychange?
	// . TODO: cache the docid/scores of this result
	// the query we were executing
	QueryLogEntry *qe = m_qe;
	// get offset of "qe"
	//long ourOff = (char *)qe - g_qbuf.getBufStart();
	// relative to the buffer of querylogentries we match
	long ourOff = (char *)qe - m_queryLogBuf.getBufStart();
	// init hashtable for mapping a score for this query to the rank
	// that that score would give you.
	HashTableX dups;
	if ( ! dups.set(4,4,512,NULL,0,false,m_niceness,"qedt") ) {
		m_errno = g_errno;
		log("seo: error initing dups table");
		// allow m39->reset() to be called
		//return;
	}
	// point to array of QueryChanges
	QueryChange *qcs;
	qcs = (QueryChange *)m_queryChangeBuf.getBufStart();
	// how many are there?
	long nqc = m_queryChangeBuf.length() / sizeof(QueryChange);
	// store score vector here
	SafeBuf scoreVec;
	long dummy = -999;
	// . populate the "dups" table for mapping a score to a rank for
	//   this query.
	// . scan all QueryChanges for this
	//   query and hash the m_oldScore and m_newScore so they
	//   map to a score of -1. then sort those scores. then
	//   map them to a rank vector by scanning the top tree
	//   of the search results for this query
	for ( long i = 0 ; i < nqc ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// shortcut
		QueryChange *qc = &qcs[i];
		// the query change must be for the query we just did
		if ( qc->m_replyQueryOffset != ourOff ) continue;
		// dedup its score
		if ( ! dups.isInTable ( &qc->m_oldScore ) ) {
			// do not add dups
			if ( ! dups.addKey(&qc->m_oldScore,&dummy) )
				// we will return a errno
				m_errno = g_errno;
			// add the score to our score vector
			scoreVec.pushFloat(qc->m_oldScore);
		}
		// dedup its score
		if ( ! dups.isInTable ( &qc->m_newScore ) ) {
			// do not add dups
			if ( ! dups.addKey(&qc->m_newScore,&dummy) )
				// we will return a errno
				m_errno = g_errno;
			// add the score to our score vector
			scoreVec.pushFloat(qc->m_newScore);
		}
	}
	// now sort all the scores from all the querychanges
	float *scores = (float *)scoreVec.getBufStart();
	qsort ( scores, scoreVec.length() / 4, sizeof(float),floatcmp);
	// score ptrs
	float *scorePtr = scores;
	float *scoreEnd = (float *)scoreVec.getBuf();
	// now scan the top search results for this query
	long       numResults   = m_msg3a.getNumDocIds();
	float     *resultScores = m_msg3a.getScores();
	long long *resultDocIds = m_msg3a.getDocIds();
	long addMe = 0;
	//if ( strcmp(m_query.m_orig,"power+led") == 0 )
	//	log("poo");
	//long rank = 0;
	for ( long i = 0 ; i <= numResults ; i++ ) { // , rank++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// done?
		if ( scorePtr >= scoreEnd ) break;
		// . get current search results rank of this result
		// . ignore our docid in the serps, we use "addMe" for that
		long srank = i + addMe;
		// end of results? throw in our results then
		if ( i == numResults ) {
			// pop all the scores of the stack into ranks
		subLoop:
			// done?
			if ( scorePtr >= scoreEnd ) break;
			// stop if full already. might have hit
			// our docid, so subtract 1.
			//if ( rank >= TOP_RESULTS_TO_SCAN-1 ) break;
			// toss it in there
			if ( ! dups.addKey(scorePtr,&srank) )
				// note the error
				m_errno = g_errno;
			// advance to next score
			scorePtr++;
			// keep rank same
			//rank--;
			goto subLoop;
		}
		// shortcuts
		//TopNode *t = &tt->m_nodes[ti];
		// ignore our docid. consider it removed.
		if ( resultDocIds[i] == m_req95->m_docId ) {
			// save the old score? is this right?
			//oldScore = t->m_score;
			//rank--; 
			addMe = -1;
			continue; 
		}
		// keep adding same rank for this score until it gets
		// <= resultscore at current ith position
	subLoop2:
		// or is exhausted!
		if ( scorePtr >= scoreEnd ) break;
		// test rank
		if ( *scorePtr <= resultScores[i] ) continue;
		// . overwrite it in the dups table
		// . rank is i!!!
		// . subtract 1 if we hit our own docid, consider it removed.
		if ( ! dups.addKey(scorePtr,&srank) )
			// note the error
			m_errno = g_errno;
		// advance to next score
		scorePtr++;
		// do a subloop
		goto subLoop2;
	}	
	//if ( strcmp(m_query.m_orig,"power+led") == 0 )
	//	log("poo");
	// now assign the QueryChange::m_old/newRank members
	float lastNewScore = -2.00;
	long  lastNewRank  = -1;
	float lastOldScore = -2.00;
	long  lastOldRank  = -1;
	for ( long i = 0 ; i < nqc ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// shortcut
		QueryChange *qc = &qcs[i];
		// does it match this query?
		if ( qc->m_replyQueryOffset != ourOff ) continue;
		// set its rank from score
		long rank;
		if ( qc->m_oldScore == 0.0 ) rank = -1;
		else if ( qc->m_oldScore == lastOldScore ) 
			rank = lastOldRank;
		else 
			rank = *(long *)dups.getValue(&qc->m_oldScore);
		// wtf?
		if ( rank == -999 ) { char *xx=NULL;*xx=0; }
		qc->m_oldRank = rank;
		lastOldScore = qc->m_oldScore;
		lastOldRank  = qc->m_oldRank;
		if ( qc->m_newScore == 0.0 ) rank = -1;
		else if ( qc->m_newScore == lastNewScore ) 
			rank = lastNewRank;
		else rank = *(long *)dups.getValue(&qc->m_newScore);
		qc->m_newRank = rank;
		lastNewScore = qc->m_newScore;
		lastNewRank = qc->m_newRank;
	}
	// save mem
	m_msg3a.reset();
	return true;
}

// . handleRequest95() has determined that html insertions have provided
//   a score for our url above the "m_required" score (m_minTop50Score)
//   so we need to do the full query to see exactly what our rank changes
//   would be.
// . XmlDoc::getMatchingQueriesScored() is similar code which
//   uses msg17 to hit the seoresults cache, so use that. actually, integrate
//   that code into Msg3a.cpp itself...
bool State95::performFullQueries ( ) {

 loop:

	// client close connection?
	


	// done?
	if ( m_p >= m_pend ) return true;

	// we now store QueryLogEntries in here
	QueryLogEntry *qe = (QueryLogEntry *)m_p;
	// save it
	m_qe = qe;
	// set query string
	char *qstr = qe->getQueryStr();
	long qlen = gbstrlen(qstr);
	// the language for synonyms
	uint8_t thisLangId = qe->getQueryLangId();
	// advance our cursor
	m_p += qe->getSize();

	// note it
	log("seo: doing query '%s' for current ranks and scores",qstr);

	// the search parms
	Msg39Request *r = &m_r;

	r->reset();

	r->m_getSectionStats = false;
	r->m_fastIntersection  = 1;
	r->m_language = thisLangId;
	r->m_getDocIdScoringInfo = false;
	r->ptr_coll = m_coll;
	r->size_coll = gbstrlen(m_coll)+1;
	r->m_niceness = m_niceness;
	r->ptr_query  = qstr;
	r->size_query = qlen+1;
	r->m_queryExpansion = 1;
	r->m_doSiteClustering    = true;
	r->m_doDupContentRemoval = true;
	r->m_docsToGet = TOP_RESULTS_TO_SCAN;
	// i'd like to heavily cache all queries in the log to get decent
	// response times!!!
	r->m_useSeoResultsCache = true;
	//r->m_seoDebug = m_req95->m_seoDebug;

	m_query.set2 ( qstr , 
		       thisLangId ,
		       true ); // do query expansion?
		 

	if ( ! m_msg3a.getDocIds ( &m_r ,
				   &m_query ,
				   this,
				   gotDocIdsForFullQueryWrapper
				   ) )
		// return false if this blocked
		return false;

	// it didn't block!
	goto loop;

}


void sendMsg95Reply ( void *state ) {

	// cast the state
	State95 *st = (State95 *)state;

	// all done, make the msg95 reply
	Msg95Reply mp;

	mp.ptr_queryChangeBuf    = st->m_queryChangeBuf.getBufStart();
	mp.ptr_debugScoreInfoBuf = st->m_debugScoreInfoBuf.getBufStart();
	mp.ptr_origScoreInfoBuf  = st->m_origScoreInfoBuf.getBufStart();
	mp.ptr_queryLogBuf       = st->m_queryLogBuf   .getBufStart();

	mp.size_queryChangeBuf    = st->m_queryChangeBuf.length();
	mp.size_debugScoreInfoBuf = st->m_debugScoreInfoBuf.length();
	mp.size_origScoreInfoBuf  = st->m_origScoreInfoBuf.length();
	mp.size_queryLogBuf       = st->m_queryLogBuf   .length();

	long replySize;
	char *reply = serializeMsg ( sizeof(Msg95Reply),
				     &mp.size_queryChangeBuf ,// firstSizeParm
				     &mp.size_queryLogBuf,//lastSizeP
				     &mp.ptr_queryChangeBuf,// firststrptr
				     &mp            , // thisPtr
				     &replySize     ,
				     NULL           ,
				     0              ,
				     true           );

	UdpSlot *udpSlot = st->m_udpSlot;

	// free it
	mdelete ( st , sizeof(State95) , "st95" );
	delete ( st );

	g_udpServer.sendReply_ass ( reply,
				    replySize,
				    reply,     // alloc
				    replySize, // allocSize
				    udpSlot ,
				    60 , // timeout in seconds
				    NULL, // callback state
				    NULL , // senddone callback
				    // to optimize our flow use 30 because
				    // we all return our replies at about
				    // the same time so there's lots of
				    // collisions???
				    30 ); // backoff in ms, 
}


bool State95::setTermFreqTable ( ) {
	// map a 32-bit word id to a 64-bit termfreq
	if ( !m_termFreqTable.set ( 8,8,4096,NULL,0,false,m_niceness,"tftbl") )
		return false;

	char *coll = m_req95->ptr_coll;
	// just scan all matching queries
	QueryMatch *qm =(QueryMatch *)m_queryMatchBuf.getBufStart();
	// stop here
	QueryMatch *qmend =(QueryMatch *)m_queryMatchBuf.getBuf();
	// scan those matching queries
	for ( ; qm < qmend ; qm++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// . stolen from addQueryChanges()
		// . set query
		Query q;
		// get it
		char *qstr = qm->m_queryLogEntry->getQueryStr();
		// we have to assign a language for synonym expansion to work
		// TODO: put a language in QueryLogEntry!!!
		q.set2 ( qstr , m_req95->m_docLangId, true );
		// scan terms
		for ( long i = 0 ; i < q.m_numTerms ; i++ ) {
			// shortcut
			QueryTerm *qt = &q.m_qterms[i];
			// get the full 64-bit hash of the word
			long long wid = qt->m_rawTermId;
			long long tf64 = g_posdb.getTermFreq ( coll, wid );
			// add it
			if ( ! m_termFreqTable.addKey ( &wid , &tf64 ) )
				return false;
		}
	}

	return true;

	/*
	// point to it
	char *p = m_req95->ptr_termInfoBuf;//wordFreqInfoBuf;
	char *pend = p + m_req95->size_termInfoBuf;
	// first is the language
	//m_docLang95 = *(uint8_t *)p;
	// skip langid
	//p++;
	// scan the WordFreqInfo buf now
	//WordFreqInfo *wfis = (WordFreqInfo *)p;
	//long nwfi = m_req95->size_wordFreqInfoBuf / sizeof(WordFreqInfo);
	//for ( long i = 0 ; i < nwfi ; i++ ) {
	for ( ; p < pend ; ) {
		TermInfo *ti = (TermInfo *)p;
		p += sizeof(TermInfo);
		// look it up
		long long tf64 = g_posdb.getTermFreq ( coll, ti->m_termId64 );
		// add it
		if ( ! m_termFreqTable.addKey ( &ti->m_termId64 , &tf64 ) )
			//&ti->m_termFreq64 ) )
			return false;
	}
	return true;
	*/
}

// . these are 1-1 with the insertable terms
// . they describe each term to insert
// . they are only wordids not phraseids
class WordInfo {
 public:
	long getSize ( ) { return sizeof(WordInfo) + m_numWordIds * 8; };
	long       m_numWordIds;
	long long  m_wordIds[0];
};


bool State95::setWordInfoBuf ( ) {

	// . scan the insertable terms, a list of \0 separated strings
	// . each term could be multiple words
	char *tstart       = m_req95->ptr_insertableTerms;
	char *tend         = tstart + m_req95->size_insertableTerms;

	// alloc enough space, about 1-1 i guess kinda
	long need = tend - tstart;
	if ( ! m_wordInfoBuf.reserve ( need*2 ) ) return false;

	// scan the InsertableTerms
	for (char *p = tstart ; p < tend ; ) {
		// cast it
		InsertableTerm *it = (InsertableTerm *)p;
		p += it->getSize();
		// get this
		char *term = it->getTerm();
		// breathe
		QUICKPOLL(m_niceness);
		// how many words?
		Words ww;
		ww.set3 ( term );
		// # of words in there?
		long nw = ww.getNumWords();
		// how much space, max? we only want alnum wordids stored.
		need = ww.getNumWords() * 8;
		// and the # of words
		need += sizeof(WordInfo);
		// reserve it
		if ( ! m_wordInfoBuf.reserve(need) ) return false;
		// ponit to it
		WordInfo *wi = (WordInfo *)m_wordInfoBuf.getBuf();
		// reset
		wi->m_numWordIds = 0;
		// the ptr
		long long *pwids = wi->m_wordIds;
		// shortcut
		long long *wids = ww.getWordIds();
		// set it up
		for ( long i = 0 ; i < nw ; i++ ) {
			// skip if not an alnum word
			if ( ! wids[i] ) continue;
			// add it
			*pwids++ = wids[i];
			// inc count
			wi->m_numWordIds++;
		}
		// realize it into the buffer
		long took = wi->getSize();
		m_wordInfoBuf.incrementLength(took);
	}
	return true;
}



bool State95::setQueryMatchBuf ( ) {

	// ptrs to the matching queries in the g_qbuf safebuf. we use these
	// to hold the queries our doc matches. each ptr is to a
	// QueryLogEntry class.
	if ( ! m_queryMatchBuf.reserve ( 1000000 ) ) return false;

	// just try scanning the whole query buffer for matching queries
	unsigned char qn;
	//long qpop;
	register unsigned long *qids;
	char *qstr;
	long numStopWords;
	long k;

	// breathe
	QUICKPOLL(m_niceness);

	// hash the twids into a hash table so we can quickly see if a
	// query term matches a twid (a word our document has indexed)
	long numTermInfos = m_req95->size_termInfoBuf/sizeof(TermInfo);
	long numSlots = numTermInfos * 4;

	// done? no queries match then
	if ( numTermInfos <= 0 ) return true;

	HashTableX qht;
	if ( ! qht.set (4,0,numSlots,NULL,0,false,m_niceness,"qhttab") ) 
		return false;
	TermInfo *tiPtr = (TermInfo *)(m_req95->ptr_termInfoBuf);
	long size = m_req95->size_termInfoBuf;
	TermInfo *tiEnd = (TermInfo *)(m_req95->ptr_termInfoBuf+size);
	unsigned long last32 = 0;
	for ( ; tiPtr < tiEnd ; tiPtr++ ) {
		QUICKPOLL(m_niceness);
		unsigned long tid32 = (unsigned long)(tiPtr->m_termId64);
		if ( tid32 <= last32 && last32 ) { char *xx=NULL;*xx=0; }
		last32 = tid32;
		qht.addKey( &tid32 );
	}

	// reset ptr
	tiPtr = (TermInfo *)(m_req95->ptr_termInfoBuf);

	// skip stop words
	for ( ; tiPtr < tiEnd ; ) {
		QUICKPOLL(m_niceness);
		unsigned long tid32 = (unsigned long)(tiPtr->m_termId64);
		if ( ! isStopWord32( tid32) ) break;
		tiPtr++;
	}

	// done? only consisted of stop words?
	if ( tiPtr >= tiEnd ) return true;

	// breathe
	QUICKPOLL(m_niceness);

	// divide up workload amongst twins
	long myStripe = g_hostdb.m_myHost->m_stripe;


	//long long startTime = gettimeofdayInMilliseconds();
	char *p = g_qbuf.getBufStart();
	char *pend = p + g_qbuf.length();

	// list size of query records that begin with the same minimum wordid
	long listSize = *(long *)p;
	p += 4;
	char *nextList = p + listSize;

	for ( ; p < pend ; ) {
		// cast it
		QueryLogEntry *qe = (QueryLogEntry *)p;
		// get the 32-bit hashes of the query words for this query 
		// in the log. the first qid in this array is what the 
		// queries are sorted by and is the minimal wordid of all
		// terms in the query.
		//qids = (unsigned long *)(p+5);
		qids = qe->m_queryTermIds32;
		// . quick check of first term, if not in tibuf, skip query
		// . no! keep the tibuf vector sorted so we can compare easily
		//   because a hashtable lookup is too expensive here
	subloop:

		// breathe
		QUICKPOLL(m_niceness);

		if ( qids[0] < ((unsigned long)(tiPtr->m_termId64)) ) {
			// skip entire list of queries for which this is the
			// minimum termid (non stop-word)
			p += listSize;
			// next list size
			listSize = *(long *)p;
			// skip list size itself
			p += 4;
			// update this
			nextList = p + listSize;
			continue;
		}
		if ( qids[0] > ((unsigned long)(tiPtr->m_termId64)) ) {
		skiptwid:
			// see if we can match it
			if ( ++tiPtr >= tiEnd ) 
				break;
			// also skip stop words. like 'the' is contained by
			// too many queries!! so just look at the non-stop
			// words. and we also make sure that the "qids[]" array
			// puts stop words termids at the back even if they
			// are the minimum hash! (see qlogcmp())
			if ( isStopWord32((unsigned long)(tiPtr->m_termId64))) 
				goto skiptwid;
			// loop back
			goto subloop;
		}
		// get # of query terms
		qn = qe->m_qn; //qn = (unsigned char)*p;
		// count these
		numStopWords = 0;
		// does it match us?
		for ( k = 0 ; k < qn ; k++ ) {
			// stop if not in twids
			if ( qht.getSlot(&qids[k]) < 0 ) break;
			// if all query terms are stop words then
			// ignore the query because it looks kinda
			// crap to have crap like "to" or "to to" 
			// or "and the" in there.
			// why is 'from+a' not triggering this?
			if ( isStopWord32(qids[k]) ) numStopWords++;
		}
		// backtrack a little
		//qpop = *(long *)(p+1);
		// skip p beyond
		qstr = qe->getQueryStr();//p + 1 + 4 + qn*4;
		// save it
		//char *qptr = p;
		// skip over terminating \0
		//p = qstr + gbstrlen(qstr) + 1;
		// skip the query log entry now
		p += qe->getSize();
		// end of list?
		if ( p == nextList ) {
			listSize = *(long *)p;
			p += 4;
			nextList = p + listSize;
		}

		// which twin will handle this query?
		long stripe = qids[0] % g_hostdb.getNumHostsPerGroup();
		if ( stripe != myStripe ) continue;

		// or if he has zero results! wtf?
		// 'design and' query and i think maybe 'able 5' have
		// this because everyone was ranking like #1 spot for them
		if ( qe->m_numTotalResultsInSlice <= 0 ) continue;

		// bail if not all matched
		if ( k < qn ) continue;
		// or if all are stop words
		if ( numStopWords == qn ) continue;
		// if query is badly formatted, just skip it then
		if ( ! verifyUtf8(qstr) ) 
			continue;

		// make one
		QueryMatch qm;
		qm.m_queryLogEntry = qe;//(QueryLogEntry *)qptr;
		// add query
		if ( ! m_queryMatchBuf.safeMemcpy ( &qm ,sizeof(QueryMatch)))
			return false;
		// this is gigablast traffic pop, so multiply by 10
		// to simulate google
		// now save pop and string into safebuf
		//qp.m_gigablastTraffic = qpop * GB_TRAFFIC_MODIFIER;
	}
	return true;
}


void State95::sendErrorReply95 ( ) {
	UdpSlot *udpSlot = m_udpSlot;
	State95 *st = (State95 *)this;
	// free it
	mdelete ( st , sizeof(State95) , "st95" );
	delete ( st );
	// sanity check
	if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	g_udpServer.sendErrorReply ( udpSlot , g_errno );
}

//
// THE CORE AORTA of the keyword insertion tool
//

// terms can be user defined or multi-word things like
// "This is an example term I want to insert." but we are limiting it to
// this many words for now. 30.
#define MAXWORDSINTERM 30

// . try inserting "term" everywhere in the document and inlink text and
//   see how it would affect the ranking for each matching query
// . if it changes our rank in the top 50 results, then add a QueryChange
//   instance to the  m_queryChangeBuf detailing the rank changes. i.e. if
//   it moved up or down.
// . consider just inserting right before and after each query term occurence
//   in the doc to see if we can break "requiredScore"
bool State95::addQueryChangesForQuery ( QueryMatch *qm ) {


	// get query log entry for this query
	QueryLogEntry *qe = qm->m_queryLogEntry;

	//if ( g_conf.m_logDebugSEOInserts )
	//	log("seo: inserting terms for query '%s'",qe->getQueryStr());

	// now we store a min top 50 score!
	float required = qe->m_minTop50Score;

	// uncomputed? this should not be. we should at least have
	// done an initial batch run that would only take a few days
	// to compute a rough score estimate of what it takes to be in
	// the top 50 results for this query.
	if ( required == -1.0 ) {
		log("seo: uncomputed top 50 score for query '%s'",
		    qe->getQueryStr());
		return true;
	}

	char *qstr = qe->getQueryStr();

	// debug
	//qstr = "cent";

	// set this since PosdbTable needsit
	Query q;
	// we have to assign a language for synonym expansion to work
	// TODO: put a language in QueryLogEntry!!!
	q.set2 ( qstr , // qe->getQueryStr() ,
		 // crap, we gotta use the same language as the document
		 // because we only store the term freqs for every word
		 // in the doc and each word's synonyms... otherwise
		 // if a query is in spanish, but doc is in english,
		 // then we will find synonyms for a word in the query
		 // for which we supplied no termfreq!!
		 m_req95->m_docLangId,//m_docLang95,
		 //qe->getQueryLangId(), // langEnglish , // langId 
		 true // queryExpansion?
		 );

	long queryHash32 = hash32n ( qe->getQueryStr() );

	// set these in the for loop below
	RdbList rdbLists[MAX_QUERY_TERMS];

	// point to the termlists relevant to the query that were provided
	// in the msg95request in the ptr_posdbLists data.
	// set QueryTerm::m_posdbListPtr i guess and use that in
	// intersectLists10_r()
	setQueryTermTermList ( &q , &m_plistTable , rdbLists );

	char *coll = m_req95->ptr_coll;

	// . construct the term freqs array for these query terms
	// . host #0 must provide these for each insertable term and we use
	//   that to build m_tfTable. Msg95Request::m_termFreqs[] should be
	//   1-1 with the Msg95::m_termIds[] array. and we supply one termid
	//   per word in each insertable term. maybe call them insertable
	//   phrases?
	float termFreqWeights[MAX_QUERY_TERMS];
	for ( long i = 0 ; i < q.m_numTerms ; i++ ) {
		// shortcut
		QueryTerm *qt = &q.m_qterms[i];
		// get the full 64-bit hash of the word
		long long wid = qt->m_rawTermId;
		// look up termid in table. 
		long long tf = *(long long *)m_termFreqTable.getValue(&wid);
		// convert to weight
		termFreqWeights[i] = getTermFreqWeight(tf,m_numDocsInColl);
	}
	// the search parms
	Msg39Request mr;
	mr.m_getSectionStats = false;
	mr.ptr_termFreqWeights = (char *)termFreqWeights;
	mr.size_termFreqWeights = sizeof(float) * q.m_numTerms;
	mr.m_fastIntersection  = 1;
	mr.m_language = qe->getQueryLangId();
	mr.m_getDocIdScoringInfo = false;
	mr.m_seoDebug = m_req95->m_seoDebug;
	// get score as it is now
	PosdbTable posdbTable;
	// initialize it
	posdbTable.init ( &q ,
			  false, // isDebug?
			  NULL, // logState
			  NULL, // top tree
			  coll,
			  NULL, // msg2
			  &mr ); // msg39Request (search parms)
	// . needs this before calling intersect
	// . was called in PosdbTable::allocTopTree but we do not need that
	if ( ! posdbTable.setQueryTermInfo() ) return false;
	// also need this from PosdbTable::allocTopTree()
	if ( m_req95->m_seoDebug >= 2 ) { // g_conf.m_logDebugSEOInserts ) {
		// reset buf ptr, but do not free mem
		// alloc mem if not already there
		posdbTable.m_scoreInfoBuf.reserve ( 5*sizeof(DocIdScore) );
		// # of possible score pairs
		posdbTable.m_pairScoreBuf.reserve ( 50*sizeof(PairScore) );
		// # of possible singles
		posdbTable.m_singleScoreBuf.reserve ( 50*sizeof(SingleScore) );
	}
		
	// debug note
	//if ( g_conf.m_logDebugSEOInserts )
	//	log("seo: executing query to get origscore" );
	// hack set
	posdbTable.m_docIdHack = m_req95->m_docId;
	// execute it
	posdbTable.intersectLists10_r();
	// get the final score
	float origScore = posdbTable.m_finalScore;

	// . the 32-bit termids of the words in query
	// . we could use the Query class for this as well, "q"
	//unsigned long *qtids = qe->m_queryTermIds32;
	//long  ntids = qe->m_qn;

	// set for speedy calls to setPosdbLists()
	m_q = &q;
	m_rdbLists = rdbLists;

	// print original termlists
	//if ( g_conf.m_logDebugSEOInserts ) {
	//	printTermLists(true);
	//	// show scoring info
	//	logScoreInfo ( &posdbTable.m_scoreInfoBuf ,
	//		       &mr,
	//		       origScore);
	//}
	// . store the original DocIdScore member, should only be one
	//   m_scoreInfoBuf since it is just this docid we are scoring
	// . DocIdScore references the m_pairScoreBuf and m_singleScoreBuf
	//   of the posdbTable so let's utilize DocIdScore::serialize()
	if ( m_req95->m_seoDebug >= 2 ) {
		// get it
		DocIdScore *ds;
		ds = (DocIdScore *)posdbTable.m_scoreInfoBuf.getBufStart();
		// sanity! it can be zero if it is a related query i guess
		long blen = posdbTable.m_scoreInfoBuf.length();
		if ( blen > (long)sizeof(DocIdScore)) { char *xx=NULL;*xx=0; }
		// save that
		m_origOffset = m_origScoreInfoBuf.length();
		// . if storing it was oom, return false
		// . serialize the DocIdScore into m_origScoreInfoBuf
		if ( blen && ! ds->serialize ( &m_origScoreInfoBuf ) ) 
			return false;
	}

	// shortcut
	char *qeStart = g_qbuf.getBufStart();

	// advance this along with term
	char *pwi = m_wordInfoBuf.getBufStart();

	long numWordPosInfos = m_req95->size_wordPosInfoBuf;
	numWordPosInfos /= sizeof(WordPosInfo);
	WordPosInfo *wpis = (WordPosInfo *)m_req95->ptr_wordPosInfoBuf;

	//bool called = false;

	long lastGenericOffset1 = -1;
	long lastGenericOffset2 = -2;
	long lastGenericOffset3 = -3;
	long lastGenericSize1 = -1;
	long lastGenericSize2 = -1;
	long lastGenericSize3 = -1;
	
	// scan the insertable terms
	char *termBufStart = m_req95->ptr_insertableTerms;
	char *tstart       = termBufStart;
	char *tend         = termBufStart + m_req95->size_insertableTerms;
	long  termLen;


	// scan the InsertableTerms
	for (char *p = tstart ; p < tend ; ) {
		// cast it
		InsertableTerm *it = (InsertableTerm *)p;
		p += it->getSize();

		char *term = it->getTerm();
		termLen = it->getTermLen();

		// breathe
		QUICKPOLL(m_niceness);

		// get information about the term we are inserting, like
		// how many ALNUM words it contains and those word ids
		WordInfo *wi = (WordInfo *)pwi;
		// skip pwi to next term's WordInfo class
		pwi += wi->getSize();

		// convert the term into list of wordids, usually just one
		//WordInfo *wi = wordInfos[termCount];
		// set for speedy calls to setPosdbLists()
		m_numWordsInTerm = wi->m_numWordIds;
		m_wordIds        = wi->m_wordIds;

		// do not breach
		if ( m_numWordsInTerm > (long)MAXWORDSINTERM )
			m_numWordsInTerm = (long)MAXWORDSINTERM;

		// does this term have words that are in the query?
		bool  hasWordsInQuery = false;
		for ( long i = 0 ; i < q.m_numTerms ; i++ ) {
			for ( long x= 0 ; x < m_numWordsInTerm ; x++ ) {
				// use "m_rawTermId" because "m_termId" has
				// its top bits truncated by TERMID_MASK.
				// also, i don't think m_rawTermId hashes the
				// field, if any, of the query term. careful!
				if ( q.m_qterms[i].m_rawTermId != 
				     m_wordIds[x] ) 
					continue;
				hasWordsInQuery = true;
				break;
			}
		}


		// . evaluate if term in new inlink
		// . sometimes term can be multiple words so be careful
		//for ( long sr = 0 ; sr <= MAX_SITE_RANK ; sr++ )
		//	addQueryChangesForNewInlink ( term , sr );

		//bool onlyDoNewInlinks = false;
		// do not bother evaluating this insertion if there is no
		// way it can improve our score and we are currently not
		// in the top 50. Hopfully this logic kicks in a lot
		// and makes things bearable performance-wise.
		//if ( ! hasWordsInQuery &&
		//     // sometimes term can be multiple words so be careful
		//     m_numWordsInTerm == 1 &&
		//     // and we must not be in the top50 as it is now
		//     origScore < required )
		//	// limit term insertions to new inlinks!!! saves
		//	// a bunch of time!!
		//	onlyDoNewInlinks = true;

		// . what is our perfect score if title is the query exactly?
		// . precompute for each query
		//if ( perfectTitleScore < required ) continue;

		// debug log
		//if ( g_conf.m_logDebugSEOInserts )
		//	log("seo: inserting term=%s for q=%s", term, q.m_orig);

		// get its hash
		long long termHash64 = hash64 ( term , termLen );

		long lastOff = -1;
		long lastSize = -1;
		// is it generic?
		if ( ! hasWordsInQuery &&
		     m_numWordsInTerm == 1 &&
		     lastGenericOffset1 >= 0 ) {
			lastOff = lastGenericOffset1;
			lastSize = lastGenericSize1;
		}
		if ( ! hasWordsInQuery &&
		     m_numWordsInTerm == 2 &&
		     lastGenericOffset2 >= 0 ) {
			lastOff = lastGenericOffset2;
			lastSize = lastGenericSize2;
		}
		if ( ! hasWordsInQuery &&
		     m_numWordsInTerm == 3 &&
		     lastGenericOffset3 >= 0 ) {
			lastOff = lastGenericOffset3;
			lastSize = lastGenericSize3;
		}

		if ( lastOff >= 0 ) {
			// reserve enough space
			if ( ! m_queryChangeBuf.reserve ( lastSize ) )
				return false;
			// point to list of queryChanges we added for last
			// generic term for this query
			char *pp = m_queryChangeBuf.getBufStart();
			pp += lastOff;
			char *ppEnd =m_queryChangeBuf.getBufStart();
			m_queryChangeBuf.safeMemcpy ( pp , lastSize );
			// now update copied querychanges to our termhash
			for ( ; pp < ppEnd ; ) {
				// cast it
				QueryChange *qc = (QueryChange *)pp;
				// skip it
				pp += sizeof(QueryChange);
				// update it
				qc->m_termHash64 = termHash64 ;
			}
			// go to next term now
			continue;
		}

		// save it
		long savedLen = m_queryChangeBuf.length();

		float lastScore = -1.0;

		//static long s_special2 = 0;

		// loop over all word positions to insert term into
		for ( long wi = 0 ; wi < numWordPosInfos ; wi++ ) {

			// get it
			WordPosInfo *wpi = &wpis[wi];

			// breathe
			QUICKPOLL(m_niceness);

			//log("special2 %li",s_special2);
			//s_special2++;

			// . sets QueryTerm::m_posdbListPtr of EVERY query
			//   term even though the rdblist may be empty
			// . make new posdblists from the old lists with
			//   the term inserted into position "wp"
			// . will set QueryTerm::m_posdbListPtr to point to
			//   the new rdblists
			// . uses m_rdbLists[] as the original basis
			// . uses m_newRdbLists[] to hold the new ones
			// . for some reason the query 'cent' on
			//   www.cheatcodes.com has no termlists!
			if ( ! setPosdbLists ( wpi ) ) continue;

			// need to call this one more time to set
			// m_q->m_qterms[i].m_posdbListPtr = &m_newRdbLists[i]
			// because if we don't then it is set to the old
			// stuff from our call above to set origScore and
			// posdbTable.instersectLists10_r() does not
			// see our new lists set by setPosdbLists().
			//
			// NO! must call each time if we are inserting a
			// new term not contained by the doc. for example,
			// for the query 'fine player' for jezebelgallery.com
			// it does not have 'player' but we add that as
			// a "missing term", so we can't have the QueryTermInfo
			// class excluding our list.
			//if ( ! called ) {
			//	called = true;
			if ( ! posdbTable.setQueryTermInfo() ) 
				return false;
			//}

			if ( m_req95->m_seoDebug >= 2 ) {
				posdbTable.m_scoreInfoBuf.reset();
				posdbTable.m_pairScoreBuf.reset();
				posdbTable.m_singleScoreBuf.reset();
			}

			// . now evaluate our new posdb lists with the mods
			// . Posdb will use these lists,not the lists from Msg2
			posdbTable.intersectLists10_r ( );
			// this includes the siterank and same lang modifiers
			float newScore = posdbTable.m_finalScore;

			// if the old score and new score are both outside
			// of the top 50 docids for this query, do not add
			// a QueryChange instance. ignore it.
			if ( newScore < required && origScore < required ) 
				continue;

			// why add a querychange instance if your rank
			// did not change!!??
			if ( newScore == origScore ) 
				continue;

			// if same as last one skip...
			if ( newScore == lastScore )
				continue;

			// debug point
			//char *xx=NULL;*xx=0;

			lastScore = newScore;

			// ok, store it as a QueryChange class
			QueryChange qc;
			// where did we insert this term?
			qc.m_insertPos     = wpi->m_wordPos;
			qc.m_oldScore      = origScore;
			qc.m_newScore      = newScore;
			// offset into g_qbuf buf of QueryLogEntries.
			// but when we send the Msg95Reply back we have
			// to make this relative to Msg95Reply::ptr_queryLogBuf
			// which also is a list of QueryLogEntries.
			qc.m_queryOffset3  = (char *)qe - (char *)qeStart;
			qc.m_termStrOffset = term - termBufStart;
			qc.m_oldRank       = -1; // invalid
			qc.m_newRank       = -1; // invalid
			qc.m_termHash64    = termHash64;
			qc.m_queryHash32   = queryHash32;
			qc.m_debugScoreInfoOffset   = -1;
			qc.m_origScoreInfoOffset = m_origOffset;

			if ( m_req95->m_seoDebug >= 2 &&
			     // i guess related queries might be empty! check
			     posdbTable.m_scoreInfoBuf.length() ) {
				char *bs ;
				bs = posdbTable.m_scoreInfoBuf.getBufStart();
				DocIdScore *ds = (DocIdScore *)bs;
				long current = m_debugScoreInfoBuf.length();
				qc.m_debugScoreInfoOffset = current;
				// store it
				if ( ! ds->serialize(&m_debugScoreInfoBuf) )
					return false;
			}

			//qc.m_hasWordsInQuery = hasWordsInQuery;
			//qc.m_numWordsInTerm  = numWordsInTerm;
			long size = (long)sizeof(QueryChange);
			if ( ! m_queryChangeBuf.safeMemcpy(&qc,size) )
				return false;

			// . store debug scoring info into special buf
			// . that way we can easily see in the tooltip popup
			//   why a particular insertion point gets the score
			//   that it got
			//g_conf.m_logDebugSEOInserts ) {
			/*
			// empty separator line
			log("seo:");
			char *pp ="";
			if ( newScore >  origScore ) pp = " !!";
			if ( newScore == origScore ) pp = " ==";
			log("seo: q=%s ipos=%06li %.03f->%.03f t=%s%s",
			qe->getQueryStr(),
			wpi->m_wordPos,
			origScore,
			newScore,
			term,
			pp);
			// show the termlists that did not tie
			printTermLists(false);
			// show scoring info
			logScoreInfo ( &posdbTable.m_scoreInfoBuf ,
			&mr,
			newScore);
			*/
		}
	
		// save it
		long storedSize = m_queryChangeBuf.length() - savedLen;

		// is it generic?
		if ( hasWordsInQuery ) continue;

		// caching logic
		if ( m_numWordsInTerm == 1 ) {
			lastGenericOffset1 = savedLen;
			lastGenericSize1   = storedSize;
		}
		if ( m_numWordsInTerm == 2 ) {
			lastGenericOffset2 = savedLen;
			lastGenericSize2   = storedSize;
		}
		if ( m_numWordsInTerm == 3 ) {
			lastGenericOffset3 = savedLen;
			lastGenericSize3   = storedSize;
		}

	}
	return true;
}

void State95::printTermLists ( bool orig ) {
	for ( long i = 0 ; i < m_q->m_numTerms ; i++ ) {
		// shortcut
		//QueryTerm *qt = &m_q->m_qterms[i];
		// get the posdblist for that query term
		RdbList *list;
		if ( orig ) {
			list = &m_rdbLists[i];
			log("seo: printing orig termlist #%li",i);
		}
		else {
			list = &m_newRdbLists[i];
			log("seo: printing new termlist #%li",i);
		}
		printTermList ( i,list->m_list,list->m_listSize );
		log("seo: done printing termlist");
	}
}

// . sets QueryTerm::m_posdbListPtr of EVERY query term, even though
//   the list itself may be empty
// . make new posdblists from the old lists with
//   the term inserted into position "wp"
// . will set QueryTerm::m_posdbListPtr to point to
//   the new lists
// . uses rdbLists[] as the original basis
// . make all posdb keys 6 bytes!!!!! and make PosdbTable::
//   intersectLists10_r() use that. since our docid is the same for all keys.
// . i added a hack into Posdb::intersectLists10_r() to use 6 byte keys to
//   getTermPairScoreForNonBody() and getTermPairScoreForAny()
bool State95::setPosdbLists ( WordPosInfo *wpi ) { // long insertPosArg ) {

	// what sentence are we being inserted into?
	//long senti = wpi->m_sentNum;//sentBuf[insertPosArg];
	char inserthg = wpi->m_hashGroup;

	long insertPos  = wpi->m_wordPos;//insertPosArg;
	char insertdr = wpi->m_densityRank;
	insertdr -= m_numWordsInTerm;
	if ( insertdr < 0 ) insertdr = 0;

	// store into temp safebuf
	//char *dst = m_tmpBuf.getBufStart();
	char *dst = m_tmpListData;

	// at least one termlist must have a 12 byte key so if we are
	// inserting our term into an empty list we can copy the
	// docid/siterank/langid 6 bytes
	char *top6 = NULL;
	for ( long i = 0 ; i < m_q->m_numTerms ; i++ ) {
		// shortcut
		//QueryTerm *qt = &m_q->m_qterms[i];
		// get the posdblist for that query term
		char *list = m_rdbLists[i].m_list;
		// skip if not him
		if ( ! list ) continue;
		// wtf?
		if ( m_rdbLists[i].m_listSize < 12 ) { char *xx=NULL;*xx=0; }
		// got one
		top6 = list + 6;
		break;
	}

	if ( ! top6 ) return false;

	// scan query terms. loop over each posdblist.
	for ( long i = 0 ; i < m_q->m_numTerms ; i++ ) {
		// shortcut
		QueryTerm *qt = &m_q->m_qterms[i];
		// get the posdblist for that query term
		RdbList *list = &m_rdbLists[i];
		// assume this query term is not in insertable term
		bool insertTerm = false;
		//long insertPos = 0x7fffffff;
		// bookmark it
		char *listStart = dst;
		//char insertdr = wpi->m_densityRank;
		// does this query term match a word in the inserted term
		for ( long j = 0 ; j < m_numWordsInTerm ; j++ ) {
			// the regular m_termId has some top bits removed
			if ( m_wordIds[j] != qt->m_rawTermId ) continue;
			insertTerm = true;
			break;
		}
		// . if it does then insert the term there into the list!
		// . each list here should be from setPosdbListTable() where
		//   the lists we use here were constructed from
		//   Msg95Request::ptr_posdbTermList which was a bunch of
		//   full 18-byte posdb keys, which we reduced to a 
		//   leading 12 byte key followed by 6 byters. hopefully
		//   PosdbTable::intersectLists10_r() will be ok with that.
		char *p    = list->m_list;
		char *pend = list->m_listEnd;
		for ( ; p < pend ; ) {
			// get word position of this key
			long wpos = g_posdb.getWordPos ( p );
			// get sentence. 
			//long sentNum = sentBuf[wpos];
			bool sameSent = false;
			// hack for now. TODO: create sentBuf from the
			// wordPosInfos to map wordpos to a sent # maybe
			// using a hash table? but really just need to know
			// if same sentence or not. so need a fix for sentences
			// with more than 50 units in them.
			long wdist = wpos - insertPos;
			if ( wdist < 0 ) wdist *= -1;
			if ( wdist < 50 ) sameSent = true;
			// get density rank
			char dr = g_posdb.getDensityRank ( p );
			// insert the new key now?
			if ( wpos >= insertPos && insertTerm ) {
				// only insert once
				insertTerm = false;
				/*
				// debug log
				if ( g_conf.m_logDebugSEOInserts )
					log("seo: insert1 pos=%li dr=%li "
					    "hg=%s "
					    , (long)insertPos
					    , (long)insertdr
					    , getHashGroupString(inserthg)
					    );
				*/
				// . insert our key
				// . only a 48 bit key! (6 bytes)
				g_posdb.makeKey48( dst,
						   insertPos ,
						   insertdr, // density rank
						   MAXDIVERSITYRANK,
						   MAXWORDSPAMRANK,
						   inserthg ,
						   0 , // langid
						   0 , // isSynonym?
						   0 ); // isDelKey?

				// . shit, first key must be 12 bytes now
				// . take it from p i guess
				if ( dst == listStart ) {
					// indicate a 12 byte key
					dst[0] &= 0xf9; // ~0x06
					dst[0] |= 0x02;
					dst += 6;
					memcpy ( dst , top6 , 6 );
					dst += 6;
				}
				else {
					dst += 6;
				}
			}

			// copy it
			memcpy ( dst , p , 6 );

			/*
			// debug
			if ( g_conf.m_logDebugSEOInserts ) {
				char *ts = qt->m_term;
				char *np = &qt->m_term[qt->m_termLen];
				char savednp = *np;
				*np ='\0';
				if ( wpos >= insertPos )
					log("seo: copyingkey term=%s (%li) "
					    "oldwpos=%li newwpos=%li"
					    ,ts,i,wpos,
					    wpos+m_numWordsInTerm*2);
				else
					log("seo: copyingkey term=%s (%li) "
					    "wpos=%li"
					    ,ts,i,wpos);
				*np = savednp;
			}
			*/

			// inc his word position if he is after the insertPos
			if ( wpos >= insertPos ) 
				g_posdb.setWordPos ( dst , 
						     wpos +m_numWordsInTerm*2);
			// same sent as insertable?
			if ( sameSent ) { // sentNum == senti ) {
				dr  = g_posdb.getDensityRank( dst );
				dr -= m_numWordsInTerm;
				if ( dr < 0 ) dr = 0;
				g_posdb.setDensityRank ( dst , dr );
			}


			// are we the first key in the new list?
			if ( dst == listStart ) {
				// skip what we just copied to
				dst += 6;
				// copy the upper 6 bytes of src 12 byte key
				memcpy ( dst , p + 6 , 6 );
				// skip those 6
				dst += 6;
			}
			else {
				// indicate 6 byte key if not first key in dst
				dst[0] &= 0xf9;
				dst[0] |= 04;
				// skip what we copied to
				dst += 6;
			}

			// first key is 12 bytes
			if ( p == list->m_list ) p += 12;
			// skip the 6 bytes we copied from
			else p += 6;
		}

		// after all else?
		if ( insertTerm ) {
			// debug log
			/*
			if ( g_conf.m_logDebugSEOInserts )
				log("seo: insert2 pos=%li dr=%li hg=%s "
				    , (long)insertPos
				    , (long)insertdr
				    , getHashGroupString(inserthg)
				    );
			*/
			// insert our key
			g_posdb.makeKey48 ( dst,
					    insertPos ,
					    insertdr, // density rank
					    MAXDIVERSITYRANK,
					    MAXWORDSPAMRANK,
					    inserthg , // hashgroup 
					    0 , // langid
					    0 , // isSynonym?
					    0 ); // isDelKey?
			// first key? indicate 12 byte key
			if ( dst == listStart ) {
				// indicate a 12 byte key
				dst[0] &= 0xf9; // ~0x06
				dst[0] |= 0x02;
				dst += 6;
				// make a 12 byte key
				memcpy ( dst , top6 , 6 );
				dst += 6;
			}
			else {
				dst += 6;
			}
		}

		// sanity tests
		long listSize = dst - listStart;
		if ( listSize && g_posdb.getKeySize(listStart)!=12) {
			char *xx=NULL;*xx=0; }
		if ( listSize > 12 && g_posdb.getKeySize(listStart+12)!=6) {
			char *xx=NULL;*xx=0; }

		m_q->m_qterms[i].m_posdbListPtr = &m_newRdbLists[i];
		m_newRdbLists[i].m_list = listStart;
		m_newRdbLists[i].m_listSize = listSize;
		m_newRdbLists[i].m_listEnd  = dst;
	}

	// sanity breach check
	if ( dst - m_tmpListData > TMPLISTDATASIZE ) {
		char *xx=NULL;*xx=0; }

	return true;
}



//////////////////////////////
//
// THE QUERY TOP 50 LOOP
//

//
// THIS runs in the background executing queries in g_qbuf in order
// to set QueryLogEntry::m_minTop50Score. it might even perform truncation
// on the termlists to restrict to a small docid range to get a quick
// estimate.
//

// debug
State3g *g_st3g = NULL;

void doneWithQueryLoop ( void *state ) {
	State3g *st3g = (State3g *)state;
	// this is notable
	log("seo: finished processing query log for min top 50 scores");
	// nuke it!
	mdelete ( st3g , sizeof(State3g) , "st3gc" );
	delete ( st3g );
	// save g_qbuf
	g_process.saveBlockingFiles1();
}

void runSEOQueryLoop ( int fd , void *state ) {

	// wait to be synced with host #0 lest we core in 
	// gettimeofdayInMilliseconds()
	if ( ! isClockInSync() ) return;

	// unregister
	g_loop.unregisterSleepCallback ( state , runSEOQueryLoop );

	State3g *st3g;

	// just kick off a state3g to do the whole thing!!
	try { st3g = new (State3g); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("seo: st3g new(%i): %s", 
		    sizeof(State3g),mstrerror(g_errno));
		return;
	}
	mnew ( st3g, sizeof(State3g) , "st3gb");

	// for debug
	g_st3g = st3g;

	// run this when done!
	st3g->m_doneCallback = doneWithQueryLoop;

	st3g->m_coll = "main";
	
	// run it on this set of queries
	if ( ! st3g->processPartialQueries ( ) ) return;

	// how did it complete without blocking?
	doneWithQueryLoop ( st3g );
}

/* 
   this is in synonyms.cpp

// langId is language of the query
long long getSynBaseHash64 ( char *qstr , uint8_t langId ) {
	Words ww;
	ww.set3 ( qstr );
	long nw = ww.getNumWords();
	long long *wids = ww.getWordIds();
	//char **wptrs = ww.getWords();
	//long *wlens = ww.getWordLens();
	long long baseHash64 = 0LL;
	Synonyms syn;
	// assume english if unknown to fix 'pandora's tower'
	// vs 'pandoras tower' where both words are in both
	// english and german so langid is unknown
	if ( langId == langUnknown ) langId = langEnglish;
	// . store re-written query into here then hash that string
	// . this way we can get rid of spaces
	//char rebuf[1024];
	//char *p = rebuf;
	//if ( strstr(qstr,"cheatcodes") )
	//	log("hey");
	// for deduping
	HashTableX dups;
	if ( ! dups.set ( 8,0,1024,NULL,0,false,0,"qhddup") ) return false;
	// scan the words
	for ( long i = 0 ; i < nw ; i++ ) {
		// skip if not alnum
		if ( ! wids[i] ) continue;
		// get its synonyms into tmpBuf
		char tmpBuf[TMPSYNBUFSIZE];
		// . assume niceness of 0 for now
		// . make sure to get all synsets!! ('love' has two synsets)
		long naids = syn.getSynonyms (&ww,i,langId,tmpBuf,0);
		// term freq algo
		//long pop = g_speller.getPhrasePopularity(NULL,
		//					 wids[i],
		//					 true,
		//					 langId);
		// is it a queryStopWord like "the" or "and"?
		bool isQueryStop = ::isQueryStopWord(NULL,0,wids[i]);
		// a more restrictive list
		bool isStop = ::isStopWord(NULL,0,wids[i]);
		if ( ::isCommonQueryWordInEnglish(wids[i]) ) isStop = true;
		// find the smallest one
		unsigned long long min = wids[i];
		//char *minWordPtr = wptrs[i];
		//long  minWordLen = wlens[i];
		// declare up here since we have a goto below
		long j;
		// add to table too
		if ( dups.isInTable ( &min ) ) goto gotdup;
		// add to it
		if ( ! dups.addKey ( &min ) ) return false;
		// now scan the synonyms, they do not include "min" in them
		for ( j = 0 ; j < naids ; j++ ) {
			// get it
			unsigned long long aid64;
			aid64 = (unsigned long long)syn.m_aids[j];
			// if any syn already hashed then skip it and count
			// as a repeated term. we have to do it this way
			// rather than just getting the minimum synonym 
			// word id, because 'love' has two synsets and
			// 'like', a synonym of 'love' only has one synset
			// and they end up having different minimum synonym
			// word ids!!!
			if ( dups.isInTable ( &aid64 ) ) break;
			// add it. this could fail!
			if ( ! dups.addKey ( &aid64 ) ) return false;
			// set it?
			if ( aid64 >= min ) continue;
			// got a new min
			min = aid64;
			//minWordPtr = syn.m_termPtrs[j];
			//minWordLen = syn.m_termLens[j];
			// get largest term freq of all synonyms
			//long pop2 = g_speller.getPhrasePopularity(NULL,aid64,
			//					  true,langId);
			//if ( pop2 > pop ) pop = pop2;
		}
		// early break out means a hit in dups table
		if ( j < naids ) {
		gotdup:
			// do not count as repeat if query stop word
			// because they often repeat
			if ( isQueryStop ) continue;
			// count # of repeated word forms
			//nrwf++;
			continue;
		}
		// hash that now
		// do not include stop words in synbasehash so
		// 'search the web' != 'search web'
		if ( ! isStop ) {
			// no! make it order independent so 'search the web'
			// equals 'web the search' and 'engine search'
			// equals 'search engine'
			//baseHash64 <<= 1LL;
			baseHash64 ^= min;
		}
		// count it, but only if not a query stop word like "and"
		// or "the" or "a". # of unique word forms.
		//if ( ! isQueryStop ) nuwf++;
		// get term freq 
		//if ( pop > maxPop ) maxPop = pop;
		// control word?
		//if ( wids[i] == cw1 ) ncwf++;
	}
	return baseHash64;
}
*/

/*
// returns false and sets g_errno on error
bool setQueryInfo ( char *qstr , QueryInfo *qi ) {
	Words ww;
	ww.set3 ( qstr );
	long nw = ww.getNumWords();
	long long *wids = ww.getWordIds();
	long long exactHash64 = 0LL;
	long long baseHash64 = 0LL;
	long nrwf = 0;
	long nuwf = 0;
	long ncwf = 0;
	long maxPop = 0;
	Synonyms syn;
	// for deduping
	HashTableX dups;
	if ( ! dups.set ( 8,0,1024,NULL,0,false,0,"qhddup") ) return false;
	// language of the query. assume english for now.
	uint8_t langId = langEnglish;
	static long long cw1 = hash64n("http");
	// scan the words
	for ( long i = 0 ; i < nw ; i++ ) {
		// skip if not alnum
		if ( ! wids[i] ) continue;
		// not the best way, but oh well
		exactHash64 <<= 1LL;
		// hash it up
		exactHash64 ^= wids[i];
		// get its synonyms into tmpBuf
		char tmpBuf[TMPSYNBUFSIZE];
		// . assume niceness of 0 for now
		// . make sure to get all synsets!! ('love' has two synsets)
		long naids = syn.getSynonyms (&ww,i,langId,tmpBuf,0);
		// term freq algo
		long pop = g_speller.getPhrasePopularity(NULL,
							 wids[i],
							 true,
							 langId);
		// is it a queryStopWord like "the" or "and"?
		bool isQueryStop = ::isQueryStopWord(NULL,0,wids[i]);
		// a more restrictive list
		bool isStop = ::isStopWord(NULL,0,wids[i]);
		// find the smallest one
		unsigned long long min = wids[i];
		// declare up here since we have a goto below
		long j;
		// add to table too
		if ( dups.isInTable ( &min ) ) goto gotdup;
		// add to it
		if ( ! dups.addKey ( &min ) ) return false;
		// now scan the synonyms, they do not include "min" in them
		for ( j = 0 ; j < naids ; j++ ) {
			// get it
			unsigned long long aid64;
			aid64 = (unsigned long long)syn.m_aids[j];
			// if any syn already hashed then skip it and count
			// as a repeated term. we have to do it this way
			// rather than just getting the minimum synonym 
			// word id, because 'love' has two synsets and
			// 'like', a synonym of 'love' only has one synset
			// and they end up having different minimum synonym
			// word ids!!!
			if ( dups.isInTable ( &aid64 ) ) break;
			// add it. this could fail!
			if ( ! dups.addKey ( &aid64 ) ) return false;
			// set it?
			if ( aid64 < min ) min = aid64;
			// get largest term freq of all synonyms
			long pop2 = g_speller.getPhrasePopularity(NULL,aid64,
								  true,langId);
			if ( pop2 > pop ) pop = pop2;
		}
		// early break out means a hit in dups table
		if ( j < naids ) {
		gotdup:
			// do not count as repeat if query stop word
			// because they often repeat
			if ( isQueryStop ) continue;
			// count # of repeated word forms
			nrwf++;
			continue;
		}
		// hash that now
		// do not include stop words in synbasehash so
		// 'search the web' != 'search web'
		if ( ! isStop ) {
			// no! make it order independent so 'search the web'
			// equals 'web the search' and 'engine search'
			// equals 'search engine'
			//baseHash64 <<= 1LL;
			baseHash64 ^= min;
		}
		// count it, but only if not a query stop word like "and"
		// or "the" or "a". # of unique word forms.
		if ( ! isQueryStop ) nuwf++;
		// get term freq 
		if ( pop > maxPop ) maxPop = pop;
		// control word?
		if ( wids[i] == cw1 ) ncwf++;
	}
	// 0 means error
	if ( exactHash64 == 0 ) exactHash64 = 1LL;
	// normalize into 0 to 1.0 range
	float norm = ((float)maxPop) / ((float)MAX_PHRASE_POP);
	// constraint
	if ( norm <  .5) norm = 0.50;
	if ( norm > 1.0) norm = 1.00;
	// set stuff
	qi->m_smallestNormTermFreq = norm;
	qi->m_querySynBaseHash64   = baseHash64;
	qi->m_queryExactHash64     = exactHash64;
	qi->m_numRepeatWordForms   = nrwf;
	qi->m_numUniqueWordForms   = nuwf;
	// like "http" and "www". typically scores are always high for
	// such words because they are very common in linktext.
	qi->m_numControlWordForms  = ncwf;
	return true;
}
*/

////
//
// NOW WE RENDER THE KIT GUI OURSELVES for performance
//
////

enum TabNum {
	tn_competitorLinks = 1,
	tn_linkSim   ,
	tn_htmlSim   ,
	tn_pageRankSim     ,
	tn_mattCuttsSim     ,
	tn_competingPages   ,
	tn_matchingQueries  ,
	tn_missingTerms,
	tn_relatedQueries   ,
	tn_dupSentences     ,
	tn_pageBacklinks    ,
	tn_siteBacklinks    ,
	tn_rainbowSections
	//tn_traffic 
	//tn_report ,
	//tn_competitorTerms,
	//tn_moreDropDown
};


class SubPage {
public:
	char *m_title;
	long  m_tabNum;
	char *m_path;
	char *m_desc;
};

SubPage subPages[] = {
	/*
	{ "HTML Simulator",
	  tn_htmlSim,
	  "htmlsimulator",
	  "How changing your HTML "
	  "affects your traffic from Google."},
	*/
	{ "Competitor Backlinks",
	  tn_competitorLinks,
	  "competitorbacklinks",
	  "Backlinks your competitors have that you do not. We recommend "
	  "you get these pages to link to you as well."},

	{ "Competitors",
	  tn_competingPages,
	  "competitorpages",
	  "What pages are competitive with your page?"},

	/*
	{ "Competitor Terms",
	  tn_competitorTerms,
	  "competitorterms",
	  "Terms your competitors have in their pages, but you do not."},
	*/

	/*
	// add this in later
	{ "Backlink Simulator",
	  tn_linkSim,
	  "backlinksimulator",
	  "How changing your backlinks "
	  "affects your traffic from Google."},
	*/

	/*
	// add this in later too
	{ "PageRank Simulator",
	  tn_pageRankSim,
	  "pageranksimulator",
	  "How changing your PageRank "
	  "affects your traffic from Google."},
	*/
	
	// Rank Monitor?

	{ "Missing Terms",
	  tn_missingTerms,
	  "missingterms",
	  "What terms are you missing on your page, "
	  "but your competitors have?"},

	
	{ "Matching Queries",
	  tn_matchingQueries,
	  "matchingqueries",
	  "What queries match your page?"},

	{ "Related Queries",
	  tn_relatedQueries,
	  "relatedqueries",
	  "Queries your competitors match, but you do NOT! Consider "
	  "matching them."},
	
	{ "Duplicate Sentences",
	  tn_dupSentences,
	  "duplicatesentences",
	  "What sentences on your page are duplicated on other "
	  "pages?"},

	// the more generic stuff goes under the "More" menu

	/*
	{ "More &raquo;",
	  tn_moreDropDown,
	  "more",
	  ""},
	*/

	{ "Page Backlinks",
	  tn_pageBacklinks,
	  "pagebacklinks",
	  "What PAGES link to your page and what is their value?"},
	
	{ "Site Backlinks",
	  tn_siteBacklinks,
	  "sitebacklinks",
	  "What SITES link to your page and what is their value?"},

	{ "Sections",
	  tn_rainbowSections,
	  "sections",
	  "How Gigablast sees your webpage"}
	
	//{ "Traffic Graph",
	//  tn_traffic,
	//  "trafficgraph",
	//  "What traffic does your page receive?"}
	
	//{ "Report",
	//  tn_report,
	//  "report",
	//  "A cumulative report for your page for printing out."},

	//{ "Matt Cutts Simulator",
	//  tn_mattCuttsSim,
	//  "mattcuttssimulator",
	//  "Get the answers you need form Matt Cutts in real time."}
};


class StateSEO {
public:
	// return html reply on this socket
	TcpSocket *m_socket;
	long long m_selectedTermHash64;
	bool m_htmlSrcOnly;
	class SubPage *m_subPage;
	long m_callbackTimes;
	XmlDoc m_xd;
	SafeBuf m_sb;
	HttpRequest m_hr;
	// print out in xml?
	char m_xml;
};


static void sendSEOPageWrapper ( void *state ) ;

bool printToolTip ( SafeBuf *sb , 
		    WordPosInfo *wpix , 
		    StateSEO *sk ,
		    InsertableTerm *selectedTerm ) ;

bool printToolTipScoresForQuery ( SafeBuf *sb , 
				  StateSEO *sk,
				  QueryChange *qc ,
				  long insertPos ,
				  long qh32 ,
				  WordPosInfo *wpix );

// defined in pageroot.cpp
bool printNav ( SafeBuf &sb , HttpRequest *r ) ;

void printSEOHomepage ( SafeBuf &sb , HttpRequest *r ) {

	sb.safePrintf("<html>\n");
	sb.safePrintf("<head>\n");
	//sb.safePrintf("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\"><meta name=\"description\" content=\"A powerful, new search engine that does real-time indexing!\">\n");
	//sb.safePrintf("<meta name=\"keywords\" content=\"search, search engine, search engines, search the web, fresh index, green search engine, green search, clean search engine, clean search\">\n");
	sb.safePrintf("<title>Gigablast</title>\n");
	sb.safePrintf("<style><!--\n");
	sb.safePrintf("body {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("margin: 20px 5px;\n");
	sb.safePrintf("letter-spacing: 0.04em;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("a:link {color:#00c}\n");
	sb.safePrintf("a:visited {color:#551a8b}\n");
	sb.safePrintf("a:active {color:#f00}\n");
	sb.safePrintf(".bold {font-weight: bold;}\n");
	sb.safePrintf(".bluetable {background:#d1e1ff;margin-bottom:15px;font-size:12px;}\n");
	sb.safePrintf(".url {color:#008000;}\n");
	sb.safePrintf(".cached, .cached a {font-size: 10px;color: #666666;\n");
	sb.safePrintf("}\n");
	sb.safePrintf("table {\n");
	sb.safePrintf("font-family:Arial, Helvetica, sans-serif;\n");
	sb.safePrintf("color: #000000;\n");
	sb.safePrintf("font-size: 12px;\n");
	sb.safePrintf("}\n");
	sb.safePrintf(".directory {font-size: 16px;}\n");
	sb.safePrintf("-->\n");
	sb.safePrintf("</style>\n");
	sb.safePrintf("\n");
	sb.safePrintf("</head>\n");
	sb.safePrintf("<script>\n");
	sb.safePrintf("<!--\n");
	sb.safePrintf("function x(){document.f.q.focus();}\n");
	sb.safePrintf("// --></script>\n");
	sb.safePrintf("<body onload=\"x()\">\n");
	sb.safePrintf("<body>\n");
	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<center><a href=/><img border=0 width=500 height=122 src=http://www.gigablast.com/logo-med.jpg></a>\n");
	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<br><br><br>\n");
	sb.safePrintf("<a href=/>web</a> &nbsp;&nbsp;&nbsp;&nbsp; <b>seo</b> &nbsp;&nbsp;&nbsp;&nbsp; <a href=\"http://www.gigablast.com/?c=dmoz3\">directory</a> &nbsp;&nbsp;&nbsp;&nbsp; \n");
	sb.safePrintf("<a href=/adv.html>advanced search</a>");
	sb.safePrintf(" &nbsp;&nbsp;&nbsp;&nbsp; ");
	sb.safePrintf("<a href=/addurl title=\"Instantly add your url to "
		      "Gigablast's index\">add url</a>");
	sb.safePrintf("\n");
	sb.safePrintf("<br><br>\n");
	sb.safePrintf("<form method=get action=/seo name=f>\n");
	sb.safePrintf("<input size=60 type=text name=u "
		      "style=color:gray; "
		      "value=\"Enter your URL here "
		      "to SEO it\" ");
	sb.safePrintf("onclick=\""
		      // do not re-set it if already black-ified
		      "if (this.style.color=='black') return;"
		      "this.value='http://';"
		      "this.style.color='black';\">");
	sb.safePrintf("&nbsp;<input type=\"submit\" value=\"Submit URL\">\n");
	sb.safePrintf("\n");
	sb.safePrintf("</form>\n");
	sb.safePrintf("<br>\n");
	sb.safePrintf("\n");
	sb.safePrintf("<table cellpadding=3>\n");
	sb.safePrintf("\n");
	/*
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:green;></td>\n");
	sb.safePrintf("<td><font size=+1><b>The Green Search Engine</b></font><br>\n");
	sb.safePrintf("Gigablast is the leading clean-energy search engine. 90%% of its power usage comes from<br>wind energy. Can we afford anything less?<br><br></td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:0040fe;></td>\n");
	sb.safePrintf("<td><font size=+1><b>The Transparent Search Engine</b></font><br>\n");
	sb.safePrintf("Gigablast is the first truly transparent search engine. It tells you exactly why the search<br>results are ranked the way they are. There is nothing left to the imagination.<br><br>\n");
	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	*/
	sb.safePrintf("<tr valign=top>\n");
	sb.safePrintf("<td><div style=width:50px;height:50px;display:inline-block;background-color:f2b629;></td>\n");
	sb.safePrintf("<td><font size=+1><b>SEO with Gigablast</b></font><br>\n");
	sb.safePrintf("<br>");
	sb.brify2("Wouldn't it be great if a search engine told you exactly how to SEO it? Well now it does. Gigablast provides you with all the information you need to rank higher in its results. It's unique set of SEO tools leverage unparalleled insight into the inner workings of web search technology. Because Gigablast uses many similar algorithms to Google that are fundamental in the field of information retrieval, you gain the benefits of a solid <u>scientific approach</u> to SEO. Enter your URL in the search box above to begin.\n",90);

	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("\n");
	sb.safePrintf("\n");
	sb.safePrintf("</table>\n");
	sb.safePrintf("<br><br>\n");
	// defined in PageRoot.cpp
	printNav ( sb , r );
}

// . show queries that match a url
// . returns false if blocked, true otherwise
// . sets g_errno on error
// . alias: sendPageQueries()
bool sendPageSEO ( TcpSocket *s , HttpRequest *r ) {

	HttpRequest *hr = r;

	long xml = r->getLong("xml",0);

	long  ulen;
	char *ustr = hr->getString("u",&ulen,NULL,NULL);

	// can use docid instead of url
	long long d = hr->getLongLong("d",0LL);

	if ( ! ustr && ! d ) {
		SafeBuf sb;
		printSEOHomepage ( sb , hr );
		return g_httpServer.sendDynamicPage(s,
						    sb.getBufStart(),
						    sb.length(),
						    // try 120 not 0
						    120, // cachetime, dnx
						    false, // POST?
						    "text/html", 
						    200,  // httpstatus
						    NULL, // cookie
						    "UTF-8"); // charset
	}

	//
	// send back page frame with the ajax call to get the real
	// search results
	//
	if ( r->getLong("id",0) == 0 && ! xml ) {
		SafeBuf sb;
		sb.safePrintf(
			      "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML "
			      "4.01 Transitional//EN\">\n"
			      //"<meta http-equiv=\"Content-Type\" "
			      //"content=\"text/html; charset=utf-8\">\n"
			      "<html>\n"
			      "<head>\n"
			      "<title>Gigablast SEO Tools</title>\n"
			      "<style><!--"
			      "body {"
			      "font-family:Arial, Helvetica, sans-serif;"
			      "color: #000000;"
			      "font-size: 12px;"
			      //"margin: 20px 5px;"
			      "}"
			      "a:link {color:#00c}"
			      "a:visited {color:#551a8b}"
			      "a:active {color:#f00}"
			      ".bold {font-weight: bold;}"
			      ".bluetable {background:#d1e1ff;"
			      "margin-bottom:15px;font-size:12px;}"
			      ".url {color:#008000;}"
			      ".cached, .cached a {font-size: 10px;"
			      "color: #666666;"
			      "}"
			      "table {"
			      "font-family:Arial, Helvetica, sans-serif;"
			      "color: #000000;"
			      "font-size: 12px;"
			      "}"
			      ".directory {font-size: 16px;}"
			      "-->\n"
			      "</style>\n"
			      "</head>\n"
			      );
		long pageLen;
		char *page = hr->getString("page",&pageLen,"matchingqueries");
		if ( ustr || d ) {
			sb.safePrintf(
				      "<body "
				      "onLoad=\""
				      "var client = new XMLHttpRequest();\n"
				      "client.onreadystatechange = handler;\n"
				      //"var url='http://10.5.1.203:8000/sea
				      "var url='/seo?"
				      );
			unsigned long h32 = 0;
			if ( ustr ) {
				sb.safePrintf("u=");
				sb.urlEncode ( ustr );
				h32 = hash32n(ustr);
			}
			else if ( d ) {
				sb.safePrintf("d=%lli",d);
				h32 = hash32((char *)&d,8);
			}
			// propagate "admin" if set
			long admin = hr->getLong("admin",-1);
			if ( admin != -1 ) sb.safePrintf("&admin=%li",admin);
			// propagate debug
			long debug = hr->getLong("debug",0);
			if ( debug ) sb.safePrintf("&debug=%li",debug);
			long hipos = hr->getLong("hipos",-1);
			if ( hipos != -1 ) sb.safePrintf("&hipos=%li",hipos);
			// turn off the seo cachedb cache? recomputes 
			// missing terms, matching queries, etc.
			long useCache = hr->getLong("usecache",1);
			if ( ! useCache ) sb.safePrintf("&usecache=0");
			// don't forget page!
			sb.safePrintf("&page=%s",page);
			// provide hash of the query so clients can't just 
			// pass in a bogus id to get search results from us
			if ( h32 == 0 ) h32 = 1;
			sb.safePrintf("&id=%lu';\n"
				      "client.open('GET', url );\n"
				      "client.send();\n"
				      "\">\n"
				      , h32
				      );
		}
		else {
			sb.safePrintf("<body>\n");
		}
		// 
		// logo header
		//
		sb.safePrintf(
			      // logo and menu table
			      "<table border=0 cellspacing=5>"
			      //"style=color:blue;>"
			      "<tr>"
			      "<td rowspan=2 valign=top>"
			      "<a href=/>"
			      "<img "
			      "src=http://www.gigablast.com/logo-small.png height=64 width=295>"
			      "</a>"
			      "</td>"

			      "<td>"
			      );
		// menu above search box
		sb.safePrintf(
			      "<br>"

			      " &nbsp; "

			      "<a title=\"Search the web\" href=/>"
			      "web"
			      "</a>"

			      " &nbsp;&nbsp;&nbsp;&nbsp; "

			      "<b title=\"Rank higher in Google\" href=/seo>"
			      "seo"
			      "</b>"

			      " &nbsp;&nbsp;&nbsp;&nbsp; "

			      "<a title=\"Browse the DMOZ directory\" "
			      "href=http://www.gigablast.com/?c=dmoz3>"
			      "directory"
			      "</a>"

			      " &nbsp;&nbsp;&nbsp;&nbsp; "

			      "<a title=\"Advanced web search\" "
			      "href=/adv.html>"
			      "advanced"
			      "</a>"

			      " &nbsp;&nbsp;&nbsp;&nbsp; "

			      "<a title=\"Add a url into Gigablast's index\" "
			      "href=/addurl>"
			      "add url"
			      "</a>"

			      /*
			      " &nbsp;&nbsp;|&nbsp;&nbsp; "

			      "<a title=\"Words from Gigablast\" "
			      "href=/blog.html>"
			      "blog"
			      "</a>"

			      " &nbsp;&nbsp;|&nbsp;&nbsp; "

			      "<a title=\"About Gigablast\" href=/about.html>"
			      "about"
			      "</a>"
			      */

			      "<br><br>"
			       //
			       // search box
			       //
			       "<form name=f method=GET action=/seo>\n\n" 
			       // input table
			       //"<div style=margin-left:5px;margin-right:5px;>
			      );
		// contents of search box
		if ( ulen ) {
			sb.safePrintf("<input size=40 type=text name=u "
				      "value=\"");
			sb.htmlEncode ( ustr , ulen , false );
			sb.safePrintf("\">");
		}
		else {
			sb.safePrintf("<input size=40 type=text name=u "
				      "style=color:gray; "
				      "value=\"Enter your URL here "
				      "to SEO it\" ");
			sb.safePrintf("onclick=\"this.value='http://';"
				      "this.style.color='black';\">");
		}
		sb.safePrintf ("<input type=submit value=\"OK\" border=0>"
			       "</form>\n"
			       "</td>"
			       "</tr>"
			       "</table>\n"
			       );

		//
		// print seo submenu for a given url
		//

		//sb.safePrintf("<table cellpadding=4 cellspacing=10 border=0 "
		//	      "style=font-size:13px;color:blue;>"
		//	      "<tr>"
		//	      );
		sb.safePrintf("<div style=line-height:30px;>");
		long ni = (long)sizeof(subPages)  / (long)sizeof(SubPage);
		// set this
		long myTabNum = -1;
		for ( long i = 0 ; i < ni ; i++ ) {
			SubPage *sp = &subPages[i];
			//char *bg = "";
			char *bc = "color:blue;";
			if ( ! strcmp(page,sp->m_path ) ) {
				// set our tabnum for reference below
				myTabNum = sp->m_tabNum;
				//bg = " bgcolor=green";
				bc = //"style=background-color:green;"
					"text-decoration:underline;"
					"font-weight:bold;"
					"color:black;";
			}
			sb.safePrintf(
				      //"<td%s>"
				       "<a "
				       "style="
				       "text-decoration:none;"
				       "cursor:hand;cursor:pointer;"
				       "%s "
				       "title=\"%s\" "
				       "href=/seo?"
				       //, bg
				       , bc
				       , sp->m_desc
				       );
			// the url encoded
			if ( ustr ) {
				sb.safePrintf("u=");
				sb.urlEncode ( ustr );
			}
			else {
				sb.safePrintf("d=%lli",d);
			}
			// then finish it up
			sb.safePrintf ( "&page=%s>"
					 , sp->m_path
					);
			// replace space with &nbsp;
			char *p = sp->m_title;
			for ( ; *p ; p++ ) {
				if ( *p == ' ' )
					sb.safeStrcpy("&nbsp");
				else
					sb.pushChar ( *p );
			}
			sb.safePrintf("</a>");
			if ( i+1 >= ni ) continue;
			sb.safePrintf( " &nbsp;&nbsp;&nbsp; ");
		}
		//sb.safePrintf("</tr></table>");
		sb.safePrintf("</div><br>");

		//
		// script to populate seo tool results
		//
		sb.safePrintf("<script type=\"text/javascript\">\n"
			      "function handler() {\n" 
			      "if(this.readyState == 4 ) {\n"
			      "document.getElementById('results').innerHTML="
			      "this.responseText;\n"
			      );
		// javascript to scroll to "hipos" anchor tag
		// in the case of rainbowsections
		if ( myTabNum == tn_rainbowSections ) {
			sb.safePrintf("location.hash='#hipos';\n");
		}
		sb.safePrintf(
			      //"alert(this.status+this.statusText+"
			      //"this.responseXML+this.responseText);\n"
			      "}}\n"
			      "</script>\n"
			      // put search results into this div
			      "<div id=results>"
			      );
		if ( ulen || d )
			sb.safePrintf("Waiting for results... This may "
				      "take one to five minutes for some "
				      "of these tools!!");
		sb.safePrintf("</div>\n"

			      "<br>"
			      "<center>"
			      "<font color=gray>"
			      "Copyright &copy; 2013. All Rights Reserved."
			      "</font>"
			      "</center>\n"

			      "</body>\n"
			      "</html>\n"
			      );
		return g_httpServer.sendDynamicPage(s,
						    sb.getBufStart(),
						    sb.length(),
						    0, // cachetime, dnx
						    false, // POST?
						    "text/html", 
						    200,  // httpstatus
						    NULL, // cookie
						    "UTF-8"); // charset
	}





	// get the collection
	long  collLen = 0;
	char *coll  = r->getString ( "c" , &collLen  , NULL );
	if ( ! coll ) coll = "main";
	// get collection rec
	CollectionRec *cr = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("keywords: request from %s failed. "
		    "Collection \"%s\" does not exist.",
		    iptoa(s->m_ip),coll);
		return g_httpServer.sendErrorReply(s,500,
						"collection does not exist");
	}

	// if no url was given just print the page
	long urlLen;
	char *url = r->getString("u",&urlLen);

	//g_pages.printAdminTop ( sb ,
	//			s ,
	//			r ,
	//			NULL ,
	//			NULL );


	// default sub page
	SubPage *sp = NULL;//&subPages[];

	char *tab = r->getString("page",NULL);
	// default to matching queries tab
	if ( ! tab ) tab = "matchingqueries";

	// default to competitor links
	//sk->m_subPage = NULL; // &subPages[0];

	//SubPage *sp = NULL;

 tryagain:

	long ni = (long)sizeof(subPages)  / (long)sizeof(SubPage);
	for ( long i = 0 ; i < ni ; i++ ) {
		SubPage *tmp_sp = &subPages[i];
		if ( strcasecmp ( tab , tmp_sp->m_path ) ) continue;
		//sk->m_subPage = sp;
		sp = tmp_sp;
		break;
	}

	// if still null, bogus tab i guess
	if ( ! sp ) {
		tab = "matchingqueries";
		goto tryagain;
	}


	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;
	// . send this page if we do not have a url to process
	// . encapsulates in html header and tail
	// . make a Mime
	// . i thought we need -2 for cacheTime, but i guess not
	/*
	if ( ! url ) {
		SafeBuf sb;
		printSEOHeader ( &sb , true , NULL , sp );
		// point to it as string
		char *buf    = sb.getBufStart();
		long  bufLen = sb.length();
		return g_httpServer.sendDynamicPage ( s, 
						      buf, 
						      bufLen, 
						      -1 ); // cachetime
	}
	*/

	if ( g_hostdb.m_myHost->m_hostId != 0 ) {
		g_errno = EBADENGINEER;
		return g_httpServer.sendErrorReply(s,g_errno,
						   "can only do this on "
						   "host #0");
	}


	// test url sanity. do not do this if docid given.
	if ( url ) {
		Url ttt;
		ttt.set ( url );
		bool isBad = false;
		if ( ttt.getDomainLen() <= 0 ) isBad = true;
		if ( ! ttt.isHttp() && ! ttt.isHttps() ) isBad = true;
		if ( isBad ) {
			g_errno = EBADURL;
			return g_httpServer.sendErrorReply(s,g_errno,
							   "Malformed Url");
		}
	}

	// make a new state
	StateSEO *sk;
	try { sk = new (StateSEO); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("keywords: new(%i): %s", 
		    sizeof(StateSEO),mstrerror(g_errno));
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( sk, sizeof(StateSEO) , "stkit");

	sk->m_callbackTimes = 0;

	// for returning our http page reply
	//xd->m_hackSocket = s;
	sk->m_socket = s;

	char *termStr = r->getString("term",NULL);
	sk->m_selectedTermHash64 = 0;
	if ( termStr ) sk->m_selectedTermHash64 = hash64n ( termStr );

	long hs = r->getLong("hs",0);
	if ( hs ) sk->m_htmlSrcOnly = true;
	else      sk->m_htmlSrcOnly = false;

	sk->m_xml = r->getLong("xml",0);

	sk->m_subPage = sp;

	sk->m_hr.copy ( hr );

	// shortcut
	XmlDoc *xd = &sk->m_xd;


	xd->m_doingSEO = true;

	// send back "5%10%15%"... in socket BEFORE we send back mime+page
	if ( r->getLong("progressbar",0) )
		xd->m_progressBar = 1;

	// a little hack to divide progress between doing full queries
	// for getting related docids (about 80% of the resources) and then
	// computing the backlinks of those and intersecting (about 20%
	// of the whole process)
	if ( xd->m_progressBar &&
	     sk->m_subPage->m_tabNum == tn_competitorLinks ) 
		xd->m_progressBar = 2;

	// in debug mode we set Msg95Reply::ptr_queryChangeDebugBuf
	// buffer of QueryChangeDebugs which contain additional 
	// termpair score info so we know why we got the score we did for
	// inserting that term into that position for that query. we'll
	// show it on the tooltip when mouseover a circle glyphe in the
	// html src of the page.
	xd->m_seoDebug = r->getLong("debug",0);

	xd->m_readFromCachedb = r->getLong("usecache",1);
	//xd->m_writeToCachedb  = r->getLong("usecache",1);
	// turn off until it works!
	// writing to but not reading from cachedb is bad!
	//xd->m_readFromCachedb = 0;
	xd->m_writeToCachedb  = 1;

	// . store the provided socket
	// . periodically (based on g_now) xmldoc can do a recv(MSG_PEEK)
	//   on that socket to see if client closed connection then can
	//   abort his calcs !
	xd->m_seoSocket = s;

	// fix for weddingdress.publicniches.com
	xd->m_allowSimplifiedRedirs = true;

	// set the url
	xd->setFirstUrl ( url , true , NULL );

	// "main" collection?
	strncpy(xd->m_collBuf,coll,MAX_COLL_LEN);
	xd->m_coll = xd->m_collBuf;
	xd->m_cr   = cr;

	// make sure we flush the msg4 buffers since we query for the url
	// right away almost!
	xd->m_contentInjected = true;

	// -1 means no max
	//xd->m_maxQueries = r->getLong("maxqueries",50);
	// limit to 50 no matter what lest our precious resources vanish
	//if ( xd->m_maxQueries > 50 ) {
	//	log("seopipe: limiting maxqueries of %li to %li",
	//	    xd->m_maxQueries,(long)50);
	//	xd->m_maxQueries = 50;
	//}
	//xd->m_maxRelatedQueries = r->getLong("maxrelatedqueries",-1);
	//xd->m_maxRelatedUrls = r->getLong("maxrelatedurls",-1);

	// . list of \n (url encoded) separated queries to add to our list
	// . we gotta copy since HttpRequest, "r", will go bye-bye on return
	//   of this function.
	// . copy these queries supplied by the user into m_extraQueryBuf
	//   in the exact same format that we get Msg99 replies. that way
	//   we can use our m_queryPtrsBuf safebuf to reference them the same
	//   way those reference the queries in the msg99 replies.
	char *eq = r->getString("extraqueries",NULL);
	if ( eq ) {
		// scan the queries in there
		char *p = eq;
		// loop here for each line
	doNextLine:
		// set nextLine for quick advancing
		char *nextLine = p;
		// skip to next \n or \0
		for ( ; *nextLine && *nextLine != '\n' ; nextLine++ );
		// save that
		char *queryEnd = nextLine;
		// if \n, skip that
		if ( *nextLine == '\n' ) nextLine++;
		// skip white space before traffic number
		for ( ; *p && is_wspace_a(*p) ; p++ );
		// all done?
		if ( ! *p ) goto doneScanningQueries;
		// must be '('
		if ( *p != '(' ) { p = nextLine; goto doNextLine; }
		// skip '('
		p++;
		// then a digit
		long traffic = atoi(p);
		// skip till ')'
		for ( ; *p && *p !=')' ; p++ );
		// must be ')'
		if ( *p != ')' ) { p = nextLine; goto doNextLine; }
		// skip ')'
		p++;
		// skip spaces after the (X) traffic number
		for ( ; *p && is_wspace_a(*p) ; p++ );
		// now that's the query
		char *qstr = p;
		// find end of it
		*queryEnd = '\0';
		// store traffic
		xd->m_extraQueryBuf.pushLong(traffic);
		// fake query score of -1, means a manual query
		xd->m_extraQueryBuf.pushLong(-1);
		// then the query
		xd->m_extraQueryBuf.safeStrcpy(qstr);
		// then the \0
		xd->m_extraQueryBuf.pushChar('\0');
		// do next query
		p = nextLine; goto doNextLine;
	}
	// all done?
 doneScanningQueries:

	//
	// index the doc first (iff not in index)
	//
	// only index it if not indexed yet
	xd->m_newOnly          = true;
	// do not lookup ips of each outlink
	xd->m_spiderLinks      = false;
	xd->m_spiderLinksValid = true;
	// and set the SpiderRequest record which details how to
	// spider it kinda
	long firstIp = 0;
	if ( url ) firstIp = hash32n(url);
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
	//long long d = hr->getLongLong("d",0LL);
	if ( d ) {
		xd->m_docId = d;
		xd->m_docIdValid = true;
		xd->m_firstUrlValid = false;
	}
	else {
		strcpy (xd->m_oldsr.m_url,url);
	}
	xd->m_oldsr.m_firstIp       = firstIp;
	xd->m_oldsr.m_isInjecting   = 1;
	xd->m_oldsr.m_hopCount      = 0;
	xd->m_oldsr.m_hopCountValid = 1;
	xd->m_oldsr.m_fakeFirstIp   = 1;
	xd->m_oldsrValid            = true;
	// other crap from set3() function
	xd->m_version      = TITLEREC_CURRENT_VERSION;
	xd->m_versionValid = true;

	// unless i set the niceness to 1 i cannot connect to the http server
	// when it's sending out the msg9s in parallel in the getQueryRels() 
	// function. seems to really jam the cpu.
	xd->m_niceness     = 1;

	// when done indexing call this
	xd->m_callback1 = sendSEOPageWrapper;
	// and use this state
	xd->m_state = sk;

	// fix core when calling getUrlFilterNum()
	xd->m_priority = 50;
	xd->m_priorityValid = true;

	xd->m_recycleContent = true;
	//xd->m_recycleLinkInfo = true;

	// try to index it
	//if ( ! xd->indexDoc() ) return false;

	//
	// . first load from titlerec
	// . returns false if blocks
	if ( ! xd->loadTitleRecFromDiskOrSpider() ) return false;

	// all done no callback
	sendSEOPageWrapper ( sk );

	return true;
}

int wpiCmp ( const void *a, const void *b ) {
	WordPosInfo *wa = *(WordPosInfo **)a;
	WordPosInfo *wb = *(WordPosInfo **)b;
	if ( (unsigned long)wa->m_wordPtr < (unsigned long)wb->m_wordPtr )
		return -1;
	if ( (unsigned long)wa->m_wordPtr > (unsigned long)wb->m_wordPtr )
		return  1;
	return 0;
}

int itCmp ( const void *a, const void *b ) {
	InsertableTerm *wa = *(InsertableTerm **)a;
	InsertableTerm *wb = *(InsertableTerm **)b;
	return wb->m_bestTrafficGain - wa->m_bestTrafficGain;
}

static void printCompetitorBacklinks ( void *stk ) ;
static void printHTMLSimulator   ( void *stk ) ;
static void printMatchingQueries ( void *stk ) ;
static void printRelatedQueries ( void *stk ) ;
static void printMissingTerms ( void *stk ) ;
//static void printCompetitorTerms    ( void *stk ) ;
static void printCompetitorPages  ( void *stk ) ;
static void printDupSentences    ( void *stk ) ;
//static void printLinkSim         ( void *stk ) ;
//static void printPageRankSim     ( void *stk ) ;
static void printDupSentences    ( void *stk ) ;
static void printPageBacklinks   ( void *stk ) ;
static void printSiteBacklinks   ( void *stk ) ;
static void printRainbowSections ( void *stk ) ;
//static void printTraffic         ( void *stk ) ;
//static void printMattCuttsSim    ( void *stk ) ;

// . we come here after calling xd->indexDoc()
// . print out the page
void sendSEOPageWrapper ( void *state ) {

	StateSEO *sk = (StateSEO *)state;

	XmlDoc *xd = &sk->m_xd;

	// reset these to be set by entry function we pick below:
	// getRelatedQueryBuf() etc.
	xd->m_masterLoop = NULL;
	xd->m_masterState = NULL;

	TcpSocket *sock = sk->m_socket;

	//bool printHeader = true;
	//if ( sk->m_htmlSrcOnly ) printHeader = false;
	//if ( sk->m_callbackTimes ) printHeader = false;
	//if ( sk->m_xml ) printHeader = false;
	
	// in case we blocked in some call below like
	// for printPageBacklinks() call to XmlDoc::getAllInlinks()
	// which might block
	sk->m_callbackTimes++;


	//long niceness = xd->m_niceness;

	// error getting seo info?
	if ( g_errno ) {
	hadError:
		log("seopipe: return http error page: %s",mstrerror(g_errno));
		mdelete ( sk , sizeof(StateSEO) , "stkit" );
		delete (sk);
		g_httpServer.sendErrorReply(sock,500,mstrerror(g_errno));
		return;
	}

	// otherwise print the html page data into "sb" then send that back
	//SafeBuf *sb = &sk->m_sb;

	//if ( printHeader && ! sb->reserve ( 50000 ) )
	//	goto hadError;

	// assume no error
	g_errno = 0;

	// print the header. logo, menu bar, etc.
	//if ( printHeader )
	//	printSEOHeader ( sb , false , xd->m_firstUrl.m_url , 
	//			 sk->m_subPage );



	//bool status;

	//if ( xd->getSEOQueryInfo() == (void *)-1 ) return false;

	// print recommended inlinks?
	if ( sk->m_subPage->m_tabNum == tn_competitorLinks )
		printCompetitorBacklinks ( sk );
	// print the keyword insertion tool (KIT)
	else if ( sk->m_subPage->m_tabNum == tn_htmlSim )
		printHTMLSimulator ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_matchingQueries )
		printMatchingQueries ( sk );
	//else if ( sk->m_subPage->m_tabNum == tn_competitorTerms )
	//	printCompetitorTerms ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_relatedQueries )
		printRelatedQueries ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_missingTerms )
		printMissingTerms ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_competingPages )
		printCompetitorPages ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_dupSentences )
		printDupSentences ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_pageBacklinks )
		printPageBacklinks ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_siteBacklinks )
		printSiteBacklinks ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_rainbowSections )
		printRainbowSections ( sk );
	/*
	else if ( sk->m_subPage->m_tabNum == tn_traffic )
		printTraffic ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_linkSim )
		printLinkSim ( sk );
	else if ( sk->m_subPage->m_tabNum == tn_pageRankSim )
		printPageRankSim ( sk );
	*/
	// must be there!
	else { 
		g_errno = EBADENGINEER;
		goto hadError;
	}

	// blocked?
	//if ( ! status ) return;

	// sanity
	//if ( ! status && ! g_errno ) { char *xx=NULL;*xx=0; }

	// error generating page?
	//if ( ! status ) goto hadError;

	// do not allow xmldoc to destroy it!
	//xd->m_seoSocket = NULL;
}

void sendPageBack ( StateSEO *sk ) {

	SafeBuf *sb = &sk->m_sb;

	if ( g_errno && sk->m_xml ) {
		sb->setLength(0);
		sb->safePrintf( "<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<error>%li</error>\n"
				"\t<errMsg><![CDATA[%s]]></errMsg>\n"
				"</response>\n"
				,(long)g_errno
				,mstrerror(g_errno));
	}
	else if ( sk->m_xml )
		sb->safePrintf("</response>\n");
	else if ( ! sk->m_htmlSrcOnly )
		sb->safePrintf("</body></html>");

	TcpSocket *sock = sk->m_socket;

	// validate html. scan for \0's
	char *p = sb->getBufStart();
	char *pend = sb->getBuf() ;
	for ( ; p < pend ; p++ ) 
		// change \0's in the html to spaces i guess
		if ( !*p ) *p = ' '; 
	// make sure we end on a \0
	if ( *pend != '\0' ) { 
		sb->pushChar('\0');
		sb->incrementLength(-1);
	}

	log("seo: sending back page of %li bytes (sd=%li)",sb->length() ,
	    (long)sock);

	g_httpServer.sendDynamicPage (sock,sb->getBufStart(),sb->length());

	//log("seo: deleted xd!!!");

	// nuke it
	mdelete ( sk , sizeof(StateSEO) , "stkit" );
	delete (sk);
}

static int rlCmp ( const void *a, const void *b ) {
	RecommendedLink *wa = *(RecommendedLink **)a;
	RecommendedLink *wb = *(RecommendedLink **)b;
	long diff = wb->m_votes - wa->m_votes;
	if ( diff ) return diff;
	if ( wb->m_totalRecommendedScore > wa->m_totalRecommendedScore )
		return 1;
	if ( wb->m_totalRecommendedScore < wa->m_totalRecommendedScore )
		return -1;
	// docid to break all ties
	if ( wb->m_rl_docId > wa->m_rl_docId )
		return  1;
	if ( wb->m_rl_docId < wa->m_rl_docId )
		return -1;

	return 0;
}

// returns false and sets g_errno on error
void printCompetitorBacklinks ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;
	xd->m_masterState = sk;
	xd->m_masterLoop  = printCompetitorBacklinks;

	// now get recommended links (link juice)
	SafeBuf *kbuf = xd->getRecommendedLinksBuf();
	if ( ! kbuf ) { sendPageBack(sk); return; }
	if ( kbuf == (void *)-1 ) return;

	// make ptrs to them
	char *p = kbuf->getBufStart();
	char *pend = kbuf->getBuf();
	SafeBuf ptrBuf;
	if ( ! ptrBuf.reserve ( 300 * 4 ) ) return;
	for ( ; p < pend ; ) {
		// cast it
		RecommendedLink *ri = (RecommendedLink *)p;
		// stow it
		ptrBuf.pushLong ( (long) ri );
		// skip it
		p += ri->getSize();
	}
	// sort ptrs
	RecommendedLink **ptrs = (RecommendedLink **)ptrBuf.getBufStart();
	long numPtrs = ptrBuf.length() / 4;
	// sort those
	long niceness = xd->m_niceness;
	gbqsort ( ptrs,
		  numPtrs ,
		  sizeof(RecommendedLink *),
		  rlCmp,
		  niceness );


	if ( ! sk->m_xml ) {
		sb->safePrintf("<div "
			       "style=\""
			       "border:2px solid;"
			       "margin-left:5px;"
			       "margin-right:5px;"
			       "overflow-x:scroll;"
			       "\">");
		
		sb->safePrintf("<table width=100%% cellspacing=0 cellpadding=5"
			       //"rules=none border=2 frame=box "
			       //"style="
			       //"border-style:solid;"
			       ">"
			       "<tr>"
			       "<td></td>"
			       "<td><b>Competitor Backlink</b></td>"
			       "<td><b>IP</b></td>"
			       "<td><b>Title</b></td>"
			       "<td><b>Score</b></td>"
			       "<td><b>Votes</b></td>"
			       "<td><b>PageRank~</b></td>"
			       "</tr>\n"
			       );
	}


	if ( sk->m_xml ) {
		sb->safePrintf (
				"<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<error>0</error>\n"
				);
	}

	// print each linkjuice link
	//char *p = kbuf->getBufStart();
	//char *pend = kbuf->getBuf();
	long maxRows = 200;
	long rowCount = 0;
	for ( long i = 0 ; i < numPtrs ; i++ ) {
		// only print top 100
		if ( maxRows-- <= 0 ) break;
		// cast it
		RecommendedLink *ri = ptrs[i];//(RecommendedLink *)p;
		// skip it

		char *url = ri->getUrl();

		//p += ri->getSize();
		if ( ! sk->m_xml ) {
			sb->safePrintf("<tr valign=top>"
				       "<td>%li</td>"
				       "<td>"
				       "<a href=%s>"
				       ,++rowCount 
				       ,url
				       );
			// print the url, print ellipsis if truncated
			sb->safeTruncateEllipsis(url,60);
			sb->safePrintf("</a>");
		}
		else {
			sb->safePrintf("\t<competitor>\n"
				       "\t\t<url>"
				       "<![CDATA[%s]]>"
				       "</url>\n"
				       "\t\t<title>"
				       "<![CDATA[%s]]>"
				       "</title>\n"
				       "\t\t<votes>%li</votes>\n"
				       "\t\t<siteRank>%li</siteRank>\n"
				       "\t\t<score>%f</score>\n"
				       ,url
				       ,ri->getTitle()
				       ,ri->m_votes
				       ,(long)ri->m_rl_siteRank
				       ,ri->m_totalRecommendedScore
				       );
		}


		char *rdStart = xd->m_relatedDocIdBuf.getBufStart();
		bool firstOne = true;
		for ( long k = 0 ; k < 10 ; k++ ) {
			long rdOff = ri->m_relatedDocIdOff[k];
			if ( rdOff == -1 ) break;
			// cast it
			RelatedDocId *rd = (RelatedDocId *)(rdStart + rdOff );

			char *rurl = rd->getUrl(&xd->m_relatedTitleBuf);
			
			if ( sk->m_xml ) {
				sb->safePrintf("\t\t<linksTo>"
					       "<![CDATA[%s]]>"
					       "</linksTo>\n"
					       ,rurl);
				continue;
			}

			// first rd?
			if ( firstOne ) {
				firstOne = false;
				sb->safePrintf("<br><font size=-2>");
			}
			else {
				sb->safePrintf("<br>");
			}
			// print the related docid that had this as an inlink
			sb->safePrintf(" &nbsp; because it links to "
				      "your competitor "
				      "<i>%s"
				      ,rurl
				       );
			// show the "first ip" of competitor if in debug mode
			if ( xd->m_seoDebug )
				sb->safePrintf( " (%s)"
						,iptoa(rd->m_relatedFirstIp)
						);
			sb->safePrintf("</i>");
		}
		if ( sk->m_xml ) {
			sb->safePrintf("\t</competitor>\n");
			continue;
		}
		if ( ! firstOne )
			sb->safePrintf("</font>");
		// print it out
		sb->safePrintf("</td>"
			       "<td>%s</td>"
			       "<td>"
			       ,iptoa(ri->m_rl_firstIp));
		// print the title, print ellipsis if truncated
		sb->safeTruncateEllipsis(ri->getTitle(),50);
		sb->safePrintf("</td>"
			       "<td>%.04f</td>"
			       "<td>%li</td>"
			       "<td>%li</td>"
			       "</tr>\n"
			       ,ri->m_totalRecommendedScore
			       ,ri->m_votes
			       ,(long)ri->m_rl_siteRank
			       );
	}

	if ( ! sk->m_xml ) {
		sb->safePrintf("</table>\n");
		sb->safePrintf("</div>\n");
	}

	sendPageBack ( sk );
}

// returns false and sets g_errno on error
void printHTMLSimulator ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printHTMLSimulator;

	// print the matching queries first
	SafeBuf *qkbuf = xd->getMatchingQueryBuf();
	if ( ! qkbuf ) { sendPageBack(sk); return; }
	if ( qkbuf == (void *)-1 ) return;

	// scan each term
	SafeBuf *itBuf = xd->getScoredInsertableTerms();
	if ( ! itBuf ) { sendPageBack(sk); return; }
	if ( itBuf == (void *)-1 ) return;

	SafeBuf *sb = &sk->m_sb;

	long niceness = xd->m_niceness;


	/*

	  turn this off for now...

	// . how many queries do we have that match this url?
	// . they should be sorted by our url's score
	long numQueryPtrs = qpbuf->length() / sizeof(Msg99Reply *);
	// cast the msg99 reply ptrs, i.e. query ptrs
	Msg99Reply **queryPtrs = (Msg99Reply **)qpbuf->getBufStart();
	// print matching queries div
	sb->safePrintf("<div style=\""
		      "margin-left:5px;"
		      "margin-right:5px;"
		      "max-height:100px;"
		      "overflow-x:auto;" // auto;"
		      "overflow-y:scroll;"
		      //"width:95%%;" // scrollbar not included!
		      "padding:5px;"
		      "border:2px solid;"
		      "\">\n"
		      "<table width=100%% style=font-size:11px;>\n"
		      "<tr>"
		      "<td><b>Query</b></td>"
		      "<td><b>Traffic Change</b></td>"
		      "<td><b>Total Monthly Traffic</b></td>"
		      "<td><b>Relevance</b></td>"
		      "<td><b>Old Rank</b></td>"
		      "<td><b>New Rank</b></td>"
		      "<td><b>Old Traffic</b></td>"
		      "<td><b>New Traffic</b></td>"
		      "</tr>\n"
		      );
	
	// use ajax to request the next 20 on a mouse event
	long max = numQueryPtrs;
	if ( max > 200 ) max = 200;
	for ( long i = 0 ; i < max ; i++ ) {
		// shortcut
		Msg99Reply *qp = queryPtrs[i];
		// sometimes queries like 'gallery-view' are 
		// hard-phrased and do not show up for us, so skip.
		// they should be at the very end so we should be
		// trimming the tail for them, so don't worry about
		// <queryNum> having holes in it.
		if ( qp->m_myDocId == 0LL && qp->m_myScore == 0.0 )
			continue;
		// see XmlDoc::getSEOQueryInfo() for more things to print
		sb->safePrintf("<tr>"
			      "<td>%s</td>"
			      "<td>--</td>"
			      "<td>%li</td>"
			      "<td>%.0f</td>"
			      "<td>--</td>"
			      "<td>--</td>"
			      "<td>--</td>"
			      "<td>--</td>"
			      "</tr>\n"
			      , qp->m_queryStr
			      // already has  * GB_TRAFFIC_MODIFIER in it
			      , qp->m_gigablastTraffic
			      //, qp->m_gigablastTraffic * 10
			      , qp->m_queryInfo.m_queryImportance
			      );
	}
	sb->safePrintf("</table>\n"
		      "</div>\n" );

	sb->safePrintf("<br>");
	//sb->safePrintf("<br>");
	*/




	// cast it
	//InsertableTerm *its = (InsertableTerm *)itBuf->getBufStart();
	// how many terms do we have?
	//long nits = itBuf->length() / sizeof(InsertableTerm);


	InsertableTerm *selectedTerm = NULL;

	// use first term if none selected
	//if ( nits ) selectedTerm = &its[0];
	// . first set WordPosInfo::m_trafficGain based on the selected
	//   insertable term.
	// . get the selected insertable term. 
	// . identify it by term hash 64
	char *pend = itBuf->getBuf();

	long numInsertableTerms = 0;

	char *p = itBuf->getBufStart();
	for ( ; p < pend ; ) {
		// shortcut
		InsertableTerm *it = (InsertableTerm *)p;
		p += it->getSize();
		// count them
		numInsertableTerms++;
		// is it the selected one?
		if ( it->m_termHash64 == sk->m_selectedTermHash64 )
			// mark it
			selectedTerm = it;
	}

	// some handy javascript
	sb->safePrintf("<script>\n");

	if ( selectedTerm )
		// name of the insertable term <tr> tag which is
		// just the hash of the insertable term
		sb->safePrintf("var last='%lu';\n", 
			      // just use the last 32 bits
			      (unsigned long)selectedTerm->m_termHash64 );
	else
		sb->safePrintf("var last;\n");

	sb->safePrintf("function sss( me ) {\n"

		      "me.style.backgroundColor='green';\n"
		      "me.style.color='white';\n"
		      // this is the following <td> which is blue
		      "me.firstChild.style.color='white';\n"

		      // get last guy so we can revert his bgcolor
		      "var obj=document.getElementById(last);\n"
		      "if (obj) {\n"
		      "obj.style.backgroundColor='white';\n"
		      "obj.style.color='black';\n"
		      "obj.firstChild.style.color='blue';\n"
		      "}\n"

		      // we are the last guy now
		      "last=me.id;\n"
		      // put spinning gears in html div
		      "document.getElementById('htmlsrc').innerHTML="
		      "'<img width=50 height=50 src=http://www.gigablast.com/gears.gif>';\n"
		      // . ajax to update html src div
		      // . we cached our WordPosInfos and QueryChanges
		      //   and InsertableTerms
		      "var client = new XMLHttpRequest();\n"
		      "client.onreadystatechange = handler2;\n" 
		      "client.open(\"GET\", "
		      // 'hs=1' indicates we only want the html source
		      // pass back the contents, the term as input
		      "'/seo?hs=1&page=htmlsimulator&u=%s&term='+me.firstChild.innerHTML);\n"
		      "client.send();\n"
		      "}\n"
		      , xd->m_firstUrl.m_url 
		      );

	// when that url replies, put escaped html src into here
	sb->safePrintf ("function handler2() {\n"
		      "if(this.readyState != 4 ) return;\n"
			"if(this.status != 200 || this.responseText==\"\"|| ! this.responseText ) {\n"
			"document.getElementById('htmlsrc').innerHTML="
			"\"error! status=\"+this.status+\"<br><br>"
			"Please RELOAD this page.\";\n"
			"return;\n"
			"}\n"

		      "this.responseText;\n"
		      "document.getElementById('htmlsrc').innerHTML="
		      "this.responseText;\n"
		       "}\n"
		      );
		      

	sb->safePrintf ("</script>\n");


	//////////////
	//
	// this table holds the insertable terms on the left and the html
	// src on the right
	//
	//////////////
	sb->safePrintf("<table cellspacing=0 cellpadding=0 "
		      "style=\""
		      "margin-left:5px;"
		      "margin-right:5px;"
		      "\">"
		      "<tr><td rowspan=2>" );

	// a new div to hold insertable terms
	sb->safePrintf("<div "
		      "style=\""
		      "width:400px;"
		      "max-width:400px;"
		      "min-width:400px;"//33%%;"
		      "max-height:400px;"
		      "overflow-x:auto;" // auto;"
		      "overflow-y:scroll;"
		      "padding:5px;"
		      "border:2px solid;"
		      "\">\n"
		      "<table "
		      "cellspacing=0 "
		      "cellpadding=4 "
		      "width=100%% style=font-size:11px;>\n"
		      "<tr>"
		      "<td><b>Term to Insert</b></td>"
		       //"<td><b>Relevance</b></td>"
		      //"<td><b>Is Related?</b></td>"
		       "<td><b>Max Traffic Gain</b></td>"
		      "</tr>\n"
		      );

	// sort them
	SafeBuf iptrBuf;
	if ( ! iptrBuf.reserve ( numInsertableTerms * 4 ) ) {
		sendPageBack ( sk );
		return;
	}

	p = itBuf->getBufStart();
	for ( ; p < pend ; ) {
		// shortcut
		InsertableTerm *it = (InsertableTerm *)p;
		p += it->getSize();
		// store the ptr into here
		iptrBuf.pushLong( (long)it);
	}
	// sort those
	gbqsort ( iptrBuf.getBufStart() ,
		  numInsertableTerms ,
		  sizeof(InsertableTerm *),
		  itCmp,
		  niceness );
	InsertableTerm **sorted = (InsertableTerm **)iptrBuf.getBufStart();

	// scan them
	for ( long i = 0 ; i < numInsertableTerms ; i++ ) {
		// get the ith insertable term
		//InsertableTerm *it = &its[i];
		InsertableTerm *it = sorted[i];
		// the string
		sb->safePrintf("<tr id=%lu onclick=\"sss(this)\" "
			      // just use the lower 32 bits to save mem
			      , (unsigned long)it->m_termHash64
			      );
		if ( it == selectedTerm )
			sb->safePrintf(" bgcolor=green style=color:white;"
				      "cursor:hand;cursor:pointer;>"
				      "<td>"
				      );
		else
			sb->safePrintf("><td style=color:blue;"
				      "cursor:hand;cursor:pointer;>" );
		// print term
		sb->safeMemcpy ( it->getTerm() , it->getTermLen() );
		// close up td
		sb->safePrintf("</td>\n" );
		// relavance:
		// sum of traffic of all queries containing this term
		//sb->safePrintf("<td>%li</td>",
		//		 it->m_trafficSum);
		// is it contained in the doc/linktext or is it "related"
		//char *ir = "N";
		//if ( it->m_isRelatedTerm ) ir = "Y";
		//sb->safePrintf("<td>%s</td>",ir);

		/*
		// get the first query change if any
		QueryChange *qc = it->m_firstQueryChange;
		// skip if no list
		if ( ! qc ) goto skip;
		// print the insert position that gives us the most traffic
		sb->safePrintf("\t\t\t<bestInsertPosition>%li"
				 "</bestInsertPosition>\n",
				 it->m_bestInsertPos);
		*/
		sb->safePrintf("<td>%li</td>"
				"</tr>",
				 it->m_bestTrafficGain);
	}
	// end table and div
	sb->safePrintf("</table>"
		      "</div>\n" );

	sb->safePrintf("</td><td>&nbsp;&nbsp;&nbsp;&nbsp;</td>"
		      "<td width=100%%>");

	// div of URLs
	/*
	sb->safePrintf("<div style=\""
		      //"width:400px;"
		      //"max-width:400px;"
		      "min-width:400px;"
		      "height:100px;"
		      "max-height:100px;"
		      "min-height:100px;"
		      "overflow-x:auto;"
		      "overflow-y:scroll;"
		      "padding:5px;"
		      "border:2px solid;"
		      "\">"

		      // table of url and backlinks to it with siteranks
		      "<table cellspacing=5 style=font-size:11px;>"
		      "<tr>"
		      "<td><b>URL or Backlink</b></td>"
		      "<td><b>Enabled</b></td>"
		      "<td><b><nobr>Site Rank</nobr></b></td>"
		      "</tr>"
		      "<tr>"
		      "<td width=100%% bgcolor=lightgray>%s</td>"
		      "<td><input type=checkbox checked></td>"
		      "<td>%li</td>"
		      "</tr>"
		      "</table>"

		      "</div>"

		      "</td>"
		      "</tr>"
		      "<tr>"
		      // next table cell is in our same column because of
		      // the rowspan=2 above...
		      "<td>&nbsp;</td>"
		      "<td>"
		      , xd->m_firstUrl.m_url 
		      , (long)xd->getSiteRank()
		      );
	*/

	// div of HTML SRC
	sb->safePrintf("<div id=htmlsrc style=\""
		      //"width:400px;"
		      //"max-width:400px;"
		      "min-width:400px;"
		      //"height:275px;"
		      //"max-height:275px;"
		      //"min-height:275px;"
		      "height:400px;"
		      "max-height:400px;"
		      "min-height:400px;"
		      "overflow-x:auto;"
		      "overflow-y:scroll;"
		      "font-size:11px;"
		      "padding:5px;"
		      "border:2px solid;"
		      //"margin-top:10px;"
		      "\">\n"
		      );


	// reset?
	if ( sk->m_htmlSrcOnly )
		sb->setLength(0);

	// this has WordPosInfo::m_wordPtr/m_wordLen and is sorted by
	// m_wordPos, but not m_wordPtr!
	SafeBuf *wpib = xd->getWordPosInfoBuf();
	WordPosInfo *wpis = (WordPosInfo *)wpib->getBufStart();
	// make ptrs to these WordPosInfos so we can sort by m_wordPtr
	long nwpi = wpib->length() / sizeof(WordPosInfo);


	// set the WordPosInfo::m_trafficGain for all our WordPosInfos
	// for the selected InsertableTerm, "selectedTerm".
	xd->setWordPosInfosTrafficGain ( selectedTerm );

	// the worst loss
	long minTrafficGain = 0;
	for ( long j = 0 ; j < nwpi ; j++ ) {
		// skip if not the best scoring position
		if ( wpis[j].m_trafficGain >= minTrafficGain )
			continue;
		// we got a new winner
		minTrafficGain = wpis[j].m_trafficGain;
	}

	// re-compute the m_bestTrafficGain?
	long maxTrafficGain = 0;
	for ( long j = 0 ; j < nwpi ; j++ ) {
		// skip if not the best scoring position
		if ( wpis[j].m_trafficGain < maxTrafficGain )
			continue;
		// we got a new winner
		maxTrafficGain = wpis[j].m_trafficGain;
	}


	//
	// TODO
	//
	// set WordPosInfo::m_color as a 24-bit color to be redder based
	// on the relative traffic gains...
	//long maxGain = 0;
	WordPosInfo **ppp = NULL;
	SafeBuf pb;
	if ( selectedTerm ) {

		//maxGain = selectedTerm->m_bestTrafficGain;
		for ( long i = 0 ; i < nwpi ; i++ ) {
			WordPosInfo *wp = &wpis[i];
			// assign a number,
			// -4,-3,-2,-1,0,1,2,3,4
			// based on relative traffic gain or loss
			long gain = wp->m_trafficGain;
			if ( gain == 0 ) {
				wp->m_color = 0;
				continue;
			}
			if ( gain < 0 ) {
				wp->m_color = (4 * gain) / minTrafficGain;
				// the negatives will make it positive, so
				// put it back to negative
				wp->m_color *= -1;
				// at least make it a 1/4 circle
				if ( wp->m_color == 0 ) wp->m_color = -1;
				continue;
			}
			else {
				wp->m_color = (4 * gain) / maxTrafficGain;
				// at least make it a 1/4 circle
				if ( wp->m_color == 0 ) wp->m_color = 1;
				continue;
			}
			// shade of red, max it out if "max" is 0
			//wp->m_color = 0xff;
			//if ( ! maxGain ) continue;
			//float ratio ;
			//ratio = ((float)wp->m_trafficGain / (float)maxGain);
			//wp->m_color = (unsigned char)((long)(255 * ratio));
		}

		if ( ! pb.reserve ( nwpi * 4 ) ) {
			sendPageBack ( sk );
			return;
		}
		for ( long i = 0 ; i < nwpi ; i++ ) {
			WordPosInfo *wp = &wpis[i];
			pb.pushLong((long)wp);
		}
		// sort them by WordPosInfo::m_wordPtr
		gbqsort ( pb.getBufStart() ,
			  nwpi ,
			  sizeof(WordPosInfo *),
			  wpiCmp,
			  niceness );
		// set this now
		ppp = (WordPosInfo **)pb.getBufStart();
	}

	// html source, escaped
	if ( xd->size_utf8Content <= 1 ) 
		sb->safePrintf("<i>No content</i>");

	// reserve space
	if ( ! sb->reserve ( xd->size_utf8Content * 2 + 30 ) ) {
		sendPageBack ( sk );
		return;
	}

	// scan wordposinfos and get the min and max traffic gain
	//float maxLoss = 0.0;
	//float maxGain = 0.0;
	


	// print one byte at a time
	p = xd->ptr_utf8Content;
	pend = p + xd->size_utf8Content;
	long nextwpi = 0;
	//char *delim = " <font color=red>&bull;</font> ";
	//long dlen = gbstrlen(delim);
	for ( ; p < pend ; p++ ) {
		// skip?
		if ( ! selectedTerm ) goto skipJunk;
		// catch up
		for(;nextwpi < nwpi && ppp[nextwpi]->m_wordPtr < p;) {
			// debug
			//if ( ppp[nextwpi]->m_wordPos==1169)
			//	log("poo");
			nextwpi++;
		}
		// debug
		//if ( ppp[nextwpi]->m_wordPos==1169)
		//	log("poo2");
		// print a circle at each insertable position in
		// the html source
		if ( nextwpi < nwpi && p == ppp[nextwpi]->m_wordPtr ) {
			// make it redder if traffic gain is higher
			//sb->safeMemcpy(delim,dlen);
			/*
			sb->safeMemcpy(" <font color=#",14);
			unsigned char colorVec[3];
			colorVec[0] = ppp[nextwpi]->m_color    ; // red
			colorVec[1] = ppp[nextwpi]->m_color / 2; // green
			colorVec[2] = ppp[nextwpi]->m_color / 2; // blue
			char dst[2];
			binToHex ( &colorVec[0] , 1 , dst ); //red
			sb->safeMemcpy(dst,2);
			binToHex ( &colorVec[1] , 1 , dst ); // green
			sb->safeMemcpy(dst,2);
			binToHex ( &colorVec[2] , 1 , dst ); // blue
			sb->safeMemcpy(dst,2);
			sb->safeMemcpy(">&bull;</font> ",15);
			*/
			// now use different shapes and colors!
			// 3 basic shapes, 2 colorsn
			WordPosInfo *wpix = ppp[nextwpi];
			char color = wpix->m_color;
			//long frac = 256 / 6;
			char ddd[10];
			long plen;
			char *ccc = "green";//#30ff30;";
			if ( color < 0 ) {
				color *= -1;
				ccc = "red";
			}
			if ( color == 0 ) {
				// no, don't clutter!
				continue;
				// empty circl
				ddd[0] = 0xe2;
				ddd[1] = 0x97;
				ddd[2] = 0x8b;
				ddd[3] = 0;
				plen = 3;
			}
			else if ( color == 1 ) {
				// 1/4 circ
				ddd[0] = 0xe2;
				ddd[1] = 0x97;
				ddd[2] = 0x94;
				ddd[3] = 0;
				plen = 3;
			}
			else if ( color == 2 ) {
				// half circle
				ddd[0] = 0xe2;
				ddd[1] = 0x97;
				ddd[2] = 0x90;
				ddd[3] = 0;
				plen = 3;
			}
			else if ( color == 3 ) {
				// 3/4
				ddd[0] = 0xe2;
				ddd[1] = 0x97;
				ddd[2] = 0x95;
				ddd[3] = 0;
				plen = 3;
			}
			else if ( color == 4 ) {
				// full circle
				ddd[0] = 0xe2;
				ddd[1] = 0x97;
				ddd[2] = 0x8f;
				ddd[3] = 0;
				plen = 3;
			}
			else {
				// star!
				ddd[0] = 0xe2;
				ddd[1] = 0x98;
				ddd[2] = 0x85;
				ddd[3] = 0;
				plen = 3;
				ccc = "blue";//green";
			}
			sb->safePrintf(" <font color=%s",ccc);

			// tooltip to show score info
			// only show for stars now
			//if ( xd->m_seoDebug ) { // && color >= frac * 5 ) {
			// print the scoring info that describes
			// how we arrived at this traffic gain
			printToolTip ( sb, wpix, sk, selectedTerm );
			//}
			// close the font tag
			sb->safePrintf(">");
			// print word pos too for debug
			if ( xd->m_seoDebug ) // g_conf.m_logDebugSEOInserts )
				sb->safePrintf("%li",wpix->m_wordPos);
			sb->safeMemcpy(ddd,plen);
			sb->safePrintf("</font> ");
		}
	skipJunk:
		// encode <
		if ( *p == '<' ) {
			sb->safeMemcpy ( "&lt;",4 );
			continue;
		}
		if ( *p == '>' ) {
			sb->safeMemcpy ( "&gt;",4 );
			continue;
		}
		if ( *p == '\"' ) {
			sb->safeMemcpy ( "&#34;",5 );
			continue;
		}
		if ( *p == '&' ) {
			sb->safeMemcpy ( "&amp;",5 );
			continue;
		}
		// print that byte
		sb->pushChar(*p);
	}


	if ( ! sk->m_htmlSrcOnly ) {
		sb->safePrintf("</div>\n");
		sb->safePrintf("</td></tr></table>\n" );
	}

	sendPageBack ( sk );
}



bool DocIdScore::serialize   ( class SafeBuf *sb ) {
	long need = sizeof(DocIdScore);
	need += m_numPairs   * sizeof(PairScore);
	need += m_numSingles * sizeof(SingleScore);
	if ( ! sb->reserve ( need ) ) return false;
	char *orig = sb->getBufStart();
	sb->safeMemcpy ( this , sizeof(DocIdScore));
	for ( long i = 0 ; i < m_numPairs ; i++ ) {
		PairScore *ps = &m_pairScores[i];
		sb->safeMemcpy ( ps , sizeof(PairScore) );
	}
	for ( long i = 0 ; i < m_numSingles ; i++ ) {
		SingleScore *ss = &m_singleScores[i];
		sb->safeMemcpy ( ss , sizeof(SingleScore) );
	}
	// sanity
	if ( sb->getBufStart() != orig ) { char *xx=NULL;*xx=0; }
	return true;
}

DocIdScore *QueryChange::getDebugDocIdScore ( SafeBuf *debugScoreInfoBuf ) {
	char *bs = debugScoreInfoBuf->getBufStart();
	bs += m_debugScoreInfoOffset;
	DocIdScore *ds = (DocIdScore *)bs;
	// ensure its ptrs are properly deserialized
	char *ptr = bs + sizeof(DocIdScore);
	ds->m_pairScores = (PairScore *)ptr;
	ptr += ds->m_numPairs * sizeof(PairScore);
	// now singles
	ds->m_singleScores = (SingleScore *)ptr;
	return ds;
}

DocIdScore *QueryChange::getOrigDocIdScore ( SafeBuf *origScoreInfoBuf ) {
	char *bs = origScoreInfoBuf->getBufStart();
	bs += m_origScoreInfoOffset;
	DocIdScore *ds = (DocIdScore *)bs;
	// ensure its ptrs are properly deserialized
	char *ptr = bs + sizeof(DocIdScore);
	ds->m_pairScores = (PairScore *)ptr;
	ptr += ds->m_numPairs * sizeof(PairScore);
	// now singles
	ds->m_singleScores = (SingleScore *)ptr;
	return ds;
}

// print the scoring info that describes
// how we arrived at this traffic gain
bool printToolTip ( SafeBuf *sb , 
		    WordPosInfo *wpix , 
		    StateSEO *sk ,
		    InsertableTerm *selectedTerm ) {

	// this is where we are inserting the glyph (half/full circle)
	long insertPos = wpix->m_wordPos;
	// the hash of the word being inserted
	long long termHash64 = selectedTerm->m_termHash64;
	// use the buf
	QueryChange *qc = selectedTerm->m_firstQueryChange;
	long qh32 = qc->m_queryHash32;

	sb->safePrintf(" title=\"");

	sb->safePrintf("totalMonthlyTrafficGain= %li\n",wpix->m_trafficGain);
	sb->safePrintf("---------------\n");

	// we sorted these in XmlDoc's hugePtrBuf by
	// 1: QueryChange::m_termHash64
        // 2: QueryChange::m_queryHash32
        // 3: QueryChange::m_insertPos
 subloop:
	// if it changes term, stop
	if ( qc->m_termHash64 != termHash64 ) goto done;
	// do this query
	printToolTipScoresForQuery ( sb, sk, qc, insertPos , qh32 , wpix );
	// advance to next query
	for ( qc = qc->m_next ; qc ; qc = qc->m_next )
		if ( qc->m_queryHash32 != qh32 ) break;
	//  update to next query hash
	if ( qc ) {
		qh32 = qc->m_queryHash32;
		goto subloop;
	}

 done:
	// remove last --------------- (15 -'s and one \n)
	char *end = sb->getBuf();
	if ( end[-1] == '\n' &&
	     end[-2] == '-' &&
	     end[-3] == '-' )
		sb->incrementLength(-16);

	// end alt tag
	sb->safePrintf("\"");
	return true;
}

#include "PageResults.h" // printTermPairs() printSingleTerm()

bool printToolTipScoresForQuery ( SafeBuf *sb , 
				  StateSEO *sk,
				  QueryChange *qc ,
				  long insertPos ,
				  long qh32 ,
				  WordPosInfo *wpix ) {

	// for just this query
	//long qh32 = qc->m_queryHash32;
	QueryChange *lastqc = NULL;
	
	for ( ; qc ; qc = qc->m_next ) {
		// stop if exceeds!
		if ( qc->m_insertPos > insertPos ) break;
		// stop if we run into different query
		if ( qc->m_queryHash32 != qh32 ) break;
		// keep chugging if <= our position
		lastqc = qc;
	}

	// no score?
	if ( ! lastqc ) return true;

	// if the rank was unchanged for this query for inserting this
	// term at insertpos, no need to print it!
	if ( lastqc->m_newRank == lastqc->m_oldRank ) return true;

	// sanity insurance
	qc = lastqc;

	XmlDoc *xd = &sk->m_xd;

	// we got one
	QueryLogEntry *qe = (QueryLogEntry *)(xd->m_queryLogBuf.getBufStart() +
					      lastqc->m_replyQueryOffset);
	char *qstr = qe->getQueryStr();

	sb->safePrintf("query=%s\n", qstr );
	sb->safePrintf("queryMonthlyTraffic= %li\n",
		       qe->m_gigablastTraffic*GB_TRAFFIC_MODIFIER
		       );
	long gain = xd->getTrafficGain ( lastqc );
	sb->safePrintf("monthlyTrafficGain= %li\n",gain);
	sb->safePrintf("oldrank=#%li newrank=#%li\n"
		       ,(long)lastqc->m_oldRank+1
		       ,(long)lastqc->m_newRank+1
		       );

	if ( ! xd->m_seoDebug ) {
		// . print query separator
		// . we remove the last one of these in parent function
		//   from the safebuf, sb
		sb->safePrintf("---------------\n");
		return true;
	}

	sb->safePrintf("oldscore=%.03f newscore=%.03f\n"
		       ,lastqc->m_oldScore
		       ,lastqc->m_newScore
		       );


	Query q;
	uint8_t langId = qe->getQueryLangId();
	q.set2 ( qstr , langId , true ); // true = do expansion?

	// get it
	DocIdScore *ds = NULL;
	ds = lastqc->getDebugDocIdScore(&xd->m_debugScoreInfoBuf);

	float siteRankWeight = 
		((float)ds->m_siteRank/(float)SITERANKDIVISOR)+1.0;
	float langWeight = 1.0;
	if ( langId == ds->m_docLang ||
	     langId == 0 ||
	     ds->m_docLang == 0 )
		langWeight = SAMELANGMULT;
	

	float sum = 0.0;
	// print that out then
	for ( long i = 0 ; i < ds->m_numPairs ; i++ ) {
		// cast it
		PairScore *ps = &ds->m_pairScores[i];
		//printTermPairs ( *sb , &q , ps );
		sb->safePrintf("(new): ");
		// print pair text
		long qtn1 = ps->m_qtermNum1;
		long qtn2 = ps->m_qtermNum2;
		sb->safePrintf("* ");
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		sb->safeMemcpy ( q.m_qterms[qtn1].m_term ,
				q.m_qterms[qtn1].m_termLen );
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		sb->safePrintf(" vs ");
		if ( q.m_qterms[qtn2].m_isPhrase )
			sb->pushChar('\'');
		sb->safeMemcpy ( q.m_qterms[qtn2].m_term ,
				q.m_qterms[qtn2].m_termLen );
		if ( q.m_qterms[qtn2].m_isPhrase )
			sb->pushChar('\'');
		// separator
		//if ( i > 1 ) sb->safePrintf("---\n");
		// print it
		sb->safePrintf(//"<tr><td>"
			       ": pos1=%li pos2=%li "
			       "dr1=%li dr2=%li SCORE=%.04f\n"
			       //"</td></tr>"
			       , (long)ps->m_wordPos1
			       , (long)ps->m_wordPos2
			       , (long)ps->m_densityRank1
			       , (long)ps->m_densityRank2
			       , ps->m_finalScore
			       );
		sum += ps->m_finalScore;
	}

	long ns;
	ns = ds->m_numSingles;
	if ( ds->m_numPairs ) ns = 0;
	for ( long i = 0 ; i < ns ; i++ ) {
		// cast it
		SingleScore *ss = &ds->m_singleScores[i];
		//printTermPairs ( *sb , &q , ps );
		sb->safePrintf("(new): ");
		// print pair text
		long qtn1 = ss->m_qtermNum;
		sb->safePrintf("* ");
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		sb->safeMemcpy ( q.m_qterms[qtn1].m_term ,
				q.m_qterms[qtn1].m_termLen );
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		// separator
		//if ( i > 1 ) sb->safePrintf("---\n");
		// print it
		sb->safePrintf(//"<tr><td>"
			       ": pos1=%li "
			       "dr1=%li SCORE=%.04f\n"
			       //"</td></tr>"
			       , (long)ss->m_wordPos
			       , (long)ss->m_densityRank
			       , ss->m_finalScore
			       );
		sum += ss->m_finalScore;
	}

	// show score calculation
	sb->safePrintf("TOTAL = %.04f = %.04f[abovesum] * "
		       "%f[siterankweight] * "
		       "%f[samelangweight]\n"
		       ,ds->m_finalScore
		       ,sum
		       ,siteRankWeight
		       ,langWeight
		       );

	///////////
	//
	// print the original docidscore without the insertion for
	// comparison
	//
	////////////

	ds = lastqc->getOrigDocIdScore(&xd->m_origScoreInfoBuf);


	sum = 0.0;
	// print that out then
	for ( long i = 0 ; i < ds->m_numPairs ; i++ ) {
		// cast it
		PairScore *ps = &ds->m_pairScores[i];
		//printTermPairs ( *sb , &q , ps );
		sb->safePrintf("(old): ");
		// print pair text
		long qtn1 = ps->m_qtermNum1;
		long qtn2 = ps->m_qtermNum2;
		sb->safePrintf("* ");
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		sb->safeMemcpy ( q.m_qterms[qtn1].m_term ,
				q.m_qterms[qtn1].m_termLen );
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		sb->safePrintf(" vs ");
		if ( q.m_qterms[qtn2].m_isPhrase )
			sb->pushChar('\'');
		sb->safeMemcpy ( q.m_qterms[qtn2].m_term ,
				q.m_qterms[qtn2].m_termLen );
		if ( q.m_qterms[qtn2].m_isPhrase )
			sb->pushChar('\'');
		// separator
		//if ( i > 1 ) sb->safePrintf("---\n");
		// print it
		sb->safePrintf(//"<tr><td>"
			       ": pos1=%li pos2=%li "
			       "dr1=%li dr2=%li SCORE=%.04f\n"
			       //"</td></tr>"
			       , (long)ps->m_wordPos1
			       , (long)ps->m_wordPos2
			       , (long)ps->m_densityRank1
			       , (long)ps->m_densityRank2
			       , ps->m_finalScore
			       );
		sum += ps->m_finalScore;
	}

	ns = ds->m_numSingles;
	if ( ds->m_numPairs ) ns = 0;
	for ( long i = 0 ; i < ns ; i++ ) {
		// cast it
		SingleScore *ss = &ds->m_singleScores[i];
		//printTermPairs ( *sb , &q , ps );
		sb->safePrintf("(old): ");
		// print pair text
		long qtn1 = ss->m_qtermNum;
		sb->safePrintf("* ");
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		sb->safeMemcpy ( q.m_qterms[qtn1].m_term ,
				q.m_qterms[qtn1].m_termLen );
		if ( q.m_qterms[qtn1].m_isPhrase )
			sb->pushChar('\'');
		// separator
		//if ( i > 1 ) sb->safePrintf("---\n");
		// print it
		sb->safePrintf(//"<tr><td>"
			       ": pos1=%li "
			       "dr1=%li SCORE=%.04f\n"
			       //"</td></tr>"
			       , (long)ss->m_wordPos
			       , (long)ss->m_densityRank
			       , ss->m_finalScore
			       );
		sum += ss->m_finalScore;
	}

	// show score calculation
	sb->safePrintf("TOTAL=%.04f = %.04f[abovesum] * "
		       "%f[siterankweight] * "
		       "%f[samelangweight]\n"
		       ,ds->m_finalScore
		       ,sum
		       ,siteRankWeight
		       ,langWeight
		       );

	sb->safePrintf("---------------\n");
	return true;
}

/*
// returns false and sets g_errno on error
void printCompetitorTerms ( StateSEO *sk ) {

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printCompetitorTerms;

	// scan each term
	SafeBuf *itBuf = xd->getScoredInsertableTerms();
	if ( ! itBuf ) { sendPageBack(sk); return; }
	if ( itBuf == (void *)-1 ) return;

	SafeBuf *sb = &sk->m_sb;

	sb->safePrintf("<div "
		      "style=\""
		      "border:2px solid;"
		      "margin-left:5px;"
		      "margin-right:5px;"
		      "\">");

	sb->safePrintf("<table width=100%% cellspacing=0 cellpadding=5>"
		      "<tr>"
		      "<td><b>Competitor Term</b></td>"
		      "<td><b>Importance</b></td>"
		      "<td><b>Max Traffic Gain</b></td>"
		      "</tr>\n"
		      );

	// cast it
	InsertableTerm *its = (InsertableTerm *)itBuf->getBufStart();
	// how many terms do we have?
	//long nits = itBuf->length() / sizeof(InsertableTerm);

	for ( long i = 0 ; i < nits ; i++ ) {
		// shortcut
		InsertableTerm *it = &its[i];
		// is it the selected one?
		if ( ! it->m_isRelatedTerm ) continue;
		// print it
		sb->safePrintf("<tr><td>");
		sb->safeMemcpy ( it->m_termStr , it->m_termLen );
		sb->safePrintf("</td>\n");

		sb->safePrintf("<td>%li</td>",it->m_trafficSum);
		sb->safePrintf("<td>%li</td>",it->m_bestTrafficGain);

		sb->safePrintf("</tr>\n");
	}

	sb->safePrintf("</table>");
	sb->safePrintf("</div>");
	sendPageBack ( sk );
}
*/

// returns false and sets g_errno on error
void printMatchingQueries ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printMatchingQueries;

	// now get recommended links (link juice)
	SafeBuf *mq = xd->getMatchingQueryBuf();
	if ( ! mq ) { sendPageBack ( sk ); return; }
	if (  mq == (void *)-1 ) return;

	if ( sk->m_xml ) {
		sb->safePrintf (
				"<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<error>0</error>\n"
				);
	}

	if ( ! sk->m_xml ) {

		sb->safePrintf("<div "
			       "style=\""
			       "border:2px solid;"
			       "margin-left:5px;"
			       "margin-right:5px;"
			       "\">");
		
		sb->safePrintf("<table width=100%% cellspacing=0 "
			       "cellpadding=5>"
			       "<tr>"
			       "<td></td>" // # column
			       "<td><b>Matching Query</b></td>"
			       "<td><b>Importance</b></td>"
			       "<td><b>Your SERP Score</b></td>"
			       "<td><b>Good SERP Score</b></td>"
			       "<td><b>Monthly Traffic</b></td>"
			       "<td><b>Total Results</b></td>"
			       "<td><b>Query Lang</b></td>"
			       "</tr>\n"
			       );
	}

	long nks = mq->length() / sizeof(QueryLink);
	QueryLink *qks = (QueryLink *)mq->getBufStart();

	float max = -1.0;
	for ( long i = 0 ; i < nks ; i++ ) {
		QueryLink *qk = &qks[i];
		if ( qk->m_queryImportance > max ) max = qk->m_queryImportance;
	}

	long maxPrint = 300;
	long rowNum = 1;
	for ( long i = 0 ; i < nks ; i++ ) {
		if ( --maxPrint <= 0 ) break;
		QueryLink *qk = &qks[i];
		QueryLogEntry *qe ;
		qe = qk->getQueryLogEntry(&xd->m_matchingQueryStringBuf);
		// for the big index, we did a .1 slice of docid range
		long long mod = 10 * g_hostdb.getNumGroups();
		// smaller indexes did not do the slice logic
		if ( g_titledb.getGlobalNumDocs() <= 10000000 ) 
			mod = 1;
		long long numResults = qe->m_numTotalResultsInSlice;
		if ( numResults > 0 ) numResults *= mod;
		// get estimated google traffic from gigablast traffic
		long traffic = qe->m_gigablastTraffic * GB_TRAFFIC_MODIFIER;
		// encode query string
		char qenc[1024];
		char *qstr = qe->getQueryString();
		long qlen = gbstrlen(qstr);

		// print out xml
		if ( sk->m_xml ) {
			sb->safePrintf("\t<seoQuery>\n"

				       "\t\t<queryNum>%li</queryNum>\n"
				       //"\t\t<encoded>"
				       //"<![CDATA[%s]]>"
				       //"</encoded>\n"

				       "\t\t<query>"
				       "<![CDATA[%s]]>"
				       "</query>\n"

				       "\t\t<queryUsagePerMonth>"
				       "%li"
				       "</queryUsagePerMonth>\n"
				       

				       "\t\t<queryImportance>"
				       "%f"
				       "</queryImportance>\n"
				       
				       "\t\t<mySERPScore>"
				       "%f"
				       "</mySERPScore>\n"

				       "\t\t<goodSERPScore>"
				       "%f"
				       "</goodSERPScore>\n"

				       // 64-bit (long long)
				       "\t\t<numTotalSearchResults64>"
				       "%lli"
				       "</numTotalSearchResults64>\n"

				       "\t\t<queryLanguage>"
				       "<![CDATA[%s]]>"
				       "</queryLanguage>\n"

				       "\t</seoQuery>\n"
				       ,i
				       //,qenc
				       ,qstr
				       ,traffic
				       ,qk->m_queryImportance // / max
				       ,qk->m_serpScore//for our docid in SERPS
				       ,qe->m_topSERPScore // minTop50Score
				       ,numResults
				       ,getLanguageString(qe->m_langId)
				       );
			continue;
		}


		//float imp = (qp->m_queryImportance * 100.0) / max;
		// this null terminates it for us too
		urlEncode(qenc,1023,qstr,qlen);
		sb->safePrintf("<tr>"
			       "<td>%li</td>"
			       "<td>"
			       "." // for on page searching
			       "<a href=/search?q=%s>"
			       ,rowNum++
			       ,qenc
			       );
		// print out query, if longer than 40 trunc & print ellipsis 
		sb->safeTruncateEllipsis(qstr,40);

		sb->safePrintf("</a>"
			       "." // for on page searching
			       "</td>"
			       "<td>%f</td>"
			       "<td>%f</td>"
			       "<td>%f</td>"
			       "<td>%li</td>"
			       "<td>%lli</td>"
			       "<td>%s</td>"
			       "</tr>\n"
			       ,qk->m_queryImportance // / max
			       ,qk->m_serpScore // for our docid in SERPS
			       ,qe->m_topSERPScore // minTop50Score
			       ,qe->m_gigablastTraffic * GB_TRAFFIC_MODIFIER
			       ,numResults
			       ,getLanguageString(qe->m_langId)
			       );
	}

	if ( ! sk->m_xml ) {
		sb->safePrintf("</table>\n");
		sb->safePrintf("</div>\n");
	}
	sendPageBack ( sk );
}

// give one result's score and it's approximate rank, what rank do we
// estimate the provided score to yield
long getRankEstimate ( float myScore ,
		       float goodScore ,
		       long  goodScoreRank ) {

	if ( myScore == goodScore )
		return goodScoreRank;

	// . 1.10^n * myScore = goodScore ... what is n???
	// . 1.10^n = goodScore/myScore
	// . log of (goodScore/myScore) in base 1.10
	// . 1.10^(myScore/goodScore) = n
	if ( myScore > goodScore ) {
		// goodScoreRank is usually 640 i think
		long  rank  = goodScoreRank;
		float score = goodScore;
		for ( ; ; rank-- ) {
			if ( rank <= 0 ) return rank;
			if ( score >= myScore ) return rank;
			float delta;
			if      ( rank > 500 ) delta = .001;
			else if ( rank > 400 ) delta = .002;
			else if ( rank > 300 ) delta = .003;
			else if ( rank > 200 ) delta = .004;
			else if ( rank > 100 ) delta = .005;
			else if ( rank >  50 ) delta = .009;
			else if ( rank >  10 ) delta = .05;
			else                   delta = .15;
			score *= (1.0 + delta);
		}
		//float powf(float x, float y);
	}

	// . 0.90^n * myScore = goodScore ... what is n????
	// . log of (goodScore/myScore) in base 0.90
	// . goodScoreRank is usually 640 i think
	long  rank  = goodScoreRank;
	float score = goodScore;
	for ( ; ; rank++ ) {
		// 1000 means 1000 OR beyond
		if ( rank >= 1000 ) return rank;
		if ( score < myScore ) return rank;
		// if we finally make score small enough, return that rank
		score *= .97;
	}

	return 1000;
}

// returns false and sets g_errno on error
void printRelatedQueries ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printRelatedQueries;

	//log("debug: in printrelatedqueries");

	// now get recommended links (link juice)
	SafeBuf *qkBuf = xd->getRelatedQueryBuf();
	if ( qkBuf == NULL ) { sendPageBack(sk); return; }
	if ( qkBuf == (void *)-1 ) return;

	long nks = qkBuf->length() / sizeof(QueryLink);
	QueryLink *qks = (QueryLink *)qkBuf->getBufStart();

	// this tells us what terms in ptr_termIdBuf we contain in our
	// doc or have indexed for our doc. otherwise, if they are not in
	// that table, they are "new" terms from related queries that will
	// be recommended for addition. see XmlDoc::getMissingTerms().
	HashTableX *topWordsTable = xd->getTermIdBufDedupTable32();
	if ( ! topWordsTable ) { sendPageBack(sk); return; }
	if ( topWordsTable == (void *)-1 ) return;

	if ( ! sk->m_xml ) {

		sb->safePrintf("<div "
			       "style=\""
			       "border:2px solid;"
			       "margin-left:5px;"
			       "margin-right:5px;"
			       "\">");
		
		sb->safePrintf("<table width=100%% cellspacing=0 "
			       "cellpadding=5>"
			       "<tr>"
			       "<td>#</td>"
			       "<td><b>Related Query</b></td>"
			       "<td><b>Importance</b></td>"
			       "<td><b>Traffic</b></td>"
			       "<td><b># of Competing Pages that "
			       "Match</b></td>"
			       "</tr>\n"
			       );
	}

	if ( sk->m_xml ) {
		sb->safePrintf (
				"<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<error>0</error>\n"
				);
	}

	SafeBuf *tbuf = &xd->m_relatedTitleBuf;
	if ( ! xd->m_relatedTitleBufValid ) { char *xx=NULL; *xx=0; }

	//SafeBuf *qrbuf = &xd->m_queryRelBuf;
	//if ( ! xd->m_queryRelBufValid ) { char *xx=NULL; *xx=0; }

	SafeBuf *rdbuf = &xd->m_relatedDocIdBuf;
	if ( ! xd->m_relatedDocIdBufValid ) { char *xx=NULL; *xx=0; }

	// an array of ptrs to QueryRels
	//QueryRel **rels = (QueryRel **)relBuf->getBufStart();
	//long *qrOffs = (long *)relBuf->getBufStart();
	//long numRels = relBuf->length() / sizeof(long);//QueryRel *);
	//char *base = qrbuf->getBufStart();
	long rowCount = 0;

	for ( long i = 0 ; i < nks ; i++ ) {
		// stop at 300?
		//if ( i >= 300 ) break;
		//QueryRel *rel = rels[i];
		//QueryRel *rel = (QueryRel *)(base+qrOffs[i]);
		QueryLink *qk = &qks[i];
		// skip if not a head of a linked list
		if ( ! qk->m_isFirst ) continue;

		rowCount++;

		QueryLogEntry *qe ;
		qe = qk->getQueryLogEntry(&xd->m_relatedQueryStringBuf);

		long traffic;
		if ( qe->m_googleTraffic >= 0 ) 
			traffic = qe->m_googleTraffic;
		else {
			traffic = qe->m_gigablastTraffic;
			traffic *= GB_TRAFFIC_MODIFIER;
		}


		char *qstr = qe->getQueryString();

		if ( ! sk->m_xml ) {
			sb->safePrintf("<tr valign=top>"
				       "<td>%li</td>"
				       "<td>"
				       "<a href=/search?q="
				       ,rowCount
				       );
		}
		else {
			sb->safePrintf("\t<relatedQuery>\n");
			sb->safePrintf("\t\t<query>"
				       "<![CDATA[%s]]>"
				       "</query>\n"
				       "\t\t<importance>%f</importance>\n"
				       "<monthlyTraffic>%li</monthlyTraffic>\n"
				       "<numCompetitorPagesThatMatch>%li"
				       "</numCompetitorPagesThatMatch>\n"
				       ,qstr
				       ,qk->m_totalQueryImportance
				       ,(long)traffic
				       ,(long)qk->m_docIdVotes //- notFounds
				       );
		}
		
		
		if ( ! sk->m_xml ) {
			sb->urlEncode ( qstr );
			sb->safePrintf(">");
		}

		// print query but bold-face the terms our doc has not
		Query qq;
		qq.set2 ( qstr , qe->m_langId , false );

		if ( sk->m_xml )
			sb->safePrintf("\t\t<boldedQuery><![CDATA[");

		for ( long x = 0 ; x < qq.m_numWords ; x++ ) {
			QueryWord *qw = &qq.m_qwords[x];
			long tid32 = qw->m_wordId & 0xffffffff;
			// is it not contained by our doc
			char *bs1 = NULL;
			char *bs2 = NULL;
			if ( tid32 && 
			     ! topWordsTable->isInTable ( &tid32 ) &&
			     // do not highlight "on" "at" etc.
			     ! isCommonQueryWordInEnglish(tid32) ) {
				bs1 = "<b>";
				bs2 = "</b>";
			}
			if ( bs1 ) sb->safeStrcpy(bs1);
			sb->safeMemcpy(qw->m_word,qw->m_wordLen);
			if ( bs2 ) sb->safeStrcpy(bs2);
		}

		if ( ! sk->m_xml )
			sb->safePrintf("</a>");
		else
			sb->safePrintf("]]></boldedQuery>\n");

		//QueryRel *cq = rel;
		//QueryLink *cq = qk;

		//
		// these are only valid for head of linked list i think...
		//
		// . estimate rank for this query
		// . ideally we would have the scores of the top 300
		//   results in cachedb for all queries in the
		//   querylogentries, but for now we must estimate
		//->getTopOfSliceSERPScore(rqsb);
		float goodScore = qe->m_topSERPScore;
		if ( xd->m_seoDebug )
			sb->safePrintf ( " <font size=-2>"
					 "(goodSERPscore=%f)"
					 "</font>"
					 ,goodScore);

		// goodScore is score of result probably #128
		// on avg i think since it is from just a slice
		//long goodScoreRank = g_hostdb.getNumHosts();
		// HACK, these were generated on a 64-node cluster
		// with gk208 as host #0 and then copied to
		// gk144's 64-node cluster. furthermore, it was
		// restricted to a slice of 1/10th of the docids.
		long goodScoreRank = 64 * 10;
		// single host, on titan? HACK!
		if ( g_hostdb.getNumHosts() == 1 ) goodScoreRank = 0;

		//QueryLink *prevcq = NULL;
		long notFounds = 0;
		long count = 0;
		long maxToDisplay = 10;
		if ( xd->m_seoDebug ) maxToDisplay = 300;
		// loop over all the querylinks for this query
		for ( long j = i ; j < nks ; j++ ) {
			// cast it
			QueryLink *qj = &qks[j];
			// stop if we get into another list for another query!
			if ( j != i && qj->m_isFirst ) break;
			// show at most 80
			if ( count++ >= maxToDisplay) break;
			RelatedDocId *rd = qj->getRelatedDocId(rdbuf);
			char *url = rd->getUrl(tbuf);
			// a not found in titledb?
			if ( ! url ) { 
				notFounds++; 
				if ( ! xd->m_seoDebug ) continue; 
				if ( sk->m_xml ) continue;
				sb->safePrintf("<br> &nbsp; "
					       "<font size=-2>"
					       "because it matches <i>"
					       "docid %lli</i>"
					       , rd->m_docId);
			}
			else if ( ! sk->m_xml ) {
				sb->safePrintf("<br> &nbsp; <font size=-2>"
					       "because it matches <i>"
					       "<a href=/get?d=%llu&cnsp=0>"
					       , rd->m_docId);
				sb->safeTruncateEllipsis(url,60);
				sb->safePrintf("</a></i>");
			}
			else if ( sk->m_xml ) {
				sb->safePrintf("\t\t<matchingCompetitor>"
					       "\t\t\t<url>"
					       "<![CDATA[%s]]>"
					       "</url>\n"
					       //"\t\t\t<score>%f</score>"
					       "\t\t\t<siteRank>%li"
					       "</siteRank>\n"
					       "\t\t\t<serpScore>%f"
					       "</serpScore>\n"
					       "\t\t\t<competitorWeight>%f"
					       "</competitorWeight>\n"
					       ,url
					       //,qj->m_queryImportance
					       ,(long)rd->m_rd_siteRank
					       ,qj->m_serpScore
					       ,rd->m_relatedWeight
					       );
			}
					       

			// until we store the top 300 results of all 
			// querylogentries maybe in cachedb, we have to
			// estimate what the rank of this related docid will be
			//long rank = getRankEstimate ( qj->m_myScore ,
			//			      goodScore ,
			//			      goodScoreRank );
			// estimate traffic for this related docid
			//double tp = getTrafficPercent ( rank );
			// multiply by total traffic to get our traffic
			//long myTraffic = (long)(tp * traffic);
			// show it
			//sb->safePrintf(" (rank %li, traffic %li"
			//	       , rank, myTraffic );



			//char *pstr = "0";
			char *hs = NULL;
			if ( qj->m_serpScore > goodScore &&
			     rd->m_rd_siteRank < 12 ) {
				//pstr = "<b><font color=red>1</font></b>";
				hs = " (high rank)";
			}
			// show "(high rank)" if url ranks well for query
			if ( ! sk->m_xml && hs && ! xd->m_seoDebug ) 
				sb->safePrintf("%s",hs);

			// print out debug info?
			if ( xd->m_seoDebug ) {
				char *rt1 = "";
				char *rt2 = "";
				if ( qj->m_queryImportance >= 1000.0 ) {
					rt1 = "<font color=red>";
					rt2 = "</font>";
				}
				sb->safePrintf(" (score=%s<b>%f</b>%s, "
					       "serpscore=%f, "
					       //"goodserpscore=%f, "
					       "siterank=%li, "
					       "docidnum=%li "
					       "compweight=%f)"
					       ,rt1
					       ,qj->m_queryImportance
					       ,rt2
					       ,qj->m_serpScore
					       ,(long)rd->m_rd_siteRank
					       ,(long)qj->m_relatedDocIdNum
					       //,goodScore
					       ,rd->m_relatedWeight
					       );
			}
			//if ( xd->m_seoDebug )
			//	sb->safePrintf(",score=%f"
			//		       ",topslicescore=%f"
			//		       ",topscorerank=%li"
			//		       ,qj->m_serpScore
			//		       ,goodScore
			//		       ,goodScoreRank);

			if ( ! sk->m_xml )
				sb->safePrintf("</font>");

			if ( sk->m_xml )
				sb->safePrintf("\t\t</matchingCompetitor>\n");
		}

		if ( sk->m_xml )
			sb->safePrintf("\t</relatedQuery>\n");

		if ( ! sk->m_xml )
			sb->safePrintf("</td>"
				       "<td>%li/%.03f</td>"
				       "<td>%li</td>"
				       "<td>%li</td>"
				       "</tr>\n"
				       ,(long)qk->m_uniqueRound
				       ,qk->m_totalQueryImportance
				       ,(long)traffic
				       // we now skip notfounds in xmldoc.cpp
				       // where it sets m_docIdVotes
				       ,(long)qk->m_docIdVotes //- notFounds
				       );
	}

	if ( ! sk->m_xml ) {
		sb->safePrintf("</table>\n");
		sb->safePrintf("</div>\n");
	}

	sendPageBack ( sk );
}


// returns false and sets g_errno on error
void printCompetitorPages ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printCompetitorPages;

	SafeBuf *rdbuf = xd->getRelatedDocIdsWithTitles();
	if ( rdbuf == NULL ) { sendPageBack(sk); return; }
	if ( rdbuf == (void *)-1 ) return;


	// buffer of matching queries to print queries in common with
	// the related docids (i.e. competing pages)
	SafeBuf *qkbuf = xd->getMatchingQueryBuf();
	if ( ! qkbuf || qkbuf == (void *)-1 ) { char *xx=NULL;*xx=0; }



	// now get recommended links (link juice)
	//SafeBuf *mq = xd->getMatchingQueriesScored();
	//if ( ! mq ) { sendPageBack(sk); return; }
	//if ( mq == (void *)-1 ) return;


	if ( ! sk->m_xml ) {
		sb->safePrintf("<div "
			       "style=\""
			       "border:2px solid;"
			       "margin-left:5px;"
			       "margin-right:5px;"
			       "font-size:11px;"
			       "\">");
		
		sb->safePrintf("<table width=100%% "
			       "cellspacing=0 cellpadding=5>"
			       "<tr>"
			       "<td></td>"
			       "<td><b>Competitor Page</b></td>"
			       "<td><b>Score</b></td>"
			       "<td><b>Title</b></td>"
			       "<td><b>PageRank~</b></td>"
			       "<td><b>IP</b></td>"
			       // estimate how much traffic he gets
			       //"<td><b>Monthly Traffic</b></td>"
			       "<td><b>Language</b></td>"
			       "</tr>\n"
			       );
	}

	if ( sk->m_xml ) {
		sb->safePrintf (
				"<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<error>0</error>\n"
				);
	}

	char *rdpStart = xd->m_relatedDocIdBuf.getBufStart();
	char *rdpEnd = xd->m_relatedDocIdBuf.getBuf();
	char *rdp = rdpStart;


	float max = -1.0;
	for ( ; rdp < rdpEnd ; ) {
		// cast it
		RelatedDocId *rd = (RelatedDocId *)rdp;
		// skip it. the strings are stored in m_relatedTitleBuf.
		rdp += sizeof(RelatedDocId);
		// get max
		if ( rd->m_relatedWeight > max ) 
			max = rd->m_relatedWeight;
	}

	long long numPagesIndexed = g_titledb.getGlobalNumDocs();

	//long maxRows = 300;
	long rowCount = 0;
	// print out each one
	for ( rdp = rdpStart ; rdp < rdpEnd ; ) {
		// stop?
		//if ( maxRows-- <= 0 ) break;
		// cast it
		RelatedDocId *rd = (RelatedDocId *)rdp;
		// skip it. the strings are stored in m_relatedTitleBuf.
		rdp += sizeof(RelatedDocId);
		// some guys had errors when setting their titles using Msg20
		// in XmlDoc::getRelatedDocIdsWithTitles() like EDOCFILTERED
		// EDOCBANNED or they linked to our main url's domain, so they
		// are not competing per se.
		if ( rd->rd_url_off < 0 ) continue;

		// count it
		rowCount++;

		char *url = rd->getUrl(&xd->m_relatedTitleBuf);
		char *title = rd->getTitle(&xd->m_relatedTitleBuf);

		if ( sk->m_xml ) {
			sb->safePrintf("\t<seoCompetitor>\n"

				       "\t\t<competitorNum>%li"
				       "</competitorNum>\n"

				       "\t\t<url>"
				       "<![CDATA[%s]]>"
				       "</url>\n"

				       "\t\t<docId>%lli</docId>\n"

				       "\t\t<siteHash26>%lu</siteHash26>\n"

				       "\t\t<title>"
				       "<![CDATA[%s]]>"
				       "</title>\n"

				       "\t\t<siteRank>%li</siteRank>\n"

				       "\t\t<firstIP>"
				       "<![CDATA[%s]]>"
				       "</firstIP>\n"

				       "\t\t<pageLang>"
				       "<![CDATA[%s]]>"
				       "</pageLang>\n"
				       
				       "\t\t<score>%f</score>\n"

				       ,rowCount-1 // start at 0
				       ,url
				       ,rd->m_docId
				       ,rd->m_siteHash26
				       ,title
				       
				       , (long)rd->m_rd_siteRank
				       , iptoa(rd->m_relatedFirstIp)
				       , getLanguageString(rd->m_rd_langId)
				       , rd->m_relatedWeight // dotProduct 

				       );
		}
		else {
			// show it
			sb->safePrintf("<tr valign=top>"
				       "<td>%li</td>"
				       "<td style=word-wrap;break-word;><b>"
				       "<a href=/get?d=%llu&cnsp=0>"
				       , rowCount // start at 1
				       , rd->m_docId );
			// print the url, print ellipsis if truncated
			sb->safeTruncateEllipsis(url,60);
			sb->safePrintf("</a>"
				       "</b> "
				       "<font size=-1>"
				       "[<a href=/seo?u=%s&page="
				       "matchingqueries>"
				       "analyze</a>]"
				       "</font>"
				       , rd->getUrl(&xd->m_relatedTitleBuf));
		}
		// print the queries in common!
		long firstOff = rd->m_firstCommonQueryNumOff;
		long qoffset = firstOff;
		bool first = true;
		for ( ; qoffset >= 0 ; ) {
			// get that node
			char *buf = xd->m_commonQueryNumBuf.getBufStart();
			// and offset
			buf += qoffset;
			// then cast
			QueryNumLinkedNode *qn;
			qn = (QueryNumLinkedNode *)buf;

			// advance. will be -1 when done
			if ( qn ) qoffset = qn->m_nextOff;
			else qoffset = -1;

			// get matching queries
			SafeBuf *mq = xd->getMatchingQueryBuf();
			if ( ! mq || mq == (void *)-1 ) { char *xx=NULL;*xx=0;}
			// cast it
			QueryLink *qks = (QueryLink *)mq->getBufStart();
			// get #qn into there
			QueryLink *qk = &qks[qn->m_queryNum];

			// skip this matching query if its importance was 0
			// or it was too similar (same synbasehash) to another
			// query we did in full before it. in such cases
			// we do not execute a msg3a on it in
			// XmlDoc::getMatchingQueriesScoredForFullQuery() and
			// this offset will be -1 since it will not reference
			// any topdocids structure.
			//if ( qp->m_topDocIdsBufOffset < 0 ) continue;
			

			long rdRank = qn->m_relatedDocIdRank;//-1;
			long yourRank = qn->m_mainUrlRank;//-1;
			float yourSerpScore = qk->m_serpScore;
			float rdSerpScore = qn->m_relatedDocIdSerpScore;

			// . that is the query in common!
			// . we also have traffic etc in qp->m_queryLogEntry
			if ( first && ! sk->m_xml ) 
				sb->safePrintf("<font size=-2>");
			// encode query string
			char qenc[1024];
			QueryLogEntry *qe ;
			qe=qk->getQueryLogEntry(&xd->m_matchingQueryStringBuf);
			char *qstr = qe->getQueryString();
			long qlen = gbstrlen(qstr);
			// this null terminates it for us too
			urlEncode(qenc,1023,qstr,qlen);
			//qenc[0] = '\0';
			first = false;
			if ( sk->m_xml ) {
				sb->safePrintf("\t\t<matchingQuery>"
					       "<![CDATA[%s]]>"
					       "</matchingQuery>\n"
					       ,qstr 
					       );
			}
			if ( sk->m_xml && rdRank >= 0 ) {
				sb->safePrintf("\t\t<rankForQuery>%li"
					       "</rankForQuery>\n"
					       ,rdRank
					       );
			}
			if ( sk->m_xml && yourRank >= 0 ) {
				sb->safePrintf("\t\t<yourRankForQuery>%li"
					       "</yourRankForQuery>\n"
					       ,yourRank
					       );
			}
			long traffic = qe->m_gigablastTraffic;
			traffic *= GB_TRAFFIC_MODIFIER;
			if ( qe->m_googleTraffic >= 0 )
				traffic = qe->m_googleTraffic;
			if ( sk->m_xml ) {
				sb->safePrintf("\t\t<monthlySearches>%li"
					       "</monthlySearches>\n"
					       ,traffic);
			}
			if ( sk->m_xml )
				continue;

			sb->safePrintf("<br>"
				       "<nobr>"
				      "&nbsp;"
				      "&nbsp;"
				      "because it matches <i>"
				      "<a href=/search?q=%s>"
				      "%s"
				      "</a>"
				      "</i>"
				      , qenc
				       , qstr
				      );
			// does the related docid rank high for this query?
			//if ( rdScore > qe->m_topSERPScore )
			//	sb->safePrintf(" (highrank)");
			// does he rank higher than you?

			// . we must have this data because his related docid
			//   score gets x10 if he beats you
			sb->safePrintf(" (searches %li", traffic);
			if ( rdRank >= 0 ) {
				char *bs1 = "";
				char *bs2 = "";
				//if ( rdRank < yourRank ) {
				bs1 = "<b><font color=red>";
				bs2 = "</font></b>";
				//}
				sb->safePrintf(",rank %s%li%s"
					       ,bs1
					       ,rdRank+1
					       ,bs2
					       );
			}
			if ( yourRank >= 0 ) {
				char *bs1 = "";
				char *bs2 = "";
				//if ( yourRank < rdRank ) {
				bs1 = "<b><font color=green><u>";
				bs2 = "</u></font></b>";
				//}
				sb->safePrintf(",your rank %s%li%s"
					       ,bs1
					       ,yourRank+1
					       ,bs2
					       );
			}
			// show serp scores for debug
			if ( xd->m_seoDebug ) {
				long long numResults;
				numResults = qe->m_numTotalResultsInSlice;
				numResults*=(long long)g_hostdb.getNumGroups();
				if ( numPagesIndexed > 10000000)numResults*=10;
				char *bs1 = "";
				char *bs2 = "";
				if ( qn->m_queryScoreWeight >= 50.0 ) {
					bs1 = "<b>";
					bs2 = "</b>";
				}
				sb->safePrintf(",serpscore=%f"
					       ",yourserpscore=%f"
					       ",numresults=%lli"
					       ",score=%s%f%s"
					       ,rdSerpScore
					       ,yourSerpScore
					       ,numResults
					       // the sum of these scores
					       // is the final related docid
					       // score/weight
					       ,bs1
					       ,qn->m_queryScoreWeight
					       ,bs2
					       );
			}

			sb->safePrintf(")</nobr>");
			//if ( beatsUs )
			//	sb->safePrintf("(outranks you)");
		}

		if ( sk->m_xml ) {
			sb->safePrintf("\t</seoCompetitor>\n");
			continue;
		}

		if ( ! first )
			sb->safePrintf("</font>");
		// end that cell
		sb->safePrintf("</td>");


		sb->safePrintf("<td>%.03f</td>"
			      "<td>"
			       , rd->m_relatedWeight // dotProduct / max
			       );

		// title
		// print the title, print ellipsis if truncated
		sb->safeTruncateEllipsis(title,80);
		sb->safePrintf("</td>"
			       "<td>%li</td>"
			       "<td>%s</td>"
			       "<td>%s</td>"
			       "</tr>\n"
			      , (long)rd->m_rd_siteRank
			      , iptoa(rd->m_relatedFirstIp)
			      , getLanguageString(rd->m_rd_langId)
			      );
	}

	if ( ! sk->m_xml ) {
		sb->safePrintf("</table>\n");
		sb->safePrintf("</div>\n");
	}

	sendPageBack ( sk );
}

// returns false and sets g_errno on error
void printDupSentences ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printDupSentences;

	Words *ww = xd->getWords();
	if ( ! ww ) { sendPageBack ( sk); return; }
	if ( ww == (void *)-1 ) return;

	Sections *sections = xd->getSectionsWithDupStats(); 
	if ( ! sections ) { sendPageBack ( sk); return; }
	if ( sections == (void *)-1 ) return;

	char **wptrs = ww->getWords();
	long  *wlens = ww->getWordLens();
	Section *si = sections->m_rootSection;



	SafeBuf *sb = &sk->m_sb;

	sb->safePrintf("<div "
		      "style=\""
		      "border:2px solid;"
		      "margin-left:5px;"
		      "margin-right:5px;"
		      "\">");

	sb->safePrintf("<table width=100%% cellspacing=0 cellpadding=5>"
		      "<tr>"
		      "<td><b>Sentence</b></td>"
		      "<td><b>OnSite Page Dups</b></td>"
		      "<td><b>OffSite Page Dups</b></td>"
		      "<td><b>Unique Site Dups</b></td>"
		      "</tr>\n"
		      );

	for ( ; si ; si = si->m_next ) {
		// skip if not sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		long on  = si->m_stats.m_onSiteDocIds;
		long off = si->m_stats.m_offSiteDocIds;
		long us  = si->m_stats.m_numUniqueSites;
		// ignore ourselves
		if ( on > 0 ) on--;
		us--;
		if ( on <= 0 && off <= 0 ) continue;
		// show it
		char *str = wptrs[si->m_a];
		char *end = wptrs[si->m_b-1] + wlens[si->m_b-1];
		sb->safePrintf("<tr><td>");
		sb->safePrintf("<a href=/search?q=gbsectionhash:%llu>",
			      si->m_sentenceContentHash64);

		SafeBuf tmp;
		tmp.htmlEncode ( str , end - str , false );
		// end words over 20 chars in ellipsis "..."
		sb->truncateLongWords ( tmp.getBufStart(), tmp.length() , 20 );

		sb->safePrintf("</a>");
		sb->safePrintf("</td>");
		sb->safePrintf("<td>%li</td>",on);
		sb->safePrintf("<td>%li</td>",off);
		sb->safePrintf("<td>%li</td>",us);
		sb->safePrintf("</tr>\n");
	}

	sb->safePrintf("</table>\n");
	sb->safePrintf("</div>\n");
	sendPageBack ( sk );
}

void printPageBacklinks ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printPageBacklinks;

	// . seo.cpp now shows the raw page and site inlinks
	// . TODO: make sure to cache it into cachedb!
	Msg25 *pi = xd->getAllInlinks(false); // forSite?
	if ( pi == (void *)-1 ) return;
	if ( ! pi ) { sendPageBack ( sk ); return; }


	sb->safePrintf("<div "
		      "style=\""
		      "border:2px solid;"
		      "margin-left:5px;"
		      "margin-right:5px;"
		      "\">");

	sb->safePrintf("<table width=100%% cellspacing=0 cellpadding=5>"
		      "<tr>"
		      "<td><b>PAGE Backlink</b></td>"
		      "<td><b>Title</b></td>"
		      "<td><b>Link Text</b></td>"
		      "<td><b>Note</b></td>"
		      "<td><b>Weight</b></td>"
		      "</tr>\n"
		      );


	for ( long i = 0 ; i < pi->m_numReplyPtrs ; i++ ) {
		Msg20Reply *reply = pi->m_replyPtrs[i];
		char *txt = reply->ptr_linkText;
		if ( ! txt ) txt = "";
		char *title = reply->ptr_tbuf;
		if ( ! title ) title = "";
		char *note = reply->ptr_note;
		if ( ! note ) note = "<font color=green><b>good</b></font>";
		sb->safePrintf("<tr>"
			       "<td>");
		sb->safeTruncateEllipsis(reply->ptr_ubuf,50);
		sb->safePrintf("</td>"
			       "<td>" );
		sb->safeTruncateEllipsis(title,50);
		sb->safePrintf("</td>"
			       "<td>");
		sb->safeTruncateEllipsis(txt,50);
		sb->safePrintf("</td>"
			       "<td>%s</td>"
			       "<td>%li</td>"
			       "</tr>\n"
			       , note
			       , (long)reply->m_inlinkWeight
			       );
	}

	sb->safePrintf("</table>\n");
	sb->safePrintf("</div>\n");
	sendPageBack ( sk );
}

void printSiteBacklinks ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printSiteBacklinks;

	// . seo.cpp now shows the raw page and site inlinks
	// . TODO: make sure to cache it into cachedb!
	Msg25 *pi = xd->getAllInlinks(true); // forSite?
	if ( pi == (void *)-1 ) return;
	if ( ! pi ) {
		sendPageBack ( sk );
		return;
	}


	sb->safePrintf("<div "
		      "style=\""
		      "border:2px solid;"
		      "margin-left:5px;"
		      "margin-right:5px;"
		      "\">");

	sb->safePrintf("<table width=100%% cellspacing=0 cellpadding=5>"
		      "<tr>"
		      "<td><b>SITE Backlink</b></td>"
		      "<td><b>Title</b></td>"
		      "<td><b>Link Text</b></td>"
		      "<td><b>Note</b></td>"
		      "<td><b>Weight</b></td>"
		      "<td><b>Links To</b></td>"
		      "</tr>\n"
		      );


	for ( long i = 0 ; i < pi->m_numReplyPtrs ; i++ ) {
		Msg20Reply *reply = pi->m_replyPtrs[i];
		char *txt = reply->ptr_linkText;
		if ( ! txt ) txt = "";
		char *title = reply->ptr_tbuf;
		if ( ! title ) title = "";
		char *note = reply->ptr_note;
		if ( ! note ) note = "<font color=green><b>good</b></font>";
		sb->safePrintf("<tr>"
			       "<td>");
		sb->safeTruncateEllipsis(reply->ptr_ubuf,50);
		sb->safePrintf("</td>"
			       "<td>" );
		sb->safeTruncateEllipsis(title,50);
		sb->safePrintf("</td>"
			       "<td>");
		sb->safeTruncateEllipsis(txt,50);
		sb->safePrintf("</td>"
			       "<td>%s</td>"
			       "<td>%li</td>"
			       "<td>"
			       , note
			       , (long)reply->m_inlinkWeight
			       );
		sb->safeTruncateEllipsis(reply->ptr_linkUrl,50);
		sb->safePrintf("</td>"
			       "</tr>\n"
			       );
	}

	sb->safePrintf("</table>\n");
	sb->safePrintf("</div>\n");
	sendPageBack ( sk );
}


// returns false and sets g_errno on error
void printMissingTerms ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printMissingTerms;

	// now get recommended links (link juice)
	SafeBuf *mtBuf = xd->getMissingTermBuf();
	if ( mtBuf == NULL ) { sendPageBack(sk); return; }
	if ( mtBuf == (void *)-1 ) return;

	if ( ! sk->m_xml ) {

		sb->safePrintf("<div "
			       "style=\""
			       "border:2px solid;"
			       "margin-left:5px;"
			       "margin-right:5px;"
			       "\">");
		
		sb->safePrintf("<table width=100%% cellspacing=0 "
			       "cellpadding=5>"
			       "<tr>"
			       "<td>#</td>"
			       "<td><b>Missing Term</b></td>"
			       "<td><b>Importance</b></td>"
			       "<td><b>Traffic of Related "
			       "Queries Term is In</b></td>"
			       //"<td><b># Competitors that Have It</b></td>"
			       //"<td><b>Traffic</b></td>"
			       //"<td><b># of Competing Pages that "
			       //"Match</b></td>"
			       "</tr>\n"
			       );
	}

	if ( sk->m_xml ) {
		sb->safePrintf (
				"<?xml version=\"1.0\" "
				"encoding=\"UTF-8\" ?>\n"
				"<response>\n"
				"\t<error>0</error>\n"
				);
	}

	char *p = mtBuf->getBufStart();
	char *pend = mtBuf->getBuf();
	long count = 0;
	for ( ; p < pend ; ) {
		// stop at 300?
		if ( count++ >= 300 ) break;
		// shortcut
		MissingTerm *mt = (MissingTerm *)p;
		p += mt->getSize();

		char *str = mt->getTerm();

		if ( ! sk->m_xml ) {
			sb->safePrintf("<tr valign=top>"
				       "<td>%li</td>"
				       "<td>%s"
				       ,count
				       ,str
				       );
		}
		else {
			sb->safePrintf("\t<missingTerm>\n"
				       "\t\t<term>"
				       "<![CDATA[%s]]>"
				       "</term>\n"
				       "\t\t<importance>%f</importance>\n"
				       "\t\t<monthlyTrafficOfRelated"
				       "QueriesTermIsIn>%lli"
				       "</monthlyTrafficOfRelated"
				       "QueriesTermIsIn>\n"
				       ,str
				       ,mt->m_importance
				       ,mt->m_traffic
				       );
		}

		// print the related queries that contained us
		//char *base = rqsb->getBufStart();
		for ( long i = 0 ; i < 10 ; i++ ) {
			//long qeoff = mt->m_queryOffsets[i];
			//if ( qeoff == -1 ) break;
			//QueryLogEntry *qe;
			//qe = (QueryLogEntry *)(base + qeoff);
			char *qstr = mt->getContainingQuery(i,xd);
			if ( ! qstr ) break;
			if ( sk->m_xml ) {
				sb->safePrintf("<relatedQuery>"
					       "<![CDATA[%s]]>"
					       "</relatedQuery>\n"
					       ,qstr
					       );
			}
			else {
				sb->safePrintf("<br>"
					       "&nbsp;"
					       "&nbsp;"
					       "<font size=-2>"
					       "because it matches <i>"
					       //"<a href=/search?q=%s>"
					       "%s"
					       //"</a>"
					       "</i>"
					       "</font>"
					       , qstr
					       );
			}
		}


		if ( ! sk->m_xml ) {
			sb->safePrintf("</td>"
				       "<td>%f</td>"
				       "<td>%lli</td>"
				       "</tr>\n"
				       ,mt->m_importance
				       ,mt->m_traffic
				       );
		}
		else {
			sb->safePrintf ( "\t</missingTerm>\n" );
		}
		
	}		

	if ( ! sk->m_xml ) {
		sb->safePrintf("</table>\n");
		sb->safePrintf("</div>\n");
	}

	sendPageBack ( sk );
}

char *MissingTerm::getContainingQuery ( long i , class XmlDoc *xd ) {
	long off = m_hackQueryOffsets[i];
	if ( off == -1 ) return NULL;
	if ( off < 0 ) { char *xx=NULL;*xx=0; }
	char *base;
	// we now use this class for both matching and missing terms
	if ( m_isMissingTerm ) {
		base = xd->m_relatedQueryBuf.getBufStart();
		QueryLink *qk = (QueryLink *)(base + off);
		SafeBuf *rqsb = &xd->m_relatedQueryStringBuf;
		QueryLogEntry *qe = qk->getQueryLogEntry(rqsb);
		return qe->getQueryStr();
	}
	else {
		base = xd->m_matchingQueryBuf.getBufStart();
		QueryLink *qk = (QueryLink *)(base + off);
		SafeBuf *rqsb = &xd->m_matchingQueryStringBuf;
		QueryLogEntry *qe = qk->getQueryLogEntry(rqsb);
		return qe->getQueryStr();
	}
}



//static SafeBuf *s_rdBuf = NULL;

int llCmp ( const void *a, const void *b ) {
	// these are offsets
	QueryLink *qa = *(QueryLink **)a;
	QueryLink *qb = *(QueryLink **)b;
	// get scores
	float scorea = qa->m_queryImportance;
	float scoreb = qb->m_queryImportance;
	if ( scorea < scoreb ) return  1; // swap!
	if ( scorea > scoreb ) return -1;
	// let serpscore break ties
	scorea = qa->m_serpScore;
	scoreb = qb->m_serpScore;
	if ( scorea < scoreb ) return  1; // swap!
	if ( scorea > scoreb ) return -1;

	// let docid break ties
	//long long da = qa->getRelatedDocId(s_rdBuf)->m_docId;
	//long long db = qb->getRelatedDocId(s_rdBuf)->m_docId;
	//if ( da < db ) return -1;
	//if ( da > db ) return  1; // swap
	return 0;
}


int finalrqCmp ( const void *a, const void *b ) {
	// these are offsets
	QueryLink *qa = *(QueryLink **)a;
	QueryLink *qb = *(QueryLink **)b;
	// this first
	//if ( qa->m_uniqueRound < qb->m_uniqueRound ) return -1;
	//if ( qa->m_uniqueRound > qb->m_uniqueRound ) return  1; // swap!
	// get scores
	float scorea = qa->m_totalQueryImportance;
	float scoreb = qb->m_totalQueryImportance;
	// try a new way
	scorea -= 1000.0 * qa->m_uniqueRound;
	scoreb -= 1000.0 * qb->m_uniqueRound;

	if ( qa->m_uniqueRound == 255 ) scorea = -9999.0;
	if ( qb->m_uniqueRound == 255 ) scoreb = -9999.0;

	if ( scorea < scoreb ) return  1; // swap!
	if ( scorea > scoreb ) return -1;
	//return 0;
	// let docidsincommon break ties
	return qb->m_docIdVotes - qa->m_docIdVotes;
}

////
//
// MATCHING QUERIES AND RELATED QUERIES ALGO
//
////

class State8e {
public:
	long m_i;
	UdpSlot *m_udpSlot;
	SafeBuf m_qkbuf;
	Msg20 m_msg20;
	bool m_doMatchingQueries;
	bool m_doRelatedQueries;
	long m_niceness;
	char *m_coll;
	long long *m_docIds;
	long m_numDocIds;
	long long m_sentMsg20ForDocId;
	long m_mainUrlTwidBufSize;
	char *m_mainUrlTwidBuf;
};

static void doProcessing8e ( void *st ) ;

void handleRequest8e ( UdpSlot *udpSlot , long netnice ) {

	// make a new state
	State8e *st;
	try { st = new (State8e); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("seo: new8e(%i): %s", 
		    sizeof(State8e),mstrerror(g_errno));
		g_udpServer.sendErrorReply ( udpSlot, g_errno );
		return;
	}
	mnew ( st, sizeof(State8e) , "st8e");

	st->m_udpSlot = udpSlot;
	st->m_niceness = netnice;

	// first long is the QueryLink offset that needs this query
	char *p    = (char *)udpSlot->m_readBuf;
	//char *pend = p + udpSlot->m_readBufSize;

	// are we called from XmlDoc::getMatchingQueryBuf() or from
	// XmlDoc::getRelatedQueryBuf()?
	st->m_doMatchingQueries = false;
	st->m_doRelatedQueries  = false;
	if ( *p ) st->m_doMatchingQueries = true;
	else      st->m_doRelatedQueries  = true;
	// skip that flag
	p++;

	// next is coll
	//char *coll = p;
	st->m_coll = p;
	p += gbstrlen(p) + 1;

	// then docids list
	long docIdListSize = *(long *)p;
	p += 4;
	st->m_docIds = (long long *)p;
	p += docIdListSize;

	st->m_numDocIds = docIdListSize / 8;

	// then main url syn/twid buf
	if ( st->m_doRelatedQueries ) {
		long twidBufSize = *(long *)p;
		p += 4;
		st->m_mainUrlTwidBufSize = twidBufSize;
		st->m_mainUrlTwidBuf     = p;
	}

	// sanity check
	if ( st->m_doMatchingQueries && st->m_numDocIds != 1 ) {
		char *xx=NULL;*xx=0;}

	st->m_i = 0;

	// clear these flags
	st->m_sentMsg20ForDocId = -1LL;

	// process them
	doProcessing8e ( st );

	/*
	log("seo: handlerequest8e phase 0");

	//
	//
	// 0. set termlistbuf ptrs for each docid we are doing
	//
	//
	//
	// request is a list of the termlistbufs for the related docids
	//
	char *tlistBufs [MAX_RELATED_DOCIDS];
	char *tlistEnds [MAX_RELATED_DOCIDS];
	long long docIds[MAX_RELATED_DOCIDS];
	long  numDocIds = 0;
	// buffers of 32-bit termids and syns contained in the document/docid
	char *tidBuf    [MAX_RELATED_DOCIDS];
	char *tidBufEnd [MAX_RELATED_DOCIDS];
	for ( ; p < pend ; ) {
		// . first is the querylink offset that wants this query
		// . we need this so we can set it back quickly
		long tlistBufSize = *(long *)p;
		p += 4;
		// save that
		tlistBufs[numDocIds] = p;
		// extract docid from first posdb key for this docid
		long firstListSize = *(long *)p;
		if ( firstListSize < 0 ) { char *xx=NULL;*xx=0; }

		// this can be zero i guess if no terms
		if ( firstListSize == 0 ) {
			docIds[numDocIds] = 999999;
		}
		else {
			char *key = p + 4;
			docIds[numDocIds] = g_posdb.getDocId(key);
		}

		// skip the whole termlistbuf for this docid
		p += tlistBufSize;
		tlistEnds[numDocIds] = p;

		//
		// now the termidbuf, not a termlist buf of termlists, but
		// a list of the wordids and their syns that the doc has,
		// all 32-bits. these are sorted. these are used to find
		// what queries we match now.
		//
		long size = *(long *)p;
		p += 4;
		tidBuf  [numDocIds] = p;
		p += size;
		tidBufEnd [numDocIds] = p;
		

		numDocIds++;
		// sanity breach check
		if ( numDocIds > MAX_RELATED_DOCIDS ) { 
			char *xx=NULL;*xx=0; }
	}
	*/

}


void doProcessing8e ( void *stk ) {

	State8e *st = (State8e *)stk;

	bool debug = false;

	// on error send back the g_errno
	long err = 0;
	if ( err ) {
	hadError:
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// sanity
		if ( st->m_msg20.m_inProgress ) { char *xx=NULL;*xx=0; }
		mdelete ( st , sizeof(State8e) , "st8e" );
		delete (st);
		g_udpServer.sendErrorReply ( st->m_udpSlot, g_errno );
		return;
	}
		

	// store QueryLinks into here, for all related docids
	SafeBuf *qkbuf = &st->m_qkbuf;

	long niceness = st->m_niceness;
	bool doRelatedQueries = st->m_doRelatedQueries;
	bool doMatchingQueries = st->m_doMatchingQueries;
	char *coll = st->m_coll;

	//
	// now scan each [related] docid
	//
	for ( ; st->m_i < st->m_numDocIds ; st->m_i++ ) {

		QUICKPOLL(niceness);

		// get it
		long long docId = st->m_docIds[st->m_i];

		// get the msg20 to get its termlistbuf
		if ( st->m_sentMsg20ForDocId != docId ) {
			// do not re-request for this docid
			st->m_sentMsg20ForDocId = docId;
			// make the request
			Msg20Request req;
			req.ptr_coll    = coll;
			req.size_coll   = gbstrlen(coll)+1;
			req.m_docId     = docId;
			req.m_expected  = true;
			req.m_niceness  = niceness;
			req.m_state     = st;
			req.m_callback2 = doProcessing8e;
			// do not get summary stuff. too slow.
			req.m_numSummaryLines = 0;
			// get this. gets both the termlistbuf and termid32buf
			req.m_getTermListBuf = true;
			// launch it
			if ( ! st->m_msg20.getSummary ( &req ) ) 
				// return if it blocked
				return;
			// error?
			if ( ! g_errno ) { char *xx=NULL;*xx=0; }
			// note it
			log("seo: error getting termlistbuf docid=%lli",
			    docId);
			// launch another i guess
			continue;
		}

		// get the termlistbuf
		Msg20Reply *reply = st->m_msg20.getReply();
		if ( ! reply ) {
			log("seo: 8e: msg20 #%li docid=%lli had error: %s",
			    st->m_i,docId,mstrerror(g_errno));
			continue;
		}

		char *tlistBuf    = reply->ptr_tlistBuf;
		char *tlistBufEnd = reply->size_tlistBuf + tlistBuf;

		char *tidBuf     = reply->ptr_tiBuf;
		char *tidBufEnd  = reply->size_tiBuf + tidBuf;


		// . scoring matching queries for docid #i...
		// . if seo.o is compiled with -O3 this is like 3.2x faster!!
		// . average seems like 500ms per docid with -O3... just
		//   on titan, which should be about half on a 128 node
		//   cluster since titan has a 1/64th g_qlog?
		log("seo: handlerequest8e phase 0, docid #%li of %li",
		    st->m_i,st->m_numDocIds);

		//
		// 1. add matching queries as QueryLinks into safebuf
		//


		// hash the 32-bit termids that are in the document.
		// maybe just in the title and inlink text for now, maybe the
		// body later... or maybe gigabits later...
		HashTableX qht;

		// . the termlist buf is not quite just a list of the twids
		// . it is a set of posdblists to make doing the msg39
		//   calls a lot easier. but we can harvest the twids from
		//   them.

		// i think each word/bigram in doc is a full 18 bytes 
		// as provided by XmlDoc::getTermListBuf()
		long numSlots = tidBufEnd - tidBuf;
		if ( ! qht.set (4,0,numSlots,NULL,0,false,niceness,"qhttab")) 
			goto hadError;
		unsigned long last32 = 0;
		// scan the termlists buf, it is not quite a linear scan
		// because we have termlist size info before each termlist
		// that represents the keys for one termid
		unsigned long *twidPtr2 = (unsigned long *)tidBuf;
		unsigned long *twidEnd2 = (unsigned long *)tidBufEnd;
		for ( ; twidPtr2 < twidEnd2 ; twidPtr2++ ) {
			// breathe
			QUICKPOLL(niceness);
			// get it
			unsigned long tid = *twidPtr2;
			// add that termid
			if ( ! qht.addKey( &tid ) ) goto hadError;
			// ensure sorted! make sure XmlDoc::getTermListBuf()
			// sorts posdb keys by lower 32-bits of termid. saves
			// time!
			if ( tid < last32 ) { char *xx=NULL;*xx=0; }
			last32 = tid;
		}

		//
		//
		// 2. SETUP for computing serp score
		//
		//
		HashTableX plistTable;
		SafeBuf listBuf;
		// set m_plistTable to map a termId to our docids posdblist
		if ( ! setPosdbListTable ( &plistTable ,
					   &listBuf,
					   tlistBuf,
					   tlistBufEnd - tlistBuf,
					   docId ,
					   niceness ) ) 
			goto hadError;
		// make a msg39 request for each one
		Msg39Request mr;
		mr.reset();
		mr.ptr_coll   = coll;
		mr.size_coll  = gbstrlen(coll)+1;
		mr.m_queryExpansion = 1;
		// temporarily set it to english until we know the query
		mr.m_language = langEnglish;
		mr.m_niceness = niceness;
		// turn these off for speed
		mr.m_getDocIdScoringInfo = false;
		mr.m_doSiteClustering    = false;
		mr.m_doDupContentRemoval = false;
		mr.m_fastIntersection  = 1;
		// just one result, our url!
		mr.m_docsToGet = 1;
		mr.m_getSectionStats = false;
		// we provide our own posdb termlists...
		mr.ptr_readSizes  = NULL;
		mr.size_readSizes = 0;
		// turn debugging off for now
		bool mydebug = false;
		// debug
		if ( mydebug ) {
			mr.m_debug = 1;
			mr.m_getDocIdScoringInfo = true;
		}
		// we'll set this below
		Query q;
		// get score as it is now
		PosdbTable posdbTable;
		// initialize it
		posdbTable.init ( &q ,
				  false, // debug // isDebug?
				  NULL, // logState
				  NULL, // top tree
				  coll, // collection
				  NULL, // msg2
				  &mr ); // msg39Request (search parms)
		// also need this from PosdbTable::allocTopTree()
		if ( mydebug ) {
			PosdbTable *pt = &posdbTable;
			// reset buf ptr, but do not free mem
			// alloc mem if not already there
			pt->m_scoreInfoBuf.reserve ( 5*sizeof(DocIdScore) );
			// # of possible score pairs
			pt->m_pairScoreBuf.reserve ( 50*sizeof(PairScore) );
			// # of possible singles
			pt->m_singleScoreBuf.reserve ( 50*sizeof(SingleScore));
		}

		//
		//
		// 3. scan local query log for matching queries
		//
		//
		register unsigned long *qids;
		char *qstr;
		long numStopWords;
		long k;
		// point to the local slice of the query log
		char *p    = g_qbuf.getBufStart();
		char *pend = p + g_qbuf.length();
		// list size of query records that begin with the same 
		// minimum wordid in g_qbuf
		long listSize = *(long *)p;
		p += 4;
		char *nextList = p + listSize;
		unsigned long *twidPtr = (unsigned long *)tidBuf;
		unsigned long *twidEnd = (unsigned long *)tidBufEnd;
		// no twids? don't core then
		if ( ! twidPtr ) p = pend;
		//twidPtr = (unsigned long *)twidBuf32.getBufStart();
		//twidEnd = (unsigned long *)twidBuf32.getBuf();
		// skip stop words in the list of 32-bit wordids that 
		// this docid contains/indexes
		for ( ; twidPtr < twidEnd ; twidPtr++ )
			if ( ! isStopWord32(*twidPtr) ) break;
		// all stop words? don't core
		if ( twidPtr >= twidEnd ) p = pend;
		// breathe
		QUICKPOLL(niceness);
		// if we have no twins this is always 0, otherwise if we use
		// twins it is 0 or 1, triplies it is 0, 1 or 2 , etc.
		long myStripe = g_hostdb.m_myHost->m_stripe;
		// scan the local query log linearly
		for ( ; p < pend ; ) {
			// point to the next QueryLogEntry in it
			QueryLogEntry *qe = (QueryLogEntry *)p;
			// get the 32-bit hashes of the query words for this 
			// query in the log. the first qid in this array is 
			// what the queries are sorted by and is the minimal 
			// wordid of all terms in the query.
			qids = qe->m_queryTermIds32;
			// . quick check of first term, if not in twids, skip
			//   query
			// . no! keep the twids vector sorted so we can 
			//   compare easily because a hashtable lookup is too 
			//   expensive here
		subloop:
			// breathe
			QUICKPOLL(niceness);
			if ( qids[0] < *twidPtr ) {
				// skip entire list of queries for which this 
				// is the minimum termid (non stop-word)
				p += listSize;
				// next list size
				listSize = *(long *)p;
				// skip list size itself
				p += 4;
				// update this
				nextList = p + listSize;
				continue;
			}
			if ( qids[0] > *twidPtr ) {
			skiptwid:
				// see if we can match it
				if ( ++twidPtr >= twidEnd ) 
					break;
				// also skip stop words. like 'the' is 
				// contained by too many queries!! so just look
				// at the non-stop words. and we also make sure
				// that the "qids[]" array puts stop words 
				// termids at the back even if they are the 
				// minimum hash! (see qlogcmp())
				if ( isStopWord32(*twidPtr) ) 
					goto skiptwid;
				// loop back
				goto subloop;
			}
			// get # of query terms
			long qn = qe->m_qn; // (unsigned char)*p;
			// skip the query log entry now
			p += qe->getSize();
			// end of list?
			if ( p == nextList ) {
				listSize = *(long *)p;
				p += 4;
				nextList = p + listSize;
			}
			// which twin will handle this query?
			long stripe = qids[0] % g_hostdb.getNumHostsPerGroup();
			if ( stripe != myStripe ) continue;
			// or if he has zero results! wtf?
			// 'design and' query and i think maybe 'able 5' have
			// this because everyone was ranking like #1 spot for 
			// them so we got those guys coming up high in the 
			// matching and related query tables!
			//if ( qe->m_numTotalResultsInSlice <= 0 ) continue;
			// count these
			numStopWords = 0;
			// does it match us?
			for ( k = 0 ; k < qn ; k++ ) {
				// stop if not in twids
				if ( qht.getSlot(&qids[k]) < 0 ) break;
				// if all query terms are stop words then
				// ignore the query because it looks kinda
				// crap to have crap like "to" or "to to" 
				// or "and the" in there.
				// why is 'from+a' not triggering this?
				if ( isStopWord32(qids[k]) ) 
					numStopWords++;
			}
			// skip p beyond
			qstr = qe->getQueryStr();
			// save this
			long qbufOffset = ((char *)qe) - g_qbuf.getBufStart();
			//log("seo:q=%s",qstr);
			// bail if not all matched
			if ( k < qn ) continue;
			// or if all are stop words
			if ( numStopWords == qn ) continue;
			// if query is badly formatted, just skip it then
			if ( ! verifyUtf8(qstr) ) 
				continue;
			// skip if it has negative query terms
			char *p; for ( p = qstr; *p ; p++ ) {
				if ( *p   != ' ' ) continue;
				if ( p[1] != '-' ) continue;
				// 'a - b' is ok
				if ( p[2] == ' ' ) continue;
				break;
			}
			if ( *p ) continue;
			// 
			// GOT MATCHED QUERY for this docid!!! 
			//
			// store into qkbuf for now.
			//
			// alloc 1MB at a time for efficiency
			if ( qkbuf->getAvail() < (long)sizeof(QueryLink) &&
			     ! qkbuf->reserve ( 1000000) ) 
					goto hadError;
			// ref it
			QueryLink *qk = (QueryLink *)qkbuf->getBuf();
			// init it
			memset ( qk , 0 , sizeof(QueryLink) );
			// remember query offset we represent
			qk->m_queryStringOffset = qbufOffset;
			// and what related docid matches us
			qk->m_relatedDocIdNum   = st->m_i;

			//
			// 
			// 4. set the QueryLink::m_serpScore for it
			//
			//

			QUICKPOLL(niceness);
			long qlen = gbstrlen(qstr);
			mr.ptr_query  = qstr;
			mr.size_query = qlen+1;
			// TODO: should we also store the query language id???
			uint8_t qlangId = qe->m_langId;
			if ( qlangId == langUnknown ) qlangId = langEnglish;
			mr.m_language = qlangId;
			// . stuff below here taken from Msg3a.cpp
			// . we need to set termfreq weights. 
			//   true = synonymExpansion?
			// . TODO: what are these termfreqs???
			// . TODO: need to send over termfreq map as well?
			q.set2 ( qstr , qlangId , true );
			long numTerms = q.getNumTerms();
			RdbList rdbLists[MAX_QUERY_TERMS];
			// point to the termlists relevant to the query that 
			// were provided in the request as m_termListBuf from 
			// XmlDoc::getTermListBuf().
			// set QueryTerm::m_posdbListPtr i guess and use that
			// in intersectLists10_r()
			setQueryTermTermList ( &q , &plistTable , rdbLists );
			mr.m_nqt = numTerms;
			// need termFreqWeights
			float termFreqWeights [MAX_QUERY_TERMS*8];
			setTermFreqWeights ( coll,&q,NULL,termFreqWeights);
			mr.ptr_termFreqWeights = (char *)termFreqWeights;
			mr.size_termFreqWeights = sizeof(float) * numTerms;
			// set posdb termlist for each query term
			for ( long i = 0; i < numTerms ; i++ ) {
				// shortcut
				QueryTerm *qt = &q.m_qterms[i];
				// if we are scoring queries for a single 
				// docid, then assign the list ptrs here
				qt->m_posdbListPtr = &rdbLists[i];
			}

			// . needs this before calling intersect
			// . was called in PosdbTable::allocTopTree but we do 
			//   not need that
			if ( ! posdbTable.setQueryTermInfo() )	goto hadError;
			QUICKPOLL(niceness);
			if ( mydebug ) {
				posdbTable.m_scoreInfoBuf.reset();
				posdbTable.m_pairScoreBuf.reset();
				posdbTable.m_singleScoreBuf.reset();
			}
			// debug note
			//if ( g_conf.m_logDebugSEOInserts )
			//	log("seo: executing query to get origscore" );
			// hack set
			posdbTable.m_docIdHack = docId;
			// get the score for our url on this query
			posdbTable.intersectLists10_r();
			// get the final score
			float finalScore = posdbTable.m_finalScore;
			// store it
			qk->m_serpScore = finalScore;
			// put this up here to see why 'to light' for docid 
			// 34774131193 score is so low
			//log("seo: returning qstr=%s docid=%lli score=%f",
			//    qstr,docId,finalScore);
			if ( mydebug ) {
				log("seo: returning qstr=%s docid=%lli "
				    "score=%f", qstr,docId,finalScore);
				// try to print winning pair
				logScoreInfo ( &posdbTable.m_scoreInfoBuf,
					       &mr ,
					       finalScore,
					       &q);
			}
			// if related docid got a score of zero, skip it. it 
			// was likely a query like 'track-track' that had a 
			// hyphen and the docid did not have that PHRASE, but 
			// it was selected as a matching query because the 
			// docid had the word 'track' in it's twids.
			if ( finalScore == 0.0 ) continue;
			// otherwise add it
			qkbuf->incrementLength ( sizeof(QueryLink) );
			//
			//
			// end setting QueryLink::m_serpScore
			//
			//

		} // matching query loop

	}  // docid loop




	// next is our main url's twids for setting twidTable, but only
	// if doing related queries. it is used to set 
	// QueryLink::m_uniqueRound. we need to know what terms in the related
	// query are contained in the main url so we only put the missing
	// terms in bold. this twidbuf should also have all the synonyms
	// of the terms contained by the main url.
	HashTableX twidTable;
	if ( st->m_doRelatedQueries ) {
		//long twidBufSize = *(long *)p;
		//p += 4;
		//long numTwids = twidBufSize / 4;
		//long *twid32s = (long *)p;
		//p += twidBufSize;
		long *twid32s = (long *)st->m_mainUrlTwidBuf;
		long numTwids = st->m_mainUrlTwidBufSize / 4;
		// this table is for seeing if we have a term in a related qry
		if ( ! twidTable.set(4,0,numTwids*4,NULL,0,false,
				      niceness,"twt8e") )
			goto hadError;
		for ( long i = 0 ; i < numTwids ; i++ )
			if ( ! twidTable.addKey(&twid32s[i]) ) 
				goto hadError;
	}



	log("seo: handlerequest8e phase 5");


	long nks = qkbuf->length() / sizeof(QueryLink);
	QueryLink *qks = (QueryLink *)qkbuf->getBufStart();

	long long numPagesIndexed = g_titledb.getGlobalNumDocs();
	// take 25% of that. i think 'the', the most common term, is in about
	// 25% of those pages
	//numPagesIndexed /= 4;
	// first point is about 10M to 80M pages
	long long point0 = numPagesIndexed / 119LL;
	long long point1 = numPagesIndexed / 15LL;



	//
	//
	// 5.a. set QueryLink::m_queryImportance for m_relatedQueryBuf
	//
	//

	if ( doRelatedQueries ) {
		// init the table for accumulating docid vote counts
		HashTableX vt;
		long numSlots = nks * 2;
		if ( ! vt.set(4,4,numSlots,NULL,0,false,niceness,"8evt") )
			goto hadError;
		// accumulate scores in here
		HashTableX sc;
		if ( ! sc.set(4,4,numSlots,NULL,0,false,niceness,"8esc") )
			goto hadError;
		//
		// set QueryLink::m_queryImportance for each related query
		// and add them up for the total
		//
		for ( long i = 0 ; i < nks ; i++ ) {
			QUICKPOLL(niceness);
			QueryLink *qk = &qks[i];
			QueryLogEntry *qe;
			qe = qk->getQueryLogEntry(&g_qbuf);
			float goodScore = qe->m_topSERPScore;
			// get weight of the related docid
			//float score = rd->m_relatedWeight;
			// try without related docid weight...
			float score = 1.0;
			// queries like '00.00' for some reason have
			// a 0 top serp score! wtf? therefore they
			// end up in the top of the related queries...
			if ( goodScore <= 0.0 ) {
				score = 0.0;
			}
			// . fix 'design and' (goodScore=.007688)
			// . fix 'able 5' (gooScore=.000172)
			else if ( goodScore <= .008 ) {
				score = 0.0;
			}
			else if ( qk->m_serpScore > goodScore ) {
				score *= 100.0;
				// fix google.com adobe.com etc. from pushing
				// really generic queries like
				// "the here and now" 
				//long sr = rd->m_rd_siteRank;
				// made this 12 instead of 13 
				//if      ( sr >= 13 ) score *=  .1;
				//else if ( sr >= 12 ) score *=  .2;
				//else                 score *= 100.0;
			}
			else {
				// this weight will be <= 1.0 and will
				// hopefully nuke common queries like "a's"
				// from dominating this part of the algo
				score *= qk->m_serpScore / goodScore;
			}

			// how many search results does this query have total?
			long long numResults = qe->m_numTotalResultsInSlice;
			// fix it to be global
			numResults *= (long long)g_hostdb.getNumGroups();
			// big indexes did the "slice logic" restricting docid
			// range to MAX_DOCID * .10 when setting this!
			if ( numPagesIndexed > 10000000 ) numResults *= 10;
			// zero means make it 1 to avoid div by zero below
			if ( numResults == 0 ) numResults = 1;
			// new weight scheme
			float weight = 1.0;
			if      ( numResults < point0 ) weight = 100.0;
			else if ( numResults < point1 ) weight = 10.0;
			// save it for debug printing
			qk->m_queryImportance = score * weight;
			// make a hash
			long qoff = qk->m_queryStringOffset;
			//long h32 = hash32((char *)&qoff,4);
			// count # of docids that match this query
			if  ( ! vt.addTerm32 ( &qoff , 1 ) ) goto hadError;
			// accumulate the score
			if ( ! sc.addFloat(&qoff,score) ) goto hadError;
			// skip debug stuff for now?
			if ( ! debug ) continue;
			// print out debug stuff if &debug=1 was given
			char *qstr = qe->getQueryString();
			// only debug this one query here
			//if ( ! strstr(qstr,"walkthrough") ) continue;
			log("seo: DEBUG. got vote for '%s' (qoff=%li) from "
			    "docidnum=%li score=%f "
			    "%f vs %f"// (wght=%f)",
			    ,qstr
			    ,qoff
			    ,(long)qk->m_relatedDocIdNum
			    ,score
			    ,qk->m_serpScore
			    ,goodScore
			    //,rd->m_relatedWeight);
			    );
		}

		log("seo: handlerequest8e phase 5b");

		//
		// set QueryLink::m_totalQueryImportance and m_docIdVotes
		//
		for ( long i = 0 ; i < nks ; i++ ) {
			QUICKPOLL(niceness);
			QueryLink *qk = &qks[i];
			long qoff = qk->m_queryStringOffset;
			// make a hash
			//long h32 = hash32((char *)&qoff,4);
			long score = vt.getScore32 ( &qoff );
			qk->m_docIdVotes = score;
			float score2 = *(float *)sc.getValue(&qoff);
			qk->m_totalQueryImportance = score2;
		}
	}


	//
	//
	// 5.b. set QueryLink::m_queryImportance for m_matchingQueryBuf
	//
	//

	// otherwise, this MUST be true. set QueryLink::m_queryImportance
	for ( long i = 0 ; doMatchingQueries && i < nks ; i++ ) {
		//if ( ! doMatchingQueries ) break;
		QUICKPOLL(niceness);
		QueryLink *qk = &qks[i];
		// tis irrelevant for matchingquerybuf
		qk->m_docIdVotes = 0;
		// default to 0.0
		qk->m_queryImportance = 0.0;
		// this has the query string and stats
		QueryLogEntry *qe = qk->getQueryLogEntry(&g_qbuf);
		// how many search results does this query have in total?
		long long numResults = qe->m_numTotalResultsInSlice;
		// fix it to be global
		numResults *= (long long)g_hostdb.getNumGroups();
		// big indexes did the "slice logic" restricting docid
		// range to MAX_DOCID * .10 when setting this!
		if ( numPagesIndexed > 10000000 ) numResults *= 10;
		// point to query
		char *qstr = qe->getQueryString();
		// if not processed assume like 1M?
		if ( numResults < 0 ) {
			log("seo: guessing query importance for '%s'",qstr);
			continue;
		}
		// new weight scheme
		float weight = 1.0;
		if      ( numResults < point0 ) weight = 100.0;
		else if ( numResults < point1 ) weight = 10.0;
		// the idea is to ignore the top serp score because
		// you do not want terms that you may be able to be #1
		// for but are not really relevant for your doc. so for this
		// let's focus on just getting the queries that best represent
		// your doc...
		double imp = qk->m_serpScore * weight;
		// set importance to 0 for queries with minus sign in them
		// that indicates negative terms...
		for ( char *p = qstr; *p ; p++ ) {
			if ( *p   != ' ' ) continue;
			if ( p[1] != '-' ) continue;
			// 'a - b' is ok
			if ( p[2] == ' ' ) continue;
			imp = 0.00;
			log("seo: ignoring query '%s' with minus sign", qstr);
			break;
		}
		// avoid common queries with just common words in them:
		// http web www com org us we 1 2 3 by on i https one page 
		Words ww;
		ww.set3 ( qstr );
		long i; for ( i = 0 ; i < ww.m_numWords ; i++ ) {
			long long wid = ww.m_wordIds[i];
			if ( wid == 0 ) continue;
			if ( ! isCommonQueryWordInEnglish ( wid ) ) break;
		}
		if ( i >= ww.m_numWords ) {
			imp = 0.00;
			log("seo: ignoring common query '%s'", qstr);
		}
		// give slight bonus to single word queries to fix diffbot.com
		// from having 'to extract' come above 'extract'
		if ( ww.m_numWords <= 2 ) 
			// . a 2% boost seems ample
			// . 1% misses 'or test' for diffbot.com
			imp *= 1.02; 
		// now set it
		qk->m_queryImportance = (float)imp;
		qk->m_totalQueryImportance = (float)imp;
		qk->m_uniqueRound = 0;
		// skip debug for now
		if ( ! debug ) continue;
		// note it
		log("seo: "
		    "imp=%f "
		    "numresults=%lli "
		    "numpagesindexed=%lli "
		    "popweight=%f "
		    "myscore=%f "
		    "topscore=%f "
		    "qstr=%s", 
		    qk->m_queryImportance,
		    numResults,
		    numPagesIndexed,
		    weight,
		    qk->m_serpScore,
		    qe->m_topSERPScore,
		    qstr);
	}


	log("seo: handlerequest8e phase 6");

	//
	// 
	// 6. move winners into filterbuf (> MIN_DOCID_VOTES)
	//
	//
	SafeBuf filterBuf;
	// get top 300 highest scoring querylinks
	if ( doRelatedQueries ) {
		long fneed = qkbuf->length();
		if ( ! filterBuf.reserve(fneed,"rqli") ) 
			goto hadError;
		for ( long i = 0 ; i < nks ; i++ ) {
			// breathe
			QUICKPOLL(niceness);
			// get the ith related query
			QueryLink *qk = &qks[i];
			// now we have this. it was 10 as of 1/21/2013
			if ( qk->m_docIdVotes < MIN_DOCID_VOTES ) continue;
			// store it
			if ( ! filterBuf.safeMemcpy ( qk , sizeof(QueryLink)))
				goto hadError;
		}
	}
	// move other buf into filter buf if doing matching
	if ( doMatchingQueries ) {
		filterBuf.stealBuf ( qkbuf );
	}

	// qkbuf not needed now, filterbuf has all, so save some mem
	qkbuf->purge();

	// re-assign to tmpBuf. since we stored just the best ones in there
	qks = (QueryLink *)filterBuf.getBufStart();
	nks = filterBuf.length() / sizeof(QueryLink); 
	
	//
	//
	// 7. make linked lists
	//
	//

	// ft = "first table"
	HashTableX ft;
	if ( doRelatedQueries &&
	     ! ft.set(4,4,100000,NULL,0,false,niceness,"dxxx")) 
		goto hadError;
	SafeBuf headPtrs;
	if ( ! headPtrs.reserve ( nks*2 ) )
		goto hadError;
	// debug declarations
	QueryLink *first;
	// make the linked list if their query matches
	for ( long i = 0 ; i < nks ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// get it
		QueryLink *qk = &qks[i];
		// matching queries querylinks are all heads, no linked
		// list since only a single docid's worth of matching queries
		if ( doMatchingQueries ) {
			// for sanity
			qk->m_isFirst = 1;
			// reserve space
			if (   headPtrs.getAvail() < 4 &&
			     ! headPtrs.reserve(1000000,"tlptr") )
				goto hadError;
			// store ptr to the linked list head so we can sort
			// the related queries by score later
			if ( ! headPtrs.pushLong ( (long)qk ) ) 
				goto hadError;
			continue;
		}
		// debug hardcore here
		//char *qstr2 = qk->getQueryString(&m_tmpStringBuf5);
		//RelatedDocId *rd = qk->getRelatedDocId(rdbuf);
		//log("DEBUG2: score=%f rdocid=%lli qstr=%s",
		//    qk->m_myScore,rd->m_docId,qstr2);
		// or a tiny score
		//if ( qk->m_serpScore < 1.0 ) continue;
		// get query string
		//char *qstr = qk->getQueryString(&m_tmpStringBuf5);
		// i guess we do not have the hash stored
		long qh32 = qk->m_queryStringOffset;
		// . get first msg98reply with that query
		// . the Msg98Reply 
		char *val = (char *)ft.getValue(&qh32);
		// if not there, we are it!
		if ( ! val ) {
			// store in table
			if ( ! ft.addKey(&qh32,&qk) ) goto hadError;
			// flag it as the linked list head for this query
			qk->m_isFirst =  1;
			// HACK! stored linked list in here temporarily
			qk->m_numInList = -1;
			// alloc in 1MB chunks for efficiency
			if (   headPtrs.getAvail() < 4 &&
			     ! headPtrs.reserve(1000000,"tlptr") )
				goto hadError;
			// store ptr to the linked list head so we can sort
			// the related queries by score later
			if ( ! headPtrs.pushLong ( (long)qk ) ) 
				goto hadError;
			continue;
		}
		// now we use offsets
		long qkOff = (char *)qk - (char *)&qks[0];
		// get first msg98reply with this query hash 
		first = *(QueryLink **)val;
		// HACK! use m_numInList for a linked list temporarily
		long hackOff = first->m_numInList;
		// if a querylink already follows the head, we have to
		// insert ourselves here...
		if ( hackOff >= 0 ) {
			QueryLink *next ;
			next = (QueryLink *)(filterBuf.m_buf + hackOff);
			// make us the next guy (linked list HACK)
			first->m_numInList = qkOff;
			// and our next is the old next (linked list HACK)
			qk->m_numInList = hackOff;
		}
		else {
			// otherwise, we are 2nd in the list
			first->m_numInList = qkOff;
			// nobody follows us
			qk->m_numInList = -1;
		}
		// it is not the head, it is in the linked list somewhere
		qk->m_isFirst = 0;
	}	

	log("seo: handlerequest8e phase 8");


	//
	//
	// 8. set QueryLink::m_uniqueRound
	//
	//

	HashTableX uniqueTable;
	if ( doRelatedQueries &&
	     ! uniqueTable.set(4,4,2048,NULL,0,false,niceness,"uqttbl") )
		goto hadError;

	long numPtrs = headPtrs.length() / 4;
	QueryLink **ptrs = (QueryLink **)headPtrs.getBufStart();

	// . this time scan the related queries we got
	//   sorted by m_totalQueryImportance. get the unique word
	//   not contained by our main url. see how many times we've 
	//   encountered it. in case of multiple such words, check each one 
	//   and get the min count. then set qk->m_uniqueRound to that count
	//   and increment that count in the table.
	// . check to see if the query word or its synonyms are in our twids
	//   for this doc.
	for ( long k = 0 ; doRelatedQueries && k < numPtrs ; k++ ) {
		// breathe
		QUICKPOLL(niceness);
		// get it
		QueryLink *qk = ptrs[k];
		// sanity. must be a linked list head if in tmpLinkPtrs.
		if ( ! qk->m_isFirst ) { char *xx=NULL;*xx=0; }
		if ( qk->m_queryStringOffset < 0 ) { char *xx=NULL;*xx=0; }
		// get the query info
		QueryLogEntry *qe ;
		qe = qk->getQueryLogEntry(&g_qbuf);
		// see what words are not-common and not in twid table.
		// we have their 32-bit hashes which should be fine especially
		// since we are using a 32-bit twid table for lookup!
		long minCount = 9999999;
		long minSlot  = -2;
		long minTid32;
		for ( long i = 0 ; i < qe->m_qn ; i++ ) {
			// shortcut
			long tid32 = qe->m_queryTermIds32[i];
			// skip if common
			if ( isCommonQueryWordInEnglish(tid32) )
				continue;
			// skip if in our table. that means our doc already
			// has indexed this term.
			if ( twidTable.isInTable(&tid32) ) 
				continue;
			// get count
			long slot = uniqueTable.getSlot(&tid32);
			long count = 0;
			if ( slot >= 0 ) count = *(long *)uniqueTable.
						 getValueFromSlot(slot);
			if ( count >= minCount ) continue;
			minCount = count;
			minSlot  = slot;
			minTid32 = tid32;
		}
		// wtf!?!? it is supposed to have at least one term
		// that our doc does not have!!
		if ( minSlot == -2 ) {
			char *qstr = qe->getQueryStr();
			if ( debug )
				log("seo: filtering related query '%s' "
				    "because it is matched by us",qstr);
			// . throw it to end of list
			// . this is a single byte, so max is 255 i guess
			qk->m_uniqueRound = 255;//9999;
			continue;
		}
		// first occurence of the term
		if ( minSlot == -1 ) {
			if ( ! uniqueTable.addTerm32(&minTid32) )
				goto hadError;
		}
		else {
			// inc that count
			long *val ;
			val = (long *)uniqueTable.getValueFromSlot ( minSlot );
			*val = *val + 1;
		}
		// set unique round. we'll re-sort first by this when done.
		qk->m_uniqueRound = minCount;
	}
	// free the mem now
	uniqueTable.reset();

	//
	//
	// 9. sort QueryLink ptrs using m_uniqueRound AND m_queryImportance
	//
	//

	log("seo: handlerequest8e phase 8");

	gbqsort ( ptrs,numPtrs,sizeof(long),finalrqCmp,niceness);


	//
	//
	// 10. truncate to top MAX_RELATED_QUERIES and store QueryLogEntries
	//
	//
	
	// truncate to 300 QueryLinks,  these are heads of linked lists
	long maxPtrs = MAX_MATCHING_QUERIES;
	if ( doRelatedQueries ) maxPtrs = MAX_RELATED_QUERIES;
	if ( numPtrs > maxPtrs ) numPtrs = maxPtrs;

	//
	//
	// 11. create string buf to hold QueryLogEntries to send back in reply
	//
	//

	log("seo: handlerequest8e phase 11");


	SafeBuf stringBuf;
	if ( ! stringBuf.reserve(numPtrs * sizeof(QueryLogEntry),"8estrb") )
		goto hadError;
	for ( long k = 0 ; k < numPtrs ; k++ ) {
		// now we use offsets into m_relatedQueryBuf.m_buf[]
		QueryLink *qk = ptrs[k];
		// all in here must be heads of linked list of queryRels.
		if ( ! qk->m_isFirst ) { char *xx=NULL;*xx=0; }
		if ( qk->m_queryStringOffset < 0 ) { char *xx=NULL;*xx=0; }
		// get it
		QueryLogEntry *qe = qk->getQueryLogEntry(&g_qbuf);
		// offset
		long newOff = stringBuf.length();
		// store it
		if ( ! stringBuf.safeMemcpy(qe,qe->getSize())) 
			goto hadError;
		// store offset
		qk->m_queryStringOffset = newOff;
	}

	//
	//
	// 12. copy just the QueryLinks into xbuf and sort linked lists
	//
	//
	//

	log("seo: handlerequest8e phase 12");

	// . copy each linked list of QueryLinks
	// . each ptrs[i] only points to QueryLink whose m_first is true,
	//   meaning he is head of the linked list AND he passed the
	//   MIN_DOCID_VOTES limiter above, otherwise m_first is 0.
	// . probably fewere than 50 QueryLinks per linked list, so this
	//   should be a good upper bound. especially since we only have
	//   50 related docids we are working with?
	SafeBuf xbuf;
	long need = numPtrs * 50 * sizeof(QueryLink);
	if ( ! xbuf.reserve(need,"tmpbbb") ) goto hadError;
	// scan them. these are guaranteed all linked list heads, sorted
	// by the QueryLink::m_totalQueryImportance
	for ( long k = 0 ; k < numPtrs ; k++ ) {
		// now we use offsets into m_relatedQueryBuf.m_buf[]
		QueryLink *qk = ptrs[k];
		// just copy in the case of plain old matching queries
		// for our single main docid
		if ( doMatchingQueries ) {
			if ( ! xbuf.safeMemcpy ( qk,sizeof(QueryLink) ) )
			     goto hadError;
			continue;
		}
		// save this
		QueryLink *first = qk;
		// sanity
		if ( ! first->m_isFirst ) { char *xx=NULL;*xx=0; }
		// make array of ptrs to each element in linked list for 
		// sorting QueryRels in the linked list
		QueryLink *lptrs[512];
		long uu = 0;
		// all the QueryLinks are stored in the m_modStringBuf here
		for ( ; ; uu++ ) {
			// we only have like 50 related docids so i don't
			// think i can be more than 50!?!?!
			if ( uu >= 510 ) break;
			lptrs[uu] = qk;
			// copy stuff from the head! make it all legit.
			qk->m_uniqueRound = first->m_uniqueRound;
			qk->m_queryStringOffset = first->m_queryStringOffset;
			// advance. (linked list hack)
			long hackOff = qk->m_numInList;
			if ( hackOff < 0 ) break;
			char *next = (char *)&qks[0] + hackOff;
			qk = (QueryLink *)next;
		}
		// save it
		long numInList = uu;
		// sanity - re-entry from quickpoll?
		//if ( s_rdBuf ) { char *xx=NULL;*xx=0; }
		//s_rdBuf = rdbuf;
		// sort the QueryLinks in the linked list by how many points
		// they contribute to this related query's final score
		gbqsort ( lptrs,numInList,sizeof(long),llCmp,niceness);
		//s_rdBuf = NULL;
		// now we may have a new link list header!
		QueryLink *newHead = lptrs[0];
		// copy stuff from the old head that only the old head had
		newHead->m_totalQueryImportance=first->m_totalQueryImportance;
		newHead->m_docIdVotes      = first->m_docIdVotes;
		newHead->m_uniqueRound     = first->m_uniqueRound;
		newHead->m_queryStringOffset = first->m_queryStringOffset;
		first  ->m_isFirst = false;
		newHead->m_isFirst = true;
		// debug
		//log("seopipe: DEBUG. count=%li",qk->m_docIdVotes);
		// copy the newly sorted linked list into mod part buf
		for ( long j = 0 ; j < numInList ; j++ ) {
			// shortcut
			QueryLink *qrj = lptrs[j];
			// point to him
			long len = xbuf.length();
			// store that
			if ( ! xbuf.safeMemcpy ( qrj , sizeof(QueryLink))) {
				// should always have enough room!
				char *xx=NULL;*xx=0; }
			// cast what we stored
			QueryLink *stored = (QueryLink *)(xbuf.m_buf + len);
			// now set this to its proper value!!
			stored->m_numInList = numInList;
		}
	}


	log("seo: handlerequest8e phase 13");

	//
	//
	// 13. make final reply buf
	//
	//

	SafeBuf replyBuf;
	long need3 = 4 + xbuf.length() + stringBuf.length();
	if ( ! replyBuf.reserve ( need3 , "8erbuf" ) ) goto hadError;
	replyBuf.pushLong   ( xbuf.length() );
	replyBuf.safeMemcpy ( &xbuf );
	replyBuf.safeMemcpy ( &stringBuf );
	// sanity loop
	/*
	qks = (QueryLink *)xbuf.getBufStart();
	nks = xbuf.length() / sizeof(QueryLink); 
	for ( long i = 0 ; i < nks ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// get it
		QueryLink *qk = &qks[i];
		// only valid for heads
		if ( ! qk->m_isFirst ) continue;
		// test it
		long off = qk->m_queryStringOffset;
		if ( off < 0 ) { char *xx=NULL;*xx=0; }
		if ( off >= stringBuf.length() ) { char *xx=NULL;*xx=0; }
	}
	*/
	stringBuf.purge();
	xbuf     .purge();


	log("seo: handlerequest8e returning reply");

	//
	//
	// 14. send it back
	//
	//
	char *reply = replyBuf.getBufStart();
	long  replySize = replyBuf.length();

	if ( replySize != replyBuf.getCapacity() ) { char *xx=NULL;*xx=0; }
	if ( replySize != need3                  ) { char *xx=NULL;*xx=0; }

	replyBuf.detachBuf();

	UdpSlot *slot = st->m_udpSlot;

	// nuke the state now!
	mdelete ( st , sizeof(State8e) , "st8e" );
	delete (st);

	g_udpServer.sendReply_ass ( reply,
				    replySize,
				    reply,     // alloc
				    replySize, // allocSize
				    slot ,
				    60 , // timeout in seconds
				    NULL, // callback state
				    NULL , // senddone callback
				    // to optimize our flow use 30 because
				    // we all return our replies at about
				    // the same time so there's lots of
				    // collisions???
				    30 ); // backoff in ms, 
}

void printRainbowSections ( void *stk ) {

	StateSEO *sk = (StateSEO *)stk;

	SafeBuf *sb = &sk->m_sb;

	XmlDoc *xd = &sk->m_xd;

	xd->m_masterState = sk;
	xd->m_masterLoop  = printRainbowSections;

	// returns false if blocked
	if ( ! xd->printRainbowSections ( sb , &sk->m_hr ) )
		return;

	// "sb" should be populated now
	sendPageBack ( sk );
}
