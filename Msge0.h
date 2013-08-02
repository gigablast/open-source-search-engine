// Gigablast Inc., copyright November 2007

#define MAX_OUTSTANDING_MSGE0 20

#ifndef _MSGE_H_
#define _MSGE_H_

#include "Tagdb.h"
#include "Linkdb.h"

class Msge0 {

public:

	Msge0();
	~Msge0();
	void reset();

	bool getTagRecs ( char        **urlPtrs      ,
			  linkflags_t  *urlFlags     ,
			  long          numUrls      ,
			  bool          skipOldLinks ,
			  class TagRec *baseTagRec ,
			  char         *coll         ,
			  long          niceness     ,
			  void         *state        ,
			  void (*callback)(void *state) ) ;

	TagRec *getTagRec   ( long i ) { return m_tagRecPtrs[i]; };

	bool launchRequests ( long starti ) ;
	bool sendMsg8a      ( long i );
	bool doneSending    ( long i );

	char *m_coll      ;
	long  m_niceness  ;

	char **m_urlPtrs;
	linkflags_t *m_urlFlags;
	long   m_numUrls;

	char   m_skipOldLinks;

	// buffer to hold all the data we accumulate for all the urls in urlBuf
	char *m_buf;
	long  m_bufSize;

	long   m_slabNum;
	char **m_slab;
	char  *m_slabPtr;
	char  *m_slabEnd;

	class TagRec *m_baseTagRec;

	// sub-buffers of the great "m_buf", where we store the data for eacu
	// url that we get in urlBuf
	long        *m_tagRecErrors;
	TagRec     **m_tagRecPtrs;
	long        *m_numTags;

	long  m_numRequests;
	long  m_numReplies;
	long  m_i;
	long  m_n;

	// point to next url in "urlBuf" to process
	char *m_nextPtr;

	Url     m_urls        [ MAX_OUTSTANDING_MSGE0 ]; 
	long    m_ns          [ MAX_OUTSTANDING_MSGE0 ]; 
	char    m_used        [ MAX_OUTSTANDING_MSGE0 ]; 
	Msg8a   m_msg8as      [ MAX_OUTSTANDING_MSGE0 ]; //for getting tag bufs
	//TagRec  m_tagRecs   [ MAX_OUTSTANDING_MSGE0 ];

	void     *m_state;
	void    (*m_callback)(void *state);

	// for errors
	long      m_errno;
};

#endif
