#ifndef ENTITIES_H__
#define ENTITIES_H__

// Matt Wells, copyright Jul 2001

// . i only support characters from 0-255
// . i do not plan on having unicode support, but may think about it later
// . Partap seems like a chump...I'll probably make him do it

//#include "TermTable.h"  // for hashing entity encodings
#include "Unicode.h"

// call these two
// JAB: const-ness for the optimizer
long getEntity_a          ( char *s , long maxLen , uint32_t *c );

//long getEntity_utf8 (char *s , long maxLen , long *d , long *ds ) ;

#endif
