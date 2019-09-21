// Matt Wells, copyright Sep 2000

// contains one RdbBase for each collection

#ifndef _RDB_H_
#define _RDB_H_

#include "RdbBase.h"
#include "RdbTree.h"
#include "RdbMem.h"
#include "RdbCache.h"
#include "RdbDump.h"
//#include "Dir.h"
#include "RdbBuckets.h"

bool makeTrashDir() ;

void removeFromMergeLinkedList ( class CollectionRec *cr ) ;
void addCollnumToLinkedListOfMergeCandidates ( collnum_t dumpCollnum ) ;

// . each Rdb instance has an ID
// . these ids are also return values for getIdFromRdb()
#define	RDB_START 1
enum {
	RDB_NONE = 0, //       0
	RDB_TAGDB,
	RDB_INDEXDB,
	RDB_TITLEDB,
	RDB_SECTIONDB,
	RDB_SYNCDB , // 5
	RDB_SPIDERDB,
	RDB_DOLEDB,
	RDB_TFNDB,
	RDB_CLUSTERDB,
	RDB_CATDB, // 10
	RDB_DATEDB,
	RDB_LINKDB,
	RDB_STATSDB,
	RDB_PLACEDB,
	RDB_REVDB, // 15
	RDB_POSDB, // 16
	RDB_CACHEDB, // 17
	RDB_SERPDB, // 18
	RDB_MONITORDB, // 19
	RDB_PARMDB, // kind of a fake rdb for modifying collrec/g_conf parms
	//RDB_FAKEDB, // used by spider.cpp to fake things out
// . secondary rdbs for rebuilding done in PageRepair.cpp
// . we add new recs into these guys and then make the original rdbs
//   point to them when we are done.
	RDB2_INDEXDB2,//21
	RDB2_TITLEDB2, 
	RDB2_SECTIONDB2,
	RDB2_SPIDERDB2, 
	RDB2_TFNDB2,
	RDB2_CLUSTERDB2, // 26
	RDB2_DATEDB2,
	RDB2_LINKDB2,
	RDB2_PLACEDB2,
	RDB2_REVDB2,
	RDB2_TAGDB2, // 31
	RDB2_POSDB2,
	RDB2_CATDB2,
	RDB_END
};
// how many rdbs are in "urgent merge" mode?
extern int32_t g_numUrgentMerges;

// get the RdbBase class for an rdbId and collection name
class RdbBase *getRdbBase ( uint8_t rdbId , char *coll );
class RdbBase *getRdbBase ( uint8_t rdbId , collnum_t collnum );
// maps an rdbId to an Rdb
class Rdb *getRdbFromId ( uint8_t rdbId ) ;
// the reverse of the above
char getIdFromRdb ( class Rdb *rdb ) ;
char isSecondaryRdb ( uint8_t rdbId ) ;
// get the dbname
char *getDbnameFromId ( uint8_t rdbId ) ;
// get cache by rdbId. Used by MsgB.cpp.
//RdbCache *getCache ( uint8_t rdbId ) ;
// size of keys
char getKeySizeFromRdbId  ( uint8_t rdbId );
// and this is -1 if dataSize is variable
int32_t getDataSizeFromRdbId ( uint8_t rdbId );
void forceMergeAll ( char rdbId , char niceness ) ;
// main.cpp calls this
void attemptMergeAll ( int fd , void *state ) ;
void attemptMergeAll2 ( );

class Rdb {

 public:

	 Rdb ( );
	~Rdb ( );

	bool addRdbBase1 ( char *coll );
	bool addRdbBase2 ( collnum_t collnum );
	bool delColl ( char *coll );

	bool resetBase ( collnum_t collnum );
	bool deleteAllRecs ( collnum_t collnum ) ;
	bool deleteColl ( collnum_t collnum , collnum_t newCollnum ) ;

	bool init ( char  *dir             , // working directory
		    char  *dbname          , // "indexdb","tagdb",...
		    bool   dedup           , //= true ,
		    int32_t   fixedDataSize   , //= -1   ,
		    int32_t   minToMerge      , //, //=  2   ,
		    int32_t   maxTreeMem      , //=  1024*1024*32 ,
		    int32_t   maxTreeNodes    ,
		    bool   isTreeBalanced  ,
		    int32_t   maxCacheMem     , //=  1024*1024*5 );
		    int32_t   maxCacheNodes   ,
		    bool   useHalfKeys     ,
		    bool   loadCacheFromDisk ,
		    //class DiskPageCache *pc = NULL ,
		    void *pc = NULL,
		    bool   isTitledb    = false , // use fileIds2[]?
		    bool   preloadDiskPageCache = false ,
		    char   keySize = 12    ,
		    bool   biasDiskPageCache    = false ,
		    bool   isCollectionLess = false );
	// . frees up all the memory and closes all files
	// . suspends any current merge (saves state to disk)
	// . calls reset() for each file
	// . will cause any open map files to dump
	// . will dump tables to backup or store 
	// . calls close on each file
	// . returns false if blocked, true otherwise
	// . sets errno on error
	bool close ( void *state , 
		     void (* callback)(void *state ) ,
		     bool urgent ,
		     bool exitAfterClosing );
	//bool close ( ) { return close ( NULL , NULL ); };
	// used by PageMaster.cpp to check to see if all rdb's are closed yet
	bool isClosed ( ) { return m_isClosed; };
	bool needsSave();

	// . returns false and sets g_errno on error
	// . caller should retry later on g_errno of ENOMEM or ETRYAGAIN
	// . returns the node # in the tree it added the record to
	// . key low bit must be set (otherwise it indicates a delete)
	bool addRecord ( collnum_t collnum , 
			 //key_t &key, char *data, int32_t dataSize );
			 char *key, char *data, int32_t dataSize,
			 int32_t niceness);
	bool addRecord ( char *coll , char *key, char *data, int32_t dataSize,
			 int32_t niceness);
	bool addRecord (char *coll , key_t &key, char *data, int32_t dataSize,
			int32_t niceness) {
		return addRecord(coll,(char *)&key,data,dataSize, niceness);};

	// returns false if no room in tree or m_mem for a list to add
	bool hasRoom ( RdbList *list , int32_t niceness );

	int32_t reclaimMemFromDeletedTreeNodes( int32_t niceness ) ;
	int32_t m_lastReclaim;

	// . returns false on error and sets errno
	// . return true on success
	// . if we can't handle all records in list we don't add any and
	//   set errno to ETRYAGAIN or ENOMEM
	// . we copy all data so you can free your list when we're done
	bool addList ( collnum_t collnum , RdbList *list, int32_t niceness );

	// calls addList above
	bool addList ( char *coll , RdbList *list, int32_t niceness );

	// . add a record without any data, just a key (faster)
	// . returns the node # in the tree it added the record to
	//int32_t addKey ( collnum_t collnum , key_t &key );
	int32_t addKey ( collnum_t collnum , char *key );

	// . uses the bogus data pointed to by "m_dummy" for record's data
	// . we clear the key low bit to signal a delete
	// . returns false and sets errno on error
	//bool deleteRecord ( collnum_t collnum , key_t &key ) ;
	bool deleteRecord ( collnum_t collnum , char *key );

	bool isSecondaryRdb () {
		return ::isSecondaryRdb((unsigned char)m_rdbId); };
	
	bool isInitialized () { return m_initialized; };

	// get the directory name where this rdb stores it's files
	//char *getDir       ( ) { return m_dir.getDirname(); };
	char *getDir       ( ) { return g_hostdb.m_dir; };
	char *getStripeDir ( ) { return g_conf.m_stripeDir; };

	int32_t getFixedDataSize ( ) { return m_fixedDataSize; };

	bool useHalfKeys ( ) { return m_useHalfKeys; };
	char getKeySize  ( ) { return m_ks; };

	RdbTree    *getTree    ( ) { if(!m_useTree) return NULL; return &m_tree; };
	//RdbCache   *getCache   ( ) { return &m_cache; };
	RdbMem     *getRdbMem  ( ) { return &m_mem; };
	bool       useTree     ( ) { return m_useTree;};

	int32_t       getNumUsedNodes ( );
	int32_t       getMaxTreeMem();
	int32_t       getTreeMemOccupied() ;
	int32_t       getTreeMemAlloced () ;
	int32_t       getNumNegativeKeys();
	
	void disableWrites ();
	void enableWrites  ();
	bool isWritable ( ) ;

	RdbBase *getBase ( collnum_t collnum ) ;
	int32_t getNumBases ( ) { 	return g_collectiondb.m_numRecs; };
	void addBase ( collnum_t collnum , class RdbBase *base ) ;


	// how much mem is alloced for our maps?
	int64_t getMapMemAlloced ();

	int32_t       getNumFiles ( ) ;

	// sum of all parts of all big files
	int32_t      getNumSmallFiles ( ) ;
	int64_t getDiskSpaceUsed ( );

	// returns -1 if variable (variable dataSize)
	int32_t getRecSize ( ) {
		if ( m_fixedDataSize == -1 ) return -1;
		//return sizeof(key_t) + m_fixedDataSize; };
		return m_ks + m_fixedDataSize; };

	// use the maps and tree to estimate the size of this list
	int64_t getListSize ( collnum_t collnum,
			   //key_t startKey ,key_t endKey , key_t *maxKey ,
			   char *startKey ,char *endKey , char *maxKey ,
			   int64_t oldTruncationLimit ) ;

	int64_t getListSize ( collnum_t collnum,
			   key_t startKey ,key_t endKey , key_t *maxKey ,
			   int64_t oldTruncationLimit ) {
		return getListSize(collnum,(char *)&startKey,(char *)&endKey,
				   (char *)maxKey,oldTruncationLimit);};

	// positive minus negative
	int64_t getNumTotalRecs ( bool useCache = false ) ;

	int64_t getCollNumTotalRecs ( collnum_t collnum );

	int64_t getNumRecsOnDisk ( );

	int64_t getNumGlobalRecs ( );

	// used for keeping track of stats
	void      didSeek       (            ) { m_numSeeks++; };
	void      didRead       ( int32_t bytes ) { m_numRead += bytes; };
	void      didReSeek     (            ) { m_numReSeeks++; };
	int64_t getNumSeeks   (            ) { return m_numSeeks; };
	int64_t getNumReSeeks (            ) { return m_numReSeeks; };
	int64_t getNumRead    (            ) { return m_numRead ; };

	// net stats for "get" requests
	void      readRequestGet ( int32_t bytes ) { 
		m_numReqsGet++    ; m_numNetReadGet += bytes; };
	void      sentReplyGet     ( int32_t bytes ) {
		m_numRepliesGet++ ; m_numNetSentGet += bytes; };
	int64_t getNumRequestsGet ( ) { return m_numReqsGet;    };
	int64_t getNetReadGet     ( ) { return m_numNetReadGet; };
	int64_t getNumRepliesGet  ( ) { return m_numRepliesGet; };
	int64_t getNetSentGet     ( ) { return m_numNetSentGet; };

	// net stats for "add" requests
	void      readRequestAdd ( int32_t bytes ) { 
		m_numReqsAdd++    ; m_numNetReadAdd += bytes; };
	void      sentReplyAdd     ( int32_t bytes ) {
		m_numRepliesAdd++ ; m_numNetSentAdd += bytes; };
	int64_t getNumRequestsAdd ( ) { return m_numReqsAdd;    };
	int64_t getNetReadAdd     ( ) { return m_numNetReadAdd; };
	int64_t getNumRepliesAdd  ( ) { return m_numRepliesAdd; };
	int64_t getNetSentAdd     ( ) { return m_numNetSentAdd; };

	// used by main.cpp to periodically save us if we haven't dumped
	// in a while
	int64_t getLastWriteTime   ( ) { return m_lastWrite; };
	
	// private:

	//void attemptMerge ( int32_t niceness , bool forceMergeAll ,
	//		    bool doLog = true );

	bool gotTokenForDump  ( ) ;
	//void gotTokenForMerge ( ) ;

	// called after merge completed
	//bool incorporateMerge ( );

	// . you'll lose your data in this class if you call this
	void reset();

	bool isSavingTree ( ) ;

	bool saveTree  ( bool useThread ) ;
	bool saveMaps  ( bool useThread ) ;
	//bool saveCache ( bool useThread ) ;

	// . load the tree named "saved.dat", keys must be out of order because
	//   tree is not balanced
	bool loadTree ( ) ;
	bool treeFileExists ( ) ;

	// . write out tree to a file with keys in order
	// . only shift.cpp/reindex.cpp programs set niceness to 0
	bool dumpTree ( int32_t niceness ); //= MAX_NICENESS );

	// . called when done saving a tree to disk (keys not ordered)
	void doneSaving ( ) ;

	bool dumpCollLoop ( ) ;

	// . called when we've dumped the tree to disk w/ keys ordered
	void doneDumping ( );

	bool needsDump ( );

	// these are used by Msg34 class for computing load on a machine
	bool isMerging ( ) ;
	bool isDumping ( ) { return m_dump.isDumping(); };

	// PageRepair.cpp calls this when it is done rebuilding an rdb
	// and wants to tell the primary rdb to reload itself using the newly
	// rebuilt files, pointed to by rdb2.
	bool updateToRebuildFiles ( Rdb *rdb2 , char *coll ) ;

	//bool hasMergeFile ( ) { return m_hasMergeFile; };

	// used for translating titledb file # 255 (as read from new tfndb)
	// into the real file number
	//int32_t getNewestFileNum ( ) { return m_numFiles - 1; };

	// Msg22 needs the merge info so if the title file # of a read we are
	// doing is being merged, we have to include the start merge file num
	//int32_t      getMergeStartFileNum ( ) { return m_mergeStartFileNum; };
	//int32_t      getMergeNumFiles     ( ) { return m_numFilesToMerge; };

	// used by Sync.cpp to convert a file name to a file number in m_files
	//int32_t getFileNumFromName ( char *filename );

	//void doneWrapper2 ( ) ;
	//void doneWrapper4 ( ) ;
	//int32_t m_x;
	//int32_t m_a;

	// keep a copy of these here so merge can use them to kick out
	// records whose key when, ANDed w/ m_groupMask, equals
	// m_groupId
	//uint32_t  m_groupMask;
	//uint32_t  m_groupId;

	// . we try to minimize the number of files to minimize disk seeks
	// . records that end up as not found will hit all these files
	// . when we get "m_minToMerge" or more files a merge kicks in
	// . TODO: merge should combine just the smaller files... kinda
	// . files are sorted by fileId
	// . older files are listed first (lower fileIds)
	// . filenames should include the directory (full filenames)
	// . TODO: RdbMgr should control what rdb gets merged?
	//BigFile  *m_files     [ MAX_RDB_FILES ];
	//int32_t      m_fileIds   [ MAX_RDB_FILES ];
	//int32_t      m_fileIds2  [ MAX_RDB_FILES ]; // for titledb/tfndb linking
	//RdbMap   *m_maps      [ MAX_RDB_FILES ];
	//int32_t      m_numFiles;

	// just put this into CollectionRec so we are not limited to MAX_COLLS
	//class RdbBase *m_bases [ MAX_COLLS ];
	//int32_t       m_numBases;

	bool      m_dedup;
	int32_t      m_fixedDataSize;

	//Dir       m_dir;
	char      m_dbname [32];
	int32_t      m_dbnameLen;

	bool      m_isCollectionLess;
	// for g_cachedb, g_statsdb, etc.
	RdbBase *m_collectionlessBase;

	//RdbCache  m_cache;
	// for storing records in memory
	RdbTree    m_tree;  
	RdbBuckets m_buckets;
	bool       m_useTree;
	// for dumping a table to an rdb file
	RdbDump   m_dump;  
	// memory for us to use to avoid calling malloc()/mdup()/...
	RdbMem    m_mem;

	int32_t      m_cacheLastTime;
	int64_t m_cacheLastTotal;

	bool m_inAddList;

	int32_t m_numMergesOut;

	// . this is now static in Rdb.cpp
	// . for merging many rdb files into one 
	// . no we brought it back so tfndb can merge while titledb is merging
	//RdbMerge  m_merge; 

	BigFile   m_saveFile; // for saving the tree
	bool      m_isClosing; 
	bool      m_isClosed;
	bool      m_haveSavedFile; // we only unlink this file when we dump
	bool      m_preloadCache;
	bool      m_biasDiskPageCache;

	// this callback called when close is complete
	void     *m_closeState; 
	void    (* m_closeCallback) (void *state );

	int32_t      m_maxTreeMem ; // max mem tree can use, dump at 90% of this

	int32_t      m_minToMerge;  // need at least this many files b4 merging
	int32_t      m_numFilesToMerge   ;
	int32_t      m_mergeStartFileNum ;

	int32_t m_dumpErrno;

	// a dummy data string for deleting records when m_fixedDataSize > 0
	char     *m_dummy;
	int32_t      m_dummySize ; // size of that dummy data
	int32_t      m_delRecSize; // size of the whole delete record

	// for keeping stats
	int64_t m_numSeeks;
	int64_t m_numReSeeks;
	int64_t m_numRead;
	// network request/reply info for get requests
	int64_t m_numReqsGet    ;
	int64_t m_numNetReadGet ;
	int64_t m_numRepliesGet ; 
	int64_t m_numNetSentGet ;
	// network request/reply info for add requests
	int64_t m_numReqsAdd    ;
	int64_t m_numNetReadAdd ;
	int64_t m_numRepliesAdd ; 
	int64_t m_numNetSentAdd ;

	// should our next merge in waiting force itself?
	bool      m_nextMergeForced;

	// do we need to dump to disk?
	//bool      m_needsSave;

	// . when we dump list to an rdb file, can we use int16_t keys?
	// . currently exclusively used by indexdb
	bool      m_useHalfKeys;

	// are we waiting on another merge/dump to complete before our turn?
	bool      m_inWaiting;

	// . is our merge urgent? (if so, it will starve spider disk reads)
	// . also see Threads.cpp for the starvation
	// . this is now exclusively in RdbBase.h
	//bool      m_mergeUrgent;

	// are we saving the tree urgently? like we cored...
	bool      m_urgent;
	// after saving the tree in call to Rdb::close() should the tree
	// remain closed to writes?
	bool      m_isReallyClosing;

	bool      m_niceness;

	//bool      m_waitingForTokenForDump ;
	//bool      m_waitingForTokenForMerge;

	// we now determine when in merge mode
	//bool      m_isMerging;

	// have we create the merge file?
	//bool      m_hasMergeFile;

	// rec counts for files being merged
	//int64_t m_numPos ;
	//int64_t m_numNeg ;

	// so only one save thread launches at a time
	bool m_isSaving;

	//class DiskPageCache *m_pc;

	bool m_isTitledb;

	bool m_isUnlinking;

	int32_t  m_fn;
	
	// filename of merge file for passing to g_sync to unlink it from there
	char m_oldname [ 256 ];

	char m_treeName [64];
	char m_memName  [64];

	BigFile m_dummyFile;

	int64_t m_lastWrite;

	collnum_t m_dumpCollnum;

	char      m_registered;
	int64_t m_lastTime;

	// set to true when dumping tree so RdbMem does not use the memory
	// being dumped to hold newly added records
	char m_inDumpLoop;

	char m_rdbId;
	char m_ks; // key size
	int32_t m_pageSize;

	bool m_initialized;

	int8_t m_gbcounteventsTermId[8];

	// timedb support
	time_t            m_nowGlobal;
	class HashTableX *m_sortByDateTablePtr;

	// used for deduping spiderdb tree
	Msg5 m_msg5;
};

//extern RdbCache g_forcedCache;
//extern RdbCache g_alreadyAddedCache;

#endif
