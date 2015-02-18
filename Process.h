// Gigablast, Inc., Copyright Mar 2007

#ifndef _PROCESS_H_
#define _PROCESS_H_

#define NO_MODE   0
#define EXIT_MODE 1
#define SAVE_MODE 2
#define LOCK_MODE 3


#include "HttpRequest.h"
//#include "Msg28.h"

#include "PageInject.h" // resumeImports() function

class Process {

 public:

	bool getFilesToCopy ( char *srcDir , class SafeBuf *buf ) ;
	bool checkFiles ( char *dir );

	// . the big save command
	// . does not save everything, just the important stuff
	bool save ( );

	// . this will save everything and exit
	// . urgent is true if we cored
	bool shutdown ( bool   urgent,
			void  *state = NULL,
			void (*callback) (void *state ) = NULL);

	bool checkNTPD();

	Process                 ( ) ;
	bool init               ( ) ;
	bool isAnyTreeSaving    ( ) ;
	bool save2              ( ) ;
	bool shutdown2          ( ) ;
	void disableTreeWrites  ( bool shuttingDown ) ;
	void enableTreeWrites   ( bool shuttingDown ) ;
	bool isRdbDumping       ( ) ;
	bool isRdbMerging       ( ) ;
	bool saveRdbTrees       ( bool useThread , bool shuttingDown ) ;
	bool saveRdbMaps        ( bool useThread ) ;
	bool saveRdbCaches      ( bool useThread ) ;
	bool saveBlockingFiles1 ( ) ;
	bool saveBlockingFiles2 ( ) ;
	void resetAll           ( ) ;
	void resetPageCaches    ( ) ;
	double getLoadAvg	( );
	void resetLoadAvg	( );

	int64_t getTotalDocsIndexed();
	int64_t m_totalDocsIndexed;

	class Rdb *m_rdbs[32];
	int32_t       m_numRdbs;
	bool       m_urgent;
	char       m_mode;
	int64_t  m_lastSaveTime;
	int64_t  m_processStartTime;
	bool       m_sentShutdownNote;
	bool       m_blockersNeedSave;
	bool       m_repairNeedsSave;
	int32_t       m_try;
	int64_t  m_firstShutdownTime;

	void        *m_callbackState;
	void       (*m_callback) (void *state);

	// a timestamp for the sig alarm handler in Loop.cpp
	int64_t m_lastHeartbeatApprox;

	void callHeartbeat ();

	bool m_threadOut;

	bool m_suspendAutoSave;

	bool gotPower ( TcpSocket *s );
	bool        m_powerReqOut;
	bool        m_powerIsOn;
	int64_t   m_powerOffTime;
	HttpRequest m_r;
	//Msg28       m_msg28;
	bool        m_exiting;
	bool        m_calledSave;

	bool gotFanReply ( TcpSocket *s );
	void checkFanSwitch ( ) ;
	bool m_fanReqOut;
	float m_dataCtrTemp;
	float m_roofTemp;
	int32_t  m_currentFanState;
	int32_t  m_desiredFanState;
	float m_diskUsage;
	int64_t m_diskAvail;
	char m_swapEnabled;
};

extern Process g_process;

extern char *g_files[];

#endif
