#include "gb-include.h"

#include "HashTable.h"

HashTable::HashTable () {
	m_keys = NULL;
	m_vals = NULL;
	m_numSlots     = 0;
	m_numSlotsUsed = 0;
	m_doFree       = true;
	m_label = NULL;
}

// returns false and sets errno on error
bool HashTable::set ( int32_t initialNumTerms , char *buf , int32_t bufSize ,
		      char *label ) {
	reset();
	m_label = label;
	if ( ! m_label ) m_label = "hashtablekv";
	return setTableSize ( initialNumTerms , buf , bufSize );
}

HashTable::~HashTable ( ) { reset ( ); }

// . call clean() to do a more careful reset
// . clean will rehash
void HashTable::reset ( ) {
	if ( m_doFree ) {
		if (m_keys) mfree(m_keys,m_numSlots*sizeof(int32_t),"hashtablev");
		if (m_vals) mfree(m_vals,m_numSlots*sizeof(int32_t),"hashtablev");
	}
	m_keys = NULL;
	m_vals = NULL;
	m_numSlots     = 0;
	m_numSlotsUsed = 0;
	// do not do this because then ::load() fails b/c you can't
	// pass a label into that yet
	//m_label = NULL;
}

void HashTable::clear ( ) {
	// vacate all slots
	if ( m_keys ) memset ( m_keys , 0 , sizeof(int32_t) * m_numSlots );
	m_numSlotsUsed = 0;
}	 

// . returns the slot number for "key"
// . returns -1 if key not in hash table
int32_t HashTable::getOccupiedSlotNum ( int32_t key ) {
	if ( m_numSlots <= 0 ) return -1;
        //int32_t n = ((uint32_t)key) % ((uint32_t)m_numSlots);
        int32_t n = ((uint32_t)key) & m_mask;
        int32_t count = 0;
        while ( count++ < m_numSlots ) {
                if ( m_keys [ n ] == 0   ) return -1;
		if ( m_keys [ n ] == key ) return  n;
		if ( ++n == m_numSlots ) n = 0;
        }
        log("hashtable: Could not get key. Table is full.");
        return -1;
}

// return 0 if key not in hash table
int32_t HashTable::getValue ( int32_t key ) {
	// returns -1 if key not in hash table
	int32_t n = getOccupiedSlotNum ( key );
	if ( n < 0 ) return 0;
	return m_vals[n];
}

// . returns false and sets errno on error, returns true otherwise
// . adds scores if termId already exists in table
bool HashTable::addKey ( int32_t key , int32_t value , int32_t *slot ) {
	// keys of 0 mean empty! they are reserved... fix that!
	if ( key == 0 ) { char *xx=NULL; *xx=0; }
	// check to see if we should grow the table
	if ( 100 * m_numSlotsUsed >= m_numSlots * 90 ) {
		int32_t growTo = (m_numSlots * 120 ) / 100  + 20;
		if ( ! setTableSize ( growTo , NULL , 0 ) ) return false;
	}
        //int32_t n = ((uint32_t)key) % ((uint32_t)m_numSlots);
        int32_t n = ((uint32_t)key) & m_mask;
        int32_t count = 0;
        while ( count++ < m_numSlots ) {
                if ( m_keys [ n ] == 0   ) break;
		if ( m_keys [ n ] == key ) break;
		if ( ++n == m_numSlots ) n = 0;
        }
	// bail if not found
	if ( count >= m_numSlots ) {
		g_errno = ENOMEM;
		return log("hashtable: Could not add key. Table is full.");
	}
	if ( m_keys [ n ] == 0 ) {
		// inc count if we're the first
		m_numSlotsUsed++;
		// and store the ky
		m_keys [ n ] = key;
	}
	// insert the value for this key
	m_vals [ n ] = value;
	if ( slot ) *slot = n;
	return true;
}

// patch the hole so chaining still works
bool HashTable::removeKey ( int32_t key ) {
	// returns -1 if key not in hash table
	int32_t n = getOccupiedSlotNum(key);
	if ( n < 0 ) return true;
	m_keys[n] = 0;
	m_numSlotsUsed--;
	if ( ++n >= m_numSlots ) n = 0;
	// keep looping until we hit an empty slot
	int32_t val;
	while ( m_keys[n] ) {
		key = m_keys[n];
		val = m_vals[n];
		m_keys[n] = 0;
		m_numSlotsUsed--;
		addKey ( key , val );
		if ( ++n >= m_numSlots ) n = 0;		
	}
	return true;
}

// patch the hole so chaining still works
void HashTable::removeSlot ( int32_t n ) {
	// returns -1 if key not in hash table
	//int32_t n = getOccupiedSlotNum(key);
	//if ( n < 0 ) return true;
	int32_t key = m_keys[n];
	// sanity check, must not be empty
	if ( key == 0 ) { char *xx = NULL; *xx = 0; }
	// delete it
	m_keys[n] = 0;
	m_numSlotsUsed--;
	if ( ++n >= m_numSlots ) n = 0;
	// keep looping until we hit an empty slot
	int32_t val;
	while ( m_keys[n] ) {
		key = m_keys[n];
		val = m_vals[n];
		m_keys[n] = 0;
		m_numSlotsUsed--;
		addKey ( key , val );
		if ( ++n >= m_numSlots ) n = 0;		
	}
}

// . set table size to "n" slots
// . rehashes the termId/score pairs into new table
// . returns false and sets errno on error
bool HashTable::setTableSize ( int32_t oldn , char *buf , int32_t bufSize ) {
	// don't change size if we do not need to
	if ( oldn == m_numSlots ) return true;
	// make it a power of 2
	int32_t n = getHighestLitBitValue ( (uint32_t)oldn * 2 - 1 );
	// do not go negative on me
	if ( oldn == 0 ) n = 0;
	// sanity check
	if ( n < oldn ) { char *xx = NULL; *xx = 0; }
	// do we have a buf?
	int32_t need = 2 * n * sizeof(int32_t);
	// sanity check, buf should also meet what we need
	if ( buf && bufSize < need ) { char *xx = NULL; *xx = 0; }
	// set the buf
	int32_t *newKeys ;
	int32_t *newVals ;
	// if we should not free note that
	bool savedDoFree = m_doFree ;
	// use our buf if we can
	if ( buf ) {
		m_doFree = false;
		bzero ( buf , need );
		newKeys = (int32_t *)buf;
		buf += n * sizeof(int32_t);
		newVals = (int32_t *)buf;
		buf += n * sizeof(int32_t);
	}
	else {
		m_doFree = true;
		char *label = m_label;
		if ( ! label ) label = "hashtablev";
		newKeys = (int32_t *)mcalloc ( n * sizeof(int32_t) , label);
		if ( ! newKeys ) return false;
		newVals = (int32_t *)mmalloc ( n * sizeof(int32_t) , label);
		if ( ! newVals ) {
			mfree ( newKeys , n * sizeof(int32_t) , label );
			return false;
		}
	}
	// rehash the slots if we had some
	if ( m_keys ) {
		for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
			// skip the empty slots 
			if ( m_keys [ i ] == 0 ) continue;
			// get the new slot # for this slot (might be the same)
			//int32_tnum=((uint32_t)m_keys[i])%((uint32_t)n)
			int32_t num=((uint32_t)m_keys[i])&
				((uint32_t)(n-1));
			// if that is occupied, go down
			while ( newKeys[num] ) if ( ++num >= n ) num = 0;
			// move the slotPtr/key/size to this new slot
			newKeys [ num ] = m_keys [ i ];
			newVals [ num ] = m_vals [ i ];
		}
	}
	// free the old guys
	if ( m_keys && savedDoFree ) {
		mfree ( m_keys , m_numSlots * sizeof(int32_t) , "hashtablev" );
		mfree ( m_vals , m_numSlots * sizeof(int32_t) , "hashtablev" );
	}
	// assign the new slots, m_numSlotsUsed should be the same
	m_keys = newKeys;
	m_vals = newVals;
	m_numSlots = n;
	m_mask     = n - 1;
	return true;
}

// both return false and set g_errno on error, true otherwise
bool HashTable::load ( char *dir , char *filename ) {
	reset();
	File f;
	f.set ( dir , filename );
	if ( ! f.doesExist() ) return true;
	log(LOG_INFO,"admin: Loading hashtable from %s%s",dir,filename);
	if ( ! f.open ( O_RDONLY) ) return false;
	int32_t numSlots;
	int32_t numSlotsUsed;
	int32_t off = 0;
	if ( ! f.read ( &numSlots     , 4 , off ) ) return false;
	off += 4;
	if ( ! f.read ( &numSlotsUsed , 4 , off ) ) return false;
	off += 4;
	if ( ! setTableSize ( numSlots , NULL , 0 ) ) return false;
	if ( ! f.read ( m_keys        , numSlots * 4 , off ) ) return false;
	off += numSlots * 4;
	if ( ! f.read ( m_vals        , numSlots * 4 , off ) ) return false;
	off += numSlots * 4;
	m_numSlotsUsed = numSlotsUsed;
	f.close();
	return true;
}

bool HashTable::save ( char *dir , char *filename ) {
	File f;
	f.set ( dir , filename );
	log(LOG_INFO,"admin: Saving hashtable from %s%s",dir,filename);
	if ( ! f.open ( O_RDWR | O_CREAT ) ) return false;
	int32_t numSlots     = m_numSlots;
	int32_t numSlotsUsed = m_numSlotsUsed;
	int32_t off = 0;
	if ( ! f.write ( &numSlots     , 4 , off ) ) return false;
	off += 4;
	if ( ! f.write ( &numSlotsUsed , 4 , off ) ) return false;
	off += 4;
	if ( ! f.write ( m_keys        , numSlots * 4 , off ) ) return false;
	off += numSlots * 4;
	if ( ! f.write ( m_vals        , numSlots * 4 , off ) ) return false;
	off += numSlots * 4;
	f.close();
	return true;
}
