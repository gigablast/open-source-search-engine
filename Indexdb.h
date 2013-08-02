// Matt Wells, Copyright Apr 2001

// . format of a 12-byte indexdb key
// . tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
// . tttttttt tttttttt ssssssss dddddddd  s = ~score
// . dddddddd dddddddd dddddddd dddddd0Z  d = docId (38 bits)

// . format of a 6-byte indexdb key
// . ssssssss dddddddd dddddddd dddddddd  d = docId, s = ~score
// . dddddddd dddddd1Z 

#ifndef _INDEXDB_H_
#define _INDEXDB_H_

#include "Rdb.h"
#include "Conf.h"
#include "DiskPageCache.h"

// we define these here, NUMDOCIDBITS is in ../titledb/Titledb.h
#define NUMTERMIDBITS 48
// mask the lower 48 bits
#define TERMID_MASK (0x0000ffffffffffffLL)

#include "Titledb.h" // DOCID_MASK

// Msg5.cpp and Indexdb.cpp use this
//#define MIN_TRUNC (GB_PAGE_SIZE/6 * 4 + 6)
// keep it at LEAST 12 million to avoid disasters
#define MIN_TRUNC 12000000

//#define SPLIT_INDEXDB
//#define INDEXDB_SPLIT 2
//#define INDEXDB_SPLIT 8
//#define DOCID_OFFSET_MASK (INDEXDB_SPLIT-1)
#define DOCID_OFFSET_MASK (g_conf.m_indexdbSplit-1)
#define MAX_INDEXDB_SPLIT 128

class Indexdb {

 public:

	// resets rdb
	void reset();

	// sets up our m_rdb from g_conf (global conf class)
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( long treeMem );

	bool setGroupIdTable();

	bool verify ( char *coll );
	void deepVerify ( char *coll );

	bool addIndexList ( class IndexList *list ) ;

	bool addColl ( char *coll, bool doVerify = true );

	// . get start/end keys for IndexList from a termId
	// . keys correspond to the start/end of the IndexList for this termId
	// . NOTE: score is complemented when stored in the key
	key_t makeStartKey ( long long termId );
	key_t makeEndKey   ( long long termId );

	// . make a 12-byte key from all these components
	// . since it is 12 bytes, the big bit will be set
	key_t makeKey ( long long          termId   , 
			unsigned char      score    , 
			unsigned long long docId    , 
			bool               isDelKey );

	key_t makeFirstKey ( long long termId ) {
		return makeKey ( termId , 255 , 0LL , true ); };
	key_t makeLastKey  ( long long termId ) {
		return makeKey ( termId , 0   , DOCID_MASK , false ); };

	// get a termId from a prefixHash and termHash
	long long getTermId ( long long prefixHash , long long termHash ) {
		return hash64 ( prefixHash , termHash ) & TERMID_MASK;};

	// extract the termId from a key
	long long getTermId ( key_t *k ) {
		long long termId = 0LL;
		memcpy ( &termId , ((char *)k) + 6 , 6 );
		return termId ;
	};
	long long getTermId ( key_t k ) { return getTermId ( &k ); };

	long long getDocId ( key_t k ) {
		char *rec = (char *)&k;
		return ((*(unsigned long long *)(rec)) >> 2) & DOCID_MASK; };

	long long getDocId ( key_t *k ) {
		return ((*(unsigned long long *)(k)) >> 2) & DOCID_MASK; };

	unsigned char getScore ( key_t k ) {
		char *rec = (char *)&k;
		return ~rec[5]; };

	unsigned char getScore ( char *k ) {return ~k[5]; };

	/*
	unsigned long getGroupId ( long long termId, long long docId ) {
		if ( g_conf.m_fullSplit )
			return g_titledb.getGroupId ( docId );
//#ifdef SPLIT_INDEXDB
		if ( g_conf.m_indexdbSplit > 1 ) {
			unsigned long groupId = (unsigned long)(termId >> 16);
			groupId >>= m_groupIdShift;
			unsigned long offset = docId & DOCID_OFFSET_MASK;
			return m_groupIdTable[groupId+(offset*m_numGroups)];
		}
//#else
		else
			return (unsigned long)(termId >> 16) &
				g_hostdb.m_groupMask;
//#endif
	} 

	unsigned long getGroupIdFromKey ( key_t *k ) {
		if ( g_conf.m_fullSplit )
			return g_titledb.getGroupId ( getDocId( k) );
//#ifdef SPLIT_INDEXDB
		if ( g_conf.m_indexdbSplit > 1 ) {
			unsigned long groupId = k->n1 & g_hostdb.m_groupMask;
			groupId >>= m_groupIdShift;
			unsigned long offset = (k->n0 >> 2) & DOCID_OFFSET_MASK;
			return m_groupIdTable[groupId+(offset*m_numGroups)];
		}
//#else
		else
			return k->n1 & g_hostdb.m_groupMask;
//#endif
	}
//#ifdef SPLIT_INDEXDB

	// for terms like gbdom:xyz.com that only reside in one group and
	// are not split by docid into multiple groups. reduces disk seeks
	// while spidering, cuz we use such terms for deduping and for
	// doing quotas.
	unsigned long getNoSplitGroupId ( key_t *k ) {
		// keep it simple now
		return k->n1 & g_hostdb.m_groupMask;
		//unsigned long bgid = getBaseGroupId(k);
		//return getSplitGroupId(bgid,0);
	}
	*/

	/*
	unsigned long getBaseGroupId ( key_t *k ) {
		return k->n1 & g_hostdb.m_groupMask;
	}

	unsigned long getSplitGroupId ( unsigned long baseGroupId,
					unsigned long offset ) {
		if ( g_hostdb.m_numGroups <= 1 ) return 0;
		baseGroupId >>= m_groupIdShift;
		return m_groupIdTable[baseGroupId+(offset*m_numGroups)];
	}
	*/
//#endif

	// . accesses RdbMap to estimate size of the indexList for this termId
	// . returns a pretty tight upper bound if indexList not truncated
	// . if truncated, it's does linear interpolation (use exponential!)
	long long getTermFreq ( char *coll , long long termId ) ;

	//long getTruncationLimit ( ){return g_conf.m_indexdbTruncationLimit;};

	//RdbCache *getCache ( ) { return &m_rdb.m_cache; };
	Rdb      *getRdb   ( ) { return &m_rdb; };

	Rdb m_rdb;

	DiskPageCache *getDiskPageCache ( ) { return &m_pc; };

	DiskPageCache m_pc;

//#ifdef SPLIT_INDEXDB
	// . groupId Table, for getting the correct group id based
	//   on type bits of termId and lower bits of docId
	unsigned long *m_groupIdTable;
	long m_groupIdTableSize;
	long m_groupIdShift;
	long m_numGroups;
//#endif
};

extern class Indexdb g_indexdb;
extern class Indexdb g_indexdb2;

#endif

// . the search-within operator "|"
//   - termlists are sorted by score so that when merging 2 termlists
//     we can stop when we get the first 10 docIds that have both terms and
//     we are certain that they are the top 10 highest scoring
//   - but search within says to disregard the scores of the first list,
//     so we can still be sure we got the top 10, i guess
//   - sort by date: like search-within but everybody has a date so the
//     termlist is huge!!! we can pass a sub-date termlist, say today's
//     date and merge that one. if we get no hits then try the last 3 days
//     date termlist. Shit, can't have one huge date termlist anyway cuz we 
//     need truncation to make the network thang work.
