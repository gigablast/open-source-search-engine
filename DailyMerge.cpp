// Copyright 2008, Gigablast Inc.

#include "gb-include.h"

#include "DailyMerge.h"
#include "Hostdb.h"
#include "Repair.h"
#include "Rdb.h"
#include "Process.h" // g_process.m_processStartTime
#include "Spider.h"
#include "Proxy.h"

static void dailyMergeWrapper ( int fd , void *state ) ;

// the global class
DailyMerge g_dailyMerge;

// main.cpp calls g_dailyMerge.init()
bool DailyMerge::init ( ) {
	// reset these
	m_cr        = NULL;
	m_mergeMode = 0;
	m_didDaily  = false;
	// check every 10 seconds
	if( ! g_loop.registerSleepCallback(10*1000,NULL,dailyMergeWrapper ) )
		return log("repair: Failed register callback.");
	return true;
}

// . call this once every second 
// . this is responsible for advancing from one g_repairMode to the next
void dailyMergeWrapper ( int fd , void *state ) {
	g_dailyMerge.dailyMergeLoop();
}

void DailyMerge::dailyMergeLoop ( ) {
	// disable for now!
	//return;
	// if in repair mode, do not do daily merge
	if ( g_repairMode ) return;
	// or if in read only mode
	if ( g_conf.m_readOnlyMode ) return;
	// skip if proxy, a proxy can be hostid 0!
	if ( g_proxy.isProxy() ) return;
	// wait for clock to be synced with host #0
	if ( ! isClockInSync() ) return;
	// get local time
	int64_t nowLocalMS = gettimeofdayInMillisecondsLocal();
	// get our hostid
	int32_t hid = g_hostdb.m_myHost->m_hostId;
	// if process only recently started (1 min ago or less)
	// then do not immediately do this...
	if (hid==0 && nowLocalMS - g_process.m_processStartTime < 1*60*1000)
		return;
	// wait until the right time (this is in UTC)
	time_t nowSynced = getTimeSynced();

	// get time since midnight
	struct tm *tt ;
	// how many MINUTES into the day are we? (in UTC)
	tt = gmtime ( &nowSynced );
	int32_t elapsedMins = tt->tm_hour * 60 + tt->tm_min ;

	// what collnum to merge?
	collnum_t i ;

	// . if we are not 0, just use host #0's collnum
	// . an error here will screw up the whole daily merge process
	if ( hid != 0 && m_mergeMode == 0 ) {
		// get host #0
		Host *h = &g_hostdb.m_hosts[0];
		// must have got a ping reply from him
		if ( ! h->m_gotPingReply ) return;
		// hostid #0 must NOT be in mode 0
		if ( h->m_pingInfo.m_flags & PFLAG_MERGEMODE0 ) return;
		// get the collnum that host #0 is currently daily merging
		i = g_hostdb.m_hosts[0].m_pingInfo.m_dailyMergeCollnum;
		// this means host #0 is not daily merging a collnum now
		if ( i < 0 ) return;
		// if it is valid, the CollectionRec MUST be there
		CollectionRec *cr = g_collectiondb.getRec ( i );
		if ( ! cr ) { 
			log("daily: host #0 bad collnum %"INT32"",(int32_t)i);return;}
		// if valid, use it
		m_cr = cr;
		// we set m_cr, go to next mode
		m_mergeMode = 1;
		// set the start time here, but don't commit to m_cr just yet
		m_savedStartTime = nowSynced;
	}

	// . only host #0 should do this loop!!!
	// . loop through each collection to check the time
	for (i=0; hid==0&&m_mergeMode==0 && i<g_collectiondb.m_numRecs; i++) {
		// get collection rec for collnum #i
		CollectionRec *cr = g_collectiondb.getRec ( i );
		// skip if empty, it was deleted at some point
		if ( ! cr ) continue;
		// skip if daily merge trigger is < 0 (do not do dailies)
		if ( cr->m_dailyMergeTrigger < 0 ) continue;
		// . skip if not time yet
		// . !!!!!THIS IS IN MINUTES!!!!!!!!
		if ( (int32_t)elapsedMins < (int32_t)cr->m_dailyMergeTrigger ) 
			continue;
		// do not start more than 15 mins after the trigger time,
		// if we miss that cuz we are down, then too bad
		if ( (int32_t)elapsedMins > (int32_t)cr->m_dailyMergeTrigger + 15 )
			continue;
 		// . how long has it been (in seconds)
		// . !!!!!THIS IS IN SECONDS!!!!!!!!
		int32_t diff = nowSynced - cr->m_dailyMergeStarted;
		// crazy?
		if ( diff < 0 ) continue;
		// if less than 24 hours ago, we already did it
		if ( diff < 24*3600 ) continue;
		// . we must now match the day of week
		// . use <= 0 to do it every day
		// . 0 = sunday ... 6 = saturday
		// . comma separated list is ok ("0,1, 6")
		// . leave blank or at least no numbers to do every day
		char *s = cr->m_dailyMergeDOWList;
		char dowCounts[8];
		memset(dowCounts,0,8);
		for ( ; *s ; s++ ) {
			if ( ! is_digit(*s) ) continue;
			int32_t num = atoi(s);
			if ( num < 0 ) continue;
			if ( num > 6 ) continue;
			dowCounts[num]++;
		}
		// get our dow
		int32_t todayDOW = tt->tm_wday + 1;
		// make sure 1 to 7
		if ( todayDOW < 0 || todayDOW > 6 ) { 
			log("merge: bad today dow of %i for coll %s",
			    (int)todayDOW,cr->m_coll);
			return;
		}
		//if ( todayDOW > 6 ) { char *xx=NULL;*xx=0; }
		// skip if not a dayofweek to merge on
		if ( dowCounts [ todayDOW ] == 0 ) continue;

		// set the start time here, but don't commit to m_cr just yet
		m_savedStartTime = nowSynced;
		// . wait for everyone to be in mode #0 in case they just
		//   finished another daily merge. only host #0 does this loop.
		// . PROBLEM: if host #0 crashes before everyone can get into 
		//   mode 1+ and then host #0 is brought back up, then 
		//   obviously, we will not be able to meet this condition,
		//   therefore only check to see if this condition is 
		//   satisfied our "second time around" (so we must complete
		//   one daily merge before checking this again). that is why
		//   i added "m_didDaily". -- MDW
		for ( int32_t i = 0 ; m_didDaily && i<g_hostdb.m_numHosts ; i++){
			// skip ourselves, obviously we are in merge mode 2
			if ( &g_hostdb.m_hosts[i] == g_hostdb.m_myHost )
				continue;
			// that's good if he is in mode 0
			if ( g_hostdb.m_hosts[i].m_pingInfo.m_flags & 
			     PFLAG_MERGEMODE0 )
				continue;
			// oops, someone is not mode 0
			return;
		}
		// got one, save it
		m_cr = cr;
		// if we were hostid 0, go into merge mode 1 now
		m_mergeMode = 1;
		// bust out of loop
		break;
	}

	// can we advance to merge mode 1?
	if ( m_mergeMode == 1 ) {
		// no candidates, go back to mode 0 now, we are done
		if ( ! m_cr ) {
			log("daily: Could not get coll rec.");
			m_mergeMode = 0; return; 
		}
		// ok, we got a collection that needs it so turn off spiders
		m_mergeMode = 2;
		// turn spiders off to keep query latency down
		m_spideringEnabled = g_conf.m_spideringEnabled;
		//m_injectionEnabled = g_conf.m_injectionEnabled;
		g_conf.m_spideringEnabled = false;
		//g_conf.m_injectionEnabled = false;
		// log it
		log("daily: Starting daily merge for %s.",m_cr->m_coll);
		log("daily: Waiting for other hosts to enter merge mode.");
	}

	// wait for everyone to make it to mode 1+ before going on
	if ( m_mergeMode == 2 ) {
		// check the ping packet flags
		for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
			// get the host
			Host *h = &g_hostdb.m_hosts[i];
			// skip ourselves, obviously we are in merge mode 2
			if ( h == g_hostdb.m_myHost ) 
				continue;
			// skip dead hosts
			if ( g_hostdb.isDead(h) )
				continue;
			// return if a host still in merge mode 0. wait for it.
			if ( h->m_pingInfo.m_flags & PFLAG_MERGEMODE0 )
				return;
		}
		// ok, everyone is out of mode 0 now
		m_mergeMode = 3;
		// log it
		log("daily: Waiting for all hosts to have 0 "
		    "spiders out.");
	}

	// wait for ALL spiders in network to clear
	if ( m_mergeMode == 3 ) {
		// return if we got spiders out!
		if ( g_spiderLoop.m_numSpidersOut > 0 )
			return;
		// check the ping packet flags
		for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
			// skip ourselves, obviously we are in merge mode 2
			if ( &g_hostdb.m_hosts[i] == g_hostdb.m_myHost )
				continue;
			// if host still has spiders out, we can't go to mode 4
			if ( g_hostdb.m_hosts[i].m_pingInfo.m_flags & 
			     PFLAG_HASSPIDERS ) 
				return;
		}
		// ok, nobody has spiders now
		m_mergeMode = 4;
		// log it
		log("daily: Dumping trees.");
	}

	// start the dumps
	if ( m_mergeMode == 4 ) {
		// . set when we did it last, save that to disk to avoid thrash
		// . TODO: BUT do not allow it to be set in the spider 
		//   controls!
		// . THIS IS IN SECONDS!!!!!!!
		// . use the time we started, otherwise the merge time keeps
		//   getting pushed back.
		m_cr->m_dailyMergeStarted = m_savedStartTime; // nowSynced;
		// tell it to save, otherwise this might not get saved
		m_cr->m_needsSave = true;
		// initiate dumps
		g_indexdb.getRdb  ()->dumpTree(1); // niceness = 1
		//g_datedb.getRdb   ()->dumpTree(1); // niceness = 1
		g_spiderdb.getRdb ()->dumpTree(1); // niceness = 1
		g_linkdb.getRdb   ()->dumpTree(1); // niceness = 1
		// if neither has recs in tree, go to next mode
		if(g_indexdb .getRdb()->getNumUsedNodes()>0) return;
		//if(g_datedb  .getRdb()->getNumUsedNodes()>0) return;
		if(g_spiderdb.getRdb()->getNumUsedNodes()>0) return;
		if(g_linkdb  .getRdb()->getNumUsedNodes()>0) return;
		// ok, all trees are clear and dumped
		m_mergeMode = 5;
		// log it
		log("daily: Merging indexdb and datedb files.");
	}

	// start the merge
	if ( m_mergeMode == 5 ) {
		// kick off the merges if not already going
		//g_indexdb.getRdb()->attemptMerge(1,true,false);
		//g_datedb .getRdb()->attemptMerge(1,true,false);
		// if has more than one file, bail on it
		RdbBase *base;

		base = g_indexdb .getRdb()->getBase(m_cr->m_collnum);
		// . niceness,forced?,doLog?,minFilesToMerge
		// . only does a merge if there are 2 or more "big" indexdb 
		//   files present. Merges so that there are LESS THAN 2 files.
		//   just another way of describing a tight merge.
		base->attemptMerge (1,true,false,2);
		if ( base->getNumFiles() >= 2 ) return;

		//base = g_datedb  .getRdb()->getBase(m_cr->m_collnum);
		//base->attemptMerge (1,true,false,2);
		//if ( base->getNumFiles() >= 2 ) return;

		base = g_spiderdb.getRdb()->getBase(m_cr->m_collnum);
		base->attemptMerge (1,true,false,2);
		if ( base->getNumFiles() >= 2 ) return;

		base = g_linkdb  .getRdb()->getBase(m_cr->m_collnum);
		base->attemptMerge (1,true,false,2);
		if ( base->getNumFiles() >= 2 ) return;

		// . minimize titledb merging at spider time, too
		// . will perform a merge IFF there are 200 or more titledb 
		//   files present, otherwise, it will not. will do the merge
		//   such that LESS THAN 200 titledb files will be present
		//   AFTER the merge is completed.
		// . do NOT force merge ALL files on this one, we just want
		//   to make sure there are not 200+ titledb files
		base = g_titledb .getRdb()->getBase(m_cr->m_collnum);
		// we seem to dump about 70 per day at a decent spider rate
		// so merge enough so that we don't have to merge while 
		// spidering
		base->attemptMerge (1,false,false,230-70);
		if ( base->getNumFiles() >= 230-70 ) return;

		// set m_cr to NULL up here, so that the last guy to
		// complete the daily merge, does not "cycle back" and
		// try to re-daily merge the same collection!
		m_cr = NULL;
		// ok, merges are done
		m_mergeMode = 6;
		// log it
		log("daily: Waiting for all hosts to finish merging.");
	}

	// wait for all to finish before re-enabling spiders
	if ( m_mergeMode == 6 ) {
		// check the ping packet flags
		for ( int32_t i = 0 ; i < g_hostdb.m_numHosts ; i++ ) {
			// skip ourselves, obviously we are ok
			if ( &g_hostdb.m_hosts[i] == g_hostdb.m_myHost )
				continue;
			// if host in mode 6 or 0, that's good
			if ( g_hostdb.m_hosts[i].m_pingInfo.m_flags & 
			     PFLAG_MERGEMODE0OR6)
				continue;
			// otherwise, wait for it to be in 6 or 0
			return;
		}
		// ok, nobody has spiders now, everyone is 6 or 0
		m_mergeMode = 0;
		// no coll rec now
		m_cr = NULL;
		// spiders back on
		g_conf.m_spideringEnabled = m_spideringEnabled;
		//g_conf.m_injectionEnabled = m_injectionEnabled;
		// log it
		log("daily: Daily merge completed.");
		// now the next time we do a daily we must make sure all hosts
		// are in merge mode #0 before we start
		m_didDaily  = true;
	}		
}
