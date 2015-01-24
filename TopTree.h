// Matt Wells, copyright Jul 2004

// . class used to hold the top scoring search results
// . filled by IndexTable.cpp
// . used by Msg38 to get cluster info for each TopNode
// . used by Msg39 to serialize into a reply

#ifndef _TOPTREE_H_
#define _TOPTREE_H_

#include "Clusterdb.h"   // SAMPLE_VECTOR_SIZE, 48 bytes for now
//#include "IndexTable2.h" // score_t definition
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
	int64_t      m_docId;

	// option for using int scores
	int32_t m_intScore;
	
	// clustering info
	//int32_t           m_kid      ; // result from our same site below us
	//uint32_t  m_siteHash ;
	//uint32_t  m_contentHash ;
	//int32_t           m_rank        ;

	// the lower 64 bits of the cluster rec, used by Msg51, the new
	// class for doing site clustering
	//uint64_t       m_clusterRec;

	// . for getting similarity between titleRecs
	// . this is so big only include if we need it
	//int32_t           m_vector [ VECTOR_SIZE ];

	// tree info, indexes into m_nodes array
	int32_t m_parent;
	int32_t m_left;   // kid
	int32_t m_right;  // kid

	// so we can quickly remove its scoring info from the scoreinfo
	// buf and replace with new docid's scoring info
	//int64_t m_scoreInfoBufOffset;

	//int64_t getDocId ( );

	//int64_t getDocIdForMsg3a ( );
};

class TopTree {
 public:
	TopTree();
	~TopTree();
	// free mem
	void reset();
	// pre-allocate memory
	bool setNumNodes ( int32_t docsWanted , bool doSiteClustering );
	// . add a node
	// . get an empty first, fill it in and call addNode(t)
	int32_t getEmptyNode ( ) { return m_emptyNode; };
	// . you can add a new node
	// . it will NOT overwrite a node with same bscore/score/docid
	// . it will NOT add if bscore/score/docid < m_tail node
	//   otherwise it will remove m_tail node if 
	//   m_numNodes == m_numUsedNodes
	bool addNode ( TopNode *t , int32_t tnn );

	int32_t getLowNode  ( ) { return m_lowNode ; };
	// . this is computed and stored on demand
	// . WARNING: only call after all nodes have been added!
	int32_t getHighNode ( ) ;

	float getMinScore ( ) {
		if ( m_lowNode < 0 ) return -1.0;
		return m_nodes[m_lowNode].m_score;
	}

	int32_t getPrev ( int32_t i );
	int32_t getNext ( int32_t i );

	bool checkTree ( bool printMsgs ) ;
	int32_t computeDepth ( int32_t i ) ;

	void deleteNodes ( );

	bool hasDocId ( int64_t d );

	TopNode *getNode ( int32_t i ) { return &m_nodes[i]; }

	// ptr to the mem block
	TopNode *m_nodes;
	int32_t     m_allocSize;
	// optional dedup vectors... very big, VECTOR_REC_SIZE-12 bytes each 
	// (512) so we make this an option
	//int32_t    *m_sampleVectors; 
	//bool     m_useSampleVectors;
	// which is next to be used, after m_nextPtr
	int32_t m_numUsedNodes;
	// total count
	int32_t m_numNodes;
	// the top of the tree
	int32_t m_headNode;

	// . always keep track of the high and low nodes
	// . IndexTable.cpp likes to replace the low-scoring tail often 
	// . Msg39.cpp likes to print out starting at the high-scorer
	// . these are indices into m_nodes[] array
	int32_t m_lowNode;
	int32_t m_highNode;

	// use this to set "t" in call to addNode(t)
	int32_t m_emptyNode;

	bool m_pickRight;

	float m_vcount  ;
	int32_t  m_cap     ;
	float m_partial ;
	bool  m_doSiteClustering;
	bool  m_useIntScores;
	int32_t  m_docsWanted;
	int64_t  m_ridiculousMax;
	char  m_kickedOutDocIds;
	//int64_t m_lastKickedOutDocId;
	int32_t  m_domCount[256];
	// the node with the minimum "score" for that domHash
	int32_t  m_domMinNode[256];

	// an embedded RdbTree for limiting the storing of keys to X
	// keys per domHash, where X is usually "m_ridiculousMax"
	RdbTree m_t2;

 private:

	void deleteNode  ( int32_t i , uint8_t domHash ) ;
	void setDepths   ( int32_t i ) ;
	int32_t rotateLeft  ( int32_t i ) ;
	int32_t rotateRight ( int32_t i ) ;
};

#endif
