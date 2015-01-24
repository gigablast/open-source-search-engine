#include "gb-include.h"

#include "Vector.h"
#include "Words.h"
#include "sort.h"
#include "Sections.h"

// this is used to sort the word hashes in setPairHashes()
int cmp ( const void *h1 , const void *h2 ) ;

Vector::Vector ( ) {
	reset();
}

void Vector::reset ( ) {
	//m_init           = false;
	//m_numRemoteLinks = 0;
	m_numPairHashes  = 0;
	//m_pairHashes     = NULL;
	//m_table.reset();
}

// . set our m_pairHashes[] array
// . these arrays are the vector
// . used to compare documents
// . "links" class must have been set with "setLinkHashes" set to true
/*
bool Vector::set ( Xml *xml , Links *links , Url *url , int32_t linkNode ,
		   char *buf , int32_t bufSize ) {
	// reset all
	reset();
	if ( ! links ) setPathComponentHashes ( url , buf , bufSize );
	// set m_pairHashes and m_linkHashes arrays
	else           setPairHashes ( xml , linkNode , buf , bufSize );
	//setLinkHashes     ( links , url );
	return true;
}
*/

bool Vector::setPathComponentHashes ( Url *url ){//,char *buf,int32_t bufSize ) {

	reset();

	m_numPairHashes = 0;
	// use the provided buffer
	//m_pairHashes = (uint32_t *)buf;

	char *p    = url->getPath();
	int32_t  plen = url->getPathLen();
	char *pend = p + plen;
	// save ptr
	char *last = p;
	// skip initial /
	p++;
	// do the component loop
	for ( ; ; p++ ) {
		// keep trucking if not an endpoint
		if ( p < pend && *p != '/' ) continue;
		// hash count
		int32_t k = 0;
		// hash it
		int32_t h = 0;
		for ( char *q = last ; q < p ; q++ ) {
			// skip if not alnum
			if ( ! is_alnum ( *q ) ) continue;
			// otherwise, hash it (taken from hash.cpp)
			h ^= (uint32_t)g_hashtab[(unsigned char)k++]
				[(int)to_lower((unsigned char)*q)];
		}
		// store that
		m_pairHashes[m_numPairHashes++] = h;
		// do not breech the buffer
		if ( m_numPairHashes >= MAX_PAIR_HASHES ) break;
	}
	// . TODO: remove the link text hashes here?
	// . because will probably be identical..
	// . now sort hashes to get the top MAX_PAIR_HASHES
	gbsort ( m_pairHashes , m_numPairHashes , 4 , cmp );
	// sanity check
	//if ( m_numPairHashes * 4 > bufSize ) {
	//	log("build: Vector: Buf not big enough 3.");
	//	char *xx = NULL; *xx = 0;
	//}
	return true;
}

bool Vector::setTagPairHashes ( Xml *xml , // char *buf , int32_t bufSize ,
				int32_t niceness ) {
	// store the hashes here
	uint32_t hashes [ 2000 ];
	int32_t          nh = 0;
	// go through each node
	XmlNode *nodes = xml->getNodes    ();
	int32_t   n       = xml->getNumNodes ();

	// start with the ith node
	int32_t i = 0;

	uint32_t saved = 0;
	uint32_t lastHash = 0;
	// loop over the nodes
	for ( ; i < n ; i++ ) {
		// breathe a little
		QUICKPOLL(niceness);
		// skip NON tags
		if ( ! nodes[i].isTag() ) continue;
		// use the tag id as the hash, its unique
		uint32_t h = hash32 ( nodes[i].getNodeId() , 0 );
		// ensure hash is not 0, that has special meaning
		if ( h == 0 ) h = 1;
		// store in case we have only one hash
		saved = h;

		// if we are the first, set this
		if ( ! lastHash ) {
			lastHash = h;
			continue;
		}

		// if they were the same do not xor, they will zero out
		if ( h == lastHash ) hashes[nh++] = h;
		// incorporate it into the last hash
		else                 hashes[nh++] = h ^ lastHash;
		
		// we are the new last hash
		lastHash = h;
		// bust out if no room
		if ( nh >= 2000 ) break;
	}
	// if only had one tag after, use that
	if ( nh == 0 && saved ) hashes[nh++] = saved;

	QUICKPOLL ( 0 ) ;
	// . TODO: remove the link text hashes here?
	// . because will probably be identical..
	// . now sort hashes to get the top MAX_PAIR_HASHES
	gbsort ( hashes , nh , 4 , cmp );
	// uniquify them
	int32_t d = 0;
	for ( int32_t j = 1 ; j < nh ; j++ ) { 
		if ( hashes[j] == hashes[d] ) continue;
		hashes[++d] = hashes[j];
	}
	nh = d;
	// truncate to MAX_PAIR_HASHES
	if ( nh > MAX_PAIR_HASHES ) nh = MAX_PAIR_HASHES;
	// save it
	m_numPairHashes = nh;
	// sanity check
	//if ( nh * 4 > bufSize ) {
	//	log("build: Vector: Buf not big enough.");
	//	char *xx = NULL; *xx = 0;
	//}
	// use the provided buffer
	//m_pairHashes = (uint32_t *)buf;
	QUICKPOLL ( 0 ) ;
	// store the top MAX_PAIR_HASHES
	gbmemcpy ( m_pairHashes , hashes , nh * 4 );
	return true;
}


// . hash all pairs of words (words that are adjacent only)
// . get top X hashes to store as the word pair vector
bool Vector::setPairHashes ( Words *words, int32_t linkWordNum, int32_t niceness ) {
	// are we in a <a href> tag?
	//bool inHref = false;
	// store the hashes here
	uint32_t hashes [ 3000 ];
	int32_t          nh = 0;
	// go through each word
	int32_t nw = words->getNumWords();

	// int16_tcut
	nodeid_t *tids = words->getTagIds();

	// start with the ith word
	int32_t i = 0;
	// linkNode starts pointing to a <a> tag so skip over that!
	if ( linkWordNum >= 0 ) i = linkWordNum + 1;
	// and advance i to the next anchor tag thereafter, we do not
	// want to include link text in this vector because it is usually
	// repeated and will skew our "similarities"
	for ( ; linkWordNum >= 0 && i < nw ; i++ )
		// keep going until we git a </a> or <a>
		if ( (tids[i]&BACKBITCOMP) != TAG_A ) { i++; break; }

	uint32_t saved = 0;
	// loop over the nodes
	for ( ; i < nw ; i++ ) {
		// breathe a little
		QUICKPOLL(niceness);
		// skip if punct
		if ( ! wids[i] ) continue;
		// skip tags
		if ( tids[i] ) {
			// just skip the tag if we have no link
			if ( linkWordNum < 0 ) continue;
			// if we got a linkNode, stop gathering hashes once
			// we hit one of these tags. we just want the text 
			// immediately after the link text and no more.
			// NOTE: if you add more tag ids to this list then
			// you should add them to LinkText::isLinkSpam()'s
			// rightText/leftText setting algo, too.
			nodeid_t id = tids[i] & BACKBITCOMP;
			// <table> or </table>
			if ( id == 93 ) break; 
			// <ul> or </ul>
			if ( id == 105 ) break; 
			// <p> or </p>
			//if ( id == 75 ) break;
			// <tr> or </tr>
			//if ( id == 102 ) break; 
			// <a>, </a> is ok. but if it is <a>, break
			if ( tids[i] == 2 ) break;
			// if we did not match a swtich, ignore the tag
			continue;
		}
		
		// if word was only 2 or less letters, skip it, it's not
		// very representative of the content
		if ( wlens[i] <= 2 && linkWordNum < 0 ) goto loop;

		// store in case we have only one hash
		h = saved = (uint32_t)wids[i];

		// debug: print out each word... very handy
		/*
		if ( linkNode >= 0 ) {
			char ttt[300];
			char *pt = ttt;
			int32_t len = p - pstart;
			if ( len > 290 ) len = 290;
			for ( int32_t i = 0 ; i < len ; i++ ) 
				if ( pstart[i] ) *pt++ = pstart[i];
			*pt = '\0';
			//gbmemcpy ( ttt , pstart , len );
			ttt[len] = '\0';
			uint32_t hh = h;
			if ( lastHash && h != lastHash ) hh = h ^ lastHash;
			log ("vec hash %"INT32" %s = %"UINT32" [%"UINT32"]",
			     nh-1,ttt,h,hh);
		}
		*/

		// if we are the first, set this
		if ( ! lastHash ) {
			lastHash = h;
			goto loop;
		}

		// if they were the same do not xor, they will zero out
		if ( h == lastHash ) hashes[nh++] = h;
		// incorporate it into the last hash
		else                 hashes[nh++] = h ^ lastHash;

	
		// we are the new last hash
		lastHash = h;
		// return if no more room, we stop at 100 for the after
		// link vector hashes though to keep things efficient
		if ( linkNode >= 0 && nh >= 100 ) break;
		// return if no more room
		if ( nh < 3000 ) goto loop;
		// otherwise bust out
		break;
	}
	// if only had one word after, use that
	if ( nh == 0 && saved ) hashes[nh++] = saved;

	// . TODO: remove the link text hashes here?
	// . because will probably be identical..
	// . now sort hashes to get the top MAX_PAIR_HASHES
	gbsort ( hashes , nh , 4 , cmp );
	// truncate to MAX_PAIR_HASHES
	if ( nh > MAX_PAIR_HASHES ) nh = MAX_PAIR_HASHES;
	// save it
	m_numPairHashes = nh;
	// sanity check
	//if ( nh * 4 > bufSize ) {
	//	log("build: Vector: Buf not big enough.");
	//	char *xx = NULL; *xx = 0;
	//}
	// use the provided buffer
	//m_pairHashes = (uint32_t *)buf;
	// store the top MAX_PAIR_HASHES
	gbmemcpy ( m_pairHashes , hashes , nh * 4 );
	return true;
}

// sort in descending order
int cmp ( const void *h1 , const void *h2 ) {
	return *(uint32_t *)h2 - *(uint32_t *)h1;
}

/*
// . TODO: use links->getDomHash(i) not getLinkHash() DOES NOT WORK NO MORE!!!
// . get the 20 longest links on this page
// . do not include links from the same domain
// . "links" class must have been set with "setLinkHashes" set to true
bool Vector::setLinkHashes ( Links *links , Url *url ) {
	// get our url's domain hash
	uint32_t h = hash32 ( url->getDomain() , url->getDomainLen() );
	// how many links?
	int32_t n = links->getNumLinks();
	// store hashes of all non-local links here
	uint32_t hashes[3000];
	int32_t          nh  = 0;
	// get top 20
	for ( int32_t i = 0 ; i < n && nh < 3000 ; i++ ) {
		// skip if from same domain, we just want external links
		if ( links->getLinkHash(i) == h ) continue;
		// . save it
		//hashes [ nh++ ] = links->getLinkHash(i);
		hashes [ nh++ ] = links->getDomHash(i);
	}
	// sort hashes to get the top MAX_LINK_HASHES
	gbsort ( hashes , nh , 4 , cmp );
	// remove duplicate url hashes
	int32_t k = 0;
	for ( int32_t i = 1 ; i < nh ; i++ )
		if ( hashes[i] != hashes[k] ) hashes[++k] = hashes[i];
	// possibly adjust the # of link hashes after de-duping them
	nh = k;
	// save total # of non-local, distinct links
	m_numRemoteLinks = nh;
	// truncate to MAX_LINK_HASHES
	if ( nh > MAX_PAIR_HASHES ) nh = MAX_PAIR_HASHES;
	// save it
	m_numPairHashes = nh;
	// store the top MAX_LINK_HASHES
	gbmemcpy ( m_pairHashes , hashes , nh * 4 );
	return true;
}
*/

/*
int32_t Vector::getStoredSize ( ) {
	return 4 + m_numPairHashes * 4 ;
}

// return bytes read from
int32_t Vector::set   ( char *buf , int32_t bufMaxSize ) {
	char *p = buf;
	m_numPairHashes  = *(int32_t *)p; p += 4;
	//m_numRemoteLinks = *(int32_t *)p; p += 4;

	//gbmemcpy ( m_pairHashes , p , m_numPairHashes * 4 );
	m_pairHashes = (uint32_t *)p;
	p += m_numPairHashes * 4;

	// sanity check
	if ( p - buf > bufMaxSize ) { char *xx = NULL; *xx = 0; }

	return p - buf;
}

int32_t Vector::set2  ( char *buf , int32_t numPairHashes ) {
	char *p = buf;
	m_numPairHashes  = numPairHashes;
	//m_numRemoteLinks = *(int32_t *)p; p += 4;

	//gbmemcpy ( m_pairHashes , p , m_numPairHashes * 4 );
	m_pairHashes = (uint32_t *)p;
	p += m_numPairHashes * 4;

	// sanity check
	//if ( p - buf > bufMaxSize ) { char *xx = NULL; *xx = 0; }

	return p - buf;
}

// return bytes stored
int32_t Vector::store ( char *buf , int32_t bufMaxSize ) {
	char *p = buf;
	*(int32_t *)p = m_numPairHashes;  p += 4;
	// *(int32_t *)p = m_numRemoteLinks; p += 4;

	gbmemcpy ( p , m_pairHashes , m_numPairHashes * 4 );
	p += m_numPairHashes * 4;

	return p - buf;
}
*/

// return the percent similar
int32_t getSimilarity ( Vector *v0 , Vector *v1 ) {
	// . the hashes are sorted
	// . point each recs sample vector of termIds
	// . we sorted them above as uint32_ts, so we must make sure
	//   we use uint32_ts here, too
	uint32_t *t0 = (uint32_t *)v0->m_pairHashes;
	uint32_t *t1 = (uint32_t *)v1->m_pairHashes;
	// get the ends of each vector
	uint32_t *end0 = t0 + v0->m_numPairHashes;
	uint32_t *end1 = t1 + v1->m_numPairHashes;
	// if both are empty, 100% similar
	if ( t0 >= end0 && t1 >= end1 ) return 100;
	// if either is empty, return 0 to be on the safe side
	if ( t0 >= end0 ) return 0;
	if ( t1 >= end1 ) return 0;
	// count matches between the sample vectors
	int32_t count = 0;
 loop:
	// each vector is sorted, so comparison is like a merge sort
	if      ( *t0 < *t1 ) { if ( *++t0 == 0 ) goto done; }
	else if ( *t1 < *t0 ) { if ( *++t1 == 0 ) goto done; }
	else    { 
		if ( t0 >= end0 ) goto done;
		count++; 
		t0++;
		t1++;
		if ( t0 >= end0 ) goto done;
		if ( t1 >= end1 ) goto done;
	}
	goto loop;

 done:
	// count total components in each sample vector
	int32_t total = 0;
	total += v0->m_numPairHashes;
	total += v1->m_numPairHashes;

	int32_t sim = (count * 2 * 100) / total;
	if ( sim > 100 ) sim = 100;
	return sim;
}


// . return from 0% to 100% spam rating
// . returns -1 and sets g_errno on error
// . is this page with repesentative vector, v, a link-farm brother of us?
// . if removeMatches is true we remove matching word pairs from "v"
int32_t Vector::getLinkBrotherProbability ( Vector *v , bool removeMatches ) {

	// bail if we hashed nothing
	if ( m_numPairHashes == 0 ) return 0;
	// each slot is a 4-byte key and a 4-byte value, so 8000 bytes can
	// do 1000 slots...
	char tbuf[8000];
	HashTable ht;
	// tbuf[] can do 1000 slots
	int32_t slots = 1000;
	// avoid calling bzero on 8k if we have fewer things to hash
	if ( m_numPairHashes * 2 < slots ) slots = m_numPairHashes * 2;
	// initialize it to this many slots
	if ( ! ht.set ( slots , tbuf , 8000 ) ) return -1;
	// hash all word pair hashes into the table
	for ( int32_t i = 0 ; i < m_numPairHashes ; i++ ) 
		ht.addKey ( m_pairHashes [i], 1 );
	// count matches
	int32_t c1 = 0;
	// vars for hashing
	int32_t           n ;
	uint32_t *h ;
	// . what word pairs does "v" have that we also have?
	// . TODO: speed up by making hash table use int32_t instead of int64_t
	n    = v->m_numPairHashes;
	h    = v->m_pairHashes;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// termNum into table
		int32_t slot = ht.getSlot ( h[i] );
		// if empty...
		if ( slot < 0 ) continue;
		// get score
		uint32_t score = ht.getValueFromSlot ( slot );
		// don't count if empty
		//if ( score == 0 ) continue;
		// don't count if it was marked
		if ( score == 0x7fffffff ) continue;
		// count the match
		c1++;
		// remove it from table so it's not counted again
		if ( removeMatches ) ht.setValue ( slot , 0x7fffffff );
	}

	// what external links does "v" have that we also have?
	//int32_t c2 = 0;
	//n    = v->m_numLinkHashes;
	//h    = v->m_linkHashes;
	//for ( int32_t i = 0 ; i < n ; i++ ) 
	//	if ( m_table.getScoreFromTermId ( h[i] ) != 0 ) c2++;

	// . chances of the doc having the same random word pair as us
	//   is quite small... (we excluded words w/ 2 or less letters)
	//   probably around .01% (1 in 10,000) but we can and often do
	//   select popular word pairs...
	// . "every driver" --> 2,731 / 150,000,000
	// . "same time" --> 268,000
	// . "give people" --> 34,000
	// . "entrepeneurs build" --> 2,730
	// . "view ourselves" -->2,730

	// convert # of matched word pairs to probability of link-spam brothers
	int32_t p1 = 0;

	// get min # stored word pair hashes from each doc
	int32_t min = v->m_numPairHashes;
	if ( m_numPairHashes < min ) min = m_numPairHashes;
	
	// . if all are shared, that's 100% probability,
	// . if only X% are shared, that's X% probability
	if ( min > 0 ) p1 = (100 * c1) / min ;
	
	// truncate to 100 if it's too big
	if ( p1 > 100 ) p1 = 100;

	// ensure never < 0, that means error
	if ( p1 < 0 ) p1 = 0;

	// if only 3 in common, could be coincidence
	//if ( c1 <= 3 ) p1 = 0;

	//switch (c1) {
	//	case 3: p1 = 10; break;
	//	case 4: p1 = 15; break;
	//	case 5: p1 = 30; break;
	//	case 6: p1 = 50; break;
	//	case 7: p1 = 70; break;
	//	case 8: p1 = 90; break;
	//}
	//if ( c1 >= 9 ) p1 = 100;
	// if it's 100% return that now
	//if ( p1 >= 100 ) return 100;

	return p1;

	// 1% probability of spam for each common link above 8
	//int32_t p2 = (c2 - 5) * 5;
	//if ( p2 < 0 ) p2 = 0;

	// return the max of the 2 probabilities
	//if ( p1 > p2 ) return p1;
	//return p2;
}


uint32_t Vector::getVectorHash() {
	uint32_t h = 0;
	for(int32_t i = 0; i < m_numPairHashes; i++) {
		h ^= m_pairHashes[i];
	}
	return h;
}
