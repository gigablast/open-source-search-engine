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

#define MAX_MSG36_OUT 20

class Msg37 {

 public:

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "termIds/termFreqs" should NOT be on the stack in case we block
	bool getTermFreqs ( collnum_t collnum ,
			    int32_t        maxAge     ,
			    int64_t  *termIds    ,
			    int32_t        numTermIds ,
			    int64_t  *termFreqs  ,
			    void       *state      ,
			    void (* callback)(void *state ) ,
			    int32_t        niceness   , // = MAX_NICENESS
			    bool        exactCount );

	bool launchRequests ( ) ;

	// leave public so C wrapper can call
	bool gotTermFreq ( Msg36 *msg36 ) ;

	// we can get up to MAX_QUERY_TERMS term frequencies at the same time
	Msg36 m_msg36 [ MAX_MSG36_OUT ]; // [ MAX_QUERY_TERMS ];
	char  m_inUse [ MAX_MSG36_OUT ];
	int32_t  m_i;

	// . ptr to "termFreqs" passed in by getTermFreqs[]
	// . we remember it so we can set them
	int64_t *m_termFreqs;
	int32_t       m_numTerms;

	int32_t m_numReplies  ;
	int32_t m_numRequests ;
	int32_t m_errno;

	void *m_state ;
	void ( * m_callback ) ( void *state );

	int32_t  m_niceness;

	bool  m_exactCount;

	collnum_t m_collnum;

	int32_t        m_maxAge;
	int64_t  *m_termIds ;
};

#endif
