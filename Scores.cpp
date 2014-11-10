#include "gb-include.h"

#include "Scores.h"
#include "Words.h"

// . explicit article body indicator tags:
//   <div class=blogbody,storycontent,body,article_body,story-body
//    <div/td/span class=blogbody,storycontent,body,article_body,
//     story,body-content,entry,story-body,mainarttxt>
//   <td class=story>
//   <span class="body-content">
//   <div class="entry">  --- although has "entry" for ads, etc.
//   reuters: <!-- Article Text Begins -->  and Ends -->
// . forbes have a bunch of <span class=mainartext> strewn together. they
//   are neighbor sections.

#define MAX_LEVELS 200

Scores::Scores () {
	m_buf     = NULL;
	m_bufSize = 0;
	m_scores  = NULL;
	//m_rerankScores = NULL;
}

Scores::~Scores() {
	reset();
}

void Scores::reset() {
	if ( m_buf && m_needsFree ) // m_buf != m_localBuf )
		mfree ( m_buf , m_bufSize , "Scores" );
	m_buf    = NULL;
	m_scores = NULL;
}

bool Scores::set ( Words    *words             ,
		   Sections *sections          ,
		   int32_t      titleRecVersion   ,
		   bool      eliminateMenus    ,
		   // provide it with a buffer to prevent a malloc
		   char     *buf               ,
		   int32_t      bufSize           ,
		   int32_t      minIndexableWords ) {

	//int32_t  defaultm = 40;
	//if ( titleRecVersion >= 56 ) defaultm = -1;

	// "scoreBySection" (default is true)
	// Should gigablast break the document into sections and score the
	// words in sections with mostly link text lower than words in sections
	// without much link text? This helps to reduce the effects of menu 
	// spam.
	// Used for news articles.
	// This only applies to the body of the document.

	// "indexContentSectionOnly" (default is false)
	// Should gigablast attempt to isolate just the single most-relevant
	// content section from the document and not index anything else?
	// Used for news articles.
	// This only applies to the body of the document.

	// "minSectionScore" (default is -1000000000)
	// The minimum score an entire section of the document needs to have
	// its words indexed. Each word in a section counts as 128 points, but
	// a word in a hyperlink counts as -256 points.
	// Used for news articles.
	// This only applies to the body of the document.

	// "minIndexableWords" (default is -1)
	// If the number of indexable words that have a positive average score
	// is below this value, then no words will be indexed. Used
	// to just index beefy news articles. -1 means to ignore this 
	// constraint.

	// "minAvgWordScore" (default is 0)
	// Words have an average score of the 8 neighboring words on their left
	// and the 8 neighboring words on their right, in the same section. 
	// These word scores are 128 points for a word not in a link, and only
	// 21 points for a word in a link. What is the minimum score average 
	// score a word needs to be indexed? (Before applying the top word 
	// weight, below)
	// scoreBySection must be enabled for this to work.

	// "numTopWords" (default is 0)
	// Weight the first X words higher.
	// Used for news articles.
	// This only applies to the body of the document.

	// "topWordsWeight" (default is 1.0)
	// Weight the first X words by this much, a rational number.
	// Used for news articles.
	// This only applies to the body of the document.

	// "topSentenceWeight" (default is 1.0)
	// Weight the first sentence by this much, a rational number.
	// Only applies to documents that support western punctuation.
	// Used for news articles.
	// This only applies to the body of the document.

	// "maxWordsInSentence" (default is 0)
	// Do not weight more than this words in the first sentence.
	// Used for news articles.
	// This only applies to the body of the document.

	// if we are doing "menu elimination technology" then zero out
	// scores of terms not in the single content section
	if ( eliminateMenus ) 
		return set ( words          ,
			     titleRecVersion,
			     true           , // scoreBySection
			     true           , // indexContentSectionOnly (DIFF)
			     -1000000000    , // minSectionScore
			     0              , // minAvgWordScore
			     40             , // minIndexableWords (DIFF)
			     0              , // numTopWords
			     3.0            , // topWordsWeight
			     1.0            , // topSentenceWeight
			     30             );// maxWordsInSentence


	// use all defaults if no site rec
	//if ( ! sx )
	return set ( words             ,
		     titleRecVersion   ,
		     true              , // scoreBySection
		     false             , // indexContentSectionOnly
		     -1000000000       , // minSectionScore
		     0                 , // minAvgWordScore
		     minIndexableWords , // defaults to -1
		     0                 , // numTopWords
		     3.0               , // topWordsWeight
		     1.0               , // topSentenceWeight
		     30                );// maxWordsInSentence

	/*
	// there should only by one <index> block in the ruleset file that has 
	// these special config switches
	int32_t n0 = 0;
	int32_t n1 = 0x7fffffff;

	// this is used to decrease the scores of words in menu sections.
	// this means that words will be scored based on their neighboring
	// words in the same section of the document. the section of the 
	// document is determined by <table><div><tr><td> tags and the like.
	// if the neighboring words are in links then the score is decreased.
	// this way we expect to score words in menus less. this is now
	// default scoring behaviour for newer documents.
	bool scoreBySection = true;
	if ( ! sx->getBool(n0,n1,"index.scoreBySection",true) )
		scoreBySection = false;
	
	// this is used to index newspaper articles.
	// indexContentSectionOnly means to only index the words in the top-
	// scoring section of the document. the section of the document
	// is determined by <table><div><tr><td> tags and the like. the score
	// of a section is based on how many words that are not in hyperlinks
	// are contained in that section. words in hyperlinks actually decrease
	// the score of the section.
	bool indexContentSectionOnly =
		sx->getBool(n0,n1,"index.indexContentSectionOnly",false);
	//log("REMOVE ME");
	//indexContentSectionOnly = true;

	// if the total score of a section is less than this then no words
	// in that section will get indexed. each word in a section is
	// counted as 128 points, but if the word is in a hyper link it is
	// counted as -256 points (-2*128)
	int32_t minSectionScore = sx->getLong(n0,n1,"index.minSectionScore",
					   -1000000000);

	// count words in links as 21 points, words not in links as 128.
	// the average score of each word is its score plus the scores of
	// its 8 left and its 7 right neighbors divided by 16. if that
	// average score is below this value, the word is not indexed.
	// only valid if scoreBySection is true!
	int32_t minAvgWordScore = sx->getLong(n0,n1,"index.minAvgWordScore",0);

	// if the whole document has less than this many words with positive 
	// scores, do not index any of the words (set their scores to 0)
	int32_t  minIndexableWords =
		sx->getLong (n0,n1,"index.minIndexableWords",defaultm);//40);

	// . for weighting the top portion of the document more, use these.
	// . only applicable if using the new parser so we can use the new 
	//   Scores class
	int32_t  numTopWords        =
		sx->getLong (n0,n1,"index.numTopWords",0);
	float topWordsWeight     =
		sx->getFloat(n0,n1,"index.topWordsWeight",3.0);
	float topSentenceWeight  =
		sx->getFloat(n0,n1,"index.topSentenceWeight",1.0);
	int32_t  maxWordsInSentence =
		sx->getLong (n0,n1,"index.maxWordsInSentence",30);

	return set ( words                   ,
		     titleRecVersion         ,
		     scoreBySection          ,
		     indexContentSectionOnly ,
		     minSectionScore         ,
		     minAvgWordScore         ,
		     minIndexableWords       ,
		     // these are for weighting top part 
		     // of news articles
		     numTopWords             ,
		     topWordsWeight          ,
		     topSentenceWeight       ,
		     maxWordsInSentence      ) ;
	*/
}

// . returns false and sets g_errno on error
// . scores the words in the Words.cpp class, which is set from an Xml pointer
// . Words.cpp must contain tags cuz that's what we look at to divide the
//   words up into sections
// . most docs are divided up into sections based on div, and table/tr/td tags
// . look at each section independently and score words in each section based
//   on the density of words in hyperlinks in their vicinity.
// . if a particular section has a lot of hyperlinked text it should score
//   low, while a section of a lot of pure text should score high.
// . small sections with not much plain text, but no hyperlinks, will not score
//   very high either, usually they are like copyright notices and stuff,
//   although they could be a small message on a message board.
// . most sections really don't have many things embedded in them, with the
//   exception of the root section, so we can linearly scan each section,
//   skipping over the embedded sections, with decent speed and compute the
//   score of each word on an individual basis.
// . sets m_wscores[i] to word #i's score weight.
// . if n1 is non-NULL we set the scores of all words that are not in the 
//   top-scoring section to 0 or -1. this is used for just indexing simple 
//   news articles which are mostly just contained in a single section.
// . if we have less than minIndexableWords positive scoring words, then do
//   not index any words, set their scores to 0
bool Scores::set ( Words    *words                   ,
		   Sections *sections                ,
		   int32_t      titleRecVersion         ,
		   bool      scoreBySection          ,
		   bool      indexContentSectionOnly ,
		   int32_t      minSectionScore         ,
		   int32_t      minAvgWordScore         ,
		   int32_t      minIndexableWords       ,
		   // these are for weighting top part of news articles
		   int32_t      numTopWords             ,
		   float     topWordsWeight          ,
		   float     topSentenceWeight       ,
		   int32_t      maxWordsInSentence      ,
		   char     *buf                     ,
		   int32_t      bufSize                 ) {
	
	// sanity check
	//if ( m_buf ) { char *xx = NULL; *xx = 0; }
	reset();

	// save for printing into g_pbuf in TermTable.cpp
	m_scoreBySection            = scoreBySection          ;
	m_indexContentSectionOnly   = indexContentSectionOnly ;
	m_minSectionScore           = minSectionScore         ;
	m_minAvgWordScore           = minAvgWordScore         ;
	m_minIndexableWords         = minIndexableWords       ;
	m_numTopWords               = numTopWords             ;
	m_topWordsWeight            = topWordsWeight          ;
	m_topSentenceWeight         = topSentenceWeight       ;
	m_maxWordsInSentence        = maxWordsInSentence      ;

	// allocate m_scores buffer, one byte score per word
	m_scores = NULL;
	int32_t nw = words->getNumWords();
	int32_t need = nw * 4;
	// assume no malloc
	m_needsFree = false;
	if ( need < SCORES_LOCALBUFSIZE ) m_buf = m_localBuf;
	else if ( need < bufSize ) m_buf = buf;
	else {
		m_buf = (char *)mmalloc ( need , "Scores" );
		m_needsFree = true;
	}
	m_bufSize = need;
	if ( ! m_buf ) return false;
	char *p = m_buf;
	m_scores = (int32_t *)p;
	p += nw * 4;
	//m_rerankScores = (int32_t *) p;

	// all words start with a default normal score, 128 as of right now
	for ( int32_t i = 0 ; i < nw ; i++ ) m_scores[i] = NORM_WORD_SCORE;

	nodeid_t   *tids  = words->getTagIds  ();
	int64_t  *wids  = words->getWordIds ();		
	char      **w     = words->m_words;
	int32_t       *wlens = words->m_wordLens;

	// . zero out scores of words in javascript and style tags
	// . set scores to 1 if word in select or marquee tag
	// . MATCHES.CPP check if the score is -1, and ignores it if so!!!!
	//   so if you modify this, keep that in mind
	if ( ! tids ) return true;
	char inScript  = 0;
	char inStyle   = 0;
	char inSelect  = 0;
	char inMarquee = 0;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not tag
		if ( ! tids[i] ) {
			if (inScript || inStyle) { m_scores[i] = -1; continue;}
			if (inSelect||inMarquee) { m_scores[i] = -1; continue;}
			continue;
		}
		// give all tags score of 0 by default
		m_scores[i] = 0;

		if ( (tids[i]&BACKBITCOMP) == TAG_SCRIPT ) { // <script>
			if   ( tids[i] & BACKBIT ) inScript = 0;
			else                       inScript = 1;
			continue;
		}
		if ( (tids[i]&BACKBITCOMP) == TAG_STYLE ) { // <style>
			if   ( tids[i] & BACKBIT ) inStyle = 0;
			else                       inStyle = 1;
			continue;
		}
		if ( (tids[i]&BACKBITCOMP) == TAG_SELECT ) { // <select>
			if   ( tids[i] & BACKBIT ) inSelect = 0;
			else                       inSelect = 1;
			continue;
		}
		if ( (tids[i]&BACKBITCOMP) == TAG_MARQUEE ) { // <marquee>
			if   ( tids[i] & BACKBIT ) inMarquee = 0;
			else                       inMarquee = 1;
			continue;
		}
		if ( inScript || inStyle   ) { m_scores[i] = -1; continue; }
		if ( inSelect || inMarquee ) { m_scores[i] = -1; continue; }
	}

	// . set pre-scores of words to NORM_WORD_SCORE (128) if in not in a 
	//   link and to NORM_WORD_SCORE/6 (21) if in a link
	// . ignore punctuation and tag words
	// . then set the final score of each word to the average of its
	//   pre-score and the pre-scores of its 7 left and 8 right neighbors
	// . do not score anything with a score of "1" that is reserved for
	//   the <select> tags above
	if ( scoreBySection ) {
		if ( ! setScoresBySection ( words, 
					    indexContentSectionOnly ,
					    minSectionScore ,
					    minAvgWordScore ) ) 
			return false;
	}
	// otherwise, give all indexable words a default normal score
	//else if (titleRecVersion >= 60){
	for ( int32_t i = 0 ; i < nw ; i++ )
		if ( wids[i] && m_scores[i] > 0 ) 
			m_scores[i] = NORM_WORD_SCORE; // 128;
	//}
	//else{ // old version...unignores script/select/style words
	//	for ( int32_t i = 0 ; i < nw ; i++ )
	//		if ( wids[i] ) 
	//			m_scores[i] = NORM_WORD_SCORE; // 128;
	//}

	// . we need at least this many positive scoring, indexable words
	// . this is -1 if unused
	if ( minIndexableWords > 0 ) {
		int32_t count = 0;
		for ( int32_t i = 0 ; i < nw ; i++ ) 
			if ( wids[i] && m_scores[i] > 1 ) count++;
		if ( count < minIndexableWords )
			for ( int32_t i = 0 ; i < nw ; i++ ) m_scores[i] = 0 ;
	}

	// . now weight the words in the top of the document more
	// . news articles and other docs put the most important info first
	if ( numTopWords == 0 ) return true;

	int32_t k;
	int32_t count = 0;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// skip if not indexed (even though it may have a score > 0)
		if ( wids[i] == 0 ) continue;
		// skip over anything with a weight of 0 (ignored) or 1
		// which means in a <select> tag or something else that should
		// be indexed with minimum possible score.
		if ( m_scores[i] <= 1 ) {
			// end of sentence?
			if ( wids[i] != 0            ) continue;
			if ( maxWordsInSentence == 0 ) continue;
			for ( k = 0 ; k < wlens[i] ; i++ )
				if ( w[i][k] == '.' || 
				     w[i][k] == '!'   )
				maxWordsInSentence = 0;
			continue;
		}
		if ( count < numTopWords        ) 
			m_scores[i] = 
				(int32_t)((float)m_scores[i] * topWordsWeight);
		if ( count < maxWordsInSentence ) 
			m_scores[i] = 
				(int32_t)((float)m_scores[i] * topSentenceWeight);
		count++;
		if ( count >= maxWordsInSentence ) maxWordsInSentence = 0;
		if ( count >= numTopWords        ) numTopWords        = 0;
		if ( maxWordsInSentence > 0 ) continue;
		if ( numTopWords        > 0 ) continue;
		break;
	}

	return true;
}

#define RADIUS 16

bool Scores::setScoresBySection ( Words *words, 
				  bool indexContentSectionOnly ,
				  int32_t minSectionScore         ,
				  int32_t minAvgWordScore         ) {

	int32_t       nw     = words->getNumWords();
	int64_t *wids   = words->getWordIds ();
	nodeid_t  *tids   = words->getTagIds  ();
	bool       inLink = false;
	int32_t       score  = 0;
	int32_t       level  = 0;
	int32_t       i;
	nodeid_t   ids    [ MAX_LEVELS ]; // tag ids on stack
	int32_t       scores [ MAX_LEVELS ]; // scores on stack
	int32_t       starts [ MAX_LEVELS ]; // section start positions on stack
	int32_t       previs [ MAX_LEVELS ]; // linked list end
	int32_t       previ =  0;
	// for storing the winning section
	int32_t       max   =  -2000000000;
	int32_t       maxa  = -1;
	int32_t       maxb  = -1;
	char       flag  =  0;

	// . get the vector, 1-1 with the words
	// . wscores is 1 byte, fscores is 4 bytes, wnext is 4 bytes
	int32_t need = nw * 6;
	char *tmp = NULL;
	char tstack[1024*100];
	if ( need > 1024*100 )
		tmp = (char *)mmalloc(need,"Scoress");
	else     
		tmp = tstack;
	// bail if alloc failed
	if ( ! tmp ) return log("build: Scores failed to alloc %"INT32" bytes.",
				  need);
	char *p = (char *)tmp;
	int32_t   *wnext   = (int32_t  *)p ; p += 4 * nw;
	int16_t  *wscores = (int16_t *)p ; p += 2 * nw;
	// init
	wnext[0] = -1;
	// point to our score buffer
	int32_t *fscores = m_scores;
	// convenience var
	//char *wscores = m_wscores;
	// -1 means score is unset
	//memset ( wscores , -1 , nw );
	// make this fixed for now, a hyperlink word needs to be balanced out
	// with 6 plain text words in order to be scored positively. this needs
	// to be an unchangeable knob in the ruleset file.
	float ratio = 8.0;
	// how much to score a plain text word?
	int32_t plain = NORM_WORD_SCORE; // 128;
	// how much to score a word in hypertext?
	int32_t hyper = (int32_t)((float)plain / ratio);
	// for scoring the section
	int32_t neg = plain * 4;
	// misc vars
	int32_t mid,k,j,sj,rscore,lscore,bscore,count,cumscore;
	nodeid_t tid;

	for ( i = 0 ; i < nw ; i++ ) {
		// get the tag id
		tid = tids[i] & BACKBITCOMP;
		// we have to know what words are in hyperlinks
		if ( tid == 2 ) {
			if ( tids[i] & BACKBIT ) inLink = false;
			else                     inLink = true;
			continue;
		}
		// . if score already set to 0 or 1, skip it
		// . probably in a <script>, <style> or <select> tag
		// . MDW: i commented this out because it causes 
		//   menu elimination tech to falter, zak uncommented
		//   it because it screwed up summary generation,
		//   cuz we were taking summaries from scripts i guess
		//if ( m_scores[i] <= 1 ) { 
		//	// set it to 0 in case it was 1 already
		//	if ( indexContentSectionOnly ) m_scores[i] = 0; 
		//	continue; 
		//}
		// did we have a non section delimiting word?
		if ( tid != TAG_DIV  &&    // <div>
		     tid != TAG_TEXTAREA  &&    // <textarea>
		     tid != TAG_TR  &&    // <tr>
		     tid != TAG_TD  &&    // <td>
		     tid != TAG_TABLE    ) { // <table>
			// don't score tags, only text
			//if ( tid > 0 ) continue;
			// skip if punct or tag
			if ( wids[i] == 0 ) {
				// . punish if tag, except for <br> or <p>
				// . generally, taggy sections are not very
				//   good content.
				if ( ! tid ) continue;
				if ( tid == TAG_BR ) continue; // <br>
				if ( tid == TAG_P ) continue; // <p>
				score -= neg;
				continue;
			}
			// in a <script>, <style> or <select> tag?
			//if ( indexContentSectionOnly && m_scores[i] == -1 ) 
			// hey we should always scores these as -1! we were
			// getting summaries with no spaces in them!
			if ( m_scores[i] == -1 ) 			
				continue;
			// if we hit a comment section identifier, then stop. i
			// don't want to index comments right now
			//if ( wids[i] == WID_COMMENT  ||
			//     wids[i] == WID_COMMENTS ||
			// subtract points if in link, otherwise add points
			if ( inLink ) { 
				score -= neg    ; wscores[i] = hyper; }
			else          { 
				score += plain  ; wscores[i] = plain; }
			// keep linked list up to date
			wnext[previ] =  i;
			wnext[    i] = -1;
			previ = i;
			continue;
		}

		// . we got a section delimiting tag
		// . is there an embedded section in this section?
		// . this should break any hyper link right?
		inLink = false;

		// front tag? did we start a new section?
		if ( !(tids[i] & BACKBIT) ) {
			// no more sections until we pop this one off
			if ( level >= MAX_LEVELS ) {
				// only log once
				if ( flag == 0 ) {
					log("build: Exceeded max levels."); 
					flag = 1;
				}
				continue; 
			}
			if ( g_conf.m_logDebugBuild ) 
				log(LOG_DEBUG,"build: Scored section %"INT32": %"INT32"",
				    level, score);
			// push old info onto the stack
			ids    [level] = tids[i];
			scores [level] = score;
			starts [level] = i;
			previs [level] = previ;
			level++;
			score = 0;
			// start another linked list
			previ = i;
			// assume no linked list of words for this section
			wnext[i] = -1;
			continue;
		}

		// . it's a back tag
		// . bail if no corresponding fron tag on stack
		if ( level == 0 ) continue;
		// or if did not match what was on top of stack
		if ( tid != ids[level-1] ) continue;
		// recycle code
	hookin:
		// pop stack
		level--;

		// 
		// this part scores the individual words based on the scores of
		// their neighbors. it is just like a moving average of the
		// past and future, but we don't bother dividing by the number 
		// of samples.
		//
		
		// score words on left side of section
		if ( level == -1 ) sj = 0;
		else               sj = starts[level];
		// often, the first in level is not an indexable word, but
		// just a start of the linked list
		if ( wids[sj] == 0 ) sj = wnext[sj];
		// now start with that
		j = sj;
		// bail if nothing in the list
		if ( j == -1 ) goto empty;
		// compute left Boundary score, bscore
		bscore = 0;
		// accumulate score of first 16 words
		for (count=0;count<RADIUS&&j>=0;count++,j=wnext[j]) {
			bscore += wscores[j];
			// show score of each word
			//char *s    = words->m_words   [j];
			//int32_t  slen = words->m_wordLens[j];
			//printstring(s,slen);
			// then score of it
			//fprintf(stderr,"(%"INT32") ",(int32_t)wscores[j]);
		}
		// save accumulation, not average
		cumscore = bscore;
		// if section has less than 16 words then 
		// grow bscore proportionately
		//if ( count < 16 ) bscore = (bscore *16)/count;
		// make it an average score, so it's in [0,128]
		bscore /= count;
		// 1 is reserved for <select> tag et al
		if ( bscore == 1 ) bscore = 2;
		// must be above this. all or nothing.
		if ( bscore < minAvgWordScore ) bscore = 0;
		// set score of first 16 words to the sum
		// of the scores of the first 16 words
		j = sj; // j = starts[level];
		for (count=0;count<RADIUS&&j>=0;count++,j=wnext[j])
			fscores[j] = bscore;
		// bail if no more words in section
		if ( j == -1 ) goto skip;
		
		// . set up right/mid/left ptr info
		// . set our rightmost ptr, k
		k = j;
		// and right most cumulative score
		rscore = cumscore;
		// left side cumulative score starts at 0
		lscore = 0;
		// advance j to the 8th word, almost exactly in the middle
		j = sj; // j = starts[level];
		for (count=0;count<(RADIUS/2-1)/*7*/;count++,j=wnext[j]);
		// score words in middle now
		mid = j;
		// j is the left most ptr
		j = sj; // j = starts[level];
		
	more:	// now set centroids' FINAL score, the sum of 
		// its wscore and first 5 on left and right.
		// divide by 16 to make it an average score
		//fscores[mid] = (rscore - lscore) / 16 ;
		//fscores[mid] = (rscore - lscore) >> 4;
		//fscores[mid] = cumscore >> 4;
		fscores[mid] = cumscore / RADIUS; // >> 4;
		// 1 is reserved for <select> tag et al
		if ( fscores[mid] == 1 ) fscores[mid] = 2;
		// must be above this. all or nothing.
		if ( fscores[mid] < minAvgWordScore ) fscores[mid] = 0;
		// debug point
		//if ( fscores[mid] == 0 ) {
		//	char *xx = NULL; *xx = 0; }
		// advance left  end and its cumulative score
		cumscore -= wscores[j];
		j         = wnext  [j];
		// advance middle
		mid       = wnext  [mid];
		// advance right end and its cumulative score
		k         = wnext  [k];
		cumscore += wscores[k];
		// loop if more left
		if ( k > 0 ) goto more;
		
		// score words on right end
		bscore = 0;
		count  = 0;
		for (k=mid;k>=0;k=wnext[k],count++) bscore+=wscores[k];
		// get the average score of them
		bscore /= count;
		// 1 is reserved for <select> tag et al
		if ( bscore == 1 ) bscore = 2;
		// must be above this. all or nothing.
		if ( bscore < minAvgWordScore ) bscore = 0;
		// and set
		for (k=mid;k>=0;k=wnext[k]) fscores[k]=bscore;
		
		//
		// end neighbor-influenced scoring
		//
		
	skip:
		if ( (score > max || maxa==-1) && score > minSectionScore ) {
			// zero out the previous winning section
			if ( indexContentSectionOnly && maxa >= 0 ) {
				// zero out all in list
				for ( j = maxa; j >= 0 ; j = wnext[j] )
					fscores[j] = 0;
			}
			// this section is the new winning section
			log(LOG_DEBUG, "build: Winning section: %"INT32", "
			    "score: %"INT32"", level, score);
			max  = score;
			maxa = sj; // starts[level];
			maxb = i; // our section's last word # is < i
		}
		// if we were not the winning section, zero ourselves out
		else if ( indexContentSectionOnly ) {
			for ( j = sj ; j >= 0 ; j = wnext[j] )
				fscores[j] = 0;
		}
	empty:
		// pop old score et al back to be resumed
		if ( level >= 0 ) {
			score  = scores[level];
			previ  = previs[level];
		}
		// get next node
	}

	// set scores of anything still on the stack at completion
	while ( level >= 0 ) {
		i = nw - 1;
		goto hookin;
	}


	/*
	for (int32_t i = 0 ; i < nw ; i++ ) {
		// skip if no wid
		if ( words->m_wordIds[i] == 0LL ) continue;
		if ( m_scores[i] == 0 ) continue;
		// show score of each word
		char *s    = words->m_words   [i];
		int32_t  slen = words->m_wordLens[i];
		printstring(s,slen);
		// then score of it
		fprintf(stderr,"(%"INT32") ",(int32_t)m_scores[i]);
	}
	*/

	// and scores of the main/base section
	//i = nw - 1;
	//if ( ! flag ) { flag = 1; level = 1; goto hookin; }

	// ok, now we have designated all the sections and assigned them a
	// score, so if we are just getting the top section, return that

	// give caller the article text in a nutshell if that's all they wanted
	//if ( n1 ) { 
	//	// assume no top section
	//	*n1 = -1; *n2 = -1;
	//	if ( maxi >= 0 ) { *n1 = maxa; *n2 = maxb; }
	//	return true;
	//}

	// now set the individual word scores in each section
	//for ( int32_t i = 0 ; i < nsecs ; i++ )
	//	setSectionScores ( i , secStarts , secEnds , wscores );

	// copy scores
	//for (int32_t i = 0 ; i < nw ;i++) m_scores[i]=(unsigned char)fscores[i];
	// done
	if ( tmp != tstack ) mfree ( tmp , need , "Scores" );

	// success
	return true;
}
