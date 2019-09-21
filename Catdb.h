// Matt Wells, copyright Feb 2001

// . catdb record format:
// 	. number of catids (1 byte)
// 	. list of catids   (4 bytes each)
// 	. tagdb file #    (3 bytes)
// 	. tagdb version # (1 byte)
// 	. siteUrl (remaining bytes)

// . record key:  
// . dddddddd dddddddd dddddddd dddddddd  d = domain hash (w/o collection)
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  u = special url hash
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  

#ifndef _CATDB_H_
#define _CATDB_H_

#define CATREC_CURRENT_VERSION 6

#include "Conf.h"       // for setting rdb from Conf file
#include "Rdb.h"
#include "Url.h"
#include "Loop.h"
//#include "DiskPageCache.h"
//#include "CollectionRec.h"

class Catdb  {

 public:
	// reset rdb
	void reset();

	// . TODO: specialized cache because to store pre-parsed tagdb recs
	// . TODO: have m_useSeals parameter???
	bool init  ( );
	bool init2 ( int32_t treeMem );
	
	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	// . used by ../rdb/Msg0 and ../rdb/Msg1
	Rdb      *getRdb   ( ) { return &m_rdb; };

	// calls getKeys and gets the top key
	key_t makeKey ( Url *site , bool isDelete );

	// binary search on the given list for the given key
	void listSearch ( RdbList *list,
			  key_t    exactKey,
			  char   **data,
			  int32_t    *dataSize );


	// . get the serialized SiteRec from an RdbList of SiteRecs
	//   that is the best match for "url"
	char *getRec ( RdbList *list , Url *url , int32_t *recSize ,char* coll, 
		       int32_t collLen ) ;

	// . find the indirect matches in the list which match a sub path
	//   of the url
	int32_t  getIndirectMatches ( RdbList  *list ,
				   Url      *url ,
				   char    **matchRecs ,
				   int32_t     *matchRecSizes ,
				   int32_t      maxMatches,
				   char     *coll,
				   int32_t      collLen );

	// . get the keys of all the possible site records for this url
	// . see below for the search order of the sub-urls
	// . if "useIp" is true we use the ip of "url" to form the key range,
	//   not the cannoncial domain name
	void getKeyRange ( bool useIp , Url *url , 
			   key_t *startKey , key_t *endKey );

	//DiskPageCache *getDiskPageCache() { return &m_pc; };

	// normalize a url, no www.
	void normalizeUrl ( Url *srcUrl, Url *dstUrl );

	//int32_t getGroupId ( key_t *key ) {
	//	return key->n1 & g_hostdb.m_groupMask;
	//}

 private:
	// for doing binary search on the list
	char *moveToCorrectKey ( char *listPtr,
				 RdbList *list,
				 uint32_t domainHash );

	// . we use the cache in here also for caching tagdb records
	//   and "not-founds" stored remotely (net cache)
	Rdb   m_rdb;

	//DiskPageCache m_pc;

};

extern class Catdb  g_catdb;

#endif
