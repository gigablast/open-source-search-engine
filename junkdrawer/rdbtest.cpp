// Matt Wells, copyright Jan 2002

// program to test Rdb

#include "Rdb.h"
#include "Conf.h"
#include <pthread.h>

static Rdb rdb;

int main ( int argc , char *argv[] ) {

	g_mem.init ();
	g_log.init( "/tmp/logtest");
	
	// test merging of these lists
	RdbList list1, list2;

	char data1 [ 32 ] , data2[32];
	*(int64_t *)data1 = 273777931569279933;
	*(int64_t *)data2 = 273777931569279932;
	*(int32_t *)(data1 + 8) = 1059574105;
	*(int32_t *)(data2 + 8) = 1059574105;

	key_t startKey ;
	key_t endKey   ;
	startKey.n0 = 216172782113783808LL;
	startKey.n1 = 1059574105;
	endKey.n0   = 288230376151711743LL;
	endKey.n1   = 1059574105;
	list1.set ( data1 , 16 , 
		    16 , 
		    startKey , endKey ,
		    4 , false );

	startKey.n0 = 216172782113783808LL;
	startKey.n1 = 1059574105;
	endKey.n0   = 273777931569279932LL;
	endKey.n1   = 1059574105;

	list2.set ( data2 , 16 , 
		    16 , 
		    startKey , endKey ,
		    4 , false );

	RdbList *ptrs[2];
	ptrs[0] = &list1;
	ptrs[1] = &list2;

	startKey.n0 = 216172782113783808LL;
	startKey.n1 = 1059574105;
	endKey.n0   = 273777931569279932LL;
	endKey.n1   = 1059574105;

	RdbList final;
	char *data3 = (char *)mmalloc ( 32 , "rdbtest");
	final.set ( data3 , 0 , 32 , 4 , true );

	final.prepareForMerge ( ptrs , 2 , 16 );

	final.merge_r ( ptrs , 2 , false , startKey , endKey , true , 16 );

	log("final listsize = %"INT32"", final.getListSize() );

	return 0;
}
