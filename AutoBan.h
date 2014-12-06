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
	int32_t  m_ip;
	int64_t  m_count;
	int32_t  m_outstanding;
	int32_t  m_maxEver;
	int32_t  m_maxOutstanding;
	int64_t  m_bytesSent;
	int64_t  m_bytesRead;
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
		int32_t  m_dayCount;
		int32_t  m_minuteExpires;
		int32_t  m_dayExpires;
	};


	//init functions
	AutoBan();
	~AutoBan();

	bool init();
	void reset();
	bool save();
	bool restore();


	bool hasCode(char *code, int32_t codeLen, int32_t ip);

	bool hasPerm(int32_t ip, 
		     char *code, int32_t codeLen, 
		     char *uip,  int32_t uipLen,
		     TcpSocket  *s,
		     HttpRequest *r,
		     SafeBuf *testBuf,
		     bool justCheck ); // check, not incmreneting though
	bool isBanned(uint32_t ip);


	int32_t getSlot(int32_t ip);
	bool addIp(int32_t ip, char allow);
	bool addKey(int32_t ip, DetectVal* v);
	bool growTable();
	bool printTable( TcpSocket *s , HttpRequest *r );
	void removeIp(int32_t ip);
	void cleanHouse();
	void setFromConf();

	// . each client is now limited to a max oustanding requests
	// . Proxy.cpp performs this limitation calculation
	bool incRequestCount ( int32_t h , int32_t bytesRead );
	void decRequestCount ( int32_t h , int32_t bytesSent );

 protected:
	int32_t         *m_detectKeys;
	DetectVal    *m_detectVals;


	int32_t          m_tableSize;
	int32_t          m_numEntries;

	bool setCodesFromConf();

	// hash table for mapping client codes to various stats/counts,
	// called "CodeVals"
	HashTableT <int32_t,CodeVal> m_ht;

	//int32_t          *m_codeKeys;
	//CodeVal       *m_codeVals;

	int32_t           m_codeResetTime;
	//int32_t           m_codeTabSize;
	//int32_t           m_numCodes;

};

extern AutoBan g_autoBan;

#endif
