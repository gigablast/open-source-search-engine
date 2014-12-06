// i guess _ADDRESS_H_ is reserved, so prepend _GB
#ifndef _GB_ADDRESS_H_
#define _GB_ADDRESS_H_

// values for Place::m_bits
//#define PLF_PARTIAL  0x001 // hash in the next word and try again!
#define PLF_UNIQUE     0x001 // city name is unique in the usa
//#define PLF_AMBIGUOUS0x002 // might be multiple things. 2+ place bits set
//#define PLF_ALIAS      0x002 // use PlaceDescAlias class!!
#define PLF_INFILE     0x004 // is in allCountries/postalCodes.txt
//#define PLF_INHERITED  0x008
//#define PLF_FROMZIP    0x008 // inherited from zip adm1 or city
#define PLF_FROMTAG    0x010 // a place from a TagRec Tag
#define PLF_ABBR       0x020 // allCountries.txt, "NM" is for "New Mexico"
#define PLF_FROMTITLE  0x040 // from the title tag
//#define PLF_ALT      0x080 // allCountries.txt alias/alternative name
#define PLF_HAS_UPPER  0x080 // does it have an upper-case word in it?


// secondary flags for Place::m_flags2 variable
#define PLF2_IS_NAME       0x001 // really a place name and not a street
#define PLF2_IS_POBOX      0x002
#define PLF2_AFTER_AT      0x004
// this means it is a place to register or a place to buy tickets, but
// not an actual event place per se. we just scan for "register" 
// or "buy tickets" in the same sentence, brs not allowed i guess.
#define PLF2_TICKET_PLACE  0x008
// if the adm1/city is right after the word "in" we set this to
// require it be part of the address
#define PLF2_REQUIRED      0x010
// are we a street intersections like "west ave & north street"
#define PLF2_INTERSECTION  0x020
//#define PLF2_HAD_INDICATOR 0x040
#define PLF2_COLLISION     0x040
//#define PLF2_IN_PHRAS    0x080
//#define PLF2_STORE_NAME    0x080

// for Place::m_flags3 (a 32 -bit flag guy)
#define PLF3_LATLONDUP          0x00000001
#define PLF3_SUPPLANTED         0x00000008

// . indicators. words or phrases that indicate a possible place name, suite,
// . these ARE NOT places unto themselves
// . IND_NAME = a common word in the places in allCountries.txt
#define IND_NAME       0x01
#define IND_SUITE      0x02
#define IND_STREET     0x04
#define IND_DIR        0x08
#define IND_BITS       0x0f

#define NO_LATITUDE  999.0
#define NO_LONGITUDE 999.0

#define AMBIG_LATITUDE  888.0
#define AMBIG_LONGITUDE 888.0

#include "gb-include.h"
#include "Msg0.h"
#include "Multicast.h"
#include "HashTableX.h"
#include "SafeBuf.h"

class StateDesc {
public:
	char *m_adm1;
	char *m_name1;
	char *m_name2;
};

bool printTesterPage ( SafeBuf &sb ) ;

int32_t memcpy2 ( char *dst , char *src , int32_t bytes , bool filterCommas ,
	       // do not store more than this many bytes into dst
	       int32_t dstMaxBytes = -1 ) ;

bool getIPLocation ( int32_t   ip     ,
		     double *lat    ,
		     double *lon    ,
		     double *radius ,
		     char  **city  ,
		     char  **state ,
		     char  **ctry  ,
		     char   *buf   ,
		     int32_t    bufSize ) ;

bool getLatLonFromUserInput ( float *radius,
			      char *where ,
			      float *cityLat ,
			      float *cityLon ,
			      float *stateLat,
			      float *stateLon,
			      float *countryLat,
			      float *countryLon,
			      //double *radius ,
			      // if zip code supplied, we set zipLat/zipLon
			      // to the centroid of the zipcode
			      float  *zipLat ,
			      float  *zipLon ,
			      float  *userLat ,
			      float  *userLon ,
			      class PlaceDesc **retCityDesc,
			      class PlaceDesc **retStateDesc,
			      class PlaceDesc **retCountryDesc,
			      char   *timeZone ,
			      char   *useDST,
			      // country of search based on ip (two letters)
			      uint8_t ipCrid,
			      char   *gbaddressBuf ,
			      int32_t    gbaddressBufSize );

bool getLatLon ( uint32_t cityId , double *lat , double *lon ) ;
bool getCityLatLonFromAddrStr ( char *addr , double *lat , double *lon ) ;

uint8_t getCountryIdFromAddrStr ( char *addr ) ;

uint32_t getCityIdFromAddr ( char *addr ) ;

bool getCityLatLonFromAddress ( class Address *aa , double *lat , double *lon);
bool getZipLatLonFromAddress ( class Address *aa, float *lat, float *lon );

bool hashPlaceName ( HashTableX *nt1,
		     Words *words,
		     int32_t a ,
		     int32_t b ,
		     uint64_t v ) ;

class Place *getZipPlace   ( int32_t a , int32_t alnumPos , class Words *words );
class Place *getCityPlace  ( int32_t a , int32_t alnumPos , class Words *words );
class Place *getStatePlace ( int32_t a , int32_t alnumPos , class Words *words );

int32_t getCommonWordIds ( int32_t a1 , int32_t b1 ,
			int32_t a2 , int32_t b2 ,
			int64_t *wids      ,
			int64_t *commonIds ,
			int32_t max ,
			int32_t niceness ) ;

// used by XmlDoc::makeSimpleWordVector() as well
int64_t *getSynonymWord ( int64_t *h , int64_t *prevId , bool isStreet );

// called by main.cpp
void handleRequest2c ( class UdpSlot *slot , int32_t nicenessWTF ) ;

// called by main.cpp
bool initPlaceDescTable ( ) ;
bool initCityLists      ( ) ;
bool initCityLists_new  ( ) ;
void resetAddressTables ( ) ;

typedef uint8_t pbits_t;

typedef uint8_t pflags_t;

uint64_t getAdm1Bits ( char *stateAbbr ) ;
class StateDesc *getStateDesc ( char *stateAbbr ) ;
StateDesc *getStateDescByNum ( int32_t i ) ;

// . values for Place::m_type
// . now a place can be multiple types
// . like there is a city called kentucky, or something
typedef uint16_t placetype_t;
#define PT_NAME_1  0x01 // places now have two names
#define PT_NAME_2  0x02 // places now have two names
#define PT_STREET  0x04
#define PT_CITY    0x08
#define PT_ZIP     0x10
#define PT_SUITE   0x20
#define PT_STATE   0x40
#define PT_LATLON  0x80
#define PT_COUNTRY 0x0100


// a place is a physical area or physical point which has a name
class Place {
 public:
	// first word in the place phrase
	int32_t m_a;
	// the last word plus one
	int32_t m_b;
	// . like m_a and m_b above but only over "alnum words"
	// . m_alnumA is the first alnum word # in place phrase
	// . used for seeing how many alnum words between two places
	int32_t m_alnumA;
	int32_t m_alnumB;

	// list of all the Places that are compatible with this Place
	//Place **m_brothers;
	//int32_t    m_numBrothers;

	// see above for these bit values
	placetype_t m_type;

	uint64_t m_adm1Bits;
	// if we have only a single legit adm1 bit set, then this is the
	// state's two letter abbr
	char m_adm1[2];

	// the score of the place phrase
	//float m_score;
	// . these are descriptor bits of the place
	// . these are the #define'd PLF_*/IND_* values above
	pbits_t m_bits;
	// the hash of the place for looking up in allCountries.txt hash table
	uint64_t m_hash;
	// the hash of its words (synonyms)
	uint64_t m_wordHash64;
	// straight forward xor of the wordids in the place
	// for comparing to event title for setting EV_STORE_HOURS in
	// Events.cpp.
	uint32_t m_simpleHash32;
	// these are for streets only
	uint64_t m_streetNumHash;
	uint64_t m_streetIndHash;
	// index into allCountries.txt, -1 means none
	//int32_t m_index;
	// the string, "words" not intact if setting from tag (setFromTag())
	char *m_str;
	int32_t  m_strlen;

	// secondary bit/flags
	pflags_t m_flags2;

	uint32_t m_flags3;

	// the inlined/verified address that contains us, if any
	class Address *m_address;
	class Address *m_alias;

	// are we a name and part of an unverified address?
	class Address *m_unverifiedAddress;

	char *m_siteTitleBuf;
	int32_t  m_siteTitleBufSize;

	// uesd by Events.cpp as a temporary storage
	int32_t m_eventDescOff;

	// . what city is the place in
	// . used for zip codes mostly
	uint64_t m_cityHash;
	// this points into g_cityBuf
	char *m_cityStr;
	// this is \0\0 if not applicable
	//char m_adm1[2];

	// . what states this place is in
	// . like for "springfield" being in multiple states
	// . use the STATE_NM etc values above
	//uint64_t m_adm1Bits;

	// and the *country* id
	uint8_t m_crid;
	// boost based on indicators only.
	//float m_indScore;
	// tag hash of section we are in
	//int32_t m_tagHash;
	// we can only pair up with a Place if its m_a < this m_rangeb
	//int32_t m_rangeb;

	// do we intersect place "p" ?
	bool intersects ( class Place *p ) {
		if ( m_a <  p->m_a && m_b >  p->m_a ) return true;
		if ( m_a <  p->m_b && m_b >  p->m_b ) return true;
		if ( m_a >= p->m_a && m_b <= p->m_b ) return true;
		return false;
	};

	void reset() { memset ( &m_a , 0 , sizeof(Place) ); }
};

// . bit flags for Address::m_flags
// . 0x01 is the AF_AMBIGUOUS bit
// . 0x02 is the AF_VERIFIED bit (Addressess::verify())
#define AF_AMBIGUOUS             0x01
#define AF_VERIFIED_STREET       0x02
#define AF_VERIFIED_STREET_NUM   0x04
#define AF_VERIFIED_PLACE_NAME_1 0x08
// if city and adm1 (state) are right after street address we are "inlined"
#define AF_INLINED               0x10
// ignore this address for whatever reason
//#define AF_IGNORE              0x20
#define AF_VENUE_DEFAULT         0x20
#define AF_VERIFIED_PLACE_NAME_2 0x40
#define AF_GOT_REPLY             0x80

// for Address::m_flags3
#define AF2_HAS_REQUIRED_CITY  0x01
#define AF2_HAS_REQUIRED_STATE 0x02
#define AF2_VALID              0x04
#define AF2_BADCITYSTATE       0x08
#define AF2_USEDINEVENT        0x10
#define AF2_LATLON             0x20 // are we a lat/lon address?

// an address consists of a set of Places of different types
class Address {
 public:
	bool hash ( int32_t              baseScore ,
		    class HashTableX *dt        ,
		    uint32_t          date      ,
		    class Words      *words     , 
		    class Phrases    *phrases   , 
		    class SafeBuf    *pbuf      ,
		    class HashTableX *wts       ,
		    class SafeBuf    *wbuf      ,
		    int32_t              version   ,
		    int32_t              niceness  ) ;

	//bool addToTagRec ( class TagRec *gr , 
	//		   int32_t ip , 
	//		   int32_t timestamp ,
	//		   char *origUrl ,  // = NULL
	//		   int32_t maxAddrBytes , // = -1 ,
	//		   char *tagName ) ;

	int32_t getStoredSize ( int32_t olen , bool includeHash );
	bool serializeVerified ( class SafeBuf *sb ) ;
	int32_t serialize ( char *buf , 
			 int32_t bufSize , 
			 char *origUrl ,
			 bool verifiedOnly ,
			 bool includeHash ) ;

	key128_t makePlacedbKey ( int64_t docId , 
				  bool useName1 ,
				  bool useName2 );

	void setDivId ( ) ;

	//int64_t makeAddressVotingTableKey ( );

	int32_t print  ( );
	int32_t print2 ( int32_t i, SafeBuf *pbuf , int64_t uh64 );
	void printEssentials ( SafeBuf *pbuf , bool forEvents ,
			       int64_t uh64 );

	void reset() {
		m_name1 = m_name2 = m_suite = m_street = NULL;
		m_city = m_zip = m_adm1 = NULL;
		//m_cityHash = 0;
		//m_adm1Bits = 0;
		m_hash = 0LL;
	};

	// the largest section containing this address that does not
	// contain any other addresses that are verified/inlined/afterat.
	// UNLESS of course the base section contains multiple such addresses
	// then there is nothing we can do!
	// this is set in Events.cpp and only used by Events.cpp for pairing
	// event sections (tod sections) with addresses.
	//class Section *m_disjointSection;

	// . these are taken from city, adm1 or zip
	// . sometimes m_adm1 is NULL and we imply m_adm1Bits from city or zip
	// . or if m_city is NULL we imply m_cityHash from m_zip->m_cityHash
	//uint64_t m_cityHash;
	//uint64_t m_adm1Bits;
	uint32_t m_cityId32;

	// starts with word #a, and word #b ends it and is not included in it
	class Place *m_name1;
	// secondary name!
	class Place *m_name2;
	// suite, room, etc.
	class Place *m_suite;
	// street address
	class Place *m_street;
	// populated place
	class Place *m_city;
	// zip code
	class Place *m_zip;
	// "state" in the U.S.
	class Place *m_adm1;
	// useful
	class Section *m_section;

	// sometimes we are a name like "Center" and refer to
	// the address for the "National Hispanic Culture Center" which
	// is verified or inlined on the page already.
	class Address *m_alias;

	// . this points into the address reply buffer for the address reply
	//   and is a list of votescore/placename pairs separated by \0's
	// . each placename as a minimum votescore of 2
	// . it can be used as a list of alternative place names that do
	//   not match the place names we selected, if any.
	// . it is used to fix adobetheater.com which had a street address
	//   for its venue but not the proper venue name "adobe theater".
	// . i expect this is somewhat common that the venue websites omit
	//   the venue name above the address because it is implied
	char *m_placedbNames;
	char *m_placedbNamesEnd;

	// and of those placedbnames, this is the one with the most votes
	char *m_bestPlacedbName;

	double m_latitude;
	double m_longitude;
	int32_t   m_latLonScore;
	int32_t   m_latLonDist;

	//char   m_timeZoneOffset;

	double m_importedLatitude;
	double m_importedLongitude;
	int32_t   m_importedVotes;

	double m_geocoderLat;
	double m_geocoderLon;

	void getLatLon ( double *lat , double *lon );

	// score of the address
	//float m_score;
	// . bit flags
	// . 0x01 is the AF_AMBIGUOUS bit
	char m_flags;
	// . flags set in a Msg2c reply byte
	// . we OR these into m_flags above
	char m_replyFlags;
	// bookends in word space
	//int32_t m_a;
	//int32_t m_b;

	// extra stuff, AF_HAS_REQUIRED_CITY,...
	char m_flags3;

	// range of words in the body
	//int32_t m_wordStart;
	//int32_t m_wordEnd;

	// the scores
	//float m_scoreBase;
	//float m_scoreNameBeforeStreet;
	//float m_scoreDistanceNameToStreet;
	//float m_scoreOldVoteMod;
	//float m_scoreNewVoteMod;
	//float m_scoreDistanceNameToStreetValue;

	key128_t m_placedbKey;

	// used for the m_avt, address voting table. must be unique for
	// every address we have
	//int64_t m_avtKey;

	// the dom and "firstip" this address came from
	int32_t m_domHash32;
	int32_t m_ip;

	// used to see if two addresses are basically the same
	uint64_t m_hash;
	// score of address for deduping based on m_hash
	int32_t m_score2;

	char getTimeZone ( char *useDST );

	// these are used by msg2c to verify addresses in placedb
	int32_t m_reqBufNum;
	int32_t m_addrNum;

	//int32_t m_eventTagHash;

	//char m_requestBuf [ 64 ];

	//void reset() { memset ( &m_name , 0 , sizeof(Address) ); }
};

#define MAX_ADDR_REQUESTS_OUT 50
#define REQBUFSIZE 1024

// this msg class is for getting AND adding to tagdb
class Msg2c {

 public:
	
	Msg2c ();
	~Msg2c ();
	void reset();

	// . get records from multiple subdomains of url
	// . calls g_udpServer.sendRequest() on each subdomain of url
	// . all matching records are merge into a final record
	//   i.e. site tags are also propagated accordingly
	// . closest matching "site" is used as the "site" (the site url)
	// . stores the tagRec in your "tagRec"
	bool verifyAddresses ( class Addresses *aa         ,
			       //char            *coll       , 
			       collnum_t collnum,
			       int32_t             domHash32  ,
			       int32_t             ip         ,
			       //HashTableX      *avt        ,
			       int32_t             niceness   ,
			       void            *state      ,
			       void           (* callback)(void *state ) );
	
	bool launchRequests ( );

	// some specified input
	//char  *m_coll;
	//int32_t   m_collLen;
	collnum_t m_collnum;
	void    (*m_callback ) ( void *state );
	void     *m_state;
	int32_t m_niceness;
	int32_t  m_requests;
	int32_t  m_replies;
	char  m_doneLaunching;
	int32_t  m_errno;
	//Multicast m_mcast;
	int32_t m_domHash32;
	int32_t m_ip;
	int32_t m_i;
	class Addresses *m_addresses;

	// these are used for sending out multiple msg2c requests at the
	// same time, to parallelize our lookups into placedb to verify
	// addresses
	char      m_bigBuf [MAX_ADDR_REQUESTS_OUT][REQBUFSIZE];
	Multicast m_mcasts [MAX_ADDR_REQUESTS_OUT];
	char      m_inUse  [MAX_ADDR_REQUESTS_OUT];
	bool      m_initializedInUse;
};

// use for storing Places
class PlaceMem {

 public:

	// if you just want to call setStr() and have it use stack mem to
	// store up to 10 places, then init the PlaceMem with this very quickly
	void init ( int32_t  poolSize         , 
		    int32_t  initNumPoolPtrs  ,
		    int32_t  initNumPlacePtrs ,
		    char *stackMem         , 
		    int32_t  stackMemSize     ,
		    int32_t  niceness         );

	// . returns NULL and sets g_errno on error
	// . stores ptr to the returned mem in m_placePtrs[placeNum]
	void *getMem ( int32_t need );

	PlaceMem();
	~PlaceMem();
	void reset();

	int32_t  getNumPtrs ( ) { return m_numPlacePtrs; };

	void *getPtr ( int32_t ptrNum ) {
		if ( ptrNum >= m_numPlacePtrs ) { char *xx=NULL;*xx=0; }
		return (void *)m_placePtrs[ptrNum];
	};

	// sometimes we remove Places. two ways to do this:
	void rewind     ( int32_t numPtrsToRewind ) {
		return setNumPtrs ( m_numPlacePtrs - numPtrsToRewind); };

	void setNumPtrs ( int32_t newNumPtrs      );

	char **m_poolPtrs;
	int32_t   m_numPoolsAllocated;
	int32_t   m_numPoolPtrsAllocated;

	int32_t   m_poolSize;

	// i guess we just gotta realloc this to keep it contiguous
	char **m_placePtrs;
	int32_t   m_numPlacePtrs;
	int32_t   m_numPlacePtrsAllocated;

	int32_t   m_initNumPoolPtrs;
	int32_t   m_initNumPlacePtrs;

	char  *m_cursor;
	char  *m_cursorEnd;
	int32_t   m_cursorPoolNum;

	char  *m_stack;
	int32_t   m_stackSize;

	int32_t   m_niceness;
};

//#define MAX_ADDRESSES 32000
//#define MAX_STREETS   6300
//#define MAX_PLACES    4500

// all the addresses in the document are described by this class
class Addresses {

 public:

	Addresses  ( ) ;
        ~Addresses ( ) ;
	void reset();

	bool set ( class Sections *sections    ,
		   class Words    *words       ,
		   class Bits     *bits        ,
		   class TagRec   *gr          ,
		   class Url      *url         ,
		   int64_t       docId       ,
		   //char           *coll        ,
		   collnum_t collnum,
		   int32_t            domHash32   ,
		   int32_t            ip          ,
		   //int32_t            tagPairHash ,
		   int32_t            niceness    ,
		   class SafeBuf  *pbuf        ,
		   void           *state       ,
		   void          (*callback) (void *state) ,
		   uint8_t         contentType ,
		   // this is XmlDoc::ptr_addressReply
		   //char           *addressReply ,
		   //int32_t            addressReplySize ,
		   //bool            addressReplyValid ,
		   char *siteTitleBuf     ,
		   int32_t  siteTitleBufSize ,
		   class XmlDoc *xd );

	bool set2 ( ) ;

	void print ( class SafeBuf *pbuf , int64_t uh64 );

	bool addAddress ( class Place *name1  ,
			  class Place *name2  ,
			  class Place *suite  ,
			  class Place *street ,
			  class Place *city   ,
			  class Place *adm1   ,
			  class Place *zip    ,
			  class Place *ctry   ,
			  class Section *sn   ,
			  int32_t           startAlnum ,
			  char   flags3    ,
			  class Address **retAddr );

	bool hashForPlacedb ( int64_t         docId    ,
			      int32_t              domHash  ,
			      int32_t              ip       ,
			      class HashTableX *dt       ) ;

	bool isCityName ( int32_t a , int32_t b ) ;

	bool isStateName ( int32_t a ) ;

	bool isCityState ( class Section *si ) ;

	int32_t isCityState3 ( int64_t h1 , int64_t h2 ) ;

	int32_t isCityState2 ( int32_t a , int32_t b ) ;

	void setAmbiguousFlags ( ) ;

	// . used by XmlDoc.cpp to update tagRec
	// . adds all m_na addresses to the TagRec
	//bool addToTagRec ( TagRec *gr , int32_t ip , int32_t timestamp ,
	//		   char *origUrl , // = NULL ,
	//		   int32_t maxAddrBytes , // = -1 ) ;
	//		   char *tagName );

	/*
	int32_t addProperPlaces  ( int32_t         a             , 
				int32_t         b             ,
				int32_t         maxAlnumCount ,
				class Place *pp            , 
				int32_t         maxPlaces     ,
				int32_t         np            ,
				pbits_t      flags         ,
				int32_t         alnumPos      ,
				int32_t         forcedEnd     );
	*/

	int32_t cityAdm1Follows ( int32_t a ) ;

	int32_t setFromTag ( Address *a, class Tag *tag, PlaceMem *placeMem );

	int32_t getNumAddresses ( ) { return m_am.getNumPtrs(); };
	int32_t getNumNonDupAddresses ( ) { return m_numNonDupAddresses; };
	int32_t getNumVenues ( ) { return m_numVenues; };

	class Place *getAssociatedPlace ( int32_t i ) ;

	// . before computing addresses we try to get the streets
	// . this includes typical streets name "123 main street" as well
	//   as potential names of places like "Dave's Bar"
	//Place m_streets[ MAX_STREETS ];
	//int32_t  m_ns;
	PlaceMem m_sm;

	// if we got too many streets and fake streets set this to true
	// in order to avoid parsing the doc any further since it will
	// take too long!
	bool m_breached;

	// cities, states and zips are in here
	//Place m_places[ MAX_PLACES ];
	//int32_t  m_np;
	PlaceMem m_pm;
	int32_t  m_npSaved;

	//Address m_addresses[ MAX_ADDRESSES ];
	//int32_t    m_na;
	PlaceMem m_am;
	int32_t    m_numNonDupAddresses;
	int32_t    m_numVenues;

	Address m_venueDefault;

	// store msg2c reply into m_sb
	bool addToReplyBuf ( char *reply , int32_t replySize , int32_t addrNum );
	SafeBuf m_sb;

	// final places we use for addresses are stored in here
	//Place m_final [ MAX_FINAL_PLACES ];
	//int32_t  m_nf;

	Msg2c          *m_msg2c;
	Msg0            m_msg0;
	RdbList         m_list;
	class Url      *m_url;
	int64_t       m_docId;
	//char           *m_coll;
	collnum_t m_collnum;
	int64_t       m_termId;
	int32_t            m_domHash32;
	int32_t            m_ip;
	//int32_t            m_tagPairHash;
	class Sections *m_sections;
	class Words    *m_words;
	char          **m_wptrs;
	int32_t           *m_wlens;
	int32_t            m_nw;
	int64_t      *m_wids;
	nodeid_t       *m_tids;
	class Bits     *m_bits;
	class TagRec   *m_gr;
	int32_t            m_niceness;
	class SafeBuf  *m_pbuf;
	void           *m_state;
	void          (* m_callback) (void *state );
	uint8_t         m_contentType;

	// address verification table (set by msg2c intially)
	//HashTableX      m_avt;
	// table serialize into this buffer which we alloc
	char *m_buf;
	int32_t  m_bufSize;
	// this is XmlDoc::ptr_addressReply
	//char           *m_addressReply;
	//int32_t            m_addressReplySize;
	//bool            m_addressReplyValid;

	bool m_firstBreach;

	char *m_siteTitleBuf     ;
	int32_t  m_siteTitleBufSize ;

	int32_t m_uniqueStreetHashes;

	class XmlDoc *m_xd;
	
	// XmlDoc sets its ptr_addressReply to this for storing in title rec
	//char *getAddressReply ( int32_t *size ) {
	//	*size = m_sb.length();	return m_sb.getBufStart(); };

	// this sets the m_avt table from the m_addresses[]
	bool updateAddresses();

	bool setGeocoderLatLons ( void *state, 
				  void (*callback) (void *state) );
	bool processGeocoderReply ( class TcpSocket *s );
	bool m_calledGeocoder;

	// set Section::m_firstPlaceNum which indexes into m_sorted[]
	// so you can quickly scan the places contained by a section
	bool setFirstPlaceNums ( );
	Place **m_sorted;
	int32_t    m_sortedSize;
	int32_t    m_numSorted;
	bool    m_sortedValid;
	//class HashTableX *getPlaceTable();
	//bool m_ptValid;
	//HashTableX m_pt;

	// # of inlined or verified addresses out of the m_na we have
	int32_t m_numValid;

	bool addIntersection ( int32_t i , int32_t alnumPos ) ;
	bool isInStreet ( int32_t j ) ;

	// used by Repair.cpp to cache the old event address's lat/lon
	// to save us from slamming the poor geocoder!
	RdbCache m_latLonCache;
};

extern void getDSTInterval ( int32_t year, int32_t *a , int32_t *b );
extern int32_t getDayOfWeek ( int64_t h ) ;
extern bool setFromStr ( Address *a, char *s, pbits_t flags , 
			 class PlaceMem *placeMem ,
			 int32_t niceness ) ;
extern void setFromStr2 ( char  *addr   ,
			  char **name1  ,
			  char **name2  ,
			  char **suite  ,
			  char **street ,
			  char **city   ,
			  char **adm1   ,
			  char **zip    ,
			  char **country,
			  double *lat    ,
			  double *lon    );
			  //int32_t  *tzoff  ) ;

#define UNKNOWN_TIMEZONE 100

extern bool getIsDST ( int32_t nowUTC , char timezone ) ;
extern char getTimeZoneFromAddr ( char *addr , char *useDST ) ;
// state is two letter state abbr
extern char getTimeZone2 ( char *city , char *state , char *useDST );
extern char getTimeZone3 ( uint32_t cid32 , char *useDST );
extern char getTimeZoneFromUserIP ( int32_t uip , int32_t niceness , char *useDST ) ;
extern char getTimeZoneFromLatLon ( float lat , float lon , int32_t niceness ,
				    char *useDST ) ;
extern uint32_t getNearestCityId ( float lat , float lon , int32_t niceness ,
				   float *distInMiles = NULL ) ;

extern uint64_t getHashFromAddr ( char *addr ) ;

extern bool getZipLatLonFromStr ( char  *addrStr ,
				  float *zipLat  ,
				  float *zipLon  ) ;

PlaceDesc *getPlaceDescBuf ();

// . IndDesc = Indicator Descriptor
// . indicators are words or phrases that indicate a possible Place
// . used for *scoring* all the Places we extract from the document
class IndDesc {
public:
	char m_bit;
	// the stronger the indicator, the higher the referenced score
	float m_indScore;
};


extern HashTableX g_indicators;

// values for PlaceDesc::m_flags:
#define PDF_CITY    0x01
#define PDF_STATE   0x02
#define PDF_COUNTRY 0x04
// does place use daylight savings time?
#define PDF_USE_DST 0x08


// . there are about 2.5 million cities in the world
// . every city, state and country in the world has its own PlaceDesc
class PlaceDesc {
public:
	// . PDF_CITY|PDF_COUNTRY|PDF_STATE
	// . only one flag can be set
	char m_flags;
	// one per country
	uint8_t m_crid;
	// population
	int32_t m_population;
	// the id assigned by the geonames ppl for looking up in
	// alternateNames.txt
	//int32_t m_geoId;
	// the lat lon of the place centroid
	float m_lat;
	float m_lon;
	// from -12 to + 12 i guess
	char  m_timeZoneOffset;
	// the official name. offset into g_pbuf/g_pbufSize
	int32_t  m_officialNameOffset;
	// the two letter state code
	char  m_adm1[2];

	char *getOfficialName ( ) ;
	char *getStateName ( ) ;
	const char *getCountryName ( ) ;

	// first is "oh," or or "nm," or "<adm1>," then its a
	// list like "us-fi-nl=egypt,de-es=egypti,."
	// that has all the names of the place
	//char *m_data;
};

PlaceDesc *getPlaceDesc ( uint64_t placeHash64 , 
			  uint8_t placeType ,
			  uint8_t crid,
			  char *stateAbbr,
			  int32_t niceness ) ;

PlaceDesc *getNearestCity_new ( float  lat , 
				float  lon , 
				int32_t   niceness ,
				float *distInMilesSquared ) ;

int64_t getWordXorHash ( char *s ) ;

#endif
