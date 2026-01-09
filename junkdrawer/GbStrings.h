// Matt Wells, copyright Aug 2002

// . a Strings class is a list of NULL-terminated strings all in one string

#ifndef _STRINGS_H_
#define _STRINGS_H_

#include "Mem.h"

class StringArray {
 public:
	Strings() { m_s = NULL; m_len = 0; };
	~Strings () { if (m_s) mfree(m_s,m_len,"Strings"); m_s = NULL; };
	void set ( char *s , int32_t len ) { m_s=s; m_len=len; m_end=s+len; };
	char *getString ( int32_t i ) ;
	// returns false and sets errno on error
	bool addString ( char *s ) {
		int32_t len = gbstrlen(s);
		m_s = (int32_t *) mrealloc (m_s, m_len, m_len+len+1, "Strings");
		if ( ! m_s ) return false;
		strcpy ( m_s + m_len , s );
		m_len += len + 1;
	};
	char *m_s;
	char *m_end;
	int32_t  m_len;
};

class LongArray {
 public:
	LongArray() { m_x = NULL; m_n = 0; };
	~LongArray() { if (m_x) mfree(m_x,m_n*4,"LongArray"); m_x = NULL; };
	// returns false and sets errno on error
	bool addLong ( int32_t x ) {
		m_x = (int32_t *) mrealloc (m_x, m_n*4, m_n*4+4, "LongArray");
		if ( ! m_x ) return false;
		m_x [ m_n++ ] = x;
	};
	int32_t getLong ( int32_t i ) { return m_x[i]; };
	int32_t *m_x;
	int32_t  m_n;
};

class CharArray {
 public:
	CharArray() { m_x = NULL; m_n = 0; };
	~CharArray() { if (m_x) mfree(m_x,m_n,"CharArray"); m_x = NULL; };
	// returns false and sets errno on error
	bool addChar ( int32_t x ) {
		m_x = (int32_t *) mrealloc (m_x, m_n, m_n+1, "CharArray");
		if ( ! m_x ) return false;
		m_x [ m_n++ ] = x;
	};
	char getChar ( int32_t i ) { return m_x[i]; };
	char *m_x;
	int32_t  m_n;
};

#endif
