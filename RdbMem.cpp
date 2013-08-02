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

/*
#include <asm/page.h> // PAGE_SIZE

// return #of bytes scanned for timing purposes
long RdbMem::scanMem ( ) {
	// ahh.. just scan the whole thing to keep it simple
	char *p    = m_mem + 64 ;
	char *pend = m_mem + m_memSize;
	char  c;
	while ( p < pend ) { c = *p; p += PAGE_SIZE; }
	return m_memSize;
}
*/

// initialize us with the RdbDump class your rdb is using
bool RdbMem::init ( Rdb *rdb , long memToAlloc , char keySize ,
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
	long n = m_memSize / 4;
	for ( long i = 0 ; i < n ; i++ ) ((long *)m_mem)[i] = 0;
	// set up primary/secondary mem ptrs
	m_ptr1 = m_mem;
	// secondary mem initially grow downward
	m_ptr2 = m_mem + m_memSize;
	// . set our limit markers
	// . one for when primary mem, m_ptr1, is growing upward
	//   and the other for when it's growing downward
	long long limit = ((long long)m_memSize * 90LL) / 100LL;
	m_90up   = m_mem + limit;
	m_90down = m_mem + m_memSize - limit;
	// success
	return true;
}

// . if a dump is not going on this uses the primary mem space
// . if a dump is going on and this key has already been dumped
//   (we check RdbDump::getFirstKey()/getLastKey()) add it to the
//   secondary mem space, otherwise add it to the primary mem space
//void *RdbMem::dupData ( key_t key , char *data , long dataSize ) {
void *RdbMem::dupData ( char *key , char *data , long dataSize ,
			collnum_t collnum ) {
	char *s = (char *) allocData ( key , dataSize , collnum );
	if ( ! s ) return NULL;
	memcpy ( s , data , dataSize );
	return s;
}

//void *RdbMem::allocData ( key_t key , long dataSize ) {
void *RdbMem::allocData ( char *key , long dataSize , collnum_t collnum ) {
	// if we're dumping and key has been dumped, use the secondary mem
	//if ( m_dump->isDumping() && key < m_dump->getLastKeyInQueue() ) {
	if ( m_rdb->m_inDumpLoop && // m_dump->isDumping() && 
	     ( collnum < m_rdb->m_dumpCollnum ||
	       (collnum == m_rdb->m_dumpCollnum &&
		// if dump fails to alloc mem in RdbDump::dumpTree it does
		// a sleep wrapper and keeps retrying, and 
		// RdbDump::m_lastKeyInQueue can remain NULL because we've
		// never dumped out a list from the tree yet
		m_rdb->m_dump.m_lastKeyInQueue &&
		KEYCMP(key,m_rdb->m_dump.getLastKeyInQueue(),m_ks)<0)) ){
		// if secondary mem is growing down...
		if ( m_ptr2 > m_ptr1 ) {
			// return NULL if it would breech,
			// don't allow ptrs to equal each other because
			// we know which way they're growing based on order
			if ( m_ptr2 - dataSize <= m_ptr1 ) return NULL;
			// otherwise, grow downward
			m_ptr2 -= dataSize;
			return m_ptr2;
		}
		// . if it's growing up...
		// . return NULL if it would breech
		if ( m_ptr2 + dataSize >= m_ptr1 ) return NULL;
		// otherwise, grow downward
		m_ptr2 += dataSize;
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
	// return the ptr
	return m_ptr1 - dataSize;
}

// . when a dump completes we free the primary mem space and make
//   the secondary mem space the new primary mem space
void RdbMem::freeDumpedMem() {
	// bail if we have no mem
	if ( m_memSize == 0 ) return;
	// save primary ptr
	char *tmp = m_ptr1;
	// debug
	//logf(LOG_DEBUG,
	//     "db: freeing dumped mem ptr1=%lx ptr2=%lx.",m_ptr1,m_ptr2);
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
