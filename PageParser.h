#ifndef _PAGEPARSER_H_
#define _PAGEPARSER_H_

// global flag
extern bool g_inPageParser ;
extern bool g_inPageInject ;

#define PP_NICENESS 2

#include "XmlDoc.h"
#include "Pages.h"
#include "Unicode.h"
#include "Title.h"
#include "Pos.h"
#include "TopTree.h"

bool sendPageAnalyze ( TcpSocket *s , HttpRequest *r ) ;

bool sendPageParser2 ( TcpSocket    *s , 
		       HttpRequest  *r ,
		       class State8 *st ,
		       int64_t     docId ,
		       Query        *q ,
		       int64_t    *termFreqs       ,
		       float        *termFreqWeights ,
		       float        *affWeights ,
		       void         *state ,
		       void        (* callback)(void *state) ) ;

class State8 {
public:
	TopTree m_topTree;
	//Msg16 m_msg16;
	//Msg14 m_msg14;
	//Msg15 m_msg15;
	Msg22 m_msg22;
	SafeBuf m_dbuf;
	//XmlDoc m_doc;
	//Url   m_url;
	//Url   m_rootUrl;
	char *m_u;
	int32_t  m_ulen;
	bool  m_applyRulesetToRoot;
	char  m_rootQuality;
	int32_t  m_reparseRootRetries;
	char  m_coll[MAX_COLL_LEN];
	int32_t  m_collLen;
	//int32_t  m_sfn;
	//int32_t  m_urlLen;
	TcpSocket  *m_s;
	bool       m_isLocal;
	char  m_pwd[32];
	HttpRequest m_r;
	int32_t m_old;
	// recyle the link info from the title rec?
	int32_t m_recycle;
	// recycle the link info that was imported from another coll?
	int32_t m_recycle2;
	int32_t m_render;
	char m_recompute;
	int32_t m_oips;
	char m_linkInfoColl[11];
	//	char m_buf[16384 * 1024];

	//int32_t m_page;
	// m_pbuf now points to m_sbuf if we are showing the parsing junk
	SafeBuf  m_xbuf;
	SafeBuf  m_wbuf;
	bool m_donePrinting;
	//SafeBuf  m_sbuf;
	// this is a buffer which cats m_sbuf into it
	//SafeBuf  m_sbuf2;

	// new state vars for Msg3b.cpp
	int64_t  m_docId;
	void      *m_state ;
	void    (* m_callback) (void *state);
	Query      m_tq;
	Query     *m_q;
	int64_t *m_termFreqs;
	float     *m_termFreqWeights;
	float     *m_affWeights;
	//score_t    m_total;
	bool       m_freeIt;
	bool       m_blocked;

	// these are from rearranging the code
	int32_t      m_indexCode;
	//uint64_t m_chksum1;
	int64_t m_took1;
	int64_t m_took1b;
	int64_t m_took2;
	int64_t m_took3;

	char m_didRootDom;
	char m_didRootWWW;
	char m_wasRootDom;

	// call Msg16 with a versino of title rec to do
	int32_t m_titleRecVersion;
	
	char m_hopCount;

	//TitleRec m_tr;

	//XmlDoc m_oldDoc;
	XmlDoc m_xd;
};

#endif
