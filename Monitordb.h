// Matt Wells Copyright April 2013

// Monitordb - a semi-permanent monitor for storing seo safebufs

// . Format of a 12-byte key in monitordb
// .
// . HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH H = hash of the url
// . HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH H = hash of the url
// . tttttttt 00000000 00000000 00000000 t = type of object

#ifndef _MONITORDB_H_
#define _MONITORDB_H_

// 12 byte key size
#define MONITORDBKS sizeof(key96_t)

#include "Rdb.h"
//#include "DiskPageCache.h"

class Monitordb {
 public:
	void reset();

	bool init    ( );
	bool init2 ( int32_t treeMem );
	bool verify  ( char *coll );
	bool addColl ( char *coll, bool doVerify = true );


	Rdb           *getRdb()           { return &m_rdb; };

	//DiskPageCache *getDiskPageMonitor () { return &m_pc; };
	//DiskPageCache m_pc;

 private:
	Rdb           m_rdb;

};

extern class Monitordb g_monitordb;

#endif
