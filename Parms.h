// Matt Wells, copyright Feb 2002

// Ideally, CollectionRec.h and SearchInput.h should be automatically generated
// from Parms.cpp. But Parms need to be marked if they contribute to 
// SearchInput::makeKey() for caching the SERPS.

#ifndef _PARMS_H_
#define _PARMS_H_

#include "Rdb.h"

//#include "CollectionRec.h"

void handleRequest3e ( UdpSlot *slot , long niceness ) ;
void handleRequest3f ( UdpSlot *slot , long niceness ) ;

// special priorities for the priority drop down 
// in the url filters table
enum {
	SPIDER_PRIORITY_FILTERED  = -3 ,
	SPIDER_PRIORITY_BANNED    = -2 ,
	SPIDER_PRIORITY_UNDEFINED = -1 };

enum {
	OBJ_CONF    = 1 ,
	OBJ_COLL        ,
	OBJ_SI          }; // SearchInput class

enum {
	TYPE_BOOL       = 1 ,
	TYPE_BOOL2          ,
	TYPE_CHECKBOX       ,
	TYPE_CHAR           ,
	TYPE_CHAR2          , //needed to display char as a number (maxNumHops)
	TYPE_CMD            ,
	TYPE_FLOAT          ,
	TYPE_IP             ,
	TYPE_LONG           ,
	TYPE_LONG_LONG      , // 10
	TYPE_NONE           ,
	TYPE_PRIORITY       ,
	TYPE_PRIORITY2      ,
	TYPE_PRIORITY_BOXES ,
	TYPE_RETRIES        ,
	TYPE_STRING         ,
	TYPE_STRINGBOX      ,
	TYPE_STRINGNONEMPTY ,
	TYPE_TIME           ,
	TYPE_DATE2          , // 20
	TYPE_DATE           ,
	TYPE_RULESET        ,
	TYPE_FILTER         ,
	TYPE_COMMENT        ,
        TYPE_CONSTANT       ,
	TYPE_MONOD2         ,
	TYPE_MONOM2         ,
	TYPE_LONG_CONST     ,
	TYPE_SITERULE       , // 29
	TYPE_SAFEBUF        
	//TYPE_DIFFBOT_DROPDOWN
};

//forward decls to make compiler happy:
class HttpRequest;
class TcpSocket;

class Page {
	long  m_page;     // from the PAGE_* enums above
	char *m_bgcolor;  // color of the cells in the table
	char *m_topcolor; // color of the table's first row
	char *m_title;    // browser title bar
};

// values for Parm::m_subMenu
#define SUBMENU_DISPLAY     1
#define SUBMENU_MAP         2
#define SUBMENU_CALENDAR    3
#define SUBMENU_LOCATION    4
#define SUBMENU_SOCIAL      5
#define SUBMENU_TIME        6
#define SUBMENU_CATEGORIES  7
#define SUBMENU_LINKS       8
#define SUBMENU_WIDGET      9
#define SUBMENU_SUGGESTIONS 10
#define SUBMENU_SEARCH      11
#define SUBMENU_CHECKBOX    0x80 // flag

// values for Parm::m_flags
#define PF_COOKIE  0x01  // store in cookie?
#define PF_REDBOX  0x02  // redbox constraint on search results
#define PF_SUBMENU_HEADER  0x04
#define PF_WIDGET_PARM     0x08
#define PF_API             0x10
#define PF_REBUILDURLFILTERS 0x20
#define PF_NOSYNC            0x40
#define PF_DIFFBOT           0x80

#define PF_HIDDEN 0x0100
#define PF_NOSAVE 0x0200


class Parm {
 public:
	char *m_title; // displayed above m_desc on admin gui page
	char *m_desc;  // description of variable displayed on admin gui page
	char *m_cgi;   // cgi name, contains %i if an array
	char *m_xml;   // default to rendition of m_title if NULL
	long  m_off;   // this variable's offset into the CollectionRec class
	char  m_type;  // TYPE_BOOL, TYPE_LONG, ...
	long  m_page;  // PAGE_MASTER, PAGE_SPIDER, ... see Pages.h
	char  m_obj;   // OBJ_CONF or OBJ_COLL
	// the maximum number of elements supported in the array.
	// this is 1 if NOT an array (i.e. array of only one parm).
	// in such cases a "count" is NOT stored before the parm in 
	// CollectionRec.h or Conf.h.
	bool isArray() { return (m_max>1); };

	long getNumInArray() ;

	long  m_max;   // max elements in the array
	// if array is fixed size, how many elements in it?
	// this is 0 if not a FIXED size array.
	long  m_fixed; 
	long  m_size;  // max string size
	char *m_def;   // default value of this variable if not in either conf
	char  m_cast;  // true if we should broadcast to all hosts (default)
	char *m_units;
	char  m_addin; // add "insert above" link to gui when displaying array
	char  m_rowid; // id of row controls are in, if any
	char  m_rdonly;// if in read-only mode, blank out this control?
	char  m_hdrs;  // print headers for row or print title/desc for single?
	char  m_perms; // 0 means same as WebPages' m_perms
	char  m_subMenu;
	short m_flags;
	char *m_class;
	char *m_icon;
	char *m_qterm;
	long  m_parmNum; // slot # in the m_parms[] array that we are
	//bool (*m_func)(TcpSocket *s , HttpRequest *r,
	//	       bool (*cb)(TcpSocket *s , HttpRequest *r));
	bool (*m_func)(char *parmRec);
	// some functions can block, like when deleting a coll because
	// the tree might be saving, so they take a "we" ptr
	bool (*m_func2)(char *parmRec,class WaitEntry *we);
	long  m_plen;  // offset of length for TYPE_STRINGS (m_htmlHeadLen...)
	char  m_group; // start of a new group of controls?
	// m_priv = 1 means gigablast's software license clients cannot see
	//            or change.
	// m_priv = 2 means gigablast's software license clients, including
	//            even metalincs, cannot see or change.
	// m_priv = 3 means nobody can see in admin controls, but can be 
	//            in search input by anybody. really a hack for yaron
	//            from quigo so he can set "t2" to something bigger.
	char  m_priv;  // true if gigablast's software clients cannot see
	char  m_save;  // save to xml file? almost always true
	long  m_min;
	// these are used for search parms in PageResults.cpp
	char  m_sparm; // is this a search parm? for passing to PageResults.cpp
	char *m_scgi;  // parm in the search url
	char  m_spriv; // is it private? only admins can see/use private parms
	char *m_scmd;  // the url path for this m_scgi variable
	//long  m_sdefo; // offset of default into CollectionRec (use m_off)
	long  m_sminc ;// offset of min in CollectionRec (-1 for none)
	long  m_smaxc ;// offset of max in CollectionRec (-1 for none)
	long  m_smin;  // absolute min
	long  m_smax;  // absolute max
	long  m_soff;  // offset into SearchInput to store value in
	char  m_sprpg; // propagate the cgi variable to other pages via GET?
	char  m_sprpp; // propagate the cgi variable to other pages via POST?
	bool  m_sync;  // this parm should be synced
	long  m_hash;  // hash of "title"
	long  m_cgiHash; // hash of m_cgi

	bool   getValueAsBool   ( class SearchInput *si ) ;
	long   getValueAsLong   ( class SearchInput *si ) ;
	char * getValueAsString ( class SearchInput *si ) ;	

	long getNumInArray ( collnum_t collnum ) ;

	bool printVal ( class SafeBuf *sb , collnum_t collnum , long occNum ) ;
};

#define MAX_PARMS 940

#define MAX_XML_CONF (200*1024)

#include "Xml.h"
#include "SafeBuf.h"

struct SerParm;

class Parms {

 public:

	Parms();

	void init();
	
	bool sendPageGeneric ( class TcpSocket *s, class HttpRequest *r, 
			       long page , char *cookie = NULL ,
			       // Diffbot.cpp uses this to print the
			       // url filters into
			       SafeBuf *pageBuf = NULL ,
			       // used by diffbot.cpp
			       char *collOverride = NULL ,
			       bool isJSON = false ) ;

	bool sendPageGeneric2 ( class TcpSocket *s , class HttpRequest *r , 
				long page , char *coll , char *pwd ) ;


	//char *printParms (char *p, char *pend, TcpSocket *s, HttpRequest *r);
	bool printParms (SafeBuf* sb, TcpSocket *s , HttpRequest *r );

	//char *printParms (char *p,char *pend,long page,char *username,
	//                  void *THIS, char *coll , char *pwd , 
	//		  long nc , long pd ) ;
	bool printParms (SafeBuf* sb, long page,char *username,void *THIS,
			 char *coll , char *pwd , long nc , long pd ,
			 bool isCrawlbot = false ,
			 bool isJSON = false );

	/*
	char *printParm ( char *p    , 
			  char *pend ,
			  //long  user ,
			  char *username,
			  Parm *m    , 
			  long  mm   , // m = &m_parms[mm]
			  long  j    ,
			  long  jend ,
			  char *THIS ,
			  char *coll ,
			  char *pwd  ,
			  char *bg   ,
			  long  nc   ,
			  long  pd   ) ;
	*/

	bool printParm ( SafeBuf* sb,
			 //long  user ,
			  char *username,
			  Parm *m    , 
			  long  mm   , // m = &m_parms[mm]
			  long  j    ,
			  long  jend ,
			  char *THIS ,
			  char *coll ,
			  char *pwd  ,
			  char *bg   ,
			  long  nc   ,
			 long  pd   ,
			 bool lastRow ,
			 bool isCrawlbot = false,
			 bool isJSON = false ) ;

	char *getTHIS ( HttpRequest *r , long page );

	class Parm *getParmFromParmHash ( long parmHash );

	bool setFromRequest ( HttpRequest *r , //long user,
			      TcpSocket* s,
			      bool (*callback)(TcpSocket *s , HttpRequest *r),
			      class CollectionRec *newcr = NULL );
	
	bool insertParm ( long i , long an , char *THIS ) ;
	bool removeParm ( long i , long an , char *THIS ) ;

	void setParm ( char *THIS, Parm *m, long mm, long j, char *s,
		       bool isHtmlEncoded , bool fromRequest ) ;
	
	void setToDefault ( char *THIS ) ;

	bool setFromFile ( void *THIS        , 
			   char *filename    , 
			   char *filenameDef ) ;

	bool setXmlFromFile(Xml *xml, char *filename, char *buf, long bufSize);

	bool saveToXml ( char *THIS , char *f ) ;

	// get the parm with the associated cgi name. must be NULL terminated.
	Parm *getParm ( char *cgi ) ;

	char *getParmHtmlEncoded ( char *p , char *pend , Parm *m , char *s );

	// . make it so a collectionrec can be copied in Collectiondb.cpp
	// . so the rec can be copied and the old one deleted without
	//   freeing the safebufs now used by the new one.
	void detachSafeBufs ( class CollectionRec *cr ) ;

	// calc checksum of parms
	unsigned long calcChecksum();

	// get size of serialized parms
	//long getStoredSize();
	// . serialized to buf
	// . if buf is NULL, just calcs size
	//bool serialize( char *buf, long *bufSize );
	//void deserialize( char *buf );

	void overlapTest ( char step ) ;


	/////
	//
	// parms now in parmdb
	//
	/////

	// all parm recs need to be in the tree
	//Rdb m_rdb;

	//
	// new functions
	//

	bool addNewParmToList1 ( SafeBuf *parmList ,
				 collnum_t collnum ,
				 char *parmValString ,
				 long  occNum ,
				 char *parmName ) ;
	bool addNewParmToList2 ( SafeBuf *parmList ,
				 collnum_t collnum , 
				 char *parmValString ,
				 long occNum ,
				 Parm *m ) ;
	bool addCurrentParmToList1 ( SafeBuf *parmList ,
				     CollectionRec *cr , 
				     char *parmName ) ;
	bool addCurrentParmToList2 ( SafeBuf *parmList ,
				     collnum_t collnum , 
				     long occNum ,
				     Parm *m ) ;
	bool convertHttpRequestToParmList (HttpRequest *hr,SafeBuf *parmList,
					   long page);
	Parm *getParmFast2 ( long cgiHash32 ) ;
	Parm *getParmFast1 ( char *cgi , long *occNum ) ;
	bool broadcastParmList ( SafeBuf *parmList ,
				 void    *state ,
				 void   (* callback)(void *) ,
				 bool sendToGrunts  = true ,
				 bool sendToProxies = false ,
				 // send to this single hostid? -1 means all
				 long hostId = -1 ,
				 long hostId2 = -1 ); // hostid range?
	bool doParmSendingLoop ( ) ;
	bool syncParmsWithHost0 ( ) ;
	bool makeSyncHashList ( SafeBuf *hashList ) ;
	long getNumInArray ( collnum_t collnum ) ;
	bool addAllParmsToList ( SafeBuf *parmList, collnum_t collnum ) ;
	bool updateParm ( char *rec , class WaitEntry *we ) ;

	//
	// end new functions
	//

	bool m_inSyncWithHost0;

	bool m_isDefaultLoaded;

	Page m_pages [ 50 ];
	long m_numPages;
	
	Parm m_parms [ MAX_PARMS ];
	long m_numParms;

	// just those Parms that have a m_sparm of 1
	Parm *m_searchParms [ MAX_PARMS ];
	long m_numSearchParms;

	/*
 private:
	// these return true if overflow
	bool serializeConfParm( Parm *m, long i, char **p, char *end, 
				long size, long cnt, 
				bool sizeChk, long *bufSz );
	bool serializeCollParm( class CollectionRec *cr, 
				Parm *m, long i, char **p, char *end,
				long size, long cnt,
				bool sizeChk, long *bufSz );
			

	void deserializeConfParm( Parm *m, SerParm *sp, char **p,
				   bool *confChgd );
	void deserializeCollParm( class CollectionRec *cr,
				  Parm *m, SerParm *sp, char **p );
	*/

	// for holding default.conf file for collection recs for OBJ_COLL
	char m_buf [ MAX_XML_CONF ];

	// for parsing default.conf file for collection recs for OBJ_COLL
	Xml m_xml2;
};

extern Parms g_parms;

#endif

