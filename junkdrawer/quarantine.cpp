//#include "gb-include.h"
#include <malloc.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

bool testChunk ( unsigned char *mem , int32_t memSize ) {

	unsigned char *pend = mem + memSize;
	for ( unsigned char *p = mem ; p < pend ; p++ ) {
		*p = 0;
		if ( *p ) return false;
		*p = 0x01;
		if ( *p != 0x01 ) return false;
		*p = 0x02;
		if ( *p != 0x02 ) return false;
		*p = 0x04;
		if ( *p != 0x04 ) return false;
		*p = 0x08;
		if ( *p != 0x08 ) return false;
		*p = 0x10;
		if ( *p != 0x10 ) return false;
		*p = 0x20;
		if ( *p != 0x20 ) return false;
		*p = 0x40;
		if ( *p != 0x40 ) return false;
		*p = 0x80;
		if ( *p != 0x80 ) return false;
		*p = 0x01;
		*p <<= 1; if ( *p != 0x02 ) return false;
		*p <<= 1; if ( *p != 0x04 ) return false;
		*p <<= 1; if ( *p != 0x08 ) return false;
		*p <<= 1; if ( *p != 0x10 ) return false;
		*p <<= 1; if ( *p != 0x20 ) return false;
		*p <<= 1; if ( *p != 0x40 ) return false;
		*p <<= 1; if ( *p != 0x80 ) return false;
	}
	return true;
}

int main ( int argc , char *argv[] ) {

	if ( argc < 2 || argc > 2 ) {
		fprintf(stderr,"quarantine <bytes>\n");
		return -1;
	}

	//mlockall(MCL_FUTURE);

	int64_t maxMem = atoi(argv[1]); // 1GB?
	int64_t chunkSize = 300000;
	int32_t n = 0;
	int32_t max = (int32_t)(maxMem / chunkSize) + 10;
	char *mem [ max ];
	int64_t total = 0LL;
	for ( ; ; ) {
		mem[n] = (char *)malloc ( chunkSize );
		if ( mem[n] == NULL ) break;
		total += chunkSize;
		n++;
		if ( total >= maxMem ) break;
		if ( (n % 1000) == 0 )
			fprintf(stderr,"quarantine: alloc block #%"INT32"\n",n);
	}
	fprintf(stderr,
		"quarantine: grabbed %"INT32" chunks of ram for "
		"total of %"UINT64" : %s\n",
		n,total,strerror(errno));

	fprintf(stderr,
		"quarantine: scanning grabbed mem for errors.\n");

	int64_t badRam = 0;
	// scan each chunk
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// erturns true if passes
		if ( testChunk ( (unsigned char *)mem[i], chunkSize ) )
			free ( mem[i] );
		// otherwise do not free it
		else {
			badRam += chunkSize;
			// and lock it up
			mlock ( mem[i] , chunkSize );
		}
		if ( (i % 1000) == 0 )
			fprintf(stderr,"quarantine: test block #%"INT32"\n",i);
	}

	if ( badRam ) {
		fprintf(stderr,
			"quarantine: quarantining %"UINT64" bytes of bad ram.\n",
			badRam);
		// sleep forever
		for ( ; ; ) sleep ( 100 );
	}

	fprintf(stderr,"quarantine: all ram was good. exiting.\n");


	return 0;
}

