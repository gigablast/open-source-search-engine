#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_READ_SIZE 2000000100

#define MAX_HASHES 1000

uint64_t g_hashtab[256][256];

uint32_t hash32 ( char *s, int32_t len ) {
	uint32_t h = 0;
	int32_t i = 0;
	while ( i < len ) {
		h ^= (uint32_t) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

int32_t atoip ( char *s , int32_t slen ) {
	// point to it
	char *p = s;
	if ( s[slen] ) {
		// copy into buffer and NULL terminate
		char buf[1024];
		if ( slen >= 1024 ) slen = 1023;
		gbmemcpy ( buf , s , slen );
		buf [ slen ] = '\0';
		// point to that
		p = buf;
	}
	// convert to int
	struct in_addr in;
	in.s_addr = 0;
	inet_aton ( p , &in );
	// ensure this really is a int32_t before returning ip
	if ( sizeof(in_addr) == 4 ) return in.s_addr;
	// otherwise bitch and return 0
	//log("ip:bad inet_aton"); 
	return 0; 
}

// . returns -1 on error, 0 on success
// . reads HTTP reply from filename given as argument, filters it, 
//   and then writes it to stdout
// . originally, we read from stdin, but popen was causing problems when called
//   from a thread on linux 2.4.17 with the old linux threads
int main ( int argc , char *argv[] ) {

	// should have one and only 1 arg (excluding filename)
	if ( argc != 2 ) {
		fprintf(stderr,"usage: fql <querylogfilename1>..."
			"<querylogfilenameN>\n");
		return -1;
	}

	// each log file should be <= 2GB
	char *buf = (char *)malloc ( MAX_READ_SIZE );
	if ( ! buf ) {
		fprintf(stderr,"fql:malloc:li: %s: %s\n",
			(int32_t)MAX_READ_SIZE,strerror(errno)); 
		return -1;
	}


	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( int32_t i = 0 ; i < 256 ; i++ )
		for ( int32_t j = 0 ; j < 256 ; j++ ) {
			g_hashtab [i][j]  = (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
		}
	if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;


	fprintf(stderr,"fql: reading %s\n", argv[1]);

	// first and only arg is the input file to read from
	int fd = open ( argv[1] , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"fql:open: %s: %s\n",
			argv[1],strerror(errno)); 
		free ( buf );
		return -1;
	}

	int n = read ( fd , buf , MAX_READ_SIZE );

	close ( fd );

	fprintf(stderr,"fql: done reading %s\n", argv[1]);

	// return -1 on read error
	if ( n < 0 ) {
		fprintf(stderr,"fql:fread: %s\n",strerror(errno)); 
		free ( buf );
		return -1;
	}

	// warn if the doc was bigger than expected
	if ( n >= (int32_t)MAX_READ_SIZE ) 
		fprintf(stderr,"fql: WARNING: MAX_READ_SIZE "
			"needs boost\n");
	// if nothing came in then nothing goes out, we're done
	if ( n == 0 ) { free ( buf ) ; return 0; }

	// store last 1000 hashes in a ring
	int32_t hashes[MAX_HASHES];
	memset ( hashes, 0 , MAX_HASHES * 4 );
	int32_t nh = 0;

	// parse out query from each url
	char *p = buf;
	for ( ; *p ; p++ ) {
		if ( p[0] != '?' && p[0] != '&' ) continue;
		if ( p[1] != 'q' ) continue;
		if ( p[2] != '=' ) continue;
		p += 3;
		// mark the end
		char *end = p;
		bool good = true;
		for ( ; *end && *end!='&' && *end!='\n' && *end!=' '; end++ ) {
			// double quote?
			if ( *end == '%' &&
			     end[1] == '2' &&
			     end[2] == '2' ) {
				good = false;
				break;
			}
			// colon or pipe operators, ignore
			if ( *end == '|') {
				good = false;
				break;
			}
			if ( *end == '%' &&
			     end[1] == '3' &&
			     end[2] == 'a' ) {
				good = false;
				break;
			}
			if ( *end == '%' &&
			     end[1] == '3' &&
			     end[2] == 'A' ) {
				good = false;
				break;
			}

		}
		// filter out?
		if ( ! good ) continue;
		// limit size. 150 is too big.
		if ( end - p > 150 ) continue;

		// scan backwards to get ip
		char *ips = p;
		for ( ; ips>buf && *ips != ' ' && *ips != '\t' ; ips-- ); 
		if ( ips>buf ) ips--;
		for ( ; ips>buf && *ips != ' ' && *ips != '\t' ; ips-- ); 
		char *ipend = ips;
		if ( ips>buf ) ips--;
		for ( ; ips>buf && *ips != ' ' && *ips != '\t' ; ips-- );
		ips++;
		// should be ip now!
		int32_t iplen = ipend - ips;
		//int32_t uip = atoip(ips,ipend-ips);
		//if ( ! uip ) continue;
		// must be ip #
		if ( !isdigit(ips[0]) ) continue;

		// replace comma with space
		for ( char *r = p ; r < end ; r++ ) {
			if ( *r == ',' ) *r = '+';
		}

		char *dst2 = p;
		for ( char *r = p ; r < end ; r++ ) {
			*dst2 = *r;
			if ( *r == '%' &&
			     r[1] == '2' &&
			     r[2] == '0' ) {
				*dst2 = '+';
				r += 2;
			}
			dst2++;
		}
		end = dst2;


		// skip initial spaces
		char *x = p;
		for ( ; x < end ; x++ ) {
			if ( *x == '+' ) continue;
			break;
		}
		char *query = p;
		// filter out back to back spaces
		char *dst = p;
		bool lastWasSpace = false;
		for ( char *x = p ; x < end ; x++ ) {
			// skip back to back spaces
			if ( *x == '+' && lastWasSpace ) continue;
			// skip initial spaces
			if ( x == p && *x == '+' ) {
				lastWasSpace = true;
				continue;
			}
			// skip initial spaces
			*dst++ = *x;
			if      ( *x == '+' ) lastWasSpace = true;
			else                  lastWasSpace = false;
		}
		// null term the overwritten buffer
		*dst = '\0';
		// get the length of the query
		int32_t queryLen = dst - p;
		// skip that for the for loop
		p = dst;
		// skip empty queries
		if ( queryLen==0 ) continue;
		// hash it up
		int32_t h = hash32(query,queryLen);
		for ( int32_t i = 0 ; i < MAX_HASHES ; i++ ) {
			if ( hashes[i] == h ) { good = false; break; }
		}
		hashes[nh] = h;
		// inc and wrap
		if ( ++nh >= MAX_HASHES ) nh = 0;
		// filter out?
		if ( ! good ) continue;
		// cblock it
		char dotCount = 0;
		for ( int32_t k = 0 ; k < iplen ; k++ ) {
			if ( ips[k] != '.' ) continue;
			if ( ++dotCount < 3 ) continue;
			ips[k] = '\0';
			break;
		}
		if ( dotCount != 3 ) continue;
		// print ip 
		//ips[iplen] = '\0';
		// write that out
		fprintf(stdout,"%s %s\n",ips,query);
	}


	return 0;
}


