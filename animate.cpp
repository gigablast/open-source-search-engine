#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>      // for opendir()
#include <dirent.h>         // for opendir()
#include <time.h>           // for time()
#include <ctype.h>
#include <sys/socket.h>  // inet_ntoa()
#include <netinet/in.h>  // inet_ntoa()
#include <arpa/inet.h>   // inet_ntoa()
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

bool     hashinit () ;
uint32_t hash32 ( const char *s, int32_t len ) ;
int32_t     mysystem ( char *cmd );

#define HAS_SM_GIF   0x01
#define HAS_LG_GIF   0x02
#define HAS_LG_ANIM  0x04
#define HAS_SM_ANIM  0x08
#define HAS_ROOT     0x10
#define IS_PARENT    0x20

class Entry {
public:
	// includes extension
	char  m_root[64];
	// the actual filename as it was uploaded
	char m_filename[90];
	// the parsed out date in MILLIseconds since epoch
	int64_t m_timestamp;
	// the camera name, NULL terminated
	char *m_camera;
	int32_t  m_cameraLen;
	// the hash of the filename excluding extension
	uint32_t m_h;
	// and the flags
	uint8_t m_flags;
	// when the base file was created (uploaded by camera)
	int32_t m_ctime;
	// int16_tcuts
	int32_t m_year;
	int32_t m_month;
	int32_t m_day;
	// linked list
	class Entry *m_next;
};

void main2 ( char *dirname ) ;

int main ( int argc , char *argv[] ) {

	system("echo RUNNING > /tmp/msg");

	char *dirname = NULL;
	if ( argc < 2 ) 
		dirname = "/home/camera2/";
	else
		dirname = argv[1];
	// sanity
	if ( ! dirname || dirname[0]!='/' ) {
		fprintf(stderr,"animate: bad dir name");
		return -1;
	}
	char lock[200];
	sprintf ( lock , "%s/lockfile",dirname);
	// check lock file
	int fd = open ( lock , O_CREAT | O_EXCL );
	// if exists, it fails
	if ( fd < 0 ) {
		fprintf(stderr,"animate: %s exists",lock);
		exit(-1);
	}
	main2 ( dirname );
	// remove lock
	unlink ( lock );
	return 0;
}

void main2 ( char *dirname ) {

	// open the dir and scan for files
	DIR *edir = opendir ( dirname );
	if ( ! edir ) {
		fprintf ( stderr, "animate: opendir (%s):%s\n",
				   dirname,strerror( errno ) );
		return;
	}

	// start with a million buckets, must be power of 2
	int32_t numBuckets = 1024 * 1024;
	// make the hash table
	Entry *buckets = (Entry *)calloc ( sizeof(Entry) * numBuckets , 1 );
	if ( ! buckets ) { 
		fprintf(stderr,"animate: could not alloc hash table\n"); 
		return;
	}

	// . are we in a subdir already? or are we in /home/camera2/?
	// . if we are not in a subdir already we move everything into the
	//   subdir which is of the format yyyymmdd/ for time keeping purposes
	char issubdir = false;
	char *dend = dirname + gbstrlen(dirname) ;
	// backup over \0
	dend--;
	if ( *dend == '/' ) dend--;
	while ( dend > dirname && *dend != '/' ) dend--;
	if ( *dend == '/' ) dend++;
	if ( dend[0]=='2' && dend[1]=='0' )
		issubdir = true;

	hashinit();

	Entry *head = NULL;
	Entry *tail = NULL;

	// hash subdir names into this table
	uint32_t bb[1024 * 32];
	memset ( bb , 0 , 10000 * 8 );

	int32_t now = time(NULL);

	// loop over all the log files in this directory
	struct dirent *ent;
	while ( (ent = readdir ( edir ))  ) {
		// get filename of that entry
		char *filename = ent->d_name;

		// get extension
		char *ext = strstr ( filename , "." );

		// hash subdir names!
		if ( ! ext ) { // ent->m_issubdir ) {
			// must be this, "20081123"
			if ( gbstrlen(filename) != 8 ) continue;
			// hash it
			uint32_t h = hash32 ( filename , 8 );
			// add to "bb" table
			uint32_t n = h & (1024*32-1);
			// chain it
			while ( bb[n] && bb[n] != h ) 
				if ( ++n >= 1024*32 ) n = 0;
			// add it 
			bb[n] = h;
		}

		// skip if no extension
		if ( ! ext ) continue;

		// skip these headers, but flags this frame
		// as having anims or thumbs as appropriate
		if ( strncmp(filename,"ANIM"  , 4) == 0 ) continue;
		if ( strncmp(filename,"THUMB" , 5) == 0 ) continue;

		// do we already have the big gif?
		char flags = 0;
		if ( strcmp(ext,".gif") == 0 ) flags |= HAS_LG_GIF;

		//
		// get the bucket
		//

		// get length w/o extension
		int32_t len = ext - filename;
		// must be . or ..
		if ( len == 0 ) continue;
		// hash it
		uint32_t h = hash32 ( filename , len );
		// debug
		//fprintf(stderr,"filename %s h=%"UINT32"\n",filename,
		//(uint32_t)h);
		// never allow 0, that means empty bucket
		if ( h == 0LL ) h = 1LL;
		// get it
		int32_t i = h & (numBuckets - 1);
		// chain
		while ( buckets[i].m_h && buckets[i].m_h != h ) 
			if ( ++i == numBuckets ) i = 0;
		// was it found?
		bool found = false;
		if ( buckets[i].m_h ) found = true;
		// int16_tcut
		Entry *e = &buckets[i];

		// and add in flags
		e->m_flags |= flags;
		// and hash, might already be set!
		e->m_h = h;

		// if we are a gif, we are done now!
		if ( flags == HAS_LG_GIF ) continue;

		// parse filename into date stuff
		char *d = filename;
		while ( *d && d[0] != '2' && d[1] != '0' ) d++;
		// strange!
		if ( ! *d ) {
			//fprintf(stderr,"animate: strange filename %s . "
			//	"ignoring.\n",filename);
			continue;
		}

		// get camera name length
		e->m_cameraLen = d - filename;

		// get year
		int32_t y = 
			(d[0]-'0') * 1000 + 
			(d[1]-'0') * 100  + 
			(d[2]-'0') * 10   + 
			(d[3]-'0');
		// get month
		int32_t mon = 
			(d[4]-'0') * 10   + 
			(d[5]-'0');
		// sanity
		if ( mon <= 0 || mon > 12 ) {
			//fprintf(stderr,"animate: strange month "
			//	"filename %s . ignoring.\n",filename);
			continue;
		}
		// get day
		int32_t day =
			(d[6]-'0') * 10   + 
			(d[7]-'0');
		// sanity
		if ( day <= 0 || day > 31 ) {
			fprintf(stderr,"animate: strange day "
				"filename %s . ignoring.\n",filename);
			continue;
		}
		// save it 
		e->m_year  = y;
		e->m_month = mon;
		e->m_day   = day;

		// point to what we got left
		char *p = d + 8;

		// stupid cams put an "s" in here during daylight savings
		char *delme = NULL;
		if ( *p == 's' ) { delme = p; p++; }

		// now the hour
		int32_t hour = 
			(p[0]-'0') * 10   + 
			(p[1]-'0');
		// sanity
		if ( hour < 0 || hour > 23 ) {
			fprintf(stderr,"animate: strange hour "
				"filename %s . ignoring.\n",filename);
			continue;
		}
		// minute
		int32_t min =
			(p[2]-'0') * 10   + 
			(p[3]-'0');
		// sanity
		if ( min < 0 || min >= 60  ) {
			fprintf(stderr,"animate: strange minute "
				"filename %s . ignoring.\n",filename);
			continue;
		}
		// then milliseconds
		char *MM = p + 4;
		while ( *MM && isdigit(*MM) ) MM++;
		// must be a '.'
		if ( *MM != '.' ) {
			fprintf(stderr,"animate: strange timestamp for "
				"filename %s . ignoring.\n",filename);
			continue;
		}


		// get that as numeric
		*MM = '\0';

		int32_t ms = atoi(p);
		// now convert to seconds since epoch
		tm ts1;
		memset(&ts1, 0, sizeof(tm));
		ts1.tm_mon  = mon - 1;
		ts1.tm_mday = day;
		ts1.tm_year = y - 1900;
		// make the time
		int64_t timestamp = mktime(&ts1);
		// add in time since start of day in seconds
		timestamp += hour * 60 * 60;
		timestamp += min  * 60;
		// convert into milliseconds since epoch
		timestamp *= 1000;
		// then add in milliseconds
		timestamp += ms;
		// store it
		e->m_timestamp = timestamp;

		// debug
		//fprintf(stderr,"animate: timestamp %s = %"INT64"\n",filename,timestamp);

		// store filename, might already be set
		char *src = filename;
		// filename without the extension really
		char *dst = e->m_root;
		for ( ; *src ; src++ ) {
			// skip that 's' that the web cams put in for daylight savings time
			if ( *src=='s' && src>filename && isdigit(src[-1]) && 
			     isdigit(src[1]) && src==delme ) continue;
			// otherwise, copy over exactly
			*dst++ = *src;
		}
		*dst = '\0';

		// sanity
		if ( strncmp(e->m_root,"ANIM",4)==0 ||
		     strncmp(e->m_root,"THUMB-ANIM",4)==0 ) {
			//fprintf(stderr,"animate: bad filename "
			//	"%s ignoring.\n",filename);
			continue;
		}
		// truncate the filename to remove the extension
		char *ext2 = strstr(e->m_root,".");
		if ( ext2 ) *ext2 = '\0';


		// add it to the linked list
		//fprintf(stderr,"animate: adding h=%"UINT32" file=%s ts=%"INT64"\n",
		//	(int32_t)e->m_h,filename,e->m_timestamp);
		if ( tail ) { tail->m_next = e; tail = e; }
		else        head = tail  = e;

		// our camera name, length was set above
		e->m_camera    = e->m_root;

		// save the original filename
		sprintf ( e->m_filename,"%s",filename);

		// do not rename as it could be uploading!
		/*
		// rename the file in order to remove the 's'
		if ( delme ) {
			char buf[500];
			// generate the LARGE gif
			sprintf ( buf , "mv %s/%s.jpg %s/%s.jpg",
				  dirname   ,
				  filename  ,
				  dirname   ,
				  e->m_root );
			// execute it
			if ( mysystem ( buf ) == -1 ) return;
		}
		*/

		// get the creation date
		struct stat stats;
		// make it
		char sfile[1024];
		//sprintf(sfile,"%s\%s.jpg",dirname,filename);
		sprintf(sfile,"%s\%s.jpg",dirname,e->m_filename);//root);
		if ( flags == 0 && stat(sfile, &stats) ) {
			fprintf(stderr,"animate: could not stat %s. ignoring.\n",sfile);
			continue;
		}
		// save creation time
		e->m_ctime = stats.st_ctime;
		// sanity check
		if ( e->m_ctime == 0 ) { char *xx=NULL;*xx=0; }

		// add the flags
		e->m_flags |= HAS_ROOT;


		//fprintf(stderr,"animate: ctime %s = %"INT32"\n",filename,e->m_ctime);
	}

	// sort the entries by their timestamp

	bool swapped = true;
	while ( swapped ) {
		// assume no more swaps
		swapped = false;
		// loop over all entries
		Entry *next = NULL; if ( head ) next = head->m_next;
		for ( Entry *e = head ; e ; e = next ) {
			// point to the next entry 
			next = e->m_next;
			// stop if we are the last entry
			if ( ! next ) continue;
			// continue if we are in order in regards to next entry
			if ( next->m_timestamp >= e->m_timestamp ) continue;
			// otherwise, swap us with the next entry
			swapped = true;
			Entry tmp;
			Entry *a = e;
			Entry *b = next;
			// these must be preserved and not affected by gbmemcpy()
			Entry *anext = a->m_next;
			Entry *bnext = b->m_next;
			gbmemcpy ( &tmp , a    , sizeof(Entry) );
			gbmemcpy ( a    , b    , sizeof(Entry) );
			gbmemcpy ( b    , &tmp , sizeof(Entry) );
			// preserve
			a->m_next = anext;
			b->m_next = bnext;
		}
	}



	// now loop over entries
	for ( Entry *e = head ; e ; e = e->m_next ) {
		// skip if already has a large gif
		if ( e->m_flags & HAS_LG_GIF ) continue;
		// get our local time
		int32_t now = time(NULL);
		// skip if too young! allow it to finish uploading, give
		// it 2 minutes
		if ( now - e->m_ctime < 120 ) continue;
		// generate the LARGE gif
		char buf[1024];
		sprintf ( buf , "jpegtopnm %s/%s.jpg 2> /dev/null | "
			  "ppmquant 256 2> /dev/null| ppmtogif > "
			  "%s/%s.gif 2> /dev/null",
			  dirname,
			  //e->m_root,
			  e->m_filename,
			  dirname,
			  e->m_root );
		// execute it
		if ( mysystem ( buf ) == -1 ) return;
		// mark it
		e->m_flags |= HAS_SM_GIF ;
	}

	// . animate the small gif thumbs
	// . CAUTION: make sure timestamp of entry is at least 20 seconds old before
	//   animating to avoid cutting animations int16_t!
	for ( Entry *e = head ; e ; e = e->m_next ) {
		// note it
		//fprintf(stderr,"animate: file=%s ts=%"UINT64"\n",
		//	e->m_root,e->m_timestamp);

		// skip if already done
		if ( e->m_flags & HAS_LG_ANIM ) continue;
		if ( e->m_flags & HAS_SM_ANIM ) continue;

		// needs to have the large gif in there!
		if ( ! (e->m_flags & HAS_LG_GIF) ) continue;

		// need to have the root .jpg there!
		if ( ! (e->m_flags & HAS_ROOT) ) continue;

		if ( e->m_ctime == 0 ) { char *xx=NULL;*xx=0; }

		// . skip if too recent, give it 2 minutes
		// . we need to wait for it to finish uploading and for
		//   all its brothers to finish too!
		if ( now - e->m_ctime <= 5*60 ) continue;
		// get our time according to camera
		int64_t r = e->m_timestamp;
		// get our camera name and length
		char *c    = e->m_camera;
		int32_t  clen = e->m_cameraLen;

		int64_t min  = r;
		int64_t max  = r;

		// hard limit of 30 seconds
		int64_t hardmin = r - 1000 * 30;
		int64_t hardmax = r + 1000 * 30;

		char abort = 0;

	subloop:

		// assume we are the parent, but pick min!
		Entry *parent = NULL;

		Entry *list [10000];
		int32_t n = 0;

		char flag = 0;
		// ok, find the guys closest to our timestamp
		for ( Entry *k = head ; k ; k = k->m_next ) {
			// check his time
			if ( k->m_timestamp < min - 10000 ) continue;
			if ( k->m_timestamp > max + 10000 ) continue;
			// hard limits
			if ( k->m_timestamp < hardmin ) continue;
			if ( k->m_timestamp > hardmax ) continue;
			// must match cam name, skip if not
			if ( k->m_cameraLen != clen     ) continue;
			if ( strncmp(k->m_camera,c,clen) ) continue;
			// skip if too recent, 1 minute
			if ( now - k->m_ctime <= 1*60 ) {
				abort = 1;
				break;
			}
			// or if he ain't got a big gif yet! this is a better
			// check than checking k->m_ctime above...
			if ( ! (k->m_flags & HAS_LG_GIF) ) {
				abort = 1;
				break;
			}
			// we got a brother
			if ( k->m_timestamp < min ) {
				min  = k->m_timestamp;
				flag = 1;
			}
			if ( k->m_timestamp > max ) {
				max  = k->m_timestamp;
				flag = 1;
			}
			// add it for animation
			list[n++] = k;
		}

		// if ANY one of our brothers is too close to the current time, 
		// then wait a little longer
		if ( abort ) continue;

		// if we increased our range, keep going
		if ( flag ) goto subloop;

		/*
		// point to them so we can sort them by their timestamps!
		for ( Entry *k = head ; k ; k = k->m_next ) {
			// skip if not in range
			if ( k->m_timestamp < min ) continue;
			if ( k->m_timestamp > max ) continue;
			// must match cam name, skip if not
			if ( k->m_cameraLen != clen     ) continue;
			if ( strncmp(k->m_camera,c,clen) ) continue;
			// pick the smallest
			if ( k->m_timestamp == min ) parent = k;
			// add it in
			list[n++] = k;
		}
		*/

		// mark the parent
		parent->m_flags |= IS_PARENT;

		// bubble sort the entries
		char swapped = 1;
		while ( swapped ) {
			swapped = 0;
			for ( int32_t i = 1 ; i < n ; i++ ) {
				if ( list[i-1]->m_timestamp <= 
				     list[i  ]->m_timestamp ) continue;
				Entry *tmp = list[i-1];
				list[i-1] = list[i];
				list[i  ] = tmp;
				swapped = 1;
			}
		}

		char  buf[330000];
		char *bend = buf + 330000;
		char  big  = false;

	subloop2:

		char *res  = "--resize-width 100 ";
		if ( big ) res = "";

		// make the thumbnail animation, 100 is the thumbnail x scale
		char *cmd  = buf;
		sprintf ( cmd , "gifsicle %s"
			  " --delay=10 --colors 256 --loop " ,
			  res);
		cmd += gbstrlen(cmd);

		for ( int32_t i = 0 ; i < n ; i++ ) {
			// get it
			Entry *k = list[i];
			// ok, mark it as processed
			if ( big ) k->m_flags |= HAS_LG_ANIM;
			else       k->m_flags |= HAS_SM_ANIM;
			// store it
			sprintf(cmd,"%s/%s.gif ",
				dirname,k->m_root);
			cmd += gbstrlen(cmd);
		}

		// sanity
		if ( cmd + 1000 > bend ) {
			fprintf(stderr,"animate: buf too small\n");
			char *xx=NULL;*xx=0;
		}
		if ( ! parent ) { char *xx=NULL;*xx=0; }
		// if big gif change this
		char *prefix = "";
		if ( ! big ) prefix = "THUMB";
		// finish it up
		sprintf ( cmd , " > %s/%sANIM-%s.gif 2> /dev/null" ,
			  dirname,prefix,parent->m_root);

		// execute it
		if ( mysystem ( buf ) == -1 ) return;

		// make the big gif animation
		if ( ! big ) {
			big = true;
			goto subloop2;
		}

		// 
		// all done making the large and small animations
		//

		// now delete all the big gifs we used in gifsicle
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// get it
			Entry *k = list[i];
			// delete the big gif
			sprintf( buf ,
				 "rm %s/%s.gif",
				 dirname,
				 k->m_root);
			// execute it
			if ( mysystem ( buf ) == -1 ) return;
		}


		// make a small gif just for the root!
		sprintf ( buf , "jpegtopnm %s/%s.jpg 2> /dev/null | "
			  "pnmscale -xysize 100 100 | "
			  "ppmquant 256 2> /dev/null| ppmtogif > "
			  "%s/THUMB-%s.gif 2> /dev/null",
			  dirname,
			  parent->m_filename,//parent->m_root ,
			  dirname,
			  parent->m_root );
		// execute it
		if ( mysystem ( buf ) == -1 ) return;


		// now move the original jpegs into a subdir
		sprintf ( buf ,  "mkdir %s/ANIM-%s/ " , dirname,parent->m_root);
		// execute it
		if ( mysystem ( buf ) == -1 ) return;

		// move each jpeg we used into there!
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// get it
			Entry *k = list[i];
			// store it in its jpeg subdir
			sprintf( buf ,
				 "mv %s/%s.jpg %s/ANIM-%s/",
				 dirname,
				 k->m_filename,//k->m_root,
				 dirname,
				 parent->m_root);
			// execute it
			if ( mysystem ( buf ) == -1 ) return;
		}

	}




	// loop over all entries and create the subdir
	for ( Entry *e = head ; ! issubdir && e ; e = e->m_next ) {
		// make the dirname for this entry
		char subdir[128];
		sprintf(subdir,"%"INT32"%02"INT32"%02"INT32"", e->m_year,
			e->m_month,e->m_day);
		// hash it
		uint32_t h = hash32 ( subdir , 8 );
		// have we checked this guy already? or does it exist?
		uint32_t n = h & (1024*32-1);
		// chain it
		while ( bb[n] && bb[n] != h ) 
			if ( ++n >= 1024*32 ) n = 0;
		// skip if we have checked or it does exist
		if ( bb[n] == h ) continue;
		// don't repeat it
		bb[n] = h;
		// make the dir!
		char  buf[1000];
		sprintf ( buf , 
			  "mkdir %s/%s/ ; "
			  // index2.php does not have a meta redirect to the current date subdi
			  // which index.php does have
			  "cp %s/index2.php %s/%s/index.php ; "
			  "cp %s/scripts.js %s/%s/scripts.js ; "
			  "cp %s/styles.css %s/%s/styles.css",
			  dirname,
			  subdir,

			  dirname,
			  dirname,
			  subdir,

			  dirname,
			  dirname,
			  subdir,

			  dirname,
			  dirname,
			  subdir );

		// execute it
		if ( mysystem ( buf ) == -1 ) return;
	}

	// . move files to their appropriate subdirs
	// . do not move files with their "RECENT" flag set
	for ( Entry *e = head ; ! issubdir && e ; e = e->m_next ) {
		if ( ! (e->m_flags & HAS_LG_ANIM ) ) continue;
		// only move if parent, the kids wered moved into the ANIM-* subdir already
		if ( ! (e->m_flags & IS_PARENT ) ) continue;
		// make the dirname for this entry
		char subdir[128];
		sprintf(subdir,"%"INT32"%02"INT32"%02"INT32"", e->m_year,
			e->m_month,e->m_day);
		// move it all!
		char buf[128];
		sprintf(buf,"mv %s/*%s* %s/%s/", 
			dirname,
			e->m_root,
			dirname ,
			subdir );
		// execute it
		if ( mysystem ( buf ) == -1 ) return;
		// again for files with an 's' in them
		sprintf(buf,"mv %s/*%s* %s/%s/", 
			dirname,
			e->m_filename,//e->m_root,
			dirname ,
			subdir );
		// execute it
		if ( mysystem ( buf ) == -1 ) return;
	}
}

int32_t mysystem ( char *cmd ) {
	// log it as well
	fprintf(stderr,"%s\n",cmd );
	// skip for now
	//return 1;
	// execute it
	if ( system ( cmd ) == -1 ) {
		fprintf(stderr,"animate: system call failed\n");
		return -1;
	}
	return 1;
}


uint64_t g_hashtab[256][256] ;

bool hashinit () {
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;
	// show RAND_MAX
	//printf("RAND_MAX = %"UINT32"\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( int32_t i = 0 ; i < 256 ; i++ )
		for ( int32_t j = 0 ; j < 256 ; j++ ) {
			g_hashtab [i][j]  = (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
		}
	//if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;
	s_initialized = true;
	return true;
}

uint32_t hash32 ( const char *s, int32_t len ) {
	uint32_t h = 0;
	int32_t i = 0;
	int32_t j = 0;
	for ( ; i < len ; i++ ) {
		// skip stupid s from daylight savings time
		if ( s[i]=='s' && i>0 && isdigit(s[i-1]) && isdigit(s[i+1]))
			continue;
		h ^= (uint32_t) g_hashtab [(unsigned char)j]
			[(unsigned char)s[i]];
		j++;
	}
	return h;
}

