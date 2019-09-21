//-*- coding: utf-8 -*-

#include "Proxy.h"

class Address *g_address; // for debug

#define CRID_ANY 0
#define CRID_US  226

//
// if you have "in <city/adm1 name>" in same sentence as street then
// require that that item be a city/adm1 in any address you try to do.
// i would set "int64_t inPrepPhrase" to be the city/adm1 place hash. 
// so if it is not zero, check for it. but add it with addProperPlaces()
// first to see if it added anything!! then we can 
//

//and fix it so "1914" years and older years are pub dates!
//and inclide days of the week in pub dates like "sunday, april 11, 2004"
//too!!
//do not allow lower case 'or' in place name! 
//do not allow place names starting with "arrangements by" or "sponsored by"


// test on http://alibi.com/index.php?scn=cal
// test on http://www.burtstikilounge.com/burts/

// TODO: FOR ADDRESS overlap detection, just hash every word index for
//       every Place which can not be shared. then store the score and
//       Address ptr as the data value, so we can do a quick compare!

// TODO: also add conflicting addresses with the same score as winners.
//       if we can't resolve a winner then we should just eliminate both/all
//       to be on the safe side. like the alibi.com page has both albuquerque
//       and santa fe in the <title> tag so it is really just lucky that we
//       pick albuquerque most of the time... we might be able to bring in
//       street name to city map to help us fix this one. if both cities have
//       the same street name, then nuke both! any other ideas?

// TODO: for the abqjournal.com page we need to determine the most popular
//       city/adm1 pair over the whole page and use that as another default 
//       option. also consider if we should have several and score them...

// TODO: for all the phrases in "small" sections and all phrases following 
//       "at" or "at the" look those phrases up in placedb as place names
//       to get their addresses. also confirm the place names we extract
//       that are immediately before street names. also get all the possible
//       city/adm1/ctry tuples that each place name might have. if these
//       are not right next to it then i guess you need to get them from
//       the title and tagdb. that way the placedb lookup can integrate
//       the tuples into the key and greatly narrow the list. we may have
//       to then do multiple lookups for the same place name in placedb,
//       so another reason we should distribute them and keep them in memory
//       or at least on an SSD. use *namedb* to index place names just like
//       indexdb. then we can conduct a search for a place name on namedb
//       and get the corresponding keys of the place records in placedb.
//       namedb will need to be mostly in memory then! 

// TODO: verify street addresses we do extract by looking up each one in
//       placedb by the street. each street may have multiple city/adm1/ctry
//       tuples, so this lookup should narrow it down!

// test zipcode hyphen fix on abqjournal.com/contact.html

#include "gb-include.h"
#include "Address.h"
#include "Sections.h"
//#include "DateParse2.h"
#include "Abbreviations.h"
#include "Phrases.h"
//#include "Weights.h"
#include "XmlDoc.h" // hashWords()
#include "Hostdb.h"
#include "Placedb.h"
#include "sort.h"
#include "HttpServer.h"

//#define CF_UNIQUE (((uint64_t)1LL)<<63)

bool getBestLatLon ( RdbList *list      ,
		     double  *bestLat   ,
		     double  *bestLon   ,
		     int32_t    *numVotes  ,
		     int32_t     niceness  ,
		     int32_t     winnerSnh ) ;
char *getLatLonPtrFromStr ( char *data ) ;
void getLatLonFromStr ( char *data , double *lat , double *lon);
char *getStateAbbr ( uint64_t bit ) ;
int64_t getWordXorHash ( char *s ) ;
int64_t getWordXorHash2 ( char *s ) ;
int32_t getStateOffset ( int64_t *h ) ;
class StateDesc *getStateDescFromBits ( uint64_t bit ) ;
// returns 0 if not a state:
uint64_t getStateBitFromHash ( int64_t *h ) ;
static bool setHashes ( class Place *p , Words *ww , int32_t niceness ) ;

static bool    addIndicator ( char *s     , char bit , float boost );
static bool    addIndicator ( int64_t h , char bit , float boost );
//static void  printAddress ( class Address *A , class SafeBuf *pbuf , int32_t i);
static void    printPlaces  ( PlaceMem *pm , SafeBuf *pbuf ,
			      class Sections *sections ,
			      class Address *base ) ;
static bool getZipLatLon ( char  *zip    ,
			   int32_t   zipLen ,
			   float *zipLat ,
			   float *zipLon ) ;

//
// new stuff
//
static bool generatePlacesFile ( ) ;
static bool loadPlaces ( ) ;
class PlaceDesc *getState_new ( uint64_t pd64 , uint8_t crid , int32_t niceness );
PlaceDesc *getState2_new ( char *state , uint8_t crid , int32_t niceness ) ;
class PlaceDesc *getCity_new ( uint64_t ch64 , 
			       char *stateAbbr ,
			       uint8_t crid ,
			       int32_t niceness ) ;
class PlaceDesc *getCity2_new ( char *city ,
				char *stateAbbr ,
				uint8_t crid ,
				int32_t niceness ) ;
PlaceDesc *getCity3_new ( uint64_t ch64 , 
			  uint64_t stateHash64,
			  uint8_t crid ,
			  int32_t niceness ) ;
bool getLongestPlaceName_new ( int32_t i,
			       int32_t alnumPos,
			       Words *w,
			       // must match! PDF_CITY|STATE|COUNTRY
			       uint8_t placeType,
			       uint8_t crid, // can be CRID_ANY
			       char *stateAbbr, // can be NULL
			       uint64_t *placeHash64,
			       int32_t *placeAlnumA,
			       int32_t *placeAlnumB,
			       int32_t *placeA,
			       int32_t *placeB ,
			       // set to most popular match
			       PlaceDesc **pdp ) ;
bool getZip_new ( int32_t a , 
		  int32_t alnumPos , 
		  Words *words ,
		  uint64_t *zipHash64 ,
		  uint64_t *zipCityHash64 ,
		  uint64_t *zipStateHash64 ,
		  int32_t *zipAlnumA,
		  int32_t *zipAlnumB,
		  int32_t *zipA,
		  int32_t *zipB ,
		  float *zipLat,
		  float *zipLon) ;

PlaceDesc *getMostPopularPlace_new ( int64_t cityHash64, 
				     uint8_t crid ,
				     uint8_t placeType,
				     int32_t niceness );

char *g_pbuf = NULL;
int32_t  g_pbufSize = 0;
HashTableX g_nameTable;

char *PlaceDesc::getOfficialName ( ) {
	return g_pbuf + m_officialNameOffset; 
}

char *PlaceDesc::getStateName ( ) {
	// get our state abbr
	char buf[3];
	buf[0] = m_adm1[0];
	buf[1] = m_adm1[1];
	buf[2] = '\0';
	// does this convert to lowercase? yes... it should
	uint64_t placeHash64 = getWordXorHash ( buf );
	// look up the place desc for the state
	PlaceDesc *sd = getPlaceDesc ( placeHash64 ,
				       PDF_STATE,
				       m_crid,
				       buf, // state abbr
				       0 ); // niceness
	if ( ! sd ) return NULL;
	return sd->getOfficialName();
}

const char *PlaceDesc::getCountryName ( ) {
	return g_countryCode.getName ( m_crid );
}

HashTableX g_indicators;
static HashTableX g_timeZones;
static HashTableX g_cities;
static HashTableX g_states;
static HashTableX g_aliases;
static HashTableX g_zips;

char *g_cityBuf     = NULL;
int32_t  g_cityBufSize = 0;

// . NOW each slot in the g_cities has a ptr to a CityDesc in SafeBuf g_cityBuf
// . so now we can put all the alternate names and aliases into the same table
class CityDesc {
public:
	// set bit for each state that the city is in
	uint64_t m_adm1Bits;
	// for chicago, we would pick "13" since s_states[13] is illinois
	char     m_mostPopularState;
	// "us.nm,us.ny,es.a1,...|en-nl-fi=cincinnati,es-de=cincinnatus,..."
	char     m_data[];
};

//bool setFromStr(Address *a,char *s,pbits_t flags , 
//		Place *places , int32_t *np , int32_t maxPlaces, int32_t niceness );

static uint64_t  getAddressHash ( Place *street ,
				  Place *city   ,
				  Place *adm1   ,
				  Place *zip    ) ;

static void verifiedWrapper ( void *state ) ;
static void gotMsg2cReplyWrapper ( void *state , void *state2 ) ;
static void gotList2c            ( void *state , RdbList *xxx , Msg5 *yyy ) ;
static void sendBackAddress ( class State2c *st ) ;

Place *g_pa = NULL;

#define MIN_POP_COUNT 500

//#define MAX_STREETS 300
//#define MAX_PLACES  3500
// i raised from 15 to 25 since "Virginia Beach" city was not being picked up
// on socialmediabeach.com
#define MAX_CITIES  25
#define MAX_ADM1    80 // 1500
#define MAX_ZIPS    5

// stock g_zips with these zip code descriptors
class ZipDesc {
public:
	// . this is unique within the country code only
	// . see /gb/geo/geonames/admin1Codes.txt for the list
	// . remove the "CC." country code prefixing each 
	// . example from that file: "NL.09 Utrecht\n"
	char m_adm1[2];
	// a single byte country id (converted to from a 2 char country id)
	//uint8_t m_crid;
	// hash of the city it is in
	int64_t m_cityHash;
	// offset into g_cityBuf of the city name
	int32_t m_cityOffset;
	// now we use the adm1 bits since US-only now
	uint64_t m_adm1Bits;
	// lat/lon of centroid. for sorting by dist when user's zip is known
	float m_latitude;
	float m_longitude;

	//void reset() {m_crid = 0; m_adm1[0] = m_adm1[1] = 0;};
        void reset() {m_adm1Bits = 0;m_adm1[0]=0; m_adm1[1]=0;};
};


static char      *s_days[] = {
	"sunday",
	"monday",
	"tuesday",
	"wednesday",
	"thursday",
	"friday",
	"saturday",

	"sundays",
	"mondays",
	"tuesdays",
	"wednesdays",
	"thursdays",
	"fridays",
	"saturdays",
	NULL
};


static StateDesc s_states[] = {
		{"al","alabama","ala"},
		{"ak","alaska","alas"},
		{"az","arizona","ariz"},
		{"ar","arkansas","ark"},
		{"ca","california","calif"},
		{"co","colorado","colo"},
		{"ct","connecticut","conn"},
		{"de","delaware","del"},
		{"dc","district of columbia","d.c."},
		{"fl","florida","fla"},
		{"ga","georgia",NULL},
		{"hi","hawaii","h.i."},
		{"id","idaho","ida"},
		{"il","illinois","ill"},
		{"in","indiana","ind"},
		{"ia","iowa",NULL},
		{"ks","kansas","kan"},
		{"ky","kentucky",NULL},
		{"la","louisiana",NULL},
		{"me","maine",NULL},
		{"md","maryland",NULL},
		{"ma","massachusetts","mass"},
		{"mi","michigan","mich"},
		{"mn","minnesota","minn"},
		{"ms","mississippi","miss"},
		{"mo","missouri",NULL},
		{"mt","montana","mont"},
		{"ne","nebraska","nebr"},
		{"nv","nevada","nev"},
		{"nh","new hampshire","n.h."},
		{"nj","new jersey","n.j."},
		{"nm","new mexico","n.m."},
		{"ny","new york","n.y."},
		{"nc","north carolina","n.c."},
		{"nd","north dakota","n.d."},
		{"oh","ohio",NULL},
		{"ok","oklahoma","okla"},
		{"or","oregon","ore"},
		{"pa","pennsylvania","penn"},
		{"ri","rhode island","r.i."},
		{"sc","south carolina","s.c."},
		{"sd","south dakota","s.d."},
		{"tn","tennessee","tenn"},
		{"tx","texas","tex"},
		{"ut","utah",NULL},
		{"vt","vermont",NULL},
		{"va","virginia","virg"},
		{"wa","washington","wash"},
		{"wv","west virginia","w.v."},
		{"wi","wisconsin","wis"},
		{"wy","wyoming","wyo"}
};

#include "StopWords.h"
static HashTableX s_doyTable;
static bool       s_doyInit = false;
int32_t getDayOfWeek ( int64_t h ) {
	if ( ! s_doyInit ) {
		s_doyInit = initWordTable(&s_doyTable, s_days ,
					  //sizeof(s_days),
					  "doytbl");
		if ( ! s_doyInit ) return -1;
	} 
	// . get from table
	// . score should be 1 for sunday i guess
	int32_t score = s_doyTable.getScore ( &h );
	// make it 0-6
	score = (score-1) % 7;
	// that's it
	return score;
}		

// http://www.dailylobo.com/calendar/
// http://www.abqthemag.com/events.html
// http://www.abqjournal.com/calendar/default.php
// http://www.abqjournal.com/calendar/month.htm (243k! do not truncate!!)
// http://www.kasa.com/subindex/entertainment/events_calendar
// http://www.trumba.com/calendars/KRQE_Calendar.rss (rss)
// http://www.koat.com/calendar/index.html
// http://www.trumba.com/calendars/albuquerque-area-events-calendar.rss.
// http://www.google.com/calendar/embed?mode=AGENDA&height=700&wkst=1&bgcolor=%23FFFFFF&src=vn90mq4n30kodohqjv8cdn5cfg%40group.calendar.google.com&color=%237A367A
// http://www.krqe.com/subindex/features/events_calendar
// http://www.alibi.com/index.php?scn=cal
// http://www.publicbroadcasting.net/kunm/events.eventsmain
// http://www.publicbroadcasting.net/kunm/events.eventsmain?action=showCategoryListing&newSearch=true&categorySearch=4025
// http://www.770kob.com/article.asp?id=521586
// http://events.kgoradio.com/
// http://www.livenation.com/venue/journal-pavilion-tickets (journal pavilion)
// http://www.livenation.com/venue/kiva-auditorium-tickets
// http://events.kqed.org/events/
// http://www.sfbg.com/entry.php?entry_id=8401&catid=85&l=1
// http://events.sfgate.com/ (zvents.com)
// http://events.sfgate.com/search?cat=1
// http://entertainment.signonsandiego.com/search/?type=event
// http://www.sdcitybeat.com/cms/event/search/?menu=Events
// ** http://www.sandiegometro.com/calendar/arts.php

// address parsing test cases:
// http://yellowpages.superpages.com/listings.jsp?CS=L&MCBP=true&search=Find+It&SRC=&C=bicycles&STYPE=S&L=Albuquerque+NM+&x=0&y=0

// address examples:

// BRAZIL:
// Marina Costa e Silva
// Rua Afonso Canargo, 805
// Santana
// 85070-200 Guarapuava - PR

// University of New Mexico
// Department of Physics and Astronomy
// MSC07 4220
// 800 Yale Blvd NE
// Albuquerque, New Mexico 87131-0001 USA 

// US-380
// Lincoln, NM 
// Saturday, August 8, 2009 


static bool s_init = false;

Addresses::Addresses ( ) {
	m_buf     = NULL;
	m_bufSize = 0;
	m_calledGeocoder = false;
	m_xd = NULL;
	m_msg2c = NULL;
	m_sorted = NULL;
	m_sortedValid = false;
	m_breached = false;
	m_numValid = 0;
}

Addresses::~Addresses ( ) {
	reset();
}

void Addresses::reset ( ) {
	if ( m_buf && m_bufSize )
		mfree ( m_buf , m_bufSize , "adata");
	m_buf     = NULL;
	m_bufSize = 0;
	m_sb.purge();
	//m_ptValid = false;
	//m_msg2c.m_requests = 0;
	//m_msg2c.m_replies  = 0;
	m_firstBreach = true;
	m_breached = false;
	m_numValid = 0;
	m_calledGeocoder = false;
	if ( m_msg2c ) {
		mdelete ( m_msg2c , sizeof(Msg2c),"aamsg2c");
		delete (m_msg2c);
		m_msg2c = NULL;
	}
	// free buf
	if ( m_sorted )
		mfree ( m_sorted , m_sortedSize , "asortbuf");
	m_sorted = NULL;
	m_sortedValid = false;
	m_uniqueStreetHashes = 0;
}

static int64_t h_court;
static int64_t h_i;
static int64_t h_interstate;
static int64_t h_page    ;
static int64_t h_corner  ;
static int64_t h_between ;
static int64_t h_btwn    ;
static int64_t h_bet     ;
static int64_t h_streets ;
static int64_t h_sts     ;
static int64_t h_at      ;
static int64_t h_come    ;
static int64_t h_is      ;
static int64_t h_located ;
static int64_t h_intersection;
static int64_t h_law     ;
static int64_t h_address ;
static int64_t h_added   ;
static int64_t h_copy    ;
static int64_t h_search  ;
static int64_t h_find    ;
static int64_t h_go      ;
static int64_t h_town    ;
static int64_t h_city    ;
static int64_t h_street  ;
static int64_t h_telephone; 
static int64_t h_tel       ;
static int64_t h_ph       ;
static int64_t h_fax      ;
static int64_t h_where   ;
static int64_t h_location;
static int64_t h_venue   ;
static int64_t h_map     ;
static int64_t h_office  ;
static int64_t h_center  ;
static int64_t h_mailing ;
static int64_t h_mail    ;
static int64_t h_snail   ;
static int64_t h_edit    ;
static int64_t h_email   ;
static int64_t h_phone   ;
static int64_t h_inc     ;
static int64_t h_llc     ;
static int64_t h_review  ;
static int64_t h_reviews ;
static int64_t h_write   ;
static int64_t h_add          ; 
static int64_t h_view         ; 
static int64_t h_favorites    ; 
static int64_t h_more         ; 
static int64_t h_info         ; 
static int64_t h_information  ; 
static int64_t h_the          ; 
static int64_t h_in           ; 
static int64_t h_a            ; 
static int64_t h_paseo        ; 
static int64_t h_de           ; 
static int64_t h_del          ; 
static int64_t h_all          ; 
static int64_t h_rights       ; 
static int64_t h_reserved     ; 
static int64_t h_contact      ; 
static int64_t h_us           ; 
static int64_t h_by           ; 
static int64_t h_of           ; 
static int64_t h_for          ; 
static int64_t h_arrangements ; 
static int64_t h_arranged     ; 
static int64_t h_sponsored    ; 
static int64_t h_to        ; 
static int64_t h_every     ; 
static int64_t h_p         ; 
static int64_t h_b         ; 
static int64_t h_hwy       ; 
static int64_t h_state     ; 
static int64_t h_county    ; 
static int64_t h_cnty      ; 
static int64_t h_cty       ; 
static int64_t h_road      ; 
static int64_t h_route     ; 
static int64_t h_rte       ; 
static int64_t h_rt        ; 
static int64_t h_highway   ; 
static int64_t h_hiway     ; 
static int64_t h_cr        ; 
static int64_t h_o         ;
static int64_t h_po        ;
static int64_t h_post      ;
static int64_t h_box       ;
static int64_t h_top       ; 
static int64_t h_one       ; 
static int64_t h_noon      ; 
static int64_t h_midnight  ; 
static int64_t h_daily     ; 
static int64_t h_st        ; 
static int64_t h_nd        ; 
static int64_t h_rd        ; 
static int64_t h_th        ; 
static int64_t h_away      ; 
static int64_t h_results   ; 
static int64_t h_days      ; 
static int64_t h_blocks    ; 
static int64_t h_block     ; 
static int64_t h_miles     ; 
static int64_t h_mile      ; 
static int64_t h_year      ;
static int64_t h_years     ;
static int64_t h_yr        ;
static int64_t h_yrs       ;
static int64_t h_hours     ; 
static int64_t h_hrs       ; 
static int64_t h_hour      ; 
static int64_t h_hr        ; 
static int64_t h_mi        ; 
static int64_t h_kilometers; 
static int64_t h_km        ; 
static int64_t h_copyright ; 
static int64_t h_and       ; 
static int64_t h_or        ; 
static int64_t h_suite     ; 
static int64_t h_ste       ; 
static int64_t h_bldg      ; 
static int64_t h_bld       ; 
static int64_t h_building  ; 
static int64_t h_unit      ; 
static int64_t h_room      ; 
static int64_t h_pier      ; 
static int64_t h_rm        ; 
static int64_t h_run ;
static int64_t h_ne        ; 
static int64_t h_nw        ; 
static int64_t h_se        ; 
static int64_t h_sw        ; 
static int64_t h_n         ; 
static int64_t h_s         ; 
static int64_t h_e         ; 
static int64_t h_w         ; 
static int64_t h_north;
static int64_t h_northeast;
static int64_t h_northwest;
static int64_t h_east;
static int64_t h_west;
static int64_t h_south;
static int64_t h_southeast;
static int64_t h_southwest;
static int64_t h_heart ;
static int64_t h_core  ;
static int64_t h_least ;
static int64_t h_most  ;
static int64_t h_this  ;
static int64_t h_appeared  ;
static int64_t h_role  ;
static int64_t h_studied;
static int64_t h_prize;
static int64_t h_finish;
static int64_t h_door;
static int64_t h_entrance;
static int64_t h_area;
static int64_t h_left  ;
static int64_t h_right ;
static int64_t h_stare  ;
static int64_t h_sea  ;
static int64_t h_discount  ;
static int64_t h_discounted  ;
static int64_t h_www;
static int64_t h_gaze   ;
static int64_t h_look  ;
static int64_t h_looking;
static int64_t h_be ;
static int64_t h_determined ;
static int64_t h_call ;
static int64_t h_details;
static int64_t h_tba;
static int64_t h_avenue;
static int64_t h_ave;
static int64_t h_register;
static int64_t h_sign;
static int64_t h_up;
static int64_t h_signup;
static int64_t h_tickets;
static int64_t h_purchase;
static int64_t h_get;
static int64_t h_enroll;
static int64_t h_buy;
static int64_t h_presale ;
static int64_t h_pre ;
static int64_t h_sale ;
static int64_t h_on ;
static int64_t h_sales ;
static int64_t h_end ;
static int64_t h_begin ;
static int64_t h_start ;
static int64_t h_am;
static int64_t h_fm;

// . first identifies all the "Places" using the rules above
// . then clusters the "Places" together into an "Address"
// . we use the address at the top of the page, and the site contact info,
//   etc. to be defaults, so we can inherit, city, state, etc. from those
// . returns false if blocked, true otherwise. sets g_errno on error.
bool Addresses::set ( Sections  *sections    ,
		      Words     *words       ,
		      Bits      *bits        ,
		      TagRec    *gr          ,
		      Url       *url         ,
		      int64_t  docId       ,
		      //char      *coll        ,
		      collnum_t collnum ,
		      int32_t       domHash32   ,
		      int32_t       ip          ,
		      //int32_t       tagPairHash ,
		      int32_t       niceness    ,
		      SafeBuf   *pbuf        ,
		      void      *state       ,
		      void     (*callback) (void *state) ,
		      uint8_t    contentType ,
		      // from XmlDoc::ptr_addressReply in a title rec
		      //char      *addressReply      ,
		      //int32_t       addressReplySize  ,
		      //bool       addressReplyValid ,
		      char      *siteTitleBuf     ,
		      int32_t       siteTitleBufSize ,
		      XmlDoc    *xd ) {

	reset();

	// save stuff
	m_xd          = xd;
	m_sections    = sections;
	m_words       = words;
	m_wptrs       = words->m_words;
	m_wlens       = words->m_wordLens;
	m_nw          = words->m_numWords;
	m_wids        = words->getWordIds();
	m_tids        = words->getTagIds();
	m_bits        = bits;
	m_gr          = gr;
	m_url         = url;
	m_docId       = docId;
	m_collnum        = collnum;
	m_domHash32   = domHash32;
	m_ip          = ip;
	//m_tagPairHash = tagPairHash;
	m_niceness    = niceness;
	m_pbuf        = pbuf;
	m_state       = state;
	m_callback    = callback;
	m_contentType = contentType;

	//m_addressReply      = addressReply;
	//m_addressReplySize  = addressReplySize;
	//m_addressReplyValid = addressReplyValid;

	m_siteTitleBuf     = siteTitleBuf;
	m_siteTitleBufSize = siteTitleBufSize;

	m_sb.purge();

	static bool s_setHashes = false;
	if ( ! s_setHashes ) {
		// flag it
		s_setHashes = true;
		// int16_tcuts
		h_i    = hash64n ("i");
		h_court      = hash64n ("court");
		h_interstate = hash64n ("interstate");
		h_page    = hash64n ("page");
		h_corner  = hash64n ("corner");
		h_between = hash64n ( "between");
		h_btwn    = hash64n ( "btwn");
		h_bet     = hash64n ( "bet");
		h_streets = hash64n ( "streets");
		h_sts     = hash64n ( "sts");
		h_at      = hash64n ( "at"   );
		h_come    = hash64n ("come");
		h_is      = hash64n ( "is" );
		h_located = hash64n ( "located" );
		h_intersection = hash64n("intersection");
		h_law     = hash64 ( "law"  ,3);
		h_address = hash64 ( "address",7);
		h_added   = hash64 ( "added",5);
		h_copy    = hash64 ( "copy",4);
		h_search  = hash64 ( "search",6);
		h_find    = hash64 ( "find",4);
		h_go      = hash64 ( "go",2);
		h_town    = hash64n ( "town");
		h_city    = hash64n ( "city");
		h_street  = hash64 ( "street",6);
		h_telephone = hash64 ( "telephone",9);
		h_tel       = hash64 ( "tel",3);
		h_ph       = hash64 ( "ph",2);
		h_fax       = hash64 ( "fax",3);
		h_where   = hash64 ( "where",5);
		h_location= hash64 ( "location",8);
		h_venue   = hash64n("venue");
		h_map     = hash64 ( "map"  ,3);
		h_office  = hash64 ( "office"  ,6);
		h_center  = hash64n ("center");
		h_mailing = hash64 ( "mailing"  ,7);
		h_mail    = hash64 ( "mail"  ,4);
		h_snail   = hash64 ( "snail"  ,5);
		h_edit   = hash64 ( "edit"  ,4);
		h_email  = hash64 ( "email"  ,5);
		h_phone   = hash64 ( "phone"  ,5);
		h_inc     = hash64 ( "inc"  ,3);
		h_llc     = hash64 ( "llc"  ,3);
		h_review  = hash64 ( "review"  ,6);
		h_reviews = hash64 ( "reviews"  ,7);
		h_write   = hash64 ( "write", 5);
		h_add = hash64 ( "add",3 );
		h_view = hash64 ( "view", 4);
		h_favorites = hash64 ( "favorites", 9);
		h_more = hash64 ( "more",4 );
		h_info = hash64 ( "info",4 );
		h_information = hash64 ( "information", 11);
		h_the     = hash64 ( "the"  ,3);
		h_in      = hash64 ( "in"  ,2);
		h_a       = hash64 ( "a"  ,1);
		h_paseo       = hash64n ( "paseo");
		h_de       = hash64n ( "de");
		h_del       = hash64n ( "del");
		h_all      = hash64 ( "all"      ,3);
		h_rights   = hash64 ( "rights"   ,6);
		h_reserved = hash64 ( "reserved" ,8);
		h_contact  = hash64 ( "contact" , 7);
		h_us       = hash64 ( "us" , 2);
		h_by           = hash64 ( "by"  ,2);
		h_of           = hash64 ( "of"  ,2);
		h_for          = hash64 ( "for"  ,3);
		h_arrangements = hash64("arrangements",12);
		h_arranged     = hash64("arranged",8);
		h_sponsored    = hash64("sponsored",9);
		h_to    = hash64 ( "to"   ,2);
		h_every   = hash64 ( "every",5);
		h_p       = hash64 ( "p"    ,1);
		h_b       = hash64n ( "b" );
		h_hwy     = hash64 ( "hwy"      ,3);
		h_state   = hash64 ( "state"      ,5);
		h_county  = hash64 ( "county" , 6 );
		h_cnty    = hash64 ( "cnty" , 4 );
		h_cty     = hash64 ( "cty" , 3 );
		h_road    = hash64 ( "road"      ,4);
		h_route   = hash64 ( "route"      ,5);
		h_rte     = hash64 ( "rte"      ,3);
		h_rt      = hash64 ( "rt"      ,2);
		h_highway = hash64 ( "highway"  ,7);
		h_hiway   = hash64 ( "hiway"  ,5);
		h_cr = hash64 ( "cr"  ,2);
		h_o       = hash64 ( "o"    ,1);
		h_po      = hash64 ( "po"   ,2);
		h_post    = hash64 ( "post"   ,4);
		h_box     = hash64 ( "box"  ,3);
		h_top     = hash64n ( "top" );
		h_one     = hash64 ( "one"    ,3);
		h_noon     = hash64n ( "noon" );
		h_midnight = hash64n ( "midnight" );
		h_daily    = hash64n ( "daily" );
		h_st     = hash64 ( "st"    ,2);
		h_nd     = hash64 ( "nd"    ,2);
		h_rd     = hash64 ( "rd"    ,2);
		h_th     = hash64 ( "th"    ,2);
		h_away    = hash64 ( "away" ,4);
		h_results = hash64 ( "results" , 7 );
		h_days      = hash64 ( "days", 4 );
		h_blocks    = hash64 ( "blocks",6);
		h_block     = hash64 ( "block",5);
		h_miles     = hash64 ( "miles",5);
		h_mile      = hash64n ( "mile");
		h_year      = hash64n("year");
		h_years     = hash64n("years");
		h_yr        = hash64n("yr");
		h_yrs       = hash64n("yrs");
		h_hours     = hash64 ( "hours",5);
		h_hrs       = hash64 ( "hrs",3);
		h_hour      = hash64n ( "hour");
		h_hr        = hash64n ( "hr");
		h_mi        = hash64 ( "mi",2);
		h_kilometers= hash64 ( "kilometers",10);
		h_km        = hash64 ( "km",2);
		h_copyright = hash64 ( "copyright",9);
		h_and = hash64 ( "and" , 3 );
		h_or  = hash64 ( "or"  , 2 );
		h_suite    = hash64 ( "suite",5);
		h_ste      = hash64 ( "ste",3);
		h_bldg     = hash64 ( "bldg",4);
		h_bld     = hash64n ( "bld");
		h_building = hash64 ( "building",8);
		h_unit     = hash64 ( "unit",4);
		h_room     = hash64 ( "room",4);
		h_pier     = hash64 ( "pier",4);
		h_rm       = hash64 ( "rm",2);
		h_run      = hash64n ("run");
		h_ne      = hash64 ( "ne" ,2);
		h_nw      = hash64 ( "nw" ,2);
		h_se      = hash64 ( "se" ,2);
		h_sw      = hash64 ( "sw" ,2);
		h_n       = hash64 ( "n" ,1);
		h_s       = hash64 ( "s" ,1);
		h_e       = hash64 ( "e" ,1);
		h_w       = hash64 ( "w" ,1);
		h_north     = hash64n("north");
		h_south     = hash64n("south");
		h_east      = hash64n("east");
		h_west      = hash64n("west");
		h_northeast = hash64n("northeast");
		h_northwest = hash64n("northwest");
		h_southeast = hash64n("southeast");
		h_southwest = hash64n("southwest");
		h_heart = hash64n ( "heart" );
		h_core  = hash64n ( "core" );
		h_least = hash64n ( "least" );
		h_most  = hash64n ( "most" );
		h_this  = hash64n ( "this" );
		h_north  = hash64n ( "north" );
		h_south  = hash64n ( "south" );
		h_east   = hash64n ( "east" );
		h_west   = hash64n ( "west" );
		h_appeared = hash64n ( "appeared" );
		h_role = hash64n ( "role" );
		h_studied = hash64n ( "studied" );
		h_prize = hash64n ( "prize" );
		h_finish = hash64n("finish");
		h_door = hash64n("door");
		h_entrance = hash64n("entrance");
		h_area = hash64n("area");
		h_left   = hash64n ( "left" );
		h_right  = hash64n ( "right" );
		h_stare = hash64n ( "stare" );
		h_sea = hash64n ( "sea" );
		h_discount = hash64n("discount");
		h_discounted = hash64n("discounted");
		h_www        = hash64n("www");
		h_gaze  = hash64n ( "gaze" );
		h_look  = hash64n ( "look" );
		h_looking  = hash64n ( "looking" );
		h_be = hash64n("be");
		h_determined = hash64n("determined");
		h_call = hash64n("call");
		h_details = hash64n("details");
		h_tba = hash64n("tba");
		h_avenue = hash64n("avenue");
		h_ave = hash64n("ave");

		h_register = hash64n("register");
		h_sign = hash64n("sign");
		h_up = hash64n("up");
		h_signup = hash64n("signup");
		h_tickets = hash64n("tickets");
		h_purchase = hash64n("purchase");
		h_get = hash64n("get");
		h_enroll = hash64n("enroll");
		h_buy = hash64n("buy");
		h_presale = hash64n("presale");
		h_pre = hash64n("pre");
		h_sale = hash64n("sale");
		h_on = hash64n("on");
		h_sales = hash64n("sales");
		h_end   = hash64n("end");
		h_begin = hash64n("begin");
		h_start = hash64n("start");
		h_am = hash64n("am");
		h_fm = hash64n("fm");
	}

	//m_msg2c.m_mcast.reset();
	// sanity check -- did set2() corrupt our junk?
	//if ( m_msg2c.m_mcast.m_ownMsg && m_msg2c.m_mcast.m_msgSize > 5000 ){ 
	//	char *xx=NULL;*xx=0; }
	// returns false and sets g_errno on error
	bool status = set2 ( );
	// sanity check -- did set2() corrupt our junk?
	//if ( m_msg2c.m_mcast.m_ownMsg && m_msg2c.m_mcast.m_msgSize > 5000 ){ 
	//	char *xx=NULL;*xx=0; }
	// sanity check
	if ( ! status && ! g_errno ) { char *xx=NULL;*xx=0; }
	// return true on error now
	if ( ! status ) return true;

	// . ok, go no further if from msg13
	// . it will have to check m_good or something, not m_valid
	if ( ! m_sections ) return true;

	// if valid and empty, we are done
	//if ( m_addressReplyValid && ! m_addressReply ) return true;

	/*
	  -- mdw took this out because it had too many false positives. often
	     the place name 1 and/or 2 was wrong and was calling nonsense a 
	     place! for many urls... and now that i removed the
	     SEC_CONTENDED_ADDRESS algo all the events on a page even if
	     different tag hashes, can share the same address. to replace
	     that algo i am ignore events with SEC_TITLE_OUTLINKED if the
	     event title is an outlink to another page, and also i am trying
	     to identify all place names in events. this outlinked bit should
	     fix the http://www.zvents.com/albuquerque-nm/events/show/88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer url, since it has a
	     little section that has "You may Also Like..." for events at
	     different venues, mentioned by name.
	//
	// . SELF-VERIFICATION LOOPS
	//
	// . now use the addresses that were inlined to verify those
	//   that were not inlined, assuming the place name matches
	// . this will allow "The Filling Station" to be verified in 
	//   http://www.zvents.com/albuquerque-nm/events/show/
	//   88543421-the-love-song-of-j-robert-oppenheimer-by-carson-kreitzer
	// . first scan the addresses for inlined ones
	// . logic taken basically from hashForPlacedb()
	//
	// init the table
	HashTableX pt;
	// returns true with g_errno set on error
	if ( ! pt.set ( 8,4,256,NULL,0,false,m_niceness) ) return true;

	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// get it
		Address *a = &m_addresses[i];
		// must be inlined
		if ( ! ( a->m_flags & AF_INLINED ) ) continue;
		// sometimes a street can exist in two cities or states
		if ( a->m_flags & AF_AMBIGUOUS ) continue;
		// must not have a place name in place of the street name
		if ( a->m_street.m_flags2 & PLF2_IS_NAME ) continue;
		// hash into table only if valid
		int64_t h1 = a->m_name1.m_hash;
		// adjust it since setHashes() xors in 0x123456 for street
		// names that are actually place names in disguise
		h1 ^= 0x123456;
		// incorporate the adm1 and city and ctry
		h1 = hash64 ( a->m_city.m_hash , h1 );
		h1 = hash64 ( a->m_adm1.m_hash , h1 );
		h1 = hash64 ( a->m_ctry.m_hash , h1 );
		// put it in
		if ( a->m_name1.m_strlen && ! pt.addKey ( (char *)&h1, &a ) ) 
			return true;
		// same for second place name
		int64_t h2 = a->m_name2.m_hash;
		// adjust it since setHashes() xors in 0x123456 for street
		// names that are actually place names in disguise
		h2 ^= 0x123456;
		// incorporate the adm1 and city and ctry
		h2 = hash64 ( a->m_city.m_hash , h2 );
		h2 = hash64 ( a->m_adm1.m_hash , h2 );
		h2 = hash64 ( a->m_ctry.m_hash , h2 );
		// hash into table only if valid
		if ( a->m_name2.m_strlen && ! pt.addKey ( (char *)&h2, &a ) ) 
			return true;
	}		

	// now scan our addresses that have a place name in place of
	// the street name and see if we can get a match
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// get it
		Address *a = &m_addresses[i];
		// we want a place name in place of the street name now
		if ( ! ( a->m_street.m_flags2 & PLF2_IS_NAME ) ) continue;
		// . USE the STREET here, not the name
		// . it should already have had the 0x123456 xor'ed in
		//   in the logic below because PLF2_IS_NAME is set.
		int64_t h1 = a->m_street.m_hash;
		// incorporate the adm1 and city and ctry
		h1 = hash64 ( a->m_city.m_hash , h1 );
		h1 = hash64 ( a->m_adm1.m_hash , h1 );
		h1 = hash64 ( a->m_ctry.m_hash , h1 );
		// note it
		//logf(LOG_DEBUG,"add: lookuphash=%"XINT64"",a->m_street.m_hash);
		// test that
		//if ( a->m_street.m_hash == 0x14a2446f2d5a2647LL ) {
		//	setHashes ( &a->m_street );
		//	logf(LOG_DEBUG,"Add: had=%"XINT64"",a->m_street.m_hash);
		//}
		// get hash of street, i.e. hash of name
		// see if we have that in the table
		int32_t slot = pt.getSlot ( &h1 );
		// skip if not there
		if ( slot < 0 ) continue;
		// kewl, we got a match, get the matching address
		Address *ma = *(Address **)pt.getValueFromSlot ( slot );
		//
		// . now use it, i.e. replace ourselves with its info
		// . this logic is from above.
		//
		// int16_tcuts
		Place *name1  = &a->m_name1;
		Place *street = &a->m_street;
		// street name was name1
		gbmemcpy ( name1 , street , sizeof(Place) );
		// and set the street to what it should be
		street->m_str    = ma->m_street.m_str;
		street->m_strlen = ma->m_street.m_strlen;
		// let it fly
		a->m_flags |= AF_VERIFIED_STREET;
		a->m_flags |= AF_VERIFIED_STREET_NUM;
		// do not verify place name though!
		a->m_flags |= AF_VERIFIED_PLACE_NAME_1;
		// so set hashes makes its own words class
		street->m_a = -1;
		street->m_b = -1;
		// clear these, since PLF2_IS_NAME should be clear for us!!
		// otherwise it causes setHashes() function below to set
		// our hash as if we were a place name!!!
		street->m_flags2 = 0;
		// compute the street hash
		// Events.cpp relies on this to make substitutions to places
		// that have verified place names
		setHashes(street);
		// and in case hashForPlacedb() is called on us we
		// have to tell it to not hash us!! so put flag back!!
		street->m_flags2 |= PLF2_IS_NAME;
	}
	// free mem
	pt.reset();
	//
	// END SELF-VERIFICATION LOOPS
	//
	*/

	// update status
	if ( m_xd ) // && ! m_addressReplyValid ) 
		m_xd->setStatus ( "consulting placedb" );

	// make a msg2c first
	try { m_msg2c = new (Msg2c); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("addr: msg2c: new(%"INT32"): %s", (int32_t)sizeof(Msg2c), 
		    mstrerror(g_errno));
		// return true on error with g_errno set
		return true;
	}
	mnew ( m_msg2c , sizeof(Msg2c) , "aamsg2c" );

	// use niceness 0 if we are a turk injecting
	/*
	int32_t niceness2 = m_niceness;
	if ( m_xd->m_oldsrValid &&
	     m_xd->m_oldsr.m_isInjecting &&
	     m_xd->m_oldsr.m_isPageInject )
		niceness2 = 0;

	if ( m_xd->m_oldsrValid &&
	     m_xd->m_oldsr.m_isInjecting &&
	     m_xd->m_oldsr.m_isPageReindex )
		niceness2 = 0;
	*/

	// rather than look up stuff in placedb, if we have m_addressReply
	// provided, then that data represents placedb when we first
	// indexed this titleRec and we need to use that to ensure
	// parsing consistency
	if ( //! m_addressReplyValid && 
	    ! m_msg2c->verifyAddresses ( this         ,
					 m_collnum       ,
					 m_domHash32 ,
					 m_ip         ,
					 m_niceness   ,
					 this         ,
					 verifiedWrapper ) )
			return false;

	// . update addresses from the table
	// . returns false and sets g_errno on error
	updateAddresses ( );
	// all done
	return true;
}

void verifiedWrapper ( void *state ) {
	// get us
	Addresses *THIS = (Addresses *)state;
	// update addresses from replies
	if ( ! g_errno ) THIS->updateAddresses();
	// try this now. return if it blocked
	//if ( ! g_errno && ! THIS->getGeocoderLatLon() ) return;
	// call callback
	THIS->m_callback ( THIS->m_state );
}

Address *g_aa = NULL;

// . return false with g_errno set on error
// . take the msg2c replies we got in m_sb.m_buf or in m_addressReply,
//   which is a save of m_sb.m_buf in the titleRec (XmlDoc), and use
//   those replies to set Address::m_flags bits.
// . also use those replies to update the place names in your addresses
//   to verified place names
bool Addresses::updateAddresses ( ) {
	// bail on error
	if ( g_errno ) return false;

	// sanity check - i think 
	//if 

	// loop over replies in the replyBuf
	char *p    = m_sb.getBufStart();
	char *pend = p + m_sb.length();

	// . but use this buffer from title rec if valid though
	// . this will ensure parsing consistency
	//if ( m_addressReplyValid ) {
	//	p    =     m_addressReply;
	//	pend = p + m_addressReplySize;
	//}

	// loop over the msg2c replies
	for ( ; p < pend ; ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// parse this reply
		int32_t  addrNum   = *(int32_t *)p; p += 4;
		int32_t  replySize = *(int32_t *)p; p += 4;
		char *reply     = p; p += replySize;
		// sanity check
		if ( addrNum >= m_am.getNumPtrs() ) { char *xx=NULL;*xx=0;}
		if ( addrNum < 0                  ) { char *xx=NULL;*xx=0;}
		// skip if none!
		if ( replySize == 0 ) continue;
		// sanity check... why was this here? it was coring for
		// a bunch of suites in 500 marquette ave.
		//if ( replySize > 3000 ) { char *xx=NULL;*xx=0; }
		if ( replySize > 5000 ) 
			logf(LOG_DEBUG,"addr: got large addr reply of %"INT32" "
			     "bytes",replySize);
		// sanity check
		if ( replySize < 0 ) { char *xx=NULL;*xx=0; }
		// sanity check
		if ( p > pend ) { char *xx=NULL;*xx=0; }
		// int16_tcut
		Address *a = (Address *)m_am.getPtr(addrNum);
		// make sure never got a reply for this
		if ( a->m_flags & AF_GOT_REPLY ) { char *xx=NULL;*xx=0; }
		// mark it
		a->m_flags |= AF_GOT_REPLY;

		// . parse it up
		// . both reply types now have this same header
		char *p = reply; // + 1;
		// # of voters for the following lat/lon
		int32_t numVotes = *(int32_t *)p; p += 4;
		// then the lat lon
		double lat = *(double *)p; p += sizeof(double);
		double lon = *(double *)p; p += sizeof(double);
		// sanity check
		if ( p > reply + replySize ) { char *xx=NULL;*xx=0; }
		// do not confuse with a->m_latitude/m_longitude 
		// because we do not want to re-serialize these back
		// into the placedb record voting framework that
		// would create some kind of feedback loop
		a->m_importedLatitude  = lat;
		a->m_importedLongitude = lon;
		a->m_importedVotes     = numVotes;
		
		// is the street really a place name (Tingley Colesium)
		char isName = ( a->m_street->m_flags2 & PLF2_IS_NAME );
		// deal with normal case
		if ( ! isName ) {
			// must be one byte
			//if ( replySize != 1 ) { char *xx=NULL;*xx=0; }
			// or in the flags
			a->m_flags |= *p; p++; // *reply;
			// then the alternate placedb names
			char *str = p;
			// set end
			char *replyEnd = reply + replySize;
			// and now we have a list of score/names separated
			// by \0's
			a->m_placedbNames    = str;
			a->m_placedbNamesEnd = replyEnd;
			// assume no best
			a->m_bestPlacedbName = NULL;
			// max score
			int32_t max = 0;
			// set the best one
			for ( ; ; str += gbstrlen(str) + 1 ) {
				// stop if that was it
				if ( str >= replyEnd ) break;
				// get score
				int32_t vote = *(int32_t *)str;
				// skip vote
				str += 4;
				// skip if not max
				if ( vote <= max ) continue;
				// set max
				max = vote;
				// got new max
				a->m_bestPlacedbName = str;
			}
			// if no, best, make this null too
			if ( ! a->m_bestPlacedbName ) a->m_placedbNames = NULL;
			// all done integrating this reply
			continue;
		}
		// if the address parser changes a lot of times the addrNum
		// is incorrect, so really we should do it by the unique
		// hash of the entire string
		//if ( replySize == 1 ) {
		//	log("addr: addr num out of sync with addr data. "
		//	    "addr parser change and was not versioned.");
		//	continue;
		//}
		//if ( replySize == 1 ) { char *xx=NULL;*xx=0; }
		// parse out street from reply (name1;name2;suite;street;...)
		char *sp = p; // reply;
		// reset  count
		int32_t scount = 0;
		char *replyEnd = reply+replySize;
		// advance
		for ( ; sp < replyEnd && scount < 3 ; sp++ )
			if ( *sp == ';' ) scount++;
		// crazy! must be the street
		if ( ! *sp ) {
			// print it out
			log("addr: no street for %s",p);
			//char *xx=NULL;*xx=0; }
			g_errno = EBADENGINEER;
			return false;
		}
		// get end
		char *spend = sp;
		// advance to next ;
		for ( ; *spend && *spend != ';' ; spend++ );
		// sanity check
		if ( ! *spend ) { 
			// print it out
			log("addr: no street end for %s",p);
			//char *xx=NULL;*xx=0; }
			g_errno = EBADENGINEER;
			return false;
		}

		// int16_tcuts
		//Place *name1  = a->m_name1;
		//Place *street = a->m_street;
		// now we just ptr swap
		a->m_name1 = a->m_street;
		// make that street reference this address then
		// i guess we are supplanting the Place::m_address setting
		// logc below here
		a->m_name1->m_address = a;
		// but we need a new street place
		//if ( m_np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }
		Place *street = (Place *)m_pm.getMem(sizeof(Place));
		if ( ! street ) return false; 
		a->m_street = street;
		// street name was name1
		//gbmemcpy ( name1 , street , sizeof(Place) );
		// and set the street to what it should be
		street->m_str    = sp;
		street->m_strlen = spend - sp;
		// this means from placedb i guess... HACK!
		street->m_bits |= PLF_FROMTAG;//|PLF_FROMTITLE;
		// let it fly
		a->m_flags |= AF_VERIFIED_STREET;
		a->m_flags |= AF_VERIFIED_STREET_NUM;
		a->m_flags |= AF_VERIFIED_PLACE_NAME_1;
		// so set hashes makes its own words class
		street->m_a = -1;
		street->m_b = -1;
		// clear these, since PLF2_IS_NAME should be clear for us!!
		// otherwise it causes setHashes() function below to set
		// our hash as if we were a place name!!!
		street->m_flags2 = 0;
		// fix this before doing hash, otherwise setHashes() is wrong
		street->m_type = PT_STREET;
		// compute the street hash
		// Events.cpp relies on this to make substitutions to places
		// that have verified place names
		setHashes(street, m_words, m_niceness );
		// and in case hashForPlacedb() is called on us we
		// have to tell it to not hash us!! so put flag back!!
		street->m_flags2 |= PLF2_IS_NAME;
		// . what is this then??
		// . we use this for setting the lat/lon, etc. 
		a->m_hash = getAddressHash ( a->m_street,
					     a->m_city,
					     a->m_adm1,
					     a->m_zip );
		//if ( m_np < MAX_PLACES ) continue;
		//log("addr: hit np limit");
		//break;
	}

	Section **sp = m_sections->m_sectionPtrs;
	//
	// . auto verify place names if in <eventVenue> tag
	// . supports injection of our xml format
	//
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *aa = (Address *)m_am.getPtr(i);
		// get place name
		Place *name1 = aa->m_name1;
		// skip if none
		if ( ! name1 ) continue;
		// now we always set this so we can make it a turk
		// venue candidate
		name1->m_unverifiedAddress = aa;
		// set this too!
		if ( aa->m_name2 ) aa->m_name2->m_unverifiedAddress = aa;
		// get word pos
		int32_t a = name1->m_a;
		// skip if not in doc
		if ( a < 0 ) continue;
		// get section its in
		Section *ns = sp[a];
		// go up if sentence or implied
		for ( ; ns ; ns = ns->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// need a tag
			if ( m_tids[ns->m_a] ) break;
		}
		// stop if not in tag at all
		if ( ! ns ) continue;
		// get tag word then
		a = ns->m_a;
		// get tagid, must be xml
		if ( m_tids[a] != TAG_XMLTAG ) continue;
		// get tag name
		if ( ! strncasecmp(m_wptrs[a],"<eventVenue",11) )
			// it's a match!
			aa->m_flags |= AF_VERIFIED_PLACE_NAME_1;
	}


	/*
	// loop over all addresses
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// get address
		Address *a = &m_addresses[i];
		// get the reply byte
		char *replyFlags = (char *)m_avt.getValue(&a->m_avtKey);
		// skip if not there
		if ( ! replyFlags ) continue;
		// grab em
		a->m_flags |= *replyFlags;
		// skip if not ambiguous
		//if ( ! ( a->m_flags & AF_AMBIGUOUS ) ) continue;
		// needs to have verified at least the street/city/ctry
		//if ( ! ( a->m_flags & AF_VERIFIED_STREET ) ) continue;
		// ok, remove the ambiguous flag
		//a->m_flags &= ~AF_AMBIGUOUS;
	}
	*/


	// . now re-set the AF_AMBIGUIOUS flags
	// . we do this again now that we have set a lot of Address::m_flags
	//   like AF_VERIFIED_PLACE_NAME_1 etc from the msg2c replies
	//   (or msg2c replies saved in the titleRec/XmlDoc)
	setAmbiguousFlags();

	// keep count if unique street hashes
	int32_t count = 0;
	// keep a table
	char tmp[5000];
	HashTableX ds; ds.set(8,0,300,tmp,5000,false,m_niceness,"addr-strhsh");
	// count how many distinct street hashes we have
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get address
		Address *a = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// get street hash
		int64_t sh = a->m_street->m_hash;
		// skip if already got
		if ( ds.isInTable ( &sh ) ) continue;
		// add it. i guess ignore if on error
		if ( ! ds.addKey ( &sh ) ) return false;
		// count it
		count++;
	}		
	// set it
	m_uniqueStreetHashes = count;

	// int16_tcuts
	int32_t x , y;
	wbit_t *bits = m_bits->m_bits;
	unsigned char vflags = 0;
	vflags |= AF_VERIFIED_STREET;
	vflags |= AF_VERIFIED_PLACE_NAME_1;
	vflags |= AF_VERIFIED_PLACE_NAME_2;
	vflags |= AF_INLINED;
	// now that we have verified the addresses, set the D_IS_IN_ADDRESS
	// bit for those words in verified addresses... but only for
	// words in verified portions or any portion of an inlined address
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *a = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// must have something verified or be inlined
		if ( ! ( a->m_flags & vflags ) ) continue;
		// is it inlined
		bool inlined = (a->m_flags & AF_INLINED);
		// . even if inlined, if its a "fake" street it
		//   needs to be verified
		// . fixes "RAFFLE ... Rio Rancho NM" for trumba.com which
		//   thought that "RAFFLE" was a "street" and we ended up
		//   setting D_IS_IN_ADDRESS for it, and then in Events.cpp
		//   it got demoted for being a title even though it was
		//   part of the actual event title!
		if ( inlined && (a->m_street->m_flags2 & PLF2_IS_NAME) )
			inlined = false;
		// get flags
		if ( inlined || (a->m_flags & AF_VERIFIED_STREET) ) {
			// loop over words in street
			x = a->m_street->m_a;
			y = a->m_street->m_b;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			for ( ; x >= 0 && x < y ; x++ ) 
				bits[x] |= D_IS_IN_ADDRESS;
		}
		// now all place names must be verified only to avoid
		// false positives in the event title scoring algo
		if ( a->m_name1 ){//(a->m_flags & AF_VERIFIED_PLACE_NAME_1) ) {
			// loop over words in street
			x = a->m_name1->m_a;
			y = a->m_name1->m_b;
			// verified or not?
			wbit_t af ;
			if ( a->m_flags & AF_VERIFIED_PLACE_NAME_1 )
				af = D_IS_IN_VERIFIED_ADDRESS_NAME;
			else
				af = D_IS_IN_UNVERIFIED_ADDRESS_NAME;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			if ( ! a->m_name1->m_str ) { x = 0; y = 0; }
			for ( ; x >= 0 && x < y ; x++ ) 
				bits[x] |= af;//D_IS_IN_VERIFIED_ADDRESS_NAME;
		}
		if ( (a->m_flags & AF_VERIFIED_PLACE_NAME_2) ) {
			// loop over words in street
			x = a->m_name2->m_a;
			y = a->m_name2->m_b;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			if ( ! a->m_name2->m_str ) { x = 0; y = 0; }
			for ( ; x >= 0 && x < y ; x++ ) 
				bits[x] |= D_IS_IN_VERIFIED_ADDRESS_NAME;
		}
		// suite
		if ( a->m_suite ) {
			x = a->m_suite->m_a;
			y = a->m_suite->m_b;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			//if ( ! a->m_suite->m_str ) { x = 0; y = 0; }
			for ( ; x >= 0 && x < y ; x++ ) 
				bits[x] |= D_IS_IN_ADDRESS;
		}

		// verified if anything was
		if ( a->m_city ) {
			x = a->m_city->m_a;
			y = a->m_city->m_b;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			for ( ; x>= 0 && x < y ; x++ ) 
				bits[x] |= D_IS_IN_ADDRESS;
		}

		if ( a->m_adm1 ) {
			x = a->m_adm1->m_a;
			y = a->m_adm1->m_b;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			for ( ; x >= 0 && x < y ; x++ ) 
				bits[x] |= D_IS_IN_ADDRESS;
		}

		// zip
		if ( a->m_zip ) {
			x = a->m_zip->m_a;
			y = a->m_zip->m_b;
			if ( y > m_nw ) { char *xx=NULL;*xx=0; }
			//if ( ! a->m_zip->m_str ) { x = 0; y = 0; }
			for ( ; x >= 0 && x < y ; x++ ) 
				bits[x] |= D_IS_IN_ADDRESS;
		}		
	}

	/////////////////////////////////////
	//
	// hash the words in such address names into this hash table, name tble
	//
	/////////////////////////////////////
	HashTableX nt1;
	//HashTableX nt2;
	HashTableX nt3;
	char ntbuf1[5000];
	//char ntbuf2[5000];
	char ntbuf3[5000];
	nt1.set ( 8,8,256,ntbuf1,5000,true,m_niceness,"addr-nt1");
	//nt2.set ( 8,4,256,ntbuf2,5000,true,m_niceness);
	nt3.set ( 8,4,256,ntbuf3,5000,true,m_niceness,"addr-nt3");
	int32_t goodCount = 0;
	// hash words of the addresses
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// is it inlined
		bool inlined = (ad->m_flags & AF_INLINED);
		// is its name verified?
		bool vn1 = ( ad->m_flags & AF_VERIFIED_PLACE_NAME_1) ;
		bool vn2 = ( ad->m_flags & AF_VERIFIED_PLACE_NAME_2) ;
		bool vs  = ( ad->m_flags & AF_VERIFIED_STREET);

		// must be inlined or verified or after "at"

		// add place name even if not verified, because if we match
		// an unverified place name the alias must have its
		// PLF2_AFTER_AT flag set, meaning it was after the word "at"
		// so it is a lot less likely to be a false positive.
		// this fixes the solstics seed swap url:
		// events.sfgate.com/san-francisco-ca/events/show/
		// 88884664-solstice-seed-swap because it was not allowing
		// "exploratorium" to be an alias with the exploratorium 
		// inlined address because its place name was not verified.
		// so down below we make sure to only allow such aliasing if
		// the place name alias is "after an at"... so it is clearly
		// a place name and not just menu cruft.
		if ( ! inlined && ! vn1 && ! vn2 && ! vs ) continue;

		// . i don't want aliases to a po box
		// . fixes adobetheater.org which aliases 
		//   "at the adobe theater" to the po box address at the 
		//   bottom of the page because it is a better match than
		//   the placedbName "adobe theater" that we have as an
		//   alternative name for the non-pobox address...
		if ( ad->m_street->m_flags2 & PLF2_IS_POBOX ) continue;

		// do not add if ambiguous and known to be BAD city/state
		if ( ad->m_flags3 & AF2_BADCITYSTATE ) continue;

		// sometimes a street can exist in two cities or states
		//if ( ad->m_flags & AF_AMBIGUOUS ) continue;
		// count 
		goodCount++;

		uint64_t v = ((uint64_t)((uint32_t)(PTRTYPE)ad)); // WRONG!MDW

		// . hash place name 1
		// . use "0" for the name number
		if ( ad->m_name1 &&
		     ! hashPlaceName (&nt1,
				      m_words,
				      ad->m_name1->m_a,
				      ad->m_name1->m_b,
				      v))
			return false;

		// use "1" for the name number
		if ( ad->m_name2 &&
		     ! hashPlaceName (&nt1,
				      m_words,
				      ad->m_name2->m_a,
				      ad->m_name2->m_b,
				      v| (1LL<<32) ) )
			return false;

		// hash the verified alternative names
		char *s    = ad->m_placedbNames;
		char *send = ad->m_placedbNamesEnd;
		uint64_t count = 2;
		// scan them
		for ( ; s && s < send ; count++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip score
			s += 4;
			// empty? strange...
			if ( ! *s ) { char *xx=NULL;*xx=0; }
			// hash that
			Words tmp;
			if ( ! tmp.set9 ( s, m_niceness ) ) return false;
			int32_t nw = tmp.m_numWords;
			if ( ! hashPlaceName (&nt1,&tmp,0,nw,v|(count<<32)) ) 
				return false;
			// skip that and the \0
			s += gbstrlen(s) + 1;
		}

		// hash their street hash and street num hash
		int64_t ch = 0;
		ch ^= ad->m_street->m_hash;
		ch ^= ad->m_street->m_streetNumHash;
		ch ^= ad->m_street->m_streetIndHash;
		if ( ! nt3.addKey ( &ch , &ad ) ) return false;
		// hash the street as a name!
		if ( ! nt3.addKey(&ad->m_street->m_wordHash64,&ad))
			return false;
		// . and exact name too for placedb verified names
		// . it includes a xor'ed 0x123456 in its hash to distinguish
		//   from street names that are the same name
		if ( vn1 && ! nt3.addKey ( &ad->m_name1->m_hash , &ad ) ) 
			return false;
		if ( vn2 && ! nt3.addKey ( &ad->m_name2->m_hash , &ad ) ) 
			return false;
	}
	// . if we had no inlined or verified addresses, bail at this point
	// . no, might be able to add some lat/lon only addresses below!
	//if ( goodCount == 0 ) {
	//	// validate this
	//	m_numSorted = 0;
	//	m_sortedValid = true;
	//	return true;
	//}


	/////////////////////////////////////
	//
	// Lastly, set Street/Place::m_alias and m_address
	//
	// So now streets point to the inlined/verified address that uses them.
	//
	/////////////////////////////////////

	// make the match table
	char mtbuf[5000];
	HashTableX mt;
	mt.set ( 8,4,32,mtbuf,5000,true,m_niceness,"plmtchtbl");

	//Section **sp = m_sections->m_sectionPtrs;

	//
	// no! scan the streets since maybe alias did not pair up with
	// a city/adm1 and make it into the m_addresses[] array
	//
	for ( int32_t i = 0 ; i < m_sm.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Place *street = (Place *)m_sm.getPtr(i);
		// skip if already has an address set from above in this func
		if ( street->m_address ) continue;
		// is it a name?
		bool isName  = street->m_flags2 & PLF2_IS_NAME ;
		// if we are a street like "111 Maple SE"  for panjea.org
		// because it is listed twice! one time is inlined and the
		// other is not!
		if ( ! isName ) {
			// make special hash
			int64_t ch = 0;
			ch ^= street->m_hash;
			ch ^= street->m_streetNumHash;
			ch ^= street->m_streetIndHash;
			Address **pad = (Address **) nt3.getValue ( &ch );
			if ( ! pad )  continue;
			if ( (*pad)->m_street->m_a == street->m_a )
				street->m_address = *pad;
			else
				street->m_alias = *pad;
			continue;
		}
		// need a place name
		//if ( ! isName ) continue;

		// match name to name of address that was verified in placedb
		Address **pad = (Address **) nt3.getValue ( &street->m_hash );
		// sometimes what is really the street has isName set to
		// true. we do not know its a street name in this context
		// because it does not end in an indicator. but the address
		// we are trying to alias to it does end in an indicator
		// or in a city/state. like santafe.org for 
		// "1160 Camino Cruz Blanca". it is used twice on the page.
		// the first time it is clearly a street, the 2nd time is
		// why we are doing this! Same for "705 Camino Lejo" on
		// that page as well!
		if ( ! pad ) {
			pad =(Address **) nt3.getValue (&street->m_wordHash64);
			// are we a street address ourself?
			if ( pad ) {
				street->m_alias = *pad;
				continue;
			}
		}


		if ( pad && 
		     (*pad)->m_name1 &&
		     (*pad)->m_name1->m_a == street->m_a ) {
			street->m_address = *pad;
			continue;
		}
		if ( pad && 
		     (*pad)->m_name2 &&
		     (*pad)->m_name2->m_a == street->m_a ) {
			street->m_address = *pad;
			continue;
		}


		// . and make it after at i guess
		// . no we need "Explora" as an alias too!
		// . no! for "santa fe playhouse" it is not preceeded by an at
		//   ... so i hope commenting this out is ok
		//if ( ! afterAt ) continue;
		// grabs its name
		int32_t a = street->m_a;
		int32_t b = street->m_b;
		// . are we after at?
		// . this also includes being after "location: " and some
		//   other strong place indicators
		bool afterAt = street->m_flags2 & PLF2_AFTER_AT ;
		// reset mt
		mt.clear();
		// count its words
		int32_t need = 0;
		// scan its words
		for ( int32_t k = a ; k < b ; k++ ) {
			// skip if not word
			if ( ! m_wids[k] ) continue;
			// . we do not need to match an initial the
			// . fix for aliasing "The Adobe Theater" to 
			//   "Adobe Theater" for adobetheater.org
			if ( need == 0 && m_wids[k] == h_the ) continue;
			// count it
			need++;
			// get possible candidates
			int32_t slot1 = nt1.getSlot ( &m_wids[k] );
			// if no match, forget it! we need to match
			// all our words
			//if ( slot1 < 0 ) break;
			// loop
			for(;slot1>=0;slot1=nt1.getNextSlot(slot1,&m_wids[k])){
				// get the value
				uint64_t val = 
				    *(uint64_t *)nt1.getValueFromSlot(slot1);
				// lower 32 bits is the address ptr
				Address *cand = (Address *)(val & 0xffffffff);
				// upper 32 bits is the name number
				int32_t nn = (val >> 32);
				// sanity check
				if ( nn < 0     ) { char *xx=NULL;*xx=0; }
				if ( nn > 10000 ) { char *xx=NULL;*xx=0; }
				// get street flags
				pflags_t sf = cand->m_street->m_flags2;
				// if name number is 0, then place name 1 must
				// be verified or at least "after at"
				if ( nn==0 && 
				     !(sf&AF_VERIFIED_PLACE_NAME_1)&& 
				     !afterAt ) 
					continue;
				// same goes for place name 2
				if ( nn==1 &&
				     !(sf&AF_VERIFIED_PLACE_NAME_2)&& 
				     !afterAt ) 
					continue;

				// other nn's are place names with 2+ votes
				// from placedb in Address::m_placedbNames
				// so let them ride.

				// store in match table, add one point
				if(!mt.addTerm((int64_t *)&val))return false;
			}
		}
		
		// scan match table for best matches
		int32_t dups = 0;
		Address *best = NULL;
		int32_t bestScore = 0;
		Section *bestContainer = NULL;
		int32_t bestnn = -1;
		// int16_tcut
		char vmask1 = 0;
		vmask1 |= AF_VERIFIED_PLACE_NAME_1;
		vmask1 |= AF_VERIFIED_PLACE_NAME_2;
		vmask1 |= AF_VERIFIED_STREET;
		for ( int32_t y = 0 ; y < mt.m_numSlots ; y++ ) {
			// skip if empty bucket/slot
			if ( ! mt.m_flags[y] ) continue;
			// get score
			int32_t score = mt.getScoreFromSlot ( y );
			// need to match all of our words
			if ( score < need ) continue;
			// skip if not max
			//if ( score < max ) continue;
			// get the address ptr that has this score
			//Address *matcher = *( Address **)mt.getKey ( y );
			uint64_t v = *(uint64_t *)mt.getKey ( y );
			// get name number
			int32_t nn = v>>32;
			// sanity check
			if ( nn < 0 || nn > 10000 ) { char *xx=NULL;*xx=0; }
			// get matching address
			Address *matcher = (Address *)(v & 0xffffffff);

			// get our alias section
			Section *ads = sp[street->m_a];//ad->m_section;

			// . telescope our alias up
			// . see which address it hits first, "best" or 
			//   "matcher"
			// . if it hits both at the same time then it is
			//   ambiguous and we can't make a decision
			// . keep telescoping out matcher until it contains
			//   the alias
			Section *sm = matcher->m_section;
			for ( ; sm ; sm = sm->m_parent ) 
				if ( sm->contains ( ads ) ) break;

			// we got one, or tied for max
			if ( ! best ) {
				bestScore     = score;
				best          = matcher;
				bestContainer = sm;
				bestnn        = nn;
				continue;
			}

			// if our container is smaller we win!
			if ( bestContainer->contains ( sm ) ) {
				bestScore     = score;
				best          = matcher;
				bestContainer = sm;
				dups          = 0;
				bestnn        = nn;
				continue;
			}

			// if we contain him, he stays winning
			if ( sm->contains ( bestContainer ) )
				continue;

			//
			// otherwise we are brothers or in the same section
			//

			// if it is a dup of the best just ignore it
			if ( matcher->m_street->m_hash == 
			     best->m_street->m_hash &&
			     matcher->m_street->m_streetNumHash == 
			     best->m_street->m_streetNumHash &&
			     matcher->m_street->m_streetIndHash == 
			     best->m_street->m_streetIndHash )
				continue;

			// ok, it is a tie! we won't be able to alias him!
			dups++;
		}
		// if winner is ambiguous, this address, "ad", has no alias
		if ( dups ) continue;
		// or if no winner
		if ( ! best ) continue;

		// . trumba.com had an address like 
		// "Aztec, NM<br />398 S Light Plant Rd, Aztec, NM 87410-1826"
		//   and then referred to NM below, and we thought it was
		//   an alias for that address! 
		// . BUT it turns out that when i fixed the bug above for
		//   incorrectly checking to make sure that matching places
		//   had verified place name 1 or 2, then that fixed this bug,
		//   but if the place name had the word "NM" or "Aztec" in it
		//   AND was verified, i would expect us to need this code
		//   so let's make sure we are "after at" if only doing a
		//   partial alias
		if ( ! afterAt ) {
			// get alnum words in best
			//int32_t aw1 = 0;
			//int32_t aw2 = 0;
			Place *n1 = best->m_name1;
			Place *n2 = best->m_name2;
			//if ( n1 ) aw1 = n1->m_alnumB - n1->m_alnumA;
			//if ( n2 ) aw2 = n2->m_alnumB - n2->m_alnumA;
			// crap, what if we matched a str in m_placedbName,
			// we don't know which one we matched! yes we do,
			// its # "nn-2" in the string
			char *ps = NULL;
			int32_t  pslen;
			if ( bestnn == 0 ) {ps=n1->m_str; pslen=n1->m_strlen;}
			if ( bestnn == 1 ) {ps=n2->m_str; pslen=n2->m_strlen;}
			// subtract 
			bestnn -= 2;
			// otherwise, gotta cycle
			char *s    = best->m_placedbNames;
			char *send = best->m_placedbNamesEnd;
			// scan them and set "aw"
			for ( ; bestnn>= 0 && s && s < send ; bestnn-- ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip score
				s += 4;
				// point to it
				char *wp = s;
				// get this
				int32_t slen = gbstrlen(s);
				// skip that and the \0
				s += slen + 1 ;
				// skip if not 0
				if ( bestnn > 0 ) continue;
				// set the process string
				ps = wp;
				pslen = slen;
				// and break for processing
				break;
			}

			// make into word array
			Words tmp;
			if ( ! tmp.setx (ps,pslen,m_niceness)) return false;
			// count the alnumwords, but ignore "the"
			int32_t aw = 0;
			for (int32_t x=0;x<tmp.m_numWords;x++) {
				if ( ! tmp.m_wordIds[x] ) continue;
				if ( tmp.m_wordIds[x] == h_the) continue;
				aw++;
			}

			bool fullMatch = false;
			if (  aw == need ) fullMatch = true;
			if ( ! fullMatch ) continue;
		}

		// int16_tcut
		char vmask2 = 0;
		vmask2 |= AF_VERIFIED_PLACE_NAME_1;
		vmask2 |= AF_VERIFIED_PLACE_NAME_2;
		vmask2 |= AF_VERIFIED_STREET;
		Address *ak = NULL;
		// might not be ordered by position
		int32_t k = 0;
		// get the min position right above us
		int32_t abovePos = -1;
		Address *above = NULL;
		int32_t belowPos = -1;
		Address *below = NULL;
		// now the winner must also be the first verified address
		// above or below us!!!
		for ( k = 0 ; k < m_am.getNumPtrs() ; k++ ) {
			// get it
			ak = (Address *)m_am.getPtr(k);//&m_addresses[k];
			// ignore if a place name
			if ( ak->m_street->m_flags2 & PLF2_IS_NAME )
				continue;
			// skip if not inlined or verified 
			bool inlined = (ak->m_flags & AF_INLINED);
			// is its name verified?
			bool verified = ( ak->m_flags & vmask2);
			// skip if not either!
			if ( ! inlined && ! verified ) continue;
			// ignore if after us, must be ABOVE us since we
			// are referencing it as an alias
			if ( ak->m_street->m_a < a ) {
				// skip if doesn't beat the current "above" one
				if ( ak->m_street->m_a <= abovePos ) continue;
				// set it
				above = ak;
				abovePos = ak->m_street->m_a;
				continue;
			}
			// ok, below winner?
			// skip if doesn't beat the current "above" one
			if ( belowPos >= 0 &&
			     ak->m_street->m_a >= belowPos ) continue;
			// set it
			below = ak;
			belowPos = ak->m_street->m_a;

		}
		// skip if not one before us
		if ( ! above && ! below ) continue;

		// try "above"
		if ( above ) {
			// skip if not a match with the winner, "best"
			if ( best ->m_street->m_hash          != 
			     above->m_street->m_hash          ) 
				above = NULL;
			if ( above &&
			     best ->m_street->m_streetNumHash != 
			     above->m_street->m_streetNumHash ) 
				above = NULL;
			if ( above &&
			     best ->m_street->m_streetIndHash != 
			     above->m_street->m_streetIndHash ) 
				above = NULL;
		}

		// try "below"
		if ( below ) {
			// skip if not a match with the winner, "best"
			if ( best ->m_street->m_hash          != 
			     below->m_street->m_hash          ) 
				below = NULL;
			if ( below &&
			     best ->m_street->m_streetNumHash != 
			     below->m_street->m_streetNumHash ) 
				below = NULL;
			if ( below &&
			     best ->m_street->m_streetIndHash != 
			     below->m_street->m_streetIndHash ) 
				below = NULL;
		}

		// pick the non null one
		if ( ! above && ! below ) continue;
		
		// ok, use him as our alias
		if      ( above ) street->m_alias = above;
		else if ( below ) street->m_alias = below;
	}

	Place *prev = NULL;
	////////////////////////////////
	//
	// set m_alias for intersections
	//
	////////////////////////////////
	for ( int32_t i = 0 ; i < m_sm.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Place *street = (Place *)m_sm.getPtr(i);
		// if intersection, check if alias of prev street
		if ( ! ( street->m_flags2 & PLF2_INTERSECTION ) ) {
			// update this so its a real street always
			prev = street;
			continue;
		}
		// if we are actually in an address like
		// "CORNER OF HWY 64& HWY 38\0 EAGLE NEST, NM 87718"
		// then skip it as well!
		if ( street->m_address ) continue;
		// try next street
		Place *next = NULL;
		//Place *prev = NULL;
		// if we can get it, get it
		//if ( i - 1 >= 0   ) prev = &m_streets[i-1];
		if ( i + 1 < m_sm.getNumPtrs() ) 
			next = (Place *)m_sm.getPtr(i+1);
		// ignore if also intersection
		if ( prev && (prev->m_flags2 & PLF2_INTERSECTION)) prev = NULL;
		if ( next && (next->m_flags2 & PLF2_INTERSECTION)) next = NULL;
		// try prev first
		Place *first = prev;
		// declare up here
		int32_t a;
		int32_t b;
		bool good;
		int64_t commonIds[32];
		int32_t nc;
		// loop over both
	subloop:
		// need a street above us to be alias of
		if ( ! first ) goto done;
		// must be an address
		if ( !first->m_address && !first->m_alias ) goto done;
		// must match up
		a = first ->m_b;
		b = street->m_a;
		// swap em
		
		// forget it if too big
		if ( b - a > 200 ) continue;
		// scan to make sure only good words in between
		int32_t j; for ( j = a ; j < b ; j++ ) {
			// skip if not wid
			if ( ! m_wids[j] ) continue;
			// must be special word
			if ( m_wids[j] == h_of ) continue;
			if ( m_wids[j] == h_at ) continue;
			if ( m_wids[j] == h_intersection ) continue;
			if ( m_wids[j] == h_corner ) continue;
			if ( m_wids[j] == h_sw ) continue;
			if ( m_wids[j] == h_ne ) continue;
			if ( m_wids[j] == h_nw ) continue;
			if ( m_wids[j] == h_se ) continue;
			break;
		}
		// set if good - if only words we permit in between
		good = (j >= b);

		// if that failed we could still success by containing
		// a street name in common!
		if ( ! good ) {
			nc = getCommonWordIds ( street->m_a , 
						street->m_b ,
						first->m_a  , 
						first->m_b  ,
						m_wids ,
						commonIds , 
						32 ,
						m_niceness );
			for ( int32_t k = 0 ; k < nc ; k++ ) {
				// get it
				int64_t cid = commonIds[k];
				// skip if indicator, must be non-indicator
				IndDesc *id;
				id = (IndDesc *)g_indicators.getValue(&cid);
				if ( id ) continue;
				// that is good enough!
				good = true;
				break;
			}
		}
		// if it was not an alias, go on to next place
		if ( ! good ) goto done;
		// assign our m_alias
		if      ( first->m_address ) 
			street->m_alias = first->m_address;
		else if ( first->m_alias )
			street->m_alias = first->m_alias;
		continue;
	done:
		// give up if really done
		if ( first == next ) continue;
		// try next now
		first = next;
		goto subloop;
	}

	////////////////////////////////
	//
	// set m_alias for intersections more loosely
	//
	////////////////////////////////
	//
	// fixes "14th and Curtis, Denver CO" on denver.org
	// which is a proper address and has the full address next to it
	//
	for ( int32_t i = 0 ; i < m_sm.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Place *street = (Place *)m_sm.getPtr(i);
		// if intersection, check if alias of prev street
		if ( ! ( street->m_flags2 & PLF2_INTERSECTION ) ) {
			// update this so its a real street always
			prev = street;
			continue;
		}
		// must be full address for this algo
		if ( ! street->m_address ) continue;
		// try next street
		Place *next = NULL;
		// if we can get it, get it
		if ( i + 1 < m_sm.getNumPtrs() ) 
			next = (Place *)m_sm.getPtr(i+1);
		// ignore if also intersection
		if ( prev && (prev->m_flags2 & PLF2_INTERSECTION)) prev = NULL;
		if ( next && (next->m_flags2 & PLF2_INTERSECTION)) next = NULL;
		// try prev first
		Place *first = prev;
		if ( ! first ) first = next;
		if ( ! first ) continue;

	subloop2:

		char cmpbuf[1024];
		HashTableX cmp;
		cmp.set(8,0,32,cmpbuf,1024,false,m_niceness,"strtcmp");
		// see if matches one non-indicator in street
		for ( int32_t j = first->m_a ; j < first->m_b ; j++ ) {
			// get it
			int64_t h = m_wids[j];
			// skip punct
			if ( ! h ) continue;
			// skip if indicator
			if ( g_indicators.isInTable(&h) ) continue;
			// hash it otherwise
			if ( ! cmp.addKey(&h) ) return false;
		}
		// assume intersection does not match any words
		bool matched = false;
		// now compare to our intersection streets
		for ( int32_t j = street->m_a ; j < street->m_b ; j++ ) {
			// get it
			int64_t h = m_wids[j];
			// skip punct
			if ( ! h ) continue;
			// skip if indicator
			if ( g_indicators.isInTable(&h) ) continue;
			// hash it otherwise
			if ( ! cmp.isInTable(&h) ) continue;
			// got a match!
			matched = true;
			// all done
			break;
		}
		// if no match, forget the alias
		if ( ! matched ) {
			// give up if really done
			if ( first == next ) continue;
			// or if nex tis NULL
			if ( ! next ) continue;
			// try next now
			first = next;
			goto subloop2;
		}
		// it matched!
		if      ( first->m_address ) 
			street->m_alias = first->m_address;
		else if ( first->m_alias )
			street->m_alias = first->m_alias;
	}


	////////////////////////////////
	//
	// set D_IS_IN_ADDRESS[_NAME] for places that alias an address
	//
	////////////////////////////////

	// . now scan the places. if not in an address, but aliases one then
	//   we need to set D_IS_IN_ADDRESS[_NAME] for it...
	// . this fixes the aliased streets and names in ceder.net from
	//   being event titles...
	for ( int32_t i = 0 ; i < m_sm.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Place *street = (Place *)m_sm.getPtr(i);//&m_streets[i];
		// skip if no alias
		Address *alias = street->m_alias;
		if ( ! alias ) continue;
		// is it a name?
		bool isName  = street->m_flags2 & PLF2_IS_NAME ;
		// if a street, set this
		wbit_t flag;
		if ( isName ) flag = D_IS_IN_VERIFIED_ADDRESS_NAME;
		else          flag = D_IS_IN_ADDRESS;
		// set bits for alias
		int32_t x = street->m_a;
		int32_t y = street->m_b;
		if ( y > m_nw ) { char *xx=NULL;*xx=0; }
		for ( ; x >= 0 && x < m_nw && x < y ; x++ ) 
			bits[x] |= flag;
	}


	////////////////////////////////
	//
	// set m_numNonDupAddresses
	//
	////////////////////////////////
	m_numNonDupAddresses = 0;
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() - 1 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Address *aa = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// get street position
		int32_t a = aa->m_street->m_a;
		// sanity check
		if ( a < 0 ) continue;
		// get section
		Section *ss = sp[a];
		// skip if dup
		//if ( ss->m_flags & SEC_DUP ) continue;
		if ( ss->m_votesForDup > 0 ) continue;
		// count it otherwise
		m_numNonDupAddresses++;
	}

	///////////////////////////////
	//
	// set Address::m_flags AF_VENUE_DEFAULT bit
	//
	///////////////////////////////

	m_numVenues = 0;
	// what are the addresses of this website? (assuming this website
	// is essentially the website of a venue or physical place)
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// is its name verified?
		bool vn1 = (ad->m_flags & AF_VERIFIED_PLACE_NAME_1) ;
		bool vn2 = (ad->m_flags & AF_VERIFIED_PLACE_NAME_2) ;
		// we might have some alternative verified names too!
		bool vn3 = (bool) ad->m_bestPlacedbName;
		// must be inlined or verified
		if ( ! vn1 && ! vn2 && ! vn3 ) continue;
		// if address used the dc[] array that consist of elements
		// from the venue tag in tagdb, then do not add it back
		// to tagdb
		//bool add = true;
		//if ( ad->m_street->m_a < 0 ) add = false;
		// see if its place name 1 is in the siteTitleBuf
		char *p1 = NULL;
		char *p2 = NULL;
		if ( vn1 && ad->m_name1 ) p1 = ad->m_name1->m_str;
		if ( vn2 && ad->m_name2 ) p2 = ad->m_name2->m_str;
		// temp null term
		char c1;
		char c2;
		int32_t plen1;
		int32_t plen2;
		if ( p1 ) plen1 = ad->m_name1->m_strlen;
		if ( p2 ) plen2 = ad->m_name2->m_strlen;
		char *saved1 = NULL;
		char *saved2 = NULL;
		if ( p1 ) saved1 = &p1[plen1];
		if ( p2 ) saved2 = &p2[plen2];
		if ( p1 ) { c1 = *saved1; *saved1 = 0; }
		if ( p2 ) { c2 = *saved2; *saved2 = 0; }
		// . skip "the"
		// . fixes "the adobe theater" in title and "adobe theater"
		//   being the verified place name for adobetheater.org
		if ( p1 && strncasecmp(p1,"the ",4) == 0 ) p1 += 4;
		if ( p2 && strncasecmp(p2,"the ",4) == 0 ) p2 += 4;
		// scan m_siteTitleBuf for either p1 or p2
		char *d    = m_siteTitleBuf;
		char *dend = m_siteTitleBuf + m_siteTitleBufSize;
		// loop over the \0 delimeted list of titles
		for ( ; d < dend ; d += gbstrlen(d) + 1 ) {
			// skip "the"
			if ( strncasecmp(d,"the ",4) == 0 ) d += 4;
			// compare
			bool match = false;
			if ( p1 && gb_strcasestr ( d , p1 ) ) match = true;
			if ( p2 && gb_strcasestr ( d , p2 ) ) match = true;
			// loop over all possible alternative placedb names
			// that have 2 or more votes as well
			char *s = ad->m_placedbNames;
			for ( ; s && s<ad->m_placedbNamesEnd;s+=gbstrlen(s)+1){
				// breathe
				QUICKPOLL(m_niceness);
				// skip score of 4 bytes
				s += 4;
				// skip "the"
				if ( strncasecmp(s,"the ",4)==0) s += 4;
				// compare
				if ( ! gb_strcasestr(d,s) ) continue;
				// got a match
				match = true;
				// stop
				break;
			}
			// go to next title if no match
			if ( ! match ) continue;
			// we got a match!
			ad->m_flags |= AF_VENUE_DEFAULT;
			// count it
			m_numVenues++;
			// done
			break;
		}
		if ( saved1 ) *saved1 = c1;
		if ( saved2 ) *saved2 = c2;
	}

	//int32_t imax = m_nw;
	// skip if no streets... no might add a lat/lon "street" below
	//if ( m_sm.getNumPtrs() <= 0 ) imax = 0;

	/////////
	//
	// we gotta call this twice. once here and once below
	//
	/////////
	if ( ! setFirstPlaceNums() ) return false;

	/////////////////////////////
	//
	// scan for lat/lon coordinates
	//
	/////////////////////////////

	// US lat from  24.450000 to   47.4666666
	// US lon from -71.083333 to -114.1333333

	// yellowpages.com: 
	//  <span class="latitude" id="map-latitude">35.146292</span> 
	//  <span class="longitude" id="map-longitude">-90.0148638</span>

	// citysearch.com
	//  <span class="latitude">37.793126</span>
	//  <span class="longitude">-122.42289</span>

	// yellowpages.aol.com
	//  <div style="display:none" class="result_json">{"lat":"35.084278",
	//    "lon":"-106.649467","cb":false,"photo":""}</div>

	// www.superpages.com
	//    <a href="http://clicks.superpages.com/ct/clickThrough?SRC=portals
	//    ... &POI1lat=039396979&POI1lng=-076564398&POI1name=Baynesville+..

	// www.yellowbook.com
	//   /listing-map.png?lat=35.0981&amp;int32_t=-106.6694

	// yelp.com
	//   use "center=" cgi parm on maps.google.com

	// google maps link
	//  http://www.moma.org/visit/plan/gettinghere
	//  src="http://maps.google.com/maps/ms?ie=UTF8&amp;hl=en&amp;msa=0&amp;msid=104870349047867594566.0004626e9d41225400a1c&amp;ll=40.761325,-73.977642&amp;sp...

	char *bufEnd   = m_words->getContentEnd();
	char *bufStart = m_words->getContent   ();

	// now we do a generic scan for any numbers that look like lat/lon
	p = m_words->getContent();

	// must be latitude then longitude, in that order
	int32_t lastScore = -1;
	double lastVal ;
	char *lastPos = NULL;
	char  lastType;
	char *lastAddedPos = NULL;
	int32_t  lastAddedWord = -1;
	int32_t  lastAddedWordDist;
	int32_t  lastAddedCharDist;
	bool  addedSomething = false;

	if ( ! p ) p = "\0";

	for ( ; *p ; p++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not digit
		if ( ! is_digit(*p) ) continue;
		// set start
		char *start = p;
		// avoid %3D from url encodings
		if ( p > bufStart && p[-1] == '%' &&
		     p[0] == '3' && 
		     to_lower_a(p[1]) == 'd' ) {
			// skip over that encoded equal sign
			p += 2;
			start += 2;
			// skip over negative sign
			if ( *p == '-' ) { p++; start++; }
			// forget it if got a negative sign or a non-digit
			if ( ! is_digit(*p) ) continue;
		}

		// negative sign?
		if ( p>bufStart && p[-1] == '-' ) start--;
		// reset counts
		int32_t digitCount = 0;
		int32_t decimalCount = 0;
		// do not scan so far
		char *pmax = p + 20;
		if ( pmax > bufEnd ) pmax = bufEnd;
		// scan until no digit or period
		for ( ; *p && p < pmax ; p++ ) {
			// count the digits
			if ( is_digit(*p) ) {
				digitCount++;
				continue;
			}
			// decimal point is ok
			if ( *p == '.' ) {
				decimalCount++;
				continue;
			}
			// stop on other crap
			break;
		}
		// give up if end of doc
		if ( ! *p ) break;
		//  give up if less than 6 digits encountered
		if ( digitCount <  6 ) continue;
		// some pages have no period in it
		// and we just have to assume the first
		// 3 digits are before the period. like for
		// switchboard.com urls
		if ( decimalCount >= 2 ) continue;
		// convert
		double dval = atod2(start,p-start);
		// fix switchboard.com stuff which has no decimal pt
		if ( decimalCount == 0 ) {
			// how many digits to left of decimal
			int32_t left = 3;
			// make a divisor
			double ddd = 1;
			for ( int32_t vv = 0 ; vv<digitCount-left; vv++)
				ddd *= 10;
			// fix it
			dval /= ddd;
		}
		// bail if bad
		if ( dval < -180.0 || dval > 180.0 ) continue;
		// the continental US ranges from
		// latitude : 24 27/60 (http://en.wikipedia.org/wiki/Florida)
		// latitude : 49       (http://en.wikipedia.org/wiki/Washington
		// longitude: 71 5/60  (http://en.wikipedia.org/wiki/Maine)
		// longitude: 114 8/60 (http://en.wikipedia.org/wiki/California

		// which is lat from 24.450000 to  47.4666666
		// which is lon from 71.083333 to 114.1333333
		// in the usual decimal it is
		// lat from  24.450000 to   47.4666666
		// lon from -71.083333 to -114.1333333
		char type = 0;
		if ( dval >=  24.45 && dval <=  50.0 ) type = 1; // lat
		if ( dval >= -125.0 && dval <= -66.1 ) type = 2; // lon

		// this overrides though
		char *r = start -1;
		char *rend = start - 10;
		if ( rend < bufStart + 5 ) rend = bufStart + 5;
		for ( ; r >= rend ; r-- ) {
			if ( ! is_alpha_a(*r) ) continue;
			// <latitude> facebook/brazil
			if ( to_lower_a(r[ 0]) == 'e' &&
			     to_lower_a(r[-1]) == 'd' &&
			     to_lower_a(r[-2]) == 'u' &&
			     to_lower_a(r[-3]) == 't' &&
			     to_lower_a(r[-4]) == 'i' &&
			     to_lower_a(r[-5]) == 't' ) {
				type = 1;
				break;
			}
			// <longitude> facebook/brazil
			if ( to_lower_a(r[ 0]) == 'e' &&
			     to_lower_a(r[-1]) == 'd' &&
			     to_lower_a(r[-2]) == 'u' &&
			     to_lower_a(r[-3]) == 't' &&
			     to_lower_a(r[-4]) == 'i' &&
			     to_lower_a(r[-5]) == 'g' ) {
				type = 2;
				break;
			}
		}

		// bail if unknown lat or lon
		if ( type == 0 ) continue;


		// . need a latitude before longitude can be accepted
		// . fixes www.happycow.net/gmaps/get-map-direct.php?vid=5447
		//   which had a bogus large number (no decimal) after the
		//   first legit lat/lon pair in the filename of a url i think
		//if ( needLat && type == 2 ) continue;

		// get word position for this function
		int32_t wn2 = m_words->getWordAt ( start );
		// sanity check
		if ( wn2 < 0 ) { char *xx=NULL;*xx=0; }
		// find nearest place. the associated place must be a verified
		// place name or a true street.
		Place *ap2 = getAssociatedPlace ( wn2 );
		// get the address that contains the place
		Address *aa = NULL;
		// try address
		if ( ! aa && ap2 ) aa = ap2->m_address;
		// try alias
		if ( ! aa && ap2 ) aa = ap2->m_alias;
		// if this lat/lon had an associated place but the associated
		// place had no address because it is like "at Effex" 
		// (after at) then allow it through. we should add the lat/lon
		// as its own address and alias the simple place, ap2, to
		// that. i.e. ap2->m_alias = newlatlonaddress
		//if ( ! aa && ap2 ) continue;
		// assign it
		double *ptr = NULL;
		if ( type == 1 && aa ) ptr = &aa->m_latitude;
		if ( type == 2 && aa ) ptr = &aa->m_longitude;

		// are we from google maps url?
		//  src="http://maps.google.com/maps/ms?ie=UTF8&amp;hl=en&amp;msa=0&amp;msid=104870349047867594566.0004626e9d41225400a1c&amp;ll=40.761325,-73.977642&amp;sp...
		// compute the score of the lat/lon pair
		int32_t score = -1;
		bool inFormat = false;
		// . ll=lat,lon
		// . this is the center of the map and almost always not
		//   exactly the exact place of the business which tends to be
		//   a little lower down below the center of the map, however
		//   if a query is specified then google highlights all 
		//   locations on the map that match that query
		if      ( start - 10 >= bufStart &&
			  start[-1] == '=' &&
			  start[-2] == 'l' &&
			  start[-3] == 'l' &&
			  (start[-4] == ';'||start[-4]=='&') ) {
			// this is the correct one
			score = 100;
			inFormat = true;
		}
		// cbll=lat,lon
		else if ( start - 15 >= bufStart &&
			  start[-1] == '=' &&
			  start[-2] == 'l' &&
			  start[-3] == 'l' &&
			  start[-4] == 'b' &&
			  start[-5] == 'c' &&
			  (is_punct_a(start[-6])) ) {
			// this is street view coords
			score = 50;
			inFormat = true;
		}
		// sll=lat,lon (this is not good!?!?!)
		else if ( start - 15 >= bufStart &&
			  start[-1] == '=' &&
			  start[-2] == 'l' &&
			  start[-3] == 'l' &&
			  start[-4] == 's' ) {
			// business search thingy? MAKE IT NEGATIVE SCORE!
			score = -20;
			inFormat = true;
		}
		// geocode=0,lat,lon
		else if ( start - 20 >= bufStart &&
			  start[-1] == ',' &&
			  start[-2] == '0' &&
			  start[-3] == '=' &&
			  start[-4] == 'e' &&
			  start[-5] == 'd' &&
			  start[-6] == 'o' &&
			  start[-7] == 'c' &&
			  start[-8] == 'o' &&
			  start[-9] == 'e' &&
			  start[-10] == 'g' &&
			  (is_punct_a(start[-11])) ) {
			// related to directions somehow
			score = 30;
			inFormat = true;
		}
		else
			score = 10;


		// save that
		char   *savePos   = lastPos;
		int32_t    saveScore = lastScore;
		char    saveType  = lastType;
		double  saveVal   = lastVal;

		// then update
		lastPos   = start;
		lastScore = score;
		lastType  = type;
		lastVal   = dval;

		// if first number, skip
		if ( ! savePos ) continue;

		// if too far apart, forget it! most likely not a lat/lon pair
		//if ( start - savePos > 100 ) continue;

		// skip if both are lats or both are lons
		if ( saveType == type ) continue;

		// if it is a google url thing then we need to wait for
		// the longitude right after the latitude
		if ( inFormat && type == 1 ) continue;

		// a negative score curses the longitude that follows
		if ( saveScore < 0 ) continue;

		// get word # and associated place of previous lat/lon #
		int32_t wn1 = m_words->getWordAt ( savePos );//start );
		if ( wn1 < 0 ) { char *xx=NULL;*xx=0; }
		// find nearest place. the associated place must be a verified
		// place name or a true street.
		Place *ap1 = getAssociatedPlace ( wn1 );
		if ( ap1 != ap2 ) continue;

		// super crazy? try to fiz graffiti.org which pairs together
		// to bogus numbers that are really far apart
		int32_t wordDist = wn2 - wn1;
		if ( wordDist > 30 ) 
			continue;

		// better distance counting. should fix
		// santafe.org/perl/page.cgi?p=maps;gid=2415 which
		// has multiple lat/lon pairs all that had a different #
		// of chars between them, but this will make their distances
		// equal where they should be now
		int32_t dist = 0;
		bool inalnum = false;
		bool inpunct = false;
		for ( char *d = savePos ; d < start ; d++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if space
			if ( is_wspace_a(*d) ) {
				inalnum = false;
				inpunct = false;
				continue;
			}
			// count words
			if ( is_alnum_a(*d) ) {
				if ( inalnum ) continue;
				inalnum = true;
				inpunct = false;
				dist++;
				continue;
			}
			// punctuation
			if ( inpunct ) continue;
			inpunct = true;
			inalnum = false;
			dist++;
			continue;
		}

		// this is likewise bad as well...
		if ( dist > 30 )
			continue;


		bool addLatLonAddress = false;
		if ( ! ap1 ) addLatLonAddress = true;
		if ( ap1 && ! ap1->m_alias && ! ap1->m_address ) 
			addLatLonAddress = true;

		/////////////
		//
		// if neither lat nor lon has associated place then add addr
		//
		/////////////
		if ( addLatLonAddress ) {
			// if last address we added used the number at
			// savePos then we can't both be right. so compare
			if ( lastAddedPos == savePos &&
			     lastAddedWordDist == 0  &&
			     wordDist >= 2 )
				continue;
			if ( lastAddedPos == savePos &&
			     lastAddedCharDist > 1 &&
			     lastAddedCharDist < dist/2 &&
			     dist > 10 )
				continue;
			if ( lastAddedWord == wn1 &&
			     lastAddedWord == wn2 ) {
				// nuke what we had added just before
				if ( addedSomething ) {
					addedSomething = false;
					m_am.rewind(1);
					m_sm.rewind(1);
				}
				continue;
			}

			addedSomething = true;
			// note what we add
			if ( wn1 == wn2 ) lastAddedWord = wn1;
			else              lastAddedWord = -1;
			lastAddedPos      = start;
			lastAddedWordDist = wordDist;
			lastAddedCharDist = dist;
			// set this to the added address
			Address *retAddr = NULL;
			// . now try to add place vec to our array of addresses
			// . we now supply the containing section, "sec"
			//   so we can vote on which tag hash supplied the best
			//   addresses
			if ( ! addAddress ( NULL,//name1  ,
					    NULL,//name2  ,
					    NULL,//suite  ,
					    NULL,//street ,
					    NULL,//city   ,
					    NULL,//adm1   ,
					    NULL,//zip    ,
					    NULL   , // ctry   ,
					    NULL   ,
					    -1, // startAlnum ,
					    AF2_LATLON ,
					    &retAddr ) ) return false;
			// set lat/lon
			if ( type == 2 ) {
				retAddr->m_latitude  = saveVal;
				retAddr->m_longitude = dval;
			}
			else {
				retAddr->m_latitude  = dval;
				retAddr->m_longitude = saveVal;
			}
			// add the lat or lon as a simple place
			Place *pp = (Place *)m_sm.getMem(sizeof(Place));
			if ( ! pp ) return false; 
			pp->m_address = retAddr;
			// this seems good to do
			retAddr->m_street = pp;
			pp->m_str    = savePos;//start;
			pp->m_strlen = p - savePos;//start;
			int64_t h1 = *(int64_t *)&retAddr->m_latitude;
			int64_t h2 = *(int64_t *)&retAddr->m_longitude;
			pp->m_hash = hash64h ( h1 , h2 );
			pp->m_bits = 0; // |= PLF_FROMTAG;//|PLF_FROMTITLE;
			pp->m_a = wn1;
			pp->m_b = wn2+1;
			pp->m_flags2 = 0;
			pp->m_type = PT_LATLON;
			pp->m_flags2 = 0; // PLF2_IS_NAME;
			// address hash is usually set by calling 
			// getAddressHash() but just use the hash of the 
			// lat/lon from "street" we already computed
			retAddr->m_hash = pp->m_hash;
			//a->m_street = street;
			Section *as = NULL;
			if ( m_sections ) {
				as = m_sections->m_sectionPtrs[pp->m_a];
				retAddr->m_section = as;
			}
			// add the nearest city to that lat/lon so
			// that Address::getTimeZone() works
			float distInMilesSquared = 100.0;
			uint32_t cid32 = getNearestCityId(retAddr->m_latitude ,
							  retAddr->m_longitude,
							  m_niceness ,
							  &distInMilesSquared);
			// only set this if nearby...
			if ( distInMilesSquared < 1000)
				retAddr->m_cityId32 = cid32;
			else    
				retAddr->m_cityId32 = 0;
			// if we had "at Effex" then alias "Effex" to
			// this lat/lon address
			if ( ap1 ) ap1->m_alias = retAddr;
			continue;
		}

		// if we had matching associated places but the associated
		// place is not part of a good address, skip it
		if ( ! aa ) continue;

		// pick the highest score between us and the last guy,
		// AS LONG AS WE ARE A LONGITUDE since google maps always
		// has latitude then longitude
		if ( saveScore > score && type == 2 )
			score = saveScore;

		// get our distance
		//int32_t dist = start - savePos;

		// if we are know to be right, and it wasn't we can override
		// it without triggering the ambiguous flag
		if ( score > aa->m_latLonScore || 
		     // if score is tied but distance is less than, we can
		     // win on that too!
		     ( score == aa->m_latLonScore && dist<aa->m_latLonDist) ) {
			if ( type == 2 ) {
				aa->m_latitude  = saveVal;
				aa->m_longitude = dval;
			}
			else {
				aa->m_latitude  = dval;
				aa->m_longitude = saveVal;
			}
			aa->m_latLonScore = score;
			aa->m_latLonDist  = dist;
			continue;
		}

		// if we lost, bail
		if ( score < aa->m_latLonScore || dist > aa->m_latLonDist)
			continue;

		// . if already has one set flag
		// . but only mark it as ambiguous if the conflicting location
		//   is more than .010 of a degree off. this fixes abqcsl.org
		//   which has a few different &ll=x,y values in its goog url
		// . don't worry about it now since we have a geocoder
		// . this was causing a core because it was resetting the
		//   lat/lon of lat/lon only address for discovertherockies.com
		//   and was coring in Dates::getIntervals2() because the
		//   timezone was like "66" because the lat/lon was reset
		//   here to 888 or 999 or whatever
		// . but we need this in case there is ambiguity as to
		//   which lat/lon pair is the real deal when there are 
		//   mutiple ones in the same vicinity...
		// . so we have to nuke the address somehow if its lat/lon
		//   only
		if ( *ptr != dval && fabs(*ptr - dval) > .010 ) {
			*ptr = AMBIG_LATITUDE;
			*ptr = AMBIG_LONGITUDE;
		}
	}

	////////////////////////////////
	//
	// blank out the lat/lon if we do not have both for an address
	//
	////////////////////////////////
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// skip address if no lat/lon
		bool haveBoth = true;
		if ( ad->m_latitude  == NO_LATITUDE     ) haveBoth = false;
		if ( ad->m_latitude  == AMBIG_LATITUDE  ) haveBoth = false;
		if ( ad->m_longitude == NO_LONGITUDE    ) haveBoth = false;
		if ( ad->m_longitude == AMBIG_LONGITUDE ) haveBoth = false;
		if ( haveBoth ) continue;
		// blank out both otherwise
		ad->m_latitude  = NO_LATITUDE;
		ad->m_longitude = NO_LONGITUDE;
	}

	////////////////////////////////
	//
	// blank out all lat/lon of two are identical
	//
	// if two different addresses have the same lat/lon then disregard
	// all on that page
	//
	////////////////////////////////
	class Coordinate { public: double lat; double lon; };
	HashTableX dat;
	char datbuf[2000];
	dat.set ( 16 , 8 , 32 , datbuf , 2000 , false ,m_niceness,"latlontbl");
	Coordinate nukeList[5000];
	int32_t nc = 0;
	// scan the addresses and hash the lat/lon of each one
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// skip address if no lat/lon
		if ( ad->m_latitude == NO_LATITUDE ) continue;
		// skip if its a plain lat/lon address
		if ( ad->m_flags3 & AF2_LATLON ) continue;
		// make the coordinate
		Coordinate cc;
		cc.lat = ad->m_latitude;
		cc.lon = ad->m_longitude;
		// get it as a hash
		//int64_t h1 = *(int64_t *)((double *)&ad->m_latitude);
		//int64_t h2 = *(int64_t *)((double *)&ad->m_latitude);
		//int64_t h = hash64 ( h1 , h2 );
		//double pr = ad->m_latitude*ad->m_longitude;
		//int64_t h = *(int64_t *) &pr;
		// mix it up some more
		//h = hash64 ( h , h1 );
		//h = hash64 ( h , h2 );
		// if another entry that has this same lat/lon exists but
		// different address hash, then nuke them all!
		uint64_t *addrHash = (uint64_t *) dat.getValue ( &cc );
		// check if there
		if ( addrHash && *addrHash != ad->m_hash ) {
			//nuke = true;
			// now just add to the nuke list
			if ( nc < 5000 ) nukeList[nc++] = cc;
			break;
		}
		// hash it in "Dup Address Table"
		if ( ! dat.addKey ( &cc , &ad->m_hash ) ) return false;
	}
	for ( int32_t i = 0 ; nc > 0 && i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// skip if its a plain lat/lon address
		if ( ad->m_flags3 & AF2_LATLON ) continue;
		// see if in nuke like
		for ( int32_t j = 0 ; j < nc ; j++ ) {
			QUICKPOLL(m_niceness);
			if ( nukeList[j].lat != ad->m_latitude  ) continue;
			if ( nukeList[j].lon != ad->m_longitude ) continue;
			// blank it out
			ad->m_latitude  = NO_LATITUDE;
			ad->m_longitude = NO_LONGITUDE;
			break;
		}
	}

	////////////////////////////////
	//
	// set m_latitude and m_longitude for the same address
	//
	////////////////////////////////
	HashTableX nt4;
	HashTableX nt5;
	char ntbuf4[5000];
	char ntbuf5[5000];
	nt4.set ( 8,4,256,ntbuf4,5000,false,m_niceness,"nt4addr");
	nt5.set ( 8,4,256,ntbuf5,5000,false,m_niceness,"nt5addr");
	// hash words of the addresses
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// skip address if no lat/lon
		if ( ad->m_latitude != NO_LATITUDE &&
		     // do not add if already in there
		     ! nt4.isInTable(&ad->m_hash) )
			// return false if error adding
			if ( ! nt4.addKey(&ad->m_hash,&ad) ) return false;
		// deal with imported lat/lon too
		if ( ad->m_importedLatitude != NO_LATITUDE &&
		     // do not add if already in there
		     ! nt5.isInTable(&ad->m_hash) )
			// return false if error adding
			if ( ! nt5.addKey(&ad->m_hash,&ad) ) return false;
	}
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Address *ad = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// see if other same address but with lat/lon exists
		Address **pad = (Address **) nt4.getValue ( &ad->m_hash );
		// inherit otherwise
		if ( pad && ad->m_latitude == NO_LATITUDE ) {
			ad->m_latitude  = (*pad)->m_latitude;
			ad->m_longitude = (*pad)->m_longitude;
		}
		// see if other same address but with lat/lon exists
		Address **pad2 = (Address **) nt5.getValue ( &ad->m_hash );
		// inherit otherwise
		if ( pad2 && ad->m_importedLatitude == NO_LATITUDE ) {
			ad->m_importedLatitude  = (*pad2)->m_importedLatitude;
			ad->m_importedLongitude = (*pad2)->m_importedLongitude;
			ad->m_importedVotes     = (*pad2)->m_importedVotes;
		}
	}

	///////////////////////////////
	//
	// . set AF2_LATLONDUP for dup lat/lons like stubhub has
	//
	///////////////////////////////
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);
		// skip if its a plain lat/lon address
		if ( !(ad->m_flags3 & AF2_LATLON) ) continue;
		// see if in matches another
		for ( int32_t j = i+1 ; j < m_am.getNumPtrs() ; j++ ) {
			QUICKPOLL(m_niceness);
			Address *aj = (Address *)m_am.getPtr(j);
			// must also be lat/lon
			if ( !(aj->m_flags3 & AF2_LATLON) ) continue;
			// compute distance
			float d1 = ad->m_latitude  - aj->m_latitude;
			float d2 = ad->m_longitude - aj->m_longitude;
			if ( d1 >  .01 ) continue;
			if ( d2 >  .01 ) continue;
			if ( d1 < -.01 ) continue;
			if ( d2 < -.01 ) continue;
			// . ok, they are the same i guess
			// . prefer the one with the longest digits as the orig
			//   and the other as the alias
			if ( ad->m_street->m_strlen > aj->m_street->m_strlen){
				//aj->m_street->m_alias = ad;
				ad->m_street->m_flags3 |= PLF3_LATLONDUP;
			}
			else {
				//ad->m_street->m_alias = aj;
				aj->m_street->m_flags3 |= PLF3_LATLONDUP;
			}
		}
	}


	////////////////////////////////
	//
	// . SET AF3_SUPPLANTED
	// . fixes stubhub.com xml feed
	// . supplant afterAt and other lat/lon addresses with a single
	//   winning lat/lon address
	// . the problem with the getAssociatedPlace() logic above is
	//   that it only aliases out true street names or verified street
	//   names that are afterat... so we have to fix afterat streets
	//   that are not verified here. 
	// . fixes "blah blah at STUBHUB. <lat=yyy>><lon=xxx>" so that
	//   STUBHUB gets AF3_SUPPLANTED set so that Events.cpp ignores it
	//   as a competing address.
	//
	///////////////////////////////
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = (Address *)m_am.getPtr(i);
		// skip if not a lat/lon ADDRESS (unassociated with street)
		// i.e. an independent lat/lon because getAssociatedPlace()
		// above was returning NULL for this lat/lon..
		if ( !(ad->m_flags3 & AF2_LATLON) ) continue;
		// skip if dup lat/lon though
		if ( ad->m_street &&
		     (ad->m_street->m_flags3 & PLF3_LATLONDUP) ) 
			continue;
		// get its section and blow it up until right before we
		// hit a verified fake street name or we hit a street name
		// or we hit a latlon that is not a latlondup.
		// use Section::m_firstPlaceNum. we set that above, but
		// we also set it right below in a secon call to
		// setFirstPlaceNums().
		Section *sk = sp[ad->m_street->m_a];
		// telescope section up around this lat/lon address
		for ( ; sk ; sk = sk->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get it
			int32_t pi = sk->m_firstPlaceNum;
			bool hitRealStreet = false;
			// . scan places in this section
			// . just like Events.cpp address assigning algo does
			for ( ; pi >= 0 && pi < m_numSorted ; pi++ ) {
				// get it
				Place *sr = m_sorted[pi];
				// stop if section breach
				if ( sr->m_a >= sk->m_b ) break;
				// sanity
				if ( sr->m_a < 0 ) { char *xx=NULL;*xx=0; }
				// skip us
				if ( sr == ad->m_street ) continue;
				// ignore if POBOX
				if ( sr->m_flags2 & PLF2_IS_POBOX ) continue;
				// skip if dup latlon
				if ( sr->m_flags3 & PLF3_LATLONDUP ) continue;
				// is the street name really a place name?
				bool isName  = ( sr->m_flags2 & PLF2_IS_NAME );
				// skip if fake name
				if ( isName ) continue;
				// stop on real street (not-fake name)
				hitRealStreet = true; 
				break;
			}
			// stop if we hit real street!
			if ( hitRealStreet ) 
				break;
			// ok, supplant all if no real street name to go
			// with our lat/lon
			pi = sk->m_firstPlaceNum;
			// do the scan again
			for ( ; pi >=0 && pi < m_numSorted ; pi++ ) {
				// get it
				Place *sr = m_sorted[pi];
				// stop if section breach
				if ( sr->m_a >= sk->m_b ) break;
				// sanity
				if ( sr->m_a < 0 ) { char *xx=NULL;*xx=0; }
				// skip us
				if ( sr == ad->m_street ) continue;
				// flag it
				sr->m_flags3 |= PLF3_SUPPLANTED;
			}
		}
	}





	////////////////////////////////
	//
	// normalize m_latitude and m_longitude to be from 0 to 360
	// no! - just do in Events::hash() now
	//
	////////////////////////////////
	/*
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get address
		Address *ad = &m_addresses[i];
		// skip address if no lat/lon
		if ( ad->m_latitude == NO_LATITUDE    ) continue;
		if ( ad->m_latitude == AMBIG_LATITUDE ) continue;
		ad->m_latitude  += 180.0;
		ad->m_longitude += 180.0;
	}
	*/


	////////////////////
	//
	// set Address::m_timeZoneOffset (from GMT)
	//
	////////////////////
	/*
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get it
		Address *aa = &m_addresses[i];
		Place *city = aa->m_city;
		Place *zip  = aa->m_zip;
		Place *adm1 = aa->m_adm1;
		// and city hash
		uint64_t cityHash = 0;
		if      ( city ) cityHash = city->m_hash;
		else if ( zip  ) cityHash = zip->m_cityHash;
		if ( ! cityHash ) { char *xx=NULL;*xx=0; }
		// need this
		char *adm1Str = NULL;
		if      ( adm1 ) adm1Str = adm1->m_adm1;
		else if ( zip  ) adm1Str = zip->m_adm1;
		else if ( city && city->m_adm1[0] ) adm1Str = city->m_adm1;
		else    { char *xx=NULL;*xx=0; }
		// sanity check
		if ( is_upper_a(adm1Str[0]) ) { char *xx=NULL;*xx=0; }
		if ( is_upper_a(adm1Str[1]) ) { char *xx=NULL;*xx=0; }
		uint32_t adm1Hash32 = (uint32_t)*((uint16_t *)adm1Str);
		uint32_t cityHash32 = (uint32_t)cityHash;
		// combine the two hashes
		uint32_t cityStateHash = hash32h(cityHash32,adm1Hash32);
		// get timezone
		int32_t slot = g_timeZones.getSlot ( &cityStateHash );
		// call it 0 if not good
		aa->m_timeZoneOffset = 0;
		// otherwise, set m_timeZoneOffset appropriately
		if ( slot >= 0 )
			aa->m_timeZoneOffset = *(char *)g_timeZones.
				getValueFromSlot(slot);
	}
	*/


	//////////////////////////
	//
	// set Section::m_firstPlaceNum
	//
	// . so we can quickly scan the places contained by a section
	//
	//////////////////////////
	if ( ! setFirstPlaceNums() ) return false;



	////////////////////
	//
	// count # of valid/inlined addresses we have
	//
	////////////////////
	m_numValid = 0;
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get it
		Address *aa = (Address *)m_am.getPtr(i);//&m_addresses[i];
		// is inlined or verified?
		bool valid = false;
		if ( aa->m_flags & AF_INLINED               ) valid = true;
		// but unverified streetisname is not good
		if ( aa->m_street && (aa->m_street->m_flags2 & PLF2_IS_NAME) )
			valid = false;
		if ( aa->m_flags & AF_VERIFIED_PLACE_NAME_1 ) valid = true;
		if ( aa->m_flags & AF_VERIFIED_PLACE_NAME_2 ) valid = true;
		if ( aa->m_flags & AF_VERIFIED_STREET       ) valid = true;
		if ( ! valid ) continue;
		m_numValid++;
		aa->m_flags3 |= AF2_VALID;
	}


	return true;
}


static void gotGeocoderReply ( void *state , TcpSocket *s ) {
	// get us
	Addresses *THIS = (Addresses *)state;
	// process it
	THIS->processGeocoderReply ( s );
	// call callback
	THIS->m_callback ( THIS->m_state );
}

// . set m_geocoderLat/m_geocoderLon
// . returns false if blocks
// . returns true with g_errno set on error
// . only call from Events.cpp if we have 1+ valid event that will be
//   indexed...
bool Addresses::setGeocoderLatLons ( void *state, 
				     void (*callback) (void *state) ) {

	// only call this once unless we get reset()
	if ( m_calledGeocoder ) return true;
	m_calledGeocoder = true;

	m_callback = callback;
	m_state    = state;

	// store candidates to select from here
	int32_t cands[MAX_GEOCODERS];
	int32_t nc = 0;
	// select a geocoder by IP
	for ( int32_t i = 0 ; i < MAX_GEOCODERS ; i++ ) {
		// check ip
		if ( ! g_conf.m_geocoderIps[i] ) continue;
		// add to candidates
		cands[nc++] = g_conf.m_geocoderIps[i];
	}
	// if none, bail, we do not do this
	if ( nc <= 0 ) return true;

	int32_t need = 0;
	// loop over each valid address we and add to request size
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get it
		Address *aa = (Address *)m_am.getPtr(i);
		// reset
		aa->m_geocoderLat = 999;
		aa->m_geocoderLon = 999;
		// is inlined or verified?
		if ( ! ( aa->m_flags3 & AF2_VALID ) ) continue;
		// only do it if used in event now
		if ( ! ( aa->m_flags3 & AF2_USEDINEVENT ) ) continue;
		// skip if lat/lon address
		if ( aa->m_flags3 & AF2_LATLON ) {
			// just inherit that
			aa->m_geocoderLat = aa->m_latitude;
			aa->m_geocoderLon = aa->m_longitude;
			continue;
		}
		// check the cache first!!! used by Repair.cpp to speed up!!
		int64_t key64 = aa->m_hash;
		double *recs;
		int32_t    recSize;
		bool inCache = m_latLonCache.getRecord ( (collnum_t) 0,
							 (char *)&key64 ,
							 (char **)&recs ,
							 &recSize ,
							 false ,
							 3600 ,
							 false );
		if ( inCache && recs && recs[0] != 999 ) {
			aa->m_geocoderLat = recs[0];
			aa->m_geocoderLon = recs[1];
			continue;
		}

		// request needs street,state,city (and zip if there)
		need += aa->m_street->m_strlen + 1;
		// get city length
		if ( aa->m_city ) need += aa->m_city->m_strlen;
		else if ( aa->m_zip ) need += strlen(aa->m_zip->m_cityStr);
		else if ( aa->m_flags3 & AF2_LATLON );
		else { char *xx=NULL;*xx=0; }
		if ( aa->m_zip ) need += 2 + aa->m_zip->m_strlen;
		//need += aa->m_adm1->m_strlen + 1;
		need += 2; // use state abbr
		need += 20; // addrXXX=...&
	}

	// if none valid, vail
	if ( need == 0 ) return true;

	// need url cruft "http://..../"
	need += 100;

	char sbuf[5024];
	char *requestBuf = NULL;
	if ( need < 5024  ) requestBuf = sbuf;
	if ( ! requestBuf ) requestBuf = (char *)mmalloc(need,"geocode");
	if ( ! requestBuf ) return true;

	// make the url
	char *p = requestBuf;
	// select a geocoder randomly
	int32_t r = rand() % nc;
	// to request manually:
	// http://10.5.66.11:5678/json/+2935-D+Louisiana+NE,+Albuquerque,+NM
	// http://10.5.66.11:5678/txt/+2935-D+Louisiana+NE,+Albuquerque,+NM
	// make the request
	p += sprintf(p,"POST /xml? HTTP/1.0\r\n"
		     "Accept: */*\r\n"
		     "Host: %s:5678\r\n"
		     "Content-Length: xxxxxx\r\n"
		     "\r\n",
		     iptoa(cands[r]));

	int32_t num = 1;
	char *contentStart = p;
	// loop over each valid address we and add to request size
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get it
		Address *aa = (Address *)m_am.getPtr(i);
		// is inlined or verified?
		if ( ! ( aa->m_flags3 & AF2_VALID ) ) continue;
		// only do it if used in event now
		if ( ! ( aa->m_flags3 & AF2_USEDINEVENT ) ) continue;
		// skip if we got it already in the cache above
		if ( aa->m_geocoderLat != 999 ) continue;
		// for debugging
		//char *start = p;
		// request needs street,state,city (and zip if there)
		p += sprintf(p,"addr%"INT32"=",num++);
		gbmemcpy(p,aa->m_street->m_str,aa->m_street->m_strlen);
		p += aa->m_street->m_strlen;
		*p++ = ',';
		*p++ = ' ';
		if ( aa->m_city ) {
			gbmemcpy(p,aa->m_city->m_str,aa->m_city->m_strlen);
			p += aa->m_city->m_strlen;
		}
		else if ( aa->m_zip ) {
			int32_t clen = strlen(aa->m_zip->m_cityStr);
			gbmemcpy(p,aa->m_zip->m_cityStr,clen);
			p += clen;
		}
		else if ( aa->m_flags3 & AF2_LATLON );
		else { char *xx=NULL; *xx=0; }
		*p++ = ' ';
		// get state abbr
		if      ( aa->m_adm1 ) {
			gbmemcpy(p,aa->m_adm1->m_adm1,2);
		}
		else if ( aa->m_zip )  {
			gbmemcpy(p,aa->m_zip->m_adm1,2);
		}
		else if ( aa->m_flags3 & AF2_LATLON );
		else { char *xx=NULL;*xx=0; }
		p += 2;
		// zip if we got it, seems to help geocoder sometimes
		if ( aa->m_zip ) {
			*p++ = ' ';
			int32_t zlen = aa->m_zip->m_strlen;
			gbmemcpy(p,aa->m_zip->m_str,zlen);
			p += zlen;
		}
		*p++ = '&';
		// log debug
		//log("addr: GET %s",start);
	}
	// null term
	*p = '\0';

	// fix content-length
	char *qq = strstr(requestBuf,"xxxxxx");
	if ( ! qq ) { char *xx=NULL;*xx=0; }
	if ( p-contentStart > 999999 ) { char *xx=NULL;*xx=0; }
	sprintf(qq,"%06"INT32"",(int32_t)(p-contentStart));
	qq[6]='\r'; // sprintf might have written a \0, so put \r back

	// finish it
	//p += sprintf(p," HTTP/1.0\r\n\r\n");
	// size of it
	int32_t reqLen = p - requestBuf;
	// sanity
	if ( reqLen >= need ) { char *xx=NULL;*xx=0; }
	// send it off to get back xml reply
	bool status = g_httpServer.getDoc( cands[r]         , // ip
					   5678             , // port
					   requestBuf       ,
					   reqLen           ,
					   this             ,
					   gotGeocoderReply ,
					   60*1000          , // timeout 60s
					   -1               , // no max
					   -1               );// no max
	// free the request since it mdups it
	if ( requestBuf != sbuf ) mfree ( requestBuf , need , "geocode" );
	// return false if it blocked
	if ( ! status ) return false;
	// error? ENOMEM?
	if ( g_errno ) {
		log("addr: get geocoder lat lon: %s",mstrerror(g_errno));
		return true;
	}
	// otherwise, should always block!
	char *xx=NULL;*xx=0;
	return true;
}

// process it
bool Addresses::processGeocoderReply ( TcpSocket *s ) {

	if ( g_errno ) {
		log("addr: geocoder reply: %s",mstrerror(g_errno));
		g_errno = EBADGEOCODERREPLY;
		return true;
	}
	// get reply
	char *reply      = s->m_readBuf;
	//int32_t  replyAlloc = s->m_readBufSize;
	//int32_t  replySize  = s->m_readOffset;

	// same for an empty reply
	if ( ! reply || s->m_readBufSize == 0 ) {
		g_errno = EBADGEOCODERREPLY;
		log("addr: geocoder returned empty reply: %s",
		    mstrerror(g_errno));
		return true;
	}
		
	// breathe
	QUICKPOLL(m_niceness);

	int32_t num = 0;
	// loop over each valid address we and add to request size
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe 
		QUICKPOLL(m_niceness);
		// get it
		Address *aa = (Address *)m_am.getPtr(i);
		// is inlined or verified?
		if ( ! ( aa->m_flags3 & AF2_VALID ) ) continue;
		// only do it if used in event now
		if ( ! ( aa->m_flags3 & AF2_USEDINEVENT ) ) continue;
		// skip if we got it already in the cache above
		if ( aa->m_geocoderLat != 999 ) continue;
		// inc it
		num++;
		// make the tag name
		char tagName[32];
		sprintf(tagName,"<addr%"INT32">",num);
		// ok now get that reply
		char *p = strstr(reply,tagName);
		// not found?
		if ( ! p ) {
			log("addr: missing geocoder reply for addr #%"INT32"",num);
			continue;
		}
		// get end tag of it
		char endTagName[32];
		sprintf(endTagName,"</addr%"INT32">",num);
		char *end = strstr(p,endTagName);
		// strange!
		if ( ! end ) {
			log("addr: missing geocoder endtag for addr #%"INT32"",num);
			continue;
		}
		// tmp shutoff
		char c = *end;
		*end = '\0';

		// set official latitude, this
		double lastLat = NO_LATITUDE;
		// ok, got it, grab all possible lat/lons for it
		for ( char *s = strstr(p,"<lat>"); s ; s=strstr(s+1,"<lat>")){
			// breathe
			QUICKPOLL(m_niceness);
			// get that
			double lat = atof(s+5);
			// had a last? if so, and they do not match, then
			// give up because i'm not sure which is right
			if ( lastLat != NO_LATITUDE && lat  != lastLat ) {
				lastLat = NO_LATITUDE;
				break;
			}
			// mark this
			lastLat = lat;
		}

		// same for longitude
		double lastLon = NO_LONGITUDE;
		// ok, got it, grab all possible lon/lons for it
		for ( char *s = strstr(p,"<lon>"); s ; s=strstr(s+1,"<lon>")){
			// breathe
			QUICKPOLL(m_niceness);
			// get that
			double lon = atof(s+5);
			// had a last? if so, and they do not match, then
			// give up because i'm not sure which is right
			if ( lastLon != NO_LONGITUDE && lon  != lastLon ) {
				lastLon = NO_LONGITUDE;
				break;
			}
			// mark this
			lastLon = lon;
		}
		
		// put back for next address's reply
		*end = c;

		// skip if not good
		if ( lastLat == NO_LATITUDE  || lastLon == NO_LONGITUDE ) {
			// log it now
			SafeBuf sb;
			sb.safeMemcpy(aa->m_street->m_str,
				      aa->m_street->m_strlen);
			if ( aa->m_city ) {
				sb.pushChar(',');
				sb.safeMemcpy(aa->m_city->m_str,
					      aa->m_city->m_strlen);
			}
			if ( aa->m_adm1 ) {
				sb.pushChar(',');
				sb.safeMemcpy(aa->m_adm1->m_str,
					      aa->m_adm1->m_strlen);
			}
			if ( aa->m_zip && aa->m_zip->m_strlen ) {
				sb.pushChar(',');
				sb.safeMemcpy(aa->m_zip->m_str,
					      aa->m_zip->m_strlen);
			}
			log("addr: geocoder failed on %s",sb.getBufStart());
			continue;
		}
		// otherwise, set it!
		aa->m_geocoderLat = lastLat;
		aa->m_geocoderLon = lastLon;
	}

	// free when done
	//mfree ( reply , replyAlloc , "geocodrp");
	return true;
}

void Address::getLatLon( double *lat, double *lon ) {
	// use geocoder if valid
	if ( m_geocoderLat != NO_LATITUDE && m_geocoderLon != NO_LONGITUDE ) {
		*lat = (double)m_geocoderLat;
		*lon = (double)m_geocoderLon;
		return;
	}
	// use other guy otherwise
	if ( m_latitude != NO_LATITUDE && m_longitude != NO_LONGITUDE ) {
		*lat = (double)m_latitude;
		*lon = (double)m_longitude;
		return;
	}
	// otherwise, no go
	*lat = NO_LATITUDE;
	*lon = NO_LONGITUDE;
}

bool hashPlaceName ( HashTableX *nt1,
		     Words *words,
		     int32_t a ,
		     int32_t b ,
		     uint64_t v ) {

	int64_t *wids = words->m_wordIds;
	// hash
	for ( int32_t k = a ; k < b ; k++ ) {
		// skip if not word
		if ( ! wids[k] ) continue;
		// add it
		if ( ! nt1->addKey ( &wids[k] , &v ) ) return false;
	}
	return true;
}



// returns -1 and sets g_errno on error
int32_t getCommonWordIds ( int32_t a1 , int32_t b1 ,
			int32_t a2 , int32_t b2 ,
			int64_t *wids      ,
			int64_t *commonIds ,
			int32_t max ,
			int32_t niceness ) {
	int32_t nc = 0;
	HashTableX ht;
	char sbuf[640];
	ht.set ( 8,0,64,sbuf,640,false,niceness,"cmmnwrds");
	// hash first round
	for ( int32_t i = a1 ; i < b1 ; i++ ) {
		// skip if not word
		if ( ! wids[i] ) continue;
		// add it otherwise
		if ( ! ht.addKey ( &wids[i] ) ) return -1;
	}
	// now check the other guy
	for ( int32_t i = a2 ; i < b2 ; i++ ) {
		// skip if not word
		if ( ! wids[i] ) continue;
		// add it otherwise
		if ( ! ht.isInTable ( &wids[i] ) ) continue;
		// add him to our common list
		commonIds[nc++] = wids[i];
		// stop if no room left
		if ( nc >= max ) break;
	}
	// return that
	return nc;
}


Place *Addresses::getAssociatedPlace ( int32_t i ) {
	// get smallest section containing word #i
	Section *si = m_sections->m_sectionPtrs[i];
	// scan addresses also in this section
	for ( ; si ; si = si->m_parent ) {
		// key mixing now
		//int32_t key = hash32h((int32_t)si,456789);
		// ok, now telescope our section out until we
		// find the address
		//int32_t slot = pt->getSlot ( &key );
		// get it
		int32_t pi = si->m_firstPlaceNum;
		// telescope if none
		//if ( slot < 0 ) continue;
		if ( pi < 0 ) continue;
		// count them
		//int32_t count = 0;
		int64_t lasth = 0LL;
		Place *lastpp = NULL;
		// . scan the addresses in section "si"
		// . the places in m_sorted[] are streets or are verfied
		//   place names
		for ( ; pi < m_numSorted ; pi++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get place
			Place *pp = m_sorted[pi];
			// stop if breach
			if ( pp->m_a >= si->m_b ) break;
			// get that place
			//Place *pp = *(Place **)pt->getValueFromSlot(slot);
			// use address or alias
			Address *aa = pp->m_address;
			if ( ! aa ) aa = pp->m_alias;
			// get hash. fix www.reverbnation.com/venue/448772
			// which has "Low Spirits" as a place which aliases
			// to an address whose street is 2823 2nd St NW. as
			// are all the places around this url's only pair of
			// valid lat/lon coordinates.
			int64_t h = pp->m_hash;
			if ( aa ) h = aa->m_street->m_hash;
			// compare to last h
			if ( lasth && h != lasth ) { lastpp = NULL; break; }
			// set it for next guy
			lasth = h;
			// save it
			lastpp = pp;
			// count them
			//count++;
		}
		// if multiple stop, we can not be sure with
		// which address we are associated
		//if ( count >= 2 ) 
		//	break;
		//if ( slot >= 0 )
		//	break;
		//if ( ! lastpp )
		//	break;
		// get that address
		//Place *pa = *(Place **)pt->getValue(&key);
		// this returns NULL if we had multiple possible addresses
		return lastpp; // pa;
	}
	return NULL;
}

// . array for setting s_lc hashtable
// . these are words that can be lower case in a place name
// . fixes "Santa Maria de la Paz Catholic Church" not being a place name
static char      *s_lcWords[] = {
        "de",
        "la",

        "at",
        "be",
        "by",
        "of",
        "on",
        "or",
        "in",

        "re", // you're
        "to",
        "vs",
        "the",
        "and",
        "are",
        "for",

	"s", // Slim's

	"y", // spanish "Pupuseria y Restaurant Salvado"
	"del", // spanish "this" "Bosque del Apache National Wildfile Refuge"
	"del", // spanish "of" "Casa de las Chimeneas"
	"las", // spanish "the"

        "not",
        "from",
        "ll",    // they'll this'll that'll you'll
        "ve",    // would've should've
	NULL
};


// returns false with g_errno set on error
bool setHashes ( Place *p , Words *ww , int32_t niceness ) {

	//Words *ww = m_words;
	int32_t a = p->m_a;
	int32_t b = p->m_b;

	// adm1 hash is just hash of the two letters
	if ( p->m_type == PT_STATE ) {
		// must be there
		// do not core here anymore since we coule be a foreign
		// latlon only place in which case this will be zero.
		// happens when such a place is in the contactinfo tag
		//if ( ! p->m_adm1Bits ) { char *xx=NULL;*xx=0;}
		//p->m_hash = hash64Lower_utf8 ( p->m_adm1 , 2);
		// will this work?
		p->m_hash = p->m_adm1Bits;
		return true;
	}

	// if place name was taken from a tag or placedb then we have
	// to set the words class ourself
	Words tmp;
	if ( p->m_a < 0 ) {
		// return false with g_errno set on error
		if ( ! tmp.set ( p->m_str , 
				 p->m_strlen ,
				 TITLEREC_CURRENT_VERSION ,
				 true ,
				 niceness ) ) return false;
		// set it up
		ww = &tmp;
		a = 0;
		b = ww->m_numWords;
	}

	int64_t *wids  = ww->m_wordIds;
	int32_t      *wlens = ww->m_wordLens;
	char     **wptrs = ww->m_words;
	int32_t       nw    = ww->m_numWords;

	// the straight up hash
	int64_t h = 0LL;
	// hash of the non indicator alpha words in street name
	int64_t h1  = 0;
	// . includes hash of directional indicators
	// . we only use this if street name is a directional indicator
	int64_t h2  = 0;
	int64_t h2b = 0;
	int64_t h3  = 0;
	int64_t h4  = 0;
	// word id of previous word
	int64_t pi  = 0LL;

	int32_t alphaCount = 0;
	int64_t prevIndId = 0LL;

	// to fix the street that is "25 School" we cannot map "school"
	// to h_zero
	bool isStreet = ( p->m_type == PT_STREET );

	// sanity check -- no, suites start with punct!
	//if ( ! wids[a] ) { char *xx=NULL;*xx=0; }
	p->m_simpleHash32 = 0;

	// loop over words
	for ( int32_t i = a ; i < b ; i++ ) {
		// skip if not alnum word
		if ( ! wids[i] ) continue;
		// make a simple hash so setting the EV_STORE_HOURS flag
		// in Events.cpp works, since we compare it to the simple
		// hash of the event title
		p->m_simpleHash32 ^= (uint32_t)wids[i];
		// this logic taken from Sections.cpp where it is setting
		// Section::m_sentenceContentHash
		if ( p->m_simpleHash32 == 0 )
			p->m_simpleHash32 = 123456;
		// get synonym of word id
		int64_t *swid = getSynonymWord ( &wids[i] , &pi , isStreet );
		// word id of previous word
		pi = wids[i];
		
		// mix it up
		h <<= 1LL;
		// xor it in
		h ^= *swid;

		// done if not street
		if ( p->m_type != PT_STREET ) continue;

		// is street a place name in disguise? if so, continue
		if ( p->m_flags2 & PLF2_IS_NAME ) continue;

		// int16_tcut
		bool isNum = ww->isNum2(i);
		// count it
		if ( ! isNum ) alphaCount++;

		// the street num hash, hash of the first number
		if ( isNum && h3 == 0 ) h3 = wids[i];

		// is this word like "st" or "ave" or "blvd"
		IndDesc *id=(IndDesc *)g_indicators.getValue(swid);

		// hash of last "indicator"
		if ( id ) {
			// map them
			h4 = *swid;
			// map "N.E." to "NE"
			if ( prevIndId == h_north && *swid == h_east )
				h4 = h_northeast;
			if ( prevIndId == h_north && *swid == h_west )
				h4 = h_northwest;
			if ( prevIndId == h_south && *swid == h_east )
				h4 = h_southeast;
			if ( prevIndId == h_south && *swid == h_west )
				h4 = h_southwest;
			// save that
			prevIndId = *swid;
		}
		// prevIndId only means for the previous word, so reset it
		else 
			prevIndId = 0LL;

		// set some flags based on indFlags
		bool isStreetInd = ( id && (id->m_bit & IND_STREET) );
		bool isDir       = ( id && (id->m_bit & IND_DIR   ) );

		// cancel the 'S' indicator if potential
		// apostrophe! "aug 17 burt's lounge"
		// we do not want "17 burt's"
		if ( isDir &&
		     wlens[i] == 1 && 
		     (wptrs[i][0]=='s' || wptrs[i][0]=='S') && 
		     i > 1 &&
		     wptrs[i][-1] != ' ' ) 
			isDir = false;

		// . update this. 
		// . exclude numbers from this!
		// . allow other numbers if no alpha word before them!
		// . exclude directional indicators from this
		// . MDW: for PLF2_INTERSECTION "streets" we need to allow
		//   when i == a! because we do not have numeric addresses
		//   for intersections, so made it from i>a to i>=a
		if ( i >= a && 
		     // but allow directional indicators if right after 
		     // the street number though, like "123 west street"
		     ( ! isDir || i == a + 2 ) &&
		     // commenting this out hurts "100 3/4 road"
		     // but it helps "2001 1/2 montgomery blvd"
		     //( ! isNum || alphaCount == 0 ) &&
		     ! isNum &&
		     ! isStreetInd ) {
			// mix it up
			h1 <<= 1;
			// xor it
			h1 ^= *swid;//wids[j];
		}

		// fix "2804 hwy 250" from excluding the "250"
		if ( isNum && alphaCount > 0 ) {
			// mix it up
			h1 <<= 1;
			// xor it
			h1 ^= *swid;//wids[j];
		}

		// set back up hash in case the others are 0
		if ( isStreetInd ) {
			h2b <<= 1;
			h2b ^= wids[i];
		}

		if ( isDir ) {
			// mix it  up
			h2 <<= 1;
			// include it in this
			h2 ^= wids[i];
		}

	}

	// set hash
	p->m_hash = h;

	// keep this as it is
	p->m_wordHash64 = h;

	// . if we are a city look up in g_places and see if we are an
	//   alias for a different city name
	// . fix "abq" so it maps to albuquerque
	// . we now fixed getAddressHash() so this logic is not needed
	//if ( p->m_type == PT_CITY ) { // && (p->m_flags & PF_IS_ALIAS) ) {
	//	// convert hash to alias hash
	//	int64_t *newh = (int64_t *)g_aliases.getValue ( &h );
	//	// set that to h now
	//	if ( newh ) p->m_hash = *newh;
	//	// could not find this city in the table... strange
	//	return true;
	//}


	// done if not street
	if ( p->m_type != PT_STREET ) return true;

	// only use the purer hash if it is non-zero
	if      ( h1 )  p->m_hash = h1;
	else if ( h2 )  p->m_hash = h2;
	else            p->m_hash = h2b;

	// sanity check
	//if ( p->m_hash == 0 ) { char *xx=NULL;*xx=0; }

	p->m_streetNumHash = h3;
	p->m_streetIndHash = h4;

	// if we are a "fake" street
	if ( p->m_flags2 & PLF2_IS_NAME )
		// PROBLEM: the street "6201 San Antonio Dr NE" is matching the
		// place name "San Antonio" so let's mix up "h" a little when
		// we are using "place names" in place of the street
		// ALSO, lets revert it back to "h" not "h1", since "h1" is
		// probably zero since i added that extra "continue" above.
		p->m_hash = h ^ 0x123456;

	// . sanity check
	// . no! the word "The" has a hash of 0, and we don't add it
	//   from the caller's point
	//if ( p->m_hash == 0LL ) { char *xx=NULL;*xx=0; }

	// done if a fake street
	if ( p->m_flags2 & PLF2_IS_NAME ) return true;

	// done if street was not a "pobox street"
	if ( to_lower_a(wptrs[a][0])!='p' )  return true;

	// assume none
	int32_t k = -1;

	// "p o box 123"
	if ( a + 6 < nw &&
	     wids[a  ] == h_p   && 
	     wids[a+2] == h_o   &&
	     wids[a+4] == h_box &&
	     is_digit(wptrs[a+6][0]) )
		k = a + 6;
	// "p o box 123"
	if ( a + 6 < nw &&
	     wids[a  ] == h_post   && 
	     wids[a+2] == h_office &&
	     wids[a+4] == h_box &&
	     is_digit(wptrs[a+6][0]) ) 
		k = a + 6;
	// "po box 123"
	if ( a + 4 < nw &&
	     wids[a  ] == h_po  && 
	     wids[a+2] == h_box &&
	     is_digit(wptrs[a+4][0]) )
		k = a + 4;
	// "p.o. 81255"
	if ( a + 4 < nw &&
	     wids[a  ] == h_p && 
	     wids[a+2] == h_o &&
	     is_digit(wptrs[a+4][0]) ) 
		k = a + 4;
	// "p o b 81255"
	if ( a + 6 < nw &&
	     wids[a  ] == h_p && 
	     wids[a+2] == h_o &&
	     wids[a+4] == h_b &&
	     is_digit(wptrs[a+6][0]) ) 
		k = a + 6;

	// not a po box i guess
	if ( k == -1 ) return true;

	// xor it in along with h_po
	p->m_hash = h_po ^ wids[k];

	return true;
}

static HashTableX s_lc;
//static char s_lcbuf[2000];
static HashTableX s_jobTable;

#define MAX_ALNUMS_IN_NAME 16

// . called from above
// . returns false and sets g_errno on error
bool Addresses::set2 ( ) {
	// sanity check
	if ( ! s_init ) { char *xx=NULL; *xx=0; }

	bool printed = false;

	// int16_tcuts
	int32_t       nw = m_words->getNumWords();
	// msg13 provides a NULL sections ptr. it can't set them for speed!
	// it is the spider compression proxy...
	Section  **sp = NULL;
	if ( m_sections ) sp = m_sections->m_sectionPtrs;
	// int16_tcut
	//Sections *ss = m_sections;
	// reset # of addresses we got
	//m_na = 0;
	// and streets
	//m_ns = 0;
	// and cities, states, zips
	//m_np = 0;

	// place mem and street mem and address mem
	m_pm.reset();
	m_sm.reset();
	m_am.reset();

	// init them. poolSize=5000.initnumpoolptrs=300.initnumplaceptrs=3000
	m_pm.init(15000,300,3000,NULL,0,m_niceness);
	m_sm.init(15000,300,3000,NULL,0,m_niceness);
	m_am.init(15000,300,3000,NULL,0,m_niceness);

	// . inherit from contact info page ONLY IF NO OTHERS
	// . tag format =  "city=x;adm1=*;adm2=*;country=*"
	// . get up to 10 addresses from the contact info 
	Address da[10];
	// init
	int32_t dc = 0;
	// first address is the empty one
	memset ( &da[0] , 0 , sizeof(Address) );
	// skip it 
	dc++;

	// get contact info addresses, use their city/state for our addresses
	int32_t tt = getTagTypeFromStr ( "contactaddress" );
	Tag *tag = NULL;
	// . taken from TagRec::getTag() function
	// . Msg13.cpp does not have tag..
	if ( m_gr ) tag = m_gr->getFirstTag();
	// loop over all contact info addresses in the TagRec
	for ( ; tag && dc < 10 ; tag = m_gr->getNextTag(tag) ){
		// breathe
		QUICKPOLL(m_niceness);
		// . skip if not a "address" tag (ci=contactInfo)
		// . no, now these are venue default addresses
		if ( tag->m_type != tt ) continue;
		// get str
		char *str = tag->getTagData();
		// reserve mem for it
		
		// . set address, da[dc], from tag "tag"
		// . flags to OR into Place::m_bits
		if(!setFromStr(&da[dc],str,PLF_FROMTAG,&m_pm,m_niceness))
			return false;
		// if it was a latlon only address, just skip it for now
		// because i'm not sure what the effects will be. plus its
		// m_adm1 and m_city are typically NULL!!
		if ( da[dc].m_flags3 & AF2_LATLON ) 
			continue;
		// check it out
		// . this just means it was an AF2_LATLON but we were not
		//   able to set that because it has the foreign state
		//   and city and country set.
		//if ( ! da[dc].m_adm1->m_hash ) { char *xx=NULL;*xx=0; }
		if ( ! da[dc].m_adm1->m_hash ) continue;
		// advance
		dc++;
	}

	/*
	// . inherit from what abyznewslinks.com says about our place
	// . tag format =  "city=x;adm1=*;adm2=*;country=*"
	if ( ( tag = m_gr->getTag("abyznewslinks.address") ) &&
	     // skip if not a "address" tag (ci=contactInfo)
	     tag->m_type == tt ) {
		// get str
		char *str = tag->m_data;
		// . set address, da[dc], from tag "tag"
		// . flags to OR into Place::m_bits
		if ( ! setFromStr ( &da[dc] , str,PLF_FROMTAG,m_niceness)) 
			return false;
		// advance
		dc++;
	}
	*/

	// now use the default venue address, should be more accurate?
	tt = getTagTypeFromStr ( "venueaddress" );
	// taken from TagRec::getTag() function
	if ( m_gr ) tag = m_gr->getFirstTag();
	// loop over all contact info addresses in the TagRec
	for ( ; tag && dc < 10 ; tag = m_gr->getNextTag(tag) ){
		// breathe
		QUICKPOLL(m_niceness);
		// . skip if not a "address" tag (ci=contactInfo)
		// . no, now these are venue default addresses
		if ( tag->m_type != tt ) continue;
		// get str
		char *str = tag->getTagData();
		// . set address, da[dc], from tag "tag"
		// . flags to OR into Place::m_bits
		if(!setFromStr(&da[dc],str,PLF_FROMTAG,&m_pm,m_niceness))
			return false;
		// if it was a latlon only address, just skip it for now
		// because i'm not sure what the effects will be. plus its
		// m_adm1 and m_city are typically NULL!!
		if ( da[dc].m_flags3 & AF2_LATLON ) 
			continue;
		// check it out
		// . this just means it was an AF2_LATLON but we were not
		//   able to set that because it has the foreign state
		//   and city and country set.
		//if ( ! da[dc].m_adm1->m_hash ) { char *xx=NULL;*xx=0; }
		if ( ! da[dc].m_adm1->m_hash ) continue;
		// advance
		dc++;
		// stop it
		break;
	}

	// let's use the meta description as well.
	// should get jonson gallery on collectorsguide.com
	//char *md = m_xd->getMetaDescription();
	


	// . if section flag is one of these, ignore the words in it
	// . google seems to index marquee, so i took SEC_MARQUEE out
	// . SEC_HIDDEN applies to text and tags in style=display:none tags.
	int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_HIDDEN|
		SEC_NOSCRIPT;

	//
	//
	// BEGIN STREET NAME IDENTIFICATION
	//
	//

	// fill this array
	//Place streets[MAX_STREETS];
	//Place *streets = m_streets;
	//int32_t qx = 0;

	// the copyright symbol in utf8 (see Entities.cpp for the code)
	char copy[3];
	copy[0] = 0xc2;
	copy[1] = 0xa9;
	copy[2] = 0x00;

	// int16_tcuts
	Words     *ww    = m_words;
	int64_t *wids  = ww->getWordIds();
	char     **wptrs = ww->getWordPtrs();
	int32_t      *wlens = ww->getWordLens();
	nodeid_t   *tids = ww->getTagIds();
	// . if section flag is one of these, ignore the words in it
	// . google seems to index marquee, so i took SEC_MARQUEE out
	// . SEC_HIDDEN applies to text and tags in style=display:none tags.
	//int32_t badFlags = SEC_SCRIPT|SEC_STYLE|SEC_SELECT|SEC_HIDDEN|
	//	SEC_NOSCRIPT;
	// int16_tcut
	wbit_t *bits = NULL;
	if ( m_bits ) bits = m_bits->m_bits;

	// does the word "at" preceed the potential address?
	//bool atPreceeds = false;
	// reset this position
	int32_t alnumPos = -1;
	// "b" of last street added
	int32_t lastb = -1;
	// previous word id
	int64_t savedPrevWid = 0LL;
	// scan the entire document
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// debug
		//if ( wptrs[i][0]=='1' &&
		//     wptrs[i][1]=='3' &&
		//     wptrs[i][2]=='1' ) {
		//	char *xx=NULL;*xx=0; }
		// skip if not an alnum word
		if ( ! wids[i] ) {
			if ( wlens[i] == 1 ) continue;
			if ( wlens[i] >  5 ) continue;
			if ( ! m_words->hasChar(i,'&') ) continue;
		}
		// skip if in a script section
		if ( sp && sp[i] && (sp[i]->m_flags & badFlags) )  continue;
		// stop if streets are maxed
		//if ( m_ns >= MAX_STREETS ) break;
		// record
		int64_t prevWid = savedPrevWid;
		// and update
		savedPrevWid = wids[i];
		// it's an alnum OR has "  &  " (see above)
		if ( wids[i] ) alnumPos++;
		// . if we are not outside the scope of previous street then
		//   keep going!
		// . fixes "1025 1/2 Lomas Blvd" from picking up the substreet
		//   of "2 Lomas Blvd" which was causing an AF_AMBIGUOUS
		if ( i < lastb ) continue;

		// make this the end point
		// quickly add po boxes
		if ( to_lower_a(wptrs[i][0])=='p' ||
		     // sometimes they just have "box 27693" like on
		     // http://www.unm.edu/~willow/homeless/services.html
		     to_lower_a(wptrs[i][0])=='b'  ) {
			// assume none
			int32_t j = -1;
			// the hash
			//int64_t poh = 0LL;
			// "box 123"
			if ( i + 2 < nw &&
			     wids[i  ] == h_box &&
			     is_digit(wptrs[i+2][0]) ) {
				j = i + 2;
			}
			// "p o box 123"
			if ( i + 6 < nw &&
			     wids[i  ] == h_p   && 
			     wids[i+2] == h_o   &&
			     wids[i+4] == h_box &&
			     is_digit(wptrs[i+6][0]) ) {
				j = i + 6;
				//poh = h_po ^ wids[j];
			}
			// "p o box 123"
			if ( i + 6 < nw &&
			     wids[i  ] == h_post   && 
			     wids[i+2] == h_office &&
			     wids[i+4] == h_box &&
			     is_digit(wptrs[i+6][0]) ) {
				j = i + 6;
				//poh = h_po ^ wids[j];
			}
			// p o b 123
			if ( i + 6 < nw &&
			     wids[i  ] == h_p  && 
			     wids[i+2] == h_o &&
			     wids[i+4] == h_b &&
			     is_digit(wptrs[i+6][0]) ) {
				j = i + 6;
				//poh = h_po ^ wids[j];
			}
			// "po box 123"
			if ( i + 4 < nw &&
			     wids[i  ] == h_po  && 
			     wids[i+2] == h_box &&
			     is_digit(wptrs[i+4][0]) ) {
				j = i + 4;
				//poh = h_po ^ wids[j];
			}
			// "p.o. 81255"
			if ( i + 4 < nw &&
			     wids[i  ] == h_p && 
			     wids[i+2] == h_o &&
			     is_digit(wptrs[i+4][0]) ) {
				j = i + 4;
				//poh = h_po ^ wids[j];
			}
			// skip if no good
			if ( j < 0 ) continue;
			// int16_tcuts
			int32_t a = i;
			int32_t b = j+1;
			// add the street
			Place *street = (Place *)m_sm.getMem(sizeof(Place));
			if ( ! street ) return false;
			street->m_a       = a;
			street->m_b       = b;
			street->m_alnumA  = alnumPos;
			street->m_alnumB  = alnumPos+(j-i+2)/2;
			street->m_type    = PT_STREET;
			street->m_str     = wptrs[i];
			street->m_strlen  = wptrs[j]+wlens[j]-wptrs[i];
			//street->m_adm1[0] = 0;
			//street->m_adm1[1] = 0;
			street->m_adm1Bits= 0LL;
			//street->m_crid     = 0;
			street->m_flags2  = 0;
			street->m_bits    = 0;
			street->m_address = NULL;
			street->m_alias   = NULL;
			//street->m_hash    = poh;
			street->m_streetNumHash = wids[j];
			street->m_streetIndHash = h_po;
			// prevent overlap with next street
			lastb = street->m_b;
			// . need to know this for getting place name
			// . place name must also be in upper case if po box is
			if ( is_upper_a(wptrs[i][0]) ) 
				street->m_bits |= PLF_HAS_UPPER;
			// and note that it is a po box so Events.cpp can
			// exclude it as an event location
			street->m_flags2 |= PLF2_IS_POBOX;
			// set its m_hash member
			setHashes ( street , m_words , m_niceness );
			// set some bits
			for ( int32_t k = a ; bits && k < b ; k++ )
				bits[k] |= D_IS_IN_STREET;
			// advance
			//m_ns++;
			// stop if overflowing
			//if ( m_ns >= MAX_STREETS ) break;
			// advance, no! this fux up alnumPos... use lastb
			//i = j;
			// to next
			//continue;
		}

		//
		// we might be a street intersection!
		//
		bool hasAmp = m_words->hasChar(i,'&') ;
		if ( wids[i] == h_and || hasAmp ) {
		//if ( m_words->hasChar(i,'&') ) {
			// save it
			int32_t old = m_sm.getNumPtrs();
			// use this
			int32_t alnumPosArg = alnumPos;
			// modify alnumPos if we are amp so it doesn't double
			// count the word before the ampersand!
			if ( hasAmp ) alnumPosArg++;
			//m_ns = m_ns;
			if ( ! addIntersection(i,alnumPosArg) )
				return false;
			/*
			// show it
			int32_t a = i - 8;
			int32_t b = i + 8;
			if ( a < 0 ) a = 0;
			if ( m_ns != old ) {
				a = m_streets[m_ns-1].m_a;
				b = m_streets[m_ns-1].m_b;
			}
			char *str = m_wptrs[a];
			int32_t ss = m_words->getStringSize ( a , b );
			SafeBuf pp;
			char c = str[ss];
			str[ss] = 0;
			char *gs = "bad";
			if ( m_ns != old ) gs = "GOOD";
			log("intersect: %s \"%s\"", gs,str);
			str[ss] = c;
			*/
			//m_ns = m_ns;
			int32_t ns = m_sm.getNumPtrs();
			// if no intersection added, keep on going
			if ( ns == old ) continue;
			// keep going if not a street before it either
			if ( ns <= 1 ) continue;
			// get it and street before it
			Place *s1 = (Place *)m_sm.getPtr(ns-1);
			Place *s2 = (Place *)m_sm.getPtr(ns-2);
			// get prev two streets
			if ( s2->m_a > s1->m_a ) {
				// i saw this for 
				// "Corner of 1551 State Route 232 and
				//  State Route 52". the street at m_ns-2
				// was "1551 State Route 232" and the
				// intersection street started at the word
				// "Corner", so its m_a was less than...
				// so in this case, let's simply disregard
				// this intersection and not core.
				// CAUTION. some m_bits are still set to
				// D_IS_IN_STREET though...
				// url was www.visitclermontohio.com/events.htm
				//m_ns = old;
				m_sm.setNumPtrs ( old );
				//char *xx=NULL;*xx=0; }
				continue;
			}
			// do not overlap streets!
			//i = streets[m_ns-1].m_b - 1;
			lastb = s1->m_b;
		}


		// we must now start with a number since we are just doing
		// addresses in the usa, BUT i am now allowing "PO Box 1234"
		// to be a valid street address
		if ( ! is_digit(wptrs[i][0]) && wids[i] != h_one ) continue;
		// if we are h_one we must be capitalized!
		if ( wids[i] == h_one && wptrs[i][0] != 'O' ) continue;
		// must not be in a date!
		if ( bits &&
		     (bits[i] & D_IS_IN_DATE) &&
		     // noon street?
		     wids[i] != h_daily    &&
		     wids[i] != h_noon     &&		     
		     wids[i] != h_midnight ) 
			continue;
		// a '#' sign can not preceed us
		// "KELLY S #7 JUAN TABO 1418 JUAN TABO NE, ..."
		// . no! messes up "#3515 Berkeley Place NE"
		//if ( i-1 >= 0 && wptrs[i  ][-1]=='#' ) continue;
		//if ( i-1 >= 0 && wptrs[i-1][ 0]=='#' ) continue;
		// do not split hyphens
		if ( i-2 >= 0 &&wptrs[i-1][0]=='-'&&wlens[i-1]==1&&wids[i-2])
			continue;
		// do not split periods like '1."5 miles west"'
		if ( i-1 >= 0 && wptrs[i-1][0]=='.'&&wlens[i-1]==1 )
			continue;
		// fix "top X", that is not a street name!
		if ( i-2 >= 0 && wids[i-2] == h_top )
			continue;
		// fix "route 66 casino" (highway 32 hotdogs) etc.
		if ( i-2 >= 0 && wids[i-2] == h_route )
			continue;
		if ( i-2 >= 0 && wids[i-2] == h_rte )
			continue;
		// . fix 'highway "14 on the sandia crest road"'
		// . yeah, the "14" is not a street address
		if ( i-2 >= 0 && wids[i-2]==h_highway )
			continue;
		// fix 'hwy "14 on the sandia crest road"'
		if ( i-2 >= 0 && wids[i-2]==h_hwy )
			continue;
		// fix 'hwy "14 on the sandia crest road"'
		if ( i-2 >= 0 && wids[i-2]==h_hiway )
			continue;
		// fix "8600 West Bryn Mawr Avenue, Suite 920-N, Chicago, IL"
		if ( prevWid == h_suite )
			continue;
		// and "county road" i guess
		if ( i-2 >= 0 && wids[i-2]==h_cr )
			continue;
		// and "state road/route 14" too i guess
		if ( i-4 >= 0 && 
		     (wids[i-4]==h_state ||
		      wids[i-4]==h_cnty  ||
		      wids[i-4]==h_cty   ||
		      wids[i-4]==h_county ) && 
		     (wids[i-2]==h_road  ||
		      wids[i-2]==h_rd    ||
		      wids[i-2]==h_rt    ||
		      wids[i-2]==h_rte   ||
		      wids[i-2]==h_route   ) )
			continue;
		// . skip if an an "open" section
		// . cored on http://www.abqtango.org/current.html
		// . 'continue' was causing us to miss 4915 hawkins street
		//   for that url, so i commented out
		//if ( sp[i]->m_wordEnd == -1 ) {
		//	char *xx=NULL;*xx=0;
		//	continue;
		//}
		// sanity check. make sure its the right section
		//if ( i >= sp[i]->m_wordEnd   ) {char*xx=NULL;*xx=0;}
		// sanity check
		if ( sp && i <  sp[i]->m_a ) {char*xx=NULL;*xx=0;}
		// are we a stop word?
		//bool isStop = wlens[i] <=1 || ww->isQueryStopWord(i);
		// are we cap?
		//bool isCap = ww->isCapitalized(i);
		// do not start with uncapitalized stop word
		//if ( isStop && ! isCap ) continue;
		// never start with "At"
		//if ( wids[i] == h_at ) { atPreceeds = true; continue; }
		// count the number of numbers
		int32_t nums = 0;
		// are we delimeted on the left end?
		//bool leftEnd  = false;
		// keep an accumulative hash of all the wids in the phrase
		bool firstWasDir    =  false; // 1st word is a direction?
		bool hadCornerDir   =  false;
		char uc             = -1; // are we capitalized?
		int32_t alphaCount     =  0;
		int32_t indCountStreet =  0;
		int32_t indCountDir    =  0;
		int32_t stopCount      =  0;
		int32_t numCount       =  0;
		bool firstWordIsNum =  false;
		bool lastWasNum     = false;
		bool lastWasDir     = false;
		int32_t commaCount     =  0;
		int32_t alnumsInPhrase =  0;
		int64_t lastIndStreetHash = 0LL;
		// hash of the non indicator alpha words in street name
		//int64_t h1             =  0;
		// . includes hash of directional indicators
		// . we only use this if street name is a directional indicator
		//int64_t h2             =  0;
		//int64_t h2b            =  0;
		//int64_t h3             =  0;
		//int64_t h4             =  0;
		// word id of previous word
		//int64_t pi = 0LL;
		// punct right before us is a left bookend
		//if ( i-1 >= 0 && wlens[i-1] >= 2 ) leftEnd = true;
		//if ( i-1 >= 0 && wptrs[i-1][0] != ' ' && 
		//     getUtf8CharSize(wptrs[i-1])==1) leftEnd = true;
		// if we are a number that is good too
		//if ( is_digit(wptrs[i][0]) ) leftEnd = true;
		// or a number is before us
		//if ( i-1 >= 0 && is_digit(wptrs[i-1][0]) ) leftEnd = true;
		// or tag is before us, no alnumword in between us and the tag
		//if ( i-1 >= 0 && tids[i-1] ) leftEnd = true;
		//if ( i-2 >= 0 && tids[i-2] ) leftEnd = true;
		// if we are cap'd and word before us is not let that be a 
		// delimeter as well
		//if (i-2>= 0 && isCap && wids[i-2] &&!ww->isCapitalized(i-2)) 
		//	leftEnd = true;
		// need a delimeter on the left
		//if ( ! leftEnd ) { atPreceeds = false; continue; }
		// save it
		int32_t ns_stack = m_sm.getNumPtrs();//m_ns;
		// a flag for "1025 1/2 Lomas Blvd NE..."
		int32_t fractionj = -1;
		// "620-624 Central Ave SW." (El Rey) ?
		bool hasRange = false;
		// fix for "4909-15 Hawkins NE" for ceder.net
		bool hasHyphenAddress = false;
		// reset this
		int32_t lastSpecialj = -1;
		// loop over it
		for ( int32_t j = i ; j < nw ; j++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// we can never contain a tag
			if ( tids[j] ) {
				// skip if <sup>
				if ( tids[j] == TAG_SUP ) continue;
				if ( tids[j] == (TAG_SUP|BACKBIT) ) continue;

				// fix "1024 4th st sw <span>edit</span>" for
				// mapquest.com url, but carefule, i think
				// a trumba url or something uses spans
				// within its addresses
				if ( (indCountDir || indCountStreet) &&
				     tids[j] == TAG_SPAN )
					break;

				// skip if non-breaking tag
				if ( ! isBreakingTagId(tids[j]) ) 
					continue;
				// . allow br tags since microsofot front page
				// . no! this is causing the zip code from
				//   a previous address to be used as the
				//   street address for the name of a business
				//   for www.enewsbuilder.net
				// . well then at least allow it for
				//   "14 s.<br>2nd street"???? dunno...
				//if ( tids[j] == TAG_BR ) 
				//	continue;
				// allow xml tags
				// . NO! this may help gwair.org because they
				//   have stupid xml tags in between addresses
				//   but it hurts trumba.com:
				//   "86454011</guid>\r\n\t\t\t
				//    <xCal:summary>9th Annual Thanksgiving..."
				//   because most people do not do this!
				//if ( tids[j] == TAG_XMLTAG ) 
				//	continue;
				//if ( tids[j] == (TAG_XMLTAG|BACKBIT) ) 
				//	continue;
				// otherwise, stop it
				break;
			}
			// are we punctuation?
			if ( ! wids[j] ) {
				// single space is ok
				if (wptrs[j][0]==' '&&wlens[j]==1) continue;
				// double space is ok
				if (wptrs[j][0]==' '&&wptrs[j][1]==' '&&
				    wlens[j]==2) continue;
				// period only after abbreviation
				if ( wptrs[j][0] == '.' && j > 0 && 
				     isAbbr(wids[j-1])&&
				     // watch out for "4477 9TH AVE.    SE"
				     // from boe.sandovalcountynm.gov
				     m_words->isSpaces2(j,1) )
					//wptrs[j][1] == ' ' && wlens[j]==2 )
					continue;
				// . period after a single letter as well
				// . N. M.
				if ( wptrs[j][0] == '.' && j > 0 && 
				     wlens[j-1]==1 && 
				     // fix "8. wall street"
				     !is_digit(wptrs[j-1][0]) &&
				     wptrs[j][1] == ' ' && 
				     wlens[j]==2 ) continue;
				// N.M.
				if ( wptrs[j][0] == '.' && j > 0 && 
				     // fix 1."5 miles west"
				     !is_digit(wptrs[j-1][0]) &&
				     wlens[j-1]==1 && wlens[j]==1 ) continue;
				// quote: The Noyes House 2525 "N" Avenue 
				// National
				if (wptrs[j][0]=='\"'&&wptrs[j][1]==' ' &&
				    wlens[j]==2&&
				    // 'closer to 37"' is not a street name!
				    !is_digit(wptrs[j-1][0]))
					continue;
				if (wptrs[j][0]==' ' &&wptrs[j][1]=='\"'&&
				    wlens[j]==2) continue;
				// punct mark: st. michael's drive
				if (wptrs[j][0]=='\''&&wlens[j]==1) continue;
				// mosby's run: utf8 apostrophe
				if (wlens[j]==3&&
				    wptrs[j][0]==-30 &&
				    wptrs[j][1]==-128 &&
				    wptrs[j][2]==-103 )
					continue;
				// village of los ranchos growers' market
				if (wptrs[j][0]=='\''&&wptrs[j][1]==' '&&
				    wlens[j]==2) continue;
				// hyphens usually bad, but x-y is ok.
				if(wptrs[j][0]=='-'&&wlens[j]==1&&j>0&&j+1<nw&&
				   ww->isAlpha(j-1)&&ww->isAlpha(j+1))continue;
				// fix "3650-A Hwy 528..."
				if(wptrs[j][0]=='-'&&wlens[j]==1&&j==i+1&&
				   j+1<nw&&wlens[j+1]==1&&
				   is_alpha_a(wptrs[j+1][0])) continue;
				// "620-624 Central Ave SW." (El Rey) 
				if ( hasRange &&j==i+1 ) continue;
				// fix for 4909-15 Hawkins NE" for ceder.net
				if(j+1<nw&&
				   wlens[j+1]==2&&is_digit(wptrs[j+1][0])&&
				   wlens[j-1]>=4&&is_digit(wptrs[j-1][0]) ) {
					hasHyphenAddress = true;
					continue;
				}
				// sequence of whitespace is ok
				int32_t k;	for(k=0;k<wlens[j];k++)
					if(!is_wspace_a(wptrs[j][k])) break;
				if(k==wlens[j]) continue;
				// '/' is ok if part of a fraction!
				if( j == fractionj ) continue;
				// . allow commas in foreign street addresses
				// . brazil street address: 
				//   "Rua Afonso Canargo, 805"
				//if ( wptrs[j][0]==',' && wptrs[j][1]==' ' &&
				//     is_digit(wptrs[j][2]) && 
				//     j>0 && !is_digit(wptrs[j][-1]) ) {
				//	commaCount++;
				//	continue;
				//}
				//if ( wptrs[j][0]==' ' && wptrs[j][1]==',' &&
				//     is_digit(wptrs[j][2]) &&
				//     j>0 && !is_digit(wptrs[j][-1]) ) {
				//	commaCount++;
				//	continue;
				//}
				// . comma allowed only b4 directional indicatr
				// . "131 Monroe  St, NE"
				// . no because we got a false positive:
				//   "1024 4th street, sw corner..."
				// . ok, this is back again now! BUT... need
				//   to make sure a tag or city name follows it
				// . crap, now we got 
				//   "5305 Gibson, S.E. <b>Albuquerque ..."
				// . shoot, also need to watch out for
				//   "Wisconsin Ave., NW"
				if ( j+3 >= nw ) break;
				bool commaAfter = false;
				if ( wptrs[j][0]==',' ) 
					commaAfter = true;
				if ( wptrs[j][0]=='.' && wptrs[j][1]==',') 
					commaAfter = true;
				if ( wptrs[j][0]==' ' && wptrs[j][1]==',') 
					commaAfter = true;
				if ( ! commaAfter ) break;
				char gotDir = 0;
				if ( wids[j+1] == h_ne ) gotDir = 2;
				if ( wids[j+1] == h_nw ) gotDir = 2;
				if ( wids[j+1] == h_se ) gotDir = 2;
				if ( wids[j+1] == h_sw ) gotDir = 2;
				if ( wids[j+1] == h_n&&wids[j+3]==h_e)gotDir=4;
				if ( wids[j+1] == h_n&&wids[j+3]==h_w)gotDir=4;
				if ( wids[j+1] == h_s&&wids[j+3]==h_e)gotDir=4;
				if ( wids[j+1] == h_s&&wids[j+3]==h_w)gotDir=4;
				if ( ! gotDir ) break;
				// do not breach
				if ( j+gotDir >= nw ) continue;
				// its great if tag follows the dir indicator
				if ( tids[j+gotDir] ) continue;
				// do not breach
				if ( j+gotDir+1 >= nw ) continue;
				// or a punct then a tag
				if ( tids[j+gotDir+1] ) continue;
				// fix for "700 Louisiana, SE 87108" for
				// unm.edu url
				if(is_digit(m_wptrs[j+gotDir+1][0]))continue;
				// ok, a cap word must follow
				if ( ! is_upper_utf8 (wptrs[j+gotDir+1]))break;
				// we are good
				continue;
				// otherwise, stop, we hit bad punct that
				// can not be included in a street address
				//break;
			}
			// . otherwise we are alphanumeric
			// . more than 10 is too many for a street
			if ( alnumsInPhrase++ >= 10 ) break;
			// one common is enough for a street address
			if ( commaCount >= 2 ) break;
			// . forbidden words
			// . fixes "less than ; 1 mile away ; abq nm"
			if ( wids[j] == h_away    ) break;
			// showing "39 results near" Albuquerque, NM
			if ( wids[j] == h_results ) break;
			// "3 Ave, E 144 To E 145 Sts"
			if ( j==i+2 && wids[j] == h_to ) break;
			// "11 Ave" implies "11th avenue"
			if ( j==i+2 && wids[j] == h_ave ) break;
			if ( j==i+2 && wids[j] == h_avenue ) break;
			// "24 st to crescent st"
			// www.nycgovparks.org/facilities/playgrounds has
			// a ton of street formations describing park 
			// boundaries. so fix those:
			if ( j==i+2 && 
			     j+2<nw && 
			     (wids[j] == h_st      ||
			      wids[j] == h_sts     ||
			      wids[j] == h_street  ||
			      wids[j] == h_streets ||
			      wids[j] == h_ave     ||
			      wids[j] == h_avenue  ||
			      wids[j] == h_road    ||
			      wids[j] == h_rd       ) &&
			     (wids[j+2] == h_bet ||
			      wids[j+2] == h_between ||
			      wids[j+2] == h_btwn ||
			      wids[j+2] == h_to ||
			      wids[j+2] == h_at ) )
				break;
			// "90 And E"
			if ( j==i+2 && wids[j] == h_and ) break;
			// 124 st btwn 5 ave"
			if ( wids[j] == h_btwn ) break;
			// are we a stop word?
			//bool isStopWord=wlens[j]<=1 ||ww->isQueryStopWord(j);
			bool isStopWord=wlens[j]<=1 ||s_lc.isInTable(&wids[j]);
			// treat this as a stop word, fixes 
			// "2001 E 7<sup>th</sup>"
			if ( lastWasNum ) {
				if ( wids[j] == h_th ) isStopWord = true;
				if ( wids[j] == h_st ) isStopWord = true;
				if ( wids[j] == h_nd ) isStopWord = true;
				if ( wids[j] == h_rd ) isStopWord = true;
			}
			// are we upper or not?
			bool upper = is_upper_utf8(wptrs[j]);
			// do we have an upper or lower case word?
			if ( uc == -1 && ! is_digit(wptrs[j][0]) ) {
				if ( upper ) uc = 1;
				else if ( ! isStopWord ) uc = 0;
			}
			// mixed case? if so stop!
			if ( ! isStopWord &&
			     ! is_digit(wptrs[j][0])&&
			     upper != uc ) {
				// . fix "123 Wyoming ave."
				// . fix "123 Wyoming ne"
				IndDesc *id;
				id=(IndDesc *)g_indicators.getValue(&wids[j]);
				// set some flags based on indFlags
				if ( ! id ) break;
				// must be "avenue" or "ne" etc.
				if ( ! (id->m_bit & IND_STREET) &&
				     ! (id->m_bit & IND_DIR) )
					break;
			}
			// if lower case stop word of two letters or more
			// leads then do not allow that
			// "1950 in New York, NY"
			if ( isStopWord && wlens[j]>=2 && !upper && j==i+2 )
				break;
			// "7 days a week"
			if ( wids[j]==h_days && j==i+2 )
				break;
			// "2 blocks north"
			if ( wids[j]==h_blocks && j==i+2 )
				break;
			// "1 block north"
			if ( wids[j]==h_block && j==i+2 )
				break;
			// "90 miles north"
			if ( wids[j]==h_miles && j==i+2 )
				break;
			// "1 hour ago"
			if ( wids[j]==h_hour && j==i+2 )
				break;
			if ( wids[j]==h_hr && j==i+2 )
				break;
			// "8 hours ..."
			if ( wids[j]==h_hours && j==i+2 )
				break;
			if ( wids[j]==h_hrs && j==i+2 )
				break;
			// "2 mi north"
			if ( wids[j]==h_mi && j==i+2 )
				break;
			// "cross 8 mile road"
			if ( wids[j]==h_mile && j==i+2 )
				break;
			// "90 kilometers north"
			if ( wids[j]==h_kilometers && j==i+2 )
				break;
			// "90 km north"
			if ( wids[j]==h_km && j==i+2 )
				break;
			// "5 reviews"
			if ( wids[j]==h_reviews && j==i+2)
				break;
			// 18 year(s) old
			if ( (wids[j] == h_year ||
			      wids[j] == h_years ||
			      wids[j] == h_yr ||
			      wids[j] == h_yrs ) && j==i+2 )
				break;
			// this is not a street:
			// "[copyright] 2008 The E.W. Scripps Co."
			if ( j==i && i-1>0 && !tids[i-1] && !wids[i-1] &&
			     gb_strncasestr(wptrs[i-1],wlens[i-1],copy) )
				break;
			// this is not a street:
			// "[copyright] 1997 - 2009 Albuquerque Journal"
			if ( j==i && i-4>0 && is_digit(wptrs[i-2][0]) &&
			     gb_strncasestr(wptrs[i-1],wlens[i-1],copy) )
				break;
			
			// assume not
			bool isDir = false;
			bool isStreetInd = false;
			// int16_tcut
			bool isNum = ww->isNum2(j);
			// set "lastWasNum"
			if ( isNum ) lastWasNum = true;
			else         lastWasNum = false; 
			// treat this as a number too!
			if ( wids[j] == h_one ) isNum = true;
			// are we a number? (might also be "13a")
			if ( isNum ) { 
				// . only one number per phrase?
				// . NO! "2860 state highway 14 N.". needs 2!
				if ( ++nums >= 3 ) break;
				// if a $ preceeds, that is bad!
				if ( j-1>=0 && wptrs[j][-1]=='$' ) break;
				// . or break in front
				// . was messing up "Elk Lodge #929\n
				//   1720 N Montana Ave" so i added the tids 
				//   check
				// . i took this out because of
				//   "Albertsons #903 4300 ridge crest..."
				//   for http://www.estrelladelnortevineyard.
				//   com/SFV_retloc.php
				//if(j-2>=0&&ww->isNum(j-2)&&!tids[j-1]&&
				//   !ww->hasChar(j-1,','))
				//	break;
				// . filter "23,000 years ago"
				// . filter "ages 8-16"
				// . filter "ages 8 - 16"
				// . filter "june 3-31"
				// . filter "june 3 - 31"
				// . filter "tuesday 3 - 5"
				// . get first number, make it word #f
				if ( wlens[j]==3 && j-2>=0 && 
				     is_digit(wptrs[j-2][0])&&
				     wlens[j-2]<=3 && 
				     (wptrs[j-1][0]=='-'||wptrs[j-1][0]==','||
				      wptrs[j-1][1]=='-') ) {
					// "620-624 Central Ave SW." (El Rey)
					// if word was not a number before us
					if ( ! hasRange ) break;
					if ( j != i+2 ) break;
				}
				if ( wlens[j]<=3 && j+2<nw && 
				     is_digit(wptrs[j+2][0]) &&
				     wlens[j+2]==3 && 
				     wlens[j+1]==1 &&
				     (wptrs[j+1][0]=='-'||wptrs[j+1][0]==','||
				      wptrs[j+1][1]=='-') ) {
					// "620-624 Central Ave SW." (El Rey)
					// if word was not a number before us
					if ( j != i ) break;
					if ( wptrs[j+1][0]==',') break;
					int32_t a = ww->getAsLong(j);
					int32_t b = ww->getAsLong(j+2);
					if ( a >= b ) break;
					if ( b - a > 10 ) break;
					// i guess it is ok now
					hasRange = true;
				}
				// no years.
				int32_t n = ww->getAsLong(j);
				// possible possessive year?
				if ( n>=1980 && n<=2030 && 
				     j+1<nw && wptrs[j+1][0]=='\'')
					break;
				// year ending in s (1960s)
				if(n>=1980&&n<=2030&&wptrs[j][wlens[j]-1]=='s')
					break;
				// count it
				numCount++;
				// and if we are first
				if ( i == j ) firstWordIsNum = true;
				// use for street num hash
				//if ( nums == 1 ) h3 = wids[j];
			}
			// inc this count if not a number
			else alphaCount++;
			// time indicator?
			//if ( wids[j] == h_am ) break;
			//if ( wids[j] == h_pm ) break;
			//if ( wids[j] == h_a && j+2<nw &&wids[j+2]==h_m)break;
			//if ( wids[j] == h_p && j+2<nw &&wids[j+2]==h_m)break;

			// break if we hit a suite indicator
			if ( wids[j] == h_suite    ) break;
			if ( wids[j] == h_ste      ) break;

			// does a single letter or number follow "room"?
			bool numFollows = false;
			if ( j+2<nw && is_digit(wptrs[j+2][0]))numFollows=true;
			// a single letter counts as a number too!
			if (j+2<nw&&wids[j+2] && wlens[j+2]==1)numFollows=true;
			// or ends in a number (like "A1")
			if ( j+3<nw &&is_digit(wptrs[j+3][-1]))numFollows=true;

			// these are like suites but need a number or 
			// single letter after them
			if ( ( wids[j] == h_unit     ||
			       wids[j] == h_bldg     ||
			       wids[j] == h_bld      ||
			       wids[j] == h_building ||
			       wids[j] == h_room     ||
			       wids[j] == h_pier     ||
			       wids[j] == h_rm         ) && numFollows )
				break;

			// does this number start a fraction?
			// 1025 1/2 Lomas Boulevard North West, Albuquerque, NM
			if ( isNum && numCount == 2 && j+2<nw &&
			     wlens[j] == 1 && wptrs[j+1][0]=='/' && 
			     wlens[j+1]==1 && ww->isNum(j+2) ) {
				// ignore it kinda
				numCount -= 2;
				nums     -= 2;
				// allow the / to pass
				fractionj = j+1;
			}

			// no back to back numbers allowed in street address
			else if ( isNum && j+3<nw && ww->isNum(j+2) &&
				  // exception for "1025 1/2 Lomas Blvd..."
				  ( wptrs[j+3][0]!='/' || wlens[j+3]!=1) &&
				  // exception for "4909-15 hawkins NE"
				  // for www.ceder.net
				  (j>1&&wptrs[j-1][0]=='-'&&wlens[j-1]==1&&
				   wlens[j]<=2&&wlens[j-2]>=4) &&
				  ! hasRange )
				break;

			// street has 2 or less numbers though!
			if ( numCount >= 3 ) break;

			// . if we are the 2nd number in the street name
			//   we must follow a "highway" or "state route" or
			//   "state road" or such abbreviation... 
			// . if we are "3rd" that should not be considered a 
			//   num so isNum should be false for that,
			//   but we might have 3<sup>rd</sup>
			// . this screws ups "Corrales Office Plaza, 
			//   3611 NM 528 NW, Ste. B, ABQ 87114" and makes us
			//   thinks the road is "528 NW" and "3611 NM" is
			//   part of the place name
			/*
			if ( isNum && numCount == 2 ) {
				// assume not ok!
				bool ok = false;
				// are we ok?
				if ( i-2>=0 && wids[i-2]==h_hwy     ) 
					ok = true;
				if ( i-2>=0 && wids[i-2]==h_highway ) 
					ok = true;
				if ( i-4>=0 && 
				     wids[i-4]==h_state &&
				     wids[i-2]==h_road   ) 
					ok = true;
				if ( i-4>=0 && 
				     wids[i-4]==h_state &&
				     wids[i-2]==h_route  ) 
					ok = true;
				// get next alnum word, should be
				// the "th" in "4 th street" for example
				int32_t nn = i + 2;
				if ( nn<nw &&  tids[nn] ) nn++;
				if ( nn<nw && !wids[nn] ) nn++;
				if ( nn<nw && wids[nn]==h_st )	ok = true;
				if ( nn<nw && wids[nn]==h_nd )	ok = true;
				if ( nn<nw && wids[nn]==h_rd )	ok = true;
				if ( nn<nw && wids[nn]==h_th )	ok = true;
				if ( ! ok ) 
					break;
			}
			*/


			// . fix "4701 wyoming blvd. NE abq nm 87111"
			// . watch out for "501 elizabeth st. S.E."
			// . after dir pretty much stop
			// . "204 bryn mawr drive north east" --> 5 --> 6
			if ( indCountDir>0 && alphaCount >= 6 ) break;
			// containing an indicator qualifies us.
			IndDesc *id=(IndDesc *)g_indicators.getValue(&wids[j]);
			// set some flags based on indFlags
			if ( id && (id->m_bit & IND_STREET) ) {
				// invalidate it if it is "8k run"
				if ( wids[j] == h_run &&
				     j-2>0 &&
				     is_digit(wptrs[j-2][0]) &&
				     to_lower_a(wptrs[j-1][-1])=='k' )
					break;
				// otherwise count it
				indCountStreet++;
				isStreetInd = true;
				// save it
				lastIndStreetHash = wids[j];
				// back up hash
				//h2b <<= 1;
				//h2b ^= wids[j];
			}
			if ( id && (id->m_bit & IND_DIR   ) ) {
				// cancel the 'S' indicator if potential
				// apostrophe! "aug 17 burt's lounge"
				// we do not want "17 burt's"
				if ( wlens[j]==1&& 
				     (wptrs[j][0]=='s' ||
				      wptrs[j][0]=='S' ) && 
				     j>1 && wptrs[j][-1]!=' ' ) 
					id = NULL;
				else {
					// mix it  up
					//h2 <<= 1;
					// include it in this
					//h2 ^= wids[j];
				}
			}
			// assume not
			lastWasDir = false;
			if ( id && (id->m_bit & IND_DIR   ) ) {
				indCountDir++;
				isDir = true;
				if ( alphaCount == 1 ) firstWasDir = true;
				// se? ne? nw? sw?
				if ( wlens[j] == 2 ) hadCornerDir = true;
				// northeast? etc.
				if ( wlens[j] >= 9 ) hadCornerDir = true;
				lastWasDir = true;
			}

			// . fix "1024 4th st sw <span>edit</span>" for
			//   mapquest.com url
			// . this caught "330 Tijeras Ave NW Ofc Albuquerque,"
			// . and "1664 Bridge Boulevard Southwest Rea" but i
			//   don't know what ofc and rea mean??
			// . crap we lost "10000 NW Coors Blvd" which is a
			//   type-o
			//if ( hadCornerDir && ! id && alphaCount >= 2 )
			//	break;

			// stop "KELLY S #7 JUAN TABO 1418 JUAN TABO NE"
			// from giving "7 JUAN TABO 1418 JUAN TABO NE" street
			// basically, do not allow a part of the street name
			// to be after this 2nd number...
			if ( numCount == 2 && 
			     ! isNum && 
			     ! isDir && 
			     ! isStreetInd &&
			     ! hasRange &&
			     ! hasHyphenAddress &&
			     wids[j] != h_st &&
			     wids[j] != h_nd &&
			     wids[j] != h_rd &&
			     wids[j] != h_th )
				break;

			// get synonym of word id
			//int64_t *swid = getSynonymWord ( &wids[j] , &pi );
			// word id of previous word
			//pi = wids[j];
			// this too
			//if ( id ) h4 = *swid;//wids[j];
			// . update this. 
			// . exclude numbers from this!
			// . allow other numbers if no alpha word before them!
			// . exclude directional indicators from this
			// . but allow directional indicators if right after 
			//   the street number though
			//if ( j > i &&
			//     ( ! isDir || j == i + 2 ) &&
			//     // commenting this out hurts "100 3/4 road"
			//     // but it helps "2001 1/2 montgomery blvd"
			//     //( ! isNum || alphaCount == 0 ) &&
			//     ! isNum &&
			//     ! isStreetInd ) {
			//	// mix it up
			//	h1 <<= 1;
			//	// xor it
			//	h1 ^= *swid;//wids[j];
			//}
			// fix "2804 hwy 250" from excluding the "250"
			//if ( isNum && alphaCount > 0 ) {
			//	// mix it up
			//	h1 <<= 1;
			//	// xor it
			//	h1 ^= *swid;//wids[j];
			//}
			// count stop words
			//if ( ! id && ww->isStopWord(j) ) stopCount++;
			if ( ! id && s_lc.isInTable(&wids[j]) ) stopCount++;

			// need at least one number to be a street address
			if ( numCount == 0 ) continue;
			// . first or last word must be num
			// . now i am deciding to limit to america only so
			//   we need the first word to be a number
			//if ( ! firstWordIsNum && ! isNum ) continue;
			if ( ! firstWordIsNum ) continue;
			// need at least one alpha word
			if ( alphaCount <= 0 ) continue;

			// if first was number and we are stop word,
			// no stop word right after the number!
			// "2009 at the arts alliance gallery,1100 san mateo.."
			// what about "488 E. hwy 66" ! E is a stop word!
			//if ( numCount == 1 && stopCount == 1 && 
			//     alnumsInPhrase == 2 ) 
			//	break;
			// can't have just stop words
			if ( alphaCount == stopCount ) continue;
			// or if a single char word, skip!
			if ( j == i && wlens[i] == 1 ) continue;
			// do not split hyphens
			if ( j+2 <nw && wlens[j+1]==1 && wptrs[j+1][0]=='-'&&
			     wids[j+2]&&
			     // if both are digits, it is ok!
			   (!is_digit(wptrs[j][0])||!is_digit(wptrs[j+2][0])) )
				continue;
			// ok, now we are name, street or suite
			bool goodStreet = ( indCountStreet >= 1 );

			// if we are not an indicator but "Paseo de" preceeds
			// us like in "Paseo de Peralta" then consider us to
			// be good!
			bool isPaseoDe = false;
			if ( ! isStreetInd && j-4 > i &&
			     (wids[j-2]==h_de ||
			      // "407 paseo del canon" for guidebookamerica.com
			      wids[j-2]==h_del ) &&
			     wids[j-4]==h_paseo ) {
				isPaseoDe = true;
				goodStreet = true;
			}

			// . can't end on a lower case word if we have upper
			// . "311 Main Street is in" was a street name!!
			if ( uc==1 && ! upper && !is_digit(wptrs[j][0])) 
				goodStreet = false;
			// direction is ok too
			if ( firstWasDir ) goodStreet = true;
			if ( isDir       ) goodStreet = true;
			// if just one alpha word and one indicator,that is bad
			if ( alphaCount == 1 && indCountStreet==1 ) 
				goodStreet = false;
			if ( alphaCount == 1 && indCountDir   ==1 ) 
				goodStreet = false;
			// if we are not good but an indicator follows, wait
			if ( ! goodStreet && j+2<nw ) {
				IndDesc *id=(IndDesc *)
					g_indicators.getValue(&wids[j+2]);
				if ( id && (id->m_bit & IND_STREET) ) continue;
				if ( id && (id->m_bit & IND_DIR   ) ) continue;
				if ( is_digit(wptrs[j+2][0]       ) ) continue;
			}
			// did we have a highway? (or state route)
			bool isHighwayNum = false;
			if ( isNum && j-2>=0 && wids[j-2] == h_highway ) 
				isHighwayNum = true;
			if ( isNum && j-2>=0 && wids[j-2] == h_hwy ) 
				isHighwayNum = true;
			if ( isNum && j-2>=0 && wids[j-2] == h_hiway ) 
				isHighwayNum = true;
			if ( isNum && j-2>=0 && wids[j-2] == h_cr ) 
				isHighwayNum = true;
			if ( isNum && j-4>=0 && 
			     (wids[j-4] == h_state  ||
			      wids[j-4] == h_county ||
			      wids[j-4] == h_cnty   ||
			      wids[j-4] == h_cty    )  &&
			     ( wids[j-2] == h_rd    ||
			       wids[j-2] == h_road  ) )
				isHighwayNum = true;
			// 1501 Route 66 (no state or county before it req'd)
			if ( wids[j-2] == h_route ||
			     wids[j-2] == h_rte   ||
			     wids[j-2] == h_rt    )
				isHighwayNum = true;
			// ok if we are like "1300 state route 12" that is good
			if ( isHighwayNum )
				goodStreet = true;
			// two or more street indicators can signifiy
			// a combo of two streets. crap but we have
			// "750 North St. Francis Drive" !
			// "1300 st. hway 14"
			//if ( indCountStreet >= 2 ) goodStreet = false;
			// we must end on an indicator (or be like hwy 13)
			if ( ! isDir && ! isStreetInd && ! isHighwayNum &&
			     ! isPaseoDe )
				goodStreet = false;

			// . check this only if we need to
			// . fixes "328 galisteo<br>santa fe. NM 87501"
			// . should fix estrellanortevineyard.com's
			//   "T & D Market 485 Parker, Santa Rosa, NM..."
			if ( ! goodStreet && 
			     alphaCount >= 1 && 
			     ! isNum && j+2<nw &&
			     // for for "77kkob am abq nm" (radio station fix)
			     wids[j] != h_am &&
			     wids[j] != h_fm ) {
				int32_t follows = cityAdm1Follows(j+2);
				// good then
				if ( follows ) goodStreet = true;
				// error? this can never happen...
				//if ( follows == -1 ) return false;
				// fix for "6th Ave. New York, NY" which
				// thinks that the city is "York!" for
				// local.botw.org
				if ( follows ) {
					int32_t f2 = cityAdm1Follows(j);
					// this can never happen... comment out
					//if ( f2 == -1 ) return false;
					if ( f2 ) goodStreet = false;
				}
			}
			// if suite follows that is good too:
			// "One Hallidie Plaza, Suite 404,..."
			// from http://pipl.com/contact/
			if ( ! goodStreet && alphaCount >= 1 && 
			     ! isNum && j+2<nw && 
			     ( wids[j+2]==h_suite    || 
			       wids[j+2]==h_ste      ) ) {
				// set it good
				goodStreet = true;
			}
			// does a single letter or number follow "room"?
			bool numFollows2 = false;
			if( j+4<nw && is_digit(wptrs[j+4][0]))numFollows2=true;
			// a single letter counts as a number too!
			if(j+4<nw&&wids[j+4] && wlens[j+4]==1)numFollows2=true;
			// or ends in a number (like "A1")
			if( j+5<nw &&is_digit(wptrs[j+5][-1]))numFollows2=true;
			// room <num> is likewise a good stopping point
			if ( ! goodStreet && 
			     alphaCount >= 1 && 
			     ! isNum && 
			     numFollows2 &&
			     ( wids[j+2]==h_building ||
			       wids[j+2]==h_bldg     ||
			       wids[j+2]==h_bld      ||
			       wids[j+2]==h_unit     ||
			       wids[j+2] == h_pier   ||
			       wids[j+2] == h_room   ||
			       wids[j+2] == h_rm       ) )
				goodStreet = true;
			// if we end on "hwy" and a number follows, incl #
			if ( (wids[j] == h_hwy     ||  
			      wids[j] == h_highway ||
			      wids[j] == h_hiway   ||
			      wids[j] == h_cr        ) &&
			     j + 2 < nw && ww->isNum(j+2) && wlens[j+1]<=3 &&
			     ! tids[j+1] &&
			     // fix "86 Old Las Vegas Hwy., 983-2700."
			     ! ww->hasChar(i,',') &&
			     (j+3>=nw||wptrs[j+3][0]!='-') )
				goodStreet = false;
			// same goes for state routes/roads
			if ( (wids[j] == h_route || 
			      wids[j] == h_road  ||
			      wids[j] == h_rd    ||
			      wids[j] == h_rte   ||
			      wids[j] == h_route  ) &&
			     j - 2 >= 0 && 
			     ( wids[j-2] == h_state || 
			       wids[j-2] == h_cty   ||
			       wids[j-2] == h_cnty  ||
			       wids[j-2] == h_county )&&
			     j + 2 < nw && ww->isNum(j+2) && wlens[j+1]<=3 &&
			     ! tids[j+1] &&
			     // anticipate similar problem to
			     // "86 Old Las Vegas Hwy., 983-2700."
			     ! ww->hasChar(i,',') &&
			     (j+3>=nw||wptrs[j+3][0]!='-') )
				goodStreet = false;
			

			// must not end on a lower case stop word of 2+ letters
			if ( wids[j] == h_and || wids[j] == h_or || 
			     // fixes "2006 census for ... abq nm"
			     wids[j] == h_for )
				goodStreet = false;

			// fix 'b "9 st n" of boardwalk'
			if ( numCount == 1 &&
			     indCountDir == 1 &&
			     indCountStreet == 1 &&
			     // fix "357 Court NE" for 
			     // http://www.anneryan.com/book/order.htm
			     lastIndStreetHash != h_court &&
			     lastWasDir &&
			     alphaCount == (indCountDir + indCountStreet) )
				goodStreet = false;

			// add as a street?
			if ( ! goodStreet ) continue;
			// only add one street per i
			// UNLESS lasti ended right before a city or state
			// in which case we should add both
			if ( lastSpecialj == -1 ) 
				//m_ns = ns_stack;
				m_sm.setNumPtrs(ns_stack);

			// record if a city/state follows us so if we end
			// up absorbing that city/state to make a bigger
			// street name then we create 2+ streets and do not
			// erase the previous one
			if ( goodStreet &&
			     j+4<nw && 
			     // "9501 Indian School NE" for
			     // www.cabq.gov/communitycenters/centers.html
			     // was thinking about "School, Nebraska" so
			     // let's fix that with this h_ne constraint
			     m_wids[j+4] != h_ne && // nebraska = NorthEast
			     cityAdm1Follows(j+2) && 
			     lastSpecialj < 0 )
				lastSpecialj = j;

			// . erase previous entry if same starting point
			// . like "501 Copper Ave" vs "501 Copper Ave. NW"
			//if ( ns > 0 && i == streets[ns-1].m_a ) ns--;
			// length of current street (place)
			//int32_t plen = (wptrs[j] + wlens[j]) - wptrs[i];
			// int16_t cut
			int32_t a = i;
			int32_t b = j+1;

			// fix "corrales bosque gallery 
			//      4685 Corrales Rd. *in* Corrales NM"
			if ( m_wids[b-1] == h_in && alphaCount >= 2 ) {
				b -= 2;
				alnumsInPhrase -= 1;
			}

			// length of current street (place)
			int32_t plen = (wptrs[b-1] + wlens[b-1]) - wptrs[a];

			// add the street
			Place *street = (Place *)m_sm.getMem(sizeof(Place));
			if ( ! street ) return false;
			street->m_a       = a;
			street->m_b       = b;
			street->m_alnumA  = alnumPos;
			street->m_alnumB  = alnumPos + alnumsInPhrase;
			street->m_type    = PT_STREET;
			street->m_str     = wptrs[i];
			street->m_strlen  = plen;
			//street->m_adm1[0] = 0;
			//street->m_adm1[1] = 0;
			street->m_adm1Bits= 0LL;
			//street->m_crid     = 0;
			street->m_flags2  = 0;
			street->m_bits    = 0;
			street->m_address = NULL;
			street->m_alias   = NULL;
			// only use the purer hash if it is non-zero
			//if      ( h1 ) street->m_hash = h1;
			//else if ( h2 ) street->m_hash = h2;
			//else           street->m_hash = h2b;
			//street->m_streetNumHash = h3;
			//street->m_streetIndHash = h4;
			// set its m_hash member
			setHashes ( street , m_words , m_niceness );
			// prevent overlap with next street
			lastb = street->m_b;
			// . need to know this for getting place name
			// . place name must also be in upper case if 
			//   the street is...
			if ( uc == 1 ) street->m_bits |= PLF_HAS_UPPER;
			// . set some bits
			// . only do this if we are the unambiguous part, 
			//   otherwise we miss "Sandia Park" in
			//   "1 WILDFLOWER LANE SANDIA PARK NM" because
			//   the 2nd street has "SANDIA PARK" as part of it
			//   and is doesn't get considered as a city to add
			//   to m_places[] below because this bit was getting
			//   set -- i.e. we don't take cities from street names
			if ( lastSpecialj==-1 || lastSpecialj==j ) {
				for ( int32_t k = a ; bits && k < b ; k++ )
					bits[k] |= D_IS_IN_STREET;
			}

			// this is a hack
			if ( lastSpecialj >= 0 && lastSpecialj != j ) {
				int32_t    ns = m_sm.getNumPtrs();
				Place  *ps = (Place *)m_sm.getPtr(ns-2);
				ps    ->m_flags2 |= PLF2_COLLISION;
				street->m_flags2 |= PLF2_COLLISION;
			}

			// had an indicator? ave rd or direction
			//if ( indCountDir ||  indCountStreet )
			//	street->m_flags2 |= PLF2_HAD_INDICATOR;
			// point to next street
			//m_ns++;
			// stop if overflowing
			//if ( m_ns >= MAX_STREETS ) break;
		}
		// nuke this
		//atPreceeds = false;
	// end i loop - go to next potential start of a phrase
	}

	//
	//
	// END STREET LIST GENERATION
	//
	//





	//
	//
	// SET the m_places[] array (m_np) of cities, states and zips
	//
	// we now allow any street address to use any city/state mentioned
	// anywhere in the document.
	//

	// for setting Place
	alnumPos = -1;
	int32_t ignoreUntil = -1;
	int32_t lastCityAlnumB = -1;
	int64_t prevWid = 0LL;
	bool inCityIndicator = false;
	bool inStateIndicator = false;

	// scan the entire document
	for ( int32_t i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// a tag?
		if ( m_tids[i] ) {
			// assume not an indicator tag
			inCityIndicator = false;
			inStateIndicator = false;
			// mus tbe xml
			if ( m_tids[i] != TAG_XMLTAG ) continue;
			// it can inidcate things
			char *tagName = m_wptrs[i]+1;
			if ( strncasecmp(tagName,"eventCity",9) == 0 )
				inCityIndicator = true;
			if ( strncasecmp(tagName,"eventState",10) == 0 )
				inStateIndicator = true;
			continue;
		}
		// skip if not alnum
		if ( ! m_wids[i] ) continue;
		// skip if in a script section
		if ( sp && (sp[i]->m_flags & badFlags) )  continue;
		// count alnums
		alnumPos++;
		// skip if in a street. avoid getting "NE" for nebraska when
		// it is in a street like "1234 girard NE" or something. same
		// goes for streets named after cities or states. and using
		// zip codes that are street numbers
		// . assume if in street not capitalized, fixes
		//   "123 Main Street Abq" so Abq is not in a phrase too
		if ( bits && (bits[i] & D_IS_IN_STREET) ) continue;
		// skip if in menu
		//if ( sp[i]->m_flags & SEC_MENU ) continue;

		if ( i < ignoreUntil ) continue;

		// get it
		int64_t lastWid = prevWid;
		// update it
		prevWid = m_wids[i];

		// must be a zip
		if ( is_digit(m_wptrs[i][0]) ) {
			// int16_tcut
			// this crashed for h=70799779105646092LL
			// word="60527"
			int64_t h = m_wids[i];
			// 5 digits
			if ( m_wlens[i] != 5 ) continue;
			// check for zip code
			int32_t slot = g_zips.getSlot(&h);
			// skip if not
			if ( slot < 0 ) continue;
			// make sure only one! US-only for now...
			// unfortunately we do have zips that have multiple
			// city names... so we can't have this here...
			// later we should add code to pick the best one...
			//if(g_zips.getNextSlot(slot,&h)>=0){char*xx=NULL;*xx=0
			// get the place
			ZipDesc *zd =(ZipDesc *)g_zips.getValueFromSlot(slot);
			// sanity check
			//if ( m_np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }
			// ok, add this entry
			Place *p = (Place *)m_pm.getMem(sizeof(Place));
			if ( ! p ) return false;
			// set it
			p->m_adm1Bits = zd->m_adm1Bits;
			p->m_adm1[0]  = zd->m_adm1[0];
			p->m_adm1[1]  = zd->m_adm1[1];
			p->m_type     = PT_ZIP;
			p->m_a        = i;
			p->m_b        = i+1;
			p->m_alnumA   = alnumPos;
			p->m_alnumB   = alnumPos+1;
			p->m_str      = m_wptrs[i];
			p->m_strlen   = m_wlens[i];
			p->m_hash     = h;
			p->m_cityHash = zd->m_cityHash;
			p->m_cityStr  = g_cityBuf + zd->m_cityOffset;
			p->m_bits     = 0;
			// set PLF_FROMTITLE bit
			if ( sp ) {
				Section *ss = sp[p->m_a];
				if ( ss->m_flags & SEC_IN_TITLE ) 
					p->m_bits |= PLF_FROMTITLE;
			}
			continue;
		}

		// . fix <eventCity>abq</eventCity> for pageaddevent
		// . update this now that we set lastWidCapitalized
		if ( ! is_upper_utf8(m_wptrs[i]) && 
		     ! inCityIndicator &&
		     ! inStateIndicator )
				 continue;

		// . deal with "Kansas City"
		// . deal with "New Mexico" where "New" is also a city!
		// . does this word start a city?
		Place *pc = getCityPlace ( i , alnumPos , m_words );
		// or start a state?
		Place *ps = getStatePlace ( i , alnumPos , m_words );

		// . ignore two letter state codes that are not both capped
		// . fixes "In" "De Paul" "Co" "La"
		if ( ps && 
		     ps->m_strlen==2 && 
		     // unless like <eventState>nm</eventState>
		     ! inStateIndicator &&
		     !is_upper_a(m_wptrs[ps->m_a][1]) &&
		     // . unless we follow a city!
		     // . fixes "New Orleans;La;70113" for 
		     //   http://texasdrums.drums.org/new_orleansdrums.htm
		     lastCityAlnumB != alnumPos )
			ps = NULL;

		// if neither, continue on
		if ( ! pc && ! ps ) continue;

		// set preferred place, "pp"
		Place *pp = NULL;
		if ( ! pp ) pp = pc;
		if ( ! pp ) pp = ps;
		// . if tied prefer longer. if length tied prefer state
		// . "California" is both a state and a city
		if ( pc && ps ) {
			// kill state if city longer
			if ( pc->m_alnumB > ps->m_alnumB ) ps = NULL;
			// or kill city is state is longer
			else if ( pc->m_alnumB < ps->m_alnumB ) pc = NULL;
		}

		if ( pc ) 
			lastCityAlnumB = pc->m_alnumB;

		// set this
		if ( pc ) ignoreUntil = pc->m_b;
		if ( ps ) ignoreUntil = ps->m_b;

		// prevent breach
		// leave some room for adding places below...
		//if ( m_np + 200 > MAX_PLACES ) {
		//	log("addr: too many cities/state to store in places "
		//	    "array. truncating.");
		//	break;
		//	//char *xx=NULL;*xx=0;
		//}

		bool inTitle = false;
		// do not do this if called from msg13 and have no sections
		if ( sp && (sp[i]->m_flags & SEC_IN_TITLE) ) inTitle = true;

		if ( pc ) {
			// int16_tcut
			Place *p = (Place *)m_pm.getMem(sizeof(Place));
			if ( ! p ) return false;
			// ok, good to add
			gbmemcpy ( p , pc , sizeof(Place) );
			// set PLF_FROMTITLE bit
			if ( inTitle ) p->m_bits |= PLF_FROMTITLE;
			// if last word was in,set this
			if ( lastWid == h_in ) p->m_flags2 |= PLF2_REQUIRED;
		}

		if ( ps ) {
			// int16_tcut
			Place *p = (Place *)m_pm.getMem(sizeof(Place));
			if ( ! p ) return false;
			// ok, good to add
			gbmemcpy ( p , ps , sizeof(Place) );
			// set PLF_FROMTITLE bit
			if ( inTitle ) p->m_bits |= PLF_FROMTITLE;
			// if last word was in,set this
			if ( lastWid == h_in ) 	p->m_flags2 |= PLF2_REQUIRED;
		}
	}		

	// record end of this
	m_npSaved = m_pm.getNumPtrs(); // m_np;

	//
	// make a list of occupation names for avoid false positive 
	// identifcation of a place because it is after the word "at" but 
	// really it is something like "john, an engineer at HP, ..." referring
	// to where that person works. fixes 
	// www.aliconferences.com/conf/social_media_govt1209/pre.htm which has
	// "jon carpenter, digital strategist at stratacomm"
	//
	// left off on http://www.jobvertise.com/jobs/indexC21.html
	//
	// search for "One who..." in dictionary? "person that ..."
	//
	// AMBIGUITY:
	// "meet the engineer at cisco"
	// - does the at phrase modify "meet" or "engineer" ???
	//
	static char *s_jobs[] = {
		"strategist",
		"accountant",
		// interim rector at St. Margaret's (www.st-margarets.org)
		"rector",
		"director",
		"programmer",
		"lawyer",
		"attorney",
		"engineer",
		"residence", // "artist in residence at the LA county HS"
		"developer",
		"worker",
		"ceo",
		"cto",
		"cmo",
		"cfo",

		// jobvertise.com
		"lead",
		"mechanic",
		"technician",
		"clerk",
		"specialist",
		"manager",
		"distributor",
		"salesman",
		"consultant",
		"developer",
		"therapist",
		"officer",
		"coordinator",
		"administrator",
		"pilot",
		"advisor",
		"counselor",
		"counsellor",
		"hospitalist",
		"chair",
		"chairman",
		"pulmonologist",
		"repesentative",
		"tutor",
		"planner",
		"assistant",
		"scientist",
		"nutritionist",
		"aquarist",
		"biologist",
		"doctor",
		"dentist",
		"farmer",
		"intern",
		"expert",
		"partner",
		"adjuster",
		"bartender",
		"associate",
		"supervisor",
		"executive",
		"typist",
		"nurse",
		"actor",
		"actress",
		"analyst",
		"modeler",
		"actuary",
		"acupuncturist",
		"poster",
		"professor",
		"teacher",
		"student",
		"senior",
		"junior",
		"sophomore",
		"freshman",
		"writer",
		"blogger",
		"reporter",
		"instructor",
		"designer",
		"physician",
		"driver",
		"trucker",
		"diver",
		"carrier",
		"receptionist",
		"hostess",
		"host",
		"waiter",
		"waitress",
		"cook",
		"chef",
		"recruiter",
		"secretary",
		"practitioner",
		"architect",
		"contractor",
		"plumber",
		"electrician",
		"janitor",
		"bricklayer",
		"banker",
		"trainer",
		"buyer",
		"welder",
		"assembler",
		"packer",
		"aesthetician",
		"officer",
		"policeman",
		"fireman",
		"cop",
		"sheriff",
		"deputy",
		"dispatcher",
		"warden",
		"guard",
		"chemist",
		"operator",
		"owner",
		"producer",
		"housekeeper",
		"maid",
		"babysitter",
		"model",
		"agent",
		"controller",
		"inspector",
		"professional",
		"athlete",
		"facilitator",
		"mover",
		"biller",
		"builder",
		"carpenter",
		"anesthesiologist",
		"animator",
		"investigator",
		"detective",
		"cleaner",
		"maker",
		"sewer",
		"installer",
		"mgr",
		"eng",
		"appraiser",
		"telemarketer",
		"interpreter",
		"linguist",
		"attendant",
		"jeweler",
		"cutter",
		"lumberjack",
		"laborer",
		"collector",
		"coach",
		"counsel",
		"pastor",
		"priest",
		"bishop",
		"cardinal",
		"scout",
		"tester",
		"auditor",
		"drafter",
		"submitter",
		"tech",
		"integrator",
		"machinist",
		"monkey", // grease monkey code monkey
		"liason",
		"fabricator",
		"wholesaler",
		"baker",
		"handler",
		"bagger",
		"teller",
		"captain",
		"houseperson",
		"server",
		"porter",
		"barber",
		"stylist",
		"barista",
		"reviewer",
		"critic",
		"barwoman",
		"demonstrator",
		"beautician",
		"ambassador",
		"boss",
		"shopper",
		"entrepreneur",
		"bellperson",
		"bellman",
		"biostatistician",
		"statistician",
		"mathematician",
		"biopsychologist",
		"biotechnician",
		"organizer",
		"leader",
		"foreman",
		"bookeeper",
		"bookkeeper",
		"player",
		"bowler",
		"golfer",
		"customer",
		"visitor",
		"ranger",
		"broker",
		"busser",
		"busboy",
		"dishwasher",
		"washer",
		"sweeper",
		"purchaser",
		"cabinetmaker",
		"decorator",
		"cameraman",
		"registrar",
		"canvaser",
		"canvasser",
		"promoter",
		"announcer",
		"pharmacist",
		"stocker",
		"cardiologist",
		"surgeon",
		"miner",
		"dancer",
		"caregiver",
		"aide",
		"caseworker",
		"cashier",
		"librarian",
		"technologist",
		"anchorman",
		"anchor",
		"employee",
		"manufacturer",
		"assoc",
		"scheduler",
		"botanist",
		"grower",
		"processor",
		"educator",
		"marketer",
		"hygienist",
		"coder",
		"paramedic",
		"anesthetist",
		"midwife",
		"doula",
		"master",
		"moderator",
		"mediator",
		"judge",
		"member",
		"juror",
		"chauffeur",
		"butler",
		"cheesemaker",
		NULL
	};
	static bool s_initJobs = false;
	if ( ! s_initJobs ) {
		// load it up
		if ( ! initWordTable ( &s_jobTable,s_jobs,
				       //sizeof(s_jobs),
				       "jobstbl") )
			return false;
		// do not re-do
		s_initJobs = true;
	}


	//
	//
	// BEGIN FAKE STREET NAME IDENTIFICATION
	//
	// "Tingley Colesium"
	//
	// We treat POTENTIAL place names as street names for all practical 
	// purposes.
	//
	//

	// flag
	char lastWasBreak = 0;
	// reset this since we loop anew
	alnumPos = -1;
	// set if at preceeds the name
	bool atFlag = false;
	int64_t lastWid = 0LL;
	// do not do this if we are javascript
	int32_t ni = nw;
	if ( m_contentType == CT_JS ) ni = 0;
	// do not do this if called from msg13
	if ( ! m_sections ) ni = 0;
	// the first word in a td table cell
	int32_t firstWordInCell;
	// first we identify the candidate place names
	for ( int32_t i = 0 ; i < ni ; i++ ) {
		// skip tags
		if ( tids[i] ) { 
			// input tags reset at tag, like
			// Location: <input ...> for zevents.com
			if ( tids[i] == TAG_INPUT ) atFlag = false;
			// hit a td cell?
			if ( sp[i]->m_tagId == TAG_TD )
				firstWordInCell = sp[i]->m_firstWordPos;
			lastWasBreak = 1; 
			continue; 
		}
		// skip if in script section or whatever to keep alnumPos right
		if ( sp[i]->m_flags & badFlags ) continue;
		// skip if not alnum word
		if ( ! wids[i] ) {
			// if not just spaces, then we are a "break" in which
			// case set "lastWasBreak" to true
			char *p    = wptrs[i];
			char *pend = p + wlens[i];
			for ( ; p < pend ; p++ ) {
				if ( is_wspace_a(*p) ) continue;
				// Dave & Buster's
				if ( *p == '\''      ) continue;
				// Dave & Buster's
				if ( *p == '&'       ) continue;
				// St. John's College
				if ( *p == '.' && is_wspace_a(p[1]) &&
				     i>0 && isAbbr(wids[i-1]) )
					continue;
				lastWasBreak = 1;
				break;
			}
			// skip this now
			continue;
		}
		// it's an alnum
		alnumPos++;
		// remember last i
		bool saved = atFlag;
		// and update to the new one
		atFlag = false;
		// save this
		int64_t savedWid = lastWid;
		// update it now
		lastWid = wids[i];
		// do not start with a date
		if ( bits && (bits[i]&D_IS_IN_DATE)){lastWasBreak=1;continue;}
		// a lower guy followed by an upper guy is a break
		if ( is_lower_utf8 ( wptrs[i] ) &&
		     is_upper_utf8 ( wptrs[i] ) ) {lastWasBreak = 1;continue;}

		// if it is the first word in a td cell and the column header
		// is like "location" or "venue" then mark it as after at
		if ( i == firstWordInCell ) {
			// get column header
			Section *cp = sp[i]->m_headColSection;
			if ( cp && 
			     cp->m_firstWordPos > 0 &&
			     // skip the header itself
			     cp->m_firstWordPos != i &&
			     // must just be one word for now
			     cp->m_firstWordPos == cp->m_lastWordPos &&
			     ( wids[cp->m_firstWordPos] == h_location ||
			       wids[cp->m_firstWordPos] == h_venue ||
			       wids[cp->m_firstWordPos] == h_where ) ) {
				// assume what follows is a place name
				saved = true; // atFlag = true; 
				lastWasBreak = 1; 
				//continue; 
			}
		}

		// this is a break
		if ( wids[i] == h_at ) { 
			// ignore it though if previous word was one of
			// these because it could be driving directions!!
			// this fixes the "4139 prospect" event because we
			// thought it had two locations and it got
			// SEC_MULT_LOCATIONS because we thought "at Menaul"
			// was a place name and not a driving direction
			// for the salsapower.com url
			if ( savedWid == h_left  ||
			     savedWid == h_right || 
			     // appeared at the blah
			     savedWid == h_appeared ||
			     // had a role at the world premier
			     savedWid == h_role ||
			     savedWid == h_studied ||
			     // won a prize at the blah
			     savedWid == h_prize ||
			     savedWid == h_right || 
			     // men who stare at goats
			     savedWid == h_stare   ||
			     savedWid == h_gaze    ||
			     savedWid == h_look    ||
			     savedWid == h_looking ||
			     //savedWid == ||
			     savedWid == h_north || 
			     savedWid == h_south || 
			     savedWid == h_east  || 
			     savedWid == h_west  ) {lastWasBreak=0;continue;}
			// "at sea"
			if ( i+2<nw &&
			     ( wids[i+2] == h_sea ||
			       // "at discounted"
			       wids[i+2] == h_discounted ||
			       // "at www.fridaynight.com"
			       wids[i+2] == h_www ||
			       // $10 at door
			       wids[i+2] == h_door ||			       
			       // "at discount price"
			       wids[i+2] == h_discount ) ) {
				lastWasBreak=0;continue;}
			// skip directional at phrases like
			// "(at Siler Road)" from culturemob.com
			if ( i+4<nw &&
			     ( wids[i+4]==h_road   ||
			       // at the finish [line] (racing)
			       wids[i+4]==h_finish ||
			       // "at the door"
			       wids[i+4]==h_door   ||
			       // "at [a|the] discount[ed]"
			       wids[i+4]==h_discount ||
			       wids[i+4]==h_discounted ||
			       wids[i+4]==h_street ||
			       wids[i+4]==h_avenue ||
			       wids[i+4]==h_ave    ||
			       wids[i+4]==h_st     ||
			       wids[i+4]==h_rd    ) ) {
				lastWasBreak=0;continue;}
			// "at the entrance" but not "at the entrance to"
			if ( i+4<nw &&
			     wids[i+4] == h_entrance &&
			     (i+6>=nw || wids[i+6]!=h_to ) ) {
				lastWasBreak=0;continue;}
			// . at the X area
			// . x = registration (for races)
			if ( i+6<nw &&
			     wids[i+2] == h_the &&
			     wids[i+6] == h_area ) {
				lastWasBreak=0;continue;}
			// "[occuptation] at [company]"
			if ( s_jobTable.isInTable(&savedWid) ) {
				lastWasBreak=0;continue;}
			// otherwise assume what follows is a place name
			atFlag = true; 
			lastWasBreak = 1; 
			continue; 
		}
		// location: or where: indicates a location too!
		if ( ( wids[i]==h_location  ||
		       wids[i]==h_venue  ||
		       wids[i]==h_where ) &&
		     i+1<nw && ww->hasChar(i+1,':') &&
		     // fix "Events at this location:" for
		     // nycday.eventbrite.com
		     (i-2<0 || wids[i-2]!=h_this) ) {
			atFlag = true; 
			lastWasBreak = 1; 
			// skip the colon-containing word
			i++;
			continue; 
		}
		// . "come to" is similar to "at"
		// . fixes http://www.metropolisarts.com/index.php/fuseaction/
		//   show.details/showid/238/metropolis-wine-tasting.html
		if ( i+4<nw && wids[i] == h_come && wids[i+2]== h_to ) {
			atFlag = true; 
			lastWasBreak = 1; 
			i = i + 2;
			continue; 
		}
		// skip "at least"
		if ( saved && wids[i] == h_least ) {lastWasBreak=0;continue;}
		if ( saved && wids[i] == h_most  ) {lastWasBreak=0;continue;}
		if ( saved && wids[i] == h_this  ) {lastWasBreak=0;continue;}
		// allow lower case "the" after "at", but skip it
		if ( saved && wids[i] == h_the ) {
			// check for fake at phrase
			if ( i+2 < nw && (wids[i+2] == h_heart ||
					  wids[i+2] == h_core ) ) {
				// skip it
				lastWasBreak = 0; continue; }
			// if it is lower case skip it so it is not
			// included in the place name
			if ( is_lower_utf8(wptrs[i]) ) {
				atFlag = true; lastWasBreak = 1; continue; }
			// otherwise do not do the lower case check right below
		}
		// "at the entrace"
		else if ( saved && wids[i] == h_entrance ) {
			atFlag = true; 
			// not a break because we need "at the entrance to the"
			lastWasBreak = 0; 
			continue;
		}
		else if ( saved && wids[i] == h_to && savedWid == h_entrance ){
			atFlag = true; 
			lastWasBreak = 1; 
			continue;
		}
		// does it have some kind of delimeter before it?
		else if ( is_lower_utf8(wptrs[i])){lastWasBreak = 0; continue;}
		// each candidate needs somekind of "break" before them
		if ( ! lastWasBreak ) continue;
		// skip if in a script section
		if ( sp[i]->m_flags & badFlags )  continue;
		// or in menu
		if ( sp[i]->m_flags & SEC_MENU )  continue;
		// . skip if trying to start with a date
		// . fixes http://www.usadancenm.org/links.html so we do
		//   no start fake street names with ":30 pm ..."
		if ( bits && (bits[i] & D_IS_IN_DATE) ) continue;

		// skip if trying to start with something we have already
		// listed as a street in the above loop
		if ( bits && (bits[i] & D_IS_IN_STREET) ) continue;

		// stop if streets are maxed
		//if ( m_ns >= MAX_STREETS ) break;
		// ok, we got a candidate, reset this
		lastWasBreak = 0;
		//int64_t h = 0LL;
		int64_t pi = 0LL;
		bool prevUpper = false;
		bool prevAdded = false; // added prev to the street array?
		// count em
		int32_t alphaCount = 0;
		int32_t numCount   = 0;
		// subalnum count
		int32_t subAlnumCount = 0;
		int64_t h = 0LL;
		int64_t lastWid2 = 0LL;
		// . now make a hash of all substrings of the following words
		//   for lookup into namedb
		// . ADD CANDIDATE
		for ( int32_t j = i ; j < nw ; j++ ) {
			// tags stop our train
			if ( tids[j] ) break;
			// or if ventures into a street from above
			if ( bits && (bits[j] & D_IS_IN_STREET) ) break;
			// do not include a date
			if ( bits && (bits[j] & D_IS_IN_DATE) ) break;
			// bad punct stops our train
			if ( ! wids[j] ) {
				char *p = wptrs[j];
				char *pend = p + wlens[j];
				for ( ; p < pend ; p++ ) {
					if ( is_wspace_a(*p) ) continue;
					if ( *p == '\''      ) continue;
					// Dave & Buster's
					if ( *p == '&'       ) continue;
					// St. John's College
					if ( *p == '.' && is_wspace_a(p[1]) &&
					     j>0 && isAbbr(wids[j-1]) )
						continue;
					break;
				}
				// bad punct stops the train!
				if ( p < pend ) break;
				// otherwise, just skip it
				continue;
			}
			// count it
			subAlnumCount++;
			// . do not add the first word if its "The" into this
			// . fixes "The Guild Cinema" not matching placedb
			//   entries for "Guild Cinema"
			//if ( wids[j] == h_the && h == 0LL ) continue;
			// are we upper?
			bool isUpper = is_upper_utf8 ( wptrs[j] );
			// fix for "North 4th Arts Center"
			if ( is_digit(wptrs[j][0])){isUpper=true; numCount++; }
			else                        alphaCount++;
			// lowercase non-stopword stops our train
			//if ( ! isUpper && ! ww->isStopWord(j) ) break;
			if ( ! isUpper && ! s_lc.isInTable(&wids[j]) ) break;
			// . convert place name word into base word
			// . synonyms
			// . converts 4th to fourth, theatre to theater, etc.
			//int64_t *hw = getSynonymWord ( &wids[j] , &pi );
			// wordid of previous word
			pi = wids[j];
			// shift and store
			h <<= 1LL;
			// xor it in
			h ^= wids[j];
			// save it
			int64_t savedWid2 = lastWid2;
			lastWid2 = wids[j];
			// do not int16_ten "Center of Arts" to "Center" because
			// it is causing the "Performing Arts Center of the 
			// the Steinbeck Institute of Art" to be an alias for
			// "San Jose Performing Arts Center" because 
			// "Performing Arts Center" is a subset of 
			// "San Jose Performing Arts Center".
			if(prevAdded&&savedWid2==h_center&&wids[j]==h_of){
				m_sm.rewind(1);
				prevAdded = false;
			}
			// do not end on a lower case stop word
			if ( ! isUpper ) { 
				// . got hash in stop words now
				// . ignore it if syn table returned 0 (ignore)
				//if ( *hw ) {
				//	h <<= 1LL;
				//	h ^= *hw;//wids[j];
				//}
				prevUpper = false; 
				continue; 
			}
			// prev was upper case and we are upper case,
			// overwrite the previous entry
			if ( prevAdded && prevUpper && isUpper ) {
				//m_ns--;
				m_sm.rewind(1);
				prevAdded = false;
			}
			// likewise, do not split sequences of lowercase words
			if ( prevAdded && ! prevUpper && ! isUpper ) {
				//m_ns--;
				m_sm.rewind(1);
				prevAdded = false;
			}
			// fix "Submit a" in "Submit a New Event"
			//if ( ! prevUpper && isUpper ) ns--;
			// set this
			prevUpper = isUpper;
			// ignore it if syn table returned 0 (ignore) (school)
			//if ( *hw ) {
			//	// mix it up
			//	h <<= 1LL;
			//	// incorporate
			//	h ^= *hw; // wids[j];
			//}
			// do not add if only a number, like 4th or 113
			if ( alphaCount == 0 ) continue;
			// skip if crazy - fixes graffiti.org
			if ( alphaCount > 10 ) continue;
			// . do not add if only one word with one letter
			// . fixes javascript variables being place names
			if ( alphaCount == 1 && wlens[j] == 1 ) continue;
			// or if just the word "the"
			if ( alphaCount == 1 && wids[j] == h_the ) continue;
			// now allowed to have City or Town like in
			// "City/Town: Albuquerque NM"
			// fixes www.dukecityfix.com/xn/detail/1233957:Eve
			// nt:391851?xg_source=activity from getting that
			// as a place name in abq
			if ( alphaCount ==1 && wids[j] == h_city ) continue;
			if ( alphaCount ==1 && wids[j] == h_town ) continue;
			// . mdw mdw mdw
			// . not allowed to be a city or adm1 name!
			// . fixes us getting "albuquerque" as a place name!
			if ( g_cities.isInTable ( &h ) ) continue;
			// or state name
			if ( g_states.isInTable ( &h ) ) continue;
			// or zip
			if ( g_zips.isInTable ( &h ) ) continue;
			// TODO: or country????

			// set this flag
			prevAdded = true;
			// add the street
			Place *street = (Place *)m_sm.getMem(sizeof(Place));
			if ( ! street ) return false;
			street->m_a       = i;
			street->m_b       = j+1;
			street->m_alnumA  = alnumPos;
			street->m_alnumB  = alnumPos+subAlnumCount;
			street->m_type    = PT_STREET;
			street->m_str     = wptrs[i];
			street->m_strlen  = wptrs[j]+wlens[j]-wptrs[i];
			//street->m_adm1[0] = 0;
			//street->m_adm1[1] = 0;
			street->m_adm1Bits= 0LL;
			//street->m_crid     = 0;
			street->m_bits    = 0;
			street->m_address = NULL;
			street->m_alias   = NULL;
			//street->m_hash    = h;
			//street->m_streetNumHash = 0;//wids[j];
			//street->m_streetIndHash = 0;//h_po;
			// why do we need this now?
			if ( is_upper_a(wptrs[i][0]) ) 
				street->m_bits |= PLF_HAS_UPPER;
			//
			// we are SPECIAL!!!!!!
			//
			street->m_flags2 = PLF2_IS_NAME;
			// or in this
			if ( saved ) street->m_flags2 |= PLF2_AFTER_AT;
			// set the m_hash member
			setHashes ( street , m_words , m_niceness );
			// do not add if hash is zero, that usually means it
			// is the single word "the"
			if ( street->m_hash == 0 ) {
				m_sm.rewind(1);
				continue;
			}
			// sanity check
			//if(street->m_hash == 0 ) { char *xx=NULL;*xx=0;}
			//m_ns++;
			// stop if full
			//if ( m_ns >= MAX_STREETS ) break;
		}
	}

	//
	//
	// END FAKE STREET LIST GENERATION
	//
	//


	//
	//
	// add UNKNOWN addresses
	//
	// i.e. "location to be determined"
	// i.e. "call for location"
	// This will cause Events.cpp to set the EV_UNKNOWN_LOCATION bit!!!
	//
	int32_t b2;
	bool add = false;
	alnumPos = -1;
	// do not do this if we are javascript
	ni = nw;
	if ( m_contentType == CT_JS ) ni = 0;
	// do not do this if we have no sections -- call from msg13
	if ( ! m_sections ) ni = 0;
	// loop over every word
	for ( int32_t i = 0 ; i < ni ; i++ ) {
		// skip if not word
		if ( ! wids[i] ) continue;
		// skip if in script section or whatever to keep alnumPos right
		// we need this to keep alnumPos in alignment with the other
		// places!
		if ( sp[i]->m_flags & badFlags ) continue;
		// count this
		alnumPos++;
		// must match this
		if ( i+6<nw &&
		     wids[i  ] == h_location &&
		     wids[i+2] == h_to &&
		     wids[i+4] == h_be &&
		     wids[i+6] == h_determined ) {
			add = true;
			b2 = i + 7;
		}
		if ( i+6<nw &&
		     wids[i  ] == h_call &&
		     wids[i+2] == h_for &&
		     wids[i+4] == h_location ) {
			add = true;
			b2 = i + 5;
		}
		// . no,no, i like looking for words that indicate events.
		//   getting into the meaning of the language seems to be the
		//   way to go, because signmeup.com's sections are all
		//   div tags describing the same event really.
		// . no, now we fix this right with SEC_TOD_EVENT flags
		//   set in Dates.cpp. you can't telescope to a brother
		//   that has that flag set
		// . "details tba"
		// . fixes abtango.com where everyone uses the April 2010
		//   as a header
		if ( i+2<nw &&
		     wids[i  ] == h_details &&
		     wids[i+2] == h_tba ) {
			add = true;
			b2 = i + 3;
		}
		// call x-y-z for location
		if ( i+6<nw &&
		     wids[i  ] == h_call &&
		     wids[i+8] == h_for &&
		     wids[i+10] == h_location ) {
			add = true;
			b2 = i + 11;
		}
		// call x-y for location
		if ( i+6<nw &&
		     wids[i  ] == h_call &&
		     wids[i+6] == h_for &&
		     wids[i+8] == h_location ) {
			add = true;
			b2 = i + 9;
		}
		// skip if nothing found
		if ( ! add ) continue;
		// reset it
		add = false;
		// stop if full
		//if ( m_ns >= MAX_STREETS ) break;
		// add the street
		Place *street = (Place *)m_sm.getMem(sizeof(Place));
		if ( ! street ) return false;
		street->m_a       = i;//a2;
		street->m_b       = b2;
		// do we need these?
		street->m_alnumA  = alnumPos;
		street->m_alnumB  = alnumPos + 1; // this is wrong
		street->m_type    = PT_STREET;
		street->m_str     = wptrs[i];
		street->m_strlen  = wptrs[b2-1]+wlens[b2-1]-wptrs[i];
		//street->m_adm1[0] = 0;
		//street->m_adm1[1] = 0;
		street->m_adm1Bits= 0LL;
		//street->m_crid     = 0;
		street->m_bits    = 0;
		street->m_address = NULL;
		street->m_alias   = NULL;
		// why do we need this now?
		if ( is_upper_a(wptrs[i][0]) ) 
			street->m_bits |= PLF_HAS_UPPER;
		// we are SPECIAL!!!!!!
		street->m_flags2 = PLF2_IS_NAME | PLF2_AFTER_AT;
		// set the m_hash member
		setHashes ( street , m_words , m_niceness );
		// do not add if hash is zero, that usually means it
		// is the single word "the"
		if ( street->m_hash == 0 ) continue;
		// inc it
		//m_ns++;
	}


	// update this
	//m_ns = m_ns;

	// sanity check
	//if ( m_ns > MAX_STREETS ) { char *xx=NULL;*xx=0; }

	//if ( m_ns == MAX_STREETS ) { 
	//	log("addr: street buf is maxed out for %s!",m_url->m_url);
	//	//char *xx=NULL;*xx=0;
	//}

	// if no streets found, then bail, that is it
	if ( m_sm.getNumPtrs() == 0 ) return true;

	// breached?
	//if ( m_sm.getNumPtrs() > 4000 ) 
	//	m_breached = true;

	/////////////////////////////
	//
	// set PLF2_REGISTER
	//
	/////////////////////////////
	// do not do this logic if we are javascript because we do not set
	// SEC_SENTENCE if the file is javascript
	int32_t imax = m_sm.getNumPtrs();//m_ns;
	if ( m_contentType == CT_JS ) imax = 0;

	//
	// if it is a place to buy tickets or register for an event then
	// let's set this flag so Events.cpp can ignore it!
	for ( int32_t i = 0 ; i < imax ; i++ ) {
		// not for msg13's call
		if ( ! m_sections ) break;
		// get the street that we center the address around
		Place *street = (Place *)m_sm.getPtr(i);
		// telescope up until we hit the sentence section
		Section *ss = m_sections->m_sectionPtrs[street->m_a];
		for ( ; ss ; ss = ss->m_parent ) 
			if ( ss->m_flags & SEC_SENTENCE ) break;
		// must have it
		if ( ! ss ) { char *xx=NULL;*xx=0; }
		// . if section is contained in title tag, allow it through
		// . fixes "Tingley Coliseum : Buy Tickets , ... " for
		//   events.mapchannels.com
		if ( ss->m_flags & SEC_IN_TITLE ) continue;
		// . use it as the bookends
		// . [a,b) may now actually expand beyond the "ss" section
		//   because of the new split sentence logic in
		//   Sections::addSentences() to deal with sentences that
		//   unevenly span multiple sections like in aliconference.com
		//   and abqtango.com
		int32_t a = ss->m_senta;
		int32_t b = ss->m_sentb;
		// use this i guess
		if ( isTicketDate ( a , b , m_wids , m_bits , m_niceness ) ) 
			street->m_flags2 |= PLF2_TICKET_PLACE;
		/*
		// assume not
		bool reg = false;
		// now scan forward from there
		for ( int32_t j = a ; j < b ; j++ ) {
			// skip punct words
			if ( ! m_wids[j] ) continue;
			// is it register?
			if ( m_wids[j] == h_register ) { 
				reg = true; break; }
			if ( m_wids[j] == h_sign && m_wids[j+2] == h_up ) {
				reg = true; break; }
			if ( m_wids[j] == h_signup ) {
				reg = true; break; }
			if ( m_wids[j] == h_buy && m_wids[j+2] == h_tickets ) {
				reg = true; break; }
			if ( m_wids[j] == h_purchase&&m_wids[j+2]==h_tickets) {
				reg = true; break; }
			if ( m_wids[j] == h_get && m_wids[j+2] == h_tickets ) {
				reg = true; break; }
			// "give them tickets to" for santafe playhouse url
			// to cancel out "Max's or Dish n' Spoon" as a place
			if ( m_wids[j] == h_tickets&& m_wids[j+2] == h_to ) {
				reg = true; break; }
			if ( m_wids[j] == h_presale ) {
				reg = true; break; }
			if ( m_wids[j] == h_on && m_wids[j+2] == h_sale ) {
				reg = true; break; }
			if ( m_wids[j] == h_pre && m_wids[j+2] == h_sale ) {
				reg = true; break; }
			if ( m_wids[j] == h_sales && m_wids[j+2] == h_end ) {
				reg = true; break; }
			if ( m_wids[j] == h_sales && m_wids[j+2] == h_begin ) {
				reg = true; break; }
			if ( m_wids[j] == h_sales && m_wids[j+2] == h_start ) {
				reg = true; break; }
		}
		// it is such a place
		if ( reg ) street->m_flags2 |= PLF2_TICKET_PLACE;
		*/
	}
	



	//
	// . set Section::numStreets var
	// . scan streets and set Section::m_numStreets
	// . if streets are adjacent in one continuous mass, then treat as
	//   a single street for these purposes
	/*
	for ( int32_t X = 0 ; X < ns ; X++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get the street that we center the address around
		Place *street = &streets[X];
		// get street before it
		Place *prev = NULL; if ( X > 0 ) prev = &streets[X-1];
		// . if we had a street immediately before us, bail
		// . we count consecutive streets as a single street
		if ( prev && prev->m_alnumB == street->m_alnumA ) continue;
		// get it
		Section *si = sp[street->m_a];
		// inc recusrively
		for ( ; si ; si = si->m_parent )
			// inc it
			si->m_numStreets++;
	}
	*/

	// debug
	//printPlaces( streets , ns , m_pbuf , m_sections );

	//
	//
	// . the huge address creation part
	// . ultimately sets m_addresses[]/m_na array
	//
	//

	// . make a 5 lists, one for each place type, to hold all the 
	//   Places in the int16_tlist[] array we just created
	// . include Places in the tagRec and title as well
	// . use a NULL ptr to indicate "no place"
	// . then do a 6-way nested loop over all the combos
	Place *pname   [10]; int32_t nn = 0;
	Place *padm1   [MAX_ADM1  ]; int32_t na = 0;
	Place *pcity   [MAX_CITIES]; int32_t nc = 0;
	Place *pzip    [MAX_ZIPS]; int32_t nz = 0;
	Place *psuite  [10]; int32_t nu = 0;
	// each latlon might be tethered to a street address already
	// topologically speaking. we need to telescope it out and
	// tether it to the first street we hit. including afterats and
	// fake street names? it might be tethered to a place venue name
	// that we never recognize. and intead we tether it to a brother
	// brother city/state when we shouldn't.
	//Place *latlon  [MAX_LATLONS];
	//Place *pctry   [10]; int32_t ny = 0;
	
	//Place places [ MAX_PLACES ];
	//int32_t np = 0;

	// sanity check
	//if ( 500 > MAX_PLACES ) { char *xx=NULL;*xx=0; }
	// add places from the body!
	//np = addProperPlaces ( 0 , nw , 500 , places , MAX_PLACES , np ,
	//		       // set this flag Place::m_flags
	//		       PLF_FROMBODY );
	
	/*
	// add in default adm1/city/zip from title
	int32_t a = 0;
	int32_t b = 0;
	int32_t tapos = 0;
	if ( ss ) {
		a     = ss->m_titleStart;
		tapos = ss->m_titleStartAlnumPos;
	}
	if ( ss ) b = ss->m_titleEnd  ;
	// limit those nasty int32_t titles
	if ( b > a + 30 ) b = a + 30;

	// add proper places from title into "places" array
	np = addProperPlaces ( a , b , 20 , places , MAX_PLACES , np ,
			       // . set this flag Place::m_flags
			       PLF_FROMTITLE , 
			       // alnumPos, subtract -1 since it immediately
			       // adds 1 to the first alnum it finds
			       tapos - 1 ,
			       -1 );
	// breach check
	if ( np > MAX_PLACES ) { char *xx=NULL;*xx=0; }
	*/

	// save for popping
	//int32_t np_stack = m_np;
	
	// int16_tcut
	char **w = wptrs;

	HashTableX dat;
	char datbuf[4000];
	dat.set ( 4 , 4 , 256, datbuf, 4000,false,m_niceness,"adm1buf");
	// . set up the base array of all states
	// . "bn" = baseNum
	// . TODO: make sure state we select is not in a street!
	int32_t bn = 0;
	// alway have a NULL
	padm1 [ bn++ ] = NULL;
	// then
	for ( int32_t i = 0 ; i < m_npSaved ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get city, state or zip
		Place *p = (Place *)m_pm.getPtr(i);
		// . allow state to come from anywhere in the document
		// . TODO: later add meta description to get christinesaari.com
		if ( p->m_type != PT_STATE ) continue;
		// skip if interesects a street, like "ohio street"
		if ( p->m_a >= 0 && bits && (bits[p->m_a] & D_IS_IN_STREET) ) 
			continue;
		// make the key for deduping
		char key[4];
		key[0] = p->m_adm1[0];
		key[1] = p->m_adm1[1];
		key[2] = 0;
		key[3] = 0;
		// skip if dup
		if ( dat.isInTable ( &key ) ) continue;
		// add it to the dedup table
		if ( ! dat.addKey ( &key, &p ) ) return false;
		// add to our array
		padm1 [ bn++ ] = p;
	}
	// how can this happen?
	if ( bn > 55 ) { char *xx=NULL;*xx=0; }


	// "X" loops over all the streets we have
	for ( int32_t X = 0 ; X < m_sm.getNumPtrs() ; X++ ) {
		// get the street that we center the address around
		Place *street = (Place *)m_sm.getPtr(X);
		// debug
		//logf(LOG_DEBUG,"events: ****** X=%"INT32" *****",X);
		// reset these
		nc = 0;
		na = bn;
		nz = 0;
		nn = 0;
		nu = 0;
		//ny = 0;
		// preserve the places on there from title
		//np = np_stack;
		// these guys are allowed to have "no place", but everyone else
		// must have something
		pzip   [nz++] = NULL;
		//padm1  [na++] = NULL;
		//psuite [nu++] = NULL;
		//pctry  [ny++] = NULL;
		//if ( dc > 0 ) pcity  [nc++] = NULL;
		//if ( dc > 0 ) padm1  [na++] = NULL;
		//if ( dc > 0 ) pname  [nn++] = NULL;
		// add a NULL because if city is unique we can fill this in
		//padm1  [na++] = NULL;
		// likewise, if we have a zip code we can fill in the city too
		pcity  [nc++] = NULL;		


		//
		// search for a suite name BEFORE the street
		//
		int32_t k = street->m_a - 1 ;
		// re-set this
		alnumPos = street->m_alnumA ;
		// start of it
		int32_t ak = -1;
		// flag init
		bool gotSuiteBefore = 0;
		// ptr
		Place *suiteBefore = NULL;
		// suite hash
		int64_t suh = 0LL;
		// start alnumPos
		int32_t akPos = -1;
		// now scan for suite, stop after hitting our first alnum word
		for ( ; k >= 0 ; k-- ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if non alnum word
			if ( ! wids[k] ) continue;
			// skip if in a script section
			if (sp&&sp[k]&&(sp[k]->m_flags & badFlags) )  continue;
			// it's an alnum
			alnumPos--;
			// stop if we are not a suite designation
			if ( wlens[k] != 1 && ! m_words->hasDigit(k) ) break;
			// now before us must be a # sign
			if ( k - 1 > 0 &&  m_words->hasChar(k-1,'#') ) {
				// start of it was this punct word i guess
				ak = k - 1;
				// and this
				akPos = alnumPos;
				// update suite hash
				suh = wids[k];
			}
			// or a suite indicator
			if ( k - 2 >= 0 && 
			     ( wids[k-2] == h_suite    ||
			       wids[k-2] == h_ste      ||
			       wids[k-2] == h_building ||
			       wids[k-2] == h_bldg     ||
			       wids[k-2] == h_bld      ||
			       wids[k-2] == h_pier     ||
			       wids[k-2] == h_room     ||
			       wids[k-2] == h_rm       ||
			       wids[k-2] == h_unit      ) ) {
				// set this
				akPos = alnumPos - 1;
				// start here
				ak = k - 2;
				// update suite hash
				suh = wids[k];
				// skip that
				//k++;
				// skip punct word
				//k++;
				// update suite hash
				suh <<= 1;
				// xor it in
				suh ^= wids[k];
				// and the indicator
				suh <<= 1;
				suh ^= wids[k-2];
			}
			// that is it either way
			break;
		}
		// add the suite before the place name
		if ( suh ) { // && m_np < MAX_PLACES ) {
			// note it
			gotSuiteBefore = true;
			// sanity check
			//if ( m_np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }
			// point to the suite to add
			Place *pp = (Place *)m_pm.getMem(sizeof(Place));
			if ( ! pp ) return false;
			// point to it
			suiteBefore = pp;
			// length
			int32_t plen = wptrs[k]-wptrs[ak]+wlens[k];
			// point to the suite
			char *ps = wptrs[ak];
			// skip over initial comma
			if ( *ps == ',' ) { ps++; plen--; }
			// set it
			pp->m_a       = ak;
			pp->m_b       = k+1;
			pp->m_alnumA  = akPos;
			pp->m_alnumB  = alnumPos+1;
			pp->m_type    = PT_SUITE;
			pp->m_str     = ps;
			pp->m_strlen  = plen;
			pp->m_hash    = 0LL;//suh;
			//pp->m_adm1[0] = 0;
			//pp->m_adm1[1] = 0;
			//pp->m_crid     = 0;
			pp->m_bits    = 0;
			pp->m_flags2  = 0;
			// thats a suite
			psuite[nu++] = pp;
			// now just use this
			setHashes(pp,m_words,m_niceness);
			// point to next place
			//m_np++;
		}



		//
		// search for a suite name after the street
		//
		k = street->m_b;
		// re-set this
		alnumPos = street->m_alnumB - 1;
		// suite hash
		suh = 0LL;
		// remember start of suite
		int32_t startk        = -1;
		int32_t startAlnumPos = -1;
		char got = 0;
		// point to next street
		Place *next = NULL;
		if ( X+1 < m_sm.getNumPtrs() ) 
			next = (Place *)m_sm.getPtr(X+1);
		// skip until we got a wordid
		for ( ; k < nw ; k++ ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if not an alnum word
			if ( ! wids[k] ) continue;
			// skip if in a script section
			if (sp&&sp[k]&&(sp[k]->m_flags & badFlags) )  continue;
			// it's an alnum
			alnumPos++;
			// start here
			if ( wids[k] == h_building ) { got = 3; continue; }
			if ( wids[k] == h_bldg     ) { got = 3; continue; }
			if ( wids[k] == h_bld      ) { got = 3; continue; }
			if ( wids[k] == h_unit     ) { got = 3; continue; }
			if ( wids[k] == h_suite    ) { got = 2; continue; }
			if ( wids[k] == h_ste      ) { got = 2; continue; }
			if ( wids[k] == h_pier     ) { got = 3; continue; }
			if ( wids[k] == h_room     ) { got = 3; continue; }
			if ( wids[k] == h_rm       ) { got = 3; continue; }
			// having a # sign before us is good!
			if ( k-1>=0 && !tids[k-1]&& ! got &&
			     m_words->hasChar(k-1,'#'))
				got = 1;
			// stop if no suite indicator
			if ( ! got ) break;
			// no tag must preceed us
			if ( tids[k-1]   ) break;
			// a number follows?
			bool isNum = false;
			if ( is_digit(wptrs[k][0])) isNum = true;
			// a single letter counts as a number too!
			if ( wlens[k]==1 ) isNum = true;
			// or if we end in a number
			if ( is_digit(wptrs[k][wlens[k]-1])) isNum = true;
			// everyone but suites need something more stringent
			if ( got == 3 && ! isNum ) { got = 0; continue; }
			// put back
			if ( got == 3 ) got = 2;
			// remember the start of it
			startk = k - got;
			// and this too
			if ( got == 2 ) startAlnumPos = alnumPos - 1;
			// if just the pound sign, do not change this
			else            startAlnumPos = alnumPos;
			// incorporate into the suite place hash
			if ( got == 2 ) suh = wids[k];
			else            suh = 0;
			// incorporate ourselves into "suh" (suite hash)
			suh <<= 1;
			suh ^= wids[k];
			// next is supposed to be the next street name!
			// but it can run into the next list of fake street
			// names that we added above, so fix that
			if ( next && next->m_a <= k ) next = NULL;
			// all done?
			bool gotExt = true;
			if      ( k+1 >= nw                  ) gotExt = false;
			else if ( wptrs[k+1][0] != '-'       ) gotExt = false;
			else if ( wlens[k+1]    != 1         ) gotExt = false;
			// fix "Suite 920-N"
			//if ( ! is_digit(wptrs[k+2][0])  ) gotExt = false;
			if ( next && k + 2 >= next->m_a ) gotExt = false;
			// if we got something like "Suite G-2" (extension)
			// then add these up
			if ( gotExt ) {
				k        += 2;
				alnumPos += 1;
				// incorporate that too
				suh <<= 1;
				suh ^= wids[k];
			}
			// length
			int32_t plen = wptrs[k]-wptrs[startk]+wlens[k];
			// sanity check. i've seen this happen before,
			// on http://cruises.priceline.com/promotion/price
			// line/lm/default.asp for the $339 price, so let's
			// just ignore such beasties now
			if ( plen > 100 ) continue;//{ char *xx=NULL;*xx=0; }
			// sanity check -- if we have no room, bail!
			//if ( m_np >= MAX_PLACES ) break;
			// point to the suite to add
			Place *pp = (Place *)m_pm.getMem(sizeof(Place));
			if ( ! pp ) return false;
			// point to the suite
			char *ps = wptrs[startk];
			// skip over initial comma
			if ( *ps == ',' ) { ps++; plen--; }
			// set it
			pp->m_a       = startk;
			pp->m_b       = k+1;
			pp->m_alnumA  = startAlnumPos;
			pp->m_alnumB  = alnumPos+1;
			pp->m_type    = PT_SUITE;
			pp->m_str     = ps;
			pp->m_strlen  = plen;
			pp->m_hash    = 0;//suh;
			//pp->m_adm1[0] = 0;
			//pp->m_adm1[1] = 0;
			//pp->m_crid     = 0;
			pp->m_bits    = 0;
			pp->m_flags2  = 0;
			// thats a suite
			psuite[nu++] = pp;
			// now just use this
			setHashes(pp,m_words,m_niceness);
			// point to next place
			//m_np++;
			// all done
			break;
		}

		// provide an empty suite if none
		if ( nu <= 0 ) psuite [nu++] = NULL;

		// "end" is the word # of first word in the street address
		int32_t end      = street->m_a;
		int32_t endAlnum = street->m_alnumA;
		// but if we had a suite before... skip over it
		if ( gotSuiteBefore ) {
			end      = suiteBefore->m_a;
			endAlnum = suiteBefore->m_alnumA;
		}

		///////////////////////
		//
		// GET THE PLACE NAME before the street (or before the suite)
		//
		///////////////////////

		// start at word before word # end
		int32_t i = end - 1;
		// start here
		int32_t pa2 = m_am.getNumPtrs() - 1; // m_na - 1;
		// save start of place array
		int32_t savednp = m_pm.getNumPtrs();//m_np;
		// save start of name array
		int32_t savednn = nn;
		// init
		Address *preva = NULL;
		// assign
		if ( pa2 >= 0 ) preva = (Address *)m_am.getPtr(pa2);
				
		// count how many place names we add
		int32_t pcount = 0;

		////
		//
		// "Tingley Colesium, Abq NM"
		//
		// if the street is a place name, skip this next part...
		//
		////
		if ( street->m_flags2 & PLF2_IS_NAME ) i = -1;


		// we come back up here to filter out street address labels
	redo:

		// set this
		int32_t mini = -1;
		// get the prev address b boundard
		if ( preva ) mini = preva->m_street->m_b;
		// if preva was inlined, use zip or adm1 then
		if ( preva && (preva->m_flags & AF_INLINED) ) {
			if ( preva->m_zip && preva->m_zip->m_b > mini )
				mini = preva->m_zip->m_b;
			if ( preva->m_adm1 && preva->m_adm1->m_b > mini )
				mini = preva->m_adm1->m_b;
			if ( preva->m_city && preva->m_city->m_b > mini )
				mini = preva->m_city->m_b;
		}

		int32_t parensCount = 0;
		// keep an ongoing hash of alnum words in the name
		//int64_t h = 0LL;
		// backup until we hit an alnum
		for ( ; i >= 0 ; i-- ) {

			// do not cross a title tag to get place name
			if ( tids[i] ==  TAG_TITLE         ) { i = -1; break; }
			if ( tids[i] == (TAG_TITLE|BACKBIT)) { i = -1; break; }

			// skip if not alnum word
			if ( ! wids[i] ) {
				// skip tags
				if ( tids[i] ) continue;
				// see if this punct word has a ')' in it!
				char *pp = wptrs[i];
				char *ppend = pp + wlens[i];
				for ( ; pp < ppend ; pp++ ) {
					// count 'em
					if ( *pp=='(' ) parensCount--;
					if ( *pp==')' ) parensCount++;
				}
				continue;
			}
			// . skip if in bad section
			// . the two trumba.com urls have quite a few
			//   addresses in common, causing the place names
			//   to get their SEC_DUP bit set.  But out new algo
			//   plays somewhat nicely with menu cruft because
			//   we have to verify the place names with another
			//   website to really make the place name stick,
			//   so let's no longer use SEC_DUP or'ed in with
			//   the badFlags. mdw.
			if ( sp && (sp[i]->m_flags & badFlags ) )  // |SEC_DUP)
				continue;

			// abqpeaceandjustice.org has their address on every
			// web page, but on one web page it was 
			// "202 Hardvard SE" and another it was SouthEast...
			// BUT for the most part this logic is ok!

			// if the street does not have SEC_DUP set in its
			// section, BUT the name does, then ignore the name!
			if ( street->m_a>= 0 &&
			     // msg13 has no sections
			     sp &&
			     // if street section does not have SEC_DUP set
			     ! (sp[street->m_a]->m_votesForDup) &&
			     // but the ith word does
			     ( sp[i]->m_votesForDup ) )
				// then skip over this word and do not
				// allow it to be the place name
				continue;

			// . skip if "at"
			// . "Post Office & Library at 950 pinetree se ..."
			//    http://www.xeriscapenm.com/xeriscape_gardens.php
			// . no "thru October at 6718 Rio Grande NW."
			// . "write elizabeth doak, treasurer at 1606 silver"
			// . no no i guess we got date detection now
			// . and skip "xyz [is located at] 123 main st"
			if ( wids[i] == h_at && is_lower_utf8(wptrs[i]) )
				continue;

			if ( wids[i] == h_is && is_lower_utf8(wptrs[i]) )
				continue;

			if ( wids[i] == h_located && is_lower_utf8(wptrs[i]) )
				continue;

			// skip phone #'s
			if ( i>=6 &&
			     wlens[i]==4 && 
			     m_words->isNum(i) && 
			     wlens[i-2]==3 &&
			     m_words->isNum(i-2) &&
			     wlens[i-4]==3 &&
			     m_words->isNum(i-4) ) {
				i -= 4;
				continue;
			}
			// phone with no area code
			else if ( i>=4 &&
				  wlens[i]==4 && 
				  m_words->isNum(i) && 
				  wlens[i-2]==3 &&
				  m_words->isNum(i-2) ) {
				i -= 2;
				continue;
			}

			//
			// . we are getting place names like "3 baths..."
			//   for "6769 Guadalupe Trl Nw" for the url 
			//   http://www.realtor.com/property-detail/608-
			//   Bledsoe-Rd-NW_Albuquerque_NM_87107_fa9ca500
			//   which are in the section of a different street,
			//   so fix that with this logic.
			// . basically expand the section around "i" and see
			//   if it belongs to street #X or to street #X-1.
			//

			// get prev street
			Place *prev = NULL; 
			if ( X>0 ) prev = (Place *)m_sm.getPtr(X-1);
			// flags
			bool gotOurStreet  = false;
			bool gotPrevStreet = false;
			// keep expanding the section around the
			// place name until we get a street or multiple 
			// streets. if we only get a single street, then
			// it must be OUR STREET, "street"
			Section *si = NULL;
			// msg13 has no sections
			if ( sp ) si = sp[i];
			// keep expanding section until we got street in it
			for ( ; prev && si ; si=si->m_parent ) {
				// stop when it contains our street or
				// previous street
				if ( si->m_a <= street->m_a &&
				     si->m_b >= street->m_b ) 
					gotOurStreet = true;
				if ( si->m_a <= prev->m_a &&
				     si->m_b >= prev->m_b ) 
					gotPrevStreet = true;
				// break on either
				if ( gotOurStreet  ) break;
				if ( gotPrevStreet ) break;
			}
			// if it is more closely related to the previous street
			// then do not assign this place name to us, i guess
			// we do not have a good one for this street!
			if ( gotPrevStreet && ! gotOurStreet ) 
				i = -1;

			// ok we got a candidate
			break;
		}

		// . if our place name candidate is in a date, then assume
		//   that we have no place name!
		// . fixes http://obits.abqjournal.com/obits/2004/04/13
		if ( i >= 0 && i < nw && bits && ( bits[i] & D_IS_IN_DATE ) &&
		     // incase place name ends in midnight or noon
		     wids[i] != h_daily && 
		     wids[i] != h_noon && 
		     wids[i] != h_midnight ) 
			i = -1;

		// fix "copyright ; 2009 Albuquerque Journal; Abq ; NM"
		// for http://obits.abqjournal.com/obits/2004/04/13
		if ( i >= 0 && i < nw && wids[i] == h_copyright) {
			// stop getting a name
			i = -1;
			// and mark street as bad
			//street->m_bits |= PLF_IGNORE;
			// go to next street!
			continue;
		}

		// set that as our right side
		int32_t righti = i;
		// reset this count
		int32_t alnumCount = 0;
		int32_t alphaCount = 0;
		// reset this
		int32_t atPos = -1;
		bool atCityName = false;
		int32_t atAlnumCount = -1;
		// reset this
		bool hadUpper = false;
		bool hadLower = false;
		bool hadAnd   = false;
		// save last good i
		int32_t lasti = -1;
		bool isUpper;
		bool isLower;
		// . ok, go backwards up to 15 alnum words from there
		// . The Harwood Museum of Art of the University of New Mexico
		for ( ; i >= 0 && alnumCount < MAX_ALNUMS_IN_NAME ; i-- ) {
			// ignore if in script, etc. tags
			if ( sp && (sp[i]->m_flags & badFlags) ) continue;
			// . ignore if in menu section
			// . might be like "<td>place</td>"
			// . i know for http://www.publicbroadcasting.net/kunm/
			//   events.eventsmain?action=showEvent&eventID=833142
			//   we are getting "Address: " as the place name
			//   because it is in the table like that.
			// . TODO: for single event pages we must require at
			//   least another page from same site with same 
			//   tagPairHash to prevent this kind of thing
			// . likewise, for the same reason above, there are
			//   two trumba.com urls that share some addresses
			//   in common and the place name is getting its
			//   SEC_DUP bit set, so let's reply more on 
			//   verifying place name 1 and 2 than this:
			//if ( sp[i]->m_flags & SEC_DUP ) continue;
			// stop at tag, not bold tags though
			// fix for local.yahoo.com highlighting terms
			// in the place name.
			if ( tids[i] ) {
				if ( tids[i] == TAG_B ) continue;
				if ( tids[i] == (TAG_B | BACKBIT) ) continue;
				break;
			}
			// count alnums
			if ( wids[i] ) { 

				// do not stop something in parentheses
				if ( parensCount > 0 )
					goto skipbreak;

				// no dates allowed in name
				if ( bits && (bits[i] & D_IS_IN_DATE) &&
				     // "1am gallery"
				     (wlens[i]!=3||
				      to_lower_a(wptrs[i][1])!='a') &&
				     // high noon saloon on 
				     // www.estrelladelnortevineyard.com/
				     // SFV_retloc.php
				     wids[i] != h_daily && 
				     wids[i] != h_noon     &&
				     wids[i] != h_midnight ) 
					break;
				/*
				// if we are the "last" word in the place name
				// then we must always be upper case!
				if ( alnumCount == 0 &&
				     ! is_upper_utf8(wptrs[i]) &&
				     // digits can not be upper case
				     ! is_digit(wptrs[i]) &&
				     // allow "Subway at 1300 main st."
				     wids[i] != h_at &&
				     // allow "Cable.com"
				     (i-1<0 || wptrs[i][-1]=='.') )
					break;
				*/

				// "KS CITY CONFIDENTIAL and 99 RIVER STREET"
				if ( alnumCount==0 && wids[i]==h_and) break;
				/*
				// "Property Information for 440 Bledsoe Rd"
				// "Map for ..."
				if ( alnumCount==0 && wids[i]==h_for) continue;
				// "Map of ..."
				if ( alnumCount==0 && wids[i]==h_of) continue;
				*/

				isLower = is_lower_utf8(wptrs[i]);
				isUpper = is_upper_utf8(wptrs[i]);

				// hack fix for "O'niell's Pub" (apostrop)
				if ( i >= 2 &&
				     wlens[i-1] == 1    &&
				     wptrs[i-1][0]=='\''&&
				     wids[i-2]          &&
				     wlens[i-2] == 1    &&
				     wptrs[i-2][0] =='O' ) {
					// assume it is not lower case
					isLower = false;
					isUpper = true;
				}

				// if this is lower and we had an upper
				if ( isLower &&
				     hadUpper && 
				     // must not be an allowable lowercase word
				     ! s_lc.isInTable(&wids[i]) &&
				     // fix "Bandido's Hideout Restaurant" cuz
				     // it was breaking on the "s" cuz that is
				     // not a query stop word!
				     wlens[i] > 1 )
					break;
				// if we had a lower non-stop word, and then
				// we hit an upper...
				if ( isUpper && hadLower ) {
					// force an abort on this street
					lasti = -1;
					break;
				}
				// if we hit a number followed by am or pm, 
				// that is a time so stop the scan!
				//if (( wids[i] == h_am || wids[i] == h_pm ) &&
				//     i >= 2 && is_digit(wptrs[i-2][0]) )
				//	break;
				// if we hit "by" and "sponsored" or
				// "arranged" preceeds it, stop!
				// fixes: "arrangements by ..." in 
				// obits.abqjournal.com/obits/2004/04/13
				if ( wids[i] == h_by && i-2>=0 &&
				     ( wids[i-2] == h_arrangements ||
				       wids[i-2] == h_arranged ||
				       wids[i-2] == h_sponsored ) )
					break;
				// if we got something and we hit the
				// previous address zip or state or city
				// then just stop
				if ( i < mini && lasti >= 0 )
					break;
				// to be more strict, no lower at all!
				// NO! we lose "explora" then
				//if ( is_lower_utf8(wptrs[i]) && 
				//     ! ww->isQueryStopWord(i) ) 
				//	break;
				// . cut off here too
				// . do not include the previous street name
				//   as part of your place name
				if ( //preva && 
				     i < mini && // preva->m_street->m_b && 
				     lasti == -1 ) {
					// skip over it
					i = preva->m_street->m_a - 1; 
					// update prev
					pa2--;
					if ( pa2>=0 ) 
					     preva=(Address *)m_am.getPtr(pa2);
					else  
					     preva = NULL;
					// now we only redo if this is the
					// FIRST place name
					if ( pcount == 0 ) goto redo; 
					// otherwise, stop it!
					break;
				}
				// if we did have some junk in the place name
				// then use that, but do not include this
				// street name as part of it
				if ( preva && i < preva->m_street->m_b ) 
					break;

				// if we hit previous address 

			skipbreak:
				// store the last good word position
				lasti = i; 
				// count it
				alnumCount++; 
				// NO! we are looping backwards, so we 
				// can't do this here. we now do it below
				// mix it up
				//h <<= 1;
				// hash it into our ongoing hash
				//h ^= wids[i];
				// skip words starting with a digit
				if ( is_digit(wptrs[i][0]) ) continue;
				// consider it alpha i guess now
				alphaCount++;
				// is it upper?
				if ( isUpper ) hadUpper = 1;

				if ( wids[i] == h_and ) hadAnd = true;

				// caution "Santa Fe Co-op" or "E-mail" is ok ;
				// don't set hasLower for "op" or "mail" 
				if ( i-2>= 0 &&  wptrs[i][-1]=='-' &&
				     is_alnum_a(wptrs[i][-2]) )
					continue;

				// same goes for "Cable.com"
				if ( i-2>= 0 &&  wptrs[i][-1]=='.' &&
				     is_alnum_a(wptrs[i][-2]) )
					continue;

				// hadLower only valid if not query stop word
				if ( isLower && //_lower_utf8(wptrs[i]) &&
				     // must not be an allowable lowercase word
				     ! s_lc.isInTable(&wids[i]) 
				     // for smoe reason 's' is not a query
				     // stop word, and we had a bar named
				     // "Slim's" that we needed to get
				     // ... this is in s_lc table now
				     //! ww->isStopWord(i) ) 
				     )
					hadLower = 1;
				// record first at
				if ( wids[i] == h_at && atPos == -1) {
					atPos= i;
					// save this in case we trim off
					atAlnumCount = alnumCount - 1;
					// get string from right after "at"
					// and before the street and see
					// if it is a city name. get hash
					// of all those words so we can look
					// it up. hashes all alnum words
					// in [i+2,righti+1) interval.
					atCityName = isCityName(i+2,righti+1);
				}
				// skip to next
				continue; 
			}
			// keep parensCount up to date
			char *pp = wptrs[i];
			char *ppend = pp + wlens[i];
			for ( ; pp < ppend ; pp++ ) {
				// count 'em
				if ( *pp=='(' ) parensCount--;
				if ( *pp==')' ) parensCount++;
			}
			// do not stop something in parentheses
			if ( parensCount > 0 ) continue;
			// only certain types of punct can be in a place name
			if ( wlens[i] == 1 ) {
				// single space ok
				if ( is_wspace_a(w[i][0])  ) continue;
				if ( w[i][0] == '\r' ) continue;
				// hyphen ok
				if ( w[i][0] == '-' ) continue;
				// apostrophe ok
				if ( w[i][0] == '\'' ) continue;
				// / ok, "QX&V Electro/Mechanical"
				// but breaks:
				// "Santa Fe Playhouse/Santa Fe Little Theater"
				//if ( w[i][0] == '/' ) continue;
				// ampersand ok
				if ( w[i][0] == '&' ) continue;
				// asterisk ok ( e*trade)
				if ( w[i][0] == '*' ) continue;
				// period ok (xyz.com,u.s. post office)
				if ( w[i][0] == '.' ) continue;
				// . apostrophe ok if alnum-locked
				// . "Bandido's Hideout"
				if ( w[i][0]=='\'' ) 
					if (is_alnum_a(w[i][-1]) &&
					    is_alnum_a(w[i][1]) ) 
						continue;
				// otherwise, not
				break;
			}
			if ( wlens[i] == 2 ) {
				// . up to one parenthetical is ok
				// . "The Filling Station (Albuquerque, NM)"
				//   http://eventful.com/albuquerque/venues/
				//   the-filling-station-/V0-001-001121221-1
				// . we now have parensCount for this
				if ( is_wspace_a(w[i][0])&&
				     w[i][1]=='(')
					break; // continue;
				// double space ok
				if ( is_wspace_a(w[i][0])&&
				     is_wspace_a(w[i][1]))
					continue;
				// . comma space
				// . i was only allow inc. or llc. to follow
				//   but what about:
				//   "NM Children, Youth, and Families Dept."
				// . but then we got "St. John's College, 
				//   Peterson Student Center" which is bad
				//   so now we require an and i guess
				if ( w[i][0]==','&&
				     is_wspace_a(w[i][1]) &&
				     ( hadAnd || 
				       wids[i+1] == h_inc ||
				       wids[i+1] == h_llc ) )
					continue;
				// Yahoo! or Yelp! Inc.
				if ( w[i][0]=='!' &&
				     is_wspace_a(w[i][1]) &&
				     i+1<nw && wids[i+1]==h_inc )
					continue;
				// colon space
				if ( w[i][0]==':'&&
				     is_wspace_a(w[i][1]) ) {
					// NO NO NO, never allow names
					// with colons in them now because
					// we have "place name 2" to pick
					// up the other name if it is a 
					// compound name containing a ':'
					break;
					// . Location: not allowed!
					// . "Location: Albuquerque Dance Ctr"
					if ( i-1>=0 && wids[i-1]==h_location)
						break;
					// . Address: not allowed!
					if ( i-1>=0 && wids[i-1]==h_address)
						break;
					// stop at Phone: too!
					if ( i-1>=0 && wids[i-1]==h_phone)
						break;
					// otherwise, allow it!
					continue;
				}
				// the $1 store
				if ( is_wspace_a(w[i][0])&&
				     w[i][1]== '$' ) 
					continue;
				// abbreviation (mtn. supply store)
				if ( w[i][0]=='.'&&
				     // "moving co., inc." (allow comma after)
				     (is_wspace_a(w[i][1]) ||w[i][1]==',') &&
				     i-1>=0 && wids[i-1] &&
				     ( isAbbr(wids[i-1]) || wlens[i-1]==1 ) &&
				     //fix "Institute Inc. All Rights Reserved"
				     // for www.aliconferences.com
				     wids[i-1] != h_inc )
					continue;
				// store #13
				if ( is_wspace_a(w[i][0]) &&
				     w[i][1]== '#' ) 
					continue;
				// apostrophe space is ok (dunkin' donuts)
				if ( w[i][0]=='\''&&
				     is_wspace_a(w[i][1])) 
					continue;
				// otherwise, not
				break;
			}
			if ( wlens[i] == 3 ) {
				// crazy utf8 apostrophe from
				// http://www.earthcare.org/guide_online/
				// 197.html
				if ( wptrs[i][0] == (char)0xe2 &&
				     wptrs[i][1] == (char)0x80 &&
				     wptrs[i][2] == (char)0x99 )
					continue;
			}
			/*
			if ( wlens[i] == 3 ) {
				// "B & B plumbing"
				if ( is_wspace_a(w[i][0])&&w
				     [i][1]=='&'&&
				     is_wspace_a(w[i][2]) )
					continue;
				// otherwise, not
				break;
			}
			*/

			// a string of nothing but \n and ' ' is allowed
			// and i see that in quite a few pages. microsoft
			// front page had this issue as i remember...
			int32_t ampCount = 0;
			int32_t comCount = 0;
			// "Dr. Smith, Obstetrician / Gynecologist"
			int32_t slashCount = 0;
			// period is ok "Moving Co., Inc."
			int32_t kstart   = 0;
			if ( w[i][0]=='.'&&
			     (is_wspace_a(w[i][1]) ||w[i][1]==',') &&
			     i-1>=0 && wids[i-1] &&
			     ( isAbbr(wids[i-1]) || wlens[i-1]==1 ) )
				kstart++;
			// ok now do the loop
			int32_t k ; for ( k = kstart ; k < wlens[i] ; k++ ) {
				// "B & B Plumbing"
				if ( w[i][k] == '&' ) {
					if ( ++ampCount >= 2 ) break;
					if ( comCount   >  0 ) break;
					if ( slashCount >  0 ) break;
					continue;
				}
				if ( w[i][k] == '/' ) {
					if ( ++slashCount >= 2 ) break;
					if ( comCount >      0 ) break;
					if ( ampCount >      0 ) break;
					continue;
				}
				// . this is a good delimeter for place names
				//   usually, but of course if someone has
				//   "Gigablast, \nInc." then this will hurt!
				// . i was only allow inc. or llc. to follow
				//   but what about:
				//   "NM Children, Youth, and Families Dept."

				if ( w[i][k] == ',' && 
				     ( hadAnd ||
				       wids[i+1]==h_inc || 
				       wids[i+1]==h_llc)) {
					if ( ++comCount >= 2 ) break;
					if ( ampCount >    0 ) break;
					if ( slashCount >  0 ) break;
					continue;
				}
				if ( ! is_wspace_a(w[i][k]) ) 
					break;
			}
			// skip if ok
			if ( k == wlens[i] ) continue;
			// nothing else allowed
			break;
		}

		// forget it if too long
		if ( alnumCount >= MAX_ALNUMS_IN_NAME )
			lasti = -1;

		// come back up here after removing the " ... at" substring
	subloop:
		// trim off lower case stop words from the beginning
		for ( ; lasti >= 0 && lasti <= righti ; lasti++ ) {
			// skip if not alnum
			if ( ! wids[lasti] ) continue;
			// assume nuked!
			alnumCount--;
			// is it like "Friday at The Source"?
			if ( lasti+2 <= righti &&
			     //ww->isQueryStopWord(lasti+2) &&
			     s_lc.isInTable( &wids[lasti+2]) &&
			     getDayOfWeek ( wids[lasti] ) >= 1 ) continue;
			// "monday, wednesday and friday at The Source"
			if ( lasti+2 <= righti &&
			     getDayOfWeek ( wids[lasti+2]) >= 1 &&
			     getDayOfWeek ( wids[lasti] )  >= 1 ) continue;
			// . or stopword + day of week is bad too!
			// . "Every Monday at The Source"
			if ( lasti+2 <= righti &&
			     getDayOfWeek ( wids[lasti+2] ) >= 1 &&
			     //( ww->isQueryStopWord(lasti) ||
			     ( s_lc.isInTable(&wids[lasti]) ||
			       wids[lasti] == h_every )  ) continue;
			// assume not nuked
			alnumCount++;
			// stop if not stop word
			//if ( ! ww->isQueryStopWord(lasti) ) break;
			if ( ! s_lc.isInTable(&wids[lasti]) ) break;
			// stop if capitalized
			if ( is_upper_utf8(wptrs[lasti]) &&
			     // trim a capitalized "At" off regardless
			     wids[lasti] != h_at ) break;
			// assume nuked
			alnumCount--;
		}
		// trim off lower case stop words from the end (Wat Center, at)
		for ( ; righti >= lasti && lasti >= 0 ; righti-- ) {
			// skip if not alnum
			if ( ! wids[righti] ) continue;
			// assume nuked
			alnumCount--;
			// . stop if not stop word
			// . no! too strong. was removing "com" in "Cable.com"
			// . "Sonic Drive-In" "Stepping Stones-Drop In"
			//if ( ! ww->isQueryStopWord(righti) ) break;
			if ( wids[righti] == h_at  ) continue;
			//if ( wids[righti] == h_in  ) continue;
			//if ( wids[righti] == h_by  ) continue;
			//if ( wids[righti] == h_and ) continue;
			// not nuked
			alnumCount++;
			// stop it
			break;
		}
		// if we included "at" then trim up until we hit the "at"
		// UNLESS the place name starts with "The".
		// we need to protect "The Lodge at Santa Fe" for instance.
		if ( lasti>= 0 && lasti<=righti && wids[lasti] != h_the &&
		     atPos >= 0 && 
		     // ignore "at" in "at law" (e.g. "attorney at law")
		     // or really any other "at phrase" like that
		     wids[atPos+2] != h_law && 
		     // if a city name is between the "at" and the street,
		     // then assume the "at" is actually part of the place
		     // name!!
		     ! atCityName ) {
			lasti = atPos + 1;
			// pop this back
			alnumCount = atAlnumCount;
			// undo
			atPos = -1;
			// redo filtering
			goto subloop;
		}

		// "All rights reserved". no place name in this case
		if ( alnumCount == 3 && 
		     lasti >= 0 &&
		     i+4<nw &&
		     wids[lasti  ] == h_all &&
		     wids[lasti+2] == h_rights &&
		     wids[lasti+4] == h_reserved ) 
			lasti = -1;

		// "Contact Us". no place name in this case
		if ( alnumCount == 2 && 
		     lasti >= 0 &&
		     i+2<nw &&
		     wids[lasti  ] == h_contact &&
		     wids[lasti+2] == h_us )
			lasti = -1;

		// "[copyrightSign] 2000 Carrier Hotels"
		if ( lasti-1>=0 && 
		     gb_strncasestr(wptrs[lasti-1],wlens[lasti-1],copy))
			lasti = -1;

		// "map of"
		if ( alnumCount == 2 && 
		     lasti>=0 &&
		     wids[lasti] == h_map &&
		     lasti+2<nw &&
		     wids[lasti+2] == h_of ) 
			lasti = -1;
		
		// "map for"
		if ( alnumCount == 2 && 
		     lasti>=0 &&
		     wids[lasti] == h_map &&
		     lasti+2<nw &&
		     wids[lasti+2] == h_for )
			lasti = -1;

			
		// fix "copyright ; 2009 Albuquerque Journal; Abq ; NM"
		// for http://obits.abqjournal.com/obits/2004/04/13
		if ( lasti >= 0 && alnumCount==1 && wids[lasti]==h_copyright) 
			lasti = -1;

		// ends on lower case word with a whitespace before it
		// so as to not hurt "Wendy's" or "citysearch.com"
		if ( lasti >= 0 && 
		     hadUpper && 
		     righti>0 && // fix core...
		     is_wspace_a(wptrs[righti][-1]) &&
		     !is_digit(wptrs[righti][0]) &&
		     !is_upper_utf8(wptrs[righti]) )
			lasti = -1;

		// . we often get zips like "NM 87571" because the previous
		//   place has not official street but has a state/zip thing
		// . fixes guidebookamerica.com
		if ( lasti >= 0 &&
		     alnumCount == 2 &&
		     lasti + 2 < nw &&
		     isStateName (lasti) &&
		     wlens[lasti+2] == 5 &&
		     is_digit(wptrs[lasti+2][0]) )
			lasti = -1;

		// "New Mexico 87109"
		if ( lasti >= 0 &&
		     alnumCount == 3 &&
		     lasti + 4 < nw &&
		     isStateName (lasti) &&
		     wlens[lasti+4] == 5 &&
		     is_digit(wptrs[lasti+2][0]) )
			lasti = -1;

		// now check to see if we should skip this place name and
		// try another before it...
		if ( lasti >= 0 ) {
			// watch out for "Address:" which often preceeds a 
			// street name when address is in a table
			if ( alnumCount == 1 && wids[lasti] == h_address ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_street ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_where ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_location ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_office ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_map ) {
				i = lasti - 1; goto redo; }
			// fix "tel: xxxxxxx 9000 girard"
			if ( alnumCount == 1 && wids[lasti] == h_tel ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_edit ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_email ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_added ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_copy ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_search ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_find ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_go ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_town ) {
				i = lasti - 1; goto redo; }
			if ( alnumCount == 1 && wids[lasti] == h_city ) {
				i = lasti - 1; goto redo; }

			// sometimes "phone:" wedged in there
			if ( alnumCount == 1 && wids[lasti] == h_phone ) {
				i = lasti - 1; goto redo; }

			// "e-mail"
			if ( alnumCount == 2 && 
			     wids[lasti] == h_e &&
			     wids[lasti+2] == h_mail ) {
				i = lasti - 1; goto redo; }

			// "mailing address"
			if ( alnumCount == 2 && 
			     wids[lasti] == h_mailing &&
			     wids[lasti+2] == h_address ) {
				i = lasti - 1; goto redo; }

			// "mail address"
			if ( alnumCount == 2 && 
			     wids[lasti] == h_mail &&
			     wids[lasti+2] == h_address ) {
				i = lasti - 1; goto redo; }

			// "snail mail"
			if ( alnumCount == 2 && 
			     wids[lasti] == h_snail &&
			     wids[lasti+2] == h_mail ) {
				i = lasti - 1; goto redo; }

			// . skip over "33 miles..." or "33 mi..."
			// . Carlsbad Cavern National Park
			//   27 miles S of Carlsbad
			//   3225 National Parks Highway
			if ( alnumCount >= 2 &&
			     is_digit(wptrs[lasti][0]) &&
			     ( wids[lasti+2] == h_mi ||
			       wids[lasti+2] == h_miles ||
			       wids[lasti+2] == h_km ||
			       wids[lasti+2] == h_kilometers ) ) {
				i = lasti - 1; goto redo; }
			
			// skip over "(1 review)" or "(33 reviews)"
			if ( alnumCount == 1 && 
			     ( wids[lasti] == h_review  ||
			       wids[lasti] == h_reviews ) ) {
				// skip number before too!
				if ( lasti-2>=0 && is_digit(wptrs[lasti-2][0]))
					i = lasti - 3;
				else
					i = lasti - 1;
				goto redo;
			}

			// skip over "Write a Review"
			if ( alnumCount == 3 && 
			     wids[lasti] == h_write  &&
			     wids[lasti+2] == h_a  &&
			     wids[lasti+4] == h_review  ) {
				i = lasti - 1;
				// skip back until we hit a tag i guess
				// if we have "Be the first to Write a Review"
				for ( ; i > 0 && ! tids[i] ; i-- );
				goto redo;
			}

			// "Fax: "
			if ( alnumCount >=2 && wids[lasti] == h_fax &&
			     m_words->hasChar(lasti+1,':') ) {
				i = lasti - 1; goto redo; }
			// "Ph: "
			if ( alnumCount >=2 && wids[lasti] == h_ph &&
			     m_words->hasChar(lasti+1,':') ) {
				i = lasti - 1; goto redo; }
			// "Tel: "
			if ( alnumCount >=2 && wids[lasti] == h_tel &&
			     m_words->hasChar(lasti+1,':') ) {
				i = lasti - 1; goto redo; }
			// "Telephone: "
			if ( alnumCount >=2 && wids[lasti] == h_telephone &&
			     m_words->hasChar(lasti+1,':') ) {
				i = lasti - 1; goto redo; }
			// "Street Address:"
			if ( alnumCount ==2 && wids[lasti] == h_street &&
			     wids[lasti+2] == h_address ) {
				i = lasti - 1; goto redo; }
			// "Location Address:"
			if ( alnumCount ==2 && wids[lasti] == h_location &&
			     wids[lasti+2] == h_address ) {
				i = lasti - 1; goto redo; }
			
			// "Add to Favorites"
			if ( alnumCount == 3 && 
			     wids[lasti  ] == h_add &&
			     wids[lasti+2] == h_to &&
			     wids[lasti+4] == h_favorites ) {
				i = lasti - 1; goto redo; }
			
			// "view favorites"
			if ( alnumCount == 2 && 
			     wids[lasti  ] == h_view &&
			     wids[lasti+2] == h_favorites ) {
				i = lasti - 1; goto redo; }
			
			// "more info"
			if ( alnumCount == 2 && 
			     wids[lasti  ] == h_more &&
			     wids[lasti+2] == h_info ) {
				i = lasti - 1; goto redo; }
			
			// "more information"
			if ( alnumCount == 2 && 
			     wids[lasti  ] == h_more &&
			     wids[lasti+2] == h_information ) {
				i = lasti - 1; goto redo; }
			
			// if we just had a sequence of numbers for the place 
			// name then ignore that. usually a phone number. fixes
			// http://local.yahoo.com/NM/Albuquerque/Food+Dining/
			// Restaurants/Food+Delivery+Services
			if ( alphaCount == 0 && alnumCount > 0 ) {
				i = lasti - 1; goto redo; }
		}

		// . if street had upper case words, but we had lower case,
		//   then we are not a good place name!
		// . put this after the redo's so we can redo things like
		//   "map" or "reviews" which may be in lower case
		if ( (street->m_bits & PLF_HAS_UPPER) && hadLower ) {
			//lasti = -1;
			// EXPERIEMENT:
			// skip back to a tag like we do for
			// "Write a Review" skipping logic below
			//i = lasti - 1;
			// skip back until we hit a tag i guess
			// if we have "Be the first to Write a Review"
			for ( ; i > 0 && ! tids[i] ; i-- );
			goto redo;
		}

		// . add the place name if we found something
		// . if we broke out of the loop because of the alnumCount then
		//   that is NOT good because we want something that has a
		//   delimeter on the left!
		if ( lasti >= 0 && lasti<=righti && alphaCount > 0 && 
		     // this is restricted above!
		     //alnumCount <10 && 
		     nn<10 ) { // && m_np<MAX_PLACES ) {
			// point to it
			char *p = wptrs[lasti];
			// length
			int32_t plen = (wptrs[righti]+wlens[righti])-wptrs[lasti];
			// set end
			char *pend = p + plen;
			// end on period if we had it
			if ( *pend == '.' ) pend++;
			// include terminating ')' if any
			int32_t parens = 0;
			// start scan
			for ( char *s = p ; s < pend ; s++ ) {
				if ( *s == '(' ) parens++;
				if ( *s == ')' ) parens--;
			}
			// term it with a ) if we had a (
			if ( parens > 0 ) {
				if      ( *pend == ')' )
					pend += 1;
				else if ( is_wspace_a(*pend) && pend[1]==')')
					pend += 2;
			}
			// re-set length
			plen = pend - p;
			// note it if crazy...
			if ( plen >= 200 ) 
				// note it
				log("addr: got place name of %"INT32" chars int32_t",
				    plen);
			// sanity check
			//if ( m_np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }
			// point to the place name
			Place *pp = (Place *)m_pm.getMem(sizeof(Place));
			if ( ! pp ) return false;
			// set the type
			int32_t ptype = 0;
			if ( pcount == 0 ) ptype = PT_NAME_1;
			if ( pcount == 1 ) ptype = PT_NAME_2;
			if ( ptype  == 0 ) { char *xx=NULL;*xx=0; }
			// set it
			pp->m_a       = lasti;
			pp->m_b       = righti+1;
			pp->m_alnumA  = -1;//alnumCount;
			pp->m_alnumB  = -1;//alnumCount + subcount;
			pp->m_type    = ptype;//PT_NAME;
			pp->m_str     = p;//wptrs[lasti];
			pp->m_strlen  = pend - p;//plen;
			//pp->m_hash    = h;
			//pp->m_adm1[0] = 0;//pd->m_adm1[0];
			//pp->m_adm1[1] = 0;//pd->m_adm1[1];
			//pp->m_crid     = 0;//pd->m_crid;
			pp->m_bits    = 0;//PLF_INFILE;
			pp->m_flags2  = 0;
			// reset hash
			//int64_t h = 0LL;
			// word if of previous word
			//int64_t pi = 0LL;
			// we WERE looping backwards, so we need to
			// compute the hash here
			setHashes ( pp , m_words , m_niceness );
			// if name1/name2 is a city/state or state/city then
			// do not add it
			bool isGood = true;
			// get previous two places, see if city/state
			Place *prev1 = NULL;
			Place *prev2 = NULL;
			int32_t   np = m_pm.getNumPtrs();
			if ( np >= 2 ) {
				prev1 = (Place *)m_pm.getPtr(np-1);
				prev2 = (Place *)m_pm.getPtr(np-2);
			}
			// . fix "Kimo Theater, Albuquerque NM, 423 Central"
			//   for http://www.zvents.com/albuquerque-nm/venues/sh
			//   ow/11865-kimo-theatre
			// . do not allow a city & state to be the two names
			// . sometimes ppl put this before the street
			// . only do this after we have two names (pcount==1)
			if ( pcount == 1 && 
			     np > savednp && // we at least added one to np
			     prev1 &&
			     prev2 &&
			     isCityState3 (prev1->m_hash,prev2->m_hash)==1) {
				// wipe out previous name
				nn = savednn;
				// wipe out prevous place
				//m_np = savednp;
				m_pm.setNumPtrs ( savednp );
				// reset this too!
				pcount = 0;
				// skip over these guys to get real name
				i = lasti - 1;
				// try again
				goto redo;
				// and do not add this one
				//isGood = false;
			}
			// too long is bad
			if ( plen >= 200 )
				isGood = false;
			if ( ! pp->m_hash )
				isGood = false;
			// . if nothing worth hashing, do not add it
			// . only really add if length is somewhat sane!!
			if ( isGood ) {
				// store it
				pname[nn++] = pp;
				// sanity
				//if (m_np>= MAX_PLACES ){char *xx=NULL;*xx=0;}
				// advance it, but not if we only had "the" for
				// the place name!!
				//m_np++;
			}
			/*
			for ( int32_t k = pp->m_a ; k < pp->m_b ; k++ ) {
				// skip if not word
				if ( ! wids[k] ) continue;
				// . do not add the first word if its "The" 
				//   into this
				// . fixes "The Guild Cinema" not matching 
				//   placedb  entries for "Guild Cinema"
				if ( h == 0LL && wids[k] == h_the ) continue;
				// . convert place name word into base word
				// . synonyms
				// . converts 4th to fourth, etc.
				int64_t *hw = getSynonymWord (&wids[k],&pi);
				// set previous id
				pi = wids[k];
				// ignore it if returned 0 (ignore) (school)
				if ( ! *hw ) continue;
				// mix it up
				h <<= 1LL;
				// xor it in
				h ^= *hw; // wids[k];
			}
			// only consumate it if not the single word "the"
			if ( h ) {
				// set it
				pp->m_hash = h;
				// store it
				pname[nn++] = pp;
				// advance it, but not if we only had "the" for
				// the place name!!
				np++;
			}
			*/
			// point to before us!
			i = lasti - 1;
			// try to get another one if we only got one
			if ( ++pcount == 1 ) 
				goto redo;
		}
		// . if no name, beat it. go to the next street we got
		// . no, some events just have a street address and no
		//   place name!
		//else 
		//	continue;
		
	
		///////////////////////
		//
		// END GET THE PLACE NAME before the street
		//
		///////////////////////


		//
		// . if we had multiple streets RIGHT AFTER us, skip over them!
		// . where the "po box 1293" is technically a street
		// . http://www.yelp.com/biz/pizza-9-albuquerque had some too
		// 
		// start looking for city/state here
		Place *xstreet  = (Place *)m_sm.getPtr(X);
		int32_t start      = xstreet->m_b;
		int32_t startAlnum = xstreet->m_alnumB;
		// as = "After Street"
		int32_t as = X + 1; 
		// int16_tcut
		int32_t ns = m_sm.getNumPtrs();
		// scan the streets after street #X
		for ( ; as < ns ; as++ ) {
			// get that
			Place *astreet = (Place *)m_sm.getPtr(as);
			// stop if "as" is a "fake street"
			if ( astreet->m_flags2 & PLF2_IS_NAME ) break;
			// if we are NOT the ending word of prev street, then
			// stop this loop.
			if ( startAlnum != astreet->m_alnumA ) break;
			// assign, and do the next
			startAlnum = astreet->m_alnumB;
			start      = astreet->m_b;
		}
		// use this
		Place *sss = NULL;
		if ( as < ns ) sss = (Place *)m_sm.getPtr(as);
		// stop if "as" is a "fake street"
		if ( as<ns && (sss->m_flags2 & PLF2_IS_NAME)) as=ns;
		// skip over punct
		if ( start < nw && ! wids[start] ) start++;
		// . skip over "in"
		// . inlines "950 Pinetree SE, in Rio Rancho, NM" for
		//   http://www.xeriscapenm.com/xeriscape_gardens.php
		if ( start<nw && wids[start] == h_in ) {
			startAlnum++;
			start += 2;
		}
		// do not scan past this then
		int32_t max = nw;
		if ( as < m_sm.getNumPtrs() ) max = sss->m_a;

		// NO NO we had "124 ST BTWN 5 AVE AND MT MORRIS PARK WEST"
		// for www.nycgovparks.org/facilities/playgrounds and
		// the street was "124 ST BTWN 5 AVE" and the intersection
		// "AVE AND MT MORRIS PARK WEST" intersected with that
		// street and caused this to core!
		// sanity check
		//if ( max <= street->m_b ) { char *xx=NULL;*xx=0; }
		
		//
		// begin parsing out city/adm1/ctry/zip after street name
		//


		/*

		THIS IS THE OLD WAY

		// . start scan at street->m_b
		// . end scan at "max"
		// . end scan after up to 15 alnum words as well
		// . adds into our places[] array we started up above that
		//   includes places from the title
		// . i am expanding from 6 words to 15 because of :
		//   "111 Maple Street SE @ Maple and Central beside "
		//    Knadjian's Oriental Rugs in Albuquerque, New Mexico "
		//    87106. "
		// . and to reduce bleeding into another address i am now
		//   limiting based on the start of the next street, "max"
		np =addProperPlaces(start,max,15,places,MAX_PLACES,np,0,
				    // subtract 1 since it is an OPEN ended
				    // half interval just like [a,b)
				    startAlnum - 1,-1);
		// breach check
		if ( np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }

		// check before the street, too, but stay in the sentence!
		if ( nn >= 1 ) {
			int32_t na = pname[0]->m_a;
			int32_t nb = pname[0]->m_b;
			np=addProperPlaces(na,nb,15,places,
					   MAX_PLACES,np,0,
					   pname[0]->m_alnumA - 1,-1);
		}
		if ( nn >= 2 ) {
			int32_t na = pname[1]->m_a;
			int32_t nb = pname[1]->m_b;
			np=addProperPlaces(na,nb,15,places,
					   MAX_PLACES,np,0,
					   pname[1]->m_alnumA - 1,-1);
		}
		// breach check
		if ( np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }


		///////////////////////////
		//
		// ** "... in Santa Fe 213 Washington Ave."
		//
		///////////////////////////
		
		// now scan the sentence this street is in for any
		// prepositional phrase beginning with the preposition "in"
		// immediately followed by a city or adm1 name.
		// this logic was hurting abqtango.com because our sentence
		// formation was not good enough and we were allowing the
		// many span tags in the sentence to break the sentence into
		// many smaller sentences because we decided span tags should
		// do that by default. so i made the sentence detection logic
		// better so that abqtango.com would keep 213 washington ave
		// in the sentence that had "in Santa Fe" still...
		Section *ss = m_sections->m_sectionPtrs[street->m_a];
		for ( ; ss ; ss = ss->m_parent )
			if ( ss && (ss->m_flags & SEC_SENTENCE) ) break;

		// might not have a sentence if we are CT_JAVASCRIPT content
		// type, sense we avoid sentence setting for those doc types
		int32_t sa = 0;
		int32_t sb = 0;
		// scan the first and last word of the senentce this street
		// is in. MAY ACTUALLY BE OUTSIDE of the "ss" section because
		// of the new logic in Sections::addSentences() which allows
		// us to have sentences that split sections now to deal with
		// aliconference.com, abqtango.com, etc.
		if ( ss ) { sa = ss->m_senta; sb = ss->m_sentb; }
		// init this
		bool hasRequiredPlace = false;
		// set this. does it matter???
		int32_t alnumPos = 0;//ss->m_alnumA - 1;
		bool afterIn = false;
		// scan the sentence
		for ( int32_t i = sa ; i < sb ; i++ ) {
			// skip if not alnum word
			if ( ! m_wids[i] ) continue;
			// count it
			alnumPos++;
			// skip if not "in"
			if ( m_wids[i] == h_in ) {
				afterIn = true;
				continue;
			}
			// skip if not after the word "in"
			if ( ! afterIn ) continue;
			// reset in case we get continued below
			afterIn = false;
			// to avoid "just in case" or "in time" let's
			// require it be capitalized
			if ( ! m_words->isCapitalized(i) ) continue;
			// find the end of it
			int32_t j = i + 1;
			int32_t lastj = j;
			// loop until we hit something lowercase or number
			for ( ; j < sb ; j++ ) {
				// stop on tag
				if ( m_tids[j] ) break;
				// check case
				if ( m_wids[j] ) {
					// if upper that's ok
					if ( ! m_words->isCapitalized(j) &&
					     ! s_lc.isInTable(&m_wids[j]) ) 
						break;
					// save it
					lastj = j;
				}
				// stop on certain punct
				char *p    = wptrs[j];
				char *pend = p + wlens[j];
				for ( ; p < pend ; p++ ) {
					if ( is_wspace_a(*p) ) 
						continue;
					// St. James?
					if ( *p == '.' ) 
						continue;
					break;
				}
				if ( p < pend ) break;
			}
			// save
			int32_t oldnp = np;
			// reset
			np = addProperPlaces(i,i+1,8,places,
					     MAX_PLACES,np,0,
					     alnumPos-1,
					     lastj);
			// set the required bit
			for ( int32_t k = oldnp ; k < np ; k++ )
				// set this bit
				places[k].m_bits |= PLF2_REQUIRED;
			// must contain a required bit?
			if ( np > oldnp ) hasRequiredPlace = true;
			// stop
			break;
		}
		// breach check
		if ( np >= MAX_PLACES ) { char *xx=NULL;*xx=0; }

		//
		// parse up all our accumulated Places into arrays so we can
		// loop over them all and get all the possible combinations
		// of Place types, Place::m_type.
		//
		for ( int32_t i = 0 ; i < np ; i++ ) {
			// get it
			Place *pi = &places[i];
			// sanity check
			if ( ! pi->m_hash ) { char *xx=NULL;*xx=0; }
			// parse it up
			if ( pi->m_type == PT_CITY ) {
				if ( nc >= MAX_CITIES2 ) continue;
				pcity[nc++] = pi;
			}
			if ( pi->m_type == PT_STATE ) {
				if ( na >= MAX_ADM1 ) continue;
				padm1[na++] = pi;
			}
			if ( pi->m_type == PT_ZIP ) {
				if ( nz >= 10 ) continue;
				pzip[nz++] = pi;
			}
			if ( pi->m_type == PT_CTRY ) {
				if ( ny >= 10 ) continue;
				pctry[ny++] = pi;
			}
			// sanity check
			if ( pi && ! pi->m_hash ) { char *xx=NULL;*xx=0; }
		}

		END THE OLD WAY

		*/


		// . the new way is to telescope out from our street section
		//   looking for cities
		// . we note the telescope depth of each city/state/zip place 
		//   we encounter so that we prefer the city topologically
		//   closest to us
		int32_t sa = xstreet->m_a;
		if ( sa < 0 ) { char *xx=NULL;*xx=0; }

		// int16_tcut
		Place *st = xstreet;//&streets[X];

		// are we a street or place name in the title?
		bool streetInTitle = false;
		if ( st->m_a > 0 && sp ) 
			streetInTitle = (sp[st->m_a]->m_flags & SEC_IN_TITLE);

		Section *ss = NULL;
		int32_t senta = -1;
		int32_t sentb = -1;
		if ( m_sections ) {
			ss = m_sections->m_sectionPtrs[street->m_a];
			senta = ss->m_senta;
			sentb = ss->m_sentb;
		}

		int32_t maxZips = nz + 1;
		bool hasRequiredCity  = false;
		bool hasRequiredState = false;

		////////////
		//
		// set pcity[], array of potential cities for this street
		//
		////////////
		for ( int32_t i = 0 ; i < m_npSaved ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get city, state or zip
			Place *p = (Place *)m_pm.getPtr(i);
			// sanity check
			if ( p->m_alnumA < st->m_alnumA && p->m_a > st->m_a ) {
				char *xx=NULL;*xx=0; }

			// skip city if it intersects street
			if ( p->intersects ( xstreet ) ) continue;
			// skip city if it intersects the name too now
			//if(nn>0&&pname[0]&&p->intersects(pname[0])) continue;
			// or name2, to fix omnimedicalsearch.com
			//if(nn>1&&pname[1]&&p->intersects(pname[1])) continue;
			// for zips really, should not be in the suite
			if (psuite[0]&&p->intersects(psuite[0])) continue;
			
			// is it required
			bool isRequired = ( p->m_flags2 & PLF2_REQUIRED );

			// . allow state to come from anywhere in the document
			// . TODO: later add meta description to get
			//         christinesaari.com etc.
			if ( p->m_type == PT_STATE ) {
				// is it in our sentence
				bool inSent = (p->m_a>=senta&&p->m_a<sentb);
				// if in our sentence and required, set this
				if ( inSent &&
				     isRequired &&
				     // fix "in NE Albuquerque" so we do not
				     // think that means nebraska... this
				     // fixes address in local.yahoo.com/NM/
				     // Albuquerque/Food+Dining/Restaurants/
				     // Food+Delivery+Services
				     m_wids[p->m_a]!= h_ne)
					hasRequiredState = true;
				// make the key for deduping
				char key[4];
				key[0] = p->m_adm1[0];
				key[1] = p->m_adm1[1];
				key[2] = 0;
				key[3] = 0;
				// get if already in padm1[] array
				Place **pp = (Place **)dat.getValue ( &key );
				// if it is us already, skip for sure
				if ( pp && *pp == p ) continue;
				// if we are not near street, skip us
				int32_t dist1 = p->m_alnumA - st->m_alnumA;
				int32_t dist2 = p->m_alnumA - st->m_alnumB;
				if ( dist1 < 0 ) dist1 *= -1;
				if ( dist2 < 0 ) dist2 *= -1;
				int32_t mdist = dist1;
				if ( dist2 < mdist ) mdist = dist2;
				if ( mdist > 10 && ! inSent ) continue;
				// sanity
				if ( na >= 80 ) continue;
				// ok, add it in even though this state might
				// already be represented by another word
				// somewhere else in the document
				padm1 [ na++ ] = p;
				// that's it
				continue;
			}

			// . stop if far beyond the street
			// . if in venue tag then m_a will be < 0
			if ( p->m_a >= 0 &&
			     p->m_alnumA > st->m_alnumB + 10 ) 
				continue;

			// is place in title?
			bool inTitle = (p->m_bits & PLF_FROMTITLE);

			// if we are an xml doc they often have multiple
			// <title> tags, one for each element, so do not
			// consider in that case. this was causing trumba.com
			// to miss its city after the address.
			if ( m_contentType == CT_XML ) inTitle = false;

			// skip if before us and not in title
			if ( p->m_a >= 0 &&
			     p->m_a < st->m_a && 
			     // well, allow it to be a few words before us
			     // to fix some addresses that have the city
			     // before the street. like menuism.com
			     // christinesaari.com salsapower.com
			     p->m_alnumB < st->m_alnumA - 5 &&
			     ! inTitle )
				continue;

			// zip is not allowed to be before us ever though
			// even if in title, which is not allowed
			if ( p->m_type == PT_ZIP && 
			     p->m_a >= 0 &&
			     p->m_a < st->m_a )
				continue;

			// only use first zip, no because one zip may be
			// in the title and the other in the body
			if ( p->m_type == PT_ZIP && nz >= MAX_ZIPS ) 
				continue;

			// skip zip codes in the title
			if ( p->m_type == PT_ZIP &&
			     p->m_a >= 0 &&
			     inTitle &&
			     ! streetInTitle )
				continue;

			// skip zip codes in the tag
			if ( p->m_type == PT_ZIP && p->m_a < 0 )
				continue;

			// only allow one zip from what we started with
			if ( p->m_type == PT_ZIP && nz >= maxZips ) 
				continue;

			if ( p->m_type == PT_ZIP  ) {
				pzip [nz++] = p;
				continue;
			}

			// limit to like 5 or so, that is indicative of
			// a list of cities after us...
			if ( nc >= MAX_CITIES )
				continue;

			// this can be a type of PT_NAME since we add tags
			// from a tagrec like 
			// "Albuquerque Center for Peace and Justice;;;202 
			// Harvard Southeast;Albuquerque;nm;87106;;165445..."
			// and that adds its places into m_places[] and
			// incs m_np
			if ( p->m_type != PT_CITY ) continue;

			// add it to place table like how addProperPlaces() did
			if ( p->m_type == PT_CITY ) pcity[nc++] = p;

			// if in our sentence and required, set this
			if ( p->m_a>= senta && p->m_a < sentb && isRequired )
				hasRequiredCity = true;
		}

		// complain
		if ( nn >= 10  ) {
			if ( ! printed ) log("events: name breach");
			printed = true;
			//char *xx=NULL;*xx=0; 
		}
		if ( nc >= MAX_CITIES  ) {
			if ( ! printed ) log("addr: cities breach");
			printed = true;
			// just bail out now to fix the slow parsing of
			// www.soul-patrol.com
			g_errno = EBUFOVERFLOW;
			m_breached = true;
			return false;
			//char *xx=NULL;*xx=0; 
		}
		if ( na >= MAX_ADM1  ) {
			if ( ! printed ) log("events: adm1 breach");
			printed = true;
			//char *xx=NULL;*xx=0; 
		}
		//if ( nc >= MAX_CITIES || nc <= 0 ) {
		//	log("events: city breach");
		//	char *xx=NULL;*xx=0; 
		//}

		// need at least one city or zip to make an address
		if ( nc <= 1 && nz <= 1 ) continue;

		// . PO Boxes do not have names
		// . YES THEY DO!
		// . was picking up "yahoo" as the place name for:
		//   http://www.usadancenm.org/links.html :
		//   "usadancenm@yahoo.com ** P.O. Box 94766, Albuquerque"
		//if ( to_lower_a(street->m_str[0])=='p' ) nn = 0;

		// . allow for a null place name
		// . some events just have a street address with no official
		//   place name
		if ( nn < 2 ) pname[nn++] = NULL;
		if ( nn < 2 ) pname[nn++] = NULL;

		//
		// TODO: filter out places using the hashtable adm1/ctryId algo
		//

		// adjust nc
		//int32_t fakena = na + dc;

		// . now the heavily nested loop (BIG LOOP)
		// . first over addresses to inherit from
		// . default addresses (from tagdb rec - contact info)
		// . TODO: fix this i1 < 2 HACK!
		for ( int32_t i1 = 0 ; i1 < dc && i1 < 2 ; i1++ ) {
		// loop over default address again, but ignore city and
		// just use the adm1 (state).
		// should fix "913 W. Alameda - Santa Fe" which has no state,
		// but "Albuquerque, New Mexico" is in the tag!
		for ( int32_t i1b = 0 ; i1b < 2 /*3*/ ; i1b++ ) {
		// adm1 
		for ( int32_t i2 = 0 ; i2 < na ; i2++ ) {
		// city
		for ( int32_t i3 = 0 ; i3 < nc ; i3++ ) {
		// ctry
		//for ( int32_t i4 = 0 ; i4 < ny ; i4++ ) {
		// zip
		for ( int32_t i5 = 0 ; i5 < nz ; i5++ ) {
		// suite
		for ( int32_t i6 = 0 ; i6 < nu ; i6++ ) {
		// place name
		//for ( int32_t i7 = 0 ; i7 < nn ; i7++ ) {

			// breathe
			QUICKPOLL(m_niceness);

			// we only use i1b for default addresses in da[]
			if ( i1b > 0 && i1 == 0 ) continue;

			// int16_tcuts
			Place   *adm1   =  padm1  [i2];
			//Place *ctry   =  pctry  [i4];
			Place   *zip    =  pzip   [i5];
			Place   *suite  =  psuite [i6];
			Place   *name1  =  pname  [0];
			Place   *name2  =  pname  [1];
			Place   *city   =  pcity  [i3];

			// now if city is out of bounds use the venue address
			if ( i1 > 0 ) {
				// set it
				Address *addr   = &da     [i1];
				// always use venue's state!
				adm1 = addr->m_adm1;
				// 1 means inherit city too!
				if ( i1b == 1 ) 
					city = addr->m_city;
				// don't take the zip!!
				//zip    =  addr->m_zip;
				zip    =  NULL;
			}

			if ( hasRequiredCity ) {
				// skip if no city
				if ( ! city ) continue;
				// skip if city is not "required"
				if ( ! ( city->m_flags2 & PLF2_REQUIRED ) )
					continue;
				// must be in our sentence! this fixes
				// when we had "... in Central New Mexico"
				// in the title, it thought Central was the
				// city. but we had "in Abq" in our sentence.
				// and both cities had this bit set but
				// only Abq should have applied!
				if ( city->m_a <  senta ) continue;
				if ( city->m_a >= sentb ) continue;
			}

			if ( hasRequiredState ) {
				// skip if no state
				if ( ! adm1 ) continue;
				// skip if stateis not "required"
				if ( ! ( adm1->m_flags2 & PLF2_REQUIRED ) )
					continue;
				// see the "city" fix right above
				if ( adm1->m_a <  senta ) continue;
				if ( adm1->m_a >= sentb ) continue;
			}

			// no overlap of adm1 and city
			if ( adm1 && city && 
			     adm1->m_a >= 0 &&
			     adm1->m_a == city->m_a ) continue;

			// if we had a prepositional phrase starting with "in"
			// then we must contain its city/adm1 name if it
			// had one...
			/*
			if ( hasRequiredPlace ) {
				bool gotIt = false;
				if ( city && (city->m_bits & PLF2_REQUIRED ) )
					gotIt = true;
				if ( adm1 && (adm1->m_bits & PLF2_REQUIRED ) )
					gotIt = true;
				if ( ! gotIt ) 
					continue;
			}
			*/

			/*
			// . inherit!
			// . "addr" i think is just the default venue addr now
			if ( i1b == 0 ) {
				// if addr is supplying these, skip if there
				// was a collision.
				if ( addr->m_adm1 && adm1 ) continue;
				if ( addr->m_city && city ) continue;
				//if(addr->m_name.m_str && name ) continue;
				if ( addr->m_adm1 ) adm1 = addr->m_adm1;
				if ( addr->m_city ) city = addr->m_city;
				//if(addr->m_name.m_str ) name = &addr->m_name;
			}
			// . if i1b is 1 then we only inherit adm1!!!
			// . this fixes the bug for 913 W. Alameda described 
			//   above.
			else if ( i1b == 1 ) {
				// if addr is supplying these, skip if there
				// was a collision.
				if ( addr->m_adm1 && adm1 ) continue;
				if ( addr->m_adm1 ) {
					adm1 = addr->m_adm1;
					if(!adm1->m_hash){char *xx=NULL;*xx=0;}
				}
			}
			*/

			// need a city, can be implied by a zip
			if ( ! city && ! zip ) continue;

			// the CF_UNIQUE is too inaccruate for this!!
			//bool hasState = false;
			//if ( adm1 ) hasState = true;
			//if ( zip  ) hasState = true;
			//if ( city && city->m_alnumA == st->m_alnumB &&
			//     city->m_adm1[0] )
			//	hasState = true;
			//if ( ! hasState ) continue;
			// . need a state too, can be implied by a zip
			// . certain unique cities can also imply the state,
			//   like "Albuquerque" or "Washington DC"
			if ( ! adm1 && ! zip ) continue;


		
			// . how to fix "1024 4th St SW in downtown 
			//   Albuquerque" which has no adm1?
			// . get the adm1/state from the city, BUT
			//   only if city is UNIQUE!!!
			/*
			if ( ! adm1 && city->m_bits & PLF_UNIQUE ) {
				tap.m_crid     = city->m_crid;
				tap.m_str     = city->m_adm1;
				tap.m_strlen  = 2;
				tap.m_adm1[0] = city->m_adm1[0];
				tap.m_adm1[1] = city->m_adm1[1];
				adm1 = &tap;
				//continue;
			}
			*/
			// this is required
			//if ( ! adm1 ) continue;

			//if ( ! name ) continue;

			// quickly check adm1 vs. city
			//if ( adm1->m_adm1[0] != city->m_adm1[0] ) continue;
			//if ( adm1->m_adm1[1] != city->m_adm1[1] ) continue;
			//if ( adm1->m_crid     != city->m_crid     ) continue;
			if ( adm1 && city &&
			     !(adm1->m_adm1Bits & city->m_adm1Bits)) 
				continue;

			/*
			// sanity check
			if ( zip && ! zip->m_hash ) { char *xx=NULL;*xx=0; }
			// cancel out bad zips
			if ( zip && adm1 && adm1->m_adm1Bits!=zip->m_adm1Bits)
				zip = NULL;//continue;
			//if ( adm1->m_crid   !=zip->m_crid    )continue;
			// cut the int64_t to a int32_t for this compare
			if ( zip && city && city->m_hash != zip->m_cityHash ) 
				zip = NULL;//continue;
			*/

			/*
			// debug
			Address tmp;
			memset ( &tmp , 0 , sizeof(Address) );
			if ( street ) tmp.m_street = street;
			if ( adm1   ) tmp.m_adm1   = adm1;
			if ( city   ) tmp.m_city   = city;
			//if ( ctry   ) tmp.m_ctry   = ctry;
			if ( zip    ) tmp.m_zip    = zip;
			if ( suite  ) tmp.m_suite  = suite;
			if ( name1   ) tmp.m_name1   = name1;
			if ( name2   ) tmp.m_name2   = name2;
			tmp.print();
			*/

			//if ( street->m_str[0]=='4' && city->m_str[0]=='R' 
			//     && name->m_str[0]=='E' && adm1->m_str[1]=='M' 
			//     && name->m_str[20]=='n' ) {
			//printAddress ( &tmp,NULL,0);
			//log("hey");
			//}


			//log("events: i1=%"INT32" i2=%"INT32" i3=%"INT32" i4=%"INT32" "
			//    "i5=%"INT32" i6=%"INT32" i7=%"INT32"",
			//    i1,i2,i3,i4,i5,i6,i7);

			// clear
			char flags3 = 0;
			// this should be an address flag because we might
			// be using a city/state from another sentence 
			// in which it is required, but it is not for us
			// if we are in a different sentence
			if ( hasRequiredCity )
				flags3 |= AF2_HAS_REQUIRED_CITY;
			if ( hasRequiredState )
				flags3 |= AF2_HAS_REQUIRED_STATE;

			// . now try to add place vec to our array of addresses
			// . we now supply the containing section, "sec"
			//   so we can vote on which tag hash supplied the best
			//   addresses
			if ( ! addAddress ( name1  ,
					    name2  ,
					    suite  ,
					    street ,
					    city   ,
					    adm1   ,
					    zip    ,
					    NULL   , // ctry   ,
					    NULL   ,
					    startAlnum ,
					    flags3 ,
					    NULL ) ) return false;

			//if ( m_breached )
			//	goto bustout;

		} // i1
		} // i1b
		} //adm1
		}
		//} ctry
		} //i5 nz
		}
		// end the BIG LOOP
	}

	// CRAP! this algo was causing many streets to be ignored on
	// http://www.estrelladelnortevineyard.com/SFV_retloc.php
	// because it has like "main st" and "central" in multiple cities!
	// so comment this algo out and try to think of a better way
	/*
	//
	// now if all street names are the same but with a 
	// different city then i would say nuke them! cuz it 
	// can be a list of some kind of statistic per city, 
	// like 
	// Amsterdam Netherlands (114 events) 
	// Anaheim CA United States (249 events)
	// Ann Arbor MI United States (155 events) 
	// Atlanta GA United States (708 events) 
	// on http://events.mapchannels.com/
	//
	//
	// only allow one city to use a streetHash
	HashTableX su; 
	char subuf[2000];
	// set allowDups to true!!!!
	su.set ( 8 , 8 , 0 , subuf , 2000 , true , m_niceness );
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Address *a = (Address *)m_am.getPtr(i);
		// skip if not inlined
		if ( ! ( a->m_flags & AF_INLINED ) ) continue;
		// get street hash
		int64_t sh = a->m_street->m_hash;
		// get city hash
		int64_t ch = a->m_city.m_hash;
		// hash it. return false with g_errno set on error
		if ( ! su.addKey ( &sh , &ch ) ) return false;
	}
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Address *a = (Address *)m_am.getPtr(i);
		// skip if not inlined
		if ( ! ( a->m_flags & AF_INLINED ) ) continue;
		// get street hash
		int64_t sh = a->m_street->m_hash;
		// how many different cities have this same street?
		int32_t slot = su.getSlot ( &sh );
		// reset count
		int32_t count = 0;
		// multiple places might have this hash
		for ( ; slot>=0 ; slot = su.getNextSlot ( slot , &sh ) ) {
			// count it
			count++;
		}
		// if only 1 city had this street name, keep it
		if ( count <= 1 ) continue;
		// otherwise, ignore this address
		a->m_flags &= ~AF_INLINED;
		a->m_flags |=  AF_IGNORE;
	}
	// free mem just in case
	su.reset();
	*/

	// bustout:
	//
	// set the AF_AMBIGUOUS bits of each Address if we should
	//
	setAmbiguousFlags();

	//log("events: combos=%"INT32"",combos);
	//char *xx=NULL;*xx=0;
	//log("events: sleeping 3 seconds. waiting for possible Ctrl-C");
	//sleep(3);

	return true;
}

Place *getZipPlace ( int32_t a , int32_t alnumPos , Words *words ) {
	// must be a number
	if ( ! is_digit(words->m_words[a][0]) ) return NULL;
	// return this if we got one
	static Place p;
	// make hash
	int64_t h = 0 ^ words->m_wordIds[a];
	// check for zip code
	int32_t slot = g_zips.getSlot(&h);
	// skip if not
	if ( slot < 0 ) return NULL;
	// get the place
	ZipDesc *zd =(ZipDesc *)g_zips.getValueFromSlot(slot);
	// set it
	p.m_adm1Bits = zd->m_adm1Bits;
	p.m_adm1[0]  = zd->m_adm1[0];
	p.m_adm1[1]  = zd->m_adm1[1];
	p.m_type     = PT_ZIP;
	p.m_a        = a;
	p.m_b        = a+1;
	p.m_bits     = 0;
	p.m_alnumA   = alnumPos;
	p.m_alnumB   = alnumPos+1;
	p.m_str      = words->m_words[a];
	p.m_strlen   = words->m_wordLens[a];
	p.m_hash     = h;
	p.m_cityHash = zd->m_cityHash;
	p.m_cityStr  = g_cityBuf + zd->m_cityOffset;
	return &p;
}

Place *getCityPlace ( int32_t a , int32_t alnumPos , Words *words ) {
	// return this if we got one
	static Place p;
	// init hash to zero
	int64_t h = 0LL;
	// max count
	int32_t count = 0;
	// record start
	int32_t startAlnumPos = alnumPos;
	// fix this
	alnumPos--;
	// return this
	Place *retp = NULL;
	// for some filtering
	static bool s_flag = false;
	static int64_t h_university;
	static int64_t h_of;
	if ( ! s_flag ) {
		s_flag = true;
		h_university = hash64n("university");
		h_of         = hash64n("of");
	}
	// int16_tcut
	int32_t nw = words->m_numWords;
	int32_t wcount = 0;
	// loop over words in [a,b)
	for ( int32_t k = a ; k < nw ; k++ ) {
		// or 15 words is good enough too!
		if ( ++wcount >= 20 ) break;
		// skip if not alnum
		if ( ! words->isAlnum(k) ) continue;
		// count it
		alnumPos++;
		// only up to 4 words in a city name
		if ( ++count >= 5 ) break;
		// get the hash of potential place name
		int64_t wid = words->m_wordIds[k];
		// int16_tcut
		int32_t  wlen = words->m_wordLens[k];
		char *wptr = words->m_words[k];
		// if it ended in apostrophe s then fix that
		if ( wlen > 2 &&
		     wptr[wlen-2]=='\'' && 
		     to_lower_a(wptr[wlen-1]) == 's' )
			// hash the word without the 's
			wid = hash64Lower_utf8(wptr,wlen-2);
		// mix it up
		h <<= 1;
		// hash it into our ongoing hash
		h ^= wid; // words->m_wordIds[k];
		// might be alias
		//int64_t *ah1 = (int64_t *) g_aliases.getValue(&h); 
		//if ( ah1 ) h = *ah1;
		// ignore "University" if "of" follows
		if ( h == h_university && 
		     k + 2 < nw &&
		     words->m_wordIds[k+2] == h_of )
			continue;
		// get it
		CityDesc *cd = (CityDesc *)g_cities.getValue(&h);
		if ( ! cd ) continue;
		// check for "county" (santa fe county is not a city name)
		if ( k + 2 < nw && words->m_wordIds[k+2] == h_county ) 
			return NULL;
		// int16_tcuts
		char **wptrs = words->getWords();
		int32_t  *wlens = words->getWordLens();
		// set the place
		p.m_adm1Bits  = cd->m_adm1Bits;
		p.m_type      = PT_CITY;
		p.m_a         = a;
		p.m_b         = k+1;
		p.m_alnumA    = startAlnumPos;
		p.m_alnumB    = alnumPos+1;
		p.m_str       = wptrs[a];
		p.m_strlen    = wptrs[k]+wlens[k]-wptrs[a];
		p.m_hash      = h;
		p.m_cityHash  = h;
		p.m_bits      = 0;
		/*
		// if city is unique, set its adm1Hash
		if ( p.m_adm1Bits & CF_UNIQUE ) {
			// get it
			char *ap = getStateAbbr ( p.m_adm1Bits );
			// set it
			p.m_adm1[0] = ap[0];
			p.m_adm1[1] = ap[1];
		}
		else {
			p.m_adm1[0] = 0;
			p.m_adm1[1] = 0;
		}
		*/
		// note it
		retp = &p;
		// see if we can beat it though
	}
	return retp;
}

Place *getStatePlace ( int32_t a , int32_t alnumPos , Words *words ) {
	// return this if we got one
	static Place p;
	// init hash to zero
	int64_t h = 0LL;
	// max count
	int32_t count = 0;
	// record start
	int32_t startAlnumPos = alnumPos;
	// fix this
	alnumPos--;
	// int16_tcut
	int32_t nw = words->getNumWords();
	// loop over words in [a,b)
	for ( int32_t k = a ; k < nw ; k++ ) {
		// skip if not alnum
		if ( ! words->isAlnum(k) ) continue;
		// count it
		alnumPos++;
		// only up to 3 words "district of columbia"
		if ( ++count >= 4 ) break;
		// get the hash of potential place name
		int64_t wid = words->m_wordIds[k];
		// int16_tcut
		int32_t  wlen = words->m_wordLens[k];
		char *wptr = words->m_words[k];
		// if it ended in apostrophe s then fix that
		if ( wlen > 2 &&
		     wptr[wlen-2]=='\'' && 
		     to_lower_a(wptr[wlen-1]) == 's' )
			// hash the word without the 's
			wid = hash64Lower_utf8(wptr,wlen-2);
		// mix it up
		h <<= 1;
		// hash it into our ongoing hash
		h ^= wid; // words->m_wordIds[k];
		// get it
		int32_t pos = getStateOffset ( &h );
		// skip if not a state
		if ( pos < 0 ) continue;
		// int16_tcuts
		char **wptrs = words->getWords();
		int32_t  *wlens = words->getWordLens();
		// otherwise, set it
		int64_t stateBit = 1LL << pos;
		p.m_adm1Bits = stateBit;
		p.m_type     = PT_STATE;
		p.m_a        = a;
		p.m_b        = k+1;
		p.m_alnumA   = startAlnumPos;
		p.m_alnumB   = alnumPos+1;
		p.m_str      = wptrs[a];
		p.m_strlen   = wptrs[k]+wlens[k]-wptrs[a];
		// set adm1 code
		StateDesc *sd = &s_states[pos];
		p.m_adm1[0]  = sd->m_adm1[0];
		p.m_adm1[1]  = sd->m_adm1[1];
		p.m_hash     = p.m_adm1Bits;;
		p.m_bits     = 0;
		return &p;
	}
	return NULL;
}

// . returns -1 and sets g_errno on error
// . returns false if not city/state combo, true otherwise
int32_t Addresses::isCityState3 ( int64_t h1 , int64_t h2 ) {

	int64_t nh1 = h1;
	int64_t nh2 = h2;

	// we now put the aliases into g_cities as if they were their own
	// cities!
	// convert aliases -- only for cities methinks
	//int64_t *ah1 = (int64_t *) g_aliases.getValue(&h1);
	//if ( ah1 ) nh1 = *ah1;
	//int64_t *ah2 = (int64_t *) g_aliases.getValue(&h2);
	//if ( ah2 ) nh2 = *ah2;

	// get the places
	bool c1 = g_cities.isInTable ( &nh1 );
	bool c2 = g_states.isInTable ( & h1 );
	if ( ! c1 && ! c2 ) return false;

	bool d1 = g_cities.isInTable ( &nh2 );
	bool d2 = g_states.isInTable ( & h2 );
	if ( ! d1 && ! d2 ) return false;

	// "Coutrnyside Mobile Home Park" is a PPL (popluated place) in MN
	// so we assume it to be a city. then it is mentioned on the new mexico
	// page http://www.thecityofalbuquerque.com/mobilehome/ in new mexico.
	// so make sure the city is in that state i guess...
	if ( d1 && c2 ) {
		CityDesc *cd = (CityDesc *)g_cities.getValue(&nh2);
		uint64_t sb = getStateBitFromHash ( &h1 );
		if ( ! ( (cd->m_adm1Bits) & sb ) ) { d1 = false; c2 = false; }
	}

	if ( d2 && c1 ) {
		CityDesc *cd = (CityDesc *)g_cities.getValue(&nh1);
		uint64_t sb = getStateBitFromHash ( &h2 );
		if ( ! ( (cd->m_adm1Bits) & sb ) ) { d2 = false; c1 = false; }
	}

	if ( c1 && d2 ) return true;
	if ( c2 && d1 ) return true;
	return false;
}

// words range is [a,b)
bool Addresses::isCityName ( int32_t a , int32_t b ) {
	// init hash to zero
	int64_t h = 0LL;
	// loop over words in [a,b)
	for ( int32_t k = a ; k < b ; k++ ) {
		// skip if not alnum
		if ( ! m_words->isAlnum(k) ) continue;
		// mix it up
		h <<= 1;
		// hash it into our ongoing hash
		h ^= m_wids[k];
	}
	// might be alias
	//int64_t *ah1 = (int64_t *) g_aliases.getValue(&h);	
	//if ( ah1 ) h = *ah1;
	// get it
	return g_cities.isInTable(&h);
}

// words range is [a,b)
bool Addresses::isStateName ( int32_t a ) {
	// init hash to zero
	int64_t h = 0LL;
	// max count
	int32_t count = 0;
	// loop over words in [a,b)
	for ( int32_t k = a ; k < m_nw ; k++ ) {
		// skip if not alnum
		if ( ! m_words->isAlnum(k) ) continue;
		// only up to "district of columbia"
		if ( ++count >= 4 ) break;
		// mix it up
		h <<= 1;
		// hash it into our ongoing hash
		h ^= m_wids[k];
		// get it
		if ( g_states.isInTable(&h) ) return true;
	}
	return false;
}

// . words range is [a,b)
// . used by Events.cpp to demote title score
bool Addresses::isCityState ( Section *si ) {

	// skip if too many words
	int32_t na = si->m_lastWordPos - si->m_firstWordPos;
	if ( na <= 0    ) return false;
	if ( na >= 2*10 ) return false;

	int32_t a = si->m_a;
	int32_t b = si->m_lastWordPos + 1;

	int32_t lastb = isCityState2 ( a , b );

	if ( lastb <= 0 ) return false;
	if ( lastb == si->m_lastWordPos ) return true;
	return false;
}

// . returns -1 and sets g_errno on error
// . returns 0 or 1 otherwise
int32_t Addresses::cityAdm1Follows ( int32_t a ) {
	// returns -1 if does not follow
	if ( isCityState2 ( a , m_nw ) < 0 ) return 0;
	// it did follow
	return 1;
}

int32_t Addresses::isCityState2 ( int32_t a , int32_t b ) {

	// m must lie on a punt word or tag
	for ( ; a < b ; a++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop on wid
		if ( m_wids[a] ) break;
	}
	// bail if no wid
	if ( a >= b ) return -1;

	Place *cp = getCityPlace ( a , 0 , m_words );
	if ( ! cp ) return -1;
	// point to start of state
	int32_t sta = cp->m_b;
	for ( ; sta < b ; sta++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// need a wid
		if ( m_wids[sta] ) break;
	}
	// bail if no room
	if ( sta >= b ) return -1;
	// otherwise, see if its a state
	Place *sp = getStatePlace ( sta , cp->m_alnumB , m_words );
	// skip if not
	if ( ! sp ) return -1;
	// now we make sure city supports state
	if ( ! ( sp->m_adm1Bits & cp->m_adm1Bits ) ) return -1;
	// return last word we match otherwise
	return sp->m_b - 1;
}

void Addresses::setAmbiguousFlags ( ) {

	// clear those flags first
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		Address *ai = (Address *)m_am.getPtr(i);
		ai->m_flags &= ~AF_AMBIGUOUS;
	}

	// . loop over the addresses we got
	// . determine which addresses we want to add to placedb and namedb
	// . placedb key is based on street address, city, adm1,crid(ctry),name
	// . namedb key is based on name, city, adm1, crid
	// . only add in addresses that are definitive
	// . must have zip code, must not have another address with the same
	//   street address
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() - 1 ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Address *a = (Address *)m_am.getPtr(i);
		// do not do fake street names
		if ( a->m_street->m_a < 0 ) continue;
		// reset verified counts
		int32_t verified1 = 0;
		int32_t verified2 = 0;
		int32_t verified3 = 0;
		int32_t verified4 = 0;
		// count dups, addresses using the same street
		int32_t dups = 0;
		// do we have other verified addresses using this street?
		for ( int32_t j = i ; j < m_am.getNumPtrs() ; j++ ) {
			// get one before us
			Address *b = (Address *)m_am.getPtr(j);
			// stop when street is different
			if ( b->m_street->m_a != a->m_street->m_a ) break;
			// count dups
			dups++;
			// is "b" verified?
			if ( b->m_flags & AF_VERIFIED_STREET       ) 
				verified1++;
			if ( b->m_flags & AF_VERIFIED_STREET_NUM   ) 
				verified2++;
			if ( b->m_flags & AF_VERIFIED_PLACE_NAME_1 ) 
				verified3++;
			if ( b->m_flags & AF_VERIFIED_PLACE_NAME_2 ) 
				verified4++;
		}

		// loop over all the dups
		for ( int32_t j = i ; dups >= 2 && j < m_am.getNumPtrs() ; j++ ) {
			// get one before us
			Address *b = (Address *)m_am.getPtr(j);
			// stop when street is different
			if ( b->m_street->m_a != a->m_street->m_a ) break;
			// if we are the only verified, we are not ambiguous
			if((b->m_flags&AF_VERIFIED_STREET    )&&verified1==1)
				continue;
			if((b->m_flags&AF_VERIFIED_STREET_NUM)&&verified2==1)
				continue;
			if((b->m_flags&AF_VERIFIED_PLACE_NAME_1)&&verified3==1)
				continue;
			if((b->m_flags&AF_VERIFIED_PLACE_NAME_2)&&verified4==1)
				continue;
			// otherwise, we are!
			b->m_flags |= AF_AMBIGUOUS;
			// this now too only if some street made it through
			if ( verified2 ) b->m_flags3 |= AF2_BADCITYSTATE;
		}
	}
}

class SynTwin {
public:
	char *m_s1;
	char *m_s2;
};

// map the place name synonyms here
static SynTwin s_synList[] = {
	{"1st","first"}
	,{"2nd","second"}
	,{"3rd","third"}
	,{"4th","fourth"} // North 4th Arts Center, Abq NM
	,{"5th","fifth"}
	,{"6th","sixth"}
	,{"7th","seventh"}
	,{"8th","eighth"}
	,{"9th","ninth"}
	,{"10th","tenth"}
	,{"11th","eleventh"}
	,{"12th","twelfth"}

	,{"theatre","theater"} // Kimo Theatre

	,{"n","north"}
	,{"s","south"}
	,{"e","east"}
	,{"w","west"}

	,{"ne","northeast"}
	,{"se","southeast"}

	,{"nw","northwest"}
	,{"sw","southwest"}

	// smith elementary should equal smith elementary school
	//,{"school",""}

	//
	// how about road stuff
	//
	// from http://www.usps.com/ncsc/lookups/usps_abbreviations.html
	//
	// cat usps_abbreviations.html | grep -v "*"  | grep -v "back to" | awk '{print ",{\""$2"\",\""$1"\"}"}' > foo
	// cat usps_abbreviations.html | grep -v "*"  | grep -v "back to" | awk '{print ",{\""$3"\",\""$1"\"}"}' >> foo
	// cat foo | sort | uniq >> Address.cpp
	,{"ALLEE","ALLEY"}
	,{"ALLEY","ALLEY"}
	,{"ALLY","ALLEY"}
	,{"ALY","ALLEY"}
	,{"ANEX","ANNEX"}
	,{"ANNEX","ANNEX"}
	,{"ANNX","ANNEX"}
	,{"ANX","ANNEX"}
	,{"ARCADE","ARCADE"}
	,{"ARC","ARCADE"}
	,{"AV","AVENUE"}
	,{"AVE","AVENUE"}
	,{"AVEN","AVENUE"}
	,{"AVENU","AVENUE"}
	,{"AVENUE","AVENUE"}
	,{"AVN","AVENUE"}
	,{"AVNUE","AVENUE"}
	,{"BAYOO","BAYOO"}
	,{"BAYOU","BAYOO"}
	,{"BCH","BEACH"}
	,{"BEACH","BEACH"}
	,{"BEND","BEND"}
	,{"BG","BURG"}
	,{"BGS","BURGS"}
	,{"BLF","BLUFF"}
	,{"BLFS","BLUFFS"}
	,{"BLUF","BLUFF"}
	,{"BLUFF","BLUFF"}
	,{"BLUFFS","BLUFFS"}
	,{"BLVD","BOULEVARD"}
	,{"BND","BEND"}
	,{"BOT","BOTTOM"}
	,{"BOTTM","BOTTOM"}
	,{"BOTTOM","BOTTOM"}
	,{"BOUL","BOULEVARD"}
	,{"BOULEVARD","BOULEVARD"}
	,{"BOULV","BOULEVARD"}
	,{"BRANCH","BRANCH"}
	,{"BR","BRANCH"}
	,{"BRDGE","BRIDGE"}
	,{"BRG","BRIDGE"}
	,{"BRIDGE","BRIDGE"}
	,{"BRK","BROOK"}
	,{"BRKS","BROOKS"}
	,{"BRNCH","BRANCH"}
	,{"BROOK","BROOK"}
	,{"BROOKS","BROOKS"}
	,{"BTM","BOTTOM"}
	,{"BURG","BURG"}
	,{"BURGS","BURGS"}
	,{"BYPA","BYPASS"}
	,{"BYPAS","BYPASS"}
	,{"BYPASS","BYPASS"}
	,{"BYP","BYPASS"}
	,{"BYPS","BYPASS"}
	,{"BYU","BAYOO"}
	,{"CAMP","CAMP"}
	,{"CANYN","CANYON"}
	,{"CANYON","CANYON"}
	,{"CAPE","CAPE"}
	,{"CAUSEWAY","CAUSEWAY"}
	,{"CAUSWAY","CAUSEWAY"}
	,{"CEN","CENTER"}
	,{"CENT","CENTER"}
	,{"CENTER","CENTER"}
	,{"CENTERS","CENTERS"}
	,{"CENTR","CENTER"}
	,{"CENTRE","CENTER"}
	,{"CIRC","CIRCLE"}
	,{"CIR","CIRCLE"}
	,{"CIRCL","CIRCLE"}
	,{"CIRCLE","CIRCLE"}
	,{"CIRCLES","CIRCLES"}
	,{"CIRS","CIRCLES"}
	,{"CLB","CLUB"}
	,{"CLF","CLIFF"}
	,{"CLFS","CLIFFS"}
	,{"CLIFF","CLIFF"}
	,{"CLIFFS","CLIFFS"}
	,{"CLUB","CLUB"}
	,{"CMN","COMMON"}
	,{"CMNS","COMMONS"}
	,{"CMP","CAMP"}
	,{"CNTER","CENTER"}
	,{"CNTR","CENTER"}
	,{"CNYN","CANYON"}
	,{"COMMON","COMMON"}
	,{"COMMONS","COMMONS"}
	,{"COR","CORNER"}
	,{"CORNER","CORNER"}
	,{"CORNERS","CORNERS"}
	,{"CORS","CORNERS"}
	,{"COURSE","COURSE"}
	,{"COURT","COURT"}
	,{"COURTS","COURTS"}
	,{"COVE","COVE"}
	,{"COVES","COVES"}
	,{"CP","CAMP"}
	,{"CPE","CAPE"}
	,{"CRCL","CIRCLE"}
	,{"CRCLE","CIRCLE"}
	,{"CREEK","CREEK"}
	,{"CRESCENT","CRESCENT"}
	,{"CRES","CRESCENT"}
	,{"CREST","CREST"}
	,{"CRK","CREEK"}
	,{"CROSSING","CROSSING"}
	,{"CROSSROAD","CROSSROAD"}
	,{"CROSSROADS","CROSSROADS"}
	,{"CRSE","COURSE"}
	,{"CRSENT","CRESCENT"}
	,{"CRSNT","CRESCENT"}
	,{"CRSSING","CROSSING"}
	,{"CRSSNG","CROSSING"}
	,{"CRST","CREST"}
	,{"CSWY","CAUSEWAY"}
	,{"CT","COURT"}
	,{"CTR","CENTER"}
	,{"CTRS","CENTERS"}
	,{"CTS","COURTS"}
	,{"CURV","CURVE"}
	,{"CURVE","CURVE"}
	,{"CV","COVE"}
	,{"CVS","COVES"}
	,{"CYN","CANYON"}
	,{"DALE","DALE"}
	,{"DAM","DAM"}
	,{"DIV","DIVIDE"}
	,{"DIVIDE","DIVIDE"}
	,{"DL","DALE"}
	,{"DM","DAM"}
	,{"DR","DRIVE"}
	,{"DRIV","DRIVE"}
	,{"DRIVE","DRIVE"}
	,{"DRIVES","DRIVES"}
	,{"DRS","DRIVES"}
	,{"DRV","DRIVE"}
	,{"DVD","DIVIDE"}
	,{"DV","DIVIDE"}
	,{"ESTATE","ESTATE"}
	,{"ESTATES","ESTATES"}
	,{"EST","ESTATE"}
	,{"ESTS","ESTATES"}
	,{"EXP","EXPRESSWAY"}
	,{"EXPRESS","EXPRESSWAY"}
	,{"EXPRESSWAY","EXPRESSWAY"}
	,{"EXPR","EXPRESSWAY"}
	,{"EXPW","EXPRESSWAY"}
	,{"EXPY","EXPRESSWAY"}
	,{"EXTENSION","EXTENSION"}
	,{"EXT","EXTENSION"}
	,{"EXTN","EXTENSION"}
	,{"EXTNSN","EXTENSION"}
	,{"EXTS","EXTENSIONS"}
	,{"FALL","FALL"}
	,{"FALLS","FALLS"}
	,{"FERRY","FERRY"}
	,{"FIELD","FIELD"}
	,{"FIELDS","FIELDS"}
	,{"FLAT","FLAT"}
	,{"FLATS","FLATS"}
	,{"FLD","FIELD"}
	,{"FLDS","FIELDS"}
	,{"FLS","FALLS"}
	,{"FLT","FLAT"}
	,{"FLTS","FLATS"}
	,{"FORD","FORD"}
	,{"FORDS","FORDS"}
	,{"FOREST","FOREST"}
	,{"FORESTS","FOREST"}
	,{"FORGE","FORGE"}
	,{"FORGES","FORGES"}
	,{"FORG","FORGE"}
	,{"FORK","FORK"}
	,{"FORKS","FORKS"}
	,{"FORT","FORT"}
	,{"FRD","FORD"}
	,{"FRDS","FORDS"}
	,{"FREEWAY","FREEWAY"}
	,{"FREEWY","FREEWAY"}
	,{"FRG","FORGE"}
	,{"FRGS","FORGES"}
	,{"FRK","FORK"}
	,{"FRKS","FORKS"}
	,{"FRRY","FERRY"}
	,{"FRST","FOREST"}
	,{"FRT","FORT"}
	,{"FRWAY","FREEWAY"}
	,{"FRWY","FREEWAY"}
	,{"FRY","FERRY"}
	,{"FT","FORT"}
	,{"FWY","FREEWAY"}
	,{"GARDEN","GARDEN"}
	,{"GARDENS","GARDENS"}
	,{"GARDN","GARDEN"}
	,{"GATEWAY","GATEWAY"}
	,{"GATEWY","GATEWAY"}
	,{"GATWAY","GATEWAY"}
	,{"GDN","GARDEN"}
	,{"GDNS","GARDENS"}
	,{"GLEN","GLEN"}
	,{"GLENS","GLENS"}
	,{"GLN","GLEN"}
	,{"GLNS","GLENS"}
	,{"GRDEN","GARDEN"}
	,{"GRDN","GARDEN"}
	,{"GRDNS","GARDENS"}
	,{"GREEN","GREEN"}
	,{"GREENS","GREENS"}
	,{"GRN","GREEN"}
	,{"GRNS","GREENS"}
	,{"GROVE","GROVE"}
	,{"GROVES","GROVES"}
	,{"GROV","GROVE"}
	,{"GRV","GROVE"}
	,{"GRVS","GROVES"}
	,{"GTWAY","GATEWAY"}
	,{"GTWY","GATEWAY"}
	,{"HARB","HARBOR"}
	,{"HARBOR","HARBOR"}
	,{"HARBORS","HARBORS"}
	,{"HARBR","HARBOR"}
	,{"HAVEN","HAVEN"}
	,{"HBR","HARBOR"}
	,{"HBRS","HARBORS"}
	,{"HIGHWAY","HIGHWAY"}
	,{"HIGHWY","HIGHWAY"}
	,{"HILL","HILL"}
	,{"HILLS","HILLS"}
	,{"HIWAY","HIGHWAY"}
	,{"HIWY","HIGHWAY"}
	,{"HL","HILL"}
	,{"HLLW","HOLLOW"}
	,{"HLS","HILLS"}
	,{"HOLLOW","HOLLOW"}
	,{"HOLLOWS","HOLLOW"}
	,{"HOLW","HOLLOW"}
	,{"HOLWS","HOLLOW"}
	,{"HRBOR","HARBOR"}
	,{"HT","HEIGHTS"}
	,{"HTS","HEIGHTS"}
	,{"HVN","HAVEN"}
	,{"HWAY","HIGHWAY"}
	,{"HWY","HIGHWAY"}
	,{"INLT","INLET"}
	,{"IS","ISLAND"}
	,{"ISLAND","ISLAND"}
	,{"ISLANDS","ISLANDS"}
	,{"ISLE","ISLE"}
	,{"ISLES","ISLE"}
	,{"ISLND","ISLAND"}
	,{"ISLNDS","ISLANDS"}
	,{"ISS","ISLANDS"}
	,{"JCTION","JUNCTION"}
	,{"JCT","JUNCTION"}
	,{"JCTN","JUNCTION"}
	,{"JCTNS","JUNCTIONS"}
	,{"JCTS","JUNCTIONS"}
	,{"JUNCTION","JUNCTION"}
	,{"JUNCTIONS","JUNCTIONS"}
	,{"JUNCTN","JUNCTION"}
	,{"JUNCTON","JUNCTION"}
	,{"KEY","KEY"}
	,{"KEYS","KEYS"}
	,{"KNL","KNOLL"}
	,{"KNLS","KNOLLS"}
	,{"KNOL","KNOLL"}
	,{"KNOLL","KNOLL"}
	,{"KNOLLS","KNOLLS"}
	,{"KY","KEY"}
	,{"KYS","KEYS"}
	,{"LAKE","LAKE"}
	,{"LAKES","LAKES"}
	,{"LANDING","LANDING"}
	,{"LAND","LAND"}
	,{"LANE","LANE"}
	,{"LCK","LOCK"}
	,{"LCKS","LOCKS"}
	,{"LDGE","LODGE"}
	,{"LDG","LODGE"}
	,{"LF","LOAF"}
	,{"LGT","LIGHT"}
	,{"LGTS","LIGHTS"}
	,{"LIGHT","LIGHT"}
	,{"LIGHTS","LIGHTS"}
	,{"LK","LAKE"}
	,{"LKS","LAKES"}
	,{"LNDG","LANDING"}
	,{"LNDNG","LANDING"}
	,{"LN","LANE"}
	,{"LOAF","LOAF"}
	,{"LOCK","LOCK"}
	,{"LOCKS","LOCKS"}
	,{"LODGE","LODGE"}
	,{"LODG","LODGE"}
	,{"LOOP","LOOP"}
	,{"LOOPS","LOOP"}
	,{"MALL","MALL"}
	,{"MANOR","MANOR"}
	,{"MANORS","MANORS"}
	,{"MDW","MEADOW"}
	,{"MDW","MEADOWS"}
	,{"MDWS","MEADOWS"}
	,{"MEADOW","MEADOW"}
	,{"MEADOWS","MEADOWS"}
	,{"MEDOWS","MEADOWS"}
	,{"MEWS","MEWS"}
	,{"MILL","MILL"}
	,{"MILLS","MILLS"}
	,{"MISSN","MISSION"}
	,{"ML","MILL"}
	,{"MLS","MILLS"}
	,{"MNR","MANOR"}
	,{"MNRS","MANORS"}
	,{"MNTAIN","MOUNTAIN"}
	,{"MNT","MOUNT"}
	,{"MNTN","MOUNTAIN"}
	,{"MNTNS","MOUNTAINS"}
	,{"MOTORWAY","MOTORWAY"}
	,{"MOUNTAIN","MOUNTAIN"}
	,{"MOUNTAINS","MOUNTAINS"}
	,{"MOUNTIN","MOUNTAIN"}
	,{"MOUNT","MOUNT"}
	,{"MSN","MISSION"}
	,{"MSSN","MISSION"}
	,{"MTIN","MOUNTAIN"}
	,{"MT","MOUNT"}
	,{"MTN","MOUNTAIN"}
	,{"MTNS","MOUNTAINS"}
	,{"MTWY","MOTORWAY"}
	,{"NCK","NECK"}
	,{"NECK","NECK"}
	,{"OPAS","OVERPASS"}
	,{"ORCHARD","ORCHARD"}
	,{"ORCH","ORCHARD"}
	,{"ORCHRD","ORCHARD"}
	,{"OVAL","OVAL"}
	,{"OVERPASS","OVERPASS"}
	,{"OVL","OVAL"}
	,{"PARK","PARK"}
	,{"PARK","PARKS"}
	,{"PARKS","PARKS"}
	,{"PARKWAY","PARKWAY"}
	,{"PARKWAYS","PARKWAYS"}
	,{"PARKWY","PARKWAY"}
	,{"PASSAGE","PASSAGE"}
	,{"PASS","PASS"}
	,{"PATH","PATH"}
	,{"PATHS","PATH"}
	,{"PIKE","PIKE"}
	,{"PIKES","PIKE"}
	,{"PINE","PINE"}
	,{"PINES","PINES"}
	,{"PKWAY","PARKWAY"}
	,{"PKWY","PARKWAY"}
	,{"PKWY","PARKWAYS"}
	,{"PKWYS","PARKWAYS"}
	,{"PKY","PARKWAY"}
	,{"PLAIN","PLAIN"}
	,{"PLAINS","PLAINS"}
	,{"PLAZA","PLAZA"}
	,{"PLN","PLAIN"}
	,{"PLNS","PLAINS"}
	,{"PL","PLACE"}
	,{"PLZA","PLAZA"}
	,{"PLZ","PLAZA"}
	,{"PNE","PINE"}
	,{"PNES","PINES"}
	,{"POINT","POINT"}
	,{"POINTS","POINTS"}
	,{"PORT","PORT"}
	,{"PORTS","PORTS"}
	,{"PRAIRIE","PRAIRIE"}
	,{"PRK","PARK"}
	,{"PR","PRAIRIE"}
	,{"PRR","PRAIRIE"}
	,{"PRT","PORT"}
	,{"PRTS","PORTS"}
	,{"PSGE","PASSAGE"}
	,{"PT","POINT"}
	,{"PTS","POINTS"}
	,{"RADIAL","RADIAL"}
	,{"RADIEL","RADIAL"}
	,{"RADL","RADIAL"}
	,{"RAD","RADIAL"}
	,{"RAMP","RAMP"}
	,{"RANCHES","RANCH"}
	,{"RANCH","RANCH"}
	,{"RAPID","RAPID"}
	,{"RAPIDS","RAPIDS"}
	,{"RDGE","RIDGE"}
	,{"RDG","RIDGE"}
	,{"RDGS","RIDGES"}
	,{"RD","ROAD"}
	,{"RDS","ROADS"}
	,{"REST","REST"}
	,{"RIDGE","RIDGE"}
	,{"RIDGES","RIDGES"}
	,{"RIVER","RIVER"}
	,{"RIV","RIVER"}
	,{"RIVR","RIVER"}
	,{"RNCH","RANCH"}
	,{"RNCHS","RANCH"}
	,{"ROAD","ROAD"}
	,{"ROADS","ROADS"}
	,{"ROUTE","ROUTE"}
	,{"ROW","ROW"}
	,{"RPD","RAPID"}
	,{"RPDS","RAPIDS"}
	,{"RST","REST"}
	,{"RTE","ROUTE"}
	,{"RUE","RUE"}
	,{"RUN","RUN"}
	,{"RVR","RIVER"}
	,{"SHL","SHOAL"}
	,{"SHLS","SHOALS"}
	,{"SHOAL","SHOAL"}
	,{"SHOALS","SHOALS"}
	,{"SHOAR","SHORE"}
	,{"SHOARS","SHORES"}
	,{"SHORE","SHORE"}
	,{"SHORES","SHORES"}
	,{"SHR","SHORE"}
	,{"SHRS","SHORES"}
	,{"SKWY","SKYWAY"}
	,{"SKYWAY","SKYWAY"}
	,{"SMT","SUMMIT"}
	,{"SPG","SPRING"}
	,{"SPGS","SPRINGS"}
	,{"SPNG","SPRING"}
	,{"SPNGS","SPRINGS"}
	,{"SPRING","SPRING"}
	,{"SPRINGS","SPRINGS"}
	,{"SPRNG","SPRING"}
	,{"SPRNGS","SPRINGS"}
	,{"SPUR","SPUR"}
	,{"SPUR","SPURS"}
	,{"SPURS","SPURS"}
	,{"SQRE","SQUARE"}
	,{"SQR","SQUARE"}
	,{"SQRS","SQUARES"}
	,{"SQ","SQUARE"}
	,{"SQS","SQUARES"}
	,{"SQUARE","SQUARE"}
	,{"SQUARES","SQUARES"}
	,{"SQU","SQUARE"}
	,{"STA","STATION"}
	,{"STATION","STATION"}
	,{"STATN","STATION"}
	,{"STN","STATION"}
	,{"STRA","STRAVENUE"}
	,{"STRAVEN","STRAVENUE"}
	,{"STRAVENUE","STRAVENUE"}
	,{"STRAVN","STRAVENUE"}
	,{"STRAV","STRAVENUE"}
	,{"STREAM","STREAM"}
	,{"STREETS","STREETS"}
	,{"STREET","STREET"}
	,{"STREME","STREAM"}
	,{"STRM","STREAM"}
	,{"STR","STREET"}
	,{"STRT","STREET"}
	,{"STRVN","STRAVENUE"}
	,{"STRVNUE","STRAVENUE"}
	,{"STS","STREETS"}
	,{"ST","STREET"}
	,{"SUMIT","SUMMIT"}
	,{"SUMITT","SUMMIT"}
	,{"SUMMIT","SUMMIT"}
	,{"TERRACE","TERRACE"}
	,{"TERR","TERRACE"}
	,{"TER","TERRACE"}
	,{"THROUGHWAY","THROUGHWAY"}
	,{"TPKE","TURNPIKE"}
	,{"TRACES","TRACE"}
	,{"TRACE","TRACE"}
	,{"TRACKS","TRACK"}
	,{"TRACK","TRACK"}
	,{"TRAFFICWAY","TRAFFICWAY"}
	,{"TRAILER","TRAILER"}
	,{"TRAILS","TRAIL"}
	,{"TRAIL","TRAIL"}
	,{"TRAK","TRACK"}
	,{"TRCE","TRACE"}
	,{"TRFY","TRAFFICWAY"}
	,{"TRKS","TRACK"}
	,{"TRK","TRACK"}
	,{"TRLRS","TRAILER"}
	,{"TRLR","TRAILER"}
	,{"TRLS","TRAIL"}
	,{"TRL","TRAIL"}
	,{"TRNPK","TURNPIKE"}
	,{"TRWY","THROUGHWAY"}
	,{"TUNEL","TUNNEL"}
	,{"TUNLS","TUNNEL"}
	,{"TUNL","TUNNEL"}
	,{"TUNNELS","TUNNEL"}
	,{"TUNNEL","TUNNEL"}
	,{"TUNNL","TUNNEL"}
	,{"TURNPIKE","TURNPIKE"}
	,{"TURNPK","TURNPIKE"}
	,{"UNDERPASS","UNDERPASS"}
	,{"UNIONS","UNIONS"}
	,{"UNION","UNION"}
	,{"UNS","UNIONS"}
	,{"UN","UNION"}
	,{"UPAS","UNDERPASS"}
	,{"VALLEYS","VALLEYS"}
	,{"VALLEY","VALLEY"}
	,{"VALLY","VALLEY"}
	,{"VDCT","VIADUCT"}
	,{"VIADCT","VIADUCT"}
	,{"VIADUCT","VIADUCT"}
	,{"VIA","VIADUCT"}
	,{"VIEWS","VIEWS"}
	,{"VIEW","VIEW"}
	,{"VILLAGES","VILLAGES"}
	,{"VILLAGE","VILLAGE"}
	,{"VILLAG","VILLAGE"}
	,{"VILLE","VILLE"}
	,{"VILLG","VILLAGE"}
	,{"VILLIAGE","VILLAGE"}
	,{"VILL","VILLAGE"}
	,{"VISTA","VISTA"}
	,{"VIST","VISTA"}
	,{"VIS","VISTA"}
	,{"VLGS","VILLAGES"}
	,{"VLG","VILLAGE"}
	,{"VLLY","VALLEY"}
	,{"VL","VILLE"}
	,{"VLYS","VALLEYS"}
	,{"VLY","VALLEY"}
	,{"VSTA","VISTA"}
	,{"VST","VISTA"}
	,{"VWS","VIEWS"}
	,{"VW","VIEW"}
	,{"WALKS","WALKS"}
	,{"WALK","WALK"}
	,{"WALK","WALKS"}
	,{"WALL","WALL"}
	,{"WAYS","WAYS"}
	,{"WAY","WAY"}
	,{"WELLS","WELLS"}
	,{"WELL","WELL"}
	,{"WLS","WELLS"}
	,{"WL","WELL"}
	,{"WY","WAY"}
	,{"XING","CROSSING"}
	,{"XRD","CROSSROAD"}
	,{"XRDS","CROSSROADS"}

	// . cities and states
	// . helps with "abq square dance center" i guess
	// . "abq jump" --> "albuquerque jump"
	,{"abq","albuquerque"}
	,{"alb","albuquerque"}
	,{"cinti","cincinnati"}
	,{"cincy","cincinnati"}

};

static HashTableX s_syn;
static bool s_synInit = false;

// . normalize some words in the place name
// . synonyms
// . 4th --> fourth
// . theatre --> theater
// . school --> {0}
int64_t *getSynonymWord ( int64_t *h, int64_t *prevId, bool isStreet ) {

	static int64_t h_cafeteria;
	static int64_t h_auditorium;
	static int64_t h_school;
	static int64_t h_library;
	static int64_t h_zero;
	static int64_t h_the;
	// set syn table?
	if ( ! s_synInit ) {
		// init it
		if ( ! s_syn.set ( 8,8,1024,NULL,0,false,0,"syntbl")){
			// core dump if this fails
			char *xx=NULL;*xx=0;}
		// stock it
		int32_t n = (int32_t)sizeof(s_synList)/ sizeof(SynTwin);
		for ( int32_t i = 0 ; i < n ; i++ ) {
			// breathe
			//QUICKPOLL ( m_niceness );
			char      *s1   = s_synList[i].m_s1;
			char      *s2   = s_synList[i].m_s2;
			int32_t       len1 = gbstrlen ( s1 );
			int32_t       len2 = gbstrlen ( s2 );
			int64_t  sh1  = hash64Lower_utf8 ( s1 , len1 );
			int64_t  sh2  = hash64Lower_utf8 ( s2 , len2 );
			// skip if the same
			if ( sh1 == sh2 ) continue;
			// sanity check
			if ( sh1 == 0 ) { char *xx=NULL;*xx=0; }
			// core on failure here, this is critical
			if ( ! s_syn.addKey (&sh1,&sh2)){char *xx=NULL;*xx=0;}
		}

		// set these
		h_cafeteria  = hash64b ( "cafeteria" );
		h_auditorium = hash64b ( "auditorium" );
		h_school     = hash64b ( "school" );
		h_library    = hash64b ( "library" );
		h_the        = hash64b ( "the" );
		h_zero       = 0LL;
		// only call once
		s_synInit = true;
	}


	if ( ! isStreet ) {
		// . fix for "Grant Middle School Cafeteria"
		// . blank out "school cafeteria"
		if ( *h==h_cafeteria  && *prevId == h_school ) return &h_zero;
		// blank out "school auditorium" 
		if ( *h==h_auditorium && *prevId == h_school ) return &h_zero;
		// try for "Loma Colorado Main Library Auditorium"?
		if ( *h==h_auditorium && *prevId == h_library ) return &h_zero;
		// smith elementary should equal smith elementary school
		if ( *h==h_school ) return &h_zero;
	}

	// TODO: uncomment this later and replace h_the logic above
	if ( *h == h_the && *prevId == 0LL ) return &h_zero;

	int64_t *p = (int64_t *)s_syn.getValue64 ( *h );

	// check city aliases table. we no longer store city aliases
	// in the synonym list
	// . no! might have "SF Smith" not "Santa Fe Smith"
	//if ( ! p ) {
	//	int64_t *ah1 = (int64_t *) g_aliases.getValue(h);
	//	if ( ah1 ) return ah1;
	//}

	// return what we had if not in syn table
	if ( ! p ) return h;
	// . if *p is 0, that means to ignore it!
	// . return the mapped guy otherwise
	return p;
}

void Addresses::print ( SafeBuf *pbuf , int64_t uh64 ) {

	// print the streets first
	printPlaces( &m_sm , pbuf , m_sections , NULL);//&m_addresses[0] );

	// print NAMES then
	printPlaces( &m_pm , pbuf , m_sections , NULL);//&m_addresses[0] );

	char *hdrFormat = 
		"<table cellpadding=3 border=1>\n"
		"<tr>"
		"<td colspan=40>"
		// table header row
		"%s"
		"</tr>"
		"<tr>"
		"<td><b><nobr>start word</nobr></b></td>"
		"<td><nobr><b>place name 1</b></nobr></td>"
		"<td><nobr><b>place name 2</b></nobr></td>"
		"<td><b>suite</b></td>"
		"<td><b>street</b></td>"
		"<td><b>city</b></td>"
		"<td><b>adm1</b></td>"
		"<td><b>zip</b></td>"
		"<td><b>ctry</b></td>"

		"<td><b>geolat</b></td>"
		"<td><b>geolon</b></td>"

		"<td><b>minedlat</b></td>"
		"<td><b>minedlon</b></td>"

		"<td><b><nobr>importlat</nobr></b></td>"
		"<td><b><nobr>importlon</nobr></b></td>"

		"<td><b>flags</b></td>"
		"<td><b>addrptr</b></td>"
		"<td><b>addrhash</b></td>"
		"<td><b>altnames</b></td>"
		"<td><b>hashes</b></td>"
		"</tr>\n" ;


	// print address table header
	pbuf->safePrintf ( hdrFormat , "Invalid Addresses" );

	// print the final winning addresses
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Address *aa = (Address *)m_am.getPtr(i);
		// is inlined or verified?
		bool valid = false;
		if ( aa->m_flags & AF_INLINED               ) valid = true;
		// but unverified streetisname is not good
		if ( aa->m_street && (aa->m_street->m_flags2 & PLF2_IS_NAME)) 
			valid = false;
		if ( aa->m_flags & AF_VERIFIED_PLACE_NAME_1 ) valid = true;
		if ( aa->m_flags & AF_VERIFIED_PLACE_NAME_2 ) valid = true;
		if ( aa->m_flags & AF_VERIFIED_STREET       ) valid = true;
		// we are only printing INvalids in this table
		if ( valid ) continue;
		// print to page parser pbuf
		Address *ai = (Address *)m_am.getPtr(i);
		ai->print2 ( i,pbuf , 0 );
	}

	pbuf->safePrintf("</table>\n");
	pbuf->safePrintf("<br><br>\n");

	pbuf->safePrintf("<a name=events>\n");

	// Spider.cpp when storing parse.* file will also store an
	// abbreviate file called parse-int16_tdisplay.* consisting only
	// of these div tags for rendering within the qa.html file! that
	// way the qa person can easily check/uncheck all the checkboxes
	// right in the qa.html file
	pbuf->safePrintf("<div class=int16_tdisplay>\n");

	// print checkbox to indicate if events are wrong
	pbuf->safePrintf ( "<!--ignore-->" // ignore for Test.cpp diff
			   "<br>"
			   "<nobr>"
			   // light blue background
			   "<span class=validated "
			   "style=background-color:#9090e0>"
			   "<input type=checkbox "
			   "onclick=\"senddiv(this,'%"INT64"');\" "
			   "unchecked>"
			   "<div class=validated style=display:inline>"
			   " Has <b>address</b> parsing issue. Flag to fix."
			   "</div>"
			   "</span>"
			   "</nobr>" 
			   "<br>"
			   "<br>\n" ,
			   uh64 );

	// print address table header
	pbuf->safePrintf ( hdrFormat , "Inlined and Verified Addresses" );

	// . first print only the INLINED (valid) addresses
	// . i guess if they are verified that is considered valid too!
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// get it
		Address *aa = (Address *)m_am.getPtr(i);
		// is inlined or verified?
		bool valid = false;
		if ( aa->m_flags & AF_INLINED               ) valid = true;
		// but unverified streetisname is not good
		// but unverified streetisname is not good
		if ( aa->m_street && (aa->m_street->m_flags2 & PLF2_IS_NAME)) 
			valid = false;
		if ( aa->m_flags & AF_VERIFIED_PLACE_NAME_1 ) valid = true;
		if ( aa->m_flags & AF_VERIFIED_PLACE_NAME_2 ) valid = true;
		if ( aa->m_flags & AF_VERIFIED_STREET       ) valid = true;
		if ( ! valid ) continue;
		// print to page parser pbuf
		aa->print2 ( i,pbuf , uh64 );//&m_addresses[0]);
	}
	pbuf->safePrintf("</table>\n");
	pbuf->safePrintf("</div class=int16_tdisplay>\n");
	pbuf->safePrintf("<i>NOTE: a name must be VERIFIED before it will "
			 "be a KEY in placedb. So you generally need two "
			 "places inlining the same name before that will "
			 "happen.</i>");
	pbuf->safePrintf("<br>\n");

}

// . looks up each word/phrase in our table of known places
// . table incudes cities, countries, states (adm1), counties, zipcodes
/*
int32_t Addresses::addProperPlaces ( int32_t    a             ,
				  int32_t    b             , 
				  int32_t    maxAlnumCount ,
				  Place  *places        , 
				  int32_t    maxPlaces     ,
				  int32_t    np            ,
				  pbits_t flags         ,
				  // this count excludes "a"?
				  int32_t    alnumPos ,
				  int32_t    forcedEnd ) {
	// int16_tcuts
	Words     *ww    = m_words;
	int32_t       nw    = ww->getNumWords();
	int64_t *wids  = ww->getWordIds();
	char     **wptrs = ww->getWordPtrs();
	int32_t      *wlens = ww->getWordLens();
	nodeid_t   *tids = ww->getTagIds();
	// "4 miles" and "miles" does not mean "miles, california", the city
	int64_t h_miles     = hash64 ( "miles",5);
	int64_t h_mi        = hash64 ( "mi",2);
	int64_t h_kilometers= hash64 ( "kilometers",10);
	int64_t h_km        = hash64 ( "km",2);
	// reset this count again
	int32_t alnumCount = 0;
	// after the street is an optional city
	for ( int32_t j = a ; j<b && alnumCount<maxAlnumCount ; j++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// skip if not alnum
		if ( ! wids[j] ) continue;
		// count alnums
		alnumCount++;
		// skip "miles" in "4 miles"
		if ( wids[j] == h_miles && j-2>= 0 && is_digit(wptrs[j-2][0]))
			continue;
		if ( wids[j] == h_mi    && j-2>= 0 && is_digit(wptrs[j-2][0]))
			continue;
		if ( wids[j] == h_km    && j-2>= 0 && is_digit(wptrs[j-2][0]))
			continue;
		if ( wids[j] == h_kilometers&&j-2>=0&&is_digit(wptrs[j-2][0]))
			continue;
		// . skip if only one char
		// . no! might be like "N. M." to be "new mexico"
		//if ( wlens[j] == 1 ) continue;
		// . skip if two chars and not capitalized
		// . no! misses "123 main st, albuquerque, nm"
		//if ( wlens[j] == 2 && ! is_upper_utf8(wptrs[j]) ) continue;
		// try just doing caps only for now
		if ( is_lower_utf8(wptrs[j]) ) continue;
		// do not skip too far
		int32_t max = j + 6;
		// truncate?
		if ( max > nw ) max = nw;
		// init hash
		int64_t h = 0LL;
		// the alnumcount for this
		int32_t subcount = 0;
		// scan for city/adm1/zip after this street address
		for ( int32_t k = j ; k < max ; k++ ) {
			// stop if tag
			if ( tids[k] ) {
				// skip non-breaking tags
				if ( !isBreakingTagId(tids[k]) ) continue;
				// allow <br> too since microsoft front page
				// inserts those to break a line
				if ( tids[k] == TAG_BR ) continue;
				// other tags, stop us
				break;
			}
			// is it punct?
			if ( ! wids[k] ) {
				// . big punct is a show stopper
				// . no, we had "New\n   Mexico"
				//if ( wlens[k] >= 4 ) break;
				// just skip otherwise
				continue;
			}
			// count it
			subcount++;
			// mix it up
			h <<= 1;
			// hash it into our ongoing hash
			h ^= wids[k];
			// look it up
			int32_t slot = g_cities.getSlot(&h);
			// length
			int32_t plen = (wptrs[k] + wlens[k]) - wptrs[j];
			// skip otherwise
			if ( forcedEnd >= 0 && k < forcedEnd ) continue;
			// clear this
			//int32_t cityCount = 0;
			// init
			Place *pp;
			// multiple places might have this hash
			for ( ; slot>=0 ; slot=g_cities.getNextSlot(slot,&h)){
				// get the place
				PlaceDesc *pd =(PlaceDesc *)g_cities.
					getValueFromSlot(slot);

				// it might be an alias to another slot!
				int32_t slot2 = -1;
				if ( pd->m_bits & PLF_ALIAS ) {
					// get the slot we alias
					slot2 = pd->getSlot();
					// sanity check
					if ( slot2 < 0 ) {char *xx=NULL;*xx=0;}
					// re-get
			     pd=(PlaceDesc *)g_cities.getValueFromSlot(slot2);
				}

				// skip if not a recognized place
				if ( pd->m_type != PT_CITY &&
				     pd->m_type != PT_STATE &&
				     //pd->m_type != PT_ZIP  &&
				     pd->m_type != PT_CTRY   )
					continue;
				// city count
				//if(pd->m_type == PT_CITY) cityCount++;
				// skip if full
				if ( np >= maxPlaces ) continue;
				// point to the right place to store into
				pp = &places[np];
				// sanity check
				if ( ! h ) { char *xx=NULL;*xx=0; }
				// make a place
				pp->m_a       = j;
				pp->m_b       = k+1;
				pp->m_alnumA  = alnumPos + alnumCount;
				pp->m_alnumB  = alnumPos + alnumCount+subcount;
				pp->m_type    = pd->m_type;
				pp->m_str     = wptrs[j];
				pp->m_strlen  = plen;
				pp->m_hash    = h;

				// . use the aliased city, etc. if we had it
				// . that way when we lookup this place in
				//   placedb it will use the right hash
				if ( slot2 >= 0 )
		  pp->m_hash = *(int64_t *)g_cities.getKeyFromSlot(slot2);

				pp->m_adm1[0] = pd->m_adm1[0];
				pp->m_adm1[1] = pd->m_adm1[1];
				pp->m_crid     = pd->m_crid;
				pp->m_bits    = PLF_INFILE | flags ;
				// we use these for zip codes mostly
				pp->m_cityHash= 0;//pd->m_cityHash;
				// inc it
				np++;
				// sanity check
				if ( np >= maxPlaces ) {char*xx=NULL;*xx=0;}
			}

			// only one word for zip code
			if ( k != j ) continue;

			//
			// check if zip code
			//

			// look it up
			slot = g_zips.getSlot(&h);
			// multiple places might have this hash
			for ( ; slot>=0 ; slot=g_zips.getNextSlot(slot,&h)){
				// get the place
				ZipDesc *zd =(ZipDesc *)g_zips.
					getValueFromSlot(slot);
				// skip if full
				if ( np >= maxPlaces ) continue;
				// point to the right place to store into
				pp = &places[np];
				// sanity check
				if ( ! h ) { char *xx=NULL;*xx=0; }
				// make a place
				pp->m_a       = j;
				pp->m_b       = k+1;
				pp->m_alnumA  = alnumPos + alnumCount;
				pp->m_alnumB  = alnumPos + alnumCount+subcount;
				pp->m_type    = PT_ZIP;
				pp->m_str     = wptrs[j];
				pp->m_strlen  = plen;
				pp->m_hash    = h;
				pp->m_adm1[0] = zd->m_adm1[0];
				pp->m_adm1[1] = zd->m_adm1[1];
				pp->m_crid     = zd->m_crid;
				pp->m_bits    = PLF_INFILE | flags ;
				// we use these for zip codes mostly
				pp->m_cityHash= zd->m_cityHash;
				pp->m_cityStr = g_cityBuf + zd->m_cityOffset;
				// inc it
				np++;
				// sanity check
				if ( np >= maxPlaces ) {char*xx=NULL;*xx=0;}
			}
		}
	}
	return np;
}
*/

uint32_t getCityId32 ( uint64_t cityHash64, char *adm1Str ) {
	// sanity checks
	//if ( is_upper_a(adm1Str[0]) ) { char *xx=NULL;*xx=0; }
	//if ( is_upper_a(adm1Str[1]) ) { char *xx=NULL;*xx=0; }
	//if ( adm1Str[2]             ) { char *xx=NULL;*xx=0; }
	// make it lower case to normalize hash
	char na[3];
	na[0] = to_lower_a(adm1Str[0]);
	na[1] = to_lower_a(adm1Str[1]);
	na[2] = '\0';
	// simple hash value
	uint32_t adm1Hash32 =  (uint32_t)*((uint16_t *)na);//adm1Str);
	// get the hash
	uint32_t cid32 = hash32h ( (uint32_t)cityHash64 , adm1Hash32 );
	// . now normalize city if its an abbreviation
	// . if we got the citystatehash for "SF, CA" we want to map it to
	//   "San Francisco, CA"'s citystatehash. this normalizes the cityid.
	// . likewise "SF, NM" --> "Santa Fe, NM"
	uint32_t *ah = (uint32_t *)g_aliases.getValue (&cid32);
	// use that if we had it
	if ( ah ) return *ah;
	// otherwise, we were the real deal
	return cid32;
}

// . make all possible addresses from Places in that section
// . use the Address class
// . only keep the address with maximum score/probability
// . record the section it was found in as well via the Section ptr
// . assign an address probability/score from 0 to 1.0
// . allow inheriting of city or adm1 from title tag or tagdb rec
//   (consider other inheritable places and areas later)
// . must have agreeing street,placeName,adm1 and city 
// . zip is optional
// . base score is .20
// . then add streetScore*0.30 + placeScore*0.30
// . add .10 if we got a valid agreeable zip code
// . add .03 if we got a valid suite
// . add (20-X)/20 * .07 where X is the avg # of alnum words between
//   all possible pairs of the places involved. do not consider
//   inherited Places in this calculation. actually weight the distance
//   involving the place name half as much as other pairs since 
//   place name is often in a subtitle...
// . if first section's m_numOccurences > 1, stop... otherwise...
// . get parent section of that first section
// . and repeat as if it were the first section
// . "startAlnum" is where we expect the city to be in order to set the
//   AF_INLINED bit for this address
// . zip code does NOT override a non-zip code address if the city or adm1
//   are derived from the zip code! or from title or tag!
bool Addresses::addAddress ( Place   *name1   ,
			     Place   *name2   ,
			     Place   *suite   ,
			     Place   *street  ,
			     Place   *city    ,
			     Place   *adm1    ,
			     Place   *zip     ,
			     Place   *ctry    ,
			     Section *addrSec ,
			     // where we expect the city to be in an inlined
			     // address. because we can have multiple streets
			     // for one place name we need this to be
			     // after all such streets.
			     // "abq conv ctr 401 2nd st nw po box 1293 abq nm"
			     // http://www.yelp.com/biz/pizza-9-albuquerque too
			     int32_t     startAlnum ,
			     char     flags3     ,
			     Address **retAddr   ) {

	if ( retAddr ) *retAddr = NULL;

	if ( flags3 & AF2_LATLON ) {
		// assume to store the new address here, the destination
		Address *dst = NULL;
		if ( ! dst ) dst = (Address *)m_am.getMem(sizeof(Address));
		if ( ! dst ) return false;
		if ( retAddr ) *retAddr = dst;
		dst->m_hash     = 0;
		dst->m_score2   = 0;
		// now just do ptrs
		dst->m_name1  = name1;
		dst->m_name2  = name2;
		dst->m_suite  = suite;
		dst->m_street = street;
		dst->m_city   = city;
		dst->m_adm1   = adm1;
		dst->m_zip    = zip;
		dst->m_placedbNames = NULL;
		dst->m_alias = NULL;
		dst->m_latitude  = NO_LATITUDE; // 999.0;
		dst->m_longitude = NO_LONGITUDE; // 999.0;
		dst->m_latLonScore = 0;
		dst->m_latLonDist  = 9999999;
		// reset this for the geocoder lookup
		dst->m_geocoderLat = NO_LATITUDE;
		dst->m_geocoderLon = NO_LONGITUDE;
		// make placedbkey
		//dst->m_placedbKey = dst->makePlacedbKey(m_docId,false,false);
		dst->m_bestPlacedbName = NULL;
		// sanity check
		//if ( dst->m_placedbKey.n1 == 0LL ) { char *xx=NULL;*xx=0; }
		// force this to true
		dst->m_flags      = AF_INLINED;
		dst->m_replyFlags = 0;
		dst->m_domHash32 = m_domHash32;
		dst->m_ip        = m_ip;
		dst->m_section = NULL;
		dst->m_flags3    = flags3;
		dst->m_importedLatitude  = NO_LATITUDE;
		dst->m_importedLongitude = NO_LONGITUDE;
		dst->m_importedVotes     = -1;
		return true;
	}

	// no room left?
	//if ( m_na >= MAX_ADDRESSES ) {
	//	// note it
	//	if ( ! m_firstBreach ) return true;
	//	m_firstBreach = false;
	//	log("addr: got address breach for %s",m_url->getUrl());
	//	return true;
	//	char *xx=NULL; *xx=0; 
	//	return true;
	//}

	// maybe we should try to speed up msg2c by quickly validating
	// whether the street is in that city/state using zak's db... but
	// i don't think truncating the addresses is the right approach
	/*
	if ( m_am.getNumPtrs() >= 10000 ) {
		// note it
		if ( ! m_firstBreach ) return true;
		m_firstBreach = false;
		m_breached = true;
		log("addr: got address breach for %s",m_url->getUrl());
		return true;
	}
	*/

	// if we have a city and the zip does not agree and the
	// zip is after the city, the nuke the zip
	//if ( city && zip && zip->m_cityHash != city->m_hash &&
	//     zip->m_a > city->m_a )
	//	zip = NULL;

	// skip if zip does not agree with state
	if ( adm1 && zip && zip->m_adm1Bits != adm1->m_adm1Bits )
		return true;
	// or agree with city
	if ( city && zip && ! (zip->m_adm1Bits & city->m_adm1Bits ) )
		return true;

	static bool hset = false;
	static int64_t h_zip;
	static int64_t h_code;
	static int64_t h_postal;
	static int64_t h_zipcode;
	static int64_t h_usa;
	if ( ! hset ) {
		hset      = true;
		h_zip     = hash64n("zip");
		h_code    = hash64n("code");
		h_postal  = hash64n("postal");
		h_zipcode = hash64n("zipcode");
		h_zipcode = hash64n("usa");
	}

	//
	// set zipAlnumA
	//
	int32_t zipAlnumA ;
	if ( zip ) zipAlnumA = zip->m_alnumA;
	// scan to left of zip to change zipAlnumA to allow for acceptable
	// words in between it
	int32_t zipa = -1; if ( zip ) zipa = zip->m_a - 1;
	int32_t mini = zipa - 10;
	if ( mini < 0 ) mini = 0;
	int32_t count = 0;
	for ( int32_t i = zipa ; i >= mini ; i-- ) {
		if ( ! m_wids[i] ) continue;
		if ( m_wids[i] == h_zip     ) count++;
		else if ( m_wids[i] == h_code    ) count++;
		else if ( m_wids[i] == h_postal  ) count++;
		else if ( m_wids[i] == h_zipcode ) count++;
		else if ( m_wids[i] == h_usa     ) count++;
		else break;
	}
	//if ( count > 0 )
	//	log("hey");
	// adjust it to allow for words in between
	zipAlnumA -= count;


	/*
	// if zip and no state or city,do not allow unless right next to street
	if ( zip && ! adm1 && ! city && zipAlnumA != startAlnum )
		return true;
	// or if no state, but we have a city and zip, then zip must follow
	// the city or the street
	if ( zip && ! adm1 &&   city && 
	     zipAlnumA != startAlnum &&
	     zipAlnumA != city->m_alnumB &&
	     zip->m_alnumB != city->m_alnumA )
		return true;

	// or if a state and no city...
	if ( zip &&   adm1 && ! city && 
	     zipAlnumA != startAlnum &&
	     zipAlnumA != adm1->m_alnumB &&
	     zip->m_alnumB != adm1->m_alnumA )
		return true;
	*/

	// set cityhash immediately
	uint64_t cityHash = 0;
	if      ( city ) cityHash = city->m_hash;
	else if ( zip  ) cityHash = zip->m_cityHash;
	if ( ! cityHash ) return true;

	// set these
	uint64_t adm1Bits;
	char    *adm1Str = NULL;
	if      ( adm1 ) {
		adm1Bits = adm1->m_adm1Bits;
		adm1Str  = adm1->m_adm1;
	}
	else if ( zip ) {
		adm1Bits = zip->m_adm1Bits;
		adm1Str  = zip->m_adm1;
	}
	//else if ( city && (city->m_adm1Bits & CF_UNIQUE ) ) 
	//	adm1Bits = city->m_adm1Bits;
	else 
		return true;

	// zip cannot be suite #
	if ( suite && zip && zip  ->intersects ( suite ) ) return true;
	if ( suite && zip && suite->intersects ( zip   ) ) return true;

	bool inlined = true;

	// . are we an inlined address? that means the city and adm1 (state)
	//   are right after the street address
	// . therefore we are not inlined if we inherited the city or the
	//   adm1 (state) from a tag or the title of the doc
	pbits_t flags = PLF_FROMTAG | PLF_FROMTITLE;
	// do not use PLF_FROMTITLE if street is in title too though
	if ( m_sections && 
	     (m_sections->m_sectionPtrs[street->m_a]->m_flags & SEC_IN_TITLE) )
		flags = PLF_FROMTAG;
	bool cityOut = false;
	bool adm1Out = false;
	bool zipOut  = false;
	if ( ! city ) cityOut = true;
	if ( ! adm1 ) adm1Out = true;
	if ( ! zip  ) zipOut  = true;
	if ( city && ( city->m_bits & flags ) ) cityOut = true;
	if ( adm1 && ( adm1->m_bits & flags ) ) adm1Out = true;
	if ( zip  && ( zip ->m_bits & flags ) ) zipOut  = true;


	// if we have a suite to the right of the street, it must be
	// RIGHT after the street for now (TODO: allow colon)
	if ( suite && suite->m_a > street->m_a && startAlnum !=suite->m_alnumA)
		inlined = false;


	bool cityInline = false;
	// what is between street and city.
	if ( city && ! suite && startAlnum      == city->m_alnumA ) 
		cityInline = true;
	// suite to the right of street
	if ( city &&   suite && suite->m_alnumB == city->m_alnumA ) 
		cityInline = true;
	// suite to the left of street
	if ( city &&   suite && 
	     suite->m_a < street->m_a && 
	     startAlnum == city->m_alnumA )
		cityInline = true;
	// or if a colon is before city
	// "Street: 4904 4th St NW \nCity/Town: Albuquerque, NM"
	//www.dukecityfix.com/xn/detail/1233957:Event:391851?xg_source=activity
	bool gotColon = false;
	bool gotWord  = false;
	int32_t x;
	if ( city ) x = city->m_a - 1;
	// only loop if city not inlined from above
	for ( ;  city && ! cityInline && x >= street->m_alnumB ; x-- ) {
		// skip if tag
		if ( m_tids[x] ) {
			// just ignore
			if ( ! gotColon ) continue;
			// must have had a word
			if ( ! gotWord ) continue;
			// we need a breaking tag now!
			if ( ! isBreakingTagId ( m_tids[x] ) ) continue;
			// all done!
			cityInline = true;
			// stop
			break;
		}
		// alnum word???
		if ( m_wids[x] ) {
			// if got alnum word before getting colon, no good!
			if ( ! gotColon ) break;
			// mark this
			gotWord = true;
			// otherwise ignore
			continue;
		}
		// got colon?
		if ( m_words->hasChar(x,':') ) gotColon = true;
	}

	// assume we have no city right after the street...
	x = 0;
	int32_t xend = -1;
	char c = 0;
	// also allow something like "123 main st (downtown mall) las cruces"
	// to fix http://www.newmexico.org/calendar/events/index.php?com=
	// detail&eID=22180&year=2011&month=01
	if ( city && 
	     city->m_a >= 0 && 
	     city->m_a > street->m_b &&
	     city->m_a - street->m_b < 20 ) {
		x    = street->m_b;
		xend = city->m_a;
	}
	// loop from end of street to beginning of city looking for '('
	for ( ; x < xend ; x++ ) {
		// skip if tag
		if ( m_tids[x] ) continue;
		// stop on word!
		if ( m_wids[x] ) {
			// unless in parens!
			if ( c ) continue;
			// crap... Msg13.cpp when it sets the dates does not
			// filter out html entites for speed, so watch
			// out for crap after an ampersand or &#. this
			// was causing some americantowns.com urls to
			// not get their address inlined!
			if (x>0 && m_wptrs[x][-1] =='&' ) continue;
			if (x>1 && m_wptrs[x][-1] =='#'&&m_wptrs[x][-2]=='&' )
				continue;
			// otherwise, really stop
			break;
		}
		// check for '(' or '['
		char *p = m_wptrs[x];
		char *pend = p + m_wlens[x];
		for ( ; p < pend ; p++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// check for ( or [
			if ( *p=='(' ) c = '(';
			if ( *p=='[' ) c = '[';
			if ( *p==')' ) c = 0;
			if ( *p==']' ) c = 0;
			continue;
		}
	}
	// if we scanned all the way through, that's great, we are inlined
	if ( x == xend ) cityInline = true;
		

	// turn it off
	if ( city && ! cityInline ) inlined = false;


	// this restriction was inspidered by "The TAVERN, 4701 Menaul,
	// between Washington and Carlisle..." making gb think that it is
	// in the city of Carlisle in Washington...
	if (city && adm1 && city->m_alnumB != adm1->m_alnumA )
	    // but if city is "unique" like albuquerque, we allow it
	    //!(city->m_adm1Bits & CF_UNIQUE) ) 
		inlined = false;

	/*
	// . wow, "less than 1 mile away from Abq NM" inspired me to require
	//   that the street be adjacent to the city now!
	// . but i am seeing more false positives, so restrict things more
	//if ( ! suite && street->m_alnumB != city->m_alnumA ) 
	if ( ! suite && startAlnum != city->m_alnumA ) 
		inlined = false;
	// if we have a suite, and it is left of the street, that is ok too
	if (   suite && suite->m_a < street->m_a &&
	       //street->m_alnumB != city->m_alnumA ) 
	       startAlnum != city->m_alnumA ) 
		inlined = false;
	if ( suite && suite->m_a > street->m_a &&
	     suite->m_alnumB != city->m_alnumA )
		inlined = false;
	*/

	// if you got a zip, must follow adm1 immediately
	// fixes http://www.estrelladelnortevineyard.com/SFV_retloc.php
	//if ( zip && adm1 && adm1->m_alnumB != zipAlnumA )
	//	inlined = false;

	bool zipInline = false;
	// . zip right after street is good
	// . but the city/adm1 must in title or tag, not after the zip 
	//   otherwise we end up inlining bad cities after the zip like
	//   "house, nm"
	if ( zip ) {
		if ( startAlnum == zipAlnumA ) zipInline = true;
		if ( suite && suite->m_alnumB == zipAlnumA ) zipInline = true;
		// . or if zip follows city where city is tight
		// . "114 Coronado Road, Corrales, 87048"
		if ( city && city->m_alnumB == zipAlnumA ) zipInline = true;
		if ( adm1 && adm1->m_alnumB == zipAlnumA ) zipInline = true;
		// turn it off
		if ( ! zipInline ) inlined = false;
	}

	// set this
	bool adm1Inline = false;
	if ( adm1 ) {
		if ( adm1->m_alnumA == street->m_alnumB ) 
			adm1Inline = true;
		if ( city && adm1->m_alnumA == city->m_alnumB ) 
			adm1Inline = true;
		if ( ! adm1Inline ) inlined = false;
	}

	// fix for http://www.ucomparehealthcare.com/drs/washington/
	// obstetrics_and_gynecology/Seattle.html
	// 1959 NE Pacific St
	// University Washington Medical Center
	// Seattle, WA 98195
	// gets "University" as a city in "Washington" state!
	if ( adm1 ) {
		int32_t ab = adm1->m_b;
		int64_t *wids  = m_words->getWordIds();
		char     **wptrs = m_words->getWordPtrs();
		int32_t      *wlens = m_words->getWordLens();
		nodeid_t   *tids = m_words->getTagIds();
		int32_t          nw = m_words->getNumWords();
		if ( inlined && ab-1>= 0 && ab+1 < nw && ! tids[ab] && 
		     ! wids[ab] &&
		     wlens[ab]==1 &&
		     // this was hurting 
		     // "195 Crystie Street, Suite 20<br>\nNew York, NY USA"
		     // so i added this constraint
		     wlens[ab-1] == 1 &&
		     is_wspace_utf8(wptrs[ab]) &&
		     is_upper_utf8(wptrs[adm1->m_a]) &&
		     is_upper_utf8(wptrs[ab+1]) )
			inlined = false;
	}

	// TEMPORARY HACK TO DEBUG URL
	//if ( city && (city->m_flags2 & PLF2_REQUIRED) )
	//	inlined = true;

	/////////////////////
	//
	//  set the address hash (combo of street,city,adm1)
	//
	/////////////////////
	uint64_t ch = getAddressHash ( street, city, adm1, zip );

	// do not add it if street name is lower case and adm1 and city
	// are inlined and upper. should fix "4 barrack Oakland CA" and
	// "3 spacios - Seattle WA" for graffiti.org
	//if ( inlined &&
	//     ! (street->m_bits & PLF_HAS_UPPER ) &&
	//     ! (street->m_flags2 & PLF2_HAD_INDICATOR ) &&
	//     is_upper_utf8(wptrs[adm1->m_a]) &&
	//     is_upper_utf8(wptrs[city->m_a]) )
	//	return true;


	// . now compare to other address with this same street
	for ( int32_t i = m_am.getNumPtrs() - 1 ; i >= 0 ; i-- ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Address *prev = (Address *)m_am.getPtr(i);
		// if not our street, bail!
		if ( prev->m_street->m_a != street->m_a ) break;
		// if he is inlined and we are not!
		if ( prev->m_flags & AF_INLINED ) {
			// if we are not, bail, do not add us
			if ( ! inlined ) return true;
		}
		// if he is not inlined and we are, we overwrite him
		else if ( inlined ) {
			// overwrite him
			//dst = prev;
			// kill him
			//m_na--;
			m_am.rewind ( 1 );
			// print him
			//log("DELETING the following address 1:");
			//dst->print();
			// try to kill more
			continue;
		}
		// ok, we are not inlined and previous got isn't either...
		break;
	}

	// . now for the remaining address with this same street, they are
	//   all, including ourselves, either inlined or not inlined
	// . assign a score to each address for a particular street
	// . the address with the highest score wins and the others
	//   are removed. in the case of a tie we keep all of them.
	// . we only do this comparison to addresses that have the same
	//   address hash, 
	int32_t score = 0;
	// inlining always trumps all others
	//if ( inlined ) score += 10000;
	// and then if all else is equal, having a zip is better than just
	// a city because it is more specific
	if ( zip ) score += 1000;
	// having a valid adm1 is good (might not have one explicity if city
	// is unique to a particular state)
	if ( adm1 ) score += 100;
	// prefer city over no city
	if ( city ) score += 10;
	// sanity check
	if ( score <= 0 ) { char *xx=NULL;*xx=0; }

	Address *dst = NULL;

	// now compare to other address with this same address hash
	for ( int32_t i = m_am.getNumPtrs() - 1 ; i >= 0 ; i-- ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Address *prev = (Address *)m_am.getPtr(i);
		// stop if for a different street
		if ( prev->m_street->m_a != street->m_a ) break;
		if ( prev->m_street->m_b != street->m_b ) break;
		// skip if should not compare
		if ( prev->m_hash != ch ) continue;
		// do not add us if he is higher score
		if ( prev->m_score2 > score ) return true;
		// if a tie, that is strange!
		if ( prev->m_score2 == score ) return true;
		// overwrite him
		dst = prev;
		// an undo for the m_na down below
		//m_na--;
		//m_am.rewind ( 1 );
		// one at a time
		break;
		// print him
		//log("DELETING the following address 1:");
		//dst->print();
		// try to kill more
		//continue;
	}

	// assume to store the new address here, the destination
	if ( ! dst ) dst = (Address *)m_am.getMem(sizeof(Address));
	if ( ! dst ) return false;

	if ( retAddr ) *retAddr = dst;

	//dst->m_cityHash = cityHash;
	//dst->m_adm1Bits = adm1Bits;
	dst->m_cityId32 = getCityId32 ( cityHash , adm1Str ); 
	dst->m_hash     = ch;
	dst->m_score2   = score;

	// now just do ptrs
	dst->m_name1  = name1;
	dst->m_name2  = name2;
	dst->m_suite  = suite;
	dst->m_street = street;
	dst->m_city   = city;
	dst->m_adm1   = adm1;
	dst->m_zip    = zip;

	dst->m_placedbNames = NULL;

	// nuke this for comparing for setting AF_AMBIGUOUS bit
	//if ( zip ) dst->m_zip->m_hash = 0;

	// reset this too
	dst->m_alias = NULL;

	dst->m_latitude  = NO_LATITUDE; // 999.0;
	dst->m_longitude = NO_LONGITUDE; // 999.0;
	dst->m_latLonScore = 0;
	dst->m_latLonDist  = 9999999;

	// reset this for the geocoder lookup
	dst->m_geocoderLat = NO_LATITUDE;
	dst->m_geocoderLon = NO_LONGITUDE;
	
	// make placedbkey
	dst->m_placedbKey = dst->makePlacedbKey ( m_docId , false, false );

	dst->m_bestPlacedbName = NULL;

	// the address voting table key is based on the placedb key but needs
	// to be unique for each address! there are often times the same
	// street address with a different place name, and since the placedb
	// key does not even take the place name into account, we need to
	// for this...
	//dst->m_avtKey = dst->makeAddressVotingTableKey ( );

	// need these
	//if ( ! tmp->m_name   ) { char *xx=NULL;*xx=0; }
	if ( ! street ) { char *xx=NULL;*xx=0; }
	if ( ! city && ! zip  ) { char *xx=NULL;*xx=0; }
	// unique cities like Albuquerque imply a state
	if ( ! adm1 && ! zip && ! city->m_adm1[0] ) { char *xx=NULL;*xx=0; }


	// sanity check
	if ( ! street->m_hash ) { char *xx=NULL;*xx=0; }
	//if ( ! street->m_streetNumHash ) { char *xx=NULL;*xx=0; }
	if ( city && ! city->m_hash ) { char *xx=NULL;*xx=0; }
	if ( adm1 && ! adm1->m_adm1Bits ) { char *xx=NULL;*xx=0; }

	// sanity check
	if ( dst->m_placedbKey.n1 == 0LL ) { char *xx=NULL;*xx=0; }

	// reset flags
	dst->m_flags      = 0;
	dst->m_replyFlags = 0;

	if ( inlined ) dst->m_flags |= AF_INLINED;

	// . HACK! if our m_str referenced our m_adm1, fix that!
	// . see "HACK" above to where we did this
	//if ( adm1->m_str == adm1->m_adm1 ) 
	//	dst->m_adm1->m_str = dst->m_adm1->m_adm1;

	// set m_b for the address so we can use it when as a boundary
	// for harvesting place names for following addresses above
	/*
	int32_t max = -1;
	if ( dst->m_street->m_b     > max ) max = dst->m_street->m_b;
	if ( dst->m_adm1->m_b       > max && inlined ) max = dst->m_adm1->m_b;
	if ( dst->m_city->m_b       > max && inlined ) max = dst->m_city->m_b;
	// do not require inlineness for a zip!
	if ( zip && dst->m_zip->m_b > max            ) max = dst->m_zip->m_b;
	// or for a suite!
	if ( suite && suite->m_b   > max            ) max = suite->m_b;
	dst->m_b = max;

	// and the left most point not including place name
	dst->m_a = dst->m_street->m_a;
	// suite might be before street sometimes
	if ( suite && suite->m_a < dst->m_a ) dst->m_a = suite->m_a;
	*/

	// add these in
	dst->m_domHash32 = m_domHash32;
	dst->m_ip        = m_ip;

	// get the section containing all components
	int32_t a = dst->m_street->m_a;
	int32_t b = dst->m_street->m_b;
	// increase address range? 
	if ( suite && suite->m_a < a ) a = suite->m_a;
	if ( suite && suite->m_b > b ) b = suite->m_b;
	// sometimes the city/adm1/zip is in the title or something
	// so only use it if within reach!!
	if ( ! cityOut && city && city->m_b > b && city->m_b < b + 20 ) 
		b = city->m_b;
	if ( ! adm1Out && adm1 && adm1->m_b > b && adm1->m_b < b + 20 ) 
		b = adm1->m_b;
	if ( ! zipOut  && zip  && zip ->m_b > b && zip->m_b < b + 20 ) 
		b = zip->m_b;

	//if ( ! cityOut && city && city->m_a < a ) a = city->m_a;
	//if ( ! adm1Out && adm1 && adm1->m_a < a ) a = adm1->m_a;
	//if ( ! zipOut  && zip  && zip ->m_a < a ) a = zip->m_a;

	if ( a < 0 ) { char *xx=NULL;*xx=0; }

	// get section
	Section *as = NULL;
	if ( m_sections ) as = m_sections->m_sectionPtrs[a];
	// telescope up until contains all inlined things in address
	//for ( ; as ; as = as->m_parent ) 
	//	// stop if contained
	//	if ( as->m_a <= a && as->m_b >= b ) break;
	// store that
	dst->m_section = as;
	dst->m_flags3    = flags3;
	//dst->m_latitude  = latitude;
	//dst->m_longitude = longitude;

	// reset the imported lat/lon
	dst->m_importedLatitude  = NO_LATITUDE;
	dst->m_importedLongitude = NO_LONGITUDE;
	dst->m_importedVotes     = -1;

	// advance m_na iff we did not overwrite a previous address
	//m_na++;

	//log("addr: u=%s addr # = %"INT32"",m_url->m_url,m_na-1);
	// uncomment this for debug to the log
	//dst->print ( );
		
	return true;
}

uint64_t  getAddressHash ( Place *street ,
			   Place *city   ,
			   Place *adm1   ,
			   Place *zip    ) {

	int64_t ch = 0;
	ch ^= street->m_hash;
	ch ^= street->m_streetNumHash;
	ch ^= street->m_streetIndHash;
	// adm1
	char *adm1Str = NULL;
	if      ( adm1 ) adm1Str = adm1->m_adm1;
	else if ( zip  ) adm1Str = zip->m_adm1;
	else if ( city && city->m_adm1[0] ) adm1Str = city->m_adm1;
	else               { char *xx=NULL;*xx=0; }
	// xor in adm1
	//ch ^= (int64_t)*((uint16_t *)adm1Str);
	// and city hash
	uint64_t cityHash = 0;
	if      ( city ) cityHash = city->m_hash;
	else if ( zip  ) cityHash = zip->m_cityHash;
	if ( ! cityHash ) { char *xx=NULL;*xx=0; }
	//ch ^= cityHash;
	// . use this instead. it will convert "SF,CA" to "San Francisco"
	// . use a special adm1 bit in the bit vector to indicate its an alias
	// . if its an alias we check the g_aliases table to see what the
	//   cityHash64 should really be
	uint32_t cid32 = getCityId32(cityHash,adm1Str);
	ch ^= cid32;
	return ch;
}

bool setFromStr ( Address *a, char *s, pbits_t flags , 
		  PlaceMem *pm ,
		  int32_t niceness ) {
	// clear it up
	a->reset();
	// int16_tcuts
	//Place *city = NULL;
	//Place *adm1 = NULL;
	a->m_latitude  = NO_LATITUDE;
	a->m_longitude = NO_LONGITUDE;
	a->m_geocoderLat = NO_LATITUDE;
	a->m_geocoderLon = NO_LONGITUDE;
	// ctry is always empty, because its always the US
	// name1;name2;suite;street;city;adm1;zip;ctry;domhash;ip;origurl;lat;lon;addrHash\0
	// . loop it
	for ( int32_t i = 0 ; i <= 13 ; i++ , s++ ) {
		// stop if no more fields
		if ( ! *s ) break;
		// save it
		char *start = s;
		// advance s to ;
		//while ( *s && *s != ';' && *s !='(' ) s++;
		while ( *s && *s != ';' ) s++;
		// site hash?
		if ( i == 8 ) {
			a->m_domHash32 = 0;
			// panic if none!
			if ( *start == ';' ) { char *xx=NULL;*xx=0;}//continue;
			a->m_domHash32 = (uint32_t)atoll(start);
			continue;
		}
		// ip?
		if ( i == 9 ) {
			a->m_ip = 0;
			if ( *start == ';' ) continue;
			a->m_ip = atoip(start,s-start);
			// 0 -1 not allowed
			if ( a->m_ip==0 || a->m_ip==-1) {char *xx=NULL;*xx=0;}
			continue;
		}
		// skip orig url
		if ( i == 10 ) {
			// skip if empty
			if ( *start == ';' ) continue;
			continue;
		}
		// latitude
		if ( i == 11 ) {
			// skip if empty
			if ( *start == ';' ) continue;
			// set it
			a->m_latitude = atod2 (start,s-start);
			continue;
		}
		// longitude
		if ( i == 12 ) {
			// skip if empty
			if ( *start == ';' ) continue;
			// set it
			a->m_longitude = atod2 (start,s-start);
			// skip semicolon
			if ( ! *s ) break;
			continue;
		}
		// addrHash
		if ( i == 13 ) {
			// skip if empty
			if ( *start == ';' ) continue;
			// must be digit
			//if ( is_digit(*p) )
			a->m_hash = strtoull(start,NULL,10);//atoll(p);
			// skip semicolon
			break;
		}
		// timezone offset
		//if ( i == 13 ) {
		//	// skip if empty
		//	if ( *start == ';' ) continue;
		//	// set it
		//	a->m_timeZoneOffset= atol2 (start,s-start);
		//	// skip semicolon
		//	if ( *s && *s == ';' ) s++;
		//	continue;
		//}
		// ptr to a place
		//Place *p = NULL;
		// get length of place
		int32_t slen = s - start;
		// skip if empty
		if ( slen <= 0 ) continue;
		// do not breach
		//if ( *np >= maxPlaces ) { char *xx=NULL;*xx=0; }
		// ok, add this entry
		Place *p = (Place *)pm->getMem(sizeof(Place));//&places[*np];
		if ( ! p ) { char *xx=NULL;*xx=0; }
		// advance np
		//*np = *np + 1;
		// pt = "place type"
		int32_t pt;
		if ( i == 0 ) { a->m_name1  = p; pt = PT_NAME_1;}
		if ( i == 1 ) { a->m_name2  = p; pt = PT_NAME_2;}
		if ( i == 2 ) { a->m_suite  = p; pt = PT_SUITE;}
		if ( i == 3 ) { a->m_street = p; pt = PT_STREET;}
		if ( i == 4 ) { a->m_city   = p; pt = PT_CITY;}
		if ( i == 5 ) { a->m_adm1   = p; pt = PT_STATE;}
		if ( i == 6 ) { a->m_zip    = p; pt = PT_ZIP; }
		if ( i == 7 ) { continue; }// p = a->m_ctry;   pt = PT_CTRY;}
		// clear it
		p->reset();
		// set it
		p->m_type     =  pt;
		p->m_a        = -7;
		p->m_b        = -6;
		p->m_alnumA   = -5;
		p->m_alnumB   = -4;
		p->m_str      = start;
		p->m_strlen   = slen;
		p->m_bits     = 0;
		// set adm1 bits if adm1
		if ( pt == PT_STATE ) {
			p->m_adm1Bits = getAdm1Bits ( start );
			// set the state two-letter abbr as well
			p->m_adm1[0] = start[0];
			p->m_adm1[1] = start[1];
		}
		/*
		// we got a parenthetical?
		char *parens = NULL;
		// skip semicolon
		if ( *s && *s == '(' ) {
			// what is this from now?
			char *xx=NULL;*xx=0;
			// skip parens
			s++;
			// mark it
			parens = s;
			// skip to end
			for ( ; *s && *s != ';' ; s++ );
		}
		*/
		// skip semicolon
		//if ( *s && *s == ';' ) s++;
		// store it in Address class if not NULL
		if ( ! p->m_str ) continue;

		// incorporate the flags. usually PLF_FROMTAG
		p->m_bits = flags;

		// clear these
		p->m_flags2 = 0;

		// two letter country code in parentheses
		//if ( i == 7 && parens && parens[2] == ')' )
		//	p->m_crid = getCountryId ( parens );
		// . two letter admin code in parentheses
		// . usually only city names and zip codes have this
		//if ( i != 7 && parens && parens[2] == ')' ) {
		//	p->m_adm1[0] = parens[0];
		//	p->m_adm1[1] = parens[1];
		//}

		// and make the word non-overlappable
		p->m_a = -3;
		p->m_b = -2;
		// null it out
		p->m_hash = 0LL;
		p->m_streetIndHash = 0LL;
		p->m_streetNumHash = 0LL;
		
		// set m_streetHash, m_streetIndHash, m_streetNumHash of
		// this Place, p
		setHashes ( p , NULL , niceness );

		// do not take streets from tag, must be on the page itself
		if ( i == 3 && (flags & PLF_FROMTAG) ) continue;
		// do not take name from tag either!
		if ( i == 0 && (flags & PLF_FROMTAG) ) continue;
		if ( i == 1 && (flags & PLF_FROMTAG) ) continue;
		// nor suite
		if ( i == 2 && (flags & PLF_FROMTAG) ) continue;

		// and make the word non-overlappable
		//p->m_a = -3;
		//p->m_b = -2;

		// save these
		//if ( i == 4 ) city = p;
		//if ( i == 5 ) adm1 = p;

		// if we are a city OR a zip code, we must set m_hash since 
		// addAddress() uses it to check for dups!
		/*
		if ( i == 4 || i == 5 ) {
			Words w; 
			// i guess just use "version" of 0
			if ( ! w.set (p->m_str , p->m_strlen,0,true,niceness)) 
				return false;
			// int16_tcut
			int64_t *wids = w.getWordIds();
			// zero out the hash
			int64_t h = 0LL;
			// loop em
			for ( int32_t j = 0 ; j < w.m_numWords ; j++ ) {
				// skip if not alnum
				if ( ! wids[j] ) continue;
				// mix it up
				h <<= 1;
				// xor it in
				h ^= wids[j];
			}
			// set that hash
			p->m_hash = h;
		}
		*/
		// update crid
		if ( i == 7 ) {
			/*
			// get numeric id
			uint8_t crid = getCountryId(p->m_str);
			// set it 
			p->m_crid = crid;
			// and for adm1
			adm1->m_crid = crid;
			// and city
			city->m_crid = crid;
			*/
		}
	}

	// if it was a lat/lon only contact address it will not have a
	// city, so this is NULL. perhaps, just give up on that?
	// this is not the case any more since we insert the foreign
	// country and state and city sometimes
	if ( ! a->m_city || ! a->m_adm1 ) 
		a->m_flags3 |= AF2_LATLON;

	// set adm1 bits last from the two character string code
	if ( a->m_city && a->m_adm1 ) {
		a->m_city->m_adm1Bits = a->m_adm1->m_adm1Bits;
		a->m_city->m_adm1[0]  = a->m_adm1->m_adm1[0];
		a->m_city->m_adm1[1]  = a->m_adm1->m_adm1[1];
	}
	if ( a->m_zip ) {
		a->m_zip->m_adm1Bits = a->m_adm1->m_adm1Bits;
		a->m_zip->m_adm1[0]  = a->m_adm1->m_adm1[0];
		a->m_zip->m_adm1[1]  = a->m_adm1->m_adm1[1];
	}

	// require ip
	if ( a->m_ip == 0 || a->m_ip == -1 ) { char *xx=NULL;*xx=0; }

	// do we need this?
	a->m_cityId32 = 0;
	/*
	// adm1
	char *adm1Str = NULL;
	if      ( a->m_adm1 ) 
		adm1Str = a->m_adm1->m_adm1;
	else if ( a->m_zip  ) 
		adm1Str = a->m_zip->m_adm1;
	else if ( a->m_city && a->m_city->m_adm1[0] ) 
		adm1Str = a->m_city->m_adm1;
	else               { char *xx=NULL;*xx=0; }
	// use city hash
	a->m_cityId64 = getCityId64 ( a->m_city->m_hash , adm1Str );
	*/

	// update "m_crid" member on all relevant places
	return true;
}

void setFromStr2 ( char  *addr   ,
		   char **name1  ,
		   char **name2  ,
		   char **suite  ,
		   char **street ,
		   char **city   ,
		   char **adm1   ,
		   char **zip    ,
		   char **country,
		   double *lat    ,
		   double *lon    ) {
	// use this
	static char s_addr[2048];
	//int32_t alen = gbstrlen(addr);
	//char *aend = addr + alen;
	//int32_t  *tzoff  ) {
	if ( name1  ) *name1  = NULL;
	if ( name2  ) *name2  = NULL;
	if ( suite  ) *suite  = NULL;
	if ( street ) *street = NULL;
	if ( city   ) *city   = NULL;
	if ( adm1   ) *adm1   = NULL;
	if ( zip    ) *zip    = NULL;
	if ( country) *country= NULL;
	if ( lon    ) *lon    = 999.00;
	if ( lat    ) *lat    = 999.00;

	// breach check
	int32_t len = gbstrlen(addr);
	if ( len + 1 > 2048 ) {
		log("addr: address is too big to parse");
		return;
	}

	// copy into our static buffer
	gbmemcpy ( s_addr , addr , len+1 );

	// parse it in our static buffer so we do not destroy it
	char *p = s_addr;

	// if we are double called on the same "addr" string we have to
	// expect to encounter \0 just as we would ';'... and we do this
	// now from PageResults.cpp because it uses ExpandedResults, where
	// an event that has a date like "every wednesday" results in like
	// 104 search results, so that search result has to be repeated
	// in the listings using the same address "addr" over and over again,
	// and each time it calls setFromStr2, so since this is destructive
	// that way, be prepared!
	if ( name1  ) *name1  = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( name2  ) *name2  = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( suite  ) *suite  = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( street ) *street = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( city   ) *city   = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( adm1   ) *adm1   = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( zip    ) *zip    = p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	if ( country) *country= p; for ( ; *p != ';' ; p++ ); *p++ = '\0';
	//for ( ; *p != ';' ; p++ ); p++; // was country
	for ( ; *p != ';' ; p++ ); p++; // domhash?
	for ( ; *p != ';' ; p++ ); p++; // ip
	for ( ; *p != ';' ; p++ ); p++; // orig url
	if ( lat && *p!=';' ) *lat = atof(p); 
	for ( ; *p != ';' ; p++ ); p++;
	if ( lon && *p      ) *lon = atof(p); 
	//if ( tzoff ) *tzoff= atol(p);
	//s anity check
	//if ( p > aend ) { char *xx=NULL;*xx=0; }
}

// . year is like "2011" or whatever
// . assume we are in greenwhich england (timezone=+0)
// . BUT apply the american daylight start/end times
// . currently in affect from 2nd sunday in march to first sunday in nov @ 2am
void getDSTInterval ( int32_t year , int32_t *a , int32_t *b ) {
	// find the 2nd sunday in march for this year
	*a = getDOWStart ( year, 3, 1, 2); // 3=march 1=sunday, 2=2nd
	// 2am?
	*a += 2*3600;
	// the end point now
	*b = getDOWStart ( year, 11, 1, 1); // 11=nov 1=sunday 1=1st
	// 2am
	*b += 2*3600;
}
// . nowUTC is # secs elapsed since epoch in UTC (no DST)
// . currently in affect from 2nd sunday in march to first sunday in nov @ 2am
bool getIsDST ( int32_t nowUTC , char timezone2 ) {
	// mod the time
	time_t mod = (time_t)nowUTC ;
	// add if known
	if ( timezone2 != UNKNOWN_TIMEZONE ) {
		// sanity check, make sure its the offset, not in seconds
		if ( timezone2 >  13 ) { char *xx=NULL;*xx=0; }
		if ( timezone2 < -13 ) { char *xx=NULL;*xx=0; }
		mod += timezone2*3600;
	}
	// get DOW now
	struct tm *timeStruct = gmtime ( &mod );
	// certain months are always dst. jan = 0. goes from 0 to 11.
	int32_t mon = timeStruct->tm_mon;
	// feb=1,mar=2,apr=3,may=4,jun=5,jul=6,aug=7,sep=8,oct=9,nov=10,dec=11
	if ( mon >= 3 && mon <= 9 ) return true;
	// not in dec
	if ( mon == 11 ) return false;
	// not in jan or feb
	if ( mon >= 0 && mon <= 1 ) return false;
	// get dow. 0 to 6. 0 being sunday.
	int32_t dow = timeStruct->tm_wday;
	// what # of dow are we? i.e. xth monday, where x=dowCount
	int32_t dowCount = 1 + timeStruct->tm_mday / 7;
	// for march, if we are the 2nd dow, and not sunday, return true
	if ( mon == 2 ) {
		if ( dowCount <= 1 ) return false;
		if ( dowCount >= 3 ) return true;
		if ( dowCount == 2 && dow != 0 ) return true;
		// if before 2nd sunday at 2am, not yet summer time
		if ( dowCount == 2 && dow == 0 )
			return ( timeStruct->tm_hour >= 2 );
	}
	// november
	if ( mon == 10 ) {
		if ( dowCount >= 2 ) return false;
		if ( dowCount == 1 && dow != 0 ) return false;
		// if before 1st sunday at 2am, it is still summer time
		if ( dowCount == 1 && dow == 0 )
			return ( timeStruct->tm_hour < 2 );
	}
	// how did we get here?
	char *xx=NULL;*xx=0;
	return false;
}

class CityStateDesc {
public:
	float m_latitude;
	float m_longitude;
	char  m_timeZoneOffset;
	char  m_useDST;
	//uint8_t m_crid;
	// id within that country
	//uint8_t m_stateId;
};


bool getCityLatLonFromAddrStr ( char *addr , double *lat , double *lon ) {
	// get city from string
	uint32_t cid32 = 0;
	if ( addr[0] ) cid32 = getCityIdFromAddr ( addr );
	// assume city/state not found in our list
	*lat = NO_LATITUDE;
	*lon = NO_LONGITUDE;
	// now get lat lon of that city
	bool status = getLatLon ( cid32 , lat , lon );
	// returns false if city not found
	return status;
}

uint32_t getCityIdFromAddr ( char *addr ) {
	// get city and adm1 from address
	char *p = addr;
	int32_t semiCount = 0;
	char *adm1 = NULL;
	char *city = NULL;
	for ( ; ; p++ ) {
		// skip if not border
		if ( *p != ';' ) continue;
		// inc it
		semiCount++;
		// city?
		if ( semiCount == 4 ) {
			city = p + 1;
			continue;
		}
		if ( semiCount == 5 ) {
			adm1 = p + 1;
			continue;
		}
		if ( semiCount != 6 ) continue;

		break;
	}
	// if no city try lat/lon
	if ( city[0] == ';' ) {
		double lat = 0.0;
		double lon = 0.0;
		getLatLonFromStr ( addr , &lat , &lon );
		float distInMilesSquared = 0.0;
		uint32_t cid32 = getNearestCityId ( lat , lon , 0,
						    &distInMilesSquared);
		if ( distInMilesSquared > 1000 ) 
			cid32 = 0;
		// how can this be 0?
		//if ( cid32 == 0 ) { char *xx=NULL;*xx=0; }
		return cid32;
	}
	// ok, we got both now
	char *semi1 = adm1 - 1;
	char *semi2 = p;
	// temp null term
	*semi1 = '\0';
	*semi2 = '\0';
	// fix Denver's so we do not return unknown timezone
	if ( semi1[-1]=='s' && semi1[-2]=='\'' ) semi1[-2]='\0';
	// get city hash
	int64_t h = getWordXorHash(city);
	// TODO: make state into two letter abbr?
	//if ( gbstrlen(adm1) != 2 ) { char *xx=NULL;*xx=0; }
	// use this now
	uint32_t cid32 = (uint64_t)getCityId32(h,adm1);
	// put back
	*semi1 = ';';
	*semi2 = ';';
	// put apostrophe back if we stripped it
	if ( ! semi1[-2] ) semi1[-2] = '\'';
	return cid32;
}

PlaceDesc *getCityPlaceDescFromAddrLatLon_new ( char *addr ) {
	double lat = 0.0;
	double lon = 0.0;
	getLatLonFromStr ( addr , &lat , &lon );
	float distInMilesSquared = 0.0;
	PlaceDesc *pd = getNearestCity_new (lat,lon,0,&distInMilesSquared);
	if ( distInMilesSquared < 1000 ) return pd;
	return NULL;
}



char getTimeZoneFromAddr ( char *addr , char *useDST ) {

	// . try this new function
	// . if no city explicitly, use lat/lon to get nearest city?
	// . returns NULL if no nearby city
	PlaceDesc *pd = getCityPlaceDescFromAddrLatLon_new ( addr );

	if ( pd && useDST ) {
		*useDST = 0;
		if ( pd->m_flags & PDF_USE_DST ) *useDST = 1;
	}
	if ( pd ) return pd->m_timeZoneOffset;

	// i guess we choose not to store the lat/lon for US cities
	// because we can look them up by name here...
	uint32_t cid32 = getCityIdFromAddr ( addr );
	// if it had a city specified, or its lat/lon was nearby a city,
	// then use that city id to get the timezone
	if ( cid32 ) return getTimeZone3 ( cid32 , useDST );
	// if doesn't have a city or the specified lat/lon is not close
	// to a city in our list then let's use the lat lon to get the
	// timezone


	double lat = 0.0;
	double lon = 0.0;
	getLatLonFromStr ( addr, &lat, &lon );
	if ( lat == NO_LATITUDE ) return UNKNOWN_TIMEZONE;
	if ( lon == NO_LATITUDE ) return UNKNOWN_TIMEZONE;
	// ASSUME THEY USE DST! WE DON'T KNOW REALLY!!
	if ( useDST ) *useDST = 1;
	return  (char)(int32_t)(lon / (360.0/24.0));
}	


/*
// . hash city and state together then lookup in g_timeZones table
// . name1;name2;suite;street;city;adm1;zip;domhash;ip;origurl;lat;lon\0
// . uint32_t getCityHash32 ( char *addr , uint32_t *adm1Hash ) {
char getTimeZoneFromAddr ( char *addr , char *useDST ) {

	// get city and adm1 from address
	char *p = addr;
	int32_t semiCount = 0;
	char *adm1 = NULL;
	char *city = NULL;
	for ( ; ; p++ ) {
		// skip if not border
		if ( *p != ';' ) continue;
		// inc it
		semiCount++;
		// city?
		if ( semiCount == 4 ) {
			city = p + 1;
			continue;
		}
		if ( semiCount == 5 ) {
			adm1 = p + 1;
			continue;
		}
		if ( semiCount != 6 ) continue;

		break;
	}
	// ok, we got both now
	char *semi1 = adm1 - 1;
	char *semi2 = p;
	// temp null term
	*semi1 = '\0';
	*semi2 = '\0';

	// fix Denver's so we do not return unknown timezone
	if ( semi1[-1]=='s' && semi1[-2]=='\'' ) semi1[-2]='\0';

	char tzoff = getTimeZone2 ( city , adm1 , useDST );
	// put back
	*semi1 = ';';
	*semi2 = ';';
	// put apostrophe back if we stripped it
	if ( ! semi1[-2] ) semi1[-2] = '\'';
	return tzoff;
}
*/

char getTimeZone2 ( char *city , char *state , char *useDST ) {
	// get the words
	//Words ww; ww.set3 ( city );
	// int16_tcut
	//int64_t *wids = ww.m_wordIds;
	// limit hash
	//int32_t count = 0;
	// get city hash
	int64_t h = getWordXorHash(city);
	// TODO: make state into two letter abbr?
	// crap, if state is taken from class ZipDesc it is only
	// 2 letters and has no \0 in it
	//if ( gbstrlen(state) != 2 ) { char *xx=NULL;*xx=0; }
	// use this now
	uint32_t cid32 = (uint64_t)getCityId32(h,state);
	// and call this
	return getTimeZone3 ( cid32 , useDST );
}

char getTimeZone3 ( uint32_t cid32 , char *useDST ) {
	// now lookup timezone
	int32_t slot = g_timeZones.getSlot ( &cid32 );//&cityStateHash );
	// return 0 if not found
	if ( slot < 0 ) {
		log("addr: gettimezone3: unknown timezone");
		return UNKNOWN_TIMEZONE;
		// Denver Art Museum;;;100 West 14th Avenue Parkway;Denver's;
		// co;;;1993583704;173.203.24.218;;;
	}
	// otherwise, set m_timeZoneOffset appropriately
	CityStateDesc *csd=(CityStateDesc *)g_timeZones.getValueFromSlot(slot);
	*useDST = csd->m_useDST;
	// sanity corruption check
	if ( *useDST != 0 && *useDST != 1 ) { char *xx=NULL;*xx=0; }
	char tz = csd->m_timeZoneOffset;
	if ( tz < -13 || tz > 13 ) { char *xx=NULL;*xx=0; }
	return tz;
}


// . for now just get the closest city to the user and use that timezone
// . this is not 100% accurate but should be like 99.9%
// . no, just use the GeoCityLite.dat call, that returns the city/state already
char getTimeZoneFromUserIP ( int32_t uip , int32_t niceness , char *useDST ) {
	double lat;
	double lon;
	double radius;
	char *city,*state,*ctry;
	// use this by default
	//int32_t ip = r->m_userIP;
	// ip for testing?
	//int32_t iplen;
	//char *ips = r->getString("uip",&iplen);
	//if ( ips ) ip = atoip(ips);
	// returns true if found in db
	char buf[128];
	getIPLocation ( uip ,
			&lat , 
			&lon , 
			&radius,
			&city ,
			&state ,
			&ctry  ,
			buf    ,
			128    ) ;
	// 999 means unknown timezone offset
	if ( ! city || ! state ) {
		log("addr: got unknown timezone for user");
		return UNKNOWN_TIMEZONE;
	}
	// get timezone offset from this
	return getTimeZone2 ( city , state , useDST );
}

// used by SearchInput.cpp to get timezone of the user from user's lat/lon 
char getTimeZoneFromLatLon ( float lat,float lon,int32_t niceness,char *useDST ) {
	// get nearest city/state
	float distInMilesSquared = 0.0;
	uint32_t cid32 = getNearestCityId ( lat , lon , niceness , 
					    &distInMilesSquared );
	if ( distInMilesSquared > 1000 ) 
		cid32 = 0;
	// then its easy
	return getTimeZone3 ( cid32 , useDST );
}

static int32_t *s_latList = NULL;
static int32_t  s_latListSize = 0;
//static int32_t *s_lonList = NULL;
static int32_t       s_ni  = 0;

// . we need a list of the city ids sorted by lat, and a list sorted by lon
// . then we do b-stepping on each list
// . bstep down to a 20 mile by 20 mile box
// . then intersect using a hashtable
// . if empty, then increase to 30 by 30 mile box, etc.
// . there are 123k US cities in cities.dat
// . these 2 lists should be about 2MB then
// . then lookup cityid in g_timezones to get timezone
uint32_t getNearestCityId ( float lat , 
			    float lon , 
			    int32_t niceness ,
			    float *distInMilesSquared ) {

	// radius is 5 miles, put miles into degrees
	float radius = 5.0 / 69.0;
	CityStateDesc *csd;

 tryagain:

	int32_t step = s_ni / 2;
	// get lat boundaries using bstep
	int32_t start = s_ni / 2;
	// do the bstepping
	for ( ; ; ) {
		// get that city
		int32_t citySlot = s_latList[start];
		// get csd
		csd = (CityStateDesc *)g_timeZones.getValueFromSlot(citySlot);
		if ( ! csd ) { char *xx=NULL;*xx=0; }
		// increase resolution for next round
		step /= 2;
		//if ( step <= 0 ) step = 1;
		// step it down?
		if      ( lat < csd->m_latitude ) start -= step;
		// use " - radius" here as well to avoid infinite loop?
		else if ( lat > csd->m_latitude ) start += step;
		// ok, we are in range, done
		else break;
		// avoid breaching!
		if ( start < 0     ) { start = 0     ; break; }
		if ( start >= s_ni ) { start = s_ni-1; break; }
		// stop if we hit steps of 0
		if ( step <= 0 ) break;
		// if step was 0 and we failed, than need to increase radius
		//if ( step > 0 ) continue;
		// ok, we failed, we will increase radius below and try again
		// increase stripe width
		//radius += 5.0;
		// try again
		//goto tryagain;
	}

	//getCityRange ( s_latList , lat , radius , &lata , &latb );
	//getCityRange ( s_lonList , lon , radius , &lona , &lonb );
	// now take intersection of the ranges
	//int32_t numCities = lata - latb;
	//HashTableX ih;
	//if(! ih.set ( 4 , 0 , numCities , ihbuf, 3000 , false , niceness )){
	//	char *xx=NULL;*xx=0; }

	int32_t lata = start;
	int32_t latb = start;
	int32_t count = 0;
	// TODO: do b-step on these too, takes like 3500 iterations for
	//       both of these loops
	// adjust lata/latb until just out of range
	for ( ; lata > 0 ; lata-- ) {
		// get csd
		int32_t slot = s_latList[lata];
		csd = (CityStateDesc *)g_timeZones.getValueFromSlot(slot);
		if ( csd->m_latitude < lat - radius ) break;
		count++;
	}
	for ( ; latb < s_ni ; latb++ ) {
		// get csd
		int32_t slot = s_latList[latb];
		csd = (CityStateDesc *)g_timeZones.getValueFromSlot(slot);
		if ( csd->m_latitude > lat + radius ) break;
		count++;
	}

	float min = -1.0;
	int32_t  minSlot = -1;
	// add in the lat cities
	for ( int32_t i = lata ; i <= latb ; i++ ) {
		// break?
		if ( i >= s_ni ) break;
		// breathe
		QUICKPOLL(niceness);
		// get that city
		int32_t citySlot = s_latList[i];
		// get cd
		CityStateDesc *csd;
		csd = (CityStateDesc *)g_timeZones.getValueFromSlot(citySlot);
		// just compute distance
		float latDiff = csd->m_latitude  - lat;
		float lonDiff = csd->m_longitude - lon;
		// add up
		float dist = latDiff*latDiff + lonDiff*lonDiff;
		// min?
		if ( dist > min && minSlot >= 0 ) continue;
		// set it
		min     = dist;
		minSlot = citySlot;
	}

	// must have one
	if ( minSlot == -1 ) {
		// note it
		log("addr: what the hell.");
		// increase stripe width
		radius += 10.0;
		// try again
		goto tryagain;
	}

	if ( distInMilesSquared ) *distInMilesSquared = min;

	uint32_t *cidp = (uint32_t *)g_timeZones.getKeyFromSlot(minSlot);

	// get that then
	return *cidp;
}


int latcmp ( const void *arg1 , const void *arg2 ) {
	int32_t slot1 = *(int32_t *)arg1;
	int32_t slot2 = *(int32_t *)arg2;
	// get the addresses
	CityStateDesc *cd1;
	CityStateDesc *cd2;
	cd1 = (CityStateDesc *)g_timeZones.getValueFromSlot(slot1);
	cd2 = (CityStateDesc *)g_timeZones.getValueFromSlot(slot2);
	// simple compare
	if ( cd1->m_latitude < cd2->m_latitude ) return -1;
	if ( cd1->m_latitude > cd2->m_latitude ) return  1;
	return 0;
}

//int loncmp ( const void *arg1 , const void *arg2 ) {
//	// get the addresses
//	CityDesc *cd1 = *(CityDesc **)arg1;
//	CityDesc *cd2 = *(CityDesc **)arg2;
//	// simple compare
//	return ( cd1->m_longitude - cd2->m_longitude );
//}

// . our data is used by getNearestCityId
// . about 123k cities, sort them by lat in one list, lon in the other
// . 4 bytes per entry, we are talking 1.2MB for both lists
bool initCityLists ( ) {
	// scan city table
	int32_t ns = g_timeZones.m_numSlots;
	// need this
	int32_t used = g_timeZones.m_numSlotsUsed;
	// how much space to alloc?
	int32_t need = used * 4;
	// alloc it
	char *space = (char *)mmalloc(need,"latlist");
	if ( ! space ) return false;
	char *p = space;
	s_latList = (int32_t *)p;
	s_latListSize = need;
	//p += 4 * used;
	//s_lonList = (CityDesc **)p;
	// reset
	s_ni = 0;
	// scan the slots
	for ( int32_t i = 0 ; i < ns ; i++ ) {
		// skip empties
		if ( ! g_timeZones.m_flags[i] ) continue;
		// get it
		CityStateDesc *csd;
		csd = (CityStateDesc *)g_timeZones.getValueFromSlot(i);
		// add to the list
		s_latList[s_ni] = i;
		//s_lonList[s_ni] = cd;
		s_ni++;
	}
	// now sort each list
	gbqsort ( s_latList , s_ni , 4 , latcmp , 0 );
	//gbqsort ( s_lonList , s_ni , 4 , loncmp , 0 );
	return true;
}

char Address::getTimeZone ( char *useDST ) {

	// need this
	char *adm1Str = NULL;
	char *cityStr = NULL;
	if      ( m_adm1 ) adm1Str = m_adm1->m_adm1;
	else if ( m_zip  ) { 
		cityStr = m_zip->m_cityStr;
		adm1Str = m_zip->m_adm1;
	}
	else if ( m_city && m_city->m_adm1[0] ) {
		adm1Str = m_city->m_adm1;
	}
	// this sets m_cityId32 to the nearest city to the lat/lon
	else if ( (m_flags3 & AF2_LATLON) && m_cityId32 ) ;
	// if we failed to set city id because no city was nearby
	// then just guess based on lat/lon
	else if ( m_flags3 & AF2_LATLON ) {
		// ASSUME THEY USE IT! WE DON'T KNOW REALLY!!
		if ( useDST ) *useDST = 1;
		char timeZone = (char)(int32_t)(m_longitude / (360.0/24.0));
		if ( timeZone < -12 || timeZone > 12 ) { char *xx=NULL;*xx=0;}
		return timeZone;
	}
	else    { char *xx=NULL;*xx=0; }
	// normalize this
	//char aa[3];
	//aa[0] = to_lower_a(adm1Str[0]);
	//aa[1] = to_lower_a(adm1Str[1]);
	//aa[2] = 0;
	// hash state hash
	//uint32_t adm1Hash32 = (uint32_t)*((uint16_t *)aa);
	//uint32_t cityHash32 = (uint32_t)m_cityHash;
	// combine the two hashes
	//uint32_t cityStateHash = hash32h(cityHash32,adm1Hash32);

	// use this now
	//uint32_t cid32 = (uint32_t)m_cityId64;
	
	// now lookup timezone
	int32_t slot = g_timeZones.getSlot ( &m_cityId32 );
	// return 0 if not found
	if ( slot < 0 ) {
		// nte it
		if ( cityStr && adm1Str ) {
			log("addr: could not find timezone in g_timezones, "
			    "trying to call getTimeZone2");
			char tzoff = getTimeZone2 ( cityStr, adm1Str, useDST );
			if ( tzoff != UNKNOWN_TIMEZONE )
				return tzoff;
		}
		log("addr: got unknown timezone for addr");
		*useDST = 1;
		return UNKNOWN_TIMEZONE;
	}
	// otherwise, set m_timeZoneOffset appropriately
	CityStateDesc *csd;
	csd = (CityStateDesc *)g_timeZones.getValueFromSlot(slot);
	char tzoff = csd->m_timeZoneOffset;
	if ( tzoff < - 13 || tzoff > 13 ) { char *xx=NULL;*xx=0; }
	*useDST = csd->m_useDST;
	return tzoff;
}

/*
bool Addresses::addToTagRec ( TagRec *gr , int32_t ip , int32_t timestamp ,
			      char *origUrl , int32_t maxAddrBytes ,
			      char *tagName ) {
	// inherit Places that all the Addresses in the list agree on
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// get it
		Address *ai = (Address *)m_am.getPtr(i);
		// do not add this to tagdb if not inlined!
		if ( ! ( ai->m_flags & AF_INLINED ) ) continue;
		// add address #i
		if ( ! ai->addToTagRec (gr,ip,timestamp,origUrl,
					maxAddrBytes,tagName) ) 
			return false;
	}
	return true;
}

// can xmldoc use this for venue addresses?
bool Address::addToTagRec ( TagRec *gr , int32_t ip , int32_t timestamp ,
			    char *origUrl , int32_t maxAddrBytes ,
			    char *tagName ) {

	//
	// we are no longer storing contact info addresses
	//
	//return true;

	// use ; as delimter
	char buf[5003];
	// . size includes the terminating \0
	// . include the Address::m_hash for deduping in XmlDoc.cpp
	int32_t size = serialize ( buf , 5000 , origUrl , false , true );
	// returns -1 and sets g_errno on error
	if ( size < 0 ) return false;

	//
	// point to end of data excluding the origUrl for deduping
	//
	char *end1 = buf + size - 1;
	for ( ; end1 > buf && *end1 != ';' ; end1-- ) ;
	// the length without that
	int32_t len1 = end1 - buf;

	//
	// how many address bytes are we using currently? only need to 
	// compute this if we have a limit, i.e. "maxAddrBytes" >= 0
	//
	// count those bytes
	int32_t used = 0;
	if ( maxAddrBytes >= 0 ) {
		// our tag type
		int32_t tt = getTagTypeFromStr ( tagName );//"contactaddress" );
		// taken from TagRec::getTag() function
		Tag *tag = gr->getFirstTag();
		// loop over all contact info addresses in the TagRec
		for ( ; tag ; tag = gr->getNextTag(tag) ){
			// skip if not a "address" tag (ci=contactInfo)
			if ( tag->m_type != tt ) continue;
			// get str
			used += tag->m_dataSize;
			// point to end of data excluding the origUrl for
			// deduping contact addresses in the tag rec
			char *end2 = tag->m_data + tag->m_dataSize - 1;
			for ( ; end2 > tag->m_data && *end2 != ';' ; end2-- ) ;
			// get lengths
			int32_t len2 = end2 - tag->m_data;
			// is it a dup?
			if ( len1 != len2 ) continue;
			if ( memcmp(tag->m_data, buf, len1 ) ) continue;
			// it was a dup!
			return true;
		}
	}
	// can we fit it? if not, do not add it
	if ( maxAddrBytes >= 0 && used + size > maxAddrBytes ) return true;

	// store it
	//int32_t now = getTimeGlobal();
	// returns false and sets g_errno on error
	return gr->addTag (tagName,timestamp,"xmldoc",ip,buf,size);
}
*/

// . hash city and state together then lookup in g_timeZones table
// . name1;name2;suite;street;city;adm1;zip;country;domhash;ip;origurl;lat;lon;hash\0
// . uint32_t getCityHash32 ( char *addr , uint32_t *adm1Hash ) {
uint64_t getHashFromAddr ( char *addr ) {
	char *p = addr;
	int32_t semiCount = 0;
	for ( ; *p ; p++ ) {
		// skip if not border
		if ( *p != ';' ) continue;
		// inc it
		semiCount++;
		// hash?
		if ( semiCount != 13 ) continue;
		// got it
		break;
	}
	// none?
	if ( ! *p ) { char *xx=NULL;*xx=0; }
	// skip semi
	p++;
	// must be digit
	if ( ! is_digit(*p) ) { char *xx=NULL;*xx=0; }
	// get that value
	uint64_t ah = strtoull(p,NULL,10);//atoll(p);
	// that's what we want
	return ah;
}

// . used by Address::serialize
// . filter out back to back spaces
// . covert \n and \t to ' '
int32_t memcpy2 ( char *dst , char *src , int32_t bytes , bool filterCommas ,
	       int32_t dstMaxBytes ) {
	char *srcEnd = src + bytes;
	// do not start with a space, so set this to 1
	char lastWasSpace = 1;
	char *dstStart = dst;
	char fc = ' ';
	if ( filterCommas ) fc = ',';
	bool inTag = false;
	char *dstEnd = NULL;
	if ( dstMaxBytes >= 0 ) dstEnd = dstStart + dstMaxBytes;
	char cs ;
	//if ( src[0]=='G' && src[1]=='o' && src[2]=='n' )
	//	log("hey");
	for ( ; src < srcEnd ; src += cs ) {
		// set it
		cs = getUtf8CharSize(src);
		// remove tags
		if ( *src == '<' )  {
			inTag = true;
			// skip if bold tag
			if ( to_lower_a(src[1])=='b' && src[2]=='>' ) continue;
			// skip if italic
			if ( to_lower_a(src[1])=='i' && src[2]=='>' ) continue;
			// skip if already had printed space
			if ( lastWasSpace ) continue;
			// otherwise print the space
			*dst++ = ' ';
			// and set this flag
			lastWasSpace = true;
			continue;
		}
		if ( *src == '>' ) { inTag = false; continue;}
		if ( inTag ) continue;
		// . when serializing address semicolons have special meaning
		// . deal special with spaces. treat comma as a space too now!
		if ( is_wspace_utf8 (src) || *src == fc || *src == ';' ) { 
			// stop if would breach
			if ( dstEnd && dst + 1 > dstEnd ) break;
			if ( ! lastWasSpace ) *dst++ = ' ';//*src;
			lastWasSpace = 1;
			continue;
		}
		// reset
		lastWasSpace = 0;
		// stop if would breach
		if ( dstEnd && dst + cs > dstEnd ) break;
		// everything else
		if( cs == 1 ) { *dst++ = *src; continue; }
		// otherwise characters is > 1 byte
		gbmemcpy ( dst , src , cs );
		dst += cs;
	}
	// return bytes written
	return dst - dstStart;
}

// "olen" is length of origUrl to be stored
int32_t Address::getStoredSize ( int32_t ulen , bool includeHash ) {
	// how much buffer space do we need?
	int32_t need = 0;
	if ( m_name1  ) need += m_name1 ->m_strlen + 1;
	if ( m_name2  ) need += m_name2 ->m_strlen + 1;
	if ( m_suite  ) need += m_suite ->m_strlen + 1;
	if ( m_street ) need += m_street->m_strlen + 1;
	if ( m_city   ) need += m_city  ->m_strlen + 1;
	if ( m_zip    ) need += m_zip   ->m_strlen + 1;
	if ( m_adm1   ) need += m_adm1  ->m_strlen + 1;
	//if ( m_ctry   ) need += m_ctry  ->m_strlen + 1;
	// if city our adm1 or country is NULL, guess because it
	// will be looked up and supplied based on lat/lon
	if ( ! m_city ) need += 64 + 1;
	if ( ! m_adm1 ) need += 2 + 1;
	// country!
	need += 3;
	// country is now just ;
	//need++;
	// domainhash
	need += 10 + 1; 
	// ip string
	need += 16;
	// this includes the "..." of truncated urls
	need += ulen;
	// latitude
	need += 12;
	// longitude
	need += 12;
	// address hash -- printing out a uint64_t in ascii
	// 18446744073709551615LL = 20 digits + semicolon before it
	need += 21;
	// null term
	need++;
	// timezoneoffset
	//need += 4;
	return need;
}

bool Address::serializeVerified ( SafeBuf *sb ) {
	// get min # of bytes needed
	int32_t need = getStoredSize ( 0 , false );
	// make room
	if ( ! sb->reserve ( need ) ) return false;
	// store it here
	char *buf = sb->getBuf();
	// do it
	int32_t written = serialize ( buf , need , NULL , true , false );
	// sanity check
	if ( written > need ) { char *xx=NULL;*xx=0; }
	// update it
	sb->incrementLength ( written );
	// success
	return true;
}

// . returns -1 and sets g_errno on error
// . name1;name2;suite;street;city;adm1;zip;country;domHash32;ipStr;url;lat;lon;addHash
// . setfromstr() above
int32_t Address::serialize ( char *buf , int32_t bufSize , char *origUrl ,
			  bool verifiedOnly , bool includeHash ) {

	char *p    = buf;

	// sanity check. these should be filtered out
	//if ( m_score <= 0.0 ) { char *xx=NULL;*xx=0; }

	// also truncate at semicolon in urls since that is our delimeter
	char *o = origUrl;
	for ( ; o && *o && *o !=';' ; o++ );
	// truncate this if we should
	int32_t olen = o - origUrl; // gbstrlen(origUrl);
	bool trunc = false;
	if ( olen > 128 ) { olen = 96; trunc = true; }
	// if a semicolon kicked us out, we were truncated as well
	else if ( o && *o == ';' ) trunc = true;
	// include ...
	int32_t extra = 0;
	if ( trunc ) extra = 3;

	// how much buffer space do we need?
	int32_t need = getStoredSize( olen + extra , includeHash );

	// silenty ignore overflow errors
	if ( need > bufSize ) return -1;

	PlaceDesc *pd = NULL;
	// guess the city/state names if we got lat/lon only
	if ( m_flags3 & AF2_LATLON ) {
		float distInMilesSquared = 0.0;
		pd = getNearestCity_new ( m_latitude ,
					  m_longitude ,
					  0 , // niceness 
					  &distInMilesSquared );
		if ( distInMilesSquared >= 1000 ) pd = NULL;
	}


	Place *d ;

	char flags = m_flags;
	if ( ! verifiedOnly ) flags |= AF_VERIFIED_PLACE_NAME_1;
	if ( ! verifiedOnly ) flags |= AF_VERIFIED_PLACE_NAME_2;

	d = m_name1;
	if ( d && (flags & AF_VERIFIED_PLACE_NAME_1) ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		// should also remove semicolons
		p += memcpy2(p,d->m_str,d->m_strlen,false);
	}
	*p++ = ';';

	d = m_name2;
	if ( d && (flags & AF_VERIFIED_PLACE_NAME_2) ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		p += memcpy2(p,d->m_str,d->m_strlen,false);
	}
	*p++ = ';';

	d = m_suite;
	if ( d ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		p += memcpy2(p,d->m_str,d->m_strlen,true);
	}
	*p++ = ';';

	d = m_street;
	if ( d ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		p += memcpy2(p,d->m_str,d->m_strlen,true);
	}
	*p++ = ';';

	d = m_city;
	if ( d ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		p += memcpy2(p,d->m_str,d->m_strlen,true);
		// append the adm1 code
		//if ( d->m_adm1[0] ) {
		//	*p++ = '(';
		//	gbmemcpy(p,d->m_adm1,2);
		//	p += 2;
		//	*p++ = ')';
		//}
	}
	// if city is NULL it must be implied from zip code
	else if ( m_zip ) {
		char *cs = m_zip->m_cityStr;
		if ( gbstrlen(cs) == 0 ) { char *xx=0;*xx=0; }
		p += memcpy2(p,cs,gbstrlen(cs),true);
	}
	else if ( m_flags3 & AF2_LATLON ) {
		if ( pd ) {
			char *str = pd->m_officialNameOffset + g_pbuf;
			int32_t slen = gbstrlen(str);
			// limit to 64 since that is getStoredSize() number
			if ( slen > 64 ) slen = 64;
			gbmemcpy ( p , str ,slen );
			p += slen;
		}
	}
	// otherwise, we have an issue, it must be impliable
	else {
		char *xx=NULL;*xx=0;
	}
	*p++ = ';';

	// mdw mdw
	d = m_adm1;
	if ( d ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		//p += memcpy2(p,d->m_str,d->m_strlen,true);
		// to save space use two letter abbr
		p += memcpy2(p,d->m_adm1,2,true);
		// append the adm1 code
		//if ( d->m_adm1[0] ) {
		//	*p++ = '(';
		//	gbmemcpy(p,d->m_adm1,2);
		//	p += 2;
		//	*p++ = ')';
		//}
	}
	// if city is NULL it must be implied from zip code
	else if ( m_zip ) {
		p += memcpy2(p,m_zip->m_adm1,2,true);
	}
	// imply from city if city is unique
	//else if ( m_city && (m_city->m_adm1Bits & CF_UNIQUE) ) {
	//	p += memcpy2(p,m_city->m_adm1,2,true);
	//}
	else if ( m_flags3 & AF2_LATLON ) {
		// this is the nearest city's state based on our lat/lon
		if ( pd && pd->m_adm1[0] && pd->m_adm1[1] ) {
			gbmemcpy ( p , pd->m_adm1 ,2 );
			p += 2;
		}
	}
	// otherwise, we have an issue, it must be impliable
	else {
		char *xx=NULL;*xx=0;
	}
	*p++ = ';';

	d = m_zip;
	if ( d ) { 
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		p += memcpy2(p,d->m_str,d->m_strlen,true);
		// append the adm1 code
		//if ( d->m_adm1[0] ) {
		//	*p++ = '(';
		//	gbmemcpy(p,d->m_adm1,2);
		//	p += 2;
		//	*p++ = ')';
		//}
	}
	*p++ = ';';

	// use country code from "crid"
	//char *cn = (char *)g_countryCode.getAbbr(m_adm1->m_crid-1);
	//if ( cn ) { 
	//	gbmemcpy(p,cn,gbstrlen(cn));
	//	p += gbstrlen(cn);
	//}
	if ( m_flags3 & AF2_LATLON ) {
		if ( pd && pd->m_crid ) {
			char *cc = getCountryCode(pd->m_crid);
			gbmemcpy ( p , cc , 2 );
			p += 2;
		}
	}
	*p++ = ';';


	// sanity check
	if ( m_domHash32 == 0 ) { char *xx=NULL;*xx=0; }
	// serialize 32-bit domain hash
	p += sprintf( p , "%"UINT32"", m_domHash32 );
	*p++ = ';';

	// sanity check
	if ( m_ip == 0 || m_ip == -1 ) { char *xx=NULL;*xx=0;}
	// serialize ip string
	p += sprintf( p , "%s", iptoa(m_ip));
	*p++ = ';';


	if ( origUrl ) {
		// bytes written may be different than d->m_strlen since
		// memcpy2() filters out back-to-back spaces
		p += memcpy2(p,origUrl,olen,false);
		if ( trunc ) p += memcpy2 (p,"...",3,false);
	}
	*p++ = ';';

	// then latitude
	if ( m_latitude != NO_LATITUDE && m_latitude != AMBIG_LATITUDE )
		p += sprintf(p,"%f",m_latitude);

	*p++ = ';';

	// then longitude
	if ( m_longitude != NO_LONGITUDE && m_longitude != AMBIG_LONGITUDE ) 
		p += sprintf(p,"%f",m_longitude);

	if ( includeHash ) {
		*p++ = ';';
		// finally the address hash in ascii
		p += sprintf ( p , "%"UINT64"" , m_hash );
	}

	// . then timezone off, a single signed byte really
	// . we add 100 to this to signify that it does NOT use DST
	//p += sprintf(p,"%"INT32"", (int32_t)m_timeZoneOffset);

	*p++ = '\0';

	// count the semicolons to make sure data did not insert extra ones
	char *s = buf;
	int32_t semiCount = 0;
	int32_t semiNeed = 12;
	if ( includeHash ) semiNeed++;
	for ( ; *s ; s++ ) if ( *s == ';' ) semiCount++;
	if ( semiCount != semiNeed ) { char *xx=NULL;*xx=0; }

	int32_t size = p - buf;
	// sanity check
	if ( size > bufSize ) { char *xx=NULL;*xx=0; }
	// all done
	return size; 
}


int32_t Address::print ( ) {
	return print2 ( 0,NULL,0);
}

int32_t Address::print2 ( int32_t i , SafeBuf *pbuf , int64_t uh64 ) { 

	// print out each candidate for debug
	SafeBuf sb;

	//bool validAddr = ( (m_flags) & AF_INLINED );
	// old sanity checker to ensure div ids were unique
	//static bool s_init = false;
	//static HashTableX ht;
	//if ( ! s_init ) {
	//	s_init = true;
	//	ht.set ( 4 , 4 , 128 , NULL , 0 , false , 2 );
	//}
	//if ( validAddr ) {
	//	if ( ht.isInTable ( &m_divId) ) { char *xx=NULL;*xx=0; }
	//	ht.addKey ( &m_divId );
	//}

	// print out to a table?
	if ( pbuf ) {
		// dump it
		// . for the sake of doing delta diffs in Test.cpp
		//   eliminate the number!
		//pbuf->safePrintf ( "<td>%"INT32"/%"INT32"</td>", num ,m_street.m_a);
		//if ( m_street.m_a >= 0 )
		//	pbuf->safePrintf ( "<td>%"INT32"</td>", m_street.m_a);
		//else
		int32_t napos = -1;
		if ( m_name1 ) napos = m_name1->m_a;

		int32_t stra = -1;
		if ( m_street ) stra = m_street->m_a;
		pbuf->safePrintf ( "<td>%"INT32"/%"INT32"</td>", napos,stra );
		
		//pbuf->safePrintf ( "<td>%.06f</td>", m_score );
		//pbuf->safePrintf("<td>0x%"XINT32"</td>", m_section->m_tagHash);

		printEssentials ( pbuf , false , uh64 );

		// print flags
		pbuf->safePrintf("<td><nobr>");
		//if ( (m_flags) & AF_IGNORE )
		//	pbuf->safePrintf("ignore ");
		if ( m_flags & AF_VENUE_DEFAULT ) 
			pbuf->safePrintf("venueaddress ");
		if ( (m_flags) & AF_INLINED )
			pbuf->safePrintf("inlined ");
		else
			pbuf->safePrintf("notinlined ");
		if ( m_alias )
			pbuf->safePrintf("alias[a=%"INT32"] ",
					 m_alias->m_street->m_a);

		if ( m_flags3 & AF2_HAS_REQUIRED_CITY )
			pbuf->safePrintf("requiredcity ");
		if ( m_flags3 & AF2_HAS_REQUIRED_STATE )
			pbuf->safePrintf("requiredstate ");
		if ( m_street && (m_street->m_flags2 & PLF2_COLLISION) )
			pbuf->safePrintf("streetcollision ");

		// means that we are inlined and the city FOLLOWS the state
		//if ( (m_flags) & AF_BADORDER )
		//	pbuf->safePrintf("badorder ");
		if ( (m_flags) & AF_AMBIGUOUS ) 
			pbuf->safePrintf("ambig ");
		if ( (m_flags3) & AF2_BADCITYSTATE ) 
			pbuf->safePrintf("badcitystate ");
		if ( (m_flags) & AF_VERIFIED_STREET ) 
			pbuf->safePrintf("verifiedstreet ");
		if ( (m_flags) & AF_VERIFIED_STREET_NUM ) 
			pbuf->safePrintf("verifiedstreetnum ");
		if ( (m_flags) & AF_VERIFIED_PLACE_NAME_1 ) 
			pbuf->safePrintf("verifiedplacename1 ");
		if ( (m_flags) & AF_VERIFIED_PLACE_NAME_2 ) 
			pbuf->safePrintf("verifiedplacename2 ");

		if ( m_street &&(m_street->m_flags3 & PLF3_SUPPLANTED))
			pbuf->safePrintf("<b>supplanted</b> ");
		if ( m_street &&(m_street->m_flags3 & PLF3_LATLONDUP))
			pbuf->safePrintf("<b>latlondup</b> ");

		if ( m_street &&(m_street->m_flags2 & PLF2_INTERSECTION) )
			pbuf->safePrintf("intersection ");
		if ( m_street &&(m_street->m_flags2 & PLF2_IS_NAME ))
			pbuf->safePrintf("streetisname ");
		if ( m_street &&(m_street->m_flags2 & PLF2_AFTER_AT) )
			pbuf->safePrintf("afterat ");
		if ( m_street &&(m_street->m_flags2 & PLF2_TICKET_PLACE) )
			pbuf->safePrintf("ticketplace ");
		// when the event hours are not "store hours" we flag the
		// place name so as to avoid it as the event title in
		// Events.cpp
		//if ( m_name1 && (m_name1->m_flags2 & PLF2_STORE_NAME) )
		//	pbuf->safePrintf("storename ");

		//if ( (m_flags) & AF_VERIFIED_STREET_IND )
		//	pbuf->safePrintf("verifiedstreetind ");
		if ( !(m_flags) )
			pbuf->safePrintf("&nbsp;");
		pbuf->safePrintf("</nobr></td>");

		// print the address ptr, but make it an offset so
		// it doesn't show up on the test qa run diffs
		//int32_t offset = this - base;
		int32_t offset = i;
		pbuf->safePrintf("<td>%"UINT32"</td>",(int32_t)offset);

		pbuf->safePrintf("<td><nobr>0x%"XINT64" (%"INT32")</nobr></td>",
				 m_hash,m_score2);


		// print placedb names
		pbuf->safePrintf("<td><nobr>");
		char *s    = m_placedbNames;
		char *send = m_placedbNamesEnd;
		// scan them
		for ( ; s && s < send ; ) {
			// skip score
			s += 4;
			// empty? strange...
			if ( ! *s ) { char *xx=NULL;*xx=0; }
			if ( s > m_placedbNames + 4 )
				pbuf->pushChar(',');
			// print that
			pbuf->safePrintf("%s",s);
			// skip that and the \0
			s += gbstrlen(s) + 1;
		}
		pbuf->safePrintf("</nobr></td>");

		// adm1
		char *adm1Str = "\0\0";
		if      ( m_adm1 ) adm1Str = m_adm1->m_adm1;
		else if ( m_zip  ) adm1Str = m_zip->m_adm1;
		//else if ( m_city && m_city->m_adm1[0] )
		//	adm1Str = m_city->m_adm1;
		else if ( m_flags3 & AF2_LATLON );
		else  { char *xx=NULL;*xx=0; }
		// city
		int64_t cityHash = 0LL;
		if      ( m_city ) cityHash = m_city->m_hash;
		else if ( m_zip  ) cityHash = m_zip->m_cityHash;
		else if ( m_flags3 & AF2_LATLON );
		else  { char *xx=NULL;*xx=0; }
		uint32_t cityId = getCityId32(cityHash,adm1Str);
		// ripped from XmlDoc.cpp placedb logic
		key128_t *k2      = &m_placedbKey;
		int64_t bigHash = g_placedb.getBigHash       ( k2 );
		int64_t docId   = g_placedb.getDocId         ( k2 );
		int32_t      snh     = g_placedb.getStreetNumHash ( k2 );
		int64_t nh1 = 0;
		int64_t nh2 = 0;
		if ( m_name1 ) nh1 = m_name1->m_hash;
		if ( m_name2 ) nh2 = m_name2->m_hash;
		int64_t strh = 0LL;
		if ( m_street ) strh = m_street->m_hash;
		pbuf->safePrintf("<td><nobr>"
				 "k.n1=0x%16"XINT64" n0=0x%16"XINT64" "
				 //"addrhash=0x%"XINT64" "
				 "bigHash64=0x%016"XINT64" "
				 "docId=%"UINT64" "
				 "streetNumHash25=0x%08"XINT32" "
				 "cityHash=0x%016"XINT64" "
				 "cityId=0x08%"XINT32" "
				 "streetHash=0x%016"XINT64" "
				 "adm1Hash=0x%04"XINT32" "
				 "name1Hash=0x%016"XINT64" "
				 "name2Hash=0x%016"XINT64" "
				 "</nobr>"
				 "</td>"
				 ,
				 k2->n1 , k2->n0 ,
				 //m_hash,
				 bigHash,
				 docId,
				 snh ,
				 cityHash,//m_city->m_hash,
				 (int32_t)cityId,
				 strh, // m_street->m_hash,
				 (int32_t)*(uint16_t *)adm1Str,
				 nh1,nh2
				 );
		

		/*
		char *b1 = "&nbsp;";
		char *b2 = "&nbsp;";
		char *b3 = "&nbsp;";
		if ( m_flags & AF_VERIFIED_STREET     ) b1 = "yes";
		if ( m_flags & AF_VERIFIED_STREET_NUM ) b2 = "yes";
		if ( m_flags & AF_VERIFIED_PLACE_NAME ) b3 = "yes";
		pbuf->safePrintf("<td>%s</td>",b1);
		pbuf->safePrintf("<td>%s</td>",b2);
		pbuf->safePrintf("<td>%s</td>",b3);
		*/

		/*
		pbuf->safePrintf("<td>%.02f</td>",
				 m_scoreBase);
		pbuf->safePrintf("<td>%.02f</td>",
				 m_scoreNameBeforeStreet);
		pbuf->safePrintf("<td>%.02f</td>",
				 m_scoreDistanceNameToStreet);
		pbuf->safePrintf("<td>%.02f</td>",
				 m_scoreOldVoteMod);
		pbuf->safePrintf("<td>%.02f</td>",
				 m_scoreNewVoteMod);
		pbuf->safePrintf("<td>%.02f</td>",
				 m_scoreDistanceNameToStreetValue);
		*/

		// wrap up the table row
		pbuf->safePrintf ( "</tr>\n");

		return 1;
	}


	if ( m_name1 ) {
		sb.safePrintf("name1=");
		sb.safeMemcpy(m_name1->m_str,m_name1->m_strlen);
	}
	if ( m_name2 && m_name2->m_str ) {
		sb.safePrintf(" name2=");
		sb.safeMemcpy(m_name2->m_str,m_name2->m_strlen);
	}
	if ( m_street ) {
		sb.safePrintf(" street[%"INT32"]=",m_street->m_a);
		sb.safeMemcpy(m_street->m_str,m_street->m_strlen);
	}
	//if ( m_zip ) {
	//	sb.safePrintf(" zip=");
	//	sb.safeMemcpy(m_zip->m_str,m_zip->m_strlen);
	//}
	if ( m_suite ) {
		sb.safePrintf(" suite=");
		sb.safeMemcpy(m_suite->m_str,m_suite->m_strlen);
	}
	if ( m_city ) {
		sb.safePrintf(" city[%"INT32"]=",m_city->m_a);
		sb.safeMemcpy(m_city->m_str,m_city->m_strlen);
	}
	if ( m_adm1 ) {
		sb.safePrintf(" adm1[%"INT32"]=",m_adm1->m_a);
		sb.safeMemcpy(m_adm1->m_str,m_adm1->m_strlen);
		sb.pushChar('|');
		sb.safeMemcpy(m_adm1->m_adm1,2);//str,m_adm1->m_strlen);
	}
	if ( m_zip ) {
		sb.safePrintf(" zip=");
		sb.safeMemcpy(m_zip->m_str,m_zip->m_strlen);
	}
	//if ( m_adm2 && m_adm2->m_str ) {
	//	sb.safePrintf(" adm2=");
	//	sb.safeMemcpy(m_adm2->m_str,m_adm2->m_strlen);
	//}
	//if ( m_ctry->m_str ) {
	//	sb.safePrintf(" country=");
	//	sb.safeMemcpy(m_ctry->m_str,m_ctry->m_strlen);
	//}

	sb.safePrintf(" score2=%"INT32"",m_score2);

	sb.safePrintf(" flags=");
	if ( (m_flags) & AF_INLINED )
		sb.safePrintf("inlined ");
	else
		sb.safePrintf("notinlined ");
	// means that we are inlined and the city FOLLOWS the state
	//if ( (m_flags) & AF_BADORDER )
	//	sb.safePrintf("badorder ");
	if ( (m_flags) & AF_AMBIGUOUS ) 
		sb.safePrintf("ambig ");
	if ( (m_flags) & AF_VERIFIED_STREET ) 
		sb.safePrintf("verifiedstreet ");
	if ( (m_flags) & AF_VERIFIED_STREET_NUM ) 
		sb.safePrintf("verifiedstreetnum ");
	if ( (m_flags) & AF_VERIFIED_PLACE_NAME_1 ) 
		sb.safePrintf("verifiedplacename1 ");
	if ( (m_flags) & AF_VERIFIED_PLACE_NAME_2 ) 
		sb.safePrintf("verifiedplacename2 ");
	if ( m_street && (m_street->m_flags2 & PLF2_INTERSECTION ))
		sb.safePrintf("intersection ");
	if ( m_street && (m_street->m_flags2 & PLF2_IS_NAME ))
		sb.safePrintf("streetisname ");
	if ( m_street && (m_street->m_flags2 & PLF2_AFTER_AT ))
		sb.safePrintf("afterat ");

	//sb.safePrintf(" a=%"INT32" b=%"INT32"",m_a,m_b);

	// null term
	sb.safeMemcpy ( "\0",1 );
	//sb.safePrintf(" =");
	//sb.safeMemcpy(m_->m_str,m_->m_strlen);
	//logf(LOG_DEBUG,"events: addr score=%.06f %s",
	logf(LOG_DEBUG,"events: %s",
	     sb.getBufStart() );

	return 1;
}

void Address::printEssentials ( SafeBuf *pbuf , bool forEvents ,
				int64_t uh64 ) {


	pbuf->safePrintf ( "<td><nobr>");

	// . this is for XmlDoc::validateOutput()
	// . we use javascriptEncode() to convert &'s to &amp; since
	//   the javascript escape() function does that before 
	//   converting into a url encoded character for some
	//   reason, which is very annoying!!!! maybe tagInner
	//   does that! yeah, probably, it returns normalized output
	//   as i've seen it reorganize the attributes of html tags.
	if ( uh64 ) {
		pbuf->safePrintf(
			 "<!--ignore-->" // ignore for Test.cpp diff
			 "<span class=validated>"
			 "<input type=checkbox "
			 "onclick=\"senddiv(this,'%"INT64"');\" "
			 "unchecked> "
			 "<div class=validated style=\"display:none\">",
			 // this must be unsigned
			 uh64);
		//char *p = pbuf->getBuf();
		//
		// map utf8 characters into &#xxxx entites because
		// the senddiv() function maps all utuf8 chars to 
		// crap like "%u2019" for the apostrophe for instance
		// 
		if ( m_name1 ) 
		     pbuf->javascriptEncode(m_name1->m_str,m_name1->m_strlen);
		pbuf->pushChar(';');
		if ( m_name2 ) 
		   pbuf->javascriptEncode(m_name2->m_str,m_name2->m_strlen);
		pbuf->pushChar(';');
		if ( m_suite ) 
			pbuf->javascriptEncode(m_suite->m_str,m_suite->m_strlen);
		pbuf->pushChar(';');
		if ( m_street )
			pbuf->javascriptEncode(m_street->m_str,m_street->m_strlen);
		pbuf->pushChar(';');
		if ( m_city ) 
			pbuf->javascriptEncode(m_city->m_str,m_city->m_strlen);
		else if ( m_zip ) 
			pbuf->javascriptEncode(m_zip->m_cityStr,
					       gbstrlen(m_zip->m_cityStr));
		else if ( m_flags3 & AF2_LATLON );
		else { char *xx=NULL;*xx=0; }
		pbuf->pushChar(';');
		// now print adm1 abbr
		char *as = NULL;
		int32_t aslen = 2;
		// mdw mdw
		if ( m_adm1 )
			as = m_adm1->m_adm1;
		else if ( m_zip ) 
			as = m_zip->m_adm1;
		//else if ( m_city &&  (m_city->m_adm1Bits & CF_UNIQUE) )
		//	as = m_city->m_adm1;
		else if ( m_flags3 & AF2_LATLON );
		else { char *xx=NULL;*xx=0; }
		if ( as ) pbuf->javascriptEncode(as,aslen);
		pbuf->pushChar(';');
		if ( m_zip ) 
			pbuf->javascriptEncode(m_zip->m_str,m_zip->m_strlen);
		pbuf->pushChar(';');
		//if ( m_ctry->m_str ) 
		//	pbuf->javascriptEncode(m_ctry->m_str,m_ctry->m_strlen);
		// now we include lat and long, but only if we got both valid
		if ( m_longitude != NO_LONGITUDE &&
		     m_latitude  != NO_LONGITUDE ) {
			pbuf->pushChar(';');
			pbuf->safePrintf("%f",m_latitude);
			pbuf->pushChar(';');
			pbuf->safePrintf("%f",m_longitude);
		}
		// now also check the lat/lon we import
		if ( m_importedLatitude != NO_LATITUDE )
			pbuf->safePrintf(";ilat=%f",m_importedLatitude);
		if ( m_importedLongitude != NO_LONGITUDE )
			pbuf->safePrintf(";ilon=%f",m_importedLongitude);

		//char *pend = pbuf->getBuf();
		pbuf->safePrintf ("\n</div>" );
		pbuf->safePrintf ("</span>" );
	}


	// set these
	int32_t  nameLen1 = 0;
	char *name1 = NULL;
	if ( m_name1 ) {
		name1    = m_name1->m_str;
		nameLen1 = m_name1->m_strlen;
	}
	if ( forEvents && !(m_flags & AF_VERIFIED_PLACE_NAME_1) )
		name1 = NULL;
	if ( forEvents && m_alias ) {
		name1    = m_alias->m_name1->m_str;
		nameLen1 = m_alias->m_name1->m_strlen;
	}
	if ( ! name1 ) {
		name1    = "&nbsp;";
		nameLen1 = gbstrlen(name1);
	}
	
	//pbuf->safePrintf("<td><nobr>");
	if ( m_alias && forEvents ) {
		pbuf->safePrintf("(alias = ");
		// this will have STREET_IS_NAME set so use the street
		// not name 1
		//pbuf->safeMemcpy(m_name1->m_str,m_name1->m_strlen);
		pbuf->safeMemcpy(m_street->m_str,m_street->m_strlen);
		pbuf->safePrintf(") ");
	};
	pbuf->safeMemcpy(name1,nameLen1);
	pbuf->safePrintf("</nobr></td>\n");


	int32_t nameLen2 = 0;
	char *name2 = NULL;
	if ( m_name2 ) {
		nameLen2 = m_name2->m_strlen;
		name2 = m_name2->m_str;
	}
	if ( forEvents && !(m_flags & AF_VERIFIED_PLACE_NAME_2) )
		name2 = NULL;
	if ( forEvents && m_alias ) {
		name2 = m_alias->m_name2->m_str;
		nameLen2 = m_alias->m_name2->m_strlen;
	}
	if ( ! name2 ) {
		name2 = "&nbsp;";
		nameLen2 = gbstrlen(name2);
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy(name2,nameLen2);
	pbuf->safePrintf("</nobr></td>\n");


	int32_t  suiteLen = 0;
	char *suite = NULL;
	if ( m_suite ) {
		suiteLen = m_suite->m_strlen;
		suite = m_suite->m_str;
	}
	if ( forEvents && m_alias ) {
		suite = m_alias->m_suite->m_str;
		suiteLen = m_alias->m_suite->m_strlen;
	}
	if ( ! suite ) {
		suite = "&nbsp;";
		suiteLen = gbstrlen(suite);
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy(suite,suiteLen);
	pbuf->safePrintf("</nobr></td>\n");


	int32_t streetLen = 0;
	char *street = NULL;
	if ( m_street ) {
		streetLen = m_street->m_strlen;
		street = m_street->m_str;
	}
	if ( forEvents && m_alias ) {
		street = m_alias->m_street->m_str;
		streetLen = m_alias->m_street->m_strlen;
	}
	if ( ! street ) {
		street = "&nbsp;";
		streetLen = gbstrlen(street);
	}
	pbuf->safePrintf("<td><nobr>");
	//pbuf->safeMemcpy(street,streetLen);
	// print it right. niceness = 0
	pbuf->htmlEncode ( street,streetLen, true,0);
	pbuf->safePrintf("</nobr></td>\n");


	int32_t cityLen = 0;
	char *city = NULL;
	if ( m_city ) {
		cityLen = m_city->m_strlen;
		city = m_city->m_str;
	}
	if ( forEvents && m_alias ) {
		city = m_alias->m_city->m_str;
		cityLen = m_alias->m_city->m_strlen;
	}
	if ( ! city ) {
		city = "&nbsp;";
		cityLen = gbstrlen(city);
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy(city,cityLen);
	pbuf->safePrintf("</nobr></td>\n");


	
	int32_t adm1Len = 0;
	char *adm1 = NULL;
	if ( m_adm1 ) {
		adm1Len = 2;//m_adm1->m_strlen;
		adm1 = m_adm1->m_adm1;//str;
	}
	if ( forEvents && m_alias ) {
		adm1 = m_alias->m_adm1->m_adm1;//str;
		adm1Len = 2;//m_alias->m_adm1->m_strlen;
	}
	if ( ! adm1 ) {
		adm1 = "&nbsp;";
		adm1Len = gbstrlen(adm1);
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy(adm1,adm1Len);
	pbuf->safePrintf("</nobr></td>\n");


	int32_t zipLen = 0;
	char *zip = NULL;
	if ( m_zip ) {
		zipLen = m_zip->m_strlen;
		zip = m_zip->m_str;
	}
	if ( forEvents && m_alias ) {
		zip = m_alias->m_zip->m_str;
		zipLen = m_alias->m_zip->m_strlen;
	}
	if ( ! zip ) {
		zip = "&nbsp;";
		zipLen = gbstrlen(zip);
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy(zip,zipLen);
	pbuf->safePrintf("</nobr></td>\n");


	pbuf->safePrintf("<td><nobr>");
	/*
	// ctry is special
	char *ctry = m_ctry->m_str;
	if ( forEvents && m_alias ) ctry = m_alias->m_ctry->m_str;
	if ( ! ctry ) {
		Place *cp = &m_adm1;
		char *cn = (char *)g_countryCode.getName(cp->m_crid-1);
		if ( cn ) pbuf->safeMemcpy ( cn,gbstrlen(cn) );
		else pbuf->safePrintf("unknown");
	}
	else
		pbuf->safePrintf("%s",ctry);
	*/
	pbuf->safePrintf("</nobr></td>");

	double lat = m_latitude;
	double lon = m_longitude;

	// geocoder lat/lon
	lat = m_geocoderLat;
	lon = m_geocoderLon;
	pbuf->safePrintf("<td><nobr>");
	if ( lat != NO_LATITUDE && lat != AMBIG_LATITUDE ) 
		pbuf->safePrintf("%f",lat);
	pbuf->safePrintf("</nobr></td>\n");

	pbuf->safePrintf("<td><nobr>");
	if ( lon != NO_LONGITUDE && lon != AMBIG_LONGITUDE )
		pbuf->safePrintf("%f",lon);
	pbuf->safePrintf("</nobr></td>\n");

	// then lat/lon
	lat = m_latitude;
	lon = m_longitude;
	pbuf->safePrintf("<td><nobr>");
	if ( lat != NO_LATITUDE && lat != AMBIG_LATITUDE ) 
		pbuf->safePrintf("%f",lat);
	pbuf->safePrintf("</nobr></td>\n");

	pbuf->safePrintf("<td><nobr>");
	if ( lon != NO_LONGITUDE && lon != AMBIG_LONGITUDE )
		pbuf->safePrintf("%f",lon);
	pbuf->safePrintf("</nobr></td>\n");

	// IMPORTED lat/lon
	lat = m_importedLatitude;
	lon = m_importedLongitude;
	pbuf->safePrintf("<td><nobr>");
	if ( lat != NO_LATITUDE && lat != AMBIG_LATITUDE ) 
		pbuf->safePrintf("%f (%"INT32")",lat,m_importedVotes);
	pbuf->safePrintf("</nobr></td>\n");

	pbuf->safePrintf("<td><nobr>");
	if ( lon != NO_LONGITUDE && lon != AMBIG_LONGITUDE )
		pbuf->safePrintf("%f (%"INT32")",lon,m_importedVotes);
	pbuf->safePrintf("</nobr></td>\n");
}
			
void printPlaces ( PlaceMem *pm , SafeBuf *pbuf , Sections *sections,
		   Address *base ) {

	if ( pbuf ) pbuf->safePrintf ( "<table cellpadding=3 border=1>"
				       //"<tr><td>#</td>"
				       "<td><b>simple place</b></td>"

				       //"<td><b>score</b></td>"
				       //"<td><b>indBoost</b></td>"

				       "<td><b>flags</b></td>"

				       "<td><b><nobr>place hash"
				       "</nobr></b></td>"

				       "<td><b><nobr>address ptr"
				       "</nobr></b></td>"

				       "<td><b><nobr>word a</nobr></b></td>"
				       "<td><b><nobr>word b</nobr></b></td>"
				       "<td><b><nobr>alnum word a</nobr>"
				       "</b></td>"
				       "<td><b><nobr>alnum word b</nobr>"
				       "</b></td>"
				       //"<td><b>depth</b></td>"
				       //"<td><b><nobr>section #</nobr></b></td>"
				       //"<td><b><nobr>parent section #</nobr>"
				       //"</b></td>"
				       "<td><b><nobr>section tagHash</nobr>"
				       "</b></td>"

				       "</tr>\n" );

	// just streets really, or fake streets
	for ( int32_t i = 0 ; i < pm->getNumPtrs() ; i++ ) { // np
		Place *pi = (Place *)pm->getPtr(i);
		char *p    = pi->m_str;
		char *pend = p + pi->m_strlen;
		char c = *pend;
		*pend = 0;
		int32_t flags = pi->m_bits;
		char fbuf[1000];
		char *f = fbuf;
		// skip if filtered out from the city/adm1 loop above
		if ( ! pi->m_type  ) { *pend = c; continue; }
		f += sprintf ( f , "type=" );
		/*
		if ( pi->m_type == PT_SCH ) 
			f += sprintf ( f , "school " );
		if ( pi->m_type == PT_PRK ) 
			f += sprintf ( f , "park " );
		if ( pi->m_type == PT_CITY ) 
			f += sprintf ( f , "city " );
		if ( pi->m_type == PT_STATE ) 
			f += sprintf ( f , "adm1 " );
		if ( pi->m_type == PT_ADM2 ) 
			f += sprintf ( f , "adm2 " );
		if ( pi->m_type == PT_ADM3 ) 
			f += sprintf ( f , "adm3 " );
		if ( pi->m_type == PT_ADM4 ) 
			f += sprintf ( f , "adm4 " );
		if ( pi->m_type == PT_CTRY ) 
			f += sprintf ( f , "ctry " );
		if ( pi->m_type == PT_ZIP ) 
			f += sprintf ( f , "zip " );
		if ( pi->m_type == PT_SUITE ) 
			f += sprintf ( f , "suite " );
		if ( pi->m_type == PT_NAME_1      ) 
			f += sprintf ( f , "name1 " );
		if ( pi->m_type == PT_NAME_2      ) 
			f += sprintf ( f , "name2 " );
		*/
		if ( pi->m_type == PT_STREET ) 
			f += sprintf ( f , "street " );
		else if ( pi->m_type == PT_CITY ) 
			f += sprintf ( f , "city " );
		else if ( pi->m_type == PT_STATE ) 
			f += sprintf ( f , "state " );
		else if ( pi->m_type == PT_NAME_1 ) 
			f += sprintf ( f , "name1 " );
		else if ( pi->m_type == PT_NAME_2 ) 
			f += sprintf ( f , "name2 " );
		else if ( pi->m_type == PT_SUITE ) 
			f += sprintf ( f , "suite " );
		else if ( pi->m_type == PT_ZIP ) 
			f += sprintf ( f , "zip " );
		else if ( pi->m_type == PT_LATLON ) 
			f += sprintf ( f , "latlon " );
		else { char *xx=NULL;*xx=0; }
			
		f += sprintf ( f , "flags=" );
		char *of = f;
		//if ( flags & PLF_HAS_UPPER )
		//	f += sprintf ( f , "hasupper " );
		//if ( flags & PLF_ALT )
		//	f += sprintf ( f , "alt " );
		//if ( flags & PLF_IGNORE )
		//	f += sprintf ( f , "ignore " );
		//if ( flags & PLF_PARTIAL ) 
		//	f += sprintf ( f , "partial " );
		//if ( flags & PLF_AMBIGUOUS ) 
		//	f += sprintf ( f , "ambig " );


		if ( pi->m_flags2 & PLF2_COLLISION )
			f += sprintf(f,"streetcollision ");
		if ( pi->m_flags2 & PLF2_REQUIRED )
			f += sprintf(f,"requiredplace ");
		if ( pi->m_flags2 & PLF2_TICKET_PLACE )
			f += sprintf(f,"ticketplace ");
		if ( pi->m_flags2 & PLF2_INTERSECTION )
			f += sprintf(f,"intersection ");
		if ( pi->m_flags2 & PLF2_IS_NAME )
			f += sprintf(f,"streetisname ");
		if ( pi->m_flags2 & PLF2_AFTER_AT )
			f += sprintf(f,"afterat ");
		if ( pi->m_flags2 & PLF2_IS_POBOX )
			f += sprintf(f,"ispobox ");
		if ( pi->m_address )
			f += sprintf(f,"inaddress ");
		if ( pi->m_unverifiedAddress )
			f += sprintf(f,"inunverifiedaddress ");
		if ( pi->m_alias )
			f += sprintf(f,"alias[a=%"INT32"] ",
				     pi->m_alias->m_street->m_a);

		if ( flags & PLF_INFILE ) 
			f += sprintf ( f , "infile " );
		//if ( flags & PLF_INHERITED ) 
		//	f += sprintf ( f , "inherited " );
		//if ( flags & PLF_FROMZIP )
		//	f += sprintf ( f , "fromzip ");
		if ( flags & PLF_FROMTAG   ) 
			f += sprintf ( f , "fromtag " );
		if ( flags & PLF_FROMTITLE   ) 
			f += sprintf ( f , "fromtitle " );
		if ( flags & PLF_ABBR ) 
			f += sprintf ( f , "abbr " );
		//if ( f == of ) *f++ = ' ';
		//else           f[-1] = ' ';
		if ( f == of )
			f += sprintf(f,"&nbsp;");

		/*
		if ( flags & IND_NAME      ) 
			f += sprintf ( f , "ind_name " );
		if ( flags & IND_SUITE     ) 
			f += sprintf ( f , "ind_suite " );
		if ( flags & IND_STREET    ) 
			f += sprintf ( f , "ind_street " );
		if ( flags & IND_DIR       ) 
			f += sprintf ( f , "ind_dir " );
		*/
		//if ( flags & IND_BITS      ) 
		//	f += sprintf ( f , "ind_bits " );

		// add state
		//if ( pi->m_adm1[0] && pi->m_adm1[1] ) 
		//	f += sprintf(f,"adm1=%c%c ",
		//		     pi->m_adm1[0],pi->m_adm1[1]);

		// add country
		//if ( pi->m_crid )
		//	f += sprintf(f,"ctry=%s ",
		//		     g_countryCode.getName(pi->m_crid-1) );

		*f = '\0';

		// int16_tcut
		Section **sp = sections->m_sectionPtrs;
		// get section
		Section *sn = NULL;
		if ( pi->m_a >= 0 ) sn = sp [ pi->m_a ];
		int32_t depth = -1;
		if ( sn ) depth = sn->m_depth;
		// sectio number
		int32_t secNum = -1;
		int32_t parentSecNum = -1;
		if ( sn ) secNum = (int32_t)(sn - sp[0]);
		Section *parent = NULL;
		if ( sn ) parent = sn->m_parent;
		if ( parent ) parentSecNum = (int32_t)(parent - sp[0]);
		int32_t secHash = 0;
		if ( sn ) secHash = sn->m_turkTagHash32;
		// print the address we are in or the address we alias
		Address *myaddr = NULL;
		if ( pi->m_address ) myaddr = pi->m_address;
		if ( pi->m_alias   ) myaddr = pi->m_alias;
		// make it relative so qa test run diff is ok
		// MDW: might need to store the off in m_addressOff/m_aliasOff
		// or something.. keep an eye on this
		int32_t myoff = i;//myaddr - base;
		if ( myaddr == NULL ) myoff = -1;
		// sanity check
		// no, we now allow a full address like
		// "14th and curtis, denver co" to be an alias to a non
		// intersection address "1000 14th street, denver co"
		// as in devner.org
		//if ( pi->m_address && pi->m_alias ) {char *xx=NULL;*xx=0;}

		if ( pbuf ) {
			pbuf->safePrintf ( "<tr>"
					   //"<td>%"INT32"</td>"
					   "<td><nobr>" );
			// print it right. niceness = 0
			pbuf->htmlEncode ( p , gbstrlen(p) , true,0);
			pbuf->safePrintf ("</nobr></td>"
					   //"<td>%.02f</td>"
					   //"<td>%.02f</td>"

					   "<td><nobr>%s</nobr></td>"
					   "<td>0x%"XINT64"</td>"
					   "<td>%"INT32"</td>"
					   "<td>%"INT32"</td>"
					   "<td>%"INT32"</td>"
					   "<td>%"INT32"</td>"
					   "<td>%"INT32"</td>"
					   //"<td>%"INT32"</td>"
					   //"<td>%"INT32"</td>"
					   //"<td>%"INT32"</td>"
					   "<td>0x%"XINT32"</td>"
					   "</tr>\n" , 
					   //i,
					  //p,
					   //pi->m_score,
					   //pi->m_indScore,

					   fbuf  ,
					   pi->m_hash ,//m_hash
					   (int32_t)myoff,
					   (int32_t)pi->m_a ,
					   (int32_t)pi->m_b ,
					   (int32_t)pi->m_alnumA ,
					   (int32_t)pi->m_alnumB ,
					   //(int32_t)depth ,
					   //secNum,
					   //parentSecNum,
					   secHash);
		}
		else
			logf(LOG_DEBUG,"events: place #%"INT32" \"%s\" "
			     "flags=%s alnuma=%"INT32" alnumb=%"INT32" "
			     //"taghash=0x%"XINT32""
			     ,  
			     i,p,
			     //pi->m_score,
			     fbuf,
			     pi->m_alnumA ,
			     pi->m_alnumB 
			     //pi->m_indScore 
			     //pi->m_tagHash);
			     );
		// put char back
		*pend = c;

		// sanity 
		if ( ! ( pi->m_type ) ) { char *xx=NULL;*xx=0; }

	}
	if ( pbuf ) pbuf->safePrintf ( "</table><br>\n" );
}



// THINK ABOUT: discard phrases with number at end, no "suite" indicator, and 
// has US as the country (do this last)
// "... be eligible to play. AYSO Region 1447 offers a fun..."
// "Sunday 9 . 6, Tuesday 10 - 4; " --> no street called "Tuesday"!

class AliasDesc {
public:
	char *m_s1;
	char *m_s2;
	char *m_adm1;
	char *m_mostPopStateAbbr;
	// these are relative to the aliases as far as computing the best/
	// default state that contains it. right now we just set santa fe
	// down to 99 so that "sf" maps to "san francisco" by default.
	int32_t  m_pop;
};

static AliasDesc s_cityList[] = {
	//{"abq","albquerque"}
	//,{"alb","albquerque"}
	//,{"albq","albquerque"}

	{"ny","new york city","ny","ny",1000}
	,{"nyc","new york city","ny","ny",1000}
	,{"n y c","new york city","ny","ny",1000}
	,{"la","los angeles","ca","ca",1000}
	,{"lax","los angeles","ca","ca",1000}
	,{"chi","chicago","il","il",1000}
	,{"hou","houston","tx","tx",1000}
	,{"phx","phoenix","az","az",1000}
	,{"phoex","phoenix","az","az",1000}
	,{"phi","philadelphia","pa","pa",1000}
	,{"sa","san antonio","tx","tx",1000}
	,{"sd","san diego","ca","ca",1000}
	,{"dal","dallas","tx","tx",1000}
	,{"sj","san jose","ca","ca",1000}
	,{"det","detroit","mi","mi",1000}
	,{"jax","jacksonville","fl","fl",1000}
	,{"j-ville","jacksonville","fl","fl",1000}
	,{"indy","indianapolis","in","in",1000}
	,{"sf","san francisco","ca","ca",1000}
	,{"san fran","san francisco","ca","ca",1000}
	,{"sf","santa fe","nm","ca",99}
	,{"cols","columbus","oh","oh",1000}
	,{"colo","columbus","oh","oh",1000}
	,{"atx","austin","tx","tx",1000}
	,{"mem","memphis","tn","tn",1000}
	,{"fw","fort worth","tx","tx",1000}
	,{"ft worth","fort worth","tx","tx",1000}
	,{"balto","baltimore","md","md",1000}
	,{"clt","charlotte","nc","nc",1000}

	,{"ept","El Paso","tx","tx",1000}
	,{"elp","El Paso","tx","tx",1000} // airport
	,{"bos","Boston","ma","ma",1000} // airport
	,{"sea","Seattle","wa","wa",1000}
	,{"mil","Milwaukee","wi","wi",1000}
	,{"milw","Milwaukee","wi","wi",1000}
	,{"mke","Milwaukee","wi","wi",1000}
	,{"den","Denver","co","co",1000}
	,{"denv","Denver","co","co",1000}
	,{"lv","Las Vegas","nv","nv",1000} // postal
	,{"las","Las Vegas","nv","nv",1000} // airport
	,{"nash","Nashville","tn","tn",1000}
	,{"nashv","Nashville","tn","tn",1000}
	,{"bna","Nashville","tn","tn",1000}
	,{"okc","Oklahoma City","ok","ok",1000}
	,{"pdx","Portland","or","or",1000}
	,{"port","Portland","or","or",1000}
	,{"tuc","Tucson","az","az",1000}
	,{"tucs","Tucson","az","az",1000}
	,{"abq","Albuquerque","nm","nm",1000}
	,{"alb","Albuquerque","nm","nm",1000}
	,{"albq","Albuquerque","nm","nm",1000}
	,{"q-town","Albuquerque","nm","nm",1000}
	,{"atl","Atlanta","ga","ga",1000}
	,{"lbc","Long Beach","ca","ca",1000}
	,{"lb","Long Beach","ca","ca",1000}
	,{"frs","Fresno","ca","ca",1000}
	,{"sacto","Sacramento","ca","ca",1000}
	,{"smf","Sacramento","ca","ca",1000} // airport
	//,{"","Mesa","","",1000}
	,{"kc","Kansas City","ks","ks",1000}
	,{"cle","Cleveland","oh","oh",1000}
	,{"cleve","Cleveland","oh","oh",1000}
	,{"vab","Virginia Beach","va","va",1000}
	,{"oma","Omaha","ne","ne",1000}
	,{"mi","Miami","fl","fl",1000}
	,{"oak","Oakland","ca","ca",1000}
	//,{"","Tulsa","","",1000}
	,{"hon","Honolulu","hi","hi",1000}
	,{"hnl","Honolulu","hi","hi",1000}
	,{"hono","Honolulu","hi","hi",1000}
	,{"mpls","Minneapolis","mn","mn",1000}
	,{"anc","Arlington","va","va",1000}
	,{"wh","Wichita","ks","ks",1000}
	//,{"","Raleigh","","",1000}
	,{"stl","Saint Louis","mo","mo",1000}
	,{"st louis","saint louis","mo","mo",1000}
	,{"sna","Santa Ana","ca","ca",1000}
	,{"aoc","Anaheim","ca","ca",1000} // anaheim orange county
	,{"tpa","Tampa","fl","fl",1000}
	,{"cinti","Cincinnati","oh","oh",1000}
	,{"cincy","Cincinnati","oh","oh",1000}
	,{"pitt","Pittsburgh","pa","pa",1000}
	,{"pit","Pittsburgh","pa","pa",1000}
	,{"pgh","Pittsburgh","pa","pa",1000}
	,{"pitts","Pittsburgh","pa","pa",1000}
	,{"bfd","Bakersfield","ca","ca",1000}
	//,{"","Aurora","","",1000}
	//,{"","Toledo","","",1000}
	//,{"","Riverside","","",1000}
	,{"sto","Stockton","ca","ca",1000}
	,{"cctx","Corpus Christi","tx","tx",1000}
	,{"cor chr","Corpus Christi","tx","tx",1000}
	//,{"","Newark","","",1000}
	,{"anch","Anchorage","ak","ak",1000}
	,{"buff","Buffalo","ny","ny",1000}
	,{"stpaul","Saint Paul","mn","mn",1000}
	,{"st paul","Saint Paul","mn","mn",1000}
	//,{"","Plano","","",1000}
	,{"fwa","Fort Wayne","in","in",1000} // airport
	//,{"ftw","Fort Wayne","","",1000} 
	,{"ft wayne","Fort Wayne","in","in",1000} // airport
	,{"st petersburg","saint petersburg","fl","fl",1000}
	//,{"","Glendale","","",1000}
	,{"jc","Jersey City","nj","nj",1000}
	//,{"","Lincoln","","",1000}
	//,{"","Henderson","","",1000}
	//,{"","Chandler","","",1000}
	//,{"","Greensboro","","",1000}
	//,{"","Scottsdale","","",1000}
	,{"br","Baton Rouge","la","la",1000}
	,{"bham","Birmingham","al","al",1000}
	,{"b ham","Birmingham","al","al",1000}
	,{"nflk","Norfolk","va","va",1000}
	,{"madsn","Madison","wi","wi",1000}
	,{"no","New Orleans","la","la",1000}
	,{"north hempstead","Town of North Hempstead","ny","ny",1000}
	,{"n hempstead","Town of North Hempstead","ny","ny",1000}
	,{"n hemp","Town of North Hempstead","ny","ny",1000}
	,{"north hemp","Town of North Hempstead","ny","ny",1000}
	,{"chesp","Chesapeake","va","va",1000}
	//,{"","Orlando","","",1000}
	//,{"","Garland","","",1000}
	//,{"","Hialeah","","",1000}
	//,{"","Laredo","","",1000}
	,{"cv","Chula Vista","ca","ca",1000}
	//,{"","Lubbock","","",1000}
	//,{"","Reno","","",1000}
	//,{"","Akron","","",1000}
	//,{"","Durham","","",1000}
	,{"roch","Rochester","ny","ny",1000}
	//,{"","Modesto","","",1000}
	,{"mont","Montgomery","al","al",1000}
	//,{"","Fremont","","",1000}
	//,{"","Shreveport","","",1000}
	//,{"","Arlington","","",1000}
	//,{"","Glendale","","",1000}
};	 


bool addCity ( uint64_t ch64 , 
	       char *adm1 ,
	       int32_t pop ,
	       HashTableX *maxPops ) {

	// see if already in the table
	CityDesc *cdp = (CityDesc *)g_cities.getValue(&ch64);

	//
	// if contending with another state that has this
	// same city name, check his city pop
	//
	// get the last max popularity for this state
	int32_t *v=(int32_t *)maxPops->getValue(&ch64);
	// save it into "lastPop" in case *v changes
	int32_t lastPop = -1;
	if ( v ) lastPop = *v;
	// update pop with ours if bigger
	if ( v && pop > *v ) *v = pop;

	uint64_t adm1Bits = getAdm1Bits ( adm1 );
	if ( ! adm1Bits ) { char *xx=NULL;*xx=0; }
	
	// if there, or it in
	if ( cdp ) cdp->m_adm1Bits |= adm1Bits;

	//if ( ch64==2443313629685134902LL && adm1Bits==2147483648 ) {
	//	log("hey");
	//}
	
	// get our state
	StateDesc *sd = getStateDesc ( adm1 );
	// get our state index
	int32_t stateIndex = sd - s_states;
	
	// update most popular state index?
	if ( cdp && pop > lastPop ) {
		// change it to our state
		cdp->m_mostPopularState = stateIndex;
		return true;
	}
	
	// already there? then skip
	if ( cdp ) return true;
	
	// otherwise, add the pop for the first time
	maxPops->addKey(&ch64,&pop);
	
	// now this is CityDesc
	CityDesc cd;
	cd.m_adm1Bits = adm1Bits;
	cd.m_mostPopularState = stateIndex;
	
	// otherwise, just add it
	g_cities.addKey ( &ch64 , &cd ) ; // adm1Bits );
	return true;
}

// . ch64 is the 64bit hash of the original city name
// . "alias" is the alias name o fthe city
// . adm1Str is the state it is in
bool addAlias ( char *alias , 
		char *adm1Str , 
		uint64_t ch64 , 
		int32_t pop ,
		HashTableX *maxPops ) {
	// sanity check
	if ( is_upper_a(adm1Str[0]) ) { char *xx=NULL;*xx=0; }
	if ( is_upper_a(adm1Str[1]) ) { char *xx=NULL;*xx=0; }
	// get "hash" of state
	uint32_t adm1Hash32 = (uint32_t)(*(uint16_t *)adm1Str);
	// get hash of city name alias
	uint64_t ah = getWordXorHash ( alias );
	// nothing?
	if ( ! ah ) return true;
	// debug point
	if ( !strcmp(alias,"sf") ) 
		log("hey");
	// get the bits
	uint64_t adm1Bits = getAdm1Bits ( adm1Str );
	// if already in g_cities for this state, do not add as alias!
	CityDesc *test = (CityDesc *) g_cities.getValue(&ah);
	if ( test && (test->m_adm1Bits & adm1Bits ) ) {


		// no! strange... how is this happening...
		//log("strange");
		return true;
	}
	// hash city name alias and adm1 together
	uint32_t aliasStateHash = hash32h ( (uint32_t)ah , adm1Hash32 );
	// now that maps to the proper cityId32
	uint32_t cid32 = getCityId32 ( ch64 , adm1Str ) ;
	// must be a proper city name
	CityDesc *cd = (CityDesc *)g_cities.getValue(&ch64);
	if ( ! cd ) { char *xx=NULL;*xx=0; }
	// make sure the city we are an alias for is in our state!
	if ( !(cd->m_adm1Bits & adm1Bits) ) { char *xx=NULL;*xx=0; }
	// add to alias table
	if (!g_aliases.addKey (&aliasStateHash,&cid32)){char*xx=NULL;*xx=0;}
	// sanity check -- verify the cityId works out
	if ( ! g_timeZones.isInTable(&cid32) ) { char *xx=NULL;*xx=0;}
	// then add to city table
	addCity ( ah , adm1Str , pop , maxPops );
	return true;
}

bool initPlaceDescTable ( ) {

	// sanity check
	if ( s_init ) { char *xx=NULL;*xx=0; }

	// bail if not indexing events
	//if ( ! g_conf.m_indexEventsOnly ) return true;
	return true;

	// . make this table
	// . has words that can be lower case in a place name
	//s_lc.set ( 8 , 0 , 0 , s_lcbuf , 2000 , false , 0 ,"plnametbl");
	// stock the table (StopWords.cpp function)
	if ( ! initWordTable ( &s_lc , s_lcWords , 
			       //sizeof(s_lcWords),
			       "plnametbl")){
		char *xx=NULL;*xx=0; }

	// we are init now
	s_init = true;

	// init indicator table
	g_indicators.set ( 6                 ,  // keySize
			   sizeof(IndDesc)   ,  // dataSize
			   0                 ,  // initial # slots 
			   NULL              ,  // initial buf
			   0                 ,  // initial buf size
			   false             ,  // allowDup keys?
			   0                 ,  // niceness
			   "indictbl"        );
	
	// load inidcator table
	//bool loadedIndicators = false;
	/*
	if ( g_indicators.load ( g_hostdb.m_dir , "indicators.dat" ) ) {
		loadedIndicators = true;
		int64_t h = hash64 ( "highway" , 7 );
		// test the indicators
		if ( g_indicators.getSlot ( &h ) < 0 ){char *xx=NULL;*xx=0; }
		// test the indicators
		h = hash64Lower_a ( "N" , 1 );
		if ( g_indicators.getSlot ( &h ) < 0 ){char *xx=NULL;*xx=0; }
	}
	*/
	// fix it
	//loadedIndicators = true;

	// keep these separate so we do not have to recompute any time we
	// add or substract to/from this list
	addIndicator ( "airport"       , IND_NAME , 1.0 );	
	addIndicator ( "airstrip"       , IND_NAME , 1.0 );	
	addIndicator ( "area"       , IND_NAME , 1.0 );	
	addIndicator ( "arena"       , IND_NAME , 1.0 );	
	addIndicator ( "arroyo"       , IND_NAME , 1.0 );	
	addIndicator ( "bank"       , IND_NAME , 1.0 );	
	addIndicator ( "banks"       , IND_NAME , 1.0 );	
	addIndicator ( "bar"       , IND_NAME , 1.0 );	
	addIndicator ( "pub"       , IND_NAME , 1.0 );	
	addIndicator ( "brewpub"       , IND_NAME , 1.0 );	
	addIndicator ( "atrium"       , IND_NAME , 1.0 );	
	addIndicator ( "base"       , IND_NAME , 1.0 );	
	addIndicator ( "basin"       , IND_NAME , 1.0 );	
	addIndicator ( "bay"       , IND_NAME , 1.0 );	
	addIndicator ( "beach"       , IND_NAME , 1.0 );	
	addIndicator ( "bluff"       , IND_NAME , 1.0 );	
	addIndicator ( "bog"       , IND_NAME , 1.0 );	
	addIndicator ( "boundary"       , IND_NAME , 1.0 );	
	addIndicator ( "branch"       , IND_NAME , 1.0 );	
	addIndicator ( "bridge"       , IND_NAME , 1.0 );	
	addIndicator ( "brook"       , IND_NAME , 1.0 );	
	addIndicator ( "building"       , IND_NAME , 1.0 );	
	addIndicator ( "bunker"       , IND_NAME , 1.0 );	
	addIndicator ( "burro"       , IND_NAME , 1.0 );	
	addIndicator ( "butte"       , IND_NAME , 1.0 );	
	addIndicator ( "cabin"       , IND_NAME , 1.0 );	
	addIndicator ( "camp"       , IND_NAME , 1.0 );	
	addIndicator ( "campground"       , IND_NAME , 1.0 );	
	addIndicator ( "campgrounds"       , IND_NAME , 1.0 );	
	addIndicator ( "campus"       , IND_NAME , 1.0 );	
	addIndicator ( "canal"       , IND_NAME , 1.0 );	
	addIndicator ( "canyon"       , IND_NAME , 1.0 );	
	addIndicator ( "casa"       , IND_NAME , 1.0 );	
	addIndicator ( "castle"       , IND_NAME , 1.0 );	
	addIndicator ( "cathedral"       , IND_NAME , 1.0 );	
	addIndicator ( "cave"       , IND_NAME , 1.0 );	
	addIndicator ( "cemetery"       , IND_NAME , 1.0 );	
	addIndicator ( "center"       , IND_NAME , 1.0 );	
	addIndicator ( "centre"       , IND_NAME , 1.0 );	
	// "channel 13 news"?
	//addIndicator ( "channel"       , IND_NAME , 1.0 );	
	addIndicator ( "chapel"       , IND_NAME , 1.0 );	
	addIndicator ( "church"       , IND_NAME , 1.0 );	
	// "bible study circle"
	//addIndicator ( "circle"       , IND_NAME , 1.0 );	
	addIndicator ( "cliffs"       , IND_NAME , 1.0 );	
	addIndicator ( "clinic"       , IND_NAME , 1.0 );	
	addIndicator ( "college"       , IND_NAME , 1.0 );	
	addIndicator ( "company"       , IND_NAME , 1.0 );	
	addIndicator ( "complex"       , IND_NAME , 1.0 );	
	addIndicator ( "corner"       , IND_NAME , 1.0 );	
	addIndicator ( "cottage"       , IND_NAME , 1.0 );	
	addIndicator ( "course"       , IND_NAME , 1.0 );	 // golf
	addIndicator ( "courthouse"       , IND_NAME , 1.0 );	
	addIndicator ( "courtyard"       , IND_NAME , 1.0 );	
	addIndicator ( "cove"       , IND_NAME , 1.0 );	
	addIndicator ( "creek"       , IND_NAME , 1.0 );	
	addIndicator ( "dam"       , IND_NAME , 1.0 );	
	addIndicator ( "den"       , IND_NAME , 1.0 );	
	addIndicator ( "department"       , IND_NAME , 1.0 );	
	addIndicator ( "depot"       , IND_NAME , 1.0 );	
	addIndicator ( "dome"       , IND_NAME , 1.0 );	
	addIndicator ( "downs"       , IND_NAME , 1.0 );	
	addIndicator ( "fair"       , IND_NAME , 1.0 );	
	addIndicator ( "fairgrounds"       , IND_NAME , 1.0 );	
	addIndicator ( "fairground"       , IND_NAME , 1.0 );	
	addIndicator ( "falls"       , IND_NAME , 1.0 );	
	addIndicator ( "farm"       , IND_NAME , 1.0 );	
	addIndicator ( "farms"       , IND_NAME , 1.0 );	
	addIndicator ( "field"       , IND_NAME , 1.0 );	
	addIndicator ( "fields"       , IND_NAME , 1.0 );	
	addIndicator ( "flat"       , IND_NAME , 1.0 );	
	addIndicator ( "flats"       , IND_NAME , 1.0 );	
	addIndicator ( "forest"       , IND_NAME , 1.0 );	
	addIndicator ( "fort"       , IND_NAME , 1.0 );	
	addIndicator ( "fountain"       , IND_NAME , 1.0 );	
	addIndicator ( "garden"       , IND_NAME , 1.0 );	
	addIndicator ( "gardens"       , IND_NAME , 1.0 );	
	addIndicator ( "gate"       , IND_NAME , 1.0 );	
	addIndicator ( "glacier"       , IND_NAME , 1.0 );	
	addIndicator ( "graveyard"       , IND_NAME , 1.0 );	
	addIndicator ( "gulch"       , IND_NAME , 1.0 );	
	addIndicator ( "gully"       , IND_NAME , 1.0 );	
	addIndicator ( "hacienda"       , IND_NAME , 1.0 );	
	addIndicator ( "hall"       , IND_NAME , 1.0 );	
	addIndicator ( "halls"       , IND_NAME , 1.0 );	
	addIndicator ( "harbor"       , IND_NAME , 1.0 );	
	addIndicator ( "harbour"       , IND_NAME , 1.0 );	
	addIndicator ( "hatchery"       , IND_NAME , 1.0 );	
	addIndicator ( "headquarters"       , IND_NAME , 1.0 );	
	addIndicator ( "heights"       , IND_NAME , 1.0 );	
	addIndicator ( "heliport"       , IND_NAME , 1.0 );	
	addIndicator ( "hill"       , IND_NAME , 1.0 );	
	addIndicator ( "hillside"       , IND_NAME , 1.0 );	
	addIndicator ( "hilton"       , IND_NAME , 1.0 );	
	addIndicator ( "historical"       , IND_NAME , 1.0 );	
	addIndicator ( "historic"       , IND_NAME , 1.0 );	
	addIndicator ( "holy"       , IND_NAME , 1.0 );	
	addIndicator ( "home"       , IND_NAME , 1.0 );	
	addIndicator ( "homestead"       , IND_NAME , 1.0 );	
	addIndicator ( "horn"       , IND_NAME , 1.0 );	
	addIndicator ( "hospital"       , IND_NAME , 1.0 );	
	addIndicator ( "hotel"       , IND_NAME , 1.0 );	
	addIndicator ( "house"       , IND_NAME , 1.0 );	
	addIndicator ( "howard"       , IND_NAME , 1.0 );	 // johnson's
	addIndicator ( "inlet"       , IND_NAME , 1.0 );	
	addIndicator ( "inn"       , IND_NAME , 1.0 );	
	addIndicator ( "institute"       , IND_NAME , 1.0 );	
	addIndicator ( "international"       , IND_NAME , 1.0 );	
	addIndicator ( "isla"       , IND_NAME , 1.0 );	
	addIndicator ( "island"       , IND_NAME , 1.0 );	
	addIndicator ( "isle"       , IND_NAME , 1.0 );	
	addIndicator ( "islet"       , IND_NAME , 1.0 );	
	addIndicator ( "junction"       , IND_NAME , 1.0 );	
	addIndicator ( "knoll"       , IND_NAME , 1.0 );	
	addIndicator ( "lagoon"       , IND_NAME , 1.0 );	
	addIndicator ( "laguna"       , IND_NAME , 1.0 );	
	addIndicator ( "lake"       , IND_NAME , 1.0 );	
	addIndicator ( "landing"       , IND_NAME , 1.0 );	
	addIndicator ( "ledge"       , IND_NAME , 1.0 );	
	addIndicator ( "lighthouse"       , IND_NAME , 1.0 );	
	addIndicator ( "lodge"       , IND_NAME , 1.0 );	
	addIndicator ( "lookout"       , IND_NAME , 1.0 );	
	addIndicator ( "mall"       , IND_NAME , 1.0 );	 // added
	addIndicator ( "manor"       , IND_NAME , 1.0 );	
	addIndicator ( "marina"       , IND_NAME , 1.0 );	
	addIndicator ( "meadow"       , IND_NAME , 1.0 );	
	addIndicator ( "mine"       , IND_NAME , 1.0 );	
	addIndicator ( "mines"       , IND_NAME , 1.0 );	
	addIndicator ( "monument"       , IND_NAME , 1.0 );	
	addIndicator ( "motel"       , IND_NAME , 1.0 );	
	addIndicator ( "museum"       , IND_NAME , 1.0 );	
	addIndicator ( "office"       , IND_NAME , 1.0 );	
	addIndicator ( "outlet"       , IND_NAME , 1.0 );	
	addIndicator ( "palace"       , IND_NAME , 1.0 );	
	addIndicator ( "park"       , IND_NAME , 1.0 );	
	addIndicator ( "peaks"       , IND_NAME , 1.0 );	
	addIndicator ( "peninsula"       , IND_NAME , 1.0 );	
	addIndicator ( "pit"       , IND_NAME , 1.0 );	

	addIndicator ( "place"       , IND_STREET , 1.0 ); // leroy place
	addIndicator ( "pl"          , IND_STREET , 1.0 );	 // place

	addIndicator ( "plains"       , IND_NAME , 1.0 );	
	addIndicator ( "plant"       , IND_NAME , 1.0 );	
	addIndicator ( "plantation"       , IND_NAME , 1.0 );	
	addIndicator ( "plateau"       , IND_NAME , 1.0 );	
	addIndicator ( "playa"       , IND_NAME , 1.0 );	
	addIndicator ( "plaza"       , IND_NAME , 1.0 );	
	addIndicator ( "point"       , IND_NAME , 1.0 );	
	addIndicator ( "pointe"       , IND_NAME , 1.0 );	
	addIndicator ( "pond"       , IND_NAME , 1.0 );	
	addIndicator ( "port"       , IND_NAME , 1.0 );	
	addIndicator ( "ramada"       , IND_NAME , 1.0 );	
	addIndicator ( "ranch"       , IND_NAME , 1.0 );	
	addIndicator ( "rancho"       , IND_NAME , 1.0 );	
	addIndicator ( "range"       , IND_NAME , 1.0 );	
	addIndicator ( "reef"       , IND_NAME , 1.0 );	
	addIndicator ( "refure"       , IND_NAME , 1.0 );	
	addIndicator ( "reserve"       , IND_NAME , 1.0 );	
	addIndicator ( "reservoir"       , IND_NAME , 1.0 );	
	addIndicator ( "residence"       , IND_NAME , 1.0 );	
	addIndicator ( "resort"       , IND_NAME , 1.0 );	
	//addIndicator ( "rio"       , IND_NAME , 1.0 );	
	//addIndicator ( "river"       , IND_NAME , 1.0 );	
	//addIndicator ( "riverside"       , IND_NAME , 1.0 );	
	//addIndicator ( "riverview"       , IND_NAME , 1.0 );	
	// was getting "rock bands"
	//addIndicator ( "rock"       , IND_NAME , 1.0 );	
	addIndicator ( "sands"       , IND_NAME , 1.0 );	 // added
	addIndicator ( "sawmill"       , IND_NAME , 1.0 );	
	addIndicator ( "school"       , IND_NAME , 1.0 );	
	// try to fix hadcolon algo for
	// The+Webb+Schools:+Calendars+...
	addIndicator ( "schools"       , IND_NAME , 1.0 );	
	addIndicator ( "schoolhouse"       , IND_NAME , 1.0 );	
	addIndicator ( "shore"       , IND_NAME , 1.0 );	
	addIndicator ( "spa"       , IND_NAME , 1.0 );	
	addIndicator ( "spring"       , IND_NAME , 1.0 );	
	addIndicator ( "springs"       , IND_NAME , 1.0 );	
	addIndicator ( "stadium"       , IND_NAME , 1.0 );	
	addIndicator ( "station"       , IND_NAME , 1.0 );	
	addIndicator ( "strip"       , IND_NAME , 1.0 );	
	addIndicator ( "suites"       , IND_NAME , 1.0 );	
	addIndicator ( "temple"       , IND_NAME , 1.0 );	
	addIndicator ( "terrace"       , IND_NAME , 1.0 );	
	addIndicator ( "tower"       , IND_NAME , 1.0 );	
	//addIndicator ( "trail"       , IND_NAME , 1.0 );	
	addIndicator ( "travelodge"       , IND_NAME , 1.0 );	
	addIndicator ( "triangle"       , IND_NAME , 1.0 );	
	addIndicator ( "tunnel"       , IND_NAME , 1.0 );	
	addIndicator ( "university"       , IND_NAME , 1.0 );	
	//addIndicator ( "valley"       , IND_NAME , 1.0 );	
	addIndicator ( "wall"       , IND_NAME , 1.0 );	
	addIndicator ( "ward"       , IND_NAME , 1.0 );	
	addIndicator ( "waterhole"       , IND_NAME , 1.0 );	
	addIndicator ( "waters"       , IND_NAME , 1.0 );	
	addIndicator ( "well"       , IND_NAME , 1.0 );	
	addIndicator ( "wells"       , IND_NAME , 1.0 );	
	addIndicator ( "wilderness"       , IND_NAME , 1.0 );	
	addIndicator ( "windmill"       , IND_NAME , 1.0 );	
	addIndicator ( "woodland"       , IND_NAME , 1.0 );	
	addIndicator ( "woods"       , IND_NAME , 1.0 );	

	
	// good stuff i added
	// some from http://www.geonames.org/export/codes.html
	addIndicator ( "gallery"       , IND_NAME , 1.0 );	
	addIndicator ( "theater"       , IND_NAME , 1.0 );	
	addIndicator ( "theatre"       , IND_NAME , 1.0 );	
	addIndicator ( "playhouse"       , IND_NAME , 1.0 );	
	addIndicator ( "saloon"       , IND_NAME , 1.0 );	
	addIndicator ( "nightclub"       , IND_NAME , 1.0 );	
	addIndicator ( "lounge"       , IND_NAME , 1.0 );	
	addIndicator ( "ultralounge"       , IND_NAME , 1.0 );	
	addIndicator ( "brewery"       , IND_NAME , 1.0 );	
	addIndicator ( "chophouse"       , IND_NAME , 1.0 );	
	addIndicator ( "tavern"       , IND_NAME , 1.0 );	
	addIndicator ( "company"       , IND_NAME , 1.0 );	
	addIndicator ( "rotisserie"       , IND_NAME , 1.0 );	
	addIndicator ( "bistro"       , IND_NAME , 1.0 );	
	addIndicator ( "parlor"       , IND_NAME , 1.0 );	
	addIndicator ( "studio"       , IND_NAME , 1.0 );	
	addIndicator ( "studios"       , IND_NAME , 1.0 );	
	// albuquerque publishing co., int16_t for "company"
	addIndicator ( "co"       , IND_NAME , 0.9 );
	addIndicator ( "bureau"   , IND_NAME , 1.0 );	
	addIndicator ( "estates"   , IND_NAME , 1.0 );	
	addIndicator ( "dockyard"       , IND_NAME , 1.0 );	
	addIndicator ( "gym"       , IND_NAME , 1.0 );	
	addIndicator ( "synagogue"       , IND_NAME , 1.0 );	
	addIndicator ( "shrine"       , IND_NAME , 1.0 );	
	addIndicator ( "mosque"       , IND_NAME , 1.0 );	
	addIndicator ( "store"       , IND_NAME , 1.0 );	
	addIndicator ( "mercantile"       , IND_NAME , 1.0 );	
	addIndicator ( "mart"       , IND_NAME , 1.0 );	
	addIndicator ( "amphitheatre"       , IND_NAME , 1.0 );	
	addIndicator ( "kitchen"       , IND_NAME , 1.0 );	
	addIndicator ( "casino"       , IND_NAME , 1.0 );	
	addIndicator ( "diner"       , IND_NAME , 1.0 );	
	addIndicator ( "eatery"       , IND_NAME , 1.0 );	
	addIndicator ( "shop"       , IND_NAME , 1.0 );	
	addIndicator ( "inc"       , IND_NAME , 1.0 );	 // incorporated
	addIndicator ( "incorporated" , IND_NAME , 1.0 );
	addIndicator ( "corporation" , IND_NAME , 1.0 );
	addIndicator ( "limited"       , IND_NAME , 1.0 );	
	addIndicator ( "llc"       , IND_NAME , 1.0 );	
	addIndicator ( "foundation"       , IND_NAME , 1.0 );	
	addIndicator ( "warehouse"       , IND_NAME , 1.0 );	
	addIndicator ( "roadhouse"       , IND_NAME , 1.0 );	
	addIndicator ( "foods"       , IND_NAME , 1.0 );	
	addIndicator ( "cantina"       , IND_NAME , 1.0 );	
	addIndicator ( "steakhouse"       , IND_NAME , 1.0 );	
	addIndicator ( "smokehouse"       , IND_NAME , 1.0 );	
	addIndicator ( "deli"       , IND_NAME , 1.0 );	
	addIndicator ( "enterprises"       , IND_NAME , 1.0 );	
	addIndicator ( "repair"       , IND_NAME , 1.0 );	
	addIndicator ( "service"       , IND_NAME , 1.0 );	
	addIndicator ( "services"       , IND_NAME , 1.0 );	
	addIndicator ( "systems"       , IND_NAME , 1.0 );	
	addIndicator ( "salon"       , IND_NAME , 1.0 );	
	addIndicator ( "boutique"       , IND_NAME , 1.0 );	
	addIndicator ( "preschool"       , IND_NAME , 1.0 );	
	addIndicator ( "galleries"       , IND_NAME , 1.0 );	
	addIndicator ( "bakery"       , IND_NAME , 1.0 );	
	addIndicator ( "factory"       , IND_NAME , 1.0 );	
	addIndicator ( "llp"       , IND_NAME , 1.0 );	
	addIndicator ( "attorney"       , IND_NAME , 1.0 );	
	addIndicator ( "association"       , IND_NAME , 1.0 );	
	addIndicator ( "solutions"       , IND_NAME , 1.0 );	
	addIndicator ( "facility"       , IND_NAME , 1.0 );	
	addIndicator ( "cannery"       , IND_NAME , 1.0 );	
	addIndicator ( "mill"       , IND_NAME , 1.0 );	
	addIndicator ( "quarry"       , IND_NAME , 1.0 );	
	addIndicator ( "monastery"       , IND_NAME , 1.0 );	
	addIndicator ( "observatory"       , IND_NAME , 1.0 );	
	addIndicator ( "nursery"       , IND_NAME , 1.0 );	
	addIndicator ( "pagoda"       , IND_NAME , 1.0 );	
	addIndicator ( "pier"       , IND_NAME , 1.0 );	
	addIndicator ( "prison"       , IND_NAME , 1.0 );	
	addIndicator ( "post"       , IND_NAME , 1.0 );	
	addIndicator ( "ruin"       , IND_NAME , 1.0 );	
	addIndicator ( "ruins"       , IND_NAME , 1.0 );	
	addIndicator ( "storehouse"       , IND_NAME , 1.0 );	
	addIndicator ( "square"       , IND_NAME , 1.0 );	
	addIndicator ( "tomb"       , IND_NAME , 1.0 );	
	addIndicator ( "wharf"       , IND_NAME , 1.0 );	
	addIndicator ( "zoo"       , IND_NAME , 1.0 );	
	addIndicator ( "mesa"       , IND_NAME , 1.0 );	
	addIndicator ( "pass"       , IND_NAME , 1.0 );	
	addIndicator ( "passage"       , IND_NAME , 1.0 );	
	addIndicator ( "peak"       , IND_NAME , 1.0 );	
	addIndicator ( "vineyard"       , IND_NAME , 1.0 );	
	addIndicator ( "grove"       , IND_NAME , 1.0 );	
	//addIndicator ( ""       , IND_NAME , 1.0 );	



	// maple street dance space
	addIndicator ( "space"       , IND_NAME , 1.0 );	
	addIndicator ( "library"       , IND_NAME , 1.0 );	
	addIndicator ( "school"       , IND_NAME , 1.0 );	
	addIndicator ( "church"       , IND_NAME , 1.0 );	
	addIndicator ( "park"       , IND_NAME , 1.0 );	
	addIndicator ( "house"       , IND_NAME , 1.0 );	
	// markets are sometimes more of events than place names
	addIndicator ( "market"       , IND_NAME , 0.5 );	
	addIndicator ( "marketplace"       , IND_NAME , 0.75 );	
	addIndicator ( "university"       , IND_NAME , 1.0 );	
	addIndicator ( "center"       , IND_NAME , 1.0 );	
	addIndicator ( "restaurant"       , IND_NAME , 1.0 );	
	//addIndicator ( "bar"       , IND_NAME , 1.0 );	
	addIndicator ( "grill"       , IND_NAME , 1.0 );	
	addIndicator ( "grille"       , IND_NAME , 1.0 );	
	addIndicator ( "cafe"       , IND_NAME , 1.0 );	
	addIndicator ( "cabana"       , IND_NAME , 1.0 );	
	addIndicator ( "shack"       , IND_NAME , 1.0 );	
	addIndicator ( "shoppe"       , IND_NAME , 1.0 );	
	addIndicator ( "collesium"       , IND_NAME , 1.0 );	
	addIndicator ( "colliseum"       , IND_NAME , 1.0 );	
	addIndicator ( "pavilion"       , IND_NAME , 1.0 );	
	// cafe with accent mark
	char tmp[64];
	sprintf(tmp,"caf"); tmp[3]=0xc3; tmp[4]=0xa9; tmp[5]=0;
	addIndicator ( tmp      , IND_NAME , 1.0 );	

	// Less effective place name indicators
	addIndicator ( "club"       , IND_NAME , 0.5 );	



	// . now add some more indicators to g_cities hash table
	// . TODO: get these in other languages. use wikipedia page!
	addIndicator ( "suite"       , IND_SUITE , 1.0 );
	addIndicator ( "ste"         , IND_SUITE , 1.0 );
	addIndicator ( "room"        , IND_SUITE , 1.0 );
	addIndicator ( "pier"        , IND_SUITE , 1.0 );
	addIndicator ( "department"  , IND_SUITE , 0.5 );
	addIndicator ( "rm"          , IND_SUITE , 1.0 );
	addIndicator ( "floor"       , IND_SUITE , 1.0 );
	addIndicator ( "bldg"        , IND_SUITE , 1.0 );
	addIndicator ( "bld"        , IND_SUITE , 1.0 );
	addIndicator ( "building"    , IND_SUITE , 1.0 );
	addIndicator ( "apartment"   , IND_SUITE , 1.0 );
	addIndicator ( "apt"         , IND_SUITE , 1.0 );
	addIndicator ( "po"          , IND_SUITE , 1.0 ); 
	addIndicator ( "pobox"       , IND_SUITE , 1.0 ); 
	//addIndicator("p.o. box"    , IND_SUITE , 1.0 ); 
	addIndicator ( "box"         , IND_SUITE , 1.0 ); 
	addIndicator ( "postbus"     , IND_SUITE , 1.0 ); // european
	addIndicator ( "post"        , IND_SUITE , 1.0 ); // european
	addIndicator ( "bus"         , IND_SUITE , 1.0 ); // european
	addIndicator ( "private"     , IND_SUITE , 1.0 ); // australia
	addIndicator ( "box"         , IND_SUITE , 1.0 ); // australia

	// TODO: get these in other languages. use wikipedia page!
	addIndicator (  "north" , IND_DIR , 1.0 );
	addIndicator (  "east"  , IND_DIR , 1.0 );
	addIndicator (  "south" , IND_DIR , 1.0 );
	addIndicator (  "west"  , IND_DIR , 1.0 );

	addIndicator (  "northeast" , IND_DIR , 1.0 );
	addIndicator (  "northwest" , IND_DIR , 1.0 );
	addIndicator (  "southeast" , IND_DIR , 1.0 );
	addIndicator (  "southwest" , IND_DIR , 1.0 );

	addIndicator (  "north" , IND_DIR , 1.0 );
	addIndicator (  "east"  , IND_DIR , 1.0 );
	addIndicator (  "south" , IND_DIR , 1.0 );
	addIndicator (  "west"  , IND_DIR , 1.0 );

	addIndicator (  "n"     , IND_DIR , 1.0 );
	addIndicator (  "s"     , IND_DIR , 1.0 );
	addIndicator (  "e"     , IND_DIR , 1.0 );
	addIndicator (  "w"     , IND_DIR , 1.0 );
	addIndicator (  "ne"    , IND_DIR , 1.0 );
	addIndicator (  "nw"    , IND_DIR , 1.0 );
	addIndicator (  "se"    , IND_DIR , 1.0 );
	addIndicator (  "sw"    , IND_DIR , 1.0 );

	// TODO: get in other languages
	addIndicator (  "highway"   , IND_STREET , 1.0 );
	addIndicator (  "hghway"    , IND_STREET , 1.0 );
	addIndicator (  "hiway"     , IND_STREET , 1.0 );
	addIndicator (  "hway"      , IND_STREET , 1.0 );
	addIndicator (  "hwy"       , IND_STREET , 1.0 );

	// county road
	//addIndicator (  "cr"       , IND_STREET , 1.0 );
	// state route
	//addIndicator (  "route"       , IND_STREET , 1.0 );

	addIndicator (  "avenue"    , IND_STREET , 1.0 );
	addIndicator (  "ave"       , IND_STREET , 1.0 );
	addIndicator (  "drive"     , IND_STREET , 1.0 );
	addIndicator (  "dr"        , IND_STREET , 1.0 );
	addIndicator (  "ln"        , IND_STREET , 1.0 );
	addIndicator (  "lane"      , IND_STREET , 1.0 );
	addIndicator (  "blvd"      , IND_STREET , 1.0 );
	addIndicator (  "boulevard" , IND_STREET , 1.0 );
	addIndicator (  "street"    , IND_STREET , 1.0 );
	addIndicator (  "st"        , IND_STREET , 1.0 );
	addIndicator (  "circle"    , IND_STREET , 1.0 );
	addIndicator (  "place"     , IND_STREET , 1.0 );
	addIndicator (  "parkway"   , IND_STREET , 1.0 );
	addIndicator (  "pkway"     , IND_STREET , 1.0 );
	addIndicator (  "pkwy"      , IND_STREET , 1.0 );
	addIndicator (  "straße", IND_STREET , 1.0 ); //!test this!
	addIndicator (  "strasse"   , IND_STREET , 1.0 );
	addIndicator (  "sr"   , IND_STREET , 1.0 ); // state route

	addIndicator (  "trail"      , IND_STREET , 1.0 );
	// 80 mosby's run
	addIndicator (  "run"      , IND_STREET , 1.0 );
	addIndicator (  "entrada"  , IND_STREET , 1.0 );

	// these were taken from http://en.wikipedia.org/wiki/Street_name
	addIndicator ( "Autobahn" , IND_STREET , 1.0 );
	addIndicator ( "Auto-estrada" , IND_STREET , 1.0 );
	addIndicator ( "Autoroute" , IND_STREET , 1.0 );
	addIndicator ( "Autostrada" , IND_STREET , 1.0 );
	addIndicator ( "Autostrasse" , IND_STREET , 1.0 );
	addIndicator ( "Byway" , IND_STREET , 1.0 );
	addIndicator ( "Expressway" , IND_STREET , 1.0 );
	addIndicator ( "Freeway" , IND_STREET , 1.0 );
	addIndicator ( "Motorway" , IND_STREET , 1.0 );
	addIndicator ( "Pike" , IND_STREET , 1.0 );
	addIndicator ( "Avenue" , IND_STREET , 1.0 );
	addIndicator ( "Boulevard" , IND_STREET , 1.0 );
	addIndicator ( "Road" , IND_STREET , 1.0 );
	addIndicator ( "rd" , IND_STREET , 1.0 );
	addIndicator ( "Street" , IND_STREET , 1.0 );
	
	addIndicator ( "Alley" , IND_STREET , 1.0 );
	addIndicator ( "Bay" , IND_STREET , 1.0 );
	addIndicator ( "Drive" , IND_STREET , 1.0 );
	addIndicator ( "Fairway" , IND_STREET , 1.0 );
	addIndicator ( "Gardens" , IND_STREET , 1.0 );
	addIndicator ( "Gate" , IND_STREET , 1.0 );
	addIndicator ( "Grove" , IND_STREET , 1.0 );
	addIndicator ( "Heights" , IND_STREET , 1.0 );
	addIndicator ( "Highlands" , IND_STREET , 1.0 );
	addIndicator ( "Knoll" , IND_STREET , 1.0 );
	addIndicator ( "Lane" , IND_STREET , 1.0 );
	addIndicator ( "Manor" , IND_STREET , 1.0 );
	addIndicator ( "Mews" , IND_STREET , 1.0 );
	addIndicator ( "Passage" , IND_STREET , 1.0 );
	addIndicator ( "Pathway" , IND_STREET , 1.0 );
	addIndicator ( "Place" , IND_STREET , 1.0 );
	addIndicator ( "Row" , IND_STREET , 1.0 );
	addIndicator ( "Terrace" , IND_STREET , 1.0 );
	addIndicator ( "Trail" , IND_STREET , 1.0 );
	addIndicator ( "View" , IND_STREET , 1.0 );
	addIndicator ( "Way" , IND_STREET , 1.0 );
	
	addIndicator ( "Close" , IND_STREET , 1.0 );
	addIndicator ( "Court" , IND_STREET , 1.0 );
	addIndicator ( "Cove" , IND_STREET , 1.0 );
	addIndicator ( "Croft" , IND_STREET , 1.0 );
	addIndicator ( "Garth" , IND_STREET , 1.0 );
	addIndicator ( "Green" , IND_STREET , 1.0 );
	addIndicator ( "Lawn" , IND_STREET , 1.0 );
	addIndicator ( "Nook" , IND_STREET , 1.0 );
	addIndicator ( "Place" , IND_STREET , 1.0 );
	
	addIndicator ( "Circle" , IND_STREET , 1.0 );
	addIndicator ( "Crescent" , IND_STREET , 1.0 );
	addIndicator ( "Loop" , IND_STREET , 1.0 );
	addIndicator ( "Lp" , IND_STREET , 1.0 ); // abbreviation for loop
	addIndicator ( "Oval" , IND_STREET , 1.0 );
	addIndicator ( "Quadrant" , IND_STREET , 1.0 );
	addIndicator ( "Square" , IND_STREET , 1.0 );
	
	addIndicator ( "Canyon" , IND_STREET , 1.0 );
	addIndicator ( "Causeway" , IND_STREET , 1.0 );
	addIndicator ( "Grade" , IND_STREET , 1.0 );
	addIndicator ( "Hill" , IND_STREET , 1.0 );
	addIndicator ( "Mount" , IND_STREET , 1.0 );
	addIndicator ( "Parkway" , IND_STREET , 1.0 );
	addIndicator ( "Rise" , IND_STREET , 1.0 );
	addIndicator ( "Vale" , IND_STREET , 1.0 );
	
	addIndicator ( "Approach" , IND_STREET , 1.0 );
	addIndicator ( "Bypass" , IND_STREET , 1.0 );
	addIndicator ( "Esplanade" , IND_STREET , 1.0 );
	addIndicator ( "Frontage road" , IND_STREET , 1.0 );
	addIndicator ( "Parade" , IND_STREET , 1.0 );
	addIndicator ( "Park" , IND_STREET , 1.0 );
	addIndicator ( "Plaza" , IND_STREET , 1.0 );
	addIndicator ( "Promenade" , IND_STREET , 1.0 );
	addIndicator ( "Quay" , IND_STREET , 1.0 );
	addIndicator ( "Stravenue" , IND_STREET , 1.0 );
	// was matching intersection "8k run and walk"
	//addIndicator ( "Walk" , IND_STREET , 1.0 );
	// italy?
	addIndicator ( "via" , IND_STREET , 1.0 );
	

	// try to load places.dat. the new junk first
	if ( ! loadPlaces ( ) ) return false;

	// we do zips separate now! use wordId as the key
	if ( ! g_zips.set ( 8,sizeof(ZipDesc),0,NULL,0,true,0,"tbl-zipcodes")){
		char *xx=NULL;*xx=0; }

	// zip codes reference city strings stored in this buffer
	char *cityBuf     = NULL;
	int32_t  cityBufSize = 0;
	// load zip code table
	bool loadedZips = false;
	if ( g_zips.load ( g_hostdb.m_dir,"zips.dat",&cityBuf,&cityBufSize)) {
		// sanity check
		//if ( g_zips.m_numSlotsUsed != 89471 ) { char*xx=NULL;*xx=0;}
		if ( g_zips.m_numSlotsUsed != 43595 ) { char*xx=NULL;*xx=0;}
		loadedZips = true;
		int64_t h = hash64 ( "87109" , 5 );
		// test the zips table
		if ( g_zips.getSlot ( &h ) < 0 ){char *xx=NULL;*xx=0; }
		// . assign it
		// . ZipDesc::m_cityOffset reference this buffer
		g_cityBuf     = cityBuf;
		g_cityBufSize = cityBufSize;
	}

	// . quickly set the states
	// . map each name of a state to its index into s_states[] array
	g_states.set ( 8 , 4 , 256 , NULL , 0 , false , 0 ,"adm1tbl");
	int32_t size = sizeof(s_states);
	// item count
	int32_t n = (int32_t)size/ sizeof(StateDesc); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get it
		StateDesc *sd = &s_states[i];
		// get hash of abbr
		int64_t h = hash64n ( sd->m_adm1 );
		// make the value
		//int32_t val = 0;
		// shift up
		//val <<= 8;
		// or in the position
		//val |= i;
		// no dups
		if ( g_states.isInTable ( &h ) ) { char *xx=NULL;*xx=0; }
		// store it
		if ( ! g_states.addKey ( &h , &sd ) ) { char*xx=NULL;*xx=0; }
		// stop if done
		if ( ! sd->m_name1 ) continue;
		// then the second name
		h = getWordXorHash ( sd->m_name1 );
		// must be there
		if ( ! h ) { char *xx=NULL;*xx=0; }
		// flag it
		//val = 1;
		// shift up
		//val <<= 8;
		// or in the position
		//val |= i;
		// no dups
		if ( g_states.isInTable ( &h ) ) { char *xx=NULL;*xx=0; }
		// store it
		if ( ! g_states.addKey ( &h , &sd ) ) { char*xx=NULL;*xx=0; }
		// and the second name
		if ( ! sd->m_name2 ) continue;
		// then the second name
		h = getWordXorHash ( sd->m_name2 );
		// must be there
		if ( ! h ) { char *xx=NULL;*xx=0; }
		// flag it as second name
		//val = 2;
		// shift up
		//val <<= 8;
		// or in the position
		//val |= i;
		// no dups
		if ( g_states.isInTable ( &h ) ) { char *xx=NULL;*xx=0; }
		// store it
		if ( ! g_states.addKey ( &h , &sd ) ) { char*xx=NULL;*xx=0; }
	}

	// . timezone table
	// . hash of city and adm1 is the key
	// . maps to a one byte timezone offset, usually negative
	g_timeZones.set ( 4         ,
			  sizeof(CityStateDesc),// 1 byte date timezone offset
			  0         ,
			  NULL      ,
			  0         ,
			  false     , // dups?
			  0         , // niceness
			  "tbl-tzs" );


	if ( loadedZips && !g_timeZones.load(g_hostdb.m_dir,"timezones.dat")){
		log("places: failed to load timezones.dat");
		loadedZips = false;
	}

	int32_t vv = 185747;
	if ( g_timeZones.m_numSlotsUsed && g_timeZones.m_numSlotsUsed!=vv){
		log("places: bad timezones.dat file %"INT32" != %"INT32"",
		    g_timeZones.m_numSlotsUsed,vv);
		return false;
	}
	// sanity
	if ( g_timeZones.m_numSlotsUsed ) {
		char udst;
		char tzoff;
		tzoff = getTimeZone2 ( "houston", "tx", &udst );
		if ( tzoff == UNKNOWN_TIMEZONE ) { char *xx=NULL;*xx=0; }
		if ( tzoff != -5 ) { char *xx=NULL;*xx=0; }
		tzoff = getTimeZone2 ( "woods hole", "ma", &udst );
		if ( tzoff == UNKNOWN_TIMEZONE ) { char *xx=NULL;*xx=0; }
		tzoff = getTimeZone2 ( "albuquerque", "nm", &udst );
		if ( tzoff == UNKNOWN_TIMEZONE ) { char *xx=NULL;*xx=0; }
	}


	// map a cityHash/state of an aliased city name to a normalized cityId
	if ( ! g_aliases.set(4,4,128,NULL,0,false,0,"aliastab") )
		return false;

	// load the aliases
	if ( loadedZips && g_aliases.load ( g_hostdb.m_dir , "aliases.dat")){
		// match this
		int32_t na = 11663;//11462;
		// sanity check
		if ( g_aliases.m_numSlotsUsed != na){char*xx=NULL;*xx=0;}
	}

	// . init the hash table
	// . use an 8-byte hash for the key
	// . xor the wids together for quick lookups
	// . all subphrases that include the first word of the place name will
	//   be hashed, that way we know if we should hash further
	// . also, we should allow dups!
	// . use a 6 byte key (truncated wordId) to use up less space!
	g_cities.set ( 8                 ,  // keySize
		       sizeof(CityDesc)  ,  // adm1 bit vector + mostpopcity
		       0                 ,  // initial # slots 
		       NULL              ,  // initial buf
		       0                 ,  // initial buf size
		       true              ,  // allowDup keys?
		       0                 ,  // niceness
		       "tbl-places"      );

	// try to load the binary hash table first
	if ( loadedZips && g_cities.load ( g_hostdb.m_dir , "cities.dat" ) ) {
		// sanity check
		int32_t nc = 123347; // 123141;
		if ( g_cities.m_numSlotsUsed != nc){char*xx=NULL;*xx=0;}
		// another test
		char *str;
		//char *str = "nm";
		//str = "madrid";
		//int64_t h = hash64 (str,gbstrlen(str));
		int64_t h = 0;
		//h =  hash64 ("santa",5);
		//h ^= hash64 ("n",1);
		h =  hash64n ("jemez");

		h <<= 1;
		//h ^= hash64 ("fe",2);
		//h ^= hash64 ("m",1);
		h ^= hash64n("springs");

		//str = "santa fe";
		//str = "n.m.";
		str = "jemez springs";

		//h = hash64 ( "abq",3);
		//str = "abq";

		//h = hash64 ( "alb",3);
		//str = "alb";

		//str = "albuquerque";
		//h = hash64 ( str,gbstrlen(str) );

		str = "new york";
		h = getWordXorHash ( str );


		// make sure we got madrid nm
		//int32_t slot = g_cities.getSlot ( &h );

		//if ( slot < 0 ) { char *xx=NULL;*xx=0; }

		CityDesc *cd = (CityDesc *)g_cities.getValue(&h);
		if ( ! cd ) { char *xx=NULL;*xx=0; }
	
		uint64_t abits = getAdm1Bits ( "ny" );
		if ( ! ( cd->m_adm1Bits & abits ) ) { char *xx=NULL;*xx=0;}

		// check city ids
		int64_t abqh1 = getWordXorHash("abq");
		int64_t abqh2 = getWordXorHash("albuquerque");
		uint32_t cid1 = getCityId32(abqh1,"nm");
		uint32_t cid2 = getCityId32(abqh2,"nm");
		if ( cid1 != cid2 ) { char *xx=NULL;*xx=0; }

		// get nm
		int64_t hnm = getWordXorHash("new mexico");
		// get state descriptor
		int32_t pos = getStateOffset ( &hnm );
		// sanity
		if ( pos < 0 ) { char *xx=NULL;*xx=0; }
		// make bit mask
		uint64_t mask = 1LL << pos;
		// and in nm
		if ( ! ((cd->m_adm1Bits) & mask) ) { char *xx=NULL;*xx=0;}
		/*
		// a nested loop
		for ( ; slot >= 0 ; slot = g_cities.getNextSlot(slot,&h)) {
			// get the place
			pd = (PlaceDesc *)g_cities.getValueFromSlot(slot);

			// map to alias?
			if ( pd->m_bits & PLF_ALIAS )
pd=(PlaceDesc *)g_cities.getValueFromSlot(pd->getSlot());

			if ( ! is_ascii(pd->m_adm1[0]) ||
			     ! is_ascii(pd->m_adm1[1]) ) {
				char *xx=NULL;*xx=0; }
			// print it
			log("places: h=%s adm1=%c%c ctry=%s",
			    str,
			    pd->m_adm1[0],
			    pd->m_adm1[1],
			    g_countryCode.getName(pd->m_crid-1));
		}
		*/
		// now hash for zip code
		//h = hash64Lower_a("BC",2);
		//int64_t h1 = hash64("n",1);
		//int64_t h2 = hash64("m",1);
		//int64_t h3 = (h1<<1LL) ^ h2;

		char *zstr = "87102";
		h = hash64 ( zstr,gbstrlen(zstr));
		//h = hash64 ("78404",5);
		//slot = g_cities.getSlot ( &h );
		int32_t slot = g_zips.getSlot ( &h );

		//char *city="Corpus Christi";
		char *city="Albuquerque";
		int64_t ch = hash64Lower_utf8(city,gbstrlen(city));
		//int32_t ch = (int32_t)(th64&0xffffffff);
		log("places: %s hash = %"UINT64"",city,ch);
		// a nested loop
		for ( ; slot >= 0 ; slot = g_zips.getNextSlot(slot,&h)) {
			// get the place
			ZipDesc *zd;
			zd = (ZipDesc *)g_zips.getValueFromSlot(slot);
			// convert adm1 bit to adm1 code
			StateDesc *sd = getStateDescFromBits(zd->m_adm1Bits);
			// must be there
			if ( ! sd ) { char *xx=NULL;*xx=0; }
			//if(!is_ascii(zd->m_adm1[0]) ) {char *xx=NULL;*xx=0;}
			// print it
			log("places: h=%s cityhash=%"UINT64" adm1=%s "//adm1=%c%c "
			    "pd=0x%"PTRFMT"",
			    zstr,
			    zd->m_cityHash,
			    sd->m_name1,
			    //zd->m_adm1[0],
			    //zd->m_adm1[1],
			    //g_countryCode.getName(zd->m_crid-1),
			    (PTRTYPE)zd);
			if ( zd->m_cityHash != ch ) { char*xx=NULL;*xx=0; }
		}
		// exit until we get "nm" and "bc" for british columbia!!!
		//log("hey hey!!!!!!!!!!!!!!!!! fix me you");
		//exit(-1);
		// otherwise, we passed
		//if ( loadedIndicators ) return true;
		return true;
		//loadedCities = true;
	}

	// let them know that we are creating it
	logf(LOG_INFO,"places: creating cities.dat");

	g_cities.reset();
	g_zips.reset();
	g_timeZones.reset();
	g_aliases.reset();
	//g_states.reset();

	// init with 8M slots
	//g_cities.set ( 6,sizeof(PlaceDesc),6950000,NULL,0,true,0);
	// 1M since doing USA only now. now cities.dat is only 12MB not 100MB
	// uses 731k slots
	//g_cities.set ( 8,sizeof(PlaceDesc),100000,NULL,0,true,0,"placestbl");

	// this now maps just a city to the state/adm1 bit vector of the states
	// it is in... AND the one byte timezone offset
	g_cities.set ( 8,sizeof(CityDesc),100000,NULL,0,false,0,"placestbl");

	// we do zips separate now! use wordId as the key (89k used)
	if ( ! g_zips.set ( 8,sizeof(ZipDesc),10000,NULL,0,true,0,"zipstbl")) {
		char *xx=NULL;*xx=0; }

	if (!g_timeZones.set(4,sizeof(CityStateDesc),100000,NULL,0,false,0,
			     "tbl99")){ char *xx=NULL;*xx=0;}

	// map a cityHash/state of an aliased city name to a normalized cityId
	if ( ! g_aliases.set(4,4,128,NULL,0,false,0,"aliastab") )
		return false;


	// keep track of max population for each city name and the state
	// in which that max population occurs
	HashTableX maxPops;
	maxPops.set (8,4,100000,NULL,0,false,0,"poptbl");


	//////////////////////////////////////////////////////////////
	//
	// LOAD THE allCountries.txt file
	//
	//////////////////////////////////////////////////////////////

	// geonameid         : integer id of record in geonames database
	// name              : name of geographical point (utf8) varchar(200)
	// asciiname         : name of geographical point in plain ascii 
	//                     characters, varchar(200)
	// alternatenames    : alternatenames, comma separated varchar(4000) 
	//                     (varchar(5000) for SQL Server)
	// latitude          : latitude in decimal degrees (wgs84)
	// longitude         : longitude in decimal degrees (wgs84)
	// feature class     : see http://www.geonames.org/export/codes.html, 
	//                     char(1)
	// feature code      : see http://www.geonames.org/export/codes.html, 
	//                     varchar(10)
	// country code      : ISO-3166 2-letter country code, 2 characters
	// cc2               : alternate country codes, comma separated, 
	//                     ISO-3166 2-letter country code, 60 characters
	// admin1 code       : fipscode (subject to change to iso code), 
	//                     isocode for the us and ch, see file 
	//                     admin1Codes.txt for display names of this code;
	//                     varchar(20)
	// admin2 code       : code for the second administrative division, a 
	//                     county in the US, see file admin2Codes.txt; 
	//                     varchar(80)
	// admin3 code       : code for third level administrative division, 
	//                     varchar(20)
	// admin4 code       : code for fourth level administrative division, 
	//                     varchar(20)
	// population        : bigint (4 byte int)
	// elevation         : in meters, integer
	// gtopo30           : average elevation of 30'x30' (ca 900mx900m) 
	//                     area in meters, integer
	// timezone          : the timezone id (see file timeZone.txt)
	// modification date : date of last modification in yyyy-MM-dd format


	// . make the filename to open
	// . downloadeded from http://geonames.org/allCountries.zip ?
	// . sample line = 
	//   3038840 Serrat de Ventader Serrat de Ventader         42.4833333
	//   1.4333333       T       MT AD           00
	char ff[1024];
	sprintf ( ff , "%sallCountries.txt", g_hostdb.m_dir );
	// places.txt is just the United States
	//sprintf ( ff , "%splaces.txt", g_hostdb.m_dir );
	logf(LOG_INFO,"places: reading %s",ff);
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd )
		return log("places: failed to open %s: %s",ff,strerror(errno));



	// count how many times we see each word for purposes of establishing
	// the most common indicators of a place. i.e. "center", "square",...
	//HashTableX ct;
	// init with 8M places too
	//ct.set ( 8 , 4 , 9300000,NULL,0,false,0 ,"addrcmmn");

	// similar to "ct" but we incorporate latitude/longitude to restrict
	// voting in order to remove "local words", like Edisto!
	//HashTableX gvt;
	//gvt.set ( 8 , 0 , 30000 ,NULL,0,false,0,"addrgvt" );

	HashTableX popTable;
	popTable.set ( 4,4,30000,NULL,0,false,0,"poptab");


	int32_t badEntry = 0;

	int32_t line = 0;

	//int32_t MAX = 0;

	// . go through the places in allCountries.txt
	// . format described in /gb/geo/geonames/readme.txt
	char buf[10000];
	// for debuging
	char *dbuf = buf;

	//char  topBuf[1000000];
	//char *topBufPtr = topBuf;
	// map a wid to a string ptr with this table, "st"
	HashTableX st;
	st.set ( 8 , 4 , 30000 , NULL,0,false,0 ,"addrst");

	while ( fgets ( buf , 10000 , fd ) ) {
		// tmp debug for postalCodes.txt
		//break;
		// length of line, including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// sanity check
		if ( wlen >= 9000 ) { char *xx=NULL;*xx=0; }
		// skip if empty
		if ( wlen <= 0 ) continue;
		// null terminate it, instead of \n
		buf[wlen-1]='\0';

		// debug point
		//char *poo = strstr(buf,"Town of North Hempstead" ); if (poo)
		//	log("hey");

		// log it
		if ( (line % 10000) == 0 )
			log(LOG_INFO,"places: read line #%"INT32" out of "
			    "6,900,574 (%"INT32" places added)",line,
			    g_cities.m_numSlotsUsed);
		line++;

		// country id
		uint8_t crid = 0;
		// country code
		char cc[3];
		cc[0] = 0;
		cc[1] = 0;
		// admin1code
		char a1[2];
		// admin2code
		//char a2[2];
		// reset
		a1[0] = a1[1] = 0;
		// descriptive bits
		//pbits_t bits = 0;
		// place type
		placetype_t ptype = 0;
		// official name of the place
		char *name = NULL;
		// the ascii version
		char *ascii = NULL;
		// comma-separated abbreviations and alternative names
		char *alt = NULL;
		// stop after this char ptr
		char *stop = NULL;
		double latitude = 0.0;
		double longitude = 0.0;
		// population of the city/place
		int32_t pop = 0;
		// count tabs
		int32_t tabs = 0;
		// point to the beginning of the line
		char *p = buf;
		char tzoff = 0;
		char useDST; // daylight savings time
		// debug point
		//if ( strncmp(buf,"2241297\t", 8) ==0 ) 
		//if ( strncmp(buf,"3856157\t", 8) ==0 ) 
		//	log("gotit");

		// parse out the tab delimeted things from the line
		for ( ; *p ; p++ ) {
			// skip if no tab
			if ( *p != '\t' ) continue;
			// count tabs
			tabs++;
			// point "s" to right after the tab
			char *s = p + 1;
			// done?
			if ( ! *s ) break;
			// after first tab is the official place name
			if ( tabs == 1 ) name  = s;
			// then the name in ascii
			if ( tabs == 2 ) ascii = s;
			// then comma-separated list of alternative names
			if ( tabs == 3 ) alt   = s;
			// the latitude
			if ( tabs == 4 ) {
				// a stopping point for "alt"
				stop  = s;
				// get it
				latitude = atof(s);
			}
			// the longitude
			if ( tabs == 5 ) {
				// get it
				longitude = atof(s);
			}
			// . the category of place is after the 6th tab
			// . the specific type of place is after the 7th tab
			// . see http://www.geonames.org/export/codes.html
			// . to save mem, only hash certain types...
			if ( tabs == 7 ) {
				// this is usually a state in the U.S.
				if      ( ! strncmp(s,"ADM1",4) ) 
					ptype = 0;//PT_STATE;
				// this is usually a county in the U.S.
				else if ( ! strncmp(s,"ADM2",4) ) 
					ptype = 0;//PT_ADM2;
				// this is usually a county in the U.S.
				else if ( ! strncmp(s,"ADM3",4) ) 
					ptype = 0;//PT_ADM3;
				// this is usually a county in the U.S.
				else if ( ! strncmp(s,"ADM4",4) ) 
					ptype = 0;//PT_ADM4;
				// populated place = city
				else if ( ! strncmp(s,"PPL" ,3) ) 
					ptype = PT_CITY;
				// town of, township, etc.
				// town of north hempstead
				// . crap! this gets a different san jose!
				else if ( ! strncmp(s,"ADMD" ,4) ) 
					ptype = PT_CITY;
				// locality
				else if ( ! strncmp(s,"LCTY" ,4) ) 
					ptype = PT_CITY;
				// independent political entity
				else if ( ! strncmp(s,"PCLIX" ,4) ) 
					ptype = PT_CITY;
				else if ( ! strncmp(s,"P\t" ,2) ) 
					ptype = PT_CITY;
				// independent political entity = country
				else if ( ! strncmp(s,"PCLI",4) ) 
					ptype = PT_COUNTRY;
				// allow schools (popular meeting place)
				else if ( ! strncmp(s,"SCH",3) )
					ptype = 0;//PT_SCH;
				// and parks (popular meeting place)
				else if ( ! strncmp(s,"PRK",3) )
					ptype = 0;//PT_PRK;
			}
			// . country code (two letters)
			// . sometimes things like a gulf of aden has no
			//   associated country code!
			if ( tabs == 8 && s[0] != '\t' ) { 
				cc[0] = to_lower_a(s[0]); 
				cc[1] = to_lower_a(s[1]); 
				cc[2] = 0;
				crid = getCountryId ( cc );
				// sanity check
				if ( s[2]!='\t'&&s[2]) { char *xx=NULL;*xx=0;}
				continue;
			}
			// alternate country code (two letters)
			if ( tabs == 9 && ! crid && s[0] != '\t' ) {
				cc[0] = to_lower_a(s[0]); 
				cc[1] = to_lower_a(s[1]); 
				cc[2] = 0;
				crid = getCountryId ( cc );
			}

			// . admin1 code (two letters)
			// . readme.txt says varchar(20) but
			//   /gb/geo/admin1Codes.txt seems to say 2 chars
			// . actually i have seen 3 letter ones... but they
			//   if truncated to two chars would be unique in their
			//   respective country. i.e. GB.ENG, GB.NIR, ...
			// . BUT for GR.ESYE11 through GR.ESYE14, ... just use
			//   the last two chars!
			if ( tabs == 10 ) {
				// usually these 2 chars are digits!
				a1[0] = to_lower_a(s[0]); 
				a1[1] = to_lower_a(s[1]); 
				// panic!
				if ( s[2] == '\t' ) continue;
				// watch out for GReece
				if ( cc[0] != 'g' ) continue;
				if ( cc[1] != 'r' ) continue;
				// and its "states" (admin1 codes)
				if ( a1[0] != 'e' ) continue;
				if ( a1[1] != 's' ) continue;
				// use the last two for this guy!
				s += 4;
				if ( ! is_digit(s[0]) ) continue;
				if ( ! is_digit(s[1]) ) continue;
				a1[0] = s[0];
				a1[1] = s[1];
			}
			// pop is timezone - 3
			if ( tabs == 14 ) {
				// get it
				pop = atol(s);
			}
			// timezone
			if ( tabs == 17 ) {
				char *tzname = p + 1;
				// assume we use daylights savings time
				useDST = 1;
				// assume not found
				tzoff = 0;
				// find the end, a tab i guess or wsapce
				char *e = tzname;
				for ( ; *e && ! is_wspace_a(*e) ; e++ );
				// temp null term
				char saved = *e;
				*e = '\0';
				// convert to timezone offset
				if ( ! strcmp(tzname,"America/Chicago") )
					tzoff = -6;
				else if ( ! strcmp(tzname,"America/Anchorage"))
					tzoff = -9;
				else if ( ! strcmp(tzname,"America/Indiana/Knox"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Kentucky/Monticello"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Boise"))
					tzoff = -7;
				else if ( ! strcmp(tzname,"America/Indiana/Indianapolis"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Indiana/Marengo"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Indiana/Petersburg"))
					tzoff = -6;
				else if ( ! strcmp(tzname,"America/Indiana/Tell_City"))
					tzoff = -6;

				else if ( ! strcmp(tzname,"America/Indiana/Vevay"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Indiana/Vincennes"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Indiana/Winamac"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Juneau"))
					tzoff = -9;
				else if ( ! strcmp(tzname,"America/Kentucky/Louisville"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"America/Menominee"))
					tzoff = -6;
				else if ( ! strcmp(tzname,"America/Nome"))
					tzoff = -9;
				else if ( ! strcmp(tzname,"America/North_Dakota/Center"))
					tzoff = -6;
				else if ( ! strcmp(tzname,"America/North_Dakota/New_Salem"))
					tzoff = -6;
				else if ( ! strcmp(tzname,"America/Shiprock"))
					tzoff = -7;
				else if ( ! strcmp(tzname,"America/Yakutat"))
					// could not find this - guessing
					tzoff = -9;

				else if ( ! strcmp(tzname,"America/Detroit"))
					tzoff = -5;
				else if ( !strcmp(tzname,"America/St_Thomas")){
					tzoff = -4;
					useDST = 0;
				}
				else if ( ! strcmp(tzname,"Pacific/Kwajalein"))
					tzoff = -12;


				else if ( ! strcmp(tzname,"America/Adak"))
					tzoff = -10;
				else if ( ! strcmp(tzname,"America/Phoenix")){
					tzoff = -7; useDST = 0; }
				else if ( ! strcmp(tzname,"America/Denver"))
					tzoff = -7;
				else if (!strcmp(tzname,"America/Los_Angeles"))
					tzoff = -8;
				else if ( ! strcmp(tzname,"America/New_York"))
					tzoff = -5;
				else if ( ! strcmp(tzname,"Pacific/Honolulu")){
					tzoff = -10; useDST = 0; }
				// amchitka in alasakn aleutian islands...
				else if ( ! tzname[0] ) 
					tzoff = 0;
				else {
					char *xx=NULL;*xx=0; }
				// restore
				*e = saved;
			}
		}

		// break point
		//if ( name && strncasecmp(name,"Madrid\t",7)==0 )
		//	log("hey");

		// skip if not a place we are interested in
		//if ( ! bits ) 
		//	continue;

		if ( ! crid ) {
			badEntry++;
			log("places: bad country for "
			        "for %s",dbuf);
			continue;
		}

		// must have all 4 things here:
		if ( !a1[0] || ! name ) {
			//log("places: %s does not have country of adm1",name);
			badEntry++;
			continue;
		}

		// skip all NON-USA places now that we are specializing
		// no, now we had facebook events from all over, if they
		// have a lat/lon! yeah, so let foreign cities through...
		//if ( crid != CRID_US )continue;

		// only store cities for now
		if ( ! ptype ) continue;
		// sanity check
		if ( ! is_ascii(a1[0]) || ! is_ascii(a1[1]) ) {
			//log("places: bad %s",name);
			badEntry++;
			continue;
		}
		// what is this???? i see "00"
		if ( is_digit(a1[0]) ) continue;

		uint64_t h_washington =  hash64n ("washington");
		uint64_t h_dc =  hash64n ("dc");
		uint64_t h_d  =  hash64n ("d");
		uint64_t h_c  =  hash64n ("c");
		uint64_t h_wdc = h_washington;
		h_wdc <<= 1;
		h_wdc ^= h_dc;
		uint64_t h_wdc2 = h_washington;
		h_wdc2 <<= 1;
		h_wdc2 ^= h_d;
		h_wdc2 <<= 1;
		h_wdc2 ^= h_c;
		
		// set nameEnd/asciiEnd/altEnd
		char *nameEnd = name;
		for (;nameEnd;nameEnd++)
			if(*nameEnd ==','||*nameEnd=='\t'||!*nameEnd ) break;
		char *asciiEnd = ascii;
		for (;asciiEnd;asciiEnd++)
			if(*asciiEnd ==','||*asciiEnd=='\t'||!*asciiEnd)break;
		char *altEnd = alt;
		for ( ; altEnd ; altEnd++ ) 
			if (*altEnd==','||*altEnd=='\t'||!*altEnd) break;
		// null terms
		*nameEnd  = '\0';
		*asciiEnd = '\0';
		*altEnd   = '\0';

		// ok, now we need to grab the place id in the file and
		// use that to reference the alt names table we hashed up
		// top. because that includes the language code of the
		// altname!!!
		// then we need to make a string like
		// cs.en.nb.nn.sk=Egypt,fy.nl=Egypte,fi=Egypti
		// and store that into a buffer for each place. then the
		// city desc needs to references that buffer. we also hash
		// every alt name to point to the same CityDesc or CountryDesc
		// or StateDesc whichever type of place it is...
		//
		// MDW LEFT OFF HERE

		uint64_t h = getWordXorHash ( name );

		// hashes we added, to dedup
		//HashTableX dt;
		//char buf[10000];
		//dt.set ( 6,0,100,buf,10000,false,0);

		// do not add "washington, dc" as a city, treat
		// dc as a state!!
		if ( h == h_wdc ) 
			continue;
		if ( h == h_wdc2 ) 
			continue;
		// no dups!
		//if ( dt.isInTable(&h ) ) continue;
		// add it
		//if ( ! dt.addKey(&h) ) { char *xx=NULL;*xx=0; }

		// normalize this
		char adm1[3];
		adm1[0] = to_lower_a(a1[0]);
		adm1[1] = to_lower_a(a1[1]);
		adm1[2] = 0;

		// use this now
		uint32_t cid32 = (uint32_t)getCityId32(h,a1);

		// we add 100 to the timeZoneOffset to indicate it 
		// does not use DST
		//if ( useDST == 0 ) tzoff += 100;

		// already in there?
		int32_t slot = g_timeZones.getSlot ( &cid32 );
		if ( slot >= 0 ) {
			CityStateDesc *csd ;
			csd = (CityStateDesc *)g_timeZones.
				getValueFromSlot(slot);
			char tv = csd->m_timeZoneOffset;
			if ( tv != tzoff ) { 
				log("places: bad city timezone "
				    "csh=%"UINT32" z: %s",
				    (uint32_t)cid32,
				    name);
				//char *xx=NULL;*xx=0; }
			}
			// get the pop from this
			int32_t cpop = *(int32_t *)popTable.getValue ( &cid32 );
			// if already in there, and this has more pop,
			// then use it!
			if ( pop > cpop ) {
				csd->m_latitude       = latitude;
				csd->m_longitude      = longitude;
				popTable.addKey ( &cid32, &pop );
			}
		}
		// timezone table maps city/state pair to a tzoffset
		else {
			// for each city/state pair we must store its
			// lat/lon now too
			CityStateDesc csd;
			csd.m_timeZoneOffset = tzoff;
			csd.m_useDST         = useDST;
			csd.m_latitude       = latitude;
			csd.m_longitude      = longitude;
			g_timeZones.addKey ( &cid32 , &csd );
			popTable.addKey ( &cid32, &pop );
		}
		

		// add city name to the temporary hashtable of CityDescriptors.
		// later we will serialize it into g_cityDescBuf and make
		// the g_city hash table map ptrs into that. i think
		// we can save it in cities.dat because HashTableX provides
		// the mechanism for that.
		addCity ( h , adm1 , pop , &maxPops );

		// if the ascii hash is different, add as alias
		addAlias ( ascii, adm1, h,pop, &maxPops );
		// and the alt hash
		addAlias ( alt, adm1, h,pop, &maxPops );

		// now add the alternate names of this city
		// as aliases, not just to g_cities, but also to
		// g_aliases
		int32_t len = gbstrlen(name);
		if ( strncmp(name,"Township of ",12) == 0 )
			addAlias ( name + 12,adm1,h,pop,&maxPops);
		if ( strncmp(name,"Town of ",8) == 0 )
			addAlias ( name + 8 ,adm1,h,pop,&maxPops );
		if ( strncmp(name,"City of ",7) == 0 )
			addAlias ( name + 7 ,adm1,h,pop,&maxPops );
		if ( strncmp(ascii,"Township of ",12) == 0 )
			addAlias ( ascii + 12,adm1,h,pop,&maxPops);
		if ( strncmp(ascii,"Town of ",8) == 0 )
			addAlias ( ascii + 8 ,adm1,h,pop,&maxPops );
		if ( strncmp(ascii,"City of ",7) == 0 )
			addAlias ( ascii + 7 ,adm1,h,pop,&maxPops );
		// "New York City" equals "New York"
		char *tail = name+len-5;
		if ( len >=6 && strncmp(tail," City",5)==0) {
			*tail = '\0';
			addAlias ( name ,adm1,h,pop,&maxPops );
			*tail = ' ';
		}
		tail = ascii+len-5;
		if ( len >=6 && strncmp(tail," City",5)==0) {
			*tail = '\0';
			addAlias ( ascii ,adm1,h,pop,&maxPops );
			*tail = ' ';
		}
	}

	/*
	// now scan each city in g_cities and set their CF_SINGLE_STATE
	// flag if they only have one state
	for ( int32_t i = 0 ; i < g_cities.m_numSlots ; i++ ) {
		// skip empty slots
		if ( ! g_cities.m_flags[i] ) continue;
		// get the data value
		uint64_t *bv = (uint64_t *)g_cities.getValueFromSlot(i);
		// count bits on
		int32_t nb = getNumBitsOn(*bv);
		// sanity check
		if ( nb == 0 ) { char *xx=NULL;*xx=0; }
		// if only 1 set this flag
		if ( nb == 1 ) *bv |= CF_UNIQUE;
	}
	*/

	// close that file
	fclose(fd);

	//logf(LOG_INFO,"places: allCountries.txt had %"INT32" bad entries.",
	logf(LOG_INFO,"places: places.txt had %"INT32" bad entries.",
	     badEntry);

	// reset for this file
	badEntry = 0;

	//////////////////////////////////////////////////////////////
	//
	// LOAD THE postalCodes.txt file
	//
	//////////////////////////////////////////////////////////////

	//country code      :iso country code, 2 characters
	//postal code       :varchar(10)
	//place name        :varchar(180)
	//admin name1       :1. order subdivision (state) varchar(100)
	//admin code1       :1. order subdivision (state) varchar(20)
	//admin name2       :2. order subdivision (county/province) varchar(100
	//admin code2       :2. order subdivision (county/province) varchar(20)
	//admin name3       :3. order subdivision (community) varchar(100)
	//latitude          :estimated latitude (wgs84)
	//longitude         :estimated longitude (wgs84)
	//accuracy          :accuracy of lat/lng from 1=estimated to 6=centroid

	//
	// crap canadian state abbreviations are not in allCountries.txt
	// so use the "admin code1" in the postalCodes.txt file!
	//

	// . now read in the zip codes
	// . make the filename to open
	sprintf ( ff , "%spostalCodes.txt", g_hostdb.m_dir );
	logf(LOG_INFO,"places: reading %s",ff);
	fd = fopen ( ff, "r" );
	if ( ! fd )
		return log("places: failed to open %s: %s",ff,strerror(errno));

	// make the city buf
	SafeBuf sb;

	line = 0;

	// . go through the places in allCountries.txt
	// . format described in /gb/geo/geonames/readme.txt
	while ( fgets ( buf , 10000 , fd ) ) {
		// length of line, including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// sanity check
		if ( wlen >= 9000 ) { char *xx=NULL;*xx=0; }
		// skip if empty
		if ( wlen <= 0 ) continue;
		// null terminate it, instead of \n
		buf[wlen-1]='\0';

		// log it
		if ( (line % 10000) == 0 )
			log(LOG_INFO,"places: read postal line #%"INT32" out of "
			    "848,226 (%"INT32" places added)",line,
			    g_cities.m_numSlotsUsed);
		line++;

		// country id
		uint8_t crid = 0;
		// admin1code
		char a1[2];
		// reset
		a1[0] = a1[1] = 0;

		// count tabs
		int32_t tabs = 0;
		// point to the beginning of the line
		char *p = buf;
		// isoalte the zip code
		char *zip       = NULL;
		char *cityName  = NULL;
		char *a1name    = NULL;
		char *a2name    = NULL;
		//char *zipEnd = NULL;
		// parse out the tab delimeted things from the line
		for ( ; *p ; p++ ) {
			// a temp var
			char *s = p;
			// put country code here
			char cc[3];
			// first is country code
			if ( p == buf ) {
				cc[0] = to_lower_a(s[0]); 
				cc[1] = to_lower_a(s[1]); 
				cc[2] = 0;
				// sanity check
				if ( s[2] != '\t' ) { char *xx=NULL;*xx=0;}
				// to id
				crid = getCountryId ( cc );
				// must be valid
				//if ( ! crid ) { char *xx=NULL;*xx=0; }
				// there is a "gg" in there!
				if ( ! crid ) break;
				continue;
			}
			// skip if no tab
			if ( *p != '\t' ) continue;
			// count tabs
			tabs++;
			// after first tab is the POSTAL CODE
			if ( tabs == 1 ) {
				zip = p + 1;
				continue;
			}
			if ( tabs == 2 ) {
				// terminate zip for Words::set() below
				*p = '\0';
				cityName = p + 1;
				continue;
			}
			if ( tabs == 3 ) {
				// terminate for cityName
				*p = '\0';
				a1name = p + 1;
				continue;
			}
			// . after 4th tab is admin code1
			// . admin1 code (two letters)
			// . readme.txt says varchar(20) but
			//   /gb/geo/admin1Codes.txt seems to say 2 chars
			// . actually i have seen 3 letter ones... but they
			//   if truncated to two chars would be unique in their
			//   respective country. i.e. GB.ENG, GB.NIR, ...
			// . BUT for GR.ESYE11 through GR.ESYE14, ... just use
			//   the last two chars!
			if ( tabs == 4 ) {
				// terminate for a1name
				*p = '\0';
				// usually these 2 chars are digits!
				a1[0] = to_lower_a(p[1]); 
				a1[1] = to_lower_a(p[2]); 
				// one letter province/state code?
				if ( p[2] == '\t' ) {
					a1[1] = 0;
					continue;
				}
				// panic!
				if ( p[3] == '\t' ) continue;
				// watch out for GReece
				if ( cc[0] != 'g' ) continue;
				if ( cc[1] != 'r' ) continue;
				// and its "states" (admin1 codes)
				if ( a1[0] != 'e' ) continue;
				if ( a1[1] != 's' ) continue;
				// use the last two for this guy!
				s += 4;
				if ( ! is_digit(s[0]) ) continue;
				if ( ! is_digit(s[1]) ) continue;
				a1[0] = s[0];
				a1[1] = s[1];
			}
			if ( tabs == 5 ) {
				// terminate for cityName
				//*p = '\0';
				a2name = p + 1;
				continue;
			}
			if ( tabs == 6 ) {
				// terminate for a2name
				*p = '\0';
				continue;
			}
		}

		// if we got an illegit adm1 code try convert the admin 1 name
		bool legit = true;
		if ( !a1[0] ) 
			legit = false;
		if ( !is_ascii(a1[0]) ) 
			legit = false;
		if ( !is_ascii(a1[1]) ) 
			legit = false;
		// empty is NULL
		if ( a1name    && ! *a1name    ) a1name = NULL;
		if ( a2name    && ! *a2name    ) a2name = NULL;
		if ( cityName  && ! *cityName  ) cityName = NULL;
		//if ( is_ascii(a1[0])&&is_ascii(a1[1])&&is_ascii(a1[2]) ) 
		//	legit = false;
		// do we got this?
		//if ( ! legit && ! a1name ) continue;
		// not a chance to save ourselves if no adm1 name given
		if ( ! legit && ! a1name && ! a2name && ! cityName ) {
			badEntry++;
			continue;
		}

		// now we must have a valid a1name because as we have found
		// the adm1 code in postalCodes.txt does not always correspond
		// to those in allCountries.txt. like "british columbia" is
		// "02" in allCountries.txt and "bc" in postalCodes.txt.
		if ( ! a1name ) {
			badEntry++;
			continue;
		}

		//
		// skip all NON-USA places now that we are specializing
		//
		if ( crid != CRID_US )
			continue;

		/*
		// try to convert it
		PlaceDesc *tpd ;
		int32_t ss;
		int64_t th;
		int64_t *twids;
		Words tw;

		// make a city hash that would match Place::m_hash
		//int64_t cityHash = hashStringXor ( cityName );

		//int64_t tmpHash ;
		//tmpHash = hash64Lower_utf8 ( cityName , gbstrlen(cityName) ) ;
		//int32_t cityHash = (int32_t)(ch & 0xffffffff);
		//if ( strncmp(cityName,"Budlake",7)==0 ) 
		//	log("hey");
		if ( ! legit ) {
			char *use = NULL;
			if ( ! use ) use = a2name;
			if ( ! use ) use = a1name;
			if ( ! use ) use = cityName;
			if ( ! use ) { char *xx=NULL;*xx=0; }
			// hash each alnum word in there
		redo:
			if ( ! use ) { char *xx=NULL;*xx=0; }
			// hash the name
			int64_t uh = hashStringXor ( use );
			// see if we got it
			City *c = (City *) g_cities.getValue ( &uh );
			// set adm1 i guess
			if ( c ) {
				legit = true;
				adm1Bits = c->m_adm1Bits;
			}
			// a nested loop
			for ( ; ss >= 0 ; ss = g_cities.getNextSlot(ss,&th)) {
				// get the place
				tpd=(PlaceDesc *)g_cities.getValueFromSlot(ss);
				// must be our ctry
				if ( tpd->m_crid != crid ) continue;
				// got it
				a1[0] = tpd->m_adm1[0];
				a1[1] = tpd->m_adm1[1];
				legit = true;
				break;
			}
			// if still not found, try the other
			if ( ! legit && use && use == a2name && a1name ) {
				use = a1name;
				goto redo;
			}
			if ( ! legit && use && use == a1name && cityName ) {
				use = cityName;
				goto redo;
			}
		}
		*/

		static int32_t s_printed = 0;
		// sanity  check
		if ( ! legit ) {
			if ( ++s_printed < 100 )
			log("places: bad adm1 for "
			    "zip=\"%s\" cityName=\"%s\" "
			    "adm1Name=\"%s\" adm2Name=\"%s\"",
			    zip, cityName,a1name,a2name);
			badEntry++;
			continue;
		}

		// the two-letter adm1 in postalCodes.txt sometimes differs
		// from those in allCountries.txt. like, for example, 
		// British Columbia has adm1 code of "02" in allCountries.txt
		// but it is "bc" in postalCodes.txt.
		// so let's hash the full adm1 name in postalCodes.txt in order
		// to get the proper adm1 from allCountries.txt.

		if ( ! a1name ) continue;
		// hash the proper name of the adm1
		int64_t HH = getWordXorHash ( a1name );
		// skip if empty
		if ( HH == 0 ) continue;
		// now get state
		int32_t pos = getStateOffset ( &HH );
		// skip if could not match it to an adm1 in allCountries.txt
		// by the full name of the adm1
		if ( pos < 0 ) { char *xx=NULL;*xx=0; }//continue;

		// set it
		ZipDesc zd;
		//zd.m_crid   = crid;
		// set the state's bit. each state has its own unique bit
		zd.m_adm1Bits = 1LL << pos;
		zd.m_adm1[0] = a1[0];
		zd.m_adm1[1] = a1[1];
		zd.m_cityHash = getWordXorHash ( cityName );
		// centroid lat/lon now
		zd.m_latitude = 999.0;
		zd.m_longitude = 999.0;

		// sanity check
		if ( ! zd.m_cityHash ) { char *xx=NULL;*xx=0; }

		// offset to current position
		int32_t cityOffset = sb.length();
		// store it
		int32_t cityNameLen = gbstrlen(cityName);
		sb.safeMemcpy ( cityName , cityNameLen );
		sb.safeMemcpy ( "\0", 1 ); // null terminate
		// update zd
		zd.m_cityOffset = cityOffset;

		int64_t zh = getWordXorHash ( zip );
		// skip if bad
		if ( ! zh ) { badEntry++; continue; }

		// sanity check
		//if ( g_zips.isInTable ( &zh ) ) { 
		//	// both willowbrook,Il and hinsdale,IL have the
		//	// same zip code!
		//	//char *xx=NULL;*xx=0; }
		//	continue;
		//}
		// debug point
		//if ( zh == 70799779105646092LL ) 
		//	log("hey");

		if ( ! g_zips.addKey ( &zh , &zd ) ) return false;

	}
	// close that file
	fclose(fd);

	//
	// now open zipcode.csv and add the lat/lon of each zip code
	// from http://www.boutell.com/zipcodes/zipcode.zip
	//
	sprintf ( ff , "%szipcode.csv", g_hostdb.m_dir );
	logf(LOG_INFO,"places: reading %s",ff);
	fd = fopen ( ff, "r" );
	if ( ! fd )
		return log("places: failed to open %s: %s",ff,strerror(errno));
	line = 0;
	// go through the zipcodes in zipcode.csv, one per line
	while ( fgets ( buf , 10000 , fd ) ) {
		// length of line, including the terminating \n
		int32_t wlen = gbstrlen(buf) ;
		// sanity check
		if ( wlen >= 9000 ) { char *xx=NULL;*xx=0; }
		// skip if empty
		if ( wlen <= 0 ) continue;
		// null terminate it, instead of \n
		buf[wlen-1]='\0';
		// log it
		if ( (line % 10000) == 0 )
			log(LOG_INFO,"places: read line #%"INT32"",line);
		line++;
		// for debug
		char *p = buf;
		// lat is after 7th quote, lon is after 9th quote
		int32_t qcount = 0;
		float latitude = 999.0;
		float longitude = 999.0;
		char *zip = NULL;
		for ( ; *p ; p++ ) {
			if ( *p == '\"' ) qcount++;
			else              continue;
			if ( qcount == 1 ) zip = p+1;
			if ( qcount == 7 ) latitude  = atof (p+1);
			if ( qcount == 9 ) longitude = atof (p+1);
		}
		if ( ! zip ) continue;
		// must be numeric (disregard line 1 that has "zip")
		if ( ! is_digit(zip[0]) ) continue;
		// null term
		if ( zip[6] != '\"' ) zip[6] = '\0';
		else { char *xx=NULL;*xx=0; }
		// look it up
		int64_t zh = getWordXorHash ( zip );
		// skip if bad
		ZipDesc *zd = (ZipDesc *)g_zips.getValue ( &zh );
		// must be there
		if ( ! zd ) { 
			logf(LOG_INFO,"places: could not find zip %s",zip);
			continue;
		}
		// set it
		zd->m_latitude = latitude;
		zd->m_longitude = longitude;
	}
	fclose(fd);

	// 
	// scan all zips and make sure all have lat/lon
	//
	int32_t missed = 0;
	for ( int32_t i = 0 ; i < g_zips.m_numSlotsUsed ; i++ ) {
		// skip i fempty bucket
		if ( ! g_zips.m_flags[i] ) continue;
		// get it
		ZipDesc *zd = (ZipDesc *)g_zips.getValueFromSlot(i);
		// check it
		if ( zd->m_latitude  == 999.0 ||
		     zd->m_longitude == 999.0    ) 
			missed++;
	}
	logf(LOG_INFO,"places: missed lat/lon for %"INT32" zipcodes",missed);


	logf(LOG_INFO,"places: postalCodes.txt had %"INT32" bad entries.",
	     badEntry);

	/*
	// convert the indicator count table into g_indicators for IND_NAME
	// and add them into g_indicators now
	for ( int32_t i = 0 ; i < ct.m_numSlots ; i++ ) {
		// skip if empty
		if ( ct.m_flags[i] == 0 ) continue;
		// this is a count table
		int32_t count = *(int32_t *)ct.getValueFromSlot ( i );
		// skip if not popular
		if ( count < MIN_POP_COUNT ) continue;
		// skip for now
		continue;
		// make into score
		//float boost = 1.0 + (9.0 * (float)count / (float)MAX);
		//float boost = 1.00;
		// increment for every count
		//for ( int32_t j = 10 ; j < count ; j++ )
		//	boost *= 1.002;
		// limit it to 1.5 for now...
		//if ( boost > 1.5 ) boost = 1.5;
		// get wid
		//int64_t *wid = (int64_t *)ct.getKey ( i );
		// . add it
		// . use a boost of just 0.25 for now
		//if(! addIndicator ( *wid , IND_NAME , 0.25 ) ) // boost ) ) 
		//	return log("places: failed to make indicators.");
		// debug
		//char *str = *(char **)st.getValue ( wid );
		// show it
		//logf (LOG_DEBUG,"events: top place %s boost=%.02f",
		//     str,boost);
	}
	*/

	//////////////////////////////////////////////////////////////
	//
	// add the aliases
	//
	//////////////////////////////////////////////////////////////

	logf(LOG_INFO,"places: making aliases.dat");

	// . abbreviations for popular cities
	// . now we use the s_cityList array
	int32_t ncl = (int32_t)sizeof(s_cityList)/ sizeof(AliasDesc);
	for ( int32_t i = 0 ; i < ncl ; i++ ) {
		char      *s1   = s_cityList[i].m_s1;
		char      *s2   = s_cityList[i].m_s2;
		// use this now
		uint64_t h1 = getWordXorHash(s1);
		uint64_t h2 = getWordXorHash(s2);
		// skip if the same
		if ( h1 == h2 ) continue;
		// sanity check
		if ( h1 == 0 ) { char *xx=NULL;*xx=0; }
		if ( h2 == 0 ) { char *xx=NULL;*xx=0; }
		// get it
		CityDesc *cdp2 = (CityDesc *)g_cities.getValue ( &h2 );
		// must be there
		if ( ! cdp2 ) { char *xx=NULL;*xx=0; }

		// . add it as an alias for h2
		// . will add to g_aliases table which maps our 
		//   cityHash and adm1Str to the normalized cityHash
		// . also adds to g_cities which maps a normalized city
		//   hash to a bit vector of states that contain a city
		//   by that name
		addAlias ( s1 , s_cityList[i].m_adm1,h2,
			   s_cityList[i].m_pop,&maxPops);

		// you know addAlias() now adds this junk to g_cities...!
		/*
		// get our special cdp
		CityDesc *cdp1 = (CityDesc *)g_cities.getValue ( &h1 );
		// if not there, add one
		if ( ! cdp1 ) {
			// make CityDesc to add
			CityDesc cd;
			// . we choose most pop state for this alias
			// . so "SF" has two entries in s_cityList and the
			//   "mostPopState" is "ca" for both
			char *ss = s_cityList[i].m_mostPopStateAbbr;
			// get this
			StateDesc *tsd = getStateDesc(ss);
			// convert to index
			int32_t si = tsd - &s_states[0];
			// sanity
			if ( si < 0 ) { char *xx=NULL;*xx=0; }
			// store it
			cd.m_mostPopularState = si;
			// and the bits indicating states we are in
			cd.m_adm1Bits         = cdp2->m_adm1Bits;
			if ( ! g_cities.addKey(&h1,&cd) ){ char*xx=NULL;*xx=0;}
			// flag it as an alias so getCityId32() knows to
			// look it up special...
			//cd.m_adm1Bits |= 0x8000000000000000LL;
			continue;
		}
		// then update bits
		cdp1->m_adm1Bits |= cdp2->m_adm1Bits;
		*/
	}
	     


	// save it
	logf(LOG_INFO,"places: saving timezones.dat");

	if ( ! g_timeZones.save ( g_hostdb.m_dir , "timezones.dat" ) )
		return log("places: failed to save timezones.dat");

	// save it
	logf(LOG_INFO,"places: saving cities.dat");

	if ( ! g_cities.save ( g_hostdb.m_dir , "cities.dat" ) )
		return log("places: failed to save cities.dat");

	logf(LOG_INFO,"places: saving aliases.dat");

	if ( ! g_aliases.save ( g_hostdb.m_dir , "aliases.dat" ) )
		return log("places: failed to save aliases.dat");

	logf(LOG_INFO,"places: saving zips.dat");

	char *tbuf     = sb.getBufStart();
	int32_t  tbufSize = sb.length();
	if ( ! g_zips.save ( g_hostdb.m_dir , "zips.dat",tbuf,tbufSize ) )
		return log("places: failed to save zips.dat");

	// let this memlose
	g_cityBuf     = tbuf;
	g_cityBufSize = tbufSize;
	// do not let "sb" free it
	//sb.m_buf      = NULL;
	sb.detachBuf();

	//if ( ! g_indicators.save ( g_hostdb.m_dir, "indicators.dat" ) )
	//	return log("places: failed to save indicators.dat");


	//////////////////////////////////////////////////////////////
	//
	// LOAD THE planet-090421.osm file to get street names
	//
	//////////////////////////////////////////////////////////////

	/*
	// init indicator table
	g_streets.set ( 7                 ,  // keySize
			0                 ,
			0                 ,  // initial # slots 
			NULL              ,  // initial buf
			0                 ,  // initial buf size
			false             ,  // allowDup keys?
			0                 ); // niceness
	
	// load inidcator table
	if ( g_streets.load ( g_hostdb.m_dir , "streetnames.dat" ) ) 
		return true;

	// . open the unholy planet-090421.osm file to create streetnames.dat
	// . see http://wiki.openstreetmap.org/wiki/Data_Primitives to
	//   explain a bit about this xml file
	// . http://wiki.openstreetmap.org/wiki/Map_Features
	// . http://wiki.openstreetmap.org/wiki/Develop
	// . http://code.google.com/apis/maps/documentation/examples/
	sprintf ( ff , "%splanet-090421.osm", g_hostdb.m_dir );
	logf(LOG_INFO,"places: reading %s",ff);
	FILE *fd = fopen ( ff, "r" );
	if ( ! fd )
		return log("places: failed to open %s: %s",ff,strerror(errno));
	*/


	return true;
}

// . "boost" is how much to boost the Place's score by if it has this indicator
bool addIndicator ( char *s , char bit , float indScore ) {
	// hash it
	int64_t h = hash64Lower_utf8 ( s , gbstrlen(s) );
	return addIndicator ( h , bit , indScore );
}

bool addIndicator ( int64_t h , char bit , float indScore ) {
	// plaza is two types of indicator, street and name
	IndDesc *pid = (IndDesc *)g_indicators.getValue (&h);
	// if there, augment the bits
	if ( pid ) {
		pid->m_bit |= bit;
		return true;
	}
	// add in some indicators of our own
	IndDesc id;
	// set bit, should only be one
	id.m_bit = bit;
	id.m_indScore = indScore;
	// add it. should gbmemcpy "pd"
	return g_indicators.addKey ( &h , &id ) ;
}

// "baseScore" should be event id
bool Address::hash ( int32_t        baseScore ,
		     HashTableX *dt        ,
		     uint32_t    date      ,
		     Words      *words     , 
		     Phrases    *phrases   , 
		     SafeBuf    *pbuf      ,
		     HashTableX *wts       ,
		     SafeBuf    *wbuf      ,
		     int32_t        version   ,
		     int32_t        niceness  ) {
	return true;
}


// . returns false and sets g_errno on error
bool Addresses::hashForPlacedb ( int64_t   docId    ,
				 int32_t        siteHash32 ,
				 int32_t        ip       ,
				 HashTableX *dt       ) {
	
	// sanity check
	if ( dt->m_ds != 512 ) { char *xx=NULL;*xx=0; }
	if ( dt->m_ks != 16  ) { char *xx=NULL;*xx=0; }

	// ensure we allow dups because some streets are repeated on
	// the page, but with different place names. see
	// http://www.zvents.com/albuquerque-nm/venues/show/11865-kimo-theatre
	//if ( ! dt->m_allowDups ) { char *xx=NULL;*xx=0; }

	// now create the meta rdb list
	for ( int32_t i = 0 ; i < m_am.getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( dt->m_niceness );
		// get it
		Address *a = (Address *)m_am.getPtr(i);
		// skip if lat/lon
		if ( a->m_flags3 & AF2_LATLON ) continue;
		// is it good?
		bool good = false;
		// being inlined is awesome
		if ( a->m_flags & AF_INLINED ) good = true;
		// if the street is verified, add the whole thing too!
		// even if the street num and place name are not verified.
		if ( a->m_flags & AF_VERIFIED_STREET ) good = true;
		// sometimes a street can exist in two cities or states
		if ( a->m_flags & AF_AMBIGUOUS ) good = false;
		// do not add addresses that have no street per se
		if ( a->m_street->m_flags2 & PLF2_IS_NAME ) good = false;
		// no intersections
		if ( a->m_street->m_flags2 & PLF2_INTERSECTION ) good = false;
		// . skip if not good
		// . we no longer add non-inlined addresses cuz those are
		//   not as accurate. many pages have the street address
		//   too far from the city and state, and we use one from the
		//   tag and it ain't right.
		// . THE TAVERN ~
		//   4007 Menaul NE ~
		//   Between Washington and Carlisle ~
		//   87110 ~
		//   with the tag: 
		//   New Mexico Music Commission;;PO Box 1450;Santa Fe(nm);...
		//   caused it to get "Santa Fe" as the city
		if ( ! good ) continue;
		// not if amibiguous
		//if ( a->m_flags & AF_AMBIGUOUS ) good = false;
		// . skip if no zip
		// . hmmm, a lot seem to be missing zip, so forget about it
		//if  ( ! a->m_zip ) continue;
		// seraialize into "buf"
		char buf[513];
		// reset it to all 0s
		memset ( buf , 0 , 513 );
		// convert to semicolon format 
		int32_t size = a->serialize ( buf , 511 , NULL , false , false);
		// skip on error, probably > 511 bytes!
		if ( size < 0 ) continue;
		// make the key for this address
		key128_t k = a->makePlacedbKey ( m_docId , false,false );
		// store it for getNamedbData() to use
		if ( a->m_placedbKey != k ) { char *xx=NULL; *xx=0; }

		// if key already added, skip. assume the first one is better.
		// www.zvents.com/albuquerque-nm/venues/show/11865-kimo-theatre
		// has two different place names for Kimo Theater street addr

		// will add the entire 512 bytes of buffer to this hash table
		// so it is really up to XmlDoc::addTable128() to fix that
		// when it creates the corresponding meta list. it will need
		// to shrink that list
		if ( ! dt->isInTable (&k) &&
		     ! dt->addKey ( (char *)&k , buf ) ) return false;

		// now if the name is verified, then use the hash of the
		// name in place of the street hash
		if ( a->m_flags & AF_VERIFIED_PLACE_NAME_1 ) {
			// use that
			key128_t k2 = a->makePlacedbKey ( m_docId,true,false);
			// add again
			if ( ! dt->addKey ( (char *)&k2 , buf ) ) return false;
		}
		// same with place name 2
		if ( a->m_flags & AF_VERIFIED_PLACE_NAME_2 ) {
			// use that
			key128_t k2 = a->makePlacedbKey ( m_docId,false,true);
			// add again
			if ( ! dt->addKey ( (char *)&k2 , buf ) ) return false;
		}

		// . skip if not a venue location for this venue website
		if ( ! ( a->m_flags & AF_VENUE_DEFAULT ) ) continue;
		// . do not do this now... key formation is setting del bit!
		continue;
		// . we do not really use this right now...
		// . add the address of the website itself!! a venue website!!
		// . use siteHash32 as the top key
		// . make the key
		key128_t k3;
		k3.n0 = 0LL;
		k3.n1 = 0LL;
		k3.n1 = siteHash32;
		k3.n1 <<= 32;
		k3.n0 = (docId<<1);
		// add it
		if ( ! dt->addKey((char *)&k3,buf)) return false;
	}
	return true;
}


#include "Placedb.h"

// . H = 48 bit hash of (streetname,ctryId,adm1,city)
//   N = 16 bit hash of streetnum
// . placedb key format:
//   H (48 bits) | N (16 bits) |docId(38bits) | delbit(1)
// . data = serialized address ( see setFromStr() function)
// . "streetname" should exclude any indicators
// . we determine the group responsible for this key by the 64 bit hash (H)
//   alone... see Hostdb::getGroupId()
key128_t Address::makePlacedbKey (int64_t docId,bool useName1,bool useName2){

	// the key we are setting
	key128_t k;
	// sanity check, must be 8 bits or less
	//if ( m_adm1->m_crid > 255 ) { char *xx=NULL;*xx=0; }

	// sanity
	if ( m_cityId32 == 0 ) { char *xx=NULL;*xx=0; }

	// save for sanity check. mask it to 25 bits
	int32_t snh = m_street->m_streetNumHash & 0x01ffffff;

	// add in street name (not including indicators)
	int64_t h = m_street->m_hash;
	// . use place name 1 instead of street name?
	// . we use this for when "Tingley Colesium" is given and no street!
	if ( useName1 || useName2 ) {
		// use the name hash in place of the street hash!!! HACK
		if ( useName1 ) h = m_name1->m_hash;
		if ( useName2 ) h = m_name2->m_hash;
		// anytime we use a name as the street hash we have to
		// xor in this to prevent a place name from matching
		// a street name (see above)
		h ^= 0x123456;
		// and incorporate the street hash into the snh so that
		// sendBackAddress() function's life is easier
		snh ^= m_street->m_hash;
		// mask it
		snh &= 0x01ffffff;
	}

	// country id
	//h = hash64 ( (int64_t)m_adm1.m_crid , h );
	// adm1
	// get the two-letter state abbreviation code (nm = new mexico)
	char *adm1Str = NULL;
	if      ( m_adm1 ) adm1Str = m_adm1->m_adm1;
	else if ( m_zip  ) adm1Str = m_zip->m_adm1;
	// unique cities like "Albuquerque" imply a state
	//else if ( m_city && m_city->m_adm1[0] ) adm1Str = m_city->m_adm1;
	else               { char *xx=NULL;*xx=0; }
	h = hash64 ( (int64_t)(*(uint16_t *)adm1Str) , h );
	// city
	int64_t cityHash = 0LL;
	if      ( m_city ) cityHash = m_city->m_hash;
	else if ( m_zip  ) cityHash = m_zip->m_cityHash;
	else              { char *xx=NULL;*xx=0; }
	// use the *city id* to deal with aliases of the same city
	uint64_t cid64 = (uint64_t)getCityId32 ( cityHash , adm1Str );
	// incorporate that into "h"
	h = hash64 ( cid64 , h );
	// store that in most signficant int64_t
	k.n1 = h;

	// street hash
	int64_t n0 = snh;
	// shift up for docid
	n0 <<= 38;
	// sanity
	if ( (int32_t)NUMDOCIDBITS != 38 ) { char *xx=NULL;*xx=0; }
	// put that in
	n0 |= docId;
	// empty bit for del bit
	n0 <<= 1;
	// set the del bit to indicate a positive key
	n0 |= 0x01;
	// set
	k.n0 = n0;

	// sanity checks
	if ( g_placedb.getBigHash      (&k) != h     ) { char *xx=NULL;*xx=0; }
	if ( g_placedb.getStreetNumHash(&k) != snh   ) { char *xx=NULL;*xx=0; }
	if ( g_placedb.getDocId        (&k) != docId ) { char *xx=NULL;*xx=0; }
	// return
	return k;
}

/*
// similar to Address::serialize()
int64_t Address::makeAddressVotingTableKey ( ) {

	int64_t h = 0LL;
	Place *d = NULL;

	// incorporate place name into the hash
	d = &m_name1;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );

	// and secondary name
	d = &m_name2;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );

	// incorporate suite into the hash
	d = &m_suite;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );

	// incorporate street into the hash
	d = &m_street;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );

	// incorporate city into the hash
	d = &m_city;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );
	// adm1 of the city
	if ( d->m_str ) h = hash64 ( d->m_adm1 , 2 , h );

	// incorporate zip into the hash
	d = &m_zip;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );
	// adm1 as well
	if ( d->m_str ) h = hash64 ( d->m_adm1 , 2 , h );

	// incorporate adm1 into the hash
	d = &m_adm1;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );
	// adm1 as well
	if ( d->m_str ) h = hash64 ( d->m_adm1 , 2 , h );

	// incorporate adm2 into the hash
	//d = &m_adm2;
	//if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );
	// adm1 as well
	//if ( d->m_str ) h = hash64 ( d->m_adm1 , 2 , h );

	// incorporate ctry into the hash
	d = &m_ctry;
	if ( d->m_str ) h = hash64 ( d->m_str , d->m_strlen , h );
	
	return h;
}
*/

///////////////////////////////////////////////
//
// Msg2c : for verifying all the places/addresses
//
///////////////////////////////////////////////

Msg2c::Msg2c() {
	m_replies = 0;
	m_requests = 0;
	//m_mcast.constructor();
	m_initializedInUse = false;
}

#include "Process.h"

Msg2c::~Msg2c () {
	// no destroying if still awaiting replies
	if ( m_replies != m_requests && ! g_process.m_exiting ) { 
		char *xx=NULL;*xx=0; }
	reset();
}

void Msg2c::reset() {
	m_replies = 0;
	// all done if never initialized the multicasts
	if ( ! m_initializedInUse ) return;
	// int16_tcut
	int32_t max = (int32_t)MAX_ADDR_REQUESTS_OUT;
	// call DEstructors on multicasts
	for ( int32_t i = 0 ; i < max ; i++ ) {
		QUICKPOLL(m_niceness);
		m_mcasts[i].destructor();
	}
}


// . sets Address::m_verified to 1 if verified
// . returns false if blocked
// . returns true and sets g_errno on error
// . and also sets the "avt" address verification table which we serialize
//   into the TitleRec for re-parsing purposes later on, so we consistently
//   re-parse
bool Msg2c::verifyAddresses ( Addresses  *aa         ,
			      //char       *coll       ,
			      collnum_t collnum ,
			      int32_t        domHash32  ,
			      int32_t        ip         ,
			      int32_t        niceness   ,
			      void       *state      ,
			      void     (* callback)(void *state ) ) {
	
	m_niceness   = niceness;
	m_addresses  = aa;
	m_collnum = collnum;
	m_domHash32  = domHash32;
	m_ip         = ip;
	m_callback   = callback;
	m_state      = state;
	// reset
	m_errno    = 0;
	m_requests = 0;
	m_replies  = 0;
	m_doneLaunching = false;
	
	// reset address ptr
	m_i = 0;

	// all done if no addresses!
	if ( m_addresses->m_am.getNumPtrs() == 0 ) return true;

	// sanity check
	if ( aa->m_sb.length() != 0 ) { char *xx=NULL; *xx=0; }

	// . launch the requests
	// . returns false if we are waiting for replies to come in
	if ( ! launchRequests() ) return false;
	// fill the the m_sb buf with all replies
	//allDone();
	// did not block and all replies are in
	return true;
}

// keep tabs on total out
static int32_t s_totalOut = 0;

// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool Msg2c::launchRequests ( ) {
	// clear it
	g_errno = 0;
	// how many max can be out?
	int32_t maxOut = (int32_t)MAX_ADDR_REQUESTS_OUT;
	// but be careful
	if ( s_totalOut >= 200 ) maxOut = 1;
	// we are only built for one at a time since request buffer is static
	//if ( (int32_t)MAX_ADDR_REQUESTS != 1 ) { char *xx=NULL;*xx=0; }
 loop:
	// all done?
	if ( m_i == m_addresses->m_am.getNumPtrs() ) 
		m_doneLaunching = true;
	// return true if nothing to launch
	if ( m_doneLaunching ) 
		return (m_requests == m_replies);
	// don't bother if already got an error
	if ( m_errno ) 
		return (m_requests == m_replies);
	// limit max to 5ish
	if (m_requests-m_replies >= maxOut ) // MAX_ADDR_REQUESTS_OUT) 
		return (m_requests==m_replies);
	// . limit total requests for better performance
	// . www.vinarium-usa.com does like 500,000 lookups. it would take
	//   like 30 seconds on a single test server. limiting to 50,000
	//   lookups it still takes 10 seconds on titan.
	// . this limit doesn't affect any other pages in urls.txt - 11/18/11
	if ( m_requests > 50000 ) {
		if ( m_requests == m_replies )
			log("addr: limiting msg2c requests to 50000 for %s",
			    m_addresses->m_url->m_url);
		return (m_requests==m_replies);
	}
	// take a breath
	QUICKPOLL(m_niceness);

	Address *a = (Address *)m_addresses->m_am.getPtr(m_i);
	// skip it 
	m_i++;
	// assume not verified
	a->m_replyFlags = 0;

	// . skip if it is like "call for location"
	// . no no no this is messing up "at the filling station" for
	//   http://www.zvents.com/albuquerque-nm/events/show/
	//   88688960-sea-the-invalid-mariner
	//if ( a->m_street->m_flags2 & PLF2_AFTER_AT ) {
	//	// might be done
	//	if ( m_i == m_addresses->m_na ) m_doneLaunching = true;
	//	// try the next one
	//	goto loop;
	//}

	// max size of request
	//int32_t max = 1024;
	// request is startKey,endKey,pihash,niceness,coll
	//char *requestBuf = a->m_requestBuf;

	// prepare to get a request buf if we haven't already
	if ( ! m_initializedInUse ) {
		int32_t max = (int32_t)MAX_ADDR_REQUESTS_OUT;
		memset(m_inUse,0,max);
		// call constructors on multicasts
		for ( int32_t i = 0 ; i < max ; i++ ) {
			QUICKPOLL(m_niceness);
			m_mcasts[i].constructor();
		}
		// do not repeat
		m_initializedInUse = true;
	}
	// get a request buf, assume none (-1)
	int32_t reqBufNum = -1;
	// scan what we got
	for ( int32_t i = 0 ; i < MAX_ADDR_REQUESTS_OUT ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if in use
		if ( m_inUse[i] ) continue;
		// and let caller know which one
		reqBufNum = i;
		// and stop
		break;
	}
	// panic! how did this happen?
	if ( reqBufNum == -1 ) { char *xx=NULL;*xx=0; }
	// claim it
	m_inUse[reqBufNum] = 1;
	// point to the junk
	char *requestBuf = m_bigBuf[reqBufNum];
	// store requestbuf # we did get
	a->m_reqBufNum = reqBufNum;
	// and store addr # (subtract one since we increment m_i above)
	a->m_addrNum = m_i - 1;
	// point to this
	Multicast *m = &m_mcasts[reqBufNum];


	// store it
	char *p = requestBuf;
	// store placedbKey
	*(key128_t *)p = a->m_placedbKey; p += sizeof(key128_t);
	// site hash
	*(int32_t *)p = m_domHash32; p += 4;
	*(int32_t *)p = m_ip       ; p += 4;
	// niceness, 1 byte
	*(char *)p = m_niceness; p += 1;
	// is the street really a place name in disguise? ("Tingley Colesium")
	char isName = ( a->m_street->m_flags2 & PLF2_IS_NAME ) ;
	*(char *)p = isName  ; p += 1;
	// collection
	//int32_t collSize = gbstrlen(m_coll) + 1;
	//gbmemcpy ( p , m_coll , collSize );
	//p += collSize;
	*(collnum_t *)p = m_collnum;
	p += sizeof(collnum_t);
	// end of it
	char *pend = requestBuf + REQBUFSIZE; // s_requestBuf + max;
	// . then the address string, semicolon separated, null terminated
	// . like ";;5815 Wyoming Blvd NE;Albuquerque;nm;87109;;..."(see below)
	// . returns -1 and sets g_errno on error
	// . returns # of bytes written, including null terminator
	int32_t written = a->serialize ( p , pend - p , NULL , false , false );
	// error?
	if ( written == -1 ) { 
		m_errno = g_errno; 
		// unclaim
		m_inUse[reqBufNum] = 0;
		return (m_requests == m_replies);
	}
	// update our ptr
	p += written;
	// must be there
	if ( written == 0 ) { char *xx=NULL;*xx=0; }
	// ensure null terminated
	if ( p[-1] != '\0' ) { char *xx=NULL;*xx=0; }

	// size of it
	int32_t requestSize = p - requestBuf;
	// sanity check for breach
	if ( requestSize > REQBUFSIZE ) { char *xx=NULL;*xx=0; }

	// . get group to handle it
	// . each group is responsible for a specific streetname/ctry/city/adm1
	// . Hostdb.cpp::getGroupId()
	//uint32_t gid = getGroupId(RDB_PLACEDB,(char *)&a->m_placedbKey);
	uint32_t shardNum;
	shardNum = getShardNum (RDB_PLACEDB,(char *)&a->m_placedbKey);

	// . pick a host within that group based on docid
	// . base that on streetname hash i guess
	// . but i would like to cache this using a biased cache
	// . so we need to divide based on streetname hash
	// . that is the most significant 16 bits of the placedb key
	int32_t  numHosts = g_hostdb.getNumHostsPerShard();
	int32_t  hostNum  = a->m_street->m_hash % numHosts;
	Host *group    = g_hostdb.getShard ( shardNum );
	// get host # "hostNum" in group "group" to send our request to
	Host *h        = &group [ hostNum ];

	//int32_t addrNum = m_i - 1;

	// launch it
	//Multicast *m = &m_mcast;
	// this returns false and sets g_errno on error
	if ( ! m->send ( requestBuf  ,
			 requestSize ,
			 0x2c        , // msgType
			 false       , // multicast own request?
			 shardNum, // gid         ,
			 false       , // send to whole group?
			 0           , // key for selecting host (not used)
			 this        , // state
			 (void *)a   , // state2
			 gotMsg2cReplyWrapper ,
			 180            , // total timeout
			 m_niceness     ,
			 false          , // realtime udp
			 h->m_hostId    ,
			 NULL,//&a->m_replyFlags , // replyBuf
			 0,//1              , // replyBufMaxSize 
			 false          )) { // freeReplyBuf?
		// note it
		m_errno = g_errno;
		// return false if we are waiting on replies
		return (m_requests == m_replies);
	}

	// keep tabls
	s_totalOut++;
	// successfully launched
	m_requests++;
	// launch another
	goto loop;
}
	
void gotMsg2cReplyWrapper ( void *state , void *state2 ) {
	Msg2c *THIS = (Msg2c*)state;
	// we got one
	THIS->m_replies++;
	// back
	s_totalOut--;

	// error?
	if ( g_errno ) {
		THIS->m_errno = g_errno;
		log("addr: msg2c reply: %s",mstrerror(g_errno));
	}

	// cast this
	Addresses *aa = THIS->m_addresses;

	// point to the address we were working for
	Address *a = (Address *)state2;
	// what address # was it matching?
	int32_t addrNum = a->m_addrNum;
	// and the reply buffer num for making available again
	int32_t reqBufNum = a->m_reqBufNum;
	// sanity
	if ( reqBufNum<0 || reqBufNum>=MAX_ADDR_REQUESTS_OUT ) {
		char *xx=NULL; *xx=0; }
	// make it available again
	THIS->m_inUse[reqBufNum] = 0;

	// test it
	Multicast *m = &THIS->m_mcasts[reqBufNum];
	int32_t replySize , replyMaxSize; bool freeIt;
	char *r = m->getBestReply (&replySize,&replyMaxSize,&freeIt);

	// store reply into our cache
	if ( ! g_errno && ! aa->addToReplyBuf (r,replySize,addrNum)){
		// sanity check
		if ( ! g_errno ) { char *xx=NULL;*xx=0; }
		// set this
		THIS->m_errno = g_errno;
	}

	// free that memory to stop the mem leak
	mfree ( r , replyMaxSize , "umsg2c" );

	// test it
	//if ( r && replySize != 1 ) { char *xx=NULL; *xx=0; }
	// show it
	//log("addr: got reply=%"INT32" replyaddr=0x%"XINT32"",(int32_t)*r,(int32_t)r);
	// launchGetRequests() returns false if still waiting for replies...
	if ( ! THIS->launchRequests() ) return;
	// set g_errno for the callback
	if ( THIS->m_errno ) g_errno = THIS->m_errno;
	// fill the table
	//THIS->allDone ( );
	// otherwise, call callback
	THIS->m_callback ( THIS->m_state );
}

// we then call Addresses::updateAddresses() to modify our m_addresses[]
// array with these replies!
bool Addresses::addToReplyBuf ( char *reply , int32_t replySize , int32_t addrNum ) {
	// if nothing found in placedb lookup we get  a 0 byte reply
	if ( replySize == 0 ) return true;
	// sanity
	if ( addrNum < 0 || addrNum >= m_am.getNumPtrs()){char *xx=NULL;*xx=0;}
	// if no room, make it 1.5 times bigger
	if ( m_sb.m_length + replySize+4+4 > m_sb.m_capacity &&
	     ! m_sb.reserve ( (int32_t)(m_sb.m_capacity * 1.5 + 1000 ) ) ) {
		log("addr: addtoreplybuf: %s",mstrerror(g_errno));
		return false;
	}
	// store the address # this reply is for
	if ( ! m_sb.pushLong    ( addrNum           ) ) return false;
	// then reply stuff
	if ( ! m_sb.pushLong    ( replySize         ) ) return false;
	if ( ! m_sb.safeMemcpy  ( reply , replySize ) ) return false;
	return true;
}

class State2c {
public:
	UdpSlot *m_slot;
	Msg5 m_msg5;
	int32_t m_votesForStreet;
	int32_t m_votesForStreetNum;
	int32_t m_votesForPlaceName1;
	int32_t m_votesForPlaceName2;
	RdbList m_list;
	int32_t m_domHash32;
	int32_t m_ip;
	key128_t m_placedbKey;
	int32_t     m_niceness;
	// is the street really a place name in disguise? (Tingley Colesium)
	char     m_isName;
	// point to the serialize Address (semicolon separated, null term'd)
	char *m_addrStr;
};

void handleRequest2c ( UdpSlot *slot , int32_t nicenessWTF ) {
	// get the request
	char *request     = slot->m_readBuf;
	int32_t  requestSize = slot->m_readBufSize;
	// overflow protection for corrupt requests
	if ( requestSize < 4 ) {
		g_errno = EBUFTOOSMALL;
		g_udpServer.sendErrorReply ( slot , g_errno );
		return;
	}

	// parse the request
	char *p = request;

	// do the lookup on disk (hopefully in cache or ssd!)
	// make a new Msg5
	State2c *st;
	try { st = new (State2c); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("msg2c: new(%"INT32"): %s", (int32_t)sizeof(State2c), mstrerror(g_errno));
		return g_udpServer.sendErrorReply ( slot, g_errno );
	}
	mnew ( st , sizeof(State2c) , "hndl2c" );

	// save slot for sending reply
	st->m_slot = slot;

	// extract placedb key from request
	st->m_placedbKey = *(key128_t *)p; p += sizeof(key128_t);
	// get key range
	key128_t startKey = st->m_placedbKey;
	key128_t endKey   = st->m_placedbKey;
	// sanity check
	if ( startKey.n1 == 0LL ) { char *xx=NULL;*xx=0; }
	if ( endKey.n1   == 0LL ) { char *xx=NULL;*xx=0; }
	// now we also mask out the street num hash
	startKey.n1 &= 0xffffffffffff0000LL;
	// and or that in for the endKey
	endKey.n1   |= 0x000000000000ffffLL;
	// mask out all but n1
	startKey.n0  = 0x0000000000000000LL;
	// or in lower bits for the endKey
	endKey  .n0  = 0xffffffffffffffffLL;

	// domhash
	st->m_domHash32 = *(int32_t *)p; p += 4;
	st->m_ip        = *(int32_t *)p; p += 4;
	// get niceness
	//int32_t niceness = *(char *)p; p++;
	// skip still though!!
	p++;
	// this was messing up our niceness conversion algo
	int32_t niceness = slot->m_niceness;
	// is the street really a place name in disguise? (Tingley Colesium)
	st->m_isName    = *(char *)p; p++;
	// save it
	st->m_niceness = niceness;
	// get coll
	//char *coll = p; p += gbstrlen(p) + 1;
	collnum_t collnum = *(collnum_t *)p;
	p += sizeof(collnum_t);
	// the address string, semicolon separated, NULL terminated
	st->m_addrStr = p; p += gbstrlen(p) + 1;

	// . get from msg5, return if it blocked
	// . will probably not block since in the disk page cache a lot
	if ( ! st->m_msg5.getList ( RDB_PLACEDB ,
				    collnum        ,
				    &st->m_list ,
				    (char *)&startKey    ,
				    (char *)&endKey      ,
				    100000      , // minRecSizes
				    true        , // include tree?
				    false       , // addtocache?
				    0           , // maxcacheage
				    0           , // startfilenum
				    -1          , // numFiles
				    st          ,
				    gotList2c   ,
				    niceness    ,
				    true        ))// do err correction?
		return;
	// it did not block...
	gotList2c( st , NULL , NULL );
}

void gotList2c ( void *state , RdbList *xxx , Msg5 *yyy ) {
	// cast our state class
	State2c *st = (State2c *)state;
	// get this
	UdpSlot *slot = st->m_slot;
	// return right away if error getting the rec
	if ( g_errno ) { 
		// loop back up here on error below as well
	hadError:
		// all done with this
		mdelete ( st , sizeof(State2c),"msg2cfr");
		delete (st);
		g_udpServer.sendErrorReply ( slot,g_errno );
		return;
	}
	// assume not good
	st->m_votesForStreet     = 0;
	st->m_votesForStreetNum  = 0;
	st->m_votesForPlaceName1 = 0;
	st->m_votesForPlaceName2 = 0;

	// if request was looking up a *place name* and not a street
	// then we do some different logic
	if ( st->m_isName ) { 
		// caller needs a street address for the place
		sendBackAddress ( st ); 
		return; 
	}

	// get our street num hash
	key128_t *pk = &st->m_placedbKey;
	int64_t myBigHash       = g_placedb.getBigHash(pk);
	int32_t      myStreetNumHash = g_placedb.getStreetNumHash(pk);

	// point to the place name
	char *pn1 = st->m_addrStr;
	// get the first semicolon
	char *semi1 = pn1;
	// scan for it
	for ( ; *semi1 && *semi1 !=';' ; semi1++ );
	// NULL term 
	*semi1 = '\0';
	// skip leading "the"
	if ( ! strncasecmp ( pn1, "the ", 4) ) pn1 += 4;
	// get niceness
	int32_t niceness = st->m_niceness;
	// make a vector of "int32_ts" from the place name
	int32_t myvbuf1[50];
	int32_t mynv1 = makeSimpleWordVector ( pn1 , myvbuf1 , 50*4,niceness ) ;
	if ( mynv1 == -1 ) goto hadError;

	// do the same for the second name
	char *pn2 = semi1 + 1;
	// skip for it
	char *semi2 = pn2;
	// scan for it
	for ( ; *semi2 && *semi2 !=';' ; semi2++ );
	// NULL term 
	*semi2 = '\0';
	// skip leading "the"
	if ( ! strncasecmp ( pn2, "the ", 4) ) pn2 += 4;
	// make vector of secondary place name
	int32_t myvbuf2[50];
	int32_t mynv2 = makeSimpleWordVector ( pn2 , myvbuf2 , 50*4,niceness ) ;
	if ( mynv2 == -1 ) goto hadError;

	//log("build: matching %s",pn1);

	// each placedb record's place name in the list is hashed and
	// stored in this table so we can accumulate votes. "voting table"
	HashTableX vt;
	char vtableBuf[5000];
	vt.set(4,4,128,vtableBuf,5000,false,niceness,"addrvt");

	// and likewise each hash has a ptr to the original string
	// of the place name
	HashTableX ptrTable;
	char ptrBuf[5000];
	ptrTable.set(4,4,128,ptrBuf,5000,false,niceness,"addptr");

	// how much reply buf to allocate? need at least one byte for
	// the original one byte reply of flags...
	// now we also store the best lat and lon which are the two doubles,
	// and the 4 bytes before for the # of votes for that lat/lon
	int32_t need = 1 + 4 + sizeof(double)*2;

	// int16_tcut
	RdbList *list = &st->m_list;

	while ( ! list->isExhausted() ) {
		// breathe
		QUICKPOLL ( st->m_niceness );
		// get it
		char *data = list->getCurrentData();
		// get the key
		key128_t k; list->getCurrentKey(&k);
		// skip it
		list->skipCurrentRecord();
		// cast it
		Address a2; 
		//Place places2[10];
		//int32_t np2 = 0;
		PlaceMem pm;
		char tmpbuf[7024];
		pm.init ( 5000 ,10,10,tmpbuf,7024,0 );
		// set "a"
		setFromStr ( &a2, data, 0 , &pm ,st->m_niceness );
		// must not be same site as us for better voting accuracy
		if ( a2.m_domHash32 == st->m_domHash32 ) continue;
		// and different ip from us, for better voting accuracy
		if ( iptop(a2.m_ip) == iptop(st->m_ip) ) continue;
		// valid ip sanity check
		if ( a2.m_ip == 0 || a2.m_ip==-1 ) { char *xx=NULL; *xx=0; }

		// sanity check
		if (g_placedb.getBigHash(&k)!=myBigHash) {char*xx=NULL;*xx=0;}

		// ok, now we have verfied the street for sure
		st->m_votesForStreet++;

		// get the street num hash of that record
		int32_t snh = g_placedb.getStreetNumHash ( &k );
		// . does it match our street number?
		// . i.e. the "15110" in "15110 Wyoming blvd"
		if ( snh != myStreetNumHash ) continue;
		// yes, another match
		st->m_votesForStreetNum++;

		//
		// build a vector for each of the two place names
		//

		// get place name
		pn1 = data;
		// get semi
		semi1 = pn1;
		// scan for it
		for ( ; *semi1 && *semi1 !=';' ; semi1++ );
		// NULL term 
		*semi1 = '\0';
		// skip leading "the"
		if ( ! strncasecmp ( pn1, "the ", 4) ) pn1 += 4;
		// make its place name into a vector
		int32_t vbuf1[50];
		int32_t nvbuf1 ;
		nvbuf1 = makeSimpleWordVector(pn1,vbuf1,50*4,st->m_niceness);
		if ( nvbuf1 == -1 )
			goto hadError;
		// do the same for the second name
		pn2 = semi1 + 1;
		// skip for it
		semi2 = pn2;
		// scan for it
		for ( ; *semi2 && *semi2 !=';' ; semi2++ );
		// NULL term 
		*semi2 = '\0';
		// skip leading "the"
		if ( ! strncasecmp ( pn2, "the ", 4) ) pn2 += 4;
		// make vector of secondary place name
		int32_t vbuf2[50];
		int32_t nvbuf2;
		nvbuf2 = makeSimpleWordVector (pn2,vbuf2,50*4,st->m_niceness);
		if ( nvbuf2 == -1) 
			goto hadError;
		// undo
		*semi1 = ';';
		*semi2 = ';';

		//log("build: matching %s vs %s",pn1,pn2);

		// ok, compare the two vectors
		float sim1 = computeSimilarity ( myvbuf1 ,
						 vbuf1   ,
						 NULL    ,
						 NULL    ,
						 NULL    ,
						 st->m_niceness );

		float sim2 = computeSimilarity ( myvbuf2 ,
						 vbuf2   ,
						 NULL    ,
						 NULL    ,
						 NULL    ,
						 st->m_niceness );

		// compare the secondary to primary, and vice versa
		float sim3 = computeSimilarity ( myvbuf1 ,
						 vbuf2   ,
						 NULL    ,
						 NULL    ,
						 NULL    ,
						 st->m_niceness );

		float sim4 = computeSimilarity ( myvbuf2 ,
						 vbuf1   ,
						 NULL    ,
						 NULL    ,
						 NULL    ,
						 st->m_niceness );

		//
		// now we also hash each word in each place name and
		// store those two hashes into a table so we can score
		// each place name of each placedb record. this allows us
		// to ultimately set Address::m_placedbName1 and 2.
		//
		int32_t h1 = hash32 ( (char *)vbuf1 , nvbuf1 * 4 , 0 );
		int32_t h2 = hash32 ( (char *)vbuf2 , nvbuf2 * 4 , 0 );

		// . update max buf if its a new string
		// . include one byte for the \0
		// . include 4 bytes for preceeding score
		if ( h1 &&           ! vt.isInTable(&h1) ) {
			// update what we allocate
			need+=gbstrlen(pn1)+5;
			// add to ptr table
			if ( ! ptrTable.addKey ( &h1 , &pn1 ) ) goto hadError;
		}

		if ( h2 && h2!=h1 && ! vt.isInTable(&h2) ) {
			// update what we allocate
			need+=gbstrlen(pn2)+5;
			// add to ptr table
			if (! ptrTable.addKey ( &h2 , &pn2 ) ) goto hadError;
		}

		// add to voting table
		if ( h1 &&             ! vt.addTerm32 ( &h1 ) ) goto hadError;
		if ( h2 && h2 != h1 && ! vt.addTerm32 ( &h2 ) ) goto hadError;

		// break here for now to figure it out!
		//char *xx=NULL;*xx=0; 

		//log("build: matching sim=%.02f for %s vs %s",sim,pn1,pn2);

		// skip this guy if not a match
		if ( sim1 < 85.0 && 
		     sim2 < 85.0 &&
		     sim3 < 85.0 &&
		     sim4 < 85.0    ) continue;

		// 85%+ is good enough to be a vote for
		if ( sim1 >= 85.0 ) st->m_votesForPlaceName1++;
		if ( sim2 >= 85.0 ) st->m_votesForPlaceName2++;
		if ( sim3 >= 85.0 ) st->m_votesForPlaceName1++;
		if ( sim4 >= 85.0 ) st->m_votesForPlaceName2++;

		// that is good enough
		break;
	}

	// set the reply
	char *reply = NULL;
	if ( need < TMPBUFSIZE ) reply = slot->m_tmpBuf;
	else reply = (char *)mmalloc ( need , "repbuf" );
	if ( ! reply ) goto hadError;
	char *rend = reply + need;

	// reply is either 1 or 0
	//char *reply = slot->m_tmpBuf;
	// clear it
	uint8_t flags = 0;
	// use flags
	if ( st->m_votesForStreet     ) flags |= AF_VERIFIED_STREET;
	if ( st->m_votesForStreetNum  ) flags |= AF_VERIFIED_STREET_NUM;
	if ( st->m_votesForPlaceName1 ) flags |= AF_VERIFIED_PLACE_NAME_1;
	if ( st->m_votesForPlaceName2 ) flags |= AF_VERIFIED_PLACE_NAME_2;
	// sanity checks
	if ( (flags & AF_VERIFIED_STREET_NUM) && 
	     !(flags & AF_VERIFIED_STREET     ) ) { char *xx=NULL;*xx=0; }
	if ( (flags & AF_VERIFIED_PLACE_NAME_1) && 
	     !(flags & AF_VERIFIED_STREET_NUM ) ) { char *xx=NULL;*xx=0; }
	if ( (flags & AF_VERIFIED_PLACE_NAME_2) && 
	     !(flags & AF_VERIFIED_STREET_NUM ) ) { char *xx=NULL;*xx=0; }

	// point to reply buffer after that first byte
	char *rptr = reply ;

	// now scan these placedb recs to find the most agreed upon lat/lon
	// so that we do not trust the one on our page necessarily
	double lat;
	double lon;
	int32_t   numVotes;
	// need the street number hash so we only get lat/lon coords from
	// addresses with the same street number as well as street
	if ( ! getBestLatLon ( list , &lat, &lon , &numVotes, niceness , 
			       myStreetNumHash ) )
		goto hadError;
	// add that in
	*(int32_t   *)rptr = numVotes; rptr += 4;
	*(double *)rptr = lat; rptr += sizeof(double);
	*(double *)rptr = lon; rptr += sizeof(double);

	// then the 1 byte flag
	*rptr = flags; rptr++;

	// . now we store all the alternative place names and their vote count,
	//   as int32_t as it was 2 or more. so scan the score table to find
	//   the hashes of the winners, then lookup the hashes of the winners
	//   in the ptr table, ptrTable, to get the string to send back.
	// . we set Address::m_placedbNames to this string above when we
	//   process this reply
	for ( int32_t i = 0 ; i < vt.m_numSlots ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		// skip emptyies
		if ( vt.isEmpty(i) ) continue;
		// get score
		int32_t score = vt.getScoreFromSlot ( i );
		// skip if too small
		if ( score <= 1 ) continue;
		// get key
		int32_t key = *(int32_t *)vt.getKeyFromSlot ( i );
		// grab string
		char *str = *(char **)ptrTable.getValue ( &key );
		// must be there
		if ( ! str ) { char *xx=NULL;*xx=0; }
		// skip if empty string... was it just "the "???
		if ( ! *str ) continue;
		// store score first
		*(int32_t *)rptr = score;
		// skip it
		rptr += 4;
		// get length
		int32_t len = gbstrlen(str);
		// store in reply buf, include \0
		gbmemcpy ( rptr , str , len + 1 );
		// skip over
		rptr += len + 1;
		// sanity check
		if ( rptr > rend ) { char *xx=NULL;*xx=0; }
	}
	// the reply size may be less than what we allocated
	int32_t replySize = rptr - reply;

	// set it
	//if ( st->m_votes ) *reply = 1;
	//else               *reply = 0;
	// all done with this
	mdelete ( st , sizeof(State2c),"msg2cfr");
	delete (st);
	// send the 1 byte reply
	g_udpServer.sendReply_ass(reply,replySize,reply,need,slot);
}

// the msg2c request was asking for the address of a possible place name,
// like "Tingley Colesium", so this sends back the address
void sendBackAddress ( State2c *st ) {

	// int16_tcut
	RdbList *list = &st->m_list;
	// winning street address
	char *winner = NULL;
	int32_t  winnerSnh = 0;
	// and max count
	int32_t max = 0;
	// get this
	UdpSlot *slot = st->m_slot;
	// set myBigHash for comparing
	key128_t *pk = &st->m_placedbKey;
	int64_t myBigHash = g_placedb.getBigHash(pk);

	// set up a little voting table
	char vbuf[30000];
	HashTableX vt;
	vt.set ( 4 , 4 , 100 ,vbuf,30000,false,0 ,"addrvt");

	while ( ! list->isExhausted() ) {
		// breathe
		QUICKPOLL ( st->m_niceness );
		// get it
		char *data = list->getCurrentData();
		// get the key
		key128_t k; list->getCurrentKey(&k);
		// skip it
		list->skipCurrentRecord();
		// cast it
		//Address a2; 
		// set "a2"
		//setFromStr ( &a2, data, 0 , st->m_niceness );
		// must not be same site as us
		//if ( a2.m_domHash32 == st->m_domHash32 ) continue;
		// and different ip from us
		//if ( iptop(a2.m_ip) == iptop(st->m_ip) ) continue;
		// sanity check
		if (g_placedb.getBigHash(&k)!=myBigHash) {char*xx=NULL;*xx=0;}

		// now his key's street hash was replaced with his placename1
		// hash, and (TODO) his street num hash was made to include
		// his actual street name hash, so we can use this to make sure
		// everyone agrees on the same street address

		// get the street num hash of that record
		int32_t snh = g_placedb.getStreetNumHash ( &k );
		// get his vote count, we take the max
		if ( ! vt.addTerm32 ( &snh ) ) {
			// all done with this
			mdelete ( st , sizeof(State2c),"msg2cfr");
			delete (st);
			g_udpServer.sendErrorReply ( slot,g_errno );
			return;
		}

		// does this guy have a latitude/longitude in him?
		char *pp = data;
		// count out like 9 semicolons to see
		int32_t scount = 0;
		for ( ; scount < 10 ; pp++ )
			if ( *pp == ';' ) scount++;
		// check it out
		bool hasLatLon = ( pp[1] != ';' );
		// bad?
		if ( scount < 5 ) { char *xx=NULL;*xx=0; }
		// get his count
		int32_t score = vt.getScore32 ( &snh );
		// new max?
		if ( score < max ) continue;
		// on tie, pref if has lat/lon
		if ( score == max && ! hasLatLon ) continue;
		// point to winning address then
		winner = data;
		// set this for loop below
		winnerSnh = snh;
	}

	// for looping again, reset this, but only if we had a winner
	double bestLat;
	double bestLon;
	int32_t   numVotes;
	if ( winner && ! getBestLatLon ( list, 
					 &bestLat, 
					 &bestLon, 
					 &numVotes,
					 st->m_niceness,
					 winnerSnh ) ) {
		// all done with this
		mdelete ( st , sizeof(State2c),"msg2cfr");
		delete (st);
		g_udpServer.sendErrorReply ( slot,g_errno );
		return;
	}

	// all done with this
	// CRAP! the winner is referencing into this list which is in this
	// state we are freeing!

	// debug
	//log("placedb: input=%s output=%s",st->m_addrStr,winner);

	// if no winner, send empty reply
	if ( ! winner ) {
		mdelete ( st , sizeof(State2c),"msg2cfr");
		delete (st);
		g_udpServer.sendReply_ass(NULL,0,NULL,0,slot);
		return;
	}

	int32_t wlen = gbstrlen(winner);
	// hos can this be?
	if ( wlen <= 1 ) { char *xx=NULL;*xx=0; }
	// send winner back. add in extra for lat/lon
	int32_t need = wlen + 48;
	// use the slot's tmp buf to hold the reply if we can
	char *reply = slot->m_tmpBuf;
	// make buf if we need to
	if ( need > TMPBUFSIZE )
		reply = (char *)mmalloc ( need , "msg2creply");
	// return error on error
	if ( ! reply ) {
		mdelete ( st , sizeof(State2c),"msg2cfr");
		delete (st);
		g_udpServer.sendErrorReply ( slot,g_errno );
		return;
	}
	// now store here
	char *p = reply;
	*(int32_t   *)p = numVotes; p += 4;
	*(double *)p = bestLat ; p += sizeof(double);
	*(double *)p = bestLon ; p += sizeof(double);
	// how much to copy, include \0
	int32_t bytes = wlen + 1;
	// copy over all but lat and lon if there, includes last ';'
	gbmemcpy ( p , winner , bytes ); p += bytes;
	// how big is reply?
	int32_t replySize = p - reply;
	// sanity check
	if ( replySize > need ) { char *xx=NULL;*xx=0; }
	// free it last since winner points into it
	mdelete ( st , sizeof(State2c),"msg2cfr");
	delete (st);
	// send back empty reply if no winner, strange!
	g_udpServer.sendReply_ass(reply,replySize,reply,need,slot);
}

// returns false and sets g_errno on error
bool getBestLatLon ( RdbList *list      ,
		     double  *bestLat   ,
		     double  *bestLon   ,
		     int32_t    *numVotes  ,
		     int32_t     niceness  ,
		     int32_t     winnerSnh ) {
	// reset ptr, since we did a loop above with it
	list->resetListPtr();
	// no best now
	int32_t bestScore = 0;
	*bestLat = NO_LATITUDE;
	*bestLon = NO_LONGITUDE;
	*numVotes = 0;
	// voting table for lat/lon
	HashTableX gpsTable;
	char gbuf[1024];
	gpsTable.set ( 8 , 4 , 32 , gbuf , 1024 , false , niceness,"addrgps");
	// now loop again looking for the best lat/lon of the winning street
	while ( ! list->isExhausted() ) {
		// breathe
		QUICKPOLL ( niceness );
		// get it
		char *data = list->getCurrentData();
		// need this now
		//int32_t dataSize = list->getCurrentDataSize();
		// get the key
		key128_t k; list->getCurrentKey(&k);
		// skip it
		list->skipCurrentRecord();
		// get the street num hash of that record
		int32_t snh = g_placedb.getStreetNumHash ( &k );
		// skip if not winner
		if ( winnerSnh && snh != winnerSnh ) continue;
		// grab it from the string (TODO: use this for above too!)
		double lat;
		double lon;
		getLatLonFromStr ( data , &lat , &lon );
		// skip if either not there
		if ( lat == NO_LATITUDE  ) continue;
		if ( lon == NO_LONGITUDE ) continue;
		// sanity check
		if ( sizeof(double) != 8 ) { char *xx=NULL;*xx=0; }
		// get hash for them
		int64_t h1 = *(int64_t *)&lat;
		int64_t h2 = *(int64_t *)&lon;
		int64_t h = (h1<<1) ^ h2;
		// add to table
		if ( ! gpsTable.addTerm ( &h ) ) 
			return false;
		// get score
		int32_t score = gpsTable.getScore ( &h );
		// skip if not best
		if ( score <= bestScore ) continue;
		// otherwise set it
		*bestLat   = lat;
		*bestLon   = lon;
		bestScore = score;
	}
	*numVotes = bestScore;
	return true;
}

uint8_t getCountryIdFromAddrStr ( char *addr ) {
	char *p = addr;
	int32_t scount = 0;
	for ( ; scount < 7 ; p++ )
		if ( *p == ';' ) scount++;
	// empty? assume US then
	if ( *p == ';' ) return CRID_US;
	// map abbr to crid
	uint8_t crid = getCountryId ( p );
	return crid;
}

char *getLatLonPtrFromStr ( char *data ) {
	// now point to latitude,longitude
	// skip city,state,zip,something,hash,ip
	char *latitudePtr = data;
	int32_t scount = 0;
	for ( ; scount < 11 ; latitudePtr++ )
		if ( *latitudePtr == ';' ) scount++;
	// pts past that ';'
	return latitudePtr;
}

void getLatLonFromStr ( char *data, double *lat, double *lon ) {
	// set lat lon
	*lat = NO_LATITUDE;
	*lon = NO_LONGITUDE;
	// now point to latitude,longitude
	// skip city,state,zip,something,hash,ip
	char *latitudePtr = getLatLonPtrFromStr ( data );
	// find end of it
	char *latitudeEnd = latitudePtr;
	// this may not be incremented at all if we have no latitude
	for ( ; *latitudeEnd != ';' ; latitudeEnd++ );
	// if we had something, then assign it
	if ( *latitudePtr != ';' ) 
		*lat = atod2(latitudePtr,latitudeEnd-latitudePtr);
	// skip to l
	char *longitudePtr = latitudeEnd + 1;
	// need this now
	//char *dataEnd = data + dataSize;
	// this may not be incremented at all if we have no latitude
	char *longitudeEnd = longitudePtr;
	// this may not be incremented at all if we have no latitude
	for ( ; *longitudeEnd && *longitudeEnd != ';' ; longitudeEnd++ );
	// . this is the last item so it is already \0 terminated
	// . sometimes is not \0 terminated because it is a sequence of 
	//   replies serialized into our reply buffer, m_sb
	if ( *longitudePtr && *longitudePtr != ';' )
		*lon = atod2(longitudePtr,longitudeEnd-longitudePtr);
	// sanity check
	if ( *lon == 0.0 || *lat == 0.0 ) { 
		log("addr: bad 0.0 lon or lat");
		*lat = NO_LATITUDE;
		*lon = NO_LONGITUDE;
	}
}


//
// used by Events.cpp and by Dates.cpp 
//

int streetcmp ( const void *arg1 , const void *arg2 ) {
	// get the addresses
	Place *street1 = *(Place **)arg1;
	Place *street2 = *(Place **)arg2;
	// get word position
	int32_t a1 = street1->m_a;
	int32_t a2 = street2->m_a;
	// if tied, prefer the one whose m_address is set! that means
	// it came from a inlined or verified address
	if ( a1 == a2 ) {
		if ( street1->m_address ) return -1;
		if ( street2->m_address ) return  1;
		if ( street1->m_alias   ) return -1;
		if ( street2->m_alias   ) return  1;
		return 0;
	}
	// sanity check
	if ( a1 < 0 ) { char *xx=NULL;*xx=0; }
	if ( a2 < 0 ) { char *xx=NULL;*xx=0; }
	// compare
	return ( a1 - a2);
}

// . allow "store hours" addresses to telescope up without limit
// . only store streets now that have PLF2_AFTER_AT set, or are a street
//   name like "404 John NE"
// . and store streets in addresses that have verified street, name1 or name2
//   OR are inlined
// . returns false and sets g_errno on error
bool Addresses::setFirstPlaceNums ( ) {

	// no double calls
	//if ( m_sorted ) { char *xx=NULL;*xx=0; }
	if ( m_sorted ) {
		mfree ( m_sorted , m_sortedSize , "asortbuf");
		m_sorted = NULL;
		m_sortedValid = false;
	}

	//char sbuf[10000];
	// set the sorted[] array which consists of addresses 
	// sorted by their street position, or in the
	// case if PLF2_IS_NAME addresses, their place name 1 position
	//Place **sorted = (Place **)sbuf;
	// how much space do we need?
	int32_t need = (m_am.getNumPtrs() + m_sm.getNumPtrs())* 4;
	// alloc if we need to
	m_sorted = (Place **)mmalloc(need,"getaddrtab");
	if ( ! m_sorted ) return false;
	m_sortedValid = true;
	// store for freeing
	m_sortedSize = need;
	// reset count
	m_numSorted = 0;


	//////////////////////////////////
	// 
	// add streets from m_streets[]
	//
	//////////////////////////////////
	int32_t lasta1 = -1;
	for ( int32_t i = 0 ; i < m_sm.getNumPtrs() ; i++ ) {
		// give up control
		QUICKPOLL(m_niceness);
		// get streets #i
		Place *street = (Place *)m_sm.getPtr(i);
		// skip if po box. causes us to miss setting DF_STORE_HOURS
		// for a date because there is a PO box as well as the
		// bldg street address in the "store hours" section.
		if ( street->m_flags2 & PLF2_IS_POBOX ) continue;
		// is the street name really a place name?
		bool isName  = ( street->m_flags2 & PLF2_IS_NAME  );
		// assume not a good place
		bool good = false;
		// is our street really a place name
		if ( street->m_flags2 & PLF2_AFTER_AT ) good = true;
		// intersections are good
		if ( street->m_flags2 & PLF2_INTERSECTION ) good = true;
		// if it is a verified place name, allow it through too!
		Address *aa = street->m_address;
		if ( aa ) {
			if ( aa->m_flags&AF_VERIFIED_PLACE_NAME_1) good = true;
			if ( aa->m_flags&AF_VERIFIED_PLACE_NAME_2) good = true;
		}
		// . allow an aliases street name to be ok
		// . helps fix zvents.com invalid mariner url even though
		//   "The Filling Station" is really after an at... but we
		//   were not picking that up before because of another bug
		//   which is now fixed.
		if ( street->m_alias ) good = true; // afterAt = true;
		// get the address or the alias, whichever is non-NULL, if any
		Address *ax = aa;
		if ( ! ax ) ax = street->m_alias;
		// sometimes we re-nege on our lat lon address we added because
		// it was ambiguous because their were multiple lat/lon pairs
		// and we didn't know which one was right. we really should
		// delete them i guess up there but i am not sure they were
		// last on stack? this is for addresses that are like 
		// after at like "at Norquay" and they have a latlon only
		// flag...
		if ( ax && (ax->m_flags3 & AF2_LATLON) ) {
		     // make sure lat/lon is not AMBIG_LATITUDE
			if ( ax->m_latitude  == AMBIG_LATITUDE  ||
			     ax->m_longitude == AMBIG_LONGITUDE ||
			     ax->m_latitude  == NO_LATITUDE     ||
			     ax->m_longitude == NO_LONGITUDE )
				continue;
		}
		// is not a name, that's good!
		if ( ! isName ) good = true;
		// must have address or be after at OR it must be a 
		// street name like "400 John NE"
		if ( ! good ) continue;
		// skip if it is a place to buy tickets and not really
		// an actual event place
		//if ( street->m_flags2 & PLF2_TICKET_PLACE ) continue;
		// do add po box addresses, the above loop will just 
		// disqualify the event if this is the best address for it!
		//if ( street->m_flags2 & PLF2_IS_POBOX ) continue;
		// get the street name word range
		int32_t a1 = street->m_a;
		int32_t b1 = street->m_b;
		// sanity check
		if ( a1 < 0 || b1 < 0 ) { char *xx=NULL;*xx=0; }
		// stop dups
		if ( a1 == lasta1 ) continue;
		// update
		lasta1 = a1;
		// add it
		m_sorted[m_numSorted++] = street;
	}


	// . now sort the array by the street/name word start number
	// . i.e. sort streets by their position on the page
	// . in case of ties prefers the street with m_address set, because
	//   that indicates it came from an inlined or verified address
	gbqsort ( m_sorted , m_numSorted , 4 , streetcmp , m_niceness );

	///////////////////////////////////////
	//
	// . remove duplicate places
	// . fix "classes at Blue Tribe School. contact tammy. 
	//   School 111 Maple SE Abq NM" for panjea.org.
	// . basically an address can have a place name and a street
	//   and our streets array treats both kinds separately, so we
	//   have to detect if what we think is a different place name
	//   is really the place name of a street name here
	//
	///////////////////////////////////////
	int32_t numSorted3 = 0;
	for ( int32_t i = 0 ; i < m_numSorted - 1 ; i++ ) {
		// give up control
		QUICKPOLL(m_niceness);
		// get address #i
		Place *street = m_sorted[i];
		// get next
		Place *next = m_sorted[i+1];
		// re-add "street"
		bool add = false;
		// we must eb after at
		if ( ! ( street->m_flags2 & PLF2_AFTER_AT ) ) add = true;
		// and he must be a regular street
		if ( next->m_flags2 & PLF2_AFTER_AT ) add = true;
		if ( next->m_flags2 & PLF2_IS_NAME  ) add = true;
		// and must be kinda close together
		if ( next->m_alnumA - street->m_alnumA > 10 ) add = true;
		// fix "Grants Middle Schoole ... 111 Easterday NE" for
		// www.superpages.com/yellowpages/C-Junior%2BHigh%2B%2526%2BMiddle%2BSchools/S-NM/T-Albuquerque
		// because we get two places for that one address.
		// one place is "Grants Middle School" as a fake street place
		// name, and the other is the address with the actual street
		// which also incorporates the same "Grants Middle School" as
		// its name... so stop that!
		if ( next->m_address && 
		     next->m_address->m_name1 &&
		     next->m_address->m_name1->m_a == street->m_a )
			add = false;
		if ( next->m_address && 
		     next->m_address->m_name2 &&
		     next->m_address->m_name2->m_a == street->m_a )
			add = false;
		// ok, ignore us!
		if ( ! add ) continue;
		// re-add it
		m_sorted[numSorted3++] = street;
	}
	// last one
	if ( m_numSorted > 0 )
		m_sorted[numSorted3++] = m_sorted[m_numSorted-1];

	// replace with the smaller deduped number
	m_numSorted = numSorted3;

	// clear all in case of re-call
	for ( int32_t i = 0 ; i < m_sections->m_numSections ; i++ ) {
		QUICKPOLL ( m_niceness );
		Section *sn = &m_sections->m_sections[i];
		sn->m_firstPlaceNum = -1;
	}

	///////////////////////////////
	//
	// loop over streets in sorted[] and hash their sections
	//
	///////////////////////////////
	int32_t lasta = -1;
	for ( int32_t i = 0 ; i < m_numSorted ; i++ ) {
		// give up control
		QUICKPOLL(m_niceness);
		// get address #i
		Place *street = m_sorted[i];
		// get word position, word #a
		int32_t a = street->m_a;
		if ( a == lasta ) continue;
		lasta = a;
		if ( a < 0 ) { char *xx=NULL;*xx=0; }
		// get section
		Section *sa = m_sections->m_sectionPtrs[a];
		// telescope up
		for ( ; sa ; sa = sa->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if already has one
			if ( sa->m_firstPlaceNum >= 0 ) break;
			// we are the first place contained by this section
			sa->m_firstPlaceNum = i;
		}

		// dbug
		/*
		int32_t b = street->m_b;
		SafeBuf tmp;
		char *start = m_wptrs[a];
		char *end = m_wptrs[b-1]+m_words->m_wordLens[b-1];
		tmp.safeMemcpy(start,end-start);
		tmp.pushChar(0);
		Section **sp = m_sections->m_sectionPtrs;
		int32_t sa = -1;
		int32_t aa = -1;
		if ( street->m_address ) sa = street->m_address->m_street->m_a;
		if ( street->m_alias ) sa = street->m_alias->m_street->m_a;
		log("dbug: (a=%"INT32",b=%"INT32") sec=%"XINT32" %s addr=%"INT32" alias=%"INT32" "
		    "url=%s",
		    a,b,
		    (int32_t)sp[a],
		    tmp.getBufStart() ,
		    (int32_t)sa,//street->m_address,
		    (int32_t)aa,//street->m_alias, 
		    m_url->m_url);
		*/
	}
	return true;
}		


// . returns false and sets g_errno on error
// . "i" is the word position of "and" or "&"
bool Addresses::addIntersection ( int32_t i , int32_t alnumPos ) {

	//if ( m_ns >= MAX_STREETS ) return true;

	bool hadUpper = false;

	//////////
	//
	// to the LEFT of the "and"
	//
	//////////

	int32_t good1 = -1;
	int32_t j1 = i;
	int32_t numPos1 = -1;
	int32_t lastBeforeNum1 = -1;
	int32_t routePos1 = -1;
	int32_t ap1 = alnumPos;
	int32_t dirCount1 = 0;
	int32_t wcount1 = 0;
	int32_t icount1 = 0;
	bool firstWord = true;
	int64_t lastWid1 = 0LL;
	bool explicit1 = false;
	bool hadPage1 = false;
	bool lastWasStreetInd = false;
	bool badLeftStreetEnd = false;

	// do not back up past this
	int32_t minj = i - 14; if ( minj < 0 ) minj = 0;
	// now back up to the left, see if that is a street
	for ( int32_t j = i - 1 ; j >= minj ; j-- ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// count it
		if ( m_wids[j] ) ap1--;
		// between is a total killer!
		if ( m_wids[j] == h_between ) return true;
		if ( m_wids[j] == h_btwn    ) return true;
		if ( m_wids[j] == h_bet     ) return true;
		// try this out
		if ( ! isInStreet( j ) ) break;
		// if not alnum word, keep going
		if ( ! m_wids[j] ) continue;

		// detect "corner of the page"
		if ( m_wids[j] == h_page ) hadPage1 = true;

		if ( m_wids[j] == h_intersection &&  lastWid1 == h_of ) {
			explicit1 = true;
			// include "intersection of" so it is not in name
			good1 = j;
			break;
		}

		if ( m_wids[j] == h_corner &&  lastWid1 == h_of ) {
			// ignore "corner of the page"
			if ( hadPage1 ) return true;
			explicit1 = true;
			// include "corner of" so it is not in name
			good1 = j;
			break;
		}

		// save it
		bool saved3 = lastWasStreetInd;
		// reset this
		lastWasStreetInd = false;
		
		// first word we encounter must be a directional or
		// street indicator
		if ( firstWord ) {
			firstWord = false;
			IndDesc *id;
			id=(IndDesc *)g_indicators.getValue(&m_wids[j]);
			bool ok = false;
			if ( id && (id->m_bit & IND_DIR   ) &&
			     // must have space or comma before us to prevent
			     // "tom's and jerry's"
			     j>0 && 
			     ( is_wspace_a(m_wptrs[j][-1]) ||
			       m_wptrs[j][-1]==',') ) {
				ok = true;
				dirCount1++;
				icount1++;
			}
			if ( id && (id->m_bit & IND_STREET) ) {
				lastWasStreetInd = true;
				ok = true;
				icount1++;
			}
			// "14th and W St. NW" for gwair.org
			// "i-25 & hwy 301"
			if ( is_digit(m_wptrs[j][0]) &&
			     // fix "21+ & I.D. Required" for groundkontrol.com
			     is_alnum_a(m_wptrs[j][m_wlens[j]-1]) )
				ok = true;
			// otherwise, stop on any other word
			if ( ! ok ) {
				badLeftStreetEnd = true;
				//break;
			}
		}

		bool isNum = false;
		// this is good "4th and 5th"
		if      ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 's' &&
			  m_wptrs[j][m_wlens[j]-1] == 't' )
			good1 = j;
		else if ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 'n' &&
			  m_wptrs[j][m_wlens[j]-1] == 'd' )
			good1 = j;
		else if ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 'r' &&
			  m_wptrs[j][m_wlens[j]-1] == 'd' )
			good1 = j;
		else if ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 't' &&
			  m_wptrs[j][m_wlens[j]-1] == 'h' )
			good1 = j;
		// numbers not allowed unless after "route", etc.
		else if ( m_words->isNum(j) ) {
			numPos1 = j;
			isNum = true;
		}

		// allow "79 st & shore rd" for 
		// www.nycgovparks.org/facilities/playgrounds
		if ( isNum && saved3 ) good1 = j;

		// record this
		if ( numPos1 == -1 ) lastBeforeNum1 = j;

		// this one too
		if ( m_wids[j] == h_route   ) routePos1 = j;
		if ( m_wids[j] == h_rte     ) routePos1 = j;
		if ( m_wids[j] == h_rt      ) routePos1 = j;
		if ( m_wids[j] == h_hwy     ) routePos1 = j;
		if ( m_wids[j] == h_highway ) routePos1 = j;
		if ( m_wids[j] == h_hiway   ) routePos1 = j;
		if ( m_wids[j] == h_road    ) routePos1 = j;
		if ( m_wids[j] == h_rd      ) routePos1 = j;
		// "Locatd on US 64 and New Mexico Highway X"
		if ( m_wids[j] == h_us      ) routePos1 = j;
		if ( m_wids[j] == h_interstate ) routePos1 = j;
		if ( m_wids[j] == h_i          ) routePos1 = j;

		// stop if word after the number is not a route
		if ( ! isNum && numPos1 >= 0 && routePos1 == -1 )
			break;

		// save it
		lastWid1 = m_wids[j];

		// no mixing caps
		if ( s_lc.isInTable ( &m_wids[j] ) ) continue;
		// cap?
		if ( is_upper_utf8(m_wptrs[j]) ) hadUpper = true;
		// do not include a lower case guy
		else if ( hadUpper && is_lower_utf8(m_wptrs[j]) )
			break;

		// count it
		wcount1++;

		// note it
		j1 = j;
	}

	// scan to left looking for "corner of" etc
	int32_t minsj = j1 - 10; if ( minsj < 0 ) minsj = 0;
	bool hadOf = false;
	for ( int32_t sj = j1 - 1 ; sj > minsj ; sj-- ) {
		// skip tags etc
		if ( ! m_wids[sj] ) continue;
		// of is ok
		if ( m_wids[sj] == h_of ) { hadOf = true; continue; }
		// bad i fno of
		if ( ! hadOf ) break;
		// corner of intersection of
		if ( m_wids[sj] != h_intersection &&
		     m_wids[sj] != h_corner ) 
			break;
		explicit1 = true;
		break;
	}

	if ( badLeftStreetEnd && ! explicit1 ) return true;

	// . return if only indicator in street name. 
	// . fixes "NE and NW parts of Metro Atlanta."
	if ( ! explicit1 && dirCount1 == wcount1 ) return true;

	// reset it to before the pure number if no "route" before number
	if ( ! explicit1 && numPos1 >= 0 && routePos1 != numPos1 - 2 ) {
		j1 = lastBeforeNum1;
		// if negative give up!
		if ( j1 < 0 ) return true;
	}
	// use good1 if we had that!
	if ( good1 >= 0 && good1 < j1 ) 
		j1 = good1;

	// return if no street to the left
	if ( j1 == i ) return true;

	//////////
	//
	// to the right of the "and"
	//
	//////////

	bool good2 = false;
	int32_t icount2 = 0;
	int32_t dirCount2 = 0;
	int32_t wcount2 = 0;
	int32_t j2 = i;
	bool hadStreetInd = false;
	bool hadDirInd = false;
	int32_t numPos2 = -1;
	int32_t lastBeforeNum2 = -1;
	int32_t routePos2 = -1;
	int32_t ap2 = alnumPos;
	bool hadCornerDirInd2 = false;
	bool firstWord2 = true;

	// do not exceed this
	int32_t maxj = i + 14; if ( maxj > m_nw ) maxj = m_nw;
	// need a street to the right as well
	for ( int32_t j = i + 1 ; j < maxj ; j++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// count it
		if ( m_wids[j] ) ap2++;
		// try this out
		if ( ! isInStreet( j ) ) break;
		// skip if not alnum at this point
		if ( ! m_wids[j] ) continue;

		bool savedFirstWord2 = firstWord2;
		if ( firstWord2 ) firstWord2 = false;

		// if we hit a street indicator, only a dir can follow
		IndDesc *id=(IndDesc *)g_indicators.getValue(&m_wids[j]);
		if ( id && (id->m_bit & IND_STREET) && ! savedFirstWord2 ) {
			hadStreetInd = true;
			icount2++;
			good2 = true;
		}
		else if ( id && (id->m_bit & IND_DIR ) ) {
			hadDirInd = true;
			// fix "Central Ave SE and Richmond SE  Albuquerque"
			if ( m_wlens[j] == 2 ) 
				hadCornerDirInd2 = true;
			icount2++;
			dirCount2++;
			good2 = true;
		}
		else if ( hadStreetInd || hadCornerDirInd2 )
			break;

		// this is good "4th and 5th"
		if      ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 's' &&
			  m_wptrs[j][m_wlens[j]-1] == 't' )
			good2 = true;
		else if ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 'n' &&
			  m_wptrs[j][m_wlens[j]-1] == 'd' )
			good2 = true;
		else if ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 'r' &&
			  m_wptrs[j][m_wlens[j]-1] == 'd' )
			good2 = true;
		else if ( is_digit(m_wptrs[j][0]) && m_wlens[j] >= 3 &&
			  m_wptrs[j][m_wlens[j]-2] == 't' &&
			  m_wptrs[j][m_wlens[j]-1] == 'h' )
			good2 = true;
		// numbers not allowed unless after "route", etc.
		else if ( m_words->isNum(j) ) {
			numPos2 = j;
			// stop if had no route
			if ( routePos2 == -1 ) break;
		}

		// fix for 14th and Curtis Denver CO
		if ( cityAdm1Follows ( j ) ) {
			good2 = true;
			break;
		}

		// record this
		if ( numPos2 == -1 ) lastBeforeNum2 = j;

		// this one too
		if ( m_wids[j] == h_route   ) routePos2 = j;
		if ( m_wids[j] == h_rte     ) routePos2 = j;
		if ( m_wids[j] == h_rt      ) routePos2 = j;
		if ( m_wids[j] == h_hwy     ) routePos2 = j;
		if ( m_wids[j] == h_highway ) routePos2 = j;
		if ( m_wids[j] == h_hiway   ) routePos2 = j;
		if ( m_wids[j] == h_road    ) routePos2 = j;
		if ( m_wids[j] == h_rd      ) routePos2 = j;
		// "Locatd on US 64 and New Mexico Highway X"
		if ( m_wids[j] == h_us      ) routePos2 = j;
		if ( m_wids[j] == h_interstate ) routePos2 = j;
		if ( m_wids[j] == h_i          ) routePos2 = j;

		// no mixing caps
		if ( s_lc.isInTable ( &m_wids[j] ) ) continue;
		// cap?
		if ( is_upper_utf8(m_wptrs[j]) ) hadUpper = true;
		// do not include a lower case guy
		else if ( hadUpper && is_lower_utf8(m_wptrs[j]) )
			break;

		// count it
		wcount2++;

		// note it
		j2 = j;
	}

	// reset it to before the pure number if no "route" before number
	if ( numPos2 >= 0 && routePos2 != numPos2 - 2 ) {
		j2 = lastBeforeNum2;
		// if negative give up!
		if ( j2 < 0 ) return true;
	}

	// fix "First Nations North and South" and 
	// "Broadway South East and North East"
	if ( ! explicit1 && wcount2 == dirCount2 ) return true;

	// trim after the "route x"
	if ( numPos2 == routePos2 + 2 )
		j2 = numPos2;

	// return if no street to the left
	if ( j2 == i ) return true;

	// these are indivative of good street names
	if ( routePos2 >= 0 ) good2 = true;

	// no need for street indicator on right street if we have 
	// "intersection of" or whatever to left of left street


	// need to have a "good" street name in there
	if ( ! explicit1 && ! good2 ) return true;

	int32_t a = j1;
	int32_t b = j2+1;

	// . no starting/ending with stop word
	// . i-25 is exception!
	if ( m_wids[j1] != h_i && m_words->isStopWord(j1) ) return true;
	if ( m_wids[j2] != h_i && m_words->isStopWord(j2) ) return true;

	// count alnums from a to b
	int32_t ac = 0;
	for ( int32_t i = a ; i < b ; i++ ) 
		if ( m_wids[i] ) ac++;

	// add the INTERSECTION
	Place *street = (Place *)m_sm.getMem(sizeof(Place));
	if ( ! street ) return false;
	street->m_a       = a;
	street->m_b       = b;
	street->m_alnumA  = ap1;
	street->m_alnumB  = ap1 + ac; // ap2+1;
	street->m_type    = PT_STREET;
	street->m_str     = m_wptrs[j1];
	street->m_strlen  = m_wptrs[j2]-m_wptrs[j1]+m_wlens[j2];
	//street->m_adm1[0] = 0;
	//street->m_adm1[1] = 0;
	//street->m_crid    = 0;
	street->m_flags2  = PLF2_INTERSECTION;
	street->m_bits    = 0;
	street->m_address = NULL;
	street->m_alias   = NULL;
	// set its m_hash member
	setHashes ( street , m_words , m_niceness );

	// prevent overlap with next street
	//lastb = m_street->m_b;
	// . need to know this for getting place name
	// . place name must also be in upper case if 
	//   the street is...
	// . TODO: do we need this???? mdw
	//if ( uc == 1 ) m_street->m_bits |= PLF_HAS_UPPER;
	// set some bits
	for ( int32_t k = a ; m_bits && k < b ; k++ )
		m_bits->m_bits[k] |= D_IS_IN_STREET;
	// point to next street
	//m_ns++;
	return true;
}

// . returns false and sets g_errno on error
// . sets *good to true when we have a completed street
bool Addresses::isInStreet ( int32_t j ) {
	// we can never contain a tag
	if ( m_tids[j] ) {
		// skip if <sup>
		if ( m_tids[j] == TAG_SUP ) return true;
		if ( m_tids[j] == (TAG_SUP|BACKBIT) ) return true;
		// . crap but micorosft front page has brs
		// . "intersection of Interstate 405 and Sunset <br>Boulevard"
		if ( m_tids[j] == TAG_BR ) return true;
		// be a little more sensitive with this since it is easier
		// to have false positives because we do not have a street
		// number!
		return false;
	}
	// are we punctuation?
	if ( ! m_wids[j] ) {
		// single space is ok
		if (m_wptrs[j][0]==' '&&m_wlens[j]==1) return true;
		// double space is ok
		if (m_wptrs[j][0]==' '&&m_wptrs[j][1]==' '&& m_wlens[j]==2) 
			return true;
		// period only after abbreviation
		if ( m_wptrs[j][0] == '.' && j > 0 && 
		     isAbbr(m_wids[j-1])&&
		     m_wptrs[j][1] == ' ' && m_wlens[j]==2 )
			return true;
		// . period after a single letter as well
		// . N. M.
		if ( m_wptrs[j][0] == '.' && j > 0 && 
		     m_wlens[j-1]==1 && 
		     // fix "8. wall street"
		     !is_digit(m_wptrs[j-1][0]) &&
		     m_wptrs[j][1] == ' ' && 
		     m_wlens[j]==2 ) 
			return true;
		// N.M.
		if ( m_wptrs[j][0] == '.' && j > 0 && 
		     // fix 1."5 miles west"
		     !is_digit(m_wptrs[j-1][0]) &&
		     m_wlens[j-1]==1 && m_wlens[j]==1 ) 
			return true;
		// quote: The Noyes House 2525 "N" Avenue 
		// National
		if (m_wptrs[j][0]=='\"'&&m_wptrs[j][1]==' ' &&
		    m_wlens[j]==2&&
		    // 'closer to 37"' is not a street name!
		    !is_digit(m_wptrs[j-1][0]))
			return true;
		if (m_wptrs[j][0]==' ' &&m_wptrs[j][1]=='\"'&&
		    m_wlens[j]==2) return true;
		// punct mark: st. michael's drive
		if (m_wptrs[j][0]=='\''&&m_wlens[j]==1) return true;
		// mosby's run: utf8 apostrophe
		if (m_wlens[j]==3&&
		    m_wptrs[j][0]==-30 &&
		    m_wptrs[j][1]==-128 &&
		    m_wptrs[j][2]==-103 )
			return true;
		// village of los ranchos growers' market
		if (m_wptrs[j][0]=='\''&&m_wptrs[j][1]==' '&&
		    m_wlens[j]==2) return true;
		// hyphens usually bad, but x-y is ok.
		if(m_wptrs[j][0]=='-'&&m_wlens[j]==1&&j>0&&j+1<m_nw&&
		   m_words->isAlpha(j-1)&&m_words->isAlpha(j+1))return true;
		// i-25 is ok now too
		if (m_wptrs[j][0]=='-'&&j>0&&m_wids[j-1]==h_i&&j+1<m_nw&&
		    is_digit(m_wptrs[j+1][0]) )
			return true;
		// fix "3650-A Hwy 528..."
		//if(m_wptrs[j][0]=='-'&&m_wlens[j]==1&&j==i+1&&
		//   j+1<m_nw&&m_wlens[j+1]==1&&
		//   is_alpha_a(m_wptrs[j+1][0])) return true;
		// "620-624 Central Ave SW." (El Rey) 
		//if ( hasRange &&j==i+1 ) return true;
		// fix for 4909-15 Hawkins NE" for ceder.net
		//if(j+1<m_nw&&
		//   m_wlens[j+1]==2&&is_digit(m_wptrs[j+1][0])&&
		//   m_wlens[j-1]>=4&&is_digit(m_wptrs[j-1][0]) ) {
		//	hasHyphenAddress = true;
		//	return true;
		//}
		// sequence of whitespace is ok
		int32_t k;	for(k=0;k<m_wlens[j];k++)
			if(!is_wspace_a(m_wptrs[j][k])) break;
		if(k==m_wlens[j]) return true;
		// '/' is ok if part of a fraction!
		//if( j == fractionj ) return true;
		// . comma allowed only b4 directional indicatr
		// . "131 Monroe  St, NE"
		// . no because we got a false positive:
		//   "1024 4th street, sw corner..."
		// . ok, this is back again now! BUT... need
		//   to make sure a tag or city name follows it
		// . crap, now we got 
		//   "5305 Gibson, S.E. <b>Albuquerque ..."
		if ( m_wptrs[j][0]!=',' ) return false;
		if ( m_wptrs[j][1]!=' ' ) return false;
		if ( j+3>= m_nw         ) return false;
		char gotDir = 0;
		if ( m_wids[j+1] == h_ne ) gotDir = 2;
		if ( m_wids[j+1] == h_nw ) gotDir = 2;
		if ( m_wids[j+1] == h_se ) gotDir = 2;
		if ( m_wids[j+1] == h_sw ) gotDir = 2;
		if ( m_wids[j+1] == h_n&&m_wids[j+3]==h_e)gotDir=4;
		if ( m_wids[j+1] == h_n&&m_wids[j+3]==h_w)gotDir=4;
		if ( m_wids[j+1] == h_s&&m_wids[j+3]==h_e)gotDir=4;
		if ( m_wids[j+1] == h_s&&m_wids[j+3]==h_w)gotDir=4;
		if ( ! gotDir ) return false;
		// its great if tag follows the dir indicator
		if ( m_tids[j+gotDir] ) return true;
		// or a punct then a tag
		if ( m_tids[j+gotDir+1] ) return true;
		// ok, a cap word must follow
		if ( ! is_upper_utf8 (m_wptrs[j+gotDir+1])) return false;
		// we are good
		return true;
	}


	// skip dates, not allowed in there
	if ( m_bits && (m_bits->m_bits[j] & D_IS_IN_DATE) )
		return false;

	// . otherwise we are alphanumeric
	// . more than 10 is too many for a street
	//if ( alnumsInPhrase++ >= 10 ) return false;

	// stop at "at"
	if ( m_wids[j] == h_at )
		return false;

	// stop at "and"
	if ( m_wids[j] == h_and )
		return false;

	// stop at "between"
	if ( m_wids[j] == h_between )
		return false;
	if ( m_wids[j] == h_btwn )
		return false;
	if ( m_wids[j] == h_bet )
		return false;

	// stop at "location"
	if ( m_wids[j] == h_location )
		return false;

	// stop at "location"
	if ( m_wids[j] == h_intersection )
		return false;

	int64_t postWid = 0LL;
	int32_t maxj = j+15; if ( j > m_nw ) j = m_nw;
	for ( int32_t pi = j + 1 ; pi < maxj ; pi++ ) {
		if ( ! m_wids[pi] ) continue;
		postWid = m_wids[pi];
		break;
	}

	// skip if indicator
	//IndDesc *id=(IndDesc *)g_indicators.getValue(&m_wids[j]);
	//if ( id && (id->m_bit & IND_STREET) ) return true;
	//if ( id && (id->m_bit & IND_DIR   ) ) return true;

	return true;
}

uint64_t getAdm1Bits ( char *stateAbbr ) {
	//if ( stateAbbr[2] ) { char *xx=NULL;*xx=0; }
	uint64_t h64 = hash64Lower_a( stateAbbr , 2 );
	StateDesc **sdp = (StateDesc **)g_states.getValue(&h64);
	//uint16_t *val = (uint16_t *)g_states.getValue ( &h64 );
	// this happens if we have a foreign latlon only address in the contact
	// address tags and we call setFromStr() on that. obviously
	// foreign states will not be in here! so allow this for now and
	// do not core!
	if ( ! sdp ) return 0;
	// get position in the s_states[] array
	int32_t pos = (int32_t)((*sdp) - s_states);
	// that is the shifter
	return (1LL << pos);
}

// . search for all PCLI entries in /geo/allCountries.txt
// . grep out into countries.txt and process into countries.dat
// . remove 
//   "Kingdom of" 
//   "Republic of" 
//   "Democractic Republic of" 
//   "Oriental Republic of" 
//   "* Republic"
//   "United Republic of" 
//   "Socialist Republic of" 
//   "Independent State of"
//   "State of the" (Vatican City)
//   "Federative Replublic of"

/*
SafeBuf g_countryDescBuf;

// . g_countryDescBuf consists of a list of these
// . the hashtablex g_countryTable maps a country name word hash to
//   a CountryDesc pointer
class CountryDesc {
public:
	// country id in one byte
	uint8_t m_crid;
	// two letter, upper case countrycode includes \0
	char *m_countryCode[3];
	// country population, up to 4B
	uint32_t  m_population;
	// centroid
	float m_latitude;
	float m_longitude;
	// box radius i guess
	float m_radius;
	// . ptr into SafeBuf g_countryNameBuf
	// . all the country names with their languages like:
	//   us-fi-nl=egypt,de-es=egypti,...
	// . comma separated
	// . \0 terminated
	char m_nameBufPtr[];
	// . get the name of the country in the designated language
	// . langAbbr is the two letter lang abbreviation (en=english,etc.)
	// . sometimes it can be 3 letters! nds, nrm, ... see
	//   /geo/geonames/iso-languagecodes.txt
	// . sometimes there are names of the place with no associated language
	//   as well, so watch out for that
	char *getCountryName ( char *langAbbr );
};

// . a huge string of all the countries and corresponding data
// . we parse this up into the g_countries table where each slot is a
//   CountryDesc and CountryDesc::m_nameBufPtr references into g_countryData.
// . we need to know the language of each spelling of the country name
//   so we can display that name if someone's browser says they only know
//   Spanish or something, we'd say Estados instead of States or whatever.
// . well the alternateNames.txt file has the alternate names of each 
//   city or country or state and the language it is from, so use that...
// . make a name list like "en=Egypt" to indicate its called Egypt in english
// . cs.en.nb.nn.sk=Egypt,fy.nl=Egypte,fi=Egypti
char *g_countryData = "";

HashTableX g_countries;

bool setCountryTable ( ) {
	return true;
}

// access g_countries table to find it
CountryDesc *getCountryDesc ( int64_t wid ) {
	return NULL;
}

// two letter country code
CountryDesc *getCountryDesc ( char *countryCode ) {
	int64_t wid = hash64Lower_a ( countryCode , 2 );
	return getCountryDesc ( wid );
}

Place *getCountryPlace ( int32_t a , int32_t alnumPos , Words *words ) {
	return NULL;
}
*/

StateDesc *getStateDesc ( char *stateAbbr ) {
	uint64_t h64 = hash64Lower_a( stateAbbr , 2 );
	StateDesc **sdp = (StateDesc **)g_states.getValue(&h64);
	if ( ! sdp ) return NULL;
	return *sdp;
}

StateDesc *getStateDescByNum ( int32_t i ) {
	// sto breach;
	if ( i >= (int32_t)sizeof(s_states)/ (int32_t)sizeof(StateDesc)) return NULL;
	if ( i < 0 ) return NULL;
	return &s_states[i];
}


inline int32_t getStateOffset ( int64_t *h ) {
	StateDesc **sdp = (StateDesc **)g_states.getValue(h);
	if ( ! sdp ) return -1;
	// return the POSITION though
	return (int32_t)((*sdp) - s_states);
}

// from hash of state
uint64_t getStateBitFromHash ( int64_t *h ) {
	int32_t pos = getStateOffset ( h );
	if ( pos < 0 ) return 0;
	return (1LL << pos);
}

StateDesc *getStateDescFromBits ( uint64_t bit ) {
	int32_t size = sizeof(s_states);
	// item count
	int32_t n = (int32_t)size/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get it
		StateDesc *sd = &s_states[i];
		// check bits
		if ( (((uint64_t)1LL)<<i) == bit ) return sd;
	}
	// sanity check
	char *xx=NULL;*xx=0; 
	return NULL;
}

char *getStateAbbr ( uint64_t bit ) {
	// clear the unique bit
	//bit &= ~ CF_UNIQUE;
	// use this for speed
	int32_t pos = getBitPosLL((uint8_t *)&bit);
	// must be there
	return s_states[pos].m_adm1;
}

int64_t getWordXorHash2 ( char *s , int32_t slen ) {
	// tmp save
	char c = s[slen];
	s[slen] = '\0';
	int64_t h = getWordXorHash(s);
	// put back
	s[slen] = c;
	return h;
}

int64_t getWordXorHash ( char *s ) {
	Words tmp;
	tmp.set9 ( s , 0 );
	int64_t *wids = tmp.m_wordIds;
	uint64_t h = 0LL;
	for ( int32_t i = 0 ; i < tmp.m_numWords ; i++ ) {
		if ( !wids[i] ) continue;
		// make it
		h <<= 1LL;
		h ^= wids[i];
	}
	return h;
}


#include "GeoIP.h"
#include "GeoIPCity.h"

static const char * _mk_NA( const char * p ){
 return p ? p : "N/A";
}

// try "geolite city" free software
// mwells@titan:~/tmp2/GeoIP-1.4.6/apps$ geoiplookup -f GeoLiteCity.dat 67.16.94.2
// GeoIP City Edition, Rev 1: US, NM, Albuquerque, N/A, 35.102501, -106.611702, 505
// i guess i can just include that library in the gb source
// . i would say just trace the code and just grab the code we need
//   and re-code into gb. BUT do indeed keep the GeoLiteCity.dat file
//   that is only 28MB so we should load it up at start time
// . put our api code into here down below
bool getIPLocation ( int32_t    ip     ,
		     double  *lat    ,
		     double  *lon    ,
		     double  *radius ,
		     char  **city  ,
		     char  **state ,
		     char  **ctry  ,
		     char   *buf   ,
		     int32_t    bufSize ) {

	//static int    s_i  = 0;

	// assume none
	*city  = NULL;
	*state = NULL;
	if ( ctry ) *ctry  = NULL;

	static GeoIP *s_gi = NULL;

	char *sip = (char *)&ip;
	// if ip is local use abq, nm
	if ( sip[0]==10 ||
	     // 192.168.x.x is local
	     (sip[0]==(char)192 && sip[1]==(char)168) ||
	     // 127.0.0.1
	     ip==(int32_t)16777343 ) {
		char *p = buf;
		*city = p;
		p += sprintf ( p , "Albuquerque" );
		*p++ = '\0';
		*state = p;
		p += sprintf ( p , "NM" );
		*p++ = '\0';
		*ctry = p;
		p += sprintf ( p , "US" );		
		// use this
		*lat = 35.10438;
		*lon = -106.6270;
		return true;
	}


	if ( ! s_gi ) {
		// make full pathc
		char full[1024];
		sprintf(full,"%s%s",g_hostdb.m_dir,"GeoLiteCity.dat");
		s_gi = GeoIP_open(full, GEOIP_STANDARD); 
		if ( ! s_gi ) {
			log("gb: could not open %s",full);
			return false;
		}
		//s_i  = GeoIP_database_edition(s_gi);
	}

	// geoiplookup(gi,hostname,i);

	//char hostname[64];
	//sprintf(hostname,"%s",iptoa(ip));
	//geoiplookup(gi,hostname,i);
	//uint32_t ipnum = GeoIP_lokupaddress(hostname);

	// put in network byte order, host to network
	int32_t ipnum = htonl ( ip );
	// temp
	//ipnum = ip;

	GeoIPRecord *gir = GeoIP_record_by_ipnum(s_gi, ipnum);

	// false if not found
	if ( ! gir ) return false;

	log("geoip: "
	    //"%s: %s, %s, %s, %s, %f, %f, %d", 
	    "%s, %s, %s, %s, %f, %f, %d", 
	    //GeoIPDBDescription[(uint32_t)s_gi->databaseType], 
	    gir->country_code, 
	    _mk_NA(gir->region), 
	    _mk_NA(gir->city), 
	    _mk_NA(gir->postal_code),
	    // %d
	    gir->latitude, 
	    gir->longitude, //gir->metro_code,
	    gir->area_code);

	// transfer
	if ( lat ) *lat = gir->latitude;
	if ( lon ) *lon = gir->longitude;
	// express 20 miles in degrees... one degree is 69 miles
	if ( radius ) *radius = 20.0 / 69.0;

	// city and state
	char *p = buf;
	int32_t len ;

	// bogus?
	if ( ! gir->country_code ) return false;

	if ( ctry ) *ctry = p;

	//len = gbstrlen(gir->country_code);
	//gbmemcpy ( p , gir->country_code , len + 1 );
	p[0] = gir->country_code[0];
	p[1] = gir->country_code[1];
	p += 2;
	*p++ = '\0';

	*state = p;
	len = 0;
	if ( gir->region ) len = gbstrlen(gir->region);
	// bogus?
	if ( len == 0 ) return false;
	//gbmemcpy ( p , gir->region , len + 1 );
	// make it all lowercase so we don't core anywhere
	int32_t written = to_lower_alnum_a(gir->region,len,p);
	// sanity
	if ( written != len ) { char *xx=NULL;*xx=0; }
	// skip over what we stored
	p += len ;
	// null term
	*p++ = '\0';
	// get len
	//int32_t plen = gbstrlen(p);
	//p += len + 1;

	*city = p;
	len = 0;
	if ( gir->city ) len = gbstrlen(gir->city);
	// bogus?
	if ( len == 0 ) return false;
	gbmemcpy ( p , gir->city , len );
	p += len;
	*p++ = '\0';

	// sanbity check
	if ( p - buf > bufSize ) { char *xx=NULL;*xx=0; }

	// free this junk too!
	GeoIPRecord_delete ( gir );

	//free ( gir );

	//GeoIP_delete(gi);

	return true;
}

bool getLatLon ( uint32_t cityId , double *lat , double *lon ) {
	// now lookup timezone
	int32_t slot = g_timeZones.getSlot ( &cityId );
	// return 0 if not found
	if ( slot < 0 ) return false;
	// otherwise, set m_timeZoneOffset appropriately
	CityStateDesc *csd;
	csd = (CityStateDesc *)g_timeZones.getValueFromSlot(slot);
	*lat = csd->m_latitude;
	*lon = csd->m_longitude;
	return true;
}

// or numeric lat/lon
float getLatLonSpecial ( char *p , 
			 char *bufStart, 
			 char *bufEnd , 
			 char *found ) {
	// assume none
	*found = 0;
	// must start with digit
	if ( ! is_digit(*p) ) return 0.0;
	// set start
	char *start = p;
	// negative sign?
	if ( p>bufStart && p[-1] == '-' ) start--;
	// reset counts
	int32_t digitCount = 0;
	int32_t decimalCount = 0;
	// do not scan so far
	char *pmax = p + 20;
	if ( pmax > bufEnd ) pmax = bufEnd;
	// scan until no digit or period
	for ( ; p < pmax ; p++ ) {
		// count the digits
		if ( is_digit(*p) ) {
			digitCount++;
			continue;
		}
		// decimal point is ok
		if ( *p == '.' ) {
			decimalCount++;
			continue;
		}
		// stop on other crap
		break;
	}
	//  give up if less than 3 digits encountered
	if ( digitCount <  3 ) return 0.0;
	// some pages have no period in it
	// and we just have to assume the first
	// 3 digits are before the period. like for
	// switchboard.com urls
	if ( decimalCount >= 2 ) return 0.0;
	// convert
	double dval = atod2(start,p-start);
	// fix switchboard.com stuff which has no decimal pt
	if ( decimalCount == 0 ) {
		// how many digits to left of decimal
		int32_t left = 3;
		// make a divisor
		double ddd = 1;
		for ( int32_t vv = 0 ; vv<digitCount-left; vv++)
			ddd *= 10;
		// fix it
		dval /= ddd;
	}
	// bail if bad
	if ( dval < -180.0 || dval > 180.0 ) return 0.0;
	// in the usual decimal it is
	// lat from  24.450000 to   60 (juneau alask) // 47.4666666
	// lon from -71.083333 to -114.1333333
	//char type = 0;
	//if      ( dval >=  24.45 && dval <=  60.0 ) type = 1; // lat
	//else if ( dval >= -140.0 && dval <= -66.1 ) type = 2; // lon
	//else log("query: lat/lon point not in our scope. fix!");
	//if ( type == 0 ) return 0.0;

	*found = 1;//type;
	return dval;
}

// TEST SCRIPT:
static char *s_tests[] = {
	"sf",
	"sf ca",
	"sf nm",
	"ottawa ontario",
	"rio de janeiro",
	"mexico city",
	// pasadena texas is more popular than california!
	"pasadena",
	"berlin",
	"berlin, germany",
	"paris",
	"paris, tx",
	"paris, ky",
	"paris, france",
	"homestead",
	"key west",
	"santa fe",
	"san francisco",
	"poland",
	"germany",
	"georgia", // the country!!
	"nm",
	"texas",
	"mass",
	"d.c.",
	"washington",// (should be the state)
	"washington d.c.",
	"washington dc",
	"kentucky",
	"mexico",
	"tokyo",
	"philippines",
	"usa",
	"united states of america",
	"georgia", // (should be the US state, not the country!)
	"87109",
	"90210",
	"taste of germany",
	"kimo theater", // (venue name)
	"pleasant arena", // (venue name)
	"barton road"//  (street name test)
};




bool printTesterPage ( SafeBuf &sb ) {

	sb.safePrintf("<table>");

	int32_t count = 0;
	int32_t n = sizeof(s_tests)/sizeof(char *);
	bool firstRow = true;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		if ( count %4 == 0 ) {
			if ( ! firstRow )
				sb.safePrintf("</tr>");
			firstRow = false;
			sb.safePrintf("<tr>");
		}
		count++;
		sb.safePrintf("<td>");
		// print map
		int32_t width = 200;
		int32_t height = 200;
		// get stuff
		float radius;
		char *where = s_tests[i];
		float cityLat;
		float cityLon;
		float stateLat;
		float stateLon;
		float countryLat;
		float countryLon;
		float zipLat;
		float zipLon;
		float userLat;
		float userLon;
		char timeZone2;
		char useDST;
		uint8_t ipCrid = CRID_US;
		char gbwhereBuf[512];
		int32_t gbwhereBufSize = 500;
		getLatLonFromUserInput ( &radius,
					 where ,
					 &cityLat ,
					 &cityLon ,
					 &stateLat,
					 &stateLon,
					 &countryLat,
					 &countryLon,
					 &zipLat ,
					 &zipLon ,
					 &userLat ,
					 &userLon ,
					 NULL,
					 NULL,
					 NULL,
					 &timeZone2 ,
					 &useDST,
					 ipCrid,
					 gbwhereBuf ,
					 gbwhereBufSize ) ;
		// get most accurate lat/lon
		float lat = NO_LATITUDE;
		float lon = NO_LONGITUDE;
		int32_t zoom = 0; // world
		if ( countryLat != NO_LATITUDE && countryLon != NO_LONGITUDE) {
			lat = countryLat;
			lon = countryLon;
			zoom = 3; // country?
		}
		if ( stateLat != NO_LATITUDE && stateLon != NO_LONGITUDE ) {
			lat = stateLat;
			lon = stateLon;
			zoom = 5; // state?
		}
		if ( cityLat != NO_LATITUDE &&  cityLon != NO_LONGITUDE ) {
			lat = cityLat;
			lon = cityLon;
			zoom = 7; // city?
		}
		if ( zipLat != NO_LATITUDE &&  zipLon != NO_LONGITUDE ) {
			lat = zipLat;
			lon = zipLon;
			zoom = 8; // zip?
		}
		if ( userLat != NO_LATITUDE && userLon != NO_LONGITUDE ) {
			lat = userLat;
			lon = userLon;
			zoom = 8;
		}
	
		sb.safePrintf ( "<img src=\""
				"http://maps.google.com/maps/api/staticmap?"
				"size=%"INT32"x%"INT32"&maptype=roadmap&sensor=false" ,
				width, height );
		sb.safePrintf("&zoom=%"INT32""
			      "&markers="
			      "size:medium"
			      "%%7Ccolor:%s"
			      "%%7Clabel:%c" // letter
			      "%%7C%.07f" // lat
			      "%%2C%.07f" //lon
			      ,zoom
			      ,"red" // s_mapColors[0]
			      ,'A'
			      ,lat 
			      ,lon );
		
		sb.safePrintf("\"><br>%s",s_tests[i]);
		sb.safePrintf("</td>");
	}

	sb.safePrintf("</tr></table>");

	return true;
}


//
// TODO: maybe just print out like 20 google maps for these on a page tester?

// . returns false if we could not identify a lat/lon from "where" string
// . returns false and sets g_errno on error
// . stores words NOT used for lat/lon determination into gbwhereBuf each
//   word with a "gbwhere:" prefix so we can append gbwhereBuf to the query.
// . if input is just a state like new mexico, then uses gbwhere:"new mexico"
//   otherwise it could be referring to a street called New Mexico Avenue...
// . you pass in the radius SearchInput::m_radius as "radius" and we may
//   change it here! if its 0 and we find a lat/lon in the "where" string
//   then we will change it to 100. if the *radius you pass in is non-zero
//   we may change it to zero if we can't find a lat/lon...
bool getLatLonFromUserInput ( float  *radius,
			      char   *where ,
			      float  *cityLat ,
			      float  *cityLon ,
			      float *stateLat,
			      float *stateLon,
			      float *countryLat,
			      float *countryLon,
			      //double *radius ,
			      // . position of the user
			      // . we try to set these from the zipcode if ther
			      float  *zipLat ,
			      float  *zipLon ,
			      float  *userLat ,
			      float  *userLon ,
			      PlaceDesc **retCityDesc,
			      PlaceDesc **retStateDesc,
			      PlaceDesc **retCountryDesc,
			      char   *timeZone2 ,
			      char   *useDST,
			      // country of search based on ip (two letters)
			      uint8_t ipCrid,
			      char   *gbwhereBuf ,
			      int32_t    gbwhereBufSize ) {

	// convert "where" string into a cityId32 so we can convert
	// to a lat/lon by calling getLatLon(cityId)

	g_errno = 0;

	Words w;
	if ( ! w.set3 ( where ) ) return false;




	// express 20 miles in degrees... one degree is 69 miles
	//*radius = 20.0 / 69.0;

	// start at -1
	int32_t alnumPos = -1;

	//char    *adm1Str = NULL;

	int32_t cityA  = -1;
	int32_t cityB  = -1;
	int32_t stateA = -1;
	int32_t stateB = -1;
	int32_t zipA   = -1;
	int32_t zipB   = -1;
	int32_t countryA = -1;
	int32_t countryB = -1;

	int32_t cityAlnumA  = -1;
	int32_t cityAlnumB  = -1;
	int32_t stateAlnumA = -1;
	int32_t stateAlnumB = -1;
	int32_t zipAlnumA   = -1;
	int32_t zipAlnumB   = -1;
	int32_t countryAlnumA = -1;
	int32_t countryAlnumB = -1;

	int32_t finalCityA  = -1;
	int32_t finalCityB  = -1;
	int32_t finalStateA = -1;
	int32_t finalStateB = -1;
	int32_t finalCountryA = -1;
	int32_t finalCountryB = -1;
	int32_t finalZipA  = -1;
	int32_t finalZipB  = -1;

	// int16_tcuts
	int64_t *wids = w.getWordIds();
	char **wptrs = w.getWords();
	int32_t  *wlens = w.getWordLens();

	// set lastWidPos
	int64_t lastWidPos = w.m_numWords;
	for ( int32_t i = 0 ; i < w.m_numWords ; i++ )
		if ( wids[i] ) lastWidPos = i;

	char *bufStart = where;
	char *bufEnd   = where + gbstrlen(where);

	// reset all
	*userLat = NO_LATITUDE;
	*userLon = NO_LONGITUDE;
	*cityLat = NO_LATITUDE;
	*cityLon = NO_LONGITUDE;
	*stateLat = NO_LATITUDE;
	*stateLon = NO_LONGITUDE;
	*countryLat = NO_LATITUDE;
	*countryLon = NO_LONGITUDE;
	*zipLat  = NO_LATITUDE;
	*zipLon  = NO_LONGITUDE;

	//int32_t totalAlnums = w.getNumAlnumWords ();

	// for numeric entries like 58.xxxx -128.yyyy
	bool hasLat = false;
	bool hasLon = false;

	int32_t ignoreUntil;


	// do a initial loop looking for the country to use, otherwise,
	// we'll assume ipcrid. once we establish a country it will be
	// easier to know what state or city is being talked about.
	alnumPos = -1;
	ignoreUntil = -1;
	PlaceDesc *finalCountryDesc = NULL;
	for ( int32_t i = 0 ; i < w.m_numWords ; i++ ) {
		// skip if punct
		if ( ! wids[i] ) continue;
		// alnum pos count
		alnumPos++;
		// fix "united states of america"
		if ( i < ignoreUntil ) continue;
		// country names are unique, so we can set this here
		PlaceDesc *crd = NULL;
		// get the last non-null country in the where box
		getLongestPlaceName_new   ( i,
					    0 , // alnumPos,
					    &w,
					    PDF_COUNTRY,
					    CRID_ANY,
					    NULL, // state abbr
					    NULL,//&countryHash64,
					    &countryAlnumA,
					    &countryAlnumB,
					    &countryA,
					    &countryB ,
					    &crd );
		// record last one
		if ( crd ) {
			finalCountryDesc = crd;
			finalCountryA = countryA;
			finalCountryB = countryB;
			ignoreUntil = countryB;
		}
	}


	// assume country based on searcher's IP address
	uint8_t crid = ipCrid;
	// unless a country was specified in the wherebox, then use that
	if ( finalCountryDesc ) crid = finalCountryDesc->m_crid;

	// do a secondary loop looking for the state before the country
	// or picking the last encountered state. ignore any country we
	// might have found in the first loop. require state be in that
	// country.
	alnumPos = -1;
	ignoreUntil = -1;
	PlaceDesc *finalStateDesc = NULL;
	for ( int32_t i = 0 ; i < w.m_numWords ; i++ ) {
		// skip if punct
		if ( ! wids[i] ) continue;
		// alnum pos count
		alnumPos++;
		// skip if already in use by us
		if ( i < ignoreUntil ) continue;
		// skip if its like a lat/lon
		if ( i+2<w.m_numWords && 
		     is_digit(wptrs[i][0]) &&
		     wptrs[i][wlens[i]] == '.' &&
		     is_digit(wptrs[i][wlens[i]+1]) )
			continue;
		// skip of country words
		//if ( i >= finalCountryA && i < finalCountryB ) continue;
		// country names are unique, so we can set this here
		PlaceDesc *srd = NULL;
		// use this country id (CRID_ANY = 0)
		uint8_t useCrid = CRID_ANY;
		// come back up here with a non-zero crid
	redo:
		// . don't use the countryid to fix "new mexico"...
		// . picks the most popular in case of ties
		getLongestPlaceName_new   ( i,
					    alnumPos,
					    &w,
					    PDF_STATE,
					    useCrid,
					    NULL, // state abbr
					    NULL,//&stateHash64,
					    &stateAlnumA,
					    &stateAlnumB,
					    &stateA,
					    &stateB ,
					    &srd );

		// if that does not overlap the country we had then
		// re-do it using the country id!!!
		if ( srd && 
		     useCrid == CRID_ANY &&
		     finalCountryDesc &&
		     stateB <= finalCountryA ) {
			useCrid = finalCountryDesc->m_crid;
			goto redo;
		}


		// if it is exact overlap and same country... prefer 
		// the state. try to fix 'georgia' which is a state and country
		if ( srd && 
		     useCrid == CRID_ANY &&
		     finalCountryDesc &&
		     stateA == finalCountryA &&
		     stateB == finalCountryB &&
		     finalCountryDesc->m_crid == srd->m_crid &&
		     // if in 'mexico' searching for 'mexico' assume the
		     // state, and nuke the country...
		     ipCrid == srd->m_crid ) {
			ignoreUntil = stateB;
			finalCountryDesc = NULL;
			finalCountryA = -1;
			finalCountryB = -1;
			crid = ipCrid;
		}

		// otherwise, if NOT in 'mexico' searching for 'mexico'
		// assume the country, not the state in mexico
		if ( srd && 
		     useCrid == CRID_ANY &&
		     finalCountryDesc &&
		     stateA == finalCountryA &&
		     stateB == finalCountryB &&
		     finalCountryDesc->m_crid == srd->m_crid &&
		     // if in 'mexico' searching for 'mexico' assume the
		     // state, and nuke the country...
		     ipCrid != srd->m_crid ) {
			ignoreUntil = finalCountryB;
			srd = NULL;
		}


		// if it is exact overlap and different countries,
		// prefer one that is "crid", the same country as the user!
		// try to fix 'georgia' which is a state and country...
		// in the US we expect georgia the state.
		if ( srd && 
		     useCrid == CRID_ANY &&
		     finalCountryDesc &&
		     stateA == finalCountryA &&
		     stateB == finalCountryB &&
		     finalCountryDesc->m_crid == ipCrid &&
		     srd->m_crid != ipCrid ) {
			ignoreUntil = stateB;
			srd = NULL;
		}

		// if the state is in the user's country but the country
		// is not the user's country. kill the country descriptor.
		// so 'georgia' in the US will match the state, not
		// 'georgia' the country.
		if ( srd && 
		     useCrid == CRID_ANY &&
		     finalCountryDesc &&
		     stateA == finalCountryA &&
		     stateB == finalCountryB &&
		     finalCountryDesc->m_crid != ipCrid &&
		     srd->m_crid == ipCrid ) {
			ignoreUntil = stateB;
			finalCountryDesc = NULL;
			finalCountryA = -1;
			finalCountryB = -1;
			crid = ipCrid;
		}


		// if it does overlap the country, nuke the country then
		// to fix 'new mexico' so country is not 'mexico'
		if ( srd && 
		     useCrid == CRID_ANY &&
		     finalCountryDesc &&
		     stateB > finalCountryA ) {
			finalCountryDesc = NULL;
			finalCountryA = -1;
			finalCountryB = -1;
			crid = ipCrid;
		}

		// get the last non-null state
		if ( srd ) {
			finalStateDesc = srd;
			finalStateA = stateA;
			finalStateB = stateB;
			ignoreUntil = stateB;
		}
	}

	// do a third loop looking for the city. ignore any state or country
	// we found in the first two loops. require city be in an state or
	// country we found in the first two loops.
	alnumPos = -1;
	ignoreUntil = -1;
	PlaceDesc *finalCityDesc = NULL;
	for ( int32_t i = 0 ; i < w.m_numWords ; i++ ) {
		// skip if punct
		if ( ! wids[i] ) continue;
		// alnum pos count
		alnumPos++;
		// skip if already in use by us
		if ( i < ignoreUntil ) continue;
		// skip of country words
		// no, was hurting "mexico city" because "mexico" was
		// our country and should have been the city!
		//if ( i >= finalCountryA && i < finalCountryB ) continue;
		// . skip over the state
		// . no, for 'santa fe' it was a state, but we need
		//   to comment this line out to contest that.
		//if ( i >= finalStateA && i < finalStateB ) continue;
		// state abbr?
		char *stateAbbr = NULL;
		//if ( finalStateDesc ) stateAbbr = finalStateDesc->m_adm1;
	redoCity:
		// country names are unique, so we can set this here
		PlaceDesc *crd1 = NULL;
		// picks the most popular in case of ties
		getLongestPlaceName_new   ( i,
					    alnumPos,
					    &w,
					    PDF_CITY,
					    crid,
					    stateAbbr,
					    NULL,//&cityHash64,
					    &cityAlnumA,
					    &cityAlnumB,
					    &cityA,
					    &cityB ,
					    &crd1 );

		// if none found, try not restricting to searcher's
		// country then!!! should fix 'tokyo' since there is no
		// 'tokyo' city in the US at all.
		// crap, then this gets georgia this city in jamaica
		PlaceDesc *crd2 = NULL;
		int32_t city2A;
		int32_t city2B;
		int32_t city2AlnumA;
		int32_t city2AlnumB;
		getLongestPlaceName_new   ( i,
					    alnumPos,
					    &w,
					    PDF_CITY,
					    CRID_ANY,//crid,
					    stateAbbr,
					    NULL,//&cityHash64,
					    &city2AlnumA,
					    &city2AlnumB,
					    &city2A,
					    &city2B ,
					    &crd2 );

		// default to city in user's country
		PlaceDesc *crd = crd1;

		// use the worldly city if the local city name does not
		// exist in the user's country.
		if ( ! crd ) {
			crd = crd2;
			cityA = city2A;
			cityB = city2B;
		}
		// if both existed, prefer the longer. if tied. prefer
		// the local one even if its population might be smaller
		if ( crd && crd2 && city2B > cityB ) {
			crd = crd2;
			cityA = city2A;
			cityB = city2B;
		}

		// if city does NOT overlap the state re-do it using the 
		// stateAbbr. constrain to that state then...
		if ( crd && 
		     ! stateAbbr &&
		     finalStateDesc &&
		     cityB <= finalStateA ) {
			stateAbbr = finalStateDesc->m_adm1;
			goto redoCity;
		}

		// if it more than contains the country... nuke the country
		// fixes "mexico city" where it thinks "mexico" is the country
		if ( crd && 
		     ! stateAbbr &&
		     finalCountryDesc &&
		     cityA == finalCountryA &&
		     cityB  > finalCountryB ) {
			ignoreUntil = cityB;
			finalCountryDesc = NULL;
			finalCountryA = -1;
			finalCountryB = -1;
			crid = ipCrid;
		}
		
		// do not intersect with country otherwise beyond this point
		if ( i >= finalCountryA && i < finalCountryB ) continue;

		// if it is exact overlap and same country... prefer state!
		if ( crd && 
		     ! stateAbbr &&
		     finalStateDesc &&
		     cityA == finalStateA &&
		     cityB == finalStateB &&
		     // the state must be in different country now to
		     // fix the 'kentucky' query so we do not get
		     // 'kentucky, arkansas'
		     finalStateDesc->m_crid == crd->m_crid ) {
			ignoreUntil = cityB;
			crd = NULL;
		}

		// if it equals the state, and we already had a finalCity
		// then toss that city... it's most likely a city/state
		// combo where the state is a city name somewhere as well!
		// fixes 'ottawa, ontario' where it ontario is also a city
		// in the US!
		if ( crd &&
		     ! stateAbbr &&
		     finalStateDesc &&
		     finalCityDesc &&
		     cityA == finalStateA ) {
			ignoreUntil = finalStateB;
			crd = NULL;
			continue;
		}

		// if it is exacvt overlap and different countries,
		// prefer one that is "crid"
		if ( crd && 
		     ! stateAbbr &&
		     finalStateDesc &&
		     cityA == finalStateA &&
		     cityB == finalStateB &&
		     // the state must be in different country now to
		     // fix the 'kentucky' query so we do not get
		     // 'kentucky, arkansas'
		     finalStateDesc->m_crid == crid ) {
			ignoreUntil = cityB;
			crd = NULL;
		}

		// if exact overlap and city is in the user's country,
		// then prefer city and nuke state
		if ( crd && 
		     ! stateAbbr &&
		     finalStateDesc &&
		     cityA == finalStateA &&
		     cityB == finalStateB &&
		     // the state must be in different country now to
		     // fix the 'kentucky' query so we do not get
		     // 'kentucky, arkansas'
		     finalStateDesc->m_crid == crid ) {
			ignoreUntil = cityB;
			finalStateDesc = NULL;
			finalStateA = -1;
			finalStateB = -1;
		}



		// if it does overlap the state, nuke the state then
		// to fix 'key west' query. it thought 'west' was a
		// state in iceland!
		if ( crd && 
		     ! stateAbbr &&
		     finalStateDesc &&
		     cityB > finalStateA &&
		     // the state must be in different country now to
		     // fix the 'kentucky' query so we do not get
		     // 'kentucky, arkansas'
		     finalStateDesc->m_crid != crd->m_crid &&
		     // i added this so 'georgia' the city in jamaica
		     // did not beat out the state in the US...
		     crd->m_crid == crid ) {
			ignoreUntil = cityB;
			finalStateDesc = NULL;
			finalStateA = -1;
			finalStateB = -1;
		}

		// BUT kill the city if its the one in a different state
		if ( crd && 
		     ! stateAbbr &&
		     finalStateDesc &&
		     cityB > finalStateA &&
		     finalStateDesc->m_crid != crd->m_crid &&
		     finalStateDesc->m_crid == crid ) {
			ignoreUntil = finalStateB;
			crd = NULL;
		}
		

		// get the last non-null city
		if ( crd ) {
			finalCityDesc = crd;
			finalCityA = cityA;
			finalCityB = cityB;
			ignoreUntil = cityB;
		}
	}

	// and a 4th loop to get the zip code
	alnumPos = -1;
	for ( int32_t i = 0 ; i < w.m_numWords ; i++ ) {
		// skip if punct
		if ( ! wids[i] ) continue;
		// alnum pos count
		alnumPos++;
		// skip of country words
		if ( i >= finalCountryA && i < finalCountryB ) continue;
		// skip over the state
		if ( i >= finalStateA && i < finalStateB ) continue;
		// skip over city
		if ( i >= finalCityA && i < finalCityB ) continue;
		// we must be in the US
		//if ( crid != CRID_US ) continue;
		// U.S. only for now
		getZip_new ( i,
			     alnumPos,
			     &w,
			     NULL,//&zipHash64,
			     NULL,//&zipCityHash64,
			     NULL,//&zipStateHash64,
			     &zipAlnumA,
			     &zipAlnumB,
			     &zipA,
			     &zipB ,
			     zipLat,
			     zipLon);
		// skip if none
		if ( *zipLat != NO_LATITUDE ) {
			// set these i guess
			finalZipA = zipA;
			finalZipB = zipB;
		}
	}

	// loop for numeric lat/lon
	alnumPos = -1;
	ignoreUntil = -1;
	for ( int32_t i = 0 ; i < w.m_numWords ; i++ ) {
		// skip if punct
		if ( ! wids[i] ) continue;
		// ignore
		if ( i < ignoreUntil ) continue;
		// stop if we had any of the above though!
		//if ( finalCityDesc    ) break;
		//if ( finalStateDesc   ) break;
		//if ( finalCountryDesc ) break;
		//if ( zipA >= 0        ) break;
		// alnum pos count
		alnumPos++;
		char found = 0;
		float ret = getLatLonSpecial(wptrs[i],
					     bufStart,
					     bufEnd,
					     &found);
		if ( found && !hasLat ) { // == 1
			*userLat = ret;
			ignoreUntil = i + 3;
			// the next one should be the lon
			hasLat = true;
			continue;
		}
		if ( found && !hasLon ) { // == 2
			*userLon = ret;
			ignoreUntil = i + 3;
			hasLon = true;
			continue;
		}
		if ( found ) {
			log("query: got extra lat/lon term! ignoring.");
			ignoreUntil = i + 3;
			continue;
		}
		// ok, a random gbwhere: term i guess
	}

	// if we had a lat/lon toss all else out. should fix location of
	// "33.83660 -116.54670" which thought the 83660 was a french city.
	if ( hasLat && hasLon ) {
		finalCityDesc    = NULL;
		finalStateDesc   = NULL;
		finalCountryDesc = NULL;
		// nuke other lons/lats too
		*cityLat = NO_LATITUDE;
		*cityLon = NO_LONGITUDE;
		*stateLat = NO_LATITUDE;
		*stateLon = NO_LONGITUDE;
		*countryLat = NO_LATITUDE;
		*countryLon = NO_LONGITUDE;
		*zipLat  = NO_LATITUDE;
		*zipLon  = NO_LONGITUDE;
	}


	/*
	// . if we got a lat and a lon convert...
	// . this was in pageevents.cpp
	if ( hasLat && hasLon ) {
		float distInMilesSquared;
		PlaceDesc *pd;
		pd = getNearestCity_new ( lat ,lon,0, &distInMilesSquared);
		if ( distInMilesSquared < 1000 ) {
			finalCityDesc = pd;
			finalStateDesc = 
	*/

	int32_t nw = w.getNumWords();
	// was it just a city name by itself?
	bool onlyCity = ( ( finalCityA == 0  || finalCityA == 1    ) &&
			  ( finalCityB == nw || finalCityB == nw-1 )  );

	// but if only a city and the city name is also a street indicator
	// then cancel it? that way if they put 'avenue' in the where box
	// they do not get 'avenue, maryland' city.
	// they should! and this messed up 'homestead' in florida.
	//if ( onlyCity && finalCityB == finalCityA+1 ) {
	//	IndDesc *id = (IndDesc *)g_indicators.getValue(&wids[
	//						       finalCityA]);
	//	if ( id ) onlyCity = false;
	//}

	// . if only a city name, nuke it if no state
	// . otherwise if we enter 'avenue' into the where box it thinks
	//   its "Avenue, Maryland"
	// . but if the whole thing is just this city, then let it fly...
	if (   finalCityDesc &&
	       // no country
	     ! finalCountryDesc &&
	       // no state
	     ! finalStateDesc &&
	       // not just city
	     ! onlyCity && 
	       // no zip..
	       finalZipA < 0 ) {
		// do not lookup lat/lon...
		finalCityDesc = NULL;
		// nuke these
		finalCityA  = -1;
		finalCityB  = -1;
		finalStateA = -1;
		finalStateB = -1;
	}

	// use userlat/lon to make the bounding box. this is usually the
	// city centroid otherwise.
	if ( *userLat != 999.0 && *userLon != 999.0 )
		// uses getNearestCityId() ... need to update to use
		// our new foreign cities...? really only need to add them
		// if they do not use dst i guess...?
		*timeZone2=getTimeZoneFromLatLon(*userLat, *userLon,0,useDST);

	// this is true if we had a city with a lat/lon
	//bool status = false;

	if ( finalCityDesc ) {
		// this is easy...
		*timeZone2 = finalCityDesc->m_timeZoneOffset;
		*useDST    = false;
		if ( finalCityDesc->m_flags & PDF_USE_DST ) *useDST = true;
		//status = true;
		*cityLat = finalCityDesc->m_lat;
		*cityLon = finalCityDesc->m_lon;
	}

	if ( finalStateDesc ) {
		*stateLat = finalStateDesc->m_lat;
		*stateLon = finalStateDesc->m_lon;
	}

	if ( finalCountryDesc ) {
		*countryLat = finalCountryDesc->m_lat;
		*countryLon = finalCountryDesc->m_lon;
	}

	// did we get a lat/lon from the "where" string?
	bool hasCentroid = false;
	if ( *cityLat    != NO_LATITUDE ) hasCentroid = true;
	if ( *zipLat     != NO_LATITUDE ) hasCentroid = true;
	if ( *userLat    != NO_LATITUDE ) hasCentroid = true;
	// if we got a cityLat or zipLat or userLat and
	// radius is zero then we gotta make it default to 100
	if ( *radius == 0 && hasCentroid ) *radius = 100;
	// if no centroid...
	if ( *radius && ! hasCentroid ) *radius = 0;
	// bitch if no centroid
	if ( ! hasCentroid && w.m_numWords )
		log("query: no centroid for location in wherebox");


	if ( *userLat != NO_LATITUDE ) 
		return true;

	// reset
	alnumPos = -1;
	ignoreUntil = -1;
	// set the gbwherebuf if provided
	char *p    =     gbwhereBuf;
	char *pend = p + gbwhereBufSize - 1; // room for \0
	bool  gotStuff = false;
	bool  firstOne = true;
	for ( int32_t i = 0 ; p && i < w.m_numWords ; i++ ) {
		// count it?
		if ( wids[i] ) alnumPos++;
		// skip punct
		if ( ! wids[i] ) continue;
		// skip if in middle of state or city name
		if ( i < ignoreUntil ) continue;
		// if we had a valid city/state/zip, do not include those
		// in this buffer
		if ( //status &&
		    ( 
		     //(i>= finalCountryA  && i <finalCountryB ) ||
		     //(i>= finalStateA && i <finalStateB) ||
		     (i>= finalCityA  && i <finalCityB ) ||
		     (i>= finalZipA   && i < finalZipB  ) ) )
		     continue;
		// breach check
		if ( p + 8 + wlens[i] + 2 >= pend ) break;

		if ( ! firstOne ) *p++ = ' ';
		firstOne = false;

		// now do not break up a state name like 'new mexico' into
		// 'gbwhere:new gbwhere:mexico' but rather do
		// 'gbwhere:newmexico' because when we hash the gbwhere:
		// terms we hash the state adm1 string as 'nm' and its synonym
		// 'newmexico'
		// we can't do this right now because when we index foreign
		// events it is always by lat/lon and we do not know the
		// state it is in necessarily...
                //Place *ps = getStatePlace ( i , alnumPos , &w );
		// only print field header if we got something
		//if ( wids[i] ) gotStuff = true;

		// . if this is a state name, condense it
		// . TODO: what about 'new mexico avenue' will 
		//   Address::hash() index 'nm' for that? i would think so
		//   if synonyms work right... TEST!
		if ( finalStateDesc &&
		     //finalStateDesc->m_crid == CRID_US &&
		     i >= finalStateA && 
		     i < finalStateB ) {
			// if we got a city ignore though!
			if ( finalCityDesc ) continue;
			// or zip...
			if ( finalZipA >= 0 ) continue;
			// mark it
			gotStuff = true;
			// use gbstate:
			gbmemcpy ( p , "gbeventstatecode:", 17 );
			p += 17;
			// special treatment. a state abbr is always 2 chars
			gbmemcpy ( p , finalStateDesc->m_adm1 , 2 );
			p += 2;
			// store the country as well for that state whether
			// it was entered or not! because some states are
			// reduced to their numeric code like "08" and
			// many countries have that same code!
			char *cc = getCountryCode(finalStateDesc->m_crid);
			gbmemcpy ( p , " gbeventcountrycode:", 20 ); 
			p += 20;
			gbmemcpy ( p , cc , 2 );
			p += 2;
			// also set the timezone
			*timeZone2 = finalStateDesc->m_timeZoneOffset;
			// and useDST
			*useDST = false;
			if ( finalStateDesc->m_flags&PDF_USE_DST) *useDST=true;
			// ignore until end of state words
			ignoreUntil = finalStateB;
			continue;
		}
		// . we cover foreign states using radius logic up above now
		// . when we index a foreign event we do so using the lat/lon
		//   only since we do not support foreign addresses yet
		// . therefore we do not index gbwhere:<adm1> for it...
		//   so we use the radius centroid logic above
		// . we could fix this by using getNearestCityId() for
		//   the foreign events...
		//else if ( finalStateDesc &&
		//	  finalStateDesc->m_crid != CRID_US &&
		//	  i >= finalStateA && 
		//	  i < finalStateB ) {
		//}
		// same logic for countries
		if ( finalCountryDesc &&
		     //finalCountryDesc->m_crid == CRID_US &&
		     i >= finalCountryA && 
		     i < finalCountryB ) {
			// if we got a city ignore though!
			if ( finalCityDesc ) continue;
			// or zip...
			if ( finalZipA >= 0 ) continue;
			// mark it
			gotStuff = true;
			// special treatment. a country abbr is always 2 chars
			char *cc = getCountryCode(finalCountryDesc->m_crid);
			gbmemcpy ( p , "gbeventcountrycode:", 19 ); 
			p += 19;
			gbmemcpy ( p , cc , 2 );
			p += 2;
			ignoreUntil = finalCountryB;
			continue;
		}
		// . we cover foreign countrys using radius logic up above now
		// . when we index a foreign event we do so using the lat/lon
		//   only since we do not support foreign addresses yet
		// . therefore we do not index gbwhere:<adm1> for it...
		//   so we use the radius centroid logic above
		//else if ( finalCountryDesc &&
		//	  finalCountryDesc->m_crid != CRID_US &&
		//	  i >= finalCountryA && 
		//	  i < finalCountryB ) {
		//}
		
		// mark it
		gotStuff = true;
		// field header
		gbmemcpy ( p , "gbwhere:", 8 );
		// advance
		p += 8;
		// otherwise store into buffer as is
		gbmemcpy ( p , wptrs[i] , wlens[i] );
		// advance ptr cursor
		p += wlens[i];
	}

	// delete?
	if ( ! gotStuff ) p = gbwhereBuf;
	// null term if provided
	if ( p ) *p = '\0';

	// set these
	if ( retCityDesc    ) *retCityDesc    = finalCityDesc;
	if ( retStateDesc   ) *retStateDesc   = finalStateDesc;
	if ( retCountryDesc ) *retCountryDesc = finalCountryDesc;

	return true;//status;
}

// returns false if not found
bool getCityLatLonFromAddress ( Address *aa , double *lat , double *lon ) {

	// assume none
	*lat = NO_LATITUDE;
	*lon = NO_LONGITUDE;

	Place *city  = aa->m_city;
	Place *state = aa->m_adm1;
	Place *zip   = aa->m_zip;

	// set these
	uint64_t cityHash64 = 0;
	char    *adm1Str    = NULL;

	// set city/state from zip if necessary
	if ( ! city && zip ) {
		cityHash64 = zip->m_cityHash;
		adm1Str = zip->m_adm1;
	}
	if ( city )
		cityHash64 = city->m_cityHash;
	if ( state )
		adm1Str = state->m_adm1;

	// both must be valid
	if ( ! cityHash64 ) return false;
	if ( ! adm1Str    ) return false;

	// combine the two hashes
	uint32_t cid32 = (uint32_t)getCityId32(cityHash64,adm1Str);

	// now get the lat lon
	bool status = getLatLon ( cid32 , lat , lon );

	return status;
}

// . like ";;5815 Wyoming Blvd NE;Albuquerque;87109;NM;;;" ???
char *getZipPtrFromStr ( char *data , int32_t *zipLen ) {
	// now point to latitude,longitude
	// skip city,state,zip,something,hash,ip
	char *zipPtr = data;
	int32_t scount = 0;
	for ( ; scount < 6 ; zipPtr++ )
		if ( *zipPtr == ';' ) scount++;
	// get length
	char *end = zipPtr + 1;
	for ( ; *end != ';' ; end++ );
	*zipLen = end - zipPtr ;
	// pts past that ';'
	return zipPtr;
}

bool getZipLatLon ( char  *zip    ,
		    int32_t   zipLen ,
		    float *zipLat ,
		    float *zipLon ) {
	// assume none
	*zipLat = NO_LATITUDE;
	*zipLon = NO_LONGITUDE;
	// only 5 digits i guess
	if ( zipLen != 5 ) return false;
	// hash it
	int64_t zh = getWordXorHash2(zip,zipLen);
	// get it
	ZipDesc *zd = (ZipDesc *)g_zips.getValue(&zh);
	// mine it
	if ( ! zd ) return false;
	*zipLat = zd->m_latitude;
	*zipLon = zd->m_longitude;
	return true;
}

bool getZipLatLonFromStr ( char  *addrStr ,
			   float *zipLat  ,
			   float *zipLon  ) {
	int32_t zipLen;
	char *zip = getZipPtrFromStr ( addrStr , &zipLen );
	return getZipLatLon ( zip , zipLen , zipLat, zipLon );
}

bool getZipLatLonFromAddress ( Address *aa   ,
			       float *zipLat ,
			       float *zipLon ) {
	// assume none
	*zipLat = NO_LATITUDE;
	*zipLon = NO_LONGITUDE;
	Place *zip   = aa->m_zip;
	if ( ! zip ) return false;
	return getZipLatLon(zip->m_str, zip->m_strlen,zipLat,zipLon);
}


// if you just want to call setStr() and have it use stack mem to
// store up to 10 places, then init the PlaceMem with this very quickly
void PlaceMem::init ( int32_t  poolSize         , 
		      int32_t  initNumPoolPtrs  ,
		      int32_t  initNumPlacePtrs ,
		      char *stackMem         , 
		      int32_t  stackMemSize     ,
		      int32_t  niceness         ) { 
	m_stack                 = stackMem;
	m_stackSize             = stackMemSize;
	m_initNumPoolPtrs       = initNumPoolPtrs;
	m_initNumPlacePtrs      = initNumPlacePtrs;
	m_poolSize              = poolSize;
	m_numPlacePtrsAllocated = 0;
	m_numPoolPtrsAllocated  = 0;
	m_numPoolsAllocated     = 0;
	m_numPlacePtrs          = 0;
	m_cursor                = NULL;
	m_cursorEnd             = NULL;
	m_cursorPoolNum         = -1;
	m_niceness              = niceness;
}

// . returns NULL and sets g_errno on error
// . stores ptr to the returned mem in m_placePtrs[placeNum]
void *PlaceMem::getMem ( int32_t need ) {
	// sanity
	if ( need > m_poolSize ) { char *xx=NULL;*xx=0; }
 top:
	// return if we got it
	if ( m_cursor && m_cursor + need <= m_cursorEnd ) {
		// do we need to realloc m_placePtrs?
		if ( m_numPlacePtrs + 1 > m_numPlacePtrsAllocated ) {
			if ( m_stack ) { char *xx=NULL;*xx=0; }
			int32_t   oldSize  =m_numPlacePtrsAllocated * 4;
			int32_t   newAlloc =m_numPlacePtrsAllocated + 2000;
			if ( m_numPlacePtrsAllocated == 0 )
				newAlloc = m_initNumPlacePtrs;
			char **newPtrs = (char **)mmalloc(newAlloc*4,"pptbl");
			if ( ! newPtrs ) return NULL;
			for ( int32_t i = 0 ; i < m_numPlacePtrs ; i++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				newPtrs[i] = m_placePtrs[i];
				// to be safe to avoid bad mem writes
				m_placePtrs[i] = NULL;
			}
			//gbmemcpy ( newPtrs, m_placePtrs , m_numPlacePtrs*4);
			mfree  ( m_placePtrs , oldSize     , "pptbl");
			m_placePtrs             = newPtrs;
			m_numPlacePtrsAllocated = newAlloc;
		}
		// store it
		m_placePtrs[m_numPlacePtrs] = m_cursor;
		// increment it
		m_numPlacePtrs++;
		// save cursor so we can return that
		char *returnPtr = m_cursor;
		// increment to next place (need = sizeof(Place) usually)
		m_cursor += need;
		// return the mem for them to use now
		return (void *)returnPtr;
	}
	
	// try to use stack
	if ( m_stack && m_numPoolPtrsAllocated == 0 ) {
		// compute min size for stack...
		int32_t need = 0;
		need += m_initNumPoolPtrs  * 4 ;
		need += m_initNumPlacePtrs * 4 ;
		need += m_poolSize;
		// make sure stack size is big enough for what they want
		if ( m_stackSize < need ) { char *xx=NULL;*xx=0;}
		// parse it up
		char *p = m_stack;
		m_placePtrs = (char **)p;
		p += m_initNumPlacePtrs * 4;
		m_poolPtrs = (char **)p;
		p += m_initNumPoolPtrs + 4;
		m_poolPtrs[0] = p;
		p += m_poolSize;
		m_numPoolsAllocated = 1;
		m_numPlacePtrsAllocated = m_initNumPlacePtrs;
		m_numPoolPtrsAllocated  = m_initNumPoolPtrs;
		m_cursor        = m_poolPtrs[0];
		m_cursorEnd     = m_cursor + m_poolSize;
		m_cursorPoolNum = 0;
		// give em that mem now i guess
		goto top;
	}

	// always constrain to stack if provided to make things simple
	if ( m_stack ) { char *xx=NULL;*xx=0; }

	// add a new pool
	if ( m_numPoolsAllocated + 1 > m_numPoolPtrsAllocated ) {
		int32_t   oldSize  = m_numPoolPtrsAllocated * 4;
		int32_t   newAlloc = m_numPoolPtrsAllocated + 100;
		if ( m_numPoolPtrsAllocated == 0 )
			newAlloc = m_initNumPoolPtrs;
		char **newPtrs = (char **)mmalloc(newAlloc*4,"pptbl2");
		if ( ! newPtrs ) return NULL;
		gbmemcpy ( newPtrs    , m_poolPtrs , m_numPoolsAllocated*4 );
		mfree  ( m_poolPtrs , oldSize    , "pptbl2");
		m_poolPtrs             = newPtrs;
		m_numPoolPtrsAllocated = newAlloc;
	}

	// if we had called setNumPtrs() or rewind() the next pool might
	// already be allocated, so if that is true, use it!
	int32_t poolNum = m_cursorPoolNum + 1;

	// sanity check
	if ( poolNum > m_numPoolsAllocated ) { char *xx=NULL;*xx=0; }
	// poolNum could be < m_numPoolsAllocated IF we did a rewind at
	// somepoint so that m_cursorPoolNum was decreased in setNumPtrs().
	// but we need to allocate a new pool if that was not the case.
	if ( poolNum == m_numPoolsAllocated ) {
		// make a new pool now
		char *pool = (char *)mcalloc(m_poolSize,"pool3");
		if ( ! pool ) return NULL;
		m_poolPtrs [ m_numPoolsAllocated ] = pool;
		m_numPoolsAllocated++;
	}

	// update cursor now
	m_cursor        = m_poolPtrs[poolNum];
	m_cursorEnd     = m_poolPtrs[poolNum] + m_poolSize;
	m_cursorPoolNum = poolNum;
	// sanity check
	char *pool    = m_poolPtrs[m_cursorPoolNum];
	char *poolEnd = pool + m_poolSize;
	if ( m_cursor < pool || m_cursor >= poolEnd ) { char *xx=NULL;*xx=0;}

	// and re-try
	goto top;
}

PlaceMem::PlaceMem() {
	// make sure reset() won't core us
	m_placePtrs   = NULL;
	m_poolPtrs    = NULL;
	m_numPoolsAllocated = 0;
	m_niceness    = 0;
	m_numPlacePtrs = 0;
	m_numPoolPtrsAllocated = 0;
	m_numPlacePtrsAllocated = 0;
	m_stack = NULL;//false;
}

PlaceMem::~PlaceMem() {
	reset();
}

void PlaceMem::reset ( ) {
	// do not core
	if ( m_stack ) return;
	// free everything
	for ( int32_t i = 0 ; i < m_numPoolsAllocated; i++ ) {
		QUICKPOLL(m_niceness);
		mfree( m_poolPtrs[i] , m_poolSize, "pool3");
		m_poolPtrs[i] = NULL;
	}
	// free ptrs
	if ( m_placePtrs ) 
		mfree ( m_placePtrs, m_numPlacePtrsAllocated * 4,"plptrs");
	if ( m_poolPtrs )
		mfree ( m_poolPtrs , m_numPoolPtrsAllocated * 4,"poptrs");
	m_placePtrs = NULL;
	m_poolPtrs  = NULL;
	m_numPoolPtrsAllocated = 0;
	m_numPlacePtrsAllocated = 0;
	m_cursor = NULL;
	m_numPlacePtrs = 0;
	m_numPoolsAllocated = 0;
}

// . sometimes we remove the last X Places we added above when we realized
//   something was bogus
// . pass in ptr to first Place ptr to be nuked
void PlaceMem::setNumPtrs ( int32_t newNumPtrs ) {
	// return if no change requested
	if ( newNumPtrs == m_numPlacePtrs ) return;
	// sanity check
	if ( newNumPtrs >= m_numPlacePtrs ) { char *xx=NULL;*xx=0;};
	if ( newNumPtrs <  0              ) { char *xx=NULL;*xx=0;};
	// set it back
	m_cursor = m_placePtrs[newNumPtrs];
	// back up the pool until we are in it
	for ( ; m_cursorPoolNum >= 0 ; m_cursorPoolNum-- ) {
		char *pool    = m_poolPtrs[m_cursorPoolNum];
		char *poolEnd = pool + m_poolSize;
		if ( m_cursor >= pool && m_cursor < poolEnd ) {
			m_cursorEnd = poolEnd;
			break;
		}
	}
	// this is wierd
	if ( m_cursorPoolNum < 0 ) { char *xx=NULL;*xx=0; }
	// reset final
	m_numPlacePtrs = newNumPtrs;
}

void resetAddressTables ( ) {
	if ( s_latList ) mfree ( s_latList,s_latListSize,"latlist");
	s_latList = NULL;
	g_timeZones.reset();
	g_zips.reset();
	g_cities.reset();
	g_indicators.reset();
	g_aliases.reset();
	g_states.reset();
	s_lc.reset();
	s_syn.reset();
	s_jobTable.reset();
	s_doyTable.reset();
	g_nameTable.reset();
	if ( g_pbuf ) mfree ( g_pbuf, g_pbufSize , "placbuf");
}

///////////////////////////////////////////////////
//
// NEW PLACES LOGIC
//
// Use this for the new functions:

// If user enters 'berlin': (try to get in country of m_ipCrid first)
// If user enters 'berlin, germany':
// PlaceDesc *getMostPopularCity_new ( uint64_t cityHash64,char crid)
//   Algorithm: scan list of cities in that country and choose the most
//   populated one in that country.

// If user enters 'berlin': (next, try to get most popular in world)
// PlaceDesc *getMostPopularCity_new ( uint64_t cityHash64 , 0 = crid );

// If user enters 'berlin, <adm1>'  or 'cincinnati, ohio'.
// PlaceDesc *getCityInState_new ( uint64_t cityHash64,uint64_t stateHash64);
//   Algorithm: get list of all places that are states with stateHash64, and
//   record list as the two-letter state codes. Then scan the cities with
//   cityHash64 and see which has one of the state codes in that list.

// If user enters 'germany' or 'republic of chad'
// PlaceDesc *getCountryPlace ( int32_t a, int32_t alnumPos, Words *w );

// need this
// PlaceDesc *getCountryDescFromId ( uint8_t crid );

// For getting the timezone from a lat/lon in a foreign country:
// PlaceDesc *getNearestCity_new ( float lat , float lon );


///////////////////////////////////////////////////


// . maps a hash of a word or phrase to a PlaceDesc ptr
// . dups are allowed - one key can map to multiple PlaceDescriptors
//HashTableX g_nameTable;

bool loadPlaces ( ) {

	// map 64bit name hash to a place dec ptr. allowdups= true.
	// niceness = 0.
	g_nameTable.set ( 8 , // 64 bit key hash
			  4 , // placedec ptr
			  0 , // no initial slots
			  NULL , // no intiial buf
			  0 ,  // zero initial buf size
			  true , // allow dups?
			  0 , // niceness
			  "nametab" );


	if ( g_proxy.isProxy() ) return true;

	// log it
	log("places: loading places.dat");

	// try to load from disk
	if ( g_nameTable.load ( g_hostdb.m_dir , 
				"places.dat" , 
				&g_pbuf ,
				&g_pbufSize ) ) {
		// test it out
		PlaceDesc *pd = getCity2_new ( "abq", "nm", CRID_US,0);
		if ( ! pd ) { char *xx=NULL;*xx=0; }
		// make sure "nm" brings up new mexico
		pd = getState2_new ( "nm", CRID_US,0);
		if ( ! pd ) { char *xx=NULL;*xx=0; }
		// scan for integrity
		pd = (PlaceDesc *)g_pbuf;
		//PlaceDesc *pdend = (PlaceDesc *)(g_pbuf+g_pbufSize);
		for ( ; ; pd++ ) {
			// stop if we enter the name buf space
			if ( ((char *)pd)[0] == 'u' &&
			     ((char *)pd)[1] == 'n' &&
			     ! strcmp((char *)pd,"unknown name" ) )
				break;
			// sanity
			if ( pd->m_lat < -180.0 ) { char *xx=NULL;*xx=0; }
			if ( pd->m_lat >  180.0 ) { char *xx=NULL;*xx=0; }
			if ( pd->m_lon < -180.0 ) { char *xx=NULL;*xx=0; }
			if ( pd->m_lon >  180.0 ) { char *xx=NULL;*xx=0; }
		}
		return true;
	}

	// error?
	log("places: failed to load places.dat: %s",mstrerror(g_errno));

	// try making it
	return generatePlacesFile ( );
}

// used by PageEvents.cpp's getSiteMap() to list the most popular cities
PlaceDesc *getPlaceDescBuf () {
	return (PlaceDesc *)g_pbuf;
}

bool generatePlacesFile ( ) {

	log("places: generating places.dat file");

	char buf[10000];


	//
	// MAKE TIMEZONE TABLE for referencing
	//
	// scan allCountries.txt
	char pcmd[1024];
	sprintf(pcmd,"cat %s/timeZones.txt",g_hostdb.m_dir);
	FILE *pf = popen ( pcmd , "r" );
	if ( ! pf ) { 
		g_errno = errno; 
		return log("places: could not open timeZones.txt");
	}
	class TZVal {
	public:
		char m_tzoff;
		char m_useDST;
	};
	HashTableX tztab;
	tztab.set ( 8 , sizeof(TZVal),0,NULL,0,false,0,"tztab");
	// read in the lines
	while ( fgets ( buf , 10000 , pf ) ) {
		// null terminate it, instead of \n
		buf[gbstrlen(buf)-1]='\0';
		// parse it up. timezonestr\ttzoff1|tzoffdst
		char  timeZoneStr[64]; // Europe/Andorra
		int32_t  off1;
		int32_t  off2; // dst
		sscanf ( buf , 
			 "%s\t" // timezone name
			 "%"INT32"\t" // off1
			 "%"INT32"" // off2
			 , timeZoneStr
			 , &off1
			 , &off2
			 );
		// make a table
		int64_t tzh64 = getWordXorHash ( timeZoneStr );
		// make the value
		TZVal tzval;
		tzval.m_tzoff = off1;
		if ( off1 != off2 ) tzval.m_useDST = 1;
		else                tzval.m_useDST = 0;
		tztab.addKey ( &tzh64 , &tzval );
	}
		



	// . map a geoId to ptr to the PlaceDesc in the g_placeBuf
	// . a temporary table really...
	HashTableX places;
	places.set ( 4, 4, 5000000 , NULL ,0 , false, 0,"gpht");

	// official names of each place
	SafeBuf nameBuf;
	nameBuf.reserve ( 10*1024*1024 );
	// this is actually required and we check for it to avoid
	// overruning our PlaceDesc when we scan those. we need this
	// to set "pdend" for the PlaceDesc scan because we concatenate
	// the nameBuf to the end of the placeBuf. so basically
	// places.dat holds those two conjoined buffers ...
	nameBuf.safePrintf("unknown name");
	nameBuf.pushChar('\0');

	int32_t zero = 0;

	// reserve 100MB
	SafeBuf placeBuf;
	placeBuf.reserve ( 100*1024*1024 );

	HashTableX dedup;
	dedup.set ( 8,4,100000,NULL,0,false,0,"pddptb");

	// this will have to be remade
	sprintf(pcmd,"unlink %s/citylatlist.dat",g_hostdb.m_dir);
	system(pcmd);

	// scan allCountries.txt
	sprintf(pcmd,"cat %s/allCountries.txt",g_hostdb.m_dir);
	pf = popen ( pcmd , "r" );
	if ( ! pf ) { g_errno = errno; return false; }

	// limit g_nameTable from getting too big! otherwise places.dat
	// is 550MB on disk and in memory!!! with this is it 200MB.
	// otherwise it grows to 32M slots...
	g_nameTable.m_maxSlots = 8388608; // 1<<23

	// read in the lines
	while ( fgets ( buf , 10000 , pf ) ) {
		// null terminate it, instead of \n
		buf[gbstrlen(buf)-1]='\0';
		// parse it up. id|name|lat|lon|abbr
		/*
		int32_t  geoId;
		char  name[512];
		float lat;
		float lon;
		char  code     [16];
		char  countryAbbr[32];
		char  stateAbbr[32];
		int32_t  population = 0;
		char  timeZoneStr[64]; // Europe/Andorra
		*/
		// convert all tabs to \0
		char *p = buf;
		for ( ; *p ; p++ ) if ( *p == '\t' ) *p = '\0';
		// see /geo/geonames/index.html for format description
		p = buf;
		int32_t geoId = atol(p); p += strlen(p) + 1;
		//if ( geoId == 1850147 )
		//	log("hey");
		char *officialName = p; p += strlen(p) + 1; // official name
		char *asciiName = p; p += strlen(p) + 1; // asciname
		char *altNames = p; p += strlen(p)+1; // altnames
		float lat;
		// sometimes allCountries.txt leaves out "altNames" field!
		// so detect if this field is a latitude or not...
		bool hadAlpha  = false;
		bool hadDigit  = false;
		bool hadPeriod = false;
		char *tmp = altNames;
		for ( ; *tmp ; tmp++ ) {
			if ( is_alpha_a(*tmp) ) hadAlpha = true;
			if ( is_digit  (*tmp) ) hadDigit = true;
			if ( *tmp == '.' ) hadPeriod = true;
		}
		// need a digit and no alphas to be a latitude
		bool isLat = false;
		if ( hadDigit && ! hadAlpha && hadPeriod ) isLat = true;
		if ( isLat ) {
			lat = atof ( altNames );
		}
		else {
			lat = atof(p); 
			p += strlen(p) + 1;
		}
		float lon = atof ( p ); p += strlen(p) + 1;
		p += strlen(p) + 1; // code class
		char *code = p; p += strlen(p)+1; // code type
		char *countryAbbr = p; p += strlen(p)+1;
		p += strlen(p)+1; // altCountry
		char *stateAbbr =  p; p += strlen(p)+1;
		p += strlen(p)+1; // adm2
		p += strlen(p)+1; // adm3
		p += strlen(p)+1; // adm4
		int32_t population = atol(p); p += strlen(p)+1;
		p += strlen(p)+1; // elevation
		p += strlen(p)+1; // avg elevation
		char *timeZoneStr = p; p += strlen(p)+1;
		p += strlen(p)+1; // moddate

		// debug point
		//if ( geoId == 5381396 )
		//	log("hey");

		// skip if no timezone for now
		if ( ! timeZoneStr[0] ) {
			log("places: no timezone for geoid=%"INT32" name=%s",
			    geoId,officialName);
			continue;
		}
		
		// reserve space
		//placeBuf.reserve ( 1024 );
		// not allowed to grow since we use dedup table now
		if ( placeBuf.getAvail() < (int32_t)sizeof(PlaceDesc) ) {
			char *xx=NULL;*xx=0;}

		// make a new country desc
		PlaceDesc *pd = (PlaceDesc *)placeBuf.getBuf();
		//
		// see http://www.geonames.org/export/codes.html
		//
		
		// exceptions: 
		// "122 Mile House" ...
		if  ( ! strncmp( code,"PPLL",4)) continue;
		// a basic city
		if      ( ! strncmp( code,"PPL",3)) pd->m_flags = PDF_CITY;
		// locality
		else if ( ! strcmp ( code ,"LCTY")) pd->m_flags = PDF_CITY;
		// . town of, township, town of north hempstead
		// . crap! this gets a different san jose!
		// . avoid "City of Cincinnati" etc.. crap
		// . BUT allow town of north hempstead through (5129081)
		else if ( ! strcmp ( code ,"ADMD") && geoId == 5129081 )
			pd->m_flags = PDF_CITY;
		// independent political entity
		else if ( ! strcmp ( code,"PCLIX")) pd->m_flags = PDF_CITY;
		// another city i guess
		else if ( ! strcmp ( code , "P" ) ) pd->m_flags = PDF_CITY;
		// states
		else if ( ! strcmp ( code ,"ADM1")) pd->m_flags = PDF_STATE;
		// countries
		else if ( ! strcmp ( code ,"PCLI")) pd->m_flags = PDF_COUNTRY;
		// otherwise, skip it!
		else continue;

		// . sanity
		// . these were messing up our raw lat/lon processing
		//   in searchinput.cpp because we thought that a direct
		//   lat/lon in the wherebox was a city name because there was
		//   a city name that was "35", which was our latitude entered!
		if ( pd->m_flags == PDF_CITY && is_digit(officialName[0]) ){
			log("places: bad city name: %s",officialName);
			continue;
		}

		// a bunch of cities do not have states...
		//if ( pd->m_flags != PDF_COUNTRY && 
		//     ( ! stateAbbr[0] || ! stateAbbr[0] ) ) {
		//	log("hey %s",officialName);
		//	continue;
		//}

		// get country id
		pd->m_crid = getCountryId ( countryAbbr );
		// geoid for looking up in alternateNames.txt
		//pd->m_geoId = geoId;
		// lat and lon
		pd->m_lat = lat;
		pd->m_lon = lon;
		pd->m_population = population;
		// skip over it (not allowed to grow anymore!)
		//placeBuf.advance ( sizeof(PlaceDesc) );
		placeBuf.m_length += (int32_t)sizeof(PlaceDesc);
		// . point to that. we'll store <adm1>,<name> in there now
		// . we need to somehow append alternate names later
		//pd->m_data = placeBuf.getBuf();
		// store adm1 in m_data[]
		pd->m_adm1[0] = to_lower_a(stateAbbr[0]);
		pd->m_adm1[1] = to_lower_a(stateAbbr[1]);
		// if greece... use last two
		if ( to_lower_a(countryAbbr[0]) == 'g' &&
		     to_lower_a(countryAbbr[1]) == 'r' &&
		     pd->m_adm1[0] == 'e' &&
		     pd->m_adm1[1] == 's' &&
		     is_digit(stateAbbr[4]) &&
		     is_digit(stateAbbr[5]) ) {
			// store the last two letter's for greece
			pd->m_adm1[0] = to_lower_a(stateAbbr[4]);
			pd->m_adm1[1] = to_lower_a(stateAbbr[5]);
		}
		// hash timezone string
		uint64_t tzh64 = getWordXorHash ( timeZoneStr );
		//look it up in our table made from /geo/geonames/timeZones.txt
		TZVal *tzv = (TZVal *)tztab.getValue ( &tzh64 );
		if ( ! tzv ) { char *xx=NULL;*xx=0 ;}
		// from -12 to + 12 i guess
		pd->m_timeZoneOffset = tzv->m_tzoff;
		// now the daylightsavings time flag
		if ( tzv->m_useDST ) pd->m_flags |= PDF_USE_DST;
		// . add to table using the name as the key
		// . i think this table is just for generation since
		//   we'll use the g_namesTable to map place names to
		//   the PlaceDesc.
		places.addKey(&geoId,&pd);
		// store OFFSETS in nametable
		int32_t placeDescOffset = (char *)pd - placeBuf.getBufStart();

		// we need to add the official name here because it's not
		// always in alternateNames.txt... 
		uint64_t nh64a = getWordXorHash ( officialName );
		uint64_t dedupKeya = nh64a ^ (uint32_t)placeDescOffset;
		// skip if in there
		if ( ! dedup.isInTable(&dedupKeya) ) {
			// make this name's hash point to its PlaceDesc
			if ( ! g_nameTable.addKey ( &nh64a, &placeDescOffset)) 
				return false;
			// do not add dup combos
			dedup.addKey ( &dedupKeya , &zero );
		}

		// hmmm... we need nh64 to be ascii for adding to nameBuf...
		uint64_t exactHash64 = hash64n ( officialName );
		// also make this name's hash point to the
		// name itself so we can convert a lat/lon into
		// a place name, based on getNearestCity_new()
		if ( ! dedup.isInTable ( &exactHash64 ) ) {
			// nameBuf
			int32_t nameOffset = nameBuf.length();
			// store it
			int32_t olen = gbstrlen(officialName);
			nameBuf.safeMemcpy ( officialName , olen );
			nameBuf.pushChar('\0');
			// store offset
			pd->m_officialNameOffset = nameOffset;
			// do not repeat!
			dedup.addKey ( &exactHash64 , &nameOffset );
		}
		else {
			// i guess we already added this name before so
			// point to where we added it
			int32_t off = *(int32_t *)dedup.getValue ( &exactHash64 );
			// use that then
			pd->m_officialNameOffset = off;
		}

		//
		// also add the ascii too, it seems a lot of times that
		// is not given in the alternateNames.txt file either!!!!
		//
		uint64_t nh64b = getWordXorHash ( asciiName );
		uint64_t dedupKeyb = nh64b ^ (uint32_t)placeDescOffset;
		// skip if in there
		if ( ! dedup.isInTable(&dedupKeyb) ) {
			// make this name's hash point to its PlaceDesc
			if ( ! g_nameTable.addKey ( &nh64b, &placeDescOffset)) 
				return false;
			// do not add dup combos
			dedup.addKey ( &dedupKeyb , &zero );
		}


		// skip if not state
		if ( ! ( pd->m_flags & PDF_STATE) ) continue;
		// skip if is numeric for now... strange...
		//if ( is_digit(stateAbbr[0]) ) continue;
		if ( ! stateAbbr[0] ) continue;
		// if we are a state, add our abbreviation here as well!
		// does this convert to lowercase? yes... it should
		uint64_t nh64c = getWordXorHash ( stateAbbr );
		// make another dedupkey
		uint64_t dedupKeyc = nh64c ^ (uint32_t)placeDescOffset;
		// check that as well
		if ( dedup.isInTable(&dedupKeyc) ) continue;
		if ( ! g_nameTable.addKey ( &nh64c , &placeDescOffset ) ) 
			return false;
		// do not add dup combos
		dedup.addKey ( &dedupKeyc , &zero );
	}
	// close the pipe
	pclose(pf);

	// . now scan in the alternateNames.txt
	// . add to the hashtablex g_nameTable
	// . key is word xor hash of the name
	// . value is ptr to the PlaceDesc in placeBuf
	// . allow dups since a single name can point to multiple unique places
	sprintf(pcmd,"cat %s/alternateNames.txt",g_hostdb.m_dir);
	pf = popen ( pcmd , "r" );
	if ( ! pf ) { g_errno = errno; return false; }

	// read in the lines
	while ( fgets ( buf , 10000 , pf ) ) {
		// null terminate it, instead of \n
		buf[gbstrlen(buf)-1]='\0';
		// convert all tabs to \0
		char *p = buf;
		for ( ; *p ; p++ ) if ( *p == '\t' ) *p = '\0';
		// parse it up. id|name|lat|lon|abbr
		p = buf;
		p += strlen(p) + 1; // some number
		int32_t geoId = atol(p); p += strlen(p) + 1;
		p += strlen(p) + 1; // langIdStr
		char *altName = p; p += strlen(p) + 1; 
		p += strlen(p) + 1; // is preferred name
		p += strlen(p) + 1; // is int16_t ?name
		// now hash up that name
		uint64_t nh64d = getWordXorHash ( altName );
		// find the place desc for it
		PlaceDesc **ppd = (PlaceDesc **)places.getValue ( &geoId );
		// this won't be there if its not a city,ctry,state, etc.
		// or timezone was missing above
		if ( ! ppd ) continue;
		// cast it otherwise
		PlaceDesc *pd = *ppd;
		// store OFFSETS in nametable
		int32_t placeDescOffset = (char *)pd - placeBuf.getBufStart();
		// do not add dup combos
		uint64_t dedupKeyd = nh64d ^ (uint32_t)placeDescOffset;
		if ( dedup.isInTable ( &dedupKeyd ) ) continue;
		// use that
		if ( ! g_nameTable.addKey ( &nh64d , &placeDescOffset ) ) 
			return false;
		// do not add dup combos
		dedup.addKey ( &dedupKeyd , &zero ) ;
	}
	pclose(pf);

	// set this temporarily so getState_new() etc. works for now
	g_pbuf = placeBuf.getBufStart();

	// . add in state aliases for states in the US
	// . "wash" = "washington" "ore = oregeon" etc.
	int32_t n = (int32_t)sizeof(s_states)/ sizeof(StateDesc);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get it
		StateDesc *sd = &s_states[i];
		// skip if none
		if ( ! sd->m_name2 ) continue;
		// get original name
		uint64_t nh64 = getWordXorHash ( sd->m_name1 );
		// get the PlaceDesc. this will scan all the matches and
		// get the one that is a state in the US
		PlaceDesc *pd = getState_new ( nh64 , CRID_US , 0 );
		// must be there
		if ( ! pd ) { char *xx=NULL;*xx=0; }
		// make key (d.c. colo. n.m.)
		uint64_t anh64 = getWordXorHash ( sd->m_name2 );
		// store OFFSETS in nametable
		int32_t offset = (char *)pd - placeBuf.getBufStart();
		// add the alias
		if ( ! g_nameTable.addKey ( &anh64 , &offset ) ) return false;
	}

	// add our CITY aliases i.e. "abq" or "nyc" for cities in the US
	n = (int32_t)sizeof(s_cityList)/ sizeof(AliasDesc);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get it
		AliasDesc *ad = &s_cityList[i];
		// get the PlaceDesc. this will scan all the matches and
		// get the one that is a state in the US
		PlaceDesc *pd = getCity2_new(ad->m_s2, ad->m_adm1 , CRID_US,0);
		// must be there
		if ( ! pd ) { char *xx=NULL;*xx=0; }
		// make key (d.c. colo. n.m.)
		uint64_t ach64 = getWordXorHash ( ad->m_s1 );
		// store OFFSETS in nametable
		int32_t offset = (char *)pd - placeBuf.getBufStart();
		// add the alias
		if ( ! g_nameTable.addKey ( &ach64 , &offset ) ) return false;
	}

	// size of placeBuf
	int32_t placeBufLength = placeBuf.length();
	// concatenate nameBuf to placeBuf for saving to disk
	if ( ! placeBuf.cat ( nameBuf ) ) return false;
	// adjust all PlaceDesc::m_officialNameOffset vars to compensate for 
	// this concatenation
	PlaceDesc *pd = (PlaceDesc *)placeBuf.getBufStart();
	PlaceDesc *pdend = (PlaceDesc *)(((char *)pd) + placeBufLength);
	for ( ; pd < pdend ; pd++ ) 
		pd->m_officialNameOffset += placeBufLength;
	
	// test it out
	PlaceDesc *pd2 = getCity2_new ( "abq", "nm", CRID_US,0);
	if ( ! pd2 ) { char *xx=NULL;*xx=0; }

	int64_t ph64 = getWordXorHash ( "Tokyo" );
	pd2 = getMostPopularPlace_new ( ph64 ,CRID_ANY ,PDF_CITY,0 );
	if ( ! pd2 ) { char *xx=NULL;*xx=0; }

	// pasadena texas is more popular than california!
	ph64 = getWordXorHash ( "Pasadena" );
	pd2 = getMostPopularPlace_new ( ph64 ,CRID_US ,PDF_CITY,0 );
	//if ( pd2->m_population != 144618 ) { char *xx=NULL;*xx=0; }
	if ( ! pd2 ) { char *xx=NULL;*xx=0; }

	// . now the g_nameTable points into the buffer of PlaceDesc, save it
	// . HashTableX can save the buffer too now!
	if ( ! g_nameTable.save ( g_hostdb.m_dir ,
				  "places.dat" ,
				  placeBuf.getBufStart() ,
				  placeBuf.length() ) )
		return false;

	// ok, try loading now
	placeBuf.purge();
	g_nameTable.reset();

	log("places: loading generated table places.dat from disk");

	return g_nameTable.load ( g_hostdb.m_dir , "places.dat" , 
				  &g_pbuf , 
				  &g_pbufSize );
}

// get the state in this country
PlaceDesc *getState_new ( uint64_t pd64 , uint8_t crid , int32_t niceness ) {
	int32_t slot = g_nameTable.getSlot ( &pd64 );
	// scan the slots
	for ( ; slot >= 0 ; slot = g_nameTable.getNextSlot(slot,&pd64) ) {
		// breathe
		QUICKPOLL(niceness);
		// get the placedesc
		int32_t offset = *(int32_t *)g_nameTable.getValueFromSlot(slot);
		PlaceDesc *pd = (PlaceDesc *)(g_pbuf + offset);
		// skip if not a state
		if ( ! (pd->m_flags & PDF_STATE ) ) continue;
		// skip if not right country
		if ( pd->m_crid != crid ) continue;
		// we got it!
		return pd;
	}
	return NULL;
}

// get the state in this country
PlaceDesc *getState2_new ( char *state , uint8_t crid , int32_t niceness ) {
	uint64_t sh64 = getWordXorHash ( state );
	return getState_new ( sh64, crid,niceness);
}

PlaceDesc *getCity_new ( uint64_t ch64 , 
			 char *stateAbbr ,
			 uint8_t crid ,
			 int32_t niceness ) {

	// sanity
	if ( ! is_lower_a(stateAbbr[0]) ) { char *xx=NULL;*xx=0; }
	if ( ! is_lower_a(stateAbbr[1]) ) { char *xx=NULL;*xx=0; }

	int32_t slot = g_nameTable.getSlot ( &ch64 );
	// scan the slots
	for ( ; slot >= 0 ; slot = g_nameTable.getNextSlot(slot,&ch64) ) {
		// breathe
		QUICKPOLL(niceness);
		// get the placedesc
		int32_t offset = *(int32_t *)g_nameTable.getValueFromSlot(slot);
		PlaceDesc *pd = (PlaceDesc *)(g_pbuf + offset);
		// skip if not a city
		if ( ! (pd->m_flags & PDF_CITY ) ) continue;
		// skip if not right country
		if ( crid != CRID_ANY && pd->m_crid != crid ) continue;
		// or right state
		if ( stateAbbr[0] != pd->m_adm1[0] ) continue;
		if ( stateAbbr[1] != pd->m_adm1[1] ) continue;
		// we got it!
		return pd;
	}
	return NULL;
}

PlaceDesc *getCity2_new ( char *city ,
			  char *stateAbbr ,
			  uint8_t crid ,
			  int32_t niceness ) {
	uint64_t ch64 = getWordXorHash ( city );
	return getCity_new ( ch64, stateAbbr,crid,niceness);
}

PlaceDesc *getCity3_new ( uint64_t ch64 , 
			  uint64_t stateHash64,
			  uint8_t crid ,
			  int32_t niceness ) {

	int32_t slot1 = g_nameTable.getSlot ( &ch64 );
	// scan the slots
	for ( ; slot1 >= 0 ; slot1 = g_nameTable.getNextSlot(slot1,&ch64) ) {
		// breathe
		QUICKPOLL(niceness);
		// get the placedesc
		int32_t offset1 = *(int32_t *)g_nameTable.getValueFromSlot(slot1);
		PlaceDesc *pd1 = (PlaceDesc *)(g_pbuf + offset1);
		// skip if not a city
		if ( ! (pd1->m_flags & PDF_CITY ) ) continue;
		// skip if not right country
		if ( crid != CRID_ANY && pd1->m_crid != crid ) continue;
		// see if we got a state that matches "stateHash64" and
		// "pd->m_adm1"
		int32_t slot2 = g_nameTable.getSlot ( &stateHash64 );
		for ( ; slot2 >= 0 ; 
		      slot2=g_nameTable.getNextSlot(slot2,&stateHash64)) {
			// breathe
			QUICKPOLL(niceness);
			// get the placedesc
			int32_t offset2;
			offset2 = *(int32_t *)g_nameTable.getValueFromSlot(slot2);
			PlaceDesc *pd2 = (PlaceDesc *)(g_pbuf + offset2);
			// skip if not a city
			if ( ! (pd2->m_flags & PDF_CITY ) ) continue;
			// skip if not right country
			if ( crid != CRID_ANY && pd2->m_crid != crid) continue;
			// matching abbr?
			if ( pd2->m_adm1[0] != pd1->m_adm1[0] ) continue;
			if ( pd2->m_adm1[1] != pd1->m_adm1[1] ) continue;
			// it's a match!
			return pd1;
		}
	}
	return NULL;
}


bool getLongestPlaceName_new ( int32_t a,
			       int32_t alnumPos,
			       Words *words,
			       // must match! PDF_CITY|STATE|COUNTRY
			       uint8_t placeType,
			       uint8_t crid,
			       char *stateAbbr,
			       uint64_t *placeHash64,
			       int32_t *placeAlnumA,
			       int32_t *placeAlnumB,
			       int32_t *placeA,
			       int32_t *placeB ,
			       // set to most popular match
			       PlaceDesc **pdp ) {
	// assume none
	if ( placeHash64 ) *placeHash64 = 0LL;
	// init hash to zero
	int64_t h = 0LL;
	// max count
	int32_t count = 0;
	// record start
	int32_t startAlnumPos = alnumPos;
	// fix this
	alnumPos--;
	// for some filtering
	static bool s_flag = false;
	static int64_t h_university;
	static int64_t h_of;
	if ( ! s_flag ) {
		s_flag = true;
		h_university = hash64n("university");
		h_of         = hash64n("of");
	}
	// int16_tcut
	int32_t nw = words->m_numWords;
	int32_t wcount = 0;
	// loop over words in [a,b)
	for ( int32_t k = a ; k < nw ; k++ ) {
		// or 15 words is good enough too!
		if ( ++wcount >= 20 ) break;
		// skip if not alnum
		if ( ! words->isAlnum(k) ) continue;
		// count it
		alnumPos++;
		// only up to 4 words in a place name
		if ( ++count >= 5 ) break;
		// get the hash of potential place name
		int64_t wid = words->m_wordIds[k];
		// int16_tcut
		int32_t  wlen = words->m_wordLens[k];
		char *wptr = words->m_words[k];
		// if it ended in apostrophe s then fix that
		if ( wlen > 2 &&
		     wptr[wlen-2]=='\'' && 
		     to_lower_a(wptr[wlen-1]) == 's' )
			// hash the word without the 's
			wid = hash64Lower_utf8(wptr,wlen-2);
		// mix it up
		h <<= 1;
		// hash it into our ongoing hash
		h ^= wid;
		// ignore "University" if "of" follows
		if ( h == h_university && 
		     k + 2 < nw &&
		     words->m_wordIds[k+2] == h_of )
			continue;
		// get it. just get the most popular that matches
		PlaceDesc *pd = getPlaceDesc ( h,placeType,crid,stateAbbr,0);
		if ( ! pd ) continue;
		// check for "county" (santa fe county is not a city name)
		if ( k + 2 < nw && words->m_wordIds[k+2] == h_county ) {
			// nuke it
			if ( placeHash64 ) *placeHash64 = 0LL;
			return true;
		}
		// int16_tcuts
		//char **wptrs = words->getWords();
		//int32_t  *wlens = words->getWordLens();
		// set the place
		*placeA = a;
		*placeB = k+1;
		*placeAlnumA = startAlnumPos;
		*placeAlnumB = alnumPos+1;
		if ( placeHash64 ) *placeHash64 = h;
		if ( pdp ) *pdp = pd;
	}
	return true;
}

// . placeType is like PDF_CITY or PDF_STATE or PDF_COUNTRY
// . return most popular i guess
PlaceDesc *getPlaceDesc ( uint64_t placeHash64 , 
			  uint8_t placeType ,
			  uint8_t crid,
			  char *stateAbbr,
			  int32_t niceness ) {
	int32_t maxPop = -1;
	PlaceDesc *best = NULL;
	int32_t slot = g_nameTable.getSlot ( &placeHash64 );
	// scan the slots
	for ( ; slot >= 0 ; slot = g_nameTable.getNextSlot(slot,&placeHash64)){
		// breathe
		QUICKPOLL(niceness);
		// get the placedesc
		int32_t offset = *(int32_t *)g_nameTable.getValueFromSlot(slot);
		PlaceDesc *pd = (PlaceDesc *)(g_pbuf + offset);
		// skip if not the right type of place
		if ( ! (pd->m_flags & placeType ) ) continue;
		// crid too match?
		if ( crid != CRID_ANY && pd->m_crid != crid ) continue;
		// state match?
		if ( stateAbbr && pd->m_adm1[0] != stateAbbr[0] ) continue;
		if ( stateAbbr && pd->m_adm1[1] != stateAbbr[1] ) continue;
		// get pop
		if ( pd->m_population <= maxPop ) continue;
		// otherwise, a new max
		maxPop = pd->m_population;
		// save it
		best = pd;
	}
	return best;
}

bool getZip_new ( int32_t a , 
		  int32_t alnumPos , 
		  Words *words ,
		  uint64_t *zipHash64 ,
		  uint64_t *zipCityHash64 ,
		  uint64_t *zipStateHash64 ,
		  int32_t *zipAlnumA,
		  int32_t *zipAlnumB,
		  int32_t *zipA,
		  int32_t *zipB,
		  float *zipLat,
		  float *zipLon ) {
	// assume none
	if ( zipHash64 ) *zipHash64 = 0LL;
	// must be a number
	if ( ! is_digit(words->m_words[a][0]) ) return true;
	// make hash
	int64_t h = 0 ^ words->m_wordIds[a];
	// check for zip code
	int32_t slot = g_zips.getSlot(&h);
	// skip if not
	if ( slot < 0 ) return true;
	// get the place
	ZipDesc *zd =(ZipDesc *)g_zips.getValueFromSlot(slot);
	// set state hash
	if ( zipStateHash64 ) *zipStateHash64 = hash64(zd->m_adm1,2,0LL);
	// and city hash
	if ( zipCityHash64 ) *zipCityHash64 = zd->m_cityHash;
	*zipA = a;
	*zipB = a+1;
	*zipAlnumA = alnumPos;
	*zipAlnumB = alnumPos+1;
	if ( zipHash64 ) *zipHash64 = h;
	*zipLat = zd->m_latitude;
	*zipLon = zd->m_longitude;
	return true;
}

PlaceDesc *getMostPopularPlace_new ( int64_t placeHash64, 
				     uint8_t crid ,
				     uint8_t placeType,
				     int32_t niceness ) {
	int32_t maxPop = -1;
	PlaceDesc *best = NULL;
	int32_t slot = g_nameTable.getSlot ( &placeHash64 );
	// scan the slots
	for ( ; slot >= 0; slot = g_nameTable.getNextSlot(slot,&placeHash64)){
		// breathe
		QUICKPOLL(niceness);
		// get the placedesc
		int32_t offset = *(int32_t *)g_nameTable.getValueFromSlot(slot);
		PlaceDesc *pd = (PlaceDesc *)(g_pbuf + offset);
		// skip if not a the right type of place
		if ( ! (pd->m_flags & placeType ) ) continue;
		// skip if not right country
		if ( crid != CRID_ANY && pd->m_crid != crid ) continue;
		// get pop
		if ( pd->m_population <= maxPop ) continue;
		// otherwise, a new max
		maxPop = pd->m_population;
		// save it
		best = pd;
	}
	return best;
}

//
// . the new getNearestCity_new() function
// . copied from getNearestCity() function above
//

//static int32_t *s_latList2 = NULL;
//static int32_t  s_latListSize2 = 0;
//static int32_t  s_ni2 = 0;
static SafeBuf s_cityLatList;


// . we need a list of the city ids sorted by lat, and a list sorted by lon
// . then we do b-stepping on each list
// . bstep down to a 20 mile by 20 mile box
// . then intersect using a hashtable
// . if empty, then increase to 30 by 30 mile box, etc.
// . there are 123k US cities in cities.dat
// . these 2 lists should be about 2MB then
// . then lookup cityid in g_timezones to get timezone
PlaceDesc *getNearestCity_new ( float  lat , 
				float  lon , 
				int32_t   niceness ,
				float *distInMilesSquared ) {

	// . radius is 10 miles, put miles into degrees
	// . when it was 5 we did not get "Santa Fe" for an event, it
	//   thought it was in "Agua Fria"
	float radius = 10.0 / 69.0;
	PlaceDesc *pd = NULL;
	// how many cities we got?
	int32_t  ni      = s_cityLatList.length() / 4;
	int32_t *latList = (int32_t *)s_cityLatList.getBufStart();

 tryagain:

	int32_t step = ni / 2;
	// get lat boundaries using bstep
	int32_t start = ni / 2;
	// do the bstepping
	for ( ; ; ) {
		// get that city
		int32_t cityOffset = latList[start];
		// get PlaceDesc
		pd = (PlaceDesc *)(g_pbuf + cityOffset);
		// increase resolution for next round
		step /= 2;
		// step it down?
		if      ( lat < pd->m_lat ) start -= step;
		// use " - radius" here as well to avoid infinite loop?
		else if ( lat > pd->m_lat ) start += step;
		// ok, we are in range, done
		else break;
		// avoid breaching!
		if ( start < 0     ) { start = 0     ; break; }
		if ( start >= ni ) { start = ni-1; break; }
		// stop if we hit steps of 0
		if ( step <= 0 ) break;
	}

	int32_t lata = start;
	int32_t latb = start;
	int32_t count = 0;
	// TODO: do b-step on these too, takes like 3500 iterations for
	//       both of these loops
	// adjust lata/latb until just out of range
	for ( ; lata > 0 ; lata-- ) {
		int32_t cityOffset = latList[lata];
		pd = (PlaceDesc *)(g_pbuf + cityOffset);
		if ( pd->m_lat < lat - radius ) break;
		count++;
	}
	for ( ; latb < ni ; latb++ ) {
		int32_t cityOffset = latList[latb];
		pd = (PlaceDesc *)(g_pbuf + cityOffset);
		if ( pd->m_lat > lat + radius ) break;
		count++;
	}

	//
	// first do a loop to get the absolutely closest place
	// to this lat/lon regardless of population
	//
	float min1 = -1.0;
	PlaceDesc *minpd1 = NULL;
	// add in the lat cities
	for ( int32_t i = lata ; i <= latb ; i++ ) {
		// break?
		if ( i >= ni ) break;
		// breathe
		QUICKPOLL(niceness);
		// get that city
		int32_t cityOffset = latList[i];
		pd = (PlaceDesc *)(g_pbuf + cityOffset);
		// sanity check
		if ( cityOffset > g_pbufSize ) { char *xx=NULL;*xx=0; }
		if ( cityOffset < 0          ) { char *xx=NULL;*xx=0; }
		// just compute distance
		float latDiff = pd->m_lat - lat;
		float lonDiff = pd->m_lon - lon;
		// add up
		float dist = latDiff*latDiff + lonDiff*lonDiff;
		// min?
		if ( dist > min1 && minpd1 ) continue;
		// set it
		min1   = dist;
		minpd1 = pd;
	}


	//
	// then do a second loop to find the closest place, taking population
	// into account, but also keeping the state/country the same
	// as in "minpd1"
	//
	float min2 = -1.0;
	PlaceDesc *minpd2 = NULL;
	// add in the lat cities
	for ( int32_t i = lata ; i <= latb ; i++ ) {
		// break?
		if ( i >= ni ) break;
		// breathe
		QUICKPOLL(niceness);
		// get that city
		int32_t cityOffset = latList[i];
		pd = (PlaceDesc *)(g_pbuf + cityOffset);
		// just compute distance
		float latDiff = pd->m_lat - lat;
		float lonDiff = pd->m_lon - lon;
		// convert into miles
		latDiff *= 69;
		lonDiff *= 69;
		// must match that of minpd1's state and country
		if ( pd->m_adm1[0] != minpd1->m_adm1[0] ) continue;
		if ( pd->m_adm1[1] != minpd1->m_adm1[1] ) continue;
		if ( pd->m_crid    != minpd1->m_crid    ) continue;
		// but consider the radius of the city to be up to 10 miles
		// for a population of 1M people...
		// one degree is 69.0 miles
		float pop = pd->m_population;
		// restrict to 500k people
		if ( pop > 500000.0 ) pop = 500000.0;
		// compute the city radius, can be up to 33*33 miles
		float cityRadiusSquared = (1000.0 * pop) / 500000.0;
		// square that
		//float cityRadiusSquared = cityRadius * cityRadius;
		// add up
		float dist = latDiff*latDiff + lonDiff*lonDiff;
		// subtract
		dist -= cityRadiusSquared;
		// DEBUG
		//if ( dist < 200 )
		//	log("places: city=%s dist=%.01f rad=%.01f",
		//	    pd->getOfficialName(),dist,cityRadiusSquared);
		// min?
		if ( dist > min2 && minpd2 ) continue;
		// set it
		min2   = dist;
		minpd2 = pd;
	}

	// must have one
	if ( ! minpd2 ) {
		// note it
		log("addr: what the hell.");
		// increase stripe width
		radius += 10.0;
		// try again
		goto tryagain;
	}

	// debug point  -- undo this later
	//if ( ! strcmp(minpd2->getOfficialName(),"Agua Fria") )
	//	log("hey");


	if ( distInMilesSquared ) *distInMilesSquared = min2;

	// return that then
	return minpd2;
}


int latcmp_new ( const void *arg1 , const void *arg2 ) {
	int32_t off1 = *(int32_t *)arg1;
	int32_t off2 = *(int32_t *)arg2;
	// get the addresses
	PlaceDesc *cd1;
	PlaceDesc *cd2;
	cd1 = (PlaceDesc *)(g_pbuf + off1);
	cd2 = (PlaceDesc *)(g_pbuf + off2);
	// simple compare
	if ( cd1->m_lat < cd2->m_lat ) return -1;
	if ( cd1->m_lat > cd2->m_lat ) return  1;
	return 0;
}

bool testCityList ( ) {
	PlaceDesc *pd;
	char *name;

	pd = getNearestCity_new ( 35.596035,-106.052246,0,NULL);
	if ( ! pd ) { char *xx=NULL;*xx=0; }
	name = pd->m_officialNameOffset + g_pbuf;
	if ( strcmp ( name , "Santa Fe" ) ) { char *xx=NULL;*xx=0; }

	// try this. make sure this is albuquerque
	pd = getNearestCity_new ( 35.08449 ,-106.6511,0,NULL);
	if ( ! pd ) { char *xx=NULL;*xx=0; }
	name = pd->m_officialNameOffset + g_pbuf;
	if ( strcmp ( name , "Albuquerque" ) ) { char *xx=NULL;*xx=0; }

	return true;
}

// . our data is used by getNearestCityId
// . about 123k cities, sort them by lat in one list, lon in the other
// . 4 bytes per entry, we are talking 1.2MB for both lists
bool initCityLists_new ( ) {

	// bail if not indexing events
	//if ( ! g_conf.m_indexEventsOnly ) return true;
	return true;

	log ("places: loading citylatlist.dat");

	// first try to load the list of city offsets into g_pbuf 
	// which are pre-sorted
	if ( s_cityLatList.fillFromFile(g_hostdb.m_dir,"citylatlist.dat")>=1) {
		// test it out right quick
		testCityList();
		return true;
	}

	// scan the buffer of placeDescriptors
	PlaceDesc *pd    = (PlaceDesc *) g_pbuf;
	PlaceDesc *pdend ;//= (PlaceDesc *)(g_pbuf + g_pbufSize);

	// find the real end of it!
	for ( pdend = pd ; ; pdend++ ) {
		// stop if we enter the name buf space
		if ( ((char *)pdend)[0] == 'u' &&
		     ((char *)pdend)[1] == 'n' &&
		     ! strcmp((char *)pdend,"unknown name" ) )
			break;
	}

	// count how many cities we got
	int32_t cityCount = 0;
	for ( ; pd < pdend ; pd++ ) 
		if ( pd->m_flags & PDF_CITY ) cityCount++;

	// . alloc for the "ptrs" which will really be offsets into g_pbuf
	// . use offsets so we can save/load to/from disk easily
	int32_t need = cityCount * 4;
	// alloc it
	if ( ! s_cityLatList.reserve ( need ) ) return false;
	// point into it so we can fill it up
	int32_t *latList = (int32_t *)s_cityLatList.getBufStart();
	int32_t nc = 0;

	pd = (PlaceDesc *)g_pbuf;
	// scan the cities again
	for ( ; pd < pdend ; pd++ ) {
		// skip if not city
		if ( ! (pd->m_flags & PDF_CITY ) ) continue;
		// get offset
		int32_t cityOffset = ((char *)pd) - g_pbuf;
		// add to the list
		latList[nc++] = cityOffset;
	}
	// sanity
	if ( cityCount != nc ) { char *xx=NULL;*xx=0; }
	// now sort each list
	gbqsort ( latList , nc , 4 , latcmp_new , 0 );

	// update length
	s_cityLatList.m_length = nc * 4;

	// test it out right quick
	testCityList();

	log ("places: saving citylatlist.dat");
	// save it
	s_cityLatList.saveToFile(g_hostdb.m_dir,"citylatlist.dat");

	return true;
}

