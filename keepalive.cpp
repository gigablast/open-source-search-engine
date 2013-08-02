#include "gb-include.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

int main ( int argc , char *argv[] ) {

 loop:
	printf("keepalive\n");
	//printf("%c",0x07); // the bell!
	sleep(400);
	goto loop;
}
