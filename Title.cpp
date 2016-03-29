#include "gb-include.h"

#include "Title.h"
#include "Words.h"
#include "Sections.h"
#include "Pops.h"
#include "Pos.h"
#include "Titledb.h" // TITLEREC_CURRENT_VERSION
#include "Profiler.h"
#include "sort.h"
#include "HashTable.h"
//#include "CollectionRec.h"
#include "Indexdb.h"
#include "XmlDoc.h"

// test urls
// http://www.thehindu.com/2009/01/05/stories/2009010555661000.htm
// http://xbox360.ign.com/objects/142/14260912.html
// http://www.scmp.com/portal/site/SCMP/menuitem.2c913216495213d5df646910cba0a0a0?vgnextoid=edeb63a0191ae110VgnVCM100000360a0a0aRCRD&vgnextfmt=teaser&ss=Markets&s=Business
// http://www.legacy.com/shelbystar/Obituaries.asp?Page=LifeStory&PersonId=122245831
// http://web.me.com/bluestocking_bb/The_Bluestocking_Guide/Book_Reviews/Entries/2009/1/6_Hamlet.html
// http://larvatusprodeo.net/2009/01/07/partisanship-politics-and-participation/
// http://content-uk.cricinfo.com/ausvrsa2008_09/engine/current/match/351682.html
// www4.gsb.columbia.edu/cbs-directory/detail/6335554/Schoenberg 
// http://www.washingtonpost.com/wp-dyn/content/article/2008/10/29/AR2008102901960.html
// http://www.w3.org/2008/12/wcag20-pressrelease.html
// http://www.usnews.com/articles/business/best-careers/2008/12/11/best-careers-2009-librarian.html
// http://www.verysmartbrothas.com/2008/12/09/
// http://www.slashgear.com/new-palm-nova-handset-to-have-touchscreen-and-qwerty-keyboard-0428710/

// still bad
// http://66.231.188.171:8500/search?k3j=668866&c=main&n=20&ldays=1&q=url%3Ahttp%3A%2F%2Fmichellemalkin.com%2F2008%2F12%2F29%2Fgag-worthy%2F selects
// "gag-worthy" instead of 
// "Gag-worthy: Bipartisan indignance over .Barack the Magic Negro. parody"
// http://www.1800pocketpc.com/2009/01/09/web-video-downloader-00160-download-videos-from-youtube-on-your-pocket-pc.html : need to fix the numbers in the
// path somehow so similarity is higher


/*
static int32_t isHeadlineClass(Xml *xml, Words *words, int32_t wordIndex);

// . List of title tags
// . do not include bold cuz  
//   http://www.groovanauts.com/board/showthread.php?threadid=41718
//   gets "Username" as the title!
static char s_titleTags[] = { TAG_TITLE, TAG_H1, TAG_H2, TAG_H3}; //,TAG_B };
	
static inline int s_min(const int x, const int y) {
	if(x < y) return x;
	return y;
}

static inline int s_max(const int x, const int y) {
	if(x > y) return x;
	return y;
}
*/

Title::Title() {
	m_title = NULL;
	m_titleBytes = 0;
	m_query = NULL;
}

Title::~Title() {
	reset();
}

void Title::reset() {
	if ( m_title && m_title != m_localBuf ) 
		mfree ( m_title , m_titleAllocSize , "Title" );
	m_title = NULL;
	m_titleBytes = 0;
	m_titleAllocSize = 0;
	m_query = NULL;
	m_titleTagStart = -1;
	m_titleTagEnd   = -1;
}

// returns false and sets g_errno on error
bool Title::setTitle ( XmlDoc   *xd            ,
		       Xml      *xml           ,
		       Words    *words         ,
		       Sections *sections      ,
		       Pos      *pos           ,
		       int32_t      maxTitleChars ,
		       int32_t      maxTitleWords ,
		       SafeBuf  *pbuf,
		       Query    *q,
		       CollectionRec *cr ,
		       int32_t niceness ) {

	// if this is too big the "first line" algo can be huge!!!
	// and really slow everything way down with a huge title candidate
	if ( maxTitleWords > 128 ) maxTitleWords = 128;

	m_niceness = niceness;

	// make Msg20.cpp faster if it is just has
	// Msg20Request::m_setForLinkInfo set to true, no need
	// to extricate a title.
	if ( maxTitleChars <= 0 ) return true;
	if ( maxTitleWords <= 0 ) return true;

	int64_t startTime = gettimeofdayInMilliseconds();

	// . reset so matches.cpp using this does not core
	// . assume no title tag
	m_titleTagStart = -1;
	m_titleTagEnd   = -1;

	// if we are a json object
	if ( ! xd->m_contentTypeValid ) { char *xx=NULL;*xx=0; }
	char *val = NULL;
	// look for the "title:" field in json then use that
	SafeBuf jsonTitle;
	int32_t vlen = 0;
	if ( xd->m_contentType == CT_JSON ) {
		// int16_tcut
		char *s = xd->ptr_utf8Content;
		char *jt;
		jt = getJSONFieldValue(s,"title",&vlen);
		if ( jt && vlen > 0 ) {
			jsonTitle.safeDecodeJSONToUtf8 (jt, vlen, m_niceness);
			jsonTitle.nullTerm();
		}
		// if we got a product, try getting price
		int32_t oplen;
		char *op = getJSONFieldValue(s,"offerPrice",&oplen);
		if ( op && oplen ) {
			if ( ! is_digit(op[0]) ) { op++; oplen--; }
			float price = atof2(op,oplen);
			// print without decimal point if ends in .00
			if ( (float)(int32_t)price == price )
				jsonTitle.safePrintf(", &nbsp; $%"INT32"",
						     (int32_t)price);
			else
				jsonTitle.safePrintf(", &nbsp; $%.02f",price);
		}
		if ( jsonTitle.length() ) {
			val = jsonTitle.getBufStart();
			vlen = jsonTitle.length();
		}
	}
	// if we had a title: field in the json...
	if ( val && vlen > 0 ) {
		char *dst = NULL;
		m_titleBytes = vlen;
		if ( m_titleBytes+1 <  TITLE_LOCAL_SIZE )
			dst = m_localBuf;
		else {
			dst = (char *)mmalloc ( m_titleBytes+1,"titdst" );
			if ( ! dst ) return false;
			m_titleAllocSize = m_titleBytes+1;
		}
		m_title = dst;
		gbmemcpy ( dst , val , m_titleBytes );
		dst[m_titleBytes] = '\0';
		return true;
	}

	// json content, if has no explicit title field, has no title then
	if ( xd->m_contentType == CT_JSON ) {
		m_localBuf[0] = '\0';
		m_title = m_localBuf;
		m_titleBytes = 0;
		return true;
	}

	bool status = setTitle4 ( xd ,
				  xml ,
				  words ,
				  sections ,
				  pos ,
				  maxTitleChars ,
				  maxTitleWords ,
				  pbuf ,
				  q ,
				  cr );

	int64_t took = gettimeofdayInMilliseconds() - startTime;
	if ( took > 5 ) log("query: Title set took %"INT64" ms for %s", took,
			    xd->getFirstUrl()->getUrl());

	return status;
}

// types of titles. indicates where they came from.
#define TT_LINKTEXTLOCAL  1
#define TT_LINKTEXTREMOTE 2
#define TT_RSSITEMLOCAL   3
#define TT_RSSITEMREMOTE  4
#define TT_BOLDTAG        5
#define TT_HTAG           6
#define TT_TITLETAG       7
#define TT_DMOZ           8
#define TT_FIRSTLINE      9
#define TT_DIVTAG         10
#define TT_FONTTAG        11
#define TT_ATAG           12
#define TT_TDTAG          13
#define TT_PTAG           14
#define TT_URLPATH        15
#define TT_TITLEATT       16

#define MAX_TIT_CANDIDATES 100

// does word qualify as a subtitle delimeter?
bool isWordQualified ( char *wp , int32_t wlen ) {
	// must be punct word
	if ( is_alnum_utf8(wp) ) return false;
	// scan the chars
	int32_t x; for ( x = 0 ; x < wlen ; x++ ) {
		if ( wp[x] == ' ' ) continue;
		break;
	}
	// does it qualify as a subtitle delimeter?
	bool qualified = false;
	if ( x < wlen ) qualified = true;
	// fix amazon.com from splitting on period
	if ( wlen==1 ) qualified = false;
	return qualified;
}


//
// TODO: do not accumulate boosts from a parent
// and its kids, subtitles...
//
bool Title::setTitle4 ( XmlDoc   *xd            ,
			Xml      *XML           ,
			Words    *WW            ,
			Sections *sections      ,
			Pos      *POS           ,
			int32_t      maxTitleChars ,
			int32_t      maxTitleWords ,
			SafeBuf  *pbuf,
			Query    *q,
			CollectionRec *cr     ) {

	m_maxTitleChars = maxTitleChars;
	m_maxTitleWords = maxTitleWords;

	// assume no title
	reset();

	int32_t NW = WW->getNumWords();
	if (pbuf) {
		//pbuf->safePrintf("<div stype=\"border:1px solid black\">");
		//pbuf->safePrintf("<b>***Finding Title***</b><br>\n");
	}

	// array of candidate tags
	static char s_candTags[512];
	static char s_flag = 0;
	if ( s_flag == 0 ) {
		// do not re-do
		s_flag = 1;
		// reset
		memset ( s_candTags , 0 , 512 );
	}

	// set each time since we "unset" below if we've no "article content"
	s_candTags [ TAG_B     ] = 1;
	s_candTags [ TAG_H1    ] = 1;
	s_candTags [ TAG_H2    ] = 1;
	s_candTags [ TAG_H3    ] = 1;
	s_candTags [ TAG_DIV   ] = 1;
	s_candTags [ TAG_TD    ] = 1;
	s_candTags [ TAG_P     ] = 1;
	s_candTags [ TAG_FONT  ] = 1;
	s_candTags [ TAG_TITLE ] = 1;
	// we only allow candidates in <a> tags if it is a self link!
	s_candTags [ TAG_A     ] = 1;

	//
	// now get all the candidates
	//

	// . allow up to 100 title CANDIDATES
	// . "as" is the word # of the first word in the candidate
	// . "bs" is the word # of the last word IN the candidate PLUS ONE
	int32_t   n = 0;
	int32_t   as      [MAX_TIT_CANDIDATES];
	int32_t   bs      [MAX_TIT_CANDIDATES];
	float  scores  [MAX_TIT_CANDIDATES];
	Words *cptrs   [MAX_TIT_CANDIDATES];
	int32_t   types   [MAX_TIT_CANDIDATES];
	char   htmlEnc [MAX_TIT_CANDIDATES];
	int32_t   numAlnum[MAX_TIT_CANDIDATES];
	int32_t   parent  [MAX_TIT_CANDIDATES];
	// record the scoring algos effects
	float  baseScore        [MAX_TIT_CANDIDATES];
	float  noCapsBoost      [MAX_TIT_CANDIDATES];
	float  qtermsBoost      [MAX_TIT_CANDIDATES];
	float  inCommonCandBoost[MAX_TIT_CANDIDATES];
	float  inCommonBodyBoost[MAX_TIT_CANDIDATES];
	// reset these
	for ( int32_t i = 0 ; i < MAX_TIT_CANDIDATES ; i++ )
		// assume no parent
		parent[i] = -1;

	// xml and words class for each link info, rss item
	Xml   tx[MAX_TIT_CANDIDATES];
	Words tw[MAX_TIT_CANDIDATES];
	int32_t  ti = 0;

	// restrict how many link texts and rss blobs we check for titles
	// because title recs like www.google.com have hundreds and can
	// really slow things down to like 50ms for title generation
	int32_t kcount = 0;
	int32_t rcount = 0;

	// only allow 4 internal inlink titles
	//int32_t didHost = 0;

	LinkInfo *info = xd->getLinkInfo1();
	// a flag to control subloop jumping
	char didit = false;

	// come back to top of loop after switching "info" to point to
	// the imported link info from another collection, linkInfo2...
 fooloop:

	//int64_t x = gettimeofdayInMilliseconds();

	// . get every link text
	// . TODO: repeat for linkInfo2, the imported link text
	for ( Inlink *k = NULL; info && (k = info->getNextInlink(k)) ; ) {
		// breathe
		QUICKPOLL(m_niceness);
		// fast skip check for link text
		if ( k->size_linkText >= 3 && ++kcount >= 20 ) continue;
		// fast skip check for rss item
		if ( k->size_rssItem > 10 && ++rcount >= 20 ) continue;
		// set Url
		Url u;
		u.set ( k->getUrl() , k->size_urlBuf );
		// is it the same host as us?
		bool sh = true;
		// the title url
		Url *tu = xd->getFirstUrl();
		// skip if not from same host and should be
		if ( tu->getHostLen() != u.getHostLen() ) sh = false;
		// skip if not from same host and should be
		if (strncmp(tu->getHost(),u.getHost(),u.getHostLen()))sh=false;
		// get the link text
		if ( k->size_linkText >= 3 ) {
			// if same host and it already "voted" skip it
			//if ( sh && didHost >= 4 ) continue;
			// only one vote for this host
			//if ( sh ) didHost++;
			// set the words to it
			//if ( ! k->setXmlFromLinkText ( &tx[ti] ) )
			//	return false;
			char *p    = k->getLinkText();
			int32_t  plen = k->size_linkText - 1;
			if ( ! verifyUtf8 ( p , plen ) ) {
				log("title: set4 bad link text from url=%s",
				    k->getUrl());
				continue;
			}
			// now the words.
			if ( ! tw[ti].set ( k->getLinkText() ,
					    k->size_linkText-1, // len
					    TITLEREC_CURRENT_VERSION ,
					    true              , // computeIds
					    0                 ))// niceness
				return false;
			// set the bookends, it is the whole thing
			cptrs   [n] = &tw[ti];
			as      [n] = 0;
			bs      [n] = tw[ti].getNumWords();
			htmlEnc [n] = false;
			// score higher if same host
			if ( sh ) scores[n] = 1.05;
			// do not count so high if remote!
			else      scores[n] = 0.80;
			// set the type
			if ( sh ) types [n] = TT_LINKTEXTLOCAL;
			else      types [n] = TT_LINKTEXTREMOTE;
			// another candidate
			n++;
			// use xml and words
			ti++;
			// break out if too many already. save some for below.
			if ( n + 30 >= MAX_TIT_CANDIDATES ) break;
		}
		// get the rss item
		if ( k->size_rssItem <= 10 ) continue;
		// . returns false and sets g_errno on error
		// . use a 0 for niceness
		if ( ! k->setXmlFromRSS ( &tx[ti] , 0 ) ) return false;
		// if same host and it already "voted" skip it
		//if ( sh && didHost >= 4 ) continue;
		// only one vote for this host
		//if ( sh ) didHost++;
		// get the word range
		int32_t tslen;
		bool isHtmlEnc;
		char *ts = tx[ti].getRSSTitle ( &tslen , &isHtmlEnc );
		// skip if not in the rss
		if ( ! ts ) continue;
		// skip if empty
		if ( tslen <= 0 ) continue;
		// now set words to that
		if ( ! tw[ti].set ( ts                       ,
				    tslen                    ,
				    TITLEREC_CURRENT_VERSION ,
				    true       , // compute wordIds?
				    0          ))// niceness
			return false;
		// point to that
		cptrs   [n] = &tw[ti];
		as      [n] = 0;
		bs      [n] = tw[ti].getNumWords();
		htmlEnc [n] = isHtmlEnc;
		// increment since we are using it
		ti++;
		// base score for rss title
		if ( sh ) scores[n] = 5.0;
		// if not same host, treat like link text
		else      scores[n] = 2.0;
		// set the type
		if ( sh ) types [n] = TT_RSSITEMLOCAL;
		else      types [n] = TT_RSSITEMREMOTE;
		// advance
		n++;
		// break out if too many already. save some for below.
		if ( n + 30 >= MAX_TIT_CANDIDATES ) break;
	}

	//logf(LOG_DEBUG,"title: took1=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// process the imported link info
	info = *xd->getLinkInfo2();
	// only process it once though, use the flag "didit" to control that
	if ( ! didit && info ) { didit = true; goto fooloop; }


	// . set the flags array
	// . indicates what words are in title candidates already, but
	//   that is set below
	// . up here we set words that are not allowed to be in candidates,
	//   like words that are in a link that is not a self link
	// . alloc for it
	char *flags = NULL;
	char  localBuf[10000];
	int32_t  need = WW->getNumWords();
	if ( need <= 10000 ) flags = (char *)localBuf;
	else                 flags = (char *)mmalloc(need,"TITLEflags");
	if ( ! flags ) return false;
	// clear it
	memset ( flags , 0 , need );

	// check tags in body
	nodeid_t *tids = WW->getTagIds();
	// scan to set link text flags
	// loop over all "words" in the html body
	char inLink   = false;
	char selfLink = false;
	for ( int32_t i = 0 ; i < NW ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// if in a link that is not self link, cannot be in a candidate
		if ( inLink && ! selfLink ) flags[i] |= 0x02;
		// out of a link
		if ( tids[i] == (TAG_A | BACKBIT) ) inLink = false;
		// if not start of <a> tag, skip it
		if ( tids[i] != TAG_A ) continue;
		// flag it
		inLink = true;
		// get the node in the xml
		int32_t xn = WW->m_nodes[i];
		// is it a self link?
		int32_t len;
		char *link = XML->getString(xn,"href",&len);
		// . set the url class to this
		// . TODO: use the base url in the doc
		Url u; u.set(link, len, true, false );
		// compare
		if ( u.equals ( xd->getFirstUrl() ) ) 
			selfLink = true;
		else           
			selfLink = false;
		// skip if not selfLink
		if ( ! selfLink ) continue;
		// if it is a selflink , check for an "onClock" tag in the
		// anchor tag to fix that Mixx issue for:
		// http://www.npr.org/templates/story/story.php?storyId=5417137
		int32_t  oclen;
		char *oc = NULL;
		if ( ! oc ) oc = XML->getString(xn,"onclick",&oclen);
		if ( ! oc ) oc = XML->getString(xn,"onClick",&oclen);
		// assume not a self link if we see that...
		if ( oc ) selfLink = false;
		// if this <a href> link has a "title" attribute, use that
		// instead! that thing is solid gold.
		int32_t  atlen;
		char *atitle = XML->getString(xn,"title",&atlen);
		// stop and use that, this thing is gold!
		if ( ! atitle || atlen <= 0 ) continue;
		// craziness? ignore it...
		if ( atlen > 400 ) continue;
		// if it contains permanent or permalink, ignore it!
		if ( strncasestr ( atitle,"permalink", atlen))continue;
		if ( strncasestr ( atitle,"permanent", atlen))continue;
		// do not count the link text as viable
		selfLink = false;
		// aw, dammit
		if ( ti >= MAX_TIT_CANDIDATES ) continue;
		// other dammit
		if ( n >= MAX_TIT_CANDIDATES ) break;
		// ok, process it
		if ( ! tw[ti].set ( atitle            ,
				    atlen             , // len
				    TITLEREC_CURRENT_VERSION ,
				    true              , // computeIds
				    0                 ))// niceness
			return false;
		// set the bookends, it is the whole thing
		cptrs   [n] = &tw[ti];
		as      [n] = 0;
		bs      [n] = tw[ti].getNumWords();
		htmlEnc [n] = false;
		scores  [n] = 3.0; // not ALWAYS solid gold!
		types   [n] = TT_TITLEATT;
		// we are using the words class
		ti++;
		// advance
		n++;
		// break out if too many already. save some for below.
		if ( n + 20 >= MAX_TIT_CANDIDATES ) break;
	}

	//logf(LOG_DEBUG,"title: took2=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	//int64_t *wids = WW->getWordIds();
	// . find the last positive scoring guy
	// . do not consider title candidates after "r" if "r" is non-zero
	// . FIXES http://larvatusprodeo.net/2009/01/07/partisanship-politics-
	//         and-participation/
	/*
	int32_t r = NW - 1;
	if ( ! SS ) r = 0;
	for ( ; r > 0 ; r-- ) {
		// skip if no word
		if ( wids[r] == 0LL ) continue;
		if ( SS->m_scores[r] > 0 ) break;
	}
	// if it is zero that means we had none! so consider all titles!
	if ( r == 0 ) r = NW;
	*/

	// . Sections class obsoletes Scores class
	// . this sets r to -1 if no words in article
	//int32_t seca,secb;
	//sections->getArticleRange ( &seca , &secb );

	// do we have a valid article even?
	//bool validArticle = false;
	// this must be something
	//if ( seca < secb ) validArticle = true;

	// the candidate # of the title tag
	int32_t tti = -1;

	// if no "article content", ignore these tags
	//if ( secb == -1 ) {
		s_candTags [ TAG_B     ] = 0;
		s_candTags [ TAG_H1    ] = 0;
		s_candTags [ TAG_H2    ] = 0;
		s_candTags [ TAG_H3    ] = 0;
		s_candTags [ TAG_DIV   ] = 0;
		s_candTags [ TAG_TD    ] = 0;
		s_candTags [ TAG_P     ] = 0;
		s_candTags [ TAG_FONT  ] = 0;
	//}

	// allow up to 4 tags from each type
	char table[512];
	// sanity check
	if ( getNumXmlNodes() > 512 ) { char *xx=NULL;*xx=0; }
	// clear table counts
	memset ( table , 0 , 512 );
	// ignore "titles" in script or style tags
	bool ignore = false;
	// the first word
	char *wstart = NULL; if ( NW > 0 ) wstart = WW->getWord(0);
	// loop over all "words" in the html body
	for ( int32_t i = 0 ; i < NW ; i++ ) {
		// come back up here if we encounter another "title-ish" tag
		// within our first alleged "title-ish" tag
	subloop:
		// get the tag id minus the back tag bit
		nodeid_t tid = tids[i] & BACKBITCOMP;
		// pen up and pen down for these comment like tags
		if ( tid == TAG_SCRIPT || tid == TAG_STYLE ) {
			// if start of it flag it
			if ( tids[i] & BACKBIT ) ignore = false;
			else                     ignore = true;
		}
		// stop after 30k of text
		if ( WW->getWord(i) - wstart > 200000 ) 
			break; // 1106
		// keep going if in script or style tag
		if ( ignore ) continue;
		// skip if not a good tag.
		if ( ! s_candTags[tid] ) continue;
		// must NOT be a back tag
		if ( tids[i] & BACKBIT ) continue;
		// skip if we hit our limit
		if ( table[tid] >= 4 ) continue;
		// after the document body we can only have "self link" titles
		//if ( validArticle && i >= secb && tid != 2 ) continue;
		// when using pdftohtml, the title tag is the filename
		if ( tid == TAG_TITLE && *xd->getContentType() == CT_PDF )
			continue;
		// skip over tag/word #i
		i++;
		// no words in links, unless it is a self link
		if ( i < NW && (flags[i] & 0x02) ) continue;
		// the start should be here
		int32_t start = -1;
		// do not go too far
		int32_t max = i + 200;
		// find the corresponding back tag for it
		for (  ; i < NW && i < max ; i++ ) {
			// hey we got it, BUT we got no alnum word first
			// so the thing was empty, so loop back to subloop
			if ( (tids[i] & BACKBITCOMP) == tid  &&   
			     (tids[i] & BACKBIT    ) && 
			     start == -1 )
				goto subloop;
			// if we hit another title-ish tag, loop back up
			if ( s_candTags [ tids[i] & BACKBITCOMP ] ) {
				// if no alnum text, restart at the top
				if ( start == -1 ) 
					goto subloop;
				// otherwise, break out and see if title works
				break;
			}
			// if we hit a breaking tag...
			if ( isBreakingTagId ( tids[i] & BACKBITCOMP ) &&
			     // do not consider <span> tags breaking for 
			     // our purposes. i saw a <h1><span> setup before.
			     tids[i] != TAG_SPAN )
				break;
			// skip if not alnum word
			if ( ! WW->isAlnum(i) ) continue;
			// if in link and score is 0 stop
			//if ( SS && SS->m_scores[i] <= 0 ) break;
			// if we hit an alnum word, break out
			if ( start == -1 ) start = i;
		}
		// if no start was found, must have had a 0 score in there
		if ( start == -1 ) continue;
		// if we exhausted the doc, we are done
		if ( i >= NW ) 
			break;
		// skip if way too big!
		if ( i >= max ) continue;
		// if was too long do not consider a title
		if ( i - start > 300 ) continue;
		// if not a back tag, that is bad too
		//if ( ! WW->isBackTag(i) ) continue;
		// . skip if too many bytes
		// . this does not include the length of word #i, but #(i-1)
		if ( WW->getStringSize ( start , i ) > 1000 ) continue;
		// count it
		table[tid]++;
		// max it out if we are positive scoring. stop after the
		// first positive scoring guy in a section. this might
		// hurt the "Hamlet" thing though...
		// MDW: well we now uses Sections, so commented this out
		//if ( SS && SS->m_scores[start] > 0 ) table[tid] = 100;
		// store a point to the title tag guy. Msg20.cpp needs this
		// because the zak's proximity algo uses it in Summary.cpp
		// and in Msg20.cpp
		if ( tid == TAG_TITLE &&
		     // only get the first one! often the 2nd on is in
		     // an iframe!! which we now expand into here.
		     m_titleTagStart == -1 ) {
			m_titleTagStart = start;
			m_titleTagEnd   = i;
			// save the candidate # because we always use this
			// as the title if we are a root
			if ( tti < 0 ) tti = n;
		}
		// point to words class of the body that was passed in to us
		cptrs   [n] = WW;
		as      [n] = start;
		bs      [n] = i;
		htmlEnc [n] = true;
		if ( tid == TAG_B     ) types      [n] = TT_BOLDTAG;
		if ( tid == TAG_H1    ) types      [n] = TT_HTAG;
		if ( tid == TAG_H2    ) types      [n] = TT_HTAG;
		if ( tid == TAG_H3    ) types      [n] = TT_HTAG;
		if ( tid == TAG_TITLE ) types      [n] = TT_TITLETAG;
		if ( tid == TAG_DIV   ) types      [n] = TT_DIVTAG;
		if ( tid == TAG_TD    ) types      [n] = TT_TDTAG;
		if ( tid == TAG_P     ) types      [n] = TT_PTAG;
		if ( tid == TAG_FONT  ) types      [n] = TT_FONTTAG;
		if ( tid == TAG_A     ) types      [n] = TT_ATAG;
		// the score
		if      ( tid == TAG_B     ) scores[n] = 1.0;
		else if ( tid == TAG_H1    ) scores[n] = 1.8;
		else if ( tid == TAG_H2    ) scores[n] = 1.7;
		else if ( tid == TAG_H3    ) scores[n] = 1.6;
		else if ( tid == TAG_TITLE ) scores[n] = 3.0;
		else if ( tid == TAG_DIV   ) scores[n] = 1.0;
		else if ( tid == TAG_TD    ) scores[n] = 1.0;
		else if ( tid == TAG_P     ) scores[n] = 1.0;
		else if ( tid == TAG_FONT  ) scores[n] = 1.0;
		// . self link is very powerful
		// . BUT http://www.npr.org/templates/story/
		//   story.php?storyId=5417137 doesn't use it right! so use
		//   1.3 instead of 3.0. that has an "onClick" thing in the
		//   <a> tag, so check for that!
		// this was bad for http://www.spiritualwoman.net/?cat=191
		// so i am demoting from 3.0 to 1.5
		else if ( tid == TAG_A     ) scores[n] = 1.5;
		// count it
		n++;
		// start loop over at tag #i, for loop does an i++, so negate
		// that so this will work
		i--;
		// break out if too many already. save some for below.
		if ( n + 10 >= MAX_TIT_CANDIDATES ) break;
	}

	//logf(LOG_DEBUG,"title: took3=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	/*
	// add in the dmoz title
	const unsigned char  numCatids     = xd->size_catIds/4;
	//char         	    *dmozTitles    = xd->ptr_dmozTitles;
	//int32_t         	    *dmozTitleLens = tr->getDmozTitleLens();
	// dmoz titles are always stored in UTF-8 format
	Xml   dxml;
	Words dwords;
	if ( numCatids && xd->ptr_dmozTitles && n < MAX_TIT_CANDIDATES ) {
		// point to buffer of NULL terminated titles
		char *dt = xd->ptr_dmozTitles;
		// set the xml
		if ( ! dxml.set ( dt                ,
				  gbstrlen(dt)        ,
				  false             , // ownData?
				  0                 , // allocSize
				  false             , // pureXml?
				  xd->m_version     ) )
			return false;
		// . set the words
		// . javier says he doesn't htmldecode() the dmoz titles
		//   so they should have html entities in them
		if ( ! dwords.set ( &dxml ,
				    true  , // compute word ids
				    true  ))// has html entities
			return false;
		// set the ptrs
		cptrs   [n] = &dwords;
		htmlEnc [n] =  true;
		scores  [n] =  3.0;
		types   [n] =  TT_DMOZ;
		as      [n] =  0;
		bs      [n] =  dwords.getNumWords();
		n++;
	}
	*/

	// sanity check
	if ( ! xd->m_contentTypeValid ) { char *xx=NULL;*xx=0; }
	// to handle text documents, throw in the first line of text
	// as a title candidate, just make the score really low
	bool textDoc = false;
	if ( xd->m_contentType == CT_UNKNOWN ) textDoc = true;
	if ( xd->m_contentType == CT_TEXT    ) textDoc = true;
	// make "i" point to first alphabetical word in the document
	int32_t i ; for ( i = 0 ; textDoc && i < NW && !WW->isAlpha(i) ; i++);
	// if we got a first alphabetical word, then assume that to be the
	// start of our title
	if ( textDoc && i < NW && n < MAX_TIT_CANDIDATES ) {
		// first word in title is "t0"
		int32_t t0 = i;
		// find end of first line
		int32_t numWords = 0;
		// set i to the end now. we MUST find a \n to terminate the
		// title, otherwise we will not have a valid title
		while (i < NW &&
		       numWords < maxTitleWords &&
		       (WW->isAlnum(i) ||
			!WW->hasChar(i, '\n'))){ 
			if(WW->isAlnum(i)) numWords++;
			i++;
		}
		// "t1" is the end
		int32_t t1 = -1; 
		// we must have found our \n in order to set "t1"
		if (i <= NW && numWords < maxTitleWords ) t1 = i;
		// set the ptrs
		cptrs   [n] =  WW;
		htmlEnc [n] =  true;
		// this is the last resort i guess...
		scores  [n] =  0.5;
		types   [n] =  TT_FIRSTLINE;
		as      [n] =  t0;
		bs      [n] =  t1;
		// add it as a candidate if t0 and t1 were valid
		if (t0 >= 0 && t1 > t0) n++;
	}

	//logf(LOG_DEBUG,"title: took4=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// now add the last url path to contain underscores or hyphens
	char *pstart = xd->getFirstUrl()->getPath();
	// get first url
	Url *fu = xd->getFirstUrl();
	// start at the end
	char *p = fu->getUrl() + fu->getUrlLen();
	// end pointer
	char *pend = NULL;
	// come up here for each path component
	while ( p >= pstart ) {
		// save end
		pend = p;
		// skip over /
		if ( *p == '/' ) p--;
		// now go back to next /
		int32_t count = 0;
		for ( ; p >= pstart && *p !='/' ; p-- ) 
			if ( *p == '_' || *p == '-' ) count++;
		// did we get it?
		if ( count > 0 ) break;
	}

	// did we get any?
	if ( p > pstart && n < MAX_TIT_CANDIDATES ) {
		// now set words to that
		if ( ! tw[ti].set ( p                        , // string
				    pend - p                 , // len
				    TITLEREC_CURRENT_VERSION ,
				    true       , // compute wordIds?
				    0          ))// niceness
			return false;
		// point to that
		cptrs   [n] = &tw[ti];
		as      [n] = 0;
		bs      [n] = tw[ti].getNumWords();
		htmlEnc [n] = false;
		scores  [n] = 1.0;
		types   [n] = TT_URLPATH;
		// increment since we are using it
		ti++;
		// advance
		n++;
	}

	// save old n
	int32_t oldn = n;
	// . do not split titles if we are a root url
	// . maps.yahoo.com was getting "Maps" for the title
	Url *tu = xd->getFirstUrl();
	if ( tu->isRoot    ()           ) oldn = -2;

	// point to list of \0 separated titles
	char *rootTitleBuf    = NULL;
	char *rootTitleBufEnd = NULL;

	bool doRootTitleRemoval = false;

	if ( ! xd->ptr_rootTitleBuf ) doRootTitleRemoval = false;

	// get the root title if we are not root!
	if ( doRootTitleRemoval ) { // xd->ptr_rootTitleBuf ) {
		// it should not block
		char **px = xd->getFilteredRootTitleBuf();
		// error?
		if ( ! px ) return false;
		// should never block! should be set from title rec basically
		if ( px == (void *)-1 ) { char *xx=NULL;*xx=0; }
		// point to list of \0 separated titles
		rootTitleBuf    = *px;
		rootTitleBufEnd =  *px + xd->m_filteredRootTitleBufSize;
	}


	Matches m;
	if ( rootTitleBuf && q ) m.setQuery ( q );

	// debug hack for 'spiritual books for women query'
	//rootTitleBuf = "Forbes.com";
	//rootTitleBufEnd = rootTitleBuf + gbstrlen(rootTitleBuf);

	// convert into an array
	int32_t nr = 0;
	char *pr = rootTitleBuf;
	char *rootTitles[20];
	int32_t  rootTitleLens[20];
	// loop over each root title segment
	for ( ; pr && pr < rootTitleBufEnd ; 
	      // sometimes roottitlebuf is missing terminating \0 for some
	      // reason. could be related to corruption in tagdb from
	      // corruption bug fixed a week ago, but handle it here.
	      // thanks to isj.
	      pr += strnlen(pr,(size_t)(rootTitleBufEnd-pr)) + 1 ) {
		// if we had a query...
		if ( q ) {
			// reset it
			m.reset();
			// see if root title segment has query terms in it
			m.addMatches ( pr,
				       gbstrlen(pr),
				       MF_TITLEGEN  ,
				       xd->m_docId  ,
				       m_niceness   );
			// if matches query, do NOT add it, we only add it for
			// removing from the title of the page...
			if ( m.getNumMatches() ) continue;
		}
		// point to it. it should start with an alnum already
		// since it is the "filtered" list of root titles...
		// if not, fix it in xmldoc then.
		rootTitles   [nr] = pr;
		rootTitleLens[nr] = gbstrlen(pr);
		// advance
		nr++;
		// no breaching
		if ( nr >= 20 ) break;
	}

	// TODO: fix this... put the isSiteRoot bit in title rec?
	//if ( tu->isSiteRoot(xd->m_coll) ) oldn = -2;
	// now split up candidates in children candidates by tokenizing
	// using :, | and - as delimters. 
	// the hyphen must have a space on at least one side, so "cd-rom" does
	// not create a pair of tokens...
	// FIX: for the title:
	// Best Careers 2009: Librarian - US News and World Report
	// we need to recognize "Best Careers 2009: Librarian" as a subtitle
	// otherwise we don't get it as the title. so my question is are we
	// going to have to do all the permutations at some point? for now
	// let's just add in pairs...
	for ( int32_t i = 0 ; i < oldn && n + 3 < MAX_TIT_CANDIDATES ; i++ ) {
		// stop if no root title segments
		if ( nr <= 0 ) break;
		// get the word info
		Words *w = cptrs[i];
		int32_t   a = as[i];
		int32_t   b = bs[i];
		// init
		int32_t lasta = a;
		char prev  = false;
		// char length in bytes
		//int32_t charlen = 1;
		// see how many we add
		int32_t added = 0;
		char *skipTo = NULL;
		bool qualified = true;
		// . scan the words looking for a token
		// . sometimes the candidates end in ": " so put in "k < b-1"
		// . made this from k<b-1 to k<b to fix 
		//   "Hot Tub Time Machine (2010) - IMDb" to strip IMDb
		for ( int32_t k = a ; k < b && n + 3 < MAX_TIT_CANDIDATES; k++){
			// get word
			char *wp = w->getWord(k);
			// skip if not alnum
			if ( ! w->isAlnum(k) ) {
				// in order for next alnum word to
				// qualify for "clipping" if it matches
				// the root title, there has to be more
				// than just spaces here, some punct.
				// otherwise title
				// "T. D. Jakes: Biography from Answers.com"
				// becomes
				// "T. D. Jakes: Biography from"
				qualified=isWordQualified(wp,w->getWordLen(k));
				continue;
			}
			// gotta be qualified!
			if ( ! qualified ) continue;
			// skip if in root title
			if ( skipTo && wp < skipTo ) continue;
			// does this match any root page title segments?
			int32_t j; 
			for ( j = 0 ; j < nr ; j++ ) {
				// . compare to root title
				// . break out if we matched!
				if ( ! strncmp( wp,
						rootTitles[j],
						rootTitleLens[j] ) )
					break;
			}
			// if we did not match a root title segment,
			// keep on chugging
			if ( j >= nr ) continue;
			// . we got a root title match!
			// . skip over
			skipTo = wp + rootTitleLens[j];
			// must land on qualified punct then!!
			int32_t e = k+1;
			for ( ; e<b && w->m_words[e]<skipTo ; e++ );
			// ok, word #e must be a qualified punct
			if ( e<b &&
			     ! isWordQualified(w->getWord(e),w->getWordLen(e)))
				// assume no match then!!
				continue;
			// if we had a previous guy, reset the end of the
			// previous candidate
			if ( prev ) {
				bs[n-2] = k;
				bs[n-1] = k;
			}
			// . ok, we got two more candidates
			// . well, only one more if this is not the 1st time
			if ( ! prev ) {
				cptrs   [n] = cptrs   [i];
				htmlEnc [n] = htmlEnc [i];
				scores  [n] = scores  [i];
				types   [n] = types   [i];
				as      [n] = lasta;
				bs      [n] = k;
				parent  [n] = i;
				n++;
				added++;
			}
			// the 2nd one
			cptrs   [n] = cptrs   [i];
			htmlEnc [n] = htmlEnc [i];
			scores  [n] = scores  [i];
			types   [n] = types   [i];
			as      [n] = e + 1;
			bs      [n] = bs      [i];
			parent  [n] = i;
			n++;
			added++;

			// now add in the last pair as a whole token
			cptrs   [n] = cptrs   [i];
			htmlEnc [n] = htmlEnc [i];
			scores  [n] = scores  [i];
			types   [n] = types   [i];
			as      [n] = lasta;
			bs      [n] = bs      [i];
			parent  [n] = i;
			n++;
			added++;

			// nuke the current candidate then since it got
			// split up to not contain the root title...
			//cptrs[i] = NULL;

			// update this
			lasta = k+1;
			// if we encounter another delimeter we will have
			// to revise bs[n-1], so note that
			prev = true;
		}

		// nuke the current candidate then since it got
		// split up to not contain the root title...
		if ( added ) {
			scores[i] = 0.001;
			//cptrs[i] = NULL;
		}

		// erase the pair if that there was only one token
		if ( added == 3 ) n--;
	}

	//logf(LOG_DEBUG,"title: took5=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();


	// set base score
	for ( int32_t i = 0 ; i < n ; i++ ) baseScore[i] = scores[i];

	// set # alnum words
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// point to the words
		Words *w = cptrs[i];
		// skip if got nuked above
		if ( ! w ) continue;
		// get the word boundaries
		int32_t a = as[i];
		int32_t b = bs[i];
		int32_t count = 0;
		// scan the words in this title candidate
		for ( int32_t j = a ; j < b ; j++ ) 
			if ( w->isAlnum(j) ) count++;
		// store it
		numAlnum[i] = count;
	}
	
	//
	// . now punish by 0.85 for every lower case non-stop word it has
	// . reward by 1.1 if has a non-stopword in the query
	//
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// point to the words
		Words *w = cptrs[i];
		// skip if got nuked above
		if ( ! w ) continue;
		// the word ptrs
		char **wptrs = w->getWordPtrs();
		// skip if empty
		if ( w->getNumWords() <= 0 ) continue;
		// get the word boundaries
		int32_t a = as[i];
		int32_t b = bs[i];
		// record the boosts
		float ncb = 1.0;
		float qtb = 1.0;
		// a flag
		char uncapped = false;
		// scan the words in this title candidate
		for ( int32_t j = a ; j < b ; j++ ) {
			// skip stop words
			if ( w->isQueryStopWord(j,xd->m_langId) ) continue;
			// punish if uncapitalized non-stopword
			if ( ! w->isCapitalized(j) ) uncapped = true;
			// skip if no query
			if ( ! q ) continue;
			// convert the word id into a term id
			//int64_ttermid=g_indexdb.getTermId(0,w->getWordId(j));
			int64_t wid = w->getWordId(j);
			// reward if in the query
			if ( q->getWordNum(wid) >= 0 ) {
				qtb       *= 1.5;
				scores[i] *= 1.5;
			}
		}
		// . only punish once if missing a capitalized word
		// . hurts us for:
		//   http://content-uk.cricinfo.com/ausvrsa2008_09/engine/
		//   current/match/351682.html
		if ( uncapped ) {
			ncb       *= 1.00;//0.85;
			scores[i] *= 1.00;//0.85;
		}
		// punish if a http:// title thingy
		char *s    = wptrs[a];//w->getWord(a);
		int32_t  size = w->getStringSize(a,b);
		if ( size > 9 && memcmp("http://",s,7)==0 )
			ncb *= .10;
		if ( size > 14 && memcmp("h\0t\0t\0p\0:\0/\0/",s,14)==0 )
			ncb *= .10;
		// set these guys
		scores     [i] *= ncb;
		noCapsBoost[i]  = ncb;
		qtermsBoost[i]  = qtb;
	}

	//logf(LOG_DEBUG,"title: took6=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// . now compare each candidate to the other candidates
	// . give a boost if matches
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// point to the words
		Words *w1 = cptrs[i];
		// skip if got nuked above
		if ( ! w1 ) continue;
		int32_t   a1 = as   [i];
		int32_t   b1 = bs   [i];	
		//int32_t   nw1 = b1 - a1;
		// reset our array
		//int32_t found[512];
		// sanity check
		//if ( nw1 > 512 ) { char *xx=NULL;*xx=0; };
		//memset ( found , 0 , 4*512);
		// reset some flags
		char localFlag1 = 0;
		char localFlag2 = 0;
		// record the boost
		float iccb = 1.0;
		// total boost
		float total = 1.0;
		//int32_t count = 0;
		// to each other candidate
		for ( int32_t j = 0 ; j < n ; j++ ) {
			// not to ourselves
			if ( j == i ) continue;
			// or our derivatives
			if ( parent[j] == i ) continue;
			// or derivates to their parent
			if ( parent[i] == j ) continue;
			// only check parents now. do not check kids.
			// this was only for when doing percent contained
			// not getSimilarity() per se
			//if ( parent[j] != -1 ) continue;
			//
			// TODO: do not accumulate boosts from a parent
			// and its kids, subtitles...
			//
			// do not compare type X to type Y
			if ( types[i] == TT_TITLETAG ) {
				if ( types[j] == TT_TITLETAG      ) continue;
			}
			// do not compare a div candidate to another div cand
			// http://friendfeed.com/foxiewire?start=30
			// likewise, a TD to another TD
			// http://content-uk.cricinfo.com/ausvrsa2008_09/
			// engine/match/351681.html ... etc.
			if ( types[i] == TT_BOLDTAG ||
			     types[i] == TT_HTAG    ||
			     types[i] == TT_DIVTAG  ||
			     types[i] == TT_TDTAG   ||
			     types[i] == TT_FONTTAG    ) {
				if ( types[j] == types[i] ) continue;
			}
			// . do not compare one kid to another kid
			// . i.e. if we got "x | y" as a title and "x | z"
			//   as a link text, it will emphasize "x" too much
			//   http://content-uk.cricinfo.com/ausvrsa2008_09/
			//   engine/current/match/351682.html
			if ( parent[j] != -1 && parent[i] != -1 ) continue;
			// . body type tags are mostly mutually exclusive
			// . for the legacy.com url mentioned below, we have
			//   good stuff in <td> tags, so this hurts us...
			// . but for the sake of 
			//   http://larvatusprodeo.net/2009/01/07/partisanship
			//   -politics-and-participation/ i put bold tags back
			//if ( types[i] == TT_BOLDTAG ) 
			//	if ( types[j] == TT_BOLDTAG       ) continue;
			/*
			if ( types[i] == TT_BOLDTAG ||
			     types[i] == TT_HTAG    ||
			     types[i] == TT_DIVTAG  ||
			     types[i] == TT_TDTAG   ||
			     types[i] == TT_FONTTAG    ) {
				if ( types[j] == TT_HTAG          ) continue;
				if ( types[j] == TT_BOLDTAG       ) continue;
				if ( types[j] == TT_DIVTAG        ) continue;
				if ( types[j] == TT_TDTAG         ) continue;
				if ( types[j] == TT_FONTTAG       ) continue;
			}
			*/
			if ( types[i] == TT_LINKTEXTLOCAL ) {
				if ( types[j] == TT_LINKTEXTLOCAL ) continue;
			}
			if ( types[i] == TT_RSSITEMLOCAL ) {
				if ( types[j] == TT_RSSITEMLOCAL ) continue;
			}
			// only compare to one local link text for each i
			if ( types[j] == TT_LINKTEXTLOCAL && localFlag1 )
				continue;
			if ( types[j] == TT_RSSITEMLOCAL  && localFlag2 )
				continue;
			if ( types[j] == TT_LINKTEXTLOCAL ) localFlag1 = 1;
			if ( types[j] == TT_RSSITEMLOCAL  ) localFlag2 = 1;
			// not link title attr to link title attr either
			// fixes http://www.spiritualwoman.net/?cat=191
			if ( types[i] == TT_TITLEATT &&
			     types[j] == TT_TITLEATT )
				continue;
			// get our words
			Words *w2 = cptrs[j];
			// skip if got nuked above
			if ( ! w2 ) continue;
			int32_t   a2 = as   [j];
			int32_t   b2 = bs   [j];
			// use body scores if we can
			//Scores *scores1 = NULL;
			//Scores *scores2 = NULL;
			//if ( w1 == WW ) scores1 = SS;
			//if ( w2 == WW ) scores2 = SS;
			/*
			// make his hashtable
			HashTable ht;
			char hbuf[5000];
			// but we cannot have more than 1024 slots then
			if ( ! ht.set ( 256 , hbuf,5000) ) return false;
			// and table auto grows when 90% full, so limit us here
			int32_t count    = 0;
			// loop over all words in "w1" and hash them
			for ( int32_t k = a2 ; k < b2 && count<128; k++ ) {
				// the word id
				int32_t wid = (int32_t) w2->m_wordIds[k] ;
				// skip if not indexable
				if ( wid == 0 ) continue;
				// count it
				count++;
				// add to table
				if ( ! ht.addKey ( (int32_t)wid , 1 , NULL ) ) 
					return false;
			}
			// which words are found in another candidate
			for ( int32_t k = 0 ; k < nw1 ; k++ ) {
				// get word id
				int32_t wid = (int32_t)w1->m_wordIds[a1 + k];
				// skip if punct. set it to -1
				if ( wid == 0LL ) { found[k] = -1; continue; }
				// see if in table
				int32_t slot = ht.getSlot ( wid );
				// this word was found in another candidate
				if ( slot >= 0 ) found[k]++;
			}
			*/
			// how similar is title #i to title #j ?
			float fp = getSimilarity ( w2 , a2 , b2 ,
						   w1 , a1 , b1 );
			                          // TODO: scores1 , scores2 );
			// error?
			if ( fp == -1.0 ) return false;
			// give a 1.1 boost per word i guess
			//float boost = 1.0;
			// get # of "matched words" in the two titles
			//int32_t nw1 = (int32_t)(fp * (float)numAlnum[i]);
			//for ( int32_t v = 0 ; v < nw1 ; v++ )
			//	boost *= 1.1;
			// custom boosting...
			float boost = 1.0;
			if      ( fp >= .95 ) boost = 3.0;
			else if ( fp >= .90 ) boost = 2.0;
			else if ( fp >= .85 ) boost = 1.5;
			else if ( fp >= .80 ) boost = 1.4;
			else if ( fp >= .75 ) boost = 1.3;
			else if ( fp >= .70 ) boost = 1.2;
			else if ( fp >= .60 ) boost = 1.1;
			else if ( fp >= .50 ) boost = 1.08;
			else if ( fp >= .40 ) boost = 1.04;
			// limit total
			total *= boost;
			if ( total > 100.0 ) break;
			// if you are matching the url path, that is pretty 
			// good so give more!
			// actually, that would hurt:
			// http://michellemalkin.com/2008/12/29/gag-worthy/
			/*
			if ( types[j] == TT_URLPATH ) {
				float delta = boost - 1.0;
				// double the delta boost
				boost = boost + delta;
			}
			*/
			// . boost by that!
			// . if 100% similar give x3.0
			// . if 0% similar x1.0
			//float boost = 1.0 + (2.0 * fp);
			//float boost = ((1.0 + fp)*(1.0 + fp));
			// custom boosting!
			if ( fp > 0.0 && g_conf.m_logDebugTitle )
				logf(LOG_DEBUG,"title: i=%"INT32" j=%"INT32" fp=%.02f "
				     "b=%.02f", i,j,fp,boost);
			// apply it
			scores[i] *= boost;
			iccb      *= boost;
		}
		// . boost from words that word found in other candidates
		// . TODO: dedup the found vector so we don't count the same
		//   word twice!!
		/*
		float boost = 1.0;
		for ( int32_t k = 0 ; k < nw1 ; k++ ) {
			// skip punct
			if ( found[k] == -1 ) continue;
			// boost or punish
			if ( found[k] ) boost *= 1.20;
			else            boost *= 0.85;
		}
		// assigne
		scores           [i] = boost;
		inCommonCandBoost[i] = boost;
		*/
		inCommonCandBoost[i] = iccb;
	}

	//logf(LOG_DEBUG,"title: took7=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();


	// loop over all n candidates
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// skip if not in the document body
		if ( cptrs[i] != WW ) continue;
		// point to the words
		int32_t       a1    = as   [i];
		int32_t       b1    = bs   [i];
		// . loop through this candidates words
		// . TODO: use memset here?
		for ( int32_t j = a1 ; j <= b1 && j < NW ; j++ )
			// flag it
			flags[j] |= 0x01;
	}

	Section **sp = NULL;
	if ( sections ) sp = sections->m_sectionPtrs;

	//logf(LOG_DEBUG,"title: took8=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	/*
		  MDW: removed since SEC_ARTICLE was removed ----
	// . now compare each candidate to the words in the positive scoring
	//   body of the document.
	// . hash each word in the document with a positive score
	// . go up to the first 5000 "words"
	// . hash up to 1000 "words"
	HashTableT <int64_t,int32_t> ht;
	inLink = false;
	for ( int32_t i = 0 ; i < NW && i < 5000 ; i++ ) {
		// see whose in a link tag
		if ( tids[i] == TAG_A           ) inLink = true;
		if ( tids[i] == (TAG_A | BACKBIT) ) inLink = false;
		// must be alnum word
		if ( wids[i] == 0LL ) continue;
		// skip if not in article section
		if ( sp && ! (sp[i]->m_flags & SEC_ARTICLE ) ) continue;
		// skip if 0 score
		//if ( SS && SS->m_scores[i] <= 0 ) continue;
		// . skip if this word is in a candidate title
		// . for http://www.legacy.com/shelbystar/Obituaries.asp?Pa
		//   ge=LifeStory&PersonId=122245831
		//   the body is actually a <td> candidate and the first
		//   td candidate is a good title and is unable to get boost
		//   from the body because it is a <td> candidate! so remove
		//   this logic for now
		if ( flags[i] & 0x01 ) continue;
		// or in a link as determined with the flags
		if ( flags[i] & 0x02 ) continue;
		// skip if in a link
		if ( inLink ) continue;
		// skip if stop word
		if ( WW->isQueryStopWord(i) ) continue;
		// . hash it. return false if error adding it.
		// . store the word # so we can avoid comparing to ourselves
		//   in case the title candidate intersect this part of the doc
		if ( ! ht.addKey ( wids[i] , i ) ) {
			if ( flags!=localBuf ) mfree (flags,need,"TITLEflags");
			return false;
		}
	}
	*/
	// free our stuff
	if ( flags!=localBuf ) mfree (flags,need, "TITLEflags");

	//logf(LOG_DEBUG,"title: took9=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// ok, now compare each candidate to that hash table
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// record the boost
		float icbb = 1.0;
		/*
		  MDW: removed since SEC_ARTICLE was removed ----
		// point to the words
		Words     *w1    = cptrs[i];
		int32_t       a1    = as   [i];
		int32_t       b1    = bs   [i];
		int32_t       nw1   = w1->getNumWords();
		int64_t *wids1 = w1->getWordIds ();
		// loop through this candidates words
		for ( int32_t j = a1 ; j <= b1 && j < nw1 ; j++ ) {
			// skip if not alnum
			if ( wids1[j] == 0LL ) continue;
			// is it in the positive scoring body?
			if ( ! ht.getValuePtr ( wids1[j] ) ) continue;
			// boost score by 20% for every term we have that
			// is also in the positive scoring body
			icbb      *= 1.20;
			scores[i] *= 1.20;
		}
		*/
		inCommonBodyBoost[i] = icbb;
	}			

	//logf(LOG_DEBUG,"title: took10=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// now get the highest scoring candidate title
	float max    = -1.0;
	int32_t  winner = -1;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// skip if got nuked
		if ( ! cptrs[i] ) continue;
		if ( winner != -1 && scores[i] <= max ) continue;
		// url path's cannot be titles in and of themselves
		if ( types[i] == TT_URLPATH ) continue;
		// skip if empty basically, like if title was exact
		// copy of root, then the whole thing got nuked and
		// some empty string added, where a > b
		if ( as[i] >= bs[i] ) continue;
		// got one
		max = scores[i];
		// save it
		winner = i;
	}

	//logf(LOG_DEBUG,"title: took11=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// if we are a root, always pick the title tag as the title
	if ( oldn == -2 && tti >= 0 ) winner = tti;

	// if no winner, all done. no title
	if ( winner == -1 ) return true;

	// point to the words class of the winner
	Words *w = cptrs[winner];
	// skip if got nuked above
	if ( ! w ) { char *xx=NULL;*xx=0; }

	// make the Pos class of the winner, and point "pp" to it
	Pos    *pp = POS;
	//Scores *ss = SS;

	// need to make our own Pos class if title not from body
	Pos  tp;
	if ( w != WW ) {
		// use the temp Pos class, "tp"
		pp = &tp;
		// use no scores then
		//ss = NULL;
		// set "Scores" ptr to NULL. we assume all are positive scores
		if ( ! tp.set ( w , NULL ) ) return false;
	}
	// the string ranges from word #a up to and including word #b
	int32_t a = as[winner];
	int32_t b = bs[winner];
	// sanity check
	if ( a < 0 || b > w->getNumWords() ) { char*xx=NULL;*xx=0; }
	// save the title
	if ( ! copyTitle ( w , pp , a , b , sections ) ) 
		return false;

	// save these
	m_htmlEncoded = htmlEnc [winner];

	//logf(LOG_DEBUG,"title: took12=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// return now if no need to log this stuff
	//SafeBuf sb;
	//pbuf = &sb;
	if ( ! pbuf ) return true;

	//log("title: candidates for %s",xd->getFirstUrl()->getUrl() );

	pbuf->safePrintf("<table cellpadding=5 border=2><tr>"
			 "<td colspan=20><center><b>Title Generation</b>"
			 "</center></td>"
			 "</tr>\n<tr>"
			 "<td>#</td>"
			 "<td>type</td>"
			 "<td>parent</td>"
			 "<td>base score</td>"
			 "<td>format penalty</td>"
			 "<td>query term boost</td>"
			 "<td>candidate intersection boost</td>"
			 "<td>body intersection boost</td>"
			 "<td>FINAL SCORE</td>"
			 "<td>title</td>"
			 "</tr>\n" );
			 

	// print out all candidates
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char *ts = "unknown";
		if ( types[i] == TT_LINKTEXTLOCAL  ) ts = "local inlink text";
		if ( types[i] == TT_LINKTEXTREMOTE ) ts = "remote inlink text";
		if ( types[i] == TT_RSSITEMLOCAL   ) ts = "local rss title";
		if ( types[i] == TT_RSSITEMREMOTE  ) ts = "remote rss title";
		if ( types[i] == TT_BOLDTAG        ) ts = "bold tag";
		if ( types[i] == TT_HTAG           ) ts = "header tag";
		if ( types[i] == TT_TITLETAG       ) ts = "title tag";
		if ( types[i] == TT_DMOZ           ) ts = "dmoz title";
		if ( types[i] == TT_FIRSTLINE      ) ts = "first line in text";
		if ( types[i] == TT_FONTTAG        ) ts = "font tag";
		if ( types[i] == TT_ATAG           ) ts = "anchor tag";
		if ( types[i] == TT_DIVTAG         ) ts = "div tag";
		if ( types[i] == TT_TDTAG          ) ts = "td tag";
		if ( types[i] == TT_PTAG           ) ts = "p tag";
		if ( types[i] == TT_URLPATH        ) ts = "url path";
		if ( types[i] == TT_TITLEATT       ) ts = "title attribute";
		// get the title
		pbuf->safePrintf(
				 "<tr>"
				 "<td>#%"INT32"</td>"
				 "<td><nobr>%s</nobr></td>"
				 "<td>%"INT32"</td>" 
				 "<td>%0.2f</td>" // baseScore
				 "<td>%0.2f</td>"
				 "<td>%0.2f</td>"
				 "<td>%0.2f</td>"
				 "<td>%0.2f</td>"
				 "<td>%0.2f</td>"
				 "<td>",
				 i,
				 ts ,
				 parent[i],
				 baseScore[i],
				 noCapsBoost[i],
				 qtermsBoost[i],
				 inCommonCandBoost[i],
				 inCommonBodyBoost[i],
				 scores[i]);
		// ptrs
		Words *w = cptrs[i];
		int32_t   a = as[i];
		int32_t   b = bs[i];
		// skip if no words
		if ( w->getNumWords() <= 0 ) continue;
		// the word ptrs
		char **wptrs = w->getWordPtrs();
		// string ptrs
		char *ptr  = wptrs[a];//w->getWord(a);
		int32_t  size = w->getStringSize(a,b);
		// it is utf8
		pbuf->safeMemcpy ( ptr , size );
		// end the line
		pbuf->safePrintf("</td></tr>\n");
	}

	pbuf->safePrintf("</table>\n<br>\n");

	//logf(LOG_DEBUG,"title: took13=%"INT64"",gettimeofdayInMilliseconds()-x);
	//x = gettimeofdayInMilliseconds();

	// log these for now
	//log("title: %s",sb.getBufStart());
		
	return true;
}

// . returns 0.0 to 1.0
// . what percent of the alnum words in "w1" are in "w2" from words in [t0,t1)
// . gets 50% points if has all single words, and the other 50% if all phrases
// . Scores class applies to w1 only, use NULL if none
// . use word popularity information for scoring rarer term matches more
// . ONLY CHECKS FIRST 1000 WORDS of w2 for speed
float Title::getSimilarity ( Words  *w1 , int32_t i0 , int32_t i1 ,
			     Words  *w2 , int32_t t0 , int32_t t1 ) {
	// if either empty, that's 0% contained
	if ( w1->getNumWords() <= 0 ) return 0;
	if ( w2->getNumWords() <= 0 ) return 0;
	if ( i0 >= i1 ) return 0;
	if ( t0 >= t1 ) return 0;
	// invalids vals
	if ( i0 < 0   ) return 0;
	if ( t0 < 0   ) return 0;
	// . for this to be useful we must use idf
	// . get the popularity of each word in w1
	// . w1 should only be a few words since it is a title candidate
	// . does not add pop for word #i if scores[i] <= 0
	// . take this out for now since i removed the unified dict,
	//   we could use this if we added popularity to g_wiktionary
	//   but it would have to be language dependent
	Pops pops1;
	Pops pops2;
	if ( ! pops1.set ( w1 , i0 , i1 ) ) return -1.0;
	if ( ! pops2.set ( w2 , t0 , t1 ) ) return -1.0;
	// now hash the words in w1, the needle in the haystack
	int32_t nw1 = w1->getNumWords();
	if ( i1 > nw1 ) i1 = nw1;
	HashTable table;
	//int32_t *ss1 = NULL;
	//int32_t *ss2 = NULL;
	//if ( scores1 ) ss1 = scores1->m_scores;
	//if ( scores2 ) ss2 = scores2->m_scores;
	// this augments the hash table
	//int64_t lastWids[1024];
	int64_t lastWid   = -1;
	float     lastScore = 0.0;
	// but we cannot have more than 1024 slots then
	if ( ! table.set ( 1024 ) ) return -1.0;
	// and table auto grows when 90% full, so limit us here
	int32_t count    = 0;
	int32_t maxCount = 20; // (1024 * 90) / 100 - 1;
	// sum up everything we add
	float sum = 0.0;
	// loop over all words in "w1" and hash them
	for ( int32_t i = i0 ; i < i1 ; i++ ) {
		// the word id
		int64_t wid = (int32_t) w1->m_wordIds[i] ;
		// skip if not indexable
		if ( wid == 0 ) continue;
		// or score is 0
		//if ( ss && ss[i] <= 0 ) continue;
		// no room left in table!
		if ( count++ > maxCount ) {
			//logf(LOG_DEBUG, "query: Hash table for title "
			//    "generation too small. Truncating words from w1.");
			break;
		}
		// . map pop to a score, "pscore"
		// . the least popular something is the more points it is worth
		//val = MAX_POP - pops.m_pops[i];
		// . make this a float. it ranges from 0.0 to 1.0
		// . 1.0 means the word occurs in 100% of documents sampled
		// . 0.0 means it occurs in none of them
		// . but "val" is the complement of those two statements!
		float score = 1.0 - pops1.getNormalizedPop(i);
		// accumulate
		sum += score;
		// debug
		//logf(LOG_DEBUG,"adding wid=%"INT32" score=%.02f sum=%.02f",
		//     (int32_t)wid,score,sum);
		// accumulate for scoring phrases too! (adjacent words)
		//psum += val;
		// update the linked list
		//if ( oldi < 1024 ) next[oldi] = i;
		// prepare for next link, it may never come if we're last one!
		//oldi = i;
		// add to table
		if ( ! table.addKey ( (int32_t)wid , (int32_t)score , NULL ) ) 
			return -1.0;
		// if no last wid, continue
		if ( lastWid == -1LL ) {lastWid=wid;lastScore=score;continue; }
		// keep this 1-1 with the hash table slots
		//lastWids [ slot ] = lastWid;
		// . what was his val?
		// . the "val" of the phrase: 
		float phrScore = score + lastScore;
		// do not count as much as single words
		phrScore *= 0.5;
		// accumulate
		sum += phrScore;
		// get the phrase id
		int64_t pid = hash64 ( wid , lastWid );
		// debug
		//logf(LOG_DEBUG,
		//     "adding pid=%"INT32" score=%.02f sum=%.02f",
		//	     (int32_t)pid,phrScore,sum);
		// now add that
		if ( ! table.addKey ( (int32_t)pid , (int32_t)phrScore , NULL ) )
			return -1.0;
		// we are now the last wid
		lastWid   = wid;
		lastScore = score;
	}
	// sanity check. it can't grow cuz we keep lastWids[] 1-1 with it
	if ( table.getNumSlots() != 1024 ) {
		log(LOG_LOGIC,"query: Title has logic bug.");
		return -1.0;
	}

	// reset score sum to get "percent contained" functionality back
	//sum = 0.0;

	// accumulate scores of words that are found
	float found = 0.0;
	// reset
	lastWid = -1LL;
	// loop over all words in "w1" and hash them
	for ( int32_t i = t0 ; i < t1 ; i++ ) {
		// the word id
		int64_t wid = (int32_t) w2->m_wordIds[i] ;
		// skip if not indexable
		if ( wid == 0 ) continue;
		// or score is 0
		//if ( ss && ss[i] <= 0 ) continue;
		// . make this a float. it ranges from 0.0 to 1.0
		// . 1.0 means the word occurs in 100% of documents sampled
		// . 0.0 means it occurs in none of them
		// . but "val" is the complement of those two statements!
		float score = 1.0 - pops2.getNormalizedPop(i);
		// accumulate
		sum += score;
		// is it in table? 
		int32_t slot = table.getSlot ( (int32_t)wid ) ;
		// . if in table, add that up to "found"
		// . we essentially find his wid AND our wid, so 2.0 times
		if ( slot >= 0 ) found += 2.0 * score;
		// use percent contained functionality now
		//if ( slot >= 0 ) found += score;
		// debug
		//logf(LOG_DEBUG,"checking wid=%"INT32" score=%.02f sum=%.02f "
		//   "found=%.02f slot=%"INT32"",   (int32_t)wid,score,sum,found,slot);
		// now the phrase
		if ( lastWid == -1LL ) {lastWid=wid;lastScore=score;continue;}
		// . what was his val?
		// . the "val" of the phrase: 
		float phrScore = score + lastScore;
		// do not count as much as single words
		phrScore *= 0.5;
		// accumulate
		sum += phrScore;
		// get the phrase id
		int64_t pid = hash64 ( wid , lastWid );
		// is it in table? 
		slot = table.getSlot ( (int32_t)pid ) ;
		// . accumulate if in there
		// . we essentially find his wid AND our wid, so 2.0 times
		if ( slot >= 0 ) found += 2.0 * phrScore;
		// use percent contained functionality now
		//if ( slot >= 0 ) found += score;
		// we are now the last wid
		lastWid   = wid;
		lastScore = score;
		// debug
		//logf(LOG_DEBUG,
		//     "checking pid=%"INT32" score=%.02f sum=%.02f found=%.02f "
		//     "slot=%"INT32"",
		//     (int32_t)pid,phrScore,sum,found,slot);
	}

	// do not divide by zero
	if ( sum == 0.0 ) return 0.0;
	// sanity check
	//if ( found > sum              ) { char *xx=NULL;*xx=0; }
	if ( found < 0.0 || sum < 0.0 ) { char *xx=NULL;*xx=0; }
	// . return the percentage matched
	// . will range from 0.0 to 1.0
	return found / sum;
}

// . copy just words in [t0,t1)
// . returns false on error and sets g_errno
bool Title::copyTitle ( Words *w , Pos *pos ,
			int32_t t0 , int32_t t1 ,
			Sections *sections ) {

	// skip initial punct
	//int64_t  *wids      = w->m_wordIds;
	//nodeid_t *tids      = w->m_tagIds;
	char      **wp        = w->m_words;
	int32_t       *wlens     = w->m_wordLens;
	int32_t        nw        = w->m_numWords;

	// sanity check
	if ( t1 < t0 ) { char *xx = NULL; *xx = 0; }

	// don't breech number of words
	if ( t1 > nw ) t1 = nw;

	// no title?
	if ( nw == 0 || t0 == t1 ) { reset(); return true; }

	char *end = wp[t1-1] + wlens[t1-1] ;

	// allocate title
	int32_t need = end - wp[t0];

	// . max bytes we'll need
	// . no, all "chars" could be encoded so they take up like 5 bytes each
	//int32_t max = m_maxTitleChars;
	// truncate the bytes to allocate if we can, based on m_maxTitleChars
	//if ( need > max ) need = max;
	// add 3 bytes for "..." and 1 for \0
	need += 5;

	// assume we can use our local buf
	m_title = m_localBuf;
	// if it is too small, then we must allocate
	if ( need >= TITLE_LOCAL_SIZE ) {
		m_title = (char *)mmalloc ( need , "Title" );
		m_titleAllocSize = need;
	}
	// return false if could not alloc mem to hold the title
	if ( ! m_title ) {
		m_titleBytes = 0;
		log("query: Could not alloc %"INT32" bytes for title.",need);
		return false;
	}
	// save for freeing later
	m_titleAllocSize = need;

	// point to the title to transcribe
	char *src    = wp[t0];
	char *srcEnd = end;

	// include a \" or \'
	if ( t0>0 && 
	     (src[-1] == '\'' || src[-1] == '\"' ) )
		src--;
	// and remove terminating | or :
	for ( ; 
	      srcEnd > src && 
		      (srcEnd[-1] == ':' || 
		       srcEnd[-1] == ' ' ||
		       srcEnd[-1] == '-' ||
		       srcEnd[-1] == '\n' ||
		       srcEnd[-1] == '\r' ||
		       srcEnd[-1] == '|'   )    ; 
	      srcEnd-- );

	// store in here
	char *dst    = m_title;
	// leave room for "...\0"
	char *dstEnd = m_title + m_titleAllocSize - 4;
	// size of character in bytes, usually 1
	char cs ;
	// point to last punct char
	char *lastp = dst;//NULL;
	// convert them always for now
	bool convertHtmlEntities = true;
	int32_t charCount = 0;
	// copy the node @p into "dst"
	for ( ; src < srcEnd ; src += cs , dst += cs ) {
		// get src size
		cs = getUtf8CharSize ( src );
		// break if we are full!
		if ( dst + cs >= dstEnd ) break;
		// or hit our max char limit
		if ( charCount++ >= m_maxTitleChars ) break;
		// remember last punct for cutting purposes
		if ( ! is_alnum_utf8 ( src ) ) lastp = dst;
		// encode it as an html entity if asked to
		if ( *src == '<' && convertHtmlEntities ) {
			if ( dst + 4 >= dstEnd ) break;
			gbmemcpy ( dst , "&lt;" , 4 );
			dst += 4 - cs;
			continue;
		}
		// encode it as an html entity if asked to
		if ( *src == '>' && convertHtmlEntities ) {
			if ( dst + 4 >= dstEnd ) break;
			gbmemcpy ( dst , "&gt;" , 4 );
			dst += 4 - cs;
			continue;
		}
		// if more than 1 byte in char, use gbmemcpy
		if ( cs == 1 ) *dst = *src;
		else           gbmemcpy ( dst , src , cs );
	}

	// null term always
	*dst = '\0';
	
	// do not split a word in the middle!
	if ( src < srcEnd ) { 
		if ( lastp ) {
			gbmemcpy ( lastp , "...\0" , 4 );
			dst = lastp + 3;
		}
		else {
			gbmemcpy ( dst   , "...\0" , 4 );
			dst += 3;
		}
	}

	// set size. does not include the terminating \0
	m_titleBytes = dst - m_title;

	return true;
}
