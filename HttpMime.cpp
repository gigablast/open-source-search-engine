// -*- c-basic-offset:8 tab-width:8 -*-

#include "gb-include.h"

#include "HttpMime.h"
#include "HashTable.h"

// . convert these values to strings
// . these must be 1-1 with the #define's in HttpMime.h
char *g_contentTypeStrings [] = {
	""     ,
	"html" ,
	"text" ,
	"xml"  ,
	"pdf"  ,
	"doc"  ,
	"xls"  ,
        "ppt"  ,
        "ps"   , // 8
	"gif"  , // 9
	"jpg"  , // 10
	"png"  , // 11
	"tiff" , // 12
	"bmp"  , // 13
	"javascript" , // 14
	"css"  , // 15
	"json" ,  // 16
	"image", // 17
	"spiderstatus" // 18
};

HttpMime::HttpMime () { reset(); }

void HttpMime::reset ( ) {
	m_mimeStartPtr     = NULL;
	m_firstCookie      = NULL;
	m_status           = -1;
	m_contentLen       = -1;
	m_lastModifiedDate =  0;
	m_contentType      =  CT_HTML;
	m_lastModifiedDate =  0;
	m_charset          =  NULL;
	m_charsetLen       =  0;
	m_cookie           =  NULL;
	m_cookieLen        =  0;
	m_locationField    = NULL;
	m_locationFieldLen = 0;
	m_contentEncodingPos = NULL;
	m_contentLengthPos = NULL;
	m_contentTypePos   = NULL;
}

// . returns false if could not get a valid mime
// . we need the url in case there's a Location: mime that's base-relative
bool HttpMime::set ( char *buf , int32_t bufLen , Url *url ) {
	// reset some stuff
	m_mimeStartPtr     = NULL;
	m_firstCookie      = NULL;
	m_contentLen       = -1;
	m_content          = NULL;
	m_bufLen           =  0;
	m_contentType      =  CT_HTML;
	m_contentEncoding  =  ET_IDENTITY;
	m_lastModifiedDate =  0;
	m_charset          =  NULL;
	m_charsetLen       =  0;
	// at the very least we should have a "HTTP/x.x 404\[nc]"
	if ( bufLen < 13 ) { m_boundaryLen = 0; return false; }
	// . get the length of the Mime, must end in \r\n\r\n , ...
	// . m_bufLen is used as the mime length
	m_mimeStartPtr = buf;
	m_bufLen = getMimeLen ( buf , bufLen , &m_boundaryLen );
	// . return false if we had no mime boundary
	// . but set m_bufLen to 0 so getMimeLen() will return 0 instead of -1
	//   thus avoiding a potential buffer overflow
	if ( m_bufLen < 0 ) { 
		m_bufLen = 0; 
		m_boundaryLen = 0; 
		log("mime: no rnrn boundary detected");
		return false; 
	}
	// set this
	m_content = buf + m_bufLen;
	// . parse out m_status, m_contentLen, m_lastModifiedData, contentType
	// . returns false on bad mime
	return parse ( buf , m_bufLen , url );
}

// . returns -1 if no boundary found
int32_t HttpMime::getMimeLen ( char *buf , int32_t bufLen , int32_t *bsize ) {
	// size of the boundary
	*bsize = 0;
	// find the boundary
	int32_t i;
	for ( i = 0 ; i < bufLen ; i++ ) {
		// continue until we hit a \r or \n
		if ( buf[i] != '\r' && buf[i] != '\n' ) continue;
		// boundary check
		if ( i + 1 >= bufLen ) continue;
		// prepare for a smaller mime size
		*bsize = 1;
		// \r\r
		if ( buf[i  ] == '\r' && buf[i+1] == '\r' ) break;
		// \n\n
		if ( buf[i  ] == '\n' && buf[i+1] == '\n' ) break;
		// boundary check
		if ( i + 3 >= bufLen ) continue;
		// prepare for a larger mime size
		*bsize = 2;
		// \r\n\r\n
		if ( buf[i  ] == '\r' && buf[i+1] == '\n' &&
		     buf[i+2] == '\r' && buf[i+3] == '\n'  ) break;
		// \n\r\n\r
		if ( buf[i  ] == '\n' && buf[i+1] == '\r' &&
		     buf[i+2] == '\n' && buf[i+3] == '\r'  ) break;
	}
	// return false if could not find the end of the MIME
	if ( i == bufLen ) return -1;
	return i + *bsize * 2;
}

// returns false on bad mime
bool HttpMime::parse ( char *mime , int32_t mimeLen , Url *url ) {
	// reset locUrl to 0
	m_locUrl.reset();
	// return if we have no valid complete mime
	if ( mimeLen == 0 ) return false;
	// status is on first line
	m_status = -1;
	// skip HTTP/x.x till we hit a space
	char *p = mime;
	char *pend = mime + mimeLen;
	while ( p < pend && !is_wspace_a(*p) ) p++;
	// then skip over spaces
	while ( p < pend &&  is_wspace_a(*p) ) p++;
	// return false on a problem
	if ( p == pend ) return false;
	// then read in the http status
	m_status = atol2 ( p , pend - p );
	// if no Content-Type: mime field was provided, assume html
	m_contentType = CT_HTML;
	// assume default charset
	m_charset    = NULL;
	m_charsetLen = 0;
	// set contentLen, lastModifiedDate, m_cookie
	p = mime;
	while ( p < pend ) {
		// compute the length of the string starting at p and ending
		// at a \n or \r
		int32_t len = 0;
		while ( &p[len] < pend && p[len]!='\n' && p[len]!='\r' ) len++;
		// . if we could not find a \n or \r there was an error
		// . MIMEs must always end in \n or \r
		if ( &p[len] >= pend ) return false;
		// . stick a NULL at the end of the line 
		// . overwrites \n or \r TEMPORARILY
		char c = p [ len ];
		p [ len ] = '\0';
		// parse out some meaningful data
		if      ( strncasecmp ( p , "Content-Length:" ,15) == 0 ) {
			m_contentLengthPos = p + 15;
			m_contentLen = atol( m_contentLengthPos);
		}
		else if ( strncasecmp ( p , "Last-Modified:"  ,14) == 0 ) {
			m_lastModifiedDate=atotime(p+14);
			// do not let them exceed current time for purposes
			// of sorting by date using datedb (see Msg16.cpp)
			time_t now = time(NULL);
			if (m_lastModifiedDate > now) m_lastModifiedDate = now;
		}
		else if ( strncasecmp ( p , "Content-Type:"   ,13) == 0 ) {
			m_contentType = getContentTypePrivate ( p + 13 );
			char *s = p + 13;
			while ( *s == ' ' || *s == '\t' ) s++;
			m_contentTypePos = s;
		}
		else if ( strncasecmp ( p , "Set-Cookie:"   ,10) == 0 ) {
			if ( ! m_firstCookie ) m_firstCookie = p;
			m_cookie = p + 11;
			if ( m_cookie[0] == ' ' ) m_cookie++;
			m_cookieLen = gbstrlen ( m_cookie );
		}
		else if ( strncasecmp ( p , "Location:"       , 9) == 0 ) {
			// point to it
			char *tt = p + 9;
			// skip if space
			if ( *tt == ' ' ) tt++;
			if ( *tt == ' ' ) tt++;
			// at least set this for Msg13.cpp to use
			m_locationField    = tt;
			m_locationFieldLen = gbstrlen(tt);
			// . we don't add the "www." because of slashdot.com
			// . we skip initial spaces in this Url::set() routine
			if(url)
				m_locUrl.set ( url, p + 9, len - 9,
					       false/*addWWW?*/);
		}
		else if ( strncasecmp ( p , "Content-Encoding:", 17) == 0 ) {
			//only support gzip now, it doesn't seem like servers
			//implement the other types much
			m_contentEncodingPos = p+17;
			if(strstr(m_contentEncodingPos, "gzip")) {
				m_contentEncoding = ET_GZIP;
			}
			else if(strstr(m_contentEncodingPos, "deflate")) {
				//zlib's compression
				m_contentEncoding = ET_DEFLATE;
			}
		}
		//else if ( strncasecmp ( p, "Cookie:", 7) == 0 )
		//	log (LOG_INFO, "mime: Got Cookie = %s", (p+7));
		// re-insert the character that we replaced with a '\0'
		p [ len ] = c;
		// go to next line
		p += len;
		// skip over the cruft at the end of this line
		while ( p < pend && ( *p=='\r' || *p=='\n' ) ) p++;
	}
	return true;
}				

// . s must be null terminated
// . http://wgc.chem.pu.ru/educate/rfc/rfc1945/part4.htm#3.3 has date formats
// . #1: Sun, 06 Nov 1994 08:49:37 GMT  ;RFC 822, updated by RFC 1123
// . #2: Sunday, 06-Nov-94 08:49:37 GMT ;RFC 850,obsoleted by RFC1036
// . #3: Sun Nov  6 08:49:37 1994       ;ANSI C's asctime() format
// . #4: 06 Nov 1994 08:49:37 GMT  ... my own
// . #5: 2007-12-31
// . #6: 2008-04-30T20:48:25Z (ISO8601)

time_t atotime ( char *s ) {

	// skip non-alnum padding
	while ( *s && ! isalnum (*s) ) s++;

	// if first char is a num, it's type #4
	if ( is_digit(*s) ) {
		int32_t num = atol(s);
		// 2007-12-31
		if ( num > 1900 ) return atotime5 ( s );
		return atotime4 ( s );
	}

	// . determine if we have type #1, #2 or #3 date format
	// . now if there's hyphens we have type #2
	char *t = s;
	while ( *t && *t!='-') t++;
	if ( *t == '-' ) return atotime2 ( s );	

	// now if there's a comma we have type 1
	t = s;
	while ( *t && *t!=',') t++;
	if ( *t == ',' ) return atotime1 ( s );

	// otherwise, must be type 3
	return atotime3 ( s );
}

#include "Dates.h" // for getTimeZone()

// #1: Sun, 06 Nov 1994 08:49:37 GMT  ;RFC 822, updated by RFC 1123
time_t atotime1 ( char *s ) {
	// this time structure, once filled, will help yield a time_t
	struct tm t;
	// DAY OF WEEK 
	t.tm_wday = getWeekday ( s );
	while ( *s && ! isdigit(*s) ) s++;
	// DAY OF MONTH
	t.tm_mday = atol ( s );
	while ( *s && ! isalpha (*s) ) s++;
	// MONTH
	t.tm_mon = getMonth ( s );
	while ( *s && ! isdigit (*s) ) s++;
	// YEAR
	t.tm_year = atol ( s ) - 1900 ; // # of years since 1900
	while ( isdigit (*s) ) s++;
	while ( isspace (*s) ) s++;	
	// TIME
	getTime ( s , &t.tm_sec , &t.tm_min , &t.tm_hour );
	// unknown if we're in  daylight savings time
	t.tm_isdst = -1;

	// translate using mktime
	time_t global = timegm ( &t );

	// skip HH:MM:SS
	while ( *s && ! isspace (*s) ) s++;	

	// no timezone following??? fix core.
	if ( ! *s ) return global;

	// skip spaces
	while ( isspace (*s) ) s++;
	// convert local time to "utc" or whatever timezone "s" points to,
	// which is usually gmt or utc
	int32_t tzoff = getTimeZone ( s ) ;
	if ( tzoff != BADTIMEZONE ) global += tzoff;
	return global;

	// now, convert to utc
	//time_t utc  = time(NULL);
	// get time here locally
	//time_t here = localtime(&utc);
	// what is the diff?
	//int32_t delta = here - utc;
	// modify our time to make it into utc
	//return local - delta;
}

// #2: Sunday, 06-Nov-94 08:49:37 GMT ;RFC 850,obsoleted by RFC1036
time_t atotime2 ( char *s ) {
	// this time structure, once filled, will help yield a time_t
	struct tm t;
	// DAY OF WEEK 
	t.tm_wday = getWeekday ( s ); // need getLongWeekday()?
	while ( *s && ! isdigit ( *s ) ) s++;
	// DAY OF MONTH
	t.tm_mday = atol ( s );
	while ( *s && ! isalpha (*s) ) s++;
	// MONTH
	t.tm_mon = getMonth ( s );
	while ( *s && ! isdigit (*s) ) s++;
	// YEAR
	t.tm_year = atol ( s ) ;  // # of years since 1900
	while ( isdigit (*s) ) s++;
	while ( isspace (*s) ) s++;	
	// TIME
	getTime ( s , &t.tm_sec , &t.tm_min , &t.tm_hour );
	// unknown if we're in  daylight savings time
	t.tm_isdst = -1;
	// translate using mktime
	time_t global = timegm ( &t );

	// skip HH:MM:SS
	while ( ! isspace (*s) ) s++;	
	// skip spaces
	while ( isspace (*s) ) s++;
	// convert local time to "utc" or whatever timezone "s" points to,
	// which is usually gmt or utc
	int32_t tzoff = getTimeZone ( s ) ;
	if ( tzoff != BADTIMEZONE ) global += tzoff;
	return global;
}

// #3: Sun Nov  6 08:49:37 1994       ;ANSI C's asctime() format
time_t atotime3 ( char *s ) {
	// this time structure, once filled, will help yield a time_t
	struct tm t;
	// DAY OF WEEK 
	t.tm_wday = getWeekday ( s ); // need getLongWeekday()?
	while ( isalpha(*s) ) s++;
	while ( isspace(*s) ) s++;
	// MONTH
	t.tm_mon = getMonth ( s );
	while ( *s && ! isdigit (*s) ) s++;
	// DAY OF MONTH
	t.tm_mday = atol ( s );
	while ( *s && ! isalpha (*s) ) s++;
	// TIME
	getTime ( s , &t.tm_sec , &t.tm_min , &t.tm_hour );
	while ( *s && ! isspace (*s) ) s++;	
	while ( isspace (*s) ) s++;	
	// YEAR
	t.tm_year = atol ( s ) - 1900 ; // # of years since 1900
	// unknown if we're in  daylight savings time
	t.tm_isdst = -1;
	// translate using mktime
	time_t tt = timegm ( &t );
	return tt;
}

// . #4: 06 Nov 1994 08:49:37 GMT  ;RFC 822, updated by RFC 1123
// . like atotime1()
time_t atotime4 ( char *s ) {
	// this time structure, once filled, will help yield a time_t
	struct tm t;
	// DAY OF WEEK 
	//t.tm_wday = getWeekday ( s );
	//while ( *s && ! isdigit(*s) ) s++;
	// DAY OF MONTH
	t.tm_mday = atol ( s );
	while ( *s && ! isalpha (*s) ) s++;
	// MONTH
	t.tm_mon = getMonth ( s );
	while ( *s && ! isdigit (*s) ) s++;
	// YEAR
	t.tm_year = atol ( s ) - 1900 ; // # of years since 1900
	while ( isdigit (*s) ) s++;
	while ( isspace (*s) ) s++;	
	// TIME
	getTime ( s , &t.tm_sec , &t.tm_min , &t.tm_hour );
	// unknown if we're in  daylight savings time
	t.tm_isdst = -1;
	// translate using mktime
	time_t global = timegm ( &t );

	// skip HH:MM:SS
	while ( ! isspace (*s) ) s++;	
	// skip spaces
	while ( isspace (*s) ) s++;
	// convert local time to "utc" or whatever timezone "s" points to,
	// which is usually gmt or utc
	int32_t tzoff = getTimeZone ( s ) ;
	if ( tzoff != BADTIMEZONE ) global += tzoff;
	return global;
}

// 2007-12-31
// 2008-04-30T20:48:25Z (ISO8601)
time_t atotime5 ( char *s ) {
	// this time structure, once filled, will help yield a time_t
	struct tm t;
	// YEAR
	int32_t y = atol ( s ) ;
	// must be > 1900
	if ( y < 1900 ) return -1;
	if ( y > 2100 ) return -1;
	t.tm_year = y - 1900 ; // # of years since 1900
	// skip year
	while ( *s && isdigit (*s) ) s++;
	// skip the hyphen or space
	if ( *s != '-' && *s !='/' && *s !=' ' ) return -1;
	s++;
	// must be a digit
	if ( ! is_digit(*s) ) return -1;

	// month
	t.tm_mon = atol(s) - 1;
	// skip month
	while ( *s && isdigit (*s) ) s++;
	// skip the hyphen or space
	if ( *s != '-' && *s !='/' && *s !=' ' ) return -1;
	s++;
	// must be a digit
	if ( ! is_digit(*s) ) return -1;

	// day of week
	t.tm_mday = atol ( s );
	while ( isdigit (*s) ) s++;
	while ( isspace (*s) ) s++;	
        if (*s == 'T')         s++;

	// TIME
	getTime ( s , &t.tm_sec , &t.tm_min , &t.tm_hour );
	// unknown if we're in  daylight savings time
	t.tm_isdst = -1;
	// translate using mktime
	return timegm ( &t );
}


// sunday=0, monday=1, tuesday=2, wednesday=3, thursday=4, friday=5, saturday=6
// sun=0, mon=1, tue=2, wed=3, thu=4, fri=5, sat=6
int32_t getWeekday ( char *s ) {

	char a = tolower(s[0]);
	char b = tolower(s[1]);

	switch ( a ) {
	case 's': 
		if ( b=='u' ) return 0; // sun
		return 6;                  // sat
	case 'm': 
		return 1;                  // mon
	case 't':
		if ( b=='u' ) return 2; // tue
		return 4;                  // thu
	case 'w':
		return 3;                  // wed
	case 'f':
		return 5;                  // fri
	}
	//  bad week day, return sunday
	return 0;
}

int32_t getMonth ( char *s ) {

	char a = tolower(s[0]);
	char b = tolower(s[1]);
	char c = tolower(s[2]);

	switch ( a ) {
	case 'j':
		if ( b == 'a' ) return 0; // january
		if ( c == 'n' ) return 5; // june
		if ( c == 'l' ) return 6; // july
	case 'm': 
		if ( c == 'r' ) return 2; // march
		if ( c == 'y' ) return 4; // may
	case 'a':
		if ( b == 'p' ) return 3; // april
		if ( b == 'u' ) return 7; // august
	case 'f': return  1; // feburary
	case 's': return  8; // september
	case 'o': return  9; // october
	case 'n': return 10; // november
	case 'd': return 11; // december
	}
	// default
	return 0;
}

// . s = "xx:xx:xx"
void getTime ( char *s , int *sec , int *min , int *hour ) {
	*hour = atol ( s );	
	while ( isdigit ( *s ) ) s++;  if ( *s == ':' ) s++;
	*min  = atol ( s );
	while ( isdigit ( *s ) ) s++;  if ( *s == ':' ) s++;
	*sec  = atol ( s );
}

int32_t getContentTypeFromStr ( char *s ) {

	int32_t slen = gbstrlen(s);

	// trim off spaces at the end
	char tmp[64];
	if ( s[slen-1] == ' ' ) {
		strncpy(tmp,s,63);
		tmp[63] = '\0';
		int32_t newLen = gbstrlen(tmp);
		s = tmp;
		char *send = tmp + newLen;
		for ( ; send>s && send[-1] == ' '; send-- );
		*send = '\0';
	}

	
	// -1 means unknown
	//int32_t ct = -1;
	int32_t ct = CT_UNKNOWN;
	// html
	if      (!strcasecmp(s,"text/html"               ) ) ct = CT_HTML;
	else if (!strcasecmp(s,"text/plain"              ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"text/xml"                ) ) ct = CT_XML;
	else if (!strcasecmp(s,"text/txt"                ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"text"                    ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"text"                    ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"txt"                     ) ) ct = CT_TEXT;
	else if (!strcasecmp(s,"application/xml"         ) ) ct = CT_XML;
	// we were not able to spider links on an xhtml doc because
	// this was set to CT_XML, so try CT_HTML
	else if (!strcasecmp(s,"application/xhtml+xml"   ) ) ct = CT_HTML;
	else if (!strcasecmp(s,"application/rss+xml"     ) ) ct = CT_XML;
	else if (!strcasecmp(s,"rss"                     ) ) ct = CT_XML;
	else if (!strcasecmp(s,"application/rdf+xml"     ) ) ct = CT_XML;
	else if (!strcasecmp(s,"application/atom+xml"    ) ) ct = CT_XML;
	else if (!strcasecmp(s,"atom+xml"                ) ) ct = CT_XML;
	else if (!strcasecmp(s,"application/pdf"         ) ) ct = CT_PDF;
	else if (!strcasecmp(s,"application/msword"      ) ) ct = CT_DOC;
	else if (!strcasecmp(s,"application/vnd.ms-excel") ) ct = CT_XLS;
	else if (!strcasecmp(s,"application/vnd.ms-powerpoint")) ct = CT_PPT;
	else if (!strcasecmp(s,"application/mspowerpoint") ) ct = CT_PPT;
	else if (!strcasecmp(s,"application/postscript"  ) ) ct = CT_PS;
	else if (!strcasecmp(s,"application/warc"        ) ) ct = CT_WARC;
	else if (!strcasecmp(s,"application/arc"         ) ) ct = CT_ARC;
        else if (!strcasecmp(s,"image/gif"               ) ) ct = CT_GIF;
        else if (!strcasecmp(s,"image/jpeg"              ) ) ct = CT_JPG;
        else if (!strcasecmp(s,"image/png"               ) ) ct = CT_PNG;
        else if (!strcasecmp(s,"image/tiff"              ) ) ct = CT_TIFF;
        else if (!strncasecmp(s,"image/",6               ) ) ct = CT_IMAGE;
	else if (!strcasecmp(s,"application/javascript"  ) ) ct = CT_JS;
	else if (!strcasecmp(s,"application/x-javascript") ) ct = CT_JS;
	else if (!strcasecmp(s,"application/x-gzip"      ) ) ct = CT_GZ;
	else if (!strcasecmp(s,"text/javascript"         ) ) ct = CT_JS;
	else if (!strcasecmp(s,"text/x-js"               ) ) ct = CT_JS;
	else if (!strcasecmp(s,"text/js"                 ) ) ct = CT_JS;
	else if (!strcasecmp(s,"text/css"                ) ) ct = CT_CSS;
	else if (!strcasecmp(s,"application/json"        ) ) ct = CT_JSON;
	// facebook.com:
	else if (!strcasecmp(s,"application/vnd.wap.xhtml+xml") ) ct =CT_HTML;
	else if (!strcasecmp(s,"binary/octet-stream") ) ct = CT_UNKNOWN;
	else if (!strcasecmp(s,"application/octet-stream") ) ct = CT_UNKNOWN;
	else if (!strcasecmp(s,"application/binary" ) ) ct = CT_UNKNOWN;
	else if (!strcasecmp(s,"application/x-tar" ) ) ct = CT_UNKNOWN;
	else if ( !strncmp ( s , "audio/",6)  ) ct = CT_UNKNOWN;
	// . semicolon separated list of info, sometimes an element is html
	// . these might have an address in them...
	else if (!strcasecmp(s,"text/x-vcard" )  ) ct = CT_HTML;

	return ct;
}

// . s is a NULL terminated string like "text/html"
int32_t HttpMime::getContentTypePrivate ( char *s ) {
	char *send = NULL;
	char c;
	int32_t ct;
	// skip spaces
	while ( *s==' ' || *s=='\t' ) s++;
	// find end of s
	send = s;
	// they can have "text/plain;charset=UTF-8" too
	for ( ; *send && *send !=';' && *send !='\r' && *send !='\n' ; send++);

	//
	// point to possible charset desgination
	//
	char *t = send ;
	// charset follows the semicolon
	if ( *t == ';' ) {
		// skip semicolon
		t++;
		// skip spaces
		while ( *t==' ' || *t=='\t' ) t++;
		// get charset name "charset=euc-jp"
		if ( strncasecmp ( t , "charset" , 7 ) != 0 ) goto next;
		// skip it
		t += 7;
		// skip spaces, equal, spaces
		while ( *t==' ' || *t=='\t' ) t++;
		if    ( *t=='='             ) t++;
		while ( *t==' ' || *t=='\t' ) t++;
		// get charset
		m_charset = t;
		// get length
		while ( *t && *t!='\r' && *t!='\n' && *t!=' ' && *t!='\t') t++;
		m_charsetLen = t - m_charset;
	}

 next:

	// temp term it for the strcmp() function
	c = *send; *send = '\0';
	// set this
	//ct = -1;

	// returns CT_UNKNOWN if unknown
	ct = getContentTypeFromStr  ( s );

	// log it for reference
	//if ( ct == -1 ) { char *xx=NULL;*xx=0; }
	if ( ct == CT_UNKNOWN ) { 
		//ct = CT_UNKNOWN;
		log("http: unrecognized content type \"%s\"",s);
	}
	// unterm it
	*send = c;
	// return 0 for the contentType if unknown
	return ct;
}

// the table that maps a file extension to a content type
static HashTableX s_mimeTable;
bool s_init = false;

void resetHttpMime ( ) {
	s_mimeTable.reset();
}

const char *extensionToContentTypeStr2 ( char *ext , int32_t elen ) {
	// assume text/html if no extension provided
	if ( ! ext || ! ext[0] ) return NULL;
	if ( elen <= 0 ) return NULL;
	// get hash for table look up
	int32_t key = hash32 ( ext , elen );
	char **pp = (char **)s_mimeTable.getValue ( &key );
	if ( ! pp ) return NULL;
	return *pp;
}

const char *HttpMime::getContentTypeFromExtension ( char *ext , int32_t elen) {
	// assume text/html if no extension provided
	if ( ! ext || ! ext[0] ) return "text/html";
	if ( elen <= 0 ) return "text/html";
	// get hash for table look up
	int32_t key = hash32 ( ext , elen );
	char **pp = (char **)s_mimeTable.getValue ( &key );
	// if not found in table, assume text/html
	if ( ! pp ) return "text/html";
	return *pp;
}


// . list of types is on: http://www.duke.edu/websrv/file-extensions.html
// . i copied it to the bottom of this file though
const char *HttpMime::getContentTypeFromExtension ( char *ext ) {
	// assume text/html if no extension provided
	if ( ! ext || ! ext[0] ) return "text/html";
	// get hash for table look up
	int32_t key = hash32n ( ext );
	char **pp = (char **)s_mimeTable.getValue ( &key );
	// if not found in table, assume text/html
	if ( ! pp ) return "text/html";
	return *pp;
}

const char *HttpMime::getContentEncodingFromExtension ( char *ext ) {
	if ( ! ext ) return NULL;
	if ( strcasecmp ( ext ,"bz2"  )==0 ) return "x-bzip2";
	if ( strcasecmp ( ext ,"gz"   )==0 ) return "x-gzip";
	//if ( strcasecmp ( ext ,"htm"   ) == 0 ) return "text/html";
	//if ( strcasecmp ( ext ,"html"  ) == 0 ) return "text/html";
	return NULL;
}

// make a redirect mime
void HttpMime::makeRedirMime ( char *redir , int32_t redirLen ) {
	char *p = m_buf;
	gbmemcpy ( p , "HTTP/1.0 302 RD\r\nLocation: " , 27 );
	p += 27;
	if ( redirLen > 600 ) redirLen = 600;
	gbmemcpy ( p , redir , redirLen );
	p += redirLen;
	*p++ = '\r';
	*p++ = '\n';
	*p++ = '\r';
	*p++ = '\n';
	*p = '\0';
	m_bufLen = p - m_buf;
	if ( m_bufLen > 1023 ) { char *xx=NULL;*xx=0; }
	// set the mime's length
	//m_bufLen = gbstrlen ( m_buf );
}

// a cacheTime of -1 means browser should not cache at all
void HttpMime::makeMime  ( int32_t    totalContentLen    , 
			   int32_t    cacheTime          ,
			   time_t  lastModified       ,
			   int32_t    offset             , 
			   int32_t    bytesToSend        ,
			   char   *ext                ,
			   bool    POSTReply          ,
			   char   *contentType        ,
			   char   *charset            ,
			   int32_t    httpStatus         ,
			   char   *cookie             ) {
	// assume UTF-8
	//if ( ! charset ) charset = "utf-8";
	// . make the content type line
	// . uses a static buffer
	if ( ! contentType ) 
		contentType = (char *)getContentTypeFromExtension ( ext );

	// do not cache plug ins
	if ( contentType && strcmp(contentType,"application/x-xpinstall")==0)
		cacheTime = -2;

	// assume UTF-8, but only if content type is text
	// . No No No!!!  
	// . This prevents charset specification in html files
	// . -partap

	//if ( ! charset && contentType && strncmp(contentType,"text",4)==0) 
	//	charset = "utf-8";
	// this is used for bz2 and gz files (mp3?)
	const char *contentEncoding = getContentEncodingFromExtension ( ext );
	// the string
	char enc[128];
	if ( contentEncoding ) 
		sprintf ( enc , "Content-Encoding: %s\r\n", contentEncoding );
	else
		enc[0] = '\0';
	// get the time now
	//time_t now = getTimeGlobal();
	time_t now;
	if ( isClockInSync() ) now = getTimeGlobal();
	else                   now = getTimeLocal();
	// get the greenwhich mean time (GMT)
	char ns[128];
	struct tm *timeStruct = gmtime ( &now );
	// Wed, 20 Mar 2002 16:47:30 GMT
	strftime ( ns , 126 , "%a, %d %b %Y %T GMT" , timeStruct );
	// if lastModified is 0 use now
	if ( lastModified == 0 ) lastModified = now;
	// convert lastModified greenwhich mean time (GMT)
	char lms[128];
	timeStruct = gmtime ( &lastModified );
	// Wed, 20 Mar 2002 16:47:30 GMT
	strftime ( lms , 126 , "%a, %d %b %Y %T GMT" , timeStruct );
	// . the pragma no cache string (used just for proxy servers?)
	// . also use cache-control: for the browser itself (HTTP1.1, though)
	// . pns = "Pragma: no-cache\nCache-Control: no-cache\nExpires: -1\n";
	char tmp[128];
	char *pns ;
	// with cache-control on, when you hit the back button, it reloads
	// the page, this is bad for most things... so we only avoid the
	// cache for index.html and PageAddUrl.cpp (the main and addurl page)
	if      ( cacheTime == -2 ) pns =  "Cache-Control: no-cache\r\n"
					   "Pragma: no-cache\r\n"
					   "Expires: -1\r\n";
	// so when we click on a control link, it responds correctly.
	// like turning spiders on.
	else if  ( cacheTime == -1 ) pns = "Pragma: no-cache\r\n"
					   "Expires: -1\r\n";
	// don't specify cache times if it's 0 (let browser regulate it)
	else if ( cacheTime == 0 ) pns = "";
	// otherwise, expire tag: "Expires: Wed, 23 Dec 2001 10:23:01 GMT"
	else {
		time_t  expDate = now + cacheTime;
		timeStruct = gmtime ( &expDate );
		strftime ( tmp , 100 , "Expires: %a, %d %b %Y %T GMT\r\n", 
			   timeStruct );
		pns = tmp;
	}
	// . set httpStatus
	// . a reply to a POST (not a GET or HEAD) should be 201
	char *p = m_buf;
	char *smsg = "";
	if ( POSTReply ) {
		if ( httpStatus == -1 ) httpStatus = 200;
		if ( httpStatus == 200 ) smsg = " OK";
		if ( ! charset ) charset = "utf-8";
		//sprintf ( m_buf , 
		p += sprintf ( p,
			  "HTTP/1.0 %"INT32"%s\r\n"
			  "Date: %s\r\n"
			       //"P3P: CP=\"CAO PSA OUR\"\r\n"
			  "Access-Control-Allow-Origin: *\r\n"
			  "Server: Gigablast/1.0\r\n"
			  "Content-Length: %"INT32"\r\n"
			  //"Expires: Wed, 23 Dec 2003 10:23:01 GMT\r\n"
			  //"Expires: -1\r\n"
			  "Connection: Close\r\n"
			  "%s"
			  "Content-Type: %s\r\n",
			  //"Connection: Keep-Alive\r\n"
			  //"%s"
			  //"Location: fuck\r\n"
			  //"Location: http://192.168.0.4:8000/cgi/3.cgi\r\n"
			  //"Last-Modified: %s\r\n\r\n" ,
			  httpStatus , smsg ,
			  ns , totalContentLen , enc , contentType  );
			  //pns ,
	                  //ns );
			  //lms );
	}
	// . is it partial content?
	// . if bytesToSend is < 0 it means "totalContentLen"
	else if ( offset > 0 || bytesToSend != -1 ) {
		if ( httpStatus == -1 ) httpStatus = 206;
		if ( ! charset ) charset = "utf-8";
		//sprintf ( m_buf , 
		p += sprintf( p,
			      "HTTP/1.0 %"INT32" Partial content\r\n"
			      "%s"
			      "Content-Length: %"INT32"\r\n"
			      "Content-Range: %"INT32"-%"INT32"(%"INT32")\r\n"// added "bytes"
			      "Connection: Close\r\n"
			      //"P3P: CP=\"CAO PSA OUR\"\r\n"
			      // for ajax support
			      "Access-Control-Allow-Origin: *\r\n"
			      "Server: Gigablast/1.0\r\n"
			      "%s"
			      "Date: %s\r\n"
			      "Last-Modified: %s\r\n" 
			      "Content-Type: %s\r\n",
			      httpStatus ,
			      enc ,bytesToSend ,
			      offset , offset + bytesToSend , 
			      totalContentLen ,
			      pns ,
			      ns , 
			      lms , contentType );
		// otherwise, do a normal mime
	}
	else {
		char encoding[256];
		if (charset) sprintf(encoding, "; charset=%s", charset);
		else encoding[0] = '\0';
		
		
		if ( httpStatus == -1 ) httpStatus = 200;
		if ( httpStatus == 200 ) smsg = " OK";
		//sprintf ( m_buf , 
		p += sprintf( p,
			      "HTTP/1.0 %"INT32"%s\r\n"
			      , httpStatus , smsg );
		// if content length is not known, as in diffbot.cpp, then
		// do not print it into the mime
		if ( totalContentLen >= 0 )
			p += sprintf ( p , 
				       // make it at least 4 spaces so we can
				       // change the length of the content 
				       // should we insert a login bar in 
				       // Proxy::storeLoginBar()
				       "Content-Length: %04"INT32"\r\n"
				       , totalContentLen );
		p += sprintf ( p ,
			      "%s"
			      "Content-Type: %s",
			       enc , contentType );
		if ( charset ) p += sprintf ( p , "; charset=%s", charset );
		p += sprintf ( p , "\r\n");
		p += sprintf ( p ,
			       //"Connection: Keep-Alive\r\n"
			       "Connection: Close\r\n"
			       //"P3P: CP=\"CAO PSA OUR\"\r\n"
			       "Access-Control-Allow-Origin: *\r\n"
			       "Server: Gigablast/1.0\r\n"
			       "%s"
			       "Date: %s\r\n"
			       "Last-Modified: %s\r\n" ,
			       pns ,
			       ns , 
			       lms );
	}
	// write the cookie if we have one
	if (cookie) {
		// now it is a list of Set-Cookie: x=y\r\n lines
		//p += sprintf ( p, "Set-Cookie: %s\r\n", cookie);
		if ( strncmp(cookie,"Set-Cookie",10 ) )
			p += sprintf(p,"Set-Cookie: ");
		p += sprintf ( p, "%s", cookie);
		if ( p[-1] != '\n' && p[-2] != '\r' ) {
			*p++ = '\r';
			*p++ = '\n';
		}
	}
			
	// write another line to end the mime
	p += sprintf(p, "\r\n");
	// set the mime's length
	//m_bufLen = gbstrlen ( m_buf );
	m_bufLen = p - m_buf;
}


//FILE EXTENSIONS to MIME CONTENT-TYPE
//------------------------------------

// set hash table 
static char *s_ext[] = {
      "ai" , "application/postscript",
     "aif" , "audio/x-aiff",
    "aifc" , "audio/x-aiff",
    "aiff" , "audio/x-aiff",
     "asc" , "text/plain",
      "au" , "audio/basic",
     "avi" , "video/x-msvideo",
   "bcpio" , "application/x-bcpio",
     "bin" , "application/octet-stream",
     "bmp" , "image/gif",
      "bz2", "application/x-bzip2",
       "c" , "text/plain",
      "cc" , "text/plain",
    "ccad" , "application/clariscad",
     "cdf" , "application/x-netcdf",
   "class" , "application/octet-stream",
    "cpio" , "application/x-cpio",
     "cpt" , "application/mac-compactpro",
     "csh" , "application/x-csh",
     "css" , "text/css",
     "dcr" , "application/x-director",
     "dir" , "application/x-director",
     "dms" , "application/octet-stream",
     "doc" , "application/msword",
     "drw" , "application/drafting",
     "dvi" , "application/x-dvi",
     "dwg" , "application/acad",
     "dxf" , "application/dxf",
     "dxr" , "application/x-director",
     "eps" , "application/postscript",
     "etx" , "text/x-setext",
     "exe" , "application/octet-stream",
      "ez" , "application/andrew-inset",
       "f" , "text/plain",
     "f90" , "text/plain",
     "fli" , "video/x-fli",
     "gif" , "image/gif",
    "gtar" , "application/x-gtar",
      "gz" , "application/x-gzip",
       "h" , "text/plain",
     "hdf" , "application/x-hdf",
      "hh" , "text/plain",
     "hqx" , "application/mac-binhex40",
     "htm" , "text/html",
    "html" , "text/html",
     "ice" , "x-conference/x-cooltalk",
     "ief" , "image/ief",
    "iges" , "model/iges",
     "igs" , "model/iges",
     "ips" , "application/x-ipscript",
     "ipx" , "application/x-ipix",
     "jpe" , "image/jpeg",
    "jpeg" , "image/jpeg",
     "jpg" , "image/jpeg",
      "js" , "application/x-javascript",
     "kar" , "audio/midi",
   "latex" , "application/x-latex",
     "lha" , "application/octet-stream",
     "lsp" , "application/x-lisp",
     "lzh" , "application/octet-stream",
       "m" , "text/plain",
     "man" , "application/x-troff-man",
      "me" , "application/x-troff-me",
    "mesh" , "model/mesh",
     "mid" , "audio/midi",
    "midi" , "audio/midi",
     "mif" , "application/vnd.mif",
    "mime" , "www/mime",
     "mov" , "video/quicktime",
   "movie" , "video/x-sgi-movie",
     "mp2" , "audio/mpeg",
     "mp3" , "audio/mpeg",
     "mpe" , "video/mpeg",
    "mpeg" , "video/mpeg",
     "mpg" , "video/mpeg",
    "mpga" , "audio/mpeg",
      "ms" , "application/x-troff-ms",
     "msh" , "model/mesh",
      "nc" , "application/x-netcdf",
     "oda" , "application/oda",
     "pbm" , "image/x-portable-bitmap",
     "pdb" , "chemical/x-pdb",
     "pdf" , "application/pdf",
     "pgm" , "image/x-portable-graymap",
     "pgn" , "application/x-chess-pgn",
     "png" , "image/png",
     "ico" , "image/x-icon",
     "pnm" , "image/x-portable-anymap",
     "pot" , "application/mspowerpoint",
     "ppm" , "image/x-portable-pixmap",
     "pps" , "application/mspowerpoint",
     "ppt" , "application/mspowerpoint",
     "ppz" , "application/mspowerpoint",
     "pre" , "application/x-freelance",
     "prt" , "application/pro_eng",
      "ps" , "application/postscript",
      "qt" , "video/quicktime",
      "ra" , "audio/x-realaudio",
     "ram" , "audio/x-pn-realaudio",
     "ras" , "image/cmu-raster",
     "rgb" , "image/x-rgb",
      "rm" , "audio/x-pn-realaudio",
    "roff" , "application/x-troff",
     "rpm" , "audio/x-pn-realaudio-plugin",
     "rtf" , "text/rtf",
     "rtx" , "text/richtext",
     "scm" , "application/x-lotusscreencam",
     "set" , "application/set",
     "sgm" , "text/sgml",
    "sgml" , "text/sgml",
      "sh" , "application/x-sh",
    "shar" , "application/x-shar",
    "silo" , "model/mesh",
     "sit" , "application/x-stuffit",
     "skd" , "application/x-koan",
     "skm" , "application/x-koan",
     "skp" , "application/x-koan",
     "skt" , "application/x-koan",
     "smi" , "application/smil",
    "smil" , "application/smil",
     "snd" , "audio/basic",
     "sol" , "application/solids",
     "spl" , "application/x-futuresplash",
     "src" , "application/x-wais-source",
    "step" , "application/STEP",
     "stl" , "application/SLA",
     "stp" , "application/STEP",
 "sv4cpio" , "application/x-sv4cpio",
  "sv4crc" , "application/x-sv4crc",
     "swf" , "application/x-shockwave-flash",
       "t" , "application/x-troff",
     "tar" , "application/x-tar",
     "tcl" , "application/x-tcl",
     "tex" , "application/x-tex",
    "texi" , "application/x-texinfo",
  "texinfo", "application/x-texinfo",
     "tif" , "image/tiff",
    "tiff" , "image/tiff",
      "tr" , "application/x-troff",
     "tsi" , "audio/TSP-audio",
     "tsp" , "application/dsptype",
     "tsv" , "text/tab-separated-values",
     "txt" , "text/plain",
     "unv" , "application/i-deas",
   "ustar" , "application/x-ustar",
     "vcd" , "application/x-cdlink",
     "vda" , "application/vda",
     "viv" , "video/vnd.vivo",
    "vivo" , "video/vnd.vivo",
    "vrml" , "model/vrml",
     "wav" , "audio/x-wav",
     "wrl" , "model/vrml",
     "xbm" , "image/x-xbitmap",
     "xlc" , "application/vnd.ms-excel",
     "xll" , "application/vnd.ms-excel",
     "xlm" , "application/vnd.ms-excel",
     "xls" , "application/vnd.ms-excel",
     "xlw" , "application/vnd.ms-excel",
     "xml" , "text/xml",
     "xpm" , "image/x-xpixmap",
     "xwd" , "image/x-xwindowdump",
     "xyz" , "chemical/x-pdb",
      "zip" , "application/zip" ,
      "xpi", "application/x-xpinstall",
      // newstuff
      "warc", "application/warc",
      "arc", "application/arc"
};

// . init s_mimeTable in this call
// . called from HttpServer::init
// . returns false and sets g_errno on error
bool HttpMime::init ( ) {
	// only need to call once
	if ( s_init ) return true;
	// make sure only called once
	s_init = true;
	//s_mimeTable.set ( 256 );
	//s_mimeTable.setLabel("mimetbl");
	if ( ! s_mimeTable.set(4,sizeof(char *),256,NULL,0,false,1,"mimetbl"))
		return false;
	// set table from internal list
	for ( uint32_t i = 0 ; i < sizeof(s_ext)/sizeof(char *) ; i+=2 ) {
		int32_t key = hash32n ( s_ext[i] );
		if ( ! s_mimeTable.addKey ( &key , &s_ext[i+1] ) ) 
			return log("HttpMime::init: failed to set table.");
	}
	// quick text
	const char *tt = getContentTypeFromExtension ( "zip" );
	if ( strcmp(tt,"application/zip") != 0 ) {
		g_errno = EBADENGINEER;
		return log("http: Failed to init mime table correctly.");
	}
	// a more thorough test
	for ( uint32_t i = 0 ; i < sizeof(s_ext)/sizeof(char *) ; i+=2) {
		tt = getContentTypeFromExtension ( s_ext[i] );
		if ( strcmp(tt,s_ext[i+1]) == 0 ) continue;
		g_errno = EBADENGINEER;
		return log("http: Failed to do mime table correctly. i=%"INT32"",i);
	}

	// TODO: set it from a user supplied file here
	return true;
}

bool HttpMime::addCookiesIntoBuffer ( SafeBuf *sb ) {
	// point to start of request
	if ( m_bufLen <= 0 ) return true;
	if ( ! m_mimeStartPtr ) return true;
	if ( ! m_firstCookie  ) return true;
	char *p = m_firstCookie;
	char *pend = m_mimeStartPtr + m_bufLen;
	while ( p < pend ) {
		// compute the length of the string starting at p and ending
		// at a \n or \r
		int32_t len = 0;
		while ( &p[len] < pend && p[len]!='\n' && p[len]!='\r' ) len++;
		// . if we could not find a \n or \r there was an error
		// . MIMEs must always end in \n or \r
		if ( &p[len] >= pend ) return false;
		// . stick a NULL at the end of the line 
		// . overwrites \n or \r TEMPORARILY
		char c = p [ len ];
		p [ len ] = '\0';
		// parse out some meaningful data
		if ( strncasecmp ( p , "Set-Cookie:"   ,10) == 0 ) {
			char *cookie = p + 11;
			if ( cookie[0] == ' ' ) cookie++;
			char *cookieEnd = cookie;
			for ( ; *cookieEnd && *cookieEnd != ';';cookieEnd++);
			int32_t cookieLen = cookieEnd - cookie;
			// accumulate into buffer
			sb->safeMemcpy ( cookie , cookieLen );
			sb->pushChar(';');
			sb->nullTerm();
		}
		// re-insert the character that we replaced with a '\0'
		p [ len ] = c;
		// go to next line
		p += len;
		// skip over the cruft at the end of this line
		while ( p < pend && ( *p=='\r' || *p=='\n' ) ) p++;
	}
	return true;
}
