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
			  int32_t          numUrls      ,
			  bool          skipOldLinks ,
			  class TagRec *baseTagRec ,
			  collnum_t  collnum,
			  int32_t          niceness     ,
			  void         *state        ,
			  void (*callback)(void *state) ) ;

	TagRec *getTagRec   ( int32_t i ) { return m_tagRecPtrs[i]; };

	bool launchRequests ( int32_t starti ) ;
	bool sendMsg8a      ( int32_t i );
	bool doneSending    ( int32_t i );

	collnum_t m_collnum;
	int32_t  m_niceness  ;

	char **m_urlPtrs;
	linkflags_t *m_urlFlags;
	int32_t   m_numUrls;

	char   m_skipOldLinks;

	// buffer to hold all the data we accumulate for all the urls in urlBuf
	char *m_buf;
	int32_t  m_bufSize;

	int32_t   m_slabNum;
	char **m_slab;
	char  *m_slabPtr;
	char  *m_slabEnd;

	class TagRec *m_baseTagRec;

	// sub-buffers of the great "m_buf", where we store the data for eacu
	// url that we get in urlBuf
	int32_t        *m_tagRecErrors;
	TagRec     **m_tagRecPtrs;
	int32_t        *m_numTags;

	int32_t  m_numRequests;
	int32_t  m_numReplies;
	int32_t  m_i;
	int32_t  m_n;

	// point to next url in "urlBuf" to process
	char *m_nextPtr;

	Url     m_urls        [ MAX_OUTSTANDING_MSGE0 ]; 
	int32_t    m_ns          [ MAX_OUTSTANDING_MSGE0 ]; 
	char    m_used        [ MAX_OUTSTANDING_MSGE0 ]; 
	Msg8a   m_msg8as      [ MAX_OUTSTANDING_MSGE0 ]; //for getting tag bufs
	//TagRec  m_tagRecs   [ MAX_OUTSTANDING_MSGE0 ];

	void     *m_state;
	void    (*m_callback)(void *state);

	// for errors
	int32_t      m_errno;
};

#endif
