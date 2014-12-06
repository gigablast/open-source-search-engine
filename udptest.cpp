// Matt Wells, copyright Sep 2001

#include "gb-include.h"

#include "Mem.h"
#include "Conf.h"
#include "Hostdb.h"
#include "UdpServer.h"
#include "Loop.h"
#include "ip.h"
#include <ctype.h>
#include "Spider.h"

// the default protocol
UdpProtocol g_dp;

// the request handler
static void handleRequest ( UdpSlot *slot  , int32_t     niceness ) ;
static void gotit         ( void    *state , UdpSlot *slot     ) ;

// where we store the file contents, if any
char s_buf [ 1024*1024 ];
int32_t s_n = 0;

int64_t s_startTime;

bool mainShutdown ( bool urgent ) { return true; }

int main ( int argc , char *argv[] ) {

	// ip/port to ask for the file from
	int32_t           ip   = 0;
	uint16_t port = 0;

	// . filename may be supplied 
	// . if so we send that file back to all who ask
	if ( argc == 2 && !isdigit(argv[1][0]) ) {
		char *fn = argv[1];
		log("Reading filename = %s", fn );
		int fd = open ( fn , O_RDONLY );
		if ( fd < 0 ) {
			log("File %s does not exist. exiting.",fn );
			return -1;
		}
		s_n = 0;
		while ( s_n < 1024*1024 && read ( fd , &s_buf[s_n] , 1 ) == 1)
			s_n++;
		close ( fd );
		// listen on udp port 2000
		port = 2000;
		//log("Listening on udp port %hu", port );
	}
	// otherwise if argc is 2 we ask an ip/port for the file
	else if ( argc == 2 && isdigit(argv[1][0]) ) {
		// send on udp port 2001
		port = 2001;
		ip   = atoip ( argv[1] , gbstrlen(argv[1]) );
	}
	// otherwise, print usage
	else {
		fprintf(stderr,
			"udptest <filename>\n"
			"\tThis will set up a server on udp port 2000 which\n"
			"\twill serve the sepcified filename to all incoming\n"
			"\trequests. The first char of the filename must NOT\n"
			"\tbe a number.\n\n"
			"udptest <ip>\n"
			"\tThis will send a request for a file to the\n"
			"\tudptest running on the specified ip. ip must be\n"
			"\tof form like 1.2.3.4\n"
			"\tIt will listen on udp port 2001.\n"
			);
		return -1;
	}
		// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("main::hashinit failed" ); return 1; }
		// default conf filename
	char *confFilename = "./gigablast.conf";
	if ( ! g_conf.init ( confFilename ) ) {
		fprintf (stderr,"main::Conf init failed\n" ); return 1; }
	// init the memory class after conf since it gets maxMem from Conf
 	if ( ! g_mem.init ( 1000000000 ) ) {
		fprintf (stderr,"main::Mem init failed" ); return 1; }
		// start up hostdb
	if ( ! g_hostdb.init()     ) {
		log("main::Hostdb init failed" ); return 1; }
		
	// init the loop
	if ( ! g_loop.init() ) {
		fprintf ( stderr , "main::Loop init failed\n" ); 
		return 1; 
	}

	// . then our main udp server
	// . must pass defaults since g_dns uses it's own port/instance of it
	// . server should listen to a socket and register with g_loop
	// . the last 1 and 0 are respective niceness levels
	if ( ! g_udpServer.init( port                 , // use 2000 or 2001
				 &g_dp                , // use default proto
				 0                    , // niceness = 0
				 1024*1024              ,
				 1024*1024              )){
		fprintf ( stderr, "main::UdpServer init failed\n"); 
		return 1; 
	}

	// . otherwise, set up a handler for incoming msgTypes of 0x00
	// . register a handler for msgType 0x00
	// . we must register this msgType even if we're not going to handle
	//   these requests, because we may send them...
	if ( ! g_udpServer.registerHandler ( 0x00 , handleRequest )) {
		fprintf ( stderr , "udp server registration failed\n");
		return 1;
	}

	// if ip is non-zero send a request to it
	if ( ip != 0 ) {
		log("Sending request to ip=%s port=2000", argv[1] );
		s_startTime = gettimeofdayInMilliseconds();
		g_udpServer.sendRequest ( NULL , // msg ptr
					  0    , // msg size
					  0x00 , // msg type
					  ip   , // ip 
					  2000 , // port
					  0    , // hostId (bogus)
					  NULL , // UdpSlot ptr being used
					  NULL , // callback state
					  gotit, // callback
					  30   );// timeout in seconds
	}

	// . now start g_loops main interrupt handling loop
	// . it should block forever
	// . when it gets a signal it dispatches to a server or db to handle it
	if ( ! g_loop.runLoop()    ) {
		log("main::runLoop failed" ); 
		return 1; 
	}

	// dummy return (0-->normal exit status for the shell)
	return 0;
}


void handleRequest ( UdpSlot *slot , int32_t niceness ) {

	log("got request of type 0x00");

	// send the deisignated file back
	g_udpServer.sendReply ( s_buf ,  // data ptr
				s_n   ,  // data size
				0     ,  // allocated size
				slot  );
	//sendErrorReply ( slot , EBADREQUESTSIZE )
}

void gotit ( void *state , UdpSlot *slot ) {
	// compute speed
	double size    = slot->m_readBufSize;
	double elapsed = gettimeofdayInMilliseconds() - s_startTime;
	log("got reply. %f Mbps", (size*8/(1024.0*1024.0)) / (elapsed / 1000.0) );
	// exit for good
	exit(-1);
}
