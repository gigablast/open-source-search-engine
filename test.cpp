// Matt Wells, copyright Jan 2002

// program to test Rdb

#include "gb-include.h"

long x,y,z;

#define x y

#undef x
#define y z
#define x y

int main ( int argc , char *argv[] ) {

	x = 13;

	printf("z=%li\n",z);

	long shift = 2;
	unsigned long long h = 1 >> shift;
	printf("h=%llu\n",h);

	shift = 32;
	unsigned long x = 1 >> shift;
	printf("x=%llu\n",x);

}
