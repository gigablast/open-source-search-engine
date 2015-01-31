// Matt Wells, copyright Mar 2001

// . this class defines a protocol on top of udp
// . a protocol is defined with the member functions of UdpProtocol
// . used by UdpServer class
// . overriden by the Dns.h class for Dns protocol
// . the default protocol for UdpServer is the Mattster Protocol (MP)
// . we call callbacks of N=0 before N=1

// . bitmap of a Mattster Protocol (MP) msg:
// . tttttttt ACNnnnnn nnnnnnnn nnnnnnnn  R = is Reply?, E = hadError? N=nice
// . REiiiiii iiiiiiii iiiiiiii iiiiiiii  t = msgType, A = isAck?, n = dgram #
// . ssssssss ssssssss ssssssss ssssssss  i = transId C = cancelTransAck
// . dddddddd dddddddd dddddddd dddddddd  s = msgSize (iff !ack) (w/o hdrs!)
// . dddddddd ........ ........ ........  d = msg content ...

// . NOTE: we use the hadError bit to indicate server-side errors
// . NOTE: we use the weInitiated bit to avoid transId collisions 
// . IMPORTANT: ACKs don't have the 4 bytes used for msgSize, "sssss..."
// . dgrams are generaly limited to 512 bytes so dgram # needs only 23 bits

// . TODO: verify you keep network order here, i found lots of mistakes
// . byte order is different on different machines

#ifndef _UDPPROTOCOL_H_
#define _UDPPROTOCOL_H_

#include <netinet/in.h>

//#define MAX_MSGTYPES (0x3f+1)
// this is for PageStats.cpp
#define MAX_MSG_TYPES 256

#define UDP_MAX_TRANSID 0x3fffffff

class UdpProtocol {

 public:
	 UdpProtocol() {}
	 virtual ~UdpProtocol() {}

	// every dgram needs a transaction id so we can link reply w/ request
	virtual int32_t  getTransId    ( const char *peek, int32_t peekSize ) {
		if ( peekSize < 8 ) return -1;
		return (ntohl (*(int32_t *)(peek+4))) & 0x3fffffff;    };

	// . dns server returns false for this
	virtual bool  useAcks () { return true; };

	// . we need the weInitiated bit because the keys of UdpSlots that 
	//   were initiated by us are different than if they were initiated 
	//   by a remote request
	// . this is to avoid collisions between 2 transactions, with the
	//   same transactionId (transId) where one of the transactions was
	//   initiated by us and the other was initiated remotely.
	virtual bool  didWeInitiate ( const char *peek, int32_t peekSize ) {
		if ( peekSize < 8 ) return false;
		return ( ntohl(*(int32_t *)(peek+4)) & 0x80000000 ); };

	// . this bit is flipped from dns protocol
	// . dns uses it to signify a reply, it's our weInitiated (request) bit
	virtual bool  isReply ( const char *peek, int32_t peekSize ) {
		return ! didWeInitiate ( peek , peekSize ); };

	virtual bool hadError       ( const char *peek, int32_t peekSize ) {
		if ( ntohl(*(int32_t *)(peek+4)) & 0x40000000) return true;
		return false;
	}

	// should we remove headers from the UdpSlot::m_readBuf ?
	virtual bool stripHeaders ( ) { return true; };

	// peek ahead for header (12 bytes) then 4 bytes for possible errno
	//virtual int32_t getMaxPeekSize ( ) { return 24; };
	// add 1 so we can get RDBIDOFFSET from msg 0x00 requests
	virtual int32_t getMaxPeekSize ( ) { return 25; };

	// . returns 0 if hadError bit is NOT set
	// . otherwise, returns first 4 bytes of msg CONTENT as an errno
	virtual int32_t getErrno       ( const char *peek, int32_t peekSize ) {
		if ( peekSize < 16 ) return 0;
		if ( ! hadError (peek,peekSize)  ) return 0;
		return ntohl (*(int32_t *)(peek + 12 ) ); };

	// is this dgram an ACK?
	virtual bool  isAck         ( const char *peek, int32_t peekSize ) {
		if ( peekSize != 8 ) return false;
		return ( ntohl(*(int32_t *)peek) & 0x00800000 ); };

	virtual bool  isCancelTrans ( const char *peek, int32_t peekSize ) {
		return ( ntohl(*(int32_t *)peek) & 0x00400000 ); };

	virtual bool  isNice ( const char *peek, int32_t peekSize ) {
		return ( ntohl(*(int32_t *)peek) & 0x00200000 ); };

	// . get the key of the slot this dgram belongs to
	// . ip is in network order BUT port is in host order
	// . this dgram is one that we read, so flip weInitiated bit
	// . this will make the key of a reply match the key of the request
	virtual key_t makeKey        ( const char     *header    ,
				       int32_t            peekSize ,
				       uint32_t   ip , 
				       uint16_t  port ) {
		return makeKey ( ip , port , 
				 getTransId    ( header , 12 ) ,
				 ! didWeInitiate ( header , 12 ) );
	};

	// . weInitiated is true iff we initiated this transaction
	// . that applies to ACKs as well
	virtual key_t makeKey ( uint32_t  ip , 
				uint16_t port, 
				int32_t           transId ,
				bool           weInitiated ) {
		key_t key;
		key.n1 = transId;
		key.n0 = (((uint64_t) ip) << 16 ) | port;
		// . this prevents collisions between hosts using same transId
		// . because only one of the 2 will have the callback set
		if ( weInitiated ) key.n0 |= 0x8000000000000000LL;
		return key;
	};

	// need this so we can re-assemble dgrams in order
	virtual int32_t  getDgramNum   ( const char *peek, int32_t peekSize ) {
		return ntohl (*(int32_t *)(peek  ))  & 0x001fffff ;  };

	// . msgSize without the dgram headers
	// . returns -1 is unknown, but less than a dgrams worth of bytes
        virtual int32_t  getMsgSize     ( const char *peek , int32_t peekSize ) { 
		if ( peekSize < 12 ) return 0;
		return ntohl (*(int32_t *)(peek+8)) ; };

	// . how many dgram in the msg?
	// . similar to UdpSlot::sendSetup(...)
	virtual int32_t  getNumDgrams  ( int32_t msgSize , int32_t maxDgramSize ) {
		if ( msgSize == -1 ) return 1;
		int32_t n = msgSize / (maxDgramSize - 12);
		if      ( n == 0              ) n = 1;
		else if ( msgSize % (maxDgramSize - 12) != 0 ) n++;
		return n;
	};

	virtual unsigned char getMsgType ( const char *peek, int32_t peekSize ) {
		if ( peekSize <  1 ) return 0xff;
		return *peek & 0xff;
	};

	// how big is the header? used so we can extract the msg w/o header.
	virtual int32_t  getHeaderSize ( const char *peek, int32_t peekSize ) {
		return 12; };

	// given a msg to send, how big is the header per dgram?
	// TODO: fix this!
	virtual int32_t  getHeaderSize ( int32_t msgSize ) { 
		return 12; };

	// we don't accept any dgrams from a msg bigger than this
	virtual int32_t  getMaxMsgSize ( ) { 
		return 0x7fffffff; };

	// . make an ACK dgram for this dgram # and this transId
	// . return the dgram size
	virtual int32_t makeAck ( char *dgram, int32_t dgramNum, int32_t transId ,
			       bool  weInitiated , bool cancelTrans ) {
		// set ack bit in dgramNum
		dgramNum = (dgramNum & 0x001fffff) | 0x00800000;
		// . set the weInitiated bit appropriately
		// . this allows makeKey() to get the right slot
		if ( weInitiated ) transId  |= 0x80000000;
		if ( cancelTrans ) dgramNum |= 0x00400000;
		// set dgram # w/ ack bit on
		*(int32_t *)(dgram+0) = htonl ( dgramNum );
		// store the transId
		*(int32_t *)(dgram+4) = htonl ( transId  );
		// return size of the dgram
		return 8; 
	};

	// . you gotta fill this dgram from the msg with your protocol
	// . return the size of the dgram INCLUDING HEADER!
	// . WEtttttt Annnnnnn nnnnnnnn nnnnnnnn  W = weInitiated?, E=hadError?
	virtual void setHeader ( char   *buf         ,
				 int32_t    msgSize     , 
				 unsigned char msgType     ,
				 int32_t    dgramNum    , 
				 int32_t    transId     ,
				 bool    weInitiated ,
				 bool    hadError    ,
				 int32_t    niceness    ) {
		// copy msg first since we alter dgramNum
		//int32_t offset    = dgramNum * ( maxDgramSize - 12);
		//int32_t size      = msgSize - offset;
		//if ( size > maxDgramSize - 12 ) size = maxDgramSize - 12;
		// gbmemcpy is not async signal safe!! why not???
		//gbmemcpy ( dgram + 12 , msg + offset , size );
		//gbmemcpy_as ( dgram + 12 , msg + offset , size );
		// . bitmap of first 4 bytes in hi bit to low bit order:
		// . WEmmmmdd dddddddd dddddddd dddddddd
		// . ensure dgramNum doesn't invade reply/ack bits
		if ( dgramNum > 0x001fffff ) {
			log(LOG_LOGIC,"udp: dgramnum too big."); return; }
		// ensure msgType not too big
		//if ( msgType & 0xc0 ) {
		//	log(LOG_LOGIC,"udp: Msg type too big."); return; }
		// turn on weInitiated bit and hadError bit
		//if ( weInitiated ) dgramNum |= 0x80000000;
		//if ( hadError    ) dgramNum |= 0x40000000;
		// set msgType
		dgramNum |= ((uint32_t)(msgType)) << 24 ;
		// niceness bit
		if ( niceness    ) dgramNum |= 0x00200000;
		// store dgram # and it's flags
		*(int32_t *)buf = htonl ( dgramNum );
		// set the transaction id
		int32_t t = transId;
		if ( t & 0xc0000000 ) {
			log(LOG_LOGIC,"udp: Transid too big."); return; }
		// store top 2 bits here now
		if ( weInitiated ) t |= 0x80000000;
		if ( hadError    ) t |= 0x40000000;
		*(int32_t *)(buf + 4) = htonl ( t );
		// set msg Size
		*(int32_t *)(buf + 8) = htonl ( msgSize );
		// return the total dgramSize
		//return size + 12;   
	};
};

#endif
