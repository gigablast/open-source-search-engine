// Matt Wells copyright Jan 9, 2002

// ad fetcher and parser

#ifndef _ADS_H_
#define _ADS_H_

#include "Url.h"
#include "SafeBuf.h"
//#include "CollectionRec.h"

#define MAX_BLOB_SIZE 2048

#define MAX_ADS          20
#define MAX_FEEDS        4
#define MAX_AD_QUERY_LEN 1024

#define PI_PRIMARY      0
#define PI_BACKUP       1
#define SS_PRIMARY      2
#define SS_BACKUP       3


class Ads {
 public:

	 Ads();		// constructor
	~Ads();		// destructor

        bool getAds ( char           *q         , 
                      int32_t            qlen      ,
                      int32_t            pageNum   ,
                      int32_t            queryIP   ,
                      char           *coll      ,
                      void           *state     ,
                      void          (*callback)(void *state) );
	// . returns false if blocks, true otherwise
	// . sets g_errno on error
	// . "q" is the NULL terminated query
	// . "ip" is the ip of the user performing the query
		
	int32_t  getNumPaidInclusionAds ( ) { return m_numAds[m_indexPIAds]; };
	int32_t  getNumSkyscraperAds    ( ) { return m_numAds[m_indexSSAds]; };
        bool  hasAds                 ( ) { 
                return (bool)(m_numAds[m_indexPIAds] || m_numAds[m_indexSSAds]);
        };
        bool  gotAllRequests         () {
                return (bool)(m_numGotAds >= MAX_FEEDS);
        };

        void  printPaidInclusionAds(SafeBuf *sb, int32_t numCharPerLine );
        void  printSkyscraperAds   (SafeBuf *sb, int32_t numCharPerLine );

	// need to keep public so wrapper can call
        void  gotDoc( class TcpSocket *ts, int32_t index );
        void  selectDisplayAds( );
	void   *m_state;
	void  (*m_callback)(void *state);

        //static void initCollAvailAds(              );
        //static void setAvailableAds ( char *coll   );
        //static int32_t getAdFeedIndex  ( collnum_t cn );

 private:
        bool getAd ( int32_t            index     ,
                     char           *cgi       ,
                     int32_t            numAds    );
        void printAd( SafeBuf *sb    , 
                      char *url      , int32_t urlLen,
                      char *title    , int32_t titleLen,
                      char *desc     , int32_t descLen,
                      char *site     , int32_t siteLen,
                      int32_t  numCharPerLine );

        char *m_coll;
        int32_t  m_queryIP;      
        int32_t  m_pageAds;
        int32_t  m_feedIndex;
        int32_t  m_indexPIAds;
        int32_t  m_indexSSAds;
        int32_t  m_numGotAds;
        bool  m_adSSSameasPI;
        bool  m_adBSSSameasBPI;
        CollectionRec *m_cr;

        char  m_q        [ MAX_AD_QUERY_LEN ];
        int32_t  m_qlen;
	char *m_titles   [MAX_FEEDS][ MAX_ADS ];
	int32_t  m_titlesLen[MAX_FEEDS][ MAX_ADS ];
	char *m_desc     [MAX_FEEDS][ MAX_ADS ];
	int32_t  m_descLen  [MAX_FEEDS][ MAX_ADS ];
	char *m_sites    [MAX_FEEDS][ MAX_ADS ];
	int32_t  m_sitesLen [MAX_FEEDS][ MAX_ADS ];
	char *m_urls     [MAX_FEEDS][ MAX_ADS ];
	int32_t  m_urlsLen  [MAX_FEEDS][ MAX_ADS ];
	int32_t  m_numAds   [MAX_FEEDS];
	char *m_buf      [MAX_FEEDS];
	int32_t  m_bufLen   [MAX_FEEDS];
	//Url m_url      [MAX_FEEDS];
	Xml   m_xml      [MAX_FEEDS];

        //static int32_t s_availableAds[16][MAX_AD_FEEDS];
        //static int32_t s_numAvailableAds[16];
};

struct AdFeed {
        Ads  *m_ads;
        int32_t  m_index;
};

#endif
