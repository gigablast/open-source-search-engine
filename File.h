// Matt Wells, Copyright May 2001

// . TODO: don't closes block us? if we have many fd's our closes might block!!
// . TODO: must we create a separate fd for each non-blocking read even if
//         on the same file?????? that would save us...

// . this class simulates having 1K file descriptors.
// . by using it's open/write/read/close it will make it seem like you have 5K file descriptors
// . minimizes the # of open/closes it does.

// On my solaris ultra 1 i could do 28,000 open/close pairs per second.
// my 400mhz pentium linux box was 2.5 times faster! it only had 256 file
// descriptors to work with, while the sun box had 1024.

// the sockets must share with these so we'd like to set a maximum for each.

#ifndef _FILE_H_
#define _FILE_H_

#define MAX_FILENAME_LEN 128

// . max # of VIRTUAL file descriptors
// . man, chris has 958 files, lets crank it up from 2k to 5k
// . boost up to 50,000 since we are hitting this limit with crawlbot
// . we are hitting again with crawlbot, boost to 200k from 50k
// . TODO: make this dynamically allocate based on need
//#define MAX_NUM_VFDS (1024*1024)

#include <sys/types.h>       // for open/lseek
#include <sys/stat.h>        // for open
#include <fcntl.h>           // for open
#include <sys/stat.h>        // for stat
#include "Mem.h"             // for g_mem
#include "Loop.h"            // for g_loop.setNonBlocking(int fd)
#include "SafeBuf.h"

bool doesFileExist ( char *filename ) ;

int64_t getFileSize ( char *filename ) ;

int64_t getFileSize_cygwin ( char *filename ) ;

// for avoiding unlink/opens that mess up our threaded read
int32_t getCloseCount_r ( int fd );

// prevent fd from being closed on us when we are writing
void enterWriteMode ( int fd ) ;
void exitWriteMode  ( int fd ) ;
// error correction routine used by BigFile.cpp
//void releaseVfd     ( int32_t vfd ) ;
//int  getfdFromVfd   ( int32_t vfd ) ;

class File {

	friend class BigFile;

 public:

	// along the same lines as getCloseCount_r()
	//void incCloseCount_r ( ) ;

	 File ( );
	~File ( );

	void constructor();
	void destructor ();

	// . if you don't need to do a full open then just set the filename
	// . useful for unlink/rename/reserve/...
	// . IMPORTANT: if bytes were already reserved can only increase the 
	//   reserve, not decrease
	void set ( char *dir , char *filename );
	void set ( char *filename );

	// returns false and sets errno on error, returns true on success
	bool rename ( char *newFilename );

	bool calledOpen () { return m_calledOpen; };
	bool calledSet  () { return m_calledSet; };

	bool isNonBlocking () ;

	// . get the file extension of this file
	// . return NULL if none
	char *getExtension ( ) ;
	
	// uses lseek to get file's current position
	int32_t getCurrentPos ( ) ;

	// . open() returns true on success, false on failure, errno is set.
	// . opens for reading/writing only
	// . returns false if does not exist
	bool open  ( int flags , int permissions = 
		     S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );

	// . use an offset of -1 to use current file seek position
	// . returns what ::read returns
	// . returns -1 on lseek failure (if offset is valid)
	// . returns 0 on EOF
	// . returns numBytesRead if not error
	// . a negative offset means current read offset
	int  read    ( void *buf , int32_t size , int32_t offset );

	// . use an offset of -1 to use current file seek position
	// . returns what ::write returns
	// . returns -1 on lseek failure (if offset is valid)
	// . returns numBytesWritten if not error
	// . this is non-blocking so may return < "numBytesToWrite"
	// . a negative offset means current write offset
	int  write   ( void *buf , int32_t size , int32_t offset );

	// . this will really close this file
	bool close   ( );  

	// . flush the output
	bool flush   ( );

	// used by threaded unlinks and renames by BigFile.cpp
	bool m_closedIt;
	void close1_r ();
	void close2   ();

	// . returns -1 on error
	// . otherwise returns file size in bytes
	// . returns 0 if does not exist
	int64_t getFileSize ( );

	// . when was it last touched?
	time_t getLastModifiedTime ( );

	// . returns -1 on error and sets errno
	// . returns  0 if does not exist
	// . returns  1 if it exists
	// . a simple stat check
	int32_t doesExist ( );

	// . static so you don't need an instant of this class to call it
	// . returns false and sets errno on error
	bool unlink ( );

	// . file position seeking -- just a wrapper for lseek
	// . returns -1 on error
	// . used by reserve/write/read/getFileSize()
	int32_t lseek ( int32_t offset , int whence = SEEK_SET );

	// . interface so BigFile and others can access the static member info
	//char *getName        ( ) ;
	//int   getMode        ( ) ;
	//int   getPermissions ( ) ;

	// . will try to REopen the file to get the fd if necessary
	// . used by BigFile
	// . returns -2 if we've never been officially opened
	// . returns -1 on error getting the fd or opening this file
	// . must call open() before calling this
	int   getfd          ( ) ;

	// return -1 if not opened, otherwise, return the opened fd
	int   getfdNoOpen ( ) ;

	//char *getFilename ( ) { return m_filename.getBufStart(); };
	char *getFilename ( ) { return m_filename; };

	// our filename allocated with strdup
	// we publicize for ease of use
	char m_filename [ MAX_FILENAME_LEN ];
	//SafeBuf m_filename;

	//char m_filenameBuf [ MAX_FILENAME_LEN ];

	// File::rename() uses this
	//char m_oldFilename [ MAX_FILENAME_LEN ];

	// BigFile uses these when passing us to a thread for unlink/rename
	// so it can store its THIS ptr and the i in BigFile::m_files[i]
	void *m_this;
	int32_t  m_i;

	int32_t m_closeCount;

	// private: 

	// initializes the fd pool
	bool initialize ();

	// free the least-used file.
	bool closeLeastUsed ( );

	// THIS file's VIRTUAL descriptor
	//int m_vfd;

	// now just the real fd. is -1 if not opened
	int m_fd;


	// save the permission and flag sets in case of re-opening
	int m_flags;
	//int m_permissions;
	
	char m_calledOpen;
	char m_calledSet;

	time_t m_st_mtime;  // file last mod date
	int32_t   m_st_size;   // file size
	time_t getLastModifiedDate ( ) ;

	//class File *m_nextActive;
	//class File *m_prevActive;
};


#endif
