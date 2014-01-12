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
	m_inRebalanceLoop = false;
	m_numForeignRecs = 0;
	// reset 
	m_rdbNum = 0;
	m_collnum = 0;
	KEYMIN ( m_nextKey , MAX_KEY_BYTES );
	KEYMIN ( m_endKey , MAX_KEY_BYTES );
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
	if ( ! g_hostdb.m_hostsConfInAgreement ) return NULL;

	// for simplicty,  only gb shards on stripe 0 should run this i guess
	if ( g_hostdb.m_myHost->m_stripe != 0 ) {
		m_needsRebalanceValid = true;
		m_needsRebalance = false;
		return &m_needsRebalance;
	}

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
	unsigned long x = 0;
	long y = 0;
	long rebalancing = 0;
	long cn;
	// parse the file
	char keyStr[128];
	sscanf(sb.getBufStart(),
	       "myshard: %li\n"
	       "numshards: %li\n"
	       "rebalancing: %li\n"
	       "collnum: %li\n"
	       "rdbnum: %li\n"
	       "nextkey: %s\n",
	       &x,
	       &y,
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
	// we are valid now either way
	m_needsRebalanceValid = true;
	// assume ok
	m_needsRebalance = false;
	// if hosts.conf is different and we are part of a different 
	// shard then we must auto scale
	if ( x != g_hostdb.m_myHost->m_shardNum ) m_needsRebalance = true;
	if ( y != g_hostdb.m_numShards          ) m_needsRebalance = true;
	if ( rebalancing                        ) m_needsRebalance = true;

	// and we don't need user consent, they already did last time
	if ( rebalancing ) {
		m_warnedUser   = true;
		m_userApproved = true;
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
		// scan all rdbs in that collection
		for ( ; m_rdbNum < g_process.m_numRdbs ; m_rdbNum++ ) {
			// skip if not good
			Rdb *rdb = g_process.m_rdbs[m_rdbNum];
			// not an RDB2
			if ( rdb->isSecondaryRdb() ) continue;
			// scan it. returns true if done, false if blocked
			if ( ! scanRdb ( ) ) return;
		}
	}

	// all done
	m_isScanning     = false;
	m_needsRebalance = false;

	// save the file then, but with these stats:
	m_collnum = 0;
	m_rdbNum = 0;
	KEYMIN(m_nextKey,MAX_KEY_BYTES);

	saveRebalanceFile();
}

bool Rebalance::saveRebalanceFile ( ) {

	char keyStr[128];
	// convert m_nextKey 
	binToHex ( (unsigned char *)&m_nextKey , MAX_KEY_BYTES , keyStr );

	SafeBuf sb;
	sb.safePrintf (
		       "myshard: %li\n"
		       "numshards: %li\n"
		       "rebalancing: %li\n"
		       "collnum: %li\n"
		       "rdbnum: %li\n"
		       "nextkey: %s\n",
		       (long)g_hostdb.m_myHost->m_shardNum,
		       (long)g_hostdb.m_numShards,
		       // were we rebalancing last time?
		       (long)m_isScanning,
		       // how far did we get?
		       (long)m_collnum,
		       (long)m_rdbNum,
		       keyStr
		       );

	return sb.save ( g_hostdb.m_dir , "rebalance.txt" );
}

static void gotListWrapper ( void *state , RdbList *list, Msg5 *msg5 ) {
	// this never blocks
	g_rebalance.gotList();
	// init another rdb scan pass
	g_rebalance.scanLoop();
}

bool Rebalance::scanRdb ( ) {

	// get collrec i guess
	CollectionRec *cr = g_collectiondb.m_recs[m_collnum];

	Rdb *rdb = g_process.m_rdbs[m_rdbNum];

	// skip empty collrecs, unless like statsdb or something
	if ( ! cr && ! rdb->m_isCollectionLess ) return true;

	char *coll = NULL;
	if ( cr ) coll = cr->m_coll;

 readAnother:

	if ( ! m_msg5.getList ( rdb->m_rdbId     ,
				coll             ,
				&m_list          ,
				m_nextKey        ,
				m_endKey         , // should be maxed!
				1024             , // min rec sizes
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

	// process that list
	gotList();

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
void Rebalance::gotList ( ) {

	Rdb *rdb = g_process.m_rdbs[m_rdbNum];

	char rdbId = rdb->m_rdbId;

	long keySize = rdb->m_ks;//getKeySize();

	long myShard = g_hostdb.m_myHost->m_shardNum;

	m_list.resetListPtr();

	for ( ; ! m_list.isExhausted() ; m_list.skipCurrentRec() ) {
		// get tht rec
		char *rec = m_list.getCurrentRec();
		// get shard
		long shard = getShardNum ( rdbId , rec );
		// skip it if it belongs with us
		if ( shard == myShard ) continue;
		// otherwise, it does not!
		//long recSize = m_list.getCurrentRecSize();
		// copy the full key into "key" buf because might be compressed
		char key[MAX_KEY_BYTES];
		m_list.getCurrentKey ( key );
		// store rdbid
		m_posMetaList.pushChar ( rdbId );
		// first key
		m_posMetaList.safeMemcpy ( key , keySize );
		// then record
		long dataSize = rdb->m_fixedDataSize;
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
		// store rdbid
		m_negMetaList.pushChar ( rdbId );
		// make key a delete
		key[0] &= 0xfe;
		// and store that negative key
		m_posMetaList.safeMemcpy ( key , keySize );
	}

	if ( ! m_blocked ) { char *xx=NULL;*xx=0; }

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

	if ( m_blocked ) return;

	scanLoop();
}
