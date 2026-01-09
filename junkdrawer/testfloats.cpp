// Matt Wells, copyright Jan 2002

//#include "gb-include.h"
#include <sys/time.h>  // gettimeofday() 
#include <stdio.h>

int64_t gettod ( ) {
	struct timeval tv;
	gettimeofday ( &tv , 0 );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	return now;
}

int main ( int argc , char *argv[] ) {

	float x=  0.4598;
	float y = 13.4567;

	printf("start float loop\n");
	float z;
	int64_t start = gettod();
	for ( int32_t i = 0 ; i < 10000000 ; i++ ) 
		z = x * y;
	int64_t end = gettod();
	printf("float muls took %"INT64"ms\n",end-start);

	int32_t xi = 14598;
	int32_t yi = 134567;
	int32_t zi;
	start = gettod();
	for ( int32_t i = 0 ; i < 10000000 ; i++ ) 
		zi = xi * yi;
	end = gettod();
	printf("int muls took %"INT64"ms\n",end-start);




	start = gettod();
	for ( int32_t i = 0 ; i < 10000000 ; i++ ) 
		z = x + y;
	end = gettod();
	printf("float adds took %"INT64"ms\n",end-start);


	start = gettod();
	for ( int32_t i = 0 ; i < 10000000 ; i++ ) 
		zi = xi + yi;
	end = gettod();
	printf("int adds took %"INT64"ms\n",end-start);


	start = gettod();
	for ( int32_t i = 0 ; i < 10000000 ; i++ ) 
		z = x / y;
	end = gettod();
	printf("float divs took %"INT64"ms\n",end-start);


	start = gettod();
	for ( int32_t i = 0 ; i < 10000000 ; i++ ) 
		zi = xi / yi;
	end = gettod();
	printf("int divs took %"INT64"ms\n",end-start);



	return 0;
}
