// Matt Wells, copyright Jan 2002

// program to test the RdbTree

#include "RdbTree.h"
#include "sort.h"

static int cmp ( const void *h1 , const void *h2 ) ;

bool mainShutdown ( bool urgent ) { return true; }

long speedtest ( long numNodes , bool balanced ) ;
long sanitytest () ;

int main ( int argc , char *argv[] ) {
	g_mem.init ( 1000000000 );
	long n = 15000*2 ;
	bool balanced = false;
	if ( argc > 1 ) n = atoi ( argv[1] );
	if ( argc > 2 ) balanced = (bool) atoi ( argv[2] );
	// return sanitytest();
	return speedtest ( n , balanced );
}

long speedtest ( long numNodes , bool balanced ) {

	//long numNodes = 15000*2;
	char *bs = "";
	if ( ! balanced ) bs = "UN";
	log("






























making %sbalanced tree with %li nodes",bs,numNodes);
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
	for ( long i = 0 ; i < numNodes ; i++ ) {
		key_t k;
		k.n1 = rand() ;
		k.n0 = rand();
		k.n0 <<= 32;
		k.n0 |= rand();
		keys[i] = k;
	}
	log("generated %li random keys",numNodes);


	//char *data = (char *)mmalloc ( numNodes * sizeof(key_t), "treetest");
	//char *pp = data;
	//if ( ! data ) { log("shit"); return -1; }
	long long t1 = gettimeofdayInMilliseconds();
	for ( long i = 0 ; i < numNodes ; i++ ) {
		t.addNode ( keys[i] , NULL , 0 ); // , pp , 5*1024 );
		//pp += 5*1024;
	}
	long long t2 = gettimeofdayInMilliseconds();
	log("added %li keys to tree in %lli ms",  numNodes , t2 - t1 );


	// now get the list
	t1 = gettimeofdayInMilliseconds();

	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.n0 = 0LL; startKey.n1 = 0;
	endKey.setMax();
	long xx;
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
	log("got list of %li bytes (%li keys) in %lli ms", 
	    list.getListSize(),numNodes,t2-t1);
	return 0;
}

long sanitytest () {
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
	long p = 0;
 loop:
	fprintf(stderr,"pass=%li\n",p++);
	// make random keys
	for ( long i = 0 ; i < 1505000 ; i++ ) {
		key_t k;
		k.n1 = rand() ;
		k.n0 = rand();
		k.n0 <<= 32;
		k.n0 |= rand();
		keys[i] = k;
	}
	// add em
	for ( long i = 0 ; i < 1500000 ; i++ ) 
		if ( t.addNode ( keys[i] , NULL , 0) < 0 ) {
			fprintf(stderr,"add error\n");
			return -1;
		}
	// sort 'em
	gbsort (keys , 1500000 , sizeof(key_t) , cmp );
	// delete em in order now
	for ( long i = 0 ; i < 750000 ; i++ ) 
		if ( t.deleteNode ( keys[i] , true ) < 0 ) {
			fprintf(stderr,"node not found\n");
			return -1;
		}
	// add 5000 nodes
	for ( long i = 1500000 ; i < 1505000 ; i++ ) 
		if ( t.addNode ( keys[i] , NULL , 0) < 0 ) {
			fprintf(stderr,"add error\n");
			return -1;
		}	
	// delete remaining 750000
	for ( long i = 750000 ; i < 1500000 ; i++ ) 
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
