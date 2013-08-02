// JAB: this is required for pwrite() in this module
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "File.h"
#include "Threads.h"

// THE FOLLOWING IS ALL STATIC 'CUZ IT'S THE FD POOL
// if someone is using a file we must make sure this is true...
static int       s_isInitialized = false;

// We have up to 5k virtual descriptors, each is mapped to a real descriptor 
// or -1. We gotta store the filename to re-open one if it was closed.
// 5 ints = 20 bytes = 20k
static int       s_fds           [ MAX_NUM_VFDS ]; // the real fd
                                                   // -1 means not opened
                                                   // -2 means available
//static char   *s_filenames     [ MAX_NUM_VFDS ]; // in case we gotta re-open
static long long s_timestamps    [ MAX_NUM_VFDS ]; // when was it last accessed
static char      s_writing       [ MAX_NUM_VFDS ]; // is it being written to?
static char      s_unlinking     [ MAX_NUM_VFDS ]; // is being unlinked/renamed

// . how many open files are we allowed?? hardcode it! 
// . rest are used for sockets 
// . we use 512 for sockets as of now
// . this linux kernel has 1024 fd's
// . i saw the tcp server using 211 sockets when spidering, must be doing
//   a lot of robots.txt lookups! let's set this down from 800 to 500
static int       s_maxNumOpenFiles = 500;
static int       s_numOpenFiles    = 0;

// . keep track of number of times an fd was closed
// . so if we do a read on an fd, and it gets unlinked and a new file opened
//   with that same fd, we know it, and can compensate in BigFile.cpp for it
// . here is the updated sequence:
//   a. read begins with fd1
//   -> read stores s_closeCounts[fd1] in FState
//   b. we close fd1
//   -> we do s_closeCounts[fd1]++
//   c. we open another file with fd1
//   d. read reads the wrong file!
//   -> s_closeCounts[fd1] changed so g_errno is set.
// . UPDATE: now we just inc s_closeCounts[fd1] write after calling ::open().
//           Since ::open() is never called in a thread, this should be ok,
//           because i now call ::close1_r() in the unlink or rename thread.

#include "Loop.h" // MAX_NUM_FDS
static long s_closeCounts [ MAX_NUM_FDS ];

// for avoiding unlink/opens that mess up our threaded read
long getCloseCount_r ( int fd ) {
	if ( fd < 0 ) return 0;
	return s_closeCounts [ fd ];
}

// return -1 if not opened, otherwise, return the opened fd
/*
void File::incCloseCount_r ( ) {
	if ( m_vfd < 0 ) return;
	int fd = s_fds [ m_vfd ];
	if ( fd < 0 ) return;
	s_closeCounts [ fd ]++;
}
*/

File::File ( ) {
	m_vfd = -1; 
	// initialize m_maxFileSize and the virtual fd table
	if ( ! s_isInitialized ) initialize ();
	// we are not being renamed
	//m_oldFilename[0] = '\0';
	// threaded unlink sets this to true before spawning thread so we
	// do not try to open it!
	//m_gone = 0;
}


File::~File ( ) {
	close ();
}

void File::set ( char *dir , char *filename ) {
	if ( ! dir ) { set ( filename ); return; }
	char buf[1024];
	sprintf ( buf , "%s/%s" , dir , filename );
	set ( buf );
}

void File::set ( char *filename ) {
	// reset m_filename
	m_filename[0] = '\0';
	// return if NULL
	if ( ! filename ) { 
		log ( LOG_LOGIC,"disk: Provided filename is NULL");
		return;
	}
	// bail if too long
	long len = gbstrlen ( filename );
	// account for terminating '\0'
	if ( len + 1 >= MAX_FILENAME_LEN ) { 
		log ( "disk: Provdied filename %s length of %li is bigger "
		      "than %li.",filename,len,(long)MAX_FILENAME_LEN-1); 
		return; 
	}
	// if we already had another file open then we must close it first.
	if ( m_vfd >= 0 ) close();
	// copy into m_filename and NULL terminate
	memcpy ( m_filename , filename , len );
	m_filename [ len ] = '\0';
	// TODO: make this a bool returning function if ( ! m_filename ) g_log
}

bool File::rename ( char *newFilename ) {
	// close ourselves if we were open... why? historical reasons?
	close();
	// do the rename
	if ( ::rename ( m_filename , newFilename ) != 0 ) return false;
	// sync it to disk in case power goes out
	sync();
	//return log("file::rename: from %s to %s failed", 
	//m_filename , newFilename );
	// set to our new name
	set ( newFilename );
	return true;
}

// . open the file
// . only call once per File after calling set()
bool File::open ( int flags , int permissions ) {
	// if we already had another file open then we must close it first.
	if ( m_vfd >= 0 ) {
		log(LOG_LOGIC,
		    "disk: Open already called. Closing and re-opening.");
		close();
	}
	// save these in case we need to reopen in getfd()
	m_flags       = flags;
	m_permissions = permissions;
	// sanity check
	//long ss = 0;
	//for ( long i = 0 ; i < MAX_NUM_VFDS ; i++ ) 
	//	if (s_fds [ i ] >= 0 && s_writing[i] ) ss++;
	//log("got %li doing writes",ss);
	
	// we must assign this to a virtual descriptor
	// scan down our list looking for an m_fd of -2 (available) [-1 means 
	//    used but but not really open]
	int i;
	for ( i = 0 ; i < MAX_NUM_VFDS ; i++ ) if (s_fds [ i ] == -2 ) break;
	// can these fools use all 5k fd's?
	if ( i >= MAX_NUM_VFDS ) {
		g_errno = EBADENGINEER;
		return log ( 
			     "disk: All %li virtual fd's are in use. Panic.",
			     (long)MAX_NUM_VFDS);
	}
	// remember OUR virtual file descriptor for successive calls to 
	// read/write/...
	m_vfd      = i;
	// we are not open at this point, but no longer available at least
	s_fds [ m_vfd ] = -1;
	// open for real, return true on success
	if ( getfd () >= 0 ) return true;
	// . close the virtual fd so we can call open again
	// . sets s_fds [ m_vfd ] to -2 (available)
	// . and sets our m_vfd to -1
	close();
	// otherwise bitch and return false
	return log("disk: open: %s",mstrerror(g_errno));
}

// . returns number of bytes written
// . returns -1 on error
// . may return < numBytesToWrite if non-blocking
int File::write ( void *buf             , 
		  long  numBytesToWrite , 
		  long  offset          ) {
	// safety catch!
	if ( g_conf.m_readOnlyMode ) {
		logf(LOG_DEBUG,"disk: Trying to write while in "
		     "read only mode.");
		return -1;
	}
	// this return -2 if never opened, -1 on error, fd on success
	int fd = getfd();
	if ( fd < 0 ) { 
		g_errno = EBADENGINEER; 
		log("disk: write: fd is negative");
		return -1; 
	}
	// write it
	int n;
 retry21:
	if ( offset < 0 ) n = ::write ( fd , buf , numBytesToWrite );
	else              n =  pwrite ( fd , buf , numBytesToWrite , offset );
	// valgrind
	if ( n < 0 && errno == EINTR ) goto retry21;	
	// copy errno to g_errno
	if ( n < 0 ) g_errno = errno;
	// cancel blocking errors - not really errors
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }
	// log an error
	if ( n < 0 ) 
		log("disk: write(%s) : %s" ,  
		    m_filename/*s_filenames[m_vfd]*/, strerror ( g_errno ) );
	return n;
}

int File::read ( void *buf            , 
		 long  numBytesToRead , 
		 long  offset         ) {
	// this return -2 if never opened, -1 on error, fd on success
	int fd = getfd();
	if ( fd < 0 ) { 
		g_errno = EBADENGINEER; 
		log("disk: read: fd is negative");
		return -1; 
	}
	// do the read
	int n ;
 retry9:
	if ( offset < 0 ) n = ::read  ( fd , buf , numBytesToRead );
	else              n =  pread  ( fd , buf , numBytesToRead , offset );
	// valgrind
	if ( n < 0 && errno == EINTR ) goto retry9;	
	// copy errno to g_errno
	if ( n < 0 ) g_errno = errno;
	// cancel blocking errors - not really errors
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }
	if ( n < 0 ) 
		log("disk: read(%s) : %s" ,  
		    m_filename/*s_filenames[m_vfd]*/, strerror ( g_errno ) );
	return n;
}

// uses lseek to get file's current position
long File::getCurrentPos ( ) {
	return (long) ::lseek (s_fds[m_vfd] , 0 , SEEK_CUR );
}

bool File::isNonBlocking () {
	// return true if never opened! 
	if ( m_vfd < 0 ) return false;
	// what was the actual file descriptor it represented?
	int fd = s_fds [ m_vfd ];
	// always block on a close
	int flags = fcntl ( fd , F_GETFL ) ;
	// return true if non-blocking
	return ( flags & O_NONBLOCK );
}

// . BigFile calls this from inside a rename or unlink thread
// . it calls File::close() proper when out of the thread
// . PROBLEM #1: we close this fd, an open happens for the fd we just closed
//               and a pending read reads from the wrong fd. to fix this
//               i inc'd s_closeCountds[fd] right after the call to ::open()
//               BUT what if it is opened by a socket???!?!?!?! Then the
//               read should have got EBADF i guess...
// . otherwise, any read for this fd might fail with BADFD if it got closed 
//   before or during the read. in that case BigFile should retry 
// . PROBLEM #2: yeah, but if its a write, what then? if opened for writing,
//               NEVER allow the fd to be closed in closeLeastUsed()!!!
//               because if merge and dump going on at same time, and both get
//               their fds closed in closedLeastUsed(), then merge reopens his
//               file but with dumps fd, and a dump in mid thread using the
//               same old fd writes, he will write to the merge file!!!
void File::close1_r ( ) { 
	// assume no close
	m_closedIt = false;
	// debug
	log(LOG_DEBUG,"disk: close1_r: Closing vfd=%i after unlink/rename.",
	     m_vfd);
	// problem. this could be a closed map file, m_vfd=-1.
	if ( m_vfd < 0 ) {
		// -1 just means it was already closed, probably this is
		// from unlinking and RdbMap file which is closed after we
		// read it in at startup.
		log(LOG_INFO,"disk: close1_r: m_vfd=%i < 0",m_vfd);
		return ;
	}
	// panic!
	if ( s_writing [ m_vfd ] ) {
		log(LOG_LOGIC,"disk: close1_r: In write mode and closing.");
		return;
	}
	// if already being unlinked, skip
	if ( s_unlinking [ m_vfd ] ) {
		log(LOG_LOGIC,"disk: close1_r: In unlink mode and closing.");
		return;
	}
	// this is < 0 if invalid
	int fd = s_fds [ m_vfd ];
	// debug
	log(LOG_INFO,"disk: close1_r: Closing  fd=%i for %s after "
	    "unlink/rename.",fd,m_filename);

	if ( fd < 0 ) return ;
	// . do not allow closeLeastUsed to close this fd as well
	// . that can really mess us up:
	// . 1. we close this fd being unlinked/renamed
	// . 2. another file gets that fd
	// . 3. closeLeastUsed closes it again and sets our s_fds[m_vfd] to -1
	//      this leaving the other file with a seemingly valid fd that
	//      always gives EBADF errors cuz it was closed.
	s_unlinking [ m_vfd ] = 1;
 again:
	if ( fd == 0 ) log("disk: closing1 fd of 0");
	if ( ::close(fd) == 0 ) { m_closedIt = true; return; }
	log("disk: close(%i): %s.",fd,strerror(errno));
	if ( errno == EINTR ) goto again;
}

// just update the counts
void File::close2 ( ) { 
	// if already gone, bail. this could be a closed map file, m_vfd=-1.
	if ( m_vfd < 0 ) {
		// -1 just means it was already closed, probably this is
		// from unlinking and RdbMap file which is closed after we
		// read it in at startup.
		log(LOG_INFO,"disk: close2: m_vfd=%i < 0",m_vfd);
		return;
	}
	// clear for later
	s_unlinking [ m_vfd ] = 0;
	// return if we did not actually do a close
	if ( ! m_closedIt ) {
		// this can happen if the fd was always -1 before call to
		// close1_r(), like when deleting a map file... so we never
		// needed to call ::close() in close1_r().
		return;
		int fd = -3;
		if ( m_vfd >= 0 ) fd = s_fds[m_vfd];
		log(LOG_LOGIC,"disk: close2: "
		    "closeLeastUsed() or someone else beat us to the close. "
		    "This should never happen. vfd=%i fd=%i.", m_vfd,fd);
		return;
	}
	// mark this virtual file descriptor as available.
	s_fds [ m_vfd ] = -2;           
	// no more virtual file descriptor
	m_vfd = -1;
	//s_closeCounts [ fd ]++;
	s_numOpenFiles--; 
}

// . return -2 on error
// . return -1 if does not exist
// . return 0-N otherwise
// . closes the file for real!
// . analogous to a reset() routine
bool File::close ( ) {
	// return true if never opened! 
	if ( m_vfd < 0 ) return true;
	// flush any changes 
	//flush ( );
	// what was the actual file descriptor it represented?
	int fd = s_fds [ m_vfd ];
	// mark this virtual file descriptor as available.
	s_fds       [ m_vfd ] = -2;
	// save
	long vfd = m_vfd;
	//s_filenames [ m_vfd ] = NULL;
	// no more virtual file descriptor
	m_vfd = -1;
	// if it was already closed or available then return true
	if ( fd < 0 ) return true;
	// panic!
	if ( s_writing [ vfd ] )
		return log(LOG_LOGIC,"disk: In write mode and closing 2."); 
	// if already being unlinked, skip
	if ( s_unlinking [ vfd ] )
		return log(LOG_LOGIC,"disk: In unlink mode and closing 2.");
	// always block on a close
	int flags = fcntl ( fd , F_GETFL ) ;
	// turn off these 2 flags on fd to make sure
	flags &= ~( O_NONBLOCK | O_ASYNC );
	// return false on error
 retry26:
	if ( fcntl ( fd, F_SETFL, flags ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry26;
		// copy errno to g_errno
		g_errno = errno;
		return log("disk: fcntl(%s) : %s",
			   m_filename,strerror(g_errno));
	} 
	// . tally up another close for this fd, if any
	// . so if an open happens shortly here after, and 
	//   gets this fd, then any read that was started 
	//   before that open will know it!
	//s_closeCounts [ fd ]++;
	// otherwise we gotta really close it
 again:
	if ( fd == 0 ) log("disk: closing2 fd of 0");
	int status = ::close ( fd );
	if ( status == -1 && errno == EINTR ) goto again;
	// there was a closing error if status is non-zero. --- not checking 
	// the error may lead to silent loss of data --- see "man 2 close"
	if ( status != 0 ) {
		log("disk: close(%s) : %s" , m_filename,mstrerrno(g_errno)); 
		return false; 
	}
	// otherwise decrease the # of open files
	s_numOpenFiles--; 
	// return true blue
	return true; 
}

int File::getfdNoOpen ( ) {
	if ( m_vfd < 0 ) return -1;
	// this is < 0 if invalid
	return s_fds [ m_vfd ];
}

// . get the fd of this file
// . if it was closed by us we reopen it
// . may re-open a virtual fd whose real fd was closed
// . if we hit our max # of real fds allowed we'll have to close 
//   the least used of those so we can open this one
// . return -2 if never been opened
// . return -1 on other errors
// . otherwise, return the file descriptor
int File::getfd () {
	// if m_vfd is -1 it's never been opened
	if ( m_vfd < 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"disk: getfd: Must call open() first.");
		char *xx=NULL; *xx=0; 
		return -2;
	}
	// . sanity check
	// . no caller should call open/getfd after unlink was queued for thred
	//if ( m_gone ) { char *xx = NULL; *xx = 0; }
	// get the real fd from the virtual fd
	int fd = s_fds [ m_vfd ];
	// return true if it's already opened
	if ( fd >=  0 ) { 
		// debug msg
		log(LOG_DEBUG,"disk: Opened vfd #%li of %li.",
		    (long)m_vfd,(long)s_fds[m_vfd]);
		// but update the timestamp to reduce chance it closes on us
		//s_timestamps [ m_vfd ] = getTime();
		s_timestamps [ m_vfd ] = gettimeofdayInMillisecondsLocal();
		return fd;
	}
	// if fd is -2 it's marked as available
	if ( fd != -1 ) {
		g_errno = EBADENGINEER;
		log (LOG_LOGIC, "disk: getfd: fd is available?!?!" );
		return -2;
	}
	// . a real fd of -1 means it's been closed and we gotta reopen it
	// . we have to close someone if we don't have enough room
	while ( s_numOpenFiles >= s_maxNumOpenFiles ) 
		if ( ! closeLeastUsed() ) return -1;
	// what was the filename/mode of this timed-out fd?
	//char *filename    = s_filenames   [ m_vfd ];
	// time the calls to open just in case they are hurting us
	long long t1 = -1LL;
	// . re-open the sleeping file descriptor
	// . if a rename thread was queued or spawned, try old guy first
	//if ( m_oldFilename[0] ) {
	//	t1 = gettimeofdayInMilliseconds();
	//	fd = ::open ( m_oldFilename , m_flags , m_permissions );
	//}
	// then try to open the new name
	if ( fd == -1 ) {
		t1 = gettimeofdayInMilliseconds();
 retry7:
		fd = ::open ( m_filename , m_flags , m_permissions );
		// valgrind
		if ( fd == -1 && errno == EINTR ) goto retry7;
		// 0 means stdout, right? why am i seeing it get assigned???
		if ( fd == 0 ) 
			log("disk: Got fd of 0 when opening %s.",m_filename);
		if ( fd == 0 )
			fd = ::open ( m_filename , m_flags , m_permissions );
		if ( fd == 0 ) 
			log("disk: Got fd of 0 when opening2 %s.",m_filename);
		// . now inc that count in case there was someone reading on
		//   that fd right before it was closed and we got it
		// . ::close() call can now happen in a thread, so we
		//   need to inc this guy here now, too
		// . so when that read returns it will know to re-do
		// . this should really be named s_openCounts!!
		if ( fd >= 0 ) s_closeCounts [ fd ]++;
		// . we now record this
		// . that way if our fd gets closed in closeLeastUsed() or
		//   in close1_r() due to a rename/unlink then we know it!
		// . this fixes a race condition of closeCounts in Threads.cpp
		//   where we did not know that the fd had been stolen from
		//   us and assigned to another file because our close1_r()
		//   had called ::close() on our fd and our closeCount algo
		//   failed us. see the top of this file for more description
		//   into this bug fix.
		m_closeCount = s_closeCounts[fd];
	}
	if ( t1 >= 0 ) {
		long long dt = gettimeofdayInMilliseconds() - t1 ;
		if ( dt > 1 ) log(LOG_INFO,
				  "disk: call to open(%s) blocked for "
				  "%lli ms.",m_filename,dt);
	}
	// copy errno to g_errno
	if ( fd == -1 ) {
		g_errno = errno;
		log("disk: error open(%s) : %s",m_filename,strerror(g_errno));
		return -1;
	}
	// we're another open file
	s_numOpenFiles++;
	// set this file descriptor, the other stuff remains the same
	s_fds [ m_vfd ] = fd;
	// 0 means stdout, right? why am i seeing it get assigned???
	if ( fd == 0 ) 
		log("disk: Found fd of 0 when opening %s.",m_filename);
	// reset
	s_writing   [ m_vfd ] = 0;
	s_unlinking [ m_vfd ] = 0;
	// update the time stamp
	s_timestamps [ m_vfd ] = gettimeofdayInMillisecondsLocal();
	return fd;
}

// close the least used of all the file descriptors.
// we don't touch files opened for writing, however.
bool File::closeLeastUsed () {

	long long min  ;
	int    mini = -1;
	long long now = gettimeofdayInMillisecondsLocal();

	// get the least used of all the actively opened file descriptors.
	// we can't get files that were opened for writing!!!
	int i;
	for ( i = 0 ; i < MAX_NUM_VFDS ; i++ ) {
		if ( s_fds   [ i ] < 0        ) continue;
		// fds opened for writing are not candidates, because if
		// we close on a threaded write, that fd may be used to
		// re-open another file which gets garbled!
		if ( s_writing [ i ] ) continue;
		// do not close guys being unlinked they are in the middle
		// of being closed ALREADY in close1_r(). There should only be 
		// like one unlink thread allowed to be active at a time so we 
		// don't have to worry about it hogging all the fds.
		if ( s_unlinking [ i ] ) continue;
		// when we got like 1000 reads queued up, it uses a *lot* of
		// memory and we can end up never being able to complete a
		// read because the descriptors are always getting closed on us
		// so do a hack fix and do not close descriptors that are
		// about .5 seconds old on avg.
		if ( s_timestamps [ i ] == now ) continue;
		if ( s_timestamps [ i ] == now - 1 ) continue;
		if ( mini == -1 || s_timestamps [ i ] < min ) {
			min  = s_timestamps [ i ];
			mini = i;
		}
	}

	// if nothing to free then return false
	if ( mini == -1 ) 
		return log("File: closeLeastUsed: failed. All %li descriptors "
			   "are unavailable to be closed and re-used to read "
			   "from another file.",(long)s_maxNumOpenFiles);

	// debug msg
	log(LOG_DEBUG,"disk: Closing vfd #%i of %li. delta=%lli",
	    mini,(long)s_fds[mini],now-s_timestamps[mini]);

	// always block on close
	int fd    = s_fds[mini];
	int flags = fcntl ( fd , F_GETFL ) ;
	// turn off these 2 flags on fd to make sure
	flags &= ~( O_NONBLOCK | O_ASYNC );
 retry27:
	// return false on error
	if ( fcntl ( fd, F_SETFL, flags ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry27;
		//char *xx = NULL; *xx = 1;
		log("disk: fcntl(%i): %s",fd,mstrerror(errno));
		// return false;
		errno = 0;
	}

	// . tally up another close for this fd, if any
	// . so if an open happens shortly here after, and 
	//   gets this fd, then any read that was started 
	//   before that open will know it!
	//s_closeCounts [ fd ]++;
	// otherwise we gotta really close it
 again:
	if ( fd == 0 ) log("disk: closing3 fd of 0");
	int status = ::close ( fd );
	if ( status == -1 && errno == EINTR ) goto again;

	// -1 means can be reopened because File::close() wasn't called.
	// we're just conserving file descriptors
	s_fds [ mini ] = -1;

	// if the real close was successful then decrement the # of open files
	if ( status == 0 ) s_numOpenFiles--;

	if ( status == -1 ) 
		return log("disk: close(%i) : %s", fd , strerror(errno));

	return true;
}	

// . returns -2 on error
// . returns -1 if does not exist
// . otherwise returns file size in bytes
long File::getFileSize ( ) {

	// allow the substitution of another filename
        struct stat stats;

        stats.st_size = 0;

        int status = stat ( m_filename , &stats );

        // return the size if the status was ok
        if ( status == 0 ) return stats.st_size;

	// copy errno to g_errno
	g_errno = errno;

        // return 0 and reset g_errno if it just does not exist
        if ( g_errno == ENOENT ) { g_errno = 0; return 0; }

        // resource temporarily unavailable (for newer libc)
        if ( g_errno == EAGAIN ) { g_errno = 0; return 0; }

        // log & return -1 on any other error
        log("disk: error getFileSize(%s) : %s",m_filename , strerror(g_errno));
        return -1;
}

// . return 0 on error
time_t File::getLastModifiedTime ( ) {

	// allow the substitution of another filename
        struct stat stats;

        stats.st_size = 0;

        // bitch & return 0 on error (stat returns 0 on success)
        if ( stat ( m_filename , &stats ) == 0 ) return stats.st_mtime;

        // resource temporarily unavailable (for newer libc)
        if ( errno == EAGAIN ) return 0;

	// copy errno to g_errno
	g_errno = errno;
	log("disk: error stat2(%s) : %s", m_filename,strerror(g_errno));
	return 0;
}


// . returns -1 on error
// . returns  0 if does not exist
// . returns  1 if it exists
long File::doesExist ( ) {
	// preserve g_errno
	int old_errno = g_errno;
	// allow the substitution of another filename
        struct stat stats;
	// return true if it exists
        if ( stat ( m_filename , &stats ) == 0 ) return 1;
	// copy errno to g_errno
	g_errno = errno;
        // return 0 if it just does not exist and reset g_errno
        if ( g_errno == ENOENT ) { g_errno = old_errno; return 0; }
        // resource temporarily unavailable (for newer libc)
        if ( g_errno == EAGAIN ) { g_errno = old_errno; return 0; }
        // log & return -1 on any other error
        log("disk: error stat3(%s): %s", m_filename , strerror(g_errno));
        return -1;
}

bool File::unlink ( ) {
	// safety catch!
	if ( g_conf.m_readOnlyMode )
		return logf(LOG_DEBUG,"disk: Trying to unlink "
			    "while in read only mode.");
	// give the fd back to the pull, free the m_vfd
	close ();
	// avoid unneccessary unlinking
	long status = doesExist();
	// return true if we don't exist anyway
	if ( status == 0 ) return true;
	// return false and set g_errno on error
	if ( status  < 0 ) return false;
	// log it so we can see what happened to timedb!
	log("disk: unlinking %s", m_filename );
	// remove ourselves from the disk
	if ( ::unlink ( m_filename ) == 0 ) return true;
	// sync it to disk in case power goes out
	sync();
	// copy errno to g_errno
	g_errno = errno;
	// return false and set g_errno on error
	return log("disk: unlink(%s) : %s" , m_filename,strerror(g_errno));
}

bool File::flush ( ) {
	int fd =s_fds[m_vfd];
	if ( fd < 0 ) return false;
	//return log("file::flush(%s): no fd", m_filename );
	int status = fsync ( fd );
	if ( status == 0 ) return true;
	// copy errno to g_errno
	g_errno = errno;
	return log("disk: fsync(%s): %s" ,m_filename,strerror ( g_errno ) );
}

// a wrapper for lseek
long File::lseek ( long offset , int whence ) {

	long position = (long) ::lseek (s_fds [ m_vfd ] , offset , whence );

	if ( position >= 0 ) return position;

	// copy errno to g_errno
	g_errno = errno;

	log("disk: lseek ( %s(%i) , %li , whence ): %s" , m_filename , 
	      s_fds [m_vfd], offset , strerror ( g_errno ) );

	return -1;
}

// called by File::open() when it's found out that we're not initialized.
bool File::initialize ( ) {

	if ( s_isInitialized ) return true;

	//	log ( 0 , "file::initialize: running");

	// reset all the virtual file descriptos
	for ( int i = 0 ; i < MAX_NUM_VFDS ; i++ ) {
		s_fds         [ i ] = -2;    // -2 means vfd #i is available
		//s_filenames   [ i ] = NULL;
		s_timestamps  [ i ] = 0LL;
		s_writing     [ i ] = 0;
		s_unlinking   [ i ] = 0;
	}

	for ( long i = 0 ; i < MAX_NUM_FDS ; i++ ) 
		s_closeCounts[i] = 0;

	s_isInitialized = true;

	return true;
}

char *File::getExtension ( ) {
	// keep backing up over m_filename till we hit a . or / or beginning
	long i = gbstrlen(m_filename) ;
	while ( --i > 0 ) {
		if ( m_filename[i] == '.' ) break;
		if ( m_filename[i] == '/' ) break;
	}
	if ( i == 0               ) return NULL;
	if ( m_filename[i] == '/' ) return NULL;
	return &m_filename[i+1];
}

// We do not close the fd in closeLeastUsed() for fear of getting it 
// reassigned mid-thread and getting a write into a file that should not 
// have had it. this can be *VERY* bad. like if one file is being merged 
// into and a file being dumped. both lose their fd from closeLeastUsed()
// and the merge guy gets a new fd which happens to be the old fd of the
// dump, so when the dump thread lets its write go it writes into the merge
// file.
void enterWriteMode ( long vfd ) {
	if ( vfd >= 0 ) s_writing [ vfd ] = 1;
}
void exitWriteMode ( long vfd ) {
	if ( vfd >= 0 ) s_writing [ vfd ] = 0;
}
// error correction routine used by BigFile.cpp
void releaseVfd ( long vfd ) {
	if ( vfd >= 0 && s_fds [ vfd ] >= 0 ) s_fds [ vfd ] = -1;
}
int  getfdFromVfd ( long vfd ) {
	if ( vfd <= 0 ) return -1;
	return s_fds [ vfd ];
}
