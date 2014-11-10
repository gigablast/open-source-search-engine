#include "UdpServer.h"
//#include "HttpServer.h"
#include "Stats.h"
#include "AutoBan.h"
#include "Pages.h"
#include "UdpProtocol.h"
#include "PingServer.h"
//#include "Msg3c.h"
#include <sys/resource.h>  // setrlimit

#define MAX_STRIPES 8

class Proxy {
 public:
	Proxy();
	~Proxy();
	
	void reset();

	bool initHttpServer ( uint16_t httpPort, 
			      uint16_t httpsPort );

	bool initProxy ( int32_t proxyId,
			 uint16_t udpPort,
			 uint16_t udpPort2,
			 UdpProtocol *dp );

	bool handleRequest ( TcpSocket *s );

	bool forwardRequest ( class StateControl *stC );//, class Host *h ) ;

	//bool verifiedDataFeed();

	void gotReplyPage ( void *state, class UdpSlot *slot );//TcpSocket *s);
	
	Host *pickBestHost ( class StateControl *stC );

	bool isProxyRunning () {return m_proxyRunning;};
	// are we a proxy?
	bool isProxy        () {return m_proxyRunning;};
	bool sendFile ( TcpSocket *s, HttpRequest *r );

	//bool insertLoginBarDirective( SafeBuf *sb ) ;
	char *storeLoginBar ( char *reply , 
			      int32_t replySize , 
			      int32_t replyAllocSize,
			      int32_t mimeLen,
			      int32_t *newReplySize ,
			      class HttpRequest *hr);//char *currentUrl );

	//pages.getUser needs to know if we're proxy to display the admintop
	//and main.cpp needs to set this so that it can stop the proxy
	bool m_proxyRunning;
	//Msg3c *m_msg3c;
	//bool   m_verifiedDataFeed;
	// protected:
	void printRequest (TcpSocket *s, HttpRequest *r,
			   uint64_t took = 0,
			   char *content = NULL ,
			   int32_t contentLen = 0 );
	//HttpRequest  m_r;
	//TcpSocket   *m_s;
	int32_t m_proxyId;
	//number of requests outstanding per hosts
	int32_t m_numOutstanding[MAX_HOSTS];
	//last host to which we sent the request
	int32_t m_lastHost;
	//host to which we pass the index page and the addurl page
	int32_t m_mainHost;

	// assume no more than 8 stripes for now
	int32_t m_stripeLastHostId   [MAX_STRIPES];
	// how many query terms are outstanding on this stripe
	int32_t m_termsOutOnStripe   [MAX_STRIPES];
	int32_t m_queriesOutOnStripe [MAX_STRIPES];
	int32_t m_nextStripe;

	////////
	//
	// USER ACCOUNTING STUFF
	//
	///////

	class UserInfo *getUserInfoForFeedAccess ( class HttpRequest *hr );
	bool hasAccessPermission ( class StateControl *stC );
	int32_t getAccessType ( HttpRequest *hr );
	float getPrice ( int32_t accessType ) ;
	bool addAccessPoint ( class StateControl *stC ,int64_t nowms,
				     int32_t httpStatus );
	bool addAccessPoint2 ( class UserInfo *ui , 
			       char accessType ,
			       int64_t nowms ,
			       int64_t startTime ) ;
	class SummaryRec *getSummaryRec ( int32_t userId32 , char accessType );
	class UserInfo *getUserInfoFromId ( int32_t userId32 ) ;
	class UserInfo *getLoggedInUserInfo2 ( HttpRequest *hr,
					       TcpSocket *socket,
					      SafeBuf *errmsg );
	class UserInfo *getLoggedInUserInfo ( class StateUser *su, 
					      SafeBuf *errmsg );
	bool doesUsernameExist ( char *user );
	bool printAccountingInfoPage ( class StateUser *su ,
				       class SafeBuf *errmsg = NULL );
	bool gotGif ( class StateUser *su );
	bool printDepositTable ( SafeBuf *sb , int32_t userId32 );
	int32_t getNextTransactionId ( ) ;
	bool hitCreditCard ( class StateUser *su );
	bool gotDepositDoc ( class StateUser *su );
	bool printEditForm ( class StateUser *su );
	bool saveUserBufs();
	bool loadUserBufs();
	void printUsers ( SafeBuf *sb ) ;

	// we save these buffers to disk. this is the all important acct info
	SafeBuf m_userInfoBuf;
	SafeBuf m_sumBuf;
	SafeBuf m_depositBuf;
	HashTableX m_srht;
};

extern Proxy g_proxy;
