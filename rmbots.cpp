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

#define MAX_READ_SIZE 10000000

#define MAX_HASHES 100000

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
		fprintf(stderr,"usage: rmbots <fileofbotips>\n");
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


	// first and only arg is the input file to read from
	int fd = open ( argv[1] , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"rmbots:open: %s: %s\n",
			argv[1],strerror(errno)); 
		free ( buf );
		return -1;
	}

	int n = read ( fd , buf , MAX_READ_SIZE );

	close ( fd );

	// return -1 on read error
	if ( n < 0 ) {
		fprintf(stderr,"rmbots:fread: %s\n",strerror(errno)); 
		free ( buf );
		return -1;
	}

	// warn if the doc was bigger than expected
	if ( n >= (int32_t)MAX_READ_SIZE ) 
		fprintf(stderr,"rmbots: WARNING: MAX_READ_SIZE "
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
		// mark the end
		char *end = p;
		for ( ; *end && *end!='\n' ; end++ ) ;
		// set it
		char *ip = p;
		// advance p for next call
		if ( *end == '\n' ) p = end;
		// should be ip now!
		int32_t iplen = end - ip;
		//int32_t uip = atoip(ips,ipend-ips);
		//if ( ! uip ) continue;
		// must be ip #
		if ( !isdigit(ip[0]) ) continue;
		// skip empty ip lines
		if ( iplen == 0 ) continue;
		// hash it up
		uint32_t h = hash32(ip,iplen);
		uint32_t n = h % MAX_HASHES;
		for ( ; ; ) {
			if ( hashes[n] == h ) break;
			if ( hashes[n] == 0 ) break;
			if ( ++n >= MAX_HASHES ) n = 0;
		}
		// store it
		hashes[n] = h;
	}

	// now read stdin and filter out the line if it contains
	// the bot ip!!
	char line[5000];
	while ( fgets ( line , 5000 , stdin ) ) {
		char *p = line;
		bool skip = false;
		// scan line for uip
		for ( ; *p ; p++ ) {
			if ( p[0] != 'u' ) continue;
			if ( p[1] != 'i' ) continue;
			if ( p[2] != 'p' ) continue;
			if ( p[3] != '=' ) continue;
			char *ip = p + 4;
			// find end of ip
			char *end = ip;
			for ( ; *end &&*end!='\n'&&*end!='&'; end++);
			// get len
			int32_t iplen = end - ip;
			// hash it now
			uint32_t h = hash32(ip,iplen);
			uint32_t n = h % MAX_HASHES;
			// skip if none
			//if (iplen == 0 ) goto printit;
			// find it in has htable
			for ( ; ; ) {
				if ( hashes[n] == h ) break;
				if ( hashes[n] == 0 ) break;
				if ( ++n >= MAX_HASHES ) n = 0;
			}
			// skip printing it
			if ( hashes[n] == h ) {
				skip = true;
				break;
			}
		}
		// skip printing it cuz its a bot?
		if ( skip ) 
			continue;
		// print it now
		fprintf(stdout,"%s",line);
	}

	return 0;
}


