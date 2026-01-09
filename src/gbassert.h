#ifndef OPEN_SOURCE_SEARCH_ENGINE_GBASSERT_H
#define OPEN_SOURCE_SEARCH_ENGINE_GBASSERT_H

#include "Log.h"

#include <signal.h>

#define gbassert_false(expr) \
((expr) ?                                                                             \
        (void) (log(LOG_LOGIC, "%s:%d: %s: gb Negative Assertion `%s` failed\n",     \
                        __FILE__, __LINE__, __FUNCTION__, #expr), raise(SIGSEGV)) :   \
        (void) 0)
#define gbassert(expr) \
((expr) ?                                                                             \
        (void) 0 :                                                                    \
        (void) (log(LOG_LOGIC, "%s:%d: %s: gb Assertion `%s` failed\n",              \
                        __FILE__, __LINE__, __FUNCTION__, #expr), raise(SIGSEGV)))

#endif //OPEN_SOURCE_SEARCH_ENGINE_GBASSERT_H
