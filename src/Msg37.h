// Matt Wells, copyright Jul 2001

// . get the number of records (termId/docId/score tuple) in an IndexList(s)
//   for a given termId(s)
// . if it's truncated then interpolate based on score
// . used for query routing
// . used for query term weighting (IDF)
// . used to set m_termFreq for each termId in query in the Query class

#ifndef _MSG37_H_
#define _MSG37_H_

#include "Msg36.h"
#include "Query.h"  // MAX_QUERY_TERMS

#endif
