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
	void gotList ( ) ;
	bool saveRebalanceFile ( ) ;

	bool m_inRebalanceLoop;
	long m_numForeignRecs;

	long m_rdbNum;
	collnum_t m_collnum;
	char m_nextKey[MAX_KEY_BYTES];
	char m_endKey[MAX_KEY_BYTES];
	bool m_needsRebalanceValid;
	char m_needsRebalance;
	bool m_warnedUser;
	bool m_userApproved;
	bool m_isScanning;
	long m_blocked;

	RdbList m_list;
	SafeBuf m_posMetaList;
	SafeBuf m_negMetaList;
	Msg4 m_msg4a;
	Msg4 m_msg4b;
	Msg5 m_msg5;
};

extern Rebalance g_rebalance;

#endif
