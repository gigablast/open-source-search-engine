#ifndef _EVENTS_H_
#define _EVENTS_H_

#include "gb-include.h"
#include "SafeBuf.h"
#include "Bits.h" // wbit_t
#include "HashTableX.h"
#include "Sections.h"

void printEventIds ( char *evs , long esize , uint8_t *bits ) ;

typedef uint64_t evflags_t;
//typedef uint32_t evflags_t;

#define EV_MULT_LOCATIONS    0x0001 
#define EV_NO_LOCATION       0x0002
#define EV_UNVERIFIED_LOCATION  0x0004 // is contained by larger event section
#define EV_OLD_EVENT         0x0008 // event max date is in the past
#define EV_NO_YEAR           0x0010 // has a daynum but no year
#define EV_NOT_COMPATIBLE    0x0020
#define EV_EMPTY_TIMES       0x0040 // date intervals are empty set!
#define EV_OUTLINKED_TITLE   0x0080
#define EV_IS_POBOX          0x0100
#define EV_VENUE_DEFAULT     0x0200
#define EV_SAMEDAY           0x0400
#define EV_MISSING_LOCATION  0x0800
#define EV_REGISTRATION      0x1000
//#define EV_MENU              0x2000
#define EV_HASTITLEWITHCOLON 0x2000
#define EV_TICKET_PLACE      0x4000
// when one street could be in 2+ cities, etc. we set this
#define EV_AMBIGUOUS_LOCATION 0x008000
#define EV_SPECIAL_DUP        0x010000
//#define EV_BAD_STORE_HOURS  0x020000
#define EV_INCRAZYTABLE       0x020000
// are we store hours?
#define EV_STORE_HOURS        0x00040000
#define EV_DEDUPED            0x00080000
#define EV_ADCH32DUP          0x00100000
#define EV_HADNOTITLE         0x00200000
#define EV_DO_NOT_INDEX       0x00400000
#define EV_DESERIALIZED       0x00800000 // HACK: used by getEventDisplay()
#define EV_SUBSTORE_HOURS     0x01000000
#define EV_HASTITLEWORDS       0x02000000
#define EV_SENTSPANSMULTEVENTS 0x04000000
#define EV_HASTITLEFIELD       0x08000000
#define EV_HASTITLESUBSENT     0x10000000
#define EV_HASTITLEBYVOTES     0x20000000
#define EV_HASTIGHTDATE        0x40000000
#define EV_LONGDURATION        0x80000000

// now this is 64bits
#define EV_PRIVATE             0x0000000100000000LL
// from facebook xml?
#define EV_FACEBOOK            0x0000000200000000LL
// facebook flag <hide_guest_list>1</> ? in facebook fql xml
#define EV_HIDEGUESTLIST       0x0000000400000000LL
#define EV_LATLONADDRESS       0x0000000800000000LL
#define EV_STUBHUB             0x0000001000000000LL
#define EV_EVENTBRITE          0x0000002000000000LL

// i took EV_OUTLINKED_TITLE out of here because it causes us to lose
// a result if the turks force its title to an outlinked title, then the
// event disappears and so does their voting record of it. so just
// index gboutlinkedtitle:1 in Events::hash() so we can exclude them from
// the search results if you want.
// if your event flags are not "EV_BAD_EVENT" then the event will still be
// indexed. it might not qualify for gbresultset:1 however... but at least
// you can otherwise search for it.
#define EV_BAD_EVENT ( EV_IS_POBOX | EV_NOT_COMPATIBLE | EV_UNVERIFIED_LOCATION | EV_NO_LOCATION | EV_MULT_LOCATIONS | EV_NO_YEAR | EV_EMPTY_TIMES | EV_MISSING_LOCATION | EV_REGISTRATION | EV_TICKET_PLACE | EV_AMBIGUOUS_LOCATION | EV_SPECIAL_DUP | EV_ADCH32DUP|EV_HADNOTITLE | EV_SAMEDAY | EV_SENTSPANSMULTEVENTS )

#define MAX_CLOSE_DATES 15

bool printConfirmFlags ( class SafeBuf *sb , long confirmed  ) ;
bool printEventDescFlags ( SafeBuf *sb, long dflags ) ;
bool printEventFlags ( class SafeBuf *sb , evflags_t eventFlags );
bool printEventDisplays ( SafeBuf *sb ,
			  long numHashableEvents ,
			  char *ptr_eventData ,
			  long size_eventData ,
			  //char *ptr_eventTagbuf ,
			  char *ptr_utf8Contet );
bool cacheEventLatLons ( char     *ptr_eventsData  ,
			 long      size_eventsData ,
			 class RdbCache *latLonCache     ,
			 long      niceness        ) ;

// an event in the document
class Event {
 public:
	// used by Events.cpp. start of event. usually nearest pub date.
	// includes m_startHour already...
	//long m_startTime;
	// the title of the event, word #'s in Words class
	long m_titleStart;
	long m_titleEnd;
	float m_titleScore;
	class Section *m_titleSection;

	// the section containing the event
	class Section *m_section;
	// every event has a location given by the Address class
	class Address *m_address;

	// . the original street used by the event 
	// . m_address is the final alias or whatever and m_street is more
	//   like the original place indicator
	class Place *m_origPlace;

	// "Fred's Place" etc.
	class Place *m_bestVenueName;

	// for the bits #define'd above
	evflags_t m_flags;

	// . list of sections containing auxillary event descriptions
	// . basically when an event has multiple dates contained in 
	//   multiple sections, sharing some common event description,
	//   we need to telescope out to the event description
	// . <div0>santa fe playhouse presents a tuna christmas
	//   <div1>Performance Dates:
	//   <div2>first night $30 7pm dec 10th</div2>
	//   <div2>mondays: dec 12,19,jan3 6pm</div2>
	//   </div1></div0>


	// . for indexing into datedb 
	// . see Dates.cpp::hashStartDates() and Events::hash()
	long m_eventId;

	// what we store in the event db index
	long m_indexedEventId;

	class TagRec *m_tagRec;
	// offset into XmlDoc::m_eventTagRecsBuf, a SafeBuf. after we look
	// up all TagRecs for all events, then we set m_tagRec, otherwise
	// SafeBuf could grow and realloc and change the ptrs!
	//long          m_tagRecOffset;

	// turk votes now have their own adth32-%llu-<domain>.com tag rec
	class TagRec *m_turkVoteTagRec;

	// point to comma separated list of tag words/phrases referecing
	// into the buffer XmlDoc::ptr_tagBuf
	//char *m_tagsPtr;

	unsigned long long  m_addressHash64;
	// . this should be normalized as well
	// . hash mostly the date types and the numbers together
	// . use stuff like m_minTOD/m_maxTOD/etc. for hashing
	unsigned long long  m_dateHash64;
	// hash of the title, just the alnum words
	unsigned long long  m_titleHash64;
	// hash of all three
	unsigned long long  m_eventHash64;

	// tag hash of the most specific date
	unsigned long m_dateTagHash32;
	// tag hash for the tag containing the street or place name if no strt
	unsigned long m_addressTagHash32;
	// tag hash of tag containing the event title
	unsigned long m_titleTagHash32;

	// address/date content hash
	unsigned long m_adch32;
	// address/date tag hash
	unsigned long m_adth32;

	// hash of description, first 50 sentences in description
	unsigned long long m_descHash64;

	// . similar to event hash but also incorporates the description
	// . used to prevent indexing EXACTLY the same event on same pages
	//   of same site, or even different sites
	uint64_t m_dedupHash64;

	long m_numDescriptions;

	// turk voting info is compiled into these counts in 
	// XmlDoc::hashTagRecForEvents() so we can index turk terms for
	// each event
	//bool m_accepted;
	//bool m_rejected;
	bool m_turked; // turked at least once?
	bool m_turkedDirectly;
	bool m_turkedIndirectly;
	bool m_conflicted; // accepts = rejects > 0
	bool m_confirmedAccept;
	bool m_confirmedReject;
	bool m_confirmedError;
	bool m_confirmedTitle;
	bool m_confirmedVenue;
	long m_unconfirmedAccepts;
	long m_unconfirmedRejects;
	// how many turks directly turked this event?
	//long m_directVotes;

	unsigned long m_confirmedTitleContentHash32;
	unsigned long m_confirmedVenueContentHash32;

	// index into Events::m_dates->m_datePtrs[] of the first date that
	// belongs to this event section
	//long m_firstDatePtrNum;

	// the first TOD date we use
	class Date *m_date;

	//long m_eventCmpId;
	long m_tmp;

	// max interval start time of all intervals we got
	time_t m_maxStartTime;

	// . offset of our Intervals into a SafeBuf buffer
	// . these Interval classes are time_t ranges that are when the
	//   event is taking place. we have m_ni such Intervals.
	long m_intervalsOff;
	long m_ni;

	// dates when venue is closed
	class Date *m_closeDates[MAX_CLOSE_DATES];
	long        m_numCloseDates;
};

//#define EDIF_STORE_HOURS 0x01

#define MAX_ED_DATES 7

// we keep a blob of these in the "title rec" so we can return an event
// super fast, given a docId and an eventId for that docId
class EventDisplay {
public:
	long  m_indexedEventId;
	long  m_totalSize;

	//char *m_tagsPtr;

	// we now retain the event flags since we sometimes index bad
	// events now if they were turked directly, otherwise, we'd lose
	// the voting info.
	evflags_t m_eventFlags;

	// hash of normalized address
	unsigned long long  m_addressHash64;
	// . this should be normalized as well
	// . hash mostly the date types and the numbers together
	// . use stuff like m_minTOD/m_maxTOD/etc. for hashing
	unsigned long long  m_dateHash64;
	// hash of the title, just the alnum words
	unsigned long long  m_titleHash64;
	// hash of all three
	unsigned long long  m_eventHash64;

	// now hash of the date types in order? 
	// TODO: consider putting this into m_adth32
	//unsigned long m_dateTypeHash32;

	// tag hash of the most specific date
	unsigned long m_dateTagHash32;
	// tag hash for the tag containing the street or place name if no strt
	unsigned long m_addressTagHash32;
	// tag hash of tag containing the event title
	unsigned long m_titleTagHash32;

	// address/date content hash
	unsigned long m_adch32;
	// address/date tag hash
	unsigned long m_adth32;

	long  m_numDescriptions;

	// the lat/lon we lookedup from the geocoder. will be NO_LATITUDE
	// if is invalid.
	double m_geocoderLat;
	double m_geocoderLon;

	// sets CF_TITLE bit if its confirmed, etc.
	char m_confirmed;

	// for EDIF_STORE_HOURS bit
	//long  m_edflags;

	class EventDesc *m_desc;
	char            *m_addr;
	long            *m_int;
	char            *m_normDate;

	// these are in bytes
	long  m_descSize;
	long  m_addrSize;
	long  m_intSize;
	long  m_normDateSize;

	// byte offsets into the docStart of the compound date pieces
	long m_dateStarts[MAX_ED_DATES];
	long m_dateEnds  [MAX_ED_DATES];
	long m_numDates;
	
	//long m_dateStart1;
	//long m_dateStart2;
	//long m_dateStart3;
	//long m_dateEnd1;
	//long m_dateEnd2;
	//long m_dateEnd3;
	// the sentence the date is in. we show the date in this sentence
	// in the summary. the date itself will be highlighted somehow.
	// this way if an event has a different description
	// for a particular date then we can show the entire sentence. like
	// if all events have the same address but different times and each
	// time is something different... i.e. Tingley Colesium has the
	// "Motorcross every Saturday..." and "Sat Nov 5th is Kool-Aid Day..."
	// then we will show that special day's description in its sentence.
	// this will happen if someone searches for "motorcross". this is
	// the sentence that contains the date in m_dateStart1/m_dateSize1.
	//long m_dateSentStart1;
	//long m_dateSentStart2;
	//long m_dateSentStart3;
	//long m_dateSentEnd1;
	//long m_dateSentEnd2;
	//long m_dateSentEnd3;

	bool printEventDisplay ( class SafeBuf *sb , char *ptr_utf8Content );

	bool printEventTitle ( SafeBuf &sb , char *content );
};

// confirmed flags for EventDisplay::m_confirmed
#define CF_ACCEPT     0x01
#define CF_REJECT     0x02
#define CF_TITLE      0x04
#define CF_VENUE      0x08
#define CF_VENUE_NONE 0x10


// values for EventDesc::m_dflags
#define EDF_TITLE             0x01
#define EDF_IN_SUMMARY        0x02
#define EDF_DATE_ONLY         0x04
// . if sentence is just the address and no extra info
// . used by PageGet.cpp
#define EDF_HASEVENTADDRESS   0x08
//#define EDF_UNIQUE_TURK_HASH  0x04
// this means the EventDesc is actually an exact Place Name
#define EDF_PLACE_TYPE        0x10
// this is the one we chose
#define EDF_BEST_PLACE_NAME   0x20
// . is this a pretty, well-formed sentence?
// . we need to know this because if the turks change the title
//   then the pretty sentences that were above the title are all
//   of a sudden by default part of the event's description, and they
//   were never even turked! because isIndexable() returns false for
//   any sentence before the event title and makes the descScore 0.0.
//   but "pretty" sentences are really all that we need to worry about
//   because they are basically the only sentences in the description,
//   excluding title, price, phone, date and address based sentences.
#define EDF_PRETTY              0x0040
#define EDF_CONFIRMED_DESCR     0x0080
#define EDF_CONFIRMED_NOT_DESCR 0x0100
#define EDF_TURK_TITLE          0x0200
#define EDF_TURK_VENUE          0x0400
#define EDF_MENU_CRUFT          0x0800
#define EDF_HASEVENTDATE        0x1000
#define EDF_INDEXABLE           0x2000

#define EDF_TURK_TITLE_CANDIDATE  0x04000
#define EDF_TURK_VENUE_CANDIDATE  0x08000
#define EDF_FARDUP                0x10000
#define EDF_FARDUPPHONE           0x20000
#define EDF_FARDUPPRICE           0x40000
#define EDF_SUBEVENTBROTHER       0x80000
#define EDF_TAGS                  0x100000
// these two flags are used by XmlDoc::getEventSummary()
#define EDF_TRUNCATED             0x200000
#define EDF_HAS_DATE              0x400000
#define EDF_HAS_PRICE             0x800000
#define EDF_INTITLETAG            0x01000000
#define EDF_FBPICSQUARE           0x02000000
#define EDF_JUST_PLACE            0x04000000

class EventDesc {
 public:
	// offset into doc in bytes [m_off1,m_off2) of desc
	long  m_off1;
	long  m_off2;
	float m_titleScore;
	float m_descScore;
	// EDF_TITLE|EDF_IN_SUMMARY|EDF_DATE_ONLY|...
	long  m_dflags;
	// the turk now stores the content hash of this sentence
	// in tagdb for this eventhash/docid
	unsigned long m_sentContentHash32;
	// . hash of the last 5 tagids containing us
	// . turk votes on this tag hash being title/desc/nondesc
	//long  m_turkTagHash5;
	unsigned long m_tagHash32;
	// for the SummaryLline generation algo in XmlDoc.cpp, we like
	// to know if the SummaryLines are adjacent to avoid printing a
	// "..." between them
	long  m_alnumPosA;
	long  m_alnumPosB;

	bool printEventDesc ( class SafeBuf *sb , char *ptr_utf8Content );
};

#define MAX_EVENTS 255

// XmlDoc::getMsg20Reply() calls this to parse up ptr_eventsData into
// Msg20Reply::ptr_eventTitle, ... when Msg20Request::m_eventId >= 0 ...
// when user is searching events
EventDisplay *getEventDisplay ( long  eventId         ,
				char *ptr_eventsData  , 
				long  size_eventsData );
				//char *ptr_tagBuf      ) ;

// . the list of all the events described in the document
// . each event has a time (in the future) and a place associated with it
class Events {
 public:

	bool set ( class Url         *u         ,
		   class Words       *w         ,
		   class Xml         *xml       ,
		   class Links       *links     ,
		   class Phrases     *phrases   ,
		   class Synonyms    *synonyms  ,
		   //class Weights     *weights   ,
		   class Bits        *bits      ,
		   class Sections    *sections  ,
		   class SubSent     *subSents  ,
		   long               numSubSents,
		   class SectionVotingTable *osvt ,
		   class Dates       *dp        ,
		   class Addresses   *addresses ,
		   class TagRec      *gr        ,
		   class XmlDoc      *xd        ,
		   long               spideredTime ,
		   class SafeBuf     *pbuf      ,
		   long               niceness  ) ;

	// tbt = turk bits table
	bool setTitlesAndVenueNames ( class HashTableX  *tbt ,
				      class HashTableX *est );

	bool setTitle ( class Event *ev );

	float getSimilarity ( long a0 , long b0 , long a1 , long b1 ) ;

	bool setBestVenueName ( class Event *ev );

	bool hash ( //long              baseScore ,
		    //long              version   ,
		    class HashTableX *dt        ,
		    class SafeBuf    *pbuf      ,
		    class HashTableX *wts       ,
		    class SafeBuf    *wbuf      ,
		    long              numHashableEvents ) ;

	bool hashIntervals ( class Event *ev , class HashTableX *dt ) ;

	float getSentTitleScore ( class Section *si ,
				  sentflags_t sflags ,
				  esflags_t esflags ,
				  long sa ,
				  long sb ,
				  bool isStoreHoursEvent ,
				  class Event *ev ,
				  float *retDescScore ,
				  bool isSubSent );

	// print a table of the events
	bool print ( class SafeBuf *pbuf , long siteHash32 ,
		     long long uh64 ) ;

	bool printEvent ( class SafeBuf *pbuf , long i , long long uh64 );

	bool printEventForCheckbox ( class Event *ev , 
				     class SafeBuf *pbuf , 
				     long long uh64 ,
				     long i ,
				     char *boxPrefix );

	// if in the div no display tag for validation, we set hidden to true
	void printEventDescription ( class SafeBuf *pbuf , class Event *ev ,
				     bool hidden );

	bool printEventSentence ( Section *si ,
				  class Event *ev,
				  sentflags_t sflags ,
				  esflags_t esflags ,
				  long a ,
				  long b ,
				  bool hidden ,
				  float tscore ,
				  //float dscore ,
				  class SafeBuf *pbuf ) ;

	// sets Event::m_intervalsOff and m_ni for an event and stores the
	// resulting Intervals into the safebuf, "sb"
	bool getIntervals ( class Event *ev , class SafeBuf *sb ) ;

	// how much space in XmlDoc::m_metaList we need to write out
	// all the Timedb keys to store into timedb
	long getIntervalsNeed();
	char *addIntervals ( char *metaList, 
			     long long docId ,
			     char rdbId ) ;
	char *addIntervals2 ( class Event *ev , 
			      char *metaList, 
			      long long docId ,
			      char rdbId ) ;

	// . returns -1 and sets g_errno on error
	// . returns NULL if no event data
	char *makeEventDisplay ( long *size , long *retNumDisplays );

	// we use this to set XmlDoc::ptr_eventsData/size_eventsData blob
	// that describes our events using the EventDisplay class
	bool makeEventDisplay ( class SafeBuf *sb , long *numDisplaysMade );

	bool makeEventDisplay2 ( class Event *ev , class SafeBuf *sb );

	bool isIndexable ( class Event *ev , class Section *si ) ;

	long getNumEvents ( ) { return m_numEvents; };

	// save these for hashing
	class Words    *m_words;
	class Xml      *m_xml;
	class Links    *m_links;
	class Phrases  *m_phrases;
	class Synonyms *m_synonyms;
	class HashTableX *m_tbt;
	class HashTableX *m_evsft;
	//class Weights  *m_weights;
	class Dates    *m_dates;
	class XmlDoc   *m_xd;
	class Sections *m_sections;
	class SubSent  *m_subSents;
	long            m_numSubSents;
	class SectionVotingTable *m_osvt;
	class Addresses *m_addresses;
	class Url       *m_url;
	class Bits      *m_bitsOrig;

	// is the url a facebook url. i.e. we are indexing a facebook event
	// from an xml or json format...
	bool m_isFacebook;

	wbit_t *m_bits;

	bool m_eventDataValid;

	bool m_isStubHubValid;
	bool m_isStubHub;
	bool m_isEventBrite;

	long     m_niceness;

	// from XmlDoc
	long m_spideredTime;

	// for holding the event data
	SafeBuf m_sb2;

	// for holding the Intervals which Event::m_intervalsOff references
	SafeBuf m_sb;

	// only compute and hash intervals within [m_year0,m_year1), otherwise
	// we'd have an infinite # of intervals
	long m_year0;
	long m_year1;

	char *m_note;

	Event m_events[MAX_EVENTS];
	long  m_numEvents;

	// . of those, how many were legit
	// . this is referenced by eventid now, which starts at 1! so
	//   add one to this array
	//Event *m_validEvents[MAX_EVENTS+1];
	Event *m_idToEvent[MAX_EVENTS+1];
	long   m_numIds;

	// # of events in m_events[] that do not have a EV_BAD_EVENT flag set
	//long  m_numValidEvents;

	Event *getEventFromId ( long id ) {return m_idToEvent[id]; };

	// this excludes the outlinked titles
	//long  m_revisedValid;

	SafeBuf *m_pbuf;

	// shortcuts
	char          **m_wptrs;
	long           *m_wlens;
	long long      *m_wids;
	nodeid_t       *m_tids;
	long            m_nw;

	long m_numFutureDates    ;
	long m_numRecurringDates ;
	long m_numTODs           ;

	//bool m_regTableValid;
	//HashTableX m_regTable;
	//HashTableX *getRegistrationTable();
	//bool setRegistrationBits();
	//bool m_setRegBits;

	//long m_maxIndexedEventId;

	// for printing out score of each title candidate
	HashTableX m_titleScoreTable;
	HashTableX m_descScoreTable;
	//HashTableX m_titleFlagsTable;
	//HashTableX m_eventSentFlagsTable;
};



class EventIdBits {
 public:
	uint8_t m_bits[32];

	void addEventId ( long eid ) {
		// get our event id bit
		long byteOff = eid / 8;
		// bit mask
		uint8_t bitMask = 1 << ( eid % 8 );
		// update the byte
		m_bits[byteOff] |= bitMask;
	};

	void clear ( ) { memset ( m_bits , 0 , 32 ); };

	bool isEmpty () {
		for ( long i = 0 ; i < 32 ; i++ )
			if ( m_bits[i] ) return false;
		return true;
	};

	bool hasEventId ( long eid ) {
		// get our event id bit
		long byteOff = eid / 8;
		// bit mask
		uint8_t bitMask = 1 << ( eid % 8 );
		// update the byte
		return ( m_bits[byteOff] & bitMask );
	};

	void print ( char *evs , long esize ) {
		char *pe = evs;
		pe += sprintf(evs," eventIds=");
		bool first = true;
		for ( long j = 0 ; j<32 ; j++ ) {
			// get bit mask
			long byteOff = j/8;
			uint8_t bitMask = 1<<(j%8);
			if ( !( m_bits[byteOff] & bitMask))
				continue;
			if ( ! first )
				pe += sprintf(pe,",");
			first = false;
			pe += sprintf(pe,"%li",j);
		}
	};

	void set ( class HttpRequest *r , char *cgiparm );
};

#endif
