// Matt Wells, copyright Jul 2004

// . class used to hold the top scoring search results
// . filled by IndexTable.cpp
// . used by Msg38 to get cluster info for each TopNode
// . used by Msg39 to serialize into a reply

#ifndef _TOPTREE_H_
#define _TOPTREE_H_

#include "Clusterdb.h"   // SAMPLE_VECTOR_SIZE, 48 bytes for now
#include "IndexTable2.h" // score_t definition
#include "RdbTree.h"

class TopNode {
 public:
	//unsigned char  m_bscore ; // bit #6(0x40) is on if has all explicitly
	// do not allow a higher-tiered node to outrank a lower that has
	// bit #6 set, under any circumstance

	char           m_depth    ;

	// Msg39 now looks up the cluster recs so we can do clustering
	// really quick on each machine, assuming we have a full split and the
	// entire clusterdb is in our local disk page cache.
	char     m_clusterLevel;
	key_t    m_clusterRec;

	// no longer needed, Msg3a does not need, it has already
	//unsigned char  m_tier     ;
	float          m_score    ;
	long long      m_docId;
	// clustering info
	//long           m_kid      ; // result from our same site below us
	//unsigned long  m_siteHash ;
	//unsigned long  m_contentHash ;
	//long           m_rank        ;

	// the lower 64 bits of the cluster rec, used by Msg51, the new
	// class for doing site clustering
	//uint64_t       m_clusterRec;

	// . for getting similarity between titleRecs
	// . this is so big only include if we need it
	//long           m_vector [ VECTOR_SIZE ];

	// tree info, indexes into m_nodes array
	long m_parent;
	long m_left;   // kid
	long m_right;  // kid

	//long long getDocId ( );

	//long long getDocIdForMsg3a ( );
};

class TopTree {
 public:
	TopTree();
	~TopTree();
	// free mem
	void reset();
	// pre-allocate memory
	bool setNumNodes ( long docsWanted , bool doSiteClustering );
	// . add a node
	// . get an empty first, fill it in and call addNode(t)
	long getEmptyNode ( ) { return m_emptyNode; };
	// . you can add a new node
	// . it will NOT overwrite a node with same bscore/score/docid
	// . it will NOT add if bscore/score/docid < m_tail node
	//   otherwise it will remove m_tail node if 
	//   m_numNodes == m_numUsedNodes
	bool addNode ( TopNode *t , long tnn );

	long getLowNode  ( ) { return m_lowNode ; };
	// . this is computed and stored on demand
	// . WARNING: only call after all nodes have been added!
	long getHighNode ( ) ;

	float getMinScore ( ) {
		if ( m_lowNode < 0 ) return -1.0;
		return m_nodes[m_lowNode].m_score;
	}

	long getPrev ( long i );
	long getNext ( long i );

	bool checkTree ( bool printMsgs ) ;
	long computeDepth ( long i ) ;

	void deleteNodes ( );

	bool hasDocId ( long long d );

	TopNode *getNode ( long i ) { return &m_nodes[i]; }

	// ptr to the mem block
	TopNode *m_nodes;
	long     m_allocSize;
	// optional dedup vectors... very big, VECTOR_REC_SIZE-12 bytes each 
	// (512) so we make this an option
	//long    *m_sampleVectors; 
	//bool     m_useSampleVectors;
	// which is next to be used, after m_nextPtr
	long m_numUsedNodes;
	// total count
	long m_numNodes;
	// the top of the tree
	long m_headNode;

	// . always keep track of the high and low nodes
	// . IndexTable.cpp likes to replace the low-scoring tail often 
	// . Msg39.cpp likes to print out starting at the high-scorer
	// . these are indices into m_nodes[] array
	long m_lowNode;
	long m_highNode;

	// use this to set "t" in call to addNode(t)
	long m_emptyNode;

	bool m_pickRight;

	float m_vcount  ;
	long  m_cap     ;
	float m_partial ;
	bool  m_doSiteClustering;
	long  m_docsWanted;
	long  m_ridiculousMax;
	char  m_kickedOutDocIds;
	//long long m_lastKickedOutDocId;
	long  m_domCount[256];
	// the node with the minimum "score" for that domHash
	long  m_domMinNode[256];

	// an embedded RdbTree for limiting the storing of keys to X
	// keys per domHash, where X is usually "m_ridiculousMax"
	RdbTree m_t2;

 private:

	void deleteNode  ( long i , uint8_t domHash ) ;
	void setDepths   ( long i ) ;
	long rotateLeft  ( long i ) ;
	long rotateRight ( long i ) ;
};

#endif
