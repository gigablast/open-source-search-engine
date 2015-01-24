// Matt Wells, copyright Jan 2002

// program to test the RdbTree

#include "RdbTree.h"
#include "sort.h"

static int cmp ( const void *h1 , const void *h2 ) ;

bool mainShutdown ( bool urgent ) { return true; }

int32_t speedtest ( int32_t numNodes , bool balanced ) ;
int32_t sanitytest () ;

int main ( int argc , char *argv[] ) {
	g_mem.init ( 1000000000 );
	int32_t n = 15000*2 ;
	bool balanced = false;
	if ( argc > 1 ) n = atoi ( argv[1] );
	if ( argc > 2 ) balanced = (bool) atoi ( argv[2] );
	// return sanitytest();
	return speedtest ( n , balanced );
}

int32_t speedtest ( int32_t numNodes , bool balanced ) {

	//int32_t numNodes = 15000*2;
	char *bs = "";
	if ( ! balanced ) bs = "UN";
	log("






























making %sbalanced tree with %"INT32" nodes",bs,numNodes);
	RdbTree t;
	if ( ! t.set ( 0             ,    // fixedDataSize
		       numNodes      ,    // maxTreeNodes   
		       false         ,    // isTreeBalanced
		       numNodes*35   ,    // maxmem 
		       false         )) { // own data?
		fprintf(stderr,"init error\n");
		return -1;
	}
	// make random keys
	key_t *keys = (key_t *) malloc ( sizeof(key_t) * numNodes );
	if ( ! keys ) {
		fprintf(stderr,"malloc error\n");
		return -1;
	}
	for ( int32_t i = 0 ; i < numNodes ; i++ ) {
		key_t k;
		k.n1 = rand() ;
		k.n0 = rand();
		k.n0 <<= 32;
		k.n0 |= rand();
		keys[i] = k;
	}
	log("generated %"INT32" random keys",numNodes);


	//char *data = (char *)mmalloc ( numNodes * sizeof(key_t), "treetest");
	//char *pp = data;
	//if ( ! data ) { log("shit"); return -1; }
	int64_t t1 = gettimeofdayInMilliseconds();
	for ( int32_t i = 0 ; i < numNodes ; i++ ) {
		t.addNode ( keys[i] , NULL , 0 ); // , pp , 5*1024 );
		//pp += 5*1024;
	}
	int64_t t2 = gettimeofdayInMilliseconds();
	log("added %"INT32" keys to tree in %"INT64" ms",  numNodes , t2 - t1 );


	// now get the list
	t1 = gettimeofdayInMilliseconds();

	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.n0 = 0LL; startKey.n1 = 0;
	endKey.setMax();
	int32_t xx;
	if ( ! t.getList ( startKey ,
			   endKey   ,
			   1*1024*1024 ,
			   &list    ,
			   &xx      ,     // num negative keys
			   false    ) ) { // use half keys?
		log("problem: %s",mstrerror(g_errno));
		return -1;
	}
	t2 = gettimeofdayInMilliseconds();
	log("got list of %"INT32" bytes (%"INT32" keys) in %"INT64" ms", 
	    list.getListSize(),numNodes,t2-t1);
	return 0;
}

int32_t sanitytest () {
	RdbTree t;
	if ( ! t.set ( 0 , //fixedDataSize  , 
		       1500000 , // maxTreeNodes   
		       false , //isTreeBalanced , 
		       1500000*12*2 ,// maxmem 
		       false        )) { // own data?
		fprintf(stderr,"init error\n");
		return -1;
	}
	key_t *keys = (key_t *) malloc ( sizeof(key_t) * 1505000 );
	if ( ! keys ) {
		fprintf(stderr,"malloc error\n");
		return -1;
	}
	int32_t p = 0;
 loop:
	fprintf(stderr,"pass=%"INT32"\n",p++);
	// make random keys
	for ( int32_t i = 0 ; i < 1505000 ; i++ ) {
		key_t k;
		k.n1 = rand() ;
		k.n0 = rand();
		k.n0 <<= 32;
		k.n0 |= rand();
		keys[i] = k;
	}
	// add em
	for ( int32_t i = 0 ; i < 1500000 ; i++ ) 
		if ( t.addNode ( keys[i] , NULL , 0) < 0 ) {
			fprintf(stderr,"add error\n");
			return -1;
		}
	// sort 'em
	gbsort (keys , 1500000 , sizeof(key_t) , cmp );
	// delete em in order now
	for ( int32_t i = 0 ; i < 750000 ; i++ ) 
		if ( t.deleteNode ( keys[i] , true ) < 0 ) {
			fprintf(stderr,"node not found\n");
			return -1;
		}
	// add 5000 nodes
	for ( int32_t i = 1500000 ; i < 1505000 ; i++ ) 
		if ( t.addNode ( keys[i] , NULL , 0) < 0 ) {
			fprintf(stderr,"add error\n");
			return -1;
		}	
	// delete remaining 750000
	for ( int32_t i = 750000 ; i < 1500000 ; i++ ) 
		if ( t.deleteNode ( keys[i] , true ) < 0 ) {
			fprintf(stderr,"node not found\n");
			return -1;
		}
	// test tree
	//t.printTree();
	t.clear();
	goto loop;
	return 0;
}

int cmp ( const void *h1 , const void *h2 ) {
	if ( *(key_t *)h1 < *(key_t *)h2 ) return -1;
	if ( *(key_t *)h1 > *(key_t *)h2 ) return  1;
	return 0;
}
