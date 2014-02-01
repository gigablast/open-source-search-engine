// Matt Wells, Copyright Apr 2004

#ifndef _PINGSERVER_H_
#define _PINGSERVER_H_

#include "gb-include.h"
#include "Hostdb.h"
#include "SafeBuf.h"
//#include "Repair.h"

extern char g_repairMode;


class EmailInfo {
public:
	SafeBuf m_toAddress;
	SafeBuf m_fromAddress;
	SafeBuf m_subject;
	SafeBuf m_body;
	//char *m_spiderStatusMsg;
	SafeBuf m_spiderStatusMsg;
	//CollectionRec *m_cr;
	collnum_t m_collnum;
	char *m_dom; // ref into m_toAddress of the domain in email addr
	SafeBuf m_mxDomain; // just the domain with a "gbmxrec-" prepended
	void *m_state;
	void (* m_callback ) (void *state);
	void *m_finalState;
	void (* m_finalCallback ) (void *state);
	// ip address of MX record for this domain
	long m_mxIp;
	long m_notifyBlocked;
	bool m_inUse;

	EmailInfo() { 
		memset ( this,0,sizeof(EmailInfo) ); 
	};
	void reset() { 
		if ( m_inUse ) { char *xx=NULL;*xx=0; }
		if ( m_notifyBlocked ) { char *xx=NULL;*xx=0; }
		memset ( this,0,sizeof(EmailInfo) ); 
	};
};

class PingServer {

 public:

	// . set up our PingServer
	// . sets itself from g_conf (our configuration class)
	// . returns false on fatal error
	// . gets filename from Conf.h class
	bool init ( );

	void initKernelErrorCheck();
	
	// for dealing with pings
	bool registerHandler ( );

	void sendPingsToAll();

	// ping host #i
	void          pingHost      ( Host *h , uint32_t ip , uint16_t port );

	void          pingNextHost ( );

	// . send notes to EVERYONE that you are shutting down
	// . when they get one they'll set your ping to DEAD status
	// . returns false if blocked, true otherwise
	bool broadcastShutdownNotes ( bool  sendEmail                ,
				      void   *state                  ,
				      void  (* callback)(void *state));

	// send an email warning that host "h" is dead
	//bool sendEmail ( Host *h );
	bool sendEmail ( Host *h , 
			 char *errmsg = NULL , 
			 bool sendToAdmin = true ,
			 bool oom = false ,
			 bool kernelErrors = false ,
			 bool parmChanged  = false ,
			 bool forceIt      = false ,
			 long mxIP = 0 );

	// tapping a host is telling it to store a sync point by calling
	// Sync::addOp(OP_SYNCPT,NULL,s_timestamp);
	void tapHost ( long hostId ) ;

	// are all hosts in repair mode "mode1" or "mode2"?
	//bool allHostsInRepairModes1 ( );
	//bool allHostsInRepairModes ( char mode1 , char mode2 ) ;
	
	// . update ping time info of this host
	// . uses a 10-ping running average
	// . "tripTime" is in milliseconds
	// . hostId of -1 means unknown (will just return true)
	//void  stampHost  ( long hostId , long tripTime , bool timedOut );

	//long m_launched ;
	//long m_totalLaunched ;
	//long long m_startTime;
	long m_i;

	char m_useShotgun;
	//char m_request[14+4+4+4+1+4];
	char m_pingProxy;
	//char m_reply[9];

	// broadcast shutdown info
	long    m_numRequests ;
	long    m_numReplies ;
	void   *m_broadcastState ;
	void  (*m_broadcastCallback) ( void *state );

	long    m_numRequests2;
	long    m_numReplies2;
	long    m_maxRequests2;

	long    m_pingSpacer;
	long    m_callnum;

	//char   *getReplyBuffer( ) { return m_reply; }
	// . these functions used by Repair.cpp
	// . we do not tally ourselves when computing m_minRepairMode
	long    getMinRepairMode ( ) {
		// is it us?
		if ( g_repairMode < m_minRepairMode ) return g_repairMode;
		// m_minRepairMode could be -1 if uninitialized
		if ( g_hostdb.getNumHosts() != 1    ) return m_minRepairMode;
		return g_repairMode;
	};
	long    getMaxRepairMode ( ) {
		// is it us?
		if ( g_repairMode > m_maxRepairMode ) return g_repairMode;
		// m_maxRepairMode could be -1 if uninitialized
		if ( g_hostdb.getNumHosts() != 1    ) return m_maxRepairMode;
		return g_repairMode;
	};
	// we do not tally ourselves when computing m_numHostsInRepairMode7
	long    getMinRepairModeBesides0 ( ) {
		// is it us?
		if ( g_repairMode < m_minRepairModeBesides0 && 
		     g_repairMode != 0 ) return g_repairMode;
		// m_minRepairMode could be -1 if uninitialized
		if ( g_hostdb.getNumHosts() != 1    ) 
			return m_minRepairModeBesides0;
		return g_repairMode;
	};

	void sendEmailMsg ( long *lastTimeStamp , char *msg ) ;

	void    setMinRepairMode ( Host *h ) ;
	// set by setMinRepairMode() function
	long    m_minRepairMode;
	long    m_maxRepairMode;
	long    m_minRepairModeBesides0;
	Host   *m_minRepairModeHost;
	Host   *m_maxRepairModeHost;
	Host   *m_minRepairModeBesides0Host;

	int32_t m_currentPing  ;
	int32_t m_bestPing     ;
	time_t  m_bestPingDate ;


	// some cluster stats
	long m_numHostsWithForeignRecs;
	long m_numHostsDead;
	long m_hostsConfInAgreement;
	bool m_hostsConfInDisagreement;
};

extern class PingServer g_pingServer;

// . returns false if blocked, true otherwise
// . use this for sending generic emails
bool sendEmail ( class EmailInfo *ei ) ;

// use mailchimp's mandrill email http api
bool sendEmailThroughMandrill ( class EmailInfo *ei ) ;

// send email and webhook notification
bool sendNotification ( class EmailInfo *ei );

#endif

