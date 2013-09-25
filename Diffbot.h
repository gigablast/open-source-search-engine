
#ifndef DIFFBOT_H
#define DIFFBOT_H

// values for the diffbot dropdown
#define DBA_NONE 1
#define DBA_ALL  2
#define DBA_ARTICLE_FORCE 3
#define DBA_ARTICLE_AUTO  4
#define DBA_PRODUCT_FORCE 5
#define DBA_PRODUCT_AUTO 6
#define DBA_IMAGE_FORCE 7
#define DBA_IMAGE_AUTO 8
#define DBA_FRONTPAGE_FORCE 9 
#define DBA_FRONTPAGE_AUTO 10

// add new fields to END of list since i think we store the
// field we use as a number in the coll.conf, starting at 0
extern char *g_diffbotFields [];

bool printCrawlBotPage ( TcpSocket *s , HttpRequest *hr );

bool printCrawlBotPage2 ( TcpSocket *s , 
			  HttpRequest *hr ,
			  char fmt,
			  SafeBuf *injectionResponse ,
			  SafeBuf *urlUploadResponse ) ;

//bool handleDiffbotRequest ( TcpSocket *s , HttpRequest *hr ) ;
bool sendBackDump ( TcpSocket *s,HttpRequest *hr );

bool getSpiderRequestMetaList ( char *doc , SafeBuf *listBuf ) ;

#endif
