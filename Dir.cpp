#include "gb-include.h"

#include "Dir.h"

Dir::Dir ( ) {
	m_dirname = NULL;
	m_dir     = NULL;
	m_needsClose = false;
}


Dir::~Dir ( ) {
	reset();
}

void Dir::reset ( ) {
	close();
	if ( m_dirname ) free ( m_dirname );
	m_dirname = NULL;
}

bool Dir::set ( char *d1 , char *d2 ) {
	reset ();
	char tmp[1024];
	if ( gbstrlen(d1) + gbstrlen(d2) + 1 > 1024 ) {
		log("disk: Could not set directory, directory name \"%s/%s\" "
		    "is too big.",d1,d2);
		return false;
	}
	sprintf ( tmp , "%s/%s", d1 , d2 );
	return set ( tmp );
}

bool Dir::set ( char *dirname ) {
	reset ();
	m_dirname = strdup ( dirname );
	if ( m_dirname ) return true;
	log("disk: Could not set directory, directory name to \"%s\": %s.",
	    dirname,mstrerror(g_errno));
	return false;
}

bool Dir::close ( ) {
	if ( m_dir && m_needsClose ) closedir ( m_dir );
	m_needsClose = false;
	return true;
}

bool Dir::open ( ) {
	close ( );
	if ( ! m_dirname ) return false;
 retry8:
	// opendir() calls malloc
	g_inMemFunction = true;
	m_dir = opendir ( m_dirname );
	g_inMemFunction = false;
	// interrupted system call
	if ( ! m_dir && errno == EINTR ) goto retry8;

	if ( ! m_dir ) 
		g_errno = errno;

	if ( ! m_dir ) 
		return log("disk: opendir(%s) : %s",
			   m_dirname,strerror( g_errno ) );
	m_needsClose = true;
	return true;
}

// remove all files in m_dirname
bool Dir::cleanOut ( ) {
	char buf[1024];
	sprintf ( buf , "rm -r %s/*", m_dirname );
	gbsystem ( buf );
	return true;
}

// create m_dirname
bool Dir::create ( ) {
	char buf[1024];
	sprintf ( buf , "mkdir %s", m_dirname );
	gbsystem ( buf );
	return true;
}


int Dir::getNumFiles ( char *pattern ) {
	int count = 0;
	while ( getNextFilename ( pattern ) ) count++;
	return count;
}

// rewind to get the first filename
void Dir::rewind ( ) {
	rewinddir ( m_dir );
}

char *Dir::getNextFilename ( char *pattern ) {

	if ( ! m_dir ) {
		log("dir: m_dir is NULL so can't find pattern %s",pattern);
		return NULL;
	}

	struct dirent *ent;
	int32_t plen = gbstrlen ( pattern );
	while ( (ent = readdir ( m_dir ))  ) {
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

// must call set first
// not recursive!
// return -1 on error
int64_t Dir::getUsedSpace ( ) {

	rewinddir ( m_dir );

	int64_t total = 0;
	struct dirent *ent ;
	while ( ( ent = readdir ( m_dir ) )  )  {
		File tmpfile;
		tmpfile.set ( ent->d_name );
		int64_t space = tmpfile.getFileSize ( );
		if ( space > 0 ) total += space;
	}
		
	return total;
}

// . replace the * in the pattern with a unique id from getNewId()
char *Dir::getFullName ( char *filename ) {
	static char buf[1024];
	sprintf ( buf , "%s/%s", m_dirname , filename );
	return buf;
}

// . replace the * in the pattern with a unique id from getNewId()
char *Dir::getNewFilename ( char *pattern ) {
	int64_t id = getNewId ( pattern );
	static char buf[1024];
	strcpy ( buf , m_dirname );
	int j = gbstrlen ( buf );
	for ( int i = 0 ; pattern[i] ; i++ ) {
		if ( pattern[i] != '*' ) {buf[j++] = pattern[i]; continue;}
		sprintf ( &buf[j] , "%"INT64"" , id );
		j = gbstrlen ( buf );
	}
	buf[j++] = '\0';
	return buf;
}

// . a highly specialized function
// . gets a new id represented by files of pattern "pattern"
// . if pattern is "*.data" will return the LUB for *, a int64_t
int64_t Dir::getNewId ( char *pattern ) {

	rewinddir ( m_dir );

	char *filename ;
	int64_t   lub = 0;
	while ( (filename = getNextFilename ( pattern ))  ) {
		int64_t id = getFileId ( filename );
		if ( id >= lub ) lub = id + 1;
	}	

	return lub;
}

// . another highly specialized function
// . expects filename to begin with a number
// . return -1 if none exists
int64_t Dir::getFileId ( char *filename ) {

	int end = gbstrlen ( filename ) -1;
	while ( end >= 0 && filename [ end ] != '.' ) end--;
	if ( end < 0 ) return -1;
	end--;
	while ( end >= 0 && isdigit ( filename [ end ] ) ) end--;
	// now 3 cases:
	// 1. end  = -1 and filename[0] is NOT a digit
	// 2. end  = -1 and filename[0] is a digit
	// 3. end >=  0 and filename[end+1] is a digit
	if   ( end < 0 && ! isdigit ( filename[0] ) ) return -1;
	if   ( end < 0 ) end = 0;
	else             end++;
	int64_t id = -1;
	sscanf ( filename + end , "%"INT64"." , & id );
	return id;
}
