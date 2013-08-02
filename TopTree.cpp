#include "gb-include.h"

#include "TopTree.h"
#include "Mem.h"
#include "Errno.h"
#include "Titledb.h" // DOCID_MASK
#include "Msg40.h" // MAXDOCIDSTOCOMPUTE

/*
long long TopNode::getDocId ( ) {
	long long d;
	memcpy ( &d , m_docIdPtr , 6 );
	d >>= 2;
	d &= DOCID_MASK;
	return d;
}


long long TopNode::getDocIdForMsg3a ( ){
	long long d;
	memcpy ( &d , m_docIdPtr , 6 );
	//	d >>= 2;
	d &= DOCID_MASK;
	return d;
}
*/

TopTree::TopTree() { 
	m_nodes = NULL; 
	// sampleVectors = NULL; 
	reset(); 
}

TopTree::~TopTree() { reset(); }

void TopTree::reset ( ) {
	if ( m_nodes ) mfree(m_nodes,m_allocSize,"TopTree");
	m_nodes = NULL;
	//m_sampleVectors  = NULL;
	m_numNodes = 0;
	m_numUsedNodes = 0;
	m_headNode = -1;
	m_lowNode  = -1;
	m_highNode = -1;
	m_t2.reset();
}

// deletes the nodes, but doesn't free memory
// so basically just reset everything
void TopTree::deleteNodes ( ) {
	m_numUsedNodes = 0;
	// make empty the last
	m_emptyNode = 0;
	// set it
	m_headNode = -1;
	// score info
	m_lowNode  = -1;
	m_highNode = -1;
}	

// . pre-allocate memory
// . returns false and sets g_errno on error
bool TopTree::setNumNodes ( long docsWanted , bool doSiteClustering ) {

	// save this
	m_docsWanted       = docsWanted;
	m_doSiteClustering = doSiteClustering;

	// reset this
	m_kickedOutDocIds = false;
	//m_lastKickedOutDocId = -1LL;
	
	// how many nodes to we need to accomodate "docsWanted" docids?
	m_ridiculousMax = docsWanted * 2;
	if ( m_ridiculousMax < 50 ) m_ridiculousMax = 50;
	long numNodes = m_ridiculousMax * 256;
	// i would say limit it to 100,000 nodes regarless
	if ( numNodes > MAXDOCIDSTOCOMPUTE ) numNodes = MAXDOCIDSTOCOMPUTE;
	// craziness overflow?
	if ( numNodes < 0 ) numNodes = MAXDOCIDSTOCOMPUTE;
	// amp it up last minute, after we set numNodes, if we need to
	if ( ! m_doSiteClustering ) m_ridiculousMax = 0x7fffffff;

	// how many docids do we have, not FULLY counting docids from
	// "dominating" domains? aka the "variety count"
	m_vcount = 0.0;

	// limit vcount to "cap" docids per domain
	m_cap  = m_docsWanted / 50;
	if ( m_cap < 2 ) m_cap = 2;
	if ( ! m_doSiteClustering ) m_cap = 0x7fffffff;

	// to keep things more continuous as a function of "m_docsWanted" we 
	// count docids right at the "cap" as a fractional count. see below.
	m_partial = (float)(m_docsWanted % 50) / 50.0;

	// reset dom count array
	memset ( m_domCount , 0 , 4 * 256 );

	// reset domain min nodes
	for ( long i = 0 ; i < 256 ; i++ )
		m_domMinNode[i] = -1;

	// return if nothing needs to be done
	if ( m_nodes && numNodes == m_numNodes ) return true;
	// save this
	//m_useSampleVectors = useSampleVectors;
	// . grow using realloc if we should
	// . alloc for one extra to use as the "empty node"
	//long vecSize = 0;
	//if ( useSampleVectors ) vecSize = SAMPLE_VECTOR_SIZE ;
	char *nn ;
	long oldsize = (m_numNodes+1) * ( sizeof(TopNode) );
	long newsize = (  numNodes+1) * ( sizeof(TopNode) );
	bool updated = false;
	if (! m_nodes) {
		nn=(char *)mmalloc (newsize,"TopTree");
		m_numUsedNodes = 0;
	}
	else  {
		nn=(char *)mrealloc(m_nodes,oldsize,newsize,"TopTree");
		updated = true;
	}
	if ( ! nn ) return log("query: Can not allocate %li bytes for "
			       "holding resulting docids.",  newsize);
	// save this for freeing
	m_allocSize = newsize;
	// success
	char *p = nn;
	m_nodes    = (TopNode *)p;
	m_numNodes = numNodes;
	p += (numNodes+1) * sizeof(TopNode);
	// vectors
	//if ( m_useSampleVectors ) m_sampleVectors = (long *)p;
	// bail now if just realloced
	if ( updated ) return true;
	// make empty the last
	m_emptyNode = 0;
	// set it
	m_headNode = -1;
	// score info
	m_lowNode  = -1;
	m_highNode = -1;

	// setup the linked list of empty nodes
	for ( long i = 0 ; i < m_numNodes ; i++ ) {
		m_nodes[i].m_parent = -2;
		m_nodes[i].m_right  = i+1;
	}
	// last node is the end of the linked list of available nodes
	m_nodes[m_numNodes-1].m_right = -1;

	// alloc space for m_t2, only if doing site clustering
	if ( ! m_doSiteClustering ) return true;

	// . we must limit domHash to m_ridiculousMax nodes
	// . "dataInPtrs" mean we have a 4 byte data that we store in the
	//   "dataPtr". this is somewhat of a hack, but we need a place to
	//   store the node number of this node in this top tree. see below.
	if ( ! m_t2.set ( 4          , // fixedDataSize
			  m_numNodes , // maxNumNodes
			  true       , // doBalancing
			  -1         , // memMax (-1-->no max)
			  false      , // ownData?
			  "tree-toptree"  ,
			  true       , // dataInPtrs?
			  NULL       , // dbname (generic)
			  12         , // keySize
			  false      ))// useProtection?
		return false;

	return true;
}

#define RIGHT(i)  m_nodes[i].m_right
#define LEFT(i)   m_nodes[i].m_left
#define DEPTH(i)  m_nodes[i].m_depth
#define PARENT(i) m_nodes[i].m_parent

// . we only compute this when we need to, no need to keep it going on
// . no, because we re-use the tree
long TopTree::getHighNode ( ) {
	if ( m_headNode == -1 ) return -1;
	long tn2;
	long tn = m_headNode;
	while ( (tn2=RIGHT(tn)) >= 0 ) tn = tn2;
	return tn;
	//m_highNode = tn;
	//return m_highNode;
}

// returns true if added node. returns false if did not add node
bool TopTree::addNode ( TopNode *t , long tnn ) {

	// respect the dom hashes
	//uint8_t domHash = g_titledb.getDomHash8((uint8_t*)t->m_docIdPtr);
	uint8_t domHash = g_titledb.getDomHash8FromDocId(t->m_docId);

	// if vcount is satisfied, only add if better score than tail
	if ( m_vcount >= m_docsWanted ) {
		long i = m_lowNode;

		if ( t->m_score < m_nodes[i].m_score ) {
			m_kickedOutDocIds = true; return false; }
		if ( t->m_score > m_nodes[i].m_score ) goto addIt;
		// . finally, compare docids, store lower ones first
		// . docids should not tie...
		if ( t->m_docId >= m_nodes[i].m_docId ) {
			m_kickedOutDocIds = true; return false; }
		// we got a winner
		goto addIt;
		/*
		if ( *(unsigned long  *)(t->m_docIdPtr+1) >
		     *(unsigned long  *)(m_nodes[i].m_docIdPtr+1) ) {
			m_kickedOutDocIds = true; return false; }
		if ( *(unsigned long  *)(t->m_docIdPtr+1) <
		     *(unsigned long  *)(m_nodes[i].m_docIdPtr+1) ) goto addIt;
		if ( (*(unsigned char  *)(t->m_docIdPtr)&0xfc) >
		     (*(unsigned char  *)(m_nodes[i].m_docIdPtr)&0xfc)) {
			m_kickedOutDocIds = true; return false; }
		if ( (*(unsigned char  *)(t->m_docIdPtr)&0xfc) <
		     (*(unsigned char  *)(m_nodes[i].m_docIdPtr)&0xfc) ) 
			goto addIt;
		// a tie, skip it
		m_kickedOutDocIds = true;
		return false;
		*/
	}

 addIt:

	long iparent = -1;
	// this is -1 iff there are no nodes used in the tree
	long i = m_headNode;
	// JAB: gcc-3.4
	char dir = 0;
	// if we're the first node we become the head node and our parent is -1
	if ( m_numUsedNodes == 0 ) {
		m_headNode  =  0;
		iparent     = -1;
	}
	// . find the parent of node i and call it "iparent"
	// . if a node exists with our key then do NOT replace it
	else while ( i >= 0 ) {
		iparent = i;
		// . compare to the ith node
		if ( t->m_score < m_nodes[i].m_score ) {
			i = LEFT(i); dir = 0; continue; }
		if ( t->m_score > m_nodes[i].m_score ) {
			i = RIGHT(i); dir = 1; continue; }
		// . finally, compare docids, store lower ones first
		// . docids should not tie...
		if ( t->m_docId > m_nodes[i].m_docId ) {
			i = LEFT (i); dir = 0; continue; }
		if ( t->m_docId < m_nodes[i].m_docId ) {
			i = RIGHT(i); dir = 1; continue; }
		// if equal do not replace
		return false;

		/*
		if ( *(unsigned long  *)(t->m_docIdPtr+1) >
		     *(unsigned long  *)(m_nodes[i].m_docIdPtr+1) ) {
			i = LEFT(i); dir = 0; continue; }
		if ( *(unsigned long  *)(t->m_docIdPtr+1) <
		     *(unsigned long  *)(m_nodes[i].m_docIdPtr+1) ) {
			i = RIGHT(i); dir = 1; continue; }
		if ( (*(unsigned char  *)(t->m_docIdPtr)&0xfc) >
		     (*(unsigned char  *)(m_nodes[i].m_docIdPtr)&0xfc) ) {
			i = LEFT(i); dir = 0; continue; }
		if ( (*(unsigned char  *)(t->m_docIdPtr)&0xfc) <
		     (*(unsigned char  *)(m_nodes[i].m_docIdPtr)&0xfc) ) {
			i = RIGHT(i); dir = 1; continue; }
		// IF EQUAL DO NOT REPLACE IT
		return false;
		*/
	}

	//
	// this block of code here makes a new key and adds it to m_t2,
	// and RdbTree. This allows us to keep track of the top 
	// "m_ridiculousMax" domains, and keep them in order of highest
	// to lowest scoring. Without limiting nodes from the same domHash
	// a single domain can easily flood/dominate the TopTree. We are seek
	// a variety of domains to make site clustering as "guaranteed" as
	// possible. If not doing site clustering we could skip this block.
	//

	// debug hack
	//m_ridiculousMax = 2;

	// . make our key the dom tree, m_t2
	// . mask out 0x80 and 0x40 in bscore
	// . WARNING: if t->m_score is fractional, the fraction will be
	//   dropped and could result in the lower scoring of the two docids
	//   being kept.
	uint32_t cs = ((uint32_t)t->m_score);
	key_t k;
	k.n1  =  domHash                 << 24; // 1 byte domHash
	//k.n1 |= (t->m_bscore & ~0xc0)    << 16; // 1 byte bscore
	k.n1 |=  cs                      >> 16; // 4 byte score
	k.n0  =  ((long long)cs)         << (64-16);
	k.n0 |=  t->m_docId; // getDocIdFromPtr ( t->m_docIdPtr );

	// do not add dups
	//long dd = m_t2.getNode ( 0 , k );
	//if ( dd >= 0 ) return false;

	// get min node now for this dom
	long min = m_domMinNode[domHash];
	// the node we add ourselves to
	long n;
	// delete this node
	long deleteMe = -1;
	// do not even try to add if ridiculous count for this domain
	if ( m_domCount[domHash] >= m_ridiculousMax ) {
		// sanity check
		//if ( min < 0 ) { char *xx=NULL; *xx=0; }
		// if we are lesser or dup of min, just don't add!
		if ( k <= *((key_t *)m_t2.getKey(min)) ) return false;
		// . add ourselves. use 0 for collnum.
		// . dataPtr is not really a ptr, but the node
		n = m_t2.addNode ( 0 , k , NULL , 4 );
		//if ( n == 52 )
		//	log("r2 node 52 has domHash=%li",domHash);
		// the next node before the current min will be the next min
		long next = m_t2.getNextNode(min);
		// sanity check
		//if ( next < 0 ) { char *xx=NULL;*xx=0; }
		// sanity check
		//key_t *kp1 = (key_t *)m_t2.getKey(min);
		//if ( (kp1->n1) >>24 != domHash ) {char*xx=NULL;*xx=0;}
		//key_t *kp2 = (key_t *)m_t2.getKey(next);
		//if ( (kp2->n1) >>24 != domHash ) {char*xx=NULL;*xx=0;}
		// the new min is the "next" of the old min
		m_domMinNode[domHash] = next;
		// get his "node number" in the top tree, "nn" so we can
		// delete him from the top tree as well as m_t2. it is 
		// "hidden" in the dataPtr
		deleteMe = (long)m_t2.m_data[min];
		// delete him from the top tree now as well
		//deleteNode ( nn , domHash );
		// then delete him from the m_t2 tree
		m_t2.deleteNode ( min , false );
		//logf(LOG_DEBUG,"deleting1 %li",min);
	}
	// if we have not violated the ridiculous max, just add ourselves
	else if ( m_doSiteClustering ) {
		n = m_t2.addNode ( 0 , k , NULL , 4 );
		//if ( n == 52 )
		//	log("r2 nodeb 52 has domHash=%li",domHash);
		// sanity check
		//if ( min > 0 ) {
		//	key_t *kp1 = (key_t *)m_t2.getKey(min);
		//	if ( (kp1->n1) >>24 != domHash ) {char*xx=NULL;*xx=0;}
		//}
		// are we the new min? if so, assign it
		if ( min == -1 || k < *((key_t *)m_t2.getKey(min)) )
			m_domMinNode[domHash] = n;
	}

	if ( m_doSiteClustering ) {
		// update the dataPtr so every node in m_t2 has a reference
		// to the equivalent node in this top tree
		if ( n < 0 || n > m_t2.m_numNodes ) { char *xx=NULL;*xx=0; }
		m_t2.m_data[n] = (char *)tnn;
	}

	//
	// end special m_t2 code block
	//


	// increment count of domain hash of the docId added
	m_domCount[domHash]++;
	// do not count if over limit
	if      ( m_domCount[domHash] <  m_cap ) m_vcount += 1.0;
	// if equal, count partial
	else if ( m_domCount[domHash] == m_cap ) m_vcount += m_partial;

	// . we were the empty node, get the next in line in the linked list
	// . should be -1 if none left
	m_emptyNode = t->m_right;
	// stick ourselves in the next available node, "m_nextNode"
	t->m_parent = iparent;
	// make our parent, if any, point to us
	if ( iparent >= 0 ) {
		if ( dir == 0 ) LEFT(iparent)  = tnn; // 0
		else            RIGHT(iparent) = tnn; // 1
	}
	// our kids are -1 means none
	t->m_left  = -1;
	t->m_right = -1;
	// our depth is now 1 since we're a leaf node (we include ourself)
	t->m_depth = 1;
	// . reset depths starting at i's parent and ascending the tree
	// . will balance if child depths differ by 2 or more
	setDepths ( iparent );
	// are we the new low node? lower-scoring stuff is on the LEFT!
	if ( iparent == m_lowNode && dir == 0 ) m_lowNode = tnn;
	// count it
	m_numUsedNodes++;

	// we should delete this, it was delayed for the add...
	if ( deleteMe >= 0 ) deleteNode ( deleteMe , domHash );
	// remove as many docids as we should
	while ( m_vcount-1.0 >= m_docsWanted || m_numUsedNodes == m_numNodes) {
		// he becomes the new empty node
		long tn = m_lowNode;
		// sanity check
		if ( tn < 0 ) { char *xx=NULL; *xx=0; }
		// sanity check
		//if ( getNext(tn) == -1 ) { char *xx=NULL;*xx=0; }
		// get the min node
		TopNode *t = &m_nodes[tn];
		// get its docid ptr
		//uint8_t domHash2 = g_titledb.getDomHash8((ui)t->m_docIdPtr);
		uint8_t domHash2 = g_titledb.getDomHash8FromDocId(t->m_docId);
		// . also must delete from m_t2
		// . make the key
		key_t k;
		// WARNING: if t->m_score is fractional, the fraction will be
		// dropped and could result in the lower scoring of the two 
		// docids being kept.
		uint32_t cs = ((uint32_t)t->m_score);
		k.n1  =  domHash2                << 24; // 1 byte domHash
		//k.n1 |= (t->m_bscore & ~0xc0)    << 16; // 1 byte bscore
		k.n1 |=  cs                      >> 16; // 4 byte score
		k.n0  =  ((long long)cs)         << (64-16);
		k.n0 |=  t->m_docId; // getDocIdFromPtr ( t->m_docIdPtr );
		// delete the low node, this might do a rotation
		deleteNode ( tn , domHash2 );

		// the rest is for site clustering only
		if ( ! m_doSiteClustering ) continue;

		// get the node from t2
		long min = m_t2.getNode ( 0 , (char *)&k );
		// sanity check. LEAVE THIS HERE!
		if ( min < 0 ) { break; char *xx=NULL; *xx=0; }
		// sanity check
		//key_t *kp1 = (key_t *)m_t2.getKey(min);
		//if ( (kp1->n1) >>24 != domHash2 ) {char*xx=NULL;*xx=0;}
		// get next node from t2
		long next = m_t2.getNextNode ( min );
		// delete from m_t2
		m_t2.deleteNode ( min , false );
		// skip if not th emin
		if ( m_domMinNode[domHash2] != min ) continue;
		// if we were the last, that's it
		if ( m_domCount[domHash2] == 0 ) {
			// no more entries for this domHash2
			m_domMinNode[domHash2] = -1;
			// sanity check
			//if ( next > 0 ) {
			//key_t *kp2 = (key_t *)m_t2.getKey(next);
			//if ( (kp2->n1) >>24 == domHash2 ) {char*xx=NULL;*xx=0;}
			//}
			continue;
		}
		// sanity check
		//if ( next < 0 ) { char *xx=NULL;*xx=0; }
		// sanity check
		//key_t *kp2 = (key_t *)m_t2.getKey(next);
		//if ( (kp2->n1) >>24 != domHash2 ) {char*xx=NULL;*xx=0;}
		// the new min is the "next" of the old min
		m_domMinNode[domHash2] = next;
		//logf(LOG_DEBUG,"deleting %li",on);
	}
	return true;
}


// . remove this node from the tree
// . used to remove the last node and replace it with a higher scorer
void TopTree::deleteNode ( long i , uint8_t domHash ) {
	// sanity check
	if ( PARENT(i) == -2 ) { char *xx=NULL;*xx=0; }
	// get node
	//TopNode *t = &m_nodes[i];
	// debug
	//if ( ! checkTree ( false ) ) { char *xx = NULL; *xx = 0; }
	//if ( i == 262 )
	//	log("HEY");

	// if it was the low node, update it
	if ( i == m_lowNode ) {
		m_lowNode = getNext ( i );
		if ( m_lowNode == -1 ) { char *xx=NULL;*xx=0; }
	}
	
	// update the vcount
	if      ( m_domCount[domHash] <  m_cap ) m_vcount -= 1.0;
	else if ( m_domCount[domHash] == m_cap ) m_vcount -= m_partial;
	// update the dom count
	m_domCount[domHash]--;
	// debug
	//if ( domHash == 0x35 )
	//	log("top: domCount down for 0x%lx now %li",domHash,m_domCount[domHash]);

	// parent of i
	long iparent ;
	long jparent ;
	// j will be the node that replace node #i
	long j = i;
	// . now find a node to replace node #i
	// . get a node whose key is just to the right or left of i's key
	// . get i's right kid
	// . then get that kid's LEFT MOST leaf-node descendant
	// . this little routine is stolen from getNextNode(i)
	// . try to pick a kid from the right the same % of time as from left
	if ( ( m_pickRight     && RIGHT(j) >= 0 ) || 
	     ( LEFT(j)   < 0 && RIGHT(j) >= 0 )  ) {
		// try to pick a left kid next time
		m_pickRight = 0;
		// go to the right kid
		j = RIGHT ( j );
		// now go left as much as we can
		while ( LEFT ( j ) >= 0 ) j = LEFT ( j );
		// use node j (it's a leaf or has a right kid)
		goto gotReplacement;
	}
	// . now get the previous node if i has no right kid
	// . this little routine is stolen from getPrevNode(i)
	if ( LEFT(j) >= 0 ) {
		// try to pick a right kid next time
		m_pickRight = 1;
		// go to the left kid
		j = LEFT ( j );
		// now go right as much as we can
		while ( RIGHT ( j ) >= 0 ) j = RIGHT ( j );
		// use node j (it's a leaf or has a left kid)
		goto gotReplacement;
	}
	// . come here if i did not have any kids (i's a leaf node)
	// . get i's parent
	iparent = PARENT(i);
	// make i's parent, if any, disown him
	if ( iparent >= 0 ) {
		if   ( LEFT(iparent) == i ) LEFT (iparent) = -1;
		else                        RIGHT(iparent) = -1;
	}
	// empty him
	PARENT(i) = -2;
	// . reset the depths starting at iparent and going up until unchanged
	// . will balance at pivot nodes that need it
	//if ( m_doBalancing ) 
	setDepths ( iparent );

	// debug
	//if ( ! checkTree ( false ) ) { char *xx = NULL; *xx = 0; }

	goto done;

	// . now replace node #i with node #j
	// . i should not equal j at this point
 gotReplacement:

	// . j's parent should take j's one kid
	// . that child should likewise point to j's parent
	// . j should only have <= 1 kid now because of our algorithm above
	// . if j's parent is i then j keeps his kid
	jparent = PARENT(j);
	if ( jparent != i ) {
		// parent:    if j is my left  kid, then i take j's right kid
		// otherwise, if j is my right kid, then i take j's left kid
		if ( LEFT ( jparent ) == j ) {
			LEFT  ( jparent ) = RIGHT ( j );
			if (RIGHT(j)>=0) PARENT ( RIGHT(j) ) = jparent;
		}
		else {
			RIGHT ( jparent ) = LEFT   ( j );
			if (LEFT (j)>=0) PARENT ( LEFT(j) ) = jparent;
		}
	}

	// . j inherits i's children (providing i's child is not j)
	// . those children's parent should likewise point to j
	if ( LEFT (i) != j ) {
		LEFT (j) = LEFT (i);
		if ( LEFT(j) >= 0 ) PARENT(LEFT (j)) = j;
	}
	if ( RIGHT(i) != j ) {
		RIGHT(j) = RIGHT(i);
		if ( RIGHT(j) >= 0 ) PARENT(RIGHT(j)) = j;
	}
	// j becomes the kid of i's parent, if any
	iparent = PARENT(i);
	if ( iparent >= 0 ) {
		if   ( LEFT(iparent) == i ) LEFT (iparent) = j;
		else                        RIGHT(iparent) = j;
	}
	// iparent may be -1
	PARENT(j) = iparent;

	// if i was the head node now j becomes the head node
	if ( m_headNode == i ) m_headNode = j;

	// kill i
	PARENT(i) = -2;

	// return if we don't have to balance
	//if ( ! m_doBalancing ) return;
	// our depth becomes that of the node we replaced, unless moving j
	// up to i decreases the total depth, in which case setDepths() fixes
	DEPTH ( j ) = DEPTH ( i );
	// debug msg
	//fprintf(stderr,"... replaced %li it with %li (-1 means none)\n",i,j);
	// . recalculate depths starting at old parent of j
	// . stops at the first node to have the correct depth
	// . will balance at pivot nodes that need it
	if ( jparent != i ) setDepths ( jparent );
	else                setDepths ( j );

 done:

	// the guy we are deleting is now the first "empty node" and
	// he must link to the old empty node
	m_nodes[i].m_right = m_emptyNode;
	m_emptyNode = i;

	//m_lastKickedOutDocId = m_nodes[i].m_docId;

	// count it
	m_numUsedNodes--;
	// flag it
	m_kickedOutDocIds = true;

	// debug
	//if ( ! checkTree ( true ) ) { char *xx = NULL; *xx = 0; }
}
	
long TopTree::getPrev ( long i ) { 
	// cruise the kids if we have a left one
	if ( LEFT(i) >= 0 ) {
		// go to the left kid
		i = LEFT ( i );
		// now go right as much as we can
		while ( RIGHT ( i ) >= 0 ) i = RIGHT ( i );
		// return that node (it's a leaf or has one left kid)
		return i;
	}
	// now keep getting parents until one has a key bigger than i's key
	long p = PARENT(i);
	// if we're the right kid of the parent, then the parent is the
	// next least node
	if ( RIGHT(p) == i ) return p;
	// if we're the low that's it!
	if ( i == m_lowNode ) return -1;
	// keep getting the parent until it has a bigger key
	// or until we're the RIGHT kid of the parent. that's better
	// cuz comparing keys takes longer. loop is 6 cycles per iteration.
	//while ( p >= 0   &&  m_keys(p) > m_keys(i) ) p = PARENT(p);
	while ( p >= 0   &&  LEFT(p) == i ) { i = p; p = PARENT(p); }
	// p will be -1 if none are left
	return p;
}

long TopTree::getNext ( long i ) {
	// cruise the kids if we have a right one
	if ( RIGHT(i) >= 0 ) {
		// go to the right kid
		i = RIGHT ( i );
		// now go left as much as we can
		while ( LEFT ( i ) >= 0 ) i = LEFT ( i );
		// return that node (it's a leaf or has one right kid)
		return i;
	}
	// now keep getting parents until one has a key bigger than i's key
	long p = PARENT(i);
	// if parent is negative we're done
	if ( p < 0 ) return -1;
	// if we're the left kid of the parent, then the parent is the
	// next biggest node
	if ( LEFT(p) == i ) return p;
	// . if we're the low that's it!
	// . we're only called for getting a new m_lowNode, should never happen
	//if ( i == m_highNode ) return -1;
	// otherwise keep getting the parent until it has a bigger key
	// or until we're the LEFT kid of the parent. that's better
	// cuz comparing keys takes longer. loop is 6 cycles per iteration.
	//while ( p >= 0  &&  m_keys[p] < m_keys[i] ) p = m_parents[p];
	while ( p >= 0   &&  RIGHT(p) == i ) { i = p; p = PARENT(p); }
	// p will be -1 if none are left
	return p;
}

// . recompute depths of nodes starting at i and ascending the tree
// . call rotateRight/Left() when depth of children differs by 2 or more
void TopTree::setDepths ( long i ) {
	// inc the depth of all parents if it changes for them
	while ( i >= 0 ) {
		// . compute the new depth for node i
		// . get depth of left kid
		// . left/rightDepth is depth of subtree on left/right
		long leftDepth  = 0;
		long rightDepth = 0;
		if ( LEFT (i) >= 0 ) leftDepth  = DEPTH ( LEFT (i) ) ;
		if ( RIGHT(i) >= 0 ) rightDepth = DEPTH ( RIGHT(i) ) ;
		// . get the new depth for node i
		// . add 1 cuz we include ourself in our DEPTH
		long newDepth ;
		if ( leftDepth > rightDepth ) newDepth = leftDepth  + 1;
		else                          newDepth = rightDepth + 1;
		// if the depth did not change for i then we're done
		long oldDepth = DEPTH(i) ;
		// set our new depth
		DEPTH(i) = newDepth;
		// diff can be -2, -1, 0, +1 or +2
		long diff = leftDepth - rightDepth;
		// . if it's -1, 0 or 1 then we don't need to balance
		// . if rightside is deeper rotate left, i is the pivot
		// . otherwise, rotate left
		// . these should set the DEPTH(*) for all nodes needing it
		if      ( diff == -2 ) i = rotateLeft  ( i );
		else if ( diff ==  2 ) i = rotateRight ( i );
		// . return if our depth was ultimately unchanged
		// . i may have change if we rotated, but same logic applies
		if ( DEPTH(i) == oldDepth ) break;
		// debug msg
		//fprintf (stderr,"changed node %li's depth from %li to %li\n",
		//i,oldDepth,newDepth);
		// get his parent to continue the ascension
		i = PARENT ( i );
	}
	// debug msg
	//printTree();
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
long TopTree::rotateRight ( long i ) {
	// i's left kid's RIGHT kid takes his place
	long A = i;
	long N = LEFT  ( A );
	long W = LEFT  ( N );
	long X = RIGHT ( N );
	long Q = -1;
	long T = -1;
	if ( X >= 0 ) {
		Q = LEFT  ( X );
		T = RIGHT ( X );
	}
	// let AP be A's parent
	long AP = PARENT ( A );
	// whose the bigger subtree, W or X? (depth includes W or X itself)
	long Wdepth = 0;
	long Xdepth = 0;
	if ( W >= 0 ) Wdepth = DEPTH(W);
	if ( X >= 0 ) Xdepth = DEPTH(X);
	// debug msg
	//fprintf(stderr,"A=%li AP=%li N=%li W=%li X=%li Q=%li T=%li "
	//"Wdepth=%li Xdepth=%li\n",A,AP,N,W,X,Q,T,Wdepth,Xdepth);
	// goto Xdeeper if X is deeper
	if ( Wdepth < Xdepth ) goto Xdeeper;
	// N's parent becomes A's parent
	PARENT ( N ) = AP;
	// A's parent becomes N
	PARENT ( A ) = N;
	// X's parent becomes A
	if ( X >= 0 ) PARENT ( X ) = A;
	// A's parents kid becomes N
	if ( AP >= 0 ) {
		if ( LEFT ( AP ) == A ) LEFT  ( AP ) = N;
		else                    RIGHT ( AP ) = N;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node from %li to %li\n",
		//m_headNode,N);
		m_headNode = N;
	}
	// N's RIGHT kid becomes A
	RIGHT ( N ) = A;
	// A's LEFT  kid becomes X		
	LEFT  ( A ) = X;
	// . compute A's depth from it's X and B kids
	// . it should be one less if Xdepth smaller than Wdepth
	// . might set DEPTH(A) to computeDepth(A) if we have problems
	if ( Xdepth < Wdepth ) DEPTH ( A ) -= 2;
	else                   DEPTH ( A ) -= 1;
	// N gains a depth iff W and X were of equal depth
	if ( Wdepth == Xdepth ) DEPTH ( N ) += 1;
	// now we're done, return the new pivot that replaced A
	return N;
	// come here if X is deeper
 Xdeeper:
	// X's parent becomes A's parent
	PARENT ( X ) = AP;
	// A's parent becomes X
	PARENT ( A ) = X;
	// N's parent becomes X
	PARENT ( N ) = X;
	// Q's parent becomes N
	if ( Q >= 0 ) PARENT ( Q ) = N;
	// T's parent becomes A
	if ( T >= 0 ) PARENT ( T ) = A;
	// A's parent's kid becomes X
	if ( AP >= 0 ) {
		if ( LEFT ( AP ) == A ) LEFT  ( AP ) = X;
		else	                RIGHT ( AP ) = X;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node2 from %li to %li\n",
		//m_headNode,X);
		m_headNode = X;
	}
	// A's LEFT     kid becomes T
	LEFT  ( A ) = T;
	// N's RIGHT    kid becomes Q
	RIGHT ( N ) = Q;
	// X's LEFT     kid becomes N
	LEFT  ( X ) = N;
	// X's RIGHT    kid becomes A
	RIGHT ( X ) = A;
	// X's depth increases by 1 since it gained 1 level of 2 new kids
	DEPTH ( X ) += 1;
	// N's depth decreases by 1
	DEPTH ( N ) -= 1;
	// A's depth decreases by 2
	DEPTH ( A ) -= 2; 
	// now we're done, return the new pivot that replaced A
	return X;
}

// this is the same as above but LEFT and RIGHT are swapped
long TopTree::rotateLeft ( long i ) {
	// i's left kid's LEFT kid takes his place
	long A = i;
	long N = RIGHT  ( A );
	long W = RIGHT  ( N );
	long X = LEFT ( N );
	long Q = -1;
	long T = -1;
	if ( X >= 0 ) {
		Q = RIGHT  ( X );
		T = LEFT ( X );
	}
	// let AP be A's parent
	long AP = PARENT ( A );
	// whose the bigger subtree, W or X? (depth includes W or X itself)
	long Wdepth = 0;
	long Xdepth = 0;
	if ( W >= 0 ) Wdepth = DEPTH(W);
	if ( X >= 0 ) Xdepth = DEPTH(X);
	// debug msg
	//fprintf(stderr,"A=%li AP=%li N=%li W=%li X=%li Q=%li T=%li "
	//"Wdepth=%li Xdepth=%li\n",A,AP,N,W,X,Q,T,Wdepth,Xdepth);
	// goto Xdeeper if X is deeper
	if ( Wdepth < Xdepth ) goto Xdeeper;
	// N's parent becomes A's parent
	PARENT ( N ) = AP;
	// A's parent becomes N
	PARENT ( A ) = N;
	// X's parent becomes A
	if ( X >= 0 ) PARENT ( X ) = A;
	// A's parents kid becomes N
	if ( AP >= 0 ) {
		if ( RIGHT ( AP ) == A ) RIGHT  ( AP ) = N;
		else                    LEFT ( AP ) = N;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node from %li to %li\n",
		//m_headNode,N);
		m_headNode = N;
	}
	// N's LEFT kid becomes A
	LEFT ( N ) = A;
	// A's RIGHT  kid becomes X		
	RIGHT  ( A ) = X;
	// . compute A's depth from it's X and B kids
	// . it should be one less if Xdepth smaller than Wdepth
	// . might set DEPTH(A) to computeDepth(A) if we have problems
	if ( Xdepth < Wdepth ) DEPTH ( A ) -= 2;
	else                   DEPTH ( A ) -= 1;
	// N gains a depth iff W and X were of equal depth
	if ( Wdepth == Xdepth ) DEPTH ( N ) += 1;
	// now we're done, return the new pivot that replaced A
	return N;
	// come here if X is deeper
 Xdeeper:
	// X's parent becomes A's parent
	PARENT ( X ) = AP;
	// A's parent becomes X
	PARENT ( A ) = X;
	// N's parent becomes X
	PARENT ( N ) = X;
	// Q's parent becomes N
	if ( Q >= 0 ) PARENT ( Q ) = N;
	// T's parent becomes A
	if ( T >= 0 ) PARENT ( T ) = A;
	// A's parent's kid becomes X
	if ( AP >= 0 ) {
		if ( RIGHT ( AP ) == A ) RIGHT  ( AP ) = X;
		else	                LEFT ( AP ) = X;
	}
	// if A had no parent, it was the headNode
	else {
		//fprintf(stderr,"changing head node2 from %li to %li\n",
		//m_headNode,X);
		m_headNode = X;
	}
	// A's RIGHT     kid becomes T
	RIGHT  ( A ) = T;
	// N's LEFT    kid becomes Q
	LEFT ( N ) = Q;
	// X's RIGHT     kid becomes N
	RIGHT  ( X ) = N;
	// X's LEFT    kid becomes A
	LEFT ( X ) = A;
	// X's depth increases by 1 since it gained 1 level of 2 new kids
	DEPTH ( X ) += 1;
	// N's depth decreases by 1
	DEPTH ( N ) -= 1;
	// A's depth decreases by 2
	DEPTH ( A ) -= 2; 
	// now we're done, return the new pivot that replaced A
	return X;
}

// returns false if tree had problem, true otherwise
bool TopTree::checkTree ( bool printMsgs ) {
	// now check parent kid correlations
	for ( long i = 0 ; i < m_numUsedNodes ; i++ ) {
		// skip node if parents is -2 (unoccupied)
		if ( PARENT(i) == -2 ) continue;
		// if no left/right kid it MUST be -1
		if ( LEFT(i) < -1 )
			return log("query: toptree: checktree: left "
				   "kid %li < -1",i);
		if ( RIGHT(i) < -1 )
			return log("query: toptree: checktree: right "
				   "kid %li < -1",i);
		// check left kid
		if ( LEFT(i) >= 0 && PARENT(LEFT(i)) != i ) 
			return log("query: toptree: checktree: tree has "
				   "error %li",i);
		// then right kid
		if ( RIGHT(i) >= 0 && PARENT(RIGHT(i)) != i )
                       return log("query: toptree: checktree: tree has "
				  "error2 %li",i);
	}
	// now return if we aren't doing active balancing
	//if ( ! DEPTH ) return true;
	// debug -- just always return now
	if ( printMsgs ) log("***m_headNode=%li, m_numUsedNodes=%li",
			      m_headNode,m_numUsedNodes);
	// verify that parent links correspond to kids
	for ( long i = 0 ; i < m_numUsedNodes ; i++ ) {
		long P = PARENT (i);
		if ( P == -2 ) continue; // deleted node
		if ( P == -1 && i != m_headNode ) 
			return log("query: toptree: checktree: node %li has "
				   "no parent",i);
		// check kids
		if ( P>=0 && LEFT(P) != i && RIGHT(P) != i ) 
			return log("query: toptree: checktree: node %li's "
				    "parent disowned",i);
		// ensure i goes back to head node
		long j = i;
		while ( j >= 0 ) { 
			if ( j == m_headNode ) break;
			j = PARENT(j);
		}
		if ( j != m_headNode ) 
			return log("query: toptree: checktree: node %li's no "
				   "head node above",i);
		if ( printMsgs ) 
			fprintf(stderr,"***node=%li left=%li rght=%li "
				"prnt=%li, depth=%li\n",
				i,LEFT(i),RIGHT(i),PARENT(i),
				(long)DEPTH(i));
		//ensure depth
		long newDepth = computeDepth ( i );
		if ( DEPTH(i) != newDepth ) 
			return log("query: toptree: checktree: node %li's "
				   "depth should be %li",i,newDepth);
	}
	if ( printMsgs ) log("query: ---------------");
	// no problems found
	return true;
}

// . depth of subtree with i as the head node
// . includes i, so minimal depth is 1
long TopTree::computeDepth ( long i ) {
	long leftDepth  = 0;
	long rightDepth = 0;
	if ( LEFT (i) >= 0 ) leftDepth  = DEPTH ( LEFT (i) ) ;
	if ( RIGHT(i) >= 0 ) rightDepth = DEPTH ( RIGHT(i) ) ;
	// . get the new depth for node i
	// . add 1 cuz we include ourself in our DEPTH
	if ( leftDepth > rightDepth ) return leftDepth  + 1;
	else                          return rightDepth + 1;  
}	

bool TopTree::hasDocId ( long long d ) {
	long i = getLowNode ( );
	// scan the nodes
	for ( ; i >= 0 ; i = getNext ( i ) ) {
		//if ( PARENT(i) == -2 ) continue; // deleted node
		if ( m_nodes[i].m_docId == d ) return true; 
	}
	return false;
}
