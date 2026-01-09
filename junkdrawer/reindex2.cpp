// Matt Wells, copyright Jan 2002

// . usage: reindex2
// . moves files resulting from running "reindex" to their right locations
// . run on host0
// . assumes files on hosts4-7 in /?/new/*

#include "gb-include.h"

#include <ctype.h>

int main ( int argc , char *argv[] ) {
	// must have big filename
	if ( argc != 2 ) {
		printf("reindex2 [this hostnum]\n");
		exit(-1);
	}
	int32_t thishostnum = atoi ( argv[1] );
	if ( thishostnum <4 || thishostnum >7 ) {
		printf("reindex2 [this hostnum]\n");
		exit(-1);
	}
	printf("using hostnum %"INT32"\n",thishostnum);

	// map of dbname to index #
	char *names[] = { "index" , "spider" , "url" , "checksum" , "title" };
	int32_t  numnames = 5;

	// 
	// gather list of all hosts/files
	//
	int32_t hosts    [600*4*4];
	char drives   [600*4*4];
	char filenames[600*4*4][64];
	//int32_t filesize [600*4*4];
	int32_t prenum   [600*4*4];
	int32_t filenum  [600*4*4];
	int32_t dbnamenum [600*4*4];
	char *ext     [600*4*4];
	int32_t next [ 8 ] [ 4 ] [ 5 ];
	int32_t count = 0;
	for ( int32_t i = 4 ; i <= 7 ;i++ ) {
		for ( char c ='a' ; c <= 'd' ; c++ ) {
			char buf[128];
			sprintf ( buf , "rsh host%"INT32" ls -1 /%c/new" , i, c );
			//sprintf ( buf , "ls -1 /%c/new" , c );
			// open pipe to read in
			FILE *fd;
			fd = popen ( buf , "r" );
			if ( ! fd ) {
				printf("reindex2: popen failed");
				return -1;
			}
			//char tmp[1024];
			while ( fgets ( filenames[count] , 64 , fd ) ) {
			  // get filename
				//sscanf ( tmp,"%*s %*s %*s %*s %"INT32" %*s %*s %*s %s", 
				//   &filesize[count], filenames[count] );
				// ref the filename
				char *f = filenames[count];
				int32_t len = gbstrlen ( f );
				f[--len] = '\0';
				// print it
				//printf("%s\n", f);
				// parse it up
				hosts  [ count ] = i;
				drives [ count ] = c;
				// parse out prenum and filenum
				int32_t *p1 = &prenum[count];
				int32_t *p2 = &filenum[count];
				int32_t *p  = p1;
				for ( int32_t j = 0 ; j < len ; j++ ) {
					if ( ! isdigit ( f[j] ) ) continue;
					char *end = &f[j+1];
					while ( isdigit ( *end ) ) end++;
					// tmp null
					char x = *end;
					*end = '\0';
					*p = atoi(&f[j]);
					j += (end - &f[j]) -1;
					*end = x;
					if ( p == p2 ) break;
					p = p2;
				}
				// parse out extension
				int32_t j = 0;
				while ( f[j] != '.' ) j++;
				ext [ count ] = &f[j+1];
				// parse out db name
				j = 0;
				while ( ! isdigit (f[j]) ) j++;
				f[j] ='\0';
				// map f to #
				int32_t k = 0;
				for (  ; k < numnames ; k++ )
					if ( strcmp ( f , names[k] ) == 0 ) {
						dbnamenum [ count ] = k;
						break;
					}
				if ( k >= numnames ) {
					printf("error2\n");
					return -1;
				}
				// last # for each dbnum/host/drive
				next [ i ] [ c -'a'] [ dbnamenum[count]] = 1;
				// print our reconstruction to verify
				sprintf(buf,
				"host%"INT32":/%c/new/%s%"INT32"db%04"INT32".%s",
					hosts  [count] , drives[count] ,
					names[dbnamenum [count]] , 
					prenum[count] , 
					filenum[count] , ext[count]    );
				//printf("%s\n",buf);
				// next file
				count++;
			}
			pclose ( fd );
		}
	}

	// print out all file names
	//for ( int32_t i = 0 ; i < count ; i++ ) 
	//	printf("host%"INT32":/%c/new/%s%"INT32"db%"INT32".%s (%"INT32",%"INT32")\n",
	//	       hosts[i],drives[i],
	//	       dbname[i], prenum[i], 
	//	       filenum[i] , ext[i]);
	// print total
	//printf("total files = %"INT32"\n", count);

	printf("echo \"ls phase done. writing rcps now\"\n");

	// populate base dir of each host 4-7
	for ( int32_t i = 0 ; i < count ; i++ ) {
		// . get all files for host #i, dir $c
		// . index*db0001.dat ...
		char buf[128];
		sprintf(buf,
			"host%"INT32":/%c/new/%s%"INT32"db%04"INT32".%s",
			hosts  [i] , drives[i] ,
			names [ dbnamenum [i]] , prenum[i] , 
			filenum[i] , ext[i]    );
		// map prenum to new host/drive/dbnum
		int32_t newhost  = 4 + prenum[i] / 4;
		char newdrive = 'a' + (prenum[i] % 4);
		int32_t *p = &next[newhost] [newdrive -'a'] [dbnamenum[i]];
		int32_t newnext = *p;
		// skip, but advance, if we're not src host
		if ( thishostnum != hosts[i] ) {
			// advance to next file #
			if ( ext[i][0]=='m') *p = *p + 2;
			continue;
		}
		// print new filename
		char buf2[128];
		sprintf(buf2,
			"host%"INT32":/%c/%sdb%04"INT32".%s",
			newhost , newdrive ,
			names [ dbnamenum [i]] , 
			newnext , ext[i]    );
		// first get destinate filesize if it exists
		// make the ls cmd first
		/*
		char buf3[128];
		sprintf ( buf3 , "rsh host%"INT32" ls -la /%c/%sdb%04"INT32".%s",
			newhost , newdrive ,
			names [ dbnamenum [i]] , 
			newnext , ext[i]    );
		// echo command
		printf("echo \"%s\"\n", buf3 );
		FILE *fd = popen ( buf3 , "r" );
		char ttt[1024];
		int32_t dorcp = 1;
		int32_t size = 0;
		if ( fgets ( ttt , 1024 , fd ) ) {
			char tmp[1024];
			  sscanf ( tmp,"%*s %*s %*s %*s %"INT32" %*s %*s %*s %*s", 
				   &size);
			  if ( size == filesize[i] ) dorcp = 0;
		}
		pclose(fd);
		// avoid rcp if we can
		if ( dorcp == 0 ) continue;
		*/
		// 
		//printf ( "%s --> %s\n", buf , buf2 );
		// now copy file if we're src host
		char buf4[128];
		sprintf ( buf4 , "rcp /%c/new/%s%"INT32"db%04"INT32".%s %s",
			  drives[i] ,
			  names [ dbnamenum [i]] , prenum[i] , 
			  filenum[i] , ext[i]    , buf2);
		// execute and wait for copy to complete
		printf ("%s\n",buf4 );
		system ( buf4 );
		//printf ("echo \"%s\"\n",buf4 );
		//system ( buf4 );
		// advance to next file #
		if ( ext[i][0]=='m') *p = *p + 2;

	}
}
