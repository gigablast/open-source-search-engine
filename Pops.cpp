#include "gb-include.h"

#include "Pops.h"
#include "Words.h"
#include "StopWords.h"
#include "Speller.h"

Pops::Pops () {
	m_pops = NULL;
}

Pops::~Pops() {
	if ( m_pops && m_pops != (int32_t *)m_localBuf )
		mfree ( m_pops , m_popsSize , "Pops" );
}

/*
// should be one for each host in the network
bool Pops::readPopFiles ( ) {
	int32_t n = g_hostdb.getNumGroups();
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// note it
		log(LOG_INIT,"db: Reading %s/pops.%"INT32" of %"INT32".",
		    g_conf.m_dir,i,n);
		// 
	}
}

bool Pops::makeFinalPopFile ( char *coll ) {

	int32_t n = g_hostdb.getNumGroups();

	// tell each host to write his pop file to html directory
	Msg3e msg3e;
	for ( int32_t i = 0 ; i < n ; i++ ) 
		msg3e.sendRequest ( );

	// no more than 4096 groups supported for this now, but up later maybe
	char *buf [ 4096 ];

	// retrieve it from each host (msg3f = getFile)
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get over http
		g_httpServer.getDoc ( ... );
		// save to disk
		out[i].write ( content , contentLen );
	}

	// merge out file
	BigFile out;

	// then merge all of them out
	for ( int32_t i = 0 ; i < n ; i++ ) {
		
	}	

	// merge final

	// distribute final copy to all

	// clean up locals
}

// . make the pop file from indexdb
// . a bunch of wordhash/#docs pairs
// . word hash is lower 4 bytes of the termid
// . first int64_t in file is the # of docs
bool Pops::makeLocalPopFile ( char *coll ) {
	// get the rdbmap of the first indexdb file
	RdbBase *base = g_indexdb.getBase ( coll );
	//RdbMap  *map  = base->getMap(0);
	if ( ! base ) 
		return log("admin: Collection \"%s\" does not exist.",coll);
	BigFile *f    = base->getFile(0);
	// term must be in at least this many docs
	int32_t minDocs = 4000;
	// log it
	log(LOG_INFO,"admin: Making popularity file from %s for coll \"%s\".",
	    f->getFilename(),coll);
	log(LOG_INFO,"admin: Using cutoff of %"INT32" docs.",minDocs);

	// output the wordId/count pairs to this file
	BigFile out;
	char outFilename[256];
	sprintf(outFilename,"%s/popout.%"INT32"",g_conf.m_dir,g_hostdb.m_hostId);
	out.set ( outFilename );

	// store # of docs
	int64_t n = g_titledb.getGlobalNumDocs();
	out.write ( &n , 8 );

	// store key read from disk into here
	char tmp [ MAX_KEY_BYTES ];

	//
	//
	// this part is taken from main.cpp:dumpIndexdb()
	//
	//
	char buf [ 1000000 ];
	int32_t bufSize = 1000000;
	if ( ! f.open ( O_RDONLY ) ) return;
	// init our vars
	bool haveTop = false;
	char top[6];
	memset ( top , 0 , 6 );
	bool warned = false;
	// how big is this guy?
	int64_t filesize = f.getFileSize();
	// reset error number
	g_errno = 0;
	// the big read loop
 loop:
	int64_t readSize = bufSize;
	if ( off + readSize > filesize ) readSize = filesize - off;
	// return if we're done reading the whole file
	if ( readSize <= 0 ) return;
	// read in as much as we can
	f.read ( buf , readSize , off );
	// bail on read error
	if ( g_errno ) {
		log("admin: Read of %s failed.",f.getFilename());
		return;
	}
	char *p    = buf;
	char *pend = buf + readSize;
 inner:
	// parse out the keys
	int32_t size;
	if ( ((*p) & 0x02) == 0x00 ) size = ks;
	else                         size = ks-6;
	if ( p + size > pend ) {
		// skip what we read
		off  += readSize ;
		// back up so we don't split a key we should not
		off -= ( pend - p );
		// read more
		goto loop;
	}
	// new top?
	if ( size == ks ) { gbmemcpy ( top , p + (ks-6) , 6 ); haveTop = true; }
	// warning msg
	if ( ! haveTop && ! warned ) {
		warned = true;
		log("admin: Warning: first key is a half key.");
	}

	//
	// BUT i added this part to the main.cpp stuff
	//

	// was it the same as last key?
	if ( ks == 6 ) 
		count++;
	// ok, this starts a new key
	else {
		// did the previous key meet the min count requirement?
		if ( count >= minDocs ) {
			// if so, store the upper 4 bytes of the termid
			int32_t h;
			gbmemcpy ( &h , tmp+8 , 4 );
			// write it out
			out.write ( &h , 4 );
			// and the count
			out.write ( &count , 4 );
		}
		// reset, we got a new termid
		count = 1;
	}

	// 
	// end new stuff
	//


	// make the key
	gbmemcpy ( tmp , p , ks-6 );
	gbmemcpy ( tmp + ks-6 , top , 6 );
	// print the key
	//if ( ks == 12 )
	//	fprintf(stdout,"%08lli) %08"XINT32" %016"XINT64"\n",
	//		off + (p - buf) ,
	//		*(int32_t *)(tmp+8),*(int64_t *)tmp );
	//else
	//	fprintf(stdout,"%08lli) %016"XINT64" %016"XINT64"\n",
	//		off + (p - buf) ,
	//		*(int64_t *)(tmp+8),*(int64_t *)tmp );

	// go to next key
	p += size;
	// loop up
	goto inner;


	
}
*/


bool Pops::set ( Words *words , int32_t a , int32_t b ) {
	int32_t        nw        = words->getNumWords();
	int64_t  *wids      = words->getWordIds ();
	char      **wp        = words->m_words;
	int32_t       *wlen      = words->m_wordLens;

	// point to scores
	//int32_t *ss = NULL;
	//if ( scores ) ss = scores->m_scores;

	int32_t need = nw * 4;
	if ( need > POPS_BUF_SIZE ) m_pops = (int32_t *)mmalloc(need,"Pops");
	else                        m_pops = (int32_t *)m_localBuf;
	if ( ! m_pops ) return false;
	m_popsSize = need;

	for ( int32_t i = a ; i < b && i < nw ; i++ ) {
		// skip if not indexable
		if ( ! wids[i] ) { m_pops[i] = 0; continue; }
		// or if score <= 0
		//if ( ss && ss[i] <= 0 ) { m_pops[i] = 0; continue; }
		// it it a common word? like "and" "the"... see StopWords.cpp
		/*
		if ( isCommonWord ( (int32_t)wids[i] ) ) {
		max:
			m_pops[i] = MAX_POP; 
			continue; 
		}

		else if ( wlen[i] <= 1 && is_lower(wp[i][0]) ) 
			goto max;
		*/
		// once again for the 50th time partap's utf16 crap gets in 
		// the way... we have to have all kinds of different hashing
		// methods because of it...
		uint64_t key ; // = wids[i];
		key = hash64d(wp[i],wlen[i]);
		m_pops[i] = g_speller.getPhrasePopularity(wp[i], key,true);
		// sanity check
		if ( m_pops[i] < 0 ) { char *xx=NULL;*xx=0; }
		if ( m_pops[i] == 0 ) m_pops[i] = 1;
	}
	return true;
}

