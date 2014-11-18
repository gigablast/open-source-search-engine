// Matt Wells, copyright Feb 2001

// . add/delete an RdbList to/from a group
// . RdbList can be hetergoneous: not all it's records may belong to same group
// . we only send to the next group when the group before is done sending
// . we block/loop indefinitely if a host in the group cannot add the msg

// . TODO: add seal support
// . TODO: re-add incremental synchronization later
// . TODO: store rdbId in msgType so we can add to rdb faster w/ copying
// . TODO: send all at once or at least X at a time, don't wait for each one
//         before sending the next

#ifndef _MSG1_H_
#define _MSG1_H_

#include "Rdb.h"
#include "UdpServer.h"
#include "Multicast.h"
#include "Msg0.h"                         // for getRdb(char rdbId)
#include "Titledb.h"
//#include "Tfndb.h"
#include "Indexdb.h"
//#include "Spider.h"
#include "Clusterdb.h"
#include "Linkdb.h"
#include "Datedb.h"
#include "Tagdb.h"

#define MSG1_BUF_SIZE 64

// . used to add rec to tfndb after adding a rec to titledb or spiderdb
// . called in Msg1.cpp for when injecting a title rec
// . called in Sync.cpp for when adding a new title rec or spider rec that
//   the remote host has but the local does not
//bool updateTfndb(char *coll , RdbList *list , bool isTitledb, int32_t niceness);

class Msg1 {

 public:

	// . this should only be called once
	// . should also register our get record handlers with the udpServer
	bool registerHandler ( );

	// . returns false if blocked, true otherwise
	// . sets errno on error
	// . if "groupId" is -1 we determine the groupId for each record
	//   by ANDing the high int32_t of it's key with the groupMask in confdb
	// . spiderdb should be the only db that specifies it's own groupId
	//   since it wants to partition by the low int32_t, not the high int32_t
	//   since it's high int32_t is the spiderTime and low int32_t is the top
	//   32 bits of the docId
	// . i added "forceLocal" cuz we had a corrupt key in spiderdb which
	//   made it belong to a foreign group, and when we tried to delete it
	//   the delete key went elsewhere and we couldn't delete it!
	// . deleteRecs will cause Rdb::deleteRec() to be called. if the rdb
	//   does not support delbits in the keys use this.
	// . when deleteRecs is true, the recs in the list are really just keys
	bool addList ( RdbList  *list  ,
		       char      rdbId ,
		       collnum_t collnum, // char     *coll  ,
		       void     *state ,
		       void    (*callback)(void *state) ,
		       bool      forceLocal    ,
		       int32_t      niceness      ,
		       bool      injecting    = false ,
		       bool      waitForReply = true  ,
		       bool     *inTransit    = NULL  );

	bool addRecord ( char *rec , 
			 int32_t recSize , 
			 char          rdbId             ,
			 collnum_t collnum ,
			 void         *state             ,
			 void (* callback)(void *state)  ,
			 int32_t          niceness          ) ;

	// private:

	// keep this public cuz it's called by a C wrapper in Msg1.cpp
	bool sendSomeOfList ();

	// send m_list in entirety to a group
	bool sendData ( uint32_t groupId , char *list , int32_t listSize ) ;

	void      (*m_callback ) ( void *state );
	void       *m_state;

	// list to be added
	RdbList    *m_list;

	// for Msg1::addRecord:
	RdbList m_tmpList;

	// rdb id to add to ( see Msg0::getRdb(char rdbId) )
	char        m_rdbId;
	//char       *m_coll;
	collnum_t m_collnum;

	// groupId to send to (may be -1 if it's up to us to decide)
	uint32_t m_groupId;

	// . use this for sending to all hosts in a group
	// . will block indefinitely if could not send to one host in the
	//   group for some reason
	Multicast   m_mcast;

	// . a flag, if this is true always keep it local to the groupId
	// . used to combat data corruption really by SpiderLoop
	bool        m_forceLocal;

	int32_t        m_niceness;
	
	bool        m_injecting;

	// don't hold up the pipeline
	bool        m_waitForReply;

	// when adding a list without making the caller wait for the reply
	// we must keep the list in our possession so caller does not free it
	RdbList     m_ourList;

	char m_buf [ MSG1_BUF_SIZE ];
};


/*
inline uint32_t getGroupId ( char rdbId , char *key ) {
	// try to put those most popular ones first for speed
	if      ( rdbId == RDB_INDEXDB )
		return g_indexdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB_DATEDB )
		return g_datedb.getGroupIdFromKey((key128_t *)key);
	else if ( rdbId == RDB_TITLEDB)
		return g_titledb.getGroupIdFromKey((key_t *)key);
	//else if ( rdbId == RDB_CHECKSUMDB)
	//	return g_checksumdb.getGroupId ( key );
	else if ( rdbId == RDB_SPIDERDB )
		return g_spiderdb.getGroupId ( (key_t *)key );
	else if ( rdbId == RDB_TFNDB )
		return g_tfndb.getGroupId    ( (key_t *)key );
	else if ( rdbId == RDB_CLUSTERDB )
		return g_clusterdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB_LINKDB )
		return g_linkdb.getGroupId((key128_t *)key);
	else if ( rdbId == RDB_TAGDB )
		return g_tagdb.getGroupId((key_t *)key);
	else if ( rdbId == RDB_CATDB )
		return g_tagdb.getGroupId((key_t *)key);

	else if ( rdbId == RDB2_INDEXDB2 )
		return g_indexdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB2_DATEDB2 )
		return g_datedb.getGroupIdFromKey((key128_t *)key);
	else if ( rdbId == RDB2_TITLEDB2)
		return g_titledb.getGroupIdFromKey((key_t *)key);
	//else if ( rdbId == RDB2_CHECKSUMDB2)
	//	return g_checksumdb.getGroupId ( key );
	else if ( rdbId == RDB2_SPIDERDB2 )
		return g_spiderdb.getGroupId ( (key_t *)key );
	else if ( rdbId == RDB2_TFNDB2 )
		return g_tfndb.getGroupId    ( (key_t *)key );
	else if ( rdbId == RDB2_CLUSTERDB2 )
		return g_clusterdb.getGroupIdFromKey((key_t *)key);
	else if ( rdbId == RDB2_LINKDB2 )
		return g_linkdb.getGroupId((key128_t *)key);
	else if ( rdbId == RDB2_SITEDB2 )
		return g_tagdb.getGroupId((key_t *)key);
	else if ( rdbId == RDB2_CATDB2 )
		return g_tagdb.getGroupId((key_t *)key);
	// core -- must be provided
	char *xx = NULL; *xx = 0;
	//groupId=key.n1 & g_hostdb.m_groupMask;
	return (((key_t *)key)->n1) & g_hostdb.m_groupMask;
}

//if split is true, it can still be overrriden by the parm in g_conf
//if false, we don't split index and date lists, other dbs are unaffected
extern uint32_t getGroupId ( char rdbId , char *key, bool split ) ;
*/

#endif
