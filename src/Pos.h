// Matt Wells, copyright Jul 2005

#ifndef _POS_H_
#define _POS_H_

#include "Words.h"

// this class is used to measure the number of characters between two "words"
// (as defined in the Words.cpp class) in units of "characters". A utf8
// character can be 1, 2, 3 or 4 bytes, so be careful.

#define POS_LOCALBUFSIZE 20

class Pos {

 public:

	Pos();
	~Pos();
	void reset();

	bool set    ( class Words  *words   ,
		      class Sections *sections ,
		      char         *f      = NULL ,
		      char         *fend   = NULL ,
		      int32_t         *flen   = NULL ,
		      int32_t          a      =  0   ,
		      int32_t          b      = -1   ,
		      // you can provide it with a buffer to prevent a malloc
		      char         *buf    = NULL ,
		      int32_t          bufSize= 0    );

	// . filter out xml words [a,b] into plain text, stores into "p"
	// . will not exceed "pend"
	// . returns number of BYTES stored into "p"
	int32_t filter ( char         *p     ,
		      char         *pend  ,
		      class Words  *words ,
		      int32_t          a     =  0 ,
		      int32_t          b     = -1 ,
		      class Sections *sections = NULL );

	int32_t getMemUsed () { return m_bufSize; };

	// . the position in CHARACTERS of word i is given by m_pos[i]
	// . this is NOT the byte position. you can have 2, 3 or even 4
	//   byte characters in utf8. the purpose here is for counting 
	//   "letters" or "characters" for formatting purposes.
	int32_t *m_pos;
	int32_t  m_numWords;

	char  m_localBuf [ POS_LOCALBUFSIZE ];
	char *m_buf;
	int32_t  m_bufSize;

	bool  m_needsFree;
};

#endif
