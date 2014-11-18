#include "gb-include.h"

#include "MemPoolTree.h"

MemPoolTree::MemPoolTree () {
	m_mem           = NULL;
	m_memSize       =  0;
	m_headNode      = NULL;
	m_numUsedNodes  =  0;
	m_nextNode      = NULL;
	m_floor         = NULL; 
}

MemPoolTree::~MemPoolTree ( ) {
	m_mem           = NULL;
	m_memSize       =  0;
}

// "memMax" includes records plus the overhead
bool MemPoolTree::init ( char *mem , int32_t memSize ) {
	// all the mem
	m_mem = mem;
	m_memSize = memSize;
	// the floor
	m_floor = (MemNode *)(m_mem + m_memSize) ;
	// next available node
	m_nextNode = m_floor - 1;
	return true;
}

// . used by cache 
// . wrapper for getNode()
MemNode *MemPoolTree::getNode ( MemKey &key ) {
	// debug msg
	//log("getting node for k.n1=%"UINT32" n0=%"UINT64"",key.n1,key.n0);
	// get the node (about 4 cycles per loop, 80cycles for 1 million items)
	MemNode *i = m_headNode;
	while ( i ) {
		if ( key < i->m_key  ) { i = i->m_left ; continue;}
		if ( key > i->m_key  ) { i = i->m_right; continue;}
		return i;
        }
	return NULL;
}

// . returns node whose key is >= "key"
// . returns NULL if none
MemNode *MemPoolTree::getNextNode ( MemKey &key ) {
	// return NULL if tree empty
	if ( ! m_headNode ) return NULL;
	// get the node
	MemNode *parent;
	MemNode *i = m_headNode ;
	while ( i ) {
		parent = i ;
		if ( key < i->m_key  ) { i = i->m_left  ; continue; }
		if ( key > i->m_key  ) { i = i->m_right ; continue; }
		return i;
        }
	// if parent's key is > we're done
	if ( parent->m_key  > key ) return parent;
	// otherwise we must get the node after the parent
	return getNextNode ( parent );
}

// . get the node whose key is <= "key"
// . returns -1 if none
MemNode *MemPoolTree::getPrevNode ( MemKey &key ) {
	// return NULL if tree empty
	if ( ! m_headNode ) return NULL;
	// get the node 
	MemNode *parent;
	MemNode *i = m_headNode ;
	while ( i ) {
		parent = i ;
		if ( key < i->m_key  ) { i = i->m_left  ; continue; }
		if ( key > i->m_key  ) { i = i->m_right ; continue; }
		return i;
        }
	// if parent's key is < we're done
	if ( parent->m_key  < key ) return parent;
	// otherwise we must get the node after the parent
	return getPrevNode ( parent );
}

// get next node after "node"
MemNode *MemPoolTree::getNextNode ( MemNode *i ) {
	// cruise the kids if we have a right one
	if ( i->m_right ) {
		// go to the right kid
		i = i->m_right;
		// now go left as much as we can
		while ( i->m_left ) i = i->m_left;
		// return that node (it's a leaf or has one right kid)
		return i;
	}
	// now keep getting parents until one has a key bigger than i's key
	MemNode *p = i->m_parent;
	// if parent is NULL we're done
	if ( ! p ) return NULL;
	// if we're the left kid of the parent, then the parent is the
	// next biggest node
	if ( p->m_left == i ) return p;
	// otherwise keep getting the parent until it has a bigger key
	// or until we're the LEFT kid of the parent. that's better
	// cuz comparing keys takes longer. loop is 6 cycles per iteration.
	while ( p && p->m_key < i->m_key ) p = p->m_parent;
	// p will be NULL if none are left
	return p;
}

MemNode *MemPoolTree::getPrevNode ( MemNode *i ) {
	// cruise the kids if we have a left one
	if ( i->m_left ) {
		// go to the left kid
		i = i->m_left;
		// now go right as much as we can
		while ( i->m_right ) i = i->m_right ;
		// return that node (it's a leaf or has one left kid)
		return i;
	}
	// now keep getting parents until one has a key bigger than i's key
	MemNode *p = i->m_parent;
	// if we're the right kid of the parent, then the parent is the
	// next least node
	if ( p->m_right == i ) return p;
	// keep getting the parent until it has a bigger key
	// or until we're the RIGHT kid of the parent. that's better
	// cuz comparing keys takes longer. loop is 6 cycles per iteration.
	while ( p && p->m_key > i->m_key ) p = p->m_parent;
	// p will be NULL if none are left
	return p;
}

// . returns NULL and sets errno on failure
// . returns node ptr we added it to on success
// . this will NOT replace any current node with the same key
MemNode *MemPoolTree::addNode ( MemKey &key ) {
	// debug msg
	//log("adding k.n1=%"UINT32" n0=%"UINT64"",key.n1,key.n0);
	// set up vars
	MemNode *iparent ;
	// this is NULL iff there are no nodes used in the tree
	MemNode *i = m_headNode;
	// . find the parent of node i
	// . if a node exists with our key then replace it
	while ( i ) {
		iparent = i ;
		if      ( key < i->m_key ) i = i->m_left  ; 
		else if ( key > i->m_key ) i = i->m_right ;
		else {
			errno = EBADENGINEER;
			log("MemPoolTree::addNode: node already in tree");
			return NULL;
		}
	}
	// store in the next available node
	i = m_nextNode;
	// if we're the first node we become the head node and our parent is -1
	if ( ! m_headNode ) {
		m_headNode = i;
		iparent    = NULL;
	}
	// . the right kid of an empty node is used as a linked list of
	//   empty nodes formed by deleting nodes
	// . we keep the linked list so we can re-used these vacated nodes
	MemNode *rightGuy = i->m_right ;
	// stick ourselves in the next available node, "m_nextNode"
	i->m_key    = key;
	i->m_parent = iparent;
	i->m_left   = NULL;
	i->m_right  = NULL;
	i->m_depth  = 1;      // leave nodes have depth of 1
	// make our parent, if any, point to us
	if ( iparent ) {
		if      ( key  < iparent->m_key  ) iparent->m_left  = i;
		else if ( key  > iparent->m_key  ) iparent->m_right = i;
		else log("MemPoolTree::addNode: bad engineer");
	}
	// . if we weren't recycling a middle node then advance to next
	// . m_floor is the lowest node number that was never filled
	//   at any one time in the past
	// . you might call it the brand new housing district
	// . we advance downwards like a stack to use memory most efficiently
	if ( m_nextNode == m_floor - 1 ) { m_nextNode--; m_floor--; }
	// . otherwise, we're in a linked list of vacated used houses
	// . we have a linked list in the right kid
	// . make sure the new head doesn't have a left
	else if ( rightGuy ) m_nextNode = rightGuy;
	// otherwise point it to the next brand new house (TODO:REMOVE)
	else {
		log("MemPoolTree::addNode: bad engineer 2");
		sleep(50000); // m_nextNode = m_floor;
	}
	// we have one more used node
	m_numUsedNodes++;
	// . reset depths starting at our parent and ascending the tree
	// . will balance if child depths differ by 2 or more
	setDepths ( iparent );
	// return the node we occupied
	return i;
}

// . deletes node i from the tree
// . i's parent should point to i's left or right kid
// . if i has no parent then his left or right kid becomes the new top node
void MemPoolTree::deleteNode ( MemNode *i ) {
	// watch out for NULL ptrs
	if ( ! i ) { 
		log("\n\n\n\n\nMemPoolTree::deleteNode: NULL MemNode"); return; }
	// watch out for double deletes
	if ( i->m_parent == (MemNode *) -2 ) {
		log("MemPoolTree::deleteNode: caught bad deleteNode() call");
		return;
	}
	// parents
	MemNode *iparent ;
	MemNode *jparent ;
	// node2 replaces node
	MemNode *j = i;
	// . now find a node to replace "node"
	// . get a node whose key is just to the right or left of node's key
	// . get node's right kid
	// . then get that kid's LEFT MOST leaf-node descendant
	// . this little routine is stolen from getNextNode(i)
	// . try to pick a kid from the right the same % of time as from left
	if ( j->m_right && ( m_pickRight || ! j->m_left )  ) {
		// try to pick a left kid next time
		m_pickRight = 0;
		// go to the right kid
		j =j->m_right ;
		// now go left as much as we can
		while ( j->m_left ) j = j->m_left ;
		// usej (it's a leaf or has a right kid)
		goto gotReplacement;
	}
	// . now get the previous node if i has no right kid
	// . this little routine is stolen from getPrevNode(i)
	if ( j->m_left ) {
		// try to pick a right kid next time
		m_pickRight = 1;
		// go to the left kid
		j = j->m_left;
		// now go right as much as we can
		while ( j->m_right ) j =j->m_right;
		// usej (it's a leaf or has a left kid)
		goto gotReplacement;
	}
	// . come here if i did not have any kids (i's a leaf node)
	// . get i's parent
	iparent = i->m_parent;
	// make parent, if any, disown him
	if ( iparent ) {
		if   ( iparent->m_left == i ) iparent->m_left  = NULL;
		else                          iparent->m_right = NULL;
	}
	// node now goes to the top of the list of vacated, available homes
	i->m_right = m_nextNode;
	// m_nextNode now points to node
	m_nextNode = i;
	// his parent is -2 (god) cuz he's dead and available
	i->m_parent = (MemNode *) -2;
	// . if we were the head node then, since we didn't have any kids,
	//   the tree must be empty
	// . one less node in the tree
	m_numUsedNodes--;
	// . reset the depths starting at iparent and going up until unchanged
	// . will balance at pivot nodes that need it
	setDepths ( iparent );
	// return if there are still nodes in the tree
	if ( m_numUsedNodes > 0 ) return;
	// otherwise tree must be empty
	m_headNode = NULL;
	// this will nullify our linked list of vacated, used homes
	m_floor    = (MemNode *)(m_mem   + m_memSize) ;
	m_nextNode = m_floor - 1;
	return;
	// . now replace node #i with node #j
	// . i should not equal j at this point
 gotReplacement:

	// . j's parent should take j's one kid
	// . that child should likewise point to j's parent
	// . j should only have <= 1 kid now because of our algorithm above
	// . if j's parent is i then j keeps his kid
	jparent = j->m_parent;
	if ( jparent != i ) {
		// parent:    if j is my left  kid, then i take j's right kid
		// otherwise, if j is my right kid, then i take j's left kid
		if ( jparent->m_left == j ) {
			jparent->m_left  = j->m_right;
			if ( j->m_right ) j->m_right->m_parent = jparent;
		}
		else {
			jparent->m_right = j->m_left ;
			if ( j->m_left  ) j->m_left->m_parent  = jparent;
		}
	}
	// . j inherits i's children (providing i's child is not j)
	// . those children's parent should likewise point to j
	if ( i->m_left != j ) {
		j->m_left = i->m_left;
		if ( j->m_left ) j->m_left->m_parent = j;
	}
	if ( i->m_right != j ) {
		j->m_right = i->m_right;
		if ( j->m_right ) j->m_right->m_parent = j;
	}
	// j becomes the kid of i's parent, if any
	iparent = i->m_parent;
	if ( iparent ) {
		if   ( iparent->m_left == i ) iparent->m_left  = j;
		else                          iparent->m_right = j;
	}
	// iparent may be -1
	j->m_parent = iparent;

	// if i was the head node now j becomes the head node
	if ( m_headNode == i ) m_headNode = j;

	// . i joins the linked list of available used homes
	// . put it at the head of the list 
	// . "m_nextNode" is the head node of the linked list
	i->m_right  = m_nextNode;
	m_nextNode  = i;
	// . i's parent should be -2 so we know it's unused in case we're
	//   stepping through the nodes linearly for dumping in RdbDump
	// . used in getListUnordered()
	i->m_parent = (MemNode *)-2;
	// we have one less used node
	m_numUsedNodes--;
	// debug step -- check chain from iparent down making sure that
	// all kids don't have -2 for their parent... seems to be a rare bug
	//printTree();
	// debug msg
	//fprintf(stderr,"- #%"INT32" %"INT64" %"INT32"\n",i,m_keys[i].n0,iparent);
	// our depth becomes that of the node we replaced, unless moving j
	// up to i decreases the total depth, in which case setDepths() fixes
	j->m_depth = i->m_depth ;
	// debug msg
	//fprintf(stderr,"... replaced %"INT32" it with %"INT32" (-1 means none)\n",i,j);
	// . recalculate depths starting at old parent of j
	// . stops at the first node to have the correct depth
	// . will balance at pivot nodes that need it
	if ( jparent != i ) setDepths ( jparent );
	else                setDepths ( j );
	// TODO: register growTree with g_mem to free on demand
	// do a grow/shrink test and shrink if we need to
	//	return growTable ( );
	// done:
}

// . recompute depths of nodes starting at i and ascending the tree
// . call rotateRight/Left() when depth of children differs by 2 or more
void MemPoolTree::setDepths ( MemNode *i ) {
	// inc the depth of all parents if it changes for them
	while ( i ) {
		// . compute the new depth for node i
		// . get depth of left kid
		// . left/rightDepth is depth of subtree on left/right
		int32_t leftDepth  = 0;
		int32_t rightDepth = 0;
		if ( i->m_left  ) leftDepth  = i->m_left->m_depth;
		if ( i->m_right ) rightDepth = i->m_right->m_depth;
		// . get the new depth for node i
		// . add 1 cuz we include ourself in our m_depth
		int32_t newDepth ;
		if ( leftDepth > rightDepth ) newDepth = leftDepth  + 1;
		else                          newDepth = rightDepth + 1;
		// if the depth did not change for i then we're done
		int32_t oldDepth = i->m_depth;
		// set our new depth
		i->m_depth = newDepth;
		// diff can be -2, -1, 0, +1 or +2
		int32_t diff = leftDepth - rightDepth;
		// . if it's -1, 0 or 1 then we don't need to balance
		// . if rightside is deeper rotate left, i is the pivot
		// . otherwise, rotate left
		// . these should set the m_depth[*] for all nodes needing it
		if      ( diff == -2 ) i = rotateLeft  ( i );
		else if ( diff ==  2 ) i = rotateRight ( i );
		// . return if our depth was ultimately unchanged
		// . i may have change if we rotated, but same logic applies
		if ( i->m_depth == oldDepth ) break;
		// debug msg
		//fprintf (stderr,"changed node %"INT32"'s depth from %"INT32" to %"INT32"\n",
		//i,oldDepth,newDepth);
		// get his parent to continue the ascension
		i = i->m_parent;
	}
}

/*
// W , X and B are SUBTREES.
// B's subtree was 1 less in depth than W or X, then a new node was added to
// W or X triggering the imbalance.
// However, if B gets deleted W and X can be the same size.
//
// Right rotation if W subtree depth is >= X subtree depth:
//
//          A                N
//         / \              / \
//        /   \            /   \
//       N     B   --->   W     A
//      / \                    / \
//     W   X                  X   B
//
// Right rotation if W subtree depth is <  X subtree depth:
//          A                X
//         / \              / \
//        /   \            /   \
//       N     B   --->   N     A
//      / \              / \   / \
//     W   X            W   Q T   B
//        / \                 
//       Q   T               
*/

// . we come here when A's left subtree is deeper than it's right subtree by 2
// . this rotation operation causes left to lose 1 depth and right to gain one
// . the type of rotation depends on which subtree is deeper, W or X
// . W or X must deeper by the other by exactly one
// . if they were equal depth then how did adding a node inc the depth?
// . if their depths differ by 2 then N would have been rotated first!
// . the parameter "i" is the node # for A in the illustration above
// . return the node # that replaced A so the balance() routine can continue
// . TODO: check our depth modifications below
MemNode *MemPoolTree::rotateRight ( MemNode *i ) {
	// i's left kid's right kid takes his place
	MemNode *A = i;
	MemNode *N = A->m_left ;
	MemNode *W = N->m_left ;
	MemNode *X = N->m_right;
	MemNode *Q = NULL;
	MemNode *T = NULL;
	if ( X ) {
		Q = X->m_left ;
		T = X->m_right;
	}
	// let AP be A's parent
	MemNode *AP = A->m_parent ;
	// whose the bigger subtree, W or X? (depth includes W or X itself)
	int32_t Wdepth = 0;
	int32_t Xdepth = 0;
	if ( W ) Wdepth = W->m_depth;
	if ( X ) Xdepth = X->m_depth;
	// debug msg
	//fprintf(stderr,"A=%"INT32" AP=%"INT32" N=%"INT32" W=%"INT32" X=%"INT32" Q=%"INT32" T=%"INT32" "
	//"Wdepth=%"INT32" Xdepth=%"INT32"\n",A,AP,N,W,X,Q,T,Wdepth,Xdepth);
	// goto Xdeeper if X is deeper
	if ( Wdepth < Xdepth ) goto Xdeeper;
	// N's parent becomes A's parent
	N->m_parent = AP;
	// A's parent becomes N
	A->m_parent = N;
	// X's parent becomes A
	if ( X ) X->m_parent = A;
	// A's parents kid becomes N
	if ( AP ) {
		if ( AP->m_left == A ) AP->m_left  = N;
		else                   AP->m_right = N;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node from %"INT32" to %"INT32"\n",
		//m_headNode,N);
		m_headNode = N;
	}
	// N's right kid becomes A
	N->m_right = A;
	// A's left  kid becomes X		
	A->m_left  = X;
	// . compute A's depth from it's X and B kids
	// . it should be one less if Xdepth smaller than Wdepth
	// . might set m_depth[A] to computeDepth(A) if we have problems
	if ( Xdepth < Wdepth ) A->m_depth -= 2;
	else                   A->m_depth -= 1;
	// N gains a depth iff W and X were of equal depth
	if ( Wdepth == Xdepth ) N->m_depth += 1;
	// now we're done, return the new pivot that replaced A
	return N;
	// come here if X is deeper
 Xdeeper:
	// X's parent becomes A's parent
	X->m_parent = AP;
	// A's parent becomes X
	A->m_parent = X;
	// N's parent becomes X
	N->m_parent = X;
	// Q's parent becomes N
	if ( Q ) Q->m_parent = N;
	// T's parent becomes A
	if ( T ) T->m_parent = A;
	// A's parent's kid becomes X
	if ( AP ) {
		if ( AP->m_left == A ) AP->m_left  = X;
		else	               AP->m_right = X;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node2 from %"INT32" to %"INT32"\n",
		//m_headNode,X);
		m_headNode = X;
	}
	// A's left     kid becomes T
	A->m_left     = T;
	// N's right    kid becomes Q
	N->m_right    = Q;
	// X's left     kid becomes N
	X->m_left     = N;
	// X's right    kid becomes A
	X->m_right    = A;
	// X's depth increases by 1 since it gained 1 level of 2 new kids
	X->m_depth   += 1;
	// N's depth decreases by 1
	N->m_depth   -= 1;
	// A's depth decreases by 2
	A->m_depth   -= 2; 
	// now we're done, return the new pivot that replaced A
	return X;
}


MemNode *MemPoolTree::rotateLeft ( MemNode *i ) {
	// i's left kid's right kid takes his place
	MemNode *A = i;
	MemNode *N = A->m_right ;
	MemNode *W = N->m_right ;
	MemNode *X = N->m_left;
	MemNode *Q = NULL;
	MemNode *T = NULL;
	if ( X ) {
		Q = X->m_right ;
		T = X->m_left ;
	}
	// let AP be A's parent
	MemNode *AP = A->m_parent ;
	// whose the bigger subtree, W or X? (depth includes W or X itself)
	int32_t Wdepth = 0;
	int32_t Xdepth = 0;
	if ( W ) Wdepth = W->m_depth;
	if ( X ) Xdepth = X->m_depth;
	// debug msg
	//fprintf(stderr,"A=%"INT32" AP=%"INT32" N=%"INT32" W=%"INT32" X=%"INT32" Q=%"INT32" T=%"INT32" "
	//"Wdepth=%"INT32" Xdepth=%"INT32"\n",A,AP,N,W,X,Q,T,Wdepth,Xdepth);
	// goto Xdeeper if X is deeper
	if ( Wdepth < Xdepth ) goto Xdeeper;
	// N's parent becomes A's parent
	N->m_parent = AP;
	// A's parent becomes N
	A->m_parent = N;
	// X's parent becomes A
	if ( X ) X->m_parent = A;
	// A's parents kid becomes N
	if ( AP ) {
		if ( AP->m_right == A ) AP->m_right  = N;
		else                    AP->m_left = N;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node from %"INT32" to %"INT32"\n",
		//m_headNode,N);
		m_headNode = N;
	}
	// N's right kid becomes A
	N->m_left  = A;
	// A's left  kid becomes X		
	A->m_right = X;
	// . compute A's depth from it's X and B kids
	// . it should be one less if Xdepth smaller than Wdepth
	// . might set m_depth[A] to computeDepth(A) if we have problems
	if ( Xdepth < Wdepth ) A->m_depth -= 2;
	else                   A->m_depth -= 1;
	// N gains a depth iff W and X were of equal depth
	if ( Wdepth == Xdepth ) N->m_depth += 1;
	// now we're done, return the new pivot that replaced A
	return N;
	// come here if X is deeper
 Xdeeper:
	// X's parent becomes A's parent
	X->m_parent = AP;
	// A's parent becomes X
	A->m_parent = X;
	// N's parent becomes X
	N->m_parent = X;
	// Q's parent becomes N
	if ( Q ) Q->m_parent = N;
	// T's parent becomes A
	if ( T ) T->m_parent = A;
	// A's parent's kid becomes X
	if ( AP ) {
		if ( AP->m_right == A ) AP->m_right = X;
		else	                AP->m_left  = X;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node2 from %"INT32" to %"INT32"\n",
		//m_headNode,X);
		m_headNode = X;
	}
	// A's left     kid becomes T
	A->m_right    = T;
	// N's right    kid becomes Q
	N->m_left     = Q;
	// X's left     kid becomes N
	X->m_right    = N;
	// X's right    kid becomes A
	X->m_left     = A;
	// X's depth increases by 1 since it gained 1 level of 2 new kids
	X->m_depth   += 1;
	// N's depth decreases by 1
	N->m_depth   -= 1;
	// A's depth decreases by 2
	A->m_depth   -= 2; 
	// now we're done, return the new pivot that replaced A
	return X;
}
