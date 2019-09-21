// Matt Wells, copyright Jul 2008

#ifndef _TAGDB_H_
#define _TAGDB_H_

#include "Conf.h"       // for setting rdb from Conf file
#include "Rdb.h"
#include "Xml.h"
#include "Url.h"
#include "Loop.h"
//#include "DiskPageCache.h"
//#include "CollectionRec.h"
#include "SafeBuf.h"
#include "Msg0.h"

// . now we can store multiple rating by multiple users or algorithms
// . we can use accountability, we can merge sources, etc.
// . we can use time-based merging

bool isTagTypeUnique    ( int32_t tt ) ;
bool isTagTypeIndexable ( int32_t tt ) ;
//bool isTagTypeString ( int32_t tt ) ;
int32_t hexToBinary     ( char *src , char *srcEnd , char *dst , bool decrement );

// . Tag::m_type is this if its a dup in the TagRec
// . so if www.xyz.com has one tag and xyz.com has another, then
//   the xyz.com tag should have its m_type set to TT_DUP, but only if we can only have
//   one of those tag types...
#define TT_DUP 123456

#define TAGDB_KEY key128_t

// a TagRec can contain multiple Tags, even of the same Tag::m_type
class Tag {
 public:

	int32_t  getSize    ( ) { return sizeof(key128_t) + 4 + m_recDataSize; };
	int32_t  getRecSize ( ) { return sizeof(key128_t) + 4 + m_recDataSize; };

	void set ( char *site ,
		   char *tagname ,
		   int32_t  timestamp ,
		   char *user ,
		   int32_t  ip ,
		   char *data ,
		   int32_t  dataSize );

	int32_t print ( ) ; 
	bool printToBuf             ( SafeBuf *sb );
	bool printToBufAsAddRequest ( SafeBuf *sb );
	bool printToBufAsXml        ( SafeBuf *sb );
	bool printToBufAsXml2       ( SafeBuf *sb );
	bool printToBufAsHtml       ( SafeBuf *sb , char *prefix );
	bool printToBufAsTagVector  ( SafeBuf *sb );
	// just print the m_data...
	bool printDataToBuf         ( SafeBuf *sb );
	bool isType                 ( char    *t  );
	bool isIndexable            ( ) {
		return isTagTypeIndexable ( m_type ); }
	//( m_dataSize == 1 || isType("meta") ); };
	// for parsing output of printToBuf()
	int32_t setFromBuf            ( char *p , char *pend ) ;
	int32_t setDataFromBuf        ( char *p , char *pend ) ;

	// skip of the username, whose size (including \0) is encoded
	// as the first byte in the m_recData buffer
	char *getTagData     ( ) {return m_buf + *m_buf + 1;};
	int32_t  getTagDataSize ( ) {return m_bufSize - *m_buf - 1; };
	// what user added this tag?
	char *getUser ( ) { return m_buf + 1;};
	// remove the terminating \0 which is included as part of the size
	int32_t  getUserLen ( ) { return *m_buf - 1; };

	// used to determine if one Tag should overwrite the other! if they
	// have the same dedup hash... then yes...
	int32_t getDedupHash ( );

	// tagdb uses 128 bit keys now
	key128_t  m_key;
	int32_t      m_recDataSize;

	// when tag was added/updated
	int32_t     m_timestamp; 
	// . ip address of user adding tag
	// . prevent multiple turk voters from same ip!
	int32_t     m_ip;        

	// each tag in a TagRec now has a unique id for ez deletion
	//int32_t     m_tagId;

	// the "type" of tag. see the TagDesc array in Tagdb.cpp for a list
	// of all the tag types. m_type is a hash of the type name.
	int32_t     m_type;

	// . m_user[] IS ACTUALLY a 6-byte KEY for another TagRec
	// . this is also a user's name like "mwells"
	// . each user has a TagRec whose FULL key is this userHash
	// . m_user[7] is always \0
	//char     m_user[8];

	int32_t     m_bufSize;
	char     m_buf[0];
};

// . convert "domain_squatter" to ST_DOMAIN_SQUATTER
// . used by CollectionRec::getRegExpNum()
int32_t  getTagTypeFromStr( char *tagTypeName , int32_t tagnameLen = -1 );

// . convert ST_DOMAIN_SQUATTER to "domain_squatter"
char *getTagStrFromType ( int32_t tagType ) ;

// . max # of tags any one site or url can have
// . even AFTER the "inheritance loop"
// . includes the 4 bytes used for size and # of tags
//#define MAX_TAGREC_SIZE 1024

// max "oustanding" msg0 requests sent by TagRec::lookup()
#define MAX_TAGDB_REQUESTS 3

// . the latest version of the TagRec
//#define TAGREC_CURRENT_VERSION 0

class TagRec {

 public:


	TagRec();
	~TagRec();
	void reset();
	void constructor ();

	// . an rdb record is a key, dataSize, then the data
	// . the "data" is al the stuff after "m_dataSize"
	//key_t    m_key;
	//int32_t     m_dataSize;
	//uint16_t m_numTags;
	//char     m_version;
	//char     m_buf [ MAX_TAGREC_SIZE - 12 - 4 - 2 ];

	//char *getKey      () { return (char *)&m_key; };
	//char *getData     () { return (char *)this + 12 + 4; };
	//int32_t  getDataSize () { return m_dataSize; };

	//void  copy        (class TagRec *tp ) {
	//	gbmemcpy ( this , (void *)tp , tp->getSize() ); };

	// includes the 4 byte "header" which consists of the first 2 bytes
	// being the size of the actual tag buffer and the second two bytes
	// being the number of tags in the actual tag buffer
	//int32_t         getSize       ( ) { return 12+4+1+2+m_dataSize; };
	//int32_t           getSize       ( ) { return 12+4+m_dataSize;};

	int32_t           getNumTags    ( );

	int32_t getSize ( ) { return sizeof(TagRec); };

	class Tag *getFirstTag   ( ) { 
		if ( m_numListPtrs == 0 ) return NULL;
		return (Tag *)m_listPtrs[0]->m_list; 
	}

	bool isEmpty ( ) { return (getFirstTag()==NULL); };

	// lists should be in order of precedence i guess
	class Tag *getNextTag ( class Tag *tag ) {
		// watch out
		if ( ! tag ) return NULL;
		// get rec size
		int32_t recSize = tag->getRecSize();
		// point to current tag
		char *current = (char *)tag;
		// find what list we are in
		int32_t i;
		for ( i = 0 ; i < m_numListPtrs ; i++ ) {
			if ( current <  m_listPtrs[i]->m_list    ) continue;
			if ( current >= m_listPtrs[i]->m_listEnd ) continue;
			break;
		}
		// sanity
		if ( i >= m_numListPtrs ) { char *xx=NULL;*xx=0; }
		// advance
		current += recSize;
		// sanity check
		if ( recSize > 500000 || recSize < 12 ) { 
			log("tagdb: corrupt tag recsize %i",(int)recSize);
			return NULL;
			char *xx=NULL;*xx=0;}
		// breach list?
		if ( current < m_listPtrs[i]->m_listEnd) return (Tag *)current;
		// advance list
		i++;
		// breach of lists?
		if ( i >= m_numListPtrs ) return NULL;
		// return that list record then
		return (Tag *)(m_listPtrs[i]->m_list);
	};

	// return the number the tags having particular tag types
	int32_t           getNumTagTypes ( char *tagTypeStr );

	// get a tag from the tagType
	class Tag *getTag        ( char *tagTypeStr );

	class Tag *getTag2       ( int32_t tagType );

	//char *getRecEnd ( ) { return (char *)this + getSize(); };
	//char *getMaxEnd ( ) { return (char *)this + (int32_t)MAX_TAGREC_SIZE; };

	// . for showing under the summary of a search result in PageResults
	// . also for Msg6a
	int32_t print ( ) ; 
	bool printToBuf             ( SafeBuf *sb );
	bool printToBufAsAddRequest ( SafeBuf *sb );
	bool printToBufAsXml        ( SafeBuf *sb );
	bool printToBufAsHtml       ( SafeBuf *sb , char *prefix );
	bool printToBufAsTagVector  ( SafeBuf *sb );

	// . make sure not a dup of a pre-existing tag
	// . used by the clock code to not at a clock if already in there
	//   in Msg14.cpp
	Tag *getTag ( char *tagTypeStr , char *dataPtr , int32_t dataSize );

	int32_t getTimestamp ( char *tagTypeStr , int32_t defalt );

	// . functions to act on a site "tag buf", like that in Msg16::m_tagRec
	// . first 2 bytes is size, 2nd to bytes is # of tags, then the tags
	int32_t getLong ( char        *tagTypeStr       ,
		       int32_t         defalt    , 
		       Tag        **bookmark  = NULL ,
		       int32_t        *timeStamp = NULL ,
		       char       **user      = NULL );
	int32_t getLong ( int32_t         tagId     ,
		       int32_t         defalt    , 
		       Tag        **bookmark  = NULL ,
		       int32_t        *timeStamp = NULL ,
		       char       **user      = NULL );
	
	int64_t getLongLong ( char        *tagTypeStr,
				int64_t    defalt    , 
				Tag        **bookmark  = NULL ,
				int32_t        *timeStamp = NULL ,
				char       **user      = NULL );

	char *getString ( char      *tagTypeStr       ,
			  char      *defalt    = NULL ,
			  int32_t      *size      = NULL ,
			  Tag      **bookmark  = NULL ,
			  int32_t      *timestamp = NULL ,
			  char     **user      = NULL );

	// we only store the first 6 chars of "user" into this TagRec, m_buf
	/*
	bool addTag ( char        *tagTypeStr,
		      int32_t         timestamp , 
		      char        *user      ,
		      int32_t         ip        ,
		      char        *data      , 
		      int32_t         dataSize  );
	

	// we convert the int32_t to a string for you here...
	bool addTag ( char        *tagTypeStr ,
		      int32_t         timestamp  , 
		      char        *user       ,
		      int32_t         ip         ,
		      int32_t         dataAsLong ) {
		char buf[16]; sprintf(buf,"%"INT32"",dataAsLong);
		return addTag(tagTypeStr,timestamp,user,ip,buf,
			      gbstrlen(buf)); };

	
	// same as above
	bool addTag ( Tag *tag );

	// add "negative" tags, so when these tags are added to the
	// TagRec in tagdb, they will delete them
	bool addDelTag ( char *tagTypeStr );

	// now you can specify a unique tag id in the case of multiple tags
	// that have the same tagType and user
	bool removeTags ( char *tagTypeStr , char *user , int32_t tagId = 0 ) ;
	bool removeTags ( int32_t  tagType    , char *user , int32_t tagId = 0 ) ;
	bool removeTag  ( class Tag *rmTag ) ;

	// add/remove all the tags from "tagRec" to our list of tags
	bool addTags ( TagRec *tagRec ) ;
	bool removeTags ( TagRec *tagRec ) ;

	// return false and set g_errno on error
	bool addTags     ( Tag *tags , char *tagEnd , char *bufEnd );
	bool removeTags  ( Tag *tags , char *tagEnd , char *bufEnd );
	bool replaceTags ( Tag *tags , char *tagEnd , char *bufEnd );
	*/

	bool setFromBuf ( char *buf , int32_t bufSize );
	bool serialize ( SafeBuf &dst );

	bool setFromHttpRequest ( HttpRequest *r , TcpSocket *s );


	// use this for setFromBuf()
	SafeBuf m_sbuf;
	

	// some specified input
	//char  *m_coll;
	Url   *m_url;

	collnum_t m_collnum;

	void    (*m_callback ) ( void *state );
	void     *m_state;

	// hold possible tagdb records
	RdbList m_lists[MAX_TAGDB_REQUESTS];

	// ptrs to lists in the m_lists[] array
	RdbList *m_listPtrs[MAX_TAGDB_REQUESTS];
	int32_t     m_numListPtrs;
};

class Tagdb  {

 public:
	// reset rdb
	void reset();

	// . TODO: specialized cache because to store pre-parsed tagdb recs
	// . TODO: have m_useSeals parameter???
	bool init  ( );
	bool init2 ( int32_t treeMem );
	
	bool verify ( char *coll );

	//bool convert ( char *coll ) ;

	bool addColl ( char *coll, bool doVerify = true );

	// used by ../rdb/Msg0 and ../rdb/Msg1
	Rdb *getRdb ( ) { return &m_rdb; };

	//key128_t makeKey      ( Url *u , bool isDelete ) ;
	key128_t makeStartKey ( char *site );//Url *u ) ;
	key128_t makeEndKey   ( char *site );//Url *u ) ;


	key128_t makeDomainStartKey ( Url *u ) ;
	key128_t makeDomainEndKey   ( Url *u ) ;

	// . get the serialized TagRec from an RdbList of TagRecs from tagdb
	//   that is the best match for "url"
	char *getRec ( RdbList *list , Url *url , int32_t *recSize ,char* coll, 
		       int32_t collLen, RdbList *retList) ;

	//DiskPageCache *getDiskPageCache() { return &m_pc; };

	//int32_t getGroupId (key_t *key) {return key->n1 & g_hostdb.m_groupMask;}

	// . dump tagdb to stdout
	// . dump as URL requests so we can re-add with blaster on each host
	// . this replaces dumpTagdb() in main.cpp
	// . sendPageTagdb() will process such URL requests
	void dumpTagdb ( );

	// private:

	// . returns 0 if url is not a suburl of "site"
	int32_t getMatchPoints ( Url *url , Url *site ) ;

	bool setHashTable ( ) ;

	// . we use the cache in here also for caching tagdb records
	//   and "not-founds" stored remotely (net cache)
	Rdb   m_rdb;

	//DiskPageCache m_pc;

	bool    loadMinSiteInlinksBuffer ( );
	bool    loadMinSiteInlinksBuffer2 ( );
	int32_t getMinSiteInlinks ( uint32_t hostHash32 ) ;
	SafeBuf m_siteBuf1;
	SafeBuf m_siteBuf2;

};

// derive this from tagdb
class Turkdb {
 public:
	void reset();
	bool init  ( );
	bool addColl ( char *coll, bool doVerify = true );
	Rdb *getRdb ( ) { return &m_rdb; };
	Rdb   m_rdb;
	//DiskPageCache m_pc;	
};

extern class Tagdb  g_tagdb;
extern class Tagdb  g_tagdb2;
extern class Turkdb g_turkdb;
//extern class Tagdb g_sitedb;

bool sendPageTagdb ( TcpSocket *s , HttpRequest *req ) ;



///////////////////////////////////////////////
//
// Msg8a gets TagRecs from Tagdb
//
///////////////////////////////////////////////

// msg8a needs to keep a ptr to the tags it
// generates in the "inheritance loop". so don't
// store more than this! log if we do.
//#define MAX_TAGS 128

//#define MSG8A_MAX_REQUEST_SIZE 2048

// this msg class is for getting AND adding to tagdb
class Msg8a {

 public:
	
	Msg8a ();
	~Msg8a ();
	void reset();

	// . get records from multiple subdomains of url
	// . calls g_udpServer.sendRequest() on each subdomain of url
	// . all matching records are merge into a final record
	//   i.e. site tags are also propagated accordingly
	// . closest matching "site" is used as the "site" (the site url)
	// . stores the tagRec in your "tagRec"
	bool getTagRec ( Url      *url              , 
			 char     *site , // set to NULL to auto set
			 //char     *coll             , 
			 collnum_t collnum,
			 //bool      useCanonicalName ,
			 bool skipDomainLookup ,
			 int32_t      niceness         ,
			 void     *state            ,
			 void    (* callback)(void *state ),
			 TagRec   *tagRec           ,
			 bool      doInheritance = true ,
			 char      rdbId         = RDB_TAGDB);
	
	bool launchGetRequests();
	void gotAllReplies ( ) ;

	// some specified input
	//char  *m_coll;
	//int32_t   m_collLen;
	Url   *m_url;
	//bool   m_doFullUrl;
	char   m_rdbId;

	collnum_t m_collnum;

	void    (*m_callback ) ( void *state );
	void     *m_state;

	Msg0    m_msg0s[MAX_TAGDB_REQUESTS];


	key128_t m_siteStartKey ;
	key128_t m_siteEndKey   ;

	// hold possible tagdb records
	//RdbList m_lists[MAX_TAGDB_REQUESTS];

	int32_t m_niceness;

	char *m_dom;
	char *m_hostEnd;
	char *m_p;

	int32_t  m_requests;
	int32_t  m_replies;
	char  m_doneLaunching;

	int32_t  m_errno;

	// we set this for the caller
	TagRec *m_tagRec;

	// hacks for msg6b
	void *m_parent;
	int32_t  m_slotNum;

	// hack for MsgE
	void *m_state2;
	void *m_state3;
	
	bool  m_doInheritance;
};

/*
void handleRequest9a ( UdpSlot *slot , int32_t niceness ) ;

class Msg9a {

 public:

	Msg9a ();
	~Msg9a();
	void reset() ;
	bool launchAddRequests ( ) ;

	static bool registerHandler ( ) {
		return g_udpServer.registerHandler ( 0x9a, handleRequest9a );};
	
	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "sites" is a NULL-terminated list of space-separated urls
	// . if "deleteTags" is false, then the tags will be added to the
	///  the TagRecs specified by the sites in "sites". if a TagRec
	//   does not exist for a given "site" then it will be added just
	//   so we can add the Tags to it. If it does exist, we will
	//   just append the given Tags to it. Tags with the same tagType and
	//   and "user" will be replaced by these Tags in the tagRecPtrs[].
	// . if "deleteTags" is true, then the Tags matching the Tags
	//   given will be removed from each TagRec. If a nonzero timestamp
	//   or a username that does not start with \0 or a non-zero data
	//   is specified in the Tag, then that must match the Tag
	//   being removed as well. In this way we can remove specific
	//   Tags from a list of Tags that share the same m_tagType.
	// . if "deleteTagRecs" is false then the entire TagRec will be removed
	//   from Tagdb. tagRecPtrs must be NULL in this case.
	// . if provided, the ip vector is 1-1 with the sites in "sitesPtrs[]"
	//   and we set the Tag::m_ip with the corresponding
	//   ip given by that. this allows XmlDoc.cpp to "ip stamp" a tag. 
	//   and the tag will be rendered invalid by XmlDoc.cpp if the ip of 
	//   the site changes from that! in this way we can invalidate tags 
	//   when a site changes ownership. assuming it also changes ip!
	// . you can now pass in either "sites" or sitePtrs/numSitePtrs,
	//   whichever is easiest for you...
	bool addTags ( char    *sites                  ,
		       char   **sitePtrs               ,
		       int32_t     numSitePtrs            ,
		       char    *coll                   , 
		       void    *state                  ,
		       void   (*callback)(void *state) ,
		       int32_t     niceness               ,
		       TagRec  *tagRec                 ,
		       bool     nukeTagRecs            ,
		       int32_t    *ipVector               );//= NULL );

	// like above, but we are adding the output of a
	// './gb dump S main 0 -1 1' cmd
	bool addTags ( char    *dumpFile               ,
		       char    *coll                   , 
		       void    *state                  ,
		       void   (*callback)(void *state) ,
		       int32_t     niceness               );

	void    (*m_callback ) ( void *state );
	void     *m_state;

	int32_t  m_errno;
	int32_t  m_requests;
	int32_t  m_replies;

	char *m_requestBuf;
	int32_t  m_requestBufSize;

	char *m_p;
	char *m_pend;

	int32_t  m_niceness;
};
*/

int32_t getY ( int64_t X , int64_t *x , int64_t *y , int32_t n ) ;

#endif

// Lookup order for the url hostname.domainname.com/mydir/mypage.html 
// (aka 1.2.3.4/mydir/mypage.html):

//  . hostname.domainname.com/mydir/mypage.html
//  . hostname.domainname.com/mydir/
//  . hostname.domainname.com
//  .          domainname.com/mydir/mypage.html
//  .          domainname.com/mydir/
//  .          domainname.com
//  .          1.2.3.4       /mydir/mypage.html
//  .          1.2.3.4       /mydir/
//  .          1.2.3.4                
//  .          1.2.3
