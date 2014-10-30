#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

static int64_t gettimeofdayInMilliseconds() ;

int64_t gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	return(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
}

int main ( int argc , char *argv[] ) {
	int64_t last = -1LL;
 loop:
	int64_t now = gettimeofdayInMilliseconds();
	char *msg;
	int64_t diff = now - last;
	if ( last != -1LL && diff >= 2000 ) 
		fprintf (stderr,"last=%lli now=%lli diff=%lli\n", 
			 last,now,diff);
	last = now;
	sleep(1);
	goto loop;
}
