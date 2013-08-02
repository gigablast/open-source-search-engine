// Matt Wells, copyright Sep 2001

// . get a lists from tree, cache and disk

#ifndef _MSG5_H_
#define _MSG5_H_

#include "Msg3.h"
#include "RdbList.h"
#include "HashTableX.h"

extern long g_numCorrupt;

extern HashTableX g_waitingTable;

extern long g_isDumpingRdbFromMain;

// this is used internally and by Msg0 for generating a cacheKey
// to see if its request can be satisfied by any remote host without
// hitting disk. Multicast uses Msg34 to ask each host that.
//key_t makeCacheKey ( key_t startKey     ,
//		     key_t endKey       ,
void  makeCacheKey ( char *startKey     ,
		     char *endKey       ,
		     bool  includeTree  ,
		     long  minRecSizes  ,
		     long  startFileNum ,
		     long  numFiles     ,
		     char *cacheKeyPtr  ,
		     char  keySize      ) ;

inline key_t makeCacheKey ( key_t startKey     ,
		     key_t endKey       ,
		     bool  includeTree  ,
		     long  minRecSizes  ,
		     long  startFileNum ,
		     long  numFiles     ) {
	key_t k;
	makeCacheKey ( (char *)&startKey ,
		       (char *)&endKey   ,
		       includeTree ,
		       minRecSizes ,
		       startFileNum ,
		       numFiles ,
		       (char *)&k ,
		       sizeof(key_t) );
	return k;
}

class Msg5 { 

 public:

	Msg5();

	~Msg5();

	// . set "list" with the asked for records
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . RdbList will be populated with the records you want
	// . we pass ourselves back to callback since requestHandler()s of
	//   various msg classes often do not have any context/state, and they
	//   construct us in a C wrapper so they need to delete us when 
	//   they're done with us
	// . if maxCacheAge is > 0, we lookup in cache first
	bool getList ( //class RdbBase *base      ,
		       char       rdbId         ,
		       char      *coll          ,
		       RdbList   *list          ,
		       //key_t      startKey      , 
		       //key_t      endKey        , 
		       void      *startKey      , 
		       void      *endKey        , 
		       long       recSizes      , // requestd scan size(-1 all)
		       bool       includeTree   ,
		       bool       addToCache    ,
		       long       maxCacheAge   , // in secs for cache lookup
		       long       startFileNum  , // first file to scan
		       long       numFiles      , // rel.to startFileNum,-1 all
		       void      *state         , // for callback
		       void     (* callback ) ( void    *state ,
						RdbList *list  ,
						Msg5    *msg5  ) ,
		       long       niceness      ,
		       bool       doErrorCorrection  ,
		       //key_t     *cacheKeyPtr  = NULL ,
		       char      *cacheKeyPtr  = NULL ,
		       long       retryNum     =  0  ,
		       long       maxRetries   = -1  ,
		       bool       compensateForMerge = true ,
		       long long      syncPoint = -1 ,
		       class Msg5 *msg5b = NULL ,
		       bool        isRealMerge = false ,
		       bool        allowPageCache = true ,
		       bool        hitDisk        = true ,
		       // if this is false we just return all lists unmerged
		       bool        mergeLists     = true );

	bool getList ( //class RdbBase *base      ,
		       char       rdbId         ,
		       char      *coll          ,
		       RdbList   *list          ,
		       key_t      startKey      , 
		       key_t      endKey        , 
		       long       recSizes      , // requestd scan size(-1 all)
		       bool       includeTree   ,
		       bool       addToCache    ,
		       long       maxCacheAge   , // in secs for cache lookup
		       long       startFileNum  , // first file to scan
		       long       numFiles      , // rel.to startFileNum,-1 all
		       void      *state         , // for callback
		       void     (* callback ) ( void    *state ,
						RdbList *list  ,
						Msg5    *msg5  ) ,
		       long       niceness      ,
		       bool       doErrorCorrection  ,
		       key_t     *cacheKeyPtr  = NULL ,
		       long       retryNum     =  0  ,
		       long       maxRetries   = -1  ,
		       bool       compensateForMerge = true ,
		       long long      syncPoint = -1 ,
		       class Msg5 *msg5b = NULL ,
		       bool        isRealMerge = false ,
		       bool        allowPageCache = true ,
		       bool        hitDisk        = true ) {
		return getList ( rdbId         ,
				 coll          ,
				 list          ,
				 (char *)&startKey      , 
				 (char *)&endKey        , 
				 recSizes      , 
				 includeTree   ,
				 addToCache    ,
				 maxCacheAge   , 
				 startFileNum  , 
				 numFiles      , 
				 state         , 
				 callback      ,
				 niceness      ,
				 doErrorCorrection  ,
				 (char *)cacheKeyPtr   ,
				 retryNum      ,
				 maxRetries    ,
				 compensateForMerge ,
				 syncPoint     ,
				 msg5b         ,
				 isRealMerge   ,
				 allowPageCache ,
				 hitDisk        ); };

	// need niceness to pass on to others
	long getNiceness ( ) { return m_niceness; };

	// frees m_treeList, m_diskList (can be quite a lot of mem 2+ megs)
	void reset();

	// called to read lists from disk using Msg3
	bool readList();

	// keep public for doneScanningWrapper to call
	bool gotList();
	bool gotList2();

	void copyAndSendBackList ( class RdbList *list );

	// does readList() need to be called again due to negative rec
	// annihilation?
	bool needsRecall();

	void repairLists_r ();
	void mergeLists_r  ();
	bool doneMerging   ();

	// how many dup keys removed from the IndexList merge, if any?
	long getDupsRemoved ( ) { return m_dupsRemoved; };

	// . when a list is bad we try to patch it by getting a list from
	//   a host in our redundancy group
	// . we also get a local list from ALL files and tree to remove
	//   recs we already have from the remote list
	bool getRemoteList  ( );
	bool gotRemoteList  ( );

	// we add our m_finalList(s) to this, the user's list
	RdbList  *m_list;

	// used for testing integrity of the big root list merge algo
	RdbList   m_list2;

	// hold the caller of getList()'s callback here
	void    (* m_callback )( void *state , RdbList *list , Msg5 *msg );
	void    *m_state       ;
	char     m_calledCallback;

	// private:

	// holds all RdbLists from disk
	Msg3      m_msg3;

	// holds list from tree
	RdbList   m_treeList;

	RdbList   m_dummy;

	// holds list parms
	//key_t     m_startKey;
	//key_t     m_endKey;
	char      m_startKey[MAX_KEY_BYTES];
	char      m_endKey[MAX_KEY_BYTES];
	bool      m_includeTree;
	bool      m_addToCache;
	long      m_maxCacheAge;

	long      m_numFiles;
	long      m_startFileNum;
	long      m_minRecSizes;
	//RdbBase  *m_base;
	char     *m_coll;
	char      m_rdbId;

	// . cache may modify these
	// . gotLists() may modify these before reading more
	//key_t     m_fileStartKey;
	char      m_fileStartKey[MAX_KEY_BYTES];
	long      m_newMinRecSizes;
	long      m_round;
	long      m_totalSize;
	long      m_treeMinRecSizes;
	bool      m_readAbsolutelyNothing;

	long      m_niceness;
	// error correction stuff
	bool      m_doErrorCorrection;
	bool      m_hadCorruption;
	//long long m_checkTime;
	class Msg0 *m_msg0;
	
	// for timing debug
	long long m_startTime;

	// hold pointers to lists to merge
	RdbList  *m_listPtrs [ MAX_RDB_FILES + 1 ]; // plus tree
	//long      m_tfns     [ MAX_RDB_FILES + 1 ]; // plus tree
	long      m_numListPtrs;

	//RdbList   m_tfndbList;

	// cache ptr
	//class RdbCache *m_cache;
	// key for hitting the cache
	//key_t           m_cacheKey;
	char           m_cacheKey[MAX_KEY_BYTES];

	bool m_removeNegRecs;

	//key_t m_minEndKey;
	char m_minEndKey[MAX_KEY_BYTES];

	// info for truncating and passing to RdbList::indexMerge_r()
	//key_t m_prevKey;
	char  m_prevKey[MAX_KEY_BYTES];
	long  m_prevCount;

	long  m_oldListSize;
	
	// how many dup keys removed from the IndexList merge, if any?
	long  m_dupsRemoved;

	bool m_compensateForMerge;

	long m_maxRetries;

	//long long m_syncPoint;

	long m_filtered;

	// used for reading a corresponding tfndb list for a titledb read
	class Msg5 *m_msg5b;
	bool        m_isRealMerge;
	long long   m_time1;

	long m_indexdbTruncationLimit;

	char m_ks;

	// for allowing the page cache
	bool  m_allowPageCache;

	bool  m_hitDisk;

	bool  m_mergeLists;

	char m_waitingForList;
	
	// actually part of a different algo than m_waitingForList!
	unsigned long long m_waitingKey;

	// hack parms
	void *m_parent;
	long  m_i;
};

#endif
