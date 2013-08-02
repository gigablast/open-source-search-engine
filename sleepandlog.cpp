#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

static long long gettimeofdayInMilliseconds() ;

long long gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	return(long long)(tv.tv_usec/1000)+((long long)tv.tv_sec)*1000;
}

int main ( int argc , char *argv[] ) {
	long long last = -1LL;
 loop:
	long long now = gettimeofdayInMilliseconds();
	char *msg;
	long long diff = now - last;
	if ( last != -1LL && diff >= 2000 ) 
		fprintf (stderr,"last=%lli now=%lli diff=%lli\n", 
			 last,now,diff);
	last = now;
	sleep(1);
	goto loop;
}
