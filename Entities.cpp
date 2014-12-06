#include "gb-include.h"

#include "Entities.h"
#include "Unicode.h"
#include "HashTableX.h"

// JAB: const-ness for optimizer...
// don't call these, they're used internally
static bool     initEntityTable();
static uint32_t getTextEntity        ( char *s , int32_t len );
static uint32_t getDecimalEntity     ( char *s , int32_t len );
static uint32_t getHexadecimalEntity ( char *s , int32_t len );

// . s[maxLen] should be the NULL
// . returns full length of entity @ "s" if there is a valid one, 0 otherwise
// . sets *c to the iso character the entity represents (if there is one)
// JAB: const-ness for optimizer...
int32_t getEntity_a ( char *s , int32_t maxLen , uint32_t *c ) {
	// ensure there's an & as first char
	if ( s[0] != '&' ) return 0;
	// compute maximum length of entity, if it's indeed an entity
	int32_t len = 1;
	if ( s[len]=='#' ) len++;
	// cut it off after 9 chars to save time
	while ( len < maxLen && len < 9 && is_alnum_a(s[len]) ) len++;
	// include the ending ; if any
	if ( len < maxLen && s[len]==';' ) len++;
	//	char d = s[len];
	//	s[len]='\0';
	//	fprintf(stderr,"got entity %s \n",s);
	//	s[len]=d;
	// we don't have entities longer than "&curren;"
	if ( len > 10 ) return 0;
	// all entites are 3 or more chars (&gt)
	if ( len < 3 ) return 0;
	// . if it's a numeric entity like &#123 use this routine
	// . pass in the whole she-bang: "&#12...;" or "&acute...;
	if ( s[1] == '#' ) {
		if ( s[2] == 'x' ) *c = getHexadecimalEntity (s, len );
		else               *c = getDecimalEntity     (s, len );
	}
	// otherwise, it's text
	else *c = getTextEntity ( s , len );
	// return 0 if not an entity, length of entity if it is an entity
	if ( *c ) return len;
	else      return 0;
}


// Moved this out of function to be shared by ascii and unicode versions
static HashTableX s_table;
static bool       s_isInitialized = false;
struct Entity {
	int32_t           unicode;
	char          *entity;
	unsigned char  c;
	int32_t           utf8Len;
	unsigned char  utf8[4];
};

//parse these out of 
//http://en.wikipedia.org/wiki/List_of_XML_and_HTML_character_entity_references
// http://www.w3.org/TR/html4/sgml/entities.html
// wget that and and awk the crap out:
//grep ENTITY poo | awk '{print $2" "$4}' | awk -F" \"&amp;#" '{print $1" "$2}' | awk -F";" '{print $1}' | awk '{print "\t{ "$2", \"&"$1"\", 0,0,{0,0,0,0}},"}' >> Entities.cpp
static struct Entity s_entities[] = {
	{ 160, "&nbsp", 0,0,{0,0,0,0}},
	{ 161, "&iexcl", 0,0,{0,0,0,0}},
	{ 162, "&cent", 0,0,{0,0,0,0}},
	{ 163, "&pound", 0,0,{0,0,0,0}},
	{ 164, "&curren", 0,0,{0,0,0,0}},
	{ 165, "&yen", 0,0,{0,0,0,0}},
	{ 166, "&brvbar", 0,0,{0,0,0,0}},
	{ 167, "&sect", 0,0,{0,0,0,0}},
	{ 168, "&uml", 0,0,{0,0,0,0}},
	{ 169, "&copy", 0,0,{0,0,0,0}},
	{ 170, "&ordf", 0,0,{0,0,0,0}},
	{ 171, "&laquo", 0,0,{0,0,0,0}},
	{ 172, "&not", 0,0,{0,0,0,0}},
	{ 173, "&shy", 0,0,{0,0,0,0}},
	{ 174, "&reg", 0,0,{0,0,0,0}},
	{ 175, "&macr", 0,0,{0,0,0,0}},
	{ 176, "&deg", 0,0,{0,0,0,0}},
	{ 177, "&plusmn", 0,0,{0,0,0,0}},
	{ 178, "&sup2", 0,0,{0,0,0,0}},
	{ 179, "&sup3", 0,0,{0,0,0,0}},
	{ 180, "&acute", 0,0,{0,0,0,0}},
	{ 181, "&micro", 0,0,{0,0,0,0}},
	{ 182, "&para", 0,0,{0,0,0,0}},
	{ 183, "&middot", 0,0,{0,0,0,0}},
	{ 184, "&cedil", 0,0,{0,0,0,0}},
	{ 185, "&sup1", 0,0,{0,0,0,0}},
	{ 186, "&ordm", 0,0,{0,0,0,0}},
	{ 187, "&raquo", 0,0,{0,0,0,0}},
	{ 188, "&frac14", 0,0,{0,0,0,0}},
	{ 189, "&frac12", 0,0,{0,0,0,0}},
	{ 190, "&frac34", 0,0,{0,0,0,0}},
	{ 191, "&iquest", 0,0,{0,0,0,0}},
	{ 192, "&Agrave", 0,0,{0,0,0,0}},
	{ 193, "&Aacute", 0,0,{0,0,0,0}},
	{ 194, "&Acirc", 0,0,{0,0,0,0}},
	{ 195, "&Atilde", 0,0,{0,0,0,0}},
	{ 196, "&Auml", 0,0,{0,0,0,0}},
	{ 197, "&Aring", 0,0,{0,0,0,0}},
	{ 198, "&AElig", 0,0,{0,0,0,0}},
	{ 199, "&Ccedil", 0,0,{0,0,0,0}},
	{ 200, "&Egrave", 0,0,{0,0,0,0}},
	{ 201, "&Eacute", 0,0,{0,0,0,0}},
	{ 202, "&Ecirc", 0,0,{0,0,0,0}},
	{ 203, "&Euml", 0,0,{0,0,0,0}},
	{ 204, "&Igrave", 0,0,{0,0,0,0}},
	{ 205, "&Iacute", 0,0,{0,0,0,0}},
	{ 206, "&Icirc", 0,0,{0,0,0,0}},
	{ 207, "&Iuml", 0,0,{0,0,0,0}},
	{ 208, "&ETH", 0,0,{0,0,0,0}},
	{ 209, "&Ntilde", 0,0,{0,0,0,0}},
	{ 210, "&Ograve", 0,0,{0,0,0,0}},
	{ 211, "&Oacute", 0,0,{0,0,0,0}},
	{ 212, "&Ocirc", 0,0,{0,0,0,0}},
	{ 213, "&Otilde", 0,0,{0,0,0,0}},
	{ 214, "&Ouml", 0,0,{0,0,0,0}},
	{ 215, "&times", 0,0,{0,0,0,0}},
	{ 216, "&Oslash", 0,0,{0,0,0,0}},
	{ 217, "&Ugrave", 0,0,{0,0,0,0}},
	{ 218, "&Uacute", 0,0,{0,0,0,0}},
	{ 219, "&Ucirc", 0,0,{0,0,0,0}},
	{ 220, "&Uuml", 0,0,{0,0,0,0}},
	{ 221, "&Yacute", 0,0,{0,0,0,0}},
	{ 222, "&THORN", 0,0,{0,0,0,0}},
	{ 223, "&szlig", 0,0,{0,0,0,0}},
	{ 224, "&agrave", 0,0,{0,0,0,0}},
	{ 225, "&aacute", 0,0,{0,0,0,0}},
	{ 226, "&acirc", 0,0,{0,0,0,0}},
	{ 227, "&atilde", 0,0,{0,0,0,0}},
	{ 228, "&auml", 0,0,{0,0,0,0}},
	{ 229, "&aring", 0,0,{0,0,0,0}},
	{ 230, "&aelig", 0,0,{0,0,0,0}},
	{ 231, "&ccedil", 0,0,{0,0,0,0}},
	{ 232, "&egrave", 0,0,{0,0,0,0}},
	{ 233, "&eacute", 0,0,{0,0,0,0}},
	{ 234, "&ecirc", 0,0,{0,0,0,0}},
	{ 235, "&euml", 0,0,{0,0,0,0}},
	{ 236, "&igrave", 0,0,{0,0,0,0}},
	{ 237, "&iacute", 0,0,{0,0,0,0}},
	{ 238, "&icirc", 0,0,{0,0,0,0}},
	{ 239, "&iuml", 0,0,{0,0,0,0}},
	{ 240, "&eth", 0,0,{0,0,0,0}},
	{ 241, "&ntilde", 0,0,{0,0,0,0}},
	{ 242, "&ograve", 0,0,{0,0,0,0}},
	{ 243, "&oacute", 0,0,{0,0,0,0}},
	{ 244, "&ocirc", 0,0,{0,0,0,0}},
	{ 245, "&otilde", 0,0,{0,0,0,0}},
	{ 246, "&ouml", 0,0,{0,0,0,0}},
	{ 247, "&divide", 0,0,{0,0,0,0}},
	{ 248, "&oslash", 0,0,{0,0,0,0}},
	{ 249, "&ugrave", 0,0,{0,0,0,0}},
	{ 250, "&uacute", 0,0,{0,0,0,0}},
	{ 251, "&ucirc", 0,0,{0,0,0,0}},
	{ 252, "&uuml", 0,0,{0,0,0,0}},
	{ 253, "&yacute", 0,0,{0,0,0,0}},
	{ 254, "&thorn", 0,0,{0,0,0,0}},
	{ 255, "&yuml", 0,0,{0,0,0,0}},
	{ 402, "&fnof", 0,0,{0,0,0,0}},
	{ 913, "&Alpha", 0,0,{0,0,0,0}},
	{ 914, "&Beta", 0,0,{0,0,0,0}},
	{ 915, "&Gamma", 0,0,{0,0,0,0}},
	{ 916, "&Delta", 0,0,{0,0,0,0}},
	{ 917, "&Epsilon", 0,0,{0,0,0,0}},
	{ 918, "&Zeta", 0,0,{0,0,0,0}},
	{ 919, "&Eta", 0,0,{0,0,0,0}},
	{ 920, "&Theta", 0,0,{0,0,0,0}},
	{ 921, "&Iota", 0,0,{0,0,0,0}},
	{ 922, "&Kappa", 0,0,{0,0,0,0}},
	{ 923, "&Lambda", 0,0,{0,0,0,0}},
	{ 924, "&Mu", 0,0,{0,0,0,0}},
	{ 925, "&Nu", 0,0,{0,0,0,0}},
	{ 926, "&Xi", 0,0,{0,0,0,0}},
	{ 927, "&Omicron", 0,0,{0,0,0,0}},
	{ 928, "&Pi", 0,0,{0,0,0,0}},
	{ 929, "&Rho", 0,0,{0,0,0,0}},
	{ 931, "&Sigma", 0,0,{0,0,0,0}},
	{ 932, "&Tau", 0,0,{0,0,0,0}},
	{ 933, "&Upsilon", 0,0,{0,0,0,0}},
	{ 934, "&Phi", 0,0,{0,0,0,0}},
	{ 935, "&Chi", 0,0,{0,0,0,0}},
	{ 936, "&Psi", 0,0,{0,0,0,0}},
	{ 937, "&Omega", 0,0,{0,0,0,0}},
	{ 945, "&alpha", 0,0,{0,0,0,0}},
	{ 946, "&beta", 0,0,{0,0,0,0}},
	{ 947, "&gamma", 0,0,{0,0,0,0}},
	{ 948, "&delta", 0,0,{0,0,0,0}},
	{ 949, "&epsilon", 0,0,{0,0,0,0}},
	{ 950, "&zeta", 0,0,{0,0,0,0}},
	{ 951, "&eta", 0,0,{0,0,0,0}},
	{ 952, "&theta", 0,0,{0,0,0,0}},
	{ 953, "&iota", 0,0,{0,0,0,0}},
	{ 954, "&kappa", 0,0,{0,0,0,0}},
	{ 955, "&lambda", 0,0,{0,0,0,0}},
	{ 956, "&mu", 0,0,{0,0,0,0}},
	{ 957, "&nu", 0,0,{0,0,0,0}},
	{ 958, "&xi", 0,0,{0,0,0,0}},
	{ 959, "&omicron", 0,0,{0,0,0,0}},
	{ 960, "&pi", 0,0,{0,0,0,0}},
	{ 961, "&rho", 0,0,{0,0,0,0}},
	{ 962, "&sigmaf", 0,0,{0,0,0,0}},
	{ 963, "&sigma", 0,0,{0,0,0,0}},
	{ 964, "&tau", 0,0,{0,0,0,0}},
	{ 965, "&upsilon", 0,0,{0,0,0,0}},
	{ 966, "&phi", 0,0,{0,0,0,0}},
	{ 967, "&chi", 0,0,{0,0,0,0}},
	{ 968, "&psi", 0,0,{0,0,0,0}},
	{ 969, "&omega", 0,0,{0,0,0,0}},
	{ 977, "&thetasym", 0,0,{0,0,0,0}},
	{ 978, "&upsih", 0,0,{0,0,0,0}},
	{ 982, "&piv", 0,0,{0,0,0,0}},
	{ 8226, "&bull", 0,0,{0,0,0,0}},
	{ 8230, "&hellip", 0,0,{0,0,0,0}},
	{ 8242, "&prime", 0,0,{0,0,0,0}},
	{ 8243, "&Prime", 0,0,{0,0,0,0}},
	{ 8254, "&oline", 0,0,{0,0,0,0}},
	{ 8260, "&frasl", 0,0,{0,0,0,0}},
	{ 8472, "&weierp", 0,0,{0,0,0,0}},
	{ 8465, "&image", 0,0,{0,0,0,0}},
	{ 8476, "&real", 0,0,{0,0,0,0}},
	{ 8482, "&trade", 0,0,{0,0,0,0}},
	{ 8501, "&alefsym", 0,0,{0,0,0,0}},
	{ 8592, "&larr", 0,0,{0,0,0,0}},
	{ 8593, "&uarr", 0,0,{0,0,0,0}},
	{ 8594, "&rarr", 0,0,{0,0,0,0}},
	{ 8595, "&darr", 0,0,{0,0,0,0}},
	{ 8596, "&harr", 0,0,{0,0,0,0}},
	{ 8629, "&crarr", 0,0,{0,0,0,0}},
	{ 8656, "&lArr", 0,0,{0,0,0,0}},
	{ 8657, "&uArr", 0,0,{0,0,0,0}},
	{ 8658, "&rArr", 0,0,{0,0,0,0}},
	{ 8659, "&dArr", 0,0,{0,0,0,0}},
	{ 8660, "&hArr", 0,0,{0,0,0,0}},
	{ 8704, "&forall", 0,0,{0,0,0,0}},
	{ 8706, "&part", 0,0,{0,0,0,0}},
	{ 8707, "&exist", 0,0,{0,0,0,0}},
	{ 8709, "&empty", 0,0,{0,0,0,0}},
	{ 8711, "&nabla", 0,0,{0,0,0,0}},
	{ 8712, "&isin", 0,0,{0,0,0,0}},
	{ 8713, "&notin", 0,0,{0,0,0,0}},
	{ 8715, "&ni", 0,0,{0,0,0,0}},
	{ 8719, "&prod", 0,0,{0,0,0,0}},
	{ 8721, "&sum", 0,0,{0,0,0,0}},
	{ 8722, "&minus", 0,0,{0,0,0,0}},
	{ 8727, "&lowast", 0,0,{0,0,0,0}},
	{ 8730, "&radic", 0,0,{0,0,0,0}},
	{ 8733, "&prop", 0,0,{0,0,0,0}},
	{ 8734, "&infin", 0,0,{0,0,0,0}},
	{ 8736, "&ang", 0,0,{0,0,0,0}},
	{ 8743, "&and", 0,0,{0,0,0,0}},
	{ 8744, "&or", 0,0,{0,0,0,0}},
	{ 8745, "&cap", 0,0,{0,0,0,0}},
	{ 8746, "&cup", 0,0,{0,0,0,0}},
	{ 8747, "&int", 0,0,{0,0,0,0}},
	{ 8756, "&there4", 0,0,{0,0,0,0}},
	{ 8764, "&sim", 0,0,{0,0,0,0}},
	{ 8773, "&cong", 0,0,{0,0,0,0}},
	{ 8776, "&asymp", 0,0,{0,0,0,0}},
	{ 8800, "&ne", 0,0,{0,0,0,0}},
	{ 8801, "&equiv", 0,0,{0,0,0,0}},
	{ 8804, "&le", 0,0,{0,0,0,0}},
	{ 8805, "&ge", 0,0,{0,0,0,0}},
	{ 8834, "&sub", 0,0,{0,0,0,0}},
	{ 8835, "&sup", 0,0,{0,0,0,0}},
	{ 8836, "&nsub", 0,0,{0,0,0,0}},
	{ 8838, "&sube", 0,0,{0,0,0,0}},
	{ 8839, "&supe", 0,0,{0,0,0,0}},
	{ 8853, "&oplus", 0,0,{0,0,0,0}},
	{ 8855, "&otimes", 0,0,{0,0,0,0}},
	{ 8869, "&perp", 0,0,{0,0,0,0}},
	{ 8901, "&sdot", 0,0,{0,0,0,0}},
	{ 8968, "&lceil", 0,0,{0,0,0,0}},
	{ 8969, "&rceil", 0,0,{0,0,0,0}},
	{ 8970, "&lfloor", 0,0,{0,0,0,0}},
	{ 8971, "&rfloor", 0,0,{0,0,0,0}},
	{ 9001, "&lang", 0,0,{0,0,0,0}},
	{ 9002, "&rang", 0,0,{0,0,0,0}},
	{ 9674, "&loz", 0,0,{0,0,0,0}},
	{ 9824, "&spades", 0,0,{0,0,0,0}},
	{ 9827, "&clubs", 0,0,{0,0,0,0}},
	{ 9829, "&hearts", 0,0,{0,0,0,0}},
	{ 9830, "&diams", 0,0,{0,0,0,0}},
	{ 34, "&quot", 0,0,{0,0,0,0}},
	{ 38, "&amp", 0,0,{0,0,0,0}},
	{ 38, "&AMP", 0,0,{0,0,0,0}}, // a hack fix
	{ 60, "&lt", 0,0,{0,0,0,0}},
	{ 62, "&gt", 0,0,{0,0,0,0}},
	{ 338, "&OElig", 0,0,{0,0,0,0}},
	{ 339, "&oelig", 0,0,{0,0,0,0}},
	{ 352, "&Scaron", 0,0,{0,0,0,0}},
	{ 353, "&scaron", 0,0,{0,0,0,0}},
	{ 376, "&Yuml", 0,0,{0,0,0,0}},
	{ 710, "&circ", 0,0,{0,0,0,0}},
	{ 732, "&tilde", 0,0,{0,0,0,0}},
	{ 8194, "&ensp", 0,0,{0,0,0,0}},
	{ 8195, "&emsp", 0,0,{0,0,0,0}},
	{ 8201, "&thinsp", 0,0,{0,0,0,0}},
	{ 8204, "&zwnj", 0,0,{0,0,0,0}},
	{ 8205, "&zwj", 0,0,{0,0,0,0}},
	{ 8206, "&lrm", 0,0,{0,0,0,0}},
	{ 8207, "&rlm", 0,0,{0,0,0,0}},
	{ 8211, "&ndash", 0,0,{0,0,0,0}},
	{ 8212, "&mdash", 0,0,{0,0,0,0}},
	{ 8216, "&lsquo", 0,0,{0,0,0,0}},
	{ 8217, "&rsquo", 0,0,{0,0,0,0}},
	{ 8218, "&sbquo", 0,0,{0,0,0,0}},
	{ 8220, "&ldquo", 0,0,{0,0,0,0}},
	{ 8221, "&rdquo", 0,0,{0,0,0,0}},
	{ 8222, "&bdquo", 0,0,{0,0,0,0}},
	{ 8224, "&dagger", 0,0,{0,0,0,0}},
	{ 8225, "&Dagger", 0,0,{0,0,0,0}},
	{ 8240, "&permil", 0,0,{0,0,0,0}},
	{ 8249, "&lsaquo", 0,0,{0,0,0,0}},
	{ 8250, "&rsaquo", 0,0,{0,0,0,0}},
	{ 8364, "&euro", 0,0,{0,0,0,0}}
};

/*
	// yeah right... here is a ton ton more!
	// http://www.blackwellpublishing.com/xml/dtds/4-0/help/bpg4-0entities.mod
	// it is like there is a text entity for every char!

	// JAB: from http://rabbit.eng.miami.edu/info/htmlchars.html
	//      non-Latin1 that are missing from this version...
	// &Etilde
	// &Ering
	// &etilde
	// &ering
	// &Itilde
	// &Iring
	// &itilde
	// &iring
	// &OElig
	// &Oring
	// &oelig
	// &oring
	// &Utilde
	// &Uring
	// &utilde
	// &uring
	// &Ygrave
	// &Ycirc
	// &Ytilde
	// &Yuml
	// &Yring
	// &ygrave
	// &ycirc
	// &ytilde
	// &yring
};
*/

void resetEntities ( ) {
	s_table.reset();
}

static bool initEntityTable(){
	if ( ! s_isInitialized ) {
		// set up the hash table
		if ( ! s_table.set ( 8,4,255,NULL,0,false,0,"enttbl" ) )
			return log("build: Could not init table of "
					   "HTML entities.");
		// now add in all the stop words
		int32_t n = (int32_t)sizeof(s_entities) / (int32_t)sizeof(Entity);
		for ( int32_t i = 0 ; i < n ; i++ ) {
			int64_t h = hash64b ( s_entities[i].entity );
			// grab the unicode code point
			UChar32 up = s_entities[i].unicode;
			// now we are 100% up
			if ( ! up ) { char *xx=NULL;*xx=0; }
			// point to it
			char *buf = (char *)s_entities[i].utf8;
			// if uchar32 not 0 then set the utf8 with it
			int32_t len = utf8Encode(up,buf);
			//
			// make my own mods to make parsing easier
			//
			if ( up == 160 ) {  // nbsp
				buf[0] = ' '; len = 1; }
			// make all quotes equal '\"' (34 decimal)
			// double and single curling quotes
			//http://www.dwheeler.com/essays/quotes-test-utf-8.html
			// &#x201c, 201d, 2018, 2019 (unicode values, not utf8)
			// &ldquo, &rdquo, &lsquo, &rsquo
			/*
			if ( up == 171 ||
			     up == 187 ||
			     up == 8216 ||
			     up == 8217 ||
			     up == 8218 ||
			     up == 8220 ||
			     up == 8221 ||
			     up == 8222 ||
			     up == 8249 ||
			     up == 8250 ) {
				buf[0] = '\"'; len = 1; }
			// and normalize all dashes (mdash,ndash)
			if ( up == 8211 || up == 8212 ) {
				buf[0] = '-'; len = 1; }
			*/

			//
			// end custom mods
			//

			// set length
			s_entities[i].utf8Len = len;
			// check it
			if ( len == 0 ) { char *xx=NULL;*xx=0; }
			// must not exist!
			if ( s_table.isInTable(&h) ) { char*xx=NULL;*xx=0;}
			// store the entity index in the hash table as score
			if ( ! s_table.addTerm ( &h, i+1 ) ) return false;
		}
		s_isInitialized = true;
	} 
	return true;
}
// . is "s" an HTML entity? (ascii representative of an iso char)
// . return the 32-bit unicode char it represents
// . returns 0 if none
// . JAB: const-ness for optimizer...
uint32_t getTextEntity ( char *s , int32_t len ) {
	if ( !initEntityTable()) return 0;
	// take the ; off, if any
	if ( s[len-1] == ';' ) len--;
	// compute the hash of the entity including &, but not ;
	int64_t h = hash64 ( s , len );
	// get the entity index from table (stored in the score field)
	int32_t i = (int32_t) s_table.getScore ( &h );
	// return 0 if no match
	if ( i == 0 ) return 0;
	// point to the utf8 char. these is 1 or 2 bytes it seems
	char *p = (char *)s_entities[i-1].utf8;
	// encode into unicode
	uint32_t c = utf8Decode ( p );
	// return that
	return c;
	// return the iso character
	//printf("Converted text entity \"");
	//for(int si=0;si<len;si++)putchar(s[si]);
	//printf("\" to 0x%x(%d)\"%c\"\n",s_entities[i-1].c,s_entities[i-1].c, 
	//	   s_entities[i-1].c);
	//return (uint32_t)s_entities[i-1].c;
}

// . get a decimal encoded entity
// . s/len is the whol thing
// . JAB: const-ness for optimizer...
uint32_t getDecimalEntity ( char *s , int32_t len ) {
	// take the ; off, if any
	if ( s[len-1] == ';' ) len--;
	// . &#1 is smallest it can be
	// . &#1114111 is biggest
	if ( len < 3  ||  len > 9 ) return 0;
	// . must start with &#[0-9]
	if ( s[0] !='&'  ||  s[1] != '#' || ! is_digit(s[2]) ) return 0;
	// use space as default
	uint32_t v ;
	if ( len == 3 ) v = (s[2]-48); 
	else if ( len == 4 ) v = (s[2]-48)*10    +
				     (s[3]-48);
	else if ( len == 5 ) v = (s[2]-48)*100   +
				     (s[3]-48)*10  +
				     (s[4]-48);
	else if ( len == 6 ) v = (s[2]-48)*1000  +
				     (s[3]-48)*100 +
				     (s[4]-48)*10 +
				     s[5]-48;
	else if ( len == 7 ) v = (s[2]-48)*10000 +
				     (s[3]-48)*1000+
				     (s[4]-48)*100+
				     (s[5]-48)*10+
				     s[5]-48;
	else if ( len == 8 ) v = (s[2]-48)*100000 +
				     (s[3]-48)*10000 +
				     (s[4]-48)*1000+
				     (s[5]-48)*100+
				     (s[6]-48)*10+
				     s[7]-48;
	else if ( len == 9 ) v = (s[2]-48)*1000000 +
				     (s[3]-48)*100000 +
				     (s[4]-48)*10000 +
				     (s[5]-48)*1000 +
				     (s[6]-48)*100 +
				     (s[7]-48)*10 +
				     s[7]-48;
	else return (uint32_t)' ';

	//printf("Translated entity (dec)");
	//for (int i=0;i<len;i++)putchar(s[i]);
	//printf(" to [U+%"INT32"]\n", v);

	if (v < 32 || v>0x10ffff) return (uint32_t)' ';

	return v;
}		


// . get a hexadecimal encoded entity
// . JAB: const-ness for optimizer...
// . returns a UChar32
uint32_t getHexadecimalEntity ( char *s , int32_t len ) {
	// take the ; off, if any
	if ( s[len-1] == ';' ) len--;
	// . &#x1  is smallest it can be
	// . &#x10FFFF is biggest
	if ( len < 4  ||  len > 9 ) return (char)0;
	// . must start with &#x[0-f]
	if ( s[0] !='&'  ||  s[1] != '#' ||  s[2] !='x'  ) return (char)0;
	if ( ! is_hex ( s[3] ) ) return (char)0;
	// use space as default
	uint32_t v;
	if      ( len == 4 ) v = htob(s[3]);
	else if ( len == 5 ) v = (htob(s[3]) << 4) + 
			htob(s[4]);
	else if ( len == 6 ) v = (htob(s[3]) << 8) + 
		(htob(s[4]) << 4) + 
		htob(s[5]);
	else if ( len == 7 ) v = (htob(s[3]) << 12) + 
		(htob(s[4]) << 8) + 
		(htob(s[5]) << 4) +
				htob(s[6]);
	else if ( len == 8 ) v = (htob(s[3]) << 16) + 
		(htob(s[4]) << 12) + 
		(htob(s[5]) << 8) +
		(htob(s[6]) << 4) +
		htob(s[7]);
	else if ( len == 9 ) v = (htob(s[3]) << 20) + 
		(htob(s[4]) << 16) + 
		(htob(s[5]) << 12) +
		(htob(s[6]) << 8) +
		(htob(s[7]) << 4) +
		htob(s[8]);
	else 
		return (uint32_t)' ';
	// return the char
	//printf("Translated entity (dec)");
	//for (int i=0;i<len;i++)putchar(s[i]);
	//printf(" to [U+%04lX]\n", v);
	if (v < 32 || v>0x10ffff) return (uint32_t)' ';
	return (uint32_t) v;
}		
