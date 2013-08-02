#include "gb-include.h"

#include <pthread.h>

void *start ( void *args );

int main ( ) {
	pthread_t tid1,tid2,tid3;
 loop:
	pthread_create ( &tid1, NULL , start , NULL );
	pthread_create ( &tid2, NULL , start , NULL );
	pthread_create ( &tid3, NULL , start , NULL );
	pthread_join ( tid1 , NULL );
	pthread_join ( tid2 , NULL );
	pthread_join ( tid3 , NULL );
	goto loop;
}


void *start ( void *args ) {
	return NULL;
}
