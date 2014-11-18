// Matt Wells, copyright Jun 2001

// . time intervals of events are now stored in timedb
// . basically a linked list of the event time intervals
// . allows us to cache the next start time for a docid/eventid
//   and do very efficient updating by just sampling the current time interval
//   of timedb to update next start times for just those docid/eventids that
//   need it
// . we use the special cache g_sortByDateTable to cache one record for
//   each unique docid/eventid we have in timedb.
// . if user sets their own "clockset" though this cache will not help so
//   we have to read the entire timedb for addLists_r() in that case

#ifndef _TIMEDB_H_
#define _TIMEDB_H_

#include "Rdb.h"
#include "Url.h"
#include "Conf.h"
#include "Xml.h"
#include "Titledb.h"
#include "Collectiondb.h"
//#include "CollectionRec.h"

bool initAllSortByDateTables ( ) ;
bool initSortByDateTable ( char *coll ) ;
bool addTimedbKey ( key128_t *kp , uint32_t nowGlobal , 
		    class HashTableX *ht ) ;
bool addTmpTimeList(RdbList *list,HashTableX *ht,time_t nowGlobal,
		    int32_t niceness ) ;
bool compareTimeTables ( HashTableX *ht1 , HashTableX *ht2 , 
			 uint32_t now );

// mdw subtract 7hrs to get into utc, since all our date intervals are in utc
#define START2009 (1230793200-7*3600)
#define START2030 (1893481200-7*3600)

class Timedb {

 public:

	// reset rdb
	void reset();

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	// init m_rdb
	bool init ();

	// init secondary/rebuild timedb
	bool init2 ( int32_t treeMem ) ;

	key128_t makeKey ( time_t    startTime ,
			   int64_t docId , 
			   uint16_t  eventId ,
			   time_t    endTime ,
			   time_t    nextStartTime ,
			   bool      isDelete ) ;

	key128_t makeStartKey ( time_t startTime ) {
		return makeKey ( startTime , 0LL, 0, 0, 0, true); };

	key128_t makeEndKey ( time_t startTime ) {
		return makeKey ( startTime , MAX_DOCID,255,0x7fffffff,
				 0x7fffffff, false); };


	// the time in the key is in minutes since jan 1, 2010
	time_t   getStartTime32     ( key128_t *k ) {
		return ((k->n1 >> 38) * 60 + START2009); }
	// need to mask out 26 bits
	time_t   getEndTime32       ( key128_t *k ) {
		return (((k->n0 >> 27)&0x03ffffff) * 60 + START2009); }
	// need to mask out 26 bits
	time_t   getNextStartTime32 ( key128_t *k ) {
		return (((k->n0 >>  1)&0x03ffffff) * 60 + START2009); }
	// a simple mask on this one
	int64_t getDocId   ( key128_t *k ) {
		return (k->n1 & DOCID_MASK); };
	// need to mask out 11 bits
	uint16_t  getEventId ( key128_t *k ) {
		return ((k->n0 >> 53)&0x000007ff); }


	Rdb *getRdb() { return &m_rdb; };

	int32_t getNumTotalEvents() {
		int32_t total = 0;
		// loop over all coll's sortbydate tables
		for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
			CollectionRec *cr = g_collectiondb.m_recs[i];
			if ( ! cr ) continue;
			total += cr->m_sortByDateTable.getNumSlotsUsed();
		}
		return total;
	}

	// holds binary format time entries
	Rdb m_rdb;
};

extern class Timedb g_timedb;
extern class Timedb g_timedb2;

class TimeSlot {
 public:
	uint32_t m_startTime;
	uint32_t m_endTime;
	uint32_t m_nextStartTime;
};

// . if event already in progress, list those that close first first
// . used by IndexTable2.cpp to get final scores of search results
inline uint32_t getTimeScore ( //collnum_t collnum   ,
			       uint64_t  docId     ,
			       uint16_t  eventId   ,
			       uint32_t nowGlobal ,
			       HashTableX *ht ,
			       bool showInProgress ) {
	// select the right table. each collection has its own...
	//HashTableX *ht = &g_collectiondb.m_recs[collnum]->m_sortByDateTable;
	// 32 bit keys had too many collisions, now use 64bit
	uint64_t key64 = eventId;
	key64 <<= NUMDOCIDBITS; // 38
	key64 |= docId;
	// make a 32 bit hash of docid and eventid
	//uint32_t key32 = (uint32_t)(docId&0xffffffff) ;
	// just in case eventid was > 255
	//if ( eventId > 255 ) {
	//	key32 ^= (uint32_t)g_hashtab[eventId&0xff][0];
	//	key32 ^= (uint32_t)g_hashtab[eventId>>8  ][1];
	//}
	//else {
	//	key32 ^= (uint32_t)g_hashtab[eventId][0];
	//}
	// lookup in hashtable to see if we got one already
	TimeSlot *old = (TimeSlot *)ht->getValue(&key64);
	// debug test
	// i think collisions here might be causing us to lose search
	// results because we get the wrong time for them! and their
	// intervals ptr ends up being empty. and we print out a debug msg
	// for that when computing ExpandedResults in Msg40.cpp!
	/*
	int32_t slot = ht->getSlot(&key32);
	if ( slot >= 0 && 
	     slot+1 < ht->m_numSlots && 
	     ht->m_flags[slot+1] &&
	     *(uint32_t *)ht->getKeyFromSlot(slot+1) == key32 ) {
		log("time collision!!!!!");
		TimeSlot *old2 = (TimeSlot *)ht->getValueFromSlot(slot+1);
		if ( old2->m_startTime < old->m_startTime )
			old = old2;
	}
	*/

	// bail if not there
	if ( ! old ) {
		// this happens when doing a clockset and we match the
		// event's query terms but its been too long and timedb
		// does not have it anymore since we only like store
		// a year out or so since spider time
		//log("timedb: docid/eid not found d=%"UINT64" eid=%"INT32".",
		//    docId,(int32_t)eventId);
		return 0;
	}
	// over? return 1 then, next smallest score
	if ( old->m_endTime < nowGlobal ) return 1;
	// in progress? then base on when closed
	if ( old->m_startTime < nowGlobal ) {
		// bad?
		if ( ! showInProgress ) return 0;
		// TODO: make sure these are always on top of the
		//       guys that haven't started yet
		// . well since most times are like
		//   (gdb) p ~(uint32_t)1319584941
		//   $2 = 2975382354
		//   this should work. but once we cross the 2B midpoint
		//   it will cause these scores to be below those event's 
		//   scores that are starting in the future.
		return ~old->m_endTime;
	}
	// . if not yet started, complement score
	// . divide by 60 to make smaller than score above, but yet
	//   maintain to the minute accuracy
	// . divide by 32 for speed!
	return ~(old->m_startTime) / 32;
}

#endif
