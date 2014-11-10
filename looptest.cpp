#include "gb-include.h"

#include "Loop.h"
#include <pthread.h>
#include <sys/types.h>  // pid_t

static pthread_attr_t s_attr;
static void *startUp ( void *state ) ;

static pid_t s_pid;

int main ( int argc , char *argv[] ) {

	s_pid = getpid();
	
	if ( ! g_loop.init() ) {
		log("






























main::Loop init failed" ); return 1; }

	// start a thread
	if ( pthread_attr_init( &s_attr ) ) 
		fprintf (stderr,"Threads::init: pthread_attr_init: error\n");
	if ( pthread_attr_setdetachstate(&s_attr,PTHREAD_CREATE_DETACHED) )
		fprintf ( stderr,"Threads::init: pthread_attr_setdeatcte:\n");
	pthread_t tid1, tid2;
	int err = pthread_create ( &tid1 , &s_attr, startUp, (void *)2) ;
	if ( err != 0     ) return -1;

	// pause for signals to queue up
	//sleep(1);

	// run the loop
	if ( ! g_loop.runLoop()    ) {
		log("






























main::runLoop failed" ); return 1; }

	return 0;
}

void *startUp ( void *state ) {
	// sleep 
	sleep(2);
	// put low priroity first
	int32_t niceness;
	for ( int32_t i = 0 ; i < 16 ; i++ ) {
		if ( i < 8 ) niceness = 0;
		else         niceness = 1;
		sigval_t svt; 
		svt.sival_int = (int)niceness ; //(int)(t->m_state); // fd;
		fprintf(stderr,"queuing niceness of %"INT32"\n",niceness);
		sigqueue ( s_pid , GB_SIGRTMIN + 1 + niceness, svt );
	}
}
