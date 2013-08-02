// Matt Wells, copyright Jan 2002

#include "gb-include.h"

#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

// program to dump a core
int main ( int argc , char *argv[] ) {
	// let's ensure our core file can dump
	struct rlimit lim;
	lim.rlim_cur = lim.rlim_max = RLIM_INFINITY;
	if ( setrlimit(RLIMIT_CORE,&lim) )
		printf ("main.cpp: setrlimit: %s\n", strerror(errno) );
	char *k = 0;
	*k = 32;
}
