//*****************************************************************************
//
//	very efficient sorting routines from OpenBSD.
//
//	use gbqsort for generalized sorting tasks.
//	this version of qsort is guaranteed to not devolve into O(N^2) behavior.
//
//	use gbmergesort for sorting tasks where partial ordering is present.
//	gbmergesort approaches O(n) when elements are already partially sorted.
//
//*****************************************************************************

#define gbsort gbmergesort

extern void gbqsort(void *aa, size_t n, size_t es,
			int (*cmp)(const void *, const void *),
			int niceness = 0);
extern void gbmergesort(void *base, size_t nmemb, size_t size,
			int (*cmp)(const void *, const void *),
			int niceness = 0,
			char* bufSpace = NULL, int32_t bufSpaceSize = 0);

//*****************************************************************************
/* Copyright (C) 1999 Lucent Technologies */
/* From 'Programming Pearls' by Jon Bentley */

/* Lucent Technologies license: */
/* You may use this code for any purpose, as int32_t as you */
/* leave the copyright notice and book citation attached. */
//*****************************************************************************

// should not be required if gb-include.h is used...
//#include <stdint.h>

//*****************************************************************************
/*
 * Heap Sort 10_1_5
 *
 * This is derived from material in the copyright notice above.
 * This sorting algorithm sorts 10-bytes blocks
 * The sort key is at offset 1 and length five
 */
//*****************************************************************************

inline int32_t cmp_10_1_5(uint8_t* b1, uint8_t* b2) {
	uint32_t*	pu32_1		=	(uint32_t*) (b1 + 1);
	uint32_t*	pu32_2		=	(uint32_t*) (b2 + 1);

	if (*pu32_1 < *pu32_2)
		return -1;
	if (*pu32_1 > *pu32_2)
		return 1;

	//uint8_t*	pu8_1		=	b1 + 5;
	//uint8_t*	pu8_2		=	b2 + 5;

	if (*b1 < *b2)
		return -1;
	if (*b1 > *b2)
		return 1;
	return 0;
}

inline void swap_10(uint8_t* pi, uint8_t* pj) {
	if (pi == pj)
		return;
	uint8_t		tmp[10];
	gbmemcpy(tmp, pi, 10);
	gbmemcpy(pi, pj, 10);
	gbmemcpy(pj, tmp, 10);
}

inline void siftup_10_1_5(uint8_t* arr, int32_t u) {
	int32_t		i		=	u;
	int32_t		p;
	for (;;) {
		if (i == 1)
			break;
		p = i / 2;
		//if (x[p] >= x[i])
		uint8_t*	pp	=	arr+(10*p);
		uint8_t*	pi	=	arr+(10*i);
		if (cmp_10_1_5(pp, pi) > -1)
			break;
		swap_10(pp, pi);
		i = p;
	}
}

inline void siftdown_10_1_5(uint8_t* arr, int32_t l, int32_t u) {
	int32_t		i;
	int32_t		c;
	for (i = l; (c = 2*i) <= u; i = c) {
		//if (c+1 <= u && x[c+1] > x[c])
		if (c+1 <= u && cmp_10_1_5(arr+((c+1)*10), arr+(c*10)) == 1)
			c++;
		//if (x[i] > x[c])
		uint8_t*	pi	=	arr+(10*i);
		uint8_t*	pc	=	arr+(10*c);
		if (cmp_10_1_5(pi, pc) == 1)
			break;
		swap_10(pi, pc);
	}
}

inline void hsort_10_1_5(uint8_t* arr, int32_t count) {
	int32_t i;
	//x--;
	arr -= 10;
	for (i = 2; i <= count; i++)
		siftup_10_1_5(arr, i);
	for (i = count; i >= 2; i--) {
		swap_10(arr+10, arr+(i*10));
		siftdown_10_1_5(arr, 1, i-1);
	}
	//x++;
}

//*****************************************************************************
// end of Lucent Technologies code
//*****************************************************************************

#ifdef JAB_TEST
#include <stdio.h>
// JAB - test stub
int main() {
	char arr[101]			=	
						"321098765\0"
						"765432109\0"
						"876543210\0"
						"543210987\0"
						"098765432\0"
						"432109876\0"
						"987654321\0"
						"210987654\0"
						"654321098\0"
						"109876543\0"
						;

	hsort_10_1_5((uint8_t*) arr, 10);

	printf("out:\n");
	int32_t i;
	for (i = 0; i < 10; i++) {
		printf("%s\n", arr+(i*10));
	}
}

#endif	// JAB_TEST
