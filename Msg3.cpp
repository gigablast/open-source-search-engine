#include "gb-include.h"

#include "Msg3.h"
#include "Rdb.h"
#include "Threads.h"
#include "Stats.h"     // for timing and graphing merge time
//#include "Sync.h"      // incremental syncing
//#include "Tfndb.h"     // makeMaxKey()
#include "PingServer.h"
#include "Process.h"

static void  doneScanningWrapper ( void *state ) ;

//bool mainShutdown ( bool urgent );

int32_t g_numIOErrors = 0;

Msg3::Msg3() {
	m_alloc = NULL;
	m_numScansCompleted = 0;
	m_numScansStarted = 0;
}

Msg3::~Msg3() {
	reset();
}

void Msg3::reset() {
	if ( m_numScansCompleted < m_numScansStarted ) { char *xx=NULL;*xx=0; }
	m_hadCorruption = false;
	// reset # of lists to 0
	m_numScansCompleted = 0;
	m_numScansStarted = 0;
	if ( ! m_alloc        ) return;
	// call destructors
	for ( int32_t i = 0 ; i < m_numChunks ; i++ ) m_lists[i].destructor();
	if ( m_alloc == m_buf ) return;
	mfree ( m_alloc , m_allocSize , "Msg3" );
	m_alloc = NULL;
}

key192_t makeCacheKey ( int64_t vfd ,
			int64_t offset ,
			int64_t readSize ) {
	key192_t k;
	k.n2 = vfd;
	k.n1 = readSize;
	k.n0 = offset;
	return k;
}

RdbCache g_rdbCaches[5];

class RdbCache *getDiskPageCache ( char rdbId ) {

	RdbCache *rpc = NULL;
	int64_t *maxSizePtr = NULL;
	int64_t maxMem;
	int64_t maxRecs;
	char *dbname;
	if ( rdbId == RDB_POSDB ) {
		rpc = &g_rdbCaches[0];
		maxSizePtr = &g_conf.m_posdbFileCacheSize;
		maxMem = *maxSizePtr;
		maxRecs = maxMem / 5000;
		dbname = "posdbcache";
	}
	if ( rdbId == RDB_TAGDB ) {
		rpc = &g_rdbCaches[1];
		maxSizePtr = &g_conf.m_tagdbFileCacheSize;
		maxMem = *maxSizePtr;
		maxRecs = maxMem / 200;
		dbname = "tagdbcache";
	}
	if ( rdbId == RDB_CLUSTERDB ) {
		rpc = &g_rdbCaches[2];
		maxSizePtr = &g_conf.m_clusterdbFileCacheSize;
		maxMem = *maxSizePtr;
		maxRecs = maxMem / 32;
		dbname = "clustcache";
	}
	if ( rdbId == RDB_TITLEDB ) {
		rpc = &g_rdbCaches[3];
		maxSizePtr = &g_conf.m_titledbFileCacheSize;
		maxMem = *maxSizePtr;
		maxRecs = maxMem / 3000;
		dbname = "titdbcache";
	}
	if ( rdbId == RDB_SPIDERDB ) {
		rpc = &g_rdbCaches[4];
		maxSizePtr = &g_conf.m_spiderdbFileCacheSize;
		maxMem = *maxSizePtr;
		maxRecs = maxMem / 3000;
		dbname = "spdbcache";
	}

	if ( ! rpc )
		return NULL;

	if ( maxMem < 0 ) maxMem = 0;

	// did size change? if not, return it
	if ( rpc->m_maxMem == maxMem )
		return rpc;

	// re-init or init for the first time here
	if ( ! rpc->init ( maxMem ,
			   -1 , // fixedDataSize. -1 since we are lists
			   false , // support lists?
			   maxRecs ,
			   false , // use half keys?
			   dbname ,
			   false , // loadfromdisk
			   sizeof(key192_t), // cache key size
			   0 , // data key size
			   -1 ) )  // numptrsmax
		return NULL;

	return rpc;
}

// . return false if blocked, true otherwise
// . set g_errno on error
// . read list of keys in [startKey,endKey] range
// . read at least "minRecSizes" bytes of keys in that range
// . the "m_endKey" of resulting, merged list may have a smaller endKey
//   than the argument, "endKey" due to limitation by "minRecSizes"
// . resulting list will contain ALL keys between ITS [m_startKey,m_endKey]
// . final merged list "should" try to have a size of at least "minRecSizes"
//   but due to negative/postive rec elimination may be less
// . the endKey of the lists we read may be <= "endKey" provided
// . we try to shrink the endKey if minRecSizes is >= 0 in order to
//   avoid excessive reading
// . by shrinking the endKey we cannot take into account the size of deleted
//   records, so therefore we may fall int16_t of "minRecSizes" in actuality,
//   in fact, the returned list may even be empty with a shrunken endKey
// . we merge all lists read from disk into the provided "list"
// . caller should call Msg3.getList(int32_t i) and Msg3:getNumLists() to retrieve
// . this makes the query engine faster since we don't need to merge the docIds
//   and can just send them across the network separately and they will be
//   hashed into IndexTable's table w/o having to do time-wasting merging.
// . caller can specify array of filenums to read from so incremental syncing
//   in Sync class can just read from titledb*.dat files that were formed
//   since the last sync point.
bool Msg3::readList  ( char           rdbId         ,
		       collnum_t collnum ,
		       //key_t          startKey      , 
		       //key_t          endKey        , 
		       char          *startKeyArg      , 
		       char          *endKeyArg        , 
		       int32_t           minRecSizes   , // max size of scan
		       int32_t           startFileNum  , // first file to scan
		       int32_t           numFiles      , // rel. to startFileNum
		       void          *state         , // for callback
		       void        (* callback ) ( void *state ) ,
		       int32_t           niceness      ,
		       int32_t           retryNum      ,
		       int32_t           maxRetries    ,
		       bool           compensateForMerge ,
		       int64_t      syncPoint     ,
		       bool           justGetEndKey ,
		       bool           allowPageCache ,
		       bool           hitDisk        ) {

	// set this to true to validate
	m_validateCache = false;//true;

	// clear, this MUST be done so if we return true g_errno is correct
	g_errno = 0;
	// assume lists are not checked for corruption
	m_listsChecked = false;
	// warn
	if ( minRecSizes < -1 ) {
		log(LOG_LOGIC,"db: Msg3 got minRecSizes of %"INT32", changing "
		    "to -1.",minRecSizes);
		minRecSizes = -1;
	}
	// reset m_alloc and data in all lists in case we are a re-call
	reset();
	// warning
	if ( collnum < 0 ) log(LOG_LOGIC,"net: NULL collection. msg3.");
	// remember the callback
	m_rdbId              = rdbId;
	m_collnum = collnum;
	m_callback           = callback;
	m_state              = state;
	m_niceness           = niceness;
	m_numScansCompleted  = 0;
	m_retryNum           = retryNum;
	m_maxRetries         = maxRetries;
	m_compensateForMerge = compensateForMerge;
	m_allowPageCache     = allowPageCache;
	m_hitDisk            = hitDisk;
	m_hadCorruption      = false;
	// get keySize of rdb
	m_ks = getKeySizeFromRdbId ( m_rdbId );
	// reset the group error
	m_errno    = 0;
	// . reset all our lists 
	// . these are reset in call the RdbScan::setRead() below
	//for ( int32_t i = 0 ; i < MAX_RDB_FILES ; i++ ) m_lists[i].reset();
	// . ensure startKey last bit clear, endKey last bit set
	// . no! this warning is now only in Msg5
	// . if RdbMerge is merging some files, not involving the root 
	//   file, then we can expect to get a lot of unmatched negative recs.
	// . as a consequence, our endKeys may often be negative. This means
	//   it may not annihilate with the positive key, but we should only
	//   miss like this at the boundaries of the lists we fetch.
	// . so in that case RdbList::merge will stop merging once the
	//   minRecSizes limit is reached even if it means ending on a negative
	//   rec key
	//if ( (startKey.n0 & 0x01) == 0x01 ) 
	if ( !KEYNEG(startKeyArg) )
		log(LOG_REMIND,"net: msg3: StartKey lastbit set."); 
	if (  KEYNEG(endKeyArg) )
		log(LOG_REMIND,"net: msg3: EndKey lastbit clear."); 

	// declare vars here becaues of 'goto skip' below
	int32_t mergeFileNum = -1 ;
	int32_t max ;

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;

	// if caller specified exactly
	/*
	m_syncPoint = syncPoint;
	if ( syncPoint != -1 && syncPoint != 0 ) {
		// . store them all
		// . what if we merged one of these files (or are merging)???
		// . then sync class should not discard syncpoints until no
		//   longer syncing and we'll know about it
		// . this should compensate for merges by including any files
		//   that are merging a file in m_fileNums
		m_numFileNums = g_sync.getFileNums ( m_rdbId       ,
						     m_coll        ,
						     m_syncPoint   ,
						     m_fileNums    , 
						     MAX_RDB_FILES );
		log("NOOOOOO. we do not alloc if we go to skip!!");
		char *xx = NULL; *xx = 0;
		// bring back the comment below... i removed it because i added
		// "int32_t chunk" et al below and didn't want to move them.
		//if ( m_numFileNums > 0 ) goto skip;
		log("net: Trying to read data in %s from files generated after"
		    " a sync point %"UINT64" in \"sync\" file, but none found.",
		    base->m_dbname,m_syncPoint);
		return true;
	}
	// should we read all?
	if ( m_syncPoint == 0 ) {
		numFiles     = -1;
		startFileNum =  0;
	}
	*/

	// store the file numbers in the array, these are the files we read
	m_numFileNums = 0;

	// save startFileNum here, just for recall
	m_startFileNum = startFileNum;
	m_numFiles     = numFiles;

	// . if we have a merge going on, we may have to change startFileNum
	// . if some files get unlinked because merge completes then our 
	//   reads will detect the error and loop back here
	// . we launch are reads right after this without giving up the cpu
	//   and we use file descriptors, so any changes to Rdb::m_files[]
	//   should not hurt us
	// . WARNING: just make sure you don't lose control of cpu until after
	//   you call RdbScan::set()
	// . we use hasMergeFile() instead of isMerging() because he may not 
	//   be merging cuz he got suspended or he restarted and
	//   hasn't called attemptMerge() yet, but he may still contain it
	if ( g_conf.m_logDebugQuery )
		log(LOG_DEBUG,
		    "net: msg3: "
		    "c=%"INT32" hmf=%"INT32" sfn=%"INT32" msfn=%"INT32" nf=%"INT32" db=%s.",
		     (int32_t)compensateForMerge,(int32_t)base->hasMergeFile(),
		     (int32_t)startFileNum,(int32_t)base->m_mergeStartFileNum-1,
		     (int32_t)numFiles,base->m_dbname);
	int32_t pre = -10;
	if ( compensateForMerge && base->hasMergeFile() && 
	     startFileNum >= base->m_mergeStartFileNum - 1 &&
	     (startFileNum > 0 || numFiles != -1) ) {
		// now also include the file being merged into, but only
		// if we are reading from a file being merged...
		if ( startFileNum < base->m_mergeStartFileNum +
		     base->m_numFilesToMerge - 1 )
			//m_fileNums [ m_numFileNums++ ] =
			//	base->m_mergeStartFileNum - 1;
			pre = base->m_mergeStartFileNum - 1;
		// debug msg
		if ( g_conf.m_logDebugQuery )
			log(LOG_DEBUG,
			   "net: msg3: startFileNum from %"INT32" to %"INT32" (mfn=%"INT32")",
			    startFileNum,startFileNum+1,mergeFileNum);
		// if merge file was inserted before us, inc our file number
		startFileNum++;
	}
	// adjust num files if we need to, as well
	if ( compensateForMerge && base->hasMergeFile() && 
	     startFileNum < base->m_mergeStartFileNum - 1 &&
	     numFiles != -1 &&
	     startFileNum + numFiles - 1 >= base->m_mergeStartFileNum - 1 ) {
		// debug msg
		if ( g_conf.m_logDebugQuery )
			log(LOG_DEBUG,"net: msg3: numFiles up one.");
		// if merge file was inserted before us, inc our file number
		numFiles++;
	}

	// . how many rdb files does this base have?
	// . IMPORTANT: this can change since files are unstable because they
	//   might have all got merged into one!
	// . so do this check to make sure we're safe... especially if
	//   there was an error before and we called readList() on ourselves
	max = base->getNumFiles();
	// -1 means we should scan ALL the files in the base
	if ( numFiles == -1 ) numFiles = max;
	// limit it by startFileNum, however
	if ( numFiles > max - startFileNum ) numFiles = max - startFileNum;
	// set g_errno and return true if it is < 0
	if ( numFiles < 0 ) { 
		log(LOG_LOGIC,
		   "net: msg3: readList: numFiles = %"INT32" < 0 (max=%"INT32")(sf=%"INT32")",
		    numFiles , max , startFileNum );
		g_errno = EBADENGINEER; 
		// force core dump
		char *xx=NULL;*xx=0;
		return true; 
	}

	// . allocate buffer space
	// . m_scans, m_startpg, m_endpg, m_hintKeys, m_hintOffsets,
	//   m_fileNums, m_lists, m_tfns
	int32_t chunk = sizeof(RdbScan) + // m_scans
		4 +                    // m_startpg
		4 +                    // m_endpg
		//sizeof(key_t) +        // m_hintKeys
		m_ks +                 // m_hintKeys
		4 +                    // m_hintOffsets
		4 +                    // m_fileNums
		sizeof(RdbList) +      // m_lists
		4 ;                    // m_tfns
	int32_t nn   = numFiles;
	if ( pre != -10 ) nn++;
	m_numChunks = nn;
	int32_t need = nn * (chunk);
	m_alloc = m_buf;
	if ( need > (int32_t)MSG3_BUF_SIZE ) {
		m_allocSize = need;
		m_alloc = (char *)mcalloc ( need , "Msg3" );
		if ( ! m_alloc ) {
			log("disk: Could not allocate %"INT32" bytes read "
			    "structures to read %s.",need,base->m_dbname);
			return true;
		}
	}
	char *p = m_alloc;
	m_scans       = (RdbScan *)p; p += nn * sizeof(RdbScan);
	m_startpg     = (int32_t    *)p; p += nn * 4;
	m_endpg       = (int32_t    *)p; p += nn * 4;
	//m_hintKeys    = (key_t   *)p; p += nn * sizeof(key_t);
	m_hintKeys    = (char    *)p; p += nn * m_ks;
	m_hintOffsets = (int32_t    *)p; p += nn * 4;
	m_fileNums    = (int32_t    *)p; p += nn * 4;
	m_lists       = (RdbList *)p; p += nn * sizeof(RdbList);
	m_tfns        = (int32_t    *)p; p += nn * 4;
	// sanity check
	if ( p - m_alloc != need ) {
		log(LOG_LOGIC,"disk: Bad malloc in Msg3.cpp.");
		char *xx = NULL; *xx = 0;
	}
	// call constructors
	for ( int32_t i = 0 ; i < nn ; i++ ) m_lists[i].constructor();
	// make fix from up top
	if ( pre != -10 ) m_fileNums [ m_numFileNums++ ] = pre;

	// store them all
	for ( int32_t i = startFileNum ; i < startFileNum + numFiles ; i++ )
		m_fileNums [ m_numFileNums++ ] = i;

	// we skip down to here when a syncPoint was used to set the
	// m_fileNums/m_numFileNums array of files to read from
// JAB: warning abatement
// skip:
	// . remove file nums that are being unlinked after a merge now
	// . keep it here (below skip: label) so sync point reads can use it
	int32_t n = 0;
	for ( int32_t i = 0 ; i < m_numFileNums ; i++ ) {
		// skip those that are being unlinked after the merge
		if ( base->m_isUnlinking && 
		     m_fileNums[i] >= base->m_mergeStartFileNum &&
		     m_fileNums[i] <  base->m_mergeStartFileNum + 
		                      base->m_numFilesToMerge      )
			continue;
		// otherwise, keep it
		m_fileNums[n++] = m_fileNums[i];
	}
	m_numFileNums = n;

	// . if root file is being merged, he's file #0, & root file is file #1
	// . this is a hack so caller gets what he wants
	//if ( startFileNum == 0 && base->getFileId(0) == 0 && numFiles == 1 )
	//	numFiles = 2;

	// remember the file range we should scan
	m_numScansStarted    = 0;
	m_numScansCompleted  = 0;
	//m_startKey           = startKey;
	//m_endKey             = endKey;
	//m_constrainKey       = endKey; // set in case justGetEndKey is true
	KEYSET(m_startKey,startKeyArg,m_ks);
	KEYSET(m_endKey,endKeyArg,m_ks);
	KEYSET(m_constrainKey,endKeyArg,m_ks);//set incase justGetEndKey istrue
	m_minRecSizes        = minRecSizes;
	m_compensateForMerge = compensateForMerge;
	// bail if 0 files to scan -- no! need to set startKey/endKey
	if ( numFiles == 0 ) return true;
	// don't read anything if endKey < startKey
	//if ( m_startKey > m_endKey ) return true;
	if ( KEYCMP(m_startKey,m_endKey,m_ks)>0 ) return true;
	// keep the original in tact in case g_errno == ETRYAGAIN
	//m_endKeyOrig        = endKey;
	KEYSET(m_endKeyOrig,endKeyArg,m_ks);
	m_minRecSizesOrig   = minRecSizes;
	// start reading at this key
	m_fileStartKey = startKeyArg;
	// start the timer, keep it fast for clusterdb though
	if ( g_conf.m_logTimingDb ) m_startTime = gettimeofdayInMilliseconds();
	// translate base to an id, for the sake of m_msg0
	//char baseId = m_msg0->getRdbId ( base );
	// map ptrs
	RdbMap **maps = base->getMaps();
	// . we now boost m_minRecSizes to account for negative recs 
	// . but not if only reading one list, cuz it won't get merged and
	//   it will be too big to send back
	if ( m_numFileNums > 1 ) compensateForNegativeRecs ( base );
	// . often endKey is too big for an efficient read of minRecSizes bytes
	//   because we end up reading too much from all the files
	// . this will set m_startpg[i], m_endpg[i] for each RdbScan/RdbFile
	//   to ensure we read "minRecSizes" worth of records, not much more
	// . returns the new endKey for all ranges
	// . now this just overwrites m_endKey
	//m_endKey = setPageRanges ( base           ,
	setPageRanges ( base           ,
			m_fileNums     ,
			m_numFileNums  ,
			m_fileStartKey , // start reading @ key
			m_endKey       , // stop reading @ key
			m_minRecSizes  );

	// . NEVER let m_endKey be a negative key, because it will 
	//   always be unmatched, since delbit is cleared
	// . adjusting it here ensures our generated hints are valid
	// . we will use this key to call constrain() with
	//m_constrainKey = m_endKey;
	//if ( ( m_constrainKey.n0 & 0x01) == 0x00 ) 
	//	m_constrainKey -= (uint32_t)1;
	KEYSET(m_constrainKey,m_endKey,m_ks);
	if ( KEYNEG(m_constrainKey) )
		KEYSUB(m_constrainKey,1,m_ks);

	// if m_endKey splits some keys that should be together, we need to
	// decrease it so such a split doesn't happen. 
	//if ( m_endKey != m_endKeyOrig && m_rdbId==RDB_TFNDB && numFiles > 0){
	/*
	if ( KEYCMP(m_endKey,m_endKeyOrig,m_ks)!=0 && m_rdbId==RDB_TFNDB && 
	     numFiles > 0 ) {
		// . drop the docid down one and max out the tfn...
		// . we may lose some recs when we call constrain, but at least
		//   we are guaranteed not to split a sequence with the same
		//   docid but different tfns... thus the disk merge will
		//   then work correctly. before we were splitting these
		//   sequence between successive disk reads and they were not
		//   getting annihilated together in the call to indexMerge_r()
		int64_t d = g_tfndb.getDocId ( (key_t *)&m_endKey );
		if ( d > 0 ) d = d - 1LL;
		//m_constrainKey = g_tfndb.makeMaxKey(d);
		*(key_t *)m_constrainKey = g_tfndb.makeMaxKey(d);
		// set the half bit on
		//m_constrainKey.n0 |= 0x02;
		*m_constrainKey |= 0x02;
		// note it
		//logf(LOG_DEBUG,"oldukey.n1=%"XINT32" n0=%"XINT64" new.n1=%"XINT32" n0=%"XINT64"",
		//     m_endKey.n1,m_endKey.n0,
		//     m_constrainKey.n1,m_constrainKey.n0);
	}
	*/

	// Msg5 likes to get the endkey for getting the list from the tree
	if ( justGetEndKey ) return true;

	// sanity check
	if ( m_numFileNums > nn ) {
		log(LOG_LOGIC,"disk: Failed sanity check in Msg3.");
		char *xx = NULL; *xx = 0;
	}

	// debug msg
	//log("msg3 getting list (msg5=%"UINT32")",m_state);
	// . MDW removed this -- go ahead an end on a delete key
	// . RdbMerge might not pick it up this round, but oh well
	// . so we can have both positive and negative co-existing in same file
	// make sure the last bit is set so we don't end on a delete key
	//m_endKey.n0 |= 0x01LL;
	// . now start reading/scanning the files
	// . our m_scans array starts at 0
	for ( int32_t i = 0 ; i < m_numFileNums ; i++ ) {
		// get the page range
		//int32_t p1 = m_startpg [ i ];
		//int32_t p2 = m_endpg   [ i ];
		//#ifdef GBSANITYCHECK
		int32_t fn = m_fileNums[i];
		// this can happen somehow!
		if ( fn < 0 ) {
			log(LOG_LOGIC,"net: msg3: fn=%"INT32". Bad engineer.",fn);
			continue;
		}
		// sanity check
		if ( i > 0 && m_fileNums[i-1] >= fn ) {
			log(LOG_LOGIC,
			    "net: msg3: files must be read in order "
			    "from oldest to newest so RdbList::indexMerge_r "
			    "works properly. Otherwise, corruption will "
			    "result. ");
			char *xx = NULL; *xx = 0;
			return true;
		}
		// . sanity check?
		// . no, we must get again since we turn on endKey's last bit
		int32_t p1 , p2;
		maps[fn]->getPageRange ( m_fileStartKey , 
					m_endKey       , 
					&p1            , 
					&p2            ,
					NULL           );
		//if ( p1 != p1c || p2 != p2c ) {
		//	fprintf(stderr,"Msg3::bad page range\n");
		//	sleep(50000);
		//}
		// sanity check, each endpg's key should be > endKey
		//if ( p2 < maps[fn]->getNumPages() && 
		//     maps[fn]->getKey ( p2 ) <= m_endKey ) {
		//	fprintf(stderr,"Msg3::bad page range 2\n");
		//	sleep(50000);
		//}
		//#endif
		//int32_t p1 , p2; 
		//maps[fn]->getPageRange (startKey,endKey,minRecSizes,&p1,&p2);
		// now get some read info
		int64_t offset      = maps[fn]->getAbsoluteOffset ( p1 );
		int32_t      bytesToRead = maps[fn]->getRecSizes ( p1, p2, false);
		// max out the endkey for this list
		// debug msg
		//#ifdef _DEBUG_		
		//if ( minRecSizes == 2000000 ) 
		//log("Msg3:: reading %"INT32" bytes from file #%"INT32"",bytesToRead,i);
		//#endif
		// inc our m_numScans
		m_numScansStarted++;
		// . keep stats on our disk accesses
		// . count disk seeks (assuming no fragmentation)
		// . count disk bytes read
		if ( bytesToRead > 0 ) {
			base->m_rdb->didSeek (             );
			base->m_rdb->didRead ( bytesToRead );
		}
		// . the startKey may be different for each RdbScan class
		// . RdbLists must have all keys within their [startKey,endKey]
		// . therefore set startKey individually from first page in map
		// . this endKey must be >= m_endKey 
		// . this startKey must be < m_startKey
		//key_t startKey = maps[fn]->getKey ( p1 );
		//key_t endKey   = maps[fn]->getKey ( p2 );
		char startKey2 [ MAX_KEY_BYTES ];
		char endKey2   [ MAX_KEY_BYTES ];
		maps[fn]->getKey ( p1 , startKey2 );
		maps[fn]->getKey ( p2 , endKey2 );
		//char *startKey = maps[fn]->getKeyPtr ( p1 );
		//char *endKey   = maps[fn]->getKeyPtr ( p2 );
		// store in here
		m_startpg [ i ] = p1;
		m_endpg   [ i ] = p2;

		// . we read UP TO that endKey, so reduce by 1
		// . but iff p2 is NOT the last page in the map/file
		// . maps[fn]->getKey(lastPage) will return the LAST KEY
		//   and maps[fn]->getOffset(lastPage) the length of the file
		//if ( maps[fn]->getNumPages()!=p2) endKey -=(uint32_t)1;
		if ( maps[fn]->getNumPages() != p2 ) KEYSUB(endKey2,1,m_ks);
		// otherwise, if we're reading all pages, then force the
		// endKey to virtual inifinite
		//else endKey.setMax();
		else KEYMAX(endKey2,m_ks);

		// . set up the hints
		// . these are only used if we are only reading from 1 file
		// . these are used to call constrain() so we can constrain
		//   the end of the list w/o looping through all the recs
		//   in the list
		int32_t h2 = p2 ;
		// decrease by one page if we're on the last page
		if ( h2 > p1 && maps[fn]->getNumPages() == h2 ) h2--;
		// . decrease hint page until key is <= endKey on that page
		//   AND offset is NOT -1 because the old way would give
		//   us hints passed the endkey
		// . also decrease so we can constrain on minRecSizes in
		//   case we're the only list being read
		// . use >= m_minRecSizes instead of >, otherwise we may
		//   never be able to set "size" in RdbList::constrain()
		//   because "p" could equal "maxPtr" right away
		while ( h2 > p1 && 
			//( maps[fn]->getKey   (h2) > m_constrainKey ||
		      (KEYCMP(maps[fn]->getKeyPtr(h2),m_constrainKey,m_ks)>0||
			  maps[fn]->getOffset(h2) == -1            ||
			  maps[fn]->getAbsoluteOffset(h2) - offset >=
			  m_minRecSizes ) )
			h2--;
		// now set the hint
		m_hintOffsets [ i ] = maps[fn]->getAbsoluteOffset ( h2 ) -
			              maps[fn]->getAbsoluteOffset ( p1 ) ;
		//m_hintKeys    [ i ] = maps[fn]->getKey            ( h2 );
		KEYSET(&m_hintKeys[i*m_ks],maps[fn]->getKeyPtr(h2),m_ks);

		// reset g_errno before calling setRead()
		g_errno = 0;
		// . this fix is now in RdbList::checklist_r()
		// . we can now have dup keys, so, we may read in
		//   a rec with key "lastMinKey" even though we don't read
		//   in the first key on the end page, so don't subtract 1...
		//if ( endKey != m_endKeyOrig ) 
		//	endKey += (uint32_t) 1;

		// timing debug
		if ( g_conf.m_logTimingDb )
			log(LOG_TIMING,
			    "net: msg: reading %"INT32" bytes from %s file #%"INT32" "
			     "(niceness=%"INT32")",
			     bytesToRead,base->m_dbname,i,m_niceness);

		// set the tfn
		if ( m_rdbId == RDB_TITLEDB )
			m_tfns[i] = base->getFileId2(m_fileNums[i]);

		// log huge reads, those hurt us
		if ( bytesToRead > 150000000 ) {
			logf(LOG_INFO,"disk: Reading %"INT32" bytes at offset %"INT64" "
			    "from %s.",
			    bytesToRead,offset,base->m_dbname);
		}

		// if any keys in the map are the same report corruption
		char tmpKey    [16];
		char lastTmpKey[16];
		int32_t ccount = 0;
		if ( bytesToRead     > 10000000      && 
		     bytesToRead / 2 > m_minRecSizes &&
		     base->m_fixedDataSize >= 0        ) {
			for ( int32_t pn = p1 ; pn <= p2 ; pn++ ) {
				maps[fn]->getKey ( pn , tmpKey );
				if ( KEYCMP(tmpKey,lastTmpKey,m_ks) == 0 ) 
					ccount++;
				gbmemcpy(lastTmpKey,tmpKey,m_ks);
			}
		}
		if ( ccount > 10 ) {
			logf(LOG_INFO,"disk: Reading %"INT32" bytes from %s file #"
			     "%"INT32" when min "
			     "required is %"INT32". Map is corrupt and has %"INT32" "
			     "identical consecutive page keys because the "
			     "map was \"repaired\" because out of order keys "
			     "in the index.",
			     (int32_t)bytesToRead,
			     base->m_dbname,fn,
			     (int32_t)m_minRecSizes,
			     (int32_t)ccount);
			m_numScansCompleted++;
			m_errno = ECORRUPTDATA;
			m_hadCorruption = true;
			//m_maxRetries = 0;
			break;
		}

		////////
		//
		// try to get from PAGE CACHE
		//
		////////
		BigFile *ff = base->getFile(m_fileNums[i]);
		RdbCache *rpc = getDiskPageCache ( m_rdbId );
		if ( ! m_allowPageCache ) rpc = NULL;
		// . vfd is unique 64 bit file id
		// . if file is opened vfd is -1, only set in call to open()
		int64_t vfd = ff->getVfd();
		key192_t ck = makeCacheKey ( vfd , offset, bytesToRead);
		char *rec; int32_t recSize;
		bool inCache = false;
		if ( rpc && vfd != -1 && ! m_validateCache ) 
			inCache = rpc->getRecord ( (collnum_t)0 , // collnum
						   (char *)&ck , 
						   &rec , 
						   &recSize ,
						   true , // copy?
						   -1 , // maxAge, none 
						   true ); // inccounts?
		m_scans[i].m_inPageCache = false;
		if ( inCache ) {
			m_scans[i].m_inPageCache = true;
			m_numScansCompleted++;
			// now we have to store this value, 6 or 12 so
			// we can modify the hint appropriately
			m_scans[i].m_shifted = *rec;
			m_lists[i].set ( rec +1,
					 recSize-1 ,
					 rec , // alloc
					 recSize , // allocSize
					 startKey2 ,
					 endKey2 ,
					 base->m_fixedDataSize ,
					 true , // owndata
					 base->useHalfKeys() ,
					 getKeySizeFromRdbId ( m_rdbId ) );
			continue;
		}

		// . do the scan/read of file #i
		// . this returns false if blocked, true otherwise
		// . this will set g_errno on error
		bool done = m_scans[i].setRead (base->getFile(m_fileNums[i]),
						base->m_fixedDataSize ,
						 offset                 ,
						 bytesToRead            ,
						 startKey2              ,
						 endKey2                ,
						m_ks                    ,
						 &m_lists[i]            ,
						 this                   ,
						 doneScanningWrapper    ,
						 base->useHalfKeys()    ,
						m_rdbId,
						 m_niceness             ,
						 m_allowPageCache       ,
						 m_hitDisk              ) ;
		// . damn, usually the above will indirectly launch a thread
		//   to do the reading, but it sets g_errno to EINTR,
		//   "interrupted system call"!
		// . i guess the thread does the read w/o blocking and then
		//   queues the signal on g_loop's queue before it exits
		// . try ignoring, and keep going
		if ( g_errno == EINTR ) {
			log("net: Interrupted system call while reading file. "
			    "Ignoring.");
			g_errno = 0;
		}
		// debug msg
		//fprintf(stderr,"Msg3:: reading %"INT32" bytes from file #%"INT32","
		//	"done=%"INT32",offset=%"INT64",g_errno=%s,"
		//	"startKey=n1=%"UINT32",n0=%"UINT64",  "
		//	"endKey=n1=%"UINT32",n0=%"UINT64"\n",
		//	bytesToRead,i,(int32_t)done,offset,mstrerror(g_errno),
		//	m_startKey,m_endKey);
		//if ( bytesToRead == 0 )
		//	fprintf(stderr,"shit\n");
		// if it did not block then it completed, so count it
		if ( done ) m_numScansCompleted++;
		// break on an error, and remember g_errno in case we block
		if ( g_errno && g_errno != ENOTHREADSLOTS ) { 
			int32_t tt = LOG_WARN;
			if ( g_errno == EFILECLOSED ) tt = LOG_INFO;
			log(tt,"disk: Reading %s had error: %s.",
			    base->m_dbname, mstrerror(g_errno));
			m_errno = g_errno; 
			break; 
		}
	}
	// debug test
	//if ( rand() % 100 <= 10 ) m_errno = EIO;

	// if we blocked, return false
	if ( m_numScansCompleted < m_numScansStarted ) return false;
	// . if all scans completed without blocking then wrap it up & ret true
	// . doneScanning may now block if it finds data corruption and must
	//   get the list remotely
	return doneScanning();
}

void doneScanningWrapper ( void *state ) {
	Msg3 *THIS = (Msg3 *) state;
	// inc the scan count
	THIS->m_numScansCompleted++;
	// we decided to try to ignore these errors
	if ( g_errno == EINTR ) {
		log("net: Interrupted system call while reading file. "
		    "Ignoring.");
		g_errno = 0;
	}
	// if we had an error, remember it
	if ( g_errno ) { 
		// get base, returns NULL and sets g_errno to ENOCOLLREC on err
		RdbBase *base; base=getRdbBase(THIS->m_rdbId,THIS->m_collnum);
		char *dbname = "NOT FOUND";
		if ( base ) dbname = base->m_dbname;
		int32_t tt = LOG_WARN;
		if ( g_errno == EFILECLOSED ) tt = LOG_INFO;
		log(tt,"net: Reading %s had error: %s.",
		    dbname,mstrerror(g_errno));
		THIS->m_errno = g_errno; 
		g_errno = 0; 
	}
	// return now if we're awaiting more scan completions
	if ( THIS->m_numScansCompleted < THIS->m_numScansStarted ) return;
	// . give control to doneScanning
	// . return if it blocks
	if ( ! THIS->doneScanning() ) return;
	// if one of our lists was *huge* and could not alloc mem, it was
	// due to corruption
	if ( THIS->m_hadCorruption ) g_errno = ECORRUPTDATA;
	// if it doesn't block call the callback, g_errno may be set
	THIS->m_callback ( THIS->m_state );
}

static void doneSleepingWrapper3 ( int fd , void *state ) ;

// . but now that we may get a list remotely to fix data corruption,
//   this may indeed block
bool Msg3::doneScanning ( ) {
	QUICKPOLL(m_niceness);
	// . did we have any error on any scan?
	// . if so, repeat ALL of the scans
	g_errno = m_errno;
	// 2 retry is the default
	int32_t max = 2;
	// see if explicitly provided by the caller
	if ( m_maxRetries >= 0 ) max = m_maxRetries;
	// now use -1 (no max) as the default no matter what
	max = -1;
	// ENOMEM is particulary contagious, so watch out with it...
	if ( g_errno == ENOMEM && m_maxRetries == -1 ) max = 0;
	// msg0 sets maxRetries to 2, don't let max stay set to -1
	if ( g_errno == ENOMEM && m_maxRetries != -1 ) max = m_maxRetries;
	// when thread cannot alloc enough read buf it keeps the read buf
	// set to NULL and BigFile.cpp sets g_errno to EBUFTOOSMALL
	if ( g_errno == EBUFTOOSMALL && m_maxRetries == -1 ) max = 0;
	// msg0 sets maxRetries to 2, don't let max stay set to -1
	if ( g_errno == EBUFTOOSMALL && m_maxRetries != -1 ) max = m_maxRetries;
	// . if no thread slots available, that hogs up serious memory.
	//   the size of Msg3 is 82k, so having just 5000 of them is 430MB.
	// . i just made Msg3 alloc mem when it needs more than about 2k
	//   so this problem is greatly reduced, therefore let's keep 
	//   retrying... forever if no thread slots in thread queue since
	//   we become the thread queue in a way.
	if ( g_errno == ENOTHREADSLOTS ) max = -1;
	// this is set above if the map has the same consecutive key repeated
	// and the read is enormous
	if ( g_errno == ECORRUPTDATA ) max = 0;
	// usually bad disk failures, don't retry those forever
	//if ( g_errno == EIO ) max = 3;
        // no, now our hitachis return these even when they're good so
	// we have to keep retrying forever
	if ( g_errno == EIO ) max = -1;
	// count these so we do not take drives offline just because
	// kernel ring buffer complains...
	if ( g_errno == EIO ) g_numIOErrors++;
	// bail early on high priority reads for these errors
	if ( g_errno == EDISKSTUCK && m_niceness == 0 ) max = 0;
	if ( g_errno == EIO        && m_niceness == 0 ) max = 0;

	// how does this happen? we should never bail out on a low priority
	// disk read... we just wait for it to complete...
	if ( g_errno == EDISKSTUCK && m_niceness != 0 ) { char *xx=NULL;*xx=0;}

	// on I/O, give up at call it corrupt after a while. some hitachis
	// have I/O errros on little spots, like gk88, maybe we can fix him
	if ( g_errno == EIO && m_retryNum >= 5 ) {
		m_errno = ECORRUPTDATA;
		m_hadCorruption = true;
		// do not do any retries any more
		max = 0;
	}

	// convert m_errno to ECORRUPTDATA if it is EBUFTOOSMALL and the
	// max of the bytesToRead are over 500MB.
	// if bytesToRead was ludicrous, then assume that the data file
	// was corrupted, the map was regenerated and it patched
	// over the corrupted bits which were 500MB or more in size.
	// we cannot practically allocate that much, so let's just
	// give back an empty buffer. treat it like corruption...
	// the way it patches is to store the same key over all the corrupted
	// pages, which can get pretty big. so if you read a range with that
	// key you will be hurting!!
	// this may be the same scenario as when the rdbmap has consecutive
	// same keys. see above where we set m_errno to ECORRUPTDATA...
	if ( g_errno == EBUFTOOSMALL ) { 
		int32_t biggest = 0;
		for ( int32_t i = 0 ; i < m_numFileNums ; i++ ) {
			if ( m_scans[i].m_bytesToRead < biggest ) continue;
			biggest = m_scans[i].m_bytesToRead;
		}
		if ( biggest > 500000000 ) {
			log("db: Max read size was %"INT32" > 500000000. Assuming "
			    "corrupt data in data file.",biggest);
			m_errno = ECORRUPTDATA;
			m_hadCorruption = true;
			// do not do any retries on this, the read was > 500MB
			max = 0;
		}
	}

	// if shutting down gb then limit to 20 so we can shutdown because
	// it can't shutdown until all threads are out of the queue i think
	if ( g_process.m_mode == EXIT_MODE && max < 0 ) {
		//log("msg3: forcing retries to 0 because shutting down");
		max = 0;
	}

	// get base, returns NULL and sets g_errno to ENOCOLLREC on error
	RdbBase *base; if (!(base=getRdbBase(m_rdbId,m_collnum))) return true;

	// this really slows things down because it blocks the cpu so
	// leave it out for now
#ifdef GBSANITYCHECK
	// check for corruption here, do not do it again in Msg5 if we pass
	if ( ! g_errno ) { // && g_conf.m_doErrorCorrection ) {
		int32_t i;
		for ( i = 0 ; i < m_numFileNums ; i++ )
			if ( ! m_lists[i].checkList_r ( false, false ) ) break;
		if ( i < m_numFileNums ) {
			g_errno = ECORRUPTDATA;
			m_errno = ECORRUPTDATA;
			max     = g_conf.m_corruptRetries; // try 100 times
			log("db: Encountered corrupt list in file %s.",
			    base->getFile(m_fileNums[i])->getFilename());
		}
		else
			m_listsChecked = true;
	}
#endif

	// try to fix this error i've seen
	if ( g_errno == EBADENGINEER && max == -1 )
		max = 100;

	// . if we had a ETRYAGAIN error, then try again now
	// . it usually means the whole file or a part of it was deleted 
	//   before we could finish reading it, so we should re-read all now
	// . RdbMerge deletes BigFiles after it merges them and also chops
	//   off file heads
	// . now that we have threads i'd imagine we'd get EBADFD or something
	// . i've also seen "illegal seek" as well
	if ( m_errno && (m_retryNum < max || max < 0) &&
	     // this will complete in due time, we can't call a sleep wrapper
	     // on it because the read is really still pending...
	     m_errno != EDISKSTUCK ) {
		// print the error
		static time_t s_time  = 0;
		time_t now = getTime();
		if ( now - s_time > 5 || g_errno != ENOTHREADSLOTS ) {
			log("net: Had error reading %s: %s. Retrying. "
			    "(retry #%"INT32")", 
			    base->m_dbname,mstrerror(m_errno) , m_retryNum );
			s_time = now;
		}
		// send email alert if in an infinite loop, but don't send
		// more than once every 2 hours
		static int32_t s_lastSendTime = 0;
		if ( m_retryNum == 100 && getTime() - s_lastSendTime > 3600*2){
			// remove this for now it is going off all the time
			//g_pingServer.sendEmail(NULL,//g_hostdb.getMyHost(),
			//		       "100 read retries",true);
			s_lastSendTime = getTime();
		}
		// clear g_errno cuz we should for call to readList()
		g_errno = 0;
		// free the list buffer since if we have 1000 Msg3s retrying
		// it will totally use all of our memory
		for ( int32_t i = 0 ; i < m_numChunks ; i++ ) 
			m_lists[i].destructor();
		// count retries
		m_retryNum++;
		// backoff scheme, wait 100ms more each time
		int32_t wait ;
		if ( m_retryNum == 1 ) wait = 10;
		else                   wait = 200 * m_retryNum;
		// . don't wait more than 10 secs between tries
		// . i've seen gf0 and gf16 get mega saturated
		if ( wait > 10000 ) wait = 10000;
		// wait 500 ms
		if ( g_loop.registerSleepCallback ( wait  , // ms
						    this  ,
						    doneSleepingWrapper3,
						    m_niceness))
			return false;
		// otherwise, registration failed
		log(
		    "net: Failed to register sleep callback for retry. "
		    "Abandoning read. This is bad.");
		// return, g_errno should be set
		g_errno = EBUFTOOSMALL;
		m_errno = EBUFTOOSMALL;
		return true;
	}

	// if we got an error and should not retry any more then give up
	if ( g_errno ) {
		log(
		    "net: Had error reading %s: %s. Giving up after %"INT32" "
		    "retries.",
		    base->m_dbname,mstrerror(g_errno) , m_retryNum );
		return true;
	}

	// note it if the retry finally worked
	if ( m_retryNum > 0 ) 
		log(LOG_INFO,"disk: Read succeeded after retrying %"INT32" times.",
		    (int32_t)m_retryNum);

	// count total bytes for logging
	int32_t count = 0;
	// . constrain all lists to make merging easier
	// . if we have only one list, then that's nice cuz the constrain
	//   will allow us to send it right away w/ zero copying
	// . if we have only 1 list, it won't be merged into a final list,
	//   that is, we'll just set m_list = &m_lists[i]
	for ( int32_t i = 0 ; i < m_numFileNums ; i++ ) {
		QUICKPOLL(m_niceness);
		// count total bytes for logging
		count += m_lists[i].getListSize();
		// . hint offset is relative to the offset of first key we read
		// . if that key was only 6 bytes RdbScan shift the list buf
		//   down 6 bytes to make the first key 12 bytes... a 
		//   requirement for all RdbLists
		// . don't inc it, though, if it was 0, pointing to the start
		//   of the list because our shift won't affect that
		if ( m_scans[i].m_shifted == 6 && m_hintOffsets[i] > 0 ) 
			m_hintOffsets[i] += 6;
		// posdb double compression
		if ( m_scans[i].m_shifted == 12 && m_hintOffsets[i] > 0 ) 
			m_hintOffsets[i] += 12;
		// . don't constrain on minRecSizes here because it may
		//   make our endKey smaller, which will cause problems
		//   when Msg5 merges these lists.
		// . If all lists have different endKeys RdbList's merge
		//   chooses the min and will merge in recs beyond that
		//   causing a bad list BECAUSE we don't check to make
		//   sure that recs we are adding are below the endKey
		// . if we only read from one file then constrain based 
		//   on minRecSizes so we can send the list back w/o merging
		//   OR if just merging with RdbTree's list
		int32_t mrs ;
		// . constrain to m_minRecSizesOrig, not m_minRecSizes cuz 
		//   that  could be adjusted by compensateForNegativeRecs()
		// . but, really, they should be the same if we only read from
		//   the root file
		if ( m_numFileNums == 1 ) mrs = m_minRecSizesOrig;
		else                      mrs = -1;
		// . this returns false and sets g_errno on error
		// . like if data is corrupt
		BigFile *ff = base->getFile(m_fileNums[i]);
		// if we did a merge really quick and delete one of the 
		// files we were reading, i've seen 'ff' be NULL
		char *filename = "lostfilename";
		if ( ff ) filename = ff->getFilename();

		// compute cache info
		RdbCache *rpc = getDiskPageCache ( m_rdbId );
		if ( ! m_allowPageCache ) rpc = NULL;
		int64_t vfd ;
		if ( ff ) vfd = ff->getVfd();
		key192_t ck ;
		if ( ff )
			ck = makeCacheKey ( vfd ,
					    m_scans[i].m_offset ,
					    m_scans[i].m_bytesToRead );
		if ( m_validateCache && ff && rpc && vfd != -1 ) {
			bool inCache;
			char *rec; int32_t recSize;
			inCache = rpc->getRecord ( (collnum_t)0 , // collnum
						   (char *)&ck , 
						   &rec , 
						   &recSize ,
						   true , // copy?
						   -1 , // maxAge, none 
						   true ); // inccounts?
			if ( inCache && 
			     // 1st byte is RdbScan::m_shifted
			     ( m_lists[i].m_listSize != recSize-1 ||
			       memcmp ( m_lists[i].m_list , rec+1,recSize-1) ||
			       *rec != m_scans[i].m_shifted ) ) {
				log("msg3: cache did not validate");
				char *xx=NULL;*xx=0;
			}
			mfree ( rec , recSize , "vca" );
		}


		///////
		//
		// STORE IN PAGE CACHE
		//
		///////
		// store what we read in the cache. don't bother storing
		// if it was a retry, just in case something strange happened.
		// store pre-constrain call is more efficient.
		if ( m_retryNum<=0 && ff && rpc && vfd != -1 &&
		     ! m_scans[i].m_inPageCache )
			rpc->addRecord ( (collnum_t)0 , // collnum
					 (char *)&ck , 
					 // rec1 is this little thingy
					 &m_scans[i].m_shifted,
					 1,
					 // rec2
					 m_lists[i].getList() ,
					 m_lists[i].getListSize() ,
					 0 ); // timestamp. 0 = now

		QUICKPOLL(m_niceness);

		// if from our 'page' cache, no need to constrain
		if ( ! m_lists[i].constrain ( m_startKey       ,
					      m_constrainKey   , // m_endKey
					      mrs           , // m_minRecSizes
					      m_hintOffsets[i] ,
					      //m_hintKeys   [i] ,
					      &m_hintKeys   [i*m_ks] ,
					      filename,//ff->getFilename() ,
					      m_niceness ) ) {
			log("net: Had error while constraining list read from "
			    "%s: %s/%s. vfd=%"INT32" parts=%"INT32". "
			    "This is likely caused by corrupted "
			    "data on disk.", 
			    mstrerror(g_errno), ff->getDir(),
			    ff->getFilename(), ff->m_vfd , 
			    (int32_t)ff->m_numParts );
			continue;
		}
	}

	// print the time
	if ( g_conf.m_logTimingDb ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t took = now - m_startTime;
		log(LOG_TIMING,
		    "net: Took %"INT64" ms to read %"INT32" lists of %"INT32" bytes total"
		     " from %s (niceness=%"INT32").",
		     took,m_numFileNums,count,base->m_dbname,m_niceness);
	}
	return true;
}

void doneSleepingWrapper3 ( int fd , void *state ) {
	Msg3 *THIS = (Msg3 *)state;
	// now try reading again
	if ( ! THIS->doneSleeping ( ) ) return;
	// if it doesn't block call the callback, g_errno may be set
	THIS->m_callback ( THIS->m_state );
}

bool Msg3::doneSleeping ( ) {
	// unregister
	g_loop.unregisterSleepCallback(this,doneSleepingWrapper3);
	// read again
	if ( ! readList ( m_rdbId            ,
			  m_collnum          ,
			  m_startKey         ,
			  m_endKeyOrig       ,
			  m_minRecSizesOrig  ,
			  m_startFileNum     ,
			  m_numFiles         ,
			  m_state            ,
			  m_callback         ,
			  m_niceness         ,
			  m_retryNum         ,
			  m_maxRetries       ,
			  m_compensateForMerge ,
			  -1,//m_syncPoint          ,
			  false                ,
			  m_allowPageCache     ,
			  m_hitDisk            ) ) return false;
	return true;
}

// . returns a new, smaller endKey
// . shrinks endKey while still preserving the minRecSizes requirement
// . this is the most confusing subroutine in the project
// . this now OVERWRITES endKey with the new one
//key_t Msg3::setPageRanges ( RdbBase *base ,
void  Msg3::setPageRanges ( RdbBase *base ,
			    int32_t  *fileNums      ,
			    int32_t   numFileNums   ,
			    //key_t  startKey      , 
			    //key_t  endKey        ,
			    char  *startKey      , 
			    char  *endKey        ,
			    int32_t   minRecSizes   ) {
	// sanity check
	//if ( m_ks != 12 && m_ks != 16 ) { char *xx=NULL;*xx=0; }
	// get the file maps from the rdb
	RdbMap **maps = base->getMaps();
	// . initialize the startpg/endpg for each file
	// . we read from the first offset on m_startpg to offset on m_endpg
	// . since we set them equal that means an empty range for each file
	for ( int32_t i = 0 ; i < numFileNums ; i++ ) {
		int32_t fn = fileNums[i];
		if ( fn < 0 ) { char *xx = NULL; *xx = 0; }
		m_startpg[i] = maps[fn]->getPage( startKey );
		m_endpg  [i] = m_startpg[i];
	}
	// just return if minRecSizes 0 (no reading needed)
	//if ( minRecSizes <= 0 ) return endKey ;
	if ( minRecSizes <= 0 ) return;
	// calculate minKey minus one
	//key_t lastMinKey ;
	char lastMinKey[MAX_KEY_BYTES];
	char lastMinKeyIsValid = 0;
	// loop until we find the page ranges that barely satisfy "minRecSizes"
  loop:
	// find the map whose next page has the lowest key
	int32_t  minpg   = -1;
	//key_t minKey; 
	char minKey[MAX_KEY_BYTES];
	for ( int32_t i = 0 ; i < numFileNums ; i++ ) {
		int32_t fn = fileNums[i];
		// this guy is out of race if his end key > "endKey" already
		//if ( maps[fn]->getKey ( m_endpg[i] ) > endKey ) continue;
		if(KEYCMP(maps[fn]->getKeyPtr(m_endpg[i]),endKey,m_ks)>0)
			continue;
		// get the next page after m_endpg[i]
		int32_t nextpg = m_endpg[i] + 1;
		// if endpg[i]+1 == m_numPages then we maxed out this range
		if ( nextpg > maps[fn]->getNumPages() ) continue;
		// . but this may have an offset of -1
		// . which means the page has no key starting on it and
		//   it's occupied by a rec which starts on a previous page
		while ( nextpg < maps[fn]->getNumPages() &&
			maps[fn]->getOffset ( nextpg ) == -1 ) nextpg++;
		// . continue if his next page doesn't have the minimum key
		// . if nextpg == getNumPages() then it returns the LAST KEY
		//   contained in the corresponding RdbFile
		//if ( minpg != -1 && maps[fn]->getKey ( nextpg ) > minKey ) 
		if (minpg != -1 && 
		    KEYCMP(maps[fn]->getKeyPtr(nextpg),minKey,m_ks)>0)continue;
		// . we got a winner, his next page has the current min key
		// . if m_endpg[i]+1 == getNumPages() then getKey() returns the
		//   last key in the mapped file
		// . minKey should never equal the key on m_endpg[i] UNLESS 
		//   it's on page #m_numPages
		//minKey = maps[fn]->getKey ( nextpg );
		KEYSET(minKey,maps[fn]->getKeyPtr(nextpg),m_ks);
		minpg  = i;
		// if minKey is same as the current key on this endpg, inc it
		// so we cause some advancement, otherwise, we'll loop forever
		//if ( minKey != maps[fn]->getKey ( m_endpg[i] ) ) continue;
		if ( KEYCMP(minKey,maps[fn]->getKeyPtr(m_endpg[i]),m_ks)!=0) 
			continue;
		//minKey += (uint32_t) 1;
		KEYADD(minKey,1,m_ks);
	}
	// . we're done if we hit the end of all maps in the race
	// . return the max end key
	// key_t maxEndKey; maxEndKey.setMax(); return maxEndKey; }
	// . no, just the endKey
	//if ( minpg  == -1 ) return endKey;
	if ( minpg  == -1 ) return;
	// sanity check
	if ( lastMinKeyIsValid && KEYCMP(minKey,lastMinKey,m_ks)<=0 ) {
		g_errno = ECORRUPTDATA;
		log("db: Got corrupted map in memory for %s. This is almost "
		    "always because of bad memory. Please replace your RAM.",
		    base->m_dbname);
		// do not wait for any merge to complete... otherwise
		// Rdb.cpp will not close until the merge is done
		g_merge.m_isMerging  = false;
		g_merge2.m_isMerging = false;
		// to complete
		// shutdown with urgent=true so threads are disabled.
		//mainShutdown(true);
		g_process.shutdown(true);
		//g_numCorrupt++;
		// sleep for now until we make sure this works
		//sleep(2000);
		return;
	}
	// don't let minKey exceed endKey, however
	//if ( minKey > endKey ) {
	if ( KEYCMP(minKey,endKey,m_ks)>0 ) {
		//minKey      = endKey ;
		//minKey     += (uint32_t) 1;
		//lastMinKey  = endKey;
		KEYSET(minKey,endKey,m_ks);
		KEYADD(minKey,1,m_ks);
		KEYSET(lastMinKey,endKey,m_ks);
	}
	else {
		//lastMinKey = minKey ;
		//lastMinKey -= (uint32_t) 1;
		KEYSET(lastMinKey,minKey,m_ks);
		KEYSUB(lastMinKey,1,m_ks);
	}
	// it is now valid
	lastMinKeyIsValid = 1;
	// . advance m_endpg[i] so that next page < minKey 
	// . we want to read UP TO the first key on m_endpg[i]
	for ( int32_t i = 0 ; i < m_numFileNums ; i++ ) {
		int32_t fn = fileNums[i];
		m_endpg[i] = maps[fn]->getEndPage ( m_endpg[i], lastMinKey );
	}
	// . if the minKey is BIGGER than the provided endKey we're done
	// . we don't necessarily include records whose key is "minKey"
	//if ( minKey > endKey ) return endKey;
	if ( KEYCMP(minKey,endKey,m_ks)>0) return;
	// . calculate recSizes per page within [startKey,minKey-1]
	// . compute bytes of records in [startKey,minKey-1] for each map
	// . this includes negative records so we may have annihilations
	//   when merging into "diskList" and get less than what we wanted
	//   but endKey should be int16_tened, so our caller will know to call
	//   again if he wants more
	int32_t recSizes = 0;
	for ( int32_t i = 0 ; i < m_numFileNums ; i++ ) {
		int32_t fn = fileNums[i];
		recSizes += maps[fn]->getMinRecSizes ( m_startpg[i] , 
						       m_endpg  [i] ,
						       startKey     , 
						       lastMinKey   ,
						       false        );
	}
	// if we hit it then return minKey -1 so we only read UP TO "minKey"
	// not including "minKey"
	//if ( recSizes >= minRecSizes ) 
	if ( recSizes >= minRecSizes ) {
		// . sanity check
		// . this sanity check fails sometimes, but leave it
		//   out for now... causes the Illegal endkey msgs in
		//   RdbList::indexMerge_r()
		//if ( KEYNEG(lastMinKey) ) { char *xx=NULL;*xx=0; }
		KEYSET(endKey,lastMinKey,m_ks);
		//return lastMinKey;
		return;
	}
	// keep on truckin'
	goto loop;
}

// . we now boost m_minRecSizes to account for negative recs in certain files
// . TODO: use floats for averages, not ints
void Msg3::compensateForNegativeRecs ( RdbBase *base ) {
	// get the file maps from the rdb
	RdbMap **maps = base->getMaps();
	// add up counts from each map
	int64_t totalNegatives = 0;
	int64_t totalPositives = 0;
	int64_t totalFileSize  = 0;
	for (int32_t i = 0 ; i < m_numFileNums ; i++) {
		int32_t fn = m_fileNums[i];
		// . this cored on me before when fn was -1, how'd that happen?
		// . it happened right after startup before a merge should
		//   have been attempted
		if ( fn < 0 ) {
			log(LOG_LOGIC,"net: msg3: fn=%"INT32". bad engineer.",fn);
			continue;
		}
		totalNegatives += maps[fn]->getNumNegativeRecs();
		totalPositives += maps[fn]->getNumPositiveRecs();
		totalFileSize  += maps[fn]->getFileSize();
	}
	// add em all up
	int64_t totalNumRecs = totalNegatives + totalPositives;
	// if we have no records on disk, why are we reading from disk?
	if ( totalNumRecs == 0 ) return ;
	// what is the size of a negative record?
	//int32_t negRecSize  = sizeof(key_t);
	int32_t negRecSize  = m_ks;
	if ( base->getFixedDataSize() == -1 ) negRecSize += 4;
	// what is the size of all positive recs combined?
	int64_t posFileSize = totalFileSize - negRecSize * totalNegatives;
	// . we often overestimate the size of the negative recs for indexdb
	//   because it uses half keys...
	// . this can make posFileSize go negative and ultimately result in
	//   a newMin of 0x7fffffff which really fucks us up
	if ( posFileSize < 0 ) posFileSize = 0;
	// what is the average size of a positive record?
	int32_t posRecSize  = 0;
	if ( totalPositives > 0 ) posRecSize = posFileSize / totalPositives;
	// we annihilate the negative recs and their positive pairs
	int64_t loss   = totalNegatives * (negRecSize + posRecSize);
	// what is the percentage lost?
	int64_t lostPercent = (100LL * loss) / totalFileSize;
	// how much more should we read to compensate?
	int32_t newMin = ((int64_t)m_minRecSizes * (lostPercent + 100LL))/100LL;
	// newMin will never be smaller unless it overflows
	if ( newMin < m_minRecSizes ) newMin = 0x7fffffff;
	// print msg if we changed m_minRecSizes
	//if ( newMin != m_minRecSizes )
	//	log("Msg3::compensated from minRecSizes from %"INT32" to %"INT32"",
	//	    m_minRecSizes, newMin );
	// set the new min
	m_minRecSizes = newMin;
}
