#ifndef GB_INCLUDE_H
#define GB_INCLUDE_H

// fix on 64-bit architectures so sizeof(uint96_t) is 12, not 16!
//#pragma pack(0)

//#define memcpy memcpy_ass
//#define memset memset_ass

#include <inttypes.h>

#define XINT32 "x"
#define UINT32 "u"
#define INT32  "d"
#define PRIX32 "X"

//#define XINT64 "llx"
//#define UINT64 "llu"
//#define INT64  "lli"

#if __WORDSIZE == 64
#define XINT64 "lx"
#define UINT64 "lu"
#define INT64  "ld"
#define PTRTYPE  uint64_t
#define SPTRTYPE int64_t
#define PTRFMT  "lx"
#endif

#if __WORDSIZE == 32
#define XINT64 "llx"
#define UINT64 "llu"
#define INT64  "lld"
#define PTRTYPE  unsigned long //uint32_t
#define SPTRTYPE int32_t
#define PTRFMT  "lx"
#endif

#include <ctype.h>	// Log.h
#include <errno.h>	// Errno.h
#include <sys/errno.h>	// Errno.h
#include <stdarg.h>	// Log.h
#include <stdint.h>	// commonly included in include files
#include <stdio.h>	// commonly included in include files
#include <stdlib.h>	// commonly included in include files
#include <string.h>	// commonly included in include files
#include <unistd.h>	// commonly included in include files

#include "types.h"	// commonly included in includ files
#include "fctypes.h"	// commonly included in includ files
#include "hash.h"	// commonly included in includ files

#include "Errno.h"	// commonly included in include files
#include "Log.h"	// commonly included in include files

// cygwin fix
#ifndef O_ASYNC
#define O_ASYNC 0
#endif

#endif
