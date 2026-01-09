#include "gb-include.h"

#include "RdbList.h"
#include "Mem.h"

void test0 ( int arg ) ;

bool mainShutdown ( bool urgent ) { return true; }

int main ( int argc , char *argv[] ) {
	if ( argc != 2 ) {
		fprintf(stderr,"mergetest: minRecSizes\n");
		return -1;
	}
	test0 ( atoi ( argv[1] ) );
}

#include "IndexTable.h"

void test0 ( int arg ) {

	IndexList lists[MAX_TIERS][MAX_QUERY_TERMS];

	key_t startKey;
	key_t endKey;
	startKey.n1 = 0;
	startKey.n0 = 0LL;
	endKey.n1   = 0xffffffff;
	endKey.n0   = 0xffffffffffffffffLL;

	g_mem.init ( 1024*1024*3);

	char *qs = "ab bc cd de ef";
	Query q;
	q.set ( qs , gbstrlen(qs) , NULL , 0 , false );

	int32_t numLists = q.getNumTerms();

	// how many keys per list?
	int32_t nk = 100000;

	printf("intersecting %"INT32" keys total from %"INT32" lists\n", 
	       nk*numLists , numLists );

	for ( int32_t i = 0 ; i < numLists ; i++ ) {
		printf("loading list #%"INT32"\n",i);
		RdbList *list = &lists[0][i]; 
		// make a list of compressed (6 byte) docIds
		key_t *keys = (key_t *) malloc ( 12 + 6 * nk );
		// point to keys
		char *kp = (char *)keys;
		// store radnom docIds in this list
		char *p = kp;
		// value
		int64_t value = 0LL;
		// first key is 12 bytes
		key_t firstKey;
		firstKey.n0 = 1LL;
		firstKey.n1 = 0;
		gbmemcpy ( p , &firstKey , 12 );
		p += 12;
		// random docIds
		for ( int32_t i = 0 ; i < nk ; i++ ) {
			int32_t toAdd = rand() % 65536 + 2;
			value += toAdd;
			*(char *)&value |= 0x03;
			gbmemcpy ( p , &value , 6 );
			p += 6;
		}
		// sort em up
		//gbsort ( keys , nk , sizeof(key_t) , cmp );
		// set the list
		int32_t listSize = p - (char *)keys;
		list->set ( kp       ,
			    listSize ,
			    kp       ,
			    listSize ,
			    startKey ,
			    endKey   ,
			    0        , // fixedDataSize
			    true     , // own data?
			    true     );// use half keys?
	}

	IndexTable table;
	table.init ( &q , false , NULL );
	table.prepareToAddLists();

	printf("beginning intersection\n");

	int64_t startTime = gettimeofdayInMilliseconds();

	table.addLists_r ( lists    ,
			   1        , // num tiers
			   numLists , // lists per tier
			   15       );// docs wanted

	int64_t now = gettimeofdayInMilliseconds();
	printf("intersection took %"UINT64" ms\n" , now - startTime );

	log("addLists_r: took %"INT64" ms docids=%"UINT32" "
	    "panics=%"INT32" chains=%"INT32" ptrs=%"INT32" loops=%"INT32".",
	    table.m_addListsTime  ,
	    table.m_totalDocIds   ,
	    table.m_numPanics     ,
	    table.m_numCollisions ,
	    table.m_numPtrs       ,
	    table.m_numLoops      );
}
