// Matt Wells, copyright Sep 2000

// . the core database class, we have one of these for each collection and
//   pointer to them are stored in the new "Rdb" class
// . records stored on disk ordered by lowest key first
// . allows simple key-based record retrieval
// . uses non-blocking i/o with callbacks
// . thread UNsafe for maximum speed
// . has a "groupMask" that allows us to split db between multiple Rdb's
// . uses BigFile class to support files bigger than 2gb
// . can instantly delete records in memory 
// . deletes records on disk by re-writing them to disk with key low bit CLEAR
// . we merge files (non-blocking operation) into 1 file to save on disk seeks
// . adding a record with the same key as an existing one we will replace it
//   unless you set dedup to false which is yet to be supported
// . when mem is low dumps records from tree to disk, frees as it dumps
// . has a key-to-diskOffset/dataSize map in memory (good for small records)
//   for mapping a file of records on disk
// . this key-to-offset map takes up sizeof(key_t)+  bytes per disk page 
// . we can map .8 gigs of disk with 1 meg of mem (using page size of 8k)
// . memory is only freed by the Mem.h class when it finds it's running out
// . addRecord will only return false if there's some lack of memory problems
// . we can only dump the RdbTree to disk if it's using at least "minMem" or 
//   we are shutting down and Rdb::close() was called 

#ifndef _RDBBASE_H_
#define _RDBBASE_H_

#include "Conf.h"
#include "Mem.h"
#include "RdbScan.h"
#include "RdbDump.h"
#include "RdbTree.h"
#include "RdbBuckets.h"
#include "RdbCache.h"
#include "RdbMerge.h"
#include "Msg3.h"               // MAX_RDB_FILES definition
#include "Dir.h"
#include "RdbMem.h"

// how many rdbs are in "urgent merge" mode?
extern long g_numUrgentMerges;

extern RdbMerge g_merge;
extern RdbMerge g_merge2;

class RdbBase {

 public:

	 RdbBase ( );
	~RdbBase ( );

	// . the more memory the tree has the less file merging required
	// . when a slot's key is ANDed with "groupMask" the result must equal
	//   "groupId" in order to be in this database
	// . "minMem" is how much mem must be used before considering dumping
	//   the RdbTree (our unbalanced btree) to disk
	// . you can fix the dataSize of all records in this rdb by setting
	//   "fixedDataSize"
	// . if "maskKeyLowLong" we mask the lower long of the key and then
	//   compare that to the groupId to see if the record belongs
	// . this is currently just used by Spiderdb
	// . otherwise, we mask the high long in the key
	bool init ( char  *dir             , // working directory
		    char  *dbname          , // "indexdb","tagdb",...
		    bool   dedup           , //= true ,
		    long   fixedDataSize   , //= -1   ,
		    //unsigned long   groupMask       , //=  0   ,
		    //unsigned long   groupId         , //=  0   ,
		    long   minToMerge      , //, //=  2   ,
		    //long   maxTreeMem      , //=  1024*1024*32 ,
		    //long   maxTreeNodes    ,
		    //bool   isTreeBalanced  ,
		    //long   maxCacheMem     , //=  1024*1024*5 );
		    //long   maxCacheNodes   ,
		    bool   useHalfKeys     ,
		    char   keySize         ,
		    long   pageSize        ,
		    char                *coll    ,
		    collnum_t            collnum ,
		    RdbTree             *tree    ,
		    RdbBuckets          *buckets ,
		    RdbDump             *dump    ,
		    class Rdb           *rdb    ,
		    class DiskPageCache *pc = NULL ,
		    bool                 isTitledb = false , // use fileIds2[]?
		    bool                 preloadDiskPageCache = false ,
		    bool                 biasDiskPageCache    = false );

	void closeMaps ( bool urgent );
	void saveMaps  ( bool useThread );

	// . frees up all the memory and closes all files
	// . suspends any current merge (saves state to disk)
	// . calls reset() for each file
	// . will cause any open map files to dump
	// . will dump tables to backup or store 
	// . calls close on each file
	// . returns false if blocked, true otherwise
	// . sets errno on error
	//bool close ( void *state , 
	//	     void (* callback)(void *state ) ,
	//	     bool urgent ,
	//	     bool exitAfterClosing );
	//bool close ( ) { return close ( NULL , NULL ); };
	// used by PageMaster.cpp to check to see if all rdb's are closed yet
	//bool isClosed ( ) { return m_isClosed; };

	// . returns false and sets g_errno on error
	// . caller should retry later on g_errno of ENOMEM or ETRYAGAIN
	// . returns the node # in the tree it added the record to
	// . key low bit must be set (otherwise it indicates a delete)
	//bool addRecord ( key_t &key, char *data, long dataSize );

	// returns false if no room in tree or m_mem for a list to add
	//bool hasRoom ( RdbList *list );

	// . returns false on error and sets errno
	// . return true on success
	// . if we can't handle all records in list we don't add any and
	//   set errno to ETRYAGAIN or ENOMEM
	// . we copy all data so you can free your list when we're done
	//bool addList ( RdbList *list );

	// . add a record without any data, just a key (faster)
	// . returns the node # in the tree it added the record to
	//long addKey ( key_t &key );

	// . uses the bogus data pointed to by "m_dummy" for record's data
	// . we clear the key low bit to signal a delete
	// . returns false and sets errno on error
	//bool deleteRecord ( key_t &key ) ;

	// TODO: this needs to support
	// . we split our data over rdbs across the network based on masks
	// . we now just use g_conf.m_groupMask, g_hostdb.m_groupId, ...
	//long getGroupId ( key_t &key ) { return (key.n1 & m_groupMask); };
	//unsigned long getGroupMask ( ) { return m_groupMask; };
	//unsigned long getGroupId   ( ) { return m_groupId  ; };

	// . when a slot's key is ANDed with "groupMask" the result must equal
	//   "groupId" in order to be in this database
	// . used to split data across multiple rdbs
	//void setMask ( unsigned long groupMask , unsigned long groupId );

	// get the directory name where this rdb stores it's files
	char *getDir ( ) { return m_dir.getDirname(); };
	char *getStripeDir ( ) { return g_conf.m_stripeDir; };

	long getFixedDataSize ( ) { return m_fixedDataSize; };

	bool useHalfKeys ( ) { return m_useHalfKeys; };

	//RdbTree    *getTree    ( ) { return &m_tree; };
	//RdbCache   *getCache   ( ) { return &m_cache; };

	RdbMap   **getMaps  ( ) { return m_maps; };
	BigFile  **getFiles ( ) { return m_files; };

	BigFile   *getFile   ( long n ) { return m_files   [n]; };
	long       getFileId ( long n ) { return m_fileIds [n]; };
	long       getFileId2( long n ) { return m_fileIds2[n]; };
	RdbMap    *getMap    ( long n ) { return m_maps    [n]; };

	long getFileNumFromId  ( long id  ) ; // for converting old titledbs
	long getFileNumFromId2 ( long id2 ) ; // map tfn to real file num (rfn)

	//RdbMem    *getRdbMem () { return &m_mem; };

	// how much mem is alloced for our maps?
	long long getMapMemAlloced ();

	long       getNumFiles ( ) { return m_numFiles; };

	// sum of all parts of all big files
	long      getNumSmallFiles ( ) ;
	long long getDiskSpaceUsed ( );

	// returns -1 if variable (variable dataSize)
	long getRecSize ( ) {
		if ( m_fixedDataSize == -1 ) return -1;
		//return sizeof(key_t) + m_fixedDataSize; };
		return m_ks + m_fixedDataSize; };

	// use the maps and tree to estimate the size of this list
	//long getListSize ( key_t startKey ,key_t endKey , key_t *maxKey ,
	long long getListSize ( char *startKey ,char *endKey , char *maxKey ,
			        long long oldTruncationLimit ) ;

	// positive minus negative
	long long getNumTotalRecs ( ) ;

	long long getNumRecsOnDisk ( );

	long long getNumGlobalRecs ( );

	/*
	// used for keeping track of stats
	void      didSeek       (            ) { m_numSeeks++; };
	void      didRead       ( long bytes ) { m_numRead += bytes; };
	long long getNumSeeks   (            ) { return m_numSeeks; };
	long long getNumRead    (            ) { return m_numRead ; };

	// net stats for "get" requests
	void      readRequestGet ( long bytes ) { 
		m_numReqsGet++    ; m_numNetReadGet += bytes; };
	void      sentReplyGet     ( long bytes ) {
		m_numRepliesGet++ ; m_numNetSentGet += bytes; };
	long long getNumRequestsGet ( ) { return m_numReqsGet;    };
	long long getNetReadGet     ( ) { return m_numNetReadGet; };
	long long getNumRepliesGet  ( ) { return m_numRepliesGet; };
	long long getNetSentGet     ( ) { return m_numNetSentGet; };

	// net stats for "add" requests
	void      readRequestAdd ( long bytes ) { 
		m_numReqsAdd++    ; m_numNetReadAdd += bytes; };
	void      sentReplyAdd     ( long bytes ) {
		m_numRepliesAdd++ ; m_numNetSentAdd += bytes; };
	long long getNumRequestsAdd ( ) { return m_numReqsAdd;    };
	long long getNetReadAdd     ( ) { return m_numNetReadAdd; };
	long long getNumRepliesAdd  ( ) { return m_numRepliesAdd; };
	long long getNetSentAdd     ( ) { return m_numNetSentAdd; };

	// used by main.cpp to periodically save us if we haven't dumped
	// in a while
	long long getLastWriteTime   ( ) { return m_lastWrite; };
	*/
	
	// private:

	void attemptMerge ( long niceness , bool forceMergeAll , 
			    bool doLog = true ,
			    // -1 means to not override it
			    long minToMergeOverride = -1 );

	bool gotTokenForDump  ( ) ;
	void gotTokenForMerge ( ) ;

	// called after merge completed
	bool incorporateMerge ( );

	// . you'll lose your data in this class if you call this
	void reset();

	// . load the tree named "saved.dat", keys must be out of order because
	//   tree is not balanced
	//bool loadTree ( ) ;

	// . write out tree to a file with keys in order
	// . only shift.cpp/reindex.cpp programs set niceness to 0
	//bool dumpTree ( long niceness ); //= MAX_NICENESS );

	// . set the m_files, m_fileMaps, m_fileIds arrays and m_numFiles
	bool setFiles ( ) ;

	// . called when done saving a tree to disk (keys not ordered)
	//void doneSaving ( ) ;

	// . called when we've dumped the tree to disk w/ keys ordered
	//void doneDumping ( );

	void verifyDiskPageCache ( );

	// . add a (new) file to the m_files/m_maps/m_fileIds arrays
	// . both return array position we added it to
	// . both return -1 and set errno on error
	long addFile     ( long fileId, bool isNew, long mergeNum, long id2 ,
			   bool converting = false ) ;
	long addNewFile  ( long id2 ) ;
	long getAvailId2 ( ); // used only by titledb

	// used by the high priority udp server to suspend merging for ALL
	// rdb's since we share a common merge class, s_merge
	//void suspendAllMerges ( ) ;
	// resume ANY merges
	//void resumeAllMerges ( ) ;

	//bool needsDump ( );

	// these are used by Msg34 class for computing load on a machine
	bool isMerging ( ) { return m_isMerging; };
	bool isDumping ( ) { return m_dump->isDumping(); };

	bool hasMergeFile ( ) { return m_hasMergeFile; };

	// used for translating titledb file # 255 (as read from new tfndb)
	// into the real file number
	long getNewestFileNum ( ) { return m_numFiles - 1; };

	// Msg22 needs the merge info so if the title file # of a read we are
	// doing is being merged, we have to include the start merge file num
	long      getMergeStartFileNum ( ) { return m_mergeStartFileNum; };
	long      getMergeNumFiles     ( ) { return m_numFilesToMerge; };

	// used by Sync.cpp to convert a file name to a file number in m_files
	long getFileNumFromName ( char *filename );

	// bury m_files[] in [a,b)
	void buryFiles ( long a , long b );

	void doneWrapper2 ( ) ;
	void doneWrapper4 ( ) ;
	long m_x;
	long m_a;

	// PageRepair indirectly calls this to move the map and data of this
	// rdb into the trash subdir after renaming them, because they will
	// be replaced by the rebuilt files.
	bool moveToDir   ( char *dstDir ) { return moveToTrash ( dstDir ); };
	bool moveToTrash ( char *dstDir ) ;
	// PageRepair indirectly calls this to rename the map and data files
	// of a secondary/rebuilt rdb to the filenames of the primary rdb.
	// after that, RdbBase::setFiles() is called to reload them into
	// the primary rdb. this is called after moveToTrash() is called for
	// the primary rdb.
	bool removeRebuildFromFilenames ( ) ;
	bool removeRebuildFromFilename  ( BigFile *f ) ;

	// keep a copy of these here so merge can use them to kick out
	// records whose key when, ANDed w/ m_groupMask, equals
	// m_groupId
	//unsigned long  m_groupMask;
	//unsigned long  m_groupId;

	// . we try to minimize the number of files to minimize disk seeks
	// . records that end up as not found will hit all these files
	// . when we get "m_minToMerge" or more files a merge kicks in
	// . TODO: merge should combine just the smaller files... kinda
	// . files are sorted by fileId
	// . older files are listed first (lower fileIds)
	// . filenames should include the directory (full filenames)
	// . TODO: RdbMgr should control what rdb gets merged?
	BigFile  *m_files     [ MAX_RDB_FILES ];
	long      m_fileIds   [ MAX_RDB_FILES ];
	long      m_fileIds2  [ MAX_RDB_FILES ]; // for titledb/tfndb linking
	RdbMap   *m_maps      [ MAX_RDB_FILES ];
	long      m_numFiles;

	// this class contains a ptr to us
	class Rdb           *m_rdb;

	bool      m_dedup;
	long      m_fixedDataSize;

	Dir       m_dir;
	char      m_dbname [32];
	long      m_dbnameLen;

	char      *m_coll;
	collnum_t  m_collnum;

	//RdbCache  m_cache;
	// for storing records in memory
	RdbTree    *m_tree;  
	RdbBuckets *m_buckets;  
	// for dumping a table to an rdb file
	RdbDump    *m_dump;  
	// memory for us to use to avoid calling malloc()/mdup()/...
	//RdbMem    m_mem;

	// . this is now static in Rdb.cpp
	// . for merging many rdb files into one 
	// . no we brought it back so tfndb can merge while titledb is merging
	//RdbMerge  m_merge; 

	//BigFile   m_saveFile; // for saving the tree
	//bool      m_isClosing; 
	//bool      m_isClosed;
	//bool      m_haveSavedFile; // we only unlink this file when we dump

	// this callback called when close is complete
	//void     *m_closeState; 
	//void    (* m_closeCallback) (void *state );

	long      m_maxTreeMem ; // max mem tree can use, dump at 90% of this

	long      m_minToMergeArg;
	long      m_minToMerge;  // need at least this many files b4 merging
	long      m_absMaxFiles;
	long      m_numFilesToMerge   ;
	long      m_mergeStartFileNum ;

	// a dummy data string for deleting records when m_fixedDataSize > 0
	//char     *m_dummy;
	//long      m_dummySize ; // size of that dummy data
	//long      m_delRecSize; // size of the whole delete record

	/*
	// for keeping stats
	long long m_numSeeks;
	long long m_numRead;
	// network request/reply info for get requests
	long long m_numReqsGet    ;
	long long m_numNetReadGet ;
	long long m_numRepliesGet ; 
	long long m_numNetSentGet ;
	// network request/reply info for add requests
	long long m_numReqsAdd    ;
	long long m_numNetReadAdd ;
	long long m_numRepliesAdd ; 
	long long m_numNetSentAdd ;
	*/

	// should our next merge in waiting force itself?
	bool      m_nextMergeForced;

	// do we need to dump to disk?
	//bool      m_needsSave;

	// . when we dump list to an rdb file, can we use short keys?
	// . currently exclusively used by indexdb
	bool      m_useHalfKeys;

	// key size
	char      m_ks;
	
	long      m_pageSize;

	// are we waiting on another merge/dump to complete before our turn?
	bool      m_inWaiting;

	// . is our merge urgent? (if so, it will starve spider disk reads)
	// . also see Threads.cpp for the starvation
	bool      m_mergeUrgent;

	// are we saving the tree urgently? like we cored...
	//bool      m_urgent;
	// after saving the tree in call to Rdb::close() should the tree
	// remain closed to writes?
	//bool      m_isReallyClosing;

	bool      m_niceness;

	//bool      m_waitingForTokenForDump ;
	bool      m_waitingForTokenForMerge;

	// we now determine when in merge mode
	bool      m_isMerging;

	// have we create the merge file?
	bool      m_hasMergeFile;

	// rec counts for files being merged
	long long m_numPos ;
	long long m_numNeg ;

	// so only one save thread launches at a time
	//bool m_isSaving;

	class DiskPageCache *m_pc;

	bool m_isTitledb;

	long m_numThreads;

	bool m_isUnlinking;
	
	// filename of merge file for passing to g_sync to unlink it from there
	char m_oldname [ 256 ];

	//BigFile m_dummyFile;

	long long m_lastWrite;

	char m_doLog;
};

extern long g_numThreads;

extern char g_dumpMode;

#endif
