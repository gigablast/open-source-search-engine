// Copyright Sep 2000 Matt Wells

// . this class makes the 2gb file limit transparent
// . you can actually set you own file size limit to whatever you want as 
//   int32_t as it's greater than zero
// . this is usually 2gb
// . TODO: fix the O_SYNC option
// . TODO: provide ability to pass flags

#ifndef _BIGFILE_H_
#define _BIGFILE_H_

#include "File.h"

ssize_t gbpwrite(int fd, const void *buf, size_t count, off_t offset);

// . use 512 disk megs per part file
// . at a GB_PAGE_SIZE of 16k this is 32k RdbMap pages (512k) per part file
// . let's use 2GB now to conserve file descriptors
//#define MAX_PART_SIZE  (2000LL*1000LL*1000LL-1414144LL)
#define MAX_PART_SIZE  (1920LL*1024LL*1024LL)
// debug define
//#define MAX_PART_SIZE  (32LL*1024LL*1024LL)

// have enough part files to do a 2048gig file
//#define MAX_PART_FILES (((2048LL*1000LL*1000LL*1000LL)/MAX_PART_SIZE)+1LL)

// HACK to save mem. support a 128GB file
//#define MAX_PART_FILES (((128LL*1000LL*1000LL*1000LL)/MAX_PART_SIZE)+1LL)

// debug define
//#define MAX_PART_FILES 100

#define LITTLEBUFSIZE 210

// use this state class for doing non-blocking reads/writes
#ifdef ASYNCIO
#include <aio.h> // TODO: use kaio, uses only 4 threads
#endif

class FileState {
public:
	// this is where we go after the thread has exited
	//void          (*m_threadDone) ( void *state ) ;
	// callback must be top 4 bytes of the state class we give to g_loop
	// callback must be first X bytes
	class BigFile  *m_this;
	//struct aiocb   m_aiostate;
	char           *m_buf;
	int64_t            m_bytesToGo;
	int64_t       m_offset;
	// . the original offset, because we set m_offset to m_currentOffset
	//   if the original offset specified is -1
	// . we also advance BigFile::m_currentOffset when done w/ read/write
	//int64_t       m_origOffset;
	bool            m_doWrite;
	int64_t            m_bytesDone;
	void           *m_state ;
	void          (*m_callback) ( void *state ) ;
	// goes from 0 to 1, the lower the niceness, the higher the priority
	int32_t            m_niceness;
	// was it found in the disk page cache?
	char m_inPageCache;
	// . if signal is still pending we need to know if BigFile got deleted
	// . m_files must be NULL terminated
	//class BigFile **m_files;
	// . we get our fds before starting the read thread to avoid
	//   problems with accessing m_files since RdbMerge may call unlinkPart
	//   from the main thread while we're trying to get these things
	// . no read should span more than 2 file descriptors
	int32_t            m_filenum1;
	int32_t            m_filenum2;
	int             m_fd1 ;
	int             m_fd2 ;
	// hold the errno from the threaded read/write here
	int32_t            m_errno;
	// just for flagging unlinked/renamed thread ops
	int32_t            m_errno2;
	// when we started for graphing purposes (in milliseconds)
	int64_t       m_startTime;
	int64_t       m_doneTime;
	char m_usePartFiles;
	// this is used for calling DiskPageCache::addPages() when done 
	// with the read/write
	//class DiskPageCache *m_pc;
	// this is just used for accessing the DiskPageCache, m_pc, it is
	// a "virtual fd" for this whole file
	int64_t            m_vfd;
	// test parms
	//int32_t  m_osize;
	//char *m_obuf;
	// for avoiding unlink/reopens while doing a threaded read
	int32_t m_closeCount1 ;
	int32_t m_closeCount2 ;
	//int32_t m_vfd1;
	//int32_t m_vfd2;

	//char m_baseFilename[32];
	int32_t m_flags;	
	// when we are given a NULL buffer to read into we must allocate
	// it either in DiskPageCache.cpp or in Threads.cpp right before the
	// thread is launched. this will stop us from having 19000 unlaunched
	// threads each hogging up 32KB of memory waiting to read tfndb.
	// m_allocBuf points to what we allocated.
	char *m_allocBuf;
	int64_t  m_allocSize;
	// m_allocOff is offset into m_allocBuf where we start reading into 
	// from the file
	int64_t  m_allocOff;
	// do not call pthread_create() for every read we do. use async io
	// because it should be much much faster
#ifdef ASYNCIO
	struct aiocb m_aiocb[2];
#endif
};


class BigFile {

 public:

	~BigFile();
	BigFile();

	// . set a big file's name
	// . we split little files that make up this BigFile between
	//   "dir" and "stripeDir"
	bool set ( char *dir , char *baseFilename , char *stripeDir = NULL );

	// self explanatory
	bool doesExist ( ) ;

	// does file part #n exist?
	bool doesPartExist ( int32_t n ) ;

	// . does not actually open any part file we have
	// . waits for a read/write operation before doing that
	// . if you set maxFileSize to -1 we set it to BigFile::getFileSize()
	// . if you are opening a new file for writing, you need to provide it
	//   if you pass in a DiskPageCache ptr
	bool open  ( int flags , 
		     //class DiskPageCache *pc = NULL ,
		     void *pc = NULL ,
		     int64_t maxFileSize = -1 ,
		     int permissions    = 
		     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
	//bool usePartFiles = true );

	// this will set usepartfiles to false! so use this to open large
	// warc or arc files
	//bool open2  ( int flags , 
	//	     //class DiskPageCache *pc = NULL ,
	//	     void *pc = NULL ,
	//	     int64_t maxFileSize = -1 ,
	//	     int permissions    = 
	//	      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );

	

	int getFlags() { return m_flags; };

	void setBlocking    ( ) { m_flags &= ~((int32_t)O_NONBLOCK); };
	void setNonBlocking ( ) { m_flags |=         O_NONBLOCK ; };

	// . return -2 on error
	// . return -1 if does not exist
	// . otherwise return the big file's complete file size (can b >2gb)
	int64_t getFileSize ( );
	int64_t getSize     ( ) { return getFileSize(); };

	// use the base filename as our filename
	char *getFilename() { return m_baseFilename.getBufStart(); };

	char *getDir() { return m_dir.getBufStart(); };

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . otherwise, returns 1 if the read was completed
	// . decides what 2gb part file(s) we should read from
	bool read  ( void       *buf    , 
		     int32_t        size   , 
		     int64_t   offset                         , 
		     FileState  *fs                      = NULL , 
		     void       *state                   = NULL , 
		     void      (* callback)(void *state) = NULL ,
		     int32_t        niceness                = 1    ,
		     bool        allowPageCache          = true ,
		     bool        hitDisk                 = true ,
		     int32_t        allocOff                = 0    );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . IMPORTANT: if returns -1 it MAY have written some bytes 
	//   successfully to OTHER parts that's why caller should be 
	//   responsible for maintaining current write offset
	bool  write ( void       *buf    , 
		      int32_t        size   , 
		      int64_t   offset                         , 
		      FileState  *fs                      = NULL , 
		      void       *state                   = NULL , 
		      void      (* callback)(void *state) = NULL ,
		      int32_t       niceness                 = 1    ,
		      bool       allowPageCache           = true );

	// unlinks all part files
	bool unlink ( );

	// . renames ALL parts too
	// . doesn't change directory, just the base filename
	// . use m_dir if newBaseFilenameDir is NULL
	bool rename ( char *newBaseFilename , char *newBaseFilenameDir=NULL) ;

	bool move ( char *newDir );

	// . returns false and sets g_errno on failure
	// . chop only parts LESS THAN "part"
	bool chopHead ( int32_t part );

	// . these here all use threads and call your callback when done
	// . they return false if blocked, true otherwise
	// . they set g_errno on error
	bool unlink   ( void (* callback) ( void *state ) , 
		        void *state ) ;
	bool rename   ( char *newBaseFilename ,
		        void (* callback) ( void *state ) , 
		        void *state ) ;
	bool chopHead ( int32_t part , 
			void (* callback) ( void *state ) , 
			void *state ) ;

	// closes all part files
	bool close ();

	// just close all the fds of the part files, used by RdbMap.cpp.
	bool closeFds ( ) ;

	//int getfdByOffset ( int64_t offset );

	// what part (little File) of this BigFile has offset "offset"?
	int getPartNum ( int64_t offset ) { return offset / MAX_PART_SIZE; };

	// . opens the nth file if necessary to get it's fd
	// . returns -1 if none, >=0 on success
	int getfd ( int32_t n , bool forReading );//, int32_t *vfd = NULL );

	// public for wrapper to call
	//bool readwrite_r ( FileState *fstate );

	//int64_t m_currentOffset;

	//DiskPageCache *getDiskPageCache ( ) { return m_pc;  };
	int32_t       getVfd       ( ) { return m_vfd; };

	// WARNING: some may have been unlinked from call to chopHead()
	int32_t getNumParts ( ) { return m_numParts; };

	// makes the filename of part file #n
	void makeFilename_r ( char *baseFilename    , 
			      char *baseFilenameDir ,
			      int32_t  n               , 
			      char *buf             ,
			      int32_t maxBufSize );

	void removePart ( int32_t i ) ;

	// don't launch a threaded rename/unlink if one already in progress
	// since we only have one callback, m_callback
	int32_t m_numThreads;

	void (*m_callback)(void *state);
	void  *m_state;
	// is the threaded op an unlink? (or rename?)
	bool   m_isUnlink;
	int32_t   m_part; // part # to unlink (-1 for all)

	// number of parts remaining to be unlinked/renamed
	int32_t   m_partsRemaining;

	char m_tinyBuf[8];

	// to hold the array of Files
	SafeBuf m_filePtrsBuf;

	// enough mem for our first File so we can avoid a malloc
	char m_littleBuf[LITTLEBUFSIZE];

	// ptrs to the part files
	//File *m_files ;//[ MAX_PART_FILES ];

	// private: 

	// . wrapper for all reads and writes
	// . if doWrite is true then we'll do a write, otherwise we do a read
	// . returns false and sets errno on error, true on success
	bool readwrite ( void       *buf, 
			 int32_t        size, 
			 int64_t   offset, 
			 bool        doWrite,
			 FileState  *fstate   ,
			 void       *state    ,
			 void      (* callback) ( void *state ) ,
			 int32_t        niceness ,
			 bool        allowPageCache ,
			 bool        hitDisk        ,
			 int32_t        allocOff       );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool unlinkRename ( char *newBaseFilename             ,
			    int32_t  part                        ,
			    bool  useThread                   ,
			    void (* callback) ( void *state ) ,
			    void *state                       ,
			    char *newBaseFilenameDir = NULL   ) ;

	// . add all parts from this directory
	// . called by set() above for normal dir as well as stripe dir
	bool addParts ( char *dirname ) ;

	bool addPart ( int32_t n ) ;

	//bool unlinkPart ( int32_t n , bool block );

	File *getFile2 ( int32_t n ) { 
		if ( n >= m_maxParts ) return NULL;
		File **filePtrs = (File **)m_filePtrsBuf.getBufStart();
		File *f = filePtrs[n];
		//if ( ! f ->calledSet() ) return NULL;
		// this will be NULL if addPart(n) never called
		return f;
	};

	// if part file not created, will create it
	//File *getPartFile2 ( int32_t n ) { return getFile2(n); }

	// . put a signal on the queue to do reading/writing
	// . we call readwrite ( FileState *) when we handle the signal
	void addSig ( FileState *fstate ) ;

	bool reset ( );

	// for basefilename to avoid an alloc
	char m_tmpBaseBuf[32];

	// our most important the directory and filename
	SafeBuf m_dir      ;//    [256];
	SafeBuf m_baseFilename ;//[256];

	// rename stores the new name here so we can rename the m_files[i] 
	// after the rename has completed and the rename thread returns
	SafeBuf m_newBaseFilename ;//   [256];
	// if first char in this dir is 0 then use m_dir
	SafeBuf m_newBaseFilenameDir ;//[256];


	//int32_t m_permissions;
	int32_t m_flags;

	// determined in open() override
	int       m_numParts;
	// maximum part #
	int32_t      m_maxParts;

	//class DiskPageCache *m_pc;
	int32_t             m_vfd;
	//bool             m_vfdAllowed;

	// prevent circular calls to BigFile::close() with this
	char m_isClosing;

	char m_usePartFiles;

	int64_t m_fileSize;

	// oldest of the last modified dates of all the part files
	time_t m_lastModified;
	time_t getLastModifiedTime();
};

extern int32_t g_unlinkRenameThreads;

extern int64_t g_lastDiskReadStarted;
extern int64_t g_lastDiskReadCompleted;
extern bool      g_diskIsStuck;

extern void *readwriteWrapper_r ( void *state , class ThreadEntry *t ) ;

#endif
