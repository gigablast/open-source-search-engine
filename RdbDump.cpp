#include "gb-include.h"

#include "RdbDump.h"
#include "Rdb.h"
//#include "Tfndb.h"
//#include "Sync.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Tagdb.h"
//#include "Catdb.h"
#include "Statsdb.h"
#include "Accessdb.h"

extern void dumpDatedb   ( char *coll,int32_t sfn,int32_t numFiles,bool includeTree, 
			   int64_t termId , bool justVerify ) ;
extern void dumpPosdb    ( char *coll,int32_t sfn,int32_t numFiles,bool includeTree, 
			   int64_t termId , bool justVerify ) ;

void doneReadingForVerifyWrapper ( void *state ) ;
//void gotTfndbListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) ;

// . return false if blocked, true otherwise
// . sets g_errno on error
bool RdbDump::set ( //char     *coll          ,
		   collnum_t collnum ,
		    BigFile  *file          ,
		    int32_t      id2           , // in Rdb::m_files[] array
		    bool      isTitledb     ,
		    RdbBuckets *buckets     , // optional buckets to dump
		    RdbTree  *tree          , // optional tree to dump
		    RdbMap   *map           ,
		    RdbCache *cache         ,
		    int32_t      maxBufSize    ,
		    bool      orderedDump   , // dump in order of keys?
		    bool      dedup         , // 4 RdbCache::incorporateList()
		    int32_t      niceness      ,
		    void     *state         ,
		    void      (* callback) ( void *state ) ,
		    bool      useHalfKeys   ,
		    int64_t startOffset   ,
		    //key_t     prevLastKey   ,
		    char     *prevLastKey   ,
		    char      keySize       ,
		   //class DiskPageCache *pc     ,
		   void *pc ,
		    int64_t maxFileSize   ,
		    Rdb      *rdb           ) {

	if ( ! orderedDump ) {
		log(LOG_LOGIC,"db: RdbDump does not support non-ordered.");
		char *xx = NULL; *xx = 0;
	}
	//if ( ! coll &&
	//if ( ! coll && rdb->m_isCollectionLess )
	//	strcpy(m_coll,rdb->m_dbname);

	m_collnum = collnum;

	// use 0 for collectionless
	if ( rdb && rdb->m_isCollectionLess ) m_collnum = 0;

	// are we like catdb/statsdb etc.?
	m_doCollCheck = true;
	if ( rdb && rdb->m_isCollectionLess ) m_doCollCheck = false;
	// RdbMerge also calls us but rdb is always set to NULL and it was
	// causing a merge on catdb (collectionless) to screw up
	if ( ! rdb ) m_doCollCheck = false;

	/*
	if ( ! coll && g_catdb.getRdb() == rdb )
		strcpy(m_coll, "catdb");
	else if ( ! coll && g_statsdb.getRdb() == rdb )
		strcpy(m_coll, "statsdb");
	else if ( ! coll && g_accessdb.getRdb() == rdb )
		strcpy(m_coll, "accessdb");
	*/
	//else
	//	strcpy ( m_coll , coll );
	m_file          = file;
	m_id2           = id2;
	m_isTitledb     = isTitledb;
	m_buckets       = buckets;
	m_tree          = tree;
	m_map           = map;
	m_cache         = cache;
	m_orderedDump   = orderedDump;
	m_dedup         = dedup;
	m_state         = state;
	m_callback      = callback;
	m_list          = NULL;
	m_niceness      = niceness;
	m_tried         = false;
	m_isSuspended   = false;
	m_ks            = keySize;
	m_addToMap      = true;

	// reset this in case we run out of mem, it doesn't get set properly
	// and needs to be NULL for RdbMem's call to getLastKeyinQueue()
	m_lastKeyInQueue  = NULL;
	KEYMIN(m_firstKeyInQueue,m_ks);

	m_isDumping     = false;
	m_writing       = false;
	m_buf           = NULL;
	m_verifyBuf     = NULL;
	m_maxBufSize    = maxBufSize;
	m_offset        = startOffset ;
	m_rolledOver    = false; // true if m_nextKey rolls over back to 0
	//m_nextKey       = 0 ; // used in dumpTree()
	KEYMIN(m_nextKey,m_ks);
	m_nextNode      = 0 ; // used in dumpTree()
	// if we're dumping indexdb, allow half keys
	m_useHalfKeys  = useHalfKeys;
	//m_prevLastKey  = prevLastKey;
	KEYSET(m_prevLastKey,prevLastKey,m_ks);
	// for setting m_rdb->m_needsSave after deleting the dump list
	m_rdb = rdb;
	// . don't dump to a pre-existing file
	// . seems like Rdb.cpp makes a new BigFile before calling this
	// . now we can resume merges, so we can indeed dump to the END
	//   of a pre-exiting file, but not when dumping a tree!
	//if ( m_file->doesExist() > 0 ) {
	if ( (m_tree || m_buckets) && m_file->getFileSize() > 0 ) {
		g_errno = EEXIST;
		log("db: Could not dump to %s. File exists.",
		    m_file->getFilename());
		return true;
	}
	// . NOTE: MAX_PART_SIZE in BigFile must be defined to be bigger than
	//   anything we actually dump since we only anticipate spanning 1 file
	//   and so only register the first file's fd for write callbacks
	//if ( m_tree && m_tree->getMaxMem() > MAX_PART_SIZE ) 
	//return log("RdbDump::dump: tree bigger than file part size");
	// . open the file nonblocking, sync with disk, read/write
	// . NOTE: O_SYNC doesn't work too well over NFS
	// . we need O_SYNC when dumping trees only because we delete the
	//   nodes/records as we dump them 
	// . ensure this sets g_errno for us
	// . TODO: open might not block! fix that!
	int32_t flags = O_RDWR | O_CREAT ;
	// a niceness bigger than 0 means to do non-blocking dumps
	if ( niceness > 0 ) flags |=  O_ASYNC | O_NONBLOCK ;
	if ( ! m_file->open ( flags , pc , maxFileSize ) ) return true;
	// . get the file descriptor of the first real file in BigFile
	// . we should only dump to the first file in BigFile otherwise,
	//   we'd have to juggle fd registration
	m_fd = m_file->getfd ( 0 , false /*for reading?*/ );
	if ( m_fd < 0 ) {
		log(LOG_LOGIC,"db: dump: Bad fd of first file in BigFile.") ;
		return true;
	}
	// debug test
	//char buf1[10*1024];
	//int32_t n1 = m_file->write ( buf1 , 10*1024 , 0 );
	//log("bytes written=%"INT32"\n",n1);
	// we're now considered to be in dumping state
	m_isDumping = true;
	// . if no tree was provided to dump it must be RdbMerge calling us
	// . he'll want to call dumpList() on his own
	if ( ! m_tree && !m_buckets ) return true;
	// how many recs in tree?
	int32_t nr;
	char *structureName;
	if(m_tree) {
		nr = m_tree->getNumUsedNodes();
		structureName = "tree";
	}
	else if(m_buckets){
		nr = m_buckets->getNumKeys();
		structureName = "buckets";
	}
	// debug msg
	log(LOG_INFO,"db: Dumping %"INT32" recs from %s to files.",
	    nr, structureName);
	//    nr , m_file->getFilename() );
	// keep a total count for reporting when done
	m_totalPosDumped = 0;
	m_totalNegDumped = 0;

	// we have our own flag here since m_dump::m_isDumping gets
	// set to true between collection dumps, RdbMem.cpp needs
	// a flag that doesn't do that... see RdbDump.cpp.
	// this was in Rdb.cpp but when threads were turned off it was
	// NEVER getting set and resulted in corruption in RdbMem.cpp.
	m_rdb->m_inDumpLoop = true;

	// . start dumping the tree 
	// . return false if it blocked
	if ( ! dumpTree ( false ) ) return false;
	// no longer dumping
	doneDumping();
	// return true since we didn't block
	return true;
}

void RdbDump::reset ( ) {
	// free verify buf if there
	if ( m_verifyBuf ) {
		mfree ( m_verifyBuf , m_verifyBufSize , "RdbDump4");
		m_verifyBuf = NULL;
	}
}	

void RdbDump::doneDumping ( ) {

	int32_t saved = g_errno;

	m_isDumping = false;
	// print stats
	log(LOG_INFO,
	    "db: Dumped %"INT32" positive and %"INT32" negative recs. "
	    "Total = %"INT32".",
	     m_totalPosDumped , m_totalNegDumped ,
	     m_totalPosDumped + m_totalNegDumped );

	// . map verify
	// . if continueDumping called us with no collectionrec, it got
	//   deleted so RdbBase::m_map is nuked too i guess
	if ( saved != ENOCOLLREC && m_map )
		log("db: map # pos=%"INT64" neg=%"INT64"",
		    m_map->getNumPositiveRecs(),
		    m_map->getNumNegativeRecs()
		    );

	// free the list's memory
	if ( m_list ) m_list->freeList();
	// reset verify buffer
	reset();

	// did collection get deleted/reset from under us?
	if ( saved == ENOCOLLREC ) return;

	// save the map to disk. true = allDone
	if ( m_map ) m_map->writeMap( true );

	// now try to merge this collection/db again
	// if not already in the linked list. but do not add to linked list
	// if it is statsdb or catdb.
	if ( m_rdb && ! m_rdb->m_isCollectionLess )
		addCollnumToLinkedListOfMergeCandidates ( m_collnum );

#ifdef GBSANITYCHECK
	// sanity check
	log("DOING SANITY CHECK FOR MAP -- REMOVE ME");
	if ( m_map && ! m_map->verifyMap ( m_file ) ) {
		char *xx = NULL; *xx = 0; }
	// now check the whole file for consistency
	if ( m_ks == 18 ) { // map->m_rdbId == RDB_POSDB ) {
		collnum_t collnum = g_collectiondb.getCollnum ( m_coll );
		class RdbBase *base = m_rdb->m_bases[collnum];
		int32_t startFileNum = base->getNumFiles()-1;
		log("sanity: startfilenum=%"INT32"",startFileNum);
		dumpPosdb(m_coll,
			  startFileNum, // startFileNum
			   1                    , // numFiles
			   false                , // includeTree
			   -1                   , // termId
			   true                 );// justVerify?
	}
#endif
	// . append it to "sync" state we have in memory
	// . when host #0 sends a OP_SYNCTIME signal we dump to disk
	//g_sync.addOp ( OP_CLOSE , m_file , 0 );
}

static void tryAgainWrapper2 ( int fd , void *state ) ;
void        tryAgainWrapper2 ( int fd , void *state ) {
	// debug msg
	log(LOG_INFO,"db: Trying to get data again.");
	// stop waiting
	g_loop.unregisterSleepCallback ( state , tryAgainWrapper2 );
	// bitch about errors
	if (g_errno) log("db: Had error: %s.",mstrerror(g_errno));
	// get THIS ptr from state
	RdbDump *THIS = (RdbDump *)state;
	// continue dumping the tree or give control back to caller
	THIS->continueDumping ( );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . dumps the RdbTree, m_tree, into m_file
// . also sets and writes the RdbMap for m_file
// . we methodically get RdbLists from the RdbTree 
// . dumped recs are ordered by key if "orderedDump" was true in call to set()
//   otherwise, lists are ordered by node #
// . we write each list of recs to the file until the whole tree has been done
// . we delete all records in list from the tree after we've written the list
// . if a cache was provided we incorporate the list into the cache before
//   deleting it from the tree to keep the cache in sync. NO we do NOT!
// . called again by writeBuf() when it's done writing the whole list
bool RdbDump::dumpTree ( bool recall ) {
	// set up some vars
	//int32_t  nextNode;
	//key_t maxEndKey;
	//maxEndKey.setMax();
	char maxEndKey[MAX_KEY_BYTES];
	KEYMAX(maxEndKey,m_ks);
	// if dumping statsdb, we can only dump records 30 seconds old or
	// more because Statsdb.cpp can "back modify" such records in the tree
	// because it may have a query that took 10 seconds come in then it
	// needs to add a partial stat to the last 10 stats for those 10 secs.
	// we use Global time at this juncture
	if ( m_rdb->m_rdbId == RDB_STATSDB ) {
		int32_t nowSecs = getTimeGlobal();
		StatKey *sk = (StatKey *)maxEndKey;
		sk->m_zero      = 0x01;
		sk->m_labelHash = 0xffffffff;
		// leave last 60 seconds in there just to be safe
		sk->m_time1     = nowSecs - 60;
	}

	// this list will hold the list of nodes/recs from m_tree
	m_list = &m_ourList;
	// convert coll to collnum
	//collnum_t collnum = g_collectiondb.getCollnum ( m_coll );
	// a collnum of -1 is for collectionless rdbs
	//if ( collnum < 0 ) {
	//	//if ( g_catdb->getRdb() == m_rdb )
	//	if ( ! m_rdb->m_isCollectionLess ) {
	//		char *xx=NULL;*xx=0; //return true;
	//	}
	//	g_errno = 0;
	//	collnum = 0;
	//}
	// getMemOccupiedForList2() can take some time, so breathe
	int32_t niceness = 1;
 loop:
	// if the lastKey was the max end key last time then we're done
	if ( m_rolledOver     ) return true;
	// this is set to -1 when we're done with our unordered dump
	if ( m_nextNode == -1 ) return true;
	// . NOTE: list's buffer space should be re-used!! (TODO)
	// . "lastNode" is set to the last node # in the list
	bool status = true;
	//if ( ! m_orderedDump ) {
	//	status = ((RdbTree *)m_tree)->getListUnordered ( m_nextNode ,
	//							 m_maxBufSize ,
	//							 m_list , 
	//							 &nextNode );
	//	// this is -1 when no more nodes are left
	//	m_nextNode = nextNode;
	//}
	// "lastKey" is set to the last key in the list
	//else {
	{

		// can we remove neg recs?
		// class RdbBase *base = m_rdb->getBase(m_collnum);
		// bool removeNegRecs = false;
		// if ( base->m_numFiles <= 0 ) removeNegRecs = true;

		if ( recall ) goto skip;

		// debug msg
		//log("RdbDump:: getting list");
		m_t1 = gettimeofdayInMilliseconds();
		if(m_tree)
			status = m_tree->getList ( m_collnum       ,
					   m_nextKey     , 
					   maxEndKey     ,
					   m_maxBufSize  , // max recSizes
					   m_list        , 
					   &m_numPosRecs   ,
					   &m_numNegRecs   ,
					   m_useHalfKeys ,
						   niceness );
		else if(m_buckets)
			status = m_buckets->getList ( m_collnum,
					   m_nextKey     , 
					   maxEndKey     ,
					   m_maxBufSize  , // max recSizes
					   m_list        , 
					   &m_numPosRecs   ,
					   &m_numNegRecs   ,
					   m_useHalfKeys );


		// don't dump out any neg recs if it is our first time dumping
		// to a file for this rdb/coll. TODO: implement this later.
		//if ( removeNegRecs )
		//	m_list.removeNegRecs();

 		// if(!m_list->checkList_r ( false , // removeNegRecs?
 		// 			 false , // sleep on problem?
 		// 			 m_rdb->m_rdbId )) {
 		// 	log("db: list to dump is not sane!");
		// 	char *xx=NULL;*xx=0;
 		// }


	skip:
		int64_t t2;
		//key_t lastKey;
		char *lastKey;
		// if error getting list (out of memory?)
		if ( ! status ) goto hadError;
		// debug msg
		t2 = gettimeofdayInMilliseconds();
		log(LOG_INFO,"db: Get list took %"INT64" ms. "
		    "%"INT32" positive. %"INT32" negative.",
		    t2 - m_t1 , m_numPosRecs , m_numNegRecs );
		// keep a total count for reporting when done
		m_totalPosDumped += m_numPosRecs;
		m_totalNegDumped += m_numNegRecs;
		// . check the list we got from the tree for problems
		// . ensures keys are ordered from lowest to highest as well
		//#ifdef GBSANITYCHECK
		if ( 1==1 ||
		     g_conf.m_verifyWrites ||
		     g_conf.m_verifyDumpedLists ) {
			char *s = "none";
			if ( m_rdb ) s = getDbnameFromId(m_rdb->m_rdbId);
			char *ks1 = "";
			char *ks2 = "";
			char tmp1[32];
			char tmp2[32];
			if ( m_firstKeyInQueue ) {
				strcpy ( tmp1 , 
					 KEYSTR(m_firstKeyInQueue,
						m_list->m_ks));
				ks1 = tmp1;
			}
			if ( m_lastKeyInQueue ) {
				strcpy ( tmp2 , 
					 KEYSTR(m_lastKeyInQueue,
						m_list->m_ks));
				ks2 = tmp2;
			}

			log("dump: verifying list before dumping (rdb=%s "
			    "collnum=%i k1=%s k2=%s)",s,
			    (int)m_collnum,ks1,ks2);
			m_list->checkList_r ( false , // removeNegRecs?
					      false , // sleep on problem?
					      m_rdb->m_rdbId );
		}
		// if list is empty, we're done!
		if ( status && m_list->isEmpty() ) {
			// consider that a rollover?
			if ( m_rdb->m_rdbId == RDB_STATSDB )
				m_rolledOver = true;
			return true;
		}
		// get the last key of the list
		lastKey = m_list->getLastKey();
		// advance m_nextKey
		//m_nextKey  = lastKey ;
		//m_nextKey += (uint32_t)1;
		//if ( m_nextKey < lastKey ) m_rolledOver = true;
		KEYSET(m_nextKey,lastKey,m_ks);
		KEYADD(m_nextKey,1,m_ks);
		if (KEYCMP(m_nextKey,lastKey,m_ks)<0) m_rolledOver = true;
	      // debug msg
	      //log(0,"RdbDump:lastKey.n1=%"UINT32",n0=%"UINT64"",lastKey.n1,lastKey.n0);
	      //log(0,"RdbDump:next.n1=%"UINT32",n0=%"UINT64"",m_nextKey.n1,m_nextKey.n0);
	}
	// . return true on error, g_errno should have been set
	// . this is probably out of memory error
	if ( ! status ) {
	hadError:
		log("db: Had error getting data for dump: %s. Retrying.", 
		    mstrerror(g_errno));
		// debug msg
		//log("RdbDump::getList: sleeping and retrying");
		// retry for the remaining two types of errors
		if (!g_loop.registerSleepCallback(1000,this,tryAgainWrapper2)){
			log(
			    "db: Retry failed. Could not register callback.");
			return true;
		}
		// wait for sleep
		return false;
	}
	// if list is empty, we're done!
	if ( m_list->isEmpty() ) return true;
	// . set m_firstKeyInQueue and m_lastKeyInQueue
	// . this doesn't work if you're doing an unordered dump, but we should
	//   not allow adds when closing
	m_lastKeyInQueue  = m_list->getLastKey();

	// ensure we are getting the first key of the list
	m_list->resetListPtr();

	//m_firstKeyInQueue = m_list->getCurrentKey();
	m_list->getCurrentKey(m_firstKeyInQueue);
	// . write this list to disk
	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . if this blocks it should call us (dumpTree() back)
	if ( ! dumpList ( m_list , m_niceness , false ) ) return false;
	// close up shop on a write/dumpList error
	if ( g_errno ) return true;
	// . if dumpList() did not block then keep on truckin'
	// . otherwise, wait for callback of dumpTree()
	goto loop;
}

static void doneWritingWrapper ( void *state ) ;

// . return false if blocked, true otherwise
// . sets g_errno on error
// . this one is also called by RdbMerge to dump lists
bool RdbDump::dumpList ( RdbList *list , int32_t niceness , bool recall ) {

	// if we had a write error and are being recalled...
	if ( recall ) { m_offset -= m_bytesToWrite; goto recallskip; }
	// assume we don't hack the list
	m_hacked = false;
	m_hacked12 = false;
	// save ptr to list... why?
	m_list = list;
	// nothing to do if list is empty
	if ( m_list->isEmpty() ) return true;
	// we're now in dump mode again 
	m_isDumping = true;
	//#ifdef GBSANITYCHECK
	// don't check list if we're dumping an unordered list from tree!
	if ( g_conf.m_verifyWrites && m_orderedDump ) {
		m_list->checkList_r ( false /*removedNegRecs?*/ );
		// print list stats
		// log("dump: sk=%s ",KEYSTR(m_list->m_startKey,m_ks));
		// log("dump: ek=%s ",KEYSTR(m_list->m_endKey,m_ks));
	}
	//#endif

	// before calling RdbMap::addList(), always reset list ptr
	// since we no longer call this in RdbMap::addList() so we don't
	// mess up the possible HACK below
	m_list->resetListPtr();

	// . SANITY CHECK
	// . ensure first key is >= last key added to the map map
	if ( m_offset > 0 && m_map ) {
		//key_t k       = m_list->getCurrentKey();
		char k[MAX_KEY_BYTES];
		m_list->getCurrentKey(k);
		//key_t lastKey = m_map->getLastKey    (); // m_lastKey
		char lastKey[MAX_KEY_BYTES];
		m_map->getLastKey(lastKey);
		//char *lastKey = m_map->getLastKey();
		//if ( k <= lastKey ) {
		if ( KEYCMP(k,lastKey,m_ks)<=0 ) {
			log(LOG_LOGIC,"db: Dumping list key out of order. "
			    //"lastKey.n1=%"XINT32" n0=%"XINT64" k.n1=%"XINT32" n0=%"XINT64"",
			    //lastKey.n1,lastKey.n0,k.n1,k.n0);
			    "lastKey=%s k=%s",
			    KEYSTR(lastKey,m_ks),
			    KEYSTR(k,m_ks));
			g_errno = EBADENGINEER;
			//return true;
			char *xx = NULL; *xx = 0;
		}
	}

	if ( g_conf.m_verifyWrites ) {
		char rdbId = 0;
		if ( m_rdb ) rdbId = m_rdb->m_rdbId;
		m_list->checkList_r(false,false,rdbId);//RDB_POSDB);
		m_list->resetListPtr();
	}

	// HACK! POSDB
	if ( m_ks == 18 && m_orderedDump && m_offset > 0 ) {
		char k[MAX_KEY_BYTES];
		m_list->getCurrentKey(k);
		// . same top 6 bytes as last key we added?
		// . if so, we should only add 6 bytes from this key, not 12
		//   so on disk it is compressed consistently
		if ( memcmp ( (k             ) + (m_ks-12) ,
			      (m_prevLastKey ) + (m_ks-12) , 12 ) == 0 ) {
			char tmp[MAX_KEY_BYTES];
			char *p = m_list->getList();
			// swap high 12 bytes with low 6 bytes for first key
			gbmemcpy ( tmp   , p            , m_ks-12 );
			gbmemcpy ( p     , p + (m_ks-12) ,      12 );
			gbmemcpy ( p + 12, tmp          , m_ks-12 );
			// big hack here
			m_list->m_list         = p + 12;
			m_list->m_listPtr      = p + 12;
			m_list->m_listPtrLo    = p ;
			m_list->m_listPtrHi    = p + 6;
			m_list->m_listSize    -= 12 ;
			// turn on both bits to indicate double compression
			*(p+12) |= 0x06;
			m_hacked12 = true;
		}
	}

	// . HACK
	// . if we're doing an ordered dump then hack the list's first 12 byte
	//   key to make it a 6 byte iff the last key we dumped last time
	//   shares the same top 6 bytes as the first key of this list
	// . this way we maintain compression consistency on the disk
	//   so IndexTable.cpp can expect all 6 byte keys for the same termid
	//   and RdbList::checkList_r() can expect the half bits to always be
	//   on when they can be on
	// . IMPORTANT: calling m_list->resetListPtr() will mess this HACK up!!
	if ( m_useHalfKeys && m_orderedDump && m_offset > 0 && ! m_hacked12 ) {
		//key_t k = m_list->getCurrentKey();	
		char k[MAX_KEY_BYTES];
		m_list->getCurrentKey(k);
		// . same top 6 bytes as last key we added?
		// . if so, we should only add 6 bytes from this key, not 12
		//   so on disk it is compressed consistently
		//if ( memcmp ( ((char *)&k             ) + 6 ,
		//	      ((char *)&m_prevLastKey ) + 6 , 6 ) == 0 ) {
		if ( memcmp ( (k             ) + (m_ks-6) ,
			      (m_prevLastKey ) + (m_ks-6) , 6 ) == 0 ) {
			m_hacked = true;
			//char tmp[6];
			char tmp[MAX_KEY_BYTES];
			char *p = m_list->getList();
			//gbmemcpy ( tmp   , p     , 6 );
			//gbmemcpy ( p     , p + 6 , 6 );
			//gbmemcpy ( p + 6 , tmp   , 6 );
			gbmemcpy ( tmp   , p            , m_ks-6 );
			gbmemcpy ( p     , p + (m_ks-6) ,      6 );
			gbmemcpy ( p + 6 , tmp          , m_ks-6 );
			// big hack here
			m_list->m_list       = p + 6;
			m_list->m_listPtr    = p + 6;
			// make this work for POSDB, too
			m_list->m_listPtrLo  = p + 6 + 6;
			m_list->m_listPtrHi  = p ;
			m_list->m_listSize  -= 6 ;
			// hack on the half bit, too
			*(p+6) |= 0x02;
		}
	}

	// update old last key
	//m_prevLastKey = m_list->getLastKey();
	m_list->getLastKey(m_prevLastKey);

	// now write it to disk
	m_buf          = m_list->getList    ();
	m_bytesToWrite = m_list->getListSize();
	//#ifdef GBSANITYCHECK
	//if (m_list->getListSize()!=m_list->getListEnd() - m_list->getList()){
	//	log("RdbDump::dumpList: major problem here!");
	//	sleep(50000);
	//}
	//#endif
 recallskip:
	// make sure we have enough mem to add to map after a successful
	// dump up here, otherwise, if we write it and fail to add to map
	// the map is not in sync if we core thereafter
	if ( m_addToMap && m_map && ! m_map->prealloc ( m_list ) ) {
		log("db: Failed to prealloc list into map: %s.",
		    mstrerror(g_errno));
		// g_errno should be set to something if that failed
		if ( ! g_errno ) { char *xx = NULL; *xx = 0; }
		return true;
	}
	// tab to the old offset
	int64_t offset = m_offset;
	// might as well update the offset now, even before write is done
	m_offset += m_bytesToWrite ;
	// write thread is out
	m_writing = true;
	//m_bytesWritten = 0;

	// sanity check
	//log("dump: writing %"INT32" bytes at offset %"INT64"",m_bytesToWrite,offset);

	// . if we're called by RdbMerge directly use m_callback/m_state
	// . otherwise, use doneWritingWrapper() which will call dumpTree()
	// . BigFile::write() return 0 if blocked,-1 on error,>0 on completion
	// . it also sets g_errno on error
	bool isDone = m_file->write ( m_buf          ,
				      m_bytesToWrite ,
				      offset         ,
				      &m_fstate      ,
				      this           ,
				      doneWritingWrapper ,
				      niceness         );
	// debug msg
	//log("RdbDump dumped %"INT32" bytes, done=%"INT32"\n",
	//	m_bytesToWrite,isDone); 
	// return false if it blocked
	if ( ! isDone ) return false;
	// done writing
	m_writing = false;
	// return true on error
	if ( g_errno    ) return true;
	// . delete list from tree, incorporate list into cache, add to map
	// . returns false if blocked, true otherwise, sets g_errno on error
	// . will only block in calling updateTfndb()
	return doneDumpingList ( true );
}

// . delete list from tree, incorporate list into cache, add to map
// . returns false if blocked, true otherwise, sets g_errno on error
bool RdbDump::doneDumpingList ( bool addToMap ) {
	// we can get suspended when gigablast is shutting down, in which
	// case the map may have been deleted. only RdbMerge suspends its
	// m_dump class, not Rdb::m_dump. return false so caller nevers
	// gets called back. we can not resume from this suspension!
	//if ( m_isSuspended ) return false;
	// . if error was EFILECLOSE (file got closed before we wrote to it)
	//   then try again. file can close because fd pool needed more fds
	// . we cannot do this retry in BigFile.cpp because the BigFile
	//   may have been deleted/unlinked from a merge, but we could move
	//   this check to Msg3... and do it for writes, too...
	// . seem to be getting EBADFD errors now, too (what code is it?)
	//   i don't remember, just do it on *all* errors for now!
	//if ( g_errno == EFILECLOSED || g_errno == EBADFD ) {
	if ( g_errno && ! m_isSuspended ) {
		log(LOG_INFO,"db: Had error dumping data: %s. Retrying.",
		    mstrerror(g_errno));
		// . deal with the EBADF bug, it will loop forever on this
		// . i still don't know how the fd gets closed and s_fds[vfd]
		//   is not set to -1?!?!?!
		if ( g_errno == EBADF ) {
			// note it
			log(LOG_LOGIC,"db: setting fd for vfd to -1.");
			// mark our fd as not there...
			//int32_t i=(m_offset-m_bytesToWrite) / MAX_PART_SIZE;
			// sets s_fds[vfd] to -1
			// MDW: no, can't do this now
			// if ( m_file->m_files[i] )
			// 	releaseVfd ( m_file->m_files[i]->m_vfd );
		}
		//log("RdbDump::doneDumpingList: retrying.");
		return dumpList ( m_list , m_niceness , true );
	}
	// bail on error
	if ( g_errno ) {
		log("db: Had error dumping data: %s.", mstrerror(g_errno));
		//log("RdbDump::doneDumpingList: %s",mstrerror(g_errno));
		return true;
	}
	// . don't delete the list if we were dumping an unordered list
	// . we only dump unordered lists when we do a save
	// . it saves time not having to delete the list and it also allows
	//   us to do saves without deleting our data! good!
	if ( ! m_orderedDump ) return true; //--turn this off until save works

	// save for verify routine
	m_addToMap = addToMap;

	// should we verify what we wrote? useful for preventing disk 
	// corruption from those pesky Western Digitals and Maxtors?
	if ( g_conf.m_verifyWrites ) {
		// a debug message, if log disk debug messages is enabled
		log(LOG_DEBUG,"disk: Verifying %"INT32" bytes written.",
		    m_bytesToWrite);
		// make a read buf
		if ( m_verifyBuf && m_verifyBufSize < m_bytesToWrite ) {
			mfree ( m_verifyBuf , m_verifyBufSize , "RdbDump3" );
			m_verifyBuf = NULL;
			m_verifyBufSize = 0;
		}
		if ( ! m_verifyBuf ) {
			m_verifyBuf = (char *)mmalloc ( m_bytesToWrite , 
							"RdbDump3" );
			m_verifyBufSize = m_bytesToWrite;
		}
		// out of mem? if so, skip the write verify
		if ( ! m_verifyBuf ) return doneReadingForVerify();
		// read what we wrote
		bool isDone = m_file->read ( m_verifyBuf    ,
					     m_bytesToWrite ,
					     m_offset - m_bytesToWrite ,
					     &m_fstate      ,
					     this           ,
					     doneReadingForVerifyWrapper ,
					     m_niceness      );
		// debug msg
		//log("RdbDump dumped %"INT32" bytes, done=%"INT32"\n",
		//	m_bytesToWrite,isDone); 
		// return false if it blocked
		if ( ! isDone ) return false;
	}
	return doneReadingForVerify();
}

void doneReadingForVerifyWrapper ( void *state ) {
	RdbDump *THIS = (RdbDump *)state;
	// return if this blocks
	if ( ! THIS->doneReadingForVerify() ) return;
	// delete list from tree, incorporate list into cache, add to map
	//if ( ! THIS->doneDumpingList( true ) ) return;
	// continue
	THIS->continueDumping ( );
}

bool RdbDump::doneReadingForVerify ( ) {

	// if someone reset/deleted the collection we were dumping...
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	// . do not do this for statsdb/catdb which always use collnum of 0
	// . RdbMerge also calls us but gives a NULL m_rdb so we can't
	//   set m_isCollectionless to false
	if ( ! cr && m_doCollCheck ) {
		g_errno = ENOCOLLREC;
		log("db: lost collection while dumping to disk. making "
		    "map null so we can stop.");
		m_map = NULL;
		// m_file is probably invalid too since it is stored
		// in cr->m_bases[i]->m_files[j]
		m_file = NULL;
	}


	// see if what we wrote is the same as what we read back
	if ( m_verifyBuf && g_conf.m_verifyWrites &&
	     memcmp(m_verifyBuf,m_buf,m_bytesToWrite) != 0 &&
	     ! g_errno ) {
		log("disk: Write verification of %"INT32" bytes to file %s "
		    "failed at offset=%"INT64". Retrying.",
		    m_bytesToWrite,
		    m_file->getFilename(),
		    m_offset - m_bytesToWrite);
		// try writing again
		return dumpList ( m_list , m_niceness , true );
	}
	// time dump to disk (and tfndb bins)
	int64_t t ;
	// start timing on first call only
	if ( m_addToMap ) t = gettimeofdayInMilliseconds();
	// sanity check
	if ( m_list->m_ks != m_ks ) { char *xx = NULL; *xx = 0; }

	bool triedToFix = false;

 tryAgain:
	// . register this with the map now
	// . only register AFTER it's ALL on disk so we don't get partial
	//   record reads and we don't read stuff on disk that's also in tree
	// . add the list to the rdb map if we have one
	// . we don't have maps when we do unordered dumps
	// . careful, map is NULL if we're doing unordered dump
	if ( m_addToMap && m_map && ! m_map->addList ( m_list ) ) {
		// keys  out of order in list from tree?
		if ( g_errno == ECORRUPTDATA ) {
			log("db: trying to fix tree or buckets");
			if ( m_tree ) m_tree->fixTree();
			//if ( m_buckets ) m_buckets->fixBuckets();
			if ( m_buckets ) { char *xx=NULL;*xx=0; }
			if ( triedToFix ) { char *xx=NULL;*xx=0; }
			triedToFix = true;
			goto tryAgain;
		}
		g_errno = ENOMEM; 
		log("db: Failed to add data to map.");
		// undo the offset update, the write failed, the parent
		// should retry. i know RdbMerge.cpp does, but not sure
		// what happens when Rdb.cpp is dumping an RdbTree
		//m_offset -= m_bytesToWrite ;
		// this should never happen now since we call prealloc() above
		char *xx = NULL; *xx = 0;
		return true;
	}

	// debug msg
	int64_t now = gettimeofdayInMilliseconds();
	log(LOG_TIMING,"db: adding to map took %"UINT64" ms" , now - t );

	// . Msg5.cpp and RdbList::merge_r() should remove titleRecs
	//   that are not supported by tfndb, so we only need to add tfndb
	//   records at this point to update the tfndb recs to point to the
	//   new tfn we are dumping into for the existing titlerecs
	// . we just add one tfndb rec per positive titleRec in m_list
	// . negative TitleRec keys should have had a negative tfndb key
	//   added to tfndb in Rdb.cpp::addRecord() already, and ...
	// . RdbList::indexMerge_r() will take care of merging properly
	//   so as to not treat the tfn bits as part of the key when comparing
	// . this will re-call this doneDumpingList(false) if it blocks
	// . returns false if blocks, true otherwise
	//if ( ! updateTfndbLoop() ) return false;

	// . HACK: fix hacked lists before deleting from tree
	// . iff the first key has the half bit set
	if ( m_hacked ) {
		//char tmp[6];
		char tmp[MAX_KEY_BYTES];
		char *p = m_list->getList() - 6 ;
		//gbmemcpy ( tmp   , p     , 6 );
		//gbmemcpy ( p     , p + 6 , 6 );
		//gbmemcpy ( p + 6 , tmp   , 6 );
		gbmemcpy ( tmp          , p     , 6 );
		gbmemcpy ( p            , p + 6 , m_ks-6 );
		gbmemcpy ( p + (m_ks-6) , tmp   , 6 );
		// undo the big hack
		m_list->m_list       = p ;
		m_list->m_listPtr    = p ;
		// make this work for POSDB...
		m_list->m_listPtrLo  = p + m_ks - 12;
		m_list->m_listPtrHi  = p + m_ks - 6;
		m_list->m_listSize  += 6 ;
		// hack off the half bit, we're 12 bytes again
		*p &= 0xfd ;
		// turn it off again just in case
		m_hacked = false;
	}

	if ( m_hacked12 ) {
		char tmp[MAX_KEY_BYTES];
		char *p = m_list->getList() - 12 ;
		// swap high 12 bytes with low 6 bytes for first key
		gbmemcpy ( tmp   , p            , 12 );
		gbmemcpy ( p     , p + 12 ,      6 );
		gbmemcpy ( p + 6, tmp          , 12 );
		// big hack here
		m_list->m_list         = p ;
		m_list->m_listPtr      = p ;
		m_list->m_listPtrLo    = p + 6;
		m_list->m_listPtrHi    = p + 12;
		m_list->m_listSize    += 12 ;
		// hack off the half bit, we're 12 bytes again
		*p &= 0xf9 ;
		m_hacked12 = false;
	}


	// verify keys are in order after we hack it back
	//if ( m_orderedDump ) m_list->checkList_r ( false , true );

	// if we're NOT dumping a tree then return control to RdbMerge
	if ( ! m_tree && !m_buckets ) return true;

	// . merge the writeBuf into the cache at this point or after deleting
	// . m_list should have it's m_lastKey set since we got called from
	//   RdbMerge if m_cache is non-NULL and it called RdbList::merge()
	//   through Msg5 at one point to form this list
	// . right now i just made this clear the cache... it's easier
	//if ( m_cache ) m_cache->incorporateList ( m_list , m_dedup ,
	//					  m_list->getLastKey() );
	// . delete these nodes from the tree now that they're on the disk
	//   now that they can be read from list since addList() was called
	// . however, while we were writing to disk a key that we were
	//   writing could have been deleted from the tree. To prevent
	//   problems we should only delete nodes that are present in tree...
	// . actually i fixed that problem by not deleting any nodes that
	//   might be in the middle of being dumped 
	// . i changed Rdb::addNode() and Rdb::deleteNode() to do this
	// . since we made it here m_list MUST be ordered, therefore
	//   let's try the new, faster deleteOrderedList and let's not do
	//   balancing to make it even faster 
	// . balancing will be restored once we're done deleting this list
	// debug msg
	//log("RdbDump:: deleting list");
	int64_t t1 = gettimeofdayInMilliseconds();
	// convert to number, this is -1 if no longer exists
	//collnum_t collnum = g_collectiondb.getCollnum ( m_coll );
	//if ( collnum < 0 && m_rdb->m_isCollectionLess ) {
	//	collnum = 0;
	//	g_errno = 0;
	//}
	//m_tree->deleteOrderedList ( m_list , false /*do balancing?*/ );
	// tree delete is slow due to checking for leaks, not balancing
	bool s;
	if(m_tree) {
		s = m_tree->deleteList(m_collnum,m_list,true/*do balancing?*/);
		log("dump: tree now has %i nodes",(int)m_tree->m_numUsedNodes);
	}
	else if(m_buckets) {
		s = m_buckets->deleteList(m_collnum, m_list);
	}

	// problem?
	if ( ! s && ! m_tried ) {
		m_tried = true;
		if ( m_file )
		log("db: Corruption in tree detected when dumping to %s. "
		    "Fixing. Your memory had an error. Consider replacing it.",
		    m_file->getFilename());
		log("db: was collection restarted/reset/deleted before we "
		    "could delete list from tree? collnum=%"INT32"",
		    (int32_t)m_collnum);
		// reset error in that case
		g_errno = 0;
		// if ( m_rdb && m_rdb->m_rdbId != RDB_DOLEDB ) {
		// 	// core now to debug this for sectiondb
		// 	char *xx=NULL;*xx=0;
		// 	((RdbTree *)m_tree)->fixTree ( );
		// }
	}
	// tell rdb he needs saving now
	//if ( m_rdb ) m_rdb->m_needsSave = true;
	// debug msg
	int64_t t2 = gettimeofdayInMilliseconds();
	log(LOG_TIMING,"db: dump: deleteList: took %"INT64"",t2-t1);
	return true;
}
/*
static void tryAgainWrapper ( int fd , void *state ) ;

// returns false if blocks, true otherwise
bool RdbDump::updateTfndbLoop () {
	// only if dumping titledb 
	if ( ! m_isTitledb ) return true;
	// . start from beginning in case last add failed
	// . this may result in some dups if we get re-called, but that's ok
	m_list->resetListPtr();
	// point to it
	Rdb *tdb = g_tfndb.getRdb();
	// is it the secondary/repair rdb used by Repair.cpp?
	if ( m_rdb == g_titledb2.getRdb () ) tdb = g_tfndb2.getRdb();
	// get collection number
	collnum_t collnum = g_collectiondb.getCollnum ( m_coll );
	// bail if collection gone
	if ( collnum < (collnum_t)0 ) {
		//if ( g_catdb->getRdb() == m_rdb )
		if ( strcmp ( m_coll, "catdb" ) == 0 )
			collnum = 0;
		else if ( strcmp ( m_coll, "statsdb" ) == 0 )
			collnum = 0;
		else {
			log("Collection \"%s\" removed during dump.",m_coll);
			return true;
		}
	}
 loop:
	// get next
	if ( m_list->isExhausted() ) return true;
	// get the TitleRec key
	//key_t k = m_list->getCurrentKey();
	char k[MAX_KEY_BYTES];
	m_list->getCurrentKey(k);
	//char *rec     = m_list->getCurrentRec();
	//int32_t  recSize = m_list->getCurrentRecSize();
	// advance for next call
	m_list->skipCurrentRecord();
	// skip if a delete
	if ( KEYNEG(k) ) goto loop;
	// . otherwise, this is the "final" titleRec for this docid because
	//   Msg5/RdbList::merge_r() should have removed it if it is not the
	//   ultimate titleRec for this docid, because RdbList::merge_r()
	//   takes a "tfndbList" as input just to weed out titleRecs that
	//   are not supported by a tfndb record
	// . make the tfndb key
	int64_t d = g_titledb.getDocIdFromKey ((key_t *) k );
	//int32_t e = g_titledb.getHostHash ( (key_t *)k );
	int64_t uh48 = g_titledb.getUrlHash48 ( (key_t *)k );
	int32_t tfn = m_id2;
	// delete=false
	key_t tk = g_tfndb.makeKey ( d, uh48, tfn, false );
	KEYSET(m_tkey,(char *)&tk,sizeof(key_t));
	// debug msg
	//logf(LOG_DEBUG,"db: rdbdump: updateTfndbLoop: tbadd docId=%"INT64" "
	//    "tfn=%03"INT32"", g_tfndb.getDocId((key_t *)m_tkey ),
	//    (int32_t)g_tfndb.getTitleFileNum((key_t *)m_tkey));
	// . add it, returns false and sets g_errno on error
	// . this will override any existing tfndb record for this docid
	//   because RdbList.cpp uses a special key compare function (cmp2)
	//   to ignore the tfn bits on tfndb keys, so we get the newest/latest
	//   tfndb key after the merge.
	if ( tdb->addRecord ( collnum , m_tkey , NULL , 0 , 0) ) goto loop;
	// return true with g_errno set for most errors, that's bad
	if ( g_errno != ETRYAGAIN && g_errno != ENOMEM ) {
		log("db: Had error adding record to tfndb: %s.",
		    mstrerror(g_errno));
		return true;
	}
	// try starting a dump, Rdb::addRecord() does not do this like it
	// should, only Rdb::addList() does
	if ( tdb->needsDump() ) {
		log(LOG_INFO,"db: Dumping tfndb while merging titledb.");
		// . CAUTION! must use niceness one because if we go into
		//   urgent mode all niceness 2 stuff will freeze up until
		//   we exit urgent mode! so when tfndb dumps out too much
		//   stuff he'll go into urgent mode and freeze himself
		if ( ! tdb->dumpTree ( 1 ) ) // niceness
			log("db: Error dumping tfndb to disk: %s.",
			    mstrerror(g_errno));
	}
	// debug msg
	//log("db: Had error when trying to dump tfndb: %s. Retrying.",
	//    mstrerror(g_errno));
	// retry for the remaining two types of errors
	if ( ! g_loop.registerSleepCallback(1000,this,tryAgainWrapper)) {
		log("db: Failed to retry. Very bad.");
		return true;
	}
	// wait for sleep
	return false;
}

void tryAgainWrapper ( int fd , void *state ) {
	// debug msg
	log(LOG_INFO,"db: Trying to update tfndb again.");
	// stop waiting
	g_loop.unregisterSleepCallback ( state , tryAgainWrapper );
	// bitch about errors
	if ( g_errno ) log(LOG_LOGIC,"db: dump: Could not unregister "
			   "retry callback: %s.",mstrerror(g_errno));
	// get THIS ptr from state
	RdbDump *THIS = (RdbDump *)state;
	// continue loop, this returns false if it blocks
	if ( ! THIS->updateTfndbLoop() ) return;
	// don't add to map, we already did
	if ( ! THIS->doneDumpingList ( false ) ) return;
	// continue dumping the tree or give control back to caller
	THIS->continueDumping ( );
}
*/

// continue dumping the tree
void doneWritingWrapper ( void *state ) {
	// get THIS ptr from state
	RdbDump *THIS = (RdbDump *)state;
	// done writing
	THIS->m_writing = false;
	// bitch about errors
	if ( g_errno && THIS->m_file ) 
		log("db: Dump to %s had write error: %s.",
		    THIS->m_file->getFilename(),mstrerror(g_errno));
	// delete list from tree, incorporate list into cache, add to map
	if ( ! THIS->doneDumpingList( true ) ) return;
	// continue
	THIS->continueDumping ( );
}

void RdbDump::continueDumping() {

	// if someone reset/deleted the collection we were dumping...
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	// . do not do this for statsdb/catdb which always use collnum of 0
	// . RdbMerge also calls us but gives a NULL m_rdb so we can't
	//   set m_isCollectionless to false
	if ( ! cr && m_doCollCheck ) {
		g_errno = ENOCOLLREC;
		// m_file is probably invalid too since it is stored
		// in cr->m_bases[i]->m_files[j]
		m_file = NULL;
		log("db: continue dumping lost collection");
	}

	// bitch about errors, but i guess if we lost our collection
	// then the m_file could be invalid since that was probably stored
	// in the CollectionRec::RdbBase::m_files[] array of BigFile ptrs
	// so we can't say m_file->getFilename()
	else if (g_errno)log("db: Dump to %s had error writing: %s.",
			     m_file->getFilename(),mstrerror(g_errno));

	// go back now if we were NOT dumping a tree
	if ( ! (m_tree || m_buckets) ) {
		m_isDumping = false;
		m_callback ( m_state );		
		return;
	}
	// . continue dumping the tree
	// . return if this blocks
	// . if the collrec was deleted or reset then g_errno will be
	//   ENOCOLLREC and we want to skip call to dumpTree(
	if ( g_errno != ENOCOLLREC && ! dumpTree ( false ) ) 
		return;
	// close it up
	doneDumping ( );
	// call the callback
	m_callback ( m_state );
}

// . load the table from a dumped btree (unordered dump only!)
// . must NOT have been an ordered dump cuz tree will be seriously skewed
// . this is completely blocking cuz it used on init to recover a saved table
// . used for recovering a table that was too small to dump to an rdbfile
// . returns true if "filename" does not exist
// . stored in key/dataSize/data fashion
// . TODO: TODO: this load() routine and the m_orderedDump stuff above are
//   just hacks until we make the tree balanced. Then we can use RdbScan
//   to load the tree. Also, I we may not have enough mem to load the tree
//   because it loads it all in at once!!!!!
/*
bool RdbDump::load ( Rdb *rdb ,  int32_t fixedDataSize, BigFile *file ,
		     class DiskPageCache *pc ) {
        //m_tree          = tree;
	// return true if the file does not exist
	if ( file->doesExist() <= 0 ) return true; 
	// open the file read only
	if ( ! file->open ( O_RDONLY , pc ) )
	       return log("db: Could not open %s: %s.",file->getFilename(),
			  mstrerror(g_errno));
	// a harmless note
	log(LOG_INFO,"db: Loading data from %s",file->getFilename());
	// read in all data at once since this should only be run at
	// startup when we still have plenty of memory
	int32_t bufSize = file->getFileSize();
	// return true if filesize is 0
	if ( bufSize == 0 ) return true;
	// otherwise, alloc space to read the WHOLE file
	char *buf  = (char *) mmalloc( bufSize ,"RdbDump");
	if ( ! buf ) return log("db: Could not allocate %"INT32" bytes to load "
				"%s" , bufSize , file->getFilename());
	//int32_t n = file->read ( buf , bufSize , m_offset );
	file->read ( buf , bufSize , m_offset );
	if ( g_errno ) {
		mfree ( buf , bufSize , "RdbDump");
		return log("db: Had error reading %s: %s.",file->getFilename(),
			   mstrerror(g_errno));
	}
	char *p    = buf;
	char *pend = buf + bufSize;
	// now let 'er rip
	while ( p < pend ) {
		// get the key
	        key_t key = *(key_t *) p;
		// advance the buf ptr
		p += sizeof(key_t);
		// get dataSize
		int32_t dataSize = fixedDataSize;
		// we may have a datasize
		if ( fixedDataSize == -1 ) {
			dataSize = *(int32_t *)p;
			p += 4;
		}
		// point to data if any
		char *data ;
		if ( dataSize > 0 ) data = p;
		else                data = NULL;
		// skip p over data
		p += dataSize;
		// add to rdb
		if ( ! rdb->addRecord ( key , data , dataSize ) ) {
			mfree ( buf , bufSize ,"RdbDump");
			return log("db: Could not add record from %s: %s.",
				   file->getFilename(),mstrerror(g_errno));
		}

		// we must dup the data so the tree can free it		
		//char *copy = mdup ( p , dataSize ,"RdbDump");
		// add the node
		//if ( m_tree->addNode ( key , copy , dataSize ) < 0 ) {
		//	mfree ( buf , bufSize ,"RdbDump");
		//		return log("RdbDump::load:addNode failed");
		//}
	}
	// free the m_buffer we used
	mfree ( buf , bufSize , "RdbDump");
	file->close();
	return true;
}
*/
