#ifndef _SAFEBUF_H_
#define _SAFEBUF_H_

//#include "Mem.h"
//#include "Unicode.h"
#include "gb-include.h"

class SafeBuf {
public:
	//*TRUCTORS
	SafeBuf();
	SafeBuf(long initSize, char *label = NULL);

	void constructor();

	//be careful with passing in a stackBuf! it could go out
	//of scope independently of the safebuf.
	SafeBuf(char* stackBuf, long cap);
	SafeBuf(char *heapBuf, long bufMax, long bytesInUse, bool ownData);
	~SafeBuf();

	void setLabel ( char *label );
	
	// CAUTION: BE CAREFUL WHEN USING THE FOLLOWING TWO FUNCTIONS!!
	// setBuf() allows you reset the contents of the SafeBuf to either
	// a stack buffer or a dynamic buffer. Only pass in true for
	// ownData if this is not a stack buffer and you are sure you
	// want SafeBuf to free the data for you. Keep in mind, all
	// previous content in SafeBuf will be cleared when you pass it
	// a new buffer.
	bool setBuf(char *newBuf, long bufMax, long bytesInUse, bool ownData,
		    short encoding );
	// yieldBuf() allows you to take over the buffer in SafeBuf. 
	// You may only free the data if it was originally owned by
	// the SafeBuf.
	// Think twice before using this function.
	bool yieldBuf(char **bufPtr, long *bufAlloc, long *bytesInUse,
		      bool *ownData, short *encoding );

	// set buffer from another safebuf, stealing it
	bool stealBuf ( SafeBuf *sb );

	//ACCESSORS
	char *getBuf() { return m_buf + m_length; }
	char *getBufStart() { return m_buf; }
	char *getBufEnd() { return m_buf + m_capacity; }
	long getCapacity() { return m_capacity; }
	long getAvail() { return m_capacity - m_length; }
	long length() { return m_length; }
	long getLength() { return m_length; }
	long getBufUsed() { return m_length; }
	void print() { 
	  if ( write(1,m_buf,m_length) != m_length) { char*xx=NULL;*xx=0;}; }

	// . returns bytes written to file, 0 is acceptable if m_length == 0
	// . returns -1 on error and sets g_errno
	long saveToFile ( char *dir , char *filename ) ;
	long dumpToFile(char *filename);
	long save ( char *dir, char *fname){return saveToFile(dir,fname); };
	long save ( char *fullFilename ) ;
	// saves to tmp file and if that succeeds then renames to orig filename
	long safeSave (char *filename );

	long  fillFromFile(char *filename);
	long  fillFromFile(char *dir,char *filename);
	long  load(char *dir,char *fname) { return fillFromFile(dir,fname);};
	long  load(char *fname) { return fillFromFile(fname);};

	void filterTags();
	void filterQuotes();
	bool truncateLongWords ( char *src, long srcLen , long minmax );
	bool safeTruncateEllipsis ( char *src , long maxLen );
	bool safeTruncateEllipsis ( char *src , long srcLen, long maxLen );

	bool convertJSONtoXML ( long niceness , long startConvertPos );

	bool safeDecodeJSONToUtf8 ( char *json, long jsonLen, long niceness);
	//			    bool decodeAll = false );

	bool decodeJSONToUtf8 ( long niceness );
	bool decodeJSON ( long niceness );
	bool linkify ( long niceness , long startPos );

	void truncLen ( long newLen ) {
		if ( m_length > newLen ) m_length = newLen; };

	bool set ( char *str ) {
		purge();
		if ( ! str ) return true;
		// puts a \0 at the end, but does not include it in m_length:
		return safeStrcpy ( str );
	};

	void removeLastChar ( char lastChar ) {
		if ( m_length <= 0 ) return;
		if ( m_buf[m_length-1] != lastChar ) return;
		m_length--;
		m_buf[m_length] = '\0';
	};

	//MUTATORS
#ifdef _CHECK_FORMAT_STRING_
	bool  safePrintf(char *formatString, ...)
		__attribute__ ((format(printf, 2, 3)));
#else
	bool  safePrintf(char *formatString, ...);
#endif
	bool  safeMemcpy(void *s, long len){return safeMemcpy((char *)s,len);};
	bool  safeMemcpy(char *s, long len);
	bool  safeMemcpy_nospaces(char *s, long len);
	bool  safeMemcpy(SafeBuf *c){return safeMemcpy(c->m_buf,c->m_length);};
	bool  safeMemcpy ( class Words *w , long a , long b ) ;
	bool  safeStrcpy ( char *s ) ;
	//bool  safeStrcpyPrettyJSON ( char *decodedJson ) ;
	bool  safeUtf8ToJSON ( char *utf8 ) ;
	bool jsonEncode ( char *utf8 ) { return safeUtf8ToJSON(utf8); };
	bool jsonEncode ( char *utf8 , long utf8Len );

	bool  csvEncode ( char *s , long len , long niceness = 0 );

	bool  base64Encode ( char *s , long len , long niceness = 0 );
	bool  base64Decode ( char *src , long srcLen , long niceness = 0 ) ;

	//bool  pushLong ( long val ) { return safeMemcpy((char *)&val,4); }
	bool  cat(SafeBuf& c);
	// . only cat the sections/tag that start with "tagFilter"
	// . used by Spider.cpp to dump <div class=shortdisplay> sections
	//   to parse-shortdisplay.uh64.runid.txt for displaying the
	//   validation checkboxes in qa.html
	bool  cat2 ( SafeBuf& c,char *tagFilter1,char *tagFilter2);
	void  reset() { m_length = 0; }
	void  purge(); // Clear all data and free all allocated memory
	bool  advance ( long i ) ;

	bool safePrintFilterTagsAndLines ( char *p , long plen ,
					   bool oneWordPerLine ) ;

	// . if clearIt is true we init the new buffer space to zeroes
	// . used by Collectiondb.cpp
	bool  reserve(long i, char *label=NULL , bool clearIt = false );
	bool  reserve2x(long i, char *label = NULL );

	char *makeSpace ( long size ) {
		if ( ! reserve ( size ) ) return NULL;
		return m_buf + m_length;
	};

	bool  inlineStyleTags();
	void  incrementLength(long i) { 
		m_length += i; 
		// watch out for negative i's
		if ( m_length < 0 ) m_length = 0; 
	};
	void  setLength(long i) { m_length = i; };
	char *getNextLine ( char *p ) ;
	long  catFile(char *filename) ;
	//long  load(char *dir,char *filename) { 
	//	return fillFromFile(dir,filename);};
	bool  safeLatin1ToUtf8(char *s, long len);
	bool  safeUtf8ToLatin1(char *s, long len);
	void  detachBuf();
	bool  insert ( class SafeBuf *c , long insertPos ) ;
	bool  insert ( char *s , long insertPos ) ;
	bool  insert2 ( char *s , long slen, long insertPos ) ;
	bool  replace ( char *src , char *dst ) ; // must be same lengths!
	bool removeChunk1 ( char *p , long len ) ;
	bool removeChunk2 ( long pos , long len ) ;
	bool  safeReplace(char *s, long len, long pos, long replaceLen);
	bool  safeReplace2 ( char *s, long slen, 
			     char *t , long tlen ,
			     long niceness ,
			     long startOff = 0 );
	bool  safeReplace3 ( char *s, char *t , long niceness = 0 ) ;
	void replaceChar ( char src , char dst );
	bool  copyToken(char* s);;
	//output encoding
	bool  setEncoding(short cs);
	short getEncoding() { return m_encoding; };

	void zeroOut() { memset ( m_buf , 0 , m_capacity ); }

	bool brify2 ( char *s , long cols , char *sep = "<br>" ,
		      bool isHtml = true ) ;

	bool brify ( char *s , long slen , long niceness , long cols ,
		     char *sep = "<br>" , bool isHtml = true );

	bool fixIsolatedPeriods ( ) ;

	bool hasDigits();

	// treat safebuf as an array of signed longs and sort them
	void sortLongs ( long niceness );

	// . like "1 minute ago" "5 hours ago" "3 days ago" etc.
	// . "ts" is the delta-t in seconds
	bool printTimeAgo ( long ts , long now , bool shorthand = false ) ;

	// . a function for adding Tags to buffer, like from Tagdb.cpp
	// . if safebuf is a buffer of Tags from Tagdb.cpp
	class Tag *addTag2 ( char *mysite , 
			    char *tagname ,
			    long  now ,
			    char *user ,
			    long  ip ,
			    long  val ,
			    char  rdbId );
	class Tag *addTag3 ( char *mysite , 
			    char *tagname ,
			    long  now ,
			    char *user ,
			    long  ip ,
			    char *data ,
			    char  rdbId );
	// makes the site "%llu.com" where %llu is userId
	class Tag *addFaceookTag ( long long userId ,
				   char *tagname ,
				   long  now ,
				   long  ip ,
				   char *data ,
				   long  dsize ,
				   char  rdbId ,
				   bool  pushRdbId ) ;
	class Tag *addTag ( char *mysite , 
			    char *tagname ,
			    long  now ,
			    char *user ,
			    long  ip ,
			    char *data ,
			    long  dsize ,
			    char  rdbId ,
			    bool  pushRdbId );
	bool addTag ( class Tag *tag );

	//insert strings in their native encoding
	bool  encode ( char *s , long len , long niceness=0) {
		return utf8Encode2(s,len,false,niceness); };
	// htmlEncode default = false
	bool  utf8Encode2(char *s, long len, bool htmlEncode=false, 
			 long niceness=0);
	bool  latin1Encode(char *s, long len, bool htmlEncode=false,
			   long niceness=0);
	//bool  utf16Encode(UChar *s, long len, bool htmlEncode=false);
	//bool  utf16Encode(char *s, long len, bool htmlEncode=false) {
	//	return utf16Encode((UChar*)s, len>>1, htmlEncode); };
	//bool  utf32Encode(UChar32 c);
	bool  htmlEncode(char *s, long len,bool encodePoundSign,
			 long niceness=0 , long truncateLen = -1 );
	bool  javascriptEncode(char *s, long len );

	bool  htmlEncode(char *s) ;

	//bool convertUtf8CharsToEntity = false ) ;
	// html-encode any of the last "len" bytes that need it
	bool htmlEncode(long len,long niceness=0);

	bool htmlDecode (char *s,
			 long len,
			 bool doSpecial = false,
			 long niceness = 0 );

	//bool  htmlEncode(long niceness );
	bool  dequote ( char *t , long tlen );
	bool  escapeJS ( char *s , long slen ) ;

	bool  urlEncode (char *s , 
			 long slen, 
			 bool requestPath = false,
			 bool encodeApostrophes = false );

	bool  urlEncode (char *s ) {
		return urlEncode ( s,strlen(s),false,false); };


	bool  urlEncode2 (char *s , 
			  bool encodeApostrophes ) { // usually false
		return urlEncode ( s,strlen(s),false,encodeApostrophes); };

	bool  urlEncodeAllBuf ( bool spaceToPlus = true );
	bool  latin1CdataEncode(char *s, long len);
	bool  utf8CdataEncode(char *s, long len);

	// . filter out parentheses and other query operators
	// . used by SearchInput.cpp when it constructs the big UOR query
	//   of facebook interests
	bool queryFilter ( char *s , long len );

	//bool  utf16CdataEncode(UChar *s, long len);
	//bool  utf16CdataEncode(char *s, long len) {
	//	return utf16CdataEncode((UChar*)s, len>>1); };

	bool  latin1HtmlEncode(char *s, long len, long niceness=0);
	//bool  utf16HtmlEncode(UChar *s, long len);
	//bool  utf16HtmlEncode(char *s, long len) {
	//	return utf16HtmlEncode((UChar*)s, len>>1); };

	bool htmlEncodeXmlTags ( char *s , long slen , long niceness ) ;

	bool  cdataEncode ( char *s ) ;
	bool  cdataEncode ( char *s , long slen ) ;

	// . append a \0 but do not inc m_length
	// . for null terminating strings
	bool nullTerm ( ) {
		if(m_length >= m_capacity && !reserve(m_capacity + 1) )
			return false;
		m_buf[m_length] = '\0';
		return true;
	};


	bool  safeCdataMemcpy(char *s, long len);
	bool  pushChar (char i) {
		if(m_length >= m_capacity) 
			if(!reserve(2*m_capacity + 1))
				return false;
		m_buf[m_length++] = i;
		// let's do this because we kinda expect it when making strings
		// and i've been burned by not having this before.
		// no, cause if we reserve just the right length, we end up
		// doing a realloc!! sux...
		//m_buf[m_length] = '\0';
		return true;
	};


	bool  pushLong (long i);
	bool  pushLongLong (long long i);
	bool  pushFloat (float i);
	bool  pushDouble (double i);
	long  popLong();
	float popFloat();

	long  pad(const char ch, const long len);
	bool  printKey(char* key, char ks);

	// these use zlib
	bool compress();
	bool uncompress();

	//OPERATORS
	//copy numbers into the buffer, *in binary*
	//useful for making lists.
	bool  operator += (uint64_t i);
	bool  operator += (long long i);
	bool  operator += (long i);
	bool  operator += (unsigned long i);
	bool  operator += (float i);
	bool  operator += (double i);
	bool  operator += (char i);

	//bool  operator += (uint64_t i);
	bool  operator += (uint32_t i);
	bool  operator += (uint16_t i);
	bool  operator += (uint8_t  i);

	//bool  operator += (int64_t  i) { return *this += (uint64_t)i; };
	bool  operator += (int32_t  i) { return *this += (uint32_t)i; };
	bool  operator += (int16_t  i) { return *this += (uint16_t)i; };
	bool  operator += (int8_t   i) { return *this += (uint8_t)i;  };

	//return a reference so we can use on lhs and rhs.
	char& operator[](long i);
	
public:
	long  m_capacity;
	long  m_length;
protected:
	char *m_buf;
public:
	char *m_label;
	bool  m_usingStack;
	short m_encoding; // output charset

	// . a special flag used by PageParser.cpp
	// . if this is true it PageParser shows the page in its html form,
	//   otherwise, if false, it converts the "<" to &lt; etc. so we see the html
	//   source view.
	// . only Words.cpp looks at this flag
	char  m_renderHtml;
};


#endif
