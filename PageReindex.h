#ifndef _PAGEREINDEX_H_
#define _PAGEREINDEX_H_

// . for adding docid-based spider requests to spiderdb
// . this is the original method, for queuing up docid-based spider requests
class Msg1c {

public:

	Msg1c();

	bool reindexQuery ( char *query ,
			    char *coll  ,
			    long startNum ,
			    long endNum ,
			    void *state ,
			    void (* callback) (void *state ) ) ;
	
	bool gotList ( );
	
	char *m_coll;
	long m_startNum;
	long m_endNum;
	void *m_state;
	void (* m_callback) (void *state);
	long m_niceness;
	Msg39Request m_req;
	Msg3a m_msg3a;
	Msg1 m_msg1;
	RdbList m_list2;
	long m_numDocIds;
	long m_numDocIdsAdded;
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
			    long startNum ,
			    long endNum ,
			    void *state ,
			    void (* callback) (void *state ) ) ;
	
	bool updateTagTerms ( ) ;

	bool getMetaList ( long long docId , 
			   long eventId , 
			   TagRec *egr ,
			   RdbList *oldList ,
			   long niceness ,
			   SafeBuf *addBuf ) ;

	void *m_state;
	void (* m_callback) (void *state);

	Msg40 m_msg40;
	SearchInput m_si;
	long m_startNum;
	long m_endNum;
	long m_numDocIds;
	long m_i;
	Msg12 m_msg12;
	Msg8a m_msg8a;
	Msg0  m_msg0;
	char *m_coll;
	long  m_niceness;
	TagRec m_tagRec;
	RdbList m_revdbList;
	SafeBuf m_addBuf;
	SafeBuf m_rr;
	char *m_metaList;
	long  m_metaListSize;
	Msg4 m_msg4;
	Query      m_qq;

	Url  m_fakeUrl;

	long m_gotLock;
	long m_gotTagRec;
	long m_gotRevdbRec;
	long m_madeList;
	long m_addedList;
	long m_removeLock;
	long m_flushedList;
};
*/
#endif
