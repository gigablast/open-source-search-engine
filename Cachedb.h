// Matt Wells Copyright April 2013

// Cachedb - a semi-permanent cache for storing seo safebufs

// . so if they click on a different insertable term we can quickly
//   send them the wordposinfos with WordPosInfo::m_trafficGain set.

// . Format of a 12-byte key in cachedb
// .
// . HHHHHHHH HHHHHHHH HHHHHHHH HHHHHHHH H = hash of the url
// . cccccccc cccccccc cccccccc cccccccc c = hash of the content
// . tttttttt 00000000 00000000 00000000 t = type of object

#ifndef _CACHEDB_H_
#define _CACHEDB_H_

// 12 byte key size
#define CACHEDBKS sizeof(key96_t)

#include "Rdb.h"
//#include "DiskPageCache.h"

// do not change these numbers, they are permanent and stored in cachedb
// that way... just add new numbers to the end.
enum {
	cr_MatchingQueries = 0,
	cr_RelatedDocIds   = 1,
	cr_RelatedQueries  = 2,
	cr_ScoredInsertableTerms = 3,
	cr_WordPosInfoBuf = 4,
	cr_Msg25SiteInfo = 5,
	cr_Msg25PageInfo = 6,
	cr_RecommendedLinks = 7,
	cr_MissingTermBuf = 8
};

class Cachedb {
 public:
	void reset();

	bool init    ( );
	bool init2 ( int32_t treeMem );
	bool verify  ( char *coll );
	bool addColl ( char *coll, bool doVerify = true );

	char getTypeFromKey ( char *key ) { return key[3]; }

	// url and content hashes are the args
	key_t makeStartKey ( int32_t uh32 , int32_t ch32 ) {
		key_t k;
		k.n1 = (uint32_t)uh32;
		// clear hi bit, set hi bit when storing query results
		// not not these
		//k.n1 &= 0x7fffffff;
		k.n0 = (uint32_t)ch32;
		k.n0 <<= 32;
		// del key
		k.n0 &= 0xfffffffffffffffeLL;
		return k;
	};

	key_t makeEndKey ( int32_t uh32 , int32_t ch32 ) {
		key_t k;
		k.n1 = (uint32_t)uh32;
		// clear hi bit, set hi bit when storing query results
		// not not these
		//k.n1 &= 0x7fffffff;
		k.n0 = (uint32_t)ch32;
		// max object type i guess
		k.n0 <<= 8;
		k.n0 |= 0xff;
		k.n0 <<= 24;
		// not a del key
		k.n0 |= 0x01;
		return k;
	};

	key_t makeStartKey2 ( int32_t uh32 , int32_t ch32 , int32_t objectType ) {
		key_t k = makeKey ( uh32 , ch32 , objectType );
		// del key
		k.n0 &= 0xfffffffffffffffeLL;
		return k;
	};

	key_t makeEndKey2 ( int32_t uh32 , int32_t ch32 , int32_t objectType ) {
		key_t k = makeKey ( uh32 , ch32 , objectType );
		// not a del key
		k.n0 |= 0x01;
		return k;
	};

	key_t makeKey ( int32_t uh32 , int32_t ch32 , uint8_t objectType ) {
		key_t k;
		k.n1 = (uint32_t)uh32;
		// clear hi bit, set hi bit when storing query results
		// not not these
		//k.n1 &= 0x7fffffff;
		k.n0 = (uint32_t)ch32;
		k.n0 <<= 8;
		k.n0 |= objectType; // 1 byte
		k.n0 <<= 24;
		// not a del key
		k.n0 |= 0x01;
		return k;
	};

	char *m_name;
	char  m_rdbId;

	Rdb           *getRdb()           { return &m_rdb; };

	//DiskPageCache *getDiskPageCache () { return &m_pc; };
	//DiskPageCache m_pc;

 private:
	Rdb           m_rdb;

};

extern class Cachedb g_cachedb;
extern class Cachedb g_serpdb;

#endif
