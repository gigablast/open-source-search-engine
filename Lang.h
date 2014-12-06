// Matt Wells, copyright Jul 2001

// . language detector
// . TODO: use stopwords in doc to determine the language

#ifndef _LANG_H_
#define _LANG_H_

#define MAX_LANGUAGES 64
// for langs 1-55, exclude translingual
// 64 - 8 is 56, then minus 1 is 55 bits
// translingual is the 31st bit, english is the first bit
#define LANG_BIT_MASK 0x007fffffffffffffLL
#include "Unicode.h"
#include "Iso8859.h"
#include "iana_charset.h"
enum {
	langUnknown     = 0,
	langEnglish     = 1,
	langFrench      = 2,
	langSpanish     = 3,
	langRussian     = 4,
	langTurkish     = 5,
	langJapanese    = 6,
	langChineseTrad = 7, // cantonese
	langChineseSimp = 8, // mandarin
	langKorean      = 9,
	langGerman      = 10,
	langDutch       = 11,
	langItalian     = 12,
	langFinnish     = 13,
	langSwedish     = 14,
	langNorwegian   = 15,
	langPortuguese  = 16,
	langVietnamese  = 17,
	langArabic      = 18,
	langHebrew      = 19,
	langIndonesian  = 20,
	langGreek       = 21,
	langThai        = 22,
	langHindi       = 23,
	langBengala     = 24,
	langPolish      = 25,
	langTagalog     = 26,

	// added for wiktionary
	langLatin          = 27,
	langEsperanto      = 28,
	langCatalan        = 29,
	langBulgarian      = 30,
	langTranslingual   = 31, // used by multiple langs in wiktionary
	langSerboCroatian  = 32,
	langHungarian      = 33,
	langDanish         = 34,
	langLithuanian     = 35,
	langCzech          = 36,
	langGalician       = 37,
	langGeorgian       = 38,
	langScottishGaelic = 39,
	langGothic         = 40,
	langRomanian       = 41,
	langIrish          = 42,
	langLatvian        = 43,
	langArmenian       = 44,
	langIcelandic      = 45,
	langAncientGreek   = 46,
	langManx           = 47,
	langIdo            = 48,
	langPersian        = 49,
	langTelugu         = 50,
	langVenetian       = 51,
	langMalgasy        = 52,
	langKurdish        = 53,
	langLuxembourgish  = 54,
	langEstonian       = 55,
	langLast           = 56
};

uint8_t getLanguageFromName(uint8_t *name);
uint8_t getLangIdFromAbbr ( char *abbr ) ;
char *getLangAbbr ( uint8_t langId ) ;

void  	 languageToString ( unsigned char lang , char *buf );
char* 	 getLanguageString ( unsigned char lang);
char* 	 getNativeLanguageString ( unsigned char lang);
char*    getLanguageAbbr ( unsigned char lang);
unsigned char  getLanguageCharset ( unsigned char LangId );
bool     isAdult( char *s, int32_t slen, char **loc = NULL );
//unsigned char getLanguageFromScript(UChar32 c);
unsigned char getLanguageFromAbbr(char *abbr);
unsigned char getLanguageFromAbbrN(char *abbr);
//unsigned char getLanguageFromUnicodeAbbr(UChar *abbr);
// abbr is now in utf8
unsigned char getLanguageFromUnicodeAbbr(char *abbr);
unsigned char getLanguageFromUserAgent(char *abbr);
unsigned char getLanguageFromCountryCode(char *code);

#endif
