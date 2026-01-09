// Matt Wells, copyright Jul 2001

#include "gb-include.h"
#include "gbassert.h"

#include "HashTableX.h"
#include "Threads.h"

#include <array>
#include <string_view>

struct Abbr {
	std::string_view m_str;
	// MUST it have a word after it????
	bool  m_hasWordAfter;
};

// . i shrunk this list a lot
// . see backups for the hold list
constexpr std::array<Abbr, 190> s_abbrs99 = {{
	{"hghway",false},//highway
	{"hway",false},//highway
	{"hwy",false},//highway
	{"ln",false}, // lane
	{"mil",false}, // military
	{"pkway",false}, // parkway
	{"pkwy",false},  // parkway
	{"lp",false}, // Loop
	{"phd",false}, // Loop
	{"demon",false}, // demonstration
	{"alz",false}, // alzheimer's

	{"lang",false}, // language
	{"gr",false}, // grade(s) "xmas concert gr. 1-5"
	{"vars",false}, // varsity
	{"avg",false}, // average
	{"amer",false}, // america

	{"bet",false}, // between 18th and 19th for piratecatradio.com
	{"nr",false}, // near 6th street = nr. 6th street
	{"appt",false},
	{"tel",true},
	{"intl",false},
	{"div",true}, // div. II

	{"int",true}, // Intermediate Dance
	{"beg",true}, // Beginner Dance
	{"adv",true}, // Advanced Dance

	{"feat",true}, // featuring.
	{"tdlr",false}, // toddler
	{"schl",false}, // pre-schl

	// times
	{"am",false}, // unm.edu url puts {"7 am. - 9 am.{" time ranges!
	{"pm",false},
	{"mon",false},
	{"tue",false},
	{"tues",false},
	{"wed",false},
	{"wednes",false},
	{"thu",false},
	{"thur",false},
	{"thurs",false},
	{"fri",false},
	{"sat",false},
	{"sun",false},

	{"Ala",false},
	{"Ariz",false},
	{"Assn",false},
	{"Assoc",false},
	{"asst",false}, // assistant
	{"Atty",false},
	{"Attn",true},
	{"Aug",false},
	{"Ave",false},
	{"Bldg",false},
	{"Bros",false}, // brothers
	{"Blvd",false},
	{"Calif",false},
	{"Capt",true},
	{"Cf",false},
	{"Ch",false},
	{"Co",false},
	{"Col",false},
	{"Colo",false},
	{"Conn",false},
	{"Mfg",false},
	{"Corp",false},
	{"DR",false},
	{"Dec",false},
	{"Dept",false},
	{"Dist",false},
	{"Dr",false},
	{"Drs",false},
	{"Ed",false},
	{"Eq",false},
	{"ext",false}, // extension
	{"FEB",false},
	{"Feb",false},
	{"Fig",false},
	{"Figs",false},
	{"Fla",false},
	{"Ft",true}, // ft. worth texas or feet
	{"Ga",false},
	{"Gen",false},
	{"Gov",false},
	{"HON",false},
	{"Ill",false},
	{"Inc",false},
	{"JR",false},
	{"Jan",false},
	{"Jr",false},
	{"Kan",false},
	{"La",false},
	{"Lt",false},
	{"Ltd",false},
	{"MR",true},
	{"MRS",true},
	{"Mar",false},
	{"Mass",false},
	{"Md",false},
	{"Messrs",true},
	{"Mich",false},
	{"Minn",false},
	{"Miss",false},
	{"Mmes",false},
	//{"Mo",false}, no more 2-letter state abbreviations
	{"Mr",true},
	{"Mrs",true},
	{"Ms",true},
	{"Msgr",true},
	{"Mt",true},
	{"NO",false},
	{"No",false},
	{"Nov",false},
	{"Oct",false},
	{"Okla",false},
	{"Op",false},
	{"Ore",false},
	{"Pp",false},
	{"Prof",true},
	{"Prop",false},
	{"Rd",false},
	{"Ref",false},
	{"Rep",false},
	{"Reps",false},
	{"Rev",false},
	{"Rte",false},
	{"Sen",false},
	{"Sept",false},
	{"Sr",false},
	{"St",false},
	{"ste",false},
	{"Stat",false},
	{"Supt",false},
	{"Tech",false},
	{"Tex",false},
	{"Va",false},
	{"Vol",false},
	{"Wash",false},
	{"av",false},
	{"ave",false},
	{"ca",false},
	{"cc",false},
	{"chap",false},
	{"cm",false},
	{"cu",false},
	{"dia",false},
	{"dr",false},
	{"eqn",false},
	{"etc",false},
	{"fig",true},
	{"figs",true},
	{"ft",false}, // fort or feet or featuring
	{"hr",false},
	{"lb",false},
	{"lbs",false},
	{"mg",false},
	{"ml",false},
	{"mm",false},
	{"mv",false},
	{"oz",false},
	{"pl",false},
	{"pp",false},
	{"sec",false},
	{"sq",false},
	{"st",false},
	{"vs",true},
	{"yr",false},
	{"yrs",false}, // 3 yrs old
	// middle initials
	{"a",false},
	{"b",false},
	{"c",false},
	{"d",false},
	{"e",false},
	{"f",false},
	{"g",false},
	{"h",false},
	{"i",false},
	{"j",false},
	{"k",false},
	{"l",false},
	{"m",false},
	{"n",false},
	{"o",false},
	{"p",false},
	{"q",false},
	{"r",false},
	{"s",false},
	{"t",false},
	{"u",false},
	{"v",true}, // versus
	{"w",false},
	{"x",false},
	{"y",false},
	{"z",false}
}};

static HashTableX s_abbrTable;
static bool       s_abbrInitialized = false;

bool isAbbr ( int64_t h , bool *hasWordAfter ) {
	//TODO is this used in multiple threads??
	if ( ! s_abbrInitialized ) {
		// int16_tcut
		HashTableX *t = &s_abbrTable;
		// set up the hash table
		int32_t n = static_cast<int32_t>(s_abbrs99.size());
		if ( ! t->set ( 8,4,n*4, nullptr,0,false,MAX_NICENESS,"abbrtbl"))
			return log("build: Could not init abbrev table.");
		// now add in all the stop words
		for (const auto &abbr : s_abbrs99) {
			int64_t hash = hash64Lower_utf8(abbr.m_str.data());
			int32_t val = static_cast<int32_t>(&abbr - &s_abbrs99[0]) + 1;
			gbassert(t->addKey(&hash, &val));
		}
		s_abbrInitialized = true;
		// test it
		int64_t st_h = hash64Lower_utf8("St");
		gbassert( t->isInTable(&st_h) );
		int32_t sc = s_abbrTable.getScore ( &st_h );
        gbassert( sc < n );
	}

	int32_t sc = s_abbrTable.getScore(&h);
	if (sc <= 0) return false;

	if (hasWordAfter) *hasWordAfter = s_abbrs99[sc - 1].m_hasWordAfter;
	return true;
}		


void resetAbbrTable ( ) {
	s_abbrTable.reset();
}

