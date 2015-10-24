#include "gb-include.h"

#include "Rdb.h"
#include "RdbMerge.h"
#include "Msg3.h"
#include "Indexdb.h"
#include "Process.h"
#include "Spider.h"

// declare the lock unlocked
//static bool s_isMergeLocked = false;

static void getLockWrapper  ( int fd , void *state ) ;
static void dumpListWrapper ( void *state ) ;
static void gotListWrapper  ( void *state , RdbList *list , Msg5 *msg5 ) ;
static void tryAgainWrapper ( int fd , void *state ) ;

RdbMerge::RdbMerge   () {}; 
RdbMerge::~RdbMerge  () {}; 
void RdbMerge::reset () { m_isMerging = false; m_isSuspended = false; }

// . buffer is used for reading and writing
// . return false if blocked, true otherwise
// . sets g_errno on error
// . if niceness is 0 merge will block, otherwise will not block
// . we now use niceness of 1 which should spawn threads that don't allow
//   niceness 2 threads to launch while they're running
// . spider process now uses mostly niceness 2 
// . we need the merge to take priority over spider processes on disk otherwise
//   there's too much contention from spider lookups on disk for the merge
//   to finish in a decent amount of time and we end up getting too many files!
bool RdbMerge::merge ( char     rdbId        ,
		       //char    *coll         , //RdbBase *base         , 
		       collnum_t collnum,
		       BigFile *target       , 
		       RdbMap  *targetMap    ,
		       int32_t     id2          , // target's secondary id
		       int32_t     startFileNum , 
		       int32_t     numFiles     ,
		       int32_t     niceness     ,
		       //class DiskPageCache *pc   ,
		       void *pc ,
		       int64_t maxTargetFileSize ,
		       char     keySize      ) {
	// reset ourselves
	reset();
	// set it
	m_rdbId = rdbId;
	Rdb *rdb = getRdbFromId ( rdbId );
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,collnum))) return true;
	// don't breech the max
	//if ( numFiles > m_maxFilesToMerge ) numFiles = m_maxFilesToMerge;
	// reset this map! it's m_crcs needs to be reset
	//targetMap->reset();
	// remember some parms
	//if ( ! coll && rdb->m_isCollectionLess )
	//	strcpy ( m_coll , rdb->m_dbname );
	//else
	//	strcpy ( m_coll , coll );

	m_collnum = collnum;
	if ( rdb->m_isCollectionLess ) m_collnum = 0;

	m_target          = target;
	m_targetMap       = targetMap;
	m_id2             = id2;
	m_startFileNum    = startFileNum;
	m_numFiles        = numFiles;
	m_dedup           = base->m_dedup;
	m_fixedDataSize   = base->m_fixedDataSize;
	m_niceness        = niceness;
	//m_pc              = pc;
	m_maxTargetFileSize = maxTargetFileSize;
	m_doneMerging     = false;
	m_ks              = keySize;
	// . set the key range we want to retrieve from the files
	// . just get from the files, not tree (not cache?)
	//m_startKey.setMin();
	//m_endKey.setMax();
	KEYMIN(m_startKey,m_ks);
	KEYMAX(m_endKey,m_ks);
	// if we're resuming a killed merge, set m_startKey to last
	// key the map knows about.
	// the dump will start dumping at the end of the targetMap's data file.
	if ( m_targetMap->getNumRecs() > 0 ) {
		log(LOG_INIT,"db: Resuming a killed merge.");
		//m_startKey = m_targetMap->getLastKey();
		m_targetMap->getLastKey(m_startKey);
		//m_startKey += (uint32_t) 1;
		KEYADD(m_startKey,1,m_ks);
		// if power goes out and we are not doing synchronous writes
		// then we could have completely lost some data and unlinked
		// a part file from the file being merged, so that the data is
		// gone. to be able to resume merging, we must increment the
		// startKey until it references a valid offset in all the 
		// files being merged. invalid offsets will reference parts 
		// that have been chopped.
		/*
		RdbMap  **maps  = rdb->getMaps();
		BigFile **files = rdb->getFiles();
		for ( int32_t i=m_startFileNum;i<m_startFileNum+m_numFiles;i++){
			int64_t minOff = 0LL;
			int32_t k = 0;
			while ( k < files[i]->m_maxParts &&
				!   files[i]->m_files[k]    ) {
				k++;
				minOff += MAX_PART_SIZE;
			}
			int32_t pn0 = maps[i]->getPage ( m_startKey );
			int32_t pn  = pn0;
			while ( maps[i]->getAbsoluteOffset(pn) < minOff ) pn++;
			if ( pn != pn0 ) {
				log("db: Lost data during merge. Starting "
				    "merge at page number %"INT32" from %"INT32" for "
				    "file.",pn,pn0);
				m_startKey = maps[i]->getKey ( pn );
			}
		}
		*/
	}
	// free our list's memory, just in case
	//m_list.freeList();
	// . we may have multiple hosts running on the same cpu/hardDrive
	// . therefore, to maximize disk space, we should only have 1 merge
	//   at a time going on between these hosts
	// . now tfndb has own merge class since titledb merge writes url recs
	/*
	if ( s_isMergeLocked ) {
		//log("RdbMerge::merge: someone else merging sleeping.");
		log("RdbMerge::merge: someone else merging. bad engineer.");
		return false;
		// if it fails then sleep until it works
		//returng_loop.registerSleepCallback(5000,this,getLockWrapper);
	}
	*/
	return gotLock();
}

// . called once every 5 seconds or so (might be 1 second)
void getLockWrapper ( int fd , void *state ) {
	RdbMerge *THIS = (RdbMerge *) state;
	// . try getting the file again
	// . now tfndb has own merge class since titledb merge writes url recs
	//if ( s_isMergeLocked ) {
	//	log("RdbMerge::merge: someone else merging sleeping.");
	//	return;
	//}
	// if we got the file then unregister this callback
	g_loop.unregisterSleepCallback ( THIS, getLockWrapper );
	// . and call gotLock(), return if it succeeded
	// . it returns false if it blocked
	if ( ! THIS->gotLock() ) return;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool RdbMerge::gotLock ( ) {
	// get total recSizes of files we're merging
	//int32_t totalSize = 0;
	//for ( int32_t i=m_startFileNum ; i < m_startFileNum + m_numFiles ; i++ )
	//totalSize += m_base->m_files[i]->getSize();
	// . grow the map now so it doesn't have to keep growing dynamically
	//   which wastes memory
	// . setMapSize() returns false and sets g_errno on error
	// . we return true if it had an error
	//if ( ! m_targetMap->setMapSizeFromFileSize ( totalSize ) ) {
	//log("RdbMerge::getLockFile: targetMap setMapSize failed");
	//return true;
	//}

	// . get last mapped offset
	// . this may actually be smaller than the file's actual size
	//   but the excess is not in the map, so we need to do it again
	int64_t startOffset = m_targetMap->getFileSize();

	// if startOffset is > 0 use the last key as RdbDump:m_prevLastKey
	// so it can compress the next key it dumps providee m_useHalfKeys
	// is true (key compression) and the next key has the same top 6 bytes
	// as m_prevLastKey
	//key_t prevLastKey;
	//if ( startOffset > 0 ) prevLastKey = m_targetMap->getLastKey();
	//else                   prevLastKey.setMin();
	char prevLastKey[MAX_KEY_BYTES];
	if ( startOffset > 0 ) m_targetMap->getLastKey(prevLastKey);
	else                   KEYMIN(prevLastKey,m_ks);

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;

	// . set up a a file to dump the records into
	// . returns false and sets g_errno on error
	// . this will open m_target as O_RDWR | O_NONBLOCK | O_ASYNC ...
	m_dump.set ( m_collnum          ,
		     m_target           ,
		     m_id2              ,
		     //m_startFileNum - 1 , // merge fileNum in Rdb::m_files[]
		     (m_rdbId == RDB_TITLEDB||m_rdbId== RDB2_TITLEDB2) ,
		     NULL         , // buckets to dump is NULL, we call dumpList
		     NULL         , // tree to dump is NULL, we call dumpList
		     m_targetMap  ,
		     NULL         , // for caching dumped tree
		     0            , // m_maxBufSize. not needed if no tree! 
		     true         , // orderedDump?
		     m_dedup      ,
		     m_niceness   , // niceness of dump
		     this         , // state
		     dumpListWrapper ,
		     base->useHalfKeys() ,
		     startOffset  ,
		     prevLastKey  ,
		     m_ks         ,
		     NULL,//m_pc         ,
		     m_maxTargetFileSize ,
		     NULL                ); // set m_base::m_needsToSave? no.
	// what kind of error?
	if ( g_errno ) {
		log("db: gotLock: %s.", mstrerror(g_errno) );
		return true;
	}
	// . create a new msg3
	// . don't keep static because it contains a msg3, treeList & diskList
	// . these can take up  many megs of mem
	// . yes, but we need to avoid fragmentation, so hold on to our mem!
	//m_msg3 = new (Msg3);
	//if ( ! m_msg3 ) return false;
	// we're now merging since the dump was set up successfully
	m_isMerging     = true;
	// make it suspended for now
	m_isSuspended   = true;
	// grab the lock
	//s_isMergeLocked = true;
	// . this unsuspends it
	// . this returns false on error and sets g_errno
	// . it returns true if blocked or merge completed successfully
	return resumeMerge ( );
}

void RdbMerge::suspendMerge ( ) {
	if ( ! m_isMerging ) return;
	// do not reset m_isReadyToSave...
	if ( m_isSuspended ) return;
	m_isSuspended = true;
	// we are waiting for the suspension to kick in really
	m_isReadyToSave = false;
	// . we don't want the dump writing to an RdbMap that has been deleted
	// . this can happen if the close is delayed because we are dumping
	//   a tree to disk
	m_dump.m_isSuspended = true;
}

void RdbMerge::doSleep() {
	log("db: Merge had error: %s. Sleeping and retrying.", 
	    mstrerror(g_errno));
	g_errno = 0;
	g_loop.registerSleepCallback (1000,this,tryAgainWrapper);
}

// . return false if blocked, otherwise true
// . sets g_errno on error
bool RdbMerge::resumeMerge ( ) {
	// return true if not suspended
	if ( ! m_isSuspended ) return true;
	// turn off the suspension so getNextList() will work
	m_isSuspended = false;
	// the usual loop
 loop:
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . we return true if it blocked
	if ( ! getNextList ( ) ) return false;
	// if g_errno is out of memory then msg3 wasn't able to get the lists
	// so we should sleep and retry...
	// or if no thread slots were available...
	if ( g_errno == ENOMEM || g_errno == ENOTHREADSLOTS ) { 
		doSleep(); return false; }
	// if list is empty or we had an error then we're done
	if ( g_errno || m_doneMerging ) { doneMerging(); return true; }
	// . otherwise dump the list we read to our target file
	// . this returns false if blocked, true otherwise
	if ( ! dumpList ( ) ) return false;
	// repeat ad nauseam
	goto loop;
}

static void chopWrapper ( void *state ) ;

// . return false if blocked, true otherwise
// . sets g_errno on error
bool RdbMerge::getNextList ( ) {
	// return true if g_errno is set
	if ( g_errno || m_doneMerging ) return true;
	// it's suspended so we count this as blocking
	if ( m_isSuspended ) {
		m_isReadyToSave = true;
		return false;
	}
	// if the power is off, suspend the merging
	if ( ! g_process.m_powerIsOn ) {
		m_isReadyToSave = true;
		doSleep();
		return false;
	}
	// no chop threads
	m_numThreads = 0;
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base = getRdbBase(m_rdbId,m_collnum);
	if ( ! base ) {
		// hmmm it doesn't set g_errno so we set it here now
		// otherwise we do an infinite loop sometimes if a collection
		// rec is deleted for the collnum
		g_errno = ENOCOLLREC;
		return true;
	}
	// . if a contributor has just surpassed a "part" in his BigFile
	//   then we can delete that part from the BigFile and the map
	for ( int32_t i = m_startFileNum ; i < m_startFileNum + m_numFiles; i++ ){
		RdbMap    *map    = base->m_maps[i];
		int32_t       page   = map->getPage ( m_startKey );
		int64_t  offset = map->getAbsoluteOffset ( page );
		BigFile   *file   = base->m_files[i];
		int32_t       part   = file->getPartNum ( offset ) ;
		if ( part == 0 ) continue;
		// i've seen this bug happen if we chop a part off on our
		// last dump and the merge never completes for some reason...
		// so if we're in the last part then don't chop the part b4 us
		if ( part >= file->m_maxParts - 1 ) continue;
		// if we already unlinked part # (part-1) then continue
		if ( ! file->doesPartExist ( part - 1 ) ) continue;
		// . otherwise, excise from the map
		// . we must be able to chop the mapped segments corresponding
		//   EXACTLY to the part file
		// . therefore, PAGES_PER_SEGMENT define'd in RdbMap.h must
		//   evenly divide MAX_PART_SIZE in BigFile.h
		// . i do this check in RdbMap.cpp
		if ( ! map->chopHead ( MAX_PART_SIZE ) ) {
			// we had an error!
			log("db: Failed to remove data from map for "
			    "%s.part%"INT32".",
			    file->getFilename(),part);
			return true;
		}
		// . also, unlink any part files BELOW part # "part"
		// . this returns false if it blocked, true otherwise
		// . this sets g_errno on error
		// . now we just unlink part file #(part-1) explicitly
		if ( ! file->chopHead ( part - 1 , chopWrapper , this ) ) 
			m_numThreads++;
		if ( ! g_errno ) continue;
		log("db: Failed to unlink file %s.part%"INT32".",
		    file->getFilename(),part);
		return true;
	}
	// wait for file to be unlinked before getting list
	if ( m_numThreads > 0 ) return false;
	// otherwise, get it now
	return getAnotherList ( );
}

void chopWrapper ( void *state ) {
	RdbMerge *THIS = (RdbMerge *)state;
	// wait for all threads to complete
	if ( --THIS->m_numThreads > 0 ) return;
	// return if this blocks
	if ( ! THIS->getAnotherList ( ) ) return;
	// otherwise, continue the merge loop
	THIS->resumeMerge();
}

bool RdbMerge::getAnotherList ( ) {
	log(LOG_DEBUG,"db: Getting another list for merge.");
	// clear it up in case it was already set
	g_errno = 0;
	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;
	// if merging titledb files, we must adjust m_endKey so we do
	// not have to read a huge 200MB+ tfndb list
	//key_t newEndKey = m_endKey;
	char newEndKey[MAX_KEY_BYTES];
	KEYSET(newEndKey,m_endKey,m_ks);

	//CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	//char *coll = cr->m_coll;

	/*
	if ( m_rdbId == RDB_TITLEDB ) { // && m_rdbId == RDB_TFNDB ) {
		//int64_t docId1 = g_titledb.getDocIdFromKey ( m_startKey );
	       int64_t docId1=g_titledb.getDocIdFromKey((key_t *)m_startKey);
		//int64_t docId2 = g_titledb.getDocIdFromKey ( m_endKey );
		// tfndb is pretty much uniformly distributed
		RdbBase *ubase = getRdbBase(RDB_TFNDB,m_coll);
		if ( ! ubase ) return true;
		int64_t space    = ubase->getDiskSpaceUsed();
		//int64_t readSize = (space * (docId2-docId1)) / DOCID_MASK;
		int64_t bufSize  = g_conf.m_mergeBufSize;
		// for now force to 100k
		bufSize = 100000;
		if ( bufSize > space ) bufSize = space;
		int64_t docId3   = (int64_t) (((double)bufSize /
						  (double)space) *
			(double)DOCID_MASK  + docId1);
		// constrain newEndKey based on docId3
		if ( docId3 < 0 ) docId3 = DOCID_MASK;
		//if ( docId3 >= DOCID_MASK ) newEndKey.setMax();
		if ( docId3 >= DOCID_MASK ) KEYMAX(newEndKey,m_ks);
		//else newEndKey = g_titledb.makeLastKey ( docId3 );
		else {
			key_t nk = g_titledb.makeLastKey(docId3);
			KEYSET(newEndKey,(char *)&nk,m_ks);
		}
		//log(LOG_DEBUG,"build: remapping endkey from %"XINT32".%"XINT64" to "
		//    "%"XINT32".%"XINT64" to avoid big tfndb read.",
		//    m_endKey.n1,m_endKey.n0, newEndKey.n1,newEndKey.n0);
		log(LOG_DEBUG,"build: remapping endkey from %"XINT64".%"XINT64" to "
		    "%"XINT64".%"XINT64" to avoid big tfndb read.",
		    KEY1(m_endKey,m_ks),KEY0(m_endKey),
		    KEY1(newEndKey,m_ks),KEY0(newEndKey));
	}
	*/
	// . this returns false if blocked, true otherwise
	// . sets g_errno on error
	// . we return false if it blocked
	// . m_maxBufSize may be exceeded by a rec, it's just a target size
	// . niceness is usually MAX_NICENESS, but reindex.cpp sets to 0
	// . this was a call to Msg3, but i made it call Msg5 since
	//   we now do the merging in Msg5, not in msg3 anymore
	// . this will now handle truncation, dup and neg rec removal
	// . it remembers last termId and count so it can truncate even when
	//   IndexList is split between successive reads
	// . IMPORTANT: when merging titledb we could be merging about 255
	//   files, so if we are limited to only X fds it can have a cascade
	//   affect where reading from one file closes the fd of another file
	//   in the read (since we call open before spawning the read thread)
	//   and can therefore take 255 retries for the Msg3 to complete 
	//   because each read gives a EFILCLOSED error.
	//   so to fix it we allow one retry for each file in the read plus
	//   the original retry of 25
	int32_t nn = base->getNumFiles();
	if ( m_numFiles > 0 && m_numFiles < nn ) nn = m_numFiles;
	// don't access any biased page caches
	bool usePageCache = true;
	if ( m_rdbId == RDB_CLUSTERDB )
		usePageCache = false;
	// . i don't trust page cache too much (mdw)... well, give it a shot
	// . see if ths helps fix WD corruption... i doubt it
	usePageCache = false;
	// for now force to 100k
	int32_t bufSize = 100000; // g_conf.m_mergeBufSize , // minRecSizes
	// get it
	return m_msg5.getList ( m_rdbId        ,
				m_collnum           ,
				&m_list        ,
				m_startKey     ,
				newEndKey      , // usually is maxed!
				bufSize        ,
				false          , // includeTree?
				false          , // add to cache?
				0              , // max cache age for lookup
				m_startFileNum , // startFileNum
				m_numFiles     ,
				this           , // state 
				gotListWrapper , // callback
				m_niceness     , // niceness
				true           , // do error correction?
				NULL           , // cache key ptr
				0              , // retry #
				nn + 75        , // max retries (mk it high)
				false          , // compensate for merge?
				-1LL           , // sync point
				&m_msg5b       ,
				true           , // isRealMerge? absolutely!
				usePageCache   );
}

void gotListWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	// get a ptr to ourselves
	RdbMerge *THIS = (RdbMerge *)state;
 loop:
	// if g_errno is out of memory then msg3 wasn't able to get the lists
	// so we should sleep and retry
	if ( g_errno == ENOMEM || g_errno == ENOTHREADSLOTS ) { 
		THIS->doSleep(); return; }
	// if g_errno we're done
	if ( g_errno || THIS->m_doneMerging ) { THIS->doneMerging(); return; }
	// return if this blocked
	if ( ! THIS->dumpList ( ) ) return;
	// return if this blocked
	if ( ! THIS->getNextList() ) return;
	// otherwise, keep on trucking
	goto loop;
}

// called after sleeping for 1 sec because of ENOMEM or ENOTHREADSLOTS
void tryAgainWrapper ( int fd , void *state ) {
	// if power is still off, keep things suspended
	if ( ! g_process.m_powerIsOn ) return;
	// get a ptr to ourselves
	RdbMerge *THIS = (RdbMerge *)state;
	// unregister the sleep callback
	g_loop.unregisterSleepCallback ( THIS, tryAgainWrapper );
	// clear this
	g_errno = 0;
	// return if this blocked
	if ( ! THIS->getNextList() ) return;
	// if this didn't block do the loop
	gotListWrapper ( THIS , NULL , NULL );
}
		
// similar to gotListWrapper but we call getNextList() before dumpList()
void dumpListWrapper ( void *state ) {
	// debug msg
	log(LOG_DEBUG,"db: Dump of list completed: %s.",mstrerror(g_errno));
	// get a ptr to ourselves
	RdbMerge *THIS = (RdbMerge *)state;

 loop:
	// collection reset or deleted while RdbDump.cpp was writing out?
	if ( g_errno == ENOCOLLREC ) { THIS->doneMerging(); return; }
	// return if this blocked
	if ( ! THIS->getNextList() ) return;
	// if g_errno is out of memory then msg3 wasn't able to get the lists
	// so we should sleep and retry
	if ( g_errno == ENOMEM || g_errno == ENOTHREADSLOTS ) { 
		// if the dump failed, it should reset m_dump.m_offset of
		// the file to what it was originally (in case it failed
		// in adding the list to the map). we do not need to set
		// m_startKey back to the startkey of this list, because
		// it is *now* only advanced on successful dump!!
		THIS->doSleep(); return; }
	// . if g_errno we're done
	// . if list is empty we're done
	if ( g_errno || THIS->m_doneMerging ) { THIS->doneMerging(); return; }
	// return if this blocked
	if ( ! THIS->dumpList ( ) ) return;
	// otherwise, keep on trucking
	goto loop;
}

// . return false if blocked, true otherwise
// . set g_errno on error
// . list should be truncated, possible have all negative keys removed,
//   and de-duped thanks to RdbList::indexMerge_r() and RdbList::merge_r()
bool RdbMerge::dumpList ( ) {
	// return true on g_errno
	if ( g_errno ) return true;

	// . it's suspended so we count this as blocking
	// . resumeMerge() will call getNextList() again, not dumpList() so
	//   don't advance m_startKey
	if ( m_isSuspended ) {
		m_isReadyToSave = true;
		return false;
	}

	// . set the list to only those records that should be in our group
	// . filter the records that don't belong in this group via groupId
	//filterList ( &m_list );

	// keep track of how many dups we removed for indexdb
	m_dupsRemoved += m_msg5.getDupsRemoved();

	// . compute the new m_startKey to get the next list from disk
	// . m_list was formed via RdbList::merge() 
	// . m_list may be empty because of negative/positive collisions
	//   but there may still be data left
	//m_startKey = m_list.getLastKey() ;
	//m_list.getLastKey(m_startKey) ;
	// if we use getLastKey() for this the merge completes but then
	// tries to merge two empty lists and cores in the merge function
	// because of that. i guess it relies on endkey rollover only and
	// not on reading less than minRecSizes to determine when to stop
	// doing the merge.
	m_list.getEndKey(m_startKey) ;
	//m_startKey += (uint32_t)1;
	KEYADD(m_startKey,1,m_ks);

	/////
	//
	// dedup for spiderdb before we dump it. try to save disk space.
	//
	/////
	if ( m_rdbId == RDB_SPIDERDB )
		// removeNegRecs? = false
		dedupSpiderdbList(&m_list,m_niceness,false); 

	// if the startKey rolled over we're done
	//if ( m_startKey.n0 == 0LL && m_startKey.n1 == 0 ) m_doneMerging=true;
	if ( KEYCMP(m_startKey,KEYMIN(),m_ks)==0 ) m_doneMerging = true;
	// debug msg
	log(LOG_DEBUG,"db: Dumping list.");
	// debug msg
	//fprintf(stderr,"list startKey.n1=%"UINT32",n0=%"UINT64", endKey.n1=%"UINT32",n0=%"UINT64","
	//	" size=%"INT32"\n", 
	//	m_list.getStartKey().n1, 
	//	m_list.getStartKey().n0, 
	//	m_list.getLastKey().n1, 
	//	m_list.getLastKey().n0,  m_list.getListSize() );
	// . send the whole list to the dump
	// . it returns false if blocked, true otherwise
	// . it sets g_errno on error
	// . it calls dumpListWrapper when done dumping
	// . return true if m_dump had an error or it did not block
	// . if it gets a EFILECLOSED error it will keep retrying forever
	return m_dump.dumpList ( &m_list , m_niceness , false/*recall?*/ ) ;
}

void RdbMerge::doneMerging ( ) {
	// save this
	int32_t saved = g_errno;
	// let RdbDump free its m_verifyBuf buffer if it existed
	m_dump.reset();
	// debug msg
	//fprintf(stderr,"exiting, g_errno=%s!\n",mstrerror(g_errno));
	//exit(-1);
	// . free the list's memory, reset() doesn't do it
	// . when merging titledb i'm still seeing 200MB allocs to read from
	//   tfndb.
	m_list.freeList();
	// nuke our msg3
	//delete (m_msg3);
	// log a msg
	log(LOG_INFO,"db: Merge status: %s.",mstrerror(g_errno));
	// . reset our class
	// . this will free it's cutoff keys buffer, trash buffer, treelist
	// . TODO: should we not reset to keep the mem handy for next time
	//   to help avoid out of mem errors?
	m_msg5.reset();
	// . do we really need these anymore?
	// . turn these off before calling incorporateMerge() since it
	//   will call attemptMerge() on all the other dbs
	m_isMerging     = false;
	m_isSuspended   = false;

	// if collection rec was deleted while merging files for it
	// then the rdbbase should be NULL i guess.
	if ( saved == ENOCOLLREC ) return;

	// if we are exiting then dont bother renaming the files around now.
	// this prevents a core in RdbBase::incorporateMerge()
	if ( g_process.m_mode == EXIT_MODE ) {
		log("merge: exiting. not ending merge.");
		return;
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return;
	// pass g_errno on to incorporate merge so merged file can be unlinked
	base->incorporateMerge ( );
	// nuke the lock so others can merge
	//s_isMergeLocked = false;
}

// . do not call this if "list" is empty
// . remove records whose keys don't belong
// . when we split the db cuz we scaled to more groups this will rid us
//   of data we no longer control
// . a split is done by turning on the next bit in m_groupMask starting
//   at the highest bit going down
// . this spiderdb thang is a HACK
// . TODO: now tfndb and titledb are special kinda like spiderdb
//         so use g_tfndb.getGroupId() and g_titledb.getGroupId()
/*
void RdbMerge::filterList ( RdbList *list ) {
	// set these for ease of use
	uint32_t gid   = g_hostdb.m_groupId;
	uint32_t gmask = g_hostdb.m_groupMask;
	// return if no mask specified
	if ( gmask == 0  ) return;
	// return if list is empty
	if ( list->getListSize() == 0 ) return;
	// since list was formed via RdbList::merge() it's getLastKey()
	// should be valid
	key_t lastKey  = list->getLastKey ();
	key_t firstKey = list->getFirstKey();
	// reset the list ptr since we might scan records in the list
	list->resetListPtr();
	// . spiderdb masks on the key's low int32_t because it stores
	//   a timestamp for ordering it's urls in the high int32_t
	// . every other db masks on the high int32_t
	// . it's easy to mask on the high int32_t cuz we're sorted by that!
	if ( m_rdb != g_spiderdb.getRdb() ) {
		// determine if firstKey and lastKey are in our group now
		//
		//
		// TODO: if we're rdbId == RDB_SPIDERDB filter by n0, not n1
		//
		//
		bool in1 = ( (firstKey.n1 & gmask) == gid );
		bool in2 = ( (lastKey.n1  & gmask) == gid );
		// bail quickly if we don't need to remove anything
		if ( in1 && in2 ) return;
		// erase list's records if both are bad
		if ( ! in1 && ! in2 ) { list->reset(); return; }
		// . otherwise find the boundary between what we want and don't
		// . if the first key is chop off the bad tail
		if ( in1 ) {
			while ( (list->getCurrentKey().n1 & gmask) == gid )
				list->skipCurrentRecord();
			list->m_listSize = list->m_listPtr - list->m_list;
			list->m_listEnd  = list->m_listPtr ;
			return;
		}
		// . otherwise, move the good tail over the bad head
		// . but find the boundary this way (!=)
		while ( (list->getCurrentKey().n1 & gmask) != gid )
			list->skipCurrentRecord();
		// get size of list/recs we haven't visited yet
		int32_t backSize = list->m_listEnd - list->m_listPtr ;
		// have those bury what we did visit
		memmove ( list->m_list , list->m_listPtr , backSize );
		list->m_listSize = backSize;
		list->m_listEnd  = list->m_list + backSize;
		return;
	}			
	// . TODO: each file should have a groupId/groupMask from when it
	//         was formed so we can even avoid this check most of the time
	// . now we must filter out records that don't belong in spiderdb
        // . changing the groupMask/groupId is somewhat rare so first
        //   do a check to see if anything needs to be nuked
        while ( (list->getCurrentKey().n0 & gmask) == gid ) 
                if ( ! list->skipCurrentRecord () ) break;
        // return if nothing needs to be nuked
        if ( list->isExhausted() ) return;
        // otherwise let's remove the records that don't belong in this list
        char *addPtr = list->m_list;
        char *rec;
        int32_t  recSize;
        bool  status;
	// reset m_listPtr since we're scanning again
        list->resetListPtr();
 loop:
        // . skip over records that don't belong in our group, groupId
	// . skipCurrentRecord() returns false if skipped to end of list
        while ( (list->getCurrentKey().n0 & gmask) != gid ) 
                if ( ! list->skipCurrentRecord() ) goto done;
        // now copy this record that does belong to "addPtr"
        rec     = list->getCurrentRec    ();
        recSize = list->getCurrentRecSize();
        status  = list->skipCurrentRecord();
        gbmemcpy ( addPtr , rec , recSize );
        addPtr += recSize;
        if ( status ) goto loop;
 done:
	// now set our new list size
	list->m_listSize = addPtr - list->m_list;
	// and listEnd...
	list->m_listEnd  = list->m_list + list->m_listSize;
}
*/
