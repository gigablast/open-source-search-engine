
#ifndef CRAWLBOT_H
#define CRAWLBOT_H

bool printCrawlDetails2(class SafeBuf *sb,class CollectionRec *cx,char format);

bool printCrawlDetailsInJson ( class SafeBuf *sb , class CollectionRec *cx ) ;

bool printCrawlDetailsInJson ( class SafeBuf *sb , class CollectionRec *cx, int version ) ;

// values for the diffbot dropdown
/*
#define DBA_NONE 0
#define DBA_ALL  1
#define DBA_ARTICLE_FORCE 2
#define DBA_ARTICLE_AUTO  3
#define DBA_PRODUCT_FORCE 4
#define DBA_PRODUCT_AUTO 5
#define DBA_IMAGE_FORCE 6
#define DBA_IMAGE_AUTO 7
#define DBA_FRONTPAGE_FORCE 8
#define DBA_FRONTPAGE_AUTO 9

// add new fields to END of list since i think we store the
// field we use as a number in the coll.conf, starting at 0
extern char *g_diffbotFields [];
*/

bool sendPageCrawlbot ( class TcpSocket *s , class HttpRequest *hr );

//bool handleDiffbotRequest ( TcpSocket *s , HttpRequest *hr ) ;
bool sendBackDump ( class TcpSocket *s, class HttpRequest *hr );

bool getSpiderRequestMetaList ( char *doc, 
				class SafeBuf *listBuf, 
				bool spiderLinks,
				class CollectionRec *cr);

#endif
