// Matt Wells,  Copyright, Apr 2001

// . an UNbalanced b-tree for storing keys in memory
// . UNbalanced because it's slow to balance and input should be quite random
// . if the tree becomes highly unbalanced we could just create a new tree
//   and insert things at random into there, but this should we a rarity
// . we store a -1 in the parent field for nodes that were deleted so if you
//   were to dump the nodes out unordered you'd know where the deleted nodes
//   were
// . we store "m_emptyNode" in the left kid field of deleted nodes and then
//   assign m_emptyNode to that deleted node's node # so you can re-use them 
//   in a linked-list type fashion
// . "m_minUsedNode" is the max node # ever occupied. it's used so we know the 
//   limits of a dump, if we were to dump the nodes out unordered
// . NOTE: i changed m_maxNode to m_minUnusedNode for clarity
// . "m_numNodes" is the total (used/unused) # of nodes. this can be grown
//   if we run outta room

// . RdbTree(btree) vs. a hash table 
// . 1. does not need to rehash
// . 2. does not need to sort before dump (uses getNextNode())
// . 3. takes log(N) to add/get/delete plus lotsa balancing overhead
// . 4. has 3 longs overhead per node as opposed to 1 for hash table
// . 4. has 3 longs overhead per node as opposed to 1 for hash table
// . 5. can do key-range lookups very quickly (hash table can't do this at all)

// TODO: use an RdbNode class so we don't have to perform as many
//       random memory accesses which are somewhat slow

// What good is just storing keys in this db? What about the data?
// You can store your data with the keys if it fits in the key size.
// However, large amounts of data per key are better stored separately because
// the key files are continually merged and sorted. Carrying around extra
// weight during these merging processes just slows things down and takes up
// more disk space. Use the "cdb" database to couple your data with your
// keys for you. It is much better suited to handling data of variable length.

#ifndef _RDBTREE_H_
#define _RDBTREE_H_

#include "Mem.h"          // for g_mem.calloc and g_mem.malloc
#include "BigFile.h"      // for saving and loading the tree
#include "RdbList.h"
#include "BigFile.h"
#include "RdbMem.h"

class RdbTree {

	// . this RdbCache class caches scans from a startKey to an endKey
	// . it adds an m_endKeys,m_next,m_prev,m_time to each node
	friend class RdbCache;

 public:

	 RdbTree       ( );
	~RdbTree       ( );

	// . Rdb uses this to determine when to dump this tree to disk
	// . look at % of memory occupied/alloced of max, as well as % of
	//   nodes used
	bool is90PercentFull ( ) {
		// . m_memOccupied is amount of alloc'd mem that data occupies
		// . now we /90 and /100 since multiplying overflowed
		//if ( m_memOccupied /90 >= m_maxMem  /100 ) return true;
		if ( m_numUsedNodes/90 >= m_numNodes/100 ) return true;
		return false;
		// if we have data-less records (just keys) then we're not
		// full so return false
		//if ( m_fixedDataSize == 0 ) return false;
		// how much space does avg rec take up?
		//long avgDataSize ;
		//avgDataSize = m_memAlloced - ( m_numUsedNodes * m_overhead );
		//avgDataSize / m_numUsedNodes;
		// how much space is left
		// are we at 90% capacity?
		// otherwise we should check mem alloc'd as well
		//if ( m_memAlloced *100 >= m_maxMem  *90 ) return true;
	};

	bool isFull ( ) { return (m_numUsedNodes >= m_numNodes); };

	bool hasRoomForKeys ( long nk ) {
		return (m_numUsedNodes + nk <= m_numNodes); };

	// . a fixedDataSize of -1 means each node has data of a variable size
	// . set maxMem to -1 for no max 
	// . returns false & sets errno if fails to alloc "maxNumNodes" nodes
	bool set ( long fixedDataSize , long maxNumNodes ,
		   bool doBalancing   , long maxMem      , bool ownData ,
		   char *allocName ,
		   bool dataInPtrs = false ,
		   char *dbname = NULL , char keySize = 12 ,
		   bool useProtection = false ,
		   bool allowDups     = false );

	// . frees the used memory, etc.
	// . override so derivatives can free up extra header arrays
	void reset  ( );

	// . this just makes all the nodes available for occupation (liberates)
	// . it does not free this tree's control structures
	// . returns # of occupied nodes we liberated
	long clear ( );

	// remove recs from tree that have invalid collnums. this is done
	// at load time. i dunno why it happens. it should never!
	void cleanTree ( char **bases );

	void delColl ( collnum_t collnum ) ;

	// . this will overwrite nodes with the same key
	// . returns -1 if it couldn't grab the memory or grow the table
	// . returns the node # we added it to on success
	// . don't free your data because we don't copy it!
	// . sets errno if it returns -1
	//long addNode ( collnum_t collnum , key_t  key, char *data, 
	long addKey  ( void *key ) {
		return addNode ( 0,(char *)key,NULL,0);};
	long addNode ( collnum_t collnum , char *key, char *data, 
		       long dataSize );
	long addNode ( collnum_t collnum , key_t key, char *data, 
		       long dataSize ) {
		return addNode(collnum,(char *)&key,data,dataSize);};
	long addNode ( collnum_t collnum , char *key ) { //key_t &key ) {
		return addNode ( collnum , key , NULL , 0 ); };
			
	// . returns -1 if not found
	// . otherwise return the node #
	long getNode ( collnum_t collnum , char *key ); //key_t &key );
	long getNode ( collnum_t collnum , key_t &key ) {
		return getNode(collnum,(char *)&key);};

	// . get the node's data directly
	char *getData ( collnum_t collnum , char *key ); // key_t &key ) ;

        // . get the node whose key is >= key 
        // . much much slower than getNextNode() below
        long getNextNode ( collnum_t collnum , char *key ); // key_t &key );
        long getNextNode ( collnum_t collnum , key_t &key ) {
		return getNextNode ( collnum, (char *)&key); };

        // . get the next node # AFTER "node" by key
        // . used for dumping out the nodes ordered by their keys
        // . returns -1 on end
        long getNextNode ( long node );

	long getFirstNode ( );
	long getLastNode  ( );

	long getFirstNode2 ( collnum_t collnum );

	// . get the node whose key is <= "key"
        long getPrevNode ( collnum_t collnum , char *key ); // key_t &key );
        long getPrevNode ( collnum_t collnum , key_t &key ) {
		return getPrevNode(collnum,(char *)&key);};

	// . get the prev node # whose key is <= to key of node #i
	long getPrevNode ( long i ) ;

        // returns the node # with the lowest key, -1 if no nodes in tree
	long getLowestNode ( ) ;

	// . returns true  iff was found and deleted
	// . returns false iff not found 
	// . frees m_data[node] if freeIt is true
	void deleteNode  ( long  node , bool freeData );
	//long deleteNode  ( collnum_t collnum , key_t &key , bool freeData ) ;
	long deleteNode  ( collnum_t collnum , char *key , bool freeData ) ;
	long deleteNode  ( collnum_t collnum , key_t &key , bool freeData ) {
		return deleteNode ( collnum , (char *)&key , freeData ); };

	// delete all nodes with keys in [startKey,endKey]
	void deleteNodes ( collnum_t collnum ,
			   key_t startKey , key_t endKey , bool freeData ) {
		deleteNodes(collnum,(char *)&startKey,(char *)&endKey,
			    freeData); };

	void deleteNodes ( collnum_t collnum ,
			   //key_t startKey , key_t endKey , bool freeData );
			   char *startKey , char *endKey , bool freeData );

	// . delete all records in this list from the tree
	// . call deleteNode()
	// . when deleting lists from spiderdb this seems to take a LONG time
	//   so to make it faster set doBalancing to false. at least nodes
	//   being added after this will still be balanced
	// . returns false if a key in list was not found
	// . this happens if memory is corrupted!
	bool deleteList ( collnum_t collnum ,
			  RdbList *list , bool doBalancing ); //= true );

	bool deleteKeys ( collnum_t collnum , char *keys , long numKeys );

	// . if the list's keys are ordered from smallest to largest
	//   this acts just like deleteList() above, but saves time by
	//   using getNextNode() rather than lookup each key from root of tree
	void deleteOrderedList ( collnum_t collnum , 
				 RdbList *list , bool doBalancing ); //= true);

	// since our arrays aren't public
	char *getData      ( long node ) { return m_data    [node]; };
	long  getDataSize  ( long node ) { return m_sizes   [node]; };
	//key_t getKey       ( long node ) { return m_keys    [node]; };
	char *getKey       ( long node ) { return &m_keys   [node*m_ks]; };
	long  getParentNum ( long node ) { return m_parents [node]; };

	collnum_t getCollnum ( long node ) { return m_collnums [node];};

	bool  isEmpty      ( long node ) { return (m_parents [ node ] == -2);};

	// an upper bound on the # of used nodes
	long  getNumNodes  ( ) { return m_minUnusedNode; };

	long  getNumUsedNodes  ( ) { return m_numUsedNodes; };

	bool  isEmpty ( ) { return (m_numUsedNodes == 0); };

	long  getNumAvailNodes ( ) { return m_numNodes - m_numUsedNodes; };

	long  getNumTotalNodes ( ) { return m_numNodes; };

	// negative and postive counts
	long  getNumNegativeKeys ( ) { return m_numNegativeKeys; };
	long  getNumPositiveKeys ( ) { return m_numPositiveKeys; };
	long  getNumNegativeKeys ( collnum_t collnum ) { 
		return m_numNegKeysPerColl[collnum]; };
	long  getNumPositiveKeys ( collnum_t collnum ) { 
		return m_numPosKeysPerColl[collnum]; };

	// how much mem, including data, is used by this class?
	long getMemAlloced       ( ) { return m_memAlloced;  };
	// . how much of the alloc'd mem is actually in use holding data
	// . includes the tree infrastructure as well as the data itself
	long getMemOccupied      ( ) { return m_memOccupied; };
	long getMaxMem           ( ) { return m_maxMem; };

	// . like getMemOccupied() above but does not include left/right/parent
	// . only includes occupied keys/sizes and the dataSizes themself
	long getMemOccupiedForList2 ( collnum_t collnum  ,
				      //key_t     startKey ,
				      //key_t     endKey   ,
				      char     *startKey ,
				      char     *endKey   ,
				      long      minRecSizes ,
				      long      niceness ) ;

	//  how much mem the tree would take if it were made into a list
	long getMemOccupiedForList ( );

	// . how much mem does this tree use, not including stored data
	// . this will be the same as getMemAlloced() if fixedDataSize is 0
	long getTreeOverhead() { return m_overhead * m_numNodes; };

	// . throw all the records in this range into this list
	// . used for dumping to an rdb file permanently
	// . sets list->m_lastKey to last key inserted into the list
	// . list->m_lastKey will not be valid if list is empty
	// . returns false if outta memory
	// . "antiNumRecs" is set to # of keys w/ low bit cleared (antiKeys)
	//   that were added to "list"
	bool getList ( collnum_t collnum    ,
		       //key_t    startKey    , 
		       //key_t    endKey      , 
		       char    *startKey    , 
		       char    *endKey      , 
		       long     minRecSizes ,
		       RdbList *list        ,
		       long    *numPosRecs  ,
		       long    *numNegRecs ,   // = NULL 
		       bool     useHalfKeys ,  // = false 
		       // RdbDump calls with niceness 1 since 
		       // getMemOccupiedForList2() takes some time!
		       long     niceness = 0 );

	bool getList ( collnum_t collnum    ,
		       key_t    startKey    , 
		       key_t    endKey      , 
		       long     minRecSizes ,
		       RdbList *list        ,
		       long    *numPosRecs  ,
		       long    *numNegRecs ,   // = NULL 
		       bool     useHalfKeys ) {  // = false 
		return getList(collnum,(char *)&startKey,(char *)&endKey,
			       minRecSizes,list,numPosRecs,numNegRecs,
			       useHalfKeys);};

 	// . don't order by keys, order by node #
	// . used for saving a tree to disk temporarily so it can be re-loaded
	//   w/o totally unbalancing the tree
	// . sets "*lastNode" to last node # inserted into the list
	//bool getListUnordered ( long startNode , 
	//			long minRecSizes  ,
	//			RdbList *list  , long *lastNode ) ;

	// estimate the size of the list defined by these keys
	long getListSize ( collnum_t collnum ,
			   //key_t startKey ,key_t endKey , 
			   //key_t *minKey , key_t *maxKey );
			   char *startKey , char *endKey , 
			   char *minKey   , char *maxKey );

	// how balanced is this tree? = #nodes w/ right kids / # node w/ left
	// the multiplied by 100. invereted to make smaller than 100.
	long getBalancePercent();

	// . load & save the tree quickly
	// . returns false on error, true otherwise
	// . sometimes sets g_errno when it returns false
	bool fastLoad ( BigFile *f , RdbMem *memStack ) ;
	// . we now optionally save with a thread
	// . when saving m_isSaving is set to true and nothing can be added
	//   to the tree, g_errno will be set to ETRYAGAIN when addNode()
	//   is called
	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool fastSave ( char    *dir       ,
			char    *dbname    ,
			bool     useThread ,
			void    *state     , 
			void    (* callback)(void *state ) );
	// this is called by a thread
	bool fastSave_r() ;

	// to fix lar's machine
	bool oldLoad  ( BigFile *f , RdbMem *memStack ) ;
	long oldLoadBlock ( BigFile *f, long remainingNodes , RdbMem *stack,
			    long long offset );

	long getMinUnusedNode () { return m_minUnusedNode; };

	bool checkTree  ( bool printMsgs , bool doChainTest );
	bool checkTree2 ( bool printMsgs , bool doChainTest );
	bool fixTree    ( );

	// all except the data: the keys,dataPtr,size,left,right,parents,depth
	long  getRecOverhead () { return m_overhead; };

	// Rdb which contains this class calls this to prevent swap-out once
	// per minute or so
	long scanMem ( ) ;

	void disableWrites () { m_isWritable = false; };
	void enableWrites  () { m_isWritable = true ; };

	// can we write to the tree?
	bool    m_isWritable;
	// . this stuff is accessed by thread an must be public
	// . cannot add to tree when saving
	bool    m_isSaving;

	long    m_gettingList;

	// loading?
	bool    m_isLoading;

	// true if tree was modified and needs to be saved
	bool    m_needsSave;
	// need to pass this file to the fastSave() thread
	//BigFile *m_saveFile;
	char  m_rdbId;
	char  m_dir[128];
	char  m_dbname[32];
	char  m_memTag[16];

	// this callback called when fastSave is complete
	void     *m_state; 
	void    (* m_callback) (void *state );

	long long getBytesWritten ( ) { return m_bytesWritten; };
	long long getBytesRead    ( ) { return m_bytesRead   ; };

	// private:

	// used by fastSave() and fastLoad()
	long fastSaveBlock_r ( int        fd         ,
			       long       start      , 
			       long long  offset     ) ;
	long fastLoadBlock ( BigFile *f            , 
			     long       start      , 
			     long       totalNodes ,
			     RdbMem    *stack      ,
			     long long  offset     );

	void setDepths    ( long bottomNode );
	long rotateRight  ( long pivotNode );
	long rotateLeft   ( long pivotNode );
	long rotate       ( long pivotNode , long *lefts , long *rights );
	long computeDepth ( long headNode  );
	// is this tree a balanced binary tree?
	bool m_doBalancing;

	// used by getListSize() to estiamte a list size
	//long getOrderOfKey ( collnum_t collnum , key_t key , key_t *retKey );
	long getOrderOfKey ( collnum_t collnum , char *key , char *retKey );
	// used by getrderOfKey() (have to estimate if tree not balanced)
	long getTreeDepth  ();

	// . returns true if tree doesn't need to grow/shrink
        // . re-allocs the m_keys,m_data,m_sizes,m_leftNodes,m_rightNodes
	// . used for growing AND shrinking the table
        bool  growTree  ( long newNumNodes );

	// are we responsible for freeing nodes' data
	bool    m_ownData;

	// true if the m_data[i] ptrs are not really ptrs
	bool    m_dataInPtrs;

	// each node/node in the tree has these datum:
	collnum_t *m_collnums; // each key now has a collection number
	//key_t  *m_keys;         // 96bits each (3 longs)
	char   *m_keys;         // X bytes each
	char  **m_data;         // NULL iff m_dataSize is 0
	long   *m_sizes;        // NULL iff m_dataSize is 0
	long   *m_left;         // left  kid of this node in the tree
	long   *m_right;        // right kid of this node in the tree
	long   *m_parents;      // parent of this node - for getNextNode()
	char   *m_depth;        // depth of this node (used iff m_doBalancing)
	long    m_numNodes;     // how many we have, empty or full
	long    m_numUsedNodes; // how many of those are used? (full)
	// negative and postive key counts
	long    m_numNegativeKeys;
	long    m_numPositiveKeys;
	// memory overhead per node (excluding data)
	long    m_overhead;     
	// switch between picking left and right kids to replace deleted nodes
	// in order to keep the tree more balanced
	char    m_pickRight;
	// the node at the top of the tree
	long    m_headNode;
	// total mem this tree is using (including data that nodes point to)
	long    m_memAlloced;
	// total amount of m_memAlloced that is occupied
	long    m_memOccupied; 
	// max limit of m_memAlloced
	long    m_maxMem;      
	// mem allocated for overhead of tree structure
	long    m_baseMem;
	// -1 means any dataSize, otherwise, it's fixed to this
	long    m_fixedDataSize;
	// node of the next available/empty node
	long    m_nextNode;
	// maximum node # that was ever used at some point in time
	long    m_minUnusedNode;

	char *m_allocName;

	// so we can save the tree within a file that has other stuff
	long long m_bytesWritten;
	long long m_bytesRead;

	long m_saveErrno;
	char m_ks;

	bool m_useProtection;
	bool m_isProtected;
	void protect   () { 
		if ( m_isProtected ) return;
		m_isProtected = true; 
		protect ( PROT_READ ); };
	void unprotect () { 
		if ( ! m_isProtected ) return;
		m_isProtected = false; 
		protect ( PROT_READ | PROT_WRITE ); };
	void protect   ( int prot );
	void gbmprotect ( void *p , long size , int prot );

	bool m_allowDups;

	long m_corrupt;

	long m_numPosKeysPerColl[MAX_COLL_RECS];
	long m_numNegKeysPerColl[MAX_COLL_RECS];
};

#endif
