#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#include "RdbBuckets.h"
#include "gb-include.h"
#include "sort.h"
#include "SafeBuf.h"
#include "Threads.h"
#include <unistd.h>
#include <sys/stat.h>
#include "Loop.h"
#include "Rdb.h"

//#define BUCKET_SIZE 64
//#define BUCKET_SIZE 1024
//#define BUCKET_SIZE 2048
//#define BUCKET_SIZE 4096
#define BUCKET_SIZE 8192
//#define BUCKET_SIZE 16384
//#define BUCKET_SIZE 1638400
#define INIT_SIZE 4096
#define SAVE_VERSION 0


inline int KEYCMP12 ( const void *a, const void *b ) {
	char* k1 = (char*)a;
	char* k2 = (char*)b;
	if ( (*(unsigned long long *)(k1+4)) <
	     (*(unsigned long long *)(k2+4)) ) return -1;
	if ( (*(unsigned long long *)(k1+4)) > 
	     (*(unsigned long long *)(k2+4)) ) return  1;
	unsigned long k1n0 = ((*(unsigned long*)(k1)) & ~0x01UL);
	unsigned long k2n0 = ((*(unsigned long*)(k2)) & ~0x01UL);
	if ( k1n0 < k2n0 ) return -1;
	if ( k1n0 > k2n0 ) return  1;
	return 0;
}



inline int KEYCMP16 ( const void *a, const void *b ) {
	char* k1 = (char*)a;
	char* k2 = (char*)b;
	if ( (*(unsigned long long *)(k1+8)) <
	     (*(unsigned long long *)(k2+8)) ) return -1;
	if ( (*(unsigned long long *)(k1+8)) >
	     (*(unsigned long long *)(k2+8)) ) return  1;
	unsigned long long k1n0 = ((*(unsigned long long *)(k1)) & ~0x01ULL);
	unsigned long long k2n0 = ((*(unsigned long long *)(k2)) & ~0x01ULL);
	if ( k1n0 < k2n0 ) return -1;
	if ( k1n0 > k2n0 ) return  1;
	return 0;
}

inline int KEYCMP18 ( const void *a, const void *b ) {
	char* k1 = (char*)a;
	char* k2 = (char*)b;
	if ( (*(unsigned long long *)(k1+10)) <
	     (*(unsigned long long *)(k2+10)) ) return -1;
	if ( (*(unsigned long long *)(k1+10)) >
	     (*(unsigned long long *)(k2+10)) ) return  1;
	if ( (*(unsigned long long *)(k1+2)) <
	     (*(unsigned long long *)(k2+2)) ) return -1;
	if ( (*(unsigned long long *)(k1+2)) >
	     (*(unsigned long long *)(k2+2)) ) return  1;
	unsigned short k1n0 = ((*(unsigned short *)(k1)) & 0xfffe);
	unsigned short k2n0 = ((*(unsigned short *)(k2)) & 0xfffe);
	if ( k1n0 < k2n0 ) return -1;
	if ( k1n0 > k2n0 ) return  1;
	return 0;
}

inline int KEYCMP24 ( const void *a, const void *b ) {
	char* k1 = (char*)a;
	char* k2 = (char*)b;
	if ( (*(unsigned long long *)(k1+16)) <
	     (*(unsigned long long *)(k2+16)) ) return -1;
	if ( (*(unsigned long long *)(k1+16)) >
	     (*(unsigned long long *)(k2+16)) ) return  1;
	if ( (*(unsigned long long *)(k1+8)) <
	     (*(unsigned long long *)(k2+8)) ) return -1;
	if ( (*(unsigned long long *)(k1+8)) >
	     (*(unsigned long long *)(k2+8)) ) return  1;
	unsigned long long k1n0 = ((*(unsigned long long *)(k1)) & ~0x01ULL);
	unsigned long long k2n0 = ((*(unsigned long long *)(k2)) & ~0x01ULL);
	if ( k1n0 < k2n0 ) return -1;
	if ( k1n0 > k2n0 ) return  1;
	return 0;
}

inline int KEYCMP6 ( const void *a, const void *b ) {
	char* k1 = (char*)a;
	char* k2 = (char*)b;
	if ( (*(unsigned long  *)(k1+2)) <
	     (*(unsigned long  *)(k2+2)) ) return -1;
	if ( (*(unsigned long  *)(k1+2)) >
	     (*(unsigned long  *)(k2+2)) ) return  1;
	if ( (*(unsigned short *)(k1+0)) <
	     (*(unsigned short *)(k2+0)) ) return -1;
	if ( (*(unsigned short *)(k1+0)) >
	     (*(unsigned short *)(k2+0)) ) return  1;
	return 0;
}






bool RdbBucket::set(RdbBuckets* parent, char* newbuf) {
	m_endKey = NULL;
	m_parent = parent;
	m_lastSorted = 0;
	m_numKeys = 0;
	m_keys = newbuf;
	return true;
}


void RdbBucket::reBuf(char* newbuf) {
	if(!m_keys) {
		m_keys = newbuf;
		return;
	}
	memcpy(newbuf, m_keys, m_numKeys * m_parent->getRecSize());
	if(m_endKey) m_endKey = newbuf + (m_endKey - m_keys);
	m_keys = newbuf;
}




RdbBucket::~RdbBucket() {
	reset();
}


void RdbBucket::reset() {
	//m_keys = NULL;
	m_numKeys = 0;
	m_lastSorted = 0;
	m_endKey = NULL;
}

long RdbBuckets::getMemAlloced () {
	long alloced = sizeof(RdbBuckets) + m_masterSize + m_dataMemOccupied;
	return alloced;
}

//includes data in the data ptrs
long RdbBuckets::getMemOccupied() {
	return (m_numKeysApprox * m_recSize) + m_dataMemOccupied +
		sizeof(RdbBuckets) + 
		m_sortBufSize + 
		BUCKET_SIZE * m_recSize; //swapbuf
}


long RdbBuckets::getMemAvailable() {
	return m_maxMem - getMemOccupied();
}


bool RdbBuckets::is90PercentFull() {
	return getMemOccupied () > m_maxMem * .9;
}

bool RdbBuckets::needsDump() {
	if(m_numBuckets + 1 < m_maxBuckets) return false;
	if(m_maxBuckets == m_maxBucketsCapacity) return true;
	return false;
}

//be very conservative with this because if we say we can fit it
//and we can't then we'll get a partial list added and we will
//add the whole list again.
bool RdbBuckets::hasRoom ( long numRecs ) {
	long numBucketsRequired = (((numRecs / BUCKET_SIZE)+1) * 2);
	if(m_maxBucketsCapacity - m_numBuckets < numBucketsRequired) 
		return false;
	return true;
}



bool RdbBucket::sort() {

	//m_lastSorted = 0;//for debugging
	if(m_lastSorted == m_numKeys) return true;


	if(m_numKeys < 2) {
		m_lastSorted = m_numKeys;
		return true;		
	}

	uint8_t ks = m_parent->getKeySize();
	long recSize = m_parent->getRecSize();
	long fixedDataSize = m_parent->getFixedDataSize();
	int (*cmpfn) (const void*, const void *) = NULL;
	if ( ks == 18 ) cmpfn = KEYCMP18;
	else if ( ks == 24 ) cmpfn = KEYCMP24;
	else if ( ks == 12 ) cmpfn = KEYCMP12;
	else if ( ks == 16 ) cmpfn = KEYCMP16;
	else if ( ks ==  6 ) cmpfn = KEYCMP6;
	else { char *xx=NULL;*xx=0; }

	char* mergeBuf  = m_parent->getSwapBuf();
	if(!mergeBuf) {	char* xx = NULL; *xx = 0; }

	long numUnsorted = m_numKeys - m_lastSorted;
	char *list1  = m_keys;
	char *list2  = m_keys + (recSize*m_lastSorted);
	char *list1end  = list2;
	char *list2end  = list2 + (recSize * numUnsorted);
	//turn quickpoll off while we sort,
	//because we do not know what sort of indeterminate state
	//we will be in while sorting
	// MDW: this no longer disables it since it is based on g_niceness
	// now, but what is the point, does it use static vars or what?
	//bool canQuickpoll = g_loop.m_canQuickPoll;
	//g_loop.m_canQuickPoll = false;
	//sort the unsorted portion
	// turn off this way
	long saved = g_niceness;
	g_niceness = 0;
	// . use merge sort because it is stable, and we need to always keep
	// . the identical keys that were added last
	// . now we pass in a buffer to merge into, otherwise one is malloced,
	// . which can fail.  It falls back on qsort which is not stable.
	if(!m_parent->getSortBuf()) {char *xx = NULL; *xx = 0;}
	gbmergesort (list2, numUnsorted , recSize , cmpfn, 0,
		     m_parent->getSortBuf(), m_parent->getSortBufSize()); 
	
	//g_loop.m_canQuickPoll = canQuickpoll;
	g_niceness = saved;

	char *p  = mergeBuf;
	char v;
	char *lastKey = NULL;
	long br = 0; //bytesRemoved (abbreviated for column width)
	long dso = ks + sizeof(char*);//datasize offset
	long numNeg = 0;

	while(1) {
		if(list1 >= list1end) {
			// . just copy into place, deduping as we go
			while(list2 < list2end) {
				if(lastKey && KEYCMPNEGEQ(list2,lastKey,ks) == 0) {
					//this is a dup, we are removing data
					if(fixedDataSize != 0) {
					      if(fixedDataSize == -1) 
					           br += *(long*)(lastKey+dso);
					      else br += fixedDataSize;
					}
					if ( KEYNEG(lastKey) ) numNeg++;
					p = lastKey;
				}
				memcpy(p, list2, recSize);
				lastKey = p;
				p += recSize;
				list2 += recSize;
			}

			break;
		}
		if(list2 >= list2end) {
			// . if all that is left is list 1 just copy it into 
			// . place, since it is already deduped
			memcpy(p, list1, list1end - list1);
			p += list1end - list1;
			break;
		}
		v = KEYCMPNEGEQ(list1, list2, ks); 
		if(v < 0) {
			//never overwrite the merged list from list1 because
			//it is always older and it is already deduped
			if(lastKey && KEYCMPNEGEQ(list1, lastKey, ks) == 0) {
				if ( KEYNEG(lastKey) ) numNeg++;
				list1 += recSize;
				continue;
			}
			memcpy(p, list1, recSize);
			lastKey = p;
			p += recSize;
			list1 += recSize;
		}
		else if(v > 0) {
			//copy it over the one we just copied in
			if(lastKey && KEYCMPNEGEQ(list2, lastKey, ks) == 0) {
				//this is a dup, we are removing data
				if(fixedDataSize != 0) {
					if(fixedDataSize == -1) 
						br += *(long*)(lastKey+dso);
					else br += fixedDataSize;
				}
				if ( KEYNEG(lastKey) ) numNeg++;
				p = lastKey;
			}

			memcpy(p, list2, recSize);
			lastKey = p;
			p += recSize;
			list2 += recSize;
		}
		else {
			if(lastKey && KEYCMPNEGEQ(list2, lastKey, ks) == 0) {
				if(fixedDataSize != 0) {
					if(fixedDataSize == -1) 
						br += *(long*)(lastKey+dso);
					else br += fixedDataSize;
				}
				if ( KEYNEG(lastKey) ) numNeg++;
				p = lastKey;
			}

			//found dup, take list2's
 			memcpy(p, list2, recSize);
			lastKey = p;
 			p += recSize;
 			list2 += recSize;
 			list1 += recSize; //fuggedaboutit!
		}
	}

	//we compacted out the dups, so reflect that here
	long newNumKeys = (p - mergeBuf) / recSize;
	m_parent->updateNumRecs(newNumKeys - m_numKeys , - br, -numNeg);
	m_numKeys = newNumKeys;

	if(m_keys != mergeBuf) m_parent->setSwapBuf(m_keys);
	m_keys = mergeBuf;
	m_lastSorted = m_numKeys;
	m_endKey = m_keys + ((m_numKeys - 1) * recSize);
	return true;
	

}


//make 2 half full buckets,
//addKey assumes that the *this bucket retains the lower half of the keys
//returns a new bucket with the remaining upper half.
RdbBucket* RdbBucket::split(RdbBucket* newBucket) {

	//	log(LOG_WARN, "splitting bucket");
	long b1NumKeys = m_numKeys >> 1; //m_numkeys / 2
	long b2NumKeys = m_numKeys - b1NumKeys;
	long recSize = m_parent->getRecSize();
	//configure the new bucket
	memcpy(newBucket->m_keys, 
	       m_keys + (b1NumKeys*recSize),
	       b2NumKeys * recSize);
	newBucket->m_numKeys = b2NumKeys;
	newBucket->m_lastSorted = b2NumKeys;
	newBucket->m_endKey = newBucket->m_keys + ((b2NumKeys - 1) * recSize);
	
	//reconfigure the old bucket
	m_numKeys = b1NumKeys;
	m_lastSorted = b1NumKeys;
	m_endKey = m_keys + ((b1NumKeys - 1) * recSize);

	//add it to our parent
	return newBucket;
}


bool RdbBucket::addKey(char *key , char *data , long dataSize) {

	uint8_t ks = m_parent->getKeySize();
	long recSize = m_parent->getRecSize();
	bool isNeg = KEYNEG(key);

	char *newLoc = m_keys + (recSize * m_numKeys);
	memcpy(newLoc, key, ks);
	
	if(data) {
		*(char**)(newLoc + ks) = data;
		if(m_parent->getFixedDataSize() == -1) {
			*(long*)(newLoc + ks + sizeof(char*)) = (long)dataSize;
		}
	}
	
	if(m_endKey == NULL) { //are we the first key?
		if(m_numKeys > 0) {char* xx = NULL; *xx = 0;}
		m_endKey = newLoc;
		m_lastSorted = 1;
	}
	else {
		// . minor optimization: if we are almost sorted, then
		// . see if we can't maintain that state.
		char v = KEYCMPNEGEQ(key, m_endKey, ks); 
		//char v = KEYCMP(key, m_endKey, ks);
		if(v == 0) {
			// . just replace the old key if we were the same,
			// . don't inc num keys
			memcpy(m_endKey, newLoc, recSize);
			if(KEYNEG(m_endKey)) {
				if(isNeg) return true;
				else m_parent->updateNumRecs(0, 0, -1);
			}
			else if(isNeg) m_parent->updateNumRecs(0, 0, 1);;
			return true;
		}
		else if(v > 0) {
			// . if we were greater than the old key, 
			// . we can assume we are still sorted, which
			// . really helps us for adds which are in order
			if(m_lastSorted == m_numKeys) m_lastSorted++;
			m_endKey = newLoc;
		}
	}
	m_numKeys++;
	m_parent->updateNumRecs(1 , dataSize, isNeg?1:0);
	return true;
}

char* RdbBucket::getKeyVal ( char *key , char **data , long* dataSize ) {

	sort();
	long i = getKeyNumExact(key);
	if(i < 0) return NULL;

	long recSize = m_parent->getRecSize();
	uint8_t ks = m_parent->getKeySize();
	char* rec = m_keys + (recSize * i);

	if(data) {
		*data = rec + ks;
		if(dataSize)
			*dataSize = *(long*)*data + sizeof(char*);
	}
	return rec;
}

long RdbBucket::getKeyNumExact(char* key) {

	uint8_t ks = m_parent->getKeySize();
	long recSize = m_parent->getRecSize();
	long i = 0;
	char v;
	char* kk;
	long low = 0;
	long high = m_numKeys - 1;
	while(low <= high) {
		long delta = high - low;
		i = low + (delta >> 1);
		kk = m_keys + (recSize * i);
		v = KEYCMP(key,kk,ks);
		if(v < 0) {
			high = i - 1;
			continue;
		}
		else if(v > 0) {
			low = i + 1;
			continue;
		}
		else return i;
	}
	return -1;
}





bool RdbBucket::selfTest (char* prevKey) {
	sort();
	char* last = NULL;
	char* kk = m_keys;
	long recSize = m_parent->getRecSize();
	long ks = m_parent->getKeySize();

	//ensure our first key is > the last guy's end key
	if(prevKey != NULL && m_numKeys > 0) {
		if(KEYCMP(prevKey, m_keys,ks) > 0) {
			log(LOG_WARN, "db: bucket's first key: %016llx%08lx "
			    "is less than last bucket's end key: "
			    "%016llx%08lx!!!!!",
			    *(long long*)(m_keys+(sizeof(long))), 
			    *(long*)m_keys,
			    *(long long*)(prevKey+(sizeof(long))), 
			    *(long*)prevKey);
			//printBucket();
			return false;
			//char* xx = NULL; *xx = 0;
		}
	}

 	for(long i = 0; i < m_numKeys; i++) {
//log(LOG_WARN, "rdbbuckets last key: ""%016llx%08lx num keys: %li",
//   *(long long*)(kk+(sizeof(long))), *(long*)kk, m_numKeys);
		if(i > 0 && KEYCMP(last, kk, ks) > 0) {
			log(LOG_WARN, "db: bucket's last key was out "
			    "of order!!!!!"
			    "key was: %016llx%08lx vs prev: %016llx%08lx"
			    " num keys: %li"
			    " ks=%li bucketNum=%li",
			    *(long long*)(kk+(sizeof(long))), *(long*)kk, 
			    *(long long*)(last+(sizeof(long))), *(long*)last, 
			    m_numKeys, ks, i);
			return false;
			//char* xx = NULL; *xx = 0;
		}
		last = kk;
		kk += recSize;
	}
	return true;
}

void RdbBuckets::printBuckets() {
 	for(long i = 0; i < m_numBuckets; i++) {
		m_buckets[i]->printBucket();
	}
}


void RdbBucket::printBucket() {
	char* kk = m_keys;
	long recSize = m_parent->getRecSize();
 	for(long i = 0; i < m_numKeys;i++) {
 		log(LOG_WARN, "rdbbuckets last key: ""%016llx%08lx num "
		    "keys: %li",
 		    *(long long*)(kk+(sizeof(long))), *(long*)kk, m_numKeys);
		kk += recSize;
	}
}


RdbBuckets::RdbBuckets() {
	m_numBuckets = 0; 
	m_masterPtr = NULL;
	m_buckets = NULL;
	m_swapBuf = NULL;
	m_sortBuf = NULL;
	m_isWritable = true;
	m_isSaving = false;
	m_dataMemOccupied = 0;
	m_needsSave = false;
	m_repairMode = false;
}



bool RdbBuckets::set ( long fixedDataSize , long maxMem, 
		       bool ownData ,
		       char *allocName ,
		       char rdbId,
		       bool dataInPtrs  ,
		       char *dbname ,
		       char keySize ,
		       bool useProtection ) {
	m_numBuckets = 0;
	m_ks = keySize;
	m_rdbId = rdbId;
	m_allocName = allocName;
	m_fixedDataSize = fixedDataSize;
	m_recSize = m_ks;
	if(m_fixedDataSize != 0) {
		m_recSize += sizeof(char*);
		if(m_fixedDataSize == -1) m_recSize += sizeof(long);
	}
	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_dbname = dbname;
	m_swapBuf = NULL;
 	m_sortBuf = NULL;
	//taken from sort.cpp, this is to prevent mergesort from mallocing
	m_sortBufSize = BUCKET_SIZE * m_recSize + sizeof(char*);
	if(m_buckets) {char *xx = NULL; *xx = 0;}
	m_maxBuckets = 0;
	m_masterSize = 0;
	m_masterPtr =  NULL;
	m_maxMem = maxMem;

	long perBucket = sizeof(RdbBucket*) + 
		sizeof(RdbBucket)
		+ BUCKET_SIZE * m_recSize;
	long overhead = m_sortBufSize +
		BUCKET_SIZE * m_recSize + //swapbuf
		sizeof(RdbBuckets); //thats us, silly
	long avail = m_maxMem - overhead;

	m_maxBucketsCapacity = avail / perBucket;
	if(m_maxBucketsCapacity <= 0) {
		log("db: max memory for %s's buckets is way too small to"
		    " accomodate even 1 bucket, reduce bucket size(%li) "
		    "or increase max mem(%li)",
		    m_dbname, (long)BUCKET_SIZE, m_maxMem);
		char *xx = NULL; *xx = 0;
	}

	if(!resizeTable(INIT_SIZE)) {
		g_errno = ENOMEM;
		return false;
	}
	
	log("init: Successfully initialized buckets for %s, "
	    "keysize is %li, max mem is %li, datasize is %li",
	    m_dbname, (long)m_ks, m_maxMem, m_fixedDataSize);


	/*
	RdbBuckets b;
	b.set ( 0, // fixedDataSize, 
		50000000 , // maxTreeMem,
		false, //own data
		"tbuck", // m_treeName, // allocName
		false, //data in ptrs
		"tbuck",//m_dbname,
		16, // m_ks,
		false);
	collnum_t cn = 1;
	key128_t k;
	k.n1 = 12;
	k.n0 = 11;
	b.addNode ( cn , (char *)&k, NULL, 0 );
	// negate it
	k.n0 = 10;
	b.addNode ( cn , (char *)&k, NULL, 0 );

	// try that
	key128_t k1;
	key128_t k2;
	k1.setMin();
	k2.setMax();
	RdbList list;
	long np,nn;
	b.getList ( cn,(char *)&k1,(char *)&k2,1000,&list,&np,&nn,false);
	if ( np != 0 || nn != 1 ) { char *xx=NULL;*xx=0; }
	// must be empty
	if ( b.getNumKeys() != 0 ) { char *xx=NULL;*xx=0; }
	*/

	return true;
}


RdbBuckets::~RdbBuckets( ) {
	reset();
}


void RdbBuckets::setNeedsSave(bool s) {
	m_needsSave = s;
}


void RdbBuckets::reset() {
	for(long j = 0; j < m_numBuckets; j++) {
		m_buckets[j]->reset();
	}
	if(m_masterPtr) mfree(m_masterPtr, m_masterSize, m_allocName );
	m_masterPtr = NULL;
	m_buckets = NULL;
	m_bucketsSpace = NULL;
	m_numBuckets = 0;
	m_maxBuckets = 0;
	m_dataMemOccupied = 0;
	m_firstOpenSlot = 0;
	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_sortBuf = NULL;
	m_swapBuf = NULL;
}


void RdbBuckets::clear() {
	for(long j = 0; j < m_numBuckets; j++) {
		m_buckets[j]->reset();
	}
	m_numBuckets = 0;
	m_firstOpenSlot = 0;
	m_dataMemOccupied = 0;
	m_numKeysApprox = 0;
	m_numNegKeys = 0;
	m_needsSave = true;
}




RdbBucket* RdbBuckets::bucketFactory() {

	if(m_numBuckets == m_maxBuckets - 1) {
		if(!resizeTable(m_maxBuckets * 2)) return NULL;
	}
	
	RdbBucket *b;
	if(m_firstOpenSlot > m_numBuckets) {
		long i = 0;
		for(; i < m_numBuckets; i++) {
			if(m_bucketsSpace[i].getNumKeys() == 0) break;
		}
		b = &m_bucketsSpace[i];
	}
	else {
		b = &m_bucketsSpace[m_firstOpenSlot];
		m_firstOpenSlot++;
	}
	return b;
}




bool RdbBuckets::resizeTable(long numNeeded) {
	if(numNeeded == m_maxBuckets) return true;

	if(numNeeded < INIT_SIZE) numNeeded = INIT_SIZE;

	if(numNeeded > m_maxBucketsCapacity) {
		if(m_maxBucketsCapacity <= m_maxBuckets) {
			log(LOG_INFO,
			    "db: could not resize buckets currently have %li "
			    "buckets, asked for %li, max number of buckets"
			    " for %li bytes with keysize %li is %li",
			    m_maxBuckets, numNeeded, m_maxMem, (long)m_ks,
			    m_maxBucketsCapacity);
			g_errno = ENOMEM;
			return false;
		}
		log(LOG_INFO,
		    "db: scaling down request for buckets.  "
		    "Currently have %li "
		    "buckets, asked for %li, max number of buckets"
		    " for %li bytes is %li.",
		    m_maxBuckets, numNeeded, m_maxMem, m_maxBucketsCapacity);

		numNeeded = m_maxBucketsCapacity;
	}

	long perBucket = sizeof(RdbBucket*) + 
		sizeof(RdbBucket)
		+ BUCKET_SIZE * m_recSize;

	long tmpMaxBuckets = numNeeded;
	long newMasterSize = tmpMaxBuckets * perBucket +
		(BUCKET_SIZE * m_recSize) + /*swap buf*/
		m_sortBufSize;         /*sort buf*/

	if(newMasterSize > m_maxMem) {
		log(LOG_WARN,
		    "db: Buckets oops, trying to malloc more(%li) that max "
		    "mem(%li), should've caught this earlier.",
		    newMasterSize, m_maxMem);
		char* xx = NULL; *xx = 0;
	}

	char *tmpMasterPtr = (char*)mmalloc(newMasterSize, m_allocName);
	if(!tmpMasterPtr) {
		g_errno = ENOMEM;
		return false;
	}
	char* p = tmpMasterPtr;
	char* bucketMemPtr = p;
	p += (BUCKET_SIZE * m_recSize) * tmpMaxBuckets;
	m_swapBuf = p;
	p += (BUCKET_SIZE * m_recSize);
	m_sortBuf = p;
	p += m_sortBufSize;

	RdbBucket** tmpBucketPtrs = (RdbBucket**)p;
	p += tmpMaxBuckets * sizeof(RdbBucket*);
	RdbBucket* tmpBucketSpace = (RdbBucket*)p;
	p += tmpMaxBuckets * sizeof(RdbBucket);
	if(p - tmpMasterPtr != newMasterSize) {char* xx = NULL; *xx = 0;}

	for(long i = 0; i < m_numBuckets; i++) {
		//copy them over one at a time so they
		//will now be contiguous and consistent
		//with the ptrs array.
		tmpBucketPtrs[i] = &tmpBucketSpace[i];
		memcpy(&tmpBucketSpace[i],
		       m_buckets[i],
		       sizeof(RdbBucket));
		tmpBucketSpace[i].reBuf(bucketMemPtr);
		bucketMemPtr += (BUCKET_SIZE * m_recSize);
	}
	//now do the rest
	for(long i = m_numBuckets; i < tmpMaxBuckets; i++) {
		tmpBucketSpace[i].set(this, bucketMemPtr);
		bucketMemPtr += (BUCKET_SIZE * m_recSize);
	}
	if(bucketMemPtr != m_swapBuf) {char* xx = NULL; *xx = 0;}

// 	log(LOG_WARN, "new size = %li, old size = %li, newMemUsed = %li "
// 	    "oldMemUsed = %li", 
// 	    numNeeded, m_maxBuckets, newMasterSize, m_masterSize);

	if(m_masterPtr) mfree(m_masterPtr, m_masterSize, m_allocName);
	m_masterPtr = tmpMasterPtr;
	m_masterSize = newMasterSize;
	m_buckets = tmpBucketPtrs;
	m_bucketsSpace = tmpBucketSpace;
	m_maxBuckets = tmpMaxBuckets;
	m_firstOpenSlot = m_numBuckets;
	return true;
}




long RdbBuckets::addNode (collnum_t collnum,
			  char *key,
			  char *data , long dataSize ) {

	if(!m_isWritable || m_isSaving ) {
		g_errno = EAGAIN;
		return false;
	}

	m_needsSave = true;

	long i;

	i = getBucketNum(key, collnum);
	if(i == m_numBuckets  ||
	   m_buckets[i]->getCollnum() != collnum) {
		long bucketsCutoff = (BUCKET_SIZE>>1);
		//when repairing the keys are added in order,
		//so fill them up all of the way before moving
		//on to the next one.
		if(m_repairMode) bucketsCutoff = BUCKET_SIZE;

  		if(i != 0 && 
		   m_buckets[i-1]->getCollnum() == collnum &&
		   m_buckets[i-1]->getNumKeys() < bucketsCutoff) {
  			i--;
  		}
		else if(i == m_numBuckets) {
			m_buckets[i] = bucketFactory();
			if(m_buckets[i] == NULL) {
				g_errno = ENOMEM;
				return -1;
			}
			m_buckets[i]->setCollnum(collnum);
			m_numBuckets++;
		}
		else { //m_buckets[i]->getCollnum() != collnum
			RdbBucket* newBucket = bucketFactory();
			if(m_buckets[i] == NULL) {//can't really happen here..
				g_errno = ENOMEM;
				return -1;
			}
			newBucket->setCollnum(collnum);
			addBucket(newBucket, i);
		}

	}
	//check if we are full
	if(m_buckets[i]->getNumKeys() == BUCKET_SIZE) {
		//split the bucket
		long long t = gettimeofdayInMilliseconds();
		m_buckets[i]->sort();
		RdbBucket* newBucket = bucketFactory();
		if(newBucket == NULL ) {
			g_errno = ENOMEM;
			return -1;
		}
		newBucket->setCollnum(collnum);
		m_buckets[i]->split(newBucket);
		addBucket(newBucket, i+1);
		if(bucketCmp(key, collnum, m_buckets[i]) > 0) i++;
		
		long long took = gettimeofdayInMilliseconds() - t;
		if(took > 10) log(LOG_WARN, 
				  "db: split bucket in %lli ms for %s",took, 
				  m_dbname);
	}

	m_buckets[i]->addKey(key, data, dataSize);
	//if(rand() % 100 == 0) 	selfTest(true, true);
	return 0;
}


bool RdbBuckets::addBucket (RdbBucket* newBucket, long i) {

	//long i = getBucketNum(newBucket->getEndKey(), newBucket->getCollnum());
	m_numBuckets++;
	long moveSize = (m_numBuckets - i)*sizeof(RdbBuckets*);
	if(moveSize > 0)
		memmove(&m_buckets[i+1], &m_buckets[i], moveSize);
	m_buckets[i] = newBucket;
	return true;
}

bool RdbBuckets::getList ( collnum_t collnum ,
			   char *startKey, char *endKey, long minRecSizes ,
			   RdbList *list , long *numPosRecs , long *numNegRecs,
			   bool useHalfKeys ) {

	if ( numNegRecs ) *numNegRecs = 0;
	if ( numPosRecs ) *numPosRecs = 0;
	// set *lastKey in case we have no nodes in the list
	//if ( lastKey ) *lastKey = endKey;
	// . set the start and end keys of this list
	// . set lists's m_ownData member to true
	list->reset();
	// got set m_ks first so the set ( startKey, endKey ) works!
	list->m_ks = m_ks;
	list->set              ( startKey , endKey );
	list->setFixedDataSize ( m_fixedDataSize   );
	list->setUseHalfKeys   ( useHalfKeys       );
	// bitch if list does not own his own data
	if ( ! list->getOwnData() ) {
		g_errno = EBADENGINEER;
		return log(LOG_LOGIC,"db: rdbbuckets: getList: List does not "
			   "own data");
	}
	// bail if minRecSizes is 0
	if ( minRecSizes == 0 ) return true;
	if ( minRecSizes < 0 ) minRecSizes = LONG_MAX;

	long startBucket = getBucketNum(startKey, collnum);
	if(startBucket > 0 && 
	   bucketCmp(startKey, collnum, m_buckets[startBucket-1]) < 0)
		startBucket--;

	// if the startKey is past our last bucket, then nothing
	// to return
	if(startBucket == m_numBuckets ||
	   m_buckets[startBucket]->getCollnum() != collnum) return true;


	long endBucket;
	if(bucketCmp(endKey, collnum, m_buckets[startBucket]) <= 0)
		endBucket = startBucket;
	else endBucket = getBucketNum(endKey, collnum);
	if(endBucket == m_numBuckets ||
	   m_buckets[endBucket]->getCollnum() != collnum) endBucket--;
	//log(LOG_WARN, "db numBuckets %li start %li end %li", 
	//m_numBuckets, startBucket, endBucket);
	if(m_buckets[endBucket]->getCollnum() != collnum) {
		char* xx = NULL; *xx = 0;
	}

	long growth = 0;

	if(startBucket == endBucket) {
		growth = m_buckets[startBucket]->getNumKeys() * m_recSize;
		if(growth > minRecSizes) growth = minRecSizes + m_recSize;
		if(!list->growList(growth))
			return log("db: Failed to grow list to %li bytes "
				   "for storing "
				   "records from buckets: %s.",
				   growth,mstrerror(g_errno));

		if(!m_buckets[startBucket]->getList(list, 
						    startKey, 
						    endKey,
						    minRecSizes,
						    numPosRecs, 
						    numNegRecs, 
						    useHalfKeys))
			return false;
		return true;
	}

	//reserve some space, it is an upper bound
	for(long i = startBucket; i <= endBucket; i++) 
		growth += m_buckets[i]->getNumKeys() * m_recSize;

	if(growth > minRecSizes) growth = minRecSizes + m_recSize;
	if(!list->growList(growth))
		return log("db: Failed to grow list to %li bytes for storing "
			   "records from buckets: %s.",
			   growth, mstrerror(g_errno));

	// separate into 3 different calls so we don't have 
	// to search for the start and end keys within the buckets
	// unnecessarily.
	if(!m_buckets[startBucket]->getList(list, 
					    startKey, 
					    NULL,
					    minRecSizes,
					    numPosRecs, 
					    numNegRecs, 
					    useHalfKeys))
		return false;

	long i = startBucket + 1;
	for(; i < endBucket && list->getListSize() < minRecSizes; i++) {
		if(!m_buckets[i]->getList(list, 
					  NULL, 
					  NULL,
					  minRecSizes,
					  numPosRecs, 
					  numNegRecs, 
					  useHalfKeys))
			return false;
	}
	
	if(list->getListSize() < minRecSizes)
		if(!m_buckets[i]->getList(list, 
					  NULL, 
					  endKey,
					  minRecSizes,
					  numPosRecs, 
					  numNegRecs, 
					  useHalfKeys))
			return false;

	return true;

}

bool RdbBuckets::testAndRepair() {
	if(!selfTest(true/*thorough*/, 
		     false/*core on error*/)) {
		if(!repair()) return false;
		m_needsSave = true;
	}
	return true;
}


bool RdbBuckets::repair() {
	if(m_numBuckets == 0 && 
	   (m_numKeysApprox != 0 || m_numNegKeys != 0)) {
		   m_numKeysApprox = 0;
		   m_numNegKeys = 0;
		   log("db: RdbBuckets repaired approx key count to reflect "
		       "true number of keys.");
	}

	//long tmpMaxBuckets = m_maxBuckets;
	long tmpMasterSize = m_masterSize;
	char *tmpMasterPtr = m_masterPtr;
	RdbBucket **tmpBucketPtrs = m_buckets;
	long tmpNumBuckets = m_numBuckets;
	m_masterPtr = NULL;
	m_masterSize = 0;
	m_numBuckets = 0;
	reset();
	if(!resizeTable(INIT_SIZE)) {
		log("db: RdbBuckets could not alloc enough memory to repair "
		    "corruption.");
		g_errno = ENOMEM;
		return false;
	}

	m_repairMode = true;

 	for(long j = 0; j < tmpNumBuckets; j++) {
		collnum_t collnum = tmpBucketPtrs[j]->getCollnum();
 		for(long i = 0; i < tmpBucketPtrs[j]->getNumKeys(); i++) {
 			char* currRec = tmpBucketPtrs[j]->getKeys() + 
				m_recSize * i;
 			char* data = NULL;
 			long dataSize = m_fixedDataSize;
			
 			if(m_fixedDataSize != 0) {
 				data = currRec + m_ks;
 				if(m_fixedDataSize == -1)
 					dataSize = *(long*)(data + sizeof(char*));
 			}
 			if(addNode(collnum, currRec, data, dataSize) < 0) {
 				log(LOG_WARN, "db: got unrepairable error in "
 				    "RdbBuckets, could not re-add data");
				return false;
			}
 		}
 	}

	m_repairMode = false;

	if(tmpMasterPtr) mfree(tmpMasterPtr, tmpMasterSize, m_allocName);

 	log("db: RdbBuckets repair for %li keys complete", m_numKeysApprox);
	return true;
}



bool RdbBuckets::selfTest(bool thorough, bool core) {
	if(m_numBuckets == 0 && m_numKeysApprox != 0) return false;
	long totalNumKeys = 0;
	char* last = NULL;
	collnum_t lastcoll = -1;
	long numColls = 0;

	for(long i = 0; i < m_numBuckets; i++) {
		RdbBucket* b = m_buckets[i];
		if(lastcoll != b->getCollnum()) {
			last = NULL;
			numColls++;
		}
		if(thorough) {
			if(!b->selfTest (last)) {
				if(!core) return false;
				char* xx = NULL; *xx = 0;
			}
		}


		totalNumKeys += b->getNumKeys();
		char* kk = b->getEndKey();
		//log(LOG_WARN, "rdbbuckets last key: ""%016llx%08lx "
		//"num keys: %li",
		//*(long long*)(kk+(sizeof(long))),*(long*)kk,b->getNumKeys());
		if(i > 0 &&
		   lastcoll == b->getCollnum() && 
		   KEYCMPNEGEQ(last, kk,m_ks) >= 0) {
			log(LOG_WARN, "rdbbuckets last key: "
			    "%016llx%08lx num keys: %li",
			    *(long long*)(kk+(sizeof(long))),
			    *(long*)kk, b->getNumKeys());
			log(LOG_WARN, "rdbbuckets last key was out "
			    "of order!!!!!");
			if(!core) return false;
			char* xx = NULL; *xx = 0;
		}
		last = kk;
		lastcoll = b->getCollnum();
	}
	log(LOG_WARN, "db have %li keys,  should have %li. "
	    "%li buckets in %li colls for db %s", 
	    totalNumKeys, m_numKeysApprox, m_numBuckets, 
	    numColls, m_dbname);

	if(thorough && totalNumKeys != m_numKeysApprox) {
		return false;
	}
	return true;
}


char RdbBuckets::bucketCmp(char *akey, collnum_t acoll, 
			   char *bkey, collnum_t bcoll) {
	if (acoll == bcoll) return KEYCMPNEGEQ(akey, bkey, m_ks);
	if (acoll <  bcoll) return -1;
	return 1;
}

char RdbBuckets::bucketCmp(char *akey, collnum_t acoll, 
			   RdbBucket* b) {
	if (acoll == b->getCollnum())
		return KEYCMPNEGEQ(akey, b->getEndKey(), m_ks);
	if (acoll < b->getCollnum()) return -1;
	return 1;
}



long RdbBuckets::getBucketNum(char* key, collnum_t collnum) {

	if(m_numBuckets < 10) {
		long i = 0;
		for(; i < m_numBuckets; i++) {
			RdbBucket* b = m_buckets[i];
			char v = bucketCmp(key, collnum, b);
			if(v > 0) continue;
			if(v < 0) {break;}
			else break;
		}
		return i;
	}
	long i = 0;
	char v;
	RdbBucket* b = NULL;
	long low = 0;
	long high = m_numBuckets - 1;
	while(low <= high) {
		long delta = high - low;
		i = low + (delta >> 1);
		b = m_buckets[i];
		char v = bucketCmp(key, collnum, b);
		if(v < 0) {
			high = i - 1;
			continue;
		}
		else if(v > 0) {
			low = i + 1;
			continue;
		}
		else return i;
	}
	
	//now fine tune:
  	v = bucketCmp(key, collnum, b);
	if(v > 0) i++;
	return i;
}


bool RdbBuckets::collExists(collnum_t collnum) {
	for(long i = 0; i < m_numBuckets; i++) {
		if(m_buckets[i]->getCollnum() == collnum)return true;
		if(m_buckets[i]->getCollnum() > collnum)  break;
	}
	return false;
}

long RdbBuckets::getNumKeys(collnum_t collnum) {
	long numKeys = 0;
	for(long i = 0; i < m_numBuckets; i++) {
		if(m_buckets[i]->getCollnum() == collnum) 
			numKeys += m_buckets[i]->getNumKeys();
		if(m_buckets[i]->getCollnum() > collnum)  break;
	}
	return numKeys;
}

	
long RdbBuckets::getNumKeys() {
	return m_numKeysApprox;
}


// long RdbBuckets::getNumNegativeKeys ( collnum_t collnum ) {
//  	return m_numNegKeys;
// }


// long RdbBuckets::getNumPositiveKeys ( collnum_t collnum ) {
// 	return getNumKeys(collnum) - getNumNegativeKeys(collnum); 
// }


long RdbBuckets::getNumNegativeKeys ( ) {
	return m_numNegKeys;
}


long  RdbBuckets::getNumPositiveKeys ( ) {
	return getNumKeys() - getNumNegativeKeys ( );
}


char* RdbBuckets::getKeyVal ( collnum_t collnum , char *key , 
			      char **data , long* dataSize ) {

	long i = getBucketNum(key, collnum);
	if(i == m_numBuckets ||
	   m_buckets[i]->getCollnum() != collnum ) return NULL;
	return m_buckets[i]->getKeyVal(key, data, dataSize);
}


void RdbBuckets::updateNumRecs(long n, long bytes, long numNeg) {
	m_numKeysApprox += n;
	m_dataMemOccupied += bytes;
	m_numNegKeys += numNeg;
}


char *RdbBucket::getFirstKey() {
	sort();
	return m_keys;
}

long RdbBucket::getNumNegativeKeys ( ) {
	long numNeg = 0;
	long recSize = m_parent->getRecSize();
	char *currKey = m_keys;

	char *lastKey = m_keys + (m_numKeys * recSize);

	while(currKey < lastKey) {  //&& !list->isExhausted()
		if ( KEYNEG(currKey) ) numNeg++;
		currKey += recSize;
	}
	return numNeg;
}


bool RdbBucket::getList(RdbList* list, 
			char* startKey, 
			char* endKey,
			long minRecSizes,
			long *numPosRecs, 
			long *numNegRecs, 
			bool useHalfKeys) {

	sort();
	//get our bounds within the bucket:
	uint8_t ks = m_parent->getKeySize();
	long recSize = m_parent->getRecSize();
	long start = 0;
	long end = m_numKeys - 1;
	char v;
	char* kk = NULL;
	if(startKey) {
		long low = 0;
		long high = m_numKeys - 1;
		while(low <= high) {
			long delta = high - low;
			start = low + (delta >> 1);
			kk = m_keys + (recSize * start);
			v = KEYCMP(startKey,kk,ks);
			if(v < 0) {
				high = start - 1;
				continue;
			}
			else if(v > 0) {
				low = start + 1;
				continue;
			}
			else break;

		}
		//now back up or move forward s.t. startKey
		//is <= start
		while(start < m_numKeys) {
			kk = m_keys + (recSize * start);
			v = KEYCMP(startKey, kk, ks);
			if(v > 0) start++;
			else break;
		}
	}
	else start = 0;


	if(endKey) {
		long low = start;
		long high = m_numKeys - 1;
		while(low <= high) {
			long delta = high - low;
			end = low + (delta >> 1);
			kk = m_keys + (recSize * end);
			v = KEYCMP(endKey,kk,ks);
			if(v < 0) {
				high = end - 1;
				continue;
			}
			else if(v > 0) {
				low = end + 1;
				continue;
			}
			else break;

		}
		while(end > 0) {
			kk = m_keys + (recSize * end);
			v = KEYCMP(endKey, kk, ks);
			if(v < 0) end--;
			else break;
		}

	}
	else end = m_numKeys - 1;

	//log(LOG_WARN, "numKeys %li start %li end %li", 
	//m_numKeys, start, end);
	
	//keep track of our negative a positive recs
	long numNeg = 0;
	long numPos = 0;

	long fixedDataSize = m_parent->getFixedDataSize();

	char* currKey = m_keys + (start * recSize);

	//bail now if there is only one key and it is out of range.
	if(start == end && 
	   ((startKey && KEYCMP(currKey, startKey, ks) < 0) ||
	    (endKey   && KEYCMP(currKey, endKey, ks) > 0))) {
		return true;
	}
	// 	//set our real start key
	// 	if(startKey != NULL) list->setStartKey(currKey);

	char* lastKey = NULL;
	for(long i = start; 
	    i <= end && list->getListSize() < minRecSizes; 
	    i++, currKey += recSize) {
		if ( fixedDataSize == 0 ) {
			if ( ! list->addRecord(currKey, 0, NULL))
				return log("db: Failed to add record "
					   "to list for %s: %s. "
					   "Fix the growList algo.",
					   m_parent->getDbname(),
					   mstrerror(g_errno));
		} 
		else {
			long dataSize = fixedDataSize;
			if ( fixedDataSize == -1 ) 
				dataSize = *(long*)(currKey + 
						    ks + sizeof(char*));
			if ( ! list->addRecord ( currKey ,
						 dataSize,
						 currKey + ks) )
				return log("db: Failed to add record "
					   "to list for %s: %s. "
					   "Fix the growList algo.",
					   m_parent->getDbname(),
					   mstrerror(g_errno));
		}
		if ( KEYNEG(currKey) ) numNeg++;
		else                   numPos++;
		lastKey = currKey;

#ifdef __SANITY_CHECK__
		//sanity, remove for production
		if(startKey && KEYCMP(currKey, startKey, ks) < 0) {
			log("db: key is outside the "
			    "keyrange given for getList."
			    "  it is < startkey."
			    "  %016llx%08lx %016llx%08lx."
			    "  getting keys %li to %li for list"
			    "bounded by %016llx%08lx %016llx%08lx",
			    *(long long*)(startKey+(sizeof(long))),
			    *(long*)startKey,
			    *(long long*)(currKey+(sizeof(long))),
			    *(long*)currKey,
			    start, end,
 			    *(long long*)(startKey+(sizeof(long))),
			    *(long*)startKey,
 			    *(long long*)(endKey+(sizeof(long))),
			    *(long*)endKey);

			printBucket();
			char* xx=NULL; *xx=0;
		}
		if(endKey &&   KEYCMP(currKey, endKey, ks) > 0) {
			log("db: key is outside the "
			    "keyrange given for getList."
			    "  it is > endkey"
			    "  %016llx%08lx %016llx%08lx."
			    "  getting keys %li to %li for list"
			    "bounded by %016llx%08lx %016llx%08lx",
			    *(long long*)(currKey+(sizeof(long))),
			    *(long*)currKey,
			    *(long long*)(endKey+(sizeof(long))),
			    *(long*)endKey,
			    start, end,
 			    *(long long*)(startKey+(sizeof(long))),
			    *(long*)startKey,
 			    *(long long*)(endKey+(sizeof(long))),
			    *(long*)endKey);

			printBucket();
			char* xx=NULL; *xx=0;
		}
#endif
	}

	// set counts to pass back, we may be accumulating over multiple
	// buckets so add it to the count
	if ( numNegRecs ) *numNegRecs += numNeg;
	if ( numPosRecs ) *numPosRecs += numPos;

	//if we don't have an end key, we were not the last bucket, so don't 
	//finalize the list... yes do, because we might've hit min rec sizes
	if(endKey == NULL && list->getListSize() < minRecSizes) return true;

	if ( lastKey != NULL ) list->setLastKey ( lastKey );
	
	// reset the list's endKey if we hit the minRecSizes barrier cuz
	// there may be more records before endKey than we put in "list"
	if ( list->getListSize() >= minRecSizes && lastKey != NULL ) {
		// use the last key we read as the new endKey
		//key_t newEndKey = m_keys[lastNode];
		char newEndKey[MAX_KEY_BYTES];
		KEYSET(newEndKey, lastKey, ks);
		// . if he's negative, boost new endKey by 1 because endKey's
		//   aren't allowed to be negative
		// . we're assured there's no positive counterpart to him 
		//   since Rdb::addRecord() doesn't allow both to exist in
		//   the tree at the same time
		// . if by some chance his positive counterpart is in the
		//   tree, then it's ok because we'd annihilate him anyway,
		//   so we might as well ignore him
		// we are little endian
		if ( KEYNEG(newEndKey,0,ks) ) KEYADD(newEndKey,1,ks);
		// if we're using half keys set his half key bit
		if ( useHalfKeys ) KEYOR(newEndKey,0x02);
		if ( m_parent->m_rdbId == RDB_POSDB ||
		     m_parent->m_rdbId == RDB2_POSDB2 ) 
			newEndKey[0] |= 0x04;
		// tell list his new endKey now
		list->setEndKey ( newEndKey );
	}
	// reset list ptr to point to first record
	list->resetListPtr();

	// success
	return true;
}


bool RdbBuckets::deleteList(collnum_t collnum, RdbList *list) {
	if(list->getListSize() == 0) return true;

	if(!m_isWritable || m_isSaving ) {
		g_errno = EAGAIN;
		return false;
	}

	// . set this right away because the head bucket needs to know if we
	// . need to save
	m_needsSave = true;

	char startKey [ MAX_KEY_BYTES ];
	char endKey   [ MAX_KEY_BYTES ];
	list->getStartKey ( startKey );
	list->getEndKey   ( endKey   );

	long startBucket = getBucketNum(startKey, collnum);
	if(startBucket > 0 && 
	   bucketCmp(startKey, collnum, m_buckets[startBucket-1]) < 0)
		startBucket--;

	// if the startKey is past our last bucket, then nothing
	// to delete
	if(startBucket == m_numBuckets ||
	   m_buckets[startBucket]->getCollnum() != collnum) return true;


	long endBucket = getBucketNum(endKey, collnum);
	if(endBucket == m_numBuckets ||
	   m_buckets[endBucket]->getCollnum() != collnum) endBucket--;
	//log(LOG_WARN, "db numBuckets %li start %li end %li", 
	//  m_numBuckets, startBucket, endBucket);

	list->resetListPtr();
	for(long i= startBucket; i <= endBucket && !list->isExhausted(); i++) {
		if(!m_buckets[i]->deleteList(list)) {
			m_buckets[i]->reset();
			m_buckets[i] = NULL;
		}
	}
	long j = 0;
	for(long i = 0; i < m_numBuckets; i++) 
		if(m_buckets[i]) m_buckets[j++] = m_buckets[i];
	m_numBuckets = j;

	//did we delete the whole darn thing?  
	if(m_numBuckets == 0) {
		if(m_numKeysApprox != 0) {
			log("db: bucket's number of keys is getting off by %li"
			    " after deleting a list", m_numKeysApprox);
			char *xx = NULL; *xx = 0;
		}
		m_firstOpenSlot = 0;
	}
	return true;
}


bool RdbBucket::deleteList(RdbList *list) {

	sort();
	uint8_t ks = m_parent->getKeySize();
	long recSize = m_parent->getRecSize();
	long fixedDataSize = m_parent->getFixedDataSize();
	char v;

	char *currKey = m_keys;
	char *p = currKey;
	char listkey[MAX_KEY_BYTES]; 

	char *lastKey = m_keys + (m_numKeys * recSize);
	long br = 0; //bytes removed
	long dso = ks+sizeof(char*);//datasize offset
	long numNeg = 0;

	list->getCurrentKey(listkey);
	while(currKey < lastKey) {  //&& !list->isExhausted()

		v = KEYCMP(currKey, listkey, ks);
		if(v == 0) {
			if(fixedDataSize != 0) {
				if(fixedDataSize == -1) 
					br += *(long*)(currKey+dso);
				else br += fixedDataSize;
			}
			if(KEYNEG(currKey)) numNeg++;

			// . forget it exists by advancing read ptr without 
			// . advancing the write ptr
			currKey += recSize;
			if(!list->skipCurrentRecord()) break;
			list->getCurrentKey(listkey);
			continue;
		} 
		else if (v < 0) {
			// . copy this key into place, it was not in the 
			// . delete list
			if(p != currKey) memcpy(p, currKey, recSize);
			p += recSize;
			currKey += recSize;
		} 
		else { //list key > current key
			// . otherwise advance the delete list until 
			//listKey is <= currKey
			if(!list->skipCurrentRecord()) break;
			list->getCurrentKey(listkey);
		}
	}

	// . do we need to finish copying our list down to the 
	// . vacated mem?
	if(currKey < lastKey) {
		long tmpSize = lastKey - currKey;
		memcpy(p, currKey, tmpSize);
		p += tmpSize;
	}

	if(p > m_keys) {	//do we have anything left?
		long newNumKeys = (p - m_keys) / recSize;
		m_parent->updateNumRecs(newNumKeys - m_numKeys, - br, -numNeg);
		m_numKeys = newNumKeys;
		m_lastSorted = m_numKeys;
		m_endKey = m_keys + ((m_numKeys - 1) * recSize);
		return true;

	}
	else {
		//we deleted the entire bucket, let our parent know to free us
		m_parent->updateNumRecs( - m_numKeys, - br, -numNeg);
		return false;
	}
	// success
	return true;
}


bool RdbBuckets::delColl(collnum_t collnum) {

	m_needsSave = true;
	RdbList list;
	long minRecSizes = 1024*1024;
	long numPosRecs  = 0;
	long numNegRecs = 0;
	while (1) {
		if(!getList(collnum, KEYMIN(), KEYMAX(), minRecSizes ,
			    &list , &numPosRecs , &numNegRecs, false )) {
			if(g_errno == ENOMEM && minRecSizes > 1024) {
				minRecSizes /= 2;
				continue;
			} else {
				log("db: buckets could not delete collection: %s.",
				    mstrerror(errno));
				return false;
			}
		} 
		if(list.isEmpty()) break;
		deleteList(collnum, &list);
	}
	return true;
}

long RdbBuckets::addTree(RdbTree* rt) {

	long n = rt->getFirstNode();
	long count = 0;
	char* data = NULL;

	long dataSize = m_fixedDataSize;

	while ( n >= 0 ) {
		if(m_fixedDataSize != 0) {
			data = rt->getData ( n );
			if(m_fixedDataSize == -1) 
				dataSize = rt->getDataSize(n);
		}

		if(addNode ( rt->getCollnum (n), 
			     rt->getKey ( n ) , 
			     data , dataSize) < 0) 
			break;
		n = rt->getNextNode ( n );
		count++;
	}
	log("db: added %li keys from tree to buckets for %s.",count, m_dbname);
	return count;
}


//this could be sped up a lot, but it is only called from repair at
//the moment.
bool RdbBuckets::addList(RdbList* list, collnum_t collnum) {
	char listKey[MAX_KEY_BYTES]; 

	for( list->resetListPtr(); 
	     !list->isExhausted(); 
	     list->skipCurrentRecord()) {

		list->getCurrentKey(listKey);

		if(addNode(collnum,  
			   listKey , 
			   list->getCurrentData() , 
			   list->getCurrentDataSize()) < 0)
			return false;
	}

	return true;
}




//return the total bytes of the list bookended by startKey and endKey
long long RdbBuckets::getListSize ( collnum_t collnum,
				    char *startKey , char *endKey , 
				    char *minKey , char *maxKey ) {

	if ( minKey ) KEYSET ( minKey , endKey   , m_ks );
	if ( maxKey ) KEYSET ( maxKey , startKey , m_ks );

	long startBucket = getBucketNum(startKey, collnum);
	if(startBucket > 0 && 
	   bucketCmp(startKey, collnum, m_buckets[startBucket-1]) < 0)
		startBucket--;

	if(startBucket == m_numBuckets ||
	   m_buckets[startBucket]->getCollnum() != collnum) return 0;


	long endBucket = getBucketNum(endKey, collnum);
	if(endBucket == m_numBuckets ||
	   m_buckets[endBucket]->getCollnum() != collnum) endBucket--;
	//log(LOG_WARN, "db numBuckets %li start %li end %li", 
	//m_numBuckets, startBucket, endBucket);

	long long retval = 0;
	for(long i = startBucket; i <= endBucket; i++) {
		retval += m_buckets[i]->getNumKeys();
	}
	return retval * m_recSize;
}

void *saveBucketsWrapper      ( void *state , ThreadEntry *t ) ;
void threadDoneBucketsWrapper ( void *state , ThreadEntry *t ) ;

// . caller should call f->set() himself
// . we'll open it here
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool RdbBuckets::fastSave ( char    *dir       ,
			    bool     useThread ,
			    void    *state     ,
			    void    (* callback) (void *state) ) {
	if ( g_conf.m_readOnlyMode ) return true;
	// we do not need a save
	if ( ! m_needsSave ) return true;
	// return true if already in the middle of saving
	if ( m_isSaving ) return false;

	// do not use thread for now!! test it to make sure that was
	// not the problem
	//useThread = false;

	// save parms
	//m_saveFile = f;
	m_dir      = dir;
	m_state    = state;
	m_callback = callback;
	// assume no error
	m_saveErrno = 0;
	// no adding to the tree now
	m_isSaving = true;

	/*
// skip thread call if we should
	if ( ! useThread ) goto skip;
	// make this a thread now
	if ( g_threads.call ( SAVETREE_THREAD   , // threadType
			      1                 , // niceness
			      this              , 
			      threadDoneBucketsWrapper ,
			      saveBucketsWrapper) ) return false;
	// if it failed
	if ( ! g_threads.m_disabled ) 
		log("db: Thread creation failed. Blocking while "
		    "saving buckets. Hurts performance.");
 skip:
	*/

	// . this returns false and sets g_errno on error
	// . must now lock for each bucket when saving that bucket, but
	//   release lock to breathe between buckets
	fastSave_r ();

	// store save error into g_errno
	g_errno = m_saveErrno;
	// resume adding to the tree
	m_isSaving = false;
	// we do not need to be saved now?
	m_needsSave = false;
	// we did not block
	return true;
}

void *saveBucketsWrapper ( void *state , ThreadEntry *t ) {
	// get this class
	RdbBuckets *THIS = (RdbBuckets *)state;
	// this returns false and sets g_errno on error
	THIS->fastSave_r();
	// now exit the thread, bogus return
	return NULL;
}

// we come here after thread exits
void threadDoneBucketsWrapper ( void *state , ThreadEntry *t ) {
	// get this class
	RdbBuckets *THIS = (RdbBuckets *)state;
	// store save error into g_errno
	g_errno = THIS->m_saveErrno;
	// . resume adding to the tree
	// . this will also allow other threads to be queued
	// . if we did this at the end of the thread we could end up with
	//   an overflow of queued SAVETHREADs
	THIS->m_isSaving = false;
	// we do not need to be saved now?
	THIS->m_needsSave = false;
	// g_errno should be preserved from the thread so if fastSave_r()
	// had an error it will be set
	if ( g_errno )
		log("db: Had error saving tree to disk for %s: %s.",
		    THIS->m_dbname,mstrerror(g_errno));
	// . call callback
	if ( THIS->m_callback ) THIS->m_callback ( THIS->m_state );
}

// . returns false and sets g_errno on error
// . NO USING g_errno IN A DAMN THREAD!!!!!!!!!!!!!!!!!!!!!!!!!
bool RdbBuckets::fastSave_r() {
	if ( g_conf.m_readOnlyMode ) return true;
	// recover the file
	//BigFile *f = m_saveFile;
	// open it up
	//if ( ! f->open ( O_RDWR | O_CREAT ) ) 
	//	return log("RdbTree::fastSave_r: %s",mstrerror(g_errno));
	// cannot use the BigFile class, since we may be in a thread and it 
	// messes with g_errno
	//char *s = m_saveFile->getFilename();
	char s[1024];
	sprintf ( s , "%s/%s-buckets-saving.dat", m_dir , m_dbname );
	int fd = ::open ( s , 
			  O_RDWR | O_CREAT | O_TRUNC , S_IRUSR | S_IWUSR | 
			  S_IRGRP | S_IWGRP | S_IROTH);
	if ( fd < 0 ) {
		m_saveErrno = errno;
		return log("db: Could not open %s for writing: %s.",
			   s,mstrerror(errno));
	}
	// clear our own errno
	errno = 0;
	// . save the header
	// . force file head to the 0 byte in case offset was elsewhere
	long long offset = 0;

	offset = fastSaveColl_r(fd, offset);
	
	// close it up
	close ( fd );
	// now fucking rename it
	char s2[1024];
	sprintf ( s2 , "%s/%s-buckets-saved.dat", m_dir , m_dbname );
	::rename ( s , s2 ) ; //fuck yeah!
	// info
	log("db RdbBuckets saved %li keys, %lli bytes for %s",
	    getNumKeys(), offset, m_dbname);

	return offset >= 0;
}

long long RdbBuckets::fastSaveColl_r(int fd, long long offset) {
	if(m_numKeysApprox == 0) return offset;
	long version = SAVE_VERSION;
	long err = 0;
	if ( pwrite ( fd , &version, sizeof(long) , offset ) != 4 ) err=errno;
	offset += sizeof(long);

	if ( pwrite ( fd , &m_numBuckets, sizeof(long) , offset)!=4)err=errno; 
	offset += sizeof(long);
	if ( pwrite ( fd , &m_maxBuckets, sizeof(long) , offset)!=4)err=errno; 
	offset += sizeof(long);

	if ( pwrite ( fd , &m_ks, sizeof(uint8_t) , offset ) != 1) err=errno; 
	offset += sizeof(uint8_t);
	if ( pwrite ( fd , &m_fixedDataSize,sizeof(long),offset)!=4) err=errno;
	offset += sizeof(long);
	if ( pwrite ( fd , &m_recSize, sizeof(long) , offset ) != 4) err=errno;
	offset += sizeof(long);
	if ( pwrite ( fd , &m_numKeysApprox,sizeof(long),offset) !=4)err=errno;
	offset += sizeof(long);
	if ( pwrite ( fd , &m_numNegKeys,sizeof(long),offset) != 4 ) err=errno;
	offset += sizeof(long);

	if ( pwrite ( fd,&m_dataMemOccupied,sizeof(long),offset)!=4)err=errno;
	offset += sizeof(long);

	long tmp = BUCKET_SIZE;
	if ( pwrite ( fd , &tmp, sizeof(long) , offset ) != 4 ) err=errno;
	offset += sizeof(long);

	// 	long len = gbstrlen(m_dbname) + 1;
	// 	pwrite ( fd , &m_dbname, len , offset ); 
	// 	offset += len;

	// set it
	if ( err ) errno = err;

	// bitch on error
	if ( errno ) {
		m_saveErrno = errno;
		close ( fd );
		log("db: Failed to save buckets for %s: %s.",
		    m_dbname,mstrerror(errno));
		return -1;
	}
	// position to store into m_keys, ...
	for (long i = 0; i < m_numBuckets; i++ ) {
		offset = m_buckets[i]->fastSave_r(fd, offset);
		// returns -1 on error
		if ( offset < 0 ) {
			close ( fd );
			m_saveErrno = errno;
			log("db: Failed to save buckets for %s: %s.",
			    m_dbname,mstrerror(errno));
			return -1;
		}
	}
	return offset;
}


bool RdbBuckets::loadBuckets ( char* dbname) {
	char filename[256];
	sprintf(filename,"%s-buckets-saved.dat",dbname);
	// set this to false
	// msg
	//log (0,"Rdb::loadTree: loading %s",filename);
	// set a BigFile to this filename
	BigFile file;//g_hostdb.m_dir
	char *dir = g_hostdb.m_dir;
	if( *dir == '\0') dir = ".";
	file.set ( dir , filename , NULL ); 
	if ( file.doesExist() <= 0 ) return true;
	// load the table with file named "THISDIR/saved"
	bool status = false ;
	status = fastLoad ( &file , dbname ) ;
	file.close();
	return status;
}

bool RdbBuckets::fastLoad ( BigFile *f , char* dbname) {
	// msg
	log(LOG_INIT,"db: Loading %s.",f->getFilename());
	// open it up
	if ( ! f->open ( O_RDONLY ) ) return false;
	long fsize = f->getFileSize();
	if ( fsize == 0 ) return true;

	// init offset
	long long offset = 0;

	offset = fastLoadColl(f, dbname, offset);

	if ( offset < 0 ) {
		log("db: Failed to load buckets for %s: %s.",
		    m_dbname,mstrerror(g_errno));
		return false;
	}
	return true;
}


long long RdbBuckets::fastLoadColl( BigFile *f,
				    char *dbname,
				    long long offset ) {
	long maxBuckets;
	long numBuckets;
	long version;

	f->read  ( &version,sizeof(long), offset ); 
	offset += sizeof(long);
	if(version > SAVE_VERSION) {
		log("db: Failed to load buckets for %s: "
		    "saved version is in the future or is corrupt, "
		    "please restart old executable and do a ddump.",
		    m_dbname);
		return -1;
	}

	f->read  ( &numBuckets,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &maxBuckets,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &m_ks,sizeof(uint8_t), offset ); 
	offset += sizeof(uint8_t);

	f->read  ( &m_fixedDataSize,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &m_recSize,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &m_numKeysApprox,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &m_numNegKeys,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &m_dataMemOccupied, sizeof(long), offset ); 
	offset += sizeof(long);

	long bucketSize;
	f->read  ( &bucketSize, sizeof(long), offset ); 
	offset += sizeof(long);


	if(bucketSize != BUCKET_SIZE) {
		log("db: It appears you have changed the bucket size "
		    "please restart the old executable and dump "
		    "buckets to disk. old=%li new=%li", 
		    bucketSize, (long)BUCKET_SIZE);
		char *xx = NULL; *xx = 0;
	}

	m_dbname = dbname;

	if ( g_errno ) return -1;

	for (long i = 0; i < numBuckets; i++ ) {
		m_buckets[i] = bucketFactory();
		if(m_buckets[i] == NULL) return -1;
		offset = m_buckets[i]->fastLoad(f, offset);
		// returns -1 on error
		if ( offset < 0 ) return -1;
		m_numBuckets++;
	}
	return offset;
}

// max key size -- posdb, 18 bytes, so use 18 here
#define BTMP_SIZE (BUCKET_SIZE*18+1000)

long long RdbBucket::fastSave_r(int fd, long long offset) {

	// first copy to a buf before saving so we can unlock!
	char tmp[BTMP_SIZE];
	char *p = tmp;

	memcpy ( p , &m_collnum, sizeof(collnum_t) );
	p += sizeof(collnum_t);
	//pwrite ( fd , &m_collnum, sizeof(collnum_t) , offset ); 
	//offset += sizeof(collnum_t);

	memcpy ( p , &m_numKeys, sizeof(long) );
	p += sizeof(m_numKeys);
	//pwrite ( fd , &m_numKeys, sizeof(long) , offset ); 
	//offset += sizeof(m_numKeys);

	memcpy ( p , &m_lastSorted, sizeof(long) ); 
	p += sizeof(m_lastSorted);
	//pwrite ( fd , &m_lastSorted, sizeof(long) , offset ); 
	//offset += sizeof(m_lastSorted);

	long endKeyOffset = m_endKey - m_keys;
	memcpy ( p , &endKeyOffset, sizeof(long) ); 
	p += sizeof(long);
	//pwrite ( fd , &endKeyOffset, sizeof(long) , offset ); 
	//offset += sizeof(long);
	
	long recSize = m_parent->getRecSize();
	
	memcpy ( p , m_keys, recSize*m_numKeys ); 
	p += recSize*m_numKeys;
	//pwrite ( fd , m_keys, recSize*m_numKeys , offset ); 
	//offset += recSize*m_numKeys;

	long size = p - tmp;
	if ( size > BTMP_SIZE ) { 
		log("buckets: btmp_size too small. keysize>18 bytes?");
		char *xx=NULL;*xx=0; 
	}

	// now we can save it without fear of being interrupted and having
	// the bucket altered
	errno = 0;
	if ( pwrite ( fd , tmp , size , offset ) != size ) {
		log("db:fastSave_r: %s.",mstrerror(errno));
		return -1;
	}
	
	return offset + size;
}

long long RdbBucket::fastLoad(BigFile *f, long long offset) {
	errno = 0;

	f->read  ( &m_collnum,sizeof(collnum_t), offset ); 
	offset += sizeof(collnum_t);

	f->read  ( &m_numKeys,sizeof(long), offset ); 
	offset += sizeof(long);

	f->read  ( &m_lastSorted,sizeof(long), offset ); 
	offset += sizeof(long);

	long endKeyOffset;
	f->read  ( &endKeyOffset,sizeof(long), offset ); 
	offset += sizeof(long);

	long recSize = m_parent->getRecSize();
	
	f->read  ( m_keys,recSize*m_numKeys, offset ); 
	offset += recSize*m_numKeys;

	m_endKey = m_keys + endKeyOffset;

	if(errno) return -1;
	return offset;
}
