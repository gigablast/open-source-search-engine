// Zak Betz,  Copyright, Jun 2008

// A sorted list of sorted lists for storing rdb keys.  Faster and more
// cache friendly than RdbTree.  Optimized for batched adds, amortized O(1)
// add operation, O(log n) for retrival, ranged getList for k keys is 
// O(log(n) + k) where as RdbTree is O(k * log(n)).
// Memory is alloced and used on a on demand basis rather than all up
// front as with RdbTree, so memory usage is much lower most of the time.

// Collections are handled as a linked list, each RdbBuckets has a nextColl
// pointer.  The front bucket acts as the gatekeeper for all of the
// other buckets, Only it's values for needsSave and isWritable are 
// significant

//when selecting bucketnum and also when deduping, use KEYCMPNEGEQ which
//will mask off the delbit, that way pos and neg keys will be in the same
//bucket and only the newest key will survive.  When getting or deleting
//a list, use KEYCMP within a bucket and use KEYCMPNEGEQ to select the 
//bucket nums.  This is because iterators in rdb dump get a list, then
//add 1 to a key and get the next list and adding 1 to a pos key will get
//the negative one.

#ifndef _RDBBUCKETS_H_
#define _RDBBUCKETS_H_

#include "Mem.h"          // for g_mem.calloc and g_mem.malloc
#include "BigFile.h"      // for saving and loading the tree
#include "RdbList.h"
#include "RdbMem.h"
#include "RdbTree.h"
class RdbBuckets;
class RdbBucket {
public:
	RdbBucket() {};
	~RdbBucket();
	bool set(RdbBuckets *parent, char *newbuf );
	void reset();
	void clear();
	void reBuf(char *newbuf);

	char *getFirstKey();
	char *getEndKey()  { return m_endKey;  }
	long  getNumKeys() { return m_numKeys; }
	char *getKeys()    { return m_keys; }
	collnum_t getCollnum()    { return m_collnum; }
	void  setCollnum(collnum_t c){ m_collnum = c; }

	bool  addKey(char *key , char *data , long dataSize);
	char *getKeyVal ( char *key , char **data , long* dataSize ); 
	long  getKeyNumExact(char* key); //returns -1 if not found
	long  getNumNegativeKeys ( );
	void  resetLastSorted() { m_lastSorted = 0; }
	bool  getList(RdbList* list, 
		      char *startKey, 
		      char *endKey,
		      long minRecSizes,
		      long *numPosRecs, 
		      long *numNegRecs, 
		      bool useHalfKeys);

	bool deleteList(RdbList *list);

	//Save State
	long long fastSave_r(int fd, long long offset);
	long long fastLoad(BigFile *f, long long offset);
	
	//Debug
	bool selfTest ( char *prevKey);
	void  printBucket();

	bool       sort();
	RdbBucket *split(RdbBucket *newBucket);

private:	

	char       *m_endKey;
	char       *m_keys;
	RdbBuckets *m_parent;
	long        m_numKeys;
	long        m_lastSorted;
	collnum_t   m_collnum;
};


class RdbBuckets {
 public:
	friend void *saveBucketsWrapper      ( void *state , ThreadEntry *t );
	friend void threadDoneBucketsWrapper ( void *state , ThreadEntry *t );


	RdbBuckets( );
	~RdbBuckets( );
	void clear();
	void reset();

	bool set ( long fixedDataSize , 
		   long maxMem, 
		   bool ownData ,
		   char *allocName ,
		   char rdbId ,
		   bool dataInPtrs ,//= false ,
		   char *dbname ,//= NULL , 
		   char keySize ,//= 12 ,
		   bool useProtection );//= false );

	bool resizeTable(long numNeeded);

	
	long addNode ( collnum_t collnum , 
		       char *key , char *data , long dataSize );
	
	bool addList(RdbList* list, collnum_t collnum);

	char* getKeyVal ( collnum_t collnum , char *key , 
			  char **data , long* dataSize ); 
	
	bool getList ( collnum_t collnum ,
		       char *startKey, char *endKey, long minRecSizes ,
		       RdbList *list , long *numPosRecs , long *numNegRecs ,
		       bool useHalfKeys );

	bool deleteList(collnum_t collnum, RdbList *list);

	long long getListSize ( collnum_t collnum,
				char *startKey, char *endKey, 
				char *minKey, char *maxKey );



	bool addBucket (RdbBucket *newBucket, long i);
	long getBucketNum(char *key, collnum_t collnum);
	char bucketCmp(char *akey, collnum_t acoll, char *bkey, collnum_t bcoll);
	char bucketCmp(char *akey, collnum_t acoll, RdbBucket* b);

	bool collExists(collnum_t coll);

	//MEMBER ACCESS
	char     *getDbname()         { return m_dbname;       }
	uint8_t   getKeySize()        { return m_ks;           }
	long      getFixedDataSize()  { return m_fixedDataSize;}
	long      getRecSize()        { return m_recSize;      }
	void      setSwapBuf(char *s) { m_swapBuf = s;         }
	char     *getSwapBuf()        { return m_swapBuf;      }
	bool      needsSave()         { return m_needsSave;    }
	bool      isSaving()          { return m_isSaving;     }
	char     *getSortBuf()        { return m_sortBuf;      }
	long      getSortBufSize()    { return m_sortBufSize;  }
	bool      isWritable()        { return m_isWritable;   }
	void      disableWrites ()    { m_isWritable = false;  }
	void      enableWrites  ()    { m_isWritable = true ;  }
	long      getMaxMem()         { return m_maxMem;       }
	void      setNeedsSave(bool s);

	
	long      getMemAlloced ();
	long      getMemAvailable();
	bool      is90PercentFull();
	bool      needsDump();
	bool      hasRoom ( long numRecs );

	long      getNumKeys();
	long      getMemOccupied( );

	long      getNumNegativeKeys ( );
	long      getNumPositiveKeys ( );
 	bool      delColl            ( collnum_t collnum );

	//just for this collection
// 	long      getMemOccupied     ( collnum_t collnum );
 	long      getNumKeys         ( collnum_t collnum );
//  	long      getNumNegativeKeys ( collnum_t collnum );
//  	long      getNumPositiveKeys ( collnum_t collnum );

	//syntactic sugar
 	RdbBucket* bucketFactory();
	void updateNumRecs(long n, long bytes, long numNeg);

	//DEBUG
	bool selfTest(bool thorough, bool core);
	long addTree(RdbTree *rt);
	void printBuckets();
	bool repair();
	bool testAndRepair();
	


	//Save/Load/Dump
	bool      fastSave      ( char    *dir       ,
				  bool     useThread ,
				  void    *state     ,
				  void    (*callback) (void *state) );
	bool      fastSave_r    ();
	long long fastSaveColl_r( int fd, long long offset);
	bool      loadBuckets   ( char* dbname);
	bool      fastLoad      ( BigFile *f , char* dbname);
	long long fastLoadColl  ( BigFile *f,
				  char* dbname,
				  long long offset );


	//private:
	RdbBucket **m_buckets;
	RdbBucket *m_bucketsSpace;
	char      *m_masterPtr;
	long       m_masterSize;
	long       m_firstOpenSlot;
	long       m_numBuckets;
	long       m_maxBuckets;
	uint8_t    m_ks;
	long       m_fixedDataSize;
	long       m_recSize;
	long       m_numKeysApprox;//includes dups
	long       m_numNegKeys;
	long       m_maxMem;
	long       m_maxBucketsCapacity;
	long       m_dataMemOccupied;

	char        m_rdbId;
	char       *m_dbname;
	char       *m_swapBuf;
	char       *m_sortBuf;
	long        m_sortBufSize;

	bool    m_repairMode;
	bool    m_isWritable;
	bool    m_isSaving;
	// true if buckets was modified and needs to be saved
	bool    m_needsSave;
	char   *m_dir;
	void   *m_state; 
	void  (*m_callback) (void *state);
	long    m_saveErrno;
	char   *m_allocName;
};

#endif
