#include "gb-include.h"

#include "RdbMem.h"
//#include "RdbDump.h"
#include "Rdb.h"

RdbMem::RdbMem() {
	m_mem     = NULL;
	m_memSize = 0;
	//m_dump  = NULL;
	m_rdb     = NULL;
	m_is90PercentFull = false;
}

RdbMem::~RdbMem() {
	if ( m_mem ) mfree ( m_mem , m_memSize , m_allocName );
}

void RdbMem::reset ( ) {
	if ( m_mem ) mfree ( m_mem , m_memSize , m_allocName );
	m_mem = NULL;
}

void RdbMem::clear ( ) {
	// set up primary/secondary mem ptrs
	m_ptr1 = m_mem;
	// secondary mem initially grow downward
	m_ptr2 = m_mem + m_memSize;
}	

/*
#include <asm/page.h> // PAGE_SIZE

// return #of bytes scanned for timing purposes
int32_t RdbMem::scanMem ( ) {
	// ahh.. just scan the whole thing to keep it simple
	char *p    = m_mem + 64 ;
	char *pend = m_mem + m_memSize;
	char  c;
	while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	return m_memSize;
}
*/

// initialize us with the RdbDump class your rdb is using
bool RdbMem::init ( Rdb *rdb , int32_t memToAlloc , char keySize ,
		    char *allocName ) {
	// hold on to this so we know if dump is going on
	//m_dump = dump;
	m_rdb  = rdb;
	m_ks   = keySize;
	m_allocName = allocName;
	// return true if no mem
	if ( memToAlloc <= 0 ) return true;
	// get the initial mem
	m_mem = (char *) mmalloc ( memToAlloc , m_allocName );
	if ( ! m_mem ) 	return log("RdbMem::init: %s", mstrerror(g_errno));
	m_memSize = memToAlloc;
	// rush it into mem for real
	int32_t n = m_memSize / 4;
	for ( int32_t i = 0 ; i < n ; i++ ) ((int32_t *)m_mem)[i] = 0;
	// set up primary/secondary mem ptrs
	m_ptr1 = m_mem;
	// secondary mem initially grow downward
	m_ptr2 = m_mem + m_memSize;
	// . set our limit markers
	// . one for when primary mem, m_ptr1, is growing upward
	//   and the other for when it's growing downward
	int64_t limit = ((int64_t)m_memSize * 90LL) / 100LL;
	m_90up   = m_mem + limit;
	m_90down = m_mem + m_memSize - limit;
	// success
	return true;
}

// . if a dump is not going on this uses the primary mem space
// . if a dump is going on and this key has already been dumped
//   (we check RdbDump::getFirstKey()/getLastKey()) add it to the
//   secondary mem space, otherwise add it to the primary mem space
//void *RdbMem::dupData ( key_t key , char *data , int32_t dataSize ) {
void *RdbMem::dupData ( char *key , char *data , int32_t dataSize ,
			collnum_t collnum ) {
	char *s = (char *) allocData ( key , dataSize , collnum );
	if ( ! s ) return NULL;
	gbmemcpy ( s , data , dataSize );
	return s;
}

//void *RdbMem::allocData ( key_t key , int32_t dataSize ) {
void *RdbMem::allocData ( char *key , int32_t dataSize , collnum_t collnum ) {
	// if we're dumping and key has been dumped, use the secondary mem
	//if ( m_dump->isDumping() && key < m_dump->getLastKeyInQueue() ) {
	if ( m_rdb->m_inDumpLoop ) {
		/////
		// MDW: 3/15/2016
		// if we're dumping then ALWAYS use secondary mem, wtf...
		// primary is being dumped out and when the dump completes
		// the ptr gets reset so we'll end up point to garbage.
		///////
	     // ( collnum < m_rdb->m_dumpCollnum ||
	     //   (collnum == m_rdb->m_dumpCollnum &&
	     //	// if dump fails to alloc mem in RdbDump::dumpTree it does
	     //	// a sleep wrapper and keeps retrying, and 
	     //	// RdbDump::m_lastKeyInQueue can remain NULL because we've
	     // 	// never dumped out a list from the tree yet
	     // 	m_rdb->m_dump.m_lastKeyInQueue &&
	     //	KEYCMP(key,m_rdb->m_dump.getLastKeyInQueue(),m_ks)<0))){
		// if secondary mem is growing down...
		if ( m_ptr2 > m_ptr1 ) {
			// return NULL if it would breech,
			// don't allow ptrs to equal each other because
			// we know which way they're growing based on order
			if ( m_ptr2 - dataSize <= m_ptr1 ) return NULL;

		// debug why recs added during dump aren't going into
		// secondary mem
		// log("rdbmem: allocating %i bytes for rec in %s (cn=%i) "
		//     "ptr1=%"PTRFMT" --ptr2=%"PTRFMT" mem=%"PTRFMT,
		//     (int)dataSize,m_rdb->m_dbname,(int)collnum,
		//     (PTRTYPE)m_ptr1,(PTRTYPE)m_ptr2,(PTRTYPE)m_mem);


			// otherwise, grow downward
			m_ptr2 -= dataSize;
			// note it
			//if ( m_ks == 16 )
			//log("rdbmem: ptr2a=%"UINT32" size=%"INT32"",
			//    (int32_t)m_ptr2,dataSize);
			return m_ptr2;
		}
		// . if it's growing up...
		// . return NULL if it would breech
		if ( m_ptr2 + dataSize >= m_ptr1 ) return NULL;

		// debug why recs added during dump aren't going into
		// secondary mem
		// log("rdbmem: allocating %i bytes for rec in %s (cn=%i) "
		//     "ptr1=%"PTRFMT" ++ptr2=%"PTRFMT" mem=%"PTRFMT,
		//     (int)dataSize,m_rdb->m_dbname,(int)collnum,
		//     (PTRTYPE)m_ptr1,(PTRTYPE)m_ptr2,(PTRTYPE)m_mem);

		// otherwise, grow downward
		m_ptr2 += dataSize;
		// note it
		//if ( m_ks == 16 )
		//log("rdbmem: ptr2b=%"UINT32" size=%"INT32"",
		//    (int32_t)m_ptr2-dataSize,dataSize);
		return m_ptr2 - dataSize;
	}
	// . otherwise, use the primary mem
	// . if primary mem growing down...
	if ( m_ptr1 > m_ptr2 ) {
		// return NULL if it would breech
		if ( m_ptr1 - dataSize <= m_ptr2 ) return NULL;
		// otherwise, grow downward
		m_ptr1 -= dataSize;
		// are we at the 90% limit?
		if ( m_ptr1 < m_90down ) m_is90PercentFull = true;
		// note it
		//if ( m_ks == 16 )
		//log("rdbmem: ptr1a=%"UINT32" size=%"INT32"",(int32_t)m_ptr1,dataSize);
		// return the ptr
		return m_ptr1;
	}
	// . if it's growing up...
	// . return NULL if it would breech
	if ( m_ptr1 + dataSize >= m_ptr2 ) return NULL;
	// otherwise, grow upward
	m_ptr1 += dataSize;
	// are we at the 90% limit?
	if ( m_ptr1 > m_90up ) m_is90PercentFull = true;
	// note it
	//if ( m_ks == 16 )
	//log("rdbmem: ptr1b=%"UINT32" size=%"INT32"",(int32_t)m_ptr1-dataSize,dataSize);
	// return the ptr
	return m_ptr1 - dataSize;
}

#include "Threads.h"

// . when a dump completes we free the primary mem space and make
//   the secondary mem space the new primary mem space
void RdbMem::freeDumpedMem( RdbTree *tree ) {
	// bail if we have no mem
	if ( m_memSize == 0 ) return;

	log("rdbmem: start freeing dumped mem");

	//char *memEnd = m_mem + m_memSize;

	// this should still be true so allocData() returns m_ptr2 ptrs
	if ( ! m_rdb->m_inDumpLoop ) { char *xx=NULL;*xx=0; }

	// count how many data nodes we had to move to avoid corruption
	int32_t count = 0;
	int32_t scanned = 0;
	for ( int32_t i = 0 ; i < tree->m_minUnusedNode ; i++ ) {
		// give up control to handle search query stuff of niceness 0
		QUICKPOLL ( MAX_NICENESS );
		// skip node if parents is -2 (unoccupied)
		if ( tree->m_parents[i] == -2 ) continue;
		scanned++;
		// get the ptr
		char *data = tree->m_data[i];
		if ( ! data ) continue;
		// how could it's data not be stored in here?
		// if ( data < m_mem ) {
		// 	log("rdbmem: bad data1");
		// 	continue;
		// }
		// if ( data >= memEnd ) {
		// 	log("rdbmem: bad data2");
		// 	continue;
		// }
		// is it in primary mem? m_ptr1 mem was just dump
		// if growing upward
		bool needsMove = false;
		// if the primary mem (that was dumped) is
		// growing upwards
		if ( m_ptr1 < m_ptr2 ) {
		     // and the node data is in it...
			if ( data < m_ptr1 ) 
				needsMove = true;
		}
		// growing downward otherwise
		else if ( data >= m_ptr1 ) {
			needsMove = true;
		}
		if ( ! needsMove ) continue;
		// move it. m_inDumpLoop should still 
		// be true so we will get added to
		// m_ptr2
		int32_t size;
		if ( tree->m_sizes ) size = tree->m_sizes[i];
		else size = tree->m_fixedDataSize;
		if ( size < 0 ) { char *xx=NULL;*xx=0; }
		if ( size == 0 ) continue;
		// m_inDumpLoop is still true at this point so 
		// so allocData should return m_ptr2 guys
		char *newData = (char *)allocData(NULL,size,0);
		if ( ! newData ) {
			int32_t cn = 0;
			if ( tree->m_collnums ) cn = tree->m_collnums[i];
			log("rdbmem: failed to alloc %i "
			    "bytes node %i (cn=%i)",(int)size,(int)i,(int)cn);
			continue;
		}
		// debug test
		bool stillNeedsMove = false;
		if ( m_ptr1 < m_ptr2 ) {
		     // and the node data is in it...
			if ( newData < m_ptr1 ) 
				stillNeedsMove = true;
		}
		// growing downward otherwise
		else if ( newData >= m_ptr1 ) {
			stillNeedsMove = true;
		}
		if ( stillNeedsMove ) {// this should never happen!!
			log("rdbmem: olddata=0x%"PTRFMT" newdata=0x%"PTRFMT,
			    (PTRTYPE)data, (PTRTYPE)newData);
			log("rdbmem: still needs move!");
		}
		count++;
		gbmemcpy(newData,data,size);
		tree->m_data[i] = newData;
	}
	if ( count > 0 )
		log("rdbmem: moved %i tree nodes for %s",(int)count,
		    m_rdb->m_dbname);
				
	log("rdbmem: stop freeing dumped mem. scanned %i nodes.",(int)scanned);

	// save primary ptr
	char *tmp = m_ptr1;
	// debug
	//logf(LOG_DEBUG,
	//     "db: freeing dumped mem ptr1=%"XINT32" ptr2=%"XINT32".",m_ptr1,m_ptr2);
	// primary pointer, m_ptr1, becomes m_ptr2
	m_ptr1 = m_ptr2;
	// secondary ptr becomes primary
	m_ptr2 = tmp;
	// reset secondary (old primary mem was dumped out to disk)
	if ( m_ptr2 > m_ptr1 ) m_ptr2  = m_mem + m_memSize;
	else                   m_ptr2  = m_mem;
	// no longer 90% full
	m_is90PercentFull = false;
}
