#include "gb-include.h"

#include "Msg9b.h"


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . numCatids is an array of int32_ts, each the number of catids for
//   the corrisponding url, length equal to number of urls
// . catids is an array of int32_ts with the catids for the urls
bool Msg9b::addCatRecs ( char *urls        ,
			 char *coll        , 
			 int32_t  collLen     ,
			 int32_t  filenum     ,
			 void *state       ,
			 void (*callback)(void *state) ,
			 unsigned char *numCatids   ,
			 int32_t *catids      ,
			 int32_t niceness,
			 bool deleteRecs) {
	//int32_t dbIndex = RDB_CATDB;
	// use default collection
	//coll    = g_conf.m_dirColl;
	//collLen = gbstrlen(coll);
	// catdb uses a dummy collection now, should not be looked at
	coll = "catdb";
	collLen = 5;
	
	// warning
	//if ( ! coll ) log(LOG_LOGIC,"net: NULL collection. msg9b.");
	// reset/free our list of siteRecs
	m_list.set ( NULL  , 
		     0     , 
		     NULL  ,
		     0     , 
		     -1    ,  // fixedDataSize
		     true  ,  // ownData?
		     false ); // use half keys?
	// ensure NULL terminated
	if ( coll[collLen] ) {
		log(LOG_LOGIC,"admin: Collection not NULL terminated.");
		char *xx = NULL; *xx = 0;
	}
	// . SiteRec bitmap is given in SiteRec.cpp
	// . key(12) + dataSize(4) + filenum(4) + url + ?NULL?
	// . assume about 15 bytes per url
	// . hopefully this will be big enough to prevent many reallocs
	int32_t usize = gbstrlen(urls);
	int32_t initSize = ((usize / 15)*(12+4+4+1) + usize);
	if ( ! m_list.growList ( initSize ) ) {
		g_errno = ENOMEM;
		log("admin: Failed to allocate %"INT32" bytes to hold "
		    "urls to add to tagdb.", initSize);
		return true;
	}
	// loop over all urls in "urls" buffer
	char *p = urls;
	// stop when we hit the NULL at the end of "urls"
	int32_t k = 0;
	int32_t c = 0;
	int32_t lastk = 0;
	//int32_t firstPosIds = -1;
	while ( *p ) {
		if (  *p != '\n' ||  lastk != k   ) {
			log (LOG_WARN, "Msg9b: FOUND BAD URL IN LIST AT %"INT32", "
				       "EXITING", k );
			return true;
		}
		lastk++;
		// skip initial spaces
		char *lastp = p;
		while ( is_wspace_a (*p) ) p++;
		if (  p - lastp > 1 ) {
			log(LOG_WARN, "Msg9b: SKIPPED TWO SPACES IN A ROW AT "
				      " %"INT32", EXITING", k );
			//return true;
		}
		// break if end
		if ( ! *p )
			break;
		// . if comment, skip until \n
		// . also, skip if its not alnum
		// . this is really assuming a format of one url per line,
		//   so a space separated list of urls may not work here
		if ( ! is_alnum_a (*p) ) { // *p == '#' ) { 
			while ( *p && *p != '\n' ) p++;
			// skip over the \n
			p++;
			// try the next line
			continue;
		}
		// find end of this string of non-spaces
		char *e = p; while ( *e && ! is_wspace_a (*e) ) e++;
		// . set the url
		// . but don't add the "www."
		// . watch out for
		//   http://twitter.com/#!/ronpaul to http://www.twitter.com/
		//   so do not strip # hashtags
		Url site;
		site.set ( p , e - p , false ); // addwww?
		// normalize the url
		g_catdb.normalizeUrl(&site, &site);

		// sanity
		if ( numCatids[k] > MAX_CATIDS ) { char *xx=NULL;*xx=0; }

		// make a siteRec from this url
		CatRec sr;
		// returns false and sets g_errno on error
		if ( ! sr.set ( &site, filenum, &catids[c], numCatids[k] ) )
			return true;
		// add url to our list
		// extract the record itself (SiteRec::m_rec/m_recSize)
		char *data     = sr.getData ();
		int32_t  dataSize = sr.getDataSize ();
		key_t key;
		// sanity test
		CatRec cr2;
		if ( ! cr2.set ( NULL , sr.getData(), sr.getDataSize(),false)){
			char *xx=NULL;*xx=0; }
		// debug when generating catdb
		//char *x = p;
		//for ( ; x<e ; x++ ) {
		//	if ( x[0] == '#' )
		//		log("hey");
		//}
		if ( numCatids[k] == 0 )
			key = g_catdb.makeKey(&site, true);
		else
			key = g_catdb.makeKey(&site, false);

		// if it's an ip then ensure only last # can be 0
		//if ( site.isIp() &&  (site.getIp() & 0x00ff0000) == 0 )
		//	goto skip;
		// . add site rec to our list
		// . returns false and sets g_errno on error
		if ( numCatids[k] == 0 ) {
			if ( !m_list.addRecord(key, 0, NULL) )
				return true;
		}
		else if ( ! m_list.addRecord ( key, dataSize, data ) )
			return true;

		/*
		// debug point
		SafeBuf sb;
		//sb.safeMemcpy(p , e-p );
		sb.safeStrcpy(sr.m_url);
		sb.safePrintf(" ");
		for ( int32_t i = 0 ; i < numCatids[k] ; i++ )
			sb.safePrintf ( "%"INT32" " , catids[c+i] );
		log("catdb: adding key=%s url=%s",
		    KEYSTR(&key,12),
		    sb.getBufStart());
		*/

		// debug
		//log("gencat: adding url=%s",sr.m_url);

		//skip:
		// now advance p to e
		p = e;
	
		c += numCatids[k];
		k++;
		
		QUICKPOLL((niceness));
	}
	log ( LOG_INFO, "Msg9b: %"INT32" sites and %"INT32" links added. "
	      "listSize=%"INT32"", k , c , m_list.m_listSize );
	// . now add the m_list to tagdb using msg1
	// . use high priority (niceness of 0)
	// . i raised niceness from 0 to 1 so multicast does not use the
	//   small UdpSlot::m_tmpBuf... might have a big file...
	return m_msg1.addList ( &m_list, RDB_CATDB, 
				(collnum_t)0 ,
				state , callback ,
				false , // force local?
				niceness     ); // niceness 
}
