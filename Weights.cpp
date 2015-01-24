// TODO: pass spam class to weight class and modify weights based on the spam
//       then we can just serialize the weight vector in the title rec along
//       with ptr offsets to the words that we index. carver can just scan
//       through the word ptrs rather than 1 char at a time. summary generator
//       can just use the weights to score each sample then. 


#include "gb-include.h"

#include "Weights.h"
#include "Words.h"
#include "Bits.h"
#include "Phrases.h"
#include "Titledb.h"
#include "HashTableX.h"
#include "Abbreviations.h"
#include "XmlNode.h" // g_nodes[]
#include "HashTable.h"
#include "Sections.h"

static HashTable s_punctTable;

bool initPunctWeights ( ) ;

Weights::Weights () {
	m_buf     = NULL;
	m_bufSize = 0;
	m_ww      = NULL;
	m_pw      = NULL;
	m_countTablePtr = NULL;
}

Weights::~Weights () {
	reset();
}

void Weights::reset() {
	if ( m_buf && m_buf != m_localBuf )
		mfree ( m_buf , m_bufSize , "Weights" );
	m_buf     = NULL;
	m_bufSize = 0;
	m_ww      = NULL;
	m_pw      = NULL;
	m_countTablePtr = NULL;
}

// RULE #1 (apply to meta tags, too)
// these rules should be applied when indexing/hashing the incoming linktext, 
// title, document body and meta tags.

// RULE #2 (promte rather than demote)
// prefer to promote rather than demote because then we do not have to reindex 
// the low quality pages, just the good ones, which are fewer, and the good 
// ones will bubble to the top.

// RULE #3 (in parentheses)
// if word in ()'s []'s or {}'s then demote word and phrase score equally.
// after the first 40 words in the parentheses, do not demote any more.

// RULE #4 (next to bad punct)
// if we have a sequence of punctuation that is not just spaces on the left
// or the right of the word, demote it. Right now we just use 
// Bits::canPairAcross(), but we should get more accurate...
// We compute a word and phrase  weight for each punctuation "word", i.e.
// sequence of punctuation, and we use initPunctWeights() to set that table.
// Apply these weights differently to both the word and the phrase. The
// phrase weight from this is averaged across the 

// RULE #5 (in hyperlink)
// if word or phrase is in a hyperlink multiply score by .15.

// RULE #6 (in content section)
// if word is in a section with lots of plain text words that are not
// in hyperlinks, boost it. If section is skimpy demote each word, but only 
// demote a word if NOT the first occurence of that word in that section.
// That way we allow for headers in their own section, like a table row, <tr>.
// Sections are delimited by "breaking tags" defined in XmlNode.cpp, g_nodes[].

// RULE #7 (in header, italic, bold or title tag)
// boost words and phrases in header, italic, bold or title tags.
// Only do this for the first 20 words in header tags to 
// prevent abuse. Italic and bold are times also have a combined limit of 40.
// up to the first 20 alnum words in a title tag are boosted.

// RULE #8 (in a ul list)
// demote if in a list, under the <ul> tag.

// RULE #9 (repeated in sentence/fragment)
// if word/phrase repeated in same sentence, demote it.

// RULE #10 (comma-separated list)
// multiply score by .3 if in a comma-separated list. only 
// count one occurence in all such lists.

// RULE #11 (capitalized words)
// multiply score by 1.3 if the word is capitalized and is NOT immediately
// adjacent to another capitalized word, and not at the beginning of a 
// sentence. Demote word score if it is immediately next to a capitalized
// word on the left or right, it is part of a phrase probably.
// If word is capitalized and so is adjacent word, but they are separated
// by a punct word that has less than DW of word weight, do not demote
// the word for being adjacent to a cap word.

// RULE #12 (repeated sentence frags)
// Demote the weight of the words and phrases in repeated sentence fragments.
// Fixes message boards which include the same msg over again in the reply. 
// The first title and first header tag have amnesty, those often repeat 
// anyway. What about int32_t titles? The demotion will be more the longer
// the repeated fragment. TODO: Fragments have to have a minimum length of
// 5 words unless they are surrounded by breaking tags. Hey, but we will demote
// those words for being in a small section via RULE #6.

// RULE #13 (promote if well distributed)
// If word is preceeded by the same word, but in a different section, boost
// the word for being well distributed.

// RULE #14 (common phrases) (UNIMPLEMENTED)
// if word is part of a common fragment in the dictionary files then multiply 
// its score by .15. Have a constant fixed file of just the common fragments 
// in order to keep parser consistent for now, until we store the 
// scores/termids in the title rec. Fixes 'new mexico' results coming up for
// 'mexico' query. Apply this rule to incoming link text as well. Names like 
// "Tom Cruise" should be an exception. Regenerate the dictionary because 
// fragments are often bad, like a lot start with just 's ' and some do not 
// break over tags like they should it seems, and we have "states of america" 
// in there, too. Let's just hand filter the top ones out for now and put them 
// into a file.

// RULE #15 (word to phrase ratio)
// if a word repeats in different phrases, promote the word and 
// demote the phrase. BUT if a word repeats in pretty much the same phrase, 
// promote the phrase and demote the word

// RULE #16 (beginning of the sentence)
// weight word at the beginning of the sentence, 1.2, and at the end, .8. 
// interpolate linearly in between. It is more likely to be the subject 
// matter if at the start of the sentence.

// RULE #17
// if phrase preceeded immediately by single or double quote, promote it.
// but only if not promoted from Rule #18 below.

// RULE #18 (capitalized phrase)
// if the word is capitalized and the last word in its phrase is capitalized
// then promote the phrase score.
// TODO: Do not consider fragments that start or end with 'the' or 'a' or 
//       fragments with punctuation in them, like "Sparks, OK".


// RULE #19 (hyphenated phrases)
// if word occurs in a hyphenated phrase, boost the phrase score and demote
// the word score.

// RULE #20 (inherit score)
// the base score is the average of the word scores in the phrase. HOWEVER, do 
// not take the punishment received in SOME of the above rules, like for the 
// word being in a common phrase or something. that should not apply to us!

// RULE #21 (UNIMPLEMENTED)
// index capitalized words with the capitalized letter and with it 
// uncapitalized. then if someone types in "Sparks" they get the one that 
// have that word capitalized.

// RULE #22 (phrase weight adjustment)
// the score of a phrase should be the phrase score of the first word in the 
// phrase but weighted by the scores of the phrase scores of the contained 
// punctuation words. certain sequences of punctuation contained in the phrase
// will drastically reduce its score.

// RULE #23 (words in special tags)
// do not index words in style, script or marquee tags. Index words and phrases
// in select tags, but with minimal weight.

// RULE #24 (primitive numbered list detector)
// if previous word began with a digit demote the word that follows it, but not
// if there is a breaking tag in between, however. demote the word if a number
// word follows the word as well, with no tag in between.
// TODO: make this more accurate.

// RULE #25 (demote phrases splitting hyphenated phrases)
// if one of the boundary words in a phrase is next to one and only one hyphen,
// and that hyphen is not in the phrase, then demote the phrase. if we have a 
// phrase like like "spam-free email" we should demote the phrase "free email".
// it splits a STRONG_CONNECTOR.

// RULE #26 ("long" phrases) (not implemented -- using PQR, post query rerank)
// if phrase is very popular we usually weight it high and the individual words
// low. however, if the phrase term to the left or right of the phrase term
// has a very similar count, we should cut each phrase term's weights in half
// because it is probably a int32_t phrase. the more phrases in the chain that
// have the similar count, the more we should doc the phrase term weights
// of all involved. this is very similar to the anti-spam logic we have in
// set3(), repeated sentence frags, rule #13...

// RULE #27 reduce words/phrases in menu-y  sections.

// RULE #28 repetitive spam detector (was Spam.cpp class)

float g_wtab[30][30];
float g_ptab[30][30];

// . returns false and sets g_errno on error
// . determines weights that are 1-1 with the words in the Words.cpp class
// . Words.cpp must contain tags cuz that's what we look at to divide the
//   words up into sections
// . most docs are divided up into sections based on div, and table/tr/td tags
bool Weights::set ( Words      *words              ,
		    Phrases    *phrases            ,
		    Bits       *bits               ,
		    Sections   *sections           ,
		    SafeBuf    *pbuf               , // debug?
		    bool        eliminateMenus     ,
		    bool        isPreformattedText ,
		    int32_t        titleRecVersion    ,
		    int32_t        titleWeight        ,
		    int32_t        headerWeight       ,
		    HashTableX *countTablePtr      ,
		    bool        isLinkText         ,
		    bool        isCountTable       ,
		    int32_t        siteNumInlinks     ,
		    int32_t        niceness           ) {

	reset();

	m_version        = titleRecVersion;
	m_titleWeight    = titleWeight;
	m_headerWeight   = headerWeight;
	m_niceness       = niceness;
	m_isLinkText     = isLinkText;
	m_isCountTable   = isCountTable;
	m_words          = words;
	m_bits           = bits;
	m_siteNumInlinks = siteNumInlinks;

	// use the provided count table, if not NULL
	m_countTablePtr = countTablePtr;

	// init the punct weight hash table
	static bool s_needsInit = true;
	if ( s_needsInit ) {
		// if out of memory, we can't do much...
		if ( ! initPunctWeights() ) return false;
		s_needsInit = false;
	}

	// allocate memory
	int32_t nw = words->getNumWords();
	int32_t need = nw * (4 + 4 );

	// one float for each rule per word so we can print out the weight
	// associated with each algorithm (RULE #) for XmlDoc::print() as
	// it is called by PageParser.cpp for debug purposes
	if ( pbuf ) need +=  2 * nw * MAX_RULES * sizeof(float);

	if ( need < LOCAL_BUF_SIZE ) m_buf = m_localBuf;
	else     m_buf = (char *)mmalloc ( need , "Weights" );
	m_bufSize = need;
	if ( ! m_buf ) return false;
	// assign ptrs
	char *p = m_buf;
	m_ww = (int32_t *)p; p += 4 * nw;
	m_pw = (int32_t *)p; p += 4 * nw;

	// the rule weight vector used for debugging by XmlDoc::print()
	m_rvw = NULL;
	m_rvp = NULL;
	if ( pbuf ) {
		float *start = (float *)p;
		m_rvw = (float *)p; p += MAX_RULES * 4 * nw;
		m_rvp = (float *)p; p += MAX_RULES * 4 * nw;
		// init all to 1.0
		for ( ; start < (float *)p ; start++ ) *start = 1.0;
		// sanity check
		if ( p > m_buf + need ) { char *xx=NULL;*xx=0; }
	}

	// . set all weights to default if weighting incoming link text
	// . we only want to apply the word to phrase ratio weights really
	if ( m_isLinkText || m_isCountTable ) {
		for ( int32_t i = 0 ; i < nw ;i++ ) {
			m_ww[i] = DW;
			m_pw[i] = DW;
		}
	}
	// . for each word, set a phrase weight and a word weight
	// . each word can be a tag, alnum word or punct word
	else {
		memset ( m_pw , 0 , 4 * nw );
		memset ( m_ww , 0 , 4 * nw );
	}

	// yield cpu
	QUICKPOLL ( m_niceness );


	m_wids      = words->getWordIds ();		
	m_nw        = words->getNumWords();

	// . TODO?
	// . if url is cgi, hash the values of the cgi parms into this table
	// . demote those words in the doc because chances are we are a
	//   search results page, or content generated from those words
	// . but increase the weight if the word is in the mid-domain of the
	//   url. that should override being in the cgi val in case of ties
	// . do not do this promotion for urls with hyphens, but only for
	//   urls like gigablast.com where there is only one word "gigablast"
	//m_urlTable

	QUICKPOLL ( m_niceness );

	// . set pass 1
	// . 7 of the 14 ms is this
	if ( ! set1 ( words, phrases, bits, isPreformattedText) ) return false;

	QUICKPOLL ( m_niceness );

	// . set pass 2
	// . just adjusts the phrase scores per RULE #22 and word scores
	//   per RULE #4
	// . 2 of the 14 ms is this
	if ( ! set2 ( words, phrases, bits ) ) return false;

	QUICKPOLL ( m_niceness );

	// . set pass 3
	// . look for repeating fragments
	// . 1 of the 14 ms is this
	//if ( ! set3 ( words , bits ) ) return false;

	QUICKPOLL ( m_niceness );

	// the old Spam.cpp spam detector
	//if ( ! set4 ( ) ) return false;

	QUICKPOLL ( m_niceness );

	// . Sections replaces Scores for menu elimination technology
	// . eliminate words in script, style, etc. tags
	// . google seems to index marquee junk so i took SEC_MARQUEE out
	int32_t badFlags  = SEC_SCRIPT|SEC_STYLE|SEC_SELECT;
	int32_t goodFlags = SEC_IN_TITLE|SEC_IN_HEADER;//SEC_ARTICLE
	// if not enough voters do not eliminate menus, cuz we are not
	// that sure...
	//if ( sections && sections->m_totalSimilarLayouts < 5 ) 
	//	eliminateMenus = false;
	// only do this if we have sections
	if ( ! sections ) nw = 0;
	// might have no identified sections
	if ( sections && sections->m_numSections <= 0 ) nw = 0;
	// int16_tcut
	Section *sn;
	// loop over all words
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get section
		sn = sections->m_sectionPtrs[i];
		// get the flags of the section containing this word
		int32_t flags = 0;
		// set 'em if we got 'em. a root level meta tag has NULL sect.
		if ( sn ) flags = sn->m_flags;
		// is it in marquee, select, script or style tag?
		if ( flags & badFlags ) {
			// nukey!
			m_ww[i] = 0;
			m_pw[i] = 0;
			// debug purposes
			if ( m_rvw ) {
				m_rvw[i*MAX_RULES+23] = 0.0;
				m_rvp[i*MAX_RULES+23] = 0.0;
			}
			continue;
		}
		// if we are not valid and we are supposed to eliminate menus
		// then we should not index anything, since we are not sure
		// what section is what! being invalid just means we could
		// not get enough voters to make a decision on what was 
		// a menu section and what wasn't.
		if ( eliminateMenus ) {
			// nukey!
			m_ww[i] = 0;
			m_pw[i] = 0;
			// debug purposes
			if ( m_rvw ) {
				m_rvw[i*MAX_RULES+23] = 0.0;
				m_rvp[i*MAX_RULES+23] = 0.0;
			}
			continue;
		}
		// if an article, or non-menu, do not reduce it
		if ( flags & goodFlags ) continue;
		// if not a repeat section skip as well
		//if ( ! ( flags & SEC_DUP ) ) continue;
		if ( sn->m_votesForNotDup > sn->m_votesForDup ) continue;
		// . RULE #27
		// . reduce dynamic, non-unique sections (top stories)
		// . reduce static sections (menus)
		// . also reduces EVERYTHING if we are "invalid"...
		m_ww[i] = (int32_t)(m_ww[i] * .10);
		m_pw[i] = (int32_t)(m_pw[i] * .10);
		// debug purposes
		if ( m_rvw ) {
			m_rvw[i*MAX_RULES+27] = 0.10;
			m_rvp[i*MAX_RULES+27] = 0.10;
		}
	}

	/*
	// we now use the scores for its menu elimination tech
	if ( ! scores ) return true;

	// now if the score is 0, make the m_ww and m_pw BOTH 0
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// skip if score is good
		if ( scores->m_scores[i] > 0 ) continue;
		// otherwise nukey!
		m_ww[i] = 0;
		m_pw[i] = 0;
	}
	*/

	return true;
}
/*
// match word sequences of NUMWORDS or more words
#define NUMWORDS 5

// . RULE #12 (repeated sentence frags)
bool Weights::set3 ( Words *words , Bits *bits ) {

	if ( m_isLinkText ) return true;

	// ez vars
	int64_t  *wids  = words->getWordIds ();		
	int32_t        nw    = words->getNumWords();

	// if no words, nothing to do
	if ( nw == 0 ) return true;

	// truncate for performance reasons. i've seen this be over 4M
	// and it was VERY VERY SLOW... over 10 minutes...
	// - i saw this tak over 200MB for an alloc for
	//   WeightsSet3 below, so lower from 200k to 50k. this will probably
	//   make parsing inconsistencies for really large docs...
	if ( nw > 80000 ) nw = 80000;

	int64_t   ringWids [ NUMWORDS ];
	int32_t        ringPos  [ NUMWORDS ];
	int32_t        ringi = 0;
	int32_t        count = 0;
	int64_t   h     = 0;

	// . make the hash table
	// . make it big enough so there are gaps, so chains are not too long
	int32_t       minBuckets = (int32_t)(nw * 1.5);
	int32_t       nb         = 2 * getHighestLitBitValue ( minBuckets ) ;
	int32_t       need       = nb * (8+4);
	char      *buf        = NULL;
	char       tmpBuf[50000];
	if ( need < 50000 ) buf = tmpBuf;
	else                buf = (char *)mmalloc ( need , "WeightsSet3" );
	char      *ptr        = buf;
	int64_t *hashes     = (int64_t *)ptr; ptr += nb * 8;
	int32_t      *vals       = (int32_t      *)ptr; ptr += nb * 4;
	if ( ! buf ) return false;

	// make the mask
	uint32_t mask = nb - 1;

	// clear the hash table
	memset ( hashes , 0 , nb * 8 );

	// clear ring of hashes
	memset ( ringWids , 0 , NUMWORDS * 8 );

	// for sanity check
	int32_t lastStart = -1;

	// . hash EVERY NUMWORDS-word sequence in the document
	// . if we get a match look and see what sequences it matches
	// . we allow multiple instances of the same hash to be stored in
	//   the hash table, so keep checking for a matching hash until you
	//   chain to a 0 hash, indicating the chain ends
	// . check each matching hash to see if more than NUMWORDS words match
	// . get the max words that matched from all of the candidates
	// . demote the word and phrase weights based on the total/max
	//   number of words matching
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not alnum word
		if ( ! wids[i] ) continue;
		// yield
		QUICKPOLL ( m_niceness );		
		// add new to the 5 word hash
		h ^= wids[i];
		// . remove old from 5 word hash before adding new...
		// . initial ring wids are 0, so should be benign at startup
		h ^= ringWids[ringi];
		// add to ring
		ringWids[ringi] = wids[i];
		// save our position
		ringPos[ringi] = i;
		// wrap the ring ptr if we need to, that is why we are a ring
		if ( ++ringi >= NUMWORDS ) ringi = 0;
		// this 5-word sequence starts with word # "start"
		int32_t start = ringPos[ringi];
		// need at least NUMWORDS words in ring buffer to do analysis
		if ( ++count < NUMWORDS ) continue;
		// . skip if it starts with a word which can not start phrases
		// . that way "a new car" being repeated a lot will not 
		//   decrease the weight of the phrase term "new car"
		// . setCountTable() calls set3() with this set to NULL
		//if ( bits && ! bits->canStartPhrase(start) ) continue;
		// sanity check
		if ( start <= lastStart ) { char *xx = NULL; *xx = 0; }
		// reset max matched
		int32_t max = 0;
		// look up in the hash table
		int32_t n = h & mask;
	loop:
		// all done if empty
		if ( ! hashes[n] ) {
			// sanity check
			//if ( n >= nb ) { char *xx = NULL; *xx = 0; }
			// add ourselves to the hash table now
			hashes[n] = h;
			// sanity check
			//if ( wids[start] == 0 ) { char *xx = NULL; *xx = 0; }
			// this is where the 5-word sequence starts
			vals  [n] = start;
			// save it
			lastStart = start;
			// debug point
			//if ( start == 7948 )
			//	log("heystart");
			// do not demote words if less than NUMWORDS matched
			if ( max < NUMWORDS ) continue;
			// . how much we should we demote
			// . 10 matching words pretty much means 0 weights
			float demote = 1.0 - ((max-5)*.10);
			if ( demote >= 1.0 ) continue;
			if ( demote <  0.0 ) demote = 0.0;

			// . RULE #26 ("long" phrases)
			// . if we got 3, 4 or 5 in our matching sequence
			// . basically divide by the # of *phrase* terms
			// . multiply by 1/(N-1) 
			// . HOWEVER, should we also look at HOW MANY other
			//   sequences matches this too!???
			//float demote = 1.0 / ((float)max-1.0);
			// set3() is still called from setCountTable() to 
			// discount the effects of repeated fragments, and
			// the count table only understands score or no score
			//if ( max >= 15 ) demote = 0.0;

			// demote the next "max" words
			int32_t mc = 0;
			int32_t j;
			for ( j = start ; mc < max ; j++ ) {
				// skip if not an alnum word
				if ( ! wids[j] ) continue;
				// count it
				mc++;
				// demote it
				m_ww[j] = (int32_t)(m_ww[j] * demote);
				m_pw[j] = (int32_t)(m_pw[j] * demote);
				if ( m_ww[j] <= 0 ) m_ww[j] = 2;
				if ( m_pw[j] <= 0 ) m_pw[j] = 2;
				// debug purposes
				if ( m_rvw ) {
					m_rvw[i*MAX_RULES+26] *= demote;
					m_rvp[i*MAX_RULES+26] *= demote;
				}
			}
			// save the original i
			int32_t mini = i;
			// advance i, it will be incremented by 1 immediately
			// after hitting the "continue" statement
			i = j - 1;
			// must be at least the original i, we are monotinic
			// otherwise ringPos[] will not be monotonic and core
			// dump eventually cuz j and k will be equal below
			// and we increment matched++ forever.
			if ( i < mini ) i = mini;
			// get next word
			continue;
		}
		// get next in chain if hash does not match
		if ( hashes[n] != h ) { 
			// wrap around the hash table if we hit the end
			if ( ++n >= nb ) n = 0; 
			// check out bucket #n now
			goto loop; 
		}
		// how many words match so far
		int32_t matched = 0;
		// . we have to check starting at the beginning of each word
		//   sequence since the XOR compositional hash is order
		//   independent
		// . see what word offset this guy has
		int32_t j = vals[n] ;
		// k becomes the start of the current 5-word sequence
		int32_t k = start;
		// sanity check
		if ( j == k ) { char *xx = NULL; *xx = 0; }
		// skip to next in chain to check later
		if ( ++n >= nb ) n = 0;
		// keep advancing k and j as int32_t as the words match
	matchLoop:
		// get next wid for k and j
		while ( k < nw && ! wids[k] ) k++;
		while ( j < nw && ! wids[j] ) j++;
		if ( k < nw && wids[k] == wids[j] ) { 
			matched++; 
			k++; 
			j++; 
			goto matchLoop; 
		}
		// keep track of the max matched for i0
		if ( matched > max ) max = matched;
		// get another matching string of words, if possible
		goto loop;
	}
	if ( buf != tmpBuf ) mfree ( buf , need , "WeightsSet3" );
	return true;
}
*/

// RULE #22
// adjust the phrase weights so that the phrase weight, m_pw[], of the first
// word in the phrase is the weight of the whole phrase
bool Weights::set2 ( Words   *words   ,
		     Phrases *phrases ,
		     Bits     *bits   ) {

	if ( m_isCountTable ) return true;

	// . we need to set the count table if not provided
	// . m_countTablePtr is a ptr to the count table
	// . we make it point to m_localCountTable if not provided
	// . 3 of the 14 ms is this
	if ( ! m_countTablePtr ) { char *xx = NULL; *xx = 0; }

	// ez var
	int64_t  *wids  = words->getWordIds ();		
	nodeid_t   *tids  = words->getTagIds  ();
	//char      **wptrs = words->m_words;
	//int32_t       *wlens = words->m_wordLens;
	int32_t        nw    = words->getNumWords();
	int64_t  *pids  = phrases->getPhraseIds();
	HashTableX *tt1   = m_countTablePtr;

	//int32_t      phrcountLast = 0;
	int32_t      nexti        = -10;
	int64_t pidLast      = 0;

	//logf(LOG_DEBUG,"build: still doing int32_t sanity check in here");

	// . now consider ourselves the last word in a phrase
	// . adjust the score of the first word in the phrase to be
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not alnum word
		if ( ! wids[i] ) continue;
		//if ( i == 38 )
		//	log("hey2");
		//if(wptrs[i][0]=='S' && wptrs[i][1]=='e' && wptrs[i][2]=='r' )
		//	log("hey2");

		// yield
		QUICKPOLL ( m_niceness );		

		// . RULE #15
		// . try to inline this
		int64_t nextWid = 0;
		int64_t lastPid = 0;
		int32_t      nwp = phrases->getNumWordsInPhrase(i);
		if ( nwp > 0 ) nextWid = wids [i + nwp - 1] ;
		if ( i == nexti ) lastPid = pidLast;
		// get current pid
		int64_t pid = pids[i];
		// PHRASE WEIGHT HACK:
		// if it is 0, force a phrase so that "News &amp; Events"
		// is seen as a phrase as far as Weights.cpp is concerned
		// NO LONGER NEEDED since we de-entityize all html docs now!!
		/*
		if ( pid == 0LL ) {
			// get the next wid in line
			int32_t j    = i+ 2;
			int32_t maxj = i+15;
			if ( maxj >= nw ) maxj = nw;
			for ( ; j < maxj ; j++ ) if ( wids[j] ) break;
			if ( j < maxj ) {
				pid = hash64 ( pid , wids[i] );
				pid = hash64 ( pid , wids[j] );
			}
		}
		*/
		// get the word and phrase weights for term #i
		float ww;
		float pw;
		getWordToPhraseRatioWeights ( lastPid  , // pids[i-1],
					      wids[i]  ,
					      pid      ,
					      nextWid  , // wids[i+1] ,
					      &ww      ,
					      &pw      ,
					      tt1      ,
					      m_version );
		// assigne for debugging
		//if ( m_rvw ) {
		//	m_rvw[i*MAX_RULES+15] *= ww;
		//	m_rvp[i*MAX_RULES+15] *= pw;
		//}
		// save the last phrase id
		if ( nwp > 0 ) {
			nexti        = i + nwp - 1;
			pidLast      = pid; // pids[i] ;
		}
		// . apply the weights
		// . do not hit all the way down to zero though...
		// . Words.cpp::hash() will not index it then...
		if ( m_ww[i] > 0 ) {
			m_ww[i] = (int32_t)(m_ww[i] * ww);
			if ( m_ww[i] <= 0 ) m_ww[i] = 1;
			// debug purposes
			if ( m_rvw ) m_rvw[i*MAX_RULES+15] *= ww;
		}
		if ( m_pw[i] > 0 ) {
			m_pw[i] = (int32_t)(m_pw[i] * pw);
			if ( m_pw[i] <= 0 ) m_pw[i] = 1;
			// debug purposes
			if ( m_rvw ) m_rvp[i*MAX_RULES+15] *= pw;
		}

		if ( m_isLinkText ) continue;

		//
		// sanity check
		//

		/*
		// . RULE #15
		// . we copy getWordToPhraseRatioWeights() here in an effort
		//   to verify that the function works properly. if so,
		//   then we should end up with the same output here.
		// . this will be commented out later.
		// . if a word repeats in different phrases, promote the word 
		//   and  demote the phrase
		// . if a word repeats in pretty much the same phrase, promote 
		//   the  phrase and demote the word
		int32_t phrcount = 0 ;
		if ( pids[i] ) 
			phrcount=tt1->getScore (pids[i]);
		int32_t wrdcount =  tt1->getScore (wids[i]);
		// if we are always ending the same phrase, like "Mexico"
		// in "New Mexico"... get the most popular phrase this word is
		// in...
		int32_t phrcountMax;
		if ( i == lastj && phrcountLast > phrcount ) 
			phrcountMax = phrcountLast;
		else
			phrcountMax = phrcount;
		// save wordcount before possibly truncating it
		int32_t wrdcountMin = wrdcount;
		// watch out for breeches
		if ( wrdcount > 29 ) {
			float ratio = (float)phrcountMax / (float)wrdcount;
			phrcountMax = (int32_t)((29.0 * ratio) + 0.5);
			wrdcount    = 29;
		}
		if ( phrcountMax > 29 ) {
			float ratio = (float)wrdcount / (float)phrcountMax;
			wrdcount    = (int32_t)((29.0 * ratio) + 0.5);
			phrcountMax = 29;
		}
		// . sanity check
		// . countLinkInfo() below may just add a phrase term without 
		//   its corresponding word term, thereby breeching this 
		//   sanity check, so let's comment it out
		//if ( phrcountMax > wrdcount ) { char *xx = NULL; *xx = 0; }
		// apply the weights from the table we computed above
		m_ww[i] = (int32_t)(m_ww[i] * g_wtab[wrdcount][phrcountMax]);
		// get length of phrase in words
		//int32_t nwp = phrases->getNumWords(i);
		// save the last phrase id
		if ( nwp > 0 ) {
			lastj        = i + nwp - 1;
			phrcountLast = phrcount;
			pidLast      =  pids[i] ;
		}
		// . if the word is Mexico in 'New Mexico good times' then 
		//   phrase term #i which is, say, "Mexico good" needs to
		//   get the min word count when doings its word to phrase
		//   ratio. 
		// . it has two choices, it can use the word count of 
		//   "Mexico" or it can use the word count of "good".
		// . say, each is pretty high in the document so the phrase
		//   ends up getting penalized heavily, which is good because
		//   it is a nonsense phrase.
		// . if we had "united socialist soviet republic" repeated
		//   a lot, the phrase "socialist soviet" would score high
		//   and the individual words would score low. that is good.
		if ( nwp > 0 ) {
			// get the word count of the next word
			nextWid = wids [i + nwp - 1] ;
			int32_t wc = tt1->getScore(nextWid);
			// if it is smaller than current word, set wrdcountMin
			if ( wc < wrdcountMin ) wrdcountMin = wc;
		}
		// watch out for breeches
		if ( wrdcountMin > 29 ) {
			float ratio = (float)phrcount / (float)wrdcountMin;
			phrcount    = (int32_t)((29.0 * ratio) + 0.5);
			wrdcountMin = 29;
		}
		if ( phrcount > 29 ) {
			float ratio = (float)wrdcountMin / (float)phrcount;
			wrdcountMin = (int32_t)((29.0 * ratio) + 0.5);
			phrcount    = 29;
		}
		// try to seek the highest weight possible for this phrase
		// by choosing the lowest word count possible
		m_pw[i] = (int32_t)(m_pw[i] * g_ptab[wrdcountMin][phrcount]);
		// temp sanity checks until the inlined function works
		if ( g_wtab[wrdcount][phrcountMax] != ww ) {
			char *xx = NULL; *xx = 0; }
		if ( g_ptab[wrdcountMin][phrcount] != pw ) {
			char *xx = NULL; *xx = 0; }
		*/

		//
		// end sanity check
		//


		// . RULE #4
		// . if word adjacent to bad punct, demote it
		// . or if adjacent to a high phrase weight punct word then
		//   likewise, we should demote it, like a hyphen 

		// . set1() must have been called for this to work
		// . apply word weight based on punct to our left, if any
		if ( i  >0  && !wids[i-1] && (!tids || !tids[i-1]) ) {
			m_ww[i] = ((int64_t)m_ww[i]*(int64_t)m_ww[i-1])/DW;
			// debug purposes here
			if ( m_rvw ) m_rvw [i*MAX_RULES+4] *= 
					     ((float)m_ww[i-1])/(float)DW;
		}
		// apply word weight based on punct to our right, if any
		if ( i+1<nw && !wids[i+1] && (!tids || !tids[i+1]) ) {
			m_ww[i] = ((int64_t)m_ww[i]*(int64_t)m_ww[i+1])/DW;
			// debug purposes here
			if ( m_rvw ) m_rvw [i*MAX_RULES+4] *= 
					     ((float)m_ww[i+1])/(float)DW;
		}
		// do not demote all the way to 0
		if ( m_ww[i] <= 0 ) m_ww[i] = 2;

		// RULE #25
		// . if one of the boundary words in a phrase is next to one
		//   and only one hyphen, and that hyphen is not in the phrase
		//   then demote the phrase, it is splitting a hyphenated 
		//   phrase, like for "spam-free email" we should demote the 
		//   phrase "free email". it splits a STRONG_CONNECTOR. (mdw)
		// . if we had a tight hyphen right before us this should
		//   have the 1.1 weight specified in the PunctWeights array
		//if ( m_pw[i-1] == 1.10 * DW && m_pw[i+1] != 1.10 * DW ) {
		//	// down to 25%
		//	m_pw[i] = m_pw[i] * .25;
		//}

		//if ( i  >0  && !wids[i-1] && 
		//     (!tids || !tids[i-1]) && m_pw[i-1]!=DW )
		//	m_ww[i] = (int32_t)(m_ww[i] * .75);
		//if ( i+1<nw && !wids[i+1] && 
		//    (!tids || !tids[i+1]) && m_pw[i+1]!=DW )
		//	m_ww[i] = (int32_t)(m_ww[i] * .75);

		// skip if phrase score is 0
		if ( ! m_pw[i] ) continue;
		// skip if cannot start a phrase
		//if ( ! bits->canStartPhrase(i) ) { m_pw[i] = 0; continue; }
		//if ( pids[i] == 0 ) { m_pw[i] = 0; continue; }
		if ( pid == 0 ) { m_pw[i] = 0; continue; }
		// skip if does not start phrase
		if ( nwp <= 0 ) continue;
		// sanity check
		if ( nwp == 99 ) { char *xx = NULL; *xx = 0; }
		// now mod the score
		int64_t avg = m_pw[i];
		// weight by punct in between
		for ( int32_t j = i+1 ; j < i+nwp ; j++ ) {
			if ( wids[j] ) continue;
			avg = (avg * (int64_t)m_pw[j]) / DW;
		}
		// do not demote all the way to zero, we still want to index it
		// and when normalized on a 100 point scale, like when printed
		// out by PageParser.cpp, a score of 1 here gets normalized to
		// 0, so make sure it is at least 2.
		if ( avg < 2 ) 
			avg = 2;
		// apply
		if ( m_rvp ) m_rvp [i*MAX_RULES+22] *= avg / m_pw[i] ;
		// set that as our new score
		m_pw[i] = avg;
	}
	return true;
}


// . inline this for speed
// . RULE #15
// . if a word repeats in different phrases, promote the word 
//   and  demote the phrase
// . if a word repeats in pretty much the same phrase, promote 
//   the  phrase and demote the word
// . if you have the window of text "new mexico good times"
//   and word #i is mexico, then:
//   pid1 is "new mexico"
//   wid1 is "mexico"
//   pid2 is "mexico good"
//   wid2 is "good"
// . we store sliderParm in titleRec so we can update it along
//   with title and header weights on the fly from the spider controls
void getWordToPhraseRatioWeights ( int64_t   pid1 , // pre phrase
				   int64_t   wid1 ,
				   int64_t   pid2 ,
				   int64_t   wid2 , // post word
				   float      *ww   ,
				   float      *pw   ,
				   HashTableX *tt1  ,
				   int32_t        titleRecVersion ) {

	static float s_fsp;
	// from 0 to 100
	char sliderParm = g_conf.m_sliderParm;
	// i'm not too keen on putting this as a parm in the CollectionRec
	// because it is so cryptic...
	//static char sliderParm = 25;

	// . to support RULE #15 (word to phrase ratio)
	// . these weights are based on the ratio of word to phrase count
	//   for a particular word
	static char s_sp = -1;
	if ( s_sp != sliderParm ) {
		// . set it to the newly updated value
		// . should range from 0 up to 100
		s_sp = sliderParm;
		// the float version
		s_fsp = (float)sliderParm / 100.0;
		// sanity test
		if ( s_fsp < 0.0 || s_fsp > 1.0 ) { char *xx = NULL; *xx = 0; }
		// i is the word count, how many times a particular word
		// occurs in the document
		for ( int32_t i = 0 ; i < 30 ; i++ ) {
		// . k is the phrase count, how many times a particular phrase
		//   occurs in the document
		// . k can be GREATER than i because we index only phrase terms
		//   sometimes when indexing neighborhoods, and not the
		//   single words that compose them
		for ( int32_t k = 0 ; k < 30 ; k++ ) {
			// do not allow phrase count to be greater than
			// word count, even though it can happen since we
			// add imported neighborhood pwids to the count table
			int32_t j = k;
			if ( k > i ) j = i;
			// get ratio
			//float ratio = (float)phrcount / (float)wrdcount;
			float ratio = (float)j/(float)i;
			// it should be impossible that this can be over 1.0
			// but might happen due to hash collisions
			if ( ratio > 1.0 ) ratio = 1.0;
			// restrict the range we can weight a word or phrase
			// based on the word count
			//float r = 1.0;
			//if      ( i >= 20 ) r = 2.1;
			//else if ( i >= 10 ) r = 1.8;
			//else if ( i >=  4 ) r = 1.5;
			//else                r = 1.3;
			g_ptab[i][k] = 1.00;
			g_wtab[i][k] = 1.00;
			if ( i <= 1 ) continue;
			// . we used to have a sliding bar between 0.0 and 1.0.
			//   word is weighted (1.0 - x) and phrase is weighted
			//   by (x). however, x could go all the way to 1.0 
			//   even when i = 2, so we need to restrict x.
			// . x is actually "ratio"
			// . when we have 8 or less word occurences, do not
			//   remove more than 80% of its score, a 1/5 penalty
			//   is good enough for now. but for words that occur
			//   a lot in the link text or pwids, go to town...
			if      ( i <=  2 && ratio >= .50 ) ratio = .50;
			else if ( i <=  4 && ratio >= .60 ) ratio = .60;
			else if ( i <=  8 && ratio >= .80 ) ratio = .80;
			else if ( i <= 12 && ratio >= .95 ) ratio = .95;
			// round up, so many "new mexico" phrases but only
			// make it up to 95%...
			if ( ratio >= .95 ) ratio = 1.00;
			// if word's phrase is repeated 3 times or more then
			// is a pretty good indication that we should weight
			// the phrase more and the word itself less
			//if ( k >= 3 && ratio < .90 ) ratio = .90;
			// compute the weights
			float pw = 2.0 * ratio;
			float ww = 2.0 * (1.0 - ratio);

			// . punish words a little more
			// . if we got 50% ratio, words should not get as much
			//   weight as the phrase
			//ww *= .45;
			// do not weight to 0, no less than .15
			if ( ww < 0.0001 ) ww = 0.0001;
			if ( pw < 0.0001 ) pw = 0.0001;
			// do not overpromote either
			if ( ww > 2.50 ) ww = 2.50;
			if ( pw > 2.50 ) pw = 2.50;
			// . do a sliding weight of the weight
			// . a "ww" of 1.0 means to do no weight
			// . can't do this for ww cuz we use "mod" below
			//float newWW = s_fsp*ww + (1.0-s_fsp)*1.00;
			float newPW = s_fsp*pw + (1.0-s_fsp)*1.00;
			// limit how much we promote a word because it
			// may occur 30 times total, but have a phrase count
			// of only 1. however, the other 29 times it occurs it
			// is in the same phrase, just not this particular 
			// phrase.
			//if ( ww > 2.0 ) ww = 2.0;
			g_wtab[i][k] = ww; 
			g_ptab[i][k] = newPW;
			//logf(LOG_DEBUG,"build: wc=%"INT32" pc=%"INT32" ww=%.2f "
			//"pw=%.2f",i,k,g_wtab[i][k],g_ptab[i][k]);
		}
		}
	}			

	int32_t phrcount1 = 0;
	int32_t phrcount2 = 0;
	int32_t wrdcount1 = 0;
	int32_t wrdcount2 = 0;
	if ( tt1->m_numSlotsUsed > 0 ) {
		if (pid1) phrcount1 = tt1->getScore(&pid1);
		if (pid2) phrcount2 = tt1->getScore(&pid2);
		if (wid1) wrdcount1 = tt1->getScore(&wid1);
		if (wid2) wrdcount2 = tt1->getScore(&wid2);
	}
	// if we are always ending the same phrase, like "Mexico"
	// in "New Mexico"... get the most popular phrase this word is
	// in...
	int32_t phrcountMax = phrcount1;
	int32_t wrdcountMin = wrdcount1;
	// these must actually exist to be part of the selection
	if ( pid2 && phrcount2 > phrcountMax ) phrcountMax = phrcount2;
	if ( wid2 && wrdcount2 < wrdcountMin ) wrdcountMin = wrdcount2;
	

	// . but if we are 'beds' and in a popular phrase like 'dog beds'
	//   there maybe a lot of other phrases mentioned that have 'beds'
	//   in them like 'pillow beds', 'pet beds', but we need to assume
	//   that is phrcountMax is high enough, do not give much weight to
	//   the word... otherwise you can subvert this algorithm by just 
	//   adding other random phrases with the word 'bed' in them.
	// . BUT, if a page has 'X beds' with a lot of different X's then you 
	//   still want to index 'beds' with a high score!!! we are trying to 
	//   balance those 2 things.
	// . do this up here before you truncate phrcountMax below!!
	float mod = 1.0;
	if      ( phrcountMax <=  6 ) mod = 0.50;
	else if ( phrcountMax <=  8 ) mod = 0.20;
	else if ( phrcountMax <= 10 ) mod = 0.05;
	else if ( phrcountMax <= 15 ) mod = 0.03;
	else                          mod = 0.01;

	// scale wrdcount1/phrcountMax down for the g_wtab table
	if ( wrdcount1 > 29 ) {
		float ratio = (float)phrcountMax / (float)wrdcount1;
		phrcountMax = (int32_t)((29.0 * ratio) + 0.5);
		wrdcount1   = 29;
	}
	if ( phrcountMax > 29 ) {
		float ratio = (float)wrdcount1 / (float)phrcountMax;
		wrdcount1   = (int32_t)((29.0 * ratio) + 0.5);
		phrcountMax = 29;
	}
	
	// . sanity check
	// . neighborhood.cpp does not always have wid/pid pairs
	//   that match up right for some reason... so we can't do this
	//if ( phrcount1 > wrdcount1 ) { char *xx = NULL; *xx = 0; }
	//if ( phrcount2 > wrdcount2 ) { char *xx = NULL; *xx = 0; }

	// apply the weights from the table we computed above
	*ww = mod   *   g_wtab[wrdcount1][phrcountMax];

	// slide it
	*ww = s_fsp*(*ww) + (1.0-s_fsp)*1.00;
	
	// ensure we do not punish too hard
	if ( *ww <= 0.0 ) *ww = 0.01;

	/*
	if ( phrcountMax >= 0 ) {
		int64_t sh = getPrefixHash ( (char *)NULL , 0 , NULL , 0 );
		int64_t tid = g_indexdb.getTermId ( sh , wid1 );
		logf(LOG_DEBUG,"build: phrcountMax=%"INT32" wrdCount1=%"INT32" "
		     "*ww=%.4f for word with tid=%"UINT64"",
		     phrcountMax,wrdcount1,(float)*ww,tid);
		//if ( phrcountMax < 10 && tid == 16944700235015LL ) 
		//	log("hey");
	}
	*/

	// sanity check
	//if ( *ww == 0.0 ) { char *xx = NULL; *xx = 0; }

	// scale wrdcountMin/phrcount down for the g_ptab table
	if ( wrdcountMin > 29 ) {
		float ratio = (float)phrcount2 / (float)wrdcountMin;
		phrcount2   = (int32_t)((29.0 * ratio) + 0.5);
		wrdcountMin = 29;
	}
	if ( phrcount2 > 29 ) {
		float ratio = (float)wrdcountMin / (float)phrcount2;
		wrdcountMin = (int32_t)((29.0 * ratio) + 0.5);
		phrcount2   = 29;
	}
	// . if the word is Mexico in 'New Mexico good times' then 
	//   phrase term #i which is, say, "Mexico good" needs to
	//   get the min word count when doings its word to phrase
	//   ratio. 
	// . it has two choices, it can use the word count of 
	//   "Mexico" or it can use the word count of "good".
	// . say, each is pretty high in the document so the phrase
	//   ends up getting penalized heavily, which is good because
	//   it is a nonsense phrase.
	// . if we had "united socialist soviet republic" repeated
	//   a lot, the phrase "socialist soviet" would score high
	//   and the individual words would score low. that is good.
	// . try to seek the highest weight possible for this phrase
	//   by choosing the lowest word count possible
	// . NO LONGER AFFECT phrase weights because just because the
	//   words occur a lot in the document and this may be the only
	//   occurence of this phrase, does not mean we should punish
	//   the phrase.  -- MDW
	if ( titleRecVersion >= 88 ) {
		*pw = 1.0;
		return;
	}

	// do it the old way...
	*pw = g_ptab[wrdcountMin][phrcount2];

	// sanity check
	if ( *pw == 0.0 ) { char *xx = NULL; *xx = 0; }
}


bool Weights::set1 ( Words    *words              ,
		     Phrases  *phrases            ,
		     Bits     *bits               ,
		     bool      isPreformattedText ) {

	if ( m_isLinkText   ) return true;
	if ( m_isCountTable ) return true;

	if ( ! m_countTablePtr ) { char *xx = NULL; *xx = 0; }

	nodeid_t   *tids  = words->getTagIds  ();
	int64_t  *wids  = words->getWordIds ();		
	char      **wptrs = words->m_words;
	int32_t       *wlens = words->m_wordLens;
	int64_t  *pids  = phrases->getPhraseIds();
	int32_t tid = 0;

	// these punct weights are used for RULE #4
        for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// skip alnum words
		if ( wids[i] ) continue;
		// skip tags
		if ( tids && tids[i] ) continue;
		// just set weight for punct "words"
		setPunctWeights ( i , wptrs[i] , wlens[i] );
		// yield
		QUICKPOLL ( m_niceness );		
        }

	//HashTableT<uint64_t,char> titleRepeatTable;
	HashTableX titleRepeatTable;
	int32_t bufSize = 1024 * 20; // 10 bytes per record 
	char buf[bufSize]; 
	// false = allowDupKeys
	titleRepeatTable.set(8,0,2048,buf,bufSize,false,m_niceness,"titlerpt");

	char inScript  = 0;
	char inStyle   = 0;
	char inSelect  = 0; 
	char inTitle   = 0;
	char inMarquee = 0;
	char inLink    = 0;
	char inQuotes  = 0;
	char inParens  = 0;
	char inHeader  = 0;
	char inItalic  = 0;
	char inBold    = 0;
	char inList    = 0;
	char inPre     = 0;
	char afterNum  = 0;
	char afterQuote= 0;
	int32_t hcount    = 0;
	int32_t tcount    = 0;
	int32_t bcount    = 0;
	int32_t pcount    = 0;
	int32_t brcount   = 0;
	int32_t qcount    = 0;
	// start with fragNum 1 since TermTable::addScore() does not like
	// scores of 0
	int32_t fragNum   = 1;
	int32_t fragPos   = 0;

	if ( isPreformattedText ) inPre = 1;

	// section vars
	int32_t sectionStart     = 0;
	int32_t sectionWordCount = 0;
	int32_t sectionWeightSum = 0;
	// start with section 1 since TermTable::addScore() does not like
	// scores of 0
	int32_t section          = 1;

	// comma vars
	int32_t commaWordCount     = 0;
	int32_t lastCommaWordCount = 0;
	int32_t lastCommai         = 0;

	char charLen = 1;

	// . we use a hash table for:
	// . 1. for hashing all the phrase termids (phrase ids) into the term 
	//      table so we can see if the phrase is unique or not. words that 
	//      are in phrases that are repeated a lot will have their scores 
	//      demoted, likewise, such phrase scores will be promoted. Now
	//      we also hash the wordids into here so we can look at the 
	//      phrase count to word count ratio. (tt1)
	// . 2. discounting multiple occurences of a word in the same 
	//      section. (tt2)
	// . 3. for seeing in what section a word last occurred in, because
	//      we boost the words score when it is spread out over many 
	//      different sections (tt3)
	// . 4. discounting multiple occurences of a word in the same 
	//      sentence/fragment. (tt4)
	//TermTable *tt1 = m_countTablePtr;
	HashTableX  tt2;
	HashTableX  tt3;
	HashTableX  tt4;

	// allow for 33% of the slots to be empty to avoid excessive chaining
	if ( ! tt2.set ( 8,4,(int32_t)(m_nw * 4),NULL,0,false,m_niceness,
			 "weight2") ) 
		return false;
	if ( ! tt3.set ( 8,4,(int32_t)(m_nw * 4),NULL,0,false,m_niceness,
			 "weight3") ) 
		return false;
	// this is for all words and phrases
	if ( ! tt4.set ( 8,4,(int32_t)(m_nw * 4),NULL,0,false,m_niceness,
			 "weight4") ) 
		return false;

	// set the tag ptrs
	char *tagPtrs[256];
	memset ( tagPtrs , 0, 256 * sizeof(char *) );
	// sanity check - let user know to increase this over 256
	if ( getNumXmlNodes() > 256 ) { char *xx = NULL; *xx = 0; }
	// set our quickie array
	tagPtrs[ 43] = &inHeader;
	tagPtrs[ 44] = &inHeader;
	tagPtrs[ 45] = &inHeader;
	tagPtrs[ 46] = &inHeader;
	tagPtrs[ 47] = &inHeader;
	tagPtrs[ 48] = &inHeader;
	tagPtrs[105] = &inList;
	tagPtrs[ 79] = &inPre;
	tagPtrs[ 83] = &inScript;
	tagPtrs[111] = &inStyle;
	tagPtrs[ 84] = &inSelect;
	tagPtrs[ 65] = &inMarquee;
	tagPtrs[101] = &inTitle;
	tagPtrs[  2] = &inLink;
	tagPtrs[ 52] = &inItalic;
	tagPtrs[ 10] = &inBold;
	tagPtrs[ 89] = &inBold; // strong
	int32_t i = -1;
	int32_t tiw = m_titleWeight;
	int32_t hdw = m_headerWeight;
	// sanity check, we cannot support this as we are now since we 
	// subtract 42 from the tagId before setting tagPtrs[x] to it 
	// (see below)
	if ( tagPtrs[42] ) { char *xx = NULL; *xx = 0; }
	// we do not use a *for* loop in order to save a level of indentation
 loop:
	if ( ++i >= m_nw ) 
		return true;

	// yield
	QUICKPOLL ( m_niceness );		

	//
	// . is the word a "tag"?
	// . 2.3 ms of the 7 ms
	//
	if ( tids && tids[i] ) {
		// get the tag id of the ith "word"
		tid = tids[i];
		// it it a front or back tag? (back tags start with "</")
		char isFront;
		if ( tid & BACKBIT ) isFront = 0;
		else                 isFront = 1;
		// clear that hi bit
		tid &= BACKBITCOMP;
		// debug
		//if ( tid == 43 ) {
		//	log("got it");
		//	sleep(5);
		//}
		// set the in* thang
		if ( tagPtrs[tid] ) {
			// this - 42 is to support inHeader
			if ( isFront ) *tagPtrs[tid] = tid - 42; 
			// exit the tag
			else           *tagPtrs[tid] = 0;
		}
		// is it a <br> tag?
		else if ( tid == 20 ) {
			// cancel this flag
			afterNum = false;
			// is there a 2nd, 3rd ... consecutive <br> tag?
			if ( brcount > 0 ) {
				// if so, any phrase containing this tag "word"
				// will have a score of 0
				m_pw[i] = 0;
				goto loop;
			}
			// ok, we only have one <br> so far
			brcount++; 
			// do not affect the score of the phrase containing us
			m_pw[i] = DW;
			goto loop;
		}

		// . a phrase can contain any "non-breaking" tag, so make sure
		//   that m_pw[i], the phrase weight, is the normal 128, so
		//   we can include this tag in the phrase without penalty
		// . see XmlNode.cpp for the list of g_nodes
		if ( ! g_nodes[tid].m_isBreaking ) { 
			m_pw[i] = DW; goto loop; }

		// hey, if we just got a header tag

		// cancel this flag with any breaking tag
		afterNum = false;

		// come here when we hit a word that ends our current section
	newSection:
		// reset the attribute flags if we encounter a breaking tag
		inBold   = 0;
		inItalic = 0;
		// . do not reset this! we might have just had a header tag
		//   right above and just got done setting this to 1!!!!
		// . we have to reset to delete old stuff cleanly!
		if ( m_version <= 85 ) inHeader = 0;
		// and reset the thing for comma-separated list detection
		lastCommaWordCount = -999;
		// . RULE #6
		// . adjust word/phrase scores of this section that just ended
		// . do not count words in links for "sectionWordCount"
		// . these are based on default weights of 128
		// . it is not necessarily bad for the word to be in a int16_t
		//   fragment, like when searching for 'denver weather', for
		//   example, you want to find those words in int16_t header 
		//   fragments. so do not demote weight for int16_t frags so much
		float sw;
		if      ( sectionWordCount <   10 ) sw = 0.8;
		else if ( sectionWordCount <   30 ) sw = 1.1;
		else if ( sectionWordCount <   50 ) sw = 1.2;
		else if ( sectionWordCount <  100 ) sw = 1.3;
		else if ( sectionWordCount <  150 ) sw = 1.4;
		else if ( sectionWordCount <  200 ) sw = 1.5;
		else                                sw = 1.6;
		// TODO: get the average word score of the lowest scoring
		// half of all words in this section
		for ( int32_t j = sectionStart ; j < i ; j++ ) {
			// skip if not a word
			if ( ! wids[j] ) continue;
			// hash the word and phrase ids with the section num
			int64_t hw = wids[j] * (int64_t)section;
			int64_t hp = pids[j] * (int64_t)section;
			// . do not demote first occurence of word in a 
			//   low-scoring section, but only  demote successive 
			//   occurences in low-scoring sections. 
			//   This allows for headers in separate sections.
			// . get the special hash
			if ( tt2.getScore(&hw) > 1 || sw >= DW ) {
				m_ww[j] = (int32_t)(m_ww[j] * sw);
				// debug purposes
				if ( m_rvw ) m_rvw [i*MAX_RULES+6]*=(float)sw;
			}
			if ( tt2.getScore(&hp) > 1 || sw >= DW ) {
				m_pw[j] = (int32_t)(m_pw[j] * sw);
				// debug purposes
				if ( m_rvp ) m_rvp [i*MAX_RULES+6]*=(float)sw;
			}
			// add to the hash table
			if ( ! tt2.addTerm(&hw) ) return false;
			if ( ! tt2.addTerm(&hp) ) return false;
		}
		// start a new section with word #i
		sectionStart = i;
		// count words in this section
		sectionWordCount = 0;
		// accumulate their scores so we can average them
		sectionWeightSum = 0;
		// new fragment as well
		fragNum++;
		fragPos = 0;
		// new section
		section++;
		goto loop;
	}

	// . RULE #23
	// . do not index anything in these tags
	if ( inScript  ) goto loop;
	if ( inStyle   ) goto loop;
	if ( inMarquee ) goto loop;

	char *w    = wptrs[i];
	int32_t  wlen = wlens[i];

	//
	//  is the word a "punct word"? if so, wids[i] will be zero.
	// . 0.7 ms of the 7 ms
	//
	if ( ! wids[i] ) {
		// . these are similar to Bits.cpp functions
		// . this is now called above
		//setPunctWeights ( i , w , wlen );
		//m_pw[i] = getPunctPhraseWeight  (i,         w,wlen);
		// point to the word
		char *p ;
		char *wend     = w + wlen;
		char  hasComma = false;
		int32_t  nlcount  = 0;
		// . scan the chars in the word
		// . this might not work super well for unicode... :(
		for ( p = w ; p < wend ; p++ ) {
			// RULE #3
			if ( *p == '\n' ) { nlcount++   ; continue;}
			if ( *p == ' '  ) continue;
			//if ( ! specialTab[*p] ) continue;
			if ( *p == '('  ) { inParens = 1; pcount =0; continue;}
			if ( *p == '['  ) { inParens = 1; pcount =0; continue;}
			if ( *p == '{'  ) { inParens = 1; pcount =0; continue;}
			if ( *p == ')'  ) { inParens = 0; continue;}
			if ( *p == ']'  ) { inParens = 0; continue;}
			if ( *p == '}'  ) { inParens = 0; continue;}
			if ( *p == '\"' ) {
				if ( inQuotes ) { inQuotes = 0; qcount = 0; }
				else            { inQuotes = 1; qcount = 0; }
				continue;
			}
			// do not do single quote yet, it could be apostrophe
			if ( *p == '\"' ) { afterQuote = true; continue; }
			if ( *p == ','  ) { hasComma   = true; continue; }
			// . did we end a fragment? just check for a period 
			// . this should also check for !'s and ?'s
			if ( *p != '.' && *p != '!' && *p != '?' ) continue;
			// do not allow abbreviations to break sentences/frags
			if ( *w == '.' && i>0 && wids[i-1] &&isAbbr(wids[i-1]))
			     break;
			fragNum++;
			fragPos = 0;
		}
		// in pre-formatted text we have no tags, so consider
		// two new lines a breaking tag
		if ( inPre && nlcount >= 2 ) goto newSection;

		//
		// demote words/phrases in comma-separated lists
		//

		// . RULE #10
		// . if we have no comma get the next "word"
		if ( ! hasComma ) goto loop;
		// . if previous comma was nearby, tell all the words in
		//   between they are in a comma separated list
		// . TODO: reset these to 0 when section/fragment changes
		//         or when we hit a breaking tag
		if ( commaWordCount - lastCommaWordCount > 5 ) goto loop;
		// reduce the scores of all in between these two commas
		int32_t j = lastCommai - 2;
		if ( j < 0 ) j = 0;
		for ( ; j < i ; j++ ) {
			// only demote scores of alnum words
			if ( ! wids[j] ) continue;
			// do not hit it too hard since we also demote words
			// and phrase weights that are next to punctuation
			// words in the set2() function above
			m_ww[j] = (int32_t)(m_ww[j] * .75);
			//m_pw[j]=(int32_t)(m_ww[j] * .75);
			m_pw[j] = (int32_t)(m_pw[j] * .75);
			// debug purposes
			if ( m_rvw ) {
				m_rvw [j*MAX_RULES+10] *= (float).75;
				m_rvp [j*MAX_RULES+10] *= (float).75;
			}
		}
		// save the new comma state
		lastCommaWordCount = commaWordCount;
		lastCommai         = i;
		goto loop;
	}
	
	//
	// the word is an "alnum word"
	//

	// reset brcount
	brcount = 0;

	//if ( w[0]=='T' && w[1]=='h' && w[2]=='e' ) // && w[3]=='l' )
	//	log("hey");

	// assume a normal weight
	m_ww[i] = DW;

	// and this
	if ( m_rvw ) {
		// init all to 1.0
		float *start = &m_rvw[i*MAX_RULES];
		float *end   = &m_rvw[i*MAX_RULES+MAX_RULES];
		for ( ; start < end ; start++ ) *start = 1.0;
	}


	// . RULE #23
	// . minimize the weight if in a select tag
	if ( inSelect ) {
		m_ww[i] = 2;
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+23] = 2.0 / (float)DW;
	}
	

	// . RULE #3
	// . i don't like search results that have the matching words in a 
	//   parenthetical usually
	// . but only demote the first 40 words in ()'s, otherwise we might
	//   end up demoting everything because we have an open parentheses bug
	//   in the html source
	if ( inParens && pcount++ < 40 ) {
		m_ww[i] /= 4;
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+3] *= 0.25;
	}


	// . RULE #8
	// . or in a <ul> unnumbered list...
	// . actually if each bullet point in the list is beefy it is pretty
	//   good, so be careful...
	if ( inList ) {
		m_ww[i] = (int32_t)(m_ww[i] * 0.75);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+8] *= 0.75;
	}

	

	// in quotes is usually not that good, unless it is
	// a int32_t quotation!!! TODO
	//if ( inQuotes && qcount++ < 40 ) m_ww[i] /= 2;
	
	
	// . RULE #7
	// . in a header tag?
	// . only boost first 40 words, though

	// support version 85 and below for backwards compatability
	/*
	if ( inHeader && m_version <= 85 && hcount++ < 40 ) {	
                // <h1> tag
                if      ( inHeader == 1 ) m_ww[i] *= 3;
                // <h2> tag
                else if ( inHeader == 2 ) m_ww[i] *= 2;
                // <h3> tag
                else if ( inHeader == 3 ) m_ww[i] = (int32_t)(m_ww[i] * 1.5);
                // others
                else                      m_ww[i] = (int32_t)(m_ww[i] * 1.2);
        }
	*/



	if ( inHeader && m_version > 85 ) { // && hcount++ < 40 ) {
		// <h1> tag
		if      ( inHeader == 1 ) {
			if ( hdw > 100 ) m_ww[i] = (m_ww[i] * hdw) / 100;
			// debug purposes
			if ( m_rvw && hdw > 100 ) 
				m_rvw[i*MAX_RULES+7] *= (float)hdw/100.0;
		}
		// <h2> tag
		else if ( inHeader == 2 ) {
			if ( hdw > 150 ) m_ww[i] = (m_ww[i] * hdw) / 150;
			// debug purposes
			if ( m_rvw && hdw > 150 ) 
				m_rvw[i*MAX_RULES+7] *= (float)hdw/150.0;
		}
		// <h3> tag
		else if ( inHeader == 3 ) {
			if ( hdw > 200 ) m_ww[i] = (m_ww[i] * hdw) / 200;
			// debug purposes
			if ( m_rvw && hdw > 200 ) 
				m_rvw[i*MAX_RULES+7] *= (float)hdw/200.0;
		}
		// others
		else                      {
			if ( hdw > 300 ) m_ww[i] = (m_ww[i] * hdw) / 300;
			// debug purposes
			if ( m_rvw && hdw > 300 ) 
				m_rvw[i*MAX_RULES+7] *= (float)hdw/300.0;
		}
		// . degrade it by .8 each time
		// . but only start degrading the 6th term in title
		if ( ++hcount >= 30 ) hdw = (int32_t)((float)hdw * .90);
	}


	// . RULE #7
	// . titleWeight is a percentage
	if ( inTitle ) {

		// support old rule for backwards compatability
		//if ( m_version <= 85 && tcount++ < 20 ) 
		//	m_ww[i] = ( m_ww[i] * m_titleWeight ) / 100;

		// is it a title repeat?
		bool repeated = false;

		if ( m_titleWeight>100 && !titleRepeatTable.isEmpty(&wids[i])) 
			repeated=true;

		// only use this for newer versions, 89 and above
		//if ( m_version < 89 ) repeated = false;

		// only add to repeat table
		if ( tiw>100 && ! repeated ) 
			if ( ! titleRepeatTable.addKey (&wids[i])  ) 
				return false;

		//m_ww[i] = ( m_ww[i] * m_titleWeight ) / 100;
		// do not allow the same term to be repeated and receive the
		// title boost both times!
		if ( tiw > 100 && ! repeated ) {
			m_ww[i] = (m_ww[i] * tiw) / 100;
			// debug purposes
			if ( m_rvw ) m_rvw[i*MAX_RULES+7] *=((float)tiw)/100.0;
		}
		// . degrade it by .8 each time
		// . but only start degrading the 4th term in title
		if ( ++tcount >= 3 ) tiw = (int32_t)((float)tiw * .80);
		// do not weight for italics or bold now either
		goto skipBoldWeight;

		
	}


	// . RULE #5
	// . or in a link
	if ( inLink ) {
		//if  ( m_version <= 85 ) m_ww[i] /= 3;
		m_ww[i] /= 5;
		// save that
		if ( m_rvw ) m_rvw[i*MAX_RULES+5] *= .20;
		// inherit the weight here
		m_pw[i]  = m_ww[i]; 
		// debug purposes
		if ( m_rvw ) {
			gbmemcpy ( &m_rvp[i*MAX_RULES] , 
				 &m_rvw[i*MAX_RULES] ,
				 MAX_RULES * sizeof(float) );
		}
		// reset this, it is only for immediate quotes
		afterQuote = false;
		fragPos++;
		goto loop;
	}
	
	
	// . RULE #7
	// . use same count, bcount, for both these
	if      ( inBold   && bcount++ < 20 ) {
		m_ww[i] = (int32_t)(m_ww[i] * 1.5);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+7] *= 1.5;
	}
	else if ( inItalic && bcount++ < 20 ) {
		m_ww[i] = (int32_t)(m_ww[i] * 1.5);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+7] *= 1.5;
	}

	// apply body weight here

 skipBoldWeight:

	// . RULE #24
	// . if we are following a number, demote us
	// . TODO: make this a numbered list detector!! more accurate.
	if ( afterNum && ! is_digit(wptrs[i][0]) ) {
		m_ww[i] = (int32_t)(m_ww[i] * 0.5);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+24] *= 0.5;
	}
	// are we a number?
	if ( is_digit(wptrs[i][0]) ) afterNum = true;
	else                         afterNum = false;
	// if a number follow us, demote us, too
	if ( i+2<m_nw && is_digit(wptrs[i+2][0]) ) {
		m_ww[i] = (int32_t)(m_ww[i] * 0.5);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+24] *= 0.5;
	}


	// . RULE #13
	// . is previous occurence in a different section?
	// . this is -1 if no previous occurence
	int32_t lastSection = tt3.getScore ( &wids[i] );
	// . if this is the first occurence ever, boost it
	// . if the last occurence was not this section, boost it
	// . this rewards uniformly distributed words.
	if ( lastSection != section ) {
		m_ww[i] = (int32_t)(m_ww[i]*1.3);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+13] *= 1.3;
		// subtract lastSection from the score since TermTable will 
		// accumulate  the score
		if (!tt3.addTerm(&wids[i],section-lastSection))
			return false;
	}

	// . RULE #16
	// . if at beginning of sentence/fragment, boost
	if ( fragPos < 50 ) {
		m_ww[i] = (int32_t)(m_ww[i] * ((200.0 - (float)fragPos)/150.0));
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+16] *= 
				     ((200.0 - (float)fragPos)/150.0);
	}
	//if      ( fragPos <= 4  ) m_ww[i] = (int32_t)(m_ww[i] * 1.30);
	//else if ( fragPos <= 7  ) m_ww[i] = (int32_t)(m_ww[i] * 1.20);
	//else if ( fragPos <= 13 ) m_ww[i] = (int32_t)(m_ww[i] * 1.10);


	// . RULE #4
	// . if word adjacent to "bad" punct, demote it
	//if ( i  >0  && !wids[i-1] && !tids[i-1] && !bits->canPairAcross(i-1))
	//	m_ww[i] = (int32_t)(m_ww[i] * .75);
	//if ( i+1<nw && !wids[i+1] && !tids[i+1] && !bits->canPairAcross(i+1))
	//	m_ww[i] = (int32_t)(m_ww[i] * .75);
	
	//
	// phrase weight inherits word weight at this point
	//
	m_pw[i] = m_ww[i];
	// debug purposes
	if ( m_rvw ) 
		gbmemcpy ( &m_rvp[i*MAX_RULES] , 
			 &m_rvw[i*MAX_RULES] ,
			 MAX_RULES * sizeof(float) );

	// . RULE #9
	// . if this is NOT the first time this word has occurred in this
	//   particular fragment/sentence, then quarter its weight
	// . int64_t h = hash64 ( wids[i] , fragNum );
	int64_t h = wids[i] * (int64_t)fragNum;
	if ( tt4.getScore(&h)) {
		m_ww[i] = (int32_t)(m_ww[i]*.25);
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+9] *= .25;
	}
	if ( ! tt4.addTerm ( &h ) ) return false;
	if ( pids[i] ) {
		h = pids[i] * (int64_t)fragNum;
		if ( tt4.getScore(&h)) {
			m_pw[i] = (int32_t)(m_pw[i]*.25);
			if ( m_rvw ) m_rvp[i*MAX_RULES+9] *= .25;
		}
		if ( ! tt4.addTerm ( &h ) ) return false;
	}

	
	// . keep track of the average word weight for this section
	// . do not count weight influencers below since they are mostly
	//   phrase specific
	if ( ! inLink ) {
		sectionWordCount++;
		sectionWeightSum += m_ww[i];
	}

	// does word have a hyphen immediately on left or right of it?
	char leftHyphen  = false;
	char rightHyphen = false;
	char promoted    = false;
	if (i-1>=0   && wptrs[i-1][0]=='-' && wlens[i-1]==charLen )
		leftHyphen = true;
	if (i+1<m_nw && wptrs[i+1][0]=='-' && wlens[i+1]==charLen )
		rightHyphen = true;

	// . are we capitalized?
	// . only alnum words can be capitalized
	// . returns false if any letter besides the first is capitalized
	char iscap = bits->isCap(i) ;

	// is word to the left  capitalized?
	char iscap1 = 0;
	// is word to the right capitalized?
	char iscap2 = 0;
	// do not set if not well connected (phrase weight is low)
	if ( i-2 >= 0   && m_pw[i-1] >= DW && wids[i-2] ) 
		iscap1 = bits->isCap(i-2);
	// m_pw[i+1] should have been set at the top of set1()
	if ( i+2 < m_nw && m_pw[i+1] >= DW && wids[i+2] )
		iscap2 = bits->isCap(i+2);

	// RULE #11
	// demote word if immediate left word if it is capitalized but so
	// is the word on the immediate left or right
	if ( iscap && (iscap1 || iscap2) ) {
		m_ww[i] /= 2;
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+11] *= .50;
	}

	// . RULE #11
	// . if an "isolated" cap promote the word 
	// . only do this if NOT the beginning of a sentence, it
	//   has to have a space and then word on the immediate left 
	else if ( iscap && i-2>=0 &&wids[i-2] && wlens[i-1]==1) {
		m_ww[i] *= 2;
		// debug purposes
		if ( m_rvw ) m_rvw[i*MAX_RULES+11] *= 2.0;
	}


	// . RULE #18
	// . if the phrase this word starts is capitalized and
	//   so is the ending word of the phrase, promote the phrase.
	// . if word has hyphen to right, do not demote.
	// . if phrase contains junk like commas, it will be demoted
	//   via RULE #4 
	int32_t nwp = phrases->getNumWordsInPhrase(i);
	if ( iscap && pids[i] && nwp>0 && bits->isCap(i + nwp - 1) ) {
		m_pw[i] = (int32_t)(m_pw[i] * 2.0);
		if ( m_rvw ) m_rvp[i*MAX_RULES+18] *= 2.0;
		promoted = true;
	}
	// . RULE #18
	// . if phrase is not all in caps, and not after a quote, and does
	//   not contain a hyphen after the first word, demote it
	else if ( ! afterQuote && ! rightHyphen ) {
		// only demote if phrase occured only once or twice
		int32_t count = m_countTablePtr->getScore ( &pids[i] );
		if      ( count <= 1 ) {
			m_pw[i] = (int32_t)(m_pw[i] * 0.1);
			if ( m_rvw ) m_rvp[i*MAX_RULES+18] *= 0.1;
		}
		else if ( count == 2 ) {
			m_pw[i] = (int32_t)(m_pw[i] * 0.4);
			if ( m_rvw ) m_rvp[i*MAX_RULES+18] *= 0.4;
		}
	}


	// . RULE #17
	// . if this phrase is immediately preceeded by double quote then
	//   promote it. but only if not promoted from rule #18 above.
	if ( afterQuote && ! promoted ) {
		m_pw[i] = (int32_t)(m_pw[i] * 2.0);
		if ( m_rvw ) m_rvp[i*MAX_RULES+17] *= 2.0;
	}

	// reset this, it is only for immediate quotes
	afterQuote = false;

	// RULE #25
	// if one of the boundary words in a phrase is next to one and only one
	// hyphen, and that hyphen is not in the phrase, then demote the 
	// phrase. if we have a phrase like like "spam-free email" we should 
	// demote the phrase "free email". it splits a STRONG_CONNECTOR.
	// '22-year-old man' should demote "old man".
	if ( leftHyphen && ! rightHyphen ) {
		m_pw[i] = (int32_t)(m_pw[i] * 0.25);
		if ( m_rvw ) m_rvp[i*MAX_RULES+25] *= 0.25;
	}

	// is word part of a common phrase? like "san francisco"?
	// we have a file of these commonPhrases.dat
	//if ( isInCommonPhrase(i) ) {
	//	m_ww[i] /= 3;
	//	m_pw[i] *= 3;
	//}

	// TODO: REMOVE REPEATED FRAGMENTS SO THE BELOW CODE IS BETTER!!
	// we are getting the headline repeated...


	// do not demote all the way to zero, we still want to index it
	// and when normalized on a 100 point scale, like when printed
	// out by PageParser.cpp, a score of 1 here gets normalized to
	// 0, so make sure it is at least 2.
	if ( m_ww[i] < 2 ) m_ww[i] = 2;

	//if ( m_pw[i] > m_ww[i] )
	//	log("hey");

	//if ( isInSpammedFrag(i) ) {
	//	m_ww[i] = 0;
	//	m_pw[i] = 0;
	//}

	// this is the alnum word # in the current sentence fragment
	fragPos++;

	goto loop;
}

// . multiply the phrase score by the minimum phrase connector modifier of
//   all punct words in the phrase
// . phrases that contain certain sequences of punctuation can have their
//   scores lowered all the way to 0, and sometimes even boosted
// . normal modifier is 128, to keep things fast and float-free
/*
int32_t Weights::getPunctPhraseWeight ( int32_t i , char *s , int32_t len ) {

	if ( len != 2 ) goto tryLen1;

	// a comma is not usually a good thing to have in a phrase
	if ( s[0]==',' && (s[1]=='\n' || s[1]==' ')) return DW/8;

	// this is a url path and is not usually good either
	if ( s[0]=='/' && s[1]=='~') return DW/8;

	// double spaces, are ok, but not as good as a single space
	if ( is_space(s[0]) && is_space(s[1]) ) return DW;//(int32_t)(DW*1.8);

	if ( s[0]=='.' && is_space(s[1]) && i+1 < m_nw && m_wids[i+1] ) {
		if ( i == 0 ) return 0;
		if ( isAbbr ( m_wids[i-1] ) ) return DW;
		return 1;
	}

	// a punct follow by a space:
	if ( is_space(s[1]) && is_punct(s[0]) ) {
		switch ((unsigned char)s[1]) {
		case '?': return 0;
		case '!': return 0;
		case ';': return 0;
		case '<': return 0;
		case '>': return 0;
		case 171: return 0;  // <<  left shift operator 
		case 187: return 0;  // >>  right shift operator
		case 191: return 0;  // upsidedown question mark
		case 161: return 0;  // upsidedown exclamation point
		case '(': return 1;  // parens
		case ')': return 1;  // parens
		case '[': return 1;  // parens
		case ']': return 1;  // parens
		case '{': return 1;  // parens
		case '}': return 1;  // parens
		case ',': return DW/3; // ususally bad, but could be "Abq, NM"
		default : return DW/8;
		}
	}
	if ( is_space(s[0]) && is_punct(s[1]) ) {
		switch ((unsigned char)s[1]) {
		case '?': return 0;
		case '!': return 0;
		case ';': return 0;
		case '<': return 0;
		case '>': return 0;
		case '.': return DW/16; // the .exe file extension
		case 171: return 0;  // <<  left shift operator 
		case 187: return 0;  // >>  right shift operator
		case 191: return 0;  // upsidedown question mark
		case 161: return 0;  // upsidedown exclamation point
		case '(': return 1;  // parens
		case ')': return 1;  // parens
		case '[': return 1;  // parens
		case ']': return 1;  // parens
		case '{': return 1;  // parens
		case '}': return 1;  // parens
		case '$': return DW; 
		default : return DW/8;
		}
	}
	return 0;

 tryLen1:

	if (len != 1) goto tryLen3;
	switch ((unsigned char)s[0]) {
	case '?' : return 0;
	case '!' : return 0;
	case ';' : return 0;
	case '{' : return 0;
	case '}' : return 0;
	case '<' : return 0;
	case '>' : return 0;
	case '.' : return DW;
	case ',' : return (int32_t)(DW*0.25);
	case '_' : return (int32_t)(DW*0.25);
	case '@' : return (int32_t)(DW*0.25);
	case '&' : return (int32_t)(DW*0.80);
	case '\t': return (int32_t)(DW*0.80);
	case '\n': return DW;
	case '\r': return DW;
	case ' ' : return DW;
	case '\'': return (int32_t)(DW*1.5); // tom's
	case '-' : return (int32_t)(DW*2.00);
	case 171 : return 0;
	case 187 : return 0;
	case 191 : return 0;
	case 161 : return 0;
	default  : return (int32_t)(DW*0.25);
	}

 tryLen3:

	// pair across any number of spaces, it will only show up as one
	// space in html and Microsoft Front Page separates lines by a 
	// bunch of spaces
	if ( is_space(s[0]) && is_space(s[1]) && is_space(s[2]) ) {
		int32_t k = 3;
		while ( k < len ) if ( ! is_space(s[k++]) ) return 0;
		return DW;
	}

	// any other sequence of 4 or more punct chars we do not phrase across
	if (len != 3) return 0;

	// "://" is indicative of a url
	if (s[0]==':' && s[1]=='/'&&s[2]=='/') return (int32_t)(DW*.025);

	// we can pair across:
	// "://"
	// " , "
	// " - "
	// " & "
	// " + "
	if ( is_space(s[0]) && is_space(s[2]) ) 
		switch (s[1]) {
		case ',': return DW/8;
		case '-': return DW/8;
		case '+': return DW/8;
		case '&': return DW;
		}
	return 0;
}
*/

struct PunctWeight {
	float  m_wordWeightFloat;
	float  m_phraseWeightFloat;
	char  *m_str;
	int32_t   m_hash;
	int32_t   m_wordWeight;
	int32_t   m_phraseWeight;
};

// . each PunctWeight contains both a word and phrase weight
// . here are all the sequences of punct that do not have a weight of DW/8,
//   which is the default weight assigned below when nothing matches
// . we add a space to the end of the punct word if a tag immediately follows 
//   it, so when it is hashed, it is hashed as if a space follows it
// . a single space represents any number of \t or \n or space chars
// . before hashing your punct word to look it up in this table, we condense
//   any sequence of spaces (\t \n or space) into a single space in the
//   setPunctWeight()'s hash routine
// . make another table for version incrementing?
// . upsidedown ? and ! are hashed just as a ? or ! is
static PunctWeight s_punctWeights[] = {
	// 1 char
	{1.0, 1.00, " "    ,0,0,0},
	{0.2, 1.10, "-"    ,0,0,0}, // cd-rom, i-pod
	{0.2, 1.00, "."    ,0,0,0}, // www.ibm
	{0.2, 1.00, "&"    ,0,0,0}, // m&m
	{1.0, 1.00, "\'"   ,0,0,0}, // tom's
	{0.2, 0.20, "/"    ,0,0,0}, // this/that
	{0.8, 0.10, ","    ,0,0,0}, // meta keywords?
	// 2 chars
	{1.0, 0.00, ". "   ,0,0,0}, // mr. tom (assume not abbr here)
	// do not punish word weight for ", ", it could be 
	// "kanoodle, an ad company, ..."
	// and we punish for comma-separated lists above...
	// also, it could be "Abq, NM"
	{1.0, 0.20, ", "   ,0,0,0},
	{0.8, 0.20, " ,"   ,0,0,0}, // probably a typo for ", "
	{0.2, 0.16, "/~"   ,0,0,0}, // usually bad, url path component
	{1.0, 0.00, "? "   ,0,0,0}, // hello? anyone
	{1.0, 0.10, "! "   ,0,0,0}, // yahoo! games
	{1.0, 0.00, ": "   ,0,0,0},
	{1.0, 0.00, "; "   ,0,0,0},
	{1.0, 0.00, "< "   ,0,0,0},
	{1.0, 0.00, "> "   ,0,0,0},
	{1.0, 0.00, " <"   ,0,0,0},
	{1.0, 0.00, " >"   ,0,0,0},
	{1.0, 0.00, "( "   ,0,0,0},
	{1.0, 0.00, ") "   ,0,0,0},
	{1.0, 0.00, " ("   ,0,0,0},
	{1.0, 0.00, " )"   ,0,0,0},
	{1.0, 0.00, "[ "   ,0,0,0},
	{1.0, 0.00, "] "   ,0,0,0},
	{1.0, 0.00, " ["   ,0,0,0},
	{1.0, 0.00, " ]"   ,0,0,0},
	{1.0, 0.00, "{ "   ,0,0,0},
	{1.0, 0.00, "} "   ,0,0,0},
	{1.0, 0.00, " {"   ,0,0,0},
	{1.0, 0.00, " }"   ,0,0,0},
	{1.0, 1.00, " $"   ,0,0,0},
	{1.0, 1.00, " %"   ,0,0,0},
	{1.0, 0.00, "\" "  ,0,0,0},
	{1.0, 0.00, " \""  ,0,0,0},
	{1.0, 1.00, "\' "  ,0,0,0}, // plural possessive or single quote
	{1.0, 0.00, " \'"  ,0,0,0}, // single quote
	// 3 chars
	{1.0, 0.30, " , "  ,0,0,0}, // like ", "
	{1.0, 0.05, " : "  ,0,0,0},
	{1.0, 0.00, " ( "  ,0,0,0},
	{1.0, 0.00, " ) "  ,0,0,0},
	{1.0, 0.00, " [ "  ,0,0,0},
	{1.0, 0.00, " ] "  ,0,0,0},
	{1.0, 0.00, " { "  ,0,0,0},
	{1.0, 0.00, " } "  ,0,0,0},
	{0.5, 1.00, " & "  ,0,0,0}, // M & M
	{1.0, 0.10, " - "  ,0,0,0},
	{1.0, 0.00, ", \"" ,0,0,0},
	{1.0, 0.00, ", \'" ,0,0,0},
	{1.0, 0.00, "\"! " ,0,0,0},
	{1.0, 0.00, "\"? " ,0,0,0},
	{1.0, 0.00, "\": " ,0,0,0},
	// 4 chars
	{1.0, 0.00, " -- " ,0,0,0},
	{1.0, 0.00, "... " ,0,0,0},
	// 5 chars
	{1.0, 0.00, " ... ",0,0,0},
	{1.0, 0.00, ".... ",0,0,0},
	// special
	{1.0, 1.00, "&nbsp;" ,0,0,0},
	{1.0, 1.00, "&nbsp; ",0,0,0} // can have tag right after it
};

bool initPunctWeights ( ) {

	PunctWeight *pws = s_punctWeights;
	int32_t         npw = sizeof(s_punctWeights) / sizeof(PunctWeight);

	if ( ! s_punctTable.set ( 1024 , NULL , 0 ) ) return false;

	for ( int32_t i = 0 ; i < npw ; i++ ) {
		PunctWeight *PW = &pws[i];
		// set hash
		//int32_t len = gbstrlen ( PW->m_str       );
		int32_t h   = hash32n ( PW->m_str );//, len );
		// store the hash
		PW->m_hash = h;
		// set int32_t weights from float weights
		int32_t xww = (int32_t)(PW->m_wordWeightFloat   * DW);
		int32_t xpw = (int32_t)(PW->m_phraseWeightFloat * DW);
		// ensure minimums, do not go all the way to 0 because we
		// want to still index a phrase or word with minimal score
		// just in case someone searches for it. it will come up, but
		// at the very bottom of the results.
		if ( xww == 0 ) xww = 2;
		if ( xpw == 0 ) xpw = 2;
		// assign
		PW->m_wordWeight   = xww;
		PW->m_phraseWeight = xpw;
		// add this PunctWeight class to the hash table
		if ( ! s_punctTable.addKey ( h , (int32_t)PW ) ) return false;
	}
	return true;
}

// . set m_ww[k] and m_pw[k] for a punctuation word which is a sequence of 
//   non-alnum chars (as defined in Words.cpp)
// . caller can divide the returned weight by DW to make it a proper percentage
void Weights::setPunctWeights ( int32_t k , char *s , int32_t len ) {
	// convert the punct word into a normalized hash for look-up
	unsigned char c;
	int32_t          j = 0;
	uint32_t h = 0;
	char          lastSpace = false;

	uint8_t *p    = (uint8_t *)s;
	uint8_t *pend = p + len;
	char     cs ;

	for ( ; p < pend ; p += cs ) {
		// get char size in bytes
		cs = getUtf8CharSize ( p );
		// assume ascii i guess
		c = *p;
		// normalize it
		if ( is_wspace_utf8 (p) ) c = ' ';
		// assume all all other multi byte punct is bad
		else if ( cs >= 2 ) break;
		// . normalize upside down ? and ! to ? and !
		// . see http://www.utf8-chartable.de/ table latin1 to utf8
		if ( p[0]==0xc2 && p[1]==0xbf ) c = '?' ; // inverted qmark
		if ( p[0]==0xc2 && p[1]==0xa1 ) c = '?' ; // inverted excl mark
		if ( p[0]==0xc2 && p[1]==0xb4 ) c = '\''; // grave mark
		// if we are space and last was a space, skip
		if ( c == ' ' && lastSpace ) continue;
		if ( c == ' ' ) lastSpace = true;
		else            lastSpace = false;
		// . incorporate it into hash
		// . taken from hash32() in hash.cpp so it matches with the
		//   hash32() function in initPunctWeights() above
		h ^= (uint32_t) g_hashtab
			[(unsigned char)j]
			[(unsigned char)c] ;
		// advance j for next char
		j++;
	}

	// tag follows? if so, append a space to hash if does not end in one
	if ( k+1<m_nw && s[len] == '<' && ! lastSpace )
		h ^= (uint32_t) g_hashtab
			[(unsigned char)j++]
			[(unsigned char)' '] ;

	// lookup in the hash table, try to get the PunctWeight
	PunctWeight *PW = NULL;

	// only consult table if we had no unrecognized utf8 punct
	if ( p >= pend ) PW = (PunctWeight *) s_punctTable.getValue ( h );

	// if not there, use minimal weights
	if ( ! PW ) {
		// just make a really low weight by default, but not 0, just
		// to be on the safe side
		m_ww[k] = DW/8;
		m_pw[k] = DW/8;
		// TODO: put in code for rvw and rvp????
		// debug, see if we are missing anything special
		/*
		char dbuf[256];
		int32_t dlen = len;
		if ( dlen > 100 ) dlen = 100;
		gbmemcpy ( dbuf , s , dlen );
		dbuf[dlen]=0;
		logf(LOG_DEBUG,"build: missed \"%s\"",dbuf);
		*/
		return;
	}

	// was it an abbreviation? that is a special case
	if ( PW->m_str[0]=='.' && PW->m_str[1]==' ' && PW->m_str[2]=='\0' &&
	     isAbbr(m_wids[k-1]) ) {
		// give normal weights if so
		m_ww[k] = DW;
		m_pw[k] = DW;
		return;
	}

	// otherwise grab the weight for this known punctuation sequence
	m_ww[k] = PW->m_wordWeight;
	m_pw[k] = PW->m_phraseWeight;

	return;
}

/*
float Weights::getPunctWordWeight ( char *s , int32_t len ) {


	// must be a tag right after, or possible EOF
	if ( len == 0 ) return 1.0;

	if ( len != 2 ) goto tryLen1;

 tryLen2:

	if ( is_space(s[0]) && is_space(s[1]) ) return 1.0;

	if ( is_space(s[1]) ) {
	skippy:
		switch ((unsigned char)s[0]) {
		case '?': return 1.0;
		case '!': return 1.0;
		case ';': return 1.0;
		case '(': return 1.0;
		case ')': return 1.0;
		case '{': return 1.0;
		case '}': return 1.0;
		case '<': return 1.0;
		case '>': return 1.0;
		case '.': return 1.0;
		case '\"': return 1.0;
		case '\'': return 1.0;
		case 191: return 1.0;  // upsidedown question mark
		case 161: return 1.0;  // upsidedown exclamation point
		default : return 0.5;
		}
	}

	if ( is_space(s[0]) ) {
		switch ((unsigned char)s[1]) {
		case '?': return 0.8;
		case '!': return 0.8;
		case ';': return 0.8;
		case '(': return 1.0;
		case ')': return 1.0;
		case '{': return 1.0;
		case '}': return 1.0;
		case '<': return 1.0;
		case '>': return 1.0;
		case '.': return 0.8;
		case '\"': return 1.0;
		case '\'': return 1.0;
		case 191: return 1.0;  // upsidedown question mark
		case 161: return 1.0;  // upsidedown exclamation point
		default : return 0.5;
		}
	}
	return 0.5;

 tryLen1:

	if ( len != 1 ) goto tryLen3;

	// this is probably the most common case
	if ( is_space(s[0]) ) return 1.0;

	if ( s[0]=='-' ) return  0.5;

	// if a tag follows, treat as a space
	if ( s[1] == '<' ) goto skippy;

	// apostrophe is ok
	if ( s[1] == '\'' ) return 1.0;

	// otherwise, a word follows, so <word><punctChar><word> pretty
	// much always is a low weight for the word
	return 0.5;

 tryLen3:

	// if first char is a legit ending of a sentence, skip that
	// if .!?!? followed by string of spaces
	char skipped = false;
	if ( s[0] == '.' ||
	     s[0] == '?' ||
	     s[0] == '!' ||
	     s[0] == ':' ||
	     (unsigned char)s[0] == 191 ||   // upsidedown question mark
	     (unsigned char)s[0] == 161  ) { // upsidedown exclamation point
		if ( len == 2 ) {
			if ( ! is_space(s[1]) ) return 0.5;
			if ( ! is_space(s[2]) ) return 0.5;
			return 1.0;
		}
		s++;
		len--;
		skipped = true;
	}

	// pair across any number of spaces, it will only show up as one
	// space in html and Microsoft Front Page separates lines by a 
	// bunch of spaces
	if ( is_space(s[0]) && is_space(s[1]) && is_space(s[2]) ) {
		int32_t k = 3;
		// a int32_t string of more than spaces sucks...
		while ( k < len ) if ( ! is_space(s[k++] ) ) return 0.3;
		// it was all spaces, do not hurt it
		return 1.0;
	}
	     
	// if we skipped a starting .?! no need to go any further
	if ( skipped  ) return 0.5;

	// a glorified breaking hyphen
	if ( len == 4 &&
	     is_space(s[0]) &&
	     s[1] == '-' &&
	     s[2] == '-' &&
	     is_space(s[3]) ) return 1.0;

	// ...
	if ( len == 4 &&
	     s[0] = '.' &&
	     s[1] = '.' &&
	     s[2] = '.' &&
	     is_space(s[3]) ) return 1.0;

	if ( len != 3 ) return 0.5;

	if ( s[0] = ',' &&
	     s[1] == '\"' &&
	     is_space(s[2] ) return 1.0;

	if ( is_space(s[0]) && is_space(s[2]) ) 
		switch (s[1]) {
		case ',': return 0.8;
		case '&': return 0.7; // better as a phrase
		}
	return 0.5;
}
*/

/*
//
//
// BEGIN OLD SPAM.CPP class
//
//

#define WTMPBUFSIZE (MAX_WORDS *21*3)

// . RULE #28, repetitive word/phrase spam detector
// . set's the "spam" member of each word from 0(no spam) to 100(100% spam)
// . "bits" describe each word in phrasing terminology
// . if more than maxPercent of the words are spammed to some degree then we
//   consider all of the words to be spammed, and give each word the minimum
//   score possible when indexing the document.
// . returns false and sets g_errno on error
bool Weights::set4 ( ) {

	// assume not the repeat spammer
	m_isRepeatSpammer = false;

	if ( m_isLinkText   ) return true;
	if ( m_isCountTable ) return true;

	// int16_tcuts
	Words *words = m_words;
	Bits  *bits  = m_bits;

	// if 20 words totally spammed, call it all spam?
	m_numRepeatSpam = 20;

	// int16_tcut
	int32_t sni = m_siteNumInlinks;

	// set "m_maxPercent"
	int32_t maxPercent = 6;
	if ( sni > 10  ) maxPercent = 8;
        if ( sni > 30  ) maxPercent = 10;
        if ( sni > 100 ) maxPercent = 20;
        if ( sni > 500 ) maxPercent = 30;

	// assume not totally spammed
	m_totallySpammed = false;
	// get # of words we have to set spam for
	int32_t numWords = words->getNumWords();

	// set up the size of the hash table (number of buckets)
	int32_t  size = numWords * 3;

	// . add a tmp buf as a scratch pad -- will be freed right after
	// . allocate this second to avoid mem fragmentation more
	// . * 2 for double the buckets
	char  tmpBuf [ WTMPBUFSIZE ];
	char *tmp     = tmpBuf;
	int32_t  need    = (numWords * 21) * 3 + numWords;
	if ( need > WTMPBUFSIZE ) {
		tmp = (char *) mmalloc ( need , "Spam" );
		if ( ! tmp ) 
			return log("build: Failed to allocate %"INT32" more "
				   "bytes for spam detection:  %s.",
				   need,mstrerror(g_errno));
	}

	QUICKPOLL(m_niceness);
	// set up ptrs
	char *p = tmp;
	// first this
	unsigned char *spam      = (unsigned char *)p; p += numWords ;
	// . this allows us to make linked lists of indices of words
	// . i.e. next[13] = 23--> word #23 FOLLOWS word #13 in the linked list
	int32_t      *next          = (int32_t      *)p;  p += size * 4;  
	// hash of this word's stem (or word itself if useStem if false)
	int64_t *bucketHash    = (int64_t *)p;  p += size * 8;
	// that word's position in document
	int32_t      *bucketWordPos = (int32_t      *)p;  p += size * 4;
	// profile of a word
	int32_t      *profile       = (int32_t      *)p;  p += size * 4;
	// is it a common word?
	char      *commonWords   = (char      *)p;  p += size * 1;

	// sanity check
	if ( p - tmp > need ) { char *xx=NULL;*xx=0; }

	// clear all our spam percentages for these words
	memset ( spam , 0 , numWords );

	int32_t np;
        // clear the hash table
        int32_t i;
        for ( i = 0 ; i < size ; i++ ) {
                bucketHash   [i] =  0;
                bucketWordPos[i] = -1;
		commonWords  [i] =  0;
        }

	// count position since Words class can now have tags in it
	//
	//int32_t pos = 0;
	//bool usePos = false;
	//if ( words->m_tagIds ) usePos = true;

	int64_t *wids = words->getWordIds();

	// . loop through each word 
	// . hash their stems and place in linked list
	// . if no stemming then don't do stemming
	for ( i = 0 ; i < numWords ; i++ ) {
		// . skip punctuation 
		// . this includes tags now , too i guess
		//if ( words->isPunct(i) ) continue;
		if ( wids[i] == 0 ) continue;
		// skip if will not be indexed cuz score is too low
		//if ( wscores && wscores[i] <= 0 ) continue;
		QUICKPOLL(m_niceness);
		// TODO: get phrase stem if stemming is on
		// store the phrase stem this word int32_to the buffer
		//		blen = words->getPhraseStem(i,buf,100);
		//		if (blen<=0) continue;
		// get the hash of the ith word
		int64_t h = words->getWordId(i);
		// use secondary wordId if available
		//if ( words->getStripWordId(i) ) 
		//	h = words->getStripWordId(i);
		// "j" is the bucket index
		int32_t j = (uint64_t)h % size;
		// make sure j points to the right bucket
		while (bucketHash[j]) {
			if ( h == bucketHash[j] ) break;
			if (++j == size) j = 0;
		}
		// if this bucket is occupied by a word then replace it but 
		// make sure it adds onto the "linked list"
		if (bucketHash[j])  { 
			// if Words class contain tags as words, do this
			//if ( usePos ) {
			//	next         [pos] = bucketWordPos[j];
			//	bucketWordPos[  j] = pos++;
			//}
			//else {
			// add onto linked list for the ith word
			next[i]  = bucketWordPos[j]; 
			// replace bucket with index to this word
			bucketWordPos[j] = i;
			//}
		}
		// otherwise, we have a new occurence of this word
		else { 
			bucketHash  [j] = h;
			// if Words class contain tags as words, do this
			//if ( usePos ) {
			//	bucketWordPos[  j] = pos++;
			//	next         [pos] = -1;
			//}
			//else {
			// store our position # (i) in bucket
			bucketWordPos[j] = i;
			// no next occurence of the ith word yet
			next[i] = -1;
			//}
		}
		// if stop word or number then mark it
		if ( bits->isStopWord(i) ) commonWords[j] = 1;
		if ( words->isNum ( i )  ) commonWords[j] = 1;
	}
	// count distinct candidates that had spam and did not have spam
	int32_t spamWords = 0;
	int32_t goodWords = 0;
	// . now cruise down the hash table looking for filled buckets
	// . grab the linked list of indices and make a "profile"
	for ( i = 0 ; i < size ; i++ ) {
		// skip empty buckets
		if (bucketHash[i] == 0) continue; 
		np=0;
		// word #j is in bucket #i
		int32_t j = bucketWordPos[i];  
		// . cruise down the linked list for this word
		while ( j!=-1) {
			// store position of occurence of this word in profile
			profile [ np++ ] = j;
			// get the position of next occurence of this word
			j = next[ j ];  
		}
		// if 2 or less occurences of this word, don't check for spam
		if ( np < 3 ) { goodWords++; continue; }

		//
		// set m_isRepeatSpammer
		//
		// look for a word repeated in phrases, in a big list,
		// where each phrase is different
		//
		int32_t max = 0;
		int32_t count = 0;
		int32_t knp = np;
		// must be 3+ letters, not a stop word, not a number
		if ( words->m_wordLens[profile[0]] <= 2 || commonWords[i] )
			knp = 0;
		// scan to see if they are a tight list
		for ( int32_t k = 1 ; k < knp ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// are they close together? if not, bail
			if ( profile[k-1] - profile[k] >= 25 ) {
				count = 0;
				continue;
			}
			// otherwise inc it
			count++;
			// must have another word in between or tag
			int32_t a = profile[k];
			int32_t b = profile[k-1];
			bool gotSep = false;
			bool inLink = false;
			for ( int32_t j = a+1 ; j <b ; j++ ) {
				// if in link do not count, chinese spammer
				// does not have his crap in links
				if ( words->m_words[j][0] == '<' &&
				     words->m_wordLens[j]>=3 ) {
					// get the next char after the <
					char nc;
					nc=to_lower_a(words->m_words[j][1]);
					// now check it for anchor tag
					if ( nc == 'a' ) {
						inLink = true; break; }
				}
				if ( words->m_words[j][0] == '<' ) 
					gotSep = true; 
				if ( is_alnum_a(words->m_words[j][0]) ) 
					gotSep = true; 
			}	     
			// . the chinese spammer always has a separator, 
			//   usually another tag
			// . and fix "BOW BOW BOW..." which has no separators
			if      ( ! gotSep  ) count--;
			else if (   inLink  ) count--;
			// get the max
			if ( count > max ) max = count;
		}
		// a count of 50 such monsters indicates the chinese spammer
		if ( max >= 50 )
			m_isRepeatSpammer = true;
		//
		// end m_isRepeatSpammer detection
		//

		// . determine the probability this word was spammed by looking
		//   at the distribution of it's positions in the document
		// . sets "spam" member of each word in this profile
		// . don't check if word occurred 2 or less times
		// . TODO: what about TORA! TORA! TORA!
		// . returns true if 1+ occurences were considered spam
		QUICKPOLL(m_niceness);
		bool isSpam = setSpam ( profile , np , numWords , spam );
		// don't count stop words or numbers towards this threshold
		if ( commonWords[i] ) continue;
		// tally them up
		if ( isSpam ) spamWords++;
		else          goodWords++;
	}
	// what percent of distinct cadidate words were spammed?
	int32_t totalWords = spamWords + goodWords;
	// if no or ver few words return true
	int32_t percent;
	if ( totalWords <= 10 ) goto done;
	percent    = ( spamWords * 100 ) / totalWords;
	// if 20% of words we're spammed punish everybody now to 100% spam
	// if we had < 100 candidates and < 20% spam, don't bother
	//if ( percent < 5 ) goto done;
	if ( percent <= maxPercent ) goto done;
	// set flag so linkspam.cpp can see if all is spam and will not allow
	// this page to vote
	m_totallySpammed = true;
	// now only set to 99 so each singleton usually gets hashed
	for ( i = 0 ; i < numWords ; i++ ) 
		if ( words->getWordId(i) && spam[i] < 99 ) 
			spam[i] = 99;
 done:

	// update the weights for the words
	for ( i = 0 ; i < numWords ; i++ ) {
		m_ww[i] = ( m_ww[i] * (100 - spam[i]) ) / 100;
		if ( m_rvw ) m_rvw[i*MAX_RULES+28] *= (100 - spam[i]) / 100;
	}

	// TODO: use the min word spam algo as in Phrases.cpp for this!
	for ( i = 0 ; i < numWords ; i++ ) {
		m_pw[i] = ( m_pw[i] * (100 - spam[i]) ) / 100;
		if ( m_rvp ) m_rvp[i*MAX_RULES+28] *= (100 - spam[i]) / 100;
	}

	// free our temporary table stuff
	if ( tmp != tmpBuf ) mfree ( tmp , need , "Spam" );

	return true;
}


// . a "profile" is an array of all the positions of a word in the document
// . a "position" is just the word #, like first word, word #8, etc...
// . we map "each" subProfile to a probability of spam (from 0 to 100)
// . if the profile is really big we get really slow (O(n^2)) iterating through
//   many subProfiles
// . so after the first 25 words, it's automatically considered spam
// . return true if one word was spammed w/ probability > 20%
bool Weights::setSpam ( int32_t *profile, int32_t plen , int32_t numWords ,
			unsigned char *spam ) {
	// don't bother detecting spam if 2 or less occurences of the word
	if ( plen < 3 ) return false;
	int32_t i;
	// if we have more than 10 words and this word is 20% or more of 
	// them then all but the first occurence is spammed
	//log(LOG_INFO,"setSpam numRepeatSpam = %f", m_numRepeatSpam);
	if (numWords > 10 && (plen*100)/numWords >= m_numRepeatSpam) {
		for (i=1; i<plen; i++) spam[profile[i]] = 100;
		return true ;
	}
	// . over 50 repeated words is ludicrous
	// . set all past 50 to spam and continue detecting
	// . no, our doc length based weight takes care of that kind of thing
	//if (plen > 50 && m_version < 93 ) {
	//	// TODO: remember, profile[i] is in reverse order!! we should
	//	// really do i=0;i<plen-50, but this is obsolete anyway...
	//	for (i=50; i<plen;i++) m_spam[profile[i]] = 100;
	//	plen = 50;
	//}


	// we have to do this otherwise it takes FOREVER to do for plens in
	// the thousands, like i saw a plen of 8338!
	if ( plen > 50 ) { // && m_version >= 93 ) {
		// . set all but the last 50 to a spam of 100%
		// . the last 50 actually occur as the first 50 in the doc
		for (i=0; i<plen-50;i++) spam[profile[i]] = 100;
		// we now have only 50 occurences
		plen = 50;
		// we want to skip the first plen-50 because they actually
		// occur at the END of the document
		profile += plen - 50;
	}

	QUICKPOLL(m_niceness);
	// higher quality docs allow more "freebies", but only starting with
	// version 93... (see Titledb.h)
	// profile[i] is actually in reverse order so we subtract off from wlen
	//int32_t off ;
	//if ( m_version >= 93 ) {
	//	off = (m_docQuality - 30) / 3;
	//	if ( off < 0 ) off = 0;
	//}
	// just use 40% "quality"
	int32_t off = 3;

	// . now the nitty-gritty part
	// . compute all sub sequences of the profile
	// . similar to a compression scheme (wavelets?)
	// . TODO: word positions should count by two's since punctuation is
	//         not included so start step @ 2 instead of 1
	// . if "step" is 1 we look at every       word position in the profile
	// . if "step" is 2 we look at every other word position 
	// . if "step" is 3 we look at every 3rd   word position, etc...
	int32_t maxStep = plen / 4; 
	if ( maxStep > 4 ) maxStep = 4; 
	// . loop through all possible tuples
	int32_t window, wlen, step, prob;
	 for ( step = 1 ; step <= maxStep ; step++ ) { 
		for ( window = 0 ; window + 3 < plen ; window+=1) {
			for (wlen = 3; window+wlen <= plen ; wlen+=1) {
				// continue if step isn't aligned with window
				// length
				if (wlen % step != 0) continue;
				// . get probability that this tuple is spam
				// . returns 0 to 100
				prob = getProbSpam ( profile + window ,
						     wlen , step); 
				// printf("(%i,%i,%i)=%i\n",step,window,
				// wlen,prob);
				// . if the probability is too low continue
				// . was == 100
				if ( prob <= 20 ) continue;
				// set the spammed words spam to "prob"
				// only if it's bigger than their current spam
				for (i=window; i<window+wlen;i++) {
					// first occurences can have immunity 
					// due to doc quality being high
					if ( i >= plen - off ) break;
					if (spam[profile[i]] < prob)
						spam[profile[i]] = prob;
				}
				QUICKPOLL(m_niceness);
			}

		}
	 }
	 // was this word spammed at all?
	 bool hadSpam = false;
	 for (i=0;i<plen;i++) if ( spam[profile[i]] > 20 ) hadSpam = true;
	 // make sure at least one word survives
	 for (i=0;i<plen;i++) if ( spam[profile[i]] == 0)  return hadSpam;
	 // clear the spam level on this guy
	 spam[profile[0]] = 0;
	 // return true if we had spam, false if not
	 return hadSpam;
}
*/
