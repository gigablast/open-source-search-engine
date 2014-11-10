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
	int32_t numSlots = 1048576 /2; // about 300,000 * 3 rounded up to 2power
	fslot *slots = (fslot *) calloc (sizeof(fslot), numSlots );
	// set all scores to 0
	//for ( int32_t i = 0 ; i < numSlots ; i++ )
	//	m_sc
	// point to 6 byte docIds
	unsigned char *end = p;
	p   = (unsigned char *)docIds;
	int32_t score;
	int32_t score2;
	int32_t score3;
	int32_t score4;
	int32_t score5;
	int32_t score6;
	int32_t score7;
	int32_t score8;
	int32_t collisions = 0;
	uint32_t n;
	uint32_t n2;
	uint32_t n3;
	uint32_t n4;
	uint32_t n5;
	uint32_t n6;
	uint32_t n7;
	uint32_t n8;
	int64_t docIdBits;
	int64_t docIdBits2;
	int64_t docIdBits3;
	int64_t docIdBits4;
	int64_t docIdBits5;
	int64_t docIdBits6;
	int64_t docIdBits7;
	int64_t docIdBits8;
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
	for ( ; p < end ; p += 6 ) {

		score = 255 - *(p+5);
		c = *p;
		if ( ( c & (unsigned char)0x01 ) == 0 ) score = -score;
		*(((unsigned char *)(&docIdBits))+0) = c;
		*(((unsigned char *)(&docIdBits))+1) = *(p+1);
		*(((unsigned char *)(&docIdBits))+2) = *(p+2);
		*(((unsigned char *)(&docIdBits))+3) = *(p+3);
		*(((unsigned char *)(&docIdBits))+4) = *(p+4);
		// hash
		n = (docIdBits ^ g_hashtab[c] ) & mask;

		// chain
	chain:
		if ( slots[n].m_score == 0 ) { 
			slots[n].m_score      = score;
			slots[n].m_docIdBits  = docIdBits;
			slots[n].m_termBits   = termBitMask;
			continue;
		}
		if ( slots[n].m_docIdBits == docIdBits ) {
			slots[n].m_score     += score;
			slots[n].m_termBits  |= termBitMask;
			continue;
		}
		//collisions++;
		if ( ++n >= (uint32_t)numSlots ) n = 0;
		goto chain;
	}


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
