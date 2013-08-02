// Matt Wells, copyright Jun 2001

// . db of XmlDocs

#ifndef _TITLEDB_H_
#define _TITLEDB_H_

// how many bits is our docId? (4billion * 64 = 256 billion docs)
#define NUMDOCIDBITS 38
#define DOCID_MASK   (0x0000003fffffffffLL)
#define MAX_DOCID    DOCID_MASK

// replace m_docQuality and m_prevQuality with
// m_siteNumInlinks, m_sitePop and m_prevSiteNumInlinks
//#define TITLEREC_CURRENT_VERSION 114
// fix bug with badly serialized tagrecs in ptr_tagRec
//#define TITLEREC_CURRENT_VERSION 118
// add new link stats into LinkInfo
//#define TITLEREC_CURRENT_VERSION 119
#define TITLEREC_CURRENT_VERSION 120

#include "Rdb.h"
#include "Url.h"
#include "Conf.h"
#include "Xml.h"
#include "DiskPageCache.h"

// new key format:
// . <docId>     - 38 bits
// . <urlHash48> - 48 bits  (used when looking up by url and not docid)
// . <delBit>    -  1 bit

class Titledb {

 public:

	// reset rdb
	void reset();

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	// init m_rdb
	bool init ();

	// init secondary/rebuild titledb
	bool init2 ( long treeMem ) ;

	// . get the probable docId from a url/coll
	// . it's "probable" because it may not be the actual docId because
	//   in the case of a collision we pick a nearby docId that is 
	//   different but guaranteed to be in the same group/cluster, so you 
	//   can be assured the top 32 bits of the docId will be unchanged
	unsigned long long getProbableDocId ( Url *url , bool mask = true ) {
		unsigned long long probableDocId = hash64b(url->getUrl(),0);
		// Linkdb::getUrlHash() does not mask it
		if ( mask ) probableDocId = probableDocId & DOCID_MASK;
		// clear bits 6-13 because we want to put the domain hash there
		// dddddddd dddddddd ddhhhhhh hhdddddd
		probableDocId &= 0xffffffffffffc03fULL;
		unsigned long h = hash8(url->getDomain(), url->getDomainLen());
		//shift the hash by 6
		h <<= 6;
		// OR in the hash
		probableDocId |= h;
		return probableDocId;
	};

	// a different way to do it
	unsigned long long getProbableDocId ( char *url  ) {
		Url u;
		u.set(url, gbstrlen(url));
		return getProbableDocId ( &u ); 
	};

	// a different way to do it
	unsigned long long getProbableDocId(char *url,char *dom,long domLen) {
		unsigned long long probableDocId = hash64b(url,0) & 
			DOCID_MASK;
		// clear bits 6-13 because we want to put the domain hash there
		probableDocId &= 0xffffffffffffc03fULL;
		unsigned long h = hash8(dom,domLen);
		//shift the hash by 6
		h <<= 6;
		// OR in the hash
		probableDocId |= h;
		return probableDocId;
	};

	// turn off the last 6 bits
	unsigned long long getFirstProbableDocId ( long long d ) {
		return d & 0xffffffffffffffc0LL; };

	// turn on the last 6 bits for the end docId
	unsigned long long getLastProbableDocId  ( long long d ) {
		return d | 0x000000000000003fLL; };

	// . the top NUMDOCIDBITs of "key" are the docId
	// . we use the top X bits of the keys to partition the records
	// . using the top bits to partition allows us to keep keys that
	//   are near each other (euclidean metric) in the same partition
	long long getDocIdFromKey ( key_t *key ) {
		unsigned long long docId;
		docId = ((unsigned long long)key->n1)<<(NUMDOCIDBITS - 32);
		docId|=                      key->n0 >>(64-(NUMDOCIDBITS-32));
		return docId;
	};
	long long getDocId ( key_t *key ) { return getDocIdFromKey(key); };
	long long getDocIdFromKey ( key_t  key ) {
		return getDocIdFromKey(&key);};

	uint8_t getDomHash8FromDocId (long long d) {
		return (d & ~0xffffffffffffc03fULL) >> 6; }

	long long getUrlHash48 ( key_t *k ) {
		return ((k->n0 >> 10) & 0x0000ffffffffffffLL); };

	// . dptr is a char ptr to the docid
	// . used by IndexTable2.cpp
	// . "dptr" is pointing into a 6-byte indexdb key
	// . see IndexTable2.cpp, grep for memcpy() to see
	//   how the docid is parsed out of this key (or see
	//   Indexdb.h)
	// . return  ((*((uint16_t *)dptr)) >> 8) & 0xff; }
	uint8_t getDomHash8 ( uint8_t *dptr ) { return dptr[1]; }

	// does this key/docId/url have it's titleRec stored locally?
	bool isLocal ( long long docId );
	bool isLocal ( Url *url ) {
		return isLocal ( getProbableDocId(url) ); };
	bool isLocal ( key_t key ) { 
		return isLocal (getDocIdFromKey(&key));};


	Rdb *getRdb() { return &m_rdb; }

	// . make the key of a TitleRec from a docId
	// . remember to set the low bit so it's not a delete
	// . hi bits are set in the key
	key_t makeKey ( long long docId, long long uh48, bool isDel );

	key_t makeFirstKey ( long long docId ) {
		return makeKey ( docId , 0, true ); };

	key_t makeLastKey  ( long long docId ) {
		return makeKey ( docId , 0xffffffffffffLL, false ); };

	// . this is an estimate of the number of docs in the WHOLE db network
	// . we assume each group/cluster has about the same # of docs as us
	long long getGlobalNumDocs ( ) { 
		return m_rdb.getNumTotalRecs()*
			(long long)g_hostdb.m_numGroups;};

	long getLocalNumDocs () { return m_rdb.getNumTotalRecs(); };
	long getNumDocsInMem () { return m_rdb.m_tree.getNumUsedNodes(); };
	long getMemUsed      () { return m_rdb.m_tree.getMemOccupied(); };

	// holds binary format title entries
	Rdb m_rdb;

	DiskPageCache *getDiskPageCache ( ) { return &m_pc; };

	DiskPageCache m_pc;
};

extern class Titledb g_titledb;
extern class Titledb g_titledb2;

#endif
