// Matt Wells, copyright Feb 2001

// . get a list from any rdb (hostmap,tagdb,termdb,titledb,quotadb)
// . TODO: support concatenation of lists from different groups

#ifndef _MSG0_H_
#define _MSG0_H_

#include "UdpServer.h"
#include "Multicast.h"
#include "Hostdb.h"
#include "Indexdb.h"

#define RDBIDOFFSET (8+4+4+4+4)


/*
// termlist cache accessor functions for Msg5.cpp to use
extern RdbCache g_termListCache;
class RdbCache *getTermListCache ( ) ;
int64_t getTermListCacheKey ( char *startKey , char *endKey ) ;
bool addRecToTermListCache ( char *coll,
			     char *startKey , 
			     char *endKey , 
			     char *list ,
			     int32_t  listSize ) ;
bool getListFromTermListCache ( char *coll,
				char *startKey,
				char *endKey,
				int32_t  maxCacheAge,
				RdbList *list ) ;
bool getRecFromTermListCache ( char *coll,
			       char *startKey,
			       char *endKey,
			       int32_t  maxCacheAge,
			       char **rec ,
			       int32_t *recSize ) ;
*/

//#define MSG0_REQ_SIZE (8 + 2 * sizeof(key_t) + 16 + 5 + MAX_COLL_LEN + 1 )
#define MSG0_REQ_SIZE (8 + 2 * MAX_KEY_BYTES + 16 + 5 + 4 + 1 + 1 )

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
	bool getList ( int64_t hostId      , // -1 if unspecified
		       int32_t      ip          , // info on hostId
		       int16_t     port        ,
		       int32_t      maxCacheAge , // max cached age in seconds
		       bool      addToCache  , // add net recv'd list to cache?
		       char      rdbId       , // specifies the rdb
		       //char     *coll        ,
		       collnum_t collnum ,
		       class RdbList  *list  ,
		       //key_t     startKey    , 
		       //key_t     endKey      , 
		       char     *startKey    ,
		       char     *endKey      ,
		       int32_t      minRecSizes ,  // Positive values only
		       void     *state       ,
		       //void  (* callback)(void *state , class RdbList *list),
		       void  (* callback)(void *state ),
		       int32_t      niceness    ,
		       bool      doErrorCorrection = true ,
		       bool      includeTree       = true ,
		       bool      doMerge           = true ,
		       int32_t      firstHostId       = -1   ,
		       int32_t      startFileNum      =  0   ,
		       int32_t      numFiles          = -1   ,
		       int32_t      timeout           = 30   ,
		       int64_t syncPoint         = -1   ,
		       int32_t      preferLocalReads  = -1   , // -1=use g_conf
		       class Msg5 *msg5            = NULL ,
		       class Msg5 *msg5b           = NULL ,
		       bool        isRealMerge     = false , // file merge?
//#ifdef SPLIT_INDEXDB
		       bool        allowPageCache  = true ,
		       bool        forceLocalIndexdb = false,
		       bool        noSplit           = false , // MDW ????
		       int32_t        forceParitySplit = -1    );
//#else
//		       bool        allowPageCache  = true );
//#endif

	bool getList ( int64_t hostId      , // -1 if unspecified
		       int32_t      ip          , // info on hostId
		       int16_t     port        ,
		       int32_t      maxCacheAge , // max cached age in seconds
		       bool      addToCache  , // add net recv'd list to cache?
		       char      rdbId       , // specifies the rdb
		       //char     *coll        ,
		       collnum_t collnum ,
		       class RdbList  *list  ,
		       key_t     startKey    , 
		       key_t     endKey      , 
		       int32_t      minRecSizes , // Positive values only 
		       void     *state       ,
		       //void (* callback)(void *state , class RdbList *list),
		       void    (* callback)(void *state ),
		       int32_t      niceness    ,
		       bool      doErrorCorrection = true ,
		       bool      includeTree       = true ,
		       bool      doMerge           = true ,
		       int32_t      firstHostId       = -1   ,
		       int32_t      startFileNum      =  0   ,
		       int32_t      numFiles          = -1   ,
		       int32_t      timeout           = 30   ,
		       int64_t syncPoint         = -1   ,
		       int32_t      preferLocalReads  = -1   , // -1=use g_conf
		       class Msg5 *msg5            = NULL ,
		       class Msg5 *msg5b           = NULL ,
		       bool        isRealMerge     = false, // file merge?
//#ifdef SPLIT_INDEXDB
		       bool        allowPageCache  = true ,
		       bool        forceLocalIndexdb = false,
		       // default for this should be false, because true
		       // means to send a msg0 to every indexdb split!
		       bool        doIndexdbSplit    = false ,
		       int32_t        forceParitySplit = -1    ) {
//#else
//		       bool        allowPageCache  = true ) {
//#endif

		return getList ( hostId      , 
				 ip          ,
				 port        ,
				 maxCacheAge ,
				 addToCache  ,
				 rdbId       ,
				 collnum     ,
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

	bool getList ( int32_t firstHostId ) ;

	// gotta keep this handler public so the C wrappers can call them
	void gotReply   ( char *reply , int32_t replySize , int32_t replyMaxSize );
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
	int64_t m_hostId;
	int32_t      m_ip;
	int16_t     m_port;

	// group we sent RdbList request to
	//uint32_t  m_groupId;    
	uint32_t  m_shardNum;

	UdpSlot  *m_slot;

	// 2*4 + 1 + 2 * keySize
	char      m_request [ MSG0_REQ_SIZE ];
	int32_t      m_requestSize;

	// used for multicasting the request
//#ifdef SPLIT_INDEXDB
	//Multicast m_mcast[INDEXDB_SPLIT];
	//Multicast m_mcast[MAX_SHARDS];
	// casting to multiple splits is obsolete, but for PageIndexdb.cpp
	// we still need to do it, but we alloc for it
	Multicast  m_mcast;
	Multicast *m_mcasts;

	int32_t      m_numRequests;
	int32_t      m_numReplies;
	//int32_t      m_numSplit;
	int32_t      m_errno;
	// local reply, need to handle it for splitting
	char     *m_replyBuf;
	int32_t      m_replyBufSize;
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
	int32_t      m_fixedDataSize;
	bool      m_useHalfKeys;

	// should we add an received lists from across network to our cache?
	bool  m_addToCache;

	class XmlDoc *m_hackxd;

	// . parameters that define the RdbList we want
	// . we use precisely this block to define a network request 
	//key_t m_startKey    ;
	//key_t m_endKey      ;
	char  m_startKey[MAX_KEY_BYTES];
	char  m_endKey[MAX_KEY_BYTES];
	int32_t  m_minRecSizes ;
	char  m_rdbId       ;
	//char *m_coll        ;
	collnum_t m_collnum;

	class Msg5  *m_msg5 ;
	class Msg5  *m_msg5b;
	bool         m_deleteMsg5;
	bool         m_deleteMsg5b;
	bool         m_isRealMerge;

	// for timing the get
	int64_t m_startTime;

	// and for reporting niceness
	int32_t m_niceness;

	char m_ks;

	// for allowing the page cache
	bool m_allowPageCache;

	// this is a hack so Msg51 can store his this ptr here
	void *m_parent;  // used by Msg51 and by Msg2.cpp
	int32_t  m_slot51;  // for resending on same Msg0 slot in array
	void *m_dataPtr; // for holding recepient record ptr of TopNode ptr
	char  m_inUse;
};

#endif
