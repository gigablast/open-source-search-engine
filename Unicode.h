#ifndef UNICODEH
#define UNICODEH

#include <sys/types.h>
#include <limits.h>
#include <stdio.h>
#include "UnicodeProperties.h"
#include "iconv.h"
//#include "UCNormalizer.h"

//U_CFUNC uint32_t
//u_getUnicodeProperties(UChar32 c, int32_t column);
//#define USE_ICU 
// Initialize unicode word parser
bool 	ucInit(char *path = NULL, bool verifyFiles = false);

//////////////////////////////////////////////////////
// Converters
iconv_t gbiconv_open(char *tocode, char *fromcode) ;
int gbiconv_close(iconv_t cd) ;

// Convert to Unicode (UTF-16) from the specified charset
// set normalized to find out if the buffer is NFKC-normalized
//int32_t 	ucToUnicode(UChar *outbuf, int32_t outbuflen, 
//		    char *inbuf, int32_t inbuflen, 
//		    char *charset, int32_t ignoreBadChars,
//		    int32_t titleRecVersion );

int32_t 	ucToAny(char *outbuf, int32_t outbuflen, char *charset_out,
		 char *inbuf, int32_t inbuflen, char *charset_in,
		 int32_t ignoreBadChars,int32_t niceness);

// table for decoding utf8...says how many bytes in the character
// based on value of first byte.  0 is an illegal value
static int bytes_in_utf8_code[] = {
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,

	// next two rows are all illegal, so return 1 byte
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,

	// many for loop add this many bytes to iterate, so since the last
	// 8 entries in this table are invalid, assume 1, not 0
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,1,1,1,1,1,1,1,1
};

static int utf8_sane[] = {
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,

	// next two rows are all illegal, so return 1 byte
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,

	// many for loop add this many bytes to iterate, so since the last
	// 8 entries in this table are invalid, assume 1, not 0
	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0
};

// how many bytes is char pointed to by p?
inline char getUtf8CharSize ( uint8_t *p ) {
	uint8_t c = *p;
	if(c<128)
		return 1;
	else
		return bytes_in_utf8_code[c];
}

inline char getUtf8CharSize ( char *p ) {
	uint8_t c = (uint8_t)*p;
	if(c<128)
		return 1;
	else
		return bytes_in_utf8_code[c];
}

inline char getUtf8CharSize ( uint8_t c ) {
	if(c<128)
		return 1;
	else
		return bytes_in_utf8_code[c];
}

inline char getUtf8CharSize2 ( uint8_t *p ) {
        if ( ! (p[0] & 0x80) ) return 1;
	if ( ! (p[0] & 0x20) ) return 2;
	if ( ! (p[0] & 0x10) ) return 3;
	if ( ! (p[0] & 0x08) ) return 4;
	// crazy!!!
	return 1;
}

inline char isSaneUtf8Char ( uint8_t *p ) {
	return utf8_sane[p[0]];
}


// utf8 bytes. up to 4 bytes in a char:
// 0xxxxxxx
// 110yyyxx 10xxxxxx
// 1110yyyy 10yyyyxx 10xxxxxx
// 11110zzz 10zzyyyy 10yyyyxx 10xxxxxx
// TODO: make a table for this as well
inline char isFirstUtf8Char ( char *p ) {
	// non-first chars have the top bit set and next bit unset
	if ( (p[0] & 0xc0) == 0x80 ) return false;
	// we are the first char in a sequence
	return true;
}

// point to the utf8 char BEFORE "p"
inline char *getPrevUtf8Char ( char *p , char *start ) {
	for ( p-- ; p >= start ; p-- )
		if ( isFirstUtf8Char(p) ) return p;
	return NULL;
}

inline int32_t ucToUtf8(char *outbuf, int32_t outbuflen, 
			 char *inbuf, int32_t inbuflen, 
			 char *charset, int32_t ignoreBadChars,
		     int32_t niceness) {
  return ucToAny(outbuf, outbuflen, (char *)"UTF-8",
		 inbuf, inbuflen, charset, ignoreBadChars,niceness);
}

/* int32_t 	ucToUnicode_iconv(UChar *outbuf, int32_t outbuflen,  */
/* 		    char *inbuf, int32_t inbuflen,  */
/* 		    char *charset, bool *normalized = NULL); */

// Convert to a specified charset from Unicode (UTF-16)
//int32_t 	ucFromUnicode(char *outbuf, int32_t outbuflen, 
//		      const UChar *inbuf, int32_t inbuflen, 
//		      char *charset);

// Normalize a UTF16 text buffer in Kompatibility Form
//int32_t 	ucNormalizeNFKC(UChar *outbuf, int32_t outbufLen,
//			const UChar *inbuf, int32_t inbufLen);

// JAB: Normalize a UTF-8 text buffer in Canonical Form
//int32_t utf8CDecompose(	char*       outBuf, int32_t outBufSize,
//			char* inBuf,  int32_t inBufSize,
//			bool decodeEntities);

// Encode a code point in UTF-16
//int32_t 	utf16Encode(UChar32 c, UChar *buf);

// Encode a code point into latin-1, return 0 if not able to
uint8_t latin1Encode ( UChar32 c ) ;
// Encode a code point in UTF-8
int32_t	utf8Encode(UChar32 c, char* buf);

// Try to detect the Byte Order Mark of a Unicode Document
char *	ucDetectBOM(char *buf, int32_t bufsize);
//UChar32 utf16Decode(UChar *s, UChar **next, int32_t maxLen=LONG_MAX);
//UChar32 utf16EntityDecode(UChar *s, UChar **next, int32_t maxLen = LONG_MAX);
//int32_t utf16Size(UChar32 c) ;

// . get the size of a utf16 character pointed to by "s"
// . it will be either 1, 2 or 4 bytes
//char usize ( char *s );

// Special case converter...for web page output
int32_t latin1ToUtf8(char *outbuf, int32_t outbufsize,
		  char *inbuf, int32_t inbuflen);

//int32_t utf8ToAscii(char *outbuf, int32_t outbufsize,
//		  unsigned char *inbuf, int32_t inbuflen);
int32_t stripAccentMarks(char *outbuf, int32_t outbufsize,
		      unsigned char *inbuf, int32_t inbuflen);

//int32_t utf16ToUtf8(char* outbuf, int32_t outbufSize, 
//		 UChar *s, int32_t slen);
//int32_t utf16ToUtf8(char* outbuf, int32_t outbufSize, 
//		 char *s, int32_t slen);
//int32_t utf16ToUtf8_iconv(char *outbuf, int32_t outbufsize, 
//		       UChar *inbuf, int32_t inbuflen);
//int32_t utf16ToUtf8_intern(char *outbuf, int32_t outbufsize, 
//			UChar *inbuf, int32_t inbuflen);
//int32_t utf16ToLatin1(char* outbuf, int32_t outbufSize, 
//		   UChar *s, int32_t slen);
//int32_t utf16ToLatin1(char* outbuf, int32_t outbufSize, 
//		   char *s, int32_t slen);
//int32_t utf16EncodeLatinStr(char *outbuf, int32_t outbufLen, 
//			 char *inbuf, char inbufLen);

// Utility functions
// Print a code point (in ASCII)
//void 	ucPutc(UChar32 c);

// Is this character part of a "word"?
//bool 	ucIsWordChar(UChar32 c);
//int32_t 	ucAtoL(UChar* buf, int32_t len);
//int32_t 	ucTrimWhitespaceInplace(UChar*buf, int32_t bufLen);
//int32_t 	ucTrimWhitespaceInplace(char *buf, int32_t bufLen);


//bool utf16IsTrail(UChar c);
//UChar32 utf16Prev(UChar *s, UChar **prev); 
//int32_t ucStrCaseCmp(UChar *s1, int32_t slen1, UChar *s2, int32_t slen2);
//int32_t ucStrCaseCmp(UChar *s1, int32_t slen1, char *s2, int32_t slen2);
//int32_t ucStrCmp(UChar *s1, int32_t slen1, UChar*s2, int32_t slen2) ;
//int32_t ucStrNLen(UChar *s, int32_t maxLen) ;
//int32_t ucStrNLen(char *s, int32_t maxLen) ;

// . determine needed size and convert utf16 to utf8 in 
//   buffer or allocate space if no buffer
// . returns NULL if error occurs
//char *utf16ToUtf8Alloc( char *utf16Str, int32_t utf16StrLen,
//			char *buf, int32_t *bufSize );



//////////////////////////////////////////////////////////
// Debugging/Testing
//////////////////////////////////////////////////////////
// encode a UChar* string into ascii (for debugging, mostly)
// slen in UChars
//int32_t 	ucToAscii(char *buf, int32_t bufsize, UChar *s, int32_t slen);
// slen in bytes
//int32_t 	ucToAscii(char *buf, int32_t bufsize, char *s, int32_t slen);
//int32_t 	ucAnyToAscii(char *buf, int32_t bufsize,
//			char *s, int32_t slen, char* charset);
//void 	uccDebug(UChar *s, int32_t slen);

//#define CrashMe() {					     
//		log("Unicode: Need to update %s, line %d!!!\n", 
//		    __FILE__, __LINE__);		     
//		char *xx = NULL; *xx = 0;                    
//	}


// parse a buffer encoded in utf8 format
// Don't use these
// JAB: unused
//#if 0
//int utf8_parse_buf(char *s);
//#endif
//int32_t utf8_count_words ( char *p ) ;
//bool utf8_is_alnum(UChar32 c);


//BreakIterator *ucGetWordIterator();//
//void ucReleaseWordIterator(BreakIterator* it);


//////////////////////////////////////////////////////////////
//  Inline functions
//////////////////////////////////////////////////////////////

//inline 
//int32_t 	ucTrimWhitespaceInplace(char *buf, int32_t bufLen) { 
//	return ucTrimWhitespaceInplace((UChar *)buf, bufLen >> 1 ) << 1;
//};

// Print a code point (in ASCII)
//inline void ucPutc(UChar32 c)
//{
//	if (c < 0x80){
//		fputc(c, stdout);
//	}
//	else{
//		printf("[U+%04X]", (unsigned int)c);
//	}
//}



// Words can start with these chars
// (as opposed to punct words)
// TODO:  optimize (precompile) this function

// inline bool ucIsWordChar(UChar32 c) {
// 	bool val = ((U_MASK(u_charType(c))&
// 		(U_GC_N_MASK|
// 		 U_GC_L_MASK|
// 		 U_GC_MC_MASK))
// 		!=0 );
// 	return val;
// }


//inline int32_t utf8EncodeStr(char *outbuf, int32_t outbufsize, 
//		   UChar *inbuf, int32_t inbuflen)
//{
//	return utf8EncodeStr(outbuf, outbufsize, (char*)inbuf, inbuflen<<1);
//}

/*
inline int32_t utf16ToUtf8(char* outbuf, int32_t outbufSize, 
			char *s, int32_t slen) {
	return utf16ToUtf8(outbuf, outbufSize,
			   (UChar*)s, slen >> 1);
}
inline int32_t utf16ToLatin1(char* outbuf, int32_t outbufSize, 
			char *s, int32_t slen) {
	return utf16ToLatin1(outbuf, outbufSize,
			     (UChar*)s, slen >> 1);
}

inline // returns length of UChar sequence encoded
int32_t utf16Encode(UChar32 c, UChar *buf){
	// if character fits into 1 code unit
	// AND it's not an invalid char that makes it look like the
	// first half of a 2 unit char, just copy it in
	if (!(c & 0xffff0000L)){
		if (( c & 0xfffffc00  ) != 0xd800)
			buf[0] = c;
		else    buf[0] = 0xffff; //invalid character
		return 1;
	}
        buf[0] = (UChar)(((c)>>10)+0xd7c0); 
        buf[1] = (UChar)(((c)&0x3ff)|0xdc00);	
	return 2;
}

// special case conversion...quickly convert latin1 to utf16 in a char* buffer
// return # bytes written
inline
int32_t utf16EncodeLatinStr(char *outbuf, int32_t outbufLen, 
			char *inbuf, char inbufLen){
	int32_t j = 0;
	for (int32_t i = 0 ; i<inbufLen && j < outbufLen; i++) {
		j += utf16Encode((UChar32)(unsigned char)inbuf[i], 
				 ((UChar*)(outbuf))+j);
	}
	return j<<1;
}
*/

// . convert a unicode char into latin1
// . returns 0 if could not do it
// . see UNIDATA/NamesList.txt for explanation of all UChar32 values
// . seems like Unicode is conventiently 1-1 with latin1 for the first 256 vals
inline uint8_t latin1Encode ( UChar32 c ) {
	// keep ascii chars as ascii
	if ( c <= 255 ) return (uint8_t)c;
	// that ain't latin-1!
	return 0;
}

// . returns length of byte sequence encoded
// . store the unicode character, "c", as a utf8 character
// . return how many bytes were stored into "buf"
inline int32_t utf8Encode(UChar32 c, char* buf) {
	if (!(c & 0xffffff80)){  
		// 1 byte
		buf[0] = (char)c;
		return 1;
	}
	if (!(c & 0xfffff800)){ 
		// 2 byte
		buf[0] = (char)(0xc0 | (c >> 6 & 0x1f));
		buf[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if (!(c & 0xffff0000)){ 
		// 3 byte
		buf[0] = (char)(0xe0 | (c >> 12 & 0x0f));
		buf[1] = (char)(0x80 | (c >> 6 & 0x3f));
		buf[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	if (!(c & 0xe0)){ 
		// 4 byte
		buf[0] = (char)(0xf0 | (c >> 18 & 0x07));//5
		buf[1] = (char)(0x80 | (c >> 12 & 0x3f));//5
		buf[2] = (char)(0x80 | (c >> 6 & 0x3f));//5
		buf[3] = (char)(0x80 | (c & 0x3f));//4
		return 4;
	}
	// illegal character
	return 0;
}

// return the utf8 character at "p" as a 32-bit unicode character
inline UChar32 utf8Decode(char *p){//, char **next){
	// single byte character
	if (!(*p & 0x80)){
		//*next = (char*) p + 1;
		return (UChar32)*p;
	}
	// 2 bytes
	else if (!(*p & 0x20)){
		//*next = (char*) p + 2;
		return (UChar32)((*p & 0x1f)<<6 | 
				(*(p+1) & 0x3f));
	}
	// 3 bytes
	else if (!(*p & 0x10)){
		//*next = (char*) p + 3;
		return (UChar32)((*p & 0x0f)<<12 | 
				(*(p+1) & 0x3f)<<6 |
				(*(p+2) & 0x3f));
	}
	// 4 bytes
	else if (!(*p & 0x08)){
		//*next = (char*) p + 4;
		return (UChar32)((*p & 0x07)<<18 | 
				(*(p+1) & 0x3f)<<12 |
				(*(p+2) & 0x3f)<<6 |
				(*(p+3) & 0x3f));
	}
	// invalid
	else{
		//*next = (char*) p + 1;
		return (UChar32)-1;
	}
}


//can't include Entities.h here...weird dependencies
// JAB: const-ness for the optimizer...
//extern int32_t getEntity(char *s, int32_t maxLen, uint32_t *c,
//			bool doUnicode);
// JAB: const-ness for the optimizer
//inline UChar32 utf8EntityDecode(char *s, char **next,
//					int32_t maxLen) {
//	UChar32 c = utf8Decode(s, (char**) next);
//	if (c != '&')
//		return c;
//	UChar32 entity;
//	int32_t skip = getEntity(s, maxLen, &entity, true /*doUnicode*/);
//	if (skip) {
//		*next = s+skip;
//		return entity;
//	}
//	return c;
//}

////////////////////////////////////////////////////
/*
inline UChar32 utf16Decode(UChar *s, UChar **next, int32_t maxLen){
	UChar32 ret = s[0];
	*next = s+1; // 99% of common chars are in BMP (16 bit)
	if ( ( ret & 0xfffffc00  ) != 0xd800 ) { //is this a 2 unit code point?
		return ret;
	}

	if ((ret & 0x400) == 0){//surrogate lead
		ret = (ret<<10)+s[1] -((0xd800<<10UL)+0xdc00-0x10000);
		(*next)++;
	}
	else // surrogate trail
		ret = (s[1]<<10)+ret -((0xd800<<10UL)+0xdc00-0x10000);
	return ret;
}

// returns the number of int16_ts required to encode character c in UTF-16
inline int32_t utf16Size(UChar32 c){
	if (!(c & 0xffff0000L))
		return 1;
	return 2;
}
*/

// JAB: returns the number of bytes required to encode character c in UTF-8
inline int32_t utf8Size(UChar32 c){
  if ((c & 0xFFFFFF80) == 0) return 1;
  if ((c & 0xFFFFF800) == 0) return 2;
  if ((c & 0xFFFF0000) == 0) return 3;
  if ((c & 0xFFE00000) == 0) return 4;
  if ((c & 0xFC000000) == 0) return 5;
	return 6;
}

/*
inline bool utf16IsTrail(UChar c) {
	return ( c & 0xfc00 ) == 0xdc00;
}

inline UChar32 utf16Prev(UChar *s, UChar **prev) {
	*prev = s-1;
	if (utf16IsTrail(**prev)) 
	{
		(*prev)--; 
		return ((UChar32)*(s-2)<<10UL)+(UChar32)*(s-1) 
			- ((0xd800<<10UL)+0xdc00-0x10000);
	}
	return (UChar32)(**prev);
}
*/

// JAB: find the first byte of the previous UTF-8 character
inline UChar32 utf8Prev(char* cur, char** prev) {
	cur--;
	while (((*cur) & 0xC0) == 0x80)
		cur--;
	*prev = cur;
	//char* next;
	return utf8Decode(cur);//, &next);
}

//inline int32_t ucStrNLen(char *s, int32_t maxLen) {
//	return ucStrNLen((UChar*)s, maxLen>>1) << 1;
//}

//can't include Entities.h here...weird dependencies
// JAB: const-ness for the optimizer...
//extern int32_t getEntity(const UChar*s, int32_t maxLen, uint32_t *c);
// JAB: const-ness for the optimizer...
/*
inline UChar32 utf16EntityDecode(UChar *s, UChar **next, int32_t maxLen) {
	UChar32 c = utf16Decode(s, next);
	if (c == '&'){
		UChar32 entity;
		int32_t skip = getEntity(s, maxLen, &entity);
		if (skip){
			*next = s+skip;
			return entity;
		}
	}
	return c;
}
*/

//can't include Entities.h here...weird dependencies
// JAB: const-ness for the optimizer...
//extern int32_t getEntity(char *s, int32_t maxLen, uint32_t *c,
//			bool doUnicode);

// JAB: const-ness for the optimizer
//inline UChar32 utf8EntityDecode(char *s, char **next,int32_t maxLen) {
//	UChar32 c = utf8Decode(s, next);
//	if (c == '&') {
//		UChar32 entity;
//		int32_t skip = getEntity(s, maxLen, &entity, true /*doUnicode*/);
//		if (skip) {
//			*next = (char*) s+skip;
//			return entity;
//		}
//	}
//	return c;
//}

inline UChar32 fixWindows1252(UChar32 c){
	if ( c < 130 || c > 159 ) return c;
	switch (c){
	case 130: c = 0x201a; break;
	case 131: c = 0x0192; break;
	case 132: c = 0x201e; break;
	case 133: c = 0x2026; break;
	case 134: c = 0x2020; break;
	case 136: c = 0x2021; break;
	case 137: c = 0x2030; break;
	case 138: c = 0x0160; break;
	case 139: c = 0x2039; break;
	case 140: c = 0x0152; break;
	case 145: c = 0x2018; break;
	case 146: c = 0x2019; break;
	case 147: c = 0x201c; break;
	case 148: c = 0x201d; break;
	case 149: c = 0x2022; break;
	case 150: c = 0x2013; break;
	case 151: c = 0x2014; break;
	case 152: c = 0x02dc; break;
	case 153: c = 0x2122; break;
	case 154: c = 0x0161; break;
	case 155: c = 0x203a; break;
	case 156: c = 0x0153; break;
	case 159: c = 0x0178; break;
	}
	return c;
}

/*
// look for an ascii substring in a utf-16 string
UChar *ucStrNCaseStr(UChar *haystack, int32_t haylen, char *needle); 
UChar *ucStrNCaseStr(UChar *haystack, int32_t haylen, char *needle, 
		     int32_t needleLen);
// look for a utf-16 substring in a utf-16 string
UChar *ucStrNCaseStr(UChar *haystack, int32_t haylen,
		     UChar *needle, int32_t needleLen); 
// look for a unicode substring in an ascii string
char *ucStrNCaseStr(char *haystack,
		    UChar *needle, int32_t needleLen);
char *ucStrNCaseStr(char *haystack, int32_t haylen,
		    UChar *needle, int32_t needleLen);
*/
#endif
