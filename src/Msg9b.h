// Matt Wells, copyright Feb 2001

// . a network interface to Catdb.h
// . handles add/delete requests for catdb records
// . use the all powerful ../rdb/Msg1.h class to add lists or records to an rdb

#ifndef _MSG9B_H_
#define _MSG9B_H_

#include "Msg1.h"    // add an RdbList to an rdb
#include "RdbList.h"
#include "CatRec.h"
#include "Msg8b.h"

class Msg9b {

 public:
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "urls" is a NULL-terminated list of space-separated urls
	// . makes a siteRec for each url in "urls" and adds it to an RdbList
	// . then adds the records in the RdbList to their appropriate 
	//   host/tagdb using msg1
	bool addCatRecs  ( char *urls        ,
			   char *coll        , 
			   int32_t  collLen     ,
			   int32_t  filenum     ,
			   void *state       ,
			   void (*callback)(void *state) ,
			   unsigned char *numCatids ,
			   int32_t *catids    ,
			   int32_t niceness = MAX_NICENESS ,
			   bool deleteRecs = false );

	// use this to convey our data
	RdbList m_list;

	// used to add our assembled list
	Msg1 m_msg1;

	// used to keep track of calling class
	void *m_parent;

	//we use these to store the passed in values from update
	Url   m_url;
	char *m_coll;
	int32_t  m_collLen;
	int32_t  m_filenum;
	void *m_state;              
	void (*m_callback)(void *state);
	unsigned char *m_numCatids;
	int32_t *m_catids;
	int32_t m_niceness;
};


#endif
