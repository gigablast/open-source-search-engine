#include "gb-include.h"

#include "Url.h"
#include "Domains.h"
#include "Errno.h"
#include "HashTable.h"
#include "Speller.h"
#include "Punycode.h"
#include "Unicode.h"

static void print_string ( char *s , int32_t len );

void Url::reset() {
	m_scheme    = NULL; 
	m_host      = NULL; 
	m_path      = NULL; 
	m_filename  = NULL; 
	m_extension = NULL; 
	m_query     = NULL; 
	m_domain    = NULL;
	m_tld       = NULL;
	m_anchor    = NULL;
	//m_site      = NULL;

	m_url[0]    = '\0';
	m_ulen      = 0;
	m_dlen      = 0;
	m_slen      = 0;
	m_qlen      = 0;
	m_hlen      = 0;
	m_elen      = 0;
	m_mdlen     = 0;
	m_anchorLen = 0;
	//m_siteLen   = 0;
	// ip related stuff
	m_ip          = 0;
	// m_isWarcValid = false;
	// m_isArcValid  = false;
}

// set from another Url, does a copy
void Url::set ( Url *url , bool addWWW ) {
	if ( ! url ) { reset(); return; }
	set ( url->getUrl() , url->getUrlLen() , addWWW );
}


void Url::set (Url *baseUrl,char *s,int32_t len,bool addWWW,bool stripSessionId,
	       bool stripPound , bool stripCommonFile, int32_t titleRecVersion ) {

	reset();
	// debug msg
	//if ( addWWW )
	//	log("Url::set: warning, forcing WWW\n");

	if ( ! baseUrl ) { set ( s , len , addWWW ); return; }

	char *base = (char *) baseUrl->m_url;
	int32_t  blen =          baseUrl->m_ulen;
	// don't include cgi crap
	if ( baseUrl->m_query ) blen -= (baseUrl->m_qlen + 1);

	// . adjust length of the base url.
	// . if base url does not end in / then it must have a m_filename at 
	//   the end, therefore we should strip the m_filename
	if ( blen > 0 && base[blen-1] != '/' ) 
		while (blen > 0 && base[blen-1] != '/')   blen--;

	// . fix baseurl = "http://xyz.com/poo/all" and s = "?page=3"
	// . if "s" starts with ? then keep the filename in the base url
	if ( s[0] == '?' ) {
		for ( ; base[blen] && base[blen]!='?'; blen++ );
	}

	if ( blen==0 && len==0 ) return;

	// skip s over spaces
	char *send = s + len;
	while ( s < send && is_wspace_a ( *s ) ) { s++; len--; }

	// . is s a relative url? search for ://, but break at first /
	// . but break at any non-alnum or non-hyphen
	bool isAbsolute = false;
	int32_t i;
	for ( i = 0; i < len && (is_alnum_a(s[i]) || s[i]=='-') ; i++ );
        //for ( i = 0 ; s[i] && (is_alnum_a(s[i]) || s[i]=='-') ; i++ );
	if ( ! isAbsolute )
		isAbsolute = ( i + 2 < len &&
			       s[i+0]==':' &&
			       s[i+1]=='/' ); // some are missing both /'s!
	                     //s[i+2]=='/'  ) ;
	if ( ! isAbsolute )
		isAbsolute = ( i + 2 < len &&
		       s[i+0]==':' &&
		       s[i+1]=='\\' );
	// or if s starts with // then it's also considered absolute!
	if ( ! isAbsolute && len > 1 && s[0]=='/' && s[1]=='/' )
		isAbsolute = true;
	// watch out for idiots
	if ( ! isAbsolute && len > 1 && s[0]=='\\' && s[1]=='\\' )
		isAbsolute = true;


	// debug msg
	//log("base=%s, abs=%i, slen=%i, s=%s, i=%i\n",
	// base,isAbsolute,len,s,i);

	// don't use base if s is not relative
	if ( blen==0 || isAbsolute ) {
		set(s,len,addWWW,stripSessionId,stripPound,
		    false, // stripCommonFile?
		    titleRecVersion);
		return;
	}

	// . if s starts with / then hack of base's m_path
	// . careful not to hack of the port, if any
	// . blen = baseUrl->m_slen + 3 + baseUrl->m_hlen;
	if ( len > 0 && s[0]=='/' ) 
		blen = baseUrl->m_path - baseUrl->m_url ; 
		
	char temp[MAX_URL_LEN*2+1];
	strncpy(temp,base,blen);
	if (len>MAX_URL_LEN) len = MAX_URL_LEN-2;
	// if s does NOT start with a '/' then add one here in case baseUrl
	// does NOT end in one.
	// fix baseurl = "http://xyz.com/poo/all" and s = "?page=3"
	if ( len > 0 && s[0] != '/' && s[0] !='?' && temp[blen-1] != '/' ) 
		temp[blen++] ='/';
	strncpy(temp+blen,s,len);
	set ( temp, blen+len , addWWW , stripSessionId , stripPound ,
	      stripCommonFile ,
	      titleRecVersion );
}
// . url rfc = http://www.blooberry.com/indexdot/html/topics/urlencoding.htm
// . "...Only alphanumerics [0-9a-zA-Z], the special characters "$-_.+!*'()," 
//    [not including the quotes - ed], and reserved characters used for their 
//    reserved purposes may be used unencoded within a URL."
// . i know sun.com has urls like "http://sun.com/;$sessionid=123ABC$"
// . url should be ENCODED PROPERLY for this to work properly
void Url::set ( char *t , int32_t tlen , bool addWWW , bool stripSessionId ,
                bool stripPound , bool stripCommonFile , 
		int32_t titleRecVersion ) {
	reset();
	// debug
	//t = "http://www.ac.uk/../../news/.asp";
	//tlen = gbstrlen(t);
	if ( ! t || tlen == 0 ) return ;
	// we may add a "www." a trailing backslash and \0, ...
	if ( tlen > MAX_URL_LEN - 10 ) {
		log( LOG_LIMIT,"db: Encountered url of length %"INT32". "
		     "Truncating to %i" , tlen , MAX_URL_LEN - 10 );
		tlen = MAX_URL_LEN - 10;
	}
	// . skip over non-alnum chars (except - or /) in the beginning
	// . if url begins with // then it's just missing the http: (slashdot)
	// . watch out for hostname like: -dark-.deviantart.com(yes, it's real)
	// . so all protocols are hostnames MUST start with alnum OR hyphen
	while ( tlen > 0 && !is_alnum_a(*t) && *t!='-' && *t!='/'){t++;tlen--;}
	// . stop t at first space or binary char
	// . url should be in encoded form!
	int32_t i = 0;
	int32_t nonAsciiPos = -1;
	for ( i = 0 ; i < tlen ; i++ )	{
		if ( is_wspace_a(t[i])   ) break; // no spaces allowed

		if ( ! is_ascii(t[i]) ) {
			// Sometimes the length with the null is passed in, 
			// so ignore nulls FIXME?
			if( t[i] ) nonAsciiPos = i;
			break; // no non-ascii chars allowed
		}
	}

	
	if(nonAsciiPos != -1) { 
		// Try turning utf8 and latin1 encodings into punycode.
		// All labels(between dots) in the domain are encoded 
		// separately.  We don't support encoded tlds, but they are 
		// not widespread yet.
		// If it is a non ascii domain it needs to take the form 
		// xn--<punycoded label>.xn--<punycoded label>.../
		char tmp = t[tlen];
		if(t[tlen]) t[tlen] = 0;
		log(LOG_DEBUG, "build: attempting to decode unicode url %s pos at %"INT32, t, nonAsciiPos);
		if(tmp) t[tlen] = tmp;
		char encoded [ MAX_URL_LEN ];
		size_t encodedLen = MAX_URL_LEN;
		char *encodedDomStart = encoded;
		char *p = t;
		char *pend = t+tlen;
		
		// Find the start of the domain
		if(tlen > 7 && strncmp(p, "http://", 7) == 0) p += 7;
		else if(tlen > 8 && strncmp(p, "https://", 8) == 0) p += 8;
 
		gbmemcpy(encodedDomStart, t, p-t);
		encodedDomStart += p-t;

		while(p < pend && *p != '/') {
			char *labelStart = p;
			uint32_t tmpBuf[MAX_URL_LEN];
			int32_t tmpLen = 0;
		
			while(p < pend && *p != '.' && *p != '/') p++;
			int32_t	labelLen = p - labelStart;

			bool tryLatin1 = false;
			// For utf8 urls
			p = labelStart;
			bool labelIsAscii = true;

			// Convert the domain to code points and copy it to 
			// tmpbuf to be punycoded
			for(;p-labelStart<labelLen;
				p += utf8Size(tmpBuf[tmpLen]), tmpLen++) {

				labelIsAscii &= is_ascii(*p);
				tmpBuf[tmpLen] = utf8Decode(p);
				if(!tmpBuf[tmpLen]) { // invalid char?
					tryLatin1 = true;
					break;
				}
			}
			if(labelIsAscii) {
				if(labelStart[labelLen] == '.') {
					labelLen++;
					p++;
				}
				gbmemcpy(encodedDomStart, labelStart, labelLen);
				encodedDomStart += labelLen;
				continue;
			}

			if( tryLatin1 ) {
				// For latin1 urls
				tmpLen = 0;
				for(;tmpLen<labelLen;tmpLen++) {
					tmpBuf[tmpLen] = labelStart[tmpLen];
				}
			}

			gbmemcpy(encodedDomStart, "xn--", 4);
			encodedDomStart += 4;

			punycode_status status ;
			status = punycode_encode(tmpLen, 
						 tmpBuf,
						 NULL, 
						 &encodedLen,
						 encodedDomStart);
			if ( status != 0 ) {
				// Give up? try again?
				log("build: Bad Engineer, failed to "
				    "punycode international url %s", t);
				return;
			}
			// We should check if what we encoded were valid url 
			// characters, no spaces, etc
			// FIXME: should we exclude just the bad chars? I've 
			// seen plenty of urls with
			// a newline in the middle.  Just discard the whole 
			// chunk for now
			bool badUrlChars = false;
			for(uint32_t i=0;i<encodedLen;i++) {
				if(is_wspace_a(encodedDomStart[i])){
					badUrlChars = true;
					break;
				}
			}

			if(encodedLen == 0 || badUrlChars) {
				encodedDomStart -= 4; //don't need the xn--
				p++;
			} else {
				encodedDomStart += encodedLen;
				*encodedDomStart++ = *p++; // Copy in the . or the /

			}
		}
		
		// p now points to the end of the domain
		// encodedDomStart now points to the first free space in encoded string

		// Now copy the rest of the url in.  Watch out for non-ascii chars 
		// truncate the url, and keep it under max url length
		uint32_t newUrlLen = encodedDomStart - encoded;

		while(p < pend) {
			if ( ! *p ) break; // null?
			if(!is_ascii(*p)) {
				//break;
				// url encode utf8 characters now
				char cs = getUtf8CharSize(p);
				// bad utf8 char?
				if ( cs <= 1 ) break;
				// too long?
				if ( newUrlLen + 12 >= MAX_URL_LEN )
					break;
				char stored = urlEncode ( &encoded[newUrlLen], 
							  12 ,
							  p ,
							  cs );
				p += cs;
				newUrlLen += stored;
				continue;
			}
			if(is_wspace_a(*p)) break;
			if(newUrlLen >= MAX_URL_LEN) break;
			encoded[newUrlLen++] = *p++;
		}


		//gbmemcpy(encodedDomStart, p, restOfUrlLen);
		encoded[newUrlLen] = '\0';
		return this->set(encoded, newUrlLen, addWWW, stripSessionId, 
				 stripPound, stripCommonFile, titleRecVersion);
    }
	// truncate length to the first occurence of an unacceptable char
	tlen = i;
	// . decode characters that should not have been encoded
	// . also NULL terminates
	//char tmp[MAX_URL_LEN];
	//int32_t tmpLen;
	//tmpLen = safeDecode ( t , tlen , tmp );
	// . jump over http:// if it starts with http://http://
	// . a common mistake...
	while ( tlen > 14 && ! strncasecmp ( t , "http://http://" , 14 ) ) {
		t += 7;	tlen -= 7; }
	//if ( tlen > 26 )
	//while ( ! strncasecmp ( t , "http%3A%2F%2Fhttp%3A%2F%2F",26)){
	//t += 13; tlen -= 13; }

	// strip the "#anchor" from http://www.xyz.com/somepage.html#anchor"
	int32_t anchorPos = 0;
	int32_t anchorLen = 0;
	for ( int32_t i = 0 ; i < tlen ; i++ ) {
		if ( t[i] != '#' ) continue;
		// ignore anchor if a ! follows it. 'google hash bang hack'
		// which breaks the web and is now deprecated, but, there it is
		if ( i+1<tlen && t[i+1] == '!' ) continue;
		anchorPos = i;
		anchorLen = tlen - i;
		if ( stripPound )
			tlen = i;
		break;
	}

	// copy to "s" so we can NULL terminate it
	char s [ MAX_URL_LEN ];
	int32_t len = tlen;
	// store filtered url into s
	gbmemcpy ( s , t , tlen );
	s[len]='\0';

	// make http:////www.xyz.com into http://www.xyz.com
	// if ( len > 14 && s[7]=='/' && ! strncasecmp ( s , "http:////" ,9) ){
	// 	gbmemcpy (s+7,s+9,len-9+1);
	// 	len -= 2;
	// }
	// if ( len > 14 && s[8]=='/' && ! strncasecmp ( s ,"https:////",10)){
	// 	gbmemcpy (s+8,s+10,len-9+1);
	// 	len -= 2;
	// }

	// . remove session ids from s
	// . ';' most likely preceeds a session id
	// . http://www.b.com/p.jhtml;jsessionid=J4QMFWBG1SPRVWCKUUXCJ0W?pp=1
	// . http://www.b.com/generic.html;$sessionid$QVBMODQAAAGNA?pid=7
	// . http://www.b.com/?PHPSESSID=737aec14eb7b360983d4fe39395&p=1
	// . http://www.b.com/cat.cgi/process?mv_session_id=xrf2EY3q&p=1
	// . http://www.b.com/default?SID=f320a739cdecb4c3edef67e&p=1
	if ( stripSessionId ) {
		// CHECK FOR A SESSION ID USING SEMICOLONS
		// or don't...bad for dmoz urls and apparently has ligit use
//		int32_t i = 0;
//		while ( s[i] && s[i]!=';' ) i++;
//		// did we get a semi colon?
//		if ( s[i] == ';' ) {
//			// i is start of it
//			int32_t a = i;
//			// find the end of the session id
//			int32_t b = i + 1;
//			while ( s[b] && s[b] != '?' ) b++;
//			// remove the session id by covering it up
//			memmove ( &s[a] , &s[b] , len - b );
//			// reduce length
//			len -= (b-a);
//			// NULL terminate
//			s[len] = '\0';
//		}
		// CHECK FOR A SESSION ID USING QUERY STRINGS
		char *p = s;
		while ( *p && *p != '?' && *p != ';' ) p++;
		// bail if no ?
		if ( ! *p ) goto skip;
		// now search for severl strings in the cgi query string
		char *tt = NULL;
		int32_t x;
		if ( ! tt ) { tt = gb_strcasestr ( p , "PHPSESSID=" ); x = 10;}
		if ( ! tt ) { tt = strstr        ( p , "SID="       ); x =  4;}
		// . osCsid and XTCsid are new session ids
		// . keep this up here so "sid=" doesn't override it
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = strstr ( p , "osCsid=" ); 
			x =  7;
			if ( ! tt ) tt = strstr ( p , "XTCsid=" );
			// a hex sequence of at least 10 digits must follow
			if ( tt && ! isSessionId ( tt + x, titleRecVersion ) )
				tt = NULL;
		}
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = strstr ( p , "osCsid/" ); 
			x =  7;
			// a hex sequence of at least 10 digits must follow
			if ( tt && ! isSessionId ( tt + x, titleRecVersion ) )
				tt = NULL;
		}
		// this is a new session id thing
		if ( ! tt && titleRecVersion >= 54 ) { 
			tt = strstr ( p , "sid=" ); x =  4;
			// a hex sequence of at least 10 digits must follow
			if ( tt && ! isSessionId ( tt + x, titleRecVersion ) ) 
				tt = NULL;
		}
		// osCsid and XTCsid are new session ids
		if ( ! tt && titleRecVersion >= 57 ) { 
			tt = strstr ( p , "osCsid=" ); 
			x =  7;
			if ( ! tt ) tt = strstr ( p , "XTCsid=" );
			// a hex sequence of at least 10 digits must follow
			if ( tt && ! isSessionId ( tt + x, titleRecVersion ) ) 
				tt = NULL;
		}
		// fixes for bug of matching plain &sessionid= first and
		// then realizing char before is an alnum...
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = gb_strcasestr ( p, "jsessionid="); x = 11; }
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = gb_strcasestr ( p , "vbsessid="  ); x =  9;}
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = gb_strcasestr ( p, "asesessid=" ); x = 10; }
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = gb_strcasestr ( p, "nlsessid="  ); x =  9; }
		if ( ! tt && titleRecVersion >= 59 ) { 
			tt = gb_strcasestr ( p, "psession="  ); x =  9; }

		if ( ! tt ) { tt = gb_strcasestr ( p , "session_id="); x = 11;}
		if ( ! tt ) { tt = gb_strcasestr ( p , "sessionid=" ); x = 10;}
		if ( ! tt ) { tt = gb_strcasestr ( p , "sessid="    ); x =  7;}
		if ( ! tt ) { tt = gb_strcasestr ( p , "vbsessid="  ); x =  9;}
		if ( ! tt ) { tt = gb_strcasestr ( p , "session="   ); x =  8;}
		if ( ! tt ) { tt = gb_strcasestr ( p , "session/"   ); x =  8;}
		if ( ! tt ) { tt = gb_strcasestr ( p , "POSTNUKESID=");x = 12;}
		// some new session ids as of Feb 2005
		if ( ! tt ) { tt = gb_strcasestr ( p, "auth_sess=" ); x = 10; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "mysid="     ); x =  6; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "oscsid="    ); x =  7; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "cg_sess="   ); x =  8; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "galileoSession");x=14; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "asesessid=" ); x = 10; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "nlsessid="  ); x =  9; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "jsessionid="); x = 11; }
		if ( ! tt ) { tt = gb_strcasestr ( p, "psession="  ); x =  9; }
		// new as of Jan 2006. is hurting news5 collection on gb6
		if ( ! tt ) { tt = gb_strcasestr ( p, "sess="      ); x =  5; }
		
		// .php?s=8af9d6d0d59e8a3108f3bf3f64166f5a&
		// .php?s=eae5808588c0708d428784a483083734&
		// .php?s=6256dbb2912e517e5952caccdbc534f3&
		if ( ! tt && (tt = strstr ( p-4 , ".php?s=" )) ) {
			// point to the value of the s=
			char *pp = tt + 7; 
			int32_t i = 0;
			// ensure we got 32 hexadecimal chars
			while ( pp[i] && 
				( is_digit(pp[i]) || 
				  ( pp[i]>='a' && pp[i]<='f' ) ) ) i++;
			// if not, do not consider it a session id
			if ( i < 32 ) tt = NULL;
			// point to s= for removal
			else { tt += 5; x = 2; }
		}
		// bail if none were found
		if ( ! tt ) goto skip;
		// . must not have an alpha char before it!
		// . prevent "DAVESID=" from being labeled as session id
		if ( is_alnum_a ( *(tt-1) ) ) goto skip;
		// start of the shit
		int32_t a = tt - s;
		// get the end of the shit
		int32_t b = a + x;
		// back up until we hit a ? or & or / or ;
		while ( a > 0 && s[a-1] != '?' && s[a-1] != '&' &&
			s[a-1] != '/' && s[a-1] != ';' ) a--;
		// keep the '?'
		if ( s[a]=='?' ) a++;
		// back up over any semicolon
		if ( s[a-1] == ';' && titleRecVersion >= 59 ) a--;
		// advance b until we hit & or end or ? or a ';'
		while ( s[b] && s[b] != '&' && s[b] != '?' && s[b] != ';') b++;
		// if we don't have 5+ chars in session id itself, skip it
		if ( b - (a + x) < 5 ) goto skip;
		// go over a & or a ;
		if ( s[b] == '&' || s[b] == ';' ) b++;
		// remove the session id by covering it up
		memmove ( &s[a] , &s[b] , len - b );
		// reduce length
		len -= (b-a);
		// if s ends in ? or & or ;, backup
		while ( len > 0 &&
			(s[len-1]=='?'||s[len-1]=='&'||s[len-1]==';')) len--;
		// NULL terminate
		s[len] = '\0';
	}

 skip:
	// remove common filenames like index.html
	if ( stripCommonFile ) {
		if ( len - 14 > 0 &&
		     strncasecmp(&s[len-14], "/default.xhtml", 14) == 0 )
			len -= 13;
		else if ( len - 13 > 0 &&
			( strncasecmp(&s[len-13], "/default.html", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.ascx", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.ashx", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.asmx", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.xhtm", 13) == 0 ||
		          strncasecmp(&s[len-13], "/default.aspx", 13) == 0 ) )
			len -= 12;
		else if ( len - 12 > 0 &&
		        ( strncasecmp(&s[len-12], "/default.htm", 12) == 0 ||
		          strncasecmp(&s[len-12], "/default.php", 12) == 0 ||
		          strncasecmp(&s[len-12], "/default.asp", 12) == 0 ||
		          strncasecmp(&s[len-12], "/index.xhtml", 12) == 0 ) )
			len -= 11;
		else if ( len - 11 > 0 &&
		        ( strncasecmp(&s[len-11], "/index.html", 11) == 0 ||
		          strncasecmp(&s[len-11], "/index.aspx", 11) == 0 ||
		          strncasecmp(&s[len-11], "/index.xhtm", 11) == 0 ||
		          strncasecmp(&s[len-11], "/default.pl", 11) == 0 ||
		          strncasecmp(&s[len-11], "/default.cs", 11) == 0 ) )
			len -= 10;
		else if ( len - 10 > 0 &&
			( strncasecmp(&s[len-10], "/index.htm", 10) == 0 ||
			  strncasecmp(&s[len-10], "/index.php", 10) == 0 ||
			  strncasecmp(&s[len-10], "/index.asp", 10) == 0 ||
			  strncasecmp(&s[len-10], "/main.html", 10) == 0 ||
			  strncasecmp(&s[len-10], "/main.aspx", 10) == 0 ) )
			len -= 9;
		else if ( len - 9 > 0 &&
			( strncasecmp(&s[len-9], "/index.pl", 9) == 0 ||
			  strncasecmp(&s[len-9], "/main.htm", 9) == 0 ||
			  strncasecmp(&s[len-9], "/main.php", 9) == 0 ) )
			len -= 8;
		else if ( len - 8 > 0 &&
			( strncasecmp(&s[len-8], "/main.pl", 8) == 0 ) )
			len -= 7;
		s[len] = '\0';
	}
	

	// replace the "\" with "/" -- a common mistake
	int32_t j;
	for ( j = 0 ; s[j] ; j++) if (s[j]=='\\') s[j]='/';
	// . dig out the protocol/scheme for this s (check for ://)
	// . protocol may only have alnums and hyphens in it
	for ( i = 0 ; s[i] && (is_alnum_a(s[i]) || s[i]=='-') ; i++ );
	// if we have a legal protocol, then set "m_scheme", "slen" and "sch" 
	// and advance i to the m_host
	if ( i + 2 < len && s[i]==':' && s[i+1]=='/' && s[i+2]=='/') {
		// copy lowercase protocol to "m_url"
		to_lower3_a ( s , i + 3 , m_url ); 
		m_scheme = m_url;
		m_slen   = i;
		m_ulen   = i + 3;
		i += 3;
	}
	else if (i + 2 < len && s[i]==':' && s[i+1]=='/'&& is_alnum_a(s[i+2])){
		// copy lowercase protocol to "m_url"
		to_lower3_a ( s , i + 2 , m_url ); 
		// add in needed /
		m_url[i+2]='/';
		m_scheme = m_url;
		m_slen   = i;
		m_ulen   = i + 3;
		i += 2;
	}
	// callto:+441202300007  (skype links)
	// mailto:blah@blah.com
	/*
	else if ( i+1 < len && s[i]==':' && version >= 62 ) {
		// copy lowercase protocol to "m_url"
		to_lower3_a ( s , i + 1 , m_url ); 
		// add in needed /
		m_url[i+2]='/';
		m_scheme = m_url;
		m_slen   = i;
		m_ulen   = i + 3;
		i += 2;
	}
	*/
	// otherwise we had no syntactically correct protocol
	else {
		gbmemcpy ( m_url,"http://" , 7 );
		m_scheme = m_url;
		m_slen   = 4;
		m_ulen   = 7;
		i        = 0;
		// if s started with // then skip that (slashdot)
		if ( s[0]=='/' && s[1]=='/' ) i = 2;
	}
	// . now &s[i] should point to the m_host name
	// . chars allowed in hostname = period,alnum,hyphen,underscore
	// . stops at '/' or ':' or any other disallowed character
	j = i;
	while (s[j] && (is_alnum_a(s[j]) || s[j]=='.' || s[j]=='-'||s[j]=='_'))
		j++;
	// copy m_host into "s" (make it lower case, too)
	to_lower3_a ( s + i, j - i, m_url + m_ulen );
	m_host    = m_url + m_ulen;
	m_hlen    = j - i;
	// common mistake: if hostname ends in a . then back up
	while ( m_hlen > 0 && m_host[m_hlen-1]=='.' ) m_hlen--;
	// NULL terminate for strchr()
	m_host [ m_hlen ] = '\0';
	// . common mistake: if hostname has no '.' in it append a ".com"
	// . now that we use hosts in /etc/hosts we no longer do this
	//if ( m_hlen > 0 && strchr ( m_host ,'.' ) == NULL ) {
	//	gbmemcpy ( &m_host[m_hlen] , ".com" , 4 );
	//	m_hlen += 4;
	//}
	// advance m_ulen to end of hostname
	m_ulen += m_hlen;
	// . set our m_ip if hostname is in a.b.c.d format
	// . this returns 0 if not a valid ip string
	m_ip = atoip ( m_host , m_hlen );
	// advance i to the : for the port, if it exists
	i = j;
	// NULL terminate m_host for getTLD(), getDomain() and strchr() below
	m_host [ m_hlen ] = '\0';
	// use ip as domain if we're just an ip address like 1.2.3.4
	if ( m_ip ) {
		// ip address has no tld, or mid domain
		m_tld    = NULL;
		m_tldLen = 0;
		// but it does have a domain (1.2.3)
		m_domain = getDomainOfIp ( m_host , m_hlen , &m_dlen );
		// just use the domain as the mid domain for ip-based urls
		m_mdlen  = m_dlen;
	}
	// . otherwise, get the tld
	// . uses thorough list of tlds in Domains.cpp
	else if ( ( m_tld = ::getTLD ( m_host, m_hlen ) ) && m_tld > m_host ) {
		// set m_domain if we had a tld that's not equal to our host
		m_tldLen = gbstrlen ( m_tld  );
		m_domain = ::getDomain ( m_host , m_hlen , m_tld , &m_dlen );
		// set the mid domain length (-1 for the '.')
		m_mdlen  = m_dlen - m_tldLen - 1;
	}
	// otherwise, we're no ip and we have no valid domain
	else {
		m_domain = NULL;
		m_dlen   = 0;
		m_tldLen = 0;
		m_mdlen  = 0;
	}

	// . if domain same as host then we might insert a "www." server name
	// . however, must have a period in domain name
	// . otherwise a domain name of "xxx" would become "www.xxx" and if
	//   Url::set() is called on that it would be "www.www.xxx" (bad bad)
	// . let's only add "www." if there's only 1 period, ok?
	if ( ! m_ip && addWWW && m_host == m_domain  && strchr(m_host,'.') ) {
		memmove ( m_host + 4 , m_host , m_hlen );
		gbmemcpy ( m_host , "www." , 4 );
		if ( m_domain ) m_domain += 4;
		if ( m_tld    ) m_tld    += 4;
		m_ulen += 4;
		m_hlen += 4;
	}
	// set the default port based on the protocol
	m_defPort = 80;
	if ( m_slen==5 && strncmp(m_scheme, "https",5)==0 ) m_defPort = 443;
	if ( m_slen==3 && strncmp(m_scheme, "ftp"  ,3)==0 ) m_defPort =  21;
	// assume we're using the default port for this scheme/protocol
	m_port = m_defPort;
	m_portStr = NULL;
	// see if a port was provided in the hostname after a colon
	if ( s[i] == ':' ) { 
		// remember the ptr so far
		int32_t savedLen = m_ulen;
		// add a colon to our m_url
		m_url [ m_ulen++ ] = ':';
		// scan for a '/' 
		j = i + 1;
		while ( s[j] && s[j]!='/') m_url[m_ulen++] = s[j++];
		// now read our port
		m_portStr = s + i; // str includes ':'
		m_port = atol2 ( s + (i + 1) , j - (i + 1) );
		// if it's the default port, then remove what we copied
		if ( m_port == m_defPort ) m_ulen = savedLen;
		// make i point to the root / in the m_path, if any
		i = j; 
	}
	// how many chars is taken up by a specified port?
	m_portLen = 0;
	if ( m_port != m_defPort ) {
		m_portLen += 2; // :3
		if ( m_port >= 10    ) m_portLen += 1;
		if ( m_port >= 100   ) m_portLen += 1;
		if ( m_port >= 1000  ) m_portLen += 1;
		if ( m_port >= 10000 ) m_portLen += 1;
	}
	//m_site = m_url;
	//m_siteLen = m_ulen+1;
	// append a '/' to m_url then bail if there is no m_path after the port
	if ( s[i]=='\0' || s[i] != '/') {
		m_path    = m_url + m_ulen;
		m_path[0] = '/';
		m_plen    = 1;
		m_url[ ++m_ulen ]='\0';
		// debug change
		goto done;
		return;
	}
	// . get the m_path and m_path length
	// . j,i should point to start of path slash '/'
	// . scan so it points to end or a ? or # 
	j = i;
	// now we include # as part of the path if it is a hash bang '#!'
	// which was the web-breaking google hack that is now deprecated
	while ( s[j] && s[j]!='?' ) {
		if ( s[j] == '#' && s[j+1] != '!' )
			break;
		j++;
	}
	// point the path inside m_url even though we haven't written it yet
	m_path = m_url + m_ulen;
	m_plen = m_ulen; 
	// . deal with wierd things in the path
	// . i points to start of path (should be /)
	for (; i < j ; i++ ) {
		// dedup double backslashes
		// ensure m_ulen >= m_plen so we don't hurt "http:///" ...
		// but people sometimes put http:// in the *path*
		if ( s[i] == '/'  &&  m_url[m_ulen-1] == '/' &&
		     m_ulen-1 >= m_plen && 
		     m_ulen >= 2 && m_url[m_ulen-2] != ':' ) continue;
		// deal with current directories in the m_path
		if ( s[i] == '.'  &&  m_url[m_ulen-1] == '/' && 
		     (i+1 == j || s[i+1]=='/'))	continue;
		// . deal with damned ..'s in the m_path
		// . if next 2 chars are .'s and last char we wrote was '/'
		if ( s[i] == '.' && s[i+1]=='.' && m_url[m_ulen-1] == '/' ) {
			// dont back up over first / in path
			if ( m_url + m_ulen - 1 > m_path ) m_ulen--;
			while ( m_url[m_ulen-1] != '/'   ) m_ulen--;
			// skip i to next / after these 2 dots
			while ( s[i] && s[i]!='/' ) i++;
			continue;
		}
		// don't allow ; before the ?...probably because of stripped 
		// sessionId...
		// I was going to add other possible dup separators, but now
		// it seems as though it might cause problems
		if (titleRecVersion >= 78){
			if (s[i] == ';' && s[i+1] == '?') continue;
		}

		// store char and advance to next
		m_url[m_ulen++] = s[i];
	}
	// reset the path length in case we had to remove some wierd stuff
	m_plen = m_ulen - m_plen;

	// . remove trailing /'s from path, but not first one!
	// . NO! this is WRONG! we need it so we know it's a dir name
	//while ( m_plen > 1 && m_path[m_plen-1]=='/' ) { m_plen--; m_ulen--; }
	// . get the m_query
	// . the query is anything after the path that starts with ?
	// . NOTE: we ignore strings beginning with '#' (page relative anchors)
	if ( i < len && s[i] != '#' ) {
		//gbmemcpy ( m_url + m_ulen , s + i , len - i );
		//remove back to back &'s in the cgi query
		//http://www.nyasatimes.com/national/politics/160.html?print&&&
		char *kstart = s + i;
		char *kend   = s + i + (len - i);
		char *dst    = m_url + m_ulen;
		for ( char *k = kstart ; k < kend ;  k++ ) {
			// skip & if we just did one
			if ( *k == '&' && k > kstart && *(k-1)=='&' ) continue;
			// copy over one char at a time
			*dst++ = *k;
		}
		// point after the '?' i guess
		m_query   = m_url + m_ulen + 1;
		//m_qlen  = len - i - 1;
		m_qlen    = dst - m_query;
		m_ulen += m_qlen + 1;
	}
	// get the m_filename from the m_path (m_flen might be 0)
	m_flen = 0;
	while (m_path[m_plen-1-m_flen]!='/' && m_flen<m_plen) m_flen++;
	m_filename = m_path + m_plen - m_flen;
	// get the m_extension from the m_path
	m_elen = 0;
	while (is_alnum_a(m_path[m_plen-1-m_elen]) && m_elen < m_plen)m_elen++;
	if ( m_path[ m_plen-1-m_elen] != '.' ) m_elen = 0; // no m_extension
	m_extension = m_path + m_plen - m_elen;
	// null terminate our s
	m_url[ m_ulen ]='\0';
	// add the anchor after
	m_anchor = NULL;
	m_anchorLen = anchorLen;
	if ( anchorLen > 0 &&
	     m_ulen + anchorLen + 2 < MAX_URL_LEN ) {
		m_anchor = &m_url[m_ulen+1];
		gbmemcpy(&m_url[m_ulen+1], &t[anchorPos], anchorLen);
		m_url[m_ulen+1+anchorLen] = '\0';
	}
 done:
	// debug msg
	//log("--------------%s has domain \"",s);
	//for (int32_t k=0;k <m_dlen; k++ ) log("%c",m_domain[k]);
	//log("\"\n");
	// check for iterative stablization
	static int32_t flag = 0;
	if ( flag == 1 ) return;
	Url u2;
	flag = 1;
	// Must not use defaults!
	u2.set ( m_url, m_ulen , addWWW, stripSessionId ,
		 stripPound , stripCommonFile , titleRecVersion );
	if ( strcmp(u2.getUrl(),m_url) != 0 ) {
		log(LOG_REMIND,"db: *********url %s-->%s\n",m_url,u2.getUrl());
		//sleep(5000);
	}
	flag = 0;
}

char Url::isSessionId ( char *hh, int32_t titleRecVersion ) {
	int32_t count = 0;
	int32_t step = 0;
	int32_t nonNumCount = 0;
	// old bug didn't step through characters
	if (titleRecVersion >= 69) step = 1;
	// do not limit count to 12, the hex numbers may only be
	// after the 12th character! we were not identifying these
	// as sessionids when we shold have been because of that.
	for ( ; *hh ; count++, hh+=step ) {
		if ( *hh >= '0' && *hh <= '9' ) continue;
		nonNumCount++;
		if ( *hh >= 'a' && *hh <= 'f' ) continue;
		// we got an illegal session id character
		return false;
	}
	// if we got at least 12 of em, consider it a valid id
	if (titleRecVersion >= 69)
		// make sure it's a hexadecimal number...lots of product
		// ids and dates use only decimal numbers
		return ( nonNumCount > 0 && count >= 12);

	return ( count >= 12 );
}

// hostname must also be www or NULL to be a root url
bool Url::isRoot() {
	if ( m_plen    != 1              ) return false;
	if ( !m_path || m_path[0] != '/' ) return false;
	if ( m_query                     ) return false;
	// for now we'll let all thos *.deviantart.com names clog us up
	// because i don't want to dis' stuff like espn.go.com
	return true;
	// get just the hostname w/o the domain (includes '.' following name)
	//int32_t nameLen = m_hlen - m_dlen ;
	//if ( nameLen   <= 0   ) return true;
	//if ( nameLen   != 4   ) return false; // "www."
	//if ( strncmp ( m_host , "www" , 3 ) != 0 ) return false;
	//return true;
}

// a super root url is a root url where the hostname is NULL or "www"
bool Url::isSuperRoot () {
	if ( ! isRoot() ) return false;
	// if hostname is same as domain, it's a super root
	if ( m_host == m_domain && m_hlen == m_dlen ) return true;
	// if host is not "www." followed by domain, it's NOT a super root
	if ( m_hlen != m_dlen + 4 ) return false;
	if ( strncmp ( m_host , "www." , 4 ) == 0 ) return true;
	return false;
}

bool Url::isSimpleSubdomain ( ) {
	// if hostname is same as domain, it's passes
	if ( m_host == m_domain && m_hlen == m_dlen ) return true;
	// if host is not "www." followed by domain, it's NOT
	if ( m_hlen != m_dlen + 4 ) return false;
	if ( strncmp ( m_host , "www." , 4 ) == 0 ) return true;
	return false;
}

// . get length of sub-url #j
// . basically like adding j /.. to the end of the url
// . sub-url #0 is the full url
// . includes /~ as it's own path
int32_t Url::getSubUrlLen ( int32_t j ) {

	// assume it's the whole url
	int32_t len = m_ulen;

	// subtract the m_query (cgi) part at the end of the url
	if ( m_query ) len -= m_qlen + 1; //and the ?
	
	// return the full url (without m_query) if j is 0
	if ( j == 0 ) return len;

	// . start right past the http://m_host.domain.com/
	int32_t start = m_slen + 3 + m_hlen + 1 + m_portLen ;
	while ( len > start ) {
		if ( m_url [ len - 1 ] == '/'                            ) j--;
		if ( m_url [ len - 2 ] == '/' && m_url [ len - 1 ] == '~') j--;
		// include this backslash (or ~) in the sub-url
		if ( j == 0 ) return len;
		// shrink by one character
		len--;
	}

	// return 0 if jth sub-url does not exist
	return 0;
}

// . similar to getSubUrlLen() above but only works on the path
// . if j is 0 that's the whole url path!
int32_t Url::getSubPathLen ( int32_t j ) {
	int32_t subUrlLen = getSubUrlLen ( j );
	if ( subUrlLen <= 0 ) return 0; 
	// . the subPath length includes the root backslash
	// . portLen includes the whole :8080 thing (for non default ports)
	return subUrlLen - m_slen - 3 - m_hlen - m_portLen; 
}

void Url::print() {

	printf("############ url ############\n");

	printf("url: %s\n",m_url);

	printf("host: ");
	print_string( m_host, m_hlen );
	printf("\n");


	printf("scheme: ");
	print_string( m_scheme , m_slen );
	printf("\n");


	printf("path: ");
	print_string(m_path , m_plen );
	printf("\n");

	printf("query: %s\n",m_query);

	printf("port: %"INT32"\n", m_port );

	printf("domain: ");
	print_string(m_domain, m_dlen );
	printf("tld: ");
	print_string(m_tld, m_tldLen );
	printf("mid domain: ");
	print_string(m_domain, m_mdlen );
	printf("\n");

	printf("is root %i\n",isRoot());
}

void print_string ( char *s , int32_t len ) {
	int32_t i = 0;
	if ( ! s ) return;
	while ( i < len ) printf("%c",s[i++]);
}

bool Url::isExtensionIndexable () {
	// assume no extension is html
	if ( m_elen == 0   ) return true;
	if ( ! m_extension ) return true;
	// no matter what the extension, if it has cgi parms, let it through.
	if ( m_query ) return true;
	// now we index source code
	if ( m_elen == 1 ) {
		if ( m_extension[0] == 'c' ) return true;
		if ( m_extension[0] == 'h' ) return true;
	}
	else if ( m_elen == 2 ) {
		if ( strncasecmp ( m_extension , "ps", 2 ) == 0 ) return true;
		// perl is used like cgi
		if ( strncasecmp ( m_extension , "pl", 2 ) == 0 ) return true;
		return false;
	}
	else if ( m_elen == 3 ) {
		if ( strncasecmp ( m_extension , "htm", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "asp", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "xml", 3 ) == 0 ) return true;
		// rss has a Content-Type of xml usually
		if ( strncasecmp ( m_extension , "rss", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "cgi", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "dll", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "jsp", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "php", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "txt", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "pdf", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "doc", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "xls", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "ppt", 3 ) == 0 ) return true;
		// now we index source code
		if ( strncasecmp ( m_extension , "cpp", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "hpp", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "vbs", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "frm", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "bas", 3 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "jsp", 3 ) == 0 ) return true;
		// probably cold fusion template file. seems to be text/html
		if ( strncasecmp ( m_extension , "cfm", 3 ) == 0 ) return true;
		//http://news.bbc.co.uk/2/hi/science/nature/default.stm
		if ( strncasecmp ( m_extension , "stm", 3 ) == 0 ) return true;
		// rdf for rss feeds
		if ( strncasecmp ( m_extension , "rdf", 3 ) == 0 ) return true;
		return false;
	}
	else if ( m_elen == 4 ) {
		if ( strncasecmp ( m_extension , "html",4 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "phtm",4 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "shtm",4 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "fcgi",4 ) == 0 ) return true;
		if ( strncasecmp ( m_extension , "aspx",4 ) == 0 ) return true;
		// php3 and php4 etc.
		if ( strncasecmp ( m_extension , "php" ,3 ) == 0 ) return true;
		// now we index source code
		if ( strncasecmp ( m_extension , "java",4 ) == 0 ) return true;
		return false;
	}
	else if ( m_elen == 5 ) {
		if ( strncasecmp ( m_extension, "shtml", 5) == 0 ) return true;
		if ( strncasecmp ( m_extension, "jhtml", 5) == 0 ) return true;
		if ( strncasecmp ( m_extension, "phtml", 5) == 0 ) return true;
		if ( strncasecmp ( m_extension, "story", 5) == 0 ) return true;
		return false;
	}
	return false;
}

// . return url w/o http://
// . without trailing / if path is just "/"
// . without "www." if in hostname and "rmWWW" is true
// . sets *len to it's length
char *Url::getShorthandUrl  ( bool rmWWW , int32_t *len ) {
	char *u    = m_url;
	int32_t  ulen = m_ulen;
	if ( ulen > 7 && strncasecmp ( u , "http://" , 7 ) == 0) { 
		u    += 7 ; 
		ulen -= 7 ;
		// if hostname is just "www" then skip iff rmWWW is true
		if ( rmWWW && m_hlen >= 4 &&  strncmp(m_host,"www.",4) == 0) { 
			u    += 4; 
			ulen -= 4; 
		}
	}
	// skip trailing /
	if ( m_plen == 1 && m_path[0]=='/'  && m_query == NULL ) ulen--;
	// set the length
	*len = ulen;
	// return the url int16_thand
	return u;
}

int32_t  Url::getPathDepth ( bool countFilename ) {
	char *s     = m_path + 1;
	char *send  = m_url + m_ulen;
	int32_t  count = 0;
	while ( s < send ) if ( *s++ == '/' ) count++;
	// if we're counting the filename as a path component...
	if ( countFilename && *(send-1) != '/' ) count++;
	return count;
}

char *Url::getPathComponent ( int32_t num , int32_t *clen ) {
	// start countint at path
	char *start = m_path;
	char *p     = m_path;
	char *pend  = m_path + m_plen;
	int32_t  count = 0;
	// loop up here for each component
 loop:
	// skip the '/'
	p++;
	start++;
	// advance until next / or end
	while ( p < pend && *p != '/' ) p++;
	// set length of that
	if ( clen ) *clen = p - start;
	// all done?
	if ( count == num ) return start;
	// if none, simply does not exist
	if ( p >= pend ) return NULL;
	// advance
	count++;
	// set start to p now
	start = p;
	goto loop;
}

//char *Url::getPathEnd ( int32_t num ) {
//	// get component
//	int32_t  pclen = 0;
//	char *pc    = getPathComponent ( num , &pclen );
//	// return the end of it
//	return pc + pclen;
//}





bool Url::isHostWWW ( ) {
	if ( m_hlen < 4 ) return false;
	if ( m_host[0] != 'w' ) return false;
	if ( m_host[1] != 'w' ) return false;
	if ( m_host[2] != 'w' ) return false;
	if ( m_host[3] != '.' ) return false;
	return true;
}

// . is the url a porn/spam url?
// . i use /usr/share/dict/words to check for legit words
// . if it's int32_t and has 4+ hyphens, consider it spam
// . if you add a word here, add it to PageResults.cpp:isQueryDirty()
bool Url::isSpam() {
	// store the hostname in a buf since we strtok it
	char s [ MAX_URL_LEN ];
	// don't store the .com or .org while searching for isSpam
	int32_t  slen = m_hlen - m_tldLen - 1;
	gbmemcpy ( s , m_host , slen );
	if ( ! m_domain ) return false;
	if ( ! m_dlen   ) return false;
	//int32_t  len = m_dlen;
	//gbmemcpy ( s , m_domain , len );
	// if tld is gov or edu or org, not porn
	if ( m_tldLen >= 3 && strncmp ( m_tld , "edu" , 3 )==0 ) return false;
	if ( m_tldLen >= 3 && strncmp ( m_tld , "gov" , 3 )==0 ) return false;
	// NULL terminate for strstr
	s[slen]='\0';
	// . if there is 4 or more hyphens, and hostLen > 30 consider it spam
	// . actually there seems to be a lot of legit sites with many hyphens
	if ( slen > 30 ) {
		int32_t count = 0;
		char *p = s;
		while ( *p ) if ( *p++ == '-' ) count++;
		if ( count >= 4 ) return true;
	}

	//
	// TODO: use getMatch()!!!! +pts -pts system
	// 

	// check each thing separated by periods for porn
	char *send = s + slen;
	char *p    = s;

 loop:
	// if done, return
	if ( p >= send ) return false;
	// find the next period or hyphen
	char *pend = p;
	while ( pend < send && *pend != '.' && *pend !='-' ) pend++;
	// ok NULL terminate it
	*pend = '\0';
	// check that
	if ( isSpam ( p , pend - p ) ) return true;
	// point to next
	p = pend + 1;
	// loop back
	goto loop;
}

bool Url::isSpam ( char *s , int32_t slen ) {	

	// no need to indent below, keep it clearer
	if ( ! isAdult ( s, slen ) ) return false;

	// check for naughty words. Split words to deep check if we're surely 
	// adult. Required because montanalinux.org is showing up as porn 
	// because it has 'anal' in the hostname.
	// send each phrase seperately to be tested.
	// hotjobs.yahoo.com
	char *a = s;
	char *p = s;
	bool foundCleanSequence = false;
	char splitWords[1024];
	char *splitp = splitWords;
	while ( p < s + slen ){
		while ( p < s + slen && *p != '.' && *p != '-' )
			p++;
		bool isPorn = false;
		// TODO: do not include "ult" in the dictionary, it is
		// always splitting "adult" as "ad ult". i'd say do not
		// allow it to split a dirty word into two words like that.
		if ( g_speller.canSplitWords( a, p - a, &isPorn,
					      splitp,
					      langEnglish,
					      csUTF8 ) ){
			if ( isPorn ){
				log(LOG_DEBUG,"build: identified %s as "
				    "porn  after splitting words as "
				    "%s", s, splitp);
				return true;
			}
			foundCleanSequence = true;
			// keep searching for some porn sequence
		}
		p++;
		a = p;
		splitp += gbstrlen(splitp);
	}
	// if we found a clean sequence, its not porn
	if ( foundCleanSequence ) {
		log(LOG_INFO,"build: did not identify url %s "
		    "as porn after splitting words as %s", s, splitWords);
		return false;
	}
	// we tried to get some seq of words but failed. Still report
	// this as porn, since isAdult() was true
	logf ( LOG_DEBUG,"build: failed to find sequence of words to "
	      "prove %s was not porn.", s );
	return true;

	/*	if ( strstr ( s , "upskirt"     ) ) return true;
	if ( strstr ( s , "downblouse") ) return true;
	if ( strstr ( s , "adult"     ) ) return true;
	if ( strstr ( s , "shemale"   ) ) return true;
	if ( strstr ( s , "spank"     ) ) return true;
	if ( strstr ( s , "dildo"     ) ) return true;
	if ( strstr ( s , "shaved"    ) ) return true;
	if ( strstr ( s , "bdsm"      ) ) return true;
	if ( strstr ( s , "voyeur"    ) ) return true;
	if ( strstr ( s , "shemale"   ) ) return true;
	if ( strstr ( s , "fisting"   ) ) return true;
	if ( strstr ( s , "escorts"   ) ) return true;
	if ( strstr ( s , "vibrator"  ) ) return true;
	if ( strstr ( s , "rgasm"     ) ) return true; // 0rgasm
	if ( strstr ( s , "orgy"      ) ) return true; 
	if ( strstr ( s , "orgies"    ) ) return true; 
	if ( strstr ( s , "masturbat" ) ) return true; 
	if ( strstr ( s , "stripper"  ) ) return true; 
	if ( strstr ( s , "lolita"    ) ) return true; 
	//if ( strstr ( s , "hardcore"  ) ) return true; ps2hardcore.co.uk
	if ( strstr ( s , "softcore"  ) ) return true;
	if ( strstr ( s , "whore"     ) ) return true;
	if ( strstr ( s , "slut"      ) ) return true;
	if ( strstr ( s , "smut"      ) ) return true;
	if ( strstr ( s , "tits"      ) ) return true;
	if ( strstr ( s , "lesbian"   ) ) return true;
	if ( strstr ( s , "swinger"   ) ) return true;
	if ( strstr ( s , "fetish"    ) ) return true;
	if ( strstr ( s , "housewife" ) ) return true;
	if ( strstr ( s , "housewive" ) ) return true;
	if ( strstr ( s , "nude"      ) ) return true;
	if ( strstr ( s , "bondage"   ) ) return true;
	if ( strstr ( s , "centerfold") ) return true;
	if ( strstr ( s , "incest"    ) ) return true;
	if ( strstr ( s , "pedophil"  ) ) return true;
	if ( strstr ( s , "pedofil"   ) ) return true;
	// hornyear.com
	if ( strstr ( s , "horny"     ) ) return true;
	if ( strstr ( s , "pussy"     ) ) return true;
	if ( strstr ( s , "pussies"   ) ) return true;
	if ( strstr ( s , "penis"     ) ) return true;
	if ( strstr ( s , "vagina"    ) ) return true;
	if ( strstr ( s , "phuck"     ) ) return true;
	if ( strstr ( s , "blowjob"   ) ) return true;
	if ( strstr ( s , "gangbang"  ) ) return true;
	if ( strstr ( s , "xxx"       ) ) return true;
	if ( strstr ( s , "porn"      ) ) return true;
	if ( strstr ( s , "felch"     ) ) return true;
	if ( strstr ( s , "cunt"      ) ) return true;
	if ( strstr ( s , "bestial"   ) ) return true;
	if ( strstr ( s , "beastial"  ) ) return true;
	//if ( strstr ( s , "oral"    ) ) return true; // moral, doctorial, ...
	// these below may have legit meanings
	if ( strstr ( s , "kink"      ) ) {
		if ( strstr ( s , "kinko"     ) ) return false; // the store
		return true;
	}
	if ( strstr ( s , "sex"     ) ) {
		// sexton, sextant, sextuplet, sextet
		if ( strstr ( s , "sext"    ) ) return false; 
		if ( strstr ( s , "middlesex" ) ) return false;
		if ( strstr ( s , "sussex"    ) ) return false;
		if ( strstr ( s , "essex"     ) ) return false;
		if ( strstr ( s , "deusex"    ) ) return false; // video game
		if ( strstr ( s , "sexchange" ) ) return false; // businessexh
		if ( strstr ( s , "sexpress"  ) ) return false; // *express
		if ( strstr ( s , "sexpert"   ) ) return false; // *expert
		if ( strstr ( s , "sexcel"    ) ) return false; // *excellence
		if ( strstr ( s , "sexist"    ) ) return false; // existence
		if ( strstr ( s , "sexile"    ) ) return false; // existence
		if ( strstr ( s , "harassm"   ) ) return false; // harassment
		if ( strstr ( s , "sexperi"   ) ) return false; // experience
		if ( strstr ( s , "transex"   ) ) return false; // transexual
		if ( strstr ( s , "sexual"    ) ) return false; // abuse,health
		if ( strstr ( s , "sexpo"     ) ) return false; // expo,expose
		if ( strstr ( s , "exoti"     ) ) return false; // exotic(que)
		if ( strstr ( s , "sexclu"    ) ) return false; // exclusive/de
		return true;
	}
	// www.losAnaLos.de
	// sanalcafe.net
	if ( strstr ( s , "anal") ) {
		if ( strstr ( s , "analog"    ) ) return false; // analogy
		if ( strstr ( s , "analy"     ) ) return false; // analysis
		if ( strstr ( s , "canal"     ) ) return false;
		if ( strstr ( s , "kanal"     ) ) return false; // german
		if ( strstr ( s , "banal"     ) ) return false;
		return true;
	}
	if ( strstr ( s , "cum") ) {
		if ( strstr ( s , "circum"    ) ) return false; // circumvent
		if ( strstr ( s , "magn"      ) ) return false; // magna cum
		if ( strstr ( s , "succu"     ) ) return false; // succumb
		if ( strstr ( s , "cumber"    ) ) return false; // encumber
		if ( strstr ( s , "docum"     ) ) return false; // document
		if ( strstr ( s , "cumul"     ) ) return false; // accumulate
		if ( strstr ( s , "acumen"    ) ) return false; // acumen
		if ( strstr ( s , "cucum"     ) ) return false; // cucumber
		if ( strstr ( s , "incum"     ) ) return false; // incumbent
		if ( strstr ( s , "capsicum"  ) ) return false; 
		if ( strstr ( s , "modicum"   ) ) return false; 
		if ( strstr ( s , "locum"     ) ) return false; // slocum
		if ( strstr ( s , "scum"      ) ) return false; 
		if ( strstr ( s , "accu"      ) ) return false; // compounds!
		// arcum.de
		// cummingscove.com
		// cumchristo.org
		return true;
	}
	//if ( strstr ( s , "lust"       ) ) {
	//	if ( strstr ( s , "illust"  ) ) return false; // illustrated
	//	if ( strstr ( s , "clust"   ) ) return false; // cluster
	//	if ( strstr ( s , "blust"   ) ) return false; // bluster
	//	if ( strstr ( s , "lustrad" ) ) return false; // balustrade
	//	// TODO: plusthemes.com wanderlust
	//	return true;
	//}
	// brettwatt.com
	//if ( strstr ( s , "twat"      ) ) {
	//	if ( strstr ( s , "watch"   ) ) return false; // wristwatch
	//	if ( strstr ( s , "atwater" ) ) return false;
	//	if ( strstr ( s , "water"   ) ) return false; // sweetwater
	//	return true;
	//}
	if ( strstr ( s, "clit" ) && ! strstr ( s, "heraclitus") ) return true;
	// fuckedcompany.com is ok
	if ( strstr ( s, "fuck" ) && ! strstr ( s, "fuckedcomp") ) return true;
	if ( strstr ( s, "boob" ) && ! strstr ( s, "booboo"    ) ) return true;
	if ( strstr ( s, "wank" ) && ! strstr ( s, "swank"     ) ) return true;
	// fick is german for fuck (fornication under consent of the king)
	if ( strstr ( s, "fick" ) && ! strstr ( s, "fickle") &&
	     ! strstr ( s , "traffick" ) ) return true;
	// sclerotic
	// buerotipp.de
	if ( strstr ( s, "eroti") && ! strstr ( s, "sclero"    ) ) return true;
	// albaberlin.com
	// babelfish.altavista.com
	if ( strstr ( s, "babe" ) && ! strstr ( s, "toyland"   ) &&
	     ! strstr ( s , "babel") ) return true;
	// what is gaya.dk?
	if ( strstr ( s , "gay" ) && ! strstr ( s, "gaylord"   ) ) return true;
	// url appears to be ok
	return false;*/
}


// . remove any session id
// . i'm sick of these tihngs causing dup problems
// . types:
// http://www.b.com/?PHPSESSID=737aec14eb7b360983d4fe39395
// http://www.b.com/cat.cgi/process?mv_session_id=xrf2EY3q&
// http://www.b.com/default?SID=f320a739cdecb4c3edef67e

// http://www.b.com/generic.html;$sessionid$QVBMODQAAAGNA?pid=7
// http://www.b.com/p.jhtml;jsessionid=J4QMFWBG1SPRVWCKUUXCJ0W?stuff=1
// look for ';'
// look for PHPSESSID, session_id, SID, jsessionid
// followed by string of at least 4 letters/numbers
		
//List of extensions NOT to parse
static char *s_badExtensions[] = {
        "ai",
        "aif",
        "aifc",
        "aiff",
        "asc",
        "au",
        "avi",
        "bcpio",
        "bin",
        "bmp",
        "bz2",
        //"c",
        //"cc",// c source code, allow
        "ccad",
        "cdf",
        //"class",// text source code file usually, allow
        "cpio",
        "cpt",
        //"csh",
        "css",
        "dcr",
        "dir",
        "dms",
        //"doc",
        "drw",
        "dvi",
        "dwg",
        "dxf",
        "dxr",
        "eps",
        "etx",
        "exe",
        "ez",
        //"f", // ambigous
        "f90",
        "fli",
        "gif",
        "gtar",
        "gz",
        //"h",
        "hdf",
        "hh",
        "hqx",
        //"htm",
        //"html",
        "ice",
        "ief",
        "iges",
        "igs",
        "ips",
        "ipx",
        "jpe",
        "jpeg",
        "jpg",
        //"js",
        "kar",
        "latex",
        "lha",
        "lsp",
        "lzh",
        //"m", // ambiguous
        "man",
        "me",
        "mesh",
        "mid",
        "midi",
        "mif",
        "mime",
        "mov",
        "movie",
        "mp2",
        "mp3",
        "mpe",
        "mpeg",
        "mpg",
        "mpga",
        "ms",
        "msh",
        "nc",
        "oda",
        "pbm",
        "pdb",
        //"pdf",
        "pgm",
        "pgn",
        "png",
        "pnm",
        "pot",
        "ppm",
        "pps",
	// "ppt",
        "ppz",
        "pre",
        "prt",
	// "ps",
        "qt",
        "ra",
        "ram",
        "ras",
        "rgb",
        "rm",
        "roff",
        "rpm",
	"deb", // debian/ubuntu package file
        "rtf",
        "rtx",
        "scm",
        "set",
        "sgm",
        "sgml",
        //"sh", // shells are text files
        "shar",
        "silo",
        "sit",
        "skd",
        "skm",
        "skp",
        "skt",
        "smi",
        "smil",
        "snd",
        "sol",
        "spl",
        "src",
        "step",
        "stl",
        "stp",
        "sv4cpio",
        "sv4crc",
        "swf",
        //"t", // ambiguous ... Mr.T.
        "tar",
        "tcl",
        "tex",
        "texi",
        "texinfo",
        "tif",
        "tiff",
        "tr",
        "tsi",
        "tsp",
        "tsv",
        //"txt",
        "unv",
        "ustar",
        "vcd",
        "vda",
        "viv",
        "vivo",
        "vrml",
        "wav",
        "wrl",
        "xbm",
        "xlc",
        "xll",
        "xlm",
        //"xls",
        "xlw",
        //"xml",
        "xpm",
        "xwd",
        "xyz",
        "zip",//
};//look below, I added 3 more types for TR version 73



static HashTable s_badExtTable;
static bool s_badExtInitialized;

//returns True if the extension is listed as bad
bool Url::isBadExtension ( int32_t version ) {
	//	return !isExtensionIndexable();
	
	if ( ! m_extension || m_elen == 0 ) return false;
	if(!s_badExtInitialized) { //if hash has not been created-create one
		int32_t i=0;
		//version 72 and before.
		do {
			int tlen = gbstrlen(s_badExtensions[i]);
			int64_t swh = hash64Lower_a(s_badExtensions[i],tlen);
			if(!s_badExtTable.addKey(swh,(int32_t)50))
				return false;
			i++;

		} while(strcmp(s_badExtensions[i],"zip")!=0);


		//version 73 and after.
		if(!s_badExtTable.addKey(hash64Lower_a("wmv", 3),
					 (int32_t)73) ||
		   !s_badExtTable.addKey(hash64Lower_a("wma", 3),
					 (int32_t)73) ||    
		   !s_badExtTable.addKey(hash64Lower_a("ogg", 3),
					 (int32_t)73))
			return false;
		
		s_badExtInitialized = true;
	}


	int myKey = hash64Lower_a(m_extension,m_elen);
	//zero unless we have a bad extention, otherwise
	//we return TR version in which it was banned
	int32_t badVersion = s_badExtTable.getValue(myKey);
	if (badVersion == 0) return false;
	//if(badVersion <= version) return true;
	if ( badVersion > version ) return false;
	// exceptions for .warc.gz .warc .arc .argc.gz
	if ( isWarc() || isArc() ) return false;
	return true;
}

bool Url::isWarc ( ) {

	// if ( ulen>8 && strncmp(uend-8,".warc.gz",8)==0 )
	// 	m_isWarc = true;
	// if ( ulen>8 && strncmp(uend-5,".warc"   ,5)==0 )
	// 	m_isWarc = true;

	// if ( ulen>8 && strncmp(uend-7,".arc.gz",7)==0 )
	// 	m_isArc = true;
	// if ( ulen>8 && strncmp(uend-4,".arc"   ,4)==0 )
	// 	m_isArc = true;

	if ( m_elen == 4 &&
	     m_extension[0] == 'w' &&
	     m_extension[1] == 'a' &&
	     m_extension[2] == 'r' &&
	     m_extension[3] == 'c' )
		return true;

	if ( m_elen == 2 && 
	     m_extension[0] == 'g' &&
	     m_extension[1] == 'z' &&
	     m_ulen > 10 &&
	     m_extension[-1] == '.' &&
	     m_extension[-2] == 'c' &&
	     m_extension[-3] == 'r' &&
	     m_extension[-4] == 'a' &&
	     m_extension[-5] == 'w' &&
	     m_extension[-6] == '.' ) {
		// m_isWarc = true;
		// m_isWarcValid = true;
		return true;
	}

	return false;
}

bool Url::isArc ( ) {

	if ( m_elen == 3 &&
	     m_extension[0] == 'a' &&
	     m_extension[1] == 'r' &&
	     m_extension[2] == 'c' )
		return true;
	     
	// hack to allow for .gz if it is .warc.gz or .arc.gz
	if ( m_elen == 2 && 
	     m_extension[0] == 'g' &&
	     m_extension[1] == 'z' &&
	     m_ulen > 10 &&
	     m_extension[-1] == '.' &&
	     m_extension[-2] == 'c' &&
	     m_extension[-3] == 'r' &&
	     m_extension[-4] == 'a' &&
	     m_extension[-5] == '.' ) {
		// m_isArc = true;
		// m_isArcValid = true;
		return true;
	}

	return false;
}

// see Url.h for a description of this.
bool Url::isLinkLoop ( ) {
	char *s             = m_path ;
	char *send          = m_url + m_ulen;
	int32_t  count         = 0;
	int32_t  components    = 0;
	bool  prevWasDouble = false;
	char *last          = NULL;
	if (!s) return false;
	// use this hash table to hash each path component in the url
	char  buf [ 5000 ];
	HashTable t; t.set ( 100 , buf , 5000 );
	// grab each path component
	for ( ; s < send ; s++ ) {
		if ( *s != '/' ) continue;
		// ok, add this guy to the hash table, if we had one
		if ( ! last ) { last = s; continue; }
		// give up after 50 components
		if ( components++ >= 50 ) return false;
		// hash him
		uint32_t h = hash32 ( last , s - last );
		// is he in there?
		int32_t slot = t.getSlot ( h );
		// get his val (count)
		int32_t val = 0;
		if ( slot >= 0 ) val = t.getValueFromSlot ( slot );
		// if not in there put him in a slot
		if ( slot < 0 ) {
			last = s;
			t.addKey ( h , 1 );
			continue;
		}
		// increment it
		val++;
		// does it occur 3 or more times? if so, we have a link loop
		if ( val >= 3 ) return true;
		// is it 2 or more? 
		if ( val == 2 ) count++;
		// if we have two such components, then we are a link loop.
		// BUT, we must be a pair!
		if ( count >= 2 && prevWasDouble ) return true;
		// set this so in case next guy is a double
		if ( val == 2 ) prevWasDouble = true;
		else            prevWasDouble = false;
		// add it back after incrementing
		t.setValue ( slot , val );
		// update "last"
		last = s;
	}
	return false;
}		

//
// here are some examples of link loops in urls:
//
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/letters/send/archive/bish/archive/bish/letters/bish/archive/lette\rs/send/archive/letters/send/bish/letters/archive/bish/letters/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/bish/letters/archive/bish/archive/letters/send/archive/letters/send/archive/le\tters/send/archive/letters/send/bish/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/bish/letters/send/archive/letters/send/archive/bish/archive/bish/\archive/bish/letters/send/archive/letters/archive/letters/send/archive/bish/let\ters/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/letters/send/archive/letters/archive/bish/archive/bish/archive/bi\sh/letters/send/archive/bish/archive/letters/send/bish/archive/bish/letters/sen\d/archive/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/bish/letters/send/archive/bish/letters/bish/letters/send/archive/\bish/archive/letters/bish/letters/send/archive/bish/letters/send/bish/archive/l\etters/bish/letters/archive/letters/send/
//http://www.pittsburghlive.com:8000/x/tribune-review/opinion/steigerwald/letters\/send/archive/bish/letters/send/archive/bish/letters/send/bish/archive/letters/\send/bish/archive/letters/send/archive/letters/bish/archive/bish/archive/letter\s/


bool Url::isIp() { 
	if(!m_host)            return false;
	if(!is_digit(*m_host)) return false; 
	return atoip ( m_host , m_hlen ); 
}

/*
bool Url::isSiteRoot ( char *coll , TagRec *tagRec ,
		       char **retSite , int32_t *retSiteLen ) {
	int32_t  siteLen;
	// use the DOMAIN as the default site
	char *site = getSite ( &siteLen , coll , false , tagRec );
	// check end of site
	char *send = site + siteLen;
	// our end
	char *uend = m_url + m_ulen;
	// backup over an ending '/'
	if ( uend[-1] == '/' ) uend--;
	// set it
	if ( retSite    ) *retSite    = site;
	if ( retSiteLen ) *retSiteLen = siteLen;
	// if before our end, we are not a site root
	return (uend <= send);
}

// . a "site" is a set of urls controlled/regulated primarily by the same 
//   entity.
// . this returns the smallest site containing the url, m_url
// . so fred.blogspot.com is considered a site regulated by "fred"
// . BUT blogspot.com is a larger site regulated by blogspot
// . the default site is the domain
// . returns NULL and sets g_errno on error
// . if "defaultToHostname" is true we default to the hostname
//   as opposed to the domain name.
char *Url::getSite ( int32_t *siteLen , char *coll , bool defaultToHostname ,
		     TagRec *tagRec ,
		     bool *isDefault ) {
	// clear just in case
	g_errno = 0;
	// convenience vars
	char *p;
	int32_t  len = 0;
	// assume we return the default
	if ( isDefault ) *isDefault = true;

	int32_t sitepathdepth = -1;
	// we may have a defined path depth
	Tag *tag = NULL;
	// see if we do
	if ( tagRec ) tag = tagRec->getTag("sitepathdepth");
	// sanity check
	if ( tag && tag->m_dataSize != 1 ) { char *xx=NULL;*xx=0; }
	// if there, get the sitepathdepth value it contains
	if ( tag ) sitepathdepth = (int32_t)tag->m_data[0];

	// . deal with site indicators
	// . these are applied to all domains uniformly
	// . if it is xyz.com/users/  use xyz.com/users/fred/ as the site
	p = m_path;
	// a lot of times these were not indivual blogs, but the blog subsite
	// of a site... http://dccc.org/blog/P4575/
	//if ( strncasecmp(p,"/blogs/"       , 7) == 0 ) len = 7;
	//if ( strncasecmp(p,"/blog/"        , 6) == 0 ) len = 6;
	// commented out a bunch cuz they were profiles mostly, not blogs...
	if ( strncasecmp(p,"/~"            , 2) == 0 ) len = 2;
	// assume this is a username. skip the first /
	if ( sitepathdepth == 1                      ) len = 1;
	//if ( strncasecmp(p,"/users/"       , 7) == 0 ) len = 7;
	//if ( strncasecmp(p,"/user/"        , 6) == 0 ) len = 6;
	//if ( strncasecmp(p,"/members/"     , 9) == 0 ) len = 9;
	//if ( strncasecmp(p,"/membres/"     , 9) == 0 ) len = 9;
	//if ( strncasecmp(p,"/member/"      , 8) == 0 ) len = 8;
	//if ( strncasecmp(p,"/membre/"      , 8) == 0 ) len = 8;
	//if ( strncasecmp(p,"/member.php?u=",14) == 0 ) len = 14;
	// point to after the /users/, /blogs/, /user/, /blog/ or /~xxx/
	p += len;
	// assume there is NOT an alpha char after this
	char username = false;
	// . skip to next / OR ?
	// . stop at . or -, because we do not allow those in usernames and
	//   they are often indicative of filenames without file extensions
	while ( len && *p && *p!= '/'&&*p!='?'&&*p!='.'&&*p!='-'&&*p!='_') {
		if ( is_alpha_a(*p) ) username = true;
		p++;
	}
	// if we hit this, not a username
	if ( *p=='.' || *p == '-' || *p == '_' ) username = false;
	// did we get a match?
	// . www.cits.ucsb.edu/users/michael-osborne
	// . www.cits.ucsb.edu/users/michael-osborne/
	// . after /blog/ or /~ should be another / or \0, not a period,
	//   because that indicates probably a filename, which is not right,
	//   because we are expecting a username!
	if ( username ) {
		// include the '/'
		if ( *p == '/' ) p++;
		// get length
		*siteLen = p - m_host;
		// not the default
		if ( isDefault ) *isDefault = false;
		// return the site
		return m_host;
	}


	// assume none
	*siteLen = 0;
	// . the default site is the domain
	// . if domain is invalid, site/siteLen will be NULL/0
	if ( ! getDomain() ) return NULL;
	// check the Site Filters table
	CollectionRec *cr = NULL; if ( coll ) cr = g_collectiondb.getRec(coll);
	// the default site is the domain
	char *site;
	if ( defaultToHostname ) { site = m_host  ; *siteLen = m_hlen; }
	else                     { site = m_domain; *siteLen = m_dlen; }
	// g_errno should be set
	if ( ! cr ) return site;

	// initialize the hash table if it needs to be
	if ( cr->m_updateSiteRulesTable ) {
		logf ( LOG_INFO, "db: Updating Site Filters Table" );
		// fill in the hash tables with domain hashes
		cr->m_siteRulesTable.reset();
		cr->m_siteRulesTable.set(cr->m_numSiteExpressions*2);
		for ( int32_t i = 0; i < cr->m_numSiteExpressions ; i++ ) {
			Url f;
			char *u    = cr->m_siteExpressions[i];
			int32_t  ulen = gbstrlen ( u );
			// do not add "www."
			f.set(u,ulen,false);
			// hash the whole hostname (might be just domain)
			int32_t h = hash32 ( f.getHost(), f.getHostLen() );
			// also hash scheme and port
			h = hash32 ( f.getScheme() , f.getSchemeLen() , h );
			h = hash32 ( h , f.getPort() );
			// add to the table
			cr->m_siteRulesTable.addKey(h, i+1);
		}
		// unset the update flag
		cr->m_updateSiteRulesTable = 0;
	}

	// . you can only have on entry per domain or subdomain in the table!
	// . that entry will be a domain or a subdomain
	// . so check for both in the hash table
	int32_t t = 2;
loop:
	t--;
	// return the DEFAULT SITE if no matches
	if ( t < 0 ) return site;
	// check hash table for this domain or subdomain
	int32_t h ;
	if ( t == 1 ) h = hash32 ( getHost  () , getHostLen  () );
	else          h = hash32 ( getDomain() , getDomainLen() );
	// also hash scheme and port
	h = hash32 ( getScheme(), getSchemeLen(), h);
	h = hash32 ( h, getPort());
	// is it in the table?
	int32_t s = cr->m_siteRulesTable.getSlot(h);
	// if not found, try the domain next
	if ( s < 0 ) goto loop;


	// found, grab the index #
	int32_t i = cr->m_siteRulesTable.getValueFromSlot(s) - 1;
	// . see if the url properly matches a filter
	// . do NOT add "www." to the domain/subdomain of the filter url
	char *e = cr->m_siteExpressions[i];
	Url f; f.set (e,gbstrlen(e),false);
	// what is the rule #? if rule is 0, that means the "hostname" rule
	// otherwise this specifies a path depth that defines the site...
	int32_t r = cr->m_siteRules[i];
	// get the full hostname of it
	//char *h    = f.getHost();
	//int32_t  hlen = f.getHostLen();
	// get its hostname (might just be a domain name)
	char *sub    = f.getHost();
	int32_t  subLen = f.getUrlLen() - f.getSchemeLen() - 3;
	// assume we did not match it
	char matched = 0;
	// is the filtered url "f" a "substring" of us?
	if ( strncmp ( getHost  () , sub , subLen ) == 0 ) matched = 1;
	if ( strncmp ( getDomain() , sub , subLen ) == 0 ) matched = 2;
	// must also match scheme and port
	if ( getPort() != f.getPort()                    ) matched = false;
	if ( getSchemeLen() != f.getSchemeLen()          ) matched = false;
	if ( strncmp(getScheme(), f.getScheme(),m_slen)  ) matched = false;
	// if really not a match try again
	if ( ! matched ) goto loop;
	// . we got a match
	// . if r == 0 then the hostname is the site for this rule
	// . so return the hostname as the site
	if ( r == 0 ) {
		// not the default
		if ( isDefault ) *isDefault = false;
		*siteLen = m_hlen;
		return     m_host;
	}
	// . otherwise, it is path depth based
	// . m_path starts off point to "/"
	p = m_path;
	// if empty, no good, no site. return the DEFAULT SITE
	if ( ! p ) return site;
	// do not count the first "/"
	p++;
	// how many /'s to count to?
	int32_t  count ;
	// count them
	for ( count = r ; count > 0 ; count-- ) {
		// inc p
		while ( *p && *p !='/' ) p++;
		// done?
		if ( ! *p ) break;
		// skip passed the '/'
		p++;
	}
	// if count not accomplished, no site. return the DEFAULT SITE.
	if ( count > 0 ) return site;
	// not the default
	if ( isDefault ) *isDefault = false;
	// otherwise we got the site
	if ( matched == 1 ) {
		// use the host, since we matched that
		*siteLen = p - m_host;
		// return it
		return m_host;
	}
	// otherwise, use domain
	*siteLen = p - m_domain;
	return m_domain;
}

int32_t Url::getSiteHash32 ( char *coll ) {
	int32_t siteLen;
	// prefer domain as default, not hostname
	char *site = getSite ( &siteLen , coll , false );
	return hash32 ( site , siteLen );
}
*/

int32_t Url::getHash32WithWWW ( ) {
	uint32_t hh = hash32n ( "www." );
	int32_t conti = 4;
	hh = hash32_cont ( m_domain , m_dlen , hh , &conti );
	return hh;
}

int32_t Url::getHostHash32 ( ) { 
	return hash32 ( m_host , m_hlen ); 
}

int64_t Url::getHostHash64 ( ) { 
	return hash64 ( m_host , m_hlen ); 
}

int32_t Url::getDomainHash32 ( ) { 
	return hash32 ( m_domain , m_dlen ); 
}

int64_t Url::getDomainHash64 ( ) { 
	return hash64 ( m_domain , m_dlen ); 
}

int32_t Url::getUrlHash32 ( ) { 
	return hash32(m_url,m_ulen); 
}

int64_t Url::getUrlHash64 ( ) { 
	return hash64(m_url,m_ulen); 
}

char *getHostFast ( char *url , int32_t *hostLen , int32_t *port ) {
	// point to the url
	char *pp = url;
	// skip http(s):// or ftp:// (always there?)
	while ( *pp && *pp != ':' ) pp++;
	// skip ://
	pp += 3;
	// point "uhost" to hostname right away
	char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' ) {
		// if port ptr given, do not treat port as part of hostname
		if ( port ) *port = atoi(pp+1);
		// i think this was including :1234 as part of hostname
		// if port was NULL!
		//else while ( *pe && *pe != '/' ) pe++;
	}
	// set length
	if ( hostLen ) *hostLen = pe - uhost;
	return uhost;
}

char *getPathFast ( char *url ) {
	// point to the url
	char *pp = url;
	// skip http(s):// or ftp:// (always there?)
	while ( *pp && *pp != ':' ) pp++;
	// skip ://
	pp += 3;
	// point "uhost" to hostname right away
	//char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;
	// but not if something follows the '/'
	return pe;
}

char *getTLDFast ( char *url , int32_t *tldLen , bool hasHttp ) {
	// point to the url
	char *pp = url;
	// only do this for some
	if ( hasHttp ) {
		// skip http(s):// or ftp:// (always there?)
		while ( *pp && *pp != ':' ) pp++;
		// skip ://
		pp += 3;
	}
	// point "uhost" to hostname right away
	char *uhost = pp;
	// advance "pp" till we hit a / or :<port> or \0
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// are we a root? assume so.
	char isRoot = true;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;
	// but not if something follows the '/'
	if ( *pe == '/' && *(pe+1) ) isRoot = false;
	// set length of host
	int32_t uhostLen = pp - uhost;
	// . is the hostname just an IP address?
	// . if it is an ip based url make domain the hostname
	char *ss = uhost;
	bool isIp = true;
	for ( ; *ss && ss<pp ; ss++ )
		if ( is_alpha_a(*ss) ) { isIp = false; break; }
	// if ip, no tld
	if ( isIp ) return NULL;
	// get the tld
	char *tld = ::getTLD ( uhost , uhostLen );
	// if none, done
	if ( ! tld ) return NULL;
	// set length
	if ( tldLen ) *tldLen = pp - tld;
	// return it
	return tld;
}

bool hasSubdomain ( char *url ) {
	// point to the url
	char *pp = url;
	// skip http if there
	if (      pp[0] == 'h' &&
		  pp[1] == 't' &&
		  pp[2] == 't' &&
		  pp[3] == 'p' &&
		  pp[4] == ':' &&
		  pp[5] == '/' &&
		  pp[6] == '/' )
		pp += 7;
	else if ( pp[0] == 'h' &&
		  pp[1] == 't' &&
		  pp[2] == 't' &&
		  pp[3] == 'p' &&
		  pp[4] == 's' &&
		  pp[5] == ':' &&
		  pp[6] == '/' &&
		  pp[7] == '/' )
		pp += 8;
	// point "uhost" to hostname right away
	char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// are we a root? assume so.
	//char isRoot = true;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;
	// but not if something follows the '/'
	//if ( *pe == '/' && *(pe+1) ) isRoot = false;
	// set length
	int32_t uhostLen = pp - uhost;
	// get end
	//char *hostEnd = uhost + uhostLen;
	// . is the hostname just an IP address?
	// . if it is an ip based url make domain the hostname
	char *ss = uhost;
	while ( *ss && !is_alpha_a(*ss) && ss<pp ) ss++;
	// if we are an ip, say yes
	if ( ss == pp ) return true;
	// get the tld
	char *utld = ::getTLD ( uhost , uhostLen );
	// no tld, then no domain
	if ( ! utld ) return false;
	// the domain, can only be gotten once we know the TLD
	// back up a couple chars
	char *udom = utld - 2;
	// backup until we hit a '.' or hit the beginning
	while ( udom > uhost && *udom != '.' ) udom--;
	// fix http://ok/
	if ( udom < uhost || *udom =='/' ) return false;
	// if we hit '.' advance 1
	if ( *udom == '.' ) udom++;
	// eqal to host? if not, we do have a subdomain
	if ( udom != uhost ) return true;
	// otherwise the hostname equals the domain name
	return false;
}

// returns NULL if url was in bad format and could not get domain. this
// was happening when a host gave us a bad redir url and xmldoc tried
// to set extra doc's robot.txt url to it "http://2010/robots.txt" where
// the host said "Location: 2010 ...".
char *getDomFast ( char *url , int32_t *domLen , bool hasHttp ) {
	// point to the url
	char *pp = url;
	// skip http if there
	if ( hasHttp ) {
		// skip http(s):// or ftp:// (always there?)
		while ( *pp && *pp != ':' ) pp++;
		// skip ://
		pp += 3;
	}
	// point "uhost" to hostname right away
	char *uhost = pp;
	// advance "pp" till we hit a / or :<port>
	while ( *pp && *pp !='/' && *pp !=':' ) pp++;
	// are we a root? assume so.
	char isRoot = true;
	// advance "pe" over the port
	char *pe = pp;
	if ( *pp == ':' )
		while ( *pe && *pe != '/' ) pe++;
	// but not if something follows the '/'
	if ( *pe == '/' && *(pe+1) ) isRoot = false;
	// set length
	int32_t uhostLen = pp - uhost;
	// get end
	char *hostEnd = uhost + uhostLen;
	// . is the hostname just an IP address?
	// . if it is an ip based url make domain the hostname
	char *ss = uhost;
	while ( *ss && !is_alpha_a(*ss) && ss<pp ) ss++;
	//bool isIp = false;
	//if ( ss == pp ) isIp = true;
	// if we are an ip, treat special
	if ( ss == pp ) {
		// . might just be empty! like "\0"
		// . fixes core dump from 
		//   http://www.marcom1.unimelb.edu.au/public/contact.html
		//   parsing host email address
		if ( uhostLen == 0 ) return NULL;
		// to be consistent with how Url::m_domain/m_dlen is set we 
		// need to remove the last .X from the ip address
		// skip back over digits
		for ( hostEnd-- ; is_digit(*hostEnd); hostEnd-- );
		// must be a period
		if ( *hostEnd != '.' ) { 
			log("url: getDomFast() could not find period for "
			    "hostname in url");
			return NULL;
		}
		// set length
		*domLen = hostEnd - uhost;
		// that's it
		return uhost;
	}
	// get the tld
	char *utld = ::getTLD ( uhost , uhostLen );
	// no tld, then no domain
	if ( ! utld ) return NULL;
	// the domain, can only be gotten once we know the TLD
	// set utldLen
	//int32_t utldLen = hostEnd - utld;
	// back up a couple chars
	char *udom = utld - 2;
	// backup until we hit a '.' or hit the beginning
	while ( udom > uhost && *udom != '.' ) udom--;
	// fix http://ok/
	if ( udom < uhost || *udom =='/' ) return NULL;
	// if we hit '.' advance 1
	if ( *udom == '.' ) udom++;
	// set domain length
	*domLen = hostEnd - udom;
	return udom;
}

// is it of a permalink form?
bool isPermalinky ( char *u ) {
	// get its path
	char *path = getPathFast(u);
	// our ptr
	char *p = path;
	// we must have a sequence of 3 or more digits in the path
	int32_t  dcount = 0;
	// start scanning at the path
	for ( ; *p && *p !='?'  ; p++ ) {
		// if not a digit, reset count
		if ( ! is_digit(*p) ) { dcount = 0; continue; }
		// count it if a digit
		if ( ++dcount == 3 ) break;
	}
	// it can also have 2+ hyphens or 2+ underscores in a single
	// path component to be a permalink
	int32_t hcount = 0;
	p = path;
	for ( ; *p && *p !='?'  ; p++ ) {
		// if not a digit, reset count
		if ( *p == '/' ) { hcount = 0; continue; }
		// is it a thing?
		if ( *p != '_' && *p != '-' ) continue;
		// count it
		if ( ++hcount == 2 ) break;
	}
	// must be "permalinky"
	if ( hcount < 2 && dcount < 3 ) return false;
	// we are
	return true;
}

/*
bool Url::isRSSFormat ( ) {
	// if it ends in .rss, .xml or .rdf ASSUME rss
	bool isRSS = false;
	char *e    = getExtension();
	int32_t  elen = getExtensionLen();
	if ( elen == 3 && strcmp(e,"rss")==0 ) isRSS = true;
	if ( elen == 3 && strcmp(e,"xml")==0 ) isRSS = true;
	if ( elen == 3 && strcmp(e,"rdf")==0 ) isRSS = true;
	// . but if it has wlwmanifest, then not!
	// . i don't know what those are, but they are not rss feeds
	if ( strstr ( getPath(), "wlwmanifest" ) ) isRSS = false;
	// same goes for foaf
	if ( strstr ( getPath(), "foaf" ) ) isRSS = false;
	return isRSS;
}
*/

// is it http://rpc.weblogs.com/int16_tChanges.xml, etc.?
bool Url::isPingServer ( ) {
	if ( strcmp ( m_url , "http://rpc.weblogs.com/int16_tChanges.xml") == 0 )
		return true;
	// testing page
	if ( strcmp ( m_url , "http://127.0.0.1:8000/int16_tChanges.xml") == 0 )
		return true;
	// default
	return false;
}

bool isPingServer ( char *s ) {
	if ( strstr ( s , "rpc.weblogs.com/int16_tChanges.xml") )
		return true;
	// testing page
	if ( strstr ( s , "127.0.0.1:8000/int16_tChanges.xml") )
		return true;
	// default
	return false;
}

// "s" point to the start of a normalized url (includes http://, etc.)
char *getHost ( char *s , int32_t *hostLen ) {
	// skip proto
	while ( *s != ':' ) s++;
	// skip ://
	s += 3;
	// that is the host
	char *host = s;
	// get length of hostname
	for ( s++; *s && *s != '/' ; s++ );
	// that is it
	*hostLen = s - host;
	// return it
	return host;
}

char *getFilenameFast ( char *s , int32_t *filenameLen ) {
	// skip proto
	while ( *s != ':' ) s++;
	// skip ://
	s += 3;
	// get length of hostname
	for ( s++; *s && *s != '/' ; s++ );
	// should always have a /
	if ( *s != '/' ) { char *xx=NULL;*xx=0;}
	// skip that
	s++;
	// this point to the filename
	char *filename ;
	// loop over every subdir name in the path
 subloop:
	// assume that is filename
	filename = s;
	// advance s
	for ( ; *s && *s !='/' && *s != '?' && *s !='#' ; s++ );
	// if we hit another '/' re-set filename
	if ( *s == '/' ) { s++; goto subloop; }
	// set end of it
	char *filenameEnd = s;
	// set length
	*filenameLen = filenameEnd - filename;
	// if none, return null
	if ( *filenameLen == 0 ) return NULL;
	// return it
	return filename;
}
	


// . return ptrs to the end
// . the character it points to SHOULD NOT BE part of the site
char *getPathEnd ( char *s , int32_t desiredDepth ) {
	// skip proto
	while ( *s != ':' ) s++;
	// skip ://
	s += 3;
	// get length of hostname
	for ( s++; *s && *s != '/' ; s++ );
	// should always have a /
	if ( *s != '/' ) { char *xx=NULL;*xx=0;}
	// skip that
	s++;
	// init depth
	int32_t depth = 0;
	// do a character loop
	for ( ; depth <= desiredDepth && *s ; s++ ) 
		// count the '/'
		if ( *s == '/' ) depth++;
	// return the end
	return s;
	/*
	// save for below
	int32_t saved = depth;
	// keep going
	while ( depth-- > 0 ) {
		for ( s++; *s && *s != '/' && *s != '?' ; s++ );
		// if not enough path components (or cgi), return NULL
		if ( *s != '/' ) return NULL;
	}
	// include the last '/' if we have path components
	if ( saved > 0 ) s++;
	// . we got it
	// . if depth==0 just use "www.xyz.com" as site
	// . if depth==1 just use "www.xyz.com/foo/" as site
	return s;
	*/
}

// . pathDepth==0 for "www.xyz.com"
// . pathDepth==0 for "www.xyz.com/"
// . pathDepth==0 for "www.xyz.com/foo"
// . pathDepth==1 for "www.xyz.com/foo/"
// . pathDepth==1 for "www.xyz.com/foo/x"
// . pathDepth==2 for "www.xyz.com/foo/x/"
// . pathDepth==2 for "www.xyz.com/foo/x/y"
int32_t getPathDepth ( char *s , bool hasHttp ) {
	// skip http:// if we got it
	if ( hasHttp ) {
		// skip proto
		while ( *s != ':' ) s++;
		// must have it!
		if ( ! *s ) { char *xx=NULL;*xx=0; }
		// skip ://
		s += 3;
	}
	// skip over hostname
	for ( s++; *s && *s != '/' ; s++ );
	// no, might be a site like "xyz.com"
	if ( ! *s ) return 0;
	// should always have a /
	if ( *s != '/' ) { char *xx=NULL;*xx=0;}
	// skip that
	s++;
	// init depth
	int32_t depth = 0;
	// do a character loop
	for ( ; *s ; s++ ) {
		// stop if we hit ? or #
		if ( *s == '?' ) break;
		if ( *s == '#' ) break;
		// count the '/'
		if ( *s == '/' ) depth++;
	}
	return depth;
}

// must be like xyz.com/xxxx/yyyy/[a-z]*.htm format
bool isHijackerFormat ( char *url ) {

	if ( ! url ) return false;

	// get the path
	char *p = getPathFast ( url );

	if ( strstr(p,"/docs.php?id=") ) return true;
	if ( strstr(p,"/mods.php?id=") ) return true;
	if ( strstr(p,"/show.php?p=") ) return true;

	// count the /'s
	int32_t pc = 0;

	for ( ; *p=='-' || *p=='_' || *p=='/' || (*p>='a'&&*p<='z') || 
		      is_digit(*p); p++) 
		if ( *p == '/' ) pc++;

	// too many /'s? 
	if ( pc >= 5 ) return false;
	// too few. need at least 3
	if ( pc <= 2 ) return false;

	// need a .htm
	if ( *p != '.' ) return false;

	// skip .
	p++;
	// need "htm\0" now
	if ( p[0] != 'h' ) return false;
	if ( p[1] != 't' ) return false;
	if ( p[2] != 'm' ) return false;
	if ( p[3] != 0   ) return false;
	return true;
}

bool Url::hasMediaExtension ( ) {

	if ( ! m_extension || ! m_elen ) return false;

	char *ext = m_extension;

	if ( to_lower_a(ext[0]) == 'c' &&
	     to_lower_a(ext[1]) == 's' &&
	     to_lower_a(ext[2]) == 's' )
		return true;

	if ( to_lower_a(ext[0]) == 'm' &&
	     to_lower_a(ext[1]) == 'p' &&
	     to_lower_a(ext[2]) == 'g' )
		return true;

	if ( to_lower_a(ext[0]) == 'p' &&
	     to_lower_a(ext[1]) == 'n' &&
	     to_lower_a(ext[2]) == 'g' )
		return true;

	if ( to_lower_a(ext[0]) == 'w' &&
	     to_lower_a(ext[1]) == 'm' &&
	     to_lower_a(ext[2]) == 'v' )
		return true;

	if ( to_lower_a(ext[0]) == 'w' &&
	     to_lower_a(ext[1]) == 'a' &&
	     to_lower_a(ext[2]) == 'v' )
		return true;

	if ( to_lower_a(ext[0]) == 'j' &&
	     to_lower_a(ext[1]) == 'p' &&
	     to_lower_a(ext[2]) == 'g' )
		return true;

	if ( to_lower_a(ext[0]) == 'g' &&
	     to_lower_a(ext[1]) == 'i' &&
	     to_lower_a(ext[2]) == 'f' )
		return true;

	if ( to_lower_a(ext[0]) == 'i' &&
	     to_lower_a(ext[1]) == 'c' &&
	     to_lower_a(ext[2]) == 'o' )
		return true;

	if ( to_lower_a(ext[0]) == 'm' &&
	     to_lower_a(ext[1]) == 'p' &&
	     to_lower_a(ext[2]) == '3' )
		return true;

	if ( to_lower_a(ext[0]) == 'm' &&
	     to_lower_a(ext[1]) == 'p' &&
	     to_lower_a(ext[2]) == '4' )
		return true;

	if ( to_lower_a(ext[0]) == 'm' &&
	     to_lower_a(ext[1]) == 'o' &&
	     to_lower_a(ext[2]) == 'v' )
		return true;

	if ( to_lower_a(ext[0]) == 'a' &&
	     to_lower_a(ext[1]) == 'v' &&
	     to_lower_a(ext[2]) == 'i' )
		return true;

	if ( to_lower_a(ext[0]) == 'm' &&
	     to_lower_a(ext[1]) == 'p' &&
	     to_lower_a(ext[2]) == 'e' &&
	     to_lower_a(ext[3]) == 'g' )
		return true;

	if ( to_lower_a(ext[0]) == 'j' &&
	     to_lower_a(ext[1]) == 'p' &&
	     to_lower_a(ext[2]) == 'e' &&
	     to_lower_a(ext[3]) == 'g' )
		return true;

	return false;
}

uint32_t Url::unitTests() {
	char* urls[] = {
		"http://сацминэнерго.рф/robots.txt",
		"http://www.fas.org/blog/ssp/2009/08/securing-venezuela\032s-arsenals.php",
		"http://topbeskæring.dk/velkommen",
		"www.Alliancefrançaise.nu",
		"française.Alliance.nu",
		"française.Alliance.nu/asdf",
		"http://française.Alliance.nu/asdf",
		"http://française.Alliance.nu/",
		"幸运.龍.com",
		"幸运.龍.com/asdf/运/abc",
		"幸运.龍.com/asdf",
		"http://幸运.龍.com/asdf",
		"http://Беларуская.org/Акадэмічная",
		"https://hi.Български.com",
		"https://fakedomain.中文.org/asdf",
		"https://gigablast.com/abc/文/efg",
		"https://gigablast.com/?q=文",
		"http://www.example.сайт",
		"http://genocidearchiverwanda.org.rw/index.php/Category:Official_Communiqués",
		"http://www.example.com/xn--fooled-you-into-trying-to-decode-this",
		"http://www.example.сайт/xn--fooled-you-into-trying-to-decode-this",
		"http://腕時計通販.jp/",
		// Lets check some bad urls too:
		"https://pypi.python\n\n\t\t\t\t.org/packages/source/p/pyramid/pyramid-1.5.tar.gz#md5=8747658dcbab709a9c491e43d3b0d58b"
	};

	StackBuf(sb);
	uint32_t len = sizeof(urls) / sizeof(char*);
	for(uint32_t i = 0; i < len; i++) {
		Url u;
		u.set(urls[i], strlen(urls[i]));
		log("build:%s normalized to %s, printed to %s ", 
		    urls[i], u.getUrl(), Url::getDisplayUrl(u.getUrl(), &sb));
		sb.reset();
	}
	//FIXME: need to return an error if there is a problem
	return 0;
}


char* Url::getDisplayUrl(char* url, SafeBuf* sb) {
	char* found;
	char* labelCursor = url;
	if((found = strstr(labelCursor, "xn--"))) {
		sb->safeMemcpy(url, found - url);

		char* p = url;
		char* pend = url + gbstrlen(url);
		if(strncmp(p, "http://", 7) == 0) p += 7;
		else if(strncmp(p, "https://", 8) == 0) p += 8;

		while(p < pend && *p != '/') p++;
		char* domEnd = p;

		do {
			if(found > domEnd) {
				// Dont even look if it is past the domain
				break;
			}

			char* encodedStart = found + 4;
			uint32_t decoded [ MAX_URL_LEN];
			size_t decodedLen = MAX_URL_LEN - 1 ;
			char* labelEnd = encodedStart;
			while( labelEnd < domEnd && *labelEnd != '/' &&  *labelEnd != '.' ) 
				labelEnd++;

			punycode_status status = punycode_decode(labelEnd - encodedStart,
													 encodedStart, 
													 &decodedLen, 
													 decoded, NULL);
			if(status != 0) {
				log("build: Bad Engineer, failed to depunycode international url %s", url);
				sb->safePrintf("%s", url);
				return url;
			}
			sb->utf32Encode(decoded, decodedLen);
			if(*labelEnd == '.') sb->pushChar(*labelEnd++);
			labelCursor = labelEnd;
		} while((found = strstr(labelCursor, "xn--")));
	}
    // Copy in the rest
    sb->safePrintf("%s", labelCursor);
    sb->nullTerm();
    return sb->getBufStart();
}
