#include "gb-include.h"

#include "Rdb.h"
//#include "Msg35.h"
//#include "Tfndb.h"
//#include "Checksumdb.h"
#include "Clusterdb.h"
#include "Hostdb.h"
#include "Tagdb.h"
#include "Catdb.h"
#include "Posdb.h"
#include "Cachedb.h"
#include "Monitordb.h"
#include "Datedb.h"
#include "Titledb.h"
#include "Sections.h"
#include "Spider.h"
#include "Statsdb.h"
#include "Linkdb.h"
#include "Syncdb.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"
#include "Repair.h"
#include "Rebalance.h"
//#include "Msg3.h" // debug include

// how many rdbs are in "urgent merge" mode?
int32_t g_numUrgentMerges = 0;

int32_t g_numThreads = 0;

char g_dumpMode = 0;

//static void doneDumpingWrapper  ( void *state );
//static void attemptMergeWrapper ( int fd , void *state ) ;

// since we only do one merge at a time, keep this class static
class RdbMerge g_merge;

// this one is used exclusively by tfndb so he can merge when titledb is
// merging since titledb adds a lot of tfndb records
class RdbMerge g_merge2;

RdbBase::RdbBase ( ) {
	m_numFiles  = 0;
	m_rdb = NULL;
	m_nextMergeForced = false;
	m_dbname[0] = '\0';
	m_dbnameLen = 0;
	// use bogus collnum just in case
	m_collnum = -1;
	//m_dummy = NULL;
	reset();
}

void RdbBase::reset ( ) {

	char *db = "";
	if ( m_rdb  ) db = m_dbname;
	//log("debug: base resetting db=%s collnum=%"INT32"",db,(int32_t)m_collnum);

	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		mdelete ( m_files[i] , sizeof(BigFile),"RdbBFile");
		delete (m_files[i]);
		mdelete ( m_maps[i] , sizeof(RdbMap),"RdbBMap");
		delete (m_maps[i]);
	}
	m_numFiles  = 0;
	m_files [ m_numFiles ] = NULL;
	//m_isClosing = false;
	//m_isClosed  = false;
	//m_isSaving  = false;
	// reset tree and cache
	//m_tree.reset();
	//m_cache.reset();
	// free the dummy record used for delete recs (negative recs)
	//if ( m_dummy ) mfree ( m_dummy , m_fixedDataSize ,"Rdb");
	//m_dummy     = NULL;
	// reset stats
	//m_numSeeks   = 0 ;
	//m_numRead    = 0 ;
	// reset net stats
	//m_numReqsGet    = 0 ;
	//m_numNetReadGet = 0 ;
	//m_numRepliesGet = 0 ; 
	//m_numNetSentGet = 0 ;
	//m_numReqsAdd    = 0 ;
	//m_numNetReadAdd = 0 ;
	//m_numRepliesAdd = 0 ; 
	//m_numNetSentAdd = 0 ;
	// we no longer need to be saved
	//m_needsSave = false;
	//m_inWaiting = false;
	// we're not in urgent merge mode yet
	m_mergeUrgent = false;
	//m_waitingForTokenForDump  = false;
	m_waitingForTokenForMerge = false;
	m_isMerging = false;
	m_hasMergeFile = false;
	m_isUnlinking  = false;
	m_numThreads = 0;
	m_checkedForMerge = false;
}

RdbBase::~RdbBase ( ) {
	//close ( NULL , NULL );
	reset();
}

bool RdbBase::init ( char  *dir            ,
		     char  *dbname         ,
		     bool   dedup          ,
		     int32_t   fixedDataSize  ,
		     //uint32_t groupMask      ,
		     //uint32_t groupId        ,
		     int32_t   minToMergeArg     ,
		     //int32_t   maxTreeMem     ,
		     //int32_t   maxTreeNodes   ,
		     //bool   isTreeBalanced ,
		     //int32_t   maxCacheMem    ,
		     //int32_t   maxCacheNodes  ,
		     bool   useHalfKeys    ,
		     char           keySize ,
		     int32_t           pageSize,
		     char          *coll    ,
		     collnum_t      collnum ,
		     RdbTree       *tree    ,
		     RdbBuckets          *buckets ,
		     RdbDump       *dump    ,
		     class Rdb     *rdb    ,
		     void *pc , // DiskPageCache *pc      ,
		     bool           isTitledb            ,
		     bool           preloadDiskPageCache ,
		     bool           biasDiskPageCache    ) {


	m_didRepair = false;
 top:
	// reset all
	reset();
	// sanity
	if ( ! dir ) { char *xx=NULL;*xx=0; }
	// set all our contained classes
	//m_dir.set ( dir );
	// set all our contained classes
	// . "tmp" is bogus
	// . /home/mwells/github/coll.john-test1113.654coll.john-test1113.655
	char tmp[1024];
	sprintf ( tmp , "%scoll.%s.%"INT32"" , dir , coll , (int32_t)collnum );

	// logDebugAdmin
	log(LOG_DEBUG,"db: "
	    "adding new base for dir=%s coll=%s collnum=%"INT32" db=%s",
	    dir,coll,(int32_t)collnum,dbname);

	// catdb is collection independent

	// make a special subdir to store the map and data files in if
	// the db is not associated with a collection. /statsdb /accessdb
	// /facebookdb etc.
	if ( rdb->m_isCollectionLess ) {
		if ( collnum != (collnum_t) 0 ) {
			log("db: collnum not zero for catdb.");
			char *xx = NULL; *xx = 0;
		}
		// make a special "cat" dir for it if we need to
		sprintf ( tmp , "%s%s" , dir , dbname );
		int32_t status = ::mkdir ( tmp , getDirCreationFlags() );
			       // S_IRUSR | S_IWUSR | S_IXUSR | 
			       // S_IRGRP | S_IWGRP | S_IXGRP | 
			       // 		S_IROTH | S_IXOTH );
	        if ( status == -1 && errno != EEXIST && errno )
			return log("db: Failed to make directory %s: %s.",
				   tmp,mstrerror(errno));
	}

	/*
	if ( strcmp ( dbname , "catdb" ) == 0 ) {
	// if ( strcmp ( dbname , "catdb1" ) == 0 ||
	//      strcmp ( dbname , "catdb2" ) == 0 ) {
		// sanity check, ensure we're zero
		if ( collnum != (collnum_t) 0 ) {
			log("db: collnum not zero for catdb.");
			char *xx = NULL; *xx = 0;
		}
		// make a special "cat" dir for it if we need to
		sprintf ( tmp , "%scat" , dir );
		if ( ::mkdir ( tmp ,
			       // S_IRUSR | S_IWUSR | S_IXUSR | 
			       // S_IRGRP | S_IWGRP | S_IXGRP | 
			       // S_IROTH | S_IXOTH ) == -1 && errno != EEXIST )
			return log("db: Failed to make directory %s: %s.",
				   tmp,mstrerror(errno));
	}
	// statsdb is collection independent
	else if ( strcmp ( dbname , "statsdb" ) == 0 ) {
		// sanity check, statsdb should always be zero
		if ( collnum != (collnum_t) 0 ) {
			log ( "db: collnum not zero for statsdb." );
			char *xx = NULL; *xx = 0;
		}
		// make a special "stats" dir for it if necessary
		sprintf ( tmp , "%sstats" , dir );
		if ( ::mkdir ( tmp ,
			       // S_IRUSR | S_IWUSR | S_IXUSR | 
			       // S_IRGRP | S_IWGRP | S_IXGRP | 
			       // S_IROTH | S_IXOTH ) == -1 && errno != EEXIST )
			return log( "db: Failed to make directory %s: %s.",
				    tmp, mstrerror( errno ) );
	}
	// statsdb is collection independent
	else if ( strcmp ( dbname , "accessdb" ) == 0 ) {
		// sanity check, accessdb should always be zero
		if ( collnum != (collnum_t) 0 ) {
			log ( "db: collnum not zero for accessdb." );
			char *xx = NULL; *xx = 0;
		}
		// make a special "stats" dir for it if necessary
		sprintf ( tmp , "%saccess" , dir );
		if ( ::mkdir ( tmp ,
			       // S_IRUSR | S_IWUSR | S_IXUSR | 
			       // S_IRGRP | S_IWGRP | S_IXGRP | 
			       // S_IROTH | S_IXOTH ) == -1 && errno != EEXIST )
			return log( "db: Failed to make directory %s: %s.",
				    tmp, mstrerror( errno ) );
	}
	// syncdb is collection independent
	else if ( strcmp ( dbname , "syncdb" ) == 0 ) {
		// sanity check, statsdb should always be zero
		if ( collnum != (collnum_t) 0 ) {
			log ( "db: collnum not zero for syncdb." );
			char *xx = NULL; *xx = 0;
		}
		// make a special "stats" dir for it if necessary
		sprintf ( tmp , "%ssyncdb" , dir );
		if ( ::mkdir ( tmp ,
			       // S_IRUSR | S_IWUSR | S_IXUSR | 
			       // S_IRGRP | S_IWGRP | S_IXGRP | 
			       // S_IROTH | S_IXOTH ) == -1 && errno != EEXIST )
			return log( "db: Failed to make directory %s: %s.",
				    tmp, mstrerror( errno ) );
	}
	*/

	//m_dir.set ( dir , coll );
	m_dir.set ( tmp );
	m_coll    = coll;
	m_collnum = collnum;
	m_tree    = tree;
	m_buckets = buckets;
	m_dump    = dump;
	m_rdb     = rdb;

	// save the dbname NULL terminated into m_dbname/m_dbnameLen
	m_dbnameLen = gbstrlen ( dbname );
	gbmemcpy ( m_dbname , dbname , m_dbnameLen );
	m_dbname [ m_dbnameLen ] = '\0';
	// set up the dummy file
	//char filename[256];
	//sprintf(filename,"%s-saved.dat",m_dbname);
	//m_dummyFile.set ( getDir() , filename );
	// store the other parameters
	m_dedup            = dedup;
	m_fixedDataSize    = fixedDataSize;
	//m_maxTreeMem       = maxTreeMem;
	m_useHalfKeys      = useHalfKeys;
	m_ks               = keySize;
	m_pageSize         = pageSize;
	//m_pc               = pc;
	m_isTitledb        = isTitledb;
	// wa haven't done a dump yet
	//m_lastWrite        = gettimeofdayInMilliseconds();
	//m_groupMask        = groupMask;
	//m_groupId          = groupId;
	// . set up our cache
	// . we could be adding lists so keep fixedDataSize -1 for cache
	//if ( ! m_cache.init ( maxCacheMem   , 
	//		      fixedDataSize , 
	//		      true          , // support lists
	//		      maxCacheNodes ,
	//		      m_useHalfKeys ,
	//		      m_dbname      ,
	//		      loadCacheFromDisk  ) )
	//	return false;
	// we can't merge more than MAX_RDB_FILES files at a time
	if ( minToMergeArg > MAX_RDB_FILES ) minToMergeArg = MAX_RDB_FILES;
	m_minToMergeArg = minToMergeArg;
	// . set our m_files array
	// . m_dir is bogus causing this to fail
	if ( ! setFiles () ) {
		// try again if we did a repair
		if ( m_didRepair ) goto top;
		// if no repair, give up
		return false;
	}
	//int32_t dataMem;
	// if we're in read only mode, don't bother with *ANY* trees
	//if ( g_conf.m_readOnlyMode ) goto preload;
	// . if maxTreeNodes is -1, means auto compute it
	// . set tree to use our fixed data size
	// . returns false and sets g_errno on error
	//if ( ! m_tree.set ( fixedDataSize  , 
	//		    maxTreeNodes   , // max # nodes in tree
	//		    isTreeBalanced , 
	//		    maxTreeMem     ,
	//		    false          , // own data?
	//		    false          , // dataInPtrs?
	//		    m_dbname       ) )
	//	return false;
	// now get how much mem the tree is using (not including stored recs)
	//dataMem = maxTreeMem - m_tree.getTreeOverhead();
	//if ( fixedDataSize != 0 && ! m_mem.init ( &m_dump , dataMem ) ) 
	//	return log("db: Failed to initialize memory: %s.", 
	//		   mstrerror(g_errno));
	// . scan data in memory every minute to prevent swap-out
	// . the wait here is in ms, MIN_WAIT is in seconds
	// . call this every now and then to quickly scan our memory to
	//   prevent swap out
	/*
	static bool s_firstTime = true;
	if ( !g_loop.registerSleepCallback(MIN_WAIT*1000,this,scanMemWrapper)){
		log("RdbMem::init: register sleep callback for scan failed. "
		     "Warning. Mem may be swapped out.");
		g_errno = 0;
	}
	else if ( s_firstTime ) {
		s_firstTime = false;
		log("RdbMem::init: scanning mem every %"INT32" secs with "
		    "PAGE_SIZE = %"INT32"" , MIN_WAIT , PAGE_SIZE );
	}	
	*/
	// create a dummy data string for deleting nodes
	//if ( m_fixedDataSize > 0 ) {
	//	m_dummy = (char *) mmalloc ( m_fixedDataSize ,"RdbDummy" );
	//	m_dummySize = sizeof(key_t);
	//	if ( ! m_dummy ) return false;
	//}
	//m_delRecSize = sizeof(key_t);
	//if ( m_fixedDataSize == -1 ) m_delRecSize += m_dummySize + 4;
	//if ( m_fixedDataSize  >  0 ) m_delRecSize += m_dummySize;
	// load any saved tree
	//if ( ! loadTree ( ) ) return false;

	// now diskpagecache is much simpler, just basically rdbcache...
	return true;

	/*

	// . init BigFile::m_fileSize and m_lastModifiedTime
	// . m_lastModifiedTime is now used by the merge to select older
	//   titledb files to merge
	int64_t bigFileSize[MAX_RDB_FILES];
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		BigFile *f = m_files[i];
		bigFileSize[i] = f->getFileSize();
		f->getLastModifiedTime();
	}

	// if minimizingDiskSeeks, we need to know what files are going to be
	// stored in the cache
	if ( m_pc && m_numFiles > 0 && m_pc->m_minimizeDiskSeeks ){
		int32_t maxMem = m_pc->getMemMax();
		while ( maxMem > 0 ){
			int32_t minIndex = 0;
			int64_t minFileSize = -1;
			for ( int32_t i = 0; i < m_numFiles; i++ ){
				if ( bigFileSize[i] < 0 ) continue;
				if ( minFileSize < 0 || 
				     minFileSize > bigFileSize[i] ){
					minIndex = i;
					minFileSize = bigFileSize[i];
					// don't use it again
					bigFileSize[i] = -1;
				}
			}
			if ( minFileSize == -1 ) break;
			// mark in that bigFile that it can use diskcache
			BigFile *f = m_files[minIndex];
			f->m_vfdAllowed = true;
			maxMem -= minFileSize;
		}
	}

	// sanity check
	if ( m_pc && m_pc->m_diskPageSize!=m_pageSize) { char *xx=NULL;*xx=0; }
	// now fill up the page cache
	// preload:
	if ( ! preloadDiskPageCache ) return true;
	if ( ! m_pc ) return true;
	char buf [ 512000 ];
	int32_t total = m_pc->getMemMax();
	log(LOG_DEBUG,"db: %s: Preloading page cache. Total mem to use =%"UINT32"",
	     m_dbname,total);
	//log("max=%"INT32"",total);
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		if ( total <= 0 ) break;
		BigFile *f = m_files[i];
		f->setBlocking();
		int64_t fsize  = f->getFileSize();
		int64_t off    = 0;
		// for biasing, only read part of the file
		if ( biasDiskPageCache ) {
			int32_t numTwins = g_hostdb.getNumHostsPerShard();
			int32_t thisTwin = g_hostdb.m_hostId/g_hostdb.m_numShards;
			off   = (fsize/numTwins) * thisTwin;
			if ( thisTwin < numTwins-1 )
				fsize = (fsize/numTwins) * (thisTwin+1);
		}
		// read the file
		for ( ; off < fsize ; off += 256000 ) {
			if ( total <= 0 ) break;
			int64_t toread = fsize - off;
			if ( toread > 256000 ) toread = 256000;
			f->read ( buf , toread , off );
			total -= toread;
		}
		f->setNonBlocking();
	}
	// reset m_hits/m_misses
	//m_pc->resetStats();

	// now m_minToMerge might have changed so try to do a merge
	//attemptMerge ( );
	// debug test
	//BigFile f;
	//f.set ( "/gigablast3" , "tmp" );
	//f.open ( O_RDWR | O_CREAT );
	//char buf[128*1024*5+10];
	//int32_t n = f.write ( buf , 128*1024*5+10 , 0 );
	//fprintf(stderr,"n=%"INT32"\n",n);
	return true;
	*/
}

// . move all files into trash subdir
// . change name
// . this is part of PageRepair's repair algorithm. all this stuff blocks.
bool RdbBase::moveToTrash ( char *dstDir ) {
	// get current time as part of filename
	//uint32_t t = (uint32_t)getTime();
	// loop over all files
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// . rename the map file
		// . get the "base" filename, does not include directory
		BigFile *f ;
		//char dstDir      [1024];
		//sprintf ( dstDir      , "%s" , subdir );
		char dstFilename [1024];
		f = m_maps[i]->getFile();
		sprintf ( dstFilename , "%s" , f->getFilename());
		// ALWAYS log what we are doing
		logf(LOG_INFO,"repair: Renaming %s to %s%s",
		     f->getFilename(),dstDir,dstFilename);
		if ( ! f->rename ( dstFilename , dstDir ) )
			return log("repair: Moving file had error: %s.",
				   mstrerror(errno));
		// move the data file
		f = m_files[i];
		sprintf ( dstFilename , "%s" , f->getFilename());
		// ALWAYS log what we are doing
		logf(LOG_INFO,"repair: Renaming %s to %s%s",
		     f->getFilename(),dstDir,dstFilename);
		if ( ! f->rename ( dstFilename, dstDir  ) )
			return log("repair: Moving file had error: %s.",
				   mstrerror(errno));
	}
	// now just reset the files so we are empty, we should have our
	// setFiles() called again once the newly rebuilt rdb files are
	// renamed, when RdbBase::rename() is called below
	reset();
	// success
	return true;
}

// . newly rebuilt rdb gets renamed to the original, after we call 
//   RdbBase::trash() on the original.
// . this is part of PageRepair's repair algorithm. all this stuff blocks.
bool RdbBase::removeRebuildFromFilenames ( ) {
	// loop over all files
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// . rename the map file
		// . get the "base" filename, does not include directory
		BigFile *f = m_files[i];
		// return false if it fails
		//if ( ! removeRebuildFromFilename(f) ) return false;
		// DON'T STOP IF ONE FAILS
		removeRebuildFromFilename(f);
		// rename the map file now too!
		f = m_maps[i]->getFile();
		// return false if it fails
		//if ( ! removeRebuildFromFilename(f) ) return false;
		// DON'T STOP IF ONE FAILS
		removeRebuildFromFilename(f);
	}
	// reset all now
	reset();
	// now PageRepair should reload the original
	return true;
}

bool RdbBase::removeRebuildFromFilename ( BigFile *f ) {
	// get the filename
	char *ff = f->getFilename();
	// copy it
	char buf[1024];
	strcpy ( buf , ff );
	// remove "Rebuild" from it
	char *p = strstr ( buf , "Rebuild" );
	if ( ! p ) return log("repair: Rebuild not found in filename=%s",
			      buf);
	// bury it
	int32_t  rlen = gbstrlen("Rebuild");
	char *end  = buf + gbstrlen(buf);
	int32_t  size = end - (p + rlen);
	// +1 to include the ending \0
	memmove ( p , p + rlen , size + 1 );
	// now rename this file
	logf(LOG_INFO,"repair: Renaming %s to %s",
	     f->getFilename(),buf);
	if ( ! f->rename ( buf ) )
		return log("repair: Rename to %s failed",buf);
	return true;
}

// . this is called to open the initial rdb data and map files we have
// . first file is always the merge file (may be empty)
// . returns false on error
bool RdbBase::setFiles ( ) {
	// set our directory class
	if ( ! m_dir.open ( ) )
		// we are getting this from a bogus m_dir
		return log("db: Had error opening directory %s", getDir());
	// note it
	log(LOG_DEBUG,"db: Loading files for %s coll=%s (%"INT32").",
	     m_dbname,m_coll,(int32_t)m_collnum );
	// . set our m_files array
	// . addFile() will return -1 and set g_errno on error
	// . the lower the fileId the older the data 
	//   (but not necessarily the file)
	// . we now put a '*' at end of "*.dat*" since we may be reading in
	//   some headless BigFiles left over froma killed merge
	char *filename;
	// did we rename any files for titledb?
	bool converting = false;
	// getNextFilename() writes into this
	char pattern[8]; strcpy ( pattern , "*.dat*" );
	while ( ( filename = m_dir.getNextFilename ( pattern ) ) ) {
		// filename must be a certain length
		int32_t filenameLen = gbstrlen(filename);
		// we need at least "indexdb0000.dat"
		if ( filenameLen < m_dbnameLen + 8 ) continue;
		// ensure filename starts w/ our m_dbname
		if ( strncmp ( filename , m_dbname , m_dbnameLen ) != 0 )
			continue;
		// then a 4 digit number should follow
		char *s = filename + m_dbnameLen;
		if ( ! isdigit(*(s+0)) ) continue;
		if ( ! isdigit(*(s+1)) ) continue;
		if ( ! isdigit(*(s+2)) ) continue;
		if ( ! isdigit(*(s+3)) ) continue;
		// optional 5th digit
		int32_t len = 4;
		if (   isdigit(*(s+4)) ) len = 5;
		// . read that id
		// . older files have lower numbers
		int32_t id = atol2 ( s , len );
		// skip it
		s += len;
		// if we are titledb, we got the secondary id
		int32_t id2 = -1;
		// . if we are titledb we should have a -xxx after
		// . if none is there it needs to be converted!
		if ( m_isTitledb && *s != '-' ) {
			// critical
			log("gb: bad title filename of %s. Halting.",filename);
			g_errno = EBADENGINEER;
			return false;
			// flag it
			converting = true;
			// for now, just use the primary id as the secondary id
			id2 = id;
		}
		else if ( m_isTitledb ) { id2 = atol2 ( s + 1 , 3 ); s += 4; }
		// don't add if already in there
		int32_t i ;
		for ( i = 0 ; i < m_numFiles ; i++ ) 
			if ( m_fileIds[i] >= id ) break;
		if ( i < m_numFiles && m_fileIds[i] == id ) continue;
		// assume no mergNum
		int32_t mergeNum = -1;
		// if file id is even, we need the # of files being merged
		// otherwise, if it is odd, we do not
		if ( (id & 0x01) == 0x01 ) goto addIt;
		if ( *s != '.' ) continue; 
		s++;
		if ( ! isdigit(*(s+0)) ) continue;
		if ( ! isdigit(*(s+1)) ) continue;
		if ( ! isdigit(*(s+2)) ) continue;
		mergeNum = atol2 ( s , 3 );
 addIt:

		// sometimes an unlink() does not complete properly and we
		// end up with remnant files that are 0 bytes. so let's skip
		// those guys.
		File ff;
		ff.set ( m_dir.getDir() , filename );
		// does this file exist?
		int32_t exists = ff.doesExist() ;
		// core if does not exist (sanity check)
		if ( exists == 0 ) 
			return log("db: File %s does not exist.",filename);
		// bail on error calling ff.doesExist()
		if ( exists == -1 ) return false;
		// skip if 0 bytes or had error calling ff.getFileSize()
		if ( ff.getFileSize() == 0 ) {
			// actually, we have to move to trash because
			// if we leave it there and we start writing
			// to that file id, exit, then restart, it
			// causes problems...
			char src[1024];
			char dst[1024];
			sprintf ( src , "%s/%s",m_dir.getDir(),filename);
			sprintf ( dst , "%s/trash/%s",
				  g_hostdb.m_dir,filename);
			log("db: Moving file %s/%s of 0 bytes into trash "
			    "subdir. rename %s to %s", m_dir.getDir(),filename,
			    src,dst);
			if ( ::rename ( src , dst ) ) 
				return log("db: Moving file had error: %s.",
					   mstrerror(errno));
			continue;
		}

		// . put this file into our array of files/maps for this db
		// . MUST be in order of fileId for merging purposes
		// . we assume older files come first so newer can override
		//   in RdbList::merge() routine
		if (addFile(id,false/*newFile?*/,mergeNum,id2,converting) < 0) 
			return false;
	}

	// everyone should start with file 0001.dat or 0000.dat
	if ( m_numFiles > 0 && m_fileIds[0] > 1 && m_rdb->m_rdbId == RDB_SPIDERDB ) {
		log("db: missing file id 0001.dat for %s in coll %s. "
		    "Fix this or it'll core later. Just rename the next file "
		    "in line to 0001.dat/map. We probably cored at a "
		    "really bad time during the end of a merge process.",
		    m_dbname, m_coll );

		// do not re-do repair! hmmm
		if ( m_didRepair ) return false;

		// just fix it for them
		BigFile bf;
		SafeBuf oldName;
		oldName.safePrintf("%s%04"INT32".dat",m_dbname,m_fileIds[0]);
		bf.set ( m_dir.getDir() , oldName.getBufStart() );

		// rename it to like "spiderdb.0001.dat"
		SafeBuf newName;
		newName.safePrintf("%s/%s0001.dat",m_dir.getDir(),m_dbname);
		bf.rename ( newName.getBufStart() );

		// and delete the old map
		SafeBuf oldMap;
		oldMap.safePrintf("%s/%s0001.map",m_dir.getDir(),m_dbname);
		File omf;
		omf.set ( oldMap.getBufStart() );
		omf.unlink();

		// get the map file name we want to move to 0001.map
		BigFile cmf;
		SafeBuf curMap;
		curMap.safePrintf("%s%04"INT32".map",m_dbname,m_fileIds[0]);
		cmf.set ( m_dir.getDir(), curMap.getBufStart());

		// rename to spiderdb0081.map to spiderdb0001.map
		cmf.rename ( oldMap.getBufStart() );

		// replace that first file then
		m_didRepair = true;
		return true;
		//char *xx=NULL; *xx=0;
	}


	m_dir.close();

	// ensure files are sharded correctly
	verifyFileSharding();

	if ( ! converting ) return true;

	// now if we are converting old titledb names to new...
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// . get the actual secondary to correspond with tfndb
		// . generate secondary id from primary id
		m_fileIds2[i] = (m_fileIds[i] - 1)/ 2;
		// . titledbRebuild? use m_dbname
		// . rename it
		char buf1[1024];
		sprintf ( buf1, "%s%04"INT32".dat", m_dbname,m_fileIds[i] );
		BigFile f;
		f.set ( g_hostdb.m_dir , buf1 );
		char buf2[1024];
		sprintf ( buf2 , "%s%04"INT32"-%03"INT32".dat",
			  m_dbname, m_fileIds[i] , m_fileIds2[i] );
		// log it
		log(LOG_INFO,"db: Renaming %s to %s",buf1,buf2);
		if ( ! f.rename ( buf2 ) )
			return log("db: Rename had error: %s.",
				   mstrerror(g_errno));
	}
	// otherwise, success
	return log("db: Rename succeeded.");
}

// return the fileNum we added it to in the array
// reutrn -1 and set g_errno on error
int32_t RdbBase::addFile ( int32_t id , bool isNew , int32_t mergeNum , 
			   int32_t id2 , bool converting ) {

	int32_t n = m_numFiles;
	// can't exceed this
	if ( n >= MAX_RDB_FILES ) { 
		g_errno = ETOOMANYFILES; 
		log(LOG_LOGIC,
		    "db: Can not have more than %"INT32" files. File add "
		    "failed.",(int32_t)MAX_RDB_FILES);
		return -1;
	}

	// HACK: skip to avoid a OOM lockup. if RdbBase cannot dump
	// its data to disk it can backlog everyone and memory will
	// never get freed up.
	int64_t mm = g_conf.m_maxMem;
	g_conf.m_maxMem = 0x0fffffffffffffffLL;
	BigFile *f ;
	try { f = new (BigFile); }
	catch ( ...  ) { 
		g_conf.m_maxMem = mm;
		g_errno = ENOMEM;
		log("RdbBase: new(%i): %s", 
		    (int)sizeof(BigFile),mstrerror(g_errno));
		return -1; 
	}
	mnew ( f , sizeof(BigFile) , "RdbBFile" );

	// set the data file's filename
	char name[512];
	if      ( mergeNum <= 0 && m_isTitledb )
		snprintf(name,511,"%s%04"INT32"-%03"INT32".dat",
			 m_dbname,id,id2 );
	else if ( mergeNum <= 0 )
		snprintf ( name ,511,"%s%04"INT32".dat"      , m_dbname, id );
	else if ( m_isTitledb )
		snprintf ( name ,511,"%s%04"INT32"-%03"INT32".%03"INT32".dat",
			  m_dbname, id , id2, mergeNum );
	else
		snprintf(name,511,"%s%04"INT32".%03"INT32".dat",
			m_dbname,id,mergeNum);

	f->set ( getDir() , name , NULL ); // getStripeDir() );

	// if new insure does not exist
	if ( isNew && f->doesExist() ) {
		log("rdb: creating NEW file %s/%s which already exists!",
		    f->getDir(),
		    f->getFilename());
		mdelete ( f , sizeof(BigFile),"RdbBFile");
		delete (f); 
		return -1;
		char *xx=NULL;*xx=0;
	}


	RdbMap  *m ;
	try { m = new (RdbMap); }
	catch ( ... ) { 
		g_conf.m_maxMem = mm;
		g_errno = ENOMEM;
		log("RdbBase: new(%i): %s", 
		    (int)sizeof(RdbMap),mstrerror(g_errno));
		mdelete ( f , sizeof(BigFile),"RdbBFile");
		delete (f); 
		return -1; 
	}
	mnew ( m , sizeof(RdbMap) , "RdbBMap" );
	// reinstate the memory limit
	g_conf.m_maxMem = mm;
	// sanity check
	if ( id2 < 0 && m_isTitledb ) { char *xx = NULL; *xx = 0; }

	CollectionRec *cr = NULL;

	// if we're converting, just add to m_filesIds and m_fileIds2
	if ( converting ) {
	       log("*-*-*-* Converting titledb files to new file name format");
	       goto skip;
	}


	// debug help
	if ( isNew )
		log("rdb: adding new file %s/%s",// m_numFiles=%"INT32"",
		    f->getDir(),f->getFilename());//,m_numFiles);

	// rename bug fix?
	/*
	if ( ! isNew && id != 1 && m_isTitledb && m_rdb->m_rdbId == RDB_TITLEDB ) {
		char newf[10245];
		sprintf(newf,"%s%04"INT32"-%03"INT32".dat",
			"titledbRebuild",id,id2);
		log("cmd: f->rename(%s%04"INT32"-%03"INT32".dat,%s)",m_dbname,id,id2,newf);
		g_conf.m_useThreads = 0;
		f->rename ( newf );
		g_conf.m_useThreads = 1;
		exit(-1);
	}
	*/

	// if not a new file sanity check it
	for ( int32_t j = 0 ; ! isNew && j < f->m_maxParts - 1 ; j++ ) {
		// might be headless
		File *ff = f->getFile2(j);//m_files[j];
		if ( ! ff ) continue;
		if ( ff->getFileSize() == MAX_PART_SIZE ) continue;
		log ( "db: File %s/%s has length %"INT64", but it should be %"INT64". "
		      "You should move it to a temporary directory "
		      "and restart. It probably happened when the power went "
		      "out and a file delete operation failed to complete.",
		      f->getDir(),
		      ff->getFilename() ,
		      (int64_t)ff->getFileSize(),
		      (int64_t)MAX_PART_SIZE);
		exit(0);
		return -1;
	}

	// set the map file's  filename
	sprintf ( name , "%s%04"INT32".map", m_dbname, id );
	m->set ( getDir() , name , m_fixedDataSize , m_useHalfKeys , m_ks ,
		 m_pageSize );
	if ( ! isNew && ! m->readMap ( f ) ) { 
		// if out of memory, do not try to regen for that
		if ( g_errno == ENOMEM ) return -1;
		g_errno = 0;
		log("db: Could not read map file %s",name);
		// if 'gb dump X collname' was called, bail, we do not
		// want to write any data
		if ( g_dumpMode ) return -1; 
		log("db: Attempting to generate map file for data "
		    "file %s* of %"INT64" bytes. May take a while.",
		    f->getFilename(), f->getFileSize() );
		// debug hack for now
		//exit(-1);
		// this returns false and sets g_errno on error
		if ( ! m->generateMap ( f ) ) {
			log("db: Map generation failed.");
			log("db: Moving .dat and .map file to trash dir");
			SafeBuf tmp;
			tmp.safePrintf("%s",f->getFilename());
			// take off .dat and make it * so we can move map file
			int32_t len = tmp.getLength();
			char *str = tmp.getBufStart();
			str[len-3] = '*';
			str[len-2] = '\0';
			SafeBuf cmd;
			cmd.safePrintf("mv %s/%s %s/trash/",
				       m_dir.getDir(),
				       str,
				       g_hostdb.m_dir);
			log("db: %s",cmd.getBufStart() );
			gbsystem ( cmd.getBufStart() );
			exit(0);
			mdelete ( f , sizeof(BigFile),"RdbBase");
			delete (f); 
			mdelete ( m , sizeof(RdbMap),"RdbBase");
			delete (m); 
			return -1; 
		}
		log("db: Map generation succeeded.");
		// . save it
		// . if we're an even #'d file a merge will follow
		//   when main.cpp calls attemptMerge()
		log("db: Saving generated map file to disk.");
		// turn off statsdb so it does not try to add records for 
		// these writes because it is not initialized yet and will
		// cause this write to fail!
		g_statsdb.m_disabled = true;
		// true = alldone
		bool status = m->writeMap( true );
		g_statsdb.m_disabled = false;
		if ( ! status ) return log("db: Save failed.");
	}

	if ( ! isNew ) log(LOG_DEBUG,"db: Added %s for collnum=%"INT32" pages=%"INT32"",
			    name ,(int32_t)m_collnum,m->getNumPages());

	// open this big data file for reading only
	if ( ! isNew ) {
		if ( mergeNum < 0 ) 
			f->open ( O_RDONLY | O_NONBLOCK | O_ASYNC , NULL );
		// otherwise, merge will have to be resumed so this file
		// should be writable
		else
			f->open ( O_RDWR | O_NONBLOCK | O_ASYNC , NULL );//pc
	}
 skip:
	// find the position to add so we maintain order by fileId
	int32_t i ;
	for ( i = 0 ; i < m_numFiles ; i++ ) if ( m_fileIds[i] >= id ) break;
	// cannot collide here
	if ( i < m_numFiles && m_fileIds[i] == id ) { 
		log(LOG_LOGIC,"db: addFile: fileId collided."); return -1; }
	// shift everyone up if we need to fit this file in the middle somewher
	if ( i < m_numFiles ) {
		int nn = m_numFiles-i;
		memmove ( &m_files  [i+1] , &m_files[i],nn*sizeof(BigFile *));
		memmove ( &m_fileIds[i+1] , &m_fileIds[i],nn*sizeof(int32_t));
		memmove ( &m_fileIds2[i+1], &m_fileIds2[i],nn*sizeof(int32_t));
		memmove ( &m_maps   [i+1] , &m_maps   [i],nn*sizeof(RdbMap *));
	}

	// insert this file into position #i
	m_fileIds  [i] = id;
	m_fileIds2 [i] = id2;
	m_files    [i] = f;
	m_maps     [i] = m;

	// debug point
	//log("map #0 is %s ptr=%llx (nf=%i)",
	//    m_maps[0]->getFilename(),(long long)m_maps[0],m_numFiles);

	// to free up mem for diffbot's many collections...
	cr = g_collectiondb.getRec ( m_collnum );
	if ( ! isNew && cr && cr->m_isCustomCrawl )
		m->reduceMemFootPrint();

	// are we resuming a killed merge?
	if ( g_conf.m_readOnlyMode && ((id & 0x01)==0) ) {
		log("db: Cannot start in read only mode with an incomplete "
		    "merge, because we might be a temporary cluster and "
		    "the merge might be active.");
		exit(-1);
	}

	// inc # of files we have
	m_numFiles++;
	// debug note
	//log("rdb: numFiles=%"INT32" for collnum=%"INT32" db=%s",
	//    m_numFiles,(int32_t)m_collnum,m_dbname);
	// keep it NULL terminated
	m_files [ m_numFiles ] = NULL;
	// if we added a merge file, mark it
	if ( mergeNum >= 0 ) {
		m_hasMergeFile      = true;
		m_mergeStartFileNum = i + 1 ; //merge was starting w/ this file
	}
	return i;
}

/*
// this is obsolete now
// returns -1 and sets g_errno if none available
int32_t RdbBase::getAvailId2 ( ) {
	// . otherwise we're titledb, find an available second id
	// . store id2s here as we find them
	char f[MAX_RDB_FILES]; 
	memset ( f , 0 , MAX_RDB_FILES );
	int32_t i;
	for ( i = 0 ; i < m_numFiles ; i++ ) {
		// sanity check
		if ( m_fileIds2[i] < 0 || m_fileIds2[i] >= MAX_RDB_FILES ) {
			char *xx = NULL; *xx = 0; }
		// flag it as used
		f [ m_fileIds2[i] ] = 1;
	}
	// find the first available one
	for ( i = 0 ; i < MAX_RDB_FILES ; i++ ) if ( f[i] == 0 ) break;
	// . error if none! return -1 on error
	// . 255 is reserved in tfndb for urls just in spiderdb or in 
	//   titledb tree
	if ( i >= MAX_RDB_FILES || i >= 255 ) {
		log(LOG_LOGIC,"db: No more secondary ids available for "
		    "new titledb file. All in use. This should never happen. "
		    "Are your hard drives extremely slow?");
		return -1;
	}
	return i;
}
*/

int32_t RdbBase::addNewFile ( int32_t id2 ) {

	int32_t fileId = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ )
		if ( m_fileIds[i] >= fileId ) fileId = m_fileIds[i] + 1;
	// . if not odd number then add one
	// . we like to keep even #'s for merge file names
	if ( (fileId & 0x01) == 0 ) fileId++;
	// otherwise, set it
	return addFile(fileId,true/*a new file?*/,-1 /*mergeNum*/,id2 );
}

static void doneWrapper ( void *state ) ;

// . called after the merge has successfully completed
// . the final merge file is always file #0 (i.e. "indexdb0000.dat/map")
bool RdbBase::incorporateMerge ( ) {
	// some int16_thand variable notation
	int32_t a = m_mergeStartFileNum;
	int32_t b = m_mergeStartFileNum + m_numFilesToMerge;
	// shouldn't be called if no files merged
	if ( a == b ) {
		// unless resuming after a merge completed and we exited
		// but forgot to finish renaming the final file!!!!
		log("merge: renaming final file");
		// decrement this count
		if ( m_isMerging ) m_rdb->m_numMergesOut--;
		// exit merge mode
		m_isMerging = false;
		// return the merge token, no need for a callback
		//g_msg35.releaseToken ( );
		//return true; 
	}
	// file #x is the merge file
	int32_t x = a - 1; 
	// . we can't just unlink the merge file on error anymore
	// . it may have some data that was deleted from the original file
	if ( g_errno ) {
		log("db: Merge failed for %s, Exiting.", m_dbname);
		// print mem table
		//g_mem.printMem();
		// we don't have a recovery system in place, so save state
	        // and dump core
		char *p = NULL;
		*p = 0;
		//m_isMerging = false;
		// return the merge token, no need for a callback
		//g_msg35.releaseToken ( );
		return true;
	}
	// note
	log(LOG_INFO,"db: Writing map %s.",m_maps[x]->getFilename());
	// . ensure we can save the map before deleting other files
	// . sets g_errno and return false on error
	// . allDone = true
	m_maps[x]->writeMap( true );

	// tfndb has his own merge class since titledb merges write tfndb recs
	RdbMerge *m = &g_merge;
	//if ( m_rdb == g_tfndb.getRdb() ) m = &g_merge2;

	// print out info of newly merged file
	int64_t tp = m_maps[x]->getNumPositiveRecs();
	int64_t tn = m_maps[x]->getNumNegativeRecs();
	log(LOG_INFO,
	    "merge: Merge succeeded. %s (#%"INT32") has %"INT64" positive "
	     "and %"INT64" negative recs.", m_files[x]->getFilename(), x, tp, tn);
	if ( m_rdb == g_posdb.getRdb() ) // || m_rdb == g_tfndb.getRdb() )
		log(LOG_INFO,"merge: Removed %"INT64" dup keys.",
		     m->getDupsRemoved() );
	// . bitch if bad news
	// . sanity checks to make sure we didn't mess up our data from merging
	// . these cause a seg fault on problem, seg fault should save and
	//   dump core. sometimes we'll have a chance to re-merge the 
	//   candidates to see what caused the problem.
	// . only do these checks for indexdb, titledb can have key overwrites
	//   and we can lose positives due to overwrites
	// . i just re-added some partially indexed urls so indexdb will
	//   have dup overwrites now too!!
	if ( tp > m_numPos ) {
		log(LOG_INFO,"merge: %s gained %"INT64" positives.",
		     m_dbname , tp - m_numPos );
			//char *xx = NULL; *xx = 0;
	}
	if ( tp < m_numPos - m_numNeg ) {
		log(LOG_INFO,"merge: %s: lost %"INT64" positives",
		     m_dbname , m_numPos - tp );
		//char *xx = NULL; *xx = 0;
	}
	if ( tn > m_numNeg ) {
		log(LOG_INFO,"merge: %s: gained %"INT64" negatives.",
		     m_dbname , tn - m_numNeg );
		//char *xx = NULL; *xx = 0;
	}
	if ( tn < m_numNeg - m_numPos ) {
		log(LOG_INFO,"merge: %s: lost %"INT64" negatives.",
		     m_dbname , m_numNeg - tn );
		//char *xx = NULL; *xx = 0;
	}

	// assume no unlinks blocked
	m_numThreads = 0;

	// . before unlinking the files, ensure merged file is the right size!!
	// . this will save us some anguish
	m_files[x]->m_fileSize = -1;
	int64_t fs = m_files[x]->getFileSize();
	// get file size from map
	int64_t fs2 = m_maps[x]->getFileSize();
	// compare, if only a key off allow that. that is an artificat of
	// generating a map for a file screwed up from a power outage. it
	// will end on a non-key boundary.
	if ( fs != fs2 ) {
		log("build: Map file size does not agree with actual file "
		    "size for %s. Map says it should be %"INT64" bytes but it "
		    "is %"INT64" bytes.", 
		    m_files[x]->getFilename(), fs2 , fs );
		if ( fs2-fs > 12 || fs-fs2 > 12 ) { char *xx = NULL; *xx = 0; }
		// now print the exception
		log("build: continuing since difference is less than 12 "
		    "bytes. Most likely a discrepancy caused by a power "
		    "outage and the generated map file is off a bit.");
	}

	// on success unlink the files we merged and free them
	for ( int32_t i = a ; i < b ; i++ ) {
		// incase we are starting with just the
		// linkdb0001.003.dat file and not the stuff we merged
		if ( ! m_files[i] ) continue;
		// debug msg
		log(LOG_INFO,"merge: Unlinking merged file %s/%s (#%"INT32").",
		    m_files[i]->getDir(),m_files[i]->getFilename(),i);
		// . append it to "sync" state we have in memory
		// . when host #0 sends a OP_SYNCTIME signal we dump to disk
		//g_sync.addOp ( OP_UNLINK , m_files[i] , 0 );
		// . these links will be done in a thread
		// . they will save the filename before spawning so we can
		//   delete the m_files[i] now
		if ( ! m_files[i]->unlink ( doneWrapper , this ) ) {
			m_numThreads++; g_numThreads++; }
		// debug msg
		// MDW this cores if file is bad... if collection
		// got delete from under us i guess!!
		else log(LOG_INFO,"merge: Unlinked %s (#%"INT32").",
			 m_files[i]->getFilename(),i);
		// debug msg
		log(LOG_INFO,"merge: Unlinking map file %s (#%"INT32").",
		     m_maps[i]->getFilename(),i);
		if ( ! m_maps[i]->unlink  ( doneWrapper , this ) ) {
			m_numThreads++; g_numThreads++; }
			// debug msg
		else log(LOG_INFO,"merge: Unlinked %s (#%"INT32").",
			 m_maps[i]->getFilename(),i);
		//if ( i == a ) continue;
		//delete (m_files[i]);
		//delete (m_maps[i]);
	}

	// save for re-use
	m_x = x;
	m_a = a;
	// save the merge file name so we can unlink from sync table later
	strncpy ( m_oldname , m_files[x]->getFilename() , 254 );
	// . let sync table know we closed the old file we merged to
	// . no, keep it around until after the rename finishes
	//g_sync.addOp ( OP_CLOSE , m_files[x] , 0 );	

	// wait for the above unlinks to finish before we do this rename
	// otherwise, we might end up doing this rename first and deleting
	// it!
	
	// if we blocked on all, keep going
	if ( m_numThreads == 0 ) { doneWrapper2 ( ); return true; }
	// . otherwise we blocked
	// . we are now unlinking
	// . this is so Msg3.cpp can avoid reading the [a,b) files
	m_isUnlinking = true;

	return true;
}

void doneWrapper ( void *state ) {
	RdbBase *THIS = (RdbBase *)state;
	log("merge: done unlinking file. #threads=%"INT32"",THIS->m_numThreads);
	THIS->doneWrapper2 ( );
}

static void doneWrapper3 ( void *state ) ;

void RdbBase::doneWrapper2 ( ) {
	// bail if waiting for more to come back
	if ( m_numThreads > 0 ) {
		g_numThreads--;
		if ( --m_numThreads > 0 ) return;
	}

	// debug msg
	log (LOG_INFO,"merge: Done unlinking all files.");

	// could be negative if all did not block
	m_numThreads = 0;

	int32_t x = m_x;
	int32_t a = m_a;
	// . the fileId of the merge file becomes that of a
	// . but secondary id should remain the same
	m_fileIds [ x ] = m_fileIds [ a ];
	if ( ! m_maps [x]->rename(m_maps[a]->getFilename(),doneWrapper3,this)){
		m_numThreads++; g_numThreads++; }

	// sanity check
	m_files[x]->m_fileSize = -1;
	int64_t fs = m_files[x]->getFileSize();
	// get file size from map
	int64_t fs2 = m_maps[x]->getFileSize();
	// compare
	if ( fs != fs2 ) {
		log("build: Map file size does not agree with actual file "
		    "size");
		char *xx = NULL; *xx = 0;
	}

	if ( ! m_isTitledb ) {
		// we are kind opening this up now for writing
		//g_sync.addOp ( OP_OPEN  , m_files[a]->getFilename() , 0 );
		// debug statement
		log(LOG_INFO,"db: Renaming %s of size %"INT64" to %s",
		    m_files[x]->getFilename(),fs , m_files[a]->getFilename());
		// rename it, this may block
		if ( ! m_files[x]->rename ( m_files[a]->getFilename() ,
					    doneWrapper3 , this ) ) {
			m_numThreads++; g_numThreads++; }
	}
	else {
		// rename to this (titledb%04"INT32"-%03"INT32".dat)
		char buf [ 1024 ];
		// use m_dbname in case its titledbRebuild
		sprintf ( buf , "%s%04"INT32"-%03"INT32".dat" , 
			  m_dbname, m_fileIds[a], m_fileIds2[x] );
		// we are kind opening this up now for writing
		//g_sync.addOp ( OP_OPEN  , buf , 0 );
		// rename it, this may block
		if ( ! m_files   [ x ]->rename ( buf , doneWrapper3, this ) ) {
			m_numThreads++; g_numThreads++; }
	}
	// if we blocked on all, keep going
	if ( m_numThreads == 0 ) { doneWrapper4 ( ); return ; }
	// . otherwise we blocked
	// . we are now unlinking
	// . this is so Msg3.cpp can avoid reading the [a,b) files
	m_isUnlinking = true;
}


void doneWrapper3 ( void *state ) {
	RdbBase *THIS = (RdbBase *)state;
	log("rdb: thread completed rename operation for collnum=%"INT32" "
	    "#thisbaserenamethreads=%"INT32"",
	    (int32_t)THIS->m_collnum,THIS->m_numThreads-1);
	THIS->doneWrapper4 ( );
}

static void checkThreadsAgainWrapper ( int fb, void *state  ) {
	RdbBase *THIS = (RdbBase *)state;
	g_loop.unregisterSleepCallback ( state,checkThreadsAgainWrapper);
	THIS->doneWrapper4 ( );
}

void RdbBase::doneWrapper4 ( ) {
	// bail if waiting for more to come back
	if ( m_numThreads > 0 ) {
		g_numThreads--;
		if ( --m_numThreads > 0 ) return;
	}

	// some shorthand variable notation
	int32_t a = m_mergeStartFileNum;
	int32_t b = m_mergeStartFileNum + m_numFilesToMerge;

	//
	// wait for all threads accessing this bigfile to go bye-bye
	//
	log("db: checking for outstanding read threads on unlinked files");
	bool wait = false;
	for ( int32_t i = a ; i < b ; i++ ) {
		BigFile *bf = m_files[i];
		if ( g_threads.isHittingFile(bf) ) wait = true;
	}
	if ( wait ) {
		log("db: waiting for read thread to exit on unlinked file");
		if (!g_loop.registerSleepCallback(100,this,
						  checkThreadsAgainWrapper)){
			char *xx=NULL;*xx=0; }
		return;
	}


	// . we are no longer unlinking
	// . this is so Msg3.cpp can avoid reading the [a,b) files
	m_isUnlinking = false;
	// file #x is the merge file
	//int32_t x = a - 1; 
	// rid ourselves of these files
	buryFiles ( a , b );
	// sanity check
	if ( m_numFilesToMerge != (b-a) ) {
		log(LOG_LOGIC,"db: Bury oops."); char *xx = NULL; *xx = 0; }
	// we no longer have a merge file
	m_hasMergeFile = false;
	// now unset m_mergeUrgent if we're close to our limit
	if ( m_mergeUrgent && m_numFiles - 14 < m_minToMerge ) {
		m_mergeUrgent = false;
		if ( g_numUrgentMerges > 0 ) g_numUrgentMerges--;
		if ( g_numUrgentMerges == 0 )
			log(LOG_INFO,"merge: Exiting urgent "
			    "merge mode for %s.",m_dbname);
	}
	// decrement this count
	if ( m_isMerging ) m_rdb->m_numMergesOut--;
	// exit merge mode
	m_isMerging = false;
	// return the merge token, no need for a callback
	//g_msg35.releaseToken ( );
	// the rename has completed at this point, so tell sync table in mem
	//g_sync.addOp ( OP_CLOSE , m_files[x] , 0 );
	// unlink old merge filename from sync table
	//g_sync.addOp ( OP_UNLINK , m_oldname , 0 );
	// . now in case dump dumped many files while we were merging
	//   we should see if they need to be merged now
	// . Msg35 may bitch if our get request arrives before our release
	//   request! he may think we're a dup request but that should not
	//   happen since we don't allow getToken() to be called if we are
	//   merging or have already made a request for it.
	//attemptMerge ( 1/*niceness*/ , false /*don't force it*/ ) ;
	// try all in case they were waiting (and not using tokens)
	//g_tfndb.getRdb()->attemptMerge      ( 1 , false );
	/*
	g_clusterdb.getRdb()->attemptMerge  ( 1 , false );
	g_linkdb.getRdb()->attemptMerge     ( 1 , false );
	//g_sectiondb.getRdb()->attemptMerge  ( 1 , false );
	g_tagdb.getRdb()->attemptMerge      ( 1 , false );
	//g_checksumdb.getRdb()->attemptMerge ( 1 , false );
	g_titledb.getRdb()->attemptMerge    ( 1 , false );
	g_doledb.getRdb()->attemptMerge     ( 1 , false );
	g_catdb.getRdb()->attemptMerge      ( 1 , false );
	//g_clusterdb.getRdb()->attemptMerge  ( 1 , false );
	g_statsdb.getRdb()->attemptMerge    ( 1 , false );
	g_syncdb.getRdb()->attemptMerge     ( 1 , false );
	g_cachedb.getRdb()->attemptMerge     ( 1 , false );
	g_serpdb.getRdb()->attemptMerge     ( 1 , false );
	g_monitordb.getRdb()->attemptMerge     ( 1 , false );
	//g_linkdb.getRdb()->attemptMerge     ( 1 , false );
	//g_indexdb.getRdb()->attemptMerge    ( 1 , false );
	g_posdb.getRdb()->attemptMerge    ( 1 , false );
	//g_datedb.getRdb()->attemptMerge     ( 1 , false );
	g_spiderdb.getRdb()->attemptMerge   ( 1 , false );
	*/

	// try to merge more when we are done
	attemptMergeAll2 ( );
}

void RdbBase::buryFiles ( int32_t a , int32_t b ) {
	// on succes unlink the files we merged and free them
	for ( int32_t i = a ; i < b ; i++ ) {
		mdelete ( m_files[i] , sizeof(BigFile),"RdbBase");
		delete (m_files[i]);
		mdelete ( m_maps[i] , sizeof(RdbMap),"RdbBase");
		delete (m_maps [i]);
	}
	// bury the merged files
	int32_t n = m_numFiles - b;
	gbmemcpy (&m_files   [a], &m_files   [b], n*sizeof(BigFile *));
	gbmemcpy (&m_maps    [a], &m_maps    [b], n*sizeof(RdbMap  *));
	gbmemcpy (&m_fileIds [a], &m_fileIds [b], n*sizeof(int32_t     ));
	gbmemcpy (&m_fileIds2[a], &m_fileIds2[b], n*sizeof(int32_t     ));
	// decrement the file count appropriately
	m_numFiles -= (b-a);
	// sanity
	log("rdb: bury files: numFiles now %"INT32" (b=%"INT32" a=%"INT32" collnum=%"INT32")",
	    m_numFiles,b,a,(int32_t)m_collnum);
	// ensure last file is NULL (so BigFile knows the end of m_files)
	m_files [ m_numFiles ] = NULL;
}

/*
void attemptMergeWrapper ( int fd , void *state ) {
	Rdb *THIS = (Rdb *) state;
	// keep sleeping if someone else still merging
	if ( g_merge.isMerging() ) return;
	// if no one else merging, stop the sleep-retry cycle
	//g_loop.unregisterSleepCallback ( state , attemptMergeWrapper );
	// now it's our turn
	THIS->attemptMerge ( 1, THIS->m_nextMergeForced );// 1=niceness
}
*/

//static void gotTokenForMergeWrapper ( void *state ) ;

// . the DailyMerge.cpp will set minToMergeOverride for titledb, and this
//   overrides "forceMergeAll" which is the same as setting 
//   "minToMergeOverride" to "2". (i.e. perform a merge if you got 2 or more 
//   files)
// . now return true if we started a merge, false otherwise
// . TODO: fix Rdb::attemptMergeAll() to not remove from linked list if
//   we had an error in addNewFile() or rdbmerge.cpp's call to rdbbase::addFile
bool RdbBase::attemptMerge ( int32_t niceness, bool forceMergeAll, bool doLog ,
			     int32_t minToMergeOverride ) {

	// don't do merge if we're in read only mode
	if ( g_conf.m_readOnlyMode ) return false;
	// or if we are copying our files to a new host
	//if ( g_hostdb.m_syncHost == g_hostdb.m_myHost ) return;
	// nor if EITHER of the merge classes are suspended
	if ( g_merge.m_isSuspended  ) return false;
	if ( g_merge2.m_isSuspended ) return false;

	// shutting down? do not start another merge then
	if ( g_process.m_mode == EXIT_MODE ) return false;

	// sanity checks
	if (   g_loop.m_inQuickPoll ) { 
		log("rdb: cant attempt merge in quickpoll");
		return false;
	}

	if (   niceness == 0 ) { char *xx=NULL;*xx=0; }

	if ( forceMergeAll ) m_nextMergeForced = true;

	if ( m_nextMergeForced ) forceMergeAll = true;

	if ( forceMergeAll )
		log(LOG_INFO,"merge: forcing merge for "
		    "for %s. (collnum=%"INT32")",m_dbname,(int32_t)m_collnum);

	// if we are trying to merge titledb but a titledb dump is going on
	// then do not do the merge, we do not want to overwrite tfndb via
	// RdbDump::updateTfndbLoop() 
	char rdbId = getIdFromRdb ( m_rdb );
	if ( rdbId == RDB_TITLEDB && g_titledb.m_rdb.m_dump.isDumping() ) {
		if ( doLog ) 
			log(LOG_INFO,"db: Can not merge titledb while it "
			    "is dumping.");
		return false;
	}

	// or if in repair mode, do not mess with any files in any coll
	// unless they are secondary rdbs... allow tagdb to merge, too.
	// secondary rdbs are like g_indexdb2, and should be merge. usually
	// when adding to g_indexdb, it is in a different collection for a 
	// full rebuild and we are not actively searching it so it doesn't
	// need to be merged.
	/*
	if ( g_repair.isRepairActive() && 
	     //! g_conf.m_fullRebuild &&
	     //! g_conf.m_removeBadPages &&
	     ! isSecondaryRdb ( (uint8_t)rdbId ) && 
	     rdbId != RDB_TAGDB )
		return;
	*/

	// if a dump is happening it will always be the last file, do not
	// include it in the merge
	int32_t numFiles = m_numFiles;
	if ( numFiles > 0 && m_dump->isDumping() ) numFiles--;
	// need at least 1 file to merge, stupid (& don't forget: u r stooopid)
	// crap, we need to recover those tagdb0000.002.dat files, so
	// comment this out so we can do a resume on it
	//if ( numFiles <= 1 ) return;
	// . wait for all unlinking and renaming activity to flush out
	// . otherwise, a rename or unlink might still be waiting to happen
	//   and it will mess up our merge
	// . right after a merge we get a few of these printed out...
	if ( m_numThreads > 0 ) {
		if ( doLog )
			log(LOG_INFO,"merge: Waiting for unlink/rename "
			    "operations to finish before attempting merge "
			    "for %s. (collnum=%"INT32")",m_dbname,(int32_t)m_collnum);
		return false;
	}

	if ( g_numThreads > 0 ) {
		// prevent log spam
		static int32_t s_lastTime = 0;
		int32_t now = getTimeLocal();
		if ( now - s_lastTime > 0 && doLog )
			log(LOG_INFO,"merge: Waiting for another "
			    "collection's unlink/rename "
			    "operations to finish before attempting merge "
			    "for %s (collnum=%"INT32").",
			    m_dbname,(int32_t)m_collnum);
		s_lastTime = now;
		return false;
	}


	// set m_minToMerge from coll rec if we're indexdb
	CollectionRec *cr = g_collectiondb.m_recs [ m_collnum ];
	// now see if collection rec is there to override us
	//if ( ! cr ) {
	if ( ! cr && ! m_rdb->m_isCollectionLess ) {
		g_errno = 0;
		log("merge: Could not find coll rec for %s.",m_coll);
	}
	// set the max files to merge in a single merge operation
	m_absMaxFiles = -1;
	m_absMaxFiles = 50;
	/*
	if ( cr && m_rdb == g_indexdb.getRdb() ) {
		m_absMaxFiles = cr->m_indexdbMinTotalFilesToMerge;
		// watch out for bad values
		if ( m_absMaxFiles < 2 ) m_absMaxFiles = 2;
	}
	if ( cr && m_rdb == g_datedb.getRdb() ) {
		m_absMaxFiles = cr->m_indexdbMinTotalFilesToMerge;
		// watch out for bad values
		if ( m_absMaxFiles < 2 ) m_absMaxFiles = 2;
	}
	*/
	// m_minToMerge is -1 if we should let cr override but if m_minToMerge
	// is actually valid at this point, use it as is, therefore, just set
	// cr to NULL
	m_minToMerge = m_minToMergeArg;
	if ( cr && m_minToMerge > 0 ) cr = NULL;
	// if cr is non-NULL use its value now
	if ( cr && m_rdb == g_posdb.getRdb() ) 
		m_minToMerge = cr->m_posdbMinFilesToMerge;
	if ( cr && m_rdb == g_titledb.getRdb() ) 
		m_minToMerge = cr->m_titledbMinFilesToMerge;
	//if ( cr && m_rdb == g_spiderdb.getRdb() ) 
	//	m_minToMerge = cr->m_spiderdbMinFilesToMerge;
	//if ( cr && m_rdb == g_sectiondb.getRdb() ) 
	//	m_minToMerge = cr->m_sectiondbMinFilesToMerge;
	//if ( cr && m_rdb == g_sectiondb.getRdb() ) 
	//	m_minToMerge = cr->m_sectiondbMinFilesToMerge;
	//if ( cr && m_rdb == g_checksumdb.getRdb() ) 
	//	m_minToMerge = cr->m_checksumdbMinFilesToMerge;
	//if ( cr && m_rdb == g_clusterdb.getRdb() ) 
	//	m_minToMerge = cr->m_clusterdbMinFilesToMerge;
	//if ( cr && m_rdb == g_datedb.getRdb() ) 
	//	m_minToMerge = cr->m_datedbMinFilesToMerge;
	//if ( cr && m_rdb == g_statsdb.getRdb() )
	//if ( m_rdb == g_statsdb.getRdb() )
	//	m_minToMerge = g_conf.m_statsdbMinFilesToMerge;
	if ( m_rdb == g_syncdb.getRdb() )
		m_minToMerge = g_syncdb.m_rdb.m_minToMerge;
	if ( cr && m_rdb == g_linkdb.getRdb() )
		m_minToMerge = cr->m_linkdbMinFilesToMerge;
	if ( cr && m_rdb == g_cachedb.getRdb() )
		m_minToMerge = 4;
	if ( cr && m_rdb == g_serpdb.getRdb() )
		m_minToMerge = 4;
	if ( cr && m_rdb == g_monitordb.getRdb() )
		m_minToMerge = 4;
	if ( cr && m_rdb == g_tagdb.getRdb() )
		m_minToMerge = cr->m_tagdbMinFilesToMerge;

	// if we are reblancing this coll then keep merges tight so all
	// the negative recs annihilate with the positive recs to free
	// up disk space since we could be int16_t on disk space.
	//if ( g_rebalance.m_isScanning &&
	//     // if might have moved on if not able to merge because
	//     // another was merging... so do this anyway...
	//     g_rebalance.m_collnum == m_collnum )
	//	m_minToMerge = 2;
	

	// secondary rdbs are used for rebuilding, so keep their limits high
	//if ( m_rdb == g_indexdb2.getRdb    () ) m_minToMerge = 50;
	// TODO: make this 200!!!
	//if ( m_rdb == g_posdb2.getRdb    () ) m_minToMerge = 10;
	//if ( m_rdb == g_spiderdb2.getRdb   () )	m_minToMerge = 20;
	//if ( m_rdb == g_sectiondb2.getRdb  () )	m_minToMerge = 200;
	//if ( m_rdb == g_checksumdb2.getRdb () )	m_minToMerge = 20;
	//if ( m_rdb == g_clusterdb2.getRdb  () )	m_minToMerge = 20;
	//if ( m_rdb == g_datedb2.getRdb     () )	m_minToMerge = 20;
	//if ( m_rdb == g_tfndb2.getRdb      () )	m_minToMerge = 2;
	//if ( m_rdb == g_tagdb2.getRdb     () )	m_minToMerge = 20;
	//if ( m_rdb == g_statsdb2.getRdb    () ) m_minToMerge = 20;

	// always obey the override
	if ( minToMergeOverride >= 2 ) {
		log("merge: Overriding min files to merge of %"INT32" with %"INT32"",
		    m_minToMerge,minToMergeOverride );
		m_minToMerge = minToMergeOverride;
	}

	// if still -1 that is a problem
	if ( m_minToMerge <= 0 ) {
		log("Got bad minToMerge of %"INT32" for %s. Set its default to "
		    "something besides -1 in Parms.cpp or add it to "
		    "CollectionRec.h.",
		    m_minToMerge,m_dbname);
		//m_minToMerge = 2;
		char *xx = NULL; *xx = 0;
	}
	// mdw: comment this out to reduce log spam when we have 800 colls!
	// print it
	//if ( doLog ) 
	//	log(LOG_INFO,"merge: Attempting to merge %"INT32" %s files on disk."
	//	    " %"INT32" files needed to trigger a merge.",
	//	    numFiles,m_dbname,m_minToMerge);
	// . even though another merge may be going on, we can speed it up
	//   by entering urgent merge mode. this will prefer the merge disk
	//   ops over dump disk ops... essentially starving the dumps and
	//   spider reads
	// . if we are significantly over our m_minToMerge limit
	//   then set m_mergeUrgent to true so merge disk operations will
	//   starve any spider disk reads (see Threads.cpp for that)
	// . TODO: fix this: if already merging we'll have an extra file
	// . i changed the 4 to a 14 so spider is not slowed down so much
	//   when getting link info... but real time queries will suffer!
	if ( ! m_mergeUrgent && numFiles - 14 >= m_minToMerge ) {
		m_mergeUrgent = true;
		if ( doLog ) 
		log(LOG_INFO,"merge: Entering urgent merge mode for %s "
		    "coll=%s.", m_dbname,m_coll);
		g_numUrgentMerges++;
	}


	// tfndb has his own merge class since titledb merges write tfndb recs
	RdbMerge *m = &g_merge;
	if ( m->isMerging() )
		return false;

	// if we are tfndb and someone else is merging, do not merge unless
	// we have 3 or more files
	int32_t minToMerge = m_minToMerge;
	//if (g_tfndb.getRdb()==m_rdb&& g_merge.isMerging() && minToMerge <=2 )
	//	minToMerge = 3;
	// do not start a tfndb merge while someone is dumping because the
	// dump starves the tfndb merge and we clog up adding links. i think
	// this is mainly just indexdb dumps, but we'll see.
	//if(g_tfndb.getRdb() == m_rdb && g_indexdb.m_rdb.isDumping() ) return;

	// are we resuming a killed merge?
	bool resuming = false;
	for ( int32_t j = 0 ; j < numFiles ; j++ ) {
		// skip odd numbered files
		if ( m_fileIds[j] & 0x01 ) continue;
		// yes we are resuming a merge
		resuming = true;
		break;
	}

	// this triggers the negative rec concentration msg below and
	// tries to merge on one file...
	if ( ! resuming && m_numFiles <= 1 ) {
		m_nextMergeForced = false;
		return false;
	}

	// what percent of recs in the collections' rdb are negative?
	// the rdbmaps hold this info
	int64_t totalRecs = 0LL;
	float percentNegativeRecs = getPercentNegativeRecsOnDisk ( &totalRecs);
	bool doNegCheck = false;
	// 1. if disk space is tight and >20% negative recs, force it
	if ( doNegCheck &&
	     g_process.m_diskAvail >= 0 && 
	     g_process.m_diskAvail < 10000000000LL && // 10GB
	     percentNegativeRecs > .20 ) {
		m_nextMergeForced = true;
		forceMergeAll = true;
		log("rdb: hit negative rec concentration of %f "
		    "(total=%"INT64") for "
		    "collnum %"INT32" on db %s when diskAvail=%"INT64" bytes",
		    percentNegativeRecs,totalRecs,(int32_t)m_collnum,
		    m_rdb->m_dbname,g_process.m_diskAvail);
	}
	// 2. if >40% negative recs force it
	if ( doNegCheck && 
	     percentNegativeRecs > .40 ) {
		m_nextMergeForced = true;
		forceMergeAll = true;
		log("rdb: hit negative rec concentration of %f "
		    "(total=%"INT64") for "
		    "collnum %"INT32" on db %s",
		    percentNegativeRecs,totalRecs,(int32_t)m_collnum,
		    m_rdb->m_dbname);
	}


	// . don't merge if we don't have the min # of files
	// . but skip this check if there is a merge to be resumed from b4
	if ( ! resuming && ! forceMergeAll && numFiles < minToMerge ) {
		// now we no longer have to check this collection rdb for
		// merging. this will save a lot of cpu time when we have
		// 20,000+ collections. if we dump a file to disk for it
		// then we set this flag back to false in Rdb.cpp.
		m_checkedForMerge = true;
		return false;
	}

	// bail if already merging THIS class
	if ( m_isMerging ) {
		if ( doLog ) 
			log(LOG_INFO,
			    "merge: Waiting for other merge to complete "
			    "before merging %s.",m_dbname);
		return false;
	}
	// bail if already waiting for it
	if ( m_waitingForTokenForMerge ) {
		if ( doLog ) 
			log(LOG_INFO,"merge: Already requested token. "
			    "Request for %s pending.",m_dbname);
		return false;
	}
	// score it
	m_waitingForTokenForMerge = true;
	// log a note
	//log(0,"RdbBase::attemptMerge: attempting merge for %s",m_dbname );
	// this merge forced?
	//m_nextMergeForced = forceMergeAll;
	// . bail if already merging
	// . no, RdbMerge will sleep in 5 sec cycles into they're done
	// . we may have multiple hosts running on the same cpu/hardDrive
	// . therefore, to maximize disk space, we should only have 1 merge
	//   at a time going on between these hosts
	// . we cannot merge files that are being dumped either because we'll
	//   lose data!!
	// . obsolete this shit for the token stuff
	/*
	if ( g_merge.isMerging() ) {
		// if we've been here once already, return now
		if ( m_inWaiting ) return;
		// otherwise let everyone know we're waiting
		log("RdbBase::attemptMerge: waiting for another merge to finish.");
		// set a flag so we don't keep printing the above msg
		m_inWaiting = true;
		// if it fails then sleep until it works
		g_loop.registerSleepCallback (5000,this,attemptMergeWrapper);
		return;
	}
	if ( m_dump.isDumping() ) {
		// bail if we've been here
		if ( m_inWaiting  ) return;
		// otherwise let everyone know we're waiting
		log("RdbBase::attemptMerge: waiting for dump to finish.");
		// set a flag so we don't keep printing the above msg
		m_inWaiting = true;
		// . if it fails then sleep until it works
		// . wait int16_ter to try and sneak a merge in if our
		//   buffer is so small that we're always dumping!
		//   should the next merge we do be forced?
		g_loop.registerSleepCallback (5000,this,attemptMergeWrapper);
		return;
	}
	*/
	// remember niceness for calling g_merge.merge()
	m_niceness = niceness;
	// debug msg
	//log (0,"RdbBase::attemptMerge: %s: getting token for merge", m_dbname);
	// . get token before merging
	// . returns true and sets g_errno on error
	// . returns true if we always have the token (just one host in group)
	// . returns false if blocks (the usual case)
	// . higher priority requests always supercede lower ones
	// . ensure we only call this once per dump we need otherwise, 
	//   gotTokenForMergeWrapper() may be called multiple times
	// . if a host is always in urgent mode he may starve another host
	//   whose is too, but his old request has an low priority.
	//int32_t priority = 0;
	// save this so gotTokenForMerge() can use it
	m_doLog = doLog;
	//if ( m_mergeUrgent ) priority = 2;
	//else                 priority = 0;
	// tfndb doesn't need token, since titledb merge writes tfndb recs
	// if ( //m_rdb != g_tfndb.getRdb() &&
	//      ! g_msg35.getToken ( this , gotTokenForMergeWrapper, priority))
	// 	return ;
	// bitch if we got token because there was an error somewhere
	if ( g_errno ) {
		log(LOG_LOGIC,"merge: attemptMerge: %s failed: %s",
		    m_dbname,mstrerror(g_errno));
		g_errno = 0 ;
		log(LOG_LOGIC,"merge: attemptMerge: %s: uh oh...",m_dbname);
		// undo request
		m_waitingForTokenForMerge = false;		 
		// we don't have the token, so we're fucked...
		return false;
	}
	// debug msg
	//if ( doLog )
	//log(LOG_INFO,"merge: Got merge token for %s without blocking.",
	//    m_dbname);
	// if did not block
/*
	gotTokenForMerge ( );
}

void gotTokenForMergeWrapper ( void *state ) {
	RdbBase *THIS = (RdbBase *)state;
	THIS->gotTokenForMerge();
}

void RdbBase::gotTokenForMerge ( ) {
*/
	// debug mg
	//log("RdbBase::gotTokenForMerge: for %s",m_dbname);
	// don't repeat
	m_waitingForTokenForMerge = false;
	// if a dump is happening it will always be the last file, do not
	// include it in the merge
	//int32_t numFiles = m_numFiles;
	//if ( numFiles > 0 && m_dump->isDumping() ) numFiles--;

	// . if we are significantly over our m_minToMerge limit
	//   then set m_mergeUrgent to true so merge disk operations will
	//   starve any spider disk reads (see Threads.cpp for that)
	// . TODO: fix this: if already merging we'll have an extra file
	// . i changed the 4 to a 14 so spider is not slowed down so much
	//   when getting link info... but real time queries will suffer!
	if ( ! m_mergeUrgent && numFiles - 14 >= m_minToMerge ) {
		m_mergeUrgent = true;
		if ( m_doLog )
		log(LOG_INFO,
		    "merge: Entering urgent merge mode (2) for %s coll=%s.", 
		    m_dbname,m_coll);
		g_numUrgentMerges++;
	}
	// tfndb has his own merge class since titledb merges write tfndb recs
	//RdbMerge *m = &g_merge;
	//if ( m_rdb == g_tfndb.getRdb() ) m = &g_merge2;
	// sanity check
	if ( m_isMerging || m->isMerging() ) {
		//if ( m_doLog )
			//log(LOG_INFO,
			//"merge: Someone already merging. Waiting for "
			//"merge token "
			//"in order to merge %s.",m_dbname);
		return false;
	}

	// or if # threads out is positive
	if ( m_numThreads > 0 ) return false;

	// clear for take-off
	//m_inWaiting = false;
	//	log(0,"RdbBase::attemptMerge: someone else merging"); return; }
	// . i used to just merge all the files into 1
	// . but it may be more efficient to merge just enough files as
	//   to put m_numFiles below m_minToMerge
	// . if we have the files : A B C D E F and m_minToMerge is 6
	//   then merge F and E, but if D is < E merged D too, etc...
	// . this merge algorithm is definitely better than merging everything
	//   if we don't do much reading to the db, only writing
	int32_t      n    = 0;
	int32_t      mergeFileId;
	int32_t      mergeFileNum;
	float     minr ;
	int64_t mint ;
	int32_t      mini ;
	bool      minOld ;
	int32_t      id2  = -1;
	//int32_t      minToMerge;
	bool      overide = false;
	//int32_t      smini = - 1;
	//int32_t      sn ;
	//int64_t tfndbSize = 0;
	int32_t      nowLocal = getTimeLocal();

	// but if niceness is 0 merge ALL files
	//if ( niceness == 0 ) {
	//	n = m_numFiles;
	//	i = -1;
	//	goto skip;
	//}

	//char rdbId = getIdFromRdb ( m_rdb );

	// if one file is even #'ed then we were merging into that, but
	// got interrupted and restarted. maybe the power went off or maybe
	// gb saved and exited w/o finishing the merge.
	for ( int32_t j = 0 ; j < numFiles ; j++ ) {
		// skip odd numbered files
		if ( m_fileIds[j] & 0x01 ) continue;
		// hey, we got a file that was being merged into
		mergeFileId = m_fileIds[j];
		// store the merged data into this file #
		mergeFileNum = j ;
		// files being merged into have a filename like 
		// indexdb0000.003.dat where the 003 indicates how many files
		// is is merging in case we have to resume them due to power 
		// loss or whatever
		char *s = m_files[j]->getFilename();
		// skip "indexdb0001."
		s += gbstrlen ( m_dbname ) + 5;
		// if titledb we got a "-023" part now
		if ( m_isTitledb ) {
			id2 = atol2 ( s , 3 );
			if ( id2 < 0 ) { char *xx = NULL; *xx =0; }
			s += 4;
		}
		// get the "003" part
		n = atol2 ( s , 3 );
		// sanity check
		if ( n <= 1 ) {
			log(LOG_LOGIC,"merge: attemptMerge: Resuming. bad "
			    "engineer for %s coll=%s",m_dbname,m_coll);
			//g_msg35.releaseToken();
			if ( m_mergeUrgent ) {
				log("merge: leaving urgent merge mode");
				g_numUrgentMerges--;
				m_mergeUrgent = false;
			}
			return false;
		}
		// make a log note
		log(LOG_INFO,"merge: Resuming killed merge for %s coll=%s.",
		    m_dbname,m_coll);
		// compute the total size of merged file
		mint = 0;
		int32_t mm = 0;
		for ( int32_t i = mergeFileNum ; i <= mergeFileNum + n ; i++ ) {
			if ( i >= m_numFiles ) {
				log("merge: Number of files to merge has "
				    "shrunk from %"INT32" to %"INT32" since time of "
				    "last merge. Probably because those files "
				    "were deleted because they were "
				    "exhausted and had no recs to offer."
				    ,n,m_numFiles);
				//char *xx=NULL;*xx=0;
				break;
			}
			if ( ! m_files[i] ) {
				log("merge: File #%"INT32" is NULL, skipping.",i);
				continue;
			}
			// only count files AFTER the file being merged to
			if ( i > j ) mm++;
			mint += m_files[i]->getFileSize();
		}
		if ( mm != n ) {
			log("merge: Only merging %"INT32" instead of the "
			    "original %"INT32" files.",mm,n);
			// cause the "if (mm==0)" to kick in below
			if ( mm == 1 || mm == 0 ) {
				mm = 0;
				// fix renaming final merged file tagdb-001.dat
				mergeFileId = 2;
				m_fileIds[j] = 1;
			}
		}
		// how many files to merge?
		n = mm;
		// allow a single file to continue merging if the other
		// file got merged out already
		if ( mm > 0 ) overide = true;

		// if we've already merged and already unlinked, then the
		// process exited, now we restart with just the final 
		// merge final and we need to do the rename
		if ( mm == 0 && rdbId != RDB_TITLEDB ) {
			m_isMerging = false;
			// make a fake file before us that we were merging
			// since it got nuked on disk
			//incorporateMerge();
			char fbuf[256];
			sprintf(fbuf,"%s%04"INT32".dat",m_dbname,mergeFileId-1);
			if ( m_isTitledb )
				sprintf(fbuf,"%s%04"INT32"-%03"INT32".dat",
					m_dbname,mergeFileId-1,id2);
			log("merge: renaming final merged file %s",fbuf);
			// this does not use a thread...
			m_files[j]->rename(fbuf);
			sprintf(fbuf,"%s%04"INT32".map",m_dbname,mergeFileId-1);
			//File *mf = m_maps[j]->getFile();
			m_maps[j]->rename(fbuf);
			log("merge: renaming final merged file %s",fbuf);
			return false;
		}

		// resume the merging
		goto startMerge;
	}

	minToMerge = m_minToMerge;


	// if we are reblancing this coll then keep merges tight so all
	// the negative recs annihilate with the positive recs to free
	// up disk space since we could be int16_t on disk space.
	//if ( g_rebalance.m_isScanning &&
	//     // if might have moved on if not able to merge because
	//     // another was merging... so do this anyway...
	//     g_rebalance.m_collnum == m_collnum )
	//	minToMerge = 2;


	//if (m_rdb==g_tfndb.getRdb()&& g_merge.isMerging() && minToMerge <=2 )
	//	minToMerge = 3;

	// look at this merge:
	// indexdb0003.dat.part1
	// indexdb0003.dat.part2
	// indexdb0003.dat.part3
	// indexdb0003.dat.part4
	// indexdb0003.dat.part5
	// indexdb0003.dat.part6
	// indexdb0003.dat.part7
	// indexdb0039.dat
	// indexdb0039.dat.part1
	// indexdb0045.dat
	// indexdb0047.dat
	// indexdb0002.002.dat
	// indexdb0002.002.dat.part1
	// it should have merged 45 and 46 since they are so much smaller
	// even though the ratio between 3 and 39 is lower. we did not compute
	// our dtotal correctly...

	// . use greedy method
	// . just merge the minimum # of files to stay under m_minToMerge
	// . files must be consecutive, however
	// . but ALWAYS make sure file i-1 is bigger than file i
	n = numFiles - minToMerge + 2 ;
	// . limit for posdb since more than about 8 gets abnormally slow
	// . no this was a ruse, the spider was evaluating firstips in the
	//   bg slowing the posdb merge down.
	//if ( m_rdb && m_rdb->m_rdbId == RDB_POSDB && n > 8 )
	//	n = 8;
	// titledb should always merge at least 50 files no matter what though
	// cuz i don't want it merging its huge root file and just one
	// other file... i've seen that happen... but don't know why it didn't
	// merge two small files! i guess because the root file was the
	// oldest file! (38.80 days old)???
	if ( m_isTitledb && n < 50 && minToMerge > 200 ) {
		// force it to 50 files to merge
		n = 50;
		// but must not exceed numFiles!
		if ( n > numFiles ) n = numFiles;
	}
	// NEVER merge more than this many files, our current merge routine
	// does not scale well to many files
	if ( m_absMaxFiles > 0 && n > m_absMaxFiles ) n = m_absMaxFiles;
	//smini = -1;
	// but if we are forcing then merge ALL, except one being dumped
	if ( m_nextMergeForced ) n = numFiles;
	// or if doing relabalncing, merge them all. tight merge
	//if ( g_rebalance.m_isScanning && g_rebalance.m_collnum == m_collnum) 
	//	n = numFiles;
	//else if ( m_isTitledb ) {
	//	RdbBase *base = g_tfndb.getRdb()->m_bases[m_collnum];
	//	tfndbSize = base->getDiskSpaceUsed();
	//}
	//tryAgain:
	minr = 99999999999.0;
	mint = 0x7fffffffffffffffLL ;
	mini = -1;
	minOld = false;
	for ( int32_t i = 0 ; i + n <= numFiles ; i++ ) {
		// oldest file
		time_t date = -1;
		// add up the string
		int64_t total = 0;
		for ( int32_t j = i ; j < i + n ; j++ ) {
			total += m_files[j]->getFileSize();
			time_t mtime = m_files[j]->getLastModifiedTime();
			// skip on error
			if ( mtime < 0 ) continue;
			if ( mtime > date ) date = mtime;
		}

		// does it have a file more than 30 days old?
		bool old = ( date < nowLocal - 30*24*3600 );
		// not old if error (date will be -1)
		if ( date < 0 ) old = false;
		// if it does, and current winner does not, force ourselves!
		if ( old && ! minOld ) mint = 0x7fffffffffffffffLL ;
		// and if we are not old and the min is, do not consider
		if ( ! old && minOld ) continue;

		// if merging titledb, just pick by the lowest total
		if ( m_isTitledb ) {
			if ( total < mint ) {
				mini   = i;
				mint   = total;
				minOld = old;
				log(LOG_INFO,"merge: titledb i=%"INT32" n=%"INT32" "
				    "mint=%"INT64" mini=%"INT32" "
				    "oldestfile=%.02fdays",
				    i,n,mint,mini,
				    ((float)nowLocal-date)/(24*3600.0) );
			}
			continue;
		}
		// . get the average ratio between mergees
		// . ratio in [1.0,inf)
		// . prefer the lowest average ratio
		double ratio = 0.0;
		for ( int32_t j = i ; j < i + n - 1 ; j++ ) {
			int64_t s1 = m_files[j  ]->getFileSize();
			int64_t s2 = m_files[j+1]->getFileSize();
			int64_t tmp;
			if ( s2 == 0 ) continue;
			if ( s1 < s2 ) { tmp = s1; s1 = s2 ; s2 = tmp; }
			ratio += (double)s1 / (double)s2 ;
		}
		if ( n >= 2 ) ratio /= (double)(n-1);
		// sanity check
		if ( ratio < 0.0 ) {
			logf(LOG_LOGIC,"merge: ratio is negative %.02f",ratio);
			char *xx = NULL; *xx = 0; 
		}
		// the adjusted ratio
		double adjratio = ratio;
		// . adjust ratio based on file size of current winner
		// . if winner is ratio of 1:1 and we are 10:1 but winner
		//   is 10 times bigger than us, then we have a tie. 
		// . i think if we are 10:1 and winner is 3 times bigger
		//   we should have a tie
		if ( mini >= 0 && total > 0 && mint > 0 ) {
			double sratio = (double)total/(double)mint;
			//if ( mint>total ) sratio = (float)mint/(float)total;
			//else              sratio = (float)total/(float)mint;
			adjratio *= sratio;
		}


		// debug the merge selection
		char      tooBig   = 0;
		int64_t prevSize = 0;
		if ( i > 0 ) prevSize = m_files[i-1]->getFileSize();
		if ( i > 0 && prevSize < total/4 ) tooBig = 1;
		log(LOG_INFO,"merge: i=%"INT32" n=%"INT32" ratio=%.2f adjratio=%.2f "
		    "minr=%.2f mint=%"INT64" mini=%"INT32" prevFileSize=%"INT64" "
		    "mergeFileSize=%"INT64" tooBig=%"INT32" oldestfile=%.02fdays "
		    "collnum=%"INT32"",
		    i,n,ratio,adjratio,minr,mint,mini,
		    prevSize , total ,(int32_t)tooBig,
		    ((float)nowLocal-date)/(24*3600.0) ,
		    (int32_t)m_collnum);

		// . if we are merging the last file, penalize it
		// . otherwise, we dump little files out and almost always
		//   merge them into the next file right away, but if we
		//   merged two in the back instead, our next little dump would
		//   merge with our previous little dump in no time. so we
		//   have to look into the future a step...
		// . count the next to last guy twice
		//if ( n == 2 && i + n == numFiles && i + n - 2 >= 0 ) 
		//	total += m_files[i+n-2]->getFileSize();
		// bring back the greedy merge
		if ( total >= mint ) continue;
		//if ( adjratio > minr && mini >= 0 ) continue;
		//if ( ratio > minr && mini >= 0 ) continue;
		// . don't get TOO lopsided on me now
		// . allow it for now! this is the true greedy method... no!
		// . an older small file can be cut off early on by a merge
		//   of middle files. the little guy can end up never getting
		//   merged unless we have this.
		// . allow a file to be 4x bigger than the one before it, this
		//   allows a little bit of lopsidedness.
		if (i > 0  && m_files[i-1]->getFileSize() < total/4 ) continue;
		//min  = total;
		minr   = ratio;
		mint   = total;
		mini   = i;
		minOld = old;
	}
	// . if titledb, merge at least enough titledb recs to equal the size
	//   of tfndb times three if we can, without going too far overboard
	//   because we have to read the *entire* tfndb just to merge two
	//   titledb files... for now at least
	// . save the settings that work in case we have to pop them
	// . times 6 is better than times 3 because reading from tfndb can
	//   be cpu intensive if we have to merge in the list from the rdb tree
	//if ( m_isTitledb && mint<6*tfndbSize && mini>=0 && n<numFiles ) {
	//	smini = mini;
	//	sn    = n;
	//	// but try increasing n unti we bust
	//	n++;
	//	goto tryAgain;
	//}
	// bring back saved if we should
	//if ( smini >= 0 ) {
	//	mini = smini;
	//	n    = sn;
	//}
	// if no valid range, bail
	if ( mini == -1 ) { 
		log(LOG_LOGIC,"merge: gotTokenForMerge: Bad engineer. mini "
		    "is -1.");
		//g_msg35.releaseToken(); 
		return false; 
	}
	// . merge from file #mini through file #(mini+n)
	// . these files should all have ODD fileIds so we can sneak a new
	//   mergeFileId in there
	mergeFileId = m_fileIds[mini] - 1;
	// get new id, -1 on error
	id2 = -1;
	if ( m_isTitledb ) {
		//id2 = getAvailId2 ( );
		// this is obsolete, make 000 for now
		id2 = 000;
		if ( id2 < 0 ) {
			log(LOG_LOGIC,"merge: attemptMerge: could not add "
			    "new file for titledb. No avail ids."); 
			g_errno = 0;
			//g_msg35.releaseToken();
			return false; 
		}
	}
	// . make a filename for the merge
	// . always starts with file #0
	// . merge targets are named like "indexdb0000.dat.002"
	// . for titledb is "titledb0000-023.dat.003" (23 is id2)
	// . this now also sets m_mergeStartFileNum for us... but we override
	//   below anyway. we have to set it there in case we startup and
	//   are resuming a merge.
	mergeFileNum = addFile ( mergeFileId , true/*new file?*/ , n , id2 ) ;
	if ( mergeFileNum < 0 ) {
		log(LOG_LOGIC,"merge: attemptMerge: Could not add new file."); 
		g_errno = 0;
		//g_msg35.releaseToken();
		return false; 
	}
	// we just opened a new file
	//g_sync.addOp ( OP_OPEN , m_files[mergeFileNum] , 0 );
	
	// is it a force?
	if ( m_nextMergeForced ) log(LOG_INFO,
				     "merge: Force merging all %s "
				     "files, except those being dumped now.",
				     m_dbname);
	// clear after each call to attemptMerge()
	m_nextMergeForced = false;

 startMerge:
	// sanity check
	if ( n <= 1 && ! overide ) {
	       log(LOG_LOGIC,"merge: gotTokenForMerge: Not merging %"INT32" files.",
		    n);
	       //g_msg35.releaseToken(); 
		return false; 
	}

	// . save the # of files we're merging for the cleanup process
	// . don't include the first file, which is now file #0
	m_numFilesToMerge   = n  ; // numFiles - 1;
	m_mergeStartFileNum = mergeFileNum + 1; // 1

	//CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	char *coll = "";
	if ( cr ) coll = cr->m_coll;

	// log merge parms
	log(LOG_INFO,"merge: Merging %"INT32" %s files to file id %"INT32" now. "
	    "collnum=%"INT32" coll=%s",
	    n,m_dbname,mergeFileId,(int32_t)m_collnum,coll);

	// print out file info
	m_numPos = 0;
	m_numNeg = 0;
	for ( int32_t i = m_mergeStartFileNum ; 
	      i < m_mergeStartFileNum + m_numFilesToMerge ; i++ ) {
		m_numPos += m_maps[i]->getNumPositiveRecs();		
		m_numNeg += m_maps[i]->getNumNegativeRecs();
		log(LOG_INFO,"merge: %s (#%"INT32") has %"INT64" positive "
		     "and %"INT64" negative records." , 
		     m_files[i]->getFilename() ,
		     i , 
		     m_maps[i]->getNumPositiveRecs(),
		     m_maps[i]->getNumNegativeRecs() );
	}
	log(LOG_INFO,"merge: Total positive = %"INT64" Total negative = %"INT64".",
	     m_numPos,m_numNeg);

	// assume we are now officially merging
	m_isMerging = true;

	m_rdb->m_numMergesOut++;

	//char rdbId = getIdFromRdb ( m_rdb );

	// sanity check
	if ( m_niceness == 0 ) { char *xx=NULL;*xx=0 ; }

	// . start the merge
	// . returns false if blocked, true otherwise & sets g_errno
	if ( ! m->merge ( rdbId  ,
			  m_collnum ,
			  m_files[mergeFileNum] ,
			  m_maps [mergeFileNum] ,
			  id2                   ,
			  m_mergeStartFileNum   ,
			  m_numFilesToMerge     ,
			  m_niceness            ,
			  NULL,//m_pc                  ,
			  mint /*maxTargetFileSize*/ ,
			  m_ks                  ) ) 
		// we started the merge so return true here
		return true;
	// hey, we're no longer merging i guess
	m_isMerging = false;
	// decerment this count
	m_rdb->m_numMergesOut--;
	// . if we have no g_errno that is bad!!!
	// . we should dump core here or something cuz we have to remove the
	//   merge file still to be correct
	//if ( ! g_errno )
	//	log(LOG_INFO,"merge: Got token without blocking.");
	// we now set this in init() by calling m_merge.init() so it
	// can pre-alloc it's lists in it's s_msg3 class
	//		       g_conf.m_mergeMaxBufSize ) ) return ;
	// bitch on g_errno then clear it
	if ( g_errno ) 
		log("merge: Had error getting merge token for %s: %s.",
		    m_dbname,mstrerror(g_errno));
	g_errno = 0;
	// give token back
	//g_msg35.releaseToken();
	// try again
	//m_rdb->attemptMerge( m_niceness, false , true );
	// how did this happen?
	log("merge: did not block for some reason.");
	return true;
}

// . use the maps and tree to estimate the size of this list w/o hitting disk
// . used by Indexdb.cpp to get the size of a list for IDF weighting purposes
//int32_t RdbBase::getListSize ( key_t startKey , key_t endKey , key_t *max ,
int64_t RdbBase::getListSize ( char *startKey , char *endKey , char *max ,
			         int64_t oldTruncationLimit ) {
	// . reset this to low points
	// . this is on
	//*max = endKey;
	KEYSET(max,endKey,m_ks);
	bool first = true;
	// do some looping
	//key_t newGuy;
	char newGuy[MAX_KEY_BYTES];
	int64_t totalBytes = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// the start and end pages for a page range
		int32_t pg1 , pg2;
		// get the start and end pages for this startKey/endKey
		m_maps[i]->getPageRange ( startKey , 
					  endKey   , 
					  &pg1     , 
					  &pg2     ,
					  //&newGuy  ,
					  newGuy   ,
					  oldTruncationLimit );
		// . get the range size add it to count
		// . some of these records are negative recs (deletes) so
		//   our count may be small
		// . also, count may be a bit small because getRecSizes() may
		//   not recognize some recs on the page boundaries as being
		//   in [startKey,endKey]
		// . this can now return negative sizes
		// . the "true" means to subtract page sizes that begin with
		//   delete keys (the key's low bit is clear)
		// . get the minKey and maxKey in this range
		// . minKey2 may be bigger than the actual minKey for this
		//   range, likewise, maxKey2 may be smaller than the actual
		//   maxKey, but should be good estimates
		int64_t maxBytes = m_maps[i]->getMaxRecSizes ( pg1     ,
							    pg2     ,
							    startKey,
							    endKey  ,
							    true    );//subtrct
		// get the min as well
		int64_t minBytes = m_maps[i]->getMinRecSizes ( pg1     ,
							    pg2     ,
							    startKey,
							    endKey  ,
							    true    );//subtrct
		int64_t avg = (maxBytes + minBytes) / 2LL;
		// use that
		totalBytes += avg;
		// if not too many pages then don't even bother setting "max"
		// since it is used only for interpolating if this term is
		// truncated. if only a few pages then it might be way too
		// small.
		if ( pg1 + 5 > pg2 ) continue;
		// replace *max automatically if this is our first time
		//if ( first ) { *max = newGuy; first = false; continue; }
		if ( first ) { 
			KEYSET(max,newGuy,m_ks); first = false; continue; }
		// . get the SMALLEST max key
		// . this is used for estimating what the size of the list
		//   would be without truncation
		//if ( newGuy > *max ) *max = newGuy;
		if ( KEYCMP(newGuy,max,m_ks)>0 ) KEYSET(max,newGuy,m_ks);
	}

	// debug
	// log("PHASE1: sk=%s ek=%s bytes=%i"
	//     ,KEYSTR(startKey,m_ks)
	//     ,KEYSTR(endKey,m_ks)
	//     ,(int)totalBytes);

	// TODO: now get from the btree!
	// before getting from the map (on disk IndexLists) get upper bound
	// from the in memory b-tree
	//int32_t n=getTree()->getListSize (startKey, endKey, &minKey2, &maxKey2);
	int64_t n;
	if(m_tree) n = m_tree->getListSize ( m_collnum ,
					     startKey , endKey , NULL , NULL );
	else n = m_buckets->getListSize ( m_collnum ,
					  startKey , endKey , NULL , NULL );

	// debug
	// RdbList list;
	// m_buckets->getList ( m_collnum,
	// 		     startKey,
	// 		     endKey,
	// 		     10000000,
	// 		     &list ,
	// 		     NULL,
	// 		     NULL,
	// 		     true);
	// g_posdb.printList ( list );


	totalBytes += n;
	//if ( minKey2 < *minKey ) *minKey = minKey2;
	//if ( maxKey2 > *maxKey ) *maxKey = maxKey2;	
	// ensure totalBytes >= 0
	if ( totalBytes < 0 ) totalBytes = 0;

	// debug
	// log("PHASE2: sk=%s ek=%s bytes=%i"
	//     ,KEYSTR(startKey,m_ks)
	//     ,KEYSTR(endKey,m_ks)
	//     ,(int)totalBytes);

	return totalBytes;
}

int64_t RdbBase::getNumGlobalRecs ( ) {
	return getNumTotalRecs() * g_hostdb.m_numShards;
}

// . return number of positive records - negative records
int64_t RdbBase::getNumTotalRecs ( ) {
	int64_t numPositiveRecs = 0;
	int64_t numNegativeRecs = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		// skip even #'d files -- those are merge files
		if ( (m_fileIds[i] & 0x01) == 0 ) continue;
		numPositiveRecs += m_maps[i]->getNumPositiveRecs();
		numNegativeRecs += m_maps[i]->getNumNegativeRecs();
	}
	// . add in the btree
	// . TODO: count negative and positive recs in the b-tree
	// . assume all positive for now
	// . for now let Rdb add the tree in RdbBase::getNumTotalRecs()
	if(m_tree) {
		numPositiveRecs += m_tree->getNumPositiveKeys(m_collnum);
		numNegativeRecs += m_tree->getNumNegativeKeys(m_collnum);
	}
	else {
		// i've seen this happen when adding a new coll i guess
		if ( ! m_buckets ) return 0;
		//these routines are slow because they count every time.
		numPositiveRecs += m_buckets->getNumKeys(m_collnum);
		//numPositiveRecs += m_buckets->getNumPositiveKeys(m_collnum);
		//numNegativeRecs += m_buckets->getNumNegativeKeys(m_collnum);
	}
	//int64_t total = numPositiveRecs - numNegativeRecs;
	//if ( total < 0 ) return 0LL;
	//return total;
	return numPositiveRecs - numNegativeRecs;
}

// . how much mem is alloced for all of our maps?
// . we have one map per file
int64_t RdbBase::getMapMemAlloced () {
	int64_t alloced = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) 
		alloced += m_maps[i]->getMemAlloced();
	return alloced;
}

// sum of all parts of all big files
int32_t RdbBase::getNumSmallFiles ( ) {
	int32_t count = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) 
		count += m_files[i]->getNumParts();
	return count;
}

int64_t RdbBase::getDiskSpaceUsed ( ) {
	int64_t count = 0;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) 
		count += m_files[i]->getFileSize();
	return count;
}

// . convert a tfn (title file num)(encoded in tfndb recs) to a real file num
// . used by Msg22
// . tfndb maps a docid to a tfn (tfndb is like an RdbMap but fatter)
// . if id2 is not present, it is because of our old bug: some middle files
//   got merged and the file nums changed, so all the old tfns are mapping to
//   an older file than they should!  so if 2 merges happened, if the merge 
//   above the other merge
// . IMPORTANT: de-compensate for merge file inserted before us
int32_t RdbBase::getFileNumFromId2 ( int32_t id2 ) {
	int32_t prev = -1;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		if ( m_fileIds2[i] == id2 ) {
			if ( m_hasMergeFile && i >= m_mergeStartFileNum ) i--;
			return i;
		}
		if ( m_fileIds2[i] < id2 ) prev = i;
	}
	// if not there, must have been merged below, use prev
	if ( m_hasMergeFile && prev >= m_mergeStartFileNum ) prev--;
	// debug msg
	//log("Rdb:getFileNumFromId2: id2 of %"INT32" is invalid. returning "
	//    "startFileNum of %"INT32".",id2,prev,id2);
	log("db: titledb*-%"INT32".dat file in collection \"%s\" "
	    "is referenced but no longer exists. "
	    "To fix this do a tight merge on titledb; you may have to delete "
	    "tfndb* and regenerate it using the 'gb gendbs' command after the "
	    "tight merge completes if the document is indeed missing. Cause "
	    "may have been an improper shutdown, or not saving tfndb or "
	    "titledb, or a missing document in titledb.",id2,m_coll);
	//log("DISK: titledb*-%"INT32".dat file is referenced but no longer exists."
	//  " See section on Database Repair in overview.html to fix it.",id2);
	return -1; // prev;
}

// . used by Sync.cpp to convert a sync point to a list of file nums
//   to read from for syncing
// . if it is m_savedFile then return -2
// . if it is not found   then return -1
int32_t RdbBase::getFileNumFromName ( char *fname ) {
	// this is a special case
	if ( strstr ( fname , "-sav" ) ) return -2;
	// scan our current files
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		char *ff = m_files[i]->getFilename();
		if ( strcmp ( ff , fname ) == 0 ) return i;
	}
	return -1;
}

void RdbBase::closeMaps ( bool urgent ) {
	for ( int32_t i = 0 ; i < m_numFiles ; i++ )
		m_maps[i]->close ( urgent );
}

void RdbBase::saveMaps ( bool useThread ) {
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		if ( ! m_maps[i] ) {
			log("base: map for file #%i is null",i);
			continue;
		}
		m_maps[i]->writeMap ( false );
	}
}

void RdbBase::verifyDiskPageCache ( ) {
	//if ( !m_pc ) return;
	// disable for now
	return;
	// for ( int32_t i = 0; i < m_numFiles; i++ ){
	// 	BigFile *f = m_files[i];
	// 	m_pc->verifyData(f);
	// }
}

bool RdbBase::verifyFileSharding ( ) {

	if ( m_rdb->m_isCollectionLess ) return true;

	// if swapping in from CollectionRec::getBase() then do
	// not re-verify file sharding! only do at startup
	if ( g_loop.m_isDoingLoop ) return true;

	// skip for now to speed up startup
	static int32_t s_count = 0;
	s_count++;
	if ( s_count == 50 )
		log("db: skipping shard verification for remaining files");
	if ( s_count >= 50 ) 
		return true;

	g_threads.disableThreads();

	Msg5 msg5;
	//Msg5 msg5b;
	RdbList list;
	char startKey[MAX_KEY_BYTES];
	char endKey[MAX_KEY_BYTES];
	KEYMIN(startKey,MAX_KEY_BYTES);
	KEYMAX(endKey,MAX_KEY_BYTES);
	int32_t minRecSizes = 64000;
	char rdbId = m_rdb->m_rdbId;
	if ( rdbId == RDB_TITLEDB ) minRecSizes = 640000;
	
	log ( "db: Verifying shard parity for %s of %"INT32" bytes "
	      "for coll %s (collnum=%"INT32")...", 
	      m_dbname , 
	      minRecSizes,
	      m_coll , (int32_t)m_collnum );

	if ( ! msg5.getList ( m_rdb->m_rdbId, //RDB_POSDB   ,
			      m_collnum       ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      NULL          , // &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	int32_t printed = 0;
	char k[MAX_KEY_BYTES];

	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		//key144_t k;
		list.getCurrentKey(k);

		// skip negative keys
		if ( (k[0] & 0x01) == 0x00 ) continue;

		count++;
		//uint32_t groupId = k.n1 & g_hostdb.m_groupMask;
		//uint32_t groupId = getGroupId ( RDB_POSDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( rdbId , k );

		if ( shardNum == getMyShardNum() ) {
			got++;
			continue;
		}

		if ( ++printed > 100 ) continue;

		// avoid log spam... comment this out. nah print out 1st 100.
		log ( "db: Found bad key in list belongs to shard %"INT32"",
		      shardNum);
	}

	g_threads.enableThreads();

	//if ( got ) 
	//	log("db: verified %"INT32" recs for %s in coll %s",
	//	    got,m_dbname,m_coll);
       
	if ( got == count ) {
		//log("db: passed on %"INT32" recs (count=%"INT32")",got,count);
		return true;
	}

	// tally it up
	g_rebalance.m_numForeignRecs += count - got;
	log ("db: Out of first %"INT32" records in %s for %s.%"INT32", only %"INT32" belong "
	     "to our group.",count,m_dbname,m_coll,(int32_t)m_collnum,got);
	// exit if NONE, we probably got the wrong data
	//if ( got == 0 ) log("db: Are you sure you have the "
	//		    "right data in the right directory? ");

	//log ( "db: Exiting due to Posdb inconsistency." );
	g_threads.enableThreads();
	return true;//g_conf.m_bypassValidation;

	//log(LOG_DEBUG, "db: Posdb passed verification successfully for %"INT32" "
	//		"recs.", count );
	// DONE
	//return true;
}

float RdbBase::getPercentNegativeRecsOnDisk ( int64_t *totalArg ) {
	// scan the maps
	int64_t numPos = 0LL;
	int64_t numNeg = 0LL;
	for ( int32_t i = 0 ; i < m_numFiles ; i++ ) {
		numPos += m_maps[i]->getNumPositiveRecs();
		numNeg += m_maps[i]->getNumNegativeRecs();
	}
	int64_t total = numPos + numNeg;
	*totalArg = total;
	float percent = (float)numNeg / (float)total;
	return percent;
}
