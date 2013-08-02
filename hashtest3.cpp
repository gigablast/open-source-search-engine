#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()

class fslot {
public:
	long           m_score;
	long long      m_docIdBits;
	unsigned short m_termBits;
	//unsigned short align;
};

static long long gettimeofdayInMilliseconds() ;

long long gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	long long now=(long long)(tv.tv_usec/1000)+((long long)tv.tv_sec)*1000;
	return now;
}


main ( ) {
	// fill our tbl
	unsigned long g_hashtab[256];
	static bool s_initialized = false;
	// bail if we already called this
	if ( s_initialized ) return true;
	// show RAND_MAX
	//printf("RAND_MAX = %lu\n", RAND_MAX ); it's 0x7fffffff
	// seed with same value so we get same rand sequence for all
	srand ( 1945687 );
	for ( long i = 0 ; i < 256 ; i++ )
			g_hashtab [i]  = (unsigned long)rand();
			/*
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			g_hashtab [i][j] <<= 32;
			g_hashtab [i][j] |= (unsigned long long)rand();
			// the top bit never gets set, so fix
			if ( rand() > (0x7fffffff / 2) ) 
				g_hashtab[i][j] |= 0x80000000;
			*/
	//if ( g_hashtab[0][0] != 6720717044602784129LL ) exit(-1);


	// # of docIds to hash
	long nd = 300000;
	// make a list of compressed (6 byte) docIds
        char *docIds = (char *) malloc ( 6 * nd );
	// store radnom docIds in this list
	unsigned char *p = (unsigned char *)docIds;
	// print start time
	fprintf (stderr,"hashtest:: randomizing begin."
		 " %li 6-byte docIds.\n",nd);
	// space em out 1 million to simulate suburl:com
	long long count = 1000000;
	// random docIds
	for ( long i = 0 ; i < nd ; i++ ) {
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
		// . the lower long
		// . set lower bit so it's not a delete (delbit)
		*(unsigned long  *)p = rand() | 0x01 ;
		// skip 
		p += 4;
		// . the upper 2 bytes
		// . top most byte is the 8-bit score
		*(unsigned short *)p = rand() % 0xffff ;
		// skip
		p += 2;
	}
	// make a hash table
	long numSlots = 1048576 /2; // about 300,000 * 3 rounded up to 2power
	fslot *slots = (fslot *) calloc (sizeof(fslot), numSlots );
	// set all scores to 0
	//for ( long i = 0 ; i < numSlots ; i++ )
	//	m_sc
	// point to 6 byte docIds
	unsigned char *end = p;
	p   = (unsigned char *)docIds;
	long score;
	long score2;
	long score3;
	long score4;
	long score5;
	long score6;
	long score7;
	long score8;
	long collisions = 0;
	unsigned long n;
	unsigned long n2;
	unsigned long n3;
	unsigned long n4;
	unsigned long n5;
	unsigned long n6;
	unsigned long n7;
	unsigned long n8;
	long long docIdBits;
	long long docIdBits2;
	long long docIdBits3;
	long long docIdBits4;
	long long docIdBits5;
	long long docIdBits6;
	long long docIdBits7;
	long long docIdBits8;
	unsigned short termBitMask = 1;
	unsigned long  mask = numSlots - 1;
	long scoreWeight = 13;
	// debug msg
	fprintf (stderr,"hashtest::starting loop\n");
	// time stamp
	long long t   = gettimeofdayInMilliseconds();
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
		if ( ++n >= (unsigned long)numSlots ) n = 0;
		goto chain;
	}


	// completed
	long long now = gettimeofdayInMilliseconds();
	fprintf (stderr,"hashtest:: addList took %llu ms\n" , now - t );
	// stats
	double d = (1000.0*(double)nd) / ((double)(now - t));
	fprintf (stderr,"hashtest:: each add took %f cycles\n" ,
		 400000000.0 / d );
	fprintf (stderr,"hashtest:: we can do %li adds per second\n" ,(long)d);
	fprintf (stderr,"hashtest:: collisions = %li\n", collisions);
	// exit gracefully
	exit ( 0 );
}
