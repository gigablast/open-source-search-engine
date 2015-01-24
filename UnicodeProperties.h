#ifndef UNICODE_PROPERTIES_H__
#define UNICODE_PROPERTIES_H__
#include <sys/types.h>
#include "UCPropTable.h"


#ifndef USE_ICU
typedef uint32_t  UChar32;
typedef uint16_t UChar;
typedef unsigned char  UChar8;   // utf-8
#endif
typedef uint16_t UCProps;
typedef unsigned char  UCScript;


bool loadUnicodeTable(UCPropTable *table, char *filename, bool useChecksum = false, uint32_t expectedChecksum = 0);
bool saveUnicodeTable(UCPropTable *table, char *filename);
// JAB: we now have Kompatible and Canonical decomposition tables
bool saveKDecompTable(char *baseDir = NULL) ;
bool saveCDecompTable(char *baseDir = NULL) ;
// JAB: we now have Kompatible and Canonical decomposition tables
bool loadDecompTables(char *baseDir = NULL) ;
void resetDecompTables() ;

bool     setKDValue(UChar32 c, UChar32* decomp, int32_t decompCount,
		    bool fullComp = false);
UChar32 *getKDValue(UChar32 c, int32_t *decompCount, bool *fullComp = NULL);
int32_t     recursiveKDExpand(UChar32 c, UChar32 *buf, int32_t bufSize);
		       
// JAB: we now have Kompatible and Canonical decomposition tables
bool     setCDValue(UChar32 c, UChar32* decomp, int32_t decompCount,
		    bool fullComp = false);
UChar32 *getCDValue(UChar32 c, int32_t *decompCount);
int32_t     recursiveCDExpand(UChar32 c, UChar32 *buf, int32_t bufSize);

UCProps ucProperties(UChar32 c);
bool    ucIsAlpha(UChar32 c);
bool    ucIsDigit(UChar32 c);
bool    ucIsAlnum(UChar32 c);

bool    ucIsUpper(UChar32 c);
bool    ucIsLower(UChar32 c);

int32_t ucDigitValue(UChar32 c);

UChar32 ucToLower(UChar32 c);
UChar32 ucToUpper(UChar32 c);

unsigned char ucCombiningClass(UChar32 c);


bool ucIsWhiteSpace(UChar32 c);
bool is_wspace_uc(UChar32 c);
bool ucIsIdeograph(UChar32 c);
bool ucIsPunct(UChar32 c);
bool is_punct_uc(UChar32 c);
bool ucIsWordChar(UChar32 c);
bool ucIsIgnorable(UChar32 c);
bool ucIsExtend(UChar32 c);

bool isNFKC(UChar *s, int32_t len);
UCScript ucGetScript(UChar32 c);


// Parse Properties
#define UC_WORDCHAR   ( 1 << 0 )
#define UC_IGNORABLE  ( 1 << 1 )
#define UC_IDEOGRAPH  ( 1 << 2 )
#define UC_HIRAGANA   ( 1 << 3 )
#define UC_KATAKANA   ( 1 << 4 )
#define UC_THAI       ( 1 << 5 )
#define UC_EXTEND     ( 1 << 6 )
// General Properties
#define UC_ALPHA      ( 1 << 7 )
#define UC_DIGIT      ( 1 << 8 )
#define UC_UPPER      ( 1 << 9 )
#define UC_LOWER      ( 1 << 10 )
#define UC_WHITESPACE ( 1 << 11 )
#define UC_NFKC_QC_NO ( 1 << 12 )
#define UC_COMP_EX    ( 1 << 13 )

extern UCPropTable g_ucProps;
extern UCPropTable g_ucLowerMap;
extern UCPropTable g_ucUpperMap;
extern UCPropTable g_ucCategory;
extern UCPropTable g_ucScripts;
extern UCPropTable g_ucKDIndex;
extern UCPropTable g_ucCombiningClass;


extern char *g_ucScriptNames[];

enum ucScript {
	ucScriptCommon = 0,
	ucScriptArabic,
	ucScriptArmenian,
	ucScriptBengali,
	ucScriptBopomofo,
	ucScriptBraille,
	ucScriptBuhid,
	ucScriptCanadian_Aboriginal,
	ucScriptCherokee,
	ucScriptCypriot,
	ucScriptCyrillic, // 10 = russian
	ucScriptDeseret,
	ucScriptDevanagari,
	ucScriptEthiopic,
	ucScriptGeorgian,
	ucScriptGothic, // 15
	ucScriptGreek,
	ucScriptGujarati,
	ucScriptGurmukhi,
	ucScriptHan,
	ucScriptHangul, // 20
	ucScriptHanunoo,
	ucScriptHebrew, // 22
	ucScriptHiragana,
	ucScriptInherited, // 24
	ucScriptKannada,
	ucScriptKatakana,
	ucScriptKatakana_Or_Hiragana,
	ucScriptKhmer,
	ucScriptLao,
	ucScriptLatin,
	ucScriptLimbu,
	ucScriptLinear_B,
	ucScriptMalayalam,
	ucScriptMongolian,
	ucScriptMyanmar,
	ucScriptOgham,
	ucScriptOld_Italic,
	ucScriptOriya,
	ucScriptOsmanya,
	ucScriptRunic,
	ucScriptShavian,
	ucScriptSinhala,
	ucScriptSyriac,
	ucScriptTagalog,
	ucScriptTagbanwa,
	ucScriptTai_Le,
	ucScriptTamil,
	ucScriptTelugu,
	ucScriptThaana,
	ucScriptThai,
	ucScriptTibetan,
	ucScriptUgaritic,
	ucScriptYi,
	ucScriptNumScripts
};


enum UCProperty {
	ucPropASCII_Hex_Digit 				= 1,
	ucPropBidi_Control				= 1 << 1, 
	ucPropDash					= 1 << 2,
	ucPropDeprecated 				= 1 << 3,
	ucPropDiacritic 				= 1 << 4,
	ucPropExtender 					= 1 << 5,
	ucPropGrapheme_Link 				= 1 << 6,
	ucPropHex_Digit 				= 1 << 7,
	ucPropHyphen 					= 1 << 8,
	ucPropIDS_Binary_Operator 			= 1 << 9,
	ucPropIDS_Trinary_Operator 			= 1 << 10,
	ucPropIdeographic 				= 1 << 11,
	ucPropJoin_Control 				= 1 << 12,
	ucPropLogical_Order_Exception 			= 1 << 13,
	ucPropNoncharacter_Code_Point 			= 1 << 14,
	ucPropOther_Alphabetic 				= 1 << 15,
	ucPropOther_Default_Ignorable_Code_Point 	= 1 << 16,
	ucPropOther_Grapheme_Extend 			= 1 << 17,
	ucPropOther_ID_Start 				= 1 << 18,
	ucPropOther_Lowercase 				= 1 << 19,
	ucPropOther_Math 				= 1 << 20,
	ucPropOther_Uppercase 				= 1 << 21,
	ucPropQuotation_Mark 				= 1 << 22,
	ucPropRadical 					= 1 << 23,
	ucPropSTerm 					= 1 << 24,
	ucPropSoft_Dotted 				= 1 << 25,
	ucPropTerminal_Punctuation 			= 1 << 26,
	ucPropUnified_Ideograph 			= 1 << 27,
	ucPropVariation_Selector 			= 1 << 28,
	ucPropWhite_Space 				= 1 << 29
};

enum ucDerivedCoreProperties {
	ucDCPropAlphabetic 			= 1,
	ucDCPropDefault_Ignorable_Code_Point    = 1 << 1,
	ucDCPropGrapheme_Base                   = 1 << 2,
	ucDCPropGrapheme_Extend                 = 1 << 3,
	ucDCPropID_Continue                     = 1 << 4,
	ucDCPropID_Start                        = 1 << 5,
	ucDCPropLowercase                       = 1 << 6,
	ucDCPropMath                            = 1 << 7,
	ucDCPropUppercase                       = 1 << 8,
	ucDCPropXID_Continue                    = 1 << 9,
	ucDCPropXID_Start                       = 1 << 10
};


enum HangulComposition {
	ucSBase = 0xAC00,
	ucLBase = 0x1100,
	ucVBase = 0x1161,
	ucTBase = 0x11A7,
	ucLCount = 19,
	ucVCount = 21,
	ucTCount = 28,
	ucNCount = ucVCount * ucTCount, // 588
	ucSCount = ucLCount * ucNCount //  11172
};

// Inline Functions here
inline bool ucIsAlpha(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_ALPHA;
}


inline bool ucIsDigit(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_DIGIT;
}
inline bool ucIsAlnum(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_WORDCHAR;
}

inline bool ucIsUpper(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_UPPER;
}

inline bool ucIsLower(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_LOWER;
}

inline bool ucIsWhiteSpace(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_WHITESPACE;	
}

inline bool is_wspace_uc(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_WHITESPACE;	
}

inline bool ucIsIdeograph(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_IDEOGRAPH;
}

inline bool ucIsPunct(UChar32 c) {
	return !ucIsWordChar(c);
}

inline bool is_punct_uc(UChar32 c) {
	return !ucIsWordChar(c);
}

inline bool ucIsIgnorable(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_IGNORABLE;
}
inline bool ucIsExtend(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_EXTEND;
}


inline UChar32 ucToLower(UChar32 c) {
	void *p = g_ucLowerMap.getValue(c);
	if (!p || !*(UChar32*)p) return c;
	return *(UChar32*)p;
}

inline UChar32 ucToUpper(UChar32 c){
	void *p = g_ucUpperMap.getValue(c);
	if (!p || !*(UChar32*)p) return c;
	return *(UChar32*)p;
}

inline unsigned char ucCombiningClass(UChar32 c){
	void *p = g_ucCombiningClass.getValue(c);
	if (!p) return 0;
	return *(UChar32*)p;
}

inline UCProps ucProperties(UChar32 c) {
	void *p = g_ucProps.getValue(c);
	if (!p) return (UCProps)0;
	return *(UCProps*)p;
}

inline int32_t ucDigitValue(UChar32 c) {
	if (c >= '0' && c <= '9') return (int32_t)(c-'0');
	return 0;
}

inline UCScript ucGetScript(UChar32 c) {
	void *p = g_ucScripts.getValue(c);
	if (!p) return ucScriptCommon;
	return *(UCScript*)p;
}

#endif
