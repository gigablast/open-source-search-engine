// Copyright Matt Wells, Apr 2009


// . for these descriptions, assume "you" are a host in the cluster

// ** STARTUP **
// . on startup, if you have your  cleanshutdown.dat file then set
//   your Host::m_cleanShutdown to true
// . if you had no cleanshutdown.dat file, reset your syncdb completely
//   because nobody should sync from you at all!
// . on startup, if you have your insync.dat file, set 
//   your Host::m_inSync to true
// . on startup delete your cleanshutdown.dat file
// . on startup delete you insync.dat file
// . on exit touch cleanshutdown.dat
// . if you are shutdown in a shutdown request broadcast to all in the cluster
//   then touch your insync.dat file iff your Host::m_inSync is true!
// . in PingServer.cpp send your m_cleanShutdown and m_inSync bits in your 
//   ping requests/replies so everyone knows your state
// . everyone should default everyone else to false for those bits
// . if a msg request is sent to a host that knows it is out of sync or
//   did not have a clean shutdown, then it should send back an ENOSYNC error
//   and multicast will re-route that to a twin. if all twins return ENOSYNC
//   then the multicast just fails with ENOSYNC!!
// . Multicast should not send requests to such hosts, the same way it avoids
//   dead hosts now.
// . if a host goes dead on you, sets its Host::m_inSync to false
// . display a red "X" in status column of hosts that did not shutdown cleanly
// . display a "Z" in the status column of out of sync hosts


// ** SYNCDB **
// . store all msg4 requests you receive into syncdb
// . every msg4 request has a unique, always increasing id, the "zid"
// . use the current timestamp in milliseconds as the zid (add a little
//   to it to make sure they only increase)
// . ensure your clock is sync'ed with a host in group #0's clock, and ensure
//   -they- are sync'ed with the official time server before doing any msg4s!
// . core dump if CLOCK not in sync and msg4 is called!
// . spiders are not allowed to start until our CLOCK is in sync because spider
//   uses clock to determine what to spider.
// . sanity check zids to make sure they only strictly increase
// . every host in your group will add a "checkoff" record to your syncdb
//   telling you what zids they have received.
// . once you have received all checkoff requests for a zid you can permanently
//   delete that msg4 request from your syncdb. all your twins have it.



// ** SYNC LOOP **

// . tries to update the syncdb so that we can add the msg4 request lists.
//   in order to add a list we must have received SUCCESSFUL checkoff replies 
//   from all *alive* twins. then we can add it. we remove the "need to add
//   meta list" key from syncdb once we add it, to avoid re-adds.

// . keep a m_startKey that points to the first key in syncdb that is not
//   referencing a dead twin! that will save A LOT OF TIME!!!

// . when scanning your keys in syncdb, if you find an incoming checkoff 
//   request that is 60+ seconds old and you have not received the 
//   corresponding msg4 request yourself yet, OR, you got a msg4 request zid 
//   from the same sid that occurs after that zid, then send a msg0 request 
//   for the msg4 request from that twin's syncdb. and set your Host::m_inSync
//   to false. assume the zid is the timestamp as far as computing the 60 
//   second thing. add the msg0 list you get back to syncdb.

// . only actually add the msg4 request list after you have received 
//   replies for all your checkoff requests from all your *alive* twins. so
//   msg4::handleRequest() should just add everything to syncdb... that
//   prevents us from adding the list out of order!

// . launch those requests to all twins at once using Multicast. send out
//   checkoff requests using msg1 for them to add to their syncdbs! 
//   Multicast will skip dead guys. if there was any other error Multicast
//   will timeout or whatever and should set Multicast::m_errno! verify!!

// . if you receive a checkoff request from a host for zid #X but you
//   are missing its checkoff request for zid #X-a then send back an 
//   ENOSYNC reply so it does not add the list yet!

// . if you complete a sync loop w/o finding such old checkoffs, then set
//   your Host::m_inSync to true... UNLESS the MAJORITY of hosts in the
//   network are DEAD as seen by you. (use Hostdb::m_numDeadHosts)

// . set your Host::m_inSync to false if the MAJORITY of hosts in your
//   network are seen as "DEAD" by you. (use Hostdb::m_numDeadHosts)
//   and only if you are not "shutting down!"


// . round robin over the *alive* twins to balance the load
// . when you get the msg4 request, then you can call handleRequest4()
//   on it which will add stuff to the syncdb tree...
// . PROBLEM: how can we be sure that we are adding the zids in order? well
//   every twin must get a reply to its checkoff request for moving on to
//   the next checkoff request for a successive zid. it can also have multiple
//   zids in one checkoff request, which is just a msg1 add request with a list
//   of keys!



// . PROBLEM: let's say both eth0 and eth1 ports go down on you.
//   you will still think you are in sync! i would say if the MAJORITY of
//   hosts as seen by you are DEAD, then set your Host::m_inSync bit to 
//   false and do not allow it to be set to true until the MAJORITY of hosts
//   are up again.

// . PROBLEM: the network is divided into two parts that can not communicate
//   with each other. both pieces are then considered out of sync!! well
//   when the network is re-enabled they will quickly realize they are all
//   out of sync and set their Host::m_inSync values to false.







// . TODO: spider urls that a dead host should be spidering

// . TODO: use better routing when a host is dead. seems like right now we
//   just reroute/bias towards its neighbor. i.e. Msg20, Msg39, etc.


// . keep all spares running at all times.
// . a spare should self-diagnose itself and generate then store its 
//   performance stats in performance.txt. it should certify itself! include 
//   its stats in its ping. but need to merge fff into gbmaster before doing 
//   this. 
// . use Msg55 to sync a host whose twins are actively spidering. copies
//   the rdb files over. suspends all merging operations on files.
// . get rid of gb replacehost ("replacehost=1&rhost=%"INT32"&rspare=%"INT32"")
//   and have a "replace dead hosts" blue link in master controls.
//   and have an 'automatically replace dead hosts' link in master controls.



#ifndef _SYNCDB_H_
#define _SYNCDB_H_

#include "Rdb.h"
#include "Msg0.h"

#define MAX_TO_ADD        500
#define MAX_CHECKOFF_KEYS 500

class Syncdb {

  public:

	// set up our private rdb
	Rdb *getRdb  ( ) { return &m_rdb; };

	bool gotMetaListRequest ( class UdpSlot *slot ) ;
	bool gotMetaListRequest ( char *req , int32_t reqSize , uint32_t sid );

	key128_t makeKey ( char     a      ,
			   char     b      ,
			   char     c      ,
			   char     d      ,
			   uint32_t tid    ,
			   uint32_t sid    ,
			   uint64_t zid    ,
			   char     delBit ) ;

	uint64_t getZid ( key128_t *k ) { return k->n0 & 0xfffffffffffffffeLL;}
	uint32_t getSid ( key128_t *k ) { return k->n1 & 0x00ffffff;}
	uint32_t getTid ( key128_t *k ) { return (k->n1>>32) & 0x00ffffff;}


	void loop1();
	bool sentAllCheckoffRequests ( uint32_t sid , uint64_t zid ) ;
	bool loop2 ( ) ;
	bool gotList ( ) ;
	void loop3 ( ) ;
	bool canDeleteMetaList ( uint32_t sid , uint64_t zid ) ;
	bool loop4 ( ) ;
	bool gotList4 ( ) ;
	void loop5 ( ) ;
	bool addedList5 ( ) ;
	void bigLoop ( ) ;
	void loopReset() ;
	void reset() ; 
	static bool registerHandlers ( ) ;
	bool init ( ) ;
	bool save ( ) ;
	bool verify ( char *coll ) ;
	bool syncHost ( int32_t syncHostId ) ;
	void rcpFiles ( ) ;
	void syncStart_r ( bool amThread ) ;
	void syncDone ( ) ;

	bool addColl ( char *coll, bool doVerify = false );

	//  private:

	// this rdb holds urls waiting to be spidered or being spidered
	Rdb m_rdb;

	RdbTree  m_qt;
	RdbMem   m_stack;

	uint32_t m_requestedSid;

	bool m_doRcp;
	bool m_rcpStarted;

	bool m_calledLoop1;
	bool m_calledLoop2;
	bool m_calledLoop3;
	bool m_calledLoop4;
	bool m_calledLoop5;

	// bookmarks
	key128_t m_syncKey;
	key128_t m_nextk;
	bool     m_outstanding;
	int32_t     m_addCount;

	key128_t m_addMe[MAX_TO_ADD];
	int32_t     m_ia;
	int32_t     m_na;
	Msg5     m_msg5;
	RdbList  m_list;

	// for syncing
	Msg0     m_msg0;


	// checkoff requests
	key128_t  m_keys [ MAX_CHECKOFF_KEYS ];
	int32_t      m_nk;
};

extern class Syncdb g_syncdb;

#endif
