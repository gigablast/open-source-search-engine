#include "gb-include.h"

#include "HashTableT.h"
#include "Profiler.h" //For fnInfo struct
#include "Title.h" // For Title::InLinkInfo
#include "Dns.h"
//#include "Thesaurus.h" // SynonymLinkGroup structure
#include "PostQueryRerank.h" // for ComTopInDmozRec
//#include "DateParse.h" // TimeZoneInfo structure
//#include "PageTurk.h" // TurkUserState structure
#include "types.h"

template<class Key_t, class Val_t> 
HashTableT<Key_t, Val_t>::HashTableT() {
/*	m_keys = NULL;
	m_vals = NULL;
	m_numSlots     = 0;
	m_numSlotsUsed = 0;
	m_allowDupKeys = false;
	m_doFree = true;
	m_buf = NULL;
	m_bufSize = 0;
	*/
	constructor();
}


// . call clean() to do a more careful reset
// . clean will rehash
template<class Key_t, class Val_t> 
void  HashTableT<Key_t, Val_t>::reset ( ) {
	//if ( m_keys && m_keys!=(Key_t *)m_buf1 && m_keys!=(Key_t *)m_buf2){
	//	mfree ( m_keys, m_numSlots * sizeof(Key_t), 
	//		"HashTableTk");
	//	mfree ( m_vals, m_numSlots * sizeof(Val_t), 
	//		"HashTableTv");
	//}
	if ( m_doFree && m_keys != (Key_t *)m_buf){
		if (m_keys) mfree(m_keys,m_numSlots*sizeof(Key_t),"HashTablek");
		if (m_vals) mfree(m_vals,m_numSlots*sizeof(Val_t),"HashTablev");
	}
	m_keys = NULL;
	m_vals = NULL;
	m_numSlots     = 0;
	m_numSlotsUsed = 0;
	m_buf = NULL;
	m_bufSize = 0; 
}


// . function used by tagdb list cache to
//  to initialize the class members
//  as it does not use new or local member
template<class Key_t, class Val_t>
void HashTableT<Key_t, Val_t>::constructor(){
        m_keys = NULL;
	m_vals = NULL;
	m_numSlots     = 0;
	m_numSlotsUsed = 0;
	m_allowDupKeys = false;
	m_doFree = true;
	m_buf = NULL;
	m_bufSize = 0;
}



// returns false and sets errno on error
template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::set ( long initialNumTerms, char *buf, long bufSize, bool allowDupKeys) {
	reset();
	m_allowDupKeys = allowDupKeys;
//	return setTableSize ( initialNumTerms );
	// . set table size with buffer and bufferSize
	return setTableSize ( initialNumTerms, buf, bufSize );
}

template<class Key_t, class Val_t>
HashTableT<Key_t, Val_t>::~HashTableT() { reset ( ); }



template<class Key_t, class Val_t>
bool HashTableT<Key_t, Val_t>::copy(HashTableT<Key_t, Val_t>* src) {
	long numSlots = src->m_numSlots;
	long keySize = numSlots * sizeof(Key_t);
	long valSize = numSlots * sizeof(Val_t);
	Key_t *newKeys = (Key_t *)mmalloc(keySize, "HashTableTk");
	Val_t *newVals = (Val_t *)mmalloc(valSize, "HashTableTv");
	if(!newKeys || !newVals) {
		if (newKeys) mfree(newKeys, keySize,
			"HashTableTk");
		if (newVals) mfree(newVals, valSize,
			"HashTableTv");
		return false;
	}
	// maybe this should be a member copy, but that's a LOT slower and
	//  bitwise should work with everything we're using the HashTableT 
	//  for so far
	memcpy(newKeys, src->m_keys, keySize);
	memcpy(newVals, src->m_vals, valSize);
	reset();
	m_keys = newKeys;
	m_vals = newVals;
	m_numSlots = src->getNumSlots();
	m_numSlotsUsed = src->getNumSlotsUsed();
	m_allowDupKeys = src->getAllowDupKeys();
	return true;
}


template<class Key_t, class Val_t>
void HashTableT<Key_t, Val_t>::clear ( ) {
	// vacate all slots
	if ( m_keys ) memset ( m_keys , 0 , sizeof(Key_t) * m_numSlots );
	m_numSlotsUsed = 0;
}	 

// . returns the slot number for "key"
// . returns -1 if key not in hash table
template<class Key_t, class Val_t> 
long HashTableT<Key_t, Val_t>::getOccupiedSlotNum ( Key_t& key ) const {
	if ( m_numSlots <= 0 ) return -1;
        long long n;
	/*
	switch(sizeof(Key_t)) {
	case 8:
		n = ((unsigned long long)key) % ((unsigned long)m_numSlots);
		break;		
	default:
		n = ((unsigned long)key) % ((unsigned long)m_numSlots);
		break;
	}
	*/
	if ( sizeof(Key_t) == 8 )
		n = ((unsigned long long)key) % ((unsigned long)m_numSlots);
	else
		n = ((unsigned long)key) % ((unsigned long)m_numSlots);

        long count = 0;
        while ( count++ < m_numSlots ) {
                if ( m_keys [ n ] == (Key_t)0   ) return -1;
		if ( m_keys [ n ] == key ) return  n;
		if ( ++n == m_numSlots ) n = 0;
        }
        log("hashtable: Could not get key. Table is full.");
        return -1;
}

template<class Key_t, class Val_t> 
long HashTableT<Key_t, Val_t>::getNextSlot ( Key_t& key , long n ) const {
	// inc and wrap if we need to
	if ( ++n >= m_numSlots ) n = 0;

 loop:
	if ( m_keys [ n ] == (Key_t)0   ) return -1;
	if ( m_keys [ n ] == key ) return  n;
	if ( ++n == m_numSlots ) n = 0;
	goto loop;
}

//return NULL if key not in hash table. We do not want a getValue
//function that returns 0 because then HashTableT does not work for
//non scalar templates
template<class Key_t, class Val_t> 
Val_t* HashTableT<Key_t, Val_t>::getValuePointer ( Key_t key ) const {
	// returns -1 if key not in hash table
	long n = getOccupiedSlotNum ( key );
	if ( n < 0 ) return NULL;
	return &m_vals[n];
	}

// . returns false and sets g_errno on error, returns true otherwise
// . adds scores if termId already exists in table
template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::addKey (Key_t key , Val_t value , long *slot) {
	// check to see if we should grow the table
	if ( 100 * (m_numSlotsUsed+1) >= m_numSlots * 75 ) {
		long growTo = ((long long) m_numSlots * 120LL ) / 100LL +128LL;
		if ( ! setTableSize ( growTo, NULL, 0 ) ) return false;
	}

        long long n;
	/*
	switch(sizeof(Key_t)) {
	case 8:
		n = ((unsigned long long)key) % ((unsigned long)m_numSlots);
		break;		
	default:
		n = ((unsigned long)key) % ((unsigned long)m_numSlots);
		break;
	}
	*/
	if ( sizeof(Key_t) == 8 )
		n = ((unsigned long long)key) % ((unsigned long)m_numSlots);
	else
		n = ((unsigned long)key) % ((unsigned long)m_numSlots);

        long count;
        for ( count = 0 ; count < m_numSlots ; count++ ) {
                if ( m_keys [ n ] == (Key_t)0   ) break;
		// if we allow dups, skip as if he is full...
		if ( m_keys [ n ] == key && ! m_allowDupKeys ) break;
		if ( ++n == m_numSlots ) n = 0;
        }
	// bail if not found
	if ( count >= m_numSlots ) {
		g_errno = ENOMEM;
		return log("hashtable: Could not add key. Table is full.");
	}
	if ( m_keys [ n ] == (Key_t)0 ) {
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
template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::removeKey ( Key_t key ) {
	// returns -1 if key not in hash table
	long n = getOccupiedSlotNum(key);
	if ( n < 0 ) return true;
	m_keys[n] = 0;
	m_numSlotsUsed--;
	if ( ++n >= m_numSlots ) n = 0;
	// keep looping until we hit an empty slot
	Val_t val;
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

// same as removeKey() above
template<class Key_t, class Val_t> 
void HashTableT<Key_t, Val_t>::removeSlot ( long n ) {
	// returns -1 if key not in hash table
	//long n = getOccupiedSlotNum(key);
	if ( n < 0 ) return;
	// save it
	Key_t key = m_keys[n];
	// sanity check, must be occupied
	if ( key == 0 ) { char *xx = NULL; *xx = 0; }
	// delete it
	m_keys[n] = 0;
	m_numSlotsUsed--;
	if ( ++n >= m_numSlots ) n = 0;
	// keep looping until we hit an empty slot
	Val_t val;
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
template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::setTableSize ( long n, char *buf, long bufSize ) {
	// don't change size if we do not need to
	if ( n == m_numSlots ) return true;

	//log("hey");
	//sleep(3);

	// set the bufSize
	Key_t *newKeys = (Key_t *)NULL;
	Val_t *newVals = (Val_t *)NULL;
	long need = n * ((long)sizeof(Key_t) + (long)sizeof(Val_t));

	// set the buffer and buffer size
	m_buf = buf;
	m_bufSize = bufSize; 

	// sanity check 
	//if( m_buf && m_bufSize < need){ char *xx = NULL; *xx = 0; }

	// 
	//char *buf = m_buf1;
	
	//if ( (char *)m_keys == m_buf1 ) buf = m_buf2;
	
	//if ( need <= HT_BUF_SIZE ) {
	//we're going to overwrite this before we have a chance to free, so...
	bool freeThisTime = m_doFree;

	if( need <= m_bufSize && m_buf){
		newKeys = (Key_t *)m_buf;
		newVals = (Val_t *)(m_buf + (n*(long)sizeof(Key_t)));
		memset ( newKeys , 0 , sizeof(Key_t) * n );
		m_doFree = false;
	}
	else {
		if ( ! newKeys )
			newKeys = (Key_t *)mcalloc ( n * sizeof(Key_t) , 
					     "HashTableTk");
		if ( ! newKeys ) return false;

		if ( ! newVals ) 
			newVals = (Val_t *)mmalloc ( n * sizeof(Val_t) , 
					     "HashTableTv");
		if ( ! newVals ) {
			if ( newKeys != (Key_t *)buf )
				mfree ( newKeys , n * sizeof(Key_t) , 
					"HashTableTk" );
			return false;
		}
		m_doFree = true;
	}

	// rehash the slots if we had some
	if ( m_keys ) {
		for ( long i = 0 ; i < m_numSlots ; i++ ) {
			// skip the empty slots 
			if ( m_keys [ i ] == 0 ) continue;
			// get the new slot # for this slot (might be the same)
			long long num;
			/*
			switch(sizeof(Key_t)) {
			case 8:
				num=((unsigned long long)m_keys[i])%((unsigned long)n);
				break;		
			default:
				num=((unsigned long)m_keys[i])%((unsigned long)n);
				break;
			}
			*/
			if ( sizeof(Key_t) == 8 )
				num=((unsigned long long)m_keys[i])%
					((unsigned long)n);
			else
				num=((unsigned long)m_keys[i])%
					((unsigned long)n);

			while ( newKeys [ num ] ) if ( ++num >= n ) num = 0;
			// move the slotPtr/key/size to this new slot
			newKeys [ num ] = m_keys [ i ];
			newVals [ num ] = m_vals [ i ];
		}
	}
	// free the old guys
	//if ( m_keys && m_keys != (Key_t *)m_buf1 && m_keys != (Key_t *)m_buf2){
	if ( m_keys && freeThisTime) {
		mfree ( m_keys , m_numSlots * sizeof(Key_t) , 
			"HashTableTk" );
		mfree ( m_vals , m_numSlots * sizeof(Val_t) , 
			"HashTableTv" );
	}
	// assign the new slots, m_numSlotsUsed should be the same
	m_keys = newKeys;
	m_vals = newVals;
	m_numSlots = n;
	return true;
}

template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::serialize(SafeBuf& sb) {
	sb += m_numSlots;
	sb += m_numSlotsUsed;
	if(m_numSlots == 0) return true;
	bool x = true;
	x &= sb.safeMemcpy((char*)m_keys, sizeof(Key_t) * m_numSlots);
	x &= sb.safeMemcpy((char*)m_vals, sizeof(Val_t) * m_numSlots);
	return x;
}

template<class Key_t, class Val_t> 
long HashTableT<Key_t, Val_t>::deserialize(char* s) {
	char *p = s;
	long numSlots = *(long*)p;
	p += sizeof(long);
	long numSlotsUsed = *(long*)p;
	p += sizeof(long);
	setTableSize(numSlots, m_buf, m_bufSize );
	if(m_numSlots != numSlots) {
		return -1;
	}
	if(m_numSlots == 0) {
		m_numSlotsUsed = numSlotsUsed;
		return p - s;
	}

	memcpy((char*)m_keys, p, sizeof(Key_t) * numSlots);
	p += sizeof(Key_t) * numSlots;
	memcpy((char*)m_vals, p, sizeof(Val_t) * numSlots);
	p += sizeof(Val_t) * numSlots;
	m_numSlotsUsed = numSlotsUsed;
	return p - s;
}


// both return false and set g_errno on error, true otherwise
template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::load ( char* filename , char **tbuf , long *tsize ) {
	reset();
	File f;
	f.set ( filename );
	if ( ! f.doesExist() ) return true;
	log(LOG_INFO,"admin: Loading hashtable from %s",filename);
	if ( ! f.open ( O_RDONLY) ) return false;
	long numSlots;
	long numSlotsUsed;
	long off = 0;
	if ( ! f.read ( &numSlots     , 4 , off ) ) return false;
	off += 4;
	if ( ! f.read ( &numSlotsUsed , 4 , off ) ) return false;
	off += 4;
	if ( ! setTableSize ( numSlots , NULL , 0 ) ) return false;
	long ks = sizeof(Key_t);
	long vs = sizeof(Val_t);
	// corruption check
	if ( f.getFileSize() < ks * numSlots + vs * numSlots - 8 ) return false;
	if ( ! f.read ( m_keys        , numSlots * ks , off ) ) return false;
	off += numSlots * ks;
	if ( ! f.read ( m_vals        , numSlots * vs , off ) ) return false;
	off += numSlots * vs;
	m_numSlotsUsed = numSlotsUsed;
	// done if no text buf
	if ( ! tbuf ) { f.close(); return true; }
	// read in the tbuf size, next 4 bytes
	if ( ! f.read (  tsize     , 4 , off ) ) return false;
	off += 4;
	// alloc mem for reading in the contents of the text buf
	*tbuf = (char *)mmalloc ( *tsize , "HTtxtbuf" );
	if ( ! *tbuf ) return false;
	// read in the contents of the text buf
	if ( ! f.read ( *tbuf     , *tsize , off ) ) return false;
	off += *tsize;
	// close the file, we are done
	f.close();
	return true;
}

template<class Key_t, class Val_t> 
bool HashTableT<Key_t, Val_t>::save ( char* filename , char *tbuf , long tsize ) {
	File f;
	f.set ( filename );
	log(LOG_INFO,"admin: Saving hashtable from %s",filename);
	if ( ! f.open ( O_RDWR | O_CREAT ) ) return false;
	long numSlots     = m_numSlots;
	long numSlotsUsed = m_numSlotsUsed;
	long off = 0;
	if ( ! f.write ( &numSlots     , 4 , off ) ) return false;
	off += 4;
	if ( ! f.write ( &numSlotsUsed , 4 , off ) ) return false;
	off += 4;
	long ks = sizeof(Key_t);
	long vs = sizeof(Val_t);
	if ( ! f.write ( m_keys        , numSlots * ks , off ) ) return false;
	off += numSlots * ks;
	if ( ! f.write ( m_vals        , numSlots * vs , off ) ) return false;
	off += numSlots * vs;
	if ( ! tbuf ) { f.close(); return true; }
	// save the text buf size
	if ( ! f.write ( &tsize        , 4 , off ) ) return false;
	off += 4;
	// save the text buf content
	if ( ! f.write ( tbuf          , tsize , off ) ) return false;
	off += tsize;
	f.close();
	return true;
}

// hash the space (or +) separated list of numbers in this string
//template<class Key_t, class Val_t> 
//bool HashTableT<Key_t,Val_t>::hashFromString ( HashTableT *ht , char *x ) {
bool hashFromString ( HashTableT<long long,char> *ht , char *x ) {
	if ( ! x ) return true;
	char *xend = x + gbstrlen(x);
	long  n    = 1;
	for ( char *s = x ; s < xend ; s++ ) 
		// i am assuming this is ascii here!
		if (is_wspace_a(*s)||*s == '+') n++;
	// double # slots to nd*2 so that hashtable is somewhat sparse --> fast
	if ( ! ht->set ( n * 2 , NULL , 0 , false ) ) return false;
	// now populate with the docids
	for ( char *s = x ; s < xend ; ) {
		// skip the plusses
		while ( s < xend && (is_wspace_a(*s) || *s == '+') ) s++;
		// are we done?
		if ( s >= xend ) break;
		// get the docid, a long long (64 bits)
		long long d = atoll ( s );
		// add it, should never fail!
		if ( ! ht->addKey ( d , 1 ) ) return false;
		// skip till +
		while ( s < xend && (*s != '+' && !is_wspace_a(*s)) ) s++;
		// are we done?
		if ( s >= xend ) break;
	}
	return true;
}

template class HashTableT<long, char>;
template class HashTableT<long, long>;
template class HashTableT<long long , long long>;
template class HashTableT<long , long long>;
template class HashTableT<long long , long>;
template class HashTableT<long long, uint32_t>;
template class HashTableT<unsigned long long , unsigned long>;
template class HashTableT<unsigned long long , unsigned long long>;
template class HashTableT<unsigned long long , char*>;
template class HashTableT<unsigned long, unsigned long>;
template class HashTableT<unsigned long, bool>;
template class HashTableT<long long , bool>;
template class HashTableT<unsigned long long, float>;
template class HashTableT<unsigned long long, char>;
template class HashTableT<unsigned long, char*>;
template class HashTableT<unsigned long, FnInfo>;
template class HashTableT<unsigned long, FnInfo*>;
template class HashTableT<unsigned long, QuickPollInfo*>;
template class HashTableT<unsigned long, HashTableT<unsigned long long, float>* >;
template class HashTableT<long long, char>;
template class HashTableT<unsigned long, long long>;
template class HashTableT<unsigned long, long>;
template class HashTableT<unsigned long long, 
			  HashTableT<unsigned long long, float> *>;
template class HashTableT<long long, CallbackEntry>;	// Dns.cpp
template class HashTableT<uint32_t, TLDIPEntry>;	// Dns.cpp
template class HashTableT<long, short>;
template class HashTableT<uint32_t, uint32_t>;
template class HashTableT<uint32_t, uint64_t>;
class FrameTrace;
template class HashTableT<uint32_t, FrameTrace *>;
//template class HashTableT<long long, Title::InLinkInfo>;
template class HashTableT<uint64_t, long>;
//template class HashTableT<unsigned long long, SynonymLinkGroup>;
template class HashTableT<unsigned long long, long long>;
template class HashTableT<long, ComTopInDmozRec>;
template class HashTableT<unsigned short, const char *>;
template class HashTableT<unsigned short, int>;
template class HashTableT<long,uint64_t>;
//template class HashTableT<ull_t, TimeZoneInfo>;
//template class HashTableT<long, DivSectInfo>;
//template class HashTableT<long, DivLevelInfo>;
template class HashTableT<uint32_t, char>;
template class HashTableT<uint64_t, bool>;
template class HashTableT<uint32_t, long>;
//template class HashTableT<long, TurkUserState>;
template class HashTableT<long, float>;
//template class HashTableT<unsigned long long,SiteRec>;
//#include "Spider.h"
//template class HashTableT<uint64_t,DomSlot>;
//template class HashTableT<long,IpSlot>;
//template class HashTableT<uint32_t,float>;
#include "AutoBan.h"
template class HashTableT<long,CodeVal>;
