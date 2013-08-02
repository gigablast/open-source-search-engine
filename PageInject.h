#ifndef _PAGEINJECT_H_
#define _PAGEINJECT_H_    

#include "XmlDoc.h"
#include "Users.h"

class Msg7 {

public:

	bool       m_needsSet;
	XmlDoc     m_xd;
	TcpSocket *m_socket;
	char       m_coll[MAX_COLL_LEN+1];
	char       m_pwd[32];
	bool       m_quickReply;
	char       m_username[MAX_USER_SIZE];
	char       m_url[MAX_URL_LEN];
	SafeBuf    m_sb;
	bool       m_isScrape;
	char *m_content;
	long  m_contentAllocSize;
	SafeBuf m_qbuf;
	char m_round;
	char m_useAhrefs;
	char m_injectLinks;
	HashTableX m_linkDedupTable;

	Msg7 ();
	~Msg7 ();

	bool scrapeQuery ( );

	bool inject ( TcpSocket *s , 
		      HttpRequest *r ,
		      void *state ,
		      void (*callback)(void *state));

	bool inject ( char *url ,
		      long  forcedIp ,
		      char *content ,
		      long  contentLen ,
		      bool  recycleContent,
		      uint8_t contentType,
		      char *coll ,
		      bool  quickReply ,
		      char *username ,
		      char *pwd ,
		      long  niceness,
		      void *state ,
		      void (*callback)(void *state),
		      long firstIndexedDate = 0,
		      long spiderDate = 0,
		      long hopCount = -1 ,
		      char newOnly = 0 ,
		      short charset = -1 ,
		      char spiderLinks = 0 ,
		      char deleteIt = false ,
		      char hasMime = false ,
		      bool doConsistencyTesting = false );

};

extern bool g_inPageInject;

#endif
