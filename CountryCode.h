
#ifndef _COUNTRYCODE_H
#define _COUNTRYCODE_H

#include "HashTableT.h"
#include "types.h"

// . used by Events.cpp to keep things small
// . get a single byte country id from a 2 character country code
uint8_t getCountryId ( char *cc ) ;

// map a country id to the two letter country abbr
char *getCountryCode ( uint8_t crid );

class CountryCode {
	public:
		CountryCode();
		~CountryCode();
		void init(void);
		int getNumCodes(void);
		const char *getAbbr(int index);
		const char *getName(int index);
		int getIndexOfAbbr(const char *abbr);
		unsigned short getCountryFromDMOZ(long catid);
		uint8_t getLanguageFromDMOZ(long catid);
		int createHashTable(void);
		bool loadHashTable(void);
		long getNumEntries(void);
		void debugDumpNumbers(void);
         	uint64_t getLanguagesWritten(int index);
	private:
		int fillRegexTable(void);
		void freeRegexTable(void);
		int lookupCountryFromDMOZTopic(const char *catname, int len);
		bool m_init;
		HashTableT<unsigned short, int>m_abbrToIndex;
		HashTableT<unsigned short, const char *>m_abbrToName;
};

extern CountryCode g_countryCode;

// We're currently at 24x or so...
#define MAX_COUNTRIES (255)

#endif // _COUNTRYCODE_H

