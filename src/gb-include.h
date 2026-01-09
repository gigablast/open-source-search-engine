#ifndef GB_INCLUDE_H
#define GB_INCLUDE_H

// fix on 64-bit architectures so sizeof(uint96_t) is 12, not 16!
//#pragma pack(0)

//#define gbmemcpy memcpy_ass
//#define memset memset_ass

extern int g_inMemcpy;

//#define gbmemcpy(xx,yy,zz) {g_inMemcpy=1;memcpy(xx,yy,zz);g_inMemcpy=0;}

// use bcopy() since when doing real-time profiling in Profiler.cpp
// it interrupts the code with a signal and then calls backtrace() which
// itself calls memcpy(). so if it interrupted the code in an memcpy()
// it causes a segfault because memcpy() is not async safe.
#ifdef __APPLE__
#define gbmemcpy(xx,yy,zz) {memmove(xx,yy,zz); }
#else
#define gbmemcpy(xx,yy,zz) {bcopy(yy,xx,zz); }
#endif

// i guess use bcopy for this now too!
#ifdef __APPLE__
#define memcpy_ass(xx,yy,zz) {memmove(xx,yy,zz); }
#else
#define memcpy_ass(xx,yy,zz) {bcopy(yy,xx,zz); }
#endif

#include <cinttypes>

#define XINT32 "x"
#define UINT32 "u"
#define INT32  "d"
#define PRIX32 "X"

#define XINT64 PRIx64
#define UINT64 PRIu64
#define INT64  PRId64
#define PTRTYPE  uintptr_t
#define SPTRTYPE intptr_t
#define PTRFMT  PRIuPTR

#include <ctype.h>	// Log.h
#include <errno.h>	// Errno.h
#include <sys/errno.h>	// Errno.h
#include <stdarg.h>	// Log.h
#include <stdint.h>	// commonly included in include files
#include <stdio.h>	// commonly included in include files
#include <stdlib.h>	// commonly included in include files
#include <string.h>	// commonly included in include files
#include <unistd.h>	// commonly included in include files

#include "types.h"	// commonly included in include files
#include "fctypes.h"	// commonly included in include files
#include "hash.h"	// commonly included in include files

#include "GbErrno.h"	// commonly included in include files
#include "Log.h"	// commonly included in include files

// cygwin fix
#ifndef O_ASYNC
#define O_ASYNC 0
#endif

#endif
