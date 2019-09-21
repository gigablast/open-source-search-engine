#include "gb-include.h"

#include <sys/stat.h>
#include "Catdb.h"
#include "Categories.h"
#include "CatRec.h"
#include "Unicode.h"
#include "Threads.h"

// use this to query delete all the banned sites in tagdb
// dsh -a '/a/gb -c /a/hosts.conf dump S main 0 -1 1 | grep =19' | awk '{print $6}' | sort | uniq | grep "\." > banned
// cat banned | awk -F'http://' '{print $2}' | grep "[A-z]" | awk '{print "http://64.62.168.52:8000/admin/reindex?c=main&cast=0&sq=site%3A"$1"&delbox=1&f=-1&srn=0&ern=2000000&sto=0&sp=7&action=OK"}' > URLS
// cat banned | awk -F'/' '{print $3}' | grep -v "[A-z]" | awk '{print "http://64.62.168.52:8000/admin/reindex?c=main&cast=0&sq=ip%3A"$1"&delbox=1&f=-1&srn=0&ern=2000000&sto=0&sp=7&action=OK"}' >> URLS
// nohup wget -i /a/URLS -O /dev/null &

// a global class extern'd in .h file
Catdb  g_catdb;

// reset rdb and Xmls
void Catdb::reset() {
	m_rdb.reset();
}


bool Catdb::init (  ) {
	// clear our m_keys/m_bufs arrays
	//	memset ( m_xml , 0 , MAXNUMSITEFILES * sizeof(Xml     *) );
	//memset ( m_keys, 0 , MAXNUMSITEFILES * sizeof(int64_t) );
	// . what's max # of tree nodes?
	// . assume avg tagdb rec size (siteUrl) is about 82 bytes we get:
	// . NOTE: 32 bytes of the 82 are overhead
	//int32_t treeMem = g_conf.m_catdbMaxTreeMem;
	// speed up gen catdb, use 15MB. later maybe once gen is complete
	// we can free this tree or something...
	// TODO!
	int32_t treeMem = 15000000;
	//int32_t treeMem = 100000000;
	//int32_t maxTreeNodes = g_conf.m_catdbMaxTreeMem / 82;
	int32_t maxTreeNodes = treeMem / 82;
	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	// int32_t pcmem = g_conf.m_catdbMaxDiskPageCacheMem;
	// if ( g_hostdb.m_useTmpCluster ) pcmem = 0;

	// pcmem = 0;
	// each entry in the cache is usually just a single record, no lists,
	// unless a hostname has multiple sites in it. has 24 bytes more 
	// overhead in cache.
	//int32_t maxCacheNodes = g_conf.m_tagdbMaxCacheMem / 106;
	// we now use a page cache
	// if ( ! m_pc.init ("catdb",RDB_CATDB,pcmem,
	// 		  GB_TFNDB_PAGE_SIZE) )
	// 	return log("db: Catdb init failed.");

	// . initialize our own internal rdb
	// . i no longer use cache so changes to tagdb are instant
	// . we still use page cache however, which is good enough!
	//if ( this == &g_catdb )
	if ( !  m_rdb.init ( g_hostdb.m_dir               ,
			    "catdb"                   ,
			    true                       , // dedup same keys?
			    -1                         , // fixed record size
			    //g_hostdb.m_groupMask         ,
			    //g_hostdb.m_groupId           ,
			     2,//g_conf.m_catdbMinFilesToMerge   ,
				    treeMem ,//g_conf.m_catdbMaxTreeMem  ,
			    maxTreeNodes               ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			     NULL, // &m_pc                      ,
				    false,
				    false,
			     12, // keysize
				    false,
			     true )) // is collectionless?
		return false;

	// normally Collectiondb.addColl() will call Rdb::addColl() which
	// will init the CollectionRec::m_rdbBase, which is what
	// Rdb::getBase(collnum_t) will return. however, for collectionless
	// rdb databases we set Rdb::m_collectionlessBase special here.
	// This was in Rdb.cpp::init().
	return m_rdb.addRdbBase1 ( NULL );
}

bool Catdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg tagdb rec size (siteUrl) is about 82 bytes we get:
	// . NOTE: 32 bytes of the 82 are overhead
	int32_t maxTreeNodes = 0;
	
	return m_rdb.init ( g_hostdb.m_dir             ,
			    "tagdbRebuild"            ,
			    true                       , // dedup same keys?
			    -1                         , // fixed record size
			    10                         , // min to merge
			    treeMem                    ,
			    maxTreeNodes               ,
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			    NULL                      ); //&m_pc
}

//
// end support for "cache recs"
//	

/*
bool Catdb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	// verify
	return true;
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}
*/

bool Catdb::verify ( char *coll ) {
	char *rdbName = "Catdb";
	log ( LOG_INFO, "db: Verifying %s for coll %s...", rdbName, coll );
	g_threads.disableThreads();

	Msg5 msg5;
	//Msg5 msg5b;
	RdbList list;
	key_t startKey;
	key_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	
	if ( ! msg5.getList ( RDB_CATDB     ,
			      0,//collnum          ,
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
			      NULL,//&msg5b        ,
			      true          )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key_t k = list.getCurrentKey();
		count++;
		//uint32_t groupId = g_catdb.getGroupId ( &k );
		//uint32_t shardNum = getShardNum ( RDB_CATDB , &k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		uint32_t shardNum = getShardNum( RDB_CATDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %"INT32" records in %s, only %"INT32" belong "
		     "to our group.",count,rdbName,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to %s inconsistency.", rdbName );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}
	log ( LOG_INFO, "db: %s passed verification successfully for %"INT32" recs.",
			rdbName, count );
	// DONE
	g_threads.enableThreads();
	return true;
}

void Catdb::normalizeUrl ( Url *srcUrl, Url *dstUrl ) {
	char urlStr[MAX_URL_LEN];
	int32_t urlStrLen = srcUrl->getUrlLen();
	gbmemcpy(urlStr, srcUrl->getUrl(), urlStrLen);
	// fix the url
	urlStrLen = g_categories->fixUrl(urlStr, urlStrLen);
	// create the normalized url
	dstUrl->set(urlStr, urlStrLen, true, false, false, true);
}

// . dddddddd dddddddd dddddddd dddddddd  d = domain hash w/o collection
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  u = url hash
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  
key_t Catdb::makeKey ( Url *site, bool isDelete ) {

	key_t k;
	// . get startKey based on "site"'s domain
	// . if "site"'s domain is an ip address (non-canonical) then use ip
	getKeyRange ( site->isIp() , site , &k , NULL);
	// set lower 64 bits of key to hash of this url
	k.n0 = hash64 ( site->getUrl() , site->getUrlLen() );
	// clear low bit if we're a delete, otherwise set it
	if ( isDelete ) k.n0 &= 0xfffffffffffffffeLL;
	else            k.n0 |= 0x0000000000000001LL;
	return k;
}

// . get startKey,endKey for all SiteRecs from "url"'s domain
// . key has the following format:
// . dddddddd dddddddd dddddddd dddddddd  d = domain hash w/ collection
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  u = url hash
// . uuuuuuuu uuuuuuuu uuuuuuuu uuuuuuuu  
// . putting domain as first 32bits will cluster all SiteRecs from the
//   same domain together on the same machine
void Catdb::getKeyRange ( bool useIp , Url *url, 
			   key_t *startKey , key_t *endKey ) {
	// log warning msg if we need to
	if ( useIp && ! url->hasIp() ) 
		log(LOG_LOGIC,"db: tagdb: getKeyRange: useIp is true, "
		    "but url has no ip");
	// . the upper 32 bits of the key is basically hash of the domain
	// . mask out the low-order byte (hi byte in little endian order)
	uint32_t h;
	// . make sure we use htonl() on ip domain so top byte is not zero!
	// . this made all our ip-based sites stored in group #0 before
	//   if ( useIp ) h = htonl ( url->getIpDomain() ) ;
	// . only hash first 3 bytes of ip domain to keep together w/ ip
	// . if rdbid is tagdb then use hostname as key else use domain
	if   ( useIp ) {
		// do htonl so most significant byte is first
		int32_t ipdom = htonl(url->getIpDomain());
		h = hash32 ( (char *)&ipdom , 3 ) ;
	}
	else
		h = hash32 (url->getDomain(), url->getDomainLen());
		
	// incorporate collection into "h"
	//h = hash32 ( coll , collLen , h  );
	// now make the keys
	key_t k;
	// top 4 bytes is always the domain hash (ip or canonical domain)
	k.n1 = h;
	// don't set the low del bit for startKey
	k.n0 = 0x0000000000000000LL;
	// assign the startKey
	if ( startKey ) *startKey = k;
	// set the low del bit for startKey
	k.n0 = 0xffffffffffffffffLL;
	// endkey is just as simple
	if ( endKey   ) *endKey   = k;
}

// move the current list pointer back until we hit the start of
// a valid key
char *Catdb::moveToCorrectKey ( char    *listPtr,
				 RdbList *list,
				 uint32_t domainHash ) {
	char *listEnd   = list->getListEnd();
	char *listStart = list->getList();
	char *p = listPtr;
	// move back from the end
	if (listEnd - p < (int32_t)sizeof(key_t))
		p -= sizeof(key_t);
	// loop until we get it
	for ( ; p > listStart; p--){
		// check for the domain hash in the key
		if ( ((key_t*)p)->n1 == domainHash ) {
			// . verify the match
			//   get the current rec size and check
			//   the next rec for correct data
			int32_t recSize = list->getRecSize(p);
			char *checkp = p + recSize;
			// step 1, verify the start of the next rec is good
			if ( recSize >= 0 && ( checkp == listEnd ||
			    ( checkp < listEnd &&
			      ((key_t*)checkp)->n1 == domainHash ) ) ) {
				// return if we're at end
				if ( checkp == listEnd )
					return p;
				// step 2, verify good rec on next rec
				recSize = list->getRecSize(checkp);
				checkp = checkp + recSize;
				if ( recSize >= 0 && ( checkp == listEnd ||
				    ( checkp < listEnd &&
				      ((key_t*)checkp)->n1 == domainHash )))
					// good match, return it
					return p;
			}
		}
		// otherwise backup a byte
	}
	// we'll get here if p == listStart
	return p;
}

// binary search on the given list for the given key
void Catdb::listSearch ( RdbList *list,
			  key_t    exactKey,
			  char   **data,
			  int32_t    *dataSize ) {
	// init the data
	*data = NULL;
	*dataSize = 0;
	list->resetListPtr();
	// for small lists, just loop through the list
	if (list->getListSize() < 16*1024) {
		while ( ! list->isExhausted() ) {
			// for debug!
			/*
			CatRec crec;
			crec.set ( NULL,
				   list->getCurrentData(),
				   list->getCurrentDataSize(),
				   false);
			log("catdb: caturl=%s #catid=%"INT32" version=%"INT32""
			    ,crec.m_url
			    ,(int32_t)crec.m_numCatids
			    ,(int32_t)crec.m_version
			    );
			*/
			// check the current key
			if ( list->getCurrentKey() != exactKey ) {
				// miss, next
				list->skipCurrentRecord();
				continue;
			}
			// get site from this rec
			*data     = list->getCurrentData();
			*dataSize = list->getCurrentDataSize();
			break;
		}
	}
	// otherwise do a binary search on the large lists
	else {
		// init the low and high
		char *low     = list->getList();
		char *high    = list->getListEnd();
		// move the high ptr to the start of last rec
		high = moveToCorrectKey(high, list, exactKey.n1);
		// binary search
		char *currRec;
		while ( low <= high ) {
			// next check spot
			int32_t delta = high - low;
			currRec = low + (delta / 2);
			//currRec = (char*)(((uint64_t)low + 
			//	           (uint64_t)high)/2);
			// do correction
			currRec = moveToCorrectKey( currRec,
						    list,
						    exactKey.n1 );
			// check for hit
			if (list->getKey(currRec) == exactKey) {
				// hit, save it and get out
				*data = list->getData(currRec);
				*dataSize = list->getDataSize(currRec);
				break;
			}
			else if (list->getKey(currRec) > exactKey) {
				// move high to currRec - one rec
				high = moveToCorrectKey ( currRec - 1,
							  list,
							  exactKey.n1 );
			}
			else {
				// move low to currRec + one rec
				low = currRec + list->getRecSize(currRec);
			}
		}
	}
}

// now given an RdbList of SiteRecs can we find the best matching rec
// for our site?
char *Catdb::getRec ( RdbList *list , Url *url , int32_t *recSize,
		       char* coll, int32_t collLen  ) {
	key_t exactKey;
	int64_t startTime = gettimeofdayInMilliseconds();
	int64_t took;
	char *data;
	int32_t  dataSize;
	// for now, only get exact hits for catdb
	// check for an exact key/url match
	exactKey = makeKey(url, false);
	// go throught the list looking for the exact key
	//list->resetListPtr();
	data = NULL;
	dataSize = 0;
	// call the search
	listSearch ( list, exactKey, &data, &dataSize );
	// make sure the url matches
	if (data && dataSize > 0) {
		// get the url
		/*
		char *x;
		int32_t  xlen;
		// hit, check the url
		// for catdb, skip over the catids
		if (m_rdbid == RDB_CATDB) {
			unsigned char numCatids = *data;
			// . point to stored url/site
			// . skip dataSize/fileNum
			int32_t  skip = 1 + (4 * numCatids) + 4;
			x    = data + skip;
			xlen = dataSize - skip;
		}
		else {
			// . point to stored url/site
			// . skip dataSize/fileNum
			x    = data + 4;
			xlen = dataSize - 4;
		}
		// set the site
		Url site;
		site.set ( x , xlen , false );
		*/
		CatRec site;
		site.set (url, data, dataSize, false);
		// check for an exact match against the full url
		int32_t  uflen = url->getUrlLen();
		char *ufull = url->getUrl();
		//int32_t  sflen = site.getUrlLen();
		//char *sfull = site.getUrl();
		int32_t  sflen = site.m_urlLen;
		char *sfull = site.m_url;
		// if we match, return this rec
		if ( sflen == uflen &&
		     strncmp ( sfull, ufull, sflen ) == 0 ) {
			*recSize = dataSize;
		}
		else {
			*recSize = 0;
			data = NULL;
		}
	}
	else {
		*recSize = 0;
		data = NULL;
	}
	took = gettimeofdayInMilliseconds() - startTime;
	if ( took > 10 ) 
		log(LOG_INFO, "catdb: catdb lookup took %"INT64" ms, "
		    "listSize=%"INT32"", took, list->getListSize() );
	return data;
}

// . find the indirect matches in the list which match a sub path
//   of the url
int32_t  Catdb::getIndirectMatches ( RdbList  *list ,
				   Url      *url ,
				   char    **matchRecs ,
				   int32_t     *matchRecSizes ,
				   int32_t      maxMatches ,
				   char     *coll,
				   int32_t      collLen) {
	char  path[MAX_URL_LEN+1];
	int32_t  pathLen;
	Url   partialUrl;
	key_t partialUrlKey;
	// start with the whole url...include real catid in indirect
	gbmemcpy(path, url->getUrl(), url->getUrlLen());
	pathLen = url->getUrlLen();
	// loop looking for partial matches
	char *data       = NULL;
	int32_t  dataSize   = 0;
	int32_t  numMatches = 0;
	while ( numMatches < maxMatches ) {
		// make the partial url
		partialUrl.set(path, pathLen, true);
		normalizeUrl(&partialUrl, &partialUrl);
		// make the next key
		partialUrlKey = makeKey ( &partialUrl, false );
		// search for it
		listSearch ( list, partialUrlKey, &data, &dataSize );
		// store a hit
		if ( data && dataSize > 0 ) {
			// get the url
			char *x;
			int32_t  xlen;
			// hit, check the url
			// for catdb, skip over the catids
			/*
			unsigned char numCatids = *data;
			// . point to stored url/site
			// . skip dataSize/fileNum
			int32_t  skip = 1 + (4 * numCatids) + 4;
			x    = data + skip;
			xlen = dataSize - skip;
			*/
			CatRec sr;
			sr.set ( url, data, dataSize, false);
			x    = sr.m_url;
			xlen = sr.m_urlLen;
			// ensure it's a sub-path
			if ( xlen <= url->getUrlLen() &&
			     strncasecmp(x, url->getUrl(), xlen) == 0 ) {
				//char msg[4096];
				//char *mp = msg;
				//mp += sprintf(mp, "For ");
				//gbmemcpy(mp, url->getUrl(), url->getUrlLen());
				//mp += url->getUrlLen();
				//mp += sprintf(mp, " , got Indirect: ");
				//gbmemcpy(mp, x, xlen);
				//mp += xlen;
				//*mp = '\0';
				//log ( LOG_INFO, "tagdb: %s", msg );
				matchRecs    [numMatches] = data;
				matchRecSizes[numMatches] = dataSize;
				numMatches++;
			}
		}
		// make the next partial url
		pathLen--;
		while ( pathLen > 3 && path[pathLen-1] != '/' )
			pathLen--;
		// check for end
		if ( pathLen <= 3 || strncmp(&path[pathLen-3], "://", 3) == 0 )
			break;
		// chop off the trailing /
		pathLen--;
	}
	return numMatches;
}
