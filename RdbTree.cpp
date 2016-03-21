// JAB: this is required for pwrite() in this module
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "RdbTree.h"
#include "Loop.h"
#include "Threads.h"
#include "Datedb.h"
#include "Linkdb.h"

RdbTree::RdbTree () {
	//m_countsInitialized = false;
	m_collnums= NULL;
	m_keys    = NULL;
	m_data    = NULL;
	m_sizes   = NULL;
	m_left    = NULL;
	m_right   = NULL;
	m_parents = NULL;
	m_depth   = NULL;
	m_headNode      = -1;
	m_numNodes      =  0;
	m_numUsedNodes  =  0;
	m_memAlloced    =  0;
	m_memOccupied   =  0;
	m_nextNode      =  0;
	m_minUnusedNode =  0; 
	m_fixedDataSize = -1; // variable dataSize, depends on individual node
	m_isProtected   = false;
	m_needsSave     = false;
	m_useProtection = false;
	m_pickRight     = false;
	m_gettingList   = 0;

	// before resetting... we have to set this so clear() won't breach buffers
	m_rdbId = -1;

	reset();
}

RdbTree::~RdbTree ( ) {
	reset ( );
}

/*
#include <asm/page.h> // PAGE_SIZE

// return #of bytes scanned for timing purposes
int32_t RdbTree::scanMem ( ) {
	// ahh.. just scan the whole thing to keep it simple
	char *p    ;
	char *pend ;
	char  c;
	int32_t  size = 0; // count number of bytes scanned
	// keys
	p = (char *)m_keys ; pend = p + m_numNodes * m_ks;
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	// data ptrs
	p = (char *)m_data ; pend = p + m_numNodes * sizeof(char *);
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	// sizes
	p = (char *)m_sizes ; pend = p + m_numNodes * sizeof(int32_t);
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	// left
	p = (char *)m_left ; pend = p + m_numNodes * sizeof(int32_t);
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	// right
	p = (char *)m_right ; pend = p + m_numNodes * sizeof(int32_t);
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	// parents
	p = (char *)m_parents ; pend = p + m_numNodes * sizeof(int32_t);
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	// depth
	p = (char *)m_right ; pend = p + m_numNodes * sizeof(char);
	size += pend - p;
	if ( p ) while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	return size;
}
*/

// "memMax" includes records plus the overhead
bool RdbTree::set ( int32_t fixedDataSize , 
		    int32_t maxNumNodes   ,
		    bool doBalancing   , 
		    int32_t memMax        ,
		    bool ownData       ,
		    char *allocName    ,
		    bool dataInPtrs    ,
		    char *dbname       ,
		    char  keySize      ,
		    bool  useProtection ,
		    bool  allowDups     ,
		    char  rdbId ) {
	reset();
	m_fixedDataSize   = fixedDataSize; 
	m_doBalancing     = doBalancing;
	m_maxMem          = memMax;
	m_ownData         = ownData;
	m_allocName       = allocName;
	m_dataInPtrs      = dataInPtrs;
	//m_dbname          = dbname;
	m_ks              = keySize;
	m_useProtection   = useProtection;
	m_allowDups       = allowDups;
	m_needsSave       = false;

	m_dbname[0] = '\0';
	if ( dbname ) {
		int32_t dlen = strlen(dbname);
		if ( dlen > 30 ) dlen = 30;
		gbmemcpy(m_dbname,dbname,dlen);
		m_dbname[dlen] = '\0';
	}

	// a malloc tag, must be LESS THAN 16 bytes including the NULL
	char *p = m_memTag;
	gbmemcpy  ( p , "RdbTree" , 7 ); p += 7;
	if ( dbname ) strncpy ( p , dbname    , 8 ); p += 8;
	*p++ = '\0';
	// set rdbid
	m_rdbId = rdbId; // -1;
	// sanity
	if ( rdbId < -1       ) { char *xx=NULL;*xx=0; }
	if ( rdbId >= RDB_END ) { char *xx=NULL;*xx=0; }
	// if its doledb, set it
	//if ( dbname && strcmp(dbname,"doledb") == 0 ) m_rdbId = RDB_DOLEDB;
	// adjust m_maxMem to virtual infinity if it was -1
	if ( m_maxMem < 0 ) m_maxMem = 0x7fffffff;
	// . compute each node's memory overhead
	// . size of a key/left/right/parent
	m_overhead = (m_ks + 4*3 );
	// include collection number, currently an uint16_t
	m_overhead += sizeof(collnum_t);
	// if we're a non-zero data length include a dataptr (-1 means variabl)
	if ( m_fixedDataSize !=  0 ) m_overhead += 4;
	// include dataSize if our dataSize is variable (-1)
	if ( m_fixedDataSize == -1 ) m_overhead += 4;
	// if we're balanced include 1 byte per node for the depth
	if ( m_doBalancing         ) m_overhead += 1;
	if( maxNumNodes == -1) {
		maxNumNodes = m_maxMem / m_overhead;
		if(maxNumNodes > 10000000) maxNumNodes = 10000000;
	}
	// initiate protection
	if ( m_useProtection ) protect();
	// allocate the nodes
	return growTree ( maxNumNodes , 0 );
}

void RdbTree::reset ( ) {
	// . sanity check
	// . SpiderCache.cpp uses a tree, but withou a dbname
	if ( m_needsSave && m_dbname[0] && 
	     strcmp(m_dbname,"accessdb") &&
	     strcmp(m_dbname,"statsdb") ) {
	     //strcmp(m_dbname,"doledb") ) {
		log("rdb: RESETTING UNSAVED TREE %s.",m_dbname);
		log("rdb: RESETTING UNSAVED TREE %s.",m_dbname);
		log("rdb: RESETTING UNSAVED TREE %s.",m_dbname);
		// when DELETING a collection from pagecrawlbot.cpp
		// it calls Collectiondb::deleteRec() which calls
		// SpiderColl::reset() which calls m_waitingTree.reset()
		// which was coring here! so take this out
		//char *xx = NULL; *xx = 0;
	}
	// unprotect it all
	if ( m_useProtection ) unprotect ( );
	// make sure string is NULL temrinated. this gbstrlen() should 
	if ( m_numNodes > 0 && 
	     m_dbname[0] && 
	     gbstrlen(m_dbname) >= 0 &&
	     // don't be spammy we can have thousands of these, one per coll
	     strcmp(m_dbname,"waitingtree") )
		log(LOG_INFO,"db: Resetting tree for %s.",m_dbname);

	// liberate all the nodes
	clear();
	// do not require saving after a reset
	m_needsSave = false;
	// now free all the overhead structures of this tree
	int32_t n = m_numNodes;
	// free array of collectio numbers (int16_ts for now)
	if ( m_collnums) mfree ( m_collnums, sizeof(collnum_t) *n,m_allocName);
	// free the array of keys
	if ( m_keys  ) mfree ( m_keys  , m_ks      * n , m_allocName ); 
	// free the data ptrs
	if ( m_data  ) mfree ( m_data  , sizeof(char *) * n , m_allocName ); 
	// free the array of dataSizes
	if ( m_sizes ) mfree ( m_sizes , n * 4              , m_allocName ); 
	// free the sorted node #'s
	if ( m_left    ) mfree ( m_left    , n * 4 ,m_allocName);
	if ( m_right   ) mfree ( m_right   , n * 4 ,m_allocName);
	if ( m_parents ) mfree ( m_parents , n * 4 ,m_allocName);
	if ( m_depth   ) mfree ( m_depth   , n     ,m_allocName);
	m_collnums      = NULL; 
	m_keys          = NULL; 
	m_data          = NULL;
	m_sizes         = NULL;
	m_left          = NULL;
	m_right         = NULL;
	m_parents       = NULL;
	m_depth         = NULL;
	// tree description vars
	m_headNode      = -1;
	m_numNodes      =  0;
	m_numUsedNodes  =  0;
	m_memAlloced    =  0;
	m_memOccupied   =  0;
	m_nextNode      =  0;
	m_minUnusedNode =  0; 
	m_fixedDataSize = -1; // variable dataSize, depends on individual node
	// clear counts
	m_numNegativeKeys = 0;
	m_numPositiveKeys = 0;
	//memset ( m_numNegKeysPerColl , 0 , 4*MAX_COLLS );
	//memset ( m_numPosKeysPerColl , 0 , 4*MAX_COLLS );
	m_isSaving        = false;
	m_isLoading       = false;
	m_isWritable      = true;
}

void RdbTree::delColl ( collnum_t collnum ) {
	m_needsSave = true;
	//key_t startKey;
	//key_t endKey;
	//startKey.setMin();
	//endKey.setMax();
	char *startKey = KEYMIN();
	char *endKey   = KEYMAX();
	deleteNodes ( collnum , startKey , endKey , true/*freeData*/) ;
}

// . this just makes all the nodes available for occupation (liberates them)
// . it does not free this tree's control structures
// . returns # of occupied nodes we liberated
int32_t RdbTree::clear ( ) {

	if ( m_numUsedNodes > 0 ) m_needsSave = true;
	// the liberation count
	int32_t count = 0;
	// liberate all of our nodes
	int32_t dataSize = m_fixedDataSize;
	for ( int32_t i = 0 ; i < m_minUnusedNode ; i++ ) {
		// skip node if parents is -2 (unoccupied)
		if ( m_parents[i] == -2 ) continue;
		// we no longer count the overhead of this node as occupied
		m_memOccupied -= m_overhead;
		// make the ith node available for occupation
		m_parents[i] = -2;
		// keep count
		count++;
		// continue if we have no data to free
		if ( ! m_data ) continue;
		// read dataSize from m_sizes[i] if it's not fixed
		if ( m_fixedDataSize == -1 ) dataSize = m_sizes[i];
		// free the data being pointed to
		if ( m_ownData ) mfree ( m_data[i] , dataSize ,m_allocName);
		// adjust our reported memory usage
		m_memAlloced  -= dataSize;
		m_memOccupied -= dataSize;
	}
	// reset all these
	m_headNode      = -1;
	m_numUsedNodes  =  0;
	m_nextNode      =  0;
	m_minUnusedNode =  0; 
	// clear counts
	m_numNegativeKeys = 0;
	m_numPositiveKeys = 0;


	// clear tree counts for all collections!
	int32_t nc = g_collectiondb.m_numRecs;
	// BUT only if we are an Rdb::m_tree!!!
	if ( m_rdbId == -1 ) nc = 0;
	// otherwise, we overwrite stuff in CollectionRec we shouldn't
	for ( int32_t i = 0 ; i < nc ; i++ ) {
		CollectionRec *cr = g_collectiondb.getRec(i);
		if ( ! cr ) continue;
		if ( m_rdbId < 0 ) continue;
		//if (((unsigned char)m_rdbId)>=RDB_END){char *xx=NULL;*xx=0; }
		cr->m_numNegKeysInTree[(unsigned char)m_rdbId] = 0;
		cr->m_numPosKeysInTree[(unsigned char)m_rdbId] = 0;
	}

	return count;
}

// . used by cache 
// . wrapper for getNode()
int32_t RdbTree::getNode ( collnum_t collnum , char *key ) { // key_t &key ) { 
	int32_t i = m_headNode;
	// get the node (about 4 cycles per loop, 80cycles for 1 million items)
	while ( i != -1 ) {
		if ( collnum < m_collnums[i] ) { i = m_left [i]; continue;}
		if ( collnum > m_collnums[i] ) { i = m_right[i]; continue;}
		//if ( key <  m_keys[i] ) { i = m_left [i]; continue;}
		//if ( key >  m_keys[i] ) { i = m_right[i]; continue;}
		if ( KEYCMP(key,0,m_keys,i,m_ks)<0) { i=m_left [i]; continue;}
		if ( KEYCMP(key,0,m_keys,i,m_ks)>0) { i=m_right[i]; continue;}
		return i;
        }
	return -1;
}

// . returns node # whose key is >= "key"
// . returns -1 if none
// . used by RdbTree::getList()
// . TODO: spiderdb stores records by time so our unbalanced tree really hurts
//         us for that.
// . TODO: keep a m_lastStartNode and start from that since it tends to only
//         increase startKey via Msg3. if the key at m_lastStartNode is <=
//         the provided key then we did well.
int32_t RdbTree::getNextNode ( collnum_t collnum , char *key ) { //key_t &key ) {
	// return -1 if no non-empty nodes in the tree
	if ( m_headNode < 0 ) return -1;
	// get the node (about 4 cycles per loop, 80cycles for 1 million items)
	int32_t parent;
	int32_t i = m_headNode ;
	// . set i tom_hint if it's < key
	// . this helps out severly unbalanced trees made by spiderdb
	// . it may hurt other guys a bit though
	//if (m_hint >= 0 && 
	//m_lastStartNode < m_numNodes &&
	//m_parents [m_hint ] != -2 &&
	//m_keys    [m_hint ] <= key ) 
	//i =m_hint;
	while ( i != -1 ) {
		parent = i;
		if ( collnum < m_collnums[i] ) { i = m_left [i]; continue;}
		if ( collnum > m_collnums[i] ) { i = m_right[i]; continue;}
		//if ( key <  m_keys[i] ) { i = m_left [i]; continue;}
		//if ( key >  m_keys[i] ) { i = m_right[i]; continue;}
		if (KEYCMP(key,0,m_keys,i,m_ks)<0) { i = m_left [i]; continue;}
		if (KEYCMP(key,0,m_keys,i,m_ks)>0) { i = m_right[i]; continue;}
		return i;
        }
	if ( m_collnums [ parent ] >  collnum ) return parent;
	if ( m_collnums [ parent ] == collnum && //m_keys [ parent ] > key ) 
	     KEYCMP(m_keys,parent,key,0,m_ks)>0 )
		return parent;
	return getNextNode ( parent );
}

int32_t RdbTree::getFirstNode ( ) {
	//key_t k;  k.n0 = 0LL; k.n1 = 0;
	char *k = KEYMIN();
	return getNextNode ( 0 , k );
}

int32_t RdbTree::getFirstNode2 ( collnum_t collnum ) {
	//key_t k;  k.n0 = 0LL; k.n1 = 0;
	char *k = KEYMIN();
	return getNextNode ( collnum , k );
}

int32_t RdbTree::getLastNode ( ) {
	//key_t k;  k.setMax();
	char *k = KEYMAX();
	return getPrevNode ( (collnum_t)0x7fff , k );
}

// . get the node whose key is <= "key"
// . returns -1 if none
int32_t RdbTree::getPrevNode ( collnum_t collnum , char *key ) { // key_t &key ) {
	// return -1 if no non-empty nodes in the tree
	if ( m_headNode < 0  ) return -1;
	// get the node (about 4 cycles per loop, 80cycles for 1 million items)
	int32_t parent;
	int32_t i = m_headNode ;
	while ( i != -1 ) {
		parent = i;
		if ( collnum < m_collnums[i] ) { i = m_left [i]; continue;}
		if ( collnum > m_collnums[i] ) { i = m_right[i]; continue;}
		//if ( key <  m_keys[i] ) { i = m_left [i]; continue;}
		//if ( key >  m_keys[i] ) { i = m_right[i]; continue;}
		if ( KEYCMP(key,0,m_keys,i,m_ks)<0) {i=m_left [i];continue;}
		if ( KEYCMP(key,0,m_keys,i,m_ks)>0) {i=m_right[i];continue;}
		return i;
        }
	if ( m_collnums [ parent ] <  collnum ) return parent;
	if ( m_collnums [ parent ] == collnum && //m_keys [ parent ] < key ) 
	     KEYCMP(m_keys,parent,key,0,m_ks) < 0 ) return parent;
	return getPrevNode ( parent );
}

char *RdbTree::getData ( collnum_t collnum , char *key ) { // key_t &key ) {
	int32_t n = getNode ( collnum , key ); if ( n < 0 ) return NULL;
	return m_data[n];
};

// . "i" is the previous node number
// . we could eliminate m_parents[] array if we limited tree depth!
// . 24 cycles to get the first kid
// . averages around 50 cycles per call probably
// . 8 cycles are spent entering/exiting this subroutine (inline it? TODO)
int32_t RdbTree::getNextNode ( int32_t i ) {
	// cruise the kids if we have a right one
	if ( m_right[i] >= 0 ) {
		// go to the right kid
		i = m_right [ i ];
		// now go left as much as we can
		while ( m_left [ i ] >= 0 ) i = m_left [ i ];
		// return that node (it's a leaf or has one right kid)
		return i;
	}
	// now keep getting parents until one has a key bigger than i's key
	int32_t p = m_parents[i];
	// if parent is negative we're done
	if ( p < 0 ) return -1;
	// if we're the left kid of the parent, then the parent is the
	// next biggest node
	if ( m_left[p] == i ) return p;
	// otherwise keep getting the parent until it has a bigger key
	// or until we're the LEFT kid of the parent. that's better
	// cuz comparing keys takes longer. loop is 6 cycles per iteration.
	while ( p >= 0  &&  (m_collnums[p] < m_collnums[i] ||
			     ( m_collnums[p] == m_collnums[i] && 
			       KEYCMP(m_keys,p,m_keys,i,m_ks) < 0 )) )
		p = m_parents[p];
	// p will be -1 if none are left
	return p;
}

// . "i" is the next node number
int32_t RdbTree::getPrevNode ( int32_t i ) {
	// cruise the kids if we have a left one
	if ( m_left[i] >= 0 ) {
		// go to the left kid
		i = m_left [ i ];
		// now go right as much as we can
		while ( m_right [ i ] >= 0 ) i = m_right [ i ];
		// return that node (it's a leaf or has one left kid)
		return i;
	}
	// now keep getting parents until one has a key bigger than i's key
	int32_t p = m_parents[i];
	// if we're the right kid of the parent, then the parent is the
	// next least node
	if ( m_right[p] == i ) return p;
	// keep getting the parent until it has a bigger key
	// or until we're the RIGHT kid of the parent. that's better
	// cuz comparing keys takes longer. loop is 6 cycles per iteration.
	while ( p >= 0  &&  (m_collnums[p] > m_collnums[i] ||
			     ( m_collnums[p] == m_collnums[i] && 
			       KEYCMP(m_keys,p,m_keys,i,m_ks) > 0 )) )
		p = m_parents[p];
	// p will be -1 if none are left
	return p;
}

// . get the node with the lowest key
int32_t RdbTree::getLowestNode ( ) {
	int32_t i = m_headNode;
	while ( m_left[i] != -1 ) i = m_left [ i ];
	return i;
}

// . returns -1 if we coulnd't allocate the new space and sets g_errno to ENOMEM
//   or ETREENOGROW, ...
// . returns node # we added it to on success
// . this will replace any current node with the same key
// . sets retNode to the node we added the data to (used internally)
// . negative dataSizes should be interpreted as 0
// . probably about 120 cycles per add means we can add 2 million per sec
// . NOTE: does not check to see if it will exceed m_maxMem
int32_t RdbTree::addNode ( collnum_t collnum , 
			char *key , char *data , int32_t dataSize ) {
	// cannot add if saving, tell them to try again later
	if ( m_isSaving ) { g_errno = ETRYAGAIN; return -1; }
	// nor if not writable
	if ( ! m_isWritable ) { g_errno = ETRYAGAIN; return -1; }
	// if there's no more available nodes, error out
	if ( m_numUsedNodes >= m_numNodes) { g_errno = ENOMEM; return -1; }
	// we need to be saved now
	m_needsSave = true;

	// sanity check - no empty positive keys for doledb
	if ( m_rdbId == RDB_DOLEDB && dataSize == 0 && (key[0]&0x01) == 0x01){
		char *xx=NULL;*xx=0; }

	// for posdb
	if ( m_ks == 18 &&(key[0] & 0x06) ) {char *xx=NULL;*xx=0; }

	// sanity check, break if 0 > titleRec > 100MB, it's probably corrupt
	//if ( m_dbname && m_dbname[0]=='t' && dataSize >= 4 && 
	//     (*((int32_t *)data) > 100000000 || *((int32_t *)data) < 0 ) ) {
	//	char *xx = NULL; *xx = 0; }
	// sanity check (MDW)
	//if ( dataSize == 0 && (*key & 0x01) && m_dbname[0] != 'c' && 
	//     (*key & 0x02) ) {
	//	char *xx = NULL; *xx = 0; }
	// commented out because is90PercentFull checks m_memOccupied and
	// we can breech m_memAlloced w/o breeching 90% of m_memOccupied
	// if ( m_memAlloced + dataSize > m_maxMem) {
	// . if no more mem, error out
	// . we now use RdbMem class so this isn't necessary
	//if ( m_memOccupied + dataSize > m_maxMem){g_errno=ENOMEM; return -1;}
	// set up vars
	int32_t iparent ;
	int32_t rightGuy;
	// this is -1 iff there are no nodes used in the tree
	int32_t i = m_headNode;
	// disable mem protection
	char undo ;
	if ( m_useProtection ) { 
		if ( m_isProtected ) undo = 1; 
		else                 undo = 0;
		unprotect ( ); 
	}
	// . find the parent of node i and call it "iparent"
	// . if a node exists with our key then replace it
	while ( i != -1 ) {
		iparent = i;
		if ( collnum < m_collnums[i] ) { i = m_left [i]; continue;}
		if ( collnum > m_collnums[i] ) { i = m_right[i]; continue;}
		//if      ( key < m_keys[i] ) i = m_left [i]; 
		//else if ( key > m_keys[i] ) i = m_right[i]; 
		if      ( KEYCMP(key,0,m_keys,i,m_ks)<0) i = m_left [i];
		else if ( KEYCMP(key,0,m_keys,i,m_ks)>0) i = m_right[i];
		else    {
			if ( ! m_allowDups ) goto replaceIt; 
			// otherwise, always go right on equal
			i = m_right[i];
		}
	}

	// . this overhead is key/left/right/parent
	// . we inc it by the data and sizes array if we need to below
	m_memOccupied += m_overhead;
	// point i to the next available node
	i = m_nextNode;
	// debug msg
	//if ( m_dbname && m_dbname[0]=='t' && dataSize >= 4 )
	//	logf(LOG_DEBUG,
	//	     "adding node #%"INT32" with data ptr at %"XINT32" "
	//	     "and data size of %"INT32" into a list.",
	//	     i,data,dataSize);
	// if we're the first node we become the head node and our parent is -1
	if ( m_numUsedNodes == 0 ) {
		m_headNode =  i;
		iparent    = -1;
		// ensure these are right
		m_numNegativeKeys = 0;
		m_numPositiveKeys = 0;
		// we only use these stats for Rdb::m_trees for a 
		// PER COLLECTION count, since there can be multiple 
		// collections using the same Rdb::m_tree!
		// crap, when fixing a tree this will segfault because
		// m_recs[collnum] is NULL.
		if ( m_rdbId >= 0 && g_collectiondb.m_recs[collnum] ) {
			//if( ((unsigned char)m_rdbId)>=RDB_END){char *xx=NULL;*xx=0; }
			g_collectiondb.m_recs[collnum]->
				m_numNegKeysInTree[(unsigned char)m_rdbId] =0;
			g_collectiondb.m_recs[collnum]->
				m_numPosKeysInTree[(unsigned char)m_rdbId] =0;
		}
	}

	// stick ourselves in the next available node, "m_nextNode"
	//m_keys    [ i ] = key;
	KEYSET ( &m_keys[i*m_ks] , key , m_ks );
	m_parents [ i ] = iparent;
	// save collection number now, too
	m_collnums [ i ] = collnum;
	// add the key
	// set the data and size only if we need to
	if ( m_fixedDataSize != 0 ) {
		m_data [ i ] = data;
		// ack used and occupied mem
		if ( m_fixedDataSize >= 0 ) {
			m_memAlloced  += m_fixedDataSize;
			m_memOccupied += m_fixedDataSize;
		}
		else {
			m_memAlloced  += dataSize ; 
			m_memOccupied += dataSize ;
		}
		// we may have a variable size of data as well
		if ( m_fixedDataSize == -1 ) m_sizes [ i ] = dataSize;
	}
	// make our parent, if any, point to us
	if ( iparent >= 0 ) {
		if      ( collnum < m_collnums[iparent] ) 
			m_left [iparent] = i;
		else if (collnum==m_collnums[iparent]&&//key<m_keys[iparent] ) 
			 KEYCMP(key,0,m_keys,iparent,m_ks)<0 )
			m_left [iparent] = i;
		else    
			m_right[iparent] = i;
	}
	// . the right kid of an empty node is used as a linked list of
	//   empty nodes formed by deleting nodes
	// . we keep the linked list so we can re-used these vacated nodes
	rightGuy = m_right [ i ];
	// our kids are -1 (none)
	m_left  [ i ] = -1;
	m_right [ i ] = -1;
	// . if we weren't recycling a node then advance to next
	// . m_minUnusedNode is the lowest node number that was never filled
	//   at any one time in the past
	// . you might call it the brand new housing district
	if ( m_nextNode == m_minUnusedNode ) {m_nextNode++; m_minUnusedNode++;}
	// . otherwise, we're in a linked list of vacated used houses
	// . we have a linked list in the right kid
	// . make sure the new head doesn't have a left
	else {
		// point m_nextNode to the next available used house, if any
		if ( rightGuy >= 0 ) m_nextNode = rightGuy;
		// otherwise point it to the next brand new house (TODO:REMOVE)
		// this is an error, try to fix the tree
		else {
			log("db: Encountered corruption in tree while "
			    "trying to add a record. You should "
			    "replace your memory sticks.");
			if ( ! fixTree ( ) ) {
				char *p = NULL;
				*p = 1;
			}
			//sleep(50000); // m_nextNode=m_minUnusedNode;
		}
	}
	// we have one more used node
	m_numUsedNodes++;
	// update sign counts
	if ( KEYNEG(key) ) {
		m_numNegativeKeys++;
		//m_numNegKeysPerColl[collnum]++;
		// we only use these stats for Rdb::m_trees for a 
		// PER COLLECTION count, since there can be multiple 
		// collections using the same Rdb::m_tree!
		// crap, when fixing a tree this will segfault because
		// m_recs[collnum] is NULL.
		if ( m_rdbId >= 0 && g_collectiondb.m_recs[collnum] ) {
			//if( ((unsigned char)m_rdbId)>=RDB_END){
			//char *xx=NULL;*xx=0; }
			CollectionRec *cr ;
			cr = g_collectiondb.m_recs[collnum];
			if(cr)cr->m_numNegKeysInTree[(unsigned char)m_rdbId]++;
		}
	}
	else {
		m_numPositiveKeys++;
		//m_numPosKeysPerColl[collnum]++;
		// crap, when fixing a tree this will segfault because
		// m_recs[collnum] is NULL.
		if ( m_rdbId >= 0 && g_collectiondb.m_recs[collnum] ) {
			//if( ((unsigned char)m_rdbId)>=RDB_END){
			//char *xx=NULL;*xx=0; }
			CollectionRec *cr ;
			cr = g_collectiondb.m_recs[collnum];
			if(cr)cr->m_numPosKeysInTree[(unsigned char)m_rdbId]++;
		}
	}
	// debug2 msg
	// fprintf(stderr,"+ #%"INT32" %"INT64" %"INT32"\n",i,key.n0,iparent);
	// if we don't have to balance return i now
	if ( m_doBalancing ) {
		// our depth is now 1 since we're a leaf node
		// (we include ourself)
		m_depth [ i ] = 1;
		// . reset depths starting at i's parent and ascending the tree
		// . will balance if child depths differ by 2 or more
		setDepths ( iparent );
	}
	// re-enable mem protection
	if ( m_useProtection && undo ) protect ( );
	// return the node number of the node we occupied
	return i; 

	// come here to replace node i with the new data/dataSize
 replaceIt:
	// debug msg
	//fprintf(stderr,"replaced it!\n");
	// if we don't support any data then we're done
	if ( m_fixedDataSize == 0 ) {
		if ( m_useProtection && undo ) protect();
		return i;
	}
	// get dataSize
	int32_t oldDataSize = m_fixedDataSize;
	// if datasize was 0 cuz it was a negative key, fix that for
	// calculating m_memOccupied
	if ( m_fixedDataSize >= 0 ) dataSize = m_fixedDataSize;
	if ( m_fixedDataSize < 0  ) oldDataSize = m_sizes[i];
	// free i's data
	if ( m_data[i] && m_ownData ) 
		mfree ( m_data[i] , oldDataSize ,m_allocName);
	// decrease mem occupied and increase by new size
	m_memOccupied -= oldDataSize;
	m_memOccupied += dataSize;
	m_memAlloced  -= oldDataSize;
	m_memAlloced  += dataSize;
	// otherwise set the data
	m_data [ i ] = data;
	// set the size if we need to as well
	if ( m_fixedDataSize < 0 ) m_sizes [ i ] = dataSize;
	// re-enable mem protection if we should
	if ( m_useProtection && undo ) protect();
	return i;
}

//int32_t RdbTree::deleteNode  ( collnum_t collnum , key_t &key , bool freeData ){
int32_t RdbTree::deleteNode  ( collnum_t collnum , char *key , bool freeData ) {
	int32_t node = getNode ( collnum , key );
	// debug
	//log("db: deleting n1=%"XINT64" n0=%"XINT64" node=%"INT32".",
	//    *(int64_t *)(key+8), *(int64_t *)(key+0),node);
	if ( node == -1 ) return -1;
	deleteNode3(node,freeData); 
	return node;
}

// delete all nodes with keys in [startKey,endKey]
void RdbTree::deleteNodes ( collnum_t collnum ,
			    //key_t startKey , key_t endKey , bool freeData ) {
			    char *startKey , char *endKey , bool freeData ) {

	// sanity check
	if ( ! m_isWritable ) {
		log("db: Can not delete record from tree because "
		    "not writable 2. name=%s",m_dbname);
		return;
		//char *xx = NULL; *xx = 0;
	}

	// protect it all from writes again
	if ( m_useProtection ) unprotect ( );

	int32_t node = getNextNode ( collnum , startKey );
	while ( node >= 0 ) {
		//int32_t next = getNextNode ( node );
		if ( m_collnums[node] != collnum ) break;
		//if ( m_keys    [node] > endKey   ) return;
		if ( KEYCMP(m_keys,node,endKey,0,m_ks) > 0 ) break;
		deleteNode3 ( node , freeData );
		// rotation in setDepths() will cause him to be replaced
		// with one of his kids, unless he's a leaf node
		//node = next;
		node = getNextNode ( collnum , startKey );
	}

	// protect it all from writes again
	if ( m_useProtection ) protect ( );
}

// . deletes node i from the tree
// . i's parent should point to i's left or right kid
// . if i has no parent then his left or right kid becomes the new top node
void RdbTree::deleteNode3 ( int32_t i , bool freeData ) {
	// sanity check
	if ( ! m_isWritable ) {
		log("db: Can not delete record from tree because "
		    "not writable. name=%s",m_dbname);
		return;
		//char *xx = NULL; *xx = 0;
	}
	// must be saved from interrupts lest i be changed
	//if(g_intOff <= 0 && g_globalNiceness == 0 ) { char *xx=NULL;*xx=0; }

	// no deleting if we're saving
	if ( m_isSaving ) log("db: Can not delete record from tree because "
			      "saving tree to disk now.");
	// watch out for double deletes
	if ( m_parents[i] == -2 ) {
		log(LOG_LOGIC,"db: Caught double delete.");
		return;
	}
	// we need to be saved now
	m_needsSave = true;
	// debug step -- check chain from iparent down making sure that
	// just debug2 after every 10 deletes for speed
	//static int32_t ttt = 0;
	//if ( ttt++ == 100 ) { printTree(); ttt = 0; }
	// we have one less occupied node
	m_memOccupied -= m_overhead;
	// . free it now iff "freeIt" is true (default is true)
	// . m_data can be a NULL array if m_fixedDataSize is fixed to 0
	if ( /*freeData &&*/ m_data ) {
		int32_t dataSize = m_fixedDataSize;
		if ( dataSize == -1 ) dataSize = m_sizes[i];
		if ( m_ownData ) mfree ( m_data [i] , dataSize ,m_allocName);
		m_memAlloced  -= dataSize;
		m_memOccupied -= dataSize;
	}

	// protect it all from writes again
	char undo ;
	if ( m_useProtection ) { 
		if ( m_isProtected ) undo = 1; 
		else                 undo = 0;
		unprotect ( ); 
	}

	//fprintf(stderr,"headNode=%i,numUsed=%i, before deleting node #%i\n",
	//m_headNode,m_numUsedNodes,i);
	//printTree();
	// parent of i
	int32_t iparent ;
	int32_t jparent ;
	// j will be the node that replace node #i
	int32_t j = i;
	// . now find a node to replace node #i
	// . get a node whose key is just to the right or left of i's key
	// . get i's right kid
	// . then get that kid's LEFT MOST leaf-node descendant
	// . this little routine is stolen from getNextNode(i)
	// . try to pick a kid from the right the same % of time as from left
	if ( ( m_pickRight     && m_right[j] >= 0 ) || 
	     ( m_left[j]   < 0 && m_right[j] >= 0 )  ) {
		// try to pick a left kid next time
		m_pickRight = 0;
		// go to the right kid
		j = m_right [ j ];
		// now go left as much as we can
		while ( m_left [ j ] >= 0 ) j = m_left [ j ];
		// use node j (it's a leaf or has a right kid)
		goto gotReplacement;
	}
	// . now get the previous node if i has no right kid
	// . this little routine is stolen from getPrevNode(i)
	if ( m_left[j] >= 0 ) {
		// try to pick a right kid next time
		m_pickRight = 1;
		// go to the left kid
		j = m_left [ j ];
		// now go right as much as we can
		while ( m_right [ j ] >= 0 ) j = m_right [ j ];
		// use node j (it's a leaf or has a left kid)
		goto gotReplacement;
	}
	// . come here if i did not have any kids (i's a leaf node)
	// . get i's parent
	iparent = m_parents[i];
	// make i's parent, if any, disown him
	if ( iparent >= 0 ) {
		if   ( m_left[iparent] == i ) m_left [iparent] = -1;
		else                          m_right[iparent] = -1;
	}
	// node i now goes to the top of the list of vacated, available homes
	m_right[i] = m_nextNode;
	// m_nextNode now points to i
	m_nextNode = i;
	// his parent is -2 (god) cuz he's dead and available
	m_parents[i] = -2;
	// . if we were the head node then, since we didn't have any kids,
	//   the tree must be empty
	// . one less node in the tree
	m_numUsedNodes--;
	// update sign counts
	if ( KEYNEG(m_keys,i,m_ks) ) {
		m_numNegativeKeys--;
		//m_numNegKeysPerColl[m_collnums[i]]--;
		if ( m_rdbId >= 0 ) {
			CollectionRec *cr;
			cr = g_collectiondb.m_recs[m_collnums[i]];
			if(cr)cr->m_numNegKeysInTree[(unsigned char)m_rdbId]--;
		}
	}
	else {
		m_numPositiveKeys--;
		//m_numPosKeysPerColl[m_collnums[i]]--;
		if ( m_rdbId >= 0 ) {
			CollectionRec *cr;
			cr = g_collectiondb.m_recs[m_collnums[i]];
			if(cr)cr->m_numPosKeysInTree[(unsigned char)m_rdbId]--;
		}
	}
	// debug step -- check chain from iparent down making sure that
	//printTree();
	// debug2 msg
	//fprintf(stderr,"- #%"INT32" %"INT64" %"INT32"\n",i,m_keys[i].n0,iparent);
	// . reset the depths starting at iparent and going up until unchanged
	// . will balance at pivot nodes that need it
	if ( m_doBalancing ) setDepths ( iparent );

	// protect it all from writes again
	if ( m_useProtection && undo ) protect ( );

	// return if there are still people
	if ( m_numUsedNodes > 0 ) return;
	// otherwise tree must be empty
	m_headNode      = -1;
	// this will nullify our linked list of vacated, used homes
	m_nextNode      = 0;
	m_minUnusedNode = 0;
	// ensure these are right
	m_numNegativeKeys = 0;
	m_numPositiveKeys = 0;
	//m_numNegKeysPerColl[m_collnums[i]] = 0;
	//m_numPosKeysPerColl[m_collnums[i]] = 0;
	if ( m_rdbId >= 0 ) {
		//if ( ((unsigned char)m_rdbId)>=RDB_END){
		//char *xx=NULL;*xx=0; }
		CollectionRec *cr ;
		cr = g_collectiondb.m_recs[m_collnums[i]];
		if(cr){
			cr->m_numNegKeysInTree[(unsigned char)m_rdbId] = 0;
			cr->m_numPosKeysInTree[(unsigned char)m_rdbId] = 0;
		}
	}


	return;
	// . now replace node #i with node #j
	// . i should not equal j at this point
 gotReplacement:


	// . j's parent should take j's one kid
	// . that child should likewise point to j's parent
	// . j should only have <= 1 kid now because of our algorithm above
	// . if j's parent is i then j keeps his kid
	jparent = m_parents[j];
	if ( jparent != i ) {
		// parent:    if j is my left  kid, then i take j's right kid
		// otherwise, if j is my right kid, then i take j's left kid
		if ( m_left [ jparent ] == j ) {
			m_left  [ jparent ] = m_right [ j ];
			if (m_right[j]>=0) m_parents [ m_right[j] ] = jparent;
		}
		else {
			m_right [ jparent ] = m_left   [ j ];
			if (m_left [j]>=0) m_parents [ m_left[j] ] = jparent;
		}
	}

	// . j inherits i's children (providing i's child is not j)
	// . those children's parent should likewise point to j
	if ( m_left [i] != j ) {
		m_left [j] = m_left [i];
		if ( m_left[j] >= 0 ) m_parents[m_left [j]] = j;
	}
	if ( m_right[i] != j ) {
		m_right[j] = m_right[i];
		if ( m_right[j] >= 0 ) m_parents[m_right[j]] = j;
	}
	// j becomes the kid of i's parent, if any
	iparent = m_parents[i];
	if ( iparent >= 0 ) {
		if   ( m_left[iparent] == i ) m_left [iparent] = j;
		else                          m_right[iparent] = j;
	}
	// iparent may be -1
	m_parents[j] = iparent;

	// if i was the head node now j becomes the head node
	if ( m_headNode == i ) m_headNode = j;

	// . i joins the linked list of available used homes
	// . put it at the head of the list 
	// . "m_nextNode" is the head node of the linked list
	m_right[i]   = m_nextNode;
	m_nextNode   = i;
	// . i's parent should be -2 so we know it's unused in case we're
	//   stepping through the nodes linearly for dumping in RdbDump
	// . used in getListUnordered()
	m_parents[i] = -2;
	// we have one less used node
	m_numUsedNodes--;
	// update sign counts
	if ( KEYNEG(m_keys,i,m_ks) ) {
		m_numNegativeKeys--;
		//m_numNegKeysPerColl[m_collnums[i]]--;
		if ( m_rdbId >= 0 ) {
			//if( ((unsigned char)m_rdbId)>=RDB_END){char *xx=NULL;*xx=0; }
			CollectionRec *cr ;
			cr = g_collectiondb.m_recs[m_collnums[i]];
			if(cr)cr->m_numNegKeysInTree[(unsigned char)m_rdbId]--;
		}
	}
	else {
		m_numPositiveKeys--;
		//m_numPosKeysPerColl[m_collnums[i]]--;
		if ( m_rdbId >= 0 ) {
			//if( ((unsigned char)m_rdbId)>=RDB_END){char *xx=NULL;*xx=0; }
			CollectionRec *cr ;
			cr = g_collectiondb.m_recs[m_collnums[i]];
			if(cr)cr->m_numPosKeysInTree[(unsigned char)m_rdbId]--;
		}
	}
	// debug step -- check chain from iparent down making sure that
	// all kids don't have -2 for their parent... seems to be a rare bug
	//printTree();
	// debug msg
	//fprintf(stderr,"- #%"INT32" %"INT64" %"INT32"\n",i,m_keys[i].n0,iparent);
	// return if we don't have to balance
	if ( ! m_doBalancing ) {
		// protect it all from writes again
		if ( m_useProtection && undo ) protect ( );
		return;
	}
	// our depth becomes that of the node we replaced, unless moving j
	// up to i decreases the total depth, in which case setDepths() fixes
	m_depth [ j ] = m_depth [ i ];
	// debug msg
	//fprintf(stderr,"... replaced %"INT32" it with %"INT32" (-1 means none)\n",i,j);
	// . recalculate depths starting at old parent of j
	// . stops at the first node to have the correct depth
	// . will balance at pivot nodes that need it
	if ( jparent != i ) setDepths ( jparent );
	else                setDepths ( j );
	// TODO: register growTree with g_mem to free on demand
	// do a grow/shrink test and shrink if we need to
	//	return growTable ( );
	// done:
	// protect it all from writes again
	if ( m_useProtection && undo ) protect ( );
}

bool RdbTree::deleteKeys ( collnum_t collnum , char *keys , int32_t numKeys ) {
	// make a fake list
	RdbList list;
	int32_t size = m_ks * numKeys;
	list.set ( keys  ,
		   size  ,
		   keys  ,
		   size  ,
		   keys  ,
		   keys  ,
		   0     , // fixedDataSize
		   false ,
		   false ,
		   m_ks  );
	return deleteList ( collnum , &list , true );
}

// . TODO: speed up since keys are usually ordered (use getNextNode())
// . returns false if a key in list was not found
bool RdbTree::deleteList ( collnum_t collnum ,
			   RdbList *list , bool doBalancing ) {
	// sanity check
	if ( list->m_ks != m_ks ) { char *xx = NULL; *xx = 0; }
	// return if no non-empty nodes in the tree
	if ( m_numUsedNodes <= 0 ) return true;
	// reset before calling list->getCurrent*() functions
	list->resetListPtr();
	//key_t key;
	char key[MAX_KEY_BYTES];
	// bail if list is empty now
	if ( list->isEmpty() ) return true;
	// a key not found?
	bool allgood = true;
	// preserve state of balance
	bool balanced = m_doBalancing;
	// possibly turn off balancing (only turn on/off if it's already on)
	if ( m_doBalancing ) m_doBalancing = doBalancing;
	// disable mem protection
	if ( m_useProtection ) unprotect ( );
	//int32_t  dataSize;
 top:
	//key      = list->getCurrentKey      ( );
	list->getCurrentKey ( key );
	//dataSize = list->getCurrentDataSize ( );
	if ( deleteNode ( collnum , key , true /*freeData?*/) < 0 ) {
		//log("RdbTree::deleteList: key not found");
		allgood = false;
	}		

	// debug
	//log("db: delete %s",KEYSTR(key,m_ks));

	if ( list->skipCurrentRecord() ) goto top;
	// possibly restore balancing
	m_doBalancing = balanced;
	// enable protection again
	if ( m_useProtection ) protect ( );
	// return false if a key was not found
	return allgood;
}

// TODO: speed up since keys are usually ordered (use getNextNode())
void RdbTree::deleteOrderedList ( collnum_t collnum ,
				  RdbList *list , bool doBalancing ) {
	// return if no non-empty nodes in the tree
	if ( m_numUsedNodes <= 0 ) return ;
	// reset before calling list->getCurrent*() functions
	list->resetListPtr();
	//key_t key;
	char key [ MAX_KEY_BYTES ];
	// bail if list is empty now
	if ( list->isEmpty() ) return;

	//int32_t  dataSize;
	//key = list->getCurrentKey      ( );
	list->getCurrentKey ( key );
	// get the node whose keys is just <= key
	int32_t node = getPrevNode ( collnum , key );
	// preserve state of balance
	bool balanced = m_doBalancing;
	// possibly turn off balancing (only turn on/off if it's already on)
	if ( m_doBalancing ) m_doBalancing = doBalancing;
	// disable mem protection
	if ( m_useProtection ) unprotect ( );
 top:
	// bail if no nodes in tree left that have keys >= "key"
	if ( node == -1 ) goto done;
 top2:
	// . if key of node equals key, remove node and advance key and node
	// . this condition is usually the case, so check it first for speed
	//if ( m_keys [ node ] == key && m_collnums [ node ] == collnum ) {
	if ( KEYCMP(m_keys,node,key,0,m_ks)==0 && m_collnums[node] == collnum){
		// trim the node from the tree
		deleteNode3 ( node , true /*freeData?*/ );
		// get next node in tree
		node = getNextNode ( node ) ;
		// . point to next key in list to delete
		// . return if list exhausted
		if ( ! list->skipCurrentRecord() ) goto done;
		// reference that key
		//key = list->getCurrentKey() ;
		list->getCurrentKey ( key );
		goto top;
	}
	// bust out if done
	if ( m_collnums [ node ] > collnum ) goto done;
	// if node's key is < "key" advance node
	//if ( m_keys [ node ] < key ) {
	if ( KEYCMP(m_keys,node,key,0,m_ks)<0 ) {
		// get next node in tree
		node = getNextNode ( node ) ;
		goto top;
	}
	// . otherwise, we passed "key" so "key" must have not been in tree
	// . point to next key in list to delete
	// . return if list exhausted
	if ( ! list->skipCurrentRecord() ) goto done;
	// reference that key
	//key = list->getCurrentKey() ;
	list->getCurrentKey ( key ) ;
	goto top2;
 done:
	// possibly restore balancing
	m_doBalancing = balanced;

	// re-enable mem protection
	if ( m_useProtection ) protect ( );
}

#include "Spider.h"

// . this fixes the tree
// returns false if could not fix tree and sets g_errno, otherwise true

bool RdbTree::fixTree ( ) {
	// on error, fix the linked list
	//log("RdbTree::fixTree: tree was corrupted on disk?");
	log("db: Trying to fix tree for %s.",m_dbname);
	log("db: %"INT32" occupied nodes and %"INT32" empty "
	    "of top %"INT32" nodes.",
	    m_numUsedNodes , m_minUnusedNode - m_numUsedNodes ,
	    m_minUnusedNode );

	// loop through our nodes
	int32_t n = m_minUnusedNode;
	int32_t count = 0;
	// "clear" the tree as far as addNode() is concerned
	m_headNode      = -1;
	m_numUsedNodes  =  0;
	m_memAlloced    =  0;
	m_memOccupied   =  0;
	m_nextNode      =  0;
	m_minUnusedNode =  0; 
	//CollectionRec *recs = g_collectiondb.m_recs;
	int32_t           max  = g_collectiondb.m_numRecs;
	log("db: Valid collection numbers range from 0 to %"INT32".",max);

	bool isTitledb = false;
	if ( !strcmp(m_dbname,"titledb" ) ) isTitledb = true;
	bool isSpiderdb = false;
	if ( !strcmp(m_dbname,"spiderdb" ) ) isSpiderdb = true;

	// now re-add the old nods to the tree, they should not be overwritten
	// by addNode()
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// speed update
		if ( (i % 100000) == 0 ) 
			log("db: Fixing node #%"INT32" of %"INT32".",i,n);
		// skip if empty
		if ( m_parents[i] <= -2 ) continue;

		char *key = &m_keys[i*m_ks];
		if ( isTitledb && m_data[i] ) {
			char *data = m_data[i];
			int32_t ucompSize = *(int32_t *)data;
			if ( ucompSize < 0 || ucompSize > 100000000 ) {
				log("db: removing titlerec with uncompressed "
				     "size of %i from tree (docid=%"INT64"",
				    (int)ucompSize,
				    g_titledb.getDocIdFromKey((key_t *)key));
				continue;
			}
		}

		if ( isSpiderdb && m_data[i] &&
		     g_spiderdb.isSpiderRequest ( (SPIDERDBKEY *)key ) ) {
			char *data = m_data[i];
			data -= sizeof(SPIDERDBKEY);
			data -= 4;
			SpiderRequest *sreq ;
			sreq =(SpiderRequest *)data;
			if ( strncmp(sreq->m_url,"http",4) ) {
				log("db: removing spiderrequest bad url "
				    "%s from tree",sreq->m_url);
				//return false;
				continue;
			}
		}

		collnum_t cn = m_collnums[i];
		// verify collnum
		if ( cn <  0   ) continue;
		if ( cn >= max ) continue;
		// collnum of non-existent coll
		if ( m_rdbId>=0 && ! g_collectiondb.m_recs[cn] )
			continue;

		// now add just to set m_right/m_left/m_parent
		if ( m_fixedDataSize == 0 )
			addNode(cn,&m_keys[i*m_ks], NULL, 0 );
		else if ( m_fixedDataSize == -1 )
			addNode(cn,&m_keys[i*m_ks],m_data[i],m_sizes[i] );
		else 
			addNode(cn,&m_keys[i*m_ks],m_data[i],
				m_fixedDataSize);
		// count em
		count++;
	}

	log("db: Fix tree removed %"INT32" nodes for %s.",n - count,m_dbname);
	// esure it is still good
	if ( ! checkTree ( false , true ) )
		return log("db: Fix tree failed.");
	log("db: Fix tree succeeded for %s.",m_dbname);
	return true;
}

// returns false if tree had problem, true otherwise
bool RdbTree::checkTree ( bool printMsgs , bool doChainTest ) {
	// no writing to tree while we are checking, since we
	// do quickpolls, just make sure
	bool saved = m_isWritable;
	m_isWritable = false;
	// check it
	bool status = checkTree2 ( printMsgs , doChainTest );
	// put back
	m_isWritable = saved;
	return status;
}

bool RdbTree::checkTree2 ( bool printMsgs , bool doChainTest ) {

	int32_t hkp = 0;
	char useHalfKeys = false;

	// these guy always use a collnum of 0
	bool doCollRecCheck = true;
	if ( !strcmp(m_dbname,"catdb") ) doCollRecCheck = false;
	if ( !strcmp(m_dbname,"statsdb") ) doCollRecCheck = false;


	if ( !strcmp(m_dbname,"indexdb") ) useHalfKeys = true;
	if ( !strcmp(m_dbname,"datedb" ) ) useHalfKeys = true;
	if ( !strcmp(m_dbname,"tfndb"  ) ) useHalfKeys = true;
	if ( !strcmp(m_dbname,"linkdb" ) ) useHalfKeys = true;

	bool isTitledb = false;
	if ( !strcmp(m_dbname,"titledb" ) ) isTitledb = true;
	bool isSpiderdb = false;
	if ( !strcmp(m_dbname,"spiderdb" ) ) isSpiderdb = true;

	// now check parent kid correlations
	for ( int32_t i = 0 ; i < m_minUnusedNode ; i++ ) {
		// this thing blocks for 1.5 secs for indexdb
		// so do some quick polls!
		QUICKPOLL(MAX_NICENESS);
		// skip node if parents is -2 (unoccupied)
		if ( m_parents[i] == -2 ) continue;
		// all half key bits must be off in here
		if ( useHalfKeys && (m_keys[i*m_ks] & 0x02) ) {
			hkp++;
			// turn it off
			m_keys[i*m_ks] &= 0xfd;
		}
		// for posdb
		if ( m_ks == 18 &&(m_keys[i*m_ks] & 0x06) ) {
			char *xx=NULL;*xx=0; }

		if ( isTitledb && m_data[i] ) {
			char *data = m_data[i];
			int32_t ucompSize = *(int32_t *)data;
			if ( ucompSize < 0 || ucompSize > 100000000 ) {
				log("db: found titlerec with uncompressed "
				    "size of %i from tree",(int)ucompSize);
				return false;
			}
		}

		char *key = &m_keys[i*m_ks];
		if ( isSpiderdb && m_data[i] &&
		     g_spiderdb.isSpiderRequest ( (SPIDERDBKEY *)key ) ) {
			char *data = m_data[i];
			data -= sizeof(SPIDERDBKEY);
			data -= 4;
			SpiderRequest *sreq ;
			sreq =(SpiderRequest *)data;
			if ( strncmp(sreq->m_url,"http",4) ) {
				log("db: spiderrequest bad url "
				    "%s",sreq->m_url);
				return false;
			}
		}

		// bad collnum?
		if ( doCollRecCheck ) {
			collnum_t cn = m_collnums[i];
			if ( m_rdbId>=0 && 
			     (cn >= g_collectiondb.m_numRecs || cn < 0) )
				return log("db: bad collnum in tree");
			if ( m_rdbId>=0 && ! g_collectiondb.m_recs[cn] )
				return log("db: collnum is obsolete in tree");
		}

		// if no left/right kid it MUST be -1
		if ( m_left[i] < -1 )
			return log(
				   "db: Tree left kid < -1.");
		if ( m_left[i] >= m_numNodes )
			return log(
				   "db: Tree left kid is %"INT32" >= %"INT32".",
				   m_left[i],m_numNodes);
		if ( m_right[i] < -1 )
			return log(
				   "db: Tree right kid < -1.");
		if ( m_right[i] >= m_numNodes )
			return log(
				   "db: Tree left kid is %"INT32" >= %"INT32".",
				   m_right[i],m_numNodes);
		// check left kid
		if ( m_left[i] >= 0 && m_parents[m_left[i]] != i ) 
			return log(
				   "db: Tree left kid and parent disagree.");
		// then right kid
		if ( m_right[i] >= 0 && m_parents[m_right[i]] != i ) 
			return log(
				   "db: Tree right kid and parent disagree.");
		// MDW: why did i comment out the order checking?
		// check order
		if ( m_left[i] >= 0 &&
		     m_collnums[i] == m_collnums[m_left[i]] ) {
			char *key = &m_keys[i*m_ks];
			char *left = &m_keys[m_left[i]*m_ks];
			if ( KEYCMP(key,left,m_ks)<0) 
				return log("db: Tree left kid > parent %i",i);
			
		}
		if ( m_right[i] >= 0 &&
		     m_collnums[i] == m_collnums[m_right[i]] ) {
			char *key = &m_keys[i*m_ks];
			char *right = &m_keys[m_right[i]*m_ks];
			if ( KEYCMP(key,right,m_ks)>0) 
				return log("db: Tree right kid < parent %i "
					   "%s < %s",i,
					   KEYSTR(right,m_ks),
					   KEYSTR(key,m_ks) );
		}
		//g_loop.quickPoll(1, __PRETTY_FUNCTION__, __LINE__);
	}
	if ( hkp > 0 ) 
	       return log("db: Had %"INT32" half key bits on for %s.",hkp,m_dbname);
	// now return if we aren't doing active balancing
	if ( ! m_depth ) return true;
	// debug -- just always return now
	if ( printMsgs )logf(LOG_DEBUG,"***m_headNode=%"INT32", m_numUsedNodes=%"INT32"",
			      m_headNode,m_numUsedNodes);
	//CollectionRec *recs = g_collectiondb.m_recs;
	int32_t           max  = g_collectiondb.m_numRecs;
	// verify that parent links correspond to kids
	for ( int32_t i = 0 ; i < m_minUnusedNode ; i++ ) {
		// this thing blocks for 1.5 secs for indexdb
		// so do some quick polls!
		QUICKPOLL(MAX_NICENESS);
		// verify collnum
		collnum_t cn = m_collnums[i];
		if ( cn < 0 )
			return log("db: Got bad collnum in tree, %i.",cn);
		if ( cn > max )
			return log("db: Got too big collnum in tree. %i.",cn);
		// we do not want to delete these nodes from the tree yet
		// in case the collection was accidentally removed.
		//if ( ! recs[cn] )
		//	return log("db: Got bad collnum tree. %"INT32".",cn);
		int32_t P = m_parents [i];
		if ( P == -2 ) continue; // deleted node
		if ( P == -1 && i != m_headNode ) 
			return log("db: Tree node %"INT32" has "
				   "no parent.",i);
		// check kids
		if ( P>=0 && m_left[P] != i && m_right[P] != i ) 
			return log("db: Tree kids of node # %"INT32" "
				    "disowned him.",i);
		//g_loop.quickPoll(1, __PRETTY_FUNCTION__, __LINE__);
		// speedy tests continue
		if ( ! doChainTest ) continue;
		// ensure i goes back to head node
		int32_t j = i;
		int32_t loopCount = 0;
		while ( j >= 0 ) { 
			if ( j == m_headNode ) break;
			// sanity -- loop check
			if ( ++loopCount > 10000 ) 
				return log("db: tree had loop");
			j = m_parents[j];
		}
		if ( j != m_headNode ) 
			return log(
				   "db: Node # %"INT32" does not lead back to "
				   "head node.",i);
		if ( printMsgs ) {
		        char *k = &m_keys[i*m_ks];
			logf(LOG_DEBUG,"***node=%"INT32" left=%"INT32" rght=%"INT32" "
			    "prnt=%"INT32", depth=%"INT32" c=%"INT32" key=%s",
			    i,m_left[i],m_right[i],m_parents[i],
			    (int32_t)m_depth[i],(int32_t)m_collnums[i],
			     KEYSTR(k,m_ks));
			// assume linkdb
			//key192_t *kp = (key192_t *)k;
			//unsigned char hc = g_linkdb.getLinkerHopCount_uk(kp);
			//if ( hc ) { char *xx=NULL;*xx=0; }
		}
		//ensure depth
		int32_t newDepth = computeDepth ( i );
		if ( m_depth[i] != newDepth ) 
			return log("db: Tree node # %"INT32"'s depth "
				   "should be %"INT32".",i,newDepth);
	}
	if ( printMsgs ) logf(LOG_DEBUG,"---------------");
	// no problems found
	return true;
}

// . grow tree to "n" nodes
// . this will now actually grow from a current size to a new one
bool RdbTree::growTree ( int32_t nn , int32_t niceness ) {
	// if we're that size, bail
	if ( m_numNodes == nn ) return true;

	// old number of nodes
	int32_t on = m_numNodes;
	// some quick type info
	int32_t   k = m_ks;
	int32_t   d = sizeof(char *);

	//key_t *kp = NULL;
	char  *kp = NULL;
	int32_t  *lp = NULL;
	int32_t  *rp = NULL;
	int32_t  *pp = NULL;
	char **dp = NULL;
	int32_t  *sp = NULL;
	char  *tp = NULL;
	collnum_t *cp = NULL;

	// unprotect it all
	if ( m_useProtection ) unprotect ( );

	// do the reallocs
	int32_t cs = sizeof(collnum_t);
	cp =(collnum_t *)mrealloc (m_collnums, on*cs,nn*cs,m_allocName);
	if ( ! cp ) goto error;
	QUICKPOLL(niceness);
	kp = (char  *) mrealloc ( m_keys    , on*k , nn*k , m_allocName );
	if ( ! kp ) goto error;
	QUICKPOLL(niceness);
	lp = (int32_t  *) mrealloc ( m_left    , on*4 , nn*4 , m_allocName );
	if ( ! lp ) goto error;
	QUICKPOLL(niceness);
	rp = (int32_t  *) mrealloc ( m_right   , on*4 , nn*4 , m_allocName );
	if ( ! rp ) goto error;
	QUICKPOLL(niceness);
	pp = (int32_t  *) mrealloc ( m_parents , on*4 , nn*4 , m_allocName );
	if ( ! pp ) goto error;
	QUICKPOLL(niceness);

	// deal with data, sizes and depth arrays on a basis of need
	if ( m_fixedDataSize !=  0 ) {
		dp =(char **)mrealloc (m_data  , on*d,nn*d,m_allocName);
		if ( ! dp ) goto error;
		QUICKPOLL(niceness);
	}
	if ( m_fixedDataSize == -1 ) {
		sp =(int32_t  *)mrealloc (m_sizes , on*4,nn*4,m_allocName);
		if ( ! sp ) goto error;
		QUICKPOLL(niceness);
	}
	if ( m_doBalancing         ) {
		tp =(char  *)mrealloc (m_depth , on  ,nn  ,m_allocName);
		if ( ! tp ) goto error;
		QUICKPOLL(niceness);
	}

	// re-assign
	m_collnums= cp;
	m_keys    = kp;
	m_left    = lp;
	m_right   = rp;
	m_parents = pp;
	m_data    = dp;
	m_sizes   = sp;
	m_depth   = tp;

	// adjust memory usage
	m_memAlloced -= m_overhead * on;
	m_memAlloced += m_overhead * nn;
	// bitch an exit if too much
	if ( m_memAlloced > m_maxMem )
		return log("db: Trying to grow tree for %s to %"INT32", but max is "
			   "%"INT32". Consider changing gb.conf.",
			   m_dbname,m_memAlloced , m_maxMem );
	// base mem is mem that cannot be freed
	m_baseMem = m_overhead * nn ;
	// and the new # of nodes we have
	m_numNodes = nn;

	// protect it from writes
	if ( m_useProtection ) protect ( );
	QUICKPOLL(niceness);
	return true;

 error:
	char  *kk ;
	int32_t  *x  ;
	char  *s  ;
	char **p  ;
	collnum_t *ss;
	// . realloc back down if we need to
	// . downsizing should NEVER fail!
	if ( cp ) {
		ss = (collnum_t *)mrealloc ( cp , nn*cs , on*cs , m_allocName);
		if ( ! ss ) { char *xx = NULL; *xx = 0; }
		m_collnums = ss;
		QUICKPOLL(niceness);
	}
	if ( kp ) {
		kk = (char *)mrealloc ( kp, nn*k, on*k, m_allocName );
		if ( ! kk ) { char *xx = NULL; *xx = 0; }
		m_keys = kk;
		QUICKPOLL(niceness);
	}
	if ( lp ) {
		x = (int32_t *)mrealloc ( lp , nn*4 , on*4 , m_allocName );
		if ( ! x ) { char *xx = NULL; *xx = 0; }
		m_left = x;
		QUICKPOLL(niceness);
	}
	if ( rp ) {
		x = (int32_t *)mrealloc ( rp , nn*4 , on*4 , m_allocName );
		if ( ! x ) { char *xx = NULL; *xx = 0; }
		m_right = x;
		QUICKPOLL(niceness);
	}
	if ( pp ) {
		x = (int32_t *)mrealloc ( pp , nn*4 , on*4 , m_allocName );
		if ( ! x ) { char *xx = NULL; *xx = 0; }
		m_parents = x;
		QUICKPOLL(niceness);
	}
	if ( dp && m_fixedDataSize != 0 ) {
		p = (char **)mrealloc ( dp , nn*d , on*d , m_allocName );
		if ( ! p ) { char *xx = NULL; *xx = 0; }
		m_data = p;
		QUICKPOLL(niceness);
	}
	if ( sp && m_fixedDataSize == -1 ) {
		x = (int32_t *)mrealloc ( sp , nn*4 , on*4 , m_allocName );
		if ( ! x ) { char *xx = NULL; *xx = 0; }
		m_sizes = x;
		QUICKPOLL(niceness);
	}
	if ( tp && m_doBalancing ) {
		s = (char *)mrealloc ( tp , nn   , on   , m_allocName );
		if ( ! s ) { char *xx = NULL; *xx = 0; }
		m_depth = s;
		QUICKPOLL(niceness);
	}

	return log("db: Failed to grow tree for %s from %"INT32" to %"INT32" bytes: %s.",
		   m_dbname,on,nn,mstrerror(g_errno));
}

void RdbTree::protect ( int prot ) {
	// old number of nodes
	int32_t on = m_numNodes;
	gbmprotect ( m_collnums , on*sizeof(collnum_t) , prot );
	gbmprotect ( m_keys     , on*m_ks, prot );
	gbmprotect ( m_left     , on*4  , prot );
	gbmprotect ( m_right    , on*4  , prot );
	gbmprotect ( m_parents  , on*4  , prot );
	if ( m_data  ) gbmprotect ( m_data  , on*sizeof(char *) , prot );
	if ( m_sizes ) gbmprotect ( m_sizes , on*4 , prot );
	if ( m_depth ) gbmprotect ( m_depth , on   , prot );
}

void RdbTree::gbmprotect ( void *p , int32_t size , int prot ) {
	if ( ! p || size <= 0 ) return;
	// align on page
	//p = (p + PAGESIZE) & (PAGESIZE-1);
	char *np = ((char *)p + (8*1024));
	// mask out lower bits
	np = (char *)((PTRTYPE)np & ~((8*1024)-1));
	size -= (np-(char *)p);
	if ( size <= 0 ) return;
	// align size, too
	int32_t nsize = size & (~(8*1024-1));
	if ( nsize <= 0 ) return;
	if ( mprotect ( np , nsize , prot ) == -1 )
		log("db: mprotect (size=%"INT32"): %s.",nsize,mstrerror(errno));
	//if ( prot == (PROT_READ | PROT_WRITE) )
	//	log("db: unprotect: 0x%"XINT32" size=%"INT32"",(int32_t)np,nsize);
	//else
	//	log("db: protect: 0x%"XINT32" size=%"INT32"",(int32_t)np,nsize);
}

int32_t RdbTree::getMemOccupiedForList2 ( collnum_t collnum  ,
				       char      *startKey ,
				       char      *endKey   ,
				       int32_t      minRecSizes ,
				       int32_t      niceness ) {
	int32_t ne = 0;
	int32_t size = 0;
	int32_t i = getNextNode ( collnum , startKey ) ;
	while ( i  >= 0 ) {
		// breathe now... crap what if niceness 0 add to this tree?
		// can that happen?
		QUICKPOLL(niceness);
		// break out if we should
		//if ( m_keys    [i]  > endKey  ) break;
		if ( KEYCMP(m_keys,i,endKey,0,m_ks) > 0 ) break;
		if ( m_collnums[i] != collnum ) break;
		if ( size >= minRecSizes      ) break;
		// num elements
		ne++;
		// do we got data?
		if ( m_data ) {
			// is size fixed?
			if ( m_fixedDataSize >= 0 ) size += m_fixedDataSize;
			else                        size += m_sizes[i];
		}
		// add in key overhead
		size += m_ks;
		// add in dataSize overhead (-1 means variable data size)
		if ( m_fixedDataSize < 0 ) size += 4;
		// advance
		i = getNextNode ( i );
	}
	// that's it
	return size;
}

int32_t RdbTree::getMemOccupiedForList ( ) {
	int32_t mem = 0;
	if ( m_fixedDataSize >= 0 ) {
		mem += m_numUsedNodes * m_ks;
		mem += m_numUsedNodes * m_fixedDataSize;
		return mem;
	}
	// get total mem used by occupied nodes
	mem  = getMemOccupied() ;
	// remove left/right/parent for each used node (3 int32_ts)
	mem -= m_overhead * m_numUsedNodes;
	// but do include the key in the list, even though it's in the overhead
	mem += m_ks * m_numUsedNodes;
	// but don't include the dataSize in the overhead -- that's in list too
	mem -= 4 * m_numUsedNodes;
	// . remove m_sizes array if dataSize fixed
	// . no! this is included in the list
	//if ( m_fixedDataSize == -1 ) mem -= getNumUsedNodes() * 4;
	return mem;
}

// . returns false and sets g_errno on error
// . throw all the records in this range into this list
// . probably about 24-50 cycles per key we add
// . if this turns out to be bottleneck we can use hardcore RdbGet later
// . RdbDump should use this
bool RdbTree::getList ( collnum_t collnum ,
			char *startKey, char *endKey, int32_t minRecSizes ,
			RdbList *list , int32_t *numPosRecs , int32_t *numNegRecs ,
			bool useHalfKeys ,
			int32_t niceness ) {
	// reset the counts of positive and negative recs
	int32_t numNeg = 0;
	int32_t numPos = 0;
	if ( numNegRecs ) *numNegRecs = 0;
	if ( numPosRecs ) *numPosRecs = 0;
	// set *lastKey in case we have no nodes in the list
	//if ( lastKey ) *lastKey = endKey;
	// . set the start and end keys of this list
	// . set lists's m_ownData member to true
	list->reset();
	// got set m_ks first so the set ( startKey, endKey ) works!
	list->m_ks = m_ks;
	list->set              ( startKey , endKey );
	list->setFixedDataSize ( m_fixedDataSize   );
	list->setUseHalfKeys   ( useHalfKeys       );
	// bitch if list does not own his own data
	if ( ! list->getOwnData() ) {
		g_errno = EBADENGINEER;
		return log(LOG_LOGIC,"db: rdbtree: getList: List does not "
			   "own data");
	}
	// bail if minRecSizes is 0
	if ( minRecSizes == 0 ) return true;
	// return true if no non-empty nodes in the tree
	if ( m_numUsedNodes == 0 ) return true;

	// get first node >= startKey
	int32_t node = getNextNode ( collnum , startKey );
	if ( node < 0 ) return true;
	// if it's already beyond endKey, give up
	//if ( m_keys [ node ] > endKey ) return true;
	if ( KEYCMP ( m_keys,node,endKey,0,m_ks) > 0 ) return true;
	// or if we hit a different collection number
	if ( m_collnums [ node ] > collnum ) return true;
	// save lastNode for setting *lastKey
	int32_t lastNode = -1;
	// . how much space would whole tree take if we stored it in a list?
	// . this includes records that are deletes
	// . caller will often say give me 500MB for a fixeddatasize list
	//   that is heavily constrained by keys...
	int32_t growth = getMemOccupiedForList ( );

	// do not allocate whole tree's worth of space if we have a fixed
	// data size and a finite minRecSizes
	if ( m_fixedDataSize >= 0 && minRecSizes >= 0 ) {
		// only assign if we require less than minRecSizes of growth
		// because some callers set minRecSizes to 500MB!!
		int32_t ng = minRecSizes + m_fixedDataSize + m_ks ;
		if ( ng < growth && ng > minRecSizes ) growth = ng;
	}

	// raise to virtual inifinite if not constraining us
	if ( minRecSizes < 0 ) minRecSizes = 0x7fffffff;

	// . nail it down if titledb because metalincs was getting
	//   out of memory errors when getting a bunch of titleRecs
	// . only do this for titledb/spiderdb lookups since it can be slow
	//   to go through a million indexdb nodes.
	// . this was because minRecSizes was way too big... 16MB i think
	// . if they pass us a size-unbounded request for a fixed data size
	//   list then we should call this as well... as in Msg22.cpp's
	//   call to msg5::getList for tfndb.
	if ( m_fixedDataSize < 0 || minRecSizes >= 256*1024 ) //== 0x7fffffff )
		growth = getMemOccupiedForList2 ( collnum, startKey, endKey,
						  minRecSizes , niceness );

	// don't grow more than we need to
	//if ( minRecSizes < growth ) {
	//	growth = minRecSizes;
	//	// add in a smidgen for exceeding minRecSizes by a bit
	//	growth += 128;
	//	// add lots more for titledb/spiderdb/clusterdb
	//	if ( m_fixedDataSize == -1 ) growth += 10*1024;
	//}
	// debug msg
	//if ( growth > 1000 )
	//	log (LOG_DEBUG,"db: RdbTree::getList: growth=%"INT32". "
	//	     "minRecSizes=%"INT32" db=%s.",growth,minRecSizes,m_dbname);

	// grow the list now
	if ( ! list->growList ( growth ) ) 
		return log("db: Failed to grow list to %"INT32" bytes for storing "
			   "records from tree: %s.",growth,mstrerror(g_errno));
	// similar to above algorithm but we have data along with the keys
	int32_t dataSize;

	// if a niceness 0 msg4 tries to add to the tree, return ETRYAGAIN
	// if it is hitting this quickpoll. increment it as a count in
	// case we get quickpolled and call this function as niceness 0!
	//
	// i think we were getting a list for a doledb dump, and while
	// getting that list in Rdb::getList(), a quickpoll was called
	// to handle a msg4 addList request that had its niceness converted 
	// to 0. and it deleted a record from the tree that we had just read
	// from the tree and added to the list. so then when RdbDump.cpp
	// called deleteList() after dumping that list to disk, one of the
	// recs was no longer in the tree! that then caused a core. now we
	// don't core, but i think i fixed it here.
	m_gettingList++;

	// stop when we've hit or just exceed minRecSizes
	// or we're out of nodes
	for ( ; node >= 0 && list->getListSize() < minRecSizes ;
	      node = getNextNode ( node ) ) {
		// breathe when getting big lists for dumping
		// hopefully niceness 0 stuff will not add to this tree!
		QUICKPOLL(niceness);
		// stop before exceeding endKey
		//if ( m_keys [ node ] > endKey ) break;
		if ( KEYCMP (m_keys,node,endKey,0,m_ks) > 0 ) break;
		// or if we hit a different collection number
		if ( m_collnums [ node ] != collnum ) break;
		// if more recs were added to tree since we initialized the
		// list then grow the list to compensate so we do not end up
		// reallocating one key at a time.
		

		// add record to our list
		if ( m_fixedDataSize == 0 ) {

			// node #1518 and #1565 are the key ones
			//if ( m_ks == 18 ) {
			//	log("tree: adding node %"INT32" k=%s",node,
			//	    KEYSTR((unsigned char *)&m_keys[node*m_ks],
			//		   m_ks));
			//}

			if ( ! list->addRecord(&m_keys[node*m_ks],0,NULL)) {
				m_gettingList--;
				return log("db: Failed to add record "
					   "to tree list for %s: %s. "
					   "Fix the growList algo.",
					   m_dbname,mstrerror(g_errno));
			}
		}
		else {
			// get dataSize if not fixed
			if ( m_fixedDataSize == -1 ) dataSize = m_sizes[node];
			// otherwise, it's fixed
			else dataSize = m_fixedDataSize;
			// . spiderdb is special
			// . RdbDump.cpp "deletes" nodes from the spiderdb
			//   tree by NULLifying the data but leaving the
			//   dataSize the way it was.
			// . so when it "dedups" a spiderdb rec in the tree
			//   it just sets it data ptr to NULL
			// . MDW: does this still apply? probably not!!!
			//if ( ! m_data[node] && dataSize ) continue;
			// RdbDump sets m_data[x] to -1 to indicate that a node was deleted
			// from the spiderdb tree because it was "deduped" because it was
			// a dup or it was already in tfndb.
			//if ( m_data[node] == (char *)-1 ) continue;
			// point to key
			char *key = &m_keys[node*m_ks];
			// do not allow negative keys to have data, or
			// at least ignore it! let's RdbList::addRecord()
			// core dump on us!
			if ( (key[0] & 0x01) == 0x00 ) dataSize = 0;
			// sanity check, break if 0 > titleRec > 100MB, 
			// it's probably corrupt
			//if (m_dbname && m_dbname[0]=='t' && dataSize >= 4 && 
			//     (*((int32_t *)m_data[node]) > 100000000 || 
			//      *((int32_t *)m_data[node]) < 0 ) ) {
			//	char *xx = NULL; *xx = 0; }
			// add the key and data
			if ( ! list->addRecord ( key,//&m_keys[node*m_ks] ,
						 dataSize     ,
						 m_data[node] ) ) {
				m_gettingList--;
				return log("db: Failed to add record "
					   "to tree list for %s: %s. "
					   "Fix the growList algo.",
					   m_dbname,mstrerror(g_errno));
			}
			// debug msg for detecting tagdb corruption
			/*
			if ( m_dbname && 
			     m_dbname[0]=='t' && 
			     m_dbname[1] == 'a' && 
			     dataSize >= 4 ) {
				int32_t back = dataSize + m_ks + 4;
				char *rec = list->m_list+list->m_listSize-back;
				Tag *tag = (Tag *)rec;
				logf(LOG_DEBUG,
				     "tree: "
				     "getting node #%"INT32" with data ptr at %"UINT32" "
				     "and data size of %"INT32" into a list.",
				     node,(int32_t)m_data[node],dataSize);
				// detect tagdb corruption
				if ( tag->m_bufSize < 0 ||
				     tag->m_bufSize > 3000 ) {
					char *xx=NULL;*xx=0; }
			}
			*/
		}
		// count negative and positive recs
		//if ( ((m_keys[node].n0) & 0x01) == 0 ) numNeg++;
		//else                                   numPos++;
		// we are little endian
		if ( KEYNEG(m_keys,node,m_ks) ) numNeg++;
		else                            numPos++;
		// save lastNode for setting *lastKey
		lastNode = node;
		// advance to next node
		//node = getNextNode ( node );
	}

	// allow msg4 to add/delete to/from this tree again
	m_gettingList--;

	// set counts to pass back
	if ( numNegRecs ) *numNegRecs = numNeg;
	if ( numPosRecs ) *numPosRecs = numPos;
	// . we broke out of the loop because either:
	// . 1. we surpassed endKey OR
	// . 2. we hit or surpassed minRecSizes
	// . constrain the endKey of the list to the key of "node" minus 1
	// . "node" should be the next node we would have added to this list
	// . if "node" is < 0 then we can keep endKey set high the way it is
	//if ( node >= 0 ) {
	//key_t newEndKey = m_keys[node];
	//newEndKey -= (uint32_t) 1 ;
	//list->set ( startKey , newEndKey );
	//}
	// record the last key inserted into the list
	if ( lastNode >= 0 ) 
		list->setLastKey ( &m_keys[lastNode*m_ks] );
	// reset the list's endKey if we hit the minRecSizes barrier cuz
	// there may be more records before endKey than we put in "list"
	if ( list->getListSize() >= minRecSizes && lastNode >= 0 ) {
		// use the last key we read as the new endKey
		//key_t newEndKey = m_keys[lastNode];
		char newEndKey[MAX_KEY_BYTES];
		KEYSET(newEndKey,&m_keys[lastNode*m_ks],m_ks);
		// . if he's negative, boost new endKey by 1 because endKey's
		//   aren't allowed to be negative
		// . we're assured there's no positive counterpart to him 
		//   since Rdb::addRecord() doesn't allow both to exist in
		//   the tree at the same time
		// . if by some chance his positive counterpart is in the
		//   tree, then it's ok because we'd annihilate him anyway,
		//   so we might as well ignore him
		//if (((newEndKey.n0) & 0x01) == 0x00 ) 
		//	newEndKey += (uint32_t)1;
		// we are little endian
		if ( KEYNEG(newEndKey,0,m_ks) ) KEYADD(newEndKey,1,m_ks);
		// if we're using half keys set his half key bit
		//if ( useHalfKeys ) newEndKey.n0 |= 0x02;
		if ( useHalfKeys ) KEYOR(newEndKey,0x02);
		// tell list his new endKey now
		list->set ( startKey , newEndKey );
	}
	// reset list ptr to point to first record
	list->resetListPtr();

	//if ( m_ks == 24 ) {
	//	//checkTree ( true , true );
	//	list->checkList_r(false,true,RDB_LINKDB);//POSDB);
	//}

	// if list is using less than 90% of it's mem, shrink it
	//if ( 100*list->getListSize() > list->getListMaxSize()*90 ) {
	//	// shrink the list
	//	list->growList ( list->getListSize() );
	//	// clear g_errno if there was an error
	//	g_errno = 0;
	//}
	// success
	return true;
}

// . return false on error (out of memory in list)
// . don't order by keys, order by node #
// . used for saving a tree to disk temporarily so it can be re-loaded
//   w/o totally unbalancing the tree
// . "*lastNode" is last node # in the list
// . we set *lastNode to -1 if that's all folks
/*
bool RdbTree::getListUnordered ( int32_t startNode , int32_t minRecSizes ,
				 RdbList *list  , int32_t *nextNode ) {
	// assume no nodes from startNode onward in this tree
	*nextNode = -1;
	// reset the list
	list->reset();
	list->setFixedDataSize ( m_fixedDataSize );
	// return true if no non-empty nodes in the tree
	if ( m_numUsedNodes == 0 ) return true;
	// . grow list to minRecSizes or size of tree, whichever is smallest
	// . how much space would whole tree take if we stored it in a list?
	// . this includes records that are deletes
	int32_t growth = getMemOccupiedForList ( );
	// don't grow more than we need to
	if ( minRecSizes < growth ) growth = minRecSizes;
	// grow the list now
	if ( ! list->growList ( growth ) ) 
		return log("db: Failed to grow list to %"INT32" bytes for storing "
			   "records from tree: %s.",growth,mstrerror(g_errno));
	// mdw fixed, this. it was node = 0 so we couldn't dump all of tree!!!
	int32_t node = startNode ;

	int32_t  dataSize;
	char *data;

	while ( node < m_minUnusedNode ) {
		// continue if this node is empty
		if ( m_parents [ node ] == -2 ) { node++; continue; }
		// get the data/dataSize
		if ( m_fixedDataSize == -1 ) dataSize = m_sizes[node];
		else                         dataSize = m_fixedDataSize;
		if ( m_fixedDataSize == 0  ) data = NULL;
		else                         data = m_data[node];
		// don't exceed the specified buf size
		minRecSizes -= (m_ks + dataSize);
		if ( m_fixedDataSize == -1 ) minRecSizes -= 4;
		if ( minRecSizes < 0 ) break;
		// . add to the list
		// . return false on error
		if ( ! list->addRecord ( m_keys[node], dataSize , data ) )
			return log("db: Failed to add record "
				   "to tree list for %s: %s.",
				   m_dbname,mstrerror(g_errno));
		// goto next node
		node++;
	}
	// . record the next node to be added into the list 
	// . iff there are more nodes available
	// . otherwise, leave it set to -1 so the caller knows that's it
	if ( node < m_minUnusedNode ) *nextNode = node;
	return true;
}
*/

// . this just estimates the size of the list 
// . the more balanced the tree the better the accuracy
// . this now returns total recSizes not # of NODES like it used to
//   in [startKey, endKey] in this tree
// . if the count is < 200 it returns an EXACT count
// . right now it only works for dataless nodes (keys only)
int32_t RdbTree::getListSize ( collnum_t collnum ,
			    //key_t  startKey , key_t endKey  , 
			    //key_t *minKey   , key_t *maxKey ) {
			    char *startKey , char *endKey  , 
			    char *minKey   , char *maxKey ) {
	// make these as benign as possible
	//if ( minKey ) *minKey = endKey;
	//if ( maxKey ) *maxKey = startKey;
	if ( minKey ) KEYSET ( minKey , endKey   , m_ks );
	if ( maxKey ) KEYSET ( maxKey , startKey , m_ks );
	// get order of a key as close to startKey as possible
	int32_t order1 = getOrderOfKey ( collnum , startKey , minKey );
	// get order of a key as close to endKey as possible
	int32_t order2 = getOrderOfKey ( collnum , endKey , maxKey );
	// how many recs?
	int32_t size = order2 - order1;
	// . if enough, return
	// . NOTE minKey/maxKey may be < or > startKey/endKey
	// . return an estimated list size
	if ( size > 200 ) return size * m_ks;
	// . otherwise, count exactly
	// . reset size and get the initial node
	size = 0;
	int32_t n = getPrevNode ( collnum , startKey );
	// return 0 if no nodes in that key range
	if ( n < 0 ) return 0;
	// skip to next node if this one is < startKey
	//if ( m_keys[n] < startKey ) n = getNextNode ( n );
	if ( KEYCMP(m_keys,n,startKey,0,m_ks)<0) n = getNextNode(n);
	// or collnum
	if ( m_collnums[n] < collnum ) n = getNextNode ( n );
	// loop until we run out of nodes or one breeches endKey
	//while ( n > 0 && m_keys[n] <= endKey && m_collnums[n] == collnum ) {
	while ( n>0 && KEYCMP(m_keys,n,endKey,0,m_ks)<=0 && 
	m_collnums[n]==collnum){
		size++;
		n = getNextNode(n);
	}
	// this should be an exact list size (actually # of nodes)
	return size * m_ks;
}

// . returns a number from 0 to m_numUsedNodes-1
// . represents the ordering of this key in that range
// . *retKey is the key that has the returned order
// . *retKey gets as close to "key" as it can
// . returns # of NODES
//int32_t RdbTree::getOrderOfKey ( collnum_t collnum , key_t key , key_t *retKey){
int32_t RdbTree::getOrderOfKey ( collnum_t collnum , char *key , char *retKey ) {
	if ( m_numUsedNodes <= 0 ) return 0;
	int32_t i     = m_headNode;
	// estimate the depth of tree if not balanced
	int32_t d     = getTreeDepth()   ;
	// TODO: WARNING: ensure d-1 not >= 32 !!!!!!!!!!!!!!!!!
	int32_t step  = 1 << (d-1);
	int32_t order = step;
	while ( i != -1 ) {
		//if ( retKey ) *retKey = m_keys[i];
		if ( retKey ) KEYSET ( retKey , &m_keys[i*m_ks] , m_ks );
		step /= 2;
		if ( collnum < m_collnums[i] ||
		     //(collnum == m_collnums[i] && key <  m_keys[i]) ) {
		     (collnum==m_collnums[i] &&KEYCMP(key,0,m_keys,i,m_ks)<0)){
			i = m_left [i]; 
			if ( i >= 0 ) order -= step;
			continue;
		}
		if ( collnum > m_collnums[i] ||
		     //(collnum == m_collnums[i] && key >  m_keys[i]) ) {
		     (collnum==m_collnums[i] &&KEYCMP(key,0,m_keys,i,m_ks)>0)){
			i = m_right[i]; 
			if ( i >= 0 ) order += step;
			continue;
		}
		break;
        }
	// normalize order since tree probably has less then 2^d nodes
	int64_t normOrder = 
		(int64_t) order          * 
		(int64_t) m_numUsedNodes / 
		(int64_t) ((1 << d) -1)  ;
	return (int32_t) normOrder;
}

int32_t RdbTree::getTreeDepth ( ) {
	// no problem if we're balanced
	if ( m_doBalancing ) return m_depth [ m_headNode ];
	// . otherwise compute: take log2(m_numUsedNodes)
	// . get highest bit on in m_numUsedNodes
	int32_t n = m_numUsedNodes;
	int32_t depth = 0;
	for ( int32_t i = 0 ; i < 32; i++ ) {
		if ( n & 0x01 ) depth = i;
		n >>= 1;
	}
	return depth + 1;
}


// . recompute depths of nodes starting at i and ascending the tree
// . call rotateRight/Left() when depth of children differs by 2 or more
void RdbTree::setDepths ( int32_t i ) {

	// inc the depth of all parents if it changes for them
	while ( i >= 0 ) {
		// . compute the new depth for node i
		// . get depth of left kid
		// . left/rightDepth is depth of subtree on left/right
		int32_t leftDepth  = 0;
		int32_t rightDepth = 0;
		if ( m_left [i] >= 0 ) leftDepth  = m_depth [ m_left [i] ] ;
		if ( m_right[i] >= 0 ) rightDepth = m_depth [ m_right[i] ] ;
		// . get the new depth for node i
		// . add 1 cuz we include ourself in our m_depth
		int32_t newDepth ;
		if ( leftDepth > rightDepth ) newDepth = leftDepth  + 1;
		else                          newDepth = rightDepth + 1;
		// if the depth did not change for i then we're done
		int32_t oldDepth = m_depth[i] ;
		// set our new depth
		m_depth[i] = newDepth;
		// diff can be -2, -1, 0, +1 or +2
		int32_t diff = leftDepth - rightDepth;
		// . if it's -1, 0 or 1 then we don't need to balance
		// . if rightside is deeper rotate left, i is the pivot
		// . otherwise, rotate left
		// . these should set the m_depth[*] for all nodes needing it
		if      ( diff == -2 ) i = rotateLeft  ( i );
		else if ( diff ==  2 ) i = rotateRight ( i );
		// . return if our depth was ultimately unchanged
		// . i may have change if we rotated, but same logic applies
		if ( m_depth[i] == oldDepth ) break;
		// debug msg
		//fprintf (stderr,"changed node %"INT32"'s depth from %"INT32" to %"INT32"\n",
		//i,oldDepth,newDepth);
		// get his parent to continue the ascension
		i = m_parents [ i ];
	}
	// debug msg
	//printTree();
}

/*
// W , X and B are SUBTREES.
// B's subtree was 1 less in depth than W or X, then a new node was added to
// W or X triggering the imbalance.
// However, if B gets deleted W and X can be the same size.
//
// Right rotation if W subtree depth is >= X subtree depth:
//
//          A                N
//         / \              / \
//        /   \            /   \
//       N     B   --->   W     A
//      / \                    / \
//     W   X                  X   B
//
// Right rotation if W subtree depth is <  X subtree depth:
//          A                X
//         / \              / \
//        /   \            /   \
//       N     B   --->   N     A
//      / \              / \   / \
//     W   X            W   Q T   B
//        / \                 
//       Q   T               
*/
// . we come here when A's left subtree is deeper than it's right subtree by 2
// . this rotation operation causes left to lose 1 depth and right to gain one
// . the type of rotation depends on which subtree is deeper, W or X
// . W or X must deeper by the other by exactly one
// . if they were equal depth then how did adding a node inc the depth?
// . if their depths differ by 2 then N would have been rotated first!
// . the parameter "i" is the node # for A in the illustration above
// . return the node # that replaced A so the balance() routine can continue
// . TODO: check our depth modifications below
int32_t RdbTree::rotateRight ( int32_t i ) {
	//fprintf(stderr,"rotateRight: pivot = %"INT32"\n",i);
	return rotate ( i , m_left , m_right );
}

// . i just swapped left with m_right
int32_t RdbTree::rotateLeft ( int32_t i ) {
	//fprintf(stderr,"rotateLeft: pivot = %"INT32"\n",i);
	return rotate ( i , m_right , m_left );
}

int32_t RdbTree::rotate ( int32_t i , int32_t *left , int32_t *right ) {
	// i's left kid's right kid takes his place
	int32_t A = i;
	int32_t N = left  [ A ];
	int32_t W = left  [ N ];
	int32_t X = right [ N ];
	int32_t Q = -1;
	int32_t T = -1;
	if ( X >= 0 ) {
		Q = left  [ X ];
		T = right [ X ];
	}
	// let AP be A's parent
	int32_t AP = m_parents [ A ];
	// whose the bigger subtree, W or X? (depth includes W or X itself)
	int32_t Wdepth = 0;
	int32_t Xdepth = 0;
	if ( W >= 0 ) Wdepth = m_depth[W];
	if ( X >= 0 ) Xdepth = m_depth[X];
	// debug msg
	//fprintf(stderr,"A=%"INT32" AP=%"INT32" N=%"INT32" W=%"INT32" X=%"INT32" Q=%"INT32" T=%"INT32" "
	//"Wdepth=%"INT32" Xdepth=%"INT32"\n",A,AP,N,W,X,Q,T,Wdepth,Xdepth);
	// goto Xdeeper if X is deeper
	if ( Wdepth < Xdepth ) goto Xdeeper;
	// N's parent becomes A's parent
	m_parents [ N ] = AP;
	// A's parent becomes N
	m_parents [ A ] = N;
	// X's parent becomes A
	if ( X >= 0 ) m_parents [ X ] = A;
	// A's parents kid becomes N
	if ( AP >= 0 ) {
		if ( left [ AP ] == A ) left  [ AP ] = N;
		else                    right [ AP ] = N;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node from %"INT32" to %"INT32"\n",
		//m_headNode,N);
		m_headNode = N;
	}
	// N's right kid becomes A
	right [ N ] = A;
	// A's left  kid becomes X		
	left  [ A ] = X;
	// . compute A's depth from it's X and B kids
	// . it should be one less if Xdepth smaller than Wdepth
	// . might set m_depth[A] to computeDepth(A) if we have problems
	if ( Xdepth < Wdepth ) m_depth [ A ] -= 2;
	else                   m_depth [ A ] -= 1;
	// N gains a depth iff W and X were of equal depth
	if ( Wdepth == Xdepth ) m_depth [ N ] += 1;
	// now we're done, return the new pivot that replaced A
	return N;
	// come here if X is deeper
 Xdeeper:
	// X's parent becomes A's parent
	m_parents [ X ] = AP;
	// A's parent becomes X
	m_parents [ A ] = X;
	// N's parent becomes X
	m_parents [ N ] = X;
	// Q's parent becomes N
	if ( Q >= 0 ) m_parents [ Q ] = N;
	// T's parent becomes A
	if ( T >= 0 ) m_parents [ T ] = A;
	// A's parent's kid becomes X
	if ( AP >= 0 ) {
		if ( left [ AP ] == A ) left  [ AP ] = X;
		else	                right [ AP ] = X;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node2 from %"INT32" to %"INT32"\n",
		//m_headNode,X);
		m_headNode = X;
	}
	// A's left     kid becomes T
	left  [ A ] = T;
	// N's right    kid becomes Q
	right [ N ] = Q;
	// X's left     kid becomes N
	left  [ X ] = N;
	// X's right    kid becomes A
	right [ X ] = A;
	// X's depth increases by 1 since it gained 1 level of 2 new kids
	m_depth [ X ] += 1;
	// N's depth decreases by 1
	m_depth [ N ] -= 1;
	// A's depth decreases by 2
	m_depth [ A ] -= 2; 
	// now we're done, return the new pivot that replaced A
	return X;
}

// . depth of subtree with i as the head node
// . includes i, so minimal depth is 1
int32_t RdbTree::computeDepth ( int32_t i ) {
	int32_t leftDepth  = 0;
	int32_t rightDepth = 0;
	if ( m_left [i] >= 0 ) leftDepth  = m_depth [ m_left [i] ] ;
	if ( m_right[i] >= 0 ) rightDepth = m_depth [ m_right[i] ] ;
	// . get the new depth for node i
	// . add 1 cuz we include ourself in our m_depth
	if ( leftDepth > rightDepth ) return leftDepth  + 1;
	else                          return rightDepth + 1;  
}	


// . a quick way to add a list of sorted keys (no data)...
// . will take care of positive/negative key annihilations
// . returns false and sets g_errno on error
/*
bool RdbTree::addSortedKeys ( key_t *keys , int32_t numKeys ) {
	// do we have enough room?
	if ( m_numUsedNodes + numKeys >= m_numNodes) { 
		g_errno = ENOMEM; return false; }
	// add one key at a time
	int32_t x = 0;
	// some vars
	key_t k;
	int32_t  iparent ;
	int32_t  rightGuy;
	int32_t  i;
 loop:
	// bail if x is exhausted
	if ( x >= numKeys ) return true;
	// get the xth key
	k = keys[x];
	// point x to next key
	x++;
	// this is -1 iff there are no nodes used in the tree
	i = m_headNode;
	// . find the parent of node i and call it "iparent"
	// . if a node exists with our key then replace it
	while ( i != -1 ) {
		iparent = i;
		if      ( key < m_keys[i] ) i = m_left [i]; 
		else if ( key > m_keys[i] ) i = m_right[i]; 
		else    goto replaceIt; 
	}
	// . this overhead is key/left/right/parent
	// . we inc it by the data and sizes array if we need to below
	m_memOccupied += m_overhead;
	// point i to the next available node
	i = m_nextNode;
	// if we're the first node we become the head node and our parent is -1
	if ( m_numUsedNodes == 0 ) {
		m_headNode =  i;
		iparent    = -1;
	}
	// stick ourselves in the next available node, "m_nextNode"
	m_keys    [ i ] = key;
	m_parents [ i ] = iparent;
	// add the key
	// set the data and size only if we need to
	if ( m_fixedDataSize != 0 ) {
		// ack used and occupied mem
		m_memAlloced  += dataSize ; 
		m_memOccupied += dataSize ;
	}
	// make our parent, if any, point to us
	if ( iparent >= 0 ) {
		if ( key < m_keys[iparent] ) m_left [iparent] = i;
		else                         m_right[iparent] = i;
	}
	// . the right kid of an empty node is used as a linked list of
	//   empty nodes formed by deleting nodes
	// . we keep the linked list so we can re-used these vacated nodes
	rightGuy = m_right [ i ];
	// our kids are -1 (none)
	m_left  [ i ] = -1;
	m_right [ i ] = -1;
	// . if we weren't recycling a node then advance to next
	// . m_minUnusedNode is the lowest node number that was never filled
	//   at any one time in the past
	// . you might call it the brand new housing district
	if ( m_nextNode == m_minUnusedNode ) {m_nextNode++; m_minUnusedNode++;}
	// . otherwise, we're in a linked list of vacated used houses
	// . we have a linked list in the right kid
	// . make sure the new head doesn't have a left
	else {
		// point m_nextNode to the next available used house, if any
		if ( rightGuy >= 0 ) m_nextNode = rightGuy;
		// otherwise point it to the next brand new house
		else  m_nextNode = m_minUnusedNode;
	}
	// we have one more used node
	m_numUsedNodes++;
	// if we don't have to balance return i now
	if ( ! m_doBalancing ) return i;
	// our depth is now 1 since we're a leaf node (we include ourself)
	m_depth [ i ] = 1;
	// . reset depths starting at i's parent and ascending the tree
	// . will balance if child depths differ by 2 or more
	setDepths ( iparent );
	// return the node number of the node we occupied
	return i; 
}
*/

// how balanced is this tree? = #nodes w/ right kids / # node w/ left
// the multiplied by 100. invereted to make smaller than 100.
int32_t RdbTree::getBalancePercent() {
	// count nodes w/ left kids and nodes w/ right kids
	int32_t numRight = 0;
	int32_t numLeft  = 0;
	for ( int32_t i = 0 ; i < m_minUnusedNode ; i++ ) {
		// skip nuked nodes
		if ( m_parents[i] == -2 ) continue;
		if ( m_left[i]  >= 0 ) numLeft++;
		if ( m_right[i] >= 0 ) numRight++;
	}
	// ensure these not zero
	numRight++;
	numLeft++;
	// . the ratio
	// . flip if top heavy
	int32_t p;
	if ( numLeft < numRight ) p = (numLeft  * 100) / numRight;
	else                      p = (numRight * 100) / numLeft;
	// return the percent. from 0 to 100%.
	return p;
}

#define BLOCK_SIZE 10000

static void *saveWrapper      ( void *state , ThreadEntry *t ) ;
static void threadDoneWrapper ( void *state , ThreadEntry *t ) ;

// . caller should call f->set() himself
// . we'll open it here
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool RdbTree::fastSave ( char    *dir       ,
			 char    *dbname    ,
			 bool     useThread ,
			 void    *state     ,
			 void    (* callback) (void *state) ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// we do not need a save
	if ( ! m_needsSave ) return true;
	// return true if already in the middle of saving
	if ( m_isSaving ) return false;
	// note it
	logf(LOG_INFO,"db: Saving %s/%s-saved.dat",dir,dbname);
	// save parms
	//m_saveFile = f;
	strcpy ( m_dir , dir );
	//m_dbname   = dbname;
	// sanity check
	if ( dbname && strcmp(dbname,m_dbname) ) {
		log("db: tree dbname mismatch.");
		char *xx=NULL;*xx=0;
	}
	m_state    = state;
	m_callback = callback;
	// assume no error
	m_saveErrno = 0;
	// no adding to the tree now
	m_isSaving = true;
	// skip thread call if we should
	if ( ! useThread ) goto skip;
	// make this a thread now
	if ( g_threads.call ( SAVETREE_THREAD   , // threadType
			      1                 , // niceness
			      this              , // top 4 bytes must be cback
			      threadDoneWrapper ,
			      saveWrapper   ) ) return false;
	// if it failed
	if ( ! g_threads.m_disabled ) 
		log("db: Thread creation failed. Blocking while saving tree. "
		    "Hurts performance.");
 skip:
	// this returns false and sets g_errno on error
	fastSave_r ();
	// store save error into g_errno
	g_errno = m_saveErrno;
	// resume adding to the tree
	m_isSaving = false;
	// we do not need to be saved now?
	m_needsSave = false;
	// we did not block
	return true;
}

void *saveWrapper ( void *state , ThreadEntry *t ) {
	// get this class
	RdbTree *THIS = (RdbTree *)state;
	// this returns false and sets g_errno on error
	THIS->fastSave_r();
	// now exit the thread, bogus return
	return NULL;
}

// we come here after thread exits
void threadDoneWrapper ( void *state , ThreadEntry *t ) {
	// get this class
	RdbTree *THIS = (RdbTree *)state;
	// store save error into g_errno
	g_errno = THIS->m_saveErrno;
	// . resume adding to the tree
	// . this will also allow other threads to be queued
	// . if we did this at the end of the thread we could end up with
	//   an overflow of queued SAVETHREADs
	THIS->m_isSaving = false;
	// we do not need to be saved now?
	THIS->m_needsSave = false;
	// g_errno should be preserved from the thread so if fastSave_r()
	// had an error it will be set
	if ( g_errno )
		log("db: Had error saving tree to disk for %s: %s.",
		    THIS->m_dbname,mstrerror(g_errno));
	else
		// log it
		log("db: Done saving %s/%s-saved.dat (wrote %"INT64" bytes)",
		    THIS->m_dir,THIS->m_dbname,THIS->m_bytesWritten);
	// . call callback
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
}

// . returns false and sets g_errno on error
// . NO USING g_errno IN A DAMN THREAD!!!!!!!!!!!!!!!!!!!!!!!!!
bool RdbTree::fastSave_r() {
	if ( g_conf.m_readOnlyMode ) return true;
	// recover the file
	//BigFile *f = m_saveFile;
	// open it up
	//if ( ! f->open ( O_RDWR | O_CREAT ) ) 
	//	return log("RdbTree::fastSave_r: %s",mstrerror(g_errno));
	// cannot use the BigFile class, since we may be in a thread and it 
	// messes with g_errno
	//char *s = m_saveFile->getFilename();
	char s[1024];
	sprintf ( s , "%s/%s-saving.dat", m_dir , m_dbname );
	int fd = ::open ( s , 
			  O_RDWR | O_CREAT | O_TRUNC ,
			  getFileCreationFlags() );
			  // S_IRUSR | S_IWUSR | 
			  // S_IRGRP | S_IWGRP | S_IROTH);
	if ( fd < 0 ) {
		m_saveErrno = errno;
		return log("db: Could not open %s for writing: %s.",
			   s,mstrerror(errno));
	}

 redo:
	// verify the tree
	if ( g_conf.m_verifyWrites ) {
		log("db: verify writes is enabled, checking tree before "
		    "saving.");
		if ( ! checkTree( false , true ) ) {
			log("db: fixing tree and re-checking");
			fixTree ( );
			goto redo;
		}
	}


	// clear our own errno
	errno = 0;
	// . save the header
	// . force file head to the 0 byte in case offset was elsewhere
	int64_t offset = 0;
	int64_t br = 0;
	br += pwrite ( fd , &m_numNodes       , 4 , offset ); offset += 4;
	br += pwrite ( fd , &m_fixedDataSize  , 4 , offset ); offset += 4;
	br += pwrite ( fd , &m_numUsedNodes   , 4 , offset ); offset += 4;
	br += pwrite ( fd , &m_headNode       , 4 , offset ); offset += 4;
	br += pwrite ( fd , &m_nextNode       , 4 , offset ); offset += 4;
	br += pwrite ( fd , &m_minUnusedNode  , 4 , offset ); offset += 4;
	br += pwrite ( fd , &m_doBalancing   , sizeof(m_doBalancing) , offset);
	offset += sizeof(m_doBalancing);
	br += pwrite ( fd , &m_ownData       , sizeof(m_ownData)     , offset);
	offset += sizeof(m_ownData);
	// bitch on error
	if ( br != offset ) {
		m_saveErrno = errno;
		close ( fd );
		return log("db: Failed to save tree1 for %s: %s.",
			   m_dbname,mstrerror(errno));
	}
	// position to store into m_keys, ...
	int32_t start = 0;
	// save tree in block units
	while ( start < m_minUnusedNode ) {
		// . returns number of nodes, starting at node #i, saved
		// . returns -1 and sets errno on error
		int32_t bytesWritten =  fastSaveBlock_r ( fd , start , offset ) ;
		// returns -1 on error
		if ( bytesWritten < 0 ) {
			close ( fd );
			m_saveErrno = errno;
			return log("db: Failed to save tree2 for %s: %s.",
				   m_dbname,mstrerror(errno));
		}
		// point to next block to save to
		start += BLOCK_SIZE;
		// and advance the file offset
		offset += bytesWritten;
	}
	// remember total bytes written
	m_bytesWritten = offset;
	// close it up
	close ( fd );
	// now fucking rename it
	char s2[1024];
	sprintf ( s2 , "%s/%s-saved.dat", m_dir , m_dbname );
	::rename ( s , s2 ) ;
	// info
	//log(0,"RdbTree::fastSave: saved %"INT32" nodes", m_numUsedNodes );
	return true;
}

// return bytes written
int32_t RdbTree::fastSaveBlock_r ( int fd , int32_t start , int64_t offset ) {
	// save offset
	int64_t oldOffset = offset;
	// . just save each one right out, even if empty
	//   because the empty's have a linked list in m_right[]
	// . set # n
	int32_t n = BLOCK_SIZE;
	// don't over do it
	if ( start + n > m_minUnusedNode ) n = m_minUnusedNode - start;
	// debug msg
	//log("writing block at %"INT64", %"INT32" nodes",
	//     f->m_currentOffset, n);
	errno = 0;
	int64_t br = 0;
	// write the block
	br += pwrite ( fd,&m_collnums[start], n * sizeof(collnum_t) , offset );
	offset += n * sizeof(collnum_t);
	br += pwrite ( fd , &m_keys   [start*m_ks] , n * m_ks , offset );
	offset += n * m_ks;
	br += pwrite(fd, &m_left   [start] , n * 4 , offset ); offset += n * 4;
	br += pwrite(fd, &m_right  [start] , n * 4 , offset ); offset += n * 4;
	br += pwrite(fd, &m_parents[start] , n * 4 , offset ); offset += n * 4;
	if ( m_doBalancing         ) {
	  br += pwrite ( fd , &m_depth[start] , n  , offset ); offset += n  ; }
	if ( m_fixedDataSize == -1 ) {
	  br += pwrite ( fd , &m_sizes[start] , n*4, offset ); offset += n*4; }
	// if the data is actually stored in the data ptrs, just save those
	if ( m_dataInPtrs ) {
		br +=pwrite(fd,&m_data[start],n * 4 , offset ); offset +=n*4;}
	// bitch on error
	if ( br != offset - oldOffset ) 
	  return log("db: Failed to save tree3 for %s (%"INT64"!=%"INT64"): %s.",
				m_dbname,
		     br,offset,
		     mstrerror(errno)) - 1;

	// if no data to write then return bytes written this call
	if ( m_fixedDataSize == 0 || m_dataInPtrs ) return offset - oldOffset ;

	// debug count
	//int32_t count = 0;
	// define ending node for all loops
	int32_t end = start + n ;
	// now we have to dump out all the records
	for ( int32_t i = start ; i < end ; i++ ) {
		// skip if empty
		if ( m_parents[i] == -2 ) continue;
		// write variable sized nodes
		if ( m_fixedDataSize == -1 ) {
			if ( m_sizes[i] <= 0 ) continue;
			pwrite ( fd , m_data[i] , m_sizes[i] , offset );
			offset += m_sizes[i];
			continue;
		}
		// write fixed sized nodes
		pwrite (  fd , m_data[i] , m_fixedDataSize , offset );
		offset += m_fixedDataSize;
	}
	// debug
	//log("wrote %"INT32" bytes of raw rec data", count);
	// . don't close cuz needs to stay open for the rename
	//   from *-saving.dat to *-saved.dat
	// . close it
	//f->close();
	// return bytes written
	return offset - oldOffset;
}

#include "Spider.h"

// . caller should call f->set() himself
// . we'll open it here
// . returns false and sets g_errno on error (sometimes g_errno not set)
bool RdbTree::fastLoad ( BigFile *f , RdbMem *stack ) {
	// msg
	log(LOG_INIT,"db: Loading %s.",f->getFilename());
	// open it up
	if ( ! f->open ( O_RDONLY ) ) return log("db: open failed");
	int32_t fsize = f->getFileSize();
	// init offset
	int64_t offset = 0;
	// 16 byte header
	int32_t header = 4*6 + sizeof(m_doBalancing) + sizeof(m_ownData);
	// file size must be a min of "header"
	if ( fsize < header ) { f->close(); g_errno=EBADFILE; return false; }

	// note it
	m_isLoading = true;

	// get # of nodes in the tree
	int32_t n , fixedDataSize , numUsedNodes ;
	bool doBalancing , ownData ;
	int32_t headNode , nextNode , minUnusedNode;
	// force file head to the 0 byte in case offset was elsewhere
	f->read  ( &n              , 4 , offset ); offset += 4;
	f->read  ( &fixedDataSize  , 4 , offset ); offset += 4;
	f->read  ( &numUsedNodes   , 4 , offset ); offset += 4;
	f->read  ( &headNode       , 4 , offset ); offset += 4;
	f->read  ( &nextNode       , 4 , offset ); offset += 4;
	f->read  ( &minUnusedNode  , 4 , offset ); offset += 4;
	f->read  ( &doBalancing    , sizeof(m_doBalancing) , offset ) ; 
	offset += sizeof(m_doBalancing);
	f->read  ( &ownData        , sizeof(m_ownData    ) , offset ) ; 
	offset += sizeof(m_ownData);
	// return false on read error
	if ( g_errno ) { f->close(); m_isLoading = false; return false; }
	// parms check
	if ( m_fixedDataSize != fixedDataSize || 
	     m_doBalancing   != doBalancing   ||
	     m_ownData       != ownData        ) {
		f->close();
		m_isLoading = false;
		return log(LOG_LOGIC,"db: rdbtree: fastload: Bad parms. File "
			   "may be corrupt or a key attribute was changed in "
			   "the code and is not reflected in this file.");
	}
	// make sure size it right again
	int32_t nodeSize    = (sizeof(collnum_t)+m_ks+4+4+4);
	int32_t minFileSize = header + minUnusedNode * nodeSize;
	if ( doBalancing         ) minFileSize += minUnusedNode     ;
	if ( fixedDataSize == -1 ) minFileSize += minUnusedNode * 4 ;
	//if ( fixedDataSize > 0 ) minFileSize += minUnusedNode *fixedDataSize;
	// if no data, sizes much match exactly
	if ( fixedDataSize == 0 && fsize != minFileSize ) {
		g_errno = EBADFILE;
		log(
		    "db: File size of %s is %"INT32", should be %"INT32". File may be "
		    "corrupted.",
		    f->getFilename(),fsize,minFileSize);
		f->close();
		m_isLoading = false;
		return false;
	}
	// does it fit?
	if ( fsize < minFileSize ) {
		g_errno = EBADFILE;
		log(
		    "db: File size of %s is %"INT32", should >= %"INT32". File may be "
		    "corrupted.",
		    f->getFilename(),fsize,minFileSize);
		f->close();
		m_isLoading = false;
		return false;
	}
	// make room if we don't have any
	if ( m_numNodes < minUnusedNode ) {
		log(LOG_INIT,
		    "db: Growing tree to make room for %s",f->getFilename());
		if ( ! growTree ( minUnusedNode , 0 ) ) {
			f->close();
			m_isLoading = false;
			return log("db: Failed to grow tree: %s.",
				   mstrerror(g_errno));
		}
	}
	// we'll read this many
	int32_t start = 0;

	if ( m_useProtection ) unprotect();

	// reset corruption count
	m_corrupt = 0;

	// read block by block
	while ( start < minUnusedNode ) {
		// . returns next place to start scan
		// . incs m_numPositive/NegativeKeys and m_numUsedNodes 
		// . incs m_memAlloced and m_memOccupied
		int32_t bytesRead =  fastLoadBlock ( f             , 
						  start         , 
						  minUnusedNode , 
						  stack         ,
						  offset        ) ;
		if ( bytesRead < 0 ) {
			f->close();
			if ( m_useProtection ) protect();
			g_errno = errno;
			log("db: bytesRead = %"INT32"",bytesRead);
			m_isLoading = false;
			return false;
		}
		// inc the start
		start += BLOCK_SIZE;
		// and the offset
		offset += bytesRead;
	}

	m_isLoading = false;

	// print corruption
	if ( m_corrupt ) 
		log("admin: Loaded %"INT32" corrupted recs in tree for %s.",
		    m_corrupt,m_dbname);

	// re-enable protection
	if ( m_useProtection ) protect();
	// remember total bytes read
	m_bytesRead = offset;
	// set these
	m_headNode      = headNode;
	m_nextNode      = nextNode;
	m_minUnusedNode = minUnusedNode;
	// info
	//log(0,"RdbTree::fastLoad: loaded %"INT32" nodes", m_numUsedNodes );
	// close it
	//f->close();
	// check it
	if ( ! checkTree( false , true ) ) return fixTree ( );
	// a temporary hack to remove all data less tree nodes from
	// spiderdb and titledb
	/*
	if ( m_fixedDataSize == -1 ) {
		log("REMOVING 0 SIZE NODES FROM SPIDERDB/SITEDB/TITLEDB");
		int32_t count = 0;
	again:
		for ( int32_t i = 0 ; i < m_minUnusedNode ; i++ ) {
			if ( m_parents[i] == -2 ) continue;
			if ( m_sizes[i] != 0 ) continue;
			if ( (m_keys[i].n0 & 0x01) == 0x00 ) continue;
			count++;
			log("got one");
			// make it negative
			m_keys[i].n0 &= 0xfffffffffffffffeLL;
			//deleteNode3 ( i , true ); // freeData?
			//goto again;
		}
		log("REMOVED %"INT32"",count);
		if ( ! checkTree( false ) ) return fixTree ( );
	}
	*/
	// no longer needs save
	m_needsSave = false;
	//printTree();
	return true;
}

// . return bytes loaded
// . returns -1 and sets g_errno on error
int32_t RdbTree::fastLoadBlock ( BigFile   *f          , 
			      int32_t       start      , 
			      int32_t       totalNodes , 
			      RdbMem    *stack      ,
			      int64_t  offset     ) {
	// set # ndoes to read
	int32_t n = totalNodes - start;
	if ( n > BLOCK_SIZE ) n = BLOCK_SIZE;
	// debug msg
	//log("reading block at %"INT64", %"INT32" nodes",
	//     f->m_currentOffset, n );
	int64_t oldOffset = offset;
	// . copy them in
	// . start reading at beginning of file
	f->read ( &m_collnums[start], n * sizeof(collnum_t) , offset ); 
	offset += n * sizeof(collnum_t);
	f->read ( &m_keys   [start*m_ks] , n * m_ks , offset ); 
	offset += n * m_ks;
	f->read ( &m_left   [start] , n * 4 , offset ); offset += n * 4;
	f->read ( &m_right  [start] , n * 4 , offset ); offset += n * 4;
	f->read ( &m_parents[start] , n * 4 , offset ); offset += n * 4;
	if ( m_doBalancing         ) {
		f->read ( &m_depth[start] , n , offset    ); offset += n ; }
	if ( m_fixedDataSize == -1 ) {
		f->read ( &m_sizes[start] , n * 4 , offset); offset += n * 4; }
	// if the data is actually stored in the data ptrs, just save those
	if ( m_dataInPtrs ) {
		f->read ( &m_data[start] , n * 4 , offset); offset += n * 4; }
	// return false on read error
	if ( g_errno ) {
		log("db: Failed to read %s: %s.",
		    f->getFilename(),mstrerror(g_errno));
		return -1;
	}
	// get valid collnum ranges
	int32_t max  = g_collectiondb.m_numRecs;
	// sanity check
	//if ( max >= MAX_COLLS ) { char *xx = NULL; *xx = 0; }
	// define ending node for all loops
	int32_t end = start + n ;
	// int16_tcut
	CollectionRec **recs = g_collectiondb.m_recs;
	// store into tree in the appropriate nodes
	for ( int32_t i = start ; i < end ; i++ ) {
		// skip if empty
		if ( m_parents[i] == -2 ) continue;
		// watch out for bad collnums... corruption...
		collnum_t c = m_collnums[i];
		if ( c < 0 || c >= max ) {
			m_corrupt++;
			continue;
		}
		// must have rec as well. unless it its statsdb tree
		// or m_waitingTree which are collection-less and always use
		// 0 for their collnum. if collection-less m_rdbId==-1.
		if ( ! recs[c] && m_rdbId >= 0 ) {
			m_corrupt++;
			continue;
		}
		// keep a tally on all this
		m_numUsedNodes++;
		m_memOccupied += m_overhead;
		if   ( KEYNEG(m_keys,i,m_ks) ) {
			m_numNegativeKeys++;
			//m_numNegKeysPerColl[c]++;
			// this is only used for Rdb::m_trees
			//if ( m_isRealTree )
			if ( m_rdbId >= 0 )
				recs[c]->m_numNegKeysInTree[(unsigned char)m_rdbId]++;
		}
		else {
			m_numPositiveKeys++;
			//m_numPosKeysPerColl[c]++;
			// this is only used for Rdb::m_trees
			//if ( m_isRealTree )
			if ( m_rdbId >= 0 )
				recs[c]->m_numPosKeysInTree[(unsigned char)m_rdbId]++;
		}
	}
	// bail now if we can 
	if ( m_fixedDataSize == 0 || m_dataInPtrs ) return offset - oldOffset ;
	// how much should we read?
	int32_t bufSize = 0;
	if ( m_fixedDataSize == -1 ) {
		for ( int32_t i = start ; i < end ; i++ ) 
			if ( m_parents[i] != -2 ) bufSize += m_sizes[i];
	}
	else if ( m_fixedDataSize > 0 ) {
		for ( int32_t i = start ; i < end ; i++ ) 
			if ( m_parents[i] != -2 ) bufSize += m_fixedDataSize;
	}
	// get space
	//key_t dummy;
	char *dummy = NULL;
	char *buf = (char *) stack->allocData ( dummy , bufSize , 0 );
	if ( ! buf ) {
	        log("db: Failed to allocate %"INT32" bytes to read %s. "
		    "Increase tree size for it in gb.conf.",
		    bufSize,f->getFilename());
		return -1;
	}
	// debug
	//log("reading %"INT32" bytes of raw rec data", bufSize );
	// establish end point
	char *bufEnd = buf + bufSize;
	// . read all into that buf
	// . this should block since callback is NULL
	f->read ( buf , bufSize , offset ) ;
	// return false on read error
	if ( g_errno ) return -1;
	// advance file offset
	offset += bufSize;
	// part it out now
	int32_t  size = m_fixedDataSize;
	for ( int32_t i = start ; i < end ; i++ ) {
		// skip unused
		if ( m_parents[i] == -2 ) continue;
		// get size of his data if it's variable
		if ( m_fixedDataSize == -1 ) size = m_sizes[i];
		// ensure we have the room
		if ( buf + size > bufEnd ) {
			g_errno = EBADFILE;
			log("db: Encountered record with corrupted "
			    "size parameter of %"INT32" in %s.", 
			    size,f->getFilename());
			return -1;
		}
		m_data[i]  = buf;
		buf       += size;
		// update these
		m_memAlloced  += size;
		m_memOccupied += size;
	}
	return offset - oldOffset ;
}
// . caller should call f->set() himself
// . we'll open it here
// . returns false and sets g_errno on error (sometimes g_errno not set)
/*
bool RdbTree::oldLoad ( BigFile *f , RdbMem *stack ) {
	// msg
	log(LOG_INFO,"db: Loading %s.",f->getFilename());
	// open it up
	if ( ! f->open ( O_RDONLY ) ) return false;
	int32_t fsize = f->getFileSize();
	// 16 byte header
	int32_t header = 4*6 + sizeof(m_doBalancing) + sizeof(m_ownData);
	// file size must be a min of "header"
	if ( fsize < header ) { g_errno = EBADFILE; return false; }
	// get # of nodes in the tree
	int32_t n , fixedDataSize , numUsedNodes ;
	bool doBalancing , ownData ;
	int32_t headNode , nextNode , minUnusedNode;
	int64_t offset = 0;
	f->read  ( &n              , 4 , offset ); offset += 4 ;
	f->read  ( &fixedDataSize  , 4 , offset ); offset += 4 ;
	f->read  ( &numUsedNodes   , 4 , offset ); offset += 4 ;
	f->read  ( &headNode       , 4 , offset ); offset += 4 ;
	f->read  ( &nextNode       , 4 , offset ); offset += 4 ;
	f->read  ( &minUnusedNode  , 4 , offset ); offset += 4 ;
	f->read  ( &doBalancing    , sizeof(m_doBalancing) , offset ) ; 
	offset += sizeof(m_doBalancing);
	f->read  ( &ownData        , sizeof(m_ownData    ) , offset ) ; 
	offset += sizeof(m_ownData);
	// return false on read error
	if ( g_errno ) return false;
	// parms check
	if ( m_fixedDataSize != fixedDataSize || 
	     m_doBalancing   != doBalancing   ||
	     m_ownData       != ownData        ) 
		return log("RdbTree::fastLoad: bad parms");
	// make sure size it right again
	int32_t minFileSize = header + numUsedNodes * (m_ks+4+4+4+4);
	if ( doBalancing         ) minFileSize += numUsedNodes     ;
	if ( fixedDataSize == -1 ) minFileSize += numUsedNodes * 4 ;
	// does it fit?
	if ( fsize < minFileSize || (fixedDataSize==0 && fsize!=minFileSize)){
		g_errno = EBADFILE;
		return log(LOG_LOGIC,"db: rdbtree: fastload: Bad parms. File "
			   "may be corrupt or a key attribute of %s was "
			   "changed in the code and is not reflected in this "
			   "file.");
		return false;
	}
	// make room if we don't have any
	if ( m_numNodes < numUsedNodes ) {
		log(LOG_INFO,
		    "db: Growing tree to make room for %s",f->getFilename());
		if ( ! growTree ( numUsedNodes ) ) 
			return log("RdbTree::fastLoad: %s",mstrerror(g_errno));
	}
	// read block by block
	while ( numUsedNodes > 0 ) {
		// returns next place to start scan
		int32_t bytesRead = oldLoadBlock (f, numUsedNodes, stack,offset);
		if ( bytesRead < 0 ) return false;
		// advance file offset
		offset += bytesRead;
		// subtract the count
		numUsedNodes -= BLOCK_SIZE;
	}
	// set these
	//m_headNode      = headNode;
	//m_nextNode      = nextNode;
	//m_minUnusedNode = minUnusedNode;
	// info
	//log(0,"RdbTree::fastLoad: loaded %"INT32" nodes", m_numUsedNodes );
	// close it
	f->close();

	//printTree();

	return true;
}

int32_t RdbTree::oldLoadBlock ( BigFile *f, int32_t remainingNodes , RdbMem *stack,
			     int64_t offset ){
	// save offset
	int64_t oldOffset = offset;
	// array for holding shit
	int32_t  slotNums [ BLOCK_SIZE ];
	key_t keys     [ BLOCK_SIZE ];
	int32_t  left     [ BLOCK_SIZE ];
	int32_t  right    [ BLOCK_SIZE ];
	int32_t  parents  [ BLOCK_SIZE ];
	char  depth    [ BLOCK_SIZE ];
	int32_t  sizes    [ BLOCK_SIZE ];
	// set # ndoes to read
	int32_t n = remainingNodes;
	if ( n > BLOCK_SIZE ) n = BLOCK_SIZE;
	// debug msg
	//log("reading block at %"INT64", %"INT32" nodes",
	//     f->m_currentOffset, remainingNodes);
	// copy them in
	f->read (  slotNums, n * 4 , offset ); offset += n * 4;
	f->read (  keys    , n * m_ks , offset); 
	offset += n * m_ks;
	f->read (  left    , n * 4 , offset ); offset += n * 4;
	f->read (  right   , n * 4 , offset ); offset += n * 4;
	f->read (  parents , n * 4 , offset ); offset += n * 4;
	if ( m_doBalancing         ) {
		f->read (  depth , n , offset ); offset += n; }
	if ( m_fixedDataSize == -1 ) {
		f->read (  sizes , n * 4 , offset ); offset += n * 4 ; }
	// return false on read error
	if ( g_errno ) return -1;
	// store into tree in the appropriate nodes
	//int32_t j ;
	//for ( int32_t i = 0 ; i < n ; i++ ) {
		//addNode ( m_keys[i] ,
		// get the node number this belongs in
		//j = slotNums[i];
		// store it in that node number
		//m_keys    [j] = keys    [i];
		//m_left    [j] = left    [i];
		//m_right   [j] = right   [i];
		//m_parents [j] = parents [i];
		//if ( m_doBalancing         ) m_depth[j] = depth[i];
		//if ( m_fixedDataSize == -1 ) m_sizes[j] = sizes[i];
		// keep a tally on all this
		//m_numUsedNodes++;
		//if ( (keys[i] & 0x01LL) == 0x01 ) m_numPositiveKeys++;
		//else                              m_numNegativeKeys++;
		//m_memOccupied += m_overhead;
	//}
	// bail now if we can 
	if ( m_fixedDataSize == 0 ) {
		for ( int32_t i = 0 ; i < n ; i++ ) 
			addNode ( keys[i] , NULL , 0 );
		return offset - oldOffset;
	}


	// how much should we read?
	int32_t bufSize = 0;
	if ( m_fixedDataSize == -1 ) 
		for ( int32_t i = 0 ; i < n ; i++ ) bufSize += sizes[i];
	else
		bufSize = m_fixedDataSize * n;
	// get space
	key_t dummy;
	char *buf = (char *) stack->allocData ( dummy , bufSize );
	if ( ! buf ) return -1;
	// establish end point
	char *bufEnd = buf + bufSize;
	// . read all into that buf
	// . this should block since callback is NULL
	f->read ( buf , bufSize , offset ) ;
	// return false on read error
	if ( g_errno ) return -1;
	// advance file offset
	offset += bufSize;
	// part it out now
	int32_t  size = m_fixedDataSize;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get slot num
		//k = slotNums[i];
		// get size of his data if it's variable
		if ( m_fixedDataSize == -1 ) size = sizes[i];
		// ensure we have the room
		if ( buf + size > bufEnd ) {
			g_errno = EBADFILE;
			log("RdbTree::fastLoad: bad data sizes");
			return -1;
		}
		addNode ( keys[i] , buf , size );
		//m_data[k]  = buf;
		buf       += size;
		// update these
		//m_memAlloced  += size;
		//m_memOccupied += size;
	}
	return offset - oldOffset;
}
*/

void RdbTree::cleanTree ( ) { // char **bases ) {

	// some trees always use 0 for all node collnum_t's like
	// statsdb, waiting tree etc.
	if ( m_rdbId < 0 ) return;

	// the liberation count
	int32_t count = 0;
	collnum_t collnum;
	int32_t max = g_collectiondb.m_numRecs;

	for ( int32_t i = 0 ; i < m_minUnusedNode ; i++ ) {
		// skip node if parents is -2 (unoccupied)
		if ( m_parents[i] == -2 ) continue;
		// is collnum valid?
		if ( m_collnums[i] >= 0   &&
		     m_collnums[i] <  max &&
		     g_collectiondb.m_recs[m_collnums[i]] ) continue;
		// if it is negtiave, remove it, that is wierd corruption
		// if ( m_collnums[i] < 0 ) 
		// 	deleteNode3 ( i , true );
		// remove it otherwise
		// don't actually remove it!!!! in case collection gets
		// moved accidentally.
		// no... otherwise it can clog up the tree forever!!!!
		deleteNode3 ( i , true );
		count++;
		// save it
		collnum = m_collnums[i];
	}

	// print it
	if ( count == 0 ) return;
	log(LOG_LOGIC,"db: Removed %"INT32" records from %s tree for invalid "
	    "collection number %i.",count,m_dbname,collnum);
	//log(LOG_LOGIC,"db: Records not actually removed for safety. Except "
	//    "for those with negative colnums.");
	static bool s_print = true;
	if ( ! s_print ) return;
	s_print = false;
	log (LOG_LOGIC,"db: This is bad. Did you remove a collection "
	     "subdirectory? Don't do that, you should use the \"delete "
	     "collections\" interface because it also removes records from "
	     "memory, too.");
}

int32_t  RdbTree::getNumNegativeKeys ( collnum_t collnum ) { 
	// fix for statsdb or other collectionless rdbs
	if ( m_rdbId < 0 ) return m_numNegativeKeys;
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	if ( ! cr ) return 0;
	//if ( ! m_countsInitialized ) { char *xx=NULL;*xx=0; }
	return cr->m_numNegKeysInTree[(unsigned char)m_rdbId]; 
}

int32_t  RdbTree::getNumPositiveKeys ( collnum_t collnum ) { 
	// fix for statsdb or other collectionless rdbs
	if ( m_rdbId < 0 ) return m_numPositiveKeys;
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	if ( ! cr ) return 0;
	//if ( ! m_countsInitialized ) { char *xx=NULL;*xx=0; }
	return cr->m_numPosKeysInTree[(unsigned char)m_rdbId]; 
}

void RdbTree::setNumKeys ( CollectionRec *cr ) {

	if ( ! cr ) return;

	//m_countsInitialized = true;

	return;

	if ( ((unsigned char)m_rdbId) >= RDB_END ) { char *xx=NULL;*xx=0; }

	collnum_t collnum = cr->m_collnum;
	cr->m_numNegKeysInTree[(unsigned char)m_rdbId] = 0;
	cr->m_numPosKeysInTree[(unsigned char)m_rdbId] = 0;


	for ( int32_t i = 0 ; i < m_numNodes ; i++ ) {
		//QUICKPOLL(niceness);
		// skip if empty
		if ( m_parents[i] == -2 ) continue;
		// or if we hit a different collection number
		if ( m_collnums [ i ] != collnum ) continue;
		if   ( KEYNEG(m_keys,i,m_ks) ) 
			cr->m_numNegKeysInTree[(unsigned char)m_rdbId]++;
		else
			cr->m_numPosKeysInTree[(unsigned char)m_rdbId]++;
	}
}	
