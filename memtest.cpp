//#include "gb-include.h"
#include <malloc.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

int main ( ) {

	long bufSize = 100*1024*1024;
	// test random memory accesses
	char *buf = (char *)malloc(bufSize);
	if ( ! buf ) {
		fprintf(stderr,"could not malloc 100MB.\n");
		exit(-1);
	}
	long np = 400000;
	char *ptrs[np];
	for ( long i = 0 ; i < np ; i++ ) 
		ptrs[i] = buf + (rand() % (bufSize-4));
	// access it
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	long long now=(long long)(tv.tv_usec/1000)+((long long)tv.tv_sec)*1000;
	char c; //long c; // char c;
	long loops = 1;
	for ( long k = 0 ; k < loops ; k++ ) 
		for ( long i = 0 ; i < np ; i++ ) 
			//c = *(long *)ptrs[i];
			c = *ptrs[i];
	gettimeofday ( &tv , NULL );
       long long now2=(long long)(tv.tv_usec/1000)+((long long)tv.tv_sec)*1000;
       
	fprintf(stderr,"did %li accesss in %lli ms.\n",loops*np,now2-now);
	fprintf(stderr,"did %lli accesss per second.\n",
		(1000LL*(((long long)loops)*((long long)np)))/(now2-now));
	return 0;

 loop:
	long  sizes[10000];
	long  end = 0;

	for ( long i = 0 ; i < 10000 ; i++ ) {
		sizes[i] = rand() % 1000;
		//fprintf(stderr,"i=%li\n",i);
		//ptrs[i] = (char *)mmalloc ( sizes[i] ,NULL );
		ptrs[i] = (char *)malloc ( sizes[i] );
		if ( ! ptrs[i] ) { 
			fprintf(stderr,"failed on #%li",i); 
			goto freeThem;
		}
		end = i;
		//fprintf(stderr,"ptr=%li\n",(long)ptrs[i]);
	}

 freeThem:
	// now free all
	for ( long i = 0 ; i < end ; i++ ) {
	// for ( long i = 99 ; i >= 0 ; i-- ) {
		//fprintf(stderr,"fi=%li\n",i);
		//mfree ( ptrs[i] , sizes[i] , NULL );
		free ( ptrs[i] );
	}
	//goto loop;
}
