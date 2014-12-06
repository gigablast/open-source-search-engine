// Copyright May 2007, Gigablast, Inc.

#ifndef _REQUESTTABLE_H_
#define _REQUESTTABLE_H_

#include "gb-include.h"
#include "HashTableT.h"

class RequestTable {
 public:

	RequestTable ( ) ;
	~RequestTable ( ) ;
	void reset ( ) ;

	// returns number of requests in the table with this hash
	int32_t addRequest ( int64_t  requestHash , void *state2 );

	void gotReply   ( int64_t  requestHash ,
			  char      *reply       ,
			  int32_t       replySize   ,
			  void      *state1      ,
			  void     (*callback)(char *reply     ,
					       int32_t  replySize ,
					       void *state1    ,
					       void *state2    ) );

	void cancelRequest ( int64_t requestHash , void *state2 );
	// . key of each slot is "requestHash"
	// . value of each slot is "state2" from call to addRequest above
	//HashTableT <int64_t,int32_t> m_htable;
	HashTableX m_htable;
	
	// . hash table buffer
	char m_buf[HT_BUF_SIZE];
	int32_t m_bufSize;

	// what hash are we processing
	int64_t m_processHash;
};

#endif
