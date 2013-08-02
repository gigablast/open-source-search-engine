// Matt Wells, copyright Dec 2008

#ifndef _SCRAPER_H_
#define _SCRAPER_H_

#include "Url.h"   // MAX_COLL_LEN
#include "XmlDoc.h"

#define MAX_SCRAPES_OUT 1

class Scraper {

 public:


	Scraper  ( );
	~Scraper ( );
 
	bool init ( );
	void wakeUp ( ) ;
	void gotPhrase ( ) ;
	//bool gotPages ( long i, TcpSocket *s ) ;
	//bool addedScrapedSites ( long i ) ;
	//bool gotUrlInfo ( long i ) ;
	//bool wrapItUp ( );
	bool indexedDoc ( );

	bool scrapeProCog();
	
	char m_coll[MAX_COLL_LEN];
	long m_numReceived;
	long m_numSent;

	long  m_qtype;
	//Url   m_urls[MAX_SCRAPES_OUT];
	//long  m_numUrls;

	//Msg9a m_msg9a[MAX_SCRAPES_OUT];

	//char  m_buf[50000];
	//char *m_bufPtr;
	//char *m_bufEnd;

	char  m_registered;

	XmlDoc m_xd;

	//long    m_numOutlinks;
	//Links   m_links;
	//MsgE    m_msge;
	//RdbList m_list;
	//Msg4    m_msg4;
};

extern class Scraper g_scraper;

#endif
