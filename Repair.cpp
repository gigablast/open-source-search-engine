// Copyright 2007, Gigablast Inc.

#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500

#include "gb-include.h"

#include "Repair.h"
#include "Rdb.h"
#include "Spider.h"
#include "Msg1.h"
//#include "Datedb.h"
#include "Pages.h"
#include "PingServer.h"
#include "Spider.h"
#include "Process.h"
#include "Tagdb.h"
//#include "Placedb.h"
#include "Sections.h"
//#include "Revdb.h"
//#include "Tfndb.h"

static void repairWrapper ( int fd , void *state ) ;
static void loopWrapper   ( void *state , RdbList *list , Msg5 *msg5 ) ;
//static void loopWrapper2  ( void *state );
//static void loopWrapper3  ( void *state );
//static void injectCompleteWrapper ( void *state );

static bool saveAllRdbs ( void *state , void (* callback)(void *state) ) ;
static bool anyRdbNeedsSave ( ) ;
static void doneSavingRdb ( void *state );

char g_repairMode = 0;

// the global class
Repair g_repair;

Rdb **getSecondaryRdbs ( int32_t *nsr ) {
	static Rdb *s_rdbs[50];
	static int32_t s_nsr = 0;
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		s_nsr = 0;
		//s_rdbs[s_nsr++] = g_tfndb2.getRdb      ();
		s_rdbs[s_nsr++] = g_titledb2.getRdb    ();
		//s_rdbs[s_nsr++] = g_indexdb2.getRdb    ();
		s_rdbs[s_nsr++] = g_posdb2.getRdb    ();
		//s_rdbs[s_nsr++] = g_datedb2.getRdb     ();
		s_rdbs[s_nsr++] = g_spiderdb2.getRdb   ();
		s_rdbs[s_nsr++] = g_clusterdb2.getRdb  ();
		s_rdbs[s_nsr++] = g_linkdb2.getRdb     ();
		s_rdbs[s_nsr++] = g_tagdb2.getRdb      ();
		//s_rdbs[s_nsr++] = g_placedb2.getRdb    ();
		//s_rdbs[s_nsr++] = g_sectiondb2.getRdb  ();
		//s_rdbs[s_nsr++] = g_revdb2.getRdb      ();
	}
	*nsr = s_nsr;
	return s_rdbs;
}

Rdb **getAllRdbs ( int32_t *nsr ) {
	static Rdb *s_rdbs[50];
	static int32_t s_nsr = 0;
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		s_nsr = 0;
		//s_rdbs[s_nsr++] = g_tfndb.getRdb      ();
		s_rdbs[s_nsr++] = g_titledb.getRdb    ();
		//s_rdbs[s_nsr++] = g_indexdb.getRdb    ();
		s_rdbs[s_nsr++] = g_posdb.getRdb    ();
		//s_rdbs[s_nsr++] = g_datedb.getRdb     ();
		s_rdbs[s_nsr++] = g_spiderdb.getRdb   ();
		s_rdbs[s_nsr++] = g_clusterdb.getRdb  ();
		s_rdbs[s_nsr++] = g_linkdb.getRdb     ();
		s_rdbs[s_nsr++] = g_tagdb.getRdb      ();
		//s_rdbs[s_nsr++] = g_placedb.getRdb    ();
		//s_rdbs[s_nsr++] = g_sectiondb.getRdb  ();
		//s_rdbs[s_nsr++] = g_revdb.getRdb      ();

		//s_rdbs[s_nsr++] = g_tfndb2.getRdb      ();
		s_rdbs[s_nsr++] = g_titledb2.getRdb    ();
		//s_rdbs[s_nsr++] = g_indexdb2.getRdb    ();
		s_rdbs[s_nsr++] = g_posdb2.getRdb    ();
		//s_rdbs[s_nsr++] = g_datedb2.getRdb     ();
		s_rdbs[s_nsr++] = g_spiderdb2.getRdb   ();
		s_rdbs[s_nsr++] = g_clusterdb2.getRdb  ();
		s_rdbs[s_nsr++] = g_linkdb2.getRdb     ();
		s_rdbs[s_nsr++] = g_tagdb2.getRdb      ();
		//s_rdbs[s_nsr++] = g_placedb2.getRdb    ();
		//s_rdbs[s_nsr++] = g_sectiondb2.getRdb  ();
		//s_rdbs[s_nsr++] = g_revdb2.getRdb      ();
	}
	*nsr = s_nsr;
	return s_rdbs;
}


Repair::Repair() {
}

// main.cpp calls g_repair.init()
bool Repair::init ( ) {
	//logf(LOG_DEBUG,"repair: TODO: alloc s_docs[] on demand to save mem");
	m_msg5InUse       = false;
	m_isSuspended     = false;
	m_saveRepairState = false;
	m_isRetrying      = false;
	m_needsCallback   = false;
	m_completed       = false;
	if( ! g_loop.registerSleepCallback( 1 , NULL , repairWrapper ) )
		return log("repair: Failed register callback.");
	return true;
}

bool Repair::isRepairActive() { 
	return g_repairMode >= 4; 
}

// . call this once every second 
// . this is responsible for advancing from one g_repairMode to the next
void repairWrapper ( int fd , void *state ) {

	g_errno = 0;

	// . all hosts should have their g_conf.m_repairMode parm set
	// . it is global now, not collection based, since we need to
	//   lock down titledb for the scan and there could be recs from
	//   the collection we are repairing in titledb's rdbtree, which,
	//   when dumped, would mess up our scan.
	if ( ! g_conf.m_repairingEnabled ) return;

	// if the power went off
	if ( ! g_process.m_powerIsOn ) return;

	// if it got turned back on after being suspended, start where
	// we left off, this is how we re-enter Repair::loop()
	if ( g_repair.m_isSuspended && g_repairMode == 4 ) {
		// unsuspend it
		g_repair.m_isSuspended = false;
		// note it
		log("repair: Resuming repair scan after suspension.");
		// try to read another title rec, or whatever
		g_repair.loop();
		return;
	}

	// if we are in retry mode
	if ( g_repair.m_isRetrying && g_repairMode == 4 ) {
		// reset it
		g_repair.m_isRetrying = false;
		// try to read another title rec, or whatever
		g_repair.loop();
		return;
	}

	//
	// ok, repairing is enabled at this point
	//

	// are we just starting?
	if ( g_repairMode == 0 ) {
		// turn spiders off since repairing is enabled
		g_conf.m_spideringEnabled = false;
		//g_conf.m_injectionEnabled = false;
		// wait for a previous repair to finish?
		//if ( g_pingServer.getMinRepairMode() != 0 ) return;
		// if some are not done yet with the previous repair, wait...
		// no because we are trying to load up repair.dat
		//if ( g_pingServer.getMaxRepairMode() == 8 ) return;

		g_repair.m_startTime = gettimeofdayInMilliseconds();
		// enter repair mode level 1
		g_repairMode = 1;
		// note it
		log("repair: Waiting for all writing operations to stop.");
	}

	// we can only enter repairMode 2 once all "writing" has stopped
	if ( g_repairMode == 1 ) {
		// wait for all merging to stop just to be on the safe side
		if ( g_merge.isMerging () ) return;
		if ( g_merge2.isMerging() ) return;
		// this is >= 0 is correct, -1 means no outstanding spiders
		if ( g_spiderLoop.m_maxUsed >= 0 ) return;
		// wait for ny outstanding unlinks or renames to finish
		if ( g_unlinkRenameThreads > 0 ) return;
		// . make sure all Msg4s are done and have completely added all
		//   recs they were supposed to
		// . PROBLEM: if resuming a repair after re-starting, we can
		//   not turn on repairing
		// . SOLVED: saveState() for msg4 uses different filename
		if ( hasAddsInQueue() ) return;
		// . ok, go to level 2
		// . we can only get to level *3* once PingServer.cpp sees 
		//   that all hosts in the cluster are in level 2. that way we
		//   guarantee not to add or delete any recs from any rdb, 
		//   because that could damage the repair. PingServer will 
		//   call g_repair.allHostsRead() when they all report they 
		//   have a repair mode of 2.
		g_repairMode = 2;
		// note it
		log("repair: All oustanding writing operations stopped. ");
		log("repair: Waiting for all other hosts to stop, too.");
	}

	// we can only enter mode 3 once all hosts are in 2 or higher
	if ( g_repairMode == 2 ) {
		// we are still waiting on some guy if this is <= 1
		if ( g_pingServer.getMinRepairMode() <= 1 ) return;
		// wait for others to sync clocks, lest xmldoc cores when
		// it calls getTimeGlobal() like in getNewTagBuf()
		if ( ! isClockInSync() ) return;
		// . this will return true if everything is saved to disk that
		//   needs to be, otherwise false if waiting on an rdb to finish
		//   saving
		// . do this after all hosts are done writing, otherwise
		//   they might add data to our rdbs!
		if ( ! saveAllRdbs ( NULL , NULL ) ) return;
		// note it
		//log("repair: Initializing the new Rdbs and scan parameters.");
		// reset scan info BEFORE calling Repair::load()
		g_repair.resetForNewCollection();
		// before calling loop for the first time, init the scan,
		// this will block and only return when it is done
		g_repair.initScan();
		// on error this sets g_repairingEnabled to false
		if ( ! g_conf.m_repairingEnabled ) return;
		// save "addsinprogress" file now so that the file will be 
		// saved as essentially an empty file at this point. 
		saveAddsInProgress ( NULL );
		// sanity check
		//char *xx = NULL; *xx = 0;
		// hey, everyone is done "writing"
		g_repairMode = 3;
		// not eit
		log("repair: All data saved and clock synced.");
		log("repair: Waiting for all hosts to save and sync clocks.");
	}

	if ( g_repairMode == 3 ) {
		// wait for others to save everything
		if ( g_pingServer.getMinRepairMode() <= 2 ) return;
		// start the loop
		log("repair: All hosts saved.");
		log("repair: Loading repair-addsinprogress.dat");
		// . tell Msg4 to load state using the new filename now
		// . load "repair-addsinprogress" file
		loadAddsInProgress ( "repair-" );
		//log("repair: Scanning titledb file #%"INT32".",  g_repair.m_fn );
		log("repair: Starting repair scan.");
		// advance
		g_repairMode = 4;
		// now start calling the loop. returns false if blocks
		if ( ! g_repair.loop() ) return;
	}

	// we can only enter mode 4 once we have completed the repairs
	// and have dumped all the in-memory data to disk
	if ( g_repairMode == 4 ) {
		// special case
		if ( g_repair.m_needsCallback ) {
			// only do once
			g_repair.m_needsCallback = false;
			// note it in log
			log("repair: calling needed callback for msg4");
			// and call the loop then. returns false if blocks..
			if ( ! g_repair.loop() ) return;
		}
		// wait for scan loops to complete
		if ( ! g_repair.m_completedFirstScan  ) return;
		if ( ! g_repair.m_completedSpiderdbScan ) return;
		// note it
		log("repair: Scan completed.");
		log("repair: Waiting for other hosts to complete scan.");
		// ok, we are ready to update the data files
		g_repairMode = 5;
	}		

	// we can only enter mode 5 once all hosts are in 4 or higher
	if ( g_repairMode == 5 ) {
		// if add queues still adding, wait, otherwise they will not
		// be able to add to our rebuild collection
		if ( hasAddsInQueue() ) return;
		// note it
		log("repair: All adds have been flushed.");
		log("repair: Waiting for all other hosts to flush out their "
		    "add operations.");
		// update repair mode
		g_repairMode = 6;
	}

	if ( g_repairMode == 6 ) {
		// wait for everyone to get to mode 6 before we dump, otherwise
		// data might arrive in the middle of the dumping and it stays
		// in the in-memory RdbTree!
		if ( g_pingServer.getMinRepairMode() < 6 ) return;
		// do not dump if we are doing a full rebuild or a
		// no split list rebuild -- why?
		//if(! g_repair.m_fullRebuild && ! g_repair.m_rebuildNoSplits){
		//if ( ! g_repair.m_rebuildNoSplits ) {
		// we might have to dump again
		g_repair.dumpLoop();
		// are we done dumping?
		if ( ! g_repair.dumpsCompleted() ) return;
		//}
		// wait for all merging to stop just to be on the safe side
		if ( g_merge.isMerging () ) return;
		if ( g_merge2.isMerging() ) return;
		// wait for ny outstanding unlinks or renames to finish
		if ( g_unlinkRenameThreads > 0 ) return;
		// note it
		log("repair: Final dump completed.");
		log("repair: Updating rdbs to use newly repaired data.");
		// everyone is ready
		g_repairMode = 7;
	}

	// we can only enter mode 6 once we are done updating the original
	// rdbs with the rebuilt/repaired data. we move the old rdb data files
	// into the trash and replace it with the new data.
	if ( g_repairMode == 7 ) {
		// wait for autosave...
		if ( g_process.m_mode ) return; // = SAVE_MODE;		
		// save to disk so it zeroes out indexdbRebuild-saved.dat
		// which should have 0 records in it cuz we dumped it above
		// in g_repair.dumpLoop()
		if ( ! saveAllRdbs ( NULL , NULL ) ) return;
		// . this blocks and gets the job done
		// . this will move the old *.dat and *-saved.dat files into
		//   a subdir in the trash subdir
		// . it will rename the rebuilt files to remove the "Rebuild"
		//   from their filenames
		// . it will then restart the primary rdbs using those newly
		//   rebuilt and renamed files
		// . this will not allow itself to be called more than once
		//   per scan/repair process
		g_repair.updateRdbs();
		// note this
		log("repair: resetting secondary rdbs.");
		// . only do this after indexdbRebuild-saved.dat has had a
		//   chance to save to "zero-out" its file on disk
		// . all done with these guys, free their mem
		g_repair.resetSecondaryRdbs();
		// save "repair-addsinprogress" now so that the file will 
		// be saved as essentially an empty file at this 
		// point. 
		saveAddsInProgress ( "repair-" );
		// reset it again in case it gets saved again later
		g_repair.resetForNewCollection();
		// unlink the repair.dat file, in case we core and are unable
		// to save the freshly-reset repair.dat file
		log("repair: unlinking repair.dat");
		char tmp[1024];
		sprintf ( tmp, "%s/repair.dat", g_hostdb.m_dir );
		::unlink ( tmp );
		// do not save it again! we just unlinked it!!
		g_repair.m_saveRepairState = false;
		// note it
		log("repair: Waiting for other hosts to complete update.");
		// ready to reset
		g_repairMode = 8;
		// mark it
		g_repair.m_completed = true;
	}

	// go back to 0 once all hosts do not equal 5
	if ( g_repairMode == 8 ) {
		// nobody can be in 7 (they might be 0!)
		if ( g_pingServer.getMinRepairModeBesides0() != 8 ) return;

		// note it
		log("repair: Exiting repair mode.  took %"INT64" ms", 
		    gettimeofdayInMilliseconds() - g_repair.m_startTime);
		// turn it off to prevent going back to mode 1 again
		g_conf.m_repairingEnabled = false;
		// ok reset
		g_repairMode = 0;
	}
}

		
void Repair::resetForNewCollection ( ) {
	m_stage                 = 0;
	m_lastDocId             = 0;
	m_prevDocId             = 0;
	m_completedFirstScan  = false;
	m_completedSpiderdbScan = false;
	//m_completedIndexdbScan  = false;
}

// . PingServer.cpp will call this g_repair.allHostsReady() when all hosts
//   have completely stopped spidering and merging
// . returns false if blocked, true otherwise
//void Repair::allHostsReady () {
void Repair::initScan ( ) {

	// reset some stuff for the titledb scan
	//m_nextRevdbKey.setMin ();
	m_nextTitledbKey.setMin();
	m_nextSpiderdbKey.setMin();
	m_lastSpiderdbKey.setMin();
	//m_nextIndexdbKey.setMin ();
	m_nextPosdbKey.setMin ();
	//m_nextDatedbKey.setMin  ();
	m_nextLinkdbKey.setMin  ();
	//m_nextPlacedbKey.setMin ();
	m_endKey.setMax();
	m_titleRecList.reset();
	//m_fn    = 0;
	m_count = 0;

	// all Repair::updateRdbs() to be called
	m_updated = false;

	// titledb scan stats
	m_recsScanned      = 0;
	m_recsNegativeKeys  = 0;
	m_recsOutOfOrder   = 0;
	m_recsetErrors     = 0;
	m_recsCorruptErrors = 0;
	m_recsXmlErrors     = 0;
	m_recsDupDocIds     = 0;
	m_recsOverwritten  = 0;
	m_recsUnassigned   = 0;
	m_recsWrongGroupId = 0;
	m_noTitleRecs = 0;

	m_spiderRecsScanned     = 0;
	m_spiderRecSetErrors    = 0;
	m_spiderRecNotAssigned  = 0;
	m_spiderRecBadTLD       = 0;

	m_rebuildTitledb    = g_conf.m_rebuildTitledb;
	//m_rebuildIndexdb    = g_conf.m_rebuildIndexdb;
	m_rebuildPosdb      = g_conf.m_rebuildPosdb;
	//m_rebuildNoSplits   = g_conf.m_rebuildNoSplits;
	//m_rebuildDatedb     = g_conf.m_rebuildDatedb;
	//m_rebuildTfndb      = g_conf.m_rebuildTfndb;
	//m_rebuildChecksumdb = g_conf.m_rebuildChecksumdb;
	m_rebuildClusterdb  = g_conf.m_rebuildClusterdb;
	m_rebuildSpiderdb   = g_conf.m_rebuildSpiderdb;
	m_rebuildLinkdb     = g_conf.m_rebuildLinkdb;
	//m_rebuildTagdb      = g_conf.m_rebuildTagdb;
	//m_rebuildPlacedb    = g_conf.m_rebuildPlacedb;
	//m_rebuildSectiondb  = g_conf.m_rebuildSectiondb;
	//m_rebuildRevdb      = g_conf.m_rebuildRevdb;
	m_fullRebuild       = g_conf.m_fullRebuild;
	//m_removeBadPages    = g_conf.m_removeBadPages;

	m_rebuildRoots      = g_conf.m_rebuildRoots;
	m_rebuildNonRoots   = g_conf.m_rebuildNonRoots;

	m_numOutstandingInjects = 0;


	// we call Msg14::injectUrl() directly and that will add to ALL the
	// necessary secondary rdbs automatically
	if ( m_fullRebuild ) {
		// why rebuild titledb? its the base. no we need to
		// rebuild it for new event displays.
		m_rebuildTitledb    = true;//false; 
		//m_rebuildTfndb      = true;//false;
		m_rebuildSpiderdb   = false;
		//m_removeBadPages    = false;
		//m_rebuildIndexdb    = true;
		m_rebuildPosdb    = true;
		//m_rebuildNoSplits   = true;
		//m_rebuildDatedb     = true;
		m_rebuildClusterdb  = true;
		m_rebuildLinkdb     = true;
		//m_rebuildTagdb      = true;
		//m_rebuildPlacedb    = true;
		//m_rebuildSectiondb  = true;
		//m_rebuildRevdb      = true;
	}

	// no supported right now
	//m_rebuildTfndb = false;
	// . what does it mean to rebuild titledb?
	// . we need to rebuild titledb so the eventdisplays are updated!
	//   they have the title descriptions etc.!!
	//m_rebuildTitledb = false;
	// never rebuild this for now, we'll lose our firstips...
	//m_rebuildTagdb = false;
	// don't rebuild placedb because i think we add to it from titlerecs
	// that we do not store into titledb... yeah we only store the
	// title rec in XmlDoc.cpp if we got valid events...
	//m_rebuildPlacedb = false;
	// and sectiondb votes are added for root urls even if they don't
	// have a valid event, and a title rec... so we can't really build
	// it just from title recs either... maybe from revdb recs?
	//m_rebuildSectiondb = false;
	

	// . if rebuilding tfndb, only do that...
	// . because rebuilding titledb requires that we do a 
	//   lookup on tfndb to see if the title rec we got is
	//   the real deal, i.e. from the correct tfn, which 
	//   is stored in the tfndb rec. otherwise, it is an 
	//   older version of the same url probably.
	/*
	if ( m_rebuildTfndb ) {
		m_rebuildTitledb    = false;
		m_rebuildIndexdb    = false;
		//m_rebuildNoSplits   = false;
		m_rebuildDatedb     = false;
		//m_rebuildChecksumdb = false;
		m_rebuildClusterdb  = false;
		m_rebuildSpiderdb   = false;
		m_rebuildLinkdb     = false;
		m_rebuildTagdb      = false;
		m_rebuildPlacedb    = false;
		m_rebuildSectiondb  = false;
		m_rebuildRevdb      = false;
		m_fullRebuild       = false;
		//m_removeBadPages    = false;
	}
	*/

	/*
	// only this can be on by itself
	if ( m_rebuildNoSplits ) {
		m_rebuildTitledb    = false;
		m_rebuildIndexdb    = false;
		m_rebuildDatedb     = false;
		m_rebuildTfndb      = false;
		//m_rebuildChecksumdb = false;
		m_rebuildClusterdb  = false;
		m_rebuildSpiderdb   = false;
		m_rebuildLinkdb     = false;
		m_rebuildTagdb      = false;
		m_rebuildPlacedb    = false;
		m_rebuildSectiondb  = false;
		m_rebuildRevdb      = false;
		m_fullRebuild       = false;
		m_removeBadPages    = false;
	}
	*/

	/*
	// i forgot what this was for!
	if ( m_removeBadPages ) {
		m_rebuildTitledb    = false;
		m_rebuildIndexdb    = false;
		//m_rebuildNoSplits   = false;
		m_rebuildDatedb     = false;
		m_rebuildTfndb      = false;
		//m_rebuildChecksumdb = false;
		m_rebuildClusterdb  = false;
		m_rebuildSpiderdb   = false;
		//m_rebuildSitedb     = false;
		m_rebuildLinkdb     = false;
		m_rebuildTagdb      = false;
		m_rebuildPlacedb    = false;
		m_rebuildSectiondb  = false;
		m_rebuildRevdb      = false;
		m_fullRebuild       = false;
	}
	*/

	// force reverse indexdb rebuild if you are changing 
	// of these dbs
	/*
	if ( m_rebuildIndexdb   ||
	     m_rebuildDatedb    ||
	     m_rebuildClusterdb ||
	     m_rebuildLinkdb    ||
	     m_rebuildPlacedb   ||
	     m_rebuildSectiondb )
		m_rebuildRevdb = true;
	*/

	// rebuilding spiderdb means we must rebuild tfndb, too
	if ( m_rebuildSpiderdb ) {
		logf(LOG_DEBUG,"repair: Not rebuilding tfndb like "
		     "we should because it is broken!");
		// TODO: put this back when it is fixed!
		// see the comment in addToTfndb2() below
		// YOU HAVE TO REBUILD spiderdb first then rebuild
		// tfndb when that is done...
		//m_rebuildTfndb = true;
	}

	// rebuilding titledb means we must rebuild tfndb, which means
	// we must rebuild spiderdb, too!
	//if ( m_rebuildTitledb  ) {
	//	//m_rebuildTfndb    = true;
	//	m_rebuildSpiderdb = true;
	//}

	// . set the list of ptrs to the collections we have to repair
	// . should be comma or space separated in g_conf.m_collsToRepair
	// . none listed means to repair all collections
	char *s    = g_conf.m_collsToRepair.getBufStart();
	char *cbuf = g_conf.m_collsToRepair.getBufStart();
	char emptyStr[1]; emptyStr[0] = '\0';
	if ( ! s    ) s    = emptyStr;
	if ( ! cbuf ) cbuf = emptyStr;
	// reset the list of ptrs to colls to repair
	m_numColls = 0;
	// scan through the collections in the string, if there are any
 collLoop:
	// skip non alnum chars
	while ( *s && !is_alnum_a(*s) ) s++;
	// if not at the end of the string, grab the collection
	if ( *s ) {
		m_collOffs[m_numColls] = s - cbuf;
		// hold it
		char *begin = s;
		// find the length
		while ( *s && *s != ',' && !is_wspace_a(*s) ) s++;
		// store that, too
		m_collLens[m_numColls] = s - begin;
		// advance the number of collections
		m_numColls++;
		// get the next collection if under 100 collections still
		if ( m_numColls < 100 ) goto collLoop;
	}

	// split the mem we have available among the rdbs
	m_totalMem = g_conf.m_repairMem;
	// 30MB min
	if ( m_totalMem < 30000000 ) m_totalMem = 30000000;

	//
	// try to get some more mem. 
	//

	// weight factors
	float weight = 0;
	if ( m_rebuildTitledb    ) weight += 100.0;
	//if ( m_rebuildTfndb      ) weight +=   1.0;
	//if ( m_rebuildIndexdb    ) weight += 100.0;
	if ( m_rebuildPosdb      ) weight += 100.0;
	//if ( m_rebuildDatedb     ) weight +=  80.0;
	if ( m_rebuildClusterdb  ) weight +=   1.0;
	//if ( m_rebuildChecksumdb ) weight +=   1.0;
	if ( m_rebuildSpiderdb   ) weight +=   5.0;
	if ( m_rebuildLinkdb     ) weight +=  20.0;
	if ( m_rebuildTagdb      ) weight +=   5.0;
	//if ( m_rebuildPlacedb    ) weight +=  20.0;
	//if ( m_rebuildSectiondb  ) weight +=   5.0;
	//if ( m_rebuildRevdb      ) weight +=  80.0;
	// assign memory based on weight
	int32_t titledbMem    = 0;
	//int32_t tfndbMem      = 0;
	//int32_t indexdbMem    = 0;
	int32_t posdbMem    = 0;
	//int32_t datedbMem     = 0;
	int32_t clusterdbMem  = 0;
	//int32_t checksumdbMem = 0;
	int32_t spiderdbMem   = 0;
	int32_t linkdbMem     = 0;
	//int32_t tagdbMem      = 0;
	//int32_t placedbMem    = 0;
	//int32_t sectiondbMem  = 0;
	//int32_t revdbMem      = 0;
	float tt = (float)m_totalMem;
	if ( m_rebuildTitledb    ) titledbMem    = (int32_t)((100.0 * tt)/weight);
	//if(m_rebuildTfndb      ) tfndbMem      = (int32_t)((  1.0 * tt)/weight);
	// HACK FIX CORE:
	//if ( m_rebuildTfndb      ) tfndbMem      = 100*1024*1024;
	//if(m_rebuildIndexdb    ) indexdbMem    = (int32_t)((100.0 * tt)/weight);
	if ( m_rebuildPosdb      ) posdbMem    = (int32_t)((100.0 * tt)/weight);
	//if(m_rebuildDatedb     ) datedbMem     = (int32_t)(( 80.0 * tt)/weight);
	if ( m_rebuildClusterdb  ) clusterdbMem  = (int32_t)((  1.0 * tt)/weight);
	//if(m_rebuildChecksumdb ) checksumdbMem = (int32_t)((  1.0 * tt)/weight);
	if ( m_rebuildSpiderdb   ) spiderdbMem   = (int32_t)((  5.0 * tt)/weight);
	if ( m_rebuildLinkdb     ) linkdbMem     = (int32_t)(( 20.0 * tt)/weight);
	//if ( m_rebuildTagdb    ) tagdbMem      = (int32_t)((  5.0 * tt)/weight);
	//if(m_rebuildPlacedb    ) placedbMem    = (int32_t)(( 20.0 * tt)/weight);
	//if(m_rebuildSectiondb  ) sectiondbMem  = (int32_t)((  5.0 * tt)/weight);
	//if(m_rebuildRevdb      ) revdbMem      = (int32_t)(( 80.0 * tt)/weight);

	// debug hack
	//posdbMem = 10000000;


	if ( m_numColls <= 0 ) {
		log("rebuild: Rebuild had no collection specified. You need "
		    "to enter a collection or list of collections.");
		goto hadError;
	}

	
	// init secondary rdbs
	if ( m_rebuildTitledb ) {
		if ( ! g_titledb2.init2    ( titledbMem    ) ) goto hadError;
		// clean tree in case loaded from saved file
		Rdb *r = g_titledb2.getRdb();
		if ( r ) r->m_tree.cleanTree();
	}

	//if ( m_rebuildTfndb )
	//	if ( ! g_tfndb2.init2      ( tfndbMem      ) ) goto hadError;
	//if ( m_rebuildIndexdb )
	//	if ( ! g_indexdb2.init2    ( indexdbMem    ) ) goto hadError;
	if ( m_rebuildPosdb ) {
		if ( ! g_posdb2.init2    ( posdbMem    ) ) goto hadError;
		// clean tree in case loaded from saved file
		Rdb *r = g_posdb2.getRdb();
		if ( r ) r->m_buckets.cleanBuckets();
	}



	//if ( m_rebuildDatedb )
	//	if ( ! g_datedb2.init2     ( datedbMem     ) ) goto hadError;
	if ( m_rebuildClusterdb )
		if ( ! g_clusterdb2.init2  ( clusterdbMem  ) ) goto hadError;
	//if ( m_rebuildChecksumdb )
	//	if ( ! g_checksumdb2.init2 ( checksumdbMem ) ) goto hadError;
	if ( m_rebuildSpiderdb )
		if ( ! g_spiderdb2.init2   ( spiderdbMem   ) ) goto hadError;
	//if ( m_rebuildSitedb )
	//	if ( ! g_tagdb2.init2     ( spiderdbMem   ) ) goto hadError;
	if ( m_rebuildLinkdb )
		if ( ! g_linkdb2.init2     ( linkdbMem     ) ) goto hadError;
	//if ( m_rebuildTagdb )
	//	if ( ! g_tagdb2.init2      ( tagdbMem    ) ) goto hadError;
	//if ( m_rebuildPlacedb )
	//	if ( ! g_placedb2.init2    ( placedbMem    ) ) goto hadError;
	//if ( m_rebuildSectiondb )
	//	if ( ! g_sectiondb2.init2  ( sectiondbMem    ) ) goto hadError;
	//if ( m_rebuildRevdb )
	//	if ( ! g_revdb2.init2      ( revdbMem    ) ) goto hadError;

	g_errno = 0;

	// reset current coll we are repairing
	//m_coll  = NULL;
	m_colli = -1;
	m_completedFirstScan  = false;

	// . tell it to advance to the next collection
	// . this will call addColl() on the appropriate Rdbs
	// . it will call addColl() on the primary rdbs for m_fullRebuild
	getNextCollToRepair();

	// if could not get any, bail
	if ( ! m_cr ) goto hadError;

	g_errno = 0;

	// load the old repair state if on disk, this will block
	load();
	// now we can save if we need to
	m_saveRepairState = true;
	// if error loading, ignore it
	g_errno = 0;

	//if ( ! loop() ) return;
	// was there an error
	//if ( g_errno ) log("repair: loop: %s.",mstrerror(g_errno));
	return;

	// on any init2() error, reset all and return true
 hadError:
	int32_t saved = g_errno;
	// all done with these guys
	resetSecondaryRdbs();
	// pull back g_errno
	g_errno = saved;
	// note it
	log("repair: Had error in repair init. %s. Exiting.",
	    mstrerror(g_errno));
	// back to step 0
	g_repairMode = 0;
	// a mode of 5 means we are done repairing and waiting to go 
	// back to mode 0, but only PingServer.cpp will only set our 
	// mode to 0 once it has verified all other hosts are in 
	// mode 5 or 0.
	//g_repairMode = 5;
	// reset current coll we are repairing
	//m_coll  = NULL;
	m_colli = -1;
	g_conf.m_repairingEnabled = false;
	
	return;
}

// . sets m_coll/m_collLen to the next collection to repair
// . sets m_coll to NULL when none are left (we are done)
void Repair::getNextCollToRepair ( ) {
	// . advance index into collections
	// . can be index into m_colls or into g_collectiondb
	m_colli++;
	// ptr to first coll
	if ( m_numColls ) {
		if ( m_colli >= m_numColls ) {
			//m_coll = NULL;
			//m_collLen = 0;
			return;
		}
		char *buf = g_conf.m_collsToRepair.getBufStart();
		char *coll    = buf + m_collOffs [m_colli];
		int collLen = m_collLens[m_colli];
		m_cr = g_collectiondb.getRec (coll, collLen);
		// if DNE, set m_coll to NULL to stop repairing
		if ( ! m_cr ) { g_errno = ENOCOLLREC; return; }
	}
	// otherwise, we are repairing every collection by default
	else {
		m_cr = NULL;
		// loop m_colli over all the possible collnums
		while ( ! m_cr && m_colli < g_collectiondb.m_numRecs )
			m_cr = g_collectiondb.m_recs [ ++m_colli ];
		if ( ! m_cr ) {
			//m_coll = NULL;
			//m_collLen = 0;
			g_errno = ENOCOLLREC;
			return;
		}
		//m_coll    = m_cr->m_coll;
		//m_collLen = m_cr->m_collLen;
	}

	// collection cannot be deleted while we are in repair mode...
	m_collnum = m_cr->m_collnum;

	log("repair: now rebuilding for collection '%s' (%i)"
	    , m_cr->m_coll
	    , (int)m_collnum
	    );

	/*
	if ( m_fullRebuild ) {
		// set the new collection name...
		m_newCollLen = sprintf(m_newColl,"%sRebuild",m_coll);
		// . add the new collection
		// . copy parms from m_coll
		if ( ! g_collectiondb.addRec ( m_newColl ,
					       m_coll    ,
					       m_collLen ,
					       true      , // isNew?
					       -1        , // collnum
					       false     , // isdump?
					       true      ) && // save it?
		     // if already there, just keep going
		     g_errno != EEXIST )// is dump?
			goto hadError;
		// assign m_newCollnum as well
		m_newCollnum = g_collectiondb.getCollnum ( m_newColl );
		CollectionRec *newRec = g_collectiondb.m_recs[m_newCollnum];
		// turn off link spidering on the new coll so we do
		// not add more than what is there
		newRec->m_spiderLinks = false;
		// increase min files to merge
		newRec->m_indexdbMinFilesToMerge = 100;
		newRec->m_datedbMinFilesToMerge = 100;
		newRec->m_spiderdbMinFilesToMerge = 100;
		//newRec->m_checksumdbMinFilesToMerge = 100;
		newRec->m_clusterdbMinFilesToMerge = 100;
		// do we use this one?
		newRec->m_linkdbMinFilesToMerge = 10;
		// and USUALLY we don't want to re-dedup...
		// it wastes a ton of disk seeks especially with indexdb
		// with so many files!
		newRec->m_dedupingEnabled = false;
		newRec->m_dupCheckWWW     = false;
		return;
	}
	*/

	char *coll = m_cr->m_coll;

	// add collection to secondary rdbs
	if ( m_rebuildTitledb ) {
		if ( //! g_titledb2.addColl    ( m_coll ) &&
		    ! g_titledb2.getRdb()->addRdbBase1(coll) &&
		     g_errno != EEXIST ) goto hadError;
	}

	//if ( m_rebuildTfndb ) {
	//	if ( ! g_tfndb2.addColl      ( coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}

	//if ( m_rebuildIndexdb ) {
	//	if ( ! g_indexdb2.addColl    ( coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}

	if ( m_rebuildPosdb ) {
		if ( ! g_posdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	//if ( m_rebuildDatedb ) {
	//	if ( ! g_datedb2.addColl     ( coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}

	if ( m_rebuildClusterdb ) {
		if ( ! g_clusterdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	//if ( m_rebuildChecksumdb ) {
	//	if ( ! g_checksumdb2.addColl ( coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}

	if ( m_rebuildSpiderdb ) {
		if ( ! g_spiderdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	//if ( m_rebuildSitedb ) {
	//	if ( ! g_tagdb2.addColl     ( coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}

	if ( m_rebuildLinkdb ) {
		if ( ! g_linkdb2.getRdb()->addRdbBase1 ( coll ) &&
		     g_errno != EEXIST ) goto hadError;
	}

	//if ( m_rebuildTagdb ) {
	//	if ( ! g_tagdb2.addColl     ( m_coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}
	//if ( m_rebuildPlacedb ) {
	//	if ( ! g_placedb2.addColl     ( m_coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}
	//if ( m_rebuildSectiondb ) {
	//	if ( ! g_sectiondb2.addColl     ( m_coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}
	//if ( m_rebuildRevdb ) {
	//	if ( ! g_revdb2.addColl     ( m_coll ) &&
	//	     g_errno != EEXIST ) goto hadError;
	//}

	return;

 hadError:
	// note it
	log("repair: Had error getting next coll to repair: %s. Exiting.",
	    mstrerror(g_errno));
	// a mode of 5 means we are done repairing and waiting to go back to
	// mode 0, but only PingServer.cpp will only set our mode to 0 once 
	// it has verified all other hosts are in mode 5 or 0.
	//g_repairMode = 5;
	return;
}


void loopWrapper ( void *state , RdbList *list , Msg5 *msg5 ) {
	Repair *THIS = (Repair *)state;
	THIS->m_msg5InUse = false;
	THIS->loop(NULL);
}

void loopWrapper2 ( void *state ) {
	g_repair.loop(NULL);
}

//void loopWrapper3 ( void *state ) {
//	//Repair *THIS = (Repair *)state;
//	// this hold "tr" in one case
//	g_repair.loop(state);
//}


enum {
	STAGE_TITLEDB_0  = 0 ,
	STAGE_TITLEDB_1      ,
	STAGE_TITLEDB_2      ,
	STAGE_TITLEDB_3      ,
	STAGE_TITLEDB_4      ,
	/*
	STAGE_TITLEDB_5      ,
	STAGE_TITLEDB_6      ,
	*/
	STAGE_SPIDERDB_0     
	/*
	STAGE_SPIDERDB_1     ,
	STAGE_SPIDERDB_2A    ,
	STAGE_SPIDERDB_2B    ,
	STAGE_SPIDERDB_3     ,
	STAGE_SPIDERDB_4     ,

	STAGE_INDEXDB_0      ,
	STAGE_INDEXDB_1      ,
	STAGE_INDEXDB_2      ,

	STAGE_DATEDB_0       ,
	STAGE_DATEDB_1       ,
	STAGE_DATEDB_2      
	*/
};

bool Repair::save ( ) {
	// do not do a blocking save for auto save if
	// we never entere repair mode
	if ( ! m_saveRepairState ) return true;
	// log it
	log("repair: saving repair.dat");
	char tmp[1024];
	sprintf ( tmp , "%s/repair.dat", g_hostdb.m_dir );
	File ff;
	ff.set ( tmp );
	if ( ! ff.open ( O_RDWR | O_CREAT | O_TRUNC ) )
		return log("repair: Could not open %s : %s",
			   ff.getFilename(),mstrerror(g_errno));
	// first 8 bytes are the size of the DATA file we're mapping
	g_errno = 0;
	int32_t      size   = &m_SAVE_END - &m_SAVE_START;
	int64_t offset = 0LL;
	ff.write ( &m_SAVE_START , size , offset ) ;
	ff.close();
	return true;
}

bool Repair::load ( ) {

	char tmp[1024];
	sprintf ( tmp , "%s/repair.dat", g_hostdb.m_dir );
	File ff;
	ff.set ( tmp );

	logf(LOG_INIT,"repair: Loading %s to resume repair.",tmp);

	if ( ! ff.open ( O_RDONLY ) )
		return log("repair: Could not open %s : %s",
			   ff.getFilename(),mstrerror(g_errno));
	// first 8 bytes are the size of the DATA file we're mapping
	g_errno = 0;
	int32_t      size   = &m_SAVE_END - &m_SAVE_START;
	int64_t offset = 0LL;
	ff.read ( &m_SAVE_START, size , offset ) ;
	ff.close();

	// resume titledb scan?
	//m_nextRevdbKey  = m_lastRevdbKey;
	m_nextTitledbKey = m_lastTitledbKey;
	// resume spiderdb scan?
	m_nextSpiderdbKey = m_lastSpiderdbKey;

	// reinstate the valuable vars
	m_cr   = g_collectiondb.m_recs [ m_collnum ];
	//m_coll = m_cr->m_coll;


	m_stage = STAGE_TITLEDB_0;
	if ( m_completedFirstScan  ) m_stage = STAGE_SPIDERDB_0;
	//if ( m_completedSpiderdbScan ) m_stage = STAGE_INDEXDB_0;

	//m_isSuspended = true;

	// HACK FORCE FOR BUZZ
	// point to offset of collection we are rebuilding
	// . "main" collection
	// . offset into g_conf.m_collsToRepair
	//m_collOffs[0] = 0;
	//m_collLens[0] = 4; 
	//m_numColls    = 1;

	return true;
}


// . this is the main repair loop
// . this is repsonsible for calling all the repair functions
// . all repair callbacks given come back into this loop
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool Repair::loop ( void *state ) {
	m_allowInjectToLoop = false;

	// if the power went off
	if ( ! g_process.m_powerIsOn ) {
		// sleep 1 second and retry
		m_isRetrying = true;
		return true;
	}

	// was repairing turned off all of a sudden?
	if ( ! g_conf.m_repairingEnabled ) {
		//log("repair: suspending repair.");
		// when it gets turned back on, the sleep callback above
		// will notice it was suspended and call loop() again to
		// resume where we left off...
		m_isSuspended = true;
		return true;
	}

	// if we re-entered this loop from doneWithIndexDocWrapper
	// do not launch another msg5 if it is currently out!
	if ( m_msg5InUse ) return false;

	// set this to on
	g_process.m_repairNeedsSave = true;

	// . titledb scan
	// . build g_checksumdb2, g_spiderdb2, g_clusterdb2, g_tfndb2
 loop1:

	if ( g_process.m_mode == EXIT_MODE )
		return true;

	if ( m_stage == STAGE_TITLEDB_0  ) {
		m_stage++;
		if ( ! scanRecs()       ) return false;
	}
	if ( m_stage == STAGE_TITLEDB_1  ) {
		m_stage++;
		if ( ! gotScanRecList()   ) return false;
	}
	if ( m_stage == STAGE_TITLEDB_2  ) {
		m_stage++;
		// skip this for now
		//if ( ! gotTfndbList()      ) return false;
		// get title rec for revdb. if none, then we'll just
		// re-add this revdb rec into the new revdb RDB2_REVDB2
		//if ( ! getTitleRec() ) return false;
	}
	// get the site rec to see if it is banned first, before injecting it
	if ( m_stage == STAGE_TITLEDB_3 ) {
		// if we have maxed out our injects, wait for one to come back
		if ( m_numOutstandingInjects >= g_conf.m_maxRepairSpiders ) {
			m_allowInjectToLoop = true;
			return false;
		}
		m_stage++;
		// BEGIN NEW STUFF
		bool status = injectTitleRec();
		//return false; // (state)
		// try to launch another
		if ( m_numOutstandingInjects<g_conf.m_maxRepairSpiders ) {
			m_stage = STAGE_TITLEDB_0;
			goto loop1;
		}
		// if we are full and it blocked... wait now
		if ( ! status ) return false;
	}
	if ( m_stage == STAGE_TITLEDB_4  ) {
		m_stage++;
		//if ( ! addToTfndb2()       ) return false;
	}

	// if we are not done with the titledb scan loop back up
	if ( ! m_completedFirstScan ) {
		m_stage = STAGE_TITLEDB_0;
		goto loop1;
	}

	// if we are waiting for injects to come back, return
	if ( m_numOutstandingInjects > 0 ) {
		// tell injection complete wrapper to call us back, otherwise
		// we never end up moving on to the spider phase
		g_repair.m_allowInjectToLoop = true;
		return false;
	}

	// reset list
	//m_list.reset();

	// . spiderdb scan
	// . put new spider recs into g_spiderdb2
	/*
 loop2:
	if ( m_stage == STAGE_SPIDERDB_0 ) {
		m_stage++;
		if ( ! scanSpiderdb()     ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_1 ) {
		m_stage++;
		if ( ! getTfndbListPart2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_2A ) {
		m_stage++;
		if ( ! getTagRecPart2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_2B ) {
		m_stage++;
		if ( ! getRootQualityPart2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_3 ) {
		m_stage++;
		if ( ! addToSpiderdb2Part2()  ) return false;
	}
	if ( m_stage == STAGE_SPIDERDB_4 ) {
		m_stage++;
		if ( ! addToTfndb2Part2()  ) return false;
	}
	// if we are not done with the titledb scan loop back up
	if ( ! m_completedSpiderdbScan ) {
		m_stage = STAGE_SPIDERDB_0;
		goto loop2;
	}
	*/

	// reset list
	m_titleRecList.reset();

	// . indexdb scan
	// . delete indexdb recs whose docid is not in tfndb
	// . delete duplicate docid in same termlist docids
	// . turn this off for now to get buzz ready faster
	/*
 loop3:
	if ( m_stage == STAGE_INDEXDB_0 ) {
		m_stage++;
		if ( ! scanIndexdb()      ) return false;
	}
	if ( m_stage == STAGE_INDEXDB_1 ) {
		m_stage++;
		if ( ! gotIndexRecList()  ) return false;
	}
	if ( m_stage == STAGE_INDEXDB_2 ) {
		m_stage++;
		if ( ! addToIndexdb2()    ) return false;
	}
	// if we are not done with the titledb scan loop back up
	if ( ! m_completedIndexdbScan ) {
		m_stage = STAGE_INDEXDB_0;
		goto loop3;
	}
	*/

	// in order for dump to work we must be in mode 4 because
	// Rdb::dumpTree() checks that
	g_repairMode = 4;

	// force dump to disk of the newly rebuilt rdbs, because we need to
	// make sure their trees are empty when the primary rdbs assume
	// the data and map files of the secondary rdbs. i don't want to
	// have to mess with tree data as well.

	// if we do not complete the dump here it will be monitored above
	// in the sleep wrapper, repairWrapper(), and that will call 
	// Repair::loop() (this function) again when the dump is done
	// and we will be able to advance passed this m_stage
	// . dump the trees of all secondary rdbs that need it
	//dumpLoop();
	// are we done dumping?
	//if ( ! dumpsCompleted() ) return false;
	
	// we are all done with the repair loop
	return true;
}

// this blocks
void Repair::updateRdbs ( ) {

	if ( m_updated ) return;

	// do not double call
	m_updated = true;

	// . we can only perform the update once every host is in mode 5
	// . a host can only go to mode 5 once every host has gone to mode 4
	//if ( g_hostdb.m_maxRepairMode < 5 ) return false;

	// . replace old rdbs with the new ones
	// . these calls must all block otherwise things will get out of sync
	Rdb *rdb1;
	Rdb *rdb2;

	if ( m_rebuildTitledb ) {
		rdb1 = g_titledb.getRdb ();
		rdb2 = g_titledb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	//if ( m_rebuildTfndb ) {
	//	rdb1 = g_tfndb.getRdb();
	//	rdb2 = g_tfndb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	//if ( m_rebuildIndexdb ) {
	//	rdb1 = g_indexdb.getRdb();
	//	rdb2 = g_indexdb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	if ( m_rebuildPosdb ) {
		rdb1 = g_posdb.getRdb();
		rdb2 = g_posdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	//if ( m_rebuildDatedb ) {
	//	rdb1 = g_datedb.getRdb();
	//	rdb2 = g_datedb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	if ( m_rebuildClusterdb ) {
		rdb1 = g_clusterdb.getRdb();
		rdb2 = g_clusterdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	//if ( m_rebuildChecksumdb ) {
	//	rdb1 = g_checksumdb.getRdb();
	//	rdb2 = g_checksumdb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	if ( m_rebuildSpiderdb ) {
		rdb1 = g_spiderdb.getRdb();
		rdb2 = g_spiderdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}
	//if ( m_rebuildSitedb ) {
	//	rdb1 = g_tagdb.getRdb();
	//	rdb2 = g_tagdb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	if ( m_rebuildLinkdb ) {
		rdb1 = g_linkdb.getRdb();
		rdb2 = g_linkdb2.getRdb();
		rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	}

	//if ( m_rebuildTagdb ) {
	//	rdb1 = g_tagdb.getRdb();
	//	rdb2 = g_tagdb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	//if ( m_rebuildPlacedb ) {
	//	rdb1 = g_placedb.getRdb();
	//	rdb2 = g_placedb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	//if ( m_rebuildSectiondb ) {
	//	rdb1 = g_sectiondb.getRdb();
	//	rdb2 = g_sectiondb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}
	//if ( m_rebuildRevdb ) {
	//	rdb1 = g_revdb.getRdb();
	//	rdb2 = g_revdb2.getRdb();
	//	rdb1->updateToRebuildFiles ( rdb2 , m_cr->m_coll );
	//}

	// reset scan info
	//resetForNewCollection();

	// all done with these guys, free their mem
	//resetSecondaryRdbs();

	/*
	// now go to the next collection
	//getNextCollToRepair();
	m_coll = NULL;
	m_colli = -1;
	g_conf.m_repairingEnabled = false;

	// if we got another collection, repair/rebuild it now
	if ( m_coll ) {
		// back to mode 3
		g_repairMode = 3;
		// and scan titledb for this coll
		goto loop1;
	}

	// all done with these guys, free their mem
	//resetSecondaryRdbs();

	// note it
	log("repair: Repairs completed. Exiting repair mode.");

	// a mode of 5 means we are done repairing and waiting to go back to
	// mode 0, but only PingServer.cpp will only set our mode to 0 once 
	// it has verified all other hosts are in mode 5 or 0.
	g_repairMode = 5;

	// . all done for good
	// . return true because we did not block this caller
	return true;
	*/
}

void Repair::resetSecondaryRdbs ( ) {
	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// use niceness of 1
		rdb->reset();
	}
}

bool Repair::dumpLoop ( ) {
	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// don't dump tfndb...
		if ( rdb->m_rdbId == RDB2_TFNDB2 ) continue;
		// use niceness of 1
		rdb->dumpTree ( 1 );
	}
	g_errno = 0;
	// . register sleep wrapper to check when dumping is done
	// . it will call Repair::loop() when done
	return false;
}

bool Repair::dumpsCompleted ( ) {
	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// we don't dump tfndb...
		if ( rdb->m_rdbId == RDB2_TFNDB2 ) continue;
		// anything in tree/buckets?
		if ( rdb->getNumUsedNodes() ) return false;
		// still dumping?
		if ( rdb->isDumping      () ) return false;
	}
	// no more dump activity
	return true;
}


// . this is only called from repairLoop()
// . returns false if blocked, true otherwise
// . grab the next scan record
bool Repair::scanRecs ( ) {
	// just the tree?
	//int32_t nf          = 1;
	//bool includeTree = false;
	RdbBase *base = g_titledb.getRdb()->getBase ( m_collnum );
	//if ( m_fn == base->getNumFiles() ) { nf = 0; includeTree = true; }
	// always clear last bit of g_nextKey
	m_nextTitledbKey.n0 &= 0xfffffffffffffffeLL;
	// for saving
	m_lastTitledbKey = m_nextTitledbKey;
	log(LOG_DEBUG,"repair: nextKey=%s endKey=%s"
	    "coll=%s collnum=%"INT32" "
	    "bnf=%"INT32"",//fn=%"INT32" nf=%"INT32"",
	    KEYSTR(&m_nextTitledbKey,sizeof(key_t)),
	    KEYSTR(&m_endKey,sizeof(key_t)),
	    m_cr->m_coll,
	    (int32_t)m_collnum,
	    (int32_t)base->getNumFiles());//,m_fn,nf);
	// sanity check
	if ( m_msg5InUse ) {
		char *xx = NULL; *xx = 0; }
	// when building anything but tfndb we can get the rec
	// from the twin in case of data corruption on disk
	bool fixErrors = true;
	//if ( m_rebuildTfndb ) fixErrors = false;
	// get the list of recs
	g_errno = 0;
	if ( m_msg5.getList ( RDB_TITLEDB        ,
			      m_collnum           ,
			      &m_titleRecList      ,
			      m_nextTitledbKey   ,
			      m_endKey         , // should be maxed!
			      1024             , // min rec sizes
			      true             , // include tree?
			      false            , // includeCache
			      false            , // addToCache
			      0                , // startFileNum
			      -1               , // m_numFiles   
			      this             , // state 
			      loopWrapper      , // callback
			      MAX_NICENESS     , // niceness
			      fixErrors        , // do error correction?
			      NULL             , // cache key ptr
			      0                , // retry num
			      -1               , // maxRetries
			      true             , // compensate for merge
			      -1LL             , // sync point
			      &m_msg5b         ))
		return true;
	m_msg5InUse = true;
	return false;
}


// . this is only called from repairLoop()
// . returns false if blocked, true otherwise
bool Repair::gotScanRecList ( ) {

	QUICKPOLL(MAX_NICENESS);

	// get the base
	//RdbBase *base = g_titledb.getRdb()->getBase ( m_collnum );

	if ( g_errno == ECORRUPTDATA ) {
		log("repair: Encountered corruption1 in titledb. "
		    "NextKey=%s",
		    KEYSTR(&m_nextTitledbKey,sizeof(key_t)));
		/*
		// get map for this file
		RdbMap  *map  = base->getMap(m_fn);
		// what page has this key?
		int32_t page = map->getPage ( (char *)&m_nextTitledbKey );
		// advance the page number
	advancePage:
		page++;
		// if no more pages, we are done!
		if ( page >= map->getNumPages() ) {
			log("repair: No more pages in rdb map, done with "
			    "titledb file.");
			g_errno = 0; m_recsCorruptErrors++;
			goto fileDone;
		}
		// get key from that page
		key_t next = *(key_t *)map->getKeyPtr ( page );
		// keep advancing if its the same key!
		if ( next == m_nextTitledbKey ) goto advancePage;
		// ok, we got a new key, use it
		m_nextTitledbKey = next;
		*/
		// get the docid
		//int64_t dd = g_titledb.getDocIdFromKey(&m_nextTitledbKey);
		// inc it
		//dd++;
		// re-make key
		//m_nextTitledbKey = g_titledb.makeFirstTitleRecKey ( dd );
		// advance one if positive, must always start on a neg
		if ( (m_nextTitledbKey.n0 & 0x01) == 0x01 ) 
			m_nextTitledbKey += (uint32_t)1;
		// count as error
		m_recsCorruptErrors++;
	}

	// was there an error? list will probably be empty
	if ( g_errno ) {
		log("repair: Got error reading title rec: %s.",
		    mstrerror(g_errno));
		// keep retrying, might be OOM
		m_stage = STAGE_TITLEDB_0 ;
		// sleep 1 second and retry
		m_isRetrying = true;
		// exit the loop code, Repair::loop() will be re-called
		return false;
	}
		
	/*
	// a hack
	if ( m_count > 100 ) { // && m_fn == 0 ) {
		logf(LOG_INFO,"repair: hacking titledb complete.");
		//m_completedFirstScan = true;
		//m_stage = STAGE_SPIDERDB_0;
		m_list.reset();
		//return true;
	}
	*/

	// all done with this bigfile if this list is empty
	if ( m_titleRecList.isEmpty() ) { //||m_recsScanned > 10 ) {
		// note it
		//logf(LOG_INFO,"repair: Scanning ledb file #%"INT32".",  m_fn );
		m_completedFirstScan = true;
		logf(LOG_INFO,"repair: Completed titledb scan of "
		     "%"INT64" records.",m_recsScanned);
		//logf(LOG_INFO,"repair: Starting spiderdb scan.");
		m_stage = STAGE_SPIDERDB_0;
		// force spider scan completed now too!
		m_completedSpiderdbScan = true;
		g_repair.m_allowInjectToLoop = true;
		return true;
	}

	// nextRec2:
	key_t tkey = m_titleRecList.getCurrentKey();
	int64_t docId = g_titledb.getDocId ( &tkey );
	// save it
	//m_currentTitleRecKey = tkey;

	// save it
	m_docId = docId;
	// is it a delete?
	m_isDelete = false;
	// we need this to compute the tfndb key to add/delete
	//m_ext = -1;
	m_uh48 = 0LL;

	// count the title recs we scan
	m_recsScanned++;

	// skip if bad... CORRUPTION
	if ( tkey < m_nextTitledbKey ) {
		log("repair: Encountered corruption2 in titledb. "
		    "key=%s < NextKey=%s"
		    "FirstDocId=%"UINT64".",
		    //p1-1,
		    KEYSTR(&tkey,sizeof(key_t)),
		    KEYSTR(&m_nextTitledbKey,sizeof(key_t)),
		    docId);
		m_nextTitledbKey += (uint32_t)1;
		// advance one if positive, must always start on a negative key
		if ( (m_nextTitledbKey.n0 & 0x01) == 0x01 ) 
			m_nextTitledbKey += (uint32_t)1;
		m_stage = STAGE_TITLEDB_0;
		return true;
	}
	else {
		// advance m_nextTitledbKey to get next titleRec
		m_nextTitledbKey = m_titleRecList.getCurrentKey();
		m_nextTitledbKey += (uint32_t)1;
		// advance one if positive, must always start on a negative key
		if ( (m_nextTitledbKey.n0 & 0x01) == 0x01 ) 
			m_nextTitledbKey += (uint32_t)1;
	}

	// are we the host this url is meant for?
	//uint32_t gid = getGroupId ( RDB_TITLEDB , &tkey );
	uint32_t shardNum = getShardNum (RDB_TITLEDB , &tkey );
	if ( shardNum != getMyShardNum() ) {
		m_recsWrongGroupId++;
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	// . if one of our twins is responsible for it...
	// . is it assigned to us? taken from assigendToUs() in SpiderCache.cpp
	// . get our group from our hostId
	int32_t  numHosts;
	//Host *hosts = g_hostdb.getGroup ( g_hostdb.m_groupId, &numHosts);
	Host *hosts = g_hostdb.getShard ( shardNum , &numHosts );
	int32_t  ii =  docId % numHosts ;
	// . are we the host this url is meant for?
	// . however, if you are rebuilding tfndb, each twin must scan all
	//   title recs and make individual entries for those title recs
	if ( hosts[ii].m_hostId != g_hostdb.m_hostId ){//&&!m_rebuildTfndb ) {
		m_recsUnassigned++;
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	/*
	// is the list from the tree in memory?
	int32_t id2;
	if ( m_fn == base->getNumFiles() ) id2 = 255;
	else                               id2 = base->m_fileIds2[m_fn];

	// that is the tfn...
	m_tfn = id2;
	*/

	// is it a negative titledb key?
	if ( (tkey.n0 & 0x01) == 0x00 ) {
		// count it
		m_recsNegativeKeys++;
		// otherwise, we need to delete this
		// docid from tfndb...
		m_isDelete = true;
	}

	// if not rebuilding tfndb, skip this
	//if ( ! m_rebuildTfndb && m_isDelete ) {
	if ( m_isDelete ) {
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	return true;
}
	/*
	// if rebuilding tfndb only, always add this to tfndb
	if ( m_rebuildTfndb && ! m_isDelete ) {
		// get raw rec from list
		char *rec     = m_list.getCurrentRec();
		// use this first
		m_doc.reset();
		//int32_t  recSize = m_list.getCurrentRecSize();
		//TitleRec *tr = m_doc.getTitleRec();
		if ( ! m_doc.set2 ( rec, -1, m_coll, NULL, MAX_NICENESS ) ) {
			m_recsetErrors++;
			m_stage = STAGE_TITLEDB_0; // 0
			return true;
		}
		// remember this
		m_prevDocId   = m_docId;
		// set the titleRec we got
		//if ( ! tr->set ( rec , recSize , false  ) ) {
		//	m_recsetErrors++;
		//	m_stage = STAGE_TITLEDB_0; // 0
		//	return true;
		//}
		Url *fu = m_doc.getFirstUrl();
		// set the extended hash, m_ext
		//m_ext = g_tfndb.makeExt ( fu ); // tr->getUrl() );
		m_uh48 = hash64b(fu->getUrl()) & 0x0000ffffffffffffLL;
		// addToTfndb2()
		//m_stage = STAGE_TITLEDB_6;
		m_stage = STAGE_TITLEDB_4;
		return true;
	}

	// if previous titledb key was positive and had the
	// same docid as us, then this negative key probably has
	// different "content hash" bits and is meant to delete a
	// previous version of this titlerec.
	if ( m_rebuildTfndb && m_prevDocId == m_docId ) {
		// just ignore it
		m_stage = STAGE_TITLEDB_0;
		return true;
	}

	// assume normal tfndb lookup
	char rdbId = RDB_TFNDB;

	// if a negative titledb key, then we need to lookup in the 
	// REBUILT tfndb to see/ what the ext hash bits are so we 
	// can delete that key from the new rebuilt tfndb! these
	// hash bits are not in the title rec key unfortunately
	if ( m_rebuildTfndb && m_isDelete )
		rdbId = RDB2_TFNDB2;

	//
	// look up this docid in tfndb
	//

	// . make the keys for getting recs from tfndb
	// . url recs map docid to the title file # that contains the titleRec
	key_t uk1 ;
	key_t uk2 ;
	// . if docId was explicitly specified...
	// . we may get multiple tfndb recs
	// . for this we know the docid, so get it exactly
	uk1 = g_tfndb.makeMinKey ( docId );
	uk2 = g_tfndb.makeMaxKey ( docId );

	// sanity check
	if ( m_msg5InUse ) {
		char *xx = NULL; *xx = 0; }
	// . get the list of url recs for this docid range
	// . this should not block, tfndb SHOULD all be in memory all the time
	// . use 500 million for min recsizes to get all in range
	// . no, using 500MB causes problems for RdbTree::getList, so use
	//   100k. how many recs can there be?
	if ( m_msg5.getList ( rdbId             , // RDB_TFNDB
			      m_coll            ,
			      &m_ulist          ,
			      uk1               , // startKey
			      uk2               , // endKey
			      // use 0x7fffffff preceisely because it
			      // will determine eactly how long the
			      // tree list needs to allocate in Msg5.cpp
			      0x7fffffff        , // minRecSizes
			      true              , // includeTree?
			      false             , // addToCache?
			      0                 , // max cache age
			      0                 , // startFileNum
			      -1                , // numFiles (-1 =all)
			      this              ,
			      loopWrapper       ,
			      MAX_NICENESS      ,
			      true              ))// error correction?
		return true;
	m_msg5InUse = true;
	return false;
}


// . if no recs in the list have a matching tfn, skip the title rec
// . if one matches and one does not, skip the title rec
// . if has multiple and all match that is ok
bool Repair::gotTfndbList ( ) {
	// was there an error? list will probably be empty
	if ( g_errno ) 
		log("repair: Got error reading tfndb list: %s.",
		    mstrerror(g_errno));
	// sanity check
	if ( m_rebuildTfndb && ! m_isDelete ) { char *xx=NULL;*xx=0;}
	// just in case
	m_ulist.resetListPtr();
	// did we have a matchf or our docid?
	bool matched = false;
	// check for our docid
	// we may have multiple tfndb recs but we should NEVER have to read
	// multiple titledb files...
	for ( ; ! m_ulist.isExhausted() ; m_ulist.skipCurrentRecord() ) {
		// yield
		QUICKPOLL(MAX_NICENESS);
		// get first rec
		key_t k = m_ulist.getCurrentKey();
		// some titledbs have incorrect extension hashes in the
		// buzzlogic collection, so ignore that for now
		//if ( st->m_url[0] ) {
		//	if ( g_tfndb.getExt ( k ) != e ) continue;
		//}

		// docid must match! cuz these include probable docids
		// in the range of [uk1,uk2]
		if ( g_tfndb.getDocId(&k) != m_docId ) continue;
		// . skip it if it is a delete and we are not touching tfndb
		// . remember this is the newly rebuilt tfndb we are accessing
		//   here since we set rdbId to RDB2_TFNDB2 just for rebuilding
		//   tfndb exclusively
		if ( m_rebuildTfndb && m_isDelete ) {
			// addToTfndb2()
			//m_ext   = g_tfndb.getExt(k);
			m_uh48 = g_tfndb.getUrlHash48(&k);
			//m_stage = STAGE_TITLEDB_6;
			m_stage = STAGE_TITLEDB_4;
			return true;
		}

		// . get file num this rec is stored
		// . this is updated right after the file num is merged by
		//   scanning all records in tfndb. this is very quick if all
		//   of tfndb is in memory, otherwise, it might take a few
		//   seconds. update call done in RdbMerge::incorporateMerge().
		// . 255 means just in spiderdb OR titleRec is in tree
		int32_t tfn = g_tfndb.getTfn ( &k );
		// set "matched" to true if this titlerec is the latest
		if ( tfn == m_tfn ) matched = true;
		// break now that we've matched the title rec's docid
		break;
	}

	// check if in tree
	RdbTree *tree = &g_titledb.m_rdb.m_tree;
	int32_t node = tree->getNode ( m_collnum,(char *)&m_currentTitleRecKey );
	// if there, that's a match
	if ( node >= 0 ) matched = true;
	// if not matched in tfndb and not in tree it must have been deleted!
	if ( ! matched ) {
		m_stage = STAGE_TITLEDB_0;
		m_recsOverwritten++;
		return true;
	}
	return true;
}
*/

/*
bool Repair::getTitleRec ( ) {
	key_t key = m_scanList.getCurrentKey();
	// that's a revdb record, get the docid
	m_docId = g_revdb.getDocId (&key);
	// make it
	key_t tk1 = g_titledb.makeFirstKey ( m_docId );
	key_t tk2 = g_titledb.makeLastKey  ( m_docId );
	// use msg22
	return m_msg5.getList ( RDB_TITLEDB ,
				m_coll,
				&m_titleRecList ,
				&tk1 ,
				&tk2 ,
				32   ,
				true , // include tree
				false , // add to cache
				0     , // max cache age
				0     , // start file #
				-1    , // numfiles
				this ,
				loopWrapper ,
				MAX_NICENESS     , // niceness
				true          , // do error correction?
				NULL             , // cache key ptr
				0                , // retry num
				-1               , // maxRetries
				true             , // compensate for merge
				-1LL             , // sync point
				&m_msg5b         );
}	

*/

// TODO: allocate these on demand!!!!!!
//#define MAX_OUT_REPAIR 10
//static char     s_inUse [ MAX_OUT_REPAIR ];
//static XmlDoc   s_docs  [ MAX_OUT_REPAIR ];

void doneWithIndexDoc ( XmlDoc *xd ) {
	// preserve
	int32_t saved = g_errno;
	// nuke it
	mdelete ( xd , sizeof(XmlDoc) , "xdprnuke");
	delete ( xd );
	// reduce the count
	g_repair.m_numOutstandingInjects--;
	// error?
	if ( saved ) {
		g_repair.m_recsetErrors++;
		g_repair.m_stage = STAGE_TITLEDB_0; // 0
		return;
	}
	QUICKPOLL(MAX_NICENESS);
	/*
	// find the i
	int32_t i ; for ( i = 0 ; i < MAX_OUT_REPAIR ; i++ ) {
		if ( ! s_inUse[i] ) continue;
		if ( xd == &s_docs[i] ) break;
	}
	if ( i >= MAX_OUT_REPAIR ) { char *xx=NULL;*xx=0; }
	// reset it i guess
	xd->reset();
	// give back the tr
	s_inUse[i] = 0;
	*/
}

void doneWithIndexDocWrapper ( void *state ) {
	// clean up
	doneWithIndexDoc ( (XmlDoc *)state );
	// and re-enter the loop to get next title rec
	g_repair.loop ( NULL );
}

//bool Repair::getTagRec ( void **state ) {
bool Repair::injectTitleRec ( ) {

	// no, now we specify in call to indexDoc() which
	// dbs we want to update
	//if ( ! m_fullRebuild && ! m_removeBadPages ) return true;

	QUICKPOLL(MAX_NICENESS);

	// scan for our docid in the title rec list
	char *titleRec = NULL;
	int32_t titleRecSize = 0;
	// convenience var
	RdbList *tlist = &m_titleRecList;
	// scan the titleRecs in the list
	for ( ; ! tlist->isExhausted() ; tlist->skipCurrentRecord ( ) ) {
		// breathe
		QUICKPOLL ( MAX_NICENESS );
		// get the rec
		char *rec     = tlist->getCurrentRec();
		int32_t  recSize = tlist->getCurrentRecSize();
		// get that key
		key_t *k = (key_t *)rec;
		// skip negative recs, first one should not be negative however
		if ( ( k->n0 & 0x01 ) == 0x00 ) continue;
		// get docid of that guy
		int64_t dd = g_titledb.getDocId(k);
		// compare that
		if ( m_docId != dd ) continue;
		// we got it!
		titleRec = rec;
		titleRecSize = recSize;
		break;
	}

	/*
	// title rec for this doc was not found...
	if ( ! titleRec ) {
		// don't bother with revdb?
		if ( ! m_rebuildRevdb ) return true;
		// so just add the revdb rec into the new revdb. it was
		// probably an eventless url and we just added sectiondb or
		// placedb entries for it...
		char *rec = m_scanList.getCurrentRec();
		int32_t  recSize = m_scanList.getCurrentRecSize();
		if ( recSize <= 0 ) { char *xx=NULL;*xx=0; }
		if ( ! m_msg4.addMetaList ( rec ,
					    recSize ,
					    m_coll ,
					    this , 
					    loopWrapper2 ,
					    MAX_NICENESS ,
					    RDB2_REVDB2 ) ) {
			// note it for debugging
			log("repair: msg4 returned false");
			// it will call our callback!
			return false;
		}
		// crap, gotta retry adding this if it returned false
		//g_repair.m_stage = STAGE_TITLEDB_3;
		// ask repair wrapper to call us back
		//g_repair.m_needsCallback = true;
		// sleep away!
		//return false;
		//}
		// no title recs
		m_noTitleRecs++;
		// we're all done and did not block, per se
		return true;
	}
	*/

	// make sure this is on
	//g_conf.m_injectionEnabled = true;

	// get raw rec from list
	//char *rec = m_titleRecList.getCurrentRec();
	//int32_t  recSize = m_titleRecList.getCurrentRecSize();


	/*
	// claim a title rec
	bool static s_init = false;
	if ( ! s_init ) { memset (s_inUse,0,MAX_OUT_REPAIR); s_init = true; }
	//TitleRec *tr = NULL;
	XmlDoc *xd = NULL;
	int32_t i ;
	for ( i = 0 ; i < MAX_OUT_REPAIR ; i++ ) {
		if ( s_inUse[i] ) continue;
		//tr = &s_trs[i];
		xd = &s_docs[i];
		break;
	}
	*/

	XmlDoc *xd = NULL;
	try { xd = new ( XmlDoc ); }
	catch ( ... ) {
                g_errno = ENOMEM;
		m_recsetErrors++;
		m_stage = STAGE_TITLEDB_0; // 0
		return true;
	}
        mnew ( xd , sizeof(XmlDoc),"xmldocpr");    

	// clear out first since set2 no longer does
	//xd->reset();
	if ( ! xd->set2 ( titleRec,-1,m_cr->m_coll , NULL , MAX_NICENESS ) ) {
		m_recsetErrors++;
		m_stage = STAGE_TITLEDB_0; // 0
		return true;
	}
	// set callback
	xd->setCallback ( xd , doneWithIndexDocWrapper );

	// clear any error involved with cache, it doesn't matter so much
	g_errno = 0;


	// set the titleRec we got
	//if ( ! tr->set ( rec , recSize , false /*own data?*/ ) ) {
	//	m_recsetErrors++;
	//	m_stage = STAGE_TITLEDB_0;
	//	return true;
	//}

	//Url *fu = xd->getFirstUrl();

	// . determine which host in our group should spider this
	// . just use the host that should dole it
	// . if we are not responsible for this url, skip it
	// . usually this uses m_firstIp of SpiderRequest but just use
	//   a hash of the url as the ip! HACK!
	// . no.. no.. we already have a docid based assignment filter
	//   in gotScanRecList(), it mods the docid with the # of hosts
	//   in our group
	//int32_t hh = hash32n(fu->getUrl());
	//int32_t hostId = getHostIdToDole ( hh );
	//if ( ! isAssignedToUs ( hh ) ) {
	//	m_stage = STAGE_TITLEDB_0;
	//	return true;
	//}

	// skip if root and not doing roots
	//if ( ! m_rebuildRoots && tr->getUrl()->isRoot() ) {
	//	m_recsRoot++;
	//	m_stage = STAGE_TITLEDB_0;
	//	return true;
	//}

	// skip if non-root and not doing non roots
	//if ( ! m_rebuildNonRoots && ! tr->getUrl()->isRoot() ) {
	//	m_recsNonRoot++;
	//	m_stage = STAGE_TITLEDB_0;
	//	return true;
	//}


	// invalidate certain things to recompute!
	// we are now setting from docid
	xd->m_tagRecValid    = false;

	// rebuild the title rec! otherwise we re-add the old one!!!!!!!
	xd->m_titleRecBufValid = false;
	// free it since set2() should have uncompressed it!
	//mfree ( titleRec , titleRecSize, "repair" );
	// and so xd doesn't free it
	xd->m_titleRecBuf.purge();// = NULL;

	// use the ptr_utf8Content that we have
	xd->m_recycleContent = true;

	// rebuild the content hash since we change that function sometimes
	xd->m_contentHash32Valid = false;

	// hmmm... take these out to see if fixes the core
	//xd->m_linkInfo1Valid = false;
	//xd->m_linkInfo2Valid = false;

	// claim it, so "tr" is not overwritten
	m_numOutstandingInjects++;
	//s_inUse[i] = 1;

	bool addToSecondaryRdbs = true;
	//if ( m_fullRebuild    ) addToSecondaryRdbs = false;
	//if ( m_removeBadPages ) addToSecondaryRdbs = false;

	xd->m_usePosdb     = m_rebuildPosdb;
	//xd->m_useDatedb    = m_rebuildDatedb;
	xd->m_useClusterdb = m_rebuildClusterdb;
	xd->m_useLinkdb    = m_rebuildLinkdb;
	xd->m_useSpiderdb  = m_rebuildSpiderdb;
	xd->m_useTitledb   = m_rebuildTitledb;
	//xd->m_usePlacedb   = m_rebuildPlacedb;
	//xd->m_useSectiondb = m_rebuildSectiondb;
	//xd->m_useRevdb     = m_rebuildRevdb;
	xd->m_useSecondaryRdbs = addToSecondaryRdbs;

	// always use tagdb because if we update the sitenuminlinks
	// or whatever, we want to add that to tagdb
	xd->m_useTagdb     = true;

	// not if rebuilding link info though! we assume the old link info is
	// bad...
	if ( m_rebuildLinkdb )
		xd->m_useTagdb = false;

	if ( m_rebuildLinkdb ) {
		// also need to preserve the "lost link" flag somehow
		// from the old linkdb...
		//log("repair: would lose linkdb lost flag.");
		// core until we find a way to preserve the old discovery
		// date from the old linkdb!
		//log("repair: fix linkdb rebuild. coring.");
		//char *xx=NULL;*xx=0;
	}

	if ( ! g_conf.m_rebuildRecycleLinkInfo ) {
		// then recompute link info as well!
		xd->m_linkInfo1Valid = false;
		// make null to be safe
		xd->ptr_linkInfo1  = NULL;
		xd->size_linkInfo1 = 0;
	}
	// . also lookup site rank again!
	// . this will use the value in tagdb if less than 48 hours otherwise
	//   it will recompute it
	// . CRAP! this makes the data undeletable if siterank changes!
	//   so we have to be able to re-save our title rec with the new
	//   site rank info...
	if ( xd->m_useTitledb ) {
		// save for logging
		xd->m_logLangId         = xd->m_langId;
		xd->m_logSiteNumInlinks = xd->m_siteNumInlinks;
		// recompute site, no more domain sites allowed
		xd->m_siteValid = false;
		xd->ptr_site    = NULL;
		xd->size_site   = 0;
		// recalculate the sitenuminlinks
		xd->m_siteNumInlinksValid = false;
		// recalculate the langid
		xd->m_langIdValid = false;
		// recalcualte and store the link info
		xd->m_linkInfo1Valid = false;
		// make null to be safe
		xd->ptr_linkInfo1  = NULL;
		xd->size_linkInfo1 = 0;
		//xd->m_linkInfo2Valid = false;
		// re-get the tag rec from tagdb
		xd->m_tagRecValid     = false;
		xd->m_tagRecDataValid = false;
	}


	xd->m_priority = -1;
	xd->m_priorityValid = true;

	// this makes sense now that we set from docid using set3()?
	//xd->m_recycleContent = true;

	xd->m_contentValid = true;
	xd->m_content = xd->ptr_utf8Content;
	xd->m_contentLen = xd->size_utf8Content - 1;

	// . get the meta list to add
	// . sets m_usePosdb, m_useTitledb, etc.
	bool status = xd->indexDoc ( );
	// blocked?
	if ( ! status ) return false;

	// give it back
	doneWithIndexDoc ( xd );

	return true;
}

/*
bool Repair::addToTfndb2 ( ) {

	// only do this for adding recs to tfndb
	if ( ! m_rebuildTfndb ) return true;
	// if doing a full rebuild, skip this, already done
	//if ( m_fullRebuild ) return true;

	// . this is broken!!! figure out why the rebuild doesn't work...
	// . seems like the tfns are off...
	//char *xx = NULL; *xx = 0;

	QUICKPOLL(MAX_NICENESS);

	// sanity check, must have a valid m_ext
	//if ( m_ext == -1 ) { char *xx = NULL; *xx = 0; }
	if ( ! m_uh48 ) { char *xx = NULL; *xx = 0; }

	// m_docId should already have been set!
	m_tfndbKey = g_tfndb.makeKey ( m_docId    , // tr->getDocId()
				       m_uh48 ,
				       m_tfn      , 
				       m_isDelete );// isDelete?
        // set the list from the buffer
        m_addlist.set ( (char *)&m_tfndbKey    ,
			sizeof(key_t) ,
			(char *)&m_tfndbKey    ,
			sizeof(key_t) ,
			(char *)&m_tfndbKey    , // start key
			(char *)&m_tfndbKey    , // end   key
			0             , // fixedDataSize
			false         , // ownData?
			false         , // use half keys? not when adding.
			12            );// tfndb key size

        // this returns false if it blocks
        g_errno = 0;
	// . keep these local, because the tfn in the tfndb rec
	//   make not be the same between twins!!
	// . returns true on success, so go on to next stage
	if ( g_tfndb2.getRdb()->addList(m_collnum,&m_addlist,
					MAX_NICENESS) )
		return true;
	// keep retrying, might be OOM, auto-saving, etc.
	m_stage = STAGE_TITLEDB_4 ;
	// sleep 1 second and retry
	m_isRetrying = true;
	// must need to dump, so wait for that!
	return log("repair: addToTfndb2: %s",mstrerror(g_errno));
}
*/

// . returns false if fails cuz buffer cannot be grown (oom)
// . this is called by Parms.cpp
bool Repair::printRepairStatus ( SafeBuf *sb , int32_t fromIp ) {
	// default is a repairMode of 0, "not running"
	char *status = "not running";
	if ( g_repairMode == 0 && g_conf.m_repairingEnabled )
		status = "waiting for previous rebuild to complete";
	if ( g_repairMode == 1 )
		status = "waiting for spiders or merge to stop";
	if ( g_repairMode == 2 )			
		status = "waiting for all hosts in network to stop "
			"spidering and merging";
	if ( g_repairMode == 3 )			
		status = "waiting for all hosts to save";
	if ( g_repairMode == 4 ) {
		if ( m_completedFirstScan )
			status = "scanning old spiderdb";
		else
			status = "scanning old records";
	}
	if ( g_repairMode == 5 ) 
		status = "waiting for final dump to complete";
	if ( g_repairMode == 6 ) 
		status = "waiting for others to finish scan and dump";
	if ( g_repairMode == 7 )			
		status = "updating rdbs with new data";
	if ( g_repairMode == 8 )			
		status = "waiting for all hosts to complete update";
	if ( ! g_process.m_powerIsOn && g_conf.m_repairingEnabled )
		status = "waiting for power to return";

	// the titledb scan stats (phase 1)
	int64_t ns     = m_recsScanned ;
	int64_t nr     = g_titledb.getRdb()->getNumTotalRecs() ;
	float     ratio  = ((float)ns * 100.0) / (float)nr;
	int64_t errors = 
		m_recsOutOfOrder +
		m_recsetErrors   +
		m_recsCorruptErrors +
		m_recsXmlErrors   +
		m_recsDupDocIds    ;

	// the spiderdb scan stats (phase 2)
	int64_t ns2     = m_spiderRecsScanned ;
	int64_t nr2     = g_spiderdb.getRdb()->getNumTotalRecs() ;
	float     ratio2  = ((float)ns2 * 100.0) / (float)nr2;
	int64_t errors2 = 
		m_spiderRecSetErrors;

	char *newColl = " &nbsp; ";
	//if ( m_fullRebuild ) newColl = m_newColl;

	char *oldColl = " &nbsp; ";
	if ( m_cr ) oldColl = m_cr->m_coll;

	Host *mh = g_pingServer.m_minRepairModeHost;
	int32_t  minHostId = -1;
	char  minIpBuf[64];
	minIpBuf[0] = '\0';
	int16_t minPort = 80;
	if ( mh ) {
		minHostId = mh->m_hostId;
		int32_t minHostIp = g_hostdb.getBestIp ( mh , fromIp );
		strcpy(minIpBuf,iptoa(minHostIp));
		minPort = mh->m_httpPort;
	}

	// now show the rebuild status
	sb->safePrintf ( 
			 "<table%s"
			 " id=\"repairstatustable\">"

			 "<tr class=hdrow><td colspan=2><b><center>"
			 "Rebuild Status</center></b></td></tr>\n"

			 "<tr bgcolor=#%s><td colspan=2>"
			 "<font size=-2>"
			 "Use this to rebuild a database or to reindex "
			 "all pages to pick up new link text. Or to "
			 "reindex all pages to pick up new site rank info "
			 "from tagdb. To pick up "
			 "new link text you should rebuild titledb and posdb. "
			 "If unsure, just do a full rebuild, but it will "
			 "require about 2GB more than the disk used before "
			 "the rebuild, so at its peak the rebuild will use "
			 "a little more than double the disk space you "
			 "are using now. Also you will want to set "
			 "recycle link text to false to pick up the new link "
			 "text. However, if you just want to pick up "
			 "new sitenuminlinks tags in tagdb to get more "
			 "accurate siteranks for each result, then you can "
			 "leave the recycle link text set to true."
			 ""
			 "<br><br>"
			 "All spidering for all collections will be disabled "
			 "when the rebuild is in progress. But you should "
			 "still be able to conduct searches on the original "
			 "index. You can pause "
			 "the rebuild by disabling <i>rebuild mode enabled"
			 "</i>. Each shard should save its rebuid state so "
			 "you can safely shut shards down when rebuilding "
			 "and they should resume on startup. When the rebuild "
			 "completes it moves the original files to the trash "
			 "subdirectory and replaces them with the newly "
			 "rebuilt files."
			 "</font>"
			 "</td></tr>"

			 // status (see list of above statuses)
			 "<tr bgcolor=#%s><td width=50%%><b>status</b></td>"
			 "<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td width=50%%><b>rebuild mode</b>"
			 "</td>"
			 "<td>%"INT32"</td></tr>\n"

			 "<tr bgcolor=#%s>"

			 "<td width=50%%><b>min rebuild mode</b></td>"
			 "<td>%"INT32"</td></tr>\n"

			 "<tr bgcolor=#%s>"
			 "<td width=50%%><b>host ID with min rebuild mode"
			 "</b></td>"

			 "<td><a href=\"http://%s:%hu/admin/rebuild\">"
			 "%"INT32"</a></td></tr>\n"

			 "<tr bgcolor=#%s><td><b>old collection</b></td>"
			 "<td>%s</td></tr>"

			 "<tr bgcolor=#%s><td><b>new collection</b></td>"
			 "<td>%s</td></tr>"

			 ,
			 TABLE_STYLE ,


			 LIGHT_BLUE ,
			 LIGHT_BLUE ,
			 status ,

			 LIGHT_BLUE ,
			 (int32_t)g_repairMode,

			 LIGHT_BLUE ,
			 (int32_t)g_pingServer.m_minRepairMode,

			 LIGHT_BLUE ,
			 minIpBuf, // ip string
			 minPort,  // port
			 (int32_t)minHostId,

			 LIGHT_BLUE ,
			 oldColl ,

			 LIGHT_BLUE ,
			 newColl
			 );

	sb->safePrintf ( 
			 // docs done, includes overwritten title recs
			 "<tr bgcolor=#%s><td><b>titledb recs scanned</b></td>"
			 "<td>%"INT64" of %"INT64"</td></tr>\n"

			 // percent complete
			 "<tr bgcolor=#%s><td><b>titledb recs scanned "
			 "progress</b></td>"
			 "<td>%.2f%%</td></tr>\n"

			 // title recs set errors, parsing errors, etc.
			 //"<tr bgcolor=#%s><td><b>title recs injected</b></td>"
			 //"<td>%"INT64"</td></tr>\n"

			 // title recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>titledb rec error count</b></td>"
			 "<td>%"INT64"</td></tr>\n"

			 // sub errors
			 "<tr bgcolor=#%s><td> &nbsp; key out of order</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; set errors</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; corrupt errors</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; xml errors</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; dup docid errors</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; negative keys</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 //"<tr bgcolor=#%s><td> &nbsp; overwritten recs</b></td>"
			 //"<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; twin's "
			 "respsponsibility</b></td>"
			 "<td>%"INT64"</td></tr>\n"

			 "<tr bgcolor=#%s><td> &nbsp; wrong shard</b></td>"
			 "<td>%"INT64"</td></tr>\n"

			 "<tr bgcolor=#%s><td> &nbsp; root urls</b></td>"
			 "<td>%"INT64"</td></tr>\n"
			 "<tr bgcolor=#%s><td> &nbsp; non-root urls</b></td>"
			 "<td>%"INT64"</td></tr>\n"

			 "<tr bgcolor=#%s><td> &nbsp; no title rec</b></td>"
			 "<td>%"INT64"</td></tr>\n"

			 //"<tr><td><b> &nbsp; Other errors</b></td>"
			 //"<td>%"INT64"</td></tr>\n"

			 // time left in hours
			 //"<tr><td><b>Time Left in Phase %"INT32"</b></td>"
			 //"<td>%.2f hrs</td></tr>\n"

			 ,
			 DARK_BLUE,
			 ns     ,
			 nr     ,
			 DARK_BLUE,
			 ratio  ,
			 //DARK_BLUE,
			 //m_recsInjected ,
			 DARK_BLUE,
			 errors ,
			 DARK_BLUE,
			 m_recsOutOfOrder ,
			 DARK_BLUE,
			 m_recsetErrors  ,
			 DARK_BLUE,
			 m_recsCorruptErrors  ,
			 DARK_BLUE,
			 m_recsXmlErrors  ,
			 DARK_BLUE,
			 m_recsDupDocIds ,
			 DARK_BLUE,
			 m_recsNegativeKeys ,
			 //DARK_BLUE,
			 //m_recsOverwritten ,
			 DARK_BLUE,
			 m_recsUnassigned ,

			 DARK_BLUE,
			 m_recsWrongGroupId ,

			 DARK_BLUE,
			 m_recsRoot ,
			 DARK_BLUE,
			 m_recsNonRoot ,

			 DARK_BLUE,
			 m_noTitleRecs
			 );


	sb->safePrintf(
			 // spider recs done
			 "<tr bgcolor=#%s><td><b>spider recs scanned</b></td>"
			 "<td>%"INT64" of %"INT64"</td></tr>\n"

			 // percent complete
			 "<tr bgcolor=#%s><td><b>spider recs scanned "
			 "progress</b></td>"
			 "<td>%.2f%%</td></tr>\n"

			 // spider recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>spider rec not "
			 "assigned to us</b></td>"
			 "<td>%"INT32"</td></tr>\n"

			 // spider recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>spider rec errors</b></td>"
			 "<td>%"INT64"</td></tr>\n"

			 // spider recs set errors, parsing errors, etc.
			 "<tr bgcolor=#%s><td><b>spider rec bad tld</b></td>"
			 "<td>%"INT32"</td></tr>\n"

			 // time left in hours
			 //"<tr bgcolor=#%s><td><b>"
			 //"Time Left in Phase %"INT32"</b></td>"
			 //"<td>%.2f hrs</td></tr>\n"

			 ,
			 LIGHT_BLUE ,
			 ns2    ,
			 nr2    ,
			 LIGHT_BLUE ,
			 ratio2 ,
			 LIGHT_BLUE ,
			 m_spiderRecNotAssigned ,
			 LIGHT_BLUE ,
			 errors2,
			 LIGHT_BLUE ,
			 m_spiderRecBadTLD
			 );


	int32_t nsr;
	Rdb **rdbs = getSecondaryRdbs ( &nsr );

	// . count the recs in each secondary rdb
	// . those are the rdbs we are adding the recs to
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		char *bg = DARK_BLUE;
		Rdb *rdb = rdbs[i];
		int64_t tr = rdb->getNumTotalRecs();
		// skip if init2() as not called on it b/c the
		// m_dbname will be 0
		if ( tr == 0 ) continue;
		sb->safePrintf(
			 "<tr bgcolor=#%s><td><b>%s2 recs</b></td>"
			 "<td>%"INT64"</td></tr>\n" ,
			 bg,
			 rdb->m_dbname,
			 rdb->getNumTotalRecs());
	}

	// close up that table
	sb->safePrintf("</table>\n<br>");

	// print a table
	char *rr[23];
	if ( m_fullRebuild )       rr[0] = "Y";
	else                       rr[0] = "N";

	if ( m_rebuildTitledb )    rr[1] = "Y";
	else                       rr[1] = "N";
	//if ( m_rebuildTfndb )      rr[2] = "Y";
	//else                       rr[2] = "N";
	//if ( m_rebuildIndexdb )    rr[3] = "Y";
	//else                       rr[3] = "N";
	if ( m_rebuildPosdb )    rr[3] = "Y";
	else                       rr[3] = "N";
	//if ( m_rebuildDatedb )     rr[4] = "Y";
	//else                       rr[4] = "N";
	if ( m_rebuildClusterdb )  rr[5] = "Y";
	else                       rr[5] = "N";
	//if ( m_rebuildChecksumdb ) rr[6] = "Y";
	//else                       rr[6] = "N";
	if ( m_rebuildSpiderdb )   rr[7] = "Y";
	else                       rr[7] = "N";
	//if ( m_rebuildSitedb )     rr[8] = "Y";
	//else                       rr[8] = "N";
	if ( m_rebuildLinkdb )     rr[9] = "Y";
	else                       rr[9] = "N";

	//if ( g_conf.m_rebuildRecycleLinkInfo )  rr[10] = "Y";
	//else                                    rr[10] = "N";


	if ( m_rebuildRoots  )     rr[11] = "Y";
	else                       rr[11] = "N";
	if ( m_rebuildNonRoots  )  rr[12] = "Y";
	else                       rr[12] = "N";

	//if ( m_rebuildTagdb )      rr[13] = "Y";
	//else                       rr[13] = "N";
	//if ( m_rebuildPlacedb )    rr[14] = "Y";
	//else                       rr[14] = "N";
	//if ( m_rebuildSectiondb )  rr[16] = "Y";
	//else                       rr[16] = "N";
	//if ( m_rebuildRevdb )      rr[17] = "Y";
	//else                       rr[17] = "N";

	sb->safePrintf ( 

			 "<table %s "
			 "id=\"repairstatustable2\">"

			 // current collection being repaired
			 "<tr class=hdrow><td colspan=2><b><center>"
			 "Rebuild Settings In Use</center></b></td></tr>"

			 // . print parms for this repair
			 // . they may differ than current controls because
			 //   the current controls were changed after the
			 //   repair started
			 "<tr bgcolor=#%s>"
			 "<td width=50%%><b>full rebuild</b></td>"
			 "<td>%s</td></tr>\n"

			 //"<tr bgcolor=#%s><td><b>recycle link info</b></td>"
			 //"<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild titledb</b></td>"
			 "<td>%s</td></tr>\n"

			 //"<tr bgcolor=#%s><td><b>rebuild tfndb</b></td>"
			 //"<td>%s</td></tr>\n"

			 //"<tr bgcolor=#%s><td><b>rebuild indexdb</b></td>"
			 //"<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild posdb</b></td>"
			 "<td>%s</td></tr>\n"

			 //"<tr bgcolor=#%s><td><b>rebuild datedb</b></td>"
			 //"<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild clusterdb</b></td>"
			 "<td>%s</td></tr>\n"

			 //"<tr bgcolor=#%s><td><b>rebuild checksumdb</b></td>"
			 //"<td>%s</td></tr>\n"

			 "<tr bgcolor=#%s><td><b>rebuild spiderdb</b></td>"
			 "<td>%s</td></tr>\n" 

			 "<tr bgcolor=#%s><td><b>rebuild linkdb</b></td>"
			 "<td>%s</td></tr>\n" 

			 //"<tr bgcolor=#%s><td><b>rebuild tagdb</b></td>"
			 //"<td>%s</td></tr>\n" 
			 //"<tr bgcolor=#%s><td><b>rebuild placedb</b></td>"
			 //"<td>%s</td></tr>\n" 
			 //"<tr bgcolor=#%s><td><b>rebuild sectiondb</b></td>"
			 //"<td>%s</td></tr>\n" 
			 //"<tr bgcolor=#%s><td><b>rebuild revdb</b></td>"
			 //"<td>%s</td></tr>\n" 


			 "<tr bgcolor=#%s><td><b>rebuild root urls</b></td>"
			 "<td>%s</td></tr>\n" 

			 "<tr bgcolor=#%s>"
			 "<td><b>rebuild non-root urls</b></td>"
			 "<td>%s</td></tr>\n" 

			 "</table>\n"
			 "<br>\n"
			 ,
			 TABLE_STYLE,

			 LIGHT_BLUE,
			 rr[0],
			 //rr[10],

			 LIGHT_BLUE,
			 rr[1],
			 //rr[2],

			 LIGHT_BLUE,
			 rr[3],
			 //rr[4],

			 LIGHT_BLUE,
			 rr[5],
			 //rr[6],

			 LIGHT_BLUE,
			 rr[7],
			 //rr[8],

			 LIGHT_BLUE,
			 rr[9],

			 //rr[13],
			 //rr[14],
			 //rr[15],
			 //rr[16],
			 //rr[17],

			 LIGHT_BLUE,
			 rr[11],

			 LIGHT_BLUE,
			 rr[12] 
			 );
	return true;
}

static bool   s_savingAll = false;
static void (*s_saveCallback)(void *state) ;
static void  *s_saveState;

// . return false if blocked, true otherwise
// . will call the callback when all have been saved
// . used by Repair.cpp to save all rdbs before doing repair work
bool saveAllRdbs ( void *state , void (* callback)(void *state) ) {
	// only call once
	if ( s_savingAll ) {
		//log("db: Already saving all.");
		// let them know their callback will not be called even 
		// though we returned false
		if ( callback ) { char *xx = NULL; *xx = 0; }
		return false;
	}
	// set it
	s_savingAll = true;
	// TODO: why is this called like 100x per second when a merge is
	// going on? why don't we sleep longer in between?
	//bool close ( void *state , 
	//	     void (* callback)(void *state ) ,
	//	     bool urgent ,
	//	     bool exitAfterClosing );

	int32_t nsr;
	Rdb **rdbs = getAllRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		// skip if not initialized
		if ( ! rdb->isInitialized() ) continue;
		// save/close it
		rdb->close(NULL,doneSavingRdb,false,false);
	}

	// return if still waiting on one to close
	if ( anyRdbNeedsSave() ) return false;
	// all done
	return true;
}

// return false if one or more is still not closed yet
bool anyRdbNeedsSave ( ) {
	int32_t count = 0;
	int32_t nsr;
	Rdb **rdbs = getAllRdbs ( &nsr );
	for ( int32_t i = 0 ; i < nsr ; i++ ) {
		Rdb *rdb = rdbs[i];
		count += rdb->needsSave();
	}
	if ( count ) return true;
	s_savingAll = false;
	return false;
}

// returns false if waiting on some to save
void doneSavingRdb ( void *state ) {
	if ( ! anyRdbNeedsSave() ) return;
	// all done
	s_savingAll = false;
	// call callback
	if ( s_saveCallback ) s_saveCallback ( s_saveState );
}
