// Matt Wells, 2014

// TODO: if a host is being removed put # removed after it like we do # retired
//       and we can at least load it up and it will move its records to the 
//       new guys.


#include "Rebalance.h"

#include "gb-include.h"

#include "Rdb.h"
#include "Spider.h"
#include "Msg4.h"
#include "Pages.h"
#include "PingServer.h"
#include "Spider.h"
#include "Process.h"
#include "Parms.h"

Rebalance g_rebalance;

Rebalance::Rebalance ( ) {
	m_registered = false;
	m_allowSave = false;
	//m_inRebalanceLoop = false;
	m_numForeignRecs = 0;
	m_rebalanceCount = 0LL;
	m_scannedCount = 0LL;
	// reset 
	m_rdbNum = 0;
	m_collnum = 0;
	m_lastCollnum = -1;
	m_lastRdb = NULL;
	m_lastPercent = -1;
	KEYMIN ( m_nextKey , MAX_KEY_BYTES );
	KEYMAX ( m_endKey , MAX_KEY_BYTES );
	m_needsRebalanceValid = false;
	m_needsRebalance = false;
	m_warnedUser = false;
	m_userApproved = false;
	m_isScanning = false;
	m_blocked = 0;
}

// . returns NULL if we don't know yet if we need to rebalance
// . otherwise returns ptr to the bool we want
char *Rebalance::getNeedsRebalance ( ) {

	if ( m_needsRebalanceValid )
		return &m_needsRebalance;

	// wait for collections and parms to be in sync. new hosts won't
	// have the collection recs and subdirs...
	if ( ! g_parms.m_inSyncWithHost0 ) return NULL;

	// wait for all hosts to agree
	if ( ! g_pingServer.m_hostsConfInAgreement ) return NULL;

	// for simplicty,  only gb shards on stripe 0 should run this i guess
	if ( g_hostdb.m_myHost->m_stripe != 0 ) {
		m_needsRebalanceValid = true;
		m_needsRebalance = false;
		return &m_needsRebalance;
	}

	// allow it to save to file now that we have almost had a chance to
	// load in case it cores at startup and overwrites the file!!
	m_allowSave = true;

	// the last time we loaded hosts.conf we saved the checksum.
	// if it changed we should think about auto-scaling. spidering
	// can only occur once g_hostdb.m_hostsConfInAgreement is true.
	SafeBuf sb;
	sb.load ( g_hostdb.m_dir , "rebalance.txt");

	// if we did not note any foreign recs, and file not there, save it
	// so next time we startup we can tell if hosts.conf changed
	if ( sb.length() <= 1 && m_numForeignRecs == 0 ) {
		// save it!
		saveRebalanceFile();
		// assume we do not need a rebalance
		m_needsRebalanceValid = true;
		m_needsRebalance = false;
		return &m_needsRebalance;
	}

	// if does not exist, do the scan if we noted some foreign recs
	if ( sb.length() <= 10 ) {
		// we need it!
		m_needsRebalanceValid = true;
		m_needsRebalance = true;
		return &m_needsRebalance;
	}

	// otherwise, get data from the file if it is there
	// we are shard #x of a total of y. if that changed or 
	// crc-saved.dat does not exist we have to scan. we can 
	// periodically save our scan progress in case we get shutdown
	// mid-stream i guess.
	int32_t x = 0;
	int32_t y = 0;
	int32_t z = 0;
	int32_t rebalancing = 0;
	int32_t cn;
	// parse the file
	char keyStr[128];
	sscanf(sb.getBufStart(),
	       "myshard: %"INT32"\n"
	       "numshards: %"INT32"\n"
	       "numhostspershard: %"INT32"\n"
	       "rebalancing: %"INT32"\n"
	       "collnum: %"INT32"\n"
	       "rdbnum: %"INT32"\n"
	       "nextkey: %s\n",
	       &x,
	       &y,
	       &z,
	       // were we rebalancing last time?
	       &rebalancing,
	       // how far did we get?
	       &cn,
	       &m_rdbNum,
	       keyStr
	       );

	// convert m_nextKey into an ascii string and store into keyStr
	hexToBin(keyStr,gbstrlen(keyStr), (char *)&m_nextKey);

	m_collnum = cn;
	//m_collnum = 4695; //debug skip

	//m_collnum = 18101; // just global index for now

	// we are valid now either way
	m_needsRebalanceValid = true;
	// assume ok
	m_needsRebalance = false;
	// if hosts.conf is different and we are part of a different 
	// shard then we must auto scale
	if ( x != (int32_t)g_hostdb.m_myHost->m_shardNum) m_needsRebalance = true;
	if ( y != g_hostdb.m_numShards          ) m_needsRebalance = true;
	if ( z != g_hostdb.getNumHostsPerShard()) m_needsRebalance = true;
	if ( rebalancing                        ) m_needsRebalance = true;

	// how can this be?
	if ( m_numForeignRecs ) m_needsRebalance = true;

	// and we don't need user consent, they already did last time
	if ( rebalancing ) {
		// this was causing a core from starting too early!
		//m_warnedUser   = true;
		//m_userApproved = true;
	}

	return &m_needsRebalance;
}

// . this is called every 500ms from Process.cpp
// . if all pings came in and all hosts have the same hosts.conf
//   and if we detected any shard imbalance at startup we have to
//   scan all rdbs for records that don't belong to us and send them
//   where they should go
void Rebalance::rebalanceLoop ( ) {

	// once this knows, it returns right away. so it is super fast
	char *np = getNeedsRebalance();
	// if we don't know yet, this np is NULL
	if ( ! np ) return;
	// if we do not need to rebalance just return
	if ( *np == 0 ) return;

	// note in log
	if ( ! m_warnedUser ) {
		m_warnedUser = true;
		log("db: CRITICAL. please click on the rebalance "
		    "link in master controls");
	}

	// . ok, we need to rebalance
	// . require user to push the rebalance link to MAKE SURE!!
	// . if re-starting in the middle of a prior rebalance we should
	//   have set this to true automatically above so we do not require
	//   approval each time a host is restarted
	if ( ! m_userApproved ) return;

	// if already scanning, we are good, just bail. check this since
	// we are called from Process.cpp every 500 ms
	if ( m_isScanning ) return;

	// ok, flag it has officially scanning now
	m_isScanning = true;

	// start scanning
	scanLoop();
}

void Rebalance::scanLoop ( ) {

	// scan all rdbs in each coll
	for ( ; m_collnum < g_collectiondb.m_numRecs ; m_collnum++ ) {
		// get collrec i guess
		CollectionRec *cr = g_collectiondb.m_recs[m_collnum];
		// skip if none... like statsdb, i guess don't rebalance!!
		if ( ! cr ) continue;

		// only global index for now
		//if ( m_collnum != 18101 ) continue;

		// new?
		//if ( m_lastCollnum != m_collnum ) {
		//	log("rebal: rebalancing %s", cr->m_coll);
		//	m_lastCollnum = m_collnum;
		//}

		// scan all rdbs in that collection
		for ( ; m_rdbNum < g_process.m_numRdbs ; m_rdbNum++ ) {
			// skip if not good
			Rdb *rdb = g_process.m_rdbs[m_rdbNum];
			// not an RDB2
			if ( rdb->isSecondaryRdb() ) continue;
			// or if uninitialized
			if ( ! rdb->isInitialized() ) continue;
			// skip statsdb, do not rebalance that
			if ( rdb->m_rdbId == RDB_STATSDB ) continue;
			// only tagdb for now
			//if ( rdb->m_rdbId != RDB_TAGDB ) continue;
			// log it as well
			if ( m_lastRdb != rdb ) {
				log("rebal: scanning %s (%"INT32") [%s]",
				    cr->m_coll,(int32_t)cr->m_collnum,
				    rdb->m_dbname);
				// only do this once per rdb/coll
				m_lastRdb = rdb;
				// reset key cursor as well!!!
				KEYMIN ( m_nextKey , MAX_KEY_BYTES );

				// This logic now in RdbBase.cpp.
				// let's keep posdb and titledb tight-merged so
				// we do not run out of disk space because we 
				// will be dumping tons of negative recs
				//RdbBase *base = rdb->getBase(m_collnum);
				//base->m_savedMin = base->m_minFilesToMerge;
				//base->m_minFilesToMerge = 2;

			}
			// percent update?
			int32_t percent = (unsigned char)m_nextKey[rdb->m_ks-1];
			percent *= 100;
			percent /= 256;
			if ( percent != m_lastPercent && percent ) {
				log("rebal: %"INT32"%% complete",percent);
				m_lastPercent = percent;
			}
			// scan it. returns true if done, false if blocked
			if ( ! scanRdb ( ) ) return;
			// note it
			log("rebal: moved %"INT64" of %"INT64" recs scanned in "
			    "%s for coll.%s.%"INT32"",
			    m_rebalanceCount,m_scannedCount,
			    rdb->m_dbname,cr->m_coll,(int32_t)cr->m_collnum);
			//if ( m_rebalanceCount ) goto done;
			m_rebalanceCount = 0;
			m_scannedCount = 0;
			m_lastPercent = -1;

			// This logic now in RdbBase.cpp.
			// go back to normal merge threshold
			//RdbBase *base = rdb->getBase(m_collnum);
			//base->m_minFilesToMerge = base->m_savedMin;

		}
		// reset it for next colls
		m_rdbNum = 0;
	}

	// done:
	// all done
	m_isScanning     = false;
	m_needsRebalance = false;

	// get rid of the 'F' flag in PageHosts.cpp
	m_numForeignRecs = 0;

	// save the file then, but with these stats:
	m_collnum = 0;
	m_rdbNum = 0;
	KEYMIN(m_nextKey,MAX_KEY_BYTES);

	log("rebal: done rebalancing all collections. "
	    "Saving rebalance.txt.");

	saveRebalanceFile();
}

bool Rebalance::saveRebalanceFile ( ) {

	if ( ! m_allowSave ) return true;

	char keyStr[128];
	// convert m_nextKey 
	binToHex ( (unsigned char *)&m_nextKey , MAX_KEY_BYTES , keyStr );

	//log("db: saving rebalance.txt");
	char tmp[30000];
	SafeBuf sb(tmp,30000);
	sb.safePrintf (
		       "myshard: %"INT32"\n"
		       "numshards: %"INT32"\n"
		       "numhostspershard: %"INT32"\n"
		       "rebalancing: %"INT32"\n"
		       "collnum: %"INT32"\n"
		       "rdbnum: %"INT32"\n"
		       "nextkey: %s\n",
		       (int32_t)g_hostdb.m_myHost->m_shardNum,
		       (int32_t)g_hostdb.m_numShards,
		       (int32_t)g_hostdb.getNumHostsPerShard(),
		       // were we rebalancing last time?
		       (int32_t)m_isScanning,
		       // how far did we get?
		       (int32_t)m_collnum,
		       (int32_t)m_rdbNum,
		       keyStr
		       );

	return sb.save ( g_hostdb.m_dir , "rebalance.txt" );
}

static void gotListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) {
	// . this can block if a msg4 blocks, in which case it returns false
	// . when its msg4 callback is called it calls scanLoop() from there
	if ( ! g_rebalance.gotList() ) return;
	// init another rdb scan pass
	g_rebalance.scanLoop();
}

void sleepWrapper ( int fd , void *state ) {
	// try a re-call since we were merging last time
	g_rebalance.scanLoop();
}

bool Rebalance::scanRdb ( ) {

	// get collrec i guess
	//CollectionRec *cr = g_collectiondb.m_recs[m_collnum];

	Rdb *rdb = g_process.m_rdbs[m_rdbNum];

	// unregister it if it was registered
	if ( m_registered ) {
		g_loop.unregisterSleepCallback ( NULL,sleepWrapper );
		m_registered = false;
	}

	if ( g_process.m_mode == EXIT_MODE ) return false;

	// . if this rdb is merging wait until merge is done
	// . we will be dumping out a lot of negative recs and if we are
	//   int16_t on disk space we need to merge them in immediately with
	//   all our data so that they annihilate quickly with the positive
	//   keys in there to free up more disk
	RdbBase *base = rdb->getBase ( m_collnum );
	// base is NULL for like monitordb...
	if ( base && base->isMerging() ) {
		log("rebal: waiting for merge on %s for coll #%"INT32" to complete",
		    rdb->m_dbname,(int32_t)m_collnum);
		g_loop.registerSleepCallback ( 1000,NULL,sleepWrapper,1);
		m_registered = true;
		// we blocked, return false
		return false;
	}
	// or really if any merging is going on way for it to save disk space
	if ( rdb->isMerging() ) {
		log("rebal: waiting for merge on %s for coll ??? to complete",
		    rdb->m_dbname);
		g_loop.registerSleepCallback ( 1000,NULL,sleepWrapper,1);
		m_registered = true;
		// we blocked, return false
		return false;
	}


	// skip empty collrecs, unless like statsdb or something
	//if ( ! cr && ! rdb->m_isCollectionLess ) return true;

	//char *coll = cr->m_coll;

 readAnother:

	if ( g_process.m_mode == EXIT_MODE ) return false;

	//log("rebal: loading list start = %s",KEYSTR(m_nextKey,rdb->m_ks));

	if ( ! m_msg5.getList ( rdb->m_rdbId     ,
				m_collnum, // coll             ,
				&m_list          ,
				m_nextKey        ,
				m_endKey         , // should be maxed!
				100024             , // min rec sizes
				true             , // include tree?
				false            , // includeCache
				false            , // addToCache
				0                , // startFileNum
				-1               , // m_numFiles   
				this             , // state 
				gotListWrapper   , // callback
				MAX_NICENESS     , // niceness
				true             , // do error correction?
				NULL             , // cache key ptr
				0                , // retry num
				-1               , // maxRetries
				true             , // compensate for merge
				-1LL             , // sync point
				NULL         ))
		return false;

	//
	// msg5 did not block on i/o if we made it here
	//

	// all done if list empty
	if ( m_list.isEmpty() ) return true;

	// process that list. return false if blocked.
	if ( ! gotList() ) return false;

	// get another list
	goto readAnother;
}

static void doneAddingMetaWrapper ( void *state ) {
	if ( g_rebalance.m_blocked <= 0 ) { char *xx=NULL;*xx=0; }
	g_rebalance.m_blocked--;
	// wait for other msg4 add to complete
	if ( g_rebalance.m_blocked > 0 ) return;
	// ok, both msg4s are done, resume
	g_rebalance.scanLoop();
}

// scan that list
bool Rebalance::gotList ( ) {

	if ( m_blocked ) { char *xx=NULL;*xx=0; }

	Rdb *rdb = g_process.m_rdbs[m_rdbNum];

	char rdbId = rdb->m_rdbId;

	int32_t ks = rdb->m_ks;//getKeySize();

	int32_t myShard = g_hostdb.m_myHost->m_shardNum;

	m_list.resetListPtr();

	//log("rebal: got list of %"INT32" bytes",m_list.getListSize());

	m_posMetaList.reset();
	m_negMetaList.reset();

	if ( m_list.isEmpty() ) {
	        KEYSET ( m_nextKey , m_endKey , ks );
		return true;
	}

	//char *last = NULL;

	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRec() ) {
		// get tht rec
		//char *rec = m_list.getCurrentRec();
		// get it
		m_list.getCurrentKey  ( m_nextKey );
		// skip if negative... wtf?
		if ( KEYNEG(m_nextKey) ) continue;
		// get shard
		int32_t shard = getShardNum ( rdbId , m_nextKey );
		// save last ptr
		//last = rec;
		// debug!
		//log("rebal: checking key %s",KEYSTR(m_nextKey,ks));
		// count as scanned
		m_scannedCount++;
		// skip it if it belongs with us
		if ( shard == myShard ) continue;
		// note it
		//log("rebal: shard is %"INT32"",shard);
		// count it
		m_rebalanceCount++;
		// otherwise, it does not!
		//int32_t recSize = m_list.getCurrentRecSize();
		// copy the full key into "key" buf because might be compressed
		char key[MAX_KEY_BYTES];
		m_list.getCurrentKey ( key );
		// store rdbid, no! we supply rdbid below to msg4
		//m_posMetaList.pushChar ( rdbId );
		// first key
		m_posMetaList.safeMemcpy ( key , ks );
		// then record
		int32_t dataSize = rdb->m_fixedDataSize;
		if ( rdb->m_fixedDataSize == -1 ) {
			dataSize = m_list.getCurrentDataSize();
			m_posMetaList.pushLong ( dataSize );
		}
		// then data
		if ( dataSize ) {
			char *data = m_list.getCurrentData();
			m_posMetaList.safeMemcpy ( data , dataSize );
		}
		//
		// NOW DELETE FROM OUR SHARD!
		//
		// store rdbid, no! we supply rdbid below to msg4
		//m_negMetaList.pushChar ( rdbId );
		// make key a delete
		key[0] &= 0xfe;
		// for debug...
		//log("rebal: rm key %s",KEYSTR(key,ks));
		// and store that negative key
		m_negMetaList.safeMemcpy ( key , ks );
	}

	//log("rebal: done reading list");

	//  update nextkey
	//if ( last ) {
	if ( ! m_list.isEmpty() ) {
		// get the last key we scanned, all "ks" bytes of it.
		// because some keys are compressed and we take the
		// more significant compressed out bytes from m_list.m_*
		// member vars
		//m_list.getKey  ( last , m_nextKey );
		// if it is not maxxed out, then incremenet it for the
		// next scan round
		if ( KEYCMP ( m_nextKey , KEYMAX() , ks ) != 0 )
			KEYADD ( m_nextKey , 1 , ks );
	}
	//else {
	//	log("rebal: got empty list");
	//}

	if ( ! m_msg4a.addMetaList ( &m_posMetaList ,
				     m_collnum ,
				     this ,
				     doneAddingMetaWrapper ,
				     MAX_NICENESS ,
				     rdb->m_rdbId ,
				     -1 ) ) // shard override, not!
		m_blocked++;


	if ( ! m_msg4b.addMetaList ( &m_negMetaList ,
				     m_collnum ,
				     this ,
				     doneAddingMetaWrapper ,
				     MAX_NICENESS ,
				     rdb->m_rdbId ,
				     myShard ) ) // shard override, not!
		m_blocked++;

	if ( m_blocked ) return false;

	return true;
}
