// Matt Wells, copyright Apr 2002

// program to rescue data from a core and save it as coreSeg.offset.size

#include "gb-include.h"

#include "File.h"

int main ( int argc , char *argv[] ) {
	// must have big filename
	if ( argc != 4 ) {
		fprintf(stderr,"rescure [corefile] [offset] [size]\n");
		exit(-1);
	}
	int32_t coreOffset = atol(argv[2]);
	int32_t coreSize   = atol(argv[3]);
	File f;
	f.set ( argv[1] );
	if ( ! f.open ( O_RDONLY ) ) {
		fprintf(stderr,"could not open core file %s", argv[1] );
		exit(-1);
	}
	// read whole file into memory
	char *buf = (char *) malloc ( coreSize );
	if ( ! buf ) {
		fprintf(stderr,"could not alloc %"INT32" bytes", coreSize );
		exit(-1);
	}
	if ( f.read ( buf , coreSize , coreOffset ) < 0 ) {
		fprintf(stderr,"could not read %"INT32" bytes", coreSize );
		exit(-1);
	}
	// now dump to separate file
	f.close();
	char name[64];
	sprintf(name,"coreSeg.%"INT32".%"INT32"", coreOffset, coreSize );
	f.set ( name );
	f.open ( O_RDWR );
	f.write ( buf , coreSize );
	f.flush();
}

