// Copyright Gigablast, Inc. Apr 2008

// tight merge indexdb and datedb at the given time every day

#ifndef _DAILYMERGE_H_
#define _DAILYMERGE_H_

#include "gb-include.h"
#include "Collectiondb.h"

class DailyMerge {
public:

	bool init();

	// is the scan active and adding recs to the secondary rdbs?
	bool isMergeActive() { return (m_mergeMode >= 1); };

	void dailyMergeLoop ( ) ;

	CollectionRec *m_cr;
	char           m_mergeMode;
	char           m_spideringEnabled;
	char           m_injectionEnabled;
	char           m_didDaily;
	time_t         m_savedStartTime;
};

// the global class
extern DailyMerge g_dailyMerge;

#endif
