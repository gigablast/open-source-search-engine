#include "gb-include.h"

#include "ip.h"

int32_t atoip ( char *s , int32_t slen ) {
	// point to it
	char *p = s;
	// copy into buffer and NULL terminate
	char buf[1024];
	if ( s[slen] ) {
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
	log("ip:bad inet_aton"); 
	return 0; 
}

int32_t atoip ( char *s ) {
	// convert to int
	struct in_addr in;
	in.s_addr = 0;
	inet_aton ( s , &in );
	// ensure this really is a int32_t before returning ip
	if ( sizeof(in_addr) == 4 ) return in.s_addr;
	// otherwise bitch and return 0
	log("ip:bad inet_aton"); 
	return 0; 
}

char *iptoa ( int32_t ip ) {
	static char s_buf [ 32 ];
	sprintf ( s_buf , "%hhu.%hhu.%hhu.%hhu",
		  (unsigned char)((ip >>  0)&0xff),
		  (unsigned char)((ip >>  8)&0xff),
		  (unsigned char)((ip >> 16)&0xff),
		  (unsigned char)((ip >> 24)&0xff));
	return s_buf;
	//struct in_addr in;
	//in.s_addr = ip;
	//return inet_ntoa ( in );
}

// . get domain of ip address
// . first byte is the host (little endian)
int32_t  ipdom ( int32_t ip ) { return ip & 0x00ffffff; };

// most significant 2 bytes of ip
int32_t  iptop ( int32_t ip ) { return ip & 0x0000ffff; };

// . is least significant byte a zero?
// . if it is then this ip is probably representing a whole ip domain
bool  isIpDom ( int32_t ip ) { return ( (ip & 0xff000000) == 0 ); };

// are last 2 bytes 0's?
int32_t  isIpTop ( int32_t ip ) { return ( (ip & 0xffff0000) == 0 ); };

// returns number of top bytes in comon
int32_t  ipCmp ( int32_t ip1 , int32_t ip2 ) {
	char *a = (char *)&ip1;
	char *b = (char *)&ip2;
	// little endian compare
	if ( a[3] != b[3] ) return 0;
	if ( a[2] != b[2] ) return 1;
	if ( a[1] != b[1] ) return 2;
	if ( a[0] != b[0] ) return 3;
	return 4; // exact match
}

