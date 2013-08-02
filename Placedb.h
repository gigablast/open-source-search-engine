// Copyright Matt Wells, Aug 2009

#ifndef _PLACEDB_H_
#define _PLACEDB_H_

#include "Rdb.h"
#include "Msg0.h"

class Placedb {

  public:

	bool init  ( ) ;
	bool init2 ( long treeMem ) ;

	// set up our private rdb
	Rdb *getRdb  ( ) { return &m_rdb; };

	bool addColl ( char *coll, bool doVerify = false );

	bool verify ( char *coll ) ;

	void reset();


	//
	// see Address::makePlacedbKey() to see how placedbKey is formed
	//

	// hash of street, adm1, city and street number
	long long getBigHash   ( key128_t *placedbKey ) {
		return placedbKey->n1; };

	long getStreetNumHash   ( key128_t *placedbKey ) {
		return placedbKey->n0 >> 39; };

	// docid is top 38 bits
	long long getDocId     ( key128_t *placedbKey ) {
		return  (placedbKey->n0 >> 1) & DOCID_MASK; };

	// the hash of the place name with the street indicator(s)
	//long getSmallHash ( key128_t *placedbKey ) {
	//	return (placedbKey->n0>>1)&0x1ffffff;};



	// this rdb holds urls waiting to be spidered or being spidered
	Rdb m_rdb;

	DiskPageCache *getDiskPageCache() { return &m_pc; };

	DiskPageCache m_pc;
};

extern class Placedb g_placedb;
extern class Placedb g_placedb2;

#endif
