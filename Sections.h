#ifndef _SECTIONS_H_
#define _SECTIONS_H_

#include "HashTableX.h"
#include "Msg0.h"
#include "IndexList.h"
#include "Dates.h" // datetype_t
#include "Words.h"
#include "Rdb.h"
//#include "DiskPageCache.h"


// KEY:
// ssssssss ssssssss ssssssss ssssssss  s = 48 bit site hash
// ssssssss ssssssss hhhhhhhh hhhhhhhh  h = hash value (32 bits of the 64 bits!)
// hhhhhhhh hhhhhhhh tttttttt dddddddd  t = tag type
// dddddddd dddddddd dddddddd ddddddHD  d = docid

// DATA:
// SSSSSSSS SSSSSSSS SSSSSSSS SSSSSSSS  S = SectionVote::m_score
// NNNNNNNN NNNNNNNN NNNNNNNN NNNNNNNN  N = SectionVote::m_numSampled

// h: hash value. typically the lower 32 bits of the 
//    Section::m_sentenceContentHash64 or the Section::m_contentHash64 vars. we
//    do not need the full 64 bits because we have the 48 bit site hash included
//    to reduce collisions substantially.

// 
// BEGIN SECTION BIT FLAGS (sec_t)
// values for Section::m_flags, of type sec_t
// 

// . these are descriptive flags, they are computed when Sections is set
// . SEC_NOTEXT sections do not vote, i.e. they are not stored in Sectiondb
#define SEC_NOTEXT       0x0001 // implies section has no alnum words
//#define SEC_ARTICLE    0x0002 // section is SV_UNIQUE and SV_TEXTY
//#define SEC_DUP        0x0004 // content hash repeated on same site

// . Weights.cpp zeroes out the weights for these types of sections
// . is section delimeted by the <script> tag, <marquee> tag, etc.
#define SEC_SCRIPT       0x0008
#define SEC_STYLE        0x0010
#define SEC_SELECT       0x0020
#define SEC_MARQUEE      0x0040
#define SEC_CONTAINER    0x0080
// . is section in anchor text
// . is section delimeted by the <a href...> tag
//#define SEC_A            0x0080

// . in title/header. for gigabits in XmlDoc.cpp
// . is section delemited by <title> or <hN> tags?
#define SEC_IN_TITLE     0x0100
#define SEC_IN_HEADER    0x0200

// used by Events.cpp to indicate if section contains a TimeOfDay ("7 p.m.")
#define SEC_HAS_TOD      0x0400 
#define SEC_HIDDEN       0x0800 // <div style="display: none">
#define SEC_IN_TABLE     0x1000
#define SEC_FAKE         0x2000 // <hr>/<br>/sentence based faux section
#define SEC_NOSCRIPT     0x4000

#define SEC_HEADING_CONTAINER 0x8000

#define SEC_MENU         0x010000
#define SEC_LINK_TEXT    0x020000
#define SEC_MENU_HEADER  0x040000
#define SEC_INPUT_HEADER 0x080000
#define SEC_INPUT_FOOTER 0x100000
#define SEC_HEADING      0x200000

// reasons why a section is not an event
//#define SEC_MULT_PLACES    0x008000 
//#define SEC_IS_MENUITEM        0x00040000 // in a list of menu items?
#define SEC_UNBALANCED         0x00400000 // interlaced section/tags
#define SEC_OPEN_ENDED         0x00800000 // no closing tag found
#define SEC_SENTENCE           0x01000000 // made by a sentence?
#define SEC_PLAIN_TEXT         0x02000000
#define SEC_HAS_NONFUZZYDATE   0x04000000

// . this is set in Dates.cpp and used by Dates.cpp and Events.cpp
// . we identify max tod sections and make it so brothers in a list of two
//   or more such sections cannot telescope to each other's dates, and so we
//   do not share each other's event descriptions. fixes abqtango.com
//   and salsapower.com from grabbing event description text from "failed"
//   event sections that are brothers to successful event sections.
#define SEC_TOD_EVENT               0x00008000000LL
#define SEC_NIXED_HEADING_CONTAINER 0x00010000000LL

#define SEC_SECOND_TITLE            0x00020000000LL
#define SEC_SPLIT_SENT              0x00040000000LL
#define SEC_HAS_REGISTRATION        0x00080000000LL

#define SEC_HAS_PARKING             0x00100000000LL
#define SEC_MENU_SENTENCE           0x00200000000LL
// fix for folkmads.org:
#define SEC_HR_CONTAINER            0x00400000000LL
#define SEC_HAS_DOM                 0x00800000000LL
#define SEC_HAS_DOW                 0x01000000000LL
#define SEC_EVENT_BROTHER           0x02000000000LL
#define SEC_DATE_LIST_CONTAINER     0x04000000000LL
#define SEC_TAIL_CRAP               0x08000000000LL

#define SEC_CONTROL                 0x0000010000000000LL
#define SEC_STRIKE                  0x0000020000000000LL
#define SEC_STRIKE2                 0x0000040000000000LL
#define SEC_HAS_MONTH               0x0000080000000000LL
#define SEC_IGNOREEVENTBROTHER      0x0000100000000000LL
#define SEC_HASEVENTDOMDOW          0x0000200000000000LL
#define SEC_STOREHOURSCONTAINER     0x0000400000000000LL
#define SEC_PUBDATECONTAINER        0x0000800000000000LL

#define SEC_TABLE_HEADER            0x0001000000000000LL
#define SEC_HASDATEHEADERROW        0x0002000000000000LL
#define SEC_HASDATEHEADERCOL        0x0004000000000000LL
#define SEC_MULTIDIMS               0x0008000000000000LL
#define SEC_HASHXPATH               0x0010000000000000LL

//#define SEC_HAS_ADDRESS        0x08000000
//#define SEC_ADDRESS_CONTAINER  0x40000000
//#define SEC_HAS_STOREHOURS     0x01000000 // event is really just store hours
//#define SEC_HAS_NONSTOREHOURS  0x02000000
//#define SEC_HAS_NON_EVENT_DATE 0x04000000


// . some random-y numbers for Section::m_baseHash
// . used by splitSection() function
//#define BH_BR      -1113348753
//#define BH_BRBR    3947503
//#define BH_HR      1378153634
//#define BH_H1     -1788814047
//#define BH_H2     -1170023066
//#define BH_H3     -132582659
//#define BH_H4      2095609929
#define BH_BULLET  7845934
#define BH_SENTENCE 4590649
#define BH_IMPLIED  95468323
//#define BH_IMPLIED_LIST 9434499

// values for Section::m_sentFlags (sentence flags)
#define SENT_HAS_COLON       0x00000001
// AVAIL - #define SENT_DUP_SECTION     0x00000002
#define SENT_BAD_FIRST_WORD  0x00000004
#define SENT_MIXED_CASE      0x00000008
#define SENT_POWERED_BY      0x00000010
#define SENT_MULT_EVENTS     0x00000020
#define SENT_PAGE_REPEAT     0x00000040
#define SENT_NUMBERS_ONLY    0x00000080
#define SENT_IN_ADDRESS      0x00000100
#define SENT_SECOND_TITLE    0x00000200
#define SENT_IS_DATE         0x00000400
#define SENT_LAST_STOP       0x00000800
#define SENT_NUMBER_START    0x00001000
#define SENT_TAG_INDICATOR   0x00002000
#define SENT_PRETTY          0x00004000
#define SENT_IN_HEADER       0x00008000
#define SENT_MIXED_CASE_STRICT 0x00010000
#define SENT_IN_LIST         0x00020000
#define SENT_COLON_ENDS      0x00040000
#define SENT_IN_ADDRESS_NAME 0x00080000
#define SENT_IN_TITLEY_TAG   0x00100000
#define SENT_CITY_STATE      0x00200000
#define SENT_PRICEY          0x00400000
#define SENT_PERIOD_ENDS     0x00800000
#define SENT_HAS_PHONE       0x01000000
#define SENT_IN_MENU         0x02000000
#define SENT_MIXED_TEXT      0x04000000
#define SENT_TAGS            0x08000000
#define SENT_INTITLEFIELD    0x10000000
#define SENT_STRANGE_PUNCT   0x20000000
#define SENT_INPLACEFIELD    0x40000000
#define SENT_INNONTITLEFIELD 0x80000000

// AVAIL -- #define SENT_TOO_MANY_WORDS      0x0000000100000000LL
#define SENT_HASNOSPACE          0x0000000200000000LL
#define SENT_IS_BYLINE           0x0000000400000000LL
#define SENT_NON_TITLE_FIELD     0x0000000800000000LL
#define SENT_TITLE_FIELD         0x0000001000000000LL
#define SENT_UNIQUE_TAG_HASH     0x0000002000000000LL
#define SENT_AFTER_SENTENCE      0x0000004000000000LL
#define SENT_WORD_SANDWICH       0x0000008000000000LL
#define SENT_LOCATION_SANDWICH   0x0000010000000000LL
#define SENT_NUKE_FIRST_WORD     0x0000020000000000LL
#define SENT_FIELD_NAME          0x0000040000000000LL
#define SENT_PERIOD_ENDS_HARD    0x0000080000000000LL
#define SENT_PARENS_START        0x0000100000000000LL
#define SENT_IN_MENU_HEADER      0x0000200000000000LL
#define SENT_IN_TRUMBA_TITLE     0x0000400000000000LL
#define SENT_PLACE_NAME          0x0000800000000000LL
#define SENT_FORMTABLE_FIELD     0x0001000000000000LL
#define SENT_FORMTABLE_VALUE     0x0002000000000000LL
#define SENT_IN_TAG              0x0004000000000000LL
#define SENT_AFTER_SPACER        0x0008000000000000LL
#define SENT_BEFORE_SPACER       0x0010000000000000LL
#define SENT_OBVIOUS_PLACE       0x0020000000000000LL
//#define SENT_ONROOTPAGE        0x0040000000000000LL
#define SENT_HASSOMEEVENTSDATE   0x0080000000000000LL
#define SENT_AFTER_COLON         0x0100000000000000LL
#define SENT_HASTITLEWORDS       0x0200000000000000LL
// AVAIL -- #define SENT_EVENT_ENDING        0x0400000000000000LL
#define SENT_CONTAINS_PLACE_NAME 0x0800000000000000LL
#define SENT_IN_BIG_LIST         0x1000000000000000LL
#define SENT_BADEVENTSTART       0x2000000000000000LL
#define SENT_MENU_SENTENCE       0x4000000000000000LL
#define SENT_HAS_PRICE           0x8000000000000000ULL


// flags for an Event/Sentence pair!!
typedef int32_t esflags_t;
#define EVSENT_DONOTPRINT          0x00000001
#define EVSENT_GENERIC_PLUS_PLACE  0x00000002
#define EVSENT_GENERIC_WORDS       0x00000004
#define EVSENT_FORMAT_DUP          0x00000008
#define EVSENT_IS_INDEXABLE        0x00000010
#define EVSENT_HASEVENTADDRESS     0x00000020
#define EVSENT_CLOSETODATE         0x00000040
#define EVSENT_HASEVENTDATE        0x00000080
#define EVSENT_NAMEABOVESTREET     0x00000100
#define EVSENT_SECTIONDUP          0x00000200
#define EVSENT_NEARDUP             0x00000400
#define EVSENT_FARDUP              0x00000800
#define EVSENT_FARDUPPHONE         0x00001000
#define EVSENT_FARDUPPRICE         0x00002000
#define EVSENT_SUBEVENTBROTHER     0x00004000
#define EVSENT_JUSTDATES           0x00008000

#define NOINDEXFLAGS (SEC_SCRIPT|SEC_STYLE|SEC_SELECT)

// the section type (bit flag vector for SEC_*) is currently 32 bits
typedef int64_t sec_t;
//typedef int64_t titleflags_t;
typedef int64_t sentflags_t;
typedef uint32_t turkbits_t;

bool  isPlaceIndicator ( int64_t *widp ) ;
char *getSentBitLabel ( sentflags_t sf ) ;
char *getEventSentBitLabel ( esflags_t esflags ) ;
char *getTurkBitLabel ( turkbits_t tb ) ;
sentflags_t getMixedCaseFlags ( class Words *words , 
				wbit_t *bits ,
				int32_t senta , 
				int32_t sentb , 
				int32_t niceness ) ;
int32_t hasTitleWords ( sentflags_t sflags ,
		     int32_t senta,
		     int32_t sentb,
		     int32_t alnumCount,
		     class Bits *bits ,
		     class Words *words ,
		     bool useAsterisk ,
		     int32_t niceness );


class Sectiondb {

 public:

	// reset rdb
	void reset();

	bool verify ( char *coll );

	bool addColl ( char *coll, bool doVerify = true );

	// init m_rdb
	bool init ();

	// init secondary/rebuild sectiondb
	bool init2 ( int32_t treeMem ) ;

	Rdb *getRdb() { return &m_rdb; }

	uint64_t getSiteHash ( void *k ) {
		return ((*(uint64_t *)(((char *)k)+8))) >> 16;};


	uint32_t getSectionHash ( void *k ) {
		return (*(uint32_t *)(((char *)k)+6)); }


	int64_t getDocId ( void *k ) {
		return ((*(uint64_t *)k) >> 2) & DOCID_MASK; }


	uint8_t getSectionType ( void *k ) {
		return ((unsigned char *)k)[5]; };

	// holds binary format title entries
	Rdb m_rdb;

	//DiskPageCache *getDiskPageCache ( ) { return &m_pc; };

	//DiskPageCache m_pc;
};

extern class Sectiondb g_sectiondb;
extern class Sectiondb g_sectiondb2;


// this is only needed for sections, not facets in general i don think.
// facets has the whole QueryTerm::m_facetHashTable array with more info
//
// . for gbfacet:gbxpathsite1234567 posdb query stats compilation to
//   show how many pages duplicate your section's content on your site
//   at the same xpath. the hash of the innerHTML for that xpath is 
//   embedded into the posdb key like a number in a number key, so the
//   wordpos bits etc are sacrificed to hold that 32-bit number.
// . used by XmlDoc::getSectionsWithDupStats() for display in
//   XmlDoc::printRainbowSections()
// . these are in QueryTerm::m_facetStats and computed from
//   QueryTerm::m_facetHashTable
class SectionStats {
 public:
	SectionStats() { reset(); }
	void reset ( ) {
		m_totalMatches  = 0; // posdb key "val" matches ours
		m_totalEntries  = 0; // total posdb keys
		m_numUniqueVals = 0; // # of unique "vals"
		m_totalDocIds   = 0;
	};
	// # of times xpath innerhtml matched ours. 1 count per docid max.
	int64_t m_totalMatches;
	// # of times this xpath occurred. doc can have multiple times.
	int64_t m_totalEntries;
	// # of unique vals this xpath had. doc can have multiple counts.
	int64_t m_numUniqueVals;
	int64_t m_totalDocIds;
};


class Section {
public:

	// . the section immediately containing us
	// . used by Events.cpp to count # of timeofdays in section
	class Section *m_parent;

	// . we are in a linked list of sections
	// . this is how we keep order
	class Section *m_next;
	class Section *m_prev;

	// used by Events.cpp to count # of timeofdays in section
	//class Event *m_event;

	// for Events class
	//uint8_t m_numAddresses;
	//class Address *m_address;
	// for Events class, usually streets!
	//uint8_t m_numPlaces;
	//class Place *m_place;
	class Addresses *m_aa;

	// . if we are an element in a list, what is the list container section
	// . a containing section is a section containing MULTIPLE 
	//   smaller sections
	// . right now we limit such contained elements to text sections only
	// . used to set SEC_HAS_MENUBROTHER flag
	class Section *m_listContainer;

	// if we are a header, of what list are we a header of?
	//class Section *m_headerOfList;
	

	// the sibling section before/after us. can be NULL.
	class Section *m_prevBrother;
	class Section *m_nextBrother;

	// if we are in a bold section in a sentence section then this
	// will point to the sentence section that contains us. if we already
	// are a sentence section then this points to itself.
	class Section *m_sentenceSection;

	// . set in XmlDoc::getSectionsWithDupStats()
	// . voting info for this section over all indexed pages from this site
	SectionStats m_stats;

	// this (minus -1) references into Addresses::m_sorted[] which is
	// a list of Places. so we can quickly scan that list for the Places
	// contained in just this section. but you have to subtract one
	// from m_firstPlaceNum to get the proper index into that array because
	// we add one to it since 0 is the initial value.
	int32_t m_firstPlaceNum;

	int32_t m_votesForDup;
	int32_t m_votesForNotDup;
	float getSectiondbVoteFactor ( ) {
		// now punish if repeated on many page on the site
		float a = (float)m_votesForNotDup;
		float b = (float)m_votesForDup;
		if ( a == 0 && b == 0 ) return 1.0;
		// use that as a modifier
		float factor = a / ( a + b);
		// minimum so we do not completely nuke title i guess
		if ( factor < .10 ) factor = .10;
		return factor;
	};

	// position of the first and last alnum word contained directly OR
	// indirectly in this section. use -1 if no text contained...
	int32_t m_firstWordPos;
	int32_t m_lastWordPos;

	// alnum positions for words contained directly OR indirectly as well
	int32_t m_alnumPosA;
	int32_t m_alnumPosB;

	// . for sentences that span multiple sections UNEVENLY
	// . see aliconference.com and abqtango.com for this crazy things
	// . for like 99% of all sections these guys equal m_firstWordPos and
	//   m_lastWordPos respectively
	int32_t m_senta;
	int32_t m_sentb;

	// each sentence is numbered
	//int32_t m_sentNum;

	class Section *m_prevSent;
	class Section *m_nextSent;

	int32_t m_phoneXor;
	int32_t m_emailXor;
	int32_t m_priceXor;
	// make this match Date::m_dateHash size
	int32_t m_todXor;
	int32_t m_dayXor;
	int32_t m_addrXor;
	int32_t m_monthXor;
	int32_t m_dowXor;

	// for Address.cpp setting place names
	//int32_t m_numStreets;

	// . if we are in a table, what position are we
	// . starts at 1 and goes upwards
	// . we start it at 1 so that way we know that 0 is invalid!
	int32_t m_rowNum;
	int32_t m_colNum;
	class Section *m_tableSec;

	class Section *m_headColSection;
	class Section *m_headRowSection;

	class Section *m_leftCell;
	class Section *m_aboveCell;

	// hash of this tag's baseHash and all its parents baseHashes combined
	uint32_t  m_tagHash;

	// like above but for turk voting. includes hash of the class tag attr
	// from m_turkBaseHash, whereas m_tagHash uses m_baseHash of parent.
	uint32_t m_turkTagHash32;

	// for debug output display of color coded nested sections
	uint32_t m_colorHash;

	// like tag hash but only the tag ids, no hashed attributes or 
	// virtual section base hashes
	//int32_t  m_formatHash;

	// tagid of this section, 0 means none (like sentence section, etc.)
	nodeid_t m_tagId;

	/*
	// used by addImpliedSections()
	int32_t getBaseHash2 ( ) { 
		// fix for funkefiredarts.com since one of the header tags
		// has a different tag attribute, but it says "Monday". so
		// treat all these special headers the same since it is
		// critical we get these type of implied sections right, lest
		// we hurt our date telscoping.
		if ( m_flags & SEC_HAS_DOM_DOW ) return 22222;
		if ( m_flags&SEC_HEADING_CONTAINER) return m_baseHash^0x789123;
		else                                return m_baseHash;
	};
	*/

	//int32_t getBaseHash3 ();


	// usually just the m_tagId, but hashes in the class attributes of
	// div and span tags, etc. to make them unique
	uint32_t  m_baseHash;

	// just hash the "class=" value along with the tagid
	uint32_t m_turkBaseHash;

	// kinda like m_baseHash but for xml tags and only hashes the
	// tag name and none of the fields
	uint32_t  m_xmlNameHash;

	// these deal with enumertated tags and are used by Events.cpp
	int32_t  m_occNum;
	int32_t  m_numOccurences;
	// section with same m_tagHash and before you
	//class Section *m_prevSibling;

	// used by XmlDoc.cpp to set a topological distance
	int32_t m_topDist;
	//int32_t m_sortedIndex;

	// all the parent tags are enumerated, but the kid (youngest tag)
	// is not enumerated
	//int32_t  m_enumTagHash;

	// . tag hash which disregards non-breaking or tags with no back tags
	// . used by Events.cpp
	//int32_t  m_hardTagHash;

	// hash of all the alnum words DIRECTLY in this section
	uint64_t  m_contentHash64;
	// if section contains words indirectly, then store xor'ed wids in here
	//int32_t  m_contentHash2;

	uint64_t  m_sentenceContentHash64;

	// . used by the SEC_EVENTBROTHER algo in Dates.cpp to detect
	//   [more] or [details] links that indicate distinct items
	// . sometimes the "(more)" link is combined into the last sentence
	//   so we have to treat the last link kinda like its own sentence too!
	uint32_t  m_lastLinkContentHash32;

	// hash of all sentences contained indirectly or directly.
	// uses m_sentenceContentHash64 (for sentences)
	uint64_t m_indirectSentHash64;

	// for voting! we basically ignore numbers and dates, months, etc.
	// for doing this hash so that if the date changes from page to page
	// it will still be recognized as a "dup section" and m_votesForDup
	// should be high
	//uint32_t m_voteHash32;

	// . range of words in Words class we encompass
	// . m_wordStart and m_wordEnd are the tag word #'s
	// . ACTUALLY it is a half-closed interval [a,b) like all else
	//   so m_b-1 is the word # of the ending tag, BUT split sections
	//   do not include ending tags!!! (i.e. <hr>, <br>, &bull, etc.)
	//   that were made with a call to splitSection()
	int32_t  m_a;//wordStart;
	int32_t  m_b;//wordEnd;

	// for event titles and descriptions
	//float m_titleScore;
	//float m_descScore;
	//titleflags_t  m_titleFlags;
	sentflags_t m_sentFlags;

	// bits set based on turk votes. see the TB_* bits in XmlDoc.h
	//turkbits_t m_turkBits;

	// alnum count for us and all sections we contain
	//int32_t  m_alnumCount;

	// . # alnum words only in this and only this section
	// . if we have none, we are SEC_NOTEXT
	int32_t  m_exclusive;

	// like above, but word must also NOT be in a hyperlink
	//int32_t  m_plain;

	// Address.cpp uses this
	//char     m_numBackToBackSubsections;
	//nodeid_t m_lastTid;

	// # of times this section appears in this doc
	//int32_t  m_totalOccurences; 

	// our depth. # of tags in the hash
	int32_t  m_depth;

	// container for the #define'd SEC_* values above
	sec_t m_flags;

	// used to mark it in Dates.cpp like a breadcrumb trail
	int32_t m_mark;

	// Events.cpp assigns a date to each section
	//int32_t m_fullDate;
	//class Date *m_datePtr;
	int32_t m_firstDate;

	//datetype_t m_hasType;
	datetype_t m_dateBits;

	char m_used;

	//int32_t m_numTods;

	// the event section we contain. used by Events.cpp
	//class Section *m_eventSec;

	// used by Events.cpp for determining what range of events a section
	// contains. we store that range in Events::hash() when we index each
	// word into datedb for events.
	//int32_t m_minEventId;
	//int32_t m_maxEventId;

	// used in Sections::splitSections() function
	int32_t m_processedHash;

	int32_t m_gbFrameNum;

	// . support event ids from 0 to 255
	// . this increases the sizeof this class from 160 to 192 bytes
	//char m_evIdBits[32];
	// how many bits in the above array are set?
	//int16_t m_numEventIdBits;

	/*
	bool hasEventId ( int32_t evId ) {
		// this is an overflow condition...
		if ( evId > 255 ) return false;
		// -1 or 0 means not associated with any event id since
		// all eventIds are >= 1
		if ( m_minEventId <= 0   ) return false;
		if ( evId < m_minEventId ) return false;
		if ( evId > m_maxEventId ) return false;
		unsigned char bitMask = 1 << (evId % 8);
		return m_evIdBits[evId/8] & bitMask;
	};

	void addEventId ( int32_t eid ) {
		if ( eid >= 256 ) return;
		unsigned char bitMask = 1 << (eid % 8);
		unsigned char byteOff = eid / 8;
		if ( m_evIdBits[byteOff] & bitMask ) return;
		m_evIdBits[byteOff] |= bitMask;
		m_numEventIdBits++;
		if ( m_minEventId <= 0 || m_minEventId > eid )
			m_minEventId = eid;
		if ( m_maxEventId <= 0 || m_maxEventId < eid )
			m_maxEventId = eid;
	};
	*/

	// do we contain section "arg"?
	bool contains ( class Section *arg ) {
		return ( m_a <= arg->m_a && m_b >= arg->m_b ); };

	// do we contain section "arg"?
	bool strictlyContains ( class Section *arg ) {
		if ( m_a <  arg->m_a && m_b >= arg->m_b ) return true;
		if ( m_a <= arg->m_a && m_b >  arg->m_b ) return true;
		return false;
	};

	// does this section contain the word #a?
	bool contains2 ( int32_t a ) { return ( m_a <= a && m_b > a ); };

	bool isVirtualSection ( ) ;
};



#define SECTIONS_LOCALBUFSIZE 500

#define FMT_HTML   1
#define FMT_PROCOG 2
#define FMT_JSON   3

class Sections {

 public:

	Sections ( ) ;
	void reset() ;
	~Sections ( ) ;

	// . returns false if blocked, true otherwise
	// . returns true and sets g_errno on error
	// . sets m_sections[] array, 1-1 with words array "w"
	bool set ( class Words    *w           ,
		   class Phrases  *phrases     ,
		   class Bits     *bits        ,
		   class Url      *url         ,
		   int64_t       docId       ,
		   int64_t       siteHash64  ,
		   char           *coll        ,
		   int32_t            niceness    ,
		   void           *state       ,
		   void          (*callback)(void *state) ,
		   uint8_t         contentType ,
		   class Dates    *dates       ,
		   char           *sectionsData,
		   bool            sectionsDataValid ,
		   char           *sectionsData2,
		   //uint64_t        tagPairHash ,
		   char           *buf         ,
		   int32_t            bufSize     ) ;


	bool addVotes(class SectionVotingTable *nsvt, uint32_t tagPairHash );

	bool verifySections ( ) ;

	// . the start and end word # of the article range
	// . all article content is in [start,end)
	//void getArticleRange ( int32_t *start , int32_t *end );

	// add docid-based forced spider recs into the metalist
	//char *respiderLineWaiters ( char *metaList    ,
	//			    char *metaListEnd );
				    // these are from the parent
				    //Url  *url         ,
				    //int32_t  ip          ,
				    //int32_t  priority    ) ;

	int32_t getStoredSize ( ) ;
	static int32_t getStoredSize ( char *p ) ;
	int32_t serialize     ( char *p ) ;
	//int32_t getMemUsed ( ) { return m_sectionsBufSize; };

	bool growSections ( );

	bool getSectiondbList ( );
	bool gotSectiondbList ( bool *needsRecall ) ;

	void setNextBrotherPtrs ( bool setContainer ) ;

	// this is used by Events.cpp Section::m_nextSent
	void setNextSentPtrs();

	bool print ( SafeBuf *sbuf ,
		     class HashTableX *pt ,
		     class HashTableX *et ,
		     class HashTableX *st ,
		     class HashTableX *at ,
		     class HashTableX *tt ,
		     //class HashTableX *rt ,
		     class HashTableX *priceTable ) ;

	void printFlags ( class SafeBuf *sbuf , class Section *sn ,
			  bool justEvents ) ;

	bool swoggleTables ( ) ;
	bool swoggleTable ( int32_t dn , class Section *ts ) ;
	
	bool printVotingInfoInJSON ( SafeBuf *sb ) ;

	bool print2 ( SafeBuf *sbuf ,
		      int32_t hiPos,
		      int32_t *wposVec,
		      char *densityVec,
		      char *diversityVec,
		      char *wordSpamVec,
		      char *fragVec,
		      class HashTableX *st2 ,
		      class HashTableX *tt  ,
		      class Addresses  *aa  ,
		      char format = FMT_HTML ); // bool forProCog );
	bool printSectionDiv ( class Section *sk , char format = FMT_HTML );
	//bool forProCog = false ) ;
	class SafeBuf *m_sbuf;
	//class HashTableX *m_pt;
	//class HashTableX *m_et;
	//class HashTableX *m_at;
	//class HashTableX *m_priceTable;

	char *getSectionsReply ( int32_t *size );
	char *getSectionsVotes ( int32_t *size );

	bool isHardSection ( class Section *sn );

	bool setMenus ( );
	bool setListFlags ( );

	bool setFormTableBits ( ) ;
	bool setTableRowsAndCols ( class Section *tableSec ) ;
	bool setTableHeaderBits ( class Section *table );
	bool setTableStuff  ( ) ;
	bool setTableDateHeaders ( class Section *ts ) ;
	bool setTableScanPtrs ( class Section *ts ) ;

	void setHeader ( int32_t r , class Section *first , sec_t flag ) ;

	bool setHeadingBit ( ) ;

	void setTagHashes ( ) ;

	bool setRegistrationBits ( ) ;
	bool m_setRegBits ;

	void setSectionFlagsForDate ( class Date *di , sec_t flag ) ;

	bool m_alnumPosValid;

	// save it
	class Words *m_words    ;
	class Bits  *m_bits     ;
	class Url   *m_url      ;
	class Dates *m_dates    ;
	int64_t    m_docId    ;
	int64_t    m_siteHash64 ;
	//int64_t    m_tagPairHash;
	char        *m_coll     ;
	void        *m_state    ;
	void       (*m_callback) ( void *state );
	int32_t         m_niceness ;
	int32_t         m_cpuNiceness ;	
	uint8_t      m_contentType;

	int32_t *m_wposVec;
	char *m_densityVec;
	char *m_diversityVec;
	char *m_wordSpamVec;
	char *m_fragVec;
	
	// url ends in .rss or .xml ?
	bool  m_isRSSExt;

	bool m_isTrumba     ;
	bool m_isFacebook   ;
	bool m_isEventBrite ;
	bool m_isStubHub    ;

	Msg0  m_msg0;
	key128_t m_startKey;
	int32_t  m_recall;
	IndexList m_list;
	int64_t m_termId;

	int32_t m_numLineWaiters;
	bool m_waitInLine;
	int32_t m_articleStartWord;
	int32_t m_articleEndWord;
	//int32_t m_totalSimilarLayouts;
	bool m_hadArticle;
	int32_t m_numInvalids;
	int32_t m_totalSiteVoters;

	int32_t m_numAlnumWordsInArticle;

	// word #'s (-1 means invalid)
	int32_t m_titleStart;
	int32_t m_titleEnd;
	int32_t m_titleStartAlnumPos;

	int32_t m_numVotes;

	// these are 1-1 with the Words::m_words[] array
	class Section **m_sectionPtrs;
	class Section **m_sectionPtrsEnd;

	// save this too
	int32_t m_nw ;

	// new stuff
	HashTableX m_ot;
	HashTableX m_vt;

	// for caching parition scores
	HashTableX m_ct;

	// buf for serializing m_osvt into
	char *m_buf;
	int32_t  m_bufSize;


	// buf for serializing m_nsvt into
	char *m_buf2;
	int32_t  m_bufSize2;

	// allocate m_sections[] buffer
	class Section  *m_sections;
	//int32_t            m_sectionsBufSize;
	int32_t            m_numSections;
	int32_t            m_maxNumSections;

	// this holds the Sections instances in a growable array
	SafeBuf m_sectionBuf;

	// this holds ptrs to sections 1-1 with words array, so we can
	// see what section a word is in.
	SafeBuf m_sectionPtrBuf;

	int32_t m_numSentenceSections;

	bool m_firstDateValid;

	// . the section ptrs sorted by Section::m_a
	// . since we set SEC_FAKE from splitSections() those new sections
	//   are appended on m_sections[] array and are out of order, so
	//   we merge sort the two sublists of m_sections[] and put the
	//   pointers into here...
	//class Section **m_sorted;

	bool m_isTestColl;

	// assume no malloc
	bool  m_needsFree;
	char  m_localBuf [ SECTIONS_LOCALBUFSIZE ];

	// set a flag
	bool  m_badHtml;

	int64_t  *m_wids;
	int64_t  *m_pids;
	int32_t       *m_wlens;
	char      **m_wptrs;
	nodeid_t   *m_tids;

	//int32_t addImpliedSections  ( bool needHR );
	//int32_t addHeaderImpliedSections ( );

	//int32_t addImpliedSectionsOld ( );
	//int32_t getHeadingScore ( class Section *sk , int32_t baseHash );

	// the new way
	bool addImpliedSections ( class Addresses *aa );//, HashTableX *svt );
	//HashTableX *m_svt;

	bool setSentFlagsPart1 ( );
	bool setSentFlagsPart2 ( );
	sentflags_t getSentEventEndingOrBeginningFlags ( sentflags_t sflags ,
							 int32_t senta ,
							 int32_t sentb ,
							 int32_t alnumCount) ;
	void setSentPrettyFlag ( class Section *si ) ;
	Addresses *m_aa;
	int32_t       m_hiPos;
	bool       m_sentFlagsAreSet;
	bool       m_addedImpliedSections;

	void setAddrXors ( class Addresses *aa ) ;

	int32_t addImpliedSections3 ();
	int32_t getDelimScore ( class Section *bro,
			     char method,
			     class Section *delim ,
			     class Partition *part );
	int32_t getDelimHash ( char method , class Section *bro ,
			    class Section *head ) ;
	//int32_t m_totalHdrCount;
	//bool m_called;

	bool addImpliedLists ( ) ;
	int32_t getDelimScore2 ( class Section *bro,
			      char method,
			      class Section *delim ,
			      int32_t *a ,
			      int32_t *b );

	bool hashSentBits ( class Section    *sx        ,
			    class HashTableX *vht       ,
			    class Section    *container ,
			    uint32_t          mod       ,
			    class HashTableX *labelTable,
			    char             *modLabel  );

	bool hashSentPairs ( Section    *sx ,
			     Section    *sb ,
			     HashTableX *vht ,
			     Section    *container ,
			     HashTableX *labelTable );

	bool addSentenceSections ( ) ;

	class Section *insertSubSection ( class Section *parent , 
					  int32_t a , 
					  int32_t b ,
					  int32_t newBaseHash ) ;

	int32_t splitSectionsByTag ( nodeid_t tagid ) ;
	bool splitSections ( char *delimeter , int32_t dh );

	class Section *m_rootSection; // the first section, aka m_firstSection
	class Section *m_lastSection;

	class Section *m_lastAdded;

	// kinda like m_rootSection, the first sentence section that occurs
	// in the document, is NULL iff no sentences in document
	class Section *m_firstSent;
	class Section *m_lastSent;

	bool containsTagId ( class Section *si, nodeid_t tagId ) ;

	bool isTagDelimeter ( class Section *si , nodeid_t tagId ) ;
	
	bool isDelimeter ( int32_t i , char *delimeter , int32_t *delimEnd ) {

		// . HACK: special case when delimeter is 0x01 
		// . that means we are back-to-back br tags
		if ( delimeter == (char *)0x01 ) {
			// must be a br tag
			if ( m_tids[i] != TAG_BR ) return false;
			// assume that
			int32_t k = i + 1;
			// bad if end
			if ( k >= m_nw ) return false;
			// bad if a wid
			if ( m_wids[k] ) return false;
			// inc if punct
			if ( ! m_tids[k] ) k++;
			// bad if end
			if ( k >= m_nw ) return false;
			// must be another br tag
			if ( m_tids[k] != TAG_BR ) return false;
			// mark as end i guess
			*delimEnd = k + 1;
			return true;
		}

		// no word is a delimeter
		if ( m_wids[i] ) return false;
		// tags "<hr" and "<br"
		if ( m_wptrs[i][0] == delimeter[0] &&
		     m_wptrs[i][1] == delimeter[1] &&
		     m_wptrs[i][2] == delimeter[2] )
			return true;
		// if no match above, forget it
		if ( m_tids[i] ) return false;
		// otherwise, we are a punctuation "word"
		// the bullet is 3 bytes long
		if ( m_wlens[i] < 3 ) return false;
		// if not a bullet, skip it (&bull)
		char *p    = m_wptrs[i];
		char *pend = p + m_wlens[i];
		for ( ; p < pend ; p++ ) {
			if ( p[0] != delimeter[0] ) continue;
			if ( p[1] != delimeter[1] ) continue;
			if ( p[2] != delimeter[2] ) continue;
			return true;
		}
		return false;
	};
		

};

// convert sectionType to a string
char *getSectionTypeAsStr ( int32_t sectionType );

// hash of the last 3 parent tagids
//uint32_t getSectionContentTagHash3 ( class Section *sn ) ;

// only allow this many urls per site to add sectiondb info
#define MAX_SITE_VOTERS 32

// . the key in sectiondb is basically the Section::m_tagHash 
//   (with a docId) and the data portion of the Rdb record is this SectionVote
// . the Sections::m_nsvt and m_osvt hash tables contain SectionVotes
//   as their data value and use an tagHash key as well
class SectionVote {
public:
	// . seems like addVote*() always uses a score of 1.0
	// . seems to be a weight used when setting Section::m_votesFor[Not]Dup
	// . not sure if we really use this now
	float m_score;
	// . how many times does this tagHash occur in this doc?
	// . this eliminates the need for the SV_UNIQUE section type
	// . this is not used for tags of type contenthash or taghash
	// . seems like pastdate and futuredate and eurdatefmt 
	//   are the only vote types that actually really use this...
	float m_numSampled;
};


class SectionVotingTable {
 public:

	SectionVotingTable ( ) ;

	//bool set ( Sections *sections , class RdbList *sectiondbList );
	void reset () { m_svt.reset(); }

	bool print ( SafeBuf *sbuf , char *title ) ;

	// stock table from a sectiondb rdblist
	bool addListOfVotes ( RdbList *list, 
			      key128_t **lastKey ,
			      uint32_t tagPairHash ,
			      int64_t docId ,
			      int32_t niceness ) ;

	// index our sections as flag|tagHash pairs using a termId which
	// is basically our sitehash. this allows us to "vote" on what
	// sections are static, dynamic, "texty" by indexing our votes into
	// datedb.
	bool hash ( int64_t docId , 
		    class HashTableX *dt , 
		    uint64_t siteHash64 ,
		    int32_t niceness ) ;


	bool addVote1 ( Section *sn , int32_t sectionType , float score ) {
		if ( ! sn ) return true;
		return addVote3 ( sn->m_tagHash,sectionType,score,1);};

	bool addVote2 ( int32_t tagHash, int32_t sectionType , float score ) {
		return addVote3 ( tagHash,sectionType,score,1);};

	bool addVote3 ( //class HashTableX *ttt         ,
		       int32_t              tagHash     ,
		       int32_t              sectionType ,
		       float             score       ,
		       float             numSampled  ,
		       bool              hackFix = false ) ;

	// return -1.0 if no voters!
	float getScore      ( Section *sn , int32_t sectionType ) {
		if ( ! sn ) return -1.0;
		return getScore ( sn->m_tagHash , sectionType ); };

	float getScore      ( int32_t tagHash , int32_t sectionType ) ;


	float getNumSampled ( Section *sn , int32_t sectionType ) {
		if ( ! sn ) return 0.0;
		return getNumSampled ( sn->m_tagHash , sectionType ); };

	float getNumSampled ( int32_t tagHash , int32_t sectionType ) ;

	int32_t getNumVotes ( ) { return m_svt.getNumSlotsUsed(); };

	bool init ( int32_t numSlots , char *name , int32_t niceness ) {
		return m_svt.set(8,sizeof(SectionVote),numSlots,
				 NULL,0,false,niceness,name); };

	HashTableX m_svt;

	int32_t m_totalSiteVoters;
	//int32_t m_totalSimilarLayouts;
};


//
// BEGIN SECTION TYPES
//

// . these are the core section types
// . these are not to be confused with the section bit flags below
// . we put these into sectiondb in the form of a SectionVote
// . the SectionVote is the data portion of the rdb record, and the key
//   of the rdb record contains the url site hash and the section m_tagHash
// . in this way, a page can vote on what type of section a tag hash describes
//#define SV_TEXTY          1 // section has mostly non-hypertext words
#define SV_CLOCK          2 // DateParse2.cpp. section contains a clock
#define SV_EURDATEFMT     3 // DateParse2.cpp. contains european date fmt
#define SV_EVENT          4 // used in Events.cpp to indicate event container
#define SV_ADDRESS        5 // used in Events.cpp to indicate address container
// . place types here
// . these #define's are used for values of Place::m_type in Events.cpp too!
// . score is from 0 to 1.0 which is probability section is a place container
//   for the specified place type
// . used by Events.cpp for address extraction
/*
#define SV_PLACE_NAME_1   7 // places now have two names
#define SV_PLACE_NAME_2   8 // places now have two names
#define SV_PLACE_STREET   9
#define SV_PLACE_CITY    10
#define SV_PLACE_ZIP     11
#define SV_PLACE_SUITE   12
#define SV_PLACE_ADM1    13
#define SV_PLACE_ADM2    14
#define SV_PLACE_ADM3    15
#define SV_PLACE_ADM4    16
#define SV_PLACE_CTRY    17
#define SV_PLACE_SCH     18
#define SV_PLACE_PRK     19
*/
// . HACK: the "date" is not the enum tag hash, but is the tagPairHash for this
// . every doc has just one of these describing the entire layout of the page
// . basically looking for these is same as doing a gbtaghash: query
#define SV_TAGPAIRHASH   20 
// . HACK: the "date" is not the enum tag hash, but is the contentHash!
// . this allows us to detect a duplicate section even though the layout
//   of the web page is not quite the same, but is from the same site
#define SV_TAGCONTENTHASH   21 
// . HACK: a statistic
// . the voter that had the max SectionVote::m_numSampled
// . the m_numSampled for this statistic is his m_numSampled
// . if we find that a section is not unique (i.e. repeated) on just one
//   voting document, then we think it is probably a comment and we do not
//   set the SEC_ARTICLE flag for that section
//#define SV_TEXTY_MAX_SAMPLED  22
// . HACK: the "date" is not the enum tag hash, but is the tagPairHash!
// . indicates this doc is waiting in line for enough docs from its site
//   with the same page layout (tagpairhash) to become indexed so that it can
//   make an informed decision in regards to eliminating comment sections
//   and determining article sections
//#define SV_WAITINLINE    23
// now Dates.cpp sets these too
#define SV_FUTURE_DATE   24
#define SV_PAST_DATE     25
#define SV_CURRENT_DATE  26
//#define SV_DUP           27
//#define SV_NOT_DUP       28
#define SV_SITE_VOTER    29
#define SV_TURKTAGHASH   30

#endif
