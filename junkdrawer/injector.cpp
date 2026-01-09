#include "gb-include.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

int main ( int argc , char *argv[] ) {
	if ( argc !=2 ) {
		fprintf(stderr,"need filename parm\n"); return -1; }
	char *filename = argv[1];
	// now make a new socket descriptor
	int sd = socket ( AF_INET , SOCK_STREAM , 0 ) ;
	if ( sd < 0 ) { fprintf(stderr,"no socket\n"); return -1; }
	// now we have a connect just starting or already in progress
	struct sockaddr_in to;
	to.sin_family = AF_INET;
	// our ip's are always in network order, but ports are in host order
	// convert to int
	struct in_addr in;
	in.s_addr = 0;
	//inet_aton ( "207.114.174.29" , &in );
	inet_aton ( "192.168.1.10" , &in );

	to.sin_addr.s_addr =  in.s_addr;
	to.sin_port        = htons ((uint16_t)( 8000));
	bzero ( &(to.sin_zero) , 8 ); // TODO: bzero too slow?
	if ( connect ( sd, (sockaddr *)&to, sizeof(to) ) != 0 ) {
		fprintf(stderr,"connect failed\n"); 
		return -1;
	}
	char buf [ 1024*1024*4 ];
	char *p = buf;

	char *mime = 
		"POST /inject HTTP/1.0\r\n"
		"Content-Length: 0000000\r\n"
		"Content-Type: text/html\r\n"
		"Connection: Close\r\n\r\n";

	char *ctype = "text/html";
	
	if ( strstr ( filename , ".doc" ) ) ctype = "application/msword";
	if ( strstr ( filename , ".pdf" ) ) ctype = "application/pdf";

	sprintf ( p , 

		  "%s"
		  "u=myurl&c=&delete=0&ip=4.5.6.7&iplookups=0&dedup=1&"
		  "rs=7&quick=1&hasmime=1&"
		  "ucontent=HTTP 200\r\n"
		  "Last-Modified: Sun, 06 Nov 1994 08:49:37 GMT\r\n"
		  "Connection: Close\r\n"
		  "Content-Type: %s\r\n"
		  "\r\n" ,
		  mime ,ctype );
	p += gbstrlen ( p );

	// put pdf here
	int fd;
	fd = open ( filename , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"open %s failed\n", filename); 
		return -1;
	}
	int n = read ( fd , p , 3*1024*1024 );
	if ( n <= 0 ) {
		fprintf(stderr,"read 0 bytes of %s\n",filename); 
		return -1;
	}
	fprintf(stderr,"read %i bytes of %s\n",n,filename); 
	p += n;
	int32_t size = p - buf;
	// set content length
	int32_t clen = size - gbstrlen(mime);
	char ttt[16];
	sprintf ( ttt , "%07"INT32"", clen );
	fprintf(stderr,"clen=%"INT32"\n",clen);
	fprintf(stderr,"lastchar=%"INT32"\n",(int32_t)buf[size-1]);
	fprintf(stderr,"lastchar-1=%"INT32"\n",(int32_t)buf[size-2]);
	char *cptr = strstr ( buf , "Content-Length: ");
	if ( ! cptr ) { fprintf(stderr,"cptr NULL\n"); return -1; }
	cptr += 16; // point to it
	gbmemcpy ( cptr , ttt , 7 );
	// print the mime
	char *xx = buf + gbstrlen(mime) ; // + 200
	char c = *xx;
	*xx = '\0';
	fprintf(stderr,"mime=%s",buf);
	*xx = c;
	fprintf(stderr,"\nsending %"INT32" bytes\n",size);
	// now inject it
	int32_t sent = 0;
 loop:
	n = send ( sd , buf + sent , size - sent , 0 );
	fprintf(stderr,"n=%i\n",n);
	if ( n == 0 && sent != size ) goto loop;
	if ( n > 0 ) { 
		fprintf(stderr,"did %"INT32"\n",n);
		sent += n; 
		goto loop; 
	}
	//if ( n == -1 && errno == EAGAIN ) goto loop;
	if ( sent != size ) {
		fprintf(stderr,"only sent %i bytes of %"INT32"\n",sent,size);
		fprintf(stderr,"err=%s\n",strerror(errno));
		return -1;
	}
	// no need to wait for reply
	char reply[1024];
	n = read ( sd , buf , 1000 );
	fprintf ( stderr, "read %i bytes\n",n);
	close (sd );
	return 0;
}
