// Matt Wells, copyright Jun 2001

#ifndef _FCTYPES_H_
#define _FCTYPES_H_

#include <sys/time.h>  // gettimeofday()
#include <math.h>      // floor()
#include "Unicode.h"

bool verifyUtf8 ( char *txt ) ;
bool verifyUtf8 ( char *txt , long tlen ) ;

bool print96  ( char     *k  ) ;
bool print96  ( key_t    *kp ) ;
bool print128 ( char     *k  ) ;
bool print128 ( key128_t *kp ) ;


// print it to stdout for debugging Dates.cpp
long printTime ( long ttt );
time_t mktime_utc ( struct tm *ttt ) ;

struct SafeBuf;
// seems like this should be defined, but it isn't
long strnlen ( const char *s , long maxLen );
// this too
char *strncasestr( char *haystack, long haylen, char *needle);
char *strnstr( char *haystack, long haylen, char *needle);

// just like sprintf(s,"%llu",n), but we insert commas
long ulltoa ( char *s , unsigned long long n ) ;

// . convert < to &lt; and > to &gt and & to &amp;
// . store "t" into "s"
// . returns bytes stored into "s"
// . NULL terminates "s"
long saftenTags ( char *s , long slen , char *t , long tlen ) ;

// . basically just converts "'s to &#34;'s
// . store "src" into "dest" and return bytes stored
// . does not do bounds checking in "dest"
// . used to encode things as form input variables, like query in HttpPage0.cpp
long dequote       ( char *dest , char *dend , char *src , long srcLen ) ;

// . entity-ize a string so it's safe for html output
// . converts "'s to &#34;'s, &'s to &amps; <'s the &lt; and >'s to &gt;
// . store "src" into "dest" and return bytes stored
// . does not do bounds checking on "dest"
// . encode t into s
char *htmlEncode ( char *s , char *send , char *t , char *tend ,
		   bool pound = false , long niceness = 0) ;
bool htmlEncode ( SafeBuf* s , char *t , char *tend , 
		  bool pound = false , long niceness = 0 );

// . like above but src is NULL terminated
// . returns length of string stored into "dest"
// . decode html entities like &amp; and &gt;
long htmlDecode    ( char *dst, char *src, long srcLen, 
		     bool doSpecial ,//=false);
		     long niceness);

// . convert " to %22 , & to %26, is that it?
// . urlEncode() stores the encoded, NULL-terminated URL in "dest"
// . requestPath leaves \0 and ? characters intact, for encoding requests
long urlEncode     ( char *dest , long destLen , char *src , long srcLen ,
		     bool  requestPath = false ) ;
// determine the length of the encoded url, does NOT include NULL
long urlEncodeLen  ( char *s , long slen , bool requestPath = false ) ;
// decode a url -- decode ALL %XX's
long urlDecode ( char *dest , char *t , long tlen ) ;
// . normalize the encoding
// . like urlDecode() but only decodes chars that should not have been encoded
// . also, will encode characters that should have been encoded
long urlNormCode ( char *dest , long destLen , char *src , long srcLen ) ;

bool is_digit(unsigned char c) ;

// is character, "s", used in textual hexadecimal representation?
bool is_hex ( char s ) ;
bool is_urlchar(char s);

// convert hex digit to value
long htob ( char s ) ;
char btoh ( char s ) ;
// convert hex-encoded binary string back to binary
void hexToBin ( char *src , long srcLen , char *dst );
void binToHex ( unsigned char *src , long srcLen , char *dst );

// the _a suffix denotes an ascii string
bool is_lower2_a  (char *s,long len) ;
bool is_lower1_a  (char *s) ;
bool is_ascii2    (char *s,long len) ;
bool is_alnum2_a  (char *s,long len) ;
bool has_alpha_a  (char *s , char *send ) ;
bool has_alpha_utf8(char *s, char *send ) ;
bool is_alpha2_a  (char *s,long len) ;
bool is_num       (char *s,long len) ;
bool is_upper2_a  (char *s,long len) ;
bool is_cap_a     (char *s,long len) ;
bool is_cap_utf8  (char *s,long len) ;

bool is_vowel_a ( char s );
bool has_vowel_a ( char *s , long slen );

// does it have at least one upper case character in it?
bool has_upper_a  (char *s,long len) ;
bool has_binary_a (char *s,long len) ;
void to_lower3_a  (char *s,long len, char *buf) ;

void to_lower1            (char *s) ;
long to_lower_alnum       (char *s,long len, char *buf) ;
long to_lower_utf8        (char *dst , char *src ) ;
long to_lower_utf8        (char *dst , char *dstEnd, char *src ) ;
long to_lower_utf8        (char *dst , char *dstEnd, char *src, char *srcEnd) ;
long to_lower_utf8_32     (char *src ) ;
long to_cap_alnum         (char *s,long len, char *buf) ;
long to_alnum             (char *s,long len, char *buf) ;
void to_upper3_a          (char *s,long len, char *buf) ;
void to_cap               (char *s,long len, char *buf) ;

// . approximate # of non-punct words
// . s must be NULL terminated
// . used by LinkInfo.cpp to weight link text based on # of words
long getNumWords ( char *s );

// true if character should be stripped from the end/beginning of the title
// error! make a map of this
bool is_title_junk(char c) ;

// . get the # of words in this string
long      getNumWords ( char *s , long len, long titleVersion ) ;
long      atol2       ( const char *s, long len ) ;
long long atoll1      ( const char *s ) ;
long long atoll2      ( const char *s, long len ) ;
double    atof2       ( const char *s, long len ) ;
double    atod2       (       char *s, long len ) ;
bool      atob        ( const char *s, long len ) ;

// like strstr but haystack need not be NULL terminated
char *strncasestr ( char *haystack , char *needle , long haystackSize ) ;
char *strncasestr ( char *haystack , char *needle , 
		    long haystackSize, long needleSize ) ;
char *strnstr ( char *haystack , char *needle , long haystackSize ) ;

// independent of case
char *gb_strcasestr ( char *haystack , char *needle );
char *gb_strncasestr ( char *haystack , long haystackSize , char *needle ) ;

// updates our static var, s_adjustment to keep our clock in sync to hostId #0
void settimeofdayInMillisecondsGlobal ( long long newTime ) ;

// convert global to local time in milliseconds
long long globalToLocalTimeMilliseconds ( long long global ) ;
long long localToGlobalTimeMilliseconds ( long long local  ) ;
// the same thing but in seconds
long      globalToLocalTimeSeconds      ( long      global ) ;
long      localToGlobalTimeSeconds      ( long      local  ) ;

// we now default this to local time to avoid jumpiness associated with
// having to sync with host #0. most routines calling this usually are just
// taking deltas. 
long long gettimeofdayInMillisecondsGlobal() ; // synced with host #0
long long gettimeofdayInMillisecondsGlobalNoCore() ; // synced with host #0
long long gettimeofdayInMillisecondsSynced() ; // synced with host #0
long long gettimeofdayInMillisecondsLocal () ;// this is local now
long long gettimeofdayInMilliseconds() ;// this is local now
uint64_t gettimeofdayInMicroseconds(void) ;

// . get time in seconds since epoch
// . use this instead of call to time(NULL) cuz it uses adjustment
time_t getTime       ();  // this is local now
time_t getTimeLocal  (); 
time_t getTimeGlobal (); // synced with host #0's system clock
time_t getTimeGlobalNoCore (); // synced with host #0's system clock
time_t getTimeSynced (); // synced with host #0's system clock

long stripHtml( char *content, long contentLen, long version, long strip );

extern const char g_map_is_vowel[];
extern const char g_map_to_lower[];
extern const char g_map_to_upper[];
extern const char g_map_to_ascii[];
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
extern long long g_adjustment;

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
#define is_ascii3(c)           g_map_is_ascii3[(unsigned char)c]
#define is_punct_a(c)          g_map_is_punct[(unsigned char)c]
#define is_alnum_a(c)          g_map_is_alnum[(unsigned char)c]
#define is_alpha_a(c)          g_map_is_alpha[(unsigned char)c]
#define is_digit(c)            g_map_is_digit[(unsigned char)c]
#define is_hex(c)              g_map_is_hex[(unsigned char)c]
#define is_tagname_char(c)     g_map_is_tagname_char[(unsigned char)c]
#define is_tag_control_char(c) g_map_is_tag_control_char[(unsigned char)c]
#define is_matchskip_a(c)      g_map_is_matchskip[(unsigned char)c]

inline bool is_upper_utf8 ( char *s );

inline bool has_vowel_a ( char *s , long slen ) {
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
inline long htob ( char s ) {
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
inline bool is_lower_as(char *s,long len) {
	for (long i=0;i<len;i++)
		if (!is_lower_a(s[i]))
			return false;
	return true;
}

// have to put an extra "s" on function name to avoid macro conflict
inline bool is_lower_as(char *s) {
	for (long i=0;s[i];i++)
		if (!is_lower_a(s[i]))
			return false;
	return true;
}

inline bool is_ascii2_a(char *s,long len) {
	for (long i=0;i<len;i++)
		if (!is_ascii(s[i]))
			return false;
	return true;
}

inline bool is_alnum2_a(char *s,long len) {
	for (long i=0;i<len;i++)
		if (!is_alnum_a(s[i]))
			return false;
	return true;
}

inline bool is_alpha2_a(char *s,long len) {
	for (long i=0;i<len;i++)
		if (!is_alpha_a(s[i]))
			return false;
	return true;
}

inline bool is_num(char *s,long len) {
	for (long i=0;i<len;i++)
		if (!is_digit(s[i]))
			return false;
	return true;
}

inline bool is_upper2_a (char *s,long len) {
	for (long i=0;i<len;i++)
		if (!is_upper_a(s[i]))
			return false;
	return true;
}

inline bool is_cap_a (char *s,long len) {
	if (!is_upper_a(s[0]))
		return false;
	for (long i=1;i<len;i++)
		if (!is_lower_a(s[i]))
			return false;
	return true;
}

inline bool is_cap_utf8 (char *s,long len) {
	if ( ! is_upper_utf8 ( s ) ) return false;
	char *send = s + len;
	for ( ; s < send ; s += getUtf8CharSize ( s ) ) 
		if ( is_upper_utf8 ( s ) ) return false;
	return true;
}

// does it have at least one upper case character in it?
inline bool has_upper_a (char *s,long len) {
		for (long i=0;i<len;i++) 
			if ( is_upper_a(s[i])) 
				return true;
	return false;
}

// does it have at least one binary character in it?
inline bool has_binary_a (char *s,long len) {
	for (long i=0;i<len;i++) 
		if ( is_binary_a(s[i])) 
			return true;
	return false;
}

inline void to_lower3_a(char *s,long len, char *buf) {
	for (long i=0;i<len ;i++)
		buf[i]=to_lower_a((unsigned char)s[i]);
}

inline void to_lower1_a(char *s) {
	for (long i=0;s[i] ;i++)
		s[i]=to_lower_a((unsigned char)s[i]);
}

inline long to_lower_alnum_a(char *s,long len, char *buf) {
	long j=0;
	for (long i=0;i<len ;i++)
		if (is_alnum_a(s[i]))
			buf[j++]=to_lower_a((unsigned char)s[i]);
	return j;
}

inline long to_cap_alnum_a(char *s,long len, char *buf) {
	buf[0] = to_upper_a(s[0]);
	long j=1;
	for (long i=1;i<len ;i++)
		if (is_alnum_a(s[i]))
			buf[j++]=to_lower_a((unsigned char)s[i]);
	return j;
}

inline long to_alnum_a(char *s,long len, char *buf) {
	long j=0;
	for (long i=0;i<len ;i++)
		if (is_alnum_a(s[i]))
			buf[j++]=s[i];
	return j;
}

inline void to_upper3_a(char *s,long len, char *buf) {
	for (long i=0;i<len;i++)
		buf[i]=to_upper_a(s[i]);
}

inline void to_cap_a(char *s,long len, char *buf) {
	buf[0]=to_upper_a(s[0]);
	for (long i=1;i<len;i++)
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
inline long to_lower_utf8 ( char *dst , char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) { *dst = to_lower_a ( *src ); return 1; }
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToLower ( x );
	// put it back to utf8. return bytes stored.
	return utf8Encode ( y , dst );
}

// store answer in the long and return that!
inline long to_lower_utf8_32 ( char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return (long) to_lower_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToLower ( x );
	// give that back
	return y;
}

inline long to_upper_utf8 ( char *dst , char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) { *dst = to_upper_a ( *src ); return 1; }
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToUpper ( x );
	// put it back to utf8. return bytes stored.
	return utf8Encode ( y , dst );
}

inline long to_lower_utf8 (char *dst, char *dstEnd, char *src, char *srcEnd ){
	char *dstart = dst;
	for ( ; src < srcEnd ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

inline long to_lower_utf8 (char *dst, char *dstEnd, char *src ){
	char *dstart = dst;
	for ( ; *src ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

void getCalendarFromMs(long long ms, 
		       long* days, 
		       long* hours, 
		       long* minutes, 
		       long* secs,
		       long* msecs);

//inline 
//long u16UrlEncode(char *d, long dlen, char *s, long slen, 
//		  bool requestPath = false){
//	char u8Buf[2048];
//	long u8Len = utf16ToUtf8(u8Buf, 2048, s, slen);
//	return urlEncode(d, dlen, u8Buf, u8Len, requestPath);
//}

unsigned long calculateChecksum(char *buf, long bufLen);
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
long cleanInput(char *outbuf, long outbufSize, char *inbuf, long inbufLen);

// not in math.h?
inline double round(double x) {
	return floor(x+0.5);
}

// like strcpy but return the length and always null terminates
// dst should be of size maxDstLen + 1
inline long setstr ( char *dst,
                     long  maxDstLen,
                     char *src,
                     long  srcLen ) {
        // get the proper length
        long dstLen = srcLen;
        if ( srcLen > maxDstLen ) dstLen = maxDstLen;
        // copy the string
        memcpy ( dst, src, dstLen );
        // NULL terminate
        dst[dstLen] = '\0';
        // return the proper length
        return dstLen;
}

// 
// these three functions replace the Msg.cpp/.h class
//
// actually "lastParm" point to the thing right after the lastParm
long getMsgStoredSize ( long baseSize, 
			long *firstSizeParm, 
			long *lastSizeParm ) ;
// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg ( long  baseSize ,
		     long *firstSizeParm ,
		     long *lastSizeParm ,
		     char **firstStrPtr ,
		     void *thisPtr     ,
		     long *retSize     ,
		     char *userBuf     ,
		     long  userBufSize ,
		     bool  makePtrsRefNewBuf ) ;
// convert offsets back into ptrs
long deserializeMsg ( long  baseSize ,
		      long *firstSizeParm ,
		      long *lastSizeParm ,
		      char **firstStrPtr ,
		      char *stringBuf ) ;

#endif 
