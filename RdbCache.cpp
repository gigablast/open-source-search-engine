#include "gb-include.h"

#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#include <unistd.h>
#include "Threads.h"

#include "RdbCache.h"
#include "Collectiondb.h"
#include "Loop.h"
#include "Msg17.h"
//#include "Dns.h"   // g_dns
//#include "Msg36.h" // g_qtable
#include "Msg13.h"
//#include "Msg10.h"   // g_deadWaitCache
#include "Dns.h"
#include "BigFile.h"
#include "Spider.h"

bool g_cacheWritesEnabled = true;

RdbCache::RdbCache () {
	m_totalBufSize = 0;
	m_numBufs      = 0;
	m_ptrs         = NULL;
	m_maxMem       = 0;
	m_numPtrsMax   = 0;
	reset();
	m_needsSave    = false;
	m_corruptionDetected = false;
}

RdbCache::~RdbCache ( ) { reset (); }

#define BUFSIZE (128*1024*1024)
//#define BUFSIZE (100000)
//#define BUFSIZE (200000)

void RdbCache::reset ( ) {

	//if ( m_numBufs > 0 )
	//	log("db: resetting record cache");
	m_offset = 0;
	m_tail   = 0;
	for ( int32_t i = 0 ; i < m_numBufs ; i++ )
		// all bufs, but not necessarily last, are BUFSIZE bytes big
		mfree ( m_bufs[i] , m_bufSizes[i] , "RdbCache" );
	m_numBufs     = 0;
	m_totalBufSize= 0;

	if ( m_ptrs ) mfree ( m_ptrs , m_numPtrsMax*sizeof(char *),"RdbCache");
	m_ptrs        = NULL;
	m_numPtrsUsed = 0;
	// can't reset this, breaks the load!
	//m_numPtrsMax  = 0;

	m_memOccupied = 0;
	m_memAlloced  = 0;
	m_numHits     = 0;
	m_numMisses   = 0;

	//m_wrapped = false;
	m_adds    = 0;
	m_deletes = 0;

	// assume no need to call convertCache()
	m_convert = false;

	m_isSaving = false;
}

bool RdbCache::init ( int32_t  maxMem        ,
		      int32_t  fixedDataSize ,
		      bool  supportLists  ,
		      int32_t  maxRecs       ,
		      bool  useHalfKeys   ,
		      char *dbname        ,
		      bool  loadFromDisk  ,
		      char  cacheKeySize  ,
		      char  dataKeySize   ,
		      int32_t  numPtrsMax    ) {
	// reset all
	reset();
	// watch out 
	if ( maxMem   < 0 ) return log(LOG_LOGIC,"db: cache for %s had "
				       "negative maxMem." ,  dbname);
	if ( maxRecs  < 0 ) return log(LOG_LOGIC,"db: cache for %s had "
				       "negative maxRecs.",  dbname);
	// don't use more mem than this
	m_maxMem     = maxMem;

	m_maxColls = (1LL << (sizeof(collnum_t)*8));

	RdbCache *robots = Msg13::getHttpCacheRobots();
	RdbCache *others = Msg13::getHttpCacheOthers();

	// do not use some cache if we are the tmp cluster
	RdbCache *th = NULL;
	if ( g_hostdb.m_useTmpCluster ) th = this;
	//if ( th == &g_genericCache[SITEQUALITY_CACHEID] ) maxMem = 0;
	if ( th ==  g_dns.getCache      ()              ) maxMem = 0;
	if ( th ==  g_dns.getCacheLocal ()              ) maxMem = 0;
	if ( th ==  robots                              ) maxMem = 0;
	if ( th ==  others                              ) maxMem = 0;
	//if ( th == &g_forcedCache                       ) maxMem = 0;
	//if ( th == &g_alreadyAddedCache                 ) maxMem = 0;

	// this is the fixed dataSize of all records in a list, not the
	// fixed dataSize of a list itself. Note that.
	m_fixedDataSize = fixedDataSize;
	m_supportLists  = supportLists;
	m_useHalfKeys   = useHalfKeys;
	m_useDisk       = loadFromDisk;
	m_dbname        = dbname;
	m_dks           = dataKeySize;
	m_cks           = cacheKeySize;
	// if maxMem is zero just return true
	if ( m_maxMem <= 0 ) return true;

	// assume no need to call convertCache()
	m_convert = false;

	// . double what they had so hash table is somewhat sparse
	// . ::load() uses this so set it here
	m_numPtrsMax = maxRecs * 2;

	// it might have been provided though, too
	if ( numPtrsMax > 0 ) m_numPtrsMax = numPtrsMax;

	// try loading from disk before anything else
	if ( m_useDisk ) {
		if ( load ( m_dbname ) ) return true;
		//log("RdbCache::init: cache load failed");
		g_errno = 0;
	}

	// . make our hash table, zero it out
	// . don't allow it more than 50% full for performance
	m_threshold  = maxRecs; // (maxRecs * 50 ) / 100;
	if ( m_threshold == m_numPtrsMax ) m_threshold--;
	char ttt[128];
	sprintf(ttt,"cptrs-%s",m_dbname);
	m_ptrs = (char **) mcalloc (sizeof(char *)*m_numPtrsMax , ttt );
	if ( ! m_ptrs ) return log("RdbCache::init: %s", mstrerror(g_errno));
	// debug testing -- remove later
	//m_crcs=(int32_t *)mcalloc(4*m_numPtrsMax,"RdbCache");
	//if (!m_crcs)return log("RdbCache::init: %s", mstrerror(g_errno));
	// update OUR mem alloced
	m_memAlloced = m_numPtrsMax * sizeof(char *);
	// include this
	m_memOccupied = 0;	// m_memOccupied = m_memAlloced;

	sprintf(ttt,"cbuf-%s",m_dbname);
	// . make the 128MB buffers
	// . if we do more than 128MB per buf then pthread_create() will fail
	int32_t bufMem = m_maxMem - m_memAlloced;
	if( bufMem <= 0 ) {
		log("rdbcache: cache for %s does not have enough mem. fix "
		    "by increasing maxmem or number of recs, etc.",m_dbname);
		return false;
		char *xx=NULL;*xx=0;
	}
	if ( bufMem  && m_fixedDataSize > 0 &&
	     bufMem / m_fixedDataSize < maxRecs / 2 ) {
		log("cache: warning. "
		    "cache for %s can have %i ptrs but buf mem "
		    "can only hold %i objects"
		    ,m_dbname
		    ,(int)maxRecs
		    ,(int)(bufMem/m_fixedDataSize));
	}
	m_totalBufSize = 0LL;
	m_offset       = 0LL;
	while ( bufMem > 0 && m_numBufs < 32 ) {
		int32_t size = bufMem;
		if ( size > BUFSIZE ) size = BUFSIZE;
		m_bufSizes [ m_numBufs ] = size;
		m_bufs     [ m_numBufs ] = (char *)mcalloc(size,ttt);
		//m_bufEnds  [ m_numBufs ] = NULL;
		if ( ! m_bufs [ m_numBufs ] ) {
			reset();
			return log("db: Could not allocate %"INT32" bytes for "
				   "cache for %s.",size,dbname);
		}
		m_numBufs++;
		bufMem         -= size;
		m_memAlloced   += size;
		m_totalBufSize += size;
	}

	// now fill ourselves up
	if ( m_convert ) 
		convertCache ( m_convertNumPtrsMax , m_convertMaxMem );

	return true;
}

//bool RdbCache::isInCache ( collnum_t collnum, key_t cacheKey, int32_t maxAge ) {
bool RdbCache::isInCache ( collnum_t collnum, char *cacheKey, int32_t maxAge ) {
	// maxAge of 0 means don't check cache
	if ( maxAge == 0 ) return false;
	// bail if no cache
	if ( m_numPtrsMax <= 0 ) return false;
	// look up in hash table
	//int32_t n=(cacheKey.n0 + (uint64_t)cacheKey.n1)% m_numPtrsMax;
	int32_t n = hash32 ( cacheKey , m_cks ) % m_numPtrsMax;
	// chain
	while ( m_ptrs[n] && 
		( *(collnum_t *)(m_ptrs[n]+0                ) != collnum ||
		  //*(key_t     *)(m_ptrs[n]+sizeof(collnum_t)) != cacheKey ) )
		  KEYCMP(m_ptrs[n]+sizeof(collnum_t),cacheKey,m_cks) != 0 ) )
		if ( ++n >= m_numPtrsMax ) n = 0;
	// return false if not found
	if ( ! m_ptrs[n] ) return false;
	// get timestamp
	char *p = m_ptrs[n];
	// skip over collnum_t and key
	//p += sizeof(collnum_t) + sizeof(key_t);
	p += sizeof(collnum_t) + m_cks;
	// get time stamp
	int32_t timestamp = *(int32_t *)p;
	// return false if too old
	if ( maxAge > 0 && getTimeLocal() - timestamp > maxAge ) return false;
	// return true if found
	return true;
}

// . a quick hack for SpiderCache.cpp
// . if your record is always a 4 byte int32_t call this
// . returns -1 if not found, so don't store -1 in there then
int64_t RdbCache::getLongLong ( collnum_t collnum ,
				  uint32_t key , int32_t maxAge ,
				  bool promoteRecord ) {
	char *rec;
	int32_t  recSize;
	key_t k;
	k.n0 = 0;
	k.n1 = (uint64_t)key;
	// sanity check
	//if ( m_cks != 4 ) { char *xx = NULL; *xx = 0; }
	// return -1 if not found
	if ( ! getRecord ( collnum  ,
			   //k        ,
			   //(char *)&key ,
			   (char *)&k,
			   &rec     ,
			   &recSize ,
			   false    ,
			   maxAge   , // in seconds, -1 means none
			   true     , // incCounts?
			   NULL     , // cacheTime ptr
			   promoteRecord ) ) 
		return -1LL;
	if ( recSize != 8 ) {
		log(LOG_LOGIC,"db: cache: Bad engineer. RecSize = %"INT32".",
		    recSize);
		return -1LL;
	}
	// otherwise, it was found and the right length, so return it
	return *(int64_t *)rec;
}

// both key and returned value are int64_ts for this
int64_t RdbCache::getLongLong2 ( collnum_t collnum ,
				  uint64_t key , int32_t maxAge ,
				  bool promoteRecord ) {
	char *rec;
	int32_t  recSize;
	key_t k;
	k.n0 = (uint64_t)key;
	k.n1 = 0;
	// sanity check
	if ( m_cks != 8 ) { char *xx = NULL; *xx = 0; }
	if ( m_dks != 0 ) { char *xx = NULL; *xx = 0; }
	// return -1 if not found
	if ( ! getRecord ( collnum  ,
			   (char *)&k,
			   &rec     ,
			   &recSize ,
			   false    ,
			   maxAge   , // in seconds, -1 means none
			   true     , // incCounts?
			   NULL     , // cacheTime ptr
			   promoteRecord ) ) 
		return -1LL;
	if ( recSize != 8 ) {
		log(LOG_LOGIC,"db: cache: Bad engineer. RecSize = %"INT32".",
		    recSize);
		return -1LL;
	}
	// otherwise, it was found and the right length, so return it
	return *(int64_t *)rec;
}
	
// this puts a int32_t in there
void RdbCache::addLongLong2 ( collnum_t collnum ,
			      uint64_t key , int64_t value ,
			      char **retRecPtr ) {
	key_t k;
	k.n0 = (uint64_t)key;
	k.n1 = 0;
	// sanity check
	if ( m_cks != 8 ) { char *xx = NULL; *xx = 0; }
	if ( m_dks != 0 ) { char *xx = NULL; *xx = 0; }
	addRecord ( collnum , (char *)&k , NULL , 0 , (char *)&value , 8 ,
		    0 , // timestamp=now
		    retRecPtr );
	// clear error in case addRecord set it
	g_errno = 0;
}

// this puts a int32_t in there
void RdbCache::addLongLong ( collnum_t collnum ,
			     uint32_t key , int64_t value ,
			     char **retRecPtr ) {
	key_t k;
	k.n0 = 0;
	k.n1 = (uint64_t)key;
	// sanity check
	//if ( m_cks != 4 ) { char *xx = NULL; *xx = 0; }
	// sanity check
	if ( m_cks > (int32_t)sizeof(key_t) ) { char *xx = NULL; *xx = 0; }
	//if ( m_dks != 0 ) { char *xx = NULL; *xx = 0; }
	//addRecord ( collnum , k , NULL , 0 , (char *)&value , 8 ,
	//addRecord ( collnum , (char *)&key , NULL , 0 , (char *)&value , 8 ,
	addRecord ( collnum , (char *)&k , NULL , 0 , (char *)&value , 8 ,
		    0 , // timestamp=now
		    retRecPtr );
	// clear error in case addRecord set it
	g_errno = 0;
}


int32_t RdbCache::getLong ( collnum_t collnum ,
			 uint64_t key , int32_t maxAge ,
			 bool promoteRecord ) {
	char *rec;
	int32_t  recSize;
	key_t k;
	// TODO: fix this!?! k.n0 = key, k.n1 = 0?
	k.n0 = 0;
	k.n1 = key;
	// return -1 if not found
	if ( ! getRecord ( collnum  ,
			   (char *)&k,
			   &rec     ,
			   &recSize ,
			   false    , // do copy?
			   maxAge   , // in seconds, -1 means none
			   true     , // incCounts?
			   NULL     , // cacheTime ptr
			   promoteRecord ) ) 
		return -1;
	if ( recSize != 4 ) {
		log(LOG_LOGIC,"db: cache: Bad engineer. RecSize = %"INT32".",
		    recSize);
		return -1;
	}
	// otherwise, it was found and the right length, so return it
	return *(int32_t *)rec;
}
	

// this puts a int32_t in there
void RdbCache::addLong ( collnum_t collnum ,
			 uint64_t key , int32_t value ,
			 char **retRecPtr ) {
	key_t k;
	k.n0 = 0;
	k.n1 = key;
	// sanity check
	if ( m_cks > (int32_t)sizeof(key_t) ) { char *xx = NULL; *xx = 0; }
	addRecord ( collnum , (char *)&k , NULL , 0 , (char *)&value , 
		    // by long we really mean 32 bits!
		    4,//sizeof(char *), // 4 , now 8 for 64 bit archs
		    0 , // timestamp=now
		    retRecPtr );
	// clear error in case addRecord set it
	g_errno = 0;
}


bool RdbCache::getRecord ( char    *coll       ,
			   //key_t    cacheKey   ,
			   char    *cacheKey   ,
			   char   **rec        ,
			   int32_t    *recSize    ,
			   bool     doCopy     ,
			   int32_t     maxAge     ,
			   bool     incCounts  ,
			   time_t  *cachedTime ,
			   bool     promoteRecord) {
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	if ( collnum < (collnum_t) 0 ) {
		log("db: Could not get cache rec for collection \"%s\".",coll);
		return false;
	}
	return getRecord ( collnum , cacheKey , rec , recSize , doCopy ,
			   maxAge , incCounts , cachedTime, promoteRecord );
}

// returns false if was not in the cache, true otherwise
bool RdbCache::setTimeStamp ( collnum_t  collnum      ,
			      char      *cacheKey     ,
			      int32_t       newTimeStamp ) {

	// return now if table empty
	if ( m_numPtrsMax <= 0 ) return false;
	// look up in hash table
	int32_t n = hash32 ( cacheKey , m_cks ) % m_numPtrsMax;
	// chain
	while ( m_ptrs[n] && 
		( *(collnum_t *)(m_ptrs[n]+0                ) != collnum ||
		  KEYCMP(m_ptrs[n]+sizeof(collnum_t),cacheKey,m_cks) != 0 ) )
		if ( ++n >= m_numPtrsMax ) n = 0;
	// return ptr to rec
	char *p = m_ptrs[n];
	// return false if not found
	if ( ! p ) return false;
	// skip over collnum and key
	p += sizeof(collnum_t) + m_cks;
	// set the timestamp
	*(int32_t *)p = newTimeStamp;
	return true;
}

// . returns true if found, false if not found in cache
// . sets *rec and *recSize iff found
bool RdbCache::getRecord ( collnum_t collnum   ,
			   //key_t    cacheKey   ,
			   char    *cacheKey   ,
			   char   **rec        ,
			   int32_t    *recSize    ,
			   bool     doCopy     ,
			   int32_t     maxAge     ,
			   bool     incCounts  ,
			   time_t  *cachedTime ,
			   bool     promoteRecord ) {
	// maxAge of 0 means don't check cache
	if ( maxAge == 0 ) return false;
	// bail if no cache
	if ( m_numPtrsMax <= 0 ) return false;
	// if init() called failed because of oom...
	if ( ! m_ptrs )
		//return log("cache: getRecord: failed because oom");
		return false;
	// time it -- debug
	int64_t t = 0LL ;
	if ( g_conf.m_logTimingDb ) t = gettimeofdayInMillisecondsLocal();
	// reset this
	if ( cachedTime ) *cachedTime = 0;
	// only do copy supported
	//if ( ! doCopy ) 
	//	return log("RdbCache::getRecord: only doCopy supported");
	// look up in hash table
	//int32_t n =(cacheKey.n0 + (uint64_t)cacheKey.n1)%m_numPtrsMax;
	int32_t n = hash32 ( cacheKey , m_cks ) % m_numPtrsMax;
	//int32_t n = cacheKey.n0 % m_numPtrsMax;
	// chain
	while ( m_ptrs[n] && 
		( *(collnum_t *)(m_ptrs[n]+0                ) != collnum ||
		  //*(key_t     *)(m_ptrs[n]+sizeof(collnum_t)) != cacheKey ) )
		  KEYCMP(m_ptrs[n]+sizeof(collnum_t),cacheKey,m_cks) != 0 ) )
		if ( ++n >= m_numPtrsMax ) n = 0;
	//while ( m_ptrs[n] && *(key_t *)m_ptrs[n] != cacheKey ) 
	//	if ( ++n >= m_numPtrsMax ) n = 0;
	// return false if not found
	if ( ! m_ptrs[n] ) {
		if ( incCounts ) m_numMisses++;
		return false;
	}
	// return ptr to rec
	char *p = m_ptrs[n];
	// if collnum is -1 then that means we set it to that in
	// RdbCache::clear(). this is kinda hacky.
	if ( *(collnum_t *)p == (collnum_t)-1 ) {
		if ( incCounts ) m_numMisses++;
		return false;
	}
	// skip over collnum and key
	//p += sizeof(collnum_t) + sizeof(key_t);
	p += sizeof(collnum_t) + m_cks;
	// skip over time stamp
	int32_t timestamp = *(int32_t *)p;
	if ( cachedTime ) *cachedTime = timestamp;
	// return false if too old
	if ( maxAge > 0 && getTimeLocal() - timestamp > maxAge ) {
		// debug msg
		// don't print for tagdb, however, spider prints it
		// too much and i don't care about it
		if ( m_dbname[0]!='s' || m_dbname[1]!='i' )
			log(LOG_DEBUG,"db: Found rec in cache for %s, "
			    "but elapsed time of %"INT32" is greater "
			    "than %"INT32".",
			    m_dbname, 
			    (int32_t)(getTimeLocal() - timestamp) , 
			    maxAge );
		if ( incCounts ) m_numMisses++;
		return false;
	}
	// skip timestamp
	p += 4;
	// store data size if our recs are var length or we cache lists of
	// fixed length recs, and those lists need a dataSize
	if ( m_fixedDataSize == -1 || m_supportLists ) { 
		*recSize = *(int32_t *)p; p += 4; }
	else    
		*recSize = m_fixedDataSize;


	// . debug testing -- remove later
	// . get checksum
	//char *s    = m_ptrs[n];
	//char *send = s + (p - s) + *recSize    - 3;
	//int32_t crc = 0;
	//while ( s < send ) { crc += *(int32_t *)s; s += 4; }
	//if ( crc != m_crcs[n] ) {
	//	log("BAD ENGINNEER. CRC MISMATCH.");
	//	char *pp = NULL;
	//	*pp = 1;
	//	sleep (10000);
	//}


	// set it for return
	*rec = p;
	// copy the data and set "list" with it iff "doCopy" is true
	if ( doCopy && *recSize > 0 ) {
		*rec = mdup ( p , *recSize , "RdbCache3" );
		if ( ! *rec ) {
			return log("db: Could not allocate space for "
				   "cached record for %s of %"INT32" bytes.",
				   m_dbname,*recSize);
		}
	}

	RdbCache *robots = Msg13::getHttpCacheRobots();
	RdbCache *others = Msg13::getHttpCacheOthers();

	//
	// now we only promote the record if it is near the delete head
	// in order to avoid having massive duplicity. if it is with 10%
	// of the delete head's space i guess.
	// i do this for all caches now... what are the downsides? i forget.
	//
	bool check = true;//false;
	//if ( this == &g_genericCache[SITEQUALITY_CACHEID] ) check = true;
	if ( this ==  g_dns.getCache      ()              ) check = true;
	if ( this ==  g_dns.getCacheLocal ()              ) check = true;
	if ( this ==  robots                              ) check = true;
	if ( this ==  others                              ) check = true;
	//if ( this == &g_deadWaitCache                   ) check = true;
	//if ( this == &g_forcedCache                       ) check = true;
	//if ( this == &g_alreadyAddedCache                 ) check = true;
	// this algo seems to have issues with really large recs
	// because spaces.live.com list was 570k and the data was foobar
	// so just don't do promotion on it ever...
	//if ( this == &g_tagdb.m_listCache                ) check = true;
	// the exact count cache...
	//if ( this == &g_qtable                            ) check = true;
	//if ( m_totalBufSize < 20000                       ) check = false;
	if ( check ) promoteRecord = false;
	// sanity check, do not allow the site quality cache or dns cache to 
	// be > 128MB, that just does not make sense and it complicates things
	//if(check && m_totalBufSize > BUFSIZE ) { char *xx = NULL; *xx = 0; }
	// sanity check
	if ( m_tail < 0 || m_tail > m_totalBufSize ) { 
		char *xx = NULL; *xx = 0; }
	// get the window of promotion
	int32_t  tenPercent = (int32_t)(((float)m_totalBufSize) * .10);
	char *start1     = m_bufs[0] + m_tail ;
	char *end1       = start1 + tenPercent;
	char *start2     = NULL;
	char *end2       = NULL;
	char *max        = m_bufs[0] + m_totalBufSize;
	if ( end1 > max ) {
		start2 = m_bufs[0];
		end2   = start2 + (end1 - max);
		end1   = max;
	}
	// are we in 10% range? if so, promote to head of the ring buffer
	// to avoid losing it in a delete operation
	if ( check && *rec >= start1 && *rec <= end1 ) promoteRecord = true;
	if ( check && *rec >= start2 && *rec <= end2 ) promoteRecord = true;
	// debug
	//if ( check )
	//	logf(LOG_DEBUG,
	//	     "db: promote=%"UINT32" "
	//	     "start1=%"UINT32" end1=%"UINT32" "
	//	     "start2=%"UINT32" end2=%"UINT32" "
	//	     "rec=%"UINT32" m_tail=%"UINT32" bufs[0]=%"UINT32" total=%"UINT32"",
	//	     (int32_t)promoteRecord ,
	//	     (int32_t)start1,(int32_t)end1,(int32_t)start2,(int32_t)end2,
	//	     (int32_t)*rec,(int32_t)m_tail,(int32_t)m_bufs[0],m_totalBufSize);
	
	// . now promote the record, same as adding (this always copies)
	// . do this after mdup as there is a chance it will overwrite
	//   the original record with the copy of the same record
	// . Process.cpp turns off g_cacheWritesEnabled while it saves them
	if ( promoteRecord && ! m_isSaving && g_cacheWritesEnabled ) {
		//char *ptr = m_ptrs[n];
		//removeKey ( collnum , cacheKey , ptr );
		//markDeletedRecord(ptr);

		//int32_t n = hash32 ( cacheKey , m_cks ) % m_numPtrsMax;
		//if ( this == &g_robotdb.m_rdbCache )
		// if ( this == &g_spiderLoop.m_winnerListCache ) {
		// 	logf(LOG_DEBUG, "db: cachebug: promoting record "
		// 	     "k.n0=0x%"XINT64" n=%"INT32"",
		// 	     ((key_t *)cacheKey)->n0,
		// 	     *recSize);
		// }
		char *retRec = NULL;
		addRecord ( collnum , cacheKey , *rec , *recSize , timestamp ,
			    &retRec );
		// update our rec, it might have been deleted then re-added
		// and we have to be careful of that delimter clobbering
		// memset() below
		if ( ! doCopy ) *rec = retRec;
	}
	// keep track of cache stats if we should
	if ( incCounts ) {
		m_numHits++;
		m_hitBytes += *recSize;
	}
	// debug msg time
	if ( g_conf.m_logTimingDb )
		log(LOG_TIMING,"db: cache: %s getRecord %"INT32" bytes took %"INT64" "
		    "ms.",m_dbname,*recSize,
		    gettimeofdayInMillisecondsLocal()-t);
	// it was found, so return true
	return true;
}


// . returns true if found, 
// . returns false if not found or on error
// . sets errno on error
// . list's data may be empty if it's a cached not found
// . we use "endKey" so we know if the FULL list was needed or not
// . if "incCounts" is true and we hit  we inc the hit  count
// . if "incCounts" is true and we miss we inc the miss count
bool RdbCache::getList ( collnum_t collnum  ,
			 //key_t    cacheKey  ,
			 //key_t    startKey  ,
			 char     *cacheKey  ,
			 char     *startKey  ,
			 RdbList *list      ,
			 bool     doCopy    ,
			 int32_t     maxAge    ,
			 bool     incCounts ) {
	// reset the list
	list->reset();
	// maxAge of 0 means don't check cache
	if ( maxAge == 0 ) return false;
	// get pure record
	int32_t  recSize;
	char *rec;
	// return false right away if not found
	if ( !  getRecord ( collnum   ,
			    cacheKey  , 
			    &rec      ,
			    &recSize  ,
			    doCopy    ,
			    maxAge    ,
			    incCounts ,
			    NULL      ) ) return false;
	// first 2 keys of bytes are the start and end keys
	//key_t endKey = *(key_t *)rec;
	//char *data     = rec     + sizeof(key_t);
	//int32_t  dataSize = recSize - sizeof(key_t);
	char *endKey   = rec;
	char *data     = rec     + m_dks;
	int32_t  dataSize = recSize - m_dks;
	// use NULL if empty
	if ( dataSize == 0 ) data = NULL;
	// how could this happen
	if ( dataSize <  0 ) return log(LOG_LOGIC,"db: cache: getList: "
					"Bad data size.");
	// . set the list!
	// . data is NULL if it's a cached not found (empty list)
	list->set ( data            ,
		    dataSize        ,
		    rec             , // alloc
		    recSize         , // alloc size
		    startKey        , 
		    endKey          ,
		    m_fixedDataSize , 
		    doCopy          ,  // ownData?
		    m_useHalfKeys   ,
		    m_dks           ); 
	// sanity check
	//bool ok = list->checkList_r ( false , true );
	//if ( ! ok ) log("RDBCACHE::GETLIST had problem");
	// break out
	//if ( ! ok ) { char *xx = NULL; *xx = 0; }
	return true;
}

// returns false and sets errno on error
//bool RdbCache::addList ( char *coll , key_t cacheKey , RdbList *list ) {
bool RdbCache::addList ( char *coll , char *cacheKey , RdbList *list ) {
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	if ( collnum < 0 ) {
		log("query: Collection %s does not exist. Cache failed.",
		    coll);
		return false;
	}
	return addList ( collnum , cacheKey , list );
}

// returns false and sets errno on error
//bool RdbCache::addList ( collnum_t collnum , key_t cacheKey , RdbList *list){
bool RdbCache::addList ( collnum_t collnum , char *cacheKey , RdbList *list ) {
	// . sanity check
	// . msg2 sometimes fails this check when it adds to the cache
	if ( list->m_ks != m_dks ) { 
		//g_errno = EBADENGINEER;
		return log("cache: key size %"INT32" != %"INT32"",
			   (int32_t)list->m_ks,(int32_t)m_dks);
		//char *xx = NULL; *xx = 0; }
	}
	// store endkey then list data in the record data slot
	//key_t k;
	//k = list->getLastKey  ();
	char *k = list->getLastKey  ();
	// just to make sure
	char *data     = list->getList();
	int32_t  dataSize = list->getListSize();
	if ( ! data ) dataSize = 0;
	// . add as a record
	// . key is combo of startKey/endKey
	// . return false on error (and set errno), false otherwise
	return  addRecord ( collnum  ,
			    cacheKey , 
			    //(char *)&k      , sizeof(key_t)       , 
			    k , m_dks ,
			    list->getList() , list->getListSize() ,
			    0 );
}

// . basically adding a list of only 1 record
// . used by dns/Dns.cpp to store ip's whose key is the hash of a hostname
// . "rec" may be a raw RdbList (format=key/recSize/rec) or may just be data
// . we do not copy "rec" so caller must malloc it
// . returns -1 on error and sets errno
// . returns node # in tree we added it to on success
bool RdbCache::addRecord ( collnum_t collnum ,
			   //key_t  cacheKey  , 
			   char  *cacheKey  , 
			   char  *rec       , 
			   int32_t   recSize   ,
			   int32_t   timestamp ,
			   char **retRecPtr ) {
	return addRecord (collnum, cacheKey, NULL, 0, rec, recSize, timestamp,
			  retRecPtr);
}

bool RdbCache::addRecord ( char  *coll      ,
			   //key_t  cacheKey  , 
			   char  *cacheKey  , 
			   char  *rec       , 
			   int32_t   recSize   ,
			   int32_t   timestamp ) {
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	if ( collnum < (collnum_t) 0 ) {
		log("db: Could not cache rec for collection \"%s\".",coll);
		return false;
	}
	return addRecord (collnum, cacheKey, NULL, 0, rec, recSize, timestamp);
}

bool RdbCache::addRecord ( collnum_t collnum ,
			   //key_t  cacheKey  , 
			   char  *cacheKey  , 
			   char  *rec1      ,
			   int32_t   recSize1  ,
			   char  *rec2      ,
			   int32_t   recSize2  ,
			   int32_t   timestamp ,
			   char **retRecPtr ) {

	// bail if cache empty. maybe m_maxMem is 0.
	if ( m_totalBufSize <= 0 ) return true;

	//int64_t startTime = gettimeofdayInMillisecondsLocal();
	if ( collnum < (collnum_t)0) {char *xx=NULL;*xx=0; }
	if ( collnum >= m_maxColls ) {char *xx=NULL;*xx=0; }
	// full key not allowed because we use that in markDeletedRecord()
	if ( KEYCMP(cacheKey,KEYMAX(),m_cks) == 0 ) { char  *xx=NULL;*xx=0; }

	// debug msg
	int64_t t = 0LL ;
	if ( g_conf.m_logTimingDb ) t = gettimeofdayInMillisecondsLocal();
	// need space for record data
	int32_t need = recSize1 + recSize2;
	// are we bad?
	if (m_fixedDataSize>=0 && ! m_supportLists && need != m_fixedDataSize){
		char *xx=NULL;*xx=0;
		return log(LOG_LOGIC,"db: cache: addRecord: %"INT32" != %"INT32".",
			   need,m_fixedDataSize);
	}
	// don't allow 0 timestamps, those are special indicators
	if ( timestamp == 0 ) timestamp = getTimeLocal();
	//if ( timestamp == 0 && cacheKey.n0 == 0LL && cacheKey.n1 == 0 )
	if ( timestamp == 0 && KEYCMP(cacheKey,KEYMIN(),m_cks)==0 )
		return log(LOG_LOGIC,"db: cache: addRecord: Bad "
			   "key/timestamp.");
	// bail if no writing ops allowed now
	if ( ! g_cacheWritesEnabled ) return false;
	if (   m_isSaving           ) return false;
	// collnum_t and cache key
	//need += sizeof(collnum_t) + sizeof(key_t);
	need += sizeof(collnum_t) + m_cks;
	// timestamp
	need += 4;
	// . trailing 0 collnum_t, key and trailing time stamp
	// . this DELIMETER tells us to go to the next buf
	//need += sizeof(collnum_t) + sizeof(key_t) + 4 ; // timestamp
	need += sizeof(collnum_t) + m_cks + 4 ;
	// and size, if not fixed or we support lists
	if ( m_fixedDataSize == -1 || m_supportLists ) need += 4;
	// watch out
	if ( need >= m_totalBufSize )
		return log(LOG_INFO,
			   "db: Could not fit record of %"INT32" bytes into %s "
			   "cache. Max size is %"INT32".",need,m_dbname,
			   m_totalBufSize);
	if ( need >= BUFSIZE )
		return log(LOG_INFO,
			   "db: Could not fit record of %"INT32" bytes into %s "
			   "cache. Max size is %i.",need,m_dbname,BUFSIZE);

	// if too many slots in hash table used free one up
	while ( m_numPtrsUsed >= m_threshold ) {
		if ( ! deleteRec() ) {
			return false;
		}
	}

	// . do NOT split across buffers, align on a boundary if we need to
	// . "i1" is where we PLAN to store the record
	int32_t i1      = m_offset;
	int32_t bufNum1 = i1 / BUFSIZE;

	// what buffer does the byte AFTER our last byte fall into?
	int32_t i2      = m_offset + need;
	int32_t bufNum2 = i2 / BUFSIZE;

	// BUT if bufNum1 is the last buffer, it will most likely be SMALLER
	// than "BUFSIZE" byts, so do a special check to see if "i2" falls 
	// outside of it!
	if ( i2 >= m_totalBufSize ) bufNum2 = bufNum1 + 1;

	// . "i1b" is offset of where we REALLY store the record
	// . "i2b" is the offset of the byte after the last byte that we store
	int32_t i1b = i1;
	int32_t i2b = i2;
	if ( bufNum1 != bufNum2 ) {
		// advance to first byte of the next buffer if not enough room
		// in bufNum1 to FULLY contain the record
		i1b = bufNum2 * BUFSIZE;
		i2b = i1b + need;
	}

	// . no, "i1c" is where we "really really" store it
	// . and "i2c" is the offset of the byte after the last we store
	int32_t i1c = i1b;
	int32_t i2c = i2b;
	if ( i2b >= m_totalBufSize ) {
		// reset back to the very beginning...
		i1c = 0;
		i2c = i1c + need;
	}

	// save for debug
	//int32_t saved = m_tail;


	// . increase m_tail so it is NOT in the range: [i1,i2b)
	// . NEVER do this if we are the first rec added though, because
	//   m_tail will equal i1 at that point...
	while ( m_numPtrsUsed!=0 && m_tail>=i1 && m_tail<=i2 )
		deleteRec();

	while ( m_numPtrsUsed!=0 && m_tail>=i1b && m_tail<=i2b )
		deleteRec();

	while ( m_numPtrsUsed!=0 && m_tail>=i1c && m_tail<=i2c )
		deleteRec();

	// store rec at "start"
	int32_t  bufNumStart = i1c / BUFSIZE;
	char *start       = m_bufs[bufNumStart] + i1c % BUFSIZE;

	// point to storage area
	char *p = start;

	// before we start writing over possible record data,
	// if we are promoting a rec, "rec" may actually point
	// somewhere into here, so be careful!
	//if ( rec2 <= start && rec2+recSize2 > start ) { char*xx=NULL;*xx=0;}
	//if ( start <= rec2 && start+32>= rec2       ) { char*xx=NULL;*xx=0;}

	//if ( this == &g_robotdb.m_rdbCache )
	// if ( this == &g_spiderLoop.m_winnerListCache )
	// 	logf(LOG_DEBUG, "db: cachebug: adding rec k.n0=0x%"XINT64" "
	// 	     "rs=%"INT32" "
	// 	     "off=%"INT32" bufNum=%"INT32" ptr=0x%"PTRFMT" "
	// 	     "oldtail=%"INT32" "
	// 	     "newtail=%"INT32" "
	// 	     "numPtrs=%"INT32"",
	// 	     ((key_t *)cacheKey)->n0,recSize1+recSize2,
	// 	     i1c,bufNumStart,(PTRTYPE)p,saved,m_tail,m_numPtrsUsed);

	// if we wiped out all recs then reset tail to m_offset
	if ( m_numPtrsUsed == 0 ) {
		//if ( this == &g_robotdb.m_rdbCache )
		// if ( this == &g_spiderLoop.m_winnerListCache )
		// 	logf(LOG_DEBUG,"db: cachebug: full tail reset. "
		// 	     "tail=0");
		m_tail = 0;
	}

	// store collnum
	*(collnum_t *)p = collnum; p += sizeof(collnum_t);
	// store key
	//*(key_t *)p = cacheKey; p += sizeof(key_t);
	KEYSET(p,cacheKey,m_cks); p += m_cks;
	// store timestamp
	*(int32_t *)p = timestamp; p += 4;
	// then dataSize if we need to
	if ( m_fixedDataSize == -1 || m_supportLists ) { 
		*(int32_t *)p = recSize1+recSize2; p +=4; } //datasize
	// sanity : check if the recSizes add up right
	else if ( m_fixedDataSize != recSize1 + recSize2 ){
		char *xx = NULL; *xx = 0; }
	// save for returning
	if ( retRecPtr ) *retRecPtr = p;
	// sanity check
	//if ( rec1 < p && rec1 + recSize1 > p ) { char*xx=NULL;*xx=0;}
	//if ( rec2 < p && rec2 + recSize2 > p ) { char*xx=NULL;*xx=0;}
	//if ( rec1 >= p && rec1 < p + need ) { char*xx=NULL;*xx=0;}
	//if ( rec2 >= p && rec2 < p + need ) { 
	//	log("cache: poop");}//char*xx=NULL;*xx=0;}
	// then data
	gbmemcpy ( p , rec1 , recSize1 ); p += recSize1;
	gbmemcpy ( p , rec2 , recSize2 ); p += recSize2;

	// . store 0 collnum, key AND timestamp at end of record --> delimeter
	// . CAUTION: if doing a "promote" we can end up deleting the rec
	//   we are pointing to, then clobbering it with this memset!
	//memset ( p , 0 , sizeof(collnum_t) + 16 /*key+timestamp*/);
	memset ( p , 0 , sizeof(collnum_t) + m_cks + 4 /*key+timestamp*/);

	// count the occupied memory, excluding the terminating 0 key
	m_memOccupied += ( p - start ); 

	// debug msg (MDW)
	// if ( this == &g_spiderLoop.m_winnerListCache ) {
	// log("cache: adding rec @ %"UINT32" size=%i tail=%"INT32"",
	//     i1c,(int)(p-start),m_tail);
	// log("cache: stored k.n1=%"UINT32" k.n0=%"UINT64" %"INT32" bytes @ %"UINT32" tail=%"UINT32"",
	//     ((key_t *)cacheKey)->n1,
	//     ((key_t *)cacheKey)->n0,(int)(p-start),i1c,m_tail);
	// }
	//if ( m_cks == 4 )
	//	log("stored k=%"XINT32" %"INT32" bytes @ %"UINT32"",
	//	    *(int32_t *)cacheKey,p-start,i);//(uint32_t)start);

	// update offset, excluding the terminating 0 key
	m_offset = i1c + ( p - start );

	// . debug testing -- remove later
	// . get the crc of the whole thing
	//char *s    = start; 
	//char *send = p        - 3;
	//int32_t crc = 0;
	//while ( s < send ) { crc += *(int32_t *)s; s += 4; }

	// . add to hash table
	// . if we are already in there, preserve the 
	addKey ( collnum , cacheKey , start ); // , crc ); // debug test
	// debug msg time
	log(LOG_TIMING,"db: cache: %s addRecord %"INT32" bytes took %"INT64" "
	    "ms this=0x%"PTRFMT" key.n1=%"UINT32" n0=%"UINT64"",
	    m_dbname, (int32_t)(p - start) , 
	    gettimeofdayInMillisecondsLocal()-t,
	    (PTRTYPE)this,
	    ((key_t *)(&cacheKey))->n1 ,
	    ((key_t *)(&cacheKey))->n0 );


	//log("%s addRecord %"INT32" bytes @ offset=%"INT32" k.n1=%"UINT32" n0=%"UINT64" "
	//     "TOOK %"INT64" ms" , 
	//     m_dbname , need , i , 
	//     cacheKey.n1 , cacheKey.n0 ,
	m_adds++;

	//int64_t now = gettimeofdayInMillisecondsLocal();
	//int64_t took = now - startTime;
	//if(took > 10) 
	//	log(LOG_INFO, "admin: adding to RdbCache %s of %"INT32" bytes "
	//	    "took %"INT64" ms.",m_dbname,recSize1+recSize2,took);

	m_needsSave = true;

	return true;
}

// delete the rec at m_tail from the hashtable
bool RdbCache::deleteRec ( ) {
	// sanity. 
	if ( m_tail < 0 || m_tail >= m_totalBufSize ) {
		char *xx = NULL; *xx = 0;}

	// don't do anything if we're empty
	// ...fix...we ned to make sure the head doesn't eat the tail, so
	// don't ever skip this stuff
	//if ( m_numPtrsUsed <= 0 ) return;
	//if ( b == 36887 )
	//	log("hey");
	// delete all recs in [a,b]		
	//if (b > m_totalBufSize) b = m_totalBufSize;
	//while ( m_tail < b ) {

	// get ptr from offset
	int32_t  bufNum = m_tail / BUFSIZE;
	char *p      = m_bufs[bufNum] + m_tail % BUFSIZE;

 top:

	// get ptr to where tail is currently
	char *start  = p;
	// get collnum
	collnum_t collnum = *(collnum_t *)p; p += sizeof(collnum_t);
	// NSD: trying to find the error where removeKey() doesn't
	// find the key even after going through all the records
	// I think that the data here is corrupted or not pointed right
	
	// . collnum can be 0 in case we have to go to next buffer
	// . allow -1 collnum to exist, seems to happen in robots.txt cache
	//   sometimes, maybe for delete collnum... not sure, but the timestamp
	//   seems to be legit
	if ( collnum >= m_maxColls || collnum < -1
			       // we now call ::reset(oldcollnum)
			       // when resetting a collection in
			       // Collectiondb::resetColl() which calls
			       // SpiderColl::clear() which calls
			       // lastDownloadTime.reset(oldcollnum)
			       // and then we nuke the collrec so it was
			       // triggering this. so check m_ptrs[i]==-1
			       //|| !g_collectiondb.m_recs[collnum]
	     ) {
		log (LOG_WARN,"db: cache: deleteRec: possible "
		     "corruption, start=%"PTRFMT" collNum=%"INT32" "
		     "maxCollNum=%"INT32" dbname=%s", (PTRTYPE)start,
		     (int32_t)collnum, g_collectiondb.m_numRecsUsed,  
		     m_dbname);
		char *xx=NULL;*xx=0;
		// exception for gourav's bug (dbname=Users)
		// i am tired of it craping out every 2-3 wks
		//if ( m_dbname[0]=='U' ) return true;
		// some records might have been deleted
		m_needsSave = true;
		// but its corrupt so don't save to disk
		m_corruptionDetected = true;
		//char *xx=NULL;*xx=0;
		return false;
	}
	
	// get key
	//key_t k = *(key_t *)p ; p += sizeof(key_t);
	char *k = p ; p += m_cks;
	// get time stamp
	int32_t  timestamp = *(int32_t  *)p ; p += 4;
	// a timestamp of 0 and 0 key, means go to next buffer
	//if ( timestamp == 0 && k.n0 == 0LL && k.n1 == 0 ) {
	if ( timestamp == 0 && KEYCMP(k,KEYMIN(),m_cks)==0 ) {
		// if we wrap around back to first buffer then
		// change the "wrapped" state to false. that means
		// we are no longer directly in front of the write
		// head, but behind him again.
		if ( ++bufNum >= m_numBufs ) { 
			bufNum = 0; 
			//m_tail = 0; 
			//m_wrapped = false; 
			//return true;  //continue;
		}
		// otherwise, point to the start of the next buffer
		p      = m_bufs[bufNum];
		m_tail = bufNum * BUFSIZE;
		// sanity
		//if ( m_tail < 0  || m_tail > m_totalBufSize ) {
		//	char *xx = NULL; *xx = 0;}
		// if ( this == &g_spiderLoop.m_winnerListCache )
		// 	logf(LOG_DEBUG, "db: cachebug: wrapping tail to 0");
		//return true; // continue;
		goto top;
	}
	
	// get data size
	int32_t dataSize;
	// get dataSize and data
	if ( m_fixedDataSize == -1 || m_supportLists ) {
		dataSize = *(int32_t *)p; p += 4; }
	else 	
		dataSize = m_fixedDataSize;
	
	// sanity
	if ( dataSize < 0 || dataSize > m_totalBufSize ){
		char *xx = NULL; *xx = 0;
	}

	//int32_t saved = m_tail;
	
	// debug msg (MDW)
	// if ( this == &g_spiderLoop.m_winnerListCache ) {
	// log("cache: deleting rec @ %"INT32" size=%"INT32"",m_tail,
	//     dataSize+2+12+4+4);
	// }

	// skip over rest of rec
	p += dataSize;
	// remove this rec from the count, (4 bytes for ptr)
	//m_memOccupied -= (p - start) + 4;
	// otherwise, it's a simple advance
	m_tail += (p - start);
	
	// sanity. this must be failing due to a corrupt dataSize...
	if ( m_tail < 0 || 
	     m_tail +(int32_t)sizeof(collnum_t)+m_cks+4>m_totalBufSize){
		char *xx = NULL; *xx = 0;}
	
	// if ( this == &g_spiderLoop.m_winnerListCache )
	// 	log("spider: rdbcache: removing tail rec collnum=%i",
	// 	    (int)collnum);

	// delete key from hash table, iff is for THIS record
	// but if it has not already been voided.
	// we set key to KEYMAX() in markDeletedRecord()
	if ( KEYCMP(k,KEYMAX(),m_cks) != 0 ){
		removeKey ( collnum , k , start );
		markDeletedRecord(start);
	}


	//if ( this == &g_robotdb.m_rdbCache ) 
	// if ( this == &g_spiderLoop.m_winnerListCache )
	// 	logf(LOG_DEBUG, "db: cachebug: removing k.n0=0x%"XINT64" "
	// 	     "oldtail=%"INT32" newtail=%"INT32" ds=%"INT32"", 
	// 	     ((key_t *)k)->n0,saved,m_tail,dataSize);

	//else
	//	logf(LOG_DEBUG,"test: oops");
	// count as a delete
	m_deletes++;
	// void this key in the buffer, 
	// so it doesn't try to delete it later from
	// the hash table
	// memset(start+sizeof(collnum_t), 0xff, m_cks);
	
	// debug msg
	//log("%s m_tail = %"INT32", #ptrs=%"INT32"",
	//     m_dbname,m_tail,m_numPtrsUsed);
	//}
	// debug msg
	//log("%s m_tail = %"INT32", #ptrs=%"INT32"",m_dbname,m_tail,m_numPtrsUsed);
	m_needsSave = true;
	return true;
}

// mark a record in the buffer deleted to ensure that we reclaim the memory 
// and attempt to delete the key only once.
void RdbCache::markDeletedRecord(char *ptr){
	int32_t dataSize = sizeof(collnum_t)+m_cks+sizeof(int32_t);
	// debug it 
	// if ( this == &g_spiderLoop.m_winnerListCache ) {
	//logf(LOG_DEBUG,"cache: makeDeleteRec ptr=0x%"PTRFMT" off=%"INT32"",
	//      (PTRTYPE)ptr,(int32_t)(ptr-m_bufs[0]));
	// }
	// get dataSize and data
	if ( m_fixedDataSize == -1 || m_supportLists ) {
		dataSize += 4 +                      // size
			*(int32_t*)(ptr+
				 sizeof(collnum_t)+ // collnum
				 m_cks+             // key
				 sizeof(int32_t));     // timestamp 
		
	}
	else 	
		dataSize += m_fixedDataSize;
	
	// mark newly freed mem
	m_memOccupied -= dataSize;
	
	// relabel old record key as 0xffffffff... so key is not removed 
	// more than once.
	memset(ptr+sizeof(collnum_t), 0xff, m_cks);
}

// patch the hole so chaining still works
//void RdbCache::removeKey ( collnum_t collnum , key_t key , char *rec ) {
void RdbCache::removeKey ( collnum_t collnum , char *key , char *rec ) {
	//int32_t n = (key.n0 + (uint64_t)key.n1)% m_numPtrsMax;
	int32_t n = hash32 ( key , m_cks ) % m_numPtrsMax;
	// debug msg
	//if ( m_cks == 4)
	//	log("remove first try = slot #%"INT32" (%"INT32")",n,m_numPtrsMax);
	// debug msg
	//log("%s removing key.n1=%"UINT32" key.n0=%"UINT64"",m_dbname,key.n1,key.n0);
	//if ( m_cks == 4 )
	//	log("removing k=%"XINT32"",*(int32_t *)key);
	// chain
	while ( m_ptrs[n] && 
		( *(collnum_t *)(m_ptrs[n]+0                ) != collnum ||
		  //*(key_t     *)(m_ptrs[n]+sizeof(collnum_t)) != key     ) )
		  KEYCMP(m_ptrs[n]+sizeof(collnum_t),key,m_cks) != 0 ) )
		if ( ++n >= m_numPtrsMax ) n = 0;
	//while ( m_ptrs[n] && *(key_t *)m_ptrs[n] != key ) 
	//	if ( ++n >= m_numPtrsMax ) n = 0;
	// . return false if key not found
	// . this happens sometimes, if m_tail wraps to 0 and the new rec
	//   the gets placed at the end of the last m_buf, changing m_bufEnds
	//   then m_tail may revisit many recs it already removed from hashtbl
	if ( ! m_ptrs[n] ) {
		log(LOG_LOGIC,"db: cache: removeKey: Could not find key. "
		    "Trying to scan whole table.");
		// try scanning whole table
		int32_t i;
		for ( i = 0 ; i < m_numPtrsMax ; i++ ) {
			// skip if empty
			if ( ! m_ptrs[i] ) continue;
			// skip if no match
			if (KEYCMP(m_ptrs[i]+sizeof(collnum_t),key,m_cks) != 0)
				continue;
			// got a match
			log(LOG_LOGIC,"db: cache: removeKey. Found key in "
			    "linear scan. Wierd.");
			n = i;
			break;
		}
		if ( i >= m_numPtrsMax ) {
			log(LOG_LOGIC,"db: cache: removeKey: BAD ENGINEER. "
			    "dbname=%s",m_dbname );
			char *xx = NULL;
			*xx = 1;
			return;
		}
	}
	// if does not point to this rec , it is now pointing to the latest,
	// promoted copy of the rec, so do not delete
	if ( m_ptrs[n] != rec ) {
		// debug msg
		// This shouldn't happen anymore -partap
		char *xx = NULL; xx = 0;
		return;
	}

	// debug msg 
	//key_t *k = (key_t *)(m_ptrs[n]+2);
	//log("cache: %s removing key.n1=%"UINT32" key.n0=%"UINT64" from slot #%"INT32"",
	//    m_dbname,k->n1,k->n0,n);
	
	// all done if already cleared
	if ( ! m_ptrs[n] ) return;
	// clear it
	m_ptrs[n] = NULL;
	m_numPtrsUsed--;
	m_memOccupied -= sizeof(char *);//4;
	// advance through list after us now
	if ( ++n >= m_numPtrsMax ) n = 0;
	// keep looping until we hit an empty slot
	while ( m_ptrs[n] ) {
		char *ptr = m_ptrs[n];
		// point to the key
		char *kptr = ptr + sizeof(collnum_t);
		// clear it
		m_ptrs[n] = NULL;
		// undo stats
		m_numPtrsUsed--;
		m_memOccupied -= sizeof(char *);//4;
		// re-hash it back to possibly fill the "gap"
		addKey ( *(collnum_t *)ptr , kptr , ptr );
		if ( ++n >= m_numPtrsMax ) n = 0;
	}
}

//void RdbCache::addKey ( collnum_t collnum , key_t key , char *ptr ) { 
void RdbCache::addKey ( collnum_t collnum , char *key , char *ptr ) { 
	// look up in hash table
	//int32_t n = (key.n0 + (uint64_t)key.n1)% m_numPtrsMax;
	int32_t n = hash32 ( key , m_cks ) % m_numPtrsMax;
	// save orig for debugging
	//int32_t n2 = n;
	// debug msg
	//log("add first try = slot #%"INT32" (%"INT32")",n,m_numPtrsMax);
	//int32_t n = key.n0 % m_numPtrsMax;
	// chain
	while ( m_ptrs[n] && 
		( *(collnum_t *)(m_ptrs[n]+0                ) != collnum ||
		  //*(key_t     *)(m_ptrs[n]+sizeof(collnum_t)) != key     ) )
		  KEYCMP(m_ptrs[n]+sizeof(collnum_t),key,m_cks) != 0 ) )
		if ( ++n >= m_numPtrsMax ) n = 0;
	//while ( m_ptrs[n] && *(key_t *)m_ptrs[n] != key ) 
	//	if ( ++n >= m_numPtrsMax ) n = 0;
	// if already there don't inc the count
	if ( ! m_ptrs[n] ) {
		m_numPtrsUsed++;
		m_memOccupied += sizeof(char *);
		// debug msg 
		//key_t *k = (key_t *)key;
		//log("cache: %s added key.n1=%"UINT32" key.n0=%"UINT64" to slot #%"INT32" "
		//    "ptr=0x%"XINT32" off=%"INT32" size=%"INT32"",
		//    m_dbname,k->n1,k->n0,n,ptr,ptr-m_bufs[0],
		//    *(int32_t *)(ptr+2+12+4));
	}
	// debug msg
	//else 
	//	log("%s update key.n1=%"UINT32" key.n0=%"UINT64" in slot #%"INT32"",
	//	    m_dbname,key.n1,key.n0,n);
		
	// If this pointer is already set, we may be replacing it from 
	// Msg5::needRecall.  We need to mark the old record as deleted
	if (m_ptrs[n]){
		//char *xx = NULL; *xx = 0;
		markDeletedRecord(m_ptrs[n]);
	}
	// store the ptr
	m_ptrs[n] = ptr;
	// debug testing
	//m_crcs[n] = crc;

	//if ( this == &g_robotdb.m_rdbCache ) 
	// if ( this == &g_spiderLoop.m_winnerListCache )
	// 	logf(LOG_DEBUG,"db: cachebug: addkey slot #%"INT32" has "
	// 	     "ptr=0x%"PTRFMT"",n,(PTRTYPE)ptr);

}

/*
void RdbCache::clearAll ( ) {

	//if ( m_numBufs > 0 )
	//	log("db: resetting record cache");
	m_offset = 0;
	m_tail   = 0;
	//for ( int32_t i = 0 ; i < m_numBufs ; i++ )
	//	// all bufs, but not necessarily last, are BUFSIZE bytes big
	//	mfree ( m_bufs[i] , m_bufSizes[i] , "RdbCache" );
	//m_numBufs     = 0;
	//m_totalBufSize= 0;

	//if(m_ptrs ) mfree ( m_ptrs , m_numPtrsMax*sizeof(char *),"RdbCache");
	//m_ptrs        = NULL;
	m_numPtrsUsed = 0;
	// can't reset this, breaks the load!
	//m_numPtrsMax  = 0;

	m_memOccupied = 0;
	//m_memAlloced  = 0;
	m_numHits     = 0;
	m_numMisses   = 0;

	//m_wrapped = false;
	m_adds    = 0;
	m_deletes = 0;

	// assume no need to call convertCache()
	m_convert = false;

	m_isSaving = false;
}
*/

//
// . MDW: took out clear() for corruption suspicision... i think ninad's
//   corruption detection would panic on collnum_t's of -1 anyway...
//
// . this just clears the contents of the cache
// . used when deleting a collection in Rdb::delColl() and used in
//   Rdb::updateToRebuild() when updating/setting the rdb to a rebuilt rdb
// . try it again now with new 64-bit logic updates (MDW 2/10/2015)
void RdbCache::clear ( collnum_t collnum ) {
	// bail if no writing ops allowed now
	if ( ! g_cacheWritesEnabled ) { char *xx=NULL;*xx=0; }
	if (   m_isSaving           ) { char *xx=NULL;*xx=0; }

	for ( int32_t i = 0 ; i < m_numPtrsMax ; i++ ) {
		// skip if empty bucket
		if ( ! m_ptrs[i] ) continue;
		// skip if wrong collection
		if ( *(collnum_t *)m_ptrs[i] != collnum ) continue;
		// change to the -1 collection, nobody should use that and
		// it should get kicked out over time
		//*(collnum_t *)m_ptrs[i] = -1;
		// just change the collnum to something impossible
		// this is kinda hacky but hopefully will not cause corruption
		*(collnum_t *)m_ptrs[i] = (collnum_t)-1;
	}
}

bool RdbCache::load ( ) {
	return load ( m_dbname );
}

static void *saveWrapper      ( void *state , ThreadEntry *te ) ;
static void threadDoneWrapper ( void *state , ThreadEntry *te ) ;

// . just like RdbTree::fastSave()
// . returns false if blocked and is saving
bool RdbCache::save ( bool useThreads ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// if we do not need it, don't bother
	if ( ! m_needsSave ) return true;
	// if corruption was detected, don't bother
	if ( m_corruptionDetected ) return true;
	// return true if already in the middle of saving
	if ( m_isSaving ) return false;

	// log
	log(LOG_INIT,"db: Saving %"INT32" bytes of cache to %s/%s.cache",
	     m_memAlloced,g_hostdb.m_dir,m_dbname);

	// spawn the thread
	if ( useThreads ) {
		// lock cache while saving
		m_isSaving = true;
		// make a thread. returns true on success, in which case
		// we return false to indicate we blocked.
		if ( g_threads.call ( SAVETREE_THREAD   ,
				      1                 , // niceness
				      this              , // state
				      threadDoneWrapper , // callback
				      saveWrapper       ) )
			return false;
		// crap had an error spawning thread
		if ( ! g_threads.m_disabled )
			log("db: Error spawning cache write thread. "
			    "Not using threads.");
	}
	// do it directly with no thread
	save_r();
	// wrap it up
	threadDone ();
	return true;
}

void threadDoneWrapper ( void *state , ThreadEntry *te ) {
	RdbCache *THIS = (RdbCache *)state;
	THIS->threadDone ( );
}

void RdbCache::threadDone ( ) {
	// allow cache to change now
	m_isSaving  = false;
	// and we are in sync with that data saved on disk
	m_needsSave = false;
	// report
	if ( m_saveError )
		log("db: Had error saving cache to disk for %s: %s.",
		    m_dbname,mstrerror(m_saveError));
}

void *saveWrapper ( void *state , ThreadEntry *te ) {
	RdbCache *THIS = (RdbCache *)state;
	// assume no error
	THIS->m_saveError = 0;
	// do it
	if ( THIS->save_r () ) return NULL;
	// we got an error, save it
	THIS->m_saveError = errno;
	return NULL;
}

// returns false withe rrno set on error
bool RdbCache::save_r ( ) {
	// append .cache to "dbname" to get cache filename
	char filename [ 64 ];
	if ( gbstrlen(m_dbname) > 50 )
		return log("db: Dbname too long. Could not save cache.");
	sprintf ( filename , "%s%s.cache" , g_hostdb.m_dir , m_dbname );
	//File f;
	//f.set ( g_hostdb.m_dir , filename );
	// open the file
	//if ( ! f.open ( O_RDWR | O_CREAT ) ) 
	int fd = open ( filename , O_RDWR | O_CREAT , getFileCreationFlags() );
	if ( fd < 0 )
		return log("db: Had opening file to save cache to: %s.", 
		    mstrerror(errno));

	bool status = save2_r ( fd );
	
	close ( fd );

	return status;
}

bool RdbCache::save2_r ( int fd ) {
	int n;
	int32_t off = 0;
	// general info
	n = gbpwrite ( fd , &m_numPtrsMax  , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_maxMem      , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	// mem stuff
	n = gbpwrite ( fd , &m_memAlloced  , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_memOccupied , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	// save the buffer stuff
	n = gbpwrite ( fd , &m_numBufs     , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_totalBufSize, 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_offset      , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_tail        , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_wrapped     , 1 , off ); off += 1;
	if ( n!= 1 ) return false;
	// write each buf
	for ( int32_t i = 0 ; i < m_numBufs ; i++ ) {
		// write end relative
		//int32_t end = (m_bufEnds[i] - m_bufs[i]);
		//if ( end < 0 ) end = -1;
		//n = pwrite ( fd ,&end , 4 , off ); off += 4;
		//if ( n != 4 ) return false;
		// and buf size
		int32_t bufSize = m_bufSizes[i];
		n = gbpwrite ( fd , &bufSize , 4 , off ); off += 4;
		if ( n != 4 ) return false;
		// then write contents of buffer #i
		n = gbpwrite ( fd, m_bufs[i] , bufSize , off ); off += bufSize;
		if ( n != bufSize ) return false;
	}
	// save the hash table stuff
	n = gbpwrite ( fd , &m_numPtrsUsed , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = gbpwrite ( fd , &m_threshold   , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	// save 100k at a time
	int32_t i = 0;
	while ( i < m_numPtrsMax )
		if ( ! saveSome_r ( fd, &i , &off) ) return false;
	//close ( fd ) ;
	return true;
}

#define SAVEBUFSIZE (256*1024)

bool RdbCache::saveSome_r ( int fd , int32_t *iptr , int32_t *off ) {
	char buf[SAVEBUFSIZE];
	char *bufEnd = buf + SAVEBUFSIZE;
	// point to buf
	char *bp = buf;
	int32_t used = 0;
	// make hash table ptrs relative to offset
	for ( ; *iptr < m_numPtrsMax && bp + 4 < bufEnd ; *iptr = *iptr + 1 ) {
		// resume at i
		char *p = m_ptrs[*iptr];
		// if empty, write a -1 offset
		if ( ! p ) {
			//int32_t tt = -1;
			// store that as it is
			*(int32_t *)bp = -1; bp += 4;
			//n = pwrite ( fd ,&tt , 4 , off ) ; off += 4;
			//if ( n != 4 ) return false;
			continue;
		}
		// otherwise convert ptr to offset... bitch
		int32_t converted = -1;
		for ( int32_t j = 0 ; j < m_numBufs ; j++ )
			// is p pointing into the jth buffer?
			if ( p >= m_bufs[j] && p < m_bufs[j] + m_bufSizes[j] ){
				// if so, make it relative
				converted = p - m_bufs[j] + BUFSIZE*j ;
				break;
			}
		// bitch if not found
		if ( converted == -1 ) 
			return log(LOG_LOGIC,"db: cache: save: Bad "
				   "engineer");
		// store that as it is
		*(int32_t *)bp = converted; bp += 4;
		used++;
		//n = pwrite ( fd ,&converted , 4 , off ) ; off += 4;
		//if ( n != 4 ) return false;
	}
	if ( used != m_numPtrsUsed ) { 
		log("cache: error saving cache. %"INT32" != %"INT32""
		    , used , m_numPtrsUsed );
		//char *xx=NULL;*xx=0; }
		return false;
	}
	// now write it all at once
	int32_t size = bp - buf;
	int32_t n = gbpwrite ( fd , buf , size , *off );  *off = *off + size;
	if ( n != size ) return false;
	return true;
}

bool RdbCache::load ( char *dbname ) {
	// append .cache to "dbname" to get cache filename
	char filename [ 64 ];
	if ( gbstrlen(dbname) > 50 )
		return log(LOG_LOGIC,"db: cache: load: dbname too long.");
	sprintf ( filename , "%s.cache" , dbname );
	// does the file exist?
	File f;
	f.set ( g_hostdb.m_dir , filename );
	// having cache file not existing on disk is not so bad, it's a cache
	if ( ! f.doesExist() )
		return false;
	//	return log("db: Could not load cache from %s: does not exist.",
	//		   f.getFilename());

	// open the file
	if ( ! f.open ( O_RDWR ) ) 
		return log("db: Could not open cache save file for %s: %s.", 
			   dbname,mstrerror(g_errno));
	// log
	log(LOG_INIT,"db: Loading cache from %s/%s.cache",
	     g_hostdb.m_dir,dbname);
	// clear everything
	reset();
	int n;
	int32_t off = 0;
	// general info
	int32_t numPtrsMax ;
	int32_t maxMem     ;
	n = f.read ( &numPtrsMax  , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &maxMem      , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	// . they need to match our current config to continue loading
	// . attempt to convert if not, because it is painful to rebuild
	//   the site quality cache
	if ( numPtrsMax != m_numPtrsMax ||
	     maxMem     != m_maxMem      ) {
		log("db: Error while loading cache file %s. Does not match "
		    "current cache config. "
		    "current numPtrsMax=%"INT32" maxMem=%"INT32", "
		    "ondisk  numPtrsMax=%"INT32" maxMem=%"INT32". "
		    "Attempting to convert.",
		    //"Ignoring file.",
		    f.getFilename() ,
		    m_numPtrsMax , m_maxMem ,
		      numPtrsMax ,   maxMem );
		//log("RdbCache::load: not loading");
		m_convert           = true;
		m_convertNumPtrsMax = numPtrsMax ;
		m_convertMaxMem     = maxMem     ;
		return false;
	}
	// mem stuff
	n = f.read ( &m_memAlloced  , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &m_memOccupied , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	// load the buffer stuff
	n = f.read ( &m_numBufs     , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &m_totalBufSize, 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &m_offset      , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &m_tail        , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &m_wrapped     , 1 , off ); off += 1;
	if ( n != 1 ) return false;
	// load each buf
	for ( int32_t i = 0 ; i < m_numBufs ; i++ ) {
		// load end relative
		//int32_t end ; //= (m_bufEnds[i] - m_bufs[i]);
		//n = f.read ( &end , 4 , off ); off += 4;
		//if ( n != 4 ) return false;
		// and buf size
		int32_t bufSize ; //= m_bufSizes[i];
		n = f.read ( &bufSize , 4 , off ); off += 4;
		if ( n != 4 ) return false;
		// alloc the buf
		char ttt[64];
		sprintf(ttt,"clb-%s",m_dbname);
		m_bufs[i] = (char *) mcalloc ( bufSize , ttt );
		if ( ! m_bufs[i] ) return false;
		m_bufSizes[i] = bufSize;
		//m_bufEnds [i] = m_bufs[i] + end;
		//if ( end < 0 ) m_bufEnds[i] = NULL;
		// then read contents of buffer #i
		n = f.read ( m_bufs[i] , bufSize , off ); off += bufSize;
		if ( n != bufSize ) return false;
	}
	// load the hash table stuff
	n = f.read ( &m_numPtrsUsed , 4 , off ); off += 4;
	if ( n != 4 ) return false;
	n = f.read ( &m_threshold   , 4 , off ); off += 4;
	if ( n != 4 ) return false;

	// load the OFFSETS into "fix"
	int32_t total =  sizeof(int32_t) * m_numPtrsMax ;
	SafeBuf fix;
	fix.reserve ( total );
	//n = f.read ( m_ptrs , total , off ); off += total;
	n = f.read ( fix.getBufStart() , total , off ); off += total;
	if ( n != total ) return false;
	fix.setLength ( total );
	int32_t *poff = (int32_t *)fix.getBufStart();

	// ptrs can be 8 bytes each, if we are 64-bit
	m_ptrs = (char **) mcalloc (m_numPtrsMax * sizeof(char *),m_dbname);
	if ( ! m_ptrs ) return false;


	int32_t used = 0;

	// convert offsets into pointers
	for ( int32_t i = 0 ; i < m_numPtrsMax ; i++ , poff++ ) {
		//uint32_t j = (SPTRTYPE) m_ptrs[i];
		// is it a NULL?
		//if ( j == -1 ) { m_ptrs[i] = NULL; continue; }
		if ( *poff == -1 ) { m_ptrs[i] = NULL; continue; }
		// sanity
		if ( *poff >= m_numBufs * BUFSIZE ) { char *xx=NULL;*xx=0;}
		// get buffer
		int32_t bufNum = (*poff) / BUFSIZE;
		char *p = m_bufs[bufNum] + (*poff) % BUFSIZE ;
		// re-assign
		m_ptrs[i] = p;
		// count it
		used++;
		// see what is there
		
		// debug msg
		//key_t kk = *(key_t *)p;
		//log("loaded k.n1=%"UINT32" k.n0=%"UINT64"",kk.n1,kk.n0);
		//if ( m_fixedDataSize || m_supportLists )
		//	log("loaded k.n1=%"UINT32" k.n0=%"UINT64" size=%"INT32"",
		//	    kk.n1,kk.n0, 20+*(int32_t *)(p+sizeof(key_t)+4));
		//else
		//	log("loaded k.n1=%"UINT32" k.n0=%"UINT64"", kk.n1,kk.n0);
	}
	if ( used != m_numPtrsUsed ) { 
		log("cache: error loading cache. %"INT32" != %"INT32""
		    , used , m_numPtrsUsed );
		return false;
	}
	m_needsSave = false;
	return true;
}

// remove a key range from the cache
void RdbCache::removeKeyRange ( collnum_t collnum ,
				char *startKey ,
				char *endKey ) {
	//int32_t n = (key.n0 + (uint64_t)key.n1)% m_numPtrsMax;
	// unused now!!
	int32_t n = hash32 ( startKey , m_cks ) % m_numPtrsMax;
	int32_t startn = n;
	// chain
	for ( ; n+1 != startn; n++ ) {
		// check for wrap
		if ( n >= m_numPtrsMax )
			n = 0;
		// make sure it's not null
		if ( !m_ptrs[n] )
			return;
		// check collection number
		if ( *(collnum_t *)(m_ptrs[n]) != collnum )
			continue;
		// check the range
		if ( KEYCMP ( m_ptrs[n]+sizeof(collnum_t),
			      startKey,
			      m_cks ) >= 0 &&
		     KEYCMP ( m_ptrs[n]+sizeof(collnum_t),
			      endKey,
			      m_cks ) <= 0 ) {
			// remove the key
			int32_t rem = n;
			m_ptrs[rem] = NULL;
			m_numPtrsUsed--;
			m_memOccupied -= sizeof(char *);
			if ( ++rem >= m_numPtrsMax ) rem = 0;
			// keep looping until we hit an empty slot
			while ( m_ptrs[rem] ) {
				char *ptr = m_ptrs[rem];
				m_ptrs[rem] = NULL;
				m_numPtrsUsed--;
				m_memOccupied -= sizeof(char *);
				char k[MAX_KEY_BYTES];
				KEYSET(k,ptr+sizeof(collnum_t),m_cks);
				addKey ( *(collnum_t *)ptr ,
			 		 k   ,
			 		 ptr );
				if ( ++rem >= m_numPtrsMax ) rem = 0;
			}
		}
	}
	m_needsSave = true;
}

bool RdbCache::convertCache ( int32_t numPtrsMax , int32_t maxMem ) {
	// divide numPtrsMax by 2 to get maxRecs (see above)
	int32_t maxRecs = numPtrsMax / 2;
	// load the cache stored on disk into the "tmp" cache
	RdbCache tmp;
	if ( ! tmp.init ( maxMem          ,
			  m_fixedDataSize ,
			  m_supportLists  ,
			  maxRecs         ,
			  m_useHalfKeys   ,
			  m_dbname        ,
			  true            , // loadFromDisk
			  m_cks           , // cacheKeySize
			  m_dks           , // dataKeySize
			  numPtrsMax      ))
		return false;
	// load it from disk
	//if ( ! tmp.load() ) return false;
	// copy its recs into our space
	int32_t failed  = 0;
	int32_t success = 0;
	char key[16];
	for ( int32_t i = 0 ; i < tmp.m_numPtrsMax ; i++ ) {
		// get ptr to slot in hash table
		char *p = tmp.m_ptrs[i];
		// skip if empty bucket
		if ( ! p ) continue;
		// otherwise, get collnum
		collnum_t collnum = *(collnum_t *)p;
		// get key
		gbmemcpy ( key , p + sizeof(collnum_t), m_cks );
		// now get the record proper
		bool  found;
		char *rec;
		int32_t  recSize;
		int32_t  timestamp;
		found = tmp.getRecord ( collnum    ,
					key        ,
					&rec       ,
					&recSize   ,
					false      , // do copy?
					-1         , // maxAge
					false      , // inc counts?
					// when it was cached
					(time_t *)&timestamp , 
					false      );// promote rec?
		// sanity check
		if ( ! found ) {
			log("db: key is in hash table but no rec.");
			continue;
		}
		if ( ! timestamp ) {
			log("db: has a timestamp of 0");
		}
		// now add it to our table
		bool status;
		status = addRecord ( collnum   ,
				     key       ,
				     rec       ,
				     recSize   ,
				     timestamp );
		if   ( ! status ) failed++;
		else              success++;
	}
	// log it
	logf(LOG_INFO,"db: Successfully converted %"INT32" recs from cache on disk "
	     "for %s.", success,m_dbname);
	if ( failed > 0 )
		logf(LOG_INFO,"db: Failed to convert %"INT32" recs from cache on "
		     "disk for %s.", failed,m_dbname);
	return true;
}

// goes through all the pointers and checks the integrity of the data they 
// point to. Also checks if m_tail is pointing right or not
void RdbCache::verify(){
	 bool foundTail = false;
	 int32_t count = 0;
	 logf(LOG_DEBUG,"db: cachebug: verifying");
	 for ( int32_t i = 0; i < m_numPtrsMax; i++ ){
		 char *start = m_ptrs[i];
		 if ( !start ) continue;
		 if ( start == m_bufs[0] + m_tail )
			 foundTail = true;
		 char *p      = start;
		 // get collnum
		 collnum_t collnum = *(collnum_t *)p; p += sizeof(collnum_t);
		 // -1 this means cleared! set in RdbCache::clear(collnum_t)
		 // collnum can be 0 in case we have to go to next buffer
		 if ( collnum != 0 && ( collnum >= m_maxColls || collnum <-1)){
			 //	!g_collectiondb.m_recs[collnum] ) ) {
			 char *xx = NULL; *xx = 0;
		 }
	
		 // get key
		 //char *k = p ; 
		 p += m_cks;
		 // get time stamp
		 //int32_t  timestamp = *(int32_t  *)p ; 
		 p += 4;
	
		 //logf(LOG_DEBUG, "db: cachebug: removing key.  tail=%"INT32" ", 
		 //     m_tail);
		 
		 // get data size
		 int32_t dataSize;
		 // get dataSize and data
		 if ( m_fixedDataSize == -1 || m_supportLists ) {
			 dataSize = *(int32_t *)p; p += 4; }
		 else 	
			 dataSize = m_fixedDataSize;
		 
		 // sanity
		 if ( dataSize < 0 || dataSize > m_totalBufSize ){
			 char *xx = NULL; *xx = 0;
		 }
		 // count it
		 count++;
	 }
	 if ( !foundTail && m_wrapped ){
		 char *xx = NULL; *xx = 0 ;
	 }
	 if ( count != m_numPtrsUsed ) {
		 char *xx = NULL; *xx = 0 ;
	 }
}
