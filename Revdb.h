// Matt Wells, copyright Jun 2001

// . db of metalists used to delete a doc now

#ifndef _REVDB_H_
#define _REVDB_H_

#include "Rdb.h"
#include "Url.h"
#include "Conf.h"
#include "Xml.h"
#include "Titledb.h"

// new key format:
// . <docId>     - 38 bits
// . <delBit>    -  1 bit

// data format:
// . a metalist that is passed in to Msg4

class Revdb {

 public:

	// reset rdb
	void reset();

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	// init m_rdb
	bool init ();

	// init secondary/rebuild revdb
	bool init2 ( int32_t treeMem ) ;

	// like titledb basically
	key_t makeKey ( int64_t docId , bool del ) ;

	int64_t getDocId ( key_t *k );

	Rdb *getRdb() { return &m_rdb; };

	// holds binary format rev entries
	Rdb m_rdb;
};

extern class Revdb g_revdb;
extern class Revdb g_revdb2;

#endif
