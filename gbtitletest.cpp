// find corruption in an Indexdb dump

#include "gb-include.h"

#include <sys/types.h>
#include <fcntl.h>

bool mainShutdown ( bool urgent ) { return true; }
bool closeAll ( void *state, void(*callback)(void *state) ) { return true; }
bool allExit() { return true; }

int main ( int argc , char *argv[] ) {
	int32_t count = 0;
	// check for arguments
	for (int32_t i = 1; i < argc; i++) {
	}
loop:
	// read a url from stdin
	char s[1024];
	if ( ! fgets ( s , 1024 , stdin ) ) {
		if ( count == 0 )
			printf ( "\nFound All Urls.\n" );
		else
			printf ( "\n Did Not Find %"INT32" Urls.\n", count );
		exit(1);
	}
	printf("\n\n%s",s);
	int32_t sLen = gbstrlen(s);
	// url encode the string
	char url[2048];
	int32_t urlLen = urlEncode(url, 2048, s, sLen );
	// make the WGET call
	char wget[4096];
	sprintf ( wget, "wget -q -O gbtitletest.out.tmp "
			"'http://127.0.0.1:8030/search?"
			"raw=2&q=url%%3A%s'", url );
	printf ( "calling: %s\n", wget );
	system ( wget );
	// read the tmp file
	int fd = open ( "gbtitletest.out.tmp", O_RDONLY );
	if ( fd < 0 ) {
		printf ( "ERROR: Had error openning tmp file: "
			 "gbtitletest.out.tmp\n" );
		exit(1);
	}
	char tmpBuf[1024];
	int32_t r = read ( fd, tmpBuf, 1024 );
	if ( r < 12 || strncasecmp ( tmpBuf, "<pre>\n</pre>", 12 ) == 0 ) {
		printf ( "NOT FOUND: %s\n", s );
		count++;
	}
	close(fd);
	// loop
	goto loop;
}
