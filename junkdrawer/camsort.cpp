#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>      // for opendir
#include <dirent.h>         // for opendir
#include <sys/types.h>

#include <errno.h>
#include <string.h>

char *getNextFilename ( DIR *dir , char *pattern ) ;

int main () {

	// open the dir
	char *dirname = ".";
	DIR *dir ;
	dir = opendir ( dirname );
	if ( ! dir ) {
		fprintf(stderr,"disk: opendir(%s) : %s",
			dirname,strerror( errno ) );
		return -1;
	}

	// scan each file
	rewinddir ( dir );
	char saved[1024];

 loop:
	char *f = getNextFilename ( dir , "*" );

	if ( ! f ) return 0;

	// get the date from it
	char *p = NULL;
	if ( ! p ) p = strstr (f,"2008" ) ;
	if ( ! p ) p = strstr (f,"2009" ) ;
	if ( ! p ) p = strstr (f,"2010" ) ;
	if ( ! p ) p = strstr (f,"2011" ) ;
	if ( ! p ) p = strstr (f,"2012" ) ;
	if ( ! p ) p = strstr (f,"2013" ) ;
	if ( ! p ) p = strstr (f,"2014" ) ;
	if ( ! p ) p = strstr (f,"2015" ) ;
	if ( ! p ) p = strstr (f,"2016" ) ;
	if ( ! p ) p = strstr (f,"2017" ) ;
	if ( ! p ) goto loop;

	// get the full date
	char buf[1024];
	sprintf ( buf , "mkdir %.8s", p );
	// don't repeat it
	if ( strcmp ( buf , saved ) != 0 ) {
		fprintf(stderr,"%s\n",buf);
		system ( buf );
		strcpy ( saved , buf );
	}

	// store in there
	sprintf( buf , "mv %s %.8s/",f,p);
	fprintf(stderr,"%s\n",buf);
	system(buf );

	goto loop;
}

char *getNextFilename ( DIR *dir , char *pattern ) {

	struct dirent *ent;
	int32_t plen = gbstrlen ( pattern );
	while ( (ent = readdir ( dir ))  ) {
		char *filename = ent->d_name;
		if ( ! pattern ) return filename;
		if ( plen>2 && pattern[0] == '*' && pattern[plen-1] == '*' ) {
			pattern[plen-1]='\0';
			char *s = strstr ( filename , pattern+1 ) ;
			pattern[plen-1]='*';
			if ( ! s ) continue;
			else       return filename;
		}
		if ( pattern[0] == '*' ) {
			if ( gbstrlen(filename) < gbstrlen(pattern + 1) ) continue;
			char *tail = filename + 
				gbstrlen ( filename ) - 
				gbstrlen ( pattern ) + 1;
			if ( strcmp ( tail , pattern+1) == 0 ) return filename;
		}
		if ( pattern[plen-1]=='*' ) {
			if ( strncmp ( filename , pattern , plen - 1 ) == 0 )
				return filename;
		}
	}

	return NULL;
}

