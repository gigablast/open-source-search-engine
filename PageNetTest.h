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
	bool netTestStart_r( bool amThread, long num );
	void threadControl();
	bool collectResults();
	bool gotResults( TcpSocket *s );

	long getSend1   ( long index ) {return m_hostRates[0][index]; };
	long getReceive1( long index ) {return m_hostRates[1][index]; };
	long getSend2   ( long index ) {return m_hostRates[2][index]; };
	long getReceive2( long index ) {return m_hostRates[3][index]; };

	// Socket Functions
	int  openSock ( long num, long type, struct sockaddr_in *name, 
			long port );
	long readSock ( int sock );
	long sendSock ( int sock );
	int  closeSock( int sock );

	// Html Data/Stats Page Functions
	bool controls   ( TcpSocket *s, HttpRequest *r );
	bool resultsPage( TcpSocket *s );//, HttpRequest *r );
	
	// Access Functions
	bool isRunning()  { return m_running;    };
	bool runNetTest() { return m_runNetTest; };

	long               m_threadNum;
	long		   m_threadReturned;

 private:
	//Member Variables
	bool               m_runNetTest;
	bool               m_running;
	bool               m_fullDuplex;
	char               m_coll[16];
	long		   m_numResultsSent;
	long		   m_numResultsRecv;

	long               m_type      [MAX_TEST_THREADS];
	int                m_sock      [MAX_TEST_THREADS];
	long               m_port      [MAX_TEST_THREADS];
        struct sockaddr_in m_name      [MAX_TEST_THREADS]; 
	long               m_testHostId[MAX_TEST_THREADS];
	long		   m_testIp    [MAX_TEST_THREADS];
	char               m_rdgram    [NTDGRAM_SIZE    ];
	char               m_sdgram    [NTDGRAM_SIZE    ];

	unsigned long      m_calcTable [MAX_TEST_THREADS][AVG_TABLE_SIZE];
	unsigned long      *m_hostRates[4];

	struct sockaddr_in m_to;
	struct sockaddr_in m_from;


	long long          m_startTime;
	long long          m_endTime;
	long long          m_calcTime;

	long		   m_hostId;
	long		   m_switchId;
	long		   m_firstHostOnSwitch;
	long		   m_lastHostOnSwitch;
	long		   m_numHostsOnSwitch;
	long		   m_numSwitches;
	long		   m_testBytes;
	long		   m_testDuration;

};

extern class PageNetTest g_pageNetTest;

#endif
