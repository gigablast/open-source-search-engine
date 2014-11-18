#include "gb-include.h"

#include "Timedb.h"
#include "Threads.h"

Timedb g_timedb;
Timedb g_timedb2;

// reset rdb
void Timedb::reset() { m_rdb.reset(); }

static void updateTablesWrapper ( int fd , void *state ) ;
static void gotTimeListWrapper ( void *state , RdbList *list, Msg5 *msg5 );

// init our rdb
bool Timedb::init ( ) {

	// bail if not indexing events
	if ( ! g_conf.m_indexEventsOnly ) return true;

	//int32_t maxTreeMem = 1000000 ;
	int32_t maxTreeMem = g_conf.m_timedbMaxTreeMem ;

	// . what's max # of tree nodes?
	// . assume avg TimeRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	int32_t nodeSize     = (sizeof(key128_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = maxTreeMem  / nodeSize;

	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "timedb"                    ,
			    true                        , // dedup same keys?
			    0                          , // fixed data size
			    // this should not really be changed...
			    2                           , // min files to merge
			    maxTreeMem ,
			    maxTreeNodes                ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0                           , // cache mem
			    0                           , // maxCacheNodes
			    false                       ,// half keys?
			    false                       ,// g_conf.m_timedbSav
			    NULL                        , // page cache ptr
			    false                       , // is titledb?
			    false                       , // preloaddiskcache?
			    sizeof(key128_t)            ,
			    false                       ))
		return false;

	// test out makekey
	int64_t docId = 0x34097534;
	int32_t eventId = 156;
	// shave off the SECONDS since makeKey does that
	int32_t stime = ((g_now / 1000)/60) * 60;
	int32_t etime = ((stime + 1000)/60) * 60;
	int32_t ntime = ((etime + 1301)/60) * 60;
	key128_t k = g_timedb.makeKey(stime,docId,eventId,etime,ntime,false);
	if ( g_timedb.getStartTime32    (&k) != stime ) { char *xx=NULL;*xx=0;}
	if ( g_timedb.getEndTime32      (&k) != etime ) { char *xx=NULL;*xx=0;}
	if ( g_timedb.getNextStartTime32(&k) != ntime ) { char *xx=NULL;*xx=0;}
	if ( g_timedb.getDocId          (&k) != docId ) { char *xx=NULL;*xx=0;}
	if ( g_timedb.getEventId        (&k) != eventId){ char *xx=NULL;*xx=0;}

	// every minute update the table. but check every few seconds
	// since we may have multiple collections that need this
	// check every 5000 ms
	if ( ! g_loop.registerSleepCallback ( 5000,NULL,updateTablesWrapper ) )
		return false;
	return true;
}

// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Timedb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg TimeRec size (compressed html doc) is about 1k we get:
	// . NOTE: overhead is about 32 bytes per node
	int32_t nodeSize     = (sizeof(key128_t)+12+4) + sizeof(collnum_t);
	int32_t maxTreeNodes = treeMem  / nodeSize;
	// initialize our own internal rdb
	if ( ! m_rdb.init ( g_hostdb.m_dir              ,
			    "timedbRebuild"            ,
			    true                        , // dedup same keys?
			    0                           , // fixed record size
			    50                          , // MinFilesToMerge
			    treeMem                     ,
			    maxTreeNodes                ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0                           , // MaxCacheMem ,
			    0                           , // maxCacheNodes
			    false                       , // half keys?
			    false                       , // timedbSaveCache
			    NULL                        , // page cache ptr
			    false                       , // is titledb?
			    false                       , // preloaddiskcache?
			    sizeof(key128_t)            ,
			    false                       ))
		return false;
	return true;
}

bool Timedb::addColl ( char *coll, bool doVerify ) {
	if ( ! m_rdb.addColl ( coll ) ) return false;
	if ( ! doVerify ) return true;
	// verify
	if ( verify(coll) ) return true;
	// if not allowing scale, return false
	if ( ! g_conf.m_allowScale ) return false;
	// otherwise let it go
	log ( "db: Verify failed, but scaling is allowed, passing." );
	return true;
}

bool Timedb::verify ( char *coll ) {
	log ( LOG_INFO, "db: Verifying Timedb for coll %s...", coll );
	g_threads.disableThreads();

	Msg5 msg5;
	Msg5 msg5b;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;

	if ( ! msg5.getList ( RDB_TIMEDB   ,
			      coll          ,
			      &list         ,
			      &startKey      ,
			      &endKey        ,
			      1024*1024     , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          , // cache key ptr
			      0             , // retry num
			      -1            , // maxRetries
			      true          , // compensate for merge
			      -1LL          , // sync point
			      &msg5b        ,
			      false         )) {
		g_threads.enableThreads();
		return log("db: HEY! it did not block");
	}

	// make sure endKey was truncated if necessary
	key_t maxKey; 
	maxKey.setMax();
	if ( list.m_listSize > 64000 && 
	     KEYCMP((char *)&endKey,(char *)&maxKey,12) == 0 ) { 
		log("cra[");
		//char *xx=NULL;*xx=0; }
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		key128_t k ;
		list.getCurrentKey(&k);
		count++;
		uint32_t groupId = getGroupId ( RDB_TIMEDB , &k );
		if ( groupId == g_hostdb.m_groupId ) got++;
	}
	if ( got != count ) {
		log ("db: Out of first %"INT32" records in timedb, "
		     "only %"INT32" belong to our group.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( count > 10 && got == 0 ) 
			log("db: Are you sure you have the right "
				   "data in the right directory? "
				   "Exiting.");
		log ( "db: Exiting due to Timedb inconsistency." );
		g_threads.enableThreads();
		return g_conf.m_bypassValidation;
	}

	log ( LOG_INFO, "db: Timedb passed verification successfully for %"INT32""
			" recs.", count );
	// DONE
	g_threads.enableThreads();
	return true;
}

// key format (128 bits):
// startTime     - 26 bits - in minutes since jan 1, 2010
// docId         - 38 bits
// eventId       - 11 bits - up to 2047 eventids per docid
// endTime       - 26 bits
// nextStartTime - 26 bits
// delBit        -  1 bit

// all times are in UTC
key128_t Timedb::makeKey ( time_t    startTime ,
			   int64_t docId , 
			   uint16_t  eventId ,
			   time_t    endTime ,
			   time_t    nextStartTime ,
			   bool      isDelete ) {

	// make it
	/*
	bool s_init = false;
	static time_t s_start2009;
	static time_t s_start2030;
	if ( ! s_init ) {
		s_init = true;
		// make the time class
		struct tm tmBuild;
		memset( (char *)&tmBuild, 0, sizeof( tmBuild ) );
		tmBuild.tm_mon   = 0; // jan
		tmBuild.tm_mday  = 1; // 1st
		tmBuild.tm_year  = 2009-1900;
		tmBuild.tm_hour  = 0;
		tmBuild.tm_min   = 0;
		tmBuild.tm_sec   = 0;
		tmBuild.tm_isdst = 0;
		s_start2009 = mktime ( &tmBuild );
		tmBuild.tm_year  = 2030-1900;
		s_start2030 = mktime ( &tmBuild );

		// so jan 1, 2010 returns a ttt that when printed using 
		// printTime()
		// prints out "jan 1 7am 2010" so subtract our server timezone
		s_start2009 -= timezone;
		s_start2030 -= timezone;
	}
	*/

	// sanity check. no old dates allowed! we can't go negative
	if ( startTime < START2009 ) { 
		log("timedb: starttime min breach %"INT32"",startTime);
		startTime = START2009;
		//char *xx=NULL;*xx=0; }
	}
	// don't want to breach our 26 bits either
	if ( startTime > START2030 ) { 
		log("timedb: starttime max breach %"INT32"",startTime);
		startTime = START2030;
		//char *xx=NULL;*xx=0; }
	}

	if ( docId > MAX_DOCID || docId < 0 ) { char *xx=NULL;*xx=0; }

	key128_t key ;
	key.n1 = (startTime - START2009)/60;
	key.n1 <<= 38;
	key.n1 |= docId;

	key.n0 = eventId;
	// room for endTime
	key.n0 <<= 26;
	// this equal startTime if no endTime known
	key.n0 |= (endTime - START2009)/60;
	// room for next start time
	key.n0 <<= 26;
	// room for next start time. this is 0 if does not exist
	if ( nextStartTime ) key.n0 |= (nextStartTime - START2009)/60;
	// room for delbit
	key.n0 <<= 1;
	// final del bit
	if ( ! isDelete ) key.n0 |= 0x01;

	return key;
};

bool readTimeList ( collnum_t collnum ) ;

// returns false if blocked, returns true and sets g_errno on error
bool populateTable ( char *coll , int32_t  date1 , int32_t date2 ) {
	// get it for that coll
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( ! cr ) return true;
	// can only have one at a time
	if ( cr->m_inProgress ) {
		log("timedb: populateTable: already running.");
		return true;
	}
	// make a new msg5 for it
	if ( ! cr->m_msg5 ) {
		try { cr->m_msg5 = new(Msg5); }
		catch ( ... ) {
			log("spider: failed to make msg5 for timedb");
			return true;
		}
		// register it
		mnew ( cr->m_msg5 , sizeof(Msg5), "spcoll" );
	}
	// initialize the read range
	cr->m_timedbStartKey = g_timedb.makeStartKey ( date1 );
	cr->m_timedbEndKey   = g_timedb.makeEndKey   ( date2 );
	// flag that we are in progress
	cr->m_inProgress = true;
	// pass in collnum since cr ptr might disappear if collection is
	// deleted while we are doing this!
	if ( ! readTimeList ( cr->m_collnum ) ) return false;
	// if did not block, delete the msg5
	mdelete ( cr->m_msg5 , sizeof(Msg5), "timedb3" );
	delete  ( cr->m_msg5 );
	cr->m_msg5 = NULL;
	return true;
}

#include "Threads.h"
#include "hash.h"

bool initAllSortByDateTables ( ) {
	// bail if not indexing events
	if ( ! g_conf.m_indexEventsOnly ) return true;
	// note it
	log("timedb: initializing all sort by date tables");
	// scan the colls to see if we can init one's table
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it for that coll
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		// if not indexing events, skip this entirely
		if ( ! cr->m_indexEventsOnly ) continue;
		// skip if already updating
		if ( cr->m_inProgress ) continue;
		// kinit it
		initSortByDateTable ( cr->m_coll );
	}
	return true;
}

// . returns false and sets g_errno on error
// . when a new collection is added CollectionRec.cpp calls this on it!
// . now at startup we load the gbstarttime: and gbendtime: termlists that
//   have ALL the time interval data for every docid/eventid we have on
//   this host.
// . we make a hashtable where each slot has the previous close time
//   and the next start time for a given docid/eventid key
// . if the previous close time is 0 that means there was no previous starttime
// . we call this the sortByDateTree.
// . it is used to quickly score the docid/eventids in the search results
//   when sorting by date.
// . use this for counting events now too instead of the current way!!
bool initSortByDateTable ( char *coll ) {
	// not if we are a proxy!
	if ( g_hostdb.m_initialized && g_hostdb.m_myHost->m_isProxy )
		return true;
	// note it
	log("timedb: initializing sort by date table for coll=%s",coll);
	// get it for that coll
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( ! cr ) return true;
	// if not indexing events, skip this entirely
	if ( ! cr->m_indexEventsOnly ) return true;
	// only one at a time
	if ( cr->m_inProgress ) { char *xx=NULL;*xx=0; }
	// core?
	if ( ! isClockInSync() ) { char  *xx=NULL;*xx=0; }
	// int16_tcut
	HashTableX *ht = &cr->m_sortByDateTable;
	// reset it
	ht->reset();
	// alloc mem for the cache/table
	if ( ! ht->set(8,sizeof(TimeSlot),500000,NULL,0,false,0,"sortbydate"))
		return false;
	// we are not running...
	cr->m_inProgress = false;
	// store if threads are on or not
	bool saved = g_threads.m_disabled;
	// turn off threads
	g_threads.disableThreads();
	// this might core because we are not sync'ed with host #0's clock
	// when we first start up!
	cr->m_lastUpdateTime = getTimeGlobal();
	uint32_t date1 = START2009;
	uint32_t date2 = START2030;
	// if it blocked, that is error because threads are off!
	if ( ! populateTable ( coll,date1, date2 ) ) {
		char *xx=NULL;*xx=0; }
	// error?
	if ( g_errno ) return false;
	// re-enable if we they were on when we turned them off
	if ( ! saved ) g_threads.enableThreads();
	// note it
	log("timedb: DONE initializing sort by date table for coll=%s",coll);
	return true;
}

// . this is called every minute
// . every minute read a list of each of the time start termlist and the
//   time end termlist from the last time we read it until 1 minute from now.
// . at startup we read the whole thing so make sure to mark the timestamp
//   from the beginning when we started reading the whole thing
// . then update our g_sortByDateTable
void updateTablesWrapper ( int fd , void *state ) {
	// do not do this if not in sync!
	if ( ! isClockInSync() ) return;
	// time now
	time_t now = getTimeGlobal();
	// scan the colls to see if we can update one
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		// get it for that coll
		CollectionRec *cr = g_collectiondb.m_recs[i];
		if ( ! cr ) continue;
		// if not indexing events, skip this entirely
		if ( ! cr->m_indexEventsOnly ) continue;
		// skip if already updating
		if ( cr->m_inProgress ) continue;
		// get last time we read
		time_t start = cr->m_lastUpdateTime;
		// if recently updated (30 seconds ago), skip it
		if ( start && now - start < 30 ) continue;
		// if not updated yet, prevent a core
		if ( start < START2009 ) start = START2009;
		// and one minute into future
		time_t end = now + 60;
		// update
		cr->m_lastUpdateTime = now;
		// note it
		if ( g_conf.m_logDebugTimedb )
			log("timedb: populating timedb table for %s",
			    cr->m_coll);
		// form the keys. returns false if blocks.
		if ( ! populateTable ( cr->m_coll ,start,end ) )
			// return if blocked
			return;
	}
}

static bool gotTimeList ( collnum_t collnum ) ;

// . returns false if blocked, true otherwise.
// . returns true and sets g_errno on error
bool readTimeList ( collnum_t collnum ) {
	// get it for that coll
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	if ( ! cr ) return true;

loop:
	// all done?
	if ( ! cr->m_inProgress ) return true;
	// int16_tcut
	Msg5 *m5 = cr->m_msg5;
	// use msg5 to get the list
	if ( ! m5->getList ( RDB_TIMEDB    ,
			     cr->m_coll    ,
			     &cr->m_timedbList       ,
			     &cr->m_timedbStartKey     ,
			     &cr->m_timedbEndKey       ,
			     1024*1024     , // minRecSizes, 1MB
			     true          , // includeTree   ,
			     false         , // add to cache?
			     0             , // max cache age
			     0             , // startFileNum  ,
			     -1            , // numFiles      ,
			     (void *)collnum    , // state
			     gotTimeListWrapper , // callback
			     0             , // niceness
			     false         ))// err correction?
		return false;
	// process the list
	if ( ! gotTimeList ( collnum ) ) {
		// report error and stop?
		log("timedb: error processing timedb list: %s",
		    mstrerror(g_errno));
		// force it to be done now
		cr->m_inProgress = false;
		// we did not block, so return true
		return true;
	}
	// get another list
	goto loop;
}

void gotTimeListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) {
	// cast it
	collnum_t collnum = (collnum_t)((uint32_t)state);
	// process it
	gotTimeList ( collnum );
	// try to read more. return if it blocked
	if ( ! readTimeList( collnum ) ) return;
	// get it for that coll
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	// sanity check. if it came back without blocking, should be done!
	if ( cr && cr->m_inProgress ) { char *xx=NULL; *xx=0; }
	// . otherwise, must be all done
	// . TODO: delete this in CollectionRec in the destructor!
	mdelete ( cr->m_msg5 , sizeof(Msg5), "timedb3" );
	delete  ( cr->m_msg5 );
	cr->m_msg5 = NULL;
	//if ( m_callback ) m_callback ( m_state );
}

// returns false with g_errno set on error. returns true on success.
bool gotTimeList ( collnum_t collnum ) {

	// get it for that coll
	CollectionRec *cr = g_collectiondb.getRec(collnum);
	if ( ! cr ) return true;
	// int16_tcut
	HashTableX *ht = &cr->m_sortByDateTable;
	// get the list we read into
	RdbList *list = &cr->m_timedbList;
	// all done if empty
	if ( list->isEmpty() ) {
		cr->m_inProgress = false;
		// note it
		if ( g_conf.m_logDebugTimedb )
			log("timedb: done populating timedb table for %s",
			    cr->m_coll);
		return true;
	}
	// need this
	time_t nowGlobal = getTimeGlobal();
	key128_t k;
	// loop over entries in list
	for (list->resetListPtr();!list->isExhausted();
	     list->skipCurrentRecord()){
		// get the full 128 bit key
		list->getCurrentKey(&k);
		// show it
		//if ( g_conf.m_logDebugTimedb )
		//	log("timedb: key.n1=0x%"XINT64" n0=0x%"XINT64"",
		//	    k.n1,k.n0);
		// use this
		addTimedbKey ( &k , nowGlobal , ht );
	}
	// advance to next rec in timedb
	cr->m_timedbStartKey = *(key128_t *)list->getLastKey();
	cr->m_timedbStartKey += (uint32_t) 1;
	// watch out for wrap around
	if ( cr->m_timedbStartKey < *(key128_t *)list->getLastKey() ) {
		cr->m_inProgress = false;
		// note it
		if ( g_conf.m_logDebugTimedb )
			log("timedb: done populating timedb table 2 for %s",
			    cr->m_coll);
	}
	// free that memory
	cr->m_timedbList.reset();
	return true;
}

// returns false and sets g_errno on error
bool addTimedbKey ( key128_t *kp , uint32_t nowGlobal , HashTableX *ht ) {
	// get start time for this event time interval
	uint32_t etime = g_timedb.getEndTime32 ( kp );
	// . skip if end time is in the past
	// . no, add it anyway so we can show expired events in the
	//   search results if you want to see them
	//if ( etime < nowGlobal ) return true;
	// get start time and next start time (0 means none)
	uint32_t stime   = g_timedb.getStartTime32     ( kp );
	uint64_t docId   = g_timedb.getDocId           ( kp );
	uint16_t eventId = g_timedb.getEventId         ( kp );
	// put eventid on at top
	uint64_t key64 = eventId;
	// shift up for docid bits
	key64 <<= NUMDOCIDBITS; // 38
	// or that in
	key64 |= docId;
	// . make a 32 bit hash of docid and eventid
	// . NO! i got collisions with 32 bits!!!
	//uint32_t key32 = docId ;
	// just in case eventid was > 255
	//if ( eventId > 255 ) {
	//	key64 ^= (uint32_t)g_hashtab[eventId&0xff][0];
	//	key32 ^= (uint32_t)g_hashtab[eventId>>8  ][1];
	//}
	//else {
	//	key32 ^= (uint32_t)g_hashtab[eventId][0];
	//}
	// are we a negativ/delete key?
	bool isDelete = (((char *)kp)[0] & 0x01) == 0x00;
	// lookup in hashtable to see if we got one already
	TimeSlot *old = (TimeSlot *)ht->getValue(&key64);
	if ( ! old ) {
		// if a delete, bail
		if ( isDelete ) {
			if ( g_conf.m_logDebugTimedb )
				log("timedb: missed delete "
				    "key docid=%012"UINT64" evid=%03"INT32" "
				    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32"",
				    docId,(int32_t)eventId,stime,etime,
				    g_timedb.getNextStartTime32 ( kp ) );
			return true;
		}
		// make the data
		TimeSlot ts;
		ts.m_startTime     = stime;
		ts.m_endTime       = etime;
		ts.m_nextStartTime = g_timedb.getNextStartTime32 ( kp );
		// note it for debug
		if ( g_conf.m_logDebugTimedb )
			log("timedb: adding key docid=%012"UINT64" evid=%03"INT32" "
			    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32" key=%"UINT64"",
			    docId,(int32_t)eventId,stime,etime,
			    ts.m_nextStartTime,(uint64_t)key64);
		// otherwise, good to add it
		return ht->addKey(&key64,&ts);
	}
	// if a delete and we do not match forget it
	if ( isDelete ) {
		// if we don't match for get it!
		if ( old->m_endTime   != etime ||
		     old->m_startTime != stime ) {
			if ( g_conf.m_logDebugTimedb )
				log("timedb: unmatched delete "
				    "key docid=%012"UINT64" evid=%03"INT32" "
				    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32" key=%"UINT64"",
				    docId,(int32_t)eventId,stime,etime,
				    g_timedb.getNextStartTime32 ( kp ) ,
				    (uint64_t)key64);
			return true;
		}
		// note it for debug
		if ( g_conf.m_logDebugTimedb )
			log("timedb: removing key docid=%012"UINT64" evid=%03"INT32" "
			    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32" key=%"UINT64"",
			    docId,(int32_t)eventId,stime,etime,
			    g_timedb.getNextStartTime32 ( kp ) ,
			    (uint64_t)key64);
		// . ok, nuke it i guess that was it
		// . PROBLEM: revdb negative keys are added after the latest 
		//   timedb keys for a doc, so if the best time was deleted
		//   because event changed times, then it will no longer have
		//   any time in this table!
		ht->removeKey(&key64);
		return true;
	}

	// if time currently in there is "better" keep going
	if ( // so if it is not over yet...
	    old->m_endTime >= nowGlobal &&
	    // and it started before stime
	    old->m_startTime < stime ) {
		// note that we failed
		if ( g_conf.m_logDebugTimedb )
			log("timedb: tossing key docid=%012"UINT64" evid=%03"INT32" "
			    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32" "
			    "oldstart=%"UINT32" oldend=%"UINT32" key=%"UINT64"",
			    docId,(int32_t)eventId,stime,etime,
			    g_timedb.getNextStartTime32 ( kp ) ,
			    old->m_startTime,
			    old->m_endTime,
			    (uint64_t)key64);
		// then keep it
		return true;
	}
	// if tied, ignore it. do not print out for now...
	if ( old->m_startTime == stime && old->m_endTime == etime )
		return true;

	// if same event has two overlapping time intervals that is bad,
	// but it happened for
	// http://www.1-800-volunteer.org/1800Vol/JVOC/VCContentAction.do?
	// title=28171
	if ( old->m_startTime == stime && old->m_endTime < etime ) {
		// note that we failed
		if ( g_conf.m_logDebugTimedb )
			log("timedb: tossing2 key docid=%012"UINT64" evid=%03"INT32" "
			    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32" "
			    "oldstart=%"UINT32" oldend=%"UINT32" key=%"UINT64"",
			    docId,(int32_t)eventId,stime,etime,
			    g_timedb.getNextStartTime32 ( kp ) ,
			    old->m_startTime,
			    old->m_endTime,
			    (uint64_t)key64);
		// then keep it
		return true;
	}

	// log the update
	if ( g_conf.m_logDebugTimedb )
		log("timedb: updating key docid=%012"UINT64" evid=%03"INT32" "
		    "start=%"UINT32" end=%"UINT32" nxtstr=%"UINT32" oldstart=%"UINT32" oldend=%"UINT32" "
		    "key=%"UINT64"",
		    docId,(int32_t)eventId,stime,etime,
		    g_timedb.getNextStartTime32 ( kp ) ,
		    old->m_startTime,
		    old->m_endTime,
		    (uint64_t)key64);
	// otherwise, we replace it
	old->m_startTime     = stime;
	old->m_endTime       = etime;
	old->m_nextStartTime = g_timedb.getNextStartTime32 ( kp );
	return true;
}

// returns false with g_errno set on error. returns true on success.
bool addTmpTimeList ( RdbList *list , 
		      HashTableX *ht , 
		      time_t fakeNowGlobal ,
		      int32_t niceness ) {
	// need this
	key128_t k;
	// loop over entries in list
	for (list->resetListPtr();!list->isExhausted();
	     list->skipCurrentRecord()){
		// breathe
		QUICKPOLL(niceness);
		// get the full 128 bit key
		list->getCurrentKey(&k);
		// use this. returns false with g_errno set on error
		if ( ! addTimedbKey ( &k , fakeNowGlobal , ht ) ) return false;
	}
	return true;
}

bool compareTimeTables ( HashTableX *ht1 , HashTableX *ht2 , 
			 uint32_t now ) {

	// scan all timeslots in ht1
	for ( int32_t i = 0 ; i < ht1->m_numSlots ; i++ ) {
		// skip if empty
		if ( ! ht1->m_flags[i] ) continue;
		// get it
		TimeSlot *ts1 = (TimeSlot *)ht1->getValueFromSlot ( i );
		// get key
		key128_t k1 = *(key128_t *)ht1->getKeyFromSlot ( i );
		// skip if event over
		if ( ts1->m_startTime < now ) continue;
		// now see if in ht2
		TimeSlot *ts2 = (TimeSlot *)ht2->getValue ( &k1 );
		// must be there
		if ( ! ts2 ) { char *xx=NULL;*xx=0; }
		// must agree
		if ( ts1->m_startTime != ts2->m_startTime ||
		     ts1->m_endTime != ts2->m_endTime ||
		     ts1->m_nextStartTime != ts2->m_nextStartTime ) {
			char *xx=NULL;*xx=0; }
	}

	return true;
}
