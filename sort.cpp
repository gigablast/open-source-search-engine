#include "gb-include.h"


#include "sort.h"

// uncomment this to test with the included main(), below...
//#define JAB_TEST

#ifndef JAB_TEST

#include "Mem.h"
#include "Loop.h"

#else

// quickpoll debug code...
/*
static int32_t QPC = 0;
void QUICKPOLL(int n) {
	// shut up compiler...
	n = n;
	QPC++;
	//fprintf(stderr, "QP: %d\n", QPC);
}
*/
#define mmalloc(n,s) malloc(n)
#define mfree(p,n,s) free(p)

#endif

/*
#define GBSORTQP(mask)				\
	{					\
		qp_count++; 			\
		if (!(qp_count & mask))		\
			QUICKPOLL(niceness); 	\
	}
*/

/*	$OpenBSD: qsort.c,v 1.10 2005/08/08 08:05:37 espie Exp $ */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

// JAB: duplicate includes
//#include <sys/types.h>
//#include <stdlib.h>

static __inline char	*med3(char *, char *, char *, int (*)(const void *, const void *));
static __inline void	 swapfunc(char *, char *, int, int);

#define min(a, b)	(a) < (b) ? a : b

/*
 * Qsort routine from Bentley & McIlroy's "Engineering a Sort Function".
 */
#define swapcode(TYPE, parmi, parmj, n) { 		\
	int32_t i = (n) / sizeof (TYPE); 			\
	TYPE *pi = (TYPE *) (parmi); 			\
	TYPE *pj = (TYPE *) (parmj); 			\
	do { 						\
		TYPE	t = *pi;			\
		*pi++ = *pj;				\
		*pj++ = t;				\
        } while (--i > 0);				\
}

#define SWAPINIT(a, es) swaptype = ((char *)a - (char *)0) % sizeof(int32_t) || \
	es % sizeof(int32_t) ? 2 : es == sizeof(int32_t)? 0 : 1;

static __inline void
swapfunc(char *a, char *b, int n, int swaptype)
{
	if (swaptype <= 1) 
		swapcode(int32_t, a, b, n)
	else
		swapcode(char, a, b, n)
}

// JAB: namespace collision with mergesort
//#define swap(a, b)
#define qsort_swap(a, b)				\
	if (swaptype == 0) {				\
		int32_t t = *(int32_t *)(a);			\
		*(int32_t *)(a) = *(int32_t *)(b);		\
		*(int32_t *)(b) = t;			\
	} else						\
		swapfunc(a, b, es, swaptype)

#define vecswap(a, b, n) 	if ((n) > 0) swapfunc(a, b, n, swaptype)

static __inline char *
med3(char *a, char *b, char *c, int (*cmp)(const void *, const void *))
{
	return cmp(a, b) < 0 ?
	       (cmp(b, c) < 0 ? b : (cmp(a, c) < 0 ? c : a ))
              :(cmp(b, c) > 0 ? b : (cmp(a, c) < 0 ? a : c ));
}

void
// JAB: avoid namespace collision with stdlib
//qsort(void *aa, size_t n, size_t es, int (*cmp)(const void *, const void *))
// JAB: niceness/quickpoll
//gbqsort(void *aa, size_t n, size_t es, int (*cmp)(const void *, const void *))
gbqsort(	void*	aa,
		size_t	n,
		size_t	es,
		int	(*cmp)(const void *, const void *),
		int	niceness)
{
	// JAB: quickpoll counter
	//int qp_count = 0;
	char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
	int d, r, swaptype, swap_cnt;
	// JAB: cast required
	//char *a = aa;
	char *a = (char*) aa;

	// JAB: int16_t-circuit if no action required
	if (n < 1) {
		return;
	}

loop:	SWAPINIT(a, es);
	swap_cnt = 0;
	if (n < 7) {
		for (pm = (char *)a + es; pm < (char *) a + n * es; pm += es)
			for (pl = pm; pl > (char *) a && cmp(pl - es, pl) > 0;
			     pl -= es)
				qsort_swap(pl, pl - es);
		return;
	}
	pm = (char *)a + (n / 2) * es;
	if (n > 7) {
		pl = (char *)a;
		pn = (char *)a + (n - 1) * es;
		if (n > 40) {
			d = (n / 8) * es;
			pl = med3(pl, pl + d, pl + 2 * d, cmp);
			pm = med3(pm - d, pm, pm + d, cmp);
			pn = med3(pn - 2 * d, pn - d, pn, cmp);
		}
		pm = med3(pl, pm, pn, cmp);
	}
	qsort_swap(a, pm);
	pa = pb = (char *)a + es;

	pc = pd = (char *)a + (n - 1) * es;
	for (;;) {
		while (pb <= pc && (r = cmp(pb, a)) <= 0) {
			if (r == 0) {
				swap_cnt = 1;
				qsort_swap(pa, pb);
				pa += es;
			}
			pb += es;
			// JAB: quickpoll
			//GBSORTQP(0x7FF);
			QUICKPOLL(niceness);
		}
		while (pb <= pc && (r = cmp(pc, a)) >= 0) {
			if (r == 0) {
				swap_cnt = 1;
				qsort_swap(pc, pd);
				pd -= es;
			}
			pc -= es;
			// JAB: quickpoll
			//GBSORTQP(0x7FF);
			QUICKPOLL(niceness);
		}
		if (pb > pc)
			break;
		qsort_swap(pb, pc);
		swap_cnt = 1;
		pb += es;
		pc -= es;
	}
	if (swap_cnt == 0) {  /* Switch to insertion sort */
		for (pm = (char *) a + es; pm < (char *) a + n * es; pm += es)
			for (pl = pm; pl > (char *) a && cmp(pl - es, pl) > 0; 
			     pl -= es)
				qsort_swap(pl, pl - es);
		return;
	}

	pn = (char *)a + n * es;
	r = min(pa - (char *)a, pb - pa);
	vecswap(a, pb - r, r);
	// JAB: cast required
	//r = min(pd - pc, pn - pd - es);
	r = min((int) (pd - pc), (int) (pn - pd - es));
	vecswap(pb, pn - r, r);
	// JAB: cast required
	//if ((r = pb - pa) > es)
	if ((r = pb - pa) > (int) es)
		qsort(a, r / es, es, cmp);
	// JAB: cast required
	//if ((r = pd - pc) > es) { 
	if ((r = pd - pc) > (int) es) { 
		/* Iterate rather than recurse to save stack space */
		a = pn - r;
		n = r / es;
		goto loop;
	}
/*		qsort(pn - r, r / es, es, cmp);*/
}

/*	$OpenBSD: merge.c,v 1.8 2005/08/08 08:05:37 espie Exp $ */
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Peter McIlroy.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Hybrid exponential search/linear search merge sort with hybrid
 * natural/pairwise first pass.  Requires about .3% more comparisons
 * for random data than LSMS with pairwise first pass alone.
 * It works for objects as small as two bytes.
 */

#define NATURAL
#define THRESHOLD 16	/* Best choice for natural merge cut-off. */

/* #define NATURAL to get hybrid natural merge.
 * (The default is pairwise merging.)
 */

// JAB: duplicate includes
//#include <sys/types.h>

//#include <errno.h>
//#include <stdlib.h>
//#include <string.h>

// JAB: proper typing
//static void setup(u_char *, u_char *, size_t, size_t, int (*)());
static void setup(u_char *, u_char *, size_t, size_t,
			int (*)(const void*, const void*));
//static void insertionsort(u_char *, size_t, size_t, int (*)());
static void insertionsort(u_char *, size_t, size_t,
			int (*)(const void*, const void *));

#define ISIZE sizeof(int)
#define PSIZE sizeof(u_char *)
#define ICOPY_LIST(src, dst, last)				\
	do							\
	*(int*)dst = *(int*)src, src += ISIZE, dst += ISIZE;	\
	while(src < last)
#define ICOPY_ELT(src, dst, i)					\
	do							\
	*(int*) dst = *(int*) src, src += ISIZE, dst += ISIZE;	\
	while (i -= ISIZE)

#define CCOPY_LIST(src, dst, last)		\
	do					\
		*dst++ = *src++;		\
	while (src < last)
#define CCOPY_ELT(src, dst, i)			\
	do					\
		*dst++ = *src++;		\
	while (i -= 1)
		
/*
 * Find the next possible pointer head.  (Trickery for forcing an array
 * to do double duty as a linked list when objects do not align with word
 * boundaries.
 */
/* Assumption: PSIZE is a power of 2. */
#define EVAL(p) (u_char **)						\
	((u_char *)0 +							\
	    (((u_char *)p + PSIZE - 1 - (u_char *) 0) & ~(PSIZE - 1)))

/*
 * Arguments are as for qsort.
 */
// JAB: return void now
//int
void
// JAB: avoid namespace collision with stdlib on certain OS platforms
//mergesort(void *base, size_t nmemb, size_t size,
// JAB: niceness/quickpoll
//gbmergesort(void *base, size_t nmemb, size_t size,
//    int (*cmp)(const void *, const void *))
gbmergesort(	void*	base,
		size_t	nmemb,
		size_t	size,
		int	(*cmp)(const void *, const void *),
		int	niceness,
		char*   bufSpace, int32_t bufSpaceSize) {
	// JAB: quickpoll
	//int qp_count = 0;
	int i, sense;
	int big, iflag;
	// JAB: track size malloc'd
	int mallocsize;
	u_char *f1, *f2, *t, *b, *tp2, *q, *l1, *l2;
	u_char *list2, *list1, *p2, *p, *last, **p1;


	if (size < PSIZE / 2) {		/* Pointers must fit into 2 * size. */
		// JAB: fall back on gbqsort()
		//errno = EINVAL;
		//return (-1);
		return gbqsort(base, nmemb, size, cmp);
	}

	// JAB: zero members was causing this to CRASH!
	if (nmemb < 1) {
		return;
	}

	/*
	 * XXX
	 * Stupid subtraction for the Cray.
	 */
	iflag = 0;
	if (!(size % ISIZE) && !(((char *)base - (char *)0) % ISIZE))
		iflag = 1;

	// JAB: cast required
	//if ((list2 = malloc(nmemb * size + PSIZE)) == NULL)
	// JAB: convert to mmalloc
	//if ((list2 = (u_char*) malloc(nmemb * size + PSIZE)) == NULL)
	mallocsize = nmemb * size + PSIZE;
	if(bufSpace) {
		if(bufSpaceSize < mallocsize) {
			char *xx = NULL; *xx = 0;
		}
		list2 = (u_char*)bufSpace;
	}
	else list2 = (u_char*) mmalloc(mallocsize, "gbmergesort");
	if (list2 == NULL) {
		// JAB: instead, fall back on gbqsort()
		//return (-1);
		return gbqsort(base, nmemb, size, cmp);
	}

	// JAB: cast required
	//list1 = base;
	list1 = (u_char*) base;
	setup(list1, list2, nmemb, size, cmp);
	last = list2 + nmemb * size;
	i = big = 0;
	while (*EVAL(list2) != last) {
	    l2 = list1;
	    p1 = EVAL(list1);
	    for (tp2 = p2 = list2; p2 != last; p1 = EVAL(l2)) {
	    	p2 = *EVAL(p2);
	    	f1 = l2;
	    	f2 = l1 = list1 + (p2 - list2);
	    	if (p2 != last)
	    		p2 = *EVAL(p2);
	    	l2 = list1 + (p2 - list2);
	    	while (f1 < l1 && f2 < l2) {
			// JAB: quickpoll
			//GBSORTQP(0x1FFF);
			QUICKPOLL(niceness);
	    		if ((*cmp)(f1, f2) <= 0) {
	    			q = f2;
	    			b = f1, t = l1;
	    			sense = -1;
	    		} else {
	    			q = f1;
	    			b = f2, t = l2;
	    			sense = 0;
	    		}
	    		if (!big) {	/* here i = 0 */
	    			while ((b += size) < t && cmp(q, b) >sense)
	    				if (++i == 6) {
	    					big = 1;
	    					goto EXPONENTIAL;
	    				}
	    		} else {
EXPONENTIAL:	    		for (i = size; ; i <<= 1)
	    				if ((p = (b + i)) >= t) {
	    					if ((p = t - size) > b &&
						    (*cmp)(q, p) <= sense)
	    						t = p;
	    					else
	    						b = p;
	    					break;
	    				} else if ((*cmp)(q, p) <= sense) {
	    					t = p;
						// JAB: cast required
	    					//if (i == size)
	    					if (i == (int) size)
	    						big = 0; 
	    					goto FASTCASE;
	    				} else
	    					b = p;
		    		while (t > b+size) {
	    				i = (((t - b) / size) >> 1) * size;
	    				if ((*cmp)(q, p = b + i) <= sense)
	    					t = p;
	    				else
	    					b = p;
	    			}
	    			goto COPY;
// JAB: cast required
//FASTCASE:	    		while (i > size)
FASTCASE:	    		while (i > (int) size)
	    				if ((*cmp)(q,
	    					p = b + (i >>= 1)) <= sense)
	    					t = p;
	    				else
	    					b = p;
COPY:	    			b = t;
	    		}
	    		i = size;
	    		if (q == f1) {
	    			if (iflag) {
	    				ICOPY_LIST(f2, tp2, b);
	    				ICOPY_ELT(f1, tp2, i);
	    			} else {
	    				CCOPY_LIST(f2, tp2, b);
	    				CCOPY_ELT(f1, tp2, i);
	    			}
	    		} else {
	    			if (iflag) {
	    				ICOPY_LIST(f1, tp2, b);
	    				ICOPY_ELT(f2, tp2, i);
	    			} else {
	    				CCOPY_LIST(f1, tp2, b);
	    				CCOPY_ELT(f2, tp2, i);
	    			}
	    		}
	    	}
	    	if (f2 < l2) {
	    		if (iflag)
	    			ICOPY_LIST(f2, tp2, l2);
	    		else
	    			CCOPY_LIST(f2, tp2, l2);
	    	} else if (f1 < l1) {
	    		if (iflag)
	    			ICOPY_LIST(f1, tp2, l1);
	    		else
	    			CCOPY_LIST(f1, tp2, l1);
	    	}
	    	*p1 = l2;
	    }
	    tp2 = list1;	/* swap list1, list2 */
	    list1 = list2;
	    list2 = tp2;
	    last = list2 + nmemb*size;
	}
	if (base == list2) {
		memmove(list2, list1, nmemb*size);
		list2 = list1;
	}
	// JAB: convert to mfree
	//free(list2);
	if(!bufSpace) mfree(list2, mallocsize, "gbmergesort");
	// JAB: return void now
	//return (0);
	return;
}

// JAB: namespace collision with qsort
//#define	swap(a, b) {
#define	merge_swap(a, b) {				\
		s = b;					\
		i = size;				\
		do {					\
			tmp = *a; *a++ = *s; *s++ = tmp; \
		} while (--i);				\
		a -= size;				\
	}
#define reverse(bot, top) {				\
	s = top;					\
	do {						\
		i = size;				\
		do {					\
			tmp = *bot; *bot++ = *s; *s++ = tmp; \
		} while (--i);				\
		s -= size2;				\
	} while(bot < s);				\
}

/*
 * Optional hybrid natural/pairwise first pass.  Eats up list1 in runs of
 * increasing order, list2 in a corresponding linked list.  Checks for runs
 * when THRESHOLD/2 pairs compare with same sense.  (Only used when NATURAL
 * is defined.  Otherwise simple pairwise merging is used.)
 */
// JAB: avoid namespace polution...
//void
static void
setup(u_char *list1, u_char *list2, size_t n, size_t size,
    int (*cmp)(const void *, const void *))
{
	int i, length, size2, tmp, sense;
	u_char *f1, *f2, *s, *l2, *last, *p2;

	size2 = size*2;
	if (n <= 5) {
		insertionsort(list1, n, size, cmp);
		*EVAL(list2) = (u_char*) list2 + n*size;
		return;
	}
	/*
	 * Avoid running pointers out of bounds; limit n to evens
	 * for simplicity.
	 */
	i = 4 + (n & 1);
	insertionsort(list1 + (n - i) * size, i, size, cmp);
	last = list1 + size * (n - i);
	*EVAL(list2 + (last - list1)) = list2 + n * size;

#ifdef NATURAL
	p2 = list2;
	f1 = list1;
	sense = (cmp(f1, f1 + size) > 0);
	for (; f1 < last; sense = !sense) {
		length = 2;
					/* Find pairs with same sense. */
		for (f2 = f1 + size2; f2 < last; f2 += size2) {
			if ((cmp(f2, f2+ size) > 0) != sense)
				break;
			length += 2;
		}
		if (length < THRESHOLD) {		/* Pairwise merge */
			do {
				p2 = *EVAL(p2) = f1 + size2 - list1 + list2;
				if (sense > 0)
					merge_swap (f1, f1 + size);
			} while ((f1 += size2) < f2);
		} else {				/* Natural merge */
			l2 = f2;
			for (f2 = f1 + size2; f2 < l2; f2 += size2) {
				if ((cmp(f2-size, f2) > 0) != sense) {
					p2 = *EVAL(p2) = f2 - list1 + list2;
					if (sense > 0)
						reverse(f1, f2-size);
					f1 = f2;
				}
			}
			if (sense > 0)
				reverse (f1, f2-size);
			f1 = f2;
			if (f2 < last || cmp(f2 - size, f2) > 0)
				p2 = *EVAL(p2) = f2 - list1 + list2;
			else
				p2 = *EVAL(p2) = list2 + n*size;
		}
	}
#else		/* pairwise merge only. */
	for (f1 = list1, p2 = list2; f1 < last; f1 += size2) {
		p2 = *EVAL(p2) = p2 + size2;
		if (cmp (f1, f1 + size) > 0)
			merge_swap(f1, f1 + size);
	}
#endif /* NATURAL */
}

/*
 * This is to avoid out-of-bounds addresses in sorting the
 * last 4 elements.
 */
static void
insertionsort(u_char *a, size_t n, size_t size,
    int (*cmp)(const void *, const void *))
{
	u_char *ai, *s, *t, *u, tmp;
	int i;

	for (ai = a+size; --n >= 1; ai += size)
		for (t = ai; t > a; t -= size) {
			u = t - size;
			if (cmp(u, t) <= 0)
				break;
			merge_swap(u, t);
		}
}

#ifdef JAB_TEST
#include <sys/time.h>
// compare first four bytes of a block
int cmp_4(const void* p1, const void* p2) {
	return memcmp(p1, p2, 4);
}
int cmp_8(const void* p1, const void* p2) {
	return memcmp(p1, p2, 8);
}
int cmp_13(const void* p1, const void* p2) {
	return memcmp(p1, p2, 13);
}
int cmp_1(const void* p1, const void* p2) {
	const uint8_t*	b1	=	(const uint8_t*)	p1;
	const uint8_t*	b2	=	(const uint8_t*)	p2;
	if (*b1 == *b2)
		return 0;
	if (*b1 < *b2)
		return -1;
	return 1;
}
inline int64_t gettime64() {
	struct timeval	tv;
	gettimeofday(&tv, NULL);
	int64_t t64;
	t64 = tv.tv_sec * 1000000 + tv.tv_usec;
	return t64;
}

int main(int argc, char* argv[])
{
	FILE*		pfRandom;
	pfRandom = fopen("/dev/urandom", "r");
	if (pfRandom == NULL) {
		fprintf(stderr, "unable to open /dev/urandom\n");
		exit(1);
	}

	uint8_t		block1[1024 * 1024];
	uint8_t		block2[1024 * 1024];
	uint8_t		block3[1024 * 1024];
	uint8_t*	p1;
	uint8_t*	p2;
	uint8_t*	p3;
	uint32_t	i;
	uint32_t	j;
	int64_t		t0;
	int64_t		t1;
	int64_t		t2;
	int64_t		t3;
	int32_t		qpc1;
	int32_t		qpc2;
	int32_t		qpc3;
	for (i = 0; ; i++) {
		if (fread(block1, sizeof(block1), 1, pfRandom) != 1) {
			fprintf(stderr, "unable to read /dev/urandom");
			exit(2);
		}
		//fprintf(stderr, "%d: read block\n", i);
		gbmemcpy(block2, block1, sizeof(block2));
		gbmemcpy(block3, block1, sizeof(block3));
		p1 = block1;
		p2 = block2;
		p3 = block3;
		switch (i & 0x3) {
		case 0:
			QPC = 0;
			t0 = gettime64();
			      qsort(block1, sizeof(block1) / 4, 4, cmp_4);
			t1 = gettime64();
			qpc1 = QPC; QPC = 0;
			    gbqsort(block2, sizeof(block2) / 4, 4, cmp_4);
			t2 = gettime64();
			qpc2 = QPC; QPC = 0;
			gbmergesort(block3, sizeof(block3) / 4, 4, cmp_4);
			t3 = gettime64();
			qpc3 = QPC; QPC = 0;
			fprintf(stderr,
				"%06d: sorted %6d blocks "
				"%06lld %6d "
				"%06lld %6d "
				"%06lld %6d\n",
				i, sizeof(block1) / 4,
				t1 - t0, qpc1, t2 - t1, qpc2, t3 - t2, qpc3);
			for (j = 0; j < sizeof(block1) / 16; j++) {
				if (memcmp(p1, p2, 4) != 0) {
					fprintf(stderr, "%d: mismatch 12\n", i);
					exit(3);
				}
				if (memcmp(p1, p3, 4) != 0) {
					fprintf(stderr, "%d: mismatch 13\n", i);
					exit(3);
				}
				p1 += 16;
				p2 += 16;
				p3 += 16;
			}
			break;
		case 1:
			QPC = 0;
			t0 = gettime64();
			      qsort(block1, sizeof(block1) / 16, 16, cmp_8);
			t1 = gettime64();
			qpc1 = QPC; QPC = 0;
			    gbqsort(block2, sizeof(block2) / 16, 16, cmp_8);
			t2 = gettime64();
			qpc2 = QPC; QPC = 0;
			gbmergesort(block3, sizeof(block3) / 16, 16, cmp_8);
			t3 = gettime64();
			qpc3 = QPC; QPC = 0;
			fprintf(stderr,
				"%06d: sorted %6d blocks "
				"%06lld %6d "
				"%06lld %6d "
				"%06lld %6d\n",
				i, sizeof(block1) / 16,
				t1 - t0, qpc1, t2 - t1, qpc2, t3 - t2, qpc3);
			for (j = 0; j < sizeof(block1) / 16; j++) {
				if (memcmp(p1, p2, 8) != 0) { 
					fprintf(stderr, "%d: mismatch 12\n", i);
					exit(3);
				}
				if (memcmp(p1, p3, 8) != 0) {
					fprintf(stderr, "%d: mismatch 13\n", i);
					exit(3);
				}
				p1 += 16;
				p2 += 16;
				p3 += 16;
			}
			break;
		case 2:
			QPC = 0;
			t0 = gettime64();
			      qsort(block1, sizeof(block1) / 32, 32, cmp_13);
			t1 = gettime64();
			qpc1 = QPC; QPC = 0;
			    gbqsort(block2, sizeof(block2) / 32, 32, cmp_13);
			t2 = gettime64();
			qpc2 = QPC; QPC = 0;
			gbmergesort(block3, sizeof(block3) / 32, 32, cmp_13);
			t3 = gettime64();
			qpc3 = QPC; QPC = 0;
			fprintf(stderr,
				"%06d: sorted %6d blocks "
				"%06lld %6d "
				"%06lld %6d "
				"%06lld %6d\n",
				i, sizeof(block1) / 32,
				t1 - t0, qpc1, t2 - t1, qpc2, t3 - t2, qpc3);
			for (j = 0; j < sizeof(block1) / 32; j++) {
				if (memcmp(p1, p2, 13) != 0) {
					fprintf(stderr, "%d: mismatch 12\n", i);
					exit(3);
				}
				if (memcmp(p1, p3, 13) != 0) {
					fprintf(stderr, "%d: mismatch 13\n", i);
					exit(3);
				}
				p1 += 32;
				p2 += 32;
				p3 += 32;
			}
			break;
		case 3:
			QPC = 0;
			t0 = gettime64();
			      qsort(block1, sizeof(block1) / 64, 16, cmp_1);
			t1 = gettime64();
			qpc1 = QPC; QPC = 0;
			    gbqsort(block2, sizeof(block2) / 64, 16, cmp_1);
			t2 = gettime64();
			qpc2 = QPC; QPC = 0;
			gbmergesort(block3, sizeof(block3) / 64, 16, cmp_1);
			t3 = gettime64();
			qpc3 = QPC; QPC = 0;
			fprintf(stderr,
				"%06d: sorted %6d blocks "
				"%06lld %6d "
				"%06lld %6d "
				"%06lld %6d\n",
				i, sizeof(block1) / 64,
				t1 - t0, qpc1, t2 - t1, qpc2, t3 - t2, qpc3);
			for (j = 0; j < sizeof(block1) / 64; j++) {
				if (*p1 != *p2) {
					fprintf(stderr, "%d: mismatch 12\n", i);
					exit(3);
				}
				if (*p1 != *p3) {
					fprintf(stderr, "%d: mismatch 13\n", i);
					exit(3);
				}
				p1 += 64;
				p2 += 64;
				p3 += 64;
			}
			break;
		}
	}
}

#endif
