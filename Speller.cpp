#include "gb-include.h"

#include "Mem.h"
#include "Conf.h"
#include "Dns.h"
#include "HttpServer.h"
#include "Loop.h"
#include <sys/resource.h>  // setrlimit

#include "Speller.h"
#include <stdio.h>
#include <ctype.h>

/*
static void handleRequestSpeller ( UdpSlot *slot , int32_t netnice );

static void gotSpellerReplyWrapper (void *state, void *state2);

bool Speller::registerHandler ( ) {
	// . register ourselves with the udp server
	// . it calls our callback when it receives a msg of type 0x39
	if ( ! g_udpServer.registerHandler ( 0x3d, handleRequestSpeller )) 
		return false;
	return true;
}

// . handle a request to get a linkInfo for a given docId/url/collection
// . returns false if slot should be nuked and no reply sent
// . sometimes sets g_errno on error
void handleRequestSpeller ( UdpSlot *slot , int32_t netnice ) {
	// The request is the string to be spellchecked, null ended
	char *request = slot->m_readBuf;

	// first tells us if we should narrow the search stuff
	bool narrowP = *(bool *) request;
	request += sizeof(bool);

	// is it found in dict or pop words
	bool found;
	int32_t score;
	char reco[MAX_PHRASE_LEN];
	int32_t pop;
	int64_t start = gettimeofdayInMilliseconds();
	bool recommendation = g_speller.m_language[langEnglish].
		getRecommendation( request, gbstrlen(request), 
				   reco, MAX_PHRASE_LEN, 
				   &found, &score,
				   &pop );

	log ( LOG_DEBUG,"speller: %s --> %s", request, reco );

	int32_t numNarrow = 0;
	char narrow[MAX_NARROW_SEARCHES * MAX_PHRASE_LEN];
	int32_t narrowPops[MAX_NARROW_SEARCHES];
	//if ( narrowP )
	//	numNarrow = g_speller.m_language[langEnglish].
	//		narrowPhrase ( request, narrow, narrowPops,
	//			       MAX_NARROW_SEARCHES );
	
	// calculate total reply size
	// int32_t replySize = found + recommendation + score + pop + reco
	int32_t replySize = sizeof(bool) + sizeof(bool) + 4 + 4 + 
		gbstrlen(reco) + 1;

	if ( narrowP ){
		replySize += 4; // numPhrases 
		for ( int32_t i = 0; i < numNarrow; i++ )
			replySize += 4 + gbstrlen(&narrow[i*MAX_FRAG_SIZE]) + 1;
	}

	char *reply = (char*) mmalloc(replySize, "SpellerReplyBuf");
	if ( !reply ) {
		g_errno = ENOMEM;
		//g_udpServer.sendReply_ass( NULL, 0, NULL, 0, slot );
		g_udpServer.sendErrorReply( slot , g_errno );
		return;
	}
	char *p = reply;

	*(bool *)p = found;
	p += sizeof(bool);
	
	*(bool *)p = recommendation;
	p += sizeof(bool);

	// store the score and pop
	*(int32_t *) p = score; p += 4;
	*(int32_t *) p = pop; p += 4;

	// store the recommendation
	strcpy( p, reco );
	p += gbstrlen(reco) + 1;
	if ( narrowP ){
		// store the number of narrow phrases found
		*(int32_t *) p = numNarrow;
		p += 4;
		for ( int32_t i = 0; i < numNarrow; i++ ){
			*(int32_t *)p = narrowPops[i];
			p += 4;
			strcpy(p, &narrow[i * MAX_FRAG_SIZE]);
			p += gbstrlen(&narrow[i * MAX_FRAG_SIZE]) + 1;
		}
	}

	//sanity check
	if ( p - reply != replySize ){
		char *xx = NULL; *xx = 0;
	}

	int64_t end = gettimeofdayInMilliseconds();
	if ( end - start > 1 )
		log (LOG_INFO,"speller: took %"INT64" ms to spellcheck "
		     "fragment %s", end-  start, request);
	g_udpServer.sendReply_ass ( reply   ,
				    replySize, 
				    reply   , 
				    replySize,
				    slot    );
}
*/

Speller g_speller;

Speller::Speller(){
	//m_unifiedBuf = NULL;
	//mm_unifiedBufSize = 0;
}

Speller::~Speller(){
	reset();
}
char *g_str=NULL;
bool Speller::init(){

	static bool s_init = false;
	if ( s_init ) return true;
	s_init = true;

	/*
	m_hostsPerSplit = g_hostdb.m_numHosts / g_hostdb.m_indexSplits;
	m_hostsPerSplit /= g_hostdb.m_numHostsPerShard;
	if ( m_hostsPerSplit <= 0 )
		return log("db: the <indexSplit> in gb.conf is probably not "
			   "too big. Are you using the wrong hosts.conf?");
	// check if we've got enough multicasts avaiable
	if ( m_hostsPerSplit > MAX_UNIQUE_HOSTS_PER_SPLIT ){
		log( LOG_WARN,"speller: not enough multicasts available for "
		     "this host configuration. Increase multicasts" );
		return false;
	}
	*/

	if ( !loadUnifiedDict() )
		return log("spell: Could not load unified dict from "
			   "unifiedDict-buf.txt and unifiedDict-map.dat");

	// this seems to slow our startup way down!!!
	log("speller: turning off spell checking for now");
	return true;

	/*
	int32_t myHash = g_hostdb.m_hostId % 
		( m_hostsPerSplit * g_hostdb.m_indexSplits );
	myHash /= g_hostdb.m_indexSplits;

	//for ( int32_t i = 0; i < MAX_LANGUAGES; i++ )
	m_language[langEnglish].init ( m_unifiedBuf.getBufStart(), 
				       m_unifiedBuf.length(),
				       langEnglish, 
				       m_hostsPerSplit,
				       myHash );

	return true;
	*/
}

void Speller::reset(){
	//if ( m_unifiedBuf && m_unifiedBufSize > 0 )
	//	mfree ( m_unifiedBuf, m_unifiedBufSize, "SpellerBuf" );
	m_unifiedBuf.purge();
	
	m_unifiedDict.reset();
	/*
	for(int32_t i = 0; i < MAX_LANGUAGES; i++) 
		m_language[i].reset();
	*/

	//m_unifiedBuf = NULL;
	//m_unifiedBufSize = 0;
}

// test it.
void Speller::test ( char *ff ) {
	//char *ff = "/tmp/sctest";
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd ) {
		log("speller: test: Could not open %s for "
		    "reading: %s.", ff,strerror(errno));
		return;
	}

	char buf[1026];
	//char dst[1026];
	// go through the words in dict/words
	while ( fgets ( buf , MAX_FRAG_SIZE , fd ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		Query q;
		q.set2 ( buf , langUnknown , false );

		//if ( getRecommendation ( &q, dst , 1024 ) )
		//  log(LOG_INIT,"speller: %s-->%s",buf,dst);
		//  else
		// log(LOG_INIT,"speller: %s",buf);
	}
	fclose(fd);
}

/*
///////////////////////////////////////////////////////
// RECOMMENDATION ROUTINES BELOW HERE
//
// These will spellcheck and give recommendations
///////////////////////////////////////////////////////

bool Speller::canStart( QueryWord *qw ) {
	// can only start with a alpha character, no numeric
	if ( ! is_alnum_utf8 ( qw->m_word+0 ) ) return false;

	if ( qw->m_ignoreWord &&
	     qw->m_ignoreWord != IGNORE_CONNECTED &&
	     qw->m_ignoreWord != IGNORE_QUOTED ) return false;

	// don't check 'rom' in phrase "cd-rom", or 't' in "ain't"
	if ( qw->m_leftConnected ) 
		return false;

	// don't start with a stop word
	if ( qw->m_isStopWord )
		return false;
	
	// a lot of field terms should not be spell checked
	if ( qw->m_fieldCode ) {
		if ( qw->m_fieldCode != FIELD_TITLE   &&
		     qw->m_fieldCode != FIELD_CITY    &&
		     qw->m_fieldCode != FIELD_AUTHOR  &&
		     qw->m_fieldCode != FIELD_COUNTRY   )
			return false;
	}
	return true;
}


// . returns false if blocked
//   recommended something different than original query, "q"
//   and false otherwise
// . also returns false and sets g_errno on error
// . stores recommended query in "dst" and NULL terminates it
// . if dst is too small it will bitch and return true with g_errno set
bool Speller::getRecommendation ( Query *q, 
				  bool   spellcheck,
				  char  *dst, // recommendation destination
				  int32_t   dstLen, // recommendation max len
				  bool   narrowSearch,
				  char  *narrow, // narrow search
				  int32_t   narrowLen,  // narrow search len
				  int32_t  *numNarrows, // num narrows found
				  void  *state, 
				  void (*callback)(void *state) ){
	*dst = '\0';
	*narrow = '\0';
	// no narrowing search if spellchecking is off
	if ( !spellcheck )
		return true;

	// don't spellcheck queries that are more than MAX_FRAG_SIZE int32_t.
	if ( q->getQueryLen() >= MAX_FRAG_SIZE )
		return true;

	StateSpeller *st ;
	try { st = new (StateSpeller); }
	catch ( ... ) { 
		g_errno = ENOMEM;
		log("Speller: new(%i): %s", sizeof(StateSpeller),
		    mstrerror(g_errno));
		return true; 
	}
	mnew ( st , sizeof(StateSpeller) , "State00" );
       
	st->m_state = state;
	st->m_callback = callback;
	st->m_q = q;
	st->m_spellcheck = spellcheck;
	st->m_dst = dst;
	st->m_dend = dst + dstLen;
	st->m_narrowSearch = narrowSearch;
	st->m_nrw = narrow;
	st->m_nend = narrow + narrowLen;
	st->m_numNarrow = numNarrows;
	*st->m_numNarrow = 0;
	st->m_start = gettimeofdayInMilliseconds();
	st->m_numFrags = 0;
	st->m_numFragsReceived = 0;
	
	// . break query down into fragments
	// . each fragment is a string of words
	// . quotes and field names will separate fragments
	// . TODO: make field data in its own fragment
	int32_t nqw = q->m_numWords;

	for ( int32_t i = 0 ; i < nqw ; i++ ) {
		// get a word in the Query to start a fragment with
		QueryWord *qw = &q->m_qwords[i];
		// can he start the phrase?
		if ( ! canStart( qw ) )
			continue;

		bool inQuotes  = qw->m_inQuotes;
		char fieldCode = qw->m_fieldCode;
		// . get longest continual fragment that starts with word #i
		// . get the following words that can be in a fragment
		//   that starts with word #i
		// . start of the frag
		int32_t  endQword = i;
		int32_t  startQword = i;
		for ( ; i < nqw ; i++ ) {
			// . skip if we should
			// . keep punct, however
			QueryWord *qw1 = &q->m_qwords[i];
			if ( qw1->m_opcode                 ) break;
			if ( qw1->m_inQuotes  != inQuotes  ) break;
			if ( qw1->m_fieldCode != fieldCode ) break;
			if ( qw1->m_ignoreWord == IGNORE_FIELDNAME ) break;
			if ( qw1->m_phraseSign && 
			     !qw1->m_rightConnected ) break;
			// are we punct?
			if ( ! is_alnum_utf8(qw1->m_word) ) 
				endQword = i - 1;
			else    
				endQword = i;
		}
		// revisit this i in big loop since we did not include it
		i = endQword;

		//create a new stateFrag
		StateFrag *stFrag;
		try { stFrag = new (StateFrag); }
		catch ( ... ) { 
			mdelete ( st, sizeof(StateSpeller),  "StateSpeller" );
			delete (st);
			g_errno = ENOMEM;
			log("Speller: new(%i): %s", sizeof(StateFrag),
			    mstrerror(g_errno));
			//continue;
			return true;
		}
		mnew ( stFrag, sizeof(StateFrag),
		       "StateFrag" );

		stFrag->m_state = (void*) st;
		stFrag->m_narrowPhrase = st->m_narrowSearch;
		stFrag->m_q = q;
		stFrag->m_startQword = startQword;
		stFrag->m_endQword = endQword;
		stFrag->m_errno = 0;
		st->m_stFrag[st->m_numFrags] = stFrag;
		st->m_numFrags++;
		// blocked
		if ( !getRecommendation( stFrag ) ){
			continue;
		}
		st->m_numFragsReceived++;
	}
	// if outstanding frags
	if ( st->m_numFragsReceived < st->m_numFrags )
		return false;
	gotFrags(st);
	// delete state
	mdelete ( st, sizeof(StateSpeller),  "StateSpeller" );
	delete (st);
	return true;
}

bool Speller::getRecommendation ( StateFrag *st ){
	st->m_recommended = false;
	st->m_numFound = 0;
	st->m_numNarrowPhrases = 0;
	char *dst = st->m_dst;
	
	// normalize this fragment and store in "dst"
	bool wasAlnum = true;
	for ( int32_t i = st->m_startQword; i <= st->m_endQword; i++ ){
		// start of each word
		st->m_wp[i] = dst;
		char *p = st->m_q->m_qwords[i].m_word;
		int32_t  plen = st->m_q->m_qwords[i].m_wordLen;
		for ( int32_t j = 0; dst-st->m_dst <MAX_FRAG_SIZE&&j<plen;j++ ) {
			if ( !getClean_utf8(p+j) ) 
				continue;
			// skip back to back punct/spaces
			if (j>0 && !is_alnum_utf8(p+j) &&!wasAlnum)
				continue;
			*dst = p[j];
			dst++;
			wasAlnum = is_alnum_utf8 ( p+j );
		}
		st->m_wplen[i] = dst - st->m_wp[i];
		st->m_isfound[i] = false;
	}
	*dst = '\0';
	
	// debug msg
	log(LOG_DEBUG,"speller: Getting recommendation for frag=%s",
	    st->m_dst);

	// give each word in the phrase a chance to start the subphrase
	int32_t maxPhrase = st->m_endQword - st->m_startQword;
	if ( maxPhrase > MAX_WORDS_PER_PHRASE )
		maxPhrase = MAX_WORDS_PER_PHRASE;

	// store the phraseLen and posn
	st->m_pLen = maxPhrase;
	st->m_pPosn = st->m_startQword;
	
	return launchReco(st);
}

bool Speller::launchReco(StateFrag *st){
	// if we checked all the phrases or found all the words
	if ( st->m_numFound == st->m_endQword - st->m_startQword + 1 || 
	     st->m_pLen < 0 ){
		return true;
	}

	bool launchPhrase = false;
 	for ( ; st->m_pLen >= 0; st->m_pLen-- ){
		for ( ; st->m_pPosn + st->m_pLen <= st->m_endQword; 
		      st->m_pPosn++ ) {
			// find a word that can start the phrase
			QueryWord *qw = &st->m_q->m_qwords[st->m_pPosn];
			if ( !canStart (qw) )
				continue;
			// don't do this phrase if we have found even one
			// word in the phrase
			bool found = false;
			for ( int32_t k = st->m_pPosn; 
			      k <= st->m_pPosn + st->m_pLen; k++ ) {
				if ( st->m_isfound[k] ){
					found = true;
					break;
				}
			}
			if ( found )
				continue;

			// cannot end on a stop word, punct, right-connected
			// word
			QueryWord *qwEnd = 
				&st->m_q->m_qwords[st->m_pPosn + st->m_pLen];
			if ( qwEnd->m_isStopWord || qwEnd->m_isPunct ||
			     qwEnd->m_rightConnected )
				continue;
			
			// found someone to start the phrase with
			// what is the new phrase parms?
			st->m_a = st->m_wp[st->m_pPosn];
			st->m_b = st->m_wp[st->m_pLen + st->m_pPosn]+
				st->m_wplen[st->m_pLen + st->m_pPosn];
			
			// also store the tmp char that we are changing
			st->m_c = *(st->m_b);
			*(st->m_b) = '\0';

			// if it is just a number, don't get recommendation
			// lest we emabarrass ourselves
			if ( st->m_pPosn == 0 && is_digit(st->m_a[0]) ) {
				char *k = st->m_a+1;
				while ( is_digit(*k) ) k++;
				if ( ! *k ) { 
					*st->m_b = st->m_c ; 
					continue;
				}
			}

			// if it is an adult phrase, don't get a recommendation
			// check if isAdult really finds a word.
			char *adultLoc = NULL;
			if ( isAdult(st->m_a, gbstrlen(st->m_a), &adultLoc) &&
			     ( adultLoc == st->m_a || *(adultLoc-1) == ' ' ) ){
				// mark as found
				for ( int32_t k = st->m_pPosn; 
				      k <= st->m_pPosn + st->m_pLen; k++ )
					st->m_isfound[k] = true;
				*(st->m_b) = st->m_c;
				continue;
			}
			// if the phrase is in dict or in the top pop words,
			// phrase is found. Don't check if we are narrowing 
			// the phrase because we need to multicast anyways
			uint64_t h ;
			h = hash64d(st->m_a, gbstrlen(st->m_a) );
			if ( !st->m_narrowPhrase && 
			     getPhrasePopularity( st->m_a, h, false ) > 0 ){
				// mark as found
				for ( int32_t k = st->m_pPosn; 
				      k <= st->m_pPosn + st->m_pLen; k++ )
					st->m_isfound[k] = true;
				*(st->m_b) = st->m_c;
				continue;
			}
			launchPhrase = true;
			break;
		}
		if ( launchPhrase )
			break;
		st->m_pPosn = st->m_startQword;
	}

	if ( st->m_pLen < 0 ){
		return true;
	}

	// debug msg
	log(LOG_DEBUG,"speller: ----------");
	log(LOG_DEBUG,"speller: Checking phrase=%s", st->m_a);


	// launch for all the splits
	st->m_numRequests = 0;
	st->m_numReplies = 0;


	int32_t hostsPerSplit = g_hostdb.m_numHosts / g_hostdb.m_indexSplits;
	// don't send to twins...
	hostsPerSplit /= g_hostdb.m_numHostsPerShard;
	int32_t mySplit = g_hostdb.m_hostId % g_hostdb.m_indexSplits;

	int32_t key = st->m_q->getQueryHash();//0;
	int32_t timeout = 30;
	int32_t niceness = 0;
	char request[MAX_FRAG_SIZE + 4];
	char *p = request;
	*(bool *)p = st->m_narrowPhrase;
	p += sizeof(bool);
	strcpy ( p, st->m_a );
	// send the null end too
	p += gbstrlen(st->m_a)+1;
	int32_t plen = p - request;
	for ( int32_t i = 0; i < hostsPerSplit; i++ ){
		// get the hostId of the host we're sending to
		uint32_t hostId = 
			mySplit + ( i * g_hostdb.m_indexSplits );
		Host *h = g_hostdb.getHost(hostId);
		st->m_mcast[i].reset();

		bool status = st->m_mcast[i].
			send(request   ,
			     plen      , // request size
			     0x3d      , // msgType 0x3d
			     false     , // multicast owns m_request?
			     h->m_groupId, // group to send to (groupKey)
			     false     , // send to whole group?
			     key       , 
			     st        , // state data
			     NULL      , // state data
			     gotSpellerReplyWrapper ,
			     timeout      , // in seconds
			     niceness  ,
			     false     , // realtime?
			     -1        , // m_q->m_bestHandlingHostId ,
			     NULL      , // m_replyBuf   ,
			     0         , // MSG39REPLYSIZE,
			     // this is true if multicast should free
			     // the
			     // reply, otherwise caller is responsible
			     // for freeing it after calling
			     // getBestReply).
			     // actually, this should always be false,
			     // there
			     // is a bug in Multicast.cpp.
			     false        );

		if (!status){
			st->m_numReplies++;
			log("speller: Multicast had error: %s",
			    mstrerror(g_errno));
			st->m_errno = g_errno;
			continue;
		}
		// blocked
		else
			st->m_numRequests++;
	}

	if ( st->m_numReplies == st->m_numRequests )
		return true;
	return false;
}

void gotSpellerReplyWrapper( void *state, void *state2 ){
	StateFrag *stFrag = (StateFrag *) state;
	stFrag->m_numReplies++;
	if ( stFrag->m_numReplies < stFrag->m_numRequests )
		return;
	// blocked
	if ( !g_speller.gotSpellerReply(stFrag) )
		return;

	StateSpeller *st = (StateSpeller *)stFrag->m_state;
	// One more frag received
	st->m_numFragsReceived++;
	if ( st->m_numFragsReceived < st->m_numFrags )
		return;

	g_speller.gotFrags(st);
	// callback
	st->m_callback( st->m_state );
	// delete state
	mdelete ( st, sizeof(StateSpeller),  "StateSpeller" );
	delete (st);
}

bool Speller::gotSpellerReply( StateFrag *st ){
	int32_t minScore = LARGE_SCORE;
	int32_t maxPop = -1;
	char *bestReco = NULL;

	char *reply[MAX_UNIQUE_HOSTS_PER_SPLIT];
	int32_t  replySize[MAX_UNIQUE_HOSTS_PER_SPLIT];
	int32_t  replyMaxSize[MAX_UNIQUE_HOSTS_PER_SPLIT];
	bool  freeit;
	bool  found = false; //phrase was found in dict or pop words
	int32_t hostsPerSplit = g_hostdb.m_numHosts / g_hostdb.m_indexSplits;
	// don't send to twins...
	hostsPerSplit /= g_hostdb.m_numHostsPerShard;

	int32_t  numNarrowPhrases[MAX_UNIQUE_HOSTS_PER_SPLIT];
	char *narrowPtrs[MAX_UNIQUE_HOSTS_PER_SPLIT];

	// init narrowSearch arrays
	for ( int32_t i = 0; i < MAX_UNIQUE_HOSTS_PER_SPLIT; i++ ){
		numNarrowPhrases[i] = 0;
		narrowPtrs[i] = NULL;
	}

	for ( int32_t i = 0; i < hostsPerSplit; i++ ){
		reply[i] = st->m_mcast[i].getBestReply( &replySize[i] ,
							&replyMaxSize[i] ,
							&freeit );
		// multicast may have an empty reply buffer if there was an
		// OOM error or something. m_errno should have been set, but
		// we have to loop through all the multicasts to free the
		// reply buffers.
		char *p = reply[i];

		if ( g_errno || st->m_errno || !p){
			continue;
		}
		// was is found in dict
		bool foundInDict = *(bool *)p;
		p += sizeof(bool);
		if ( foundInDict )
			found = true;

		// first is if there is a recommendation or not
		bool recommendation = *(bool *) p;
		p += sizeof (bool);

		if ( !recommendation && !st->m_narrowPhrase )
			continue;

		int32_t score = *(int32_t *)p;
		p += 4;
		int32_t pop = *(int32_t *)p;
		p += 4;

		if ( recommendation ){
			log ( LOG_DEBUG,"speller: Received reco %s, "
			      "score=%"INT32", pop=%"INT32"", p, score, pop );

			// we have a recommendation with score and pop
			// choose the one with the lowest score, and if the
			// score is same then the max pop 
			// HACK: we are getting bad recommendations for smaller
			// popularities. So don't consider them
			if ( pop > 8 && ( score < minScore || 
				   ( score == minScore && pop > maxPop ) ) ){
				bestReco = p;
				minScore = score;
				maxPop = pop;
			}
		}

		p += gbstrlen(p) + 1;
		if ( st->m_narrowPhrase ){
			numNarrowPhrases[i] = *(int32_t *)p;
			p += 4;
			narrowPtrs[i] = p;
		}
	}
	
	// merge all the narrow results
	if ( st->m_narrowPhrase ){
		int32_t currPhrase[MAX_UNIQUE_HOSTS_PER_SPLIT];
		for ( int32_t i = 0; i < MAX_UNIQUE_HOSTS_PER_SPLIT; i++ )
			currPhrase[i] = 0;
		for ( int32_t i = 0; i < MAX_NARROW_SEARCHES; i++ ){
			int32_t maxHost = -1;
			int32_t maxPop = 0;
			for ( int32_t j = 0; j < hostsPerSplit; j++ ){
				if ( numNarrowPhrases[j] <= currPhrase[j] )
					continue;
				int32_t pop = *(int32_t *)narrowPtrs[j];
				if ( pop <= maxPop )
					continue;
				maxPop = pop;
				maxHost = j;
			}
			if ( maxHost < 0 )
				break;
			// 
			narrowPtrs[maxHost] += 4;
			strcpy( st->m_narrowPhrases[i], narrowPtrs[maxHost] );
			narrowPtrs[maxHost] +=gbstrlen(narrowPtrs[maxHost]) + 1;
			currPhrase[maxHost]++;
			st->m_numNarrowPhrases++;
		}
	}

	// make narrowPhrase false here, so that its not launched a second time
	// for the same frag;
	st->m_narrowPhrase = false;

	// revert
	*(st->m_b) = st->m_c;

	// if we found a recommendation,or if the phrase was found in the
	// dictionary or pop words then mark all the
	// words that fall under the phrase as found
	if ( found || bestReco ){
		for ( int32_t k = st->m_pPosn; 
		      k <= st->m_pLen + st->m_pPosn; k++ )
			st->m_isfound[k] = true;
		st->m_numFound += st->m_pLen + 1;
	}

	// if not found in the dictionary or a recommendation, copy the phrase
	if ( !found && bestReco){
		// this fragment is going to be recommended
		st->m_recommended = true;
		// insert our recommendation into the phrase to get a new one
		char *s1    = st->m_wp[st->m_startQword];
		int32_t  slen1 = st->m_a - st->m_wp[st->m_startQword];
		char *s2    = bestReco;
		int32_t  slen2 = gbstrlen(bestReco);
		char *s3    = st->m_b ;
		// store the difference in length between the reco and the 
		// original string
		int32_t  diff = slen2 - ( st->m_b - st->m_a );
		int32_t  slen3 = st->m_wp[st->m_endQword] + 
			st->m_wplen[st->m_endQword] - st->m_b;

		if ( slen3 < 0 )
			slen3 = 0;

		int32_t  tlen = slen1 + slen2 + slen3 ;
		if ( tlen > MAX_FRAG_SIZE ){
			log(LOG_LOGIC,"speller: buf too small. Fix me 3.");
			// blocked
			if ( !launchReco(st) )
				return false;
			return true;
		}
		// make substitution and store in "dst"
		char buf2 [ MAX_FRAG_SIZE];
		char *nf = buf2;
		gbmemcpy ( nf , s1 , slen1 ) ; nf += slen1;
		gbmemcpy ( nf , s2 , slen2 ) ; nf += slen2;
		gbmemcpy ( nf , s3 , slen3 ) ; 
		nf += slen3;
	
		// don't forget to NULL terminate
		*nf = '\0';
		// debug msg
		log( LOG_DEBUG,"speller: Trying substitution \"%s\"",
		     buf2 );

		strcpy ( st->m_dst , buf2 );

		// the pointers might have to be changed if the 
		// recommendation was not of the same length as the words
		if ( diff != 0 ){
			for ( int32_t k = st->m_pLen+st->m_pPosn+1; 
			      k <= st->m_endQword; k++ )
				st->m_wp[k] += diff;
		}
	}

	// don't forget to free the replies
	for ( int32_t i = 0; i < hostsPerSplit; i++ )
		if ( reply[i] && replyMaxSize[i] > 0 )
			mfree( reply[i], replyMaxSize[i], "SpellerReplyBuf" );
	
	// go to the next position in the phrase. if we have reached the end
	// of the phrase position, decrement the phrase length and start again
	if ( st->m_pPosn + st->m_pLen >= st->m_endQword - 1 ){
		st->m_pLen--;
		st->m_pPosn = st->m_startQword;
	}
	else
		st->m_pPosn++;

	if ( !launchReco(st) )
		return false;
	return true;
}
*/
// . break a NULL-terminated string down into a list of ptrs to the words
// . return the number of words stored into "wp"
/*
int32_t Speller::getWords ( const char *s ,
			 char *wp     [MAX_FRAG_SIZE] ,
			 int32_t  wplen  [MAX_FRAG_SIZE] ,
			 bool *isstop                   ) {
	int32_t nwp = 0;
 loop:
	// skip initial punct
	while ( *s && ! is_alnum ( *s ) ) s++;
	// bail if done
	if ( ! *s ) return nwp;
	// point to word
	wp [ nwp ] = (char *)s;
	// convenience ptr
	char *ww = (char *)s;
	// count over it
	while ( is_alnum ( *s ) ) s++;
	// how long is the word?
	int32_t slen = s - wp [ nwp ];
	// set length
	wplen [ nwp ] = slen ;
	// is it a stop word?
	if ( isstop ) {
		// TODO: make the stop words utf8!!!
		int64_t h = hash64Lower_utf8 ( ww , slen ) ;
		bool stop = ::isStopWord       ( ww , slen , h ) ;
		// BUT ok if Capitalized or number
		if ( stop ) {
			if ( is_digit (ww[0])    ) stop = false;
			if ( is_cap   (ww,slen ) ) stop = false;
			// e-mail, c file, c. s. lewis
			if ( slen  == 1 && ww[0] != 'a' ) stop = false;
		}
		isstop[nwp] = stop;
	}
	nwp++;
	goto loop;
}
*/
/*
void Speller::gotFrags( void *state ){
	StateSpeller *st = (StateSpeller *) state;
	
	char *dptr = st->m_dst;
	char *nptr = st->m_nrw;
	bool recommendation = false;
	Query *q = st->m_q;

	// . break query down into fragments
	// . each fragment is a string of words
	// . quotes and field names will separate fragments
	// . TODO: make field data in its own fragment
	int32_t nqw = q->m_numWords;
	int32_t currFrag = 0;
	for ( int32_t i = 0 ; i < nqw ; i++ ) {
		// get a word in the Query to start a fragment with
		QueryWord *qw = &q->m_qwords[i];
		// if he has a phraseSign, put it right away
		//if ( qw->m_phraseSign ) {
		// *dptr = qw->m_phraseSign;
		// dptr++;
		// }
		// can he start the phrase?
		// if he can't start our fragment, just copy over to "dst"
		if ( !canStart( qw )) {
			// copy to rp and get next word
			char *w    = qw->m_word;
			int32_t  wlen = qw->m_wordLen;
			if ( dptr + wlen >= st->m_dend ) { 
				g_errno = EBUFTOOSMALL; continue; }
			// watch out for LeFtP and RiGhP
			if      ( qw->m_opcode == OP_LEFTPAREN ) *dptr++ = '(';
			else if ( qw->m_opcode == OP_RIGHTPAREN) *dptr++ = ')';
			else if ( qw->m_opcode == OP_PIPE      ) *dptr++ = '|';
			else { 
				gbmemcpy ( dptr , w , wlen ); 
				dptr += wlen; 
			}
			*dptr = '\0';
			continue;
		}
		bool inQuotes  = qw->m_inQuotes;
		char fieldCode = qw->m_fieldCode;
		// . get longest continual fragment that starts with word #i
		// . get the following words that can be in a fragment
		//   that starts with word #i
		// . start of the frag
		int32_t  endQword = i;
		for ( ; i < nqw ; i++ ) {
			// . skip if we should
			// . keep punct, however
			QueryWord *qw1 = &q->m_qwords[i];
			if ( qw1->m_opcode                 ) break;
			if ( qw1->m_inQuotes  != inQuotes  ) break;
			if ( qw1->m_fieldCode != fieldCode ) break;
			if ( qw1->m_ignoreWord== IGNORE_FIELDNAME ) break;
			if ( qw1->m_phraseSign && !qw1->m_rightConnected ) 
				break;
			// are we punct?
			if ( ! is_alnum_utf8 (qw1->m_word) ) 
				endQword = i - 1;
			else    
				endQword = i;
		}
		// revisit this i in big loop since we did not include it
		i = endQword;

		// OOM errors might cause us not to launch frags
		if ( currFrag >= st->m_numFrags )
			continue;
		StateFrag *stFrag = st->m_stFrag[currFrag];
		// don't breech
		if ( dptr + gbstrlen(stFrag->m_dst) >= st->m_dend ) {
			g_errno = EBUFTOOSMALL;
		}
		else {
			// store it
			strcpy ( dptr, stFrag->m_dst );
			dptr += gbstrlen ( dptr );
			// add a space between fragments
			//			*dptr = ' ';
			//dptr++;
			*dptr = '\0';
			// set the flag
			if ( stFrag->m_recommended )
				recommendation = true;
		}
		// copy over all the narrow searches that can fit
		for ( int32_t j = 0; j < stFrag->m_numNarrowPhrases; j++ ){
			// don't breech
			if ( nptr +gbstrlen(stFrag->m_narrowPhrases[j]) >
			     st->m_nend )
				break;
			strcpy(nptr, stFrag->m_narrowPhrases[j]);
			nptr += gbstrlen(stFrag->m_narrowPhrases[j]) + 1;
			(*st->m_numNarrow)++;
		}
		
		mdelete(stFrag, sizeof(StateFrag), "StateFrag");
		delete (stFrag);
		// now we get the next frag
		currFrag++;
	}
	if ( !recommendation )
		*st->m_dst = '\0';
	
	int64_t now = gettimeofdayInMilliseconds();
	if ( now - st->m_start > 50 )
		log(LOG_INFO,"speller: Took %"INT64" ms to spell check %s",
		    now - st->m_start, st->m_q->getQuery() );
	return;
}
*/


bool Speller::generateDicts ( int32_t numWordsToDump , char *coll ){
	m_language[2].setLang(2);
	//m_language[2].generateDicts ( numWordsToDump, coll );
	return false;
}

char *Speller::getRandomWord() {
	int32_t offset = rand() % m_unifiedBuf.length();//Size;
	// find nearest \0
	char *p = m_unifiedBuf.getBufStart() + offset;
	// backup until we hit \0
	for ( ; p > m_unifiedBuf.getBufStart() && *p ; p-- );
	// now advance!
	if ( p > m_unifiedBuf.getBufStart() ) p++;
	// that is the word
	return p;
}

// The unified dict is the combination of the word list, title rec and the top
// query dict of all languages. It has to be created by loading each languages
// dict into memory using Language.loadWordList(), loadTitleRecDict(), etc
bool Speller::loadUnifiedDict() {

	bool building = false;

 reload:

	bool needRebuild = false;

	m_unifiedBuf.purge();
	m_unifiedBuf.setLabel("unibuf");

	// this MUST be there
	if ( m_unifiedBuf.fillFromFile(g_hostdb.m_dir,
				       "unifiedDict-buf.txt" ) == 0 ) 
		needRebuild = true;

	// . give it a million slots
	// . unified dict currently has 1340223 entries
	m_unifiedDict.set ( 8,4, 2*1024*1024,NULL,0,false,0,"udictht");	

	// try to load in the hashtable and the buffer directly
	if ( ! m_unifiedDict.load(g_hostdb.m_dir,"unifiedDict-map.dat"))
		needRebuild = true;

	if ( ! needRebuild ) {
		// convert unifiedBuf \n's to \0's
		char *start = m_unifiedBuf.getBufStart();
		char *end   = start + m_unifiedBuf.length();
		for ( char *p = start ; p < end ; p++ )
			if ( *p == '\n' ) *p = '\0';
		log(LOG_DEBUG,"speller: done loading successfully");

		// a quick little checksum
		if ( ! g_conf.m_isLive ) return true;

		// the size
		int64_t h1 = m_unifiedDict.getNumSlotsUsed();
		int64_t h2 = m_unifiedBuf .length();
		int64_t h = hash64 ( h1 , h2 );
		char *tail1 = (char *)m_unifiedDict.m_keys;
		char *tail2 = m_unifiedBuf.getBufStart()+h2-1000;
		h = hash64 ( tail1 , 1000 , h );
		h = hash64 ( tail2 , 1000 , h );
		//int64_t n = 8346765853685546681LL;
		int64_t n = -14450509118443930LL;
		if ( h != n ) {
			log("gb: unifiedDict-buf.txt or "
			    "unifiedDict-map.dat "
			    "checksum is not approved for "
			    "live service (%"INT64" != %"INT64")" ,h,n);
			//return false;
		}

		return true;
	}

	if ( building ) {
		log("gb: rebuild failed. exiting.");
		exit(0);
	}

	building = true;

	log("gb: REBUILDING unifiedDict-buf.txt and unifiedDict-map.dat");

	// just in case that was there and the buf wasn't
	m_unifiedDict.clear();
	// or vice versa
	m_unifiedBuf.purge();

	// load the .txt file. this is REQUIRED for rebuild
	SafeBuf ub;
	if ( ub.fillFromFile (g_hostdb.m_dir,"unifiedDict.txt") <= 0 )
		return false;

	//
	// change \n to \0
	// TODO: filter out the first word from each line?
	//
	char *start = ub.getBufStart();
	char *end   = start + ub.length();
	for ( char *p = start ; p < end ; p++ )
		if ( *p == '\n' ) *p = '\0';


	// now scan wikitionary file wiktionary-lang.txt to get even
	// more words! this file is generated from Wiktionary.cpp when
	// it scans the wiktionary xml dump to generate the other
	// wiktionary-syns.dat and wiktionary-buf.txt files. it also
	// cranks this file out because we can use it since we do not
	// have czech in the unifiedDict.txt file.
	SafeBuf wkfBuf;
	if ( wkfBuf.fillFromFile ( g_hostdb.m_dir,"wiktionary-lang.txt") <= 0 )
		return false;

	// scan each line
	char *p = wkfBuf.getBufStart();
	char *pend = p + wkfBuf.length();
	HashTableX wkfMap;
	// true = allow dups. because same word can appear in multiple langs
	if ( ! wkfMap.set ( 8,1,1000000,NULL,0,true,0,"wkfmap") )
		return false;

	// "fr|livre" is how it's formatted
	for ( ; p && p < pend ; p = wkfBuf.getNextLine(p) ) {
		char *start = p;
		// skip til |
		for ( ; *p && *p != '|' ; p++ );
		// sanity check
		if ( *p != '|' ) { char *xx=NULL;*xx=0; }
		// tmp NULL that
		*p = '\0';
		char langId = getLangIdFromAbbr(start);
		// revert
		*p = '|';
		if ( langId == langUnknown )
			continue;
		if ( langId == langTranslingual )
			continue;
		// skip |
		p++;
		// that's the word
		char *word = p;
		// find end
		char *end = p;
		for ( ; *end && *end != '\n' ; end++ ) ;
		// so hash it up
		int64_t wid = hash64d ( word , end - word );
		// debug point
		//if ( wid == 5000864073612302341LL )
		//	log("download");
		// add it to map
		if ( ! wkfMap.addKey ( &wid , &langId ) ) return false;
	}



	//
	// scan unifiedDict.txt file
	//
	int32_t totalCollisions = 0;
	uint64_t atline = 0;
	p = start;
	while ( p < end ) {
		atline++;
		char *phrase = p;
		// if line is a comment skip it
		if ( *p == '#' ){
			p += gbstrlen(p) + 1;
			continue;
		}
		// skip phrase
		while ( *p != '\t' )
			p++;
		// Null end the phrase
		*p = '\0';

		// skip empty phrases
		if(gbstrlen(phrase) < 1) {
			log(LOG_WARN,
				"spell: Got zero length entry in unifiedDict "
			    "at line %"UINT64", skipping\n",
				atline);
			p += gbstrlen(p) + 1;
			continue;
		}

		// skip single byte words that are not alphabetic
		// Anything over 'Z' is likely unicode, so don't bother
		if(gbstrlen(phrase) == 1 && (phrase[0] < 'a')) {
			log(LOG_WARN,
				"spell: Got questionable entry in "
			    "unifiedDict at line %"UINT64", skipping: %s\n",
				atline,p);
			p += gbstrlen(p) + 1;
			continue;
		}
		// . i need to move everything over to utf8!!!
		// . this is the same hash function used by Words.cpp so that
		p++;
		// phonet
		char *phonet = p;
		// next is the phonet
		while ( *p != '\t' )
			p++;
		// Null end the phonet
		*p = '\0';
		p++;

		uint64_t key = hash64d(phrase,gbstrlen(phrase));

		// make sure we haven't added this word/phrase yet
		if ( m_unifiedDict.isInTable ( &key ) ) {
			totalCollisions++;
			p += gbstrlen(p) + 1;
			continue;
		}

		// reset lang vector
		int64_t pops[MAX_LANGUAGES];
		memset ( pops , 0 , MAX_LANGUAGES * 8 );

		// see how many langs this key is in in unifiedDict.txt file
		char *phraseRec = p;
		getPhraseLanguages2 ( phraseRec , pops );

		// make all pops positive if it has > 1 lang already
		//int32_t count = 0;
		//for ( int32_t i = 0 ; i < MAX_LANGUAGES ; i++ )
		//	if ( pops[i] ) count++;

		int32_t imax = MAX_LANGUAGES;
		//if ( count <= 1 ) imax = 0;
		// assume none are in official dict
		// seems like nanny messed things up, so undo that
		// and set it negative if in wiktionary in loop below
		for ( int32_t i = 0 ; i < imax ; i++ )
			// HOWEVER, if it is -1 leave it be, i think it
			// was probably correct in that case for some reason.
			// Wiktionary fails to get a TON of forms for
			// many foreign languages in the english dict.
			// so nanny got these from some dict, so try to
			// keep them.
			// like 'abelhudo'
			// http://pt.wiktionary.org/wiki/abelhudo
			// and is not in en.wiktionary.org
			// . NO! because it has "ein" as english with
			//   a -1 popularity as well as "ist"! reconsider
			if ( pops[i] < -1 ) pops[i] *= -1;
			//if ( pops[i] < 0 ) pops[i] *= -1;

		// debug
		//if ( strcmp(phrase,"download") == 0 )
		//	log("hey");

		// now add in from wiktionary
		int32_t slot = wkfMap.getSlot ( &key );
		for ( ; slot >= 0 ; slot = wkfMap.getNextSlot(slot,&key) ) {
			uint8_t langId = *(char *)wkfMap.getDataFromSlot(slot);
			if ( langId == langUnknown ) continue;
			if ( langId == langTranslingual ) continue;
			// if it marked as already in that dictionary, cont
			if ( pops[langId] < 0 ) continue;
			// if it is positive, make it negative to mark
			// it as being in the official dictionary
			// -1 means pop unknown but in dictionary
			if ( pops[langId] == 0 ) pops[langId]  = -1;
			else                     pops[langId] *= -1;
		}

		// save the offset
		int32_t offset = m_unifiedBuf.length();

		// print the word/phrase and its phonet, if any
		m_unifiedBuf.safePrintf("%s\t%s\t",phrase,phonet);

		int32_t count = 0;
		// print the languages and their popularity scores
		for ( int32_t i = 0 ; i < MAX_LANGUAGES ; i++ ) {
			if ( pops[i] == 0 ) continue;
			// skip "unknown" what does that really mean?
			if ( i == 0 ) continue;
			m_unifiedBuf.safePrintf("%"INT32"\t%"INT32"\t",
						i,(int32_t)pops[i]);
			count++;
		}
		// if none, revert
		if ( count == 0 ) {
			m_unifiedBuf.setLength(offset);
			// skip "p" to next line in unifiedBuf.txt
			p += gbstrlen(p) + 1;
			continue;
		}

		// trim final tab i guess
		m_unifiedBuf.incrementLength(-1);
		// end line
		m_unifiedBuf.pushChar('\n');

		// directly point to the (lang, score) tuples
		m_unifiedDict.addKey(&key, &offset);

		// skip "p" to next line in unifiedBuf.txt
		p += gbstrlen(p) + 1;
	}

	log (LOG_WARN,"spell: got %"INT32" TOTAL collisions in unified dict",
	     totalCollisions);






	HashTableX dedup;
	dedup.set(8,0,1000000,NULL,0,false,0,"dmdm");

	// . now add entries from wkfBuf that were not also in "ub"
	// . format is "<langAbbr>|<word>\n"
	p = wkfBuf.getBufStart();
	end = p + wkfBuf.length();
	for ( ; p ; p = wkfBuf.getNextLine(p) ) {
		//char *langAbbr = p;
		for ( ; *p && *p !='\n' && *p !='|' ; p++ );
		if ( *p != '|' ) {
			log("speller: bad format in wiktionary-lang.txt");
			char *xx=NULL;*xx=0;
		}
		//*p = '\0';
		//uint8_t langId = getLangIdFromAbbr ( langAbbr );
		//*p = '|';
		// get word
		char *word = p + 1;
		// get end of it
		for ( ; *p && *p !='\n' ; p++ );
		if ( *p != '\n' ) {
			log("speller: bad format in wiktionary-lang.txt");
			char *xx=NULL;*xx=0;
		}
		int32_t wordLen = p - word;
		// wiktinary has like prefixes ending in minus. skip!
		if ( word[wordLen-1] == '-' ) continue;
		// suffix in wiktionary? skip
		if ( word[0] == '-' ) continue;
		// .zr .dd
		if ( word[0] == '.' ) continue;

		// hash the word
		int64_t key = hash64d ( word , wordLen );

		// skip if we did it in the above loop
		if ( m_unifiedDict.isInTable ( &key ) ) continue;

		// skip if already did it in this loop
		if ( dedup.isInTable ( &key ) ) continue;
		if ( ! dedup.addKey ( &key ) ) return false;

		// reset lang vector
		int64_t pops[MAX_LANGUAGES];
		memset ( pops , 0 , MAX_LANGUAGES * 8 );

		// now add in from wiktionary map
		int32_t slot = wkfMap.getSlot ( &key );
		for ( ; slot >= 0 ; slot = wkfMap.getNextSlot(slot,&key) ) {
			uint8_t langId = *(char *)wkfMap.getDataFromSlot(slot);
			if ( langId == langUnknown ) continue;
			if ( langId == langTranslingual ) continue;
			if ( pops[langId] ) continue;
			// -1 means pop unknown but in dictionary
			pops[langId] = -1;
		}

		
		// save the offset
		int32_t offset = m_unifiedBuf.length();

		// . print the word/phrase and its phonet, if any
		// . phonet is unknown here...
		//char *phonet = "";
		m_unifiedBuf.safeMemcpy ( word, wordLen );
		m_unifiedBuf.safePrintf("\t\t");//word,phonet); 

		int32_t count = 0;
		// print the languages and their popularity scores
		for ( int32_t i = 0 ; i < MAX_LANGUAGES ; i++ ) {
			if ( pops[i] == 0 ) continue;
			// skip "unknown" what does that really mean?
			if ( i == 0 ) continue;
			m_unifiedBuf.safePrintf("%"INT32"\t%"INT32"\t",
						i,(int32_t)pops[i]);
			count++;
		}
		// if none, revert
		if ( count == 0 ) {
			m_unifiedBuf.setLength(offset);
			continue;
		}

		// trim final tab i guess
		m_unifiedBuf.incrementLength(-1);
		// end line
		m_unifiedBuf.pushChar('\n');

		// directly point to the (lang, score) tuples
		m_unifiedDict.addKey(&key, &offset);

	}



	


	// save the text too! a merge of unifiedDict.txt and
	// wiktionary-lang.txt!!!
	if ( m_unifiedBuf.saveToFile(g_hostdb.m_dir,"unifiedDict-buf.txt") <=0)
		return false;

	// save it
	if ( m_unifiedDict.save(g_hostdb.m_dir,"unifiedDict-map.dat")<=0 )
		return false;

	// start over and load what we created
	goto reload;

	// hmmm... seems like we need to re-run for some reason
	log("spell: PLEASE RERUN gb");
	log("spell: PLEASE RERUN gb");
	log("spell: PLEASE RERUN gb");
	exit(0);

	return true;
}

// in case the language is unknown, just give the pop of the
// first found language
int32_t Speller::getPhrasePopularity ( char *str, uint64_t h,
				    bool checkTitleRecDict,
				    unsigned char langId ){
	//char *xx=NULL;*xx=0;

	// hack fixes. 
	// common word like "and"?
	if ( isCommonWord(h) ) return MAX_PHRASE_POP;
	// another common word check
	if ( isQueryStopWord(NULL,0,h,langId) ) return MAX_PHRASE_POP;
	// single letter?
	if ( str && str[0] && str[1] == '\0' ) return MAX_PHRASE_POP;
	// 0-99 only
	if ( str && is_digit(*str) ) {
		if ( !str[1]) return MAX_PHRASE_POP;
		if ( is_digit(str[1])&& !str[2]) return MAX_PHRASE_POP;
	}

	// what up with this?
	//if ( !s ) return 0;
	int32_t slot = m_unifiedDict.getSlot(&h);
	// if not in dictionary assume 0 popularity
	if ( slot == -1 ) return 0;
	//char *p = *(char **)m_unifiedDict.getValueFromSlot(slot);
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	char *pend = p + gbstrlen(p);

	// skip word itself
	while ( *p != '\t' ) p++;
	p++;
	// skip phonet, if any
	while ( *p != '\t' ) p++;
	p++;

	int32_t max = 0;

	// the tuples are in ascending order of the langid
	// get to the right language
	while ( p < pend ){

		int32_t currLang = atoi(p);

		// the the pops are sorted by langId, return 0 if the lang
		// was not found
		if ( langId != langUnknown && currLang > langId )
			return 0;
			
		// skip language
		while ( *p != '\t' ) p++;
		p++;

		int32_t score = atoi(p);

		// i think negative scores mean it is only from titlerec and
		// not in any of the dictionaries.
		if ( score < 0 )
			score *= -1;

		if ( currLang == langId && langId != langUnknown )
			return score;

		// if lang is unknown get max
		if ( score > max ) max = score;

		// skip that score and go to the next <lang> <pop> tuple
		while ( *p != '\t' && *p != '\0' ) p++;
		p++;

	}
	return max;
}

// splits words and checks if they form a porn word or not. montanalinux.org 
// is showing up as porn because it has 'anal' in the hostname. So try to
// find a combination of words such that they are NOT porn.
// try this only after isAdult() succeeds.
// Always tries to find longer words first. so 'montanalinux' is split as
// 'montana' and 'linux' and not as 'mont', 'analinux'
// if it finds a seq of words leading upto a porn word, then it returns true
// eg. shall split montanalinux into 'mont', 'anal', and return true without
// checking if 'inux' is a word. Need to do this because isAdult() cannot
// define where an adult word has ended. 
// TODO: chatswingers.com NOT identified as porn because it is split as 
// 'chats' and 'wingers'.

bool Speller::canSplitWords( char *s, int32_t slen, bool *isPorn, 
			     char *splitWords,
			     unsigned char langId, int32_t encodeType ){
	//char *xx=NULL;*xx=0;

	*isPorn = false;
	char *index[1024];
	if ( slen == 0 )
		return true;
	*splitWords = '\0';

	// this is the current word we're on
	int32_t curr = 0;
	index[curr++] = s;
	index[curr] = s + slen;
	while ( curr > 0 ){
		char *nextWord = NULL;
		while ( findNext( index[curr-1], index[curr], 
				  &nextWord, isPorn, langId, encodeType ) ){
			// next word in chain
			index[curr++] = nextWord;
			index[curr] = s + slen;
			// found a porn word OR 
			// finished making a sequence of words
			if ( *isPorn || nextWord == s + slen ){
				char *p = splitWords;
				for ( int32_t k = 1; k < curr; k++ ){
					gbmemcpy (p, index[k - 1], 
						index[k] - index[k - 1]);
					p += index[k] - index[k - 1];
					*p = ' ';
					p++;
				}
				*p = '\0';
				return true;
			}
		}

		// did not find any word. reduce the current position
		while ( --curr > 0 ){
			if ( curr > 0 && index[curr] > index[curr-1] ){
				index[curr]--;
				break;
			}
		}
	}
	return false;
}

bool Speller::findNext( char *s, char *send, char **nextWord, bool *isPorn,
			unsigned char langId, int32_t encodeType ){
	//char *xx=NULL;*xx=0;

	char *loc = NULL;
	int32_t slen = send - s;
	// check if there is an adult word in there
	// NOTE: The word 'adult' gives a lot of false positives, so even 
	// though it is in the isAdult() list, skip it.
	// s/slen constitues an individual word.
	if ( isAdult ( s, slen, &loc ) && strncmp ( s, "adult", 5 ) != 0 ){
		// if this string starts with the adult word, don't check 
		// further
		if ( loc == s ){
			*isPorn = true;
			*nextWord = send;
			return true;
		}
	}
	for ( char *a = send; a > s; a-- ){
		// a hack, if the word is only one letter long, check if it
		// is 'a' or 'i'. If not then continue
		if ( a - s == 1 && *s != 'a' && *s != 'i')
			continue;
		// another hack, the end word of the string cannot be 2 letters
		// or less. freesex was being split as 'frees ex'
		if ( a == send && a - s <= 2 )
			continue;

		// do not allow "ult" to be a word because it always will
		// split "adult" into "ad+ult"
		if ( a - s == 3 && s[0]=='u' && s[1]=='l' && s[2]=='t' )
			continue;
		// adultsiteratings = "ad ul ts it era tings" 
		if ( a - s == 2 && s[0]=='u' && s[1]=='l' )
			continue;
		// lashaxxxnothing = "lash ax xx nothing"
		if ( a - s == 2 && s[0]=='u' && s[1]=='l' )
			continue;
		// livesexasian = "lives ex asian"
		if ( a - s == 2 && s[0]=='e' && s[1]=='x' )
			continue;
		// fuckedtits = "fu ck edt its"
		if ( a - s == 2 && s[0]=='c' && s[1]=='k' )
			continue;
		// blogsexe = "blogs exe" ... many others
		// any 3 letter fucking word starting with "ex"
		if ( a - s == 3 && s[0]=='e' && s[1]=='x' )
			continue;
		// shemales = "*s hem ales"
		if ( a - s == 4 && s[0]=='a' &&s[1]=='l'&&s[2]=='e'&&s[3]=='s')
			continue;
		// grooverotica = "groove rot ica"
		if ( a - s == 3 && s[0]=='i' && s[1]=='c' && s[2]=='a' )
			continue;
		// dinerotik = dinero tik
		if ( a - s == 3 && s[0]=='t' && s[1]=='i' && s[2]=='k' )
			continue;
		// nudeslutpics = "nud esl ut pics"
		if ( a - s == 3 && s[0]=='n' && s[1]=='u' && s[2]=='d' )
			continue;
		// seepornos = "seep or nos"
		if ( a - s == 3 && s[0]=='n' && s[1]=='o' && s[2]=='s' )
			continue;
		// bookslut = "books lut"
		if ( a - s == 3 && s[0]=='l' && s[1]=='u' && s[2]=='t' )
			continue;
		// lesexegratuit = "lese xe gratuit"
		if ( a - s == 2 && s[0]=='x' && s[1]=='e' )
			continue;
		// mooiemensensexdating = "mens ense xd a ting"
		if ( a - s == 2 && s[0]=='x' && s[1]=='d' )
			continue;
		// mpornlinks = mpo rn links
		if ( a - s == 2 && s[0]=='r' && s[1]=='n' )
			continue;
		// ukpornbases = ukp or nba bes
		if ( a - s == 2 && s[0]=='o' && s[1]=='r' )
			continue;
		// slut
		if ( a - s == 2 && s[0]=='l' && s[1]=='u' )
			continue;
		// independentstockholmescorts = "tock holme sco rts"
		if ( a - s == 3 && s[0]=='s' && s[1]=='c' && s[2]=='o' )
			continue;
		// relatosexcitantes = relat ose xci tan tes 
		if ( a - s == 3 && s[0]=='x' && s[1]=='c' && s[2]=='i' )
			continue;
		// babe = * bes
		if ( a - s == 3 && s[0]=='b' && s[1]=='e' && s[2]=='s' )
			continue;
		// xpornreviews "xp orn reviews "
		if ( a - s == 3 && s[0]=='o' && s[1]=='r' && s[2]=='n' )
			continue;
		// shemal fix
		if ( a - s == 3 && s[0]=='h' && s[1]=='e' && s[2]=='m' )
			continue;
		// adultswim = adults wim
		if ( a - s == 3 && s[0]=='w' && s[1]=='i' && s[2]=='m' )
			continue;
		// bdsm
		if ( a - s == 3 && s[0]=='d' && s[1]=='s' && s[2]=='m' )
			continue;
		// anal
		if ( a - s == 3 && s[0]=='n' && s[1]=='a' && s[2]=='l' )
			continue;
		// vibrator = bra 
		if ( a - s == 3 && s[0]=='b' && s[1]=='r' && s[2]=='a' )
			continue;
		// sitiospornox = sitio spor nox
		if ( a - s == 4 && s[0]=='s' && s[1]=='p' && s[2]=='o' &&
		     s[3] == 'r' )
			continue;
		// orn*
		if ( a - s == 4 && s[0]=='o' && s[1]=='r' && s[2]=='n' )
			continue;
		// hotescorts = hote scor
		if ( a - s == 4 && s[0]=='s' && s[1]=='c' && s[2]=='o' &&
		     s[3] == 'r' )
			continue;
		// uniformsluts = uniformts lutz
		if ( a - s == 4 && s[0]=='l' && s[1]=='u' && s[2]=='t' &&
		     s[3] == 'z' )
			continue;
		// free porn login = freep ornl
		if ( a - s == 5 && s[0]=='f' && s[1]=='r' && s[2]=='e' &&
		     s[3] == 'e' && s[4] == 'p' )
			continue;
		// shemal fix
		if ( a - s == 5 && s[0]=='h' && s[1]=='e' && s[2]=='m' &&
		     s[3] == 'a' && s[4] == 'l' )
			continue;
		// inbondage = inbond age
		if ( a - s == 6 && 
		     s[0]=='i' && s[1]=='n' && s[2]=='b' &&
		     s[3]=='o' && s[4]=='n' && s[5]=='d' )
			continue;
		// swingers = wingers
		if ( a - s == 7 && 
		     s[0]=='w' && s[1]=='i' && s[2]=='n' &&
		     s[3]=='g' && s[4]=='e' && s[5]=='r' &&
		     s[6]=='s' )
			continue;
		// free sex contents = freese xc ont ents
		if ( a - s == 2 && s[0]=='x' && s[1]=='c' )
			continue;
		// mosexstore = mose xs tore
		if ( a - s == 2 && s[0]=='x' && s[1]=='s' )
			continue;
		// phonesexfootsies
		if ( a - s == 8 && 
		     s[0]=='p' && s[1]=='h' && s[2]=='o' &&
		     s[3]=='n' && s[4]=='e' && s[5]=='s' &&
		     s[6]=='e' && s[7]=='x' )
			continue;
		// cybersex
		if ( a - s == 8 && 
		     s[0]=='c' && s[1]=='y' && s[2]=='b' &&
		     s[3]=='e' && s[4]=='r' && s[5]=='s' &&
		     s[6]=='e' && s[7]=='x' )
			continue;
		// hotescorts
		

		// check if the word has popularity. if it is in the 
		// unifiedDict, then it is considered to be a word
		uint64_t h = hash64d(s, a-s);//a - s, encodeType);
		int32_t pop = getPhrasePopularity(s, h, false, langId);

		// continue if did not find it
		if ( pop <= 0 )
			continue;
		// this is our next word
		*nextWord = a;
		return true;
	}
	return false;
}	

//similar to one above but using recursion
/*bool Speller::canSplitWords( char *s, int32_t slen, bool *isPorn, 
  char *splitWords,
  unsigned char langId, int32_t encodeType ){

  if ( slen == 0 )
  return true;
  char *loc = NULL;
  // check if there is an adult word in there
  if ( isAdult ( s, slen, &loc ) ){
  // if this string starts with the adult word
  if ( loc == s ){
  gbmemcpy ( splitWords, s, slen );
  splitWords[slen] = ' ';
  splitWords[slen + 1] = '\0';
  *isPorn = true;
  return true;
  }
  }

  char *b = s + slen;
  // split the phrase into two or more phrases.
  for ( char *a = b; a > s; a-- ){
  //	while ( a > s ){
  // a hack, if the word is only one letter long, check if it
  // is 'a' or 'i'. If not then continue
  if ( a - s == 1 && *s != 'a' && *s != 'i')
  continue;

  // check if the word has popularity. if it is in the 
  // unifiedDict, then it is considered to be a word
  uint64_t h = hash64d(s, a - s, encodeType);
  int32_t pop = getPhrasePopularity(s, h, false, langId);

  // continue if did not find it
  if ( pop <= 0 )
  continue;
  gbmemcpy ( splitWords, s, a - s );
  splitWords[a - s] = ' ';
  splitWords[a - s + 1] = '\0';
  // see if we can split the rest
  if ( canSplitWords ( a, b - a, isPorn, 
  splitWords + (a - s + 1),
  langId, encodeType ) )
  return true;
  }
  // did not find any sequence of words that can make this string
  return false;
  }*/

bool Speller::createUnifiedDict (){
	// first get all the tuples from wordlist and query file
	//HashTableT <uint64_t, char*> ht[MAX_LANGUAGES];
	HashTableX ht[MAX_LANGUAGES];
	char ff[1024];
	for ( int32_t i = 0; i < MAX_LANGUAGES; i++ ){
		ht[i].set ( 8,4,0,NULL,0,false,0,"cud");
		sprintf ( ff , "%sdict/%s/%s.wl.phonet", g_hostdb.m_dir,
			  getLanguageAbbr(i), getLanguageAbbr(i) );
		populateHashTable(ff, &ht[i], i);

		sprintf ( ff , "%sdict/%s/%s.query.phonet.top", g_hostdb.m_dir,
			  getLanguageAbbr(i), getLanguageAbbr(i) );
		populateHashTable(ff, &ht[i], i);

		for ( int32_t j = 0; j < NUM_CHARS; j++ ){
			sprintf ( ff , "%sdict/%s/%s.dict.%"INT32"", g_hostdb.m_dir,
				  getLanguageAbbr(i), getLanguageAbbr(i), j );
			populateHashTable(ff, &ht[i], i);
		}
	}

	//sprintf ( ff, "%sdict/unifiedDict",g_hostdb.m_dir );
	sprintf ( ff, "%sunifiedDict.txt",g_hostdb.m_dir );
	// delete it first
	unlink ( ff );
	// then open a new one for appending
	int fdw = open ( ff , 
			 O_CREAT | O_RDWR | O_APPEND ,
			 getFileCreationFlags());
			 // S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH);
	if ( fdw < 0 ){
		return log("lang: Could not open for %s "
			   "writing: %s.",ff, strerror(errno));
	}

	log(LOG_INIT,"spell: Making %s.", ff );

	//HashTableT <uint64_t, int32_t> phrases;
	HashTableX phrases;
	phrases.set(8,4,0,NULL,0,false,0,"phud");
	char buf[1024];
	for ( int32_t  i = 0; i < MAX_LANGUAGES; i++ ){
		// get each slot
		for ( int32_t j = 0; j < ht[i].getNumSlots(); j++ ){
			uint64_t key = *(uint64_t *)ht[i].getKey(j);
			if ( key == 0 )
				continue;
			// if key is already found
			int32_t slot = phrases.getSlot(&key);
			if ( slot != -1 )
				continue;

			char *tuple = *(char **)ht[i].getValueFromSlot(j);

			// here we print the phrase and the phonet if present
			// skip the score
			while ( *tuple != '\t' )
				tuple++;
			tuple++;
			
			sprintf( buf, "%s", tuple );
			
			char *p = buf;
			p += gbstrlen(buf);

			// if there wasn't a phonet, its from the titleRec.
			// add another tab
			bool fromTitleRec = false;
			if ( strstr (tuple,"\t") == NULL ){
				*p = '\t';
				p++;
				fromTitleRec = true;
			}

			for ( int32_t k = 0; k < MAX_LANGUAGES; k++ ){
				slot = ht[k].getSlot(&key);
				if ( slot == -1 )
					continue;
				char *val = *(char **)ht[k].getValueFromSlot(slot);
				int32_t pop = atoi(val);
				if ( fromTitleRec ) pop *= -1;
				sprintf(p,"\t%"INT32"\t%"INT32"",k,pop);
				p += gbstrlen(p);
			}
			// write out the trailing \n as well
			*p = '\n';
			p++;
			*p = '\0';
			p++;
			int32_t bufLen = gbstrlen(buf);
			int32_t wn = write ( fdw , buf , bufLen ) ;
			if ( wn != bufLen )
				return log("lang:  write: %s",strerror(errno));
			int32_t val = 1;
			phrases.addKey(&key, &val);
		}
	}
	return true;
}


bool Speller::populateHashTable( char *ff, HashTableX *htable,
				 unsigned char langId ){
	File f;
	f.set(ff);
	// open file
	if ( ! f.open ( O_RDONLY ) ) {
		log("spell: open: %s",mstrerror(g_errno)); 
		return false; 
	}
	
	// get file size
	int32_t fileSize = f.getFileSize() ;

	int32_t bufSize = fileSize + 1;
	char *buf = (char *) mmalloc(bufSize, "SpellerTmpBuf");
	if (!buf)
		return false;
	if ( !f.read(buf, fileSize,0) ){
		log("spell: read: %s", mstrerror(g_errno));
		return false;
	}
	for ( int32_t i = 0; i < bufSize; i++ ){
		if ( buf[i] == '\n' )
			buf[i] = '\0';
	}

	char *p = buf;
	while ( p < buf + fileSize ){
		char *tuple = p;
		int32_t score = atoi(p);
		// many scores in dict have a pop of 0. ignore them
		if ( score <= 0 ){
			p += gbstrlen(p) + 1;
			continue;
		}
		while ( *p != '\t' )
			p++;
		p++;
		// at the phrase
		char *phrase = p;
		while ( *p != '\t' && *p != '\0' )
			p++;
		uint64_t key = hash64d(phrase, p-phrase );
		int32_t slot = htable->getSlot(&key);
		if ( slot == -1 )
			htable->addKey(&key,&tuple);
		p += gbstrlen(p) + 1;
	}
	return true;
}

// This isn't really much use except for the spider
// language detection to keep from making 32 sequential
// calls for the same phrase to isolate the language.
char *Speller::getPhraseRecord(char *phrase, int len ) {
	//char *xx=NULL;*xx=0;
	if ( !phrase ) return NULL;
	//char *rv = NULL;
	int64_t h = hash64d(phrase, len);
	int32_t slot = m_unifiedDict.getSlot(&h);
	//log("speller: h=%"UINT64" len=%i slot=%"INT32"",h,len,slot);
	if ( slot < 0 ) return NULL;
	//rv = *(char **)m_unifiedDict.getValueFromSlot(slot);
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	return p;
}

/*
uint8_t Speller::getUniqueLang ( int64_t *wid ) {
	int32_t slot = m_unifiedDict.getSlot(wid);
	if (slot < 0) return langUnknown;
	//char *p = *(char **)m_unifiedDict.getValueFromSlot(slot);
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	int32_t langId = langUnknown;
	char langCount = 0;
	// skip over word
	for ( ; *p && *p != '\t' ; ) p++;
	// nothing after?
	if ( !*p ) return langUnknown;
	// skip tab
	p++;
	// skip over phonet
	for ( ; *p && *p != '\t' ; ) p++;
	// nothing after?
	if ( !*p ) return langUnknown;
	// skip tab
	p++;
	// loop over langid/pop pairs
	while ( *p ) {
		// get langid
		langId = atoi(p);
		// skip to next delimiter
		for ( ; *p && *p != '\t' ; p++ );
		// error?
		if ( ! *p ) break;
		// skip tab
		p++;
		// error?
		if ( ! *p ) break;
		// . if pop is zero ignore it
		// . we now set pops to zero when generating
		//   unifiedDict-buf.txt if they are not in the wiktionary
		//   map for that language. seems like to many bad entries
		//   were put in there by john nanny.
		//char pop = 1;
		//if ( *p == '0' ) pop = 0;
		// require it be in the official dictionary here
		bool official;
		if ( *p == '-' ) official = true;
		else             official = false;   
		// skip pop
		for ( ; *p && *p != '\t' ; p++ );
		// multi lang count
		if ( langId != langUnknown && official ) langCount++;
		// no unique lang
		//if ( langCount >= 2 ) return langTranslingual;
		if ( langCount >= 2 ) return langUnknown;
		// done?
		if ( ! *p ) break;
		// skip tab
		p++;
	}
	// unique lang!
	return langId;
}
*/

int64_t Speller::getLangBits64 ( int64_t *wid ) {
	int32_t slot = m_unifiedDict.getSlot(wid);
	if (slot < 0) return 0LL;
	int32_t offset =  *(int32_t *)m_unifiedDict.getValueFromSlot(slot);
	char *p = m_unifiedBuf.getBufStart() + offset;
	// skip over word
	for ( ; *p && *p != '\t' ; ) p++;
	// nothing after?
	if ( !*p ) return 0LL;
	// skip tab
	p++;
	// skip over phonet
	for ( ; *p && *p != '\t' ; ) p++;
	// nothing after?
	if ( !*p ) return 0LL;
	// skip tab
	p++;
	// init
	int64_t bits = 0LL;
	// loop over langid/pop pairs
	while ( *p ) {
		// get langid
		uint8_t langId = atoi(p);
		// skip to next delimiter
		for ( ; *p && *p != '\t' ; p++ );
		// error?
		if ( ! *p ) break;
		// skip tab
		p++;
		// error?
		if ( ! *p ) break;
		// . if pop is zero ignore it
		// . we now set pops to zero when generating
		//   unifiedDict-buf.txt if they are not in the wiktionary
		//   map for that language. seems like to many bad entries
		//   were put in there by john nanny.
		//char pop = 1;
		// if not official, cancel it?
		if ( *p != '-' ) langId = langUnknown;
		// skip pop
		for ( ; *p && *p != '\t' ; p++ );
		// multi lang count
		//if ( langId != langUnknown ) langCount++;
		// no unique lang
		//if ( langCount >= 2 ) return langTranslingual;
		if ( langId != langTranslingual &&
		     langId != langUnknown )
			// make english "1"
			bits |= 1LL << (langId-1);
		// done?
		if ( ! *p ) break;
		// skip tab
		p++;
	}
	return bits;
}

/*
int64_t *Speller::getPhraseLanguages(char *phrase, int len ) {
	//char *xx=NULL;*xx=0;

	char *phraseRec = getPhraseRecord(phrase, len );
	if(!phraseRec) return(NULL);
	int64_t *rv = (int64_t *)mmalloc(sizeof(int64_t) * MAX_LANGUAGES,
					     "PhraseRec");
	if(!rv) return(NULL);
	if(!getPhraseLanguages(phrase, len, rv)) {
		mfree(rv, sizeof(int64_t) * MAX_LANGUAGES,
		      "PhraseRec");
		return(NULL);
	}
	return(rv);
}
*/
 
bool Speller::getPhraseLanguages(char *phrase, int len,
				 int64_t *array) {
	//char *xx=NULL;*xx=0;

	char *phraseRec = getPhraseRecord(phrase, len);
	if(!phraseRec || !array) return false;
	return getPhraseLanguages2 ( phraseRec,array );
}

bool Speller::getPhraseLanguages2 (char *phraseRec , int64_t *array) {

	int64_t l = 0;
	memset(array, 0, sizeof(int64_t)*MAX_LANGUAGES);

	while(*phraseRec) {
		l = 0;
		// skip leading whitespace
		while(*phraseRec && (*phraseRec == ' ' ||
				     *phraseRec == '\t'))
			phraseRec++;

		if(!*phraseRec) break;

		int64_t l = atoi(phraseRec);
		// l = abs(l); // not using score method anymore, so this is moot.

		// skip to next delimiter
		// while(*phraseRec && *phraseRec != '\t') phraseRec++;
		if(!(phraseRec = strchr(phraseRec, '\t'))) break;

		// skip tab
		phraseRec++;

		if(!*phraseRec) break;

		// wtf?
		if ( *phraseRec == '\t' ) return true;

		// Save score
		array[l] = atoi(phraseRec);

		// skip to next delimiter
		// while(*phraseRec && *phraseRec != '\t') phraseRec++;
		if(!(phraseRec = strchr(phraseRec, '\t'))) break;

		// skip over tab
		if(*phraseRec == '\t') phraseRec++;
	}
	return(true);
}

bool Speller::getSynsInEnglish ( char *w , 
				 int32_t wlen ,
				 char nativeLang ,
				 char wikiLang ) {
	// no digits please!
	if ( is_digit(w[0]) ) return false;

	char *p = getPhraseRecord(w,wlen);
	if ( ! p ) return false;
	bool inEnglish = false;
	// skip word
	for ( ; *p != '\t' ; p++ );
	// skip tab
	p++;
	// skip phonet
	for ( ; *p != '\t' ; p++ );
	// skip tab
	p++;

	for ( ; *p ; ) {
		// end of line?
		if ( !*p ) return inEnglish;
		// get language id
		int32_t l = atoi(p);
		// english?
		//if ( l == langEnglish ) inEnglish = true;
		//if ( l > langEnglish && ! inEnglish ) return false;
		//if ( l == nativeLang ) return false;
		// skip langid
		for ( ; *p && *p != '\t' ; p++ );
		// end of line?
		if ( !*p ) return inEnglish;
		// skip tab
		p++;
		// . get popularity. if not negative undo inEnglish.
		// . it has to be negative because that means it is in the
		//   OFFICIAL wiktionary dictionary for that language
		if ( l == langEnglish && p[0] == '-' ) inEnglish = true;
		// if this word is in the doc's primary/native language
		// then do not try to get english synonyms of it
		if ( l == nativeLang && p[0] == '-' ) return false;
		// no chance? it MUST be in english, and these are
		// sorted by langid...
		if ( l > langEnglish && ! inEnglish ) return false;
		// skip popularity
		for ( ; *p && *p != '\t' ; p++ );
		// no more?
		if ( ! *p ) 
			return inEnglish;
		// skip tab
		p++;
	}
	return inEnglish;
}

/*
static inline int s_findMaxVal(int64_t *vals, int numVals) {
	int64_t max, oldmax, val;
	if(!vals) return(0);
	max = oldmax = INT_MIN;
	val = 0;
	for(int x = 0; x < numVals; x++) {
		if(vals[x] >= max) {
			oldmax = max;
			max = vals[x];
			val = x;
		}
	}
	if(oldmax == max) return(0);
	return(val);
}

char Speller::getPhraseLanguage(char *phrase, int len) {
	//char *xx=NULL;*xx=0;

	char lang;
	int64_t *langs = getPhraseLanguages(phrase, len);
	if(!langs) return(0);
	lang = s_findMaxVal(langs, MAX_LANGUAGES);
	if ( lang < 0 ) { char *xx=NULL;*xx=0; }
	if(langs[(uint8_t)lang] == 0) lang = 0;
	mfree(langs, sizeof(int) * MAX_LANGUAGES, "PhraseRec");
	return(lang);
}
*/

void Speller::dictLookupTest ( char *ff ){
	//char *ff = "/tmp/sctest";
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd ) {
		log("speller: test: Could not open %s for "
		    "reading: %s.", ff,strerror(errno));
		return;
	}
	int64_t start = gettimeofdayInMilliseconds();
	char buf[1026];
	int32_t count = 0;
	// go through the words
	while ( fgets ( buf , MAX_FRAG_SIZE , fd ) ) {
		// length of word(s), including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// skip if empty
		if ( wlen <= 0 ) continue;
		buf[wlen-1]='\0';
		uint64_t h = hash64d ( buf, gbstrlen(buf));
		int32_t pop = g_speller.getPhrasePopularity(buf, h, true);
		if ( pop < 0 ){
			char *xx = NULL; *xx = 0;
		}
		count++;
	}
	log ( LOG_WARN,"speller: dictLookupTest took %"INT64" ms to do "
	      "%"INT32" words. Compare against 46-66ms taken for dict/words file.",
	      gettimeofdayInMilliseconds() - start, count );
	fclose(fd);
}
