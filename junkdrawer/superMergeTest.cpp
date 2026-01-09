#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()
#include "RdbList.h"
#include "sort.h"

static int cmp (const void *p1, const void *p2) ;

main ( int argc , char *argv[] ) {
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	// # of keys to in each list
	int32_t nk = 200000;
	// # keys wanted
	int32_t numKeysWanted = 200000;
	// get # lists to merge
	int32_t numToMerge = atoi ( argv[1] );
	// print start time
	fprintf (stderr,"smt:: randomizing begin. %"INT32" lists of %"INT32" keys.\n",
		 numToMerge, nk);
	// make a list of compressed (6 byte) docIds
        key_t *keys0 = (key_t *) malloc ( sizeof(key_t) * nk );
        key_t *keys1 = (key_t *) malloc ( sizeof(key_t) * nk );
        key_t *keys2 = (key_t *) malloc ( sizeof(key_t) * nk );
        key_t *keys3 = (key_t *) malloc ( sizeof(key_t) * nk );
	// store radnom docIds in this list
	uint32_t *p = (uint32_t *) keys0;
	// random docIds
	for ( int32_t i = 0 ; i < nk ; i++ ) {
		*p++ = rand() ;
		*p++ = rand() ;
		*p++ = rand() ;
	}
	p = (uint32_t *) keys1;
	for ( int32_t i = 0 ; i < nk ; i++ ) {
		*p++ = rand() ;
		*p++ = rand() ;
		*p++ = rand() ;
	}
	p = (uint32_t *) keys2;
	for ( int32_t i = 0 ; i < nk ; i++ ) {
		*p++ = rand() ;
		*p++ = rand() ;
		*p++ = rand() ;
	}
	p = (uint32_t *) keys3;
	for ( int32_t i = 0 ; i < nk ; i++ ) {
		*p++ = rand() ;
		*p++ = rand() ;
		*p++ = rand() ;
	}
	// sort em up
	gbsort ( keys0  , nk , sizeof(key_t) , cmp );
	gbsort ( keys1  , nk , sizeof(key_t) , cmp );
	gbsort ( keys2  , nk , sizeof(key_t) , cmp );
	gbsort ( keys3  , nk , sizeof(key_t) , cmp );
	// set lists
	RdbList list0;
	RdbList list1;
	RdbList list2;
	RdbList list3;
	key_t minKey; minKey.n0 = 0LL; minKey.n1 = 0LL;
	key_t maxKey; maxKey.setMax();
	list0.set ( (char *)keys0 , 
		    nk * sizeof(key_t),
		    nk * sizeof(key_t),
		    minKey , 
		    maxKey , 
		    0 , 
		    false );
	list1.set ( (char *)keys1 , 
		    nk * sizeof(key_t),
		    nk * sizeof(key_t),
		    minKey , 
		    maxKey , 
		    0 , 
		    false );
	list2.set ( (char *)keys2 , 
		    nk * sizeof(key_t),
		    nk * sizeof(key_t),
		    minKey , 
		    maxKey , 
		    0 , 
		    false );
	list3.set ( (char *)keys3 , 
		    nk * sizeof(key_t),
		    nk * sizeof(key_t),
		    minKey , 
		    maxKey , 
		    0 , 
		    false );
	// mergee
	RdbList list;
	RdbList *lists[2];
	lists[0] = &list0;
	lists[1] = &list1;
	lists[2] = &list2;
	lists[3] = &list3;
	//list.prepareForMerge ( lists , 3 , numKeysWanted * sizeof(key_t));
	list.prepareForMerge (lists,numToMerge,numKeysWanted * sizeof(key_t));
	// start time
	fprintf(stderr,"starting merge\n");
	int64_t t = gettimeofdayInMilliseconds();
	// do it
	if ( numToMerge == 2 )
		list.superMerge2 ( &list0 ,
				   &list1 ,
				   minKey ,
				   maxKey ,
				   false );
	if ( numToMerge == 3 )
		list.superMerge3 ( &list0 ,
				   &list1 ,
				   &list2 ,
				   minKey ,
				   maxKey );
	// completed
	int64_t now = gettimeofdayInMilliseconds();
	fprintf(stderr,"smt:: %"INT32" list NEW MERGE took %"UINT64" ms\n",
		numToMerge,now-t);
	// time per key
	int32_t size = list.getListSize() / sizeof(key_t);
	double tt = ((double)(now - t))*1000000.0 / ((double)size);
	fprintf (stderr,"smt:: %f nanoseconds per key\n", tt);
	// stats
	//double d = (1000.0*(double)nk*2.0) / ((double)(now - t));
	double d = (1000.0*(double)(size)) / ((double)(now - t));
	fprintf (stderr,"smt:: %f cycles per final key\n" ,
		 400000000.0 / d );
	fprintf (stderr,"smt:: we can do %"INT32" adds per second\n" ,(int32_t)d);
	
	fprintf (stderr,"smt:: final list size = %"INT32"\n",list.getListSize());
	// now get list from the old merge routine
	RdbList listOld;
	listOld.prepareForMerge (lists,numToMerge,numKeysWanted*sizeof(key_t));
	t = gettimeofdayInMilliseconds();
	listOld.merge_r ( lists , numToMerge , true , minKey , maxKey , false ,
		       numKeysWanted * sizeof(key_t));
	now = gettimeofdayInMilliseconds();
	fprintf(stderr,"smt:: %"INT32" list OLD MERGE took %"UINT64" ms\n",
		numToMerge,now-t);
	// then compare

	// exit gracefully
	exit ( 0 );
}

int cmp (const void *h1, const void *h2) {
	if ( *(key_t *)h1 < *(key_t *)h2 ) return -1;
	if ( *(key_t *)h1 > *(key_t *)h2 ) return  1;
	return 0;
}
