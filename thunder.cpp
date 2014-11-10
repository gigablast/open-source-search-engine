// Matt Wells, copyright Sep 2001

#include "gb-include.h"

#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>

// seems like big dgrams are not always liked over the internet 
// connection...
#define DGRAM_SIZE 1450
//#define DGRAM_SIZE (30*1492)
//#define DGRAM_SIZE 10000 // this works, but 50,000 does not

int32_t s_n = 0;

int32_t s_isServer = 0;
int32_t s_blast = 0;

int64_t gettimeofdayInMilliseconds() ;
inline int64_t gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	return now;
}


int main ( int argc , char *argv[] ) {

	// ip/port to ask for the file from
	int32_t           ip   = 0;
	uint16_t port = 0;

	// . filename may be supplied 
	// . if so we send that file back to all who ask
	if ( argc == 2 ) {
		// listen on udp port 2000
		port = 2000;
		s_n = atoi ( argv[1] );
		s_isServer = 1;
	}
	// thunder bytesToSend Port
	else if ( argc == 3 ) {
		// send on udp port 2001
		s_n = atoi ( argv[1] );
		port = atoi(argv[2] );
		s_isServer = 1;
	}
	// thunder bytesToSend ip Port
	else if ( argc == 4 ) {
		// send on udp port 2001
		s_n = atoi ( argv[1] );
		inet_aton ( argv[2] , (struct in_addr *) &ip );
		port = atoi(argv[3]);
		s_isServer = 0;
	}
	// thunder blast bytesToSend ip Port
	else if ( argc == 5 && strcmp(argv[1],"blast") == 0 ) {
		// send on udp port 2001
		s_n = atoi ( argv[2] );
		inet_aton ( argv[3] , (struct in_addr *) &ip );
		port = atoi(argv[4]);
		s_isServer = 0;
		s_blast = 1;
	}
	// otherwise, print usage
	else {
		fprintf(stderr,
			"thunder <bytes-to-send> [port]\n"
			"\tThis will set up a server on udp port 2000 which\n"
			"\twill serve meaningless bytes to all incoming\n"
			"\trequests.\n\n"

			"thunder <bytes-to-read> <ip> <port>\n"
			"\tThis will send a request for this many bytes to\n"
			"\tthe thunder running on the specified ip on port\n"
			"\t2000. ip must be of form like 1.2.3.4\n\n"

			"thunder blast <bytes-to-send> <ip> <port>\n"
			"\tThis will blast the ip port with datagrams.\n"
			"\twill serve meaningless bytes to all incoming\n\n"
			);
		return -1;
	}
			
	if ( s_isServer )
		fprintf(stderr,"Listening on udp port %hu\n", port );

        // set up our socket
        int sock  = socket ( AF_INET, SOCK_DGRAM , 0 );
        if ( sock < 0 ) {
		fprintf(stderr,"socket: %s\n",strerror(errno));
		return -1;
	}
        // sockaddr_in provides interface to sockaddr
        struct sockaddr_in name; 
        // reset it all just to be safe
        bzero((char *)&name, sizeof(name));
        name.sin_family      = AF_INET;
        name.sin_addr.s_addr = 0; /*INADDR_ANY;*/
        name.sin_port        = htons(port);
        // we want to re-use port it if we need to restart
        int options = 1;
        if ( setsockopt(sock, SOL_SOCKET, SO_REUSEADDR ,
			&options,sizeof(options)) < 0 ) {
		fprintf(stderr,"setsockopt:%s\n", strerror(errno));
		return -1;
	}
        // bind this name to the socket
        if ( bind ( sock, (struct sockaddr *)&name, sizeof(name)) < 0) {
                close ( sock );
                fprintf(stderr,"bind on port %hu: %s\n",port,strerror(errno));
		return false;
        }

	char dgram[DGRAM_SIZE];
	int n;
	struct sockaddr_in to;
	sockaddr_in from;
	unsigned int fromLen;
	int32_t count = 0;
	int64_t startTime;
	int32_t bytes = 0;
	char *s;
	uint32_t fromport;
	//int64_t took;
	// send more than expected to make up for losses
	int32_t nn = s_n * 10;
	if ( ! s_isServer ) goto doClient;
readLoop:
	fromLen = sizeof ( struct sockaddr );
	n = recvfrom (sock,dgram,DGRAM_SIZE,0,(sockaddr *)&from, &fromLen);
	if ( n <= 0 ) goto readLoop;
	s = inet_ntoa(from.sin_addr);
	fromport = (uint32_t)ntohs(from.sin_port);
	fprintf(stderr,"got request from ip %s:%"UINT32"\n",
		s,fromport);
	to.sin_family      = AF_INET;
	to.sin_addr.s_addr = from.sin_addr.s_addr;
	to.sin_port        = from.sin_port; // htons ( 2001 );
	bzero ( &(to.sin_zero) , 8 );
	memset ( dgram , 'X' , DGRAM_SIZE );
	count = 0;
 sendLoop:
	bytes = s_n;
	if ( bytes > (int32_t)DGRAM_SIZE ) bytes = (int32_t)DGRAM_SIZE;
	n = sendto(sock,dgram,bytes,0,(struct sockaddr *)&to,sizeof(to));
	if ( n != bytes ) fprintf(stderr,"sendto:%s\n",strerror(errno));
	else count += n;
	//usleep(1);
	//fprintf(stderr,"sent %"INT32" bytes (of %"INT32")\n",count,s_n);
	if ( count < nn ) goto sendLoop;
	fprintf(stderr,"finished sending now listening again\n");
	goto readLoop;

 doClient:
	startTime = gettimeofdayInMilliseconds();
	to.sin_family      = AF_INET;
	to.sin_addr.s_addr = ip;
	to.sin_port        = htons ( port );//2000 ) ; // m_port );
	bzero ( &(to.sin_zero) , 8 );
 tight:
	n = sendto(sock,dgram,DGRAM_SIZE,0,(struct sockaddr *)&to,sizeof(to));
	if ( s_blast ) goto tight;
	count = 0;
 readLoop2:
	n = recvfrom (sock,dgram,DGRAM_SIZE,0,(sockaddr *)&from, &fromLen);
	if ( n <= 0 ) goto readLoop2;
	count += n;
	//fprintf(stderr,"read %"INT32" bytes (of %"INT32")\n",count,s_n);
	if ( count < s_n ) goto readLoop2;
	float secs = gettimeofdayInMilliseconds() - startTime;
	secs /= 1000.0;
	float mb = (float)count * 8.0 / (1024.0*1024.0);
	float mbps = mb / secs;
	fprintf(stderr,"got %"INT32" bytes at %.2f Mbps\n", count , mbps );
	return 1;
}
