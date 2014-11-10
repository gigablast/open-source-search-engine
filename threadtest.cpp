#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
static int64_t gettimeofdayInMilliseconds() ;

int64_t gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	return now;
}
*/

static pthread_attr_t s_attr;
static void *startUp ( void *state ) ;
static int32_t s_lock ;

static int s_fd1;
//static int s_fd2;

int main ( ) {
	// seed rand
	srand(time(NULL));
	// open 2 file descriptors
	//s_fd1 = open ( "/tmp/glibc-2.2.2.tar" , O_RDONLY );
	s_fd1 = open ( "/tmp/diskGraph.gif" , O_RDONLY );
	//s_fd2 = open ( "/tmp/glibc-2.2.5.tar" , O_RDONLY );
	// . set up the thread attribute we use for all threads
	// . fill up with the default values first
	if ( pthread_attr_init( &s_attr ) ) 
		fprintf (stderr,"Threads::init: pthread_attr_init: error\n");
	// then customize
	if ( pthread_attr_setdetachstate(&s_attr,PTHREAD_CREATE_DETACHED) )
		fprintf ( stderr,"Threads::init: pthread_attr_setdeatchstate:\n");
	pthread_t tid1;
	//pthread_t tid2;

	// test create overhead
 top:
	s_lock = 1;
	int err = pthread_create ( &tid1 , &s_attr, startUp, (void *)2) ;
	if ( err != 0     ) return -1;
	while ( s_lock == 1 );
	goto top;
	
	// start 2 threads of different priorities
	/*
	int err = pthread_create ( &tid1 , &s_attr, startUp, (void *)2) ;
	if ( err != 0     ) return -1;
	err = pthread_create     ( &tid2 , &s_attr, startUp, (void *)1) ;
	if ( err != 0     ) return -1;
	// sleep til done
	sleep(1000);
	*/
}

void *startUp ( void *state ) {
	int32_t id = (int32_t) state;
	s_lock =0;
	fprintf(stderr,"got\n");
	// exit now to test pthread_create overhead
	//return NULL;
	// . what this lwp's priority be?
	// . can range from -20 to +20
	// . the lower p, the more cpu time it gets
	// . this is really the niceness, not the priority
	int p;
	if ( id == 1 ) p = 0;
	else           p = 30;
	// . set this process's priority
	// . setpriority() is only used for SCHED_OTHER threads
	if ( setpriority ( PRIO_PROCESS, getpid() , p ) < 0 ) {
		fprintf(stderr,"Threads::startUp: setpriority: failed\n");
		exit(-1);
	}
	// read buf
	char buf [ 100*1024 ];
	// msg
	fprintf(stderr,"priority = %"INT32" launched\n",(int32_t)p);
	// now do a stupid loop
	int32_t j, off;
	for ( int32_t i = 0 ; i < 30000000 ; i++ ) {
		j = i % 1000000;
		//if ( j == 0 ) fprintf(stderr,"%"INT32") i=%"INT32"\n",p,i );
		if ( j == 0 ) {
			off = 0; //rand() % 70000000 ;
			fprintf(stderr,"%"INT32") i=%"INT32" start\n",id,i );
			pread ( s_fd1 , buf , 1024*2 , off );
			fprintf(stderr,"%"INT32") i=%"INT32" done\n",id,i );
		}
	}
		

	// dummy return
	return NULL;
}

