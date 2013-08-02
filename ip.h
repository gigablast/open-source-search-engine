// Matt Wells, copyright Jun 2001

#ifndef _IPROUTINES_H_
#define _IPROUTINES_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// what are my IPs?

// comcast home
//#define MATTIP1 "68.35.104.227"
//#define MATTIP1 "69.240.75.134"
//#define MATTIP1 "68.42.43.180"
#define MATTIP1 "68.35.74.178"

// local network
//#define MATTIP2 "192.168.1.2"
#define MATTIP2 "10.1.10.84"

// outbound from work network
//#define MATTIP3 "68.35.27.72"
#define MATTIP3 "64.139.94.202"

long  atoip ( char *s , long slen );
long  atoip ( char *s );//, long slen );
char *iptoa ( long ip );
// . get domain of ip address
// . first byte is the host (little endian)
long  ipdom ( long ip ) ;
// most significant 2 bytes of ip
long  iptop ( long ip ) ;
// . is least significant byte a zero?
// . if it is then this ip is probably representing a whole ip domain
bool  isIpDom ( long ip ) ;
// are last 2 bytes 0's?
long  isIpTop ( long ip ) ;

// returns number of top bytes in comon
long  ipCmp ( long ip1 , long ip2 ) ;


#endif

