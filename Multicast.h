// Matt Wells, Copyright Jun 2001


// . TODO: if i'm sending to 1 host in a specified group how do you know
//         to switch adn try anoher host in the group?  What if target host 
//         has to access disk ,etc...???

// . this class is used for performing multicasting through a UdpServer
// . the Multicast class is used to govern a multicast
// . each UdpSlot in the Multicast class corresponds to an ongoing transaction
// . takes up 24*4 = 96 bytes
// . TODO: have broadcast option for hosts in same network
// . TODO: watch out for director splitting the groups - it may
//         change the groups we select
// . TODO: set individual host timeouts yourself based on their std.dev pings

#ifndef _MULTICAST_H_
#define _MULTICAST_H_

#include "Hostdb.h"  // getGroup(), getTimes(), stampHost()
#include "UdpServer.h"        // sendRequest()
#include "Loop.h"         // registerSleepCallback()
//#include "Msg34.h"
#include "Threads.h" // MAX_NICENESS

#define MAX_HOSTS_PER_GROUP 10

class Multicast {

 public:

	Multicast  ( ) ;
	~Multicast ( ) ;
	void constructor ( );
	void destructor  ( );

	// . returns false and sets errno on error
	// . returns true on success -- your callback will be called
	// . check errno when your callback is called
	// . we timeout the whole shebang after "totalTimeout" seconds
	// . if "sendToWholeGroup" is true then this sends your msg to all 
	//   hosts in the group with id "groupId"
	// . if "sendToWholeGroup" is true we don't call callback until we 
	//   got a non-error reply from each host in the group
	// . it will keep re-sending errored replies every 1 second, forever
	// . this is useful for safe-sending adds to databases
	// . spider will reach max spiders and be stuck until
	// . if "sendToWholeGroup" is false we try to get a reply as fast
	//   as possible from any of the hosts in the group "groupId"
	// . callback will be called when a good reply is gotten or when all
	//   hosts in the group have failed to give us a good reply
	// . generally, for querying for data make "sendToWholeGroup" false
	// . generally, when adding      data make "sendToWholeGroup" true
	// . "totalTimeout" is for the whole multicast
	// . "key" is used to pick a host in the group if sendToWholeGroup
	//   is false
	// . Msg40 uses largest termId in winning group as the key to ensure
	//   that queries with the same largest termId go to the same machine
	// . likewise, Msg37 uses it to get the term frequency consistently
	//   so it doesn't jitter
	// . if you pass in a "replyBuf" we'll store the reply in there
	// . if "doLoadBalancing" is true and sendToWholeGroup is false then
	//   we send a Msg34 request to all hosts in group "groupId" and ask
	//   them how much disk load thay have then we send to the best one.
	// . if we sent a Msg34 to a host within the last couple of 
	//   milliseconds or so, then Msg34 should use results from last time.
	//   When we send a Msg0 or Msg20 request we know the approximate
	//   amount that will be read from disk so we can add this to the
	//   Load of the handling host by calling Msg34::addLoad() so if we
	//   call Msg34::getLoad() again before that Msg0/Msg20 request was
	//   acknowledge, the added load will be included.
	bool send ( char       *msg             ,
		    long        msgSize         ,
		    uint8_t     msgType         ,
		    // does this Multicast own this "msg"? if so, it will
		    // free it when done with it.
		    bool        ownMsg          , 
		    // send this request to a host or hosts who have
		    // m_groupId equal to this
		    unsigned long groupId       , 
		    // should the request be sent to all hosts in the group
		    // "groupId", or just one host. Msg1 adds data to all 
		    /// hosts in the group so it sets this to true.
		    bool        sendToWholeGroup, 
		    // if "key" is not 0 then it is used to select
		    // a host in the group "groupId" to send to.
		    long        key             ,
		    void       *state           , // callback state
		    void       *state2          , // callback state
		    void      (*callback)(void *state,void *state2),
		    long        totalTimeout    , // usually 60 seconds 
		    long        niceness        = MAX_NICENESS ,
		    bool        realTimeUdp     = false ,
		    long        firstHostId     = -1 ,// first host to try
		    char       *replyBuf        = NULL ,
		    long        replyBufMaxSize = 0 ,
		    bool        freeReplyBuf    = true ,
		    bool        doDiskLoadBalancing = false ,
		    long        maxCacheAge     = -1   , // no age limit
		    key_t       cacheKey        =  0   ,
		    char        rdbId           =  0   , // bogus rdbId
		    long        minRecSizes     = -1   ,// unknown read size
		    bool        sendToSelf      = true ,// if we should.
		    bool        retryForever    = true ,// for pick & send
		    class Hostdb *hostdb        = NULL ,
		    long        redirectTimeout = -1 ,
		    class Host *firstProxyHost  = NULL );

	// . get the reply from your NON groupSend
	// . if *freeReply is true then you are responsible for freeing this 
	//   reply now, otherwise, don't free it
	char *getBestReply ( long *replySize , 
			     long *replyMaxSize, 
			     bool *freeReply ,
			     bool  steal = false);

	// free all non-NULL ptrs in all UdpSlots, and free m_msg
	void reset ( ) ;

	// private:

	void destroySlotsInProgress ( UdpSlot *slot );

	// keep these public so C wrapper can call them
	bool sendToHostLoop ( long key, long hostNumToTry, long firstHostId );
	bool sendToHost    ( long i ); 
	long pickBestHost2 ( unsigned long key , long hostNumToTry ,
			     bool preferLocal );
	long pickBestHost  ( unsigned long key , long hostNumToTry ,
			     bool preferLocal );
	long pickRandomHost( ) ;
	void gotReply1     ( UdpSlot *slot ) ;
	void closeUpShop   ( UdpSlot *slot ) ;

	void sendToGroup   ( ) ;
	void gotReply2     ( UdpSlot *slot ) ;

	// . stuff set directly by send() parameters
	char       *m_msg;
	long        m_msgSize;
	uint8_t     m_msgType;
	bool        m_ownMsg;
	long        m_numGroups;
	unsigned long m_groupId;
	bool        m_sendToWholeGroup;
	void       *m_state;
	void       *m_state2;
	void       (* m_callback)( void *state , void *state2 );
	long       m_timeoutPerHost; // in seconds
	long       m_totalTimeout;   // in seconds

	class UdpSlot *m_slot;

	// . m_slots[] is our list of concurrent transactions
	// . we delete all the slots only after cast is done
	long        m_startTime;   // seconds since the epoch
	// so Msg3a can time response
	long long   m_startTimeMS;

	// # of replies we've received
	long        m_numReplies;

	// . the group we're sending to or picking from
	// . up to MAX_HOSTS_PER_GROUP hosts
	// . m_retired, m_slots, m_errnos correspond with these 1-1
	Host       *m_hostPtrs[16];
	long        m_numHosts;

	class Hostdb *m_hostdb;

	// . hostIds that we've tried to send to but failed
	// . pickBestHost() skips over these hostIds
	char        m_retired    [MAX_HOSTS_PER_GROUP];

	// we can have up to 8 hosts per group
	UdpSlot    *m_slots      [MAX_HOSTS_PER_GROUP]; 
	// did we have an errno with this slot?
	long        m_errnos     [MAX_HOSTS_PER_GROUP]; 
	// transaction in progress?
	char        m_inProgress [MAX_HOSTS_PER_GROUP]; 
	long long   m_launchTime [MAX_HOSTS_PER_GROUP];

	// steal this from the slot(s) we get
	char       *m_readBuf;
	long        m_readBufSize;
	long        m_readBufMaxSize;

	// if caller passes in a reply buf then we reference it here
	char       *m_replyBuf;
	long        m_replyBufSize;
	long        m_replyBufMaxSize;

	// we own it until caller calls getBestReply()
	bool        m_ownReadBuf;
	// are we registered for a callback every 1 second
	bool        m_registeredSleep;
	bool        m_registeredSleep2;

	long        m_niceness;
	bool        m_realtime;

	// . last sending of the request to ONE host in a group (pick & send)
	// . in milliseconds
	long long   m_lastLaunch;
	Host       *m_lastLaunchHost;
	// how many launched requests are current outstanding
	long        m_numLaunched;

	// only free m_reply if this is true
	bool        m_freeReadBuf;

	long        m_key;

	// Msg34 stuff -- for disk load balancing
	//Msg34 m_msg34;
	//bool  m_doDiskLoadBalancing;
	key_t m_cacheKey           ;
	long  m_maxCacheAge        ;
	char  m_rdbId              ;
	long  m_minRecSizes        ;

	// Msg1 might be able to add data to our tree to save a net trans.
	bool        m_sendToSelf;

	bool        m_retryForever;
	long        m_retryCount;

	char        m_sentToTwin;
	bool        m_callbackCalled;

	long        m_redirectTimeout;
	char        m_inUse;

	// for linked list of available Multicasts in Msg4.cpp
	class Multicast *m_next;

	// host we got reply from. used by Msg3a for timing.
	Host      *m_replyingHost;
	// when the request was launched to the m_replyingHost
	long long  m_replyLaunchTime;

	// hacky crunk use by seo pipeline in xmldoc.cpp
	//void *m_hackxd;
	//void *m_hackHost;
	//void *m_hackQPtr;
	//long  m_hackPtrCursor;
	//char  m_hackIsMsg99ReplyPtr;
};

#endif
