#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()

//static long long gettimeofdayInMilliseconds() ;

long long gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	long long now=(long long)(tv.tv_usec/1000)+((long long)tv.tv_sec)*1000;
	return now;
}

int main ( int argc , char *argv[] ) {

	if ( argc < 2 || argc > 4 ) {
		fprintf(stderr,"menbusttest <bytes> <loops> [r|w]\n");
		return -1;
	}

	long nb = atoi(argv[1]);

	long loops = 1;
	if ( argc >= 3 && argv[2][0]!='w' && argv[2][0]!='r' ) 
		loops = atoi(argv[2]);
	long count = loops;

	bool readf = true;
	if ( argc == 4 && argv[3][0] =='w' ) readf = false;

	// don't exceed 50NB
	if ( nb > 50*1024*1024 ) {
		fprintf(stderr,"truncating to 50 Megabytes\n");
		nb = 50;
	}

	long n = nb ; //* 1024 * 1024 ;

	// make n divisble by 64
	//long rem = n % 64;
	//if ( rem > 0 ) n += 64 - rem;

	// get some memory, 4 megs
	register char *buf = (char *)malloc(n + 64);
	if ( ! buf ) return -1;
	char *bufStart = buf;
	register char *bufEnd = buf + n;

	//fprintf(stderr,"pre-reading %li NB \n",nb);
	// pre-read it so sbrk() can do its thing
	for ( long i = 0 ; i < n ; i++ ) buf[i] = 1;

	// time stamp
	long long t = gettimeofdayInMilliseconds();

	// . time the read loop
	// . each read should only be 2 assenbly movl instructions:
	//   movl	-52(%ebp), %eax
	//   movl	(%eax), %eax
	//   movl	-52(%ebp), %eax
	//   movl	4(%eax), %eax
	//   ...
 loop:
	register long c;

	if ( readf ) {
		while ( buf < bufEnd ) {
			// repeat 16x for efficiency.limit comparison to bufEnd
			c = *(long *)(buf+ 0);
			c = *(long *)(buf+ 4);
			c = *(long *)(buf+ 8);
			c = *(long *)(buf+12);
			c = *(long *)(buf+16);
			c = *(long *)(buf+20);
			c = *(long *)(buf+24);
			c = *(long *)(buf+28);
			c = *(long *)(buf+32);
			c = *(long *)(buf+36);
			c = *(long *)(buf+40);
			c = *(long *)(buf+44);
			c = *(long *)(buf+48);
			c = *(long *)(buf+52);
			c = *(long *)(buf+56);
			c = *(long *)(buf+60);
			buf += 64;
		}
	}
	else {
		while ( buf < bufEnd ) {
			// repeat 8x for efficiency. limit comparison to bufEnd
			*(long *)(buf+ 0) = 0;
			*(long *)(buf+ 4) = 1;
			*(long *)(buf+ 8) = 2;
			*(long *)(buf+12) = 3;
			*(long *)(buf+16) = 4;
			*(long *)(buf+20) = 5;
			*(long *)(buf+24) = 6;
			*(long *)(buf+28) = 7;
			buf += 32;
		}
	}
	if ( --count > 0 ) {
		buf = bufStart;
		goto loop;
	}

	// completed
	long long now = gettimeofdayInMilliseconds();
	// multiply by 4 since these are longs
	char *op = "read";
	if ( ! readf ) op = "wrote";
	fprintf (stderr,"menbustest: %s %li bytes (x%li) in %llu ms\n",
		 op , n , loops , now - t );
	// stats
	if ( now - t == 0 ) now++;
	double d = (1000.0*(double)loops*(double)(n)) / ((double)(now - t));
	fprintf (stderr,"menbustest: we did %.2f MB/sec\n" ,
		 d/(1024.0*1024.0));

	return 0;
}
