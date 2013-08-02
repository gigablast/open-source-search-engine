// Matt Wells, copyright Feb 2001

// maintains a simple array of CollectionRecs

#ifndef _COLLECTIONDB_H_
#define _COLLECTIONDB_H_

// . max # of collections we're serving
// . may have to update if business gets going (or make dynamic)
// . lowered to 16 to save some mem
#define MAX_COLL_RECS 16 // 256
#define MAX_COLLS (MAX_COLL_RECS)

class Collectiondb  {

 public:
	Collectiondb();

	// does nothing
	void reset() ;

	// . this loads all the recs from host #0 
	// . returns false and sets errno on error
	// . each collection as a CollectionRec class for it and
	//   is loaded up from the appropriate config file
	bool init ( bool isDump = false );

	// this loads all the recs from host #0 
	bool load ( bool isDump = false );

	// . this will save all conf files back to disk that need it
	// . returns false and sets g_errno on error, true on success
	bool save ( );
	bool m_needsSave;

	// returns i so that m_recs[i].m_coll = coll
	collnum_t getCollnum ( char *coll , long collLen );
	collnum_t getCollnum ( char *coll ); // coll is NULL terminated here

	char *getCollName ( collnum_t collnum );
	char *getColl     ( collnum_t collnum ) {return getCollName(collnum);};

	// get coll rec specified in the HTTP request
	class CollectionRec *getRec ( class HttpRequest *r );

	// . get collectionRec from name
	// returns NULL if not available
	class CollectionRec *getRec ( char *coll );

	class CollectionRec *getRec ( char *coll , long collLen );

	class CollectionRec *getRec ( collnum_t collnum);

	//class CollectionRec *getDefaultRec ( ) ;

	class CollectionRec *getFirstRec      ( ) ;
	char                *getFirstCollName ( ) ;
	collnum_t            getFirstCollnum  ( ) ;

	// . how many collections we have in here
	// . only counts valid existing collections
	long getNumRecs() { return m_numRecsUsed; };

	// . does this requester have root admin privledges???
	// . uses the root collection record!
	bool isAdmin ( class HttpRequest *r , class TcpSocket *s );

	collnum_t getNextCollnum ( collnum_t collnum );

	long long getLastUpdateTime () { return m_lastUpdateTime; };
	// updates m_lastUpdateTime so g_spiderCache know when to reload
	void     updateTime         ();

	// private:

	// . these are called by handleRequest
	// . based on "action" cgi var, 1-->add,2-->delete,3-->update
	bool addRec     ( char *coll , char *cc , long cclen , bool isNew ,
			  collnum_t collnum , bool isDump , //  = false );
			  bool saveRec ); // = true
	bool deleteRec  ( char *coll , bool deleteTurkdb = true );
	//bool updateRec ( CollectionRec *newrec );
	bool deleteRecs ( class HttpRequest *r ) ;

	bool resetColl ( char *coll , bool resetTurkdb = true ) ;

	// . keep up to 128 of them, these reference into m_list
	// . COllectionRec now includes m_needsSave and m_lastUpdateTime
	class CollectionRec  *m_recs           [ MAX_COLLS ];
	//bool            m_needsSave      [ MAX_COLLS ];
	//long long       m_lastUpdateTime [ MAX_COLLS ];
	long            m_numRecs;
	long            m_numRecsUsed;

	long long            m_lastUpdateTime;
};

extern class Collectiondb g_collectiondb;

#endif
