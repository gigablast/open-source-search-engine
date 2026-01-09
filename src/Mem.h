// Matt Wells, copyright Sep 2001

// . mostly just wrappers for most memory functions
// . allows us to constrain memory
// . TODO: primealloc(int slotSize,int numSlots) :
//         pre-allocs a table of these slots for faster mmalloc'ing

#ifndef _MEM_H_
#define _MEM_H_

#include <sys/mman.h>        // mlockall
#include <netinet/in.h>      // for htonll
#include "Conf.h"
#include "gbassert.h"
#include <new>

extern bool g_inMemFunction;

class SafeBuf;
// some memory manipulation functions inlined below
#ifndef htonll
int64_t htonll ( uint64_t a );
#endif
#ifndef ntohll
int64_t ntohll ( uint64_t a );
#endif
key_t ntohkey ( key_t key ) ;
key_t htonkey ( key_t key ) ;

// assume only one bit is set for this (used by Address.cpp)
int32_t getBitPosLL   ( uint8_t *bit );

int32_t getHighestLitBit  ( unsigned char     bits ) ;
int32_t getHighestLitBit  ( uint16_t    bits ) ;

// these are bit #'s, like 0,1,2,3,...63 for int64_ts
int32_t getLowestLitBitLL ( uint64_t bits ) ;

// this is the value, like 0,1,2,4, ... 4billion
uint32_t      getHighestLitBitValue   ( uint32_t      bits ) ;
uint64_t getHighestLitBitValueLL ( uint64_t bits ) ;


uint32_t reverseBits ( uint32_t x ) ;

// async signal safe functions
//void memcpy_ass ( register void *dest , register const void *src , int32_t len ) ;
void memset_ass ( void *dst , const char c , int32_t len ) ;
void memset_nice ( void *dst , const char c , int32_t len ,
		   int32_t niceness ) ;

// . "*Bits" is bit offset in *
// . nb is the # of bits to compare or copy
// . returns -1 if dst < src, 0 if equal, +1 if dst > src
// . bit #0 is the least significant bit on this little endian machine
// . TODO: should we speed this up?
int32_t membitcmp  ( void *dst, int32_t dstBits, void *src, int32_t srcBits, int32_t nb );
// like above byt returns # of bits in common
int32_t membitcmp2 ( void *dst, int32_t dstBits, void *src, int32_t srcBits, int32_t nb );
// two bit copies, membitcpy1 starts copying at low bit, 2 at high bit
void membitcpy1 ( void *dst, int32_t dstBits, void *src, int32_t srcBits, int32_t nb );
void membitcpy2 ( void *dst, int32_t dstBits, void *src, int32_t srcBits, int32_t nb );

inline int gbstrlen ( const char *s ) {
    gbassert(s);
	return strlen(s);
};

class Mem {

 public:

	Mem();
	~Mem();

	bool init ( );

	void  setPid();
	pid_t getPid();

	void *gbmalloc  ( int size , const char *note  );
	void *gbcalloc  ( int size , const char *note);
	void *gbrealloc ( void *oldPtr, int oldSize, int newSize,
				const char *note);
	void  gbfree    ( void *ptr , int size , const char *note);
	char *dup     ( const void *data , int32_t dataSize , const char *note);
	char *strdup  ( const char *string , const char *note ) {
		return dup ( string , gbstrlen ( string ) + 1 , note ); };

	int32_t validate();

	// this one does not include new/delete mem, only *alloc()/free() mem
	int64_t getUsedMem () { return m_used; };
	int64_t getAvailMem() ;
	// the max mem ever allocated
	int64_t getMaxAlloced() { return m_maxAlloced; };
	int64_t getMaxAlloc  () { return m_maxAlloc; };
	const char *getMaxAllocBy() { return m_maxAllocBy; };
	// the max mem we can use!
	int64_t getMaxMem () ;

	int32_t getNumAllocated() { return m_numAllocated; };

	int64_t getNumTotalAllocated() { return m_numTotalAllocated; };

	// # of currently allocated chunks
	int32_t getNumChunks(); 

	// for debugging
	int32_t printBits  ( void *src, int32_t b , int32_t nb );

	// who underan/overran their buffers?
	int  printBreech   ( int32_t i , char core ) ;
	int  printBreeches ( char core ) ;
	// print mem usage stats
	int  printMem      ( ) ;
	void addMem ( void *mem , int32_t size , const char *note, char isnew);
	bool rmMem  ( void *mem , int32_t size , const char *note ) ;
	bool lblMem ( void *mem , int32_t size , const char *note );

	int32_t getMemSize  ( void *mem );
	int32_t getMemSlot  ( void *mem );

	void addnew ( void *ptr , int32_t size , const char *note ) ;
	void delnew ( void *ptr , int32_t size , const char *note ) ;

	bool printMemBreakdownTable(SafeBuf* sb, 
				    char *lightblue, 
				    char *darkblue);

	// We can check the size of the stack from anywhere. However,
	// setStackPointer() must be called from main, and ptr should
	// be the address of the first variable declared in main.
	void setStackPointer( char *ptr );
	int32_t checkStackSize();

	int32_t findPtr ( void *target ) ;

	int64_t m_maxAlloced; // at any one time
	int64_t m_maxAlloc; // the biggest single alloc ever done
	const char *m_maxAllocBy; // the biggest single alloc ever done

	// shared mem used
	int64_t m_sharedUsed;

	// currently used mem (estimate)
	int64_t m_used;

	// count how many allocs/news failed
	int32_t m_outOfMems;

	int32_t          m_numAllocated;
	int64_t     m_numTotalAllocated;
	uint32_t m_memtablesize;

 protected:
	char *m_stackStart;
	
};

extern class Mem g_mem;

inline void *mmalloc ( int size , const char *note ) {
	return g_mem.gbmalloc(size,note); };
inline void *mcalloc ( int size , const char *note ) { 
	return g_mem.gbcalloc(size,note); };
inline void *mrealloc (void *oldPtr, int oldSize, int newSize,
			const char *note) {
	return g_mem.gbrealloc(oldPtr,oldSize,newSize,note);};
inline void  mfree    ( void *ptr , int size , const char *note) {
	return g_mem.gbfree(ptr,size,note);};
inline char *mdup     ( const void *data , int32_t dataSize , const char *note) {
	return g_mem.dup(data,dataSize,note);};
inline char *mstrdup  ( const char *string , const char *note ) {
	return g_mem.strdup(string,note);};
inline void mnew ( void *ptr , int32_t size , const char *note ) {
	return g_mem.addnew ( ptr , size , note );};
inline void mdelete ( void *ptr , int32_t size , const char *note ) {
	return g_mem.delnew ( ptr , size , note ); };
inline bool relabel   ( void *ptr , int32_t size , const char *note ) {
	return g_mem.lblMem( ptr, size, note ); };

void operator delete ( void *p ) throw();
void * operator new (size_t size) noexcept(false);
// you MUST call mmalloc, mcalloc and mrealloc!!
#define malloc coreme 
#define calloc coreme 
#define realloc coreme 
inline void *coreme ( int x ) { gbassert(false); return NULL; }

int32_t getAllocSize(void *p);

inline int32_t getHighestLitBit ( uint16_t bits ) {
	unsigned char b = *((unsigned char *)(&bits) + 1);
	if ( ! b ) return getHighestLitBit ( (unsigned char) bits );
	return 8 + getHighestLitBit ( (unsigned char) b );
}

inline int32_t getHighestLitBit ( unsigned char c ) {
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

inline int32_t getNumBitsOn8 ( unsigned char c ) {
	return g_a[(unsigned char) c];
}

inline int32_t getNumBitsOn16 ( uint16_t bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] ;
}

inline int32_t getNumBitsOn32 ( uint32_t bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] +
		g_a [ *((unsigned char *)(&bits) + 2)  ] +
		g_a [ *((unsigned char *)(&bits) + 3)  ] ;
}

inline int32_t getNumBitsOn64 ( uint64_t bits ) {
	return 	g_a [ *((unsigned char *)(&bits) + 0)  ] +
		g_a [ *((unsigned char *)(&bits) + 1)  ] +
		g_a [ *((unsigned char *)(&bits) + 2)  ] +
		g_a [ *((unsigned char *)(&bits) + 3)  ] +
		g_a [ *((unsigned char *)(&bits) + 4)  ] +
		g_a [ *((unsigned char *)(&bits) + 5)  ] +
		g_a [ *((unsigned char *)(&bits) + 6)  ] +
		g_a [ *((unsigned char *)(&bits) + 7)  ] ;
}

inline int32_t getNumBitsOnX ( unsigned char *s , int32_t slen ) {
	if ( slen == 1 ) return getNumBitsOn8 ( *s );
	if ( slen == 2 ) return getNumBitsOn16 ( *(uint16_t *)s );
	if ( slen == 4 ) return getNumBitsOn32 ( *(uint32_t *)s );
	if ( slen == 3 ) 
		return  getNumBitsOn8 ( s[0] ) +
			getNumBitsOn8 ( s[1] ) +
			getNumBitsOn8 ( s[2] ) ;
	int32_t total = 0;
	for ( int32_t i = 0 ; i < slen ; i++ )
		total += getNumBitsOn8 ( s[i] );
	return total;
}

// assume only one bit is set for this (used by Address.cpp)
inline int32_t getBitPosLL ( uint8_t *bit ) {
	// which int32_t is it in?
	if ( *(int32_t *)bit ) {
		if ( bit[0] ) return getHighestLitBit ( bit[0] );
		if ( bit[1] ) return getHighestLitBit ( bit[1] ) + 8;
		if ( bit[2] ) return getHighestLitBit ( bit[2] ) + 16;
		if ( bit[3] ) return getHighestLitBit ( bit[3] ) + 24;
		gbassert(false);
	}
	if ( bit[4] ) return getHighestLitBit ( bit[4] ) + 32;
	if ( bit[5] ) return getHighestLitBit ( bit[5] ) + 40;
	if ( bit[6] ) return getHighestLitBit ( bit[6] ) + 48;
	if ( bit[7] ) return getHighestLitBit ( bit[7] ) + 56;
    gbassert(false);
	return -1;
}


#endif
