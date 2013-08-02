#ifndef _AUTOBAN_H_
#define _AUTOBAN_H_

#include "TcpServer.h"
#include "HttpRequest.h"
#include "Parms.h"
#include "TuringTest.h"
#include "HashTableT.h"

//must be a power of 2!!!!!!
#define AUTOBAN_INITSIZE  262144
//#define AUTOBAN_INITSIZE  65536
//#define AUTOBAN_INITSIZE  32768
//#define AUTOBAN_INITSIZE  128

#define ONE_DAY 60*60*24

struct CodeVal {
	char  m_code[32];
	long  m_ip;
	long long  m_count;
	long  m_outstanding;
	long  m_maxEver;
	long  m_maxOutstanding;
	long long  m_bytesSent;
	long long  m_bytesRead;
};

class AutoBan {
 public:
	enum AutobanFlags {
		CLEAR  = 0x0, 
		ALLOW    = 0x80,
		DENY     = 0x40,
		FROMCONF = 0x20 
	};


	struct DetectVal {
		unsigned char  m_flags;
		unsigned char  m_minuteCount;
		unsigned char  m_timesBanned;
		long  m_dayCount;
		long  m_minuteExpires;
		long  m_dayExpires;
	};


	//init functions
	AutoBan();
	~AutoBan();

	bool init();
	void reset();
	bool save();
	bool restore();


	bool hasCode(char *code, long codeLen, long ip);

	bool hasPerm(long ip, 
		     char *code, long codeLen, 
		     char *uip,  long uipLen,
		     TcpSocket  *s,
		     HttpRequest *r,
		     SafeBuf *testBuf,
		     bool justCheck ); // check, not incmreneting though
	bool isBanned(unsigned long ip);


	long getSlot(long ip);
	bool addIp(long ip, char allow);
	bool addKey(long ip, DetectVal* v);
	bool growTable();
	bool printTable( TcpSocket *s , HttpRequest *r );
	void removeIp(long ip);
	void cleanHouse();
	void setFromConf();

	// . each client is now limited to a max oustanding requests
	// . Proxy.cpp performs this limitation calculation
	bool incRequestCount ( long h , long bytesRead );
	void decRequestCount ( long h , long bytesSent );

 protected:
	long         *m_detectKeys;
	DetectVal    *m_detectVals;


	long          m_tableSize;
	long          m_numEntries;

	bool setCodesFromConf();

	// hash table for mapping client codes to various stats/counts,
	// called "CodeVals"
	HashTableT <long,CodeVal> m_ht;

	//long          *m_codeKeys;
	//CodeVal       *m_codeVals;

	long           m_codeResetTime;
	//long           m_codeTabSize;
	//long           m_numCodes;

};

extern AutoBan g_autoBan;

#endif
