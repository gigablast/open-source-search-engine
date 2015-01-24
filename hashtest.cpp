#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()
#include "sort.h"

static int cmp (const void *p1, const void *p2) ;


class fslot1 {
public:
	int32_t            m_score;
	char           *m_docIdBitsPtr;
	uint16_t  m_termBits;
	//uint16_t align;
};

class fslot2 {
public:
	int32_t            m_score;
	int64_t       m_docIdBits;
	uint16_t  m_termBits;
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

	// . # of docIds to hash
	int32_t nd = 2000000; //8192*4*4;
	// make a list of compressed (6 byte) docIds
        char *docIds1 = (char *) malloc ( 6 * nd );
        char *docIds2 = (char *) malloc ( 6 * nd );
	// print start time
	//fprintf (stderr,"hashtest:: randomizing begin."
	//	 " %"INT32" 6-byte docIds.\n",nd);
	// randomize docIds
	unsigned char *p = (unsigned char *)docIds1;
	for ( int32_t i = 0 ; i < nd ; i++ ) {
		*(uint32_t  *)p = rand() | 0x01 ; p += 4;
		*(uint16_t *)p = rand() % 0xffff ; p += 2;
	}
	p = (unsigned char *)docIds2;
	for ( int32_t i = 0 ; i < nd ; i++ ) {
		*(uint32_t  *)p = rand() | 0x01 ; p += 4;
		*(uint16_t *)p = rand() % 0xffff ; p += 2;
	}
	// . make a hash table, small...
	// . numSlots MUST be power of 2
	// . ENSURE this fits into the L1 or L2 cache!!!!!!!!!!!!!!
	// . That is the whole POINT of this!!!!!!!
	// . This seems to run fastest at about 2k, going much less than
	//   that doesn't seem to help much...
#define numSlots 1024*2
#define mask     (numSlots-1)
	//int32_t numSlots = 1024*4;
	fprintf(stderr,"numslots = %"INT32"k\n", numSlots/1024);
	fslot1 *slots = (fslot1 *) calloc (sizeof(fslot1), numSlots );
	// point to 6 byte docIds
	p = (unsigned char *)docIds1;
	// set our total end
	unsigned char *max = p + nd*6;
	// only hash enough to fill up our small hash table
	int32_t score;
	int32_t collisions = 0;
	uint32_t n;
	uint16_t termBitMask = 1;
	//uint32_t  mask = numSlots - 1;
	int32_t scoreWeight = 13;
	// debug msg
	fprintf (stderr,"hashtest:: starting loop (nd=%"INT32")\n",nd);
	// time stamp
	int64_t t   = gettimeofdayInMilliseconds();
 again:
	// advance the ending point before clearing the hash table
	unsigned char *pend = p + (numSlots>>1)*6;
	if ( pend >= max ) pend = max;
	if ( p    >= max ) goto finalDone;
	// skip clear of table if we don't need to do it
	if ( p == (unsigned char *)docIds1 ) goto top;
	// otherwise msg
	//fprintf(stderr,"looping\n");
	// empty hash table
	for ( int32_t i = 0 ; i < numSlots ; i++ ) 
		slots[i].m_score = 0;
 top:
	if ( p >= pend ) goto done;
	//*(((unsigned char *)(&docIdBits))+0) = p[0];
	//*(((unsigned char *)(&docIdBits))+1) = p[1];
	//*(((unsigned char *)(&docIdBits))+2) = p[2];
	//*(((unsigned char *)(&docIdBits))+3) = p[3];
	//*(((unsigned char *)(&docIdBits))+4) = p[4];
	n = ( (*(uint32_t *)p) ^ g_hashtab[p[0]] ) & mask;
 chain:
	if ( slots[n].m_score == 0 ) { 
		slots[n].m_score        = ~p[5];
		slots[n].m_docIdBitsPtr = (char *)p;
		slots[n].m_termBits     = termBitMask;
		p += 6;
		goto top;
	}
	// if equal, add
	if ( *(int32_t *)p == *slots[n].m_docIdBitsPtr &&
	     p[5] == slots[n].m_docIdBitsPtr[5] ) {
		slots[n].m_score     += ~p[5];
		slots[n].m_termBits  |= termBitMask;
		p += 6;
		goto top;
	}
	// otherwise, chain
	//collisions++;
	if ( ++n >= (uint32_t)numSlots ) n = 0;
	goto chain;

 done:
	// dec loopcount
	goto again;

 finalDone:
	// completed
	int64_t now = gettimeofdayInMilliseconds();
	fprintf (stderr,"hashtest:: addList took %"UINT64" ms\n" , now - t );
	// how many did we hash
	int32_t hashed = (p - (unsigned char *)docIds1) / 6;
	fprintf(stderr,"hashtest:: hashed %"INT32" docids\n", hashed);
	// stats
	double d = (1000.0*(double)hashed) / ((double)(now - t));
	fprintf (stderr,"hashtest:: each add took %f cycles\n" ,
		 400000000.0 / d );
	fprintf (stderr,"hashtest:: we can do %"INT32" adds per second\n" ,(int32_t)d);
	fprintf (stderr,"hashtest:: collisions = %"INT32"\n", collisions);

	//////////////////////////////////////////////
	//////////////////////////////////////////////
	// now do merge test of the lists
	//////////////////////////////////////////////
	//////////////////////////////////////////////

	// sort docIds1 and docIds2
	fprintf(stderr,"hashtest:: sorting docIds1 and docIds2 for merge\n");
	gbsort ( docIds1 , nd , 6 , cmp );
	gbsort ( docIds2 , nd , 6 , cmp );

	// current ptrs
	unsigned char *p1 = (unsigned char *)docIds1;
	unsigned char *p2 = (unsigned char *)docIds2;

	unsigned char *pend1 = (unsigned char *)(docIds1 + nd * 6);
	unsigned char *pend2 = (unsigned char *)(docIds2 + nd * 6);

	// list to hold them
	//char *results = (char *) malloc ( nd * 6 * 2 );
	//char *pr = results ;

	t = gettimeofdayInMilliseconds();

	// pt to slots
	fslot2 *pr = (fslot2 *) calloc (sizeof(fslot2), nd * 2 );
 loop:
	// now merge the two lists sorted by docId
	if ( p1[5] < p2[5] ) { 
		*(int32_t  *)(&pr->m_docIdBits) = *(int32_t *)p1;
		*(((int16_t *)(&pr->m_docIdBits))+2) = *(int16_t *)(p1+4);
		pr->m_score = ~p1[5];
		pr->m_termBits = 0x01;
		pr++;;
		p1+=6; 
		if ( p1 < pend1 ) goto loop;
		goto done2; 
	}
	if ( p1[5] > p2[5] ) {
		*(int32_t  *)(&pr->m_docIdBits) = *(int32_t *)p2;
		*(((int16_t *)(&pr->m_docIdBits))+2) = *(int16_t *)(p2+4);
		pr->m_score = ~p2[5];
		pr->m_termBits = 0x02;
		pr++;
		p2+=6; 
		if ( p2 < pend2 ) goto loop;
		goto done2; 
	}
	// add together
	*(int32_t  *)(&pr->m_docIdBits) = *(int32_t *)p2;
	*(((int16_t *)(&pr->m_docIdBits))+2) = *(int16_t *)(p2+4);
	pr->m_score = ((int32_t)~p1[5]) + ((int32_t)~p2[5]) ;
	pr->m_termBits = 0x01 | 0x02;
	pr++;
	p2 += 6;
	p1 += 6;
	if ( p2 >= pend2 ) goto done2;
	if ( p1 >= pend1 ) goto done2;
	goto loop;

 done2:

	// completed
	now = gettimeofdayInMilliseconds();
	fprintf (stderr,"hashtest:: 2 list MERGE took %"UINT64" ms\n" , now - t );
	// how many did we hash
	fprintf(stderr,"hashtest:: merged %"INT32" docids\n", nd*2);
	// stats
	d = (1000.0*(double)nd*2.0) / ((double)(now - t));
	fprintf (stderr,"hashtest:: each add took %f cycles\n" ,
		 400000000.0 / d );
	fprintf (stderr,"hashtest:: we can do %"INT32" adds per second\n" ,(int32_t)d);

	// exit gracefully
	exit ( 0 );
}

int cmp (const void *p1, const void *p2) {


	int64_t n1 = 0;
	int64_t n2 = 0;

	*(((unsigned char *)(&n1))+0) = *(((char *)p1)+0);
	*(((unsigned char *)(&n1))+1) = *(((char *)p1)+1);
	*(((unsigned char *)(&n1))+2) = *(((char *)p1)+2);
	*(((unsigned char *)(&n1))+3) = *(((char *)p1)+3);
	*(((unsigned char *)(&n1))+4) = *(((char *)p1)+4);

	*(((unsigned char *)(&n2))+0) = *(((char *)p2)+0);
	*(((unsigned char *)(&n2))+1) = *(((char *)p2)+1);
	*(((unsigned char *)(&n2))+2) = *(((char *)p2)+2);
	*(((unsigned char *)(&n2))+3) = *(((char *)p2)+3);
	*(((unsigned char *)(&n2))+4) = *(((char *)p2)+4);

	return n1 - n2;
}
