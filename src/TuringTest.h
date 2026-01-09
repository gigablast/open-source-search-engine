#ifndef _TURINGTEST_H_
#define _TURINGTEST_H_

#include "gb-include.h"
#include "TcpServer.h"
#include "HashTable.h"

class TuringTest {
 public:
	TuringTest();
	~TuringTest();
	static const int32_t TMAX_HEIGHT = 19;
	static const int32_t TMAX_WIDTH  = 15;
	static const int32_t MASK  = 4095;
	// turing test routines

	//if they don't have permission, print the turing test
	//otherwise don't print anything.
	bool isHuman( HttpRequest *r);
	bool printTest ( SafeBuf* sb );

protected:
	HashTable m_answers;
	bool      m_tinit;
	int32_t      m_nextQuestion;
	char      m_buf[26][TMAX_HEIGHT][TMAX_WIDTH];
};

extern TuringTest g_turingTest;
#endif
