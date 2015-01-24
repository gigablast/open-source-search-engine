#include "gb-include.h"

#include "MemPool.h"
#include "Mem.h"
#include "Errno.h"

MemPool::MemPool() {
	m_mem     = NULL;
	m_memSize = 0;
	m_top     = NULL;
	m_memUsedByData = 0;
}

MemPool::~MemPool ( ) {
	log("MemPool::~MemPool: mem alloced now: %"INT32"\n", m_memUsedByData );
	reset();
}

void MemPool::reset ( ) {
	if ( m_mem ) mfree ( m_mem , m_memSize , "MemPool" );
	m_mem     = NULL;
	m_memSize = 0;
}

bool MemPool::init ( int32_t maxMem  ) {
	// get a hunk of mem
	m_mem = (char *) ::malloc ( maxMem );
	if ( ! m_mem ) return log("MemPool::init: %s",mstrerror(g_errno));
	// assign m_memSize
	m_memSize = maxMem;
	// . init the tree
	// . each piece of memory is defined by 2 MemNodes in this tree
	if ( ! m_tree.init ( m_mem , m_memSize ) ) {
		// free mem we allocated
		mfree ( m_mem , m_memSize , "MemPool" );
		m_mem     = NULL;
		m_memSize = 0;
		// bitch and return false
		return log ("MemPool::init: tree: %s",mstrerror(g_errno));
	}
	// adjust memory chunk size to hold our initial MemNodes at the top
	int32_t size = m_memSize - sizeof(MemNode) * 2;
	// make size-ranked key of offset m_mem and size m_memSize to 
	// christian this chunk of memory
	if ( ! m_tree.addSizeNode ( size , m_mem , false ) )
		return log("MemPool::init: %s",mstrerror(g_errno));
	// add offset-ranked key too
	if ( ! m_tree.addOffsetNode ( m_mem , size , false ) )
		return log("MemPool::init: %s",mstrerror(g_errno));
	return true;
}

void *MemPool::dup ( void *data , int32_t dataSize ) {
	char *p = (char *)malloc ( dataSize );
	// async signal safe copy
	if ( p ) memcpy_ass ( p , (char *)data , dataSize );
	return p;
}

void *MemPool::gbcalloc ( int32_t size ) {
	char *p = (char *)malloc ( size );
	if ( ! p ) return NULL;
	for ( int32_t i = 0 ; i < size ; i++ ) p[i] = 0;
	return p;

}

void *MemPool::gbrealloc ( void *ptr , int32_t newSize ) {
	// find this node in our tree
	MemKey k;
	k.setOffsetKey ( ptr , newSize , true );
	MemNode *node = m_tree.getNode ( k );
	// it's gotta be there
	if ( ! node ) { 
		g_errno = EBADENGINEER; 
		log("MemPool::realloc: bad engineer");
		return NULL; 
	}
	// what's the original size and ptr?
	int32_t  size = node->m_key.getSize   ();
	// just use malloc for now
	char *mem = (char *)malloc ( newSize );
	if ( ! mem ) return NULL;
	// how much to copy?
	int32_t toCopy = size;
	// shrink if too much
	if ( toCopy > newSize ) toCopy = newSize;
	// async signal safe copy
	memcpy_ass ( mem , (char *)ptr , toCopy );
	// free old one
	free ( ptr );
	return mem;
}

void *MemPool::gbmalloc ( int32_t need ) {
	// make size key
	MemKey k;  
	k.setSizeKey ( need , NULL , false );
	// . find first slot pointing of this size or higher not in use
	// . this is essentially the best-fit algorithm
	MemNode *node = m_tree.getNextNode ( k );
	// according to bitmap of key may return one in use, so check that
	if ( node && node->m_key.inUse() ) node = NULL;
	// if no node, that's any error, buddy
	if ( ! node ) { 
		g_errno = ENOMEM; 
		log("MemPool::malloc: no memory");
		return NULL; 
	}
	// get the size of this slot
	int32_t oldSize = node->m_key.getSize() ;
	// get the mem offset into m_mem
	char *ptr = node->m_key.getOffset();
	// the new size
	int32_t size = oldSize;
	// . get the memory floor
	// . the memory above the floor is storing MemNodes
	// . we may lower the floor by 2 MemNodes on success
	char *floor = m_tree.getFloor();
	// how many nodes we have to add that will require lowering m_floor?
	int32_t extra = 4 - m_tree.getNumEmptyNodesAboveFloor ();
	// if size just right that's nice
	//if ( size == need ) goto exactFit;
	// lower it to accomodate up to 4 new MemNodes if we're maxed out now
	if ( extra > 0 ) floor -= sizeof(MemNode) * extra ;
	// if another node has data in this region, bail
	if ( m_top > floor ) { 
		g_errno = ENOMEM; 
		log("MemPool::malloc: no memory");
		return NULL; 
	}
	// does the node we are splitting come up to the floor?
	if ( ptr + size >= floor ) {
		// how much do we have to decrease the slotSize?
		int32_t dec = ptr + size - floor;
		// remove some if it's memory
		size -= dec;
		// if too much, we fail
		if ( size < need ) { 
			g_errno = ENOMEM; 
			log("MemPool::malloc: no memory");
			return NULL; 
		}
	}
	// split node into 2 halves, each half has 2 nodes actually
	//log("addOffset/Size ptr=%"INT32" size=%"INT32"",(int32_t)ptr,need);
	MemNode *n0 = m_tree.addOffsetNode ( ptr  , need , true  );
	MemNode *n1 = m_tree.addSizeNode   ( need , ptr  , true  );
	// add upper, unused half
	//log("addOffset/Size ptr=%"INT32" size=%"INT32"",(int32_t)ptr+need,size-need);
	MemNode *n2 = m_tree.addOffsetNode ( ptr + need , size - need, false );
	MemNode *n3 = m_tree.addSizeNode   ( size - need, ptr + need , false );
	// remove all on any errors
	if ( ! n0 || ! n1 || ! n2 || ! n3 ) {
		m_tree.deleteNode ( n0 );
		m_tree.deleteNode ( n1 );
		m_tree.deleteNode ( n2 );
		m_tree.deleteNode ( n3 );
		return NULL;
	}
	// boost m_top
	if ( ptr + need > m_top ) m_top = ptr + need;
	// Sanity check, if new floor breeches, that's a bad engineer
	if ( m_tree.getFloor() < m_top ) {
		g_errno = EBADENGINEER;
		log("MemPool::malloc: bad engineer");
		return NULL;
	}
	// keep track
	m_memUsedByData += need;
	// remove old node from size-sorted tree
	//log("removeSize/Offset ptr=%"INT32" size=%"INT32"",(int32_t)ptr, oldSize);
	m_tree.deleteNode ( node );
	// remove old node from offset-sorted tree
	k.setOffsetKey ( ptr , oldSize , false );
	m_tree.deleteNode ( k );
	return ptr;
}

bool MemPool::gbfree ( void *data ) {
	// if we have no mem, just bail
	if ( ! m_mem ) return true;
	// make a key based on offset
	MemKey k ; 
	k.setOffsetKey ( data , 0 , true ); // in use?
	// find in the offset subtree
	MemNode *n = m_tree.getNextNode ( k );
	// if not found it's an error
	if ( ! n ) return log("MemPool::free: illegal free.");
	// assume this key
	k = n->m_key;
	// get our data ptr offset
	char *ptr  = k.getOffset ( );
	int32_t  size = k.getSize   ( );
	// must equal
	if ( ptr != data )
		return log("MemPool::free: illegal free");
	// size should always be > 0
	if ( size <= 0 )
		return log("MemPool::free: size <= 0");
	// must be in use
	if ( ! k.inUse() )
		return log("MemPool::free: node not in use");
	// keep track
	m_memUsedByData -= size;
	// get the node before us in the offset-sorted subtree that is in use
	k -= (uint32_t) 1;
	MemNode *n0a = m_tree.getPrevNode ( k );
	// watch out for wrap around
	if ( n0a && ! n0a->m_key.inUse() ) n0a = NULL;
	// set the inUse bit to false so we limit to checking mem NOT in use
	k.setInUse ( false );
	MemNode *n0b = m_tree.getPrevNode ( k );
	// watch out for wrap around
	if ( n0b && n0b->m_key.inUse() ) n0b = NULL;
	// get the one with biggest offset
	MemNode *n0;
	if ( n0a && n0b ) {
		if ( n0a->m_key.getOffset() > n0b->m_key.getOffset() )
			n0 = n0a;
		else    n0 = n0b;
	}
	else if ( n0a ) n0 = n0a;
	else            n0 = n0b;
	// get info from n0, if not null
	int32_t     size0 ;
	char    *ptr0 ;
	bool     occupied0 = true ;
	if ( n0 ) {
		size0     = n0->m_key.getSize    ( );
		ptr0      = n0->m_key.getOffset  ( );
 		occupied0 = n0->m_key.inUse      ( );
		//log("prev node ptr=%"INT32" size=%"INT32" occ=%"INT32"",
		//    (int32_t)ptr0,size0,(int32_t)occupied0);
	}

	// get the node after us in the offset-sorted subtree that is NOT used
	k += (uint32_t) 1;
	k += (uint32_t) 1;
	MemNode *n1a = m_tree.getNextNode ( k );
	// watch out for wrap around
	if ( n1a && n1a->m_key.inUse() ) n1a = NULL;
	// set the inUse bit to true so we limit to checking mem IN use
	k.setInUse ( true );
	MemNode *n1b = m_tree.getPrevNode ( k );
	// watch out for wrap around
	if ( n1b && ! n1b->m_key.inUse() ) n1b = NULL;
	// get the one with biggest offset
	MemNode *n1;
	if ( n1a && n1b ) {
		if ( n1a->m_key.getOffset() < n1b->m_key.getOffset() )
			n1 = n1a;
		else    n1 = n1b;
	}
	else if ( n1a ) n1 = n1a;
	else            n1 = n1b;

	// extract info from the smallest ptr'ed node
	int32_t     size1 ;
	char    *ptr1;
	bool     occupied1 = true;
	if ( n1 ) {
		size1     = n1->m_key.getSize   ( );
		ptr1      = n1->m_key.getOffset ( );
		occupied1 = n1->m_key.inUse     ( );
		//log("post node ptr=%"INT32" size=%"INT32" occ=%"INT32"",
		//    (int32_t)ptr1,size1,(int32_t)occupied1);
	}
	// debug msg
	//log ("free rm ptr=%"INT32" size=%"INT32"", (int32_t)ptr , size );

	// remove node from both subtrees
	m_tree.deleteNode     ( n );
	m_tree.deleteSizeNode ( size , ptr , true );

	char *newPtr ;
	int32_t  newSize ;

	// if nodes before and after us were not occupied, join altogether
	if ( ! occupied0 && ! occupied1 ) {
		// delete old nodes
		m_tree.deleteNode     ( n0 );
		m_tree.deleteNode     ( n1 );
		m_tree.deleteSizeNode ( size0 , ptr0 , false );
		m_tree.deleteSizeNode ( size1 , ptr1 , false );
		newPtr  = ptr0;
		newSize = size + size0 + size1;
	}
	// if only n0 was free then join with him
	else if ( ! occupied0 ) {
		// combine nodes from the offset-sorted tree
		m_tree.deleteNode ( n0 );
		m_tree.deleteSizeNode ( size0 , ptr0 , false );
		newPtr  = ptr0;
		newSize = size + size0 ;
	}
	// if only n1 was free then join with him
	else if ( ! occupied1 ) {
		// combine nodes from the offset-sorted tree
		m_tree.deleteNode ( n1 );
		m_tree.deleteSizeNode ( size1 , ptr1 , false );
		newPtr  = ptr;
		newSize = size + size1 ;
	}
	// if nobody free, oh well
	else {
		newPtr = ptr;
		newSize = size;
	}

	// may lower m_top
	if ( newPtr + newSize == m_top ) m_top = newPtr;

	// debug msg
	//log("combined node ptr=%"INT32" size=%"INT32"",(int32_t)newPtr,newSize);

	// add the new combined offset node
	if ( ! m_tree.addOffsetNode ( newPtr , newSize , false ) )
		return log("MemPool::free: critical error");
	// and add the new combined size node
	if ( m_tree.addSizeNode   ( newSize , newPtr , false ) ) return true;
	// bitch about error
	m_tree.deleteOffsetNode ( newPtr , newSize , false );
	return log("MemPool::free: critical error");
}
