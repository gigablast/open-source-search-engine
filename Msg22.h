// Matt Wells, copyright Jul 2001

// . get a TitleRec from url/coll or docId

#ifndef _MSG22_H_
#define _MSG22_H_

#include "Url.h"
#include "Multicast.h"

// m_url[0]!=0 if this is a url-based request and NOT docid-based
class Msg22Request {
public:
	long long m_docId;
	long      m_niceness;
	long      m_maxCacheAge;
	collnum_t m_collnum;
	char      m_justCheckTfndb  :1;
	char      m_doLoadBalancing :1;
	char      m_addToCache      :1;
	char      m_inUse           :1;
	char      m_url[MAX_URL_LEN+1];

	long getSize () {
		return (m_url - (char *)&m_docId) + 1+gbstrlen(m_url); };
	long getMinSize() {
		return (m_url - (char *)&m_docId) + 1; };
};

class Msg22 {

 public:
	Msg22();
	~Msg22();

	static bool registerHandler ( ) ;

	// . make sure you keep url/coll on your stack cuz we just point to it
	// . see the other getTitleRec() description below for more details
	// . use a maxCacheAge of 0 to avoid the cache
	bool getTitleRec ( class Msg22Request *r ,
			   char      *url     ,
			   long long  docId   ,
			   char      *coll    ,
			   char     **titleRecPtrPtr  ,
			   long      *titleRecSizePtr ,
			   bool       justCheckTfndb ,
			   void      *state          , 
			   void     (* callback) (void *state ),
			   long       niceness       ,
			   bool       addToCache     ,
			   long       maxCacheAge    ,
			   long       timeout        ,
			   bool       doLoadBalancing = false );

	long long getAvailDocId ( ) { return m_availDocId; };

	// public so C wrappers can call
	void gotReply ( ) ;

	// this is a hack so Msg38 can store his this ptr here
	//void *m_parent; // used by Msg38
	//long  m_slot;   // for resending on same Msg22 slot in array
	//void *m_dataPtr;// for holding recepient record ptr of TopNode ptr

	char **m_titleRecPtrPtr;
	long  *m_titleRecSizePtr;

	void    (* m_callback ) (void *state);
	void     *m_state       ;
	//void     *m_state2      ;
	//void     *m_state3      ;

	bool      m_found;
	long long m_availDocId;
	// the error getting the title rec is stored here
	long      m_errno;

	bool m_outstanding ;

	// for sending the Msg22
	Multicast m_mcast;

	class Msg22Request *m_r;
};

#endif
