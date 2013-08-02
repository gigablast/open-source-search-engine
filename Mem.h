// Matt Wells, copyright Sep 2001

// . mostly just wrappers for most memory functions
// . allows us to constrain memory
// . also calls mlockall() on construction to avoid swapping out any mem
// . TODO: primealloc(int slotSize,int numSlots) :
//         pre-allocs a table of these slots for faster mmalloc'ing

#ifndef _MEM_H_
#define _MEM_H_

#include <sys/mman.h>        // mlockall
#include <netinet/in.h>      // for htonll
#include "Conf.h"
#include <new>
//#ifdef DMALLOC
//#include <dmalloc.h>
//#endif

// we share malloc between threads, so you need to get the lock
//void mutexLock   ( );
//void mutexUnlock ( );

struct SafeBuf;
// some memory manipulation functions inlined below
long long htonll ( unsigned long long a );
long long ntohll ( unsigned long long a );
key_t ntohkey ( key_t key ) ;
key_t htonkey ( key_t key ) ;

/*
long getNumBitsOn ( unsigned char      bits );
long getNumBitsOn ( unsigned short     bits );
long getNumBitsOn ( unsigned long      bits );
long getNumBitsOn ( unsigned long long bits );
*/

// assume only one bit is set for this (used by Address.cpp)
long getBitPosLL   ( uint8_t *bit );

long getHighestLitBit  ( unsigned char     bits ) ;
long getHighestLitBit  ( unsigned short    bits ) ;

// these are bit #'s, like 0,1,2,3,...63 for long longs
long getLowestLitBitLL ( unsigned long long bits ) ;

// this is the value, like 0,1,2,4, ... 4billion
unsigned long      getHighestLitBitValue   ( unsigned long      bits ) ;
unsigned long long getHighestLitBitValueLL ( unsigned long long bits ) ;


unsigned long reverseBits ( unsigned long x ) ;

// async signal safe functions
void memcpy_ass ( register void *dest , register const void *src , long len ) ;
void memset_ass ( register void *dst , register const char c , long len ) ;
void memset_nice ( register void *dst , register const char c , long len ,
		   long niceness ) ;

// . "*Bits" is bit offset in *
// . nb is the # of bits to compare or copy
// . returns -1 if dst < src, 0 if equal, +1 if dst > src
// . bit #0 is the least significant bit on this little endian machine
// . TODO: should we speed this up?
long membitcmp  ( void *dst, long dstBits, void *src, long srcBits, long nb );
// like above byt returns # of bits in common
long membitcmp2 ( void *dst, long dstBits, void *src, long srcBits, long nb );
// two bit copies, membitcpy1 starts copying at low bit, 2 at high bit
void membitcpy1 ( void *dst, long dstBits, void *src, long srcBits, long nb );
void membitcpy2 ( void *dst, long dstBits, void *src, long srcBits, long nb );

inline int gbstrlen ( const char *s ) {
	if ( ! s ) { char *xx=NULL;*xx=0; }
	return strlen(s);
};

class Mem {

 public:

	Mem();
	~Mem();

	bool init ( long long maxMem );

	void  setPid();
	pid_t getPid();

	void *gbmalloc  ( int size , const char *note  );
	void *gbcalloc  ( int size , const char *note);
	void *gbrealloc ( void *oldPtr, int oldSize, int newSize,
				const char *note);
	void  gbfree    ( void *ptr , int size , const char *note);
	char *dup     ( const void *data , long dataSize , const char *note);
	char *strdup  ( const char *string , const char *note ) {
		return dup ( string , gbstrlen ( string ) + 1 , note ); };

	long validate();

	//void *gbmalloc2  ( int size , const char *note  );
	//void *gbcalloc2  ( int size , const char *note);
	//void *gbrealloc2 ( void *oldPtr,int oldSize ,int newSize,
	//			const char *note);
	//void  gbfree2    ( void *ptr , int size , const char *note);
	//char *dup2       ( const void *data , long dataSize ,
	//			const char *note);

	// this one does not include new/delete mem, only *alloc()/free() mem
	long long getUsedMem () { return m_used; };
	long long getAvailMem() ;
	// the max mem ever alloced
	long long getMaxAlloced() { return m_maxAlloced; };
	long long getMaxAlloc  () { return m_maxAlloc; };
	const char *getMaxAllocBy() { return m_maxAllocBy; };
	// the max mem we can use!
	long long getMaxMem () ;

	long getNumAllocated() { return m_numAllocated; };

	long long getNumTotalAllocated() { return m_numTotalAllocated; };

	// # of currently allocated chunks
	long getNumChunks(); 

	// for debugging
	long printBits  ( void *src, long b , long nb );

	// who underan/overran their buffers?
	int  printBreech   ( long i , char core ) ;
	int  printBreeches ( char core ) ;
	// print mem usage stats
	int  printMem      ( ) ;
	void addMem ( void *mem , long size , const char *note , char isnew ) ;
	bool rmMem  ( void *mem , long size , const char *note ) ;
	bool lblMem ( void *mem , long size , const char *note );

	long getMemSize  ( void *mem );
	long getMemSlot  ( void *mem );

	void addnew ( void *ptr , long size , const char *note ) ;
	void delnew ( void *ptr , long size , const char *note ) ;

	bool printMemBreakdownTable(SafeBuf* sb, 
				    char *lightblue, 
				    char *darkblue);

	// We can check the size of the stack from anywhere. However,
	// setStackPointer() must be called from main, and ptr should
	// be the address of the first variable declared in main.
	void setStackPointer( char *ptr );
	long checkStackSize();

	long findPtr ( void *target ) ;

	// alloc this much memory then immediately free it.
	// this should assign this many pages to this process id so no other
	// process can grab them -- only us.
	// TODO: use sbrk()
	//	bool  reserveMem ( long long bytesToReserve );

	long long m_maxAlloced; // at any one time
	long long m_maxAlloc; // the biggest single alloc ever done
	const char *m_maxAllocBy; // the biggest single alloc ever done
	long long m_maxMem;

	// shared mem used
	long long m_sharedUsed;

	// currently used mem (estimate)
	long long m_used;

	long          m_numAllocated;
	long long     m_numTotalAllocated;
	unsigned long m_memtablesize;

 protected:
	char *m_stackStart;
	
};

extern class Mem g_mem;

//#define mmalloc(size,note) malloc(size)
//#define mfree(ptr,size,note) free(ptr)
//#define mrealloc(oldPtr,oldSize,newSize,note) realloc(oldPtr,newSize)
inline void *mmalloc ( int size , const char *note ) { 
	return g_mem.gbmalloc(size,note); };
inline void *mcalloc ( int size , const char *note ) { 
	return g_mem.gbcalloc(size,note); };
inline void *mrealloc (void *oldPtr, int oldSize, int newSize,
			const char *note) {
	return g_mem.gbrealloc(oldPtr,oldSize,newSize,note);};
inline void  mfree    ( void *ptr , int size , const char *note) {
	return g_mem.gbfree(ptr,size,note);};
inline char *mdup     ( const void *data , long dataSize , const char *note) {
	return g_mem.dup(data,dataSize,note);};
inline char *mstrdup  ( const char *string , const char *note ) {
	return g_mem.strdup(string,note);};
inline void mnew ( void *ptr , long size , const char *note ) {
	return g_mem.addnew ( ptr , size , note );};
inline void mdelete ( void *ptr , long size , const char *note ) {
	return g_mem.delnew ( ptr , size , note ); };
inline bool relabel   ( void *ptr , long size , const char *note ) {
	return g_mem.lblMem( ptr, size, note ); };

//#ifdef _LEAKCHECK_
// use a macro to make delete calls call g_mem.rmMem()
//#define delete(X) { delete X; g_mem.m_freed += sizeof(*X); g_mem.rmMem(X,sizeof(*X),"new"); }
//#elif
//#define delete(X) { delete X; g_mem.m_freed += sizeof(*X); }
//#endif
//#ifndef DMALLOC
void operator delete ( void *p ) ;
void * operator new (size_t size) throw (std::bad_alloc);
// you MUST call mmalloc, mcalloc and mrealloc!!
#define malloc coreme 
#define calloc coreme 
#define realloc coreme 
//#endif
inline void *coreme ( int x ) { char *xx = NULL; *xx = 0; return NULL; }

long getAllocSize(void *p);
//void * operator new (size_t size) ;

inline long getHighestLitBit ( unsigned short bits ) {
	unsigned char b = *((unsigned char *)(&bits) + 1);
	if ( ! b ) return getHighestLitBit ( (unsigned char) bits );
	return 8 + getHighestLitBit ( (unsigned char) b );
}

inline long getHighestLitBit ( unsigned char c ) {
	static char a[256] = { 0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 
			       4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 
			       5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
			       5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 
			       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 
			       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 
			       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 
			       6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 
			       7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
	return a[(unsigned char) c];
}

extern char g_a[];

inline long getNumBitsOn8 ( unsigned char c ) {
	return g_a[(unsigned char) c];
}

inline long getNumBitsOn16 ( unsigned short bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] ;
}

inline long getNumBitsOn32 ( unsigned long bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] +
		g_a [ *((unsigned char *)(&bits) + 2)  ] +
		g_a [ *((unsigned char *)(&bits) + 3)  ] ;
}

inline long getNumBitsOn64 ( unsigned long long bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] +
		g_a [ *((unsigned char *)(&bits) + 2)  ] +
		g_a [ *((unsigned char *)(&bits) + 3)  ] +
		g_a [ *((unsigned char *)(&bits) + 4)  ] +
		g_a [ *((unsigned char *)(&bits) + 5)  ] +
		g_a [ *((unsigned char *)(&bits) + 6)  ] +
		g_a [ *((unsigned char *)(&bits) + 7)  ] ;
}

// assume only one bit is set for this (used by Address.cpp)
inline long getBitPosLL ( uint8_t *bit ) {
	// which long is it in?
	if ( *(long *)bit ) {
		if ( bit[0] ) return getHighestLitBit ( bit[0] );
		if ( bit[1] ) return getHighestLitBit ( bit[1] ) + 8;
		if ( bit[2] ) return getHighestLitBit ( bit[1] ) + 16;
		if ( bit[3] ) return getHighestLitBit ( bit[1] ) + 24;
		char *xx=NULL;*xx=0; 
	}
	if ( bit[4] ) return getHighestLitBit ( bit[4] ) + 32;
	if ( bit[5] ) return getHighestLitBit ( bit[5] ) + 40;
	if ( bit[6] ) return getHighestLitBit ( bit[6] ) + 48;
	if ( bit[7] ) return getHighestLitBit ( bit[7] ) + 56;
	char *xx=NULL;*xx=0; 
	return -1;
}


#endif
