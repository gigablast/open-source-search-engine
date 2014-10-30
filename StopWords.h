// Matt Wells, copyright Jul 2001

#ifndef _STOPWORDS_H_
#define _STOPWORDS_H_

#include "Unicode.h"

// . this returns true if h is the hash of an ENGLISH stop word
// . list taken from www.superjournal.ac.uk/sj/application/demo/stopword.htm 
// . stop words with "mdw" next to them are ones I added
bool isStopWord ( char *s , long len , int64_t h ) ;

// used by Synonyms.cpp
bool isStopWord2 ( int64_t *h ) ;

bool isStopWord32 ( long h ) ;

//just a stub for now
//bool isStopWord ( UChar *s , long len , int64_t h );


// . damn i forgot to include these above
// . i need these so m_bitScores in IndexTable.cpp doesn't have to require
//   them! Otherwise, it's like all queries have quotes around them again...
bool isQueryStopWord ( char *s , long len , int64_t h ) ;
//bool isQueryStopWord ( UChar *s , long len , int64_t h ) ;

// is it a COMMON word?
long isCommonWord ( int64_t h ) ;

long isCommonQueryWordInEnglish ( int64_t h ) ;

bool initWordTable(class HashTableX *table, char* words[], long size ,
		   char *label);

bool isVerb ( int64_t *hp ) ;

// for Process.cpp::resetAll() to call when exiting to free all mem
void resetStopWordTables();

extern HashTableX s_table32;

#endif
