#include "gb-include.h"

#include "DiskPageCache.h"
#include "RdbCache.h"

// key = 24 bytes = 192 bits
// vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv v = vfd = unique file handle
// vvvvvvvv vvvvvvvv vvvvvvvv vvvvvvvv
// ffffffff ffffffff ffffffff ffffffff f = file read offset
// ffffffff ffffffff ffffffff ffffffff
// bbbbbbbb bbbbbbbb bbbbbbbb bbbbbbbb b = bytes to read
// bbbbbbbb bbbbbbbb bbbbbbbb bbbbbbbb


DiskPageCache::DiskPageCache () {
}

DiskPageCache::~DiskPageCache() {
	reset();
}

void DiskPageCache::reset() {
	m_rc.reset();
}

bool DiskPageCache::init ( const char *dbname ,
			   char rdbId,
			   int64_t maxMem  ,
			   int32_t pageSize ) {
	reset();

	snprintf(m_dbname,62,"pg-%s",dbname );
	m_pageSize = pageSize;
	m_rdbId = rdbId;
	m_enabled = true;

	// disable for now
	return true;

	return m_rc.init( maxMem ,
			  -1 , // fixedDataSize (-1->variable)
			  false , // supportlists?
			  // see RdbMap.h *PAGE_SIZE. it can be 1k or 32k so
			  // we pick 4k to be in the middle
			  maxMem / m_pageSize , // maxCacheNodes
			  false , // usehalfkeys?
			  m_dbname ,
			  false , // loadfromdisk?
			  24 , // 24 byte key
			  12 , // data key size?
			  -1 ); // num ptrs max

}

key192_t makePCKey ( int64_t vfd ,
		     int64_t offset ,
		     int64_t readSize ) {
	key192_t k;
	k.n2 = vfd;
	k.n1 = readSize;
	k.n0 = offset;
	return k;
}

char *DiskPageCache::getPages ( int64_t vfd , 
				int64_t offset , 
				int64_t readSize ) {
	if ( ! m_enabled ) return NULL;
	// disable for now
	return NULL;
	// make the key
	key192_t k = makePCKey ( vfd , offset , readSize );
	char *rec = NULL;
	int32_t recSize;
	bool found = m_rc.getRecord ( (collnum_t)0 , // collnum
				      (char *)&k ,
				      &rec ,
				      &recSize ,
				      true , // doCopy? for now, yes
				      -1 , // maxAge, none, permanent vfd
				      true , // incCounts - stats hits/misses
				      NULL , // cachedTimeptr
				      true ); // promote rec?
	if ( ! found ) return NULL;
	// sanity
	if ( recSize != readSize ) { char *xx=NULL;*xx=0; }
	return rec;
}

// returns true if successfully added to the cache, false otherwise
bool DiskPageCache::addPages ( int64_t vfd , 
			       int64_t offset , 
			       int64_t readSize ,
			       char   *buf ,
			       char    niceness ) {
	if ( ! m_enabled ) return false;
	// disable for now
	return true;
	// make the key
	key192_t k = makePCKey ( vfd , offset , readSize );
	time_t now = getTimeLocal();
	return m_rc.addRecord ( (collnum_t)0 , // collnum
				(char *)&k ,
				buf , 
				readSize ,
				NULL , // rec2
				0 , // recsize2
				now , //timestamp
				NULL ); // return rec ptr
}
