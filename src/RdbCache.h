// . Matt Wells, copyright Feb 2001

// . we use a big buffer, m_buf, into which we sequentially store records as 
//   they are added to the cache
// . each record we store in the big buffer has a header which consists
//   of a key_t, recordSize(int32_t), timestamp and the record data
// . the header is as follows:
//   a collnum_t (use sizeof(collnum_t)) that identifies the collection
//   a 12 byte key_t (actually, it is now m_cks bytes)
//   a 4 bytes timestamp (seconds since the epoch, like time_t)
//   a 4 bytes data size
//   the data
// . we have a hash table that maps a key_t to a ptr to a record header in the
//   big buffer
// . what if we run out of room in the hash table? we delete the oldest
//   records in the big buffer so we can remove their ptrs from the hash table
// . we keep an "tail" ptr into the hash table that point to the last
//   non-overwritten record in the big buffer
// . when we run out of room in the big buffer we wrap our ptr to the start
//   and we remove any records from the hashtable that we overwrite using
//   the tail ptr
// . when a record is read from the cache we also promote it by copying it to
//   the head of m_buf, m_bufOffset. since modern day pentiums do 2.5-6.4GB/s
//   this is ok, it will not be the bottleneck by far. if the record is
//   expired we do not promote it. The advantage is we don't have ANY memory
//   fragmentation and utilize 100% of the memory. Copying a 6 Megabyte
//   list takes like 2.4ms on a new pentium, so we should allow regular
//   allocating if the record size is 256k or more. Copying 256k only
//   takes .1 ms on the P4 2.60CGHz. This is on the TODO list.

#ifndef RDBCACHE_H
#define RDBCACHE_H

// . TODO:
// . if size of added rec is ABOVE this, then don't use our memory buffer
// . because we copy a rec to the head of memory buffer (m_bufOffset) every 
//   time that rec is accessed and doing that for big recs is more intensive
// . the idea is that allocating/freeing memory for smaller recs is what
//   causes the memory fragmentation
// . to stay under m_maxMem we should limit each memory buffer to 1M or so
//   and free the tailing memory buffer to make room for a large unbuffered rec
//#define MEM_LIMIT (256*1024)

#include <time.h>       // time_t
#include "Mem.h"        // g_mem.calloc()
#include "RdbList.h"

extern bool g_cacheWritesEnabled;

class RdbCache {

 public:

	friend class Rdb;

	// constructor & destructor
	 RdbCache();
	~RdbCache();

	void reset();

	// . this just clears the contents of the cache for a particular coll
	// . used by g_collectiondb.delRec() call to Rdb::delColl() to 
	//   clear out the collection's stuff in the cache
	void clear ( collnum_t collnum ) ;

	bool isInitialized () { 
		if ( m_ptrs ) return true; 
		return false;
	};

	// . we are allowed to keep a min mem of "minCacheSize"
	// . a fixedDataSize of -1 means the dataSize varies from rec to rec
	// . set "maxNumNodes" to -1 for it to be auto determined
	// . can only do this if fixedDataSize is not -1
	bool init ( int32_t maxCacheMem   , 
		    int32_t fixedDataSize , 
		    bool supportLists  ,
		    int32_t maxCacheNodes ,
		    bool useHalfKeys   ,
		    char *dbname       ,
		    //bool  loadFromDisk );
		    bool  loadFromDisk ,
		    char  cacheKeySize = 12 ,
		    char  dataKeySize  = 12 ,
		    int32_t  numPtrsMax   = -1 );

	// . a quick hack for SpiderCache.cpp
	// . if your record is always a 4 byte int32_t call this
	// . returns -1 if not found, so don't store -1 in there then
	int64_t getLongLong ( collnum_t collnum ,
				uint32_t key , int32_t maxAge , // in seconds
				bool promoteRecord );

	// this puts a int32_t in there
        void addLongLong ( collnum_t collnum ,
			   uint32_t key , int64_t value ,
			   char **retRecPtr = NULL ) ;

	// . both key and data are int64_ts here
	// . returns -1 if not found
	int64_t getLongLong2 ( collnum_t collnum ,
				 uint64_t key , 
				 int32_t maxAge , // in seconds
				 bool promoteRecord );

	// this puts a int32_t in there
        void addLongLong2 ( collnum_t collnum ,
			   uint64_t key , int64_t value ,
			   char **retRecPtr = NULL ) ;

	// same routines for int32_ts now, but key is a int64_t
	int32_t getLong ( collnum_t collnum ,
		       uint64_t key , int32_t maxAge , // in seconds
		       bool promoteRecord );
        void addLong ( collnum_t collnum ,
		       uint64_t key , int32_t value ,
		       char **retRecPtr = NULL ) ;

	// . returns true if found, false if not found in cache
	// . sets *rec and *recSize iff found
	// . sets *cachedTime to time the rec was cached
	// . use maxAge of -1 to have no limit to the age of cached rec
	bool getRecord ( collnum_t collnum   ,
			 //key_t    cacheKey   ,
			 char    *cacheKey   ,
			 char   **rec        ,
			 int32_t    *recSize    ,
			 bool     doCopy     ,
			 int32_t     maxAge     , // in seconds
			 bool     incCounts  ,
			 time_t  *cachedTime = NULL ,
			 bool     promoteRecord = true );

	bool getRecord ( char    *coll       ,
			 //key_t    cacheKey   ,
			 char    *cacheKey   ,
			 char   **rec        ,
			 int32_t    *recSize    ,
			 bool     doCopy     ,
			 int32_t     maxAge     , // in seconds
			 bool     incCounts  ,
			 time_t  *cachedTime = NULL ,
			 bool     promoteRecord = true);

	bool getRecord ( collnum_t collnum   ,
			 key_t    cacheKey   ,
			 char   **rec        ,
			 int32_t    *recSize    ,
			 bool     doCopy     ,
			 int32_t     maxAge     , // in seconds
			 bool     incCounts  ,
			 time_t  *cachedTime = NULL,
			 bool     promoteRecord = true) {
		return getRecord (collnum,(char *)&cacheKey,rec,recSize,doCopy,
				  maxAge,incCounts,cachedTime, promoteRecord);
	};

	bool getRecord ( char    *coll       ,
			 key_t    cacheKey   ,
			 char   **rec        ,
			 int32_t    *recSize    ,
			 bool     doCopy     ,
			 int32_t     maxAge     , // in seconds
			 bool     incCounts  ,
			 time_t  *cachedTime = NULL,
			 bool     promoteRecord = true) {
		return getRecord (coll,(char *)&cacheKey,rec,recSize,doCopy,
				  maxAge,incCounts,cachedTime, promoteRecord);
	};

	bool setTimeStamp ( collnum_t  collnum      ,
			    key_t      cacheKey     ,
			    int32_t       newTimeStamp ) {
		return setTimeStamp ( collnum           ,
				      (char *)&cacheKey ,
				      newTimeStamp      );
	};

	bool setTimeStamp ( collnum_t  collnum      ,
			    char      *cacheKey     ,
			    int32_t       newTimeStamp );

	// . returns true if found, false if not found
	// . sets errno no error
	// . if "copyRecords" is true then COPIES into a new buffer
	// . maxAge constraint for ignoring the stale nodes
	// . promotes the returned list to the head of the linked list
	// . maxAge of -1 means no maxAge
	// . maxAge of  0 means do not check the cache
	// . uses "startKey" to get the list
	// . if "incCounts" is true and we hit  we inc the hit  count
	// . if "incCounts" is true and we miss we inc the miss count
	bool getList ( collnum_t collnum  ,
		       //key_t    cacheKey  ,
		       //key_t    startKey  ,
		       char    *cacheKey  ,
		       char    *startKey  ,
		       RdbList *list      ,
		       bool     doCopy    ,
		       int32_t     maxAge    , // in seconds
		       bool     incCounts );

	// use this key for cache lookup of the list rather than form from 
	// startKey/endKey
	bool addList ( collnum_t collnum , char *cacheKey , RdbList *list );
	bool addList ( collnum_t collnum , key_t cacheKey , RdbList *list ) {
		return addList(collnum,(char *)&cacheKey,list); };

	bool addList ( char *coll , char *cacheKey , RdbList *list );
	bool addList ( char *coll , key_t cacheKey , RdbList *list ) {
		return addList(coll,(char *)&cacheKey,list); };

	// . add a list of only 1 record
	// . return false on error and set g_errno, otherwise return true
	// . recOffset is proper offset into the buffer system
	bool addRecord ( collnum_t collnum ,
			 //key_t cacheKey , 
			 char *cacheKey , 
			 char *rec      ,
			 int32_t  recSize  ,
			 int32_t  timestamp = 0 ,
			 char **retRecPtr = NULL ) ;

	bool addRecord ( char *coll     ,
			 //key_t cacheKey , 
			 char *cacheKey , 
			 char *rec      ,
			 int32_t  recSize  ,
			 int32_t  timestamp = 0 );

	bool addRecord ( collnum_t collnum ,
			 key_t cacheKey , 
			 char *rec      ,
			 int32_t  recSize  ,
			 int32_t  timestamp = 0 ) {
		return addRecord(collnum,(char *)&cacheKey,rec,recSize,
				 timestamp); };

	bool addRecord ( char *coll     ,
			 key_t cacheKey , 
			 char *rec      ,
			 int32_t  recSize  ,
			 int32_t  timestamp = 0 ) {
		return addRecord(coll,(char *)&cacheKey,rec,recSize,
				 timestamp); };

	void verify();

	// . just checks to see if a record is in the cache
	// . does not promote record
	// . used by Msg34.cpp for disk load balancing
	bool isInCache ( collnum_t collnum , char *cacheKey , int32_t maxAge );
	bool isInCache ( collnum_t collnum , key_t cacheKey , int32_t maxAge ) {
		return isInCache(collnum,(char *)&cacheKey,maxAge);};

	// these include our mem AND our tree's mem combined
	int32_t getMemOccupied () {
		return m_memOccupied ; };
	int32_t getMemAlloced  () {
		return m_memAlloced ;  };
	//int32_t getRecOverhead () {
	//	return 3*4 + m_tree.m_overhead; };
	int32_t getMaxMem      () { return m_maxMem; };

	//int32_t getBaseMem     () {
	//	return m_baseMem         + m_tree.m_baseMem; };
	// cache stats
	int64_t getNumHits   () { return m_numHits;   };
	int64_t getNumMisses () { return m_numMisses; };
	int64_t getHitBytes  () { return m_hitBytes; };
	int32_t getNumUsedNodes  () { return m_numPtrsUsed; };
	int32_t getNumTotalNodes () { return m_numPtrsMax ; };

	bool useDisk ( ) { return m_useDisk; };
	bool load ( char *dbname );
	bool save ( bool useThreads );
	bool save_r ( );
	bool save2_r ( int fd );
	void threadDone ( );
	bool load   ( );
	int32_t m_saveError;

	// called internally by save()
	bool saveSome_r ( int fd, int32_t *iptr , int32_t *off ) ;

	// remove a key range from the cache
	void removeKeyRange ( collnum_t collnum,
			      char *startKey,
			      char *endKey );

	char *getDbname () { return m_dbname ; };

	char *m_dbname;

	// private:

	bool addRecord ( collnum_t collnum ,
			 //key_t cacheKey , 
			 char *cacheKey , 
			 char *rec1     ,
			 int32_t  recSize1 ,
			 char *rec2     ,
			 int32_t  recSize2 ,
			 int32_t  timestamp ,
			 char **retRecPtr = NULL ) ;

	bool deleteRec ( );
	//void addKey     ( collnum_t collnum , key_t key , char *ptr ) ;
	//void removeKey  ( collnum_t collnum , key_t key , char *rec ) ;
	void addKey     ( collnum_t collnum , char *key , char *ptr ) ;
	void removeKey  ( collnum_t collnum , char *key , char *rec ) ;
	void markDeletedRecord(char *ptr);

	bool convertCache ( int32_t numPtrsMax , int32_t maxMem ) ;
	bool m_convert;
	int32_t m_convertNumPtrsMax;
	int32_t m_convertMaxMem;

	bool m_isSaving;

	// . mem stats -- just for arrays we contain -- not in tree
	// . memory that is allocated and in use, including dataSizes
	int32_t m_memOccupied;
	// total memory allocated including dataSizes of our records
	int32_t m_memAlloced;
	// allocated memory for m_next/m_prev/m_time arrays
	//int32_t m_baseMem;
	// don't let m_memAlloced exceed this
	int32_t  m_maxMem;

	// . data is stored in m_bufs, an array of buffers
	// . we may have to use multiple bufs because we cannot allocate more
	//   than 128 Megabytes without pthread_create() failing
	// . we can have up to 32 bufs of 128M each, that's 4 gigs, plenty
	char      *m_bufs     [32];
	int32_t       m_bufSizes [32]; // size of the alloc'd space
	int32_t       m_numBufs;
	int32_t  m_totalBufSize; // gbpwrite() assumes 32 bits
	int32_t       m_offset; // where next rec is stored
	int32_t       m_tail;   // next rec to delete

	// the hash table, buckets are ptrs into an m_bufs[i]
	char     **m_ptrs;
	int32_t       m_numPtrsMax;
	int32_t       m_numPtrsUsed;
	int32_t       m_threshold;
	// use this for testing to make sure cache doesn't fuck up the content
	//int32_t      *m_crcs;

	// cache hits and misses
	int64_t m_numHits; // includes partial hits & cached not-founds too
	//int64_t m_numPartialHits;
	int64_t m_numMisses;
	int64_t m_hitBytes;

	int32_t m_fixedDataSize;
	bool m_supportLists;
	bool m_useHalfKeys;
	bool m_useDisk;  // load/save from disk?

	// have we wrapped yet?
	int8_t m_wrapped;

	// keySize of cache keys in bytes
	char m_cks;

	// keysize of lists for addList() and getList()
	char m_dks;

	// count the add ops
	int64_t m_adds;
	int64_t m_deletes;

	char m_needsSave;

	char m_corruptionDetected;

	int64_t m_maxColls;
};	

#endif
