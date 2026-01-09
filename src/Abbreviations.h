// Matt Wells, copyright Jul 2001

// i guess _ABBREVIATIONS_H_ is reserved, so prepend _GB
#ifndef _GB_ABBREVIATIONS_H_
#define _GB_ABBREVIATIONS_H_

#include "Unicode.h"

// . is the word with this word id an abbreviation?
// . word id is just the hash64() of the word
bool isAbbr ( int64_t wid , bool *hasWordAfter = NULL ) ;

// to free the table's memory, Process::reset() will call this
void resetAbbrTable ( ) ;

#endif
