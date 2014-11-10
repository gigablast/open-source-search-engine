#ifndef __GB_INCLUDE_H__
#define __GB_INCLUDE_H__

//#define memcpy memcpy_ass
//#define memset memset_ass

#include <inttypes.h>
#define UINT32 PRIu32
#define INT32  PRId32

#define UINT64 PRIu64
#define INT64  PRId64

#define XINT64 PRIX64
#define XINT32 PRIX32

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
