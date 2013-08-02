// Copyright Matt Wells Nov 2000

// TODO: use temporary bufs for each UdpSlot to avoid mallocs for reads
// TODO: eliminate m_gotMsgsToSend, m_gotAcksToSend in lieau of Loop.h methods

// . A reliable udp/datagram server
// . non-blocking, no threads
// . works on I/O interrupts by registering callbacks with the Loop class
// . great for broadcasting to many non-local IPs
// . great for communicating with thousands of random machines to avoid
//   the connect/close overhead associated with TCP
// . the UdpSlot holds the details for one transaction
// . the UdpSlot is like a socket
// . we use transactionIds to link incoming replies w/ the initiating requests
// . the key of the UdpSlot is based on transactionId and ip/port of dest. host
// . when sending a request you supply a callback to be called on completion
//   of that transaction
// . UdpSlot's m_errno will be set if an error or timeout occurred
// . UdpSlot's m_readBuf/m_readBufSize will hold the reply
// . you register your request handlers w/ UdpServer::registerHandler()
// . msgs are routed to handling routines based on msgType in the dgram
// . you can change the protocol by changing UdpProtocol
// . UdpProtocol just contains many virtual datagram parsing functions
// . UdpProtocol's default protocol is the Mattster Protocol(see UdpProtocol.h)
// . Dns.h overrides UdpProtocol to form DnsProtocol
// . we can send up to ACK_WINDOW_SIZE dgrams before requiring the reception
//   of the first dgram we sent's ACK
// . this ACK window helps highPing/highBandwidth connections (distant hosts)
// . readPoll(), sendAckPoll(), readTimeoutPoll() can call callbacks/handlers

#ifndef _UDPSERVER_H_
#define _UDPSERVER_H_

#include <sys/time.h>          // select()
#include <sys/types.h>         // select()
#include <netinet/in.h>        // ntohl() ntohs()
#include "Mem.h"  // ntohll() getNumBitsOn()
#include "File.h" // for setting our socket non-blocking, etc.
#include "UdpSlot.h"
#include "UdpProtocol.h"
#include "Hostdb.h"
#include "Loop.h"   // loop class that handles signals on our socket

//#ifdef _SMALLDGRAMS_
//#define MAX_UDP_SLOTS 1000
//#else
//#define MAX_UDP_SLOTS 5000
//#endif

// . The rules of Async Sig Safe functions

// 1. to be safe, _ass functions should only call other _ass functions.
//    otherwise, that function may be re-entered by the async sig handler.

// 2. most _ass functions turn off interrupts or return if g_inSigHandler.
//    otherwise, they will need to be re_entrant... i.e. while entered from the
//    main process they might be interrupted and entered from the sig handler.

// 3. only _ass functions should be called from an ASYNC signal handler.


class UdpServer {

 public:

	friend class Dns;

	UdpServer() ;
	~UdpServer() ;

	// free send/readBufs for all slots
	void reset();

	// . returns false and sets errno on error
	// . we use this udp "port" for all this server's reads/writes
	// . we need to save and read out last transaction Id in a file to
	//   maintain the msgdb properly for incremental syncing
	// . niceness of 0 is highest priority, 1 is lowest
	// . read/writeBufSize are the socket buf's size
	// . pollTime is how often to call timePollWrapper() (in milliseconds)
	// . it should be at least the minimal slot timeout
	bool init ( unsigned short  port         , 
		    UdpProtocol    *proto        , 
		    long            niceness     ,
		    long            readBufSize  ,
		    long            writeBufSize ,
		    long            pollTime     ,
		    long            maxSlots     ,
		    bool            isDns        );

	bool m_isDns;

	// . sends a request
	// . returns false and sets g_errno on error, true on success
	// . callback will be called on reception of reply or on error
	// . we will set errno before calling callback if an error occurred
	// . errno may be ETIMEDOUT
	// . we call destroySlot(slot) after calling the callback
	// . NULLify slot->m_readBuf,slot->m_sendBuf if you don't want them
	//   to be freed by destroySlot(slot)
	// . hostId is used to lookup Host's in g_hostdb to get/put resend time
	// . if an ack isn't received after a certain time we resend the dgram
	// . if endpoint calls sendErrorReply() to reply to you your callback
	//   will be called with errno set to what he passed to sendErrorReply
	// . UdpSlot is like a udp socket
	// . "msgType" may be stored in each dgram's header depending on
	//   if you're using UdpProtocol, DnsProtocol, ...
	// . "msgType" is used to route the request to handling functions
	//   on the remote machine
	// . backoff is how long to wait for an ACK in ms before we resend
	// . we double backoff each time we wait w/o getting any ACK
	// . don't wait longer than maxWait for a resend
	// . if we try to resend a request dgram MORE than "maxResends" times,
	//   we do not resend it and we returns with g_errno set to ENOACK, 
	//   indicating we have not gotten ANY ack for a dgram. if a host dies
	//   we typically have 10-20 seconds or so before marking it as dead.
	//   BUT with this method we should realize it is fruiteless in like
	//   500 ms or so and Multicast or Proxy.cpp can re-send to another 
	//   host. for niceness=0 requests the backoff is usually constant
	//   and set to about 30 ms. so if you set maxResends to 10 that is
	//   probably at least 300 ms of resending tries.
	bool sendRequest ( char     *msg          ,
			   long      msgSize      ,
			   unsigned char    msgType      ,
			   unsigned long   ip     ,
			   unsigned short  port   ,
			   long      hostId       ,
			   UdpSlot **retSlot      , // can be NULL
			   void     *state        , // callback state
			   void    (* callback ) (void *state, UdpSlot *slot) ,
			   long      timeout      = 60 , // seconds
			   short     backoff      = -1 ,
			   short     maxWait      = -1   , // ms
			   char     *replyBuf     = NULL ,
			   long      replyBufMaxSize = 0 ,
			   long      niceness = 1 , 
			   long      maxResends = -1 );

	// . send a reply to the host specified in "slot"
	// . slot is destroyed on error or completion of the send
	// . the "msg" will be freed unless slot->m_sendBufAlloc is set to NULL
	// . backoff is how long to wait for an ACK in ms before we resend
	// . we double backoff each time we wait w/o getting any ACK
	// . don't wait longer than maxWait for a resend
	void sendReply_ass (char     *msg        ,
			    long      msgSize    ,
			    char     *alloc      ,
			    long      allocSize  ,
			    UdpSlot  *slot       ,
			    long      timeout    = 60   , // in seconds
			    void     *state      = NULL , // callback state
			    void (* callback2)(void *state,UdpSlot *slot)=NULL,
			    short     backoff    = -1 ,
			    short     maxWait    = -1 ,
			    bool      isCallback2Hot = false ,
			    bool      useSameSwitch  = false );

	// . propagate an errno to the requesting machine
	// . his callback will be called with errno set to "errnum"
	void sendErrorReply ( UdpSlot   *slot     , 
			      long       errnum   , 
			      long       timeout  = 60 /*seconds*/ );

	// . too many transactions going on?
	// . this is just an estimate...
	//long getNumAvailSlots () { return MAX_UDP_SLOTS - m_topUsedSlot - 1; };

	// an estimation as well
	//long getNumUsedSlots  () { return m_topUsedSlot + 1; };
	long getNumUsedSlots  () { return m_numUsedSlots; };
	

	// . when a request/msg of type "msgType" is received we call the
	//   corresponding request handler on this machine
	// . use this function to register a handler for a msgType
	// . we do NOT destroy the slot after calling the handler
	// . handler MUST call sendReply() or sendErrorReply() no matter what
	// . returns true if handler registered successfully
	// . returns false on error
	// . if you want the handler to be called while in an async signal
	//   handler then set "isHandlerHot" to true
	bool registerHandler ( unsigned char msgType, 
			       void(* handler)(UdpSlot *,long) ,
			       bool isHandlerHot = false );

	// . frees the m_readBuf and m_sendBuf
	// . marks the slot as available
	// . called after callback called for a slot you used to send a request
	// . called after sendReply()/sendErrorReply() completes or has error
	void destroySlot ( UdpSlot *slot );

	// . take a slot that we made from sendRequest() above and reset it
	// . you request will be sent again w/ the original parameters
	// void resendSlot ( UdpSlot *slot );

	// these *Poll() routines must be public so wrappers can call them

	// . this is called by main/Loop.cpp when m_sock is ready for reading
	// . actually it calls readPollWrapper() which calls this
	// . this calls registerd handlers for recvd REQUESTS based on msgType
	// . this calls callbacks (in UdpSlot) for received REPLIES
	// . returns false if it read nothing and had no errors
	// . returns true  if it read something or had an error so you can
	//   call it again ASAP because it has unfinished business
	bool readPoll ( long long now );

	// . this is called by main/Loop.cpp when m_sock is ready for writing
	// . actually it calls sendPollWrapper()
	// . it sends as much as it can from all UdpSlots until one blocks
	//   or until it's done 
	// . sends both dgrams AND ACKs
	bool sendPoll_ass ( bool allowResends , long long now );

	// called every 30ms to get tokens? not any more...
	void timePoll ( );

	// called by readPoll()/sendPoll()/readTimeoutPoll() to do
	// reading/sending/callbacks in that order until nothing left to do
	void process_ass ( long long now , long maxNiceness = 100);

	// . this is called by main/Loop.cpp every second
	// . actually it calls readTimeoutPollWrapper()
	// . call the callback of reception slots that have timed out
	// . return true if we did something, like reset a slot for resend
	//   or timed a slot out so it's callback should be called
	bool readTimeoutPoll ( long long now ) ;

	// how nice as a server are we?
	//long getNiceness ( ) { return m_niceness; };

	// m_token should point to shared memory
	//bool useSharedMem() ;
	//bool setupSharedMem() ;

	// . sets *s_token and *s_token2 to NULL
	// . resets bits and bit counts on all big-reply slots so they
	//   send dgrams or acks in attempt to get the newly available token
	//void releaseTokens ( UdpSlot *slot = NULL ) ;

	// . this will wait until all fully received requests have had their
	//   reply sent to them
	// . in the meantime it will send back error replies to all new 
	//   incoming requests
	// . this will do a blocking close on the listening socket descriptor
	// . returns false if blocked, true otherwise if shutdown immediate
	// . set g_errno on error
	bool shutdown ( bool urgent );

	// the high priority udp server suspends/resumes the low priority one
	// when it has/doesn't-have pending requests/replies
	void suspend ();
	void resume  ();
	bool isSuspended        () { return m_isSuspended; };
	bool needBottom         () { return m_needBottom; }   

	UdpSlot *getUdpSlotNum   ( long  i ) { return &m_slots[i]; };
	//bool   isEmpty         ( long i  ) { return (m_keys[i].n0 == 0LL);};
	//long   getTopUsedSlot  (         ) { return m_topUsedSlot; };

	// try calling makeCallback() on all slots
	bool makeCallbacks_ass ( long niceness );
	bool makeCallbacks_ass2( long niceness );

	// return true if we turned them off, false if we did not
	bool interruptsOff() {
		if ( m_isRealTime && g_interruptsOn && ! g_inSigHandler ) {
			g_loop.interruptsOff();
			return true;
		}
		return false; };

	void interruptsOn() { g_loop.interruptsOn(); };

	// when a call to sendto() blocks we set this to true so Loop.cpp
	// will know to manually call sendPoll_ass() rather than counting
	// on receiving a fd-ready-for-writing signal for this UdpServer
	bool m_needToSend;

	UdpSlot *getActiveHead ( ) { return m_head2; };

	// cancel a transaction
	void cancel ( void *state , unsigned char msgType ) ;

	// replace ips and ports in outstanding slots
	void replaceHost ( Host *oldHost, Host *newHost );


	void printState();

	// count how many of each msgType we drop, report on PageStats.cpp
	//long m_droppedNiceness0[128];
	//long m_droppedNiceness1[128];

	// . we have up to 1 handler routine for each msg type
	// . call these handlers for the corresponding msgType
	// . msgTypes go from 0 to 64 i think (see UdpProtocol.h dgram header)
	void (* m_handlers[MAX_MSG_TYPES])(UdpSlot *slot, long niceness);

	// changes timeout to very low on dead hosts
	bool timeoutDeadHosts ( class Host *h );

	// private:

	// . we maintain a sequential list of transaction ids to guarantee
	//   uniquness to a point
	// . if server is restarted this will go back to 0 though 
	// . the key of a UdpSlot is based on this, the endpoint ip/port and
	//   whether it's a request/reply by/from us
	long getTransId ( ) { 
		long tid = m_nextTransId;
		m_nextTransId++;
		if ( m_nextTransId >= UDP_MAX_TRANSID ) m_nextTransId = 0; 
		return tid;
	};

	// . send as many dgrams as you can from slot's m_sendBuf
	// . returns false and sets errno on error, true otherwise
	bool doSending_ass ( UdpSlot *slot, bool allowResends, long long now );

	// . calls a m_handler request handler if slot->m_callback is NULL
	//   which means it was an incoming request
	// . otherwise calls slot->m_callback because it was an outgoing
	//   request
	bool makeCallback_ass ( UdpSlot *slot ) ;

	// . picks the slot that is most caught up to it's ACKs
	// . picks resends first, however
	// . then we send a dgram from that slot
	UdpSlot *getBestSlotToSend ( long long now );

	// . reads a pending dgram on the udp stack
	// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
	// . called by readPoll()
	long readSock_ass ( UdpSlot **slot , long long now );

	// a debug util
	void dumpdgram ( char *dgram , long dgramSize );

	// returns false if cannot shutdown right now due to pending traffic
	//bool tryShuttingDown ( bool callCallback ) ;

	// our listening/sending udp socket and port
        int            m_sock ;  
        unsigned short m_port ;

	// used to prevent cpu usage by sendPoll() when nothing can be sent
	bool   m_gotMsgsToSend;

	// for defining your own protocol on top of udp
	UdpProtocol *m_proto;

	// is the handler meant to be called from an async signal handler?
	bool m_isHandlerHot [ MAX_MSG_TYPES ];

	// . we need a transaction id for every transaction so we can match
	//   incoming reply msgs with their corresponding request msgs
	// . TODO: should be stored to disk on shutdown and every 1024 sends
	// . store a shutdown bit with it so we know if we crashed
	// . on crashes add 1024 or so to the read value 
	// . TODO: make somewhat random cuz it's easy to spoof like it is now
	long         m_nextTransId;

	// our niceness level
	//long         m_niceness;

	// called when shutdown completes
	void (*m_shutdownCallback )( void *state );
	void  *m_shutdownState;
	bool   m_isShuttingDown;

	//did we have to give back control before we called all of the 
	bool   m_needBottom;
	// are we suspended so the high priority server can be more efficient?
	bool   m_isSuspended;
	// . how many requests are we handling at this momment
	// . does not include requests whose replies we are sending, only
	//   those whose replies have not yet been generated
	// . starts counting as soon as first dgram of request is recvd
	long   m_requestsInWaiting;

	// like m_requestsInWaiting but requests which spawn other requests
	long   m_msg10sInWaiting;
	//long   m_msgDsInWaiting;
	//long   m_msg23sInWaiting;
	long   m_msg25sInWaiting;
	long   m_msg50sInWaiting;
	long   m_msg39sInWaiting;
	long   m_msg20sInWaiting;
	long   m_msg2csInWaiting;
	long   m_msg0csInWaiting;
	long   m_msg0sInWaiting;

	// do we live on interrupts?
	bool   m_isRealTime;

	long m_outstandingConverts;

	// we now avoid malloc with these
	//UdpSlot m_slots    [ MAX_UDP_SLOTS ];
	// but alloc MAX_UDP_SLOTS of these in init so we don't blow the stack
	UdpSlot *m_slots    ;
	//key_t    m_keys     [ MAX_UDP_SLOTS ];
	//long     m_topUsedSlot;
	long     m_maxSlots;

	// routines
	UdpSlot *getEmptyUdpSlot_ass ( key_t k );
	void     freeUdpSlot_ass     ( UdpSlot *slot );

	void addKey ( key_t key , UdpSlot *ptr ) ;

	// verified these are only called from within _ass routines that
	// turn them interrupts off before calling this
	//bool     isEmpty         ( UdpSlot *slot ) {
	//	return isEmpty ( slot - m_slots ); };
	UdpSlot *getUdpSlot      ( key_t k );

	// . hash table for converting keys to slots
	// . if m_ptrs[i] is NULL, ith bucket is empty
	UdpSlot       **m_ptrs;
	long            m_numBuckets;
	unsigned long   m_bucketMask;
	char           *m_buf;     // memory to hold m_ptrs
	long            m_bufSize;

	// linked list of available slots (uses UdpSlot::m_next)
	UdpSlot        *m_head;
	// linked list of slots in use
	UdpSlot        *m_head2;
	UdpSlot        *m_tail2;
	// linked list of callback candidates
	//UdpSlot        *m_head3;

	long            m_numUsedSlots;

	// stats
 public:
	long long       m_eth0BytesIn;
	long long       m_eth0BytesOut;
	long long       m_eth0PacketsIn;
	long long       m_eth0PacketsOut;
	long long       m_eth1BytesIn;
	long long       m_eth1BytesOut;
	long long       m_eth1PacketsIn;
	long long       m_eth1PacketsOut;

	long long       m_outsiderPacketsIn;
	long long       m_outsiderPacketsOut;
	long long       m_outsiderBytesIn;
	long long       m_outsiderBytesOut;
};

extern class UdpServer g_udpServer;

// this is the high priority udpServer, it's requests are handled first
extern class UdpServer g_udpServer2;

extern long g_dropped;
extern long g_corruptPackets;
extern bool g_inHandler;

#endif

