// Matt Wells, copyright Apr 2001

// . non-blocking dump of an RdbTree to an RdbFile
// . RdbFile can be used with RdbGet even in the middle of dumping
// . uses a little mem for an RdbMap and some for write buffering
// . frees the nodes as it dumps them to disk (flushes cache)
// . can also do a non-key-ordered dump for quick saving of an RdbTree
// . Then you can use RdbDump::load() to load it back to the tree

#ifndef _RDBDUMP_H_
#define _RDBDUMP_H_

#include "BigFile.h"
#include "Loop.h"
#include "RdbTree.h"
#include "RdbBuckets.h"
#include "RdbMap.h"
#include "RdbCache.h"
#include "Msg5.h"

class RdbDump {

 public:

        RdbDump() { m_isDumping = false; };

	void reset ( ) ;

	bool isDumping () { return m_isDumping; };

	// . set up for a dump of rdb records to a file
	// . returns false and sets errno on error
        bool set  ( //char      *coll          ,
		   collnum_t collnum ,
		    BigFile   *file          ,
		    int32_t       id2           , // in Rdb::m_files[] array
		    bool       isTitledb     , // are we dumping TitleRecs?
		    RdbBuckets *buckets      , // optional buckets to dump
		    RdbTree   *tree          , // optional tree to dump
		    RdbMap    *map           ,
		    RdbCache  *cache         , // for caching dumped tree
		    int32_t       maxBufSize    ,
		    bool       orderedDump   , // dump in order of keys?
		    bool       dedup         , // for merging tree into cache
		    int32_t       niceness      ,
		    void      *state         ,
		    void     (* callback ) ( void *state ) ,
		    bool       useHalfKeys   ,
		    int64_t  startOffset   ,
		    //key_t      prevLastKey   ,
		    char      *prevLastKey   ,
		    char       keySize       ,
		   //class DiskPageCache *pc  ,
		   void *pc ,
		    int64_t  maxFileSize   ,
		    class Rdb    *rdb        );

	// a niceness of 0 means to block on the dumping
	int32_t getNiceness() { return m_niceness; };

	// . dump the tree to the file
	// . returns false if blocked, true otherwise
	bool dumpTree ( bool recall );

	bool dumpList ( RdbList *list , int32_t niceness , bool isRecall );

	void doneDumping ( );

	bool doneReadingForVerify ( ) ;

	// . load the table from an RdbFile
	// . used for saved table recovery
	// . table must be empty/unused otherwise false will be returned 
	// . returns true if "filename" does not exist
	// . overriden in LdbDumper to pass an LdbFile casted as an RdbFile
	// . this override makes the file's getSlot() return LdbSlots
	//   which can be appropriately added to an RdbTable or LdbTable
	bool load ( class Rdb *rdb , int32_t fixedDataSize , BigFile *file ,
		    void *pc ); // class DiskPageCache *pc );

	// . calls the callback specified in set() when done
	// . errno set to indicate error #, if any
	void close ( );

	// must be public so wrapper can call it
	bool writeBuf      ( );

	// called when we've finished writing an RdbList to the file
	bool doneDumpingList ( bool addToMap ) ;

	//key_t getFirstKeyInQueue () { return m_firstKeyInQueue; };
	//key_t getLastKeyInQueue  () { return m_lastKeyInQueue; };
	char *getFirstKeyInQueue () { return m_firstKeyInQueue; };
	char *getLastKeyInQueue  () { return m_lastKeyInQueue; };

	// this is called only when dumping TitleRecs
	bool updateTfndbLoop ( );

	void continueDumping();

	// private:

	bool      m_isDumping; // true if we're in the middle of dumping

	// true if the actual write thread is outstanding
	bool      m_writing;

	RdbTree     *m_tree          ;
	RdbBuckets  *m_buckets       ;
	RdbMap      *m_map           ;
	RdbCache    *m_cache         ;
	int32_t         m_maxBufSize    ;
	bool         m_orderedDump   ;
	bool         m_dedup         ; // used for merging/adding tree to cache
	void        *m_state         ;
	void       (*m_callback)(void *state ) ;
	int64_t    m_offset        ;

	BigFile  *m_file          ;
	int32_t      m_id2           ; // secondary id of file we are dumping to
	RdbList  *m_list          ; // holds list to dump
	RdbList   m_ourList       ; // we use for dumping a tree, point m_list
	char     *m_buf           ; // points into list
	char     *m_verifyBuf     ;
	int32_t      m_verifyBufSize ;
	int32_t      m_bytesToWrite  ;
	int32_t      m_bytesWritten  ;
	char      m_addToMap      ;

	//key_t     m_firstKeyInQueue;
	//key_t     m_lastKeyInQueue;
	char      m_firstKeyInQueue[MAX_KEY_BYTES];
	char     *m_lastKeyInQueue;

	//key_t     m_prevLastKey   ;
	char      m_prevLastKey[MAX_KEY_BYTES];

	int32_t      m_nextNode      ;
	//key_t     m_nextKey       ;
	char      m_nextKey[MAX_KEY_BYTES];
	bool      m_rolledOver    ; // true if m_nextKey rolls back to 0

	// . file descriptor of file #0 in the BigFile
	// . we're dumping to this guy
	int       m_fd;

	// we pass this to BigFile::write() to do non-blocking writes
	FileState m_fstate;

	// a niceness of 0 means the dump will block, otherwise, will not
	int32_t      m_niceness;

	bool      m_useHalfKeys;
	bool      m_hacked;
	bool      m_hacked12;

	int32_t      m_totalPosDumped;
	int32_t      m_totalNegDumped;

	// recall info
	int64_t m_t1;
	int32_t      m_numPosRecs;
	int32_t      m_numNegRecs;

	// are we dumping a list of TitleRecs?
	bool m_isTitledb;

	// for scanning db to dedup m_tmp
	//key_t m_startKey;
	// when the scanning is done
	//bool m_done;
	// use this to scan tfndb
	//Msg5 m_msg5;
	// scan the tfndb into ehre
	//RdbList m_ulist;

	// store tfndb key in here for updateTfndbLoop()
	//key_t m_tkey;
	char m_tkey[MAX_KEY_BYTES];

	// for setting m_rdb->m_needsSave after deleting list from tree
	class Rdb *m_rdb;

	//char      m_coll [ MAX_COLL_LEN + 1 ];
	collnum_t m_collnum;

	bool m_doCollCheck;

	bool m_tried;

	bool m_isSuspended;

	char m_ks;

	int32_t m_deduped1;
	int32_t m_deduped2;
	int32_t m_deduped3;
	int32_t m_unforced;
};

#endif
