// Matt Wells, copyright Jul 2001

// . get the number of records (termId/docId/score tuple) in an IndexList(s)
//   for a given termId(s)
// . if it's truncated then interpolate based on score
// . TODO: use g_conf.m_truncationLimit
// . used for query routing
// . used for query term weighting (IDF)
// . used to set m_termFreq for each termId in query in the Query class

#ifndef _MSG36_H_
#define _MSG36_H_

#include "UdpServer.h" // for sending/handling requests
#include "Multicast.h" // for sending requests
#include "Indexdb.h"
#include "Msg3.h"      // MAX_RDB_FILES definition
#include "RdbCache.h"

class Msg36 {

 public:

	// register our 0x36 handler function
	bool registerHandler ( );

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "termFreq" should NOT be on the stack in case we block
	// . sets *termFreq to UPPER BOUND on # of records with that "termId"
	bool getTermFreq ( collnum_t collnum,//char       *coll       ,
			   int32_t        maxAge     ,
			   int64_t   termId     ,
			   void       *state      ,
			   void (* callback)(void *state ) ,
			   int32_t        niceness = MAX_NICENESS ,
			   bool        exactCount  = false     ,
			   bool        incCount    = false     ,
			   bool        decCount    = false     ,
			   bool        isSplit     = true);

	int64_t getTermFreq () { return m_termFreq; };

	// public so C wrapper can call
	void gotReply ( ) ;

	// we store the recvd termFreq in what this points to
	int64_t  m_termFreq ;

	// info stored in us by Msg37.cpp
	void *m_this;
	int32_t  m_i;
	int32_t  m_j;

	// callback information
	void  *m_state  ;
	void (* m_callback)(void *state );

	// request buffer is just 8 bytes
	char m_request[1+8+MAX_COLL_LEN+1];

	// hold the reply now too
//#ifdef SPLIT_INDEXDB
//	char m_reply[8*INDEXDB_SPLIT];
//#else
//	char m_reply[8];
//#endif
	char m_reply[8*MAX_SHARDS];

	// for sending the request
//#ifdef SPLIT_INDEXDB
	//Multicast m_mcast[INDEXDB_SPLIT];
	Multicast m_mcast[1];//MAX_SHARDS];
	int32_t      m_numRequests;
	int32_t      m_numReplies;
	int32_t      m_errno;
	bool      m_isSplit;
//#else
//	Multicast m_mcast;
//#endif

	int32_t      m_niceness;
};

//extern class RdbCache g_qtable;

#endif
