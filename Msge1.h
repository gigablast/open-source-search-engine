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
			   long          numUrls                ,
			   // if urlFlags[i]&LF_OLDLINK is true, skip it
			   bool          skipOldLinks           ,
			   char         *coll                   ,
			   long          niceness               ,
			   void         *state                  ,
			   void        (*callback)(void *state) ,
			   long          nowGlobal              ,
			   bool          addTags                ,
			   char         *testDir                );

	bool launchRequests ( long starti ) ;

	bool sendMsgC      ( long i , char *host , long hlen );
	bool doneSending   ( long i );
	bool addTag        ( long i );
	bool doneAddingTag ( long i );

	char *m_coll      ;
	long  m_niceness  ;

	char **m_urlPtrs;
	linkflags_t *m_urlFlags;
	long   m_numUrls;
	bool   m_addTags;

	char   m_skipOldLinks;

	// buffer to hold all the data we accumulate for all the urls in urlBuf
	char *m_buf;
	long  m_bufSize;

	// sub-buffers of the great "m_buf", where we store the data for eacu
	// url that we get in urlBuf
	long        *m_ipBuf;
	long        *m_ipErrors;

	long  m_numRequests;
	long  m_numReplies;
	long  m_i;
	long  m_n;

	// point to next url in "urlBuf" to process
	char *m_nextPtr;

	//Url   m_urls        [ MAX_OUTSTANDING_MSGE1 ]; 
	long    m_ns          [ MAX_OUTSTANDING_MSGE1 ]; 
	char    m_used        [ MAX_OUTSTANDING_MSGE1 ]; 
	MsgC    m_msgCs       [ MAX_OUTSTANDING_MSGE1 ]; // ips
	//Msg9a   m_msg9as      [ MAX_OUTSTANDING_MSGE1 ]; // adding "firstip"

	// vector of TagRec ptrs
	TagRec **m_grv;
	

	void     *m_state;
	void    (*m_callback)(void *state);

	long m_nowGlobal;

	char *m_testDir;

	// for errors
	long      m_errno;
};

// utility functions
extern bool getTestIp ( char *url , long *retIp , bool *found , long niceness,
			char *testDir );
extern bool addTestIp ( char *host , long hostLen , long ip ) ;
extern bool saveTestBuf ( char *testDir ) ;

void resetTestIpTable ( ) ;

#endif
