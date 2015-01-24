// Matt Wells, Copyright May 2005

// . format of a 16-byte datedb key
// . tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
// . tttttttt tttttttt DDDDDDDD DDDDDDDD  D = ~date
//   DDDDDDDD DDDDDDDD ssssssss dddddddd  s = ~score
// . dddddddd dddddddd dddddddd dddddd0Z  d = docId (38 bits)

// . format of a 10-byte indexdb key
// . DDDDDDDD DDDDDDDD DDDDDDDD DDDDDDDD  D = ~date
// . ssssssss dddddddd dddddddd dddddddd 
// . dddddddd dddddd0Z                    s = ~score d = docId (38 bits)

//
// SPECIAL EVENTDB KEYS. for indexing events.
//

// . format of a 16-byte "eventdb" key with termId of 0
// . for sorting/constraining events with multiple start dates
// . each start date has a "termId 0" key. "D" date is when
//   the event starts. score is the eventId. this key is
//   added by the Events::hashIntervals(eventId) function.
//
// . 00000000 00000000 00000000 00000000  t = termId (48bits)
// . 00000000 00000000 DDDDDDDD DDDDDDDD  D = ~date (in secs after epoch)
//   DDDDDDDD DDDDDDDD IIIIIIII dddddddd  I = eventId
// . dddddddd dddddddd dddddddd dddddd0Z  d = docId (38 bits)


// . format of a 16-byte "eventdb" key from words/phrases
// . each word/phrase of each event has one and only one key of this format.
// . this key is added by the Events::hash() function.
//
// . tttttttt tttttttt tttttttt tttttttt  t = termId (48bits)
// . tttttttt tttttttt 00000000 00000000  
//   iiiiiiii IIIIIIII ssssssss dddddddd  s = ~score, [I-i] = eventId RANGE
// . dddddddd dddddddd dddddddd dddddd0Z  d = docId (38 bits)



#ifndef _DATEDB_H_
#define _DATEDB_H_

#include "Rdb.h"
#include "Conf.h"
#include "Indexdb.h"

// we define these here, NUMDOCIDBITS is in ../titledb/Titledb.h
#define NUMTERMIDBITS 48
// mask the lower 48 bits
#define TERMID_MASK (0x0000ffffffffffffLL)

#include "Titledb.h" // DOCID_MASK

// Msg5.cpp and Datedb.cpp use this
//#define MIN_TRUNC (PAGE_SIZE/6 * 4 + 6)
// keep it at LEAST 12 million to avoid disasters
#define MIN_TRUNC 12000000

class Datedb {

 public:

	// resets rdb
	void reset();

	// sets up our m_rdb from g_conf (global conf class)
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( int32_t treeMem );

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	bool addIndexList ( class IndexList *list ) ;
	// . make a 16-byte key from all these components
	// . since it is 16 bytes, the big bit will be set
	key128_t makeKey ( int64_t          termId   , 
			   uint32_t      date     ,
			   unsigned char      score    , 
			   uint64_t docId    , 
			   bool               isDelKey );

	key128_t makeStartKey ( int64_t termId , uint32_t date1 ) {
		return makeKey ( termId , date1, 255 , 0LL , true ); };
	key128_t makeEndKey  ( int64_t termId , uint32_t date2 ) {
		return makeKey ( termId , date2, 0   , DOCID_MASK , false ); };

	// works on 16 byte full key or 10 byte half key
	int64_t getDocId ( void *key ) {
		return ((*(uint64_t *)(key)) >> 2) & DOCID_MASK; };

	unsigned char getScore ( void *key ) {
		return ~(((unsigned char *)key)[5]); };

	// use the very top int32_t only
	/*
	uint32_t getGroupIdFromKey ( key128_t *key ) {
		if ( g_conf.m_fullSplit )
			return g_titledb.getGroupId ( getDocId((char *)key) );
//#ifdef SPLIT_INDEXDB
		if ( g_conf.m_indexdbSplit > 1 ) {
			uint32_t groupId =
				(((uint32_t*)key)[3]) &
				g_hostdb.m_groupMask;
			groupId >>= g_indexdb.m_groupIdShift;
			uint32_t offset = (key->n0 >> 2) &
				DOCID_OFFSET_MASK;
			return g_indexdb.m_groupIdTable [ groupId+
				(offset*g_indexdb.m_numGroups) ];
		}
//#else
		else
			return (((uint32_t *)key)[3]) &
				g_hostdb.m_groupMask;
//#endif
	};
	*/

//#ifdef SPLIT_INDEXDB

	// for terms like gbdom:xyz.com that only reside in one group and
	// are not split by docid into multiple groups. reduces disk seeks
	// while spidering, cuz we use such terms for deduping and for
	// doing quotas.
	// ---> IS THIS RIGHT???? MDW
	uint32_t getNoSplitGroupId ( key128_t *k ) {
		char *xx=NULL;*xx=0; 
		return 0;
		// wtf is this? still being used?
		//return (((uint32_t *)k)[3]) & g_hostdb.m_groupMask;
		//uint32_t bgid = getBaseGroupId(k);
		//return g_indexdb.getSplitGroupId(bgid,0);
		//return bgid;
	}

	//uint32_t getBaseGroupId ( key128_t *k ) {
	//	return (((uint32_t *)k)[3]) & g_hostdb.m_groupMask;
	//}
//#endif

	// extract the termId from a key
	int64_t getTermId ( key128_t *k ) {
		int64_t termId = 0LL;
		gbmemcpy ( &termId , ((char *)k) + 10 , 6 );
		return termId ;
	};

	int32_t getDate ( key128_t *k ) {
                uint32_t date = 0;
                date  = (uint32_t)(k->n1 & 0x000000000000ffffULL);
                date <<= 16;
                date |= (uint32_t)((k->n0 & 0xffff000000000000ULL) >> 48);
                return ~date;
        }

	int32_t getEventIdStart ( void *k ) {
		uint32_t d = getDate ( (key128_t *)k );
		return ((uint8_t *)(&d))[1];
	};

	int32_t getEventIdEnd ( void *k ) {
		uint32_t d = getDate ( (key128_t *)k );
		return ((uint8_t *)(&d))[0];
	};
		

	//RdbCache *getCache ( ) { return &m_rdb.m_cache; };
	Rdb      *getRdb   ( ) { return &m_rdb; };

	Rdb m_rdb;

	//DiskPageCache *getDiskPageCache ( ) { return &m_pc; };

	//DiskPageCache m_pc;
};

extern class Datedb g_datedb;
//extern class Datedb g_datedb2;

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
