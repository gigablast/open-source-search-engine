/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * July 2007
 * Author: Joel Edwards
 *  
 * SafeList is a template class providing an pooled, offset based 
 * doubly linked list. Effectively, all nodes are pulled from a
 * common pool of memory or "slab" that is automatically freed when
 * the destructor is called.
 *
 * !!WARNING!!
 * If you use pointers to allocated memory as your payloads for
 * the list nodes, it is your responsibility to free them! SafeList
 * will not free space you have allocated, only the space required
 * for its own nodes.
 *
 * All data addition and extraction is done via assignment operator
 * '=', so any classes passed in as the Type for the template must
 * have an overloaded '=' operator. It stores a copy of the data
 * at the reference you provide. If you do not wish the data to be
 * duplicated, you may pass in a pointer/offset rather than the
 * object itself. This is adviseable for very large objects. 
 *
 * TODO:
 * . Create two mutator functions that take arguments
 *   to specify locations of node additions and extractions.
 *   Wrap existing functions around these.
 *   
 *   Protected Functions:
 *
 *	uint32_t getNext( uint32_t current ) 
 *	uint32_t getPrev( uint32_t current )
 *	class SLNode<Type> *getP( uint32_t offset )
 *	Type *getPayloadP( uint32_t offset )
 *
 *   Public Functions:
 *
 *	SafeList()
 *	SafeList( uint32_t numNodes )
 *	~SafeList()
 *	bool add( Type &payload, bool (*compare)(Type &a1, Type &a2) )
 *	bool pushHead( Type &payload )
 *	bool pushTail( Type &payload )
 *	bool popHead( Type &payload )
 *	bool popTail( Type &payload )
 *	bool getDataAt( SLIterator<Type> &iter, Type &payload )
 *	bool getHead( Type &payload )
 *	bool getTail( Type &payload )
 *	bool pull( SLIterator<Type> &iter, Type &payload )
 *	Type *sample( SLIterator<Type> &iter )
 *	Type *sampleHead()
 *	Type *sampleTail()
 *	bool sort( bool (*compare)( Type &a1, Type &a2 ) )
 *	bool setIterator( SLIterator<Type> &iter )
 *	void setIteratorHead( SLIterator<Type> &iter )
 *	void setIteratorTail( SLIterator<Type> &iter )
 *	bool incIterator( SLIterator<Type> &iter )
 *	bool decIterator( SLIterator<Type> &iter )
 *	bool request( uint32_t numNodes )
 *	bool rebalance( bool fast = true )
 *	void setCap( uint32_t numNodes )
 *	void clearCap()
 *	void printNodes()
 *	void printListStatus( )
 *	bool isEmpty()
 *	uint32_t size()
 *
 */

#ifndef _SAFE_LIST_H_
#define _SAFE_LIST_H_

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

template<typename Type> class SLIterator;
template<typename Type> class SLNode;
template<typename Type> class SLData;

template<typename Type> class SafeList {
 protected:
 	class SLData<Type> m_usedNodes;
	class SLData<Type> m_freeNodes;

	uint8_t *m_list;
	uint32_t m_listAlloc;
	uint32_t m_listSlab;
	uint32_t m_cap;

	uint32_t getNext( uint32_t current ) {
		class SLNode<Type> *tmpNode = getP(current);
		if ( !tmpNode )
			return 0;
		return tmpNode->m_next;
	}

	uint32_t getPrev( uint32_t current ) {
		class SLNode<Type> *tmpNode = getP(current);
		if ( !tmpNode )
			return 0;
		return tmpNode->m_prev;
	}

	class SLNode<Type> *getP( uint32_t offset ) {
		if ( !offset || !m_list )
			return NULL;
		if ( offset > (m_listAlloc - sizeof(class SLNode<Type>)) )
			return NULL;
		class SLNode<Type> *tmpNode = (class SLNode<Type> *)
					      (m_list + offset);
		return tmpNode;
	}

	Type *getPayloadP( uint32_t offset ) {
		class SLNode<Type> *node = getP( offset );
		if ( !node )
			return NULL;
		if ( !node->m_used )
			return NULL;
		return &node->m_payload;
	}

 public:
	// Create a new empty node, and add it to the head of the list.
	// If Type is a class you must call its constructor manually.
	// TODO:
	// . Impliment this function
	//bool newNode( SLIterator<Type> &iter );

	// We don't sort
	bool sort( bool (*compare)( Type &a1, Type &a2 ) ) {
		return false;
	};

	SafeList() {
		// Use SafeList64 for 64-bit architectures
		assert( sizeof(int32_t) == 4 );
		m_list 		   = NULL;
		m_listAlloc	   = 0;
		m_listSlab	   = 0;
		m_usedNodes.m_head = 0;
		m_usedNodes.m_tail = 0; 
		m_usedNodes.m_size = 0;
		m_freeNodes.m_head = 0;
		m_freeNodes.m_tail = 0; 
		m_freeNodes.m_size = 0;
		m_cap              = 0xffffffff;
	};


	SafeList( uint32_t numNodes ) {
		// Use SafeList64 for 64-bit architectures
		assert( sizeof(int32_t) == 4 );
		m_list 		   = NULL;
		m_listAlloc	   = 0;
		m_listSlab	   = 0;
		m_usedNodes.m_head = 0;
		m_usedNodes.m_tail = 0; 
		m_usedNodes.m_size = 0;
		m_freeNodes.m_head = 0;
		m_freeNodes.m_tail = 0; 
		m_freeNodes.m_size = 0;
		m_cap              = 0xffffffff;
		// Attempt to set up the new list.
		request( numNodes );
	};

	~SafeList() {
		if ( m_list )
#ifdef _SL_TEST_ // If SafeList is being tested outside of gb
			free( m_list );
#else		
			mfree( m_list, m_listAlloc, "SafeList" );
#endif
	};

	// emptyList() effectively clears out all data from the list.
	// Allocated space is not freed.
	void emptyList() {
		m_listSlab = 0;
		if ( m_list ) m_listSlab = 1;
		m_usedNodes.m_head = 0;
		m_usedNodes.m_tail = 0; 
		m_usedNodes.m_size = 0;
		m_freeNodes.m_head = 0;
		m_freeNodes.m_tail = 0; 
		m_freeNodes.m_size = 0;
	}

	// purge() completely resets the list, freeing all used
	// memory, and reseting all variables.
	void purge() {
		m_usedNodes.m_head = 0;
		m_usedNodes.m_tail = 0; 
		m_usedNodes.m_size = 0;
		m_freeNodes.m_head = 0;
		m_freeNodes.m_tail = 0; 
		m_freeNodes.m_size = 0;
		if ( m_list )
#ifdef _SL_TEST_ // If SafeList is being tested outside of gb
			free( m_list );
#else
			mfree( m_list, m_listAlloc, "SafeList" );
#endif
		m_list = NULL;
		m_listAlloc = 0;
		m_listSlab = 0;
		m_cap = 0xffffffff;
	}

	bool duplicate( SafeList<Type> &list ) {
		purge();
#ifdef _SL_TEST_ // If SafeList is being tested outside of gb
		m_list = (uint8_t *)malloc( list.m_listAlloc );
#else
		m_list = (uint8_t *)mmalloc( list.m_listAlloc, "SafeList" );
#endif
		if ( !m_list )
			return false;
		gbmemcpy( m_list, list.m_list, list.m_listAlloc );
		m_listAlloc	   = list.m_listAlloc;
		m_listSlab	   = list.m_listSlab;
		m_usedNodes.m_head = list.m_usedNodes.m_head;
		m_usedNodes.m_tail = list.m_usedNodes.m_tail; 
		m_usedNodes.m_size = list.m_usedNodes.m_size;
		m_freeNodes.m_head = list.m_freeNodes.m_head;
		m_freeNodes.m_tail = list.m_freeNodes.m_tail; 
		m_freeNodes.m_size = list.m_freeNodes.m_size;
		m_cap              = list.m_cap;
		return true;
	}

	bool exists( Type &item ) {
		Type temp;
		SLIterator<Type> iter;
		iter.associate( *this );
		do {
			iter.getCurrent( temp );
			if ( temp == item )
				return true;
		} while ( iter++ );
		return false;
	}

	// If you are going to use add() for insertions, it should
	// always be used in order to keep the list sorted.
	bool add( Type &payload, bool (*compare)(Type &a1, Type &a2) ) {
		if ( m_cap <= m_usedNodes.m_size )
			return false;
		// We don't need to worry about head and tail manipulations
		// if we can ensure that the node won't end up there.
		uint32_t existingOff = 0;
		class SLNode<Type> *existing = NULL;
		bool success = false;
		if ( m_usedNodes.m_size == 0 ) {
			return pushHead( payload );
		}
		else if ( m_usedNodes.m_size == 1 ) {
			existing = getP( m_usedNodes.m_head );
			if ( !existing )
				return success;
			if ( compare( payload, existing->m_payload ) )
				return pushHead( payload );
			return pushTail( payload );
		}
		else {
			existing = getP( m_usedNodes.m_head );
			if ( !existing )
				return success;
			if ( !compare( existing->m_payload, payload ) )
				return pushHead( payload );
			existing = getP( m_usedNodes.m_tail );
			if ( !existing )
				return success;
			if ( !compare( payload, existing->m_payload ) )
				return pushTail( payload );
		}
		if ( !request( 1 ) ) {
			return success;
		}
		uint32_t tempNodeOff = 0;
		uint32_t newFreeOff  = 0;
		class SLNode<Type> *tempNode    = NULL;
		class SLNode<Type> *tempNode2   = NULL;
		class SLNode<Type> *newFreeHead = NULL;
		// If we have nodes on the free list, reuse one
		if ( m_freeNodes.m_size > 0 ) {
			tempNodeOff = m_freeNodes.m_head;
			tempNode    = getP( tempNodeOff );
			if ( !tempNode )
				return success;
			m_freeNodes.m_size--;
			// If we are taking the only remaining node
			// in the free list, it is now empty.
			if ( m_freeNodes.m_size == 0 ) {
				m_freeNodes.m_head = 0;
				m_freeNodes.m_tail = 0;
			}
			else {
				newFreeOff = tempNode->m_next;
				m_freeNodes.m_head = newFreeOff;
				// If there are more free nodes, set the new
				// head node for the free list.
				newFreeHead = getP( newFreeOff );
				if ( newFreeHead ) {
					newFreeHead->m_prev = 0;
				}
			}
		}
		// Otherwise we get a new node from the slab
		else {
			 tempNodeOff  = m_listSlab;	
			 tempNode     = getP( tempNodeOff );
			 if ( !tempNode )
				 return success;
			 m_listSlab  += sizeof(class SLNode<Type>);
		}

		bool done = false;
		existingOff = getNext( m_usedNodes.m_head );
		while ( !done ) {
			existing = getP( existingOff );
			if ( !compare( existing->m_payload, payload ) ) {
				// . existing is pointing to our next neighbor 
				// . tempNod2 is pointing to our previous
				//   neighbor
				tempNode2 = getP( existing->m_prev );
				tempNode2->m_next = tempNodeOff;
				tempNode->m_prev  = existing->m_prev;
				tempNode->m_next  = existingOff;
				existing->m_prev  = tempNodeOff;

				tempNode->m_payload = payload;
				tempNode->m_used = 1;
				m_usedNodes.m_size++;
				success = true;
				break;
			}
			if ( existingOff == m_usedNodes.m_tail )
				done = true;
			existingOff = getNext( existingOff );
		}

		return success;
	};

	bool pushHead( Type &payload ) {
		if ( m_cap <= m_usedNodes.m_size )
			return false;
		// If we are out of memory, we cannot add a new node.
		if ( !request( 1 ) ) {
			return false;
		}
		uint32_t tempNodeOff = 0;
		uint32_t newFreeOff  = 0;
		uint32_t oldUsedOff  = 0;
		class SLNode<Type> *tempNode    = NULL;
		class SLNode<Type> *newFreeHead = NULL;
		class SLNode<Type> *oldUsedHead = NULL;

		// If there are nodes in the free list, use them
		if ( m_freeNodes.m_size > 0 ) {
			tempNodeOff = m_freeNodes.m_head;
			tempNode    = getP( tempNodeOff );
			if ( !tempNode )
				return false;
			m_freeNodes.m_size--;
			// If we are taking the only remaining node in
			// the free list, it is now empty.
			if ( m_freeNodes.m_size == 0 ) {
				m_freeNodes.m_head = 0;
				m_freeNodes.m_tail = 0;
			}
			else {
				newFreeOff = getNext( tempNodeOff );
				m_freeNodes.m_head = newFreeOff;
				// If there are more free nodes, set the new
				// head node for the free list.
				if ( newFreeOff ) {
					newFreeHead = getP( newFreeOff );
					newFreeHead->m_prev = 0;
				}
			}
		}
		// Otherwise we are out of free nodes, and we use the slab.
		else {
			 tempNodeOff  = m_listSlab;	
			 tempNode     = getP( tempNodeOff );
			 if ( !tempNode )
				 return false;
			 m_listSlab  += sizeof(class SLNode<Type>);
		}

		oldUsedOff = m_usedNodes.m_head;
		// If are nodes in the used list, upate its old head node.
		if ( oldUsedOff ) {
			oldUsedHead = getP( oldUsedOff );
			oldUsedHead->m_prev = tempNodeOff;
		}
		// Set up the new node.
		tempNode->m_next    = oldUsedOff;
		tempNode->m_prev    = 0;
		tempNode->m_payload = payload;
		tempNode->m_used    = 1;
		// If the used list was empty, we are now the only node in it.
		if ( m_usedNodes.m_tail == 0 )
			m_usedNodes.m_tail = tempNodeOff;
		m_usedNodes.m_head = tempNodeOff;
		m_usedNodes.m_size++;

		return true;
	};

	bool pushTail( Type &payload ) {
		if ( m_cap <= m_usedNodes.m_size )
			return false;
		// If we are out of memory, we cannot add a new node.
		if ( !request( 1 ) ) {
			return false;
		}
		uint32_t tempNodeOff = 0;
		uint32_t newFreeOff  = 0;
		uint32_t oldUsedOff  = 0;
		class SLNode<Type> *tempNode    = NULL;
		class SLNode<Type> *newFreeTail = NULL;
		class SLNode<Type> *oldUsedTail = NULL;

		// If there are nodes in the free list, use them
		if ( m_freeNodes.m_size > 0 ) {
			tempNodeOff = m_freeNodes.m_tail;
			tempNode    = getP( tempNodeOff );
			if ( !tempNode ) 
				return false;
			m_freeNodes.m_size--;
			// If we are taking the only remaining node
			// in the free list, it is now empty.
			if ( m_freeNodes.m_size == 0 ) {
				m_freeNodes.m_tail = 0;
				m_freeNodes.m_head = 0;
			}
			else {
				newFreeOff = getPrev( tempNodeOff );
				m_freeNodes.m_tail = newFreeOff;
				// If there are more free nodes, set the new
				// head node for the free list.
				if ( newFreeOff ) {
					newFreeTail = getP( newFreeOff );
					newFreeTail->m_next = 0;
				}
			}
		}
		// Otherwise, pull a new node from the slab
		else {
			 tempNodeOff  = m_listSlab;	
			 tempNode     = getP( tempNodeOff );
			 if ( !tempNode )
				 return false;
			 m_listSlab  += sizeof(class SLNode<Type>);
		}

		oldUsedOff = m_usedNodes.m_tail;
		// If are nodes in the used list, upate its old head node.
		if ( oldUsedOff ) {
			oldUsedTail = getP( oldUsedOff );
			oldUsedTail->m_next = tempNodeOff;
		}
		// Set up the new node.
		tempNode->m_prev    = oldUsedOff;
		tempNode->m_next    = 0;
		tempNode->m_payload = payload;
		tempNode->m_used    = 1;
		// If the used list was empty, we are now the only node in it.
		if ( m_usedNodes.m_head == 0 )
			m_usedNodes.m_head = tempNodeOff;
		m_usedNodes.m_tail = tempNodeOff;
		m_usedNodes.m_size++;

		return true;
	};

	bool popHead( Type &payload ) {
		// If we have no used nodes left, there is nothing to return.
		if ( m_usedNodes.m_size == 0 ) {
			return false;
		}
		uint32_t tempNodeOff = m_usedNodes.m_head;
		uint32_t newUsedOff  = 0;
		uint32_t oldFreeOff  = 0;
		class SLNode<Type> *tempNode    = NULL;
		class SLNode<Type> *newUsedHead = NULL;
		class SLNode<Type> *oldFreeHead = NULL;

		// If the head node is null, the list is empty.
		// This should never happen if we are keeping track of
		// our sizes correctly.
		tempNode = getP( tempNodeOff );
		if ( !tempNode ) {
			return false;
		}

		// Store the offsets of the used list's new head node and the
		// free list's current head node.
		newUsedOff = tempNode->m_next;
		oldFreeOff = m_freeNodes.m_head;

		// If there was a head node for the free list, set its previous
		// to the node we are deleting.
		oldFreeHead = getP( oldFreeOff );
		if ( oldFreeHead ) {
			oldFreeHead->m_prev = tempNodeOff;
		}
		// If there was a node after the node being deleted, set its
		// previous to null because it is the new head node.
		newUsedHead = getP( newUsedOff );
		if ( newUsedHead ) {
			newUsedHead->m_prev = 0;
		}
		// Update the deleted node.
		tempNode->m_next    = oldFreeOff;
		tempNode->m_prev    = 0;
		tempNode->m_used    = 0;
		payload  	    = tempNode->m_payload;
		// If we are deleting the only node in the used list,
		// it is now empty.
		if ( m_usedNodes.m_tail == tempNodeOff )
			m_usedNodes.m_tail = 0;
		m_usedNodes.m_head = newUsedOff;
		m_usedNodes.m_size--;
		// If the free list was empty, we are now the only node in it.
		if ( m_freeNodes.m_tail == 0 )
			m_freeNodes.m_tail = tempNodeOff;
		m_freeNodes.m_head = tempNodeOff;
		m_freeNodes.m_size++;

		return true;
	};

	bool popTail( Type &payload ) {
		// If we have no used nodes left, there is nothing to return.
		if ( m_usedNodes.m_size == 0 ) {
			return false;
		}
		uint32_t tempNodeOff = m_usedNodes.m_tail;
		uint32_t newUsedOff  = 0;
		uint32_t oldFreeOff  = 0;
		class SLNode<Type> *tempNode    = NULL;
		class SLNode<Type> *newUsedTail = NULL;
		class SLNode<Type> *oldFreeTail = NULL;

		// If the head node is null, the list is empty.
		// This should never happen if we are keeping track of
		// our sizes correctly.
		tempNode = getP( tempNodeOff );
		if ( !tempNode)
			return false;

		// Store the offsets of the used list's new head node and the
		// free list's current head node.
		newUsedOff = tempNode->m_prev;
		oldFreeOff = m_freeNodes.m_tail;

		// If there was a head node for the free list, set its previous
		// to the node we are deleting.
		if ( oldFreeOff ) {
			oldFreeTail = getP( oldFreeOff );
			if ( !oldFreeTail )
				return false;
			oldFreeTail->m_next = tempNodeOff;
		}
		// If there was a node after the node being deleted, set its
		// previous to null because it is the new head node.
		if ( newUsedOff ) {
			newUsedTail = getP( newUsedOff );
			if ( !newUsedTail )
				return false;
			newUsedTail->m_next = 0;
		}
		// Update the deleted node.
		tempNode->m_prev    = oldFreeOff;
		tempNode->m_next    = 0;
		tempNode->m_used    = 0;
		payload  	    = tempNode->m_payload;
		// If we are deleting the only node in the used list,
		// it is now empty.
		if ( m_usedNodes.m_head == tempNodeOff )
			m_usedNodes.m_head = 0;
		m_usedNodes.m_tail = newUsedOff;
		m_usedNodes.m_size--;
		// If the free list was empty, we are now the only node in it.
		if ( m_freeNodes.m_head == 0 )
			m_freeNodes.m_head = tempNodeOff;
		m_freeNodes.m_tail = tempNodeOff;
		m_freeNodes.m_size++;

		return true;
	};

	bool getDataAt( SLIterator<Type> &iter, Type &payload ) {
		SLNode<Type> *node = getP( iter.m_offset );
		if ( node && node->m_used ) {
			payload = node->m_payload;
			return true;
		}
		return false;
	};

	bool getHead( Type &payload ) {
		SLNode<Type> *node = getP( m_usedNodes.m_head );
		if ( node ) {
			payload = node->m_payload;
			return true;
		}
		return false;
	};

	bool getTail( Type &payload ) {
		SLNode<Type> *node = getP( m_usedNodes.m_tail );
		if ( node ) {
			payload = node->m_payload;
			return true;
		}
		return false;
	};

	bool pull( SLIterator<Type> &iter, Type &payload ) {
		if ( m_usedNodes.m_size == 0 )
			return false;
		uint32_t tempNodeOff = iter.m_offset;
		// If we are at the head or tail of the list,
		// we simply use the specialized functions
		// for those cases.
		if ( tempNodeOff == m_usedNodes.m_head )
			return popHead( payload );
		if ( tempNodeOff == m_usedNodes.m_tail )
			return popTail( payload );
		SLNode<Type> *tempNode = getP( tempNodeOff );
		if ( !tempNode )
			return false;
		if ( !tempNode->m_used )
			return false;
		SLNode<Type> *prevNode = getP( tempNode->m_prev );
		SLNode<Type> *nextNode = getP( tempNode->m_next );
		// We already checked if we were at the head
		// or tail of the list, in which is are the
		// only cases when one of these will be NULL.
		if ( ! prevNode || ! nextNode )
			return false;
		payload = tempNode->m_payload;
		prevNode->m_next = tempNode->m_next;
		nextNode->m_prev = tempNode->m_prev;
		uint32_t oldFreeOff = m_freeNodes.m_head;
		m_freeNodes.m_size++;
		m_usedNodes.m_size--;
		m_freeNodes.m_head = tempNodeOff;
		tempNode->m_prev = 0;
		tempNode->m_used = 0;
		SLNode<Type> *oldFreeHead = getP( oldFreeOff );
		if ( !oldFreeHead ) {
			m_freeNodes.m_tail = tempNodeOff;
			tempNode->m_next = 0;
			return true;
		}
		// If this is an invalid pointer, we have not been
		// keeping account correctly.
		// Consider getting this earlier if it exists.
		oldFreeHead->m_prev = tempNodeOff;
		tempNode->m_next    = oldFreeOff;
		return true;
	};

	Type *sample( SLIterator<Type> &iter ) {
		return getPayloadP( iter.m_offset );
	};

	Type *sampleHead() {
		return getPayloadP( m_usedNodes.m_head );
	};

	Type *sampleTail() {
		return getPayloadP( m_usedNodes.m_tail );
	};

	bool isEmpty() {
		if ( m_usedNodes.m_size > 0 )
			return false;
		return true;
	};

	uint32_t size() {
		return m_usedNodes.m_size;
	};

	// Associate an iterator with this list.
	bool setIterator( SLIterator<Type> &iter ) {
		iter.m_offset = m_usedNodes.m_head;
		iter.m_list   = this;
		return true;
	};

	void setIteratorHead( SLIterator<Type> &iter ) {
		iter.m_offset = m_usedNodes.m_head;
		iter.m_list   = this;
	};

	void setIteratorTail( SLIterator<Type> &iter ) {
		iter.m_offset = m_usedNodes.m_tail;
		iter.m_list   = this;
	};

	bool incIterator( SLIterator<Type> &iter ) {
		SLNode<Type> *node = getP( iter.m_offset );
		if ( node && node->m_next ) {
			iter.m_offset = node->m_next;
			return true;
		}
		return false;
	};

	bool decIterator( SLIterator<Type> &iter ) {
		SLNode<Type> *node = getP( iter.m_offset );
		if ( node && node->m_prev ) {
			iter.m_offset = node->m_prev;
			return true;
		}
		return false;
	};

	// Request space for more nodes. We only realloc
	// if we don't have enough space in both the free
	// list and the slab for the requested number of
	// nodes.
	bool request( uint32_t numNodes ) {
		if ( m_cap <= (m_usedNodes.m_size + numNodes) )
			return false;
		// If no nodes were requested, nothing to do.
		if ( !numNodes ) {
			return true;
		}
		uint32_t bytesToAdd = numNodes * sizeof(class SLNode<Type>);
		// If we already have enough space, no need to realloc.
		if ( ( (m_freeNodes.m_size * sizeof(class SLNode<Type>)) +
		       (m_listAlloc - m_listSlab) ) >= bytesToAdd )
				return true;
		if ( numNodes == 1 ) {
			// If we are out of nodes, add 50% again ontop of the
			// previous amount of space.
			uint32_t listNodes = m_listAlloc /
					     sizeof(class SLNode<Type>);
			if ( listNodes > 2 ) {
				numNodes = listNodes / 2;
				bytesToAdd = numNodes *
					     sizeof(class SLNode<Type>);
			}
		}
		// If this is our first allocation, we need to add the
		// zero byte at the beginning of the "slab".
		if ( (!m_list) || (!m_listAlloc) ) {
			bytesToAdd++;
		}
		// If the requested number of nodes will cause overflow, don't
		// attempt to realloc, otherwise we will decrease the size of
		// our buffer.
		if ( (0xffffffff - bytesToAdd) < m_listAlloc ) {
			return false;
		}
		uint8_t *tmpList  = m_list;
		uint32_t tmpAlloc = m_listAlloc;
		m_listAlloc += bytesToAdd;
#ifdef _SL_TEST_ // If SafeList is being tested outside of gb
		m_list = (uint8_t *)realloc( m_list, m_listAlloc );
#else
		m_list = (uint8_t *)mrealloc( m_list, tmpAlloc, m_listAlloc,
					      "SafeList" );
#endif
		if (!m_list) {
			m_list = tmpList;
			m_listAlloc = tmpAlloc;
			return false;
		}
		if ( !m_listSlab ) m_listSlab = 1;

		return true;
	};

	// Move all nodes to the beginning of our allocation
	// and resize if we have to much unused space allocated.
	// @ fast - if this is true we only rebalance if we can first
	//          allocate a new buffer to push the used nodes onto
	// 	    discarding all free nodes, otherwise we 
	// 	    don't perform the operation and return false.
	bool rebalance( bool fast = true ) {
		// This feature is not yet supported
		return false;
	};
	
	// setCap() will set the max number of nodes allowed
	// in this list. If we call setCap on with a
	// number of nodes than is less than the current
	// size, we simply refuse to add any more; and
	// once we get below that cap, we will never pass
	// it again. 
	void setCap( uint32_t numNodes ) {
		m_cap = numNodes;
	};

	// clearCap() takes away the upper limit on the
	// number of nodes. We can add nodes until we run
	// out of memory.
	void clearCap() {
		m_cap = 0xffffffff;
	};

	// For debugging only
	void printNodes() {
		class SLNode<Type> *tempNode;
		uint32_t tempNodeOff;
		uint32_t nodeId;

		if ( !m_list || (m_listAlloc < sizeof(class SLNode<Type>)) ) {
			return;
		}

		printf( "================ USED NODES ================\n\n" );
		tempNodeOff = m_usedNodes.m_head;
		nodeId = 0;
		while ( tempNodeOff ) {
			tempNode = getP( tempNodeOff );
			printf( "Node %u:\n"
				"next off = %u\n"
				"prev off = %u\n"
				"p_value  = %x\n\n",
				nodeId,
				tempNode->m_next,
				tempNode->m_prev,
				(uint32_t)tempNode );
			tempNodeOff = getNext( tempNodeOff );
			nodeId++;
		}

		printf( "================ FREE NODES ================\n\n" );
		tempNodeOff = m_freeNodes.m_head;
		nodeId = 0;
		while ( tempNodeOff ) {
			tempNode = getP( tempNodeOff );
			printf( "Node %u:\n"
				"next off = %u\n"
				"prev off = %u\n"
				"p_value  = %x\n\n",
				nodeId,
				tempNode->m_next,
				tempNode->m_prev,
				(uint32_t)tempNode );
			tempNodeOff = getNext( tempNodeOff );
			nodeId++;
		}

		printf( "================== SLAB SPACE REMAINING "
			"===================="
			"\n\n" );
		printf( "Slab has room for %u additional nodes.\n",
				(m_listAlloc - m_listSlab) /
				sizeof(class SLNode<Type>) );
		printf( "Slab has %u wasted bytes at end of allocation.\n\n",
				(m_listAlloc - m_listSlab) %
				sizeof(class SLNode<Type>) );
	};

	// For debugging only
	void printListStatus( ) {
		printf( "================= SafeList current status "
			"=================="
			"\n"
			"Bytes Allocated:  %u\n"
			"Bytes Used:       %u\n"
			"Bytes on Slab:    %u\n"
			"Slab Nodes:       %u\n"
			"\n"
			"FreeList\n"
			"Nodes:            %u\n"
			"Bytes:            %u\n"
			"\n"
			"UsedList\n"
			"Nodes:            %u\n"
			"Bytes:            %u\n"
			""
			"==========================================="
			"================="
			"\n",
			m_listAlloc,
			m_listSlab,
			m_listAlloc - m_listSlab,
			(m_listAlloc - m_listSlab) / sizeof(class SLNode<Type>),
			m_freeNodes.m_size,
			m_freeNodes.m_size * sizeof(class SLNode<Type>),
			m_usedNodes.m_size,
			m_usedNodes.m_size * sizeof(class SLNode<Type>)
			);
	};

};

template<typename Type> class SLIterator {
 friend class SafeList<Type>;
 protected:
	uint32_t m_offset;
	class SafeList<Type> *m_list;
 public:
	SLIterator() {
		m_offset = 0;
		m_list   = NULL;
	}

 	// Got to next element, return false if we are at
	// the tail node.
 	bool operator ++ ( int ) {
		if ( !m_list ) return false;
		return m_list->incIterator( *this );
	}

 	// Got to previous element, return false if we are at
	// the head node.
 	bool operator -- ( int ) {
		if ( !m_list ) return false;
		return m_list->decIterator( *this );
	}

	// Set this iterator equal to an iterator of the same type.
	void operator = (SLIterator<Type> &i) {
		m_offset = i.m_offset;
		m_list   = i.m_list;
	}

	// Set iterator to the head of the list.
	void setHead () {
		if ( m_list ) m_list->setIteratorHead( *this );
	}

	// Set iterator to the tail of the list.
	void setTail () {
		if ( m_list ) m_list->setIteratorTail( *this );
	}

	// May not want to be able to do this...
	// Although it simply calls a public function,
	// so there is no security issue.
	// Gets the data at the current point if it exists,
	// otherwise payload remains unmodified.
	bool getCurrent ( Type &payload ) {
		if ( !m_list ) return false;
		return m_list->getDataAt( *this, payload );
	}

	// Associates this iterator with the list passed as the arg.
	bool associate ( SafeList<Type> &list ) {
		return list.setIterator( *this );
	}

	// Drops the list this iterator is associated with.
	void dropList () {
		m_offset = 0;
		m_list   = NULL;
	}
};

template<typename Type> class SLNode {
 friend class SafeList<Type>;
 protected:
	uint32_t m_next;
	uint32_t m_prev;
	Type     m_payload;
	uint8_t  m_used;
};


template<typename Type> class SLData {
 friend class SafeList<Type>;
 protected:
	uint32_t m_head;
	uint32_t m_tail;
	uint32_t m_size;
};


#endif
