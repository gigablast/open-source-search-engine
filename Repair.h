// Copyright Gigablast, Inc. Mar 2007

#ifndef _REPAIR_H_
#define _REPAIR_H_

#include "RdbList.h"
#include "Msg5.h"
#include "RdbCache.h"
#include "Msg1.h"
#include "Msg4.h"
//#include "Msg23.h"
//#include "LinkText.h"
//#include "Msg14.h"
#include "XmlDoc.h"
//#include "TitleRec.h"
#include "Tagdb.h"

#define SR_BUFSIZE 2048

extern char g_repairMode;
extern bool g_callAllHostsReady;
extern bool g_saveRepairState;

class Repair {
public:

	Repair();

	// is the scan active and adding recs to the secondary rdbs?
	bool isRepairActive() ;

	bool init();
	//void allHostsReady();
	void initScan();
	void resetForNewCollection();
	void getNextCollToRepair();
	bool loop( void *state = NULL );
	bool dumpLoop();
	void resetSecondaryRdbs();
	bool dumpsCompleted();
	void updateRdbs ( ) ;

	// titledbscan functions
	bool scanRecs();
	bool gotScanRecList ( );
	//bool gotTfndbList ( );
	//bool getTagRec ( void **state ) ;
	bool getTitleRec ( );
	bool injectTitleRec ( ) ; // TitleRec *tr );
	//bool computeRecs ( );
	//bool getRootQuality ( );
	//bool addToSpiderdb2 ( ) ; 
	//bool addToTfndb2 ( ); 
	//bool addToChecksumdb2 ( );
	//bool addToClusterdb2 ( );
	//bool addToIndexdb2 ( );
	//bool addToIndexdb2b( );
	//bool addToDatedb2 ( );
	//bool addToTitledb2 ( );
	//bool addToLinkdb2 ( );

	// spiderdb scan functions
	//bool scanSpiderdb ( );
	//bool getTfndbListPart2 ( );
	//bool getTagRecPart2 ( );
	//bool getRootQualityPart2 ( );
	//bool addToSpiderdb2Part2 ( );
	//bool addToTfndb2Part2 ( );

	// called by Pages.cpp
	bool printRepairStatus ( SafeBuf *sb , long fromIp );

	// if we core, call these so repair can resume where it left off
	bool save();
	bool load();

	bool       m_completed;

	// general scan vars
	Msg5       m_msg5;
	Msg5       m_msg5b;
	Msg4       m_msg4;
	bool       m_needsCallback;
	//Msg50      m_msg50;
	char       m_docQuality;
	//Msg14      m_msg14;
	//RdbList    m_scanList;
	RdbList    m_titleRecList;
	long long  m_docId;
	char       m_isDelete;
	RdbList    m_ulist;
	RdbList    m_addlist;
	//long       m_ruleset;
	//LinkTextReply  m_rootLinkText;
	long long  m_totalMem;
	long       m_stage ;
	long       m_tfn;
	long       m_count;
	bool       m_updated;
	//key_t      m_currentTitleRecKey; // for tfndb

	// titledb scan vars
	//key_t      m_nextRevdbKey;
	key_t      m_nextTitledbKey;
	key_t      m_nextSpiderdbKey;
	//key_t      m_nextIndexdbKey;
	key_t      m_nextPosdbKey;
	//key_t      m_nextDatedbKey;
	key128_t   m_nextLinkdbKey;
	//key128_t   m_nextPlacedbKey;
	key_t      m_endKey;
	long long  m_uh48;
	//TitleRec   m_tr;
	//Msg8a      m_msg8a;
	long       m_priority;
	uint64_t   m_contentHash;
	//key_t      m_tfndbKey;
	//char       m_checksumdbKey[32];
	key_t      m_clusterdbKey ;
	key_t      m_spiderdbKey;
	char       m_srBuf[SR_BUFSIZE];
	char       m_tmpBuf[32];
	IndexList *m_newIndexList;
	IndexList *m_dateListToAdd;
	IndexList *m_noSplitList;
	RdbList    m_linkdbListToAdd;
	uint64_t   m_chksum1LongLong;
	XmlDoc     m_doc;

	// spiderdb scan vars
	bool       m_isNew;
	//SpiderRec  m_sr;
	//SiteRec  m_siteRec;
	TagRec     m_tagRec;


	// . state info
	// . indicator of what we save to disk
	char       m_SAVE_START;
	long long  m_lastDocId;
	long long  m_prevDocId;
	bool       m_completedFirstScan  ;
	bool       m_completedSpiderdbScan ;
	//bool     m_completedIndexdbScan  ;
	//key_t      m_lastRevdbKey;
	key_t      m_lastTitledbKey;
	key_t      m_lastSpiderdbKey;

	long long  m_recsScanned;
	long long  m_recsOutOfOrder;
	long long  m_recsetErrors;
	long long  m_recsCorruptErrors;
	long long  m_recsXmlErrors;
	long long  m_recsDupDocIds;
	long long  m_recsNegativeKeys;
	long long  m_recsOverwritten;
	long long  m_recsUnassigned;
	long long  m_noTitleRecs;
	long long  m_recsWrongGroupId;
	long long  m_recsRoot;
	long long  m_recsNonRoot;
	long long  m_recsInjected;
	//long       m_fn;

	// spiderdb scan stats
	long       m_spiderRecsScanned  ;
	long       m_spiderRecSetErrors ;
	long       m_spiderRecNotAssigned ;
	long       m_spiderRecBadTLD      ;

	// generic scan parms
	char       m_rebuildTitledb    ;
	//char       m_rebuildIndexdb    ;
	char       m_rebuildPosdb    ;
	//char       m_rebuildNoSplits   ;
	//char       m_rebuildDatedb     ;
	//char       m_rebuildTfndb      ;
	//char     m_rebuildChecksumdb ;
	char       m_rebuildClusterdb  ;
	char       m_rebuildSpiderdb   ;
	char       m_rebuildSitedb     ;
	char       m_rebuildLinkdb     ;
	char       m_rebuildTagdb      ;
	//char       m_rebuildPlacedb    ;
	//char       m_rebuildSectiondb  ;
	//char       m_rebuildRevdb      ;
	char       m_fullRebuild       ;
	//char       m_removeBadPages    ;

	char       m_rebuildRoots      ;
	char       m_rebuildNonRoots   ;

	// current collection being repaired
	long       m_collLen;
	collnum_t  m_collnum;
	char       m_newColl[MAX_COLL_LEN];
	long       m_newCollLen;
	collnum_t  m_newCollnum;

	// . m_colli is the index into m_colls
	// . m_colli is the index into g_collectiondb.m_recs if the list
	//   of collections to repair was empty
	long       m_colli;

	// list of collections to repair, only valid of g_conf.m_collsToRepair
	// is not empty
	long       m_collOffs[100];
	long       m_collLens[100];
	long       m_numColls;
	// end the stuff to be saved
	char       m_SAVE_END;

	// i'd like to save these but they are ptrs
	char      *m_coll;
	CollectionRec *m_cr;

	//for timing a repair process
	long long  m_startTime;

	// if repairing is disabled in the middle of a repair
	char       m_isSuspended;

	// keep track of how many injects we have out
	long       m_numOutstandingInjects;
	bool       m_allowInjectToLoop;

	// sanity check
	bool  m_msg5InUse ;

	bool  m_saveRepairState;

	bool  m_isRetrying;
};

// the global class
extern Repair g_repair;

#endif
