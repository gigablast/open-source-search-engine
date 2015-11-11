
// See docs in language.h

#include "gb-include.h"
#include "LanguageIdentifier.h"
#include "LangList.h"
#include "geo_ip_table.h"
#include "Tagdb.h"
#include "Speller.h"
#include "CountryCode.h"
#include "ValidPointer.h"
#include "Categories.h"
#include "Linkdb.h"

LanguageIdentifier g_langId;

/// List of TLDs that should not be used for language detection.
/// NULL terminated.
///
/// Sadly, .de seems to be about half German pages and about half
/// English as well. We cannot use it to distinguish language.
/// Also, .at has some english pages.
/// Also, .nl has some english pages.
/// Also, .no has some english pages.
/// Also, .vn has some english pages.
/// Also, .ro has some english pages.
/// Also, .gr has some english pages.
/// Also, .th has some english pages.
/// Also, .pl has some english pages.
/// Also, .gs has some english pages.
///
/// (Pretty soon it will be faster to have a list of domains that
/// WILL work instead of domains that won't.)
///
static char *ambiguousTLDs[] = {
	"info",
	"com",
	"org",
	"net",
	"mil",
	"de",
	"at",
	"tv",
	"nl",
	"no",
	"ws",
	"vn",
	"ro",
	"ru",
	"gr",
	"th",
	"pl",
	"gs",
	NULL
};

const uint8_t *langToTopic[] = {
	(uint8_t*)"Unknown",
	(uint8_t*)"English",
	(uint8_t*)"Français",
	(uint8_t*)"Español",
	(uint8_t*)"Russian",
	(uint8_t*)"There Is No 5!",
	(uint8_t*)"Japanese",
	(uint8_t*)"Chinese_Traditional",
	(uint8_t*)"Chinese_Simplified",
	(uint8_t*)"Korean",
	(uint8_t*)"Deutsch", // 10
	(uint8_t*)"Nederlands",
	(uint8_t*)"Italiano",
	(uint8_t*)"Suomi",
	(uint8_t*)"Svenska",
	(uint8_t*)"Norsk",
	(uint8_t*)"PortuguÃªs",
	(uint8_t*)"Vietnamese",
	(uint8_t*)"Arabic",
	(uint8_t*)"Hebrew",
	(uint8_t*)"Bahasa_Indonesia", // 20
	(uint8_t*)"Greek",
	(uint8_t*)"Thai", // 22
	(uint8_t*)"Hindi",
	(uint8_t*)"Bangla",
	(uint8_t*)"Polska",
	(uint8_t*)"Tagalog"
};

#define MAX_DOCTYPE_SEARCH_LEN (512)

/// Find a language tag in a DOCTYPE element.
///
/// This looks more complex than it is.
/// Find second quote mark, back up to
/// slash, move forward one, and that
/// should be the language identifier.
///
/// @param content pointer to the document's content
///
/// @return pointer to the language tag, or NULL
///
static char * FindLanguageIndex(char *content) {
	char *str;
	str = strchr(content, '"');
	if(!str)
		return(NULL);

	// Got first quote, skip it
	str++;
	str = strchr(str, '"');
	if(!str)
		return(NULL);

	// Got second quote char, skip it
	str++;
	// now back up to slash character...
	while(str && *str && str > content && *str != '/')
		str--;
	// make sure we found the slash...
	if(str && *str && str > content && *str == '/') {
		str++;
		return(str);
	}
	return(NULL);
}

/// Copy a language tag.
///
/// Does NULL terminate dst.
///
/// @param dst the destination
/// @param src the source (returned from FindLanguageIndex())
/// @param maxSize max length of dst, not counting NULL
///
/// @return true on successful copy, false otherwise
///
static bool copyLangTag(char *dst, char *src, int maxSize) {
	int len = 0;

	if(!dst || !src || maxSize < 1)
		return(false);

	while ( *src && *src != '"' ) { // && len++ < maxSize) {
		//if(len < 2) {
		//	*dst++ = tolower(*src++);
		//} else {
		//	*dst++ = *src++;
		//}
		*dst++ = tolower(*src++);
		// how many chars have we copied over?
		len++;
		// leave 1 char for a \0 termination
		if ( len + 1 >= maxSize ) break;
	}
	*dst = 0;
	return(true);
}


LanguageIdentifier::LanguageIdentifier() {
	return;
}

inline bool LanguageIdentifier::isAmbiguousTLD(char *tld, int len) {
	register int x;
	for(x = 0; ambiguousTLDs[x]; x++) {
		if(!strncmp(tld, ambiguousTLDs[x],
					maxOf(len, gbstrlen(ambiguousTLDs[x]))))
			return(true);
	}
	return(false);
}

uint8_t getLanguageFromAbbr2 ( char *str , int32_t len ) {
	// truncate
	if ( len > 5 ) len = 5;
	// copy it and check it
	char lang[6];
	for ( int32_t j = 0 ; j < len ; j++ )
		lang[j] = to_lower_a(str[j]);
	lang[len]='\0';
	return getLanguageFromAbbr(lang);
}

uint8_t LanguageIdentifier::guessLanguageFromTag(Xml *xml) {
	uint8_t rv = langUnknown;
	int32_t len = 0;
	//char lang[6];
	int id;
	char *str;

	if(!xml) return(langUnknown);

	for(int32_t i = 0; i < xml->getNumNodes(); i++) {
		id = xml->getNodeId(i);

		// look for meta tag
		if(id == TAG_META) {
			str = (char *) xml->getString(i, "name", &len);
			if(str &&
			   (!strncasecmp(str, "Content-Language",16) ||
			    !strncasecmp(str, "language",8) ||
			    !strncasecmp(str, "Content_Language",16) ) ) {
				str = (char *) xml->getString(i, "content", &len);
				rv = getLanguageFromAbbr2(str,len);
				if(rv != langUnknown) return(rv);
			}
			else {
				str = (char *) xml->getString(i, "http-equiv", &len);
				if(str && !strncasecmp(str, "Language", 8) ) {
					str = (char *) xml->getString(i, "content", &len);
					rv = getLanguageFromAbbr2(str,len);
					if(rv != langUnknown) return(rv);
				}
			}
		}  // end looking for meta tag


		if(id != TAG_HTML &&      // html
		   id != TAG_BODY && // body
		   id != TAG_HEAD)   // head
			continue;

		str = (char *) xml->getString(i, "lang", &len);
		rv = getLanguageFromAbbr2(str,len);
		if(rv != langUnknown) return(rv);
	}
	return(rv);
}

uint8_t LanguageIdentifier::guessLanguageFromOutlinks(Links *links) {
	char link[MAX_URL_LEN];
	int32_t langs[32];
	int lc;
	char *cp = NULL;
	int max = 0;
	int oldmax = 0;
	uint8_t l;
	uint8_t maxlang = 0;
	int len;

	if(!links) return(langUnknown);

	// Try to catch bad pointers
	//if(!isValidPointer(links)) {
	//	log(LOG_WARN, "build: Bad pointer 0x%08x not above data segment.\n",
	//			(uint32_t) links);
	//	return(langUnknown);
	//}

	if(links->getNumLinks() < 1) {
		return(langUnknown);
	}

	if(links->getNumLinks() < 15) {
		return(langUnknown);
	}

	// clear list
	memset(langs, 0, sizeof(uint32_t) * 32);

	// trim to only 100 links to prevent
	// spinning on some large pages
	for(lc = 0; lc < links->getNumLinks() && lc < 100; lc++) {
		cp = links->getLink(lc);

		if(cp) {
			// skip http://
			cp += 7;
			
			len = links->getLinkLen(lc) - 7;
			char* p = link;
			while(*cp && *cp != '/') *p++ = *cp++;
			*p = '\0';

			if((cp = strrchr(link, '.')) != NULL) {

				// skip to tld
				cp++;

				// only bother if not a common TLD
				len = gbstrlen(cp);
				if(!isAmbiguousTLD(cp, len)) {
					for(l = 1; l < 32; l++) {
						if(g_langList.isLangValidForTld(cp, len, l)) 
							langs[l]++;
					}
				}
			}
		}
	}

	// look for a clear winner from the list
	// don't bother with langUnknown, it reduces hits
	for(l = 1; l < 32; l++) {
		if(langs[l] >= max) {
			oldmax = max;
			max = langs[l];
			maxlang = l;
		}
	}

	// 1st place must beat 2nd place by 5
	if(max - oldmax > 5) {
		return(maxlang);
	}
	return(langUnknown);
}

uint8_t LanguageIdentifier::guessLanguageFromTld(char *linktext) {
#if 0
	// This is not a good check of language
	int len = 0;
	char *cp;

	if(!linktext) return(langUnknown);

	// skip http://
	cp = linktext + 7;

	// if no slash, start at the end of the link
	if(!(cp = strchr(cp, '/')))
		cp = linktext + (gbstrlen(linktext) - 1);

	// find last dot
	while(*cp && cp > linktext && *cp != '.') {
		cp--;
		len++;
	}

	// skip '.'
	len--; cp++;

	if(len != 2) return(langUnknown);
#endif // 0

	return(langUnknown);

}

uint8_t LanguageIdentifier::guessLanguageFromInlinks(LinkInfo *linkInfo, int32_t ip) {
	int32_t x;
	//int32_t y;
	uint8_t languages[32];
	uint8_t max = langUnknown;
	uint8_t oldmax = langUnknown;
	uint8_t maxIndex = 0;
	uint8_t oldmaxIndex = 0;
	int hits = 0;

	// sanity check
	//if(linkInfo->m_numLangs != linkInfo->getNumDocIds()) {
	//	log(LOG_DEBUG, "build: Number of languages (%"INT32") != number of docids (%"INT32")\n",
	//			linkInfo->m_numLangs, linkInfo->getNumDocIds());
	//	return(langUnknown);
	//}

	if(linkInfo->getNumGoodInlinks() < 7) return(langUnknown);

	memset(languages, 0, 32);

	// only check the first 100 inlinks, or we'll spin
	// on some monstrous sites.
	//for(x = 0; x < linkInfo->m_numLangs && x < 100; x++) {
	for (Inlink*k=NULL;(k=linkInfo->getNextInlink(k)); ) {
		//int32_t id = linkInfo->getLanguageId(x);
		int32_t id = k->m_language;
		// sanity check, we are still getting bad lang ids!!
		if ( id < 0 || id >= 32 ) {
			log("build: Got bad lang id of %"INT32". how can this "
			    "happen?",id);
			continue;
		}
		// don't count langUnknown pages, it reduces hits
		if ( ! id ) continue;

		// skip if not from a different enough IP
		if((k->m_ip&0x0000ffff)==(ip&0x0000ffff) )
			continue;
		// otherwise count it
		languages[id]++;
		hits++;
	}
	if(hits < 7) return(langUnknown);
	for(x = 1; x < 32; x++) {
		if(languages[x] >= max) {
			oldmax = max;
			max = languages[x];
			oldmaxIndex = maxIndex;
			maxIndex = x;
		}
	}

	// sanity check
	if(maxIndex > 31 || oldmaxIndex > 31) {
		log(LOG_INFO,
			"build: guessLanguageFromInlinks(): Possible stack corruption: %d:%d\n",
				maxIndex, oldmaxIndex);
		return(langUnknown);
	}

	// Need better than 50%
	// if(max - oldmax > 4)
	if(max > (linkInfo->getNumGoodInlinks() / 2))
		return(maxIndex);
	return(langUnknown);
}

uint8_t LanguageIdentifier::guessLanguageFromDoctype(Xml *xml, char *content) {
	uint8_t rvDoc = langUnknown;
	int id;
	char *str;
	char lang[6];

	if(!content) return(langUnknown);

	for(int32_t i = 0; i < xml->getNumNodes(); i++) {
		id = xml->getNodeId(i);
		// skip if not DOCTYPE
		if ( id != TAG_DOCTYPE ) continue;
		// get the tag ptr to the tag
		char *tag    = xml->getNode(i);
		// this is in BYTES
		//int32_t  tagLen = xml->getNodeLen(i);
		// case might be upper, so we change
		// the first two letters to lower.
		str = FindLanguageIndex(tag);
		if(!str) continue;
		if(copyLangTag(lang, str, 5))
			rvDoc = getLanguageFromAbbr(lang);
		return(rvDoc);
	}
	return(rvDoc);
}

/// Skip whitespace in a string.
///
/// Includes CR and LF.
///
/// @param str the string
///
/// @return pointer to next character that is not whitespace, or NULL
///
static char *skipwhite(char *str) {
	while(str && *str &&
			(*str == ' ' ||
			 *str == '\t' ||
			 *str == '\n' ||
			 *str == '\r'))
		str++;
	return(str);
}

/// Skip over 'words' in a string.
///
/// Skips over everything until there's whitespace.
///
/// @param str the string to search
///
/// @return the pointer to the next whitespace character
///
static char *skipword(char *str) {
	while(str && *str &&
			(*str != ' ' &&
			 *str != '\t' &&
			 *str != '\n' &&
			 *str != '\r'))
		str++;
	return(str);
}

uint8_t LanguageIdentifier::guessLanguageFromUserAgent(char *str) {
	// Mozilla/5.0 (X11; U; Linux i686;
	// en-US; rv:1.8.1.4) Gecko/20070531 Firefox/2.0.0.4
	uint8_t lang = langUnknown;
	while(*str) {
		if(!(str = skipwhite(str)))
			return(langUnknown);
		if((lang = getLanguageFromUserAgent(str)) != langUnknown)
				return(lang);
		if(!(str = skipword(str)))
			return(langUnknown);
	}
	return(langUnknown);
}

// non-recursive bisect search
char *LanguageIdentifier::findGeoIP(uint32_t address, uint32_t max,
		uint32_t min, uint32_t *ldepth) {
#if 0
	uint32_t limit = max;
	register uint32_t median;

	if(aGeoIP[0].firstAddr > address || aGeoIP[max].lastAddr < address) {
		return("ob");
	}

	do {
		// extra debugging steps
		if(ldepth) {
			*ldepth += 1;
			if(*ldepth > limit) {
				log(LOG_INFO, "build: findGeoIP(): depth exceeded limit.\n");
				return("zz");
			}
		}

		median = (max+min)/2;

		// check if narrowed all the way
		if(median == max || median == min) {
			break;
		}

		// bisect down?
		if(aGeoIP[median].firstAddr > address) {
			max = median;
			continue;
		}

		// bisect up?
		if(aGeoIP[median].lastAddr < address) {
			min = median;
			continue;
		}

		// in range, pop out
		break;

	} while(max > min);

	if(aGeoIP[median].firstAddr <= address && aGeoIP[median].lastAddr >= address)
		return(aGeoIP[median].cCode);

#endif // 0
	return("zz");
}

uint8_t LanguageIdentifier::guessLanguageFromIP(uint32_t address) {
	return langUnknown; // temp change
	uint32_t ldepth = 0;
	char *code = findGeoIP(address, geoIPNumRows - 1, 0, &ldepth);
	if(!code) return(langUnknown);
	if(code[0] == 'z' && code[1] == 'z') return(langUnknown);
	if(code[0] == 'o' && code[1] == 'b') return(langUnknown);

	// return unknown for some ambiguous results
	if(code[0] == 'e' && code[1] == 'u') return(langUnknown);
	if(code[0] == 'c' && code[1] == 'a') return(langUnknown);

	return(getLanguageFromCountryCode(code));
}

uint8_t LanguageIdentifier::guessLanguageFromDMOZ(char *addr) {
	return(g_categories->findLanguage(addr));
}

uint8_t LanguageIdentifier::guessLanguageFromQuery(Query *q) {
	uint8_t lang;
	if(q->getNumTerms() == 1) {
		if(g_langList.lookup(q->getTermId(1), &lang))
			return(lang);
	} else {
		// Look for two consecutive identical languages
		// Not as good as a frequency count, but much faster
		uint8_t last = 255;
		register int32_t qcount;
		for(qcount = 0; qcount < q->getNumTerms(); qcount++) {
			if(g_langList.lookup(q->getTermId(qcount), &lang) &&
					last == lang) {
				return(lang);
				break;
			}
		}
	}
	return(langUnknown);
}

uint8_t LanguageIdentifier::getBestLanguage(char** method,
					    Url* url,
					    Xml* xml,
					    Links* links,
					    LinkInfo* linkInfo,
					    char* content) {
	uint8_t langEnum;
	// Let the site tell us what language it's in
	langEnum = g_langId.guessLanguageFromTag(xml);
	*method = "Tag";

	if(langEnum != langUnknown) return langEnum;

	// Get the language from a DMOZ category
	// Accurate, but low hit rate
	langEnum = g_langId.guessLanguageFromDMOZ(url->getUrl());
	*method = "DMOZ";
	if(langEnum != langUnknown) return langEnum;


	// Guess from the TLD
	uint8_t possibleLanguage = g_langId.guessLanguageFromTld(url->getUrl());
	if(possibleLanguage) langEnum = possibleLanguage;
	*method = "TLD";
	if(langEnum != langUnknown) return langEnum;

	// m_newDoc->getLinks() can return a bad address
	// Guess from the outlinks
	langEnum = g_langId.guessLanguageFromOutlinks(links);
	*method = "Outlinks";
	if(langEnum != langUnknown) return langEnum;
	// m_newDoc->getLinks() can return a bad address

	// Guess from the inlinks
	//	langEnum = g_langId.guessLanguageFromInlinks(linkInfo);
	//	*method = "Inlinks";
	if(langEnum != langUnknown) return langEnum;

	// Word frequency count
	langEnum = xml->getLanguage();
	*method = "Freq";
	if(langEnum != langUnknown) return langEnum;

	// Let the doctype tell us what language it's in
	langEnum = g_langId.guessLanguageFromDoctype(xml, content);
	*method = "Doctype";

	return langEnum;
}



uint8_t LanguageIdentifier::getBestLangsFromVec(char* langCount,
						//SiteType* typeVec,
						int32_t *langIds ,
						uint8_t *langScores ,
						int32_t tagVecSize) {
	int32_t bestCount = -1;
	uint8_t numTags = 0;

	int32_t langTotal = 0;
	for(int32_t j = 0; j < MAX_LANGUAGES; j++) {	
		langTotal += langCount[j];
	}
	if(langTotal == 0 || langCount[langUnknown] == langTotal)
		return 0;

	//dont store unknown language
	langTotal -= langCount[langUnknown];
	langCount[langUnknown] = 0;
	

	for(int32_t i = 0; i < tagVecSize; i++) {
		int32_t maxCount = 0;
		int32_t maxCountNdx = 0;
		for(int32_t j = 0; j < MAX_LANGUAGES; j++) {	
			if(langCount[j] > maxCount) {
				maxCount = langCount[j];
				maxCountNdx = j;
			}
		}
		if(i == 0) bestCount = maxCount;
		//if none found or this one is half as much as previous
		//then quit.
		if(maxCount == 0 ||
		   maxCount < (bestCount/2)) break;
		//typeVec[i].m_type = maxCountNdx;
		//typeVec[i].m_score = (uint8_t)((maxCount * 100.0) 
		//			       / langTotal);
		langIds   [i] = maxCountNdx;
		langScores[i] = (uint8_t)((maxCount * 100.0) / langTotal);
		langCount[maxCountNdx] = 0;
		numTags++;
	}
	return numTags;
}


uint8_t LanguageIdentifier::findLangFromDMOZTopic(char *topic) {
	int x;
	for(x = 0; x < (int)(sizeof(langToTopic)/sizeof(uint8_t *)); x++) {
		if ( ! langToTopic[x] ) continue;
		if(!strncasecmp((char*)langToTopic[x], topic,
				gbstrlen((char *)langToTopic[x])))
			return(x);
	}
	return(langUnknown);
}

uint8_t LanguageIdentifier::guessGBLanguageFromUrl(char *url) {
	if(!url) return(langUnknown);
	uint8_t lang;
	if((lang = guessLanguageFromUrl(url)) != langUnknown)
		return(lang);
	char code[6];
	char *cp = url;
	memset(code, 0, 6);
	for(int x = 0; x < 6; x++) {
		if((cp[x] < 'a' || cp[x] > 'z') &&
				(cp[x] < 'A' || cp[x] > 'Z') &&
				cp[x] != '_' && cp[x] != '-')
			break;
		code[x] = cp[x];
	}
	return(getLanguageFromCountryCode(code));
}

static inline bool s_checkCharIsBoundary(uint8_t x) {
		if(x < '0') return(true);
		if(x > '9' && x < 'A') return(true);
		if(x > 'Z' && x < 'a') return(true);
		if(x > 'z' && x < 128) return(true);
		return(false);
}

static inline bool s_isRightBoundedAbbr(char *pointer, uint8_t l) {
	if(s_checkCharIsBoundary(*(pointer + 2)))
		return(true);
	if((*(pointer + 3) == '-' || *(pointer + 3) == '_') &&
			s_checkCharIsBoundary(*(pointer + 5)))
		return(true);
	return(false);
}

static inline bool s_isRightBoundedLanguageWord(char *pointer, uint8_t l) {
	if(s_checkCharIsBoundary(*(pointer + gbstrlen(getNativeLanguageString(l)))))
		return(true);
	if(s_checkCharIsBoundary(*(pointer + gbstrlen(getLanguageString(l)))))
		return(true);
	return(false);
}

uint8_t s_lookForLanguageParam(char *url) {
	char *cp = url;
	uint8_t l;
	// Try to find lan= or lang= or language=
	while(cp && *cp && (cp = strstr(cp, "lan"))) {
		if(!s_checkCharIsBoundary(*(cp - 1))) {
			cp++;
			continue;
		}
		if(!strncmp(cp, "lan=", 4)) cp += 4;
		else if(!strncmp(cp, "lang=", 5)) cp += 5;
		else if(!strncmp(cp, "language=", 9)) cp += 9;

		if((l = getLanguageFromName((uint8_t*)cp)) &&
				s_isRightBoundedLanguageWord(cp, l))
			return(l);

		if((l = getLanguageFromAbbrN(cp)) &&
				s_isRightBoundedAbbr(cp, l))
			return(l);
		cp++;
	}
	// Try to find l=
	cp = url;
	while(cp && *cp && (cp = strstr(cp, "l="))) {
		if(!s_checkCharIsBoundary(*(cp - 1))) {
			cp++;
			continue;
		}

		if((l = getLanguageFromName((uint8_t*)cp)) &&
				s_isRightBoundedLanguageWord(cp, l))
			return(l);

		if((l = getLanguageFromAbbrN(cp)) &&
				s_isRightBoundedAbbr(cp, l))
			return(l);
		cp++;
	}
	return(0);
}

uint8_t s_lookForLanguagePrefix(char *url) {
	char *cp = url;
	uint8_t l = 0;
	// Look for a prefix on the url
	// Do not add a postfix or TLD detector,
	// they are not good indications at all.
	if(!strncmp(url, "http://", 7)) cp = url + 7;
	else cp = url;

	if((l = getLanguageFromAbbrN(cp)) &&
			s_isRightBoundedAbbr(cp, l))
		return(l);

	// Lookup, and see if it's on a word boundary
	if((l = getLanguageFromName((uint8_t*)cp)) &&
			s_isRightBoundedLanguageWord(cp, l))
		return(l);
	return(0);
}


uint8_t LanguageIdentifier::guessLanguageFromUrl(char *url) {
	int len = 0;
	char *cp = url;
	char code[3];
	uint8_t l = 0;

	if(!url) return(langUnknown);

	// Look for a parameter that would indicate the language
	if((l = s_lookForLanguageParam(url))) return(l);

	// Look for a prefix that would indicate the language
	if((l = s_lookForLanguagePrefix(url))) return(l);

	// if no slash, start at the end of the link
	if(!(cp = strchr(url, '/')))
		cp = url + (gbstrlen(url) - 1);

	// find last dot
	while(*cp && cp > url && *cp != '.') {
		cp--;
		len++;
	}

	// No dot?
	if(cp <= url) return(langUnknown);

	// skip '.'
	len--; cp++;

	code[0] = cp[0];
	code[1] = cp[1];
	code[2] = 0;

	return(getLanguageFromCountryCode(code));
}

static inline int s_findMaxInList(int *list, int numItems) {
	int max, oldmax, idx;
	if(!list) return(0);
	max = oldmax = INT_MIN;
	idx = 0;
	for(int x = 0; x < numItems; x++) {
		if(list[x] >= max) {
			oldmax = max;
			max = list[x];
			idx = x;
		}
	}
	if(oldmax == max) return(0);
	return(idx);
}

uint8_t LanguageIdentifier::guessLanguageFreqCount(Xml *xml,
		int pageLimit /* = 512 */) {
	if(!xml) return(langUnknown);

	int votes[MAX_LANGUAGES];
	int limit = xml->getNumNodes();
	int scores[MAX_LANGUAGES];

	if(pageLimit < limit) limit = pageLimit;

	memset(votes, 0, sizeof(int) * MAX_LANGUAGES);

	// Do term frequency count
	for(int x = 0; x < limit; x++) {
		if(xml->isTag(x) || xml->getNodeLen((int32_t)x) < 2) continue;
		char *cp = g_speller.getPhraseRecord(xml->getNode((int32_t)x),
						     xml->getNodeLen((int32_t)x));
		if(!cp) continue;
		memset(scores, 0, sizeof(int) * MAX_LANGUAGES);
		while(*cp) {
			// skip leading whitespace
			while(*cp && (*cp == ' ' || *cp == '\t')) cp++;
			// get language
			int l = atoi(cp);
			// skip to next delimiter
			while(*cp && *cp != '\t') cp++;
			// skip over tab
			cp++;
			// get score
			scores[l] = atoi(cp);
			// skip to next delimiter
			while(*cp && *cp != '\t') cp++;
		}
		votes[s_findMaxInList(scores, MAX_LANGUAGES)]++;
	}

	// Find max
	int max = 0;
	int maxidx = 0;
	int oldmax = 0;
	for(int x = 0; x < MAX_LANGUAGES; x++) {
		if(votes[x] < max) continue;
		oldmax = max;
		max = votes[x];
		maxidx = x;
	}

	if(max == 0) maxidx = 0;

#if 0
	// English, British, and Australian are no longer separate
	// If it's a toss up between any version of English, go with it.
	if((max == langEnglish || max == langAustralia || max == langBritish) &&
			(oldmax == langEnglish || oldmax == langAustralia || oldmax == langBritish))
		return(maxidx);
#endif // 0

	// Note the winner
	if(oldmax <= 0 || max > oldmax)
		return maxidx;
	return langUnknown;
}

uint8_t LanguageIdentifier::guessCountryTLD(const char *url) {
	uint8_t country = 0;
	char code[3];
	code[0] = code[1] = code [2] = 0;

	// check for prefix
	if(url[9] == '.') {
		code[0] = url[7];
		code[1] = url[8];
		code[2] = 0;
		country = g_countryCode.getIndexOfAbbr(code);
		if(country) return(country);
	}

	// Check for two letter TLD
	const char *cp = strchr(url+7, ':');
	if(!cp)
		cp = strchr(url+7, '/');
	if(cp && *(cp -3) == '.') {
		cp -= 2;
		code[0] = cp[0];
		code[1] = cp[1];
		code[2] = 0;
		country = g_countryCode.getIndexOfAbbr(code);
		if(country) return(country);
	}
	return(country);
}

uint8_t LanguageIdentifier::guessCountryIP(uint32_t ip) {
	// Lookup IP address
	uint8_t country = 0;
	char *codep = findGeoIP(ip, geoIPNumRows - 1, 0);
	if(!codep) return(0);
	country = g_countryCode.getIndexOfAbbr(codep);
	return(country);
}

static int s_wordLen(char *str) {
	char *cp = str;
	while(*cp && *cp != ' ' && *cp != ';' &&*cp != '\t' &&
			*cp != '\n' && *cp != '\r' && *cp != '.' && *cp != ',')
		cp++;
	return(cp - str);
}

static bool s_isLangTag(char *str) {
	int len = s_wordLen(str);
	if(len == 2) return(true);
	if(len != 5) return(false);
	if(str[2] == '_' || str[2] == '-') return(true);
	return(false);
}

static uint8_t s_getCountryFromSpec(char *str) {
	char code[6];
	memset(code, 0,6);
	gbmemcpy(code, str, s_wordLen(str));
	for(int x = 0; x < 6; x++)
		if(code[x] > 'A' && code[x] < 'Z') code[x] -= ('A' - 'a');
	if(code[2] == '_' || code[2] == '-')
		return g_countryCode.getIndexOfAbbr(&code[3]);
	return g_countryCode.getIndexOfAbbr(code);
}

uint8_t LanguageIdentifier::guessCountryFromUserAgent(char *ua) {
	if(!ua) return(0);
	uint8_t country = 0;
	while(*ua) {
		if(!(ua = skipwhite(ua)))
			return(0);
		if(s_isLangTag(ua) &&
				(country = s_getCountryFromSpec(ua)) != 0)
				return(country);
		if(!(ua = skipword(ua)))
			return(0);
	}
	return(0);
}

