// print events should print &nbsp; if nothing else to print

// when a div tag's parent truncates its section, it may have been
// paired up with a div back tag which then should become free...
// that is the problem... because those back tags are unpaired.
// so your parent should constrain you as SOON as it is constrained and
// close you up at that point. that way you cannot falsely pair-claim
// a div back tag.


#include "Sections.h"
#include "Url.h"
#include "Words.h"
#include "Msg40.h"
#include "Conf.h"
#include "Msg1.h" // getGroupId()
//#include "HashTableX.h"
#include "XmlDoc.h"
#include "Bits.h"
#include "sort.h"
#include "Abbreviations.h"

#define MIN_VOTERS 10
//#define MAX_VOTERS 200

//static void gotSectiondbListWrapper ( void *state );
//static int cmp ( const void *s1 , const void *s2 ) ;

#define SECTIONDB_MINRECSIZES 5000000

Sections::Sections ( ) {
	m_sections = NULL;
	m_buf      = NULL;
	m_buf2     = NULL;
	reset();
}

void Sections::reset() {
	//if ( m_sections && m_needsFree )
	//	mfree ( m_sections , m_sectionsBufSize , "Sections" );
	m_sectionBuf.purge();
	m_sectionPtrBuf.purge();
	if ( m_buf && m_bufSize )
		mfree ( m_buf , m_bufSize , "sdata" );
	if ( m_buf2 && m_bufSize2 )
		mfree ( m_buf2 , m_bufSize2 , "sdata2" );
	// reset the old and new section voting tables
	//m_osvt.reset();
	//m_nsvt.reset();
	m_sections         = NULL;
	m_buf              = NULL;
	m_buf2             = NULL;
	m_bits             = NULL;
	m_numSections      = 0;
	m_numSentenceSections = 0;
	m_badHtml          = false;
	m_aa               = NULL;
	m_sentFlagsAreSet  = false;
	m_addedImpliedSections = false;
	m_setRegBits       = false;
	m_rootSection      = NULL;
	m_lastSection      = NULL;
	m_lastAdded        = NULL;

	m_numLineWaiters   = 0;
	m_waitInLine       = false;
	m_hadArticle       = false;
	m_articleStartWord = -2;
	m_articleEndWord   = -2;
	m_recall           = 0;
	//m_totalSimilarLayouts = 0;
	m_numVotes = 0;
	m_nw = 0;
	m_firstSent = NULL;
	m_lastSent  = NULL;
	m_sectionPtrs = NULL;
	m_firstDateValid = false;
	m_alnumPosValid = false;
}

Sections::~Sections ( ) {
	reset();
}

// for debug watch point
class Sections *g_sections = NULL;
class Section *g_sec = NULL;

#define TXF_MATCHED 1

// an element on the stack is a Tag
class Tagx {
public:
	// id of the fron tag we pushed
	nodeid_t m_tid;
	// cumulative hash of all tag ids containing this one, includes us
	//int32_t     m_cumHash;
	// section number we represent
	int32_t     m_secNum;
	// hash of all the alnum words in this section
	//int32_t     m_contentHash;
	// set to TXF_MATCHED
	char     m_flags;
};

// i lowered from 1000 to 300 so that we more sensitive to malformed pages
// because typically they seem to take longer to parse. i also added some
// new logic for dealing with table tr and td back tags that allow us to
// pop off the other contained tags right away rather than delaying it until
// we are done because that will often breach this stack.
#define MAXTAGSTACK 300

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
// . sets m_sections[] array, 1-1 with words array "w"
// . the Weights class can look at these sections and zero out the weights
//   for words in script, style, select and marquee sections
bool Sections::set ( Words     *w                       ,
		     Phrases   *phrases                 ,
		     Bits      *bits                    ,
		     Url       *url                     ,
		     int64_t  docId                   ,
		     int64_t  siteHash64              ,
		     char      *coll                    ,
		     int32_t       niceness                ,
		     void      *state                   ,
		     void     (*callback)(void *state)  ,
		     uint8_t    contentType             ,
		     Dates     *dates                   ,
		     // from XmlDoc::ptr_sectionsData in a title rec
		     char      *sectionsData            ,
		     bool       sectionsDataValid       ,
		     char      *sectionsVotes           ,
		     //uint64_t   tagPairHash             ,
		     char      *buf                     ,
		     int32_t       bufSize                 ) {

	reset();

	if ( ! w ) return true;

	if ( w->m_numWords > 1000000 ) {
		log("sections: over 1M words. skipping sections set for "
		    "performance.");
		return true;
	}

	// save it
	m_words           = w;
	m_bits            = bits;
	m_dates           = dates;
	m_url             = url;
	m_docId           = docId;
	m_siteHash64      = siteHash64;
	m_coll            = coll;
	m_state           = state;
	m_callback        = callback;
	m_niceness        = niceness;
	//m_tagPairHash     = tagPairHash;
	m_contentType     = contentType;

	// reset this just in case
	g_errno = 0;

	if ( w->getNumWords() <= 0 ) return true;

	// int16_tcuts
	int64_t   *wids  = w->getWordIds  ();
	nodeid_t    *tids  = w->getTagIds   ();
	int32_t           nw  = w->getNumWords ();
	char      **wptrs  = w->getWords    ();
	int32_t        *wlens = w->getWordLens ();

	// set these up for isDelimeter() function to use and for
	// isCompatible() as well
	m_wids  = wids;
	m_wlens = wlens;
	m_wptrs = wptrs;
	m_tids  = tids;
	m_pids  = phrases->getPhraseIds2();


	m_isRSSExt = false;
	char *ext = m_url->getExtension();
	if ( ext && strcasecmp(ext,"rss") == 0 ) m_isRSSExt = true;
	if ( m_contentType == CT_XML ) m_isRSSExt = true;

	// are we a trumba.com url? we allow colons in sentences for its
	// event titles so that we get the correct event title. fixes
	// tumba.com "Guided Nature Walk : ..." title
	char *dom  = m_url->getDomain();
	int32_t  dlen = m_url->getDomainLen();
	m_isTrumba     = false;
	m_isFacebook   = false;
	m_isEventBrite = false;
	m_isStubHub    = false;
	if ( dlen == 10 && strncmp ( dom , "trumba.com" , 10 ) == 0 )
		m_isTrumba = true;
	if ( dlen == 12 && strncmp ( dom , "facebook.com" , 12 ) == 0 )
		m_isFacebook = true;
	if ( dlen == 11 && strncmp ( dom , "stubhub.com" , 11 ) == 0 )
		m_isStubHub = true;
	if ( dlen == 14 && strncmp ( dom , "eventbrite.com" , 14 ) == 0 )
		m_isEventBrite = true;

	// . how many sections do we have max?
	// . init at one to count the root section
	int32_t max = 1;
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// . count all front tags
		// . count twice since it can terminate a SEC_SENT sentence sec
		//if ( tids[i] && !(tids[i]&BACKBIT) ) max += 2;
		// count back tags too since some url 
		// http://www.tedxhz.com/tags.asp?id=3919&id2=494 had a bunch
		// of </p> tags with no front tags and it cored us because
		// m_numSections > m_maxNumSections!
		if ( tids[i] ) max += 2; // && !(tids[i]&BACKBIT) ) max += 2;
		// or any hr tag
		//else if ( tids[i] == (BACKBIT|TAG_HR) ) max += 2;
		//else if ( tids[i] == (BACKBIT|TAG_BR) ) max += 2;
		// . any punct tag could have a bullet in it...
		// . or if its a period could make a sentence section
		//else if ( ! tids[i] && ! wids[i] ) {
		else if ( ! wids[i] ) {
			// only do not count simple spaces
			if ( m_wlens[i] == 1 && is_wspace_a(m_wptrs[i][0]))
				continue;
			// otherwise count it as sentence delimeter
			max++;
		}
	}
	// . then \0 allows for a sentence too!
	// . fix doc that was just "localize-sf-prod\n"
	max++;
	// and each section may create a sentence section
	max *= 2;

	// truncate if excessive. growSections() will kick in then i guess
	// if we need more sections.
	if ( max > 1000000 ) {
		log("sections: truncating max sections to 1000000");
		max = 1000000;
	}

	//max += 5000;
	int32_t need = max * sizeof(Section);


	// and we need one section ptr for every word!
	//need += nw * 4;
	// and a section ptr for m_sorted[]
	//need += max * sizeof(Section *);
	// set this
	m_maxNumSections = max;

	// breathe
	QUICKPOLL(m_niceness);

	m_sectionPtrBuf.setLabel("psectbuf");

	// separate buf now for section ptr for each word
	if ( ! m_sectionPtrBuf.reserve ( nw *sizeof(Section *)) ) return true;
	m_sectionPtrs = (Section **)m_sectionPtrBuf.getBufStart();
	m_sectionPtrsEnd = (Section **)m_sectionPtrBuf.getBufEnd();

	// allocate m_sectionBuf
	m_sections = NULL;

	m_sectionBuf.setLabel ( "sectbuf" );

	if ( ! m_sectionBuf.reserve ( need ) )
		return true;

	// point into it
	m_sections = (Section *)m_sectionBuf.getBufStart();

	/*
	// assume no malloc
	m_needsFree = false;
	if      ( need < SECTIONS_LOCALBUFSIZE ) {
		m_sections        = (Section *)m_localBuf;
		m_sectionsBufSize = SECTIONS_LOCALBUFSIZE;
		//memset ( m_sections , 0 , m_sectionsBufSize );
	}
	else if ( need < bufSize               ) {
		m_sections        = (Section *)buf;
		m_sectionsBufSize = bufSize;
		//memset ( m_sections , 0 , m_sectionsBufSize );
	}
	else {
		m_sections = (Section *)mmalloc ( need , "Sections" );
		m_sectionsBufSize = need;
		m_needsFree       = true;
	}
	*/

	// clear it nicely
	//memset_nice ( m_sections , 0 , m_sectionsBufSize, m_niceness );

	// reset
	m_numAlnumWordsInArticle = 0;

	m_titleStart = -1;
	m_titleEnd   = -1;

	// bail if no luck
	//if ( ! m_sections ) return true;

	// point to buf
	//char *ppp = (char *)m_sections;
	// skip Sections array
	//ppp += max * sizeof(Section);
	// assign space for m_sorted
	//m_sorted = (Section **)ppp;
	// skip that
	//ppp += max * sizeof(Section *);
	// assign space for our ptrs that are 1-1 with the words array
	//m_sectionPtrs = (Section **)ppp;
	// the end
	//m_sectionPtrsEnd = (Section **)(ppp + nw * 4);
	// save this too
	m_nw = nw;

	// reset just in case
	//m_numSections = 0;

	// initialize the ongoing cumulative tag hash
	//int32_t h = 0;
	//int32_t h2 = 0;
	// the content hash ongoing
	//int32_t ch = 0;
	// stack of front tags we encounter
	Tagx stack[MAXTAGSTACK];
	Tagx *stackPtr = stack;

	/*
	// stack of cumulative tag hashes
	int32_t  stack      [1000];
	int32_t  cumHash    [1000];
	int32_t  secNums    [1000];
	int32_t  contentHash[1000];
	int32_t *cumHashPtr = cumHash;
	int32_t *secNumPtr  = secNums;
	int32_t *chPtr      = contentHash;
	*/

	// determine what tags we are within
	//sec_t inFlag = 0;

	Section *current     = NULL;
	Section *rootSection = NULL;

	// assume none
	m_rootSection = NULL;

	// only add root section if we got some words
	if ( nw > 0 ) {
		// record this i guess
		rootSection = &m_sections[m_numSections];
		// clear
		memset ( rootSection , 0 , sizeof(Section) );
		// . the current section we are in
		// . let's use a root section
		current = rootSection;
		// init that to be the whole page
		rootSection->m_b = nw;
		// save it
		m_rootSection = rootSection;
		// to fix a core dump
		rootSection->m_baseHash = 1;
		// advance
		m_numSections++;
	}

	// count this
	int32_t alnumCount = 0;
	// for debug
	g_sections = this;

	// Sections are no longer 1-1 with words, just with front tags
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// we got it
		//m_sectionPtrs[i] = current;
		nodeid_t fullTid = tids[i];
		// are we a non-tag?
		if ( ! fullTid ) { 
			// skip if not alnum word
			if ( ! wids[i] ) continue;
			// count it raw
			alnumCount++;
			// must be in a section at this point
			if ( ! current ) { char *xx=NULL;*xx=0; }
			// . hash it up for our content hash
			// . this only hashes words DIRECTLY in a 
			//   section because we can have a large menu
			//   section with a little bit of text, and we
			// contain some non-menu sections.
			//ch = hash32h ( (int32_t)wids[i] , ch );
			// if not in an anchor, script, etc. tag
			//if ( ! inFlag ) current->m_plain++;
			// inc count in current section
			current->m_exclusive++;
			continue;
		}

		// make a single section for input tags
		if ( fullTid == TAG_INPUT ||
		     fullTid == TAG_HR    ||
		     fullTid == TAG_COMMENT ) {
			// try to realloc i guess. should keep ptrs in tact.
			if ( m_numSections >= m_maxNumSections && 
			     ! growSections() ) 
				return true;
			// get the section
			Section *sn = &m_sections[m_numSections];
			// clear
			memset ( sn , 0 , sizeof(Section) );
			// inc it
			m_numSections++;
			// sanity check - breach check
			if ( m_numSections > max ) { char *xx=NULL;*xx=0; }
			// set our parent
			sn->m_parent = current;
			// need to keep a word range that the section covers
			sn->m_a = i;
			// init the flags of the section
			//sn->m_flags = inFlag ;
			// section consists of just this tag
			sn->m_b = i + 1;
			// go on to next
			continue;
		}

		// a section of multiple br tags in a sequence
		if ( fullTid == TAG_BR ) {
			// try to realloc i guess. should keep ptrs in tact.
			if ( m_numSections >= m_maxNumSections && 
			     ! growSections() ) 
				return true;
			// get the section
			Section *sn = &m_sections[m_numSections];
			// clear
			memset ( sn , 0 , sizeof(Section) );
			// inc it
			m_numSections++;
			// sanity check - breach check
			if ( m_numSections > max ) { char *xx=NULL;*xx=0; }
			// set our parent
			sn->m_parent = current;
			// need to keep a word range that the section covers
			sn->m_a = i;
			// init the flags of the section
			//sn->m_flags = inFlag ;
			// count em up
			int32_t brcnt = 1;
			// scan for whole sequence
			int32_t lastBrPos = i;
			for ( int32_t j = i + 1 ; j < nw ; j++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// claim br tags
				if ( tids[j] == TAG_BR ) { 
					lastBrPos = j;
					brcnt++; 
					continue; 
				}
				// break on words
				if ( wids[j] ) break;
				// all spaces is ok
				if ( w->isSpaces(j) ) continue;
				// otherwise, stop on other punct
				break;
			}
			// section consists of just this tag
			sn->m_b = lastBrPos + 1;
			// advance
			i = lastBrPos;
			// set this for later so that getDelimHash() returns
			// something different based on the br count for
			// METHOD_ATTRIBUTE
			sn->m_baseHash = 19999 + brcnt;
			// go on to next
			continue;
		}

		// get the tag id without the back bit
		nodeid_t tid = fullTid & BACKBITCOMP;

		// . ignore tags with no corresponding back tags
		// . if they have bad html and have front tags
		//   with no corresponding back tags, that will hurt!
		// . make exception for <li> tag!!!
		// . was messing up:
		//   http://events.kqed.org/events/index.php?com=detail&
		//   eID=9812&year=2009&month=11
		//   for parsing out events
		// . make excpetion for <p> tag too! most ppl use </p>
		if ( ( ! hasBackTag ( tid ) || 
		       wptrs[i][1] =='!'    || // <!ENTITY rdfns...>
		       wptrs[i][1] =='?'    ) &&
		     tid != TAG_P &&
		     tid != TAG_LI )
			continue;

		// . these imply no back tag
		// . <description />
		// . fixes inconsistency in 
		//   www.trumba.com/calendars/KRQE_Calendar.rss
		if ( wptrs[i][wlens[i]-2] == '/' && tid == TAG_XMLTAG )
			continue;

		// ignore it cuz we decided it was unbalanced
		//if ( bits->m_bits[i] & D_UNBAL ) continue;

		// and these often don't have back tags, like <objectid .> WTF!
		// no! we need these for the xml-based rss feeds
		// TODO: we should parse the whole doc up front and determine
		// which tags are missing back tags...
		//if ( tid == TAG_XMLTAG ) continue;

		// wtf, embed has no back tags. i fixed this in XmlNode.cpp
		//if ( tid == TAG_EMBED ) continue;
		// do not breach the stack
		if ( stackPtr - stack >= MAXTAGSTACK ) {
			log("html: stack breach for %s",url->getUrl());
			// if we set g_errno and return then the url just
			// ends up getting retried once the spider lock
			// in Spider.cpp expires in MAX_LOCK_AGE seconds.
			// about an hour. but really we should completely
			// give up on this. whereas we should retry OOM errors
			// etc. but this just means bad html really.
			//g_errno = ETAGBREACH;
			// just reset to 0 sections then
			reset();
			return true;
		}

		char gotBackTag ;
		if ( fullTid != tid ) gotBackTag = 1;
		else                  gotBackTag = 0;

		// "pop tid", tid to pop off stack
		nodeid_t ptid       = tid;
		nodeid_t fullPopTid = fullTid;

		// no nested <li> tags allowed
		if ( fullTid == TAG_LI &&  
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_LI ) 
			gotBackTag = 2;

		// no nested <b> tags allowed
		if ( fullTid == TAG_B &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_B ) 
			gotBackTag = 2;

		// no nested <a> tags allowed
		if ( fullTid == TAG_A &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_A ) 
			gotBackTag = 2;

		// no nested <p> tags allowed
		if ( fullTid == TAG_P &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_P ) 
			gotBackTag = 2;

		// no <hN> tags inside a <p> tag
		// fixes http://www.law.berkeley.edu/140.htm
		if ( fullTid >= TAG_H1 &&
		     fullTid <= TAG_H5 &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_P ) {
			// match this on stack
			ptid       = TAG_P;
			fullPopTid = TAG_P;
			gotBackTag = 2;
		}

		// no nested <td> tags allowed
		if ( fullTid == TAG_TD &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_TD ) 
			gotBackTag = 2;

		// encountering <tr> when in a <td> closes the <td> AND
		// should also close the <tr>!!
		if ( fullTid == TAG_TR &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_TD )
			gotBackTag = 2;

		// no nested <tr> tags allowed
		if ( fullTid == TAG_TR &&
		     stackPtr > stack && 
		     ((stackPtr-1)->m_tid)==TAG_TR )
			gotBackTag = 2;

		// this is true if we are a BACK TAG
		if ( gotBackTag ) {
			/*
			// . ok, at this point, we are a BACK TAG
			// . turn this shit on and off
			if ( tid == TAG_SCRIPT  ) inFlag &= ~SEC_SCRIPT;
			if ( tid == TAG_TABLE   ) inFlag &= ~SEC_IN_TABLE;
			if ( tid == TAG_NOSCRIPT) inFlag &= ~SEC_NOSCRIPT;
			if ( tid == TAG_STYLE   ) inFlag &= ~SEC_STYLE;
			if ( tid == TAG_MARQUEE ) inFlag &= ~SEC_MARQUEE;
			if ( tid == TAG_SELECT  ) inFlag &= ~SEC_SELECT;
			if ( tid == TAG_STRIKE  ) inFlag &= ~SEC_STRIKE;
			if ( tid == TAG_S       ) inFlag &= ~SEC_STRIKE2;
			//if ( tid == TAG_A       ) inFlag &= ~SEC_A;
			if ( tid == TAG_TITLE   ) {
				inFlag &= ~SEC_IN_TITLE;
				// first time?
				if ( m_titleEnd == -1 ) m_titleEnd = i;
			}
			if ( tid == TAG_H1      ) inFlag &= ~SEC_IN_HEADER;
			if ( tid == TAG_H2      ) inFlag &= ~SEC_IN_HEADER;
			if ( tid == TAG_H3      ) inFlag &= ~SEC_IN_HEADER;
			if ( tid == TAG_H4      ) inFlag &= ~SEC_IN_HEADER;
			*/

			// . ignore anchor tags as sections
			// . was causing double the number of its parent tag 
			//   hashes and messing up Events.cpp's counting using
			//   "ct" hashtbl
			//if ( tid == TAG_A ) continue;

			// ignore span tags that are non-breaking because they
			// do not change the grouping/sectioning behavior of
			// the web page and are often abused.
			if ( ptid == TAG_SPAN ) continue;

			// fix for gwair.org
			if ( ptid == TAG_FONT ) continue;

			// too many people use these like a <br> tag or
			// make them open-ended or unbalanced
			//if ( tid == TAG_P      ) continue;
			if ( ptid == TAG_CENTER ) continue;
			
		subloop:
			// don't blow the stack
			if ( stackPtr == stack ) continue;

			// point to it
			Tagx *spp = (stackPtr - 1);

			// assume we should pop stack
			//bool popStack = true;

			// init it
			Tagx *p ;
			// scan through the stack until we find a
			// front tag that matches this back tag
			//for(p = spp ; p >= stack && gotBackTag == 1 ; p-- ) {
			for ( p = spp ; p >= stack ; p-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				// no match?
				if ( p->m_tid != ptid ) {
					// matched before? we can pop
					if ( p->m_flags & TXF_MATCHED )
						continue;
					// do not pop it off the stack!
					//popStack = false;
					// keep on going
					continue;
				}
				// do not double match
				if ( p->m_flags & TXF_MATCHED )
					continue;
				// flag it cuz we matched it
				p->m_flags |= TXF_MATCHED;
				// set the stack ptr to it
				spp = p;
				// and stop
				break;
			}

			// no matching front tag at all?
			// then just ignore this back tag
			if ( p < stack ) continue;

			// get section number of the front tag
			//int32_t xn = *(secNumPtr-1);
			int32_t xn = spp->m_secNum;
			// sanity
			if ( xn<0 || xn>=m_numSections ) {char*xx=NULL;*xx=0;}
			// get it
			Section *sn = &m_sections[xn];

			// we are now back in the parent section
			//current = sn;
			// record the word range of the secion we complete
			sn->m_b = i+1;

			// do not include the <li> tag as part of it
			// otherwise we end up with overlapping section since
			// this tag ALSO starts a section!!
			if ( gotBackTag == 2 ) sn->m_b = i;

			// sanity check
			//if ( sn->m_b <= sn->m_a ) { char *xx=NULL;*xx=0;}

			/*
			// if our parent got closed before "sn" closed because
			// of an out-of-order back tag issue, then if it
			// no longer contains sn->m_a, then reparent "sn"
			// to its grandparent.
			Section *ps = sn->m_parent;
			for ( ; ps && ps->m_b >= 0 && ps->m_b <= sn->m_a ;
			      ps = ps->m_parent );
			// assign it
			sn->m_parent = ps;
			     
			// . force our parent section to fully contain us
			// . fixes interlaced tag bugs
			if ( sn->m_parent && 
			     sn->m_parent->m_b < i &&
			     sn->m_parent->m_b >= 0 ) 
				sn->m_b = sn->m_parent->m_b;
			
			// sanity check
			if ( sn->m_b <= sn->m_a ) { char *xx=NULL;*xx=0;}
			*/

			// if our parent got closed before "sn" closed because
			// it hit its back tag before we hit ours, then we
			// must cut ourselves int16_t and try to match this
			// back tag to another front tag on the stack
			Section *ps = sn->m_parent;
			for ( ; ps != rootSection ; ps = ps->m_parent ) {
				// skip if parent no longer contains us!
				if ( ps->m_b <= sn->m_a ) continue;
				// skip if this parent is still open
				if ( ps->m_b <= 0 ) continue;
				// parent must have closed before us
				if ( ps->m_b > sn->m_b ) {char *xx=NULL;*xx=0;}
				// we had no matching tag, or it was unbalanced
				// but i do not know which...!
				sn->m_flags |= SEC_OPEN_ENDED;
				// cut our end int16_ter
				sn->m_b = ps->m_b;
				// our TXF_MATCHED bit should still be set
				// for spp->m_flags, so try to match ANOTHER
				// front tag with this back tag now
				if ( ! ( spp->m_flags & TXF_MATCHED ) ) {
					char *xx=NULL;*xx=0; }
				// ok, try to match this back tag with another
				// front tag on the stack, because the front
				// tag we had selected got cut int16_t because
				// its parent forced it to cut int16_t.
				goto subloop;
			}
   
			// sanity check
			if ( sn->m_b <= sn->m_a ) { char *xx=NULL;*xx=0;}

			// mark the section as unbalanced
			if ( spp != (stackPtr - 1) )
				sn->m_flags |= SEC_UNBALANCED;

			// start this
			//sn->m_alnumCount = alnumCount - sn->m_alnumCount ;
			// . store the final content hash for this section
			// . should be 0 if no alnum words encountered
			//sn->m_contentHash = ch;

			// . when this was uncommented
			//   resources.umwhisp.org/census/Spotsylvania/
			//   1860_berkley_sch1.htm
			//   which had <tr><td></tr><tr><td></tr>... was
			//   keeping a ton of <td>'s on the stack and
			//   never popping them off when it hit a </tr>.
			//   and the <tr>s remained on the stack too..
			// . this fixes sfmusictech.com from truncating the
			//   table that starts at A=535 just because there
			//   is an unclosed <td> tag that has to be closed
			//   by a </tr> tag. but for some reason we are coring
			//   in sunsetpromotions.com
			// . i think the idea was to let the guys remain
			//   on the stack, and close the sections below...
			//if ( ! popStack ) goto skipme;

			// revert it to this guy, may not equal stackPtr-1 !!
			stackPtr = spp;
			
			// if perfect decrement the stack ptr
			//stackPtr--;
			//cumHashPtr--;
			//secNumPtr--;
			//chPtr--;
			// now pop it back
			//ch = *chPtr;
			//ch = stackPtr->m_contentHash;
			// get parent section
			if ( stackPtr > stack ) {
				// get parent section now
				//xn = *(secNumPtr-1);
				xn = (stackPtr-1)->m_secNum;
				// set current to that
				current = &m_sections[xn];
			}
			else {
				//current = NULL;
				//char *xx=NULL;*xx=0; 
				// i guess this is bad html!
				current = rootSection;
			}
			
			// revert the hash
			//if ( stackPtr > stack ) h = *(cumHashPtr-1);
			//if ( stackPtr > stack ) h = (stackPtr-1)->m_cumHash;
			//else                    h = 0;
			// debug log
			if ( g_conf.m_logDebugSections ) {
				char *ms = "";
				if ( stackPtr->m_tid != ptid) ms =" UNMATCHED";
				char *back ="";
				if ( fullPopTid & BACKBIT ) back = "/";
				logf(LOG_DEBUG,"section: pop tid=%"INT32" "
				     "i=%"INT32" "
				     "level=%"INT32" "
				     "%s%s "
				     //"h=0x%"XINT32""
				     "%s",(int32_t)tid,
				     i,
				     (int32_t)(stackPtr - stack),
				     back,g_nodes[tid].m_nodeName,
				     //h,
				     ms);
			}
			
			// . hmmm. did we hit a back tag with no front tag?
			//   or we could have been missing another back tag!
			// . if nested properly, this should equal here
			// . if it was a match, we are done, so continue
			//if ( *stackPtr == tid ) continue;
			//if ( stackPtr->m_tid == tid ) continue;
			
			// for </table> and </tr> keep popping off until we 
			// find it!
			//if ( tid == TAG_TABLE || tid == TAG_TR) goto subloop;
			
			// set a flag
			//m_badHtml = true;

			// if we are a <li> or <ul> tag and just closing
			// an <li> tag on the stack, then we still have
			// to add ourselves as a front tag below...
			//if(fullTid != TAG_LI && fullTid != TAG_UL ) continue;
			//skipme:
			// . if we were a back tag, we are done... but if we
			//   were a front tag, we must add ourselves below...
			// . MDW: this seems more logical than the if-statement
			//        below...
			if ( fullTid != tid ) continue;
			/*
			if ( fullPopTid != TAG_LI && 
			     fullPopTid != TAG_B  &&
			     fullPopTid != TAG_TR &&
			     fullPopTid != TAG_TD &&
			     fullPopTid != TAG_P    ) continue;
			*/
		}


		//
		// if front tag, update "h" and throw it on the stack
		//

		/*
		// turn this shit on and off
		if      ( tid == TAG_SCRIPT  ) inFlag |= SEC_SCRIPT;
		else if ( tid == TAG_TABLE   ) inFlag |= SEC_IN_TABLE;
		else if ( tid == TAG_NOSCRIPT) inFlag |= SEC_NOSCRIPT;
		else if ( tid == TAG_STYLE   ) inFlag |= SEC_STYLE;
		else if ( tid == TAG_MARQUEE ) inFlag |= SEC_MARQUEE;
		else if ( tid == TAG_SELECT  ) inFlag |= SEC_SELECT;
		else if ( tid == TAG_STRIKE  ) inFlag |= SEC_STRIKE;
		else if ( tid == TAG_S       ) inFlag |= SEC_STRIKE2;
		//else if ( tid == TAG_A       ) inFlag |= SEC_A;
		else if ( tid == TAG_TITLE   ) {
			inFlag |= SEC_IN_TITLE;
			// first time?
			if ( m_titleStart == -1 ) {
				m_titleStart = i;
				// Address.cpp uses this
				m_titleStartAlnumPos = alnumCount;
			}
		}
		else if ( tid == TAG_H1      ) inFlag |= SEC_IN_HEADER;
		else if ( tid == TAG_H2      ) inFlag |= SEC_IN_HEADER;
		else if ( tid == TAG_H3      ) inFlag |= SEC_IN_HEADER;
		else if ( tid == TAG_H4      ) inFlag |= SEC_IN_HEADER;
		*/

		// . ignore anchor tags as sections
		// . was causing double the number of its parent tag 
		//   hashes and messing up Events.cpp's counting using
		//   "ct" hashtbl
		//if ( tid == TAG_A ) continue;

		// ignore paragraph/center tags, too many people are careless
		// with them... santafeplayhouse.com
		// i can't ignore <p> tags anymore because for
		// http://www.abqfolkfest.org/resources.shtml we are allowing
		// "Halloween" to have "SEP-DEC" as a header even though
		// that header is BELOW "Halloween" just because we THINK
		// they are in the same section BUT in reality they were
		// in different <p> tags. AND now it seems that the 
		// improvements we made to Sections.cpp for closing open
		// ended tags are pretty solid that we can unignore <p>
		// tags again, it only helped the qa run...
		//if ( tid == TAG_P ) continue;
		if ( tid == TAG_CENTER ) continue;

		if ( tid == TAG_SPAN ) continue;
		// gwair.org has font tags the pair up a date "1st Sundays"
		// with the address above it, and it shouldn't do that!
		if ( tid == TAG_FONT ) continue;

		// try to realloc i guess. should keep ptrs in tact.
		if ( m_numSections >= m_maxNumSections && ! growSections() ) 
			return true;
		// get the section
		Section *sn = &m_sections[m_numSections];
		// clear
		memset ( sn , 0 , sizeof(Section) );
		// inc it
		m_numSections++;
		// sanity check - breach check
		if ( m_numSections > max ) { char *xx=NULL;*xx=0; }
		
		// . check the last tagId we encountered for this
		//   current section.
		// . a basic "majority element" algorithm
		// . the final "tid" if any may be the "majority
		//   element"
		// . Address.cpp uses this to eliminate place names
		//   that are in a totally different section than the
		//   street address and should not be paired up
		//if ( tid != TAG_B ) {
		//	if ( current->m_lastTid == tid ) 
		//		current->m_numBackToBackSubsections++;
		//	else {
		//		current->m_lastTid                =tid;
		//		current->m_numBackToBackSubsections=1;
		//	}
		//}
		// set our parent
		sn->m_parent = current;
		// set this
		current = sn;
		// update this
		//m_sectionPtrs[i] = current;
		// clear this
		//sec_t myFlag = 0;
		
		// fix it, we don't like hashes of 0 cuz HashTableT
		// uses a key of 0 to signify an empty bucket!
		//if ( h == 0 ) h = 1;
		// store it, post-hash
		//sn->m_tagHash = h;

		// add the content hash as well!
		/*
		// . get our parents enumerated tagHash
		// . all tag hashes are enumerated except his tagHash
		int32_t ph = sn->m_parent->m_tagHash;
		// put his enumeration into it
		int32_t eh = hash32h ( ph , sn->m_parent->m_occNum );
		// cap it off with our tag hash with no enumeration
		eh = hash32h ( h , eh );
		// store that
		sn->m_tagHash = eh;
		// what kid # are we for this particular parent?
		int32_t occNum = m_ot.getScore ( (int64_t *)&eh );
		// save our kid #
		sn->m_occNum = occNum;
		// inc it for the next guy
		occNum++;
		// store back. return true with g_errno set on error
		if ( ! m_ot.addTerm ( (int64_t *)&eh ) ) return true;
		*/
		
		
		/*
		// assume this is invalid
		sn->m_hardTagHash = 0;
		// . this hash is just for breaking tags with back tags
		// . this "section id" is used by Events.cpp
		// . we can have multiple sections with the same
		//   m_hardTagHash since it is more restrictive
		if ( isBreakingTagId(tid) && hasBackTag(tid) ) {
		// accumulate
		h2 = hash32h ( tid , h2 );
		// fix it
		//if ( h2 == 0 ) h2 = 1;
		// set it
		sn->m_hardTagHash = h2;
		}
		*/
		// need to keep a word range that the section covers
		sn->m_a = i;
		// start this
		//sn->m_alnumCount = alnumCount;
		// init the flags of the section
		//sn->m_flags = inFlag ;//| myFlag ;
		// assume no terminating bookend
		sn->m_b = -1;

		// if tagged as an unbalanced tag, do not push onto stack
		//if ( bits->m_bits[i] & D_UNBAL ) continue;
		
		// push a unique id on the stack so we can pop if we
		// enter a subsection
		stackPtr->m_tid         = tid;
		//stackPtr->m_cumHash   = h;
		stackPtr->m_secNum      = m_numSections - 1;
		//stackPtr->m_contentHash = ch;
		stackPtr->m_flags       = 0;
		stackPtr++;

		//*stackPtr  ++ = tid;
		//*cumHashPtr++ = h;
		//*secNumPtr ++ = m_numSections - 1;
		
		// push this for when we hit the back tag we pop it
		//*chPtr     ++ = ch;
		// reset it since starting a new section
		//ch = 0;
		// depth or level
		//sn->m_depth = stackPtr - stack;
		// debug log
		if ( ! g_conf.m_logDebugSections ) continue;
		//int32_t back = 0;
		//if ( fullTid & BACKBIT ) back = 1;
		logf(LOG_DEBUG,"section: push tid=%"INT32" "
		     "i=%"INT32" "
		     "level=%"INT32" "
		     "%s "
		     //"back=%"INT32""
		     //" h=0x%"XINT32"",
		     ,
		     (int32_t)tid,
		     i,
		     (int32_t)(stackPtr - stack)-1,
		     g_nodes[(int32_t)tid].m_nodeName
		     //,back);//,h);
		     );
	}

	/*
	// <center> tags should end at next <center>... like <li>
	// OR we could end it after our FIRST CHILD... or if text
	// follows, after that text... well the browser seems to not
	// close these center tags... sooo. let's comment this out
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if not open ended
		if ( sn->m_b >= 0 ) continue;
		// skip if not a center or p tag
		if ( sn->m_tagId != TAG_CENTER &&
		     sn->m_tagId != TAG_P ) continue;
		// close if open ended on the next <center> or <p> tag
		if ( sn->m_next && sn->m_next->m_a < sn->m_b )
			sn->m_b = sn->m_next->m_a;
	}
	*/

	// if first word in a section false outside of the parent section
	// then reparent to the grandparent. this can happen when we end
	// up closing a parent section before ???????
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// get it
		Section *si = &m_sections[i];
		// skip if we are still open-ended
		if ( si->m_b < 0 ) continue;
		// get parent
		Section *sp = si->m_parent;
		// skip if no parent
		if ( ! sp ) continue;
		// skip if parent still open ended
		if ( sp->m_b < 0 ) continue;
		// subloop it
	doagain:
		// skip if no parent
		if ( ! sp ) continue;
		// parent must start before us
		if ( sp->m_a > si->m_a ) { char *xx=NULL;*xx=0; }
		// . does parent contain our first word?
		// . it need not fully contain our last word!!!
		if ( sp->m_a <= si->m_a && sp->m_b > si->m_a ) continue;
		// if parent is open ended, then it is ok for now
		if ( sp->m_a <= si->m_a && sp->m_b == -1 ) continue;
		// get grandparent
		sp = sp->m_parent;
		// set
		si->m_parent = sp;
		// try again
		goto doagain;
	}

	bool inFrame = false;
	int32_t gbFrameNum = 0;

	//
	// . set Section::m_xmlNameHash for xml tags here
	// . set Section::m_frameNum and SEC_IN_GBFRAME bit
	//
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// get it
		int32_t ws = sn->m_a;
		// int16_tcut
		nodeid_t tid = tids[ws];

		// set SEC_IN_FRAME
		if ( tid == TAG_GBFRAME ) {
			// start or end?
			gbFrameNum++;
			inFrame = true;
		}
		if ( tid == (TAG_GBFRAME | BACKBIT) )
			inFrame = false;

		// mark it
		if ( inFrame )
			sn->m_gbFrameNum = gbFrameNum;

		// custom xml tag, hash the tag itself
		if ( tid != TAG_XMLTAG ) continue;
		// stop at first space to avoid fields!!
		char *p    =     m_wptrs[ws] + 1;
		char *pend = p + m_wlens[ws];
		// skip back tags
		if ( *p == '/' ) continue;
		// reset hash
		int64_t xh = 0;
		// and hash char count
		unsigned char cnt = 0;
		// hash till space or / or >
		for ( ; p < pend ; p++ ) {
			// stop on space or / or >
			if ( is_wspace_a(*p) ) break;
			if ( *p == '/' ) break;
			if ( *p == '>' ) break;
			// hash it in
			xh ^= g_hashtab[cnt++][(unsigned char )*p];
		}
		// sanity check
		//if ( ! xh ) { char *xx=NULL;*xx=0; }
		// if it is a string of the same chars it can be 0
		if ( ! xh ) xh = 1;
		// store that
		sn->m_xmlNameHash = (int32_t)xh;
	}



	// find any open ended tags and constrain them based on their parent
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *si = &m_sections[i];
		// get its parent
		Section *ps = si->m_parent;
		// if parent is open-ended panic!
		if ( ps && ps->m_b < 0 ) { char *xx=NULL;*xx=0; }

		// if our parent got constrained from under us, we need
		// to telescope to a new parent
		for  ( ; ps && ps->m_b >= 0 && ps->m_b <= si->m_a ; ) {
			ps = ps->m_parent;
			si->m_parent = ps;
		}

		// assume end is end of doc
		int32_t end = m_words->getNumWords();
		// get end of parent
		if ( ps ) end = ps->m_b;
		// flag it
		if ( si->m_b == -1 ) si->m_flags |= SEC_OPEN_ENDED;
		// shrink our section if parent ends before us OR if we
		// are open ended
		if ( si->m_b != -1 && si->m_b <= end ) continue;
		// this might constrain someone's parent such that
		// that someone no longer can use that parent!!
		si->m_b = end;
		// . get our tag type
		// . use int32_t instead of nodeid_t so we can re-set this
		//   to the xml tag hash if we need to
		int32_t tid1 = m_tids[si->m_a];
		// use the tag hash if this is an xml tag
		if ( tid1 == TAG_XMLTAG ) {
			// we computed this above
			tid1 = si->m_xmlNameHash;
			// skip if zero!
			if ( ! tid1 ) continue;
		}
		// must be there to be open ended
		if ( ! tid1 ) { char *xx=NULL;*xx=0; }
		// flag it for later
		//si->m_flags |= SEC_CONSTRAINED;
		// NOW, see if within that parent there is actually another
		// tag after us of our same tag type, then use that to
		// constrain us instead!!
		// this hurts <p><table><tr><td><p>.... because it
		// uses that 2nd <p> tag to constrain si->m_b of the first
		// <p> tag which is not right! sunsetpromotions.com has that.
		for ( int32_t j = i + 1 ; j < m_numSections ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			Section *sj = &m_sections[j];
			// get word start
			int32_t a = sj->m_a;
			// skip if ties with us already
			if ( a == si->m_a ) continue;
			// stop if out
			if ( a >= end ) break;

			// . it must be in the same expanded frame src, if any
			// . this fixes trulia.com which was ending our html
			//   tag, which was open-ended, with the html tag in
			//   a frame src expansion
			if ( sj->m_gbFrameNum != si->m_gbFrameNum ) continue;
			// fix sunsetpromotions.com bug. see above.
			if ( sj->m_parent != si->m_parent ) continue;
			// get its tid
			int32_t tid2 = tids[a];
			// use base hash if xml tag
			if ( tid2 == TAG_XMLTAG )
				tid2 = sj->m_xmlNameHash;
			// must be our tag type!
			if ( tid2 != tid1 ) continue;
			// ok end us there instead!
			si->m_b = a;
			// stop
			break;
		}
	}


	// reparent again now that things are closed
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// get it
		Section *si = &m_sections[i];
		// skip if we are still open-ended
		if ( si->m_b < 0 ) { char *xx=NULL;*xx=0; }
		// get parent
		Section *sp = si->m_parent;
		// skip if null
		if ( ! sp ) continue;
		// skip if parent still open ended
		if ( sp->m_b < 0 ) { char *xx=NULL;*xx=0; }
		// subloop it
	doagain2:
		// skip if no parent
		if ( ! sp ) continue;
		// . does parent contain our first word?
		// . it need not fully contain our last word!!!
		if ( sp->m_a <= si->m_a && sp->m_b > si->m_a ) continue;
		// if parent is open ended, then it is ok for now
		if ( sp->m_a <= si->m_a && sp->m_b == -1 ) continue;
		// if parent is open ended, then it is ok for now
		if ( sp->m_b == -1 ) { char *xx=NULL;*xx=0; }
		// get grandparent
		sp = sp->m_parent;
		// set
		si->m_parent = sp;
		// try again
		goto doagain2;
	}


	m_isTestColl = ! strcmp(m_coll,"qatest123") ;

	//
	//
	// now assign m_sectionPtrs[] which map a word to the first
	// section that contains it
	//
	//
	Section *dstack[MAXTAGSTACK];
	int32_t     ns = 0;
	int32_t      j = 0;
	current       = m_rootSection;//&m_sections[0];
	Section *next = m_rootSection;//&m_sections[0];
	// first print the html lines out
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// pop all off the stack that match us
		for ( ; ns>0 && dstack[ns-1]->m_b == i ; ) {
			ns--;
			current = dstack[ns-1];
		}
		// push our current section onto the stack if i equals
		// its first word #
		for ( ; next && i == next->m_a ; ) {
			dstack[ns++] = next;
			// sanity check
			//if ( next->m_a == next->m_b ) { char *xx=NULL;*xx=0;}
			// set our current section to this now
			current = next;
			// get next section for setting "next"
			j++;
			// if no more left, set "next" to NULL and stop loop
			if ( j >= m_numSections ) { next=NULL; break; }
			// grab it
			next = &m_sections[j];
		}
		// assign
		m_sectionPtrs[i] = current;
	}

	if ( m_isTestColl ) {
		// map each word to a section that contains it at least
		for ( int32_t i = 0 ; i < m_nw ; i++ ) {
			Section *si = m_sectionPtrs[i];
			if ( si->m_a >  i ) { char *xx=NULL;*xx=0; }
			if ( si->m_b <= i ) { char *xx=NULL;*xx=0; }
		}
	}

	// . addImpliedSections() requires Section::m_baseHash
	// . set Section::m_baseHash
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// these have to be in order of sn->m_a to work right
		// because we rely on the parent tag hash, which would not
		// necessarily be set if we were not sorted, because the 
		// parent section could have SEC_FAKE flag set because it is
		// a br section added afterwards.
		Section *sn = &m_sections[i];
		// get word start into "ws"
		int32_t ws = sn->m_a;
		// int16_tcut
		nodeid_t tid = tids[ws];
		// sanity check, <a> guys are not sections
		//if ( tid == TAG_A &&
		//     !(sn->m_flags & SEC_SENTENCE) ) { char *xx=NULL;*xx=0; }
		// use a modified tid as the tag hash?
		int64_t mtid = tid;
		// custom xml tag, hash the tag itself
		if ( tid == TAG_XMLTAG )
			mtid = hash32 ( wptrs[ws], wlens[ws] );
		// an unknown tag like <!! ...->
		if ( tid == 0 )
			mtid = 1;
		// . if we are a div tag, mod it
		// . treat the fields in the div tag as 
		//   part of the tag hash. 
		// . helps Events.cpp be more precise about
		//   section identification!!!!
		// . we now do this for TD and TR so Nov 2009 can telescope for
		//   http://10.5.1.203:8000/test/doc.17096238520293298312.html
		//   so the calendar title "Nov 2009" can affect all dates
		//   below the calendar.
		if ( tid == TAG_DIV  || 
		     tid == TAG_TD   || 
		     tid == TAG_TR   ||
		     tid == TAG_LI   || // newmexico.org urls class=xxx
		     tid == TAG_UL   || // newmexico.org urls class=xxx
		     tid == TAG_P    || // <p class="pstrg"> stjohnscollege.edu
		     tid == TAG_SPAN ) {
			// get ptr
		        uint8_t *p = (uint8_t *)wptrs[ws];
			// skip <
			p++;
			// skip following alnums, that is the tag name
			for ( ; is_alnum_a(*p) ; p++ );
			//if ( tid == TAG_DIV ) p += 4;
			// skip "<td" or "<tr"
			//else p += 3;
			// scan for "id" or "class" in it
			// . i had to increase this because we were missing
			//   some stuff causing us to get the wrong implied
			//   sections for 
			//   www.guysndollsllc.com/page5/page4/page4.html
			//   causing "The Remains" to be paired up with
			//   "Aug 7, 2010" in an implied section which was
			//   just wrong. it was 20, i made it 100...
			uint8_t *pend = p + 100;
			// position ptr
			unsigned char cnt = 0;
			// a flag
			bool skipTillSpace = false;
			// . just hash every freakin char i guess
			// . TODO: maybe don't hash "width" for <td><tr>
			for ( ; *p && *p !='>' && p < pend ; p++ ) {
				// skip bgcolor= tags because panjea.org
				// interlaces different colored <tr>s in the
				// table and i want them to be seen as brother
				// sections, mostly for the benefit of the
				// setting of lastBrother1/2 in Events.cpp
				if ( is_wspace_a(p[0])      &&
				     to_lower_a (p[1])=='b' &&
				     to_lower_a (p[2])=='g' ) {
					skipTillSpace = true;
					continue;
				}
				// and skip height=* tags so cabq.gov which
				// uses varying <td> height attributes will
				// have its google map links have the same
				// tag hash so TSF_PAGE_REPEAT gets set
				// in Events.cpp and they are not event titles.
				// it has other chaotic nested tag issues
				// so let's take this out.
				/*
				if ( is_wspace_a(p[0])      &&
				     to_lower_a (p[1])=='h' &&
				     to_lower_a (p[2])=='e' &&
				     to_lower_a (p[3])=='i' &&
				     to_lower_a (p[4])=='g' &&
				     to_lower_a (p[5])=='h' &&
				     to_lower_a (p[6])=='t' ) {
					skipTillSpace = true;
					continue;
				}
				*/

				// if not a space continue
				if ( skipTillSpace ) {
					if ( ! is_wspace_a(*p) ) continue;
					skipTillSpace = false;
				}
				// do not hash until we get a space
				if ( skipTillSpace ) continue;
				// skip if not alnum
				if ( !is_alnum_a(*p)) continue;
				// hash it in
				mtid ^= g_hashtab[cnt++][(unsigned char)*p];
			}
		}
		// should not have either of these yet!
		if ( sn->m_flags & SEC_FAKE     ) { char *xx=NULL;*xx=0; }
		if ( sn->m_flags & SEC_SENTENCE ) { char *xx=NULL;*xx=0; }
		// sanity check
		if ( mtid == 0 ) { char *xx=NULL;*xx=0; }
		// . set the base hash, usually just tid
		// . usually base hash is zero but if it is a br tag
		//   we set it to something special to indicate the number
		//   of br tags in the sequence
		sn->m_baseHash ^= mtid;
		// fix this
		if ( sn == rootSection ) sn->m_baseHash = 1;
		// fix root section i guess
		if ( sn->m_baseHash == 0 ) { 
			// fix core on gk21
			sn->m_baseHash = 2;
			//char *xx=NULL;*xx=0; }
		}
		// set this now too WHY? should already be set!!! was
		// causing the root section to become a title section
		// because first word was "<title>". then every word in
		// the doc got SEC_IN_TITLE set and did not get hashed
		// in XmlDoc::hashBody()... NOR in XmlDoc::hashTitle()!!!
		if ( sn != rootSection ) // || tid != TAG_TITLE ) 
			sn->m_tagId = tid;


		//
		// set m_turkBaseHash
		//
		// . using just the tid based turkTagHash is not as good
		//   as incorporating the class of tags because we can then
		//   distinguish between more types of tags in a document and
		//   that is kind of what "class" is used for
		//
		// use this = "Class" tid
		int64_t ctid = tid;
		// get ptr
		uint8_t *p = (uint8_t *)wptrs[ws];
		// skip <
		p++;
		// skip following alnums, that is the tag name
		for ( ; is_alnum_a(*p) ; p++ );
		// scan for "class" in it
		uint8_t *pend = p + 100;
		// position ptr
		unsigned char cnt = 0;
		// a flag
		bool inClass = false;
		// . just hash every freakin char i guess
		// . TODO: maybe don't hash "width" for <td><tr>
		for ( ; *p && *p !='>' && p < pend ; p++ ) {
			// hashing it up?
			if ( inClass ) {
				// all done if space
				if ( is_wspace_a(*p) ) break;
				// skip if not alnum
				//if ( !is_alnum_a(*p)) continue;
				// hash it in
				ctid ^= g_hashtab[cnt++][(unsigned char)*p];
			}
			// look for " class="
			if ( p[0] !=' ' ) continue;
			if ( to_lower_a(p[1]) != 'c' ) continue;
			if ( to_lower_a(p[2]) != 'l' ) continue;
			if ( to_lower_a(p[3]) != 'a' ) continue;
			if ( to_lower_a(p[4]) != 's' ) continue;
			if ( to_lower_a(p[5]) != 's' ) continue;
			if ( to_lower_a(p[6]) != '=' ) continue;
			// mark it
			inClass = true;
			// skip over it
			p += 6;
		}
		// if root section has no tag this is zero and will core
		// in Dates.cpp where it checks m_turkTagHash32 to be zero
		if ( ctid == 0 ) ctid = 999999;
		// set it for setting m_turkTagHash32
		sn->m_turkBaseHash = ctid;
		// always make root turkbasehash be 999999.
		// if root section did not start with tag it's turkBaseHash
		// will be 999999. but a root section that did start with
		// a tag will have a different turk base hash.
		// will be the same, even if one leads with some punct.
		// fix fandango.com and its kid.
		if ( sn == m_rootSection )
			sn->m_turkBaseHash = 999999;
	}


	// set up our linked list, the functions below will insert sections
	// and modify this linked list
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// set it
		if ( i + 1 < m_numSections )
			m_sections[i].m_next = &m_sections[i+1];
		if ( i - 1 >= 0 )
			m_sections[i].m_prev = &m_sections[i-1];
	}

	// i would say <hr> is kinda like an <h0>, so do it first
	//splitSections ( "<hr" , (int32_t)BH_HR );

	// init to -1 to indicate none
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// reset it
		si->m_firstWordPos = -1;
		si->m_lastWordPos  = -1;
		si->m_senta        = -1;
		si->m_sentb        = -1;
		si->m_firstPlaceNum = -1;
		si->m_headRowSection = NULL;
		si->m_headColSection = NULL;
	}
	// now set position of first word each section contains
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not alnum word
		if ( ! m_wids[i] ) continue;
		// get smallest section containing
		Section *si = m_sectionPtrs[i];
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// skip if already had one!
			if ( si->m_firstWordPos >= 0 ) break;
			// otherwise, we are it
			si->m_firstWordPos = i;
			// . set format hash of it
			// . do it manually since tagHash not set yet
		}
	}
	// and last word position
	for ( int32_t i = m_nw - 1 ; i > 0 ; i-- ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not alnum word
		if ( ! m_wids[i] ) continue;
		// get smallest section containing
		Section *si = m_sectionPtrs[i];
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// skip if already had one!
			if ( si->m_lastWordPos >= 0 ) break;
			// otherwise, we are it
			si->m_lastWordPos = i;
		}
	}

	sec_t inFlag = 0;
	int32_t  istack[1000];
	sec_t iflags[1000];
	int32_t  ni = 0;
	// 
	// now set the inFlags here because the tags might not have all
	// been closed, making tags like SEC_STYLE overflow from where
	// they should be...
	//
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// did we exceed a tag boundary?
		for ( ; ni>0 && si->m_a >= istack[ni-1] ; ) {
			// undo flag
			inFlag &= ~iflags[ni-1];
			// pop off
			ni--;
		}
		// get the flag if any into mf
		sec_t mf = 0;
		// skip if not special tag id
		nodeid_t tid = si->m_tagId;
		if      ( tid == TAG_SCRIPT  ) mf = SEC_SCRIPT;
		else if ( tid == TAG_TABLE   ) mf = SEC_IN_TABLE;
		else if ( tid == TAG_NOSCRIPT) mf = SEC_NOSCRIPT;
		else if ( tid == TAG_STYLE   ) mf = SEC_STYLE;
		else if ( tid == TAG_MARQUEE ) mf = SEC_MARQUEE;
		else if ( tid == TAG_SELECT  ) mf = SEC_SELECT;
		else if ( tid == TAG_STRIKE  ) mf = SEC_STRIKE;
		else if ( tid == TAG_S       ) mf = SEC_STRIKE2;
		else if ( tid == TAG_H1      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H2      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H3      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_H4      ) mf = SEC_IN_HEADER;
		else if ( tid == TAG_TITLE   ) mf = SEC_IN_TITLE;
		// accumulate
		inFlag |= mf;
		// add in the flags
		si->m_flags |= inFlag;
		// skip if nothing special
		if ( ! mf ) continue;
		// sanity
		if ( ni >= 1000 ) { char *xx=NULL;*xx=0; }
		// otherwise, store on stack
		istack[ni] = si->m_b;
		iflags[ni] = mf;
		ni++;

		// title is special
		if ( tid == TAG_TITLE && m_titleStart == -1 ) {
			m_titleStart = si->m_a; // i;
			// Address.cpp uses this
			m_titleStartAlnumPos = alnumCount;
		}
	}

	// . now we insert sentence sections
	// . find the smallest section containing the first and last
	//   word of each sentence and inserts a subsection into that
	// . we have to be careful to reparent, etc.
	// . kinda copy splitSections() function
	// . maybe add an "insertSection()" function???
	if ( m_contentType != CT_JS ) {
		// add sentence sections
		if ( ! addSentenceSections() ) return true;
		// this is needed by setSentFlags()
		setNextSentPtrs();
		// returns false and sets g_errno on error
		if ( ! setSentFlagsPart1 ( ) ) return true;
	}


	//addSentenceSections();

	// tuna texa satafexplayhouse.com page uses header tags to divide
	// the page up into sections
	// CRAP! but graffiti.org starts its event sections with h4 tags
	// and those sections contain h3 tags! which results in us capturing
	// the wrong event header in each event section, so i commented this
	// out... and santafeplayhouse.com still seems to work!
	//splitSections ("<h1", (int32_t)BH_H1 );
	//splitSections ("<h2", (int32_t)BH_H2 );
	//splitSections ("<h3", (int32_t)BH_H3 );
	//splitSections ("<h4", (int32_t)BH_H4 );


	// . set m_nextBrother
	// . we call this now to aid in setHeadingBit() and for adding the
	//   implied sections, but it is ultimately
	//   called a second time once all the new sections are inserted
	setNextBrotherPtrs ( false ); // setContainer = false

	/*
	// setHeadingBit() relies on Section::m_tagHash, so even though we
	// have not added implied sections, do this here, and then do it again
	// after adding implied sections
	setTagHashes();
	*/

	// . set SEC_HEADING bit
	// . need this before implied sections
	setHeadingBit ();

	//
	// set SEC_HR_CONTAINER bit for use by addHeaderImpliedSections(true)
	// fix for folkmads.org which has <tr><td><div><hr></div>...
	//
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not hr
		if ( si->m_tagId != TAG_HR ) continue;
		// cycle this
		Section *sp = si;
		// blow up right before it has text
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// set this
			sp->m_flags |= SEC_HR_CONTAINER;
			// stop if parent has text
			if ( sp->m_parent &&
			     sp->m_parent->m_firstWordPos >= 0 ) break;
		}
	}




	// HACK: back to back br tags
	//splitSections ( (char *)0x01 , (int32_t)BH_BRBR );

	// single br tags
	//splitSections ( "<br" , (int32_t)BH_BR );


	// just in case we added more sections
	//setNextBrotherPtrs ( false );

	/*
	//
	// now add implied sections based on dates
	//
 again:
	int32_t added = addDateBasedImpliedSections();
	// g_errno should be set when we return -1 on error
	if ( added < 0 ) return true;
	// if added something, try again
	if ( added > 0 ) goto again;


	// &bull;
	char bullet[4];
	bullet[0] = 226;
	bullet[1] = 128;
	bullet[2] = 162;
	bullet[3] = 0;
	splitSections ( bullet , BH_BULLET );

	// just in case we added more sections
	setNextBrotherPtrs ( false );

	// now set it again after adding implied sections
	*/

	setTagHashes();

	/*
	// now set m_sorted since we added some sections out of order
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) 
		m_sorted[i] = &m_sections[i];
	// sort them
	char tmpBuf[30000];
	char *useBuf = tmpBuf;
	// do not breach buffer
	int32_t need2 = m_numSections * sizeof(Section *) + 32 ;
	if ( need2 > 30000 ) {
		useBuf = (char *)mmalloc ( need2, "gbmrgtmp");
		if ( ! useBuf ) return true;
	}
	gbmergesort ( m_sorted         ,
		      m_numSections    ,
		      sizeof(Section *),
		      cmp              ,
		      m_niceness       ,
		      useBuf           ,
		      need2            );

	// free it now
	if ( useBuf != tmpBuf ) mfree ( useBuf , need2 ,"gbmrgtmp");

	// now set m_sortedIndex of each section so getSubfieldTable() can use
	// that in Address.cpp to just check to see if each subsection has
	// and address/email/fieldname/etc. 
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) 
		m_sorted[i]->m_sortedIndex = i;
	*/

	//
	//
	// TODO TODO
	//
	// TAKE OUT THESE SANITY CHECKS TO SPEED UP!!!!!!
	//
	//

	// we seem to be pretty good, so remove these now. comment this
	// goto out if you change sections.cpp and want to make sure its good
	//if ( m_isTestColl ) verifySections();

	// this caused us to not set m_nsvt from the sectionsVotes because
	// the content was blanked out because XmlDoc::m_skipIndexing was
	// set to true! but we still need the section votes to be consistent
	// so we can delete them from Sectiondb if we need to.
	//if ( m_numSections == 0 ) return true;

	// now we call it twice, so make it a function
	//setTagHashes();

	// clear this
	bool isHidden  = false;
	int32_t startHide = 0x7fffffff;
	int32_t endHide   = 0 ;
	//int32_t numTitles = 0;
	//Section *lastTitleParent = NULL;
	Section *firstTitle = NULL;
	// now that we have closed any open tag, set the SEC_HIDDEN bit
	// for all sections that are like <div style=display:none>
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// set m_lastSection so we can scan backwards
		m_lastSection = sn;
		// set SEC_SECOND_TITLE flag
		if ( sn->m_tagId == TAG_TITLE ) {
			// . reset if different parent.
			// . fixes trumba.com rss feed with <title> tags
			if ( firstTitle &&
			     firstTitle->m_parent != sn->m_parent ) 
				firstTitle = NULL;
			// inc the count
			//numTitles++;
			// 2+ is bad. fixes wholefoodsmarket.com 2nd title
			if ( firstTitle ) sn->m_flags |= SEC_SECOND_TITLE;
			// set it if need to
			if ( ! firstTitle ) firstTitle = sn;
			// store our parent
			//lastTitleParent = sn->m_parent;
		}
		// get it
		//Section *sn = m_sorted[i]; // sections[i];
		// set this
		int32_t wn = sn->m_a;
		// stop hiding it?
		if ( isHidden ) {
			// turn it off if not contained
			if      ( wn >= endHide   ) isHidden = false;
			//else if ( wn <= startHide ) isHidden = false;
			else    sn->m_flags |= SEC_HIDDEN;
		}
		// get tag id
		nodeid_t tid = sn->m_tagId;//tids[wn];
		// is div, td or tr tag start?
		if ( tid!=TAG_DIV && 
		     tid!=TAG_TD && 
		     tid!=TAG_TR &&
		     tid!=TAG_UL &&
		     tid!=TAG_SPAN) continue;

		// . if we are a div tag, mod it
		// . treat the fields in the div tag as 
		//   part of the tag hash. 
		// . helps Events.cpp be more precise about
		//   section identification!!!!
		// . we now do this for TD and TR so Nov 2009 can telescope for
		//   http://10.5.1.203:8000/test/doc.17096238520293298312.html
		//   so the calendar title "Nov 2009" can affect all dates
		//   below the calendar.

		// get the style tag in there and check it for "display: none"!
		int32_t  slen = wlens[wn];
		char *s    = wptrs[wn];
		char *send = s + slen;
		// check out any div tag that has a style
		char *style = gb_strncasestr(s,slen,"style=") ;
		if ( ! style ) continue;
		// . check for hidden
		// . if no hidden tag assume it is UNhidden
		// . TODO: later push & pop on stack
		char *ds = gb_strncasestr(style,send-style,"display:");
		// if display:none not found turn off SEC_HIDDEN
		if ( ! ds || ! gb_strncasestr(s,slen,"none") ) {
			// turn off the hiding
			isHidden = false;
			// off in us too
			sn->m_flags &= ~SEC_HIDDEN;
			continue;
		}
		// mark all sections in this with the tag
		isHidden = true;
		// on in us
		sn->m_flags |= SEC_HIDDEN;
		// stop it after this word for sure
		if ( sn->m_b   > endHide   ) endHide   = sn->m_b;
		if ( sn->m_a < startHide ) startHide = sn->m_a;
	}


	// now set the content hash of each section
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// must be an alnum word
		if ( ! m_wids[i] ) continue;
		// get its section
		m_sectionPtrs[i]->m_contentHash64 ^= m_wids[i];
		// fix "smooth smooth!"
		if ( m_sectionPtrs[i]->m_contentHash64 == 0 )
			m_sectionPtrs[i]->m_contentHash64 = 123456;

		/*
		// . also now set m_voteHash32 which we use for sectiondb now
		// . so if the content is a date or number its hash will
		//   not change if the date changes. so if the date basically
		//   repeats on each page it won't matter! ticket prices
		//   mess it up too now...!!
		if ( isMonth (m_wids [i]   ) ) continue;
		if ( is_digit(m_wptrs[i][0]) ) continue;
		// is it 1 st, 2 nd, 3 rd, etc
		if ( m_wlens[i]==2 ) {
			if ( to_lower(m_wptrs[i][0]) =='s' &&
			     to_lower(m_wptrs[i][1]) =='t' )
				continue;
			if ( to_lower(m_wptrs[i][0]) =='n' &&
			     to_lower(m_wptrs[i][1]) =='d' )
				continue;
			if ( to_lower(m_wptrs[i][0]) =='r' &&
			     to_lower(m_wptrs[i][1]) =='d' )
				continue;
		}
		// otherwise put it in
		m_sectionPtrs[i]->m_voteHash32 ^= (uint32_t)m_wids[i];
		// fix "smooth smooth!"
		if ( m_sectionPtrs[i]->m_voteHash32 == 0 )
			m_sectionPtrs[i]->m_voteHash32 = 123456;
		*/
	}

	// reset
	m_numInvalids = 0;

	// now set SEC_NOTEXT flag if content hash is zero!
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( sn->m_contentHash64 ) continue;
		// no text!
		sn->m_flags |= SEC_NOTEXT;
		// count it
		m_numInvalids++;
	}

	//
	// set m_sentenceContentHash for sentence that need it
	//
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;
		// if already set, skip
		//sn->m_sentenceContentHash64 = sn->m_contentHash64;
		// skip?
		//if ( sn->m_contentHash64 ) continue;

		// no, m_contentHash64 is just words contained 
		// directly in the section... so since a sentence can
		// contain like a bold subsection, we gotta scan the
		// whole thing.
		sn->m_sentenceContentHash64 = 0LL;

		// scan the wids of the whole sentence, which may not
		// be completely contained in the "sn" section!!
		int32_t a = sn->m_senta;
		int32_t b = sn->m_sentb;
		for ( int32_t j = a ; j < b ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// must be an alnum word
			if ( ! m_wids[j] ) continue;
			// get its section
			sn->m_sentenceContentHash64 ^= m_wids[j];
			// fix "smooth smooth!"
			if ( sn->m_sentenceContentHash64 == 0 )
				sn->m_sentenceContentHash64 = 123456;
		}
	}


	////////
	//
	// set Section::m_indirectSentHash64
	//
	////////
	for ( Section *sn = m_firstSent ; sn ; sn = sn->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		int64_t sc64 = sn->m_sentenceContentHash64;
		if ( ! sc64 ) { char *xx=NULL;*xx=0; }
		// propagate it upwards
		Section *p = sn;
		// TODO: because we use XOR for speed we might end up with
		// a 0 if two sentence are repeated, they cancel out..
		for ( ; p ; p = p->m_parent )
			p->m_indirectSentHash64 ^= sc64;
	}

	/////
	//
	// set SEC_HASHXPATH
	//
	/////
	for ( Section *sn = m_firstSent ; sn ; sn = sn->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		int64_t sc64 = sn->m_sentenceContentHash64;
		if ( ! sc64 ) { char *xx=NULL;*xx=0; }
		// propagate it upwards
		Section *p = sn->m_parent;
		// parent of sentence always gets it i guess
		uint64_t lastVal = 0x7fffffffffffffffLL;
		// TODO: because we use XOR for speed we might end up with
		// a 0 if two sentence are repeated, they cancel out..
		for ( ; p ; p = p->m_parent ) {
			// how can this be a text node?
			if ( p->m_tagId == TAG_TEXTNODE ) continue;
			// if parent's hash is same as its kid then do not
			// hash it separately in order to save index space
			// from adding gbxpathsitehash1234567 terms
			if ( p->m_indirectSentHash64 == lastVal ) continue;
			// update this for deduping
			lastVal = p->m_indirectSentHash64;
			// this parent should be hashed with gbxpathsitehash123
			p->m_flags |= SEC_HASHXPATH;
		}
	}

	//
	// set Section::m_alnumPosA/m_alnumPosB
	//
	int32_t alnumCount2 = 0;
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;
		// save this
		sn->m_alnumPosA = alnumCount2;
		// scan the wids of the whole sentence, which may not
		// be completely contained in the "sn" section!!
		int32_t a = sn->m_senta;
		int32_t b = sn->m_sentb;
		for ( int32_t j = a ; j < b ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// must be an alnum word
			if ( ! m_wids[j] ) continue;
			// alnumcount
			alnumCount2++;
		}
		// so we contain the range [a,b), typical half-open interval
		sn->m_alnumPosB = alnumCount2;
		// sanity check
		if ( sn->m_alnumPosA == sn->m_alnumPosB ){char *xx=NULL;*xx=0;}

		// propagate through parents
		Section *si = sn->m_parent;
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if already had one!
			if ( si->m_alnumPosA > 0 ) break;
			// otherwise, we are it
			si->m_alnumPosA = sn->m_alnumPosA;
		}

	}
	// propagate up alnumPosB now
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( ! ( sn->m_flags & SEC_SENTENCE ) ) continue;
		// propagate through parents
		Section *si = sn->m_parent;
		// do each parent as well
		for ( ; si ; si = si->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if already had one! no, because we need to
			// get the MAX of all of our kids!!
			//if ( si->m_alnumPosB > 0 ) break;
			// otherwise, we are it
			si->m_alnumPosB = sn->m_alnumPosB;
		}
	}
	m_alnumPosValid = true;


	/////////////////////////////
	//
	// set Section::m_rowNum and m_colNum for sections in table
	//
	// (we use this in Dates.cpp to set Date::m_tableRow/ColHeaderSec
	//  for detecting certain col/row headers like "buy tickets" that
	//  we use to invalidate dates as event dates)
	//
	/////////////////////////////
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *si = &m_sections[i];
		// need table tag
		if ( si->m_tagId != TAG_TABLE ) continue;
		// set all the headCol/RowSection ptrs rownum, colnum, etc.
		// just for this table.
		setTableRowsAndCols ( si );
	}

	// now NULLify either all headRow or all headColSections for this
	// table. dmjuice.com actually uses headRows and not columns. so
	// we need to detect if the table header is the first row or the first
	// column.
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *si = &m_sections[i];
		// need table tag
		if ( si->m_tagId != TAG_TABLE ) continue;
		// ok, now set table header bits for that table
		setTableHeaderBits ( si );
	}

	// propagate m_headCol/RowSection ptr to all kids in the td cell
	


	// . "ot" = occurence table
	// . we use this to set Section::m_occNum and m_numOccurences
	if ( ! m_ot.set (4,8,5000,NULL, 0 , false ,m_niceness,"sect-occrnc") )
		return true;

	// this tells us if the section had text or not
	//if ( ! m_cht2.set(4,0,5000,NULL, 0 , false ,m_niceness,"sect-cht2") )
	//	return true;


	// set the m_ot hash table for use below
	for ( int32_t i = 1 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// assume no vote
		uint64_t vote = 0;
		// try to get it
		uint64_t *vp = (uint64_t *)m_ot.getValue ( &sn->m_tagHash );
		// assume these are zeroes
		int32_t occNum = 0;
		// if there, set these to something
		if ( vp ) {
			// set it
			vote = *vp;
			// what section # are we for this tag hash?
			occNum = vote & 0xffffffff;
			// save our kid #
			sn->m_occNum = occNum;
			// get ptr to last section to have this tagHash
			//sn->m_prevSibling = (Section *)(vote>>32);
		}
		// mask our prevSibling
		vote &= 0x00000000ffffffff;
		// we are the new prevSiblinkg now
		vote |= ((uint64_t)((uint32_t)i))<<32; // rplcd sn w/ i
		// inc occNum for the next guy
		vote++;
		// store back. return true with g_errno set on error
		if ( ! m_ot.addKey ( &sn->m_tagHash , &vote ) ) return true;

		// use the secondary content hash which will be non-zero
		// if the section indirectly contains some text, i.e.
		// contains a subsection which directly contains text
		//int32_t ch = sn->m_contentHash;
		// skip if no content 
		//if ( ! ch ) continue;
		// use this as the "modified taghash" now
		//int32_t modified = sn->m_tagHash ^ ch;
		// add the content hash to this table as well!
		//if ( ! m_cht2.addKey ( &modified ) ) return true;
	}

	// . you are invalid in these sections
	// . google seems to index SEC_MARQUEE so i took that out of here
	//sec_t badFlags = SEC_SELECT|SEC_SCRIPT|SEC_STYLE;

	// . now we define SEC_UNIQUE using the tagHash and "ot"
	// . i think this is better than using m_tagHash
	// . basically you are a unique section if you have no siblings
	//   in your container 

	// . set Section::m_numOccurences
	// . the first section is the god section and has a 0 for tagHash
	//   so skip that!
	for ( int32_t i = 1 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// get it
		uint64_t vote ;
		// assign it
		uint64_t *slot = (uint64_t *)m_ot.getValue ( &sn->m_tagHash );
		// wtf? i've seen this happen once in a blue moon
		if ( ! slot ) {
			log("build: m_ot slot is NULL! wtf?");
			continue;
		}
		// otherwise, use it
		vote = *slot;
		// get slot for it
		int32_t numKids = vote & 0xffffffff;
		// must be at least 1 i guess
		if ( numKids < 1 && i > 0 ) { char *xx=NULL;*xx=0; }
		// how many siblings do we have total?
		sn->m_numOccurences = numKids;
		// sanity check
		if ( sn->m_a < 0    ) { char *xx=NULL;*xx=0; }
		if ( sn->m_b > m_nw ) { char *xx=NULL;*xx=0; }
		//
		// !!!!!!!!!!!!!!!! HEART OF SECTIONS !!!!!!!!!!!!!!!!
		//
		// to be a valid section we must be the sole section 
		// containing at least one alnum word AND it must NOT be
		// in a script/style/select/marquee tag
		//if ( sn->m_exclusive>0 && !(sn->m_flags & badFlags) )
		//	continue;
		// this means we are useless
		//sn->m_flags |= SEC_NOTEXT;
		// and count them
		//m_numInvalids++;
	}

	///////////////////////////////////////
	//
	// now set Section::m_listContainer
	//
	// . a containing section is a section containing
	//   MULTIPLE smaller sections
	// . so if a section has a containing section set its m_listContainer
	//   to that containing section
	// . we limit this to sections that directly contain text for now
	// . Events.cpp::getRegistrationTable() uses m_nextBrother so we
	//   need this now!!
	//
	///////////////////////////////////////
	setNextBrotherPtrs ( true ); // setContainer = true


	///////////////////////////////////////
	//
	// now set SEC_MENU and SEC_LINK_TEXT flags
	//
	///////////////////////////////////////
	setMenus();

	///////////////////////////////////////
	//
	// now set SENT_LIST flags on m_sentFlags
	//
	// try to capture sentences that are not menus but are a list of
	// things. if the sentence itself has a list of int16_t items, or a bunch
	// of commas, then also set the SEC_LIST flag on it. or if sentence
	// is part of a sequence of sentences that are a list of sentences then
	// set it for them as well. typically such sentences will be separated
	// by a vertical space, have no periods, maybe have an <li> tag or only
	// have a few words per sentence. this will help us demote search results
	// that have the query terms in such a list because it is usually not
	// very useful information.
	//
	///////////////////////////////////////
	setListFlags();


	//verifySections();

	// don't use nsvt/osvt now
	return true;

	/*
	// . init this hashtable here
	// . osvt = old section voting table
	// . this holds all the votes we got exclusively from sectiondb
	// . sectiondb maps a siteHash/tagHash pair to a SectionVote
	// . this table maps a tagHash to a single accumulated SectionVote
	if ( ! m_osvt.set(8,sizeof(SectionVote),1000 ,NULL,0,false,m_niceness,
			  "osvt"))
		return true;


	// . init this hashtable here
	// . nsvt = new section voting table
	// . this holds all the votes we computed
	if ( ! m_nsvt.set(8,sizeof(SectionVote),4096,NULL,0,false,m_niceness,
			  "nsvt"))
		return true;
	*/

	/*
	  - mdw: i got rid of m_plain, so i commented this out
	// put votes on SV_TEXTY section type into m_nsvt
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get the ith section
		Section *sn = &m_sections[i];
		// skip if not a section with its own words
		if ( sn->m_flags & SEC_NOTEXT ) continue;
		// . set SEC_TEXTY for "texty" sections
		// . are we an article section?
		float percent = //100.0 * 
			(float)(sn->m_plain) / 
			(float)(sn->m_exclusive);
		// . update m_nsvt voting table now
		// . this will return false with g_errno set
		if ( ! addVote ( &m_nsvt       ,
				 sn->m_tagHash ,
				 SV_TEXTY      , 
				 percent       ,
				 1.0           ) )
			return true;
	}
	*/

	/*
	// . add our SV_TAG_PAIR hash to the m_nsvt
	// . every page adds this one
	// . but don't screw up m_nsvt if we are deserializing from it
	if ( ! sectionsVotes &&
	     ! addVote(&m_nsvt,m_tagPairHash,SV_TAGPAIRHASH,1,1)) return true;

	// . add our checksum votes as well
	// . we use this for menu identification between web pages
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get the ith section
		Section *sn = &m_sections[i];
		// skip if not a section with its own words
		if ( sn->m_flags & SEC_NOTEXT ) continue;
		// combine the tag hash with the content hash #2
		int32_t modified = sn->m_tagHash ^ sn->m_contentHash;
		// . update m_nsvt voting table now
		// . the tagHash is the content hash for this one
		// . this will return false with g_errno set
		if ( ! addVote ( &m_nsvt           ,
				 //sn->m_contentHash ,
				 modified          ,
				 SV_TAGCONTENTHASH    , 
				 1.0               ,   // score
				 1.0               ,
				 true              ) )
			return true;
	}
	*/
	/*
	// if no url, must be being called from below around compare() on
	// a title rec looked up in titledb that we are comparing to
	if ( ! url ) return true;

	// . now combine tag pair hash with the site hash
	// . tag pair hash is a hash of the hashes of all the unique adjacent
	//   tag pairs. a great way to identify the html format of a doc
	// . see XmlDoc::getTagPairHash() for that code...
	// . tagPairHash is also used by DupDetector.cpp
	//m_termId = hash64 ( tagPairHash , m_siteHash ) & TERMID_MASK;

	// remove tagPairHash now that each SectionVote has a m_contentHash
	m_termId = m_siteHash64 & TERMID_MASK;


	if ( sectionsVotes ) {
		// sanity check, this data is only used for when
		// XmlDoc::m_skipIndexing is true and it zeroes out the content
		// so we have to save our voting info since the content is
		// gone. that way we can delete it from sectiondb later if
		// we need to.
		if ( m_numSections > 0 ) { char *xx=NULL;*xx=0; }
		// . set our m_osvt table from what's left
		// . this returns false and sets g_errno on error
		if ( ! m_nsvt.deserialize(sectionsVotes,-1,m_niceness) ) 
			return true;
	}

	// . if we are setting from a TitleRec stored in titledb, then
	//   do not consult datedb because it might have changed since we 
	//   first indexed this document. instead deserialize m_osvt from
	//   TitleRec::ptr_sectionsData.
	// . this ensures we parse exactly the same way as we did while we
	//   were creating this title rec
	// . this ensures parsing consistency
	// . basically, we m_osvt is a compressed version of the list we
	//   read from datedb, combined with any "votes" from the title rec 
	//   comparisons we might have done in Sections::compare()
	if ( sectionsDataValid ) { // xd && xd->ptr_sectionsData ) {
		// point to it
		char *p = sectionsData;
		// . set our m_osvt table from what's left
		// . this returns false and sets g_errno on error
		if ( p && ! m_osvt.deserialize(p,-1,m_niceness) ) 
			return true;
		// now we can finalize, m_osvt is already set
		return gotSectiondbList ( NULL );
	}

	// . for us, dates are really containers of the flags and tag hash
	// . init this up here, it is re-set if we re-call getSectiondbList()
	//   because there were too many records in it to handle in one read
	m_startKey = g_datedb.makeStartKey ( m_termId , 0xffffffff );

	// reset this count
	m_totalSiteVoters = 0;

	// call it
	return getSectiondbList ( );
	*/
}

// . returns false and sets g_errno on error
// . XmlDoc.cpp calls this separately from Sections::set because it needs
//   to set Addresses and Dates class partially first so we can use them here.
bool Sections::addImpliedSections ( Addresses *aa ) {

	// only call once
	if ( m_addedImpliedSections ) return true;
	m_addedImpliedSections = true;

	// no point in going any further if we have nothing
	if ( m_numSections == 0 ) return true;

	// set this
	//m_osvt = osvt;

	//
	// now do table swoggling
	//
	// turn off table rows and use columns if necessary.
	// some tables are column based not row-based and we have to fix that.
	//
	//if ( ! swoggleTables ( ) ) return false;

	// as part of a replacement for table swoggling which is confusing
	// and didn't really work right, especially when we had both 
	// table header row and column, we set these on each table cell:
	// SEC_HASDATEHEADERROW and SEC_HASDATEHEADERCOL
	if ( ! setTableStuff ( ) ) return false;

	m_aa = aa;

	// int16_tcut
	sec_t badFlags =SEC_MARQUEE|SEC_STYLE|SEC_SCRIPT|SEC_SELECT|
		SEC_HIDDEN|SEC_NOSCRIPT;


	// now we use m_numTotalPtrs, not m_numDatePtrs:
	// for santafeplayhouse.org one compound date spans
	// multiple sections. the first section is the DOW like
	// "Friday" and the next section is the "day of months list".
	// we want to add implied sections whose header is above
	// the "day of months list" using the _ABOVE_DOM method,
	// but it won't work unless we set these bits here properly.
	// if we can't add that implied section everyone now uses
	// the "Thursdays Pay for Performance" as their title because
	// Thursdays is now fuzzy since we added the isEventEnding()
	// algo to fix the themixtress.com which has Wednesdays in a
	// band name and it should be fuzzy. there are other ways to
	// fix themixtress.com, mainly ignore "Wednesdays" as a
	// recurring date unless following "every", etc., but
	// santafeplayhouse.org might not have had "Thursdays" in
	// that event title in which case we'd have to rely on 
	// implied sections... so let's do this right...
	
	// scan the dates and set a section's SEC_HAS_DOM/DOW
	// and SEC_HAS_TOD for all the sections containing it either directly
	// or indirectly
	for ( int32_t i = 0 ; i < m_dates->m_numTotalPtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_dates->m_totalPtrs[i];
		// skip if nuked. part of a compound, or in bad section.
		if ( ! di ) continue;
		// must be in body
		if ( ! ( di->m_flags & DF_FROM_BODY ) ) continue;
		// ignore, otherwise the trumba sections get messed up
		// because we use the official timestamps (with the "T" in
		// them) as a month for adding implied sections!
		if ( di->m_flags5 & DF5_IGNORE ) 
			continue;
		// skip if compound, list, range, telescope
		if ( di->m_numPtrs != 0 ) continue;
		// sanity check
		if ( di->m_a < 0 ) { char *xx=NULL;*xx=0; }
		// int16_tcut
		datetype_t dt = di->m_hasType;
		// need a DOW, DOM or TOD
		sec_t flags = 0;
		// get smallesst containing section
		Section *sa = m_sectionPtrs [ di->m_a];
		// . ignore <option> tags etc. to fix burtstikilounge.com
		// . was causing "Nov 2009" to be a date brother with
		//   its daynums in the calendar select tag dropdown
		if ( sa->m_flags & badFlags ) continue;

		// are we a header?
		//bool header = false;
		//if ( sa->m_flags & SEC_HEADING ) header = true;
		//if ( sa->m_flags & SEC_HEADING_CONTAINER) header = true;

		// day of month is good
		//if ( header && (dt & DT_MONTH) && (dt & DT_DAYNUM) ) 
		// allow non-header guys in so we can pick up setence-y 
		// sections. the additional logic in getDelimScore() should
		// cover us...
		// ALLOWING just daynum messes up stubhub.com because we
		// think every little number in every xml tag is a daynum!
		if ( (dt & DT_DAYNUM) ) // && (dt != DT_DAYNUM) ) // && (dt & DT_MONTH) ) 
			flags |= SEC_HAS_DOM;
		if ( dt & DT_MONTH )
			flags |= SEC_HAS_MONTH;
		// or day of week
		//if ( header && (dt & DT_DOW) )
		// require it be strong so things like "Hillbilly Thursday"
		// in southgatehouse.com which is a band name i think does
		// not cause SEC_DATE_BOUNDARY to be set for that list of
		// brothers, thereby killing its sections ability to telescope
		// to its day of month.
		if ( (di->m_flags & DF_HAS_STRONG_DOW) &&  // dt & DT_DOW)
		     // do not count "before 4th Saturday" for folkmads.org
		     // because it is not really a dow in this sense
		     !(di->m_flags & DF_ONGOING) )
			flags |= SEC_HAS_DOW;
		// to fix santafeplayhouse.org they have some headers
		// that are like "Thursday Pay What You Wish Performances"
		if ( dt & DT_DOW ) 
			flags |= SEC_HAS_DOW;
		// tod is good (ignore "before 11pm" for this)
		if ( di->m_flags & (DF_AFTER_TOD|DF_EXACT_TOD) )
			flags |= SEC_HAS_TOD;
		// skip fuzzies - is DF_FUZZY set for this? in parseDates?
		if ( dt == DT_DAYNUM && (di->m_flags & DF_FUZZY) )
			continue;
		// any date?
		//flags |= SEC_HAS_DATE;
		// skip if none
		if ( ! flags ) continue;
		// mark all sections
		for ( ; sa ; sa = sa->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// if already set, stop
			if ( (sa->m_flags & flags) == flags ) break;
			// set it
			sa->m_flags |= flags;
		}
	}
			
	// this is needed by setSentFlags()
	//setNextSentPtrs();

	// returns false and sets g_errno on error
	if ( ! setSentFlagsPart2 ( ) ) return false;


	// i would say <hr> is kinda like an <h0>, so do it first
	if ( splitSectionsByTag ( TAG_HR ) > 0 ) setNextBrotherPtrs ( false );

	// split by double br tags AFTER we've add all the text-based
	// implied sections that we can. 0x01 = <br><br>.
	// really instead of using back to back br tags they could have
	// put everything into <p> tags which would imply these sections
	// anyhow! this will fix sunsetpromotions which has no real
	// sections except these brbr things.
	//splitSections ( (char *)0x01 , (int32_t)BH_BRBR );
	// HACK: back to back br tags
	// . this also splits on any double space really:
	//   including text-free tr tags
	if ( splitSectionsByTag ( TAG_BR ) > 0 ) setNextBrotherPtrs ( false );

	// we now have METHOD_ABOVE_ADDR so need addr xor
	setAddrXors( m_aa );

	//
	// now add implied sections based on dates
	//
 again:
	int32_t added = addImpliedSections3();
	// g_errno should be set when we return -1 on error
	if ( added < 0 ) return true;
	// if added something, try again
	if ( added > 0 ) goto again;
	// just in case we added more sections
	setNextBrotherPtrs ( false );

	// . the header tags
	// . no! graffiti.org uses header tags for just making font size
	//   large... doing these messes up our implied sections so that
	//   the monthday range is excised from the event description
	// . So that means we have to be damn sure to get those implied
	//   date based sections in for graffiti.org then! but we do need
	//   these header tag splitters for unm.edu which has them above
	//   some tables, and for abqfolkfest.org which also does, like
	//   for "JAM SESSIONS"
	if (splitSectionsByTag ( TAG_H1 ) > 0 ) setNextBrotherPtrs ( false );
	if (splitSectionsByTag ( TAG_H2 ) > 0 ) setNextBrotherPtrs ( false );
	if (splitSectionsByTag ( TAG_H3 ) > 0 ) setNextBrotherPtrs ( false );
	if (splitSectionsByTag ( TAG_H4 ) > 0 ) setNextBrotherPtrs ( false );
	if (splitSectionsByTag ( TAG_H5 ) > 0 ) setNextBrotherPtrs ( false );

	// fix pacificmedicalcenters.org
	if ( splitSectionsByTag ( TAG_DT ) > 0 ) setNextBrotherPtrs ( false );

	// &bull;
	char bullet[4];
	bullet[0] = 226;
	bullet[1] = 128;
	bullet[2] = 162;
	bullet[3] = 0;
	splitSections ( bullet , BH_BULLET );

	// just in case we added more sections
	setNextBrotherPtrs ( false );

	// . try adding implied sections again, might getter better 
	//   similarities now that we have different subsections to divide
	// . should fix santafeplayhouse.org
 again2:
	int32_t added2 = addImpliedSections3();
	// g_errno should be set when we return -1 on error
	if ( added2 < 0 ) return true;
	// if added something, try again
	if ( added2 > 0 ) goto again2;
	// just in case we added more sections
	setNextBrotherPtrs ( false );



	// now set it again after adding implied sections
	setTagHashes();
	
	// this is used by Events.cpp Section::m_nextSent
	setNextSentPtrs();

	//
	// redo the content hash now
	//
	// set checksum of implied section
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// reset
		sk->m_contentHash64 = 0;
		// remove this flag
		sk->m_flags &= ~SEC_NOTEXT;
	}
	// now set the content hash of each section
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// must be an alnum word
		if ( ! m_wids[i] ) continue;
		// get its section
		m_sectionPtrs[i]->m_contentHash64 ^= m_wids[i];
		// fix "smooth smooth!"
		if ( m_sectionPtrs[i]->m_contentHash64 == 0 )
			m_sectionPtrs[i]->m_contentHash64 = 123456;
	}
	// now set SEC_NOTEXT flag if content hash is zero!
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Section *sn = &m_sections[i];
		// skip if had text
		if ( sn->m_contentHash64 ) continue;
		// no text!
		sn->m_flags |= SEC_NOTEXT;
	}

	///////////////////
	//
	// set form tables
	//
	// set SENT_FORMTABLE_FIELD and SENT_FORMTABLE_VALUE on sentences
	// like "Price:</td><td> $10-$20\n"  or "Good For Kids:</td><td>Yes"
	//
	///////////////////
	setFormTableBits();

	// now set the stuff that depends on the voting table now that
	// all our implied sections are added since XmlDoc::getVotingTable()
	// adds the votes/hashes after all the implied sections are in.

	/*
	/////////////////////////////
	//
	// set SENT_ONROOTPAGE
	//
	/////////////////////////////
	// . now use svt
	// . do this before telescoping because if one date in a telescope 
	//   does not have DF_ONROOTPAGE set then we clear that bit for the
	//   telescope
	// . we hashed all the sections containing any tod ranges on the root
	//   page, assuming it to be store hours.
	// . we hash the tagid with its content hash for each section as we
	//   telescoped up hashing in more tagids, up to 5.
	// . so how many layers of the onion can we match? 
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if not there
		if ( ! m_osvt ) break;
		// stop if empty
		if ( m_osvt->isTableEmpty() ) break;
		// only works on sentences for now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// get the date root hash
		uint32_t modified = getDateSectionHash ( si );
		// and see if it exists in the root doc
		if ( ! m_osvt->isInTable ( &modified ) ) continue;
		// it does! so set date flag as being from root
		si->m_sentFlags |= SENT_ONROOTPAGE;
	}
	*/
	return true;
}

// place name indicators
static HashTableX s_pit;
static char s_pitbuf[2000];
static bool s_init9 = false;

bool initPlaceIndicatorTable ( ) {
	if ( s_init9 ) return true;
	// init place name indicators
	static char *s_pindicators [] = {
		"airport",
		"airstrip",
		"auditorium",
		//"area",
		"arena",
		"arroyo",
		// wonderland ballroom thewonderlandballroom.com
		"ballroom",
		"bank",
		"banks",
		"bar",
		"base",
		"basin",
		"bay",
		"beach",
		"bluff",
		"bog",
		//"boundary",
		"branch",
		"bridge",
		"brook",
		"building",
		"bunker",
		"burro",
		"butte",
		"bookstore", // st john's college bookstore
		"enterprises", // J & M Enterprises constantcontact.com
		"llc",  // why did we remove these before???
		"inc",
		"incorporated",
		"cabin",
		"camp",
		"campground",
		"campgrounds",
		// broke "Tennis on Campus" event
		//"campus",
		"canal",
		"canyon",
		"casa",
		"castle",
		"cathedral",
		"cave",
		"cemetery",
		"cellar", // the wine cellar
		"center",
		"centre",
		// channel 13 news
		//"channel",
		"chapel",
		"church",
		// bible study circle
		//"circle",
		"cliffs",
		"clinic",
		"college",
		"company",
		"complex",
		"corner",
		"cottage",
		"course",
		"courthouse",
		"courtyard",
		"cove",
		"creek",
		"dam",
		"den",
		// subplace
		//"department",
		"depot",
		"dome",
		"downs",
		//"fair", // xxx fair is more of an event name!!
		"fairgrounds",
		"fairground",
		"forum",
		"jcc",

		"playground",
		"playgrounds",
		"falls",
		"farm",
		"farms",
		"field",
		"fields",
		"flat",
		"flats",
		"forest",
		"fort",
		//"fountain",
		"garden",
		"gardens",
		//"gate",
		"glacier",
		"graveyard",
		"gulch",
		"gully",
		"hacienda",
		"hall",
		//"halls",
		"harbor",
		"harbour",
		"hatchery",
		"headquarters",
		//"heights",
		"heliport",
		// nob hill
		//"hill",
		"hillside",
		"hilton",
		"historical",
		"historic",
		"holy",
		// members home
		//"home",
		"homestead",
		"horn",
		"hospital",
		"hotel",
		"house",
		"howard",
		"inlet",
		"inn",
		"institute",
		"international",
		"isla",
		"island",
		"isle",
		"islet",
		"junction",
		"knoll",
		"lagoon",
		"laguna",
		"lake",
		"landing",
		"ledge",
		"lighthouse",
		"lodge",
		"lookout",
		"mall",
		"manor",
		"marina",
		"meadow",
		"mine",
		"mines",
		"monument",
		"motel",
		"museum",
		// subplace
		//"office",
		"outlet",
		"palace",
		"park",
		"peaks",
		"peninsula",
		"pit",
		"plains",
		"plant",
		"plantation",
		"plateau",
		"playa",
		"plaza",
		"point",
		"pointe",
		"pond",
		"pool", // swimming pool
		"port",
		"railroad",
		"ramada",
		"ranch",
		// rio rancho
		//"rancho",
		// date range
		//"range",
		"reef",
		"refure",
		"preserve", // nature preserve
		"reserve",
		"reservoir",
		"residence",
		"resort",
		//"rio",
		//"river",
		//"riverside",
		//"riverview",
		//"rock",
		"sands",
		"sawmill",
		"school",
		"schoolhouse",
		"shore",
		"spa",
		"speedway",
		"spring",
		"springs",
		"stadium",
		"station",
		"strip",
		"suite",
		"suites",
		"temple",
		"terrace",
		"tower",
		//"trail",
		"travelodge",
		"triangle",
		"tunnel",
		"university",
		//"valley",
		"wall",
		"ward",
		"waterhole",
		"waters",
		"well",
		"wells",
		"wilderness",
		"windmill",
		"woodland",
		"woods",
		"gallery",
		"theater",
		"theatre",
		"cinema",
		"cinemas",
		"playhouse",
		"saloon",
		"nightclub", // guys n' dolls restaurant and night club
		"lounge",
		"ultralounge",
		"brewery",
		"chophouse",
		"tavern",
		"company",
		"rotisserie",
		"bistro",
		"parlor",
		"studio",
		"studios",
		// denver, co
		//"co",
		"bureau",
		//"estates",
		"dockyard",
		"gym",
		"synagogue",
		"shrine",
		"mosque",
		"store",
		"mercantile",
		"mart",
		"amphitheatre",
		"kitchen",
		"casino",
		"diner",
		"eatery",
		"shop",
		//"inc",
		//"incorporated",
		//"corporation",
		//"limited",
		//"llc",
		//"foundation",
		"warehouse",
		"roadhouse",
		"foods",
		"cantina",
		"steakhouse",
		"smokehouse",
		"deli",
		//"enterprises",
		//"repair",
		//"service",
		// group services
		//"services",
		//"systems",
		"salon",
		"boutique",
		"preschool",
		//"galleries",
		"bakery",
		"factory",
		//"llp",
		//"attorney",
		//"association",
		//"solutions",
		"facility",
		"cannery",
		"winery",
		"mill",
		"quarry",
		"monastery",
		"observatory",
		"nursery",
		"pagoda",
		"pier",
		"prison",
		//"post",
		"ruin",
		"ruins",
		"storehouse",
		"square",
		"tomb",
		"wharf",
		"zoo",
		"mesa",
		// five day pass
		//"pass",
		"passage",
		"peak",
		"vineyard",
		"vineyards",
		"grove",
		"space",
		"library",
		"bakery", // a b c bakery
		"school",
		"church",
		"park",
		"house",
		//"market",  hurt Los Ranchos Growers' Market
		//"marketplace",
		"university",
		"center",
		"restaurant",
		"bar",
		"grill",
		"grille",
		"cafe",
		"cabana",
		"shack",
		"shoppe",
		"collesium",
		"colliseum",
		"pavilion"
		//"club"
	};
	// store these words into table
	s_init9 = true;
	s_pit.set(8,0,128,s_pitbuf,2000,false,0,"ti1tab");
	int32_t n = (int32_t)sizeof(s_pindicators)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// set words
		char *s = s_pindicators[i];
		int64_t h = hash64n(s);
		s_pit.addKey ( &h );
	}
	return true;
}

bool isPlaceIndicator ( int64_t *widp ) {
	if ( ! s_init9 ) initPlaceIndicatorTable();
	return s_pit.isInTable ( widp );
}

static HashTableX s_igt;
static char s_igtbuf[10000];
static bool s_init3 = false;

void initGenericTable ( int32_t niceness ) {

	// . now we create strings of things that are somewhat generic
	//   and should be excluded from titles. 
	// . we set the GENERIC bit for each word or phrase in the sentence
	//   that matches one in this list of generic words/phrases
	// . then we ignore the longest generic phrase that is two words or
	//   more, within the sentence, for the purposes of forming titles
	// . so if we had "buy tickets for Spiderman" we would ignore
	//   "buy tickets for" and the title would just be Spiderman
	// . we also set the GENERIC bit for dates and places that match
	//   that of the event in addition to these phrases/words
	static char *s_ignore[] = {
		"repeats",
		"various", // this event repeats on various days
		"feed",
		"size", // small class size: hadcolon algo fix
		"readers",
		"rating",
		"publish",
		"category",
		"special",
		"guest",
		"guests",
		"sold", // sold out
		"out",
		"tba",
		"promotional", // promotional partners
		"partners",
		"regular", // regular ticket price
		"fee",
		"fees",
		"purchase",
		"how",
		"order", // how to order
		"redeem",
		"promo", // promo code
		"code",
		"genre",
		"type",
		"price",
		"prices", // ticket prices
		"priced",
		"pricing", // pricing information
		"bid", // minimum bid: $50,000
		"bids",
		"attn",
		"pm",
		"am",
		"store", // fix store: open blah blah... for hadcolon
		"default", // fix default: all about... title for had colon
		"id", // event id: 1234
		"buffer", // fix losing Life of Brian to Buffer:33% title
		"coming",
		"soon",
		"deadline", // price deadline
		"place", // place order
		"order", 
		"users",
		"claim",
		"it",
		"set", // set: 10:30pm  (when they start the set i guess)
		"transportation",
		"organization",
		"company",
		"important",
		"faq",
		"faqs",
		"instructions",
		"instruction",
		"advertise",
		"advertisement",
		"name",
		"upcoming",
		"attraction",
		"attractions",
		"events",
		"news", // news & events
		"posted", // posted by, posted in news
		"is",
		"your",
		"user",
		"reviews",
		"most",
		"recent",
		"latest",
		"comments",
		"bookmark",
		"creator",
		"tags",
		"close",
		"closes",
		"closed",
		"send",
		"map",
		"maps",
		"directions",
		"driving",
		"help",
		"read", // read more
		"more",
		"every", // every other wednesday
		"availability",
		"schedule",
		"scheduling", // scheduling a program
		"program",
		"other",
		"log",
		"sign",
		"up",
		"login",
		"logged", // you're not logged in
		"you're",
		"should", // you should register
		"register",
		"registration", // registration fees
		"back",
		"change",
		"write",
		"save",
		"add",
		"share",
		"forgot",
		"password",
		"home",
		"hourly", // hourly fee
		"hours",
		"operation", // hours of operation
		"public", // hours open to the public.. public tours
		"what's",
		"happening",
		"menu",
		"plan", // plan a meeting
		"sitemap",
		"advanced",
		"beginner", // dance classes
		"intermediate",
		"basic",
		"beginners",
		// beginning hebrew conversation class?
		// 6 Mondays beginning Feb 7th - Mar 14th...
		"beginning", 

		"calendar", // calendar for jan 2012
		"full", // full calendar
		"hello", // <date> hello there guest
		"level", // dance classes
		"open", // open level
		"go",
		"homepage",
		"website",
		"view",
		"submit",
		"get",
		"subscribe",
		"loading",
		"last",
		"modified",
		"updated",

		"untitled", // untitled document for nonamejustfriends.com
		"document",
		"none", // none specified
		"specified",
		"age", // age suitability
		"suitability",
		"requirement", // age requirement
		"average", // average ratings
		"ratings",
		"business", // business categories
		"categories",
		"narrow", // narrow search
		"related", // related topics/search
		"topics",
		"searches",
		"sort", // sort by

		"there",
		"seating", // there will be no late seating
		"seats", // seats are limited
		"accomodations", // find accomodations
		"request", // request registration
		"requests",
		
		"are",
		"maximum", // maximum capacity
		"max", // maximum capacity
		"capacity", // capcity 150 guests
		"early",
		"late", // Wed - Thurs 1am to Late Morning
		"latest", // latest performance
		"performance", 
		"performances", // performances: fridays & saturdays at 8 PM..
		"rsvp", // rsvp for free tickets
		"larger", // view larger map

		"special",
		"guest",
		"guests",
		"venue",
		"additional",
		"general",
		"admission",
		"information",
		"info",
		"what",
		"who",
		"where",
		"when",
		"tickets",
		"ticket",
		"tix", // buy tix
		"tuition",
		"tuitions",
		"donate",
		"donation", // $4 donation - drivechicago.com
		"donations",
		"booking",
		"now",
		"vip",
		"student",
		"students",
		"senior",
		"seniors",
		"sr",
		"mil",
		"military",
		"adult",
		"adults",
		"teen",
		"teens",
		"tween",
		"tweens",
		"elementary",
		"preschool",
		"preschl",
		"toddler",
		"toddlers",
		"tdlr",
		"family",
		"families",
		"children",
		"youth",
		"youngsters",
		"kids",
		"kid",
		"child",

		"find", // find us
		"next",
		"prev",
		"previous",
		"series", // next series

		"round", // open year round

		// weather reports!
		"currently", // currently: cloudy (hadcolon fix)
		"humidity",
		"light",
		"rain",
		"rainy",
		"snow",
		"cloudy",
		"sunny",
		"fair",
		"windy",
		"mostly", // mostly cloudy
		"partly", // partly cloudy
		"fahrenheit",
		"celsius",
		// "Wind: N at 7 mph"
		"wind",
		"mph",
		"n",
		"s",
		"e",
		"w",
		"ne",
		"nw",
		"se",
		"sw",

		// lottery title: %16.0 million
		"million",

		// 6th street
		"1st",
		"2nd",
		"3rd",
		"4th",
		"5th",
		"6th",
		"7th",
		"8th",
		"9th",

		"thanksgiving",
		"year",
		"yearly",
		"year's",
		"day",
		"night",
		"nightly",
		"daily",
		"christmas",

		"fall",
		"autumn",
		"winter",
		"spring",
		"summer",
		"season", // 2011 sesason schedule of events

		"session", // fall session
		"sessions", // fall sessions

		"jan",
		"feb",
		"mar",
		"apr",
		"may",
		"jun",
		"jul",
		"aug",
		"sep",
		"oct",
		"nov",
		"dec",
		"january",
		"february",
		"march",
		"april",
		"may",
		"june",
		"july",
		"august",
		"september",
		"october",
		"novemeber",
		"december",
		"gmt",
		"utc",
		"cst",
		"cdt",
		"est",
		"edt",
		"pst",
		"pdt",
		"mst",
		"mdt",

		"morning",
		"evening",
		"afternoon",
		"mornings",
		"evenings",
		"afternoons",

		"monday",
		"tuesday",
		"wednesday",
		"thursday",
		"friday",
		"saturday",
		"sunday",

		"mondays",
		"tuesdays",
		"wednesdays",
		"thursdays",
		//"fridays", // hurts "5,000 Fridays" title for when.com
		"saturdays",
		"sundays", // all sundays

		"mwth",
		"mw",
		"mtw",
		"tuf",
		"thf",

		"tomorrow",
		"tomorrow's",
		"today",
		"today's",

		// we should probably treat these times like dusk/dawn
		"noon",
		"midday",
		"midnight",
		"sunset",
		"sundown",
		"dusk",
		"sunrise",
		"dawn",
		"sunup",

		"m",
		"mo",
		"mon",
		"tu",
		"tue",
		"tues",
		"wed",
		"weds",
		"wednes",
		"th",
		"thu",
		"thur",
		"thr",
		"thurs",
		"f",
		"fr",
		"fri",
		"sa",
		"sat",
		"su",
		"sun",

		"discount",
		"support",
		"featuring",
		"featured", // featured events
		"features", // more features
		"featuring",
		"presents",
		"presented",
		"presenting",
		"miscellaneous",

		"usa",
		"relevancy",
		"date",
		"time", // time:
		"showtime",
		"showtimes",
		"distance",
		"pacific", // pacific time
		"eastern",
		"central",
		"mountain",
		"cost", // cost:
		"per", // $50 per ticket
		"description",
		"buy",
		"become", // become a sponsor
		"twitter",
		"hashtag",
		"digg",
		"facebook",
		"like",
		"you",
		"fan", // become a facebook fan
		"media", // media sponsor
		"charity",
		"target", // Target:
		"now", // buy tickets now


		"reminder", // event remind
		"sponsors", // event sponsors
		"sponsor",
		"questions", // questions or comments
		"question",
		"comment", // question/comment
		"message",
		"wall", // "comment wall"
		"board", // "comment board" message board
		"other", // other events
		"ongoing", // other ongoing events
		"recurring",
		"repeating", // more repeating events
		"need", // need more information
		"quick", // quick links
		"links", // quick links
		"link",
		"calendar", // calendar of events
		"class", // class calendar
		"classes", // events & classes
		"schedule", // class schedule
		"activity", // activity calendar
		"typically",
		"usually",
		"normally",
		"some", // some saturdays
		"first",
		"second",
		"third",
		"fourth",
		"fifth",

		"city", // "city of albuquerque title

		"instructors", // 9/29 instructors:
		"instructor",
		"advisor", // hadcolon algo fix: Parent+Advisor:+Mrs.+Boey
		"advisors",
		"adviser",
		"advisers",
		"caller", // square dance field
		"callers", 
		"dj", // might be dj:
		"browse", // browse nearby
		"nearby",
		"restaurants", // nearby restaurants
		"restaurant",
		"bar",
		"bars",

		// why did i take these out??? leave them in the list, i
		// think these events are too generic. maybe because sometimes
		// that is the correct title and we shouldn't punish the
		// title score, but rather just filter the entire event 
		// because of that...?
		//"dinner", // fix "6:30 to 8PM: Dinner" title for la-bike.org
		//"lunch",
		//"breakfast",

		"served",
		"serving",
		"serves",
		"notes",
		"note",
		"announcement",
		"announcing",
		"first", // first to review
		"things", // things you must see
		"must",
		"see",
		"discover", // zvents: discover things to do
		"do",
		"touring", // touring region
		"region",
		"food",
		"counties",
		"tours",
		"tour",
		"tell", // tell a friend
		"friend", // tell a friend
		"about",
		"this",
		"occurs", // this event occurs weekly
		"weekly",

		"person",
		"group",
		"groups", // groups (15 or more)
		"our", // our story (about us)
		"story", // our story
		"special", // special offers
		"offers",
		//"bars", // nearby bars
		"people", // people who viewed this also viewed
		"also",
		"viewed",
		"additional", // additional service
		"real", // real reviews
		"united",
		"states",
		"over", // 21 and over only
		"advance", // $12 in adance / $4 at the door
		"list", // event list
		"mi",
		"miles",
		"km",
		"kilometers",
		"yes",
		"no",
		"false", // <visible>false</visible>
		"true",

		"usd", // currency units (eventbrite)
		"gbp", // currency units (eventbrite)

		"st", // sunday july 31 st
		"chat", // chat live
		"live",
		"have",
		"feedback",
		"dining", // dining and nightlife
		"nightlife", 
		"yet", // no comments yet
		"welcome", // stmargarets.com gets WELCOME as title
		"cancellation",
		"cancellations",
		"review",
		"preview",
		"previews",
		"overview",
		"overviews",
		"gallery", // gallery concerts
		"premium",
		"listing",
		"listings",
		"press",
		"releases", // press releases
		"release",  // press release
		"opening",
		"openings",
		"vip",
		"video",
		"audio",
		"radio",
		"yelp",
		"yahoo",
		"google",
		"mapquest",
		"quest", // map quest
		"microsoft",
		"eventbrite",
		"zvents",
		"zipscene",
		"eventful",
		"com", // .com
		"org", // .org
		"areas", // areas covered
		"covered",
		"cover", // cover charge
		"charge",
		"detail",
		"details",
		"phone",
		"tel",
		"telephone",
		"voice",
		"data",
		"ph",
		"tty",
		"tdd",
		"fax",
		"email",
		"e",
		"sale", // on sale now
		"sales", // Sales 7:30 pm Fridays
		"club", // club members
		"join",
		"please", // please join us at our monthly meetings
		"official", // official site link
		"site",
		"blog",
		"blogs", // blogs & sites
		"sites",
		"mail",
		"mailing", // mailing address
		"postal",
		"statewide", // preceeds phone # in unm.edu
		"toll",
		"tollfree",
		"call",
		"number",
		"parking",
		"limited", // limited parking available
		"available", // limited parking available
		"accepts", // accepts credit cards
		"accept",
		"visa", // we accept visa and mastercard
		"mastercard",
		//"jump", // jump to navigation
		"credit",
		"method",
		"methods", // methods of payment
		"payment",
		"payments",
		"cards",
		"zip", // POB 4321 zip 87197
		"admin", // 1201 third nw admin
		"meetings",
		"meeting",
		"meetup",
		"meets",
		"meet",
		"future", // other future dates & times (zvents)
		"dates",
		"times",
		"range", // price range (yelp.com blackbird buvette)
		"write" , // write a review
		"a",
		"performers", // performers at this event
		"band",
		"bands",
		"concert",
		"concerts",
		"hide" , // menu cruft from zvents
		"usa", // address junk
		"musicians", // category
		"musician", // category
		"current", // current weather
		"weather",
		"forecast", // 7-day forecast
		"contact",
		"us",
		"member",
		"members",
		"get", // get involved
		"involved",
		"press", // press room
		"room",
		"back", // back a page
		"page",
		"take", // take me to the top
		"me",
		"top", // top of page
		"print", // print friendly page
		"friendly",
		"description",
		"location",
		"locations",
		"street",
		"address",
		"neighborhood",
		"neighborhoods",
		"guide", // albuquerque guide for bird walk
		"ticketing",
		"software", // ticketing software
		"download",// software download
		"search",
		"results",
		"navigation", // main navigation
		"breadcrumb", // breadcrumb navigation
		"main",
		"skip", // skip to main content
		"content",
		"start",
		"starts", // starts 9/17/2011
		"starting", // starting in 2010
		"ends",
		"end",
		"ending",
		"begin",
		"begins" // concert begins at 8
		"beginning",
		"promptly", // starts promptly
		"will", // will begin
		"visit", // visit our website
		"visitors",
		"visitor",
		"visiting", // visiting hours
		"come", // come visit
		"check", // check us out
		"site",
		"website",
		"select", // select saturdays
		"begin",
		"ends", // ends 9/17/2011
		"multiple", // multiple dates
		"hottest", // hottest upcoming event
		"cancel",
		"displaying",
		"ordering", // ordering info from stjohnscollege
		"edit", // edit business info from yelp
		"of",
		"the",
		"and",
		"at",
		"to",
		"be",
		"or",
		"not",
		"in",
		"on",
		"only", // saturday and sunday only
		"winter", // winter hours: oct 15 - march 15 (unm.edu)
		"summer",  // summer hours
		"spring",
		"fall",

		"by",
		"under",
		"for",
		"from",
		"click",
		"here",
		"new",
		"free",
		"title",
		"event",
		"tbd", // title tbd
		"adv",
		"dos",
		"day",
		"days", // this event repeats on various days
		"week", // day(s) of the week
		"weekend",
		"weekends",
		"two", // two shows
		"runs", // show runs blah to blah (woodencowgallery.com)
		"show",
		"shows",
		"door",
		"doors",
		"gate", // gates open at 8
		"gates",
		"all",
		"ages",
		"admitted", // all ages admitted until 5pm (groundkontrol.com)
		"admittance",
		"until",
		"rights", // all rights reserved
		"reserved", // all rights reserved
		"reservations",
		"reserve",
		"permit", // special event permit application
		"application",
		"shipping", // shipping policy for tickets
		"policy",
		"policies",
		"package", // package includes
		"packages",
		"includes",
		"include",
		"including",
		"tweet",
		"print", // print entire month of events
		"entire",
		"month",
		"monthly",
		"21",
		"21+",
		"both",
		"nights",
		"box",
		"office",
		"this",
		"week",
		"tonight",
		"today",
		"http",
		"https",
		"open",
		"opens"
	};

	s_init3 = true;
	s_igt.set(8,0,512,s_igtbuf,10000,false,niceness,"igt-tab");
	int32_t n = (int32_t)sizeof(s_ignore)/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char      *w    = s_ignore[i];
		int32_t       wlen = gbstrlen ( w );
		int64_t  h    = hash64Lower_utf8 ( w , wlen );
		if ( ! s_igt.addKey (&h) ) { char *xx=NULL;*xx=0; }
	}
}

// so Dates.cpp DF_FUZZY algorithm can see if the DT_YEAR date is in
// a mixed case and period-ending sentence, in which case it will consider
// it to be fuzzy since it is not header material
bool Sections::setSentFlagsPart1 ( ) {

	// int16_tcut
	wbit_t *bits = m_bits->m_bits;

	static int64_t h_i;
	static int64_t h_com;
	static int64_t h_org;
	static int64_t h_net;
	static int64_t h_pg;
	static int64_t h_pg13;
	static bool s_init38 = false;
	if ( ! s_init38 ) {
		s_init38 = true;
		h_i = hash64n("i");
		h_com = hash64n("com");
		h_org = hash64n("org");
		h_net = hash64n("net");
		h_pg  = hash64n("pg");
		h_pg13  = hash64n("pg13");
	}

	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		///////////////////
		//
		// SENT_MIXED_CASE
		//
		///////////////////
		si->m_sentFlags |= getMixedCaseFlags ( m_words ,
						       bits    ,
						       si->m_senta   ,
						       si->m_sentb   ,
						       m_niceness );


		bool firstWord = true;
		int32_t lowerCount = 0;
		for ( int32_t i = si->m_senta ; i < si->m_sentb ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// are we a stop word?
			bool isStopWord = m_words->isStopWord(i);
			// .com is stop word
			if ( m_wids[i] == h_com ||
			     m_wids[i] == h_org ||
			     m_wids[i] == h_net ||
			     // fixes mr. movie times "PG-13"
			     m_wids[i] == h_pg  ||
			     m_wids[i] == h_pg13 )
				isStopWord = true;
			// are we upper case?
			bool upper = is_upper_utf8(m_wptrs[i]) ;
			// . are we all upper case?
			// . is every single letter upper case?
			// . this is a good tie break sometimes like for
			//   the santafeplayhouse.org A TUNA CHRISTMAS
			//if ( megaCaps ) {
			//	if ( ! upper ) megaCaps = false;
			// allow if hyphen preceedes like for
			// abqfolkfest.org's "Kay-lee"
			if ( i>0 && m_wptrs[i][-1]=='-' ) upper = true;
			// if we got mixed case, note that!
			if ( m_wids[i] &&
			     /*
			     ( m_wids[i] == h_of ||
			       m_wids[i] == h_the ||
			       m_wids[i] == h_at  ||
			       m_wids[i] == h_to  ||
			       m_wids[i] == h_be  ||
			       m_wids[i] == h_or  ||
			       m_wids[i] == h_not ||
			       m_wids[i] == h_in  ||
			       m_wids[i] == h_on  ||
			       m_wids[i] == h_for ||
			       m_wids[i] == h_from ||
			     */
			     //! ww->isStopWord(i) &&
			     ! is_digit(m_wptrs[i][0]) &&
			     ! upper &&
			     (! isStopWord || firstWord ) &&
			     // . November 4<sup>th</sup> for facebook.com
			     // . added "firstword" for "on AmericanTowns.com"
			     //   title prevention for americantowns.com
			     (m_wlens[i] >= 3 || firstWord) )
				lowerCount++;
			// no longer first word in sentence
			firstWord = false;
		}
		// does it end in period? slight penalty for that since
		// the ideal event title will not.
		// fixes events.kgoradio.com which was selecting the
		// first sentence in the description and not the performers
		// name for "Ragnar Bohlin" and "Malin Christennsson" whose
		// first sentence was for the most part properly capitalized
		// just by sheer luck because it used proper nouns and was
		// int16_t.
		bool endsInPeriod = false;
		char *p = NULL;
		//if ( si->m_b < m_nw ) p = m_wptrs[si->m_b];
		int32_t lastPunct = si->m_sentb;
		// skip over tags to fix nonamejustfriends.com sentence
		for ( ; lastPunct < m_nw && m_tids[lastPunct] ; lastPunct++);
		// now assume, possibly incorrectly, that it is punct
		if ( lastPunct < m_nw ) p = m_wptrs[lastPunct];
		// scan properly to 
		char *send = p + m_wlens[lastPunct];
		char *s    = p;
		for ( ; s && s < send ; s++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// check. might have ". or ).
			if ( *s == '.' || 
			     *s == ';' ||
			     *s == '?' ||
			     *s == '!' ) {
				// do not count ellipsis for this though
				// to fix eventbrite.com 
				// "NYC iPhone Boot Camp: ..."
				if ( s[1] != '.' ) endsInPeriod = true;
				break;
			}
		}
		//if ( p && p[0] == '.' ) endsInPeriod = true;
		//if ( p && p[0] == '?' ) endsInPeriod = true;
		//if ( p && p[0] == '!' ) endsInPeriod = true;
		//if ( p && p[1] == '.' ) endsInPeriod = false; // ellipsis
		if ( isAbbr(m_wids[si->m_b-1]) && m_wlens[si->m_b-1]>1 ) 
			endsInPeriod = false;
		if ( m_wlens[si->m_b-1] <= 1 &&
		     // fix "world war I"
		     m_wids[si->m_b-1] != h_i )
			endsInPeriod = false;
		if ( endsInPeriod ) {
			si->m_sentFlags |= SENT_PERIOD_ENDS;
			// double punish if also has a lower case word
			// that should not be lower case in a title
			if ( lowerCount > 0 )
				si->m_sentFlags |= SENT_PERIOD_ENDS_HARD;
		}
	}
	return true;
}

// returns false and sets g_errno on error
bool Sections::setSentFlagsPart2 ( ) {

	static bool s_init2 = false;
	static int64_t h_close ;
	static int64_t h_send ;
	static int64_t h_map ;
	static int64_t h_maps ;
	static int64_t h_directions ;
	static int64_t h_driving ;
	static int64_t h_help ;
	static int64_t h_more ;
	static int64_t h_log ;
	static int64_t h_sign ;
	static int64_t h_login ;
	static int64_t h_back ;
	static int64_t h_change ;
	static int64_t h_write ;
	static int64_t h_save ;
	static int64_t h_share ;
	static int64_t h_forgot ;
	static int64_t h_home ;
	static int64_t h_hours;
	static int64_t h_sitemap ;
	static int64_t h_advanced ;
	static int64_t h_go ;
	static int64_t h_website ;
	static int64_t h_view;
	static int64_t h_add;
	static int64_t h_submit;
	static int64_t h_get;
	static int64_t h_subscribe;
	static int64_t h_loading;
	static int64_t h_last;
	static int64_t h_modified;
	static int64_t h_updated;
	static int64_t h_special;
	static int64_t h_guest ;
	static int64_t h_guests ;
	static int64_t h_directed;
	static int64_t h_venue ;
	static int64_t h_instructor ;
	static int64_t h_general; 
	static int64_t h_information; 
	static int64_t h_info ;
	static int64_t h_i ;
	static int64_t h_what ;
	static int64_t h_who ;
	static int64_t h_tickets; 
	static int64_t h_support ;
	static int64_t h_featuring;
	static int64_t h_presents;
	static int64_t h_phone;
	static int64_t h_usa;
	static int64_t h_relevancy; // sort by option
	static int64_t h_buy;
	static int64_t h_where;
	static int64_t h_when;
	static int64_t h_contact;
	static int64_t h_description;
	static int64_t h_location;
	static int64_t h_located;
	static int64_t h_of;
	static int64_t h_the;
	static int64_t h_and;
	static int64_t h_at;
	static int64_t h_to;
	static int64_t h_be;
	static int64_t h_or;
	static int64_t h_not;
	static int64_t h_in;
	static int64_t h_on;
	static int64_t h_for;
	static int64_t h_with;
	static int64_t h_from;
	static int64_t h_click;
	static int64_t h_here;
	static int64_t h_new;
	static int64_t h_free;
	static int64_t h_title;
	static int64_t h_event;
	static int64_t h_adv;
	static int64_t h_dos;
	static int64_t h_advance;
	static int64_t h_day;
	static int64_t h_show;
	static int64_t h_box;
	static int64_t h_office;
	static int64_t h_this;
	static int64_t h_week;
	static int64_t h_tonight;
	static int64_t h_today;
	static int64_t h_http;
	static int64_t h_https;
	static int64_t h_claim;
	static int64_t h_it;
	static int64_t h_upcoming;
	static int64_t h_events;
	static int64_t h_is;
	static int64_t h_your;
	static int64_t h_user;
	static int64_t h_reviews;
	static int64_t h_comments;
	static int64_t h_bookmark;
	static int64_t h_creator;
	static int64_t h_tags;
	static int64_t h_repeats;
	static int64_t h_feed;
	static int64_t h_readers;
	static int64_t h_no;
	static int64_t h_rating;
	static int64_t h_publish;
	static int64_t h_category;
	static int64_t h_genre;
	static int64_t h_type;
	static int64_t h_price;
	static int64_t h_rate;
	static int64_t h_rates;
	static int64_t h_users;

	static int64_t h_date ;
	static int64_t h_time ;
	static int64_t h_other ;
	static int64_t h_future ;
	static int64_t h_dates ;
	static int64_t h_times ;
	static int64_t h_hide ;
	static int64_t h_print ;
	static int64_t h_powered;
	static int64_t h_provided;
	static int64_t h_admission;
	static int64_t h_by;
	static int64_t h_com;
	static int64_t h_org;
	static int64_t h_net;
	static int64_t h_pg;
	static int64_t h_pg13;

	static int64_t h_a;
	static int64_t h_use;
	static int64_t h_search;
	static int64_t h_find;
	static int64_t h_school;
	static int64_t h_shop;
	static int64_t h_gift;
	static int64_t h_gallery;
	static int64_t h_library;
	static int64_t h_photo;
	static int64_t h_image;
	static int64_t h_picture;
	static int64_t h_video;
	static int64_t h_media;
	static int64_t h_copyright;
	static int64_t h_review;
	static int64_t h_join;
	static int64_t h_request;
	static int64_t h_promote;
	static int64_t h_open;
	static int64_t h_house;
	static int64_t h_million;
	static int64_t h_billion;
	static int64_t h_thousand;

	if ( ! s_init2 ) {
		s_init2 = true;
		h_repeats = hash64n("repeats");
		h_feed = hash64n("feed");
		h_readers = hash64n("readers");
		h_no = hash64n("no");
		h_rating = hash64n("rating");
		h_publish = hash64n("publish");
		h_category = hash64n("category");
		h_genre = hash64n("genre");
		h_type = hash64n("type");
		h_price = hash64n("price");
		h_rate = hash64n("rate");
		h_rates = hash64n("rates");
		h_users = hash64n("users");
		h_claim = hash64n("claim");
		h_it = hash64n("it");
		h_upcoming = hash64n("upcoming");
		h_events = hash64n("events");
		h_is = hash64n("is");
		h_your = hash64n("your");
		h_user = hash64n("user");
		h_reviews = hash64n("reviews");
		h_comments = hash64n("comments");
		h_bookmark = hash64n("bookmark");
		h_creator = hash64n("creator");
		h_close = hash64n("close");
		h_tags = hash64n("tags");
		h_send = hash64n("send");
		h_map = hash64n("map");
		h_maps = hash64n("maps");
		h_directions = hash64n("directions");
		h_driving = hash64n("driving");
		h_help = hash64n("help");
		h_more = hash64n("more");
		h_log = hash64n("log");
		h_sign = hash64n("sign");
		h_login = hash64n("login");
		h_back = hash64n("back");
		h_change = hash64n("change");
		h_write = hash64n("write");
		h_save = hash64n("save");
		h_add = hash64n("add");
		h_share = hash64n("share");
		h_forgot = hash64n("forgot");
		h_home = hash64n("home");
		h_hours = hash64n("hours");
		h_sitemap = hash64n("sitemap");
		h_advanced = hash64n("advanced");
		h_go = hash64n("go");
		h_website = hash64n("website");
		h_view = hash64n("view");
		h_submit = hash64n("submit");
		h_get = hash64n("get");
		h_subscribe = hash64n("subscribe");
		h_loading = hash64n("loading");
		h_last = hash64n("last");
		h_modified = hash64n("modified");
		h_updated = hash64n("updated");

		h_special = hash64n("special");
		h_guest = hash64n("guest");
		h_guests = hash64n("guests");
		h_directed = hash64n("directed");
		h_venue = hash64n("venue");
		h_instructor = hash64n("instructor");
		h_general = hash64n("general");
		h_information = hash64n("information");
		h_info = hash64n("info");
		h_what = hash64n("what");
		h_who = hash64n("who");
		h_tickets = hash64n("tickets");
		h_support = hash64n("support");
		h_featuring = hash64n("featuring");
		h_presents = hash64n("presents");

		h_phone = hash64n("phone");
		h_usa = hash64n("usa");
		h_relevancy = hash64n("relevancy");
		h_date = hash64n("date");
		h_description = hash64n("description");
		h_buy = hash64n("buy");
		h_where = hash64n("where");
		h_when = hash64n("when");
		h_contact = hash64n("contact");
		h_description = hash64n("description");
		h_location = hash64n("location");
		h_located = hash64n("located");
		h_of = hash64n("of");
		h_the = hash64n("the");
		h_and = hash64n("and");
		h_at = hash64n("at");
		h_to = hash64n("to");
		h_be = hash64n("be");
		h_or = hash64n("or");
		h_not = hash64n("not");
		h_in = hash64n("in");
		h_on = hash64n("on");
		h_for = hash64n("for");
		h_with = hash64n("with");
		h_from = hash64n("from");
		h_click = hash64n("click");
		h_here = hash64n("here");
		h_new = hash64n("new");
		h_free = hash64n("free");
		h_title = hash64n("title");
		h_event = hash64n("event");
		h_adv = hash64n("adv");
		h_dos = hash64n("dos");
		h_day = hash64n("day");
		h_show = hash64n("show");
		h_box = hash64n("box");
		h_i = hash64n("i");
		h_office = hash64n("office");
		h_this = hash64n("this");
		h_week = hash64n("week");
		h_tonight = hash64n("tonight");
		h_today = hash64n("today");
		h_http = hash64n("http");
		h_https = hash64n("https");
		h_date = hash64n("date");
		h_time = hash64n("time");
		h_other = hash64n("other");
		h_future = hash64n("future");
		h_dates = hash64n("dates");
		h_times = hash64n("times");
		h_hide = hash64n("hide");
		h_print = hash64n("print");
		h_powered = hash64n("powered");
		h_provided = hash64n("provided");
		h_admission = hash64n("admission");
		h_by = hash64n("by");
		h_com = hash64n("com");
		h_org = hash64n("org");
		h_net = hash64n("net");
		h_pg  = hash64n("pg");
		h_pg13  = hash64n("pg13");

		h_a = hash64n("a");
		h_use = hash64n("use");
		h_search = hash64n("search");
		h_find = hash64n("find");
		h_school = hash64n("school");
		h_shop = hash64n("shop");
		h_gift = hash64n("gift");
		h_gallery = hash64n("gallery");
		h_library = hash64n("library");
		h_photo = hash64n("photo");
		h_image = hash64n("image");
		h_picture = hash64n("picture");
		h_video = hash64n("video");
		h_media = hash64n("media");
		h_copyright = hash64n("copyright");
		h_review = hash64n("review");
		h_join = hash64n("join");
		h_request = hash64n("request");
		h_promote = hash64n("promote");
		h_open = hash64n("open");
		h_house = hash64n("house");
		h_million = hash64n("million");
		h_billion = hash64n("billion");
		h_thousand = hash64n("thousand");
	}

	// . title fields!
	// . if entire previous sentence matches this you are[not] a title
	// . uses + to mean is a title, - to mean is NOT a title following
	// . use '$' to indicate, "ends in that" (substring match)
	// . or if previous sentence ends in something like this, it is
	//   the same as saying that next sentence begins with this, that
	//   should fix "Presented By Colorado Symphony Orchestra" from
	//   being a good event title
	static char *s_titleFields [] = {

		// . these have no '$' so they are exact/full matches
		// . table column headers MUST match full matches and are not
		//   allowed to match substring matches
		"+who",
		"+what",
		"+event",
		"+title",

		// use '$' to endicate sentence ENDS in this
		"-genre",
		"-type",
		"-category",
		"-categories",
		"@where",
		"@location",
		"-contact", // contact: john
		"-neighborhood", // yelp uses this field
		"@venue",
		"-instructor",
		"-instructors",
		"-advisor",
		"-advisors",
		"-leader",
		"-leaders",
		"-chair",
		"-chairman",
		"-chairperson",
		"-designer",
		"-designers",
		"-convenor", // graypanthers uses convenor:
		"-convenors", // graypanthers uses convenor:
		"-caller", // square dancing
		"-callers",
		"-call", // phone number
		"-price",
		"-price range",
		"@event location",

		// put colon after to indicate we need a colon after!
		// or this can be in a column header.
		// try to fix "business categories: " for switchboard.com
		//"-$categories:"

		// use '$' to endicate sentence ENDS in this
		"+$presents",
		"+$present",
		"+$featuring",

		// use '$' to endicate sentence ENDS in this
		"-$presented by",
		"-$brought to you by",
		"-$sponsored by",
		"-$hosted by" 
	};
	// store these words into table
	static HashTableX s_ti1;
	static HashTableX s_ti2;
	static char s_tibuf1[2000];
	static char s_tibuf2[2000];
	static bool s_init6 = false;
	if ( ! s_init6 ) {
		s_init6 = true;
		s_ti1.set(8,4,128,s_tibuf1,2000,false,m_niceness,"ti1tab");
		s_ti2.set(8,4,128,s_tibuf2,2000,false,m_niceness,"ti2tab");
		int32_t n = (int32_t)sizeof(s_titleFields)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_titleFields[i];
			Words w; w.set3 ( s );
			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// . store hash of all words, value is ptr to it
			// . put all exact matches into ti1 and the substring
			//   matches into ti2
			if ( s[1] != '$' )
				s_ti1.addKey ( &h , &s );
			else
				s_ti2.addKey ( &h , &s );
		}
	}

	// store these words into table
	if ( ! s_init3 ) initGenericTable ( m_niceness );

	//
	// phrase exceptions to the ignore words
	//
	static char *s_exceptPhrases [] = {
		// this title was used by exploratorium on a zvents page
		"free day",
		"concert band", // the abq concert band
		"band concert",
		"the voice", // "the voice of yes"
		"voice of", // maybe just eliminate voice by itself...?
		"voice for"
	};
	static HashTableX s_ext;
	static char s_ebuf[10000];
	static bool s_init4 = false;
	if ( ! s_init4 ) {
		s_init4 = true;
		s_ext.set(8,0,512,s_ebuf,10000,false,m_niceness,"excp-tab");
		int32_t n = (int32_t)sizeof(s_exceptPhrases)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			Words w; w.set3 ( s_exceptPhrases[i] );
			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// store hash of all words
			s_ext.addKey ( &h );
		}
	}
	// int16_tcut
	Sections *ss = this;

	// init table
	HashTableX cht;
	char chtbuf[10000];
	cht.set(8,4,512,chtbuf,10000,false,m_niceness,"event-chash");
	// hash the content hash of each section and penalize the title
	// score if it is a repeat on this page
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no text
		//if ( ! si->m_contentHash ) continue;
		// get content hash
		int64_t ch64 = si->m_contentHash64;
		// fix for sentences
		if ( ch64 == 0 ) ch64 = si->m_sentenceContentHash64;
		// if not there in either one, skip it
		if ( ! ch64 ) continue;
		// combine the tag hash with the content hash #2 because
		// a lot of times it is repeated in like a different tag like
		// the title tag
		int64_t modified = si->m_tagHash ^ ch64;
		// store it. return false with g_errno set on error
		if ( ! cht.addTerm ( &modified ) ) return false;
	}

	// for checking if title contains phone #
	//HashTableX *pt = m_dates->getPhoneTable   ();		
	m_dates->setPhoneXors();
	// int16_tcut
	wbit_t *bits = m_bits->m_bits;

	bool afterColon = false;
	Section *lastsi = NULL;
	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);

		// if tag breaks turn this off
		if ( afterColon &&
		     si->m_tagId &&  
		     isBreakingTagId ( si->m_tagId ) )
			afterColon = false;

		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		//if ( si->m_minEventId <= 0 ) continue;

		// if after a colon add that
		if ( afterColon )
			si->m_sentFlags |= SENT_AFTER_COLON;

		// now with our new logic in Sections::addSentence() [a,b) may
		// actually not be completely contained in "si". this fixes
		// such sentences in aliconference.com and abqtango.com
		int32_t senta = si->m_senta;
		int32_t sentb = si->m_sentb;

		////////////
		//
		// a byline for a quote? 
		//
		// fixes "a great pianist" -New York Times so we do not
		// get "New York Times" as the title for terrence-wilson
		//
		///////////
		char needChar = '-';
		bool over     = false;
		// word # senta must be alnum!
		if ( ! m_wids[senta] ) { char *xx=NULL;*xx=0; }
		// start our backwards scan right before the first word
		for ( int32_t i = senta - 1; i >= 0 ; i-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// need punct
			if ( m_wids[i] ) {
				// no words allowed between hyphen and quote
				//if ( needChar == '-' ) continue;
				// otherwise if we had the hyphen and need
				// the quote, we can't have a word pop up here
				over = true;
				break;
			}
			if ( m_tids[i] ) continue;
			// got punct now
			char *pstart = m_wptrs[i];
			char *p      = m_wptrs[i] + m_wlens[i] - 1;
			for ( ; p >= pstart ; p-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				if ( is_wspace_a(*p) )
					continue;
				if ( *p != needChar ) { 
					over = true;
					break;
				}
				if ( needChar == '-' ) {
					needChar = '\"';
					continue;
				}
				// otherwise, we got it!
				si->m_sentFlags |= SENT_IS_BYLINE;
				over = true;
				break;
			}
			// stop if not byline or we got byline
			if ( over ) break;
		}
			

		// Tags:
		if ( sentb - senta == 1 && m_wids[senta] == h_tags )
			si->m_sentFlags |= SENT_TAG_INDICATOR;

		bool tagInd = false;
		if ( lastsi &&  (lastsi->m_sentFlags & SENT_TAG_INDICATOR) )
			tagInd = true;
		// tables: but if he was in different table row, forget it
		if ( tagInd && lastsi->m_rowNum != si->m_rowNum )
			tagInd = false;
		// tables: or same row, but table is not <= 2 columns
		if ( lastsi && (lastsi->m_flags & SEC_TABLE_HEADER) )
			tagInd = false;
		// if in a table, is our header a tags indicator?
		Section *hdr = si->m_headColSection;
		// must have table header set
		if ( ! hdr ) hdr = si->m_headRowSection;
		// check it out
		if ( hdr && 
		     hdr->m_nextSent &&
		     hdr->m_nextSent->m_a < hdr->m_b &&
		     (hdr->m_nextSent->m_sentFlags & SENT_TAG_INDICATOR) )
			tagInd = true;
		// ok, it was a tag indicator before, so we must be tags
		if ( tagInd )
			si->m_sentFlags |= SENT_TAGS;

		///////////////
		//
		// set D_IS_IN_PARENS, D_CRUFTY
		//
		///////////////

		// sometimes title starts with a word and the ( or [
		// is before that word! so back up one word to capture it
		int32_t a = senta;
		// if prev word is punct back up
		if ( a-1>=0 && ! m_wids[a-1] && ! m_tids[a-1] ) a--;
		// backup over prev fron tag
		if ( a-1>=0 &&   m_tids[a-1] && !(m_tids[a-1]&BACKBIT) ) a--;
		// and punct
		if ( a-1>=0 && ! m_wids[a-1] && ! m_tids[a-1] ) a--;
		// init our flags
		//int32_t nonGenerics = 0;
		bool inParens = false;
		bool inSquare = false;

		for ( int32_t j = a ; j < sentb ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip non alnm wor
			if ( ! m_wids[j] ) {
				// skip tags
				if ( m_tids[j] ) continue;
				// count stuff in ()'s or []'s as generic
				if ( m_words->hasChar(j,')') ) inParens =false;
				if ( m_words->hasChar(j,'(') ) inParens =true;
				if ( m_words->hasChar(j,']') ) inSquare =false;
				if ( m_words->hasChar(j,'[') ) inSquare =true;
				continue;
			}
			// generic if in ()'s or []'s
			if ( inParens || inSquare ) {
				bits[j] |= D_IN_PARENS;//GENERIC_WORD;
				continue;
			}
			// skip if in date
			if ( bits[j] & D_IS_IN_DATE ) {
				// was this right?
				//bits[j] |= D_IN_PARENS;//GENERIC_WORD;
				continue;
			}
			// skip doors or show, etc.
			if ( s_igt.isInTable(&m_wids[j]) ) {
				// see if phrase is an exception
				int64_t h = m_wids[j];
				int32_t wcount = 1;
				bool hadException = false;
				for ( int32_t k = j + 1 ; k < sentb ; k++ ) {
					// breathe
					QUICKPOLL(m_niceness);
					// skip if not word
					if ( ! m_wids[k] ) continue;
					// hash it in otherwise
					h ^= m_wids[k];
					// check exceptions
					if ( s_ext.isInTable(&h) ) {
						hadException = true;
						break;
					}
					// stop after 3 words
					if ( ++wcount >= 3 ) break;
				}
				if ( ! hadException ) { 
					bits[j] |= D_CRUFTY;
					continue;
				}
			}
			// numbers are generic (but not if contains an alpha)
			if ( m_words->isNum(j) ) {
				bits[j] |= D_IS_NUM;
				continue;
			}
			// hex num?
			if ( m_words->isHexNum(j) ) {
				bits[j] |= D_IS_HEX_NUM;
				continue;
			}
		}

		//int32_t upperCount = 0;
		int32_t alphas = 0;
		bool lastStop = false;
		bool inDate = true;
		bool inAddress = false;
		bool notInAddress = false;
		bool inAddressName = false;
		int32_t stops = 0;
		inParens = false;
		int32_t dollarCount = 0;
		int32_t priceWordCount = 0;
		bool hadAt = false;

		// punish for huge # of words
		//if ( si->m_alnumPosB - si->m_alnumPosA + 1 >= 15 ) {
		//	tflags |= SENT_TOO_MANY_WORDS;
		//}

		// watchout if in a table. the last table column header
		// should not be applied to the first table cell in the
		// next row! was screwing up
		// www.carnegieconcerts.com/eventperformances.asp?evt=54
		if ( si->m_tableSec &&
		     lastsi &&
		     lastsi->m_tableSec == si->m_tableSec &&
		     lastsi->m_rowNum != si->m_rowNum &&
		     lastsi->m_colNum > 1 )
			lastsi = NULL;

		////////////////////
		//
		// if prev sentence had a title field set, adjust us!
		//
		////////////////////
		if ( lastsi && (lastsi->m_sentFlags & SENT_NON_TITLE_FIELD))
			si->m_sentFlags |= SENT_INNONTITLEFIELD;
		if ( lastsi && (lastsi->m_sentFlags & SENT_TITLE_FIELD) )
			si->m_sentFlags |= SENT_INTITLEFIELD;
		// we are the new lastsi now
		lastsi = si;



		//////////////////
		//
		// set SENT_NON_TITLE_FIELD
		//
		// - any sentence immediately following us will get its
		//   title score reduced and SENT_INNONTITLEFIELD flag set
		//   if we set this SENT_INNONTITLEFIELD for this sentence
		//
		////////////////
		int64_t h = 0;
		int32_t wcount = 0;
		// scan BACKWARDS
		for ( int32_t i = sentb - 1 ; i >= senta ; i-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// must have word after at least
			if ( i + 2 >= m_nw ) continue;
			// set it first time?
			if ( h == 0 ) h  = m_wids[i];
			else          h ^= m_wids[i];
			// max word count
			if ( ++wcount >= 5 ) break;
			// get from table
			char **msp1 = (char **)s_ti1.getValue(&h);
			char **msp2 = (char **)s_ti2.getValue(&h);
			// cancel out msp1 (exact match) if we do not have
			// the full sentence hashed yet
			if ( i != senta ) msp1 = NULL;
			// if we are doing a substring match we must be
			// filled with generic words! otherwise we get
			// "...permission from the Yoga Instructor."
			// becoming a non-title field and hurting the next
			// sentence's capaiblity of being a title.
			//if ( !(si->m_sentFlags & SENT_GENERIC_WORDS) )
			//msp2 = NULL;
			// if not in table,s kip
			if ( ! msp1 && ! msp2 ) continue;
			char *s = NULL;
			// use exact match first if we got it
			if ( msp1 ) s = *msp1;
			// otherwise, use the substring match
			else        s = *msp2;

			// Fix: "Sort by: Date | Title | Photo" so Title
			// is not a title field in this case. scan for
			// | after the word.
			int32_t pcount = 0;
			bool hadPipeAfter = false;
			for ( int32_t j = i + 1 ; j < m_nw ; j++ ) {
				QUICKPOLL(m_niceness);
				if ( m_tids[j] ) continue;
				if ( m_wids[j] ) break;
				char *p    = m_wptrs[j];
				char *pend = p + m_wlens[j];
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( *p == '|' ) break;
				}
				if ( p < pend ) {
					hadPipeAfter = true;
					break;
				}
				// scan no more than two punct words
				if ( ++pcount >= 2 ) break;
			}
			// if we hit the '|' then forget it!
			if ( hadPipeAfter ) continue;
			// we matched then
			if ( s[0] == '+' )
				si->m_sentFlags |= SENT_TITLE_FIELD;
			else
				si->m_sentFlags |= SENT_NON_TITLE_FIELD;
			// @Location
			//if ( s[0] == '@' )
			//	si->m_sentFlags |= SENT_PLACE_FIELD;
			break;
		}

		//////////////////////////
		//
		// USE TABLE HEADERS AS INDICATORS
		//
		//
		// . repeat that same logic but for the table column header
		// . if we are in a table and table header is "title"
		// . for http://events.mapchannels.com/Index.aspx?venue=628
		// . why isn't this kicking in for psfs.org which has
		//   "location" in the table column header
		// . we set "m_headColSection" to NULL if not a header per se
		// . a table can only have a header row or header column

		hdr = si->m_headColSection;
		// must have table header set
		if ( ! hdr ) hdr = si->m_headRowSection;

		// ok, set to it
		int32_t csentb = 0;
		int32_t csenta = 0;
		if ( hdr && hdr->m_firstWordPos >= 0 && 
		     // do not allow the the header row to get its
		     // SENT_PLACE_INDICATED set, etc. it's a field not a value
		     ! hdr->contains ( si ) ) {
			csenta = hdr->m_firstWordPos;
			csentb = hdr->m_lastWordPos+1;
		}
		h = 0;
		wcount = 0;
		// scan BACKWARDS
		for ( int32_t i = csentb - 1 ; i >= csenta ; i-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// set it first time?
			if ( h == 0 ) h  = m_wids[i];
			else          h ^= m_wids[i];
			// max word count
			if ( ++wcount >= 5 ) break;
			// stop if not full yet
			if ( i != csenta ) continue;
			// get from table, only exact match since we are
			// checking column headers
			char **msp1 = (char **)s_ti1.getValue(&h);
			//char **msp2 = (char **)s_ti1.getValue(j);
			// the full sentence hashed yet
			if ( ! msp1 ) continue;
			// use exact match first if we got it
			char *s = *msp1;
			// we matched then
			if ( s[0] == '+' ) {
				si->m_sentFlags |= SENT_INTITLEFIELD;
			}
			else {
				si->m_sentFlags |= SENT_INNONTITLEFIELD;
			}
			// @Location, like for psfs.org has Location in tablcol
			if ( s[0] == '@' )
				si->m_sentFlags |= SENT_INPLACEFIELD;
			break;
		}
		     
		bool hadDollar = false;
		// fix sentences that start with stuff like "$12 ..."
		if ( senta>0 && 
		     ! m_tids[senta-1] &&
		     m_words->hasChar(senta-1,'$') ) {
			dollarCount++;
			hadDollar = true;
		}

		bool hasSpace = false;

		//////////////////
		//
		// SENT_TAGS
		//
		//////////////////
		//
		// . just check for <eventTags> for now
		// . get parent tag
		Section *ps = si->m_parent;
		if ( ps && 
		     (m_isRSSExt || m_contentType == CT_XML) &&
		     m_tids[ps->m_a] == TAG_XMLTAG &&
		     strncasecmp(m_wptrs[ps->m_a],"<eventTags>",11)==0 )
			si->m_sentFlags |= SENT_TAGS;

		/////////////////
		//
		// SENT_INTITLEFIELD
		//
		/////////////////
		if ( ps && 
		     (m_isRSSExt || m_contentType == CT_XML) &&
		     m_tids[ps->m_a] == TAG_XMLTAG &&
		     strncasecmp(m_wptrs[ps->m_a],"<eventTitle>",12)==0 )
			si->m_sentFlags |= SENT_INTITLEFIELD;
		// stubhub.com feed support
		if ( ps && 
		     (m_isRSSExt || m_contentType == CT_XML) &&
		     m_tids[ps->m_a] == TAG_XMLTAG &&
		     strncasecmp(m_wptrs[ps->m_a],
				 "<str name=\"act_primary\">",24)==0 )
			si->m_sentFlags |= SENT_INTITLEFIELD;


		///////////////////
		//
		// SENT_STRANGE_PUNCT etc.
		//
		///////////////////
		int64_t lastWid = 0LL;
		for ( int32_t i = senta ; i < sentb ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) {
				// skip tags right away
				if ( m_tids[i] ) continue;
				// check for dollar sign and give slight
				// demotion so we can avoid ticket price
				// titles
				//if ( m_words->hasChar(i,'$') )
				//	dollarCount++;
				// equal sign is strange. will fix
				// SSID=Truman Library for cabq.gov
				//if ( m_words->hasChar(i,'=' ) )
				//	si->m_sentFlags |= SENT_STRANGE_PUNCT;
				// blackbird buvetter has '*''s in menu, so
				// prevent those from being title.
				//if ( m_words->hasChar(i,'*' ) )
				//	si->m_sentFlags |= SENT_STRANGE_PUNCT;
				// Unnamed Server [68%] for piratecatradio
				//if ( m_words->hasChar(i,'%' ) )
				//	si->m_sentFlags |= SENT_STRANGE_PUNCT;
				// back to back _ might indicate form field
				char *p       = m_wptrs[i];
				char *pend    = p + m_wlens[i];
				bool  strange = false;
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( *p == '$' ) { 
						hadDollar = true;
						dollarCount++;
					}
					if ( *p == '=' ) strange = true;
					if ( *p == '*' ) strange = true;
					if ( *p == '%' ) strange = true;
					if ( is_wspace_a(*p) ) hasSpace = true;
					// following meats: ____chicken ___pork
					if ( *p == '_' && p[1] == '_' )
						strange = true;
					// need one alnum b4 parens check
					if ( alphas == 0 ) continue;
					if ( *p == '(' ) inParens = true;
					if ( *p == ')' ) inParens = false;
				}
				if ( strange )
					si->m_sentFlags |= SENT_STRANGE_PUNCT;
				// need at least one alnum before parens check
				if ( alphas == 0 ) continue;
				// has colon
				if ( m_words->hasChar(i,':') &&
				     i>senta && 
				     ! is_digit(m_wptrs[i][-1]) )
					si->m_sentFlags |= SENT_HAS_COLON;
				// check for end first in case of ") ("
				//if ( m_words->hasChar(i,')') )
				//	inParens = false;
				// check if in parens
				//if ( m_words->hasChar(i,'(') )
				//	inParens = true;
				continue;
			}

			//
			// check for a sane PRICE after the dollar sign.
			// we do not want large numbers after it, like for
			// budgets or whatever. those are not tickets!!
			//
			if ( hadDollar ) {
				// require a number after the dollar sign
				if ( ! is_digit(m_wptrs[i][0]) ) 
					dollarCount = 0;
				// number can't be too big!
				char *p = m_wptrs[i];
				int32_t digits =0;
				char *pmax = p + 30;
				bool hadPeriod = false;
				for ( ; *p && p < pmax ; p++ ) {
					if ( *p == ',' ) continue;
					if ( *p == '.' ) {
						hadPeriod = true;
						continue;
					}
					if ( is_wspace_a(*p) ) continue;
					if ( is_digit(*p) ) {
						if ( ! hadPeriod ) digits++;
						continue;
					}
					// $20M? $20B?
					if ( to_lower_a(*p) == 'm' )
						dollarCount = 0;
					if ( to_lower_a(*p) == 'b' ) 
						dollarCount = 0;
					break;
				}
				if ( digits >= 4 ) 
					dollarCount = 0;
				// if word after the digits is million
				// thousand billion, etc. then nuke it
				int32_t n = i + 1;
				int32_t nmax = i + 10;
				if ( nmax > m_nw ) nmax = m_nw;
				for ( ; n < nmax ; n++ ) {
					if ( ! m_wids[n] ) continue;
					if ( m_wids[n] == h_million )
						dollarCount = 0;
					if ( m_wids[n] == h_billion )
						dollarCount = 0;
					if ( m_wids[n] == h_thousand )
						dollarCount = 0;
					break;
				}
				// reset for next one
				hadDollar = false;
			} 

			int64_t savedWid = lastWid;
			lastWid = m_wids[i];

			// skip if not ours directly
			//if ( sp[i] != si ) continue;
			// in address?
			if ( bits[i] & D_IS_IN_VERIFIED_ADDRESS_NAME )
				inAddressName = true;
			else if ( bits[i] & D_IS_IN_UNVERIFIED_ADDRESS_NAME)
				inAddressName = true;
			else if ( bits[i] & D_IS_IN_ADDRESS ) 
				inAddress = true;
			else    
				notInAddress = true;

			// . skip if in parens
			// . but allow place name #1's in parens like
			//   (Albuquerque Rescue Mission) is for unm.edu
			if ( inParens ) continue;

			// . in date?
			// . hurts "4-5pm Drumming for Dancers w/ Heidi" so
			//   make sure all are in date!
			if ( ! ( bits[i] & D_IS_IN_DATE ) ) inDate = false;
			// "provided/powered by"
			if ( ( m_wids[i] == h_provided ||
			       m_wids[i] == h_powered ) &&
			     i + 2 < sentb &&
			     m_wids[i+2] == h_by )
				si->m_sentFlags |= SENT_POWERED_BY;
			// pricey? "free admission" is a price... be sure
			// to include in the description!
			if ( m_wids[i] == h_admission && savedWid == h_free )
				si->m_sentFlags |= SENT_HAS_PRICE;
			// count alphas
			if ( ! is_digit(m_wptrs[i][0]) ) alphas++;
			// "B-52" as in the band (exclude phone #'s!)
			else if ( i-2>0 && 
				  m_wptrs[i][-1] =='-' &&
				  !is_digit(m_wptrs[i-2][0] ) )
				  alphas++;
			// "B-52s", 52s has a letter in it!
			else if ( ! m_words->isNum(i) ) alphas++;
			// are we a stop word?
			bool isStopWord = m_words->isStopWord(i);
			// .com is stop word
			if ( m_wids[i] == h_com ||
			     m_wids[i] == h_org ||
			     m_wids[i] == h_net ||
			     // fixes mr. movie times "PG-13"
			     m_wids[i] == h_pg  ||
			     m_wids[i] == h_pg13 )
				isStopWord = true;
			// count them
			if ( isStopWord ) stops++;
			// set this
			if ( m_wids[i] == h_at ) hadAt = true;
			// if we end on a stop word that is usually indicative
			// of something like
			// "Search Results for <h1>Doughnuts</h1>" as for
			// switchborad.com url
			if ( m_wids[i] == h_of ||
			     m_wids[i] == h_the ||
			     m_wids[i] == h_and ||
			     m_wids[i] == h_at  ||
			     m_wids[i] == h_to  ||
			     m_wids[i] == h_be  ||
			     m_wids[i] == h_or  ||
			     m_wids[i] == h_not ||
			     m_wids[i] == h_in  ||
			     m_wids[i] == h_by  ||
			     m_wids[i] == h_on  ||
			     m_wids[i] == h_for ||
			     m_wids[i] == h_with||
			     m_wids[i] == h_from )
				lastStop = true;
			else
				lastStop = false;
			// ticket pricey words
			if ( m_wids[i] == h_adv ||
			     m_wids[i] == h_dos ||
			     m_wids[i] == h_advance ||
			     m_wids[i] == h_day ||
			     m_wids[i] == h_of ||
			     m_wids[i] == h_show ||
			     m_wids[i] == h_box ||
			     m_wids[i] == h_office )
				priceWordCount++;
		}

		// set this
		if ( ! hasSpace ) 
			si->m_sentFlags |= SENT_HASNOSPACE;

		/*
		// if all words one in address, penalize
		if ( inAddress && ! notInAddress ) {
			//tscore *= .76;
			//dscore *= .76;
			// since it is a street or city, nuke it harder, 
			// harder than a mixed case sentence really... which
			// is .15.
			// now http://www.cabq.gov/library/branches.html is
			// getting the street addresses as titles because
			// the sub header is nuked so hard from MULT_EVENTS
			// so nuke this even harder
			// generic words
			//tscore *= .001;//.12;
			//dscore *= .001;//.12;
			// if generic that means we are not store hours
			// and this is something we do not want in the title
			if ( tflags & SENT_GENERIC_WORDS )
			     tscore *= .99;
			// a slight hit now, no hurts, blackbird
			//tscore *= .99;
			tflags |= SENT_IN_ADDRESS;
		}

		// . if some words in address, penalize less
		// . fixes newmexico.org urls which have
		//   "45th Annual Rockhound Roundup - Gem & Mineral Show - 
		//    Deming, NM - 09:00 AM" where Deming, NM is in address
		//    but the rest are not. it caused us to prefer the
		//    section "Thursday, 11 March, 2010" as the title and
		//    we wound up not getting an outlinked title bit set
		//    (EV_OUTLINKED_TITLE)
		if ( inAddress && notInAddress ) {
			// hurt salsapower.com title from
			// "Intermediate Salsa class on Saturdays at 11 a.m. 
			//  at the Harwood 1114 7th Street NW ts=50.0 ds=50.0 
			//  inaddress" to "Alb, NM" with weight of .50, so i
			// changed to no penalty.
			// BUT without a penalty we get event titles like
			// "Albuquerque, NM 87104-1133" from trumba that
			// have extended zip codes not recognized as being
			// 100% in an address. and we get
			// "P.O.B. 4321, zip 87196" from unm.edu that are
			// quite address-y but not 100% recognized as all
			// address words by us!
			// . BUT we are missing some nice titles for 
			//   abqtango.com like "Dance to Live Tango Music, 
			//   Fridays, 10:00pm-Midnight at the Roasted Bean 
			//   Cafe", so let's only penalize if there is no
			//   "at" in the title
			if ( ! hadAt ) {
				// generic words
				//tscore *= .90;
				//dscore *= .90;
				// slightly - no, hurts blackbird
				//tscore *= .99;
			}
			tflags |= SENT_IN_ADDRESS;
		}
		*/

		// punish just a tiny amount to fix 
		// http://www.when.com/albuquerque-nm/venues which is using
		// the place name as the title and not the real event title
		// which is actually AFTER the event date. BUT the problem
		// is is that good titles are often mislabeled as being
		// in an address name! so we can't really punish this at all
		// because we will lose good titles... unless it is a 
		// verified place name i guess... so i changed the algo
		// in Address.cpp to only set D_IS_IN_VERIFIED_ADDRESS_NAME if
		// the place name is verified, not just inlined...
		if ( inAddressName && ! notInAddress )
			si->m_sentFlags |= SENT_PLACE_NAME;

		// does it contain a place name?
		if ( inAddressName )
			si->m_sentFlags |= SENT_CONTAINS_PLACE_NAME;

		// try to avoid mutiple-ticket- price titles
		if ( dollarCount >= 2 )
			si->m_sentFlags |= SENT_PRICEY;
		// . if all words in section are describing ticket price...
		// . fix bad titles for southgatehouse.com because right title
		//   is in SEC_MENU and perchance is also repeated on the page
		//   so its score gets slammed and it doesn't even make it in
		//   the event description. instead the title we pick is this
		//   ticket pricey title, so let's penalize that here so the
		//   right title comes through
		// . pricey title was "$17 ADV / $20 DOS"
		// . title in SEC_MENU is now "Ricky Nye". we should have
		//   "Buckwheat zydeco" too, but i guess it didn't make the
		//   cut, but since we set EV_OUTLINKED_TITLE anyway, it 
		//   doesn't matter for now.
		if ( dollarCount >= 1 && alphas == priceWordCount )
			si->m_sentFlags |= SENT_PRICEY;
		// single dollar sign?
		if ( dollarCount >= 1 )
			si->m_sentFlags |= SENT_HAS_PRICE;

		// . if ALL words in date, penalize
		// . penalize by .50 to fix events.mapchannels.com
		// . nuke even harder because www.zvents.com/albuquerque-nm/events/show/88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer
		//   was using the date of the events as the title rather than
		//   resorting to "Other Future Dates & Times" header
		//   which was penalized down to 4.7 because of MULT_EVENTS
		if ( inDate ) {
			// generic words
			//tscore *= .001;
			//dscore *= .001;
			// punish just a little so that if two titles are
			// basically the same but one has a date, use 
			// the one without the date "single's day soiree"
			// vs. "single's day soiree Monday Feb 14th..."
			//tscore *= .99;
			si->m_sentFlags |= SENT_IS_DATE;
		}
		// hurts "Jay-Z" but was helping "Results 1-10 of"
		if ( lastStop )
			si->m_sentFlags |= SENT_LAST_STOP;

		// check to see if text is a "city, state"
		// or has a city in previous section and state in this
		if ( m_aa->isCityState ( si ) )
			si->m_sentFlags |= SENT_CITY_STATE;
		
		// punish if only wnumbers (excluding stop words)
		if ( alphas - stops == 0 )
			si->m_sentFlags |= SENT_NUMBERS_ONLY;

		// do we contain a phone number?
		//if ( pt->isInTable ( &si ) )
		if ( si->m_phoneXor )
			si->m_sentFlags |= SENT_HAS_PHONE;

		//
		// end case check
		//

		char *p = NULL;
		int32_t lastPunct = si->m_sentb;
		// skip over tags to fix nonamejustfriends.com sentence
		for ( ; lastPunct < m_nw && m_tids[lastPunct] ; lastPunct++);
		// now assume, possibly incorrectly, that it is punct
		if ( lastPunct < m_nw ) p = m_wptrs[lastPunct];


		if ( p && (p[0]==':' || p[1]==':' ) ) {
		     // commas also need to fix lacma.org?
		     //!(si->m_sentFlags & SENT_HAS_COMMA) &&
		     // . only set this if we are likely a field name
		     // . fix "Members of the Capitol Ensemble ...
		     //   perform Schubert: <i>String Trio in B-flat..."
		     //   for lacma.org
		     //(si->m_alnumPosB - si->m_alnumPosA) <= 5 ) {
			si->m_sentFlags |= SENT_COLON_ENDS;
			afterColon = true;
		}
		// starts with '(' or '[' is strange!
		if ( senta>0 && 
		     ( m_wptrs[senta][-1] == '(' ||
		       m_wptrs[senta][-1] == '[' ) )
			si->m_sentFlags |= SENT_PARENS_START; // STRANGE_PUNCT;

		// . if one lower case and most are upper, we can ignore
		// . fixes mistakes
		// . fixes "Welcome to the Academy store" for wholefoods.com
		//if ( lowerCount == 1 && upperCount >= 2 ) lowerCount = 0;
		// punish if not title case
		//if ( lowerCount )
		//	si->m_sentFlags |= SENT_MIXED_CASE;

		// if in a tag of its own that's great! like being in a header
		// tag kind of
		Section *sp = si->m_parent;
		// skip paragraph tags
		//if ( sp && sp->m_tagId == TAG_P ) sp = sp->m_parent;

		if ( sp &&
		     //sp->m_tagId != TAG_P &&
		     sp->m_firstWordPos == si->m_firstWordPos &&
		     sp->m_lastWordPos  == si->m_lastWordPos ) 
			si->m_sentFlags |= SENT_IN_TAG;

		// this is obvious
		if ( si->m_sentFlags & SENT_IN_HEADER )
			si->m_sentFlags |= SENT_IN_TAG;

		// if sent to left of us starts the parent tag and a colon
		// separates us, then set both our SENT_IN_TAG bits.
		// fixes newmexico.org where event title is "Looking Ahead: 
		// Portraits from the Mott-Warsh Collection, Las Cruces"
		/*
		int32_t cc = si->m_a - 1;
		Section *leftSent = NULL;
		if ( cc - 1 >= 0 ) leftSent = ss->m_sectionPtrs [ cc - 1 ];
		if ( leftSent && 
		     (leftSent->m_flags & SEC_SENTENCE) &&
		     m_words->hasChar(cc,':') &&
		     sp->m_firstWordPos == leftSent->m_firstWordPos &&
		     sp->m_lastWordPos  == si->m_lastWordPos ) {
			si->m_sentFlags       |= SENT_IN_TAG;
			leftSent->m_sentFlags |= SENT_IN_TAG;
		}
		*/
	}


	// are we a trumba.com url? we trust the event titles for those
	//char *dom  = m_url->getDomain();
	//int32_t  dlen = m_url->getDomainLen();
	//bool  isTrumba = false;
	//bool  isFacebook = false;
	//if ( dlen == 10 && strncmp ( dom , "trumba.com" , 10 ) == 0 )
	//	isTrumba = true;
	//if ( dlen == 12 && strncmp ( dom , "facebook.com" , 12 ) == 0 )
	//	isFacebook = true;
		
	//bool lastSentenceHadColon = false;
	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		//if ( si->m_minEventId <= 0 ) continue;

		// . if we are after a colon 
		// . hurts "Dragonfly Lecture: BIGHORN SHEEP SONGS" for
		//   http://www.kumeyaay.com/2009/11/dragonfly-lecture-bighorn
		//   -sheep-songs/
		// . hurts "Four Centurues: a History of Albuquerque" for
		//   collectorsguide.com
		// . but now i do not penalize if the sentence before the
		//   colon had two or more words... see below
		//if ( lastSentenceHadColon )
		//	si->m_sentFlags |= SENT_AFTER_COLON;

		if ( si->m_flags & SEC_MENU )
			si->m_sentFlags |= SENT_IN_MENU;

		if ( si->m_flags & SEC_MENU_SENTENCE )
			si->m_sentFlags |= SENT_MENU_SENTENCE;

		// why not check menu header too?
		// would fix 'nearby bars' for terrence-wilson zvents url
		if ( si->m_flags & SEC_MENU_HEADER )
			si->m_sentFlags |= SENT_IN_MENU_HEADER;
		

		// assume we do not end in a colon
		//lastSentenceHadColon = false;

		int32_t sa = si->m_senta;
		int32_t sb = si->m_sentb;

		// if breaking tag between "sa" and last word of prev sentence
		// AND now breaking tag between our last word and beginning
		// of next sentence, that's a "word sandwich" and not 
		// conducive to titles... treat format change tags as 
		// breaking tags for this purpose...
		int32_t na = sb;
		int32_t maxna = na + 40;
		if ( maxna > m_nw ) maxna = m_nw;
		bool hadRightTag = true;
		for ( ; na < maxna ; na++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop on tag
			if ( m_tids[na] ) break;
			// or word
			if ( ! m_wids[na] ) continue;
			// heh...
			hadRightTag = false;
			break;
		}
		bool hadLeftTag = true;
		na = sa - 1;
		int32_t minna = na - 40;
		if ( minna < 0 ) minna = 0;
		for ( ; na >= minna ; na-- ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if had a tag on right
			if ( hadRightTag ) break;
			// stop on tag
			if ( m_tids[na] ) break;
			// or word
			if ( ! m_wids[na] ) continue;
			// heh...
			hadLeftTag = false;
			break;
		}
		if ( ! hadRightTag && ! hadLeftTag )
			si->m_sentFlags |= SENT_WORD_SANDWICH;

		// <hr> or <p>...</p> before us with no text?
		Section *pj = si->m_prev;
		// keep backing up until does not contain us, if ever
		for ( ; pj && pj->m_b > sa ; pj = pj->m_prev ) {
			// breathe
			QUICKPOLL(m_niceness);
		}
		// now check it out
		if ( pj && pj->m_firstWordPos < 0 )
			// must be p or hr tag etc.
			si->m_sentFlags |= SENT_AFTER_SPACER;

		// TODO: also set if first sentence in CONTAINER...!!

		

		// likewise, before a spacer tag
		pj = si->m_next;
		// keep backing up until does not contain us, if ever
		for ( ; pj && pj->m_a < sb ; pj = pj->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
		}
		// now check it out
		if ( pj && pj->m_firstWordPos < 0 )
			// must be p or hr tag etc.
			si->m_sentFlags |= SENT_BEFORE_SPACER;


		// location sandwich? punish
		// "2000 Mountain Rd NW, Old Town, Albuquerque, NM 87104"
		// for collectorsguide.com.
		if ( (bits[sa  ] & D_IS_IN_ADDRESS) &&
		     (bits[sb-1] & D_IS_IN_ADDRESS) )
			si->m_sentFlags |= SENT_LOCATION_SANDWICH;



		// . has colon at the end of it?
		// . but do allow "hours:" etc to be a title...
		// . that fixes www.thewoodencow.com's store hours "event"
		/*
		if ( m_words->hasChar(sb-1,':') &&
		     (sb-2<0 || m_wids[sb-2]!=h_hours) ) {
			// just slight penalty, no hurts us!
			// "*Practica*: 9-10pm"
			si->m_sentFlags |= SENT_HAS_COLON;
			lastSentenceHadColon = true;
		}
		else if ( sb<m_nw && m_words->hasChar(sb,':') &&
		     (sb-1<0 || m_wids[sb-1]!=h_hours) ) {
			si->m_sentFlags |= SENT_HAS_COLON;
			lastSentenceHadColon = true;
		}

		// negate if title: etc.
		if ( m_wids[sb-1] == h_title )
			lastSentenceHadColon = false;
		if ( m_wids[sb-1] == h_event )
			lastSentenceHadColon = false;
		// . or if 2+ words in sentence
		// . but still "Doves: For Divorced, Widowed and Separated"
		//   title suffers for http://www.trumba.com/calendars/
		//   KRQE_Calendar.rss
		if ( sb - sa >= 2 )
			lastSentenceHadColon = false;
		// negate if "Saturdays: 5-6:30pm All Levels African w/ ..."
		// to fix texasdrums.drums.org url
		if ( bits[sa] & D_IS_IN_DATE )
			lastSentenceHadColon = false;
		*/

		// . dup slam
		// . make it 2 to 1 to fix trumba.com
		//if ( si->m_votesForDup > 2 * si->m_votesForNotDup )
		//	si->m_sentFlags |= SENT_DUP_SECTION;

		// this crap is set in XmlDoc.cpp
		//if ( si->m_votesForDup >  2 * si->m_votesForNotDup && 
		//     si->m_votesForDup >= 1 &&
		//     ! (si->m_flags & SEC_HAS_NONFUZZYDATE) ) 
		//	si->m_sentFlags |= SENT_DUP_SECTION;

		// . second title slam
		// . need to telescope up for this i think
		Section *sit = si;
		for ( ; sit ; sit = sit->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// test
			if ( ! ( sit->m_flags & SEC_SECOND_TITLE ) ) continue;
			si->m_sentFlags |= SENT_SECOND_TITLE;
			break;
		}

		// are we in a facebook name tag?
		sit = si;
		//if ( ! m_isFacebook ) sit = NULL;
		if ( m_contentType != CT_XML ) sit = NULL;
		for ( ; sit ; sit = sit->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// are we in a facebook <name> tag? that is event title
			if ( m_isFacebook && sit->m_tagId == TAG_FBNAME ) {
				si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
				break;
			}
			// for trumba and eventbrite... etc.
			if ( sit->m_tagId == TAG_GBXMLTITLE ) {
				si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
				break;
			}
			// stop if hit an xml tag. we do not support nested
			// tags for this title algo. 
			if ( sit->m_tagId ) break;
		}

		// . in header tag boost
		// . should beat noprev/nonextbrother combo
		// . fixes meetup.com
		if ( si->m_flags & SEC_IN_HEADER )
			si->m_sentFlags |= SENT_IN_HEADER;

		// lost a title because of SENT_MIXED_TEXT 
		// "Tango Club of Albuquerque (Argentine Tango)". should we
		// split up the sentence when it ends in a parenthetical to 
		// fix that? let's take this out then see it can remove good
		// titles...
		//if ( (si->m_flags & SEC_LINK_TEXT) &&
		//     (si->m_flags & SEC_PLAIN_TEXT) ) {
		//	si->m_sentFlags |= SENT_MIXED_TEXT;
		//}

		// same goes for being in a menu sentence
		//if ( si->m_flags & SEC_MENU_SENTENCE ) {
		//	si->m_sentFlags |= SENT_MIXED_TEXT;
		//}

		// . now fix trumba.com which has <title> tag for each event
		// . if parent section is title tag or has "title" in it
		//   somewhere, give us a boost
		Section *ip = si->m_parent;
		// if counted as header do not re-count as title too
		//if ( si->m_sentFlags & SENT_IN_HEADER ) ip = NULL;
		// ignore <title> tags if we are not an rss feed (trumba fix)
		if ( ip && ip->m_tagId == TAG_TITLE && ! m_isRSSExt) ip = NULL;
		// keep telescoping up as int32_t as parent just contains this
		// sentence, si.
		for ( ; ip ; ip = ip->m_parent ) {
			// parent must only contain us
			if ( ip->m_firstWordPos != si->m_firstWordPos ) break;
			if ( ip->m_lastWordPos  != si->m_lastWordPos  ) break;
			// do not allow urls that could have "title" in them
			if ( ip->m_tagId == TAG_A      ) break;
			if ( ip->m_tagId == TAG_IFRAME ) break;
			if ( ip->m_tagId == TAG_FRAME  ) break;
			// trumba title tag? need for eventbrite too!
			if ( ip->m_tagId == TAG_GBXMLTITLE ) {//&& isTrumba ) {
				si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
				break;
			}
			// get tag limits
			char *ta = m_wptrs[ip->m_a];
			char *tb = m_wlens[ip->m_a] + ta;
			// scan for "title"
			char *ss = gb_strncasestr(ta,tb-ta,"title") ;
			// skip if not there
			if ( ! ss ) break;
			// . stop if has equal sign after
			// . exempts "<div title="blah">" to fix 
			//   reverbnation.com from getting Lyrics as a title
			//   for an event
			if ( ss[5] == '=' ) break;
			// reward
			//tflags |= SENT_IN_TITLEY_TAG;
			// if we are trumba, we trust these 100% so
			// make sure it is the title. but if we
			// have multiple candidates we still want to
			// rank them amongst themselves so just give
			// a huge boost. problem was was that some event
			// items repeated the event date in the brother 
			// section below the address section, thus causing the
			// true event title to get SENT_MULT_EVENTS set while
			// the address place name would be selected as the
			// title for one, because the address section also
			// contained the event time. and the other "event"
			// would use a title from its section.
			//if ( isTrumba ) {
			//	si->m_sentFlags |= SENT_IN_TRUMBA_TITLE;
			//	//tscore = (tscore + 100.0) * 2000.0;
			//	//dscore = (dscore + 100.0) * 2000.0;
			//}
			//else {
			si->m_sentFlags |= SENT_IN_TITLEY_TAG;
			//}
			// once is good enough
			break;
		}

		/*
		// get biggest
		Section *biggest = NULL;
		// loop over this
		Section *pp = si;
		int32_t ca = si->m_firstWordPos;
		int32_t cb = si->m_lastWordPos;
		// blow up until contain prev
		for ( ; pp ; pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if contains prev guy
			if ( pp->m_firstWordPos != ca ) break;
			if ( pp->m_lastWordPos  != cb ) break;
			// otherwise, set it
			biggest = pp;
		}


		if ( biggest && ! biggest->m_prevBrother &&
		     // we already give a bonus for being in header tag above
		     ! (biggest->m_flags & SEC_IN_HEADER) ) {
		     //!(biggest->m_flags & SEC_HEADING_CONTAINER) &&
		     //!(biggest->m_flags & SEC_NIXED_HEADING_CONTAINER) ) {
			tscore *= 1.2;
			tflags |= SENT_NO_PREV_BROTHER;
			// check this if we have no prev brother only
			//if ( ! biggest->m_nextBrother ||
			//     // or we can differ too!
			//     (biggest->m_nextBrother->m_tagHash !=
			//      biggest->m_tagHash ) ) {
			//	tscore *= 1.1;
			//	tflags |= SENT_NO_NEXT_BROTHER;
			//}
		}
		*/

		/*
		Section *bro = NULL;
		if ( biggest ) bro = biggest->m_nextBrother;
		// discard brother if not same tag hash
		if ( bro && bro->m_tagHash != biggest->m_tagHash ) bro = NULL;
		// get smallest section containing first word of bro
		Section *smbro = NULL;
		int32_t fwp = -1;
		if ( bro ) fwp = bro->m_firstWordPos;
		if ( fwp >= 0) smbro = sp[fwp];
		// discard brother if its in lower case issues and we are not
		if ( smbro && 
		     !(biggest->m_sentFlags & SENT_MIXED_CASE) &&
		     (smbro->m_sentFlags & SENT_MIXED_CASE)) bro=NULL;
		// if no next brother, and no prev brother, reward
		if ( ! bro && (tflags & SENT_NO_PREV_BROTHER) ) {
			tscore *= 1.1;
			tflags |= SENT_NO_NEXT_BROTHER;
		}
		*/

		int64_t ch64 = si->m_contentHash64;
		// fix for sentences
		if ( ch64 == 0 ) ch64 = si->m_sentenceContentHash64;
		// must be there
		if ( ! ch64 ) { char *xx=NULL;*xx=0; }
		// combine the tag hash with the content hash #2 because
		// a lot of times it is repeated in like a different tag like
		// the title tag
		int64_t modified = si->m_tagHash ^ ch64;
		// repeat on page?
		// hurts "5:30-7pm Beginning African w/ Romy" which is
		// legit and repeated for different days of the week for
		// texasdrums.drums.org, so ease off a bit
		int32_t chtscore = cht.getScore ( &modified ) ;
		if ( chtscore > 1 ) 
			si->m_sentFlags |= SENT_PAGE_REPEAT;
		/*
		for ( int32_t cc = chtscore ; cc > 1 ; cc-- ) {
			//if ( chtscore == cc ) tscore *= .80;//.40;
			// punish a little more for every repeat so that
			// "HEADLINER" will lose to "That 1 Guy" for 
			// reverbnation.com
			//else                  tscore *= .99;
			si->m_sentFlags |= SENT_PAGE_REPEAT;
		}
		*/
		// advance to first word
		//int32_t f = a; for ( ; ! m_wids[f] && f < b ; f++ );
		int32_t f = si->m_senta;//si->m_firstWordPos;
		int32_t L = si->m_sentb;//si->m_lastWordPos;
		if ( f < 0 ) { char *xx=NULL;*xx=0; }
		if ( L < 0 ) { char *xx=NULL;*xx=0; }
		// single word?
		bool single = (f == (L-1));
		/*
		// skip f forward until it is text we contain directly!
		for ( ; f < m_nw ; f++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if we contain directly
			if ( sp[f] == si ) break;
		}
		*/

		// slight penalty if first word is action word or another
		// word not very indicative of a title, and more indicative
		// of a menu element
		bool badFirst = false;
		bool skip = false;
		if ( m_wids[f] == h_send  ) badFirst = true;
		if ( m_wids[f] == h_save  ) badFirst = true;
		if ( m_wids[f] == h_add   ) badFirst = true;
		if ( m_wids[f] == h_share ) badFirst = true;
		if ( m_wids[f] == h_join  ) badFirst = true;
		// request a visit from jim hammond: booktour.com
		if ( m_wids[f] == h_request ) badFirst = true;
		if ( m_wids[f] == h_contact ) badFirst = true;
		// "promote your event" : booktour.com
		if ( m_wids[f] == h_promote  ) badFirst = true;
		if ( m_wids[f] == h_subscribe ) badFirst = true;
		if ( m_wids[f] == h_loading   ) badFirst = true;
		// "last modified|updated"
		if ( m_wids[f] == h_last && f+2 < m_nw &&
		     ( m_wids[f+2] == h_modified || m_wids[f+2]==h_updated)) 
			badFirst = true;
		// special guest
		if ( m_wids[f] == h_special && f+2 < m_nw &&
		     ( m_wids[f+2] == h_guest || m_wids[f+2]==h_guests)) 
			badFirst = true;
		// directed by ... darren dunbar (santafeplayhouse.org)
		if ( m_wids[f] == h_directed && f+2 < m_nw &&
		     m_wids[f+2] == h_by ) 
			badFirst = true;
		// map of
		if ( m_wids[f] == h_map && f+2 < m_nw &&
		     m_wids[f+2] == h_of ) 
			badFirst = true;
		// "more|other|venue information|info"
		if ( ( m_wids[f] == h_more  || 
		       m_wids[f] == h_venue || 
		       m_wids[f] == h_general || 
		       m_wids[f] == h_event || 
		       m_wids[f] == h_other ) && 
		     f+2 < m_nw &&
		     ( m_wids[f+2] == h_information || m_wids[f+2]==h_info)) 
			badFirst = true;
		// phone number field
		if ( m_wids[f] == h_phone ) badFirst = true;
		// part of address, but we do not pick it up
		if ( m_wids[f] == h_usa ) badFirst = true;
		if ( m_wids[f] == h_date ) badFirst = true;
		if ( m_wids[f] == h_description ) badFirst = true;
		// "buy tickets from $54" for events.mapchannels.com!
		if ( m_wids[f] == h_buy ) badFirst = true;
		if ( m_wids[f] == h_where ) badFirst = true;
		if ( m_wids[f] == h_location ) badFirst = true;
		if ( m_wids[f] == h_located ) badFirst = true;
		if ( m_wids[f] == h_click ) badFirst = true;
		if ( m_wids[f] == h_here ) badFirst = true;
		// "back to band profile" myspace.com
		if ( m_wids[f] == h_back && 
		     f+2 < m_nw && m_wids[f+2] == h_to )
			badFirst = true;
		// southgatehouse.com "this week"
		if ( m_wids[f] == h_this && 
		     f+2 < m_nw && m_wids[f+2] == h_week )
			skip = true;
		// "this event repeats 48 times" for 
		// http://www.zipscene.com/events/view/2848438-2-75-u-call-
		// its-with-dj-johnny-b-cincinnati was getting hammered by
		// this algo
		if ( m_wids[f] == h_this &&
		     f+2 < m_nw && m_wids[f+2] == h_event &&
		     f+4 < m_nw && m_wids[f+4] == h_repeats )
			skip = true;
		// "claim it"
		if ( m_wids[f] == h_claim &&
		     f+2 < m_nw && m_wids[f+2] == h_it )
			skip = true;
		// "claim this event"
		if ( m_wids[f] == h_claim &&
		     f+2 < m_nw && m_wids[f+2] == h_this &&
		     f+4 < m_nw && m_wids[f+4] == h_event )
			skip = true;
		// "upcoming events"
		if ( m_wids[f] == h_upcoming &&
		     f+2 < m_nw && m_wids[f+2] == h_events )
			skip = true;
		// "other upcoming events..."
		if ( m_wids[f] == h_other &&
		     f+2 < m_nw && m_wids[f+2] == h_upcoming &&
		     f+4 < m_nw && m_wids[f+4] == h_events )
			skip = true;
		// "is this your [event|venue]?"
		if ( m_wids[f] == h_is &&
		     f+2 < m_nw && m_wids[f+2] == h_this &&
		     f+4 < m_nw && m_wids[f+4] == h_your )
			skip = true;
		// "feed readers..."
		if ( m_wids[f] == h_feed &&
		     f+2 < m_nw && m_wids[f+2] == h_readers )
			skip = true;
		// "no rating..."
		if ( m_wids[f] == h_no &&
		     f+2 < m_nw && m_wids[f+2] == h_rating )
			skip = true;
		// user reviews
		if ( m_wids[f] == h_user &&
		     f+2 < m_nw && m_wids[f+2] == h_reviews )
			skip = true;
		// reviews & comments
		if ( m_wids[f] == h_reviews &&
		     f+2 < m_nw && m_wids[f+2] == h_comments )
			skip = true;
		// skip urls
		if ( (m_wids[f] == h_http || m_wids[f] == h_https) &&
		     f+6 < m_nw &&
		     m_wptrs[f+1][0]==':' &&
		     m_wptrs[f+1][1]=='/' &&
		     m_wptrs[f+1][2]=='/' ) {
			skip = true;
		}

		// single word baddies
		if ( single ) {
			if ( m_wids[f] == h_new ) {
				badFirst = true;
				// fix abqtango.com "New" as an event title
				//tscore *= .50;
			}
			// "featuring"
			if ( m_wids[f] == h_featuring ) badFirst = true;
			// "what:"
			if ( m_wids[f] == h_what ) badFirst = true;
			if ( m_wids[f] == h_who  ) badFirst = true;
			if ( m_wids[f] == h_tickets) badFirst = true;
			// a price point!
			if ( m_wids[f] == h_free ) badFirst = true;
			// navigation
			if ( m_wids[f] == h_login   ) badFirst = true;
			if ( m_wids[f] == h_back    ) badFirst = true;
			if ( m_wids[f] == h_when    ) badFirst = true;
			if ( m_wids[f] == h_contact ) badFirst = true;
			if ( m_wids[f] == h_phone   ) badFirst = true;
			// stop hours from being title
			if ( m_wids[f] == h_hours   ) badFirst = true;
			// selection menu. sort by "relevancy"
			if ( m_wids[f] == h_relevancy ) badFirst = true;
			// from southgatehouse.com
			if ( m_wids[f] == h_tonight   ) skip = true;
			if ( m_wids[f] == h_today     ) skip = true;
			if ( m_wids[f] == h_share     ) skip = true;
			if ( m_wids[f] == h_join      ) skip = true;
			if ( m_wids[f] == h_loading   ) skip = true;
			if ( m_wids[f] == h_bookmark  ) skip = true;
			if ( m_wids[f] == h_publish   ) skip = true;
			if ( m_wids[f] == h_subscribe ) skip = true;
			if ( m_wids[f] == h_save      ) skip = true;
			if ( m_wids[f] == h_creator   ) skip = true;
			if ( m_wids[f] == h_tags      ) skip = true;
			if ( m_wids[f] == h_category  ) skip = true;
			if ( m_wids[f] == h_price     ) skip = true;
			if ( m_wids[f] == h_rate      ) skip = true;
			if ( m_wids[f] == h_rates     ) skip = true;
			if ( m_wids[f] == h_users     ) skip = true;
			if ( m_wids[f] == h_support   ) skip = true;
		}


		if ( badFirst )
			si->m_sentFlags |= SENT_BAD_FIRST_WORD;

		if ( skip )
			si->m_sentFlags |= SENT_NUKE_FIRST_WORD;

	}


	Section *prevSec = NULL;
	//////////////
	//
	// now if SENT_HAS_COLON was set and the next sentence is
	// a phone number, then we can assume we are the name of a person
	// or some field for which that phone # applies
	//
	//////////////
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		//if ( si->m_minEventId <= 0 ) continue;
		// save it
		Section *lastSec = prevSec;
		// update it
		prevSec = si;
		// skip if no last yet
		if ( ! lastSec ) continue;
		// ok, last guy must have had a colon
		if ( ! (lastSec->m_sentFlags & SENT_COLON_ENDS) ) continue;
		// we must be generic, end in a period or be mixed case
		// no, this hurts "Bill Holman Big Band: Composer, arranger..."
		//if ( ! (si->m_sentFlags & SENT_GENERIC_WORDS) &&
		//     ! (si->m_sentFlags & SENT_PERIOD_ENDS)   &&
		//     ! (si->m_sentFlags & SENT_MIXED_CASE) )
		//	continue;
		// this is good for cabq.gov libraries page "children's room:"
		// which has a phone number after it
		if ( ! ( si->m_sentFlags & SENT_HAS_PHONE ) )
		     // are we like "location:"
		     //! ( si->m_sentFlags & SENT_IN_ADDRESS ) ) 
			continue;
		// ok, punish last guy in that case for having a colon
		// and preceeding a generic sentence
		//lastSec->m_titleScore *= .02;
		lastSec->m_sentFlags |= SENT_FIELD_NAME;
	}


	if ( ! s_init9 ) initPlaceIndicatorTable();


	if ( ! m_alnumPosValid ) { char *xx=NULL;*xx=0; }

	/////////////////////////////
	//
	// set SENT_OBVIOUS_PLACE
	//
	// - indicate sentences that are place names
	// - end in "theater", "hall", "bar", etc.
	//
	////////////////////////////
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// only works on sentences for now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// sentence must not be mixed case
		if ( si->m_sentFlags & SENT_MIXED_CASE ) continue;
		// ignore if in menu ("the lower school" - maret.org)
		if ( si->m_sentFlags & SENT_IN_MENU ) continue;
		if ( si->m_sentFlags & SENT_IN_MENU_HEADER ) continue;
		// how many alnum words we got?
		int32_t na = si->m_alnumPosB - si->m_alnumPosA ;
		// skip if only one word
		if ( na <= 1 ) continue;
		// skip if more than 7
		if ( na > 7 ) continue;
		// last word in sentence is "last"
		int32_t last = si->m_sentb - 1;
		// back up if "last" is numeric like "Studio 3" or "Room 127"
		for ( ; last > si->m_senta ; last-- ) {
			// skip until alnumword
			if ( ! m_wids[last] ) continue;
			// stop if last is NOT starting with a number
			if ( ! is_digit(m_wptrs[last][0]))  break;
		}
		// make a phrase wid for "night club"
		int64_t pid = 0LL;
		for ( int32_t k = last - 1 ; k >= si->m_senta ; k-- ) {
			QUICKPOLL(m_niceness);
			if ( ! m_wids[k] ) continue;
			pid = m_pids[k];
			break;
		}
		// last word must be indicator:  "theater" "center" "school"...
		bool inTable = false;
		if ( s_pit.isInTable(&m_wids[last]) ) inTable = true;
		// check phrase id for "nightclub" for guysndollsllc.com
		if ( pid && s_pit.isInTable ( &pid ) ) 
			inTable = true;
		if ( ! inTable ) continue;
		// the word "the" or "a" cannot preceed indicator
		if ( m_wids[last-2] == h_the ) continue;
		// "find a store"
		if ( m_wids[last-2] == h_a ) continue;
		// "use of museums"
		if ( m_wids[last-2] == h_of ) continue;
		// first word
		int32_t i = si->m_senta;
		// "add/contact/use/search store"
		if ( m_wids[i] == h_add     ) continue;
		if ( m_wids[i] == h_contact ) continue;
		if ( m_wids[i] == h_use     ) continue;
		if ( m_wids[i] == h_search  ) continue;
		if ( m_wids[i] == h_find    ) continue;
		// school needs 3 words at least to fix
		// "drama/music/cooking/german school"
		if ( m_wids[last] == h_school && na <= 2 ) continue;
		// "gift shop" is a subplace name (collectorsguide.com)
		if ( m_wids[last] == h_shop && m_wids[last-2]==h_gift)continue;
		// "open house" is subplace name (maret.org)
		if ( m_wids[last]==h_house&& m_wids[last-2]==h_open)continue;
		// photo/image/video/media library/gallery
		if ((m_wids[last] == h_gallery||m_wids[last]==h_library) &&
		    (m_wids[last-2] == h_photo ||
		     m_wids[last-2] == h_image ||
		     m_wids[last-2] == h_picture ||
		     m_wids[last-2] == h_video ||
		     m_wids[last-2] == h_media ) )
			continue;
		// if contains "at" or "by" or blah blah, stop it
		int32_t j; for ( j = si->m_senta ; j < si->m_sentb ; j++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip tags
			if ( m_tids[j] ) continue;
			// strange punct?
			if ( ! m_wids[j] ) {
				char *p = m_wptrs[j];
				char *pend = p + m_wlens[j];
				for ( ; p < pend ; p++ ) {
					QUICKPOLL(m_niceness);
					if ( is_wspace_a(*p) ) continue;
					if ( *p == '\''      ) continue;
					// St. John's College Bookstore
					if ( *p == '.'       ) continue;
					break;
				}
				// bad punct? do not set the flag then
				if ( p < pend ) break;
				// otherwise, we are good to go
				continue;
			}
			// stop on these
			if ( m_wids[j] == h_at ) break;
			// presented by ....
			if ( m_wids[j] == h_by ) break;
			// copyright is bad
			if ( m_wids[j] == h_copyright ) break;
			// "proceed to west hartford center" (directs)
			if ( m_wids[j] == h_to ) break;
			// "... presents Swan Lake"
			if ( m_wids[j] == h_presents ) break;
			// "...review of ..."
			if ( m_wids[j] == h_review ) break;
			// folks from erda gardens
			if ( m_wids[j] == h_from ) break;
			// join us in crested butte
			if ( m_wids[j] == h_join ) break;
		}
		// if hit a bad word, do not set flag then
		if ( j < si->m_sentb ) continue;
		// set it
		si->m_sentFlags |= SENT_OBVIOUS_PLACE;
	}

	if ( ! m_alnumPosValid ) { char *xx=NULL;*xx=0; }

	//
	// set SENT_EVENT_ENDING
	//
	// . if sentence ends in "festival" etc. set this bit
	// . or stuff like "workshop a" is ok since 'a' is a stop word
	// . or stuff like 'sunday services from 9am to 10am'
	//
	// scan the sentences
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// only works on sentences for now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// compute this
		int32_t alnumCount = si->m_alnumPosB - si->m_alnumPosA ;
		// set event ending/beginning
		int32_t val = hasTitleWords(si->m_sentFlags,si->m_a,si->m_b,
					 alnumCount,m_bits,m_words,true,
					 m_niceness);
		if ( val == 1 )
			si->m_sentFlags |= SENT_HASTITLEWORDS;
		if ( val == -1 )
			si->m_sentFlags |= SENT_BADEVENTSTART;
	}

	/*
	  supplanted by hasTitleWords()

	//////////////////////
	//
	// set SENT_BADEVENTSTART
	//
	//////////////////////
	//
	// . '-': if a title sentences begins with one of these words then
	//        set SENT_BADEVENTSTART
	// . do not give bonus for SENT_EVENT_ENDING if SENT_BADEVENTSTART
	//   is set
	// . fixes "Presented by Colorado Symphony Orchestra"
	static char *s_starters [] = {
		"-presented", // presented by fred, bad
		//"+festival", // festival of lights, good
		//"+class",     // class w/ caroline & constantine
		//"+lecture", // lecture on physics
		//"+beginning", // beginning painting constantcontact.com
		//"+intermeditate", 
		//"+advanced"
	};
	// store these words into table
	static HashTableX s_sw;
	static char s_swbuf[2000];
	static bool s_init8 = false;
	if ( ! s_init8 ) {
		s_init8 = true;
		s_sw.set(8,4,128,s_swbuf,2000,false,m_niceness,"swtab");
		int32_t n = (int32_t)sizeof(s_starters)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_starters[i];
			Words w; w.set3 ( s );
			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// . store hash of all words, value is ptr to it
			// . put all exact matches into sw1 and the substring
			//   matches into sw2
			s_sw.addKey ( &h , &s );
		}
	}
	// . use the same event ending table to see if the title sentence
	//   begins with one of these "endings"
	// . should fix "Presented By Colorado Symphony Orchestra" from
	//   being a good event title
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// only works on sentences for now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// how many alnum words in it?
		int32_t na = si->m_alnumPosB - si->m_alnumPosA ;
		// need at least two words
		if ( na <= 1 ) continue;
		// skip if too long and not capitalized
		if ( na > 7 && (si->m_sentFlags & SENT_MIXED_CASE ) ) continue;
		// get FIRST word in potential event title
		int32_t i = si->m_senta;
		int32_t a = si->m_senta;
		int32_t b = si->m_sentb;
		// skip cruft though
		for ( ; i < a ; i++ ) {
			QUICKPOLL(m_niceness);
			if ( ! m_wids[i] ) continue;
			if ( bits[i] & D_IS_IN_DATE ) continue;
			if ( bits[i] & D_IS_STOPWORD ) continue;
			if ( m_wlens[i] == 1 ) continue;
			break;
		}
		// if all cruft, ignore
		if ( i == b ) continue;
		// go to next section if word not in our list
		if ( ! s_sw.isInTable ( &m_wids[i] ) ) continue;
		// must have at least one word after that
		int32_t next = i + 2;
		// skip tags
		for ( ; next < b && ! m_wids[next] ; next++ );
		// if no more words, forget it, go to next section
		if ( next >= b ) continue;
		// get the string
		char *str = *(char **)s_sw.getValue ( &m_wids[i] );
		// check sign
		//if      ( str[0] == '+' )
		//	si->m_sentFlags |= SENT_GOODEVENTSTART;
		if ( str[0] == '-' )
			si->m_sentFlags |= SENT_BADEVENTSTART;
		// must have a sign
		else { char *xx=NULL;*xx=0; }
	}
	*/


	///////////////////
	//
	// set SENT_PRETTY
	//
	///////////////////

	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// only works on sentences for now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		setSentPrettyFlag ( si );
	}

	return true;
}

// returns +1 if has positive title words/phrases
// returns -1 if has negative title words/phrases
// return   0 otherwise
int32_t hasTitleWords ( sentflags_t sflags ,
		     int32_t a ,
		     int32_t b ,
		     int32_t alnumCount ,
		     Bits *bitsClass ,
		     Words *words ,
		     bool useAsterisk ,
		     int32_t niceness ) {

	// need at least two alnum words
	if ( alnumCount <= 0 ) return 0;
	// skip if too long and not capitalized
	if ( alnumCount > 7 && (sflags & SENT_MIXED_CASE ) ) return 0;

	// sanity. we need s_pit to be initialized
	if ( ! s_init9 ) initPlaceIndicatorTable();


	int64_t *wids = words->getWordIds();
	nodeid_t *tids = words->getTagIds();
	char **wptrs = words->getWords();
	int32_t *wlens = words->getWordLens();
	int32_t  nw = words->getNumWords();

	// . int16_tcut
	// . we are also called from dates.cpp and m_bits is NULL!
	wbit_t *bits = NULL;
	if ( bitsClass ) bits = bitsClass->m_bits;

	static bool s_flag = false;
	static int64_t h_annual;
	static int64_t h_anniversary;
	static int64_t h_next;
	static int64_t h_past;
	static int64_t h_future;
	static int64_t h_upcoming;
	static int64_t h_other;
	static int64_t h_more;
	static int64_t h_weekly;
	static int64_t h_daily;
	static int64_t h_permanent; // fix permanent exhibit for collectorsg
	static int64_t h_beginning ;
	static int64_t h_every ;
	static int64_t h_featuring ;
	static int64_t h_for ;
	static int64_t h_at ;
	static int64_t h_by ;
	static int64_t h_on ;
	static int64_t h_no ;
	static int64_t h_name ;
	static int64_t h_in ;
	static int64_t h_sponsored;
	static int64_t h_sponsered;
	static int64_t h_presented;
	static int64_t h_i;
	static int64_t h_id;
	static int64_t h_begins ;
	static int64_t h_meets ;
	static int64_t h_benefitting ;
	static int64_t h_benefiting ;
	static int64_t h_with ;
	static int64_t h_starring ;
	static int64_t h_experience;
	static int64_t h_w ;
	static int64_t h_event;
	static int64_t h_band;
	static int64_t h_tickets;
	static int64_t h_events;
	static int64_t h_jobs;
	static int64_t h_this;
	static int64_t h_series;
	static int64_t h_total;
	static int64_t h_times;
	static int64_t h_purchase;
	static int64_t h_look;
	static int64_t h_new;
	static int64_t h_us;
	static int64_t h_its;

	if ( ! s_flag ) {
		s_flag = true;
		h_annual = hash64n("annual");
		h_anniversary = hash64n("anniversary");
		h_next = hash64n("next");
		h_past = hash64n("past");
		h_future = hash64n("future");
		h_upcoming = hash64n("upcoming");
		h_other = hash64n("other");
		h_more = hash64n("more");
		h_weekly = hash64n("weekly");
		h_daily = hash64n("daily");
		h_permanent = hash64n("permanent");
		h_beginning = hash64n("beginning");
		h_every = hash64n("every");
		h_featuring = hash64n("featuring");
		h_for = hash64n("for");
		h_at = hash64n("at");
		h_by = hash64n("by");
		h_on = hash64n("on");
		h_no = hash64n("no");
		h_name = hash64n("name");
		h_in = hash64n("in");
		h_sponsored = hash64n("sponsored");
		h_sponsered = hash64n("sponsered");
		h_presented = hash64n("presented");
		h_i = hash64n("i");
		h_id = hash64n("id");
		h_begins = hash64n("begins");
		h_meets  = hash64n("meets");
		h_benefitting = hash64n("benefitting");
		h_benefiting = hash64n("benefiting");
		h_with = hash64n("with");
		h_starring = hash64n("starring");
		h_experience = hash64n("experience");
		h_w = hash64n("w");
		h_event = hash64n("event");
		h_band = hash64n("band");
		h_tickets = hash64n("tickets");
		h_events = hash64n("events");
		h_jobs = hash64n("jobs");
		h_this = hash64n("this");
		h_series = hash64n("series");
		h_total = hash64n("total");
		h_times = hash64n("times");
		h_purchase = hash64n("purchase");
		h_look = hash64n("look");
		h_new = hash64n("new");
		h_us = hash64n("us");
		h_its = hash64n("its");
	}

	// . if it just consists of one non-stop word, forget it!
	// . fixes "This series" for denver.org
	// . fixes "Practica" for abqtango.org
	// . we have to have another beefy word besides just the event ending
	int32_t goodCount = 0;
	for ( int32_t k = a ; k < b ; k++ ) {
		QUICKPOLL(niceness);
		if ( ! wids[k] ) continue;
		if ( bits && (bits[k] & D_IS_IN_DATE) ) continue;
		if ( bits && (bits[k] & D_IS_STOPWORD) ) continue;
		if ( wlens[k] == 1 ) continue;
		// treat "next" as stop word "next performance" etc.
		// to fix "next auction"?
		if ( wids[k] == h_next ) continue;
		if ( wids[k] == h_past ) continue;
		if ( wids[k] == h_future ) continue;
		if ( wids[k] == h_upcoming ) continue;
		if ( wids[k] == h_other ) continue;
		if ( wids[k] == h_more  ) continue;
		if ( wids[k] == h_beginning ) continue; // beginning on ...
		if ( wids[k] == h_weekly  ) continue; // weekly shows
		if ( wids[k] == h_daily  ) continue;
		if ( wids[k] == h_permanent ) continue;
		if ( wids[k] == h_every ) continue; // every saturday
		goodCount++;
		if ( goodCount >= 2 ) break;
	}
	// . need at least 2 non-stopwords/non-dates
	// . crap, what about "Bingo" - let reduce to 1
	//if ( goodCount <= 1 ) return 0;
	if ( goodCount <= 0 ) return 0;

	// "9th annual...."
	// "THE 9th annual..."
	// 2012 annual
	for ( int32_t k = a ; k < b ; k++ ) {
		if ( !is_digit(wptrs[k][0]))  continue;
		if ( k+2>=nw ) continue;
		if ( wids[k+2] == h_annual ) return true;
		if ( wids[k+2] == h_anniversary ) return true;
	}

	//
	// TODO: * with <person name>
	//

	// . a host list of title-y words and phrases
	// . event title indicators
	// . ^ means must be in beginning
	// . $ means must match the end
	// . * means its generic and must have 2+ words in title
	static char *s_twords[] = {
		"$jazzfest",
		"$photofest",
		"$fest", // Fan fest
		"$fanfest",
		"$brewfest",
		"$brewfestivus",
		"$musicfest",
		"$slamfest",
		"$songfest",
		"$ozfest",
		"$winefest",
		"$beerfest",
		"$winterfest",
		"$summerfest",
		"$springfest",
		"|culturefest",
		"$fest", // reggae fest
		"$fallfest",
		"$o-rama", // string-o-rama
		"$jazzshow",
		"$musicshow",
		"$songshow",
		"$wineshow",
		"$beershow",
		"$wintershow",
		"$summershow",
		"$springshow",
		"$fallshow",
		"$wintershow",
		"$winter fantasy",
		"$recital",
		"$auditions",
		"$audition",
		"$festival",
		"$the festival", // the festival of trees is held at...
		"$festivals",
		"$jubilee",
		"$concert",
		"$concerts",
		"$concerto",
		"$concertos",
		"$bout", // fight
		"*$series", // world series, concert series
		"-this series", // fix denver.org
		"-television series",
		"-tv series",
		"$hoedown",
		"$launch", // album launch
		"*$3d", // puss in boots 3d

		"*$beginning", // beginning painting constantcontact.com
		"*$intermediate", 
		"*$advanced",
		"*^beginning", // beginning painting constantcontact.com
		"-^beginning in", // beginning in january
		"-^beginning on",
		"-^beginning at",
		"*|intermediate",  // drawing, intermediate
		"*^int",
		"*^advanced",
		"*^adv",
		"*^beginner", // beginner tango
		"*$beginners", // for beginners
		"*^beg", // beg. II salsa
		"*^adult", // Adult III

		"*$con",
		"$convention",
		"$comiccon",
		"$jobfair",
		"$peepshow",
		"$ballyhoo",

		"$open", // french open
		"-$half open", // fix
		"-$we're open", // fix
		"-$is open", // fix
		"-$be open", // fix
		"-$percent open",
		"-$now open", // fix
		"-$shop open", // fix
		"-$office open",
		"-$re open",
		"-$building open", // TODO: use all place indicators!
		"-$desk open", // help desk open
		"open mic", // anywhere in the title

		// . should fix "Presented By Colorado Symphony Orchestra" from
		//   being a good event title
		"-^presented",

		"*$opening", // grand opening, art opening
		"$spectacular", // liberty belle spectacular
		"$classic", // 2nd annual cigar club classic
		"$carnival",
		"$circus",
		"$rodeo",
		"$ride", // bike ride
		"$rides", // char lift rides
		"train ride",
		"train rides",

		"$summit", // tech summit
		"$lecture",
		"*$overview", // BeachBound+Hounds+2012+Overview
		"$talk", // Curator's+Gallery+Talk
		"$discussion", // panel discussion
		"^panel discussion",
		"|public hearing",
		"$webinar",
		"$teleseminar",
		"$seminar",
		"$seminars", // S.+Burlington+Saturday+Seminars
		"$soiree", // single's day soiree
		"$extravaganza",
		"|reception",
		"-|reception desk",
		"$tribute",
		"$parade",
		"^2011 parade", // 2011 parade of homes
		"^2012 parade",
		"^2013 parade",
		"^2014 parade",
		"^2015 parade",
		"$fireworks",
		//"$car club", // social car club. "club" itself is too generic
		//"$book club",
		//"$yacht club",
		//"$glee club",
		//"$knitting club",
		//"mountaineering club",
		//"debate club",
		//"chess club",
		"*$club",
		"club of", // rotary club of kirkland
		"$forum", // The+bisexual+forum+meets+on+the+second+Tuesday
		"$meet", // swap meet
		"$swap", // solstice seed swap
		"$board", // board meeting
		"^board of", // board of selectmen
		//"-board meeting", // i'm tired of these!!
		//"-council meeting",
		"*$meeting",
		"*$mtg",
		"*$mtng",
		"*$meetings", // schedule of meetings
		"*$meet", // stonegate speech meet/ track meet
		"-no meeting",
		"-no meetings",
		"-no game",
		"-no games",
		"*$mtg", // State-Wide+Educational+Mtg
		"$meetup",
		"$meetups",
		"$meet ups",
		"$meet up",
		"*meets on" , // grade 8 meets on thursday
		"^meet the", // meet the doula
		"$committee",
		"$council", // parish council
		"$hearing", // public hearing
		"$band", // blues band
		"$quartet",
		"$trio",
		"$networking event",
		"$social", // polar express social
		"tour of", // tour of soulard / The+Chocolate+Tour+of+New+York
		"|tour",// tour boulevard brewery
		"|tours", // tours leave the main bldg...
		"$cruise",
		"$cruises",
		"cruise aboard", // dog paddle motor cruise abord the tupelo
		"motor cruise",
		"$safari", // fishing safari
		"*$trip", // photography trip
		"*$trips", // photography trip
		"$slam", // poetry slam
		"$readings", // poetry readings
		"$expedition",
		"$expeditions",
		"orchestra", // middle school orchestra in the chapel
		"$ensemble",
		"$ensembles",
		"$philharmonic",
		"$chorale",
		"$choir",
		"$chorus",
		"$prelude", // music prelude

		"-$website", // welcome to the blah website
		"-$blog",
		"-$homepage",

		"*$group", // pet loss group
		"*$groups", // 12 marin art groups
		"$sig", // special interest group
		"-$your group", // promote your group
		"-$your groups", // promote your group
		"-sub group", // IPv6+Addressing+Sub-group
		"-$a group", // not proper noun really
		"-$the group",
		"-$the groups",
		"-$by group",
		"-$by groups",
		"-$or group",
		"-$or groups",
		"-$for group",
		"-$for groups",
		"-$sub groups",
		"-$participating group",
		"-$participating groups",
		"-$large group",
		"-$large groups",
		"-$small group",
		"-$small groups",
		"-$age group",
		"-$age groups",
		"-$profit group",
		"-$profit groups",
		"-$dental group",
		"-$dental groups",
		"-$eyecare group",
		"-$eyecare groups",
		"-$medical group",
		"-$medical groups",
		"-$private group",
		"-$private groups",
		"-$media group", // locally owned by xxx media group
		"conversation group",
		"book group",
		"reading group",
		"support group",
		"support groups",
		"discussion group",
		"discussion groups",
		"$playgroup",
		"$workgroup",
		"$intergruopgroup",

		"|orientation", // Orientation+to+Floortime
		// no! "$all day" "$minimum day" from is too generic for
		// mvusd.us
		"$day", // heritage day. kite day
		"$day out", // girl's day out
		"$play day",
		"*day of", // day of action
		"*hour of", // hour of power
		"$day 2012", // local history day 2012
		"*$all day",
		"*$every day", // happy hour every day vs. "every day"
		"*$this day",
		"-$per day",
		"$caucus",
		"$caucuses",

		"*$days", // liberty lake days
		"$expo",
		"$exposition",
		"*$session",// sautrday evening jazz session
		"*$sessions",
		//"-$current exhibition", // not a good title?
		"-$current session", // fixes
		"-$current sessions",
		"-$event session",
		"-$event sessions",
		//"-$schedule",
		"-$calendar",
		"-$calendars",
		"$revue",
		"*$lesson",
		"*$lessons",
		"rehersal",
		"rehearsal", // misspelling
		"$audition",
		"$auditions",
		"*$practice",
		"*$practices", // Countryside+Christian+Basketball+Practices
		"*$practica", // common for dance
		"^guided", // guided astral travel
		"*|training", // training for leaders
		"*$exercise", // Sit+&+Fit+Chair+Exercise+for+Seniors
		"$performance",
		"$performances",
		"*$dinner",
		"*$lunch",
		"*$luncheon",
		"*$brunch",
		"$bbq",
		"$barbeque",
		"|auction", // auction begins at
		"|auctions",
		"$run", // fun run
		"$trek", // turkey trek
		"$trot", // turk trot
		"$walk", // bird walk, tunnel walk
		"$walks", // ghost walks
		"$ramble", // plymouth rock ramble
		"$crawl", // spring crawl, pub crawl
		"$ramble", // turkey ramble
		"$ceremony", // tree lighting ceremony
		"$ceremoney", // misspelling
		// "ceremonies" itself is too generic. gets 
		// "ballroom ceremonies" as part of a sales rental putch
		"$opening ceremonies", // opening ceremonies
		"art opening", // Art+Opening+-+Jeff+Hagen+Watercolorist
		"-certificate", // Coaching+Certificate
		"-$supplies", // yoga supplies
		"$awards", // 2011 Vision Awards
		"$banquet",
		"$banquets",
		"*$ball",
		"-$county", // always bad to have a county name in the title
		"-$counties",
		"scavenger hunt",
		"$celebration", // a christmas celebration
		"celebration of", // celebration of giving
		"celebrates", // +Band+Celebrates+a+Season+of+Red,...
		"celebrate", // Celebrate Recovery
		"$showdown", // sunday showdown
		"|yoga", // astha yoga
		"|meditation", // simply sitting meditation, meditation 4a calm
		"^family", // family climb, family drum time
		"$taiko", // japanese drumming
		"|karaoke", // karaoke with jimmy z
		"$party", // best of the city party
		"-$to party", // 18+ to party
		"|symposium", // event where multiple speaches made
		"|symposiums", // friday night symposiums
		"|composium",
		"|composiums",
		"|colloquium",
		"|colloquiums",
		"afterparty",
		"|blowout",
		"$potluck",
		"$pot luck",
		"$bonanza",
		"$night out", // girls' night out
		"$showcase", // bridal showcase
		"$show case", // bridal showcase
		"$sideshow", // circus sideshow
		"$hockey",
		"|bad minton",
		"|badminton",
		"ping pong",
		"pingpong",
		"$pickleball",
		"$swim", // open swim, lap swim, women only swim
		"|swimming",
		"|skiing",
		"|carving", // wood carving
		"|tai chi",
		"|balance chi",
		"|taichi",
		"|karate",
		"|judo", // kids judo
		"|wrestling",
		"|jujitsu",
		"|ju jitsu",
		"|walking",
		"|pilates",
		"|aerobics",
		"|jazzercise",
		"|birding",
		"|kickboxing",
		"|kick boxing",
		"|billiards",
		"$table tennis",
		"$basketball",
		"$fishing",
		"^fishing in", // fishing in the midwest
		"^cardiohoop",
		"$crossfit",
		"$zumba",
		"$scouts", // cub/boy/girl scouts
		"$webelos",
		"$baseball",
		"$softball",
		"$football",
		"$foosball",
		"$soccer",
		"$bb",
		"$volleyball",
		"$vb",
		"$painting", // pastel painting
		"$sculpting", // body sculpting
		"^body sculpting",
		"^chess", // chess for kids
		"$campus visit",
		"$tennis",
		"*$sale", // jewelry sale: 10% off
		"book sale",
		"book clearance",
		"$bash",
		"$pow wow",
		"$powwow",
		"$camp", // iphone boot camp / space camp
		"*bootcamp", // for one word occurences
		"*$tournament",
		"*$tournaments", // daily poker tournaments
		"$tourney",
		"$tourneys",
		"$competition",
		"$contest",
		"$cook off",
		"$bake off",
		"$kick off",
		"$fair",
		"$jam", // monday night jam
		"$jamboree",
		"$exhibition",
		"$exhibit",
		"^exhibition of",
		"^exhibit of",
		"^evening of", // evening of yoga & wine
		"group exhibition",
		"group exhibit",
		"$therapy",
		"$support", // Coshocton+County+Peer+Support
		//"^exhibition", // exhibition: .... no. "exhibit hall"
		// Graffiti Group Exhibition @ Chabot College Gallery
		"exhibition",
		"exhibitions",
		"exhibit",
		"exhibits",
		"$retrospective",
		"food drive",
		"coat drive",
		"toy drive",
		"blood drive",
		"recruitment drive",
		"waste drive",
		"donor drive",
		"$christmas",
		"$winterland",
		"$wonderland",
		"$christmasland",
		"$carol",
		"$carolling",
		"$caroling",
		"$caroles", // 3T Potluck & Caroles
		"$carols",
		"*$demo", // Spinning demo by margarete... villr.com
		"*$demonstration",
		"$debate",
		"$racing",
		"$race",
		"|5k", // 5k run/walk ... otter run 5k
		"|8k", // 5k run/walk
		"|10k", // 5k run/walk
		"$triathalon",
		"$triathlon",
		"$biathalon",
		"$biathlon",
		"$duathalon",
		"$duathlon",
		"$marathon",
		"$thon", // hack-a-thon
		"$athon",
		"$runwalk", // run/walk
		"*$relay", // Women's+Only+1/2+marathon+and+2+person+relay
		"*$hunt", // egg hunt, scavenger hunt

		// no, gets "write a review"
		//"$review", // Bone+Densitometry+Comprehensive+Exam+Review

		"|bingo",
		"|poker",
		"|billiards",
		"|bunco",
		"|crochet",
		"|pinochle",
		"|dominoes",
		"|dominos",
		"*|domino",
		"*$game", // basketball game
		"$game day", // senior game day
		"^adoption day", // Adoption+Day,+Utopia+for+Pets
		"*$gaming", // Teen+Free+Play+Gaming+at+the+Library
		"*$program", // kids program, reading program
		"school play",
		"$experience", // the pink floyd experience

		"*$programs", // children's programs
		"-^register", // register for spring programs
		"$101", // barnyard animals 101
		"*$techniques", // Core+Training+++Stretch+Techniques
		"*$technique", // course 503 raindrop technique
		"*$basics", // wilton decorating basics
		"^basics of",
		"^the basics",
		"*^basic" , // basic first aid (gets basic wire bracelet too!)
		"$first aid",
		"^fundamentals of",
		"$fundamentals", // carving fundamentals
		"^principles of",
		"$principles",
		"^intersections of",
		"$gala", // the 17th annual nyc gala
		"*$anonymous", // narcotics anonymous
		"$substance abuse", // women's substance abuse
		"$weight watchers",
		"$mass", // vietnamese mass (watch out for massachussettes)
		"midnight mass",
		"$masses",
		"|communion",
		"|keynote",
		"^opening keynote",
		"spelling bee",
		"on ice", // disney on ice
		"for charity", // shopping for charity
		"a charity", // shopping for a charity
		"storytime", // children's storytime
		"storytimes",
		"$story", // the hershey story
		"commencement", // sping commencement: undergraduate
		"$walk to", // walk to end alzheimer's
		"$walk 2", // walk to ...
		"$walk for", // walk for ...
		"$walk 4", // walk for ...
		"$walk of", // walk of hope
		"$encounters", // sea lion encounters
		"$encounter", // sea lion encounter
		"-visitor information", // Visitor+Information+@+Fort+Worth+Zoo
		"*$breakfast", // 14th+Annual+Passaic+Breakfast
		"presentation", // Annual+Banquet+&+Awards+Presentation
		"presentations",
		"-available soon",//Presentation+details+will+be+available+soon
		"bike classic",
		"$havdalah", // Children's+Donut+Making+Class+and+Havdalah
		"|shabbat",
		"|minyan", // what is this?
		"|minyans", // what is this?
		"fellowship",
		"$benefit", // The+Play+Group+Theatre+6th+Annual+Benefit
		"children's church",
		"sunday school",
		"*$event", // dog adoption event, networking event
		"-$events", // Ongoing Tango Club of Albuquerque * events at...
		"-private event",
		"-view event",
		"-view events",
		"gathering",
		"gatherings", // all-church gatherings
		"$mixer", // dallas networking mixer

		// put this into the isStoreHours() function in Dates.cpp
		//"-is open", // Our+Website+is+Open+for+Shopping+24/7
		//"-are open", // Our+Website+is+Open+for+Shopping+24/7
		//"-store hours", // Winter+Store+Hours+@+13+Creek+St.
		//"-shopping hours", // extended shopping hours
		//"-shop hours",
		//"-deadline", 
		//"-deadlines", 

		"-news", // urban planning news
		"-posted", // posted in marketing at 8pm
		"-driving directions",

		// popular titles
		"side story", // west-side story
		"westside story", // west-side story
		"doctor dolittle",
		"nutcracker",
		"mary poppins",
		"harlem globetrotters",
		"no chaser", // straight, no chaser
		"snow white",
		"charlie brown",
		"pumpkin patch",
		"marie osmond",
		"hairspray",
		"defending the", // defending the caveman
		"lion king",
		"ugly duckling",
		"santa claus", // santa claus is coming to town
		"stomp",
		"chorus line",
		"^cirque", // cirque do soleil
		"red hot", // red hot chili peppers
		"street live", // sesame street live
		"the beast", // beauty and the beast
		"lady gaga",
		"led zeppelin",
		"tom petty",
		"adam ant",
		"kid rock",
		"|annie", // little orphan annie play
		"swan lake",

		// popular event names?
		"crafty kids",
		"sit knit", // sit & knit

		// popular headliners
		//"larry king",

		// TODO: support "*ing club"(mountaineering club)(nursing club)
		// TODO: blah blah 2012 is good!!

		// gerunds (| means either $ or ^)
		"*^learning",
		"|bowling",
		"*$bowl", // orange bowl, super bowl
		"|singing", // Children's+Choirs+Singing
		"|sing along", // Messiah+Sing-Along
		"|singalong",
		"^sing", // community sing
		"$singers", // Lakeside+Singers+at+NCC
		"|soapmaking", // Girls+Spa+Day:+Lip+balm,Perfume&Soapmaking:
		"|scrapbooking", // What's+new+in+Scrapbooking
		"|exhibiting", // Exhibiting+at+The+Center+for+Photography
		"|healing", // Service+of+Healing
		"^service of", // Service+of+Remembrance,+Healing+and+Hope
		"^a healing", // a healing guide to renewing...
		"^the healing",
		"star gazing",
		"stargazing",
		"|meditating", // Meditating+With+The+Body
		"*$showing",
		"*$shooting", // Trap+shooting
		"*$skills", // Resume+and+Interviewing+Skills

		"|networking",
		"|making", // making money
		// no, was getting "serving wayne county since 1904"
		// and a bunch of others like that
		//"*^serving", // serving the children of the world
		"-serving dinner",
		"-serving breakfast",
		"-serving lunch",
		"-serving brunch",
		"|diving",
		"^hiking",
		"$hike",
		"*^varsity", // Varsity+Swim+&+Dive+-+ISL+Diving+@+Stone+Ridge
		"*^junior varsity",
		"*^jv",
		"|judging", // plant judging
		"rock climbing",
		"|kayaking",
		"|bellydancing",
		"|bellydance",
		"|belly dancing",
		"|belly dance",
		"|square dancing",
		"|square dance",
		"|swing dancing",
		"|swing dance",
		"swing night",
		"$speaking", // Pastor+Mike+speaking+-+December+18,+2011
		"|canoeing",
		"|wrestling",
		"|knitting",
		"|needlework",
		"|crocheting",
		"$voting", // early voting
		"|printmaking",
		"|making", // paper bead making
		"|writing", // ENG+2201:+Intermediate+College+Writing
		"|sharing", // Wisdom+of+the+Sangha:+Sharing,+Reflection...
		"|decorating", // wilton decorating basics
		"|reading", // Tess+Gallagher+reading+at+Village+Books
		"|readings", // Tess+Gallagher+reading+at+Village+Books
		"-currently reading", // Currently Reading:
		"|poetry",
		"$and friends",
		"*^celebrating", // Celebrating+Sankrant+2012
		"*^interpreting", // intepreting for the deaf
		"*^researching", // "Researching+and+Reflecting:+Sounds,+Sights
		"*^reflections", // Stories:+Reflections+of+a+Civil+War+Historia
		"*^reflecting",
		"*^enhancing",
		"*^mechanisms", // Mechanisms+Involved+in+Generating...
		"*^finding", // finding+time+to...
		"*^transforming", // Transforming+Transportation+2012
		"*^reinventing",
		"*^making", // Making+Good+on+the+Promise:+Effective+Board+...
		"*^creating", // Creating+Cofident+Caregivers
		"*^giving", // Giving+in+ALLAH's+Name
		"*^getting", // Getting+Out+of+Debt, Getting+to+Know...
		"-getting here", // directions! bad!
		"-getting there",// directions! bad!
		"*^turning", // Turning+Your+Strategic+Plan+into+an+Action+Plan
		"*^engaging", // Engaging+Volunteers+with+Disabilties
		"*^governing",
		"*^managing", // Managing+Your+Citations+with+RefWorks
		"*^entering", // entering the gates of judaism
		"*^stregthening", // ...+Baptist+Church:+Strengthening+the+Core
		"treeplanting",
		"-managing director",
		"-managing partner",
		"^issues in",
		"^the issues",
		"*^countdown", // countdown to new year's eve sweepstakes
		"*^navigating", // Navigating+the+Current+Volatility+
		// defensive driving
		"*|driving", // Driving+Innovation+&+Entrepreneurship
		"*^using", // Using+OverDrive
		"*^letting", // Letting+Poetry+Express+Your+Grief
		"*^feeding", // Feeding+Children+Everywhere
		"*^feeling", // Feeling+Out+of+Balance?
		"*^educating", // "Educating+for+Eternity"
		"*^demystifying", // like understanding (demystifying weightloss
		"*^discovering",
		"*^equipping", // Equipping+Ministry+-+Gospel+and+Culture
		//"-^no", // no mass, no ... HURTS "no name club" too bad!!
		"-$break", // fall break, winter break...
		"-not be", // there will not be a dance in december
		"-pollution advisory",
		"-|closed", // holiday hours - museum closed, closed for xmas
		"-is closing",
		"-is closed",
		"-be closing",
		"-be closed",
		"-are closing",
		"-are closed",
		"-$vacation", // vacation
		"-cancelled",
		"-canceled",
		"-^calendar", // calendar of events
		"-^view", // view upcoming events inlyons
		"-^suggest", // suggest a new event
		"-^find us", // find us on facebook...
		"-^hosted by", // Hosted by Cross Country Education (CCE)
		"*^comment", // Comment+in+George+Test+Group+group
		"-^purchase", // Purchase Products from this Seminar
		"-in recess", // class in recess
		"*^playing", // Playing+For+Change
		"*$drawing", // portrait drawing
		"*^integrating",
		"*^greening",
		"*^dispelling",
		"*^growing", // Growing+Up+Adopted
		"*^looking",
		"*^communicating",
		"*^leasing",
		"*^assessing",
		"^quit smoking",
		"*^exploring",
		"history of", // "Headgear:+The+Natural+History+of+Horns+and+
		"*^are your", // Are+Your+Hormones+Making+You+Sick?
		"*^are you",
		"*^when will", // When+Will+the+Outdoor+Warning+Sirens+Sound?
		"*^concerned about", // Concerned+About+Outliving+Your+Income?
		"*discover your", // discover your inner feelings
		"*creating your", // creating your personal brand
		"walking tour",
		"|strategies", // speach topics
		"*|coaching", // strategies for coaching
		"*|watching", // watching paint dry
		"ice skating",
		"free screening",
		"film screening",
		"^screening of",
		"^the screening",
		"movie screening",
		"$trilogy", // back to the future trilogy
		"dj spinning",
		"$screening", // Free+Memory+Screening
		"$screenings", // Salt+Ghost+DVD+screenings+at+9+p.m
		"$tastings", // Chocolate+Tastings+at+Oliver+Kita+Chocolates
		"wine bar",
		"open bar",
		"open gym",
		"open swim",
		"*byob", // always a good indicator
		"tea tastings",
		"*coming to", // santa claus is coming to town, blah blah...

		"^improv", // improv and standup
		"^standup",
		
		"*$circle", // past life healing circle
		"^circle of", // circle of knitting (not "circle 8's" sq dnc)
		"$invitational",
		"$invitationals",
		"-$the market", // new on the market (house sale)
		"*$market", // villr.com Los Ranchos Growers' Market
		"*$markets",// villr.com fix for "Markets ((subsent))"
		"$bazaar", // holiday bazaar
		"$bazzaar", // holiday bazaar
		"$sailing", // boat sailing
		"*$sail", // free public sail
		"candle lighting",
		"menorah lighting",
		"*$lights", // river of lights, holiday lights
		"*$lighting",
		"tree lighting",
		"tree trimming",
		"book signing",
		"booksigning",
		"bookfair",
		"ribbon cutting",
		"*special guest", // +&+Hits+with+special+guest+Billy+Dean
		// got Myspace.com:Music instead of the better title
		// for music.myspace.com
		//"$music", // live music
		"^music at", // music at the mission
		"$of music",
		//"$of jazz", // evening of jazz, jazz music
		"|jazz", // children's jazz
		"$feast", // a bountiful burlesque feast
		//"$live", // junkyard jane live - but gets a place name "Kobo live".. so bad
		"*$spree", // let it snow holiday shopping spree

		"|public skating",
		"^public skate",

		// no caused 'state emission inspection' to come up
		//"$inspection", // building life safety inspections
		//"$inspections",

		"*^occupy", // occupy portland
		"$fundraiser",
		"^fundraising", // fundraising performance for ...
		"$raffle",
		"$giveaway", // anniversary quilt giveaway
		"$townhall",
		"town hall",
		"open house",
		"open houses", // then a list of times...
		"pumpkin patch",
		"happy hour",
		"cook off",
		"bake off",
		"story time",
		"story telling",
		"storytelling",
		"story hour",
		"speed dating",

		// this can be a place name too easily: the empty bottle
		// The Florida Rowing Center
		//"^the", // IS THIS GOOD???? not for "

		"$worship",
		"$rosary",
		"bible study",
		"bible studies",
		"torah study",
		"torah studies",
		"$prayer", // bible study and prayer
		"^pray", // pray in the new year
		"$eve service", // christmas/thanksgiving eve service
		"$penance service",
		"$penance services",
		"$candlelight service",
		"$candlelight", // carols & candlelight
		"eve worship",
		"eucharist",
		"worship service",
		"worship services",
		"morning service",
		"morning services",
		"night service",
		"night services",
		"evening service",
		"evening services",
		"sunday services", // church stuff
		"monday services", // church stuff
		"tuesday services", // church stuff
		"wednesday services", // church stuff
		"thursday services", // church stuff
		"friday services", // church stuff
		"saturday services", // church stuff
		"worship services", // church stuff
		"church services", // church stuff
		"sunday service", // church stuff
		"monday service", // church stuff
		"tuesday service", // church stuff
		"wednesday service", // church stuff
		"thursday service", // church stuff
		"friday service", // church stuff
		"saturday service", // church stuff
		"day service", // memorial day service, etc.
		"candleight service",
		"prayer service",
		"traditional service",
		"traditions service",
		"blended service",
		"shabbat service",
		"shabbat services",
		"contemporary service",
		"lenten service",
		"celebration service",
		"worship service",
		"eucharist service",
		"service times",
		"service time",
		"sunday mass",
		"monday mass",
		"tuesday mass",
		"wednesday mass",
		"thursday mass",
		"friday mass",
		"saturday mass",

		"$taco tuesdays",

		"$tasting", // wine tasting
		"$tastings", // wine tastings
		"*$conference", // Parent/Teacher+Conference+
		"*$conferences",
		"*$retreat",
		"$adventure", // big bird's adventure
		"^the adventures", // the adventures of...
		"$workshop", // "$workshop a" for aliconferences.com
		"$workshops", // budget workshops
		"$worksession", // city council worksession
		"^rally", // rall to support the EPA's mercury health standards
		"$rally",  // pep rally
		"$fair",
		"$fairs",
		"*$night", //biker night for guysndollsllc.com,family fun night
		// dance club nights - hurts ritmo caliente because we lose
		// that title and get "$main room, dance nights, 21+"
		//"$nights", 
		"*^dj", // dj frosty
		"$music nights",
		"$championships",
		"$championship",
		"$challenge",
		"$picnic",
		"$dance",
		"$dancing", // irish traditional dancing
		"$dancers", // irish ceili dancers
		"$freestyle", // type of dance
		"^freestyle", // type of dance
		"^cardio",
		"*$fitness", // pre-natal fitness
		"*$workout", // football workout
		"-school of", // nix school of ballet
		"$tango",  // tango for beginners
		"$ballet", 
		"$preballet", 
		"|linedancing",
		"$modern",
		"$waltz", 
		"$polka", 
		"$musical", // menopause the musical
		"$swing",
		"$milonga", // type of dance
		"$bachata",
		"|salsa", // Salsa in New Mexico
		"$tap",
		"$pagent",
		"$pageant", // christmas pageant
		"*$tutoring",
		"*$tutorial",
		"*$tutorials",
		"*$instruction",
		"*$education", // childbirth education
		"-no class", // holday break - no classes
		"-no classes",// holday break - no classes
		"-unavailable",
		"-last class",
		"-no school",
		"*$class",
		"*$classes",
		"*$teleclass",
		"*$certification",
		"*$class week", // massage class week 1
		"*$class level", // massage class level 1
		"*$mixed level", // Hatha+Basics+Mixed+Level
		"*$mixed levels",
		"*$part 1", // Recent+Acquisitions,+Part+I:+Contemporary+Photos
		"*$part 2",
		"*$part 3",
		"*$part 4",
		"*$part i",
		"*$part ii",
		"*$part iii",
		"*$part iv",
		"*$class session", // massage class session 1
		"*$course",
		"*$courses",
		"-$golf course",
		"-$golf courses",
		"*$lessons", // Free Latin Dancing Lessons
		"*$lesson",
		"-$no shows", // we bill for no shows
		"*$show", // steve chuke show
		"*$shows",
		"-no show",
		"-no shows",
		"-past shows",
		"-future shows",

		// stuff at start
		"*^annual",
		"|anniversary", // anniversary dinner, 2 year anniversary
		"festival of", // Masonicare+Festival+of+Trees
		"^learn to",
		"*^understanding", // Understanding Heart Valves (lecture)
		"*introduction to", // lecture
		"*^introductory", // introductory potter's wheel
		"^introduction", // ... : introduction climb
		"*how to", // cheap how to succeed in business...
		"-reach us", // how to reach us
		"-contact us", // how to contact us
		"*intro to", // lecture
		"*^the story", // the story of carl kiger
		"*^all about", // Class - All About Sleep Apnea
		"*$an introduction", // lecture
		"*$an intro", // lecture
		"*^the wonder", // the wonder of kites
		//"^dance", // 'dance location map' ceder.net!
		"^graduation",
		"*$special", // valentine's special
		"*world premier",

		// stuff in the middle
		"*vs",
		"*versus",
		"*class with",
		"*class w", // class w/
		"*class at",
		"*class on",
		"*class in", // class in downtown palo alto
		"*classes with",
		"*classes w", // class w/
		"*classes at",
		"*classes on",
		"-^half day", // half day of classes
		"*$on display", // trains on display
		"*show with",
		"*dance with",
		"*dancing with",
		"^free dance", // free dance fusion class at ...
		"*dance at",
		"dancing lessons",
		"*art of", // the art of bill smith
		"*^art at", // art at the airport
		"*meeting at",
		"*meetings are", // meetings are from 7pm to 9pm
		"*$and greet", // meet and greet
		"$meet greet", // meet & greet
		"*$and mingle", // mix and mingle
		"*free lessons",
		"*lessons at", // free dancing lessons at
		"*dance for" // dance for the stars
	};

	// store these words into table
	static HashTableX s_twt;
	static char s_twtbuf[4000];
	static bool s_init10 = false;
	if ( ! s_init10 ) {
		s_init10 = true;
		s_twt.set(8,4,128,s_twtbuf,4000,false,0,"ti1tab");
		int32_t n = (int32_t)sizeof(s_twords)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_twords[i];
			// set words
			Words w; w.set3 ( s );
			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ ) {
				// fix "art of" = "of art"
				if (! wi[j] ) continue;
				h <<= 1LL;
				h ^=  wi[j];
			}
			// wtf?
			if ( h == 0LL ) { char *xx=NULL;*xx=0; }
			// . store hash of all words, value is ptr to it
			// . put all exact matches into sw1 and the substring
			//   matches into sw2
			if ( ! s_twt.addKey ( &h , &s ) ) return false;
		}
	}

	// store these words into table
	if ( ! s_init3 ) initGenericTable ( niceness );

	// scan words in [a,b) for a match. 
	// skip if in date or stop word

	// ok, now scan forward. the word can also be next to
	// strange punct like a colon like 
	// "Hebrew Conversation Class: Beginning" for dailylobo.com
	int32_t i = a;
	int64_t firstWid = 0LL;
	int64_t lastWid  = 0LL;
	bool hadAnnual = false;
	bool hadFeaturing = false;
	bool lastWordWasDate = false;
	bool negMatch = false;
	bool posMatch = false;
	bool oneWordMatch = false;
	bool hadAthon = false;
	for ( ; i < b ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if not alnum word
		if ( ! wids[i] ) continue;
		// first word?
		if ( ! firstWid ) firstWid = wids[i];
		// does it match a word in the table?
		char **sp = (char **)s_twt.getValue ( &wids[i] );
		// a match?
		if ( sp ) oneWordMatch = true;
		// or a two word phrase? even if we matched a one word
		// phrase, try for the two because it might be a negative!
		// i.e. "half open"
		if ( lastWid ) {
			int64_t combo = wids[i] ^ (lastWid<<1LL);
			char **sp2 = (char **)s_twt.getValue ( &combo );
			// if there use that! otherwise, leave sp alone
			if ( sp2 ) sp = sp2;
			if ( sp2 ) oneWordMatch = false;
		}
		// get next wid after us
		int64_t nextWid = 0LL;
		for ( int32_t k = i + 1 ; k < b ; k++ ) {
			QUICKPOLL(niceness);
			if ( ! wids[k] )  continue;
			nextWid = wids[k];
			break;
		}
		// "-getting there" to prevent "getting" from winning
		if ( nextWid ) {
			int64_t combo = (wids[i]<<1LL) ^ nextWid;
			char **sp2 = (char **)s_twt.getValue ( &combo );
			// if there use that! otherwise, leave sp alone
			if ( sp2 ) sp = sp2;
		}

		if ( wids[i] == h_annual ) hadAnnual = true;
		// must not be the first word or last word
		if ( wids[i] == h_featuring && i > a && i < b-1 ) 
			hadFeaturing = true;

		// any kind of athon, hackathon, etc. sitathon
		if ( wlens[i]>=8 &&
		     to_lower_a(wptrs[i][wlens[i]-5]) == 'a' &&
		     to_lower_a(wptrs[i][wlens[i]-4]) == 't' &&
		     to_lower_a(wptrs[i][wlens[i]-3]) == 'h' &&
		     to_lower_a(wptrs[i][wlens[i]-2]) == 'o' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 'n' )
			hadAthon = true;

		// any *fest too!! assfest
		if ( wlens[i]>=7 &&
		     to_lower_a(wptrs[i][wlens[i]-4]) == 'f' &&
		     to_lower_a(wptrs[i][wlens[i]-3]) == 'e' &&
		     to_lower_a(wptrs[i][wlens[i]-2]) == 's' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 't' )
			hadAthon = true;

		//if ( wids[i] == h_this &&
		//     i+2<nw && wids[i+2] == h_series )
		//	log("hey");

		// save it
		int64_t savedWid = lastWid;
		// assign
		lastWid = wids[i];
		// save this
		bool savedLastWordWasDate = lastWordWasDate;
		// and update
		lastWordWasDate = (bool)(bits && (bits[i] & D_IS_IN_DATE));
		// match?
		if ( ! sp ) continue;
		// get the char ptr
		char *pp = *sp;
		// or total like "total courses: xx. total sections: yy"
		if ( savedWid == h_total ) return -1;
		// . if prev word was "no" then return -1
		// . fix "no mass" "no class" etc.
		// . oneWordMatch fixes "Straight No Chaser" play title
		//   which has "no chaser" in the list above
		if ( savedWid == h_no && oneWordMatch ) return -1;
		// fix "past conferences"
		if ( savedWid == h_past && oneWordMatch ) return -1;
		// aynthing starting with no is generally bad...
		// like "no class" "no service", etc.
		if ( savedWid == h_no && firstWid == h_no &&
		     // UNLESS it's "no name" - like "the no name club"
		     lastWid != h_name ) 
			return -1;
		// return value is true by default
		bool *matchp = &posMatch;
		// . is it generic? "sesssions", etc. that means we need
		//   2+ alnum words, we can't have a title that is just 
		//   "sessions"
		// . if it is generic and we only have one word, forget it!
		if ( *pp == '*' && alnumCount == 1 && useAsterisk ) 
			continue;
		// skip asterisk
		if ( *pp == '*' ) 
			pp++;
		// is it negative. complement return value then
		if ( *pp == '-' ) { matchp = &negMatch; pp++; }
		// anywhere?
		if ( *pp != '$' && *pp != '^' && *pp != '|' ) {
			*matchp = 1;
			continue;
		}
		// the gerunds mismatch easily and therefore we require for
		// the match to be complete that we not be mixed case. fixes
		// "Looking forward to seeing you again"
		int32_t pplen = gbstrlen(pp);
		if ( pp[pplen-1]=='g' &&
		     pp[pplen-2]=='n' &&
		     pp[pplen-3]=='i' &&
		     ( sflags & SENT_MIXED_CASE ) )
			continue;
		// yes! must match first part of sentence
		if ( (*pp == '^' || *pp == '|' ) &&
		     ( wids[i] == firstWid ||
		       lastWid   == firstWid ) )
			*matchp = 1;
		// . or if a colon was right before us...
		// . Hacking+the+Planet:+Demystifying+the+Hacker+Space
		if ( (*pp == '^' || *pp == '|' ) && 
		     i>a &&
		     words->hasChar(i-1,':') )
			*matchp = 1;
		// or date right before us
		if ( (*pp == '^' || *pp == '|' ) && savedLastWordWasDate )
			*matchp = 1;
		// . this is always good (HACK)
		// . annual party / annual spring concert/annual nye afterparty
		if ( hadAnnual && *pp != '-' ) *matchp = 1;
		// keep chugging if must match first word in sentence
		if ( *pp == '^' ) continue;
		// stop if end of the line, that counts as well
		if ( i + 2 >= b ) { *matchp = 1; continue; }
		// tags are good
		if ( tids[i+1] ) *matchp = 1;
		// fix "...dance class,</strong>"
		if ( tids[i+2] ) *matchp = 1;
		// fix "workshop a" for aliconferences.com
		if ( i == b - 3 && wlens[b-1] == 1 )
			*matchp = 1;
		// Dance III..., Adult II...
		if ( i+2 < b && wptrs[i+2][0]=='I'&&wptrs[i+2][1] == 'I')
			*matchp = 1;
		// Dance I ...
		if ( i+2 < b && wlens[i+2]==1 && wptrs[i+2][0] == 'I' )
			*matchp = 1;
		// Ballet V ...
		if ( i+2 < b && wlens[i+2]==1 && wptrs[i+2][0] == 'V' )
			*matchp = 1;
		// Ballet VI ...
		if ( i+2 < b && wptrs[i+2][0] == 'V'&&wptrs[i+2][1]=='I')
			*matchp = 1;
		// hitting a date is ok... (TODO: test "... every saturday"
		if ( bits && (bits[i+2] & D_IS_IN_DATE) ) *matchp = 1;
		// a lot of times it does not treat the year as a date!
		// "Radio City Christmas Spectacular 2011"
		if ( i+2 < b && 
		     is_digit(wptrs[i+2][0]) &&
		     wlens[i+2]==4 &&
		     words->getAsLong(i+2) >= 2005 &&
		     words->getAsLong(i+2) <= 2040 )
			*matchp = 1;
		// the word for is ok? "training for grades ..."
		if ( wids[i+2] == h_for ) *matchp = 1;
		// how about "of"... symposium of science. celebration of ...
		//if ( wids[i+2] == h_of ) *matchp = 1;		
		// at following is ok. Tess+Gallagher+reading+at+Village+Book
		if ( wids[i+2] == h_at ) *matchp = 1;
		// music by the bay
		if ( wids[i+2] == h_by ) *matchp = 1;

		// "in" can be a delimeter if a place name follows it like
		// "the conference room" or "the Martin Building" ...
		// or "Zipperdome, 123 main street"
		// fix subsent:
		// ... Pre-Hearing Discussion in the Conference Room...
		// for legals.abqjournal.com/legals/show/273616
		// TODO: a city/state!!!
		if ( wids[i+2] == h_in ) {
			*matchp = 1;
			/*
			// assume none
			bool gotPlaceInd = false;
			// scan for place indicator or in address bit
			for ( int32_t j = i + 4 ; j < b ; j++ ) {
				if ( ! isPlaceIndicator(&wids[j]) ) continue;
				gotPlaceInd = true;
				break;
			}
			if ( gotPlaceInd ) *matchp = 1;
			*/
		}

		// put in subsent code 
		//if ( i+4<b && wids[i+2] == h_directed && wids[i+4] == h_by ) 
		//	*matchp = 1;
		// Mass on January 1
		if ( i+4<b && wids[i+2]==h_on && bits &&
		     (bits[i+4]&D_IS_IN_DATE) )
		     *matchp = 1;		
		// blah blah in the chapel / in the Park / symposium in Boise..
		if ( i+6<b && wids[i+2] == h_in )
			*matchp = 1;
		// beginning at
		if ( i+4<b && wids[i+2]==h_beginning && wids[i+4]==h_at )
			*matchp = 1;
		// begins at
		if ( i+4<b && wids[i+2]==h_begins && wids[i+4]==h_at )
			*matchp = 1;
		// club meets at
		if ( i+4<b && wids[i+2]==h_meets && wids[i+4]==h_at )
			*matchp = 1;
		// rehersal times, concert times, blah blah times
		if ( i+3 == b && wids[i+2]==h_times )
			*matchp = 1;
		// blah blah benefitting blah blah
		if ( wids[i+2] == h_benefitting ) *matchp = 1;
		if ( wids[i+2] == h_benefiting  ) *matchp = 1;
		// blah blah party sponsored by ...
		if ( i+4<b && wids[i+2] == h_sponsored ) *matchp = 1;
		if ( i+4<b && wids[i+2] == h_sponsered ) *matchp = 1;
		// blah blah party presented by ...
		if ( i+4<b && wids[i+2] == h_presented ) *matchp = 1;
		// a colon is good "Soapmaking: how to"
		if ( words->hasChar (i+1,':' ) && 
		     // watch out for field names
		     wids[i] != h_event &&
		     wids[i] != h_band )
			*matchp = 1;
		// likewise a hyphen "Class - All About Sleep Apnea"
		if ( words->hasChar (i+1,'-' ) && 
		     // watch out for field names
		     wids[i] != h_event &&
		     wids[i] != h_band )
			*matchp = 1;
		// or parens: Million+Dollar+Quartet+(Touring)
		if ( words->hasChar (i+1,'(' ) ) *matchp = 1;

		/*
		// strange punct follows?
		char *p    =     wptrs[i+1];
		char *pend = p + wlens[i+1];
		for ( ; p < pend ; p++ ) {
			QUICKPOLL(niceness);
			if ( *p != ':' ) continue;
			// . phone number not allowed after it!
			// . fix "Adult Services: 881-001" for unm.edu
			int32_t next = i + 1;
			for ( ; next < b && ! wids[next]; next++ );
			// no phone number, so allow this ending!
			if ( next >= b ) break;
			// phone number? if not, we're good!
			if ( ! isdigit(wptrs[next][0]) ) break;
			// crap, forget it... do not set ENDING bit
			p = pend-1;
		}
		// there is no strange punct after it, so this word is
		// not really the "last" word in this sentence
		if ( p >= pend ) continue;
		// we got some strange punct
		return retVal;
		*/
	}

	// return it if we got something
	if ( negMatch ) return -1;
	if ( posMatch ) return  1;
	if ( hadAthon ) return  1;

	// blah blah featuring blah blah
	if ( hadFeaturing ) return 1;// true;

	// . if it has quotes, with, at, @, *ing, "w/" set it
	// . if has mixed case do not do this loop
	if ( sflags & SENT_MIXED_CASE ) b = 0;
	int32_t hadNonDateWord = 0;
	int64_t lastWordId = 0LL;
	bool lastWordPastTense = false;
	// loop over all words in the title
	for ( i = a ; i < b ; i++ ) {
		QUICKPOLL(niceness);
		if ( tids[i] ) continue;
		// check for puncutation based title indicators
		if ( ! wids[i] ) {
			// MCL+Meet+@+Trego
			if ( words->hasChar(i,'@' ) &&
			     // fix "Sunday Oct 4 2pm at ..." for zvents.com
			     hadNonDateWord &&
			     // last word is not "tickets"!! fix zvents.com
			     lastWordId != h_tickets &&
			     // only for place names not tods like "@ 2pm"
			     i+1<b && ! is_digit(words->m_words[i+1][0]) )
				break;
			// "Chicago"
			if ( i>0 && 
			     ! tids[i-1] &&
			     ! wids[i-1] &&
			     words->hasChar(i-1,'\"') )
				break;
			// Event:+'Sign+Language+Interpreted+Mass'
			if ( i>0 && 
			     ! tids[i-1] &&
			     ! wids[i-1] &&
			     words->hasChar(i,'\'') ) 
				break;
			continue;
		}

		// blah blah with Tom Smith
		if ( i > a && 
		     i+2 < b &&
		     (wids[i] == h_with || wids[i] == h_starring) &&
		     // with id doesn't count
		     wids[i+2] != h_i &&
		     wids[i+2] != h_id &&
		     // experience with quickbooks
		     (i-2<a || wids[i-2] != h_experience) &&
		     // with purchase
		     wids[i+2] != h_purchase &&
		     // with $75 purchase
		     (i+4>=b || wids[i+4] != h_purchase) )
			break;
		// blah blah w/ Tom Smith
		if ( i > a && 
		     i+2 < b &&
		     wids[i] == h_w && 
		     i+1<b && 
		     words->hasChar(i+1,'/') &&
		     // with id doesn't count
		     wids[i+2] != h_i &&
		     wids[i+2] != h_id )
			break;
		// "Lotsa Fun at McCarthy's"
		if ( wids[i] == h_at &&
		     // fix "Sunday Oct 4 2pm at ..." for zvents.com
		     hadNonDateWord &&
		     // last word is not "tickets"!! fix zvents.com
		     lastWordId != h_tickets &&
		     // not look at.. take a look at the menu
		     lastWordId != h_look && 
		     // what's new at the farm
		     lastWordId != h_new &&
		     // fix "Events at Stone Brewing Co"
		     lastWordId != h_events &&
		     // search jobs at aria
		     lastWordId != h_jobs &&
		     // write us at ...
		     lastWordId != h_us &&
		     // at its best
		     (i+2>=b || wids[i+2] != h_its) &&
		     // she studied at the rcm
		     ! lastWordPastTense &&
		     // . lastword can't be a place indicator
		     // . Aquarium at Albuquerque ...
		     // . Museums at Harvard
		     ! s_pit.isInTable(&lastWordId) &&
		     // only for place names not tods like "at 2pm"
		     i+2<b && ! is_digit(words->m_words[i+2][0]) )
			break;

		bool isDateWord = (bool)(bits && (bits[i] & D_IS_IN_DATE));
		// mark this
		if ( ! isDateWord ) hadNonDateWord++;
		// save this
		lastWordId = wids[i];

		// assume not past tense
		lastWordPastTense = false;
		// is it past tense like "studied"?
		int32_t wlen = wlens[i];
		if ( to_lower_a(wptrs[i][wlen-1]) != 'd' ) continue;
		if ( to_lower_a(wptrs[i][wlen-2]) != 'e' ) continue;
		// exceptions: feed need
		if ( to_lower_a(wptrs[i][wlen-3]) == 'e' ) continue;
		// exceptions: ned zed bed
		if ( wlen == 3 ) continue;
		// probably a ton more exceptions must let's see how it does
		lastWordPastTense = true;
	}
	// i guess we had something nice...
	if ( i < b ) return 1; // true;

	// no match
	return 0;
}
/*
bool Sections::isPlaceOrBusinessWord ( int32_t i ) {

	isPlaceIndicator ( &wids[i] ) return true;
	if ( wids[i] == h_news ) return true;
	if ( wids[i] == h_network ) return true;

		// generally, allow all gerunds, anywhere in the title
		// but if a noun like association or center follows them
		// they do not count, they are describing a bldg or 
		// organization... or "news" or "club" "room" (dining room)
		// "co" "company" "llc" ...
}
*/

void Sections::setSentPrettyFlag ( Section *si ) {
	// int16_tcut
	sentflags_t sflags = si->m_sentFlags;

	// int16_tcut
	wbit_t *bits = m_bits->m_bits;

	static int64_t h_click;
	static int64_t h_here;
	static int64_t h_link;
	static int64_t h_the;
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		h_click = hash64n("click");
		h_here  = hash64n("here");
		h_the   = hash64n("the");
		h_link  = hash64n("link");
	}

	if ( ! m_alnumPosValid ) { char *xx=NULL;*xx=0; }

	// assume not pretty
	bool pretty = false;
	// . we want to record votes on pretty sentences regardless
	// . the idea is is that if a turk vote changes the event
	//   title then more sentences may be included as part of the
	//   event description without every having been voted on!
	bool periodEnds = (sflags & SENT_PERIOD_ENDS);
	// if enough mixed case words, allow it through anyway
	int32_t numAlnums = si->m_alnumPosB - si->m_alnumPosA + 1;
	if ( (sflags & SENT_MIXED_CASE) && numAlnums >= 6 &&
	     // watch out for urls like get?q=gbeventhash668419539....
	     !(sflags & SENT_HASNOSPACE) )
		periodEnds = true;
	// or if it has commas
	bool hasComma = false;
	for ( int32_t i = si->m_senta ; i < si->m_sentb ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not word
		if ( ! m_tids[i] ) continue;
		if (   m_wids[i] ) continue;
		if ( ! m_words->hasChar(i,',') ) continue;
		hasComma = true;
		break;
	}
	if ( hasComma && numAlnums >= 3 ) periodEnds = true;
	// if no period ends, nuke
	if ( periodEnds ) pretty = true;
	// BUT if its all generic words it is not pretty!
	int32_t lastWidPos = -1;
	// assume all generic words
	bool allGeneric = true;
	// is all generic words?
	for ( int32_t i = si->m_senta ; i < si->m_sentb ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not word
		if ( ! m_wids[i] ) continue;
		// ignore if it has "click here"
		if ( m_wids[i] == h_here && 
		     lastWidPos >= 0 &&
		     m_wids[lastWidPos] == h_click )
			break;
		// ignore if has "[click|follow] the link"
		if ( m_wids[i] == h_link && 
		     lastWidPos >= 0 &&
		     m_wids[lastWidPos] == h_the )
			break;
		// save it
		lastWidPos = i;
		// skip if generic like
		if ( bits[i] & (D_CRUFTY|
				  D_IS_NUM|
				  D_IS_HEX_NUM|
				  D_IN_PARENS|
				  D_IS_IN_DATE) )
			continue;
		// otherwise, not generic
		allGeneric = false;
		break;
	}
	// if all generic, not pretty
	if ( allGeneric ) pretty = false;
	// powered by is not pretty
	if ( sflags & SENT_POWERED_BY ) pretty = false;
	// all in link is not pretty
	if ( bits[si->m_senta] & D_IN_LINK ) pretty = false;
	// starts with "click" is bad "Click the map to drag around..."
	// or "Click the link to see more..."
	if ( m_wids[si->m_a] == h_click ) pretty = false;
	// set it
	if ( ! pretty ) return;
	// we are pretty
	si->m_sentFlags |= SENT_PRETTY;
}

/*
	////////////////////////////////
	//
	// punish sections that have a repeated tag hash
	//
	//
	// - the idea being title sections are somewhat unique
	// - limit repeat table recordings for each individual event
	//
	////////////////////////////////
	char rttbuf[4000];
	HashTableX rtt; 
	rtt.set(4,4,256,rttbuf,4000,false,m_niceness,"rttt");
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// insert into table, score one
		if ( ! rtt.addTerm32((int32_t *)&si->m_tagHash) ) return false;
	}
	// now punish the repeaters
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// insert into table, score one
		int32_t score = rtt.getScore32((int32_t *)&si->m_tagHash) ;
		// punish?
		if ( score > 1 ) continue;
		// yes
		si->m_sentFlags |= SENT_UNIQUE_TAG_HASH;
		si->m_titleScore *= 1.02;
	}


	/////////////
	//
	// title score boost for being part of title tag
	//
	// many event titles are repeated in the title tag
	// that may be true, but also the title is generic and repeated
	// again in the document body, and that hurts us!!
	//
	// TODO: consider doing this if only one event?
	//
	///////////
	// get the title tag section
	Section *titleSec = NULL;
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( si->m_tagId != TAG_TITLE ) continue;
		titleSec = si;
		break;
	}
	// . score each section that directly contains text.
	// . have a score for title and score for description
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// forget if no title
		if ( ! titleSec ) break;
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// get sentence
		int32_t senta = si->m_senta;
		int32_t sentb = si->m_sentb;
		// do not match title tag with itself!
		if ( senta >= titleSec->m_a && senta < titleSec->m_b ) 
			continue;
		// title sentence
		int32_t ta = titleSec->m_a;
		int32_t tb = titleSec->m_b;
		// record max words matched
		int32_t max = 0;
		bool allMatched = false;
		int32_t matchedWords = 0;
		// compare to sentence in title
		for ( int32_t i = ta ; i < tb ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum
			if ( ! m_wids[i] ) continue;
			// check with title
			if ( m_wids[i] != m_wids[senta] ) {
				// reset matched word count
				matchedWords = 0;
				// reset this ptr too
				senta = si->m_senta;
				continue;
			}
			// ok, words match, see how many
			matchedWords++;
			// record max
			if ( matchedWords > max ) max = matchedWords;
			// advance over word we match
			senta++;
			// advance to next alnumword
			for ( ; senta < sentb && !m_wids[senta] ; senta++);
			// all done?
			if ( senta < sentb ) continue;
			// all matched!
			allMatched = true;
			break;
		}
		if ( ! allMatched ) continue;
		// reward
		si->m_sentFlags |= SENT_MATCHED_TITLE_TAG;
		// give a little more the more words matched
		// no, because the title often has the containing place name, 
		// like "Museum of Science and Industry" even when the page
		// is talking about the "Bronzeville Blue Club" which will
		// get less points because its only match 3 words...
		//float minb = 2.0;
		//float maxb = 3.0;
		//if ( matchedWords > 10 ) matchedWords = 10;
		//float bonus = minb+(maxb-minb) * ((float)matchedWords/10.0);
		si->m_titleScore *= 3.0;//bonus;
		si->m_descScore  *= 3.0;//bonus;
	}
}
*/

#define METHOD_MONTH_PURE   0 // like "<h3>July</h3>"
#define METHOD_TAGID        1
#define METHOD_DOM          2
#define METHOD_ABOVE_DOM    3
#define METHOD_DOW          4
#define METHOD_DOM_PURE     5
#define METHOD_DOW_PURE     6
#define METHOD_ABOVE_DOW    7
#define METHOD_INNER_TAGID  8
#define METHOD_ABOVE_ADDR   9
#define METHOD_MAX          10
//#define METHOD_ABOVE_TOD    8 // dailylobo.com
//#define METHOD_ABOVE_DOW    6
//#define METHOD_ATTRIBUTE    2
//#define METHOD_BR_DOM       10 // like "<br>Starting July 31..."
//#define METHOD_EMPTY_TAG    8 // like "<p></p>", now also <br><br>

#define MAXCELLS (1024*5)

class Partition {
public:
	int32_t m_np;
	int32_t m_a[MAXCELLS];
	int32_t m_b[MAXCELLS];
	class Section *m_firstBro[MAXCELLS];
};

// . employ the existing methods (add METHOD_MONTH_PURE,EMPTY_TAG, etc.)
// . but in scoring the partition use the SENT_* bits
// . import SEC_HAS_DOM/TOD/etc. in the SENT_* bits for simplicity
// . hash every combination of a sentence's SENT_* bits with his neighbor
//   for making the scoring vector. or just hash the whole thing???
//   like just all the bits of him with all the sent flags with next sentence?
// . i kinda want to manually control the methods rather than resorting to
//   all combinations of sentence bits as possible delimeter hashes in order
//   to keep performance fast.

// . scan each list of brothers
// . identify if it contains dom/dow dates and tod dates in brothers
// . the scan each brother in the list
// . take all combinations of up to 3 sentence flag bits from a brother section
//   and use that as a delimeter
// . then evaluate the score of the resulting partition based on using that
//   delimeter.
// . example: the first brother has SENT_IN_HEADER and SENT_HAS_PRICE set
//   amongst other bits, so we try to use all brother sections in the list
//   that have both SENT_IN_HEADER and SENT_HAS_PRICE as delimeters. and we
//   score the partition based on that.
// . we score partitions by comparing each paritition cell to the others.
//   i.e. how similar are they in terms of the SENT_* bits they have set?
// . the idea is too create a partition that balances the SENT_* bits
//   so that each paritition has more or less the same SENT_* bits from all
//   the sections that it contains.
// . example: like each partitioned section has SENT_PRICE and SENT_TOD, ...
//   then that's pretty good.
// . we also hash all possible pairs of SENT_* flags between adjacent sentences
//   in the same paritioned section. these gives us ordering information.
// . like an SENT_MIXED_CASE sentence follows an SENT_IN_HEADER sentence would
//   result in one vector component of the vector for the partitioned cell.
//   and we compare vectors from each cell in the partition with all the other
//   cells in the partition to get the similarity score.
// . returns false with g_errno set on error
int32_t Sections::addImpliedSections3 ( ) {

	// . try skipping for xml
	// . eventbrite.com has a bunch of dates per event item and
	//   we end up using METHOD_DOM on those!
	// . i think the implied section algo is meant for html really
	//   or plain text
	if ( m_contentType == CT_XML &&
	     ( m_isEventBrite ||
	       m_isStubHub    ||
	       m_isFacebook ) )
		return 0;

		
	sec_t badFlags =SEC_MARQUEE|SEC_STYLE|SEC_SCRIPT|SEC_SELECT|
		SEC_HIDDEN|SEC_NOSCRIPT;

	// for debugging -- sleep forever
	//if ( g_hostdb.m_hostId == 44 ) {
	//	for ( ; ; ) {
	//		QUICKPOLL(m_niceness);
	//	}
	//}

	// ignore brothers that are one of these tagids
	//sec_t badFlags = SEC_SELECT|SEC_SCRIPT|SEC_STYLE|SEC_HIDDEN;

	bool addedPartition = false;

	// scan the sections
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be the first brother in a list of brothers
		if ( sk->m_prevBrother ) continue;
		// must have a brother after him
		if ( ! sk->m_nextBrother ) continue;
		// all least 3 brothers to make implied sections useful
		if ( ! sk->m_nextBrother->m_nextBrother ) continue;

		// use this to pass less args to functions then
		//m_totalHdrCount = 0;

		// tally some stats for thist list of brothers
		int32_t count1 = 0;
		int32_t count2 = 0;
		//int32_t bothCount = 0;
		// scan the brothers now
		Section *bro = sk;
		// until the end of the list
		for ( ; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// now at least two brothers must have the 
			// SEC_HAS_DOM/DOW set and at least two other brothers
			// must have SEC_HAS_TOD set
			sec_t flags = bro->m_flags;
			// skip bad ones
			if ( flags & badFlags ) continue;
			// might also have tod set like folkmads.org does for 
			// one event title
			if ( flags & (SEC_HAS_DOM|SEC_HAS_DOW) )
				count1++;
			// but for tod, must only just be tod i guess???
			if ( flags & SEC_HAS_TOD )
				count2++;
			// how many headers with dom/dow dates do we have?
			// like "Jan 23, 2010" or "Wednesdays"
			//if ( ((flags & SEC_HEADING_CONTAINER) ||
			//      (flags & SEC_HEADING          ) ) &&
			//     ( flags & (SEC_HAS_DOM|SEC_HAS_DOW) ) )
			//	m_totalHdrCount++;
			//if ( (flags & (SEC_HAS_DOM|SEC_HAS_DOW) ) &&
			//     ( flags & SEC_HAS_TOD ) )
			//	bothCount++;
		}
		// . skip this list of brothers if we did not make the cut
		// . i lowered to one to re-fix gwair.org, cabq.gov, etc.
		//   because i fixed the SEC_HAS_DOM/DOW/TOD for implied 
		//   sections so it is actually accurate and not inheriting
		//   these flags from its parent. but to keep things fast
		//   we still have this constraint.
		if ( count1 < 1 || count2 < 1 ) continue;

		// can't all be both. the whole point in the partition is
		// to group tods with doms/dows.
		// this was hurting blackbirdbuvette.com which needed the
		// hr partition to split up some sections so it got
		// "Geeks Who Drink" as the title because it would no longer
		// have the multevent penalty!
		//if ( count1 + count2 == 2 * bothCount ) continue;

		// hmmm... two brothers need to have dates. see what this hurts
		if ( count1 < 2 && count2 < 2 ) continue;

		// reset the cache
		char cbuf[2048];
		m_ct.set (4,0,32,cbuf , 2048 , false,m_niceness,"sect-cache");

		// set this partition info for each method:
		Partition parts[METHOD_MAX];

		// reset these
		int32_t bestMethodScore[METHOD_MAX];
		memset ( bestMethodScore , 0 , METHOD_MAX * 4 );

		//
		// ok, now we made the cut, so scan list of brothers again
		// for potential partition tags. then get score of each
		// partition and prefer the highest scoring one.
		//
		// reset scan
		//bro = sk;
		// assume no winner
		int32_t       bestScore  = 0;
		Section   *bestBro    = NULL;
		char       bestMethod = -1;
		Partition *bestPart   = NULL;
		// loop over all enumerated methods
		for ( int32_t m = 0 ; m < METHOD_MAX ; m++ ) {
			// try skipping these
			if ( m == METHOD_TAGID ) continue;
			if ( m == METHOD_INNER_TAGID ) continue;
			// breathe
			QUICKPOLL ( m_niceness );
			// reset for each method
			bro = sk;
			// until the end of the list
			for ( ; bro ; bro = bro->m_nextBrother ) {
				// breathe
				QUICKPOLL ( m_niceness );
				// skip bad ones
				if ( bro->m_flags & badFlags ) continue;
				// grab next partition
				Partition tmpp;
				// . try each tagid once i guess
				// . returns -1 if we already did it!
				int32_t score = getDelimScore(sk,m,bro,&tmpp);
				// error? g_errno should be set
				if ( score == -3 ) return -1;
				// skip if not legit
				if ( score == -2 ) {
					// this happens we we have a TON
					// of brothers!! so give up on them!
					// not it happens when bro tag type
					// is not compatible with method.
					// like for santafeplayhouse.org!
					// for sections around the performance
					// date lines.
					continue;
					//break;
				}
				// strange?
				if ( score == -1 ) continue;
				// score now based on where you start
				score = 1000000 - bro->m_a;
				// is it best score for this method?
				if ( score <= bestMethodScore[m] ) continue;
				// store it
				bestMethodScore[m] = score;
				// if so, store it for this method
				parts[m].m_np = tmpp.m_np;
				for ( int32_t i = 0 ; i < tmpp.m_np ; i++ ) {
					// rbeathe
					QUICKPOLL(m_niceness);
					parts[m].m_a[i] = tmpp.m_a[i];
					parts[m].m_b[i] = tmpp.m_b[i];
					parts[m].m_firstBro[i] = 
						tmpp.m_firstBro[i];
				}
				// skip if not best overall
				if ( score <= bestScore ) continue;
				// is best of all methods so far?
				bestScore  = score;
				bestBro    = bro;
				bestMethod = m;
				bestPart   = &parts[m];
			}
		}

		// if this list had no viable partitions, skip it
		if ( bestMethod == -1 ) continue;

		/*
		Partition *bestSuper = NULL;
		int32_t       bestSuperMethod;
		// . before inserting the winning partition see if another
		//   partition is a "super partition" of that and insert that
		//   first if it is. 
		// . a lot of times the super partition as a smaller score
		//   because one section in the partition is much larger
		//   than another, possibly empty, section...
		// . prefer the super partition with the lest # of cells
		for ( int32_t m = 0 ; m < METHOD_MAX ; m++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if winner
			if ( m == bestMethod ) continue;
			// if no partition here, skip it
			if ( bestMethodScore[m] <= 0 ) continue;
			// shorcut
			Partition *super = &parts[m];
			Partition *sub   =  bestPart;
			// need at least one partition
			if ( super->m_np <= 1 ) continue;
			// skip if same number of partitions or more
			if ( super->m_np >= sub->m_np ) continue;
			// must start ABOVE first partition
			if ( super->m_a[0] >= sub->m_a[0] ) continue;
			// assume it is a super partition
			bool isSuper = true;
			// w is cursor into the subpartitions cells/intervals
			int32_t w = 0;
			// now every startpoint in the super partition must
			// be right before a point in the subpartition
			int32_t k; for ( k = 0 ; k < super->m_np ; k++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// get last partition if we have a sequence
				// of empties... like if the super partition
				// was <h1> tags and the sub partition was
				// <h2> if we had:
				// <h1>..</h1>
				// <h1>..</h1>
				// <h2>..</h2>
				// <h1>..</h1>
				// <h2>..</h2>
				// then that first two adjacent <h1> tags
				// should not stop the <h1> tags from 
				// constituting a super partition over the
				// subpartition of <h2> tags
				if ( k+1 < super->m_np &&
				     super->m_firstBro[k  ]->m_alnumPosB == 
				     super->m_firstBro[k+1]->m_alnumPosA )
					continue;
				// advance "w" is need to catch up
				for ( ; w < sub->m_np ; w++ )
					if ( sub->m_a[w] >= super->m_a[k] )
						break;
				// if w is exhausted we are done
				if ( w >= sub->m_np ) break;
				// . now compare to next guy in subpartition
				// . the first bros must be adjacent
				// . i.e. <h1> tag must be adjacent to <h2> tag
				if ( super->m_firstBro[k]->m_alnumPosB ==
				     sub  ->m_firstBro[w]->m_alnumPosA )
					continue;
				// crap, not a match, not a super partition
				isSuper = false;
				break;
			}
			// skip if not a super partition
			if ( ! isSuper ) continue;
			// the best super will have the fewest partitions
			if ( bestSuper && super->m_np >= bestSuper->m_np ) 
				continue;
			// we are now the best super partition
		        bestSuper       = super;
			bestSuperMethod = m;
		}
		// turn off for now
		//bestSuper = NULL;
		*/
				
		// select the winning partition
	        int32_t       winnerMethod = bestMethod;
		Partition *winnerPart   = bestPart;
		// if no super paritition...
		//if ( bestSuper ) {
		//	winnerMethod = bestSuperMethod;
		//	winnerPart   = bestSuper;
		//}


		// log it
		char *ms = "";
		if ( winnerMethod == METHOD_TAGID       ) ms = "tagid";
		if ( winnerMethod == METHOD_DOM         ) ms = "dom";
		if ( winnerMethod == METHOD_DOM_PURE    ) ms = "dompure";
		if ( winnerMethod == METHOD_ABOVE_DOM   ) ms = "abovedom";
		if ( winnerMethod == METHOD_DOW         ) ms = "dow";
		if ( winnerMethod == METHOD_DOW_PURE    ) ms = "dowpure";
		if ( winnerMethod == METHOD_MONTH_PURE  ) ms = "monthpure";
		if ( winnerMethod == METHOD_ABOVE_DOW   ) ms = "abovedow";
		if ( winnerMethod == METHOD_INNER_TAGID ) ms = "innertagid";
		if ( winnerMethod == METHOD_ABOVE_ADDR ) ms = "aboveaddr";
		//if ( winnerMethod == METHOD_ABOVE_TOD   ) ms = "abovetod";
		//if ( winnerMethod == METHOD_EMPTY_TAG   ) ms = "emptytag";
		//if ( winnerMethod == METHOD_ATTRIBUTE ) ms = "attribute";
		log("sections: inserting winner method=%s",ms);

		// loop over his paritions and insert each one
		for ( int32_t i = 0 ; i < winnerPart->m_np ; i++ ) {
			// get partition
			int32_t a = winnerPart->m_a[i];
			int32_t b = winnerPart->m_b[i];
			// if it is a container around another container
			// then it is pointeless
			Section *cc = m_sectionPtrs[a];
			if ( cc && cc->m_a == a && cc->m_b == b ) continue;
			// this returns false and sets g_errno on error
			if ( ! insertSubSection( sk->m_parent ,
						 winnerPart->m_a[i],
						 winnerPart->m_b[i],
						 BH_IMPLIED ) )
				return -1;
		}

		// ok, flag it
		addedPartition = true;
	}

	// return now if added no parititions
	if ( ! addedPartition ) return 0;

	// we gotta fix m_nextBrother,m_prevBrother after doing this
	setNextBrotherPtrs ( false );
		
	return 1;
}

// . vec? now has dups!
float computeSimilarity2 ( int32_t   *vec0 , 
			  int32_t   *vec1 ,
			  int32_t   *s0   , // corresponding scores vector
			  int32_t   *s1   , // corresponding scores vector
			  int32_t    niceness ,
			  SafeBuf *pbuf ,
			  HashTableX *labelTable ,
			  int32_t nv0 ,
			  int32_t nv1 ) {
	// if both empty, assume not similar at all
	if ( *vec0 == 0 && *vec1 == 0 ) return 0;
	// if either is empty, return 0 to be on the safe side
	if ( *vec0 == 0 ) return 0;
	if ( *vec1 == 0 ) return 0;

	HashTableX ht;
	char  hbuf[200000];
	int32_t  phsize = 200000;
	char *phbuf  = hbuf;
	// how many slots to allocate initially?
	int32_t need = 1024;
	if ( nv0 > 0 ) need = nv0 * 4;
	// do not use the buf on stack if need more...
	if ( need > 16384 ) { phbuf = NULL; phsize = 0; }
	// allow dups!
	if ( ! ht.set ( 4,4,need,phbuf,phsize,true,niceness,"xmlqvtbl2"))
		return -1;

	// for deduping labels
	HashTableX dedupLabels;
	if ( labelTable ) 
		dedupLabels.set(4,4,need,NULL,0,false,niceness,"xmldelab");

	bool useScores  = (bool)s0;

	int32_t matches    = 0;
	int32_t total      = 0;

	int32_t matchScore = 0;
	int32_t totalScore = 0;

	// hash first vector. accumulating score total and total count
	for ( int32_t *p = vec0; *p ; p++ , s0++ ) {
		// count it
		total++;
		// get it
		int32_t score = 1;
		// get the score if valid
		if ( useScores ) score = *s0;
		// total it up
		totalScore += score;
		// accumulate all the scores into this one bucket
		// in the case of p being a dup
		if ( ! ht.addTerm32 ( p , score ) ) return -1;
	}

	//int32_t zero = 0;

	// see what components of this vector match
	for ( int32_t *p = vec1; *p ; p++ , s1++ ) {
		// count it
		total++;
		// get it
		int32_t score = 1;
		// get the score if valid
		if ( useScores ) score = *s1;
		// and total scores
		totalScore += score;
		// is it in there?
		int32_t slot = ht.getSlot ( p );
		// skip if unmatched
		if ( slot < 0 ) {
			// skip if not debugging
			if ( ! pbuf ) continue;
			// get the label ptr
			char *pptr = (char *)labelTable->getValue((int32_t *)p);
			// record label in safeBuf if provided
			dedupLabels.addTerm32 ( (int32_t *)&pptr , score );
			//dedupLabels.addTerm32 ( (int32_t *)p );
			// and then skip
			continue;
		}

		// otherwise, it is a match!
		matches++;

		// and score of what we matched
		int32_t *val = (int32_t *)ht.getValueFromSlot ( slot );
		// sanity check. does "vec1" have dups in it? shouldn't...
		if ( *val == 0 ) { char *xx=NULL;*xx=0; }
		// we get the min
		int32_t minScore ;
		if ( *val < score ) minScore = *val;
		else                minScore = score;

		// only matched "min" of them times 2!
		matchScore += 2 * minScore;
		// he is hit too
		//matchScore += *val;
		// how many were unmatched?
		int32_t unmatched = *val + score - (2*minScore);

		// remove it as we match it to deal with dups
		// once we match it once, do not match again, score was
		// already accumulated
		// otherwise, remove this dup and try to match any
		// remaining dups in the table
		ht.setValue ( slot , &unmatched ); // &zero
		//ht.removeSlot ( slot );
	}

	// for debug add all remaining into dedup table
	for ( int32_t i = 0 ; labelTable && i < ht.getNumSlots(); i++ ) {
		QUICKPOLL(niceness);
		if ( ! ht.m_flags[i] ) continue;
		uint32_t *unmatched = (uint32_t *)ht.getValueFromSlot (i);
		if ( *unmatched == 0 ) continue;
		// use the key to get the label ptr
		int32_t key = *(int32_t *)ht.getKeyFromSlot(i);
		char *pptr = (char *)labelTable->getValue(&key);
		dedupLabels.addTerm32 ( (int32_t *)&pptr , *unmatched );
	}


	// if after subtracting query terms we got no hits, return 0.framesets?
	if ( useScores && totalScore == 0 ) return 0;
	if ( total                   == 0 ) return 0;
	// . what is the max possible score we coulda had?
	// . subtract the vector components that matched a query term
	float percent = 100 * (float)matchScore / (float)totalScore;
	// sanity
	if ( percent > 100 ) { char *xx=NULL;*xx=0; }

	if ( ! labelTable ) return percent;

	// scan label table for labels
	for ( int32_t i = 0 ; i < dedupLabels.getNumSlots(); i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip empties
		if ( ! dedupLabels.m_flags[i] ) continue;
		// get hash
		char **pptr = (char **)dedupLabels.getKeyFromSlot(i);
		// and count
		int32_t count = dedupLabels.getScoreFromSlot(i);
		// get label and count
		char *str = *pptr;//(char *)labelTable->getValue(&h);
		if ( count != 1 ) pbuf->safePrintf ( "%s(%"INT32") ",str,count);
		else              pbuf->safePrintf ( "%s ",str);
	}

	return percent;
}

// . returns -1 if already got score for the suggested delimeter
// . returns -2 if suggested section cannot be a delimeter for the
//   suggested method
// . returns -3 with g_errno set on error
// . returns  0 if no viable partition exists which has a domdow/tod pair
//   in one interval of the partition and a domdow/tod pair in another
//   interval of the partition
// . otherwise returns a postive score of the strength of the partition
// . assumes all sections with the same "getDelimHash() as "delim" are the
//   first section in a particular partition cell
int32_t Sections::getDelimScore ( Section *bro , 
			       char method , 
			       Section *delim ,
			       Partition *part ) {

	// save it
	Section *start = bro;

	int32_t dh = getDelimHash ( method , delim , start );

	// bro must be certain type for some methods
	if ( dh == -1 ) return -2;

	// did we already do this dh?
	if ( m_ct.isInTable ( &dh ) ) return -1;

	// ignore brothers that are one of these tagids
	sec_t badFlags = SEC_SELECT|SEC_SCRIPT|SEC_STYLE|SEC_HIDDEN;

	// get the containing section
	Section *container = bro->m_parent;
	// sanity check... should all be brothers (same parent)
	if ( delim->m_parent != container ) { char *xx=NULL;*xx=0; }

	// the head section of a particular partition's section
	Section *currDelim = bro;
	// scores
	int32_t brosWithWords = 0;
	int32_t maxBrosWithWords = 0;
	int32_t bonus1    = 0;
	int32_t bonus2    = 0;
	int32_t bonus3    = 0;
#define MAX_COMPONENTS 15000
	int32_t pva[MAX_COMPONENTS];
	int32_t pvb[MAX_COMPONENTS];
	int32_t sva[MAX_COMPONENTS];
	int32_t svb[MAX_COMPONENTS];
	int32_t  nva  = 0;
	int32_t  nvb  = 0;
	pva[0] = 0;
	pvb[0] = 0;
	int32_t *pvec    = NULL;
	int32_t *pnum    = NULL;
	int32_t *pscore  = NULL;
	bool  firstDelim = true;
	float simTotal = 0;
	float minSim = 101.0;
	int32_t mina1 = -2;
	int32_t mina2 = -2;
	int32_t minTotalComponents=0;
	Section *prevPrevStart = NULL;
	Section *prevStart = NULL;
	int32_t  simCount = 0;
	int32_t inserts = 0;
	int32_t skips = 0;

	// no longer allow dups, keep a count of each hash now
	char vhtbuf[92000];
	HashTableX vht;
	vht.set ( 4, 4 ,256,vhtbuf,92000,false,m_niceness,"vhttab");
	int32_t cellCount = 0;
	SafeBuf minBuf;

	HashTableX labels;
	labels.set ( 4,65,20000,NULL,0,false,m_niceness,"lbldbug");
	HashTableX *dbt = NULL;
	SafeBuf sb;
	SafeBuf *pbuf = NULL;

	//
	// assign for debug DEBUG DEBUG implied sections
	//
	//dbt = &labels;
	//pbuf = &sb;

	int32_t np = 0;
	int32_t nonDelims = 0;

	bool ignoreAbove = true;
	// if delimeter is like an hr tag without text in it, then do include
	// the stuff above its first occurence as part of the partition, 
	// otherwise, assume the stuff above the first occurence of "delim"
	// is just header junk and should not be included. fixes 
	// dailylobo.com which has an "<h2>" header to the brother list
	// which should not be part of the partition we use.
	//if ( delim->m_firstWordPos < 0 ) ignoreAbove = false;

	// reset prev sentence
	Section *prevSent = NULL;
	Section *lastBro  = NULL;

	// scan the brothers
	for ( ; ; bro = bro->m_nextBrother ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if bad
		if ( bro && (bro->m_flags & badFlags) ) continue;

		// if any brother is an implied section, stop!
		if ( bro && (bro->m_baseHash == BH_IMPLIED ) ) return -2;

		// get its hash
		int32_t h = 0LL ;
		if ( bro ) h = getDelimHash ( method , bro , start );

		// . check this out
		// . don't return 0 because we make a vector of these hashes
		//   and computeSimilarity() assumes vectors are NULL term'd
		if ( bro && h == 0 ) { char *xx=NULL;*xx=0; }

		// once we hit the delimeter we stop ignoring
		if ( h == dh ) ignoreAbove = false;

		// if first time, ignore crap above the first delimeter occurnc
		if ( ignoreAbove ) continue;

		// count the section delimeter itself in this if not firstTime,
		// and then we need two sections after that...?
		//int32_t need = 3;
		// i guess the first time is reduced since it started
		// the secCount at 0, and normally it starts at 1 (see below)
		// but if we have like an hr tag delimeter then we only
		// need two sections above it. this fixed cabq.gov so it
		// got the first implied section and no longer missed it.
		//if ( firstTime ) need = 2;

		// update this for insertSubSection()
		lastBro = bro;

		if ( h == dh ) 
			currDelim = bro;

		// count non delimeter sections. at least one section
		// must have text and not be a delimeter section
		if ( h != dh && bro && bro->m_firstWordPos >= 0 )
			nonDelims++;

		// start a new partition?
		if ( h == dh ) {
			// start new one
			part->m_a        [np] = bro->m_a;
			part->m_b        [np] = bro->m_b;
			part->m_firstBro [np] = bro;
			np++;
			part->m_np = np;
			// if out of buffer space, note it and just do not
			// do this partition
			if ( np >= MAXCELLS ) {
				log("sec: partition too big!!!");
				return -2;
			}
		}
		// always extend current partition
		else if ( np > 0 && bro ) {
			part->m_b[np-1] = bro->m_b;
		}

			
		// did we finalize a cell in the paritition?
		bool getSimilarity = false;
		// if we hit a delimiting brothers, calculate the similarity
		// of the previous brothers
		if ( h == dh ) getSimilarity = true;
		// if we end the list of brothers...
		if ( ! bro   ) getSimilarity = true;
		// empty partition?
		if ( vht.isTableEmpty() ) getSimilarity = false;
		// turn off for now
		//mdwmdwgetSimilarity = false;

		// only need this for tag based method now
		//if ( method != METHOD_TAGID ) getSimilarity = false;

		// convert our hashtable into a vector and compare to
		// vector of previous parition cell if we hit a delimeter
		// section or have overrun the list (bro == NULL)
		if ( getSimilarity ) {

			// if we have been hashing sentences in the previous
			// brothers, then hash the last sentence as a previous
			// sentence to a NULL sentence after it.
			// as a kind of boundary thing. i.e. "*last* sentence 
			// is in header tag, etc." just like how we hash the 
			// first sentence with "NULL" as the previous sentence.
			if (!hashSentBits(NULL,&vht,container,0,dbt,NULL))
				return -3;

			if (!hashSentPairs(prevSent,NULL,&vht,container,dbt))
				return -3;

			// reset this since it is talking about sentences
			// just in the partition cell
			prevSent = NULL;
			// inc this for flip flopping which vector we use
			cellCount++;
			// what vector was used last?
			if ( (cellCount & 0x01) == 0x00 ) {
				pvec    = pvb;
				pnum    = &nvb;
				pscore  = svb;
			}
			else {
				pvec    = pva;
				pnum    = &nva;
				pscore  = sva;
			}
			// reset vector size
			*pnum = 0;
			// convert vht to vector
			for ( int32_t i = 0 ; i < vht.m_numSlots ; i++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip if empty
				if ( ! vht.m_flags[i] ) continue;
				// add it otherwise
				pvec[*pnum] = *(int32_t *)(&vht.m_keys[i*4]);
				// add score
				pscore[*pnum] = *(int32_t *)(&vht.m_vals[i*4]);
				// sanity
				if ( pscore[*pnum] <= 0){char *xx=NULL;*xx=0;}
				// inc component count
				*pnum = *pnum + 1;
				// how is this?
				if(*pnum>=MAX_COMPONENTS){char *xx=NULL;*xx=0;}
			}
			// null temrinate
			pvec[*pnum] = 0;
			// . this func is defined in XmlDoc.cpp
			// . vec0 is last partitions vector of delim hashes
			// . this allows us to see if each section in our
			//   partition consists of the same sequence of
			//   section "types" 
			// . TODO: just compare pva to pvb and vice versa and
			//   then take the best one of those two compares!
			//   that way one can be a SUBSET of the other and
			//   we can get a 100% "sim"
			float sim = computeSimilarity2( pva  ,
							pvb  ,
							sva  , // scores
							svb , // scores
							m_niceness ,
							pbuf ,
							dbt ,
							nva ,
							nvb );
			// add up all sims
			if ( cellCount >= 2 ) { // ! firstTime ) {
				simTotal += sim;
				simCount++;
			}
			if ( cellCount >= 2 && sim < minSim ) {
				minSim = sim;
				if ( prevPrevStart ) mina1=prevPrevStart->m_a;
				else                 mina1  = -1;
				if ( prevStart ) mina2  = prevStart->m_a;
				else             mina2  = -1;
				minTotalComponents = nva + nvb;
				// copy to our buf then
				if ( pbuf ) 
					minBuf.safeMemcpy ( pbuf );
			}
			// a new head
			//currDelim = bro;
			// reset vht for next partition cell to call
			// hashSentenceBits() into
			vht.clear();
		}

		if      ( h == dh && brosWithWords >= 1 && ! firstDelim )
			inserts++;
		else if ( h == dh && ! firstDelim )
			skips++;
		else if ( ! bro )
			inserts++;

		// sometimes we have a couple of back to back lines
		// that are like "M-F 8-5\n" and "Saturdays 8-6" and we do not
		// want them to make implied sections because it would
		// split them up wierd like for unm.edu.
		// unm.edu had 3 sentences:
		// "9 am. - 6 pm. Mon. - Sat.\n"
		// "Thur. 9 am. - 7 pm. Sun. 10 am - 4 pm.\n"
		// "Books, Furniture, Toys, TV's, Jewelry, Household Items\n"
		// and we were making an implied section around the last
		// two sentences, which messed everything up.
		// so let's add this code here to fix that.
		if ( h == dh && 
		     // this means basically back-to-back delimeters
		     brosWithWords <= 1 && 
		     // if we got a timeofday that is indicative of a schedule
		     (bro->m_flags & SEC_HAS_TOD) &&
		     ! firstDelim )
			return -2;

		// reset some stuff
		if ( h == dh ) {
			firstDelim = false;
			brosWithWords = 0;
			prevPrevStart = prevStart;
			prevStart = bro;
		}

		// . count sections.
		// . now we only count if they have text to avoid pointless
		//   implied sections for folkmads.org
		// . do not count delimeter sections towards this since
		//   delimeter sections START a partition cell
		if ( bro && bro->m_firstWordPos >= 0 )
			brosWithWords++;

		// keep a max on # of brothers with words in a given
		// partition cell. if all have just one such section
		// then no need to paritition at all!
		if ( brosWithWords > maxBrosWithWords )
			maxBrosWithWords = brosWithWords;

		//if ( h == dh && delim->m_a==525 && bro->m_a>=521 &&
		//     container->m_a == 521 &&
		//     method == METHOD_ABOVE_DOM ) //ATTRIBUTE )
		//	log("hey");
		//if ( h == dh && 
		//     delim->m_a==657 && 
		//     //bro->m_a>=521 &&
		//     container->m_a == 655 &&
		//     method == METHOD_INNER_TAGID )
		//	log("hey");

		// scan all sentences in this section and use them to
		// make a vector which we store in the hashtable "vht"
		for ( Section *sx=bro; sx &&sx->m_a<bro->m_b;sx = sx->m_next) {
			// mdwmdw break;
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not sentence
			if ( ! (sx->m_flags & SEC_SENTENCE) ) continue;
			// . store its vector components into hashtable
			// . store tagids preceeding sentence
			// . store tagid pairs preceeding sentence
			// . store SENT_* flags
			// . store SENT_* flags with prev sent SENT_* flags
			// . store hash of first word? last word?
			// . store SEC_HAS_MONTH/TOD/DOM/...
			// . should particular singles or pairs be weighted
			//   more than normal when doing the final compare
			//   between two adjacent implied sections in
			//   the partition?
			if (!hashSentBits(sx  ,&vht,container,0,dbt,NULL))
				return -3;
			if (!hashSentPairs(prevSent,sx,&vht,container,dbt))
				return -3;
			// set this
			prevSent = sx;
		}
		// stop when brother list exhausted
		if ( ! bro ) break;
	}

	// cache it
	if ( ! m_ct.addKey ( &dh ) ) return -3;

	//if ( last != currDelim && brosWithWords >= 2 )
	//	inserts++;
	//else if ( last != currDelim )
	//	skips++;

	// if no winners mdwmdw
	if ( minSim > 100.0 ) return -2;

	if ( maxBrosWithWords <= 1 ) return -2;

	if ( inserts <= 1 ) return -2;

	// at least one brother must NOT be a delimeter section and have text
	if ( nonDelims <= 0 ) return -2;

	// . empty tags always win
	// . this should not hurt anything...
	// . do this AFTER we've inserted all the big sections because it
	//   hurts large partitions like for "AUGUST" for cabq.gov/museums...
	//   because that needs to be in one big section
	// if ( method == METHOD_EMPTY_TAG ) minSim = 200.0;

	//if ( minSim < 40.0 ) return -2;

	//if ( minSim < 60.0 && inserts == 2 ) return -2;

	// . METHOD_TAGID has to be really strong to work, since tags are
	//   so flaky in general
	// . dailylobo.com depends on this but shold probably use a
	//   METHOD_ABOVE_TOD_PURE or something or do hr splits before
	//   adding implied sections?
	// . this should also fix santafeplayhouse.org from adding tagid
	//   based implied sections that are screwing it up by grouping its
	//   tods up with the address below rather than the address above,
	//   since the address below is already claimed by the 6pm parking time
	if ( minSim < 80.00 && method == METHOD_TAGID ) return -2;

	if ( minSim < 80.00 && method == METHOD_INNER_TAGID ) return -2;

	// special case. require at least two DOM sections for this method.
	// this fixes 
	// www.trumba.com/calendars/albuquerque-area-events-calendar.rss
	// where we were splitting its <description> tag for the win your
	// own home raffle so that the date of the event got into the same
	// section as a location of where the home you could win was, instead
	// of being in the same implied section as the event address. now
	// that i added this contraint we once again partition by the double
	// br tags so it works right now. we needed the METHOD_BR_DOM to fix
	// www.guysndollsllc.com/page5/page4/page4.html which was not 
	// splitting well with the METHOD_DOM i guess because "skips" was
	// too high? because some sentences contained just the dom and the
	// event description and perhaps were seen as contentless partitions??
	// perhaps we could also fix METHOD_DOM to deal with that rather than
	// add this new method, METHOD_BR_DOM
	// MDW: see if trumba still works without this!
	//if ( method == METHOD_BR_DOM && inserts <= 2 ) 
	//	return -2;

	// add the last part of the list
	//if ( partition && last != currDelim && brosWithWords >= 2 )
	//	// if inserting implied sections, add the previous one
	//	if ( ! insertSubSection ( currDelim->m_parent          ,
	//				  currDelim->m_a               ,
	//				  last->m_b , // end
	//				  BH_IMPLIED              ))
	//		// return -3 with g_errno set on error
	//		return -3;

	// return -2 if no applicable partition of two or more intervals
	//if ( inserts <= 1 ) return -2;

	// super bonus if all 100% (exactly the same)
	float avgSim = simTotal / simCount;

	// use this now
	// . very interesting: it only slighly insignificantly changes
	//   one url, graypanthers, if we use the avgSim vs. the minSim.
	// . i prefer minSim because it does not give an advantage to 
	//   many smaller sections vs. fewer larger sections in partition.
	// . later we should probably consider doing a larger partition first
	//   then paritioning those larger sections further. like looking 
	//   ahead a move in a chess game. should better partition 
	//   santafeplayhouse.org methinks this way
	//bonus1 = (int32_t)(avgSim * 100);
	bonus1 = (int32_t)(minSim * 1000);

	// i added the "* 2" to fix unm.edu because it was starting each
	// section the the <table> tag section but had a high badCount,
	// so this is kind of a hack...
	//int32_t total = goodCount * 10000 - badCount * 2 * 9000;
	int32_t total = 0;
	total += bonus1;

	// debug output
	char *ms = "";
	if ( method == METHOD_TAGID      ) ms = "tagid";
	if ( method == METHOD_DOM        ) ms = "dom";
	if ( method == METHOD_DOM_PURE   ) ms = "dompure";
	if ( method == METHOD_ABOVE_DOM  ) ms = "abovedom";
	if ( method == METHOD_DOW        ) ms = "dow";
	if ( method == METHOD_DOW_PURE   ) ms = "dowpure";
	if ( method == METHOD_MONTH_PURE ) ms = "monthpure";
	if ( method == METHOD_ABOVE_DOW  ) ms = "abovedow";
	if ( method == METHOD_INNER_TAGID ) ms = "innertagid";
	if ( method == METHOD_ABOVE_ADDR ) ms = "aboveaddr";
	//if ( method == METHOD_ABOVE_TOD  ) ms = "abovetod";
	//if ( method == METHOD_EMPTY_TAG  ) ms = "emptytag";
	//if ( method == METHOD_ATTRIBUTE ) ms = "attribute";

	// skip this for now
	//return total;

	logf(LOG_DEBUG,"sec: 1stbro=%"UINT32" "
	     "nondelims=%"INT32" "
	     "total=%"INT32" "
	     "bonus1=%"INT32" "
	     "bonus2=%"INT32" "
	     "bonus3=%"INT32" "
	     "avgSim=%.02f "
	     "minSim=%.02f "
	     "totalcomps=%"INT32" "
	     "inserts=%"INT32" "
	     "skips=%"INT32" "
	     "containera=%"INT32" "
	     //"goodcount=%"INT32" badcount=%"INT32" "
	     "dhA=%"INT32" method=%s",
	     start->m_a,
	     nonDelims,
	     total,
	     bonus1,
	     bonus2,
	     bonus3,
	     avgSim,
	     minSim,
	     minTotalComponents,
	     inserts,
	     skips,
	     (int32_t)container->m_a,
	     //goodCount,badCount,
	     delim->m_a,ms);

	// show the difference in the two adjacent brother sections that
	// resulted in the min similarity
	// NOTE: using the log() it was truncating us!! so use stderr
	fprintf(stderr,"sec: mina1=%"INT32" mina2=%"INT32" "
	     "missingbits=%s\n", mina1,mina2, minBuf.getBufStart());

	// return score
	return total;
}


char *getSentBitLabel ( sentflags_t sf ) {
	if ( sf == SENT_HAS_COLON  ) return "hascolon";
	if ( sf == SENT_AFTER_COLON  ) return "aftercolon";
	//if ( sf == SENT_DUP_SECTION ) return "dupsection";
	if ( sf == SENT_BAD_FIRST_WORD ) return "badfirstword";
	if ( sf == SENT_MIXED_CASE ) return "mixedcase";
	if ( sf == SENT_MIXED_CASE_STRICT ) return "mixedcasestrict";
	if ( sf == SENT_POWERED_BY ) return "poweredby";
	if ( sf == SENT_MULT_EVENTS ) return "multevents";
	if ( sf == SENT_PAGE_REPEAT ) return "pagerepeat";
	if ( sf == SENT_NUMBERS_ONLY ) return "numbersonly";
	if ( sf == SENT_IN_ADDRESS ) return "inaddr";
	if ( sf == SENT_SECOND_TITLE ) return "secondtitle";
	if ( sf == SENT_IS_DATE ) return "allwordsindate";
	if ( sf == SENT_LAST_STOP ) return "laststop";
	if ( sf == SENT_NUMBER_START ) return "numberstarts";
	//if ( sf == SENT_HASEVENTADDRESS ) return "haseventaddress";
	//if ( sf == SENT_PRETTY ) return "pretty";
	//if ( sf == SENT_NO_PREV_BROTHER ) return "noprevbro";
	//if ( sf == SENT_NO_NEXT_BROTHER ) return "nonextbro";
	if ( sf == SENT_IN_HEADER ) return "inheader";
	//if ( sf == SENT_DONOTPRINT ) return "donotprint";
	if ( sf == SENT_IN_LIST ) return "inlist";
	if ( sf == SENT_IN_BIG_LIST ) return "inbiglist";
	if ( sf == SENT_COLON_ENDS ) return "colonends";
	if ( sf == SENT_IN_ADDRESS_NAME ) return "inaddressname";
	if ( sf == SENT_IN_TITLEY_TAG ) return "intitleytag";
	if ( sf == SENT_CITY_STATE ) return "citystate";
	if ( sf == SENT_PRICEY ) return "pricey";
	if ( sf == (sentflags_t)SENT_HAS_PRICE ) return "hasprice";
	if ( sf == SENT_PERIOD_ENDS ) return "periodends";
	if ( sf == SENT_HAS_PHONE ) return "hasphone";
	if ( sf == SENT_IN_MENU ) return "inmenu";
	if ( sf == SENT_MIXED_TEXT ) return "mixedtext";
	if ( sf == SENT_TAGS ) return "senttags";
	if ( sf == SENT_INTITLEFIELD ) return "intitlefield";
	if ( sf == SENT_INPLACEFIELD ) return "inplacefield";
	if ( sf == SENT_STRANGE_PUNCT ) return "strangepunct";
	if ( sf == SENT_TAG_INDICATOR ) return "tagindicator";
	//if ( sf == SENT_GENERIC_WORDS ) return "genericwords";
	if ( sf == SENT_INNONTITLEFIELD ) return "innontitlefield";
	//if ( sf == SENT_TOO_MANY_WORDS ) return "toomanywords";
	if ( sf == SENT_HASNOSPACE ) return "hasnospace";
	if ( sf == SENT_IS_BYLINE ) return "isbyline";
	if ( sf == SENT_NON_TITLE_FIELD ) return "nontitlefield";
	if ( sf == SENT_TITLE_FIELD ) return "titlefield";
	if ( sf == SENT_UNIQUE_TAG_HASH ) return "uniquetaghash";
	if ( sf == SENT_AFTER_SENTENCE ) return "aftersentence";
	if ( sf == SENT_WORD_SANDWICH ) return "wordsandwich";
	if ( sf == SENT_AFTER_SPACER ) return "afterspacer";
	if ( sf == SENT_BEFORE_SPACER ) return "beforespacer";
	if ( sf == SENT_LOCATION_SANDWICH ) return "locationsandwich";
	if ( sf == SENT_NUKE_FIRST_WORD ) return "nukefirstword";
	if ( sf == SENT_FIELD_NAME ) return "fieldname";
	if ( sf == SENT_PERIOD_ENDS_HARD ) return "periodends2";
	if ( sf == SENT_PARENS_START ) return "parensstart";
	if ( sf == SENT_IN_MENU_HEADER ) return "inmenuheader";
	if ( sf == SENT_IN_TRUMBA_TITLE ) return "intrumbatitle";
	if ( sf == SENT_PLACE_NAME ) return "placename";
	if ( sf == SENT_CONTAINS_PLACE_NAME ) return "containsplacename";
	if ( sf == SENT_FORMTABLE_FIELD ) return "formtablefield";
	if ( sf == SENT_FORMTABLE_VALUE ) return "formtablevalue";
	if ( sf == SENT_IN_TAG ) return "intag";
	if ( sf == SENT_OBVIOUS_PLACE ) return "obviousplace";
	//if ( sf == SENT_ONROOTPAGE ) return "onrootpage";
	if ( sf == SENT_HASSOMEEVENTSDATE ) return "hassomeeventsdate";
	//if ( sf == SENT_EVENT_ENDING ) return "eventending";
	if ( sf == SENT_HASTITLEWORDS ) return "hastitlewords";
	//if ( sf == SENT_CLOSETODATE  ) return "closetodate";
	//if ( sf == SENT_GOODEVENTSTART ) return "goodeventstart";
	if ( sf == SENT_BADEVENTSTART ) return "badeventstart";
	if ( sf == SENT_MENU_SENTENCE ) return "menusentence";
	if ( sf == SENT_PRETTY ) return "pretty";
	char *xx=NULL;*xx=0;
	return NULL;
}

static sentflags_t s_sentFlags[] = {
	SENT_AFTER_SPACER,
	SENT_BEFORE_SPACER,
	//SENT_PERIOD_ENDS, already have periodend2
	SENT_IN_TAG,
	SENT_IN_HEADER,
	SENT_NUMBERS_ONLY,
	SENT_IN_ADDRESS,
	SENT_IS_DATE,
	SENT_NUMBER_START,
	SENT_IN_TITLEY_TAG,
	SENT_PRICEY,
	SENT_HAS_PHONE,
	SENT_IN_MENU,
	SENT_INTITLEFIELD,
	SENT_STRANGE_PUNCT,
	SENT_INNONTITLEFIELD,
	//SENT_IS_BYLINE,
	SENT_NON_TITLE_FIELD,
	SENT_TITLE_FIELD,
	//SENT_UNIQUE_TAG_HASH,
	SENT_FIELD_NAME,
	SENT_PERIOD_ENDS_HARD,
	//SENT_IN_MENU_HEADER,
	SENT_PLACE_NAME,
	//SENT_FORMAT_DUP,
	SENT_MIXED_CASE };

char *getSecBitLabel ( sec_t sf ) {
	if ( sf == SEC_HEADING_CONTAINER ) return "headingcontainer";
	if ( sf == SEC_PLAIN_TEXT ) return "plaintext";
	if ( sf == SEC_HAS_TOD   ) return "hastod";
	if ( sf == SEC_HAS_DOW   ) return "hasdow";
	if ( sf == SEC_HAS_MONTH ) return "hasmonth";
	if ( sf == SEC_HAS_DOM   ) return "hasdom";
	char *xx=NULL;*xx=0;
	return NULL;
}

static sec_t s_secFlags[] = {
	SEC_HEADING_CONTAINER,
	SEC_PLAIN_TEXT,
	SEC_HAS_TOD,
	SEC_HAS_DOW,
	SEC_HAS_MONTH,
	SEC_HAS_DOM };

#define MAXLABELSIZE 64

bool addLabel ( HashTableX *labelTable ,
		int32_t        key ,
		char       *label ) {

	if ( key == 1021574190 )
		log("got it");
	// if in there, make sure agrees
	char *ptr = (char *)labelTable->getValue (&key);
	if ( ptr ) {
		// compare sanity check
		if ( strcmp(ptr,label) ) { char *xx=NULL;*xx=0; }
		return true;
	}

	// see if label already exists under different key
	for ( int32_t i = 0 ; i < labelTable->m_numSlots ; i++ ) {
		if ( ! labelTable->m_flags[i] ) continue;
		char *v = (char *)labelTable->getValueFromSlot(i);
		if ( strcmp(v,label) ) continue;
		int32_t k1 = *(int32_t *)labelTable->getKeyFromSlot(i);
		log("sec: key=%"INT32" oldk=%"INT32"",key,k1);
		char *xx=NULL;*xx=0;
	}

	// add it otherwise
	return labelTable->addKey ( &key, label );
}

// . if sx is NULL then prevSent will be the last sentence in partition cell
// . use the hashtable
bool Sections::hashSentBits (Section    *sx         ,
			     HashTableX *vht        ,
			     Section    *container  ,
			     uint32_t    mod        ,
			     HashTableX *labelTable ,
			     char       *modLabel   ) {

	int32_t n;
	int32_t count = 0;
	char sbuf [ MAXLABELSIZE ];
	
	if ( ! sx ) {
		// if no mod, we do not hash sent bits for NULL sentences
		if ( ! mod ) return true;
		// mix up
		uint32_t key = (mod << 8) ^ 72263;
		// use that
		if ( ! vht->addTerm32 ( &key) )  return false;
		// if no debug, done
		if ( ! labelTable ) return true;
		// for debug
		sprintf(sbuf,"%sXXXX",modLabel);
		addLabel(labelTable,key,sbuf);
		return true;
	}

	n = sizeof(s_sentFlags)/sizeof(sentflags_t);
	// handle SENT_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if bit is off
		if ( ! (sx->m_sentFlags & s_sentFlags[i] ) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod it?
		key ^= mod;
		// now put that into our vector
		if ( ! vht->addTerm32 ( &key ) ) return false;
		// add to our label table too
		if ( ! labelTable ) continue;
		// convert sentence bit to text description
		char *str = getSentBitLabel ( s_sentFlags[i] );
		// store in buffer
		if ( modLabel ) 
			sprintf ( sbuf,"%s%s", modLabel,str );
		else    
			sprintf ( sbuf,"%s", str );
		// make sure X chars or less
		if ( strlen(sbuf)>= MAXLABELSIZE-1) { char *xx=NULL;*xx=0; }
		// store
		if ( ! addLabel(labelTable,key,sbuf ) ) return false;
	}

	n = sizeof(s_secFlags)/sizeof(sec_t);
	// and for SEC_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// mod it with the value for sentence "sx"
		if ( sx && ! (sx->m_flags & s_secFlags[i]) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod it?
		key ^= mod;
		// now put that into our vector
		if ( ! vht->addTerm32 ( &key ) ) return false;
		// add to our label table too
		if ( ! labelTable ) continue;
		// convert sentence bit to text description
		char *str = getSecBitLabel ( s_secFlags[i] );
		// store in buffer
		if ( modLabel ) 
			sprintf ( sbuf,"%s%s", modLabel,str );
		else    
			sprintf ( sbuf,"%s", str );
		// make sure X chars or less
		if ( strlen(sbuf)>= MAXLABELSIZE-1) { char *xx=NULL;*xx=0; }
		// store
		if ( ! addLabel(labelTable,key,sbuf ) ) return false;
	}

	// and tag sections we are in
	for ( Section *sp = sx->m_parent ; sp ; sp = sp->m_parent ) {
		// stop when not in container anymore
		if ( container && ! container->contains ( sp ) ) break;
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no tag id
		if ( ! sp->m_tagId ) continue;
		// otherwise, use it
		uint32_t key = sp->m_tagId;
		// mod it?
		key ^= mod;
		// now put that into our vector
		if ( ! vht->addTerm32 ( &key ) ) return false;
		// add to our label table too
		if ( ! labelTable ) continue;
		// convert sentence bit to text description
		char *str = getTagName(sp->m_tagId);
		if ( modLabel ) sprintf ( sbuf,"%s%s", modLabel,str);
		else            sprintf ( sbuf,"%s", str );
		// make sure X chars or less
		if ( strlen(sbuf)>= MAXLABELSIZE-1) { char *xx=NULL;*xx=0; }
		// store
		if ( ! addLabel(labelTable,key,sbuf ) ) return false;
	}		

	return true;
}

// . if sx is NULL then prevSent will be the last sentence in partition cell
// . use the hashtable
bool Sections::hashSentPairs (Section    *sx ,
			      Section    *sb ,
			      HashTableX *vht        ,
			      Section    *container  ,
			      HashTableX *labelTable ) {

	// only one can be NULL
	if ( ! sx && ! sb ) return true;

	int32_t n;
	int32_t count = 0;
	char *str = NULL;
	char sbuf [ MAXLABELSIZE ];

	if ( ! sx ) {
		// mix up
		uint32_t mod = 9944812;
		// for debug
		sprintf(sbuf,"XXXX*");
		// try to pair up with that
		return hashSentBits ( sb, vht,container,mod,labelTable,sbuf);
	}


	n = sizeof(s_sentFlags)/sizeof(sentflags_t);
	// handle SENT_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// mod it with the value for sentence "sx"
		if ( sx && ! ( sx->m_sentFlags & s_sentFlags[i]) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod that
		uint32_t mod = key << 8;
		// add to our label table too
		if ( labelTable ) {
			// convert sentence bit to text description
			char *str = getSentBitLabel ( s_sentFlags[i] );
			// store in buffer
			sprintf ( sbuf , "%s*", str );
			// make sure X chars or less
			if(strlen(sbuf)>=MAXLABELSIZE-1){char *xx=NULL;*xx=0;}
		}
		// hash sentenceb with that mod
		if ( ! hashSentBits ( sb, vht,container,mod,labelTable,sbuf)) 
			return false;
	}

	n = sizeof(s_secFlags)/sizeof(sec_t);
	// and for SEC_* flags
	for ( int32_t i = 0 ; i < n ; i++ , count++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// mod it with the value for sentence "sx"
		if ( sx && !(sx->m_flags & s_secFlags[i] ) ) continue;
		// set key
		uint32_t key = g_hashtab[count/256][count%256];
		// mod that
		uint32_t mod = key << 8;
		// add to our label table too
		if ( labelTable ) {
			// convert sentence bit to text description
			char *str = getSecBitLabel ( s_secFlags[i] );
			// store in buffer
			sprintf ( sbuf , "%s*", str );
			// make sure X chars or less
			if(strlen(sbuf)>=MAXLABELSIZE-1){char *xx=NULL;*xx=0;}
		}
		// hash sentenceb with that mod
		if ( ! hashSentBits ( sb, vht, container,mod,labelTable,sbuf)) 
			return false;
	}

	// and tag sections we are in
	for ( Section *sp = sx->m_parent ; sp ; sp = sp->m_parent ) {
		// stop when not in container anymore
		if ( container && ! container->contains ( sp ) ) break;
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no tag id
		if ( ! sp->m_tagId ) continue;
		// otherwise, use it
		uint32_t key = sp->m_tagId;
		// mod that
		uint32_t mod = (key<<8) ^ 45644;
		// fake it
		if ( labelTable ) {
			// convert sentence bit to text description
			sprintf(sbuf,"%s*", getTagName(sp->m_tagId) );
			str = sbuf;
		}
		// if no mod, then do the pairs now
		if ( ! hashSentBits ( sb,vht,container,mod,labelTable,str)) 
			return false;
	}

	return true;
}


// . don't return 0 because we make a vector of these hashes
//   and computeSimilarity() assumes vectors are NULL term'd. return -1 instead
int32_t Sections::getDelimHash ( char method , Section *bro , Section *head ) {

	// now all must have text!
	//if ( bro->m_firstWordPos < 0 ) return -1;

	// if has no text give it a slightly different hash because these
	// sections are often seen as delimeters
	int32_t mod = 0;
	if ( bro->m_firstWordPos < 0 ) mod = 3405873;

	// this is really a fix for the screwed up sections in 
	// www.guysndollsllc.com/page5/page4/page4.html which have
	// a dom and then the tod. sometimes the dom is in the same sentence
	// section as the tod and other times it isn't. so we have a list of
	// dom/tod/domtod sections that are brothers. but they are partitioned
	// by the br tag, but the dom following the br tag sometimes has
	// extra text after it like "To be announced", so we just required
	// that the first word be a date.
	/*
	if ( method == METHOD_BR_DOM ) {
		if ( bro->m_tagId != TAG_BR ) return -1;
		// get next
		Section *next = bro->m_nextBrother;
		// must have section after
		if ( ! next ) return -1;
		// must be a sort of heading like "Jul 24"
		//if ( !(next->m_flags & SEC_HEADING_CONTAINER) &&
		//     !(next->m_flags & SEC_HEADING          ) )
		//	return -1;
		// section after must have dom
		if ( ! (next->m_flags & SEC_HAS_DOM) ) return -1;
		// now it must be all date words
		int32_t a = next->m_firstWordPos;
		int32_t b = next->m_lastWordPos;
		// sanity check
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// scan
		for ( int32_t i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[i] ) continue;
			// first word must be in date
			if ( ! ( m_bits->m_bits[i] & D_IS_IN_DATE ) ) 
				return -1;
			// ok, that's good enough!
			break;
		}
		// we're good!
		return 8888;
	}
	*/

	// . single br tags not allowed to be implied section delimeters any
	//   more for no specific reason but seems to be the right way to go
	// . this hurts the guysndollsllc.com url, which has single br lines
	//   each with its own DOM, so let's allow br tags in that case
	if ( m_tids[bro->m_a] == TAG_BR && bro->m_a + 1 == bro->m_b  ) 
		return -1;

	/*
	// <P></P>
	if ( method == METHOD_EMPTY_TAG ) {
		// . and cabq.gov/museums/events.html also uses <br><br> which
		//   looks the same as <p></p> when rendered
		// . and this is not a single br tag since that is eliminated
		//   by the return statement right above us
		// . damn, they had the brbr in a <p> tag with other text so
		//   this was not fixing that ... so i took it out to be safe
		// . now i need this for sunsetpromotions.com which use
		//   double brs otherwise i get mutliple locations error
		if ( bro->m_tagId == TAG_BR ) 
			return 777777;
		// must be a p tag for now
		if ( bro->m_tagId != TAG_P ) return -1;
		// and empty. if it has alnum words in it, return -1
		if ( bro->m_firstWordPos >= 0 ) return -1;
		// images count as words i guess to fix 
		// cabq.gov/museum/events.html
		if ( containsTagId ( bro , TAG_IMG ) ) return -1;
		// use a special hash
		return 777777;
	}
	*/

	if ( method == METHOD_MONTH_PURE ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		if ( ! (bro->m_flags & SEC_HAS_MONTH) ) 
			return -1;
		// skip if also have daynum, we just want the month and
		// maybe the year...
		if ( (bro->m_flags & SEC_HAS_DOM) ) 
			return -1;
		// now it must be all date words
		int32_t a = bro->m_firstWordPos;
		int32_t b = bro->m_lastWordPos;
		// sanity check
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// scan
		for ( int32_t i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[i] ) continue;
			// must be in date
			if ( ! ( m_bits->m_bits[i] & D_IS_IN_DATE ) ) 
				return -1;
		}
		// do not collide with tagids
		return 999999;
	}
	// are we partitioning by tagid?
	if ( method == METHOD_TAGID ) {
		int32_t tid = bro->m_tagId;
		// . map a strong tag to h4 since they look the same!
		// . should fix metropolis url
		if ( tid == TAG_STRONG ) tid = TAG_H4;
		if ( tid == TAG_B      ) tid = TAG_H4;
		if ( tid ) return tid;
		// . use -1 to indicate can't be a delimeter
		// . this should fix switchboard.com which lost
		//   "Wedding cakes..." as part of description because it
		//   called it an SEC_MENU_HEADER because an implied section
		//   was inserted and placed it at the top so it had nothing
		//   above it so-to-speak and was able to be a menu header.
		if ( bro->m_baseHash == BH_SENTENCE ) return -1;
		// if 0 use base hash
		return bro->m_baseHash ^ mod;
	}
	/*
	// stole this function logic from getBaseHash2()
	if ( method == METHOD_ATTRIBUTE ) {
		// assume this
		int32_t bh = bro->m_tagHash; // Id;
		// for sentence sections, etc...
		if ( bh == 0 ) bh = bro->m_baseHash;
		// do not allow sentences (see comment above)
		if ( bro->m_baseHash == BH_SENTENCE ) return -1;
		// . make heading sections different
		// . this was wrong for christchurchcincinnati.org/worship
		//   so i commented it out to get the proper h3/h2 headers
		//if ( bro->m_flags & SEC_HEADING_CONTAINER) 
		//	bh ^= 0x789123;
		return bh ^ mod;
	}
	*/
	if ( method == METHOD_INNER_TAGID ) {
		Section *last = bro;
		// scan to right to find first kid
		for ( Section *nn = bro->m_next ; nn ; nn = nn->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if not contained
			if ( ! last->contains ( nn ) ) break;
			// ignore sentences because they alternate with
			// the strong tags used in abqfolkfest.org and
			// mess things up. some are <x><strong><sent> and
			// some are <x><sent>blah blah<strong>.
			// THIS IS NOT THE BEST WAY to fix this issue. i think
			// we need to put the sentence sections outside the
			// strong sections when possible? not sure.
			if ( nn->m_baseHash == BH_SENTENCE ) continue;
			// stop if it directly contains text DIRECTLY 
			// to fix <p> blah <a href> blah</a> </p> for
			// abqtango.com, so we stop at the <p> tag and do
			// not use the <a> tag as the inner tag id.
			// CRAP, we do not have this set yet...
			//if ( ! ( nn->m_flags & SEC_NOTEXT ) ) break;
			// just do it this way then
			if ( nn->m_a +1 < m_nw && 
			     m_wids[nn->m_a + 1] ) 
				break;
			if ( nn->m_a +2 < m_nw && 
			     ! m_tids[nn->m_a+1] &&
			     m_wids[nn->m_a + 2] ) 
				break;
			// update
			last = nn;
		}
		// do not allow sentences (see comment above)
		if ( last->m_baseHash == BH_SENTENCE ) return -1;
		// make it a little different from regular tagid
		return last->m_tagId ^ 34908573 ^ mod;
	}
	if ( method == METHOD_DOM ) {
		// need a dom of course (day of month)
		if ( ! (bro->m_flags & SEC_HAS_DOM) ) 
			return -1;
		// we require it also have another type because DOM is
		// quite fuzzy and matches all these little numbers in
		// xml docs like stubhub.com's xml feed
		if ( ! (bro->m_flags & 
			(SEC_HAS_MONTH|SEC_HAS_DOW|SEC_HAS_TOD)) )
			return -1;
		// if you aren't pure you should at least be in a tag
		// or something. don't allow regular sentences for now...
		// otherwise we get a few sections that aren't good for
		// sunsetpromotions.com because it has a few sentences 
		// mentioning the day of month (april 16 2011). but those
		// sections should be split by the double br tags.
		if ( bro->m_sentFlags & SENT_MIXED_CASE )
			return -1;
		// . must be a sort of heading like "Jul 24"
		// . without this we were getting bad implied sections
		//   for tennisoncampus because the section had a sentence
		//   with a bunch of sentences... BUT what does this hurt?? 
		// . it hurts anja when we require this stuff here on
		//   texasdrums.drums.org/new_orleansdrums.htm beause it is
		//   unclear that her event should be boxed in...
		if ( !(bro->m_flags & SEC_SENTENCE) &&
		     !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		// do not collide with tagids
		return 11111;
	}
	if ( method == METHOD_DOM_PURE ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		if ( ! (bro->m_flags & SEC_HAS_DOM) ) 
			return -1;
		// now it must be all date words
		int32_t a = bro->m_firstWordPos;
		int32_t b = bro->m_lastWordPos;
		// sanity check
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// scan
		for ( int32_t i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[i] ) continue;
			// must be in date
			if ( ! ( m_bits->m_bits[i] & D_IS_IN_DATE ) ) 
				return -1;
		}
		// do not collide with tagids
		return 55555;
	}
	if ( method == METHOD_ABOVE_DOM ) {
		// we cannot have a dom ourselves to reduce the problem
		// with cabq.gov/museums/events.html of repeating the dom
		// in the body and getting a false header.
		if ( bro->m_flags & SEC_HAS_DOM )
			return -1;
		Section *nb = bro->m_nextBrother;
		if ( ! nb ) 
			return -1;
		// skip empties like image tags
		if ( nb->m_firstWordPos < 0 ) 
			nb = nb->m_nextBrother;
		if ( ! nb ) 
			return -1;
		// must be a sort of heading like "Jul 24"
		//if ( !(nb->m_flags & SEC_HEADING_CONTAINER) &&
		//     !(nb->m_flags & SEC_HEADING          ) )
		//	return -1;
		if ( ! ( nb->m_flags & SEC_HAS_DOM ) ) 
			return -1;
		// require we be in a tag
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		// do not collide with tagids
		return 22222;
	}
	if ( method == METHOD_DOW ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		if ( ! (bro->m_flags & SEC_HAS_DOW) ) 
			return -1;
		// do not collide with tagids
		return 33333;
	}
	if ( method == METHOD_DOW_PURE ) {
		// santafeplayhouse.org had 
		// "Thursdays Pay What You Wish Performances" and so it
		// was not getting an implied section set, so let's do away
		// with the pure dow algo and see what happens.
		return -1;
		// must be a sort of heading like "Jul 24"
		//if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		//     !(bro->m_flags & SEC_HEADING          ) )
		//	return -1;
		if ( ! (bro->m_flags & SEC_HAS_DOW) ) 
			return -1;
		// this is causing core
		if ( bro->m_tagId == TAG_TC ) return -1;
		// now it must be all date words
		int32_t a = bro->m_firstWordPos;
		int32_t b = bro->m_lastWordPos;
		// sanity check
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// scan
		for ( int32_t i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not wid
			if ( ! m_wids[i] ) continue;
			// must be in date
			if ( ! ( m_bits->m_bits[i] & D_IS_IN_DATE ) ) 
				return -1;
		}
		// do not collide with tagids
		return 66666;
	}
	if ( method == METHOD_ABOVE_DOW ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		Section *nb = bro->m_nextBrother;
		if ( ! nb ) 
			return -1;
		if ( ! ( nb->m_flags & SEC_HAS_DOW ) ) 
			return -1;
		// next sentence not set yet, so figure it out
		Section *sent = nb;
		// scan for it
		for ( ; sent ; sent = sent->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop we got a sentence section now
			if ( sent->m_flags & SEC_SENTENCE ) break;
		}
		// might have been last sentence already
		if ( ! sent )
			return -1;
		// . next SENTENCE must have the dow
		// . should fix santafeplayhouse from making crazy
		//   implied sections
		if ( ! (sent->m_flags & SEC_HAS_DOW) )
			return -1;
		// do not collide with tagids
		return 44444;
	}
	if ( method == METHOD_ABOVE_ADDR ) {
		// must be a sort of heading like "11. San Pedro Library"
		// for cabq.gov
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		Section *nb = bro->m_nextBrother;
		if ( ! nb ) 
			return -1;
		if ( ! nb->m_addrXor )
			return -1;
		// next sentence not set yet, so figure it out
		Section *sent = nb;
		// scan for it
		for ( ; sent ; sent = sent->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop we got a sentence section now
			if ( sent->m_flags & SEC_SENTENCE ) break;
		}
		// might have been last sentence already
		if ( ! sent )
			return -1;
		// . next SENTENCE must have the addr
		// . should fix santafeplayhouse from making crazy
		//   implied sections
		if ( ! sent->m_addrXor )
			return -1;
		// do not collide with tagids
		return 77777;
	}
	/*
	if ( method == METHOD_ABOVE_TOD ) {
		// must be a sort of heading like "Jul 24"
		if ( !(bro->m_flags & SEC_HEADING_CONTAINER) &&
		     !(bro->m_flags & SEC_HEADING          ) )
			return -1;
		Section *nb = bro->m_nextBrother;
		if ( ! nb ) 
			return -1;
		if ( ! ( nb->m_flags & SEC_HAS_TOD ) ) 
			return -1;
		// next sentence not set yet, so figure it out
		Section *sent = nb;
		// scan for it
		for ( ; sent ; sent = sent->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop we got a sentence section now
			if ( sent->m_flags & SEC_SENTENCE ) break;
		}
		// next SENTENCE must have the tod
		if ( ! (sent->m_flags & SEC_HAS_TOD) )
			return -1;
		// do not collide with tagids
		return 444333;
	}
	*/
	char *xx=NULL;*xx=0;
	return 0;
}
			
// . PROBLEM: because we ignore non-breaking tags we often get sections
//   that are really not sentences, but we are forced into them because
//   we cannot split span or bold tags
//   i.e. "<div>This is <b>a sentence. And this</b> is a sentence.</div>"
//   forces us to treat the entire div tag as a sentence section.
// . i did add some logic to ignore those (the two for-k loops below) but then
//   Address.cpp cores because it expects every alnum word to be in a sentence
// . now make sure to shrink into our current parent if we would not lose
//   alnum chars!! fixes sentence flip flopping
// . returns false and sets g_errno on error
bool Sections::addSentenceSections ( ) {

	m_numSentenceSections = 0;

	sec_t badFlags = 
		//SEC_MARQUEE|
		SEC_STYLE|
		SEC_SCRIPT|
		SEC_SELECT|
		SEC_HIDDEN|
		SEC_NOSCRIPT;

	// int16_tcut
	Section **sp = m_sectionPtrs;

	static bool s_init = false;
	static int64_t h_in;
	static int64_t h_at;
	static int64_t h_for;
	static int64_t h_to;
	static int64_t h_on;
	static int64_t h_under;
	static int64_t h_with;
	static int64_t h_along;
	static int64_t h_from;
	static int64_t h_by;
	static int64_t h_of;
	static int64_t h_some;
	static int64_t h_the;
	static int64_t h_and;
	static int64_t h_a;
	static int64_t h_p;
	static int64_t h_m;
	static int64_t h_am;
	static int64_t h_pm;
	static int64_t h_http;
	static int64_t h_https;
	static int64_t h_room;
	static int64_t h_rm;
	static int64_t h_bldg;
	static int64_t h_building;
	static int64_t h_suite;
	static int64_t h_ste;
	static int64_t h_tags;
	//static int64_t h_noon;
	//static int64_t h_midnight;
	if ( ! s_init ) {
		s_init = true;
		h_tags = hash64n("tags");
		h_in = hash64n("in");
		h_the = hash64n("the");
		h_and = hash64n("and");
		h_a = hash64n("a");
		h_p = hash64n("p");
		h_m = hash64n("m");
		h_am = hash64n("am");
		h_pm = hash64n("pm");
		h_a = hash64n("a");
		h_at = hash64n("at");
		h_for = hash64n("for");
		h_to = hash64n("to");
		h_on = hash64n("on");
		h_under = hash64n("under");
		h_with = hash64n("with");
		h_along = hash64n("along");
		h_from = hash64n("from");
		h_by = hash64n("by");
		h_of = hash64n("of");
		h_some = hash64n("some");
		h_http = hash64n("http");
		h_https = hash64n("https");
		h_room = hash64n("room");
		h_rm = hash64n("rm");
		h_bldg = hash64n("bldg");
		h_building = hash64n("building");
		h_suite = hash64n("suite");
		h_ste = hash64n("ste");
		//h_noon  = hash64n("noon");
		//h_midnight = hash64n("midnight");
	}

	// need D_IS_IN_URL bits to be valid
	m_bits->setInUrlBits ( m_niceness );
	// int16_tcut
	wbit_t *bb = m_bits->m_bits;
	/*
	// int16_tcut
	Date **datePtrs = m_dates->m_datePtrs;
	// int16_tcut. this should not include telescoped dates!!!
	int32_t maxnd = m_dates->m_numDatePtrs;
	// set up nd to reference first date in the document
	int32_t nd = 0;
	// what date we are currently in
	Date *inDate = NULL;
	// get first date
	for ( ; nd < maxnd ; nd++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if empty
		if ( ! datePtrs[nd] ) continue;
		// skip if not in doc
		if ( datePtrs[nd]->m_a < 0 ) 
			continue;
		// stop when we got one
		break;
	}
	*/
	// is the abbr. a noun? like "appt."
	bool hasWordAfter = false;

	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need a wid
		if ( ! m_wids[i] ) continue;
		// get section we are currently in
		Section *cs = m_sectionPtrs[i];
		// skip if its bad! i.e. style or script or whatever
		if ( cs->m_flags & badFlags ) continue;
		// set that
		int64_t prevWid = m_wids[i];
		int64_t prevPrevWid = 0LL;
		// flag
		int32_t lastWidPos = i;//-1;
		bool lastWasComma = false;
		nodeid_t includedTag = -2;
		int32_t lastbr = -1;
		bool hasColon = false;
		bool endOnBr = false;
		bool endOnBold = false;
		bool capped = true;
		int32_t upper = 0;
		int32_t numAlnums = 0;
		int32_t verbCount = 0;
		// scan for sentence end
		int32_t j; for ( j = i ; j < m_nw ; j++ ) {
			// skip words
			if ( m_wids[j] ) {
				// prev prev
				prevPrevWid = prevWid;
				// assume not a word like "vs."
				hasWordAfter = false;
				// set prev
				prevWid = m_wids[j];
				lastWidPos = j;
				lastWasComma = false;
				endOnBr = false;
				endOnBold = false;
				numAlnums++;
				// skip if stop word and need not be
				// capitalized
				if ( bb[j] & D_IS_STOPWORD ) continue;
				if ( m_wlens[j] <= 1 ) continue;
				if ( is_digit(m_wptrs[j][0]) ) continue;
				if ( !is_upper_utf8(m_wptrs[j])) capped=false;
				else                           upper++;
				// is it a verb?
				if ( isVerb( &m_wids[j] ) )
					verbCount++;
				//if ( bb[i] & D_IS_IN_DATE_2 ) inDate = true;
				// it is in the sentence
				continue;
			}
			// tag?
			if ( m_tids[j] ) {
				// int16_tcut
				nodeid_t tid = m_tids[j] & BACKBITCOMP;

				// "shelter: 123-4567"
				// do not allow "<br>...:...<br>" to be
				// the 2nd line of a sentence.
				// they should be their own setence.
				// should fix st. martin's center
				// for unm.edu which has that shelter: line.
				// BUT it hurts "TUESDAYS: 7-8 song...<br>
				// off-site." for texasdrums.
				//if ( isBreakingTagId(tid) &&
				//     lastbr >= 0 && 
				//     hasColon ) {
				//	// end sentence at the prev br tag
				//	j = lastbr;
				//	break;
				//}

				// treat nobr as breaking to fix ceder.net
				// which has it after the group title
				if ( tid == TAG_NOBR ) break;

				if ( tid == TAG_BR ) endOnBr = true;
				if ( tid == TAG_B  ) endOnBold = true;

				// a </b><br> is usually like a header
				if ( capped && upper && endOnBr && endOnBold )
					break;
				// if it is <span style="display:none"> or
				// div or whatever, that is breaking!
				// fixes http://chuckprophet.com/gigs/ 
				if ( (tid == TAG_DIV ||
				      tid == TAG_SPAN ) &&
				     m_wlens[j] > 14 &&
				     strncasestr(m_wptrs[j],"display:none",
						 m_wlens[j]) )
					break;
				// ok, treat span as non-breaking for a second
				if ( tid == TAG_SPAN ) continue;
				// mark this
				if ( tid == TAG_BR ) lastbr = j;
				//
				// certain tags like span and br sometimes
				// do and sometimes do not break a sentence.
				// so by default assume they do, but check
				// for certain indicators...
				//
				if ( tid == TAG_SPAN || 
				     tid == TAG_BR   ||
				     // fixes guysndollsllc.com:
				     // causes core dump:
				     tid == TAG_P    || // villr.com
				     // fixes americantowns.com
				     tid == TAG_DIV     ) {
					// if nothing after, moot point
					if ( j+1 >= m_nw ) break;
					// if we already included this tag
					// then keep including it. but some
					// span tags will break and some won't
					// even when in or around the same
					// sentence. see that local.yahoo.com
					// food delivery services url for
					// the first street address, 
					// 5013 Miramar
					if ( includedTag == tid &&
					     (m_tids[j] & BACKBIT) ) {
						// reset it in case next
						// <span> tag is not connective
						includedTag = -2;
						continue;
					}
					// if we included this tag type
					// as a front tag, then include its
					// back tag in sentence as well.
					// fixes nonamejustfriends.com
					// which has a span tag in sentence:
					// ".. Club holds a <span>FREE</span> 
					//  Cruise Night..." and we allow
					// "<span>" because it follows "a",
					// but we were breaking on </span>!
					if ( !(m_tids[j]&BACKBIT))
						includedTag = tid;
					// if prev punct was comma and not
					// an alnum word
					if ( lastWasComma ) continue;
					// get punct words bookcasing this tag
					if ( ! m_wids[j+1] && 
					     ! m_tids[j+1] &&
					     m_words->hasChar(j+1,',') )
						continue;
					// if prevwid is like "vs." then
					// that means keep going even if
					// we hit one of these tags. fixes
					// "new york knicks vs.<br>orlando
					//  magic"
					if ( hasWordAfter )
						continue;
					// if first alnum word after tag
					// is lower case, that is good too
					int32_t aw = j + 1;
					int32_t maxaw = j + 12;
					if ( maxaw > m_nw ) maxaw = m_nw;
					for ( ; aw < maxaw ; aw++ )
						if ( m_wids[aw] ) break;
					bool isLower = false;
					if ( aw < maxaw &&
					     is_lower_utf8(m_wptrs[aw]) ) 
						isLower = true;

					// http or https is not to be
					// considered as such! fixes
					// webnetdesign.com from getting
					// sentences continued by an http://
					// url below them.
					if ( aw < maxaw &&
					     (m_wids[aw] == h_http ||
					      m_wids[aw] == h_https) )
						isLower = false;

					// this almost always breaks a sentence
					// and adding this line here fixes
					// "Sunday<p>noon" the thewoodencow.com
					// and let's villr.com stay the same
					// since its first part ends in "and"
					//if ( m_wids[aw] == h_noon ||
					//     m_wids[aw] == h_midnight )
					if ( tid == TAG_P &&
					     isLower &&
					     // Oscar G<p>along with xxxx
					     m_wids[aw] != h_along &&
					     m_wids[aw] != h_with )
						isLower = false;

					if ( isLower ) continue;
					// get pre word, preopsitional
					// phrase starter?
					if ( prevWid == h_in ||
					     prevWid == h_the ||
					     prevWid == h_and ||
					     // fix for ending on "(Room A)"
					     (prevWid == h_a &&
					      prevPrevWid != h_rm &&
					      prevPrevWid != h_room &&
					      prevPrevWid != h_bldg &&
					      prevPrevWid != h_building &&
					      prevPrevWid != h_suite &&
					      prevPrevWid != h_ste ) ||
					     prevWid == h_for ||
					     prevWid == h_to ||
					     prevWid == h_on ||
					     prevWid == h_under ||
					     prevWid == h_with ||
					     prevWid == h_from ||
					     prevWid == h_by ||
					     prevWid == h_of ||
					     // "some ... Wednesdays"
					     prevWid == h_some ||
					     prevWid == h_at )
						continue;
				}


				// seems like span breaks for meetup.com
				// et al and not for abqtango.com maybe, we
				// need to download the css??? or what???
				// by default span tags do not seem to break
				// the line but ppl maybe configure them to
				if ( tid == TAG_SPAN ) break;
				// if like <font> ignore it
				if ( ! isBreakingTagId(m_tids[j]) ) continue;
				// only break on xml tags if in rss feed to
				// fix <st1:State w:st="on">Arizona</st1>
				// for gwair.org
				if ( tid==TAG_XMLTAG && !m_isRSSExt) continue;
				// otherwise, stop!
				break;
			}
			// skip simple spaces for speed
			if ( m_wlens[j] == 1 && is_wspace_a(m_wptrs[j][0]))
				continue;

			// do not allow punctuation that is in a url
			// to be split up or used as a splitter. we want
			// to keep the full url intact.
			if ( j > i && j+1 < m_nw &&
			     (bb[j-1] & D_IS_IN_URL) &&
			     (bb[j  ] & D_IS_IN_URL) &&
			     (bb[j+1] & D_IS_IN_URL) )
				continue;

			// was last punct containing a comma?
			lastWasComma = false;
			// scan the punct chars, stop if we hit a sent breaker
			char *p    =     m_wptrs[j];
			char *pend = p + m_wlens[j];
			for ( ; p < pend ; p++ ) {
				// punct word...
				/*
				if ( is_wspace_a(*p) ) continue;
				if ( *p==',' ) continue;
				if ( *p=='&' ) continue;
				if ( *p=='_' ) continue;
				if ( *p=='$' ) continue;
				if ( *p=='#' ) continue;
				if ( *p=='\"' ) continue;
				if ( *p==';' ) continue;
				if ( *p==':' ) continue; // 10:30
				if ( *p=='@' ) continue;
				if ( *p=='%' ) continue;
				if ( *p=='%' ) continue;
				if ( *p=='+' ) continue;
				if ( *p=='-' ) continue;
				if ( *p=='=' ) continue;
				if ( *p=='/' ) continue;
				if ( *p=='*' ) continue;
				if ( *p=='\'') continue; // dish 'n' spoon
				if ( is_wspace_utf8(p) ) continue;
				break;
				*/
				if ( *p == '.' ) break;
				if ( *p == ',' ) lastWasComma =true;
				// allow this too for now... no...
				if ( *p == ';' ) break;
				// now hyphen breaks, mostly for stuff
				// in title tags like dukecityfix.com
				if ( sp[j]->m_tagId == TAG_TITLE &&
				     *p == '-' &&
				     is_wspace_a(p[-1]) &&
				     is_wspace_a(p[+1]) &&
				     lastWidPos >= 0 &&
				     ! m_isRSSExt &&
				     j+1<m_nw &&
				     m_wids[j+1] &&
				     // can't be in a date range
				     // date bits are not valid here because
				     // we are now called from ::set() without
				     // dates because dates need sentence 
				     // sections now to set DF_FUZZY for years
				     ( !isDateType(&prevWid)||
				       !isDateType(&m_wids[j+1])) &&
				     //( ! (bb[lastWidPos] & D_IS_IN_DATE) ||
				     //  ! (bb[j+1] & D_IS_IN_DATE)       ) &&
				     // fix for $10 - $12
				     ( ! is_digit ( m_wptrs[lastWidPos][0]) ||
				       ! is_digit ( m_wptrs[j+1][0]) ) )
					break;
				// . treat colon like comma now
				// . for unm.edu we have 
				//   "Summer Hours: March 15 - Oct15:
				//    8 am. Mon - Fri, 7:30 am - 10 am Sun.,
				//    Winter Hours: Oct. 15 - March 15:
				//    8 am., seven days a week"
				// . and we don't want "winter hours" being
				//   toplogically closer to the summer hours
				// . that is, the colon is a stronger binder
				//   than the comma?
				// . but for villr.com Hours: May-Aug.. gets
				//   made into two sentences and Hours is
				//   seen as a heading section and causes
				//   addImpliedSections() to be wrong.
				//if ( *p == ':' ) lastWasComma =true;
				// . why not the colon?
				if ( *p == ':' ) {

					// Tags: music,concert,fun
					if ( prevWid == h_tags &&
					     // just Tags: so far in sentence
					     j == i )
						break;

					// flag it, but only if not in
					// a time format like "8:30"
					if ( j>0 && !is_digit(m_wptrs[j][-1]))
						hasColon = true;

					// a "::" is used in breadcrumbs,
					// so break on that.
					// fixes "Dining :: Visit :: 
					// Cal Performacnes" title
					if ( p[1] == ':' ) 
						break;

					// if "with" preceeds, allow
					if ( prevWid == h_with ) continue;

					// or prev word was tag! like
					// "blah</b>:..."
					bool tagAfter=(j-1>=0&&m_tids[j-1]);

					// do not allow if next word is tag
					bool tagBefore=(j+1<m_nw&&m_tids[j+1]);

					// do not allow 
					// "<br>...:<br>" or
					// "<br>...<br>:" or
					// since such things are usually
					// somewhat like headers. isolated
					// lines ending on a colon.
					// should fix st. martin's center
					// for unm.edu "Summer Hours: ..."
					if ( lastbr >= 0 && 
					     ( tagBefore || tagAfter ) ) {
						// end sentence there then
						j = lastbr;
						break;
					}
					     
					if ( tagBefore ) break;
					if ( tagAfter  ) break;
					// for now allow it!
					continue;
					// do not break http://... though
					if ( p[1] == '/' ) continue;
					// or 10:30 etc.
					if ( is_digit(p[1]) ) continue;
					if ( j>0 && is_digit(p[-1]) ) continue;
					// allow trumba titles to have colons
					// so they can get the TSF_TITLEY
					// event title boost in Events.cpp
					if ( m_isTrumba &&
					     sp[j]->m_tagId == TAG_TITLE )
						continue;
					// fix guysndollsllc.com which has
					// "Battle of the Bands with: The Cincy
					// Rockers, Second Wind, ..."
					// if last word was a lowercase
					// and one of these, let it in the
					// sentence
					//if ( lastWidPos < 0 ) 
					//	break;
					// must have been lowercase
					if(!is_lower_utf8(m_wptrs[lastWidPos]))
						break;
					// and must be one of these words:
					if ( prevWid == h_with ||
					     // "Send info to: Booking"
					     // from guysndollsllc.com/page4.ht
					     prevWid == h_to ||
					     prevWid == h_and  )
						continue;
					// otherwise, break it
					break;
				}
				// . special hyphen
				// . breaks up title for peachpundit.com
				//   so we get better event title generation
				//   since peachpundit.com will be a reepat sec
				// . BUT it did not work!
				if ( p[0] == (char)-30 &&
				     p[1] == (char)-128 &&
				     p[2] == (char)-108 )
					break;
				// this for sure
				// "Home > Albuquerque Events > Love Song ..."
				if ( *p == '>' ) break;
				if ( *p == '!' ) break;
				if ( *p == '?' ) break;
				if ( *p == '|' ) 
					break;
				// bullets
				if ( p[0] == (char)226 &&
				     p[1] == (char)128 &&
				     p[2] == (char)162 )
					break;
			redo:
				continue;
			}
			// if none, keep going
			if ( p == pend ) continue;
			// if an alnum char follows the ., it is ok
			// probably a hostname or ip or phone #
			if ( is_alnum_utf8(p+1) &&
			     // "venue:ABQ Sq Dance Center..." for
			     // americantowns.com has no space after the colon!
			     *p !=':' ) 
				goto redo;
			// if abbreviation before we are ok too
			if ( *p == '.' && isAbbr(prevWid,&hasWordAfter) ) {
				// but the period may serve a double purpose
				// to end the abbr and terminate the sentence
				// if the word that follows is capitalized,
				// and if the abbr is a lower-case noun.
				//
				// if abbr is like "vs" then do not end sentenc
				if ( hasWordAfter )
					goto redo;

				// set "next" to next alnum word after us
				int32_t next = j+1;
				int64_t nwid = 0LL;
				int32_t max  = next + 10;
				if ( max > m_nw ) max = m_nw;
				for ( ; next < max ; next++ ) {
					if ( ! m_wids[next] ) continue;
					nwid = m_wids[next];
					break;
				}
				// am. pm.
				// if prev word was like 'm' as in am or pm
				// then assume a cap word following ends sent.
				// although if we got 
				// "At 1 p.m. Bob Jones plays"
				// then we'd be wrong.
				bool isAmPm = false;
				if ( prevWid == h_m &&
				     (prevPrevWid == h_a ||
				      prevPrevWid == h_p ) )
					isAmPm = true;
				if ( (prevWid == h_am ||
				      prevWid == h_pm ) )
					isAmPm = true;
				if ( isAmPm &&
				     verbCount >= 1 &&
				     next < max &&
				     m_words->isCapitalized(next) &&
				     // exclude "5 p.m. Friday" (santafe.org)
				     ! isDateType(&nwid) )
					// end the sentence
					break;
				// do not end sentence if like
				// "8 am. Fridays", otherwise it would end
				// without this statement since am is lower
				// case and Fridays is capitalized
				if ( isAmPm && isDateType(&nwid) )
					goto redo;

				// was previous word/abbr capitalized?
				// if so, assume period does not end sentence.
				if ( m_words->isCapitalized(lastWidPos) )
					goto redo;
				// if next word is NOT capitalized, assume
				// period does not end sentence...
				if ( next < max &&
				     ! m_words->isCapitalized ( next ) )
					goto redo;
				// otherwise, abbr is NOT capitalized and
				// next word IS capitalized, so assume the
				// period does NOT end the sentence
			}
			// fix "1. library name" for cabq.gov
			if ( *p == '.' && 
			     lastWidPos == i &&
			     m_words->isNum(lastWidPos) )
				goto redo;
			// fix for www.<b>test</b>.com in google serps
			//if ( j+3<m_nw && 
			//     *p == '.' &&
			//     ( (m_tids[j+1]&BACKBITCOMP) == TAG_B ||
			//       (m_tids[j+1]&BACKBITCOMP) == TAG_STRONG ||
			//       (m_tids[j+1]&BACKBITCOMP) == TAG_I  ) &&
			//     m_wids[j+2] )
			//	goto redo;
			// ok, stop otherwise
			break;
		}
		// set j to this to exclude ending tags
		//if ( lastWidPos >= 0 ) j = lastWidPos + 1;

		// do not include tag at end. try to fix sentence flip flop.
		for ( ; j > i ; j-- ) 
			// stop when we just contain the last word
			if ( m_wids[j-1] ) break;

		// make our sentence endpoints now
		int32_t senta = i;
		// make the sentence defined by [senta,sentb) where sentb
		// defines a half-open interval like we do for almost 
		// everything else
		int32_t sentb = j;

		// update i for next iteration
		i = sentb - 1;

		// if we ended on a punct word, include that in the sentence
		//if ( j < m_nw && ! m_tids[j] ) sentb++;


		// crap, but now sentences intersect with our tag-based
		// sections because they can now split tags because of websites
		// like aliconference.com and abqtango.com whose sentences
		// do not align with the tag sections. therefore we introduce
		// the SEC_TOP_SPLIT and SEC_BOTTOM_SPLIT to indicate 
		// that the section is a top/bottom piece of a split sentence.
		// if both bits are set we assume SEC_MIDDLE_SPLIT.
		// then we set the Section::m_senta and m_sentb to
		// indicate the whole sentence of which it is a split.
		// but the vast majority of the time m_senta and m_sentb
		// will equal m_firstWordPos and m_lastWordPos respectively.
		// then, any routine that



		// so scan the words in the sentence and as we scan we have
		// to determine the parent section we inserting the sentence
		// into as a child section.
		//Section *parent = NULL;
		int32_t     start  = -1;
		Section *pp;
		//Section *np;
		int32_t     lastk = 0;
		Section *splitSection = NULL;
		Section *lastGuy = NULL;

		for ( int32_t k = senta ; k <= sentb ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// add final piece
			if ( k == sentb ) {
				// stop i no final piece
				if ( start == -1 ) break;
				// otherwise, add it
				goto addit;
			}
			// need a real alnum word
			if ( ! m_wids[k] ) continue;
			// get his parent
			pp = m_sectionPtrs[k];
			// set parent if need to
			//if ( ! parent ) parent = pp;
			// and start sentence if need to
			if ( start == -1 ) start = k;
			// if same as exact section as last guy, save some time
			if ( pp == lastGuy ) pp = NULL;
			// store it
			lastGuy = pp;
			// . i'd say blow up "pp" until its contains "start"
			// . but if before it contains start it breaches
			//   [senta,sentb) then we have to cut things int16_t
			for ( ; pp ; pp = pp->m_parent ) {
				// breathe
				QUICKPOLL(m_niceness);
				// we now have to split section "pp"
				// when adding the sentence section.
				// once we have such a section we
				// cannot use a different parent...
				if ( pp->m_firstWordPos < start ||
				     pp->m_lastWordPos >= sentb ) {
					// set it
					if ( ! splitSection ) splitSection =pp;
					// WE ARE ONLY ALLOWED TO SPLIT ONE
					// SECTION ONLY...
					if ( pp != splitSection)
						goto addit;
					break;
				}
				// keep telescoping until "parent" contains
				// [senta,k] , and we already know that it
				// contains k because that is what we set it to
				//if ( pp->m_a <= senta ) break;
			}
			// mark it
			if ( m_wids[k] ) lastk = k;
			// ok, keep chugging
			continue;

			// add the final piece if we go to this label
		addit:
			// use this flag
			int32_t bh = BH_SENTENCE;
			// determine parent section, smallest section 
			// containing [start,lastk]
			Section *parent = m_sectionPtrs[start];
			for ( ; parent ; parent = parent->m_parent ) {
				// breathe
				QUICKPOLL(m_niceness);
				// stop if contains lastk
				if ( parent->m_b > lastk ) break;
			}
			// 
			// for "<span>Albuquerque</span>, New Mexico" 
			// "start" points to "Albuquerque" but needs to 
			// point to the "<span>" so its parent is "parent"
			int32_t adda = start;
			int32_t addb = lastk;
			// need to update "start" to so its parent is the new 
			// "parent" now so insertSubSection() does not core
			for ( ; adda >= 0 ; ) {
				// breathe
				QUICKPOLL(m_niceness);
				// stop if we finally got the right parent
				if ( m_sectionPtrs[adda]==parent ) break;
				// or if he's a tag and his parent
				// is "parent" we can stop.
				// i.e. STOP on a proper subsection of
				// the section containing the sentence.
				if ( m_sectionPtrs[adda]->m_parent==parent &&
				     m_sectionPtrs[adda]->m_a == adda )
					break;
				// backup
				adda--;
				// check
				if ( adda < 0 ) break;
				// how can this happen?
				if ( m_wids[adda] ) { char *xx=NULL;*xx=0; }
			}
			// sanity
			if ( adda < 0 ) { char *xx=NULL;*xx=0; }

			// backup addb over any punct we don't need that
			//if ( addb > 0 && addb < m_nw && 
			//     ! m_wids[addb] && ! m_tids[addb] ) addb--;

			// same for right endpoint
			for ( ; addb < m_nw ; ) {
				// breathe
				QUICKPOLL(m_niceness);
				// stop if we finally got the right parent
				if ( m_sectionPtrs[addb]==parent ) break;
				// get it
				Section *sp = m_sectionPtrs[addb];
				// come back up here in the case of a section
				// sharing its Section::m_b with its parent
			subloop:
				// or if he's a tag and his parent
				// is "parent" we can stop
				if ( sp->m_parent==parent &&
				     sp->m_b == addb+1 )
					break;
				// or if we ran into a brother section
				// that does not contain the sentence...
				// fix core dump for webnetdesign.com whose
				// sentence consisted of 3 sections from
				// A=7079 to B=7198. but now i am getting rid
				// of allowing a lower case http(s):// on
				// a separate line to indicate that the
				// sentence continues... so we will not have
				// this sentence anymore in case  you are
				// wondering why it is not there any more.
				if ( sp->m_parent==parent &&
				     sp->m_a == addb ) {
					// do not include that brother's tag
					addb--;
					break;
				}

				// when we have bad tag formations like for
				// http://gocitykids.parentsconnect.com/catego
				// ry/buffalo-ny-usa/places-to-go/tourist-stops
				// like <a><b>...</div> with no ending </a> or
				// </b> tags then we have to get the parent
				// of the parent as int32_t as its m_b is the
				// same and check that before advancing addb
				// otherwise we can miss the parent section
				// that we want! (this is because the kid
				// sections share the same m_b as their 
				// parent because of they have no ending tag)
				if ( sp->m_parent &&
				     sp->m_parent->m_b == sp->m_b ) {
					sp = sp->m_parent;
					goto subloop;
				}

				// advance
				addb++;
				// stop if addb 
				if ( addb >= m_nw ) break;
				// how can this happen?
				if ( m_wids[addb] ) { char *xx=NULL;*xx=0; }
			}
			// sanity
			if ( addb >= m_nw ) { char *xx=NULL;*xx=0; }

			// ok, now add the split sentence
			Section *is =insertSubSection(parent,adda,addb+1,bh);
			// panic?
			if ( ! is ) return false;
			// set sentence flag on it
			is->m_flags |= SEC_SENTENCE;
			// count it
			m_numSentenceSections++;
			// print it out
			/*
			SafeBuf tt;
			tt.safeMemcpy(m_wptrs[adda],
				      m_wptrs[addb]+m_wlens[addb]-
					      m_wptrs[adda]);
			tt.safeReplace2 ( "\n",1,"*",1,m_niceness);
			tt.safeReplace2 ( "\r",1,"*",1,m_niceness);
			if ( is->m_flags & SEC_SPLIT_SENT )
				tt.safePrintf(" [split]");
			tt.pushChar(0);
			fprintf(stderr,"a=%"INT32" %s\n",start,tt.m_buf);
			*/
			// . set this
			// . sentence is from [senta,sentb)
			is->m_senta = senta;//start;
			is->m_sentb = sentb;//k;
			// use this too if its a split of a sentence
			if ( is->m_senta < is->m_a ) 
				is->m_flags |= SEC_SPLIT_SENT;
			if ( is->m_sentb > is->m_b ) 
				is->m_flags |= SEC_SPLIT_SENT;
			// stop if that was it
			if ( k == sentb ) break;
			// go on to next fragment then
			start = -1;
			parent = NULL;
			splitSection = NULL;
			lastGuy = NULL;
			// redo this same k
			k--;
		}
	}

	int32_t     inSentTil = 0;
	Section *lastSent = NULL;
	// get the section of each word. if not a sentence section then
	// make its m_sentenceSection point to its parent that is a sentence
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need sentence
		if ( ( sk->m_flags & SEC_SENTENCE ) ) {
			inSentTil = sk->m_b;
			lastSent  = sk;
			sk->m_sentenceSection = sk;
			continue;
		}
		// skip if outside of the last sentence we had
		if ( sk->m_a >= inSentTil ) continue;
		// we are in that sentence
		sk->m_sentenceSection = lastSent;
	}

	return true;

	/*
		// set this
		Section *firstSection = m_sectionPtrs[i];
		Section *lastSection  = m_sectionPtrs[j-1];

		// blow up before containing each other
		for ( ; firstSection ; ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if too much
			Section *pp = firstSection->m_parent;
			// stop if too much
			if ( ! pp ) break;
			// stop if too much
			if ( pp->contains ( lastSection ) ) break;
			// advance
			firstSection = pp;
		}
		for ( ; lastSection ; ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if too much
			Section *pp = lastSection->m_parent;
			// stop if too much
			if ( ! pp ) break;
			// stop if too much
			if ( pp->contains ( firstSection ) ) break;
			// advance
			lastSection = pp;
		}

		// if last section overflowed, cut if off
		if ( lastSection != firstSection &&
		     // make sure it doesn't just contain the front part
		     // of the sentence as well as firstSection!
		     lastSection->m_firstWordPos > i &&
		     lastSection->m_lastWordPos  > j ) {
			sentb = lastSection->m_a;
			// set a flag 
			// no need to do a loop

		// likewise, we can't split first section either, but in
		// that case, drop everything not in the first section
		if ( lastSection != firstSection &&
		     firstSection->m_firstWordPos < i &&
		     firstSection->m_lastWordPos < sentb )
			sentb = firstSection->m_b;

		// for reverbnation.com we have "Tools for <a>artists</a> |
		// <a>labels</a> | ..." and the first sentence contains
		// the anchor tag, but the 2nd sentence is a subsection of
		// the anchor tag. thus giving inconsistent tag hashes
		// and messing up our menu detection, so let's include
		// anchor tags in the sentence to fix that!
		// CRAP, this was hurting blackbirdbuvette because it was
		// including the bullet delimeter and the "big" section 
		// computed below ended up spanning two bullet delimeted
		// sections to make the sentence!! that gave us a bad event
		// title that consisted of that sentence.. 
		//if ( j < m_nw && ! m_tids[j] ) sentb++;

		// sent section needs to include the initial <a>, somehow
		// that is flip flopping
		//Section *sa = m_sectionPtrs[senta];
		//if ( m_tids[senta] && sa->contains ( senta, sentb ) )
		//	senta++;

		// A = m_sectionPtrs[senta] and
		// B = m_sectionPtrs[sentb-1]
		// find the smallest section that contains both A and B.
		// if A and B are the same, we are done, we are just
		// a child if that section.
		// . telescope both A and B up until they contain
		//   each other like our lasta and lastb algo !!

		Section *big = m_sectionPtrs[senta];
		for ( ; big ; big = big->m_parent ) {
			QUICKPOLL(m_niceness);
			// stop when we contain the whole sentence
			if ( big->m_a <= senta &&
			     big->m_b >= sentb ) break;
		}

		Section *sa = m_sectionPtrs[senta];
		Section *sb = m_sectionPtrs[sentb-1];

		// get the two sections that are children of "big" and cover
		// the first and last words of the sentence.
		Section *lasta = NULL;
		Section *lastb = NULL;
		// blow up sb until it contains senta, but its section right
		// before it equals "big"
		for ( ; sb ; sb = sb->m_parent ) {
			if ( sb == big ) break;
			lastb = sb;
		}
		for ( ; sa ; sa = sa->m_parent ) {
			if ( sa == big ) break;
			lasta = sa;
		}

		// set some simple constraints on [senta,sentb) so that we
		// do no split the sections lasta and lastb. we need our new
		// inserted section to contain "lasta" and "lastb" but be
		// a child of "big"
		int32_t maxa ;
		int32_t minb ;
		// but if these are the same as big
		if ( ! lasta ) maxa = senta;
		else           maxa = lasta->m_a;
		if ( ! lastb ) minb = sentb;
		else           minb = lastb->m_b;

		// save for debug
		//int32_t saveda = senta;
		//int32_t savedb = sentb;

		// apply the constraints
		if ( senta > maxa ) senta = maxa;
		if ( sentb < minb ) sentb = minb;

		// ok, add the sentence sections as a subsection of "sa"
		Section *is = insertSubSection ( big , senta , sentb,
						 BH_SENTENCE );
		// breathe
		QUICKPOLL ( m_niceness );
		// return false with g_errno set on error
		if ( ! is ) return false;
		// set sentence flag on it
		is->m_flags |= SEC_SENTENCE;
		// make it fake to now
		//is->m_flags |= SEC_FAKE;
		// set its base hash to something special
		//is->m_baseHash = BH_SENTENCE;
		// skip over
		//i = j - 1;
		i = sentb - 1;
	}
	return true;
		*/
}

Section *Sections::insertSubSection ( Section *parentArg , int32_t a , int32_t b ,
				      int32_t newBaseHash ) {
	// debug
	//log("sect: inserting subsection [%"INT32",%"INT32")",a,b);

	// try to realloc i guess. should keep ptrs in tact.
	if ( m_numSections >= m_maxNumSections )
		// try to realloc i guess
		if ( ! growSections() ) return NULL;
		//char *xx=NULL;*xx=0;}

	//
	// make a new section
	//
	Section *sk = &m_sections[m_numSections];
	// clear
	memset ( sk , 0 , sizeof(Section) );
	// inc it
	m_numSections++;
	// now set it
	sk->m_a   = a;
	sk->m_b   = b;

	// this makes it really fast!
	//return sk;

	// don't mess this up!
	if ( m_lastSection && a > m_lastSection->m_a )
		m_lastSection = sk;

	// the base hash (delimeter hash) hack
	sk->m_baseHash = 0;// dh; ????????????????????

	// do not resplit this split section with same delimeter!!
	sk->m_processedHash = 0; // ?????? dh;

	// get first section containing word #a
	Section *si = m_sectionPtrs[a];

	for ( ; si ; si = si->m_prev ) {
		// breathe
		QUICKPOLL(m_niceness);
		// we become his child if this is true
		if ( si->m_a < a ) break;
		// if he is bigger (or equal) we become his child
		// and are after him
		if ( si->m_a == a && si->m_b >= b ) break;
	}

	// . try using section before us if it is contained by "si"
	// . like in the case when word #a belongs to the root section
	//   and there are thousands of child sections of the root before "a"
	//   we really want to get the child section of the root before us
	//   as the prev section, "si", otherwise the 2nd for loop below here
	//   will hafta loop through thousands of sibling sections
	// . this will fail if word before a is part of our same section
	// . what if we ignored this for now and set m_sectionPtrs[a] to point
	//   to the newly inserted section, then when done adding sentence
	//   sections we scanned all the words, keeping track of the last
	//   html section we entered and used that to insert the sentence sections
	if ( m_lastAdded && m_lastAdded->m_a > si->m_a && m_lastAdded->m_a < a )
		si = m_lastAdded;


	// crap we may have 
	// "<p> <strong>hey there!</strong> this is another sentence.</p>"
	// then "si" will be pointing at the "<p>" section, and we will
	// not get the "<strong>" section as the "prev" to sk, which we should!
	// that is where sk is the "this is another sentence." sentence
	// section. so to fix that try iterating over si->m_next to get si to
	// be closer to sk.
	for ( ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if no more eavailable
		if ( ! si->m_next ) break;
		// stop if would break
		if ( si->m_next->m_a > a ) break;
		// if it gets closer to us without exceeding us, use it
		if ( si->m_next->m_a < a ) continue;
		// if tied, check b. if it contains us, go to it
		if ( si->m_next->m_b >= b ) continue;
		// otherwise, stop
		break;
	}

	// set this
	m_lastAdded = si;

	// a br tag can split the very first base html tag like for
	// mapsandatlases.org we have
	// "<html>...</html> <br> ...." so the br tag splits the first
	// section!
	// SO we need to check for NULL si's!
	if ( ! si ) {
		// skip this until we figure it out
		m_numSections--;
		char *xx=NULL;*xx=0;
		return NULL;
		sk->m_next = m_rootSection;//m_rootSection;
		sk->m_prev = NULL;
		//m_sections[0].m_prev = sk;
		m_rootSection->m_prev = sk;
		m_rootSection = sk;
	}
	else {
		// insert us into the linked list of sections
		if ( si->m_next ) si->m_next->m_prev = sk;
		sk->m_next   = si->m_next;
		sk->m_prev   = si;
		si->m_next   = sk;
	}

	// now set the parent
	Section *parent = m_sectionPtrs[a];
	// expand until it encompasses both a and b
	for ( ; ; parent = parent->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		if ( parent->m_a > a ) continue;
		if ( parent->m_b < b ) continue;
		break;
	}
	// now we assign the parent to you
	sk->m_parent    = parent;
	sk->m_exclusive = parent->m_exclusive;
	// sometimes an implied section is a subsection of a sentence!
	// like when there are a lot of brbr (double br) tags in it...
	sk->m_sentenceSection = parent->m_sentenceSection;
	// take out certain flags from parent
	sec_t flags = parent->m_flags;
	// like this
	flags &= ~SEC_SENTENCE;
	flags &= ~SEC_SPLIT_SENT;
	// but take out unbalanced!
	flags &= ~SEC_UNBALANCED;

	// . remove SEC_HAS_DOM/DOW/TOD 
	// . we scan our new kids for reparenting to us below, so OR these
	//   flags back in down there if we should
	flags &= ~SEC_HAS_DOM;
	flags &= ~SEC_HAS_DOW;
	flags &= ~SEC_HAS_TOD;
	//flags &= ~SEC_HAS_DATE;

	// add in fake
	flags |= SEC_FAKE;
	// flag it as a fake section
	sk->m_flags = flags ;
	// need this
	sk->m_baseHash = newBaseHash;

	// reset these
	sk->m_firstWordPos = -1;
	sk->m_lastWordPos  = -1;
	sk->m_alnumPosA    = -1;
	sk->m_alnumPosB    = -1;
	sk->m_senta        = -1;
	sk->m_sentb        = -1;
	sk->m_firstPlaceNum= -1;
	sk->m_headColSection = NULL;
	sk->m_headRowSection = NULL;
	sk->m_tableSec       = NULL;
	sk->m_rowNum         = 0;
	sk->m_colNum         = 0;


#ifdef _DEBUG_SECTIONS_
	// interlaced section detection
	if ( m_isTestColl ) {
		// scan from words and telescope up
		Section *s1 = m_sectionPtrs[a];
		Section *s2 = m_sectionPtrs[b-1];
		// check for interlace
		for ( ; s1 ; s1 = s1->m_parent ) 
			if ( s1->m_a < a && 
			     s1->m_b > a &&
			     s1->m_b < b    ) {char *xx=NULL;*xx=0;}
		// check for interlace
		for ( ; s2 ; s2 = s2->m_parent ) 
			if ( s2->m_a < b && 
			     s2->m_b > b &&
			     s2->m_a > a    ) {char *xx=NULL;*xx=0;}
	}
#endif

	// try to keep a sorted linked list
	//Section *current = m_sectionPtrs[a];

	//return sk;

	// for inheriting flags from our kids
	sec_t mask = (SEC_HAS_DOM|SEC_HAS_DOW|SEC_HAS_TOD);//|SEC_HAS_DATE);

	//
	// !!!!!!!!!! SPEED THIS UP !!!!!!!!!!
	//

	// . find any child section of "parent" and make us their parent
	// . TODO: can later speed up with ptr to ptr logic
	// . at this point sections are not sorted so we can't
	//   really iterate linearly through them ... !!!
	// . TODO: speed this up!!!
	//
	// . TODO: use hashtable?
	// . TODO: aren't these sections in order by m_a??? could just use that
	//
	//for ( int32_t xx = 0 ; xx < m_numSections ; xx++ ) {

	// set sk->m_firstWordPos
	for ( int32_t i = a ; i < b ; i++ ) {
		// and first/last word pos
		if ( ! m_wids[i] ) continue;
		// mark this
		sk->m_firstWordPos = i;
		break;
	}

	// set sk->m_lastWordPos
	for ( int32_t i = b-1 ; i >= a ; i-- ) {
		// and first/last word pos
		if ( ! m_wids[i] ) continue;
		// mark this
		sk->m_lastWordPos = i;
		break;
	}


	//
	// to speed up scan the words in our inserted section, usually
	// a sentence section i guess, because our parent can have a ton
	// of children sections!!
	//
	for ( int32_t i = a ; i < b ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get current parent of that word
		Section *wp = m_sectionPtrs[i];
		// if sentence section does NOT contain the word's current
		// section then the sentence section becomes the new section
		// for that word.
		if ( ! sk->strictlyContains ( wp ) ) {
			// now if "wp" is like a root, then sk becomes the kid
			m_sectionPtrs[i] = sk;
			// our parent is wp
			sk->m_parent = wp;
			continue;
		}
		// we gotta blow up wp until right before it is bigger
		// than "sk" and use that
		for ( ; wp->m_parent ; wp = wp->m_parent )
			// this could be equal to, not just contains
			// otherwise we use strictlyContains()
			if ( wp->m_parent->contains(sk) ) break;
		// already parented to us?
		if ( wp->m_parent == sk ) continue;
		// sentence's parent is now wp's parent
		sk->m_parent = wp->m_parent;
		// and we become wp's parent
		wp->m_parent = sk;
		// and or his flags into us. SEC_HAS_DOM, etc.
		sk->m_flags |= wp->m_flags & mask;
		// sanity check
		if ( wp->m_b > sk->m_b ) { char *xy=NULL;*xy=0; }
		if ( wp->m_a < sk->m_a ) { char *xy=NULL;*xy=0; }
	}

	return sk;

	// start scanning here
	Section *start = parent->m_next;

	int32_t lastb = -1;
	// try just scanning sections in parent
	for ( Section *sx = start ; sx ; sx = sx->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		//Section *sx = &m_sections[xx];
 		// skip if section ends before our sentence begins
		if ( sx->m_b <= a ) continue;
		// stop if beyond sk
		if ( sx->m_a >= b ) break;
		// skip if sn not parent
		if ( sx->m_parent != parent ) continue;
		// when splitting a section do not reparent if
		// not in our split...
		//if ( sx->m_a >= b ) continue;
		// do not reparent if it contains us
		if ( sx->m_a <= a && sx->m_b >= b ) continue;
		// reset his parent to the newly added section
		sx->m_parent = sk;
		// and or his flags into us. SEC_HAS_DOM, etc.
		sk->m_flags |= sx->m_flags & mask;
		// sanity check
		if ( sx->m_b > sk->m_b ) { char *xy=NULL;*xy=0; }
		if ( sx->m_a < sk->m_a ) { char *xy=NULL;*xy=0; }
		// skip if already got the xor for this section
		if ( sx->m_a < lastb ) continue;
		// set this
		lastb = sx->m_b;
		// add all the entries from this child section from the
		// phone/email/etc. tables
		sk->m_phoneXor ^= sx->m_phoneXor;
		sk->m_emailXor ^= sx->m_emailXor;
		sk->m_priceXor ^= sx->m_priceXor;
		sk->m_todXor   ^= sx->m_todXor;
		sk->m_dayXor   ^= sx->m_dayXor;
		sk->m_addrXor  ^= sx->m_addrXor;
		// make sure did not make it zero
		if ( sx->m_phoneXor   && sk->m_phoneXor == 0 )
			sk->m_phoneXor    = sx->m_phoneXor;
		if ( sx->m_emailXor   && sk->m_emailXor == 0 )
			sk->m_emailXor    = sx->m_emailXor;
		if ( sx->m_priceXor   && sk->m_priceXor == 0 )
			sk->m_priceXor    = sx->m_priceXor;
		if ( sx->m_todXor     && sk->m_todXor == 0 )
			sk->m_todXor      = sx->m_todXor;
		if ( sx->m_dayXor     && sk->m_dayXor == 0 )
			sk->m_dayXor      = sx->m_dayXor;
		if ( sx->m_addrXor && sk->m_addrXor == 0 )
			sk->m_addrXor = sx->m_addrXor;
		// set this perhaps
		if ( sk->m_firstPlaceNum < 0 ) 
			sk->m_firstPlaceNum = sx->m_firstPlaceNum;
		// update this?
		if ( sx->m_alnumPosA < 0 ) continue;
		// take the first one we get
		if ( sk->m_alnumPosA == -1 ) 
			sk->m_alnumPosA = sx->m_alnumPosA;
		// update to the last one always
		sk->m_alnumPosB  = sx->m_alnumPosB;
	}

	// a flag
	bool needsFirst = true;
	// . set the words ptrs to it
	// . TODO: can later speed up with ptr to ptr logic
	for ( int32_t yy = a ; yy < b ; yy++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// and first/last word pos
		if ( m_wids[yy] ) {
			// mark this
			if ( needsFirst ) {
				sk->m_firstWordPos = yy;
				needsFirst = false;
			}
			// remember last
			sk->m_lastWordPos = yy;
		}
		// must have had sn as parent
		if ( m_sectionPtrs[yy] != parent ) continue;
		// "sk" becomes the new parent
		m_sectionPtrs[yy] = sk;
	}

	return sk;
}

// for brbr and hr splitting delimeters
int32_t Sections::splitSectionsByTag ( nodeid_t tagid ) {

	// . try skipping for xml
	// . eventbrite.com has a bunch of dates per event item and
	//   we end up using METHOD_DOM on those!
	// . i think the implied section algo is meant for html really
	//   or plain text
	if ( m_contentType == CT_XML &&
	     ( m_isEventBrite ||
	       m_isStubHub    ||
	       m_isFacebook ) )
		return 0;

	int32_t numAdded = 0;
	// . now, split sections up if they contain one or more <hr> tags
	// . just append some "hr" sections under that parent to m_sections[]
	// . need to update m_sectionPtrs[] after this of course!!!!!
	// . now we also support various other delimeters, like bullets
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be brbr or hr
		if ( ! isTagDelimeter ( si , tagid ) ) continue;
		// must have a next brother to be useful
		if ( ! si->m_nextBrother ) continue;
		// skip if already did this section
		if ( si->m_processedHash ) continue;
		// set first brother
		Section *first = si;
		for ( ; first->m_prevBrother ; first = first->m_prevBrother )
			// breathe
			QUICKPOLL(m_niceness);
		// save parent
		Section *parent = first->m_parent;

	subloop:
		// mark it
		first->m_processedHash = 1;

		// start of insertion section is right after tag
		int32_t a = first->m_b;

		// but if first is not a tag delimeter than use m_a
		if ( ! isTagDelimeter ( first , tagid ) ) a = first->m_a;

		// or if first section has text, then include that, like
		// in the case of h1 tags for example
		if ( first->m_firstWordPos >= 0 ) a = first->m_a;

		// end of inserted section is "b"
		int32_t b = -1;

		int32_t numTextSections = 0;
		// count this
		if ( first->m_firstWordPos >= 0 ) numTextSections++;
		// start scanning right after "first"
		Section *last = first->m_nextBrother;
		// set last brother and "b"
		for ( ; last ; last = last->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop on tag delimeters
			if ( isTagDelimeter ( last , tagid ) ) {
				// set endpoint of new subsection
				b = last->m_a;
				// and stop
				break;
			}
			// assume we are the end of the line
			b = last->m_b;
			// count this
			if ( last->m_firstWordPos >= 0 ) numTextSections++;
			// must be brbr or hr
			//if ( ! isTagDelimeter ( last , tagid ) ) continue;
			// got it, use the start of us being a splitter tag
			//b = last->m_a;
			// stop
			//break;
		}

		// . insert [first->m_b,b]
		// . make sure it covers at least one "word" which means
		//   that a != b-1
		if ( a < b - 1 &&
		     // and must group together something meaningful
		     numTextSections >= 2 ) {
			// do the insertion
			Section *sk = insertSubSection (parent,a,b,BH_IMPLIED);
			// error?
			if ( ! sk ) return -1;
			// fix it
			sk->m_processedHash = 1;
			// count it
			numAdded++;
		}

		// first is now last
		first = last;
		// loop up if there are more brothers
		if ( first ) goto subloop;
	}
	return numAdded;
}

bool Sections::splitSections ( char *delimeter , int32_t dh ) {

	// . try skipping for xml
	// . eventbrite.com has a bunch of dates per event item and
	//   we end up using METHOD_DOM on those!
	// . i think the implied section algo is meant for html really
	//   or plain text
	if ( m_contentType == CT_XML &&
	     ( m_isEventBrite ||
	       m_isStubHub    ||
	       m_isFacebook ) )
		return 0;

	// use this for ultimately setting Section::m_tagHash in loop above
	//int32_t dh ;
	//if ( delimeter == (char *)0x01 ) dh = 3947503;
	//else                             dh = hash32n ( delimeter );

	//int32_t th = hash32n("<br");

	// . is the delimeter a section starting tag itself?
	// . right now, just <hN> tags
	//bool delimIsSection = false;
	//if ( delimeter != (char *)0x01 && is_digit(delimeter[2] ) ) 
	//	delimIsSection = true;

	//int32_t ns = m_numSections;


	int32_t saved = -1;
	int32_t delimEnd = -1000;

	// . now, split sections up if they contain one or more <hr> tags
	// . just append some "hr" sections under that parent to m_sections[]
	// . need to update m_sectionPtrs[] after this of course!!!!!
	// . now we also support various other delimeters, like bullets
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		if ( i < saved ) { char *xx=NULL;*xx=0; }
		// a quicky
		if ( ! isDelimeter ( i , delimeter , &delimEnd ) ) continue;
		// get section it is in
		Section *sn = m_sectionPtrs[i];
		// save it
		//Section *origsn = sn;
		// for <h1> it must split its parent section!
		//if ( sn->m_a == i && delimIsSection )
		//	sn = sn->m_parent;
		// skip if already did this section
		if ( sn->m_processedHash == dh ) continue;
		/*
		// or if we are <br> and the double br got it
		if ( sn->m_processedHash == BH_BRBR && dh == BH_BR &&
		     // .  might have "<br><br> stuff1 <br> stuff2 <br> ..."
		     // . so need to be at the start of it to ignore it then
		     i >= sn->m_a && i <= sn->m_a+2 ) continue;
		*/
		// . or if we equal start of section
		// . it was created, not processed
		//if ( sn->m_a == i ) continue;
		// int16_tcut
		//Section *sk ;
		// what section # is section "sn"?
		int32_t offset = sn - m_sections;
		// sanity check
		if ( &m_sections[offset] != sn ) { char *xx=NULL;*xx=0; }
		// point to the section after "sn"
		//int32_t xx = offset + 1;
		// point to words in the new section
		//int32_t yy = sn->m_a ;
		// init this
		int32_t start = sn->m_a;
		// CAUTION: sn->m_a can equal "i" for something like:
		// "<div><h2>blah</h2> <hr> </div>"
		// where when splitting h2 sections we are at the start
		// of an hr section. i think its best to just skip it!
		// then if we find another <h2> within that same <hr> section
		// it can split it into non-empty sections
		if ( start == i ) continue;
		// save it so we can rescan from delimeter right after this one
		// because there might be more delimeters in DIFFERENT 
		// subsections
		saved = i;
		// sanity check
		//if ( start == i ) { char *xx=NULL;*xx=0; }

	subloop:
		// sanity check
		if ( m_numSections >= m_maxNumSections) {char *xx=NULL;*xx=0;}
		//
		// try this now
		//
		Section *sk = insertSubSection ( sn , start , i , dh );
		/*
		//
		// make a new section
		//
		sk = &m_sections[m_numSections];
		// inc it
		m_numSections++;
		// now set it
		sk->m_a = start;
		sk->m_b   = i;
		sk->m_parent    = sn;
		sk->m_exclusive = sn->m_exclusive;
		// the base hash (delimeter hash) hack
		sk->m_baseHash = dh;
		// sanity check
		if ( start == i ) { char *xx=NULL;*xx=0; }
		// flag it as an <hr> section
		sk->m_flags     = sn->m_flags | SEC_FAKE;
		// but take out unbalanced!
		sk->m_flags &= ~SEC_UNBALANCED;
		*/

		// and take out sentence as well FROM THE PARENT!
		//sn->m_flags &= ~SEC_SENTENCE;
		
		//sk->m_baseHash = dh;

		// do not resplit this split section with same delimeter!!
		if ( sk ) sk->m_processedHash = dh;

		// take out the processed flag
		//sk->m_flags &= ~SEC_PROCESSED;

		// quick hack sanity check
		//for ( int32_t w = 0 ; w < m_numSections ; w++ ){
		//for ( int32_t u = w+1 ; u < m_numSections ; u++ ) {
		//	if ( m_sections[w].m_a == m_sections[u].m_a &&
		//	     m_sections[w].m_b == m_sections[u].m_b ) {
		//		char *xx=NULL;*xx=0; }
		//}}

		/*
		// take out the SEC_NOTEXT flag if we should
		//if ( ! notext ) sk->m_flags &= ~SEC_NOTEXT;
		// . find any child section of "sn" and make us their parent
		// . TODO: can later speed up with ptr to ptr logic
		// . at this point sections are not sorted so we can't
		//   really iterate linearly through them ... !!!
		// . TODO: speed this up!!!
		for ( xx = 0 ; xx < ns ; xx++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// get it
			Section *sx = &m_sections[xx];
			// skip if sn not parent
			if ( sx->m_parent != sn ) continue;
			// when splitting a section do not reparent if
			// not in our split...
			if ( sx->m_a >= i     ) continue;
			if ( sx->m_b <= start ) continue;
			// reset his parent to the hr faux section
			sx->m_parent = sk;
		}
		// . set the words ptrs to it
		// . TODO: can later speed up with ptr to ptr logic
		for ( ; yy < i ; yy++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// must have had sn as parent
			if ( m_sectionPtrs[yy] != sn ) continue;
			// "sk" becomes the new parent
			m_sectionPtrs[yy] = sk;
		}
		*/
		// if we were it, no more sublooping!
		if ( i >= sn->m_b ) { 
			// sn loses some stuff
			sn->m_exclusive = 0;
			// resume where we left off in case next delim is
			// in a section different than "sn"
			i = saved; 
			// do not process any more delimeters in this section
			sn->m_processedHash = dh;
			//i = sn->m_b - 1;
			continue; 
		}

		// update values in case we call subloop
		start = i;

		// skip over that delimeter at word #i
		i++;

		// if we had back-to-back br tags make i point to word
		// after the last br tag
		if ( delimeter == (char *)0x01 ) i = delimEnd;

		// find the next <hr> tag, if any, stop at end of "sn"
		for ( ; i < m_nw ; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop at end of section "sn"
			if ( i >= sn->m_b ) break;
			// get his section
			Section *si = m_sectionPtrs[i];
			// delimeters that start their own sections must
			// grow out to their parent
			//if ( delimIsSection )
			//	si = si->m_parent;
			// ignore if not the right parent
			if ( si != sn ) continue; 
			// a quicky
			if ( isDelimeter ( i , delimeter , &delimEnd ) ) break;
		}

		// if we were it, no more sublooping!
		//if ( i >= sn->m_b ) { i = saved; continue; }

		// now add the <hr> section above word #i
		goto subloop;
	}
	return true;
}
/*
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Sections::...( ) {

	key128_t end   = g_datedb.makeEndKey   ( m_termId , 0 );

	// before looking up TitleRecs using Msg20, let's first consult
	// datedb to see if we got adequate data as to what sections
	// are the article sections

	int32_t minRecSizes = SECTIONDB_MINRECSIZES;
	// get the group this list is in
	uint32_t gid ;
	// split = false 
	gid = getGroupId ( RDB_SECTIONDB , (char *)&m_startKey, false );
	// we need a group # from the groupId
	int32_t split = g_hostdb.getGroupNum ( gid );
	// int16_tcut
	Msg0 *m = &m_msg0;

 loop:
	// get the list
	if ( ! m->getList ( -1                      , // hostId
			    0                       , // ip
			    0                       , // port
			    0                       , // maxCacheAge
			    false                   , // addToCache
			    RDB_SECTIONDB           , // was RDB_DATEDB
			    m_coll                  ,
			    &m_list                 ,
			    (char *)&m_startKey     ,
			    (char *)&end            ,
			    minRecSizes             ,
			    this                    ,
			    gotSectiondbListWrapper ,
			    m_niceness              , // MAX_NICENESS
			    // default parms follow
			    true  ,  // doErrorCorrection?
			    true  ,  // includeTree?
			    true  ,  // doMerge?
			    -1    ,  // firstHostId
			    0     ,  // startFileNum
			    -1    ,  // numFiles
			    30    ,  // timeout
			    -1    ,  // syncPoint
			    -1    ,  // preferLocalReads
			    NULL  ,  // msg5
			    NULL  ,  // msg5b
			    false ,  // isrealmerge?
			    true  ,  // allowpagecache?
			    false ,  // forceLocalIndexdb?
			    false ,  // doIndexdbSplit?
			    split ))
		return false;

	// record recall
	bool needsRecall = false;
	// return false if this blocked
	if ( ! gotSectiondbList ( &needsRecall ) ) return false;
	// if it needs a recall then loop back up!
	if ( needsRecall ) goto loop;
	// we are done
	return true;
}

void gotSectiondbListWrapper ( void *state ) {
	SectionVotingTable *THIS = (Sections *)state;
	// record recall
	bool needsRecall = false;
	// return if this blocked
	if ( ! THIS->gotSectiondbList( &needsRecall ) ) return;
	// needs a recall?
	if ( needsRecall && ! THIS->getSectiondbList() ) return;
	// nothing blocked and no recall needed, so we are done
	THIS->m_callback ( THIS->m_state );
}
*/

// returns false and sets g_errno on error
bool Sections::addVotes ( SectionVotingTable *nsvt , uint32_t tagPairHash ) {

	// . add our SV_TAG_PAIR hash to the m_nsvt
	// . every page adds this one
	// . but don't screw up m_nsvt if we are deserializing from it
	if ( ! nsvt->addVote2(tagPairHash,SV_TAGPAIRHASH,1)) 
		return false;

	// . add our checksum votes as well
	// . we use this for menu identification between web pages
	for ( Section *sn = m_firstSent ; sn ; sn = sn->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// sanity check
		if ( ! sn->m_sentenceContentHash64 ) { char *xx=NULL;*xx=0; }
		// add the tag hash too!
		if ( ! nsvt->addVote3 ( sn->m_turkTagHash32   ,
					SV_TURKTAGHASH        , 
					1.0               ,   // score
					1.0               ,
					true              ) )
			return false;
		// skip if not a section with its own words
		//if ( sn->m_flags & SEC_NOTEXT ) continue;
		// . combine the tag hash with the content hash #2
		// . for some reason m_contentHash is 0 for like menu-y sectns
		int32_t modified = sn->m_turkTagHash32;
		modified ^= sn->m_sentenceContentHash64;
		// now we use votehash32 which ignores dates and numbers
		//int32_t modified = sn->m_turkTagHash32 ^ sn->m_voteHash32;
		// . update m_nsvt voting table now
		// . the tagHash is the content hash for this one
		// . this will return false with g_errno set
		if ( ! nsvt->addVote3 ( modified          ,
					SV_TAGCONTENTHASH , 
					1.0               ,   // score
					1.0               ,
					true              ) )
			return false;
	}

	// success!
	return true;
}
	
SectionVotingTable::SectionVotingTable ( ) {
	m_totalSiteVoters     = 0;
	//m_totalSimilarLayouts = 0;
}

// . returns false if blocked, returns true and sets g_errno on error
// . compile the datedb list into a hash table
// . we serialize this hash table into TitleRec::ptr_sectionsData during
//   the reindexing process to ensure consistency
//bool Sections::gotSectiondbList ( bool *needsRecall ) {
bool SectionVotingTable::addListOfVotes ( RdbList *list, 
					  key128_t **lastKey ,
					  uint32_t tagPairHash ,
					  int64_t myDocId ,
					  int32_t niceness ) {

	int64_t lastDocId = 0LL;
	int64_t lastsh48 = 0LL;

	// . tally the votes
	// . the "flags" are more significant than the tag hash because
	//   we don't want a single tag hash dominating our list, even though
	//   that can still happen!
	// . TODO: limit indexing of taghashes that are well represented!
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// breathe
		QUICKPOLL ( niceness );
		// get rec
		char *rec = list->getCurrentRec();
		// get the key ptr, key128_t
		*lastKey = (key128_t *)rec;
		// for ehre
		key128_t *key = (key128_t *)rec;
		// the score is the bit which is was set in Section::m_flags
		// for that docid
		int32_t secType = g_indexdb.getScore ( (char *)key );
		// treat key like a datedb key and get the taghash
		uint32_t turkTagHash32 = g_datedb.getDate ( key );
		// get this
		int64_t d = g_datedb.getDocId(key);
		// skip this vote if from an old titlerec of ourselves!
		if ( d == myDocId ) continue;
		// sanity
		int64_t sh48 = g_datedb.getTermId ( key );
		if ( sh48 != lastsh48 && lastsh48 ) { char *xx=NULL;*xx=0;}
		lastsh48 = sh48;

		// now for every docid we index one sectiondb key with a 
		// tagHash of zero so we can keep tabs on how many section
		// voters we have. we need to limit the # of voters per site
		// otherwise we have a huge sectiondb explosion.
		if ( turkTagHash32 == 0 ) {
			// now that we do compression we strip these votes
			// in Msg0.cppp
			//char *xx=NULL;*xx=0; 
			m_totalSiteVoters++;
			//log("sect: got site voter");
			// sanity check - make sure only one per docid
			if ( d == lastDocId ) { char *xx=NULL;*xx=0; }
			lastDocId = d;
			continue;
		}
		// . the enum tag hash is the tag pair hash here as well
		// . let's us know who is waiting for a quick respider once
		//   enough docs from this site and with this particular
		//   tag pair hash (similar page layout) have been indexed
		//   and had their votes added to sectiondb
		// . then we can make better decisions as to what sections
		//   are repeated or menu or comment sections by comparing
		//   to these other documents
		//if ( secType == SV_WAITINLINE ) {
		//	// only count him if he is our same tag layout
		//	if ( tagHash == m_tagPairHash ) m_numLineWaiters++;
		//	// do not add this vote to m_osvt, it is special
		//	continue;
		//}
		// if we do not have this in our list of tagHashes then
		// skip it. we do not want votes for what we do not have.
		// this would just bloat m_osvt which we serialize into
		// TitleRec::ptr_sectionsData
		//if ( ! m_ot.isInTable ( &tagHash ) ) continue;
		// get data/vote from the current record in the sectiondb list
		SectionVote *sv = (SectionVote *)list->getCurrentData ( );
		// get the avg score for this docid
		float avg = 0;
		if ( sv->m_numSampled > 0 ) 
			avg = sv->m_score / sv->m_numSampled;
		//
		// PERFORMANCE HACK
		//
		// now we compile large sectiondb termlists in msg0.cpp
		// which means we fold keys of the same tag type and
		// hash together and increment their 
		// "SectionVote::m_numSampled" so that for the case of
		// tags of type contenthash and taghash the m_numSampled
		// value is the NUMBER OF DOCUMENTS that have that
		// contenthash/taghash... but for the other tag types it is
		// just the number of tags in that one document that have
		// that contenthash/taghash.... otherwise, before this hack
		// we were getting HUGE termlists for a handful of sites
		// totally slowing us down when rebuilding the index using
		// the "repair" tab. 
		//
		float numSampled = sv->m_numSampled;

		// incorporate this vote into m_osvt. the "old" voting table.
		if ( ! addVote3 ( turkTagHash32    ,
				  secType          ,
				  avg              ,  // score
				  numSampled       )) // numSampled
			return true;
	}

	// we are done
	return true;
}

// this is a function because we also call it from addImpliedSections()!
void Sections::setNextBrotherPtrs ( bool setContainer ) {

	// clear out
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// no! when adding implied sections we need to recompute
		// brothers for these tags to avoid an infinite loop in
		// adding implied sections forever
		// allow columns to keep theirs! since we set in
		// swoggleTable() below
		//if ( si->m_tagId == TAG_TC ) continue;
		// or if a swoggled table cell
		//if ( si->m_tagId == TAG_TH ) continue;
		//if ( si->m_tagId == TAG_TD ) continue;
		si->m_nextBrother = NULL;
		si->m_prevBrother = NULL;
	}


	//for ( int32_t i = 0 ; i + 1 < m_numSections ; i++ ) {
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip column sections. their m_a and m_b overlap with
		// one another which triggers a seg fault below... and
		// we already set their m_next/prevBrother members in the
		// swoggleTable() function
		//if ( si->m_tagId == TAG_TC ) continue;
		// int16_tcut
		//Section *si = &m_sections[i];
		// assume none
		//si->m_nextBrother = NULL;
		// crap this is overwriting our junk
		//si->m_prevBrother = NULL;
		// . skip if not a section with its own words
		// . no, i need this for Events.cpp::getRegistrationTable()
		//if ( si->m_flags & SEC_NOTEXT ) continue;
		// must be a menu
		//if ( ! ( si->m_flags & SEC_DUP ) ) continue;
		// must NOT have a unique tag hash
		//if ( si->m_numOccurences <= 1  ) continue;
		//Section *sj = m_sectionPtrs[wn];
		Section *sj = NULL;

		// get word after us
		int32_t wn = si->m_b;
		int32_t nw2 = m_nw;

		// if we hit a word in our parent.. then increment wn
		// PROBLEM "<root><t1>hey</t1> blah blah blah x 1 mill</root>"
		// would exhaust the full word list when si is the "t1"
		// section. 
		Section *j2 = si->m_next;
		if ( j2 && j2->m_a >= si->m_b ) {
			sj = j2;
			nw2 = 0;
		}

		// try one more ahead for things like so we don't end up
		// setting sj to the "t2" section as in:
		// "<root><t1><t2>hey</t2></t1> ...."
		if ( ! sj && j2 ) {
			// try the next section then
			j2 = j2->m_next;
			// set "sj" if its a potential brother section
			if ( j2 && j2->m_a >= si->m_b ) {
				sj = j2;
				nw2 = 0;
			}
		}

		// ok, try the next word algo approach
		for ( ; wn < nw2 ; wn++ ) {
			QUICKPOLL(m_niceness);
			sj = m_sectionPtrs[wn];
			if ( sj->m_a >= si->m_b ) break;
		}
		// bail if none
		if ( wn >= m_nw ) continue;

		//
		// NO! this is too slow!!!!! use the above loop!!!!
		// yeah, in certain cases it is horrible... like if si
		// is the root! then we gotta loop through all sections here
		// until sj is NULL!
		//
		// try using sections, might be faster and it is better
		// for our table column sections which have their td
		// kids in an interlaced ordering
		//for ( sj = si->m_next; sj ; sj = sj->m_next ) 
		//	if ( sj->m_a >= si->m_b ) break;

		// telescope up until brother if possible
		for ( ; sj ; sj = sj->m_parent )
			if ( sj->m_parent == si->m_parent ) break;
		// give up?
		if ( ! sj || sj->m_parent != si->m_parent ) continue;
		// sanity check
		if ( sj->m_a < si->m_b && 
		     sj->m_tagId != TAG_TC &&
		     si->m_tagId != TAG_TC ) {
			char *xx=NULL;*xx=0; }
		// set brother
		si->m_nextBrother = sj;
		// set his prev then
		sj->m_prevBrother = si;
		// sanity check
		if ( sj->m_parent != si->m_parent ) { char *xx=NULL;*xx=0; }
		// sanity check
		if ( sj->m_a < si->m_b &&
		     sj->m_tagId != TAG_TC &&
		     si->m_tagId != TAG_TC ) { 
			char *xx=NULL;*xx=0; }
		// do more?
		if ( ! setContainer ) continue;
		// telescope this
		Section *te = sj;
		// telescope up until it contains "si"
		for ( ; te && te->m_a > si->m_a ; te = te->m_parent );
		// only update list container if smaller than previous
		if ( ! si->m_listContainer )
			si->m_listContainer = te;
		else if ( te->m_a > si->m_listContainer->m_a )
			si->m_listContainer = te;
		if ( ! sj->m_listContainer )
			sj->m_listContainer = te;
		else if ( te->m_a > sj->m_listContainer->m_a )
			sj->m_listContainer = te;

		// now 
	}
}


void Sections::setNextSentPtrs ( ) {

	// kinda like m_rootSection
	m_firstSent = NULL;
	m_lastSent  = NULL;

	Section *finalSec = NULL;
	Section *lastSent = NULL;
	// scan the sentence sections and number them to set m_sentNum
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// record final section
		finalSec = sk;
		// set this
		sk->m_prevSent = lastSent;
		// need sentence
		if ( ! ( sk->m_flags & SEC_SENTENCE ) ) continue;
		// first one?
		if ( ! m_firstSent ) m_firstSent = sk;
		// we are the sentence now
		lastSent = sk;
	}
	// update
	m_lastSent = lastSent;
	// reset this
	lastSent = NULL;
	// now set "m_nextSent" of each section
	for ( Section *sk = finalSec ; sk ; sk = sk->m_prev ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// set this
		sk->m_nextSent = lastSent;
		// need sentence
		if ( ! ( sk->m_flags & SEC_SENTENCE ) ) continue;
		// we are the sentence now
		lastSent = sk;
	}
}

// returns false and sets g_errno on error
bool SectionVotingTable::addVote3 ( int32_t        turkTagHash ,
				    int32_t        sectionType ,
				    float       score       ,
				    float       numSampled  ,
				    bool        hackFix     ) {
	
	HashTableX *ttt = &m_svt;
	// or in bitnum
	//int64_t vk = turkTagHash;
	uint64_t vk = sectionType;
	// make room for bitnum
	vk <<= 32;
	// combine with turkTagHash
	vk |= (uint32_t)turkTagHash;
	//vk |= sectionType;

	// sanity
	if ( sectionType < 0 ) { char *xx=NULL;*xx=0; }

	// print out for debug
	//log("section: adding vote #%"INT32") th=%"UINT32"-%"INT32"",
	//    m_numVotes++,tagHash,sectionType);
	
	// get existing vote statistics for this vk from hash table
	int32_t slot = ttt->getSlot ( &vk );
	// return true with g_errno set on error
	if ( slot < 0 ) {
		SectionVote nv;
		nv.m_score       = score;
		nv.m_numSampled  = numSampled;
		return ttt->addKey ( &vk , &nv );
	}
	// get the current voting statistics in table
	SectionVote *sv = (SectionVote *)ttt->getValueFromSlot ( slot );

	// incorporate this voter's voting info
	sv->m_score      += score;
	sv->m_numSampled += numSampled;

	// keep this to one
	if ( hackFix )
		sv->m_numSampled = 1;

	return true;
}

/*
// . no longer use single bit flags, sec_t
// . just use enumerated section types now
// . each section type has a score and number sampled to get that score
// . returns -1 if no data
// . otherwise returns score from 0.0 to 1.0 which is probability that
//   sections with the given tagHash are of type "sectionType" where
//   "sectionType" is like SEC_TEXTY, SEC_DUP, SEC_CLOCK, etc.
float SectionVotingTable::getScore ( int32_t turkTagHash , int32_t sectionType ) {
	// make the vote key
	int64_t vk = ((uint64_t)sectionType) << 32 | (uint32_t)turkTagHash;
	//int64_t vk = ((uint64_t)tagHash) << 32 | (uint32_t)sectionType;
	// get these
	SectionVote *sv = (SectionVote *)m_svt.getValue ( &vk );
	//SectionVote *osv = (SectionVote *)m_osvt.getValue ( &vk );
	//SectionVote *nsv = (SectionVote *)m_nsvt.getValue ( &vk );
	// return -1.0 if no voting data for this guy
	//if ( ! osv && ! nsv ) return -1.0;
	if ( ! sv ) return -1.0;
	// combine
	float score      = 0.0;
	float numSampled = 0.0;
	if ( sv ) score      += sv->m_score;
	if ( sv ) numSampled += sv->m_numSampled;
	// . only count ourselves as one sample
	// . in the "old" voting table a single sample is a doc, as opposed
	//   to in the new voting table where we have one sample count every
	//   time the turkTagHash or contentHash occurs on the page.
	//if ( nsv ) numSampled++;
	// and use an average for the score
	//if ( nsv ) score += nsv->m_score / nsv->m_numSampled;
	// sanity check
	if ( numSampled <= 0.0 ) { char *xx=NULL;*xx=0; }
	// normalize
	return score / numSampled;
}
*/

/*
float SectionVotingTable::getOldScore ( Section *sn , int32_t sectionType ) {
	// make the vote key
	int64_t vk = ((uint64_t)sectionType) << 32|(uint32_t)sn->m_tagHash;
	//int64_t vk=((uint64_t)sn->m_tagHash) << 32 | (uint32_t)sectionType;
	// get these
	SectionVote *osv = (SectionVote *)m_osvt.getValue ( &vk );
	// return -1.0 if no voting data for this guy
	if ( ! osv ) return -1.0;
	// combine
	float score      = 0.0;
	float numSampled = 0.0;
	if ( osv ) score += osv->m_score;
	if ( osv ) numSampled += osv->m_numSampled;
	// sanity check
	if ( numSampled <= 0.0 ) { char *xx=NULL;*xx=0; }
	// normalize
	return score / numSampled;
}

float SectionVotingTable::getNewScore ( Section *sn , int32_t sectionType ) {
	// make the vote key
	int64_t vk = ((uint64_t)sectionType) << 32|(uint32_t)sn->m_tagHash;
	//int64_t vk=((uint64_t)sn->m_tagHash) << 32 | (uint32_t)sectionType;
	// get these
	SectionVote *nsv = (SectionVote *)m_nsvt.getValue ( &vk );
	// return -1.0 if no voting data for this guy
	if ( ! nsv ) return -1.0;
	// combine
	float score      = 0.0;
	float numSampled = 0.0;
	if ( nsv ) score      += nsv->m_score;
	if ( nsv ) numSampled += nsv->m_numSampled;
	// sanity check
	if ( numSampled <= 0.0 ) { char *xx=NULL;*xx=0; }
	// normalize
	return score / numSampled;
}
*/

// just like getScore() above basically
float SectionVotingTable::getNumSampled ( int32_t turkTagHash, int32_t sectionType) {
	// make the vote key
	int64_t vk = ((uint64_t)sectionType) << 32 | (uint32_t)turkTagHash;
	//int64_t vk = ((uint64_t)tagHash) << 32 | (uint32_t)sectionType;
	// get these
	SectionVote *sv = (SectionVote *)m_svt.getValue ( &vk );
	//SectionVote *osv = (SectionVote *)m_osvt.getValue ( &vk );
	//SectionVote *nsv = (SectionVote *)m_nsvt.getValue ( &vk );
	// return 0.0 if no voting data for this guy
	//if ( ! osv && ! nsv ) return 0.0;
	if ( ! sv ) return 0.0;
	// combine
	float numSampled = 0.0;
	if ( sv ) numSampled += sv->m_numSampled;
	// . only count ourselves as one sample
	// . in the "old" voting table a single sample is a doc, as opposed
	//   to in the new voting table where we have one sample count every
	//   time the tagHash or contentHash occurs on the page.
	//if ( nsv ) numSampled++;
	// sanity check
	if ( numSampled <= 0.0 ) { char *xx=NULL;*xx=0; }
	// normalize
	return numSampled;
}

/*
// just like getScore() above basically
float SectionVotingTable::getOldNumSampled ( Section *sn , int32_t sectionType ) {
	// make the vote key
	int64_t vk=((uint64_t)sectionType) << 32 | (uint32_t)sn->m_tagHash;
	//int64_t vk=((uint64_t)sn->m_tagHash) << 32 | (uint32_t)sectionType;
	// get these
	SectionVote *osv = (SectionVote *)m_osvt.getValue ( &vk );
	// return 0.0 if no voting data for this guy
	if ( ! osv ) return 0.0;
	// combine
	float numSampled = 0.0;
	if ( osv ) numSampled += osv->m_numSampled;
	// sanity check
	if ( numSampled <= 0.0 ) { char *xx=NULL;*xx=0; }
	// normalize
	return numSampled;
}
*/
/*
// . returns false if no article
// . otherwise sets a and b to the range in word #'s and returns true
void Sections::getArticleRange ( int32_t *start , int32_t *end ) {
	// assume no section
	*start = -1;
	*end   = -1;
	// if no article, skip all!
	if ( ! m_hadArticle ) return ;
	// return if we got it
	if ( m_articleEndWord != -2 ) {
		*start = m_articleStartWord;
		*end   = m_articleEndWord;
		return;
	}
	int32_t a   = -1;
	int32_t b   = -1;
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Section *sn = &m_sections[i];
		// skip if not article section
		if ( ! ( sn->m_flags & SEC_ARTICLE ) ) continue;
		// keep a range going
		if ( sn->m_a < a || a == -1 ) a = sn->m_a;
		// and the end of the range
		if ( sn->m_b   > b || b == -1 ) b = sn->m_b;
	}
	// set them
	m_articleStartWord = a;
	m_articleEndWord   = b;
	// sanity check. if m_hadArticle was set, we should have something
	if ( (a == -1 || b == -1) && m_hadArticle ) { char *xx=NULL;*xx=0; }
	// set the range to return it
	*start = m_articleStartWord;
	*end   = m_articleEndWord;
}
*/

// . store our "votes" into datedb
// . each vote key is:
//   siteAndTagPairHash(termId)|tagHash(date)|secTypeEnum(score)|docId
// . to index a tag hash the section's flags must NOT have SEC_NOTEXT set.
// . TODO: do not index a key if the secTypeEnum (section type) is already well
//   represented in list we read from datedb
// . returns false and sets g_errno on error
// . returns true on success
bool SectionVotingTable::hash ( int64_t docId , 
				HashTableX *dt ,
				uint64_t siteHash64 ,
				int32_t niceness ) {

	// let's try skipping this always for now and going without using
	// sectiondb. we do not use it for the test parser anyway and we
	// seem to do ok... but verify that!
	//return true; 

	// . do not index more recs to sectiondb if we have enough!
	// . this is now in XmlDoc.cpp
	//if ( m_totalSiteVoters >= MAX_SITE_VOTERS ) {
	//     //log("sect: got %"INT32" site voters. skipping.",m_totalSiteVoters);
	//	return true;
	//}

	// sanity check
	if ( dt->m_ks != sizeof(key128_t)    ) { char *xx=NULL;*xx=0; }
	if ( dt->m_ds != sizeof(SectionVote) ) { char *xx=NULL;*xx=0; }
	// sanity check
	if ( getKeySizeFromRdbId ( RDB_SECTIONDB ) != 16 ){char*xx=NULL;*xx=0;}
	// if we did not have enough voters from our site with our same
	// page layout, then wait in line. when we get enough such voters
	// we will be respidered quickly theoretically.
	// MDW: disable this for now since it is causing a parsing 
	// inconsistency since when we re-read the list from sectiondb
	// for the parsing inconsistency check in XmlDoc.cpp, it sometimes
	// sets m_waitInLine to true... which is strange... wtf?
	// guilty url: www.lis.illinois.edu/newsroom/events?dt=2011-01-15
	uint64_t termId = siteHash64 & TERMID_MASK;

	// . now for tallying the # of voters per site we use tagHash of 0
	// . we use this to check for a MAX_SITE_VOTERS breach in XmlDoc.cpp
	key128_t k = g_datedb.makeKey ( termId      ,
					// 0 is the tagHash
					0 ,
					// score/secType
					SV_SITE_VOTER ,
					docId         ,
					false         );
	// make a fake section vote
	SectionVote sv;
	sv.m_score       = 0;
	sv.m_numSampled  = 0;
	// . hash it
	// . returns false and sets g_errno on error
	if ( ! dt->addKey ( &k , &sv ) ) return false;


	// . add all votes in m_nsvt into Sectiondb
	// . we don't add m_osvt because we got those from Sectiondb!
	// . however, we do serialize both tables in the TitleRec and we
	//   only serialize m_nsvt right now because of SEC_DUP having
	//   been computed by volatile Msg20s.. to ensure parsing consistency
	for ( int32_t i = 0 ; i < m_svt.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// skip if empty
		if ( m_svt.m_flags[i] == 0 ) continue;
		// get key
		uint64_t vk = *(uint64_t *)m_svt.getKey(i);
		// get section type from key
		int32_t sectionType = vk >> 32;
		//int32_t sectionType = vk & 0xffffffff;
		// sanity check
		if ( sectionType > 255 ) { char *xx=NULL;*xx=0; }
		if ( sectionType < 0   ) { char *xx=NULL;*xx=0; }
		// and tagHash from key
		uint32_t secHash32 = vk & 0xffffffff;
		// check for "what"
		//if ( tagHash == (uint32_t)-1024871986 )
		//	log("hey");
		//int32_t tagHash = vk >> 32;
		// make the record key first
		key128_t k;
		k = g_datedb.makeKey ( termId    , // termId
				       secHash32 , // date field
				       sectionType , // score
				       docId       ,
				       false       ); // delete key?
		// the data of the record is just the SectionVote
		SectionVote *sv = (SectionVote *)m_svt.getValueFromSlot(i);
		// sanity check. this is reserved for line waiters i guess
		if ( sv->m_numSampled == 0 ) { char *xx=NULL;*xx=0; }
		// sanity check
		uint64_t sh = g_datedb.getTermId(&k);
		if ( sh != termId ) { char *xx=NULL;*xx=0; }
		int64_t d = g_datedb.getDocId(&k);
		if ( d != docId   ) { char *xx=NULL;*xx=0; }
		// this returns false and sets g_errno on error
		if ( ! dt->addKey ( &k , sv ) ) return false;
		// log this for now! last hash is the date format hash!
		//logf(LOG_DEBUG,"section: added tagHash=0x%"XINT32" "
		//     "sectionType=%"INT32" score=%.02f numSampled=%.02f",
		//     tagHash,sectionType,sv->m_score,sv->m_numSampled);
	}

	return true;
}

/*
// add docid-based forced spider recs into the metalist
char *Sections::respiderLineWaiters ( char *metaList    , 
				      char *metaListEnd ) {
				      // these are from the parent
				      //Url  *url         ,
				      //int32_t  ip          ,
				      //int32_t  priority    ) {
	
	// int16_tcut
	char *p    = metaList;
	char *pend = metaListEnd;

	// not if we ourselves had to wait in line! that means there were not
	// enough voters!
	if ( m_waitInLine ) return p;

	// MDW: disable this for now since it is causing a parsing 
	// inconsistency since when we re-read the list from sectiondb
	// for the parsing inconsistency check in XmlDoc.cpp, it sometimes
	// sets m_waitInLine to true... which is strange... wtf?
	// guilty url: www.lis.illinois.edu/newsroom/events?dt=2011-01-15
	return p;

	//int32_t now = getTimeSynced();

	// host hash
	//int32_t h = hash32 ( url->getHost() , url->getHostLen() );
	// make sure not 0
	//if ( h == 0 ) h = 1;

	// now add the line waiters into spiderdb as forced spider recs
	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRecord() ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// does it match us?
		int32_t tagPairHash = m_list.getCurrentDate();
		// skip if not line waiter
		if ( tagPairHash != m_tagPairHash ) continue;
		// key128_t
		key128_t *key = (key128_t *)m_list.getCurrentRec();
		// get section type of this sectiondb vote/key
		int32_t secType = g_indexdb.getScore ( (char *)key );
		// must be this
		if ( secType != SV_WAITINLINE ) continue;
		// get docid
		int64_t docId = m_list.getCurrentDocId();
		// store the rdbId
		*p++ = RDB_SPIDERDB;
		// . store in the meta list
		// . similar to how we do it in Links::setMetaList()
		SpiderRequest *sreq = (SpiderRequest *)p;
		// reset it
		sreq->reset();
		// now set these
		sreq->m_fromSections = 1;
		sreq->m_urlIsDocId   = 1;
		sreq->m_fakeFirstIp  = 1;
		// copy url
		sprintf(sreq->m_url,"%"UINT64"",docId);
		// fake
		int32_t firstIp = hash32n(sreq->m_url);
		if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;
		sreq->m_firstIp = firstIp;
		// set the key!
		sreq->setKey( firstIp, m_docId , false );
		// advance p
		p += sreq->getRecSize();
		// sanity check
		if ( p > pend ) { char *xx=NULL;*xx=0; }
		// debug
		logf(LOG_DEBUG,"section: respider line waiter d=%"INT64"",docId);
	}
	return p;
}		
*/
#define TABLE_ROWS 25

// . print it out
// . we can't just loop over the sections because some sections have
//   words between the subsections they contain,
// . so we have to loop over the individual "words"
/*
bool Sections::print ( SafeBuf *sbuf ,
		       HashTableX *pt ,
		       HashTableX *et ,
		       HashTableX *st2 ,
		       HashTableX *at  ,
		       HashTableX *tt  ,
		       //HashTableX *rt  ,
		       HashTableX *priceTable ) {

	sbuf->safePrintf("<b>Sections in Document</b>\n");

	// section sanity checks
	for ( int32_t i =0  ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get section
		Section *sn = &m_sections[i]; // Ptrs[i];
		// get parent
		Section *sp = sn->m_parent;
		if ( ! sp ) continue;
		// check if contained
		if ( sn->m_a < sp->m_a ) { char *xx=NULL;*xx=0; }
		if ( sn->m_b > sp->m_b ) { char *xx=NULL;*xx=0; }
	}

	char  **wptrs = m_words->getWords    ();
	int32_t   *wlens = m_words->getWordLens ();
	nodeid_t *tids = m_words->getTagIds();
	int32_t    nw    = m_words->getNumWords ();

	// check words
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get section
		Section *sn = m_sectionPtrs[i];
		if ( sn->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( sn->m_b <= i ) { char *xx=NULL;*xx=0; }
	}

	bool hadWords = false;
	Section *lastSection = NULL; // TagHash = -1;
	Section *dstack[MAXTAGSTACK];
	int32_t    ns = 0;
	int32_t printedi = -1;
	//sbuf->safePrintf("<pre>\n");
	// first print the html lines out
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get section
		Section *sn = m_sectionPtrs[i];
		// print if ending
		if ( ns>0 && dstack[ns-1]->m_b == i && ! hadWords )
			sbuf->safePrintf("&nbsp;");
		// if this word is punct print that out
		if ( ! m_wids[i] && ! m_tids[i] ) {
			sbuf->safeMemcpy(m_wptrs[i],m_wlens[i]);
			// do not reprint
			printedi = i;
		}
		// punch out some </divs>
		for ( ; ns>0 && dstack[ns-1]->m_b == i ; ns-- ) {
			//sbuf->safePrintf("</div id=0x%"XINT32">\n",
			//(int32_t)dstack[ns-1]);
			sbuf->safePrintf("</div>\n");
		}
		// . put in a div if its a new section changing front tag
		// . section might not start with a tag, like the bullet
		if ( //tids[i] && 
		     //! ( tids[i] & BACKBIT ) &&
		     sn->m_a == i &&
		     lastSection != sn ) {
			// punch out some </divs>
			//for ( ; ns>0 && dstack[ns-1] == i ; ns-- ) 
			//	sbuf->safePrintf("</div>\n");
			sbuf->safePrintf("<br>");
			// store the sections in this array
			Section *spbuf[20];
			// one div per parent!
			Section *sp = sn;
			int32_t qq = 0;
			for ( ; sp && sp->m_a == sn->m_a ; sp = sp->m_parent )
				spbuf[qq++] = sp;
			// sanity check
			if ( qq > 20 ) { char *xx=NULL;*xx=0; }
			// flag for printing
			//bool printed = false;
			// for sanity check
			Section *lastk = NULL;
			bool printFirstWord = false;
			// now reverse loop for div sections
			for ( int32_t k = qq-1 ; k >= 0 ; k-- ) {
				// get it
				Section *sk = spbuf[k];
				// must be good
				if ( lastk && 
				     ( sk->m_a > lastk->m_a || 
				       sk->m_b > lastk->m_b )) { 
					char *xx=NULL;*xx=0; }
				// save it
				lastk = sk;
				// flag
				if ( sk->m_flags & SEC_SENTENCE )
					printFirstWord = true;
				// only make font color different
				int32_t bcolor = (int32_t)sk->m_tagHash& 0x00ffffff;
				int32_t fcolor = 0x000000;
				int32_t rcolor = 0x000000;
				uint8_t *bp = (uint8_t *)&bcolor;
				bool dark = false;
				if ( bp[0]<128 && bp[1]<128 && bp[2]<128 ) 
					dark = true;
				// or if two are less than 50
				if ( bp[0]<100 && bp[1]<100 ) dark = true;
				if ( bp[1]<100 && bp[2]<100 ) dark = true;
				if ( bp[0]<100 && bp[2]<100 ) dark = true;
				// if bg color is dark, make font color light
				if ( dark ) {
					fcolor = 0x00ffffff;
					rcolor = 0x00ffffff;
				}
				// start the new div
				//sbuf->safePrintf("<div id=0x%"XINT32" "//e%"INT32" "
				sbuf->safePrintf("<div "
						 "style=\""
						 "background-color:#%06"XINT32";"
						 "margin-left:20px;"
						 "border:#%06"XINT32" 1px solid;"
						 "color:#%06"XINT32"\">",
						 //(int32_t)sk,
						 bcolor,
						 rcolor,
						 fcolor);
				// print event id range
				if ( sk->m_minEventId >= 1 )
					sbuf->safePrintf("%"INT32"-%"INT32" ",
							 sk->m_minEventId,
							 sk->m_maxEventId);
				// push that
				//if ( (sk->m_flags & SEC_FAKE) &&
				//     sk->m_b > sk->m_a + 1 )
				//	dstack[ns++] = sk->m_b - 1;
				//else
				dstack[ns++] = sk;//->m_b;
				// print word/tag #i
				if ( printedi<i && !(sk->m_flags&SEC_FAKE)) {
					// only print the contents once
					printedi = i; // = true;
					// "<br>" might be a sequnce of brs
					int32_t last = i;
					if ( tids[i] == TAG_BR )
						last = sk->m_b - 1;
					char *end = m_wptrs[last] +
						m_wlens[last];
					int32_t tlen = end - wptrs[i];
					// only encode if it is a tag
					if ( tids[i] )
						sbuf->htmlEncode(wptrs[i],
								 tlen,
								 false );
					//else
					//sbuf->safePrintf(wptrs[i],
					//wlens[i],
					//false );
				}
				// print the flags
				sbuf->safePrintf("<i>");

				if ( sk ) 
				 sbuf->safePrintf("A=%"INT32" ",sk->m_a);

				//sbuf->
				//safePrintf("fwp=%"INT32" ",sk->m_firstWordPos);

				// print tag hash now
				if ( sk )
				 sbuf->safePrintf("hash=0x%"XINT32" ",
						  (int32_t)sk->m_tagHash);

				if ( sk->m_contentHash)
					sbuf->safePrintf("ch=0x%"XINT32" ",
						 (int32_t)sk->m_contentHash);
				//else if ( sk->m_contentHash2 )
				//	sbuf->safePrintf("ch2=0x%"XINT32" ",
				//		 (int32_t)sk->m_contentHash2);
				else if ( sk->m_sentenceContentHash )
					sbuf->safePrintf("sch=0x%"XINT32" ",
					     (int32_t)sk->m_sentenceContentHash);


				// show dup votes if any
				if ( sk->m_votesForDup )
					sbuf->safePrintf("dupvotes=%"INT32" ",
							 sk->m_votesForDup);
				if ( sk->m_votesForNotDup )
					sbuf->safePrintf("notdupvotes=%"INT32" ",
							 sk->m_votesForNotDup);

				printFlags ( sbuf , sk , false );

				// print the item tables out
				int64_t *ph = (int64_t *)pt->getValue(&sk);
				int64_t *eh = (int64_t *)et->getValue(&sk);
				//bool isInRt = false;
				//if ( rt ) isInRt = rt->isInTable(&sk);
				bool inPriceTable = false;
				if ( priceTable )
					inPriceTable = 
						priceTable->isInTable(&sk);
				// get addr index ptr if any (could be mult)
				int32_t acount = 0;
				int64_t sh = 0LL;
				if ( at ) {
					int32_t slot = at->getSlot(&sk);
					for(;slot>=0;
					    slot=at->getNextSlot(slot,&sk)) {
						// get min
						Place **pp;
						pp = (Place **)at->
							getValueFromSlot(slot);
						// get hash
						int64_t tt=(*pp)->m_hash;
						// get max
						if ( ! sh || tt > sh ) sh = tt;
						// count them
						acount++;
					}
				}
				// date #
				//int32_t *ti = (int32_t *)tt->getValue(&sk);
				// print those out
				if ( ph ) 
				       sbuf->safePrintf("hasphone ");
				if ( eh ) 
				       sbuf->safePrintf("hasemail ");
				//if ( isInRt )
				//     sbuf->safePrintf("hasregistration ");

				if ( inPriceTable )
					sbuf->safePrintf("hasprice ");
					
				//if ( sk )
				//sbuf->safePrintf("dh=0x%"XINT32" ",
				//getDelimHash(METHOD_INNER_TAGID,sk));


				if ( sh )
					sbuf->safePrintf("placehash=0x%"XINT64"",
							 sh);
				if ( sh && acount >= 2 )
					sbuf->safePrintf(" (%"INT32" total)",
							 acount);

				if ( isHardSection(sk) )
					sbuf->safePrintf("hardsec ");
					
				//if ( ph ) 
				//     sbuf->safePrintf("phonehash=%"XINT64" ",*ph);
				//if ( eh ) 
				//     sbuf->safePrintf("emailhash=%"XINT64" ",*eh);
				// consider actually print the address out
				// if you want to, but don't show the pointer
				// because it causes problems in the diff
				// that the qa loop does.
				// addr #
				//if ( ai ) 
				//sbuf->safePrintf("addrindex=%"INT32" ",*ai);
				//if ( ai ) 
				//sbuf->safePrintf("hasaddress ");
				// tod #
				//if ( ti ) 
				//	sbuf->safePrintf("hastod ");//,*ti);
				//if ( sk->m_numAddresses > 0 )
				//	sbuf->safePrintf("na=%i ",
				//			 sk->m_numAddresses);
				//if ( sk->m_numPlaces > 0 )
				//	sbuf->safePrintf("np=%i ",
				//			 sk->m_numPlaces);
				sbuf->safePrintf("</i> ");
			}
			// assume had no words
			hadWords = false;

			if (printFirstWord && !tids[i] )
				sbuf->htmlEncode(wptrs[i],wlens[i],false );

			// update it
			lastSection = sn;
			continue;
		}
		// based on depth
		//for ( int32_t k = 0 ; 
		//      lastSection != sn && k < sn->m_depth + 1 ; 
		//      k++ )
		//	sbuf->safePrintf("-");
		// print word/tag #i
		if ( tids[i] != TAG_BR && i>printedi )
			sbuf->htmlEncode(wptrs[i],wlens[i],false );
		// update it
		lastSection = sn;
		// assume had words
		if ( m_wids[i] || tids[i] ) hadWords = true;
	}
	bool unbal = (ns > 0 );
	//sbuf->safePrintf("</pre>\n");
	for ( int32_t i = 0 ; i < ns ; i++ )
		sbuf->safePrintf("</div>\n");
	// sanity check
	if ( unbal ) 
		sbuf->safePrintf("<br><b>%"INT32" UNBALANCED SECTIONS</b><br><br>",
				 ns);


	// print header
	char *hdr =
		"<table border=1>"
		"<tr>"
		"<td><b>sec #</b></td>"
		"<td><b>baseHash</b></td>"
		"<td><b>cumulTagHash</b></td>"
		"<td><b>wordStart</b></td>"
		"<td><b>wordEnd</b></td>"
		"<td><b>contentHash</b></td>"
		"<td><b>XOR</b></td>" // only valid for contentHashes
		"<td><b>alnum words</b></td>" // contained in section
		"<td><b>depth</b></td>"
		"<td><b>parent word range</b></td>"
		"<td><b># siblings</b></td>"
		"<td><b>flags</b></td>"
		"<td><b>evIds</b></td>"
		"<td><b>text snippet</b></td>"
		//"<td>votes for static</td>"
		//"<td>votes for dynamic</td>"
		//"<td>votes for texty</td>"
		//"<td>votes for unique</td>"
		"</tr>\n";
	sbuf->safePrintf("%s",hdr);

	int32_t rcount = 0;
	int32_t scount = 0;
	// show word # of each section so we can look in PageParser.cpp's
	// output to see exactly where it starts, since we now label all
	// the words
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("<!--ignore--></table>%s\n",hdr);
		// get it
		//Section *sn = &m_sections[i];
		//Section *sn = m_sorted[i];
		// skip if not a section with its own words
		//if ( sn->m_flags & SEC_NOTEXT ) continue;
		char *xs = "--";
		char ttt[100];
		if ( sn->m_contentHash ) {
			int32_t modified = sn->m_tagHash ^ sn->m_contentHash;
			sprintf(ttt,"0x%"XINT32"",modified);
			xs = ttt;
		}
		// int16_tcut
		Section *parent = sn->m_parent;
		int32_t pswn = -1;
		int32_t pewn = -1;
		if ( parent ) pswn = parent->m_a;
		if ( parent ) pewn = parent->m_b;
		// print it
		sbuf->safePrintf("<!--ignore--><tr><td>%"INT32"</td>\n"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%s</td>"
				 "<td>%"INT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td><nobr>%"INT32" to %"INT32"</nobr></td>"
				 "<td>%"INT32"</td>"
				 "<td><nobr>" ,
				 scount++,//i,
				 (int32_t)sn->m_baseHash,
				 (int32_t)sn->m_tagHash,
				 sn->m_a,
				 sn->m_b,
				 (int32_t)sn->m_contentHash,
				 xs,
				 sn->m_exclusive,
				 sn->m_depth,
				 pswn,
				 pewn,
				 sn->m_numOccurences);//totalOccurences );
		// now show the flags
		printFlags ( sbuf , sn , false );
		// first few words of section
		int32_t a = sn->m_a;
		int32_t b = sn->m_b;
		// -1 means an unclosed tag!! should no longer be the case
		if ( b == -1 ) { char *xx=NULL;*xx=0; }//b=m_words->m_numWords;
		sbuf->safePrintf("</nobr></td>");

		if ( sn->m_minEventId >= 1 )
			sbuf->safePrintf("<td><nobr>%"INT32"-%"INT32"</nobr></td>",
					 sn->m_minEventId,sn->m_maxEventId);
		else
			sbuf->safePrintf("<td>&nbsp;</td>");

		sbuf->safePrintf("<td><nobr>");
		// 70 chars max
		int32_t   max   = 70; 
		int32_t   count = 0;
		char   truncated = 0;
		// do not print last word/tag in section
		for ( int32_t i = a ; i < b - 1 && count < max ; i++ ) {
			char *s    = wptrs[i];
			int32_t  slen = wlens[i];
			if ( count + slen > max ) {
				truncated = 1; 
				slen = max - count;
			}
			count += slen;
			// boldify front tag
			if ( i == a ) sbuf->safePrintf("<b>");
			sbuf->htmlEncode(s,slen,false);
			// boldify front tag
			if ( i == a ) sbuf->safePrintf("</b>");
		}
		// if we truncated print a ...
		if ( truncated ) sbuf->safePrintf("<b>...</b>");
		// then print ending tag
		if ( b < nw ) {
			int32_t blen = wlens[b-1];
			if ( blen>20 ) blen = 20;
			sbuf->safePrintf("<b>");
			sbuf->htmlEncode(wptrs[b-1],blen,false);
			sbuf->safePrintf("</b>");
		}

		sbuf->safePrintf("</nobr></td></tr>\n");
	}
			 
	sbuf->safePrintf("</table>\n<br>\n");



	// now print the NEW voting table
	sbuf->safePrintf("<b>NEW Section Voting Table (nsvt)</b>\n");
	sbuf->safePrintf("<br>");
	sbuf->safePrintf("<i>tagHash is combined with sectionType to make the "
			 "key in sectiondb. The data is everything else. "
			 "<br>"
			 "*The tagHash is XOR'ed with the contentHash for the "
			 "contentHash "
			 "section type, and is the tagPairHash for the "
			 "tagPairHash section type.</i>");
	// print table header
	char *hdr2 =
		"<table border=1>"
		"<tr>"
		"<td><b>siteHash</b></td>"
		"<td><b>tagHash*</b></td>"
		"<td><b>sectionType</b></td>"
		"<td><b>scoreTotal</b></td>"
		"<td><b>numSampled</b></td>"
		"<td><b>avgScore</b></td>"
		"</tr>\n";
	sbuf->safePrintf("%s",hdr2);
	HashTableX *st = &m_nsvt;
	rcount = 0;
	for ( int32_t i = 0 ; i < st->m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if empty
		if ( ! st->m_flags[i] ) continue;
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("<!--ignore--></table>%s\n",hdr2);
		// get the key
		uint64_t k = *(uint64_t *)st->getKey ( i );
		// get the data
		SectionVote *sv = (SectionVote *)st->getValueFromSlot ( i );
		// parse key
		int32_t tagHash     = (int32_t)(k & 0xffffffff);
		int32_t sectionType = (int32_t)(k >> 32);
		//int32_t tagHash     = (int32_t)(k >> 32);
		//int32_t sectionType = (int32_t)(k & 0xffffffff);
		// convert to string
		char *st = getSectionTypeAsStr ( sectionType );
		float avg = 0.0;
		if ( sv->m_numSampled > 0 ) avg = sv->m_score/sv->m_numSampled;
		sbuf->safePrintf("<tr>"
				 "<td>--</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%s</td>"
				 "<td>%.02f</td>"
				 "<td>%.02f</td>"
				 "<td>%.02f</td>"
				 "</tr>\n",
				 tagHash,
				 st,
				 sv->m_score,
				 sv->m_numSampled ,
				 avg );
	}
	sbuf->safePrintf("</table>\n<br><br>\n");








	// now print the OLD voting table
	sbuf->safePrintf("<b>OLD Section Voting Table (osvt)</b>\n");
	sbuf->safePrintf("<br>\n");
	sbuf->safePrintf("<i>numSampled is # of docs that had tagHash or "
			 "contentHash.\n");
	// print table header
	sbuf->safePrintf("%s",hdr2);
	st = &m_osvt;
	rcount = 0;
	for ( int32_t i = 0 ; i < st->m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if empty
		if ( ! st->m_flags[i] ) continue;
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("<!--ignore--></table>%s\n",hdr2);
		// get the key
		uint64_t k = *(uint64_t *)st->getKey ( i );
		// get the data
		SectionVote *sv = (SectionVote *)st->getValueFromSlot ( i );
		// parse key
		int32_t tagHash     = (int32_t)(k & 0xffffffff);
		int32_t sectionType = (int32_t)(k >> 32);
		//int32_t tagHash     = (int32_t)(k >> 32);
		//int32_t sectionType = (int32_t)(k & 0xffffffff);
		// convert to string
		char *st = getSectionTypeAsStr ( sectionType );
		float avg = 0.0;
		if ( sv->m_numSampled > 0 ) avg = sv->m_score/sv->m_numSampled;
		sbuf->safePrintf("<tr>"
				 "<td>--</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%s</td>"
				 "<td>%.02f</td>"
				 "<td>%.02f</td>"
				 "<td>%.02f</td>"
				 "</tr>\n",
				 tagHash,
				 st,
				 sv->m_score,
				 sv->m_numSampled ,
				 avg );
	}
	sbuf->safePrintf("</table>\n<br><br>\n");

	sbuf->safePrintf("<table border=1 cellpadding=3>\n");
	sbuf->safePrintf(
			 "<tr><td><nobr><b>Section Type</b></nobr></td>"
			 "<td><b>Description</b></td></tr>\n"
			 
			 "<tr><td>texty</td><td>An average "
			 "score of 0.0 means the "
			 "section does not have any words that are not "
			 "in anchor test. An average "
			 "score of 1.0 means the section "
			 "has words, none of which are in anchor text."
			 "</td></tr>\n"

			 "<tr><td>clock</td><td>An average "
			 "score of 1.0 means "
			 "section identified as "
			 "containing a clock. An average "
			 "score of 0.0 mean it contains "
			 "a date that is definitely not a clock."
			 "</td></tr>\n"

			 "<tr><td>eurdatefmt</td><td>An average "
			 "score of 1.0 means "
			 "the section contains a date in european format, "
			 "which means day first, not month. An average "
			 "score of 0.0 "
			 "means the section contains a date that is NOT in "
			 "european format.</td></tr>\n"

			 //"<tr><td>event</td><td>"
			 //"</td></tr>\n"

			 //"<tr><td>address</td><td></td></tr>\n"

			 "<tr><td>tagpairhash</td><td>"
			 "All unique adjacent tag ids are hashed to form "
			 "this number which represents the overal strucutre "
			 "of the document. The tagHash is the tagPairHash "
			 "in this case."
			 "</td></tr>\n"

			 "<tr><td>contenthash</td><td>"
			 "The tagHash in this case is really "
			 "the content hash of the words in the section XORed "
			 "with the original tagHash."
			 "</td></tr>\n"

			 //"<tr><td>textmaxsmpl</td><td></td></tr>\n"
			 //"<tr><td>waitinline</td><td></td></tr>\n"
			 //"<tr><td></td><td></td></tr>\n"
			 );
	sbuf->safePrintf("</table>\n<br><br>\n");
	// must be \0 terminated
	if ( sbuf->m_buf[sbuf->m_length] ) { char *xx=NULL;*xx=0; }

			 
	return true;
}
*/

void Sections::printFlags ( SafeBuf *sbuf , Section *sn , bool justEvents ) {
	sec_t f = sn->m_flags;


	//if ( f & SEC_ADDRESS_CONTAINER )
	//	sbuf->safePrintf("addresscontainer ");
	//if ( f & SEC_EVENT )
	//	sbuf->safePrintf("hasevent ");

	//if ( f & SEC_MULT_EVENTS_1 )
	//	sbuf->safePrintf("multevents1 ");
	//if ( f & SEC_MULT_EVENTS_2 )
	//	sbuf->safePrintf("multevents2 ");
	//if ( f & SEC_MULT_DATES )
	//	sbuf->safePrintf("multdates ");
	//if ( f & SEC_SUBSECTION )
	//	sbuf->safePrintf("subsection ");
	//if ( f & SEC_VENUE_PAGE )
	//	sbuf->safePrintf("venuepage ");
	//if ( f & SEC_MULT_ADDRESSES )
	//	sbuf->safePrintf("multaddresses ");
	//if ( f & SEC_MULT_PLACES )
	//	sbuf->safePrintf("multplaces ");

	if ( justEvents ) return;

	if ( f & SEC_HASHXPATH )
		sbuf->safePrintf("hashxpath ");

	sbuf->safePrintf("indsenthash64=%"UINT64" ",sn->m_indirectSentHash64);


	if ( f & SEC_TOD_EVENT )
		sbuf->safePrintf("todevent ");

	if ( f & SEC_HR_CONTAINER )
		sbuf->safePrintf("hrcontainer ");

	//if ( f & SEC_TOD_EVENT_2 )
	//	sbuf->safePrintf("containsmulttodevents ");
	//if ( f & SEC_TOD_EVENT_3 )
	//	sbuf->safePrintf("containsmulttodevents ");
	//if ( sn->m_numTods >= 1 )
	//	sbuf->safePrintf("numtods=%"INT32" ",sn->m_numTods );


	if ( f & SEC_HAS_REGISTRATION )
		sbuf->safePrintf("hasregistration ");
	if ( f & SEC_HAS_PARKING )
		sbuf->safePrintf("hasparking ");

	if ( f & SEC_NIXED_HEADING_CONTAINER )
		sbuf->safePrintf("nixedheadingcontainer ");
	if ( f & SEC_HEADING_CONTAINER )
		sbuf->safePrintf("headingcontainer ");
	if ( f & SEC_HEADING )
		sbuf->safePrintf("heading ");

	if ( f & SEC_HAS_NONFUZZYDATE )
		sbuf->safePrintf("hasnonfuzzydate ");

	if ( f & SEC_TAIL_CRAP )
		sbuf->safePrintf("tailcrap ");

	if ( f & SEC_STRIKE )
		sbuf->safePrintf("strike ");
	if ( f & SEC_STRIKE2 )
		sbuf->safePrintf("strike2 ");

	if ( f & SEC_SECOND_TITLE )
		sbuf->safePrintf("secondtitle ");

	//if ( f & SEC_HAS_EVENT_DATE )
	//	sbuf->safePrintf("haseventdate " );
	//if ( f & SEC_HAS_NON_EVENT_DATE )
	//	sbuf->safePrintf("hasnoneventdate " );
	if ( f & SEC_EVENT_BROTHER )
		sbuf->safePrintf("eventbrother " );
	if ( f & SEC_IGNOREEVENTBROTHER )
		sbuf->safePrintf("ignoreeventbrother " );
	if ( f & SEC_STOREHOURSCONTAINER )
		sbuf->safePrintf("storehourscontainer ");
	if ( f & SEC_DATE_LIST_CONTAINER )
		sbuf->safePrintf("datelistcontainer " );
	if ( f & SEC_PUBDATECONTAINER )
		sbuf->safePrintf("pubdatecontainer ");
	if ( f & SEC_TABLE_HEADER )
		sbuf->safePrintf("tableheader ");
	//if ( f & SEC_HAS_DATE )
	//	sbuf->safePrintf("hasdate " );
	if ( f & SEC_HAS_DOM )
		sbuf->safePrintf("hasdom " );
	if ( f & SEC_HAS_MONTH )
		sbuf->safePrintf("hasmonth " );
	if ( f & SEC_HAS_DOW )
		sbuf->safePrintf("hasdow " );
	if ( f & SEC_HAS_TOD )
		sbuf->safePrintf("hastod " );
	if ( f & SEC_HASEVENTDOMDOW )
		sbuf->safePrintf("haseventdomdow " );
	//if ( f & SEC_HAS_TODINMENU )
	//	sbuf->safePrintf("hastodinmenu " );
	if ( f & SEC_MENU_SENTENCE )
		sbuf->safePrintf("menusentence " );
	if ( f & SEC_MENU )
		sbuf->safePrintf("ismenu " );
	if ( f & SEC_MENU_HEADER )
		sbuf->safePrintf("menuheader " );
	//if ( f & SEC_LIST_HEADER )
	//	sbuf->safePrintf("listheader " );
	if ( f & SEC_CONTAINER )
		sbuf->safePrintf("listcontainer " );
	if ( f & SEC_INPUT_HEADER )
		sbuf->safePrintf("inputheader " );
	if ( f & SEC_INPUT_FOOTER )
		sbuf->safePrintf("inputfooter " );
	if ( f & SEC_LINK_TEXT )
		sbuf->safePrintf("linktext " );
	if ( f & SEC_PLAIN_TEXT )
		sbuf->safePrintf("plaintext " );
	//if ( f & SEC_NOT_MENU )
	//	sbuf->safePrintf("notmenu " );
	//if ( f & SEC_MAYBE_MENU )
	//	sbuf->safePrintf("maybemenu " );
	//if ( f & SEC_DUP )
	//	sbuf->safePrintf("dupsection " );
	//if ( f & SEC_IS_MENUITEM )
	//	sbuf->safePrintf("menuitem " );


	//if ( f & SEC_SENTENCE )
	//	sbuf->safePrintf("sentence ");

	if ( f & SEC_FAKE ) {
		//sbuf->safePrintf("hrtag ");
		// extra it
		//if      ( sn->m_baseHash == BH_HR )
		//	sbuf->safePrintf("hrtagdelim ");
		//if ( sn->m_baseHash == BH_BR )
		//	sbuf->safePrintf("brtagdelim ");
		//else if ( sn->m_baseHash == BH_H1 )
		//	sbuf->safePrintf("h1tagdelim ");
		//else if ( sn->m_baseHash == BH_H2 )
		//	sbuf->safePrintf("h2tagdelim ");
		//else if ( sn->m_baseHash == BH_H3 )
		//	sbuf->safePrintf("h3tagdelim ");
		//else if ( sn->m_baseHash == BH_H4 )
		//	sbuf->safePrintf("h4tagdelim ");
		//else if ( sn->m_baseHash == BH_BRBR )
		//	sbuf->safePrintf("brbrdelim ");
		if ( sn->m_baseHash == BH_BULLET )
			sbuf->safePrintf("bulletdelim ");
		else if ( sn->m_baseHash == BH_SENTENCE )
			sbuf->safePrintf("<b>sentence</b> ");
		else if ( sn->m_baseHash == BH_IMPLIED )
			sbuf->safePrintf("<b>impliedsec</b> ");
		//else if ( sn->m_baseHash == BH_IMPLIED_LIST )
		//	sbuf->safePrintf("<b>impliedLIST</b> ");
		else { char *xx=NULL;*xx=0; }
	}

	if ( f & SEC_SPLIT_SENT )
		sbuf->safePrintf("<b>splitsent</b> ");

	if ( f & SEC_NOTEXT )
		sbuf->safePrintf("notext ");

	if ( f & SEC_MULTIDIMS )
		sbuf->safePrintf("multidims ");

	if ( f & SEC_HASDATEHEADERROW )
		sbuf->safePrintf("hasdateheaderrow ");
	if ( f & SEC_HASDATEHEADERCOL )
		sbuf->safePrintf("hasdateheadercol ");



	if ( sn->m_colNum ) 
		sbuf->safePrintf("colnum=%"INT32" ",sn->m_colNum );
	if ( sn->m_rowNum ) 
		sbuf->safePrintf("rownum=%"INT32" ",sn->m_rowNum );
	if ( sn->m_headColSection )
		sbuf->safePrintf("headcola=%"INT32" ",sn->m_headColSection->m_a);
	if ( sn->m_headRowSection )
		sbuf->safePrintf("headrowa=%"INT32" ",sn->m_headRowSection->m_a);

	if ( f & SEC_IN_TABLE )
		sbuf->safePrintf("intable ");
	//if ( f & SEC_ARTICLE )
	//	sbuf->safePrintf("inarticle ");
	if ( f & SEC_SCRIPT )
		sbuf->safePrintf("inscript ");
	if ( f & SEC_NOSCRIPT )
		sbuf->safePrintf("innoscript ");
	if ( f & SEC_STYLE )
		sbuf->safePrintf("instyle ");
	//if ( f & SEC_FIRST_HIDDEN )
	//	sbuf->safePrintf("infirstdivhide ");
	if ( f & SEC_HIDDEN )
		sbuf->safePrintf("indivhide ");
	if ( f & SEC_SELECT )
		sbuf->safePrintf("inselect ");
	if ( f & SEC_MARQUEE )
		sbuf->safePrintf("inmarquee ");
	//if ( f & SEC_A )
	//	sbuf->safePrintf("inhref ");
	if ( f & SEC_IN_TITLE )
		sbuf->safePrintf("intitle ");
	if ( f & SEC_IN_HEADER )
		sbuf->safePrintf("inheader ");

	if ( f & SEC_UNBALANCED )
		sbuf->safePrintf("unbalanced " );
	if ( f & SEC_OPEN_ENDED )
		sbuf->safePrintf("openended " );

	//for ( int32_t i = 0 ; i < (int32_t)sizeof(turkbits_t)*8 ; i++ ) {
	//	uint64_t mask = ((turkbits_t)1) << (turkbits_t)i;
	//	if ( ! ((sn->m_turkBits) & mask ) ) continue;
	//	sbuf->safePrintf("%s ",getTurkBitLabel(mask));
	//}

	// sentence flags
	sentflags_t sf = sn->m_sentFlags;
	for ( int32_t i = 0 ; i < 64 ; i++ ) {
		// get mask
		uint64_t mask = ((uint64_t)1) << (uint64_t)i;
		if ( sf & mask )
			sbuf->safePrintf("%s ",getSentBitLabel(mask));
	}

	//if ( f & SEC_HAS_DATE )
	//	sbuf->safePrintf("hasdate ");
	//if ( ! f ) sbuf->safePrintf("&nbsp;");
}

char *getSectionTypeAsStr ( int32_t sectionType ) {
	//if ( sectionType == SV_TEXTY             ) return "texty";
	if ( sectionType == SV_CLOCK             ) return "clock";
	if ( sectionType == SV_EURDATEFMT        ) return "eurdatefmt";
	if ( sectionType == SV_EVENT             ) return "event";
	if ( sectionType == SV_ADDRESS           ) return "address";
	if ( sectionType == SV_TAGPAIRHASH       ) return "tagpairhash";
	if ( sectionType == SV_TAGCONTENTHASH    ) return "tagcontenthash";
	if ( sectionType == SV_TURKTAGHASH       ) return "turktaghash";
	//if ( sectionType == SV_DUP               ) return "dup";
	//if ( sectionType == SV_NOT_DUP           ) return "notdup";
	//if ( sectionType == SV_TEXTY_MAX_SAMPLED ) return "textymaxsmpl";
	//if ( sectionType == SV_WAITINLINE        ) return "waitinline";
	if ( sectionType == SV_FUTURE_DATE       ) return "futuredate";
	if ( sectionType == SV_CURRENT_DATE      ) return "currentdate";
	if ( sectionType == SV_PAST_DATE         ) return "pastdate";
	if ( sectionType == SV_SITE_VOTER        ) return "sitevoter";
	// sanity check
	char *xx=NULL;*xx=0;
	return "unknown";
}
/*
// . returns (char *)-1 with g_errno set on error
// . XmlDoc.cpp calls this to fill in TitleRec::ptr_sectionsData for storage
// . because sectiondb is volatile we need to store m_osvt, which is a tally
//   of all the relevant votes for our sections that we extracted from 
//   sectiondb
char *Sections::getSectionsReply ( int32_t *size ) {
	// assume none
	*size = 0;
	// how much buf do we need?
	m_bufSize = m_osvt.getStoredSize();
	// returning NULL is valid
	if ( m_bufSize == 0 ) return NULL;
	// make buf
	m_buf = (char *)mmalloc ( m_bufSize , "sdata" );
	if ( ! m_buf ) return (char *)-1;
	// store it
	int32_t bytes = m_osvt.serialize ( m_buf , m_bufSize );
	// sanity check
	if ( bytes != m_bufSize ) { char *xx=NULL;*xx=0; }
	// save this
	*size = bytes;
	// good
	return m_buf;
}

// . returns (char *)-1 with g_errno set on error
// . XmlDoc.cpp calls this to fill in TitleRec::ptr_sectionsData for storage
// . because sectiondb is volatile we need to store m_osvt, which is a tally
//   of all the relevant votes for our sections that we extracted from 
//   sectiondb
char *Sections::getSectionsVotes ( int32_t *size ) {
	// assume none
	*size = 0;
	// how much buf do we need?
	m_bufSize2 = m_nsvt.getStoredSize();
	// returning NULL is valid
	if ( m_bufSize2 == 0 ) return NULL;
	// make buf
	m_buf2 = (char *)mmalloc ( m_bufSize2 , "sdata" );
	if ( ! m_buf2 ) return (char *)-1;
	// store it
	int32_t bytes = m_nsvt.serialize ( m_buf2 , m_bufSize2 );
	// sanity check
	if ( bytes != m_bufSize2 ) { char *xx=NULL;*xx=0; }
	// save this
	*size = bytes;
	// good
	return m_buf2;
}
*/
bool Sections::isHardSection ( Section *sn ) {
	int32_t a = sn->m_a;
	// . treat this as hard... kinda like a div section...
	//   fixes gwair.org date from stealing address of another date
	//   because the span tags are fucked up...
	// . crap, no this prevents publicbroadcasting.net and other urls
	//   from telescoping to header dates they need to telescope to.
	//   the header dates are in span tags and if that is seen as a hard
	//   section bad things happen
	//if ( m_tids[a] == TAG_SPAN ) return true;
	if ( ! isBreakingTagId(m_tids[a]) ) {
		// . if first child is hard that works!
		// . fixes "<blockquote><p>..." for collectorsguide.com
		if ( sn->m_next && 
		     sn->m_next->m_tagId &&
		     // fix "blah blah<br>blah blah" for sentence
		     sn->m_next->m_tagId != TAG_BR &&
		     sn->m_next->m_a < sn->m_b &&
		     isBreakingTagId(sn->m_next->m_tagId) )
			return true;
		// otherwise, forget it!
		return false;
	}
	// trumba.com has sub dates in br-based implied sections that need
	// to telescope to their parent above
	if ( m_tids[a] == TAG_BR ) return false;
	//if ( sn->m_baseHash == BH_BRBR   ) return false;
	//if ( sn->m_baseHash == BH_BR     ) return false;
	if ( sn->m_flags & SEC_SENTENCE ) return false;
	// xml tag exception for gwair.org. treat <st1:Place>... as soft
	if ( (m_tids[a] & BACKBITCOMP) == TAG_XMLTAG && ! m_isRSSExt )
		return false;
	//if ( sn->m_baseHash == BH_IMPLIED ) return false;
	// try to fix abqcsl.org for allowing a tod range to extend to
	// the DOW in the paragraph before it
	//if ( (m_tids[a]&BACKBITCOMP) == TAG_P ) return false;
	//if (m_baseHash == BH_H1     ) return true; 
	//if (m_baseHash == BH_H2     ) return true; 
	//if (m_baseHash == BH_H3     ) return true; 
	//if (m_baseHash == BH_H4     ) return true; 
	//if (m_baseHash == BH_BULLET ) return true; 
	return true;
}


bool Sections::setMenus ( ) {

	/*
	// set SEC_LINK_ONLY on sections that just contain a link
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// try that out
		Section *sk = si;
		// loop back up here
	subloop:
		// mark that
		sk->m_flags |= SEC_LINK_TEXT;
		// set boundaries
		int32_t a = sk->m_a;
		int32_t b = sk->m_b;
		// mark parents as int32_t as they have no more text than this!
		sk = sk->m_parent;
		// stop if no more
		if ( ! sk ) continue;
		// check
		int32_t i;
		for ( i = sk->m_a ; i < a ; i++ ) 
			if ( m_wids[i] ) break;
		// skip this section if has text outside
		if ( i < a ) continue;
		// try the right side
		for ( i = b ; i < sk->m_b ; i++ ) 
			if ( m_wids[i] ) break;
		// skip this section if has text outside
		if ( i < sk->m_b ) continue;
		// try the next parent
		goto subloop;
	}
		
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// we are the first item
		Section *first = sk;
		// must be a link text only section
		if ( ! ( sk->m_flags & SEC_LINK_TEXT ) ) continue;
		// skip if used already
		if ( sk->m_used == 78 ) continue;
		// reset
		//bool added = false;
		// use si now
		Section *si = sk;
		// loop back up here
	subloop2:
		// mark it
		si->m_used = 78;
		// save it
		Section *last = si;
		// scan brothers
		si = si->m_nextBrother;
		if ( ! si ) continue;
		// . hard tag ids must match
		// . texasdrums.drums.org likes to put span, bold, etc.
		//   tags throughout their menus, so this fixes that
		//if ( si->m_hardSection->m_tagId !=
		//     last->m_hardSection->m_tagId ) continue;
		// . must have same tag id, baseHash
		// . no wholefoods has a different id for each, but we
		//   should match the tag id!
		//if ( si->m_baseHash != last->m_baseHash ) continue;
		if ( si->m_tagId != last->m_tagId ) continue;
		// stop if not link text
		if ( ! ( si->m_flags & SEC_LINK_TEXT ) ) continue;
		// it is link text
		si->m_flags |= SEC_MENU;
		// and the first one too
		first->m_flags |= SEC_MENU;
		// repeat
		goto subloop2;
	}
	*/


	// . this just returns if already set
	// . sets Bits::m_bits[x].m_flags & D_IN_LINK if its in a link
	// . this bits array is 1-1 with the words
	m_bits->setInLinkBits(this);

	// int16_tcut
	wbit_t *bb = m_bits->m_bits;

	sec_t flag;
	// set SEC_PLAIN_TEXT and SEC_LINK_TEXT for all sections
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need alnum word
		if ( ! m_wids[i] ) continue;
		// get our flag
		if ( bb[i] & D_IN_LINK ) flag = SEC_LINK_TEXT;
		else                     flag = SEC_PLAIN_TEXT;
		// get section ptr
		Section *sk = m_sectionPtrs[i];
		// loop for sk
		for ( ; sk ; sk = sk->m_parent ) {
			// skip if already set
			if ( sk->m_flags & flag ) break;
			// set it
			sk->m_flags |= flag;
		}
	}

	Section *last = NULL;
	// . alernatively, scan through all anchor tags
	// . compare to last anchor tag
	// . and blow up each to their max non-intersection section and make
	//   sure no PLAIN text in either of those!
	// . this is all to fix texasdrums.drums.org which has various span
	//   and bold tags throughout its menu at random
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// . if we hit plain text, we kill our last
		// . this was causing "geeks who drink" for blackbirdbuvette
		//   to get is SEC_MENU set because there was a link after it
		if ( si->m_flags & SEC_PLAIN_TEXT ) last = NULL;
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// . if it is a mailto link forget it
		// . fixes abtango.com from detecting a bad menu
		char *ptr  = m_wptrs[si->m_a];
		int32_t  plen = m_wlens[si->m_a];
		char *mailto = strncasestr(ptr,plen,"mailto:");
		if ( mailto ) last = NULL;
		// bail if no last
		if ( ! last ) { last = si; continue; }
		// save last
		Section *prev = last;
		// set last for next round, used "saved" below
		last = si;
		// get first "hard" section encountered while telescoping
		Section *prevHard = NULL;
		// blow up last until right before it contains us
		for ( ; prev ; prev = prev->m_parent ) {
			// record?
			if ( ! prevHard && isHardSection(prev) ) 
				prevHard = prev;
			// if parent contains us, stop
			if ( prev->m_parent->contains ( si ) ) break;
		}
		// if it has plain text, forget it!
		if ( prev->m_flags & SEC_PLAIN_TEXT ) continue;
		// use this for us
		Section *sk = si;
		// get first "hard" section encountered while telescoping
		Section *skHard = NULL;
		// same for us
		for ( ; sk ; sk = sk->m_parent ) {
			// record?
			if ( ! skHard && isHardSection(sk) ) skHard = sk;
			// if parent contains us, stop
			if ( sk->m_parent->contains ( prev ) ) break;
		}
		// if it has plain text, forget it!
		if ( sk->m_flags & SEC_PLAIN_TEXT ) continue;

		// . first hard sections encountered must match!
		// . otherwise for switchborad.com we lose "A B C ..." as
		//   title candidate because we think it is an SEC_MENU
		//   because the sections before it have links in them, but
		//   they have different hard sections
		if (   prevHard && ! skHard ) continue;
		if ( ! prevHard &&   skHard ) continue;
		if ( prevHard && prevHard->m_tagId!=skHard->m_tagId ) continue;

		/*
		// if this href section splits a sentence, then do not
		// let it be a menu. fixes southgatehouse.com which has
		// "<a>Songwriter Showcase and Open Mic</a> 
		//  <a>with Mike Kuntz</a>"
		// getting detected as a two-link menu system
		// . NO! hurts some other menus that have just spaces
		//   separating the terms!
		Section *se;
		se = prev;
		for ( ; se ; se = se->m_parent ) 
			if ( se->m_flags & SEC_SENTENCE ) break;
		if ( se && 
		     ( se->m_firstWordPos != prev->m_firstWordPos ||
		       se->m_lastWordPos  != prev->m_lastWordPos ) )
			continue;
		se = si;
		for ( ; se ; se = se->m_parent ) 
			if ( se->m_flags & SEC_SENTENCE ) break;
		if ( se && 
		     ( se->m_firstWordPos != sk->m_firstWordPos ||
		       se->m_lastWordPos  != sk->m_lastWordPos ) )
			continue;
		*/

		// ok, great that works!
		prev->m_flags |= SEC_MENU;
		sk  ->m_flags |= SEC_MENU;
	}

	/*
	// now remove menus that are just two links
	// IF they are not exactly the same tag hash?????
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need a menu section
		if ( ! ( si->m_flags & SEC_MENU ) ) continue;
		// skip if processed
		if ( si->m_used == 101 ) continue;
		// must be a list of menu sections
		Section *next = si->m_nextBrother;
		if ( ! next ) continue;
		if ( ! ( next->m_flags & SEC_MENU ) ) continue;
		// is 3rd set?
		Section *third = next->m_nextBrother;
		// unset?
		bool unset = 0;
		if      ( ! third                         ) unset = true;
		else if ( ! ( third->m_flags & SEC_MENU ) ) unset = true;
		// init it
		Section *sj = si;
		// do a full loop
		for ( ; sj ; sj = sj->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// mark it
			sj->m_used = 101;
			// stop if we hit third and unset is true
			if ( sj == third && unset ) break;
			// unset?
			if ( unset ) sj->m_flags &= ~SEC_MENU;
		}
	}
	*/

	// . set text around input radio checkboxes text boxes and text areas
	// . we need input tags to be their own sections though!
	//for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// need a tag
		if ( ! m_tids[i] ) continue;
		// must be an input tag
		if ( m_tids[i] != TAG_INPUT &&
		     m_tids[i] != TAG_TEXTAREA &&
		     m_tids[i] != TAG_SELECT )
			continue;
		// get tag as a word
		char *tag    = m_wptrs[i];
		int32_t  tagLen = m_wlens[i];
		// what type of input tag is this? hidden? radio? checkbox?...
		int32_t  itlen;
		char *it = getFieldValue ( tag , tagLen , "type" , &itlen );
		// skip if hidden
		if ( itlen==6 && !strncasecmp ( it,"hidden",6) ) continue;
		// get word before first item in list
		int32_t r = i - 1;
		for ( ; r >= 0 ; r-- ) {
			QUICKPOLL(m_niceness);
			// skip if not wordid
			if ( ! m_wids[r] ) continue;
			// get its section
			Section *sr = m_sectionPtrs[r];
			// . skip if in div hidden section
			// . fixes www.switchboard.com/albuquerque-nm/doughnuts
			if ( sr->m_flags & SEC_HIDDEN ) continue;
			// ok, stop
			break;
		}
		// if no header, skip
		if ( r < 0 ) continue;
		// we are the first item
		//Section *first = sk;
		//int32_t firsta = i;
		Section *first = m_sectionPtrs[i];
		// set SEC_INPUT_HEADER
		setHeader ( r , first , SEC_INPUT_HEADER );

		// and the footer, if any
		r = i + 1;
		for ( ; r < m_nw && ! m_wids[r] ; r++ ) QUICKPOLL(m_niceness);
		// if no header, skip
		if ( r >= m_nw ) continue;
		// we are the first item
		//Section *first = sk;
		//int32_t firsta = i;
		//Section *first = m_sectionPtrs[i];
		// set SEC_INPUT_FOOTER
		setHeader ( r , first , SEC_INPUT_FOOTER );

	}


	int64_t h_copyright = hash64n("copyright");
	// copyright check
	// the copyright symbol in utf8 (see Entities.cpp for the code)
	char copy[3];
	copy[0] = 0xc2;
	copy[1] = 0xa9;
	copy[2] = 0x00;
	// scan all years, lists and ranges of years, and look for
	// a preceeding copyright sign. mark such years as DF_COPYRIGHT
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		// skip if tag
		if ( m_tids[i] ) continue;
		// do we have an alnum word before us here?
		if ( m_wids[i] ) {
			// if word check for copyright
			if ( m_wids[i] != h_copyright ) continue;
		}
		// must have copyright sign in it i guess
		else if ( ! gb_strncasestr(m_wptrs[i],m_wlens[i],copy)) 
			continue;
		// mark section as copyright section then
		Section *sp = m_sectionPtrs[i];
		// flag as menu
		sp->m_flags |= SEC_MENU;
	}


	sec_t ff = SEC_MENU | 
		SEC_INPUT_HEADER | 
		SEC_INPUT_FOOTER;

	// set SEC_MENU of child sections of SEC_MENU sections
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be a link text only section
		if ( ! ( si->m_flags & ff ) ) continue;
		// ignore if went down this path
		if ( si->m_used == 82 ) continue;
		// save it
		//Section *parent = si;
		// get first potential kid
		Section *sk = si->m_next;
		// scan child sections
		for ( ; sk ; sk = sk->m_next ) {
			// stop if not contained
			if ( ! si->contains ( sk ) ) break;
			// mark it
			sk->m_flags |= (si->m_flags & ff); // SEC_MENU;
			// ignore in big loop
			sk->m_used = 82;
		}
	}

	//
	// set SEC_MENU_HEADER
	//
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not in a menu
		if ( ! ( sk->m_flags & SEC_MENU ) ) continue;
		// get his list container
		Section *c = sk->m_listContainer;
		// skip if none
		if ( ! c ) continue;
		// already flagged?
		if ( c->m_used == 89 ) continue;
		// do not repeat on any item in this list
		c->m_used = 89;
		// flag all its brothers!
		Section *zz = sk;
		for ( ; zz ; zz = zz->m_nextBrother ) 
			// bail if not in menu
			if ( ! ( zz->m_flags & SEC_MENU ) ) break;
		// if broked it, stop
		if ( zz ) continue;
		//
		// ok, every item in list is a menu item, so try to set header
		//
		// get word before first item in list
		int32_t r = sk->m_a - 1;
		for ( ; r >= 0 && ! m_wids[r] ; r-- )
			QUICKPOLL(m_niceness);
		// if no header, skip
		if ( r < 0 ) continue;
		// set SEC_MENU_HEADER
		setHeader ( r , sk , SEC_MENU_HEADER );
	}

	//
	// unset SEC_MENU if a lot of votes for not dup
	//
	/*
	for ( Section *sk = m_rootSection ; sk ; sk = sk->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not in a menu
		if ( ! ( sk->m_flags & SEC_MENU ) ) continue;
		// . check him out
		// . this is only valid for sections that have SEC_NO_TEXT clr
		if ( sk->m_votesForNotDup <= sk->m_votesForDup ) continue;
		// ok, more likely not a dup i guess
		sk->m_flags &= ~SEC_MENU;
		// and every parent
		for ( Section *sp = sk ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if clear
			if ( ! ( sp->m_flags & SEC_MENU ) ) break;
			// unset it
			sp->m_flags &= ~SEC_MENU;
		}
	}
	*/

	//
	// set SEC_MENU_SENTENCE flag
	//
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be a link text only section
		if ( ! ( si->m_flags & SEC_MENU ) ) continue;
		// set this
		bool gotSentence = ( si->m_flags & SEC_SENTENCE );
		// set SEC_MENU of the sentence
		if ( gotSentence ) continue;
		// parent up otherwise
		for ( Section *sk = si->m_parent ; sk ; sk = sk->m_parent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// stop if sentence finally
			if ( ! ( sk->m_flags & SEC_SENTENCE ) ) continue;
			// not a menu sentence if it has plain text in it
			// though! we have to make this exception to stop
			// stuff like 
			// "Wedding Ceremonies, No preservatives, more... "
			// from switchboard.com from being a menu sentence
			// just because "more" is in a link.
			if ( sk->m_flags & SEC_PLAIN_TEXT ) break;
			// set it
			sk->m_flags |= SEC_MENU_SENTENCE;
			// and stop
			break;
		}
		// sanity check - must have sentence
		//if ( ! sk ) { char *xx=NULL;*xx=0; }
	}



	// just this here
	//ff = SEC_MENU_HEADER;

	// . now set generic list headers
	// . list headers can only contain one hard section with text
	// . list headers cannot have a previous brother section
	for ( int32_t i = 0 ; i + 1 < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		Section *si = &m_sections[i];
		// get container
		Section *c = si->m_listContainer;
		// skip if no container
		if ( ! c ) continue;
		// skip if already did this container
		if ( c->m_used == 55 ) continue;
		// mark it
		c->m_used = 55;
		// flag it
		c->m_flags |= SEC_CONTAINER;
		// skip the rest for now
		continue;
		/*
		// get first word before the list container
		int32_t r = c->m_a - 1;
		for ( ; r >= 0 && ! m_wids[r] ; r-- ) QUICKPOLL(m_niceness);
		// if no header, skip
		if ( r < 0 ) continue;
		// now see if the containing list section has an outside header
		//setHeader ( r , c , SEC_LIST_HEADER );
		//Section *maxhdr = setHeader ( r , c , SEC_LIST_HEADER );
		// skip if none
		//if ( ! maxhdr ) continue;
		// if it was a header, mark the list it is a header of
		//maxhdr->m_headerOfList = c;
		*/
	}

	static bool s_init = false;
	static int64_t h_close ;
	static int64_t h_send ;
	static int64_t h_map ;
	static int64_t h_maps ;
	static int64_t h_directions ;
	static int64_t h_driving ;
	static int64_t h_help ;
	static int64_t h_more ;
	static int64_t h_log ;
	static int64_t h_sign ;
	static int64_t h_change ;
	static int64_t h_write ;
	static int64_t h_save ;
	static int64_t h_share ;
	static int64_t h_forgot ;
	static int64_t h_home ;
	static int64_t h_sitemap ;
	static int64_t h_advanced ;
	static int64_t h_go ;
	static int64_t h_website ;
	static int64_t h_view;
	static int64_t h_add;
	static int64_t h_submit;
	static int64_t h_get;
	static int64_t h_about;
	// new stuff
	static int64_t h_back; // back to top
	static int64_t h_next;
	static int64_t h_buy; // buy tickets
	static int64_t h_english; // english french german versions
	static int64_t h_click;

	if ( ! s_init ) {
		s_init = true;
		h_close = hash64n("close");
		h_send = hash64n("send");
		h_map = hash64n("map");
		h_maps = hash64n("maps");
		h_directions = hash64n("directions");
		h_driving = hash64n("driving");
		h_help = hash64n("help");
		h_more = hash64n("more");
		h_log = hash64n("log");
		h_sign = hash64n("sign");
		h_change = hash64n("change");
		h_write = hash64n("write");
		h_save = hash64n("save");
		h_share = hash64n("share");
		h_forgot = hash64n("forgot");
		h_home = hash64n("home");
		h_sitemap = hash64n("sitemap");
		h_advanced = hash64n("advanced");
		h_go = hash64n("go");
		h_website = hash64n("website");
		h_view = hash64n("view");
		h_add = hash64n("add");
		h_submit = hash64n("submit");
		h_get = hash64n("get");
		h_about = hash64n("about");
		h_back = hash64n ("back");
		h_next = hash64n ("next");
		h_buy = hash64n ("buy");
		h_english = hash64n ("english");
		h_click = hash64n ("click");
	}

	// . when dup/non-dup voting info is not available because we are
	//   more or less an isolated page, guess that these links are
	//   menu links and not to be considered for title or event description
	// . we completely exclude a word from title/description if its 
	//   SEC_MENU is set.
	// . set SEC_MENU for renegade links that start with an action
	//   verb like "close" or "add" etc. but if their # of non dup votes
	//   is high relative to their # of dup votes, then do not set this
	//   because it might be a name of a band like "More" or something
	//   and be in a link
	// . scan all href sections
	// set SEC_LINK_ONLY on sections that just contain a link
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not a href section
		if ( si->m_baseHash != TAG_A ) continue;
		// set points to scan
		int32_t a = si->m_a;
		int32_t b = si->m_b;
		// assume not bad
		bool bad = false;
		int32_t i;
		// scan words if any
		for ( i = a ; i < b ; i++ ) {
			// skip if not word
			if ( ! m_wids[i] ) continue;
			// assume bad
			bad = true;
			// certain words are indicative of menus
			if ( m_wids[i] == h_close ) break;
			if ( m_wids[i] == h_send ) break;
			if ( m_wids[i] == h_map ) break;
			if ( m_wids[i] == h_maps ) break;
			if ( m_wids[i] == h_directions ) break;
			if ( m_wids[i] == h_driving ) break;
			if ( m_wids[i] == h_help ) break;
			if ( m_wids[i] == h_more ) break;
			if ( m_wids[i] == h_log ) break; // log in
			if ( m_wids[i] == h_sign ) break; // sign up/in
			if ( m_wids[i] == h_change ) break; // change my loc.
			if ( m_wids[i] == h_write ) break; // write a review
			if ( m_wids[i] == h_save ) break;
			if ( m_wids[i] == h_share ) break;
			if ( m_wids[i] == h_forgot ) break; // forgot your pwd
			if ( m_wids[i] == h_home ) break;
			if ( m_wids[i] == h_sitemap ) break;
			if ( m_wids[i] == h_advanced ) break; // adv search
			if ( m_wids[i] == h_go ) break; // go to top of page
			if ( m_wids[i] == h_website ) break;
			if ( m_wids[i] == h_view ) break;
			if ( m_wids[i] == h_add ) break;
			if ( m_wids[i] == h_submit ) break;
			if ( m_wids[i] == h_get ) break;
			if ( m_wids[i] == h_about ) break;
			if ( m_wids[i] == h_back ) break;
			if ( m_wids[i] == h_next ) break;
			if ( m_wids[i] == h_buy ) break;
			if ( m_wids[i] == h_english ) break;
			if ( m_wids[i] == h_click ) break;
			bad = false;
			break;
		}
		// skip if ok
		if ( ! bad ) continue;
		// get smallest section
		Section *sm = m_sectionPtrs[i];
		// . exempt if lots of non-dup votes relatively
		// . could be a band name of "More ..."
		//if ( sm->m_votesForNotDup > sm->m_votesForDup ) continue;
		// if bad mark it!
		sm->m_flags |= SEC_MENU;
	}			

	return true;
}

// "first" is first item in the list we are getting header for
void Sections::setHeader ( int32_t r , Section *first , sec_t flag ) {
	// get smallest section containing word #r
	Section *sr = m_sectionPtrs[r];
	// save orig
	Section *orig = sr;

	// save that
	//Section *firstOrig = first;
	// blow up until just before "first" section
	for ( ; sr ; sr = sr->m_parent ) {
		// forget it if in title tag already!
		if ( sr->m_flags & SEC_IN_TITLE ) return;
		// any brother means to stop! this basically means
		// we are looking for a single line of text as the menu
		// header
		//if ( sr->m_prevBrother ) return;
		// stop if no parent
		if ( ! sr->m_parent ) continue;
		// parent must not contain first
		if ( sr->m_parent->contains ( first ) ) break;
	}
	// if we failed to contain "first"... what does this mean? i dunno
	// but its dropping core for
	// http://tedserbinski.com/jcalendar/jcalendar.js
	if ( ! sr ) return;
	
	// save that
	Section *biggest = sr;

	// check out prev brother
	Section *prev = biggest->m_prevBrother;

	// if we are in a hard section and capitalized (part of the 
	// SEC_HEADING) requirements, then it should be ok if we have
	// a prev brother of a different tagid.
	// this will fix americantowns.com which has a list of header tags
	// and ul tags intermingled, with menus in the ul tags.
	// should also fix upcoming.yahoo.com which has alternating
	// dd and dt tags for its menus. now that we got rid of
	// addImpliedSections() we have to deal with this here, and it will
	// be more accurate since addImpliedSections() was often wrong.
	if ( prev &&
	     //(orig->m_flags & SEC_CAPITALIZED) &&
	     //!(orig->m_flags & SEC_ENDS_IN_PUNCT) &&
	     (orig->m_flags & SEC_HEADING) &&
	     prev->m_tagId != biggest->m_tagId )
		prev = NULL;

	// but if prev brother is a blank, we should view that as a delimeter
	// BUT really we should have added those sections in with the new
	// delimeter logic! but let's put this in for now anyway...
	if ( prev && prev->m_firstWordPos < 0 )
		prev = NULL;

	// if the header section has a prev brother, forget it!
	if ( prev ) return;

	// . if we gained extra text, that is a no-no then
	// . these two checks replaced the two commented out ones above
	// . they allow for empty sections preceeding "sr" at any level as
	//   we telescope it up
	if ( biggest->m_firstWordPos != orig->m_firstWordPos ) return;
	if ( biggest->m_lastWordPos  != orig->m_lastWordPos  ) return;

	/*
	// . scan all subsections of the header section
	// . only allowed to have one hard section containing text
	Section *hdr = biggest;
	for ( int32_t i = biggest->m_a ; i < biggest->m_b ; i++ ) {
		// need word
		if ( ! m_wids[i] ) continue;
		// get smallest *hard* section containg word
		Section *sp =  m_sectionPtrs[i];
		// get first hard section
		for ( ; ! sp->isHardSection() ; sp = sp->m_parent ) ;
		// if we had a different one already, that is bad! we can't
		// be a header section then!
		if ( firstHard && firstHard != sp ) return;
		// flag it
		firstHard = sp;

		sp->m_used = 33;
	*/

	
	// . now blow up first until just before it hits biggest as well
	// . this fixes reverbnation on the nextBrother check below
	for ( ; first ; first = first->m_parent ) {
		// stop if parent is NULL
		if ( ! first->m_parent ) break;
		// stop if parent would contain biggest
		if ( first->m_parent->contains ( biggest ) ) break;
	}
	// if after blowing it up "first" contains more than just menu
	// sections, then bail. that really was not a menu header!
	// fixes reverbnation url that thought "That 1 Guy" was a menu header.
	if ( flag == SEC_MENU_HEADER ) {
		Section *fx = first;
		for ( ; fx ; fx = fx->m_next ) {
			// stop when list is over
			if ( fx->m_a >= first->m_b ) break;
			// ignore if no next
			if ( fx->m_flags & SEC_NOTEXT ) continue;
			// thats bad if SEC_MENU not set, it should be for all!
			if ( fx->m_flags & SEC_MENU ) continue;
			// we got these now
			if ( fx->m_flags & SEC_MENU_SENTENCE ) continue;
			// otherwise, bad!
			return;
			//if ( ! ( fx->m_flags & SEC_MENU ) ) return;
		}
	}

	// strange?
	if ( ! sr ) { char *xx=NULL;*xx=0; }
	// scan until outside biggest
	int32_t lastb = biggest->m_b;
	// . make sure sr does not contain any list in it
	// . scan all sections between sr and "saved"
	for ( ; sr ; sr = sr->m_next ) {
		// stop if over
		if ( sr->m_a >= lastb ) break;
		// if we have a brother with same taghash we are
		// part of a list
		if ( sr->m_nextBrother &&
		     sr->m_nextBrother->m_tagHash == sr->m_tagHash &&
		     sr->m_nextBrother != first )
			return;
		if ( sr->m_prevBrother &&
		     sr->m_prevBrother->m_tagHash == sr->m_tagHash &&
		     // for footers
		     sr->m_prevBrother != first )
			return;
	}

	// restart loop
	sr = biggest;
	// ok, not part of a list, flag it
	for ( ; sr ; sr = sr->m_next ) {
		// stop if over
		if ( sr->m_a >= lastb ) break;
		// flag each subsection
		sr->m_flags |= flag; // SEC_MENU_HEADER;
		// mark it
		//if (flag == SEC_LIST_HEADER ) sr->m_headerOfList = firstOrig;
	}
}


// . set SEC_HEADING and SEC_HEADING_CONTAINER bits in Section::m_flags
// . identifies sections that are most likely headings
// . the WHOLE idea of this algo is to take a list of sections that are all 
//   the same tagId/baseHash and differentiate them so we can insert implied 
//   sections with headers. 
// . then addImpliedSections() uses the SEC_HEADING_CONTAINER bit to
//   modify the base hash to distinguish sections that would otherwise all
//   be the same! 
// . should fix salsapower.com and abqtango.com to have proper implied sections
// . it is better to not add any implied sections than to add the wrong ones
//   so be extra strict in our rules here.
bool Sections::setHeadingBit ( ) {

	int32_t headings = 0;
	// scan the sections
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if directly contains no text
		//if ( si->m_flags & SEC_NOTEXT ) continue;
		// SEC_NOTEXT is not set at this point
		int32_t fwp = si->m_firstWordPos;
		if ( fwp == -1 ) continue;
		// we must be the smallest container around this text
		if ( m_sectionPtrs[fwp] != si ) continue;

		// . make sure we are in our own hard section
		// . TODO: allow for bold or strong, etc. tags as well
		bool hasHard = false;
		int32_t a = si->m_firstWordPos;
		int32_t b = si->m_lastWordPos;
		// go to parent
		Section *pp = si;
		Section *biggest = NULL;
		bool inLink = false;
		// . we need to be isolated in our own hard section container
		// . TODO: what about "<b>Hi There <i>Bob</i></b>" as a heading
		// . i guess that will still work!
		for ( ; pp ; pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if breached
			if ( pp->m_firstWordPos != a ) break;
			if ( pp->m_lastWordPos  != b ) break;
			// record this
			if ( pp->m_tagId == TAG_A ) inLink = true;
			// record the biggest section containing just our text
			biggest = pp;
			// is it a hard section?
			if ( isHardSection(pp) )  hasHard = true;
			// . allow bold and strong tags
			// . fixes gwair.org which has the dates of the
			//   month in strong tags. so we need to set 
			//   SEC_HEADING for those so getDelimHash() will
			//   recognize such tags as date header tags in the
			//   METHOD_DOM algorithm and we get the proper
			//   implied sections
			if ( pp->m_tagId == TAG_STRONG ) hasHard = true;
			if ( pp->m_tagId == TAG_B      ) hasHard = true;
		}
		// need to be isolated in a hard section
		if ( ! hasHard ) continue;

		// . must be a <tr> or <p> tag... for now keep it constrained
		// . fixes events.mapchannels.com where we were allowing
		//   <td> cells to be headings in a row... kinda strange
		// . allow SEC_HEADING and SEC_HEADING_CONTAINER to be set
		//   here at least and nixed in the 2nd loop. that way we
		//   can use SEC_NIXED_HEADING_CONTAINER in the algo
		//   in Dates.cpp that sets SEC_TOD_EVENT
		// . WELL, i turned this off 11/8/11 and didn't seem to
		//   be needed! this should be tagid independent anyhow.
		/*
		if ( biggest->m_tagId != TAG_TR &&
		     // if we don't allow <td> cells we miss out on 
		     // "PAYNE NURSERIES INC" for 
		     // www.magicyellow.com/category/Nurseries_Plants_Trees/
		     // Santa_Fe_NM.html
		     biggest->m_tagId != TAG_TD && 		     
		     // http://www.fabnyc.org/calendar.php?type=class
		     biggest->m_tagId != TAG_TABLE && 		     
		     // fix pacificmedicalcenters.org
		     biggest->m_tagId != TAG_DT &&
		     biggest->m_tagId != TAG_P && 
		     biggest->m_tagId != TAG_P2 && 
		     biggest->m_tagId != TAG_H1 &&
		     biggest->m_tagId != TAG_H2 &&
		     biggest->m_tagId != TAG_H3 &&
		     biggest->m_tagId != TAG_H4 &&
		     // fix gwair.org to allow dates in strong tags to be
		     // headings so our getDelimHash() works. see comment
		     // above regarding gwair.org
		     biggest->m_tagId != TAG_STRONG && 
		     biggest->m_tagId != TAG_B )
			continue;
		*/

		// now make sure the text is capitalized etc
		bool hadUpper = false;
		//bool hadLower = false;
		int32_t lowerCount = 0;
		bool hadYear  = false;
		bool hadAlpha = false;
		int32_t i;
		// scan the alnum words we contain
		for ( i = a ; i <= b ; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// . did we hit a breaking tag?
			// . "<div> blah <table><tr><td>blah... </div>"
			if ( m_tids[i] && isBreakingTagId(m_tids[i]) ) break;
			// skip if not alnum word
			if ( ! m_wids[i] ) continue;
			// skip digits
			if ( is_digit(m_wptrs[i][0]) ) {
				// skip if not 4-digit year
				if ( m_wlens[i] != 4 ) continue;
				// . but if we had a year like "2010" that
				//   is allowed to be a header.
				// . this fixes 770kob.com because the events
				//   under the "2010" header were telescoping
				//   up into events in the "December 2009"
				//   section, when they should have been in
				//   their own section! and now they are in
				//   their own implied section...
				int32_t num = m_words->getAsLong(i);
				if ( num < 1800 ) continue;
				if ( num > 2100 ) continue;
				// mark it
				hadYear = true;
				continue;
			}
			// mark this
			hadAlpha = true;
			// is it upper?
			if ( is_upper_utf8(m_wptrs[i]) ) {
				hadUpper = true;
				continue;
			}
			// skip stop words
			if ( m_words->isStopWord(i) ) continue;
			// . skip int16_t words
			// . November 4<sup>th</sup> for facebook.com
			if ( m_wlens[i] <= 2 ) continue;
			// is it lower?
			if ( is_lower_utf8(m_wptrs[i]) ) lowerCount++;
			// stop now if bad
			//if ( hadUpper ) break;
			if ( lowerCount >= 2 ) break;
		}
		// is it a header?
		bool isHeader = hadUpper;
		// a single year by itself is ok though too
		if ( hadYear && ! hadAlpha ) isHeader = true;
		// not if we had a lower case guy
		//if ( hadLower ) isHeader = false;
		// allow for one mistake like we do in Events.cpp for titles
		if ( lowerCount >= 2 ) isHeader = false;
		// . mark as bad? we need at least one upper case word!
		// . seeing too many all lower case non-headers!
		//if ( ! hadUpper ) continue;
		// if we broke out early, give up
		//if ( i < b ) continue;
		if ( ! isHeader ) continue;

		// ok, mark this section as a heading section
		si->m_flags |= SEC_HEADING;

		// mark all parents to up to biggest
		biggest->m_flags |= SEC_HEADING_CONTAINER;

		// mark all up to biggest now too! that way the td section
		// gets marked and if the tr section gets replaced with a 
		// fake tc section then we are ok for METHOD_ABOVE_DOW!
		for ( Section *pp = si; // ->m_parent ;  (bug!)
		      pp && pp != biggest ; 
		      pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			pp->m_flags |= SEC_HEADING_CONTAINER;
		}

		// a hack!
		if ( inLink ) biggest->m_flags |= SEC_LINK_TEXT;

		// count them
		headings++;
	}

	// bail now if no headings were set
	if ( ! headings ) return true;

	// . now scan sections again and scan each list of brothers
	//   whose m_used is not set to 33
	// . if it is set to 33 that means we already scanned it
	// . and unset SEC_HEADING if brothers are not all the same tr or p tag
	/*
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not a heading container
		if ( ! ( si->m_flags & SEC_HEADING_CONTAINER) ) continue;
		// skip if already did
		if ( si->m_used == 33 ) continue;
		// we got one, get the brother list
		Section *bro = si;
		// back up to first brother
		for ( ; bro->m_prevBrother ; bro = bro->m_prevBrother ) 
			// breathe
			QUICKPOLL(m_niceness);
		// skip NIXED for now
		//continue;
		// save it
		Section *first = bro;
		// are we a bad list?
		char bad = 0;
		// now do the full scan
		for ( ; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . must all be the same baseHash (kinda like tagId)
			// . the WHOLE idea of this algo is to take a list
			//   of sections that are all the same tagId/baseHash
			//   and differentiate them so we can insert
			//   implied sections with headers
			//if ( bro->m_baseHash != si->m_baseHash ) bad = 1;
			// abqtango.org has all <p> tags but with different
			// attributes... so fix that...
			if ( bro->m_tagId != si->m_tagId ) bad = 1;
			// if has link text and is heading, that is bad
			if ( ( bro->m_flags & SEC_HEADING_CONTAINER  ) &&
			     ( bro->m_flags & SEC_LINK_TEXT ) )
				bad = 2;
			// mark it
			bro->m_used = 33;
		}
		// if not bad, keep it
		if ( ! bad ) continue;
		// clear the whole list
		bro = first;
		// scan again
		for ( ; bro ; bro = bro->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not set
			if ( ! ( bro->m_flags & SEC_HEADING_CONTAINER ) )
				continue;
			// unset
			bro->m_flags &= ~SEC_HEADING_CONTAINER;
			// note it for Dates.cpp to use
			bro->m_flags |=  SEC_NIXED_HEADING_CONTAINER;
		}
	}
	*/
	return true;
}

void Sections::setTagHashes ( ) {

	if ( m_numSections == 0 ) return;

	// now recompute the tagHashes and depths and content hashes since
	// we have eliminate open-ended sections in the loop above
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// these have to be in order of sn->m_a to work right
		// because we rely on the parent tag hash, which would not
		// necessarily be set if we were not sorted, because the 
		// parent section could have SEC_FAKE flag set because it is
		// a br section added afterwards.
		//Section *sn = m_sorted[i]; // sections[i];
		// int16_tcut
		int64_t bh = (int64_t)sn->m_baseHash;
		//int64_t fh = sn->m_tagId;
		// sanity check
		if ( bh == 0 ) { char *xx=NULL;*xx=0; }
		// if no parent, use initial values
		if ( ! sn->m_parent ) {
			sn->m_depth   = 0;
			sn->m_tagHash = bh;
			sn->m_turkTagHash32 = sn->m_turkBaseHash;//m_tagId;
			//sn->m_turkTagHash32 = bh;
			//sn->m_formatHash = fh;
			// sanity check
			if ( bh == 0 ) { char *xx=NULL;*xx=0; }
			continue;
		}
		// sanity check
		if ( sn->m_parent->m_tagHash == 0 ) { char *xx=NULL;*xx=0; }

		// . update the cumulative front tag hash
		// . do not include hyperlinks as part of the cumulative hash!
		sn->m_tagHash = hash32h ( bh , sn->m_parent->m_tagHash );

		// now use this for setting Date::m_dateTagHash instead
		// of using Section::m_tagHash since often the dates like
		// for zvents.org are in a <tr id=xxxx> where xxxx changes
		sn->m_turkTagHash32 = 
			//hash32h ( sn->m_tagId, sn->m_parent->m_turkTagHash );
			hash32h ( sn->m_turkBaseHash,
				  sn->m_parent->m_turkTagHash32 );

		sn->m_colorHash = hash32h ( bh , sn->m_parent->m_colorHash );

		//if(fh)sn->m_formatHash=hash32h(fh,sn->m_parent->m_formatHash)
		//else  sn->m_formatHash=sn->m_parent->m_formatHash;

		// if we are an implied section, just use the tag hash of
		// our parent. that way since we add different implied
		// sections for msichicago.com root than we do the kid,
		// the section voting should still match up
		if ( bh == BH_IMPLIED ) {
			sn->m_tagHash     = sn->m_parent->m_tagHash;
			sn->m_turkTagHash32 = sn->m_parent->m_turkTagHash32;
		}

		// sanity check
		// i've seen this happen once for
		// sn->m_parent->m_tagHash = 791013962
		// bh = 20020
		if ( sn->m_tagHash == 0 ) sn->m_tagHash = 1234567;
		// depth based on parent, too
		//if ( tid != TAG_A ) sn->m_depth = sn->m_parent->m_depth + 1;
		//else                sn->m_depth = sn->m_parent->m_depth    ;
		sn->m_depth = sn->m_parent->m_depth + 1;
	}
}

bool Sections::containsTagId ( Section *si, nodeid_t tagId ) {
	// scan sections to right
	int32_t a = si->m_a + 1;
	// scan as int32_t as contained by us
	for ( ; a < m_nw && a < si->m_b ; a++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		if ( m_tids[a] == tagId ) return true;
	}
	return false;
}

bool Sections::setTableStuff ( ) {
	char tbuf[1024];
	HashTableX tdups;
	tdups.set(4,0,64,tbuf,1024,false,m_niceness,"tdupt");
	for ( int32_t i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get section
		Section *si = di->m_section;
		// skip if not in body
		if ( ! si ) continue;
		// need to be in a table
		Section *ts = si->m_tableSec;
		// skip if none
		if ( ! ts ) continue;
		// if already did, forget it!
		if ( tdups.isInTable ( &ts ) ) continue;
		// add it
		if ( ! tdups.addKey ( &ts ) ) return false;
		// do it
		if ( ! setTableDateHeaders ( ts ) ) return false;
	}
	return true;
}

// . does table have a date header row or column?
// . we need this for our weekly schedule detection algorithm
// . many sites have a row header this is the days of the week
// . sometimes they have tods in the first column, and sometimes they
//   just put the tods and tod ranges in the table cells directly.
// . sets Section::m_flags SEC_HASDATEHEADERCOL/ROW for JUST the table
//   section if it indeed has such date headers
// . Dates::isCompatible() looks at that table flag to see if it should
//   apply special processing when deciding if two dates should be paired
// . then we set DF_TABLEDATEHEADERROW/COL for the dates in those
//   header rows/cols so that we can set SF_RECURRING_DOW if the dow date
//   was in the header row/col
bool Sections::setTableDateHeaders ( Section *ts ) {

	// skip if only one row and column
	if ( ! ( ts->m_flags & SEC_MULTIDIMS ) ) return true;

	char adtbuf[1000];
	HashTableX adt;
	adt.set ( 4 , 0 , 64 , adtbuf, 1000, false,m_niceness,"adttab");

	// sanity check
	//if ( ! m_firstDateValid ) { char *xx=NULL;*xx=0; }
	// return right away if table contains no dates
	//int32_t dn = ts->m_firstDate - 1;
	//if ( dn < 0 ) return true;
	int32_t dn = 0;

	Section *headerCol = NULL;
	Section *headerRow = NULL;

	for ( int32_t i = dn ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if nuked. part of a compound, or in bad section.
		if ( ! di ) continue;
		// must be in table
		if ( di->m_a < ts->m_a ) continue;
		// stop if out of table
		if ( di->m_a >= ts->m_b ) break;
		// sanity check
		if ( di->m_a < 0 ) continue;
		// if it is a single tod then make sure not like "8.32"
		if ( di->m_flags & DF_FUZZY ) continue;
		// get date's section
		Section *si = di->m_section;
		// must be in that table.. in subtable?? then skip...
		if ( si->m_tableSec != ts ) continue;

		// get the table cell section
		Section *cell = si;
		for ( ; cell ; cell = cell->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( cell->m_tagId == TAG_TD ) break;
			if ( cell->m_tagId == TAG_TH ) break;
		}
		// how does this happen?
		if ( ! cell ) continue;
		// . do not add our hash if not just a date
		// . we are not header material if more than just a date
		wbit_t *bb = NULL;
		if ( m_bits ) bb = m_bits->m_bits;
		bool hasWord = false;
		for ( int32_t j = cell->m_a ; j < cell->m_b ; j++ ) {
			QUICKPOLL(m_niceness);
			if ( ! bb ) break;
			if ( ! m_wids[j] ) continue;
			if ( bb[j] & D_IS_IN_DATE ) continue;
			hasWord = true;
			break;
		}
		// we are not date header material if we got more than
		// just a date in our td/th cell
		if ( hasWord ) continue;

		// get date types
		datetype_t dt = di->m_hasType;
		// see if same date type in prev row or col
		int32_t row = si->m_rowNum;
		int32_t col = si->m_colNum;
		int32_t prevRow = row - 1;
		int32_t prevCol = col - 1;
		int32_t h;
		// zero is invalid row
		if ( prevRow >= 1 && ! headerCol ) {
			h = hash32h ( prevRow , col );
			h = hash32h ( (int32_t)(PTRTYPE)ts , h );
			h = hash32h ( dt , h ); // datetype
			if ( adt.isInTable ( &h ) ) {
				headerCol = cell;
				//break;
			}
		}
		// zero is invalid col
		if ( prevCol >= 1 && ! headerRow ) {
			h = hash32h ( row , prevCol );
			h = hash32h ( (int32_t)(PTRTYPE)ts , h );
			h = hash32h ( dt , h ); // datetype
			if ( adt.isInTable ( &h ) ) {
				headerRow = cell;
				//break;
			}
		}
		// add our hash
		h = hash32h ( row , col );
		h = hash32h ( (int32_t)(PTRTYPE)ts , h );
		h = hash32h ( dt , h ); // datetype
		if ( ! adt.addKey ( &h ) ) return false;
	}
	// set flags in all cells of table
	sec_t sef = 0;
	if ( headerCol ) sef |= SEC_HASDATEHEADERCOL;
	if ( headerRow ) sef |= SEC_HASDATEHEADERROW;
	// bail if none
	if ( ! sef ) return true;
	// just set on table now to avoid confusion
	ts->m_flags |= sef;

	// . set DF_ISTABLEHEADER on dates in the headrow and headcol
	// . then we can set SF_RECURRING if its a dow because its a 
	//   weekly schedule then
	// . we are setting implied sections so we do not have telescoped
	//   dates at this point.
	for ( int32_t i = dn ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if nuked. part of a compound, or in bad section.
		if ( ! di ) continue;
		// in row?
		Section *ds = di->m_section;
		// must be in body
		if ( ! ds ) continue;
		// stop if beyond table
		if ( ds->m_a >= ts->m_b ) break;
		// must be in table
		if ( ds->m_tableSec != ts ) continue;
		// is date in a header column?
		if ( headerCol && headerCol->m_colNum == ds->m_colNum )
			di->m_flags |= DF_TABLEDATEHEADERCOL;
		// is date in a header row?
		if ( headerRow && headerRow->m_rowNum == ds->m_rowNum )
			di->m_flags |= DF_TABLEDATEHEADERROW;
	}
		
	return true;
}

/*
bool Sections::swoggleTables ( ) {

	// . turn this off for now because it is too confusing
	// . really just need to rewrite the entire table and re-set
	//   the sections class!!
	return true;

	Section *lastTable = NULL;
	// scan dates until we find one in a table
	for ( int32_t i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if nuked. part of a compound, or in bad section.
		if ( ! di ) continue;
		// sanity check
		if ( di->m_a < 0 ) continue;
		// get section
		Section *si = di->m_section;
		// must be in a table
		if ( ! ( si->m_flags & SEC_IN_TABLE ) ) continue;
		// get our table we are swoggling
		Section *ts = si->m_tableSec;
		// seems like this happens for bad sections like
		// display:none hidden sections etc.
		if ( ! ts ) continue;
		// bail if already did it
		if ( ts == lastTable ) continue;
		// set it
		lastTable = ts;
		// swoggle that table
		if ( ! swoggleTable ( i , ts ) ) return false;
	}
	return true;
}

// . swoggle the table at section "ts"
// . start at date #dn
bool Sections::swoggleTable ( int32_t dn , Section *ts ) { 

	char adtbuf[1000];
	HashTableX adt;
	adt.set ( 4 , 0 , 64 , adtbuf, 1000, false,m_niceness,"adttab");

	bool adjacentColumns = false;
	bool adjacentRows    = false;
	for ( int32_t i = dn ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if nuked. part of a compound, or in bad section.
		if ( ! di ) continue;
		// sanity check
		if ( di->m_a < 0 ) continue;
		// if it is a single tod then make sure not like "8.32"
		if ( di->m_flags & DF_FUZZY ) continue;
		// get date's section
		Section *si = di->m_section;
		// must be in that table
		if ( si->m_tableSec != ts ) break;
		// get date types
		datetype_t dt = di->m_hasType;
		// see if same date type in prev row or col
		int32_t row = si->m_rowNum;
		int32_t col = si->m_colNum;
		int32_t prevRow = row - 1;
		int32_t prevCol = col - 1;
		int32_t h;
		if ( prevRow >= 0 ) {
			h = hash32h ( prevRow , col );
			h = hash32h ( (int32_t)ts , h );
			h = hash32h ( dt , h ); // datetype
			if ( adt.isInTable ( &h ) ) {
				adjacentColumns = true;
				//break;
			}
		}
		if ( prevCol >= 0 ) {
			h = hash32h ( row , prevCol );
			h = hash32h ( (int32_t)ts , h );
			h = hash32h ( dt , h ); // datetype
			if ( adt.isInTable ( &h ) ) {
				adjacentRows = true;
				//break;
			}
		}
		// add our hash
		h = hash32h ( row , col );
		h = hash32h ( (int32_t)ts , h );
		h = hash32h ( dt , h ); // datetype
		if ( ! adt.addKey ( &h ) ) return false;
	}
	// bail if nobody adjacent or both!
	if ( ! adjacentColumns && ! adjacentRows ) return true;
	if (   adjacentColumns &&   adjacentRows ) return true;
	if (   adjacentColumns ) return true;

	Section *next    = NULL;
	Section *tailSec = NULL;
	// . remove <tr> sections from the doubly linked list of sections
	// . up <tr> children's m_parent in the loop below though
	for ( Section *si = ts ; si ; si = next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get next in case excised
		next = si->m_next;
		// stop if out of table
		if ( si->m_a >= ts->m_b ) break;
		// remember next section after last row
		tailSec = si->m_next;
		// skip if not td section or th
		if ( si->m_tagId != TAG_TR ) continue;
		// excise from linked list
		si->m_prev->m_next = si->m_next;
		si->m_next->m_prev = si->m_prev;
		// its a doubly linked list
		si->m_next->m_prev = si->m_prev;
		si->m_prev->m_next = si->m_next;
		// sanity
		si->m_next = NULL;
		si->m_prev = NULL;
	}

#define MAXCOLS 50
	Section *colSections[MAXCOLS];
	Section *lastColCell[MAXCOLS];
	Section *lastColKid [MAXCOLS];
	for ( int32_t i = 0 ; i < MAXCOLS ; i++ ) {
		colSections[i] = NULL;
		lastColCell[i] = NULL;
		lastColKid [i] = NULL;
	}
	int32_t maxColnum = 0;

	// scan the <td> sections in the table
	for ( Section *si = ts ; si ; si = next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get next in case excised
		next = si->m_next;
		// stop if out of table
		if ( si->m_a >= ts->m_b ) break;
		// skip if not td section or th
		if ( si->m_tagId != TAG_TD && si->m_tagId != TAG_TH ) continue;
		// parent must be table now since we excised row
		if ( ! si->m_parent ) continue;
		// must be parent of excised row of THIS table, not a td
		// in another table inside this one
		if ( si->m_parent->m_parent != ts ) continue;
		// excise ourselves so we can re-add in a better place
		//si->m_prev->m_next = NULL; // si->m_next;
		//si->m_next->m_prev = si->m_prev;
		// its a doubly linked list
		//si->m_next->m_prev = si->m_prev;
		//si->m_prev->m_next = si->m_next;
		// int16_tcut
		int32_t colnum = si->m_colNum;
		// cram everything into last column in case of too many cols
		if ( colnum >= MAXCOLS ) colnum = MAXCOLS-1;
		// sanity
		if ( colnum < 0 ) { char *xx=NULL;*xx=0; }
		// get col section. starts at colnum=1, not 0!
		Section *cs = colSections[colnum];
		// make a new col section if haven't already
		if ( ! cs ) {
			// overflow check
			if ( m_numSections >= m_maxNumSections) {
				char *xx=NULL;*xx=0;}
			// insert it
			cs = &m_sections[m_numSections++];
			// count them as well
			if ( colnum > maxColnum ) maxColnum = colnum;
			// get the previous colnum from us that is non-NULL, 
			// but use the table itself if we are the first.
			Section *prevCol = NULL;
			for ( int32_t pc = colnum - 1; pc >= 1 ; pc-- ) {
				if ( ! colSections[pc] ) continue;
				prevCol = colSections[pc];
				break;
			}
			// the column sections are in a brother list
			cs->m_prevBrother = prevCol;
			if ( prevCol ) prevCol->m_nextBrother = cs;
			// make it an implied section
			cs->m_baseHash = BH_IMPLIED;
			// reset these
			cs->m_headColSection = NULL;
			cs->m_headRowSection = NULL;
			cs->m_firstWordPos = -1;
			cs->m_lastWordPos  = -1;
			cs->m_firstPlaceNum= -1;
			cs->m_senta        = -1;
			cs->m_sentb        = -1;
			cs->m_a            = si->m_a;
			cs->m_b            = si->m_b;
			cs->m_parent       = ts;
			cs->m_tagId        = TAG_TC;
			cs->m_flags        = 0;
			cs->m_flags       |= SEC_NOTEXT;
			// store in temp array
			colSections[colnum] = cs;
		}
		// get it
		if ( si->m_a == 1156 && si->m_b == 1171 )
			log("you");
		// col sec is our new parent, not the <tr> section we erased
		si->m_parent = cs;
		// update col section's m_b
		if ( si->m_b > cs->m_b ) cs->m_b = si->m_b;
		// assume not in brother list
		si->m_prevBrother = NULL;
		si->m_nextBrother = NULL;
		// and in the doubly linked list we now follow the
		// last one in the colsection
		Section *lastCell = lastColCell[colnum];
		// insert into brother list if we are not the first
		if ( lastCell ) {
			lastCell->m_nextBrother = si;
			si->m_prevBrother      = lastCell;
		}
		// get last section in this th/td cell
		Section *lastKid = si;
		// endpoint of that th/td cell
		int32_t b = lastKid->m_b;
		// and find the true last kid in this cell
		for ( ; lastKid ; lastKid = lastKid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if we are the last section period
			if ( ! lastKid->m_next ) break;
			// keep looping as int32_t as the "lastKid" 
			// section is contained in this td/th cell
			if ( lastKid->m_next->m_a >= b ) break;
		}

		// stitch the th/td cells in the same column together
		Section *prevLastKid = lastColKid[colnum];
		// link this cell up with the previous section it should be w/
		if ( prevLastKid ) {
			prevLastKid->m_next = si;
			si->m_prev          = prevLastKid;
		}
		else {
			si->m_prev = cs;
			cs->m_next = si;
		}

		// this td/th section is not connected to anything else yet. 
		// if we do not connect it right above we will connect it up
		// in the loop below.
		// no, we can't do this because it messes up our scan for
		// this for loop!
		//lastKid->m_next = NULL;
		// we are the new last kid for this col #
		lastColCell[colnum] = si;
		lastColKid [colnum] = lastKid;
	}

	//
	// now set the m_prev/m_next members of each column section
	//
	Section *prevLastKid = NULL;
	for ( int32_t i = 1 ; i <= maxColnum ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Section *cs = colSections[i];
		// skip if empty
		if ( ! cs ) continue;
		// get last section in this column
		Section *lastKid = lastColKid[i];
		// insert column into linked list with the lastKid
		// of the previous column
		if ( prevLastKid ) {
			cs->m_prev          = prevLastKid;
			prevLastKid->m_next = cs;
		}
		else {
			cs->m_prev = ts;
			ts->m_next = cs;
		}
		// assume we are the final lastKid and will not be connected
		// with the "prevLastKid" logic above
		lastKid->m_next = tailSec;
		tailSec->m_prev = lastKid;
		// set this now
		prevLastKid = lastKid;
	}

	//verifySections();

	return true;
}
*/


// . just the voting info for passing into diffbot in json
// . along w/ the title/summary/etc. we can return this json blob for each search result
bool Sections::printVotingInfoInJSON ( SafeBuf *sb ) {

	// save ptrs
	m_sbuf = sb;
	m_sbuf->setLabel ("sectprnt2");

	// print sections out
	for ( Section *sk = m_rootSection ; sk ; ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// print this section
		printSectionDiv ( sk , FORMAT_JSON ); // forProCog );
		// advance
		int32_t b = sk->m_b;
		// stop if last
		if ( b >= m_nw ) break;
		// get section after that
		sk = m_sectionPtrs[b];
	}

	// ensure ends in \0
	if ( ! sb->nullTerm() ) return false;

	return true;
}


// make this replace ::print() when it works
bool Sections::print2 ( SafeBuf *sbuf ,
			int32_t hiPos,
			int32_t *wposVec,
			char *densityVec,
			char *diversityVec,
			char *wordSpamVec,
			char *fragVec,
			HashTableX *st2 ,
			HashTableX *tt  ,
			Addresses *aa ,
			char format ) { // bool forProCog ){
	//FORMAT_PROCOG FORMAT_JSON HTML

	//sbuf->safePrintf("<b>Sections in Document</b>\n");

	// save ptrs
	m_sbuf = sbuf;

	m_sbuf->setLabel ("sectprnt");

	//m_pt = pt;
	//m_et = et;
	//m_at = at;
	//m_priceTable = priceTable;
	m_aa = aa;
	m_hiPos = hiPos;

	m_wposVec      = wposVec;
	m_densityVec   = densityVec;
	m_diversityVec = diversityVec;
	m_wordSpamVec  = wordSpamVec;
	m_fragVec      = fragVec;

	//verifySections();

	char  **wptrs = m_words->getWords    ();
	int32_t   *wlens = m_words->getWordLens ();
	//nodeid_t *tids = m_words->getTagIds();
	int32_t    nw    = m_words->getNumWords ();

	// check words
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get section
		Section *sn = m_sectionPtrs[i];
		if ( sn->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( sn->m_b <= i ) { char *xx=NULL;*xx=0; }
	}


	// print sections out
	for ( Section *sk = m_rootSection ; sk ; ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// print this section
		printSectionDiv ( sk , format );//forProCog );
		// advance
		int32_t b = sk->m_b;
		// stop if last
		if ( b >= m_nw ) break;
		// get section after that
		sk = m_sectionPtrs[b];
	}

	if ( format != FORMAT_HTML ) return true; // forProCog

	// print header
	char *hdr =
		"<table border=1>"
		"<tr>"
		"<td><b>sec #</b></td>"
		"<td><b>wordStart</b></td>"
		"<td><b>wordEnd</b></td>"
		"<td><b>baseHash</b></td>"
		"<td><b>cumulTagHash</b></td>"
		"<td><b>contentHash</b></td>"
		"<td><b>contentTagHash</b></td>"
		"<td><b>XOR</b></td>" // only valid for contentHashes
		"<td><b>alnum words</b></td>" // contained in section
		"<td><b>depth</b></td>"
		"<td><b>parent word range</b></td>"
		"<td><b># siblings</b></td>"
		"<td><b>flags</b></td>"
		"<td><b>evIds</b></td>"
		"<td><b>text snippet</b></td>"
		//"<td>votes for static</td>"
		//"<td>votes for dynamic</td>"
		//"<td>votes for texty</td>"
		//"<td>votes for unique</td>"
		"</tr>\n";
	sbuf->safePrintf("%s",hdr);

	int32_t rcount = 0;
	int32_t scount = 0;
	// show word # of each section so we can look in PageParser.cpp's
	// output to see exactly where it starts, since we now label all
	// the words
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("<!--ignore--></table>%s\n",hdr);
		// get it
		//Section *sn = &m_sections[i];
		//Section *sn = m_sorted[i];
		// skip if not a section with its own words
		//if ( sn->m_flags & SEC_NOTEXT ) continue;
		char *xs = "--";
		char ttt[100];
		if ( sn->m_contentHash64 ) {
			int32_t modified = sn->m_tagHash ^ sn->m_contentHash64;
			sprintf(ttt,"0x%"XINT32"",modified);
			xs = ttt;
		}
		// int16_tcut
		Section *parent = sn->m_parent;
		int32_t pswn = -1;
		int32_t pewn = -1;
		if ( parent ) pswn = parent->m_a;
		if ( parent ) pewn = parent->m_b;
		// print it
		sbuf->safePrintf("<!--ignore--><tr><td>%"INT32"</td>\n"
				 "<td>%"INT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%s</td>"
				 "<td>%"INT32"</td>"
				 "<td>%"INT32"</td>"
				 "<td><nobr>%"INT32" to %"INT32"</nobr></td>"
				 "<td>%"INT32"</td>"
				 "<td><nobr>" ,
				 scount++,//i,
				 sn->m_a,
				 sn->m_b,
				 (int32_t)sn->m_baseHash,
				 (int32_t)sn->m_tagHash,
				 (int32_t)sn->m_contentHash64,
				 (int32_t)(sn->m_contentHash64^sn->m_tagHash),
				 xs,
				 sn->m_exclusive,
				 sn->m_depth,
				 pswn,
				 pewn,
				 sn->m_numOccurences);//totalOccurences );
		// now show the flags
		printFlags ( sbuf , sn , false );
		// first few words of section
		int32_t a = sn->m_a;
		int32_t b = sn->m_b;
		// -1 means an unclosed tag!! should no longer be the case
		if ( b == -1 ) { char *xx=NULL;*xx=0; }//b=m_words->m_numWords;
		sbuf->safePrintf("</nobr></td>");

		sbuf->safePrintf("<td>&nbsp;</td>");

		sbuf->safePrintf("<td><nobr>");
		// 70 chars max
		int32_t   max   = 70; 
		int32_t   count = 0;
		char   truncated = 0;
		// do not print last word/tag in section
		for ( int32_t i = a ; i < b - 1 && count < max ; i++ ) {
			char *s    = wptrs[i];
			int32_t  slen = wlens[i];
			if ( count + slen > max ) {
				truncated = 1; 
				slen = max - count;
			}
			count += slen;
			// boldify front tag
			if ( i == a ) sbuf->safePrintf("<b>");
			sbuf->htmlEncode(s,slen,false);
			// boldify front tag
			if ( i == a ) sbuf->safePrintf("</b>");
		}
		// if we truncated print a ...
		if ( truncated ) sbuf->safePrintf("<b>...</b>");
		// then print ending tag
		if ( b < nw ) {
			int32_t blen = wlens[b-1];
			if ( blen>20 ) blen = 20;
			sbuf->safePrintf("<b>");
			sbuf->htmlEncode(wptrs[b-1],blen,false);
			sbuf->safePrintf("</b>");
		}

		sbuf->safePrintf("</nobr></td></tr>\n");
	}
			 
	sbuf->safePrintf("</table>\n<br>\n");


	return true;
}

/*
bool SectionVotingTable::print ( SafeBuf *sbuf , char *title ) {


	// title = "new/old section voting table"
	sbuf->safePrintf("<b>%s</b>\n",title );
	sbuf->safePrintf("<br>");
	sbuf->safePrintf("<i>turkTagHash is combined with sectionType to "
			 "make the "
			 "key in sectiondb. The data is everything else. "
			 "<br>"
			 "*The turkTagHash is XOR'ed with the contentHash "
			 "for the contentHash "
			 "section type, and is the tagPairHash for the "
			 "tagPairHash section type.</i>");
	// print table header
	char *hdr2 =
		"<table border=1>"
		"<tr>"
		"<td><b>siteHash</b></td>"
		"<td><b>turkTagHash*</b></td>"
		"<td><b>sectionType</b></td>"
		"<td><b>scoreTotal</b></td>"
		"<td><b>numSampled</b></td>"
		"<td><b>avgScore</b></td>"
		"</tr>\n";
	sbuf->safePrintf("%s",hdr2);
	HashTableX *st = &m_svt;
	int32_t rcount = 0;
	for ( int32_t i = 0 ; i < st->m_numSlots ; i++ ) {
		// breathe
		//QUICKPOLL(m_niceness);
		// skip if empty
		if ( ! st->m_flags[i] ) continue;
		// see if one big table causes a browser slowdown
		if ( (++rcount % TABLE_ROWS ) == 0 ) 
			sbuf->safePrintf("<!--ignore--></table>%s\n",hdr2);
		// get the key
		uint64_t k = *(uint64_t *)st->getKey ( i );
		// get the data
		SectionVote *sv = (SectionVote *)st->getValueFromSlot ( i );
		// parse key
		int32_t turkTagHash = (int32_t)(k & 0xffffffff);
		int32_t sectionType = (int32_t)(k >> 32);
		//int32_t tagHash     = (int32_t)(k >> 32);
		//int32_t sectionType = (int32_t)(k & 0xffffffff);
		// convert to string
		char *st = getSectionTypeAsStr ( sectionType );
		float avg = 0.0;
		if ( sv->m_numSampled > 0 ) avg = sv->m_score/sv->m_numSampled;
		sbuf->safePrintf("<tr>"
				 "<td>--</td>"
				 "<td>0x%"XINT32"</td>"
				 "<td>%s</td>"
				 "<td>%.02f</td>"
				 "<td>%.02f</td>"
				 "<td>%.02f</td>"
				 "</tr>\n",
				 turkTagHash,
				 st,
				 sv->m_score,
				 sv->m_numSampled ,
				 avg );
	}
	sbuf->safePrintf("</table>\n<br><br>\n");



	sbuf->safePrintf("<table border=1 cellpadding=3>\n");
	sbuf->safePrintf(
			 "<tr><td><nobr><b>Section Type</b></nobr></td>"
			 "<td><b>Description</b></td></tr>\n"
			 
			 "<tr><td>texty</td><td>An average "
			 "score of 0.0 means the "
			 "section does not have any words that are not "
			 "in anchor test. An average "
			 "score of 1.0 means the section "
			 "has words, none of which are in anchor text."
			 "</td></tr>\n"

			 "<tr><td>clock</td><td>An average "
			 "score of 1.0 means "
			 "section identified as "
			 "containing a clock. An average "
			 "score of 0.0 mean it contains "
			 "a date that is definitely not a clock."
			 "</td></tr>\n"

			 "<tr><td>eurdatefmt</td><td>An average "
			 "score of 1.0 means "
			 "the section contains a date in european format, "
			 "which means day first, not month. An average "
			 "score of 0.0 "
			 "means the section contains a date that is NOT in "
			 "european format.</td></tr>\n"

			 //"<tr><td>event</td><td>"
			 //"</td></tr>\n"

			 //"<tr><td>address</td><td></td></tr>\n"

			 "<tr><td>tagpairhash</td><td>"
			 "All unique adjacent tag ids are hashed to form "
			 "this number which represents the overal strucutre "
			 "of the document. The turkTagHash is the tagPairHash "
			 "in this case."
			 "</td></tr>\n"

			 "<tr><td>contenthash</td><td>"
			 "The turkTagHash in this case is really "
			 "the content hash of the words in the section XORed "
			 "with the original turkTagHash."
			 "</td></tr>\n"

			 //"<tr><td>textmaxsmpl</td><td></td></tr>\n"
			 //"<tr><td>waitinline</td><td></td></tr>\n"
			 //"<tr><td></td><td></td></tr>\n"
			 );
	sbuf->safePrintf("</table>\n<br><br>\n");
	// must be \0 terminated
	if ( sbuf->m_buf[sbuf->m_length] ) { char *xx=NULL;*xx=0; }

	return true;
}
*/

bool Sections::printSectionDiv ( Section *sk , char format ) { // bool forProCog ) {
	//log("sk=%"INT32"",sk->m_a);
	// enter a new div section now
	m_sbuf->safePrintf("<br>");
	// only make font color different
	int32_t bcolor = (int32_t)sk->m_colorHash& 0x00ffffff;
	int32_t fcolor = 0x000000;
	int32_t rcolor = 0x000000;
	uint8_t *bp = (uint8_t *)&bcolor;
	bool dark = false;
	if ( bp[0]<128 && bp[1]<128 && bp[2]<128 ) 
		dark = true;
	// or if two are less than 50
	if ( bp[0]<100 && bp[1]<100 ) dark = true;
	if ( bp[1]<100 && bp[2]<100 ) dark = true;
	if ( bp[0]<100 && bp[2]<100 ) dark = true;
	// if bg color is dark, make font color light
	if ( dark ) {
		fcolor = 0x00ffffff;
		rcolor = 0x00ffffff;
	}
	// start the new div
	m_sbuf->safePrintf("<div "
			 "style=\""
			 "background-color:#%06"XINT32";"
			 "margin-left:20px;"
			 "border:#%06"XINT32" 1px solid;"
			 "color:#%06"XINT32"\">",
			 //(int32_t)sk,
			 bcolor,
			 rcolor,
			 fcolor);

	bool printWord = true;
	if ( ! sk->m_parent && sk->m_next && sk->m_next->m_a == sk->m_a )
		printWord = false;

	// print word/tag #i
	if ( !(sk->m_flags&SEC_FAKE) && sk->m_tagId && printWord )
		// only encode if it is a tag
		m_sbuf->htmlEncode(m_wptrs[sk->m_a],m_wlens[sk->m_a],false );

	//if ( forProCog )
	//	m_sbuf->safePrintf("A=%"INT32" ",sk->m_a);


	/*
	  take out for now since we changed the stats class around
	if ( format == FORMAT_PROCOG && sk->m_stats.m_numUniqueSites >= 2 ) {
		// do not count our own site!
		m_sbuf->safePrintf("<i>"
				   "<font size=-1>"
				   "<a href=\"/search?"
				   // turn off summary deduping
				   "dr=0&"
				   // turn ON site clustering
				   "sc=1&"
				   "q=gbsectionhash:%"UINT64"\">"
				   "sitedups=%"INT32""
				   "</a>"
				   "</font>"
				   "</i> "
				   , sk->m_sentenceContentHash64
				   ,(int32_t)sk->m_stats.m_numUniqueSites-1);
	}
	*/

	m_sbuf->safePrintf("<i>");

	if ( format == FORMAT_PROCOG && (sk->m_flags & SEC_SENTENCE) ) {
		sec_t f = sk->m_flags;
		//if ( f & SEC_SENTENCE )
		//	m_sbuf->safePrintf("sentence " );
		if ( f & SEC_MENU_SENTENCE )
			m_sbuf->safePrintf("menusentence " );
		if ( f & SEC_MENU )
			m_sbuf->safePrintf("ismenu " );
		if ( f & SEC_MENU_HEADER )
			m_sbuf->safePrintf("menuheader " );
	}


	//if ( sk->m_sentenceContentHash64 && 
	//     sk->m_sentenceContentHash64 ) // != sk->m_contentHash64 )
	//	m_sbuf->safePrintf("sch=%"UINT64" ",
	//			   sk->m_sentenceContentHash64);

	// show dup votes if any
	//if ( sk->m_votesForDup )
	//	m_sbuf->safePrintf("dupvotes=%"INT32" ",sk->m_votesForDup);
	//if ( sk->m_votesForNotDup )
	//	m_sbuf->safePrintf("notdupvotes=%"INT32" ",
	//			   sk->m_votesForNotDup);
	
	if ( format != FORMAT_PROCOG ) {
		// print the flags
		m_sbuf->safePrintf("A=%"INT32" ",sk->m_a);
		
		// print tag hash now
		m_sbuf->safePrintf("taghash=%"UINT32" ",(int32_t)sk->m_tagHash);
		
		m_sbuf->safePrintf("turktaghash=%"UINT32" ",
				   (int32_t)sk->m_turkTagHash32);
		
		if ( sk->m_contentHash64 )
			m_sbuf->safePrintf("ch64=%"UINT64" ",sk->m_contentHash64);
		if ( sk->m_sentenceContentHash64 && 
		     sk->m_sentenceContentHash64 != sk->m_contentHash64 )
			m_sbuf->safePrintf("sch=%"UINT64" ",
					   sk->m_sentenceContentHash64);


		// show this stuff for tags that contain sentences indirectly,
		// that is what we hash in XmlDoc::hashSections()
		//if(sk->m_indirectSentHash64 && sk->m_tagId != TAG_TEXTNODE) {
		uint64_t mod = 0;
		if ( sk->m_flags & SEC_HASHXPATH ) {
		// show for all tags now because diffbot wants to see
		// markup on all tags
		//if ( sk->m_indirectSentHash64 && sk->m_tagId !=TAG_TEXTNODE){
		//if ( sk->m_stats.m_totalDocIds ) {
			mod = (uint32_t)sk->m_turkTagHash32;
			mod ^= (uint32_t)(uint64_t)m_siteHash64;
			m_sbuf->safePrintf("<a style=decoration:none; "
					   "href=/search?c=%s&"
					   "q=gbfacetstr%%3A"
					   "gbxpathsitehash%"UINT64">"
					   //"<u>"
					   "xpathsitehash=%"UINT64""
					   //"</u>"
					   "</a> "
					   //"</font> "
					   ,m_coll
					   ,mod
					   ,mod);
		}

		SectionStats *ss = &sk->m_stats;

		// also the value of the inner html hashed
		if ( sk->m_flags & SEC_HASHXPATH ) {//ss->m_totalMatches > 0) {
			uint32_t val ;
			val = (uint32_t) sk->m_indirectSentHash64 ;
			m_sbuf->safePrintf("xpathsitehashval=%"UINT32" ", val );
		}

		// some voting stats
		if ( sk->m_flags & SEC_HASHXPATH ) {//ss->m_totalMatches > 0) {
			m_sbuf->safePrintf("_s=M%"INT32"D%"INT32"n%"INT32"u%"INT32"h%"UINT32" "
					   ,(int32_t)ss->m_totalMatches
					   ,(int32_t)ss->m_totalDocIds
					   ,(int32_t)ss->m_totalEntries
					   ,(int32_t)ss->m_numUniqueVals
					   ,(uint32_t)mod
					   );
		}

		// take this out for now... MDW 7/7/2014

		// // for the gbsectionhash:xxxxx terms we index
		// if ( sk->m_sentenceContentHash64 ) {
		// 	uint32_t mod = (uint32_t)sk->m_turkTagHash32;
		// 	mod ^= (uint32_t)m_siteHash64;
		// 	m_sbuf->safePrintf(//"<font color=red>"
		// 			   "gbsectionhash32=%"UINT32" "
		// 			   //"</font> "
		// 			   ,mod);
		// }
		// if ( sk->m_contentHash64 )
		// 	m_sbuf->safePrintf(//"<font color=red>"
		// 			   "ch32=%"UINT32""
		// 			   //"</font> "
		// 			   ,
		// 			   (uint32_t)sk->m_contentHash64);
					   
		
		if ( sk->m_lastLinkContentHash32 )
			m_sbuf->safePrintf("llch=%"UINT32" ",
					   (int32_t)sk->m_lastLinkContentHash32);
		
		if ( sk->m_leftCell )
			m_sbuf->safePrintf("leftcellA=%"INT32" ",
					   (int32_t)sk->m_leftCell->m_a);
		if ( sk->m_aboveCell )
			m_sbuf->safePrintf("abovecellA=%"INT32" ",
					   (int32_t)sk->m_aboveCell->m_a);
		
		printFlags ( m_sbuf , sk , false );
	
		if ( isHardSection(sk) )
			m_sbuf->safePrintf("hardsec ");
	
		// get addr index ptr if any (could be mult)
		int32_t acount = 0;
		//int64_t sh = 0LL;
		int32_t pi = sk->m_firstPlaceNum;
		int32_t np = m_aa->m_numSorted;
		for ( ; pi >= 0 && pi < np ; pi++ ) {
			Place *p = m_aa->m_sorted[pi];
			// stop if not in section any more
			if ( p->m_a >= sk->m_b ) break;
			// get hash
			//int64_t tt = p->m_hash;
			// get max
			//if ( ! sh || tt > sh ) sh = tt;
			// count them
			acount++;
		}
		// print those out
		if ( sk->m_dateBits )
			m_sbuf->safePrintf("datebits=0x%"XINT32" ",
					   (int32_t)sk->m_dateBits);
		if ( sk->m_phoneXor ) 
			m_sbuf->safePrintf("phonexor=0x%"XINT32" ",sk->m_phoneXor);
		if ( sk->m_emailXor ) 
			m_sbuf->safePrintf("emailxor=0x%"XINT32" ",sk->m_emailXor);
		if ( sk->m_priceXor ) 
			m_sbuf->safePrintf("pricexor=0x%"XINT32" ",sk->m_priceXor);
		if ( sk->m_todXor )
			m_sbuf->safePrintf("todxor=0x%"XINT32" ",sk->m_todXor);
		if ( sk->m_dayXor )
			m_sbuf->safePrintf("dayxor=0x%"XINT32" ",sk->m_dayXor);
		if ( sk->m_addrXor )
			m_sbuf->safePrintf("addrxor=0x%"XINT32" ",sk->m_addrXor);
		if ( acount >= 2 )
			m_sbuf->safePrintf(" (%"INT32" places)",acount);
	}

	m_sbuf->safePrintf("</i>\n");

	// now print each word and subsections in this section
	int32_t a = sk->m_a;
	int32_t b = sk->m_b;
	for ( int32_t i = a ; i < b ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// . if its a and us, skip
		// . BUT if we are root then really this tag belongs to
		//   our first child, so make an exception for root!
		if ( i == a && m_tids[i] && (sk->m_parent) ) continue;

		// . get section of this word
		// . TODO: what if this was the tr tag we removed??? i guess
		//   maybe make it NULL now?
		Section *ws = m_sectionPtrs[i];
		// get top most parent that starts at word position #a and
		// is not "sk"
		for ( ; ; ws = ws->m_parent ) {
			if ( ws == sk ) break;
			if ( ! ws->m_parent ) break;
			if ( ws->m_parent->m_a != ws->m_a ) break;
			if ( ws->m_parent == sk ) break;
		}
		// if it belongs to another sections, print that section
		if ( ws != sk ) {
			// print out this subsection
			printSectionDiv ( ws , format ); // forProCog );
			// advance to end of that then
			i = ws->m_b - 1;
			// and try next word
			continue;
		}

		// ignore if in style section, etc. just print it out
		if ( sk->m_flags & NOINDEXFLAGS ) {
			m_sbuf->htmlEncode(m_wptrs[i],m_wlens[i],false );
			continue;
		}

		// boldify alnum words
		if ( m_wids[i] ) {
			if ( m_wposVec[i] == m_hiPos )
				m_sbuf->safePrintf("<a name=hipos></a>");
			m_sbuf->safePrintf("<nobr><b>");
			if ( i <  MAXFRAGWORDS && m_fragVec[i] == 0 ) 
				m_sbuf->safePrintf("<strike>");
		}
		if ( m_wids[i] && m_wposVec[i] == m_hiPos )
			m_sbuf->safePrintf("<blink style=\""
					   "background-color:yellow;"
					   "color:black;\">");
		// print that word
		m_sbuf->htmlEncode(m_wptrs[i],m_wlens[i],false );
		if ( m_wids[i] && m_wposVec[i] == m_hiPos )
			m_sbuf->safePrintf("</blink>");
		// boldify alnum words
		if ( m_wids[i] ) {
			if ( i < MAXFRAGWORDS && m_fragVec[i] == 0 ) 
				m_sbuf->safePrintf("</strike>");
			m_sbuf->safePrintf("</b>");
		}
		// and print out their pos/div/spam sub
		if ( m_wids[i] ) {
			m_sbuf->safePrintf("<sub "
					   "style=\"background-color:white;"
					   "font-size:10px;"
					   "border:black 1px solid;"
					   "color:black;\">");
			m_sbuf->safePrintf("%"INT32"",m_wposVec[i]);
			if ( m_densityVec[i] != MAXDENSITYRANK )
				m_sbuf->safePrintf("/<font color=purple><b>%"INT32""
						   "</b></font>"
						   ,
						   (int32_t)m_densityVec[i]);
			/*
			if ( m_diversityVec[i] != MAXDIVERSITYRANK )
				m_sbuf->safePrintf("/<font color=green><b>%"INT32""
						   "</b></font>"
						   ,
						   (int32_t)m_diversityVec[i]);
			*/
			if ( m_wordSpamVec[i] != MAXWORDSPAMRANK )
				m_sbuf->safePrintf("/<font color=red><b>%"INT32""
						   "</b></font>"
						   ,
						   (int32_t)m_wordSpamVec[i]);
			m_sbuf->safePrintf("</sub></nobr>");
		}
	}
	m_sbuf->safePrintf("</div>\n");

	return true;
}

bool Sections::verifySections ( ) {

	// make sure we map each word to a section that contains it at least
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		Section *si = m_sectionPtrs[i];
		if ( si->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( si->m_b <= i ) { char *xx=NULL;*xx=0; }
		// must have checksum
		if ( m_wids[i] && si->m_contentHash64==0){char *xx=NULL;*xx=0;}
		// must have this set if 0
		if ( ! si->m_contentHash64 && !(si->m_flags & SEC_NOTEXT)) {
			char *xx=NULL;*xx=0;}
		if (   si->m_contentHash64 &&  (si->m_flags & SEC_NOTEXT)) {
			char *xx=NULL;*xx=0;}
	}

	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) 
		// breathe
		QUICKPOLL ( m_niceness );

	// sanity check
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *sn = m_rootSection ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		//Section *sn = &m_sections[i];
		// get parent
		Section *sp = sn->m_parent;
	subloop3:
		// skip if no parent
		if ( ! sp ) continue;
		// make sure parent fully contains
		if ( sp->m_a > sn->m_a ) { char *xx=NULL;*xx=0; }
		if ( sp->m_b < sn->m_b ) { char *xx=NULL;*xx=0; }
		// breathe
		QUICKPOLL ( m_niceness );
		// and make sure every grandparent fully contains us too!
		sp = sp->m_parent;
		goto subloop3;
	}

	// sanity check
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		Section *sn = &m_sections[i];
		if ( sn->m_a >= sn->m_b ) { char *xx=NULL;*xx=0; }
	}

	// sanity check, make sure each section is contained by the
	// smallest section containing it
	//for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
	for ( Section *si = m_rootSection ; si ; si = si->m_next ) {
		//Section *si = &m_sections[i];
		//if ( ! si ) continue;
		//for ( int32_t j = 0 ; j < m_numSections ; j++ ) {
		for ( Section *sj = m_rootSection ; sj ; sj = sj->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if us
			if ( sj == si ) continue;
			// skip column sections because they are artificial
			// and only truly contain some of the sections that
			// their [a,b) interval says they contain.
			if ( sj->m_tagId == TAG_TC ) continue;
			// or if an implied section of td tags in a tc
			if ( sj->m_baseHash == BH_IMPLIED &&
			     sj->m_parent &&
			     sj->m_parent->m_tagId == TAG_TC ) 
				continue;
			// get him
			//Section *sj = &m_sections[j];
			// skip if sj does not contain first word in si
			if ( sj->m_a >  si->m_a ) continue;
			if ( sj->m_b <= si->m_a ) continue;
			// ok, make sure in our parent path
			Section *ps = si;
			for ( ; ps ; ps = ps->m_parent ) 
				if ( ps == sj ) break;
			// ok if we found it
			if ( ps ) continue;
			// sometimes if sections are equal then the other
			// is the parent
			ps = sj;
			for ( ; ps ; ps = ps->m_parent ) 
				if ( ps == si ) break;
			// must have had us
			if ( ps ) continue;
			char *xx=NULL;*xx=0;
		}
	}
	
	// make sure we map each word to a section that contains it at least
	for ( int32_t i = 0 ; i < m_nw ; i++ ) {
		Section *si = m_sectionPtrs[i];
		if ( si->m_a >  i ) { char *xx=NULL;*xx=0; }
		if ( si->m_b <= i ) { char *xx=NULL;*xx=0; }
	}

	return true;
}

bool Sections::isTagDelimeter ( class Section *si , nodeid_t tagId ) {
	// store
	Section *saved = si;
	// . people embed tags in other tags, so scan
	// . fix "<strong><em><br></em><strong><span><br></span> for
	//   guysndollsllc.com homepage
	for ( ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if si not contained in saved
		if ( ! saved->contains ( si ) ) return false;
		// stop if got it
		if ( tagId != TAG_BR &&  si->m_tagId == tagId ) break;
		// tag br can be a pr or p tag (any vert whitespace really)
		if ( tagId == TAG_BR ) {
			if ( si->m_tagId == TAG_BR ) break;
			if ( si->m_tagId == TAG_P  ) break;
			// treat <hN> and </hN> as single breaking tags too
			nodeid_t ft = si->m_tagId & BACKBITCOMP;
			if ( ft >= TAG_H1 && ft <= TAG_H5 ) break;
			// fix tennisoncampus.com which has <p>...<table> as
			// a double space
			if ( si->m_tagId == TAG_TABLE  ) break;
		}
		// . skip if has no text of its own
		// . like if looking for header tags and we got:
		//   <tr><td></td><td><h1>stuff here</h1></td></tr>
		//   we do not want the <td></td> to stop us
		//   bluefin-cambridge.com
		if ( si->m_firstWordPos < 0 ) continue;
		// stop if hit alnum before tagid
		//if ( si->m_alnumPosA != saved->m_alnumPosA ) 
		//	return false;
		// stop if hit text before hitting tagid
		if ( si->m_lastWordPos != saved->m_lastWordPos )
			return false;
	}
	// done?
	if ( ! si ) return false; 
	//if ( si->m_tagId != tagId ) return false;
	if ( tagId != TAG_BR ) return true;
	// need double brs (or p tags)
	int32_t a = si->m_a + 1;
	//if ( a + 2 >= m_nw ) return false;
	//if ( m_tids[a+1] == TAG_BR ) return true;
	//if ( m_wids[a+1]           ) return false;
	//if ( m_tids[a+2] == TAG_BR ) return true;
	// guynsdollsllc.com homepage has some crazy br tags
	// in their own em strong tags, etc.
	int32_t kmax = a+10;
	if ( kmax > m_nw ) kmax = m_nw;
	for ( int32_t k = a ; k < kmax ; k++ ) {
		if ( m_wids[k]           ) return false;
		if ( m_tids[k] == TAG_BR ) return true;
		if ( m_tids[k] == TAG_P  ) return true;
		if ( m_tids[k] == TAG_TABLE  ) return true;
		// treat <hN> and </hN> as single breaking tags too
		nodeid_t ft = m_tids[k] & BACKBITCOMP;
		if ( ft >= TAG_H1 && ft <= TAG_H5 ) return true;
	}
	return false;
};

// . this maps a section ptr to if it contains registration info or not
// . we use this so we can ignore addresses in "registration" sections
// . now we also include old years to ignore addresses in sentences like
//   "since its first opening in 2003 at gerschwin theater" talking about
//   past events
// . returns false with g_errno set on error
bool Sections::setRegistrationBits ( ) {

	//if ( m_regTableValid ) return &m_regTable;
	if ( m_setRegBits ) return true;

	//if ( ! m_regTable.set ( 4 , 0 , 128,NULL,0,false,m_niceness ) ) 
	//	return NULL;

	// make a keyword table
	static HashTableX s_kt;
	static bool s_ktinit = false;
	static char s_ktbuf[1200];
	static int64_t h_registration;
	static int64_t h_registrar;
	static int64_t h_registrations;
	static int64_t h_reservation;
	static int64_t h_reservations;
	static int64_t h_register;
	static int64_t h_sign;
	static int64_t h_up;
	static int64_t h_signup;
	static int64_t h_tickets;
	static int64_t h_purchase;
	static int64_t h_request;
	static int64_t h_requesting;
	static int64_t h_get;
	static int64_t h_enroll;
	static int64_t h_buy;
	static int64_t h_presale ;
	static int64_t h_pre ;
	static int64_t h_sale ;
	static int64_t h_on ;
	static int64_t h_to ;
	static int64_t h_sales ;
	static int64_t h_deliver;
	static int64_t h_picks;
	static int64_t h_box; // box office for newmexicojazzfestival.org
	static int64_t h_office;
	static int64_t h_ticket;//ticket window for newmexicojazzfestival.org
	static int64_t h_online;
	static int64_t h_window;
	static int64_t h_patron;
	static int64_t h_service;
	static int64_t h_services;
	static int64_t h_phone;
	static int64_t h_hours;
	static int64_t h_end ;
	static int64_t h_begin ;
	static int64_t h_start ;
	static int64_t h_stop ;
	static int64_t h_parking;
	static int64_t h_performance;
	static int64_t h_dates;
	static int64_t h_take;
	static int64_t h_takes;
	static int64_t h_place;
	static int64_t h_doors;
	static int64_t h_open;
	static int64_t h_event;
	static int64_t h_details;
	if ( ! s_ktinit ) {
		s_ktinit = true;
		s_kt.set(8,0,128,s_ktbuf,1200,false,m_niceness,"evkeywrds");
		// assign these
		h_registrar    = hash64n("registrar");
		h_registration = hash64n("registration");
		h_registrations= hash64n("registrations");
		h_reservation  = hash64n("reservation");
		h_reservations = hash64n("reservations");
		h_register     = hash64n("register");
		h_sign         = hash64n("sign");
		h_up           = hash64n("up");
		h_signup       = hash64n("signup");
		h_tickets      = hash64n("tickets");
		h_purchase     = hash64n("purchase");
		h_requesting   = hash64n("requesting");
		h_request      = hash64n("request");
		h_get          = hash64n("get");
		h_enroll       = hash64n("enroll");
		h_buy          = hash64n("buy");
		h_presale      = hash64n("presale");
		h_pre          = hash64n("pre");
		h_sale         = hash64n("sale");
		h_on           = hash64n("on");
		h_to           = hash64n("to");
		h_sales        = hash64n("sales");
		h_deliver      = hash64n("deliver");
		h_end          = hash64n("end");
		h_begin        = hash64n("begin");
		h_start        = hash64n("start");
		h_stop         = hash64n("stop");
		h_parking      = hash64n("parking");
		h_performance  = hash64n("performance");
		h_dates        = hash64n("dates");
		h_take         = hash64n("take");
		h_takes        = hash64n("takes");
		h_place        = hash64n("place");
		h_doors        = hash64n("doors");
		h_open         = hash64n("open");
		h_event        = hash64n("event");
		h_details      = hash64n("details");
		h_box          = hash64n("box");
		h_office       = hash64n("office");
		h_ticket       = hash64n("ticket");
		h_online       = hash64n("online");
		h_window       = hash64n("window");
		h_patron       = hash64n("patron");
		h_service      = hash64n("service");
		h_services     = hash64n("services");
		h_phone        = hash64n("phone");
		h_hours        = hash64n("hours");
		h_picks        = hash64n("picks");
		// populate it
		s_kt.addKey(&h_registration);
		s_kt.addKey(&h_registrar);
		s_kt.addKey(&h_registrations);
		s_kt.addKey(&h_reservation);
		s_kt.addKey(&h_reservations);
		s_kt.addKey(&h_register);
		s_kt.addKey(&h_sign);
		s_kt.addKey(&h_signup);
		s_kt.addKey(&h_buy);
		s_kt.addKey(&h_purchase);
		s_kt.addKey(&h_requesting);
		s_kt.addKey(&h_request);
		s_kt.addKey(&h_get);
		s_kt.addKey(&h_tickets);
		s_kt.addKey(&h_presale);
		s_kt.addKey(&h_on);
		s_kt.addKey(&h_pre);
		s_kt.addKey(&h_sales);
		s_kt.addKey(&h_deliver);
		s_kt.addKey(&h_picks);
		s_kt.addKey(&h_box);
		s_kt.addKey(&h_ticket);
		s_kt.addKey(&h_patron);
		s_kt.addKey(&h_phone);
		// support parking stuff now
		s_kt.addKey(&h_parking);
	}

	// this maps a section ptr to the places it contains
	//HashTableX *at = m_addresses->getPlaceTable ();
	// this maps a section ptr to the dates it contains
	//HashTableX *tt = m_dates->getTODTable ();

	// int16_tcut
	//int64_t *wids = m_wids;

	m_bits->setInLinkBits ( this ); // m_sections );

	// scan all words
	for ( int32_t j = 0 ; j < m_nw ; j++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip punct words
		if ( ! m_wids[j] ) continue;
		// if it is an old date with a year fixes "Since its openining
		// at Gerschwin THeater in Oct 30, 2003 Wicked has sold out" 
		// for events.kgoradio.com/san-francisco-ca/events/show/
		// 113475645-wicked
		// Optionally we could check for "Since * opening at *"
		// This seems to hurt to many other events, so let's
		// try the option.
		/*
		if ( (m_bits[j] & D_IS_IN_DATE) ) {
			// must be a year
			int32_t num = m_words->getAsLong(j);
			if ( num <  1700 ) continue;
			if ( num >= 2009 ) continue;
			// get section
			Section *sk = m_sections->m_sectionPtrs[j];
			// assume a year
			if ( ! m_regTable.addKey ( &sk ) ) return false;
			// skip it
			continue;
		}
		*/

		// int16_tcut
		int64_t wid = m_wids[j];

		// "ictickets office" for http://www.ictickets.com/Event/
		// Default.aspx?id=1036
		// CAUTION: this maps tickets,ticketing -> ticket
		if ( m_wlens[j] >= 7 ) {
			char *p = m_wptrs[j];
			char *pend = p + m_wlens[j];
			for ( ; p < pend ; p++ ) {
				QUICKPOLL(m_niceness);
				if ( to_lower_a(p[0]) == 't' &&
				     to_lower_a(p[1]) == 'i' &&
				     to_lower_a(p[2]) == 'c' &&
				     to_lower_a(p[3]) == 'k' &&
				     to_lower_a(p[4]) == 'e' &&
				     to_lower_a(p[5]) == 't' )
					wid = h_ticket;
			}
		}

		// skip if not in table
		if( ! s_kt.isInTable(&wid)) continue;

		// get next word
		int32_t max = j + 10; if ( max > m_nw ) max = m_nw;
		// assume right after us
		int32_t next = j + 2;
		// set nextWid to its id
		int64_t nextWid = 0LL;
		for ( ; next < max ; next++ ) {
			if ( ! m_wids[next] ) continue;
			// grab it
			nextWid = m_wids[next];
			// for loop below
			next++;
			// stop
			break;
		}
		// and the next after that
		int64_t nextWid2 = 0LL;
		for ( ; next < max ; next++ ) {
			if ( ! m_wids[next] ) continue;
			// grab it
			nextWid2 = m_wids[next];
			// stop
			break;
		}
		
		// . ignore if in a link
		// . fixes southgatehouse.com "buy tickets"
		// . i think that tingley colesium url has that too!
		if ( (m_bits->m_bits[j]) & D_IN_LINK ) continue;

		// synonyms
		if ( nextWid  == h_tickets ) nextWid  = h_ticket;
		if ( nextWid2 == h_tickets ) nextWid2 = h_ticket;

		// reset flag
		char gotIt = 0;
		// is it register?
		if ( wid == h_register                     ) gotIt = 1;
		if ( wid == h_registrar                    ) gotIt = 1;
		if ( wid == h_registration                 ) gotIt = 1;
		if ( wid == h_registrations                ) gotIt = 1;
		if ( wid == h_reservation                  ) gotIt = 1;
		if ( wid == h_reservations                 ) gotIt = 1;
		if ( wid == h_phone && nextWid == h_hours  ) gotIt = 1;
		if ( wid == h_sign && nextWid == h_up      ) gotIt = 1;
		if ( wid == h_signup                       ) gotIt = 1;
		if ( wid == h_buy && nextWid == h_ticket  ) gotIt = 1;
		//if ( wid == h_buy && nextWid == h_online  ) gotIt = 1;
		if ( wid == h_purchase&&nextWid==h_ticket ) gotIt = 1;
		if ( wid == h_get && nextWid==h_ticket    ) gotIt = 1;
		// for that jimmy kimmel live url "requesting tickets online"
		if ( wid == h_request && nextWid==h_ticket) gotIt = 1;
		if ( wid == h_requesting && nextWid==h_ticket) gotIt = 1;
		// "after requesting your tickets"
		if ( wid == h_requesting && nextWid2==h_ticket) gotIt = 1;
		if ( wid == h_ticket && nextWid==h_request) gotIt = 1;
		// "purchase single tickets" or "purchase * tickets"
		// to fix www.santarosasymphony.com/tickets/tickets_home.asp
		if ( wid == h_purchase && nextWid2==h_ticket  ) gotIt = 1;
		if ( wid == h_patron   && nextWid ==h_services) gotIt = 1;
		if ( wid == h_patron   && nextWid ==h_service ) gotIt = 1;
		if ( wid == h_phone    && nextWid ==h_hours   ) gotIt = 1;
		// "give them tickets to" for santafe playhouse url
		// to cancel out "Max's or Dish n' Spoon" as a place
		// (for Address.cpp's call to isTicketDate())
		if ( wid == h_ticket && nextWid == h_to    ) gotIt = 1;
		if ( wid == h_presale                      ) gotIt = 1;
		if ( wid == h_on    && nextWid == h_sale   ) gotIt = 1;
		if ( wid == h_pre   && nextWid == h_sale   ) gotIt = 1;
		if ( wid == h_sales && nextWid == h_end    ) gotIt = 1;
		if ( wid == h_sales && nextWid == h_stop   ) gotIt = 1;
		if ( wid == h_sales && nextWid == h_begin  ) gotIt = 1;
		if ( wid == h_sales && nextWid == h_start  ) gotIt = 1;
		// deliver saturdays and sundays - frenchtulips.com
		if ( wid == h_deliver ) gotIt = 1;
		// The Taos Ski Shuttle picks up guests daily at 9:15am
		if ( wid == h_picks && nextWid == h_up ) gotIt = 1;
		if ( wid == h_box   && nextWid == h_office ) gotIt = 1;
		if ( wid == h_ticket&& nextWid == h_window ) gotIt = 1;
		if ( wid == h_ticket&& nextWid == h_office ) gotIt = 1;
		// try to fix villr.com whose event is at a parking lot!
		if ( wid == h_parking                      ) gotIt = 2;
		// ticket: or tickets: fix for mesaartscenter.com
		//if (wid==h_ticket && m_wptrs[j][m_wlens[j]]==':') gotIt = 1;
		if ( ! gotIt ) continue;
		// skip if in title
		Section *sk = m_sectionPtrs[j];

		// telescope up to sentence
		for ( ; sk ; sk = sk->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if sentence
			if ( sk->m_flags & SEC_SENTENCE ) break;
		}

		// sk is NULL if in "bad" section like an <option> tag
		// in which case we skip it i guess
		if ( ! sk ) continue;

		// if in a menu like "register" for mrmovietimes.com continue
		if ( sk->m_flags & SEC_MENU ) continue;

		// save it
		//Section *orig = sk;

		int32_t sa = sk->m_firstWordPos;
		//int32_t sb = sk->m_lastWordPos;
		//bool expanded = false;
		Section *lastsk = NULL;

		// are we in a header section?
		//bool heading = ( sk->m_flags & SEC_HEADING );

		// ok, now telescope up this section
		// get first word
		//int32_t fw = sk->m_a - 1;
		// find alnum right before that
		//for ( ; fw >= 0 ; fw-- )
		//	if ( wids[fw] ) break;
		// get first word below us
		//int32_t fw2 = sk->m_b;
		// scan for it
		//for ( ; fw2 < m_nw ; fw2++ )
		//	if ( wids[fw2] ) break;
		// telescope until we contain a good place
		for ( ; sk ; sk = sk->m_parent ) {
			// stop if includes words above our sentence
			//if ( sk->m_a <= fw ) break;
			if ( sk->m_firstWordPos < sa ) break;

			// save it
			lastsk = sk;

			// did we expand?
			//if ( sk->m_lastWordPos != sb ) expanded = true;

			// stop if we hit a title tag
			//if ( sk->m_tagId == TAG_TITLE ) break;

			// . stop if this section is in a list of other
			// . we should hash each sections tag hash along with
			//   their parent section ptr for this
			//if ( sk->m_container ) break;

			// add it
			//if ( ! m_regTable.addKey ( &sk ) ) return false;
			// flag it as well
			if ( gotIt == 1 ) sk->m_flags |= SEC_HAS_REGISTRATION;
			else              sk->m_flags |= SEC_HAS_PARKING;

			// stop if includes new words below
			//if ( sk->m_b > fw2 ) break;
			//if ( sk->m_lastWordPos  > sb ) break;

			// or td hit. fixes events.mapchannels.com
			if ( sk->m_tagId == TAG_TD    ) break;
			if ( sk->m_tagId == TAG_TR    ) break;
			if ( sk->m_tagId == TAG_TABLE ) break;
			if ( sk->m_tagId == TAG_FORM  ) break;

			// . we can extend maxsb if we are a heading only
			// . this fixes signmeup.com which has "registration"
			//   in the first non-header sentence of a section and
			//   then in the second sentence has the critical date
			//   "Thanksgiving" which we need to telescope to to
			//   get our events.
			// . now that we only telescope "sk" if the key word
			//   was in a "heading" we fix that.
			//if ( sk->m_b > sb && ! heading ) break;

			/*
			Section *nb = sk->m_nextBrother;

			// this did fix mapchannels.com, but now we only
			// allow SEC_HEADING_CONAINER to be set on a list
			// of brothers that have <tr> or <p> tags, and not
			// the <td> tags as used in mapchannels.com.
			// get next brother

			// . skip implied sections as those are often wrong
			// . this fixed events.mapchannels.com from allowing
			//   the "Buy Tickets..." heading to telescope up
			//   to the whole row in the registration table, 
			//   because its brother got encapsulated in an
			//   implied section.
			//for ( ; nb ; nb = nb->m_next ) 
			//	// stop if not implied
			//	if ( nb->m_baseHash != BH_IMPLIED ) break;

			// do not telescope more if we have a next brother
			if ( nb &&
			     // can't use tagHash because implied sections
			     // changed it!
			     //nb->m_tagHash == sk->m_tagHash &&
			     nb->m_tagId == sk->m_tagId &&
			     // . ignore br tag sections
			     // . often the header is "registration" then there
			     //   is a br tag and the registration info like in
			     //   signmeup.com
			     ( sk->m_baseHash != BH_BR &&
			       sk->m_baseHash != BH_BRBR ) )
				break;
			*/

			// stop if title tag hit
			int32_t a = sk->m_a;
			if ( m_tids[a] == TAG_TITLE ) break;
			// . stop if we hit a place
			// . this was causing signmeup.com which had the
			//   address then the times of the registration to
			//   mess up
			//if ( at->isInTable(&sk) ) break;
			// or a time!
			// might have registration at a particular time
			// on the same day as other events all at the same
			// place like for
			// http://www.breadnm.org/custom.html
			// crap this breaks denver.org which just happens
			// to have like everything in a table...
			//if ( tt->isInTable(&sk) ) break;

			// the whole thing might have been registration!
			// so don't let sk become NULL. this went NULL for
			// the txt doc:
			// http://bywaysresourcecenter.org/events/uploads/AB
			// RC_09ConfReg_bro_FNL.txt which basically had no
			// sections
			if ( ! sk->m_parent ) break;
		}

		// if the biggest section with the registration sentence
		// at the top of it contains multiple sentences, then
		// that should be good enough...
		sk = lastsk;
		// sanity check
		if ( ! sk ) { char *xx=NULL;*xx=0; }


		// int16_tcut
		Section **sp = m_sectionPtrs;		

		//
		// now unset SEC_HAS_REGISTRATION for sections that seem
		// to contain sentences that are specifically about the
		// event or actual performance
		//

		// now completely ignore "sb" and starting at word #i
		// scan until we hit a breaking tag (excluding br) or
		// we hit a phrase that identifies the end of the reservation
		// sections. like "Performance Dates" for santafeplayhouse.org.
		// and for adobetheater.org we should stop at the </tr> tag
		// so we can pick up that "Friday from 9 AM to 4 PM" that
		// we were missing because "sb" stopped int16_t.

		for ( int32_t i = j ; i < sk->m_b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if certain tag id
			if ( m_tids[i] ) {
				/*
				// get tag id
				nodeid_t tid = m_tids[i] & BACKBITCOMP;
				// mapchannels has "buy tickets" in a td cell
				if ( tid == TAG_TD    ) clearIt = true;
				if ( tid == TAG_TR    ) clearIt = true;
				if ( tid == TAG_TABLE ) clearIt = true;
				if ( tid == TAG_FORM  ) clearIt = true;
				*/
				continue;
			}
			// skip if punct
			if ( ! m_wids[i] ) continue;
			// get section
			Section *si = sp[i];
			// certain words will stop us
			if ( i+2<m_nw ) {
				// fix for santafeplayhouse.org
				if ( m_wids[i  ] == h_performance &&
				     m_wids[i+2] == h_dates )
					goto clear;
				// fix signmeup.com
				if ( m_wids[i  ] == h_take &&
				     m_wids[i+2] == h_place )
					goto clear;
				if ( m_wids[i  ] == h_takes &&
				     m_wids[i+2] == h_place )
					goto clear;
				// fix reverbnation.com
				if ( m_wids[i  ] == h_doors &&
				     m_wids[i+2] == h_open )
					goto clear;
				// fix socialmediabeach.com
				if ( m_wids[i  ] == h_event &&
				     m_wids[i+2] == h_details )
					goto clear;
			}
			// mark it
			if ( gotIt == 1 ) si->m_flags |= SEC_HAS_REGISTRATION;
			else              si->m_flags |= SEC_HAS_PARKING;
			continue;
		clear:
			for ( ; si ; si = si->m_parent ) {
				// breathe
				QUICKPOLL(m_niceness);
				// clear flag
				si->m_flags &= ~SEC_HAS_REGISTRATION;
				si->m_flags &= ~SEC_HAS_PARKING;
			}
			break;
		}

		/*

		  // this messes up too many other pages. the better approach
		  // to fix mesaartscenter.com is to consider those events
		  // outlinked titles because of the "DETAILS" link. then
		  // we can go to the correct page and get the correct tod
		  // ranges.

		// get largest section containing us. just our sentence.
		// then get next brother of that. that whole brother
		// should be under our influence. assuming we do not have
		// any dates in our section...
		Section *gs = orig;
		// require it to be a heading i guess
		if ( ! ( gs->m_flags & SEC_HEADING ) ) gs = NULL;
		// blow it up into the biggest container of just "orig"
		for ( ; gs ; gs = gs->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ! gs->m_parent ) break;
			if ( gs->m_parent->m_alnumPosA != gs->m_alnumPosA )
				break;
			if ( gs->m_parent->m_alnumPosB != gs->m_alnumPosB )
				break;
		}
		// this is non-NULL if registration indicator was in a heading
		if ( gs ) {
			// get the brother section after that heading
			Section *bro = gs->m_nextBrother;
			// mark all sections in "bro" as registration
			for ( Section *ss = bro ; ss ; ss = ss->m_next ) {
				QUICKPOLL(m_niceness);
				if ( ss->m_a >= bro->m_b ) break;
				ss->m_flags |= SEC_HAS_REGISTRATION;
			}
		}
		*/
	}

	// . try to fix adobetheater.org which has one sentence which
	//   we see as two:
	//   "Advance reservations are accepted on Monday - Thursday from "
	//    9 AM to 5 PM" and "Friday from 9 AM to 4 PM."
	// . assume hours are registration dates is better to be wrong
	//   and call them registration dates than the other way around because
	//   we do not want to give wrong dates

	// . now scan the sections
	// . look at the lists of brothers
	// . if a brother is in the table , then assume all brothers after
	//   him are in the table as well
	/*
	for ( Section *si = m_sections->m_rootSection ; si ; si = si->m_next ){
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not in table
		//if ( ! m_regTable.isInTable ( &si ) ) continue;
		if ( ! (si->m_flags & SEC_HAS_REGISTRATION) ) continue;
		// skip if already scanned
		if ( si->m_used == 88 ) continue;
		// scan brothers
		for ( Section *nb = si ; nb ; nb = nb->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// mark all his brothers as reg too!
			nb->m_flags |= SEC_HAS_REGISTRATION;
			// mark it
			nb->m_used = 88;
		}
	}
	*/

	m_setRegBits = true;
	return true;
	//m_regTableValid = true;
	//return &m_regTable;
}


sentflags_t getMixedCaseFlags ( Words *words , 
				wbit_t *bits ,
				int32_t senta , 
				int32_t sentb , 
				int32_t niceness ) {
	
	int64_t *wids = words->getWordIds();
	int32_t *wlens = words->getWordLens();
	char **wptrs = words->getWordPtrs();
	int32_t lowerCount = 0;
	int32_t upperCount = 0;
	bool firstWord = true;
	bool inParens = false;
	for ( int32_t i = senta ; i < sentb ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip if not alnum
		if ( ! wids[i] ) {
			// skip tags right away
			if ( wptrs[i][0]=='<' ) continue;
			// check for end first in case of ") ("
			if ( words->hasChar(i,')') ) inParens = false;
			// check if in parens
			if ( words->hasChar(i,'(') ) inParens = true;
			continue;
		}
		// skip if in parens
		if ( inParens ) continue;
		// are we upper case?
		bool upper = is_upper_utf8(wptrs[i]) ;
		// are we a stop word?
		bool isStopWord = words->isStopWord(i);
		// . if first word is stop word and lower case, forget it
		// . fix "by Ron Hutchinson" title for adobetheater.org
		if ( isStopWord && firstWord && ! upper )
			// make sure both flags are returned i guess
			return (SENT_MIXED_CASE | SENT_MIXED_CASE_STRICT);

		// allow if hyphen preceedes like for
		// abqfolkfest.org's "Kay-lee"
		if ( i>0 && wptrs[i][-1]=='-' ) upper = true;
		// if we got mixed case, note that!
		if ( wids[i] &&
		     ! is_digit(wptrs[i][0]) &&
		     ! upper &&
		     (! isStopWord || firstWord ) &&
		     // . November 4<sup>th</sup> for facebook.com
		     // . added "firstword" for "on AmericanTowns.com"
		     //   title prevention for americantowns.com
		     (wlens[i] >= 3 || firstWord) )
			lowerCount++;

		// turn off
		firstWord = false;
		// . don't count words like "Sunday" that are dates!
		// . fixes "6:30 am. Sat. and Sun.only" for unm.edu
		//   and "3:30 pm. - 4 pm. Sat. and Sun., sandwiches"
		// . fixes events.kgoradio.com's
		//   "San Francisco Symphony Chorus sings Bach's 
		//    Christmas Oratorio"
		// . fixes "7:00-8:00pm, Tango Fundamentals lesson" for
		//   abqtango.com
		// . fixes "Song w/ Joanne DelCarpine (located" for
		//   texasdrums.drums.org
		// . "Loren Kahn Puppet and Object Theater presents 
		//    Billy Goat Ball" for trumba.com
		if ( bits[i] & D_IS_IN_DATE ) upper = false;
		// . was it upper case?
		if ( upper ) upperCount++;
	}

	sentflags_t sflags = 0;

	if ( lowerCount > 0 ) sflags |= SENT_MIXED_CASE_STRICT;
	
	if ( lowerCount == 1 && upperCount >= 2 ) lowerCount = 0;


	// . fix "7-10:30pm Contra dance"
	// . treat a numeric date like an upper case word
	if ( (bits[senta] & D_IS_IN_DATE) && 
	     // treat a single lower case word as error
	     lowerCount == 1 && 
	     // prevent "7:30-8:30 dance" for ceder.net i guess
	     upperCount >= 1)
		lowerCount = 0;

	if ( lowerCount > 0 ) sflags |= SENT_MIXED_CASE;

	return sflags;
}

/*
uint32_t getSectionContentTagHash3 ( Section *sn ) {
	// sanity check
	if ( sn->m_firstWordPos < 0 ) { char *xx=NULL;*xx=0; }
	// hash the tagid of this section together with its last
	// 3 parents for a total of 4
	uint32_t th = sn->m_tagId;
	// telescope up through the next 3 parents
	int32_t pcount = 0;
	for ( Section *ps = sn->m_parent; ps ; ps = ps->m_parent ) {
		// limit to 3
		if ( ++pcount >= 4 ) break;
		// incorporate it
		th = hash32h ( (uint32_t)ps->m_tagId , th );
	}
	// accumulate the tag hash
	//th = hash32 ( th , sn->m_tagHash );
	// . hash that like in XmlDoc::getSectionVotingTable()
	// . combine the tag hash with the content hash #2
	return th ^ sn->m_contentHash;
}
*/

// identify crap like "Good For Kids: Yes" for yelp.com etc. so we don't
// use that crap as titles.
bool Sections::setFormTableBits ( ) {

	if ( ! m_alnumPosValid ) { char *xx=NULL;*xx=0; }

	sec_t sdf = SEC_HAS_DOM|SEC_HAS_MONTH|SEC_HAS_DOW|SEC_HAS_TOD;
	// scan all sentences
	for ( Section *si = m_firstSent ; si ; si = si->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// must be sentence
		//if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// int16_tcut
		sentflags_t sf = si->m_sentFlags;
		// . fortable field must not end in period
		// . i.e. "Good For Kids:"
		if ( sf & SENT_PERIOD_ENDS ) continue;
		// get next sent
		Section *next = si->m_nextSent;
		// this stops things..
		if ( ! next ) continue;
		// must not end in period either
		if ( next->m_sentFlags & SENT_PERIOD_ENDS ) continue;
		Section *ps;
		// must be the only sentences in a section
		ps = si->m_parent;
		for ( ; ps ; ps = ps->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ps->contains ( next ) ) break;
		}
		// how does this happen?
		if ( ! ps ) continue;
		// must not contain any other sentences, just these two
		if ( ps->m_alnumPosA != si  ->m_alnumPosA )
			continue;
		if ( ps->m_alnumPosB != next->m_alnumPosB )
			continue;
		// get first solid parent tag for "si"
		Section *bs = si->m_parent;
		for ( ; bs ; bs = bs->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( bs->m_tagId == TAG_B ) continue;
			if ( bs->m_tagId == TAG_STRONG ) continue;
			if ( bs->m_tagId == TAG_FONT ) continue;
			if ( bs->m_tagId == TAG_I ) continue;
			break;
		}
		// if none, bail
		if ( ! bs ) continue;
		// get the containing tag then
		nodeid_t tid = bs->m_tagId;
		// must be some kind of field/value indicator
		bool separated = false;
		if ( tid == TAG_DT ) separated = true;
		if ( tid == TAG_TD ) separated = true;
		// if tr or dt tag or whatever contains "next" that is not
		// good, we need next and si to be in their own dt or td
		// section.
		if ( bs->contains ( next ) ) separated = false;
		// fix "Venue Type: Auditorium" for zvents.com kimo theater
		if ( sf & SENT_COLON_ENDS ) separated = true;
		// must be separated by a tag or colon to be field/value pair
		if ( ! separated ) continue;
		// if either one has dates, let is slide.
		// fix "zumba</td><td>10/26/2011" for www.ci.tualatin.or.us
		if ( si  ->m_flags & sdf ) continue;
		if ( next->m_flags & sdf ) continue;
		// label them
		si  ->m_sentFlags |= SENT_FORMTABLE_FIELD;
		next->m_sentFlags |= SENT_FORMTABLE_VALUE;
	}
	return true;
}

// just loop over Addresses::m_sortedPlaces
void Sections::setAddrXors ( Addresses *aa ) {
	// sanity check
	if ( ! aa->m_sortedValid ) { char *xx=NULL;*xx=0; }
	// loop over the places, sorted by Place::m_a
	int32_t np = aa->m_numSorted;
	for ( int32_t i = 0 ; i < np ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get place #i
		Place *p = aa->m_sorted[i];
		// get the address?
		Address *ad = p->m_address;
		// assume none
		int32_t h = 0;
		// or alias
		if ( ! ad ) ad = p->m_alias;
		// or just use place hash i guess!
		if ( ! ad ) 
			h = (int32_t)(PTRTYPE)p;
		else if ( ad->m_flags3 & AF2_LATLON )
			h = (int32_t)(PTRTYPE)p;
		// otherwise hash up address street etc.
		else {
			h  =(int32_t)ad->m_street->m_hash;
			h ^=(int32_t)ad->m_street->m_streetNumHash;
			//h ^= ad->m_adm1->m_cid; // country id
			//h ^= (int32_t)ad->m_adm1Bits;
			//h ^= (int32_t)ad->m_cityHash;
			h ^= (int32_t)ad->m_cityId32;
			// sanity check
			//if ( ! ad->m_adm1Bits ||
			//     ! ad->m_cityHash ) {
			if ( ! ad->m_cityId32 ) {
				//! ad->m_adm1->m_cid  ) {
				char *xx=NULL;*xx=0; }
		}
		// get first section containing place #i
		Section *sp = m_sectionPtrs[p->m_a];
		// telescope all the way up
		for ( ; sp ; sp = sp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// propagate
			sp->m_addrXor ^= h;
			if ( ! sp->m_addrXor ) sp->m_addrXor = h;
		}
	}
}


bool Sections::setTableRowsAndCols ( Section *tableSec ) {

	//int32_t rowCount = -1;
	int32_t colCount = -1;
	int32_t maxColCount = -1;
	int32_t maxRowCount = -1;
	Section *headCol[100];
	Section *headRow[300];
	int32_t rowspanAcc[300];

	int32_t maxCol = -1;
	int32_t minCol =  99999;
	int32_t maxRow = -1;
	int32_t minRow =  99999;

	int32_t rowCount = 0;
	// init rowspan info
	for ( int32_t k = 0 ; k < 300 ; k++ ) rowspanAcc[k] = 1;

	Section *prev = NULL;
	Section *above[100];
	memset ( above,0,sizeof(Section *)*100);

	// scan sections in the table
	for ( Section *ss = tableSec->m_next ; ss ; ss = ss->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if went outside of table
		if ( ss->m_a >= tableSec->m_b ) break;
		// table in a table?
		Section *p = ss->m_parent;
		for ( ; p ; p = p->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( p->m_tagId == TAG_TABLE ) break;
		}
		// must not be within another table in our table
		if ( p != tableSec ) continue;
		// int16_tcut
		nodeid_t tid = ss->m_tagId;
		// . ok, we are a section the the table "tableSec" now
		// . row?
		if ( tid == TAG_TR ) {
			rowCount++;
			colCount = 0;
			continue;
		}
		// . column?
		// . fix eviesays.com which has "what" in a <th> tag
		if ( tid != TAG_TD && tid != TAG_TH ) continue;
		// . must have had a row
		if ( rowCount <= 0 ) continue;
		// . did previous td tag have multiple rowspan?
		// . this <td> tag is referring to the first column
		//   that has not exhausted its rowspan
		for ( ; colCount<300 ; colCount++ ) 
			if ( --rowspanAcc[colCount] <= 0 ) break;
		
		// is it wacko? we should check this in Dates.cpp
		// and ignore all dates in such tables or at least
		// not allow them to pair up with each other
		int32_t  rslen;
		char *rs = getFieldValue ( m_wptrs[ss->m_a] , // tag
					   m_wlens[ss->m_a] , // tagLen
					   "rowspan"  , 
					   &rslen );
		int32_t rowspan = 1;
		if ( rs ) rowspan = atol2(rs,rslen);
		//if ( rowspan != 1 ) 
		//	tableSec->m_flags |= SEC_WACKO_TABLE;
		if ( colCount < 300 )
			rowspanAcc[colCount] = rowspan;
		
		//Section *cs = m_sectionPtrs[i];
		// update headCol[] array to refer to us
		if ( rowCount == 1 && colCount < 100 ) {
			headCol[colCount] = ss;
			// record a max since some tables have
			// fewer columns in first row! bad tables!
			maxColCount = colCount;
		}
		// update headRow[] array to refer to us
		if ( colCount == 0 && rowCount < 300 ) {
			headRow[rowCount] = ss;
			// same for this
			maxRowCount = rowCount;
		}
		// set our junk
		if ( colCount < 100 && colCount <= maxColCount )
			ss->m_headColSection = headCol[colCount];
		if ( rowCount < 300 && rowCount <= maxRowCount )
			ss->m_headRowSection = headRow[rowCount];
		colCount++;
		// start counting at "1" so that way we know that a
		// Sections::m_col/rowCount of 0 is invalid
		ss->m_colNum   = colCount;
		ss->m_rowNum   = rowCount;
		ss->m_tableSec = tableSec;


		// . sets Section::m_cellAbove and Section::m_cellLeft to 
		//   point to the td or th cells above us or to the left of us
		//   respectively.
		// . use this to scan for dates when telescoping 
		// . if date is in a table and date you are telescoping to is 
		//   in the same table it must be to your left or above you in
		//   the same row/col if SEC_HASDATEHEADERROW or 
		//   SEC_HASDATEHEADERCOL is set for the table.
		// . so Dates::isCompatible() needs this function to set 
		//   those ptrs

		// who was on our left?
		if ( prev && prev->m_rowNum == rowCount )
			ss->m_leftCell = prev;
		// who is above us?
		if ( colCount<100 && rowCount>=2 && above[colCount] )
			ss->m_aboveCell = above[colCount];

		// update for row
		prev = ss;
		// update for col
		if ( colCount<100) above[colCount] = ss;

		// first word position in section. -1 if no words contained.
		if ( ss->m_firstWordPos >= 0 ) {
			if ( colCount > maxCol ) maxCol = colCount;
			if ( colCount < minCol ) minCol = colCount;
			if ( rowCount > maxRow ) maxRow = rowCount;
			if ( rowCount < minRow ) minRow = rowCount;
		}
		
		//
		// propagate to all child sections in our section
		//
		int32_t maxb = ss->m_b;
		Section *kid = ss->m_next;
		for ( ; kid && kid->m_b <= maxb ; kid = kid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if belongs to another table already
			if ( kid->m_tableSec &&
			     tableSec->contains ( kid->m_tableSec) ) 
				continue;
			// set his junk
			kid->m_colNum         = colCount;
			kid->m_rowNum         = rowCount;
			kid->m_headColSection = ss->m_headColSection;
			kid->m_headRowSection = ss->m_headRowSection;
			kid->m_tableSec       = ss->m_tableSec;
		}
	}

	// . require at least a 2x2!!!
	// . AND require there have been text in the other dimensions
	//   in order to fix the pool hours for www.the-w.org/poolsched.html
	//   TODO!!!
	// . TODO: allow addlist to combine dates across <td> or <th> tags
	//   provided the table is NOT SEC_MULTIDIMS...
	if ( maxRow != minRow && maxCol != minCol )
		tableSec->m_flags |= SEC_MULTIDIMS;

	return true;
}

// . "<table>" section tag is "sn"
// . set SEC_TABLE_HEADER on contained <td> or <th> cells that represent
//   a header column or row
bool Sections::setTableHeaderBits ( Section *ts ) {

	static char *s_tableFields [] = {
		// . these have no '$' so they are exact/full matches
		// . table column headers MUST match full matches and are not
		//   allowed to match substring matches
		"+who",
		"+what",
		"+event",
		"+title",

		// use '$' to endicate sentence ENDS in this
		"-genre",
		"-type",
		"-category",
		"-categories",
		"@where",
		"@location",
		"-contact", // contact: john
		"-neighborhood", // yelp uses this field
		"@venue",
		"-instructor",
		"-instructors",
		"-convenor", // graypanthers uses convenor:
		"-convenors", // graypanthers uses convenor:
		"-caller", // square dancing
		"-callers",
		"-call", // phone number
		"-price",
		"-price range",
		"@event location",
	};
	// store these words into field table "ft"
	static HashTableX s_ft;
	static char s_ftbuf[2000];
	static bool s_init0 = false;
	if ( ! s_init0 ) {
		s_init0 = true;
		s_ft.set(8,4,128,s_ftbuf,2000,false,m_niceness,"ftbuf");
		int32_t n = (int32_t)sizeof(s_tableFields)/ sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// set words
			char *s = s_tableFields[i];
			Words w; w.set3 ( s );
			int64_t *wi = w.getWordIds();
			int64_t h = 0;
			// scan words
			for ( int32_t j = 0 ; j < w.getNumWords(); j++ )
				if ( wi[j] ) h ^= wi[j];
			// . store hash of all words, value is ptr to it
			// . put all exact matches into ti1 and the substring
			//   matches into ti2
			if ( ! s_ft.addKey ( &h , &s ) ) {char *xx=NULL;*xx=0;}
		}
	}


	int32_t colVotes = 0;
	int32_t rowVotes = 0;
	int32_t maxCol = 0;
	int32_t maxRow = 0;
	Section *sn = ts;
	// scan the sections in the table
	for ( ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop if leaves the table
		if ( sn->m_a >= ts->m_b ) break;
		// skip if in another table in the "ts" table
		if ( sn->m_tableSec != ts ) continue;
		// must be a TD section or TH section
		if ( sn->m_tagId != TAG_TD && sn->m_tagId != TAG_TH ) continue;
		// get max
		if ( sn->m_rowNum > maxRow ) maxRow = sn->m_rowNum;
		if ( sn->m_colNum > maxCol ) maxCol = sn->m_colNum;
		// must be row or col 1
		if ( sn->m_colNum != 1 && sn->m_rowNum != 1 ) continue;
		// header format?
		bool hdrFormatting = (sn->m_flags & SEC_HEADING_CONTAINER);
		// is it a single format? i.e. no word<tag>word in the cell?
		if ( sn->m_colNum == 1 && hdrFormatting ) colVotes++;
		if ( sn->m_rowNum == 1 && hdrFormatting ) rowVotes++;
		// skip if not heading container
		if ( ! hdrFormatting ) continue;
		// look for certain words like "location:" or "venue", those
		// are very strong indicators of a header row or header col
		for ( int32_t i = sn->m_a ; i < sn->m_b ; i++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			if ( ! s_ft.isInTable ( &m_wids[i] ) ) continue;
			if ( sn->m_colNum == 1 ) colVotes += 10000;
			if ( sn->m_rowNum == 1 ) rowVotes += 10000;
		}
	}	

	bool colWins = false;
	bool rowWins = false;
	// colWins means col #1 is the table header
	if ( colVotes > rowVotes ) colWins = true;
	// rowWins means row #1 is the table header
	if ( colVotes < rowVotes ) rowWins = true;

	// do another scan of table
	sn = ts;
	// skip loop if no need
	if ( ! rowWins && ! colWins ) sn = NULL;
	// if table only has one row or col
	if ( maxRow <= 1 && maxCol <= 1 ) sn = NULL;

	// scan the sections in the table
	for ( ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop if leaves the table
		if ( sn->m_a >= ts->m_b ) break;
		// skip if in another table in the "ts" table
		if ( sn->m_tableSec != ts ) continue;
		// must be a TD section or TH section
		if ( sn->m_tagId != TAG_TD && sn->m_tagId != TAG_TH ) continue;
		// it must be in the winning row or column
		if ( rowWins && sn->m_rowNum != 1 ) continue;
		if ( colWins && sn->m_colNum != 1 ) continue;
		// flag it as a table header
		sn->m_flags |= SEC_TABLE_HEADER;
		// propagate to all kids as well so the sentence itself
		// will have SEC_TABLE_HEADER set so we can detect that
		// in setSentFlags(), because we use it for setting
		// SENT_TAGS
		Section *kid = sn->m_next;
		for ( ; kid && kid->m_b <= sn->m_b ; kid = kid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if does not belong to our table
			if ( kid->m_tableSec &&
			     kid->m_tableSec != sn->m_tableSec ) continue;
			// set it
			kid->m_flags |= SEC_TABLE_HEADER;
		}
	}

	// scan the cells in the table, NULL out the 
	// m_headColSection or m_headRowSection depending
	sn = ts;
	// scan the sections in the table
	for ( ; sn ; sn = sn->m_next ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// stop if leaves the table
		if ( sn->m_a >= ts->m_b ) break;
		// skip if in another table in the "ts" table
		if ( sn->m_tableSec != ts ) continue;
		// must be a TD section or TH section
		if ( sn->m_tagId != TAG_TD && sn->m_tagId != TAG_TH ) continue;
		// if its a header section itself...
		if ( sn->m_flags & SEC_TABLE_HEADER ) {
			sn->m_headColSection = NULL;
			sn->m_headRowSection = NULL;
			// keep going so we can propagate the NULLs to our kids
		}
		// get its hdr
		Section *hdr = sn->m_headColSection;
		// only if we are > row 1
		//if ( sn->m_rowNum >= 2 ) hdr = sn->m_headColSection;
		// must have table header set
		if ( hdr && ! ( hdr->m_flags & SEC_TABLE_HEADER ) )
		     // but if we are not in the first col, we can use it
		     //sn->m_colNum == 1 ) 
			sn->m_headColSection = NULL;
		// same for row
		hdr = sn->m_headRowSection;
		// only if we are col > 1
		//if ( ! hdr && sn->m_colNum >= 2 ) hdr = sn->m_headRowSection;
		// must have table header set
		if ( hdr && ! ( hdr->m_flags & SEC_TABLE_HEADER ) )
		     // . but if we are not in the first row, we can use it
		     // . m_rowNum starts at 1, m_colNum starts at 1
		     //sn->m_rowNum == 1 ) 
			sn->m_headRowSection = NULL;

		//
		// propagate to all child sections in our section
		//
		Section *kid = sn->m_next;
		for ( ; kid && kid->m_b <= sn->m_b ; kid = kid->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if does not belong to our table
			if ( kid->m_tableSec != ts ) continue;
			// set his junk
			//kid->m_colNum         = sn->m_colNum;
			//kid->m_rowNum         = sn->m_rowNum;
			kid->m_headColSection = sn->m_headColSection;
			kid->m_headRowSection = sn->m_headRowSection;
			//kid->m_tableSec       = sn->m_tableSec;
		}
	}

	return true;
}


#include "gb-include.h"
#include "Threads.h"

Sectiondb g_sectiondb;
Sectiondb g_sectiondb2;

// reset rdb
void Sectiondb::reset() { m_rdb.reset(); }

// init our rdb
bool Sectiondb::init ( ) {
	// . what's max # of tree nodes?
        // . key+4+left+right+parents+dataPtr = sizeof(key128_t)+4 +4+4+4+4
        // . 32 bytes per record when in the tree
	int32_t node = 16+4+4+4+4+4 + sizeof(SectionVote);
	// . assume avg TitleRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	//int32_t maxTreeNodes  = g_conf.m_sectiondbMaxTreeMem  / node;
	int32_t maxTreeMem    = 200000000;
	int32_t maxTreeNodes  = maxTreeMem / node;
	// . we now use a disk page cache for sectiondb as opposed to the
	//   old rec cache. i am trying to do away with the Rdb::m_cache rec
	//   cache in favor of cleverly used disk page caches, because
	//   the rec caches are not real-time and get stale.
	// . just hard-code 5MB for now
	//int32_t pcmem = 5000000; // = g_conf.m_sectiondbMaxDiskPageCacheMem;

	// do not use for now i think we use posdb and store the 32bit
	// val in the key for facet type stuff
	//pcmem = 0;
	maxTreeMem = 100000;
	maxTreeNodes = 1000;

	/*
	key128_t k;
	uint32_t sech32 = (uint32_t)rand();
	uint64_t sh64 = (uint64_t)rand() << 32LL | rand();
	uint8_t secType = 23;
	int64_t docId = ((uint64_t)rand() << 32 | rand()) & DOCID_MASK;
	k = makeKey2 ( sech32,
		       sh64,
		       secType,
		       docId,
		       false ); // del key?
	if ( g_sectiondb.getSectionHash(&k) != sech32 ){char*xx=NULL;*xx=0;}
	if ( g_sectiondb.getSiteHash(&k) != sh64 ){char*xx=NULL;*xx=0;}
	if ( g_sectiondb.getSectionType(&k) !=secType){char*xx=NULL;*xx=0;}
	if ( g_sectiondb.getDocId(&k) != docId ){char*xx=NULL;*xx=0;}
	*/
	

	// do not use any page cache if doing tmp cluster in order to
	// prevent swapping
	// if ( g_hostdb.m_useTmpCluster ) pcmem = 0;
	// int32_t pageSize = GB_INDEXDB_PAGE_SIZE;
	// // init the page cache
	// if ( ! m_pc.init ( "sectiondb",
	// 		   RDB_SECTIONDB,
	// 		   pcmem    ,
	// 		   pageSize ) )
	// 	return log("db: Sectiondb init failed.");

	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir               ,
			    "sectiondb"                  ,
			    true                         , // dedup same keys?
			    sizeof(SectionVote)          , // fixed record size
			    // avoid excessive merging!
			    -1                           , // min files 2 merge
			    maxTreeMem                   ,
			    maxTreeNodes                 ,
			    true                         , // balance tree?
			    // turn off cache for now because the page cache
			    // is just as fast and does not get out of date
			    // so bad??
			    0                            ,
			    0                            , // maxCacheNodes
			    false                        , // half keys?
			    false                        , // saveCache?
			    NULL,//&m_pc                , // page cache ptr
			    false                        , // is titledb?
			    false                        , // preloadcache?
			    16                           ))// keySize
		return false;
	return true;
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Sectiondb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
        // . key+4+left+right+parents+dataPtr = sizeof(key128_t)+4 +4+4+4+4
        // . 32 bytes per record when in the tree
	int32_t node = 16+4+4+4+4+4 + sizeof(SectionVote);
	// . NOTE: overhead is about 32 bytes per node
	int32_t maxTreeNodes  = treeMem / node;
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "sectiondbRebuild"          ,
			    true                        , // dedup same keys?
			    sizeof(SectionVote)         , // fixed record size
			    50                          , // MinFilesToMerge
			    treeMem                     ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    false                       , // half keys?
			    false                       , // sectiondbSaveCache
			    NULL                        , // page cache ptr
			    false                       , // is titledb?
			    false                       , // prelaod cache?
			    16                          ))// keySize
		return false;
	return true;
}
/*
bool Sectiondb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: sectiondb verify failed, but scaling is allowed, passing.");
	return true;
}
*/
bool Sectiondb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Sectiondb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	CollectionRec *cr = g_collectiondb.getRec(coll);

	if ( ! msg5.getList ( RDB_SECTIONDB   ,
			      cr->m_collnum          ,
			      &list         ,
			      &startKey      ,
			      &endKey        ,
			      1024*1024     , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        ,
			      false         )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key128_t k;
		list.getCurrentKey(&k);
		count++;
		uint32_t shardNum = getShardNum ( RDB_SECTIONDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %"INT32" records in sectiondb, "
		     "only %"INT32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( count > 10 && got == 0 ) 
			log("db: Are you sure you have the right "
				   "data in the right directory? "
				   "Exiting.");
		log ( "db: Exiting due to Sectiondb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Sectiondb passed verification successfully for "
	      "%"INT32" recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}


bool Sections::setListFlags ( ) {

	return true;
	/*
	sec_t sdf = SEC_HAS_DOM|SEC_HAS_MONTH|SEC_HAS_DOW|SEC_HAS_TOD;
	// scan all sentences
	for ( Section *si = m_firstSent ; si ; si = si->m_nextSent ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// int16_tcut
		sentflags_t sf = si->m_sentFlags;
		// . fortable field must not end in period
		// . i.e. "Good For Kids:"
		if ( sf & SENT_PERIOD_ENDS ) continue;
		// get next sent
		Section *next = si->m_nextSent;
		// this stops things..
		if ( ! next ) continue;
		// must not end in period either
		if ( next->m_sentFlags & SENT_PERIOD_ENDS ) continue;
		Section *ps;
	*/
}

bool Sections::growSections ( ) {
	// make a log note b/c this should not happen a lot because it's slow
	log("build: growing sections!");
	g_errno = EDOCBADSECTIONS;
	return true;
	// record old buf start
	char *oldBuf = m_sectionBuf.getBufStart();
	// grow by 20MB at a time
	if ( ! m_sectionBuf.reserve ( 20000000 ) ) return false;
	// for fixing ptrs:
	char *newBuf = m_sectionBuf.getBufStart();
	// set the new max
	m_maxNumSections = m_sectionBuf.getCapacity() / sizeof(Section);
	// update ptrs in the old sections
	for ( int32_t i = 0 ; i < m_numSections ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		Section *si = &m_sections[i];
		if ( si->m_parent ) {
			char *np = (char *)si->m_parent;
			np = np - oldBuf + newBuf;
			si->m_parent = (Section *)np;
		}
		if ( si->m_next ) {
			char *np = (char *)si->m_next;
			np = np - oldBuf + newBuf;
			si->m_next = (Section *)np;
		}
		if ( si->m_prev ) {
			char *np = (char *)si->m_prev;
			np = np - oldBuf + newBuf;
			si->m_prev = (Section *)np;
		}
		if ( si->m_listContainer ) {
			char *np = (char *)si->m_listContainer;
			np = np - oldBuf + newBuf;
			si->m_listContainer = (Section *)np;
		}
		if ( si->m_prevBrother ) {
			char *np = (char *)si->m_prevBrother;
			np = np - oldBuf + newBuf;
			si->m_prevBrother = (Section *)np;
		}
		if ( si->m_nextBrother ) {
			char *np = (char *)si->m_nextBrother;
			np = np - oldBuf + newBuf;
			si->m_nextBrother = (Section *)np;
		}
		if ( si->m_sentenceSection ) {
			char *np = (char *)si->m_sentenceSection;
			np = np - oldBuf + newBuf;
			si->m_sentenceSection = (Section *)np;
		}
		if ( si->m_prevSent ) {
			char *np = (char *)si->m_prevSent;
			np = np - oldBuf + newBuf;
			si->m_prevSent = (Section *)np;
		}
		if ( si->m_nextSent ) {
			char *np = (char *)si->m_nextSent;
			np = np - oldBuf + newBuf;
			si->m_nextSent = (Section *)np;
		}
		if ( si->m_tableSec ) {
			char *np = (char *)si->m_tableSec;
			np = np - oldBuf + newBuf;
			si->m_tableSec = (Section *)np;
		}
		if ( si->m_headColSection ) {
			char *np = (char *)si->m_headColSection;
			np = np - oldBuf + newBuf;
			si->m_headColSection = (Section *)np;
		}
		if ( si->m_headRowSection ) {
			char *np = (char *)si->m_headRowSection;
			np = np - oldBuf + newBuf;
			si->m_headRowSection = (Section *)np;
		}
		if ( si->m_leftCell ) {
			char *np = (char *)si->m_leftCell;
			np = np - oldBuf + newBuf;
			si->m_leftCell = (Section *)np;
		}
		if ( si->m_aboveCell ) {
			char *np = (char *)si->m_aboveCell;
			np = np - oldBuf + newBuf;
			si->m_aboveCell = (Section *)np;
		}
	}
	return true;
}

		
