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
	long addRequest ( long long  requestHash , void *state2 );

	void gotReply   ( long long  requestHash ,
			  char      *reply       ,
			  long       replySize   ,
			  void      *state1      ,
			  void     (*callback)(char *reply     ,
					       long  replySize ,
					       void *state1    ,
					       void *state2    ) );

	void cancelRequest ( long long requestHash , void *state2 );
	// . key of each slot is "requestHash"
	// . value of each slot is "state2" from call to addRequest above
	HashTableT <long long,long> m_htable;
	
	// . hash table buffer
	char m_buf[HT_BUF_SIZE];
	long m_bufSize;

	// what hash are we processing
	long long m_processHash;
};

#endif
