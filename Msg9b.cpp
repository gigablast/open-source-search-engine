#include "gb-include.h"

#include "Msg9b.h"


// . returns false if blocked, true otherwise
// . sets g_errno on error
// . numCatids is an array of longs, each the number of catids for
//   the corrisponding url, length equal to number of urls
// . catids is an array of longs with the catids for the urls
bool Msg9b::addCatRecs ( char *urls        ,
			 char *coll        , 
			 long  collLen     ,
			 long  filenum     ,
			 void *state       ,
			 void (*callback)(void *state) ,
			 unsigned char *numCatids   ,
			 long *catids      ,
			 long niceness,
			 bool deleteRecs) {
	//long dbIndex = RDB_CATDB;
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
	long usize = gbstrlen(urls);
	long initSize = ((usize / 15)*(12+4+4+1) + usize);
	if ( ! m_list.growList ( initSize ) ) {
		g_errno = ENOMEM;
		log("admin: Failed to allocate %li bytes to hold "
		    "urls to add to tagdb.", initSize);
		return true;
	}
	// loop over all urls in "urls" buffer
	char *p = urls;
	// stop when we hit the NULL at the end of "urls"
	long k = 0;
	long c = 0;
	long lastk = 0;
	//long firstPosIds = -1;
	while ( *p ) {
		if (  *p != '\n' ||  lastk != k   ) {
			log (LOG_WARN, "Msg9b: FOUND BAD URL IN LIST AT %li, "
				       "EXITING", k );
			return true;
		}
		lastk++;
		// skip initial spaces
		char *lastp = p;
		while ( is_wspace_a (*p) ) p++;
		if (  p - lastp > 1 ) {
			log(LOG_WARN, "Msg9b: SKIPPED TWO SPACES IN A ROW AT "
				      " %li, EXITING", k );
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
		Url site;
		site.set ( p , e - p , false/*addwww?*/);
		// normalize the url
		g_catdb.normalizeUrl(&site, &site);
		// make a siteRec from this url
		CatRec sr;
		// returns false and sets g_errno on error
		if ( ! sr.set ( &site, filenum, &catids[c], numCatids[k] ) )
			return true;
		// add url to our list
		// extract the record itself (SiteRec::m_rec/m_recSize)
		char *data     = sr.getData ();
		long  dataSize = sr.getDataSize ();
		key_t key;
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
		
		//skip:
		// now advance p to e
		p = e;
	
		c += numCatids[k];
		k++;
		
		QUICKPOLL((niceness));
	}
	log ( LOG_INFO, "Msg9b: %li sites and %li links added", k , c );
	// . now add the m_list to tagdb using msg1
	// . use high priority (niceness of 0)
	// . i raised niceness from 0 to 1 so multicast does not use the
	//   small UdpSlot::m_tmpBuf... might have a big file...
	return m_msg1.addList ( &m_list, RDB_CATDB, coll ,
				state , callback ,
				false , // force local?
				niceness     ); // niceness 
}
