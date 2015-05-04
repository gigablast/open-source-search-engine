#ifndef GBINJECT_H
#define GBINJECT_H    

void  handleRequest7Import ( class UdpSlot *slot , int32_t netnice ) ;

void  handleRequest7 ( class UdpSlot *slot , int32_t netnice ) ;

bool sendPageInject ( class TcpSocket *s, class HttpRequest *hr );

bool resumeImports ( ) ;

// called by Process.cpp
void saveImportStates ( ) ;

#include "XmlDoc.h"
#include "Users.h"
#include "Parms.h" // GigablastRequest


class InjectionRequest {
 public:

	int32_t   m_injectDocIp;
	char      m_injectLinks;
	char      m_spiderLinks;
	char      m_shortReply;
	char      m_newOnly;
	char      m_deleteUrl;
	char      m_recycle;
	char      m_dedup;
	char      m_hasMime;
	char      m_doConsistencyTesting;
	char      m_getSections;
	char      m_gotSections;
	int32_t   m_charset;
	int32_t   m_hopCount;
	collnum_t m_collnum; // more reliable than m_coll
	uint32_t  m_firstIndexed;
	uint32_t  m_lastSpidered;

	char *ptr_url;
	char *ptr_queryToScrape;
	char *ptr_contentDelim;
	char *ptr_contentTypeStr;
	char *ptr_content;
	char *ptr_diffbotReply; // secret thing from dan

	int32_t size_url;
	int32_t size_queryToScrape;
	int32_t size_contentDelim;
	int32_t size_contentTypeStr;
	int32_t size_content;
	int32_t size_diffbotReply; // secret thing from dan

	// serialized space for the ptr_* strings above
	char m_buf[0];
};


class Msg7 {

public:

	//GigablastRequest m_gr;

	//SafeBuf m_injectUrlBuf;
	//bool m_firstTime;
	//char *m_start;
	//bool  m_fixMe;
	//char  m_saved;
	//int32_t  m_injectCount;
	//bool m_isDoneInjecting;

	bool       m_needsSet;
	XmlDoc    *m_xd;
	TcpSocket *m_socket;
	SafeBuf    m_sb;
	char m_round;
	char m_useAhrefs;
	HashTableX m_linkDedupTable;

	SafeBuf m_sbuf; // for holding entire titlerec for importing

	void *m_state;
	void (* m_callback )(void *state);

	//int64_t m_hackFileOff;
	//int32_t m_hackFileId;

	//int32_t m_crawlbotAPI;

	class ImportState *m_importState;

	//void constructor();
	Msg7 ();
	~Msg7 ();
	//bool m_inUse;

	void reset();

	bool scrapeQuery ( );

	// msg7request m_req7 must be valid
	//bool inject ( char *coll,
	//	      char *proxiedUrl,
	//	      int32_t  proxiedUrlLen,
	//	      char *content,
	//	      void *state ,
	//	      void (*callback)(void *state) );

	// msg7request m_req7 must be valid
	// bool inject2 ( void *state , */
	// 	       void (*callback)(void *state) ); */


	//bool injectTitleRec ( void *state ,
	//		      void (*callback)(void *state) ,
	//		      class CollectionRec *cr );

	//void gotMsg7Reply ();

};

extern bool g_inPageInject;

#endif
