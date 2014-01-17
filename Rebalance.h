// Matt Wells, Jan 2014

#ifndef REBALANCE_H
#define REBALANCE_H

#include "gb-include.h"
#include "types.h"
#include "RdbList.h"
#include "Msg4.h"
#include "Msg5.h"
#include "SafeBuf.h"

class Rebalance {

 public:

	Rebalance();

	char *getNeedsRebalance ( ) ;
	void rebalanceLoop ( ) ;
	void scanLoop ( ) ;
	bool scanRdb ( ) ;
	bool gotList ( ) ;
	bool saveRebalanceFile ( ) ;

	bool m_inRebalanceLoop;
	long m_numForeignRecs;
	long long m_rebalanceCount;
	long long m_scannedCount;

	long m_rdbNum;
	collnum_t m_collnum;
	collnum_t m_lastCollnum;
	class Rdb *m_lastRdb;
	long m_lastPercent;
	char m_nextKey[MAX_KEY_BYTES];
	char m_endKey[MAX_KEY_BYTES];
	bool m_needsRebalanceValid;
	char m_needsRebalance;
	bool m_warnedUser;
	bool m_userApproved;
	bool m_isScanning;
	long m_blocked;
	bool m_allowSave;

	RdbList m_list;
	SafeBuf m_posMetaList;
	SafeBuf m_negMetaList;
	Msg4 m_msg4a;
	Msg4 m_msg4b;
	Msg5 m_msg5;
};

extern Rebalance g_rebalance;

#endif
