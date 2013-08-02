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

int main ( int argc , char *argv[] ) {
	bool addWWW = true;
	bool stripSession = true;
	// check for arguments
	for (long i = 1; i < argc; i++) {
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
	long slen = gbstrlen(s);
	// remove any www. if !addWWW
	if (!addWWW) {
		if (slen >= 4 &&
		    strncasecmp(s, "www.", 4) == 0) {
			slen -= 4;
			memmove(s, &s[4], slen);
		}
		else {
			// get past a ://
			long si = 0;
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
	char out[1024*3];
	char *p = out;
	sprintf ( p , "dom: ");
	p += gbstrlen ( p );
	memcpy ( p , u.getDomain() , u.getDomainLen() );
	p += u.getDomainLen();
	char c = *p;
	*p = '\0';
	printf("%s\n",out);
	*p = c;
	// host
	p = out;
	sprintf ( p , "host: ");
	p += gbstrlen ( p );
	memcpy ( p , u.getHost() , u.getHostLen() );
	p += u.getHostLen();
	c = *p;
	*p = '\0';
	printf("%s\n",out);
	*p = c;
	// then the whole url
	printf("url: %s\n", u.getUrl() );

	/*
	long  siteLen;
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

	printf("isRoot: %li\n",(long)u.isRoot());

	/*
	bool perm = ::isPermalink ( NULL        , // coll
				    NULL        , // Links ptr
				    &u          , // the url
				    CT_HTML     , // contentType
				    NULL        , // LinkInfo ptr
				    false       );// isRSS?
	printf ("isPermalink: %li\n",(long)perm);
	*/

	// print the path too
	p = out;

	p += sprintf ( p , "path: " );
	memcpy ( p , u.getPath(), u.getPathLen() );
	p += u.getPathLen();

	if ( u.getFilename() ) {
		p += sprintf ( p , "\nfilename: " );
		memcpy ( p , u.getFilename(), u.getFilenameLen() );
		p += u.getFilenameLen();
		*p = '\0';
		printf("%s\n", out );
	}

	// the probable docid
	long long pd = g_titledb.getProbableDocId(&u);
	printf("pdocid: %llu\n", pd );
	printf("dom8: 0x%lx\n", (long)g_titledb.getDomHash8FromDocId(pd) );
	//printf("ext23: 0x%lx\n",g_tfndb.makeExt(&u));
	if ( u.isLinkLoop() ) printf("islinkloop: yes\n");
	else                  printf("islinkloop: no\n");
	long long hh64 = u.getHostHash64();
	printf("hosthash64: 0x%016llx\n",hh64);
	unsigned long hh32 = u.getHostHash32();
	printf("hosthash32: 0x%08lx (%lu)\n",hh32,hh32);
	long long dh64 = u.getDomainHash64();
	printf("domhash64: 0x%016llx\n",dh64);
	long long uh64 = u.getUrlHash64();
	printf("urlhash64: 0x%016llx\n",uh64);
	//if(isUrlUnregulated(NULL ,0,&u)) printf("unregulated: yes\n");
	//else                            printf("unregulated: no\n");
	goto loop;
}
