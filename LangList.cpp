#include "gb-include.h"

#include "LangList.h"
#include "Indexdb.h"

LangList g_langList;


struct TldInfo {
  char* m_tld;
  char* m_country;
  char* m_languages;
  uint32_t  m_languagebv;
};

static int32_t s_numTlds = 0;

static TldInfo s_tldInfo[] = {
{ "arpa", "Address and Routing Parameter Area", "unknown", 0xffffffff },
{ "root", "N/A", "unknown", 0xffffffff },
{ "aero", "air-transport industry", "unknown", 0xffffffff },
{ "biz", "business", "unknown", 0xffffffff },
{ "cat", "Catalan", "unknown", 0xffffffff },
{ "com", "commercial", "unknown", 0xffffffff },
{ "coop", "cooperatives", "unknown", 0xffffffff },
{ "edu", "educational", "unknown", 0xffffffff },
{ "gov", "governmental", "unknown", 0xffffffff },
{ "info", "information", "unknown", 0xffffffff },
{ "int", "international organizations", "unknown", 0xffffffff },
{ "jobs", "companies", "unknown", 0xffffffff },
{ "mil", "United States Military", "english,carolinian,chamorro,"
	  "hawaiian,samoan,spanish", 0xffffffff },
{ "mobi", "mobile devices", "unknown", 0xffffffff },
{ "museum", "museums", "unknown", 0xffffffff },
{ "name", "individuals, by name", "unknown", 0xffffffff },
{ "net", "network", "unknown", 0xffffffff },
{ "org", "organization", "unknown", 0xffffffff },
{ "pro", "professions", "unknown", 0xffffffff },
{ "travel", "travel and travel-agency related sites", "unknown", 0xffffffff },
{ "ac", "Ascension Island", "unknown", 0xffffffff },
{ "ad", "Andorra", "catalan", 0xffffffff },
{ "ae", "United Arab Emirates", "arabic", 0xffffffff },
{ "af", "Afghanistan", "arabic,balochi,dari,nuristani,pashto,pamiri,"
	  "pashai,turkmen,uzbek", 0xffffffff },
{ "ag", "Antigua and Barbuda", "english", 0xffffffff },
{ "ai", "Anguilla", "unknown", 0xffffffff },
{ "al", "Albania", "albanian", 0xffffffff },
{ "am", "Armenia", "armenian,armenian", 0xffffffff },
{ "an", "Netherlands Antilles", "dutch,frisian", 0xffffffff },
{ "ao", "Angola", "portuguese", 0xffffffff },
{ "aq", "Antarctica", "unknown", 0xffffffff },
{ "ar", "Argentina", "spanish,guarani", 0xffffffff },
{ "as", "American Samoa", "english,samoan", 0xffffffff },
{ "at", "Austria", "croatian,czech,german,hungarian,slovak,"
	  "slovenian,romani", 0xffffffff },
{ "au", "Australia", "australia", 0xffffffff },
{ "aw", "Aruba", "unknown", 0xffffffff },
{ "ax", "Ã…land", "unknown", 0xffffffff },
{ "az", "Azerbaijan", "azerbaijani", 0xffffffff },
{ "ba", "Bosnia and Herzegovina", "bosnian,croatian,serbian", 0xffffffff },
{ "bb", "Barbados", "english", 0xffffffff },
{ "bd", "Bangladesh", "bengala", 0xffffffff },
{ "be", "Belgium", "dutch,french,german", 0xffffffff },
{ "bf", "Burkina Faso", "french,more,jula,fula", 0xffffffff },
{ "bg", "Bulgaria", "bulgarian", 0xffffffff },
{ "bh", "Bahrain", "arabic", 0xffffffff },
{ "bi", "Burundi", "french,kirundi", 0xffffffff },
{ "bj", "Benin", "french", 0xffffffff },
{ "bm", "Bermuda", "unknown", 0xffffffff },
{ "bn", "Brunei Darussalam", "malay", 0xffffffff },
{ "bo", "Bolivia", "spanish,aymara,quechua", 0xffffffff },
{ "br", "Brazil", "portuguese", 0xffffffff },
{ "bs", "Bahamas", "unknown", 0xffffffff },
{ "bt", "Bhutan", "dzongkha,english", 0xffffffff },
{ "bv", "Bouvet Island", "unknown", 0xffffffff },
{ "bw", "Botswana", "english,kalanga,tswana", 0xffffffff },
{ "by", "Belarus", "belarusian,russian", 0xffffffff },
{ "bz", "Belize", "english", 0xffffffff },
{ "ca", "Canada", "chipewyan,cree,dogrib,english,french,gwich?in,inuinnaqtun,"
	  "inuktitut,inuvialuktun,slavey", 0xffffffff },
{ "cc", "Cocos (Keeling) Islands", "unknown", 0xffffffff },
{ "cd", "Democratic Republic of the Congo", "french,lingala,kikongo,swahili,"
	  "tshiluba", 0xffffffff },
{ "cf", "Central African Republic", "french,sango", 0xffffffff },
{ "cg", "Republic of the Congo", "french,lingala,munukutuba", 0xffffffff },
{ "ch", "Switzerland (Confoederatio Helvetica)", "french,german,italian,"
	  "romansh", 0xffffffff },
{ "ci", "CÃÂ´te d'Ivoire", "french", 0xffffffff },
{ "ck", "Cook Islands", "unknown", 0xffffffff },
{ "cl", "Chile", "spanish", 0xffffffff },
{ "cm", "Cameroon", "english,french", 0xffffffff },
{ "cn", "People's Republic of China", "cantonese,english,kazakh,korean,"
	  "mandarin,mongolian,portuguese,tajik,tibetan,uyghur,zhuang", 
	  0xffffffff },
{ "co", "Colombia", "spanish", 0xffffffff },
{ "cr", "Costa Rica", "spanish", 0xffffffff },
{ "cu", "Cuba", "spanish", 0xffffffff },
{ "cv", "Cape Verde", "crioulo,portuguese", 0xffffffff },
{ "cx", "Christmas Island", "unknown", 0xffffffff },
{ "cy", "Cyprus", "greek,turkish", 0xffffffff },
{ "cz", "Czech Republic", "czech", 0xffffffff },
{ "de", "Germany (Deutschland)", "danish,frisian,german,romani,"
	  "lower sorbian,upper sorbian", 0xffffffff },
{ "dj", "Djibouti", "arabic,french", 0xffffffff },
{ "dk", "Denmark", "danish,faroese,kalaallisut", 0xffffffff },
{ "dm", "Dominica", "english", 0xffffffff },
{ "do", "Dominican Republic", "english", 0xffffffff },
{ "dz", "Algeria", "arabic,tamazight", 0xffffffff },
{ "ec", "Ecuador", "spanish,quechua", 0xffffffff },
{ "ee", "Estonia", "estonian", 0xffffffff },
{ "eg", "Egypt", "arabic", 0xffffffff },
{ "er", "Eritrea", "arabic,english,tigrinya", 0xffffffff },
{ "es", "Spain (EspaÃÂ±a)", "basque,catalan,galician,occitan,"
	  "spanish", 0xffffffff },
{ "et", "Ethiopia", "amharic", 0xffffffff },
{ "eu", "European Union", "unknown", 0xffffffff },
{ "fi", "Finland", "finnish,sami,swedish", 0xffffffff },
{ "fj", "Fiji", "english,fijian,hindustani", 0xffffffff },
{ "fk", "Falkland Islands", "unknown", 0xffffffff },
{ "fm", "Federated States of Micronesia", "chuuk,english,kosraean,ponapean,"
	  "ulithian,yapese", 0xffffffff },
{ "fo", "Faroe Islands", "unknown", 0xffffffff },
{ "fr", "France", "french,tahitian", 0xffffffff },
{ "ga", "Gabon", "french", 0xffffffff },
{ "gb", "United Kingdom (Great Britain)", "english,cornish,"
	  "dgÃÂ¨rnÃÂ©siais,english,french,irish,jÃÂ¨rriais,"
	  "pitcairnese,scots,scottish gaelic,welsh", 0xffffffff },
{ "gd", "Grenada", "english", 0xffffffff },
{ "ge", "Georgia", "abkhaz,georgian,ossetic,russian", 0xffffffff },
{ "gf", "French Guiana", "unknown", 0xffffffff },
{ "gg", "Guernsey", "unknown", 0xffffffff },
{ "gh", "Ghana", "adangme,dagaare,dagbani,english,ewe,ga,gonja,kasem,"
	  "nzema,twi", 0xffffffff },
{ "gi", "Gibraltar", "unknown", 0xffffffff },
{ "gl", "Greenland", "unknown", 0xffffffff },
{ "gm", "The Gambia", "unknown", 0xffffffff },
{ "gn", "Guinea", "french,fula", 0xffffffff },
{ "gp", "Guadeloupe", "unknown", 0xffffffff },
{ "gq", "Equatorial Guinea", "french,spanish", 0xffffffff },
{ "gr", "Greece", "greek", 0xffffffff },
{ "gs", "South Georgia and the South Sandwich Islands", "abkhaz,georgian,"
	  "ossetic,russian", 0xffffffff },
{ "gt", "Guatemala", "spanish", 0xffffffff },
{ "gu", "Guam", "unknown", 0xffffffff },
{ "gw", "Guinea-Bissau", "french,fula", 0xffffffff },
{ "gy", "Guyana", "english", 0xffffffff },
{ "hk", "Hong Kong", "unknown", 0xffffffff },
{ "hm", "Heard Island and McDonald Islands", "unknown", 0xffffffff },
{ "hn", "Honduras", "spanish", 0xffffffff },
{ "hr", "Croatia (Hrvatska)", "croatian,italian", 0xffffffff },
{ "ht", "Haiti", "french,haitian creole", 0xffffffff },
{ "hu", "Hungary", "hungarian", 0xffffffff },
{ "id", "Indonesia", "balinese,javanese,indonesian,sundanese", 0xffffffff },
{ "ie", "Ireland (Ã‰ire)", "unknown", 0xffffffff },
{ "il", "Israel", "arabic,hebrew", 0xffffffff },
{ "im", "Isle of Man", "unknown", 0xffffffff },
{ "in", "India", "assamese,bengala,bodo,dogri,english,gujarati,hindi,kannada,"
	  "kashmiri,konkani,maithili,malayalam,meitei,marathi,nepali,oriya,"
	  "punjabi,sanskrit,santali,sindhi,tamil,telugu,urdu,french,karbi,"
	  "bhojpuri,magadhi,maithili,chhattisgarhi,portuguese,pahari,tulu,"
	  "garo,khasi,mizo,rajasthani,kokborok,nicobarese", 0xffffffff },
{ "io", "British Indian Ocean Territory", "assamese,bengala,bodo,dogri,"
	  "english,gujarati,hindi,kannada,kashmiri,konkani,maithili,malayalam,"
	  "meitei,marathi,nepali,oriya,punjabi,sanskrit,santali,sindhi,tamil,"
	  "telugu,urdu,french,karbi,bhojpuri,magadhi,maithili,chhattisgarhi,"
	  "portuguese,pahari,tulu,garo,khasi,mizo,rajasthani,kokborok,"
	  "nicobarese", 0xffffffff },
{ "iq", "Iraq", "arabic,kurdish", 0xffffffff },
{ "ir", "Iran", "persian", 0xffffffff },
{ "is", "Iceland (Island)", "icelandic", 0xffffffff },
{ "it", "Italy", "italian",
	  //"albanian,catalan,croatian,franco-provenÃÂ§al,french,"
	  //	  "friulian,german,greek,italian,ladin,occitan,sardinian,slovenian", 
	  0xffffffff },
{ "je", "Jersey", "unknown", 0xffffffff },
{ "jm", "Jamaica", "english", 0xffffffff },
{ "jo", "Jordan", "arabic", 0xffffffff },
{ "jp", "Japan", "japanese", 0xffffffff },
{ "ke", "Kenya", "english,swahili", 0xffffffff },
{ "kg", "Kyrgyzstan", "kirghiz,russian", 0xffffffff },
{ "kh", "Cambodia (Khmer)", "khmer", 0xffffffff },
{ "ki", "Kiribati", "english,kiribati", 0xffffffff },
{ "km", "Comoros", "arabic,comorian,french", 0xffffffff },
{ "kn", "Saint Kitts and Nevis", "english", 0xffffffff },
{ "kr", "South Korea", "korean", 0xffffffff },
{ "kw", "Kuwait", "arabic", 0xffffffff },
{ "ky", "Cayman Islands", "unknown", 0xffffffff },
{ "kz", "Kazakhstan", "kazakh,russian", 0xffffffff },
{ "la", "Laos", "lao,french", 0xffffffff },
{ "lb", "Lebanon", "arabic", 0xffffffff },
{ "lc", "Saint Lucia", "english", 0xffffffff },
{ "li", "Liechtenstein", "german", 0xffffffff },
{ "lk", "Sri Lanka", "sinhala,tamil", 0xffffffff },
{ "lr", "Liberia", "english", 0xffffffff },
{ "ls", "Lesotho", "english,sotho", 0xffffffff },
{ "lt", "Lithuania", "lithuanian", 0xffffffff },
{ "lu", "Luxembourg", "french,german,luxembourgish", 0xffffffff },
{ "lv", "Latvia", "latvian", 0xffffffff },
{ "ly", "Libya", "arabic", 0xffffffff },
{ "ma", "Morocco", "arabic", 0xffffffff },
{ "mc", "Monaco", "french", 0xffffffff },
{ "md", "Moldova", "gagauz,moldovan,russian,ukrainian", 0xffffffff },
{ "mg", "Madagascar", "french,malagasy", 0xffffffff },
{ "mh", "Marshall Islands", "english,marshallese", 0xffffffff },
{ "mk", "Republic of Macedonia", "unknown", 0xffffffff },
{ "ml", "Mali", "french", 0xffffffff },
{ "mm", "Myanmar", "burmese", 0xffffffff },
{ "mn", "Mongolia", "mongolian", 0xffffffff },
{ "mo", "Macau", "unknown", 0xffffffff },
{ "mp", "Northern Mariana Islands", "unknown", 0xffffffff },
{ "mq", "Martinique", "unknown", 0xffffffff },
{ "mr", "Mauritania", "arabic,fula,soninke,wolof", 0xffffffff },
{ "ms", "Montserrat", "unknown", 0xffffffff },
{ "mt", "Malta", "english,maltese", 0xffffffff },
{ "mu", "Mauritius", "english,french", 0xffffffff },
{ "mv", "Maldives", "dhivehi", 0xffffffff },
{ "mw", "Malawi", "chichewa,english", 0xffffffff },
{ "mx", "Mexico", "spanish", 0xffffffff },
{ "my", "Malaysia", "malay", 0xffffffff },
{ "mz", "Mozambique", "portuguese", 0xffffffff },
{ "na", "Namibia", "english", 0xffffffff },
{ "nc", "New Caledonia", "unknown", 0xffffffff },
{ "ne", "Niger", "french", 0xffffffff },
{ "nf", "Norfolk Island", "unknown", 0xffffffff },
{ "ng", "Nigeria", "french", 0xffffffff },
{ "ni", "Nicaragua", "spanish", 0xffffffff },
{ "nl", "Netherlands", "dutch,frisian", 0xffffffff },
{ "no", "Norway", "norwegian,norwegian,sami", 0xffffffff },
{ "np", "Nepal", "nepali", 0xffffffff },
{ "nr", "Nauru", "english,nauruan", 0xffffffff },
{ "nu", "Niue", "unknown", 0xffffffff },
{ "nz", "New Zealand", "english,maori,new zealand sign language,"
	  "cook islands maori,niuean,tokelauan", 0xffffffff },
{ "om", "Oman", "arabic", 0xffffffff },
{ "pa", "Panama", "spanish", 0xffffffff },
{ "pe", "Peru", "quechua,aymara,spanish", 0xffffffff },
{ "pf", "French Polynesia", "unknown", 0xffffffff },
{ "pg", "Papua New Guinea", "french,fula", 0xffffffff },
{ "ph", "Philippines", "arabic,bikol,cebuano,english,filipino,"
	  "hiligaynon,ilokano,kapampangan,kinaray-a,maranao,"
	  "maguindanao,pangasinan,spanish,tagalog,tausug,"
	  "waray-waray", 0xffffffff },
{ "pk", "Pakistan", "english,urdu", 0xffffffff },
{ "pl", "Poland", "polish", 0xffffffff },
{ "pm", "Saint-Pierre and Miquelon", "unknown", 0xffffffff },
{ "pn", "Pitcairn Islands", "unknown", 0xffffffff },
{ "pr", "Puerto Rico", "unknown", 0xffffffff },
{ "ps", "Palestinian territories", "unknown", 0xffffffff },
{ "pt", "Portugal", "portuguese,mirandese", 0xffffffff },
{ "pw", "Palau", "english,palauan,japanese", 0xffffffff },
{ "py", "Paraguay", "guaranÃÂ­,spanish", 0xffffffff },
{ "qa", "Qatar", "arabic", 0xffffffff },
{ "re", "RÃÂ©union", "unknown", 0xffffffff },
{ "ro", "Romania", "arabic", 0xffffffff },
{ "ru", "Russia", "abaza,adyghe,agul,altay,avar,bashkir,"
	  "buryat,chechen,chukchi,chuvash,dargin,dolgan,"
	  "erzya,evenk,ingush,kabardian,kalmyk,karachay-balkar,"
	  "khakas,khanty,komi-permyak,komi-zyrian,koryak,kumyk,"
	  "lak,lezgi,mansi,mari,moksha,nogai,nenets,ossetic,russian,"
	  "tabasaran,tatar,tuvin,udmurt,yakut,yiddish", 0xffffffff },
{ "rw", "Rwanda", "english,french,kinyarwanda", 0xffffffff },
{ "sa", "Saudi Arabia", "arabic", 0xffffffff },
{ "sb", "Solomon Islands", "english", 0xffffffff },
{ "sc", "Seychelles", "english,french,seselwa", 0xffffffff },
{ "sd", "Sudan", "arabic,english", 0xffffffff },
{ "se", "Sweden", "swedish,finnish,meÃÂ¤nkieli,romani,sami,"
	  "yiddish", 0xffffffff },
{ "sg", "Singapore", "english,malay,mandarin,tamil", 0xffffffff },
{ "sh", "Saint Helena", "unknown", 0xffffffff },
{ "si", "Slovenia", "hungarian,italian,slovenian", 0xffffffff },
{ "sj", "Svalbard and Jan Mayen Islands", "unknown", 0xffffffff },
{ "sk", "Slovakia", "slovak", 0xffffffff },
{ "sl", "Sierra Leone", "english", 0xffffffff },
{ "sm", "San Marino", "italian", 0xffffffff },
{ "sn", "Senegal", "french,jola-fogny,malinke,mandinka,pulaar,"
	  "serer-sine,wolof", 0xffffffff },
{ "so", "Somalia", "french", 0xffffffff },
{ "sr", "Suriname", "dutch", 0xffffffff },
{ "st", "SÃÂ£o TomÃÂ© and PrÃÂ­ncipe", "portuguese", 
	  0xffffffff },
{ "su", "former Soviet Union", "unknown", 0xffffffff },
{ "sv", "El Salvador", "spanish", 0xffffffff },
{ "sy", "Syria", "arabic,french", 0xffffffff },
{ "sz", "Swaziland", "english,swazi", 0xffffffff },
{ "tc", "Turks and Caicos Islands", "unknown", 0xffffffff },
{ "td", "Chad", "arabic,french", 0xffffffff },
{ "tf", "French Southern and Antarctic Lands", "unknown", 0xffffffff },
{ "tg", "Togo", "french", 0xffffffff },
{ "th", "Thailand", "thai", 0xffffffff },
{ "tj", "Tajikistan", "tajik", 0xffffffff },
{ "tk", "Tokelau", "unknown", 0xffffffff },
{ "tl", "East Timor", "english,indonesian,portuguese,tetum", 0xffffffff },
{ "tm", "Turkmenistan", "turkmen", 0xffffffff },
{ "tn", "Tunisia", "arabic", 0xffffffff },
{ "to", "Tonga", "english,tongan", 0xffffffff },
{ "tp", "East Timor", "english,indonesian,portuguese,tetum", 0xffffffff },
{ "tr", "Turkey", "turkish", 0xffffffff },
{ "tt", "Trinidad and Tobago", "english", 0xffffffff },
{ "tv", "Tuvalu", "english,tuvaluan", 0xffffffff },
{ "tw", "Taiwan, Republic of China", "mandarin", 0xffffffff },
{ "tz", "Tanzania", "english,swahili", 0xffffffff },
{ "ua", "Ukraine", "ukrainian", 0xffffffff },
{ "ug", "Uganda", "english,swahili", 0xffffffff },
{ "uk", "United Kingdom", "british,cornish,dgÃÂ¨rnÃÂ©siais,"
	  "irish,jÃÂ¨rriais,pitcairnese,scots,scottish gaelic,"
	  "welsh", 0xffffffff },
{ "um", "United States Minor Outlying Islands", "english,carolinian,chamorro,"
	  "hawaiian,samoan,spanish", 0xffffffff },
{ "us", "United States of America", "english,carolinian,chamorro,english,"
	  "hawaiian,samoan,spanish", 0xffffffff },
{ "uy", "Uruguay", "spanish", 0xffffffff },
{ "uz", "Uzbekistan", "uzbek", 0xffffffff },
{ "va", "Vatican City State", "latin", 0xffffffff },
{ "vc", "Saint Vincent and the Grenadines", "english", 0xffffffff },
{ "ve", "Venezuela", "spanish", 0xffffffff },
{ "vg", "British Virgin Islands", "unknown", 0xffffffff },
{ "vi", "U.S. Virgin Islands", "unknown", 0xffffffff },
{ "vn", "Vietnam", "vietnamese", 0xffffffff },
{ "vu", "Vanuatu", "bislama,english,french", 0xffffffff },
{ "wf", "Wallis and Futuna", "unknown", 0xffffffff },
{ "ws", "Samoa", "english,samoan", 0xffffffff },
{ "ye", "Yemen", "arabic", 0xffffffff },
{ "yt", "Mayotte", "unknown", 0xffffffff },
{ "yu", "Yugoslavia", "unknown", 0xffffffff },
{ "za", "South Africa (Zuid-Afrika)", "afrikaans,english,ndebele,"
	  "northern sotho,sotho,swazi,tsonga,tswana,venda,xhosa,zulu", 
	  0xffffffff },
{ "zm", "Zambia", "english", 0xffffffff },
{ "zw", "Zimbabwe", "unknown", 0xffffffff },
};

static int s_langToCatId[] = {
	0,      // langUnknown
	0,      // langEnglish
	476,    // langFrench
	471,    // langSpanish
	484,    // langRussian
	49884,  // langJapanese
	472,    // langChineseTrad
	494,    // langChineseSimp
	493,    // langKorean
	911729, // langGerman
	478,    // langDutch
	477,    // langItalian
	503,    // langFinnish
	485,    // langSwedish
	487,    // langNorwegian
	483,    // langPortuguese
	116289, // langVietnamese
	88070,  // langArabic
	118215, // langHebrew
	464465, // langIndonesian
	482,    // langGreek
	501,    // langThai
	51663,  // langHindi
	241315, // langBengala
	480,    // langPolish
	173548, // langTagalog
	0, // langBritish (Sadly, there are no British, UK, or Austrialian topics)
	0, // langAustralia
	0       // langUnknown, end of list
};

LangList::LangList ( ) {
}

LangList::~LangList ( ) {
	reset();
}

void LangList::reset ( ) {
	m_langTable.reset();
	m_tldToCountry.reset();
}

// . returns false and sets errno on error
// . loads language lists into memory
// . looks under the langlist/ directory for langlist.# files
//   each number corrisponds to a language
bool LangList::loadLists ( ) {
	//log ( LOG_INIT, "lang: Loading Language Lists.");
	// init the term table
	m_langTable.set(8,4,100000*MAX_LANGUAGES,NULL,0,false,0,"tbl-lang");
	// loop over the languages and load the files
	int32_t listCount = 0;
	int32_t dupCount  = 0;
	int32_t allocSize   = 0;
	char *buf = NULL;
	Words w;
	for ( int32_t i = 0; i < MAX_LANGUAGES; i++ ) {
		// load the file for reading
		char ff[128];
		sprintf(ff, "%slanglist/langlist.%"INT32"", g_hostdb.m_dir, i );
		int fd = open ( ff, O_RDONLY );
		// no language file, don't complain
		if ( fd < 0 ) continue;
		// get the size
		struct stat stats;
		stats.st_size = 0;
		int status = stat ( ff, &stats );
		if ( status != 0 ) {
			close(fd);
			log ( "lang: Could not stat %s: %s.",
			      ff, strerror(errno) );
			return false;
		}
		int32_t fileSize = stats.st_size;
		// read the file into a buffer
		int32_t thisAllocSize = 3 * fileSize;
		if(thisAllocSize > allocSize) {
			buf = (char*)mrealloc(buf, allocSize, thisAllocSize,
					      "LangList");
			allocSize = thisAllocSize;
		}
		if ( !buf ) {
			close(fd);
			log ( "lang: Could not allocate %"INT32" bytes for "
			      "langlist buffer: %s.",
			      thisAllocSize, mstrerror(g_errno) );
			return false;
		}
		if ( read ( fd, buf, fileSize ) != fileSize ) {
			close(fd);
			log ( "lang: Could not read %s: %s.",
			      ff, strerror(errno) );
			return false;
		}
		close(fd);
		// read the words out of the file
		//		char *p    = buf;
		//		char *pEnd = buf + fileSize;
		//		*pEnd = '\0';
		
		//UChar* ucBuf = (UChar*)(buf + fileSize);
		//int32_t   ucBufLen = fileSize * 2;
		int32_t wordsInList = 0;
		int32_t writtenLen = gbstrlen(buf);
		//int32_t writtenLen = ucToUnicode(ucBuf, ucBufLen, 
		//			      buf, fileSize, 
		//			      "UTF-8", -1, 
		//			      TITLEREC_CURRENT_VERSION);

		w.reset();
		//doubling the written length seems hackish, may
		//need to be fixed in ucToUnicode.

		if(!w.set (buf ,
			   fileSize ,
			   TITLEREC_CURRENT_VERSION,true, false)) {
			char *xx = NULL; *xx = 0;
			return false;
		}
		
		int32_t numWords = w.getNumWords();
		for(int32_t j = 0; j < numWords; j++) {
			int64_t wordId = w.m_wordIds[j];
			if(wordId == 0) continue;
			// add it to the table
			uint32_t score = m_langTable.getScore(&wordId);
			//log(LOG_WARN, 
			//    "lang: Successfully hash %"INT64" from %s dictionary.", 
			//wordId, getLanguageString(i));
			if ( score !=  (uint32_t)i ) {
				if ( score > 0 ) {
					dupCount++;
					if ( score != 0x7fffffff )
						m_langTable.addTerm ( &wordId,
								 0x7fffffff);
				}
				else {
					m_langTable.addTerm ( &wordId, i );
					wordsInList++;
				}
			}
		}

		// count the list
		listCount++;
		
		if ( wordsInList > 0 )
		log ( LOG_DEBUG, 
		      "lang: Successfully Loaded %"INT32" out of %"INT32" (%"INT32" bytes) "
		      "words from %s dictionary.",
		      wordsInList, numWords>>1, writtenLen, getLanguageString(i) );
		

	}

	// free the buffer
	if(buf)	mfree ( buf, allocSize, "LangList" );


	log ( LOG_INIT, "lang: Successfully Loaded %"INT32" Language Lists and "
			"%"INT32" duplicate word hashes.",
			listCount, dupCount );
	// all good
	return true;
}

// . lookup word in language lists
// . returns false if not found true if found and lang set
bool LangList::lookup ( int64_t      termId,
			unsigned char *lang    ) {
	// lookup the termId in the table
	uint32_t score = m_langTable.getScore(&termId);
	// is it unknown?
	if ( score == 0 || score >= MAX_LANGUAGES ) {
		*lang = 0;
		return false;
	}
	// otherwise set lang to the score
	*lang = (unsigned char)score;
	return true;
}


char* LangList::getCountryFromTld(char* tld, int32_t tldLen) {
	//initialize if not already initialized.
	if(s_numTlds == 0) tldInit();

	int32_t j = 0;
	for(; j < tldLen; j++) {
		if(tld[j] != '.') continue;
		j++; //skip .
		tld = &(tld[j]);
		tldLen -= j;
		break;
	}

	int32_t index = hash32(tld, tldLen);
	int32_t slot = m_tldToCountry.getSlot(&index);

	if(slot < 0) return NULL;
	return s_tldInfo[*(int32_t *)m_tldToCountry.getValueFromSlot(slot)].m_country;
}


bool LangList::isLangValidForTld(char* tld, int32_t tldLen, unsigned char lang) {
	if(lang == langUnknown) return true; //not much we can do here.
	//initialize if not already initialized.
	if(s_numTlds == 0) tldInit();

	int32_t j = 0;
	for(; j < tldLen; j++) {
		if(tld[j] != '.') continue;
		j++; //skip .
		tld = &(tld[j]);
		tldLen -= j;
		break;
	}

	int32_t index = hash32(tld, tldLen);
	int32_t slot = m_tldToCountry.getSlot(&index);

	if(slot < 0) return true;
	int32_t *tip = (int32_t *)m_tldToCountry.getValueFromSlot(slot);
	if ( ! tip ) { char *xx=NULL;*xx=0; }
	TldInfo* t = &s_tldInfo[*tip];
	//it is uninitalized, init on demand.
	if(t->m_languagebv == 0xffffffff) { 
		t->m_languagebv = 0;
		for(int32_t i = 1; i <= langTagalog; i++) {
			if(strstr(t->m_languages,getLanguageString(i)) == NULL)
				continue;
			//set the bit corresponding to lang
			t->m_languagebv |= 0x1 << (i-1); 
		}
	}

	if(t->m_languagebv == 0) return true; //its unknown.

	int32_t mask = 0x1 << (lang-1);
	return mask & t->m_languagebv; 
}


bool LangList::tldInit() {
  s_numTlds = sizeof(s_tldInfo) / sizeof(TldInfo);
  m_tldToCountry.set(4,4,0,NULL,0,false,0,"tldctrytbl");
  for(int32_t i = 0; i < s_numTlds; i++) {
    int32_t ndx = hash32n(s_tldInfo[i].m_tld);
    if ( ! m_tldToCountry.addKey(&ndx , &i ) ) return false;
  }
  return true;
}

uint8_t LangList::catIdToLang(uint32_t catid) {
	register uint32_t i;
	for(i = 0; i < sizeof(s_langToCatId)/sizeof(uint32_t); i++) {
		if(catid == (uint32_t)s_langToCatId[i]) return((uint8_t)i);
	}
	return(0);
}

uint32_t LangList::langToCatId(uint8_t lang) {
	return(s_langToCatId[(int)lang]);
}

uint8_t LangList::isLangCat(int catid) {
	for(int x = 0; x < MAX_LANGUAGES; x++)
		if(catid == s_langToCatId[x])
			return(x);
	return(langUnknown);
}
