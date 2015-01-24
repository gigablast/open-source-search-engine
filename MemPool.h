// Matt Wells, Copyright Nov 2002

// . provides memory management over a fixed-size pool of memory
// . uses best-fit algorithm (greedy) to minimize memory fragmentation
// . meant for use in RdbCache for caching little things
// . UdpServer can now use this since it's HOT, when it's handling an unblocked
//   real time signal to read a reply of unknown length it can malloc here
// . has overhead of 2*sizeof(MemNode) = 2*28 = 54 = very high, but, hey,
//   it's best fit, i've seen memory fragmentation take like 10x the space
//   it should have taken!! This way we are guaranteed to be more efficient.

#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_

#include "MemPoolTree.h"
#ifdef DMALLOC
#include <dmalloc.h>
#endif
class MemPool {

 public:

	MemPool();
	~MemPool();
	void reset();

	// . allocates the memory pool
	// . returns false and sets errno on error
	bool init ( int32_t maxMem );

	// all of these return NULL and set errno on error
	void *gbmalloc  ( int32_t  size );
	void *gbcalloc  ( int32_t  size );
	void *dup     ( void *ptr  , int32_t size );
	void *gbrealloc ( void *ptr  , int32_t newSize );

	// free a mem slot
	bool gbfree ( void *data );

	bool isInPool ( char *data ) {
		if ( data < m_mem ) return false;
		return ( data < m_mem + m_memSize ); };

	bool isInitialized ( ) { if ( m_mem ) return true; return false; };

	// by data and MemNode classes
	uint32_t getUsedMem ( ) {
		return m_mem +m_memSize -m_tree.getFloor() +m_memUsedByData;};

	uint32_t getAvailMem ( ) {	return m_memSize - getUsedMem(); };

	// the whole pool allocated using ::malloc()
	char *m_mem;
	// size of pool
	int32_t  m_memSize;
	// top of data usage
	char *m_top;
	// . some slots are "size" slots some are "offset" slots
	// . if sorted by size then it's a size slot, otherwise offset slot
	MemPoolTree m_tree;

	uint32_t m_memUsedByData;
};

#endif
