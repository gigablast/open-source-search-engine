// Matt Wells, copyright Feb 2001

// . get a list from any rdb (hostmap,tagdb,termdb,titledb,quotadb)
// . TODO: support concatenation of lists from different groups

#ifndef _MSG0_H_
#define _MSG0_H_

#include "UdpServer.h"
#include "Multicast.h"
#include "Hostdb.h"
#include "Indexdb.h"


/*
// termlist cache accessor functions for Msg5.cpp to use
extern RdbCache g_termListCache;
class RdbCache *getTermListCache ( ) ;
long long getTermListCacheKey ( char *startKey , char *endKey ) ;
bool addRecToTermListCache ( char *coll,
			     char *startKey , 
			     char *endKey , 
			     char *list ,
			     long  listSize ) ;
bool getListFromTermListCache ( char *coll,
				char *startKey,
				char *endKey,
				long  maxCacheAge,
				RdbList *list ) ;
bool getRecFromTermListCache ( char *coll,
			       char *startKey,
			       char *endKey,
			       long  maxCacheAge,
			       char **rec ,
			       long *recSize ) ;
*/

//#define MSG0_REQ_SIZE (8 + 2 * sizeof(key_t) + 16 + 5 + MAX_COLL_LEN + 1 )
#define MSG0_REQ_SIZE (8 + 2 * MAX_KEY_BYTES + 16 + 5 + MAX_COLL_LEN + 1 + 1 )

class Msg0 {

 public:

	Msg0  ( ) ;
	~Msg0 ( ) ;
	void reset ( ) ;
	void constructor ( );

	// . this should only be called once
	// . should also register our get record handlers with the udpServer
	bool registerHandler();

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . "list" should NOT be on the stack in case we block
	// . caches "not founds" as well
	// . rdbIds: 0=hostdb,1=tagdb,2=termdb,3=titledb,4=quotadb
	// . rdbIds: see getRdb() below
	// . set niceness to 0 for highest priority, default is lowest priority
	// . maxCacheAge is the max cached list age in our NETWORK cache
	// . our disk cache gets flushed when the disk is updated so it's never
	//   out of sync with the data
	// . a maxCacheAge of 0 (or negative) means not to check the cache
	bool getList ( long long hostId      , // -1 if unspecified
		       long      ip          , // info on hostId
		       short     port        ,
		       long      maxCacheAge , // max cached age in seconds
		       bool      addToCache  , // add net recv'd list to cache?
		       char      rdbId       , // specifies the rdb
		       char     *coll        ,
		       class RdbList  *list  ,
		       //key_t     startKey    , 
		       //key_t     endKey      , 
		       char     *startKey    ,
		       char     *endKey      ,
		       long      minRecSizes ,  // Positive values only
		       void     *state       ,
		       //void  (* callback)(void *state , class RdbList *list),
		       void  (* callback)(void *state ),
		       long      niceness    ,
		       bool      doErrorCorrection = true ,
		       bool      includeTree       = true ,
		       bool      doMerge           = true ,
		       long      firstHostId       = -1   ,
		       long      startFileNum      =  0   ,
		       long      numFiles          = -1   ,
		       long      timeout           = 30   ,
		       long long syncPoint         = -1   ,
		       long      preferLocalReads  = -1   , // -1=use g_conf
		       class Msg5 *msg5            = NULL ,
		       class Msg5 *msg5b           = NULL ,
		       bool        isRealMerge     = false , // file merge?
//#ifdef SPLIT_INDEXDB
		       bool        allowPageCache  = true ,
		       bool        forceLocalIndexdb = false,
		       bool        noSplit           = false , // MDW ????
		       long        forceParitySplit = -1    );
//#else
//		       bool        allowPageCache  = true );
//#endif

	bool getList ( long long hostId      , // -1 if unspecified
		       long      ip          , // info on hostId
		       short     port        ,
		       long      maxCacheAge , // max cached age in seconds
		       bool      addToCache  , // add net recv'd list to cache?
		       char      rdbId       , // specifies the rdb
		       char     *coll        ,
		       class RdbList  *list  ,
		       key_t     startKey    , 
		       key_t     endKey      , 
		       long      minRecSizes , // Positive values only 
		       void     *state       ,
		       //void (* callback)(void *state , class RdbList *list),
		       void    (* callback)(void *state ),
		       long      niceness    ,
		       bool      doErrorCorrection = true ,
		       bool      includeTree       = true ,
		       bool      doMerge           = true ,
		       long      firstHostId       = -1   ,
		       long      startFileNum      =  0   ,
		       long      numFiles          = -1   ,
		       long      timeout           = 30   ,
		       long long syncPoint         = -1   ,
		       long      preferLocalReads  = -1   , // -1=use g_conf
		       class Msg5 *msg5            = NULL ,
		       class Msg5 *msg5b           = NULL ,
		       bool        isRealMerge     = false, // file merge?
//#ifdef SPLIT_INDEXDB
		       bool        allowPageCache  = true ,
		       bool        forceLocalIndexdb = false,
		       // default for this should be false, because true
		       // means to send a msg0 to every indexdb split!
		       bool        doIndexdbSplit    = false ,
		       long        forceParitySplit = -1    ) {
//#else
//		       bool        allowPageCache  = true ) {
//#endif

		return getList ( hostId      , 
				 ip          ,
				 port        ,
				 maxCacheAge ,
				 addToCache  ,
				 rdbId       ,
				 coll        ,
				 list  ,
				 (char *)&startKey    , 
				 (char *)&endKey      , 
				 minRecSizes ,
				 state       ,
				 callback    ,
				 niceness    ,
				 doErrorCorrection ,
				 includeTree       ,
				 doMerge           ,
				 firstHostId       ,
				 startFileNum      ,
				 numFiles          ,
				 timeout           ,
				 syncPoint         ,
				 preferLocalReads  ,
				 msg5            ,
				 msg5b           ,
				 isRealMerge     ,
//#ifdef SPLIT_INDEXDB
				 allowPageCache  ,
				 forceLocalIndexdb ,
				 doIndexdbSplit  ); };
//#else
//				 allowPageCache  ); };
//#endif


	// . YOU NEED NOT CALL routines below here
	// . private:

	bool gotLoadReply ( ) ;

	bool getList ( long firstHostId ) ;

	// gotta keep this handler public so the C wrappers can call them
	void gotReply   ( char *reply , long replySize , long replyMaxSize );
//#ifdef SPLIT_INDEXDB
	void gotSplitReply ( );
//#endif

	// maps an rdbId to an Rdb
	class Rdb *getRdb ( char rdbId ) ;

	// the opposite of getRdb() above
	char getRdbId ( class Rdb *rdb );

	// callback info
	void    (*m_callback ) ( void *state );//, class RdbList *list );
	void     *m_state;      

	// host we sent RdbList request to 
	long long m_hostId;
	long      m_ip;
	short     m_port;

	// group we sent RdbList request to
	unsigned long  m_groupId;    

	UdpSlot  *m_slot;

	// 2*4 + 1 + 2 * keySize
	char      m_request [ MSG0_REQ_SIZE ];
	long      m_requestSize;

	// used for multicasting the request
//#ifdef SPLIT_INDEXDB
	//Multicast m_mcast[INDEXDB_SPLIT];
	//Multicast m_mcast[MAX_INDEXDB_SPLIT];
	// casting to multiple splits is obsolete, but for PageIndexdb.cpp
	// we still need to do it, but we alloc for it
	Multicast  m_mcast;
	Multicast *m_mcasts;

	long      m_numRequests;
	long      m_numReplies;
	//long      m_numSplit;
	long      m_errno;
	// local reply, need to handle it for splitting
	char     *m_replyBuf;
	long      m_replyBufSize;
//#else
//	Multicast m_mcast;
//#endif

	// ptr to passed list we're to fill
	class RdbList  *m_list;

	// use this list if you don't want to malloc one
	RdbList m_handyList;

	// . local rdb as specified by m_rdbId and gotten by getRdb(char rdbId)
	// . also, it's fixedDataSize
	//class Rdb      *m_rdb;
	long      m_fixedDataSize;
	bool      m_useHalfKeys;

	// should we add an received lists from across network to our cache?
	bool  m_addToCache;

	long m_hackxd;

	// . parameters that define the RdbList we want
	// . we use precisely this block to define a network request 
	//key_t m_startKey    ;
	//key_t m_endKey      ;
	char  m_startKey[MAX_KEY_BYTES];
	char  m_endKey[MAX_KEY_BYTES];
	long  m_minRecSizes ;
	char  m_rdbId       ;
	char *m_coll        ;

	class Msg5  *m_msg5 ;
	class Msg5  *m_msg5b;
	bool         m_deleteMsg5;
	bool         m_deleteMsg5b;
	bool         m_isRealMerge;

	// for timing the get
	long long m_startTime;

	// and for reporting niceness
	long m_niceness;

	char m_ks;

	// for allowing the page cache
	bool m_allowPageCache;

	// this is a hack so Msg51 can store his this ptr here
	void *m_parent;  // used by Msg51 and by Msg2.cpp
	long  m_slot51;  // for resending on same Msg0 slot in array
	void *m_dataPtr; // for holding recepient record ptr of TopNode ptr
	char  m_inUse;
};

#endif
