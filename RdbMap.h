// Copyright Sep 2000 Matt Wells

// . an array of key/pageOffset pairs indexed by disk page number 
// . slots in Slot (on disk) must be sorted by key from smallest to largest
// . used for quick btree lookup of a key to find the page where that slot 
//   resides on disk
// . disk page size is system's disk page size (8k) for best performance
// . TODO: split up map into several arrays so like the first 1meg of
//   map can be easily freed like during a merge 
// . TODO: use a getKey(),getOffset(),getDataSize() to make this easier to do

#ifndef _RDBMAP_H_
#define _RDBMAP_H_

#include "BigFile.h"
#include "RdbList.h"

// . this can be increased to provide greater disk coverage but it will 
//   increase delays because each seek will have to read more
// . 8k of disk corresponds to 18 bytes of map
// . 8megs of disk needs 18k of map (w/  dataSize)
// . 8megs of disk needs 14k of map (w/o dataSize)
// . a 32gig index would need 14megs*4 = 56 megs of map!!!
// . then merging would mean we'd need twice that, 112 megs of map in memory
//   unless we dumped the map to disk periodically
// . 256 megs per machine would be excellent, but costly?
// . we need 66 megabytes of mem for every 80-gigs of index (actually 40gigs
//   considering half the space is for merging)

// . PAGE_SIZE is often called the block size
// . a page or block is read in "IDE Block Mode" by the drive
// . it's the amount of disk that can be read with one i/o (interrupt)
// . try to make sure PAGE_SIZE matches your "multiple sector count"
// . use hdparm to configure (hdparm -m16 /dev/hda) will set it to 8k since
//   each sector is 512bytes
// . hdparm -u1 -X66 -d1 -c3 -m16 /dev/hda   is pretty agressive
// . actually "block size" in context of the file system can be 1024,... 4096
//   on ext2fs ... set it as high as possible since we have very large files
//   and want to avoid external fragmentation for the fastest reading/writing
// . we now set it to 16k to make map smaller in memory
// . NOTE: 80gigs of disk is 80,000,000,000 bytes NOT 80*1024*1024*1024
// . mapping 80 gigs should now take 80G/(16k) = 4.8 million pages
// . 4.8 million pages at 16 bytes a page is 74.5 megs of memory
// . mapping 320 gigs, at  8k pages is 686 megabytes of RAM (w/ crc)
// . mapping 320 gigs, at 16k pages is half that
// . mapping 640 gigs, at 16k pages is 686 megabytes of RAM (w/ crc)
// . mapping 640 gigs, at 32k pages is 343 megabytes of RAM (w/ crc)
#define GB_INDEXDB_PAGE_SIZE (32*1024)
#define GB_TFNDB_PAGE_SIZE   ( 1*1024)
//#define PAGE_SIZE (16*1024)
//#define PAGE_SIZE (8*1024)

// . I define a segment to be a group of pages
// . I use 2k pages per segment
// . each page represents m_pageSize bytes on disk
// . the BigFile's MAX_PART_SIZE should be evenly divisible by PAGES_PER_SEG
// . that way, when a part file is removed we can remove an even amount of
//   segments (we chop the leading Files of a BigFile during merges)
#define PAGES_PER_SEGMENT (2*1024)
#define PAGES_PER_SEG     (PAGES_PER_SEGMENT)
// MAX_SEGMENTS of 16*1024 allows for 32 million pages = 256gigs of disk data
//#define MAX_SEGMENTS      (16*1024)  

class RdbMap {

 public:

	 RdbMap  ();
	~RdbMap ();

	// . does not write data to disk
	// . frees all
	void reset ( );

	// set the filename, and if it's fixed data size or not
	void set ( char *dir , char *mapFilename, 
		   //int32_t fixedDataSize , bool useHalfKeys );
		   int32_t fixedDataSize , bool useHalfKeys , char keySize ,
		   int32_t pageSize );

	bool rename ( char *newMapFilename ) {
		return m_file.rename ( newMapFilename ); };

	bool rename ( char *newMapFilename ,
		      void (* callback)(void *state) , void *state ) { 
		return m_file.rename ( newMapFilename , callback , state ); };

	char *getFilename ( ) { return m_file.getFilename(); };

	BigFile *getFile  ( ) { return &m_file; };

	// . writes the map to disk if any slot was added
	// . returns false if File::close() returns false
	// . should free up all mem
	// . resets m_numPages and m_maxNumPages to 0
	// . a file's version of the popular reset() function
	// . if it's urgent we do not call mfree()
	bool close  ( bool urgent );

	// . we store the fixed dataSize in the map file
	// . if it's -1 then each record's data is of variable size
	int32_t getFixedDataSize() { return m_fixedDataSize; };

	void reduceMemFootPrint();

	// . this is called automatically when close() is called
	// . however, we may wish to call it externally to ensure no data loss
	// . return false if any write failes
	// . returns true when done dumping m_keys and m_offsets to file
	// . write out the m_keys and m_offsets arrays
	// . this is totally MTUnsafe
	// . don't be calling addRecord with this is dumping
	// . flushes when done
	bool writeMap  ( bool allDone );
	bool writeMap2 ( );
	int64_t writeSegment ( int32_t segment , int64_t offset );

	// . calls addRecord() for each record in the list
	// . returns false and sets errno on error
	// . TODO: implement transactional rollback feature
	bool addList ( RdbList *list );
	bool prealloc ( RdbList *list );

	// . like above but faster
	// . just for adding data-less keys
	// . NOTE: disabled until it works correctly
	//	bool addKey  ( key_t &key );

	// get the number of non-deleted records in the data file we map
	int64_t getNumPositiveRecs  ( ) { return m_numPositiveRecs; };
	// get the number of "delete" records in the data file we map
	int64_t getNumNegativeRecs  ( ) { return m_numNegativeRecs; };
	// total
	int64_t getNumRecs          ( ) { return m_numPositiveRecs +
						    m_numNegativeRecs; };
	// get the size of the file we are mapping
	int64_t getFileSize () { return m_offset; };

	int64_t findNextFullPosdbKeyOffset ( char *buf, int32_t bufSize ) ;

	// . gets total size of all recs in this page range
	// . if subtract is true we subtract the sizes of pages that begin
	//   with a delete key (low bit is clear)
	int64_t getRecSizes ( int32_t startPage , 
			   int32_t endPage   , 
			   bool subtract  );

	// like above, but recSizes is guaranteed to be in [startKey,endKey]
	int64_t getMinRecSizes ( int32_t   sp       , 
			      int32_t   ep       , 
			      //key_t  startKey , 
			      //key_t  endKey   ,
			      char  *startKey ,
			      char  *endKey   ,
			      bool   subtract );

	// like above, but sets an upper bound for recs in [startKey,endKey]
	int64_t getMaxRecSizes ( int32_t   sp       , 
			      int32_t   ep       , 
			      //key_t  startKey , 
			      //key_t  endKey   ,
			      char  *startKey , 
			      char  *endKey   ,
			      bool   subtract );

	// get a key range from a page range
	void getKeyRange  ( int32_t   startPage , int32_t   endPage ,
			    //key_t *minKey    , key_t *maxKey  );
			    char *minKey , char *maxKey );
	// . get a page range from a key range
	// . returns false if no records exist in that key range
	// . maxKey will be sampled under "oldTruncationLimit" so you
	//   can increase the trunc limit w/o messing up Indexdb::getTermFreq()
	//bool getPageRange ( key_t  startKey  , key_t endKey  ,
	bool getPageRange ( char  *startKey  , char *endKey ,
			    int32_t  *startPage , int32_t *endPage ,
			    //key_t *maxKey    ,
			    char  *maxKey ,
			    int64_t oldTruncationLimit = -1 ) ;

	// get the ending page so that [startPage,endPage] has ALL the recs
	// whose keys are in [startKey,endKey] 
	//int32_t getEndPage   ( int32_t startPage , key_t endKey );
	int32_t getEndPage   ( int32_t startPage , char *endKey );

	// like above, but endPage may be smaller as int32_t as we cover at least
	// minRecSizes worth of records in [startKey,endKey]
	//bool getPageRange ( key_t  startKey  , key_t endKey  ,
	//int32_t   minRecSizes ,
	//int32_t  *startPage , int32_t *endPage ) ;
	
	// . offset of first key wholly on page # "page"
	// . return length of the whole mapped file if "page" > m_numPages
	// . use m_offset as the size of the file that we're mapping
	int64_t getAbsoluteOffset     ( int32_t page ) ;
	// . the offset of a page after "page" that is a different key
	// . returns m_offset if page >= m_numPages
	int64_t getNextAbsoluteOffset ( int32_t page ) ;


	//key_t getLastKey ( ) { return m_lastKey; };
	//char *getLastKey ( ) { return m_lastKey; };
	void  getLastKey ( char *key ) { KEYSET(key,m_lastKey,m_ks); };

	// . these functions operate on one page
	// . get the first key wholly on page # "page"
	// . if page >= m_numPages use the lastKey in the file
	//key_t getKey              ( int32_t page ) { 
	void getKey ( int32_t page , char *k ) { 
		if ( page >= m_numPages ) {KEYSET(k,m_lastKey,m_ks);return;}
		//return m_keys[page/PAGES_PER_SEG][page%PAGES_PER_SEG];
	 KEYSET(k,&m_keys[page/PAGES_PER_SEG][(page%PAGES_PER_SEG)*m_ks],m_ks);
	 return;
	}
	//const key_t *getKeyPtr ( int32_t page ) { 
	char *getKeyPtr ( int32_t page ) { 
		//if ( page >= m_numPages ) return &m_lastKey;
		//if ( page >= m_numPages ) return m_lastKey;
		if ( page >= m_numPages ) return m_lastKey;
		return &m_keys[page/PAGES_PER_SEG][(page%PAGES_PER_SEG)*m_ks];
	}
	//	return getKey ( page ); };
	// if page >= m_numPages return 0
	int16_t getOffset           ( int32_t page ) { 
		if ( page >= m_numPages ) {
			log(LOG_LOGIC,"RdbMap::getOffset: bad engineer");
			return 0;
		}
		return m_offsets [page/PAGES_PER_SEG][page%PAGES_PER_SEG]; 
	};
	//void setKey               ( int32_t page , key_t &k ) { 
	void setKey ( int32_t page , char *k ) { 
		//#ifdef GBSANITYCHECK
		if ( page >= m_maxNumPages ) {
			char *xx = NULL; *xx = 0;
			log(LOG_LOGIC,"RdbMap::setKey: bad engineer");return; }
		//#endif
		//m_keys[page/PAGES_PER_SEG][page%PAGES_PER_SEG] = k; };
		KEYSET(&m_keys[page/PAGES_PER_SEG][(page%PAGES_PER_SEG)*m_ks],
		       k,m_ks);
	};

	void setOffset            ( int32_t page , int16_t offset ) {
		m_offsets[page/PAGES_PER_SEG][page%PAGES_PER_SEG] = offset;};

	// . total recSizes = positive + negative rec sizes
	// . used to read all the recs in Msg3 and RdbScan
	//int32_t  getRecSizes         ( int32_t page ) {
	//return getRecSizes         ( page , page + 1 ); };
	
	// . returns true on success
	// . returns false on i/o error.
	// . calls allocMap() to get memory for m_keys/m_offsets
	// . The format of the map on disk is described in Map.h
	// . sets "m_numPages", "m_keys", and "m_offsets".
	// . reads the keys and offsets into buffers allocated during open().
	bool readMap     ( BigFile *dataFile );
	bool readMap2    ( );
	int64_t readSegment ( int32_t segment, int64_t offset, int32_t fileSize);

	// due to disk corruption keys or offsets can be out of order in map
	bool verifyMap   ( BigFile *dataFile );
	bool verifyMap2  ( );

	bool unlink ( ) { return m_file.unlink ( ); };

	bool unlink ( void (* callback)(void *state) , void *state ) { 
		return m_file.unlink ( callback , state ); };

	int32_t getNumPages ( ) { return m_numPages; };

	// . return first page #, "N",  to read to get the record w/ this key
	//   if it exists
	// . if m_keys[N] < startKey then m_keys[N+1] is > startKey
	// . if m_keys[N] > startKey then all keys before m_keys[N] in the file
	//   are strictly less than "startKey" and "startKey" does not exist
	// . if m_keys[N] > startKey then m_keys[N-1] spans multiple pages so 
	//   that the key immediately after it on disk is in fact, m_keys[N]
	//int32_t getPage ( key_t startKey ) ;
	int32_t getPage ( char *startKey ) ;

	// used in Rdb class before calling setMapSize
	//int32_t setMapSizeFromFile ( int32_t fileSize ) ;

	// . call this before calling addList() or addRecord()
	// . returns false if realloc had problems
	// . sets m_maxNumPages to maxNumPages if successfull
	// . used to grow the map, too
	//bool setMapSize ( int32_t maxNumPages );

	bool addSegmentPtr ( int32_t n ) ;

	// called by setMapSize() to increase the # of segments
	bool addSegment (  ) ;

	// . remove and bury (shift over) all segments below the one that 
	//   contains page # "pageNum"
	// . used by RdbMerge when unlinking part files
	// . returns false and sets errno on error
	// . the first "fileSize" bytes of the BigFile was chopped off
	// . we must remove our segments
	bool chopHead (int32_t fileSize );

	// how much mem is being used by this map?
	int64_t getMemAlloced ();

	// . attempts to auto-generate from data file, f
	// . returns false and sets g_errno on error
	bool generateMap ( BigFile *f ) ;

	// . add a slot to the map
	// . returns false if map size would be exceed by adding this slot
	bool addRecord ( char *key, char *rec , int32_t recSize );
	bool addRecord ( key_t &key, char *rec , int32_t recSize ) {
		return addRecord((char *)&key,rec,recSize);};

	bool truncateFile ( BigFile *f ) ;

 private:

	// specialized routine for adding a list to an indexdb map
	bool addIndexList ( class IndexList *list ) ;

	void printMap ();

	// the map file
        BigFile m_file;

	// . we divide the map up into segments now
	// . this facilitates merges so one map can shrink while another grows

	// . these 3 arrays define the map
	// . see explanation at top of this file for map description
	// . IMPORTANT: if growing m_pageSize might need to change m_offsets 
	//   from int16_t to int32_t
	//key_t         *m_keys    [ MAX_SEGMENTS ]; 
	//char          *m_keys    [ MAX_SEGMENTS ]; 
	char          **m_keys;
	int32_t            m_numSegmentPtrs;
	//key96_t      **m_keys96; // set to m_keys
	//key128_t     **m_keys128; // set to m_keys

	//int16_t         *m_offsets [ MAX_SEGMENTS ]; 
	int16_t         **m_offsets;
	int32_t            m_numSegmentOffs;

	bool m_reducedMem;

	// number of valid pages in the map.
	int32_t          m_numPages;     

	// . size of m_keys, m_offsets arrays 
	// . not all slots are used, however
	// . this is sum of all pages in all segments
	int32_t   m_maxNumPages;      
	// each segment holds PAGES_PER_SEGMENT pages of info
	int32_t   m_numSegments;

	// is the rdb file's dataSize fixed?  -1 means it's not.
	int32_t   m_fixedDataSize;    

	// . to keep track of disk offsets of added records
        // . if this is > 0 we know a key was added to map so we should call
        //   writeMap() on close or destroy
	// . NOTE: also used as the file size of the file we're mapping
	int64_t m_offset;

	long m_newPagesPerSegment;

	// we keep global tallies on the number of non-deleted records
	// and deleted records
	int64_t m_numPositiveRecs;
	int64_t m_numNegativeRecs;
	// . the last key in the file itself
	// . getKey(pageNum) returns this when pageNum == m_numPages
	// . used by Msg3::getSmallestEndKey()
	//key_t  m_lastKey;
	char m_lastKey[MAX_KEY_BYTES];

	// when close is called, must we write the map?
	bool   m_needToWrite;

	// when a BigFile gets chopped, keep up a start offset for it
	int64_t m_fileStartOffset;

	// are we mapping a data file that supports 6-byte keys?
	bool m_useHalfKeys;

	char m_ks;

	bool m_generatingMap;

	int32_t m_pageSize;
	int32_t m_pageSizeBits;

	int32_t      m_lastLogTime ;
	int64_t m_badKeys     ;
	bool      m_needVerify  ;

};

#endif
