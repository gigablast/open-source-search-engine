#include "gb-include.h"

#include <sys/time.h>  // gettimeofday()
#include "sort.h"

static int cmp (const void *p1, const void *p2) ;


class fslot1 {
public:
	long            m_score;
	char           *m_docIdBitsPtr;
	unsigned short  m_termBits;
	//unsigned short align;
};

class fslot2 {
public:
	long            m_score;
	long long       m_docIdBits;
	unsigned short  m_termBits;
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

	// . # of docIds to hash
	long nd = 2000000; //8192*4*4;
	// make a list of compressed (6 byte) docIds
        char *docIds1 = (char *) malloc ( 6 * nd );
        char *docIds2 = (char *) malloc ( 6 * nd );
	// print start time
	//fprintf (stderr,"hashtest:: randomizing begin."
	//	 " %li 6-byte docIds.\n",nd);
	// randomize docIds
	unsigned char *p = (unsigned char *)docIds1;
	for ( long i = 0 ; i < nd ; i++ ) {
		*(unsigned long  *)p = rand() | 0x01 ; p += 4;
		*(unsigned short *)p = rand() % 0xffff ; p += 2;
	}
	p = (unsigned char *)docIds2;
	for ( long i = 0 ; i < nd ; i++ ) {
		*(unsigned long  *)p = rand() | 0x01 ; p += 4;
		*(unsigned short *)p = rand() % 0xffff ; p += 2;
	}
	// . make a hash table, small...
	// . numSlots MUST be power of 2
	// . ENSURE this fits into the L1 or L2 cache!!!!!!!!!!!!!!
	// . That is the whole POINT of this!!!!!!!
	// . This seems to run fastest at about 2k, going much less than
	//   that doesn't seem to help much...
#define numSlots 1024*2
#define mask     (numSlots-1)
	//long numSlots = 1024*4;
	fprintf(stderr,"numslots = %lik\n", numSlots/1024);
	fslot1 *slots = (fslot1 *) calloc (sizeof(fslot1), numSlots );
	// point to 6 byte docIds
	p = (unsigned char *)docIds1;
	// set our total end
	unsigned char *max = p + nd*6;
	// only hash enough to fill up our small hash table
	long score;
	long collisions = 0;
	unsigned long n;
	unsigned short termBitMask = 1;
	//unsigned long  mask = numSlots - 1;
	long scoreWeight = 13;
	// debug msg
	fprintf (stderr,"hashtest:: starting loop (nd=%li)\n",nd);
	// time stamp
	long long t   = gettimeofdayInMilliseconds();
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
	for ( long i = 0 ; i < numSlots ; i++ ) 
		slots[i].m_score = 0;
 top:
	if ( p >= pend ) goto done;
	//*(((unsigned char *)(&docIdBits))+0) = p[0];
	//*(((unsigned char *)(&docIdBits))+1) = p[1];
	//*(((unsigned char *)(&docIdBits))+2) = p[2];
	//*(((unsigned char *)(&docIdBits))+3) = p[3];
	//*(((unsigned char *)(&docIdBits))+4) = p[4];
	n = ( (*(unsigned long *)p) ^ g_hashtab[p[0]] ) & mask;
 chain:
	if ( slots[n].m_score == 0 ) { 
		slots[n].m_score        = ~p[5];
		slots[n].m_docIdBitsPtr = (char *)p;
		slots[n].m_termBits     = termBitMask;
		p += 6;
		goto top;
	}
	// if equal, add
	if ( *(long *)p == *slots[n].m_docIdBitsPtr &&
	     p[5] == slots[n].m_docIdBitsPtr[5] ) {
		slots[n].m_score     += ~p[5];
		slots[n].m_termBits  |= termBitMask;
		p += 6;
		goto top;
	}
	// otherwise, chain
	//collisions++;
	if ( ++n >= (unsigned long)numSlots ) n = 0;
	goto chain;

 done:
	// dec loopcount
	goto again;

 finalDone:
	// completed
	long long now = gettimeofdayInMilliseconds();
	fprintf (stderr,"hashtest:: addList took %llu ms\n" , now - t );
	// how many did we hash
	long hashed = (p - (unsigned char *)docIds1) / 6;
	fprintf(stderr,"hashtest:: hashed %li docids\n", hashed);
	// stats
	double d = (1000.0*(double)hashed) / ((double)(now - t));
	fprintf (stderr,"hashtest:: each add took %f cycles\n" ,
		 400000000.0 / d );
	fprintf (stderr,"hashtest:: we can do %li adds per second\n" ,(long)d);
	fprintf (stderr,"hashtest:: collisions = %li\n", collisions);

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
		*(long  *)(&pr->m_docIdBits) = *(long *)p1;
		*(((short *)(&pr->m_docIdBits))+2) = *(short *)(p1+4);
		pr->m_score = ~p1[5];
		pr->m_termBits = 0x01;
		pr++;;
		p1+=6; 
		if ( p1 < pend1 ) goto loop;
		goto done2; 
	}
	if ( p1[5] > p2[5] ) {
		*(long  *)(&pr->m_docIdBits) = *(long *)p2;
		*(((short *)(&pr->m_docIdBits))+2) = *(short *)(p2+4);
		pr->m_score = ~p2[5];
		pr->m_termBits = 0x02;
		pr++;
		p2+=6; 
		if ( p2 < pend2 ) goto loop;
		goto done2; 
	}
	// add together
	*(long  *)(&pr->m_docIdBits) = *(long *)p2;
	*(((short *)(&pr->m_docIdBits))+2) = *(short *)(p2+4);
	pr->m_score = ((long)~p1[5]) + ((long)~p2[5]) ;
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
	fprintf (stderr,"hashtest:: 2 list MERGE took %llu ms\n" , now - t );
	// how many did we hash
	fprintf(stderr,"hashtest:: merged %li docids\n", nd*2);
	// stats
	d = (1000.0*(double)nd*2.0) / ((double)(now - t));
	fprintf (stderr,"hashtest:: each add took %f cycles\n" ,
		 400000000.0 / d );
	fprintf (stderr,"hashtest:: we can do %li adds per second\n" ,(long)d);

	// exit gracefully
	exit ( 0 );
}

int cmp (const void *p1, const void *p2) {


	long long n1 = 0;
	long long n2 = 0;

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
