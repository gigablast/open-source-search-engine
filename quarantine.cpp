//#include "gb-include.h"
#include <malloc.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

bool testChunk ( unsigned char *mem , long memSize ) {

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

	long long maxMem = atoi(argv[1]); // 1GB?
	long long chunkSize = 300000;
	long n = 0;
	long max = (long)(maxMem / chunkSize) + 10;
	char *mem [ max ];
	long long total = 0LL;
	for ( ; ; ) {
		mem[n] = (char *)malloc ( chunkSize );
		if ( mem[n] == NULL ) break;
		total += chunkSize;
		n++;
		if ( total >= maxMem ) break;
		if ( (n % 1000) == 0 )
			fprintf(stderr,"quarantine: alloc block #%li\n",n);
	}
	fprintf(stderr,
		"quarantine: grabbed %li chunks of ram for "
		"total of %llu : %s\n",
		n,total,strerror(errno));

	fprintf(stderr,
		"quarantine: scanning grabbed mem for errors.\n");

	long long badRam = 0;
	// scan each chunk
	for ( long i = 0 ; i < n ; i++ ) {
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
			fprintf(stderr,"quarantine: test block #%li\n",i);
	}

	if ( badRam ) {
		fprintf(stderr,
			"quarantine: quarantining %llu bytes of bad ram.\n",
			badRam);
		// sleep forever
		for ( ; ; ) sleep ( 100 );
	}

	fprintf(stderr,"quarantine: all ram was good. exiting.\n");


	return 0;
}

