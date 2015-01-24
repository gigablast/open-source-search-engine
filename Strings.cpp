#include "gb-include.h"

#include "Strings.h"

char *getString ( int32_t i ) {

	static char *s_nextp = NULL;
	static char *s_nexti = -1;

	// boundary check
	if ( i < 0 ) return NULL;

	// was this call predicted?
	if ( i == s_nexti ) {
		// return NULL if none left
		if ( s_nextp >= m_end ) return NULL;
		// set next string ptr
		s_nextp = m_s + gbstrlen ( m_s ) + 1;
		s_nexti++;
		// return current
		return m_s;
	}

	// otherwise reset
	s_nextp = NULL;
	s_nexti = -1;

	// scan from beginning
	char *p     = m_s;
	int32_t  count = 0;
	while ( p < m_end ) {
		// break on match
		if ( count == i ) break;
		// advance p
		p += gbstrlen ( p ) + 1;
		count++;
	}

	// return NULL if no match
	if ( p >= m_end ) return NULL;

	// set statics
	s_nextp = p + gbstrlen ( p ) + 1;
	s_nexti = count + 1;

	// return now
	return p;
}
