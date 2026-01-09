#ifndef _ADULTBIT_H_
#define _ADULTBIT_H_

//#include "TermTable.h"
#include "Xml.h"

class AdultBit {

 public:

	bool isSet() { return m_isAdult; };
	bool isAdult() { return m_isAdult; };

	void set ( char *s , int32_t niceness = 0) { m_isAdult = getBit ( s ); };
	void set ( bool flag ) { m_isAdult = flag; };

	void reset() { m_isAdult = false; };
	AdultBit() { reset(); };

 private:

	bool getBit    ( char *s , int32_t niceness = 0);
	bool isDirty   ( char *s , int32_t len ) ;
	bool isObscene ( char *s , int32_t len ) ;

	bool m_isAdult;
};

#endif
