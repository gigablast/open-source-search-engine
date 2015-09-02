// Matt Wells, Copyright Jan 2004-2015

// . now we just use RdbCache
// . when a BigFile is first opened we assign it a unique 'vfd' (virtual fd)
// . to make the rdbcache key we hash this vfd with the read offset and size

#ifndef PAGECACHE_H
#define PAGECACHE_H

#include "RdbCache.h"

class DiskPageCache {

 public:

	DiskPageCache();
	~DiskPageCache();
	void reset();

	// returns false and sets g_errno if unable to alloc the memory,
	// true otherwise
	bool init ( const char *dbname , 
		    char    rdbId , 
		    int64_t maxMem ,
		    int32_t pageSize );

	// . this returns true iff the entire read was copied into
	//   "buf" from the page cache
	// . it will move the used pages to the head of the linked list
	char *getPages ( int64_t  vfd , 
			 int64_t  offset , 
			 int64_t  readSize );

	// after you read/write from/to disk, copy into the page cache
	bool addPages  ( int64_t  vfd , 
			 int64_t  offset , 
			 int64_t  readSize ,
			 char    *buf , 
			 char     niceness );

	void enableCache () { m_enabled = true ; };
	void disableCache() { m_enabled = false; };
	bool m_enabled;

	int32_t m_pageSize;
	char m_rdbId;
	char m_dbname[64];

	RdbCache m_rc;

	int64_t getNumHits   () { return m_rc.getNumHits(); }
	int64_t getNumMisses () { return m_rc.getNumMisses(); }
	int64_t getMemUsed   () { return m_rc.getMemOccupied(); } 
	int64_t getMemAlloced() { return m_rc.getMemAlloced(); } 
};

#endif
