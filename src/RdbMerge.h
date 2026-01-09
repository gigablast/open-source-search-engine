// Matt Wells, copyright Sep 2000

// . TODO: fix it, include firstPass stuff

// . the merge file reads slots from 1 or more files and dumps to another
// . the merge file dumps those slots in order of keys to the destination file
// . slots with a zero slotSize will be removed
// . does not use any memory or disk space constraints (TODO)
// . the amount of memory it needs is mostly just from map file (is small)
// . disk space is relatively small to memory
// . TODO: create a static var so only one merge can happen at a time

// . on an index of 40 gigs of just key the map file can take 56 megs
// . as we're merging X files into one file we should free up the maps
//   we're merging so we don't take too much memory
// . RdbMap should have a base page number, the page # of first page in it's
//   m_keys/m_offsets/m_dataSizes array
// . shifting 50 megs down will take probably half a second or so but
//   we do it to save memory and it should only be done every 10 megs, say
// . and we can also start benefiting from the merged files immediately in 
//   seek time

// . RdbScan/RdbGet are different now, we have to figure out a way to
//   read in 1 meg or less (as close as we can get to 1 meg) from each
//   rdb file...  TODO

#ifndef _RDBMERGE_H_
#define _RDBMERGE_H_

#include "RdbDump.h"
#include "Msg5.h"

// . we try to read this many bytes at a time then dump to a file
// . keep it to 5 megs for now
//#define MERGE_BUF_SIZE (5*1024*1024)
// . i'm trying to improve query response time
// . while merging, it seems it is directly proportional to MERGE_BUF_SIZE
// . we also need to disable bdflush so all the writes don't spew out on
//   some important reads
// . i think if we do large reads the kernel commits to it and cancelling
//   the thread does not really cancel the read, so keep reads small
//#define MERGE_BUF_SIZE (1000*1000)
// . now we cancel threads, try a bigger one
// . Lars's stripe size is 1 meg i believe
//#define MERGE_BUF_SIZE (4*1024*1024)
// for debugging tiering, i made this small
//#define MERGE_BUF_SIZE (1024)

class RdbMerge {

 public:

	// . selects the files to merge
	// . uses keyMasks and files from the passed Rdb class
	// . filter out keys where key & m_keyMask != m_maskValue
	// . merge to a new file
	// . new file name is stored in m_filename so Rdb can look at it
	// . calls rdb->incorporateMerge() when done with merge or had error
	// . "maxBufSize" is size of list to get then write (read/write buf)
	bool merge ( char       rdbId        ,
		     //char      *coll         ,
		     collnum_t collnum ,
		     BigFile   *target       ,
		     RdbMap    *targetMap    ,
		     int32_t       id2          ,
		     int32_t       startFileNum ,
		     int32_t       numFiles     ,
		     int32_t       niceness     ,
		     //class DiskPageCache *pc     ,
		     void *pc ,
		     int64_t maxTargetFileSize ,
		     char       keySize      );

	bool isMerging ( ) { return m_isMerging; };
	// if we are merging, use this to see what rdb we are merging for
	//class RdbBase *getRdbBase    ( ) { return m_base; };

	// suspend the merging until resumeMerge() is called
	void suspendMerge ( ) ;

	// . return false and sets errno on error merging
	// . returns true if blocked, or completed successfully
	bool resumeMerge  ( ) ;

	// these must be public so wrappers can call
	bool dumpList     ( );
	bool getNextList  ( ) ;
	bool getAnotherList ( ) ;
	void doneMerging  ( ) ;

	 RdbMerge() ;
	~RdbMerge() ;
	void reset();

	// . called to continue merge initialization after lock is secure
	// . lock is g_isMergingLock
	bool gotLock ( ) ;

	// set to true when m_startKey wraps back to 0
	bool        m_doneMerging;

	// how many dup keys removed from the IndexList merge, if any?
	int64_t getDupsRemoved ( ) { return m_dupsRemoved; };

	void doSleep();

	int32_t      m_numThreads;

	// private:

	// . used when growing the database
	// . removes keys that would no longer be stored by us
	//void filterList    ( RdbList *list ) ;

	// . we get the units from the master and the mergees from the units
	class RdbBase  *m_base;
	int32_t        m_startFileNum;
	int32_t        m_numFiles;
	bool        m_dedup;
	int32_t        m_fixedDataSize;
	BigFile    *m_target;
	RdbMap     *m_targetMap;

	// these are used by truncateList()
	//key_t       m_truncationMask;
	//key_t       m_lastMaskedKey;
	char        m_truncationMask[MAX_KEY_BYTES];
	char        m_lastMaskedKey[MAX_KEY_BYTES];
	int32_t        m_lastNumRecs;

	//key_t       m_startKey;
	//key_t       m_endKey;
	char        m_startKey[MAX_KEY_BYTES];
	char        m_endKey[MAX_KEY_BYTES];

	bool        m_isMerging;
	bool        m_isSuspended;
	bool        m_isReadyToSave;

	// for writing to target file
	RdbDump     m_dump;

	// a Msg5 for getting RdbLists from disk/cache
	Msg5        m_msg5;
	Msg5        m_msg5b;

	RdbList     m_list;

	int32_t        m_niceness;

	// count for indexdb
	int64_t   m_dupsRemoved;

	//class DiskPageCache *m_pc;
	int64_t m_maxTargetFileSize;

	int32_t      m_id2;

	// for getting the RdbBase class doing the merge
	uint8_t   m_rdbId;
	//char      m_coll [ MAX_COLL_LEN + 1 ];
	collnum_t m_collnum;

	char      m_ks;
};

#endif
