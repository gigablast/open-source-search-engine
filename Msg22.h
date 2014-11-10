// Matt Wells, copyright Jul 2001

// . get a TitleRec from url/coll or docId

#ifndef _MSG22_H_
#define _MSG22_H_

#include "Url.h"
#include "Multicast.h"

// m_url[0]!=0 if this is a url-based request and NOT docid-based
class Msg22Request {
public:
	int64_t m_docId;
	int32_t      m_niceness;
	int32_t      m_maxCacheAge;
	collnum_t m_collnum;
	char      m_justCheckTfndb  :1;
	char      m_getAvailDocIdOnly:1;
	char      m_doLoadBalancing :1;
	char      m_addToCache      :1;
	char      m_inUse           :1;
	char      m_url[MAX_URL_LEN+1];

	int32_t getSize () {
		return (m_url - (char *)&m_docId) + 1+gbstrlen(m_url); };
	int32_t getMinSize() {
		return (m_url - (char *)&m_docId) + 1; };

	Msg22Request() { m_inUse = 0; }
};

class Msg22 {

 public:
	Msg22();
	~Msg22();

	static bool registerHandler ( ) ;

	bool getAvailDocIdOnly ( class Msg22Request  *r              ,
				 int64_t preferredDocId ,
				 char *coll ,
				 void *state ,
				 void (* callback)(void *state) ,
				 int32_t niceness ) ;

	// . make sure you keep url/coll on your stack cuz we just point to it
	// . see the other getTitleRec() description below for more details
	// . use a maxCacheAge of 0 to avoid the cache
	bool getTitleRec ( class Msg22Request *r ,
			   char      *url     ,
			   int64_t  docId   ,
			   char      *coll    ,
			   char     **titleRecPtrPtr  ,
			   int32_t      *titleRecSizePtr ,
			   bool       justCheckTfndb ,
			   bool       getAvailDocIdOnly  ,
			   void      *state          , 
			   void     (* callback) (void *state ),
			   int32_t       niceness       ,
			   bool       addToCache     ,
			   int32_t       maxCacheAge    ,
			   int32_t       timeout        ,
			   bool       doLoadBalancing = false );

	int64_t getAvailDocId ( ) { return m_availDocId; };

	// public so C wrappers can call
	void gotReply ( ) ;

	// this is a hack so Msg38 can store his this ptr here
	//void *m_parent; // used by Msg38
	//int32_t  m_slot;   // for resending on same Msg22 slot in array
	//void *m_dataPtr;// for holding recepient record ptr of TopNode ptr

	char **m_titleRecPtrPtr;
	int32_t  *m_titleRecSizePtr;

	void    (* m_callback ) (void *state);
	void     *m_state       ;
	//void     *m_state2      ;
	//void     *m_state3      ;

	bool      m_found;
	int64_t m_availDocId;
	// the error getting the title rec is stored here
	int32_t      m_errno;

	bool m_outstanding ;

	// for sending the Msg22
	Multicast m_mcast;

	class Msg22Request *m_r;
};

#endif
