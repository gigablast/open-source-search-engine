//
// Gigablast, Copyright March 2005
// Author: Javier Olivares <jolivares@gigablast.com>
//
// Stores Categories in a Hierarchy
// Based on DMOZ
//

#ifndef _CATEGORY_H_
#define _CATEGORY_H_

#include "Mem.h"
#include "HashTable.h"

#define RDFBUFFER_SIZE      (1024*1024*100)
#define RDFSMALLBUFFER_SIZE (32*1024)
#define RDFSTRUCTURE_FILE "structure.rdf.u8"
#define RDFCONTENT_FILE   "content.rdf.u8"

#define STRUCTURE_OUTPUT_FILE  "gbdmoz.structure.dat"
#define CONTENT_OUTPUT_FILE    "gbdmoz.content.dat"
#define URL_OUTPUT_FILE        "gbdmoz.urls.dat"
#define URLTEXT_OUTPUT_FILE    "gbdmoz.urls.txt"

#define MAX_CATID_LEN    63
#define MAX_TAG_LEN      127
#define MAX_URL_CATIDS   64
#define MAX_URLTXT_SIZE  500000
#define MAX_CATIDS       96
#define MAX_CATNAME_LEN  1024

#define HASHTABLE_SIZE    (1024*1024)
#define URLHASHTABLE_SIZE (10*1024*1024)

#define MAX_SUB_CATS       1024
#define SUBCAT_LETTERBAR   10
#define SUBCAT_NARROW2     30
#define SUBCAT_SYMBOLIC2   31
#define SUBCAT_NARROW1     50
#define SUBCAT_SYMBOLIC1   51
#define SUBCAT_NARROW      70
#define SUBCAT_SYMBOLIC    71
#define SUBCAT_RELATED     90
#define SUBCAT_ALTLANG     110

struct Category {
	int32_t  m_catid;
	int32_t  m_parentid;
	//int16_t m_numSymParents;
	//int32_t  m_symParentsOffset;
	int32_t  m_nameOffset;
	int16_t m_nameLen;
	uint32_t m_structureOffset;
	uint32_t m_contentOffset;
	int32_t  m_numUrls;
};

struct CategoryHash {
	uint32_t  m_hash;
	int32_t m_catIndex;
};

struct SubCategory {
	//int32_t  m_prefixOffset;
	int32_t  m_prefixLen;
	//int32_t  m_nameOffset;
	int32_t  m_nameLen;
	char  m_type;
	int32_t getRecSize () { return sizeof(SubCategory)+m_prefixLen+m_nameLen+2;};
	char *getPrefix() { return m_buf; };
	char *getName  () { return m_buf+m_prefixLen+1;};
	char  m_buf[0];
};

class Categories {
public:
	Categories();
	~Categories();

	int32_t fileRead ( int fileid, void *buf, size_t count );

	void reset();

	// load the hierarchy from a file
	int32_t loadCategories ( char *filename );

	// . this is called by loadCategories() and constructs m_adultTable
	// . it will load/save it from/to disk, too
	bool makeBadHashTable ( ) ;
	bool addUrlsToBadHashTable ( int32_t catid ) ;

	// get the index of a cat from its id
	// -1 if not found
	int32_t getIndexFromId   ( int32_t catid );
	int32_t getIndexFromPath ( char *str, int32_t strLen );
	int32_t getIdFromPath    ( char *str, int32_t strLen );

	// determine if a category should be printed RTL
	bool isIdRTLStart    ( int32_t catid );
	bool isIndexRTLStart ( int32_t catIndex );
	bool isIdRTL         ( int32_t catid );
	bool isIndexRTL      ( int32_t catIndex );

	// see if the category is Adult
	bool isIdAdultStart    ( int32_t catid );
	bool isIndexAdultStart ( int32_t catIndex );
	bool isIdAdult         ( int32_t catid );
	bool isIndexAdult      ( int32_t catIndex );

	// is it in a bad cat, like adult, gambling, online pharmacies
	bool isIdBadStart    ( int32_t catid );
	bool isIndexBadStart ( int32_t catIndex );
	bool isIdBad         ( int32_t catid );
	bool isIndexBad      ( int32_t catIndex );
	// is this url directly in a dmoz adult category?
	bool isInBadCat      ( Url *u ) ;
	bool isInBadCat      ( uint32_t urlHash );

	// print info of cats
	void printCats ( int32_t start, int32_t end );

	// print the path of this category
	void printPathFromId ( SafeBuf *sb ,
			       int32_t  catid,
			       bool  raw = false,
			       bool  isRTL = false );
	void printPathFromIndex ( SafeBuf *sb ,
				  int32_t  catIndex,
				  bool  raw = false,
				  bool  isRTL = false );

	// print the path bread crumb links for this category
	void printPathCrumbFromId    ( SafeBuf *sb ,
				       int32_t  catid,
				       bool  isRTL = false );
	void printPathCrumbFromIndex ( SafeBuf *sb ,
				       int32_t  catid,
				       bool  isRTL = false );

	bool printUrlsInTopic ( class SafeBuf *sb , int32_t catid  ) ;

	// . get the title and summary for a specific url
	//   and catid
	bool getTitleAndSummary ( char  *url,
				  int32_t   urlLen,
				  int32_t   catid,
				  char  *title        = NULL,
				  int32_t  *titleLen     = NULL,
				  int32_t   maxTitleLen  = 0,
				  char  *summ         = NULL,
				  int32_t  *summLen      = NULL,
				  int32_t   maxSummLen   = 0,
				  char  *anchor       = NULL,
				  unsigned char *anchorLen    = NULL,
				  int32_t   maxAnchorLen = 0 ,
				  int32_t   niceness     = 0 ,
				  bool   justAddToTable = false );

	// normalize a url string
	int32_t fixUrl ( char *url, int32_t urlLen );

	// . generate sub categories for a given catid
	// . store list of SubCategories into "subCatBuf" return # stored
	// . hits disk without using threads... so kinda sucks...
	int32_t generateSubCats ( int32_t catid, SafeBuf *subCatBuf );

	int32_t getNumUrlsFromIndex ( int32_t catIndex ) {
		if ( ! m_cats ) return 0;
		return m_cats[catIndex].m_numUrls; };

	// creates a directory search request url
	//void createDirectorySearchUrl ( Url  *url,
	int32_t createDirSearchRequest ( char *requestBuf,
				      int32_t  requestBufSize,
				      int32_t  catid,
				      char *hostname,
				      int32_t  hostnameLen,
				      char *coll,
				      int32_t  collLen,
				      char *cgi ,//= NULL,
				      int32_t  cgiLen ,//= 0,
				      bool  cgiFromRequest ,//= false ,
				      class HttpRequest *r );

	bool initLangTables(void);
	bool loadLangTables(void);
	uint8_t findLanguage(char *addr);

	// Categories
	Category *m_cats;
	int32_t      m_numCats;

	// name buffer
	char *m_nameBuffer;
	int32_t  m_nameBufferSize;

	// symbolic parent buffer
	//int32_t *m_symParents;
	//int32_t  m_numSymParents;

	// hash buffer
	CategoryHash *m_catHash;

	// full buffer
	char *m_buffer;
	int32_t  m_bufferSize;

protected:
	// for parsing the original dmoz files
	char* incRdfPtr   ( int32_t skip = 1 );
	int32_t  rdfParse    ( char *tagName );
	int32_t  rdfNextTag  ( );
	int32_t  fillNextString  ( char *str, int32_t max );
	int32_t  fillNextTagBody ( char *str, int32_t max );

	// rdf stream
	char *m_rdfPtr;
	char *m_rdfEnd;
	//std::ifstream m_rdfStream;
	int   m_rdfStream;
	char *m_rdfBuffer;
	int32_t  m_rdfBufferSize;
	int32_t  m_currOffset;
	// static rdf buffer
	char  m_rdfSmallBuffer[RDFSMALLBUFFER_SIZE];
	// tag buffer
	char  m_tagRecfer[MAX_TAG_LEN+1];
	int32_t  m_tagLen;

	HashTable m_badTable;


	// sub category buffer
	//SubCategory m_subCats[MAX_SUB_CATS];
	//int32_t m_numSubCats;
};

extern class Categories  g_categories1;
extern class Categories  g_categories2;
extern class Categories *g_categories;

#endif
