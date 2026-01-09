#ifndef _DIR_H
#define _DIR_H

#include <sys/types.h>      // for opendir
#include <dirent.h>         // for opendir
#include "File.h" // for File::getFileSize()

class Dir {

 public:

	bool set      ( char *dirName );
	bool set      ( char *d1 , char *d2 );

	void reset    ( );

	bool open     ( );

	bool close    ( );

	void rewind   ( ); // rewind to get the first filename

	bool cleanOut ( ); // remove all files/dir in directory

	bool create   ( ); // create the directory

	char *getNextFilename ( char *pattern = NULL );

	// . calls getNextFilename and returns number of files matching the 
	//   pattern
	int getNumFiles             ( char *pattern = NULL );

	// . does not yet support recursion
	int64_t   getUsedSpace    ( );

	char *getNewFilename ( char *pattern  ) ;
	int64_t   getNewId       ( char *pattern  ) ;
	int64_t   getFileId      ( char *filename ) ;

	char *getDir     ( ) { return m_dirname; };
	char *getDirName ( ) { return m_dirname; };
	char *getDirname ( ) { return m_dirname; };
	char *getFullName ( char *filename ); // prepends path

	 Dir     ( );
	~Dir     ( );

 private:

	char          *m_dirname;
	DIR           *m_dir;
	bool m_needsClose;
};

#endif
