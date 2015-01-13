#include "Wiki.h"

#include "Query.h"
#include "Words.h"
#include "Titledb.h"
#include "Threads.h"

static void doneReadingWikiWrapper ( void *state ) ;

// the global instance
Wiki g_wiki;

Wiki::Wiki () {
	m_callback = NULL;
	m_state    = NULL;
	m_opened   = false;
	// use a 4 byte key size and 1 byte data size
	m_ht.set ( 4 , 1,0,NULL,0,false,0 ,"tbl-wiki"); 
}

void Wiki::reset() {
	m_ht.reset();
}

Wiki::~Wiki () {
	if ( m_opened ) m_f.close();
}

// . load from disk
// . wikititles.txt (loads wikititles.dat if and date is newer)
bool Wiki::load() {

	// load it from .dat file if exists and is newer
	char ff1[256];
	char ff2[256];
	sprintf(ff1, "%swikititles.txt.part2", g_hostdb.m_dir);
	sprintf(ff2, "%swikititles2.dat", g_hostdb.m_dir);
	int fd1 = open ( ff1 , O_RDONLY );
	if ( fd1 < 0 ) log(LOG_INFO,"wiki: open %s: %s",ff1,mstrerror(errno));
	int fd2 = open ( ff2 , O_RDONLY );
	if ( fd2 < 0 ) log(LOG_INFO,"wiki: open %s: %s",ff2,mstrerror(errno));
	struct stat stats1;
	struct stat stats2;
	int32_t errno1 = 0;
	int32_t errno2 = 0;
	if ( fstat ( fd1 , &stats1 ) == -1 ) errno1 = errno;
	if ( fstat ( fd2 , &stats2 ) == -1 ) errno2 = errno;
	// close all
	close ( fd1 );
	close ( fd2 );
	// save text size for getRandomPhrase() function below
	m_txtSize = stats1.st_size;
	// just use the .dat if we got it
	if ( ! errno2 ) {
		log(LOG_INFO,"wiki: Loading %s",ff2);
		// "dir" is NULL since already included in ff2
		return m_ht.load ( NULL , ff2 );
	}
	// if we got a newer binary version, use that
	// add in 10 seconds i guess
	if ( ! errno2 && ! errno1 && stats2.st_mtime +10> stats1.st_mtime ) {
		log(LOG_INFO,"wiki: Loading %s",ff2);
		// "dir" is NULL since already included in ff2
		return m_ht.load ( NULL , ff2 );
	}
	// if no text file that is bad
	if ( errno1 ) { 
		g_errno = errno1 ; 
		return log ("gb: could not open %s for reading: %s",ff1,
			    mstrerror(g_errno));
	}
	// get the size of it
	int32_t size = stats1.st_size;
	// now we have to load the text file
	return loadText( size );
}

bool Wiki::loadText ( int32_t fileSize ) {

	log(LOG_INFO,"wiki: generating wikititles2.dat file");

	SafeBuf sb;
	char ff1[256];
	sprintf(ff1, "%swikititles.txt.part1", g_hostdb.m_dir);
	log(LOG_INFO,"wiki: Loading %s",ff1);
	if ( ! sb.fillFromFile(ff1) ) return false;

	char ff2[256];
	sprintf(ff2, "%swikititles.txt.part2", g_hostdb.m_dir);
	log(LOG_INFO,"wiki: Loading %s",ff2);
	if ( ! sb.catFile(ff2) ) return false;
	

        //int fd1 = open ( ff1 , O_RDONLY );
	// read in whole thing
	//char *buf = (char *)mmalloc ( size + 1 , "wiki" );
	//if ( ! buf ) return false;
	//int32_t n = read ( fd1 , buf , size );
	//close ( fd1 );
	//if ( n != size ) { g_errno = EBADENGINEER; return false; }
	// null terminate
	//buf[size] = '\0';
	sb.pushChar('\0');
	// should not have reallocated too much
	if ( sb.length() + 100 < sb.m_capacity ) { char *xx=NULL;*xx=0; }

	char *buf = sb.getBufStart();
	int32_t size = sb.length() - 1;

	// scan each line
	char *p    = buf;
	char *pend = buf + size;
	char *eol  = NULL;
	for ( ; p < pend ; p = eol + 1 ) {
		// skip spaces
		while ( p < pend && is_wspace_a ( *p ) ) p++;
		// find end of line, "eol" (also treat '(' as \n now)
		//for(eol = p; eol < pend && *eol !='\n' && *eol!='('; eol++) ;
		// do not use '(' since too many non-phraes in ()'s (for love)
		for (eol = p; eol < pend && *eol !='\n' ; eol++) ;
		// parse into words
		Words w;
		if ( ! w.set ( p            , // s
			       eol - p      , // slen
			       TITLEREC_CURRENT_VERSION ,
			       true         , // computeIds?
			       MAX_NICENESS ) ) 
			return false;
		int32_t nw = w.getNumWords();

		// skip if it begins with 'the', like 'the uk' because it
		// is causing uk to get a low score in 'boots in the uk'.
		// same for all stop words i guess...
		int32_t start = 0;

		//if ( nw >= 2 && w.m_wordIds[0] == 3522767639246570644LL &&
		//     w.m_wordIds[1] == -943426581783550057LL )
		//	log("poo"); // hashfast32 = 2117103295

		// this was letting "To_bell_the_cat" through as
		// "bell_the_cat" and messing up "To_be_or_not_to_be"
		/*
		for (  ; start < nw ; start++ ) {
			// skip punct stuff
			if ( w.getWordId(start) == 0LL ) continue;
			// skip query stop words
			// why? messes up "to be or not to be"
			if ( w.isQueryStopWord(start) ) continue;
			// ok, we got a good starter word
			break;
		}
		*/

		// if no words, bail
		if ( start >= nw ) continue;
		// remove last words if not alnum
		if ( nw > 0 && !w.isAlnum(nw-1) ) nw--;
		// if no words, bail
		if ( start >= nw ) continue;
		// skip this line if no words
		if ( nw <= 0 ) continue;
		// if begins with upper/lower and ends with lower/upper, skip
		// don't skip because i still want it for gigabits for
		// "point-in-time" "real-time" etc. common phrases.
		//char flag = 0;
		//if ( w.isCapitalized(start) && ! w.isCapitalized(nw-1) &&
		//     // fix "To_be_or_not_to_be"
		//     ! w.isStopWord(nw-1) ) 
		//	flag = 1;
		//if ( ! w.isCapitalized(start) && w.isCapitalized(nw-1) ) 
		//	flag = 1;
		// skip if it has ('s in it
		char c = *eol;
		*eol = '\0';
		char *pp = NULL;
		if ( !pp ) pp = strstr ( p,"[" );
		if ( !pp && strncasecmp( p,"List of ",8)==0) pp = p;
		if ( !pp ) pp = strstr ( p,"," );
		// show it for debug
		//if ( ! pp ) printf("%s\n",p);
		*eol = c;
		if ( pp ) continue;
		// get these
		int64_t *wids = w.getWordIds();
		// reset hash
		uint32_t h = 0;
		// count the words in the phrase
		int32_t count = 0;
		// hash the word ids together
		for ( int32_t i = start ; i < nw ; i++ ) {
			// skip if not a proper word
			if ( ! w.isAlnum(i) ) continue;
			// add into hash quickly
			h = hash32Fast ( wids[i] & 0xffffffff , h );
			// count them
			count++;
		}
		// skip if too big
		if ( count > 250 ) continue;
		// store into hash table
	        // make negative i guess to indicate it is not
		// the best title form
		//if ( flag ) count = count * -1;
		if ( ! m_ht.addKey ( &h , &count ) ) return false;
	}

	/*
	// reset for 2nd scan
	p = buf;
	//
	//
	// scan a second time and allow the mixed case guys like "Lock_pick"
	// if Lockpick exists and is a word
	//
	//
	for ( ; p < pend ; p = eol + 1 ) {
		// skip spaces
		while ( p < pend && is_wspace_a ( *p ) ) p++;
		// find end of line, "eol" (also treat '(' as \n now)
		//for(eol = p; eol < pend && *eol !='\n' && *eol!='('; eol++) ;
		// do not use '(' since too many non-phraes in ()'s (for love)
		for (eol = p; eol < pend && *eol !='\n' ; eol++) ;
		// parse into words
		Words w;
		if ( ! w.set ( p            , // s
			       eol - p      , // slen
			       TITLEREC_CURRENT_VERSION ,
			       true         , // computeIds?
			       MAX_NICENESS ) ) 
			return false;
		int32_t nw = w.getNumWords();

		// skip if it begins with 'the', like 'the uk' because it
		// is causing uk to get a low score in 'boots in the uk'.
		// same for all stop words i guess...
		int32_t start = 0;

		//if ( nw >= 2 && w.m_wordIds[0] == 3522767639246570644LL &&
		//     w.m_wordIds[1] == -943426581783550057LL )
		//	log("poo"); // hashfast32 = 2117103295

		for (  ; start < nw ; start++ ) {
			// skip punct stuff
			if ( w.getWordId(start) == 0LL ) continue;
			// skip query stop words
			if ( w.isQueryStopWord(start) ) continue;
			// ok, we got a good starter word
			break;
		}
		// if no words, bail
		if ( start >= nw ) continue;
		// need something like "Lock_pick"
		if ( nw != 3 ) continue;
		// remove last words if not alnum
		if ( nw > 0 && !w.isAlnum(nw-1) ) nw--;
		// if no words, bail
		if ( start >= nw ) continue;
		// skip this line if no words
		if ( nw <= 0 ) continue;
		// if not mixed, skip for 2nd scan
		if ( w.isCapitalized(start) && w.isCapitalized(nw-1) ) continue;
		if ( ! w.isCapitalized(start) && ! w.isCapitalized(nw-1)) continue;

		char **wptrs = w.getWords();
		int32_t  *wlens = w.getWordLens();
		uint64_t h64 = 0;
		int32_t conti = 0;
		int32_t count = 0;
		// hash the word ids together
		for ( int32_t i = start ; i < nw ; i++ ) {
			// skip if not a proper word
			if ( ! w.isAlnum(i) ) continue;
			// no digits starting "08-Hillary" "08Hillary"
			if ( i == start && !w.isAlpha(i) ) continue;
			// add into hash quickly
			h64 = hash64Lower_utf8_cont(wptrs[i], 
						    wlens[i],
						    h64,
						    &conti );
			count++;
		}
		// 2 words
		if ( count != 2 ) continue;
		// skip if too big
		if ( count > 250 ) continue;
		// conert
		uint32_t h32 = h64 & 0xffffffff;
		// the compound form must be in the table from the first scan,
		// if not, skip it
		if ( ! m_ht.isInTable ( &h32 ) ) continue;

		uint32_t hf32 = 0;
		count = 0;
		int64_t *wids = w.getWordIds();
		// hash the word ids together to make a new hash that takes the
		// space into account.
		for ( int32_t i = start ; i < nw ; i++ ) {
			// skip if not a proper word
			if ( ! w.isAlnum(i) ) continue;
			// add into hash quickly
			hf32 = hash32Fast ( wids[i] & 0xffffffff , hf32 );
			// count them
			count++;
		}

		// ok, store it
		if ( ! m_ht.addKey ( &hf32 , &count ) ) return false;
		// for debug
		//char c = *eol;
		// *eol = '\0';
		//log("wiki: %s",wptrs[0]);
		// *eol = c;
	}
	*/



	// do not save if we can't
	if ( g_conf.m_readOnlyMode ) return true;
	// now save this hash table for quicker loading next time
	//char ff2[256];
	//sprintf(ff2, "%s/wikititles2.dat", g_hostdb.m_dir);
	if ( ! m_ht.save ( g_hostdb.m_dir , "wikititles2.dat" ) ) return false;

	log(LOG_INFO,"wiki: done generating wikititles2.dat file");

	// success
	return true;
}

// if a phrase in a query is in a wikipedia title, then increase
// its affWeights beyond the normal 1.0
int32_t Wiki::getNumWordsInWikiPhrase ( int32_t i , Words *w ) {
	int64_t *wids = w->m_wordIds;
	if ( ! wids[i] ) return 0;
	int32_t nw = w->m_numWords;
	char **wptrs = w->getWords();
	int32_t  *wlens = w->getWordLens();
	// how many in the phrase
	int32_t max = -1;
	int32_t maxCount = 0;
	// accumulate a hash of the word ids
	//int64_t h      = 0LL;
	uint32_t h = 0;
	int32_t      wcount = 0;
	// otherwise, increase affinity high for included words
	for ( int32_t j = i ; j < nw && j < i + 12 ; j++ ) {
		// count all words
		wcount++;
		// skip if not alnum
		if ( ! wids[j] ) continue;
		// add to hash
		//h = hash64 ( wids[j] , h );
		// add into hash quickly
		h = hash32Fast ( wids[j] & 0xffffffff , h );
		// skip single words, we only want to check phrases
		if ( j == i ) continue;
		// look in table
		char *vp = (char *)m_ht.getValue ( &h );
		// skip if nothing
		if ( ! vp ) {
			// try combining. FIX FOR "Lock_pick". we want that to
			// be a wikipedia phrase, but it's not recorded because
			// its case is mixed.
			if ( j != i + 2 ) continue;
			// fix for "Make a" being a phrase because "Makea"
			// is in the wikipedia. fix for
			// 'how to make a lock pick set'
			if ( wlens[i+2] <= 2 ) continue;
			// special hash
			uint64_t h64 = 0;
			int32_t conti = 0;
			// add into hash quickly
			h64 = hash64Lower_utf8_cont(wptrs[i], 
						    wlens[i],
						    h64,
						    &conti );
			h64 = hash64Lower_utf8_cont(wptrs[i+2], 
						    wlens[i+2],
						    h64,
						    &conti );
			// try looking that up
			uint32_t hf32 = h64 & 0xffffffff;
			vp = (char *)m_ht.getValue(&hf32);
		}
		if ( ! vp ) continue;
		// we got a match
		max = j;
		maxCount = wcount;
	}
	// return now if we got one
	if ( maxCount > 0 ) return maxCount;
	// otherwise, try combining so "lock pick" is a wikipedia phrase because
	// "lockpick" is a wikipedia title.


	return maxCount;
}

/*
// if a phrase in a query is in a wikipedia title, then increase
// its affWeights beyond the normal 1.0
bool Wiki::setPhraseAffinityWeights ( Query *q , float *affWeights ,
				      bool *oneTitle ) {

	// reset all terms wiki affinities to 1.0
	for ( int32_t i = 0 ; i < q->m_numTerms ; i++ )
		q->m_qterms[i].m_wikiAff = 1.0;

	int32_t loopCount = 0;
	int32_t lasti;
	// loop over query words
	QueryWord *qw = q->m_qwords;
	int32_t       nw = q->m_numWords;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not alnum
		if ( qw[i].m_rawWordId == 0LL ) continue;
		// loop count
		loopCount++;
		// no longer one title?
		if ( loopCount > 1 && oneTitle && i >lasti ) *oneTitle = false;
		// count it
		int32_t count = 0;
		// how many in the phrase
		int32_t max = -1;
		// accumulate a hash of the word ids
		int64_t h = 0LL;
		// otherwise, increase affinity high for included words
		for ( int32_t j = i ; j < nw && count < 12 ; j++ ) {
			// skip if not alnum
			if ( qw[j].m_rawWordId == 0LL ) continue;
			// add to hash
			h = hash64 ( qw[j].m_rawWordId , h );
			// count it
			count++;
			// skip single words, we only want to check phrases
			if ( count == 1 ) continue;
			// look in table
			char *vp = (char *)m_ht.getValue ( &h );
			// skip if nothing
			if ( ! vp ) continue;
			// we got a match
			max = j;
		}
		// set all up to max to affinity of 10.0
		for ( int32_t j = i ; j <= max ; j++ ) {
			// skip if not alnum
			if ( qw[j].m_rawWordId == 0LL ) continue;
			// get term
			QueryTerm *qtp = qw[j].m_queryPhraseTerm;
			QueryTerm *qtw = qw[j].m_queryWordTerm;
			// store affinity, say 10x normal and 1/10th normal
			if ( qtp ) qtp->m_wikiAff = 1.00;
			if ( qtw ) qtw->m_wikiAff = 0.05;
		}
		// if that was our firstTime and max is maxed, we got a
		// single wikipedia title
		if ( loopCount == 1 && oneTitle ) {
			*oneTitle = true;
			lasti     = i + max;
		}
	}

	// store into array
	for ( int32_t i = 0 ; i < q->m_numTerms ; i++ )
		affWeights[i] = q->m_qterms[i].m_wikiAff;

	return true;
}
*/

// . we hit google with random queries to see what blog sites and
//   news sites they have
// . returns false if blocks, true otherwise
// . returns true and sets g_errno on error
bool Wiki::getRandomPhrase ( void *state , void (*callback)(void *state)) {
	// sanity check
	if ( this != &g_wiki ) { char *xx=NULL;*xx=0; }
	// this will be set on error
	m_errno = 0;
	// just get it from the file...
	int32_t r = rand() % m_txtSize;
	// read in 5k
	r -= 5000;
	// prevent negatives
	if ( r < 0 ) r = 0;
	// open it
	if ( ! m_opened ) {
		// return NULL on error, g_errno should be set
		if ( ! m_f.set ( g_hostdb.m_dir , "wikititles.txt" ) ) 
			return true;
		if ( ! m_f.open ( O_NONBLOCK , // flags
				  NULL       , // pc
				  -1         , // maxFileSize
				  0          ))// permissions
			return true;
		m_opened = true;
	}
	// someone is already outstanding!
	if ( m_callback ) { char *xx=NULL;*xx=0; }
	// save it
	m_state    = state;
	m_callback = callback;
	// . read it from there
	// . returns false if blocked
	if ( ! m_f.read (m_buf,5000,r,&m_fs,NULL,doneReadingWikiWrapper ) ) 
		return false;
	// . ok it did not block
	// . this will set Wiki::m_randPhrase[]
	doneReadingWiki ( );
	// no need to call callback, we did not block
	return true;
}

void doneReadingWikiWrapper ( void *state ) {
	// set m_randPhrase[] buf
	g_wiki.doneReadingWiki ( );
	// call callback
	g_wiki.m_callback ( g_wiki.m_state );
	// NULLify it to "free it up"
	g_wiki.m_state    = NULL;
	g_wiki.m_callback = NULL;
}

void Wiki::doneReadingWiki ( ) {
	// error?
	if ( g_errno ) {
		m_errno = g_errno;
		log("wiki: had error getting rand phrase: %s",
		    mstrerror(g_errno));
		return;
	}
	// find first \n in buf
	char *p    = m_buf;
	char *pend = m_buf + 5000;
	while ( p < pend && *p != '\n' ) p++;
	// skip that '\n'
	p++;
	// strange!!
	if ( p >= pend ) { 
		m_errno = EBADENGINEER;
		log("wiki: bad random read."); 
		return; 
	}

	//
	// parse like in Wiki::load() now
	//
	char *eol = NULL;
	for ( ; p < pend ; p = eol + 1 ) {
		// skip spaces
		while ( p < pend && is_wspace_a ( *p ) ) p++;
		// find end of line, "eol" (also treat '(' as \n now)
		//for(eol = p; eol < pend && *eol !='\n' && *eol!='('; eol++) ;
		for ( eol = p; eol < pend && *eol !='\n' ; eol++) ;
		// parse into words
		Words w;
		if ( ! w.set ( p            , // s
			       eol - p      , // slen
			       TITLEREC_CURRENT_VERSION ,
			       true         , // computeIds?
			       MAX_NICENESS ) ) {
			m_errno = g_errno;
			return;
		}
		int32_t nw = w.getNumWords();
		// remove last words if not alnum
		if ( nw > 0 && !w.isAlnum(nw-1) ) nw--;
		// skip this line if no words
		if ( nw <= 0 ) continue;
		// if begins with upper/lower and ends with lower/upper, skip
		if ( w.isCapitalized(0) && ! w.isCapitalized(nw-1) ) 
			continue;
		if ( ! w.isCapitalized(0) && w.isCapitalized(nw-1) ) 
			continue;
		// skip if it has ('s in it
		char c = *eol;
		*eol = '\0';
		char *pp = NULL;
		if ( !pp ) pp = strstr ( p,"(" );
		if ( !pp ) pp = strstr ( p,"[" );
		if ( !pp && strncasecmp( p,"List of ",8)==0) pp = p;
		if ( !pp ) pp = strstr ( p,"," );
		*eol = c;
		if ( pp ) continue;
		//
		// BUT here we skip if too long!
		// we don't want to send insanely int32_t queries to goog/yhoo
		//
		int32_t size =  w.getWord(nw-1) - w.getWord(0);
		size      += w.getWordLen(nw-1);
		if ( size >= 128 ) continue;
		// get ptr to it
		char *wptr = w.getWord ( 0 );
		// copy that
		gbmemcpy ( m_randPhrase , wptr , size );
		// NULL terminate
		m_randPhrase[size] = '\0';
		// all done
		return;
	}
}
