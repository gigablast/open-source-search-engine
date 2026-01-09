#include "gb-include.h"

int main ( int argc , char *argv[] ) {

	//for ( int32_t i = 0 ; i < 137 ; i++ ) 
	//	printf("66.154.103.%"INT32" gk%"INT32"\n",i+36,136+i);

	printf ("# the new hosts.conf format:\n"
"\n"
"# <hostId> <hostname> [portoffset] [# <comment>]\n"
"# spare    <hostname> [portoffset] [# <comment>]\n"
"# proxy    <hostname> [portoffset] [# <comment>]\n"
"\n"
"# we use /etc/hosts to get the ip of eth0\n"
"# we insert an 'i' into hostname to get ip of eth1\n"
"\n"
"working-dir: /w/\n"
"port-offset: 2\n"
"index-splits: 64\n"
"\n"
		);

	int32_t gk = 0;
	for ( int32_t i = 0 ; i < 256 ; i++ ) {
		if ( i && (i%16==0) ) gk += 16;
		// wrap to lower rack half at 128
		if ( i == 128 ) gk = 16;
		printf("%03"INT32"\tgk%"INT32"\n",i,gk);
		gk++;
	}


	for ( int32_t i = 256 ; i < 271 ; i++ )
		printf ("spare\tgk%"INT32"\n",i);

	printf ("#proxy\tproxy0\n");
	printf ("#proxy\tproxy1\n");

	return 0;
}
