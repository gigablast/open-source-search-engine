/// \file language.c \brief Language detection utility routines.
///
/// Contains the main utility function, guessLanguage(), and all
/// the support routines for detecting the language of a web page.
///
/// 2007 May 24 09:02:52
/// $ID$
/// $Author: John Nanney$
/// $Workfile$
/// $Log$
///

// using a different macro because there's already a Language.h
#ifndef LANGUAGEIDENTIFIER_H
#define LANGUAGEIDENTIFIER_H

#include "gb-include.h"
#include "Xml.h"
#include "Linkdb.h"
//#include "LinkInfo.h"
#include "Query.h"

/// Contains methods of language identification by various means.
class LanguageIdentifier {
	public:
		/// Constructor, does very little.
		LanguageIdentifier();

		/// Destructor, does very little.
		~LanguageIdentifier() { return; }

		/// Get the language from the page's lang="" tag.
		///
		/// Looks for a lang="x" property in the HTML, BODY, or HEAD
		/// tag. Returns the first match. This is usually a very
		/// accurate guess of the language, since the author of the
		/// page went through all the trouble to make sure it was
		/// in there.
		///
		/// @param xml the page's xml object
		///
		/// @return the language, or langUnknown
		///
		uint8_t guessLanguageFromTag(Xml *xml);

		/// Guess the language from the TLDs of outlinks found in the page.
		///
		/// TLDs which are ambiguous like .com are skipped.
		///
		/// @param links a list of links
		///
		/// @return the language, or langUnknown
		///
		uint8_t guessLanguageFromOutlinks(Links *links);

		/// Guess the language from the page's TLD.
		///
		/// @param linktext the ascii URL
		///
		/// @return the language, or langUnknown
		///
		uint8_t guessLanguageFromTld(char *linktext);

		/// Guess the language from the languages of the inlinks.
		///
		/// @param linkInfo 
		///
		/// @return the language, or langUnknown
		///
		uint8_t guessLanguageFromInlinks(LinkInfo *linkInfo, int32_t ip);

		/// Determine whether a given TLD is suitable for language detection.
		/// @param tld the TLD in ascii
		/// @param len the length of tld
		/// @return true if suitable, false if not
		///
		inline bool isAmbiguousTLD(char *tld, int len);

		/// Return the greater of two ints.
		inline int maxOf(int a, int b) {
			if(b > a) return(b);
			return(a);
		}

		/// Guesses language from the DOCTYPE string present in many pages.
		///
		/// @param xml the page's xml object
		/// @param content the page's content, for finding the doctype
		///
		/// @return the language, or langUnknown
		///
		uint8_t guessLanguageFromDoctype(Xml *xml, char *content);

		/// Guess a language from a tag in the user agent string.
		///
		/// @param str the user agent string
		///
		/// @return the language, or langUknown
		///
		uint8_t guessLanguageFromUserAgent(char *str);
		/// Find a language for a given IP address.
		///
		/// @param address the address
		///
		/// @return the language ID, or langUnknown
		///
		uint8_t guessLanguageFromIP(uint32_t address);

		/// Find a country code for a given IP address.
		///
		/// This probably should not be called from user code,
		/// but it may be useful in some situations.
		///
		/// @param address the IP address as an unsigned int
		/// @param max optional, highest entry to search
		/// @param min optional, lowest entry to search
		/// @param *ldepth to be filled in by the highest depth of recursion
		///
		/// @return country code on success, or NULL (or "zz") on failure
		///
		char *findGeoIP(uint32_t address, uint32_t max,
				uint32_t min = 0, uint32_t *ldepth = NULL);

		/// Find an address in DMOZ for the language.
		///
		/// Looks up the page address in the category language tables.
		///
		/// @param addr the page address
		///
		/// @return language, or langUnknown if not found
		///
		uint8_t guessLanguageFromDMOZ(char *addr);

		/// Guess the query language from the query terms.
		///
		/// This algorithm looks for two consecutive terms with the
		/// same language.
		///
		/// @param q the query object
		///
		/// @return the language, or langUnknown
		///
		uint8_t guessLanguageFromQuery(Query *q);

		/// Find a language from DMOZ topic.
		///
		/// The function name is a bit misleading, we expect
		/// the country from the Top/World/X node.
		///
		/// @param topic the country name
		///
		/// @return the language, or langUnknown
		///
		uint8_t findLangFromDMOZTopic(char *topic);


	uint8_t getBestLanguage(char** method,
				Url* url,
				Xml* xml,
				Links* links,
				LinkInfo* linkInfo,
				char* content);

	uint8_t getBestLangsFromVec(char* langCount,
				    //SiteType* typeVec,
				    int32_t *langIds ,
				    uint8_t *langScores ,
				    int32_t tagVecSize);

	uint8_t guessGBLanguageFromUrl(char *url);
	uint8_t guessLanguageFromUrl(char *url);

	uint8_t guessLanguageFreqCount(Xml *xml,
			int pageLimit /* = 512 */);

	uint8_t guessCountryTLD(const char *url);
	uint8_t guessCountryIP(uint32_t ip);

	uint8_t guessCountryFromUserAgent(char *ua);
};

extern class LanguageIdentifier g_langId;
extern const uint8_t *langToTopic[];

#endif // LANGUAGEIDENTIFIER_H
