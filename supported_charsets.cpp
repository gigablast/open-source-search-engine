#include "gb-include.h"

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "iana_charset.h"
#include <iconv.h>

// fake shutdown for Loop and Parms
bool mainShutdown(bool urgent);
bool mainShutdown(bool urgent){return true;}
bool closeAll ( void *state , void (* callback)(void *state) ) { return true; }
bool allExit ( ) { return true; }

int main (int argc, char **argv) {
	if ( ! g_log.init( "foo.log" )        ) {
		fprintf (stderr,"db: Log file init failed.\n" ); exit( 1 ); }
	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("db: Failed to init hashtable." ); exit(1); }
	//ucInit();
	for (int i=2; i <= 2259 ; i++ ){
		char *charset = get_charset_str(i);
		if (!charset) continue;
		
		const char *csAlias = charset;
		if (!strncmp(charset, "x-windows-949", 13))
			csAlias = "CP949";

		if (!strncmp(charset, "Windows-31J", 13))
			csAlias = "CP932";
		
		// Treat all latin1 as windows-1252 extended charset
		if (!strcmp(charset, "ISO-8859-1") )
			csAlias = "WINDOWS-1252";
		
		iconv_t cd1 = gbiconv_open("UTF-16LE", csAlias);

		if (cd1 == (iconv_t)-1) {	
			//printf("%8s %5d %50s\n",
			//       "",i, csAlias);
			continue;
		}
		iconv_t cd2 = gbiconv_open(csAlias, "UTF-16LE");

		if (cd2 == (iconv_t)-1) {	
			//printf("%8s %5d %50s\n",
			//       "",i, csAlias);
			continue;
		}
// 		char *buf1 = "testing";
// 		size_t incount = 7;
// 		char buf2[256];
// 		size_t outcount = 256;
// 		char *p1 = buf1;
// 		char *p2 = buf2;

// 		int res = iconv(cd1, &p1, &incount,&p2, &outcount);
// 		if (res < 0 && errno) {
// 			printf("oops1: %d (%s)\n",
// 			       errno, strerror(errno)); continue;
// 		}
// 		char buf3[256];
// 		incount = outcount;
// 		outcount = 256;
// 		p1 = buf2;
// 		p2 = buf3;
// 		res = iconv(cd2, &p1, &incount,&p2, &outcount);
// 		if (res < 0 && errno) {
// 			printf("oops2: %d (%s)\n",
// 			       errno, strerror(errno)); continue;

// 		}

//		printf("%08x %08x %5d %50s\n",
//		       cd1, cd2, i, csAlias);
		printf("%5d %50s\n",
		       i, csAlias);
	}
	if (gbiconv_open("UTF-8", "WINDOWS-1252") < 0) return false;
	if (gbiconv_open("WINDOWS-1252", "UTF-8") < 0) return false;

//	while(1); //keep files open so you can check with lsof
}
