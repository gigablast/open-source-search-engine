#include "gb-include.h"

#include "Clusterdb.h"
#include "Threads.h"

// a global class extern'd in .h file
Clusterdb g_clusterdb;
Clusterdb g_clusterdb2;

/*
// for making the cluster cache key
static key_t makeClusterCacheKey ( unsigned long vfd,
				   unsigned long pageNum ) {
	key_t key;
	key.n1 = vfd + 1;
	key.n0 = (unsigned long long)pageNum + 1;
	return key;
}

// DiskPageCache override functions
static void clusterGetPages ( DiskPageCache *pc,
			      long vfd,
			      char *buf,
			      long numBytes,
			      long long offset,
			      long *newNumBytes,
			      long long *newOffset ) {
	bool cacheMiss = false;
	// return new disk offset, assume unchanged
	*newOffset   = offset;
	*newNumBytes = numBytes;
	// what is the page range?
	long long sp = offset / GB_PAGE_SIZE ;
	long long ep = (offset + (numBytes-1)) / GB_PAGE_SIZE ;
	// setup the cache list
	RdbList cacheList;
	key_t startKey;
	startKey.n1 = 0;
	startKey.n0 = 0;
	// point to the buffer to fill
	char *bufPtr = buf;
	char *bufEnd = buf + numBytes;
	// read in the pages
	while ( sp <= ep && bufPtr < bufEnd ) {
		cacheList.reset();
		// get the cache key for the page
		key_t cacheKey = makeClusterCacheKey ( vfd, sp );
		// read in the list from cache
		collnum_t collnum = 0;
		g_clusterdb.getRdb()->m_cache.getList ( collnum,
							(char*)&cacheKey,
							(char*)&startKey,
							&cacheList,
							false,
							3600*24*265,
							true );
		//cacheList.checkList_r ( false, true );
		//log ( LOG_INFO, "cache: got list [%li, %lli] [%li]",
		//		vfd, sp, cacheList.m_listSize );
		long size = cacheList.m_listSize;
		if ( size == 0 ) {
			cacheMiss = true;
			goto getPagesEnd;
		}
		//log ( LOG_INFO, "cache: got list [%li, %li] [%li]",
		//		vfd, sp, size );
		if ( bufPtr + size >= bufEnd )
			size = bufEnd - bufPtr;
		// copy the list into the buffer
		memcpy ( bufPtr, cacheList.m_list, size );
		// advance to the next page
		bufPtr += size;
		*newOffset += size;
		*newNumBytes -= size;
		sp++;
	}
getPagesEnd:
	if ( !cacheMiss ) {
		pc->m_hits++;
		// *newNumBytes = -(*newNumBytes);
	}
	else
		pc->m_misses++;
}

static void clusterAddPages ( DiskPageCache *pc,
			      long vfd,
			      char *buf,
			      long numBytes,
			      long long offset ) {
	// make sure we have a clean vfd
	if ( vfd < 0 || vfd >= MAX_NUM_VFDS2 )
		return;
	// make sure the file didn't get unlinked
	if ( ! pc->m_memOff[vfd] )
		return;
	// get the number of twins, used for filtering
	long numTwins  = g_hostdb.getNumHostsPerGroup();
	long thisTwin  = g_hostdb.m_hostId/g_hostdb.m_numGroups;
	// get the bias range for this twin
	long long biasStart = ((0x0000003fffffffffLL)/(long long)numTwins) *
		(long long)thisTwin;
	long long biasEnd;
	if ( thisTwin == numTwins - 1 )
		biasEnd = 0x0000003fffffffffLL + 1LL;
	else
		biasEnd = ((0x0000003fffffffffLL)/(long long)numTwins) *
			(long long)(thisTwin+1);
	// get the page range
	long long sp = offset / GB_PAGE_SIZE;
	// point to it
	char *bufPtr = buf;
	char *bufEnd = buf + numBytes;
	// how much did we exceed the boundary by?
	long skip = (long)(offset - sp * GB_PAGE_SIZE);
	long size = GB_PAGE_SIZE - skip;
	// setup the cache lists, may need to merge with an old list
	RdbList  cacheList1;
	cacheList1.set ( NULL,
			0,
			NULL,
			0,
			0,
			true,
			true,
			g_clusterdb.getRdb()->m_ks );
	cacheList1.growList(GB_PAGE_SIZE);
	// set the buffer data to a list so we can read it nicely
	key_t startKey;
	key_t endKey;
	startKey.n1 = 0;
	startKey.n0 = 0;
	endKey.n1 = 0xffffffff;
	endKey.n0 = 0xffffffffffffffffULL;
	// setup our source list
	RdbList dataList;
	dataList.set ( bufPtr,
		       numBytes,
		       bufPtr,
		       numBytes,
		       (char*)&startKey,
		       (char*)&endKey,
		       0,
		       false,
		       true,
		       g_clusterdb.getRdb()->m_ks );
	dataList.resetListPtr();
	// add pages to the cache
	while ( bufPtr < bufEnd ) {
		long filled  = 0;
		// ensure "size" is not too big
		if ( bufPtr + size > bufEnd )
			size = bufEnd - bufPtr;
		// . add the page to the cache
		cacheList1.reset();
		// check the first key, if it's too large, we're all done here
		key_t key = dataList.getCurrentKey();
		long long docId = g_clusterdb.getDocId ( key );
		//if ( docId >= biasEnd ) {
		//	log ( "clusterdb: DocId after bias end, key.n1=%lx key.n0=%llx", key.n1, key.n0 );
		//	log ( "clusterdb: DocId after bias end, %llx >= %llx", docId, biasEnd );
		//	return;
		//}
		// make the cache key using vfd and page number
		key_t cacheKey = makeClusterCacheKey ( vfd, sp );
		// filter the data into a list to be cached
		while ( filled < size && !dataList.isExhausted() ) {
			key = dataList.getCurrentKey();
			// check the key for filtering
			//long long docId = g_clusterdb.getDocId ( key );
			//long twin = hashLong((long)docId) % numTwins;
			//if ( twin == thisTwin ) {
				// add the key to the rdb list
				cacheList1.addRecord(key, 0, NULL);
			//}
			// next key
			filled += dataList.getCurrentRecSize();
			dataList.skipCurrentRecord();
		}
		collnum_t collnum = 0;
		// if the last key is too small, don't add the page
		docId = g_clusterdb.getDocId ( key );
		if ( docId >= biasStart )
			g_clusterdb.getRdb()->m_cache.addList ( collnum,
							cacheKey,
							&cacheList1 );
		//else
		//	log ( "clusterdb: DocId before bias start, %lli >= %lli", docId, biasStart );
		//cacheList1.checkList_r ( false, true );
		//log ( LOG_INFO, "cache: add list [%li, %lli] [%li]",
		//		vfd, sp, cacheList1.m_listSize );
		// advance
		bufPtr += filled;
		sp++;
		size = GB_PAGE_SIZE;
		skip = 0;
	}
}

static long clusterGetVfd ( DiskPageCache *pc,
			    long long maxFileSize ) {
	// pick a vfd for this file, will be used in the cache key
	long i;
	long count = MAX_NUM_VFDS2;
	for ( i = pc->m_nexti; count-- > 0; i++ ) {
		if ( i >= MAX_NUM_VFDS2 ) i = 0;
		if ( ! pc->m_memOff[i] ) break;
	}
	// bail if none left
	if ( count == 0 ) {
		g_errno = EBADENGINEER;
		log ( LOG_LOGIC, "db: pagecache: clusterGetVfd: "
				 "no vds remaining." );
		return -1;
	}
	// start looking here next time
	pc->m_nexti = i + 1;
	// set m_memOff[i] to something to hold the vfd
	pc->m_memOff[i] = (long*)0x7fffffff;
	// return the vfd
	return i;
}

static void clusterRmVfd ( DiskPageCache *pc,
			   long vfd ) {
	// make sure it's a clean vfd
	if ( vfd < 0 || vfd >= MAX_NUM_VFDS2 )
		return;
	// clear the vfd for use
	pc->m_memOff[vfd] = NULL;
	// need to clear out the cache records using this vfd
	collnum_t collnum = 0;
	key_t startKey, endKey;
	startKey.n1 = vfd + 1;
	startKey.n0 = 0;
	endKey.n1   = vfd + 1;
	endKey.n0   = 0xffffffffffffffffULL;
	g_clusterdb.getRdb()->m_cache.removeKeyRange ( collnum,
						       (char*)&startKey,
						       (char*)&endKey );
	//log ( LOG_INFO, "cache: BIASED CACHE REMOVED VFD!!" );
}
*/

// reset rdb
void Clusterdb::reset() { m_rdb.reset(); }

// . this no longer maintains an rdb of cluster recs
// . Msg22 now just uses the cache to hold cluster recs that it computes
//   from titlteRecs
// . clusterRecs are now just TitleRec keys...
// . we can load one the same from titledb as we could from clusterdb
//   and we still don't need to uncompress the titleRec to get the info
bool Clusterdb::init ( ) {
	// this should be about 200/4 = 50 megs per host on my current setup
	long maxTreeMem = g_conf.m_clusterdbMaxTreeMem;
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	long maxTreeNodes  = maxTreeMem / ( 16 + CLUSTER_REC_SIZE );
	// . each cahched list is just one key in the tree...
	// . 28(tree space) + 24(cacheoverhead) = 52
	//long maxCacheMem   = g_conf.m_clusterdbMaxCacheMem ;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	//long pcmem = g_conf.m_clusterdbMaxDiskPageCacheMem;
	long pcmem = 0;
	if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// we need that 100MB for termlists! they are >90MB now!!
	pcmem = 10000000; // 10MB
	// temp hack for rebuild
	//pcmem = 0;
	// RdbCache has a 4 byte ptr to each rec in the cache
	//long maxCacheNodes = maxCacheMem / ( 4 + CLUSTER_REC_SIZE );
	//long nodeSize      = sizeof(key_t) + sizeof(collnum_t);
	long pageSize      = GB_TFNDB_PAGE_SIZE;
	//long nodeSize      = (pageSize + 12) + sizeof(collnum_t) + 20;
	//long maxCacheNodes = maxCacheMem / nodeSize ;
	// init the page cache
	if ( ! m_pc.init ( "clusterdb",
			   RDB_CLUSTERDB,
			   pcmem      ,
			   pageSize ) )
			   //g_conf.m_clusterdbMaxDiskPageCacheMem,
			   //clusterGetPages,
			   //clusterAddPages,
			   //clusterGetVfd,
			   //clusterRmVfd ))
		return log("db: Clusterdb init failed.");
	//bool bias = true;
	//if ( g_conf.m_fullSplit ) bias = false;
	bool bias = false;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir  ,
			    "clusterdb"   ,
			    true          , // dedup
			    //CLUSTER_REC_SIZE - sizeof(key_t),//fixedDataSize 
			    0             , // no data now! just docid/s/c
			    g_conf.m_clusterdbMinFilesToMerge,
			    g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  , // maxTreeNodes  ,
			    true          , //false         , // balance tree?
			    0,//maxCacheMem   , 
			    0,//maxCacheNodes ,
			    true          , // half keys?
			    g_conf.m_clusterdbSaveCache,
			    &m_pc ,
			    false,  // is titledb
			    true ,  // preload disk page cache
			    12,     // key size
			    bias ); // bias disk page cache?
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Clusterdb::init2 ( long treeMem ) {
	// . what's max # of tree nodes?
	// . key+4+left+right+parents+dataPtr = 12+4 +4+4+4+4 = 32
	// . 28 bytes per record when in the tree
	long maxTreeNodes  = treeMem / ( 16 + CLUSTER_REC_SIZE );
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir     ,
			    "clusterdbRebuild" ,
			    true          , // dedup
			    0             , // no data now! just docid/s/c
			    50            , // m_clusterdbMinFilesToMerge,
			    treeMem       , // g_conf.m_clusterdbMaxTreeMem,
			    maxTreeNodes  , 
			    true          , // balance tree?
			    0             , // maxCacheMem   , 
			    0             , // maxCacheNodes ,
			    true          , // half keys?
			    false         , // g_conf.m_clusterdbSaveCache,
			    NULL          , // &m_pc ,
			    false         ,  // is titledb
			    false         ,  // preload disk page cache
			    12            ,     // key size
			    true          ); // bias disk page cache
}

bool Clusterdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}

bool Clusterdb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Clusterdb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//long minRecSizes = 64000;
	
	if ( ! msg5.getList ( RDB_CLUSTERDB ,
			      coll          ,
			      &list         ,
			      startKey      ,
			      endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      &msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	long count = 0;
	long got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		unsigned long groupId = getGroupId ( RDB_CLUSTERDB , &k );
		if ( groupId == g_hostdb.m_groupId ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %li records in clusterdb, "
		     "only %li belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Clusterdb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO, "db: Clusterdb passed verification successfully for "
			"%li recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

#include "IndexList.h"

// . this routine is very slow...
// . it is used to get a titleRec's (document's) sample vector at query time
//   but we should really compute this vector at build time and store it in
//   the titleRec itself, to avoid having to compute it at query time.
// . vector must have at least VECTOR_SIZE bytes available
/*
void Clusterdb::getSampleVector ( char *vec , 
				  class Doc *doc, 
				  char *coll ,
				  long  collLen ,
				  long niceness) {
	long long startTime = gettimeofdayInMilliseconds();
	TitleRec *tr = doc->getTitleRec();
	SiteRec  *sr = doc->getSiteRec();
	//sr->set ( tr->getSite() , tr->getColl() , tr->getCollLen() ,
	sr->set ( tr->getSite() , coll , collLen ,
		  tr->getSiteFilenum() , SITEREC_CURRENT_VERSION );
	// hashes the whole doc, but more importantly for us, computes
	// XmlDoc::m_vector
	//doc->set ( niceness );
	XmlDoc *xd = doc->getXmlDoc();
	xd->set ( tr , sr , NULL, niceness); 
	// this just sets the vector
	doc->getIndexList(NULL,true,true,false,NULL,NULL,NULL, niceness);
	// log the time
	long long took =gettimeofdayInMilliseconds()-startTime;
	if ( took > 3 )
		log(LOG_INFO,"query: Took %lli ms to make indexlist.",took);
	// so get it
	char *p = doc->getSampleVector ( );
	// and store it. short vectors are padded with 0's.
	memcpy ( vec , p , SAMPLE_VECTOR_SIZE );
}
*/

// if VECTOR_SIZE is 128 bytes then that is 32 termIds (4 bytes each) that we 
// use to make this vector. these 32 termids are the lowest 32 termids out of 
// all the termids for the document. we can further hash pairs to reduce the 
// vector size from 128 to 64 bytes. but we must hash the pair strategically.
// What are the odds of two things being 90% similar when they are not?
#define SAMPLE_VECTOR_LEN (SAMPLE_VECTOR_SIZE / 4)

// . it would be nice to use the new addition to the Words class that allows
//   a word to be a tag. this kinda replaces the xml class.
// . returns false and sets g_errno on error
/*
bool Clusterdb::getGigabitVector ( char *vec , Xml *xml ) {
	// . get filtered text, no link text since that is usually for menus
	// . get first 64k
	char buf[64*1024];
	xml->getText ( buf , 64*1024 );
	// hash into this table
	TermTable table;
	Query q;
	TopicGroup t;
	t.m_numTopics = 32;
	t.m_maxTopics = 32;
	t.m_docsToScanForTopics = 1;
	t.m_minTopicScore = 0;
	t.m_maxWordsPerTopic = 4;
	t.m_meta[0] = '\0';
	t.m_delimeter = 0;
	t.m_useIdfForTopics = true;
	t.m_dedup = false;
	t.m_minDocCount = 1;
	t.m_ipRestrict = false;
	t.m_dedupSamplePercent = 0;
	t.m_topicRemoveOverlaps = true;
	t.m_topicSampleSize = 64*1024;
	t.m_topicMaxPunctLen = 3;
	State23 st;
	st.m_numRequests = 1;
	st->m_msg20[0].m_bufSampleBuf    = buf;
	st->m_msg20[0].m_bufSampleBufLen = bufLen;
	st->m_returnDocIdCount = false;
	st->m_returnDocIds     = false;
	st->m_returnPops       = false;
	Msg24 msg24;
	if ( ! msg24.getTopics ( &st , // State24
				 &t  ,
				 &table ,
				 &q     ,
				 0      , // gid
				 &buf   , 
				 &bufLen ) )
		return false;
	// now hash the winning topics into our vector

}
*/

/*
void Clusterdb::getSampleVector ( char *vec , TermTable *table ) {
	// no compression is used in this list so each docId/termId is 12 bytes
	long numTerms = table->getNumTermsUsed();
	// . how many can we hold? we'll just use 4 bytes per vector component
	// . let's get 2x as many termids as required, then we will combine
	//   every 2 termids into one via hashing... this makes falsely high
	//   similarities less likely, but makes truly high similarities less
	//   likely to be detected as well.
	long maxTerms = (1 * SAMPLE_VECTOR_LEN)  - 1;
	// what portion of them do we want to mask out from the rest?
	long ratio = numTerms / maxTerms ;
	unsigned char mask = 0x00;
	while ( ratio >= 2 ) {
		// shift the mask down, ensure hi bit is set
		mask >>= 1;
		mask |= 0x80;
		ratio >>= 1; // /2
	}
	unsigned long d [ 3000 ];
	// if we don't have enough, make them 0's
	memset ( d   , 0 , SAMPLE_VECTOR_SIZE );
	memset ( vec , 0 , SAMPLE_VECTOR_SIZE );
 again:
	// a buffer to hold the top termIds
	long nd = 0;
	// . buffer should have at least "maxTerms" in it
	// . these should all be 12 byte keys
	long i = 0 ;
	long n = table->getNumTerms();
	long long     *termIds = table->getTermIds();
	unsigned long *scores  = table->getScores ();
	for ( ; i < n ; i++ ) {
		// skip if empty bucket
		if ( ! scores[i] ) continue;
		// skip if negative key, since we can be deleting old keys
		// from call from Msg14.cpp
		// NO! this should be the indexlist directly from Msg16, not
		// after subtracting the one from Msg15
		//if ( (*p & 0x01) == 0x00 ) continue;
		// skip if it's not to be considered
		//fprintf(stderr,"%hhu\n",p[11]);
		//if ( (p[11] & mask) != 0 ) continue;
		if ( ((termIds[i]>>(NUMTERMIDBITS-8)) & mask) != 0 ) continue;
		// add it
		//d[nd++] = *(long *)(p+12-5); // last byte has del bit, etc.
		d[nd] = (unsigned long)(termIds[i] >> (NUMTERMIDBITS-32));
		// 0 has special meaning, it terminates the vector
		if ( d[nd] == 0 ) d[nd] = 1;
		if ( ++nd < 3000 ) continue;
		// bitch and break out on error
		log(LOG_INFO,"build: Sample vector overflow. Slight "
		    "performance hit.");
		break;
	}
	// if nd was too small, don't use a mask to save time
	if ( nd < maxTerms && nd < numTerms && mask ) {
		// sanity check
		if ( mask == 0 ) {
			log (LOG_LOGIC,"build: Clusterdb sample vector mask "
			     "is already at 0.");
			char *xx = NULL; *xx = 0;
		}
		// debug msg
		//log("AGAIN");
		//val >>= 1;
		// shift the mask UP, allow more termIds to pass through
		mask <<= 1;
		goto again;
	}

	// bubble sort them
	bool flag = true;
	while ( flag ) {
		flag = false;
		for ( long i = 1 ; i < nd ; i++ ) {
			if ( d[i-1] <= d[i] ) continue;
			unsigned long tmp = d[i-1];
			d[i-1] = d[i];
			d[i]   = tmp;
			flag   = true;
		}
	}

	if ( nd > SAMPLE_VECTOR_LEN - 1 ) nd = SAMPLE_VECTOR_LEN - 1;
	// make sure last component is a 0
	d [ nd ] = 0;
	memcpy ( vec , (char *)d , (nd+1) * 4 );
}
*/

// return the percent similar
char Clusterdb::getSampleSimilarity ( char *vec0 , char *vec1, long size ) {
	// . the termIds are sorted
	// . point each recs sample vector of termIds
	//long *t0 = (long *)(vec0 + sizeof(key_t) + 3*4);
	//long *t1 = (long *)(vec1 + sizeof(key_t) + 3*4);
	// . we sorted them above as unsigned longs, so we must make sure
	//   we use unsigned longs here, too
	unsigned long *t0 = (unsigned long *)vec0;
	unsigned long *t1 = (unsigned long *)vec1;
	// if either is empty, return 0 to be on the safe side
	if ( *t0 == 0 ) return 0;
	if ( *t1 == 0 ) return 0;
	//long size0 = *(long *)(rec + sizeof(key_t));
	//long *end0 = (long *)(vec0 + *(long *)(vec0+12));
	//long *end1 = (long *)(vec1 + *(long *)(vec1+12));
	// how many total termIds?
	//long total = (end0 - t0 + end1 - t1) / 2;
	//if ( total <= 0 ) return 0;
	// count matches between the sample vectors
	long count = 0;
 loop:
	if( ((char*)t0 - vec0) > size ) {
		log( LOG_INFO, "query: sample vector 0 is malformed. "
		     "Returning 0%% similarity." );
		return 0;
	}
	if( ((char*)t1 - vec1) > size ) {
		log( LOG_INFO, "query: sample vector 1 is malformed. "
		     "Returning 0%% similarity." );
		return 0;
	}

	// terminate on a 0
	if      ( *t0 < *t1 ) { if ( *++t0 == 0 ) goto done; }
	else if ( *t1 < *t0 ) { if ( *++t1 == 0 ) goto done; }
	else    { 
		// if both are zero... do not inc count
		if ( *t0 == 0 ) goto done;
		count++; 
		t0++;
		t1++;
		if ( *t0 == 0 ) goto done;
		if ( *t1 == 0 ) goto done;
	}
	goto loop;

 done:
	// count total components in each sample vector
	while ( *t0 ) {
		t0++;
		if( ((char*)t0 - vec0) > size ) {
			log( LOG_INFO, "query: sample vector 0 is malformed. "
			     "Returning 0%% similarity." );
			return 0;
		}
	}
	while ( *t1 ) {
		t1++;
		if( ((char*)t1 - vec1) > size ) {
			log( LOG_INFO, "query: sample vector 1 is malformed. "
			     "Returning 0%% similarity." );
			return 0;
		}
	}
	long total = 0;
	total += t0 - ((unsigned long *)vec0);
	total += t1 - ((unsigned long *)vec1);
	// how similar are they?
	// if both are empty, assume not similar at all. this happens if we
	// do not have a content vector for either, or if both are small docs
	// with no words or links in them (framesets?)
	if ( total == 0 ) return 0;
	long sim = (count * 2 * 100) / total;
	if ( sim > 100 ) sim = 100;
	return (char)sim;
}

/*
// return the percent similar
char Clusterdb::getGigabitSimilarity ( char *vec0 , char *vec1 ,
				       long *qtable , long numSlots ) {
	// . the termIds are sorted
	// . point each recs sample vector of termIds
	//long *t0 = (long *)(vec0 + sizeof(key_t) + 3*4);
	//long *t1 = (long *)(vec1 + sizeof(key_t) + 3*4);
	unsigned long *t0  = (unsigned long *)vec0;
	unsigned long *t1  = (unsigned long *)vec1;
	short *s0 = (short *)(vec0 + 4*GIGABITS_IN_VECTOR);
	short *s1 = (short *)(vec1 + 4*GIGABITS_IN_VECTOR);
	long i0 = 0;
	long i1 = 0;
	// if both empty, cluster together... assume same topic
	//if ( *t0 == 0 && *t1 == 0 ) return 100;
	if ( *t0 == 0 && *t1 == 0 ) return 0;
	// if either is empty, return 0 to be on the safe side
	if ( *t0 == 0 ) return 0;
	if ( *t1 == 0 ) return 0;
	if ( numSlots == 0 ) return 0;
	//long size0 = *(long *)(rec + sizeof(key_t));
	//long *end0 = (long *)(vec0 + *(long *)(vec0+12));
	//long *end1 = (long *)(vec1 + *(long *)(vec1+12));
	// how many total termIds?
	//long total = (end0 - t0 + end1 - t1) / 2;
	//if ( total <= 0 ) return 0;
	// count matches between the sample vectors
	long count = 0;
	long n;
	unsigned long mask = numSlots - 1;
 loop:
	// skip if t0[i0] matches a query term
	n = t0[i0] & mask;
	while ( qtable[n] && qtable[n] != (long)t0[i0] )
		if ( ++n >= numSlots ) n = 0;
	if ( qtable[n] ) {
		s0[i0] = 0; // remove score for tallying up total
		i0++; if (t0[i0] == 0 || i0>=GIGABITS_IN_VECTOR) goto done; }
	// skip if t1[i1] matches a query term
	n = t1[i1] & mask;
	while ( qtable[n] && qtable[n] != (long)t1[i1] )
		if ( ++n >= numSlots ) n = 0;
	if ( qtable[n] ) {
		s1[i1] = 0; // remove score for tallying up total
		i1++; if (t1[i1] == 0 || i1>=GIGABITS_IN_VECTOR) goto done; }
	// terminate on a 0
	if      ( t0[i0] < t1[i1] ) { 
		i0++; if (t0[i0] == 0 || i0>=GIGABITS_IN_VECTOR) goto done; }
	else if ( t1[i1] < t0[i0] ) { 
		i1++; if (t1[i1] == 0 || i1>=GIGABITS_IN_VECTOR) goto done; }
	else    { 
		// if both are zero... do not inc count
		if ( t0[i0] == 0 ) goto done;
		//count++; 
		// now we do a weighted count
		count += s0[i0] + s1[i1];
		i0++;
		i1++;
		if ( t0[i0] == 0 || i0>=GIGABITS_IN_VECTOR) goto done;
		if ( t1[i1] == 0 || i1>=GIGABITS_IN_VECTOR) goto done;
	}
	goto loop;

 done:
	// count total components in each sample vector
	while ( t0[i0] && i0 < GIGABITS_IN_VECTOR ) i0++;
	while ( t1[i1] && i1 < GIGABITS_IN_VECTOR ) i1++;
	long total = 0;
	//total += t0 - ((long *)vec0);
	//total += t1 - ((long *)vec1);
	// get total score
	for ( long i = 0 ; i < i0 ; i++ ) total += s0[i] ;
	for ( long i = 0 ; i < i1 ; i++ ) total += s1[i] ;
	// how similar are they?
	// if both are empty, assume not similar at all. this happens if we
	// do not have a content vector for either, or if both are small docs
	// with no words or links in them (framesets?)
	if ( total == 0 ) return 0;
	//long sim = (count * 2 * 100) / total;
	long sim = (count * 100) / total;
	if ( sim > 100 ) sim = 100;
	return (char)sim;
}
*/

key_t Clusterdb::makeClusterRecKey ( long long     docId,
				     bool          familyFilter,
				     uint8_t       languageBits,
				     long          siteHash,
				     bool          isDelKey,
				     bool          isHalfKey ) {
	key_t key;
	// set the docId upper bits
	key.n1 = (unsigned long)(docId >> 29);
	key.n1 &= 0x000001ff;
	// set the docId lower bits
	key.n0 = docId;
	key.n0 <<= 35;
	// set the family filter bit
	if ( familyFilter ) key.n0 |= 0x0000000400000000ULL;
	else                key.n0 &= 0xfffffffbffffffffULL;
	// set the language bits
	key.n0 |= ((unsigned long long)(languageBits & 0x3f)) << 28;
	// set the site hash
	key.n0 |= (unsigned long long)(siteHash & 0x03ffffff) << 2;
	// set the del bit
	if ( isDelKey ) key.n0 &= 0xfffffffffffffffeULL;
	else            key.n0 |= 0x0000000000000001ULL;
	// set half bit
	if ( !isHalfKey ) key.n0 &= 0xfffffffffffffffdULL;
	else              key.n0 |= 0x0000000000000002ULL;
	// return the key
	return key;
}

/*
key_t Clusterdb::convertTitleRecKey ( key_t titleKey ) {
	// extract the docid
	long long docId;
	docId = titleKey.n1;
	docId <<= 6;
	docId |= titleKey.n0 >> 58;
	// extract the family filter
	bool familyFilter;
	if ( ( titleKey.n1 & 0x0100000000000000ULL ) ||
	     ( titleKey.n1 & 0x0200000000000000ULL ) )
		familyFilter = true;
	else
		familyFilter = false;
	// extract the site hash
	unsigned long siteHash;
	siteHash = (unsigned long)((titleKey.n0 >> 30) & 0x0000000003ffffffULL);
	// make and return the key
	return makeClusterRecKey ( docId, familyFilter, 0, siteHash, false );
}

void Clusterdb::makeRecFromTitleRec ( char     *rec,
				      TitleRec *titleRec,
				      bool      isDelKey ) {
	// get the docId
	long long docId = titleRec->getDocId();
	// get the family filter
	bool familyFilter = titleRec->hasAdultContent();
	// get the language byte
	unsigned char lang = titleRec->getLanguage();
	// . get the site hash
	// . this is really the host hash because tfndb key most use
	//   the host hash in case site changes in tagdb
	unsigned long siteHash = titleRec->getHostHash();
	// make the key and copy it to rec
	key_t key = makeClusterRecKey ( docId,
					familyFilter,
					lang,
					siteHash,
					false );
	memcpy(rec, &key, sizeof(key_t));
}

void Clusterdb::makeRecFromTitleRecKey ( char *rec,
					 char *key,
					 bool  isDelKey ) {
	// get the docId
	long long docId = g_titledb.getDocIdFromKey((key_t*)key);
	// get the family filter
	bool familyFilter = g_titledb.hasAdultContent(*(key_t*)key);
	// . get the site hash
	// . this is really the host hash because tfndb key most use
	//   the host hash in case site changes in tagdb
	unsigned long siteHash = g_titledb.getHostHash((key_t*)key);
	// make the key and copy it to rec
	key_t ckey = makeClusterRecKey ( docId,
					 familyFilter,
					 0,
					 siteHash,
					 false );
	memcpy(rec, &ckey, sizeof(key_t));
}
*/
