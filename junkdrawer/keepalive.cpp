#include "gb-include.h"

int main ( int argc , char *argv[] ) {

 loop:
	printf("keepalive\n");
	//printf("%c",0x07); // the bell!
	sleep(400);
	goto loop;
}
