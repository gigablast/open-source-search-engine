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

	long numLists = q.getNumTerms();

	// how many keys per list?
	long nk = 100000;

	printf("intersecting %li keys total from %li lists\n", 
	       nk*numLists , numLists );

	for ( long i = 0 ; i < numLists ; i++ ) {
		printf("loading list #%li\n",i);
		RdbList *list = &lists[0][i]; 
		// make a list of compressed (6 byte) docIds
		key_t *keys = (key_t *) malloc ( 12 + 6 * nk );
		// point to keys
		char *kp = (char *)keys;
		// store radnom docIds in this list
		char *p = kp;
		// value
		long long value = 0LL;
		// first key is 12 bytes
		key_t firstKey;
		firstKey.n0 = 1LL;
		firstKey.n1 = 0;
		memcpy ( p , &firstKey , 12 );
		p += 12;
		// random docIds
		for ( long i = 0 ; i < nk ; i++ ) {
			long toAdd = rand() % 65536 + 2;
			value += toAdd;
			*(char *)&value |= 0x03;
			memcpy ( p , &value , 6 );
			p += 6;
		}
		// sort em up
		//gbsort ( keys , nk , sizeof(key_t) , cmp );
		// set the list
		long listSize = p - (char *)keys;
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

	long long startTime = gettimeofdayInMilliseconds();

	table.addLists_r ( lists    ,
			   1        , // num tiers
			   numLists , // lists per tier
			   15       );// docs wanted

	long long now = gettimeofdayInMilliseconds();
	printf("intersection took %llu ms\n" , now - startTime );

	log("addLists_r: took %lli ms docids=%lu "
	    "panics=%li chains=%li ptrs=%li loops=%li.",
	    table.m_addListsTime  ,
	    table.m_totalDocIds   ,
	    table.m_numPanics     ,
	    table.m_numCollisions ,
	    table.m_numPtrs       ,
	    table.m_numLoops      );
}
