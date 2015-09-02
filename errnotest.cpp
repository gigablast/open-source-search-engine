#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <assert.h>

static int s_called = 0;

#define MAX_PID 32767
static int  s_errno ;
static int  s_errnos [ MAX_PID + 1 ];

static int32_t s_bad = 0;
static int32_t s_badPid = -1;

// WARNING: you MUST compile with -DREENTRANT for this to work
int *__errno_location (void) {
	int32_t pid = (int32_t) getpid();
	s_called++;
	if ( pid <= (int32_t)MAX_PID ) return &s_errnos[pid];
	s_bad++;
	s_badPid = pid;
	return &s_errno; 
}

//extern __thread int errno;

int g_errno = 0;

int startup ( void *state ) {
	char buf[5];
	// this sets errno, but does not seem to call our __errno_location
	// override, BUT does seem to not affect "errno" in main() either!
	// maybe this is the TLS support?
	int bytes = read(-9,buf,5);
	//errno = 7; // E2BIG;
	//assert ( errno && bytes == -1 );
	//g_errno = errno;
}


int main() {
	//errno = 10; // EINVAL;
	g_errno = 10;
	char stack[10000];
	pid_t pid = clone( startup , 
			   stack + 10000 ,
			   //CLONE_SETTLS | 
			   CLONE_VM | SIGCHLD,
			   NULL );
	int status;
	waitpid ( pid , &status, 0  );

	fprintf(stderr,"__errno_location() was called %i "
		"times\n",s_called);

	if ( errno != 10 ) fprintf(stderr,"errno=%i (failed)\n",errno);
	else fprintf(stderr,"errno=%i (success)\n",errno);

	if ( g_errno == 10 || g_errno == 0 ) 
		fprintf(stderr,"gerrno=%i (failed)\n",g_errno);
	else 
		fprintf(stderr,"gerrno=%i (success)\n",g_errno);
}
