// Matt Wells, copyright Jan 2002

// program to test Rdb

void *gohere ( void *);

#include "Rdb.h"
#include "Conf.h"
#include <pthread.h>

static Rdb rdb;

int main ( int argc , char *argv[] ) {


	// test merging of these lists
	RdbList list1, list2;

	char data1 [ 32 ] , data2[32];
	*(int64_t *)data1 = 273777931569279933;
	*(int64_t *)data2 = 273777931569279932;
	*(int32_t *)(data1 + 8) = 0;
	*(int32_t *)(data2 + 8) = 0;

	list1.set ( data1 , 16 , 16 , 4 , false );
	list2.set ( data2 , 16 , 16 , 4 , false );

	RdbList *ptrs[2];
	ptrs[0] = &list1;
	ptrs[1] = &list2;
	key_t startKey ;
	key_t endKey   ;
	startKey.n0 = 0LL;
	startKey.n1 = 0;
	endKey.setMax();

	RdbList final;
	char data3 [ 32];
	final.set ( data3 , 0 , 16 , 4 , true );

	final.prepareForMerge ( ptrs , 2 , 16 );

	final.merge_r ( ptrs , 2 , false , startKey , endKey , true , 16 );

	return 0;


	// set
	bool status = rdb.init ( "/tmp" ,
				 "testdb",
				 true     , // defup
				 0        , // fixed data size
				 600000   , // trunc limit
				 0        , // trunc mask
				 2        , // min files to merge
				 1000000  , // max tree mem = 1,000,000
				 20000    , // max # nodes in tree = 20,000
				 false    , // tree balanced?
				 10000    , // cache mem is 10k
				 1000     );// cache nodes is 1000
	if ( ! status ) {
		fprintf(stderr,"rdb init failed\n");
		exit(-1);
	}
	// set certain things in g_conf that rdb uses
	g_conf.m_numGroups       = 1;
	g_conf.m_mergeMaxBufSize = 100000;
	// fork to add to the rdb
	pthread_t t;
	pthread_create( &t , NULL /*attr*/, gohere, NULL);
	// parent runs the loop
	if ( ! g_loop.runLoop()    ) {
		fprintf(stderr,"main::runLoop failed" ); exit (-1); }
	// success
	return 0;
}

static key_t keys[50000];

void *gohere ( void *) {
	// make a bunch of random keys
	for ( int32_t i = 0 ; i < 25000 ; i++ ) {
		keys[i].n1 = rand();
		int32_t r = rand();
		keys[i].n0 = ((int64_t)r << 32) | rand();
		// for to be a positive key
		keys[i].n0 |= 0x01;
	}
	// make the negative counterparts in same order
	for ( int32_t i = 25000 ; i < 50000 ; i++ ) {
		keys[i] = keys[i-25000];
		// for to be a negative key
		keys[i].n0 &= 0xfffffffffffffffeLL;
	}
	// . add all the keys now to rdb
	// . use udp server to add it, msg1
	RdbList list;
	
	
	for ( int32_t i = 0 ; i < 50000 ; i++ ) {
		key_t k = keys[i];
		if ( (i % 1000) == 0 ) fprintf(stderr,"%"INT32"\n",i);
		if ( rdb.addRecord ( k, NULL ,0, false ) >= 0 ) continue;
		fprintf(stderr,"rdb::addRecord: %s\n",mstrerror(errno));
		exit(-1);
	}
	// force rdb to dump and merge
	//rdb.dumpTree();
	return NULL;
}
