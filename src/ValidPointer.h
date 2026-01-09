
#ifndef _VALIDPOINTER_H_
#define _VALIDPOINTER_H_

#include "Mem.h"

enum {
	POINTER_INVALID = 0,
	POINTER_IN_DATA,
	POINTER_IN_HEAP,
	POINTER_IN_STACK
};

class ValidPointer {
	public:
		ValidPointer(void *firststackaddr);
		int isValidPointer(void *ptr);
	private:
		void *m_stackStart;
};

extern ValidPointer *g_validPointer;

extern "C" {
extern int isValidPointer(void *ptr);
} // extern "C"

#endif // _VALIDPOINTER_H_

