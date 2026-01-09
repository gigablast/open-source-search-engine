// Matt Wells, copyright Jan 2002

// normalizes urls from stdin

#include "gb-include.h"

#include "Url.h"
#include "Mem.h"
#include "Titledb.h"
#include "HttpMime.h"
#include "SiteGetter.h"
//#include "Tfndb.h"
//#include "Msg50.h"
//#include "Msg16.h"

bool mainShutdown ( bool urgent ) { return true; }
bool closeAll ( void *state , void (* callback)(void *state) ) {return true;}
bool allExit ( ) { return true; }
//int32_t g_qbufNeedSave = false;
//SafeBuf g_qbuf;
bool sendPageSEO(class TcpSocket *s, class HttpRequest *hr) {return true;}
char g_recoveryMode;

int main ( int argc , char *argv[] ) {
	bool addWWW = true;
	bool stripSession = true;
	// check for arguments
	for (int32_t i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-w") == 0)
			addWWW = false;
		else if (strcmp(argv[i], "-s") == 0)
			stripSession = false;
	}
	// initialize
	//g_mem.init(100*1024);
	hashinit();
	//g_conf.m_tfndbExtBits = 23;
 loop:
	// read a url from stddin
	char sbuf[1024];
	if ( ! fgets ( sbuf , 1024 , stdin ) ) exit(1);
	char *s = sbuf;
	char fbuf[1024];
	// decode if we should
	if ( strncmp(s,"http%3A%2F%2F",13) == 0 ||
	     strncmp(s,"https%3A%2F%2F",13) == 0 ) {
		urlDecode(fbuf,s,gbstrlen(s));
		s = fbuf;
	}
	// old url
	printf("###############\n");
	printf("old: %s",s);
	int32_t slen = gbstrlen(s);
	// remove any www. if !addWWW
	if (!addWWW) {
		if (slen >= 4 &&
		    strncasecmp(s, "www.", 4) == 0) {
			slen -= 4;
			memmove(s, &s[4], slen);
		}
		else {
			// get past a ://
			int32_t si = 0;
			while (si < slen &&
			       ( s[si] != ':' ||
				 s[si+1] != '/' ||
				 s[si+2] != '/' ) )
				si++;
			// remove the www.
			if (si + 7 < slen) {
				si += 3;
				if (strncasecmp(&s[si], "www.", 4) == 0) {
					slen -= 4;
					memmove(&s[si], &s[si+4], slen-si);
				}
			}
		}
	}
	// set it
	Url u;
	u.set ( s , slen ,
		addWWW   ,      /*add www?*/
		stripSession ); /*strip session ids?*/
	// print it
	char out[1024*4];
	char *p = out;
	p += sprintf(p,"tld: ");
	gbmemcpy ( p, u.getTLD(),u.getTLDLen());
	p += u.getTLDLen();
	char c = *p;
	*p = '\0';
	printf("%s\n",out);
	*p = c;
	

	// dom
	p = out;
	sprintf ( p , "dom: ");
	p += gbstrlen ( p );
	gbmemcpy ( p , u.getDomain() , u.getDomainLen() );
	p += u.getDomainLen();
	c = *p;
	*p = '\0';
	printf("%s\n",out);
	*p = c;
	// host
	p = out;
	sprintf ( p , "host: ");
	p += gbstrlen ( p );
	gbmemcpy ( p , u.getHost() , u.getHostLen() );
	p += u.getHostLen();
	c = *p;
	*p = '\0';
	printf("%s\n",out);
	*p = c;
	// then the whole url
	printf("url: %s\n", u.getUrl() );

	/*
	int32_t  siteLen;
	char *site = u.getSite ( &siteLen , NULL , false );
	if ( site ) {
		c = site[siteLen];
		site[siteLen] = '\0';
	}
	printf("site: %s\n", site );
	if ( site ) site[siteLen] = c;
	*/
	SiteGetter sg;
	sg.getSite ( u.getUrl() ,
		     NULL , // tagrec
		     0 , // timestamp
		     NULL, // coll
		     0 , // niceness
		     //false , // addtags
		     NULL , // state
		     NULL ); // callback
	if ( sg.m_siteLen )
		printf("site: %s\n",sg.m_site);

	printf("isRoot: %"INT32"\n",(int32_t)u.isRoot());

	/*
	bool perm = ::isPermalink ( NULL        , // coll
				    NULL        , // Links ptr
				    &u          , // the url
				    CT_HTML     , // contentType
				    NULL        , // LinkInfo ptr
				    false       );// isRSS?
	printf ("isPermalink: %"INT32"\n",(int32_t)perm);
	*/

	// print the path too
	p = out;

	p += sprintf ( p , "path: " );
	gbmemcpy ( p , u.getPath(), u.getPathLen() );
	p += u.getPathLen();

	if ( u.getFilename() ) {
		p += sprintf ( p , "\nfilename: " );
		gbmemcpy ( p , u.getFilename(), u.getFilenameLen() );
		p += u.getFilenameLen();
		*p = '\0';
		printf("%s\n", out );
	}

	// encoded
	char dst[MAX_URL_LEN+200];
	urlEncode ( dst,MAX_URL_LEN+100,
				u.getUrl(), u.getUrlLen(), 
				false ); // are we encoding a request path?
	printf("encoded: %s\n",dst);

	// the probable docid
	int64_t pd = g_titledb.getProbableDocId(&u);
	printf("pdocid: %"UINT64"\n", pd );
	printf("dom8: 0x%"XINT32"\n", (int32_t)g_titledb.getDomHash8FromDocId(pd) );
	//printf("ext23: 0x%"XINT32"\n",g_tfndb.makeExt(&u));
	if ( u.isLinkLoop() ) printf("islinkloop: yes\n");
	else                  printf("islinkloop: no\n");
	int64_t hh64 = u.getHostHash64();
	printf("hosthash64: 0x%016"XINT64"\n",hh64);
	uint32_t hh32 = u.getHostHash32();
	printf("hosthash32: 0x%08"XINT32" (%"UINT32")\n",hh32,hh32);
	int64_t dh64 = u.getDomainHash64();
	printf("domhash64: 0x%016"XINT64"\n",dh64);
	int64_t uh64 = u.getUrlHash64();
	printf("urlhash64: 0x%016"XINT64"\n",uh64);
	//if(isUrlUnregulated(NULL ,0,&u)) printf("unregulated: yes\n");
	//else                            printf("unregulated: no\n");
	goto loop;
}
