// Matt Wells,  Copyright, Dec. 2002

// . generic hash table class

#ifndef _HASHTABLEX_H_
#define _HASHTABLEX_H_

#include "Mem.h"     // for mcalloc and mmalloc

class HashTableX {

 public:

	bool set ( long  keySize         ,
		   long  dataSize        ,
		   long  initialNumSlots , // = 0    ,
		   char *buf             , // = NULL ,
		   long  bufSize         , // = 0    ,
		   bool  allowDups       , // = false ,
		   long  niceness        , // = MAX_NICENESS ,
		   char *allocName       );

	 HashTableX       ( );
	~HashTableX       ( );
	void constructor ();
	void destructor ();

	// . add key/value entry to hash table
	// . will grow hash table if it needs to
	// . returns false and sets g_errno on error, returns true otherwise
	bool addKey ( void *key , void *value , long *slot = NULL );

	// for value-less hashtables
	bool addKey ( void *key );

	// . remove key/value entry to hash table. 
	// . returns false and sets g_errno on error.
	bool removeKey  ( void *key );

	// same as remove
	bool deleteSlot ( long n ) { return removeSlot(n); };

	// like removeKey. returns false and sets g_errno on error.
	bool removeSlot ( long n );

	// see how optimal the hashtable is
	long getLongestString ();

	// how many keys are dups
	long getNumDups();

	bool addFloat ( long *wid , float score ) {
		long slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                float *val = (float *)getValueFromSlot ( slot );
		*val = *val + score;
		return true;
	};


	// a replacement for TermTable.cpp
	bool addTerm ( long long *wid , long score = 1 ) {
		long slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	bool addTerm64 ( char *str ) {
		uint64_t wid64 = hash64n ( str );
		return addTerm64 ( (long long *)&wid64 );
	};
	bool addTerm64 ( long long *wid , long score = 1 ) {
		return addTerm(wid,score); }
	// a replacement for TermTable.cpp
	uint32_t getScore ( long long *wid ) {
		long slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(uint32_t *)getValueFromSlot ( slot );
	};
	// a replacement for TermTable.cpp
	uint32_t getScoreFromSlot ( long slot ) {
		return *(uint32_t *)getValueFromSlot ( slot ); };
	uint64_t getScore64FromSlot ( long slot ) {
		return *(uint64_t *)getValueFromSlot ( slot ); };


	bool addTerm32 ( char *str ) {
		uint32_t wid32 = hash32n ( str );
		return addTerm32 ( &wid32 );
	};

	bool addTerm32 ( long *wid , long score = 1 ) {
		long slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	bool addTerm32 ( uint32_t *wid , long score = 1 ) {
		long slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	bool addTerm32 ( unsigned long *wid , long score = 1 ) {
		long slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	uint32_t getScore32 ( long *wid ) {
		long slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(uint32_t *)getValueFromSlot ( slot );
	};
	uint32_t getScore32 ( unsigned long *wid ) {
		long slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(uint32_t *)getValueFromSlot ( slot );
	};


	bool addTerm144 ( key144_t *kp , long score = 1 ) {
		// grow it!
		if ( (m_numSlots < 20 || 4 * m_numSlotsUsed >= m_numSlots) &&
		     m_numSlots < m_maxSlots ) {
			long long growTo ;
			growTo = ((long long)m_numSlots * 150LL )/100LL+20LL;
			if ( growTo > m_maxSlots ) growTo = m_maxSlots;
			if ( ! setTableSize ( (long)growTo , NULL , 0 ) ) 
				return false;
		}
		// hash it up
		long n = hash32 ( (char *)kp, 18 );
		// then mask it
		n &= m_mask;
		long count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) {
				memcpy( &((key144_t *)m_keys)[n] ,kp,18);
				m_vals[n*m_ds] = score;
				m_flags[n] = 1;
				m_numSlotsUsed++;
				return true;
			}
			// get the key there
			if (((key144_t *)m_keys)[n] == *kp) {
				uint32_t *val = (uint32_t *)&m_vals[n*m_ds];
				// overflow check
				if ( *val + (uint32_t)score < *val ) 
					*val = 0xffffffff;
				else 
					*val = *val + score;
				return true;
			}
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		// crazy!
		log("hash: table is full!");
		char *xx=NULL;*xx=0;
		return true;
	};

	// return 32-bit checksum of keys in table
	long getKeyChecksum32 ();

	long getSlot144 ( key144_t *kp ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return -1;
		// sanity check
		if ( m_ks != 18 ) { char *xx=NULL;*xx=0; }
		// mask on termid bits i guess
		//long n = *((unsigned long *)(((char *)kp)+12));
		// xor with word posand hashgroup ,etc
		//n ^= *((unsigned long *)(((char *)kp)+2));
		long n = hash32 ( (char *)kp, 18 );
		// then mask it
		n &= m_mask;
		long count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return -1;
			// get the key there
			if (((key144_t *)m_keys)[n] == *kp) 
				return n;
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return -1;
	};

	// . used by ../english/Bits.h to store stop words, abbr's, ...
	// . returns the score for this termId (0 means empty usually)
	// . return 0 if key not in hash table
	void *getValue ( void *key ) {
		// make it fast
		if ( m_ks == 4 ) return getValue32 ( *(long *)key );
		if ( m_ks == 8 ) return getValue64 ( *(long long *)key );
		// returns -1 if key not in hash table
		long n = getOccupiedSlotNum ( key );
		if ( n < 0 ) return NULL;
		return &m_vals[n*m_ds];
	};

	long getSlot32 ( long key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return -1;
		// sanity check
		if ( m_ks != 4 ) { char *xx=NULL;*xx=0; }
		// mask on the lower 32 bits i guess
		long n = key & m_mask;
		long count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return -1;
			// get the key there
			if (((long *)m_keys)[n] == key) 
				return n;
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return -1;
	};

	// . specialized for 32-bit keys for speed
	// . returns NULL if not in table
	void *getValue32 ( long key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return NULL;
		// sanity check
		if ( m_ks != 4 ) { char *xx=NULL;*xx=0; }
		// mask on the lower 32 bits i guess
		long n = key & m_mask;
		long count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return NULL;
			// get the key there
			if (((long *)m_keys)[n] == key) 
				return &m_vals[n*m_ds]; 
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return NULL;
	};

	// . specialized for 64-bit keys for speed
	// . returns NULL if not in table
	void *getValue64 ( long long key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return NULL;
		// sanity check
		if ( m_ks != 8 ) { char *xx=NULL;*xx=0; }
		// mask on the lower 32 bits i guess
		long n = key & m_mask;
		long count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return NULL;
			// get the key there
			if (((long long *)m_keys)[n] == key) 
				return &m_vals[n*m_ds]; 
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return NULL;
	};

	// value of 0 means empty
	bool isEmpty ( void *key ) { return (getSlot(key) < 0); };

	bool isInTable ( void *key ) { return (getSlot(key) >= 0); };

	bool isEmpty ( long n ) { return (m_flags[n] == 0); };

	bool isTableEmpty ( ) { return (m_numSlotsUsed == 0); };

	void *getKey ( long n ) { return m_keys + n * m_ks; };
	void *getKeyFromSlot ( long n ) { return m_keys + n * m_ks; };

	long long getKey64FromSlot ( long n ) {
		return *(long long *)(m_keys+n*m_ks); }

	long getSlot ( void *key ) { return getOccupiedSlotNum ( key ); };

	// . specialized for 64-bit keys for speed
	// . returns -1 if not in table
	long getSlot64 ( long long *key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return -1;
		// sanity check
		if ( m_ks != 8 ) { char *xx=NULL;*xx=0; }
		// mask on the lower 32 bits i guess
		long n = *key & m_mask;
		long count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return -1;
			// get the key there
			if (((long long *)m_keys)[n] == *key) 
				return n;
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return -1;
	};

	long getNextSlot ( long slot , void *key );

	// count how many slots have this key
	long getCount ( void *key );

	void setValue ( long n , void *val ) { 
		if      (m_ds == 4) ((int32_t *)m_vals)[n] = *(int32_t *)val;
		else if (m_ds == 8) ((int64_t *)m_vals)[n] = *(int64_t *)val;
		else                memcpy(m_vals+n*m_ds,val,m_ds);
	};

	void *getValueFromSlot ( long n ) { return m_vals + n * m_ds; };
	void *getDataFromSlot  ( long n ) { return m_vals + n * m_ds; };

	// frees the used memory, etc.
	void  reset  ( );

	// removes all key/value pairs from hash table, vacates all slots
	void  clear  ( );

	// how many are occupied?
	long getNumSlotsUsed ( ) { return m_numSlotsUsed; };
	long getNumUsedSlots ( ) { return m_numSlotsUsed; };

	// how many are there total? used and unused.
	long getNumSlots ( ) { return m_numSlots; };

	// how many bytes are required to serialize this hash table?
	long getStoredSize();
	// return buffer we allocated and stored into. return -1 on error
	// with g_errno set.
	char *serialize ( long *bufSize ) ;
	// shortcut
	long serialize ( class SafeBuf *sb );
	// returns # bytes written into "buf"
	long serialize ( char *buf , long bufSize );
	// inflate it. returns false with g_errno set on error
	bool deserialize ( char *buf , long bufSize , long niceness );

	// both return false and set g_errno on error, true otherwise
	bool load ( char *dir , char *filename , 
		    char **tbuf = NULL , long *tsize = NULL );

	bool save ( char *dir , char *filename , 
		    char  *tbuf = NULL , long  tsize = 0);

	bool save ( char *dir , char *filename , SafeBuf *tbuf ) {
		return save ( dir,
			      filename,
			      tbuf->getBufStart(),
			      tbuf->length());
	};

	bool load ( char *dir , char *filename ,  SafeBuf *fillBuf );

	// thread based save
	bool fastSave ( bool useThread ,
			char *dir , 
			char *filename , 
			char *tbuf ,
			long tsize ,
			void *state ,
			void (* callback)(void *state) );

	bool setTableSize ( long numSlots , char *buf , long bufSize );

	void disableWrites () { m_isWritable = false; };
	void enableWrites  () { m_isWritable = true ; };
	bool m_isWritable;

 private:

	long getOccupiedSlotNum ( void *key ) ;

 public:

	// . the array of buckets in which we store the terms
	// . scores are allowed to exceed 8 bits for weighting purposes
	char  *m_keys;
	char  *m_vals;
	char  *m_flags;

	long     m_numSlots;
	long     m_numSlotsUsed;
	uint32_t m_mask;

	char  m_doFree;
	char *m_buf;
	long  m_bufSize;

	long m_ks;
	long m_ds;
	char m_allowDups;
	long m_niceness;

	// a flag used by XmlDoc.cpp
	bool m_addIffNotUnique;

	bool m_isSaving;
	bool m_needsSave;

	char  m_dir[100];
	char  m_filename[64];
	void *m_state    ;
	void (* m_callback) ( void *state);
	char *m_tbuf     ;
	long  m_tsize    ;

	// limits growing to this # of slots total
	long long  m_maxSlots;

	char *m_allocName;
	
	long m_maskKeyOffset;

	// the addon buf used by SOME hashtables. data that the ptrs
	// in the table itself reference.
	char *m_txtBuf;
	long  m_txtBufSize;
};

#endif





