#ifndef _DATES_H_
#define _DATES_H_

#include "gb-include.h"
#include "XmlNode.h" // nodeid_t
#include "Bits.h"
#include "HashTableX.h"

// now address uses these
time_t getYearMonthStart ( long y , long m ) ;
time_t getDOWStart ( long y , long m, long dowArg, long count ) ;
long getNumDaysInMonth ( long month , long year ) ; // leap year?
// dow is 0 to 6
char *getDOWName ( long dow ) ;
// month is 0 to 11
char *getMonthName ( long month ) ;


typedef long long dateflags_t;

// . values for Date::m_flags
// . these are of type dateflags_t
// . pubdate flags
// . is it a clock?
#define DF_CLOCK               0x0000000001
#define DF_NOTCLOCK            0x0000000002
// . is it > 25 hrs in the future
#define DF_FUTURE              0x0000000004
// this means we do not have a year, like "9/16" (year taken from spider date)
//#define DF_NOYEAR              0x0000000008
// where we got the date from
#define DF_FROM_RSSINLINK      0x0000000010
// is this a "modified date"? that means we could not find a valid pub
// date on the page or from rss info, but it changed significantly since the
// last time we spidered it, so we make a guess at the pub date.
#define DF_ESTIMATED           0x0000000020
// where we got the date from
#define DF_FROM_BODY           0x0000000040
#define DF_FROM_URL            0x0000000080
#define DF_FROM_RSSINLINKLOCAL 0x0000000100
#define DF_FROM_META           0x0000000200
#define DF_UNIQUETAGHASH       0x0000000400
// could it be american or european format?
#define DF_AMBIGUOUS           0x0000000800
#define DF_AMERICAN            0x0000001000
#define DF_EUROPEAN            0x0000002000
// . set if date is a bad format or we have an unknown date format
// . format can be "american" or "european" for a document
//#define DF_INHYPERLINK         0x0000004000
#define DF_ONGOING             0x0000004000
#define DF_MONTH_NUMERIC       0x0000008000
#define DF_REPEATTAGHASH       0x0000010000
#define DF_NOTIMEOFDAY         0x0000020000
// an explicitly specified time for the event which overrides all (facebook)
#define DF_OFFICIAL            0x0000040000
#define DF_STORE_HOURS         0x0000080000

// is it like "Tuesday at 7:30pm" but when we telescope up to find more
// dates, the next bunch of dor "Tuesdays"
#define DF_INBADTAG            0x0000100000
#define DF_BEFORE1970          0x0000200000
#define DF_CANONICAL           0x0000400000
#define DF_MATCHESURLDAY       0x0000800000
#define DF_MATCHESURLMONTH     0x0001000000
#define DF_MATCHESURLYEAR      0x0002000000
#define DF_IN_HYPERLINK        0x0004000000
#define DF_NONEVENT_DATE       0x0008000000

#define DF_FUZZY               0x0010000000
#define DF_LEFT_BOOKEND        0x0020000000
#define DF_RIGHT_BOOKEND       0x0040000000
#define DF_ASSUMED_YEAR        0x0080000000

#define DF_USEDASHEADER        0x0100000000LL
#define DF_INVALID             0x0200000000LL
#define DF_HARD_LEFT           0x0400000000LL
#define DF_HARD_RIGHT          0x0800000000LL

#define DF_COPYRIGHT           0x1000000000LL
#define DF_CLOSE_DATE          0x2000000000LL
// "doc last modified: "
#define DF_PUB_DATE            0x4000000000LL
#define DF_KITCHEN_HOURS       0x8000000000LL

#define DF_IN_LIST             0x0000010000000000LL
#define DF_DUP                 0x0000020000000000LL
#define DF_SUB_DATE            0x0000040000000000LL
#define DF_HAS_STRONG_DOW      0x0000080000000000LL
#define DF_HAS_WEAK_DOW        0x0000100000000000LL
#define DF_AFTER_TOD           0x0000200000000000LL
#define DF_BEFORE_TOD          0x0000400000000000LL
#define DF_EXACT_TOD           0x0000800000000000LL
#define DF_EVENT_CANDIDATE     0x0001000000000000LL
#define DF_ONOTHERPAGE         0x0002000000000000LL
#define DF_WEEKLY_SCHEDULE     0x0004000000000000LL
#define DF_REGISTRATION        0x0008000000000000LL
#define DF_SCHEDULECAND        0x0010000000000000LL
#define DF_HAS_ISOLATED_DAYNUM 0x0020000000000000LL
#define DF_IN_CALENDAR         0x0040000000000000LL
#define DF_REDUNDANT           0x0080000000000000LL
#define DF_NOTKILLED           0x0100000000000000LL
#define DF_YEAR_UNKNOWN        0x0200000000000000LL
// for dates that telescope to store hours and have no specific daynum
// or list of daynums... range of daynums is ok
#define DF_SUBSTORE_HOURS      0x0400000000000000LL
#define DF_FIRST_IN_LIST       0x0800000000000000LL
#define DF_TIGHT               0x1000000000000000LL
#define DF_INCRAZYTABLE        0x2000000000000000LL
#define DF_TABLEDATEHEADERROW  0x4000000000000000LL
#define DF_TABLEDATEHEADERCOL  0x8000000000000000LL

//
// values for Date::m_flags5
//
#define DF5_IGNORE 0x0000000000000001LL


// . returns the timestamp in seconds since the epoch
// . returns 0 if no date found in the url itself
long parseDateFromUrl ( char *url             ,
			long *urlYear  = NULL ,
			long *urlMonth = NULL ,
			long *urlDay   = NULL );


// values for Date::m_type
#define DT_TOD             0x00000001 // (1:30pm utc,one to three am gmt)
#define DT_DAYNUM          0x00000002 // (23rd,25,sixteenth)
#define DT_MONTH           0x00000004 // (nov,11)
#define DT_YEAR            0x00000008 // (2009,09)
#define DT_DOW             0x00000010 // Day Of Week (monday,tues,...)
#define DT_HOLIDAY         0x00000080 
#define DT_TIMESTAMP       0x00000100 
//#define DT_MOD           0x00000200 // first second last
#define DT_RANGE           0x00000400 
#define DT_LIST_OTHER      0x00000800
#define DT_COMPOUND        0x00001000
#define DT_TELESCOPE       0x00002000

// range types
#define DT_RANGE_TOD       0x00004000
#define DT_RANGE_DOW       0x00008000
#define DT_RANGE_MONTHDAY  0x00010000
#define DT_RANGE_DAYNUM    0x00020000

#define DT_LIST_DAYNUM     0x00040000
#define DT_LIST_MONTHDAY   0x00080000
#define DT_LIST_TOD        0x00100000
#define DT_LIST_DOW        0x00200000
#define DT_LIST_MONTH      0x00400000
#define DT_RANGE_TIMEPOINT 0x00800000

#define DT_SUBDAY          0x01000000 // night|nights|evening|mornings|afternoo
#define DT_SUBWEEK         0x02000000 // weekend,weekdays,weekends
#define DT_SUBMONTH        0x04000000 // lastdayofmonth,lastweekofmonth,...
#define DT_EVERY_DAY       0x08000000 // 7daysaweek,everyday,...
#define DT_SEASON          0x10000000 // summer,winters,spring,fall,autumn
#define DT_ALL_HOLIDAYS    0x20000000 // "holidays"
#define DT_RANGE_YEAR      0x40000000 // 2010-11
#define DT_RANGE_MONTH     0x80000000

#define DT_RANGE_ANY      (DT_RANGE|DT_RANGE_TOD|DT_RANGE_DOW|DT_RANGE_MONTHDAY|DT_RANGE_DAYNUM|DT_RANGE_TIMEPOINT|DT_RANGE_YEAR|DT_RANGE_MONTH)
#define DT_LIST_ANY      (DT_LIST_OTHER|DT_LIST_DAYNUM|DT_LIST_MONTHDAY|DT_LIST_TOD|DT_LIST_DOW|DT_LIST_MONTH)
#define DT_SPECIAL_TYPES (DT_HOLIDAY|DT_SUBDAY|DT_SUBWEEK|DT_SUBMONTH|DT_EVERY_DAY|DT_SEASON|DT_ALL_HOLIDAYS)

// . flags type
// . plenty of room for growth, 32 bits
typedef uint32_t datetype_t;

typedef uint32_t suppflags_t;

// these are just for DOWs now...
#define SF_PLURAL         0x000001
#define SF_FIRST          0x000002
#define SF_SECOND         0x000004
#define SF_THIRD          0x000008
#define SF_FOURTH         0x000010
#define SF_FIFTH          0x000020
#define SF_LAST           0x000040
#define SF_NON_FUZZY      0x000080
// did a time of day have an am/pm indicator or not?
#define SF_HAD_AMPM       0x000100
#define SF_NIGHT          0x000200
#define SF_AFTERNOON      0x000400
#define SF_MORNING        0x000800
#define SF_HAD_MINUTE     0x001000 // a TOD with a minute?
#define SF_NON            0x002000
#define SF_MID            0x004000
//#define SF_HOLIDAY_WORD 0x4000
#define SF_PM_BY_LIST     0x008000
//#define SF_NORMAL_HOLIDAY 0x010000
#define SF_RECURRING_DOW  0x020000
#define SF_EVERY          0x040000
#define SF_MILITARY_TIME  0x080000
#define SF_IMPLIED_AMPM   0x100000
#define SF_ON_PRECEEDS    0x200000
#define SF_SPECIAL_TOD    0x400000

long getDOW  ( time_t t );
long getYear ( time_t t );

bool isTicketDate ( long a , long b , long long *wids , Bits *bits ,
		    long niceness ) ;

class Date {

public:

	// word range relative to m_words Words.cpp class
	long  m_a;
	long  m_b;

	// used by Events.cpp for event titles algo
	long m_maxa;
	long m_mina;

	// the types of Dates: (see #defines above)
	// there are 8 bit flags. but only one bit is allowed to be set
	// unless (m_flags & DF_COMPOUND) is true
	datetype_t m_type;

	// descriptor bits (see #defines above)
	dateflags_t  m_flags;

	// we need more than 64 flags now!
	dateflags_t m_flags5;

	// types contained by this date
	datetype_t m_hasType;

	// modifiers to what we hold
	suppflags_t m_suppFlags;

	// the numeric value of what we represent
	long m_num;

	// . these two guys are used by Dates::getDateElements()
	// . how many date elements we consist of
	long m_numFlatPtrs;
	// offset into Dates::m_cbuf of the list of those elements
	long m_flatPtrsBufOffset;
	// the Dates class that contains us
	class Dates *m_dates;

	// the date # as added. used to set m_tmph now
	unsigned long m_arrayNum;

	// HACK: for 5pm - 2am, we now truncate to midnight so that
	// "Saturday 5pm - 2am" does not have an interval that is really
	// considered Friday night
	long m_truncated;

	long m_penalty;
	long m_tagHash;
	long m_turkTagHash; // without tag attributes in the hash (xcpt class)
	long m_dateTypeAndTagHash32;
	long m_occNum;
	long m_clockHash;

	// if we are in a table, this is the table cell section which
	// has m_headColSection and m_colNum, etc. set
	class Section *m_tableCell;

	//class Section *m_headColSection;
	//class Section *m_headRowSection;

	// for use by DT_COMPOUND types
	char   m_month;
	long   m_year;
	char   m_dayNum;
	// 1 through 7 = Sunday through Saturday
	char   m_dow;
	long   m_tod;
	time_t m_timestamp;

	// for setting dowBits in Dates.cpp
	//char m_minDow;
	//char m_maxDow;
	char m_dowBits;

	long m_minYear;
	long m_maxYear;

	char m_minDayNum;
	char m_maxDayNum;

	// in seconds
	long m_minTod;
	long m_maxTod;

	// . min pub date of the page that contains us
	// . see Dates.cpp or XmlDoc.cpp for an explanation of this
	// . this is taken from SpiderRequest::m_parentPrevSpiderTime
	//time_t m_minPubDate;

	// sometimes an event date does not have a year, so we try to guess
	// a range of years it could fall on. we look at the years of other
	// dates on the page and use those to make a range of years.
	//long m_minStartYear;
	//long m_maxStartYear;

	// we guess the max year of a date that needs a year and does not have
	// one, and we store the guess here
	long m_maxYearGuess;

	// we scan for the min/max years on page from all event dates
	// and then use that range to determine the year when other event dates
	// occur, provided they have a dow/month/daynum (but no year) then
	// we set this to that year.
	long m_dowBasedYear;

	// convert years into time_t's. truncate m_maxStartFocus based on
	// spideredTime.
	long m_minStartFocus;
	long m_maxStartFocus;

	// supplmenetal value for "first/second/fifth thursday"
	char m_supp;

	// do not telescope past this section
	//class Section *m_containingSection;

	// the smallest section containing word # m_a
	class Section *m_section;

	class Section *m_compoundSection;

	class Section *m_maxTODSection;

	class Section *m_calendarSection;

	class Date *m_lastDateInCalendar;

	// we telescope m_section up until we hit a non-br and breaking
	// section... i.e. a "hard" section
	class Section *m_hardSection;

	class Date *m_subdateOf;
	class Date *m_dupOf;

	// if we telescope, this guy essentially replaces us
	class Date *m_telescope;

	// what sentence number are we in? Dates.cpp uses this to disqualify
	// dates as headers if they are in the same sentence
	//long m_sentenceId;
	//long m_sentStart;
	//long m_sentEnd;

	void *m_used;

	long m_headerCount;

	unsigned long m_tmph;

	unsigned long m_ptrHash;

	// . try to normalize so that two dates that represent the exact
	//   same times will have the same m_dateHash
	// . i.e. "11am = 11:00 AM", "3/3/11 = March 3rd 2011"
	uint64_t m_dateHash64;

	unsigned long long m_norepeatKey ;
	long               m_norepeatResult ;

	// usually the date ptr containing the tod, but in the case of
	// burtstikilounge.com it is the daynum in that calendar layout.
	// this is set late in the game in Events.cpp.
	class Date *m_mostUniqueDatePtr;
	// used for the above algo for setting m_mostUnqiueDatePtr
	long m_usedCount;

	// kinda like m_mostUniqueDatePtr, but we dedup our telescope
	// components, using this as the base. part of normalization
	// and used in setDoNotPrintBits();
	//class Date *m_coreDate;

	// parent->m_ptrs[x] = this!
	class Date *m_dateParent;

	// used for re-sorting dates as part of printTextNorm() normalization
	long m_groupNum;

	// . this is used for COMPOUND dates
	// . this is also used for lists and ranges of basic dates
	// . leave this open-ended! so Dates::getMem() can alloc for the max
	//   but we may actually end up using less!
	long        m_numPtrs;
	class Date *m_ptrs[];

	void addPtr ( class Date *ptr , long i , class Dates *parent );

	void printText ( class SafeBuf *sb , class Words *words ,
			 bool inHtml = true ) ;

	void printText2 ( class SafeBuf *sb , class Words *words ,
			 bool inHtml = true ) ;

	bool printTextNorm ( class SafeBuf *sb , class Words *words ,
			     bool inHtml = true , class Event *ev = NULL ,
			     class SafeBuf *intbuf = NULL ) ;

	bool printTextNorm2 ( class SafeBuf *sb , class Words *words ,
			      bool inHtml = true , class Event *ev = NULL ,
			      class SafeBuf *intbuf = NULL ) ;


	void print ( class SafeBuf  *sb       ,
		     class Sections *ss       ,
		     class Words    *ww       ,
		     long            siteHash ,
		     long            num      ,
		     class Date     *best     ,
		     class Dates    *dates    );
	
	bool isSubDate ( class Date *di );

	bool addDoNotPrintDates ( class HashTableX *dnp );
	bool addDoNotPrintRecursive ( datetype_t dt , class HashTableX *dnp ) ;
	//long getTextOffset ( long num , long *retEndOff, class Words *words);

	// . is part of our compound date in this section?
	// . flag which date types are in "si" and return that
	// . used by Events.cpp to set EventDesc::m_flags so we
	//   can show that in the summary on the search results
	//   page.
	//datetype_t getDateTypesInSection ( class Section *si );

	//bool printNormalized2 ( class SafeBuf *sb , long nicess ,
	//			class Words *words );
};


// used by Dates::hashStartTimes() and Dates::getIntervals()
class Interval {
public:
	time_t m_a;
	time_t m_b;
};


//#define MAX_DATE_PTRS 8000

#define  MAX_POOLS 100

class Dates {

public:

	Dates ();
	~Dates ();

	long getStoredSize ( );
	static long getStoredSize ( char *p   );
	long serialize     ( char *buf ); 
	long deserialize   ( char *buf ); 

	void reset();

	// . returns false if blocks, returns true otherwise
	// . returns true and sets g_errno on error
	// . if the content has changed a lot since last time we spidered
	//   it, then we will add "modified dates" to the list of pub date
	//   candidates. the DF_ESTIMATED flag will be set for those, and
	//   the low bit of such pub dates will be cleared. the low bit
	//   will be set on pub dates that are not estimated.
	bool setPart1 ( Url *url ,//char             *url             ,
			Url *redirUrl, // char             *redirUrl        ,
			uint8_t           contentType     ,
			long              ip              ,
			long long         docId           ,
			long              siteHash        ,
			class Xml        *xml             ,
			class Words      *words           ,	
			class Bits       *bits            ,	
			class Sections   *sections        ,
			class LinkInfo   *info1           ,
			// . old title rec and xml and words
			// . parsed up because we had to for adding
			//   deltas to indexdb
			//class Dates      *odp             ,
			HashTableX       *cct             , // replaces "odp"
			class XmlDoc     *nd              , // new XmlDoc
			class XmlDoc     *od              , // old XmlDoc
			char             *coll            ,
			long              niceness        );

	bool addVotes ( class SectionVotingTable *nsvt ) ;

	bool hasKitchenHours ( class Section *si ) ;
	//bool isTicketDate ( long a , long b , long long *wids ) ;
	bool isFuneralDate ( long a , long b ) ;
	bool isCloseHeader ( class Section *si ) ;

	bool setPart2 ( class Addresses *aa ,
			long minPubDate ,
			long maxPubDate ,
			// the old one - we read from that
			//class SectionVotingTable *osvt ,
			bool isXml ,
			bool isSiteRoot ) ;

	bool getIntervals2 ( Date *dp , 
			     SafeBuf *sb, 
			     long year0 , 
			     long year1,
			     Date **closeDates ,
			     long  numCloseDates ,
			     char  timeZone ,
			     char  useDST ,
			     class Words *words ) ;

	long addIntervals ( class Date *di , char hflag , Interval *int3 ,
			    long depth , class Date *orig );
	long addIntervalsB ( class Date *di , char hflag , Interval *int3 ,
			     long depth , class Date *orig );
	bool addInterval  ( long a , long b , Interval *int3 , long *ni3 ,
			    long depth , bool useDayShift = true ) ;

	bool addIntervalsForDOW ( long      num    ,
				  class Interval *int3 ,
				  long     *ni3    ,
				  long      depth  ,
				  long      year   ) ;


	long intersect ( Interval *int1 ,
			 Interval *int2 ,
			 Interval *int3 ,
			 long      ni1  ,
			 long      ni2  ,
			 long      depth );
	long intersect2 ( Interval *int1 ,
			  Interval *int2 ,
			  Interval *int3 ,
			  long      ni1  ,
			  long      ni2  ,
			  long      depth );
	long intersect3 ( Interval *int1 ,
			  Interval *int2 ,
			  Interval *int3 ,
			  long      ni1  ,
			  long      ni2  ,
			  long      depth ,
			  bool      subtractint2 ,
			  bool      unionOp );

	//time_t getYearMonthStart ( long y , long m );

	// 4th monday of May 2009, for instance, use a dowArg of 2 (monday)
	// and a count of 4. returns a time_t
	//time_t getDOWStart     ( long y , long m , long dowArg , long count);


	datetype_t getDateType ( long i , long *val , long *endWord ,
				 long long *wids , long nw ,
				 bool onPreceeds ) ;

	bool addRanges ( class Words *words ,
			 bool allowOpenEndedRanges = true ) ;

	//void addOpenEndedRanges ( ) ;

	bool addLists  ( class Words *words ,
			 bool ignoreBreakingTags ) ;

	bool makeCompounds ( class Words *words , 
			     bool monthDayOnly ,
			     bool linkDatesInSameSentence , // = false ,
			     //bool dowTodOnly              , // = false );
			     bool ignoreBreakingTags ); // = false

	class Date *getMem ( long need );


	class Date *addDate ( datetype_t  dt  , // DT_*
			      dateflags_t tf  , // flags
			      long        a   ,
			      long        b   ,
			      long        num );   // data
	
	// . must call set() above before calling this
	// . mdw left off here
	long getPubDate ( ) {
		return m_pubDate;
		//if ( ! m_best ) return -1;
		//if ( m_best->m_timestamp <= 0 ) {char*xx=NULL;*xx=0;}
		//return m_best->m_timestamp;
	};

	long getRSSPublishDate ( class Inlink *k ) ;

	// returns -1 and sets g_errno on error
	long isCompatible ( class Date *di, 
			    class Date *dp , 
			    class HashTableX *ht ,
			    class Date *DD ,
			    bool  *hasMultipleHeaders );

	// returns -1 and sets g_errno on error
	long isCompatible2 ( Section *s1 , 
			     Section *s2 , bool useXors );

	//class Date *getFirstParentOfType( class Date *dd, 
	//				  class Date *last ,
	//				  class HashTableX *ht );

	// XmlDoc::hash() calls this to index the Dates stored in the
	// TitleRec. pages from the same site can use these special termlists
	// to see if their tag hashes are likely indicative of a clock or not
	bool hash ( long long         docId ,
		    class HashTableX *tt    ,
		    class XmlDoc     *xd    );

	bool checkPunct ( long i , class Words *words , char *singleChar );

	// returns false and sets g_errno on error
	bool parseDates ( class Words *w , dateflags_t defFlags ,
			  class Bits *bits ,
			  class Sections *sections ,
			  long niceness ,
			  Url *url ,
			  uint8_t contentType );
	
	bool m_bodySet ;

	Date **getDateElements ( class Date *di, long *ne );
	bool addPtrToArray ( class Date *dp );
	SafeBuf m_cbuf;

	long getDateNum ( class Date *di ) ;
	long printDateNeighborhood ( class Date *di , class Words *w ) ;
	bool printDates ( class SafeBuf *sb ) ;
	long printDates2 ( ) ;
	// gdb can call this one:
	long print ( class Date *d );

	bool getDateOffsets ( Date *date ,
			      long  num , 
			      long *dateStartOff ,
			      long *dateEndOff   ,
			      long *dateSentStartOff ,
			      long *dateSentEndOff ) ;

	// returns false and sets g_errno on error
	long parseTimeOfDay3 ( class Words *w                 ,
			       long         i                 ,
			       long         niceness          , 
			       long        *endWordNum        ,
			       class TimeZone **tzPtr         ,
			       bool             monthPreceeds ,
			       bool            *hadAmPM       ,
			       bool            *hadMinute     ,
			       bool            *isMilitary    ) ;

	void setEventBrotherBits();

	void setDateParents ( ) ;
	void setDateParentsRecursive ( class Date *di , class Date *parent ) ;

	void setDateHashes ( ) ;
	uint64_t getDateHash  ( class Date *di , class Date *orig ); 
	uint64_t getDateHash2 ( class Date *di , class Date *orig ); 

	void setStoreHours ( bool telescopesOnly );


	void setMaxYearGuesses ( ) ;
	long guessMaxYear ( long i ) ;
	long calculateYearBasedOnDOW ( long minYear, long maxYear, 
				       class Date *di );

	//bool printNormalized1 ( class SafeBuf *sb ,  
	//			class Event *ev , 
	//			long niceness ) ;

	Date **m_datePtrs;// [ MAX_DATE_PTRS ];
	long   m_numDatePtrs;

	// just like m_datePtrs[] but we do not NULL out any entries
	// just because they were used to make a compound, list or range date
	Date **m_totalPtrs;// [ MAX_DATE_PTRS ];
	long   m_numTotalPtrs;
	
	// we now (re)alloc these on demand as well
	long   m_maxDatePtrs;

	bool   m_overflowed;
	bool   m_dateFormatPanic;
	bool   m_calledParseDates;

	long m_shiftDay;

	// memory pools for holding Dates and/or Date::m_ptrs lists
	char *m_pools[MAX_POOLS];
	long  m_numPools;
	//long   m_numDates;

	char *m_coll;
	//char *m_url;
	//char *m_redirUrl;
	Url *m_url;
	Url *m_redirUrl;
	long  m_siteHash;
	// the old xmldoc, NULL if did not exist
	class XmlDoc *m_od;

	char *m_current;
	char *m_currentEnd;

	//long  m_now;
	//bool  m_canHash;
	//long  m_besti;

	// the defacto pubdate
	class Date *m_best;
	time_t m_pubDate;

	//wbit_t *m_bits;
	class Bits *m_bits;
	long  m_niceness;
	dateflags_t m_dateFormat ;
	//bool  m_gotDateFormatFromDisk ;
	//long  m_urlDate    ;
	//long  m_urlDateNum ;
	long  m_urlMonth   ;
	long  m_urlYear    ;
	long  m_urlDay     ;
	long  m_firstGood  ;
	long  m_lastGood   ;
	// the new xml doc, used for XmlDoc::m_spideredTime
	class XmlDoc *m_nd;

	class Words    *m_words;
	char          **m_wptrs;
	long           *m_wlens;
	long long      *m_wids;
	nodeid_t       *m_tids;
	long            m_nw;
	class Sections *m_sections;
	long long       m_docId;
	long            m_spiderTime;

	class Addresses *m_addresses;

	// . how much we have changed from the last time spidered
	// . is a percentage and ranges from 0 to 100
	// . will be 0 if first time spidered
	long  m_changed;

	// like javascript, gif, jpeg, xml, html, etc.
	uint8_t m_contentType;

	// timeStruct breakdown of the XmlDoc::m_spideredTime (newDoc/nd)
	struct tm *m_spts;

	bool  m_badHtml;
	bool  m_needQuickRespider;

	long m_year0;
	long m_year1;

	class HashTableX *getSubfieldTable();
	class HashTableX *getTODTable  () { return &m_tt; };
	class HashTableX *getTODNumTable  () { return &m_tnt; };
	void setPhoneXors ();
	void setEmailXors ();
	void setPriceXors ();
	void setTODXors   ();
	void setDayXors   ();
	void setAddrXors  ();
	bool m_phoneXorsValid;
	bool m_emailXorsValid;
	bool m_todXorsValid ;
	bool m_dayXorsValid ;
	bool m_priceXorsValid;
	bool m_ttValid;
	bool m_tntValid;
	bool m_sftValid;
	bool m_dateBitsValid;
	bool m_doNotPrintBitsValid;
	HashTableX m_tt;
	HashTableX m_tnt;
	HashTableX m_sft;
	// map sectionPtr to array of up to 64 bits. each bit represents
	// a field name that is duplicated in the document, and that that
	// section contains.
	HashTableX m_bitTable;
	long       m_numLongs;
	//class SectionVotingTable *m_osvt;
	HashTableX *m_rvt;
	bool m_setDateHashes;
	bool m_isXml      ;
	bool m_isSiteRoot ;
};

// now time zones
struct TimeZone {
	char m_name[16];
	// tzinfo:
        long m_hourMod;
        long m_minMod;
        long m_modType;
};

// . returns how many words starting at i are in the time zone
// . 0 means not a timezone
long getTimeZoneWord ( long i , long long *wids , long nw , 
		       TimeZone **tzptr , long niceness );

bool isDateType ( long long *pwid ) ;

// returns false and sets g_errno on error
bool getMonth ( long long wid , long *retMonth ) ;

void resetDateTables ( );

uint32_t getDateSectionHash ( class Section *sn );

extern char s_numDaysInMonth[];

#endif
