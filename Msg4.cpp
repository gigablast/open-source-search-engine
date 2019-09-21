#include "gb-include.h"

#include "UdpServer.h"
#include "Hostdb.h"
#include "Msg0.h"      // for getRdb(char rdbId)
#include "Msg4.h"
#include "Tfndb.h"
#include "Clusterdb.h"
#include "Spider.h"
//#include "Checksumdb.h"
#include "Datedb.h"
#include "Rdb.h"
//#include "Indexdb.h"
#include "Profiler.h"
#include "Repair.h"
#include "Multicast.h"
#include "Syncdb.h"

//////////////
//
// Send out our records to add every X ms here:
//
// Batching up the add requests saves udp traffic
// on large networks (100+ hosts).
//
// . currently: send out adds once every 500ms
// . when this was 5000ms (5s) it would wait like
//   5s to spider a url after adding it.
//
//////////////
//#define MSG4_WAIT 500

// article1.html and article11.html are dups but they are being spidered
// within 500ms of another
#define MSG4_WAIT 100


// we have up to this many outstanding Multicasts to send add requests to hosts
#define MAX_MCASTS 128
Multicast  s_mcasts[MAX_MCASTS];
Multicast *s_mcastHead = NULL;
Multicast *s_mcastTail = NULL;
int32_t       s_mcastsOut = 0;
int32_t       s_mcastsIn  = 0;

// we have one buffer for each host in the cluster
static char *s_hostBufs     [MAX_HOSTS];
static int32_t  s_hostBufSizes [MAX_HOSTS];
static int32_t  s_numHostBufs;

// . each host has a 32k add buffer which is sent when full or every 10 seconds
// . buffer will be more than 32k if the record to add is larger than 32k
#define MAXHOSTBUFSIZE (32*1024)

// the linked list of Msg4s waiting in line
static Msg4 *s_msg4Head = NULL;
static Msg4 *s_msg4Tail = NULL;

// . TODO: use this instead of spiderrestore.dat
// . call this once for every Msg14 so it can add all at once...
// . make Msg14 add the links before anything else since that uses Msg10
// . also, need to update spiderdb rec for the url in Msg14 using Msg4 too!
// . need to add support for passing in array of lists for Msg14

static void       gotReplyWrapper4 ( void    *state   , void *state2   ) ;
static void       storeLineWaiters ( ) ;
static void       handleRequest4   ( UdpSlot *slot    , int32_t  niceness ) ;
static void       sleepCallback4   ( int bogusfd      , void *state    ) ;
static bool       sendBuffer       ( int32_t hostId , int32_t niceness ) ;
static Multicast *getMulticast     ( ) ;
static void       returnMulticast  ( Multicast *mcast ) ;
//static void processSpecialSignal ( collnum_t collnum , char *p ) ;
//static bool storeList2 ( RdbList *list , char rdbId , collnum_t collnum,
//			 bool forceLocal, bool splitList , int32_t niceness );
static bool storeRec   ( collnum_t      collnum , 
			 char           rdbId   ,
			 uint32_t  gid     ,
			 int32_t           hostId  ,
			 char          *rec     ,
			 int32_t           recSize ,
			 int32_t           niceness ) ;

// all these parameters should be preset
bool registerHandler4 ( ) {
	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( 0x04, handleRequest4 ) )
		return false;

	// clear the host bufs
	s_numHostBufs = g_hostdb.getNumShards();
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ )
		s_hostBufs[i] = NULL;

	// init the linked list of multicasts
	s_mcastHead = &s_mcasts[0];
	s_mcastTail = &s_mcasts[MAX_MCASTS-1];
	for ( int32_t i = 0 ; i < MAX_MCASTS - 1 ; i++ ) 
		s_mcasts[i].m_next = &s_mcasts[i+1];
	// last guy has nobody after him
	s_mcastTail->m_next = NULL;

	// nobody is waiting in line
	s_msg4Head = NULL;
	s_msg4Tail = NULL;

	// spider hang bug
	//logf(LOG_DEBUG,"msg4: registering handler.");

	// for now skip it
	//return true;

	// . restore state from disk
	// . false means repair is not active
	if ( ! loadAddsInProgress ( NULL ) ) {
		log("init: Could not load addsinprogress.dat. Ignoring.");
		g_errno = 0;
	}

	// . register sleep handler every 5 seconds = 5000 ms
	// . right now MSG4_WAIT is 500ms... i lowered it from 5s
	//   to speed up spidering so it would harvest outlinks
	//   faster and be able to spider them right away.
	// . returns false on failure
	return g_loop.registerSleepCallback(MSG4_WAIT,NULL,sleepCallback4 );
}

static void flushLocal ( ) ;

// scan all host bufs and try to send on them
void sleepCallback4 ( int bogusfd , void    *state ) {
	// wait for clock to be in sync
	if ( ! isClockInSync() ) return;
	// flush them buffers
	flushLocal();
}

void flushLocal ( ) {
	g_errno = 0;
	// put the line waiters into the buffers in case they are not there
	//storeLineWaiters();
	// now try to send the buffers
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) 
		sendBuffer ( i , MAX_NICENESS );
	g_errno = 0;
}

//static void (* s_flushCallback) ( void *state ) = NULL ;
//static void  * s_flushState = NULL;

// for holding flush callback data
static SafeBuf s_callbackBuf;
static int32_t    s_numCallbacks = 0;

class CBEntry {
public:
	int64_t m_timestamp;
	void (*m_callback)(void *);
	void *m_callbackState;
};


// . injecting into the "qatest123" coll flushes after each inject
// . returns false if blocked and callback will be called
bool flushMsg4Buffers ( void *state , void (* callback) (void *) ) {
	// if all empty, return true now
	if ( ! hasAddsInQueue () ) return true;

	// how much per callback?
	int32_t cbackSize = sizeof(CBEntry);
	// ensure big enough for first call
	if ( s_callbackBuf.m_capacity == 0 ) { // length() == 0 ) {
		// make big
		if ( ! s_callbackBuf.reserve ( 300 * cbackSize ) ) {
			// return true with g_errno set on error
			log("msg4: error allocating space for flush callback");
			return true;
		}
		// then init
		s_callbackBuf.zeroOut();
	}

	// scan for empty slot
	char *buf = s_callbackBuf.getBufStart();
	CBEntry *cb    = (CBEntry *)buf;
	CBEntry *cbEnd = (CBEntry *)(buf + s_callbackBuf.getCapacity());

	// find empty slot
	for ( ; cb < cbEnd && cb->m_callback ;  cb++ ) ;

	// no room?
	if ( cb >= cbEnd ) {
		log("msg4: no room for flush callback. count=%"INT32"",
		    (int32_t)s_numCallbacks);
		g_errno = EBUFTOOSMALL;
		return true;
	}
	
	// add callback to list
	// time must be the same as used by UdpSlot::m_startTime
	cb->m_callback = callback;
	cb->m_callbackState = state;

	// inc count
	s_numCallbacks++;

	//if ( s_flushCallback ) { char *xx=NULL;*xx=0; }
	// start it up
	flushLocal();

	// scan msg4 slots for maximum start time so we can only
	// call the flush done callback when all msg4 slots in udpserver
	// have start times STRICTLY GREATER THAN that, then we will
	// be guaranteed that everything we added has been replied to!
	UdpSlot *slot = g_udpServer.getActiveHead();
	int64_t max = 0LL;
	for ( ; slot ; slot = slot->m_next ) {
		// get its time stamp 
		if ( slot->m_msgType != 0x04 ) continue;
		// must be initiated by us
		if ( ! slot->m_callback ) continue;
		// get it
		if ( max && slot->m_startTime < max ) continue;
		// got a new max
		max = slot->m_startTime;
	}

	// set time AFTER the udpslot gets its m_startTime set so
	// now will be >= each slot's m_startTime.
	cb->m_timestamp = max;

	// can we sometimes flush without blocking? maybe...
	//if ( ! hasAddsInQueue () ) return true;
	// assign it
	//s_flushState    = state;
	//s_flushCallback = callback;
	// we are waiting now
	return false;
}

// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue   ( ) {
	// if there is an outstanding multicast...
	if ( s_mcastsOut > s_mcastsIn ) return true;
	// if we have a msg4 waiting in line...
	if ( s_msg4Head               ) return true;
	// if we have an host buf that has something in it...
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		if ( ! s_hostBufs[i] ) continue;
		if ( *(int32_t *)s_hostBufs[i] > 4 ) return true;
	}
	// otherwise, we have nothing queued up to add
	return false;
}

/*
// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
// . forceLocal was removed because if you want to delete a corrupt key in
//   spiderdb, it should be removed in a merge or something...
bool Msg4::addList ( RdbList *list                   ,
		     char     rdbId                  ,
		     char    *coll                   ,
		     void    *state                  ,
		     void  (* callback)(void *state) ,
		     int32_t     niceness               ,
		     bool     forceLocal             ,
		     bool     splitList              ) {
	// warning
	if ( ! coll ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"net: NULL collection. msg4.cpp.");
		return true;
	}
	// save it
	strcpy ( m_coll , coll );
	// make it a collnum
	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	// is it non-existent
	if ( collnum == (collnum_t)-1 ) {
		g_errno = EBADENGINEER;
		log(LOG_LOGIC,"build: msg4: bad coll %s",coll);
		return true;
	}
	// then re-call
	return addList ( list , rdbId , collnum , 
			 state , callback , niceness,
			 forceLocal, splitList);
}

// . returns false if blocked, true otherwise
// . returns true and sets g_errno on error
bool Msg4::addList ( RdbList   *list                   ,
		     char       rdbId                  ,
		     collnum_t  collnum                ,
		     void      *state                  ,
		     void    (* callback)(void *state) ,
		     int32_t       niceness               ,
		     bool       forceLocal             ,
		     bool       splitList              ) {

	// sanity check
	//if ( niceness != MAX_NICENESS ) { char *xx=NULL;*xx=0;}
	// clear it
	g_errno = 0;
	// if list has no records in it return true
	if ( ! list || list->isEmpty() ) return true;
	// save it
	m_rdbId    = rdbId;
	m_niceness = niceness;
	// this is only valid if storeList() ends up returning false, otherwise
	// the caller may free it
	m_list       = list;
	m_callback   = callback;
	m_state      = state;
	m_forceLocal = forceLocal;
	m_splitList  = splitList;

	// MDW: make sure we point to start of the list
	list->resetListPtr();

	// MDW: need to reset the list ptr, otherwise twin might not get the
	//      recs if we are adding to spiderdb
	list->resetListPtr();

	// . store the list in the buffer for each hostid
	// . have a buffer for each host and each rdb
	// . and each collnum
	// . this returns true if stored, false if could not store
	if ( ! storeList ( list , rdbId , collnum ) ) return false;
	// launch anyone that needs it
	//launchBuffers ( );
	// otherwise, g_errno should be set
	return true;
}

// . split the list up into pieces based on hostname
// . call storeSubList() on each piece
// . returns false and sets g_errno on failure
bool Msg4::storeList ( RdbList *list , char rdbId , collnum_t collnum ) {

	// sanity check
	if ( rdbId < 0 ) { char *xx=NULL;*xx=0; }

	// nobody is after us in the linked list
	m_next = NULL;

	// . if others are waiting in line, we must wait in line to in case 
	//   there is an order dependency in the records being added
	// . however, if we are the head of the list we are being called from
	//   handleReply4() and this is an attempt to finish processing this
	//   list.
	//if ( s_msg4Tail && this != s_msg4Head ) {
	if ( s_msg4Tail ) {
		// sanity check -- detect re-use of a blocked msg4!
		if ( this == s_msg4Head ) { char *xx =NULL; *xx=0; }
		if ( ! s_msg4Head       ) { char *xx =NULL; *xx=0; }
		// spider hang bug
		//logf(LOG_DEBUG,
		//   "db: msg4 blocked. adding to tail. msg4=%"INT32"",(int32_t)this);
		s_msg4Tail->m_next = this;
		s_msg4Tail         = this;
		return false;
	}

	// this returns true if all of the records in the list were 
	// successfully stored in the s_hostBufs[] buffers for sending, 
	// otherwise it returns false and we must call storeList2() again
	// for this list when a Multicast becomes available.
	if ( storeList2 ( list , rdbId , collnum , m_forceLocal, m_splitList,
			  m_niceness ) )
		return true;

	// spider hang bug
	//logf(LOG_DEBUG,"build: msg4 first in line. msg4=%"INT32"",(int32_t)this);

	// sanity check
	if ( s_msg4Head || s_msg4Tail ) { char *xx=NULL; *xx=0; }

	// . wait in line
	// . when the s_hostBufs[hostId] is able to accomodate our
	//   record this loop will be resumed and the caller's callback
	//   will be called once we are able to successfully queue up
	//   all recs in the list
	// . we are the only one in line, otherwise, we would have exited
	//   the start of this function
	s_msg4Head = this;
	s_msg4Tail = this;
	
	// return false so caller blocks. we will call his callback
	// when we are able to add his list to the hostBufs[] queue
	// and then he can re-use this Msg4 class for other things.
	return false;
}

bool storeList2 ( RdbList *list , 
		  char rdbId , 
		  collnum_t collnum ,
		  bool forceLocal,
		  bool splitList ,
		  int32_t niceness ) {

	// get groupId of each key
	uint32_t gid;
	char          key[MAX_KEY_BYTES];

	// sanity check
	if ( rdbId < 0 ) { 
		log("repair: Consider erasing repair.dat and "
		    "repair-addsinprogress.dat to restart the repair IF "
		    "you were doing a repair.");
		char *xx=NULL;*xx=0; 
	}

	// store each record in the list into the send buffers
	while ( ! list->isExhausted() ) {
		// get the key of the current record
		list->getCurrentKey ( key );
		// . if key belongs to same group as firstKey then continue
		// . titledb now uses last bits of docId to determine groupId
		// . but uses the top 32 bits of key still
		// . spiderdb uses last 64 bits to determine groupId
		// . tfndb now is like titledb(top 32 bits are top 32 of docId)
		if(forceLocal)    gid = g_hostdb.m_groupId;
		else              gid = getGroupId ( rdbId , key , splitList );

		char *rec     = list->getCurrentRec();
		int32_t  recSize = list->getCurrentRecSize();

		// i fixed UdpServer.cpp to NOT call msg4 handlers when in
		// a quickpoll, in case we receive a niceness 0 msg4 request
 		QUICKPOLL(niceness); // MAX_NICENESS);

		// convert the gid to the hostid of the first host in this
		// group. uses a quick hash table.
		int32_t hostId = g_hostdb.makeHostIdFast ( gid );

		// . add that rec to this groupId, gid, includes the key
		// . these are NOT allowed to be compressed (half bit set)
		//   and this point
		// . this returns false and sets g_errno on failure
		if ( storeRec ( collnum, rdbId, gid, hostId, rec, recSize ,
				niceness )) {
			// . point to next record
			// . will point past records if no more left!
			list->skipCurrentRecord();
			// get next rec
			continue;
		}

		// g_errno is not set if the store rec could not send the
		// buffer because no multicast was available
		if ( g_errno ) 
			log("build: Msg4 storeRec had error: %s.",
			    mstrerror(g_errno));

		// clear this just in case
		g_errno = 0;

		return false;
	}
			       
	return true;
}
*/

// returns false if blocked
bool Msg4::addMetaList ( char  *metaList                , 
			 int32_t   metaListSize            ,
			 char  *coll                    ,
			 void  *state                   ,
			 void (* callback)(void *state) ,
			 int32_t   niceness                ,
			 char   rdbId                   ) {

	collnum_t collnum = g_collectiondb.getCollnum ( coll );
	return addMetaList ( metaList     ,
			     metaListSize ,
			     collnum      ,
			     state        ,
			     callback     ,
			     niceness     ,
			     rdbId        );
}

bool Msg4::addMetaList ( SafeBuf *sb ,
			 collnum_t  collnum                  ,
			 void      *state                    ,
			 void      (* callback)(void *state) ,
			 int32_t       niceness                 ,
			 char       rdbId                    ,
			 int32_t       shardOverride ) {
	return addMetaList ( sb->getBufStart() ,
			     sb->length() ,
			     collnum ,
			     state ,
			     callback ,
			     niceness ,
			     rdbId ,
			     shardOverride );
}


bool Msg4::addMetaList ( char      *metaList                 , 
			 int32_t       metaListSize             ,
			 collnum_t  collnum                  ,
			 void      *state                    ,
			 void      (* callback)(void *state) ,
			 int32_t       niceness                 ,
			 char       rdbId                    ,
			 // Rebalance.cpp needs to add negative keys to
			 // remove foreign records from where they no
			 // longer belong because of a new hosts.conf file.
			 // This will be -1 if not be overridden.
			 int32_t       shardOverride ) {

	// not in progress
	m_inUse = false;

	// empty lists are easy!
	if ( metaListSize == 0 ) return true;

	// sanity
	//if ( collnum < 0 || collnum > 1000 ) { char *xx=NULL;*xx=0; }
	if ( collnum < 0 ) { char *xx=NULL;*xx=0; }

	// if first time set this
	m_currentPtr   = metaList;
	m_metaList     = metaList;
	m_metaListSize = metaListSize;
	m_collnum      = collnum;
	m_state        = state;
	m_callback     = callback;
	m_rdbId        = rdbId;
	m_niceness     = niceness;
	m_next         = NULL;
	m_shardOverride = shardOverride;

 retry:

	// get in line if there's a line
	if ( s_msg4Head ) {
		// add ourselves to the line
		s_msg4Tail->m_next = this;
		// we are the new tail
		s_msg4Tail = this;
		// debug log. seems to happen a lot if not using threads..
		if ( g_conf.m_useThreads )
			log("msg4: queueing body msg4=0x%"PTRFMT"",(PTRTYPE)this);
		// mark it
		m_inUse = true;
		// all done then, but return false so caller does not free
		// this msg4
		return false;
	}

	// then do it
	if ( addMetaList2 ( ) ) return true;

	// . sanity check
	// . we sometimes get called with niceness 0 from possibly
	//   an injection or something and from a quickpoll
	//   inside addMetList2() in which case our addMetaList2() will
	//   fail, assuming s_msg4Head got set, BUT it SHOULD be OK because
	//   being interrupted at the one QUICKPOLL() in addMetaList2()
	//   doesn't seem like it would hurt.
	// . FURTHEMORE the multicast seems to always be called with
	//   MAX_NICENESS so i'm not sure how niceness 0 will really help
	//   with any of this stuff.
	//if ( s_msg4Head || s_msg4Tail ) { char *xx=NULL; *xx=0; }
	if ( s_msg4Head || s_msg4Tail ) {
		log("msg4: got unexpected head"); // :)
		goto retry;
	}

	// . spider hang bug
	// . debug log. seems to happen a lot if not using threads..
	if ( g_conf.m_useThreads )
		logf(LOG_DEBUG,"msg4: queueing head msg4=0x%"PTRFMT"",(PTRTYPE)this);

	// mark it
	m_inUse = true;

	// . wait in line
	// . when the s_hostBufs[hostId] is able to accomodate our
	//   record this loop will be resumed and the caller's callback
	//   will be called once we are able to successfully queue up
	//   all recs in the list
	// . we are the only one in line, otherwise, we would have exited
	//   the start of this function
	s_msg4Head = this;
	s_msg4Tail = this;
	
	// return false so caller blocks. we will call his callback
	// when we are able to add his list to the hostBufs[] queue
	// and then he can re-use this Msg4 class for other things.
	return false;
}

bool isInMsg4LinkedList ( Msg4 *msg4 ) {
	Msg4 *m = s_msg4Head;
	for ( ; m ; m = m->m_next ) 
		if ( m == msg4 ) return true;
	return false;
}

bool Msg4::addMetaList2 ( ) {

	char *p = m_currentPtr;

	// get the collnum
	//collnum_t collnum = g_collectiondb.getCollnum ( m_coll );

	char *pend = m_metaList + m_metaListSize;

	//if ( m_collnum < 0 || m_collnum > 1000 ) { char *xx=NULL;*xx=0; }
	if ( m_collnum < 0 ) { char *xx=NULL;*xx=0; }

	// store each record in the list into the send buffers
	for ( ; p < pend ; ) {
		// first is rdbId
		char rdbId = m_rdbId;
		if ( rdbId < 0 ) rdbId = *p++;
		// get nosplit
		//bool nosplit = ( rdbId & 0x80 ) ;
		// mask off rdbId
		rdbId &= 0x7f;
		// get the key of the current record
		char *key = p; 
		// negative key?
		bool del ;
		if ( *p & 0x01 ) del = false;
		else             del = true;
		// tmp debug
		//if ( del ) { char *xx=NULL;*xx=0;}
		// get the key size. a table lookup in Rdb.cpp.
		int32_t ks ;
		if      ( rdbId == RDB_POSDB || rdbId == RDB2_POSDB2) ks = 18;
		else if ( rdbId == RDB_DATEDB  ) ks = 16;
		else ks = getKeySizeFromRdbId ( rdbId );
		// skip key
		p += ks;
		// set this
		//bool split = true; if ( nosplit ) split = false;
		// . if key belongs to same group as firstKey then continue
		// . titledb now uses last bits of docId to determine groupId
		// . but uses the top 32 bits of key still
		// . spiderdb uses last 64 bits to determine groupId
		// . tfndb now is like titledb(top 32 bits are top 32 of docId)
		//uint32_t gid = getGroupId ( rdbId , key , split );
		uint32_t shardNum = getShardNum( rdbId , key );
		// override it from Rebalance.cpp for redistributing records
		// after updating hosts.conf?
		if ( m_shardOverride >= 0 ) shardNum = m_shardOverride;
		// get the record, is -1 if variable. a table lookup.
		int32_t dataSize;
		if      ( rdbId==RDB_POSDB || rdbId==RDB2_POSDB2) dataSize = 0;
		else if ( rdbId == RDB_DATEDB  ) dataSize = 0;
		else dataSize = getDataSizeFromRdbId ( rdbId );
		// . negative keys have no data
		// . this unfortunately is not true according to RdbList.cpp
		if ( del ) dataSize = 0;
		// if variable read that in
		if ( dataSize == -1 ) {
			// -1 means to read it in
			dataSize = *(int32_t *)p;
			// sanity check
			if ( dataSize < 0 ) { char *xx=NULL;*xx=0; }
			// sanity check
			//if ( rdbId == RDB_DOLEDB && 
			//     (*key & 0x01) == 0x01 && // positive key
			//     dataSize <= 0 ) {
			//	char *xx=NULL;*xx=0; }
			// skip dataSize
			p += 4;
		}
		// skip over the data, if any
		p += dataSize;
		// breach us?
		if ( p > pend ) { char *xx=NULL;*xx=0; }
		// i fixed UdpServer.cpp to NOT call msg4 handlers when in
		// a quickpoll, in case we receive a niceness 0 msg4 request
 		QUICKPOLL(m_niceness); // MAX_NICENESS);
		// convert the gid to the hostid of the first host in this
		// group. uses a quick hash table.
		//int32_t hostId = g_hostdb.makeHostIdFast ( gid );
		Host *hosts = g_hostdb.getShard ( shardNum );
		int32_t hostId = hosts[0].m_hostId;
		// . add that rec to this groupId, gid, includes the key
		// . these are NOT allowed to be compressed (half bit set)
		//   and this point
		// . this returns false and sets g_errno on failure
		if ( storeRec ( m_collnum, 
				rdbId, 
				shardNum,//gid, 
				hostId, 
				key, // start of rec, 
				p - key , // recSize
				m_niceness )) {
			// . point to next record
			// . will point past records if no more left!
			m_currentPtr = p; // += recSize;
			// debug log
			// int off = (int)(m_currentPtr-m_metaList);
			// log("msg4: cpoff=%i",off);
			// if ( off == 5271931 )
			// 	log("msg4: hey");
			// debug
			// get next rec
			continue;
		}

		// g_errno is not set if the store rec could not send the
		// buffer because no multicast was available
		if ( g_errno ) 
			log("build: Msg4 storeRec had error: %s.",
			    mstrerror(g_errno));

		// clear this just in case
		g_errno = 0;

		// if g_errno was not set, this just means we do not have
		// room for the data yet, and try again later
		return false;
	}

	// . send out all bufs
	// . before we were caching to reduce packet traffic, but
	//   since we don't use the network for sending termlists let's
	//   try going back to making it even more real-time
	//if ( ! isClockInSync() ) return true;
	// flush them buffers
	//flushLocal();
			       
	// in case this was being used to hold the data, free it
	m_tmpBuf.purge();

	return true;
}

// . modify each Msg4 request as follows
// . collnum(2bytes)|rdbId(1bytes)|listSize&rawlistData|...
// . store these requests in the buffer just like that
bool storeRec ( collnum_t      collnum , 
		char           rdbId   ,
		uint32_t  shardNum, //gid
		int32_t           hostId  ,
		char          *rec     ,
		int32_t           recSize ,
		int32_t           niceness ) {
	// loop back up here if you have to flush the buffer
 retry:
	// sanity check
	//if ( recSize==16 && rdbId==RDB_SPIDERDB && *(int32_t *)(rec+12)!=0 ) {
	//	char *xx=NULL; *xx=0; }
	// . how many bytes do we need to store the request?
	// . USED(4 bytes)/collnum/rdbId(1)/recSize(4bytes)/recData
	// . "USED" is only used for mallocing new slots really
	int32_t  needForRec = sizeof(collnum_t) + 1 + 4 + recSize;
	int32_t  needForBuf = 4 + needForRec;
	// 8 bytes for the zid
	needForBuf += 8;
	// how many bytes of the buffer are occupied or "in use"?
	char *buf = s_hostBufs[hostId];
	// if NULL, try to allocate one
	if ( ! buf  || s_hostBufSizes[hostId] < needForBuf ) {
		// how big to make it
		int32_t size = MAXHOSTBUFSIZE;
		// must accomodate rec at all costs
		if ( size < needForBuf ) size = needForBuf;
		// make them all the same size
		buf = (char *)mmalloc ( size , "Msg4a" );
		// if still no luck, we cannot send this msg
		if ( ! buf ) return false;
		
		if(s_hostBufs[hostId]) {
			//if the old buf was too small, resize
			gbmemcpy( buf, s_hostBufs[hostId], 
				*(int32_t*)(s_hostBufs[hostId])); 
			mfree ( s_hostBufs[hostId], 
				s_hostBufSizes[hostId] , "Msg4b" );
		}
		// if we are making a brand new buf, init the used
		// size to "4" bytes
		else
			// itself(4) PLUS the zid (8 bytes)
			*(int32_t *)buf = 4 + 8;
		// add it
		s_hostBufs    [hostId] = buf;
		s_hostBufSizes[hostId] = size;
	}
	// . first int32_t is how much of "buf" is used
	// . includes everything even itself
	int32_t  used = *(int32_t *)buf;
	// sanity chec. "used" must include the 4 bytes of itself
	if ( used < 12 ) { char *xx = NULL; *xx = 0; }
	// how much total buf space do we have, used or unused?
	int32_t  maxSize = s_hostBufSizes[hostId];
	// how many bytes are available in "buf"?
	int32_t  avail   = maxSize - used;
	// if we can not fit list into buffer...
	if ( avail < needForRec ) {
		// . send what is already in the buffer and clear it
		// . will set s_hostBufs[hostId] to NULL
		// . this will return false if no available Multicasts to
		//   send the buffer, in which case we must tell the caller
		//   to block and wait for us to call his callback, only then
		//   will he be able to proceed. we will call his callback
		//   as soon as we can copy... use this->m_msg1 to add the
		//   list that was passed in...
		if ( ! sendBuffer ( hostId , niceness ) ) return false;
		// now the buffer should be empty, try again
		goto retry;
	}
	// point to where to store the list
	char *start = buf + used;
	char *p     = start;
	// store the record and all the info for it
	*(collnum_t *)p = collnum; p += sizeof(collnum_t);
	*(char      *)p = rdbId  ; p += 1;
	*(int32_t      *)p = recSize; p += 4;
	gbmemcpy ( p , rec , recSize ); p += recSize;
	// update buffer used
	*(int32_t *)buf = used + (p - start);
	// all done, did not "block"
	return true;
}

// . returns false if we were UNable to get a multicast to launch the buffer, 
//   true otherwise
// . returns false and sets g_errno on error
bool sendBuffer ( int32_t hostId , int32_t niceness ) {
	//logf(LOG_DEBUG,"build: sending buf");
	// how many bytes of the buffer are occupied or "in use"?
	char *buf       = s_hostBufs    [hostId];
	int32_t  allocSize = s_hostBufSizes[hostId];
	// skip if empty
	if ( ! buf ) return true;
	// . get size used in buf
	// . includes everything, including itself!
	int32_t used = *(int32_t *)buf;
	// if empty, bail
	if ( used <= 12 ) return true;
	// grab a vehicle for sending the buffer
	Multicast *mcast = getMulticast();
	// if we could not get one, wait in line for one to become available
	if ( ! mcast ) {
		//logf(LOG_DEBUG,"build: no mcast available");
		return false;
	}
	// NO! storeRec() will alloc it!
	/*
	// make it point to another
	char *newBuf = (char *)mmalloc ( MAXHOSTBUFSIZE , "Msg4Buf" );
	// assign it to the new Buf
	s_hostBufs [ hostId ] = newBuf;
	// reset used
	if ( newBuf ) {
		*(int32_t *)newBuf = 4;
		s_hostBufSizes[hostId] = MAXHOSTBUFSIZE;
	}
	else 	s_hostBufSizes[hostId] = 0; //if we were oom reset size
	*/
	// get groupId
	//uint32_t groupId = g_hostdb.getGroupIdFromHostId ( hostId );
	Host *h = g_hostdb.getHost(hostId);
	uint32_t shardNum = h->m_shardNum;
	// get group #
	//int32_t groupNum = g_hostdb.getGroupNum ( groupId );

	// sanity check. our clock must be in sync with host #0's or with
	// a host from his group, group #0
	if ( ! isClockInSync() ) { 
		log("msg4: msg4: warning sending out adds but clock not in "
		    "sync with host #0");
		//char *xx=NULL ; *xx=0; }
	}
	// try to keep all zids unique, regardless of their group
	static uint64_t s_lastZid = 0;
	// select a "zid", a sync id
	uint64_t zid = gettimeofdayInMilliseconds();
	// keep it strictly increasing
	if ( zid <= s_lastZid ) zid = s_lastZid + 1;
	// update it
	s_lastZid = zid;
	// shift up 1 so Syncdb::makeKey() is easier
	zid <<= 1;
	// set some things up
	char *p = buf + 4;
	// . sneak it into the top of the buffer
	// . TODO: fix the code above for this new header
	*(uint64_t *)p = zid;
	p += 8;
	// syncdb debug
	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"syncdb: sending msg4 request zid=%"UINT64"",zid);

	// this is the request
	char *request     = buf;
	int32_t  requestSize = used;
	// . launch the request
	// . we now have this multicast timeout if a host goes dead on it
	//   and it fails to send its payload
	// . in that case we should restart from the top and we will add
	//   the dead host ids to the top, and multicast will avoid sending
	//   to hostids that are dead now
	key_t k; k.setMin();
	if ( mcast->send ( request    , // sets mcast->m_msg    to this
			   requestSize, // sets mcast->m_msgLen to this
			   0x04       , // msgType for add rdb record
			   false      , // does multicast own msg?
			   shardNum,//groupId , // group to send to (groupKey)
			   true       , // send to whole group?
			   0          , // key is useless for us
			   (void *)(PTRTYPE)allocSize  , // state data
			   (void *)mcast      , // state data
			   gotReplyWrapper4 ,
			   // this was 60 seconds, but if we saved the
			   // addsinprogress at the wrong time we might miss
			   // it when its between having timed out and
			   // having been resent by us!
			   999999999   , // timeout in secs
			   MAX_NICENESS, // niceness
			   false      , // realtime
			   -1         , // first host to try
			   NULL       , // replyBuf        = NULL ,
			   0          , // replyBufMaxSize = 0 ,
			   true       , // freeReplyBuf    = true ,
			   false      , // doDiskLoadBalancing = false ,
			   -1         , // no max cache age limit
			   k          , // cache key
			   RDB_NONE   , // bogus rdbId
			   -1         , // unknown minRecSizes read size
			   true      )) { // sendToSelf?
		// . let storeRec() do all the allocating...
		// . only let the buffer go once multicast succeeds
		s_hostBufs [ hostId ] = NULL;
		// success
		return true;
	}

	// g_errno should be set
	log("net: Had error when sending request to add data to rdb shard "
	    "#%"UINT32": %s.", shardNum,mstrerror(g_errno));

	returnMulticast ( mcast );

	return false;
}

Multicast *getMulticast ( ) {
	// get head
	Multicast *avail = s_mcastHead;
	// return NULL if none available
	if ( ! avail ) return NULL;
	// if all are out then forget it!
	if ( s_mcastsOut - s_mcastsIn >= MAX_MCASTS ) return NULL;
	// remove from head of linked list
	s_mcastHead = avail->m_next;
	// if we were the tail, none now
	if ( s_mcastTail == avail ) s_mcastTail = NULL;
	// count it
	s_mcastsOut++;
	// sanity
	if ( avail->m_inUse ) { char *xx=NULL;*xx=0; }
	// return that
	return avail;
}

void returnMulticast ( Multicast *mcast ) {
	// return this multicast
	mcast->reset();
	// we are at the tail, nobody is after us
	mcast->m_next = NULL;
	// if no tail we are both head and tail
	if ( ! s_mcastTail ) s_mcastHead         = mcast;
	// put after the tail
	else                 s_mcastTail->m_next = mcast;
	// and we are the new tail
	s_mcastTail = mcast;
	// count it
	s_mcastsIn++;
}

// just free the request
void gotReplyWrapper4 ( void *state , void *state2 ) {
	//logf(LOG_DEBUG,"build: got msg4 reply");
	int32_t       allocSize = (int32_t)(PTRTYPE)state;
	Multicast *mcast     = (Multicast *)state2;
	// get the request we sent
	char *request     = mcast->m_msg;
	//int32_t  requestSize = mcast->m_msgSize;
	// get the buffer alloc size
	//int32_t allocSize = requestSize;
	//if ( allocSize < MAXHOSTBUFSIZE ) allocSize = MAXHOSTBUFSIZE;
	if ( request ) mfree ( request , allocSize , "Msg4" );
	// make sure no one else can free it!
	mcast->m_msg = NULL;

	// get the udpslot that is replying here
	UdpSlot *replyingSlot = mcast->m_slot;
	if ( ! replyingSlot ) { char *xx=NULL;*xx=0; }

	returnMulticast ( mcast );

	storeLineWaiters ( ); // try to launch more msg4 requests in waiting

	//
	// now if all buffers are empty, let any flush request know that
	//

	// bail if no callbacks to call
	if ( s_numCallbacks == 0 ) return;

	//log("msg4: got msg4 reply. replyslot starttime=%"INT64" slot=0x%"XINT32"",
	//    replyingSlot->m_startTime,(int32_t)replyingSlot);

	// get the oldest msg4 slot starttime
	UdpSlot *slot = g_udpServer.getActiveHead();
	int64_t min = 0LL;
	for ( ; slot ; slot = slot->m_next ) {
		// get its time stamp
		if ( slot->m_msgType != 0x04 ) continue;
		// must be initiated by us
		if ( ! slot->m_callback ) continue;
		// if it is this replying slot or already had the callback
		// called, then ignore it...
		if ( slot->m_calledCallback ) continue;
		// ignore incoming slot! that could be the slot we were
		// waiting for to complete so its starttime will always
		// be less than our callback's m_timestamp
		//if ( slot == replyingSlot ) continue;
		// log it
		//log("msg4: slot starttime = %"INT64" ",slot->m_startTime);
		// get it
		if ( min && slot->m_startTime >= min ) continue;
		// got a new min
		min = slot->m_startTime;
	}

	// log it
	//log("msg4: slots min = %"INT64" ",min);

	// scan for slots whose callbacks we can call now
	char *buf = s_callbackBuf.getBufStart();
	CBEntry *cb    = (CBEntry *)buf;
	CBEntry *cbEnd = (CBEntry *)(buf + s_callbackBuf.getCapacity());

	// find empty slot
	for ( ; cb < cbEnd ;  cb++ ) {
		// skip if empty
		if ( ! cb->m_callback ) continue;
		// debug
		//log("msg4: cb timestamp = %"INT64"",cb->m_timestamp);
		// wait until callback's stored time is <= all msg4
		// slot's start times, then we can guarantee that all the
		// msg4s required for this callback have replied.
		// min will be zero if no msg4s in there, so call callback.
		if ( min && cb->m_timestamp >= min ) continue;
		// otherwise, call the callback!
		cb->m_callback ( cb->m_callbackState );
		// take out of queue now by setting callback ptr to 0
		cb->m_callback = NULL;
		// discount
		s_numCallbacks--;
	}

	// of course, skip this part if nobody called a flush
	//if ( ! s_flushCallback ) return;
	// if not completely empty, wait!
	if ( hasAddsInQueue () ) {
		// flush away some more just in case
		flushLocal();
		// and wait
		return;
	}
	// seems good to go!
	//s_flushCallback ( s_flushState );
	// nuke it
	//s_flushCallback = NULL;
}

void storeLineWaiters ( ) {
	// try to store all the msg4's lists that are waiting in line
 loop:
	Msg4 *msg4 = s_msg4Head;
	// now were we waiting on a multicast to return in order to send
	// another request?  return if not.
	if ( ! msg4 ) return;
	// grab the first Msg4 in line. ret fls if blocked adding more of list.
	if ( ! msg4->addMetaList2 ( ) ) return;
	// hey, we were able to store that Msg4's list, remove him
	s_msg4Head = msg4->m_next;
	// empty? make tail NULL too then
	if ( ! s_msg4Head ) s_msg4Tail = NULL;
	// . if his callback was NULL, then was loaded in loadAddsInProgress()
	// . we no longer do that so callback should never be null now
	if ( ! msg4->m_callback ) { char *xx=NULL;*xx=0; }
	// log this now i guess. seems to happen a lot if not using threads
	if ( g_conf.m_useThreads )
		logf(LOG_DEBUG,"msg4: calling callback for msg4=0x%"PTRFMT"",
		     (PTRTYPE)msg4);
	// release it
	msg4->m_inUse = false;
	// call his callback
	msg4->m_callback ( msg4->m_state );
	// ensure not re-added - no, msg4 might be freed now!
	//msg4->m_next = NULL;
	// try the next Msg4 in line
	goto loop;
}

#include "Process.h"

// . destroys the slot if false is returned
// . this is registered in Msg4::set() to handle add rdb record msgs
// . seems like we should always send back a reply so we don't leave the
//   requester's slot hanging, unless he can kill it after transmit success???
// . TODO: need we send a reply back on success????
// . NOTE: Must always call g_udpServer::sendReply or sendErrorReply() so
//   read/send bufs can be freed
void handleRequest4 ( UdpSlot *slot , int32_t netnice ) {

	// easy var
	UdpServer *us = &g_udpServer;

	// if we just came up we need to make sure our hosts.conf is in
	// sync with everyone else before accepting this! it might have
	// been the case that the sender thinks our hosts.conf is the same
	// since last time we were up, so it is up to us to check this
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		g_errno = EBADHOSTSCONF;
		us->sendErrorReply ( slot , g_errno );
		return;
	}

	// need to be in sync first
	if ( ! g_pingServer.m_hostsConfInAgreement ) {
		// . if we do not know the sender's hosts.conf crc, wait 4 it
		// . this is 0 if not received yet
		if ( ! slot->m_host->m_pingInfo.m_hostsConfCRC ) {
			g_errno = EWAITINGTOSYNCHOSTSCONF;
			us->sendErrorReply ( slot , g_errno );
			return;
		}
		// compare our hosts.conf to sender's otherwise
		if ( slot->m_host->m_pingInfo.m_hostsConfCRC != 
		     g_hostdb.getCRC() ) {
			g_errno = EBADHOSTSCONF;
			us->sendErrorReply ( slot , g_errno );
			return;
		}
	}


	//logf(LOG_DEBUG,"build: handling msg4 request");
	// extract what we read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;
	// must at least have an rdbId
	if ( readBufSize < 7 ) {
		g_errno = EREQUESTTOOSHORT;
		us->sendErrorReply ( slot , g_errno );
		return;
	}
	//char *p    = readBuf;
	//char *pend = readBuf + readBufSize;

	// get total buf used
	int32_t used = *(int32_t *)readBuf; //p += 4;

	// sanity check
	if ( used != readBufSize ) {
		// if we send back a g_errno then multicast retries forever
		// so just absorb it!
		log("msg4: got corrupted request from hostid %"INT32" "
		    "used=%"INT32" != %"INT32"=readBufSize msg4",
		    slot->m_host->m_hostId,
		    used,
		    readBufSize);
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot ) ;
		//us->sendErrorReply(slot,ECORRUPTDATA);return;}
		return;
	}

	bool skipSyncdb = false;

	// skip syncdb if we are just one host!
	if ( g_hostdb.m_numHosts == 1 ) skipSyncdb = true;

	// if we did not sync our parms up yet with host 0, wait...
	if ( g_hostdb.m_hostId != 0 && ! g_parms.m_inSyncWithHost0 ) {
		// limit logging to once per second
		static int32_t s_lastTime = 0;
		int32_t now = getTimeLocal();
		if ( now - s_lastTime >= 1 ) {
			s_lastTime = now;
			log("msg4: waiting to sync with "
			    "host #0 before accepting data");
		}
		// tell send to try again int16_tly
		g_errno = ETRYAGAIN;
		us->sendErrorReply(slot,g_errno);
		return; 
	}

	// OK, just to get the ball rolling let's delay using/debugging
	// syncdb until after launch in order to move up the launch date.
	// we are going to be running solid states so there should be a lot
	// fewer hardware issues...
	skipSyncdb = true;

	if ( skipSyncdb ) {
		// this returns false with g_errno set on error
	        if ( ! addMetaList ( readBuf , slot ) ) {
		     us->sendErrorReply(slot,g_errno);
		     return; 
		}
		// good to go
		us->sendReply_ass ( NULL , 0 , NULL , 0 , slot ) ;
		return;
	}

	// . add to syncdb tree
	// . a key_t is now before the "used"
	// . this returns false and sets g_errno if we could not add it to 
	//   syncdb OR if there were some msg4 requests we should have got 
	//   before this one!
	// . in the first case it will set g_errno to ETRYAGAIN probably,
	//   but if out of order it will just set g_errno to EOUTOFSYNC i guess
	if ( ! g_syncdb.gotMetaListRequest ( slot ) ) {
		us->sendErrorReply(slot,g_errno);return; }

	// . chalk it up
	// . it may have multiple different rdb items in the list now!
	//rdb->sentReplyAdd ( 0 );

	// . send an empty (non-error) reply as verification
	// . slot should be auto-nuked on transmission/timeout of reply
	// . udpServer should free the readBuf
	us->sendReply_ass ( NULL , 0 , NULL , 0 , slot ) ;
}


// . Syncdb.cpp will call this after it has received checkoff keys from
//   all the alive hosts for this zid/sid
// . returns false and sets g_errno on error, returns true otherwise
bool addMetaList ( char *p , UdpSlot *slot ) {

	if ( g_conf.m_logDebugSpider )
		logf(LOG_DEBUG,"syncdb: calling addMetalist zid=%"UINT64"",
		     *(int64_t *)(p+4));

	// get total buf used
	int32_t used = *(int32_t *)p;
	// the end
	char *pend = p + used;
	// skip the used amount
	p += 4;
	// skip zid
	p += 8;

	Rdb  *rdb       = NULL;
	char  lastRdbId = -1;

	// . this request consists of multiple recs, so add each one
	// . collnum(2bytes)/rdbId(1byte)/recSize(4bytes)/recData/...
 loop:
	// extract collnum, rdbId, recSize
	collnum_t collnum = *(collnum_t *)p; p += sizeof(collnum_t);
	char      rdbId   = *(char      *)p; p += 1;
	int32_t      recSize = *(int32_t      *)p; p += 4;
	// int16_tcut
	//UdpServer *us = &g_udpServer;
	// . get the rdb to which it belongs, use Msg0::getRdb()
	// . do not call this for every rec if we do not have to
	if ( rdbId != lastRdbId ) {
		rdb = getRdbFromId ( (char) rdbId );
		// skip RDBFAKEDB
		//if ( rdbId == RDB_FAKEDB ) {
		//	// do special handler process
		//	processSpecialSignal ( collnum , p );
		//	// skip the fakedb record
		//	p += recSize;
		//	// drop it for now!!
		//	if ( p < pend ) goto loop;
		//	// all done
		//	return true;
		//}
		// an uninitialized secondary rdb? it will have a keysize
		// of 0 if its never been intialized from the repair page.
		// don't core any more, we probably restarted this shard
		// and it needs to wait for host #0 to syncs its
		// g_conf.m_repairingEnabled to '1' so it can start its
		// Repair.cpp repairWrapper() loop and init the secondary
		// rdbs so "rdb" here won't be NULL any more.
		if ( rdb && rdb->m_ks <= 0 ) {
			time_t currentTime = getTime();
			static time_t s_lastTime = 0;
			if ( currentTime > s_lastTime + 10 ) {
				s_lastTime = currentTime;
				log("msg4: oops. got an rdbId key for a "
				    "secondary "
				    "rdb and not in repair mode. waiting to "
				    "be in repair mode.");
				g_errno = ETRYAGAIN;
				return false;
				//char *xx=NULL;*xx=0;
			}
		}
		if ( ! rdb ) {
			if ( slot ) 
				log("msg4: rdbId of %"INT32" unrecognized "
				    "from hostip=%s. "
				    "dropping WHOLE request", (int32_t)rdbId,
				    iptoa(slot->m_ip));
			else
				log("msg4: rdbId of %"INT32" unrecognized. "
				    "dropping WHOLE request", (int32_t)rdbId);
			g_errno = ETRYAGAIN;
			return false;
			// drop it for now!!
			//if ( p < pend ) goto loop;
			// all done
			//return true;
			char *xx=NULL;*xx=0;
			// silently drop it, the WHOLE thing, it seems 
			// corrupted!!!
			return true;
			//g_errno = EBADENGINEER;
			//return false;
		}
		//if ( ! rdb ) return false;
	}

	// . if already in addList and we are quickpoll interruptint, try again
	// . happens if our niceness gets converted to 0
	if ( rdb->m_inAddList ) {
		g_errno = ETRYAGAIN;
		return false;
	}

	// sanity check
	if ( p + recSize > pend ) { g_errno = ECORRUPTDATA; return false; }
	// reset g_errno
	g_errno = 0;
	// . make a list from this data
	// . skip over the first 4 bytes which is the rdbId
	// . TODO: embed the rdbId in the msgtype or something...
	RdbList list;
	// sanity check
	if ( rdb->getKeySize() == 0 ) {
		log("seems like a stray /e/repair-addsinprogress.dat file "
		    "rdbId=%"INT32". waiting to be in repair mode."
		    ,(int32_t)rdbId);
		    //not in repair mode. dropping.",(int32_t)rdbId);
		g_errno = ETRYAGAIN;
		return false;
		char *xx=NULL;*xx=0;
		// drop it for now!!
		p += recSize;
		if ( p < pend ) goto loop;
		// all done
		return true;
	}
	// set the list
	list.set ( p                       ,
		   recSize                 ,
		   p                       ,
		   recSize                 ,
		   rdb->getFixedDataSize() ,
		   false                   ,  // ownData?
		   rdb->useHalfKeys()      ,
		   rdb->getKeySize ()      ); 
	// advance over the rec data to point to next entry
	p += recSize;
	// keep track of stats
	rdb->readRequestAdd ( recSize );
	// this returns false and sets g_errno on error
	bool status =rdb->addList(collnum, &list, MAX_NICENESS );

	// bad coll #? ignore it. common when deleting and resetting
	// collections using crawlbot. but there are other recs in this
	// list from different collections, so do not abandon the whole 
	// meta list!! otherwise we lose data!!
	if ( g_errno == ENOCOLLREC && !status ) { g_errno = 0; status = true; }

	// do the next record here if there is one
	if ( status && p < pend ) goto loop;

	// no memory means to try again
	if ( g_errno == ENOMEM ) g_errno = ETRYAGAIN;
	// doing a full rebuid will add collections
	if ( g_errno == ENOCOLLREC  &&
	     g_repairMode > 0       )
	     //g_repair.m_fullRebuild   )
		g_errno = ETRYAGAIN;
	// ignore enocollrec errors since collection can be reset while
	// spiders are on now.
	//if ( g_errno == ENOCOLLREC )
	//	g_errno = 0;
	// are we done
	if ( g_errno ) return false;
	// success
	return true;
}


//
// serialization code
//

// . when we core, save this stuff so we can re-add when we come back up
// . have a sleep wrapper that tries to flush the buffers every 10 seconds
//   or so.
// . returns false on error, true on success
// . does not do any mallocs in case we are OOM and need to save
// . BUG: might be trying to send an old bucket, so scan udp slots too? or
//   keep unsent buckets in the list?
bool saveAddsInProgress ( char *prefix ) {

	if ( g_conf.m_readOnlyMode ) return true;

	// this does not work so skip it for now
	//return true;

	// open the file
	char filename[1024];

	// if saving while in repair mode, that means all of our adds must
	// must associated with the repair. if we send out these add requests
	// when we restart and not in repair mode then we try to add to an
	// rdb2 which has not been initialized and it does not work.
	if ( ! prefix ) prefix = "";
	sprintf ( filename , "%s%saddsinprogress.saving", 
		  g_hostdb.m_dir , prefix );

	int32_t fd = open ( filename, O_RDWR | O_CREAT | O_TRUNC ,
			    getFileCreationFlags() );
			 // S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH );
	if ( fd < 0 ) {
		log ("build: Failed to open %s for writing: %s",
		     filename,strerror(errno));
		return false;
	}

	log(LOG_INFO,"build: Saving %s",filename);

	// the # of host bufs
	write ( fd , (char *)&s_numHostBufs , 4 );
	// serialize each hostbuf
	for ( int32_t i = 0 ; i < s_numHostBufs ; i++ ) {
		// get the size
		int32_t used = 0;
		// if not null, how many bytes are used in it?
		if ( s_hostBufs[i] ) used = *(int32_t *)s_hostBufs[i];
		// size of the buf
		write ( fd , (char *)&used , 4 );
		// skip if none
		if ( ! used ) continue;
		// if only 4 bytes used, that is basically empty, the first
		// 4 bytes is how much of the total buffer is used, including
		// those 4 bytes.
		if ( used == 4 ) continue;
		// test it
		if ( used <= 4 || used > 300000000 ) {  // > 300MB????
			log("msg4: saving addsinprogress. bad bucket "
			    "used size of %"INT32,used);
			continue;
		}
		// the buf itself
		write ( fd , s_hostBufs[i] , used );
	}

	// scan in progress msg4 requests too!
	UdpSlot *slot = g_udpServer.m_head2;
	for ( ; slot ; slot = slot->m_next2 ) {
		// skip if not msg4
		if ( slot->m_msgType != 0x04 ) continue;
		// skip if we did not initiate it
		if ( ! slot->m_callback ) continue;
		// skip if got reply
		if ( slot->m_readBuf ) continue;
		// if not sending something, skip
		if ( ! slot->m_sendBuf ) continue;
		// test it
		int32_t used = *(int32_t *)slot->m_sendBuf;
		if ( used <= 4 || used > 300000000 ) {  // > 300MB????
			log("msg4: saving addsinprogress. bad slot "
			    "used size of %"INT32,used);
			continue;
		}
		if ( used != slot->m_sendBufSize ) {
			log("msg4: saving addsinprogress. bad used size of "
			    "%"INT32" != %"INT32,used,slot->m_sendBufSize);
			continue;
		}
		// write hostid sent to
		write ( fd , &slot->m_hostId , 4 );
		// write that
		write ( fd , &slot->m_sendBufSize , 4 );
		// then the buf data itself
		write ( fd , slot->m_sendBuf , slot->m_sendBufSize );
	}
	

	// MDW: if msg4 was stored in the linked list then caller 
	// never got his callback called, so the spider will redo
	// this url later...

	// . serialize each Msg4 that is waiting in line
	// . need to preserve their list ptrs so to avoid re-adds?
	/*
	Msg4 *msg4 = s_msg4Head;
	while ( msg4 ) {
		msg4->save ( fd );
		// next msg4
		msg4 = msg4->m_next;
	}
	*/

	// all done
	close ( fd );
	// if all was successful, rename the file
	char newFilename[1024];

	// if saving while in repair mode, that means all of our adds must
	// must associated with the repair. if we send out these add requests
	// when we restart and not in repair mode then we try to add to an
	// rdb2 which has not been initialized and it does not work.
	sprintf ( newFilename , "%s%saddsinprogress.dat",
		  g_hostdb.m_dir , prefix );

	::rename ( filename , newFilename );

	log(LOG_INFO,"build: Renamed %s to %s",filename,newFilename);

	return true;
}

/*
bool Msg4::save ( int fd ) {
	int16_t  collLen  = gbstrlen(m_coll);
	// collLen
	write ( fd , &collLen , 2 );
	// coll, as a string in case coll is deleted and another added
	write ( fd , coll , collLen + 1 );
	// offset
	int32_t offset = m_currentPtr - m_metaList;
	// might as well avoid re-adds
	write ( fd , (char *)&offset , 4 );
	// list size (4 bytes)
	write ( fd , (char *)&m_metaListSize , 4 );
	// list data
	write ( fd , m_metaList , m_metaListSize );
	return true;
}
*/


// . returns false on an unrecoverable error, true otherwise
// . sets g_errno on error
bool loadAddsInProgress ( char *prefix ) {

	if ( g_conf.m_readOnlyMode ) return true;

	// open the file
	char filename[1024];

	// . a load when in repair mode means something special
	// . see Repair.cpp's call to loadAddState()
	// . if we saved the add state while in repair mode when we exited
	//   then we need to restore just that
	if ( ! prefix ) prefix = "";
	sprintf ( filename, "%s%saddsinprogress.dat",
		  g_hostdb.m_dir , prefix );

	// if file does not exist, return true, not really an error
        struct stat stats;
        stats.st_size = 0;
	int status = stat ( filename , &stats );
	if ( status != 0 && errno == ENOENT ) return true;

	// get the fileSize into "pend"
	int32_t p    = 0;
	int32_t pend = stats.st_size;

	int32_t fd = open ( filename, O_RDONLY );
	if ( fd < 0 ) {
		log ("build: Failed to open %s for reading: %s",
		     filename,strerror(errno));
		g_errno = errno;
		return false;
	}

	log(LOG_INFO,"build: Loading %"INT32" bytes from %s",pend,filename);

	// . deserialize each hostbuf
	// . the # of host bufs
	int32_t numHostBufs;
	read ( fd , (char *)&numHostBufs , 4 ); 
	p += 4;
	if ( numHostBufs != s_numHostBufs ) {
		g_errno = EBADENGINEER;
		log("build: addsinprogress.dat has wrong number of "
		    "host bufs.");
	}

	// deserialize each hostbuf
	for ( int32_t i = 0 ; i < numHostBufs ; i++ ) {
		// break if nothing left to read
		if ( p >= pend ) break;
		// USED size of the buf
		int32_t used;
		read ( fd , (char *)&used , 4 );
		p += 4;
		// if used is 0, a NULL buffer, try to read the next one
		if ( used == 0 || used == 4 ) { 
			s_hostBufs    [i] = NULL;
			s_hostBufSizes[i] = 0;
			continue;
		}
		if ( used < 4 || used > 300000000 )
			return log("msg4: bad used bytes in bucket 1");
		// malloc the min buf size
		int32_t allocSize = MAXHOSTBUFSIZE;
		if ( allocSize < used ) allocSize = used;
		// alloc the buf space, returns NULL and sets g_errno on error
		char *buf = (char *)mmalloc ( allocSize , "Msg4" );
		if ( ! buf ) return log("build: Could not alloc %"INT32" bytes for "
					"reading %s",allocSize,filename);
		// the buf itself
		int32_t nb = read ( fd , buf , used );
		// sanity
		if ( nb != used ) {
			// reset the buffer usage
			//*(int32_t *)(p-4) = 4;
			*(int32_t *)buf = 4;
			// return false
			return log("build: error reading addsinprogress.dat: "
				   "%s", mstrerror(errno));
		}
		// skip over it
		p += used;
		// sanity check
		if ( *(int32_t *)buf != used ) {
			log("build: file %s is bad.",filename);
			char *xx = NULL; *xx = 0; 
		}
		if ( i >= s_numHostBufs ) {
			mfree ( buf , allocSize ,"hostbuf");
			log("build: skipping host buf #%"INT32,i);
			continue;
		}

		// set the array
		s_hostBufs     [i] = buf;
		s_hostBufSizes [i] = allocSize;
	}

	// scan in progress msg4 requests too that we stored in this file too
	for ( ; ; ) {
		// break if nothing left to read
		if ( p >= pend ) break;
		// hostid sent to
		int32_t hostId;
		read ( fd , (char *)&hostId , 4 );
		p += 4;
		// get host
		Host *h = g_hostdb.getHost(hostId);
		// host many bytes
		int32_t numBytes;
		read ( fd , (char *)&numBytes , 4 );
		p += 4;
		if ( numBytes < 4 || numBytes > 300000000 )
			return log("msg4: bad used bytes in slot 1");
		// allocate buffer
		char *buf = (char *)mmalloc ( numBytes , "msg4loadbuf");
		if ( ! buf ) {
			close ( fd );
			return log("build: could not alloc msg4 buf");
		}
		// the buffer
		int32_t nb = read ( fd , buf , numBytes );
		if ( nb != numBytes ) {
			close ( fd );
			return log("build: bad msg4 buf read");
		}
		p += numBytes;
		// must be there
		if ( ! h ) {
			//close (fd);
			log("build: bad msg4 hostid %"INT32" nb=%"INT32,
			    hostId,nb);
			mfree ( buf , numBytes,"hostbuf");
			continue;
		}
		// send it!
		if ( ! g_udpServer.sendRequest ( buf ,
						 numBytes ,
						 0x04     ,   // msgType
						 h->m_ip      ,
						 h->m_port    ,
						 h->m_hostId  ,
						 NULL         ,
						 NULL         , // state data
						 NULL , // callback
						 999999999)){// seconds timeout
			close ( fd );
			// report it
			return log("build: could not resend reload buf: %s",
				   mstrerror(g_errno));
		}
	}


	// MDW: if msg4 was stored in the linked list then caller 
	// never got his callback called, so the spider will redo
	// this url later...

	// . deserialize each Msg4 that is waiting in line
	// . need to preserve their list ptrs so to avoid re-adds?
	// . format:
	//   rdbid(1 byte)
	//   collLen(2 byte)
	//   coll(\0 terminated string)
	//   listOff(4 bytes)
	//   listSize(4 bytes)
	//   list(listSize bytes)
	/*
	while ( p < pend ) {

		// make a new msg4 to hold this
		Msg4 *msg4 ;
		try { msg4 = new (Msg4); }
		catch ( ... ) { 
			// return false and set g_errno on error
			g_errno = ENOMEM;
			return log("build: Msg4 new failed. Could not read in "
				   "addsinprogress.dat.");
		}
		// register with Mem.cpp's table
		mnew ( msg4 , sizeof(Msg4) , "Msg4c");

		char  rdbId    ;
		int16_t collLen  ;
		char  coll[MAX_COLL_LEN+1];
		int32_t  listOff  ;
		int32_t  listSize ;
		bool  err = false;

		// read in rdbid, collLen, coll, listSize, listOffset, listData
		if ( read ( fd, &rdbId   , 1         ) != 1 ) err = true;
		if ( read ( fd, &collLen , 2         ) != 2 ) err = true;
		if ( read ( fd,  coll    , collLen+1 ) != collLen+1) err =true;
		if ( read ( fd, &listOff , 4         ) != 4 ) err = true;
		if ( read ( fd, &listSize, 4         ) != 4 ) err = true;
		// advance read head
		p += 1 + 2 + collLen + 1 + 4 + 4;
		// make a buf for the list
		char *listBuf = (char *)mmalloc ( listSize , "Msg4d" );
		if ( ! listBuf )
		       return log("build: Failed to load addsinprogress.dat.");

		if ( read ( fd , listBuf , listSize ) != listSize ) err = true;
		p += listSize;

		// handle read errors, return false with g_errno set
		if ( err ) {
			mfree   ( listBuf , listSize , "Msg4d" );
			mdelete ( msg4 , sizeof(Msg4), "Msg4" );
			delete  ( msg4 );
			g_errno = ECORRUPTDATA;
			return log("build: addsinprogress.dat: %s",
				   mstrerror(g_errno));
		}

		// no callback, so it will be deleted when fully added
		msg4->m_callback = NULL;
		msg4->m_state    = NULL;

		// use our own internal list
		msg4->m_list = &msg4->m_myList;

		Rdb *rdb = getRdbFromId ( rdbId );

		// if had a bad rdbId, try reading the next queue
		if ( ! rdb ) {
			log("build: had bogus rdbId of %"INT32".",(int32_t)rdbId);
			mfree   ( listBuf , listSize , "Msg4d" );
			mdelete ( msg4 , sizeof(Msg4), "Msg4" );
			delete  ( msg4 );
			return log("build: addsinprogress.dat: %s",
				   mstrerror(g_errno));
		}
			

		// otherwise, set the list to wait in line, our linked list
		msg4->m_list->set ( listBuf                 ,
				    listSize                ,
				    listBuf                 ,
				    listSize                ,
				    rdb->getFixedDataSize() ,
				    true                    , // ownData?
				    rdb->useHalfKeys()      ,
				    rdb->getKeySize ()      );
		// force set the current rec ptr
		msg4->m_list->m_listPtr = listBuf + listOff;

		// init for linked list
		msg4->m_next = NULL;
		// store in linked list
		if ( ! s_msg4Tail ) {
			// hey, we are the first in the linked list
			s_msg4Head = msg4;
			s_msg4Tail = msg4;
			continue;
		}
		// otherwise, we are not the first
		s_msg4Tail->m_next = msg4;
		s_msg4Tail         = msg4;
	}
	*/
	// all done
	close ( fd );
	return true;
}


//
// right now the FAKEDB record is a signal to remove the spider lock
// from the lock table because we are done spidering it.
//
/*
void processSpecialSignal ( collnum_t collnum , char *p ) {

	key_t *fake = (key_t *)p;

	// use a uh48 of 0 to signify an unlock operation
	//g_titledb.getUrlHash48 ( (key_t *)key ) == 0LL ) {
	// must be 96 bits
	//if ( m_ks != 12 ) { char *xx=NULL;*xx=0; }
	// get docid that was locked
	//int64_t d = g_titledb.getDocId ( (key_t *)key);
	int64_t d = fake->n0;
	// . make it the first probable, that is the lock key
	// . we do that so if we are locking a new url that
	//   is not yet indexed, its probable docid may collide
	//   and be incremented, so we do not know what its
	//   actual docid will end up being...
	int64_t lockKey = g_titledb.getFirstProbableDocId(d);
	// log debug msg
	if ( g_conf.m_logDebugSpider)
		// log debug
		logf(LOG_DEBUG,"msg4: got FAKE titledb "
		     "key for lockkey=%"UINT64" - removing spider lock",
		     lockKey);
	// int16_tcut
	HashTableX *ht = &g_spiderLoop.m_lockTable;
	UrlLock *lock = (UrlLock *)ht->getValue ( &lockKey );
	time_t nowGlobal = getTimeGlobal();

	if ( g_conf.m_logDebugSpiderFlow )
		logf(LOG_DEBUG,"spflow: scheduled lock removal in 5 secs for "
		     "docid=lockkey=%"UINT64"",  lockKey);

	// test it
	//if ( m_nowGlobal == 0 && lock )
	//	m_nowGlobal = getTimeGlobal();
	// we do it this way rather than remove it ourselves
	// because a lock request for this guy
	// might be currently outstanding, and it will end up
	// being granted the lock even though we have by now removed
	// it from doledb, because it read doledb before we removed 
	// it! so wait 5 seconds for the doledb negative key to 
	// be absorbed to prevent a url we just spidered from being
	// re-spidered right away because of this sync issue.
	if ( lock ) lock->m_expires = nowGlobal + 5;
	// bitch if not in there
	if (!lock&&g_conf.m_logDebugSpider)//ht->isInTable(&lockKey)) 
		logf(LOG_DEBUG,"spider: rdb: lockkey %"UINT64" "
		     "was not in lock table",lockKey);
	// now unlock on that
	//g_spiderLoop.m_lockTable.removeKey(&lockKey);
	// do not actually add this fake key to titledb!
	//return true;
}
*/

