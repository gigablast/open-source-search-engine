// Matt Wells, copyright Nov 2000

// . datagram send control slot
// . UdpServer stores these things in an RdbTree

#ifndef _UDPSLOT_H_
#define _UDPSLOT_H_

#include "Mem.h"
#include "UdpProtocol.h"
#include "Hostdb.h"

// . we want to avoid the overhead of IP level fragmentation
// . so for an MTU of 1500 we got 28 bytes overhead (IP and UDP headers)
// . later we can try large DGRAM_SIZE values to see if faster
//#define DGRAM_SIZE 33000
//#define DGRAM_SIZE 32772
//#define DGRAM_SIZE 63000
//#define DGRAM_SIZE 7500
//#define DGRAM_SIZE ((1500-28)*5)
// this was the most stable size, but now, 4/8/04, i'm trying bigger...
#ifdef _SMALLDGRAMS_
// newspaperarchive machines need this smaller size
#define DGRAM_SIZE (1500-28)
#else
// . here's the new size, 4/8/04, about 20x bigger
// . only use this for our machines
// . this will save memory too, since we use a few bits of mem per dgram
//#define DGRAM_SIZE (30*1492)
// . let's see if smaller dgrams fix the ping spike problem on gk0c
// . this is in addition to lower the ack windows from 12 to 4
#define DGRAM_SIZE 16400
// . the 45k dgram doesn't travel well over the internet, and javier needs
//   to do that for the "interface client" code
#define DGRAM_SIZE_INTERNET (1500-28)
#endif

// i'd like to have less dgram to decrease interrupts and
// to decrease the MAX_DGRAMS define which decrease UdpSlot size
// BUT it may result in extra copying inthe kernel's IP layer...?
//#define DGRAM_SIZE (64*1024-256)
// . that was too big, so Msg0x21 wasn't called that much!
// . just make it 10k in anticipation of jumbo frames for gigabit nets
//#define DGRAM_SIZE (6*1492)


// loop back can do bigger dgram w/o fragmenting at the IP layer
//#define DGRAM_SIZE_LB (16436-28)
// let it do fragmenting
//#define DGRAM_SIZE_LB (64*1024-256)
// . kernel 2.6.8.1 does not like big dgram sizes for loopback
// . can not go above the MTU for the lo device in ifconfig -a
#define DGRAM_SIZE_LB (16400)

// the max of all dgram sizes, of DGRAM_SIZE and of DGRAM_SIZE_LB
#define DGRAM_SIZE_CEILING (30*1492)

// . and for the dns
// . a host was coring because the dgram it got back was bigger than this
//   so i upped from 2000 to 2800. the dns dgram reply it got was 2646 bytes
#define DGRAM_SIZE_DNS (2800)

// . we keep bit counts for every dgram, not just those in a window now
// . therefore, we limit by this for the time being
// . now allow for up to a trunc limit of 1 million --> 7 Megabytes
// . when compiling for Chris or Mark, use the 60M max msg size
// . newspaper archive has s0=20000000 which is up to 180MB termlists!
// . newspaper archive was hitting the wall at 600MB so i upped to 900MB, the
//   downside is that it uses more memory per UdpSlot
//#ifdef _BIGMSGS_
//#define MAX_DGRAMS (((900*1024*1024) / DGRAM_SIZE) + 1)
//#else
//#define MAX_DGRAMS (((7*1024*1024) / DGRAM_SIZE) + 1)
// now we use this for our machines too
//#define MAX_DGRAMS (((85*1024*1024) / DGRAM_SIZE) + 1)
// raised from 50MB to 80MB so Msg13 compression proxy can send back big replies > 5MB
// raised from 80MB to 180MB since we could be sending back a Msg95Reply
// which is a list of QueryChanges. 3/29/13.
#define MAX_DGRAMS (((180*1024*1024) / DGRAM_SIZE_LB) + 1)
//#endif

// . the max size of an incoming request for a hot udp server
// . we cannot call malloc so it must fit in here
// . i increased this from 5k to 10k to better support Msg17's caching requests
//#define TMPBUFSIZE (10*1024)
// . now Msg17 uses low priority udp server to store cached pages, hi to get
//#define TMPBUFSIZE (10*1024)
// . now we need tens of thousands of udp slots, so keep this small
//#define TMPBUFSIZE (1024)
#define TMPBUFSIZE (250)

class UdpSlot {
	
 public:

	// set the UdpSlot's protocol, endpoint info, transId, timeout
	void connect ( UdpProtocol *proto    , 
		       sockaddr_in *endPoint ,
		       Host        *host     ,
		       long         hostId   ,
		       long         transId  ,
		       long         timeout  , // in seconds
		       long long    now      ,
		       long         niceness );

	// same as above
	void connect ( UdpProtocol    *proto    ,
		       unsigned long   ip       ,
		       unsigned short  port     ,
		       Host           *host     ,
		       long            hostId   ,
		       long            transId  ,
		       long            timeout  , // in seconds
		       long long       now      ,
		       long            niceness );

	// reset the slot if ip/port has changed
	void resetConnect ( );

	// . set up this slot for a send (call after connect() above)
	// . returns false and sets errno on error
	// . use a backoff of -1 for the default
	bool sendSetup ( char      *msg         ,  
			 long       msgSize     ,
			 char      *alloc       ,
			 long       allocSize   ,
			 unsigned char msgType     ,
			 long long  now         ,
			 void      *state       ,
			 void      (*callback)(void *state,class UdpSlot *) ,
			 long       niceness    ,
			 short      backoff     ,
			 short      maxWait     ,
			 char      *replyBuf    ,
			 long       replyBufMaxSize );

	// . send a datagram from this slot on "sock" (call after sendSetup())
	// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
	//   1 if sent something
	long sendDatagramOrAck ( int sock , bool allowResends , long long now);

	// . returns false and sets errno on error, true otherwise
	// . tries to send ACK on "sock" if we read a dgram
	// . tries to send a dgram if we read an ACK
	// . sets *discard to true if caller should discard the dgram
	bool readDatagramOrAck ( int sock , char *header , long numRead ,
				 long long now , bool *discard ,
				 long *readSize );

	// . send an ACK
	// . returns -2 if nothing to send, -1 on error, 0 if blocked, 
	//   1 if sent something
	// . should only be called by sendDatagramOrAck() above
	long sendAck ( int sock , long long now ,
		       long dgramNum = -1, long weInitiated = -2 ,
		       bool cancelTrans = false );
	//, bool reset = false );

	// . or by readDataGramOrAck() to read a faked ack for protocols that 
	//   don't use ACKs
	void readAck  ( int sock , long dgramNum , long long now );

	// . will reset to send() will start sending at the first unacked dgram
	// . if "reset" is true then will resend ALL dgrams
	void prepareForResend ( long long now , bool resendAll ) ;

	// reset/set m_resendTime based on m_resendCount
	void setResendTime ();

	// . returns false and sets errno on error
	// . like sendSetup() but setting up for reading
	// . called when an incoming request arrives
	// . we create a new UdpSlot and call this to handle the request
	bool makeReadBuf ( long msgSize , long numDgrams );

	bool hasDgramsToRead ( ) {
		return ( m_readBitsOn < m_dgramsToRead ); };

	bool hasDgramsToSend ( ) {
		return ( m_sentBitsOn < m_dgramsToSend ); };

	bool hasAcksToSend ( ) {
		if ( ! m_proto->useAcks()  ) return false;
		return ( m_sentAckBitsOn  < m_dgramsToRead ) ; };

	bool hasAcksToRead ( ) {
		if ( ! m_proto->useAcks()    ) return false;
		return ( m_readAckBitsOn < m_dgramsToSend ); };

	long getNumDgramsRead () { return m_readBitsOn;    };
	long getNumDgramsSent () { return m_sentBitsOn;    };
	long getNumAcksRead   () { return m_readAckBitsOn; };
	long getNumAcksSent   () { return m_sentAckBitsOn; };

	// this does not include ACKs to read
	bool isDoneReading ( ) { 
		if ( m_dgramsToRead == 0 ) return false;
		if ( hasDgramsToRead()   ) return false;
		return true; };

	// this does not include ACKs to send
	bool isDoneSending ( ) { 
		if ( m_dgramsToSend == 0 ) return false;
		if ( hasDgramsToSend()   ) return false;
		return true; };

	bool isTransactionComplete ( ) {
		if ( ! isDoneReading ()  ) return false;
		if ( ! isDoneSending ()  ) return false;
		if (   hasAcksToRead ()  ) return false;
		if (   hasAcksToSend ()  ) return false;
		return true;
	};

	unsigned char  getMsgType ( ) { return m_msgType; };

	key_t getKey ( ) { return m_proto->makeKey ( m_ip, m_port , 
						     m_transId ,
						 m_callback/*weInitaited?*/);};
	// . for internal use
	// . set a window bit
	void setBit ( long dgramNum , unsigned char *bits ) {
		bits [ dgramNum >> 3 ] |= (1 << (dgramNum & 0x07)); };
	// clear a window bit
	void clrBit ( long dgramNum , unsigned char *bits ) {
		bits [ dgramNum >> 3 ] &= ~(1 << (dgramNum & 0x07)); };
	// get value of a window bit
	bool isOn   ( long dgramNum , unsigned char *bits ) {
		return bits [ dgramNum >> 3 ] & (1 << (dgramNum & 0x07)); };

	// clear all the bits
	//void clrAllBits ( unsigned char *bits , long numBits ) {
	//	memset ( bits , 0 , (numBits >> 3) + 1 ); };

	// set the time we first sent this dgram
	//void      setSendTime ( long dgramNum , long long now ) {
	//	m_sendTimes [ dgramNum ] = (now - m_startTime); };
	// get the time we first sent this dgram
	//long long getSendTime ( long dgramNum ) {
	//	return (long long)m_sendTimes [ dgramNum ] + m_startTime ; };

	// . get the first lit bit position after bit #i
	// . returns numBits if no bits AFTER i are lit
	long getNextLitBit ( long i , unsigned char *bits , long numBits ) {
		for ( long j = i + 1 ; j < numBits ; j++ ) 
			if ( isOn ( j , bits ) ) return j; 
		return numBits;
	};

	// . get the first unlit bit position after bit #i
	// . returns numBits if no bits AFTER i are unlit
	long getNextUnlitBit ( long i , unsigned char *bits , long numBits ) {
		for ( long j = i + 1 ; j < numBits ; j++ ) 
			if ( ! isOn ( j , bits ) ) return j; 
		return numBits;
	};

	// . for sending purposes, the max scoring UdpSlot sends first
	// . return < 0 if nothing to send
	//long getScore ( long long now , UdpSlot *s_token , 
	//		unsigned long s_tokenTime , long LARGE_MSG ) ;
	long getScore ( long long now );

	// what is our niceness level?
	long getNiceness ( ) { return m_niceness; };

	void fixSlot ( ) ;

	void printState() ;
	// call this callback on timout,error or transaction completion.
	// pass it a ptr to ourselves. It returns true if WE should delete
	// the UdpSlot. Otherwise, it must deleted later by a callback that
	// records all the slots in a list so no one forgets them.
	// Typically, you should just have your callback here return true
	// so you don't have to call deleteSlot(slot) and don't have to 
	// worry about wasting memory.
	void      (*m_callback )( void *state , class UdpSlot *slot );

	// this callback is used for letting caller know when his reply has
	// been sent (it's kinda a hack)
	void      (*m_callback2 )( void *state , class UdpSlot *slot );

	// if callback2 can be called from a signal handler then make this true
	bool        m_isCallback2Hot;

	// . save a POINTER to caller's state;
	// . caller must ensure it's not on the stack
	void       *m_state;

	long        m_transId;       // unique transaction ID (like socket fd)
	unsigned long        m_ip;            // the endpoint host's address
	unsigned short       m_port;          // the endpoint host's address

	// a ptr to the Host class for shotgun info
	Host *m_host;

	long        m_hostId;        // the endpoint host's hostId in hostmap
	unsigned char m_msgType;       // i like to use this for class routing

	long        m_timeout;       // deltaT in seconds
	long        m_errno;         // anything go wrong?  0 means ok.
	long        m_localErrno;    // are we sending back an error reply?

	UdpProtocol *m_proto;

	// . transmission-related variables
	// . send/ack times are when they were put on the udp stack by sendto()
	//   and may not be the time they were actually sent
	char          *m_sendBuf;          
	long           m_sendBufSize;
	char          *m_sendBufAlloc ;
	long           m_sendBufAllocSize;
	long           m_dgramsToSend;      
	long           m_resendTime;        // resend after this (in ms)

	// the counts of lit bits for the bits above
	long m_readBitsOn;
	long m_sentBitsOn;
	long m_readAckBitsOn;
	long m_sentAckBitsOn;

	// keep track of the next in line to send
	long m_nextToSend;
	long m_firstUnlitSentAckBit;

	// how many times we've tried to resend
        char           m_resendCount; 

	// used for debugging
	char m_coreOnDestroy;

	// . we record the start time of each dgram transmission (in millisecs)
	// . it's relative to m_startTime
	// . when an ACK arrives we can compute the roundtrip time 4 that dgram
	// . to save mem we only record times for dgrams 0,8,16 ... 4*n
	//unsigned short m_sendTimes [ MAX_DGRAMS / 8 ];

	// . birth time of the udpslot
	// . m_sendTimes are relative to this
	long long      m_startTime;

	// when the request/reply was read, we set this to the current time so
	// we can measure how long it sits in the queue until the handler
	// or callback is called
	long long      m_queuedTime;

	// reception-related variables
	char          *m_readBuf;      // store recv'd msg in here.
	long           m_readBufSize;  // w/o the dgram headers.
	long           m_readBufMaxSize;
	long           m_dgramsToRead; // closely related to m_bytesToRead.

	// last times of a read/send on this slot in milliseconds since epoch
	long long      m_lastReadTime;
	long long      m_lastSendTime;

	// remember our niceness level
	long           m_niceness;

	// when did we call it (ms)
	long long      m_calledHandlerTime;

	// these are for measuring bps (bandwidth) for g_stats
	long long      m_firstReadTime;
	long long      m_firstSendTime;

	// . this is bigger for loopback sends/reads
	// . we set it just low enough to avoid IP layer fragmentation
	long           m_maxDgramSize;

	// did we call the handler for this?
	char           m_calledHandler;
	char           m_calledCallback;
	char           m_convertedNiceness;
	// has a sig been queued to call our callback
	bool           m_isQueued;

	// now caller can decide initial backoff, doubles each time no ack rcvd
	short          m_backoff;
	// don't wait longer than this, however
	short          m_maxWait;


	// . i've discarded the window since msg size is limited
	// . this way is faster 
	// . these bits determine what dgrams we've sent/read/sentAck/readAck
	unsigned char m_sentBits    [ (MAX_DGRAMS / 8) + 1 ];
	unsigned char m_readBits    [ (MAX_DGRAMS / 8) + 1 ];
	unsigned char m_sentAckBits [ (MAX_DGRAMS / 8) + 1 ];
	unsigned char m_readAckBits [ (MAX_DGRAMS / 8) + 1 ];

	// we keep the unused slots in a linked list in UdpServer
	class UdpSlot *m_next;
	// and for doubly linked list of used slots
	class UdpSlot *m_next2;
	class UdpSlot *m_prev2;
	// and for doubly linked list of callback candidates
 	//class UdpSlot *m_next3;
	//class UdpSlot *m_prev3;
	// store the key so when returning slot we can remove from hash table
	key_t m_key;

	char m_preferEth;

	char m_maxResends;

	// . for the hot udp server, we cannot call malloc in the sig handler
	//   so we set m_readBuf to this to read in short requests
	// . caller should pre-allocated m_readBuf when calling sendRequest()
	//   if he expects a large reply
	// . incoming requests simply cannot be bigger than this for the
	//   hot udp server
	char           m_tmpBuf [ TMPBUFSIZE ];
	
	// used by Msg50 to keep a ptr to the new state created by 
	// handleRequest50() so we can spy on it in case it forgets to
	// call a callback and hangs...
	long m_tmpVar;
};

extern long g_cancelAcksSent;
extern long g_cancelAcksRead;

#endif


