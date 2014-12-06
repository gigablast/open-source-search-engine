// Gigablast, copyright Aug 2005
// Author: Javier Olivares <jolivares@gigablast.com>
//
// . stores lists of common words for various languages
// . used to determine what language a word/page belongs to
//

#ifndef _LANGLIST_H_
#define _LANGLIST_H_

//#include "TermTable.h"
#include "Words.h"
#include "Lang.h"
#include "HashTableX.h"

class LangList {
public:
	LangList  ( );
	~LangList ( );

	void reset ( );
	// . returns false and sets errno on error
	// . loads language lists into memory
	// . looks under the langlist/ directory for langlist.# files
	//   each number corrisponds to a language
	bool loadLists ( );

	// . lookup word in language lists
	// . returns false if unknown true if found and lang set
	bool lookup ( int64_t      termId,
		      unsigned char *lang   );
	
	char* getCountryFromTld(char* tld, int32_t tldLen);
	bool  isLangValidForTld(char* tld, int32_t tldLen, unsigned char lang);
	bool  tldInit();

	inline uint8_t catIdToLang(uint32_t catid);
	inline uint32_t langToCatId(uint8_t lang);
	uint8_t isLangCat(int catid);


private:
	//TermTable langTable;
	//HashTableT<int32_t, int16_t> m_tldToCountry;
	HashTableX m_langTable;
	HashTableX m_tldToCountry;
};

extern class LangList g_langList;

#endif
