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
#define MAX_CATIDS       64
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
	long  m_catid;
	long  m_parentid;
	//short m_numSymParents;
	//long  m_symParentsOffset;
	long  m_nameOffset;
	short m_nameLen;
	unsigned long m_structureOffset;
	unsigned long m_contentOffset;
	long  m_numUrls;
};

struct CategoryHash {
	unsigned long  m_hash;
	long m_catIndex;
};

struct SubCategory {
	long  m_prefixOffset;
	long  m_prefixLen;
	long  m_nameOffset;
	long  m_nameLen;
	char  m_type;
};

class Categories {
public:
	Categories();
	~Categories();

	long fileRead ( int fileid, void *buf, size_t count );

	void reset();

	// load the hierarchy from a file
	long loadCategories ( char *filename );

	// . this is called by loadCategories() and constructs m_adultTable
	// . it will load/save it from/to disk, too
	bool makeBadHashTable ( ) ;
	bool addUrlsToBadHashTable ( long catid ) ;

	// get the index of a cat from its id
	// -1 if not found
	long getIndexFromId   ( long catid );
	long getIndexFromPath ( char *str, long strLen );
	long getIdFromPath    ( char *str, long strLen );

	// determine if a category should be printed RTL
	bool isIdRTLStart    ( long catid );
	bool isIndexRTLStart ( long catIndex );
	bool isIdRTL         ( long catid );
	bool isIndexRTL      ( long catIndex );

	// see if the category is Adult
	bool isIdAdultStart    ( long catid );
	bool isIndexAdultStart ( long catIndex );
	bool isIdAdult         ( long catid );
	bool isIndexAdult      ( long catIndex );

	// is it in a bad cat, like adult, gambling, online pharmacies
	bool isIdBadStart    ( long catid );
	bool isIndexBadStart ( long catIndex );
	bool isIdBad         ( long catid );
	bool isIndexBad      ( long catIndex );
	// is this url directly in a dmoz adult category?
	bool isInBadCat      ( Url *u ) ;
	bool isInBadCat      ( unsigned long urlHash );

	// print info of cats
	void printCats ( long start, long end );

	// print the path of this category
	void printPathFromId ( SafeBuf *sb ,
			       long  catid,
			       bool  raw = false,
			       bool  isRTL = false );
	void printPathFromIndex ( SafeBuf *sb ,
				  long  catIndex,
				  bool  raw = false,
				  bool  isRTL = false );

	// print the path bread crumb links for this category
	void printPathCrumbFromId    ( SafeBuf *sb ,
				       long  catid,
				       bool  isRTL = false );
	void printPathCrumbFromIndex ( SafeBuf *sb ,
				       long  catid,
				       bool  isRTL = false );

	// . get the title and summary for a specific url
	//   and catid
	bool getTitleAndSummary ( char  *url,
				  long   urlLen,
				  long   catid,
				  char  *title        = NULL,
				  long  *titleLen     = NULL,
				  long   maxTitleLen  = 0,
				  char  *summ         = NULL,
				  long  *summLen      = NULL,
				  long   maxSummLen   = 0,
				  char  *anchor       = NULL,
				  unsigned char *anchorLen    = NULL,
				  long   maxAnchorLen = 0 ,
				  long   niceness     = 0 ,
				  bool   justAddToTable = false );

	// normalize a url string
	long fixUrl ( char *url, long urlLen );

	// generate sub categories for a given catid
	long generateSubCats ( long catid,
			       SubCategory *subCats,
			       char **catBuffer,
			       long  *catBufferSize,
			       long  *catBufferLen,
			       bool   allowRealloc = true );

	long getNumUrlsFromIndex ( long catIndex ) {
		return m_cats[catIndex].m_numUrls; };

	// creates a directory search request url
	//void createDirectorySearchUrl ( Url  *url,
	long createDirSearchRequest ( char *requestBuf,
				      long  requestBufSize,
				      long  catid,
				      char *hostname,
				      long  hostnameLen,
				      char *coll,
				      long  collLen,
				      char *cgi ,//= NULL,
				      long  cgiLen ,//= 0,
				      bool  cgiFromRequest ,//= false ,
				      class HttpRequest *r );

	bool initLangTables(void);
	bool loadLangTables(void);
	uint8_t findLanguage(char *addr);

	// Categories
	Category *m_cats;
	long      m_numCats;

	// name buffer
	char *m_nameBuffer;
	long  m_nameBufferSize;

	// symbolic parent buffer
	//long *m_symParents;
	//long  m_numSymParents;

	// hash buffer
	CategoryHash *m_catHash;

	// full buffer
	char *m_buffer;
	long  m_bufferSize;

protected:
	// for parsing the original dmoz files
	char* incRdfPtr   ( long skip = 1 );
	long  rdfParse    ( char *tagName );
	long  rdfNextTag  ( );
	long  fillNextString  ( char *str, long max );
	long  fillNextTagBody ( char *str, long max );

	// rdf stream
	char *m_rdfPtr;
	char *m_rdfEnd;
	//std::ifstream m_rdfStream;
	int   m_rdfStream;
	char *m_rdfBuffer;
	long  m_rdfBufferSize;
	long  m_currOffset;
	// static rdf buffer
	char  m_rdfSmallBuffer[RDFSMALLBUFFER_SIZE];
	// tag buffer
	char  m_tagRecfer[MAX_TAG_LEN+1];
	long  m_tagLen;

	HashTable m_badTable;


	// sub category buffer
	//SubCategory m_subCats[MAX_SUB_CATS];
	//long m_numSubCats;
};

extern class Categories  g_categories1;
extern class Categories  g_categories2;
extern class Categories *g_categories;

#endif
