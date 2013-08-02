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
char *getFilenameFast ( char *url , long *filenameLen ) ;
char *getTLDFast   ( char *url , long *tldLen  , bool hasHttp = true ) ;
char *getDomFast   ( char *url , long *domLen  , bool hasHttp = true ) ;
bool  hasSubdomain ( char *url );
char *getHostFast  ( char *url , long *hostLen , long *port = NULL ) ;
bool  isPermalinky ( char *url );

bool isHijackerFormat ( char *url );

bool  isPingServer ( char *s ) ;

// . returns the host of a normalized url pointed to by "s"
// . i.e. "s" must start with the protocol (i.e. http:// or https:// etc.)
// . used by Links.cpp for fast parsing and SiteGetter.cpp too
char *getHost ( char *s , long *hostLen ) ;

// . get the path end of a normalized url
// . used by SiteGetter.cpp
// . if num==0 just use "www.xyz.com" as site (the hostname)
// . if num==1 just use "www.xyz.com/foo/" as site
char *getPathEnd ( char *s , long num );

long getPathDepth ( char *s , bool hasHttp );

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
	void set    ( char *s , long len , bool addWWW = false,
		      bool stripSessionIds = false , bool stripPound = false ,
		      bool stripCommonFile = false ,
		      long titleRecVersion = 0x7fffffff );
	void set    ( Url *baseUrl , char *s , long len , bool addWWW = false,
		      bool stripSessionIds = false , bool stripPound = false ,
		      bool stripCommonFile = false ,
		      long titleRecVersion = 0x7fffffff );
	void setIp  ( long ip ) { m_ip = ip; };

	char isSessionId ( char *hh, long titleRecVersion ) ;

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
	bool isBadExtension(long int);
	bool isSet()            { return m_ulen != 0; }

	// does it end in .xml, .rdb or .rss, etc. kinda thing
	//bool isRSSFormat ( ) ;

	// is it http://rpc.weblogs.com/shortChanges.xml, etc.?
	bool isPingServer ( ) ;

	void setPort             (unsigned short port ) { m_port = port; };

	long getSubUrlLen        (long i);
	long getSubPathLen       (long i);

	long getPort             () { return m_port;};
	long getIp               () { return m_ip; };
	long getIpDomain         () { return ipdom(m_ip); };

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
	long  getUrlLen         () { return m_ulen;};
	long  getSchemeLen      () { return m_slen;};
	long  getHostLen        () { return m_hlen;};
	long  getDomainLen      () { return m_dlen;};
	long  getPathLen        () { return m_plen;};
	char *getPathEnd        () { return m_path + m_plen; };
	long  getFilenameLen    () { return m_flen;};
	long  getExtensionLen   () { return m_elen;};
	long  getQueryLen       () { return m_qlen;};
	long  getTLDLen         () { return m_tldLen; };
	long  getMidDomainLen   () { return m_mdlen;};
	long  getPortLen        () { return m_portLen;};
	long  getAnchorLen      () { return m_anchorLen;};
	long  getDefaultPort    () { return m_defPort;};
	//long  getSiteLen         () {return m_siteLen;};
	long  getPathLenWithCgi () {
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
	//		long *retSiteLen=NULL);

	// . returns the site and sets *siteLen
	// . returns NULL and sets g_errno on error
	// . returns NULL without g_errno set if our domain is invalid
	// . sets "*isDefault" to true if we just returned the default site,
	//   otherwise false
	//char *getSite ( long *siteLen , char *coll , 
	//		bool defaultToHostname , 
	//		class TagRec *tagRec = NULL ,
	//		bool *isDefault = NULL );

	// used by buzz i guess
	//long  getSiteHash32   ( char *coll );
	long      getUrlHash32    ( ) ;
	long      getHostHash32   ( ) ;
	long      getDomainHash32 ( ) ;

	long long getUrlHash64    ( ) ;
	long long getHostHash64   ( ) ;
	long long getDomainHash64   ( ) ;

	long long getUrlHash48    ( ) {
		return getUrlHash64() & 0x0000ffffffffffffLL; }

	// . store url w/o http://
	// . without trailing / if path is just "/"
	// . without "www." if in hostname and "rmWWW" is true
	// . returns length
	// . if "buf" is NULL just returns the shorthand-form length
	char *getShorthandUrl    ( bool rmWWW , long *len );

	// count the path components (root url as 0 path components)
	long  getPathDepth ( bool countFilename = false );

	// get path component #num. starts at 0.
	char *getPathComponent ( long num , long *clen );
	//char *getPathEnd       ( long num );

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
	bool isSpam ( char *s , long slen ) ;

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

	// private:

	char    m_url[MAX_URL_LEN]; // the normalized url
	long    m_ulen;

	// points into "url" (http, ftp, mailto, ...)(all lowercase)
	char   *m_scheme;           
	long    m_slen;

	// points into "url" (a.com, www.yahoo.com, 1.2.3.4, ...)(allLowercase)
	char   *m_host;             
	long    m_hlen;

	// it's 0 if we don't have one
	long    m_ip;  

	// points into "url" (/  /~mwells/  /a/b/ ...) (always ends in /)
	char   *m_path;             
	long    m_plen;

	// points into "url" (a=hi+there, ...)
	char   *m_query;            
	long    m_qlen;

	// points into "url" (html, mpg, wav, doc, ...)
	char   *m_extension;        
	long    m_elen;

	// (a.html NULL index.html) (can be NULL)
	char   *m_filename;         
	long    m_flen;

	char   *m_domain;
	long    m_dlen;

	char   *m_tld;
	long    m_tldLen;

	// char *m_midDomain equals m_domain
	long    m_mdlen;

	// (80, 8080, 8000, ...)
	long    m_port;             
	long    m_defPort;
	long    m_portLen;
	char   *m_portStr;

	// anchor
	char   *m_anchor;
	long    m_anchorLen;
	
	// Base site url
	//char *m_site;
	//long m_siteLen;
};

#endif

	
