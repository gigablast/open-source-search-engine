// Matt Wells, copyright Jul 2001

#include "gb-include.h"

#include "StopWords.h"
#include "HashTableX.h"
#include "Threads.h"

class Abbr {
public:
	char *m_str;
	// MUST it have a word after it????
	char  m_hasWordAfter;
};

// . i shrunk this list a lot
// . see backups for the hold list
static class Abbr s_abbrs99[] = {
	{"hghway",0},//highway
	{"hway",0},//highway
	{"hwy",0},//highway
	{"ln",0}, // lane
	{"mil",0}, // military
	{"pkway",0}, // parkway
	{"pkwy",0},  // parkway
	{"lp",0}, // Loop
	{"phd",0}, // Loop
	{"demon",0}, // demonstration
	{"alz",0}, // alzheimer's

	{"lang",0}, // language
	{"gr",0}, // grade(s) "xmas concert gr. 1-5"
	{"vars",0}, // varsity
	{"avg",0}, // average
	{"amer",0}, // america

	{"bet",0}, // between 18th and 19th for piratecatradio.com
	{"nr",0}, // near 6th street = nr. 6th street
	{"appt",0},
	{"tel",1},
	{"intl",0},
	{"div",1}, // div. II

	{"int",1}, // Intermediate Dance
	{"beg",1}, // Beginner Dance
	{"adv",1}, // Advanced Dance

	{"feat",1}, // featuring.
	{"tdlr",0}, // toddler
	{"schl",0}, // pre-schl

	// times
	{"am",0}, // unm.edu url puts {"7 am. - 9 am.{" time ranges!
	{"pm",0},
	{"mon",0},
	{"tue",0},
	{"tues",0},
	{"wed",0},
	{"wednes",0},
	{"thu",0},
	{"thur",0},
	{"thurs",0},
	{"fri",0},
	{"sat",0},
	{"sun",0},

	{"Ala",0},
	{"Ariz",0},
	{"Assn",0},
	{"Assoc",0},
	{"asst",0}, // assistant
	{"Atty",0},
	{"Attn",1},
	{"Aug",0},
	{"Ave",0},
	{"Bldg",0},
	{"Bros",0}, // brothers
	{"Blvd",0},
	{"Calif",0},
	{"Capt",1},
	{"Cf",0},
	{"Ch",0},
	{"Co",0},
	{"Col",0},
	{"Colo",0},
	{"Conn",0},
	{"Mfg",0},
	{"Corp",0},
	{"DR",0},
	{"Dec",0},
	{"Dept",0},
	{"Dist",0},
	{"Dr",0},
	{"Drs",0},
	{"Ed",0},
	{"Eq",0},
	{"ext",0}, // extension
	{"FEB",0},
	{"Feb",0},
	{"Fig",0},
	{"Figs",0},
	{"Fla",0},
	{"Ft",1}, // ft. worth texas or feet
	{"Ga",0},
	{"Gen",0},
	{"Gov",0},
	{"HON",0},
	{"Ill",0},
	{"Inc",0},
	{"JR",0},
	{"Jan",0},
	{"Jr",0},
	{"Kan",0},
	//{"Ky",0},
	{"La",0},
	{"Lt",0},
	{"Ltd",0},
	{"MR",1},
	{"MRS",1},
	{"Mar",0},
	{"Mass",0},
	{"Md",0},
	{"Messrs",1},
	{"Mich",0},
	{"Minn",0},
	{"Miss",0},
	{"Mmes",0},
	//{"Mo",0}, no more 2-letter state abbreviations
	{"Mr",1},
	{"Mrs",1},
	{"Ms",1},
	{"Msgr",1},
	{"Mt",1},
	{"NO",0},
	{"No",0},
	{"Nov",0},
	{"Oct",0},
	{"Okla",0},
	{"Op",0},
	{"Ore",0},
	//{"Pa",0},
	{"Pp",0},
	{"Prof",1},
	{"Prop",0},
	{"Rd",0},
	{"Ref",0},
	{"Rep",0},
	{"Reps",0},
	{"Rev",0},
	{"Rte",0},
	{"Sen",0},
	{"Sept",0},
	{"Sr",0},
	{"St",0},
	{"ste",0},
	{"Stat",0},
	{"Supt",0},
	{"Tech",0},
	{"Tex",0},
	{"Va",0},
	{"Vol",0},
	{"Wash",0},
	//{"al",0},
	{"av",0},
	{"ave",0},
	{"ca",0},
	{"cc",0},
	{"chap",0},
	{"cm",0},
	{"cu",0},
	{"dia",0},
	{"dr",0},
	{"eqn",0},
	{"etc",0},
	{"fig",1},
	{"figs",1},
	{"ft",0}, // fort or feet or featuring
	//{"gm",0},
	{"hr",0},
	//{"in",0},
	//{"kc",0},
	{"lb",0},
	{"lbs",0},
	{"mg",0},
	{"ml",0},
	{"mm",0},
	{"mv",0},
	//{"nw",0},
	{"oz",0},
	{"pl",0},
	{"pp",0},
	{"sec",0},
	{"sq",0},
	{"st",0},
	{"vs",1},
	{"yr",0},
	{"yrs",0}, // 3 yrs old
	// middle initials
	{"a",0},
	{"b",0},
	{"c",0},
	{"d",0},
	{"e",0},
	{"f",0},
	{"g",0},
	{"h",0},
	{"i",0},
	{"j",0},
	{"k",0},
	{"l",0},
	{"m",0},
	{"n",0},
	{"o",0},
	{"p",0},
	{"q",0},
	{"r",0},
	{"s",0},
	{"t",0},
	{"u",0},
	{"v",1}, // versus
	{"w",0},
	{"x",0},
	{"y",0},
	{"z",0}
};

static HashTableX s_abbrTable;
static bool       s_abbrInitialized = false;

/*
static bool initTable ( HashTableX *table, char *words[], int32_t size ) {
	// set up the hash table
	if ( ! table->set ( 8 , 4 , size * 2,NULL,0,false,MAX_NICENESS,
			    "abbrtbl") ) 
		return log("build: Could not init abbreviation table.");
	// now add in all the stop words
	int32_t n = (int32_t)size/ sizeof(char *); 
	for ( int32_t i = 0 ; i < n ; i++ ) {
		char      *sw    = words[i];
		//int32_t     swlen = gbstrlen ( sw );
		int64_t  swh   = hash64Lower_utf8 ( sw );
		if ( ! table->addTerm (&swh,i+1) ) return false;
	}
	return true;
}
*/

bool isAbbr ( int64_t h , bool *hasWordAfter ) {
	if ( ! s_abbrInitialized ) {
		// int16_tcut
		HashTableX *t = &s_abbrTable;
		// set up the hash table
		int32_t n = ((int32_t)sizeof(s_abbrs99))/ ((int32_t)sizeof(Abbr));
		if ( ! t->set ( 8,4,n*4, NULL,0,false,MAX_NICENESS,"abbrtbl")) 
			return log("build: Could not init abbrev table.");
		// now add in all the stop words
		for ( int32_t i = 0 ; i < n ; i++ ) {
			char      *sw    = s_abbrs99[i].m_str;
			int64_t  swh   = hash64Lower_utf8 ( sw );
			int32_t val = i + 1;
			if ( ! t->addKey (&swh,&val) ) return false;
		}
		s_abbrInitialized = true;
		// test it
		int64_t h = hash64Lower_utf8("St");
		if ( ! t->isInTable(&h) ) { char *xx=NULL;*xx=0; }
		int32_t sc = s_abbrTable.getScore ( &h );
		if ( sc >= n ) { char *xx=NULL;*xx=0; }
	} 
	// get from table
	int32_t sc = s_abbrTable.getScore ( &h );
	if ( sc <= 0 ) return false;
	if ( hasWordAfter ) *hasWordAfter = s_abbrs99[sc-1].m_hasWordAfter;
	return true;
}		


void resetAbbrTable ( ) {
	s_abbrTable.reset();
}

