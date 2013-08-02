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

	bool initHttpServer ( unsigned short httpPort, 
			      unsigned short httpsPort );

	bool initProxy ( long proxyId,
			 unsigned short udpPort,
			 unsigned short udpPort2,
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
			      long replySize , 
			      long replyAllocSize,
			      long mimeLen,
			      long *newReplySize ,
			      class HttpRequest *hr);//char *currentUrl );

	//pages.getUser needs to know if we're proxy to display the admintop
	//and main.cpp needs to set this so that it can stop the proxy
	bool m_proxyRunning;
	//Msg3c *m_msg3c;
	//bool   m_verifiedDataFeed;
	// protected:
	void printRequest (TcpSocket *s, HttpRequest *r,
			   unsigned long long took = 0,
			   char *content = NULL ,
			   long contentLen = 0 );
	//HttpRequest  m_r;
	//TcpSocket   *m_s;
	long m_proxyId;
	//number of requests outstanding per hosts
	long m_numOutstanding[MAX_HOSTS];
	//last host to which we sent the request
	long m_lastHost;
	//host to which we pass the index page and the addurl page
	long m_mainHost;

	// assume no more than 8 stripes for now
	long m_stripeLastHostId   [MAX_STRIPES];
	// how many query terms are outstanding on this stripe
	long m_termsOutOnStripe   [MAX_STRIPES];
	long m_queriesOutOnStripe [MAX_STRIPES];
	long m_nextStripe;

	////////
	//
	// USER ACCOUNTING STUFF
	//
	///////

	class UserInfo *getUserInfoForFeedAccess ( class HttpRequest *hr );
	bool hasAccessPermission ( class StateControl *stC );
	long getAccessType ( HttpRequest *hr );
	float getPrice ( long accessType ) ;
	bool addAccessPoint ( class StateControl *stC ,long long nowms,
				     long httpStatus );
	bool addAccessPoint2 ( class UserInfo *ui , 
			       char accessType ,
			       long long nowms ,
			       long long startTime ) ;
	class SummaryRec *getSummaryRec ( long userId32 , char accessType );
	class UserInfo *getUserInfoFromId ( long userId32 ) ;
	class UserInfo *getLoggedInUserInfo2 ( HttpRequest *hr,
					       TcpSocket *socket,
					      SafeBuf *errmsg );
	class UserInfo *getLoggedInUserInfo ( class StateUser *su, 
					      SafeBuf *errmsg );
	bool doesUsernameExist ( char *user );
	bool printAccountingInfoPage ( class StateUser *su ,
				       class SafeBuf *errmsg = NULL );
	bool gotGif ( class StateUser *su );
	bool printDepositTable ( SafeBuf *sb , long userId32 );
	long getNextTransactionId ( ) ;
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
