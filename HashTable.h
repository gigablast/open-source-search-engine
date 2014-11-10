// Matt Wells,  Copyright, Dec. 2002

// . generic hash table class

#ifndef _HASHTABLE_H_
#define _HASHTABLE_H_

#include "Mem.h"     // for mcalloc and mmalloc

class HashTable {

 public:

	bool set ( int32_t  initialNumSlots = 0    ,
		   char *buf             = NULL ,
		   int32_t  bufSize         = 0    ,
		   char *label           = NULL );

	 HashTable       ( );
	~HashTable       ( );

	void setLabel ( char *label ) { m_label = label; };

	// . add key/value entry to hash table
	// . will grow hash table if it needs to
	bool addKey ( int32_t key , int32_t value , int32_t *slot = NULL );

	// remove key/value entry to hash table
	bool removeKey  ( int32_t key );

	// like removeKey
	void removeSlot ( int32_t n );

	// . used by ../english/Bits.h to store stop words, abbr's, ...
	// . returns the score for this termId (0 means empty usually)
	int32_t getValue ( int32_t key );

	// value of 0 means empty
	bool isEmpty ( int32_t key ) { return (getValue(key) == 0); };

	int32_t getKey ( int32_t n ) { return m_keys[n]; };

	int32_t getSlot ( int32_t key ) { return getOccupiedSlotNum ( key ); };

	void setValue ( int32_t n , int32_t val ) { m_vals[n] = val; };

	int32_t getValueFromSlot ( int32_t n ) { return m_vals[n]; };

	// frees the used memory, etc.
	void  reset  ( );

	// removes all key/value pairs from hash table, vacates all slots
	void  clear  ( );

	// how many are occupied?
	int32_t getNumSlotsUsed ( ) { return m_numSlotsUsed; };

	// how many are there total? used and unused.
	int32_t getNumSlots ( ) { return m_numSlots; };

	// both return false and set g_errno on error, true otherwise
	bool load ( char *dir , char *filename );
	bool save ( char *dir , char *filename );

 private:

	bool setTableSize ( int32_t numSlots , char *buf , int32_t bufSize );

	int32_t getOccupiedSlotNum ( int32_t key ) ;

	// . the array of buckets in which we store the terms
	// . scores are allowed to exceed 8 bits for weighting purposes
	int32_t  *m_keys;
	int32_t  *m_vals;

	int32_t m_numSlots;
	int32_t m_numSlotsUsed;
	uint32_t m_mask;

	//char m_needsSave;
	char m_doFree;

	char *m_label;
};

#endif





