#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()

//static int64_t gettimeofdayInMilliseconds() ;

int64_t gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	return now;
}

int main ( int argc , char *argv[] ) {

	if ( argc < 2 || argc > 4 ) {
		fprintf(stderr,"menbusttest <bytes> <loops> [r|w]\n");
		return -1;
	}

	int32_t nb = atoi(argv[1]);

	int32_t loops = 1;
	if ( argc >= 3 && argv[2][0]!='w' && argv[2][0]!='r' ) 
		loops = atoi(argv[2]);
	int32_t count = loops;

	bool readf = true;
	if ( argc == 4 && argv[3][0] =='w' ) readf = false;

	// don't exceed 50NB
	if ( nb > 50*1024*1024 ) {
		fprintf(stderr,"truncating to 50 Megabytes\n");
		nb = 50;
	}

	int32_t n = nb ; //* 1024 * 1024 ;

	// make n divisble by 64
	//int32_t rem = n % 64;
	//if ( rem > 0 ) n += 64 - rem;

	// get some memory, 4 megs
	register char *buf = (char *)malloc(n + 64);
	if ( ! buf ) return -1;
	char *bufStart = buf;
	register char *bufEnd = buf + n;

	//fprintf(stderr,"pre-reading %"INT32" NB \n",nb);
	// pre-read it so sbrk() can do its thing
	for ( int32_t i = 0 ; i < n ; i++ ) buf[i] = 1;

	// time stamp
	int64_t t = gettimeofdayInMilliseconds();

	// . time the read loop
	// . each read should only be 2 assenbly movl instructions:
	//   movl	-52(%ebp), %eax
	//   movl	(%eax), %eax
	//   movl	-52(%ebp), %eax
	//   movl	4(%eax), %eax
	//   ...
 loop:
	register int32_t c;

	if ( readf ) {
		while ( buf < bufEnd ) {
			// repeat 16x for efficiency.limit comparison to bufEnd
			c = *(int32_t *)(buf+ 0);
			c = *(int32_t *)(buf+ 4);
			c = *(int32_t *)(buf+ 8);
			c = *(int32_t *)(buf+12);
			c = *(int32_t *)(buf+16);
			c = *(int32_t *)(buf+20);
			c = *(int32_t *)(buf+24);
			c = *(int32_t *)(buf+28);
			c = *(int32_t *)(buf+32);
			c = *(int32_t *)(buf+36);
			c = *(int32_t *)(buf+40);
			c = *(int32_t *)(buf+44);
			c = *(int32_t *)(buf+48);
			c = *(int32_t *)(buf+52);
			c = *(int32_t *)(buf+56);
			c = *(int32_t *)(buf+60);
			buf += 64;
		}
	}
	else {
		while ( buf < bufEnd ) {
			// repeat 8x for efficiency. limit comparison to bufEnd
			*(int32_t *)(buf+ 0) = 0;
			*(int32_t *)(buf+ 4) = 1;
			*(int32_t *)(buf+ 8) = 2;
			*(int32_t *)(buf+12) = 3;
			*(int32_t *)(buf+16) = 4;
			*(int32_t *)(buf+20) = 5;
			*(int32_t *)(buf+24) = 6;
			*(int32_t *)(buf+28) = 7;
			buf += 32;
		}
	}
	if ( --count > 0 ) {
		buf = bufStart;
		goto loop;
	}

	// completed
	int64_t now = gettimeofdayInMilliseconds();
	// multiply by 4 since these are int32_ts
	char *op = "read";
	if ( ! readf ) op = "wrote";
	fprintf (stderr,"menbustest: %s %"INT32" bytes (x%"INT32") in %"UINT64" ms\n",
		 op , n , loops , now - t );
	// stats
	if ( now - t == 0 ) now++;
	double d = (1000.0*(double)loops*(double)(n)) / ((double)(now - t));
	fprintf (stderr,"menbustest: we did %.2f MB/sec\n" ,
		 d/(1024.0*1024.0));

	return 0;
}
