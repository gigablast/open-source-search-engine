// Matt Wells, copyright Jan 2002

// program to test Rdb

#include "gb-include.h"
#include "Rdb.h"
#include "Conf.h"
#include <pthread.h>
#include "Dns.h"
#include "Url.h"

bool allExit ( ) { return true; }
bool closeAll ( void *state , void (* callback)(void *state) ) {return true;}

static void timeWrapper ( int fd , void *state ) ;
static void dnsWrapper ( void *state , int32_t ip ) ;

static int32_t s_max = 10;

bool mainShutdown ( bool urgent ) { return true; }

int main ( int argc , char *argv[] ) {

	// init our table for doing zobrist hashing
	if ( ! hashinit() ) {
		log("main::hashinit failed" ); return 1; }
	if ( argc < 2 ) {
		//fprintf(stderr,"dnstest <# threads> [conf filename]\n");
		fprintf(stderr,"dnstest <# threads> < FILEOFURLS\n");
		return -1;
	}
	g_mem.m_maxMem  = 1000000000LL; // 1G
	// default conf filename
	// char *confFilename = "./gb.conf";
	char *confFilename = "./";
	// set threads
	s_max = atoi(argv[1]);
	// use the command line parm as the full conf filename
	//if ( argc == 3 ) confFilename = argv[2];
	// use default
	//if ( argc <  3 ) confFilename = "/gigablast/gigablast.conf";
	// start up log file
	/*
	if ( ! g_log.init( "/tmp/log" )        ) {
		fprintf (stderr,"main::Log init failed\n" ); return 1; }
	*/
	// make a new conf
	// makeNewConf ( 0 , confFilename );
	// read in the conf file
	if ( ! g_conf.init ( confFilename ) ) {
		fprintf (stderr,"main::Conf init failed\n" ); return 1; }
	// debug the udp traffic
	//g_conf.m_logDebugUdp = true;
	g_conf.m_logDebugDns = true;
	// init the memory class after conf since it gets maxMem from Conf
	
	// if ( ! g_mem.init ( 1024*1024*30 ) ) {
	//	fprintf (stderr,"main::Mem init failed\n" ); return 1; }


	// . set up shared mem now, only on udpServer2
	// . will only set it up if we're the lowest hostId on this ip
	//if ( ! g_udpServer2.setupSharedMem() ) {
	//	log("main::SharedMem init failed" ); return 1; }
	// plotter test
	//g_stats.dumpGIF ();
	//exit (0);
	// init the loop
	if ( ! g_loop.init() ) {
		log("main::Loop init failed" ); return 1; }

	//if( ! g_threads.init()){
	//	log("main:Init failed."); return 1;}

	// start up hostdb
	if ( ! g_hostdb.init("./hosts.conf" , 0 )     ) {
		log("main::Hostdb init failed" ); return 1; }
	// . then dns client
	// . server should listen to a socket and register with g_loop
	if ( ! g_dns.init(8855)        ) {
		log("main::Dns client init failed" ); return 1; }

	// every .1 seconds launch a dns request
	if (!g_loop.registerSleepCallback(100,NULL,timeWrapper))
		return false;	
	
	if ( ! g_loop.runLoop()    ) {
		log("main::runLoop failed" ); return 1; }

	return 0;
}

class StateT {
public:
	int32_t m_ip;
	char m_buf[1024];
	int64_t m_time;
};

static int32_t s_count = 0;

void timeWrapper ( int fd , void *state ) { 
 top:
	// bail if too many launched
	if ( s_count >= s_max ) return;
	// new state
	StateT *st = (StateT *)mmalloc ( sizeof(StateT) , "dnstest" );
	// get url from stdin into buf
	char *p = st->m_buf;
	if ( ! fgets ( p , 1023 , stdin ) ) exit ( 0 );
	// trim tail
	while ( p[0] && !isalnum ( p [ gbstrlen(p) - 1] ) ) p [gbstrlen(p)-1]='\0';
	// time it
	st->m_time = gettimeofdayInMilliseconds();
	// then look it up
	Url url;
	url.set ( p , gbstrlen(p) );
	int32_t status = g_dns.getIp( url.getHost() , 
			url.getHostLen() , &st->m_ip , st , dnsWrapper );
	logf(LOG_INFO, "dnstest: Looking up %s", url.getHost());
	if(g_errno) {
		logf(LOG_INFO,"dns: %s.",mstrerror(g_errno));
	}

	// return on error
	if ( status == -1 ) { log("ipWrapper: error"); return; }
	// handle return if did not block
	if ( status != 0 ) {
		s_count++; 
		//log(LOG_INFO,"dns: status is not 0, calling dnsWrapper");
		dnsWrapper ( st , st->m_ip );
	}
	// otherwise count it
	if ( status ==  0 ) {
		s_count++; 
		//log(LOG_INFO,"dns: status is 0, increment s_count to %i",
		//		s_count);
	}
	// loop to do more
	goto top;
}

void dnsWrapper ( void *state , int32_t ip ) {
	StateT *st = (StateT *)state;
	int64_t time = gettimeofdayInMilliseconds() - st->m_time ;
	fprintf ( stderr,"Response: %"INT64"ms %s %s (%s)\n", time, 
			st->m_buf , iptoa(ip) , mstrerror(g_errno));
	//if ( g_errno == ETRYAGAIN )
	//	log("hey");
	mfree ( st , sizeof(StateT), "dnstest" );
	s_count--;
}
