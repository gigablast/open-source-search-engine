#ifndef _PAGEREINDEX_H_
#define _PAGEREINDEX_H_

#include "Msg4.h"
#include "SafeBuf.h"

// . for adding docid-based spider requests to spiderdb
// . this is the original method, for queuing up docid-based spider requests
class Msg1c {

public:

	Msg1c();

	bool reindexQuery ( char *query ,
			    collnum_t collnum, // char *coll  ,
			    int32_t startNum ,
			    int32_t endNum ,
			    bool forceDel ,
			    int32_t langId,
			    void *state ,
			    void (* callback) (void *state ) ) ;
	
	bool gotList ( );
	
	//char *m_coll;
	collnum_t m_collnum;
	int32_t m_startNum;
	int32_t m_endNum;
	bool m_forceDel;
	void *m_state;
	void (* m_callback) (void *state);
	int32_t m_niceness;
	Msg39Request m_req;
	Msg3a m_msg3a;
	//Msg1 m_msg1;
	//RdbList m_list2;
	Msg4 m_msg4;
	SafeBuf m_sb;
	int32_t m_numDocIds;
	int32_t m_numDocIdsAdded;
	Query  m_qq;
};

/*
// . for indexing tags for events after you add to tagdb
// . created so zak can very quickly tag eventids that are already indexed
// . will just add the tag terms directly to datedb for the eventid
class Msg1d {

public:

	bool updateQuery  ( char *query ,
			    HttpRequest *r,
			    TcpSocket *sock,
			    char *coll  ,
			    int32_t startNum ,
			    int32_t endNum ,
			    void *state ,
			    void (* callback) (void *state ) ) ;
	
	bool updateTagTerms ( ) ;

	bool getMetaList ( int64_t docId , 
			   int32_t eventId , 
			   TagRec *egr ,
			   RdbList *oldList ,
			   int32_t niceness ,
			   SafeBuf *addBuf ) ;

	void *m_state;
	void (* m_callback) (void *state);

	Msg40 m_msg40;
	SearchInput m_si;
	int32_t m_startNum;
	int32_t m_endNum;
	int32_t m_numDocIds;
	int32_t m_i;
	Msg12 m_msg12;
	Msg8a m_msg8a;
	Msg0  m_msg0;
	char *m_coll;
	int32_t  m_niceness;
	TagRec m_tagRec;
	RdbList m_revdbList;
	SafeBuf m_addBuf;
	SafeBuf m_rr;
	char *m_metaList;
	int32_t  m_metaListSize;
	Msg4 m_msg4;
	Query      m_qq;

	Url  m_fakeUrl;

	int32_t m_gotLock;
	int32_t m_gotTagRec;
	int32_t m_gotRevdbRec;
	int32_t m_madeList;
	int32_t m_addedList;
	int32_t m_removeLock;
	int32_t m_flushedList;
};
*/
#endif
