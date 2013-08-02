// Copyright Matt Wells, Apr 2000

#ifndef _TFNDB_H_
#define _TFNDB_H_

#include "Rdb.h"
#include "Url.h"
#include "Conf.h"
#include "Titledb.h"
#include "DiskPageCache.h"

// . we now use "tfndb" *only* to map docids to a title file #

// . a key in tfndb is 96 bits (compressed) and has this format:
// . <docId>       38 bits
// . <urlHash48>   48 bits
// . <tfn>          8 bits
// . <halfbit>      1 bit
// . <delbit>       1 bit

class Tfndb {

  public:

	// reset rdb
	void reset();

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );
	
	// set up our private rdb
	bool init ( );

	// init the rebuild/secondary rdb, used by PageRepair.cpp
	bool init2 ( long treeMem );

	Rdb *getRdb  ( ) { return &m_rdb; };

	key_t makeKey (long long docId, long long uh48,long tfn,bool isDelete);

	key_t makeMinKey ( long long docId ) {
		return makeKey ( docId ,0, 0 , true ); };

	key_t makeMaxKey ( long long docId ) {
		return makeKey ( docId , 0x0000ffffffffffffLL,0xff, false ); };

	long long getDocId ( key_t *k ) { 
		long long d = k->n1;
		d <<= 6;
		d |= k->n0>>58;
		return d;
	};

	long getTfn ( key_t *k ) { return ((k->n0) >>2) & 0xff; };

	long long getUrlHash48 ( key_t *k ) {
		return ((k->n0>>10) & 0x0000ffffffffffffLL); };

	DiskPageCache *getDiskPageCache() { return &m_pc; };

  private:

	// this rdb holds urls waiting to be spidered or being spidered
	Rdb m_rdb;

	DiskPageCache m_pc;

};

extern class Tfndb g_tfndb;
extern class Tfndb g_tfndb2;

#endif
