#ifndef _SAFEBUF_H_
#define _SAFEBUF_H_

//#include "Mem.h"
//#include "Unicode.h"
#include "gb-include.h"

/**
 * Safe Char Buffer, or mutable Strings.
 * (for java programmers, very similar to the StringBuffer class, with all the speed that c++ allows).
 * Most of strings in Gigablast are handled by those.
 */

#include "iana_charset.h"

class SafeBuf {
public:
	//*TRUCTORS
	SafeBuf();
	SafeBuf(int32_t initSize, const char *label = NULL);

	void constructor();
	void destructor ();

	//be careful with passing in a stackBuf! it could go out
	//of scope independently of the safebuf.
	SafeBuf(char* stackBuf, int32_t cap, const char* label = NULL);
	SafeBuf(char *heapBuf, int32_t bufMax, int32_t bytesInUse, bool ownData);
	~SafeBuf();

	void setLabel ( const char *label );
	
	// CAUTION: BE CAREFUL WHEN USING THE FOLLOWING TWO FUNCTIONS!!
	// setBuf() allows you reset the contents of the SafeBuf to either
	// a stack buffer or a dynamic buffer. Only pass in true for
	// ownData if this is not a stack buffer and you are sure you
	// want SafeBuf to free the data for you. Keep in mind, all
	// previous content in SafeBuf will be cleared when you pass it
	// a new buffer.
	bool setBuf(char *newBuf, 
		    int32_t bufMax, 
		    int32_t bytesInUse, 
		    bool ownData,
		    int16_t encoding = csUTF8 );
	// yieldBuf() allows you to take over the buffer in SafeBuf. 
	// You may only free the data if it was originally owned by
	// the SafeBuf.
	// Think twice before using this function.
	bool yieldBuf(char **bufPtr, int32_t *bufAlloc, int32_t *bytesInUse,
		      bool *ownData, int16_t *encoding );

	// set buffer from another safebuf, stealing it
	bool stealBuf ( SafeBuf *sb );

	//ACCESSORS
	char *getBuf() { return m_buf + m_length; }
	char *getBufPtr() { return m_buf + m_length; }
	char *getBufCursor() { return m_buf + m_length; }
	char *getBufStart() { return m_buf; }
    const char *getBufStart() const { return m_buf; }
	char *getBufEnd() { return m_buf + m_capacity; }
	int32_t getCapacity() const { return m_capacity; }
	int32_t getAvail() const { return m_capacity - m_length; }
	int32_t length() const { return m_length; }
	int32_t getLength() const { return m_length; }
	int32_t getBufUsed() const { return m_length; }
	void print() { 
	  if ( write(1,m_buf,m_length) != m_length) { char*xx=NULL;*xx=0;}; }

	// . returns bytes written to file, 0 is acceptable if m_length == 0
	// . returns -1 on error and sets g_errno
	int32_t saveToFile ( const char *dir , const char *filename ) ;
	int32_t dumpToFile(const char *filename);
	int32_t save ( const char *dir, const char *fname){return saveToFile(dir,fname); };
	int32_t save ( const char *fullFilename ) ;
	// saves to tmp file and if that succeeds then renames to orig filename
	int32_t safeSave (const char *filename );

	int32_t  fillFromFile(const char *filename);
	int32_t  fillFromFile(const char *dir,const char *filename, const char *label=NULL);
	int32_t  load(const char *dir,const char *fname,const char *label = NULL) { 
		return fillFromFile(dir,fname,label);};
	int32_t  load(const char *fname) { return fillFromFile(fname);};

	void filterTags();
	void filterQuotes();
	bool truncateLongWords ( const char *src, int32_t srcLen , int32_t minmax );
	bool safeTruncateEllipsis ( const char *src , int32_t maxLen );
	bool safeTruncateEllipsis ( const char *src , int32_t srcLen, int32_t maxLen );

	bool convertJSONtoXML ( int32_t niceness , int32_t startConvertPos );

	bool safeDecodeJSONToUtf8 ( const char *json, int32_t jsonLen, 
				    int32_t niceness);
	//			    bool decodeAll = false );

	bool decodeJSONToUtf8 ( int32_t niceness );
	bool decodeJSON ( int32_t niceness );
	bool linkify ( int32_t niceness , int32_t startPos );

	void truncLen ( int32_t newLen ) {
		if ( m_length > newLen ) m_length = newLen; };

	bool set ( const char *str ) {
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
	bool  safePrintf(const char *formatString, ...)
		__attribute__ ((format(printf, 2, 3)));
#else
	bool  safePrintf(const char *formatString, ...);
#endif
	bool  safeMemcpy(const void *s, int32_t len){return safeMemcpy((const char *)s,len);};
	bool  safeMemcpy(const char *s, int32_t len);
	bool  safeMemcpy_nospaces(const char *s, int32_t len);
	bool  safeMemcpy(SafeBuf *c){return safeMemcpy(c->m_buf,c->m_length);};
	bool  safeMemcpy ( class Words *w , int32_t a , int32_t b ) ;
	bool  safeStrcpy ( const char *s ) ;
	//bool  safeStrcpyPrettyJSON ( char *decodedJson ) ;
	bool  safeUtf8ToJSON ( const char *utf8 ) ;
	bool jsonEncode ( const char *utf8 ) { return safeUtf8ToJSON(utf8); };
	bool jsonEncode ( char *utf8 , int32_t utf8Len );

	bool  csvEncode ( const char *s , int32_t len , int32_t niceness = 0 );

	bool  base64Encode ( const char *s , int32_t len , int32_t niceness = 0 );
	bool  base64Decode ( const char *src , int32_t srcLen , int32_t niceness = 0 ) ;

	bool base64Encode( const char *s ) ;

	//bool  pushLong ( int32_t val ) { return safeMemcpy((char *)&val,4); }
	bool  cat(const SafeBuf& c);
	// . only cat the sections/tag that start with "tagFilter"
	// . used by Spider.cpp to dump <div class=int16_tdisplay> sections
	//   to parse-int16_tdisplay.uh64.runid.txt for displaying the
	//   validation checkboxes in qa.html
	bool  cat2 ( SafeBuf& c,const char *tagFilter1,const char *tagFilter2);
	void  reset() { m_length = 0; }
	void  purge(); // Clear all data and free all allocated memory
	bool  advance ( int32_t i ) ;

	bool safePrintFilterTagsAndLines ( const char *p , int32_t plen ,
					   bool oneWordPerLine ) ;

	// . if clearIt is true we init the new buffer space to zeroes
	// . used by Collectiondb.cpp
	bool  reserve(int32_t i, const char *label=NULL , bool clearIt = false );
	bool  reserve2x(int32_t i, const char *label = NULL );

	char *makeSpace ( int32_t size ) {
		if ( ! reserve ( size ) ) return NULL;
		return m_buf + m_length;
	};

	bool  inlineStyleTags();
	void  incrementLength(int32_t i) { 
		m_length += i; 
		// watch out for negative i's
		if ( m_length < 0 ) m_length = 0; 
	};
	void  setLength(int32_t i) { m_length = i; };
	char *getNextLine ( char *p ) ;
	int32_t  catFile(const char *filename) ;
	//int32_t  load(char *dir,char *filename) { 
	//	return fillFromFile(dir,filename);};
	bool  safeLatin1ToUtf8(const char *s, int32_t len);
	bool  safeUtf8ToLatin1(const char *s, int32_t len);
	void  detachBuf();
	bool  insert ( class SafeBuf *c , int32_t insertPos ) ;
	bool  insert ( const char *s , int32_t insertPos ) ;
	bool  insert2 ( const char *s , int32_t slen, int32_t insertPos ) ;
	bool  replace ( const char *src , const char *dst ) ; // must be same lengths!
	bool removeChunk1 ( const char *p , int32_t len ) ;
	bool removeChunk2 ( int32_t pos , int32_t len ) ;
	bool  safeReplace(const char *s, int32_t len, int32_t pos, int32_t replaceLen);
	bool  safeReplace2 ( const char *s, int32_t slen, 
			     const char *t , int32_t tlen ,
			     int32_t niceness ,
			     int32_t startOff = 0 );
	bool  safeReplace3 ( const char *s, const char *t , int32_t niceness = 0 ) ;
	void replaceChar ( char src , char dst );
	bool  copyToken(const char* s);
	//output encoding
	bool  setEncoding(int16_t cs);
	int16_t getEncoding() { return m_encoding; };

	void zeroOut() { memset ( m_buf , 0 , m_capacity ); }

	// insert <br>'s to make 's' no more than 'cols' chars per line
	bool brify2 ( const char *s , int32_t cols , const char *sep = "<br>" ,
		      bool isHtml = true ) ;

	bool brify ( const char *s , int32_t slen , int32_t niceness , int32_t cols ,
		     const char *sep = "<br>" , bool isHtml = true );

	bool fixIsolatedPeriods ( ) ;

	bool hasDigits();

	// treat safebuf as an array of signed int32_ts and sort them
	void sortLongs ( int32_t niceness );

	// . like "1 minute ago" "5 hours ago" "3 days ago" etc.
	// . "ts" is the delta-t in seconds
	bool printTimeAgo ( int32_t ts , int32_t now , bool int16_thand = false ) ;

	// . a function for adding Tags to buffer, like from Tagdb.cpp
	// . if safebuf is a buffer of Tags from Tagdb.cpp
	class Tag *addTag2 ( char *mysite , 
			    char *tagname ,
			    int32_t  now ,
			    char *user ,
			    int32_t  ip ,
			    int32_t  val ,
			    char  rdbId );
	class Tag *addTag3 ( char *mysite , 
			    char *tagname ,
			    int32_t  now ,
			    char *user ,
			    int32_t  ip ,
			    const char *data ,
			    char  rdbId );
	// makes the site "%"UINT64".com" where %"UINT64" is userId
	class Tag *addFaceookTag ( int64_t userId ,
				   char *tagname ,
				   int32_t  now ,
				   int32_t  ip ,
				   char *data ,
				   int32_t  dsize ,
				   char  rdbId ,
				   bool  pushRdbId ) ;
	class Tag *addTag ( char *mysite , 
			    char *tagname ,
			    int32_t  now ,
			    char *user ,
			    int32_t  ip ,
			    const char *data ,
			    int32_t  dsize ,
			    char  rdbId ,
			    bool  pushRdbId );
	bool addTag ( class Tag *tag );

	//insert strings in their native encoding
	bool  encode ( const char *s , int32_t len , int32_t niceness=0) {
		return utf8Encode2(s,len,false,niceness); };
	// htmlEncode default = false
	bool  utf8Encode2(const char *s, int32_t len, bool htmlEncode=false, 
			 int32_t niceness=0);
	bool  latin1Encode(const char *s, int32_t len, bool htmlEncode=false,
			   int32_t niceness=0);
    bool utf32Encode(const UChar32* codePoints, int32_t cpLen);
	//bool  utf16Encode(UChar *s, int32_t len, bool htmlEncode=false);
	//bool  utf16Encode(char *s, int32_t len, bool htmlEncode=false) {
	//	return utf16Encode((UChar*)s, len>>1, htmlEncode); };
	//bool  utf32Encode(UChar32 c);
	bool  htmlEncode(const char *s, int32_t len,bool encodePoundSign,
			 int32_t niceness=0 , int32_t truncateLen = -1 );
	bool  javascriptEncode(const char *s, int32_t len );

	bool  htmlEncode(const char *s) ;

	//bool convertUtf8CharsToEntity = false ) ;
	// html-encode any of the last "len" bytes that need it
	bool htmlEncode(int32_t len,int32_t niceness=0);

	bool htmlDecode (const char *s,
			 int32_t len,
			 bool doSpecial = false,
			 int32_t niceness = 0 );

	//bool  htmlEncode(int32_t niceness );
	bool  dequote ( const char *t , int32_t tlen );
	bool  escapeJS ( const char *s , int32_t slen ) ;

	bool  urlEncode (const char *s , 
			 int32_t slen, 
			 bool requestPath = false,
			 bool encodeApostrophes = false );

	bool  urlEncode (const char *s ) {
		return urlEncode ( s,strlen(s),false,false); };


	bool  urlEncode2 (const char *s , 
			  bool encodeApostrophes ) { // usually false
		return urlEncode ( s,strlen(s),false,encodeApostrophes); };

	bool  urlEncodeAllBuf ( bool spaceToPlus = true );
	//bool  latin1CdataEncode(char *s, int32_t len);
	bool  utf8CdataEncode(const char *s, int32_t len);

	// . filter out parentheses and other query operators
	// . used by SearchInput.cpp when it constructs the big UOR query
	//   of facebook interests
	bool queryFilter ( const char *s , int32_t len );

	//bool  utf16CdataEncode(UChar *s, int32_t len);
	//bool  utf16CdataEncode(char *s, int32_t len) {
	//	return utf16CdataEncode((UChar*)s, len>>1); };

	bool  latin1HtmlEncode(const char *s, int32_t len, int32_t niceness=0);
	//bool  utf16HtmlEncode(UChar *s, int32_t len);
	//bool  utf16HtmlEncode(char *s, int32_t len) {
	//	return utf16HtmlEncode((UChar*)s, len>>1); };

	bool htmlEncodeXmlTags ( const char *s , int32_t slen , int32_t niceness ) ;

	bool  cdataEncode ( const char *s ) ;
	bool  cdataEncode ( const char *s , int32_t slen ) ;

	// . append a \0 but do not inc m_length
	// . for null terminating strings
	bool nullTerm ( ) {
		if(m_length >= m_capacity && !reserve(m_capacity + 1) )
			return false;
		m_buf[m_length] = '\0';
		return true;
	};

	int32_t indexOf(char c);

	bool  safeCdataMemcpy(const char *s, int32_t len);
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


	// hack off trailing 0's
	bool printFloatPretty ( float f ) ;

	char* pushStr  (const char* str, uint32_t len);
	bool  pushPtr  ( const void *ptr );
	bool  pushLong (int32_t i);
	bool  pushLongLong (int64_t i);
	bool  pushFloat (float i);
	bool  pushDouble (double i);
	int32_t  popLong();
	float popFloat();

	int32_t  pad(const char ch, const int32_t len);
	bool  printKey(const char* key, char ks);

	// these use zlib
	bool compress();
	bool uncompress();

	//OPERATORS
	//copy numbers into the buffer, *in binary*
	//useful for making lists.
	bool  operator += (uint64_t i);
	bool  operator += (int64_t i);
	//bool  operator += (int32_t i);
	//bool  operator += (uint32_t i);
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
	char& operator[](int32_t i);
	
public:
	int32_t  m_capacity;
	int32_t  m_length;
protected:
	char *m_buf;
public:
	const char *m_label;
	bool  m_usingStack;
	int16_t m_encoding; // output charset

	// . a special flag used by PageParser.cpp
	// . if this is true it PageParser shows the page in its html form,
	//   otherwise, if false, it converts the "<" to &lt; etc. so we see the html
	//   source view.
	// . only Words.cpp looks at this flag
	char  m_renderHtml;
};

#define XSTRMACRO(s) STRMACRO(s)
#define STRMACRO(s) #s
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define StackBuf(name) char TOKENPASTE2(tmpsafebuf, __LINE__)[1024];	\
	SafeBuf name(TOKENPASTE2(tmpsafebuf, __LINE__), 1024, STRMACRO(TOKENPASTE2(__FILE__, __LINE__)))


#endif
