// Copyright Sep 2000 Matt Wells

// . this class makes the 2gb file limit transparent
// . you can actually set you own file size limit to whatever you want as 
//   long as it's greater than zero
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
#define MAX_PART_FILES (((2048LL*1000LL*1000LL*1000LL)/MAX_PART_SIZE)+1LL)
// debug define
//#define MAX_PART_FILES 100

// use this state class for doing non-blocking reads/writes
#include <aio.h> // TODO: use kaio, uses only 4 threads
class FileState {
public:
	// this is where we go after the thread has exited
	//void          (*m_threadDone) ( void *state ) ;
	// callback must be top 4 bytes of the state class we give to g_loop
	// callback must be first X bytes
	class BigFile  *m_this;
	//struct aiocb   m_aiostate;
	char           *m_buf;
	long            m_bytesToGo;
	long long       m_offset;
	// . the original offset, because we set m_offset to m_currentOffset
	//   if the original offset specified is -1
	// . we also advance BigFile::m_currentOffset when done w/ read/write
	//long long       m_origOffset;
	bool            m_doWrite;
	long            m_bytesDone;
	void           *m_state ;
	void          (*m_callback) ( void *state ) ;
	// goes from 0 to 1, the lower the niceness, the higher the priority
	long            m_niceness;
	// . if signal is still pending we need to know if BigFile got deleted
	// . m_files must be NULL terminated
	//class BigFile **m_files;
	// . we get our fds before starting the read thread to avoid
	//   problems with accessing m_files since RdbMerge may call unlinkPart
	//   from the main thread while we're trying to get these things
	// . no read should span more than 2 file descriptors
	long            m_filenum1;
	long            m_filenum2;
	int             m_fd1 ;
	int             m_fd2 ;
	// hold the errno from the threaded read/write here
	long            m_errno;
	// just for flagging unlinked/renamed thread ops
	long            m_errno2;
	// when we started for graphing purposes (in milliseconds)
	long long       m_startTime;
	long long       m_doneTime;
	// this is used for calling DiskPageCache::addPages() when done 
	// with the read/write
	class DiskPageCache *m_pc;
	// this is just used for accessing the DiskPageCache, m_pc, it is
	// a "virtual fd" for this whole file
	long            m_vfd;
	// test parms
	//long  m_osize;
	//char *m_obuf;
	// for avoiding unlink/reopens while doing a threaded read
	long m_closeCount1 ;
	long m_closeCount2 ;
	long m_vfd1;
	long m_vfd2;

	//char m_baseFilename[32];
	long m_flags;	
	// when we are given a NULL buffer to read into we must allocate
	// it either in DiskPageCache.cpp or in Threads.cpp right before the
	// thread is launched. this will stop us from having 19000 unlaunched
	// threads each hogging up 32KB of memory waiting to read tfndb.
	// m_allocBuf points to what we allocated.
	char *m_allocBuf;
	long  m_allocSize;
	// m_allocOff is offset into m_allocBuf where we start reading into 
	// from the file
	long  m_allocOff;
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
	bool doesPartExist ( long n ) ;

	// . does not actually open any part file we have
	// . waits for a read/write operation before doing that
	// . if you set maxFileSize to -1 we set it to BigFile::getFileSize()
	// . if you are opening a new file for writing, you need to provide it
	//   if you pass in a DiskPageCache ptr
	bool open  ( int flags , 
		     class DiskPageCache *pc = NULL ,
		     long long maxFileSize = -1 ,
		     int permissions    = 
		     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );

	int getFlags() { return m_flags; };

	void setBlocking    ( ) { m_flags &= ~((long)O_NONBLOCK); };
	void setNonBlocking ( ) { m_flags |=         O_NONBLOCK ; };

	// . return -2 on error
	// . return -1 if does not exist
	// . otherwise return the big file's complete file size (can b >2gb)
	long long getFileSize ( );
	long long getSize     ( ) { return getFileSize(); };

	// use the base filename as our filename
	char *getFilename() { return m_baseFilename; };

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . otherwise, returns 1 if the read was completed
	// . decides what 2gb part file(s) we should read from
	bool read  ( void       *buf    , 
		     long        size   , 
		     long long   offset                         , 
		     FileState  *fs                      = NULL , 
		     void       *state                   = NULL , 
		     void      (* callback)(void *state) = NULL ,
		     long        niceness                = 1    ,
		     bool        allowPageCache          = true ,
		     bool        hitDisk                 = true ,
		     long        allocOff                = 0    );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	// . IMPORTANT: if returns -1 it MAY have written some bytes 
	//   successfully to OTHER parts that's why caller should be 
	//   responsible for maintaining current write offset
	bool  write ( void       *buf    , 
		      long        size   , 
		      long long   offset                         , 
		      FileState  *fs                      = NULL , 
		      void       *state                   = NULL , 
		      void      (* callback)(void *state) = NULL ,
		      long       niceness                 = 1    ,
		      bool       allowPageCache           = true );

	// unlinks all part files
	bool unlink ( );

	// . renames ALL parts too
	// . doesn't change directory, just the base filename
	// . use m_dir if newBaseFilenameDir is NULL
	bool rename ( char *newBaseFilename , char *newBaseFilenameDir=NULL) ;

	// . returns false and sets g_errno on failure
	// . chop only parts LESS THAN "part"
	bool chopHead ( long part );

	// . these here all use threads and call your callback when done
	// . they return false if blocked, true otherwise
	// . they set g_errno on error
	bool unlink   ( void (* callback) ( void *state ) , 
		        void *state ) ;
	bool rename   ( char *newBaseFilename ,
		        void (* callback) ( void *state ) , 
		        void *state ) ;
	bool chopHead ( long part , 
			void (* callback) ( void *state ) , 
			void *state ) ;

	// closes all part files
	bool close ();

	// just close all the fds of the part files, used by RdbMap.cpp.
	bool closeFds ( ) ;

	//int getfdByOffset ( long long offset );

	// what part (little File) of this BigFile has offset "offset"?
	int getPartNum ( long long offset ) { return offset / MAX_PART_SIZE; };

	// . opens the nth file if necessary to get it's fd
	// . returns -1 if none, >=0 on success
	int getfd ( long n , bool forReading , long *vfd = NULL );

	// public for wrapper to call
	//bool readwrite_r ( FileState *fstate );

	long long m_currentOffset;

	DiskPageCache *getDiskPageCache ( ) { return m_pc;  };
	long       getVfd       ( ) { return m_vfd; };

	// WARNING: some may have been unlinked from call to chopHead()
	long getNumParts ( ) { return m_numParts; };

	File *getFile ( long n ) { return m_files[n]; };

	// makes the filename of part file #n
	void makeFilename_r ( char *baseFilename    , 
			      char *baseFilenameDir ,
			      long  n               , 
			      char *buf             );

	void removePart ( long i ) ;

	// don't launch a threaded rename/unlink if one already in progress
	// since we only have one callback, m_callback
	long m_numThreads;

	void (*m_callback)(void *state);
	void  *m_state;
	// is the threaded op an unlink? (or rename?)
	bool   m_isUnlink;
	long   m_part; // part # to unlink (-1 for all)

	// number of parts remaining to be unlinked/renamed
	long   m_partsRemaining;

	// rename stores the new name here so we can rename the m_files[i] 
	// after the rename has completed and the rename thread returns
	char m_newBaseFilename    [256];
	// if first char in this dir is 0 then use m_dir
	char m_newBaseFilenameDir [256];

	// store our base filename here
	char m_baseFilename [256];

	// ptrs to the part files
	File *m_files [ MAX_PART_FILES ];

	// private: 

	// . wrapper for all reads and writes
	// . if doWrite is true then we'll do a write, otherwise we do a read
	// . returns false and sets errno on error, true on success
	bool readwrite ( void       *buf, 
			 long        size, 
			 long long   offset, 
			 bool        doWrite,
			 FileState  *fstate   ,
			 void       *state    ,
			 void      (* callback) ( void *state ) ,
			 long        niceness ,
			 bool        allowPageCache ,
			 bool        hitDisk        ,
			 long        allocOff       );

	// . returns false if blocked, true otherwise
	// . sets g_errno on error
	bool unlinkRename ( char *newBaseFilename             ,
			    long  part                        ,
			    bool  useThread                   ,
			    void (* callback) ( void *state ) ,
			    void *state                       ,
			    char *newBaseFilenameDir = NULL   ) ;

	// . add all parts from this directory
	// . called by set() above for normal dir as well as stripe dir
	bool addParts ( char *dirname ) ;

	bool addPart ( long n ) ;

	//bool unlinkPart ( long n , bool block );

	// if part file not created, will create it
	File *getPartFile ( long n ) { return m_files[n]; };

	// . put a signal on the queue to do reading/writing
	// . we call readwrite ( FileState *) when we handle the signal
	void addSig ( FileState *fstate ) ;

	bool reset ( );

	// store our base filename here
	char m_dir          [256];
	char m_stripeDir    [256];

	long m_permissions;
	long m_flags;

	// determined in open() override
	int       m_numParts;
	// maximum part #
	long      m_maxParts;

	class DiskPageCache *m_pc;
	long             m_vfd;
	bool             m_vfdAllowed;

	// prevent circular calls to BigFile::close() with this
	char m_isClosing;

	long long m_fileSize;

	// oldest of the last modified dates of all the part files
	time_t m_lastModified;
	time_t getLastModifiedTime();
};

extern long g_unlinkRenameThreads;

extern long long g_lastDiskReadStarted;
extern long long g_lastDiskReadCompleted;
extern bool      g_diskIsStuck;

extern void *readwriteWrapper_r ( void *state , class ThreadEntry *t ) ;

#endif
