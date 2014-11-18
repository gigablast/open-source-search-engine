#include "gb-include.h"

#include "Ads.h"
#include "HttpServer.h"
#include "Xml.h"
#include "Pages.h"
#include "Titledb.h"

// . lets get feeds from:
// 1. overture
// 2. findwhat
// 3. looksmart
// 4. about
// 5. ah-ha (http://www.ah-ha.com/partners/partners_xmlv4_integration.asp)

static void gotDocWrapper( void *state , TcpSocket *s ) ;

char *removeCDATA ( char *buf, int32_t *bufLen ) {
	if(*bufLen >= 12 && strncasecmp(buf, "<![CDATA[", 9) == 0 ) {
		buf     += 9;
		*bufLen -= 12;
        }
        return buf;
}


// . Gigablast Ad Server feed implementation
//   Sends a search request on the ad collection to a remote/separate ad server
//   Ad server sends back XML encoded results.  
// . gotDoc parses and places into arrays for use when printing.  
// . printAd is used to write the ads into html, called from PageResults.cpp.

Ads::Ads(){
        m_indexPIAds   = PI_PRIMARY;
        m_indexSSAds   = SS_PRIMARY;
        m_adSSSameasPI = false;
        m_adBSSSameasBPI = false;
        
        memset(m_bufLen   , 0, sizeof(m_bufLen)   );
        memset(m_buf      , 0, sizeof(m_buf)      );
        memset(m_numAds   , 0, sizeof(m_numAds)   );
        memset(m_urls     , 0, sizeof(m_urls)     );
        memset(m_urlsLen  , 0, sizeof(m_urlsLen)  );
        memset(m_titles   , 0, sizeof(m_titles)   );
        memset(m_titlesLen, 0, sizeof(m_titlesLen));
        memset(m_desc     , 0, sizeof(m_desc)     );
        memset(m_descLen  , 0, sizeof(m_descLen)  );
        memset(m_sites    , 0, sizeof(m_sites)    );
        memset(m_sitesLen , 0, sizeof(m_sitesLen) );
}


Ads::~Ads() {
        for(int32_t i = 0; i < MAX_FEEDS; i++) {
                if( m_buf[i] )
                        mfree( m_buf[i], m_bufLen[i], "Ads-fSktXml" );
                m_bufLen[i] = 0   ;
                m_buf   [i] = NULL;
        }
}
// . This gets ads from the Gigablast Ad Server
// . Request Parameters (* = required):
//    URL: http://<adserverIP>.<adserverPort>/search
//   *q=<query>
//   *n=<num results>
//   *s=<start number>
//   *raw=<what format, should always be 11>
// . Fromatted Response:
//   <reponse>
//   	<hits>LONG LONG</hits>
//	<result>
//		<title><![CDATA[STRING]]></title>
//		<body>
//			<text><![CDATA[STRING]]></text>
//			<url><![CDATA[STRING]]></url>
//		</body>
//		<link><![CDATA[STRING]]</link>
//	</result>
//      ...
//   </response>
//


// . This gets ads from 3rd party ad provider: searchfeed.com
//   formerly miva
// . Request Parameters (* = required):
//    URL: http://www.searchfeed.com/rd/feed/XMLFeed.jsp
//   *trackID=<unique to acct>
//   *pID=<partner id>
//    cat=<url encoded query>
//    nl=<num results>
//    page=<page results, like our s/n>
//   *ip=<querying IP>
//    excID=<hides ads from this advertiserID> (N/A currently)
// . Formatted Response:
// <Listings>
//       <Page></Page>
//       <Count></Count>
//       <Listing>
//              <Title></Title>
//              <URL></URL>
//              <URI></URI>
//              <Description></Description>
//              <Bid></Bid>
//       </Listing>
//       <Listing>
//              <Title></Title>
//              <URL></URL>
//              <URI></URI>
//              <Description> </Description>
//              <Bid></Bid>
//       </Listing>
//       ...
// </Listings>
//



// . This gets ads from 3rd party ad provider: 7search.com
// . Request Parameters (* = required):
//    URL: http://meta.7search.com/feed/xml.aspx
//   *affiliate=<acct ID>
//   *qu=<url encoded query>
//    pn=<page results, like our s>
//    r=<num results>
//    filter=<adult filter on(yes)/off(no)>
//   *ip_address=<querying IP>
//   *st=<search type, our querys are "typein", can do "link">
//   *rid=<full URL where search came from>
// . Formatted Response:
// <?xml version="1.0" encoding="ISO-8859-1" standalone="yes"?>
// <results>
//      <site index="1">
//              <url><![CDATA[link to end website]]></url>
//              <name><![CDATA[title of above link]]></name>
//              <description><![CDATA[advertiser description]]></description>
//              <bid><![CDATA[total bid amount before revenue share]]></bid>
//              <httplink><![CDATA[advertisers actual url, NO LINK]]></httplink>
//      </site>
//      <site index="2">
//      ...
//      </site>
// </results>
//

bool Ads::getAds ( char           *q         , 
		   int32_t            qlen      ,
                   int32_t            pageNum   ,
                   int32_t            queryIP   ,
                   char           *coll      ,
                   void           *state     ,
                   void          (*callback)(void *state) ){

        // bail if no collection specified
        if(!coll) {
                log("query: Collection for ads is empty.");
                return true;
        }
	// bail if query too big
	if ( qlen > MAX_AD_QUERY_LEN) {
		log("query: Query length of %"INT32" is too long to get "
		    "ads for.",qlen); 
		return true;
	}
	// bail if empty, too
	if ( ! q || qlen <= 0 ) return true;
	// save stuff
        strncpy(m_q, q, MAX_AD_QUERY_LEN);
        m_qlen         = qlen;
        m_coll         = coll;
	m_state        = state;
	m_callback     = callback;
        m_pageAds      = pageNum;
        m_queryIP      = queryIP;
        m_cr           = g_collectiondb.getRec(m_coll);
        
        // . this means that it was not specifically mentioned, choose next
        //   in round robin
        /*
        if(!feedIndex)
                m_feedIndex = getAdFeedIndex(g_collectiondb.getCollnum(m_coll));
        else
                // . an ad feed was specified, so use this one instead.
                m_feedIndex = feedIndex - 1;
        */
        int32_t numPIAds = m_cr->m_adPINumAds;
        int32_t numBPIAds = m_cr->m_adPINumAds;

        if(!m_cr->m_adSSSameasPI) {
                size_t size = gbstrlen(m_cr->m_adCGI[0]);
                if((int32_t)size == gbstrlen(m_cr->m_adCGI[2]))
                        if(!memcmp(m_cr->m_adCGI[0], m_cr->m_adCGI[2], size))
                                m_adSSSameasPI = true;
        }
        else
                m_adSSSameasPI = true;

        if(!m_cr->m_adBSSSameasBPI) {
                size_t size = gbstrlen(m_cr->m_adCGI[1]);
                if((int32_t)size == gbstrlen(m_cr->m_adCGI[3]))
                        if(!memcmp(m_cr->m_adCGI[1], m_cr->m_adCGI[3], size))
                                m_adBSSSameasBPI = true;
        }
        else
                m_adBSSSameasBPI = true;

        if(m_adSSSameasPI && m_cr->m_adPIEnable && m_cr->m_adSSEnable) {
                numPIAds += m_cr->m_adSSNumAds;
                m_indexSSAds = PI_PRIMARY;
        }
        else
                m_adSSSameasPI = false;

        if(m_adBSSSameasBPI && m_cr->m_adPIEnable && m_cr->m_adSSEnable) {
                numBPIAds += m_cr->m_adSSNumAds;
        }
        else
                m_adBSSSameasBPI = false;

        // get paid inclusion ad and its backup
        m_numGotAds = 0;
        if(m_cr->m_adPIEnable) {
                if(getAd(PI_PRIMARY, m_cr->m_adCGI[PI_PRIMARY], numPIAds))
                         m_numGotAds++;
                if(getAd(PI_BACKUP, m_cr->m_adCGI[PI_BACKUP], numBPIAds))
                         m_numGotAds++;
        }
        else
                m_numGotAds += 2;

        // get skyskraper ad and its backup
        if(m_cr->m_adSSEnable) {
                // if skyscraper is not using the same feed as paid inclusion
                // get ads
                if(!m_adSSSameasPI) { 
                        if(getAd(SS_PRIMARY, m_cr->m_adCGI[SS_PRIMARY], 
                                 m_cr->m_adSSNumAds))
                                m_numGotAds++;
                }
                else
                        m_numGotAds++;

                // if backup skyscraper is not using the same feed as backup
                // paid inclusion get ads
                if(!m_adBSSSameasBPI) {
                        if(getAd(SS_BACKUP, m_cr->m_adCGI[SS_BACKUP], 
                                 m_cr->m_adSSNumAds))
                                m_numGotAds++;
                }
                else
                        m_numGotAds++;
        }
        else
                m_numGotAds += 2;

        // if we got all of them back already, return true...we failed
        if(m_numGotAds >= 4)
                return true;
        return false;
}


bool Ads::getAd ( int32_t            index     ,
                  char           *cgi       ,
                  int32_t            numAds    ) {

        char  tmp[1024] = {0};
        int32_t  clen      = gbstrlen(cgi);

        // if there is no cgi string, bail
        if(clen <= 0)
                return true;
        // if string is greater than buffer, bail
        if(clen > 1024) {
                log("query: Ad Feed CGI string %"INT32" is > 1024.", index);
                return true;
        }

        char *p    = tmp;
        int32_t  plen = htmlDecode(tmp, cgi, clen,false,0);

        SafeBuf sb(1024);

        // . replace standard characters with our search values
        while(*p) {
                char *beg = p;
                p = strstr(p, "%");
                if(p) {
                        p++;
                        if( beg != p )
                                sb.safeMemcpy(beg, (int32_t)(p-beg-1));
                        switch(*p) {
                                // insert url encoded query
                                case 'q':
                                        sb.urlEncode(m_q, m_qlen);
                                        break;
                                // insert page number
                                case 'p':
                                        sb.safePrintf("%"INT32"", m_pageAds);
                                        break;
                                // insert number of ads to return
                                case 'n':
                                        sb.safePrintf("%"INT32"", numAds);
                                        break;
                                // insert querying IP
                                case 'i':
                                        sb.safePrintf("%s", iptoa(m_queryIP));
                                        break;
                                // insert %
                                case '%':
                                        sb.safePrintf("%%");
                                        break;
                                default :
                                        break;
                        }
                        p++;
                }
                else {
                        p = tmp;
                        int32_t len = plen - (beg-p);
                        sb.safeMemcpy(beg, len);
                        sb.pushChar('\0');
                        break;
                }
        }

        log(LOG_DEBUG, "query: Ad feed request[%"INT32"] url is: %s", 
            index, sb.getBufStart());
	// make it a url
        //m_url[index].set(sb.getBufStart(), sb.length(), false /*addWWW?*/);
	// get the url
	char *url = sb.getBufStart();
        AdFeed *af = (AdFeed *)mmalloc(sizeof(AdFeed), "AdFeed");
        if(!af) {
                log("query: Ad feed malloc failed.");
                return true;
        }
        af->m_ads   = this;
        af->m_index = index;
	// get the xml
	if (!g_httpServer.getDoc( url          , // &m_url[index] ,
				 0             , // ip
				 0             , // offset
				 -1            , // size 
				 0             , // if mod since
				 af            , // state
				 gotDocWrapper,
				 m_cr->m_adFeedTimeOut, // declared in collRec
				 0             , // proxyIp --> none
				 0             , // proxyPort
				 20*1024       , // 20k maxTextDocLen
				 20*1024       ))// 20k maxOhterDocLen
		return false;
	// we got it w/o blocking,  MUST be an error!! g_errno should be set
	return true;
}


void gotDocWrapper ( void *state , TcpSocket *s ) {
	AdFeed *af = (AdFeed *)state;
        // extract our class
	Ads *ads = af->m_ads;
	ads->gotDoc(s, af->m_index);
        mfree(af, sizeof(AdFeed), "AdFeed");
        if(!ads->gotAllRequests())
                return;
        ads->selectDisplayAds();
	// call callback
	ads->m_callback ( ads->m_state );
}


void Ads::gotDoc ( TcpSocket *ts, int32_t index ) {

        m_numGotAds++;
	// return if g_errno set
	if ( g_errno )  {
                log("query: Ad feed returned an error: %s", mstrerror(g_errno));
                return;
        }
	// this is guaranteed now to be NULL terminated
	char *p    = ts->m_readBuf;
	int32_t  plen = ts->m_readBufSize;
	int32_t  len  = ts->m_readOffset;
	if ( ! p || plen <= 0 ) {
		log("query: Ad feed gave empty reply.");
		return;
	}

	HttpMime mime;
	mime.set(p, len, NULL);//&m_url[index]);
	
        if(mime.getHttpStatus() != 200) {
                log("query: Ad feed returned %"INT32" status, bailing.", 
                    mime.getHttpStatus());
                return;
        }

	char *content    = p + mime.getMimeLen();
	int32_t  contentLen = mime.getContentLen();
        if((contentLen == -1) && (len != 0))
                contentLen = len - mime.getMimeLen();
	
        log(LOG_DEBUG, "query: Ad feed response: %s", content);

        int16_t charset = get_iana_charset( mime.getCharset(), 
					  mime.getCharsetLen() );
	// sanity check
	if ( charset != csUTF8 && charset != csASCII ) { char *xx=NULL;*xx=0; }

        Xml *xml = &m_xml[index];
	xml->set( content, contentLen, 
		  false, 0, true, TITLEREC_CURRENT_VERSION);
        
        char *rsltStr     = m_cr->m_adResultXml[index];
        char *titleStr    = m_cr->m_adTitleXml [index];
        char *urlStr      = m_cr->m_adUrlXml   [index];
        char *descStr     = m_cr->m_adDescXml  [index];
        char *linkStr     = m_cr->m_adLinkXml  [index];
        int32_t  rsltStrLen  = gbstrlen(rsltStr);

	int32_t      begPtr     = 0;
	int32_t      endPtr     = xml->getNumNodes();

        char **titles    = m_titles   [index];
        int32_t  *titlesLen = m_titlesLen[index];
        char **desc      = m_desc     [index];
        int32_t  *descLen   = m_descLen  [index];
        char **sites     = m_sites    [index];
        int32_t  *sitesLen  = m_sitesLen [index];
        char **urls      = m_urls     [index];
        int32_t  *urlsLen   = m_urlsLen  [index];
        int32_t   numAds    = 0;

	while(begPtr < endPtr) {
		// . We will loop until we are out of nodes and should break
		// . Search for result tag
		begPtr = xml->getNodeNum(begPtr, endPtr, rsltStr, rsltStrLen);
		// . If there is no response.result, consider parsing finished
		if( begPtr == -1 ) break;
		// . We don't want to use the back result tags, so skip it
		if( xml->isBackTag( begPtr++ ) ) continue;
		// . save pointer to ad title
		titles[numAds] = xml->getString(begPtr, endPtr, titleStr, 
                                                &titlesLen[numAds]);
                titles[numAds] =removeCDATA(titles[numAds],&titlesLen[numAds]);
		// . save pointers for summary lines
		desc[numAds] = xml->getString(begPtr, endPtr, descStr, 
                                              &descLen[numAds]);
                desc[numAds] = removeCDATA(desc[numAds], &descLen[numAds]);
		// . save pointer for site
		sites[numAds] = xml->getString(begPtr, endPtr, linkStr, 
                                               &sitesLen[numAds]);
                sites[numAds] = removeCDATA(sites[numAds], &sitesLen[numAds]);
		// . save pointer to click url
		urls[numAds] = xml->getString(begPtr, endPtr, urlStr, 
                                              &urlsLen[numAds]);
                urls[numAds] = removeCDATA(urls[numAds], &urlsLen[numAds]);
		// . increment number of ads found
                numAds++;
	}
	log(LOG_DEBUG, "query: Ad feed[%"INT32"] returned %"INT32" ads on page %"INT32"",
            index, numAds, m_pageAds); 
        // . let mem table know we stole the buffer as well
	if( !relabel( p, plen, "Ads-sktXml" ) ) {
		log("query: Could not relabel ad memory from socket!");
		return;
	}
	// . we are now going to free the socket buffer when we are done, 
	//   so let socket know
        m_buf       [index] = p;
        m_bufLen    [index] = plen;
        m_numAds    [index] = numAds;
	ts->m_readBuf = NULL;
}


void Ads::selectDisplayAds( ) {
        
        if(m_adSSSameasPI) {  
                // Same primary AND backup AND backup has MORE
                if(m_adBSSSameasBPI && 
                   m_numAds[PI_PRIMARY] < m_numAds[PI_BACKUP]) {
                        m_indexPIAds = PI_BACKUP;
                        m_indexSSAds = PI_BACKUP;
                }
                // Same primary AND different backup AND backup has MORE
                else if(!m_adBSSSameasBPI &&
                        (m_numAds[PI_PRIMARY] < 
                         (m_numAds[PI_BACKUP] + m_numAds[SS_BACKUP]))) {
                        m_indexPIAds = PI_BACKUP;
                        m_indexSSAds = SS_BACKUP;
                }
        }
        else if(!m_adSSSameasPI && m_adBSSSameasBPI) {
                // Different primary AND same backup AND backup has MORE
                if(m_numAds[PI_BACKUP] > 
                   (m_numAds[PI_PRIMARY] + m_numAds[SS_PRIMARY])) {
                        m_indexPIAds = PI_BACKUP;
                        m_indexSSAds = PI_BACKUP;
                }
        }
        else {
                // Different primary AND different backup
                // AND backup has MORE
                if(!m_numAds[PI_PRIMARY] && m_numAds[PI_BACKUP])
                        m_indexPIAds = PI_BACKUP;
                // AND backup has MORE
                if(!m_numAds[SS_PRIMARY] && m_numAds[SS_BACKUP])
                        m_indexSSAds = SS_BACKUP;
        }
}


void Ads::printAd( SafeBuf *sb    , 
                   char *url      , int32_t urlLen,
                   char *title    , int32_t titleLen,
                   char *desc     , int32_t descLen,
                   char *site     , int32_t siteLen,
                   int32_t  numCharPerLine ) {
	sb->safePrintf( "<a href=" );
	sb->safeMemcpy (url, urlLen);
	sb->safePrintf( "><b>" );
	sb->safeMemcpy (title, titleLen);
	sb->safePrintf( "</b></a><br>\n" ); 

	sb->safeMemcpy (desc, descLen);
        sb->safePrintf( "<br>\n" );

	sb->safePrintf( "<b>" );
        int32_t len        = siteLen;
        int32_t numToPrint = numCharPerLine;
        int32_t numSplits  = len / numCharPerLine;
        int32_t index      = 0;
        if(len % numCharPerLine)
                numSplits++;
        if(numToPrint > len) 
                numToPrint = len;
	sb->safeMemcpy (site, numToPrint);

        for(int32_t i = 1; i < numSplits; i++) {
                sb->safePrintf("<br>");
                index += numCharPerLine;
                if(numCharPerLine*(i+1) > len) {
                        numToPrint = len % numCharPerLine;
                }

		sb->safeMemcpy (site+index, numToPrint);
        }
	sb->safePrintf( "</b><br>\n" );
}


void Ads::printPaidInclusionAds(SafeBuf *sb, int32_t numCharPerLine) {
        int32_t end = m_numAds[m_indexPIAds];
        if((m_indexPIAds == PI_PRIMARY && m_adSSSameasPI  ) ||
           (m_indexPIAds == PI_BACKUP  && m_adBSSSameasBPI)   )
                end = m_cr->m_adPINumAds;

        for( int32_t i = 0; i < end; i++) {
                printAd(sb, 
                        m_urls  [m_indexPIAds][i], m_urlsLen  [m_indexPIAds][i],
                        m_titles[m_indexPIAds][i], m_titlesLen[m_indexPIAds][i],
                        m_desc  [m_indexPIAds][i], m_descLen  [m_indexPIAds][i],
                        m_sites [m_indexPIAds][i], m_sitesLen [m_indexPIAds][i], numCharPerLine );
                if(i != end-1) 
                        sb->safePrintf("<br>\n");
        }
}


void Ads::printSkyscraperAds   (SafeBuf *sb, int32_t numCharPerLine) {
        int32_t i = 0;
        int32_t end = m_numAds[m_indexSSAds];
        if(m_indexSSAds == PI_PRIMARY || m_indexSSAds == PI_BACKUP) {
                i = m_cr->m_adPINumAds;
                end += i;
        }

        for( ; i < end; i++) {
                printAd(sb, 
                        m_urls  [m_indexSSAds][i], m_urlsLen  [m_indexSSAds][i],
                        m_titles[m_indexSSAds][i], m_titlesLen[m_indexSSAds][i],
                        m_desc  [m_indexSSAds][i], m_descLen  [m_indexSSAds][i],
                        m_sites [m_indexSSAds][i], m_sitesLen [m_indexSSAds][i],
			numCharPerLine);
                if(i != end-1) 
                        sb->safePrintf("<br>\n");
        }
}

/*
int32_t Ads::s_availableAds[16][MAX_AD_FEEDS] = {{0}};
int32_t Ads::s_numAvailableAds[16]            =  {0} ;


void Ads::initCollAvailAds ( ) {
        for(collnum_t cn = g_collectiondb.getFirstCollnum(); 
            cn >= 0;
            cn = g_collectiondb.getNextCollnum(cn) ) 
               setAvailableAds(g_collectiondb.getCollName(cn));
}


void Ads::setAvailableAds ( char *coll ) {
        CollectionRec *cr = g_collectiondb.getRec(coll);

        s_numAvailableAds[cr->m_collnum] = 0;

        for(int32_t i = 0; i < MAX_AD_FEEDS; i++) {
                if(cr->m_adFeedEnable[i]) {
                        if(cr->m_adCGI[i][0] != '\0')
                                s_availableAds[cr->m_collnum]
                                        [s_numAvailableAds[cr->m_collnum]++]=i;
                }
        }
}


int32_t Ads::getAdFeedIndex ( collnum_t cn ) {
        static int32_t count = 0;
        
        if(!s_numAvailableAds[cn])
                return 0;
        
        count++;
        count %= s_numAvailableAds[cn];
        return s_availableAds[cn][count];
}
*/
