// gcc errnotest.cpp ; ./a.out
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>

//__thread int errno;

static bool s_called = false;

#define MAX_PID 32767
static int  s_errno ;
static int  s_errnos [ MAX_PID + 1 ];

static long s_bad = 0;
static long s_badPid = -1;

// WARNING: you MUST compile with -DREENTRANT for this to work
int *__errno_location (void) {
	long pid = (long) getpid();
	s_called = true;
	if ( pid <= (long)MAX_PID ) return &s_errnos[pid];
	s_bad++;
	s_badPid = pid;
	return &s_errno; 
}

int g_errno = 0;

int startup ( void *state ) {
	errno = 7; // E2BIG;
	g_errno = 7;
}


int main() {
	errno = 10; // EINVAL;
	g_errno = 10;
	char stack[10000];
	pid_t pid = clone( startup , 
			   stack + 10000 ,
			   //CLONE_SETTLS | 
			   CLONE_VM | SIGCHLD,
			   NULL );
	int status;
	waitpid ( pid , &status, 0  );
	if ( errno != 10 ) fprintf(stderr,"nooooo!\n");
	if ( g_errno == 10 ) fprintf(stderr,"gerrno failed!\n",g_errno);
	if ( s_called ) fprintf(stderr,"__errno_location() was called\n");
}
