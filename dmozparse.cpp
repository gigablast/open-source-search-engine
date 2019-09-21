//
// Gigablast, Copyright March 2005
// Author: Javier Olivares <jolivares@gigablast.com>
//
// DMOZ RDF file parser into proprietary format
// See the "usage" note in the main function for usage and features.
// I apologize to anyone who must maintain or even simply read this code.
//

#include "gb-include.h"

#include <iostream>
#include <fstream>
#include "Url.h"
#include "HttpRequest.h"
#include "sort.h"

#undef malloc
#undef calloc
#undef realloc
bool closeAll ( void *state , void (* callback)(void *state) ) { return true; }
bool allExit ( ) { return true; };

bool sendPageSEO(TcpSocket *s, HttpRequest *hr) {return true;}

//int32_t g_qbufNeedSave = false;
//SafeBuf g_qbuf;

bool g_recoveryMode;
int32_t g_recoveryLevel;

int g_inMemcpy;

#define RDFBUFFER_SIZE    (1024*1024*10)
#define RDFSTRUCTURE_FILE "structure.rdf.u8"
#define RDFCONTENT_FILE   "content.rdf.u8"

#define STRUCTURE_OUTPUT_FILE   "gbdmoz.structure.dat"
#define CONTENT_OUTPUT_FILE     "gbdmoz.content.dat"
#define URL_OUTPUT_FILE         "gbdmoz.urls.dat"
#define URLTEXT_OUTPUT_FILE     "gbdmoz.urls.txt"
#define DIFFURLTEXT_OUTPUT_FILE "gbdmoz.diffurls.txt"
#define CATEGORY_OUTPUT_FILE    "gbdmoz.categories.txt"

#define NAME_BUFFER_SIZE    24*1024*1024
#define CAT_BUFFER_SIZE     256*1024
#define URL_BUFFER_SIZE     32*1024*1024
#define URLINFO_BUFFER_SIZE 1024*1024

#define MAX_CATID_LEN    63
#define MAX_TAG_LEN      127
#define MAX_URL_CATIDS   32
#define MAX_URLTXT_SIZE  500000

#define HASHTABLE_SIZE    (1024*1024)
#define URLHASHTABLE_SIZE (10*1024*1024)

#define MODE_NONE        0
#define MODE_NEW         1
#define MODE_UPDATE      2
#define MODE_URLDUMP     3
#define MODE_DIFFURLDUMP 4
#define MODE_CATDUMP     5

#define OLDURL_BUFFER_SIZE   (32*1024*1024)
#define OLDCATID_BUFFER_SIZE (1024*1024)

using namespace std;

// struct for a link list hash table
struct HashLink {
	int32_t  m_keyOffset;
	int32_t  m_keyLen;
	int32_t  m_data;
	HashLink *m_next;
};

// another hash, for urls
struct UrlHashLink {
	uint64_t m_key;
	//uint32_t      m_key2;
	//int32_t m_urlOffset;
	//int32_t m_urlLen;
	int32_t m_index;
	UrlHashLink *m_next;
};

// structure to store url info
struct UrlInfo {
	//uint64_t m_hash;
	//int16_t m_urlLen;
	//int32_t  m_urlOffset;
	unsigned char m_numCatids;
	//int32_t m_catids[MAX_URL_CATIDS];
	int32_t *m_catids;
	char  m_changed;
};

// struct for storing categories and their related info
struct RdfCat {
	int32_t   m_catid;
	int32_t   m_parentid;
	//int16_t  m_numSymParents;
	//int32_t  *m_symParents;
	int32_t   m_nameOffset;
	int16_t  m_nameLen;
	uint32_t  m_structureOffset;
	uint32_t  m_contentOffset;
	uint32_t  m_catHash;
	int32_t   m_numUrls;
};

// hash tables
HashLink *hashTable[HASHTABLE_SIZE];
UrlHashLink *urlHashTable[URLHASHTABLE_SIZE];
// url buffer
char    *urlBuffer     = NULL;
int32_t     urlBufferSize = 0;
int32_t     urlBufferLen  = 0;
// url info array
UrlInfo *urlInfos      = NULL;
int32_t     urlInfosSize  = 0;
int32_t     numUrlInfos   = 0;
// categories
RdfCat *rdfCats     = NULL;
int32_t    rdfCatsSize = 0;
int32_t    numRdfCats  = 0;
// rdf file stream
//ifstream  rdfStream;
int       rdfStream;
char     *rdfBuffer  = NULL;
char     *rdfPtr     = NULL;
char     *rdfEnd     = NULL;
// output file stream for serialization
//ofstream  outStream;
//ofstream  outStream2;
int       outStream;
int       outStream2;
// offset into the file
uint32_t      currOffset = 0;
// cat name buffer
char     *nameBuffer     = NULL;
int32_t      nameBufferSize = 0;
int32_t      nameBufferLen  = 0;
// catid buffer
char      catidBuffer[MAX_CATID_LEN+1];
int32_t      catidLen = 0;
// tag buffer
char      tagRecfer[MAX_TAG_LEN+1];
int32_t      tagLen = 0;

bool mainShutdown ( bool urgent ) { return true; }

// increment the ptr into the file, possibly reading the next chunk
char* incRdfPtr( int32_t skip = 1 ) {
	int32_t n;
	for (int32_t i = 0; i < skip; i++) {
		rdfPtr++;
		currOffset++;
		// pull the next chunk if we're at the end
		if (rdfPtr >= rdfEnd) {
			// if nothing left, return NULL
			//if (!rdfStream.good())
			//	return NULL;
			// get the next chunk
			//rdfStream.read(rdfBuffer, RDFBUFFER_SIZE);
			//n      = rdfStream.gcount();
			n = read(rdfStream, rdfBuffer, RDFBUFFER_SIZE);
			if ( n <= 0 || n > RDFBUFFER_SIZE )
				return NULL;
			rdfPtr = rdfBuffer;
			rdfEnd = &rdfBuffer[n];
		}
	}
	return rdfPtr;
}

// parse the rdf file up past a given start tag
int32_t rdfParse ( char *tagName ) {
	//bool inQuote = false;
	do {
		int32_t matchPos = 0;
		// move to the next tag
		// . quotes are no longer escaped out in the newer
		//   dmoz files in oct 2013... so take that out. i do
		//   this < is &lt; though.. perhaps only check for
		//   quotes when in a tag?
		while (*rdfPtr != '<' ) { // || inQuote ) {
			// check for quotes
			//if (*rdfPtr == '"')
			//	inQuote = !inQuote;
			// next char
			if (!incRdfPtr())
				return -1;
		}
		// check if the tag is good
		do {
			if (!incRdfPtr())
				return -1;
			if (*rdfPtr != tagName[matchPos])
				break;
			matchPos++;
		} while (tagName[matchPos]);
		// matched if we're at the end of the tagName
		if (!tagName[matchPos]) {
			if (!incRdfPtr())
				return -1;
			return 0;
		}
		// otherwise it's not a match, keep going
		matchPos = 0;
	} while (true);
}

// move to the next tag in the file
int32_t rdfNextTag ( ) {
	//bool inQuote = false;
	// move to the next tag
	while (*rdfPtr != '<' ) { // || inQuote ) {
		// check for quotes
		// NO! too many unbalanced quotes all over the place!
		// and i think quotes in tags do not have < or > in them
		// because they should be encoded as &gt; and &lt;
		//if (*rdfPtr == '"')
		//	inQuote = !inQuote;
		// next char
		if (!incRdfPtr())
			return -1;
	}
	// skip the <
	if (!incRdfPtr())
		return -1;
	// put the tag name in a buffer
	tagLen = 0;
	while ( *rdfPtr != ' ' &&
		*rdfPtr != '>' ) {
		// insert the current char
		if (tagLen < MAX_TAG_LEN) {
			tagRecfer[tagLen] = *rdfPtr;
			tagLen++;
		}
		// next char
		if (!incRdfPtr())
			return -1;
	}
	tagRecfer[tagLen] = '\0';
	// success
	return 0;
}

// compare two cats, for gbsort
int catcomp ( const void *c1, const void *c2 ) {
	return (((RdfCat*)c1)->m_catid - ((RdfCat*)c2)->m_catid);
}

// hash a string
uint32_t catHash ( char *key, int32_t keyLen ) {
	// simple hash
	uint32_t hash = 0;
	for (int32_t i = 0; i < keyLen; i++)
		hash ^= key[i]*i;
	return (hash % HASHTABLE_SIZE);
}

// NOTE: these hash functions assume the name buffer
//       and key offset are preserved throughout the
//       use of the hash

// init the hash table
void initHashTable ( ) {
	for (int32_t i = 0; i < HASHTABLE_SIZE; i++)
		hashTable[i] = NULL;
}

// clear the hash table
void clearHashTable ( ) {
	for (int32_t i = 0; i < HASHTABLE_SIZE; i++) {
		while (hashTable[i]) {
			HashLink *next = hashTable[i]->m_next;
			free(hashTable[i]);
			hashTable[i] = next;
		}
		hashTable[i] = NULL;
	}
}

// add a string to a hash table with the given data
int32_t addCatHash ( int32_t keyOffset, int32_t keyLen, int32_t data ) {
	// get the hash value
	uint32_t hashKey = catHash(&nameBuffer[keyOffset], keyLen);
	// get the first node
	HashLink **currLink = &hashTable[hashKey];
	// go to the first empty node
	while (*currLink)
		currLink = &((*currLink)->m_next);
	// fill the node
	*currLink = (HashLink*)malloc(sizeof(HashLink));
	if (!(*currLink))
		return -1;
	(*currLink)->m_keyOffset = keyOffset;
	(*currLink)->m_keyLen    = keyLen;
	(*currLink)->m_data      = data;
	(*currLink)->m_next      = NULL;
	return 0;
}

// get the data in the hash using a string key
int32_t getCatHash ( char *key, int32_t keyLen ) {
	// get the hash value
	uint32_t hashKey = catHash(key, keyLen);
	// get the first node
	HashLink *currLink = hashTable[hashKey];
	// go to the correct node
	while ( currLink &&
		( currLink->m_keyLen != keyLen ||
		  strncmp(&nameBuffer[currLink->m_keyOffset], key, keyLen) != 0 ) )
		currLink = currLink->m_next;
	// return -1 if not found
	if (!currLink)
		return -1;
	else
		return currLink->m_data;
}

// init the hash table
void initUrlHashTable ( ) {
	for (int32_t i = 0; i < URLHASHTABLE_SIZE; i++)
		urlHashTable[i] = NULL;
}

// clear the hash table
void clearUrlHashTable ( ) {
	for (int32_t i = 0; i < URLHASHTABLE_SIZE; i++) {
		while (urlHashTable[i]) {
			UrlHashLink *next = urlHashTable[i]->m_next;
			free(urlHashTable[i]);
			urlHashTable[i] = next;
		}
		urlHashTable[i] = NULL;
	}
}

// add a url hash to the hash table with the given index
int32_t addUrlHash ( uint64_t key,
		  //uint32_t      key2,
		  int32_t index ) {
		  //int32_t index,
		  //int32_t urlOffset,
		  //int32_t urlLen ) {
	// get the hash value
	uint32_t hashKey = (key%(uint64_t)URLHASHTABLE_SIZE);
	// get the first node
	UrlHashLink **currLink = &urlHashTable[hashKey];
	// go to the first empty node
	while (*currLink)
		currLink = &((*currLink)->m_next);
	// fill the node
	*currLink = (UrlHashLink*)malloc(sizeof(UrlHashLink));
	if (!(*currLink))
		return -1;
	(*currLink)->m_key       = key;
	//(*currLink)->m_key2      = key2;
	(*currLink)->m_index     = index;
	//(*currLink)->m_urlOffset = urlOffset;
	//(*currLink)->m_urlLen    = urlLen;
	(*currLink)->m_next      = NULL;
	return 0;
}

// get the index in the hash using hash key
int32_t getUrlHash ( uint64_t key ) {
		  //uint32_t      key2 ) {
		  //uint32_t      key2,
		  //int32_t urlOffset,
		  //int32_t urlLen ) {
	// get the hash value
	uint32_t hashKey = (key%(uint64_t)URLHASHTABLE_SIZE);
	// get the first node
	UrlHashLink *currLink = urlHashTable[hashKey];
	// go to the correct node
	while ( currLink && currLink->m_key != key )
		//( currLink->m_key != key || currLink->m_key2 != key2 ) )
		//( currLink->m_key != key || currLink->m_key2 != key2 ||
		//currLink->m_urlLen != urlLen ||
		//strncasecmp(&urlBuffer[currLink->m_urlOffset],
		//	    &urlBuffer[urlOffset], urlLen) != 0) )
		currLink = currLink->m_next;
	// return -1 if not found
	if (!currLink)
		return -1;
	else
		return currLink->m_index;
}


// do a binary search to get a cat from an id
int32_t getIndexFromId ( int32_t catid ) {
	int32_t low  = 0;
	int32_t high = numRdfCats-1;
	int32_t currCat;
	// binary search
	//while (rdfCats[currCat].m_catid != catid) {
	while (low <= high) {
		// next check spot
		currCat = (low + high)/2;
		// check for hit
		if (rdfCats[currCat].m_catid == catid)
			return currCat;
		// shift search range
		else if (rdfCats[currCat].m_catid > catid)
			high = currCat-1;
		else
			low  = currCat+1;
	}
	//printf("catid %"INT32" not found. sanity checking.\n",catid);
	// sanity check our algo
	//for ( int32_t i = 0 ; i < numRdfCats ; i++ ) {
	//	if ( rdfCats[i].m_catid == catid ) { char *xx=NULL;*xx=0;}
	//}
	// not found
	return -1;
}

// print cat information
void printCats ( int32_t start, int32_t end ) {
	for (int32_t i = start; i < end; i++) {
		printf("Cat %"INT32":\n", i);
		printf("  CatID: %"INT32"\n", rdfCats[i].m_catid);
		printf("  Name:  ");
		for (int32_t n = rdfCats[i].m_nameOffset;
			  n < rdfCats[i].m_nameOffset + rdfCats[i].m_nameLen; n++)
			printf("%c", nameBuffer[n]);
		printf("\n");
		printf("  Name Offset:      %"INT32"\n", rdfCats[i].m_nameOffset);
		printf("  Structure Offset: %"INT32"\n", rdfCats[i].m_structureOffset);
		printf("  Content Offset:   %"INT32"\n", rdfCats[i].m_contentOffset);
		printf("  Parent:           %"INT32"\n", rdfCats[i].m_parentid);
		printf("\n");
	}
}

// parse out the next catid
int32_t parseNextCatid() {
	// parse for <catid, this will be the next cat
	if (rdfParse("catid") == -1)
		return -1;
	// go to the catid, skip '>'
	if (!incRdfPtr())
		return -1;
	catidLen = 0;
	while (*rdfPtr != '<') {
		if (catidLen < MAX_CATID_LEN) {
			catidBuffer[catidLen] = *rdfPtr;
			catidLen++;
		}
		if (!incRdfPtr())
			return -1;
	}
	catidBuffer[catidLen] = '\0';
	// translate the id
	return atol(catidBuffer);
}

// fill the next quoted string in the name buffer
int32_t fillNextString() {
	// get the next string, skip to the next quote
	while (*rdfPtr != '"') {
		if (!incRdfPtr())
			return -1;
	}
	// skip the quote
	if (!incRdfPtr())
		return -1;
	// . pointing at the string now
	//   dump it in the buffer
	int32_t nameLen = 0;
	while (*rdfPtr != '"') {
		// make sure there's room in the buffer
		if (nameBufferLen+nameLen >= nameBufferSize) {
			nameBufferSize += NAME_BUFFER_SIZE;
			nameBuffer = (char*)realloc((void*)nameBuffer,
						    sizeof(char)*nameBufferSize);
			printf("nameBuffer: %"INT32" bytes\n", nameBufferSize);
			if (!nameBuffer)
				return -2;
		}
		// fill the next character
		nameBuffer[nameBufferLen+nameLen] = *rdfPtr;
		nameLen++;
		if (!incRdfPtr())
			return -1;
	}
	// step past the quote
	if (!incRdfPtr())
		return -1;
	// return the length
	return nameLen;
}

// fill the next quoted url in the name buffer
int32_t fillNextUrl() {
	// get the next string, skip to the next quote
	while (*rdfPtr != '"') {
		if (!incRdfPtr())
			return -1;
	}
	// skip the quote
	if (!incRdfPtr())
		return -1;
	// . pointing at the string now
	//   dump it in the buffer
	int32_t urlLen = 0;
	while (*rdfPtr != '"') {
		// make sure there's room in the buffer
		if (urlBufferLen+urlLen+10 >= urlBufferSize) {
			urlBufferSize += URL_BUFFER_SIZE;
			urlBuffer = (char*)realloc((void*)urlBuffer,
						    sizeof(char)*urlBufferSize);
			printf("urlBuffer: %"INT32" bytes\n", urlBufferSize);
			if (!urlBuffer)
				return -2;
		}
		// fill the next character
		urlBuffer[urlBufferLen+urlLen] = *rdfPtr;
		urlLen++;
		if (!incRdfPtr())
			return -1;
	}
	// step past the quote
	if (!incRdfPtr())
		return -1;
	// return the length
	return urlLen;
}

// check the url for all valid characters
bool isGoodUrl ( char *url, int32_t urlLen ) {
	// . all we're going to check for right now are
	//   characters that show up as spaces
	if ( urlLen <= 0 )
		return false;
	for (int32_t i = 0; i < urlLen; i++) {
		if (is_wspace_a(url[i]))
			return false;
	}
	// check for [prot]://[url]
	int32_t bef   = 0;
	char *p    = url;
	char *pend = url + urlLen;
	while ( p < pend && *p != ':' ) {
		p++;
		bef++;
	}
	if ( bef == 0 || pend - p < 3 || p[1] != '/' || p[2] != '/' )
		return false;
	// good url
	return true;
}

// print the category path
int32_t printCatPath ( char *str, int32_t catid, bool raw ) {
	int32_t catIndex;
	int32_t parentId;
	char *p = str;
	// get the index
	catIndex = getIndexFromId(catid);
	if (catIndex < 1)
		return 0;
	// get the parent
	parentId = rdfCats[catIndex].m_parentid;

	// . print the parent(s) first
	// . in NEWER DMOZ dumps, "Top" is catid 2 and catid 1 is an
	//   empty title. really catid 2 is Top/World but that is an
	//   error that we correct below. (see "Top/World" below).
	//   but do not include the "Top/" as part of the path name
	if ( catid == 2 ) {
		// no! we now include Top as part of the path. let's
		// be consistent. i'd rather have www.gigablast.com/Top
		// and www.gigablast.com/Top/Arts etc. then i know if the
		// path starts with /Top that it is dmoz!!
		sprintf(p,"Top");
		return 3;
	}

	if (parentId > 1 && 
	    // the newer dmoz files have the catid == the parent id of
	    // i guess top most categories, like "Top/Arts"... i would think
	    // it should have a parentId of 1 like the old dmoz files,
	    // so it's probably a bug on dmoz's end
	    parentId != catid ) {
		p += printCatPath(p, parentId, raw);
		// print spacing
		if (!raw) p += sprintf(p, " / ");
		else      p += sprintf(p, "/");
	}
	// print this category name
	int32_t nameLen = rdfCats[catIndex].m_nameLen;
	gbmemcpy ( p,
		 &nameBuffer[rdfCats[catIndex].m_nameOffset],
		 nameLen );
	p += nameLen;
	// null terminate
	*p = '\0';
	// return length
	return (p - str);
}

int32_t fixUrl ( char *url, int32_t urlLen ) {
	int32_t slashi = 0;
	int32_t newUrlLen = urlLen;
	// check for a bad protocol, something:
	while (url[slashi] != ':') {
		slashi++;
		// if no :, throw it out
		if (slashi >= newUrlLen)
			return 0;
	}
	// check for a ://
	if (newUrlLen - slashi < 3)
		return 0;
	if (url[slashi]   != ':'   ||
	    url[slashi+1] != '/'   ||
	    url[slashi+2] != '/') {
		// fix news: to news://
		if (strncasecmp(url, "news:", 5) == 0) {
			char newsFix[1024];
			gbmemcpy(newsFix, url, newUrlLen);
			gbmemcpy(url, newsFix, 5);
			gbmemcpy(&url[5], "//", 2);
			gbmemcpy(&url[7], &newsFix[5], newUrlLen - 5);
			newUrlLen += 2;
		}
		// otherwise throw it out
		else
			return 0;
	}
	slashi += 3;
	// . jump over http:// if it starts with http://http://
	// . generic for any protocol
	char prot[1024];
	gbmemcpy(prot, url, slashi);
	prot[slashi] = '\0';
	sprintf(prot, "%s%s", prot, prot);
	while ( newUrlLen > slashi*2 &&
		strncasecmp(url, prot, slashi*2) == 0 ) {
		// remove the extra protocol
		memmove(url, &url[slashi], newUrlLen - slashi);
		newUrlLen -= slashi;
	}
	/*
	// remove a www.
	if (newUrlLen - slashi >= 4 &&
	    strncasecmp(&url[slashi], "www.", 4) == 0) {
		memmove(&url[slashi], &url[slashi+4], newUrlLen - (slashi+4));
		newUrlLen -= 4;
	}
	*/
	// look for //, cut down to single /, remove any spaces
	for (; slashi < newUrlLen; slashi++) {
		if (url[slashi-1] == '/' && url[slashi] == '/') {
			memmove(&url[slashi-1], &url[slashi], newUrlLen - slashi);
			newUrlLen--;
		}
		if (is_wspace_a(url[slashi])) {
			memmove(&url[slashi], &url[slashi+1], newUrlLen - (slashi+1));
			newUrlLen--;
		}
	}
	// remove any anchor
	// mdw, sep 2013, no because there is twitter.com/#!/ronpaul
	// and others...
	/*
	for (int32_t i = 0; i < newUrlLen; i++) {
		if (url[i] == '#') {
			newUrlLen = i;
			break;
		}
	}
	*/
	// remove any trailing /
	if (url[newUrlLen-1] == '/')
		newUrlLen--;
	// return the new length
	return newUrlLen;
}

// properly read from file
int32_t fileRead ( int fileid, void *buf, size_t count ) {
	char *p = (char*)buf;
	int32_t n = 0;
	uint32_t sizeRead = 0;
	while ( sizeRead < count ) {
		n = read ( fileid, p, count - sizeRead );
		if ( n <= 0 || n > (int32_t)count )
			return n;
		sizeRead += n;
		p += n;
	}
	return sizeRead;
}

// properly write to file
int32_t fileWrite ( int fileid, void *buf, size_t count ) {
	char *p = (char*)buf;
	int32_t n = 0;
	uint32_t sizeWrote = 0;
	while ( sizeWrote < count ) {
		n = write ( fileid, p, count - sizeWrote );
		if ( n <= 0 || n > (int32_t)count )
			return n;
		sizeWrote += n;
		p += n;
	}
	return sizeWrote;
}

// print special meta tags to tell gigablast to only spider/index
// the links and not the links of the links. b/c we only want
// to index the dmoz urls. AND ignore any external error like 
// ETCPTIMEDOUT when indexing a dmoz url so we can be sure to index
// all of them under the proper category so our gbcatid:xxx search 
// works and we can replicate dmoz accurately. see XmlDoc.cpp
// addOutlinksSpiderRecsToMetaList() and indexDoc() to see
// where these meta tags come into play.
void writeMetaTags ( int outStream2 ) {
	char *str = 
		"<!-- do not spider the links of the links -->\n"
		"<meta name=spiderlinkslinks content=0>\n"
		"<!--ignore tcp timeouts, dns timeouts, etc.-->\n"
		"<meta name=ignorelinksexternalerrors content=1>\n"
		"<!--do not index this document, but get links from it-->\n"
		"<meta name=noindex content=1>\n"
		// tell gigablast to not do a dns lookup on every
		// outlink when adding spiderRequests to spiderdb
		// for each outlink. will save time up front but
		// will have to be done when spidering the doc.
		"<!-- do not lookup the ip address of every outlink, "
		"but use hash of the subdomain as the ip -->\n"
		"<meta name=usefakeips content=1>\n"
		;
	int32_t len = gbstrlen(str);
	if ( write ( outStream2, str , len ) != len )
		printf("Error writing to outStream2b\n");
}

		


// main parser
int main ( int argc, char *argv[] ) {
	int32_t n;
	int32_t t = 0;
	int32_t ti = 0;
	int32_t m = 0;
	int32_t newNameBufferSize = 0;
	int32_t newOffset = 0;
	char filename[1256];
	int32_t urlTxtCount = 0;
	int32_t urlTxtFile  = 0;
	Url normUrl;
	char decodedUrl[MAX_URL_LEN];
	char htmlDecoded[MAX_HTTP_FILENAME_LEN];
	//int32_t numSymParents = 0;
	//int32_t endpos;
	// url diff stuff
	int32_t  numUpdateIndexes = 0;
	int32_t *updateIndexes    = NULL;
	int32_t  currUrl = 0;
	int32_t  currDiffIndex = 0;
	// options
	bool splitUrls = false;
	char mode = MODE_NONE;
	int32_t totalNEC = 0;
	char *dir="";
	bool firstTime;

	// check the options and mode
	for (int32_t i = 0; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0)
			splitUrls = true;
		else if (strcmp(argv[i], "urldump") == 0)
			mode = MODE_URLDUMP;
		else if (strcasecmp(argv[i], "update") == 0)
			mode = MODE_UPDATE;
		else if (strcasecmp(argv[i], "new") == 0)
			mode = MODE_NEW;
		else if (strcasecmp(argv[i], "diffurldump") == 0)
			mode = MODE_DIFFURLDUMP;
		else if (strcasecmp(argv[i], "catdump") == 0)
			mode = MODE_CATDUMP;
	}

	// check for correct call
	if (mode == MODE_NONE) {
		printf("\n"
		       "Usage: dmozparse [OPTIONS] [MODE]\n"
		       "\n"
		       "Modes:\n"
		       "  new          Generate new .dat files.\n"
		       "\n"
		       "  update       Generate new .dat.new files, updating\n"
		       "               existing .dat files. Changes will be\n"
		       "               written to gbdmoz.changes.dat.new.\n"
		       "               Catdb will update using these files\n"
		       "               when told to update.\n"
		       "\n"
		       "  urldump      Dump urls to file only.  This will not\n"
		       "               create any .dat files, only url txt \n"
		       "               files.\n"
		       "\n"
		       "  diffurldump  Dump urls that are new, changed, or\n"
		       "               removed in the latest update. (Uses\n"
		       "               gbdmoz.content.dat.new.diff)\n"
		       "\n"
		       "  catdump      Dump categories to file only.\n"
		       "\n"
		       "Options:\n"
		       "  -s           Split url output into multiple files.\n"
		       "               This is used for adding urls to gb\n"
		       "               which has a limit to the file size.\n"
		       "\n"
		       "\n" );
		exit(0);
	}

	// init the hash table for hashing urls
	if (!hashinit()) {
		printf("Hash Init Failed!\n");
		goto errExit;
	}

	// init the hash table
	initHashTable();

	printf("\n");
	// . create a large buffer for reading chunks
	//   of the rdf files
	rdfBuffer = (char*)malloc(sizeof(char)*(RDFBUFFER_SIZE+1));
	if (!rdfBuffer) {
		printf("Out of memory!!\n");
		goto errExit;
	}
	
	// skip hierarchy stuff for url dump
	if ( mode == MODE_URLDUMP || mode == MODE_DIFFURLDUMP )
		goto contentParse;

	// create the cat array
	rdfCatsSize = CAT_BUFFER_SIZE;
	rdfCats = (RdfCat*)malloc(sizeof(RdfCat)*rdfCatsSize);
	if (!rdfCats) {
		printf("Out of memory!!\n");
		goto errExit;
	}

	// create the name buffer
	nameBufferSize = NAME_BUFFER_SIZE;
	nameBuffer = (char*)malloc(sizeof(char)*nameBufferSize);
	if (!nameBuffer) {
		printf("Out of memory!!\n");
		goto errExit;
	}

	dir = "";

 retry:

	// open the structure file
	if ( mode == MODE_NEW || mode == MODE_CATDUMP )
		sprintf(filename, "%s%s", dir,RDFSTRUCTURE_FILE);
	else
		sprintf(filename, "%s%s.new", dir,RDFSTRUCTURE_FILE);
	//rdfStream.open(filename, ifstream::in);
	rdfStream = open ( filename, O_RDONLY );
	// make sure it opened okay
	//if (!rdfStream.is_open()) {
	if ( rdfStream < 0 ) {
		// try ./catdb/ subdir if not found
		if ( ! dir[0] ) {
			dir = "./catdb/";
			goto retry;
		}
		printf("Error Opening %s\n", filename);
		goto errExit;
	}
	printf("Opened Structure File: %s\n", filename);

	// take the first chunk
	//rdfStream.read(rdfBuffer, RDFBUFFER_SIZE);
	//n      = rdfStream.gcount();
	n = read ( rdfStream, rdfBuffer, RDFBUFFER_SIZE );
	if ( n <= 0 || n > RDFBUFFER_SIZE ) {
		printf("Error Reading %s\n", filename);
		goto errExit;
	}
	rdfPtr = rdfBuffer;
	rdfEnd = &rdfBuffer[n];
	currOffset = 0;
	firstTime = true;

	// read and parse the file
	printf("Parsing Topics...\n");
	while (true) {
		// parse for <Topic...
		if (rdfParse("Topic") == -1)
			goto fileEnd;
		// the offset for this cat is 6 chars back
		uint32_t catOffset = currOffset - 6;
		// get the topic name, preserve it on the buffer
		int32_t nameOffset = nameBufferLen;
		// the name inserted by this function into "nameBuffer"
		// does not seem to contain "Top/" at the beginning.
		// it is from structure.rdf.u8, but it seems to be there!
		// yeah, later on we hack the name buffer and nameOffset
		// so it is just the last word in the directory to save
		// mem. then we print out all the parent names to
		// reconstruct.
		int32_t nameLen    = fillNextString();
		if (nameLen == -1)
			goto fileEnd;
		if (nameLen == -2) {
			printf("Out of Memory!\n");
			goto errExit1;
		}
		// fix <Topic r:id=\"\"> in the newer content.rdf.u8
		if ( nameLen == 0 ) {
			// only do this once!
			if ( ! firstTime ) {
				printf("Encountered zero length name");
				continue;
			}
			gbmemcpy(nameBuffer+nameOffset,"Top\0",4);
			nameLen = 3;
			firstTime = false;
		}
		// html decode it
		if (nameLen > MAX_HTTP_FILENAME_LEN)
			nameLen = MAX_HTTP_FILENAME_LEN;
		nameLen = htmlDecode ( htmlDecoded,
				      &nameBuffer[nameOffset],
				       nameLen ,
				       false,
				       0);

		// parse the catid
		int32_t catid = parseNextCatid();
		if (catid == -1)
			goto fileEnd;

		// crap, in the new dmoz structure.rdf.u8 catid 1 is 
		// empty name and catid 2 has Topic tag "Top/World" but 
		// Title tag "Top".
		// but it should probably be "Top" and not "World". There is 
		// another catid 3 in structure.rdf.u8 that has 
		// <Topic r:id="Top/World"> and catid 3 which is the real one,
		// so catid 2 is just "Top". this is a bug in the dmoz output 
		// i think, so fix it here.
		if ( catid == 2 ) {
			nameLen = 3;
			gbmemcpy(&nameBuffer[nameOffset],"Top",nameLen); 
			nameBufferLen += nameLen;
		}
		else {
			gbmemcpy(&nameBuffer[nameOffset], htmlDecoded, nameLen);
			nameBufferLen  += nameLen;
		}
		// . fill the current cat
		//   make sure there's room
		if (numRdfCats >= rdfCatsSize) {
			rdfCatsSize += CAT_BUFFER_SIZE;
			rdfCats = (RdfCat*)realloc((void*)rdfCats,
						   sizeof(RdfCat)*rdfCatsSize);
			printf("rdfCats: %"INT32" bytes\n", rdfCatsSize);
			if (!rdfCats) {
				printf("Out of Memory\n");
				goto errExit1;
			}
		}
		// hash the name to the catid
		if (addCatHash ( nameOffset, nameLen, catid ) == -1) {
			printf("Out of Memory!\n");
			goto errExit1;
		}
		// debug
		//printf("gbcat=");
		//for ( int32_t i = 0 ; i < nameLen ; i++ )
		//	printf("%c",htmlDecoded[i]);
		//printf("\n");
		// fill it
		rdfCats[numRdfCats].m_catid           = catid;
		rdfCats[numRdfCats].m_parentid        = 0;
		//rdfCats[numRdfCats].m_numSymParents   = 0;
		//rdfCats[numRdfCats].m_symParents      = NULL;
		rdfCats[numRdfCats].m_nameLen         = nameLen;
		rdfCats[numRdfCats].m_nameOffset      = nameOffset;
		rdfCats[numRdfCats].m_structureOffset = catOffset;
		rdfCats[numRdfCats].m_contentOffset   = 0;
		rdfCats[numRdfCats].m_catHash         = 0;
		rdfCats[numRdfCats].m_numUrls         = 0;
		numRdfCats++;
	}

fileEnd:
	// sort the cats by catid
	gbsort(rdfCats, numRdfCats, sizeof(RdfCat), catcomp);

	// dump out categories for category dump
	if ( mode == MODE_CATDUMP ) {
		char catTemp[16384];
		for ( int32_t i = 0; i < numRdfCats; i++ ) {
			//for (int32_t n = rdfCats[i].m_nameOffset;
			//  	  n < rdfCats[i].m_nameOffset +
			//	      rdfCats[i].m_nameLen; n++)
			//	printf("%c", nameBuffer[n]);
			//printf("\n");
			int32_t encLen = urlEncode(catTemp, 16383,
					&nameBuffer[rdfCats[i].m_nameOffset],
					rdfCats[i].m_nameLen);
			catTemp[encLen] = '\0';
			printf("http://dir.gigablast.com%s\n", &catTemp[3]);
		}
		close(rdfStream);
		goto goodEnd;
	}

	// . now we need to reparse the whole file again and
	//   parse out the children of each topic, this includes:
	//   <narrow>    hard links
	//   <narrow1>   hard links
	//   <narrow2>   hard links
	//   <letterbar> hard links
	//   <symbolic>  sym links
	//   <symbolic1> sym links
	//   <symbolic2> sym links
	//   </Topic> ends the topic
	
	// reset to the beginning of the file
	//rdfStream.clear();
	//rdfStream.seekg(0, ios::beg);
	if ( lseek(rdfStream, 0, SEEK_SET) < 0 ) {
		printf ( "Error Reseting RDF File\n" );
		goto errExit1;
	}
	// reset the buffer to the first block
	//rdfStream.read(rdfBuffer, RDFBUFFER_SIZE);
	//n      = rdfStream.gcount();
	n = read(rdfStream, rdfBuffer, RDFBUFFER_SIZE);
	if ( n <= 0 || n > RDFBUFFER_SIZE ) {
		printf("Error Reading %s\n", filename);
		goto errExit1;
	}
	rdfPtr = rdfBuffer;
	rdfEnd = &rdfBuffer[n];
	currOffset = 0;

	//
	// set m_parentid using structure.rdf.u8
	//

	// read and parse the file again
	printf("Building Hierarchy...\n");
	while (true) {
		// parse the next catid in the file, sequentially
		//if ( currOffset == 545468935 )
		//	printf("shit\n");
		int32_t catid = parseNextCatid();
		if (catid == -1)
			goto fileEnd1;
nextChildTag:
		// now go through the tags looking for what we want
		if (rdfNextTag() == -1)
			goto fileEnd1;
		// check it for one of the tags we're looking for
		int32_t parentType;
		if ( tagLen == 6 &&
		     strncmp ( tagRecfer, "/Topic", 6 ) == 0 )
			continue;
		else if ( tagLen == 6 && 
			  strncmp ( tagRecfer, "narrow", 6 ) == 0 )
			parentType = 1;
		else if ( tagLen == 7 &&
			  strncmp ( tagRecfer, "narrow1", 7 ) == 0 )
			parentType = 1;
		else if ( tagLen == 7 &&
			  strncmp ( tagRecfer, "narrow2", 7 ) == 0 )
			parentType = 1;
		else if ( tagLen == 9 &&
			  strncmp ( tagRecfer, "letterbar", 9 ) == 0 )
			parentType = 1;
//		else if ( tagLen == 8 &&
//			  strncmp ( tagRecfer, "symbolic", 8 ) == 0 )
//			parentType = 2;
//		else if ( tagLen == 9 &&
//			  strncmp ( tagRecfer, "symbolic1", 9 ) == 0 )
//			parentType = 2;
//		else if ( tagLen == 9 &&
//			  strncmp ( tagRecfer, "symbolic2", 9 ) == 0 )
//			parentType = 2;
		else
			goto nextChildTag;
		// will only reach here if we're at a child cat
		// get the name, use the end of nameBuffer
		char *childName    = &nameBuffer[nameBufferLen];
		int32_t childNameLen  = fillNextString();
		if (childNameLen == -1)
			goto fileEnd1;
		if (childNameLen == -2) {
			printf("Out of Memory!\n");
			goto errExit1;
		}
		// html decode it
		if (childNameLen > MAX_HTTP_FILENAME_LEN)
			childNameLen = MAX_HTTP_FILENAME_LEN;
		childNameLen = htmlDecode ( htmlDecoded,
					    childName,
					    childNameLen ,
					    false,
					    0);
		gbmemcpy(childName, htmlDecoded, childNameLen);

		// debug log
		//if ( currOffset >= 506362430 ) // 556362463
		//	printf("off=%"INT32"\n",currOffset);
		// debug point
		//if ( currOffset == 545467573 )
		//	printf("GOT DEBUG POINT before giant skip\n");

		// cut off the leading label if symbolic
//		if (parentType == 2) {
//			while (*childName != ':') {
//				childName++;
//				childNameLen--;
//			}
//			childName++;
//			childNameLen--;
//		}
		// debug point
		//if (strcmp(childName,"Top/World/Català/Arts") == 0 )
		//	printf("hey\n");
		// get the catid for the child
		int32_t childid = getCatHash(childName, childNameLen);
		// get the cat for this id
		int32_t cat = getIndexFromId(childid);
		// make sure we have a match
		if (cat == -1) {
			// debug. why does Top/World/Catala/Arts
			// not have a parent??
			printf("Warning: Child Topic Not Found: ");
			for (int32_t i = 0; i < childNameLen; i++)
				printf("%c", childName[i]);
			printf("\n");
			m++;
			goto nextChildTag;
		}
		// . assign the parent to the cat
		// . this means we are in a "child" tag within the "catid"
		// . catid 84192 
		if (parentType == 1) {
			if (rdfCats[cat].m_parentid != 0)
				printf("Warning: Overwriting Parent Id!\n");
			rdfCats[cat].m_parentid = catid;
			t++;
		}
		// assign symbolic parent to the cat
//		else if (parentType == 2) {
//			// grow the buffer
//			rdfCats[cat].m_numSymParents++;
//			rdfCats[cat].m_symParents = (int32_t*)realloc(
//					rdfCats[cat].m_symParents,
//					sizeof(int32_t)*rdfCats[cat].m_numSymParents);
//			if (!rdfCats[cat].m_symParents) {
//				printf("Out of Memory!\n");
//				goto errExit1;
//			}
//			// assign the sym parent
//			rdfCats[cat].m_symParents[rdfCats[cat].m_numSymParents-1] = catid;
//			// inc overall number of sym parents
//			numSymParents++;
//		}
		// go to the next tag
		goto nextChildTag;
	}

fileEnd1:
	printf("Completed Structure:\n");
	printf("  Total Topics:                  %"INT32"\n", numRdfCats);
	printf("  Topics with Parents:           %"INT32"\n", t);
	printf("  Topics Linked but Nonexistent: %"INT32"\n", m);

	if ( t != numRdfCats ) {
		printf("\n"
		       "  *Topics without parents is bad because they\n"
		       "   can not have their entired rawPath printed out\n"
		       "   in order to get their proper hash\n");
	}

	//printf("  Number of Symbolic Links:      %"INT32"\n", numSymParents);
	printf("\n");

	// clear the hash table
	clearHashTable();
	// close the structure file
	//rdfStream.clear();
	//rdfStream.close();
	close(rdfStream);

	printf("Truncating Category Names...\n");
	// . truncate the category names to the last directory
	//   also calculate the size of the truncated buffer
	for (int32_t i = 0; i < numRdfCats; i++) {
		// find the position of the last /
		newOffset = rdfCats[i].m_nameOffset +
				 rdfCats[i].m_nameLen - 1;
		while ( newOffset != rdfCats[i].m_nameOffset &&
			nameBuffer[newOffset-1] != '/' )
			newOffset--;
		// assign the new length and offset
		rdfCats[i].m_nameLen -= newOffset - rdfCats[i].m_nameOffset;
		rdfCats[i].m_nameOffset = newOffset;
		newNameBufferSize += rdfCats[i].m_nameLen;
	}

	printf("Creating Category Hashes...\n");
	// make the hashes
	char rawPath[4096];
	int32_t rawPathLen;
	for (int32_t i = 0; i < numRdfCats; i++) {
		// get the hash of the path
		rawPathLen = printCatPath(rawPath, rdfCats[i].m_catid, true);
		// crap, this rawpath contains "Top/" in the beginning
		// but the rdfCats[i].m_nameOffset refers to a name
		// that does not include "Top/"
		rdfCats[i].m_catHash = hash32Lower_a(rawPath, rawPathLen, 0);
		// fix. so that xyz/Arts does not just hash "Arts"
		// because it has no parent...
		if ( rdfCats[i].m_parentid == 0 ) {
			printf("Missing parent for catid %"INT32". Will be "
			       "excluded from DMOZ so we avoid hash "
			       "collisions.\n",rdfCats[i].m_catid);
		}
		//
		// DEBUG!
		// print this shit out to find the collisions
		//
		continue;
		printf("hash32=%"UINT32" catid=%"INT32" parentid=%"INT32" path=%s\n",
		       rdfCats[i].m_catHash,
		       rdfCats[i].m_catid,
		       rdfCats[i].m_parentid,
		       rawPath);
	}

	// . now we want to serialize the needed data into
	//   one (or more?) file(s) to be quickly read by gb
	if ( mode == MODE_NEW )
		sprintf(filename, "%s%s", dir,STRUCTURE_OUTPUT_FILE);
	else
		sprintf(filename, "%s%s.new", dir,STRUCTURE_OUTPUT_FILE);
	//outStream.open(filename, ofstream::out|ofstream::trunc);
	outStream = open ( filename, O_CREAT|O_WRONLY|O_TRUNC,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
	// make sure it opened okay
	//if (!outStream.is_open()) {
	if ( outStream < 0 ) {
		printf("Error Opening %s\n", filename);
		goto errExit;
	}
	printf("\nOpened %s for writing.\n", filename);

	// write the size of the truncated name buffer
	//outStream.write((char*)&newNameBufferSize, sizeof(int32_t));
	if (write(outStream, &newNameBufferSize, sizeof(int32_t)) !=
			sizeof(int32_t)) {
		printf("Error writing to %s\n", filename);
		goto errExit;
	}
	// write the number of cats
	//outStream.write((char*)&numRdfCats, sizeof(int32_t));
	if (write(outStream, &numRdfCats, sizeof(int32_t)) !=
			sizeof(int32_t)) {
		printf("Error writing to %s\n", filename);
		goto errExit;
	}
	// write the number of symbolic parents
	//outStream.write((char*)&numSymParents, sizeof(int32_t));
	// write the truncated buffer and further reassign the offsets
	newOffset = 0;
	for (int32_t i = 0; i < numRdfCats; i++) {
		int32_t writeSize = rdfCats[i].m_nameLen;
		//outStream.write((char*)&nameBuffer[rdfCats[i].m_nameOffset],
		//		sizeof(char)*rdfCats[i].m_nameLen);
		if ( write ( outStream, &nameBuffer[rdfCats[i].m_nameOffset],
			     writeSize ) != writeSize ) {
			printf("Error writing to %s\n", filename);
			goto errExit;
		}
		rdfCats[i].m_nameOffset = newOffset;
		newOffset += rdfCats[i].m_nameLen;
	}

	// close the output file
	//outStream.clear();
	//outStream.close();
	close(outStream);
	printf("Completed Writing File.\n");

	// clear up the name buffer
	free(nameBuffer);
	nameBuffer = NULL;

contentParse:
	// . now we need to parse up the content file,
	//   hash the url's with a gb hash, and store the
	//   catid associated with each
	t = 0;
	m = 0;
	
	// creat the url buffer
	urlBufferSize = URL_BUFFER_SIZE;
	urlBuffer = (char*)malloc(sizeof(char)*urlBufferSize);
	if (!urlBuffer) {
		printf("Out of Memory!\n");
		goto errExit;
	}

	// create the url info buffer
	urlInfosSize = URLINFO_BUFFER_SIZE;
	urlInfos = (UrlInfo*)malloc(sizeof(UrlInfo)*urlInfosSize);
	if (!urlInfos) {
		printf("Out of Memory!\n");
		goto errExit;
	}

 again:	
	// open the content file
	if ( mode == MODE_NEW ||  mode == MODE_URLDUMP )
		sprintf(filename, "%s%s", dir,RDFCONTENT_FILE);
	else
		sprintf(filename, "%s%s.new", dir,RDFCONTENT_FILE);
	//rdfStream.open(filename, ifstream::in);
	rdfStream = open ( filename, O_RDONLY );
	// make sure it opened okay
	//if (!rdfStream.is_open()) {
	if ( rdfStream < 0 ) {
		if ( ! dir[0] ) {
			dir = "./catdb/";
			goto again;
		}
		printf("Error Opening %s\n", filename);
		goto errExit;
	}
	printf("\nOpened Content File: %s\n", filename);

	// take the first chunk
	//rdfStream.read(rdfBuffer, RDFBUFFER_SIZE);
	//n      = rdfStream.gcount();
	n = read ( rdfStream, rdfBuffer, RDFBUFFER_SIZE );
	if ( n <= 0 || n > RDFBUFFER_SIZE ) {
		printf("Error Reading %s\n", filename);
		goto errExit;
	}
	rdfPtr = rdfBuffer;
	rdfEnd = &rdfBuffer[n];
	currOffset = 0;

	// init hash tables for indexing urls
	initUrlHashTable();

	if ( mode == MODE_URLDUMP || mode == MODE_DIFFURLDUMP ) {
		// write another file for the urls
		if ( mode == MODE_URLDUMP ) {
			if (!splitUrls)
				sprintf(filename, "html/%s", URLTEXT_OUTPUT_FILE);
			else
				// put them directly into html/ now for
				// easy add url'ing
				sprintf(filename, "html/%s.0", URLTEXT_OUTPUT_FILE);
		}
		else {
			if (!splitUrls)
				sprintf(filename, "html/%s",
					DIFFURLTEXT_OUTPUT_FILE);
			else
				sprintf(filename, "html/%s.0",
					DIFFURLTEXT_OUTPUT_FILE);
		}
		//outStream2.open(filename, ofstream::out|ofstream::trunc);
		outStream2 = open ( filename, O_CREAT|O_WRONLY|O_TRUNC,
					S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
		// make sure it opened okay
		//if (!outStream2.is_open()) {
		if ( outStream2 < 0 ) {
			printf("Error Opening %s\n", filename);
			goto errExit1;
		}
		printf("Opened %s for writing.\n", filename);

		writeMetaTags ( outStream2 );

		// if we're doing a diffurldump, load up the diff file first
		if ( mode == MODE_DIFFURLDUMP ) {
			char diffUrl[MAX_URL_LEN*2];
			int32_t numRemoveUrls = 0;
			// open the new diff file
			//ifstream diffInStream;
			int diffInStream;
			sprintf(filename, "gbdmoz.content.dat.new.diff");
			//diffInStream.open(filename, ifstream::in);
			diffInStream = open(filename, O_RDONLY);
			//if (!diffInStream.is_open()) {
			if ( diffInStream < 0 ) {
				printf("Error Opening %s\n", filename);
				goto errExit;
			}
			printf("Opened Diff File: %s\n", filename);
	
			// read in the number of urls to update/add
			//diffInStream.read((char*)&numUpdateIndexes,
			//		sizeof(int32_t));
			if ( fileRead ( diffInStream,
					 &numUpdateIndexes,
					 sizeof(int32_t) ) != sizeof(int32_t) ) {
				printf("Error Reading %s\n", filename);
				goto errExit;
			}
			// read in the number of urls to remove
			//diffInStream.read((char*)&numRemoveUrls, sizeof(int32_t));
			if ( fileRead ( diffInStream,
					 &numRemoveUrls,
					 sizeof(int32_t) ) != sizeof(int32_t) ) {
				printf("Error Reading %s\n", filename);
				goto errExit;
			}
			// create the buffer for the update/add indexes
			updateIndexes = (int32_t*)malloc(
					sizeof(int32_t)*numUpdateIndexes);
			if ( !updateIndexes ) {
				printf("Out of Memory!\n");
				//diffInStream.clear();
				//diffInStream.close();
				close(diffInStream);
				goto errExit;
			}
			// read in the update/add indexes
			//for ( int32_t i = 0; i < numUpdateIndexes &&
			//	  	diffInStream.good(); i++ ) {
			for ( int32_t i = 0; i < numUpdateIndexes; i++ ) {
				//diffInStream.read((char*)&updateIndexes[i],
				//	  	sizeof(int32_t));
				int32_t n = fileRead ( diffInStream,
						&updateIndexes[i],
						sizeof(int32_t) );
				if ( n < 0 || n > (int32_t)sizeof(int32_t) ) {
					printf("Error Reading%s\n", filename);
					goto errExit;
				}
				if ( n == 0 )
					break;
			}
			// read in the urls to remove
			//for ( int32_t i = 0; i < numRemoveUrls &&
			//	  	diffInStream.good(); i++ ) {
			for ( int32_t i = 0; i < numRemoveUrls; i++ ) {
				int16_t urlLen;
				//diffInStream.read((char*)&urlLen,
				//		sizeof(int16_t));
				if ( fileRead(diffInStream, &urlLen,
					  sizeof(int16_t)) != sizeof(int16_t) ) {
					printf("Error reading diffInStream\n");
					goto errExit;
				}
				if ( urlLen <= 0 ) {
					printf("WARNING: Found %"INT32" length"
					       "url exiting!", (int32_t)urlLen);
					//diffInStream.clear();
					//diffInStream.close();
					close(diffInStream);
					goto errExit;
				}
				// read it in
				//diffInStream.read(diffUrl, urlLen);
				if ( fileRead(diffInStream, diffUrl, urlLen) !=
						urlLen ) {
					printf("Error reading diffInStream\n");
					goto errExit;
				}
				// normalize it
				urlLen = fixUrl(diffUrl, urlLen);
				// write it out to the diffurl file
				//outStream2.write(diffUrl, urlLen);
				if ( write(outStream2, diffUrl, urlLen) !=
						urlLen ) {
					printf("Error writing to outStream2\n");
					goto errExit;
				}
				//outStream2.write("\n", 1);
				if ( write(outStream2, "\n", 1) != 1 ) {
					printf("Error writing to outStream2\n");
					goto errExit;
				}
				urlTxtCount++;

				if ( splitUrls && 
				     urlTxtCount >= MAX_URLTXT_SIZE) {
					//outStream2.clear();
					//outStream2.close();
					close(outStream2);
					printf("Completed Writing File.\n");
					// write another file for the urls
					urlTxtFile++;
					sprintf(filename, "html/%s.%"INT32"",
						URLTEXT_OUTPUT_FILE,
						urlTxtFile);
					//outStream2.open(filename,
					//	ofstream::out|ofstream::trunc);
					outStream2 = open ( filename,
					  O_CREAT|O_WRONLY|O_TRUNC,
					  S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
					// make sure it opened okay
					//if (!outStream2.is_open()) {
					if ( outStream2 < 0 ) {
						printf("Error Opening %s\n",
						       filename);
						goto errExit1;
					}
					printf("Opened %s for writing.\n",
					       filename);
					urlTxtCount = 0;
				}

			}
			// close up the diff file
			//diffInStream.clear();
			//diffInStream.close();
			close(diffInStream);
			printf("Successfully Built Diff\n");
		}
	}
	else {
		if ( mode == MODE_NEW )
			sprintf(filename, "%s%s", dir,CONTENT_OUTPUT_FILE);
		else
			sprintf(filename, "%s%s.new", dir,CONTENT_OUTPUT_FILE);
		// stream the urls into the content
		//outStream.open(filename, ofstream::out|ofstream::trunc);
		outStream = open ( filename, O_CREAT|O_WRONLY|O_TRUNC,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
		// make sure it opened okay
		//if (!outStream.is_open()) {
		if ( outStream < 0 ) {
			printf("Error Opening %s\n", filename);
			goto errExit;
		}
		printf("Opened %s for writing.\n", filename);

		// store a space for the number of urls at the start of the file
		//outStream.write((char*)&numUrlInfos, sizeof(int32_t));
		if ( write(outStream, &numUrlInfos, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing to %s", filename);
			goto errExit;
		}
	}

	// read and parse the file again
	printf("Building Links...\n");
	while (true) {
		// parse for <Topic...
		if (rdfParse("Topic") == -1)
			goto fileEnd2;
		// the offset for this cat is 6 chars back
		uint32_t catOffset = currOffset - 6;
		// parse the next catid
		int32_t catid = parseNextCatid();
		if (catid == -1)
			goto fileEnd2;
		int32_t cat;
		// skip ahead for url dump
		if ( mode == MODE_URLDUMP || mode == MODE_DIFFURLDUMP )
			goto nextLink;
		// . set the content offset for this cat
		// . it's missing catid 425187... why? because it had
		//   a double quote in it like '4"'!! so i took out inQuotes
		//   logic above.
		cat = getIndexFromId(catid);
		if (cat == -1) {
			totalNEC++;
			printf("Warning: Nonexistent Category, %"INT32", found in "
			       "Content\n", catid );
			continue;
		}
		rdfCats[cat].m_contentOffset = catOffset;
nextLink:
		// get the next tag
		if (rdfNextTag() == -1)
			goto fileEnd2;
		// check it for one of the tags we're looking for
		if ( tagLen == 6 &&
		     strncmp ( tagRecfer, "/Topic", 6 ) == 0 )
			continue;
		else if ( tagLen == 4 &&
			  strncmp ( tagRecfer, "link", 4 ) == 0 )
			goto hashLink;
		else if ( tagLen == 5 &&
			  strncmp ( tagRecfer, "link1", 5 ) == 0 )
			goto hashLink;
		else if ( tagLen == 4 &&
			  strncmp ( tagRecfer, "atom", 4 ) == 0 )
			goto hashLink;
		else if ( tagLen == 3 &&
			  strncmp ( tagRecfer, "pdf", 3 ) == 0 )
			goto hashLink;
		else if ( tagLen == 4 &&
			  strncmp ( tagRecfer, "pdf1", 4 ) == 0 )
			goto hashLink;
		else if ( tagLen == 3 &&
			  strncmp ( tagRecfer, "rss", 3 ) == 0 )
			goto hashLink;
		else if ( tagLen == 4 &&
			  strncmp ( tagRecfer, "rss1", 4 ) == 0 )
			goto hashLink;
		else
			goto nextLink;
hashLink:
		// . hash the link with the catid
		// get the link url
		int32_t  urlOffset = urlBufferLen;
		int16_t urlLen    = fillNextUrl();
		if (urlLen == -1)
			goto fileEnd2;
		if (urlLen == -2) {
			printf("Out of Memory!\n");
			goto errExit1;
		}
		// html decode the url
		if (urlLen > MAX_URL_LEN)
			urlLen = MAX_URL_LEN;
		urlLen = htmlDecode(decodedUrl, &urlBuffer[urlOffset], urlLen,
				    false,0);
		// debug point
		//if ( strcmp(decodedUrl,"http://twitter.com/#!/ronpaul")==0)
		//	printf("hey\n");

		// ignore any url with # in it for now like
		// http://twitter.com/#!/ronpaul because it bastardizes
		// the meaning of the # (hashtag) and we need to protest that
		if ( strchr ( decodedUrl , '#' ) )
			goto nextLink;

		gbmemcpy(&urlBuffer[urlOffset], decodedUrl, urlLen);
		// fix up bad urls
		urlLen = fixUrl(&urlBuffer[urlOffset], urlLen);
		if (urlLen == 0)
			goto nextLink;
		// . normalize with Url
		// . watch out for
		//   http://twitter.com/#!/ronpaul to http://www.twitter.com/
		//   so do not strip # hashtags
		normUrl.set(&urlBuffer[urlOffset], 
			    urlLen,
			    true, // addwww?
			    false, // stripsessionid
			    false, // strippound?
			    true); // stripcommonfile? (i.e. index.htm)
		// debug print
		//printf("gburl %s -> %s\n",decodedUrl,normUrl.getUrl());
		// put it back
		urlLen = normUrl.getUrlLen();
		if (urlBufferLen+urlLen+10 >= urlBufferSize) {
			urlBufferSize += URL_BUFFER_SIZE;
			urlBuffer = (char*)realloc((void*)urlBuffer,
						    sizeof(char)*urlBufferSize);
			printf("urlBuffer: %"INT32" bytes\n", urlBufferSize);
			if (!urlBuffer)
				goto errExit1;
		}
		gbmemcpy(&urlBuffer[urlOffset], normUrl.getUrl(), urlLen);
		// run it through the fixer once more
		urlLen = fixUrl(&urlBuffer[urlOffset], urlLen);
		if (urlLen == 0)
			goto nextLink;
		// check the url to make sure it is all valid characters
		if (!isGoodUrl(&urlBuffer[urlOffset], urlLen))
			goto nextLink;
		// if good, add it to the buffer and add the cat
		//urlBufferLen += urlLen;
		// get the hash value
		uint64_t urlHash =
			hash64Lower_a(&urlBuffer[urlOffset], urlLen, 0);
		//uint32_t urlHash2 =
		//	hash32Lower(&urlBuffer[urlOffset], urlLen, 0);
		// see if it's already indexed
		//int32_t urlIndex = getUrlHash(urlHash, urlOffset, urlLen);
		//int32_t urlIndex = getUrlHash(urlHash, urlHash2);
		//int32_t urlIndex = getUrlHash(urlHash, urlHash2
		//			   urlOffset, urlLen);
		int32_t urlIndex = getUrlHash(urlHash);
		if (urlIndex == -1) {
			if ( mode == MODE_URLDUMP ||
			     mode == MODE_DIFFURLDUMP ) {
				//outStream2.write((char*)&urlLen,
				//		   sizeof(int16_t));
				if ( mode != MODE_DIFFURLDUMP ||
				     currUrl == updateIndexes[currDiffIndex] ) {
					//outStream2.write(&urlBuffer[urlOffset],
					//		  urlLen);
					// print it in an anchor tag
					// now so gigablast can spider
					// these links
					write ( outStream2,"<a href=\"",9);
					if ( write ( outStream2,
						     &urlBuffer[urlOffset],
						     urlLen ) != urlLen ) {
						printf("Error writing to "
						       "outStream2\n");
						goto errExit1;
					}
					write ( outStream2,"\"></a>",6);
					//outStream2.write("\n", 1);
					if (write(outStream2, "\n", 1) != 1) {
						printf("Error writing to "
						       "outStream2\n");
						goto errExit1;
					}
					urlTxtCount++;
					currDiffIndex++;
				}
				currUrl++;

				if ( splitUrls && 
				     urlTxtCount >= MAX_URLTXT_SIZE) {
					//outStream2.clear();
					//outStream2.close();
					close(outStream2);
					printf("Completed Writing File.\n");
					// write another file for the urls
					urlTxtFile++;
					if ( mode == MODE_URLDUMP )
						sprintf(filename, "html/%s.%"INT32"",
							URLTEXT_OUTPUT_FILE,
							urlTxtFile);
					else
						sprintf(filename, "html/%s.%"INT32"",
							DIFFURLTEXT_OUTPUT_FILE,
							urlTxtFile);
					//outStream2.open(filename,
					//	ofstream::out|ofstream::trunc);
					outStream2 = open ( filename,
					  O_CREAT|O_WRONLY|O_TRUNC,
					  S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
					// make sure it opened okay
					//if (!outStream2.is_open()) {
					if ( outStream2 < 0 ) {
						printf("Error Opening %s\n",
						       filename);
						goto errExit1;
					}
					printf("Opened %s for writing.\n",
					       filename);
					writeMetaTags ( outStream2 );
					urlTxtCount = 0;
				}
			}
			else {
				// write the url to the content file
				//outStream.write((char*)&urlLen, sizeof(int16_t));
				if ( write(outStream, &urlLen, sizeof(int16_t)) !=
						sizeof(int16_t) ) {
					printf("Error writing to outStream");
					goto errExit1;
				}
				//outStream.write(&urlBuffer[urlOffset], urlLen);
				if ( write ( outStream,
					     &urlBuffer[urlOffset],
					     urlLen ) != urlLen ) {
					printf("Error writing to outStream");
					goto errExit1;
				}
			}
			// add the url info to the buffer
			if (numUrlInfos >= urlInfosSize) {
				urlInfosSize += URLINFO_BUFFER_SIZE;
				urlInfos = (UrlInfo*)realloc((void*)urlInfos,
					      sizeof(UrlInfo)*urlInfosSize);
				printf("urlInfos: %"INT32" bytes\n",
				      (int32_t)(urlInfosSize*sizeof(UrlInfo)));
				if (!urlInfos) {
					printf("Out of Memory!\n");
					goto errExit1;
				}
			}
			// fill the url info
			//urlInfos[numUrlInfos].m_hash = urlHash;
			//urlInfos[numUrlInfos].m_urlLen    = urlLen;
			//urlInfos[numUrlInfos].m_urlOffset = urlOffset;
			urlInfos[numUrlInfos].m_numCatids = 1;
			urlInfos[numUrlInfos].m_catids = 
				(int32_t*)malloc(sizeof(int32_t));
			if (!urlInfos[numUrlInfos].m_catids) {
				printf("Out of memory!\n");
				goto errExit1;
			}
			urlInfos[numUrlInfos].m_catids[0] = catid;
			// set changed to true so new urls get in the diff
			urlInfos[numUrlInfos].m_changed   = 1;
			// add it to the hash
			//if (addUrlHash(urlHash, numUrlInfos,
			//               urlOffset, urlLen) == -1) {
			//if (addUrlHash ( urlHash,
			//		   urlHash2,
			//		   numUrlInfos) == -1) {
			//if (addUrlHash(urlHash, urlHash2, numUrlInfos,
			//	       urlOffset, urlLen) == -1) {
			if (addUrlHash(urlHash, numUrlInfos) == -1) {
				printf("Out of Memory!\n");
				goto errExit1;
			}
			// next url info
			numUrlInfos++;
		}
		else {
			// make sure we aren't duping the catid
			for (int32_t i = 0; 
			          i < urlInfos[urlIndex].m_numCatids; i++)
				if (urlInfos[urlIndex].m_catids[i] == catid)
					goto nextLink;
			// add the catid
			int32_t numCatids = urlInfos[urlIndex].m_numCatids;
			//if (numCatids < MAX_URL_CATIDS) {
				urlInfos[urlIndex].m_catids = (int32_t*)realloc(
					urlInfos[urlIndex].m_catids,
					sizeof(int32_t) *
					(urlInfos[urlIndex].m_numCatids+1));
				if (!urlInfos[urlIndex].m_catids) {
					printf("Out of Memory!\n");
					goto errExit1;
				}
				urlInfos[urlIndex].m_catids[numCatids] = catid;
				urlInfos[urlIndex].m_numCatids++;

				if (urlInfos[urlIndex].m_numCatids > t) {
					t = urlInfos[urlIndex].m_numCatids;
					ti = urlIndex;
				}
			//}
			m++;
		}
		// skip increment for url dump
		if ( mode == MODE_URLDUMP || mode == MODE_DIFFURLDUMP )
			goto nextLink;

		// increment the url count for this cat and its parents
		int32_t currIndex = getIndexFromId(catid);
		while (currIndex >= 0) {
			rdfCats[currIndex].m_numUrls++;
			// the new dmoz files have catids whose parents
			// are the same cat id! so stop infinite loops
			if ( rdfCats[currIndex].m_parentid == 
			     rdfCats[currIndex].m_catid )
				break;
			// otherwise, make "currIndex" point to the parent
			currIndex = getIndexFromId(
					rdfCats[currIndex].m_parentid );
			// in the newer dmoz files 0 is a bad catid i guess
			// not -1 any more?
			// ??????
		}

		goto nextLink;
	}

fileEnd2:
	// close the output file
	if ( mode == MODE_URLDUMP || mode == MODE_DIFFURLDUMP ) {
		//outStream2.clear();
		//outStream2.close();
		close(outStream2);
		printf("Completed Writing File.\n");
	}
	else {
		//outStream.clear();
		//outStream.close();
		close(outStream);
		printf("Completed Writing File.\n");
	}

	printf("Completed Content:\n");
	printf("  Total Links:              %"INT32"\n", numUrlInfos);
	printf("  Duplicated Links:         %"INT32"\n", m);
	printf("  Max Link Duplicated:      %"INT32"\n", t);
	printf("  Nonexistant Categories:   %"INT32"\n", totalNEC );
	//printf("    ");
	//for (int32_t i = 0; i < urlInfos[ti].m_urlLen; i++)
	//	printf("%c", urlBuffer[urlInfos[ti].m_urlOffset + i]);
	printf("\n");
	printf("\n");

	// close the content file
	//rdfStream.clear();
	//rdfStream.close();
	close(rdfStream);

	// if we're updating, load up the old content here
	if ( mode == MODE_UPDATE ) {
	//if ( false ) {
		// fill the buffers
		int32_t currUrl = 0;
		int32_t urlp    = 0;
		int32_t catidp  = 0;
		bool oldErr  = false;
		int32_t oldNumUrls;
		char *oldUrls = NULL;
		int32_t oldUrlsBufferSize = OLDURL_BUFFER_SIZE;
		uint64_t *oldUrlHashes;
		char *removeOldUrl;
		//char oldUrl[MAX_URL_LEN*2];
		int32_t *oldCatids = NULL;
		int32_t oldCatidsBufferSize = OLDCATID_BUFFER_SIZE;
		unsigned char *oldNumCatids = NULL;
		int32_t numUpdateUrls = numUrlInfos;
		int32_t numRemoveUrls = 0;
		int32_t numChangedUrls = 0;
		int32_t updateIndexesWritten = 0;
		int32_t numIdsToUpdate = 0;

		// load the content and url files
		// url info (content) file
		sprintf(filename, "%s%s", dir,CONTENT_OUTPUT_FILE);
		//rdfStream.open(filename, ifstream::in);
		rdfStream = open ( filename, O_RDONLY );
		//if (!rdfStream.is_open()) {
		if ( rdfStream < 0 ) {
			printf("Error Opening %s\n", filename);
			goto oldErrExit;
		}
		// read in the number of urls
		//rdfStream.read((char*)&oldNumUrls, sizeof(int32_t));
		if (fileRead(rdfStream, &oldNumUrls, sizeof(int32_t)) !=
				sizeof(int32_t)) {
			printf("Error Reading %s\n", filename);
			goto oldErrExit;
		}
	
		// create the buffer for the urls and catids
		oldUrls = (char*)malloc(oldUrlsBufferSize);
		if (!oldUrls) {
			printf("Out of Memory!\n");
			goto oldErrExit;
		}
		oldUrlHashes = (uint64_t*)malloc (
				sizeof(int64_t)*oldNumUrls );
		if (!oldUrlHashes) {
			printf("Out of Memory!\n");
			goto oldErrExit;
		}
		removeOldUrl = (char*)malloc(oldNumUrls);
		if (!removeOldUrl) {
			printf("Out of Memory!\n");
			goto oldErrExit;
		}
		oldCatids = (int32_t*)malloc(sizeof(int32_t)*oldCatidsBufferSize);
		if (!oldCatids) {
			printf("Out of Memory!\n");
			goto oldErrExit;
		}
		oldNumCatids = (unsigned char*)malloc(oldNumUrls);
		if (!oldNumCatids) {
			printf("Out of Memory!\n");
			goto oldErrExit;
		}

		printf("Loading Old Content Data...\n");
		//while ( rdfStream.good() && currUrl < oldNumUrls ) {
		while ( currUrl < oldNumUrls ) {
			// read the next url
			int16_t urlLen = 0;
			//rdfStream.read((char*)&urlLen, sizeof(int16_t));
			int32_t n = fileRead(rdfStream, &urlLen, sizeof(int16_t));
			if ( n < 0 || n > (int32_t)sizeof(int16_t) ) {
				printf("Error Reading %s\n",filename);
				//CONTENT_OUTPUT_FILE);
				goto oldErrExit;
			}
			if ( n == 0 )
				break;
			// make sure there's room in the buffer
			if (urlp + urlLen + 4 >= oldUrlsBufferSize) {
				char *re_urls = (char*)realloc(
						oldUrls,
					       	oldUrlsBufferSize +
						   OLDURL_BUFFER_SIZE );
				if (!re_urls) {
					printf("Out of Memory!\n");
					goto oldErrExit;
				}
				oldUrls = re_urls;
				oldUrlsBufferSize += OLDURL_BUFFER_SIZE;
			}
			// insert a space between urls
			//oldUrls[urlp] = '\n';
			//urlp++;
			//char *url = &m_urls[urlp];
			//rdfStream.read(&oldUrls[urlp], urlLen);
			if (urlLen <= 0) {
				printf("WARNING: FOUND %"INT32" LENGTH URL, "
				       "WILL BE SKIPPED (1)\n",
				       (int32_t)urlLen );
			}
			n = fileRead(rdfStream, &oldUrls[urlp], urlLen);
			if ( n < 0 || n > urlLen ) {
				printf("Error Reading %s\n",filename);
				//CONTENT_OUTPUT_FILE);
				goto oldErrExit;
			}
			if ( n == 0 )
				break;
			//rdfStream.read(oldUrl, urlLen);
			// normalize it
			urlLen = fixUrl(&oldUrls[urlp], urlLen);
			// make the hash
			oldUrlHashes[currUrl] =
				hash64Lower_a(&oldUrls[urlp], urlLen, 0);
			removeOldUrl[currUrl] = 0;
			// increment the buffer pointer
			if (urlLen <= 0) {
				printf("WARNING: FOUND %"INT32" LENGTH URL, "
				       "WILL BE SKIPPED (2)\n",
				       (int32_t)urlLen );
			}
			urlp += urlLen;
			//urlLen = fixUrl(oldUrl, urlLen);
			// null terminate
			oldUrls[urlp] = '\0';
			urlp++;
			currUrl++;
		}
		currUrl = 0;
		//while ( rdfStream.good() && currUrl < oldNumUrls ) {
		while ( currUrl < oldNumUrls ) {
			// get the number of catids
			oldNumCatids[currUrl] = 0;
			//rdfStream.read((char*)&oldNumCatids[currUrl], 1);
			int32_t n = fileRead(rdfStream, &oldNumCatids[currUrl], 1);
			if ( n < 0 || n > 1 ) {
				printf("Error Reading %s\n",filename);
				//CONTENT_OUTPUT_FILE);
				goto oldErrExit;
			}
			if ( n == 0 )
				break;
			// make sure there's room
			if ( catidp + oldNumCatids[currUrl] + 1 >=
					oldCatidsBufferSize ) {
				int32_t *re_catids = (int32_t*)realloc(
					oldCatids,
					sizeof(int32_t)*(oldCatidsBufferSize+
						OLDCATID_BUFFER_SIZE) );
				if (!re_catids) {
					printf("Out of Memory!\n");
					goto oldErrExit;
				}
				oldCatids = re_catids;
				oldCatidsBufferSize += OLDCATID_BUFFER_SIZE;
			}
			//rdfStream.read((char*)&oldCatids[catidp],
			//	sizeof(int32_t)*oldNumCatids[currUrl]);
			int32_t readSize = sizeof(int32_t)*oldNumCatids[currUrl];
			n = fileRead(rdfStream, &oldCatids[catidp], readSize);
			if ( n < 0 || n > readSize ) {
				printf("Error Reading %s\n",filename);
				//CONTENT_OUTPUT_FILE);
				goto oldErrExit;
			}
			if ( n == 0 )
				break;
			// next url
			catidp += oldNumCatids[currUrl];
			currUrl++;
		}

		// now check the old urls against the new for changes
		catidp = 0;
		for ( int32_t i = 0; i < oldNumUrls; i++ ) {
			// check the new url hash for the old url
			int32_t n = oldNumCatids[i];
			// skip bad urls
			if ( oldUrlHashes[i] == 0 ) {
				printf("WARNING: FOUND 0 LENGTH URL, "
				       "SKIPPING\n" );
				catidp += n;
				continue;
			}
			int32_t urlIndex = getUrlHash(oldUrlHashes[i]);
			// check for a removed url
			if ( urlIndex == -1 ) {
				removeOldUrl[i] = 1;
				numRemoveUrls++;
				catidp += n;
				continue;
			}
			// check if we have the same number of catids
			if ( urlInfos[urlIndex].m_numCatids != n )
				goto oldIsDifferent;
			// check if all the catids match
			for ( int32_t co = 0; co < n; co++ ) {
				bool catMatch = false;
				for ( int32_t cn = 0; cn < n; cn++ ) {
					if ( urlInfos[urlIndex].m_catids[cn] ==
					     oldCatids[catidp + co] ) {
						catMatch = true;
						break;
					}
				}
				if ( !catMatch )
					goto oldIsDifferent;
			}
			// exact match, mark it unchanged and goto the next
			catidp += n;
			urlInfos[urlIndex].m_changed = 0;
			numUpdateUrls--;
			continue;
oldIsDifferent:
			// just go on, this is already marked as changed
			catidp += n;
			numChangedUrls++;
			continue;
		}
		printf("  Urls to Update:    %"INT32"\n", numChangedUrls);
		printf("  Urls to Add:       %"INT32"\n",
			numUpdateUrls - numChangedUrls);
		printf("  Urls to Remove:    %"INT32"\n", numRemoveUrls);

		//
		// . write out the diff file, contains new and changed urls and
		//   also urls to remove
		//
		// open the new diff file for writing
		sprintf(filename, "%s%s.new.diff", dir,CONTENT_OUTPUT_FILE);
		//outStream.open(filename, ofstream::out|ofstream::trunc);
		outStream = open ( filename, O_CREAT|O_WRONLY|O_TRUNC,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
		// make sure it opened okay
		//if (!outStream.is_open()) {
		if ( outStream < 0 ) {
			printf("Error Opening %s\n", filename);
			goto oldErrExit;
		}
		printf("\nOpened %s for writing.\n", filename);

		// write out the number of urls to update/add
		//outStream.write(&numUpdateUrls, sizeof(int32_t));
		if ( write(outStream, &numUpdateUrls, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing to %s\n", filename);
			goto oldErrExit;
		}
		// write out the number of urls to delete
		//outStream.write(&numRemoveUrls, sizeof(int32_t));
		if ( write(outStream, &numRemoveUrls, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing to %s\n", filename);
			goto oldErrExit;
		}
		// write out the urls to update/add
		for ( int32_t i = 0; i < numUrlInfos; i++ ) {
			if ( urlInfos[i].m_changed == 0 ) {
				continue;
			}
			// write the changed url info
			//outStream.write((char*)&urlInfos[i].m_urlLen,
			//		sizeof(int16_t));
			//outStream.write(&urlBuffer[urlInfos[i].m_urlOffset],
			//		sizeof(char)*urlInfos[i].m_urlLen);
			//outStream.write((char*)&urlInfos[i].m_numCatids,
			//		sizeof(char));
			//outStream.write((char*)urlInfos[i].m_catids,
			//		sizeof(int32_t)*urlInfos[i].m_numCatids);
			//outStream.write((char*)&i, sizeof(int32_t));
			if ( write(outStream, &i, sizeof(int32_t)) !=
					sizeof(int32_t) ) {
				printf("Error writing to outStream\n");
				goto oldErrExit;
			}
			updateIndexesWritten++;
			numIdsToUpdate += urlInfos[i].m_numCatids;
		}
		printf ( "Wrote %"INT32" urls and %"INT32" catids to update/add.\n",
			 updateIndexesWritten, numIdsToUpdate );
		if ( updateIndexesWritten != numUpdateUrls )
			printf ( "WARNING: Wrote %"INT32" Update Indexes, Should be"
				 "%"INT32"!", updateIndexesWritten, numUpdateUrls );
		// write out the urls to delete
		urlp = 0;
		for ( int32_t i = 0; i < oldNumUrls; i++ ) {
			int16_t oldUrlLen = gbstrlen(&oldUrls[urlp]);
			if ( removeOldUrl[i] == 0 ) {
				urlp += oldUrlLen + 1;
				continue;
			}
			// write the url to remove
			if ( oldUrlLen <= 0 )
				printf("WARNING: ATTEMPTING TO WRITE %"INT32" "
				       "LENGTH URL.\n", (int32_t)oldUrlLen );
			//outStream.write((char*)&oldUrlLen, sizeof(int16_t));
			if ( write(outStream, &oldUrlLen, sizeof(int16_t)) !=
					sizeof(int16_t) ) {
				printf("Error writing to outStream\n");
				goto oldErrExit;
			}
			//outStream.write((char*)&oldUrls[urlp], oldUrlLen);
			if ( write(outStream, &oldUrls[urlp], oldUrlLen) !=
					oldUrlLen ) {
				printf("Error writing to outStream\n");
				goto oldErrExit;
			}
			urlp += oldUrlLen + 1;
		}
		
		// close the file
		//outStream.clear();
		//outStream.close();
		close(outStream);
		printf("Completed Writing File.\n");
		printf("\n");
		
		// no error
		oldErr = false;
		goto oldGoodExit;
oldErrExit:
		// set error
		oldErr = true;
oldGoodExit:
		// close the file
		//rdfStream.clear();
		//rdfStream.close();
		close(rdfStream);
		// free the buffers
		if (oldUrls)      free(oldUrls);
		if (oldUrlHashes) free(oldUrlHashes);
		if (removeOldUrl) free(removeOldUrl);
		if (oldCatids)    free(oldCatids);
		if (oldNumCatids) free(oldNumCatids);

		if (oldErr) goto errExit;
	}

	printf("Clearing Url Hash Table...\n");
	// clear the url index hash
	clearUrlHashTable();

	// finish up if we're just dumping urls
	if ( mode == MODE_URLDUMP || mode == MODE_DIFFURLDUMP )
		goto goodEnd;
	
	// . now we want to serialize the needed data into
	//   one (or more?) file(s) to be quickly read by gb
	if ( mode == MODE_NEW )
		sprintf(filename, "%s%s", dir,STRUCTURE_OUTPUT_FILE);
	else
		sprintf(filename, "%s%s.new", dir,STRUCTURE_OUTPUT_FILE);
	//outStream.open(filename, ofstream::out|ofstream::ate);
	outStream = open ( filename, O_WRONLY|O_APPEND,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
	// make sure it opened okay
	//if (!outStream.is_open()) {
	if ( outStream < 0 ) {
		printf("Error Opening %s\n", filename);
		goto errExit;
	}
	printf("\nOpened %s for writing.\n", filename);

	// write the cats
	//outStream.write((char*)rdfCats, sizeof(RdfCat)*numRdfCats);
	for (int32_t i = 0; i < numRdfCats; i++) {
		//outStream.write((char*)&rdfCats[i].m_catid, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_catid, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
		//outStream.write((char*)&rdfCats[i].m_parentid, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_parentid, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
		//outStream.write((char*)&rdfCats[i].m_numSymParents, sizeof(int16_t));
		//outStream.write((char*)&rdfCats[i].m_nameOffset, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_nameOffset, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
		//outStream.write((char*)&rdfCats[i].m_nameLen, sizeof(int16_t));
		if ( write(outStream, &rdfCats[i].m_nameLen, sizeof(int16_t)) !=
				sizeof(int16_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
		//outStream.write((char*)&rdfCats[i].m_structureOffset, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_structureOffset,
			   sizeof(int32_t)) != sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
		//outStream.write((char*)&rdfCats[i].m_contentOffset, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_contentOffset,
			   sizeof(int32_t)) != sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
		//outStream.write((char*)&rdfCats[i].m_numUrls, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_numUrls, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
	}
	// write the symbolic parents
	//for (int32_t i = 0; i < numRdfCats; i++)
	//	for (int32_t s = 0; s < rdfCats[i].m_numSymParents; s++)
	//		outStream.write((char*)&rdfCats[i].m_symParents[s], sizeof(int32_t));
	// write the cat hashes
	for (int32_t i = 0; i < numRdfCats; i++) {
		//outStream.write((char*)&rdfCats[i].m_catHash, sizeof(int32_t));
		if ( write(outStream, &rdfCats[i].m_catHash, sizeof(int32_t)) !=
				sizeof(int32_t) ) {
			printf("Error writing cats to outStream.\n");
			goto errExit;
		}
	}
	// close the output file
	//outStream.clear();
	//outStream.close();
	close(outStream);
	printf("Completed Writing File.\n");

	// write another file for the urls
	if ( mode == MODE_NEW )
		sprintf(filename, "%s%s", dir,CONTENT_OUTPUT_FILE);
	else
		sprintf(filename, "%s%s.new", dir,CONTENT_OUTPUT_FILE);
	//outStream.open(filename, ofstream::out|ofstream::ate);
	outStream = open ( filename, O_WRONLY,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
	//outStream.open(filename, ofstream::out|ofstream::trunc);
	//endpos = outStream.tellp();
	// make sure it opened okay
	//if (!outStream.is_open()) {
	if ( outStream < 0 ) {
		printf("Error Opening %s\n", filename);
		goto errExit;
	}
	printf("\nOpened %s for writing.\n", filename);

	//outStream.seekp(0);
	lseek(outStream, 0, SEEK_SET);
	// write the number of urls at the start of the file
	//outStream.write((char*)&numUrlInfos, sizeof(int32_t));
	if ( write(outStream, &numUrlInfos, sizeof(int32_t)) != sizeof(int32_t) ) {
		printf("Error writing to outStream\n");
		goto errExit;
	}
	// seek to the end
	//outStream.seekp(endpos);
	lseek(outStream, 0, SEEK_END);
	// write the urls
	for (int32_t i = 0; i < numUrlInfos; i++) {
		//outStream.write((char*)&urlInfos[i].m_hash, sizeof(int64_t));
		//outStream.write((char*)&urlInfos[i].m_urlLen, sizeof(int16_t));
		//outStream.write(&urlBuffer[urlInfos[i].m_urlOffset],
		//		sizeof(char)*urlInfos[i].m_urlLen);
		//outStream.write((char*)&urlInfos[i].m_numCatids, sizeof(char));
		if ( write(outStream, &urlInfos[i].m_numCatids, sizeof(char)) !=
				sizeof(char) ) {
			printf("Error writing to outStream\n");
			goto errExit;
		}
		//outStream.write((char*)urlInfos[i].m_catids, sizeof(int32_t)*
		//		urlInfos[i].m_numCatids);
		int32_t writeSize = sizeof(int32_t)*urlInfos[i].m_numCatids;
		if ( write(outStream, urlInfos[i].m_catids, writeSize) !=
				writeSize ) {
			printf("Error writing to outStream\n");
			goto errExit;
		}
	}

	// close the output file
	//outStream.clear();
	//outStream.close();
	close(outStream);
	
	printf("Completed Writing File.\n\n");

goodEnd:
	// free up the buffers
	if (urlBuffer)
		free(urlBuffer);
	if (urlInfos) {
		for (int32_t i = 0; i < numUrlInfos; i++) {
			if (urlInfos[i].m_catids)
				free(urlInfos[i].m_catids);
		}
		free(urlInfos);
	}
	//free(nameBuffer);
	if (rdfCats)
		free(rdfCats);
	if (rdfBuffer)
		free(rdfBuffer);
	// success
	return 0;

	// error exit points
errExit1:
	clearUrlHashTable();
	clearHashTable();
	//rdfStream.clear();
	//rdfStream.close();
	close(rdfStream);
errExit:
	if (updateIndexes)
		free(updateIndexes);
	if (urlBuffer)
		free(urlBuffer);
	if (urlInfos) {
		for (int32_t i = 0; i < numUrlInfos; i++) {
			if (urlInfos[i].m_catids)
				free(urlInfos[i].m_catids);
		}
		free(urlInfos);
	}
	if (nameBuffer)
		free(nameBuffer);
	if (rdfCats)
		free(rdfCats);
	if (rdfBuffer)
		free(rdfBuffer);
	// failure
	return 1;
}
