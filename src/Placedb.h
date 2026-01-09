// Copyright Matt Wells, Aug 2009

#ifndef _PLACEDB_H_
#define _PLACEDB_H_

#include "Rdb.h"
#include "Msg0.h"

class Placedb {

  public:

	bool init  ( ) ;
	bool init2 ( int32_t treeMem ) ;

	// set up our private rdb
	Rdb *getRdb  ( ) { return &m_rdb; };

	bool addColl ( char *coll, bool doVerify = false );

	bool verify ( char *coll ) ;

	void reset();


	//
	// see Address::makePlacedbKey() to see how placedbKey is formed
	//

	// hash of street, adm1, city and street number
	int64_t getBigHash   ( key128_t *placedbKey ) {
		return placedbKey->n1; };

	int32_t getStreetNumHash   ( key128_t *placedbKey ) {
		return placedbKey->n0 >> 39; };

	// docid is top 38 bits
	int64_t getDocId     ( key128_t *placedbKey ) {
		return  (placedbKey->n0 >> 1) & DOCID_MASK; };

	// the hash of the place name with the street indicator(s)
	//int32_t getSmallHash ( key128_t *placedbKey ) {
	//	return (placedbKey->n0>>1)&0x1ffffff;};



	// this rdb holds urls waiting to be spidered or being spidered
	Rdb m_rdb;

	//DiskPageCache *getDiskPageCache() { return &m_pc; };

	//DiskPageCache m_pc;
};

extern class Placedb g_placedb;
extern class Placedb g_placedb2;

#endif
