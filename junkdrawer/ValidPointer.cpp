
#include "ValidPointer.h"

ValidPointer *g_validPointer = NULL;

ValidPointer::ValidPointer(void *firststackaddr) {
	m_stackStart = firststackaddr;
	if(g_validPointer) {
		log(LOG_LOGIC, "init: Second instantiation of ValidPointer, this is a bug\n");
		char *xx = NULL; *xx = 0;
	}
	g_validPointer = this;
}

// Wonky pointer tricks, read the ELF spec before you
// monkey with this.
int ValidPointer::isValidPointer(void *ptr) {
			uint32_t stackvar = 0;
			extern void *__FRAME_BEGIN__;
			extern void *_end;
			if(ptr < __FRAME_BEGIN__) return(POINTER_INVALID);
			if(ptr < _end) return(POINTER_IN_DATA);
			// Sadly, this isn't definitive, but it's a good guess
			if((uint32_t)ptr < ((uint32_t)&_end + g_mem.getMaxMem()))
				return(POINTER_IN_HEAP);
			if(ptr < m_stackStart && ptr > &stackvar) return(POINTER_IN_STACK);
			return(POINTER_INVALID);
		}

extern "C" {
int isValidPointer(void *ptr) {
#if 0
	// Disabled, it returns invalid on some valid pointers.
	if(!g_validPointer) {
		log(LOG_WARN,
		    "init: Pointer Validation not set up yet, this is a bug.\n");
		return(POINTER_INVALID);
	}
	return(g_validPointer->isValidPointer(ptr));
#else // 0
	return(true);
#endif // 0
}
} // extern "C"
