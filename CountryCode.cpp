
#include <sys/types.h>
#include <regex.h>

#include "CountryCode.h"
#include "HashTable.h"
#include "Categories.h"
#include "LanguageIdentifier.h"

// record for unified language/country hash table
typedef union catcountryrec_t {
	int32_t lval;
	struct {
		uint16_t country;
		uint8_t lang;
	} sval;
} catcountryrec_t;

CountryCode g_countryCode;

static HashTable s_catToCountry;

static char * s_countryCode[] = {
	"zz", // Unknown
	"ad", // Principality of Andorra
	"ae", // United Arab Emirates
	"af", // Islamic State of Afghanistan
	"ag", // Antigua and Barbuda
	"ai", // Anguilla
	"al", // Albania
	"am", // Armenia
	"an", // Netherlands Antilles
	"ao", // Angola
	"aq", // Antarctica
	"ar", // Argentina
	"as", // American Samoa
	"at", // Austria
	"au", // Australia
	"aw", // Aruba
	"az", // Azerbaidjan
	"ba", // Bosnia-Herzegovina
	"bb", // Barbados
	"bd", // Bangladesh
	"be", // Belgium
	"bf", // Burkina Faso
	"bg", // Bulgaria
	"bh", // Bahrain
	"bi", // Burundi
	"bj", // Benin
	"bm", // Bermuda
	"bn", // Brunei Darussalam
	"bo", // Bolivia
	"br", // Brazil
	"bs", // Bahamas
	"bt", // Bhutan
	"bv", // Bouvet Island
	"bw", // Botswana
	"by", // Belarus
	"bz", // Belize
	"ca", // Canada
	"cc", // Cocos (Keeling) Islands
	"cf", // Central African Republic
	"cd", // The Democratic Republic of the Congo
	"cg", // Congo
	"ch", // Switzerland
	"ci", // Ivory Coast (Cote D'Ivoire)
	"ck", // Cook Islands
	"cl", // Chile
	"cm", // Cameroon
	"cn", // China
	"co", // Colombia
	"cr", // Costa Rica
	"cs", // Former Czechoslovakia
	"cu", // Cuba
	"cv", // Cape Verde
	"cx", // Christmas Island
	"cy", // Cyprus
	"cz", // Czech Republic
	"de", // Germany
	"dj", // Djibouti
	"dk", // Denmark
	"dm", // Dominica
	"do", // Dominican Republic
	"dz", // Algeria
	"ec", // Ecuador
	"ee", // Estonia
	"eg", // Egypt
	"eh", // Western Sahara
	"er", // Eritrea
	"es", // Spain
	"et", // Ethiopia
	"fi", // Finland
	"fj", // Fiji
	"fk", // Falkland Islands
	"fm", // Micronesia
	"fo", // Faroe Islands
	"fr", // France
	"fx", // France (European Territory)
	"ga", // Gabon
	"gb", // Great Britain
	"gd", // Grenada
	"ge", // Georgia
	"gf", // French Guyana
	"gh", // Ghana
	"gi", // Gibraltar
	"gl", // Greenland
	"gm", // Gambia
	"gn", // Guinea
	"gp", // Guadeloupe (French)
	"gq", // Equatorial Guinea
	"gr", // Greece
	"gs", // S. Georgia & S. Sandwich Isls.
	"gt", // Guatemala
	"gu", // Guam (USA)
	"gw", // Guinea Bissau
	"gy", // Guyana
	"hk", // Hong Kong
	"hm", // Heard and McDonald Islands
	"hn", // Honduras
	"hr", // Croatia
	"ht", // Haiti
	"hu", // Hungary
	"id", // Indonesia
	"ie", // Ireland
	"il", // Israel
	"in", // India
	"io", // British Indian Ocean Territory
	"iq", // Iraq
	"ir", // Iran
	"is", // Iceland
	"it", // Italy
	"jm", // Jamaica
	"jo", // Jordan
	"jp", // Japan
	"ke", // Kenya
	"kg", // Kyrgyz Republic (Kyrgyzstan)
	"kh", // Kingdom of Cambodia
	"ki", // Kiribati
	"km", // Comoros
	"kn", // Saint Kitts & Nevis Anguilla
	"kp", // North Korea
	"kr", // South Korea
	"kw", // Kuwait
	"ky", // Cayman Islands
	"kz", // Kazakhstan
	"la", // Laos
	"lb", // Lebanon
	"lc", // Saint Lucia
	"li", // Liechtenstein
	"lk", // Sri Lanka
	"lr", // Liberia
	"ls", // Lesotho
	"lt", // Lithuania
	"lu", // Luxembourg
	"lv", // Latvia
	"ly", // Libya
	"ma", // Morocco
	"mc", // Monaco
	"md", // Moldavia
	"mg", // Madagascar
	"mh", // Marshall Islands
	"mk", // Macedonia
	"ml", // Mali
	"mm", // Myanmar
	"mn", // Mongolia
	"mo", // Macau
	"mp", // Northern Mariana Islands
	"mq", // Martinique (French)
	"mr", // Mauritania
	"ms", // Montserrat
	"mt", // Malta
	"mu", // Mauritius
	"mv", // Maldives
	"mw", // Malawi
	"mx", // Mexico
	"my", // Malaysia
	"mz", // Mozambique
	"na", // Namibia
	"nc", // New Caledonia (French)
	"ne", // Niger
	"nf", // Norfolk Island
	"ng", // Nigeria
	"ni", // Nicaragua
	"nl", // Netherlands
	"no", // Norway
	"np", // Nepal
	"nr", // Nauru
	"nt", // Neutral Zone
	"nu", // Niue
	"nz", // New Zealand
	"om", // Oman
	"pa", // Panama
	"pe", // Peru
	"pf", // Polynesia (French)
	"pg", // Papua New Guinea
	"ph", // Philippines
	"pk", // Pakistan
	"pl", // Poland
	"pm", // Saint Pierre and Miquelon
	"pn", // Pitcairn Island
	"pr", // Puerto Rico
	"pt", // Portugal
	"pw", // Palau
	"py", // Paraguay
	"qa", // Qatar
	"re", // Reunion (French)
	"ro", // Romania
	"ru", // Russian Federation
	"rw", // Rwanda
	"sa", // Saudi Arabia
	"sb", // Solomon Islands
	"sc", // Seychelles
	"sd", // Sudan
	"se", // Sweden
	"sg", // Singapore
	"sh", // Saint Helena
	"si", // Slovenia
	"sj", // Svalbard and Jan Mayen Islands
	"sk", // Slovak Republic
	"sl", // Sierra Leone
	"sm", // San Marino
	"sn", // Senegal
	"so", // Somalia
	"sr", // Suriname
	"st", // Saint Tome (Sao Tome) and Principe
	"su", // Former USSR
	"sv", // El Salvador
	"sy", // Syria
	"sz", // Swaziland
	"tc", // Turks and Caicos Islands
	"td", // Chad
	"tf", // French Southern Territories
	"tg", // Togo
	"th", // Thailand
	"tj", // Tadjikistan
	"tk", // Tokelau
	"tm", // Turkmenistan
	"tn", // Tunisia
	"to", // Tonga
	"tp", // East Timor
	"tr", // Turkey
	"tt", // Trinidad and Tobago
	"tv", // Tuvalu
	"tw", // Taiwan
	"tz", // Tanzania
	"ua", // Ukraine
	"ug", // Uganda
	"uk", // United Kingdom
	"um", // USA Minor Outlying Islands
	"us", // United States
	"uy", // Uruguay
	"uz", // Uzbekistan
	"va", // Holy See (Vatican City State)
	"vc", // Saint Vincent & Grenadines
	"ve", // Venezuela
	"vg", // Virgin Islands (British)
	"vi", // Virgin Islands (USA)
	"vn", // Vietnam
	"vu", // Vanuatu
	"wf", // Wallis and Futuna Islands
	"ws", // Samoa
	"ye", // Yemen
	"yt", // Mayotte
	"yu", // Yugoslavia
	"za", // South Africa
	"zm", // Zambia
	"zr", // Zaire
	"zw",  // Zimbabwe


	// political entities
	"bl" , // saint bathelemy
	"gg" , // saint martin
	"mf" ,
	"im" ,  // isle of man
	"je" ,   // jersey
	"me" ,  // montenegro
	"ps" , // gaza strip
	"rs" , // serbia
	"tl" // east timor REPEAT!!
};

// map a country id to the two letter country abbr
char *getCountryCode ( uint8_t crid ) {
	return s_countryCode[crid];
}

// get the id from a 2 character country code
uint8_t getCountryId ( char *cc ) {
	static bool s_init = false;
	static char buf[2000];
	static HashTableX ht;
	char tmp[4];
	if ( ! s_init ) {
		s_init = true;
		// hash them up
		ht.set ( 4 , 1 , -1,buf,2000,false,MAX_NICENESS,"ctryids");
		// now add in all the country codes
		int32_t n = (int32_t) sizeof(s_countryCode) / sizeof(char *); 
		for ( int32_t i = 0 ; i < n ; i++ ) {
			char *s    = (char *)s_countryCode[i];
			//int32_t  slen = gbstrlen ( s );
			// sanity check
			if ( !s[0] || !s[1] || s[2]) { char *xx=NULL;*xx=0; }
			// map it to a 4 byte key
			tmp[0]=s[0];
			tmp[1]=s[1];
			tmp[2]=0;
			tmp[3]=0;
			// a val of 0 does not mean empty in HashTableX,
			// that is an artifact of HashTableT
			uint8_t val = i; // +1;
			// add 1 cuz 0 means lang unknown
			if ( ! ht.addKey ( tmp , &val ) ) {
				char *xx=NULL;*xx=0; }
		}
	}
	// lookup
	tmp[0]=to_lower_a(cc[0]);
	tmp[1]=to_lower_a(cc[1]);
	tmp[2]=0;
	tmp[3]=0;
	int32_t slot = ht.getSlot ( tmp );
	if ( slot < 0 ) return 0;
	void *val = ht.getValueFromSlot ( slot );
	return *(uint8_t *)val ;
}

static const char *s_countryName[] = {
	"Unknown",
	"Principality of Andorra",
	"United Arab Emirates",
	"Islamic State of Afghanistan",
	"Antigua and Barbuda",
	"Anguilla",
	"Albania",
	"Armenia",
	"Netherlands Antilles",
	"Angola",
	"Antarctica",
	"Argentina",
	"American Samoa",
	"Austria",
	"Australia",
	"Aruba",
	"Azerbaidjan",
	"Bosnia-Herzegovina",
	"Barbados",
	"Bangladesh",
	"Belgium",
	"Burkina Faso",
	"Bulgaria",
	"Bahrain",
	"Burundi",
	"Benin",
	"Bermuda",
	"Brunei Darussalam",
	"Bolivia",
	"Brazil",
	"Bahamas",
	"Bhutan",
	"Bouvet Island",
	"Botswana",
	"Belarus",
	"Belize",
	"Canada",
	"Cocos (Keeling) Islands",
	"Central African Republic",
	"The Democratic Republic of the Congo",
	"Congo",
	"Switzerland",
	"Ivory Coast (Cote D'Ivoire)",
	"Cook Islands",
	"Chile",
	"Cameroon",
	"China",
	"Colombia",
	"Costa Rica",
	"Former Czechoslovakia",
	"Cuba",
	"Cape Verde",
	"Christmas Island",
	"Cyprus",
	"Czech Republic",
	"Germany",
	"Djibouti",
	"Denmark",
	"Dominica",
	"Dominican Republic",
	"Algeria",
	"Ecuador",
	"Estonia",
	"Egypt",
	"Western Sahara",
	"Eritrea",
	"Spain",
	"Ethiopia",
	"Finland",
	"Fiji",
	"Falkland Islands",
	"Micronesia",
	"Faroe Islands",
	"France",
	"France (European Territory)",
	"Gabon",
	"Great Britain",
	"Grenada",
	"Georgia",
	"French Guyana",
	"Ghana",
	"Gibraltar",
	"Greenland",
	"Gambia",
	"Guinea",
	"Guadeloupe (French)",
	"Equatorial Guinea",
	"Greece",
	"S. Georgia & S. Sandwich Isls.",
	"Guatemala",
	"Guam (USA)",
	"Guinea Bissau",
	"Guyana",
	"Hong Kong",
	"Heard and McDonald Islands",
	"Honduras",
	"Croatia",
	"Haiti",
	"Hungary",
	"Indonesia",
	"Ireland",
	"Israel",
	"India",
	"British Indian Ocean Territory",
	"Iraq",
	"Iran",
	"Iceland",
	"Italy",
	"Jamaica",
	"Jordan",
	"Japan",
	"Kenya",
	"Kyrgyz Republic (Kyrgyzstan)",
	"Kingdom of Cambodia",
	"Kiribati",
	"Comoros",
	"Saint Kitts & Nevis Anguilla",
	"North Korea",
	"South Korea",
	"Kuwait",
	"Cayman Islands",
	"Kazakhstan",
	"Laos",
	"Lebanon",
	"Saint Lucia",
	"Liechtenstein",
	"Sri Lanka",
	"Liberia",
	"Lesotho",
	"Lithuania",
	"Luxembourg",
	"Latvia",
	"Libya",
	"Morocco",
	"Monaco",
	"Moldavia",
	"Madagascar",
	"Marshall Islands",
	"Macedonia",
	"Mali",
	"Myanmar",
	"Mongolia",
	"Macau",
	"Northern Mariana Islands",
	"Martinique (French)",
	"Mauritania",
	"Montserrat",
	"Malta",
	"Mauritius",
	"Maldives",
	"Malawi",
	"Mexico",
	"Malaysia",
	"Mozambique",
	"Namibia",
	"New Caledonia (French)",
	"Niger",
	"Norfolk Island",
	"Nigeria",
	"Nicaragua",
	"Netherlands",
	"Norway",
	"Nepal",
	"Nauru",
	"Neutral Zone",
	"Niue",
	"New Zealand",
	"Oman",
	"Panama",
	"Peru",
	"Polynesia (French)",
	"Papua New Guinea",
	"Philippines",
	"Pakistan",
	"Poland",
	"Saint Pierre and Miquelon",
	"Pitcairn Island",
	"Puerto Rico",
	"Portugal",
	"Palau",
	"Paraguay",
	"Qatar",
	"Reunion (French)",
	"Romania",
	"Russian Federation",
	"Rwanda",
	"Saudi Arabia",
	"Solomon Islands",
	"Seychelles",
	"Sudan",
	"Sweden",
	"Singapore",
	"Saint Helena",
	"Slovenia",
	"Svalbard and Jan Mayen Islands",
	"Slovak Republic",
	"Sierra Leone",
	"San Marino",
	"Senegal",
	"Somalia",
	"Suriname",
	"Saint Tome (Sao Tome) and Principe",
	"Former USSR",
	"El Salvador",
	"Syria",
	"Swaziland",
	"Turks and Caicos Islands",
	"Chad",
	"French Southern Territories",
	"Togo",
	"Thailand",
	"Tadjikistan",
	"Tokelau",
	"Turkmenistan",
	"Tunisia",
	"Tonga",
	"East Timor",
	"Turkey",
	"Trinidad and Tobago",
	"Tuvalu",
	"Taiwan",
	"Tanzania",
	"Ukraine",
	"Uganda",
	"United Kingdom",
	"USA Minor Outlying Islands",
	"United States",
	"Uruguay",
	"Uzbekistan",
	"Holy See (Vatican City State)",
	"Saint Vincent & Grenadines",
	"Venezuela",
	"Virgin Islands (British)",
	"Virgin Islands (USA)",
	"Vietnam",
	"Vanuatu",
	"Wallis and Futuna Islands",
	"Samoa",
	"Yemen",
	"Mayotte",
	"Yugoslavia",
	"South Africa",
	"Zambia",
	"Zaire",
	"Zimbabwe", 

	// political entities
	"Saint Barthelemy"  ,// "bl"
	"Saint Martin" , // "gg"
	"Guadeloupe",
	"Isle of Man" , // "im"
	"Jersey" , // "je"
	"Montenegro" , // "me"
	"Gaza Strip" , // "ps"
	"Serbia" , // "rs"
	"East Timor"
};

static char *s_countryRegexSource[] = {
	"[^a-zA-Z]unknown[^a-zA-Z]",
	"[^a-zA-Z]andorra[^a-zA-Z]",
	"[^a-zA-Z]united[ -_]arab[ -_]emirates[^a-zA-Z]",
	"[^a-zA-Z]islamic[ -_]state[ -_]of[ -_]afghanistan[^a-zA-Z]",
	"[^a-zA-Z]antigua[ -_]and[ -_]barbuda[^a-zA-Z]",
	"[^a-zA-Z]anguilla[^a-zA-Z]",
	"[^a-zA-Z]albania[^a-zA-Z]",
	"[^a-zA-Z]armenia[^a-zA-Z]",
	"[^a-zA-Z]netherlands[ -_]antilles[^a-zA-Z]",
	"[^a-zA-Z]angola[^a-zA-Z]",
	"[^a-zA-Z]antarctica[^a-zA-Z]",
	"[^a-zA-Z]argentina[^a-zA-Z]",
	"[^a-zA-Z]american[ -_]samoa[^a-zA-Z]",
	"[^a-zA-Z]austria[^a-zA-Z]",
	"[^a-zA-Z]australia[^a-zA-Z]",
	"[^a-zA-Z]aruba[^a-zA-Z]",
	"[^a-zA-Z]azerbaidjan[^a-zA-Z]",
	"[^a-zA-Z]bosnia-herzegovina[^a-zA-Z]",
	"[^a-zA-Z]barbados[^a-zA-Z]",
	"[^a-zA-Z]bangladesh[^a-zA-Z]",
	"[^a-zA-Z]belgium[^a-zA-Z]",
	"[^a-zA-Z]burkina[ -_]faso[^a-zA-Z]",
	"[^a-zA-Z]bulgaria[^a-zA-Z]",
	"[^a-zA-Z]bahrain[^a-zA-Z]",
	"[^a-zA-Z]burundi[^a-zA-Z]",
	"[^a-zA-Z]benin[^a-zA-Z]",
	"[^a-zA-Z]bermuda[^a-zA-Z]",
	"[^a-zA-Z]brunei[ -_]darussalam[^a-zA-Z]",
	"[^a-zA-Z]bolivia[^a-zA-Z]",
	"[^a-zA-Z]brazil[^a-zA-Z]",
	"[^a-zA-Z]bahamas[^a-zA-Z]",
	"[^a-zA-Z]bhutan[^a-zA-Z]",
	"[^a-zA-Z]bouvet[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]botswana[^a-zA-Z]",
	"[^a-zA-Z]belarus[^a-zA-Z]",
	"[^a-zA-Z]belize[^a-zA-Z]",
	"[^a-zA-Z]canada[^a-zA-Z]",
	"[^a-zA-Z]cocos[ -_]and[ -_]keeling[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]central[ -_]african[ -_]republic[^a-zA-Z]",
	"[^a-zA-Z]the[ -_]democratic[ -_]republic[ -_]of[ -_]the[ -_]congo[^a-zA-Z]",
	"[^a-zA-Z]congo[^a-zA-Z]",
	"[^a-zA-Z]switzerland[^a-zA-Z]",
	"[^a-zA-Z]ivory[ -_]coast[^a-zA-Z]",
	"[^a-zA-Z]cook[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]chile[^a-zA-Z]",
	"[^a-zA-Z]cameroon[^a-zA-Z]",
	"[^a-zA-Z]china[^a-zA-Z]",
	"[^a-zA-Z]colombia[^a-zA-Z]",
	"[^a-zA-Z]costa[ -_]rica[^a-zA-Z]",
	"[^a-zA-Z]czechoslovakia[^a-zA-Z]",
	"[^a-zA-Z]cuba[^a-zA-Z]",
	"[^a-zA-Z]cape[ -_]verde[^a-zA-Z]",
	"[^a-zA-Z]christmas[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]cyprus[^a-zA-Z]",
	"[^a-zA-Z]czech[ -_]republic[^a-zA-Z]",
	"[^a-zA-Z]germany[^a-zA-Z]",
	"[^a-zA-Z]djibouti[^a-zA-Z]",
	"[^a-zA-Z]denmark[^a-zA-Z]",
	"[^a-zA-Z]dominica[^a-zA-Z]",
	"[^a-zA-Z]dominican[ -_]republic[^a-zA-Z]",
	"[^a-zA-Z]algeria[^a-zA-Z]",
	"[^a-zA-Z]ecuador[^a-zA-Z]",
	"[^a-zA-Z]estonia[^a-zA-Z]",
	"[^a-zA-Z]egypt[^a-zA-Z]",
	"[^a-zA-Z]western[ -_]sahara[^a-zA-Z]",
	"[^a-zA-Z]eritrea[^a-zA-Z]",
	"[^a-zA-Z]spain[^a-zA-Z]",
	"[^a-zA-Z]ethiopia[^a-zA-Z]",
	"[^a-zA-Z]finland[^a-zA-Z]",
	"[^a-zA-Z]fiji[^a-zA-Z]",
	"[^a-zA-Z]falkland[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]micronesia[^a-zA-Z]",
	"[^a-zA-Z]faroe[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]france[^a-zA-Z]",
	"[^a-zA-Z]france[ -_]european[ -_]territory[^a-zA-Z]",
	"[^a-zA-Z]gabon[^a-zA-Z]",
	"[^a-zA-Z]great[ -_]britain[^a-zA-Z]",
	"[^a-zA-Z]grenada[^a-zA-Z]",
	"[^a-zA-Z]georgia[^a-zA-Z]",
	"[^a-zA-Z]french[ -_]guyana[^a-zA-Z]",
	"[^a-zA-Z]ghana[^a-zA-Z]",
	"[^a-zA-Z]gibraltar[^a-zA-Z]",
	"[^a-zA-Z]greenland[^a-zA-Z]",
	"[^a-zA-Z]gambia[^a-zA-Z]",
	"[^a-zA-Z]guinea[^a-zA-Z]",
	"[^a-zA-Z]guadeloupe[^a-zA-Z]",
	"[^a-zA-Z]equatorial[ -_]guinea[^a-zA-Z]",
	"[^a-zA-Z]greece[^a-zA-Z]",
	"[^a-zA-Z]s.[ -_]georgia[ -_]&[ -_]s.[ -_]sandwich[ -_]isls.[^a-zA-Z]",
	"[^a-zA-Z]guatemala[^a-zA-Z]",
	"[^a-zA-Z]guam[^a-zA-Z]",
	"[^a-zA-Z]guinea[ -_]bissau[^a-zA-Z]",
	"[^a-zA-Z]guyana[^a-zA-Z]",
	"[^a-zA-Z]hong[ -_]kong[^a-zA-Z]",
	"[^a-zA-Z]heard[ -_]and[ -_]mcdonald[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]honduras[^a-zA-Z]",
	"[^a-zA-Z]croatia[^a-zA-Z]",
	"[^a-zA-Z]haiti[^a-zA-Z]",
	"[^a-zA-Z]hungary[^a-zA-Z]",
	"[^a-zA-Z]indonesia[^a-zA-Z]",
	"[^a-zA-Z]ireland[^a-zA-Z]",
	"[^a-zA-Z]israel[^a-zA-Z]",
	"[^a-zA-Z]india[^a-zA-Z]",
	"[^a-zA-Z]british[ -_]indian[ -_]ocean[ -_]territory[^a-zA-Z]",
	"[^a-zA-Z]iraq[^a-zA-Z]",
	"[^a-zA-Z]iran[^a-zA-Z]",
	"[^a-zA-Z]iceland[^a-zA-Z]",
	"[^a-zA-Z]italy[^a-zA-Z]",
	"[^a-zA-Z]jamaica[^a-zA-Z]",
	"[^a-zA-Z]jordan[^a-zA-Z]",
	"[^a-zA-Z]japan[^a-zA-Z]",
	"[^a-zA-Z]kenya[^a-zA-Z]",
	"[^a-zA-Z](kyrgyz[ -_]republic)|(kyrgyzstan)[^a-zA-Z]",
	"[^a-zA-Z]kingdom[ -_]of[ -_]cambodia[^a-zA-Z]",
	"[^a-zA-Z]kiribati[^a-zA-Z]",
	"[^a-zA-Z]comoros[^a-zA-Z]",
	"[^a-zA-Z]saint[ -_]kitts[ -_]&[ -_]nevis[ -_]anguilla[^a-zA-Z]",
	"[^a-zA-Z]north[ -_]korea[^a-zA-Z]",
	"[^a-zA-Z]south[ -_]korea[^a-zA-Z]",
	"[^a-zA-Z]kuwait[^a-zA-Z]",
	"[^a-zA-Z]cayman[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]kazakhstan[^a-zA-Z]",
	"[^a-zA-Z]laos[^a-zA-Z]",
	"[^a-zA-Z]lebanon[^a-zA-Z]",
	"[^a-zA-Z]saint[ -_]lucia[^a-zA-Z]",
	"[^a-zA-Z]liechtenstein[^a-zA-Z]",
	"[^a-zA-Z]sri[ -_]lanka[^a-zA-Z]",
	"[^a-zA-Z]liberia[^a-zA-Z]",
	"[^a-zA-Z]lesotho[^a-zA-Z]",
	"[^a-zA-Z]lithuania[^a-zA-Z]",
	"[^a-zA-Z]luxembourg[^a-zA-Z]",
	"[^a-zA-Z]latvia[^a-zA-Z]",
	"[^a-zA-Z]libya[^a-zA-Z]",
	"[^a-zA-Z]morocco[^a-zA-Z]",
	"[^a-zA-Z]monaco[^a-zA-Z]",
	"[^a-zA-Z]moldavia[^a-zA-Z]",
	"[^a-zA-Z]madagascar[^a-zA-Z]",
	"[^a-zA-Z]marshall[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]macedonia[^a-zA-Z]",
	"[^a-zA-Z]mali[^a-zA-Z]",
	"[^a-zA-Z]myanmar[^a-zA-Z]",
	"[^a-zA-Z]mongolia[^a-zA-Z]",
	"[^a-zA-Z]macau[^a-zA-Z]",
	"[^a-zA-Z]mariana[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]martinique[^a-zA-Z]",
	"[^a-zA-Z]mauritania[^a-zA-Z]",
	"[^a-zA-Z]montserrat[^a-zA-Z]",
	"[^a-zA-Z]malta[^a-zA-Z]",
	"[^a-zA-Z]mauritius[^a-zA-Z]",
	"[^a-zA-Z]maldives[^a-zA-Z]",
	"[^a-zA-Z]malawi[^a-zA-Z]",
	"[^a-zA-Z]mexico[^a-zA-Z]",
	"[^a-zA-Z]malaysia[^a-zA-Z]",
	"[^a-zA-Z]mozambique[^a-zA-Z]",
	"[^a-zA-Z]namibia[^a-zA-Z]",
	"[^a-zA-Z]new[ -_]caledonia[^a-zA-Z]",
	"[^a-zA-Z]niger[^a-zA-Z]",
	"[^a-zA-Z]norfolk[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]nigeria[^a-zA-Z]",
	"[^a-zA-Z]nicaragua[^a-zA-Z]",
	"[^a-zA-Z]netherlands[^a-zA-Z]",
	"[^a-zA-Z]norway[^a-zA-Z]",
	"[^a-zA-Z]nepal[^a-zA-Z]",
	"[^a-zA-Z]nauru[^a-zA-Z]",
	"[^a-zA-Z]neutral[ -_]zone[^a-zA-Z]",
	"[^a-zA-Z]niue[^a-zA-Z]",
	"[^a-zA-Z]new[ -_]zealand[^a-zA-Z]",
	"[^a-zA-Z]oman[^a-zA-Z]",
	"[^a-zA-Z]panama[^a-zA-Z]",
	"[^a-zA-Z]peru[^a-zA-Z]",
	"[^a-zA-Z]polynesia[^a-zA-Z]",
	"[^a-zA-Z]papua[ -_]new[ -_]guinea[^a-zA-Z]",
	"[^a-zA-Z]philippines[^a-zA-Z]",
	"[^a-zA-Z]pakistan[^a-zA-Z]",
	"[^a-zA-Z]poland[^a-zA-Z]",
	"[^a-zA-Z](saint[ -_]pierre)|(miquelon)[^a-zA-Z]",
	"[^a-zA-Z]pitcairn[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]puerto[ -_]rico[^a-zA-Z]",
	"[^a-zA-Z]portugal[^a-zA-Z]",
	"[^a-zA-Z]palau[^a-zA-Z]",
	"[^a-zA-Z]paraguay[^a-zA-Z]",
	"[^a-zA-Z]qatar[^a-zA-Z]",
	"[^a-zA-Z]reunion[ -_]island[^a-zA-Z]",
	"[^a-zA-Z]romania[^a-zA-Z]",
	"[^a-zA-Z]russian[ -_]federation[^a-zA-Z]",
	"[^a-zA-Z]rwanda[^a-zA-Z]",
	"[^a-zA-Z]saudi[ -_]arabia[^a-zA-Z]",
	"[^a-zA-Z]solomon[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]seychelles[^a-zA-Z]",
	"[^a-zA-Z]sudan[^a-zA-Z]",
	"[^a-zA-Z]sweden[^a-zA-Z]",
	"[^a-zA-Z]singapore[^a-zA-Z]",
	"[^a-zA-Z]saint[ -_]helena[^a-zA-Z]",
	"[^a-zA-Z]slovenia[^a-zA-Z]",
	"[^a-zA-Z]svalbard[ -_]and[ -_]jan[ -_]mayen[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]slovak[ -_]republic[^a-zA-Z]",
	"[^a-zA-Z]sierra[ -_]leone[^a-zA-Z]",
	"[^a-zA-Z]san[ -_]marino[^a-zA-Z]",
	"[^a-zA-Z]senegal[^a-zA-Z]",
	"[^a-zA-Z]somalia[^a-zA-Z]",
	"[^a-zA-Z]suriname[^a-zA-Z]",
	"[^a-zA-Z]sao[ -_]tome[^a-zA-Z]",
	"[^a-zA-Z]former[ -_]ussr[^a-zA-Z]",
	"[^a-zA-Z]el[ -_]salvador[^a-zA-Z]",
	"[^a-zA-Z]syria[^a-zA-Z]",
	"[^a-zA-Z]swaziland[^a-zA-Z]",
	"[^a-zA-Z]turks[ -_]and[ -_]caicos[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]chad[^a-zA-Z]",
	"[^a-zA-Z]french[ -_]southern[ -_]territories[^a-zA-Z]",
	"[^a-zA-Z]togo[^a-zA-Z]",
	"[^a-zA-Z]thailand[^a-zA-Z]",
	"[^a-zA-Z]tadjikistan[^a-zA-Z]",
	"[^a-zA-Z]tokelau[^a-zA-Z]",
	"[^a-zA-Z]turkmenistan[^a-zA-Z]",
	"[^a-zA-Z]tunisia[^a-zA-Z]",
	"[^a-zA-Z]tonga[^a-zA-Z]",
	"[^a-zA-Z]east[ -_]timor[^a-zA-Z]",
	"[^a-zA-Z]turkey[^a-zA-Z]",
	"[^a-zA-Z](trinidad)|(tobago)[^a-zA-Z]",
	"[^a-zA-Z]tuvalu[^a-zA-Z]",
	"[^a-zA-Z]taiwan[^a-zA-Z]",
	"[^a-zA-Z]tanzania[^a-zA-Z]",
	"[^a-zA-Z]ukraine[^a-zA-Z]",
	"[^a-zA-Z]uganda[^a-zA-Z]",
	"[^a-zA-Z]united[ -_]kingdom[^a-zA-Z]",
	"[^a-zA-Z]usa[ -_]minor[ -_]outlying[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]united[ -_]states[^a-zA-Z]",
	"[^a-zA-Z]uruguay[^a-zA-Z]",
	"[^a-zA-Z]uzbekistan[^a-zA-Z]",
	"[^a-zA-Z]holy[ -_]see[^a-zA-Z]",
	"[^a-zA-Z]saint[ -_]vincent[ -_]&[ -_]grenadines[^a-zA-Z]",
	"[^a-zA-Z]venezuela[^a-zA-Z]",
	"[^a-zA-Z]virgin[ -_]islands[ -_](british)[^a-zA-Z]",
	"[^a-zA-Z]virgin[ -_]islands[ -_](usa)[^a-zA-Z]",
	"[^a-zA-Z]vietnam[^a-zA-Z]",
	"[^a-zA-Z]vanuatu[^a-zA-Z]",
	"[^a-zA-Z]wallis[ -_]and[ -_]futuna[ -_]islands[^a-zA-Z]",
	"[^a-zA-Z]samoa[^a-zA-Z]",
	"[^a-zA-Z]yemen[^a-zA-Z]",
	"[^a-zA-Z]mayotte[^a-zA-Z]",
	"[^a-zA-Z]yugoslavia[^a-zA-Z]",
	"[^a-zA-Z]south[ -_]africa[^a-zA-Z]",
	"[^a-zA-Z]zambia[^a-zA-Z]",
	"[^a-zA-Z]zaire[^a-zA-Z]",
	"[^a-zA-Z]zimbabwe[^a-zA-Z]"
};

// List of languages spoken by country
// THIS IS A GENERATED LIST -- DO NOT EDIT!
// NOTE: if the list of countres changes, this must be regenerated
static uint64_t s_countryLanguages[] = {
	0LL                                                         , // zz
	0LL                                                         , // ad
	(1LL<<langArabic)                                           , // ae
	0LL                                                         , // af
	(1LL<<langEnglish)                                          , // ag
	0LL                                                         , // ai
	0LL                                                         , // al
	0LL                                                         , // am
	(1LL<<langDutch)|(1LL<<langEnglish)                         , // an
	(1LL<<langPortuguese)                                       , // ao
	0LL                                                         , // aq
	(1LL<<langSpanish)                                          , // ar
	0LL                                                         , // as
	(1LL<<langGerman)                                           , // at
	(1LL<<langEnglish)                                          , // au
	(1LL<<langDutch)                                            , // aw
	0LL                                                         , // az
	0LL                                                         , // ba
	(1LL<<langEnglish)                                          , // bb
	0LL                                                         , // bd
	(1LL<<langDutch)|(1LL<<langFrench)|(1LL<<langGerman)        , // be
	(1LL<<langFrench)                                           , // bf
	0LL                                                         , // bg
	(1LL<<langArabic)                                           , // bh
	(1LL<<langFrench)                                           , // bi
	(1LL<<langFrench)                                           , // bj
	0LL                                                         , // bm
	0LL                                                         , // bn
	(1LL<<langSpanish)                                          , // bo
	(1LL<<langPortuguese)                                       , // br
	(1LL<<langEnglish)                                          , // bs
	0LL                                                         , // bt
	0LL                                                         , // bv
	(1LL<<langEnglish)                                          , // bw
	(1LL<<langRussian)                                          , // by
	(1LL<<langEnglish)                                          , // bz
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // ca
	0LL                                                         , // cc
	(1LL<<langFrench)                                           , // cf
	0LL                                                         , // cd
	(1LL<<langFrench)                                           , // cg
	(1LL<<langFrench)|(1LL<<langGerman)|(1LL<<langItalian)      , // ch
	0LL                                                         , // ci
	0LL                                                         , // ck
	(1LL<<langSpanish)                                          , // cl
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // cm
	(1LL<<langChineseTrad)|(1LL<<langEnglish)|(1LL<<langKorean)|(1LL<<langPortuguese), // cn
	(1LL<<langSpanish)                                          , // co
	(1LL<<langSpanish)                                          , // cr
	0LL                                                         , // cs
	(1LL<<langSpanish)                                          , // cu
	(1LL<<langPortuguese)                                       , // cv
	0LL                                                         , // cx
	(1LL<<langGreek)                                            , // cy
	0LL                                                         , // cz
	(1LL<<langGerman)                                           , // de
	(1LL<<langArabic)|(1LL<<langFrench)                         , // dj
	0LL                                                         , // dk
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // dm
	(1LL<<langSpanish)                                          , // do
	(1LL<<langArabic)                                           , // dz
	(1LL<<langSpanish)                                          , // ec
	0LL                                                         , // ee
	(1LL<<langArabic)                                           , // eg
	(1LL<<langArabic)                                           , // eh
	(1LL<<langArabic)                                           , // er
	(1LL<<langSpanish)                                          , // es
	0LL                                                         , // et
	(1LL<<langFinnish)|(1LL<<langSwedish)                       , // fi
	(1LL<<langEnglish)|(1LL<<langHindi)                         , // fj
	0LL                                                         , // fk
	(1LL<<langEnglish)                                          , // fm
	0LL                                                         , // fo
	(1LL<<langFrench)                                           , // fr
	0LL                                                         , // fx
	(1LL<<langFrench)                                           , // ga
	0LL                                                         , // gb
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // gd
	0LL                                                         , // ge
	(1LL<<langFrench)                                           , // gf
	(1LL<<langEnglish)                                          , // gh
	0LL                                                         , // gi
	0LL                                                         , // gl
	(1LL<<langEnglish)                                          , // gm
	(1LL<<langFrench)                                           , // gn
	0LL                                                         , // gp
	(1LL<<langFrench)|(1LL<<langPortuguese)|(1LL<<langSpanish)  , // gq
	(1LL<<langGreek)                                            , // gr
	0LL                                                         , // gs
	(1LL<<langSpanish)                                          , // gt
	0LL                                                         , // gu
	0LL                                                         , // gw
	(1LL<<langEnglish)                                          , // gy
	(1LL<<langChineseTrad)|(1LL<<langEnglish)                   , // hk
	0LL                                                         , // hm
	(1LL<<langSpanish)                                          , // hn
	(1LL<<langItalian)                                          , // hr
	(1LL<<langFrench)                                           , // ht
	0LL                                                         , // hu
	(1LL<<langChineseTrad)|(1LL<<langIndonesian)                , // id
	(1LL<<langEnglish)                                          , // ie
	(1LL<<langArabic)|(1LL<<langHebrew)                         , // il
	(1LL<<langEnglish)|(1LL<<langHindi)                         , // in
	0LL                                                         , // io
	(1LL<<langArabic)                                           , // iq
	0LL                                                         , // ir
	0LL                                                         , // is
	(1LL<<langFrench)|(1LL<<langGerman)|(1LL<<langItalian)      , // it
	(1LL<<langEnglish)                                          , // jm
	(1LL<<langArabic)                                           , // jo
	(1LL<<langJapanese)                                         , // jp
	(1LL<<langEnglish)                                          , // ke
	0LL                                                         , // kg
	0LL                                                         , // kh
	(1LL<<langEnglish)                                          , // ki
	(1LL<<langArabic)|(1LL<<langFrench)                         , // km
	0LL                                                         , // kn
	(1LL<<langKorean)                                           , // kp
	(1LL<<langKorean)                                           , // kr
	(1LL<<langArabic)                                           , // kw
	0LL                                                         , // ky
	(1LL<<langRussian)                                          , // kz
	0LL                                                         , // la
	(1LL<<langArabic)                                           , // lb
	(1LL<<langFrench)                                           , // lc
	(1LL<<langGerman)                                           , // li
	0LL                                                         , // lk
	(1LL<<langEnglish)                                          , // lr
	(1LL<<langEnglish)                                          , // ls
	0LL                                                         , // lt
	(1LL<<langFrench)|(1LL<<langGerman)                         , // lu
	0LL                                                         , // lv
	(1LL<<langArabic)                                           , // ly
	(1LL<<langArabic)                                           , // ma
	(1LL<<langFrench)                                           , // mc
	0LL                                                         , // md
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // mg
	0LL                                                         , // mh
	0LL                                                         , // mk
	(1LL<<langFrench)                                           , // ml
	0LL                                                         , // mm
	0LL                                                         , // mn
	(1LL<<langChineseTrad)|(1LL<<langPortuguese)                , // mo
	0LL                                                         , // mp
	0LL                                                         , // mq
	(1LL<<langArabic)                                           , // mr
	0LL                                                         , // ms
	(1LL<<langEnglish)                                          , // mt
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // mu
	0LL                                                         , // mv
	(1LL<<langEnglish)                                          , // mw
	(1LL<<langSpanish)                                          , // mx
	0LL                                                         , // my
	(1LL<<langPortuguese)                                       , // mz
	(1LL<<langEnglish)                                          , // na
	0LL                                                         , // nc
	(1LL<<langFrench)                                           , // ne
	0LL                                                         , // nf
	(1LL<<langEnglish)                                          , // ng
	(1LL<<langSpanish)                                          , // ni
	(1LL<<langDutch)                                            , // nl
	(1LL<<langNorwegian)                                        , // no
	0LL                                                         , // np
	0LL                                                         , // nr
	0LL                                                         , // nt
	0LL                                                         , // nu
	(1LL<<langEnglish)                                          , // nz
	(1LL<<langArabic)                                           , // om
	(1LL<<langSpanish)                                          , // pa
	(1LL<<langSpanish)                                          , // pe
	(1LL<<langFrench)                                           , // pf
	(1LL<<langEnglish)                                          , // pg
	(1LL<<langTagalog)|(1LL<<langEnglish)|(1LL<<langSpanish)    , // ph
	(1LL<<langEnglish)                                          , // pk
	(1LL<<langPolish)                                           , // pl
	0LL                                                         , // pm
	0LL                                                         , // pn
	(1LL<<langSpanish)                                          , // pr
	(1LL<<langPortuguese)                                       , // pt
	(1LL<<langEnglish)|(1LL<<langJapanese)                      , // pw
	(1LL<<langSpanish)                                          , // py
	(1LL<<langArabic)                                           , // qa
	0LL                                                         , // re
	0LL                                                         , // ro
	0LL                                                         , // ru
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // rw
	(1LL<<langArabic)                                           , // sa
	(1LL<<langEnglish)                                          , // sb
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // sc
	(1LL<<langArabic)                                           , // sd
	(1LL<<langSwedish)                                          , // se
	(1LL<<langChineseSimp)|(1LL<<langEnglish)                   , // sg
	0LL                                                         , // sh
	(1LL<<langItalian)                                          , // si
	0LL                                                         , // sj
	0LL                                                         , // sk
	(1LL<<langEnglish)                                          , // sl
	(1LL<<langItalian)                                          , // sm
	(1LL<<langFrench)                                           , // sn
	(1LL<<langArabic)                                           , // so
	(1LL<<langDutch)                                            , // sr
	0LL                                                         , // st
	0LL                                                         , // su
	(1LL<<langSpanish)                                          , // sv
	(1LL<<langArabic)                                           , // sy
	(1LL<<langEnglish)                                          , // sz
	0LL                                                         , // tc
	(1LL<<langArabic)|(1LL<<langFrench)                         , // td
	0LL                                                         , // tf
	(1LL<<langFrench)                                           , // tg
	(1LL<<langThai)                                             , // th
	0LL                                                         , // tj
	0LL                                                         , // tk
	0LL                                                         , // tm
	(1LL<<langArabic)                                           , // tn
	(1LL<<langEnglish)                                          , // to
	(1LL<<langPortuguese)                                       , // tp
	0LL                                                         , // tr
	(1LL<<langEnglish)                                          , // tt
	(1LL<<langEnglish)                                          , // tv
	(1LL<<langChineseTrad)                                      , // tw
	0LL                                                         , // tz
	0LL                                                         , // ua
	(1LL<<langEnglish)                                          , // ug
	(1LL<<langEnglish)                                          , // uk
	0LL                                                         , // um
	(1LL<<langEnglish)                                          , // us
	(1LL<<langSpanish)                                          , // uy
	0LL                                                         , // uz
	(1LL<<langItalian)                                          , // va
	(1LL<<langEnglish)                                          , // vc
	(1LL<<langSpanish)                                          , // ve
	(1LL<<langEnglish)                                          , // vg
	(1LL<<langEnglish)                                          , // vi
	(1LL<<langVietnamese)                                       , // vn
	(1LL<<langEnglish)|(1LL<<langFrench)                        , // vu
	0LL                                                         , // wf
	(1LL<<langEnglish)                                          , // ws
	(1LL<<langArabic)                                           , // ye
	(1LL<<langFrench)                                           , // yt
	0LL                                                         , // yu
	(1LL<<langEnglish)                                          , // za
	(1LL<<langEnglish)                                          , // zm
	0LL                                                         , // zr
	(1LL<<langEnglish)                                            // zw
};


static regex_t *s_countryRegex = NULL;

static int s_numCountryCodes = sizeof(s_countryCode)/sizeof(s_countryCode[0]);

CountryCode::CountryCode() {
	m_init = false;
	//reset();
}

CountryCode::~CountryCode() {
	m_abbrToName.reset();
	m_abbrToIndex.reset();
}

// this is initializing, not resetting - mdw
void CountryCode::init(void) {
	uint16_t idx;
	if(m_init) {
		m_abbrToName.reset();
		m_abbrToIndex.reset();
	}
	m_init = true;
	if(!m_abbrToName.set(s_numCountryCodes) ||
			!m_abbrToIndex.set(s_numCountryCodes)) {
		// if we can't allocate memory, then we'll
		// just leave it uninitialized
		m_init = false;
		return;
	}
	for(int x = 0; x < s_numCountryCodes; x++) {
		idx = s_countryCode[x][0];
		idx = idx << 8;
		idx |= s_countryCode[x][1];
		m_abbrToName.addKey(idx, s_countryName[x]);
		m_abbrToIndex.addKey(idx, x);
	}

}

int CountryCode::fillRegexTable(void) {

	if(s_numCountryCodes < 1) return(0);

	// Get mem for dmoz cat regexes
	if(s_countryRegex) freeRegexTable();

	s_countryRegex = (regex_t *)mmalloc(s_numCountryCodes * sizeof(regex_t), "CountryRegex");
	if(!s_countryRegex) {
		s_countryRegex = NULL;
		log(LOG_WARN, "init: Could not get memory for regular expressions.\n");
		return(0);
	}

	// init dmoz cat regexes
	memset(s_countryRegex, 0, sizeof(regex_t) * s_numCountryCodes);
	for(int x = 1; x < s_numCountryCodes; x++) {
		switch(regcomp(&s_countryRegex[x], s_countryRegexSource[x],
					REG_EXTENDED | REG_ICASE | REG_NEWLINE | REG_NOSUB)) {
			case REG_BADBR:
				log( "init: Country regex: %d: Invalid use of back reference operator.", x);
				break;

			case REG_BADPAT:
				log( "init: Country regex: %d: Invalid use of pattern operators such as group or list.", x);
				break;

			case REG_BADRPT:
				log( "init: Country regex: %d: Invalid use of repetition operators.", x);
				break;

			case REG_EBRACE:
				log( "init: Country regex: %d: Un-matched brace interval operators.", x);
				break;

			case REG_EBRACK:
				log( "init: Country regex: %d: Un-matched bracket list operators.", x);
				break;

			case REG_ECOLLATE:
				log( "init: Country regex: %d: Invalid collating element.", x);
				break;

			case REG_ECTYPE:
				log( "init: Country regex: %d: Unknown character class name.", x);
				break;

				// cygwin doesn't like this one
			// case REG_EEND:
			// 	log( "init: Country regex: %d: Non specific error.", x);
			// 	break;

			case REG_EESCAPE:
				log( "init: Country regex: %d: Trailing backslash.", x);
				break;

			case REG_EPAREN:
				log( "init: Country regex: %d: Un-matched parenthesis group operators.", x);
				break;

			case REG_ERANGE:
				log( "init: Country regex: %d: Invalid use of the range operator.", x);
				break;

				// cygwin doesn't like this one
			// case REG_ESIZE:
			// 	log( "init: Country regex: %d: Compiled regular expression requires a pattern buffer larger than 64Kb.", x);
			// 	break;

			case REG_ESPACE:
				log( "init: Country regex: %d: The regex routines ran out of memory.", x);
				break;

			case REG_ESUBREG:
				log( "init: Country regex: %d: Invalid back reference to a subexpression.", x);
				break;
		}
	}
	return(1);
}

void CountryCode::freeRegexTable(void) {
	if(!s_countryRegex) return;
	for(int x = 1; x < s_numCountryCodes; x++)
		regfree(&s_countryRegex[x]);
	mfree(s_countryRegex, s_numCountryCodes * sizeof(regex_t), "CountryRegex");
}

// This is more permissive, the language names are more distinctive
uint8_t s_getLangIdxFromDMOZ(char *topic, int len) {
	if(!len) return(0);
	char buf[2048];
	int limit = len;
	if(limit > 2047) limit = 2047;
	memset(buf, 0, 2048);
	gbmemcpy(buf, topic, limit);
	if(gbstrlen(buf) < 1) return(0);
	for(int x = 2; x < langTagalog; x++) {
		if(x == 5) continue;
		if(strstr(buf, (char *)langToTopic[x]))
			return((uint8_t)x);
	}
	return(0);
}

// Do not call this function lightly, it takes an hour to run
int CountryCode::createHashTable(void) {
	if(!fillRegexTable()) return(0);

	char tmpbuf[2048];
	HashTable ht;
	uint64_t entries = 0UL;
	int32_t catid;
	int32_t numcats = g_categories->m_numCats;
	catcountryrec_t ccr;
	SafeBuf sb(tmpbuf, 2048);

	log( "cat: Creating category country/language table.\n");

	if(!ht.set(2,NULL,0,"ctrycode")) {
		log( "cat: Could not allocate memory for table.\n");
		return(0);
	}
	for(int32_t idx = 0; idx < numcats; idx++) {
		catid = g_categories->m_cats[idx].m_catid;
		sb.reset();
		g_categories->printPathFromId(&sb, catid, true);
		if(!sb.getBufStart()) continue;
		if(!(numcats % 1000))
			log( "init: %"INT32"/%"INT32" Generated %"UINT64" so far...\n",
					numcats,
					idx,
					entries);
		ccr.lval = 0L;
		ccr.sval.country = lookupCountryFromDMOZTopic(sb.getBufStart(), sb.length());
		ccr.sval.lang = s_getLangIdxFromDMOZ(sb.getBufStart(), sb.length());
		if(!ccr.lval) continue;
		if(ccr.sval.lang > 27 || ccr.sval.country > s_numCountryCodes) {
			char *xx = NULL; *xx = 0;
		}
		if(!ht.addKey(catid, ccr.lval)) {
			log( "init: Could not add %"INT32" (%"INT32")\n", catid, ccr.lval);
			continue;
		}
		entries++;
	}

	ht.save(g_hostdb.m_dir, "catcountry.dat");
	log( "Added %"UINT64" country entries from DMOZ to %s/catcountry.dat.\n", entries,g_hostdb.m_dir);
	log( "Slots %"INT32", Used Slots %"INT32".\n", ht.getNumSlots(), ht.getNumSlotsUsed());

	freeRegexTable();
	return(1);
}

bool CountryCode::loadHashTable(void) {
	init();
	if(!m_init) return(false);
	s_catToCountry.reset();
	s_catToCountry.setLabel("ctrycode");
	return(s_catToCountry.load(g_hostdb.m_dir, "catcountry.dat"));
}

void CountryCode::reset ( ) {
	s_catToCountry.reset();
}

int CountryCode::getNumCodes(void) {
	return(s_numCountryCodes);
}

uint16_t CountryCode::getCountryFromDMOZ(int32_t catid) {
	if(!m_init) return(0);
	catcountryrec_t ccr;
	ccr.lval = 0L;
	if(s_catToCountry.getNumSlotsUsed() < 1) return(0);
	int32_t slot = s_catToCountry.getSlot((int32_t)catid);
	if(slot < 0) return(0);
	ccr.lval = s_catToCountry.getValueFromSlot(slot);
	return(ccr.sval.country);
}

uint8_t CountryCode::getLanguageFromDMOZ(int32_t catid) {
	if(!m_init) return(0);
	catcountryrec_t ccr;
	ccr.lval = 0L;
	if(s_catToCountry.getNumSlotsUsed() < 1) return(0);
	int32_t slot = s_catToCountry.getSlot((int32_t)catid);
	if(slot < 0) return(0);
	ccr.lval = s_catToCountry.getValueFromSlot(slot);
	return(ccr.sval.lang);
}

// for table creation
int CountryCode::lookupCountryFromDMOZTopic(const char *catname, int len) {
	if(len < 1) return(0);
	if(!s_countryRegex) return(0);
	char buf[2049];
	if(len > 2047) len = 2047;
	gbmemcpy(buf, catname, len);
	buf[len+1] = 0;
	if(gbstrlen(buf) < 1) return(0);
	for(int x = 1; x < s_numCountryCodes; x++)
		if(!regexec(&s_countryRegex[x], buf, 0, NULL, 0))
			return(x);
	return(0);
}

const char *CountryCode::getAbbr(int index) {
	if(index < 0 || index > s_numCountryCodes) index = 0;
	return(s_countryCode[index]);
}

const char *CountryCode::getName(int index) {
	if(index < 0 || index > s_numCountryCodes) return(NULL);
	return(s_countryName[index]);
}

int CountryCode::getIndexOfAbbr(const char *abbr) {
	if(!m_init) return(0);
	uint16_t idx;
	if(!abbr) return(0);
	idx = abbr[0];
	idx = idx << 8;
	idx |= abbr[1];
	int32_t slot = m_abbrToIndex.getSlot(idx);
	if(slot < 0) return(0);
	return(m_abbrToIndex.getValueFromSlot(slot));
}

int32_t CountryCode::getNumEntries(void) {
	if(!m_init) return(0);
	return(s_catToCountry.getNumSlotsUsed());
}

void CountryCode::debugDumpNumbers(void) {
	int32_t slot;
	catcountryrec_t ccr;
	for(slot = 0; slot < s_catToCountry.getNumSlotsUsed(); slot++) {
		ccr.lval = 0L;
		ccr.lval = s_catToCountry.getValueFromSlot(slot);
		if(ccr.lval)
			log( "Slot %"INT32" has lang %d, country %d (%"INT32")\n",
					slot, ccr.sval.lang, ccr.sval.country, ccr.lval);
	}
}

uint64_t CountryCode::getLanguagesWritten(int index) {
	return s_countryLanguages[index];
}
