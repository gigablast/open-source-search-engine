#ifndef _PAGENETTEST_H_
#define _PAGENETTEST_H_

#include <arpa/inet.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "Pages.h"
#include "Threads.h"


#define TEST_READ		1
#define TEST_SEND		2

#define NTDGRAM_SIZE		1450
#define	MAX_TEST_THREADS	4
#define AVG_TABLE_SIZE		50


void controlNetTest   ( int fd, void *state          );
bool sendPageNetTest  ( TcpSocket *s, HttpRequest *r );
bool sendPageNetResult( TcpSocket *s );//, HttpRequest *r );


class PageNetTest {
 public:
	// Control Functions
	bool init();
	bool start();
	bool stop();
	void reset();
	void destructor();
	bool netTestStart_r( bool amThread, int32_t num );
	void threadControl();
	bool collectResults();
	bool gotResults( TcpSocket *s );

	int32_t getSend1   ( int32_t index ) {return m_hostRates[0][index]; };
	int32_t getReceive1( int32_t index ) {return m_hostRates[1][index]; };
	int32_t getSend2   ( int32_t index ) {return m_hostRates[2][index]; };
	int32_t getReceive2( int32_t index ) {return m_hostRates[3][index]; };

	// Socket Functions
	int  openSock ( int32_t num, int32_t type, struct sockaddr_in *name, 
			int32_t port );
	int32_t readSock ( int sock );
	int32_t sendSock ( int sock );
	int  closeSock( int sock );

	// Html Data/Stats Page Functions
	bool controls   ( TcpSocket *s, HttpRequest *r );
	bool resultsPage( TcpSocket *s );//, HttpRequest *r );
	
	// Access Functions
	bool isRunning()  { return m_running;    };
	bool runNetTest() { return m_runNetTest; };

	int32_t               m_threadNum;
	int32_t		   m_threadReturned;

 private:
	//Member Variables
	bool               m_runNetTest;
	bool               m_running;
	bool               m_fullDuplex;
	char               m_coll[16];
	int32_t		   m_numResultsSent;
	int32_t		   m_numResultsRecv;

	int32_t               m_type      [MAX_TEST_THREADS];
	int                m_sock      [MAX_TEST_THREADS];
	int32_t               m_port      [MAX_TEST_THREADS];
        struct sockaddr_in m_name      [MAX_TEST_THREADS]; 
	int32_t               m_testHostId[MAX_TEST_THREADS];
	int32_t		   m_testIp    [MAX_TEST_THREADS];
	char               m_rdgram    [NTDGRAM_SIZE    ];
	char               m_sdgram    [NTDGRAM_SIZE    ];

	uint32_t      m_calcTable [MAX_TEST_THREADS][AVG_TABLE_SIZE];
	uint32_t      *m_hostRates[4];

	struct sockaddr_in m_to;
	struct sockaddr_in m_from;


	int64_t          m_startTime;
	int64_t          m_endTime;
	int64_t          m_calcTime;

	int32_t		   m_hostId;
	int32_t		   m_switchId;
	int32_t		   m_firstHostOnSwitch;
	int32_t		   m_lastHostOnSwitch;
	int32_t		   m_numHostsOnSwitch;
	int32_t		   m_numSwitches;
	int32_t		   m_testBytes;
	int32_t		   m_testDuration;

};

extern class PageNetTest g_pageNetTest;

#endif
