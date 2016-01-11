// Matt Wells, copyright Jun 2001

#ifndef _FCTYPES_H_
#define _FCTYPES_H_

#include <sys/time.h>  // gettimeofday()
#include <math.h>      // floor()
#include "Unicode.h"

extern bool g_clockNeedsUpdate;

// we have to leave this as 32 bits for now because the termlists store
// the hash value as 32 bits in posdb
typedef uint32_t FacetValHash_t;

bool verifyUtf8 ( char *txt ) ;
bool verifyUtf8 ( char *txt , int32_t tlen ) ;

bool print96  ( char     *k  ) ;
bool print96  ( key_t    *kp ) ;
bool print128 ( char     *k  ) ;
bool print128 ( key128_t *kp ) ;


// print it to stdout for debugging Dates.cpp
int32_t printTime ( int32_t ttt );
time_t mktime_utc ( struct tm *ttt ) ;

class SafeBuf;
// seems like this should be defined, but it isn't
int32_t strnlen ( const char *s , int32_t maxLen );
// this too
char *strncasestr( char *haystack, int32_t haylen, char *needle);

// this is also done below
char *strnstr2( char *haystack, int32_t haylen, char *needle);

// just like sprintf(s,"%"UINT64"",n), but we insert commas
int32_t ulltoa ( char *s , uint64_t n ) ;

// . convert < to &lt; and > to &gt and & to &amp;
// . store "t" into "s"
// . returns bytes stored into "s"
// . NULL terminates "s"
int32_t saftenTags ( char *s , int32_t slen , char *t , int32_t tlen ) ;

// . basically just converts "'s to &#34;'s
// . store "src" into "dest" and return bytes stored
// . does not do bounds checking in "dest"
// . used to encode things as form input variables, like query in HttpPage0.cpp
int32_t dequote       ( char *dest , char *dend , char *src , int32_t srcLen ) ;

// . entity-ize a string so it's safe for html output
// . converts "'s to &#34;'s, &'s to &amps; <'s the &lt; and >'s to &gt;
// . store "src" into "dest" and return bytes stored
// . does not do bounds checking on "dest"
// . encode t into s
char *htmlEncode ( char *s , char *send , char *t , char *tend ,
		   bool pound = false , int32_t niceness = 0) ;
bool htmlEncode ( SafeBuf* s , char *t , char *tend , 
		  bool pound = false , int32_t niceness = 0 );

// . like above but src is NULL terminated
// . returns length of string stored into "dest"
// . decode html entities like &amp; and &gt;
int32_t htmlDecode    ( char *dst, char *src, int32_t srcLen, 
		     bool doSpecial ,//=false);
		     int32_t niceness);

int32_t cdataDecode ( char *dst , char *src , int32_t niceness ) ;

// . convert " to %22 , & to %26, is that it?
// . urlEncode() stores the encoded, NULL-terminated URL in "dest"
// . requestPath leaves \0 and ? characters intact, for encoding requests
int32_t urlEncode     ( char *dest , int32_t destLen , char *src , int32_t srcLen ,
		     bool  requestPath = false ) ;
// determine the length of the encoded url, does NOT include NULL
int32_t urlEncodeLen  ( char *s , int32_t slen , bool requestPath = false ) ;
// decode a url -- decode ALL %XX's
int32_t urlDecode ( char *dest , char *t , int32_t tlen ) ;
int32_t urlDecodeNoZeroes ( char *dest , char *t , int32_t tlen ) ;
// . normalize the encoding
// . like urlDecode() but only decodes chars that should not have been encoded
// . also, will encode characters that should have been encoded
int32_t urlNormCode ( char *dest , int32_t destLen , char *src , int32_t srcLen ) ;

bool is_digit(unsigned char c) ;

// is character, "s", used in textual hexadecimal representation?
bool is_hex ( char s ) ;
bool is_urlchar(char s);

// convert hex digit to value
int32_t htob ( char s ) ;
char btoh ( char s ) ;
// convert hex ascii string into binary
void hexToBin ( char *src , int32_t srcLen , char *dst );
// convert binary number of size srcLen bytes into hex string in "dst"
void binToHex ( unsigned char *src , int32_t srcLen , char *dst );

// the _a suffix denotes an ascii string
bool is_lower2_a  (char *s,int32_t len) ;
bool is_lower1_a  (char *s) ;
bool is_ascii2    (char *s,int32_t len) ;
bool is_alnum2_a  (char *s,int32_t len) ;
bool has_alpha_a  (char *s , char *send ) ;
bool has_alpha_utf8(char *s, char *send ) ;
bool is_alpha2_a  (char *s,int32_t len) ;
bool is_num       (char *s,int32_t len) ;
bool is_upper2_a  (char *s,int32_t len) ;
bool is_cap_a     (char *s,int32_t len) ;
bool is_cap_utf8  (char *s,int32_t len) ;

bool is_vowel_a ( char s );
bool has_vowel_a ( char *s , int32_t slen );

// does it have at least one upper case character in it?
bool has_upper_a  (char *s,int32_t len) ;
bool has_binary_a (char *s,int32_t len) ;
void to_lower3_a  (char *s,int32_t len, char *buf) ;

void to_lower1            (char *s) ;
int32_t to_lower_alnum       (char *s,int32_t len, char *buf) ;
int32_t to_lower_utf8        (char *dst , char *src ) ;
int32_t to_lower_utf8        (char *dst , char *dstEnd, char *src ) ;
int32_t to_lower_utf8        (char *dst , char *dstEnd, char *src, char *srcEnd) ;
int32_t to_lower_utf8_32     (char *src ) ;
int32_t to_cap_alnum         (char *s,int32_t len, char *buf) ;
int32_t to_alnum             (char *s,int32_t len, char *buf) ;
void to_upper3_a          (char *s,int32_t len, char *buf) ;
void to_cap               (char *s,int32_t len, char *buf) ;

// . approximate # of non-punct words
// . s must be NULL terminated
// . used by LinkInfo.cpp to weight link text based on # of words
int32_t getNumWords ( char *s );

// true if character should be stripped from the end/beginning of the title
// error! make a map of this
bool is_title_junk(char c) ;

// . get the # of words in this string
int32_t      getNumWords ( char *s , int32_t len, int32_t titleVersion ) ;
int32_t      atol2       ( const char *s, int32_t len ) ;
int64_t atoll1      ( const char *s ) ;
int64_t atoll2      ( const char *s, int32_t len ) ;
double    atof2       ( const char *s, int32_t len ) ;
double    atod2       (       char *s, int32_t len ) ;
bool      atob        ( const char *s, int32_t len ) ;

// like strstr but haystack need not be NULL terminated
char *strncasestr ( char *haystack , char *needle , int32_t haystackSize ) ;
char *strncasestr ( char *haystack , char *needle , 
		    int32_t haystackSize, int32_t needleSize ) ;
char *strnstr ( char *haystack , char *needle , int32_t haystackSize ) ;

// independent of case
char *gb_strcasestr ( char *haystack , char *needle );
char *gb_strncasestr ( char *haystack , int32_t haystackSize , char *needle ) ;

// updates our static var, s_adjustment to keep our clock in sync to hostId #0
void settimeofdayInMillisecondsGlobal ( int64_t newTime ) ;

// convert global to local time in milliseconds
int64_t globalToLocalTimeMilliseconds ( int64_t global ) ;
int64_t localToGlobalTimeMilliseconds ( int64_t local  ) ;
// the same thing but in seconds
int32_t      globalToLocalTimeSeconds      ( int32_t      global ) ;
int32_t      localToGlobalTimeSeconds      ( int32_t      local  ) ;

// we now default this to local time to avoid jumpiness associated with
// having to sync with host #0. most routines calling this usually are just
// taking deltas. 
int64_t gettimeofdayInMillisecondsGlobal() ; // synced with host #0
int64_t gettimeofdayInMillisecondsGlobalNoCore() ; // synced with host #0
int64_t gettimeofdayInMillisecondsSynced() ; // synced with host #0
int64_t gettimeofdayInMillisecondsLocal () ;// this is local now
int64_t gettimeofdayInMilliseconds() ;// this is local now
uint64_t gettimeofdayInMicroseconds(void) ;
int64_t gettimeofdayInMilliseconds_force ( ) ;

// . get time in seconds since epoch
// . use this instead of call to time(NULL) cuz it uses adjustment
time_t getTime       ();  // this is local now
time_t getTimeLocal  (); 
time_t getTimeGlobal (); // synced with host #0's system clock
time_t getTimeGlobalNoCore (); // synced with host #0's system clock
time_t getTimeSynced (); // synced with host #0's system clock

int32_t stripHtml( char *content, int32_t contentLen, int32_t version, int32_t strip );

extern const char g_map_is_vowel[];
extern const unsigned char g_map_to_lower[];
extern const unsigned char g_map_to_upper[];
extern const unsigned char g_map_to_ascii[];
extern const char g_map_is_upper[];
extern const char g_map_canBeInTagName[];
extern const char g_map_is_control[];
extern const char g_map_is_binary[];
extern const char g_map_is_wspace[];
extern const char g_map_is_vspace[];
extern const char g_map_is_hspace[];
extern const char g_map_is_lower[];
extern const char g_map_is_ascii[];
extern const char g_map_is_ascii3[];
extern const char g_map_is_iso[];
extern const char g_map_is_punct[];
extern const char g_map_is_alnum[];
extern const char g_map_is_alpha[];
extern const char g_map_is_digit[];
extern const char g_map_is_hex[];
extern const char g_map_is_tagname_char[];
extern const char g_map_is_tag_control_char[];

//extern bool      g_clockInSync;
extern int64_t g_adjustment;

bool isClockInSync();

bool setTimeAdjustmentFilename ( char *dir, char *filename ) ;
bool loadTimeAdjustment ( ) ;
bool saveTimeAdjustment ( ) ;

// . convert "c" to lower case
#define is_vowel_a(c)          g_map_is_vowel[(unsigned char)c]
#define is_lower_a(c)          g_map_is_lower[(unsigned char)c]
#define to_lower_a(c)          g_map_to_lower[(unsigned char)c]
#define is_upper_a(c)          g_map_is_upper[(unsigned char)c]
#define to_upper_a(c)          g_map_to_upper[(unsigned char)c]
// c is latin1 in this case:
#define to_ascii(c)            g_map_to_ascii[(unsigned char)c]
#define canBeInTagName(c)      g_map_canBeInTagName[(unsigned char)c]
#define is_control_a(c)        g_map_is_control[(unsigned char)c]
#define is_binary_a(c)         g_map_is_binary[(unsigned char)c]
#define is_wspace_a(c)         g_map_is_wspace[(unsigned char)c]
#define is_vspace_a(c)         g_map_is_vspace[(unsigned char)c]
#define is_hspace_a(c)         g_map_is_hspace[(unsigned char)c]
#define is_ascii(c)           g_map_is_ascii[(unsigned char)c]
#define is_ascii9(c)           g_map_is_ascii[(unsigned char)c]
#define is_ascii3(c)           ((unsigned char)c<128 || g_map_is_ascii3[(unsigned char)c])
#define is_punct_a(c)          g_map_is_punct[(unsigned char)c]
#define is_alnum_a(c)          g_map_is_alnum[(unsigned char)c]
#define is_alpha_a(c)          g_map_is_alpha[(unsigned char)c]
#define is_digit(c)            g_map_is_digit[(unsigned char)c]
#define is_hex(c)              g_map_is_hex[(unsigned char)c]
#define is_tagname_char(c)     g_map_is_tagname_char[(unsigned char)c]
#define is_tag_control_char(c) g_map_is_tag_control_char[(unsigned char)c]
#define is_matchskip_a(c)      g_map_is_matchskip[(unsigned char)c]

inline bool is_upper_utf8 ( char *s );

inline bool has_vowel_a ( char *s , int32_t slen ) {
	char *send = s + slen;
	for ( ; s < send ; s++ )
		if ( is_vowel_a(*s) ) return true;
	return false;
};

/*
// is character, "s", used in textual hexadecimal representation?
inline bool is_hex ( char s ) {
	if ( is_digit(s)) return true;
	if ( s >= 'a'  && s <= 'f' ) return true;
	if ( s >= 'A'  && s <= 'F' ) return true;
	return false;
}
*/

// convert hex digit to value
inline int32_t htob ( char s ) {
	if ( is_digit(s) ) return s - '0';
	if ( s >= 'a'  && s <= 'f' ) return (s - 'a') + 10;
	if ( s >= 'A'  && s <= 'F' ) return (s - 'A') + 10;
	return 0;
}

inline char btoh ( char s ) {
	if ( s >= 16 ) { char *xx=NULL;*xx=0; }
	if ( s < 10 ) return s + '0';
	return (s - 10) + 'a';
}

// have to put an extra "s" on function name to avoid macro conflict
inline bool is_lower_as(char *s,int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_lower_a(s[i]))
			return false;
	return true;
}

// have to put an extra "s" on function name to avoid macro conflict
inline bool is_lower_as(char *s) {
	for (int32_t i=0;s[i];i++)
		if (!is_lower_a(s[i]))
			return false;
	return true;
}

inline bool is_ascii2_a(char *s,int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_ascii(s[i]))
			return false;
	return true;
}

inline bool is_alnum2_a(char *s,int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_alnum_a(s[i]))
			return false;
	return true;
}

inline bool is_alpha2_a(char *s,int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_alpha_a(s[i]))
			return false;
	return true;
}

inline bool is_num(char *s,int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_digit(s[i]))
			return false;
	return true;
}

inline bool is_upper2_a (char *s,int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_upper_a(s[i]))
			return false;
	return true;
}

inline bool is_cap_a (char *s,int32_t len) {
	if (!is_upper_a(s[0]))
		return false;
	for (int32_t i=1;i<len;i++)
		if (!is_lower_a(s[i]))
			return false;
	return true;
}

inline bool is_cap_utf8 (char *s,int32_t len) {
	if ( ! is_upper_utf8 ( s ) ) return false;
	char *send = s + len;
	for ( ; s < send ; s += getUtf8CharSize ( s ) ) 
		if ( is_upper_utf8 ( s ) ) return false;
	return true;
}

// does it have at least one upper case character in it?
inline bool has_upper_a (char *s,int32_t len) {
		for (int32_t i=0;i<len;i++) 
			if ( is_upper_a(s[i])) 
				return true;
	return false;
}

// does it have at least one binary character in it?
inline bool has_binary_a (char *s,int32_t len) {
	for (int32_t i=0;i<len;i++) 
		if ( is_binary_a(s[i])) 
			return true;
	return false;
}

inline void to_lower3_a(char *s,int32_t len, char *buf) {
	for (int32_t i=0;i<len ;i++)
		buf[i]=to_lower_a((unsigned char)s[i]);
}

inline void to_lower1_a(char *s) {
	for (int32_t i=0;s[i] ;i++)
		s[i]=to_lower_a((unsigned char)s[i]);
}

inline int32_t to_lower_alnum_a(char *s,int32_t len, char *buf) {
	int32_t j=0;
	for (int32_t i=0;i<len ;i++)
		if (is_alnum_a(s[i]))
			buf[j++]=to_lower_a((unsigned char)s[i]);
	return j;
}

inline int32_t to_cap_alnum_a(char *s,int32_t len, char *buf) {
	buf[0] = to_upper_a(s[0]);
	int32_t j=1;
	for (int32_t i=1;i<len ;i++)
		if (is_alnum_a(s[i]))
			buf[j++]=to_lower_a((unsigned char)s[i]);
	return j;
}

inline int32_t to_alnum_a(char *s,int32_t len, char *buf) {
	int32_t j=0;
	for (int32_t i=0;i<len ;i++)
		if (is_alnum_a(s[i]))
			buf[j++]=s[i];
	return j;
}

inline void to_upper3_a(char *s,int32_t len, char *buf) {
	for (int32_t i=0;i<len;i++)
		buf[i]=to_upper_a(s[i]);
}

inline void to_cap_a(char *s,int32_t len, char *buf) {
	buf[0]=to_upper_a(s[0]);
	for (int32_t i=1;i<len;i++)
		buf[i]=to_lower_a(s[i]);
}

inline bool is_binary_utf8 ( char *p ) {
	if ( getUtf8CharSize((uint8_t *)p) != 1 ) return false;
	// it is ascii, use that table now
	return is_binary_a ( *p );
}

inline bool is_lower_utf8 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_lower_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	return ucIsLower ( x );
}

inline bool is_upper_utf8 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_upper_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint upper?
	return ucIsUpper ( x );
}

inline bool is_alnum_utf8 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_alnum_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	return ucIsAlnum ( x );
}

inline bool is_alnum_utf8 ( unsigned char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_alnum_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode((char *)src);
	// is this codepoint lower?
	return ucIsAlnum ( x );
}

inline bool is_alpha_utf8 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_alpha_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	return ucIsAlpha ( x );
}

inline bool is_punct_utf8 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_punct_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	if ( ucIsAlnum ( x ) ) return false;
	else                   return true;
}

inline bool is_wspace_utf8 ( uint8_t *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_wspace_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode((char *)src);
	// is this codepoint a whitespace?
	return is_wspace_uc ( x );
}

inline bool is_wspace_utf8 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3((uint8_t)*src) ) return is_wspace_a ( (uint8_t)*src );
	// convert to a code point
	UChar32 x = utf8Decode((char *)src);
	// is this codepoint a whitespace?
	return is_wspace_uc ( x );
}

// . returns bytes stored into "dst" from "src"
// . just do one character, which may be from 1 to 4 bytes
// . TODO: make a native utf8 to_lower to avoid converting to a code point
inline int32_t to_lower_utf8 ( char *dst , char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) { *dst = to_lower_a ( *src ); return 1; }
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToLower ( x );
	// put it back to utf8. return bytes stored.
	return utf8Encode ( y , dst );
}

// store answer in the int32_t and return that!
inline int32_t to_lower_utf8_32 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return (int32_t) to_lower_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToLower ( x );
	// give that back
	return y;
}

inline int32_t to_upper_utf8 ( char *dst , char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) { *dst = to_upper_a ( *src ); return 1; }
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToUpper ( x );
	// put it back to utf8. return bytes stored.
	return utf8Encode ( y , dst );
}

inline int32_t to_lower_utf8 (char *dst, char *dstEnd, char *src, char *srcEnd ){
	char *dstart = dst;
	for ( ; src < srcEnd ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

inline int32_t to_lower_utf8 (char *dst, char *dstEnd, char *src ){
	char *dstart = dst;
	for ( ; *src ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

void getCalendarFromMs(int64_t ms, 
		       int32_t* days, 
		       int32_t* hours, 
		       int32_t* minutes, 
		       int32_t* secs,
		       int32_t* msecs);

//inline 
//int32_t u16UrlEncode(char *d, int32_t dlen, char *s, int32_t slen, 
//		  bool requestPath = false){
//	char u8Buf[2048];
//	int32_t u8Len = utf16ToUtf8(u8Buf, 2048, s, slen);
//	return urlEncode(d, dlen, u8Buf, u8Len, requestPath);
//}

uint32_t calculateChecksum(char *buf, int32_t bufLen);
char* getNextNum(char* input, char** numPtr);

// use ucIsAlnum instead...
inline bool ucIsWordChar(UChar32 c) {
	if (!(c & 0xffffff80)) return is_alnum_a(c);
	//if (c < 256) return is_alnum(c);
	void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_WORDCHAR;
}

// don't allow "> in our input boxes
int32_t cleanInput(char *outbuf, int32_t outbufSize, char *inbuf, int32_t inbufLen);

// not in math.h?
inline double round(double x) {
	return floor(x+0.5);
}

// like strcpy but return the length and always null terminates
// dst should be of size maxDstLen + 1
inline int32_t setstr ( char *dst,
                     int32_t  maxDstLen,
                     char *src,
                     int32_t  srcLen ) {
        // get the proper length
        int32_t dstLen = srcLen;
        if ( srcLen > maxDstLen ) dstLen = maxDstLen;
        // copy the string
        gbmemcpy ( dst, src, dstLen );
        // NULL terminate
        dst[dstLen] = '\0';
        // return the proper length
        return dstLen;
}

// 
// these three functions replace the Msg.cpp/.h class
//
// actually "lastParm" point to the thing right after the lastParm
int32_t getMsgStoredSize ( int32_t baseSize, 
			int32_t *firstSizeParm, 
			int32_t *lastSizeParm ) ;
// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg ( int32_t  baseSize ,
		     int32_t *firstSizeParm ,
		     int32_t *lastSizeParm ,
		     char **firstStrPtr ,
		     void *thisPtr     ,
		     int32_t *retSize     ,
		     char *userBuf     ,
		     int32_t  userBufSize ,
		     bool  makePtrsRefNewBuf ) ;

char *serializeMsg2 ( void *thisPtr ,
		      int32_t objSize ,
		      char **firstStrPtr ,
		      int32_t *firstSizeParm ,
		      int32_t *retSize );

// convert offsets back into ptrs
// returns -1 on error
int32_t deserializeMsg ( int32_t  baseSize ,
		      int32_t *firstSizeParm ,
		      int32_t *lastSizeParm ,
		      char **firstStrPtr ,
		      char *stringBuf ) ;

bool deserializeMsg2 ( char **firstStrPtr , int32_t  *firstSizeParm );

#endif 
