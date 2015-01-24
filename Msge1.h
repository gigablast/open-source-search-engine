// Gigablast Inc., copyright November 2007

#define MAX_OUTSTANDING_MSGE1 20

#ifndef _MSGE1_H_
#define _MSGE1_H_

#include "MsgC.h"
#include "Linkdb.h"
#include "Tagdb.h"

class Msge1 {

public:

	Msge1();
	~Msge1();
	void reset();

	// . this gets the ip from "firstip" tag in "grv" array of TagRecs
	//   if it is there. otherwise, it will do an ip lookup.
	// . this will also consult the stored files on disk if this is the
	//   "test" collection to avoid any dns lookups whatsoever, unless
	//   that file is not present
	// . the purpose of this is to just get "firstips" for XmlDoc 
	//   to set the SpiderRequest::m_firstIp member which is used to
	//   determine which hostId is exclusively responsible for 
	//   doling/throttling these urls/requests out to other hosts to
	//   spider them. see Spider.h/.cpp for more info
	bool getFirstIps ( class TagRec **grv                   ,
			   char        **urlPtrs                ,
			   linkflags_t  *urlFlags               ,
			   int32_t          numUrls                ,
			   // if urlFlags[i]&LF_OLDLINK is true, skip it
			   bool          skipOldLinks           ,
			   char         *coll                   ,
			   int32_t          niceness               ,
			   void         *state                  ,
			   void        (*callback)(void *state) ,
			   int32_t          nowGlobal              ,
			   bool          addTags                ,
			   char         *testDir                );

	bool launchRequests ( int32_t starti ) ;

	bool sendMsgC      ( int32_t i , char *host , int32_t hlen );
	bool doneSending   ( int32_t i );
	bool addTag        ( int32_t i );
	bool doneAddingTag ( int32_t i );

	char *m_coll      ;
	int32_t  m_niceness  ;

	char **m_urlPtrs;
	linkflags_t *m_urlFlags;
	int32_t   m_numUrls;
	bool   m_addTags;

	char   m_skipOldLinks;

	// buffer to hold all the data we accumulate for all the urls in urlBuf
	char *m_buf;
	int32_t  m_bufSize;

	// sub-buffers of the great "m_buf", where we store the data for eacu
	// url that we get in urlBuf
	int32_t        *m_ipBuf;
	int32_t        *m_ipErrors;

	int32_t  m_numRequests;
	int32_t  m_numReplies;
	int32_t  m_i;
	int32_t  m_n;

	// point to next url in "urlBuf" to process
	char *m_nextPtr;

	//Url   m_urls        [ MAX_OUTSTANDING_MSGE1 ]; 
	int32_t    m_ns          [ MAX_OUTSTANDING_MSGE1 ]; 
	char    m_used        [ MAX_OUTSTANDING_MSGE1 ]; 
	MsgC    m_msgCs       [ MAX_OUTSTANDING_MSGE1 ]; // ips
	//Msg9a   m_msg9as      [ MAX_OUTSTANDING_MSGE1 ]; // adding "firstip"

	// vector of TagRec ptrs
	TagRec **m_grv;
	

	void     *m_state;
	void    (*m_callback)(void *state);

	int32_t m_nowGlobal;

	char *m_testDir;

	// for errors
	int32_t      m_errno;
};

// utility functions
extern bool getTestIp ( char *url , int32_t *retIp , bool *found , int32_t niceness,
			char *testDir );
extern bool addTestIp ( char *host , int32_t hostLen , int32_t ip ) ;
extern bool saveTestBuf ( char *testDir ) ;

void resetTestIpTable ( ) ;

#endif
