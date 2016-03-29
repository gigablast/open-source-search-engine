// JAB: this is required for pwrite() in this module
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "File.h"
#include "Threads.h"

// THE FOLLOWING IS ALL STATIC 'CUZ IT'S THE FD POOL
// if someone is using a file we must make sure this is true...
static int       s_isInitialized = false;

/*
// We have up to 5k virtual descriptors, each is mapped to a real descriptor 
// or -1. We gotta store the filename to re-open one if it was closed.
// 5 ints = 20 bytes = 20k
static int       s_fds           [ MAX_NUM_VFDS ]; // the real fd
                                                   // -1 means not opened
                                                   // -2 means available
*/
//static char   *s_filenames     [ MAX_NUM_VFDS ]; // in case we gotta re-open
static int64_t s_timestamps [ MAX_NUM_FDS ]; // when was it last accessed
static char    s_writing    [ MAX_NUM_FDS ]; // is it being written to?
static char    s_unlinking  [ MAX_NUM_FDS ]; // is being unlinked/renamed
static char    s_open       [ MAX_NUM_FDS ]; // is opened?
static File   *s_filePtrs   [ MAX_NUM_FDS ];

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
static int32_t s_closeCounts [ MAX_NUM_FDS ];

void sanityCheck ( ) {
	if ( ! g_conf.m_logDebugDisk ) {
		log("disk: sanity check called but not in debug mode");
		return;
	}
	int32_t openCount = 0;
	for ( int i = 0 ; i < MAX_NUM_FDS ; i++ )
		if ( s_open[i] ) openCount++;
	if ( openCount != s_numOpenFiles ) { char *xx=NULL;*xx=0; }
}


// for avoiding unlink/opens that mess up our threaded read
int32_t getCloseCount_r ( int fd ) {
	if ( fd < 0 ) return 0;
	if ( fd >= MAX_NUM_FDS ) {
		log("disk: got fd of %i out of bounds 2 of %i",
		    (int)fd,(int)MAX_NUM_FDS);
		return 0;
	}
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
	constructor();
}

File::~File ( ) {
	destructor();
}

void File::constructor ( ) {
	m_fd = -1; 
	// initialize m_maxFileSize and the virtual fd table
	if ( ! s_isInitialized ) initialize ();
	// we are not being renamed
	//m_oldFilename[0] = '\0';
	// threaded unlink sets this to true before spawning thread so we
	// do not try to open it!
	//m_gone = 0;
	// m_nextActive = NULL;
	// m_prevActive = NULL;
	m_calledOpen = false;
	m_calledSet  = false;
	//m_filename.constructor();
	// use the stack thing for now until we find the bug
	//m_filename.setBuf ( m_filenameBuf,MAX_FILENAME_LEN-1 ,0,false,0);
	//m_filename.setLabel   ("sbfnm");
	if ( g_conf.m_logDebugDisk )
		log("disk: constructor fd %i this=0x%"PTRFMT,
		    (int)m_fd,(PTRTYPE)this);
}

void File::destructor ( ) {
	if ( g_conf.m_logDebugDisk )
		log("disk: destructor fd %i this=0x%"PTRFMT,
		    (int)m_fd,(PTRTYPE)this);
	close ();
	// set m_calledSet to false so BigFile.cpp see it as 'empty'
	m_calledSet  = false;
	m_calledOpen = false;
	//m_filename.destructor();
}

void File::set ( char *dir , char *filename ) {
	if ( ! dir ) { set ( filename ); return; }
	char buf[1024];
	if ( dir[gbstrlen(dir)-1] == '/' )
		snprintf ( buf , 1020, "%s%s" , dir , filename );
	else
		snprintf ( buf , 1020, "%s/%s" , dir , filename );
	set ( buf );
}

void File::set ( char *filename ) {
	// reset m_filename
	m_filename[0] = '\0';
	//m_filename.reset();
	// return if NULL
	if ( ! filename ) { 
		log ( LOG_LOGIC,"disk: Provided filename is NULL");
		return;
	}
	// bail if too long
	int32_t len = gbstrlen ( filename );
	// account for terminating '\0'
	if ( len + 1 >= MAX_FILENAME_LEN ) { 
	 	log ( "disk: Provdied filename %s length of %"INT32" "
		      "is bigger "
	 	      "than %"INT32".",filename,len,
		      (int32_t)MAX_FILENAME_LEN-1); 
	 	return; 
	}
	// if we already had another file open then we must close it first.
	if ( m_fd >= 0 ) close();
	// copy into m_filename and NULL terminate
	gbmemcpy ( m_filename , filename , len );
	m_filename [ len ] = '\0';
	//m_filename.setLabel   ("sbfnm");
	//m_filename.safeStrcpy ( filename );
	m_calledSet  = true;
	// TODO: make this a bool returning function if ( ! m_filename ) g_log
}

bool File::rename ( char *newFilename ) {
	// close ourselves if we were open... why? historical reasons?
	close();
	// do the rename
	if ( ::rename ( getFilename() , newFilename ) != 0 ) 
		return false;
	// sync it to disk in case power goes out
	sync();
	//return log("file::rename: from %s to %s failed", 
	//m_filename , newFilename );
	// set to our new name
	set ( newFilename );
	return true;
}

/*
static File *s_activeHead = NULL;
static File *s_activeTail = NULL;

void rmFileFromLinkedList ( File *f ) {
	// excise from linked list of active files
	if ( s_activeHead == f )
		s_activeHead = f->m_nextActive;
	if ( s_activeTail == f )
		s_activeTail = f->m_prevActive;
	if ( f->m_prevActive ) 
		f->m_prevActive->m_nextActive = f->m_nextActive;
	if ( f->m_nextActive ) 
		f->m_nextActive->m_prevActive = f->m_prevActive;
	// and so we do not try to re-excise it
	f->m_prevActive = NULL;
	f->m_nextActive = NULL;
}

void addFileToLinkedList ( File *f ) {
	// must not be in there already, lest we double add it
	if ( f->m_nextActive ) return;
	if ( f->m_prevActive ) return;
	if ( s_activeHead == f ) return;

	f->m_nextActive = NULL;
	f->m_prevActive = NULL;
	if ( ! s_activeTail ) {
		s_activeHead = f;
		s_activeTail = f;
		return;
	}
	// insert at end of linked list otherwise
	s_activeTail->m_nextActive = f;
	f->m_prevActive = s_activeTail;
	s_activeTail = f;
}

// update linked list
void promoteInLinkedList ( File *f ) {
	rmFileFromLinkedList ( f );
	addFileToLinkedList  ( f );
}
*/

// . open the file
// . only call once per File after calling set()
bool File::open ( int flags , int permissions ) {
	// if we already had another file open then we must close it first.
	if ( m_fd >= 0 ) {
		log(LOG_LOGIC,
		    "disk: Open already called. Closing and re-opening.");
		close();
	}
	// save these in case we need to reopen in getfd()
	m_flags       = flags;
	//m_permissions = permissions;
	// just override and use system settings so we can get the group 
	// writable/readable/executable bits if set that way in g_conf
	//m_permissions = getFileCreationFlags();
	m_calledOpen  = true;
	// sanity check
	//int32_t ss = 0;
	//for ( int32_t i = 0 ; i < MAX_NUM_VFDS ; i++ ) 
	//	if (s_fds [ i ] >= 0 && s_writing[i] ) ss++;
	//log("got %"INT32" doing writes",ss);
	
	// we must assign this to a virtual descriptor
	// scan down our list looking for an m_fd of -2 (available) [-1 means 
	//    used but but not really open]
	//int i;
	//for ( i = 0 ; i < MAX_NUM_VFDS ; i++ ) if (s_fds [ i ] == -2 ) break;
	// can these fools use all 5k fd's?
	// if ( i >= MAX_NUM_VFDS ) {
	// 	g_errno = EBADENGINEER;
	// 	return log ( 
	// 		     "disk: All %"INT32" virtual fd's are in use. Panic.",
	// 		     (int32_t)MAX_NUM_VFDS);
	// }
	// remember OUR virtual file descriptor for successive calls to 
	// read/write/...
	//m_vfd      = i;
	// we are not open at this point, but no longer available at least
	//s_fds [ m_vfd ] = -1;
	// open for real, return true on success
	if ( getfd () >= 0 ) return true;
	// log the error
	log("disk: open: %s",mstrerror(g_errno));
	// . close the virtual fd so we can call open again
	// . sets s_fds [ m_vfd ] to -2 (available)
	// . and sets our m_vfd to -1
	close();
	// otherwise bitch and return false
	return false;
}

// . returns number of bytes written
// . returns -1 on error
// . may return < numBytesToWrite if non-blocking
int File::write ( void *buf             , 
		  int32_t  numBytesToWrite , 
		  int32_t  offset          ) {
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
	// update linked list
	//promoteInLinkedList ( this );
	// copy errno to g_errno
	if ( n < 0 ) g_errno = errno;
	// cancel blocking errors - not really errors
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }
	// log an error
	if ( n < 0 ) 
		log("disk: write(%s) : %s" ,  
		    getFilename(), strerror ( g_errno ) );
	return n;
}

int File::read ( void *buf            , 
		 int32_t  numBytesToRead , 
		 int32_t  offset         ) {
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
	// update linked list
	//promoteInLinkedList ( this );
	// copy errno to g_errno
	if ( n < 0 ) g_errno = errno;
	// cancel blocking errors - not really errors
	if ( g_errno == EAGAIN ) { g_errno = 0; n = 0; }
	if ( n < 0 ) 
		log("disk: read(%s) : %s" ,  
		    getFilename(), strerror ( g_errno ) );
	return n;
}

// uses lseek to get file's current position
int32_t File::getCurrentPos ( ) {
	return (int32_t) ::lseek ( m_fd , 0 , SEEK_CUR );
}

bool File::isNonBlocking () {
	// return true if never opened! 
	//if ( m_vfd < 0 ) return false;
	// what was the actual file descriptor it represented?
	//int fd = s_fds [ m_vfd ];
	// always block on a close
	int flags = fcntl ( m_fd , F_GETFL ) ;
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

	// debug. don't log in thread - might hurt us
	log(LOG_DEBUG,"disk: close1_r: Closing  fd %i for %s after "
	    "unlink/rename.",m_fd,getFilename());

	// problem. this could be a closed map file, m_vfd=-1.
	if ( m_fd < 0 ) {
		// -1 just means it was already closed, probably this is
		// from unlinking and RdbMap file which is closed after we
		// read it in at startup.
		log(LOG_DEBUG,"disk: close1_r: fd %i < 0",m_fd);
		return ;
	}
	// panic!
	if ( s_writing [ m_fd ] ) {
		log(LOG_LOGIC,"disk: close1_r: In write mode and closing.");
		return;
	}
	// if already being unlinked, skip
	if ( s_unlinking [ m_fd ] ) {
		log(LOG_LOGIC,"disk: close1_r: In unlink mode and closing.");
		return;
	}
	// this is < 0 if invalid
	//int fd = s_fds [ m_vfd ];

	if ( m_fd < 0 ) return ;
	// . do not allow closeLeastUsed to close this fd as well
	// . that can really mess us up:
	// . 1. we close this fd being unlinked/renamed
	// . 2. another file gets that fd
	// . 3. closeLeastUsed closes it again and sets our s_fds[m_vfd] to -1
	//      this leaving the other file with a seemingly valid fd that
	//      always gives EBADF errors cuz it was closed.
	s_unlinking [ m_fd ] = 1;
 again:
	if ( m_fd == 0 ) log("disk: closing1 fd of 0");
	if ( ::close(m_fd) == 0 ) { 
		m_closedIt = true; 
		// close2() needs to see m_fd so it can set flags...
		// so m_fd MUST be intact
		//m_fd = -1;
		return; 
	}
	log("disk: close(%i): %s.",m_fd,strerror(errno));
	if ( errno == EINTR ) goto again;
}

// . just update the counts
// . BigFile.cpp calls this when done unlinking/renaming this file
void File::close2 ( ) { 
	// if already gone, bail. this could be a closed map file, m_vfd=-1.
	if ( m_fd < 0 ) {
		// -1 just means it was already closed, probably this is
		// from unlinking and RdbMap file which is closed after we
		// read it in at startup.
		log(LOG_INFO,"disk: close2: fd %i < 0",m_fd);
		return;
	}

	// clear for later, but only if nobody else got our fd when opening
	// a file... because we called close() in a thread in close1_r()
	if ( s_filePtrs [ m_fd ] == this )
		s_unlinking [ m_fd ] = 0;

	// return if we did not actually do a close in close1_r()
	if ( ! m_closedIt ) {
		// this can happen if the fd was always -1 before call to
		// close1_r(), like when deleting a map file... so we never
		// needed to call ::close() in close1_r().
		return;
		/*
		int fd = -3;
		if ( m_vfd >= 0 ) fd = s_fds[m_vfd];
		log(LOG_LOGIC,"disk: close2: "
		    "closeLeastUsed() or someone else beat us to the close. "
		    "This should never happen. vfd=%i fd=%i.", m_vfd,fd);
		return;
		*/
	}

	if ( g_conf.m_logDebugDisk ) sanityCheck();

	// excise from linked list of active files
	//rmFileFromLinkedList ( this );
	// mark this virtual file descriptor as available.
	//s_fds [ m_vfd ] = -2;           

	// save this for stuff below
	int fd = m_fd;

	// now it is closed. do not try to re-close in destructor's call to
	// close() so set m_fd to -1
	m_fd = -1;

	// mark it as closed
	// CAUTION: since we closed the fd in a thread in close1_r() it may 
	// have been returned for another file, so check here. make sure we are
	// still considered the 'owner'. if not then we were supplanted in
	// File::getfd() and s_numOpenFiles-- was called there as well so
	// we should skip everything below here.
	if ( s_filePtrs [ fd ] != this ) return;

	s_open        [ fd ] = 0;
	s_filePtrs    [ fd ] = NULL;
	// i guess there is no need to do this close count inc
	// if we lost our fd already shortly after our thread closed
	// the fd, otherwise we'll falsely mess up the new owner
	// and he will do a re-read.
	s_closeCounts [ fd ]++;

	// to keep our sanityCheck() from coring, only decrement this
	// if we owned it still
	s_numOpenFiles--; 
	// no more virtual file descriptor
	//m_vfd = -1;
	//s_closeCounts [ fd ]++;
	// debug log
	if ( g_conf.m_logDebugDisk )
		log("disk: close2 fd %i for %s #openfiles=%i "
		    "this=0x%"PTRFMT,
		    fd,getFilename(),
		    (int)s_numOpenFiles,(PTRTYPE)this);

	if ( g_conf.m_logDebugDisk ) sanityCheck();
}

// . return -2 on error
// . return -1 if does not exist
// . return 0-N otherwise
// . closes the file for real!
// . analogous to a reset() routine
bool File::close ( ) {
	// return true if not open
	if ( m_fd < 0 ) return true;
	// flush any changes 
	//flush ( );
	// what was the actual file descriptor it represented?
	//int fd = s_fds [ m_vfd ];
	// mark this virtual file descriptor as available.
	//s_fds       [ m_vfd ] = -2;
	// save
	//int32_t vfd = m_vfd;
	//s_filenames [ m_vfd ] = NULL;
	// no more virtual file descriptor
	//m_vfd = -1;
	// if it was already closed or available then return true
	//if ( fd < 0 ) return true;
	// panic!
	if ( s_writing [ m_fd ] )
		return log(LOG_LOGIC,"disk: In write mode and closing 2."); 
	// if already being unlinked, skip
	if ( s_unlinking [ m_fd ] )
		return log(LOG_LOGIC,"disk: In unlink mode and closing 2.");
	// always block on a close
	int flags = fcntl ( m_fd , F_GETFL ) ;
	// turn off these 2 flags on fd to make sure
	flags &= ~( O_NONBLOCK | O_ASYNC );
	// return false on error
 retry26:
	if ( fcntl ( m_fd, F_SETFL, flags ) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry26;
		// copy errno to g_errno
		g_errno = errno;
		return log("disk: fcntl(%s) : %s",
			   getFilename(),strerror(g_errno));
	} 
	// . tally up another close for this fd, if any
	// . so if an open happens int16_tly here after, and 
	//   gets this fd, then any read that was started 
	//   before that open will know it!
	//s_closeCounts [ fd ]++;
	// otherwise we gotta really close it

	if ( g_conf.m_logDebugDisk ) sanityCheck();

 again:
	if ( m_fd == 0 ) log("disk: closing2 fd of 0");
	int status = ::close ( m_fd );
	if ( status == -1 && errno == EINTR ) goto again;
	// there was a closing error if status is non-zero. --- not checking 
	// the error may lead to silent loss of data --- see "man 2 close"
	if ( status != 0 ) {
		log("disk: close(%s) : %s" ,getFilename(),mstrerrno(g_errno)); 
		return false; 
	}
	// sanity
	if ( ! s_open[m_fd] ) { char *xx=NULL;*xx=0; }
	// mark it as closed
	s_open        [ m_fd ] = 0;
	s_filePtrs    [ m_fd ] = NULL;
	s_closeCounts [ m_fd ]++;
	// otherwise decrease the # of open files
	s_numOpenFiles--; 
	// debug log
	if ( g_conf.m_logDebugDisk )
		log("disk: close0 fd %i for %s #openfiles=%i",
		    m_fd,getFilename(),(int)s_numOpenFiles);
	// set this to -1 to indicate closed
	m_fd = -1;
	// excise from linked list of active files
	//rmFileFromLinkedList ( this );
	// return true blue
	if ( g_conf.m_logDebugDisk ) sanityCheck();
	return true; 
}

int File::getfdNoOpen ( ) {
	// this is -1 if not open
	return m_fd;
	//if ( m_vfd < 0 ) return -1;
	// this is < 0 if invalid
	//return s_fds [ m_vfd ];
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
	if ( ! m_calledOpen ) { // m_vfd < 0 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"disk: getfd: Must call open() first.");
		char *xx=NULL; *xx=0; 
		return -2;
	}

	// if someone closed our fd, why didn't our m_fd get set to -1 ??!?!?!!
	if ( m_fd >= 0 && m_closeCount != s_closeCounts[m_fd] ) {
		log(LOG_DEBUG,"disk: invalidating existing fd %i "
		    "for %s this=0x%"PTRFMT" ccSaved=%i ccNow=%i", 
		    (int)m_fd,getFilename(),(PTRTYPE)this,
		    (int)m_closeCount,
		    (int)s_closeCounts[m_fd]);
		m_fd = -1;
	}

	// . sanity check
	// . no caller should call open/getfd after unlink was queued for thred
	//if ( m_gone ) { char *xx = NULL; *xx = 0; }
	// get the real fd from the virtual fd
	//int fd = s_fds [ m_vfd ];
	// return true if it's already opened
	if ( m_fd >=  0 ) { 
		// debug msg
		if ( g_conf.m_logDebugDisk )
			log(LOG_DEBUG,"disk: returning existing fd %i for %s "
			    "this=0x%"PTRFMT" ccSaved=%i ccNow=%i", 
			    (int)m_fd,getFilename(),(PTRTYPE)this,
			    (int)m_closeCount,
			    (int)s_closeCounts[m_fd]);
		if ( m_fd >= MAX_NUM_FDS ) { char *xx=NULL;*xx=0; }
		// but update the timestamp to reduce chance it closes on us
		//s_timestamps [ m_vfd ] = getTime();
		s_timestamps [ m_fd ] = gettimeofdayInMillisecondsLocal();
		return m_fd;
	}
	// if fd is -2 it's marked as available
	// if ( fd != -1 ) {
	// 	g_errno = EBADENGINEER;
	// 	log (LOG_LOGIC, "disk: getfd: fd is available?!?!" );
	// 	return -2;
	// }
	// . a real fd of -1 means it's been closed and we gotta reopen it
	// . we have to close someone if we don't have enough room
	while ( s_numOpenFiles >= s_maxNumOpenFiles )  {
		if ( g_conf.m_logDebugDisk ) sanityCheck();
		if ( ! closeLeastUsed() ) return -1;
		if ( g_conf.m_logDebugDisk ) sanityCheck();
	}
	// what was the filename/mode of this timed-out fd?
	//char *filename    = s_filenames   [ m_vfd ];
	// time the calls to open just in case they are hurting us
	int64_t t1 = -1LL;
	// . re-open the sleeping file descriptor
	// . if a rename thread was queued or spawned, try old guy first
	//if ( m_oldFilename[0] ) {
	//	t1 = gettimeofdayInMilliseconds();
	//	fd = ::open ( m_oldFilename , m_flags , m_permissions );
	//}
	int fd = -1;
	// then try to open the new name
	if ( fd == -1 ) {
		t1 = gettimeofdayInMilliseconds();
 retry7:
		fd = ::open ( getFilename() , m_flags,getFileCreationFlags());
		// valgrind
		if ( fd == -1 && errno == EINTR ) goto retry7;
		// 0 means stdout, right? why am i seeing it get assigned???
		if ( fd == 0 ) 
			log("disk: Got fd of 0 when opening %s.",
			    getFilename());
		if ( fd == 0 )
		       fd=::open(getFilename(),m_flags,getFileCreationFlags());
		if ( fd == 0 ) 
			log("disk: Got fd of 0 when opening2 %s.",
			    getFilename());
		if ( fd >= MAX_NUM_FDS )
			log("disk: got fd of %i out of bounds 1 of %i",
			    (int)fd,(int)MAX_NUM_FDS);

		// if we got someone else's fd that called close1_r() in a
		// thread but did not have time to call close2() to fix
		// up these member vars, then do it here. close2() will 
		// see that s_filePtrs[fd] does not equal the file ptr any more
		// and it will not update s_numOpenFiles in that case.
		if ( fd >= 0 && s_open [ fd ] ) {
			File *f = s_filePtrs [ fd ];
			if ( g_conf.m_logDebugDisk )
				log("disk: swiping fd %i from %s before "
				    "his close thread returned "
				    "this=0x%"PTRFMT,
				    fd,
				    f->getFilename(),
				    (PTRTYPE)f);
			// he only incs/decs his counters if he owns it so in
			// close2() so dec this global counter here
			s_numOpenFiles--;
			s_open[fd] = 0;
			s_filePtrs[fd] = NULL;
			if ( g_conf.m_logDebugDisk ) sanityCheck();
		}

		// sanity. how can we get an fd already opened?
		// because it was closed in a thread in close1_r()
		if ( fd >= 0 && s_open[fd] ) { char *xx=NULL;*xx=0; }
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
		int64_t dt = gettimeofdayInMilliseconds() - t1 ;
		if ( dt > 1 ) log(LOG_INFO,
				  "disk: call to open(%s) blocked for "
				  "%"INT64" ms.",getFilename(),dt);
	}
	// copy errno to g_errno
	if ( fd <= -1 ) {
		g_errno = errno;
		log("disk: error open(%s) : %s fd %i",
		    getFilename(),strerror(g_errno),(int)fd);
		return -1;
	}

	if ( g_conf.m_logDebugDisk ) sanityCheck();

	// we're another open file
	s_numOpenFiles++;

	// debug log
	if ( g_conf.m_logDebugDisk )
		log("disk: opened1 fd %i for %s #openfiles=%i this=0x%"PTRFMT,
		    (int)fd,getFilename(),(int)s_numOpenFiles,(PTRTYPE)this);

	// set this file descriptor, the other stuff remains the same
	//s_fds [ m_vfd ] = fd;
	m_fd = fd;
	// 0 means stdout, right? why am i seeing it get assigned???
	if ( fd == 0 ) 
		log("disk: Found fd of 0 when opening %s.",getFilename());
	// reset
	s_writing   [ fd ] = 0;
	s_unlinking [ fd ] = 0;
	// update the time stamp
	s_timestamps [ fd ] = gettimeofdayInMillisecondsLocal();
	s_open       [ fd ] = true;
	s_filePtrs   [ fd ] = this;

	if ( g_conf.m_logDebugDisk ) sanityCheck();
	// add file to linked list of active files
	//addFileToLinkedList ( this );
	return fd;
}

// close the least used of all the file descriptors.
// we don't touch files opened for writing, however.
bool File::closeLeastUsed () {

	int64_t min  ;
	int    mini = -1;
	int64_t now = gettimeofdayInMillisecondsLocal();


	int32_t notopen = 0;
	int32_t writing = 0;
	int32_t unlinking = 0;
	int32_t young = 0;

	// get the least used of all the actively opened file descriptors.
	// we can't get files that were opened for writing!!!
	int i;
	for ( i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		//if ( s_fds   [ i ] < 0        ) continue;
		if ( ! s_open[i] ) { notopen++; continue; }
		// fds opened for writing are not candidates, because if
		// we close on a threaded write, that fd may be used to
		// re-open another file which gets garbled!
		if ( s_writing [ i ] ) { writing++; continue; }
		// do not close guys being unlinked they are in the middle
		// of being closed ALREADY in close1_r(). There should only be 
		// like one unlink thread allowed to be active at a time so we 
		// don't have to worry about it hogging all the fds.
		if ( s_unlinking [ i ] ) { unlinking++; continue; }
		// when we got like 1000 reads queued up, it uses a *lot* of
		// memory and we can end up never being able to complete a
		// read because the descriptors are always getting closed on us
		// so do a hack fix and do not close descriptors that are
		// about .5 seconds old on avg.
		if ( s_timestamps [ i ] == now ) { young++; continue; }
		if ( s_timestamps [ i ] == now - 1 ) { young++; continue; }
		if ( mini == -1 || s_timestamps [ i ] < min ) {
			min  = s_timestamps [ i ];
			mini = i;
		}
	}

	/*
	// use the new linked list of active file descriptors
	// . file at tail is the most active
	File *f = s_activeHead;

	// if nothing to do return true
	//if ( ! f ) return true;

	int32_t mini2 = -1;

	// close the head if not writing
	for ( ; f ; f = f->m_nextActive ) {
		mini2 = f->m_vfd;
		// how can this be?
		if ( s_fds [ mini2 ] < 0 ) { char *xx=NULL;*xx=0; }
		if ( s_writing [ mini2 ] ) continue;
		if ( s_unlinking [ mini2 ] ) continue;
		// when we got like 1000 reads queued up, it uses a *lot* of
		// memory and we can end up never being able to complete a
		// read because the descriptors are always getting closed on us
		// so do a hack fix and do not close descriptors that are
		// about .5 seconds old on avg.
		if ( s_timestamps [ mini2 ] >= now - 1000 ) continue;
		break;
	}

	// debug why it doesn't work right
	if ( mini != mini2 ) {
		int fd1 = -1;
		int fd2 = -1;
		if ( mini >= 0 ) fd1 = s_fds[mini];
		if ( mini2 >= 0 ) fd2 = s_fds[mini2];
		int32_t age = now - s_timestamps[mini] ;
		log("File: linkedlistfd=%i != rightfd=%i agems=%i",fd1,fd2,
		    (int)age);
	}
	*/

	// if nothing to free then return false
	if ( mini == -1 ) 
		return log("File: closeLeastUsed: failed. All %"INT32" "
			   "descriptors "
			   "are unavailable to be closed and re-used to read "
			   "from another file. notopen=%i writing=%i "
			   "unlinking=%i young=%i"
			   ,(int32_t)s_maxNumOpenFiles
			   ,notopen
			   ,writing
			   ,unlinking
			   ,young );


	int fd = mini;

	// always block on close
	//int fd    = s_fds[mini];
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
	//s_fds [ mini ] = -1;

	// if the real close was successful then decrement the # of open files
	if ( status == 0 ) {
		// it's not open
		s_open     [ fd ] = 0;
		// if someone is trying to read on this let them know
		s_closeCounts [ fd ]++;

		s_numOpenFiles--;

		File *f = s_filePtrs [ fd ];
		// don't let him use the stolen fd
		f->m_fd = -1 ;

		// debug msg
		if ( g_conf.m_logDebugDisk ) {
			File *f = s_filePtrs [ fd ];
			char *fname = "";
			if ( f ) fname = f->getFilename();
			logf(LOG_DEBUG,"disk: force closed fd %i for"
			     " %s. age=%"INT64" #openfiles=%i this=0x%"PTRFMT,
			     fd,fname,now-s_timestamps[mini],
			     (int)s_numOpenFiles,
			     (PTRTYPE)this);
		}

		// no longer the owner
		s_filePtrs [ fd ] = NULL;

		// excise from linked list of active files
		//rmFileFromLinkedList ( f );
		// getfd() may not execute in time to ince the closeCount
		// so do it here. test by setting the max open files to like
		// 10 or so and spidering heavily.
		//s_closeCounts [ fd ]++;
	}


	if ( status == -1 ) 
		return log("disk: close(%i) : %s", fd , strerror(errno));

	if ( g_conf.m_logDebugDisk ) sanityCheck();

	return true;
}	

int64_t getFileSize ( char *filename ) {

#ifdef CYGWIN
	return getFileSize_cygwin ( filename );
#endif

	//
	// CAUTION: i think this fails in cygwin... so for cygwin use the
	// old slower code
	//

	// allow the substitution of another filename
        struct stat stats;

        stats.st_size = 0;

        int status = stat ( filename , &stats );

        // return the size if the status was ok
        if ( status == 0 ) {
		//int64_t tmp = getFileSize_cygwin ( filename );
		//if ( tmp>=0 && tmp != stats.st_size ) {char *xx=NULL;*xx=0; }
		return stats.st_size;
	}

	// copy errno to g_errno
	g_errno = errno;

        // return 0 and reset g_errno if it just does not exist
	if ( g_errno == ENOENT ) { g_errno = 0; return 0; }

        // resource temporarily unavailable (for newer libc)
	if ( g_errno == EAGAIN ) { g_errno = 0; return 0; }

        // log & return -1 on any other error
	log("disk: error getFileSize(%s) : %s",filename,strerror(g_errno));
	return -1;
}

// this solution is quite slow, but i think cygwin needs it
int64_t getFileSize_cygwin ( char *filename ) {

	FILE *fd = fopen ( filename , "r" );
	if ( ! fd ) {
		//log("disk: error getFileSize(%s) : %s",
		//    filename , strerror(g_errno));
		return 0;//-1;
	}

	fseek(fd,0,SEEK_END);
	int64_t fileSize = ftell ( fd );

	fclose ( fd );

	return fileSize;
}



// . returns -2 on error
// . returns -1 if does not exist
// . otherwise returns file size in bytes
int64_t File::getFileSize ( ) {

	// allow the substitution of another filename
        //struct stat stats;

        //stats.st_size = 0;

        //int status = stat ( m_filename , &stats );

	return ::getFileSize ( getFilename() );

        // return the size if the status was ok
        //if ( status == 0 ) return stats.st_size;

	// copy errno to g_errno
	//g_errno = errno;

        // return 0 and reset g_errno if it just does not exist
        //if ( g_errno == ENOENT ) { g_errno = 0; return 0; }

        // resource temporarily unavailable (for newer libc)
        //if ( g_errno == EAGAIN ) { g_errno = 0; return 0; }

        // log & return -1 on any other error
        //log("disk: error getFileSize(%s) : %s",m_filename,strerror(g_errno));
        //return -1;
}

// . return 0 on error
time_t File::getLastModifiedTime ( ) {

	// allow the substitution of another filename
        struct stat stats;

        stats.st_size = 0;

        // bitch & return 0 on error (stat returns 0 on success)
        if ( stat ( getFilename() , &stats ) == 0 ) return stats.st_mtime;

        // resource temporarily unavailable (for newer libc)
        if ( errno == EAGAIN ) return 0;

	// copy errno to g_errno
	g_errno = errno;
	log("disk: error stat2(%s) : %s", getFilename(),strerror(g_errno));
	return 0;
}

bool doesFileExist ( char *filename ) {
	// allow the substitution of another filename
        struct stat stats;
	// return true if it exists
        if ( stat ( filename , &stats ) == 0 ) return true;
        // return 0 if it just does not exist and reset g_errno
        if ( errno == ENOENT ) return false;
        // resource temporarily unavailable (for newer libc)
        if ( errno == EAGAIN ) return false;
	// error
        return false;
}

// . returns -1 on error
// . returns  0 if does not exist
// . returns  1 if it exists
int32_t File::doesExist ( ) {
	// preserve g_errno
	int old_errno = g_errno;
	// allow the substitution of another filename
        struct stat stats;
	// return true if it exists
        if ( stat ( getFilename() , &stats ) == 0 ) return 1;
	// copy errno to g_errno
	g_errno = errno;
        // return 0 if it just does not exist and reset g_errno
        if ( g_errno == ENOENT ) { g_errno = old_errno; return 0; }
        // resource temporarily unavailable (for newer libc)
        if ( g_errno == EAGAIN ) { g_errno = old_errno; return 0; }
        // log & return -1 on any other error
	if ( ! g_errno ) {
		log("process: you tried to overload __errno_location() "
		    "but were unsuccessful. you need to be using pthreads.");
		char *xx=NULL;*xx=0; 
	}
        log("disk: error stat3(%s): %s", getFilename() , strerror(g_errno));
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
	int32_t status = doesExist();
	// return true if we don't exist anyway
	if ( status == 0 ) return true;
	// return false and set g_errno on error
	if ( status  < 0 ) return false;
	// . log it so we can see what happened to timedb!
	// . don't log startup unlinks of "tmpfile"
	if ( ! strstr(getFilename(),"tmpfile") )
		log(LOG_INFO,"disk: unlinking %s", getFilename() );
	// remove ourselves from the disk
	if ( ::unlink ( getFilename() ) == 0 ) return true;
	// sync it to disk in case power goes out
	sync();
	// copy errno to g_errno
	g_errno = errno;
	// return false and set g_errno on error
	return log("disk: unlink(%s) : %s" , getFilename(),strerror(g_errno));
}

bool File::flush ( ) {
	//int fd =s_fds[m_vfd];
	if ( m_fd < 0 ) return false;
	//return log("file::flush(%s): no fd", getFilename() );
	int status = fsync ( m_fd );
	if ( status == 0 ) return true;
	// copy errno to g_errno
	g_errno = errno;
	return log("disk: fsync(%s): %s" ,getFilename(),strerror ( g_errno ) );
}

// a wrapper for lseek
int32_t File::lseek ( int32_t offset , int whence ) {

	int32_t position = (int32_t) ::lseek ( m_fd , offset , whence );

	if ( position >= 0 ) return position;

	// copy errno to g_errno
	g_errno = errno;

	log("disk: lseek ( %s(%i) , %"INT32" , whence ): %s" , getFilename() , 
	      m_fd, offset , strerror ( g_errno ) );

	return -1;
}

// called by File::open() when it's found out that we're not initialized.
bool File::initialize ( ) {

	if ( s_isInitialized ) return true;

	//	log ( 0 , "file::initialize: running");

	// reset all the virtual file descriptos
	for ( int i = 0 ; i < MAX_NUM_FDS ; i++ ) {
		//s_fds         [ i ] = -2;    // -2 means vfd #i is available
		//s_filenames   [ i ] = NULL;
		s_timestamps  [ i ] = 0LL;
		s_writing     [ i ] = 0;
		s_unlinking   [ i ] = 0;
		s_open        [ i ] = 0;
		s_closeCounts [ i ] = 0;
		s_filePtrs    [ i ] = NULL;
	}

	// for ( int32_t i = 0 ; i < MAX_NUM_FDS ; i++ ) 
	// 	s_closeCounts[i] = 0;

	s_isInitialized = true;

	return true;
}

char *File::getExtension ( ) {
	// keep backing up over m_filename till we hit a . or / or beginning
	char *f = getFilename();
	int32_t i = gbstrlen(m_filename);//m_filename.getLength();
	while ( --i > 0 ) {
		if ( f[i] == '.' ) break;
		if ( f[i] == '/' ) break;
	}
	if ( i == 0               ) return NULL;
	if ( f[i] == '/' ) return NULL;
	return &f[i+1];
}

// We do not close the fd in closeLeastUsed() for fear of getting it 
// reassigned mid-thread and getting a write into a file that should not 
// have had it. this can be *VERY* bad. like if one file is being merged 
// into and a file being dumped. both lose their fd from closeLeastUsed()
// and the merge guy gets a new fd which happens to be the old fd of the
// dump, so when the dump thread lets its write go it writes into the merge
// file.
void enterWriteMode ( int fd ) {
	if ( fd >= 0 ) s_writing [ fd ] = 1;
}
void exitWriteMode ( int fd ) {
	if ( fd >= 0 ) s_writing [ fd ] = 0;
}
// error correction routine used by BigFile.cpp
// void releaseVfd ( int32_t vfd ) {
// 	if ( vfd >= 0 && s_fds [ vfd ] >= 0 ) s_fds [ vfd ] = -1;
// }
// int  getfdFromVfd ( int32_t vfd ) {
// 	if ( vfd <= 0 ) return -1;
// 	return s_fds [ vfd ];
// }
