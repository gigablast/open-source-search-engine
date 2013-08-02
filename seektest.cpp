#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


static long long gettimeofdayInMilliseconds() ;

long long gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	long long now=(long long)(tv.tv_usec/1000)+((long long)tv.tv_sec)*1000;
	return now;
}

static pthread_attr_t s_attr;
static void *startUp ( void *state ) ;
static long s_count = 0;
static long s_filesize = 0;
static long s_lock = 1;
static long s_launched = 0;

static int s_fd1, s_fd2;

static long s_numThreads = 0;

static long s_maxReadSize = 1;

static long long s_startTime = 0;

#define MAX_READ_SIZE (2000000)

main ( int argc , char *argv[] ) {

	if ( argc != 4 ) {
		fprintf(stderr,"usage: seektest <bigfilename> <numThreads> "
			"<maxReadSize>\n");
		exit(-1);
	}

	s_numThreads = atoi(argv[2]);
	s_maxReadSize = atoi(argv[3]);
	fprintf(stderr,"threads = %li  maxReadSize = %li\n",
		s_numThreads, s_maxReadSize );

	if ( s_maxReadSize <= 0 ) s_maxReadSize = 1;
	if ( s_maxReadSize > MAX_READ_SIZE ) s_maxReadSize = MAX_READ_SIZE;

	// allow the substitution of another filename
        struct stat stats;
        stats.st_size = 0;
        int status = stat ( argv[1] , &stats );
        // return the size if the status was ok
        if ( status != 0 ) {
		fprintf (stderr,"stats failed");
		exit(-1);
	}
	s_filesize = stats.st_size;
	fprintf(stderr,"file size = %li\n",s_filesize);
	// seed rand
	srand(time(NULL));
	// open 2 file descriptors
	//s_fd1 = open ( "/tmp/glibc-2.2.2.tar" , O_RDONLY );
	s_fd1 = open ( argv[1] , O_RDONLY );
	//s_fd2 = open ( "/tmp/glibc-2.2.5.tar" , O_RDONLY );
	// . set up the thread attribute we use for all threads
	// . fill up with the default values first
	if ( pthread_attr_init( &s_attr ) ) 
		fprintf (stderr,"Threads::init: pthread_attr_init: error\n");
	// then customize
	if ( pthread_attr_setdetachstate(&s_attr,PTHREAD_CREATE_DETACHED) )
		fprintf ( stderr,"Threads::init: pthread_attr_setdeatchstate:\n");
	if ( setpriority ( PRIO_PROCESS, getpid() , 0 ) < 0 ) {
		fprintf(stderr,"Threads:: setpriority: failed\n");
		exit(-1);
	}
	s_lock = 1;
	pthread_t tid1, tid2;

	// set time
	s_startTime = gettimeofdayInMilliseconds();

	for ( long i = 0 ; i < s_numThreads ; i++ ) {
		int err = pthread_create ( &tid1,&s_attr,startUp,(void *)i) ;
		if ( err != 0     ) return -1;
	}
	// unset lock
	s_lock = 0;
	// sleep til done
	while ( 1 == 1 ) sleep(1000);
}

void *startUp ( void *state ) {
	long id = (long) state;
	// . what this lwp's priority be?
	// . can range from -20 to +20
	// . the lower p, the more cpu time it gets
	// . this is really the niceness, not the priority
	int p = 0;
	//if ( id == 1 ) p = 0;
	//else           p = 30;
	// . set this process's priority
	// . setpriority() is only used for SCHED_OTHER threads
	if ( setpriority ( PRIO_PROCESS, getpid() , p ) < 0 ) {
		fprintf(stderr,"Threads::startUp: setpriority: failed\n");
		exit(-1);
	}
	// read buf
	char buf [ MAX_READ_SIZE ];
	// we got ourselves
	s_launched++;
	// msg
	fprintf(stderr,"id = %li launched\n",id);
	// wait for lock to be unleashed
	while ( s_launched != s_numThreads ) usleep(10);
	// now do a stupid loop
	long j, off , size;
	for ( long i = 0 ; i < 100000 ; i++ ) {
		off = rand() % (s_filesize - s_maxReadSize );
		// rand size
		//size = rand() % s_maxReadSize;
		size = s_maxReadSize;
		//if ( size < 32*1024 ) size = 32*1024;
		// time it
		long long start = gettimeofdayInMilliseconds();
		//fprintf(stderr,"%li) i=%li start\n",id,i );
		pread ( s_fd1 , buf , size , off );
		//fprintf(stderr,"%li) i=%li done\n",id,i );
		long long now = gettimeofdayInMilliseconds();
		s_count++;
		float sps = (float)((float)s_count * 1000.0) / 
			(float)(now - s_startTime);
		fprintf(stderr,"count=%li off=%li size=%li time=%lims "
			"(%.2f seeks/sec)\n",
			(long)s_count,
			(long)off,
			(long)size,
			(long)(now - start) , 
			sps );
	}
		

	// dummy return
	return NULL;
}

