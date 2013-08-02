// Matt Wells, copyright Jul 2001

#ifndef _ABBREVIATIONS_H_
#define _ABBREVIATIONS_H_

#include "Unicode.h"

// . is the word with this word id an abbreviation?
// . word id is just the hash64() of the word
bool isAbbr ( long long wid , bool *hasWordAfter = NULL ) ;

// to free the table's memory, Process::reset() will call this
void resetAbbrTable ( ) ;

#endif
