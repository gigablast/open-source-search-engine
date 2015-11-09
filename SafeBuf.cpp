#include "gb-include.h"

#include <sys/stat.h>
#include "iana_charset.h"
#include "Unicode.h"
#include "SafeBuf.h"
#include "Words.h"
#include "Sections.h"

//static uint32_t utf8Decode ( char *p, char **next = NULL );

// // table for decoding utf8...says how many bytes in the character
// // based on value of first byte.  0 is an illegal value
// static int bytes_in_code[] = {
// 	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
// 	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
// 	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
// 	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
// 	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
// 	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
// 	2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
// 	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,0,0,0,0,0,0,0,0
// };

SafeBuf::SafeBuf(int32_t initSize, char *label ) {
	if(initSize <= 0) initSize = 1;
	m_capacity = initSize;
	m_length = 0;
	m_label = label;
	m_buf = (char*)mrealloc(NULL, 0, m_capacity, m_label );
	if(!m_buf) m_capacity = 0;
	m_usingStack = false;
	m_encoding = csUTF8;
}

void SafeBuf::constructor ( ) {
	m_capacity = 0;
	m_length = 0;
	m_buf = NULL;
	m_usingStack = false;
	m_encoding = csUTF8;
	m_label = NULL;
}	

SafeBuf::SafeBuf() {
	m_capacity = 0;
	m_length = 0;
	m_buf = NULL;
	m_usingStack = false;
	m_encoding = csUTF8;
	m_label = NULL;
}

void SafeBuf::setLabel ( char *label ) {
	m_label = label;
}

SafeBuf::SafeBuf(char* stackBuf, int32_t cap, char* label) {
	m_usingStack = true;
	m_capacity = cap;
	m_buf = stackBuf;
	m_length = 0;
	m_encoding = csUTF8;
	m_label = label;
}

SafeBuf::SafeBuf(char *heapBuf, int32_t bufMax, int32_t bytesInUse, bool ownData) {
	// . If we don't own the data, treat it like a stack buffer
	//   so we won't attempt to free or realloc it.
	// . If you already have data in this buffer, make sure
	//   to explicitly update length.
	m_usingStack = !ownData;
	m_capacity = bufMax;
	m_buf = heapBuf;
	m_length = bytesInUse;
	m_encoding = csUTF8;
}

SafeBuf::~SafeBuf() {
	destructor();
}

void SafeBuf::destructor() {
	if(!m_usingStack && m_buf) 
		mfree(m_buf, m_capacity, "SafeBuf");
	m_buf = NULL;
}

bool SafeBuf::setBuf(char *newBuf, int32_t bufMax, int32_t bytesInUse, bool ownData,
		     int16_t encoding ){
	// . Passing in a null or a capacity smaller than the
	//   used portion of the buffer is pointless, we have
	//   more reliable functions for emptying a buffer.
	if ( !newBuf || bufMax < bytesInUse || bytesInUse < 0 )
		return false;
	if ( !m_usingStack && m_buf )
		mfree( m_buf, m_capacity, "SafeBuf" );
	// . If we don't own the data, treat it like a stack buffer
	//   so we won't attempt to free or realloc it.
	m_usingStack = !ownData;
	m_buf = newBuf;
	m_capacity = bufMax;
	m_length = bytesInUse;
	m_encoding = csUTF8;
	if ( encoding > 0 ) m_encoding = encoding;
	return true;
}

bool SafeBuf::yieldBuf( char **bufPtr, int32_t *bufCapacity, int32_t *bytesInUse,
		        bool *ownData, int16_t *encoding ) {
	// Set the references to our data.
	if ( !bufPtr || !bufCapacity || ! bytesInUse || !ownData )
		return false;
	*bufPtr = m_buf;
	*bufCapacity = m_capacity;
	*bytesInUse = m_length;
	*ownData = !m_usingStack;
	if ( encoding ) {
		*encoding = m_encoding;
	}
	// Clear out our data.
	m_buf = NULL;
	m_capacity = 0;
	m_length = 0;
	m_usingStack = false;
	m_encoding = csUTF8;
	return true;
}

void SafeBuf::purge() {
	if ( !m_usingStack && m_buf )
		mfree( m_buf, m_capacity, "SafeBuf" );
	m_buf = NULL;
	m_capacity = 0;
	m_length = 0;
	m_usingStack = false;
	m_encoding = csUTF8;
}

bool SafeBuf::safePrintf(char *formatString , ...) {
	va_list   ap;
	va_start ( ap, formatString);
	int32_t tmp = vsnprintf ( m_buf + m_length, m_capacity - m_length, 
			       formatString , ap );
	va_end(ap);
	if(tmp + m_length +1>= m_capacity) {
		// +1 some space for a silent \0 at the end
		if(!reserve(m_capacity + tmp + 1)) return false;

		va_start ( ap, formatString);
		tmp = vsnprintf ( m_buf + m_length, m_capacity - m_length,
				  formatString , ap );
		va_end(ap);
	}
	m_length += tmp;
	// this should not hurt anything
	m_buf[m_length] = '\0';
	return true;
}


bool SafeBuf::safeMemcpy(char *s, int32_t len) {
	// put a silent \0 at the end
	//int32_t tmp = len + m_length+1;
	//if(tmp >= m_capacity ) {
	if ( m_length + len > m_capacity ) {
		if ( ! reserve(m_length+len) ) return false;
	}
	gbmemcpy(m_buf + m_length, s, len);
	m_length = m_length + len; // tmp-1;
	// this should not hurt anything
	//m_buf[m_length] = '\0';
	return true;
}

bool SafeBuf::safeMemcpy_nospaces(char *s, int32_t len) {
	// put a silent \0 at the end
	int32_t tmp = len + m_length+1;
	if(tmp >= m_capacity ) {
		if ( ! reserve(tmp) ) return false;
	}
	for ( int32_t i = 0 ; i < len ; i++ ) {
		if ( is_wspace_a(s[i]) ) continue;
		m_buf[m_length++] = s[i];
	}
	//gbmemcpy(m_buf + m_length, s, len);
	//m_length = tmp-1;
	// this should not hurt anything
	m_buf[m_length] = '\0';
	return true;
}

#include "Words.h"

bool SafeBuf::safeMemcpy ( Words *w , int32_t a , int32_t b ) {
	char *p    = w->m_words[a];
	char *pend = w->m_words[b-1] + w->m_wordLens[b-1];
	return safeMemcpy ( p , pend - p );
}

char* SafeBuf::pushStr  (char* str, uint32_t len) {
	int32_t initLen = m_length;
	bool status = safeMemcpy ( str , len );
	status &= nullTerm();
	m_length++; //count the null so it isn't overwritten
	if(!status) return NULL;
	return m_buf + initLen;
}

bool SafeBuf::pushPtr ( void *ptr ) {
	if ( m_length + (int32_t)sizeof(char *) > m_capacity ) 
		if(!reserve(sizeof(char *)))//2*m_capacity + 1))
			return false;
	*(char **)(m_buf+m_length) = (char *)ptr;
	m_length += sizeof(char *);
	return true;
}

bool SafeBuf::pushLong ( int32_t i) {
	if ( m_length + 4 > m_capacity ) 
		if(!reserve(4))//2*m_capacity + 1))
			return false;
	*(int32_t *)(m_buf+m_length) = i;
	m_length += 4;
	return true;
}

bool SafeBuf::pushLongLong ( int64_t i) {
	if ( m_length + 8 > m_capacity && ! reserve(8) )
		return false;
	*(int64_t *)(m_buf+m_length) = i;
	m_length += 8;
	return true;
}

bool SafeBuf::pushFloat ( float i) {
	if ( m_length + 4 > m_capacity ) 
		if(!reserve(4))
			return false;
	*(float *)(m_buf+m_length) = i;
	m_length += 4;
	return true;
}

// hack off trailing 0's
bool SafeBuf::printFloatPretty ( float f ) {

	if ( m_length + 40 > m_capacity && ! reserve(40) )
		return false;

	char *p = m_buf + m_length;

	int len =  sprintf(p,"%f",f);

	// need a decimal point to truncate trailing 0's
	char *ss = strstr(p,".");
	if ( ! ss ) {
		m_length += len;
		return true;
	}

	// hack off trailing zeros
	char *e = p + len - 1;
	int plen = len;

	for ( ; e > p ; e-- ) {
		if ( e[0] == '.' ) {
			//e[0] = '\0';
			plen--;
			break;
		}
		if ( e[0] != '0' ) break;
		//e[0] = '\0';
		plen--;
	}

	m_length += plen;//e - p;
	p[plen] = '\0';
	return true;
}


bool SafeBuf::pushDouble ( double i) {
	if ( m_length + (int32_t)sizeof(double) > m_capacity ) 
		if(!reserve(sizeof(double)))
			return false;
	*(double *)(m_buf+m_length) = i;
	m_length += sizeof(double);
	return true;
}

int32_t SafeBuf::popLong ( ) {
	if ( m_length < 4 ) { char *xx=NULL;*xx=0; }
	int32_t ret = *(int32_t *)(m_buf+m_length-4);
	m_length -= 4;
	return ret;
}

float SafeBuf::popFloat ( ) {
	if ( m_length < 4 ) { char *xx=NULL;*xx=0; }
	float ret = *(float *)(m_buf+m_length-4);
	m_length -= 4;
	return ret;
}

int32_t SafeBuf::pad(const char ch, const int32_t len) {
	for(int32_t i = 0; i < len; ++i)
		pushChar(ch);
	return len;
}

bool SafeBuf::cat(SafeBuf& c) {
	return safeMemcpy(c.getBufStart(), c.length());
}

// returns false with g_errno set on error
bool SafeBuf::cat2 ( SafeBuf& c,
		     char *tagFilter1 ,
		     char *tagFilter2 ) {

	//SafeBuf tmp;
	// reserve 1MB to avoid excessive reallocs
	//if ( ! tmp.reserve(1000000) ) return -1;

	int32_t tlen1 = gbstrlen(tagFilter1);
	int32_t tlen2 = gbstrlen(tagFilter2);
	// parse our buffer up into words and sections
	char *p = c.m_buf;
	// ensure c.m_buf is NULL terminated so we do not overflow!
	//if ( c.m_buf[c.m_length-1]

 loop:
	// scan it 
	for ( ; *p ; p++ ) {
		// match?
		if ( p[0] != tagFilter1[0] ) continue;
		if ( p[1] != tagFilter1[1] ) continue;
		if ( p[2] != tagFilter1[2] ) continue;
		if ( p[3] != tagFilter1[3] ) continue;
		if ( strncmp(p,tagFilter1,tlen1) ) continue;
		// ok, got one!
		break;
	}

	// no more?
	if ( ! *p ) return true;

	// scan for the end tag now
	char *e = p + 1;
	for ( ; *e ; e++ ) {
		// match?
		if ( e[0] != tagFilter2[0] ) continue;
		if ( e[1] != tagFilter2[1] ) continue;
		if ( e[2] != tagFilter2[2] ) continue;
		if ( e[3] != tagFilter2[3] ) continue;
		if ( strncmp(e,tagFilter2,tlen2) ) continue;
		break;
	}

	// unmatched tag?
	if ( ! *e ) { char *xx=NULL;*xx=0; }

	// concatenate it to our buffer
	if ( ! safeMemcpy ( p , e - p ) ) return false;

	// go to next tag
	p++;

	// do more
	goto loop;
}


bool SafeBuf::advance ( int32_t i ) {
	if ( ! reserve ( i ) ) return false;
	m_length += i;
	return true;
}

bool SafeBuf::reserve(int32_t i , char *label, bool clearIt ) {

	// if we don't already have a label and they provided one, use it
	if ( ! m_label ) {
		if ( label ) m_label = label;
		else         m_label = "SafeBuf";
	}

	if(m_length + i > m_capacity) {
		char *tmpBuf = m_buf;
		int32_t tmpCap = m_capacity;
		if(m_usingStack) {
			m_buf = NULL;
			m_capacity += i;
			//if(m_capacity < 8) m_capacity = 8;
			m_buf = (char*)mrealloc(m_buf, 0, m_capacity,m_label);
			if(!m_buf) {
				m_buf = tmpBuf;
				log("safebuf: failed to reserve %"INT32" bytes",
				    m_capacity);
				m_capacity = tmpCap;
				return false;
			}
			log(LOG_DEBUG, "query: safebuf switching to heap: %"INT32"",
			    m_capacity);
			gbmemcpy(m_buf, tmpBuf, m_length);
			// reset to 0's?
			if ( clearIt ) {
				int32_t clearSize = m_capacity - tmpCap;
				memset(m_buf+tmpCap,0,clearSize);
			}
			m_usingStack = false;
			return true;
		}
		m_capacity += i;
		//if(m_capacity < 8) m_capacity = 8;
		m_buf = (char*)mrealloc(m_buf, tmpCap, m_capacity,m_label);
		if(!m_buf) {
			m_buf = tmpBuf;
			log("safebuf: failed to realloc %"INT32" bytes",m_capacity);
			m_capacity = tmpCap;
			return false;
		}
		// reset to 0's?
		if ( clearIt ) {
			int32_t clearSize = m_capacity - tmpCap;
			memset(m_buf+tmpCap,0,clearSize);
		}
		log(LOG_DEBUG, "query: resize safebuf %"INT32" to %"INT32"", 
		    tmpCap, m_capacity);
	}
	// reset to 0's?
	//if ( ! clearIt ) return true;
	//int32_t clearSize = m_capacity - m_length;
	//memset(m_buf+m_length,0,clearSize);
	return true;
}


//reserve this many bytes, if we need to alloc, we double the 
//buffer size.
bool SafeBuf::reserve2x(int32_t i, char *label) {
	//watch out for overflow!
	if((m_capacity << 1) + i < m_capacity) return false;
	if(i + m_length >= m_capacity)
		return reserve(m_capacity + i,label);
	else return true;
}

int32_t SafeBuf::saveToFile ( char *dir , char *filename ) {
	char buf[1024];
	snprintf(buf,1024,"%s/%s",dir,filename);
	return dumpToFile ( buf );
}

int32_t SafeBuf::save ( char *fullFilename ) {
	return dumpToFile ( fullFilename );
}

int32_t SafeBuf::dumpToFile(char *filename ) {
 retry22:
	int32_t fd = open ( filename , O_CREAT | O_WRONLY | O_TRUNC ,
			    getFileCreationFlags() );
			    //S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH );
	if ( fd < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry22;
		log("safebuf: Failed to open %s for writing: %s", 
		    filename,mstrerror(errno));
		return -1;
	}
	//logf(LOG_DEBUG, "test: safebuf %"INT32" bytes written to %s",m_length,
	//     filename);
 retry23:
	int32_t bytes = write(fd, (char*)m_buf, m_length) ;
	if ( bytes != m_length ) {
		// valgrind
		if ( bytes <= 0 && errno == EINTR ) goto retry23;
		logf(LOG_DEBUG,"test: safebuf bad write %"INT32" != %"INT32": %s",
		     bytes,m_length,mstrerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return m_length;
}

// return -1 on error
int32_t SafeBuf::safeSave (char *filename ) {
 retry22:

	// first write to tmp file
	char tmp[1024];
	SafeBuf fn(tmp,1024);
	fn.safePrintf( "%s.saving",filename );

	int32_t fd = open ( fn.getBufStart() ,
			    O_CREAT | O_WRONLY | O_TRUNC ,
			    getFileCreationFlags() );
			 // S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH );
	if ( fd < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry22;
		log("safebuf: Failed to open %s for writing: %s", 
		    fn.getBufStart(), mstrerror(errno));
		return -1;
	}
	//logf(LOG_DEBUG, "test: safebuf %"INT32" bytes written to %s",m_length,
	//     filename);
 retry23:
	int32_t bytes = write(fd, (char*)m_buf, m_length) ;
	if ( bytes != m_length ) {
		// valgrind
		if ( bytes <= 0 && errno == EINTR ) goto retry23;
		logf(LOG_DEBUG,"test: safebuf bad write %"INT32" != %"INT32": %s",
		     bytes,m_length,mstrerror(errno));
		close(fd);
		return -1;
	}
	close(fd);

	// remove original file before replacing it
	if ( access (filename , F_OK ) == 0) unlink ( filename );

	// now move it to the actual filename
	//int32_t status = ::rename ( fn.getBufStart() , filename );
	int32_t status = ::link ( fn.getBufStart() , filename );

	// note it
	//log("sb: saved %s safely",filename);

	if ( status == -1 ) {
		//g_errno = errno;
		log("sb: safeBuf::safeSave::link: %s",mstrerror(errno));
		return -1;
	}

	return m_length;
}


int32_t SafeBuf::fillFromFile(char *dir,char *filename,char *label) {
	m_label = label;
	char buf[1024];
	if ( dir ) snprintf(buf,1024,"%s/%s",dir,filename);
	else       snprintf(buf,1024,"%s",filename);
	return fillFromFile ( buf );
}

char *SafeBuf::getNextLine ( char *p ) {
	// skip till \n
	for ( ; *p ; p++ )
		if ( *p == '\n' ) break;
	if ( *p == '\n' ) p++;
	if ( *p == '\0' ) return NULL;
	return p;
}

// returns -1 on error
int32_t SafeBuf::catFile(char *filename) {
	SafeBuf sb2;
	if ( sb2.fillFromFile(filename) < 0 ) return -1;
	// add 1 for a null
	if ( ! reserve ( sb2.length() + 1 ) ) return -1;
	cat ( sb2 );
	return length();
}


// returns -1 on error
int32_t SafeBuf::fillFromFile(char *filename) {
	struct stat results;
	if (stat(filename, &results) != 0) {
		// An error occurred
		log(LOG_DEBUG, "query: Failed to open %s for reading: ", 
		    filename);
		// 0 means does not exist or file is empty
		return 0;
	}

	// The size of the file in bytes is in
	// results.st_size
	reserve(results.st_size+1);
	
 retry:
	int32_t fd = open ( filename , O_RDONLY , getFileCreationFlags() );
			 // S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH );
	if ( ! fd ) {
		// valgrind
		if ( errno == EINTR ) goto retry;
		log(LOG_DEBUG, "query: Failed to open %s for reading: ",
		    filename);
		// -1 means there was a read error of some sorts
		return -1;//false;
	}
retry2:
	int32_t numRead = read(fd, m_buf+m_length, results.st_size);
	// valgrind
	if ( numRead<0 && errno == EINTR ) goto retry2;
	close(fd);
	// add a \0 for good meaure
	if ( numRead >= 0 ) {
		m_length += numRead;
		m_buf[m_length] = '\0';
	}

	return numRead;
}

// safely print the string, converting Latin1 to Utf8
bool SafeBuf::safeLatin1ToUtf8(char *s, int32_t len) {
	// check how far we're going to grow
	int32_t tmp = len + m_length;
	for ( int32_t i = 0; i < len; i++ ) {
		unsigned char c = (unsigned char)s[i];
		// check if this expands to 2 chars
		if ( c >= 0x80 ) tmp++;
		//windows-1252 extensions
		//if (c >= 130 && c < 160) tmp++;
	}
	// make sure we have room
	if ( tmp >= m_capacity ) {
		int32_t expanded = tmp - (len + m_length);
		if (!reserve( m_capacity + len + expanded))
			return false;
	}
	// convert it over
	char *p = m_buf + m_length;
	/*
#if 0
	for (int32_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c < 0x80) {
			*p = c;
			p++;
		}
		else {
			*p = (char)(0xc0 | (c >> 6 & 0x1f));
			p++;
			*p = (char)(0x80 | (c & 0x3f));
			p++;
		}
	}
#else
	*/
	// use the iconv function
	p += latin1ToUtf8(p, m_capacity-m_length, s, len);
	//#endif
	
	// set the new length
	m_length = p - m_buf;
	return true;
}

// safely print the string, converting Utf8 to Latin1
/*
bool SafeBuf::safeUtf8ToLatin1(char *s, int32_t len) {
	// JAB: const-ness for the optimizer
	char *next = NULL;
	const char *current = s;
	int32_t count = 0;
	while (current && current <(s+len)) {
		utf8Decode(current,&next);
		// increment past this utf8 char
		current = next;
		count++;
	}
	// make sure we have room
	if ( m_length + count >= m_capacity ) {
		if (!reserve( 2 * m_capacity + count))
			return false;
	}
	// convert it over
	char *p = m_buf + m_length;
	current = s;
	while (current && current <(s+len)){
		// get the decoded char
		int32_t c32 = utf8Decode(current,&next);
		// if it's under 256, print it, otherwise print ?
		if ( c32 < 256 ) *p = (char)c32;
		else             *p = '?';
		// increment past this utf8 char
		current = next;
		p++;
	}
	// set the new length
	m_length += count;
	return true;
}
*/

// a special replace
bool SafeBuf::insert ( SafeBuf *c , int32_t insertPos ) {
	return safeReplace ( c->getBufStart() ,
			     c->length()      ,
			     insertPos        ,
			     0                );
}

bool SafeBuf::insert ( char *s , int32_t insertPos ) {
	return safeReplace ( s         ,
			     gbstrlen(s) ,
			     insertPos ,
			     0         );
}

bool SafeBuf::insert2 ( char *s , int32_t slen , int32_t insertPos ) {
	return safeReplace ( s         ,
			     slen      ,
			     insertPos ,
			     0         );
}

bool SafeBuf::replace ( char *src , char *dst ) {
	int32_t len1 = gbstrlen(src);
	int32_t len2 = gbstrlen(dst);
	if ( len1 != len2 ) {
		int32_t niceness = 0;
		return safeReplace2 ( src , len1,
				      dst , len2,
				      niceness ,
				      0 );
	}
	for ( char *p = strstr ( m_buf , src ) ; p ; p = strstr(p+len1,src ) )
		gbmemcpy ( p , dst , len2 );
	return true;
}

bool SafeBuf::removeChunk1 ( char *p , int32_t len ) {
	int32_t off = p - m_buf;
	return removeChunk2 ( off , len );
}

bool SafeBuf::removeChunk2 ( int32_t pos , int32_t len ) {
	if ( len == 0 ) return true;
	char *dst = m_buf + pos;
	char *src = m_buf + pos + len;
	int32_t moveLen = m_buf + m_length - src;
	memmove(dst, src, moveLen);
	m_length -= len;
	m_buf[m_length] = '\0';
	return true;
}


// replace string at "pos/replaceLen" with s/len
bool SafeBuf::safeReplace ( char *s, int32_t len, int32_t pos, int32_t replaceLen ) {
	// make sure we have room
	int32_t diff = len - replaceLen;
	// add in one for the silent terminating \0
	int32_t newLen = m_length + diff ;
	if ( newLen+1 > m_capacity ) {
		if ( !reserve ( 2 * newLen + 1 ) )
			return false;
	}
	// shift memory over
	if ( diff != 0 ) {
		char *src = m_buf + (pos + replaceLen);
		char *dst = src + diff;
		int32_t  movelen = m_length - (pos + replaceLen);
		memmove(dst, src, movelen);
	}
	// replace
	char *p = m_buf + pos;
	gbmemcpy(p, s, len);
	m_length = newLen;
	// silent terminating \0
	m_buf[m_length] = '\0';
	return true;
}

bool SafeBuf::safeReplace3 ( char *s, char *t , int32_t niceness ) {
	if ( ! safeReplace2 ( s , gbstrlen(s) ,
			      t , gbstrlen(t) ,
			      niceness ) )
		return false;
	if ( ! nullTerm() ) 
		return false;
	return true;
}

// return false and set g_errno on error
bool SafeBuf::safeReplace2 ( char *s, int32_t slen, 
			     char *t , int32_t tlen ,
			     int32_t niceness ,
			     int32_t startOff ) {

	char *pend2 = m_buf + m_length - slen + 1;
	int32_t count = 0;
	for ( char *p = m_buf + startOff ; p < pend2 ; p++ ) {
		// breathe
		QUICKPOLL(niceness);
		// search
		if ( p[0] != s[0] ) continue;
		// compare 2nd char
		if ( slen >= 2 && p[1] != s[1] ) continue;
		// check all chars now
		if ( slen >= 3 && strncmp(p,s,slen) ) continue;
		// count them
		count++;
	}

	// nothing to replace? all done then
	if ( count == 0 ) return true;
	
	int32_t extra = (tlen - slen) * count;
	// allocate new space
	int32_t need = m_length + extra;
	// make a new safebuf to copy into
	char *bbb = (char *)mmalloc(need,"saferplc");
	if ( ! bbb ) return false;
	// do it
	char *dst = bbb;
	char *pend = m_buf + m_length;
	// scan all
	for ( char *p = m_buf ; p < pend ; p++ , dst++ ) {
		// assume not a match
		*dst = *p;
		// breathe
		QUICKPOLL(niceness);
		// search
		if ( p[0] != s[0] ) continue;
		// must be big enough
		if ( p + slen > pend ) continue;
		// compare 2nd char
		if ( slen >= 2 && p[1] != s[1] ) continue;
		// check all chars now
		if ( slen >= 3 && strncmp(p,s,slen) ) continue;
		// undo copy
		gbmemcpy ( dst , t , tlen );
		// advance for that
		dst += tlen - 1;
		p   += slen - 1;
	}
	// clear us
	purge();
	// now this is our new junk
	bool status = safeMemcpy ( bbb , dst - bbb );
	// clear what we had
	mfree ( bbb , need , "saferplc");
	return status;
}


bool SafeBuf::copyToken(char* s) {
	char* p = s;
	while(*p && !isspace(*p)) p++;
	return safeMemcpy(s, (p - s));
}


bool SafeBuf::setEncoding(int16_t cs) {
	//only support utf8 and latin1 encoding for now
	if ((cs != csUTF8) &&
	    (cs != csISOLatin1)){
		m_encoding = csUTF8; //default
		return false;
	}
	m_encoding = cs;
	return true;
}

bool  SafeBuf::utf8Encode2(char *s, int32_t len, bool encodeHTML,int32_t niceness) {
	int32_t tmp = m_length;
	if ( m_encoding == csUTF8 ) {
		if (! safeMemcpy(s,len)) return false;
	}
	//else if ( m_encoding == csISOLatin1 ) {
	//	if (! safeUtf8ToLatin1(s,len)) return false;
	//}
	else
		return false;
	if (!encodeHTML) return true;
	return htmlEncode(m_length-tmp,niceness);
}



bool SafeBuf::utf32Encode(UChar32* codePoints, int32_t cpLen) {
	if(m_encoding != csUTF8) return safePrintf("FIXME %s:%i", __FILE__, __LINE__);

    int32_t need = 0;
    for(int32_t i = 0; i < cpLen;i++) need += utf8Size(codePoints[i]);
	if(!reserve(need)) return false;
    
    for(int32_t i = 0; i < cpLen;i++) {
		m_length += ::utf8Encode(codePoints[i], m_buf + m_length);
	}
	
    return true;
}

/*
bool SafeBuf::utf32Encode(UChar32 c) {
	if(!reserve2x(8)) return false;

	if ( m_encoding == csUTF8 ) {
		m_length += ::utf8Encode(c, m_buf + m_length);
		return true;
	}
	if ( m_encoding == csISOLatin1 ) {
		if(c < 128) {
			*(m_buf + m_length++) = (char)c;
		}
		else {
			*(m_buf + m_length++) = '?';
		}
		return true;
	}
	return false;
}
*/

bool  SafeBuf::latin1Encode(char *s, int32_t len, bool encodeHTML,int32_t niceness) {
	int32_t tmp = m_length;
	switch(m_encoding) {
	case csUTF8:
		if (! safeLatin1ToUtf8(s,len)) return false;
		break;
	case csISOLatin1:
		if (! safeMemcpy(s,len)) return false;
		break;
	default:
		return false;
	}
	if (!encodeHTML) return true;
	return htmlEncode(m_length-tmp,niceness);
}

/*
bool  SafeBuf::utf16Encode(UChar *s, int32_t len, bool encodeHTML){
	int32_t used=0;
	// string could be up to 4 bytes per character
	if (!reserve( len*4))
		return false;	

	switch(m_encoding) {
	case csUTF8:
		used = utf16ToUtf8(m_buf+m_length, 
				   m_capacity-m_length,
				   s,len);
		m_length += used;
		break;
	case csISOLatin1:
		used = utf16ToLatin1(m_buf+m_length, 
				   m_capacity-m_length,
				   s,len);
		m_length += used;
		break;
	default:
		return false;
	}
	if (!encodeHTML) return (used > 0);
	return htmlEncode(used);
}
*/

bool  SafeBuf::utf8CdataEncode(char *s, int32_t len) {
	int32_t len1 = m_length;
	bool r;
	if ( m_encoding == csUTF8 )
		r = safeMemcpy(s,len);
	//else if ( m_encoding == csISOLatin1 ) 
	//	r = safeUtf8ToLatin1(s,len);
	else
		return false;
	if ( !r ) return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length-2 ) {
		if ( m_buf[p]==']' && m_buf[p+1]==']' && m_buf[p+2]=='>') {
			// rewrite the > as &gt
			safeReplace("&gt", 3, p+2, 1);
		}
		p++;
	}
	return true;
}

/*
bool  SafeBuf::latin1CdataEncode(char *s, int32_t len) {
	int32_t len1 = m_length;
	bool r;
	switch(m_encoding) {
	case csUTF8:
		r = safeLatin1ToUtf8(s,len);
		break;
	case csISOLatin1:
		r = safeMemcpy(s,len);
		break;
	default:
		return false;
	}
	if ( !r ) return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length-2 ) {
		if ( m_buf[p]==']' && m_buf[p+1]==']' && m_buf[p+2]=='>') {
			// rewrite the > as &gt
			safeReplace("&gt", 3, p+2, 1);
		}
		p++;
	}
	return true;
}
*/

/*
bool  SafeBuf::utf16CdataEncode(UChar *s, int32_t len){
	int32_t len1 = m_length;
	int32_t used;
	bool r;

	// string could be up to 4 bytes per character
	if (!reserve( len*4))
		return false;	
	
	switch(m_encoding) {
	case csUTF8:
		used = utf16ToUtf8(m_buf+m_length, 
				   m_capacity-m_length,
				   s,len);
		m_length += used;
		r = (used > 0);
		break;
	case csISOLatin1:
		used = utf16ToLatin1(m_buf+m_length, 
				   m_capacity-m_length,
				   s,len);
		m_length += used;
		r = (used > 0);
		break;
	default:
		return false;
	}
	if ( !r ) return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length-2 ) {
		if ( m_buf[p]==']' && m_buf[p+1]==']' && m_buf[p+2]=='>') {
			// rewrite the > as &gt
			safeReplace("&gt", 3, p+2, 1);
		}
		p++;
	}
	return true;
}
*/

bool  SafeBuf::latin1HtmlEncode(char *s, int32_t len,int32_t niceness) {
	int32_t len1 = m_length;
	bool r;
	switch(m_encoding) {
	case csUTF8:
		r = safeLatin1ToUtf8(s,len);
		break;
	case csISOLatin1:
		r = safeMemcpy(s,len);
		break;
	default:
		return false;
	}
	if ( !r ) return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length ) {
		QUICKPOLL(niceness);
		if ( m_buf[p]=='>' ) {
			// rewrite the > as &gt
			safeReplace("&gt;", 4, p, 1);
		}
		else if ( m_buf[p]=='<' ) {
			// rewrite the < as &lt
			safeReplace("&lt;", 4, p, 1);
		}
		else if ( m_buf[p]=='&' ) {
			// rewrite the & as &amp;
			safeReplace("&amp;", 5, p, 1);
		}
		p++;
	}
	return true;
}

/*
bool  SafeBuf::utf16HtmlEncode(UChar *s, int32_t len){
	int32_t len1 = m_length;
	int32_t used;
	bool r;
	switch(m_encoding) {
	case csUTF8:
		used = utf16ToUtf8(m_buf+m_length, 
				   m_capacity-m_length,
				   s,len);
		m_length += used;
		r = (used > 0);
		break;
	case csISOLatin1:
		used = utf16ToLatin1(m_buf+m_length, 
				   m_capacity-m_length,
				   s,len);
		m_length += used;
		r = (used > 0);
		break;
	default:
		return false;
	}
	if ( !r ) return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length ) {
		if ( m_buf[p]=='>' ) {
			// rewrite the > as &gt
			safeReplace("&gt;", 4, p, 1);
		}
		else if ( m_buf[p]=='<' ) {
			// rewrite the < as &lt
			safeReplace("&lt;", 4, p, 1);
		}
		else if ( m_buf[p]=='&' ) {
			// rewrite the & as &amp;
			safeReplace("&amp;", 5, p, 1);
		}
		p++;
	}
	return true;
}
*/

bool SafeBuf::cdataEncode ( char *s ) {
	return safeCdataMemcpy(s,gbstrlen(s));
}

bool SafeBuf::cdataEncode ( char *s , int32_t len ) {
	return safeCdataMemcpy(s,len);
}


bool  SafeBuf::safeCdataMemcpy ( char *s, int32_t len ) {
	int32_t len1 = m_length;
	if ( !safeMemcpy(s,len) )
		return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length-2 ) {
		if ( m_buf[p]==']' && m_buf[p+1]==']' && m_buf[p+2]=='>') {
			// rewrite the > as &gt
			safeReplace("&gt", 3, p+2, 1);
		}
		p++;
	}
	return true;
}

bool  SafeBuf::htmlEncode(char *s, int32_t lenArg, bool encodePoundSign ,
			  int32_t niceness , int32_t truncateLen ) {
	//bool convertUtf8CharsToEntity ) {
	// . we assume we are encoding into utf8
	// . sanity check
	if ( m_encoding == csUTF16 ) { char *xx = NULL; *xx = 0; }

	// the new truncation logic
	int32_t len = lenArg;
	bool truncate = false;
	int32_t extra = 0;
	if ( truncateLen > 0 && lenArg > truncateLen ) {
		len = truncateLen;
		truncate = true;
		extra = 4;
	}

	// alloc some space if we need to. add a byte for NULL termination.
	if( m_length+len+1+extra>=m_capacity&&!reserve(m_capacity+len+1+extra))
		return false;
	// tmp vars
	char *t    = m_buf + m_length;
	char *tend = m_buf + m_capacity;
	// scan through all 
	char *send = s + len;

	

	for ( ; s < send ; s++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// ensure we have enough room
		if ( t + 12 >= tend ) {
			// save progress
			int32_t written = t - m_buf;
			if ( ! reserve (m_capacity + 100) ) return false;
			// these might have changed, so set them again
			t    = m_buf + written;
			tend = m_buf + m_capacity;
		}
		// this is only used to encode stuff for the validation
		// routine (see XmlDoc::validateOutput). like storing the
		// event tile or description ultimately unto a checkboxspan.*
		// txt file, so don't mess with these punct chars either

		// convert it?
		if ( *s == '"' ) {
			*t++ = '&';
			*t++ = '#';
			*t++ = '3';
			*t++ = '4';
			*t++ = ';';
			continue;
		}
		if ( *s == '<' ) {
			*t++ = '&';
			*t++ = 'l';
			*t++ = 't';
			*t++ = ';';
			continue;
		}
		if ( *s == '>' ) {
			*t++ = '&';
			*t++ = 'g';
			*t++ = 't';
			*t++ = ';';
			continue;
		}
		if ( *s == '&' ) { // && ! convertUtf8CharsToEntity ) {
			*t++ = '&';
			*t++ = 'a';
			*t++ = 'm';
			*t++ = 'p';
			*t++ = ';';
			continue;
		}
		if ( *s == '#' && encodePoundSign ) {
			*t++ = '&';
			*t++ = '#';
			*t++ = '0';
			*t++ = '3';
			*t++ = '5';
			*t++ = ';';
			continue;
		}
		// our own specially decoded entites!
		if ( *s == '+' && s[1]=='!' && s[2]=='-' ) {
			*t++ = '&';
			*t++ = 'l';
			*t++ = 't';
			*t++ = ';';
			s += 2;
			continue;
		}
		// our own specially decoded entites!
		if ( *s == '-' && s[1]=='!' && s[2]=='+' ) {
			*t++ = '&';
			*t++ = 'g';
			*t++ = 't';
			*t++ = ';';
			s += 2;
			continue;
		}
		*t++ = *s;		
	}

	if ( truncate ) {
		gbmemcpy ( t , "... " , 4 );
		t += 4;
	}

	*t = '\0';
	// update the used buf length
	m_length = t - m_buf ;
	// success
	return true;
}


bool  SafeBuf::javascriptEncode(char *s, int32_t len ) {
	// . we assume we are encoding into utf8
	// . sanity check
	if ( m_encoding == csUTF16 ) { char *xx = NULL; *xx = 0; }
	// alloc some space if we need to. add a byte for NULL termination.
	if(m_length+len+1>=m_capacity && !reserve(m_capacity+len))return false;
	// tmp vars
	char *t    = m_buf + m_length;
	char *tend = m_buf + m_capacity;
	// scan through all 
	char *send = s + len;
	for ( ; s < send ; s++ ) {
		// ensure we have enough room
		if ( t + 12 >= tend ) {
			// save progress
			int32_t written = t - m_buf;
			if ( ! reserve (m_capacity + 100) ) return false;
			// these might have changed, so set them again
			t    = m_buf + written;
			tend = m_buf + m_capacity;
		}
		// this is only used to encode stuff for the validation
		// routine (see XmlDoc::validateOutput). like storing the
		// event tile or description ultimately unto a checkboxspan.*
		// txt file, so don't mess with these punct chars either


		// javascript converts all spaces to plusses when it passes
		// the content to the web server so if we have a plus
		// and we care about it, we gotta convert it otherwise it
		// gets converted back to a space
		if ( *s == '+' ) {
			*t++ = ' ';
			continue;
		}

		// i don't think we need to encode these but because
		// the checkboxspans.* files already had them encoded
		// let's keep encoding < and >
		if ( *s == '<' ) {
			*t++ = '&';
			*t++ = 'l';
			*t++ = 't';
			*t++ = ';';
			continue;
		}
		if ( *s == '>' ) {
			*t++ = '&';
			*t++ = 'g';
			*t++ = 't';
			*t++ = ';';
			continue;
		}

		// i guess the javascript encodes these when the checkbox
		// is clicked, so let's keep them encoded too
		if ( *s == '&' ) {
			*t++ = '&';
			*t++ = 'a';
			*t++ = 'm';
			*t++ = 'p';
			*t++ = ';';
			continue;
		}

		// our own specially decoded entites!
		if ( *s == '+' && s[1]=='!' && s[2]=='-' ) {
			*t++ = '&';
			*t++ = 'l';
			*t++ = 't';
			*t++ = ';';
			s += 2;
			continue;
		}
		// our own specially decoded entites!
		if ( *s == '-' && s[1]=='!' && s[2]=='+' ) {
			*t++ = '&';
			*t++ = 'g';
			*t++ = 't';
			*t++ = ';';
			s += 2;
			continue;
		}
		// if s is utf8, convert it?
		// . this is now an official HACK for Address and
		//   Events::print() to remove the utf8 chars so that
		//   the senddiv() javascript doesn't convert them to
		//   %uxxxx format in the url because we can't convert them
		//   back for XmlDoc::getDivId()!!
		if ( getUtf8CharSize(s)>1 ) {
			//! is_ascii ( *s ) ) {
			//*t++ = '&';
			//*t++ = '#';
			//*t++ = 'x';
			*t++ = 'u';
			// loop over the bytes
			int32_t numBytes = getUtf8CharSize(s);
			// sanity
			if ( numBytes >= 5 ) { char *xx=NULL;*xx=0; }
			// write out each bytes as two chars each
			for ( int32_t k = 0 ; k < numBytes ; k++ , s++ ) {
				// store hex digit
				unsigned char v = ((unsigned char)*s)/16 ;
				if ( v < 10 ) v += '0';
				else          v += 'A' - 10;
				*t++ = v;
				// store second hex digit
				v = ((unsigned char)*s) & 0x0f ;
				if ( v < 10 ) v += '0';
				else          v += 'A' - 10;
				*t++ = v;
			}
			// backup for next char
			s--;
			continue;
		}

		*t++ = *s;		
	}
	*t = '\0';
	// update the used buf length
	m_length = t - m_buf ;
	// success
	return true;
}


bool SafeBuf::htmlEncode(char *s) {
	return htmlEncode(s,gbstrlen(s),true,0);
}

// scan the last "len" characters for entities to encode
bool SafeBuf::htmlEncode(int32_t len, int32_t niceness ){
	for (int32_t i = m_length-len; i < m_length ; i++){

		QUICKPOLL ( niceness );

		if ( m_buf[i] == '"' ) {
			if (!safeReplace("&#34;", 4, i, 1))
				return false;
			continue;
		}
		if ( m_buf[i] == '<' ) {
			if (!safeReplace("&lt;", 4, i, 1))
				return false;
			continue;
		}
		if ( m_buf[i] == '>' ) {
			if (!safeReplace("&gt;", 4, i, 1))
				return false;
			continue;
		}
		if ( m_buf[i] == '&' ) {
			if (!safeReplace("&amp;", 5, i, 1))
				return false;
			continue;
		}
	}
	return true;
}

// a static buffer for speed
static char s_ut[256];
static bool s_init23 = false;

void initTable ( ) {
	if ( s_init23 ) return;
	s_init23 = true;
	for ( int32_t c = 0 ; c <= 255 ; c++ ) {
		// assume we must encode it
		s_ut[c] = 1;
		if ( ! is_ascii ( (unsigned char)c ) ) continue;
		if ( c == ' ' ) continue;
		if ( c == '&' ) continue;
		if ( c == '"' ) continue;
		if ( c == '+' ) continue;
		if ( c == '%' ) continue;
		if ( c == '#' ) continue;
		if ( c == '<' ) continue;
		if ( c == '>' ) continue;
		if ( c == '?' ) continue;
		if ( c == ':' ) continue;
		if ( c == '/' ) continue;
		// no need to encode it!
		s_ut[c] = 0;
	}
}

//  url encode the whole buffer
bool SafeBuf::urlEncodeAllBuf ( bool spaceToPlus ) {
	// this makes things faster
	if ( ! s_init23 ) initTable();
	// how many chars do we need?
	char *bs = m_buf;
	int32_t size = 0;
	int32_t need = 0;
	for ( ; *bs ; bs++ ) {
		// one char is one byte
		size++;
		// skip if no encoding required
		if ( s_ut[(unsigned char)*bs] == 0 ) continue;
		// spaces are ok, just a single + byte
		if ( *bs == ' ' && spaceToPlus ) continue;
		// will need to do %ff type of thing
		need += 2;
	}
	// incorporate abse size
	need += size;
	// null term
	need++;
	// ensure enough room
	if ( ! reserve ( need ) ) return false;
	// reset s in case got reallocated
	char *src = m_buf + size - 1;
	char *dst = m_buf + need - 1;
	// null term
	*dst-- = '\0';
	// start off encoding backwards!
	for ( ; src >= m_buf ; src-- ) {
		// copy one byte if no encoding required
		if ( s_ut[(unsigned char)*src] == 0 ) {
			*dst = *src;
			dst--;
			continue;
		}
		// ok, need to encode it
		if ( *src == ' ' && spaceToPlus ) { *dst-- = '+'; continue; }
		unsigned char v;
		// store second hex digit
		v = ((unsigned char)*src) & 0x0f ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		*dst-- = v;
		// store first hex digit
		v = ((unsigned char)*src)/16 ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		*dst-- = v;
		*dst-- = '%';
	}
	return true;
}

// used by PageEvents.cpp to escape out the single quotes in javascript
// strings...
bool SafeBuf::escapeJS (char *s , int32_t slen ) {
	if ( ! reserve ( slen + 1 ) ) return false;
	char *send = s + slen;
	for ( ; s < send ; s++ ) {
		// encode the damn thing
		if ( *s == '\'' ) safeMemcpy("\\\'",2 );
		// i dunno why i need &quot; here... but \\\" does not work!
		// i think because the html parser messes up on it before
		// passing it on to the javascript parser.
		else if ( *s == '\"' ) safeMemcpy("&quot;",6 );
		else if ( *s == '\\' ) safeMemcpy("\\\\",2 );
		else pushChar ( *s );
	}
	return true;
}	


//s is a url, make it safe for printing to html
bool SafeBuf::urlEncode (char *s , int32_t slen, 
			 bool requestPath ,
			 bool encodeApostrophes ) {
	// this makes things faster
	if ( ! s_init23 ) initTable();

	char *send = s + slen;
	for ( ; s < send ; s++ ) {
		if ( *s == '\0' && requestPath ) {
			pushChar(*s);
			continue;
		}
		if ( *s == '\'' && encodeApostrophes ) {
			safeMemcpy("%27",3);
			continue;
		}
		// encode if not fit for display
		//if ( ! is_ascii ( *s ) ) goto encode;
		// skip if no encoding required
		if ( s_ut[(unsigned char)*s] == 0 ) {
			pushChar(*s); 
			continue; 
		}
		// special case
		if ( *s == '?' && requestPath ) {
			pushChar(*s); 
			continue; 
		}

		/*
		switch ( *s ) {
		case ' ': goto encode;
		case '&': goto encode;
		case '"': goto encode;
		case '+': goto encode;
		case '%': goto encode;
		case '#': goto encode;
		case ':': goto encode;
		case '/': goto encode;
		// encoding < and > are more for displaying on an
		// html page than sending to an http server
		case '>': goto encode;
		case '<': goto encode;
		case '?': if ( requestPath ) break;
			  goto encode;
		}
		// otherwise, no need to encode
		pushChar(*s);
		continue;
	encode:
		*/
		// space to +
		if ( *s == ' ' ) { pushChar('+'); continue; }
		pushChar('%');
		// store first hex digit
		unsigned char v = ((unsigned char)*s)/16 ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		pushChar(v);
		// store second hex digit
		v = ((unsigned char)*s) & 0x0f ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		pushChar(v);
	}
	nullTerm();
	return true;
}



bool SafeBuf::dequote (  char *t , int32_t tlen ) {
	if ( tlen == 0 ) return true;
	char *tend = t + tlen;
	for ( ; t < tend; t++ ) {
		if ( *t == '"' ) {
			safeMemcpy("&#34;", 5);
			continue;
		}
		safeMemcpy(t, 1);
	}
	// there may be more things added to m_buf later, so do
	// not null terminate this here...
	//char null = '\0';
	//safeMemcpy(&null, 1);
	return true;
}


void SafeBuf::detachBuf() {
	m_capacity = 0;
	m_length = 0;
	m_buf = NULL;
	m_usingStack = false;
}

// set buffer from another safebuf, stealing it
bool SafeBuf::stealBuf ( SafeBuf *sb ) {
	// free what we got!
	purge();
	m_capacity   = sb->m_capacity;
	m_length     = sb->m_length;
	m_buf        = sb->m_buf;
	m_usingStack = sb->m_usingStack;
	m_encoding   = sb->m_encoding;
	// clear his ptrs
	sb->m_buf = NULL;
	sb->m_capacity = 0;
	sb->m_length = 0;
	return true;
}


bool SafeBuf::operator += (uint64_t i) {
	return safeMemcpy((char*)&i, sizeof(uint64_t));
}

bool SafeBuf::operator += (int64_t i) {
	return safeMemcpy((char*)&i, sizeof(int64_t));
}

// bool SafeBuf::operator += (int32_t i) {
// 	return safeMemcpy((char*)&i, sizeof(int32_t));
// }

// bool SafeBuf::operator += (uint32_t i) {
// 	return safeMemcpy((char*)&i, sizeof(uint32_t));
// }

bool SafeBuf::operator += (float i) {
	return safeMemcpy((char*)&i, sizeof(float));
}

bool SafeBuf::operator += (double i) {
	return safeMemcpy((char*)&i, sizeof(double));
}

bool SafeBuf::operator += (char c) {
	return safeMemcpy((char*)&c, sizeof(char));
}

bool SafeBuf::operator += (uint32_t i) {
	return safeMemcpy((char*)&i, sizeof(uint32_t));
}

bool SafeBuf::operator += (uint16_t i) {
	return safeMemcpy((char*)&i, sizeof(uint16_t));
}

bool SafeBuf::operator += (uint8_t i) {
	return safeMemcpy((char*)&i, sizeof(uint8_t));
}


// bool SafeBuf::operator += (double i) {
// 	return safeMemcpy((char*)&i, sizeof(double));
// }

char& SafeBuf::operator[](int32_t i) {
	return m_buf[i];
}

// for decoding Utf8
// uint32_t utf8Decode ( char *p, char **next = NULL ) {
// 	int num_bytes = bytes_in_code[*p];
// 	if (!num_bytes){
// 		// ill-formed byte sequence
// 		// lets just return an invalid character and go on to the next
// 		if (next) *next = p+1;
// 		return (uint32_t)0xffffffff;
// 	}      
// 	if (next){
// 		*next = p + num_bytes;
// 	}                                        
// 	switch(num_bytes){
// 	case 1:
// 		return (uint32_t)*p;
// 	case 2:
// 		return (uint32_t)((*p & 0x1f)<<6 |
// 		(*(p+1) & 0x3f));
// 	case 3:
// 		return (uint32_t)((*p & 0x0f)<<12 |
// 		(*(p+1) & 0x3f)<<6 |
// 		(*(p+2) & 0x3f));
// 	case 4:
// 		return (uint32_t)((*p & 0x07)<<18 |
// 		(*(p+1) & 0x3f)<<12 |
// 		(*(p+2) & 0x3f)<<6 |
// 		(*(p+3) & 0x3f));
// 	default:
// 		return (uint32_t) -1;
// 	};
// }


bool SafeBuf::printKey(char* key, char ks) {
	switch (ks) {
	case 12:
		safePrintf("%016"XINT64"%08"XINT32"",
			   *(int64_t*)(key+(sizeof(int32_t))), *(int32_t*)key);
		break;
	case 16:
		safePrintf("%016"XINT64"%016"XINT64" ",
			   *(int64_t*)(key+(sizeof(int64_t))),
			   *(int64_t *)key);
		break;
	default:
		break;
	}
	return true;
}

// . filter out tags and line breaks and extra spaces or whatever
// . used for printing sentences from XmlDoc::printEventSentences()
bool SafeBuf::safePrintFilterTagsAndLines ( char *p , int32_t plen ,
					    bool oneWordPerLine ) {

	// prealloc -- also for terminating \0
	if ( ! reserve ( plen + 1 ) ) return false;

	// write into here
	char *dst = m_buf + m_length;

	char *pend = p + plen;
	char size;

	// set this to true so we do not lead with a space
	bool lastWasSpace = true;
	for ( ; p < pend ; p += size ) {
		// get size
		size = getUtf8CharSize(p);
		// if a tag, then skip till '>' (assume no comment
		// tags are in here!)
		if ( *p == '<' ) {
			// count # of lost chars
			int32_t count = 0;
			//char *pstart = p;
			for ( ; p < pend && *p != '>' ; p++ ) count++;
			// add a space to represent the tag
			if ( oneWordPerLine ) *dst++ = '\n';
			else                  *dst++ = ' ';
			lastWasSpace = true;
			continue;
		}
		// if we are space, write a simple space
		if ( is_wspace_utf8 ( p ) ) {
			// no back to back spaces
			if ( lastWasSpace ) continue;
			if ( oneWordPerLine ) *dst++ = '\n';
			else                  *dst++ = ' ';
			lastWasSpace = true;
			continue;
		}
		// punct to space
		if ( oneWordPerLine &&
		     is_punct_utf8 ( p ) ) {
			// no back to back spaces
			if ( lastWasSpace ) continue;
			*dst++ = '\n';
			lastWasSpace = true;
			continue;
		}

		// not a space
		lastWasSpace = false;
		// . copy the char if not a tag char
		// . if only 1 byte, done
		if ( size == 1 ) { *dst++ = *p; continue; }
		// might consist of multiple bytes in utf8
		gbmemcpy ( dst , p , size );
		dst += size;
	}
	// sanity scan
	//char *end = m_buf + m_length;
	//for ( char *s = m_buf ; s < end ; s++ )
	//	if ( *s == '<' )
	//		log("hey");
	
	// update length now
	m_length = dst - m_buf;
	// this should not hurt anything
	m_buf[m_length] = '\0';
	// null term for printing
	//m_buf[m_length++] = '\0';
	return true;
}

void SafeBuf::filterTags ( ) {
	// write into here
	char *dst = m_buf ;
	char *p = m_buf;
	char *pend = m_buf + m_length;
	char size;
	// set this to true so we do not lead with a space
	bool lastWasSpace = true;
	for ( ; p < pend ; p += size ) {
		// get size
		size = getUtf8CharSize(p);
		// if a tag, then skip till '>' (assume no comment
		// tags are in here!)
		if ( *p == '<' ) {
			// count # of lost chars
			int32_t count = 0;
			//char *pstart = p;
			for ( ; p < pend && *p != '>' ; p++ ) count++;
			// no back to back spaces
			if ( lastWasSpace ) continue;
			// add a space to represent the tag
			*dst++ = ' ';
			lastWasSpace = true;
			continue;
		}
		// if we are space, write a simple space
		if ( is_wspace_utf8 ( p ) ) {
			// no back to back spaces
			if ( lastWasSpace ) continue;
			*dst++ = ' ';
			lastWasSpace = true;
			continue;
		}
		// not a space
		lastWasSpace = false;
		// . copy the char if not a tag char
		// . if only 1 byte, done
		if ( size == 1 ) { *dst++ = *p; continue; }
		// might consist of multiple bytes in utf8
		gbmemcpy ( dst , p , size );
		dst += size;
	}
	// update length now
	m_length = dst - m_buf;
	// this should not hurt anything
	m_buf[m_length] = '\0';
}

void SafeBuf::filterQuotes ( ) {
	char *p = m_buf;
	char *pend = m_buf + m_length;
	char size;
	for ( ; p < pend ; p += size ) {
		// get size
		size = getUtf8CharSize(p);
		// if a tag, then skip till '>' (assume no comment
		// tags are in here!)
		if ( *p != '\"' ) continue;
		// filter?
		*p = ' ';
	}
}

#include "Tagdb.h"

// if safebuf is a buffer of Tags from Tagdb.cpp
Tag *SafeBuf::addTag2 ( char *mysite , 
			char *tagname ,
			int32_t  now ,
			char *user ,
			int32_t  ip ,
			int32_t  val ,
			char  rdbId ) {
	char buf[64];
	sprintf(buf,"%"INT32"",val);
	int32_t dsize = gbstrlen(buf) + 1;
	return addTag ( mysite,tagname,now,user,ip,buf,dsize,rdbId,true);
}

// if safebuf is a buffer of Tags from Tagdb.cpp
Tag *SafeBuf::addTag3 ( char *mysite , 
			char *tagname ,
			int32_t  now ,
			char *user ,
			int32_t  ip ,
			char *data ,
			char  rdbId ) {
	int32_t dsize = gbstrlen(data) + 1;
	return addTag ( mysite,tagname,now,user,ip,data,dsize,rdbId,true);
}

Tag *SafeBuf::addTag ( char *mysite , 
		       char *tagname ,
		       int32_t  now ,
		       char *user ,
		       int32_t  ip ,
		       char *data ,
		       int32_t  dsize ,
		       char  rdbId ,
		       bool  pushRdbId ) {
	int32_t need = dsize + 32 + sizeof(Tag);
	if ( user   ) need += gbstrlen(user);
	if ( mysite ) need += gbstrlen(mysite);
	if ( ! reserve ( need ) ) return NULL;
	if ( pushRdbId && ! pushChar(rdbId) ) return NULL;
	Tag *tag = (Tag *)getBuf();
	tag->set(mysite,tagname,now,user,ip,data,dsize);
	incrementLength ( tag->getRecSize() );
	if ( tag->getRecSize() > need ) { char *xx=NULL;*xx=0; }
	return tag;
}

bool SafeBuf::addTag ( Tag *tag ) {
	int32_t recSize = tag->getSize();
	//tag->setDataSize();
	if ( tag->m_recDataSize <= 16 ) { 
		// note it
		return log("safebuf: encountered corrupted tag datasize=%"INT32".",
			   tag->m_recDataSize);
		//char *xx=NULL;*xx=0; }
	}
	return safeMemcpy ( (char *)tag , recSize );
}

bool SafeBuf::htmlEncodeXmlTags ( char *s , int32_t slen , int32_t niceness ) {
	// count the xml tags so we know how much buf to allocated
	char *p = s;
	char *pend = s + slen;
	int32_t count = 0;
	for ( ; p < pend ; p++ ) {
		QUICKPOLL(niceness);
		if ( *p == '<'  ) count += 6; // assume '>' too!
		if ( *p == '\n' ) count += 3; // <br>
		if ( *p == '\t' ) count += 17; // &nbsp;
	}
	// include \0
	int32_t need = slen + 1;
	// then < has a > and each is expanded to &lt; or &gt;
	need += count ;
	// reserve that
	if ( ! reserve ( need ) ) return false;
	// point to dst buf
	char *dst = m_buf;
	bool inXmlTag = false;
	// copy over now
	p = s;
	for ( ; p < pend ; p++ ) {
		QUICKPOLL(niceness);
		// make > into &gt; then break out
		if ( inXmlTag && *p == '>' ) {
			gbmemcpy ( dst , "&gt;", 4 );
			dst += 4;
			inXmlTag = false;
			continue;
		}
		// translate \n to br for xml docs
		if ( *p == '\n' ) {
			gbmemcpy ( dst , "<br>", 4 );
			dst += 4;
			continue;
		}
		// translate \n to br for xml docs
		if ( *p == '\t' ) {
			gbmemcpy ( dst , "&nbsp;&nbsp;&nbsp;", 18 );
			dst += 18;
			continue;
		}
		// copy over if nothing special
		if ( *p != '<' ) {
			*dst++ = *p;
			continue;
		}
		// get tag id (will be 0 for </>)
		char *tagName;
		if ( p[1] == '/' ) tagName = p + 2;
		else               tagName = p + 1;
		NodeType *nt;
		getTagId ( tagName , &nt );
		// keep html tags
		if ( nt && ! nt->m_isXmlTag ) {
			*dst++ = '<';
			continue;
		}
		// ok, it's an xml tag so use &lt;
		gbmemcpy ( dst, "&lt;", 4 );
		dst += 4;
		inXmlTag = true;
	}
	// store \0 but do not count towards m_length
	*dst = '\0';
	// update length
	m_length = dst - m_buf;
	return true;
}

// this puts a \0 at the end but does not update m_length for the \0 
bool  SafeBuf::safeStrcpy ( char *s ) {
	if ( ! s ) return true;
	int32_t slen = gbstrlen(s);
	if ( ! reserve ( slen+1 ) ) return false;
	if ( ! safeMemcpy(s,slen) ) return false;
	nullTerm();
	return true;
}

bool SafeBuf::truncateLongWords ( char *s , int32_t srcLen , int32_t minmax ) {
	// ensure enough room
	if ( ! reserve ( srcLen * 2 + 50 ) ) return false;
	// start of where we write into
	char *dstart = m_buf + m_length;
	int32_t nospacecount = 0;
	char *src = s;
	char *srcEnd = s + srcLen;
	char *dst = getBuf();
	char size;
	int32_t scan = 10;
	bool lastWasSpace = true;
	for ( ; src < srcEnd ; src += size ) {
		size = getUtf8CharSize(src);
		// copy full tags
		if ( *src == '<' ) {
			for ( ; *src && *src != '>' ; )	*dst++ = *src++;
			nospacecount = 0;
			// copy the remaining '>' using loop logic
			if ( *src == '>' ) src--;
			continue;
		}	
		// a space?
		if ( is_wspace_a(*src) ) {
			nospacecount = 0;
			// make all spaces now plain space so PageSubmit.cpp
			// can fill in <textarea> tags with the event 
			// description...
			if ( ! lastWasSpace ) *dst++ = ' ';//*src;
			lastWasSpace = true;
			continue;
		}
		// NEW! any punct mark is a good breaking point
		if ( ! is_alnum_utf8(src) )
			nospacecount = -1;
		// reset this
		lastWasSpace = false;
		// truncate if overdue for a space
		if ( nospacecount >= minmax ) { // 25 ) {
			// go forward up to 5 more characters looking
			// for a non-alnum to break on
			char *x    = src;
			char *xend = src + scan;
			char  xsize;
			for ( ; *x && x < xend ; x += xsize ) {
				xsize = getUtf8CharSize(x);
				if ( ! is_alnum_utf8(x) ) break;
			}

			// bail on truncation if completing it would be int16_ter
			// than adding the ellipsis
			if ( *x && x < xend ) {
				// scan for next space
				char *w    = x;
				char *wmax = x + 3;
				for ( ; w<wmax && *w && !is_wspace_a(*w); w++);
				// ok bail!
				if ( w<wmax ) { nospacecount=0; goto resume;}
			}
					

			// if we had a nice break point at "src" or int16_tly
			// thereafter, then break right after that!
			if ( *x && x < xend ) {
				gbmemcpy ( dst , src , x - src );
				dst += x - src;
				src = x;
			}


			gbmemcpy ( dst , "... " , 4 );
			dst += 4;
			// skip until space or tag
			for ( ; *src ; src++ ) {
				if ( *src == '<' ) 
					break;
				if ( is_wspace_a(*src) ) 
					break;
			}
			// if could not find < or space, stop then i guess
			if ( ! *src ) break;
			// start printing there again
			src--;
			// its a size of 1
			size = 1;
			// we added a "... " which has a space
			nospacecount = 0;
			continue;
		}
		resume:
		// a nonspace i guess
		nospacecount++;
		// copy the char
		if ( size == 1 ) {
			*dst++ = *src;
			continue;
		}
		gbmemcpy ( dst , src , size );
		dst += size;
	}
	// remove trailing space in case we ended in "... "
	if ( dst > dstart && dst[-1] == ' ' ) dst--;
	// sanity check
	char *end = m_buf + m_capacity;
	if ( dst+1 > end ) { char *xx=NULL;*xx=0; }
	// update length
	m_length = dst - m_buf;
	// a safe null term
	*dst = '\0';
	return true;
}

bool SafeBuf::queryFilter ( char *s , int32_t slen ) {

	// include terminating \0
	if ( ! reserve ( slen + 1 ) ) return false;

	// copy over
	char *src = s;
	char *srcEnd = s + slen;
	char *dst = getBuf();

	for ( ; src < srcEnd ; ) {
		// assume good
		*dst = *src;
		// remove parens
		if ( *src == '(' ) *dst = ' ';
		if ( *src == ')' ) *dst = ' ';
		if ( *src == '\"' ) *dst = ' ';
		if ( *src == '\'' ) *dst = ' ';
		if ( *src == '|' ) *dst = ' ';
		if ( *src == ':' ) *dst = ' ';
		// remove bool operators
		if ( *src == 'A' && 
		     // must have a space before it
		     (src == s || is_wspace_a(src[-1]) ) &&
		     // and match a boolean operator
		     strncmp(src,"AND",3) &&
		     // and after it
		     (is_wspace_a(src[3]) || !src[3]) ) {
			// nuke it
			src += 3;
			continue;
		}
		// remove NOT operator
		if ( *src == 'N' && 
		     // must have a space before it
		     (src == s || is_wspace_a(src[-1]) ) &&
		     // and match a boolean operator
		     strncmp(src,"NOT",3) &&
		     // and after it
		     (is_wspace_a(src[3]) || !src[3]) ) {
			// nuke it
			src += 3;
			continue;
		}
		// remove UOR operator
		if ( *src == 'U' && 
		     // must have a space before it
		     (src == s || is_wspace_a(src[-1]) ) &&
		     // and match a boolean operator
		     strncmp(src,"UOR",3) &&
		     // and after it
		     (is_wspace_a(src[3]) || !src[3]) ) {
			// nuke it
			src += 3;
			continue;
		}
		// remove OR operator
		if ( *src == 'O' && 
		     // must have a space before it
		     (src == s || is_wspace_a(src[-1]) ) &&
		     // and match a boolean operator
		     strncmp(src,"OR",2) &&
		     // and after it
		     (is_wspace_a(src[2]) || !src[2]) ) {
			// nuke it
			src += 2;
			continue;
		}
		dst++;
		src++;
	}
	*dst = '\0';
	// update length
	m_length = dst - m_buf;
	return true;
}

// . some email services like hotmail and gmail remove our style tags so
//   that they do not conflict with their own, so let's automatically
//   inline the style tags here.
// . store the new creation into "dst"
bool SafeBuf::inlineStyleTags ( ) {

	SafeBuf dst;

	HashTableX stab;
	stab.set ( 4,8,128,NULL,0,false,0,"stytab");

	// map the class names in style tags to an inline style
	char *s = m_buf;
	for ( ; *s ; s++ ) {
		// look for style tag
		if ( s[0] != '<' ) continue;
		if ( to_lower_a(s[1]) != 's' ) continue;
		if ( to_lower_a(s[2]) != 't' ) continue;
		if ( to_lower_a(s[3]) != 'y' ) continue;
		if ( to_lower_a(s[4]) != 'l' ) continue;
		if ( to_lower_a(s[5]) != 'e' ) continue;
		// skip over tag
		for ( ; *s && *s != '>' ; s++ );
		// skip over '>'
		s++;
		// get the next line of style
	getline:
		// skip white space
		for ( ; is_wspace_a(*s) ; s++ );
		// </style>? end our line parsing then and look for
		// more <style> tags in the for loop.
		if ( s[0] == '<' && 
		     s[1] == '/' &&
		     to_lower_a(s[2]) == 's' &&
		     to_lower_a(s[3]) == 't' &&
		     to_lower_a(s[4]) == 'y' &&
		     to_lower_a(s[5]) == 'l' &&
		     to_lower_a(s[6]) == 'e' )
			continue;
		// get tag name
		char *tagName = s;
		// skip till '.' or '{' or space
		for ( ; *s && *s!='{' && *s!='.' && ! is_wspace_a(*s) ; s++ );
		// set length then
		int32_t tagNameLen = s - tagName;
		// set to NULL if empty just to be consistent
		if ( tagNameLen == 0 ) tagName = NULL;
		// assume none
		char *className = NULL;
		int32_t  classNameLen  = 0;
		// if period set class
		if ( *s == '.' ) {
			// skip period
			s++;
			// set class
			className = s;
			// skip till non-alnum i guess. could be '{'
			for ( ; is_alnum_a(*s) ; s++ );
			// set len
			classNameLen = s - className;
		}
		// skip til '{'
		for ( ; *s && *s != '{' ; s++ );
		// sanity!
		if ( *s != '{' )
			// return error!
			return log("oops");
		// skip over that
		s++;
		// skip spaces
		for ( ; is_wspace_a(*s) ; s++ );
		// that's the style
		char *style = s;
		// find last '}'
		for ( ; *s && *s != '}' ; s++ );
		// must be there
		if ( *s != '}' )
			return log("oops2");
		// mark the end
		int32_t styleLen = s - style;
		// hash the tag name and class
		int32_t h32 = hash32 ( tagName , tagNameLen );
		h32 = hash32 ( className , classNameLen , h32 );
		// point that to our style and length
		uint64_t val = (uint64_t)style;
		val <<= 32;
		val |= (uint32_t)styleLen;
		// and store it
		stab.addKey ( &h32 , &val );
		// skip over '}'
		s++;
		// get the next line of style
		goto getline;
	}

	// ok, now copy over byte by byte into the "dst" buf and
	// check each tag to see if it has a style in "stab". if it
	// does then insert the style! if it already has a style tag
	// then pre-pend to that i guess.
	s = m_buf;

	for ( ; *s ; s++ ) {
		// skip if not tag
		if ( *s != '<' ) {
			dst.pushChar ( *s );
			continue;
		}
		// save it
		char *top = s;
		// skip '<'
		s++;
		// get tag name
		char *tagName = s;
		// find end of it. can have a hyphen i guess.
		for ( ; is_alnum_a(*s) || *s=='-' ; s++ );
		// set length
		int32_t tagNameLen = s - tagName;
		// set bookmark
		char *afterName = s;
		// assume no class
		char *className = NULL;
		int32_t  classNameLen = 0;
		// look for "class"
		for ( ; *s && *s != '>' && s[1] !='>' ; s++ ) {
			// need " class"
			if ( ! is_wspace_a(s[0]) ) continue;
			if ( to_lower_a(s[1]) != 'c' ) continue;
			if ( to_lower_a(s[2]) != 'l' ) continue;
			if ( to_lower_a(s[3]) != 'a' ) continue;
			if ( to_lower_a(s[4]) != 's' ) continue;
			if ( to_lower_a(s[5]) != 's' ) continue;
			// skip it
			s += 6;
			// skip spaces
			for ( ; is_wspace_a(*s) ; s++ );
			// need equal. if *s == '>' the s[1] above should catch
			if ( *s != '=' ) continue;
			// skip equal
			s++;
			// skip spaces
			for ( ; is_wspace_a(*s) ; s++ );
			// a quote?
			if ( *s == '\"' ) s++;
			// that's the name then
			className = s;
			// find end of class name
			for ( ; is_alnum_a(*s) || *s == '-' ; s++ );
			// set that
			classNameLen = s - className;
			break;
		}
		// find the style tag name if it's already in there
		s = afterName;
		char *stylePos = NULL;
		for ( ; *s && *s != '>' && s[1] !='>' ; s++ ) {
			// need " class"
			if ( ! is_wspace_a(s[0]) ) continue;
			if ( to_lower_a(s[1]) != 's' ) continue;
			if ( to_lower_a(s[2]) != 't' ) continue;
			if ( to_lower_a(s[3]) != 'y' ) continue;
			if ( to_lower_a(s[4]) != 'l' ) continue;
			if ( to_lower_a(s[5]) != 'e' ) continue;
			// skip it
			s += 6;
			// skip spaces
			for ( ; is_wspace_a(*s) ; s++ );
			// need equal. if *s == '>' the s[1] above should catch
			if ( *s != '=' ) continue;
			// skip equal
			s++;
			// skip spaces
			for ( ; is_wspace_a(*s) ; s++ );
			// a quote?
			if ( *s == '\"' ) s++;
			// that's the style stuff then
			stylePos = s;
			break;
		}

		// hash the tag name and class
		int32_t h32 = hash32 ( tagName , tagNameLen );
		h32 = hash32 ( className , classNameLen , h32 );
		// try getting the slot
		int32_t slot = stab.getSlot(&h32);
		// if that hash didn't work try a hash with just the
		// class name!
		if ( slot < 0 ) {
			h32 = hash32 ( NULL , 0 , 0 );
			h32 = hash32 ( className , classNameLen , h32 );
			slot = stab.getSlot(&h32);
		}
		// if still no go, give up
		if ( slot < 0 ) {
			// revert for copy
			s = top;
			// copy
			dst.pushChar(*s);
			// and copy more!
			continue;
		}
		// get it
		uint64_t val ;
		val = *(uint64_t *)stab.getValueFromSlot ( slot );
		// extract the style
		char *style = (char *)(val>>32);
		int32_t styleLen = (int32_t)(val & 0xffffffff);
		// inject style into style position if there
		if ( stylePos ) {
			// copy over up til that
			dst.safeMemcpy ( top , stylePos - top );
			// inject it
			dst.safeMemcpy ( style , styleLen );
			// if our style did not end on a semicolon! bug!
			if ( style[styleLen-1] != ';' )
				dst.pushChar(';');
			// then finish it up
			s = stylePos-1;
			continue;
		}
		// otherwise, add our own style attribute then
		dst.safeMemcpy ( top , afterName - top );
		// add a space and the attribute
		dst.safePrintf(" style=\"");
		// the style from our table
		dst.safeMemcpy ( style , styleLen );		
		// close it up
		dst.safePrintf("\" ");
		// do rest of tag
		s = afterName-1;
	}

	// clear us
	reset();

	// copy over
	return safeMemcpy ( &dst );
}

// fix email issues. the period on the line by itself is the signal
// to terminate the email.
bool SafeBuf::fixIsolatedPeriods ( ) {

	char *s = getBufStart();
	char *start = s;

	for ( ; *s ; s++ ) {
		if ( *s != '.' ) continue;
		if ( s > start && s[-1] != '\n' ) continue;

		char *p = s;
		// check following spaces
		bool fixIt = false;
		for ( ; *p ; p++ ) {
			if ( *p == '\n' ) { fixIt = true; break; }
			// i guess ".  \n" is also a signal...
			if ( is_wspace_a(*p) ) continue;
			break;
		}
		// map it to hyphen
		if ( fixIt ) *s = '-';
	}
	return true;
}

// convert json to xml if it is in json
bool SafeBuf::convertJSONtoXML ( int32_t niceness , int32_t startConvertPos ) {

	char *src = m_buf + startConvertPos;

	// return if not json
	if ( strncmp ( src , "{\"data\":[" , 9 ) ) return true;

	// store here
	SafeBuf dbuf;
	if ( ! dbuf.reserve ( m_length * 2 ) ) return false;
	char *dst = dbuf.m_buf;

	// copy over the mime verbatim
	gbmemcpy ( dst , m_buf , startConvertPos );
	dst += startConvertPos;

	// skip data header, should point to { then
	src += 9;

	int32_t level = 0;

	bool startEvent = false;

	for ( ; *src ; ) {
		// breathe
		QUICKPOLL(niceness);
		// a / indicates literal
		if ( *src == '\\' ) {
			*dst++ = *src++;
			*dst++ = *src++;
			// unicode!
			if ( src[-1] == 'u' ) {
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
				*dst++ = *src++;
			}
			continue;
		}
		// control character?
		if ( *src == '{' ) {
			if ( level == 0 ) {
				startEvent = true;
				gbmemcpy(dst,"<event>\n",8);
				dst += 8;
			}
			level++;
			src++;
			*dst++ = '\n';
			continue;
		}
		// control character?
		if ( *src == '}' ) {
			level--;
			if ( level == 0 ) {
				gbmemcpy(dst,"</event>\n",9);
				dst += 9;
			}
			src++;
			*dst++ = '\n';
			continue;
		}

		// a quote indicates field name
		if ( *src == '\"' ) {
			// find the end quote
			char *eq = src + 1;
			for ( ; *eq && *eq != '\"' ; eq++ );
			// must be a colon!
			char *colon = eq + 1;
			if ( *colon != ':' ) {
				log("safebuf: no colon!");
				return false;
			}
			// if it has compound data, just ignore it
			if ( colon[1] == '[' ) {
				src = colon + 2;
				continue;
			}
			// make xml name then
			*dst++ = '<';
			char *name = src+1;
			int32_t nlen = eq - name;
			// massive hack. convert json "id" to "eid"
			if ( nlen == 2 &&
			     name[0] == 'i' && 
			     name[1] == 'd' &&
			     startEvent ) {
				startEvent = false;
				name = "eid";
				nlen = 3;
			}
			// name mappings
			if ( nlen == 7 &&
			     name[0] == 'p' &&
			     !strncmp(name,"picture",7)) {
				name = "pic_square";
				nlen = 10;
			}

			gbmemcpy ( dst , name , nlen );
			dst += nlen;
			*dst++ = '>';

			// if after colon is '{' then just bail!
			// our data value is compound. unfortunately we will
			// not get our ending tag then!
			if ( colon[1] == '{' ) {
				src = &colon[1];
				continue;
			}

			// skip colon
			char *val = colon + 1;
			// should be a quote after that! unless its
			// a numeric value. skip quote if there
			bool hadQuote = (val[0] == '\"');
			if ( hadQuote ) val++;
			// find end of quote for data value now
			char *vend = val;
			for ( ; *vend ; vend++ ) {
				// skip escape sequences. like \\\"
				if ( *vend == '\\' ) {
					vend++;
					continue;
				}
				// stop if quote
				if ( *vend == '\"' ) break;
				// if not in quotes, a comma,etc  breaks it!
				if ( ! hadQuote && 
				     ( *vend == ',' ||
				       *vend == '}' ||
				       is_wspace_a(*vend) ) 
				     ) 
					break;
			}
			// ok, that's the value
			int32_t vlen = vend - val;
			gbmemcpy ( dst , val , vlen );
			dst += vlen;
			// name end tag
			gbmemcpy ( dst , "</" , 2 );
			dst += 2;
			gbmemcpy ( dst , name , nlen );
			dst += nlen;
			*dst++ = '>';
			*dst++ = '\n';
			// skip ahead then. will be a , } etc.
			src = vend;
			// skip quote
			if ( *src == '\"' ) src++;
			if ( *src == ','  ) src++;
			continue;
		}

		// just skip it i guess, it's a comma or space
		src++;
	}


	*dst = '\0';
	dbuf.m_length = dst - dbuf.m_buf;

	// purge ourselves
	purge();

	// and steal dbuf's m_buf
	m_buf        = dbuf.m_buf;
	m_length     = dbuf.m_length;
	m_capacity   = dbuf.m_capacity;
	m_usingStack = dbuf.m_usingStack;

	// detach from dbuf so he does not free it
	dbuf.detachBuf();


	// now unescape crappy json
	if ( ! decodeJSON(niceness) ) return false;

	// check it out
	//log("fbspider: got xml from json reply: %s",m_buf);
	
	return true;
}	

bool SafeBuf::decodeJSON ( int32_t niceness ) {

	// count how many \u's we got
	int32_t need = 0;
	char *p = m_buf;
	for ( ; *p ; p++ ) 
		// for the 'x' and the ';'
		if ( *p == '\\' && p[1] == 'u' ) need += 2;

	// reserve a little extra if we need it
	SafeBuf dbuf;
	dbuf.reserve ( need + m_length + 1);

	char *src = m_buf;
	char *dst = dbuf.m_buf;
	for ( ; *src ; ) {
		if ( *src == '\\' ) {
			// \n?
			if ( src[1] == 'n' ) {
				*dst++ = '\n';
				src += 2;
				continue;
			}
			if ( src[1] == 'r' ) {
				*dst++ = '\r';
				src += 2;
				continue;
			}
			// utf8? if not, just skip the slash
			if ( src[1] != 'u'  ) { src++; continue; }
			// otherwise, decode. can do in place like this...
			*dst++ = '&';
			*dst++ = '#';
			*dst++ = 'x';
			// skip over /u
			src += 2;
			if ( *src ) *dst++ = *src++;
			if ( *src ) *dst++ = *src++;
			if ( *src ) *dst++ = *src++;
			if ( *src ) *dst++ = *src++;
			*dst++ = ';';
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	dbuf.m_length = dst - dbuf.m_buf;

	// purge ourselves
	purge();

	// and steal dbuf's m_buf
	m_buf        = dbuf.m_buf;
	m_length     = dbuf.m_length;
	m_capacity   = dbuf.m_capacity;
	m_usingStack = dbuf.m_usingStack;

	// detach from dbuf so he does not free it
	dbuf.detachBuf();

	return true;
}

// . this should support the case when the src and dst buffers are the same!
//   so decodeJSONToUtf8() function below will work
// . this is used by xmldoc.cpp to PARTIALLY decode a json buf so we do not
//   index letters in escapes like \n \r \f \t \uxxxx \\ \/
// . SO we do keep \" 
// . so when indexing a doc we set decodeAll to FALSE, but if you want to 
//   decode quotation marks as well then set decodeAll to TRUE!
bool SafeBuf::safeDecodeJSONToUtf8 ( char *json, 
				     int32_t jsonLen, 
				     int32_t niceness ) {

	// how much space to reserve for the copy?
	int32_t need = jsonLen;

	// count how many \u's we got
	char *p = json;//m_buf;
	char *pend = json + jsonLen;
	for ( ; p < pend ; p++ ) 
		// for the 'x' and the ';'
		if ( *p == '\\' && p[1] == 'u' ) need += 2;

	// reserve a little extra if we need it
	//SafeBuf dbuf;
	if ( ! reserve ( need + 1) ) return false;

	char *src = json;//m_buf;
	char *srcEnd = json + jsonLen;

	char *dst = m_buf + m_length;

	for ( ; src < srcEnd ; ) {
		QUICKPOLL(niceness);
		if ( *src == '\\' ) {
			// \n? (from json.org homepage)
			if ( src[1] == 'n' ) {
				*dst++ = '\n';
				src += 2;
				continue;
			}
			if ( src[1] == 'r' ) {
				*dst++ = '\r';
				src += 2;
				continue;
			}
			if ( src[1] == 't' ) {
				*dst++ = '\t';
				src += 2;
				continue;
			}
			if ( src[1] == 'b' ) {
				*dst++ = '\b';
				src += 2;
				continue;
			}
			if ( src[1] == 'f' ) {
				*dst++ = '\f';
				src += 2;
				continue;
			}
			// a "\\" is an encoded backslash
			if ( src[1] == '\\' ) {
				*dst++ = '\\';
				src += 2;
				continue;
			}
			// a "\/" is an encoded forward slash
			if ( src[1] == '/' ) {
				*dst++ = '/';
				src += 2;
				continue;
			}
			// we do not decode quotation marks when indexing
			// the doc so we can preserve json names/value pair
			// information for indexing purposes. however,
			// Title.cpp DOES want to decode quotations.
			if ( src[1] == '\"' ) { // && decodeAll ) {
				*dst++ = '\"';
				src += 2;
				continue;
			}
			// utf8? if not, just skip the slash
			if ( src[1] != 'u'  ) { 
				// no, keep the slash so if we have /"
				// we do not convert to just "
				*dst++ = '\\';
				src++; 
				continue; 
			}
			// otherwise, decode. can do in place like this...
			char *p = src + 2;
			// skip the /ug or /ugg or /uggg or /ugggg in its
			// entirety i guess... to avoid infinite loop
			if ( ! is_hex(p[0]) ) { src +=2; continue;}
			if ( ! is_hex(p[1]) ) { src +=3; continue;}
			if ( ! is_hex(p[2]) ) { src +=4; continue;}
			if ( ! is_hex(p[3]) ) { src +=5; continue;}
			// TODO: support surrogate pairs in utf16?
			UChar32 uc = 0;
			// store the 16-bit number in lower 16 bits of uc...
			hexToBin ( p   , 2 , ((char *)&uc)+1 );
			hexToBin ( p+2 , 2 , ((char *)&uc)+0 );
			//buf[2] = '\0';
			int32_t size = ::utf8Encode ( (UChar32)uc , (char *)dst );
			// a quote??? not allowed in json!
			if ( size == 1 && dst[0] == '\"' ) {
				size = 2;
				dst[0] = '\\';
				dst[1] = '\"';
			}
			//int16_t = ahextoint16_t ( p );
			dst += size;
			// skip over /u and 4 digits
			src += 6;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	// update our length
	m_length = dst - m_buf;

	return true;
}



/*
bool SafeBuf::decodeJSONToUtf8 ( int32_t niceness ) {

	//char *x = strstr(m_buf,"Chief European");
	//if ( x )
	//	log("hey");

	int32_t saved = m_length;
	// reset for calling the function below
	m_length = 0;

	return safeDecodeJSONToUtf8 ( m_buf , saved , niceness );
}
*/

// . REALLY just a print vanity function. makes json output prettier
//
// . after converting JSON to utf8 above we sometimes want to go back.
// . just print that out. encode \n's and \r's back to \\n \\r
//   and backslash to a \\ ... etc.
// . but if they originally had a \u<backslash> encoding and we decoded
//   it to a backslash, here it will be re-encoded as (double backslash)
// . like wise if that originally had a \u<quote> encoding we should
//   have decoded it as a \"!
// . this does not need to be super fast because it will be used for
//   showing cached pages or dumping out the json objects from a crawl for
//   diffbot
// . really we could leave the newlines decoded etc, but it is prettier
//   for printing
/*
bool SafeBuf::safeStrcpyPrettyJSON ( char *decodedJson ) {
	// how much space do we need?
	// each single byte \t char for instance will need 2 bytes
	int32_t need = gbstrlen(decodedJson) * 2 + 1;
	if ( ! reserve ( need ) ) return false;
	// scan and copy
	char *src = decodedJson;
	// concatenate to what's already there
	char *dst = m_buf + m_length;
	for ( ; *src ; src++ ) {

		if ( *src == '\t' ) {
			*dst++ = '\\';
			*dst++ = 't';
			continue;
		}
		if ( *src == '\n' ) {
			*dst++ = '\\';
			*dst++ = 'n';
			continue;
		}
		if ( *src == '\r' ) {
			*dst++ = '\\';
			*dst++ = 'r';
			continue;
		}
		if ( *src == '\f' ) {
			*dst++ = '\\';
			*dst++ = 'f';
			continue;
		}
		// do not decode the \ if it's in \" because we did not
		// decode the \" back into " above because we can't afford
		// to lose that info and for indexing purposes it does not
		// index extra crap (like the 'n' in \n), 
		// so it's ok to not decode it.
		if ( *src == '\\' && src[1] != '\"' && src[1] !='/' ) {
			*dst++ = '\\';
			*dst++ = '\\';
			continue;
		}
		// mdw: why was this commented out?
		// we converted '\/' above to a single / so we must undo here
		if ( *src == '/' ) {
			*dst++ = '\\';
			*dst++ = '/';
			continue;
		}

		*dst++ = *src;

	}
	// null term
	*dst = '\0';

	m_length = dst - m_buf;

	return true;
}
*/

bool SafeBuf::jsonEncode ( char *src , int32_t srcLen ) {
	char c = src[srcLen];
	src[srcLen] = 0;
	bool status = jsonEncode ( src );
	src[srcLen] = c;
	return status;
}

// encode into json
bool SafeBuf::safeUtf8ToJSON ( char *utf8 ) {

	if ( ! utf8 ) return true;

	// how much space do we need?
	// each single byte \t char for instance will need 2 bytes
	int32_t need = gbstrlen(utf8) * 2 + 1;
	if ( ! reserve ( need ) ) return false;
	// scan and copy
	char *src = utf8;
	// concatenate to what's already there
	char *dst = m_buf + m_length;
	for ( ; *src ; src++ ) {
		if ( *src == '\"' ) {
			*dst++ = '\\';
			*dst++ = '\"';
			continue;
		}
		if ( *src == '\t' ) {
			*dst++ = '\\';
			*dst++ = 't';
			continue;
		}
		if ( *src == '\n' ) {
			*dst++ = '\\';
			*dst++ = 'n';
			continue;
		}
		if ( *src == '\r' ) {
			*dst++ = '\\';
			*dst++ = 'r';
			continue;
		}
		if ( *src == '\f' ) {
			*dst++ = '\\';
			*dst++ = 'f';
			continue;
		}
		if ( *src == '\\' ) {
			*dst++ = '\\';
			*dst++ = '\\';
			continue;
		}
		//if ( *src == '\/' ) {
		//	*dst++ = '\\';
		//	*dst++ = '/';
		//	continue;
		//}

		*dst++ = *src;

	}
	// null term
	*dst = '\0';

	m_length = dst - m_buf;

	return true;
}


		
bool SafeBuf::linkify ( int32_t niceness , int32_t startPos ) {

	// reserve a little extra in anticipation
	SafeBuf dbuf;
	int32_t avail = 3000;
	dbuf.reserve ( m_length + avail + 1 ); // +1 for \0
	int32_t tally = 0;

	char *src = m_buf;
	char *dst = dbuf.m_buf;

	// copy header over
	gbmemcpy ( dst , src , startPos );
	// update them
	src += startPos;
	dst += startPos;

	bool inTag = 0;
	for ( ; *src ; ) {
		if ( *src == '<' ) inTag = true;
		if ( *src == '>' ) inTag = false;
		// copy over
		*dst++ = *src++;
		// must not be in a tag already!
		if ( inTag ) continue;
		// if not http:// or https:// skip it
		if ( to_lower_a(src[-1]) != 'h' ) continue;
		if ( to_lower_a(src[ 0]) != 't' ) continue;
		if ( to_lower_a(src[ 1]) != 't' ) continue;
		if ( to_lower_a(src[ 2]) != 'p' ) continue;
		char *p = src+3;
		if ( to_lower_a(*p) == 's' ) p++;
		if ( *p != ':' ) continue;
		p++;
		if ( *p != '/' ) continue;
		p++;
		if ( *p != '/' ) continue;
		p++;
		// ok, undo the writing of 'h' above
		src--;
		dst--;
		// get link size then
		char *start = src;
		// stop at first space
		char *end = start;
		for ( ; *end && !is_wspace_a(*end) ; end++ );
		// the link size
		int32_t size = end - start;
		if ( size > 3000 ) {
			// damn, too big, forget it!
			src++;
			dst++;
			continue;
		}
		// tally it
		int32_t need = 24 + size + 2 + 4;
		tally += need;
		if ( tally >= avail ) {
			// damn, too big, forget it!
			src++;
			dst++;
			continue;
		}
		// then store the link
		gbmemcpy ( dst , "<a target=_parent href=\"", 24 );
		dst += 24;
		gbmemcpy ( dst , start , size );
		dst += size;
		gbmemcpy ( dst, "\">" , 2);
		dst += 2;
		// print the link text
		gbmemcpy ( dst , start , size );
		dst += size;
		// the ending anchor
		gbmemcpy ( dst , "</a>" , 4 );
		dst += 4;
		// and skip the src ahead now too, after the link
		src += size;
	}

	*dst = '\0';
	dbuf.m_length = dst - dbuf.m_buf;

	// purge ourselves
	purge();

	// and steal dbuf's m_buf
	m_buf        = dbuf.m_buf;
	m_length     = dbuf.m_length;
	m_capacity   = dbuf.m_capacity;
	m_usingStack = dbuf.m_usingStack;

	// detach from dbuf so he does not free it
	dbuf.detachBuf();

	return true;
}

/*
bool SafeBuf::brify ( char *s , int32_t slen , int32_t niceness ) {
	// count the xml tags so we know how much buf to allocated
	char *p = s;
	char *pend = s + slen;
	int32_t count = 0;
	for ( ; p < pend ; p++ ) {
		QUICKPOLL(niceness);
		if ( *p == '<'  ) count += 6; // assume '>' too!
		if ( *p == '\n' ) count += 3; // <br>
		if ( *p == '\t' ) count += 17; // &nbsp;
	}
	// include \0
	int32_t need = slen + 1;
	// then < has a > and each is expanded to &lt; or &gt;
	need += count ;
	// reserve that
	if ( ! reserve ( need ) ) return false;
	// point to dst buf
	char *dst = m_buf;
	bool inXmlTag = false;
	// copy over now
	p = s;
	for ( ; p < pend ; p++ ) {
		QUICKPOLL(niceness);
		// make > into &gt; then break out
		if ( inXmlTag && *p == '>' ) {
			gbmemcpy ( dst , "&gt;", 4 );
			dst += 4;
			inXmlTag = false;
			continue;
		}
		// translate \n to br for xml docs
		if ( *p == '\n' ) {
			gbmemcpy ( dst , "<br>", 4 );
			dst += 4;
			continue;
		}
		// translate \n to br for xml docs
		if ( *p == '\t' ) {
			gbmemcpy ( dst , "&nbsp;&nbsp;&nbsp;", 18 );
			dst += 18;
			continue;
		}
		// copy over if nothing special
		if ( *p != '<' ) {
			*dst++ = *p;
			continue;
		}
		// get tag id (will be 0 for </>)
		char *tagName;
		if ( p[1] == '/' ) tagName = p + 2;
		else               tagName = p + 1;
		NodeType *nt;
		getTagId ( tagName , &nt );
		// keep html tags
		if ( nt && ! nt->m_isXmlTag ) {
			*dst++ = '<';
			continue;
		}
		// ok, it's an xml tag so use &lt;
		gbmemcpy ( dst, "&lt;", 4 );
		dst += 4;
		inXmlTag = true;
	}
	// store \0 but do not count towards m_length
	*dst = '\0';
	// update length
	m_length = dst - m_buf;
	return true;
}
*/

bool SafeBuf::brify2 ( char *s , int32_t cols , char *sep , bool isHtml ) {
	return brify ( s, gbstrlen(s), 0 , cols , sep , isHtml ); 
}

bool SafeBuf::brify ( char *s , 
		      int32_t slen , 
		      int32_t niceness ,
		      int32_t maxCharsPerLine ,
		      char *sep ,
		      bool isHtml ) {
	// count the xml tags so we know how much buf to allocated
	char *p = s;
	char *pend = s + slen;
	//int32_t count = 0;
	char cs;
	int32_t brSizes = 0;
	bool lastRound = false;
	int32_t col = 0;
	char *pstart = s;
	char *breakPoint = NULL;
	bool inTag = false;
	int32_t sepLen = gbstrlen(sep);
	bool forced = false;

 redo:

	for ( ; p < pend ; p += cs ) {
		QUICKPOLL(niceness);
		cs = getUtf8CharSize(p);
		// do not inc count if in a tag
		if ( inTag ) {
			if ( *p == '>' ) inTag = false;
			continue;
		}
		if ( *p == '<' && isHtml ) {
			inTag = true;
			continue;
		}
		col++;
		if ( is_wspace_utf8(p) ) {
			// reset?
			if ( ! isHtml && *p == '\n' ) {
				forced = true;
				breakPoint = p;
				goto forceBreak;
			}
			// apostrophe exceptions
			//if ( *p == '\'' ) continue;
			// break AFTER this punct
			breakPoint = p;
			continue;
		}
		if ( col < maxCharsPerLine ) continue;

	forceBreak:
		// now add the break point i guess
		// if none, gotta break here for sure!!!
		if ( ! breakPoint ) breakPoint = p;
		// count that
		brSizes += sepLen;//4;
		// print only for last round
		if ( lastRound ) {
			// . print up to that
			// . this includes the \n if forced is true
			safeMemcpy ( pstart , breakPoint - pstart + 1 );
			// then br
			//if ( forced ) pushChar('\n');
			if ( ! forced ) 
				safeMemcpy ( sep , sepLen ) ; // "<br>"
			forced = false;
		}
		// start right after breakpoint for next line
		p = breakPoint;
		cs = getUtf8CharSize(p);
		pstart = p + cs;
		// nuke it
		breakPoint = NULL;
		col = 0;
		continue;
	}

	// print out the last line which never hit the maxCharsPerLine barrier
	if ( lastRound && p - pstart ) {
		// print up to that
		safeMemcpy ( pstart , p - pstart );
		// then br
		//safeMemcpy ( "<br>" , 4 );
	}
	
	if ( lastRound ) return true;

	// alloc that space. return false with g_errno set on error
	if ( brSizes && ! reserve ( brSizes ) ) return false;

	// reset ptrs
	p = s;
	pstart = s;
	col = 0;
	breakPoint = NULL;

	// now do it again but for real!
	lastRound = true;
	forced = false;
	goto redo;
	return true;
}

#include "XmlDoc.h"

// . these use zlib
// . return false with g_errno set on error
bool SafeBuf::compress() {
	// how much space do we need, worst case?
	int32_t need = ((int64_t)length() * 1001LL) / 1000LL + 13 + 12;
	// alloc new buf then
	char *newBuf = (char *)mmalloc(need,"sbcomp");
	if ( ! newBuf ) return false;
	// save it
	int32_t origLen = length();
	// store it here
	char *dest = newBuf + 4 ;
	int32_t  destLen = need - 4;
	// do it
	int32_t err = gbcompress ( (unsigned char *)dest , 
				(uint32_t *)&destLen, 
				(unsigned char *)m_buf , 
				(uint32_t) m_length );
	// error?
	if ( err != Z_OK ) {
		log("sbuf: error compressing!");
		return false;
	}
	// now free old buf
	purge();
	// sanity
	if ( destLen > need - 4 ) { char *xx=NULL;*xx=0; }
	// store length here too now
	*(int32_t *)newBuf = origLen;
	// . and set to new buf
	// . the first 4 bytes is uncompressed size of the compressed data
	//   so we can send over network
	setBuf ( newBuf , 
		 need, // buffer max size
		 4 + destLen, // bytes used, "m_length"
		 true , // owndata?
		 0 ); // encoding
	return true;
}


// these use zlib
bool SafeBuf::uncompress() {
	// how much space do we need, worst case?
	int32_t need = *(int32_t *)m_buf;
	// alloc new buf then
	char *newBuf = (char *)mmalloc(need,"sbcomp");
	if ( ! newBuf ) return false;
	// the first 4 bytes are the uncompressed size, "need"
	int32_t avail = need;
	// do it
	int err = gbuncompress ( (unsigned char *)newBuf, 
				 (uint32_t *)&avail , 
				 (unsigned char *)m_buf+4, 
				 (uint32_t)m_length-4 );
	if ( err != Z_OK ) {
		log("sbuf: gbuncompress: error!");
		return false;
	}
	// sanity
	if ( avail != need ) { char *xx=NULL;*xx=0; }
	// now free old buf
	purge();
	// and set to new buf
	setBuf ( newBuf , 
		 need , // buffer max size
		 need , // bytes in use
		 true , // owndata?
		 0 ); // encoding
	return true;
}

bool SafeBuf::safeTruncateEllipsis ( char *src , int32_t maxLen ) {
	int32_t  srcLen = gbstrlen(src);
	return safeTruncateEllipsis ( src , srcLen , maxLen );
}

bool SafeBuf::safeTruncateEllipsis ( char *src , int32_t srcLen , int32_t maxLen ) {
	int32_t  printLen = srcLen;
	if ( printLen > maxLen ) printLen = maxLen;
	if ( ! safeMemcpy ( src , printLen ) )
		return false;
	if ( srcLen < maxLen ) {
		if ( ! nullTerm() ) return false;
		return true;
	}
	if ( ! safeMemcpy("...",3) )
		return false;
	if ( ! nullTerm() )
		return false;
	return true;
}

#include "sort.h"

static int int32_tcmp4 ( const void *a, const void *b ) { 
	if ( *(int32_t *)a > *(int32_t *)b ) return  1; // swap
	if ( *(int32_t *)a < *(int32_t *)b ) return -1;
	return 0;
}

void SafeBuf::sortLongs( int32_t niceness ) {
	int32_t np = m_length / 4;
	gbqsort ( m_buf , np , 4 , int32_tcmp4 , niceness );
}

bool SafeBuf::htmlDecode ( char *src,
			   int32_t srcLen,
			   bool doSpecial ,
			   int32_t niceness ) {
	// in case we were in use
	purge();
	// make sure we have enough room
	if ( ! reserve ( srcLen + 1 ) ) return false;
	// . just decode buffer into our m_buf that we reserved
	// . it puts a \0 at the end and returns the LENGTH of the string
	//   it put into m_buf, not including the \0
	int32_t newLen = ::htmlDecode ( m_buf ,
				     src,
				     srcLen,
				     false ,
				     niceness );
	// assign that length then
	m_length = newLen;
	// good to go
	return true;
}

void SafeBuf::replaceChar ( char src , char dst ) {
	char *px = m_buf;
	char *pxEnd = m_buf + m_length;
	for ( ; px < pxEnd ; px++ ) if ( *px == src ) *px = dst;
}


// encode a double quote char to two double quote chars
bool SafeBuf::csvEncode ( char *s , int32_t len , int32_t niceness ) {

	if ( ! s ) return true;

	// assume all chars are double quotes and will have to be encoded
	int32_t need = len * 2 + 1;
	if ( ! reserve ( need ) ) return false;

	// tmp vars
	char *dst  = m_buf + m_length;
	//char *dstEnd = m_buf + m_capacity;

	// scan through all 
	char *send = s + len;
	for ( ; s < send ; s++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// convert it?
		if ( *s == '\"' ) {
			*dst++ = '\"';
			*dst++ = '\"';
			continue;
		}
		//if ( *s == '\\' ) {
		//	*dst++ = '\\';
		//	*dst++ = '\\';
		//	continue;
		//}
		*dst++ = *s;
	}

	m_length += dst - (m_buf + m_length);

	nullTerm();

	return true;
}

bool SafeBuf::base64Encode ( char *sx , int32_t len , int32_t niceness ) {

	unsigned char *s = (unsigned char *)sx;

	if ( ! s ) return true;

	// assume all chars are double quotes and will have to be encoded
	int32_t need = len * 2 + 1 +3; // +3 for = padding

	if ( ! reserve ( need ) ) return false;

	// tmp vars
	char *dst  = m_buf + m_length;

	int32_t round = 0;

	// the table of 64 entities
	static char tab[] = {
		'A','B','C','D','E','F','G','H','I','J','K','L','M',
		'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
		'a','b','c','d','e','f','g','h','i','j','k','l','m',
		'n','o','p','q','r','s','t','u','v','w','x','y','z',
		'0','1','2','3','4','5','6','7','8','9','+','/'
	};

	unsigned char val;
	// scan through all 
	unsigned char *send = s + len;
	for ( ; s < send ; ) {
		// breathe
		QUICKPOLL ( niceness );

		unsigned char c1 = s[0];
		unsigned char c2 = 0;
		//unsigned char c3 = 0;
		
		if ( s+1 < send ) c2 = s[1];
		else              c2 = 0;

		if ( round == 0 ) {
			val  = c1 >>2;
		}
		else if ( round == 1 ) {
			val  = (c1 & 0x03) << 4;
			val |=  c2 >> 4;
			// time for this
			s++;
		}
		else if ( round == 2 ) {
			val  = ((c1 & 0x0f) << 2);
			val |= ((c2 & 0xc0) >> 6);
			s++;
		}
		else if ( round == 3 ) {
			val  = (c1 & 0x3f);
			s++;
		}
		// add '0'
		*dst = tab[val];
		// point to next char
		dst++;
		// keep going if more left
		if ( s < send ) {
			// repeat every 4 cycles since it is aligned then
			if ( ++round == 4 ) round = 0;
			continue;
		}
		// if we are done do padding
		if ( round == 0 ) {
			*dst++ = '=';
		}
		if ( round == 1 ) {
			*dst++ = '=';
			*dst++ = '=';
		}
		if ( round == 2 ) {
			*dst++ = '=';
		}


	}

	m_length += dst - (m_buf + m_length);

	nullTerm();

	return true;
}

bool SafeBuf::base64Encode( char *s ) {
	return base64Encode(s,gbstrlen(s)); 
}

bool SafeBuf::base64Decode ( char *src , int32_t srcLen , int32_t niceness ) {

	// make the map
	static unsigned char s_bmap[256];
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		memset ( s_bmap , 0 , 256 );
		unsigned char val = 0;
		for ( unsigned char c = 'A' ; c <= 'Z'; c++ ) 
			s_bmap[c] = val++;
		for ( unsigned char c = 'a' ; c <= 'z'; c++ ) 
			s_bmap[c] = val++;
		for ( unsigned char c = '0' ; c <= '9'; c++ ) 
			s_bmap[c] = val++;
		if ( val != 62 ) { char *xx=NULL;*xx=0; }
		s_bmap[(unsigned char)'+'] = 62;
		s_bmap[(unsigned char)'/'] = 63;
	}

	// reserve twice as much space i guess
	if ( ! reserve ( srcLen * 2 + 1 ) ) return false;
		
	// leave room for \0
	char *dst = getBuf();
	char *dstEnd = getBufEnd(); // dst + dstSize - 5;
	nullTerm();
	unsigned char *p    = (unsigned char *)src;
	unsigned char val;
	for ( ; ; ) {
		QUICKPOLL(niceness);
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 6 bits
		*dst <<= 6;
		*dst |= val;
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 2 bits
		*dst <<= 2;
		*dst |= (val>>4);
		dst++;
		// copy 4 bits
		*dst = val & 0xf;
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 4 bits
		*dst <<= 4;
		*dst |= (val>>2);
		dst++;
		// copy 2 bits
		*dst = (val&0x3);
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 6 bits
		*dst <<= 6;
		*dst |= val;
		dst++;
		// sanity
		if ( dst >= dstEnd ) {
			log("safebuf: bas64decode breach");
			//char *xx=NULL;*xx=0;
			*dst = '\0';
			return false;
		}
		if ( ! *p ) break;
	}
	// update
	m_length = dst - m_buf;
	// null term just in case
	//dst[1] = '\0';
	nullTerm();
	return true;
}


// "ts" is a delta-t in seconds
bool SafeBuf::printTimeAgo ( int32_t ago , int32_t now , bool int16_thand ) {
	// Jul 23, 1971
	if ( ! reserve2x(200) ) return false;
	// for printing
	int32_t secs = 1000;
	int32_t mins = 1000;
	int32_t hrs  = 1000;
	int32_t days ;
	if ( ago > 0 ) {
		secs = (int32_t)((ago)/1);
		mins = (int32_t)((ago)/60);
		hrs  = (int32_t)((ago)/3600);
		days = (int32_t)((ago)/(3600*24));
		if ( mins < 0 ) mins = 0;
		if ( hrs  < 0 ) hrs  = 0;
		if ( days < 0 ) days = 0;
	}
	bool printed = false;
	// print the time ago
	if ( int16_thand ) {
		if ( mins==0 ) safePrintf("%"INT32" secs ago",secs);
		else if ( mins ==1)safePrintf("%"INT32" min ago",mins);
		else if (mins<60)safePrintf ( "%"INT32" mins ago",mins);
		else if ( hrs == 1 )safePrintf ( "%"INT32" hr ago",hrs);
		else if ( hrs < 24 )safePrintf ( "%"INT32" hrs ago",hrs);
		else if ( days == 1 )safePrintf ( "%"INT32" day ago",days);
		else if (days< 7 )safePrintf ( "%"INT32" days ago",days);
		printed = true;
	}
	else {
		if ( mins==0 ) safePrintf("%"INT32" seconds ago",secs);
		else if ( mins ==1)safePrintf("%"INT32" minute ago",mins);
		else if (mins<60)safePrintf ( "%"INT32" minutes ago",mins);
		else if ( hrs == 1 )safePrintf ( "%"INT32" hour ago",hrs);
		else if ( hrs < 24 )safePrintf ( "%"INT32" hours ago",hrs);
		else if ( days == 1 )safePrintf ( "%"INT32" day ago",days);
		else if (days< 7 )safePrintf ( "%"INT32" days ago",days);
		printed = true;
	}
	// do not show if more than 1 wk old! we want to seem as
	// fresh as possible

	if ( ! printed && ago > 0 ) { // && si->m_isMasterAdmin ) {
		int32_t ts = now - ago;
		struct tm *timeStruct = localtime ( (time_t *)&ts );
		char tmp[100];
		strftime(tmp,100,"%b %d %Y",timeStruct);
		safeStrcpy(tmp);
	}
	return true;
}

bool SafeBuf::hasDigits() {
	if ( m_length <= 0 ) return false;
	for ( int32_t i = 0 ; i < m_length ; i++ )
		if ( is_digit(m_buf[i]) ) return true;
	return false;
}


int32_t SafeBuf::indexOf(char c) {
	char* p = m_buf;
	char* pend = m_buf + m_length;
	while (p < pend && *p != c) p++;
	if (p == pend) return -1;
	return p - m_buf;
}
