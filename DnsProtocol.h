// Matt Wells, copyright Mar 2001

#ifndef _DNSPROTOCOL_H_
#define _DNSPROTOCOL_H_

#include "UdpProtocol.h"

// DNS request datagram (in network/bigEndian order):
//
// unsigned        id: 16;
//
// // fields in third byte 
//
// unsigned        qr: 1;          /* response flag */
// unsigned        opcode: 4;      /* purpose of message */
// unsigned        aa: 1;          /* authoritative answer */
// unsigned        tc: 1;          /* truncated message */
// unsigned        rd: 1;          /* recursion desired */
//
// // fields in fourth byte 
//
// unsigned        ra: 1;          /* recursion available */
// unsigned        unused :1;      /* unused bits (MBZ as of 4.9.3a3) */
// unsigned        ad: 1;          /* authentic data from named */
// unsigned        cd: 1;          /* checking disabled by resolver */
// unsigned        rcode :4;       /* response code */
// unsigned        qdcount :16;    /* number of question entries */
// unsigned        ancount :16;    /* number of answer entries */
// unsigned        nscount :16;    /* number of authority entries */
// unsigned        arcount :16;    /* number of resource entries */
//
// // query record/entry 
//
// CCLLLLLL           L = length of label, C = compressed?
// ssssssss ........  s = label (or next 8 bits of offset if C is true)
// ........
// CCLLLLLL           L = length of label, C = compressed?
// ssssssss ........  s = label (or next 8 bits of offset if C is true)
// 00000000           0 = zero, no more labels
// tttttttt tttttttt  t = queryType
// cccccccc cccccccc  c = queryClass
//
// // other records/entries 
//
// CCLLLLLL           L = length of label, C = compressed?
// ssssssss ........  s = label (or next 8 bits of offset if C is true)
// ........
// CCLLLLLL           L = length of label, C = compressed?
// ssssssss ........  s = label (or next 8 bits of offset if C is true)
// 00000000           0 = zero, no more labels
// tttttttt tttttttt  t = queryType
// cccccccc cccccccc  c = queryClass
// TTTTTTTT TTTTTTTT  T = time to live (TTL)
// TTTTTTTT TTTTTTTT
// rrrrrrrr rrrrrrrr  r = resource data len
// dddddddd ........  d = resource data

class DnsProtocol : public UdpProtocol {

 public:
	 DnsProtocol() {}
	 virtual ~DnsProtocol() {}

	// override some protocol-depenent datagram parsing funcs
        // override these 4 functions to suit your own protocol
        virtual long getTransId    ( const char *peek , long peekSize) {
                return ntohs (  *(short     *)(peek)  ) ;    };

	// we never use acks in dns land
	virtual bool useAcks () { 
		return false; };

	// this is the reply/response bit in dns-land
	virtual bool isReply ( const char *peek , long peekSize ) { 
		if ( peekSize < 4 ) return true;
		return peek[2] & 0x80; };

	virtual bool  didWeInitiate ( const char *peek, long peekSize ) {
		return ! isReply(peek , peekSize); };

	virtual bool hadError      ( const char *peek , long peekSize ) { 
		return false;
	};

	// should we remove headers from the UdpSlot::m_readBuf ?
	virtual bool stripHeaders ( ) { return false; };

	// first 4 bytes should have the info we need for DNS reply processing
	virtual long getMaxPeekSize ( ) { return 4; };

	// . do we have errnos in a dns reply?
	// . TODO: investigate this
	// . returns 0 if hadError bit is NOT set
	// . otherwise, returns first 4 bytes of msg CONTENT as an errno
	virtual long getErrno       ( const char *peek, long peekSize ) {
		return 0; };

	// we don't use acks
        virtual bool isAck          ( const char *peek , long peekSize ) { 
		return false; };

	// we don't cancel signals
        virtual bool isCancelTrans  ( const char *peek , long peekSize ) { 
		return false; };

	virtual bool  isNice ( const char *peek, long peekSize ) {
		return true; };

	// we never use more than 1 udp dgram for dns
        virtual long getDgramNum    ( const char *peek , long peekSize ) { 
		return 0; };

	// . even though it may be less than this, we can assume it's full
	// . returns -1 if msg size is the length of the whole dgram
        virtual long getMsgSize     ( const char *peek , long peekSize ) { 
		return -1; };

	// how many dgram in the msg?
	virtual long  getNumDgrams  ( long msgSize , long maxDgramSize ) {
		return 1;
	}

	// TODO: i guess all msg types are 0 for now
	virtual unsigned char getMsgType ( const char *peek, long peekSize ) {
		return 0; };

	// consider for our purposes to be 0
        virtual long getHeaderSize ( const char *peek , long peekSize ) { 
		return 0;          };

	// given a msg to send, how big is the header?
	virtual long  getHeaderSize ( long msgSize ) {
		return 0; };

        virtual long getMaxMsgSize ( ) { 
		return DGRAM_SIZE_DNS; };

	// we don't make Acks so return a dgramSize of 0
	virtual long makeAck ( char *dgram, long dgramNum, long transId){
		return 0; };

	// we store the 1 dgram in it's ready to send form
	virtual void setHeader ( char   *buf         , 
				 long    msgSize     ,
				 unsigned char  msgType     ,
				 long    dgramNum    , 
				 long    transId     ,
				 bool    weInitiated ,
				 bool    hadError    ,
				 long    niceness    ) {
		//if ( msgSize > maxDgramSize ) msgSize = maxDgramSize;
		//memcpy ( dgram , msg , msgSize );
		//return msgSize;
		return;
	}

	// this is something we added
	//bool isTruncated   ( const char *peek , long peekSize ) {
	//	return ( dgram[3] & 0x02 ); };

};

#endif
