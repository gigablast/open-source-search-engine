// . generic hash table class

#ifndef _HASHTABLET_H_
#define _HASHTABLET_H_

#include "Mem.h"     // for mcalloc and mmalloc
#include "SafeBuf.h"
#include "types.h"
using namespace std;

#define HT_BUF_SIZE (4*1024)

template<class Key_t, class Val_t>
class HashTableT {

 public:

	bool set ( long initialNumSlots = 0     , 
		   char *buf		= NULL  ,
		   long bufSize		= 0     ,
		   bool allowDupKeys    = false );

	 HashTableT( ) ;
	~HashTableT( );

	void constructor();

	// . add key/value entry to hash table
	// . will grow hash table if it needs to
	bool addKey ( Key_t key , Val_t value , long *slot = NULL );

	// remove key/value entry to hash table
	bool removeKey ( Key_t key );

	void removeSlot ( long n );

	bool load ( char *filename , char **textBuf = NULL , long *textBufSize = NULL );
	bool save ( char *filename , char  *textBuf = NULL , long  textBufSize = 0    );

	// . used by ../english/Bits.h to store stop words, abbr's, ...
	// . returns the score for this termId (0 means empty usually)
	Val_t* getValuePointer ( Key_t key ) const;

	Val_t* getValuePtr ( Key_t key ) { return getValuePointer ( key ); };

	// value of 0 means empty
	//bool isEmpty ( Key_t key ) { return (getValue(key) == 0); };

	Key_t getKey ( long n ) const { return m_keys[n]; };

	long getSlot ( Key_t key ) const { return getOccupiedSlotNum ( key ); };
	// for hash tables that m_allowDupKeys
	long getNextSlot ( Key_t& key , long n ) const ;

	void setValue ( long n , Val_t val ) { m_vals[n] = val; };

	bool isEmpty ( long n ) { return ( m_keys[n] == 0 ); };

	Val_t getValueFromSlot ( long n ) const { return m_vals[n]; };

	Val_t* getValuePointerFromSlot ( long n ) { return &m_vals[n]; };

	// frees the used memory, etc.
	void  reset  ( );

	bool copy(HashTableT<Key_t, Val_t>* retval);

	// removes all key/value pairs from hash table, vacates all slots
	void  clear  ( );

	// how many are occupied?
	long getNumSlotsUsed ( ) const { return m_numSlotsUsed; };

	// how many are there total? used and unused.
	long getNumSlots ( ) const { return m_numSlots; };

	void setAllowDupKeys(char allow) { m_allowDupKeys = allow; };
	char getAllowDupKeys( ) const { return m_allowDupKeys; };

	bool serialize(SafeBuf& sb);
	long deserialize(char* s);

	bool setTableSize ( long numSlots, char *buf, long bufSize );

	long getOccupiedSlotNum ( Key_t& key ) const;

	//private:
	//friend class RequestTable;
	// . the array of buckets in which we store the terms
	// . scores are allowed to exceed 8 bits for weighting purposes
	Key_t  *m_keys;
	Val_t  *m_vals;

	long m_numSlots;
	long m_numSlotsUsed;

	char m_allowDupKeys;

	char m_doFree;

	char *m_buf; //. hash table buffer points to callers buffer on stack
	long m_bufSize;
	
	// char m_buf1 [ HT_BUF_SIZE ];
	// char m_buf2 [ HT_BUF_SIZE ];
};

bool hashFromString ( HashTableT<long long,char> *ht , char *x ) ;

#endif





