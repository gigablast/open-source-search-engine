// Matt Wells,  Copyright, Dec. 2002

// . generic hash table class

#ifndef _HASHTABLEX_H_
#define _HASHTABLEX_H_

#include "SafeBuf.h"

class HashTableX {

 public:

	bool set ( int32_t  keySize         ,
		   int32_t  dataSize        ,
		   int32_t  initialNumSlots , // = 0    ,
		   char *buf             , // = NULL ,
		   int32_t  bufSize         , // = 0    ,
		   bool  allowDups       , // = false ,
		   int32_t  niceness        , // = MAX_NICENESS ,
		   char *allocName       ,
		   bool  useKeyMagic = false );

	// key size is 0 if UNinitialized
	bool isInitialized ( ) { return (m_ks != 0); };

	 HashTableX       ( );
	~HashTableX       ( );
	void constructor ();
	void destructor ();

	// . add key/value entry to hash table
	// . will grow hash table if it needs to
	// . returns false and sets g_errno on error, returns true otherwise
	bool addKey ( void *key , void *value , int32_t *slot = NULL );

	// for value-less hashtables
	bool addKey ( void *key );

	// . remove key/value entry to hash table. 
	// . returns false and sets g_errno on error.
	bool removeKey  ( void *key );

	// same as remove
	bool deleteSlot ( int32_t n ) { return removeSlot(n); };

	// like removeKey. returns false and sets g_errno on error.
	bool removeSlot ( int32_t n );

	// see how optimal the hashtable is
	int32_t getLongestString ();

	// how many keys are dups
	int32_t getNumDups();

	// if in a thread to dont allow it to grow
	void setNonGrow() { m_allowGrowth = false; }
	bool m_allowGrowth;

	bool addFloat ( int32_t *wid , float score ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                float *val = (float *)getValueFromSlot ( slot );
		*val = *val + score;
		return true;
	};


	// a replacement for TermTable.cpp
	bool addTerm ( int64_t *wid , int32_t score = 1 ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	bool addTerm64 ( char *str ) {
		uint64_t wid64 = hash64n ( str );
		return addTerm64 ( (int64_t *)&wid64 );
	};
	bool addTerm64 ( int64_t *wid , int32_t score = 1 ) {
		return addTerm(wid,score); }
	// a replacement for TermTable.cpp
	uint32_t getScore ( int64_t *wid ) {
		int32_t slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(uint32_t *)getValueFromSlot ( slot );
	};
	// a replacement for TermTable.cpp
	uint32_t getScoreFromSlot ( int32_t slot ) {
		return *(uint32_t *)getValueFromSlot ( slot ); };
	uint64_t getScore64FromSlot ( int32_t slot ) {
		return *(uint64_t *)getValueFromSlot ( slot ); };


	bool addTerm32 ( char *str ) {
		uint32_t wid32 = hash32n ( str );
		return addTerm32 ( &wid32 );
	};

	bool addTerm32 ( int32_t *wid , int32_t score = 1 ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	//bool addTerm32 ( uint32_t *wid , int32_t score = 1 ) {
	//	int32_t slot = getSlot ( wid );
	//      if ( slot<0 ) return addKey( wid ,&score,&slot);
        //      uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
	//	// overflow check
	//	if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
	//	else                                 *val = *val + score;
	//	return true;
	//};
	bool addTerm32 ( uint32_t *wid , int32_t score = 1 ) {
		int32_t slot = getSlot ( wid );
                if ( slot<0 ) return addKey( wid ,&score,&slot);
                uint32_t *val = (uint32_t *)getValueFromSlot ( slot );
		// overflow check
		if ( *val + (uint32_t)score < *val ) *val = 0xffffffff;
		else                                 *val = *val + score;
		return true;
	};
	bool addScore ( int32_t *key , int32_t score = 1 ) {
		return addTerm32 ( key , score ); 
	};
	uint32_t getScore32 ( int32_t *wid ) {
		int32_t slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(uint32_t *)getValueFromSlot ( slot );
	};
	uint32_t getScore32 ( uint32_t *wid ) {
		int32_t slot = getSlot ( wid );
		if ( slot < 0 ) return 0;
		return *(uint32_t *)getValueFromSlot ( slot );
	};


	bool addTerm144 ( key144_t *kp , int32_t score = 1 ) {

		/*
		// debug XmlDoc.cpp's hash table
		int64_t termId = ((key144_t *)kp)->n2 >> 16;
		uint64_t d = 0LL;
		d = ((unsigned char *)kp)[11];
		d <<= 32;
		d |= *(uint32_t *)(((unsigned char *)kp)+7);
		d >>= 2;
		if ( d==110324895284 && termId == 39206941907955LL ) {
			log("got it");
			char *xx=NULL;*xx=0;
		}
		*/
		// grow it!
		if ( (m_numSlots < 20 || 4 * m_numSlotsUsed >= m_numSlots) &&
		     m_numSlots < m_maxSlots ) {
			int64_t growTo ;
			growTo = ((int64_t)m_numSlots * 150LL )/100LL+20LL;
			if ( growTo > m_maxSlots ) growTo = m_maxSlots;
			if ( ! setTableSize ( (int32_t)growTo , NULL , 0 ) ) 
				return false;
		}
		// hash it up
		int32_t n = hash32 ( (char *)kp, 18 );
		// then mask it
		n &= m_mask;
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) {
				gbmemcpy( &((key144_t *)m_keys)[n] ,kp,18);
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
	int32_t getKeyChecksum32 ();

	int32_t getSlot144 ( key144_t *kp ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return -1;
		// sanity check
		if ( m_ks != 18 ) { char *xx=NULL;*xx=0; }
		// mask on termid bits i guess
		//int32_t n = *((uint32_t *)(((char *)kp)+12));
		// xor with word posand hashgroup ,etc
		//n ^= *((uint32_t *)(((char *)kp)+2));
		int32_t n = hash32 ( (char *)kp, 18 );
		// then mask it
		n &= m_mask;
		int32_t count = 0;
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
		if ( m_ks == 4 ) return getValue32 ( *(int32_t *)key );
		if ( m_ks == 8 ) return getValue64 ( *(int64_t *)key );
		// returns -1 if key not in hash table
		int32_t n = getOccupiedSlotNum ( key );
		if ( n < 0 ) return NULL;
		return &m_vals[n*m_ds];
	};

	int32_t getSlot32 ( int32_t key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return -1;
		// sanity check
		if ( m_ks != 4 ) { char *xx=NULL;*xx=0; }
		int32_t n;
		if ( ! m_useKeyMagic ) {
			// mask on the lower 32 bits i guess
			n = key & m_mask;
		}
		else {
			// get lower 32 bits of key
			n =*(uint32_t *)(((char *)&key) +m_maskKeyOffset);
			// use magic to "randomize" key a little
			n^=g_hashtab[(unsigned char)((char *)&key)[m_maskKeyOffset]][0];
			// mask on the lower 32 bits i guess
			n &= m_mask;
		}
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return -1;
			// get the key there
			if (((int32_t *)m_keys)[n] == key) 
				return n;
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return -1;
	};

	// . specialized for 32-bit keys for speed
	// . returns NULL if not in table
	void *getValue32 ( int32_t key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return NULL;
		// sanity check
		if ( m_ks != 4 ) { char *xx=NULL;*xx=0; }
		int32_t n;
		if ( ! m_useKeyMagic ) {
			// mask on the lower 32 bits i guess
			n = key & m_mask;
		}
		else {
			// get lower 32 bits of key
			//n = (uint32_t)key;
			n =*(uint32_t *)(((char *)&key) +m_maskKeyOffset);
			// use magic to "randomize" key a little
			n^=g_hashtab[(unsigned char)((char *)&key)[m_maskKeyOffset]][0];
			// mask on the lower 32 bits i guess
			n &= m_mask;
		}
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return NULL;
			// get the key there
			if (((int32_t *)m_keys)[n] == key) 
				return &m_vals[n*m_ds]; 
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return NULL;
	};

	// . specialized for 64-bit keys for speed
	// . returns NULL if not in table
	void *getValue64 ( int64_t key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return NULL;
		// sanity check
		if ( m_ks != 8 ) { char *xx=NULL;*xx=0; }
		int32_t n;
		if ( ! m_useKeyMagic ) {
			// mask on the lower 32 bits i guess
			// get lower 32 bits of key
			n = key & m_mask;
		}
		else {
			// use magic to "randomize" key a little
			n =*(uint32_t *)(((char *)&key) +m_maskKeyOffset);
			n ^= g_hashtab[(unsigned char)((char *)&key)[m_maskKeyOffset]][0];
			// mask on the lower 32 bits i guess
			n &= m_mask;
		}
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return NULL;
			// get the key there
			if (((int64_t *)m_keys)[n] == key) 
				return &m_vals[n*m_ds]; 
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return NULL;
	};

	// value of 0 means empty
	bool isEmpty ( void *key ) { return (getSlot(key) < 0); };

	bool isInTable ( void *key ) { return (getSlot(key) >= 0); };

	bool isEmpty ( int32_t n ) { return (m_flags[n] == 0); };

	bool isTableEmpty ( ) { return (m_numSlotsUsed == 0); };

	void *getKey ( int32_t n ) { return m_keys + n * m_ks; };
	void *getKeyFromSlot ( int32_t n ) { return m_keys + n * m_ks; };

	int64_t getKey64FromSlot ( int32_t n ) {
		return *(int64_t *)(m_keys+n*m_ks); }

	int32_t getKey32FromSlot ( int32_t n ) {
		return *(int32_t *)(m_keys+n*m_ks); }

	int32_t getSlot ( void *key ) { return getOccupiedSlotNum ( key ); };

	// . specialized for 64-bit keys for speed
	// . returns -1 if not in table
	int32_t getSlot64 ( int64_t *key ) {
		// return NULL if completely empty
		if ( m_numSlots <= 0 ) return -1;
		// sanity check
		if ( m_ks != 8 ) { char *xx=NULL;*xx=0; }
		int32_t n;
		if ( ! m_useKeyMagic ) {
			// mask on the lower 32 bits i guess
			n = *key & m_mask;
		}
		else {
			// use magic to "randomize" key a little
			n =*(uint32_t *)(((char *)&key) +m_maskKeyOffset);
			n ^= g_hashtab[(unsigned char)((char *)key)[m_maskKeyOffset]][0];
			// mask on the lower 32 bits i guess
			n &= m_mask;
		}
		int32_t count = 0;
		while ( count++ < m_numSlots ) {
			// this is set to 0x01 if non-empty
			if ( m_flags [ n ] == 0 ) return -1;
			// get the key there
			if (((int64_t *)m_keys)[n] == *key) 
				return n;
			// advance otherwise
			if ( ++n == m_numSlots ) n = 0;
		}
		return -1;
	};

	int32_t getNextSlot ( int32_t slot , void *key );

	// count how many slots have this key
	int32_t getCount ( void *key );

	void setValue ( int32_t n , void *val ) { 
		if      (m_ds == 4) ((int32_t *)m_vals)[n] = *(int32_t *)val;
		else if (m_ds == 8) ((int64_t *)m_vals)[n] = *(int64_t *)val;
		else                gbmemcpy(m_vals+n*m_ds,val,m_ds);
	};

	void *getValueFromSlot ( int32_t n ) { return m_vals + n * m_ds; };
	void *getValFromSlot   ( int32_t n ) { return m_vals + n * m_ds; };
	void *getDataFromSlot  ( int32_t n ) { return m_vals + n * m_ds; };

	int32_t getVal32FromSlot ( int32_t n ){return *(int32_t *)(m_vals+n*m_ds);};
	int32_t getValue32FromSlot ( int32_t n ){return *(int32_t *)(m_vals+n*m_ds);};

	// frees the used memory, etc.
	void  reset  ( );

	// removes all key/value pairs from hash table, vacates all slots
	void  clear  ( );

	// how many are occupied?
	int32_t getNumSlotsUsed ( ) { return m_numSlotsUsed; };
	int32_t getNumUsedSlots ( ) { return m_numSlotsUsed; };

	bool isEmpty() { 
		if ( m_numSlotsUsed == 0 ) return true;
		return false; };

	// how many are there total? used and unused.
	int32_t getNumSlots ( ) { return m_numSlots; };

	// how many bytes are required to serialize this hash table?
	int32_t getStoredSize();
	// return buffer we allocated and stored into. return -1 on error
	// with g_errno set.
	char *serialize ( int32_t *bufSize ) ;
	// int16_tcut
	int32_t serialize ( class SafeBuf *sb );
	// returns # bytes written into "buf"
	int32_t serialize ( char *buf , int32_t bufSize );
	// inflate it. returns false with g_errno set on error
	bool deserialize ( char *buf , int32_t bufSize , int32_t niceness );

	// both return false and set g_errno on error, true otherwise
	bool load ( char *dir , char *filename , 
		    char **tbuf = NULL , int32_t *tsize = NULL );

	bool save ( char *dir , char *filename , 
		    char  *tbuf = NULL , int32_t  tsize = 0);

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
			int32_t tsize ,
			void *state ,
			void (* callback)(void *state) );

	bool setTableSize ( int32_t numSlots , char *buf , int32_t bufSize );

	// print as text into sb for debugging
	void print ( class SafeBuf *sb );

	void disableWrites () { m_isWritable = false; };
	void enableWrites  () { m_isWritable = true ; };
	bool m_isWritable;

 private:

	int32_t getOccupiedSlotNum ( void *key ) ;

 public:

	// . the array of buckets in which we store the terms
	// . scores are allowed to exceed 8 bits for weighting purposes
	char  *m_keys;
	char  *m_vals;
	char  *m_flags;

	int32_t     m_numSlots;
	int32_t     m_numSlotsUsed;
	uint32_t m_mask;

	char  m_doFree;
	char *m_buf;
	int32_t  m_bufSize;

	char m_useKeyMagic;

	int32_t m_ks;
	int32_t m_ds;
	char m_allowDups;
	int32_t m_niceness;

	// a flag used by XmlDoc.cpp
	bool m_addIffNotUnique;

	bool m_isSaving;
	bool m_needsSave;

	char  m_dir[100];
	char  m_filename[64];
	void *m_state    ;
	void (* m_callback) ( void *state);
	char *m_tbuf     ;
	int32_t  m_tsize    ;

	// limits growing to this # of slots total
	int64_t  m_maxSlots;

	char *m_allocName;
	
	int32_t m_maskKeyOffset;

	// the addon buf used by SOME hashtables. data that the ptrs
	// in the table itself reference.
	char *m_txtBuf;
	int32_t  m_txtBufSize;
};

#endif





