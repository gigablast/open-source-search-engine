#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()

class fslot {
public:
	int32_t           m_score;
	int64_t      m_docIdBits;
	uint16_t m_termBits;
	//uint16_t align;
};

static int64_t gettimeofdayInMilliseconds() ;

int64_t gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	return now;
}


main ( ) {
	fprintf(stderr,"sizeof fslot=%"INT32"\n",-sizeof(fslot));
	// fill our tbl
	uint32_t g_hashtab[256];
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;
	// show RAND_MAX
	//printf("RAND_MAX = %"UINT32"\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( int32_t i = 0 ; i < 256 ; i++ )
			g_hashtab [i]  = (uint32_t)rand();
			/*
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (uint64_t)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			*/
	//if ( g_hashtab[0][0] != 6720717044602784129LL ) exit(-1);


	// # of docIds to hash
	int32_t nd = 300000;
	// make a list of compressed (6 byte) docIds
        char *docIds = (char *) malloc ( 6 * nd );
	// store radnom docIds in this list
	unsigned char *p = (unsigned char *)docIds;
	// print start time
	fprintf (stderr,"hashtest:: randomizing begin."
		 " %"INT32" 6-byte docIds.\n",nd);
	// space em out 1 million to simulate suburl:com
	int64_t count = 1000000;
	// random docIds
	for ( int32_t i = 0 ; i < nd ; i++ ) {
		/*
		p[0] = ((unsigned char *)&count)[0];
		p[1] = ((unsigned char *)&count)[1];
		p[2] = ((unsigned char *)&count)[2];
		p[3] = ((unsigned char *)&count)[3];
		p[4] = ((unsigned char *)&count)[4];
		p[5] = ((unsigned char *)&count)[5];
		count += 1000000; // 1048576; 
		p += 6;
		continue;
		*/
		// . the lower int32_t
		// . set lower bit so it's not a delete (delbit)
		*(uint32_t  *)p = rand() | 0x01 ;
		// skip 
		p += 4;
		// . the upper 2 bytes
		// . top most byte is the 8-bit score
		*(uint16_t *)p = rand() % 0xffff ;
		// skip
		p += 2;
	}
	// make a hash table
	int32_t numSlots = 1048576 ; // about 300,000 * 3 rounded up to 2power
	fslot *slots = (fslot *) calloc (sizeof(fslot), numSlots );
	// set all scores to 0
	//for ( int32_t i = 0 ; i < numSlots ; i++ )
	//	m_sc

	// we can only have 6 outstanding prefetches on the athlon,
	// and only 1 on the k6
	#define PREFETCHES 3
	// point to 6 byte docIds
	unsigned char *end = p;
	p   = (unsigned char *)docIds;
	int32_t score[PREFETCHES];
	int32_t collisions = 0;
	uint32_t n[PREFETCHES];
	int64_t docIdBits[PREFETCHES];
	uint16_t termBitMask = 1;
	uint32_t  mask = numSlots - 1;
	int32_t scoreWeight = 13;
	// debug msg
	fprintf (stderr,"hashtest::starting loop\n");
	// time stamp
	int64_t t   = gettimeofdayInMilliseconds();
	// tell the memory cache to only bring in 16 bytes at a time
	// since our fslot class is 16 bytes big

	// hash em'
	unsigned char c;
	int32_t j , k;
	unsigned char *r , *rend;
	fslot *f;
	fslot *s;

	//rend = p + PREFETCHES * 6;
	//for ( r = p ; r < rend ; r += 64 )
	//	__asm__ __volatile__ ("prefetch (%0)" : : "r"(r));

	// now prefetch loop
	for ( j = 0 ; j < PREFETCHES && p < end ; j++ , p += 6) {
		score[j] = 255 - *(p+5);
		c = *p;
		if ( ( c & (unsigned char)0x01 ) == 0 ) 
			score[j] = -score[j];
		*(((unsigned char *)(&docIdBits[j]))+0) = c;
		*(((unsigned char *)(&docIdBits[j]))+1) = *(p+1);
		*(((unsigned char *)(&docIdBits[j]))+2) = *(p+2);
		*(((unsigned char *)(&docIdBits[j]))+3) = *(p+3);
		*(((unsigned char *)(&docIdBits[j]))+4) = *(p+4);
		// hash
		n[j] = (docIdBits[j] ^ g_hashtab[c] ) & mask;
		// prefetch in hash table
		__asm__ __volatile__ ("prefetchw (%0)" : : "r"(&slots[n[j]]));
	}

	// prefetch one for next time
	//__asm__ __volatile__ ("prefetch (%0)" : : "r"(p));

 top:
	// now chain them
	for ( k = 0 ; k < j ; k++ ) {
		// chain
	chain:
		s = &slots[n[k]];
		if ( s->m_score == 0 ) { 
			s->m_score      = score[k];
			s->m_docIdBits  = docIdBits[k];
			s->m_termBits   = termBitMask;
			// jump here to prefetch next slot right away
		set:
			// set next docIdBits so we can do prefetch
			score[k] = 255 - *(p+5);
			c = *p;
			if ( ( c & (unsigned char)0x01 ) == 0 ) 
				score[k] = -score[k];
			*(((unsigned char *)(&docIdBits[k]))+0) = c;
			*(((unsigned char *)(&docIdBits[k]))+1) = *(p+1);
			*(((unsigned char *)(&docIdBits[k]))+2) = *(p+2);
			*(((unsigned char *)(&docIdBits[k]))+3) = *(p+3);
			*(((unsigned char *)(&docIdBits[k]))+4) = *(p+4);
			// hash
			n[k] = (docIdBits[k] ^ g_hashtab[c] ) & mask;
			// prefetch in hash table
			__asm__ __volatile__ ("prefetchw (%0)" : : 
					      "r"(&slots[n[k]]));
			// point to next p
			p += 6;
			// find next slot
			continue;
		}
		if ( s->m_docIdBits == docIdBits[k] ) {
			s->m_score     += score[k];
			s->m_termBits  |= termBitMask;
			goto set;
		}
		//collisions++;
		if ( ++n[k] >= (uint32_t)numSlots ) n[k] = 0;
		goto chain;
	}

	// loop back
	if ( p < end ) goto top;

	// completed
	int64_t now = gettimeofdayInMilliseconds();
	fprintf (stderr,"hashtest:: addList took %"UINT64" ms\n" , now - t );
	// stats
	double d = (1000.0*(double)nd) / ((double)(now - t));
	fprintf (stderr,"hashtest:: each add took %f cycles\n" ,
		 400000000.0 / d );
	fprintf (stderr,"hashtest:: we can do %"INT32" adds per second\n" ,(int32_t)d);
	fprintf (stderr,"hashtest:: collisions = %"INT32"\n", collisions);
	// exit gracefully
	exit ( 0 );
}
