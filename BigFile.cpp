// JAB: this is required for pwrite() in this module
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "BigFile.h"
#include "Dir.h"
#include "Threads.h"
#include "Stats.h"
#include "Statsdb.h"
#include "DiskPageCache.h"

// main.cpp will wait for this to be zero before exiting so all unlink/renames
// can complete
long g_unlinkRenameThreads = 0;

long long g_lastDiskReadStarted = 0LL;
long long g_lastDiskReadCompleted = 0LL;
bool      g_diskIsStuck = false;

static void  doneWrapper        ( void *state , ThreadEntry *t ) ;
static bool  readwrite_r        ( FileState *fstate , ThreadEntry *t ) ;

BigFile::~BigFile () {
	close();
}

//#define O_DIRECT 040000

BigFile::BigFile () {
	m_permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH ;
	m_flags       = O_RDWR ; // | O_DIRECT;
	// NULLify all ptrs to files
	for ( long i = 0 ; i < MAX_PART_FILES ; i++ ) m_files[i] = NULL;
	m_maxParts = 0;
	m_numParts = 0;
	m_pc  = NULL;
	m_vfd = -1;
	m_vfdAllowed = false;
	m_fileSize = -1;
	m_lastModified = -1;
	m_numThreads = 0;
	m_isClosing = false;
	g_lastDiskReadStarted = 0;
	g_lastDiskReadCompleted = 0;
	g_diskIsStuck = false;
}

// we alternate parts into "dirname" and "stripeDir"
bool BigFile::set ( char *dir , char *baseFilename , char *stripeDir ) {
	// reset filsize
	m_fileSize = -1;
	m_lastModified = -1;
	// m_baseFilename contains the "dir" in it
	//sprintf(m_baseFilename ,"%s/%s", dirname  , baseFilename );
	strcpy ( m_baseFilename , baseFilename  );
	strcpy ( m_dir          , dir           );
	if ( stripeDir ) strcpy ( m_stripeDir    , stripeDir     );
	else             m_stripeDir[0] = '\0';
	// reset # of parts
	m_numParts = 0;
	m_maxParts = 0;
	// now add parts from both directories
	if ( ! addParts ( m_dir       ) ) return false;
	if ( ! addParts ( m_stripeDir ) ) return false;
	return true;
}

bool BigFile::reset ( ) {
	// reset filsize
	m_fileSize = -1;
	m_lastModified = -1;
	// m_baseFilename contains the "dir" in it
	//sprintf(m_baseFilename ,"%s/%s", dirname  , baseFilename );
	//strcpy ( m_baseFilename , baseFilename  );
	//strcpy ( m_dir          , dir           );
	//if ( stripeDir ) strcpy ( m_stripeDir    , stripeDir     );
	//else             m_stripeDir[0] = '\0';
	// reset # of parts
	m_numParts = 0;
	m_maxParts = 0;
	// now add parts from both directories
	if ( ! addParts ( m_dir       ) ) return false;
	if ( ! addParts ( m_stripeDir ) ) return false;
	return true;
}
	

bool BigFile::addParts ( char *dirname ) {
	// if dirname is NULL return true
	if ( ! dirname[0] ) return true;
	// . now set the names of all the Files that we consist of
	// . get the directory entry and find out what parts we have
	Dir dir;
	dir.set ( dirname );
	// set our directory class
	if (!dir.open()) return log("disk: openDir (\"%s\") failed",dirname);
	// match files with this pattern in the directory
	char pattern[256];
	sprintf(pattern,"%s*", m_baseFilename );
	// length of the base filename
	long blen = gbstrlen ( m_baseFilename );
	// . set our m_files array
	// . addFile() will return false on problems
	// . the lower the fileId the older the file (w/ exception of #0)
	char *filename;
	while ( ( filename = dir.getNextFilename ( pattern ) ) ) {
		// if filename len is exactly blen it's part 0
		long flen = gbstrlen(filename);
		long part = -1;
		if ( flen == blen ) part = 0;
		// some files have the same first X chars, like 
		// indexdb.store-info-bak but are not part files
		else if ( flen > blen && strncmp(filename+blen,".part",5)!=0) 
			continue;
		// otherwise must end in .part%i
		else if (flen - blen < 6 ) {
			log ("disk: Part extension too small for \"%s\". "
			     "Must end in .partN to be valid.",
			     filename);
			continue;
		}
		else part = atoi ( filename + blen + 5 );
		// ensure not too big
		if ( part >= MAX_PART_FILES ) {
			log ("disk: Part number of %li is too big for "
			     "\"%s\". Should be less than %li.", 
			     (long)part,filename,(long)MAX_PART_FILES);
			continue;
		}
		// make this part file
		if ( ! addPart ( part ) ) return false;
	}
	// now set the names of all our files
	//for ( long n = 0 ; n < MAX_PART_FILES ; n++ ) 
	//m_files[n].set ( makeFilename ( n, m_baseFilename ) );
	return true;
}

bool BigFile::addPart ( long n ) {
	if ( n >= MAX_PART_FILES ) 
		return log("disk: Part number %li > %li.",
			   n,(long)MAX_PART_FILES);

	File *f ;
	try { f = new (File); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		return log("BigFile: new(%i): %s",sizeof(File), 
			   mstrerror(g_errno)); 
	}
	mnew ( f , sizeof(File) , "BigFile" );
	char buf[1024];
	makeFilename_r ( m_baseFilename , NULL, n , buf );
	f->set ( buf );
	m_files [ n ] = f;
	m_numParts++;
	// set maxPart
	if ( n+1 > m_maxParts ) m_maxParts = n+1;
	return true;
}

bool BigFile::doesExist ( ) {
	return m_numParts;
}

// if we can open it with a valid fd, then it exists
bool BigFile::doesPartExist ( long n ) {
	if ( n >= MAX_PART_FILES ) return false;
	bool exists = (bool)m_files[n];
	return exists;
}

// . overide File::open so we can set m_numParts
// . set maxFileSize when opening a new file for writing and using 
//   DiskPageCache
// . use maxFileSize of -1 for us to use getFileSize() to set it
bool BigFile::open ( int flags , class DiskPageCache *pc , 
		     long long maxFileSize ,
		     int permissions ) {

        m_flags       = flags;
	m_pc          = pc;
	m_permissions = permissions;
	m_isClosing   = false;
	// . init the page cache for this vfd
	// . this returns our "virtual fd", not the same as File::m_vfd
	// . returns -1 and sets g_errno on failure
	// . we pass m_vfd to getPages() and addPages()
	if ( m_pc ) {
		if ( maxFileSize == -1 ) maxFileSize = getFileSize();
		m_vfd = m_pc->getVfd ( maxFileSize, m_vfdAllowed );
		g_errno = 0;
	}
	return true;
}

// get the filename of the nth file using m_dir/m_stripeDir & m_baseFilename
void BigFile::makeFilename_r ( char *baseFilename    , 
			       char *baseFilenameDir , 
			       long  n               , 
			       char *buf             ) {
	char *dir = m_dir;
	if ( baseFilenameDir && baseFilenameDir[0] ) dir = baseFilenameDir;
	//static char s[1024];
	if ( (n % 2) == 0 || ! m_stripeDir[0] ) 
		sprintf ( buf, "%s/%s",   dir      , baseFilename );
	else    sprintf ( buf, "%s/%s", m_stripeDir, baseFilename );
	if ( n == 0 ) return ;
	sprintf ( buf + gbstrlen(buf) , ".part%li", n );
}

//int BigFile::getfdByOffset ( long long offset ) {
//	return getfd ( offset / MAX_PART_SIZE , true /*forReading?*/ );
//}

// . get the fd of the nth file
// . will try to open the file if it hasn't yet been opened
int BigFile::getfd ( long n , bool forReading , long *vfd ) {
	// boundary check
	if ( n >= MAX_PART_FILES ) 
		return log("disk: Part number %li > %li. fd not available.",
			   n,(long)MAX_PART_FILES) - 1;

	// get the File ptr from the table
	File *f = m_files[n];
	// if part does not exist then create it!
	if ( ! f ) {
		// don't create File if we're getting it for reading
		if ( forReading    ) return -1;
		if ( ! addPart (n) ) return -1;
		f = m_files[n];
	}
	// open it if not opened
	if ( ! f->isOpen() ) {
		if ( ! f->open ( m_flags , m_permissions ) ) {
			log("disk: Failed to open file part #%li.",n);
			return -1;
		}
	}
	// set it virtual fd, too
	if ( vfd ) *vfd = f->m_vfd;
	// get it's file descriptor
	int fd = f->getfd ( ) ;
	if ( fd >= -1 ) return fd;
	// otherwise, fd is -2 and it's never been opened?!?!
	g_errno = EBADENGINEER;
	log(LOG_LOGIC,"disk: fd is -2.");
	return -1;
}

// . return -2 on error
// . return -1 if does not exist
// . otherwise return the big file's complete file size (can be well over 2gb)
long long BigFile::getFileSize ( ) {
	// return if already computed
	if ( m_fileSize >= 0 ) return m_fileSize;

	// add up the sizes of each file
	long long totalSize = 0;
	for ( long n = 0 ; n < m_maxParts ; n++ ) {
		// we can have headless big files... count the heads
		if ( ! m_files[n] ) { totalSize += MAX_PART_SIZE; continue; }
		// . returns -2 on error, -1 if does not exist
		// . TODO: it returns 0 if does not exist! FIX...
		long size = m_files[n]->getFileSize();
		if ( size == -2 ) return -2;
		if ( size == -1 ) break;
		totalSize += size;
	}
	// save time
	m_fileSize = totalSize;
	return totalSize;
}

// . return -2 on error
// . return -1 if does not exist
// . otherwise returns the oldest of the last mod dates of all the part files
time_t BigFile::getLastModifiedTime ( ) {
	// return if already computed
	if ( m_lastModified >= 0 ) return m_lastModified;

	// add up the sizes of each file
	time_t min = -1;
	for ( long n = 0 ; n < m_maxParts ; n++ ) {
		// we can have headless big files... count the heads
		if ( ! m_files[n] ) continue;
		// returns -1 on error, 0 if file does not exist
		time_t date = m_files[n]->getLastModifiedTime();
		if ( date == -1 ) return -2;
		if ( date ==  0 ) break;
		// check min
		if ( date < min || min == -1 ) min = date;
	}
	// save time
	m_lastModified = min;
	return m_lastModified;
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we need a ptr to the ptr to this BigFile so if we get deleted and
//   a signal is still pending for us, the callback will know we are nuked
bool BigFile::read  ( void       *buf    , 
		      long        size   , 
		      long long   offset , 
		      FileState  *fs     ,                 
		      void       *state  ,
		      void      (* callback)(void *state) ,
		      long        niceness                ,
		      bool        allowPageCache ,
		      bool        hitDisk        ,
		      long        allocOff  ) {
	g_errno = 0;
	return readwrite ( buf , size , offset , false/*doWrite?*/, 
			   fs  , state, callback , niceness , allowPageCache ,
			   hitDisk , allocOff );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
bool BigFile::write ( void       *buf    , 
		      long        size   , 
		      long long   offset , 
		      FileState  *fs     ,
		      void       *state  ,
		      void      (* callback)(void *state) ,
		      long        niceness                ,
		      bool        allowPageCache ) {
	// sanity check
	if ( g_conf.m_readOnlyMode ) {
		logf(LOG_DEBUG,"disk: BigFile: Trying to write while in "
		     "read only mode.");
		return true;
	}
	g_errno = 0;
	//if ( m_pc && m_pc->m_isOverriden ) allowPageCache = false;
	return readwrite ( buf , size , offset , true/*doWrite?*/ , 
			   fs  , state, callback , niceness , allowPageCache ,
			   true , 0 );
}

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . we divide into 2 writes in case write spans 2 files
// . only BigFiles will support non-blocking read/writes for now
// . damn, i thought linux supported non-blocking file reads, but it doesn't!
// . we use the aio.h calls
// . we should us kaio from sgi cuz it's in the kernel and only uses 4 threads
//   whereas using librt.a creates a thread every time we call aio_read/write()
// . fstate is used by aio_read/write()
// . we need a ptr to the ptr to this BigFile so if we get deleted and
//   a signal is still pending for us, the callback will know we are nuked
bool BigFile::readwrite ( void         *buf      , 
			  long          size     , 
			  long long     offset   , 
			  bool          doWrite  ,
			  FileState    *fstate   ,
			  void         *state    ,
			  void        (* callback) ( void *state ) ,
			  long          niceness ,
			  bool          allowPageCache ,
			  bool          hitDisk        ,
			  long          allocOff ) {
	// are we blocking?
	bool isNonBlocking = m_flags & O_NONBLOCK;
	// if we're non blocking and caller didn't supply an "fstate"
	if ( isNonBlocking && ! fstate ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"disk: readwrite() call is "
		    "specified as non-blocking, but no state provided.");
		return true;
	}
	// reset file size in case we change it here
	if ( doWrite ) {
		m_fileSize = -1;
		m_lastModified = getTimeLocal();
	}
	// . sanity check
	// . when our offset was just a long 2gig+ files, when dumped,
	//   had negative offsets, bad engineer
	if ( offset < 0 ) {
		log(LOG_LOGIC,"disk: readwrite() offset is %lli "
		    "< 0. dumping core.",offset);
		char *xx = NULL; *xx = 0;
	}
	// if we're not blocking use a fake fstate
	FileState tmp;
	if ( ! fstate ) fstate = &tmp;
	// . no error yet
	// . need this up here in case it is a cache hit from a re-call
	//   due to a EFILECLOSED error
	//fstate->m_errno = 0;
	// offset to read into "buf"
	long bufOff = 0;
	// point to start of space allocated to hold what we read. "buf"
	// should be >= allocBuf + allocOff, depending on value of bufOff
	char *allocBuf = NULL;
	long  allocSize;
	// reset this
	fstate->m_errno = 0;
	// . try to get as much as we can from page cache first
	// . the vfd of the big file will be the vfd of its last File class
	if ( ! doWrite && m_pc && allowPageCache ) {
		long oldOff  = offset;
		// we have to set these so RdbScan doesn't freak out if we
		// have it all cached and return without hitting disk
		fstate->m_bytesDone = size;
		fstate->m_bytesToGo = size;
		//log("getting pages off=%lli size=%li",offset,size);
		// now we pass in a ptr to the buf ptr, because if buf is NULL
		// this will allocate one for us if it has some pages in the
		// cache that we can use.
		m_pc->getPages (m_vfd,(char **)&buf,size,offset,&size,&offset,
				&allocBuf,&allocSize,allocOff);
		//log("got     pages off=%lli size=%li",offset,size);
		bufOff = offset - oldOff;
		// comment out for test
		if ( size == 0 ) {
			// let caller/RdbScan know about the newly alloc'd buf
			fstate->m_buf         = (char *)buf;
			fstate->m_allocBuf    = allocBuf;
			fstate->m_allocSize   = allocSize;
			fstate->m_allocOff    = allocOff;
			return true;
		}
		// check
		//if ( m_pc->m_isOverriden && size < 0 ) {
		//	fstate->m_bytesDone += size;
		//	fstate->m_bytesToGo += size;
		//	return true;
		//}
	}
	// sanity check. if you set hitDisk to false, you must allow
	// us to check the page cache! silly bean!
	if ( ! allowPageCache && ! hitDisk ) { char*xx=NULL;*xx=0; }
	//if ( m_pc && m_pc->m_isOverriden )
	//	log ( LOG_INFO, "bigfile: HITTING DISK!! %li",
	//			(long)allowPageCache );
	// set up fstate
	fstate->m_this        = this;
	// buf may be NULL if caller passed in a NULL "buf" and it did not hit 
	// the disk page cache. Threads.cpp will have to allocate it right
	// before it launches the thread.
	fstate->m_buf         = (char *)buf + bufOff;
	// if getPages() allocates a buf, this will point to it
	fstate->m_allocBuf    = allocBuf;
	fstate->m_allocSize   = allocSize;
	// when buf is passed in as NULL we allocate it in Threads.cpp right 
	// before we launch it to save memory. it may also be allocated in 
	// DiskPageCache.cpp. we have to know where to start storing
	// the read into it for RdbScan, it is not immediately at the 
	// beginning of the allocated buffer because RdbScan may have to 
	// turn the first key from a 6 byte half key into a 12 byte key so it
	// needs some initial padding. this is because RdbLists should never
	// start with a 6 byte half key.
	fstate->m_allocOff    = allocOff;
	fstate->m_bytesToGo   = size;
	fstate->m_offset      = offset;
	fstate->m_doWrite     = doWrite;
	fstate->m_bytesDone   = 0;
	fstate->m_state       = state;
	fstate->m_callback    = callback;
	fstate->m_niceness    = niceness;
	fstate->m_flags       = m_flags;
	// . set our fd's before entering the thread in case RdbMerge
	//   calls our unlinkPart() 
	// . it's thread-UNsafe to call getfd() from within the thread
	// . FUCK! what if we get unlinked and another file gets this fd!!
	// . now we do do unlinks in a thread in File.cpp, but since we
	//   employ the getCloseCount_r() scheme we can detect when this
	//   situation occurs and pass a g_errno back to the caller.
	fstate->m_filenum1    =  offset          / MAX_PART_SIZE;
	fstate->m_filenum2    = (offset + size ) / MAX_PART_SIZE;
	// . save the open count for this fd
	// . if it changes when we're done with the read we do a re-read
	// . it gets incremented once every time File calls ::open and gets
	//   back this fd
	// . fd1 and fd1 are now set in Threads.cpp since we only want to do
	//   the open right before we actually launch the thread.
	//fstate->m_fd1         = getfd ( fstate->m_filenum1 , !doWrite , 
	//				&fstate->m_vfd1);
	//fstate->m_fd2         = getfd ( fstate->m_filenum2 , !doWrite , 
	//				&fstate->m_vfd2);
	fstate->m_fd1  = -3;
	fstate->m_fd2  = -3;
	fstate->m_vfd1 = -3;
	fstate->m_vfd2 = -3;
	// . if we are writing, prevent these fds from being closed on us
	//   by File::closedLeastUsed(), because the fd could then be re-opened
	//   by someone else doing a write and we end up writing to THAT FILE!
	// . the closeCount mechanism helps us DETECT when something like this
	//   happens, but it will not prevent the write from going through
	if ( doWrite ) {
		// actually have to do the open here for writing so it
		// can prevent the fds from being closed on us
		fstate->m_fd1         = getfd ( fstate->m_filenum1 , !doWrite, 
						&fstate->m_vfd1);
		fstate->m_fd2         = getfd ( fstate->m_filenum2 , !doWrite, 
						&fstate->m_vfd2);
		//File *f1 = m_files [ fstate->m_filenum1 ];
		//File *f2 = m_files [ fstate->m_filenum2 ];
		enterWriteMode( fstate->m_vfd1 );
		enterWriteMode( fstate->m_vfd2 );
		fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
		fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );
	}
	// get the close counts after calling getfd() since if getfd() calls
	// File::open() that will inc the counts
	// closeCount1 and 2 are now set in Threads.cpp since we want to only 
	// open the fd right before we launch the thread.
	//fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
	//fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );
	fstate->m_errno       = 0;
	fstate->m_errno2      = 0;
	fstate->m_startTime   = gettimeofdayInMilliseconds();
	fstate->m_pc          = m_pc;
	if ( ! allowPageCache )
		fstate->m_pc = NULL;
	fstate->m_vfd         = m_vfd;
	// if hitDisk was false we only check the page cache!
	if ( ! hitDisk ) return true;
	// if disk stuck, forget about it! but make the spider disk reads
	// wait until it is unstuck. just don't want to screw up the queries..
	if ( g_diskIsStuck && niceness == 0 && ! doWrite ) {
		g_errno = fstate->m_errno = EDISKSTUCK;
		return true;
	}
	// . if we're blocking then do it now
	// . this should return false and set g_errno on error, true otherwise
	if ( ! isNonBlocking ) 	goto skipThread;
	// . otherwise, spawn a thread to do this i/o
	// . this returns false and sets g_errno on error, true on success
	// . we should return false cuz we blocked
	// . thread will add signal to g_loop on completion to call
	if ( g_threads.call ( DISK_THREAD/*threadType*/, niceness , fstate ,
			      doneWrapper , readwriteWrapper_r) ) return false;
	// note it
	if ( g_errno ) {
		static time_t s_time  = 0;
		time_t now = getTime();
		if ( now - s_time > 5 ) {
			log (LOG_INFO,"disk: Thread call failed: %s.", 
			     mstrerror(g_errno));
			s_time = now;
		}
	}
	// sanity check
	if ( ! callback ) { char *xx = NULL; *xx = 0; }
	// NOW we return on error because if we already have 5000 disk threads
	// queued up, what is the point in blocking ourselves off? that makes
	// us look like a dead host and very unresponsive. As long as this
	// request originated through Multicast, then multicast will sleep
	// and retry. Msg3 could retry, the multicast thing should be more
	// for running out of udp slots though...
	if ( g_errno && ! doWrite && g_errno != ENOTHREADSLOTS ) {
		log (LOG_INFO,"disk: May retry later.");
		return true;
	}
	// otherwise, thread spawn failed, do it blocking then
	g_errno = 0;
	if ( ! g_threads.m_disabled ) {
		static long s_lastTime = 0;
		long now = getTime();
		if ( now - s_lastTime >= 1 ) {
			s_lastTime = now;
			log (LOG_INFO,
			     "disk: Doing blocking disk access. This will hurt "
			     "performance. isWrite=%li.",(long)doWrite);
		}
	}
	// come here if we haven't spawned a thread
 skipThread:
	// if there was no room in the thread queue, then we must do this here
	fstate->m_fd1         = getfd ( fstate->m_filenum1 , !doWrite , 
					&fstate->m_vfd1);
	fstate->m_fd2         = getfd ( fstate->m_filenum2 , !doWrite , 
					&fstate->m_vfd2);
	fstate->m_closeCount1 = getCloseCount_r ( fstate->m_fd1 );
	fstate->m_closeCount2 = getCloseCount_r ( fstate->m_fd2 );
	// clear g_errno from the failed thread spawn
	g_errno = 0;

	// since Threads.cpp usually allocs the buffer before launching,
	// we must do it here now
	FileState *fs = fstate;
	if ( ! fs->m_doWrite && ! fs->m_buf && fs->m_bytesToGo > 0 ) {
		long need = fs->m_bytesToGo + fs->m_allocOff;
		char *p = (char *) mmalloc ( need , "ThreadReadBuf" );
		if ( p ) {
			fs->m_buf       = p + fs->m_allocOff;
			fs->m_allocBuf  = p;
			fs->m_allocSize = need;
		}
		else 
			log("disk: read buf alloc failed for %li "
			    "bytes.",need);
	}
	
	// . this returns false and sets errno on error
	// . set g_errno to the errno
	if ( ! readwrite_r ( fstate , NULL ) ) g_errno = errno;
	// exit write mode
	if ( doWrite ) {
		//File *f1 = m_files [ fstate->m_filenum1 ];
		//File *f2 = m_files [ fstate->m_filenum2 ];
		//f1->exitWriteMode();
		//f2->exitWriteMode();
		exitWriteMode( fstate->m_vfd1 );
		exitWriteMode( fstate->m_vfd2 );
	}

	// set this up here
	fstate->m_bytesDone = fstate->m_bytesToGo;
	// and this too
	fstate->m_doneTime = gettimeofdayInMilliseconds();

	// if it read less than 8MB/s bitch
	long long now   = gettimeofdayInMilliseconds() ;
	long long took  = now - fstate->m_startTime ;
	long      rate  = 100000;
	if ( took  > 500 ) rate = fstate->m_bytesDone / took ;
	if ( rate < 8000 && fstate->m_niceness <= 0 ) {
		log(LOG_INFO,"disk: Read %li bytes in %lli ms (%liMB/s).",
		    fstate->m_bytesDone,took,rate);
		g_stats.m_slowDiskReads++;
	}
	// default graph color is black
	int color = 0x00000000; 
	char *label = "disk_read";
	// use red for writes, though
	if ( fstate->m_doWrite ) {
		color = 0x00ff0000;
		label = "disk_write";
	}
	// but gray for low priority reads
	else if ( fstate->m_niceness > 0 ) color = 0x00808080;
	// add the stat
	g_stats.addStat_r ( fstate->m_bytesDone          ,
			    fstate->m_startTime          ,
			    now                          ,
			    //label                        ,
			    color                        );
	// add to statsdb as well
	//g_statsdb.addStat ( fstate->m_niceness,
	//		    label,
	//		    fstate->m_startTime,
	//		    now,
	//		    fstate->m_bytesDone);

	// store read/written pages into page cache
	if ( ! g_errno && fstate->m_pc )
		fstate->m_pc->addPages ( fstate->m_vfd       ,
					 fstate->m_buf       ,
					 fstate->m_bytesDone ,
					 fstate->m_offset    ,
					 fstate->m_niceness  );
	// now log our stuff here
	if ( g_errno && g_errno != EBADENGINEER ) 
		log("disk: readwrite: %s", mstrerror(g_errno));
	// . this EBADENGINEER can happen right after a merge if
	//   the file is renamed because the fd may have changed from
	//   under us
	// . i added EBADF because RbdDump was failing because of this when
	//   trying to write the tree to a file
	// . EBADF happens when we unlink a file from under a read or write
	// . the closeCount code below was not saving us from coring on EBADF
	//   because the closeCount is only changed if another file is opened
	//   with that fd, it is not incremented on a close() but rather on
	//   an open()
	/*
	if ( g_errno == EBADENGINEER ) { // || g_errno == EBADF ) {
		long fn1 = fstate->m_filenum1;
		long fn2 = fstate->m_filenum2;
		char *s = getFilename();
		log(LOG_DEBUG,"disk: Closing old fd1 (%s,%li)",s,fn1);
		log(LOG_DEBUG,"disk: Closing old fd2 (%s,%li)",s,fn2);
		// get the File ptr from the table
		File *f1 = getFile(fn1);
		File *f2 = getFile(fn2);
		if ( f2 == f1 ) f2 = NULL;
		log(LOG_DEBUG,"disk: Closing old fd1 (%s,%li)",s,fn1);
		if ( f2) log(LOG_DEBUG,"disk: Closing old fd2 (%s,%li)",s,fn2);
		if ( f1 ) f1->close();
		if ( f2 ) f2->close();
	}
	*/
	// we didn't block so return true
	return true;
}

// . this should be called from the main process after getting our call OUR callback here
void doneWrapper ( void *state , ThreadEntry *t ) {

	FileState *fstate = (FileState *)state;

	// any writes we did in the disk read thread were done to the
	// "tmp" FileState class on the stack, so now we have the real deal
	// we can update all this junk.
	fstate->m_bytesDone = fstate->m_bytesToGo;
	fstate->m_doneTime  = t->m_exitTime; // set in Threads.cpp
	fstate->m_errno     = t->m_errno;

	// exit write mode
	if ( fstate->m_doWrite ) {
		// THIS could have been deleted!!
		//BigFile *THIS = fstate->m_this;
		//File *f1 = THIS->m_files [ fstate->m_filenum1 ];
		//File *f2 = THIS->m_files [ fstate->m_filenum2 ];
		//f1->exitWriteMode();
		//f2->exitWriteMode();
		exitWriteMode( fstate->m_vfd1 );
		exitWriteMode( fstate->m_vfd2 );
	}
	// if it read less than 8MB/s bitch
	long long took = fstate->m_doneTime - fstate->m_startTime;
	long      rate = 100000;
	if ( took > 500 ) rate = fstate->m_bytesDone / took ;
	bool slow = false;
	if ( rate < 8000 ) slow = true;
	if ( fstate->m_errno == EDISKSTUCK ) slow = true;
	if ( slow && fstate->m_niceness <= 0 ) {
		if ( fstate->m_errno != EDISKSTUCK )
		  log(LOG_INFO, "disk: Read %li bytes in %lli ms (%liMB/s).",
		    fstate->m_bytesDone,took,rate);
		g_stats.m_slowDiskReads++;
	}
	// get the BigFIle
	//BigFile *THIS = fs->m_this;
	// recall g_errno from state's m_errno
	g_errno = fstate->m_errno;
	// might have had the file renamed/unlinked from under us
	if ( ! g_errno ) g_errno = fstate->m_errno2;
	// fstate has his own m_pc in case BigFile got deleted, we cannot
	// reference it...
	if ( ! g_errno && fstate->m_pc )
		fstate->m_pc->addPages ( fstate->m_vfd       ,
					 fstate->m_buf       ,
					 fstate->m_bytesDone ,
					 fstate->m_offset    ,
					 fstate->m_niceness  );

	// add the stat
	if ( ! g_errno ) {
		// default graph color is black
		int color = 0x00000000; 
		char *label = "disk_read";
		// use red for writes, though
		if ( fstate->m_doWrite ) {
			color = 0x00ff0000;
			label = "disk_write";
		}
		// but gray for low priority reads
		else if ( fstate->m_niceness > 0 ) color = 0x00808080;
		// add it
		g_stats.addStat_r ( fstate->m_bytesDone          ,
				    fstate->m_startTime          ,
				    fstate->m_doneTime           ,
				    //label                        ,
				    color                        );
		// add to statsdb as well
		//g_statsdb.addStat ( fstate->m_niceness,
		//		    label,
		//		    fstate->m_startTime,
		//		    fstate->m_doneTime,
		//		    fstate->m_bytesDone);
	}

	// debug msg
	//char *s = "read";
	//if ( fstate->m_doWrite ) s = "wrote";
	//char *t = "no";	// are we blocking?
	//if ( fstate->m_this->getFlags() & O_NONBLOCK ) t = "yes";
	// this is bad for real-time threads cuz our unlink() routine may
	// have been called by RdbMerge and our m_files may be altered 
	//log("disk::readwrite: %s %li bytes from %s(nonBlock=%s)",s,n,
	//    m_files[filenum]->getFilename(),t);
	//log("disk::readwrite_r: %s %li bytes (nonBlock=%s)",
	//     s,fstate->m_bytesDone/*n*/,t);
	// debug msg
	//long took = gettimeofdayInMilliseconds() - fstate->m_startTime ;
	//log("read of %li bytes took %li ms",fstate->m_bytesDone, took);
	// now log our stuff here
	long tt = LOG_WARN;
	if ( g_errno == EFILECLOSED ) tt = LOG_INFO;
	if ( g_errno && g_errno != EDISKSTUCK ) 
		log (tt,"disk: %s. fd1=%li vfd=%li "
			    "off=%lli toread=%li.", 
			    mstrerror(g_errno),
			    (long)fstate->m_fd1,(long)fstate->m_vfd,
			    (long long)fstate->m_offset , 
			    (long)fstate->m_bytesToGo );
	// someone is closing our fd without setting File::s_vfds[fd] to -1
	if ( g_errno && g_errno != EDISKSTUCK ) {
		//int fd1  = fstate->m_fd1;
		//int fd2  = fstate->m_fd2;
		int vfd1 = fstate->m_vfd1;
		int vfd2 = fstate->m_vfd2;
		int ofd1 = getfdFromVfd(vfd1);
		int ofd2 = getfdFromVfd(vfd2);
		log(tt,"disk: vfd1=%i s_fds[%i]=%i.",vfd1,vfd1,ofd1);
		log(tt,"disk: vfd2=%i s_fds[%i]=%i.",vfd2,vfd2,ofd2);
	}
	// . this EBADENGINEER can happen right after a merge if
	//   the file is renamed because the fd may have changed from
	//   under us
	// . i added EBADF because RbdDump was failing because of this when
	//   trying to write the tree to a file
	// . the closeCount code below was not saving us from coring on EBADF
	//   because the closeCount is only changed if another file is opened
	//   with that fd, it is not incremented on a close() but rather on
	//   an open()
	/*
	if ( g_errno == EBADENGINEER ) { // || g_errno == EBADF ) {
		long fn1 = fstate->m_filenum1;
		long fn2 = fstate->m_filenum2;
		// CAUTION: if file got delete THIS will be invalid!!!
		BigFile *THIS = fstate->m_this;
		char *s = THIS->getFilename();
		log(LOG_DEBUG,"disk: Closing old fd1 (%s,%li)",s,fn1);
		log(LOG_DEBUG,"disk: Closing old fd2 (%s,%li)",s,fn2);
		// get the File ptr from the table
		File *f1 = THIS->getFile(fn1);
		File *f2 = THIS->getFile(fn2);
		if ( f2 == f1 ) f2 = NULL;
		if ( f1 ) { f1->close();log(LOG_DEBUG,"disk: Closed old fd1");}
		if ( f2 ) { f2->close();log(LOG_DEBUG,"disk: Closed old fd2");}
	}
	*/
	// call the callback, with errno set if there was an error
	fstate->m_callback ( fstate->m_state );
}


void *readwriteWrapper_r ( void *state , ThreadEntry *t ) {
	// debug msg
	//log("disk: this thread id = %li",(long)pthread_self());

	// if we were queued and now we are launching stuck, just return now
	//if ( g_diskIsStuck ) {
	//	t->m_errno = EDISKSTUCK;
	//	return NULL;
	//}

	// if we got hit before we set m_readyForBail to true we must have
	// been hit pre-launch... so bail quickly in that case...
	if ( t && t->m_callback == ohcrap ) {
		t->m_errno = EDISKSTUCK;
		return NULL;
	}

	// extract our class
	FileState *orig = (FileState *)state;

	// save this shit on the stack in case fstate gets pull from under us
	FileState tmp;
	memcpy ( &tmp , orig , sizeof(FileState ));
	FileState *fstate = &tmp;

	// lead Threads::bailOnReads() know we can be bailed on now since
	// we have copied over all the date we can from fstate, which can
	// be pulled out from under us now
	t->m_readyForBail = true;

	// get THIS
	//BigFile *THIS = fstate->m_this;
	// clear thread's errno
	errno = 0;
	// . make it so we go away immediately upon receiving a cancellation 
	//   signal rather than queing the signal until we call 
	//   pthread_testcancel()
	// . this allows us to immediately hault disk reads/writes that are
	//   lower priority than i/o's we're about to do
	// . this is so merging won't affect queries per second so much
	//int err = pthread_setcanceltype ( PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
	//if ( err != 0 ) log("readwriteWrapper: pthread_setcanceltype: %s",
	//		      mstrerror(err) );
	// . do the readwrite_r() since we're a thread now
	// . this SHOULD NOT set g_errno, we're a thread!
	// . it does have it's own errno however
	// . if this gets a cancel signal in the read() it will stop blocking
	//   and errno will be EINTR
 again:
	bool status = readwrite_r ( fstate , t ) ;
	// did our callback get pre-called by Process.cpp/Threads.cpp? 
	// fstate is probably invalid then, so watch out!
	if ( t && t->m_callback == ohcrap ) return NULL;
	// set errno
	if ( ! status ) fstate->m_errno = errno;
	// test again here
	//pthread_testcancel();

	// get the two files
	File *f1 = NULL;
	File *f2 = NULL;
	// when we exit, m_this is invalid!!!
	if ( fstate->m_filenum1 < fstate->m_this->m_maxParts )
		f1 = fstate->m_this->m_files[fstate->m_filenum1];
	if ( fstate->m_filenum2 < fstate->m_this->m_maxParts )
		f2 = fstate->m_this->m_files[fstate->m_filenum2];

	// . if open count changed on us our file got unlinked from under us
	//   and another file was opened with that same fd!!! 
	// . just fail the read so caller knows it is bad
	// . do not do this for writes because RdbDump can fail when writing!
	// . in that case hopefully write will fail if the fd was re-opened
	//   for another file in RDONLY mode, but, if per chance it opens
	//   a different file for dumping or merging with this same fd then
	//   we may be seriously screwing things up!! TODO: investigate
	// . f1 and f2 can be non-null and invalid here now on the ssds
	//   i saw this happen on gk153... i preserved the core/gb on there
	//if ( (getCloseCount_r (fstate->m_fd1) != fstate->m_closeCount1 || 
	//      getCloseCount_r (fstate->m_fd2) != fstate->m_closeCount2   )) {
	if ( ! f1 || 
	     ! f2 ||
	     f1->m_closeCount != fstate->m_closeCount1 || 
	     f2->m_closeCount != fstate->m_closeCount2   ) {

		long cc1 = -1;
		long cc2 = -1;
		if ( f1 ) cc1 = f1->m_closeCount;
		if ( f2 ) cc2 = f2->m_closeCount;
		log("file: c1a=%li c1b=%li c2a=%li c2b=%li",
		    cc1,fstate->m_closeCount1,
		    cc2,fstate->m_closeCount2);
		    
		if ( ! fstate->m_doWrite ) fstate->m_errno = EFILECLOSED;
		// we use s_writing[] locks in File.cpp to prevent a write
		// operation's fd from being closed under him
		else log("PANIC: fd closed on us while writing. This should "
			 "never happen!! Simultaneous writes?");
	}
	// if it wasn't cancelled, just interrupted, try again
	if ( errno == EINTR ) {
		errno           = 0;
		fstate->m_errno = 0;
		goto again; 
	}
	// turn off the cancel-ability of this thread
	//pthread_setcancelstate ( PTHREAD_CANCEL_DISABLE , NULL );
	// set done time even if errno set
	// - mdw, can't set this here now because fstate might be invalid...
	//long long now = gettimeofdayInMilliseconds() ;
	//fstate->m_doneTime = now;
	/*
	// add the stat
	if ( ! errno ) {
		// default graph color is black
		int color = 0x00000000; 
		char *label = "disk_read";
		// use red for writes, though
		if ( fstate->m_doWrite ) {
			color = 0x00ff0000;
			label = "disk_write";
		}
		// but gray for low priority reads
		else if ( fstate->m_niceness > 0 ) color = 0x00808080;
		// add it
		g_stats.addStat_r ( fstate->m_bytesDone          ,
				    fstate->m_startTime          ,
				    now                          ,
				    label                        ,
				    color                        );
	}
	*/
	// debug msg
	//fprintf(stderr,"BigFile exiting thread, state=%lu\n",(long)fstate);
	// . we're all done, tell g_threads
	// . this never returns
	// . the state must be unique per thread so we know what thread this is
	// . i tried using pthread_self() but we'd have to store it in 
	//   g_thread's ThreadEntry ourselves, as a thread
	// . the thread's cleanUp handler should call g_threads.exit(fstate)
	//g_threads.exit ( fstate );
	//pthread_exit ( NULL );

	// update this since our updates were done to the FileState "tmp"
	// which is just on the stack
	t->m_errno = fstate->m_errno;

	// bogus return
	return NULL;
}

// . returns false and sets errno on error, true on success
// . don't log shit when you're in a thread anymore
// . if we receive a cancel sig while in pread/pwrite it will return -1
//   and set errno to EINTR
bool readwrite_r ( FileState *fstate , ThreadEntry *t ) {
	// if no buffer to read into the alloc in Threads.cpp failed
	if ( ! fstate->m_buf ) {
		errno = EBUFTOOSMALL;
		return log( "disk: read buf is NULL. malloc failed?");
	}
	// how many total bytes to write?
	long       bytesToGo = fstate->m_bytesToGo; //- fstate->m_bytesDone;
	// how many bytes we've written so far
	long       bytesDone = fstate->m_bytesDone;
	// get current offset
	long long  offset    = fstate->m_offset + fstate->m_bytesDone;
	// are we writing? or reading?
	bool       doWrite   = fstate->m_doWrite;
	// point to buf
	char      *p         = fstate->m_buf + bytesDone ;
 loop:
	// return here if done
	if ( bytesDone >= bytesToGo ) return true;
	// rand segv test (test how new cloned children handle it
	//if ( g_threads.amThread() && (rand() % 10) == 1 ) { 
	//	log("FORCING SEG FAULT");
	//	char *xx = NULL; *xx = 0; 
	//}
	// translate offset to a filenum and offset
	long filenum     = offset / MAX_PART_SIZE;
	long localOffset = offset % MAX_PART_SIZE;
	// how many bytes to read/write to first little file?
	long avail = MAX_PART_SIZE - localOffset;
	// how may bytes do we have left to read/write
	long len   = bytesToGo - bytesDone;
	// how many bytes can we write to it now
	if ( len > avail ) len = avail;
	// get the fd for this filenum
	int fd = -1;
	if      ( filenum == fstate->m_filenum1 ) fd = fstate->m_fd1;
	else if ( filenum == fstate->m_filenum2 ) fd = fstate->m_fd2;
	// this old way wasn't thread safe since unlinkPart() could be called
	// fd = getfd ( filenum , !doWrite );
	// return -1 on error 
	if ( fd < 0 ) {
		errno = EBADENGINEER;
		log(LOG_LOGIC, "disk: fd < 0. Bad engineer.");
		return false; //log("disk::readwrite_r: fd is negative");
	}

	// did our callback get pre-called by Process.cpp/Threads.cpp?
	if ( t && t->m_callback == ohcrap ) return false;

	// only set this now if we are the first one
	if ( g_threads.m_threadQueues[DISK_THREAD].m_hiReturned ==
	     g_threads.m_threadQueues[DISK_THREAD].m_hiLaunched ) 
		g_lastDiskReadStarted = fstate->m_startTime;

	// fake it out
	//static long s_poo = 0;
	//s_poo++;
	//if ( s_poo > 1125 )sleep(5);
	//log("disk: spoo=%li",s_poo);

	// reset this
	errno = 0;

	// n holds how many bytes read/written
	int n ;
 retry25:

	// do the read/write blocking 
	if ( doWrite ) 	n = pwrite ( fd , p , len , localOffset );
	else           	n = pread  ( fd , p , len , localOffset );

	// interrupted system call?
	if ( n < 0 && errno == EINTR ) 
		goto retry25;

	// this is thread safe...
	g_lastDiskReadCompleted = g_now; // gettimeofdayInMilliseconds_r();

	// debug msg
	//char *s = "read";
	//if ( fstate->m_doWrite ) s = "wrote";
	//char *t = "no";	// are we blocking?
	//if ( fstate->m_this->getFlags() & O_NONBLOCK ) t = "yes";
	// this is bad for real-time threads cuz our unlink() routine may
	// have been called by RdbMerge and our m_files may be altered 
	//log("disk::readwrite: %s %li bytes from %s(nonBlock=%s)",s,n,
	//    m_files[filenum]->getFilename(),t);
	//log("disk::readwrite_r: %s %li bytes (nonBlock=%s)", s,n,t);
	//log("disk::readwrite_r: did %li bytes", n);

	// . if n is 0 that's strange!!
	// . i think the fd will have been closed and re-opened on us if this
	//   happens... usually
	if (n==0 && len > 0 ) {
		log("disk: Read of %li bytes at offset %lli for %s "
		    "failed because file is too short for that "
		    "offset? Our fd was probably stolen from us by another "
		    "thread. Will retry. error=%s.",
		    (long)len,fstate->m_offset,
		    fstate->m_this->getFilename(),mstrerror(errno));
		errno = EBADENGINEER;
		return false; // log("disk::read/write: offset too big");
	}
	// return bytes we did if we blocked ( and reset errno )
	//if ( n < 0 && errno == EAGAIN ) { errno = 0; return false; }
	// . for some reason we sometimes get interrupted, so just try again
	// . we should block all signals a thread can get, but it seems
	//   if the parent process gets a signal the thread gets it too!
	// . this could be a thread cancel signal!!!!!!
	//if ( n < 0 && errno == EINTR ) { 
	//	//log("disk::readWrite_r: %s",mstrerror(errno));
	//	errno = 0; 
	//	goto loop; 
	//}
	// on other errno, return -1
	if ( n < 0 ) { 
		log("disk::readwrite_r: %s",mstrerror(errno));
		return false; 
	}
	// bitch if didn't read what we wanted
	//if ( n != len )
	//	log("disk::readwrite_r: only did %li, needed %li",n,len);
	// . flush the write
	// . linux's write cache may be messing with my data!
	// . no, turns out write errors (garbage written) happens anyway...
	// . now we flush all writes! skip bdflush man.
	// . only allow syncing if file is non-blocking, because blocking
	//   writes are used for when we call RdbTree::fastSave_r() and it
	//   takes forever to dump Spiderdb if we sync each little write
	if ( g_conf.m_flushWrites   && 
	     doWrite                && 
	     (fstate->m_flags & O_NONBLOCK) && 
	     fdatasync ( fd ) < 0  ) {
		log("disk: fdatasync: %s", mstrerror(errno));
		// ignore an error here
		errno = 0;
	}
	// update the count
	bytesDone += n;
	// inc the main offset and the buffer ptr, "p"
	offset    += n; 
	p         += n;
	// add to fileState
	fstate->m_bytesDone += n;
	// loop back
	goto loop;
}

////////////////////////////////////////
// non-blocking unlink/rename code
////////////////////////////////////////

bool BigFile::unlink ( ) {
	return unlinkRename ( NULL , -1 , false, NULL, NULL );
}
bool BigFile::rename ( char *newBaseFilename , char *newBaseFilenameDir ) {
	return unlinkRename ( newBaseFilename, -1, false, NULL, NULL ,
			      newBaseFilenameDir );
}
bool BigFile::chopHead ( long part ) {
	return unlinkRename ( NULL, part, false, NULL, NULL );
}

bool BigFile::unlink ( void (* callback) ( void *state ) , 
		       void *state ) {
	return unlinkRename ( NULL , -1 , true, callback , state );
}

bool BigFile::rename ( char *newBaseFilename ,
		       void (* callback) ( void *state ) , 
		       void *state ) {
	return unlinkRename ( newBaseFilename, -1, true, callback, state);
}

bool BigFile::chopHead ( long part , 
			 void (* callback) ( void *state ) , 
			 void *state ) {
	//for ( long i = 0 ; i < part ; i++ ) 
	// set return value to false if we blocked somewhere
	return unlinkRename ( NULL, part, true, callback, state );
}

static void *renameWrapper_r   ( void *state , ThreadEntry *t ) ;
static void *unlinkWrapper_r   ( void *state , ThreadEntry *t ) ;
static void  doneRenameWrapper ( void *state , ThreadEntry *t ) ;
static void  doneUnlinkWrapper ( void *state , ThreadEntry *t ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . ser "part" to -1 to remove or unlink all part files
// . "newBaseFilenameDir" if NULL, defaults to m_dir, the current dir
//   in which this file already exists
bool BigFile::unlinkRename ( // non-NULL for renames, NULL for unlinks
			     char *newBaseFilename             ,
			     // part num to unlink, -1 for all (or rename)
			     long  part                        , 
			     bool  useThread                   ,
			     void (* callback) ( void *state ) , 
			     void *state                       ,
			     char *newBaseFilenameDir          ) {
	// fail in read only mode
	if ( g_conf.m_readOnlyMode ) {
		g_errno = EBADENGINEER;
		log("disk: cannot unlink or rename files in read only mode");
		return true;
	}

	// . wait for any previous unlink to finish
	// . we can only store one callback at a time, m_callback, so we
	//   must do this for now
	if ( m_numThreads > 0 && 
	     ( callback != m_callback || state != m_state ) ) {
		g_errno = EBADENGINEER;
		log("disk: Unlink/rename threads are in progress.");
		return true;
	}
	// . is this a rename?
	// . hack off any directory in newBaseFilename
	if ( newBaseFilename ) {
		// well, now Rdb.cpp's moveToTrash() moves an old rdb file
		// into the trash subdir, so we must preserve the full path
		char *s ;
		while( (s=strchr(newBaseFilename,'/'))) newBaseFilename = s+1;
		// close all files -- they close themselves when we call rename
		// close ();
		// . set a new base filename for us
		// . readwriteWrapper_r() will retry a read if a rename is
		//   going on and a read fails. after each rename thread is
		//   done (doneWrapper) it will call File::set.
		// . when all renames have completed then 
		//   m_bigFile::m_baseFilename will be set to m_newBaseFilename
		strcpy ( m_newBaseFilename , newBaseFilename );
		// save this guy
		if ( newBaseFilenameDir )
			strcpy ( m_newBaseFilenameDir , newBaseFilenameDir );
		else 
			m_newBaseFilenameDir[0] = '\0';
		// set the op flag
		m_isUnlink = false;
	}
	else  
		m_isUnlink = true;

	// close all files
	//close ();
	// . unlink likes to sometimes just unlink one part at a time
	// . this should be -1 to unlink all at once
	m_part = part;
	// the state varies
	void *(*startRoutine)(void *state,ThreadEntry *t);
	void  (*doneRoutine )(void *state,ThreadEntry *t);

	long i = 0;
	if ( m_part >= 0 ) i = m_part;

	// how many parts have we done?
	m_partsRemaining = m_maxParts;
	// is it only 1 to be unlinked?
	if ( m_part >= 0 ) m_partsRemaining = 1;

	for ( ; i < m_maxParts ; i++ ) {
		// break out if we should only unlink one part
		if ( m_part >= 0 && i != m_part ) break;
		// get the ith file to rename/unlink
		File *f = m_files[i];
		if ( ! f ) {
			// one less part to do
			m_partsRemaining--;
			continue;
		}
		// remove it from disk
		if (  m_isUnlink ) {
			startRoutine = unlinkWrapper_r ;
			doneRoutine  = doneUnlinkWrapper ;
		}
		else {
			startRoutine = renameWrapper_r ;
			doneRoutine  = doneRenameWrapper ;
		}
		// base in ptr to file, but set f->m_this and f->m_i 
		f->m_this = this;
		f->m_i    = i;
		// assume thread launched, doneRoutine() will decrement these
		m_numThreads++; 
		g_unlinkRenameThreads++;
		// skip thread?
		if ( ! useThread ) goto skipThread;
		// save callback for when all parts are unlinked or renamed
		m_callback = callback;
		m_state    = state;
		// . we spawn the thread here now
		// . returns true on successful spawning
		// . we can't make a disk thread cuz Threads.cpp checks its
		//   FileState member for readSize for thread throttling
		if ( g_threads.call (UNLINK_THREAD/*threadType*/,1/*niceness*/,
				     f , doneRoutine , startRoutine ) )
			continue;
		// otherwise, thread spawn failed, do it blocking then
		log(LOG_INFO,
		    "disk: Failed to launch unlink/rename thread for %s. "
		    "Doing blocking unlink. part=%li/%li. "
		    "This will hurt performance. "
		    "%s.",f->getFilename(),i,m_part,mstrerror(g_errno));
	skipThread:
		// log these for now, remove later
		logf(LOG_DEBUG,"disk: Unlinking/renaming %s without thread.",
		     f->getFilename());
		// before we call doneRoutine(), we must NULLify the callback 
		m_callback = NULL;
		// clear errno, cause startRoutine() may set it
		errno = 0;
		// these are normally called from a thread
		startRoutine ( f , NULL );
		// copy errno over to g_errno
		if ( errno ) g_errno = errno;
		// wrap it up
		doneRoutine  ( f , NULL );
		// set his new name now if we're a rename with no thread
		//if ( m_isUnlink ) {
		//	// consider the part file, if any, nuked
		//	if ( m_part >= 0 ) removePart ( m_part ); // NO!
		//	continue;
		//}
		//startRoutine ( f );
		// follow up on the call to close1_r()
		//f->close2();
		//char newFilename [ 1024 ];
		//makeFilename_r  ( m_newBaseFilename,i,newFilename);
		//m_files[i]->set ( newFilename );
	}

	// remove pages from DiskPageCache if all files unlinked
	if ( m_isUnlink && part == -1 ) {
		// release it first, cuz the removeThreads() below
		// may call QUICKPOLL() and we end up reading from same file!
		if ( m_pc ) m_pc->rmVfd ( m_vfd );
		// remove all queued threads that point to us that have not
		// yet been launched
		g_threads.m_threadQueues[DISK_THREAD].removeThreads(this);
	}
	// close em up
	//close();
	// if one blocked, we block, but never return false if !useThread
	if ( m_numThreads > 0 && useThread ) return false;
	// . if we launched no threads update OUR base filename right now
	if ( ! m_isUnlink ) strcpy ( m_baseFilename , m_newBaseFilename );
	// we did not block
	return true;
}

void *renameWrapper_r ( void *state , ThreadEntry *t ) {
	// extract our class
	File *f = (File *)state;
	// . by getting the inode in the cache space the call to f->close()
	//   in doneRenameWrapper() should not block
	// . fd is < 0 if invalid, >= 0 if valid
	//int fd = f->getfdNoOpen ();
	// hey, it still blocks
	//if ( fd >= 0 ) fsync ( fd );
	// get the big guy and the i in m_files[i]
	BigFile *THIS = (BigFile *)f->m_this;
	// get the ith file we just unlinked
	long      i = f->m_i;
	// . get the new full name for this file
	// . based on m_dir/m_stripeDir and m_baseFilename
	char newFilename [ 1024 ];
	THIS->makeFilename_r ( THIS->m_newBaseFilename    , 
			       THIS->m_newBaseFilenameDir , 
			       i                          , 
			       newFilename                );
	char oldFilename [ 1024 ];
	THIS->makeFilename_r ( THIS->m_baseFilename       ,
			       NULL                       ,
			       i                          , 
			       oldFilename                );
	//if ( m_files[i]->rename ( newFilename ) ) continue;
	// this returns 0 on success
	if ( ::rename ( oldFilename , newFilename ) ) {
		// reset errno and return true if file does not exist
		if ( errno == ENOENT ) {
			log("disk: file %s does not exist.",oldFilename);
			errno = 0; 
		}
		// otherwise, it's a more serious error i guess
		else log("disk: rename %s to %s: %s", 
			   oldFilename,newFilename,mstrerror(errno));
		return NULL;
	}
	// we must close the file descriptor in the thread otherwise the
	// file will not actually be renamed in this thread
	f->close1_r();
	// sync to disk in case power goes out
	sync();
	// . this might be safe to call in a thread
	// . but we do it right after the thread exits now
	//THIS->m_files[i]->set ( THIS->m_newBaseFilename );
	return NULL;
}

void *unlinkWrapper_r ( void *state , ThreadEntry *t ) {
	// get ourselves
	File *f = (File *)state;
	// . by getting the inode in the cache space the call to delete(f) 
	//   below should not block
	// . fd is < 0 if invalid, >= 0 if valid
	//int fd = f->getfdNoOpen ();
	// hey, it still blocks
	//if ( fd >= 0 ) fsync ( fd );
	// and unlink it
	::unlink ( f->getFilename() );
	// we must close the file descriptor in the thread otherwise the
	// file will not actually be unlinked in this thread
	f->close1_r();
	// sync to disk in case power goes out
	sync();
	return NULL;
}

void doneRenameWrapper ( void *state , ThreadEntry *t ) {
	// extract our class
	File *f = (File *)state;
	// . finish the close
	// . for some reason renaming invalidates our fd so if someone wants
	//   to read from us they'll have to re-open
	// . this may bitch about a bad file descriptor since we call
	//   ::close1_r(fd) in the thread
	f->close2();
	// get the big guy and the i in m_files[i]
	BigFile *THIS = (BigFile *)f->m_this;
	// clear thread's errno
	errno = 0;
	// one less
	THIS->m_numThreads--;
	g_unlinkRenameThreads--;
	// reset g_errno and return true if file does not exist
	//if ( g_errno == ENOENT ) g_errno = 0 ;
	// otherwise, it's a more serious error i guess
	if ( g_errno ) log ( "disk: rename: %s: %s", 
			     THIS->getFilename(),mstrerror(g_errno));
	// get the ith file we just unlinked
	long      i = f->m_i;
	// rename the part if it checks out
	if ( f == THIS->m_files[i] ) {
		// set his new name
		char newFilename [ 1024 ];
		THIS->makeFilename_r  ( THIS->m_newBaseFilename,
					THIS->m_newBaseFilenameDir,
					i,
					newFilename);
		THIS->m_files[i]->set ( newFilename );
	}
	// otherwise bitch about it
	else log(LOG_LOGIC,"disk: Rename had bad file ptr.");
	// bail if more to do
	//if ( THIS->m_numThreads > 0 ) return;
	// one less part to do
	THIS->m_partsRemaining--;
	// return if more to do
	if ( THIS->m_partsRemaining > 0 ) return;
	// update OUR base filename now after all Files are renamed
	strcpy ( THIS->m_baseFilename , THIS->m_newBaseFilename );
	// . all done, call the main callback
	// . this is NULL if we were not called in a thread
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
}

void doneUnlinkWrapper ( void *state , ThreadEntry *t ) {
	// extract our class
	File *f = (File *)state;
	// finish the close
	f->close2();
	// get the big guy and the i in m_files[i]
	BigFile *THIS = (BigFile *)f->m_this;
	// clear thread's errno
	errno = 0;
	// one less
	THIS->m_numThreads--;
	g_unlinkRenameThreads--;
	// otherwise, it's a more serious error i guess
	if ( g_errno ) log("disk: unlink: %s", mstrerror(g_errno));
	// get the ith file we just unlinked
	long      i = f->m_i;
	// . remove the part if it checks out
	// . this will also close the file when it deletes it
	if ( f == THIS->m_files[i] ) THIS->removePart ( i );
	// otherwise bitch about it
	else log(LOG_LOGIC,"disk: Unlink had bad file ptr.");
	// bail if more to do
	if ( THIS->m_numThreads > 0 ) return;
	// return if more to do
	//if ( THIS->m_partsRemaining > 0 ) return;
	// . all done, call the main callback
	// . this is NULL if we were not called in a thread
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
}

void BigFile::removePart ( long i ) {

	File *f = m_files[i];
	// . thread should have stored the filename for unlinking
	// . now delete it from memory
	mdelete ( f , sizeof(File) , "BigFile" );
	delete (f);
	// and clear from our table
	m_files[i] = NULL;
	// we have one less part
	m_numParts--;
	// max part num may be different
	if ( m_maxParts != i+1 ) return;
	// set m_maxParts
	long j;
	for ( j = i ; j >= 0 ; j-- ) 
		if ( m_files[j] ) { m_maxParts = j+1; break; }
	// may have no more part files left which means no max part num
	if ( j < 0 ) m_maxParts = 0;
}

// used by RdbMap after reading in during start up, we don't want to waste
// all the fds, but we can't call BigFile::close() because then RdbMap::unlink
// doesn't work.
bool BigFile::closeFds ( ) {
	for ( long i = 0 ; i < m_maxParts ; i++ ) {
		if ( ! m_files[i] ) continue;
		m_files[i]->close();
	}
	return true;
}

bool BigFile::close ( ) {
	// do not double call this
	if ( m_isClosing ) return true;
	// this end up being called again through a sequence of like 20
	// subroutines, so put a stop to that circle
	m_isClosing = true;
	for ( long i = 0 ; i < m_maxParts ; i++ ) {
		if ( ! m_files[i] ) continue;
		m_files[i]->close();
		mdelete ( m_files[i] , sizeof(File) , "BigFile" );
		delete (m_files[i]);
		m_files[i]   = NULL;
	}
	m_numParts   = 0;
	m_maxParts   = 0;
	// save vfd and pc because removeThreads() actually ends up calling 
	// the done wrapper, sending back an error reply, shutting down the 
	// udp server, calling main.cpp::resetAll(), which resets the Rdb and
	// free this big file
	DiskPageCache *pc  = m_pc;
	long           vfd = m_vfd;

	// remove all queued threads that point to us that have not
	// yet been launched
	g_threads.m_threadQueues[DISK_THREAD].removeThreads(this);
	// release our pages from the DiskPageCache
	//if ( m_pc ) m_pc->rmVfd ( m_vfd );
	if ( pc ) pc->rmVfd ( vfd );
	return true;
}


ssize_t gbpwrite(int fd, const void *buf, size_t count, off_t offset) {
	return pwrite ( fd , buf , count , offset );
}
