// Matt Wells, Copyright Apr 2001

// . don't use XOR for hashing, "dog" would be the same as "god"

#ifndef _HASH_H_
#define _HASH_H_

#include "Unicode.h"

//#define SEED8  148
//#define SEED16 22081
//#define SEED32 987654321
//#define SEED64 5148502070294393521LL
//#define SEED8  9876
//#define SEED32 87654321
//#define SEED64 7651331LL

// call this before calling any hash*() routines so we can fill our table
extern uint64_t g_hashtab[256][256];

#include "types.h"

bool hashinit ();

unsigned char hash8            ( char *s , int32_t len ) ;
uint16_t      hash16           ( char *s , int32_t len ) ;
uint64_t      hash64n_nospaces ( char *s , int32_t len ) ;
uint32_t hash32n          ( char *s ) ;
uint32_t hash32           ( char *s, int32_t len,uint32_t startHash=0);
uint32_t hash32h          ( uint32_t h1 , uint32_t h2 ) ;
uint64_t      hash64h          ( uint64_t h1 , uint64_t h2 );
uint32_t      hash32Fast       ( uint32_t h1 , uint32_t h2 ) ;
uint32_t hash32Lower_a    ( char *s, int32_t len,uint32_t startHash=0);
uint32_t hash32Lower_utf8  ( char *s, int32_t len,uint32_t startHash=0);
uint32_t      hash32b          (char *s,int32_t len1,char *s2, int32_t len2);
uint32_t      hash32_cont      ( char *s, char *slen,
				 uint32_t startHash , int32_t *conti );
uint64_t      hash64n          ( char *s, uint64_t startHash =0LL);
uint64_t      hash64           ( uint64_t h1,uint64_t h2);
uint64_t      hash64           ( char *s,int32_t len,uint64_t startHash=0);
uint64_t      hash64_cont      ( char *s,int32_t len,
				 uint64_t startHash,int32_t *conti);
uint64_t      hash64b          ( char *s,         uint64_t startHash=0);
uint64_t      hash64Fast       ( uint64_t h1 , uint64_t h2 ) ;
uint64_t      hash64Lower_a    ( char *s, int32_t len, uint64_t startHash = 0 );
uint64_t      hash64Lower_utf8 ( char *s, int32_t len, uint64_t startHash = 0 );
uint64_t      hash64Lower_utf8_nospaces ( char *s, int32_t len );
uint64_t      hash64Lower_utf8 ( char *s );
uint64_t      hash64Lower_utf8_cont ( char *s, int32_t len, uint64_t startHash ,
				      int32_t *conti );
uint64_t      hash64LowerAscii_utf8 ( char *s );
uint96_t      hash96           ( char *s, int32_t slen,uint96_t sh=(uint96_t)0);
uint96_t      hash96           ( uint96_t  h1 ,  uint96_t h2 );
uint96_t      hash96           ( int32_t       h1 ,  uint96_t h2 );
uint128_t     hash128          ( uint128_t h1 ,  uint128_t h2 );
uint128_t     hash128          ( int32_t       h1 ,  uint128_t h2 );
void          hash2string      ( uint64_t h, char *buf ) ;
uint32_t hashLong         ( uint32_t x ) ;

// . these convert \n to \0 when hashing
// . these hash all punct as a space, except for hyphen and single quote!
// . these lower-case all alnum chars, even crazy utf8 chars that can be cap'd
// . these only take utf8 strings
uint32_t hash32d ( char *s, char *send );
uint64_t hash64d ( char *s, int32_t slen );

inline uint32_t hash32d ( char *s, int32_t slen ) { return hash32d ( s , s+slen); };
//inline uint64_t hash64d ( char *s, int32_t slen ) { return hash64d ( s , s+slen); };

uint64_t       hash64Upper_a    ( char *s, int32_t len, uint64_t startHash = 0 );
//uint64_t       hash64Ascii      ( char *s, int32_t len, uint64_t startHash = 0 );
//uint64_t       hash64AsciiLower ( char *s, int32_t len,uint64_t startHash = 0 );
//uint64_t       hash64AsciiLowerE( char *s, int32_t len,uint64_t startHash = 0 );
//uint64_t       hash64AsciiLowerAlnumOnly (char *s, int32_t len, starthash=0);
//uint64_t       hash64Cap        ( char *s, int32_t len, uint64_t startHash = 0 );
//uint64_t       hash64AsciiCap   ( char *s, int32_t len, uint64_t startHash = 0 );
// . used to setup hashing of collection/fields over a body of words for a
//   document or query
// . used in TermTable.cpp and in SimpleQuery.cpp
//int64_t getPrefixHash ( const char *prefix1 , int32_t prefixLen1 ,
//			  const char *prefix2 , int32_t prefixLen2 ) ;


inline uint64_t hash64b ( char *s , uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( s[i] ) {
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s[i]];
		i++;
	}
	return h;
}

inline uint64_t hash64 ( char *s, int32_t len, 
				   uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) { 
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s[i]];
		i++;
	}
	return h;
}

inline uint64_t hash64_cont ( char *s, int32_t len, 
					uint64_t startHash ,
					int32_t *conti ) {
	uint64_t h = startHash;
	int32_t i = 0;//*conti;
	while ( i < len ) { 
		h ^= g_hashtab [(unsigned char)(i+*conti)][(unsigned char)s[i]];
		i++;
	}
	*conti = *conti + i;
	return h;
}

inline uint32_t hash32Fast ( uint32_t h1 , uint32_t h2 ) {
	return (h2 << 1) ^ h1;
}

inline uint64_t hash64Fast ( uint64_t h1 , uint64_t h2 ) {
	return (h2 << 1) ^ h1;
}

// . combine 2 hashes into 1
// . TODO: ensure this is a good way
// . used for combining words' hashes into phrases (also fields,collections)..
inline uint64_t hash64 (uint64_t h1,uint64_t h2){
	// treat the 16 bytes as a string now instead of multiplying them
	uint64_t h = 0;

	h ^= g_hashtab [ 0] [ *((unsigned char *)(&h1)+0) ] ;
	h ^= g_hashtab [ 1] [ *((unsigned char *)(&h1)+1) ] ;
	h ^= g_hashtab [ 2] [ *((unsigned char *)(&h1)+2) ] ;
	h ^= g_hashtab [ 3] [ *((unsigned char *)(&h1)+3) ] ;
	h ^= g_hashtab [ 4] [ *((unsigned char *)(&h1)+4) ] ;
	h ^= g_hashtab [ 5] [ *((unsigned char *)(&h1)+5) ] ;
	h ^= g_hashtab [ 6] [ *((unsigned char *)(&h1)+6) ] ;
	h ^= g_hashtab [ 7] [ *((unsigned char *)(&h1)+7) ] ;

	h ^= g_hashtab [ 8] [ *((unsigned char *)(&h2)+0) ] ;
	h ^= g_hashtab [ 9] [ *((unsigned char *)(&h2)+1) ] ;
	h ^= g_hashtab [10] [ *((unsigned char *)(&h2)+2) ] ;
	h ^= g_hashtab [11] [ *((unsigned char *)(&h2)+3) ] ;
	h ^= g_hashtab [12] [ *((unsigned char *)(&h2)+4) ] ;
	h ^= g_hashtab [13] [ *((unsigned char *)(&h2)+5) ] ;
	h ^= g_hashtab [14] [ *((unsigned char *)(&h2)+6) ] ;
	h ^= g_hashtab [15] [ *((unsigned char *)(&h2)+7) ] ;

	return h;
}


inline uint64_t hash64Lower_a ( char *s, int32_t len, 
					uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i] 
			[(unsigned char)to_lower_a(s[i])];
		i++;
	}
	return h;
}

// utf8
inline uint64_t hash64Lower_utf8 ( char *p, int32_t len, uint64_t startHash ) {
	uint64_t h = startHash;
	uint8_t i = 0;
	char *pend = p + len;
	char cs;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// convert utf8 apostrophe to ascii apostrophe so Words.cpp
		// gets the right wid for stuff like "you're" when the
		// apostrophe is in utf8
		//if ( p[0]==(char)0xe2 && 
		//     p[1]==(char)0x80 && 
		//     cs==3 && 
		//     (p[2]==(char)0x99||p[2]==(char)0x9c) ) {
		//	h ^= g_hashtab [i++][(uint8_t)'\''];
		//	continue;
		//}
		// otherwise, lower case it
		x = utf8Decode((char *)p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , tmp );
		// sanity check
		if ( ncs > 4 ) { char *xx=NULL;*xx=0; }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	return h;
}

inline uint64_t hash64Lower_utf8_nospaces ( char *p, int32_t len  ) {
	uint64_t h = 0LL;
	uint8_t i = 0;
	char *pend = p + len;
	char cs;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			// skip spaces
			if ( is_wspace_a(*p) ) continue;
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// otherwise, lower case it
		x = utf8Decode((char *)p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , tmp );
		// sanity check
		if ( ncs > 4 ) { char *xx=NULL;*xx=0; }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	return h;
}


inline uint64_t hash64Lower_utf8_cont ( char *p, 
					int32_t len, 
					uint64_t startHash ,
					int32_t *conti ) {
	uint64_t h = startHash;
	uint8_t i = *conti;
	char *pend = p + len;
	char cs;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// convert utf8 apostrophe to ascii apostrophe so Words.cpp
		// gets the right wid for stuff like "you're" when the
		// apostrophe is in utf8
		//if ( p[0]==(char)0xe2 && 
		//     p[1]==(char)0x80 && 
		//     cs==3 && 
		//     (p[2]==(char)0x99||p[2]==(char)0x9c) ) {
		//	h ^= g_hashtab [i++][(uint8_t)'\''];
		//	continue;
		//}
		// otherwise, lower case it
		x = utf8Decode((char *)p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , tmp );
		// sanity check
		if ( ncs > 4 ) { char *xx=NULL;*xx=0; }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	// update this so caller can re-call with the right i
	*conti = i;
	return h;
}

inline uint32_t hash32_cont ( char *p, int32_t plen,
			      uint32_t startHash , int32_t *conti ) {
	uint32_t h = startHash;
	uint8_t i = *conti;
	char *pend = p + plen;
	for ( ; p < pend ; p++ ) {
		h ^= (uint32_t)g_hashtab [i++] [(uint8_t)(*p)];
	}
	// update this so caller can re-call with the right i
	*conti = i;
	return h;
}


// utf8
inline uint32_t hash32Lower_utf8 ( char *p, int32_t len, 
					uint32_t startHash ) {
	return (uint32_t) hash64Lower_utf8 ( p , len , startHash );
}

inline uint32_t hash32b (char *s1,int32_t len1,char *s2, int32_t len2) {
	uint32_t h = 0;//startHash;
	int32_t i = 0;
	while ( i < len1 ) { 
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s1[i]];
		i++;
	}
	i = 0;
	while ( i < len2 ) { 
		h ^= g_hashtab [(unsigned char)(i+len1)][(unsigned char)s2[i]];
		i++;
	}
	return h;
}


// exactly like above but p is NULL terminated for sure
inline uint64_t hash64Lower_utf8 ( char *p ) {
	uint64_t h = 0;
	uint8_t i = 0;
	UChar32 x;
	UChar32 y;
	char cs;
	for ( ; *p ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++] 
				[(uint8_t)to_lower_a(*p)];
			continue;
		}
		// otherwise, lower case it
		x = utf8Decode(p);
		// convert to lower
		y = ucToLower (x);
		// back to utf8
		char tmp[4];
		char ncs = utf8Encode ( y , (char *)tmp );
		// sanity check
		if ( ncs > 4 ) { char *xx=NULL;*xx=0; }
		// i've seen this happen for 4 byte char =
		// -16,-112,-51,-125  which has x=66371 and y=66371
		// but utf8Encode() returned 0!
		if ( ncs == 0 ) {
			// let's just hash it as-is then
			tmp[0] = p[0];
			if ( cs >= 1 ) tmp[1] = p[1];
			if ( cs >= 2 ) tmp[2] = p[2];
			if ( cs >= 3 ) tmp[3] = p[3];
			ncs = cs;
		}
		// hash it up
		h ^= g_hashtab [i++][(uint8_t)tmp[0]];
		if ( ncs == 1 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[1]];
		if ( ncs == 2 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[2]];
		if ( ncs == 3 ) continue;
		h ^= g_hashtab [i++][(uint8_t)tmp[3]];
	}
	return h;
}


// utf8
inline uint64_t hash64LowerAscii_utf8 (char *p,int32_t len,uint64_t startHash){
	uint64_t h = startHash;
	uint8_t i = 0;
	char *pend = p + len;
	char cs;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize(p);
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			h ^= g_hashtab [i++] [(uint8_t)to_lower_a(*p)];
			continue;
		}
		// otherwise, lower case it
		UChar x = utf8Decode((char *)p);
		// convert into latin1 (very fast)
		char y = latin1Encode(x);
		// does not work?
		if ( y == 0 ) continue;
		// convert latin1 char into ascii
		char z = to_ascii ( y );
		// hash it as ascii then
		h ^= g_hashtab [i++][(uint8_t)to_lower_a(z)];
	}
	return h;
}


inline uint64_t hash64Upper_a ( char *s , int32_t len , 
					  uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i] 
			[(unsigned char)to_upper_a(s[i])]; 
		i++; 
	}
	return h;
}


/*
// utf8
inline uint64_t hash64AsciiLowerE ( char *s, int32_t len, uint64_t startHash ) {
	uint64_t h = startHash;
	uint8_t i = 0;
	UChar32 x;
	UChar32 y;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize();
		// check for entity
		if ( *src != '&' ) { // || ! hasHtmlEntities ) {
			// deal with one ascii char quickly
			if ( cs == 1 ) {
				h ^= g_hashtab [i++] 
					[(uint8_t)to_lower_ascii(*src)];
				continue;
			}
			// otherwise, lower case it
			x = utf8Decode(src,NULL);
			// hookin here from below
		hookin:
			// convert to lower
			y = ucToLowerAscii (x);
			// back to utf8
			uint8_t tmp[4];
			char    ncs = utf8Encode ( y , tmp );
			// sanity check
			if ( ncs > 4 ) { char *xx=NULL;*xx=0; }
			// hash it up
			h ^= g_hashtab [i++][tmp[0]];
			if ( ncs == 1 ) continue;
			h ^= g_hashtab [i++][tmp[1]];
			if ( ncs == 2 ) continue;
			h ^= g_hashtab [i++][tmp[2]];
			if ( ncs == 3 ) continue;
			h ^= g_hashtab [i++][tmp[3]];
		}
		// entity?
		skip = getEntity(&s[i],len-i,&x, true);
		// if not valid... that's wierd, sanity check
		if ( skip <= 0 ) { char *xx = NULL; *xx = 0; }
		// skip it man
		p += skip - cs;
		// resume above
		goto hookin;
	}
	return h;
}

inline uint64_t hash64Ascii ( char *s , int32_t len , 
					uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i] 
			[(unsigned char)to_ascii(s[i])];
		i++;
	}
	return h;
}

inline uint64_t hash64AsciiLower (char *s, int32_t len, 
				   uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i]
			[(unsigned char)to_lower_ascii(s[i])];
		i++;
	}
	return h;
}

inline uint64_t hash64Cap( char *s , int32_t len , 
				     uint64_t startHash ) {
	uint64_t h = startHash;
	// first letter is cap
	if ( len < 1 ) return h;
	h ^= g_hashtab [0] [(unsigned char)to_upper(s[0])];
	// then hash rest normally
	int32_t i = 1;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i] 
			[(unsigned char)to_lower(s[i])];
		i++;
	}
	return h;
}
*/

/*
inline uint64_t hash64AsciiCap( char *s , int32_t len, 
					  uint64_t startHash ) {
	uint64_t h = startHash;
	// first letter is cap
	if ( len < 1 ) return h;
	h ^= g_hashtab [0] [(unsigned char)to_upper(s[0])];
	// then hash rest normally
	int32_t i = 1;
	while ( i < len ) {
		h ^= g_hashtab [(unsigned char)i]
			[(unsigned char)to_lower_ascii(s[i])];
		i++;
	}
	return h;
}

// used by Checksumdb.cpp for comparing if docs are dups
inline uint64_t hash64AsciiLowerAlnumOnly (char *s, int32_t len, 
				   uint64_t startHash ) {
	uint64_t h = startHash;
	int32_t i = 0;
	int32_t count = 0;
	while ( i < len ) {
		// only hash alnum chars, no punct or spaces, etc.
		if ( ! is_alnum(s[i]) ) { i++; continue; }
		h ^= g_hashtab [(unsigned char)count]
			[(unsigned char)to_lower_ascii(s[i])];
		i++;
		count++;
	}
	return h;
}
*/

// returns the 'clean' letter by removing accents and puctuation marks
// supporting only iso charsets for now
//char getClean( UChar32 c );
uint8_t getClean_a ( char c ) ;
UChar32 getClean_utf8 ( char *src ) ;


inline uint32_t hashLong ( uint32_t x ) {
	uint32_t h = 0;
	unsigned char *p = (unsigned char *)&x;
	h ^= (uint32_t) g_hashtab [0][p[0]];
	h ^= (uint32_t) g_hashtab [1][p[1]];
	h ^= (uint32_t) g_hashtab [2][p[2]];
	h ^= (uint32_t) g_hashtab [3][p[3]];
	return h;
}

#endif
