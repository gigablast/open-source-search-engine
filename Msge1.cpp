#include "gb-include.h"

#include "Msge1.h"
#include "Test.h"

// utility functions
bool getTestIp ( char *url , int32_t *retIp , bool *found , int32_t niceness ,
		 char *testDir ) ;
bool addTestIp ( char *host , int32_t hostLen , int32_t ip ) ;
bool saveTestBuf ( char *testDir ) ;

Msge1::Msge1() {
	m_buf = NULL;
	m_numReplies = 0;
	reset();
}

Msge1::~Msge1() {
	reset();
}

#define SLAB_SIZE (8*1024)

void Msge1::reset() {
	m_errno = 0;
	m_ipBuf = NULL;
	if ( m_buf ) mfree ( m_buf , m_bufSize,"Msge1buf");
	m_buf = NULL;
	m_numReplies = 0;
}

// . get various information for each url in a list of urls
// . urls in "urlBuf" are \0 terminated
// . used to be called getSiteRecs()
// . you can pass in a list of docIds rather than urlPtrs
bool Msge1::getFirstIps ( TagRec **grv ,
			  char   **urlPtrs                ,
			  linkflags_t *urlFlags           ,//Links::m_linkFlags
			  int32_t     numUrls                ,
			  // if skipOldLinks && urlFlags[i]&LF_OLDLINK, skip it
			  bool     skipOldLinks           ,
			  char    *coll                   ,
			  int32_t     niceness               ,
			  void    *state                  ,
			  void   (*callback)(void *state) ,
			  int32_t     nowGlobal              ,
			  bool     addTags                ,
			  char    *testDir                ) {

	reset();
	// bail if no urls or linkee
	if ( numUrls <= 0 ) return true;

	// save all input parms
	m_grv              = grv;
	m_urlPtrs          = urlPtrs;
	m_urlFlags         = urlFlags;
	m_numUrls          = numUrls;
	m_skipOldLinks     = skipOldLinks;
	m_coll             = coll;
	m_niceness         = niceness;
	m_state            = state;
	m_callback         = callback;
	m_nowGlobal        = nowGlobal;
	m_addTags          = addTags;
	m_testDir          = testDir;

	// . how much mem to alloc?
	// . include an extra 4 bytes for each one to hold possible errno
	int32_t need = 4 + 4; // ip + error
	// one per url
	need *= numUrls;
	// allocate the buffer to hold all the info we gather
	m_buf = (char *)mcalloc ( need , "Msge1buf" );
	if ( ! m_buf ) return true;
	m_bufSize = need;

	// clear it all
	memset ( m_buf , 0 , m_bufSize );

	// set the ptrs!
	char *p = m_buf;
	m_ipBuf             = (int32_t *)p ; p += numUrls * 4;
	m_ipErrors          = (int32_t *)p ; p += numUrls * 4;

	// initialize
	m_numRequests = 0;
	m_numReplies  = 0;

	// . point to first url to process
	// . url # m_n
	m_n = 0;

	// clear the m_used flags
	memset ( m_used , 0 , MAX_OUTSTANDING_MSGE1 );

	// . launch the requests
	// . a request can be a msg8a, msgc, msg50 or msg20 request depending













	//   on what we need to get
	// . when a reply returns, the next request is launched for that url
	// . we keep a msge1Slot state for each active url in the buffer
	// . we can have up to MAX_ACTIVE urls active
	if ( ! launchRequests ( 0 ) ) return false;

	// save it? might be a page parser
	if ( m_coll && ! strcmp(m_coll,"qatest123") ) saveTestBuf("qa");

	// none blocked, we are done
	return true;
}

// we only come back up here 1) in the very beginning or 2) when a url 
// completes its pipeline of requests
bool Msge1::launchRequests ( int32_t starti ) {
	// reset any error code
	g_errno = 0;
 loop:
	// stop if no more urls. return true if we got all replies! no block.
	if ( m_n >= m_numUrls ) return (m_numRequests == m_numReplies);
	// if we are maxed out, we basically blocked!
	if (m_numRequests - m_numReplies >= MAX_OUTSTANDING_MSGE1)return false;
	// . skip if "old"
	// . we are not planning on adding this to spiderdb, so Msg16
	//   want to skip the ip lookup, etc.
	if ( m_urlFlags && (m_urlFlags[m_n] & LF_OLDLINK) && m_skipOldLinks ) {
		m_numRequests++; 
		m_numReplies++; 
		m_n++; 
		goto loop; 
	}

	// grab the "firstip" from the tagRec if we can
	TagRec *gr  = m_grv[m_n];
	Tag    *tag = NULL;
	if ( gr ) tag = gr->getTag("firstip");
	int32_t ip;
	// grab the ip that was in there
	if ( tag ) ip = atoip(tag->getTagData());
	// if we had it but it was 0 or -1, then time that out
	// after a day or so in case it works again! 0 and -1 mean
	// NXDOMAIN or timeout error, etc.
	if ( tag && ( ip == 0 || ip == -1 ) ) 
		if ( m_nowGlobal - tag->m_timestamp > 3600*24 ) tag = NULL;
	// . if we still got the tag, use that, even if ip is 0 or -1
	// . this keeps things fast
	// . this makes sure doConsistencyCheck() does not block too in
	//   XmlDoc.cpp... cuz it cores if it does block
	if ( tag ) {
		// now "ip" might actually be -1 or 0 (invalid) so be careful
		m_ipBuf[m_n] = ip;
		// what is this?
		//if ( ip == 3 ) { char *xx=NULL;*xx=0; }
		m_numRequests++; 
		m_numReplies++; 
		m_n++; 
		goto loop; 
	}

	// or if banned
	Tag *btag = NULL;
	if ( gr ) btag = gr->getTag("manualban");
	if ( btag && btag->getTagData()[0] !='0') {
		// debug for now
		if ( g_conf.m_logDebugDns )
			log("dns: skipping dns lookup on banned hostname");
		// -1 means time out i guess
		m_ipBuf[m_n] = -1;
		m_numRequests++; 
		m_numReplies++; 
		m_n++; 
		goto loop; 
	}


	// . get the next url
	// . if m_xd is set, create the url from the ad id
	char *p = m_urlPtrs[m_n];

	// if it is ip based that makes things easy
	int32_t  hlen = 0;
	char *host = getHostFast ( p , &hlen );

	// reset this again
	ip = 0;
	// see if the hostname is actually an ip like "1.2.3.4"
	if ( host && is_digit(host[0]) ) ip = atoip ( host , hlen );
	// if legit this is non-zero
	if ( ip ) {
		// what is this? i no longer have this bug really - i fixed
		// it - but it did core here probably from a bad dns reply!
		// so take this out...
		//if ( ip == 3 ) { char *xx=NULL;*xx=0; }
		m_ipBuf[m_n] = ip;
		m_numRequests++; 
		m_numReplies++; 
		m_n++; 
		goto loop; 
	}

	// use domain, we are "firstip" only now!!!
	//int32_t  dlen = 0;
	//char *dom  = getDomFast ( p , &dlen );

	// get the length
	//int32_t  plen = gbstrlen(p);

	/*
	// look up in our m_testBuf.
	if ( m_coll && ! strcmp(m_coll,"qatest123") ) {
		bool found = false;
		// do we got it?
		int32_t quickIp ; bool status = getTestIp ( p , &quickIp, &found);
		// error?
		if ( ! status ) { 
			// save it
			m_errno = g_errno;
			// hard exit
			char *xx=NULL; *xx=0; 
		}
		// an ip of 0 means we could not find it
		if ( found ) { // quickIp != 0 ) {
			// set it
			m_ipBuf[m_n] = quickIp;
			m_numRequests++; 
			m_numReplies++; 
			m_n++; 
			goto loop; 
		}
	}
	*/

	// . grab a slot
	// . m_msg8as[i], m_msgCs[i], m_msg50s[i], m_msg20s[i]
	int32_t i;
	for ( i = starti ; i < MAX_OUTSTANDING_MSGE1 ; i++ )
		if ( ! m_used[i] ) break;
	// sanity check
	if ( i >= MAX_OUTSTANDING_MSGE1 ) { char *xx = NULL; *xx = 0; }
	// normalize the url
	//m_urls[i].set ( p , plen );
	// save the url number, "n"
	m_ns  [i] = m_n;
	// claim it
	m_used[i] = true;

	// note it
	//if ( g_conf.m_logDebugSpider )
	//	log(LOG_DEBUG,"spider: msge1: processing url %s",p);

	// . start it off
	// . this will start the pipeline for this url
	// . it will set m_used[i] to true if we use it and block
	// . it will increment m_numRequests and NOT m_numReplies if it blocked
	//sendMsgC ( i , dom , dlen );
	sendMsgC ( i , host , hlen );
	// consider it launched
	m_numRequests++;
	// inc the url count
	m_n++;
	// try to do another
	goto loop;
}

static void gotMsgCWrapper ( void *state , int32_t ip ) ;

bool Msge1::sendMsgC ( int32_t i , char *host , int32_t hlen ) {
	// we are processing the nth url
	int32_t   n    = m_ns[i];
	// set m_errno if we should at this point
	if ( ! m_errno && g_errno != ENOTFOUND ) m_errno = g_errno;
	// reset it
	g_errno = 0;

	// using the the ith msgC
	MsgC  *m    = &m_msgCs[i];
	// save i and this in the msgC itself
	m->m_state2 = this;
	m->m_state3 = (void *)(PTRTYPE)i;

	// note it
	//if ( g_conf.m_logDebugSpider )
	//	logf(LOG_DEBUG,"spider: msge1: getting ip for %s",
	//	     m_urlPtrs[n]);

	//int32_t  hlen = 0;
	//char *host = getHostFast ( m_urlPtrs[n] , &hlen );


	// look up in our m_testBuf.
	if ( m_coll && ! strcmp(m_coll,"qatest123") ) {
		bool found = false;
		// int16_tcut
		//char *p = m_urlPtrs[n];
		// do we got it?
		//bool status = getTestIp ( p , &m_ipBuf[n], &found);
		bool status = getTestIp ( host, &m_ipBuf[n],&found,m_niceness,
					  m_testDir );
		// error?
		if ( ! status ) { 
			// save it
			m_errno = g_errno;
			// hard exit
			char *xx=NULL; *xx=0; 
		}
		// an ip of 0 means we could not find it
		if ( found ) 
			return addTag(i);
	}

	//char *xx=NULL;*xx=0;

	if ( ! m->getIp ( host           ,
			  hlen           ,
			  &m_ipBuf[n]    ,
			  m              , // state
			  gotMsgCWrapper ))// callback
		return false;
	return doneSending ( i );
}	

void gotMsgCWrapper ( void *state , int32_t ip ) {
	MsgC   *m    = (MsgC  *)state;
	Msge1  *THIS = (Msge1 *)m->m_state2;
	int32_t    i    = (int32_t   )(PTRTYPE)m->m_state3;
	if ( ! THIS->doneSending ( i ) ) return;
	// try to launch more, returns false if not done
	if ( ! THIS->launchRequests(i) ) return;
	// . save it if we should. might be a page parser
	// . mdw i uncommented this when we cored all the time
	if ( THIS->m_coll&&!strcmp(THIS->m_coll,"qatest123"))saveTestBuf("qa");
	// must be all done, call the callback
	THIS->m_callback ( THIS->m_state );
}

void doneAddingTagWrapper ( void *state ) ;

bool Msge1::doneSending ( int32_t i ) {
	// we are processing the nth url
	int32_t n = m_ns[i];
	// save the error
	m_ipErrors[n] = g_errno;
	// save m_errno
	if ( g_errno && ! m_errno ) m_errno = g_errno;
	// clear it
	g_errno = 0;
	// get ip we got
	int32_t ip = m_ipBuf[n];
	// what is this?
	//if ( ip == 3 ) { char *xx=NULL;*xx=0; }
	//log ( LOG_DEBUG, "build: Finished Msge1 for url [%"INT32",%"INT32"]: %s ip=%s",
	//      n, i,  m_urls[i].getUrl() ,iptoa(ip));

	// store it?
	if ( ! strcmp(m_coll,"qatest123") ) {
		// get host
		int32_t  hlen = 0;
		char *host = getHostFast ( m_urlPtrs[n] , &hlen );
		// use domain, we are "firstip" only now!!!
		//int32_t  dlen = 0;
		//char *dom  = getDomFast ( m_urlPtrs[n] , &dlen );
		// add it to "./test/ips.txt"
		addTestIp ( host , hlen ,ip);
		//addTestIp ( dom,dlen ,ip);
	}

	// . all done if invalid
	// . otherwise, add the "firstip" tag to this the domain in tagdb
	// . we now add invalid ips to keep doConsistencyCheck() from 
	//   blocking as well as to keep performance fast so we do not
	//   have to keep re-looking up bad ips to get their "firstip",
	//   but we only respect bad "firstips" for 1 day (see above)
	//   before we try to recompute them
	//if ( ip == 0 || ip == -1 ) {
	//	// close it up
	//	doneAddingTag ( i );
	//	return true;
	//}

	return addTag ( i );
}

bool Msge1::addTag ( int32_t i ) {

	// we are processing the nth url
	int32_t n = m_ns[i];
	// get ip we got
	//int32_t ip = m_ipBuf[n];

	//
	// HACK: hijack this MsgC to use as a "state" for call to msg9a
	// so we can add the "firstip" tag, since we did not have one!
	//

	// using the the ith msgC
	MsgC  *m    = &m_msgCs[i];
	// save i and this in the msgC itself
	m->m_state2 = this;
	m->m_state3 = (void *)(PTRTYPE)i;
	// store the domain here
	//char *domBuf = m->m_request;
	// get the domain
	//int32_t  dlen = 0;
	//char *dom  = getDomFast ( m_urlPtrs[n] , &dlen );

	// make it all host based
	//char *hostBuf = m->m_request;
	// get the host
	int32_t  hlen = 0;
	char *host  = getHostFast ( m_urlPtrs[n] , &hlen );


	// if invalid or ip-based, skip it!
	//if ( ! dom || dlen <= 0 ) 
	if ( ! host || hlen <= 0 ) 
		return doneAddingTag ( i );

	if ( ! m_addTags )
		return doneAddingTag ( i );

	// now let xmldoc add the firstip tags of each outlink!
	return doneAddingTag ( i );

	/*
	// store it
	//strncpy ( domBuf , dom , dlen );
	strncpy ( hostBuf , host , hlen );
	// NULL term it
	//domBuf[dlen] = '\0';
	hostBuf[hlen] = '\0';

	// get time now synced with host #0
	//int32_t nowGlobal = getTimeGlobal();
	// put in buf
	char ipbuf[32];
	sprintf(ipbuf,"%s",iptoa(ip) );
	// . make the tag rec to add
	// . msg9a copies it into a request buffer, so no need to be persistent
	TagRec gr;
	// returns false and sets g_errno on error
	if ( !gr.addTag("firstip",m_nowGlobal,"msge1",ip,ipbuf,gbstrlen(ipbuf))){
		// should never have error
		char *xx=NULL;*xx=0; }

	// int16_tcut
	Msg9a *m9 = &m_msg9as[i];
	// . now add to "firstip" in tagdb
	// . borrow the ith msg9a (only 40 bytes each)
	// . this should only return control to us once it is safely in tagdb!
	if ( ! m9->addTags ( NULL                 , 
			     //&domBuf            ,
			     &hostBuf             ,
			     1                    ,
			     m_coll               ,
			     m                    , // state
			     doneAddingTagWrapper ,
			     m_niceness           ,
			     &gr                  ,
			     false                ,
			     &ip                  ))
		// we blocked
		return false;

	return doneAddingTag ( i );
	*/
}

void doneAddingTagWrapper ( void *state ) {
	// get the hijacked msgc
	MsgC   *m    = (MsgC  *)state;
	Msge1  *THIS = (Msge1 *)m->m_state2;
	int32_t    i    = (int32_t   )(PTRTYPE)m->m_state3;
	// return if that blocked
	if ( ! THIS->doneAddingTag ( i ) ) return;
	// loop back for more
	if ( ! THIS->launchRequests ( i ) ) return;
	// must be all done, call the callback
	THIS->m_callback ( THIS->m_state );
}


bool Msge1::doneAddingTag ( int32_t i ) {
	// unmangle
	//*m_pathPtr[i] = '/';
	m_numReplies++;
	// free it
	m_used[i] = false;
	// we did not block
	return true;
}

#include "HashTableX.h"
static char *s_testBuf      = NULL ;
static char *s_testBufPtr          ;
static int32_t  s_testBufSize         ;
static char *s_testBufEnd          ;
static char  s_needsReload  = true ;
static char *s_last         = NULL ;
static int32_t  s_lastLen      = 0    ;
static HashTableX s_ht;

// . only call this if the collection is "qatest123"
// . we try to get the ip by accessing the "./test/ips.txt" file
// . we also ad ips we lookup to that file in the collection is "qatest123"
// . returns false and sets g_errno on error, true on success
bool getTestIp ( char *url , int32_t *retIp , bool *found , int32_t niceness ,
		 char *testDir ) {

	// set the url from the url string, "us"
	Url u; u.set ( url );
	// get host of the url
	char *host = u.getHost();
	int32_t  hlen = u.getHostLen();

	// if it is an ip, that is easy!
	if ( is_digit(host[0]) ) {
		int32_t aip = atoip(host,hlen);
		if ( aip ) return aip;
	}

	// assume not found
	*found = false;

	// . if we are the "qatestq123" collection, check for "./test/ips.txt"
	//   file that gives us the ips of the given urls. 
	// . if we end up doing some lookups we should append to that file
	if ( ! s_testBuf || s_needsReload ) {
		// assume needs reload now
		s_needsReload = true;
		// free it
		if ( s_testBuf ) mfree ( s_testBuf , s_testBufSize, "msge1" );
		// hashtable set, map urlhash32 to ip
		if ( !s_ht.set(4,4,400000,NULL,0,false,niceness,"msge1tab")) { 
			char *xx=NULL;*xx=0; }
		// null it out now, we freed it
		s_testBuf = NULL;
		//char *testDir = g_test.getTestDir();
		// filename
		char fn[100]; 
		sprintf(fn,"%s/%s/ips.txt",g_hostdb.m_dir,testDir);
		// set it
		File f; f.set ( fn );
		// get size
		int32_t fsize = f.getFileSize ( );
		// < 0 means error? does not exist?
		if ( fsize < 0 ) fsize = 0;
		// how much to alloc? 1MB for all for now
		int32_t need = 3000001;
		// and what we had
		need += fsize;
		// make buf big enough to hold the read
		s_testBuf = (char *)mmalloc ( need , "tmsge1" );
		// this for freeing
		s_testBufSize = need;
		s_testBufEnd  = s_testBuf + need;
		// error?
		if ( ! s_testBuf ) {
			// note it
			log("test: failed to alloc %"INT32" bytes for ip buf",need);
			// error out
			return false;
		}
		// assign end to the beginning, assume nothing to read
		s_testBufPtr = s_testBuf;
		// read in the file, if it was there
		if ( fsize > 0 ) {
			// open it
			f.open ( O_RDWR );
			// read it in
			int32_t rs = f.read ( s_testBuf , fsize , 0 ) ;
			// check it
			if ( rs != fsize ) {
				// note it
				log("test: failed to read %"INT32" bytes of "
				    "./%s/ips.txt file",fsize,testDir);
				// close it
				f.close();
				// error out
				return false;
			}
			// recompute the end of s_testBuf
			s_testBufPtr = s_testBuf + fsize;
			// trim off ending punct like \n
			for ( ; is_punct_a(s_testBufPtr[-1]) ; s_testBufPtr--);
			// and null term
			*s_testBufPtr = '\0';
		}
		// close it
		f.close();
		// good to go
		s_needsReload = false;

		//
		// fill hashtable, s_ht
		//

		char *p = s_testBuf;
	loop:
		// breathe
		QUICKPOLL(niceness);
		// skip over spaces
		for ( ; p < s_testBufPtr && is_wspace_a(*p) ; p++ );
		// assign the url string, "us" to "p"
		char *us = p;
		// skip over that url
		char *next = p; 
		for (;next<s_testBufPtr && !is_wspace_a(*next);next++);
		// update
		p = next;
		// get hash of that host
		int32_t u32 = hash32 ( us,next-us);
		// if no match, try the next hostname in s_testBuf
		//if ( strncasecmp ( us , host , hlen ) )  goto loop;
		// the url in the buf must be same length to be a match
		//if ( ! is_wspace_a(us[hlen]) ) goto loop;
		char *ips = next;
		// skip spaces
		for ( ; ips < s_testBufPtr && is_wspace_a(*ips) ; ips++ );
		// all done? not found...
		if ( ! ips[0] ) { *retIp = 0; return true; }
		// sanity check, each line must have an IP!
		if ( ips >= s_testBufPtr ) { char *xx=NULL;*xx=0; }
		// must be number
		if ( ! is_digit(*ips) ) { 
			// there is a single line that is \0 0.0.0.\n
			// so let's fix this by skipping until \n
			for ( ; p<s_testBufPtr&& *p!='\n';p++);
			goto loop;
			//char *xx=NULL;*xx=0; }
		}
		// advance to end
		char *ie = ips; 
		for ( ; *ie && ie < s_testBufPtr ; ie++ ) 
			// stop if not good char
			if ( ! is_digit(*ie) && *ie != '.' ) break;
		// get it
		int32_t ip = atoip ( ips , ie - ips );
		// store in hash table for lookup below
		if ( u32 && ! s_ht.addKey ( &u32 , &ip ) ) { 
			char *xx=NULL;*xx=0; }
		// advance p for next round
		p = ie;
		// skip over spaces
		for ( ; p < s_testBufPtr && is_wspace_a(*p) ; p++ );
		// do more if we should
		if ( p < s_testBufPtr ) goto loop;
	}

	// assume none found
	*retIp = 0;
	// return 0 if no ips.txt data
	//if ( ! s_testBuf || s_testBufPtr == s_testBuf ) return true;
	// look it up in hash table now
	int32_t h = hash32 ( host,hlen);
	int32_t *ipPtr = (int32_t *)s_ht.getValue(&h);
	// if missed, return now
	if ( ! ipPtr ) 
		return true;
	// set it
	*retIp = *ipPtr;
	// flag it
	*found = true;
	// note it
	//log("test: found ip %s for %s in ips.txt",iptoa(ip),url);
	// that is it
	return true;
}

void resetTestIpTable ( ) {
	s_ht.reset();
}

// returns false if unable to add, returns true if added
bool addTestIp ( char *host , int32_t hostLen , int32_t ip ) {
	// must have first tried to get it
	if ( s_needsReload ) { char *xx=NULL;*xx=0; }
	// must have allocated this
	if ( ! s_testBuf )
		return log("test: no test buf to add ip %s",iptoa(ip));
	// make sure enough room
	int32_t need = 1 + hostLen + 1 + (4*3+3) + 1;
	// add it to test buf
	if ( s_testBufPtr + need >= s_testBufEnd ) 
		return log("test: no room to add ip %s",iptoa(ip));
	// did we just add this one? prevent dups this way...
	if ( s_last && hostLen==s_lastLen && !strncmp(s_last,host,hostLen))
		return true;
	// preserve ptr to last one we added
	s_last    = s_testBufPtr;
	s_lastLen = hostLen;
	// print it
	gbmemcpy ( s_testBufPtr , host , hostLen );
	// skip it
	s_testBufPtr += hostLen;
	// then space and ip
	int32_t ps = sprintf ( s_testBufPtr , " %s\n",iptoa(ip));
	// skip that
	s_testBufPtr += ps;
	// add to hash table too
	int32_t u32 = hash32 ( host , hostLen );
	if ( ! s_ht.addKey ( &u32 , &ip ) ) { char *xx=NULL;*xx=0; }
	// success
	return true;
}

void makeQADir();

// . save it back to disk
// . we should call this from Test.cpp when the run is completed!!
bool saveTestBuf ( char *testDir ) {
	//char *testDir = g_test.getTestDir();
	// ensure ./qa/ subdir exsts. in qa.cpp
	makeQADir();
	// filename
	char fn[100]; sprintf(fn,"%s/%s/ips.txt",g_hostdb.m_dir, testDir);
	// set it
	File f; f.set ( fn );
	// open it
	f.open ( O_RDWR | O_CREAT );
	// how much to write?
	int32_t size = s_testBufPtr - s_testBuf;
	// write it out
	int32_t ws = f.write ( s_testBuf , size , 0 );
	// close it
	f.close();
	// bitch?
	if ( ws != size ) 
		return log("test: failed to write %"INT32" bytes to %s",size,fn);
	// note it
	log("test: saved ips.txt");
	// ok
	return true;
}
