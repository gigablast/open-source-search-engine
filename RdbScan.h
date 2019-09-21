// Matt Wells, copyright Sep 2000

// . used exculsively by RdbGet
// . used for scanning a key-range of records
// . does non-blocking disk reads
// . set loop to NULL to do blocking disk reads
// . we set m_errno to a positive value on error, so check it!
// . originally you could call getNextRecord() before the entire read
//   was complete. 

#ifndef _RDBSCAN_H_
#define _RDBSCAN_H_

#include "BigFile.h"
#include "Loop.h"
#include "RdbMap.h"
#include "RdbList.h"

class RdbScan {
	
 public:

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . call this to start the read (sets up your key-range read too)
	// . endKey must be provided
	// . TODO: can we do multiple "simultaneous" non-blocking 
	//   reads on the same fd?
	// . we need array of files so BigFile::filecb() knows when file nuked
	bool setRead ( BigFile   *file          ,
		       int32_t       fixedDataSize ,
		       int64_t  offset        ,
		       int32_t       bytesToRead   ,
		       //key_t      startKey      , 
		       //key_t      endKey        , 
		       char      *startKey      , 
		       char      *endKey        , 
		       char       keySize       ,
		       RdbList   *list          , // we fill this up
		       void      *state         ,
		       void    (* callback ) ( void *state ) ,
		       bool       useHalfKeys   ,
		       char       rdbId,
		       int32_t       niceness , // = MAX_NICENESS ,
		       bool       allowPageCache , // = true ,
		       bool       hitDisk        ); // = true );

	// RdbGet likes to get our list
	RdbList *getList ( ) { return m_list; };

	// was buffer shifted down 6 bytes to turn first key into a 12 byter?
	bool wasShifted () { return m_shifted; };


	void gotList ( );

	// we set this list with the read buffer on read completion
	RdbList  *m_list;

	// for doing non-blocking reads with BigFile::read()
	FileState m_fstate;

	// for dealing with half keys
	int32_t m_off;

	// shifting it
	char m_shifted;

	char m_rdbId;

	// save for call to our gotListWrapper()
	//key_t  m_startKey;
	char m_startKey[MAX_KEY_BYTES];
	char m_endKey  [MAX_KEY_BYTES];
	int32_t m_fixedDataSize;
	char m_useHalfKeys;
	int32_t m_bytesToRead;
	void (* m_callback ) ( void *state ) ;
	void  *m_state;

	// for sanity checking
	BigFile   *m_file;
	int64_t  m_offset;
	char       m_ks;

	// for allowing page cache
	bool  m_allowPageCache;

	bool  m_hitDisk;

	// this is set by Msg3.cpp
	char m_inPageCache;
};

#endif
