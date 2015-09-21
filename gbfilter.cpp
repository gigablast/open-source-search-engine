#include "gb-include.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

// . we should not read in more than 1M from input file
// . if g_conf.m_httpMaxReadSize is ever bigger than 1M, this should be inc'd
#define MAX_READ_SIZE (20*1024*1024)

// the various content types
#define CT_UNKNOWN 0
#define CT_HTML    1
#define CT_TEXT    2
#define CT_XML     3
#define CT_PDF     4
#define CT_DOC     5
#define CT_XLS     6
#define CT_PPT     7
#define CT_PS      8

// . declare useful subroutines
// . "buf" is the mime + content, the whole HTTP reply gigabot received
// . "mime" is just the mime of the HTTP reply, the top portion of "buf"
int32_t getMimeLen     ( char *buf  , int32_t bufLen  ) ;
char getContentType ( char *mime , int32_t mimeLen ) ;
int  filterContent  ( char *buf  , int32_t bufLen  , int32_t mimeLen , char ctype ,
		      int32_t  id ) ;

// . returns -1 on error, 0 on success
// . reads HTTP reply from filename given as argument, filters it, 
//   and then writes it to stdout
// . originally, we read from stdin, but popen was causing problems when called
//   from a thread on linux 2.4.17 with the old linux threads
int main ( int argc , char *argv[] ) {

	// should have one and only 1 arg (excluding filename)
	if ( argc != 2 ) {
		fprintf(stderr,"gbfilter: usage: gbfilter <inputfilename>\n");
		return -1;
	}

	// . read HTTP reply in from file, gigablast will give it to us there
	// . this should be the HTTP mime followed by the content
	char *buf = (char *)malloc ( MAX_READ_SIZE );
	if ( ! buf ) {
		fprintf(stderr,"gbfilter:malloc:%s: %s: %s\n",
			argv[1],strerror(errno)); 
		return -1;
	}

	// first and only arg is the input file to read from
	int fd = open ( argv[1] , O_RDONLY );
	if ( fd < 0 ) {
		fprintf(stderr,"gbfilter:open: %s: %s\n",
			argv[1],strerror(errno)); 
		free ( buf );
		return -1;
	}

	int n = read ( fd , buf , MAX_READ_SIZE );

	close ( fd );

	// return -1 on read error
	if ( n < 0 ) {
		fprintf(stderr,"gbfilter:fread: %s\n",strerror(errno)); 
		free ( buf );
		return -1;
	}

	// warn if the doc was bigger than expected
	if ( n >= MAX_READ_SIZE ) 
		fprintf(stderr,"gbfilter: WARNING: MAX_READ_SIZE "
			"needs boost\n");

	//sleep(45);

	//srand(time(NULL));
	//int32_t i = rand() % 30;
	//fprintf(stderr,"sleep(%"INT32")\n",i);
	//sleep(i);

	// if nothing came in then nothing goes out, we're done
	if ( n == 0 ) { free ( buf ) ; return 0; }

	// get the end of the mime of this HTTP reply
	int32_t mimeLen = getMimeLen ( buf , n );

	// if it is -1, no mime boundary was found, so return an error
	if ( mimeLen < 0 ) {
		fprintf(stderr,"gbfilter: no mime boundary\n");
		free ( buf );
		return -1;
	}

	// . get the id from the input filename
	// . use that for out tmp files as well so parent caller can remove
	//   our cruft if we core
	int32_t id ;
	char *p = argv[1];
	// get id in the file
	while ( *p && ! isdigit(*p) ) p++;
	id = atol ( p );

	// ... begin filter logic here ...

	// get the content type (the various types are #define'd above)
	char ctype = getContentType ( buf , mimeLen );
	bool filter = false;
	if ( ctype == CT_PDF ) filter = true ;
	if ( ctype == CT_DOC ) filter = true ;
	if ( ctype == CT_XLS ) filter = true ;
	if ( ctype == CT_PPT ) filter = true ;
	if ( ctype == CT_PS  ) filter = true ;
	if ( filter ) {
		int status = filterContent ( buf, n, mimeLen, ctype, id );
		free ( buf );
		return status;
	}

	// ... end filter logic here ...

	// if not filtered, write the input to stdout unaltered
	// no! make it 0 bytes!
	//int32_t w = fwrite ( buf , 1 , n , stdout );
	//if ( w == n ) { free ( buf ) ; return 0; }
	free ( buf );
	return 0;
	// note any errors
	fprintf(stderr,"gbfilter: fwrite: %s\n",strerror(errno)); 
	free ( buf );
	return -1;
}


// returns -1 if no boundary found
int32_t getMimeLen ( char *buf , int32_t bufLen ) {
	// size of the boundary
	int32_t bsize = 0;
	// find the boundary
	int32_t i;
	for ( i = 0 ; i < bufLen ; i++ ) {
		// continue until we hit a \r or \n
		if ( buf[i] != '\r' && buf[i] != '\n' ) continue;
		// boundary check
		if ( i + 1 >= bufLen ) continue;
		// prepare for a smaller mime size
		bsize = 2;
		// \r\r
		if ( buf[i  ] == '\r' && buf[i+1] == '\r' ) break;
		// \n\n
		if ( buf[i  ] == '\n' && buf[i+1] == '\n' ) break;
		// boundary check
		if ( i + 3 >= bufLen ) continue;
		// prepare for a larger mime size
		bsize = 4;
		// \r\n\r\n
		if ( buf[i  ] == '\r' && buf[i+1] == '\n' &&
		     buf[i+2] == '\r' && buf[i+3] == '\n'  ) break;
		// \n\r\n\r
		if ( buf[i  ] == '\n' && buf[i+1] == '\r' &&
		     buf[i+2] == '\n' && buf[i+3] == '\r'  ) break;
	}
	// return false if could not find the end of the MIME
	if ( i == bufLen ) return -1;
	return i + bsize;
}

// get content-type
char getContentType ( char *mime , int32_t mimeLen ) {
	// temp null terminate so we can call strstr
	char c = mime [ mimeLen ];
	mime [ mimeLen ] = '\0';
	// find "content-type:" field in mime
	char *s = strstr ( mime , "Content-Type:" );
	if ( ! s ) s = strstr ( mime , "content-type:" );
	if ( ! s ) s = strstr ( mime , "Content-type:" );
	if ( ! s ) s = strstr ( mime , "CONTENT-TYPE:" );
	// set back
	mime [ mimeLen ] = c;
	// if no content-type specified, it's unknown
	if ( ! s ) return CT_UNKNOWN ;
	// otherwise, is it application/pdf ?
	char *mimeEnd = mime + mimeLen;
	// skip to field data
	s += 13;
	// skip spaces
	while ( s < mimeEnd && (*s == ' ' || *s == '\t') ) s++;
	// if s passed end, we had no field data, assume not pdf
	if ( s >= mimeEnd ) return CT_UNKNOWN ;
	// is it pdf?
	if ( s + 15 < mimeEnd &&
	     strncasecmp ( s , "application/pdf" , 15 ) == 0 ) 
		return CT_PDF;
	// it it word?
	if ( s + 18 < mimeEnd &&
	     strncasecmp ( s , "application/msword",18 ) == 0 ) 
		return CT_DOC;
	// it it xls?
	if ( s + 24 < mimeEnd &&
	     strncasecmp ( s , "application/vnd.ms-excel",24 ) == 0 ) 
		return CT_XLS;
	// it it ppt?
	if ( s + 24 < mimeEnd &&
	     strncasecmp ( s , "application/mspowerpoint",24 ) == 0 ) 
		return CT_PPT;
	// it it ps?
	if ( s + 22 < mimeEnd &&
	     strncasecmp ( s , "application/postscript",22 ) == 0 ) 
		return CT_PS;
	// otherwise assume unknown even though may be text/html, etc. 
	return CT_UNKNOWN;
}

int filterContent ( char *buf , int32_t n , int32_t mimeLen , char ctype , int32_t id) {
	// write mime to stdout unaltered
	int w = fwrite ( buf , 1 , mimeLen , stdout );
	if ( w != mimeLen ) {
		// note any errors
		fprintf(stderr,"gbfilter: fwrite: %s\n",strerror(errno)); 
		return -1;
	}
	// flush it so it comes first, before filtered content
	fflush ( stdout );

	// this is set on the call from gigablast server
	char *wdir = getenv ("HOME" );

	// save the content to a file so pdftohtml,etc. can work with it
	char in[64];
	sprintf ( in , "%s/content.%"INT32"", wdir , id ); // (int32_t)getpid() );

	//fprintf(stderr,"in=%s\n",in);

	int fd = open ( in , O_CREAT | O_RDWR , S_IRWXU | S_IRWXG );
	if ( fd < 0 ) {
		fprintf(stderr,"gbfilter: open: %s\n",strerror(errno)); 
		return -1;
	}
	int32_t b = n - mimeLen ;
	if ( write ( fd , buf + mimeLen , b ) != b ) {
		close ( fd );
		fprintf(stderr,"gbfilter: write: %s\n",strerror(errno)); 
		unlink ( in );
		return -1;
	}
	close(fd);
	// . open a pipe to pdf2html program
	// . the output will go to stdout
	char cmd[128];
	// different commands to filter differt ctypes
	// -i     : ignore images
	// -stdout: send output to stdout
	// -c     : generate complex document
	// Google generates complex docs, but the large ones are horribly slow
	// in the browser, but docs with 2 cols don't display right w/o -c.
	// damn, -stdout doesn't work when -c is specified.
	// These ulimit sizes are max virtual memory in kilobytes. let's
	// keep them to 25 Megabytes
	if      ( ctype == CT_PDF ) 
		sprintf ( cmd , "ulimit -v 25000 -t 30 ; nice -n 19 %s/pdftohtml -q -i -noframes -stdout %s", wdir , in );
	else if ( ctype == CT_DOC ) 
		sprintf ( cmd , "ulimit -v 25000 -t 30 ; nice -n 19 %s/antiword %s" , wdir , in );
	else if ( ctype == CT_XLS )
		sprintf ( cmd , "ulimit -v 25000 -t 30 ; nice -n 19 %s/xlhtml %s" , wdir , in );
	else if ( ctype == CT_PPT )
		sprintf ( cmd , "ulimit -v 25000 -t 30 ; nice -n 19 %s/ppthtml %s" , wdir , in );
	else if ( ctype == CT_PS  )
		sprintf ( cmd , "ulimit -v 25000 -t 30; nice -n 19 %s/pstotext %s" , wdir , in );

	// don't use too much memory, i think xhtml uses so much that it
	// swaps out all the gb processes?
	//struct rlimit lim;
	//lim.rlim_cur = lim.rlim_max = 24 * 1024 * 1024 ;
	//if ( setrlimit ( RLIMIT_AS , &lim ) )
	//	fprintf (stderr,"gbfilter:setrlimit: %s", strerror(errno) );

	FILE *pd = popen ( cmd , "w" );
	if ( ! pd ) { 
		fprintf(stderr,"gbfilter: popen: %s\n",strerror(errno)); 
		unlink ( in );
		return -1;
	}
	// success
	pclose(pd);
	fflush ( stdout );
	// clean up the binary file from disk
	if ( unlink ( in ) == 0 ) return 0;
	fprintf(stderr,"gbfilter: unlink (%s): %s\n",in,strerror(errno)); 
	// ignore it, since it was not a processing error per se
	errno = 0;
	return 0;
}


