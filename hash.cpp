#include "gb-include.h"

unsigned long long g_hashtab[256][256] ;

// . used for computing zobrist hash of a string up to 256 chars long
// . first array component is the max length, 256, of the string
bool hashinit () {
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;
	// show RAND_MAX
	//printf("RAND_MAX = %lu\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( long i = 0 ; i < 256 ; i++ )
		for ( long j = 0 ; j < 256 ; j++ ) {
			g_hashtab [i][j]  = (unsigned long long)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (unsigned long long)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
		}
	if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;
	s_initialized = true;
	return true;
}

// TODO: ensure this wraps over properly
unsigned char hash8 ( char *s , long len ) {
	unsigned char h = 0;
	long i = 0;
	while ( i < len ) {
		h ^= (unsigned char) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

unsigned short hash16 ( char *s , long len ) {
	unsigned short h = 0;
	long i = 0;
	while ( i < len ) {
		h ^= (unsigned short) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

unsigned long hash32n ( char *s ) {
	unsigned long h = 0;
	long i = 0;
	while ( s[i] ) {
		h ^= (unsigned long) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

uint64_t hash64n ( char *s, unsigned long long startHash ) {
	unsigned long long h = startHash;
	for ( long i = 0 ; s[i] ; i++ )
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s[i]];
	return h;
}

uint64_t hash64n_nospaces ( char *s, long len ) {
	unsigned long long h = 0LL;
	long k = 0;
	for ( long i = 0 ; i<len ; i++ ) {
		if ( s[i] == ' ' ) continue;
		h ^= g_hashtab [(unsigned char)k] [(unsigned char)s[i]];
		k++;
	}
	return h;
}

unsigned long hash32 ( char *s, long len, unsigned long startHash ) {
	unsigned long h = startHash;
	long i = 0;
	while ( i < len ) {
		h ^= (unsigned long) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

unsigned long hash32Lower_a ( char *s,long len,unsigned long startHash){
	unsigned long h = startHash;
	long i = 0;
	while ( i < len ) {
		h ^= (unsigned long) g_hashtab [(unsigned char)i] 
			[(unsigned char)to_lower_a(s[i])];
		i++;
	}
	return h;
}

u_int96_t hash96 ( char *s, long slen, u_int96_t startHash ) {
	u_int96_t h;
	h.n0 = hash64 ( s , slen , startHash.n0 );
	h.n1 = hash32 ( s , slen , startHash.n1 );
	return h;
}

u_int96_t hash96 ( u_int96_t h1 ,  u_int96_t h2 ) {
	h1.n0 = hash64  ( h1.n0 , h2.n0 );
	h1.n1 = hash32h ( h1.n1 , h2.n1 );
	return h1;
}

u_int96_t hash96 ( long h1 ,  u_int96_t h2 ) {
	h2.n0 = hash64  ( h1 , h2.n0 );
	h2.n1 = hash32h ( h1 , h2.n1 );
	return h2;
}

u_int128_t hash128 ( u_int128_t h1 ,  u_int128_t h2 ) {
	h1.n0 = hash64 ( h1.n0 , h2.n0 );
	h1.n1 = hash64 ( h1.n1 , h2.n1 );
	return h1;
}

u_int128_t hash128 ( long h1 ,  u_int128_t h2 ) {
	h2.n0 = hash64 ( h1 , h2.n0 );
	h2.n1 = hash64 ( h1 , h2.n1 );
	return h2;
}

// . combine 2 hashes into 1
// . TODO: ensure this is a good way
// . used for combining words' hashes into phrases (also fields,collections)..
unsigned long hash32h ( unsigned long h1 , unsigned long h2 ) {
	// treat the 16 bytes as a string now instead of multiplying them
	unsigned long h = 0;
	h ^= g_hashtab [ 0] [ ((unsigned char *)&h1)[0] ] ;
	h ^= g_hashtab [ 1] [ ((unsigned char *)&h1)[1] ] ;
	h ^= g_hashtab [ 2] [ ((unsigned char *)&h1)[2] ] ;
	h ^= g_hashtab [ 3] [ ((unsigned char *)&h1)[3] ] ;

	h ^= g_hashtab [ 4] [ ((unsigned char *)&h2)[0] ] ;
	h ^= g_hashtab [ 5] [ ((unsigned char *)&h2)[1] ] ;
	h ^= g_hashtab [ 6] [ ((unsigned char *)&h2)[2] ] ;
	h ^= g_hashtab [ 7] [ ((unsigned char *)&h2)[3] ] ;
	return h;
}

uint64_t hash64h ( uint64_t h1 , uint64_t h2 ) {
	// treat the 16 bytes as a string now instead of multiplying them
	uint64_t h = 0;
	h ^= g_hashtab [ 0] [ ((unsigned char *)&h1)[0] ] ;
	h ^= g_hashtab [ 1] [ ((unsigned char *)&h1)[1] ] ;
	h ^= g_hashtab [ 2] [ ((unsigned char *)&h1)[2] ] ;
	h ^= g_hashtab [ 3] [ ((unsigned char *)&h1)[3] ] ;
	h ^= g_hashtab [ 4] [ ((unsigned char *)&h1)[4] ] ;
	h ^= g_hashtab [ 5] [ ((unsigned char *)&h1)[5] ] ;
	h ^= g_hashtab [ 6] [ ((unsigned char *)&h1)[6] ] ;
	h ^= g_hashtab [ 7] [ ((unsigned char *)&h1)[7] ] ;

	h ^= g_hashtab [ 8] [ ((unsigned char *)&h2)[0] ] ;
	h ^= g_hashtab [ 9] [ ((unsigned char *)&h2)[1] ] ;
	h ^= g_hashtab [10] [ ((unsigned char *)&h2)[2] ] ;
	h ^= g_hashtab [11] [ ((unsigned char *)&h2)[3] ] ;
	h ^= g_hashtab [12] [ ((unsigned char *)&h2)[4] ] ;
	h ^= g_hashtab [13] [ ((unsigned char *)&h2)[5] ] ;
	h ^= g_hashtab [14] [ ((unsigned char *)&h2)[6] ] ;
	h ^= g_hashtab [15] [ ((unsigned char *)&h2)[7] ] ;
	return h;
}

void hash2string ( unsigned long long h , char *buf ) {
	//	sprintf(buf, "%016lx", h );
	sprintf(buf   , "%08lx", (unsigned long)(h >> 32) );
	sprintf(buf+10, "%08lx", (unsigned long)h );
}

// only utf8 allowed now
uint32_t hash32d ( char *p, char *pend ) {
	return (uint32_t)hash64d ( p , pend - p);
}

// . only utf8 allowed now
// . stole this from hash.h hash64LowerE()
unsigned long long hash64d ( char *p, long plen ) {
	char *pend = p + plen;
	uint64_t h = 0;
	uint8_t  i = 0;
	char     cs = 0;
	for ( ; p < pend ; p += cs ) {
		// get the size
		cs = getUtf8CharSize ( p );
		// deal with one ascii char quickly
		if ( cs == 1 ) {
			// clean it up here
			uint8_t c = getClean_a ( *p );
			h ^= g_hashtab [i++] [c];
			continue;
		}
		// filter it
		UChar32 x = getClean_utf8 ( p );
		// back to utf8
		uint8_t tmp[4];
		char    ncs = utf8Encode ( x , (char *)tmp );
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
	return h;
}

uint8_t getClean_a ( char c ) {
	if ( is_alnum_a ( c ) ) return to_lower_a(c);
	if ( c == '\n'        ) return '\0';
	if ( c == '-'         ) return c;
	if ( c == '\''        ) return c;
	if ( c == '\0'        ) return c;
	return ' ';
}


UChar32 getClean_utf8 ( char *src ) {
	// do ascii fast
	if ( is_ascii ( *src ) ) return (UChar32)getClean_a(*src);
	// otherwise, lower case it
	UChar32 x = utf8Decode(src);
	// convert to upper
	x = ucToLower (x);
	// return if alnum
	if ( ucIsAlnum ( x ) ) return x;
	// everything else is converted to space
	return (UChar32)' ';
}

//
// was inlined in hash.h below here
//

