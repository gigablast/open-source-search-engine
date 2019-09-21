// Matt Wells, copyright Mar 2001

// . a class for parsing urls
// . used by many other classes

#ifndef  _URL_H_
#define  _URL_H_

#define MAX_URL_LEN 1024

// where should i put this #define? for now i'll keep it here
#define MAX_COLL_LEN  64

#include "ip.h"      // atoip ( s,len)

char *getPathFast  ( char *url );
char *getFilenameFast ( char *url , int32_t *filenameLen ) ;
char *getTLDFast   ( char *url , int32_t *tldLen  , bool hasHttp = true ) ;
char *getDomFast   ( char *url , int32_t *domLen  , bool hasHttp = true ) ;
bool  hasSubdomain ( char *url );
char *getHostFast  ( char *url , int32_t *hostLen , int32_t *port = NULL ) ;
bool  isPermalinky ( char *url );

bool isHijackerFormat ( char *url );

bool  isPingServer ( char *s ) ;

// . returns the host of a normalized url pointed to by "s"
// . i.e. "s" must start with the protocol (i.e. http:// or https:// etc.)
// . used by Links.cpp for fast parsing and SiteGetter.cpp too
char *getHost ( char *s , int32_t *hostLen ) ;

// . get the path end of a normalized url
// . used by SiteGetter.cpp
// . if num==0 just use "www.xyz.com" as site (the hostname)
// . if num==1 just use "www.xyz.com/foo/" as site
char *getPathEnd ( char *s , int32_t num );

int32_t getPathDepth ( char *s , bool hasHttp );

class Url {

public:

	void print  ();
	void reset  ();

	// set from another Url, does a copy
	void set ( Url *url , bool addWWW );

	void set    ( char *s ) { 
		if ( ! s ) { char *xx=NULL;*xx=0; }
		return set ( s , strlen(s) ); }

	void set ( Url *baseUrl , char *s ) {
		if ( ! s ) { char *xx=NULL;*xx=0; }
		set ( baseUrl , s , strlen(s) ); }

	// . "s" must be an ENCODED url
	void set    ( char *s , int32_t len , bool addWWW = false,
		      bool stripSessionIds = false , bool stripPound = false ,
		      bool stripCommonFile = false ,
		      int32_t titleRecVersion = 0x7fffffff );
	void set    ( Url *baseUrl , char *s , int32_t len , bool addWWW = false,
		      bool stripSessionIds = false , bool stripPound = false ,
		      bool stripCommonFile = false ,
		      int32_t titleRecVersion = 0x7fffffff );
	void setIp  ( int32_t ip ) { m_ip = ip; };

	char isSessionId ( char *hh, int32_t titleRecVersion ) ;

	// compare another url to us
	bool equals ( Url *u ) {
		if ( m_ulen != u->m_ulen ) return false;
		if ( strcmp(m_url,u->m_url) == 0 ) return true;
		return false;
	};

	// is the url's hostname actually in ip in disguise ("a.b.c.d")
	bool isIp   (); 

	// is the hostname an ip #?
	bool hasIp               () { return m_ip; }; // ip of 0 means none
	bool isRoot              ();
	// a super root url is a root url where the hostname is NULL or "www"
	bool isSuperRoot         (); 
	bool isCgi               () { return m_query ; };
	bool isExtensionIndexable(); // html, htm, cgi, asp, shtml, ...

	//returns True if the extension is in the list of 
	//badExtensions - extensions not to be parsed
	bool isBadExtension(int32_t xxx);
	bool isSet()            { return m_ulen != 0; }

	// is this url a warc or arc url? i.e. ends in .warc or .arc or
	// .warc.gz or .arc.gz?
	bool isWarc ( );
	bool isArc ( );

	// does it end in .xml, .rdb or .rss, etc. kinda thing
	//bool isRSSFormat ( ) ;

	// is it http://rpc.weblogs.com/int16_tChanges.xml, etc.?
	bool isPingServer ( ) ;

	void setPort             (uint16_t port ) { m_port = port; };

	int32_t getSubUrlLen        (int32_t i);
	int32_t getSubPathLen       (int32_t i);

	int32_t getPort             () { return m_port;};
	int32_t getIp               () { return m_ip; };
	int32_t getIpDomain         () { return ipdom(m_ip); };

	char *getUrl         () { return m_url;};
	char *getUrlEnd      () { return m_url + m_ulen;};
	char *getScheme      () { return m_scheme;};
	char *getHost        () { return m_host;};
	char *getDomain      () { return m_domain;};
	char *getTLD         () { return m_tld; };
	char *getMidDomain   () { return m_domain; }; // w/o the tld
	char *getPath        () { return m_path;};
	char *getFilename    () { return m_filename;};
	char *getExtension   () { return m_extension;};
	char *getQuery       () { return m_query;};
	char *getIpString    () { return iptoa ( m_ip ); };
	char *getAnchor      () { return m_anchor;};
	//char *getSite         () {return m_site;};
	char *getPortStr     () { return m_portStr; }
	int32_t  getUrlLen         () { return m_ulen;};
	int32_t  getSchemeLen      () { return m_slen;};
	int32_t  getHostLen        () { return m_hlen;};
	int32_t  getDomainLen      () { return m_dlen;};
	int32_t  getPathLen        () { return m_plen;};
	char *getPathEnd        () { return m_path + m_plen; };
	int32_t  getFilenameLen    () { return m_flen;};
	int32_t  getExtensionLen   () { return m_elen;};
	int32_t  getQueryLen       () { return m_qlen;};
	int32_t  getTLDLen         () { return m_tldLen; };
	int32_t  getMidDomainLen   () { return m_mdlen;};
	int32_t  getPortLen        () { return m_portLen;};
	int32_t  getAnchorLen      () { return m_anchorLen;};
	int32_t  getDefaultPort    () { return m_defPort;};
	//int32_t  getSiteLen         () {return m_siteLen;};
	int32_t  getPathLenWithCgi () {
		if ( ! m_query ) return m_plen;	return m_plen + 1 + m_qlen; };
	bool  isHttp            () { 
		if ( m_ulen  < 4 ) return false;
		if ( m_slen != 4 ) return false;
		if ( m_scheme[0] != 'h' ) return false;
		if ( m_scheme[1] != 't' ) return false;
		if ( m_scheme[2] != 't' ) return false;
		if ( m_scheme[3] != 'p' ) return false;
		return true;
	};
	bool  isHttps           () { 
		if ( m_ulen  < 5 ) return false;
		if ( m_slen != 5 ) return false;
		if ( m_scheme[0] != 'h' ) return false;
		if ( m_scheme[1] != 't' ) return false;
		if ( m_scheme[2] != 't' ) return false;
		if ( m_scheme[3] != 'p' ) return false;
		if ( m_scheme[4] != 's' ) return false;
		return true;
	};


	// . are we a site root?
	// . i.e. does this url == hometown.com/users/fred/ , etc.
	// . does not take into account whether we have a subdomain or domain
	//bool isSiteRoot(char *coll,
	//		class TagRec *tagRec = NULL ,
	//		char **retSite=NULL,
	//		int32_t *retSiteLen=NULL);

	// . returns the site and sets *siteLen
	// . returns NULL and sets g_errno on error
	// . returns NULL without g_errno set if our domain is invalid
	// . sets "*isDefault" to true if we just returned the default site,
	//   otherwise false
	//char *getSite ( int32_t *siteLen , char *coll , 
	//		bool defaultToHostname , 
	//		class TagRec *tagRec = NULL ,
	//		bool *isDefault = NULL );

	// used by buzz i guess
	//int32_t  getSiteHash32   ( char *coll );
	int32_t      getUrlHash32    ( ) ;
	int32_t      getHostHash32   ( ) ;
	int32_t      getDomainHash32 ( ) ;

	// if url is xyz.com then get hash of www.xyz.com
	int32_t getHash32WithWWW ( );

	int64_t getUrlHash64    ( ) ;
	int64_t getHostHash64   ( ) ;
	int64_t getDomainHash64   ( ) ;

	int64_t getUrlHash48    ( ) {
		return getUrlHash64() & 0x0000ffffffffffffLL; }

	bool hasMediaExtension ( ) ;

	// . store url w/o http://
	// . without trailing / if path is just "/"
	// . without "www." if in hostname and "rmWWW" is true
	// . returns length
	// . if "buf" is NULL just returns the int16_thand-form length
	char *getShorthandUrl    ( bool rmWWW , int32_t *len );

	// count the path components (root url as 0 path components)
	int32_t  getPathDepth ( bool countFilename ); // = false );

	// get path component #num. starts at 0.
	char *getPathComponent ( int32_t num , int32_t *clen );
	//char *getPathEnd       ( int32_t num );

	// is our hostname "www" ?
	bool isHostWWW ( ) ;

	bool hasSubdomain() { return (m_dlen != m_hlen); };

	// is it xxx.com/* or www.xxx.com/* (CAUTION: www.xxx.yyy.com)
	bool isSimpleSubdomain();

	// spam means dirty/porn
	bool isDirty () { return isSpam(); };

	// is the url a porn/spam url?
	bool isSpam();

	// this is private
	bool isSpam ( char *s , int32_t slen ) ;


	// . detects crazy repetetive urls like this:
	//   http://www.pittsburghlive.com:8000/x/tribune-review/opinion/
	//   steigerwald/letters/send/archive/letters/send/archive/bish/
	//   archive/bish/letters/bish/archive/lettes/send/archive/letters/...
	// . The problem is they use a relative href link on the page when they
	//   should us an absolute and the microsoft web server will still
	//   give the content they meant to give!
	// . this is called by Msg14.cpp to not even spider such urls, and we
	//   also have some even better detection logic in Links.cpp which
	//   is probably more accurate than this function.
	bool isLinkLoop();

	static uint32_t unitTests();
	static char* getDisplayUrl(char* url, SafeBuf* sb);

	// private:

	char    m_url[MAX_URL_LEN]; // the normalized url
	int32_t    m_ulen;

	// points into "url" (http, ftp, mailto, ...)(all lowercase)
	char   *m_scheme;           
	int32_t    m_slen;

	// points into "url" (a.com, www.yahoo.com, 1.2.3.4, ...)(allLowercase)
	char   *m_host;             
	int32_t    m_hlen;

	// it's 0 if we don't have one
	int32_t    m_ip;  

	// points into "url" (/  /~mwells/  /a/b/ ...) (always ends in /)
	char   *m_path;             
	int32_t    m_plen;

	// points into "url" (a=hi+there, ...)
	char   *m_query;            
	int32_t    m_qlen;

	// points into "url" (html, mpg, wav, doc, ...)
	char   *m_extension;        
	int32_t    m_elen;

	// (a.html NULL index.html) (can be NULL)
	char   *m_filename;         
	int32_t    m_flen;

	char   *m_domain;
	int32_t    m_dlen;

	char   *m_tld;
	int32_t    m_tldLen;

	// char *m_midDomain equals m_domain
	int32_t    m_mdlen;

	// (80, 8080, 8000, ...)
	int32_t    m_port;             
	int32_t    m_defPort;
	int32_t    m_portLen;
	char   *m_portStr;

	// anchor
	char   *m_anchor;
	int32_t    m_anchorLen;
	
	// Base site url
	//char *m_site;
	//int32_t m_siteLen;
};

#endif

	
