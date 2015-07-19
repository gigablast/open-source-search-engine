#include "gb-include.h"

uint64_t g_hashtab[256][256] ;

// . now we explicitly specify the zobrist table so we are compatible
//   with cygwin and apple environments
// . no, let's just define the rand2() function to be compatible then
//#include "hashtab.cpp"

// . used for computing zobrist hash of a string up to 256 chars int32_t
// . first array component is the max length, 256, of the string
bool hashinit () {
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;

	// show RAND_MAX
	//printf("RAND_MAX = %"UINT32"\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );

	//if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;
	//s_initialized = true;
	//return true;

	//fprintf(stdout,"g_hashtab[256][256]={\n");
	for ( int32_t i = 0 ; i < 256 ; i++ ) {
		//fprintf(stdout,"{");
		for ( int32_t j = 0 ; j < 256 ; j++ ) {
			g_hashtab [i][j]  = (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			// fixes for cygwin/apple
			//fprintf(stdout,"%"UINT64"ULL",g_hashtab[i][j]);
			//if ( j+1<256 ) fprintf(stdout,",");
		}
		//fprintf(stdout,"},\n");
	}
	//fprintf(stdout,"};\n");
	//fflush ( stdout );

	if ( g_hashtab[0][0] != 6720717044602784129LL ) return false;

	s_initialized = true;
	return true;
}

// TODO: ensure this wraps over properly
unsigned char hash8 ( char *s , int32_t len ) {
	unsigned char h = 0;
	register int32_t i = 0;
	while ( i < len ) {
		h ^= (unsigned char) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

uint16_t hash16 ( char *s , int32_t len ) {
	uint16_t h = 0;
	register int32_t i = 0;
	while ( i < len ) {
		h ^= (uint16_t) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

uint32_t hash32n ( char *s ) {
	uint32_t h = 0;
	register int32_t i = 0;
	while ( s[i] ) {
		h ^= (uint32_t) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

uint64_t hash64n ( char *s, uint64_t startHash ) {
	uint64_t h = startHash;
	for ( register int32_t i = 0 ; s[i] ; i++ )
		h ^= g_hashtab [(unsigned char)i] [(unsigned char)s[i]];
	return h;
}

uint64_t hash64n_nospaces ( char *s, int32_t len ) {
	uint64_t h = 0LL;
	int32_t k = 0;
	for ( register int32_t i = 0 ; i<len ; i++ ) {
		if ( s[i] == ' ' ) continue;
		h ^= g_hashtab [(unsigned char)k] [(unsigned char)s[i]];
		k++;
	}
	return h;
}

uint32_t hash32 ( char *s, int32_t len, uint32_t startHash ) {
	uint32_t h = startHash;
	register int32_t i = 0;
	while ( i < len ) {
		h ^= (uint32_t) g_hashtab [(unsigned char)i]
			[(unsigned char)s[i]];
		i++;
	}
	return h;
}

uint32_t hash32Lower_a ( char *s,int32_t len,uint32_t startHash){
	uint32_t h = startHash;
	register int32_t i = 0;
	while ( i < len ) {
		h ^= (uint32_t) g_hashtab [(unsigned char)i] 
			[(unsigned char)to_lower_a(s[i])];
		i++;
	}
	return h;
}

u_int96_t hash96 ( char *s, int32_t slen, u_int96_t startHash ) {
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

u_int96_t hash96 ( int32_t h1 ,  u_int96_t h2 ) {
	h2.n0 = hash64  ( h1 , h2.n0 );
	h2.n1 = hash32h ( h1 , h2.n1 );
	return h2;
}

u_int128_t hash128 ( u_int128_t h1 ,  u_int128_t h2 ) {
	h1.n0 = hash64 ( h1.n0 , h2.n0 );
	h1.n1 = hash64 ( h1.n1 , h2.n1 );
	return h1;
}

u_int128_t hash128 ( int32_t h1 ,  u_int128_t h2 ) {
	h2.n0 = hash64 ( h1 , h2.n0 );
	h2.n1 = hash64 ( h1 , h2.n1 );
	return h2;
}

// . combine 2 hashes into 1
// . TODO: ensure this is a good way
// . used for combining words' hashes into phrases (also fields,collections)..
uint32_t hash32h ( uint32_t h1 , uint32_t h2 ) {
	// treat the 16 bytes as a string now instead of multiplying them
	uint32_t h = 0;
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

void hash2string ( uint64_t h , char *buf ) {
	//	sprintf(buf, "%016lx", h );
	sprintf(buf   , "%08"PRIX32, (uint32_t)(h >> 32) );
	sprintf(buf+10, "%08"PRIX32, (uint32_t)h );
}

// only utf8 allowed now
uint32_t hash32d ( char *p, char *pend ) {
	return (uint32_t)hash64d ( p , pend - p);
}

// . only utf8 allowed now
// . stole this from hash.h hash64LowerE()
uint64_t hash64d ( char *p, int32_t plen ) {
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

