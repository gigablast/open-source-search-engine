
// automated versioning for gigablast
#include <stdio.h>
#include "Mem.h"

static char s_vbuf[32];

// includes \0
// "Sep 19 2014 12:10:58\0"
int32_t getVersionSize () {
	return 20 + 1;
}

char *getVersion ( ) {
	static bool s_init = false;
	if ( s_init ) return s_vbuf;
	s_init = true;
	sprintf(s_vbuf,"%s %s", __DATE__, __TIME__ );
	// PingServer.cpp needs this exactly to be 24
	if ( gbstrlen(s_vbuf) != getVersionSize() - 1 ) { 
		log("getVersion: %s %"INT32" != %"INT32"",
		    s_vbuf,
		    (int32_t)gbstrlen(s_vbuf),
		    getVersionSize() - 1);
		char *xx=NULL;*xx=0; 
	}
	return s_vbuf;
}

//#define GBVERSION "2014.00.19-12:00:27-MST"
//#define GBVERSION __DATE__ __TIME__ 

