#include "gb-include.h"
#include <errno.h>

int main ( int argc , char *argv[] ) {

	if ( argc != 2 ) {
		fprintf(stderr,"uniq2 <filename>\n");
		return -1;
	}

	char *fname = argv[1];

	FILE *fd = fopen ( fname , "r" );
	if ( ! fd ) {
		fprintf(stderr,"open %s : %s\n",
			fname,strerror(errno));
		return -1;
	}

	// one line at a time
	char s[1024];
	char last[1024];
	int32_t count = 1;
	bool first = true;
	while ( fgets ( s , 1023 , fd ) ) {
		// remove \n
		s[gbstrlen(s)-1]='\0';
		// compare to last
		if ( first ) { strcpy ( last , s ); first = false; continue; }
		// same as last?
		if ( strcmp(s,last) == 0 ) { count++; continue; }
		// print out the old last
		fprintf(stdout,"%"INT32" %s\n",count,last);
		// otherwise, we become last
		strcpy ( last , s );
		// and count is reset
		count = 1;
	}
	return 0;
}
