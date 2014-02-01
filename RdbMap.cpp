#include "gb-include.h"

#include "RdbMap.h"
#include "BigFile.h"
#include "IndexList.h"

RdbMap::RdbMap() {
	m_numSegments = 0;
	m_numSegmentPtrs = 0;
	m_numSegmentOffs = 0;
	reset ( );
}

// dont save map on deletion!
RdbMap::~RdbMap() {
	reset();
}

void RdbMap::set ( char *dir , char *mapFilename, 
		   long fixedDataSize , bool useHalfKeys , char keySize ,
		   long pageSize ) {
	reset();
	m_fixedDataSize = fixedDataSize;
	m_file.set ( dir , mapFilename );
	m_useHalfKeys = useHalfKeys;
	m_ks = keySize;
	m_pageSize = pageSize;
	m_pageSizeBits = getNumBitsOn32(pageSize-1);
	// m_pageSize -1 must be able to be stored in m_offsets[][] (a short)
	if ( m_pageSize > 32768 ) {
	      log(LOG_LOGIC,"db: rdbmap: m_pageSize too big for m_offsets.");
	      char *xx = NULL; *xx = 0;
	}
	// . we remove the head part files of a BigFile when merging it
	// . this keeps the required merge space down to a small amount
	// . when we chop off a part file from a BigFile we must also
	//   chop off the corresponding segments in the map
	// . the match must be EXACT
	// . therefore, PAGES_PER_SEGMENT * m_pageSize must evenly divide 
	//   MAX_PART_SIZE #define'd in BigFile.h
	if ( (MAX_PART_SIZE % (PAGES_PER_SEGMENT*m_pageSize)) == 0 ) return;
	log(LOG_LOGIC,"db: rdbmap: PAGES_PER_SEGMENT*"
	    "m_pageSize does not divide MAX_PART_SIZE.  cannot do "
	    "space-saving merges due to this.");
	char *xx = NULL; *xx = 0;
}

bool RdbMap::close ( bool urgent ) {
	bool status = true;
	if ( /*mdw m_numPages > 0 &&*/ m_needToWrite ) status = writeMap ( );
	// clears and frees everything
	if ( ! urgent ) reset ();
	return status;
}

void RdbMap::reset ( ) {
	m_generatingMap = false;
	for ( long i = 0 ; i < m_numSegments; i++ ) {
		//mfree(m_keys[i],sizeof(key_t)*PAGES_PER_SEGMENT,"RdbMap");
		mfree(m_keys[i],m_ks*PAGES_PER_SEGMENT,"RdbMap");
		mfree(m_offsets[i], 2*PAGES_PER_SEGMENT,"RdbMap");
		// set to NULL so we know if accessed illegally
		m_keys   [i] = NULL;
		m_offsets[i] = NULL;
	}

	// the ptrs themselves are now a dynamic array to save mem
	// when we have thousands of collections
	mfree(m_keys,m_numSegmentPtrs*sizeof(char *),"MapPtrs");
	mfree(m_offsets,m_numSegmentOffs*sizeof(short *),"MapPtrs");
	m_numSegmentPtrs = 0;
	m_numSegmentOffs = 0;

	m_needToWrite     = false;
	m_fileStartOffset = 0LL;
	m_numSegments     = 0;
	m_numPages        = 0;
	m_maxNumPages     = 0;
	m_offset          = 0LL;
	m_numPositiveRecs = 0LL;
	m_numNegativeRecs = 0LL;
	//m_lastKey.n1      = 0;
	//m_lastKey.n0      = 0LL;
	KEYMIN(m_lastKey,MAX_KEY_BYTES); // m_ks);
	// close up shop
	// m_file.close ( ); this casues an error in Rdb.cpp:317 (new RdbMap)
	m_lastLogTime = 0;
	m_badKeys     = 0;
	m_needVerify  = false;
}


bool RdbMap::writeMap ( ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// return true if nothing to write out
	// mdw if ( m_numPages <= 0 ) return true;
	if ( ! m_needToWrite ) return true;
	// open a new file
	if ( ! m_file.open ( O_RDWR | O_CREAT | O_TRUNC ) ) 
		return log("db: Could not open %s for writing: %s.",
			   m_file.getFilename(),mstrerror(g_errno));
	// write map data
	bool status = writeMap2 ( );
	// on success, we don't need to write it anymore
	if ( status ) m_needToWrite = false;
	// . close map
	// . no longer since we use BigFile
	//m_file.close ( );
	// return status
	return status;
}

bool RdbMap::writeMap2 ( ) {
	// the current disk offset
	long long offset = 0LL;
	g_errno = 0;
	// first 8 bytes are the size of the DATA file we're mapping
	m_file.write ( &m_offset , 8 , offset );
	if ( g_errno ) return log("db: Failed to write to %s: %s",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// when a BigFile gets chopped, keep up a start offset for it
	m_file.write ( &m_fileStartOffset , 8 , offset );
	if ( g_errno ) return log("db: Failed to write to %s: %s",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// store total number of non-deleted records
	m_file.write ( &m_numPositiveRecs , 8 , offset );
	if ( g_errno ) return log("db: Failed to write to %s: %s",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// store total number of deleted records
	m_file.write ( &m_numNegativeRecs , 8 , offset );
	if ( g_errno ) return log("db: Failed to write to %s: %s",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// store last key in map
	//m_file.write ( &m_lastKey , 12 , offset );
	m_file.write ( m_lastKey , m_ks , offset );
	if ( g_errno ) return log("db: Failed to write to %s: %s",
				  m_file.getFilename(),mstrerror(g_errno));
	//offset += 12;
	offset += m_ks;
	// . now store the map itself
	// . write the segments (keys/offsets) from the map file
	for ( long i = 0 ; i < m_numSegments ; i++ ) {
		offset = writeSegment ( i , offset );
		if ( offset<=0 ) return log("db: Failed to write to "
					    "%s: %s",
					    m_file.getFilename(),
					    mstrerror(g_errno));
	}
	// . make sure it happens now!
	// . no, we use O_SYNC
	//m_file.flush();
	return true;
}

long long RdbMap::writeSegment ( long seg , long long offset ) {
	// how many pages have we written?
	long pagesWritten = seg * PAGES_PER_SEGMENT;
	// how many pages are left to write?
	long pagesLeft    = m_numPages - pagesWritten;
	// if none left to write return offset now
	if ( pagesLeft <= 0 ) return offset;
	// truncate to segment's worth of pages for writing purposes
	if ( pagesLeft > PAGES_PER_SEGMENT ) pagesLeft = PAGES_PER_SEGMENT;
	// determine writeSize for keys
	//long writeSize = pagesLeft * sizeof(key_t);
	long writeSize = pagesLeft * m_ks;
	// write the keys segment
	g_errno = 0;
	m_file.write ( (char *)m_keys[seg] , writeSize , offset );
	if ( g_errno ) return false;//log("RdbMapFile::writeSegment: failed");
	offset += writeSize ;
	// determine writeSize for relative 2-byte offsets
	writeSize = pagesLeft * 2;
	// write the offsets of segment
	m_file.write ( (char *)m_offsets[seg] , writeSize , offset );
	if ( g_errno ) return false;//log("RdbMapFile::writeSegment: failed");
	offset += writeSize ;
	// return the new offset
	return offset ;
}

// . called by openOld()
// . returns true on success
// . returns false on i/o error.
// . calls setMapSize() to get memory for m_keys/m_offsets
// . The format of the map on disk is described in Map.h
// . sets "m_numPages", "m_keys", and "m_offsets"
// . reads the keys and offsets into buffers allocated during open().
// . now we pass in ptr to the data file we map so verifyMap() can use it
bool RdbMap::readMap ( BigFile *dataFile ) {
	// bail if does not exist
	if ( ! m_file.doesExist() )
		return log("db: Map file %s does not exist.",
			   m_file.getFilename());
	// . open the file
	// . do not open O_RDONLY because if we are resuming a killed merge
	//   we will add to this map and write it back out.
	if ( ! m_file.open ( O_RDWR ) ) 
		return log("db: Could not open %s for reading: %s.",
			   m_file.getFilename(),mstrerror(g_errno));
	bool status = readMap2 ( );
	// . close map
	// . no longer since we use BigFile
	// . no, we have to close since we will hog all the fds
	// . we cannot call BigFile::close() because then RdbMap::unlink() will
	//   not work because BigFile::m_maxParts gets set to 0, and that is
	//   used in the loop in BigFile::unlinkRename().
	m_file.closeFds ( );
	// verify and fix map, data on disk could be corrupted
	if ( ! verifyMap ( dataFile ) ) return false;
	// return status
	return status;
}

bool RdbMap::verifyMap ( BigFile *dataFile ) {

	long long diff = m_offset - m_fileStartOffset;
	diff -= dataFile->getFileSize();
	// make it positive
	if ( diff < 0 ) diff = diff * -1LL;

	// . return false if file size does not match
	// . i've seen this happen before
	if ( diff ) {
		log(
		    "db: Map file %s says that file %s should be %lli bytes "
		    "long, but it is %lli bytes.",
		    m_file.getFilename(),
		    dataFile->m_baseFilename ,
		    m_offset - m_fileStartOffset ,
		    dataFile->getFileSize() );
		// we let headless files squeak by on this because we cannot
		// generate a map for them yet. if power went out a key can be 
		// caught in the middle of a write... thus limit to 12 bytes
		if ( dataFile->doesPartExist(0) || diff >= 12 ) return false;
		// explain it
		log("db: Datafile is headless (so the map can not be "
		    "regenerated right now) and the difference is < 12, so "
		    "we will let this one squeak by.");
		//log("RdbMap::verifyMap: Regenerating map.");
		//log("db: Please delete map file %s and restart. "
		//    "This will regenerate the map file.",
		//    //Continuing despite discrepancy.", 
		//    m_file.getFilename());
		//exit(-1);
		//return false;
		//return true;
	}
	// are we a 16k page size map?
	long long maxSize =(long long)(m_numPages + 1)*(long long)m_pageSize;
	long long minSize =(long long)(m_numPages - 1)*(long long)m_pageSize;
	long long dfs = dataFile->getFileSize();
	if ( dfs < minSize || dfs > maxSize ) {
		//log("db: File is not mapped with PAGE_SIZE of %li. Please "
		//    "delete map file %s and restart in order to regenerate "
		//    "it. Chances are you are running a new version of gb on "
		//    "old data.", (long)PAGE_SIZE, m_file.getFilename());
		log("db: File %s is not mapped with PAGE_SIZE of %li. "
		    "You may be running a new version of gb on "
		    "old data.", m_file.getFilename(),(long)m_pageSize);
		//exit (-1);
		return false;
	}
	// . first, if our data file is headless we may have to chop our heads
	//   because a merge was probably killed
	// . how many head PARTs are missing?
	//long numMissingParts = 0;
	//while ( ! dataFile->doesPartExist ( numMissingParts ) ) 
	//	numMissingParts++;

	// we should count backwards so we stop at the first gap from the top.
	// power outages sometimes leave one file linked when it should have
	// been unlinked... although a file after it was successfully recorded
	// as being unlinked on the hard drive, it itself was never committed.
	// thereby producing a gap in the contiguous sequence of part files.
	// let's ignore such islands. these islands can be more than one file
	// too. let's verify they are unlinked after the merge completes.
	long numMissingParts = dataFile->m_maxParts;
	while ( numMissingParts > 0 &&
		dataFile->doesPartExist ( numMissingParts-1 ) ) 
		numMissingParts--;
	if ( numMissingParts > 0 ) {
		File *f = dataFile->getFile ( numMissingParts );
		if ( f ) log("db: Missing part file before %s.",
			     f->getFilename());
	}

	// how many PARTs have been removed from map?
	long removed = m_fileStartOffset / MAX_PART_SIZE;
	// . balance it out
	// . don't map to PARTs of data file that have been chopped
	while ( removed < numMissingParts ) { 
		log(LOG_INIT,"db: Removing part #%li from map.",removed);
		chopHead ( MAX_PART_SIZE ); 
		removed++; 
	}
	// now fix the map if it had out of order keys in it
	return verifyMap2 ( );
}

// this just fixes a bad map
bool RdbMap::verifyMap2 ( ) {
 top:
	//key_t lastKey ; lastKey.n0 = 0LL; lastKey.n1 = 0;
	char lastKey[MAX_KEY_BYTES];
	KEYMIN(lastKey,m_ks);
	for ( long i = 0 ; i < m_numPages ; i++ ) {
		//key_t k;
		//k = getKey(i);
		//if ( k >= lastKey ) { lastKey = k; continue; }
		char *k = getKeyPtr(i);
		if ( KEYCMP(k,lastKey,m_ks)>=0 ) {
			KEYSET(lastKey,k,m_ks); continue; }
		// just bitch for now
		log(
		    "db: Key out of order in map file %s. "
		    "page = %li. key offset = %lli. Map or data file is "
		    "corrupt, but it is probably the data file.", 
		    m_file.getFilename() ,
		    i,(long long)m_pageSize*(long long)i+getOffset(i));

		//log("db: oldk.n1=%08lx n0=%016llx",
		//    lastKey.n1,lastKey.n0);
		//log("db: k.n1=%08lx n0=%016llx",k.n1 ,k.n0);
		log("db: oldk.n1=%016llx n0=%016llx",
		    KEY1(lastKey,m_ks),KEY0(lastKey));
		log("db:    k.n1=%016llx n0=%016llx",KEY1(k,m_ks),KEY0(k));
		log("db: m_numPages = %li",m_numPages);
		//char *xx=NULL;*xx=0;
		// was k too small?
		//if ( i + 1 < m_numPages && lastKey <= getKey(i+1) ) {
		if (i+1<m_numPages && KEYCMP(lastKey,getKeyPtr(i+1),m_ks)<=0){
			//key_t f = lastKey ;
			char f[MAX_KEY_BYTES];
			KEYSET(f,lastKey,m_ks);
			//if ( lastKey != getKey(i+1) ) f += (unsigned long)1;
			if (KEYCMP(lastKey,getKeyPtr(i+1),m_ks)!=0) 
				KEYADD(f,1,m_ks);
			setKey(i,f);
			log("db: Key in map was too small. Fixed.");
			goto top;
		}
		// was lastKey too big?
		//if ( i - 2 >= m_numPages && getKey(i-2) <= k ) {
		if ( i - 2 >= m_numPages && KEYCMP(getKeyPtr(i-2),k,m_ks)<=0) {
			//key_t f = getKey(i-2);
			char *f = getKeyPtr(i-2);
			//if ( f != k ) f += (unsigned long)1;
			if ( KEYCMP(f,k,m_ks)!=0) KEYADD(f,1,m_ks);
			setKey(i-1,f);
			log("db: LastKey in map was too big. Fixed.");
			goto top;
		}
		// otherwise it is a sequence of out-of-order keys
		long left  = i - 1;
		long right = i;
		// try removing left side
		//while ( left > 0 && getKey(left-1) > k ) 
		while ( left > 0 && KEYCMP(getKeyPtr(left-1),k,m_ks)>0 ) 
			left--;
		long leftCount = i - left;
		// try removing the right side
		//while ( right + 1 < m_numPages && getKey(right+1) < lastKey) 
		while ( right + 1 < m_numPages && 
			KEYCMP(getKeyPtr(right+1),lastKey,m_ks)<0) 
			right++;
		long rightCount = right - i + 1;
		// make [a,b] represent the smallest bad chunk that when 
		// removed will fix the map
		long  a , b ;
		if ( leftCount <= rightCount ) { a = left ; b = i - 1 ; }
		else                           { a = i    ; b = right ; }
		//key_t keya ; keya.n0 = 0LL; keya.n1 = 0;
		char *keya = KEYMIN();
		if ( a > 0 ) keya = getKeyPtr(a-1);
		// remove the smallest chunk
		for ( long j = a ; j <= b ; j++ )
			setKey ( j , keya );
		// count it for reference
		log("db: Removed bad block in map of %li pages. Data "
		    "may have been permanently lost. Consider "
		    "syncing from a twin.",b-a+1);
		// try from the top
		goto top;
	}
	return true;
}

bool RdbMap::readMap2 ( ) {
	// keep track of read offset
	long long offset = 0;
	g_errno = 0;
	// first 8 bytes are the size of the DATA file we're mapping
	m_file.read ( &m_offset , 8 , offset );
	if ( g_errno ) return log("db: Had error reading %s: %s.",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// when a BigFile gets chopped, keep up a start offset for it
	m_file.read ( &m_fileStartOffset , 8 , offset );
	if ( g_errno ) return log("db: Had error reading %s: %s.",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// read total number of non-deleted records
	m_file.read ( &m_numPositiveRecs , 8 , offset );
	if ( g_errno ) return log("db: Had error reading %s: %s.",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// read total number of deleted records
	m_file.read ( &m_numNegativeRecs , 8 , offset );
	if ( g_errno ) return log("db: Had error reading %s: %s.",
				  m_file.getFilename(),mstrerror(g_errno));
	offset += 8;
	// read total number of deleted records
	//m_file.read ( &m_lastKey , 12 , offset );
	m_file.read ( m_lastKey , m_ks , offset );
	if ( g_errno ) return log("db: Had error reading %s: %s.",
				  m_file.getFilename(),mstrerror(g_errno));
	//offset += 12;
	offset += m_ks;
	// get the total size of this map file from our derived file class
	long fileSize = m_file.getFileSize () ;
	if ( fileSize < 0 ) return log("db: getFileSize failed on %s: %s.",
				  m_file.getFilename(),mstrerror(g_errno));
	// read in the segments
	for ( long i = 0 ; offset < fileSize ; i++ ) {
		// . this advance offset passed the read segment
		// . it uses fileSize for reading the last partial segment
		offset = readSegment ( i , offset , fileSize ) ;
		if ( offset<=0 ) return log("db: Had error reading "
					    "%s: %s.",
					    m_file.getFilename(),
					    mstrerror(g_errno));
	}
	return true;
}

long long RdbMap::readSegment ( long seg , long long offset , long fileSize ) {
	// . add a new segment for this
	// . increments m_numSegments and increases m_maxNumPages
	if ( ! addSegment () ) return -1;
	// get the slot size, 1 12 byte key and 1 short offset per page
	//long slotSize = sizeof(key_t) + 2;
	long slotSize = m_ks + 2;
	// how much will we read now?
	long totalReadSize = PAGES_PER_SEGMENT * slotSize;
	// how much left in the map file?
	long long avail = fileSize - offset;
	// . what's available MUST always be a multiple of 16
	// . sanity check
	if ( ( avail % slotSize ) != 0 ) {
		log("db: Had error reading part of map: Bad map "
		    "size."); return -1; }
	// truncate if not a full segment
	if ( totalReadSize > avail ) totalReadSize = avail;
	// get # of keys/offsets to read
	long numKeys = totalReadSize / slotSize;
	// calculate how many bytes to read of keys
	//long readSize = numKeys * sizeof(key_t);
	long readSize = numKeys * m_ks;
	// do the read
	g_errno = 0;
	m_file.read ( (char *)m_keys[seg] , readSize , offset );
	if ( g_errno ) return false; // log("RdbMapFile::readSegment: failed");
	offset += readSize;
	// read the offsets of segment
	readSize = numKeys * 2;
	m_file.read ( (char *)m_offsets[seg] , readSize , offset );
	if ( g_errno ) return false; // log("RdbMapFile::readSegment: failed");
	offset += readSize ;
	// increase m_numPages based on the keys/pages read
	m_numPages += numKeys;
	// return the new offset
	return offset ;
}

// . add a record to the map
// . returns false and sets g_errno on error
// . offset is the current offset of the rdb file where the key/data was added
// . TODO: speed this up
// . we pass in "data" so we can compute the crc of each page
//bool RdbMap::addRecord ( key_t &key, char *rec , long recSize ) {
bool RdbMap::addRecord ( char *key, char *rec , long recSize ) {
	// calculate size of the whole slot
	//long size = sizeof(key_t) ;
	// include the dataSize, 4 bytes, for each slot if it's not fixed
	//if ( m_fixedDataSize == -1 ) size += 4;
	// include the data
	//size += dataSize;
	// what page is first byte of key on?
	//long pageNum         =  m_offset             / m_pageSize;
	long pageNum = m_offset >> m_pageSizeBits;
	// what is the last page we touch?
	//long lastPageNum     = (m_offset + recSize - 1) / m_pageSize;
	long lastPageNum     = (m_offset + recSize - 1) >> m_pageSizeBits;
	// . see if we need to reallocate/allocate more pages in the map.
	// . g_errno should be set to ENOMEM
	// . only do this if we're NOT adding to disk
	// . should only change m_maxNumPages, not m_numPages
	// . if the rec is HUGE it may span SEVERAL, so do a while()
	while ( lastPageNum + 2 >= m_maxNumPages ) {
		if ( ! addSegment() ) {
			log("db: Failed to add segment3 to map file %s.",
			    m_file.getFilename());
			// core dump until we revert to old values
			char *xx = NULL; *xx = 0;
		}
	}
	// we need to call writeMap() before we exit
	m_needToWrite = true;

#ifdef _SANITYCHECK_
	// debug
	log("db: addmap k=%s keysize=%li offset=%lli pagenum=%li",
	    KEYSTR(key,m_ks),recSize,m_offset,pageNum);
#endif

	// we now call RdbList::checkList_r() in RdbDump::dumpList()
	// and that checks the order of the keys
	//#ifdef _SANITYCHECK_
	// . sanity check
	// . a key of 0 is valid, so watch out for m_lastKey's sake
	//if ( key <= m_lastKey && (m_lastKey.n0!=0 || m_lastKey.n1!=0)) {
	if ( KEYCMP(key,m_lastKey,m_ks)<=0 && 
	     KEYCMP(m_lastKey,KEYMIN(),m_ks)!=0 ) {
		m_badKeys++;
		// do not log more than once per second
		if ( getTime() == m_lastLogTime ) goto skip;
		m_lastLogTime = getTime();
		//pageNum > 0 && getKey(pageNum-1) > getKey(pageNum) ) {
		log(LOG_LOGIC,"build: RdbMap: added key out of order. "
		    "count=%lli.",m_badKeys);
		//log(LOG_LOGIC,"build: k.n1=%lx %llx  lastKey.n1=%lx %llx",
		//    key.n1,key.n0,m_lastKey.n1,m_lastKey.n0 );
		log(LOG_LOGIC,"build: offset=%lli",
		    m_offset);
		log(LOG_LOGIC,"build: k1=%s",
		    KEYSTR(m_lastKey,m_ks));
		log(LOG_LOGIC,"build: k2=%s",
		    KEYSTR(key,m_ks));
		if ( m_generatingMap ) {
			g_errno = ECORRUPTDATA;
			return false;
		}
		char *xx=NULL;*xx=0;
		// . during a merge, corruption can happen, so let's core
		//   here until we figure out how to fix it.
		// . any why wasn't the corruption discovered and patched
		//   with a twin? or at least excised... because the read
		//   list may have all keys in order, but be out of order
		//   with respect to the previously-read list?
		//char *xx = NULL; *xx = 0;
		// let's ignore it for now and just add the corrupt
		// record (or maybe the one before was corrupted) but we
		// need to verify the map afterwards to fix these problems
		m_needVerify = true;
	//	sleep(50000);
	}
	//#endif
 skip:
	// remember the lastKey in the whole file
	//m_lastKey = key;
	KEYSET(m_lastKey,key,m_ks);
	// debug msg
	//log(LOG_LOGIC,"build: map add lastk.n1=%llx %llx",
	//    KEY1(m_lastKey,m_ks),KEY0(m_lastKey));
	// set m_numPages to the last page num we touch plus one 
	m_numPages = lastPageNum + 1;
	// keep a global tally on # of recs that are deletes (low bit cleared)
	//if ( (key.n0 & 0x01) == 0 ) m_numNegativeRecs++;
	if ( KEYNEG(key) ) m_numNegativeRecs++;
	// keep a global tally on # of recs that are NOT deletes
	else m_numPositiveRecs++;
	// increment the size of the data file
	m_offset += recSize ;
	// . reset all pages above pageNum that we touch
	// . store -1 in offset to indicate it's continuation of key which
	//   started on another page
	// . store -1 on lastPageNum PLUS 1 incase we just take up lastPageNum
	//   ourselves and the next key will start on lastPageNum+1 at offset 0
	// . also by storing -1 for offset this page becomes available for
	//   keys/recs to follow
	for ( long i = pageNum + 1; i <= lastPageNum; i++ ) setKey ( i , key );
	// . return now if we're NOT the first key wholly on page #pageNum
	// . add crc of this rec 
	// . this offset will be -1 for unstarted pages
	// . tally the crc until we hit a new page
	if ( getOffset ( pageNum ) >= 0 ) return true;
	// . if no key has claimed this page then we'll claim it
	// . by claiming it we are the first key to be wholly on this page
	setOffset ( pageNum , ( m_offset - recSize ) & (m_pageSize-1) );
	setKey    ( pageNum , key );
	// success!
	return true;
}

// . for adding a data-less key very quickly
// . i don't use m_numPages here (should use m_offset!)
// . TODO: can quicken by pre-initializing map size
// . TODO: don't use until it counts the # of deleted keys, etc...
/*
bool RdbMap::addKey ( key_t &key ) {

	// what page is first byte of key on?
	long pageNum = m_offset / m_pageSize;
 
	// increment the size of the data file
	m_offset += sizeof(key_t);

	// keep the number of pages up to date
	m_numPages = m_offset / m_pageSize + 1;

	// . see if we need to reallocate/allocate more pages in the map.
	// . g_errno should be set to ENOMEM
	// . only do this if we're NOT adding to disk
	if ( m_numPages >= m_maxNumPages ) 
		if ( ! setMapSize ( m_numPages + 8*1024 ) ) return false;

	// if no key has claimed this page then we'll claim it
	if ( m_offsets [ pageNum ] < 0 ) {
		m_offsets   [ pageNum ]  = m_offset % m_pageSize;
		m_keys      [ pageNum ]  = key;
	}

	// otherwise if current page already has a FIRST slot on it then return
	return true;
}
*/

// . call addRecord() or addKey() for each record in this list
bool RdbMap::prealloc ( RdbList *list ) {
	// sanity check
	if ( list->m_ks != m_ks ) { char *xx = NULL; *xx = 0; }
	// bail now if it's empty
	if ( list->isEmpty() ) return true;
	// what is the last page we touch?
	long lastPageNum = (m_offset + list->getListSize() - 1) / m_pageSize;
	// . need to pre-alloc up here so malloc does not fail mid stream
	// . TODO: only do it if list is big enough
	while ( lastPageNum + 2 >= m_maxNumPages ) {
		if ( ! addSegment() )
			return log("db: Failed to add segment to map file %s.",
				   m_file.getFilename());
	}
	return true;
}

// . call addRecord() or addKey() for each record in this list
bool RdbMap::addList ( RdbList *list ) {

	// sanity check
	if ( list->m_ks != m_ks ) { char *xx = NULL; *xx = 0; }

	// . reset list to beginning to make sure
	// . no, because of HACK in RdbDump.cpp we set m_listPtrHi < m_list
	//   so our first key can be a half key, calling resetListPtr() 
	//   will reset m_listPtrHi and fuck it up
	//list->resetListPtr();

	// bail now if it's empty
	if ( list->isEmpty() ) return true;

	// what is the last page we touch?
	long lastPageNum = (m_offset + list->getListSize() - 1) / m_pageSize;
	// . need to pre-alloc up here so malloc does not fail mid stream
	// . TODO: only do it if list is big enough
	while ( lastPageNum + 2 >= m_maxNumPages ) {
		if ( ! addSegment() )
			return log("db: Failed to add segment to map file %s.",
				   m_file.getFilename());
	}

	// . index lists are very special cases
	// . the keys may be the full 12 bytes or a compressed 6 bytes
	// . disable for now! for new linkdb, posdb, etc.
	//if ( list->useHalfKeys() )
	//	return addIndexList ( (IndexList *)list );

	// disabled until addKey() works correctly
	/*
	if ( list->isDataless() ) {
	top1:
		key = list->getCurrentKey ( );
		if ( ! addKey ( key ) ) return false;
		if ( list->skipCurrentRecord() ) goto top1;
		list->resetListPtr(); 
		return true; 
	}
	*/

#ifdef _SANITYCHECK_
	// print the last key from lasttime
	log("map: lastkey=%s",KEYSTR(m_lastKey,m_ks));
#endif

	//key_t key;
	char  key[MAX_KEY_BYTES];
	long  recSize;
	char *rec;
 top2:
	//key     = list->getCurrentKey ( );
	list->getCurrentKey(key);
	recSize = list->getCurrentRecSize();
	rec     = list->getCurrentRec ();
	if ( ! addRecord ( key , rec , recSize ) ) {
		log("db: Failed to add record to map: %s.",
		    mstrerror(g_errno));
		char *xx = NULL; *xx = 0;
	}
	if ( list->skipCurrentRecord() ) goto top2;

	// sanity check -- i added this for debug but i think it was
	// corrupted buckets!!
	//verifyMap2();

	list->resetListPtr(); 
	return true; 
}

// . a short list is a data-less list whose keys are 12 bytes or 6 bytes
// . the 6 byte keys are compressed 12 byte keys that actually have the
//   same most significant 6 bytes as the closest 12 byte key before them
// . CAUTION: this list may have a 6 byte key as its first key because
//   RdbDump does that hack so that on disk there are not unnecessary 12 byte
//   keys because that would make IndexTable.cpp:addLists_r() inefficient
bool RdbMap::addIndexList ( IndexList *list ) {

	// return now if empty
	if ( list->isEmpty() ) return true;

	// we need to call writeMap() before we exit
	m_needToWrite = true;

	// . reset list to beginning to make sure
	// . no, because of HACK in RdbDump.cpp we set m_listPtrHi < m_list
	//   so our first key can be a half key, calling resetListPtr() 
	//   will reset m_listPtrHi and fuck it up
	//list->resetListPtr();

	// what page # will the first rec of this list be on?
	long pageNum = m_offset / m_pageSize;
	long end;

	// convenience vars
	char *rec;
	char *recStart;
	char *recMax;
	char *recHi;
	// what was the size of the last key we hit in the while loop? 6 or 12?
	char size = 0;

	// compare our start key to last list's endkey
	char kp[MAX_KEY_BYTES];
	list->getCurrentKey(kp);
	if ( KEYCMP(kp,m_lastKey,m_ks) <= 0 &&
	     KEYCMP(m_lastKey,KEYMIN(),m_ks) != 0 ) {
		log(LOG_LOGIC,"build: RdbMap: added key out of order "
		    "in addIndexList. ");
		log(LOG_LOGIC,"build: k.n1=%llx %llx  lastKey.n1=%llx %llx. ",
		    KEY1(kp,m_ks),KEY0(kp),
		    KEY1(m_lastKey,m_ks),KEY0(m_lastKey));
		char *xx = NULL; *xx = 0;
	}

	// if the current page DOES NOT have a starting key, we are it
	if ( pageNum >= m_numPages ) goto startNewPage;

	// what is the last offset that can be on this page?
	end = m_offset + m_pageSize - (m_offset % m_pageSize) - 1;

	// get the current record
	rec = list->getListPtr();
	recStart = rec;
	// how far to advance rec?
	recMax = rec + (end - m_offset);
	// don't exceed list end
	if ( recMax > list->getListEnd() ) recMax = list->getListEnd();
	// . get hi ptr of record
	// . subtract 6 cuz we add 6 later
	//recHi = list->getListPtrHi() - 6;
	recHi = list->getListPtrHi() - (m_ks-6);
	// . is a record from the last list already starting on this page #?
	// . if it is already claimed, add until we hit next page
	// . is a record from the last list already starting on this page #?
	// . if it is already claimed, add until we hit next page
	while ( rec < recMax ) {
		// keep a global tally on # of recs that are deletes and NOT
		if   ( (*rec  & 0x01) == 0 ) m_numNegativeRecs++;
		else                         m_numPositiveRecs++;
		// is half bit on?
		//if   ( *rec & 0x02 ) { size = 6 ;              rec += 6;  }
		//else                 { size = 12; recHi = rec; rec += 12; }
		if   ( *rec & 0x02 ) { size = m_ks-6 ;           rec += size;}
		else                 { size = m_ks; recHi = rec; rec += size;}

	}
	// update list current ptr
	//list->setListPtrs ( rec , recHi + 6 );
	list->setListPtrs ( rec , recHi + (m_ks-6) );
	// and update m_offset, too
	m_offset += rec - recStart;

 startNewPage:

	// . if our list is done, return
	// . otherwise, we filled up the whole page
	if ( list->isExhausted() ) {
		// set m_lastKey
		//m_lastKey = list->getKey ( list->getListPtr() - size );
		list->getKey ( list->getListPtr() - size , m_lastKey );
		return true;
	}

	// do we need to add a segment?
	// . see if we need to reallocate/allocate more pages in the map.
	// . g_errno should be set to ENOMEM
	// . only do this if we're NOT adding to disk
	// . should only change m_maxNumPages, not m_numPages
	if ( m_numPages >= m_maxNumPages && ! addSegment() ) {
		log("db: Failed to add segment2 to map file %s.",
		    m_file.getFilename());
		// core dump until we revert to old values
		char *xx = NULL; *xx = 0;
	}

	// we are the first key fully on this page
	//key_t k = list->getCurrentKey();
	char k[MAX_KEY_BYTES];
	list->getCurrentKey(k);

	// the half bit should be in the off position, since k is 12 bytes,
	// even though the key in the list may only be 6 bytes (half key)
	setKey    ( m_numPages , k                    );
	setOffset ( m_numPages , m_offset % m_pageSize );

	// what is the last offset that can be on this page?
	end = m_offset + m_pageSize - (m_offset % m_pageSize) - 1;

	// get the current record
	rec = list->getListPtr();
	recStart = rec;
	// how far to advance rec?
	recMax = rec + (end - m_offset);
	// don't exceed list end
	if ( recMax > list->getListEnd() ) recMax = list->getListEnd();
	// . get hi ptr of record
	// . subtract 6 cuz we add 6 later
	//recHi = list->getListPtrHi() - 6;
	recHi = list->getListPtrHi() - (m_ks-6);
	// . is a record from the last list already starting on this page #?
	// . if it is already claimed, add until we hit next page
	while ( rec < recMax ) {
		// keep a global tally on # of recs that are deletes and NOT
		if   ( (*rec  & 0x01) == 0 ) m_numNegativeRecs++;
		else                         m_numPositiveRecs++;
		// is half bit on?
		//if   ( *rec & 0x02 ) { size = 6 ;              rec += 6;  }
		//else                 { size = 12; recHi = rec; rec += 12; }
		if   ( *rec & 0x02 ) { size = m_ks-6 ;           rec += size;}
		else                 { size = m_ks; recHi = rec; rec += size;}
	}
	// update list current ptr
	//list->setListPtrs ( rec , recHi + 6 );
	list->setListPtrs ( rec , recHi + (m_ks-6) );
	// and update m_offset, too
	m_offset += rec - recStart;
	// we occupied that page, baby
	m_numPages++;

	// start on the next page
	goto startNewPage;
}

// . set *rsp and *rep so if we read from first key on *rsp to first key on
//   *rep all records will have their key in [startKey,endKey]
// . the relative offset (m_offset[sp]) may be -1
// . this can now return negative sizes
long long RdbMap::getMinRecSizes ( long   sp       , 
			      long   ep       , 
			      //key_t  startKey , 
			      //key_t  endKey   ,
			      char  *startKey , 
			      char  *endKey   ,
			      bool   subtract ) {
	// . calculate first page, "sp", whose key is >= startKey
	// . NOTE: sp may have a relative offset of -1 
	// . in this case, just leave it be!
	//while ( sp <  ep && getKey(sp) <  startKey ) sp++;
	while ( sp <  ep && KEYCMP(getKeyPtr(sp),startKey,m_ks)<0 ) sp++;
	// now calculate endpg whose key is <= endKey
	long ep1 = ep;
	//while ( ep >  sp && getKey(ep) >  endKey   ) ep--;
	while ( ep >  sp && KEYCMP(getKeyPtr(ep),endKey,m_ks)>0   ) ep--;
	// . if ep has a relative offset of -1 we can advance it
	// . we cannot have back-to-back -1 offset with DIFFERENT keys
	while ( ep <= ep1 && ep < m_numPages && getOffset(ep) == -1 ) ep++;
	// now getRecSizes on this contrained range
	return getRecSizes ( sp , ep , subtract );
}

// . like above, but sets an upper bound for recs in [startKey,endKey]
long long RdbMap::getMaxRecSizes ( long   sp       , 
			      long   ep       , 
			      //key_t  startKey , 
			      //key_t  endKey   ,
			      char  *startKey , 
			      char  *endKey   ,
			      bool   subtract ) {
	// . calculate first page, "sp", whose key is >= startKey
	// . NOTE: sp may have a relative offset of -1 
	// . in this case, just leave it be!
	//while ( sp > 0 && getKey(sp) >  startKey ) sp--;
	while ( sp > 0 && KEYCMP(getKeyPtr(sp),startKey,m_ks)>0 ) sp--;
	// now calculate endpg whose key is > endKey
	//while ( ep < m_numPages && getKey(ep) <  endKey   ) ep++;
	while ( ep < m_numPages && KEYCMP(getKeyPtr(ep),endKey,m_ks)<0 ) ep++;
	// . if ep has a relative offset of -1 we can advance it
	// . we cannot have back-to-back -1 offset with DIFFERENT keys
	while ( ep < m_numPages && getOffset(ep) == -1 ) ep++;
	// now getRecSizes on this contrained range
	return getRecSizes ( sp , ep , subtract );
}

// . how many bytes in the range?
// . range is from first key on startPage UP TO first key on endPage
// . if endPage is >= m_numPages then range is UP TO the end of the file
// . this can now return negative sizes
long long RdbMap::getRecSizes ( long startPage ,
				long endPage ,
				bool subtract ) {
	// . assume a minimum of one page if key range not well mapped
	// . no, why should we?
	// . if pages are the same, there's no recs between them!
	// . this seemed to cause a problem when startPage==endPage == lastPage
	//   and we started in the middle of a dump, so instead of reading
	//   0 bytes, since offset was the end of the file, the dump dumped
	//   some and we read that. And the # of bytes we read was not 
	//   divisible by sizeof(key_t) and RdbList::checkList_r() complained
	//   about the last key out of order, but that last key's last 8
	//   bytes were garbage we did NOT read from disk... phew!
	if ( startPage == endPage ) return 0; // return (long)m_pageSize;

	long long offset1;
	long long offset2;

	if ( ! subtract ) {
		offset1 = getAbsoluteOffset ( startPage );
		offset2 = getAbsoluteOffset ( endPage   );
		return offset2 - offset1;
	}

	// . but take into account delete keys, so we can have a negative size!
	// . use random sampling
	long long size = 0;
	//key_t     k;
	char *k;
	for ( long i = startPage ; i < endPage ; i++ ) {
		// get current page size
		offset1 = getAbsoluteOffset ( i     );
		offset2 = getAbsoluteOffset ( i + 1 );
		// get startKey for this page
		k = getKeyPtr ( i );
		// if key is a delete assume all in page are deletes
		//if ( (k.n0)&0x01 == 0LL) size -= (offset2 - offset1);
		if ( KEYNEG(k) )         size -= (offset2 - offset1);
		else                     size += (offset2 - offset1);
	}
	// return the size
	return size;
}

// if page has relative offset of -1, use the next page
long long RdbMap::getAbsoluteOffset ( long page ) {
 top:
	if ( page >= m_numPages ) return m_offset; // fileSize
	long long offset = 
		(long long)getOffset(page) +
		(long long)m_pageSize * (long long)page; 
	if ( getOffset(page) != -1 ) return offset + m_fileStartOffset;
	// just use end of page if in the middle of a record
	while ( page < m_numPages && getOffset(page) == -1 ) page++;
	goto top;
}

// . get offset of next known key after the one in page
// . do a while to skip rec on page "page" if it spans multiple pages
// . watch out for eof
long long RdbMap::getNextAbsoluteOffset ( long page ) {
	// advance to next page
	page++;
	// inc page as long as we need to
	while ( page < m_numPages && getOffset(page) == -1 ) page++;
	// . if we hit eof then return m_offset
	// . otherwise, we hit another key
	return getAbsoluteOffset ( page );
}

// . [startPage,*endPage] must cover [startKey,endKey]
// . by cover i mean have all recs with those keys
// . returns the endPage #
//long RdbMap::getEndPage ( long startPage , key_t endKey ) {
long RdbMap::getEndPage ( long startPage , char *endKey ) {
	// use "ep" for the endPage we're computing
	long ep = startPage;
	// advance if "ep"'s key <= endKey
	//while ( ep < m_numPages && getKey(ep) <= endKey ) ep++;
	while ( ep < m_numPages && KEYCMP(getKeyPtr(ep),endKey,m_ks)<=0 ) ep++;
	// now we may have ended up on a page with offset of -1
	// which is not good so, even if page's key is > endKey, advance it
	while ( ep < m_numPages && getOffset(ep) == -1 ) ep++;
	// now we're done
	return ep;
}

// . convert a [startKey,endKey] range to a [startPage,endPage] range
// . this says that if you read from first key offset on *startPage UP TO
//   first key offset on *endPage you'll get the keys/recs you want
// . if *endPage equals m_numPages then you must read to the end of file
// . returns false if no keys in [startKey,endKey] are present
// . *maxKey will be an APPROXIMATION of the max key we have
//bool RdbMap::getPageRange ( key_t  startKey  , 
//			    key_t  endKey    ,
bool RdbMap::getPageRange ( char  *startKey  , 
			    char  *endKey    ,
			    long  *startPage , 
			    long  *endPage   ,
			    //key_t *maxKey    ,
			    char  *maxKey    ,
			    long long oldTruncationLimit ) {
	// the first key on n1 is usually <= startKey, but can be > startKey
	// if the page (n-1) has only 1 rec whose key is < startKey
	long n1 = getPage ( startKey );
	// . get the ending page for this scan
	// . tally up the deleted keys as we go
	long n2 = getPage ( endKey   );
	// . set maxKey if we need to
	// . ensure that it is in [startKey,endKey] because it is used for
	//   determining what the length of an IndexList would have been
	//   if it was not truncated
	// . that is, we use maxKey for interpolation
	if ( maxKey ) {
		long n3 = n2;
		if ( oldTruncationLimit >= 0 ) {
			long nn = n1 + (oldTruncationLimit*6LL) / m_pageSize;
			if ( n3 > nn ) n3 = nn;
		}
		//while ( n3 > n1 && getKey(n3) > endKey ) n3--;
		while ( n3 > n1 && KEYCMP(getKeyPtr(n3),endKey,m_ks)>0 ) n3--;
		//*maxKey = getKey ( n3 );
		KEYSET(maxKey,getKeyPtr(n3),m_ks);
	}
	// . if the first key appearing on this page is <= endKey we inc n2
	// . make m_keys[n2] > endKey since we read up to first key on n2
	// . n2 can be equal to m_numPages (means read to eof then)
	//while ( n2 < m_numPages && getKey ( n2 ) <= endKey ) n2++;
	while ( n2 < m_numPages && KEYCMP(getKeyPtr(n2),endKey,m_ks)<=0 ) n2++;
	// skip n2 over any -1 offset
	while ( n2 < m_numPages && getOffset ( n2 ) == -1 ) n2++;
	// neither n1 nor n2 should have a -1 offset
	//if ( m_offsets[n1] == -1 || m_offsets[n2] == -1 ) {
	//log("getPageRange: bad engineer"); exit (-1); }
	// if n1 == n2 then it's a not found since the key on page n1 is >
	// startKey and > endKey AND all keys on page n1-1 are < startKey
	//if ( n1 == n2 ) return false;
	// otherwise set our stuff and return true
	*startPage = n1;
	*endPage   = n2;
	return true;
}

/*
bool RdbMap::getPageRange ( key_t  startKey  , key_t endKey  ,
			    long   minRecSizes ,
			    long  *startPage , long *endPage ) {

	// the first key on n1 is usually <= startKey, but can be > startKey
	// if the page (n-1) has only 1 rec whose key is < startKey
	long n1 = getPage ( startKey );
	// set n2, the endpage
	long n2 = n1;
	// tally the recSizes
	long recSizes = 0;
	// . increase n2 until we are > endKey or meet minRecSizes requirement
	// . if n2 == m_numPages, that means all the pages from n1 on
	while ( n2<m_numPages && m_keys[n2]<=endKey && recSizes<minRecSizes) {
		// . find the next value for n2
		// . m_offsets[n2] may not be -1
		long next = n2 + 1;
		while ( next < m_numPages && m_offsets[next] == -1 ) next++;
		// . getPageSize() returns size from the first key on page n2
		//   to the next key on the next page
		// . next key may be more than 1 page away if key on page n2
		//   takes up more than 1 page (-1 == m_offsets[n2+1])
		if ( m_keys[n2] >= startKey ) recSizes += getRecSizes(n2,next);
		n2 = next;
	}
	// otherwise set our stuff and return true
	*startPage = n1;
	*endPage   = n2;
	return true;
}
*/

// . return a page number, N
// . if m_keys[N] < startKey then m_keys[N+1] is > startKey
// . if m_keys[N] > startKey then all keys before m_keys[N] in the rdb file
//   are < startKey
// . if m_keys[N] > startKey then m_keys[N-1] spans multiple pages so that 
//   the key immediately after it on disk is in fact, m_keys[N]
//long RdbMap::getPage ( key_t startKey ) {
long RdbMap::getPage ( char *startKey ) {
	// if the key exceeds our lastKey then return m_numPages
	//if ( startKey > m_lastKey ) return m_numPages;
	if ( KEYCMP(startKey,m_lastKey,m_ks)>0 ) return m_numPages;
	// . find the disk offset based on "startKey"
	// . b-search over the map of pages
	// . "n"   is the page # that has a key <= "startKey"
	// . "n+1" has a key that is > "startKey"
	long n     = ( m_numPages  ) / 2;
	long step  = n / 2;
	while ( step > 0 ) {
		//if   ( startKey <= getKey ( n ) ) n -= step;
		//else                              n += step;
		if   ( KEYCMP(startKey,getKeyPtr(n),m_ks)<=0 ) n -= step;
		else                                        n += step;
		step >>= 1; // divide by 2
	}
	// . let's adjust for the inadaquecies of the above algorithm...
	// . increment n until our key is >= the key in the table
	//while ( n < m_numPages - 1 &&  getKey(n) < startKey ) n++;
	while ( n<m_numPages - 1 && KEYCMP(getKeyPtr(n),startKey,m_ks)<0 ) n++;
	// . decrement n until page key is LESS THAN OR EQUAL to startKey
	// . it is now <= the key, not just <, since, if the positive
	//   key exists it, then the negative should not be in this file, too!
	//while ( n > 0              &&  getKey(n) > startKey ) n--;
	while ( n>0              && KEYCMP(getKeyPtr(n),startKey,m_ks)>0 ) n--;
	// debug point
	//if ( m_offsets[n] == -1 && m_keys[n] == startKey && 
	//m_keys[n-1] != startKey )
	//log("debug point\n");
	// . make sure we're not in the middle of the data
	// . decrease n until we're on a page that has the start of a key
	while ( n > 0  &&  getOffset(n) == -1 ) n--;
	// this is the page we should start reading at
	return n;
	// . return immediately if this is our key (exact match)
	//if ( m_keys[n] == startKey ) return n;
	// . now m_keys[n] should be < startKey
	// . the next m_key, however, should be BIGGER than our key
	// . but if m_keys[n] spans multiple pages then skip over it
	//   because the next key in the map IMMEDIATELY follows it
	//if ( n < m_numPages - 1 &&  m_offsets[n+1] == -1 ) 
	//while ( n < m_numPages - 1 ) n++;
	// . now m_keys[n] may actually be bigger than startKey but it's
	//   only because the previous key on disk is less than startKey
	//return n;
}

void RdbMap::printMap () {
	long h = 0;
	for ( int i = 0 ; i < m_numPages; i++ ) {
		//log(LOG_INFO,"page=%i) key=%llu--%lu, offset=%hi\n",
		//	i,getKey(i).n0,getKey(i).n1,getOffset(i));
		// for comparing
		char buf[1000];
		sprintf(buf,"page=%i) key=%llx %llx, offset=%hi",
		    i,KEY1(getKeyPtr(i),m_ks),KEY0(getKeyPtr(i)),
		    getOffset(i));
		h = hash32 ( buf , gbstrlen(buf) , h );
		log(LOG_INFO,"%s",buf);
	}
	log(LOG_INFO,"map checksum = 0x%lx",h);
}

//long RdbMap::setMapSizeFromFileSize ( long fileSize ) {
//	long n = fileSize / m_pageSize ;
//	if ( (fileSize % m_pageSize) == 0 ) return setMapSize ( n );
//	return setMapSize ( n + 1 );
//}

// . returns false if malloc had problems
// . increases m_maxNumPages 
// . increases m_numSegments
//bool RdbMap::setMapSize ( long numPages ) {
	// . add the segments
	// . addSegment() increases m_maxNumPages with each call
	// . it returns false and sets g_errno on error
//	for ( long i = 0 ; m_maxNumPages < numPages ; i++ ) 
//		if ( ! addSegment ( ) ) return false;
//	return true;
//}

long long RdbMap::getMemAlloced ( ) {
	// . how much space per segment?
	// . each page has a key and a 2 byte offset
	//long long space = PAGES_PER_SEGMENT * (sizeof(key_t) + 2);
	long long space = PAGES_PER_SEGMENT * (m_ks + 2);
	// how many segments we use * segment allocation
	return (long long)m_numSegments * space;
}

bool RdbMap::addSegmentPtr ( long n ) {
	// realloc
	if ( n >= m_numSegmentPtrs ) {
		char **k;
		long nn = (long)((float)n * 1.20) + 1;
		k = (char **) mrealloc (m_keys,
					m_numSegmentPtrs * sizeof(char *) ,
					nn * sizeof(char *) ,
					"MapPtrs" );
		// failed?
		if ( ! k ) return false;
		// succeeded
		m_numSegmentPtrs = nn;
		m_keys = k;
	}

	// try offsets 
	if ( n >= m_numSegmentOffs ) {
		short **o;
		long nn = (long)((float)n * 1.20) + 1;
		o = (short **) mrealloc (m_offsets,
					 m_numSegmentOffs * sizeof(short *) ,
					 nn * sizeof(short *) ,
					 "MapPtrs" );
		// failed?
		if ( ! o ) return false;
		// succeeded
		m_numSegmentOffs = nn;
		m_offsets = o;
	}
	return true;
}
	

// . add "n" segments
// . returns false and sets g_errno on error
bool RdbMap::addSegment (  ) {
	// a helper variable
	//long ks = sizeof(key_t);
	long ks = m_ks;
	// easy variables
	long n   = m_numSegments;
	long pps = PAGES_PER_SEGMENT;
	// ensure doesn't exceed the max
	//if ( n >= MAX_SEGMENTS ) return log("db: Mapped file is "
	//				    "too big. Critical error.");

	// the array of up to MAX_SEGMENT pool ptrs is now dynamic too!
	// because diffbot uses thousands of collections, this will save
	// over 1GB of ram!
	if ( ! addSegmentPtr ( n ) )
		return log("db: Failed to allocate memory for adding seg ptr "
			   "for map file %s.", m_file.getFilename());


	// alloc spaces for each key segment
	// allocate new segments now 
	//m_keys[n]    = (key_t         *) mmalloc ( ks * pps , "RdbMap" );
	m_keys[n]    = (char          *) mmalloc ( ks * pps , "RdbMap" );
	m_offsets[n] = (short         *) mmalloc ( 2  * pps , "RdbMap" );
	bool hadProblem = false;
	// free up the segment on any problem
	if ( ! m_offsets[n] ) hadProblem = true;
	if ( ! m_keys   [n] ) hadProblem = true;
	if ( hadProblem ) {
		if ( m_keys   [n] ) mfree ( m_keys[n]   , ks*pps, "RdbMap" );
		if ( m_offsets[n] ) mfree ( m_offsets[n], 2*pps , "RdbMap" );
		// set to NULL so we know if accessed illegally
		m_keys   [n] = NULL;
		m_offsets[n] = NULL;
		return log(
			   "db: Failed to allocate memory for adding to "
			   "map file %s.", m_file.getFilename());
	}
	// set all new offsets to -1
	for ( long j = 0 ; j < PAGES_PER_SEGMENT ; j++ ) m_offsets[n][j] = -1;
	// reset m_maxNumPages and m_numSegments
	m_numSegments++;
	m_maxNumPages += PAGES_PER_SEGMENT;
	return true;
}

// . chop off any segment COMPLETELY before pageNum
// . if pageNum is -1 free ALL segments
// . fileHeadSize should equal MAX_PART_SIZE #define'd in BigFile.h
// . MAX_PART_SIZE is the max size of a little file that is part of a BigFile
bool RdbMap::chopHead ( long fileHeadSize ) {
	// ensure fileHeadSize is valid
	if ( fileHeadSize != MAX_PART_SIZE ) 
		return log(LOG_LOGIC,"db: rdbmap: chopHead: fileHeadSize of "
			   "%li is invalid.", fileHeadSize );
	// what segment does this page fall on?
	long segNum = (fileHeadSize / m_pageSize) / PAGES_PER_SEGMENT;
	// . must match exactly
	// . not any more i guess, we can still have a segment that
	//   corresponds in part to a PART file no longer with us
	//if ( fileHeadSize * m_pageSize * PAGES_PER_SEGMENT != segNum )
	//return log("RdbMap::chopHead: file head isn't multiple");
	// return true if nothing to delete
	if ( segNum == 0 ) return true;
	// . we need to call writeMap() before we exit
	// . not any more! if the merge is killed or saved in the middle then
	//   verifyMap() will now call chopHead() until the head of the map
	//   matches the head PART file of the data file we map
	//m_needToWrite = true;
	// a helper variable
	//long ks = sizeof(key_t);
	long ks = m_ks;
	// remove segments before segNum
	for ( long i = 0 ; i < segNum ; i++ ) {
		mfree ( m_keys   [i] , ks * PAGES_PER_SEGMENT , "RdbMap" );
		mfree ( m_offsets[i] , 2  * PAGES_PER_SEGMENT , "RdbMap" );
		// set to NULL so we know if accessed illegally
		m_keys   [i] = NULL;
		m_offsets[i] = NULL;
	}
	// adjust # of segments down
	m_numSegments -= segNum;
	// same with max # of used pages
	m_maxNumPages -= PAGES_PER_SEGMENT * segNum ;
	// same with # of used pages, since the head was ALL used
	m_numPages    -= PAGES_PER_SEGMENT * segNum ;
	// this could be below zero if last segment was chopped
	if ( m_numPages < 0 ) m_numPages = 0;
	// if 0 return now
	// if ( m_numSegments == 0 ) return true;
	// bury the stuff we chopped
	//long sk = sizeof(key_t *);
	long sk = sizeof(char  *);
	long ss = sizeof(short *);
	memmove ( &m_keys   [0] , &m_keys   [segNum] , m_numSegments * sk );
	memmove ( &m_offsets[0] , &m_offsets[segNum] , m_numSegments * ss );
	// adjust the m_fileStartOffset so getAbsoluteOffset(),... is ok
	m_fileStartOffset += segNum * PAGES_PER_SEGMENT * m_pageSize;
	return true;
}

// . attempts to auto-generate from data file, f
// . returns false and sets g_errno on error
bool RdbMap::generateMap ( BigFile *f ) {
	reset();
	if ( g_conf.m_readOnlyMode ) return false;
	// we don't support headless datafiles right now
	if ( ! f->doesPartExist(0) ) {
		g_errno = EBADENGINEER;
		return log("db: Cannot generate map for "
			   "headless data files yet.");
	}
	// scan through all the recs in f
	long long offset = 0;
	long long fileSize = f->getFileSize();
	// if file is length 0, we don't need to do much
	if ( fileSize == 0 ) return true;
	// g_errno should be set on error
	if ( fileSize < 0 ) return false;
	// don't read in more than 10 megs at a time initially
	long long  bufSize = fileSize;
	if ( bufSize > 10*1024*1024 ) bufSize = 10*1024*1024;
	char *buf = (char *)mmalloc ( bufSize , "RdbMap" );
	// use extremes
	//key_t endKey;
	//key_t startKey;
	//endKey.setMax();
	//startKey.setMin();
	char *startKey = KEYMIN();
	char *endKey   = KEYMAX();
	// a rec needs to be at least this big
	long minRecSize = 0;
	// negative keys do not have the dataSize field... so undo this
	if ( m_fixedDataSize == -1 ) minRecSize += 0; // minRecSize += 4;
	else                         minRecSize += m_fixedDataSize;
	//if ( m_useHalfKeys         ) minRecSize += 6;
	//else                         minRecSize += 12;
	if ( m_ks == 18            ) minRecSize += 6; // POSDB
	else if ( m_useHalfKeys    ) minRecSize += m_ks-6;
	else                         minRecSize += m_ks;
	// for parsing the lists into records
	//key_t key;
	char key[MAX_KEY_BYTES];
	long  recSize = 0;
	char *rec     = buf;
	long long next = 0LL;
	m_generatingMap = true;
	// read in at most "bufSize" bytes with each read
 readLoop:
	// keep track of how many bytes read in the log
	if ( offset >= next ) {
		if ( next != 0 ) logf(LOG_INFO,"db: Read %lli bytes.", next );
		next += 500000000; // 500MB
	}
	// our reads should always block
	long long readSize = fileSize - offset;
	if ( readSize > bufSize ) readSize = bufSize;
	// if the readSize is less than the minRecSize, we got a bad cutoff
	// so we can't go any more
	if ( readSize < minRecSize ) {
		mfree ( buf , bufSize , "RdbMap");
		return true;
	}
	// otherwise, read it in
	if ( ! f->read ( buf , readSize , offset ) ) {
		mfree ( buf , bufSize , "RdbMap");
		return log("db: Failed to read %lli bytes of %s at "
			   "offset=%lli. Map generation failed.",
			   bufSize,f->getFilename(),offset);
	}
	// set the list
	RdbList list;
	list.set ( buf             ,
		   readSize        ,
		   buf             ,
		   readSize        ,
		   startKey        ,
		   endKey          ,
		   m_fixedDataSize ,
		   false           , // own data?
		   //m_useHalfKeys   );
		   m_useHalfKeys   ,
		   m_ks            );
	// . HACK to fix useHalfKeys compression thing from one read to the nxt
	// . "key" should still be set to the last record we read last read
	//if ( offset > 0 ) list.m_listPtrHi = ((char *)&key)+6;
	if ( offset > 0 ) list.m_listPtrHi = key+(m_ks-6);

	// ... fix for posdb!!!
	if ( offset > 0 && m_ks == 18 ) list.m_listPtrLo = key+(m_ks-12);

	// . parse through the records in the list
	// . stolen from RdbMap::addList()
 nextRec:
	rec = list.getCurrentRec ();
	if ( rec+64 > list.getListEnd() && offset+readSize < fileSize ) {
		// set up so next read starts at this rec that MAY have been
		// cut off
		offset += (rec - buf);
		goto readLoop;
	}
	// WARNING: when data is corrupted these may cause segmentation faults?
	//key     = list.getCurrentKey ( );
	list.getCurrentKey(key);
	recSize = list.getCurrentRecSize();
	//rec     = list.getCurrentRec ();
	// don't chop keys
	//if ( recSize > 1000000 ) { char *xx = NULL; *xx = 0; }
	if ( recSize < 6 ) {
		log("db: Got negative recsize of %li at offset=%lli "
		    "lastgoodoff=%lli", 
		    recSize , offset + (rec-buf), m_offset );
		// it truncates to m_offset!
		if ( truncateFile(f) ) goto done;
		return false;
	}
	// do we have a breech?
	if ( rec + recSize > buf + readSize ) {
		// save old
		long long oldOffset = offset;
		// set up so next read starts at this rec that got cut off
		offset += (rec - buf);
		// . if we advanced nothing, then we'll end up looping forever
		// . this will == 0 too, for big recs that did not fit in our
		//   read but we take care of that below
		// . this can happen if merge dumped out half-ass
		// . the write split a record...
		if ( rec - buf == 0 && recSize <= bufSize  ) {
			log(
			    "db: Map generation failed because last record "
			    "in data file was split. Power failure while "
			    "writing? Truncating file to %lli bytes. "
			    "(lost %lli bytes)", offset,fileSize-offset);
			// when merge resumes it call our getFileSize()
			// in RdbMerge.cpp::gotLock() to set the dump offset
			// otherwise, if we don't do this and write data
			// in the middle of a split record AND then we crash
			// without saving the map again, the second call to
			// generateMap() will choke on that boundary and 
			// we'll lose a massive amount of data like we did
			// with newspaperarchive
			m_offset = offset;
			goto done;
		}
		// ...we can now have huge titlerecs...
		// is it something absurd? (over 40 Megabytes?)
		/*
		if ( recSize > 40*1024*1024 ) {
			// now just cut it short
			//g_errno = ECORRUPTDATA;
			log(
			    "RdbMap::generateMap: Insane rec size of "
			    "%li bytes encountered. off=%lli. "
			    "data corruption? ignoring.",
			    recSize, offset);
			//log("RdbMap::generateMap: truncating the file.");
			//goto done;
		}
		*/
		// is our buf big enough to hold this type of rec?
		if ( recSize > bufSize ) {
			mfree ( buf , bufSize , "RdbMap");
			bufSize = recSize;
			buf = (char *)mmalloc ( bufSize , "RdbMap" );
			if ( ! buf )
				return log("db: Got error while "
					   "generating the map file: %s. "
					   "offset=%llu.",
					   mstrerror(g_errno),oldOffset);
		}
		// read agin starting at the adjusted offset
		goto readLoop;

	}
	if ( ! addRecord ( key , rec , recSize ) ) {
		// if it was key out of order, it might be because the
		// power went out and we ended up writing a a few bytes of
		// garbage then a bunch of 0's at the end of the file.
		// if the truncate works out then we are done.
		if ( g_errno == ECORRUPTDATA && truncateFile(f) ) goto done;
		// otherwise, give it up
		mfree ( buf , bufSize , "RdbMap");
		return log("db: Map generation failed: %s.",
			   mstrerror(g_errno));
	}
	// skip current good record now
	if ( list.skipCurrentRecord() ) goto nextRec;
	// advance offset
	offset += readSize;
	// loop if more to go
	if ( offset < fileSize ) goto readLoop;
done:
	// don't forget to free this
	mfree ( buf , bufSize , "RdbMap");
	// if there was bad data we probably added out of order keys
	if ( m_needVerify ) {
		log("db: Fixing map. Added at least %lli bad keys.",
		    m_badKeys);
		verifyMap2();
		m_needVerify = false;
	}

	// otherwise, we're done
	return true; 
}

// 5MB is a typical write buffer size, so do a little more than that
#define MAX_TRUNC_SIZE 6000000

bool RdbMap::truncateFile ( BigFile *f ) {
	// right now just use for indexdb, datedb, tfnb, etc.
	//if ( m_fixedDataSize != 0 ) return false;
	// how big is the big file
	long long fileSize = f->getFileSize();
	long long tail = fileSize - m_offset;

	//if ( tail > 20*1024*1024 )
	//	return log("db: Cannot truncate data file because bad tail is "
	//		   "%lli bytes > %li.",tail,(long)MAX_TRUNC_SIZE);

	// up to 20MB is ok to remove if most just bytes that are zeroes
	log("db: Counting bytes that are zeroes in the tail.");
	long long count = 0;
	char buf [100000];
	long long off = m_offset;
 loop:
	long readSize = fileSize - off;
	if ( readSize > 100000 ) readSize = 100000;
	f->read ( buf , readSize , off );
	if ( ! f->read ( buf , readSize , off ) ) {
		return log("db: Failed to read %li bytes of %s at "
			   "offset=%lli.",
			   readSize,f->getFilename(),off);
	}
	// count the zero bytes
	for ( long i = 0 ; i < readSize ; i++ )
		if ( buf[i] == 0 ) count++;
	// read more if we can
	off += readSize;
	if ( off < fileSize ) goto loop;
	// remove those from the size of the tail
	tail -= count;

	// if too much remains, do not truncate it
	if ( tail > MAX_TRUNC_SIZE )
		return log("db: Cannot truncate data file because bad tail is "
			   "%lli bytes > %li. That excludes bytes that are "
			   "zero.",tail,  (long)MAX_TRUNC_SIZE);

	// how many parts does it have?
	long numParts = f->getNumParts();
	// what part num are we on?
	long partnum = f->getPartNum ( m_offset );
	File *p = f->getFile ( partnum );
	if ( ! p ) return log("db: Unable to get part file.");
	// get offset relative to the part file
	long newSize = m_offset % (long long)MAX_PART_SIZE;

	// log what we are doing
	long oldSize = p->getFileSize();
	long lost    = oldSize - newSize;
	log("db: Removing %li bytes at the end of %s. Power outage "
	    "probably corrupted it.",lost,p->getFilename());
	log("db: Doing a truncate(%s,%li).",p->getFilename(),newSize);

	// we must always be the last part of next to last part
	if ( partnum != numParts-1 && partnum != numParts-2 )
		return log("db: This file is not the last part or next to "
			   "last part for this file. aborting truncation.");
	// sanity check. if we are not the last part file, but are the next
	// to last one, then the the last part file must be less than 
	// MAX_TRUNC_SIZE bytes big
	File *p2 = NULL;
	if ( partnum == numParts-2 ) {
		p2 = f->getFile ( partnum + 1 );
		if ( ! p2 ) return log("db: Could not get next part in line.");
		if ( p2->getFileSize() > MAX_TRUNC_SIZE )
			return log("db: Next part file is bigger than %li "
				   "bytes.",(long)MAX_TRUNC_SIZE);
	}
	// do the truncation
	if ( truncate ( p->getFilename() , newSize ) ) 
		// return false if had an error
		return log("db: truncate(%s,%li): %s.",
			   p->getFilename(),newSize,mstrerror(errno));
	// if we are not the last part, remove it
	if ( partnum == numParts-2 ) {
		log("db: Removing tiny last part. unlink (%s).",
		    p2->getFilename());
		// ensure it is smaller than 1k
		if ( ! p2->unlink() )
			return log("db: Unlink of tiny last part failed.");
	}

	// reset file size, parts, etc on the big file since we truncated
	// a part file and possibly removed another part file
	if ( ! f->reset() ) 
		return log("db: Failed to reset %s.",f->getFilename());
	// success
	return true;
}
