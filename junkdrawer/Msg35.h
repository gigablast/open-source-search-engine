#ifndef _MSG35_H_
#define _MSG35_H_

#include "UdpSlot.h"

// . in order to write to disk a gb process needs to have a token
// . in this way we only allow one host per redundancy group to write to
//   disk at any one time, so the other(s) are available to perform quick
//   disk reads for queries or spider requests
// . the first host of each group is always the permanent token manager
// . if he goes down then you just have to wait for him to come back up
//   before you can get the token
// . if any host in the group goes down, then the manager will not pass
//   the token until that host comes back up
// . clients periodically sync with the manager host to make sure they
//   have the same callback token-request info (sync about every 30 secs).
//   that way, should anyone go down, the sync() will fix things

// each client creates one of these Wait classes for every disk dump or merge
// that needs to be done
class ClientWait {
 public:
	int32_t  m_timestamp;
	char  m_priority;
	char  m_buf[12];
	bool  m_isEmpty;
	void ( *m_callback) ( void *state );
	void *m_state;
};

// . the token manager as one of these classes for every token request it reads
// . these should be one-to-one with all the ClientWaits in a group
class ServerWait {
 public:
	int32_t  m_timestamp ;
	char  m_priority  ;
	char  m_buf[12];
	bool  m_isEmpty;
	int32_t  m_hostId;
	int32_t  m_clientSlot;
};

class Msg35 {

 public:

	bool registerHandler ( ) ;
	Msg35 () ;
	void reset () ;

	// . only calls callback after we get the token
	// . if host that has it now goes down, then the next host in
	//   line should get it automatically
	bool getToken (void *state, 
		       void (*callback )(void *state ), 
		       char priority);


	bool callCallback ( int32_t n );
	void gotReply ( UdpSlot *slot ) ;
	Host *getTokenManager ( ) ;
	Host **getTokenGroup( int32_t *numHosts ) ;
	void releaseToken ( ) ;
	void gotReleaseTokenReply ( ) ;
	void removeServerWait ( int32_t i ) ;
	int32_t addServerWait ( int32_t hostId , char priority , char clientNum ,
			     int32_t timestamp ) ;
	void handleRequest ( UdpSlot *slot );
	void giveToken ( ) ;
	void sync ( ) ;


	// . this is somewhat unrelated to the token stuff, but Msg0 will
	//   call this to get the load of each host before deciding which host
	//   to read from
	// . use async handlers as well
	// . use Msg34 class
	// . if we send many requests to the same host based on one call
	//   to getLoad() we need to inc the load for that host
	// . getLoad() should set the loads in g_hostdb.m_hosts... well
	//   just the number of pending disk reads, then we can keep a separate
	//   vector that we inc for each call to a host for that round so
	//   if all hosts have same load we don't send all requests to one host
	// . besides # of pending disk threads, store if he's writing to disk
	//   and assume that he still is 5 seconds or so after so we don't
	//   have to keep asking for his load. that will save us a bit of time
	//   since then we'll probably only have one host left in the group
	//   so we don't even have to make any decision.
	//bool getLoad ( uint32_t groupId ,
	//               void *state, (*callback )(void *state ));

	// private:

	// . this Msg35 class should be declared static (only one instance)
	// . m_callbacks[m_tokeni] is the callback we called to give the token
	// . use -1 for none
	int32_t m_clientTokeni;

	// we may try to get the token multiple times for multiple writes so 
	// we need to remember the callbacks and states to call when we
	// do get the token
	ClientWait m_clientWaits[64];
	int32_t  m_topUsedClient;
	char  m_syncBuf [ 1 + 4 + 4 + 64*(4+1+1) ];

	// . the managing host needs to store requests in a queue
	// . satisfy higher priority requests (dumps) before lower (merges)
	ServerWait m_serverWaits[512];
	int32_t  m_topUsedServer;

	// . the managing host needs to know who currently has the token
	// . use -1 if nobody has it
	// . if that host goes down then pass token to someone else
	int32_t m_serverTokeni;

	// who in our group has sent us a SYNC
	bool m_flags [ 16 ];
	// when a sync has been received from all hosts in our group this is
	// then set to tru
	bool m_allReceived;
	// we enter discrepancy mode when a host sends us a SYNC saying that
	// he does not have the token when we believe he does. If his second
	// SYNC says he STILL does not have it then we believe him and 
	// unassign the token. m_discrepancyHid is the hostId of the host
	// claiming the discrepancy.
	int32_t m_discrepancyHid;
};

extern class Msg35 g_msg35;

#endif
