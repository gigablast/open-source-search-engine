
#include "gb-include.h"

int main ( int argc , char *argv[] ) {

	long count = 0;
	for ( long i = 0 ; i < 1000000 ; i+= 10000 ) 
		//printf("wget \"http://127.0.0.1:8000/search?q=%%22vital+records%%22&s=%li&n=10000&sc=0&dr=0&raw=8&rt=1&ff=0\" -O resultsvr.%li\n",i,count++);
		printf("wget \"http://127.0.0.1:8000/search?q=ext%%3Axml&s=%li&n=10000&sc=0&dr=0&raw=8&rt=1&ff=0\" -O results.%03li\n",i,count++ );
}
