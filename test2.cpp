// Matt Wells, copyright Jan 2002

// program to test Rdb

#include "gb-include.h"

int32_t x,y,z;

#define x y

#undef x
#define y z
#define x y

int main ( int argc , char *argv[] ) {

	int32_t m_bitScores[65536];
	int32_t scores[16];
	int32_t count = 65536;
	// . each set bit in singleMask and phraseMask have a term freq
	// . count can be 65536, so this can take a couple million cycles!!
	for ( uint32_t i = 0 ; i < count ; i++ ) {
		// loop through each on bit for "i"
		m_bitScores[i] = 0;
		if ( i & 0x0001 ) m_bitScores[i] += scores[0];
		if ( i & 0x0002 ) m_bitScores[i] += scores[1];
		if ( i & 0x0004 ) m_bitScores[i] += scores[2];
		if ( i & 0x0008 ) m_bitScores[i] += scores[3];
		if ( i & 0x0010 ) m_bitScores[i] += scores[4];
		if ( i & 0x0020 ) m_bitScores[i] += scores[5];
		if ( i & 0x0040 ) m_bitScores[i] += scores[6];
		if ( i & 0x0080 ) m_bitScores[i] += scores[7];
		if ( i & 0x0100 ) m_bitScores[i] += scores[8];
		if ( i & 0x0200 ) m_bitScores[i] += scores[9];
		if ( i & 0x0400 ) m_bitScores[i] += scores[10];
		if ( i & 0x0800 ) m_bitScores[i] += scores[11];
		if ( i & 0x1000 ) m_bitScores[i] += scores[12];
		if ( i & 0x2000 ) m_bitScores[i] += scores[13];
		if ( i & 0x4000 ) m_bitScores[i] += scores[14];
		if ( i & 0x8000 ) m_bitScores[i] += scores[15];
	}

}
