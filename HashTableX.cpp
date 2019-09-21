#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "HashTableX.h"
#include "SafeBuf.h"
#include "Threads.h"
#include "Mem.h"     // for mcalloc and mmalloc


void HashTableX::constructor() {
	m_buf   = NULL;
	m_allocName = NULL;
	m_doFree = false;
	m_isWritable = true;
	m_txtBuf = NULL;
	m_useKeyMagic = false;
	m_ks = 0;
	m_allowGrowth = true;
	m_numSlots = 0;
	m_numSlotsUsed = 0;
}

void HashTableX::destructor() {
	reset();
}

HashTableX::HashTableX () {
	m_buf   = NULL;
	m_allocName = NULL;
	m_doFree = false;
	m_isWritable = true;
	m_txtBuf = NULL;
	m_useKeyMagic = false;
	m_ks = 0;
	
	reset();
}

HashTableX::~HashTableX ( ) { 
	reset ( ); 
}

// returns false and sets errno on error
bool HashTableX::set ( int32_t  ks              ,
		       int32_t  ds              ,
		       int32_t  initialNumTerms , 
		       char *buf             , 
		       int32_t  bufSize         ,
		       bool  allowDups       ,
		       int32_t  niceness        ,
		       char *allocName       ,
		       // in general you want keymagic to ensure your
		       // keys are "random" for good hashing. it doesn't
		       // really slow things down either.
		       bool  useKeyMagic     ) {
	reset();
	m_ks = ks;
	m_ds = ds;
	m_allowDups = allowDups;
	m_niceness  = niceness;
	m_needsSave = true;
	m_isSaving  = false;
	m_maxSlots  = 0x7fffffffffffffffLL;
	// fi it so when you first call addKey() it does not grow your table!
	//if ( initialNumTerms < 32 ) initialNumTerms = 32;
	// sanity check. assume min keysize of 4 because we do *(int32_t *)key
	// logic below!!
	if ( ks <  4 ) { char *xx=NULL;*xx=0; }
	if ( ds <  0 ) { char *xx=NULL;*xx=0; }
	// auto?
	if ( initialNumTerms == -1 ) {
		int32_t slotSize = ks + ds + 1;
		initialNumTerms = bufSize / slotSize;
		initialNumTerms /= 2; // fix it to not exceed bufSize
	}
	// set this
	m_allocName = allocName;

	m_useKeyMagic = useKeyMagic;

	return setTableSize ( initialNumTerms , buf , bufSize );
}

// . call clean() to do a more careful reset
// . clean will rehash
void HashTableX::reset ( ) {
	if ( m_doFree && m_buf )
		mfree(m_buf ,m_bufSize,m_allocName);
	m_buf   = NULL;
	m_keys  = NULL;
	m_vals  = NULL;
	m_flags = NULL;
	m_numSlots     = 0;
	m_numSlotsUsed = 0;
	m_addIffNotUnique = false;
	m_maskKeyOffset = 0;
	m_allowGrowth = true;
	//m_useKeyMagic = false;
	// we should free it in reset()
	if ( m_doFree && m_txtBuf ) {
		mfree ( m_txtBuf , m_txtBufSize,"ftxtbuf");
		m_txtBuf = NULL;
	}
}

void HashTableX::clear ( ) {
	// vacate all slots
	//if ( m_keys ) memset ( m_keys , 0 , m_ks * m_numSlots );
	memset ( m_flags , 0 , m_numSlots );
	m_numSlotsUsed = 0;
}	 

// #n is the slot
int32_t HashTableX::getNextSlot ( int32_t n , void *key ) {
	if ( n < 0 ) return -1;
 loop:
	// inc it
	if ( ++n == m_numSlots ) n = 0;
	// this is set to 0x01 if non-empty
	if ( m_flags [ n ] == 0   ) return -1;
	// if key matches return it
	if ( *(int32_t *)(m_keys + m_ks * n) == *(int32_t *)key  &&
	     ( memcmp (m_keys + m_ks * n, key, m_ks ) == 0 ) )
		return n;
	// loop up
	goto loop;
}

// how many slots have this key
int32_t HashTableX::getCount ( void *key ) {
	int32_t n = getSlot ( key );
	if ( n < 0 ) return 0;
	int32_t count = 1;
	if ( ! m_allowDups ) return count;
 loop:
	// inc it
	if ( ++n == m_numSlots ) n = 0;
	// this is set to 0x01 if non-empty
	if ( m_flags [ n ] == 0   ) return count;
	// count it if key matches
	if ( *(int32_t *)(m_keys + m_ks * n) == *(int32_t *)key  &&
	     ( memcmp (m_keys + m_ks * n, key, m_ks ) == 0 ) )
		count++;
	// loop up
	goto loop;
}

// . returns the slot number for "key"
// . returns -1 if key not in hash table
int32_t HashTableX::getOccupiedSlotNum ( void *key ) {
	if ( m_numSlots <= 0 ) return -1;

        int32_t n = *(uint32_t *)(((char *)key)+m_maskKeyOffset);

	// use magic to "randomize" key a little
	if ( m_useKeyMagic ) 
		n^=g_hashtab[(unsigned char)((char *)key)[m_maskKeyOffset]][0];

	// mask on the lower 32 bits i guess
        n &= m_mask;

        int32_t count = 0;
        while ( count++ < m_numSlots ) {
		// this is set to 0x01 if non-empty
		if ( m_flags [ n ] == 0   ) return -1;
		// get the key there
		if ( *(int32_t *)(m_keys + m_ks * n) == *(int32_t *)key  &&
		     ( memcmp (m_keys + m_ks * n, key, m_ks ) == 0 ) )
			return n;
		// advance otherwise
		if ( ++n == m_numSlots ) n = 0;
        }
        log("htable: Could not get key. Table is full.");
        return -1;
}

// for value-less hashtables
bool HashTableX::addKey ( void *key ) {
	// sanity check -- need to supply data?
	if ( m_ds != 0 ) { char *xx=NULL;*xx=0; }
	return addKey ( key , NULL , NULL );
}

// . returns false and sets g_errno on error, returns true otherwise
// . adds scores if termId already exists in table
bool HashTableX::addKey ( void *key , void *val , int32_t *slot ) {
	// if saving, try again later
	if ( m_isSaving || ! m_isWritable ) { 
		g_errno = ETRYAGAIN; 
		return false;
	}
	// never got initialized? call HashTableX::init()
	if ( m_ks <= 0 ){ char *xx=NULL; *xx=0; }

	if ( ! m_allowGrowth && m_numSlotsUsed + 1 > m_numSlots ) {
		log("hashtable: hit max ceiling of hashtable of %"INT32" slots. "
		    "and can not grow because in thread.",m_numSlotsUsed);
		return false;
	}

	// check to see if we should grow the table. now we grow
	// when 25% full to make operations faster so getLongestString()
	// doesn't return such big numbers!
	if ( (m_numSlots < 20 || 2 * m_numSlotsUsed >= m_numSlots) &&
	     m_numSlots < m_maxSlots ) {
		int64_t growTo = ((int64_t)m_numSlots * 150LL )/100LL+20LL;
		if ( growTo > m_maxSlots ) growTo = m_maxSlots;
		if ( ! setTableSize ( (int32_t)growTo , NULL , 0 ) ) return false;
	}

        //int32_t n=(*(uint32_t *)(((char *)key)+m_maskKeyOffset)) & m_mask;

        int32_t n = *(uint32_t *)(((char *)key)+m_maskKeyOffset);

	// use magic to "randomize" key a little
	if ( m_useKeyMagic ) 
		n^=g_hashtab[(unsigned char)((char *)key)[m_maskKeyOffset]][0];

	// mask on the lower 32 bits i guess
        n &= m_mask;

        int32_t count = 0;
	m_needsSave = true;
        while ( count++ < m_numSlots ) {
		// this is set to 0x00 if empty
		if ( m_flags [ n ] == 0   ) break;
		// breathe
		//QUICKPOLL(m_niceness);
		// use "n" if key matches
		if ( *(int32_t *)(m_keys + m_ks * n) == *(int32_t *)key  &&
		     // if we are a 4 byte key no need to do the memcmp
		     (m_ks==4||memcmp (m_keys + m_ks * n, key, m_ks )==0) ) {
			// if allow dups is true it must also match the data
			if ( ! m_allowDups ) break;
			// . this behaviour is expected by Events.cpp calling
			//   g_places.addKey(h,&pd)
			// . TODO: think about adding m_allowRepeatedData
			//   and only doing this memcmp() if that is false
			// . NO! computeSimilarity adds the same termid with
			//   same score quite often
			//if ( memcmp(m_vals+n*m_ds,val,m_ds) == 0 ) 
			//	break;
			// otherwise, yes, keys match, but values do not
			// and we allow dups, so insert it somewhere else
		}			
		// advance otherwise
		if ( ++n == m_numSlots ) n = 0;
	}		     
	// bail if not found
	if ( count >= m_numSlots ) {
		g_errno = ENOMEM;
		return log("htable: Could not add key. Table is full.");
	}
	if ( m_flags [ n ] == 0 ) {
		// inc count if we're the first
		m_numSlotsUsed++;
		// and store the key
		if ( m_ks == 4 ) ((int32_t *)m_keys)[n] = *(int32_t *)key;
		else if ( m_ks == 8 ) ((int64_t *)m_keys)[n] = *(int64_t *)key;
		else             gbmemcpy ( m_keys + m_ks * n , key , m_ks );
	}
	// insert the value for this key
	if ( val ) setValue ( n , val );
	// caller sometimes wants this
	if ( slot ) *slot = n;
	// no longer empty
	m_flags[n] = 0x01;
	return true;
}

// patch the hole so chaining still works
bool HashTableX::removeKey ( void *key ) {
	// returns -1 if key not in hash table
	int32_t n = getOccupiedSlotNum(key);
	if ( n >= 0 ) return removeSlot ( n );
	return true;
}

// . patch the hole so chaining still works
// . returns false and sets g_errno on error
bool HashTableX::removeSlot ( int32_t n ) {
	if ( n < 0 ) return true;
	// skip if already empty
	if ( m_flags [ n ] == 0 ) return true;
	// if saving, try again later
	if ( m_isSaving || ! m_isWritable ) { 
		g_errno = ETRYAGAIN; 
		return false;
	}
	// clear it out
	m_flags [ n ] = 0;
	// dec the count
	m_numSlotsUsed--;
	// advance n
	if ( ++n >= m_numSlots ) n = 0;
	// keep looping until we hit an empty slot
	while ( m_flags [ n ] ) {
		void *kp = m_keys + m_ks * n;
		void *vp = m_vals + m_ds * n;
		// clear it out
		m_flags [ n ] = 0;
		// dec the count
		m_numSlotsUsed--;
		// add it back
		addKey ( kp , vp );
		// chain to next bucket while it is occupied
		if ( ++n >= m_numSlots ) n = 0;		
	}
	m_needsSave = true;
	return true;
}


// . set table size to "n" slots
// . rehashes the termId/score pairs into new table
// . returns false and sets errno on error
bool HashTableX::setTableSize ( int32_t oldn , char *buf , int32_t bufSize ) {
	// don't change size if we do not need to
	if ( oldn == m_numSlots ) return true;

	int64_t n = (int64_t)oldn;
	// make it a power of 2 for speed if small
	n = getHighestLitBitValueLL((uint64_t)oldn * 2LL -1);
	// sanity check, must be less than 1B
	if ( n > 1000000000 ) { char *xx=NULL;*xx=0; }
	// limit...
	//if ( n > m_maxSlots ) n = m_maxSlots;
	// do not go negative on me
	if ( oldn == 0 ) n = 0;
	// sanity check
	if ( n < oldn ) { char *xx = NULL; *xx = 0; }
	// do we have a buf?
	int32_t need = (m_ks+m_ds+1) * n;
	// sanity check, buf should also meet what we need
	if ( buf && bufSize < need ) { char *xx = NULL; *xx = 0; }

	// we grow kinda slow, it slows things down, so note it
	int64_t startTime =0LL;
	int32_t old = -1;
	if ( m_numSlots > 2000 ) {
		startTime = gettimeofdayInMilliseconds();
		old = m_numSlots;
	}

	// if we should not free note that
	bool  savedDoFree  = m_doFree ;
	char *savedBuf     = m_buf;
	int32_t  savedBufSize = m_bufSize;

	// use what they gave us if we can
	m_buf    = buf;
	m_doFree = false;
	// alloc if we should
	if ( ! m_buf ) {
		m_buf     = (char *)mmalloc ( need , m_allocName);
		m_bufSize = need;
		m_doFree  = true;
		if ( ! m_buf ) return false;
		QUICKPOLL(m_niceness);
	}

	// save the old junk
	char *oldFlags = m_flags;
	char *oldKeys  = m_keys;
	char *oldVals  = m_vals;

	// now point to the new bigger and empty table
	m_keys  = m_buf;
	m_vals  = m_buf + m_ks * n;
	m_flags = m_buf + m_ks * n + m_ds * n;

	// clear flags only
	//bzero ( m_flags , n );
	memset ( m_flags , 0 , n );

	// rehash the slots if we had some
	int32_t ns = m_numSlots; if ( ! m_keys ) ns = 0;

	// update these for the new empty table
	m_numSlots = n;
	m_mask     = n - 1;

	int32_t oldUsed = m_numSlotsUsed;
	// reset this before re-adding all of them
	m_numSlotsUsed = 0;

	// loop over results in old table, if any
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip the empty slots 
		if ( oldFlags [ i ] == 0 ) continue;
		// add old key/val into the empty table
		if ( m_ks == sizeof(key144_t) )
			// use this special adder that hashes it up better!
			addTerm144 ( (key144_t *)(oldKeys + m_ks * i) ,
				     *(int32_t *)(oldVals + m_ds * i) );
		else
			addKey ( oldKeys + m_ks * i , oldVals + m_ds * i );
	}

	if ( startTime ) {
		char *name ="";
		if ( m_allocName ) name = m_allocName;
		//if ( name && strcmp(name,"HashTableX")==0 )
		//	log("hey");
		int64_t now = gettimeofdayInMilliseconds();
		logf(LOG_DEBUG,"table: grewtable %s from %"INT32" to %"INT32" slots "
		     "in %"INT64" ms (this=0x%"PTRFMT") (used=%"INT32")",  
		     name,old,m_numSlots ,now - startTime,(PTRTYPE)this,oldUsed);
	}

	// free the old guys
	if ( ! savedDoFree ) return true;
	if ( ! savedBuf    ) return true;

	// let the old table go
	mfree ( savedBuf , savedBufSize , m_allocName );

	return true;
}

bool HashTableX::load ( char *dir , char *filename ,  SafeBuf *fillBuf ) {
	char *tbuf = NULL;
	int32_t  tsize = 0;
	bool status = load ( dir , filename , &tbuf, &tsize );
	if ( ! status ) return false;
	// assign to safebuf. own buf = true
	fillBuf->setBuf ( tbuf , tsize , tsize , true , csUTF8 );
	return true;
}

// both return false and set g_errno on error, true otherwise
bool HashTableX::load ( char *dir, char *filename, char **tbuf, int32_t *tsize ) {
	File f;
	f.set ( dir , filename );
	if ( ! f.doesExist() ) return false;
	char *pdir = dir;
	if ( ! pdir ) pdir = "";
	//log(LOG_INFO,"admin: Loading hashtablex from %s%s",pdir,filename);
	if ( ! f.open ( O_RDONLY) ) return false;
	int32_t numSlots;
	int32_t numSlotsUsed;
	int32_t off = 0;
	if ( ! f.read ( &numSlots     , 4 , off ) ) return false;
	off += 4;
	if ( ! f.read ( &numSlotsUsed , 4 , off ) ) return false;
	off += 4;
	int32_t ks;
	if ( ! f.read ( &ks         , 4 , off ) ) return false;
	off += 4;
	int32_t ds;
	if ( ! f.read ( &ds         , 4 , off ) ) return false;
	off += 4;

	if ( numSlots < 0 || numSlotsUsed < 0 ) {
		log("htable: bogus saved hashtable file %s%s.",dir,filename);
		return false;
	}

	// bogus key size?
	if ( ks <= 0 ) {
		// is very common for this file so skip it
		if ( strstr(filename,"ipstouseproxiesfor.dat") )
			return false;
		log("htable: reading hashtable from %s%s: "
		    "bogus keysize of %"INT32"",
		    dir,filename,ks );
		return false;
	}

	// just in case m_ks was already set, call reset() down here...
	// no, it resets our "magic" stuff!
	if ( m_ks != ks || m_ds != ds ) reset();

	m_ks = ks;
	m_ds = ds;

	if ( ! setTableSize ( numSlots , NULL , 0 ) ) return false;
	if ( ! f.read ( m_keys        , numSlots * m_ks , off ) ) return false;
	off += numSlots * m_ks;
	if ( m_ds && ! f.read ( m_vals        , numSlots * m_ds , off ) ) 
		return false;
	off += numSlots * m_ds;
	// whether the slot is empty or not
	if ( ! f.read ( m_flags       , numSlots        , off ) ) return false;
	off += numSlots ;
	
	m_numSlotsUsed = numSlotsUsed;
	// done if no text buf
        if ( ! tbuf ) { f.close(); return true; }
        // read in the tbuf size, next 4 bytes
        if ( ! f.read (  tsize     , 4 , off ) ) return false;
        off += 4;
	// make a name for it
	char ttt[64];
	sprintf(ttt,"%s-httxt",m_allocName);
        // alloc mem for reading in the contents of the text buf
        *tbuf = (char *)mmalloc ( *tsize , ttt );//"HTtxtbufx" );
        if ( ! *tbuf ) return false;
        // read in the contents of the text buf
        if ( ! f.read ( *tbuf     , *tsize , off ) ) return false;
        off += *tsize;
	// we should free it in reset()
	m_txtBuf = *tbuf;
	m_txtBufSize = *tsize;
        // close the file, we are done
        f.close();
	m_needsSave = false;
	int32_t totalMem = *tsize+m_numSlots*(m_ks+m_ds);
	log(LOG_INFO,"admin: Loaded hashtablex from %s%s %"INT32" total mem",
	    pdir,filename, totalMem);
        return true;
}

//
// . new code for saving hashtablex in a thread
// . so Process.cpp's call to g_spiderCache.save() can save the doleiptable
//   without blocking...
//
static void *saveWrapper ( void *state , class ThreadEntry *t ) {
	// get this class
	HashTableX *THIS = (HashTableX *)state;
	// this returns false and sets g_errno on error
	THIS->save( THIS->m_dir , 
		    THIS->m_filename , 
		    THIS->m_tbuf ,
		    THIS->m_tsize );
	// now exit the thread, bogus return
	return NULL;
}

// we come here after thread exits
static void threadDoneWrapper ( void *state , class ThreadEntry *t ) {
	// get this class
	HashTableX *THIS = (HashTableX *)state;
	// store save error into g_errno
	//g_errno = THIS->m_saveErrno;
	// log it
	log("db: done saving %s/%s",THIS->m_dir,THIS->m_filename);
	// . resume adding to the hashtable
	// . this will also allow other threads to be queued
	// . if we did this at the end of the thread we could end up with
	//   an overflow of queued SAVETHREADs
	THIS->m_isSaving = false;
	// we do not need to be saved now?
	THIS->m_needsSave = false;
	// g_errno should be preserved from the thread so if threadSave()
	// had an error it will be set
	if ( g_errno )
		log("db: Had error saving hashtable to disk for %s: %s.",
		    THIS->m_allocName,mstrerror(g_errno));
	// . call callback
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
}


bool HashTableX::fastSave ( bool useThread ,
			    char *dir , 
			    char *filename , 
			    char *tbuf, 
			    int32_t tsize ,
			    void *state ,
			    void (* callback)(void *state) ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// we do not need a save
	if ( ! m_needsSave ) return true;
	// return true if already in the middle of saving
	if ( m_isSaving ) return false;

	// mark it early to avoid reentries
	m_isSaving = true;

	logf(LOG_INFO,"db: Saving %s/%s",dir,filename);

	// save parms
	strcpy ( m_dir , dir );
	strcpy ( m_filename , filename );
	m_tbuf     = tbuf;
	m_tsize    = tsize;
	m_state    = state;
	m_callback = callback;
	// assume no error
	//m_saveErrno = 0;
	// no adding to the hashtable now
	//m_isSaving = true;
	//useThread = false;
	// skip thread call if we should
	if ( ! useThread ) goto skip;
	// make this a thread now
	if ( g_threads.call ( SAVETREE_THREAD   , // threadType
			      1                 , // niceness
			      this              , // top 4 bytes must be cback
			      threadDoneWrapper ,
			      saveWrapper   ) ) return false;
	// if it failed
	if ( ! g_threads.m_disabled ) 
		log("db: Thread creation failed. Blocking while saving tree. "
		    "Hurts performance.");
 skip:
	// this returns false and sets g_errno on error
	save ( dir, filename , tbuf , tsize );
	// store save error into g_errno
	//g_errno = m_saveErrno;
	// resume adding to the tree
	// we do not need to be saved now?
	m_needsSave = false;
	m_isSaving = false;
	// we did not block
	return true;
}

bool HashTableX::save ( char *dir , 
			char *filename , 
			char *tbuf, 
			int32_t tsize ) {

	//if ( ! m_needsSave ) return true;
	//if ( m_isSaving ) return true;

	char s[1024];
	sprintf ( s , "%s/%s", dir , filename );
	int fd = ::open ( s , 
			  O_RDWR | O_CREAT | O_TRUNC ,
			  getFileCreationFlags() );
			  // S_IRUSR | S_IWUSR | 
			  // S_IRGRP | S_IWGRP | S_IROTH);
	if ( fd < 0 ) {
		//m_saveErrno = errno;
		return log("db: Could not open %s for writing: %s.",
			   s,mstrerror(errno));
	}
	// clear our own errno
	errno = 0;

	//log(LOG_INFO,"db: Saving hashtablex to %s",s);

	int32_t numSlots     = m_numSlots;
	int32_t numSlotsUsed = m_numSlotsUsed;
	int32_t off = 0;
	int32_t err;

	err = pwrite ( fd,  &numSlots     , 4 , off ) ; off += 4;
	if ( err == -1 ) return log("htblx: write error");

	err = pwrite ( fd,  &numSlotsUsed , 4 , off ) ; off += 4;
	if ( err == -1 ) return log("htblx: write error");
	
	err = pwrite ( fd,  &m_ks         , 4 , off ) ; off += 4;
	if ( err == -1 ) return log("htblx: write error");

	err = pwrite ( fd,  &m_ds         , 4 , off ) ; off += 4;
	if ( err == -1 ) return log("htblx: write error");

	err = pwrite ( fd,  m_keys , numSlots * m_ks , off ); 
	off += numSlots * m_ks;
	if ( err == -1 ) return log("htblx: write error");


	if ( m_ds ) {
		err = pwrite (fd,m_vals,numSlots*m_ds,off); 
		off += numSlots * m_ds;
		if ( err == -1 ) return log("htblx: write error");
	}

	// whether the slot is empty or not!
	err = pwrite ( fd,  m_flags , numSlots , off ); off += numSlots ;
	if ( err == -1 ) return log("htblx: write error");

        if ( tbuf ) {
		// save the text buf size
		err = pwrite ( fd,  &tsize        , 4 , off ) ; off += 4;
		if ( err == -1 ) return log("htblx: write error");
		// save the text buf content
		err = pwrite ( fd,  tbuf          , tsize , off ) ; off+=tsize;
		if ( err == -1 ) return log("htblx: write error");
	}
	close ( fd );

	//m_isSaving = false;
	//m_needsSave = false;
	return true;
}

// how many bytes are required to serialize this hash table?
int32_t HashTableX::getStoredSize() {
	// see serialize() function below to explain this
	return 4 + 4 + 1 + 2 + 1 + m_numSlotsUsed*(m_ks+m_ds);
}

// . returns # bytes written into "buf"
// . returns ptr to buf used
// . set size of buf allocated and used
// . returns -1 on error
char *HashTableX::serialize ( int32_t *bufSize ) {
	int32_t need = getStoredSize();
	char *buf = (char *)mmalloc ( need , m_allocName );
	if ( ! buf ) return (char *)-1;
	int32_t used = serialize ( buf , need );
	// ensure it matches
	if ( used != need ) { char *xx=NULL;*xx=0; }
	// store it
	*bufSize = used;
	return buf;
}

// int16_tcut
int32_t HashTableX::serialize ( SafeBuf *sb ) {
	int32_t nb = serialize ( sb->getBuf() , sb->getAvail() );
	// update sb
	sb->incrementLength ( nb );
	return nb;
}

// returns # bytes written into "buf"
int32_t HashTableX::serialize ( char *buf , int32_t bufSize ) {
	// int16_tcuts
	char *p    = buf;
	//char *pend = buf + bufSize;
	// how much for table?
	int32_t need = m_numSlotsUsed * (m_ks+m_ds);
	// and # of slots
	need += 4;
	// and # of slots used
	need += 4;
	// and key size
	need += 1;
	// and data size
	need += 2;
	// flags (allowDups)
	need += 1;
	// sanity check
	if ( need > bufSize ) { char *xx=NULL;*xx=0; }
	// sanity check -- i guess placedb hashtable in XmlDoc.cpp uses 512!
	if ( m_ks > 127 || m_ds > 512 ) { char *xx=NULL;*xx=0; }

	// store # slots total
	*(int32_t *)p = m_numSlots; p += 4;	
	// store # slots
	*(int32_t *)p = m_numSlotsUsed; p += 4;
	// store key size
	*(char *)p = m_ks; p += 1;
	// store data size
	*(int16_t *)p = m_ds; p += 2;
	// flags
	*(char *)p = m_allowDups; p += 1;	
	// sanity check
	int32_t used = 0;
	// store keys that are valid
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		// skip if empty
		if ( m_flags[i] == 0 ) continue;
		// sanity check count
		used++;
		// store key
		gbmemcpy ( p , m_keys + i * m_ks , m_ks );
		// advance
		p += m_ks;
	}
	// sanity check
	if ( used != m_numSlotsUsed ) { char *xx=NULL; *xx=0; }
	// store data that is valid
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		// skip if empty
		if ( m_flags[i] == 0 ) continue;
		// store key
		gbmemcpy ( p , m_vals + i * m_ds , m_ds );
		// advance
		p += m_ds;
	}
	// return bytes stored
	return p - buf;
}

// inflate it. returns false with g_errno set on error
bool HashTableX::deserialize ( char *buf , int32_t bufSize , int32_t niceness ) {
	// clear it
	reset();

	// int16_tcuts
	char *p    = buf;
	//char *pend = buf + bufSize;

	// get stuff
	int32_t numSlots = *(int32_t *)p; p += 4;
	// how may slots to add?
	int32_t numSlotsUsed = *(int32_t *)p; p += 4;
	// key size
	int32_t ks = *(char *)p; p += 1;
	// data size
	int32_t ds = *(int16_t *)p; p += 2;
	// flags (allowDups)
	bool allowDups = *(char *)p; p += 1;

	// init it
	if ( ! set ( ks , ds , numSlots , NULL , 0 , allowDups , niceness,
		     "htxdeserial") )
		return false;

	// sanity check
	if ( m_numSlots != numSlots ) { char *xx=NULL;*xx=0; }
	
	// add keys etc. now
	char *kp = p;
	char *dpstart = p + ks * numSlotsUsed;
	char *dp      = dpstart;
	// loop over all keys
	for ( ; kp < dpstart ; kp += ks , dp += ds ) 
		// add this pair. should NEVER fail
		if ( ! addKey ( kp , dp ) ) { char *xx=NULL;*xx=0; }

	// sanity check
	if ( m_numSlotsUsed != numSlotsUsed ) { char *xx=NULL;*xx=0; }

	// sanity check
	if ( bufSize >= 0 && dp > buf + bufSize ) { char *xx=NULL;*xx=0; }

	// success
	return true;
}

// . see how optimal the hashtable is
// . return max number of consectuive filled slots/buckets
int32_t HashTableX::getLongestString () {
	int32_t count = 0;
	int32_t max = 0;
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		if ( ! m_flags[i] ) { count = 0; continue; }
		// inc it
		count++;
		if ( count > max ) max = count;
	}
	return max;
}
		
// . how many keys are dups
// . returns -1 on error
int32_t HashTableX::getNumDups() {
	if ( ! m_allowDups ) return 0;
	HashTableX tmp;
	if ( ! tmp.set ( m_ks, 0, m_numSlots, NULL , 0 , false , m_niceness,
			 "htxtmp") )
		return -1;
	// put into that table
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		// skip empty bucket
		if ( ! m_flags[i] ) continue;
		// get the key
		char *kp = (char *)getKeyFromSlot(i);
		// add to new table
		if ( ! tmp.addKey ( kp ) ) return -1;
	}
	// the unqieus
	int32_t uniques = tmp.m_numSlotsUsed;
	// the dups
	int32_t dups = m_numSlotsUsed - uniques;
	// that's it
	return dups;
}

// return 32-bit checksum of keys in table
int32_t HashTableX::getKeyChecksum32 () {
	int32_t checksum = 0;
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		// skip empty bucket
		if ( ! m_flags[i] ) continue;
		// get the key
		char *kp = (char *)getKeyFromSlot(i);
		// do it
		if ( m_ks == 18 ) {
			checksum ^= *(int32_t *)(kp);
			checksum ^= *(int32_t *)(kp+4);
			checksum ^= *(int32_t *)(kp+8);
			checksum ^= *(int32_t *)(kp+12);
			checksum ^= *(int16_t *)(kp+16);
			continue;
		}
		if ( m_ks == 28 ) {
			checksum ^= *(int32_t *)(kp);
			checksum ^= *(int32_t *)(kp+4);
			checksum ^= *(int32_t *)(kp+8);
			checksum ^= *(int32_t *)(kp+12);
			checksum ^= *(int32_t *)(kp+16);
			checksum ^= *(int32_t *)(kp+20);
			checksum ^= *(int32_t *)(kp+24);
			continue;
		}
		if ( m_ks == 16 ) {
			checksum ^= *(int32_t *)(kp);
			checksum ^= *(int32_t *)(kp+4);
			checksum ^= *(int32_t *)(kp+8);
			checksum ^= *(int32_t *)(kp+12);
			continue;
		}
		// unsupported key size
		char *xx=NULL;*xx=0;
	}
	return checksum;
}

// print as text into sb for debugging
void HashTableX::print ( class SafeBuf *sb ) {
	for ( int32_t i = 0 ; i < m_numSlots ; i++ ) {
		// skip empty bucket
		if ( ! m_flags[i] ) continue;
		// get the key
		char *kp = (char *)getKeyFromSlot(i);
		//char *dp = (char *)getValFromSlot(i);
		// show key in hex
		char *kstr = KEYSTR ( kp , m_ks );
		//char *dstr = KEYSTR ( kp , m_ds );
		// show key
		//sb->safePrintf("\n%s -> %s", kstr , dstr );
		sb->safePrintf("KEY %s\n", kstr );
	}
}
