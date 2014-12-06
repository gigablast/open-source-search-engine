//#include "gb-include.h"
#include <malloc.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

int main ( ) {

	int32_t bufSize = 100*1024*1024;
	// test random memory accesses
	char *buf = (char *)malloc(bufSize);
	if ( ! buf ) {
		fprintf(stderr,"could not malloc 100MB.\n");
		exit(-1);
	}
	int32_t np = 400000;
	char *ptrs[np];
	for ( int32_t i = 0 ; i < np ; i++ ) 
		ptrs[i] = buf + (rand() % (bufSize-4));
	// access it
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	char c; //int32_t c; // char c;
	int32_t loops = 1;
	for ( int32_t k = 0 ; k < loops ; k++ ) 
		for ( int32_t i = 0 ; i < np ; i++ ) 
			//c = *(int32_t *)ptrs[i];
			c = *ptrs[i];
	gettimeofday ( &tv , NULL );
       int64_t now2=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
       
	fprintf(stderr,"did %"INT32" accesss in %"INT64" ms.\n",loops*np,now2-now);
	fprintf(stderr,"did %"INT64" accesss per second.\n",
		(1000LL*(((int64_t)loops)*((int64_t)np)))/(now2-now));
	return 0;

 loop:
	int32_t  sizes[10000];
	int32_t  end = 0;

	for ( int32_t i = 0 ; i < 10000 ; i++ ) {
		sizes[i] = rand() % 1000;
		//fprintf(stderr,"i=%"INT32"\n",i);
		//ptrs[i] = (char *)mmalloc ( sizes[i] ,NULL );
		ptrs[i] = (char *)malloc ( sizes[i] );
		if ( ! ptrs[i] ) { 
			fprintf(stderr,"failed on #%"INT32"",i); 
			goto freeThem;
		}
		end = i;
		//fprintf(stderr,"ptr=%"INT32"\n",(int32_t)ptrs[i]);
	}

 freeThem:
	// now free all
	for ( int32_t i = 0 ; i < end ; i++ ) {
	// for ( int32_t i = 99 ; i >= 0 ; i-- ) {
		//fprintf(stderr,"fi=%"INT32"\n",i);
		//mfree ( ptrs[i] , sizes[i] , NULL );
		free ( ptrs[i] );
	}
	//goto loop;
}
